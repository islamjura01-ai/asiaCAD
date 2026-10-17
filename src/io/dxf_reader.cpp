#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/utf8.h>
#include <asiacad/io/dxf_reader.h>
#include <asiacad/io/dxf_text.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_map>

namespace asiacad {

using dxftext::Codepage;

std::string_view dxfReleaseName(std::string_view v) {
    static const std::pair<std::string_view, std::string_view> m[] = {
        {"AC1006", "R10"},   {"AC1009", "R12"},   {"AC1012", "R13"},   {"AC1014", "R14"},   {"AC1015", "R2000"},
        {"AC1018", "R2004"}, {"AC1021", "R2007"}, {"AC1024", "R2010"}, {"AC1027", "R2013"}, {"AC1032", "R2018"},
    };
    for (const auto& [k, n] : m)
        if (k == v) return n;
    return v.empty() ? std::string_view("?") : v;
}

std::string_view insUnitsName(int code) {
    switch (code) {
        case 1: return "in";
        case 2: return "ft";
        case 3: return "mi";
        case 4: return "mm";
        case 5: return "cm";
        case 6: return "m";
        case 7: return "km";
        default: return "-";
    }
}

namespace {

// ============================================================ group-kod oqimi
struct Group {
    int code;
    std::string_view value;
    std::size_t line;
};

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::vector<Group> tokenize(std::string_view text) {
    std::vector<Group> out;
    out.reserve(text.size() / 12);
    std::size_t pos = 0, line = 0;
    auto nextLine = [&](std::string_view& l) {
        if (pos >= text.size()) return false;
        std::size_t e = text.find('\n', pos);
        if (e == std::string_view::npos) e = text.size();
        l = text.substr(pos, e - pos);
        if (!l.empty() && l.back() == '\r') l.remove_suffix(1);
        pos = e + 1;
        ++line;
        return true;
    };
    std::string_view codeLine, valueLine;
    while (nextLine(codeLine)) {
        const std::string_view c = trim(codeLine);
        const std::size_t codeLineNo = line;
        if (c.empty() && pos >= text.size()) break;  // fayl oxiridagi bo'sh qator
        int code = 0;
        auto [p, ec] = std::from_chars(c.data(), c.data() + c.size(), code);
        if (ec != std::errc{} || p != c.data() + c.size()) throw Error("dxf.err.syntax", {std::to_string(codeLineNo)});
        if (!nextLine(valueLine)) break;
        out.push_back({code, valueLine, codeLineNo});
    }
    return out;
}

// ============================================================ xom entity
struct Raw {
    std::string_view type;
    std::vector<Group> g;
    std::size_t line = 0;

    std::optional<std::string_view> find(int code) const {
        for (const auto& x : g)
            if (x.code == code) return x.value;
        return std::nullopt;
    }
    double num(int code, double def = 0.0) const {
        auto v = find(code);
        if (!v) return def;
        const auto t = trim(*v);
        double d = def;
        auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), d);
        return ec == std::errc{} ? d : def;
    }
    int inum(int code, int def = 0) const {
        auto v = find(code);
        if (!v) return def;
        const auto t = trim(*v);
        long long d = def;
        auto [p, ec] = std::from_chars(t.data(), t.data() + t.size(), d);
        if (ec == std::errc{}) return static_cast<int>(d);
        return static_cast<int>(num(code, def));  // ba'zi eksportchilar 70 ga "1.0" yozadi
    }
    bool has(int code) const { return find(code).has_value(); }
    Vec3 pt(int base, Vec3 def = {}) const {
        if (!has(base) && !has(base + 10)) return def;
        return {num(base), num(base + 10), num(base + 20)};
    }
};

double parseNum(std::string_view v) {
    const auto t = trim(v);
    double d = 0;
    std::from_chars(t.data(), t.data() + t.size(), d);
    return d;
}

// i dan boshlab code-0 guruhlarni Raw'larga yig'adi; `stop` turida to'xtaydi (u iste'mol qilinmaydi)
std::vector<Raw> collect(const std::vector<Group>& gs, std::size_t& i, std::string_view stop) {
    std::vector<Raw> out;
    while (i < gs.size()) {
        if (gs[i].code != 0) { ++i; continue; }
        const std::string_view t = trim(gs[i].value);
        if (t == stop || t == "EOF") break;
        Raw r;
        r.type = t;
        r.line = gs[i].line;
        ++i;
        while (i < gs.size() && gs[i].code != 0) r.g.push_back(gs[i++]);
        out.push_back(std::move(r));
    }
    return out;
}

struct Node {
    Raw main;
    std::vector<Raw> sub;  // VERTEX yoki ATTRIB
};

std::vector<Node> groupNodes(std::vector<Raw>& raws) {
    std::vector<Node> out;
    for (std::size_t i = 0; i < raws.size(); ++i) {
        Node n{std::move(raws[i]), {}};
        const bool poly = n.main.type == "POLYLINE";
        const bool ins = n.main.type == "INSERT" && n.main.inum(66) == 1;
        if (poly || ins) {
            const std::string_view child = poly ? "VERTEX" : "ATTRIB";
            while (i + 1 < raws.size() && raws[i + 1].type == child) n.sub.push_back(std::move(raws[++i]));
            if (i + 1 < raws.size() && raws[i + 1].type == "SEQEND") ++i;
        } else if (n.main.type == "VERTEX" || n.main.type == "SEQEND" || n.main.type == "ATTRIB") {
            continue;  // yetim yozuvlar
        }
        out.push_back(std::move(n));
    }
    return out;
}

struct Block {
    std::string name;
    Vec3 base;
    int flags = 0;
    std::vector<Node> nodes;
};

// ============================================================ OCS (Arbitrary Axis Algorithm)
struct Ocs {
    Vec3 n{0, 0, 1}, ax{1, 0, 0}, ay{0, 1, 0};
    bool plusZ = true, minusZ = false;

    Vec3 toWcs(const Vec3& p) const { return ax * p.x + ay * p.y + n * p.z; }
    static Transform minusZTransform() {
        Transform t;
        t.a = -1;
        t.sz = -1;
        return t;
    }
};

Ocs makeOcs(const Raw& r) {
    Ocs o;
    Vec3 n = r.pt(210, {0, 0, 1});
    const double L = n.length();
    if (L < 1e-12) return o;
    n = n / L;
    o.n = n;
    o.plusZ = std::abs(n.x) < 1e-9 && std::abs(n.y) < 1e-9 && n.z > 0;
    o.minusZ = std::abs(n.x) < 1e-9 && std::abs(n.y) < 1e-9 && n.z < 0;
    Vec3 ax = (std::abs(n.x) < 1.0 / 64 && std::abs(n.y) < 1.0 / 64) ? Vec3{0, 1, 0}.cross(n) : Vec3{0, 0, 1}.cross(n);
    ax = ax / ax.length();
    Vec3 ay = n.cross(ax);
    ay = ay / ay.length();
    o.ax = ax;
    o.ay = ay;
    return o;
}

// ============================================================ NURBS
struct H4 {
    double x, y, z, w;
};

std::vector<Vec3> evalSpline(int p, const std::vector<double>& U, const std::vector<Vec3>& P, const std::vector<double>& W,
                             int samples) {
    const int n = static_cast<int>(P.size());
    std::vector<H4> C(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double w = W.size() == P.size() && W[static_cast<std::size_t>(i)] > 0 ? W[static_cast<std::size_t>(i)] : 1.0;
        const auto& q = P[static_cast<std::size_t>(i)];
        C[static_cast<std::size_t>(i)] = {q.x * w, q.y * w, q.z * w, w};
    }
    const double u0 = U[static_cast<std::size_t>(p)], u1 = U[static_cast<std::size_t>(n)];
    std::vector<Vec3> out;
    std::vector<H4> d(static_cast<std::size_t>(p + 1));
    for (int s = 0; s <= samples; ++s) {
        const double x = u0 + (u1 - u0) * s / samples;
        int k = p;
        while (k < n - 1 && x >= U[static_cast<std::size_t>(k + 1)]) ++k;
        for (int j = 0; j <= p; ++j) d[static_cast<std::size_t>(j)] = C[static_cast<std::size_t>(j + k - p)];
        for (int r = 1; r <= p; ++r)
            for (int j = p; j >= r; --j) {
                const double a0 = U[static_cast<std::size_t>(j + k - p)];
                const double a1 = U[static_cast<std::size_t>(j + 1 + k - r)];
                const double alpha = a1 - a0 > 1e-15 ? (x - a0) / (a1 - a0) : 0.0;
                auto& dj = d[static_cast<std::size_t>(j)];
                const auto& dm = d[static_cast<std::size_t>(j - 1)];
                dj = {(1 - alpha) * dm.x + alpha * dj.x, (1 - alpha) * dm.y + alpha * dj.y,
                      (1 - alpha) * dm.z + alpha * dj.z, (1 - alpha) * dm.w + alpha * dj.w};
            }
        const auto& r = d[static_cast<std::size_t>(p)];
        out.push_back(r.w > 1e-15 ? Vec3{r.x / r.w, r.y / r.w, r.z / r.w} : Vec3{r.x, r.y, r.z});
    }
    return out;
}

// ============================================================ konvertor
struct Ctx {
    Transform t;
    bool hasT = false;
    std::string layer = "0";
    int color = kColorByLayer;
    int depth = 0;
    bool def = false;  // blok ta'rifini qurish: "0" qatlam va BYBLOCK rang o'zgarishsiz saqlanadi
};

class Converter {
public:
    Converter(const DxfImportOptions& o, DxfImportResult& r, Codepage cp, const std::unordered_map<std::string, Block>& blocks)
        : opt_(o), res_(r), cp_(cp), blocks_(blocks) {}

    void run(const std::vector<Node>& nodes) {
        convertNodes(nodes, Ctx{});
        auto warn = [&](std::size_t n, const char* key) {
            if (n) res_.warnings.push_back({key, {std::to_string(n)}});
        };
        warn(paper_, "dxf.warn.paperspace");
        warn(inserts_, "dxf.warn.inserts");
        warn(curves_, "dxf.warn.curves");
        warn(nonUniform_, "dxf.warn.nonuniform");
        warn(ocs3d_, "dxf.warn.ocs_3d");
    }
    const std::set<std::string>& usedLayers() const { return usedLayers_; }

    std::string decodeName(std::string_view s) const { return dxftext::decodeUnicodeEscapes(dxftext::toUtf8(trim(s), cp_)); }

private:
    std::string decodeText(std::string_view s) const { return dxftext::decodeTextControlCodes(dxftext::toUtf8(s, cp_)); }

    std::string layerOf(const Raw& r, const Ctx& c) const {
        auto v = r.find(8);
        std::string name = v ? decodeName(*v) : std::string("0");
        if (name.empty()) name = "0";
        if (name == "0" && c.depth > 0) return c.layer;
        return name;
    }
    int colorOf(const Raw& r, const Ctx& c) const {
        int col = r.inum(62, kColorByLayer);
        if (col == 0) return c.def ? kColorByBlock : (c.depth > 0 ? c.color : 7);  // BYBLOCK
        if (col < 0) col = -col;
        if (col > 256) col = kColorByLayer;
        return col;
    }
    void badEntity(const Raw& r) { res_.warnings.push_back({"dxf.warn.bad_entity", {std::string(r.type), std::to_string(r.line)}}); }

    void emit(std::unique_ptr<IEntity> e, const Raw& r, const Ctx& c) {
        if (!e) return;
        e->setLayer(layerOf(r, c));
        e->setColor(colorOf(r, c));
        if (c.hasT) {
            if (!c.t.isConformal() &&
                (e->type() == EntityType::Circle || e->type() == EntityType::Arc ||
                 (e->type() == EntityType::Polyline && static_cast<PolylineEntity*>(e.get())->hasBulges())))
                ++nonUniform_;
            e = transformedCopy(*e, c.t);
        }
        usedLayers_.insert(e->layer());
        if (!c.def) ++res_.imported[std::string(toString(e->type()))];
        out_->push_back(std::move(e));
    }

    // OCS'da qurilgan entity'ni WCS'ga o'tkazadi
    std::unique_ptr<IEntity> place(std::unique_ptr<IEntity> e, const Ocs& o) {
        if (o.plusZ) return e;
        if (o.minusZ) {
            e->transform(Ocs::minusZTransform());
            return e;
        }
        ++ocs3d_;
        if (auto* t = dynamic_cast<TextEntity*>(e.get())) {
            const Vec3 ins = o.toWcs(t->insertion());
            const Vec3 dir = o.toWcs({std::cos(t->rotation()), std::sin(t->rotation()), 0});
            return std::make_unique<TextEntity>(ins, t->height(), t->text(), std::atan2(dir.y, dir.x), t->hAlign(), t->vAlign());
        }
        auto poly = toBulgePolyline(*e);
        if (!poly) return e;
        PolylineEntity flat = poly->tessellated(opt_.maxArcStep);
        std::vector<Vec3> pts;
        for (const auto& p : flat.vertices()) pts.push_back(o.toWcs(p));
        return std::make_unique<PolylineEntity>(std::move(pts), flat.isClosed());
    }

    void convertNodes(const std::vector<Node>& nodes, const Ctx& c) {
        for (const auto& n : nodes) convert(n, c);
    }

    void convert(const Node& n, const Ctx& c) {
        const Raw& r = n.main;
        if (c.depth == 0 && !c.def && r.inum(67) == 1 && !opt_.includePaperSpace) {
            ++paper_;
            return;
        }
        const std::string_view t = r.type;
        if (t == "LINE") {
            emit(std::make_unique<LineEntity>(r.pt(10), r.pt(11)), r, c);
        } else if (t == "POINT") {
            emit(std::make_unique<PointEntity>(r.pt(10)), r, c);
        } else if (t == "CIRCLE") {
            const double rad = r.num(40);
            if (rad <= 0) return badEntity(r);
            emit(place(std::make_unique<CircleEntity>(r.pt(10), rad), makeOcs(r)), r, c);
        } else if (t == "ARC") {
            const double rad = r.num(40);
            if (rad <= 0) return badEntity(r);
            emit(place(std::make_unique<ArcEntity>(r.pt(10), rad, deg2rad(r.num(50)), deg2rad(r.num(51))), makeOcs(r)), r, c);
        } else if (t == "LWPOLYLINE") {
            lwpolyline(r, c);
        } else if (t == "POLYLINE") {
            polyline(n, c);
        } else if (t == "TEXT" || t == "ATTRIB") {
            text(r, c);
        } else if (t == "MTEXT") {
            mtext(r, c);
        } else if (t == "ELLIPSE") {
            ellipse(r, c);
        } else if (t == "SPLINE") {
            spline(r, c);
        } else if (t == "3DFACE") {
            std::vector<Vec3> pts{r.pt(10), r.pt(11), r.pt(12)};
            const Vec3 p3 = r.pt(13, pts[2]);
            if (!p3.equals(pts[2])) pts.push_back(p3);
            emit(std::make_unique<PolylineEntity>(std::move(pts), true), r, c);
        } else if (t == "SOLID" || t == "TRACE") {
            const Vec3 a = r.pt(10), b = r.pt(11), cc = r.pt(12), d = r.pt(13, cc);
            std::vector<Vec3> pts{a, b, d};  // DXF tartibi: 1-2-4-3
            if (!d.equals(cc)) pts.push_back(cc);
            for (auto& p : pts) p.z = a.z;
            emit(place(std::make_unique<PolylineEntity>(std::move(pts), true), makeOcs(r)), r, c);
        } else if (t == "INSERT") {
            insert(n, c);
        } else if (t == "HATCH") {
            hatch(r, c);
        } else if (t == "DIMENSION") {
            dimension(r, c);
        } else if (t == "ATTDEF" || t == "VIEWPORT" || t == "SEQEND" || t == "VERTEX") {
            // ATTDEF — blok shabloni, VIEWPORT — layout xizmat obyekti: ko'rsatilmaydi
        } else {
            ++res_.skipped[std::string(t)];
        }
    }

    void lwpolyline(const Raw& r, const Ctx& c) {
        std::vector<Vec3> pts;
        std::vector<double> bulges;
        for (const auto& g : r.g) {
            if (g.code == 10) { pts.push_back({parseNum(g.value), 0, 0}); bulges.push_back(0); }
            else if (g.code == 20 && !pts.empty()) pts.back().y = parseNum(g.value);
            else if (g.code == 42 && !bulges.empty()) bulges.back() = parseNum(g.value);
        }
        if (pts.size() < 2) return badEntity(r);
        const double elev = r.num(38);
        for (auto& p : pts) p.z = elev;
        auto e = std::make_unique<PolylineEntity>(std::move(pts), std::move(bulges), (r.inum(70) & 1) != 0);
        emit(place(std::move(e), makeOcs(r)), r, c);
    }

    void polyline(const Node& n, const Ctx& c) {
        const Raw& r = n.main;
        const int flags = r.inum(70);
        if (flags & (16 | 64)) {
            ++res_.skipped["POLYLINE (mesh)"];
            return;
        }
        const bool d3 = (flags & 8) != 0;
        const double elev = r.pt(10).z;
        std::vector<Vec3> pts;
        std::vector<double> bulges;
        for (const auto& v : n.sub) {
            if (v.inum(70) & 16) continue;  // spline ramka nuqtasi
            Vec3 p = v.pt(10);
            if (!d3) p.z = elev;
            pts.push_back(p);
            bulges.push_back(d3 ? 0.0 : v.num(42));
        }
        if (pts.size() < 2) return badEntity(r);
        auto e = std::make_unique<PolylineEntity>(std::move(pts), std::move(bulges), (flags & 1) != 0);
        if (d3) emit(std::move(e), r, c);
        else emit(place(std::move(e), makeOcs(r)), r, c);
    }

    void text(const Raw& r, const Ctx& c) { emit(makeText(r), r, c); }

    std::unique_ptr<IEntity> makeText(const Raw& r) {
        const bool attrib = r.type == "ATTRIB";
        if (attrib && (r.inum(70) & 1)) return nullptr;  // ko'rinmas atribut
        auto v = r.find(1);
        if (!v) return nullptr;
        std::string s = decodeText(*v);
        if (s.empty()) return nullptr;
        const double h = r.num(40, 1.0) > 0 ? r.num(40, 1.0) : 1.0;
        const double rot = deg2rad(r.num(50));
        const int h72 = r.inum(72);
        const int v73 = attrib ? r.inum(74) : r.inum(73);
        TextHAlign ha = TextHAlign::Left;
        TextVAlign va = TextVAlign::Baseline;
        Vec3 anchor = r.pt(10);
        if (h72 == 3 || h72 == 5) {
            // Aligned/Fit: 10 — chap tayanch nuqta
        } else if (h72 == 4) {
            ha = TextHAlign::Center;
            va = TextVAlign::Middle;
            anchor = r.pt(11, anchor);
        } else if (h72 != 0 || v73 != 0) {
            ha = h72 == 1 ? TextHAlign::Center : h72 == 2 ? TextHAlign::Right : TextHAlign::Left;
            va = v73 == 1 ? TextVAlign::Bottom : v73 == 2 ? TextVAlign::Middle : v73 == 3 ? TextVAlign::Top : TextVAlign::Baseline;
            anchor = r.pt(11, anchor);
        }
        return place(std::make_unique<TextEntity>(anchor, h, std::move(s), rot, ha, va), makeOcs(r));
    }

    void mtext(const Raw& r, const Ctx& c) {
        std::string content;
        for (const auto& g : r.g)
            if (g.code == 3 || g.code == 1) content += dxftext::toUtf8(g.value, cp_);
        const auto paras = dxftext::mtextToParagraphs(content);
        double h = r.num(40, 2.5);
        if (h <= 0) h = 2.5;
        const double rot = r.has(11) ? std::atan2(r.num(21), r.num(11)) : r.num(50);  // MTEXT 50 — radianda
        const int attach = std::clamp(r.inum(71, 1), 1, 9);
        const double spacing = h * 5.0 / 3.0 * (r.num(44, 1.0) > 0 ? r.num(44, 1.0) : 1.0);
        const int col = (attach - 1) % 3, row = (attach - 1) / 3;
        const TextHAlign ha = col == 0 ? TextHAlign::Left : col == 1 ? TextHAlign::Center : TextHAlign::Right;
        const TextVAlign va = row == 0 ? TextVAlign::Top : row == 1 ? TextVAlign::Middle : TextVAlign::Bottom;
        const Vec3 ins = r.pt(10);
        const Vec3 down{std::sin(rot), -std::cos(rot), 0};
        const double nLines = static_cast<double>(paras.size());
        for (std::size_t k = 0; k < paras.size(); ++k) {
            if (paras[k].find_first_not_of(' ') == std::string::npos) continue;
            const double kk = static_cast<double>(k);
            const double shift = row == 0 ? kk : row == 1 ? kk - (nLines - 1) / 2.0 : kk - (nLines - 1);
            emit(std::make_unique<TextEntity>(ins + down * (shift * spacing), h, paras[k], rot, ha, va), r, c);
        }
    }

    // HATCH: chegara yo'llari (poliliniya yoki chiziq/yoy/ellips/splayn qirralari) -> bulge'li konturlar
    void hatch(const Raw& r, const Ctx& c) {
        const auto& g = r.g;
        std::size_t i = 0;
        auto seek = [&](int code) -> const Group* {
            while (i < g.size() && g[i].code != code) ++i;
            return i < g.size() ? &g[i++] : nullptr;
        };
        auto numAt = [&](int code) { const Group* x = seek(code); return x ? parseNum(x->value) : 0.0; };
        const double elev = r.pt(10).z;
        if (!seek(91)) return badEntity(r);
        const int nPaths = static_cast<int>(parseNum(g[i - 1].value));
        std::vector<PolylineEntity> loops;
        for (int pth = 0; pth < nPaths && i < g.size(); ++pth) {
            const int flags = static_cast<int>(numAt(92));
            std::vector<Vec3> pts;
            std::vector<double> bul;
            if (flags & 2) {  // poliliniya yo'li
                const bool hasBulge = numAt(72) != 0;
                numAt(73);
                const int nv = static_cast<int>(numAt(93));
                for (int k = 0; k < nv; ++k) {
                    const double x = numAt(10), y = numAt(20);
                    pts.push_back({x, y, elev});
                    bul.push_back(hasBulge && i < g.size() && g[i].code == 42 ? parseNum(g[i++].value) : 0.0);
                }
            } else {  // qirralar
                const int ne = static_cast<int>(numAt(93));
                for (int k = 0; k < ne; ++k) {
                    const int type = static_cast<int>(numAt(72));
                    if (type == 1) {
                        const double x0 = numAt(10), y0 = numAt(20);
                        numAt(11); numAt(21);
                        pts.push_back({x0, y0, elev});
                        bul.push_back(0);
                    } else if (type == 2) {
                        const double cx = numAt(10), cy = numAt(20), rad = numAt(40);
                        double a0 = numAt(50), a1 = numAt(51);
                        const bool ccw = numAt(73) != 0;
                        // CW yoy: DXF burchaklarni to'ldiruvchi (360-α) va almashtirilgan tartibda saqlaydi
                        const double startDeg = ccw ? a0 : 360.0 - a1;
                        double sweep = std::fmod((ccw ? a1 - a0 : a0 - a1) + 720.0, 360.0);
                        if (sweep < 1e-9) sweep = 360.0;
                        const double sa = deg2rad(startDeg);
                        const Vec3 p0{cx + rad * std::cos(sa), cy + rad * std::sin(sa), elev};
                        if (sweep >= 360.0 - 1e-9) {  // to'liq aylana — ikki yarimdan
                            pts.push_back(p0);
                            bul.push_back(ccw ? 1.0 : -1.0);
                            pts.push_back({cx - (p0.x - cx), cy - (p0.y - cy), elev});
                            bul.push_back(ccw ? 1.0 : -1.0);
                        } else {
                            pts.push_back(p0);
                            bul.push_back((ccw ? 1.0 : -1.0) * std::tan(deg2rad(sweep) / 4.0));
                        }
                    } else if (type == 3) {
                        const double cx = numAt(10), cy = numAt(20), mx = numAt(11), my = numAt(21), ratio = numAt(40);
                        const double a0 = numAt(50), a1 = numAt(51);
                        const bool ccw = numAt(73) != 0;
                        const double t0 = deg2rad(ccw ? a0 : 360.0 - a1);
                        double sweep = deg2rad(std::fmod((ccw ? a1 - a0 : a0 - a1) + 720.0, 360.0));
                        if (sweep < 1e-9) sweep = 2 * kPi;
                        const double dir = ccw ? 1.0 : -1.0;
                        const int nseg = std::max(8, static_cast<int>(std::ceil(sweep / opt_.maxArcStep)));
                        for (int s = 0; s < nseg; ++s) {
                            const double t = t0 + dir * sweep * s / nseg;
                            pts.push_back({cx + mx * std::cos(t) - my * ratio * std::sin(t),
                                           cy + my * std::cos(t) + mx * ratio * std::sin(t), elev});
                            bul.push_back(0);
                        }
                        ++curves_;
                    } else if (type == 4) {
                        const int p = static_cast<int>(numAt(94));
                        const bool rational = numAt(73) != 0;
                        numAt(74);
                        const int nk = static_cast<int>(numAt(95)), nc = static_cast<int>(numAt(96));
                        std::vector<double> U, W;
                        std::vector<Vec3> P;
                        for (int q = 0; q < nk; ++q) U.push_back(numAt(40));
                        for (int q = 0; q < nc; ++q) {
                            const double x = numAt(10), y = numAt(20);
                            P.push_back({x, y, elev});
                            if (rational && i < g.size() && g[i].code == 42) W.push_back(parseNum(g[i++].value));
                        }
                        if (p >= 1 && nc >= p + 1 && nk == nc + p + 1) {
                            auto sp = evalSpline(p, U, P, W, std::clamp(nc * 8, 16, 2000));
                            sp.pop_back();  // oxirgi nuqta — keyingi qirraning boshi
                            for (const auto& q : sp) { pts.push_back(q); bul.push_back(0); }
                        } else {
                            for (const auto& q : P) { pts.push_back(q); bul.push_back(0); }
                        }
                        ++curves_;
                    } else {
                        return badEntity(r);
                    }
                }
            }
            if (i < g.size() && g[i].code == 97) {  // assotsiativ manba obyektlar
                const int ns = static_cast<int>(parseNum(g[i++].value));
                for (int k = 0; k < ns && i < g.size() && g[i].code == 330; ++k) ++i;
            }
            if (pts.size() >= 2) loops.emplace_back(std::move(pts), std::move(bul), true);
        }
        if (loops.empty()) return badEntity(r);
        std::string pattern = r.find(2) ? decodeName(*r.find(2)) : std::string("SOLID");
        if (r.inum(70) == 1) pattern = "SOLID";
        double angle = 0, scale = 1;
        for (std::size_t k = i; k < g.size(); ++k) {
            if (g[k].code == 52) angle = deg2rad(parseNum(g[k].value));
            if (g[k].code == 41) { scale = parseNum(g[k].value); break; }
        }
        std::unique_ptr<IEntity> h = std::make_unique<HatchEntity>(std::move(loops), pattern, scale > 0 ? scale : 1.0, angle);
        const Ocs o = makeOcs(r);
        if (o.minusZ) h->transform(Ocs::minusZTransform());
        else if (!o.plusZ) ++ocs3d_;
        emit(std::move(h), r, c);
    }

    void ellipse(const Raw& r, const Ctx& c) {
        const Vec3 cen = r.pt(10), M = r.pt(11);
        const double ratio = r.num(40, 1.0);
        double t0 = r.num(41, 0.0), t1 = r.num(42, 2 * kPi);
        Vec3 nrm = r.pt(210, {0, 0, 1});
        if (nrm.length() < 1e-12) nrm = {0, 0, 1};
        nrm = nrm / nrm.length();
        if (M.length() < 1e-12) return badEntity(r);
        const Vec3 m = nrm.cross(M) * ratio;
        auto at = [&](double t) { return cen + M * std::cos(t) + m * std::sin(t); };
        double sweep = t1 - t0;
        while (sweep <= 1e-12) sweep += 2 * kPi;
        const bool full = std::abs(sweep - 2 * kPi) < 1e-9;
        const bool flat = std::abs(nrm.x) < 1e-9 && std::abs(nrm.y) < 1e-9;
        if (std::abs(ratio - 1.0) < 1e-9 && flat) {
            if (full) emit(std::make_unique<CircleEntity>(cen, M.length()), r, c);
            else emit(std::make_unique<ArcEntity>(ArcEntity::fromThreePoints(at(t0), at(t0 + sweep / 2), at(t0 + sweep))), r, c);
            return;
        }
        const int nseg = std::max(8, static_cast<int>(std::ceil(sweep / opt_.maxArcStep)));
        std::vector<Vec3> pts;
        for (int k = 0; k <= nseg; ++k) {
            if (full && k == nseg) break;
            pts.push_back(at(t0 + sweep * k / nseg));
        }
        ++curves_;
        emit(std::make_unique<PolylineEntity>(std::move(pts), full), r, c);
    }

    void spline(const Raw& r, const Ctx& c) {
        const int p = r.inum(71, 3);
        std::vector<double> U, W;
        std::vector<Vec3> P, F;
        for (const auto& g : r.g) {
            switch (g.code) {
                case 40: U.push_back(parseNum(g.value)); break;
                case 41: W.push_back(parseNum(g.value)); break;
                case 10: P.push_back({parseNum(g.value), 0, 0}); break;
                case 20: if (!P.empty()) P.back().y = parseNum(g.value); break;
                case 30: if (!P.empty()) P.back().z = parseNum(g.value); break;
                case 11: F.push_back({parseNum(g.value), 0, 0}); break;
                case 21: if (!F.empty()) F.back().y = parseNum(g.value); break;
                case 31: if (!F.empty()) F.back().z = parseNum(g.value); break;
                default: break;
            }
        }
        std::vector<Vec3> pts;
        const bool valid = p >= 1 && P.size() >= static_cast<std::size_t>(p + 1) && U.size() == P.size() + static_cast<std::size_t>(p) + 1;
        if (valid) {
            const int samples = std::clamp(static_cast<int>(P.size()) * 8, 32, 4000);
            pts = evalSpline(p, U, P, W, samples);
        } else if (F.size() >= 2) {
            pts = F;  // faqat fit nuqtalar — ular orqali siniq chiziq
        } else {
            return badEntity(r);
        }
        bool closed = (r.inum(70) & 1) != 0;
        if (pts.size() > 2 && pts.front().equals(pts.back(), 1e-9)) {
            pts.pop_back();
            closed = true;
        }
        ++curves_;
        emit(std::make_unique<PolylineEntity>(std::move(pts), closed), r, c);
    }

    const Block* findBlock(std::string_view rawName) {
        const std::string name = decodeName(rawName);
        auto it = blocks_.find(utf8::toUpper(name));
        if (it == blocks_.end()) {
            if (missing_.insert(name).second) res_.warnings.push_back({"dxf.warn.missing_block", {name}});
            return nullptr;
        }
        if (it->second.flags & 4) {
            if (missing_.insert(name).second) res_.warnings.push_back({"dxf.warn.xref", {name}});
            return nullptr;
        }
        return &it->second;
    }

    bool depthOk(const Ctx& c, const Block& b) {
        if (c.depth + 1 <= opt_.maxBlockDepth) return true;
        if (missing_.insert("depth:" + b.name).second) res_.warnings.push_back({"dxf.warn.depth", {b.name}});
        return false;
    }

    void insert(const Node& n, const Ctx& c) {
        if (opt_.explodeInserts) return insertExploded(n, c);
        const Raw& r = n.main;
        std::shared_ptr<BlockDef> def;
        if (auto name = r.find(2))
            if (const Block* b = findBlock(*name)) def = defOf(*b);
        if (!def) {
            for (const auto& a : n.sub) text(a, c);  // blok yo'q — atributlar oddiy matn bo'lib qoladi
            return;
        }
        const Ocs o = makeOcs(r);
        Transform ocsT;
        if (o.minusZ) ocsT = Ocs::minusZTransform();
        else if (!o.plusZ) ++ocs3d_;
        const double sx = r.num(41, 1.0), sy = r.num(42, 1.0), sz = r.num(43, 1.0);
        const double rot = deg2rad(r.num(50));
        const int cols = std::max(1, r.inum(70, 1)), rows = std::max(1, r.inum(71, 1));
        const double cs = r.num(44), rs = r.num(45);
        for (int row = 0; row < rows; ++row)
            for (int col = 0; col < cols; ++col) {
                const Transform m = ocsT * Transform::translation(r.pt(10)) * Transform::rotationAboutOrigin(rot) *
                                    Transform::translation({col * cs, row * rs, 0}) * Transform::scaling3(sx, sy, sz) *
                                    Transform::translation(def->base * -1.0);
                auto ins = std::make_unique<InsertEntity>(def, m);
                if (row == 0 && col == 0)
                    for (const auto& a : n.sub) {
                        auto t = makeText(a);
                        if (!t) continue;
                        t->setLayer(layerOf(a, c));
                        t->setColor(colorOf(a, c));
                        ins->addAttribute(decodeName(a.find(2).value_or("")), static_cast<TextEntity&>(*t));
                    }
                emit(std::move(ins), r, c);
            }
    }

    // Blok ta'rifini bir marta quradi (ichma-ich bloklar rekursiv), sikl bo'lsa nullptr
    std::shared_ptr<BlockDef> defOf(const Block& b) {
        const std::string key = utf8::toUpper(b.name);
        if (auto it = built_.find(key); it != built_.end()) return it->second;
        if (building_.count(key)) {
            if (missing_.insert("depth:" + b.name).second) res_.warnings.push_back({"dxf.warn.depth", {b.name}});
            return nullptr;
        }
        building_.insert(key);
        auto def = std::make_shared<BlockDef>();
        def->name = b.name;
        def->base = b.base;
        auto* saved = out_;
        out_ = &def->entities;
        Ctx dc;
        dc.def = true;
        convertNodes(b.nodes, dc);
        out_ = saved;
        building_.erase(key);
        built_[key] = def;
        res_.blocks.push_back(def);
        return def;
    }

    void insertExploded(const Node& n, const Ctx& c) {
        const Raw& r = n.main;
        if (auto name = r.find(2)) {
            if (const Block* b = findBlock(*name); b && depthOk(c, *b)) {
                const Ocs o = makeOcs(r);
                Transform ocsT;
                if (o.minusZ) ocsT = Ocs::minusZTransform();
                else if (!o.plusZ) ++ocs3d_;
                const double sx = r.num(41, 1.0), sy = r.num(42, 1.0), sz = r.num(43, 1.0);
                const double rot = deg2rad(r.num(50));
                const int cols = std::max(1, r.inum(70, 1)), rows = std::max(1, r.inum(71, 1));
                const double cs = r.num(44), rs = r.num(45);
                Ctx child;
                child.layer = layerOf(r, c);
                child.color = colorOf(r, c);
                child.depth = c.depth + 1;
                child.hasT = true;
                for (int row = 0; row < rows; ++row)
                    for (int col = 0; col < cols; ++col) {
                        child.t = c.t * ocsT * Transform::translation(r.pt(10)) * Transform::rotationAboutOrigin(rot) *
                                  Transform::translation({col * cs, row * rs, 0}) * Transform::scaling3(sx, sy, sz) *
                                  Transform::translation(b->base * -1.0);
                        convertNodes(b->nodes, child);
                        ++inserts_;
                    }
            }
        }
        for (const auto& a : n.sub) text(a, c);  // ATTRIB'lar allaqachon joyida
    }

    void dimension(const Raw& r, const Ctx& c) {
        if (c.depth == 0) {
            DxfDimInfo d;
            d.style = r.find(3) ? dxftext::decodeUnicodeEscapes(dxftext::toUtf8(trim(*r.find(3)), cp_)) : std::string{};
            d.text = r.find(1) ? dxftext::decodeTextControlCodes(dxftext::toUtf8(*r.find(1), cp_)) : std::string{};
            d.kind = r.inum(70) & 7;
            d.p1 = r.pt(13);
            d.p2 = r.pt(14);
            d.loc = r.pt(10);
            if (c.hasT) { d.p1 = c.t.apply(d.p1); d.p2 = c.t.apply(d.p2); d.loc = c.t.apply(d.loc); }
            if (auto it = res_.dimStyleLfac.find(utf8::toUpper(d.style)); it != res_.dimStyleLfac.end()) d.lfac = it->second;
            for (std::size_t i = 0; i + 1 < r.g.size(); ++i)  // XDATA ACAD DSTYLE: 1070 144, 1040 qiymat
                if (r.g[i].code == 1070 && trim(r.g[i].value) == "144" && r.g[i + 1].code == 1040) {
                    const double v = parseNum(r.g[i + 1].value);
                    if (v != 0) d.lfac = std::abs(v);
                }
            res_.dims.push_back(std::move(d));
        }
        auto name = r.find(2);
        const Block* b = name ? findBlock(*name) : nullptr;
        if (!b) {
            ++res_.skipped["DIMENSION"];
            return;
        }
        if (!depthOk(c, *b)) return;
        Ctx child = c;
        child.layer = layerOf(r, c);
        child.color = colorOf(r, c);
        child.depth = c.depth + 1;
        convertNodes(b->nodes, child);  // o'lcham bloki WCS'da
    }

    const DxfImportOptions& opt_;
    DxfImportResult& res_;
    Codepage cp_;
    const std::unordered_map<std::string, Block>& blocks_;
    std::set<std::string> usedLayers_, missing_, building_;
    std::map<std::string, std::shared_ptr<BlockDef>> built_;
    std::vector<std::unique_ptr<IEntity>>* out_ = &res_.entities;
    std::size_t paper_ = 0, inserts_ = 0, curves_ = 0, nonUniform_ = 0, ocs3d_ = 0;
};

} // namespace

namespace {
// ASCII'dan tashqari baytlar bor va ularning hammasi to'g'ri UTF-8 ketma-ketligi
bool looksLikeUtf8(std::string_view s) {
    bool nonAscii = false;
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { ++i; continue; }
        nonAscii = true;
        const int len = (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + static_cast<std::size_t>(len) > s.size()) return false;
        for (int k = 1; k < len; ++k)
            if ((static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0xC0) != 0x80) return false;
        i += static_cast<std::size_t>(len);
    }
    return nonAscii;
}
} // namespace

// ============================================================ asosiy o'qish
DxfImportResult readDxf(std::string_view text, const DxfImportOptions& opt) {
    if (text.size() >= 3 && text.substr(0, 3) == "\xEF\xBB\xBF") text.remove_prefix(3);
    if (text.substr(0, 18) == "AutoCAD Binary DXF") throw Error("dxf.err.binary");
    if (text.size() > 6 && text.substr(0, 3) == "AC1" && text.substr(0, 6).find('\n') == std::string_view::npos &&
        text.substr(0, 6).find(' ') == std::string_view::npos)
        throw Error("dxf.err.dwg");

    const std::vector<Group> gs = tokenize(text);
    DxfImportResult res;
    std::string codepageName;
    std::vector<Raw> entityRaws;
    std::unordered_map<std::string, Block> blocks;
    std::vector<Raw> tableRaws, blockRaws;

    std::size_t i = 0;
    while (i < gs.size()) {
        if (gs[i].code == 0 && trim(gs[i].value) == "EOF") break;
        if (!(gs[i].code == 0 && trim(gs[i].value) == "SECTION")) { ++i; continue; }
        ++i;
        const std::string_view sect = i < gs.size() && gs[i].code == 2 ? trim(gs[i].value) : std::string_view{};
        ++i;
        if (sect == "HEADER") {
            std::string_view var;
            for (; i < gs.size() && !(gs[i].code == 0 && trim(gs[i].value) == "ENDSEC"); ++i) {
                const auto& g = gs[i];
                if (g.code == 9) var = trim(g.value);
                else if (var == "$ACADVER" && g.code == 1) res.version = std::string(trim(g.value));
                else if (var == "$DWGCODEPAGE" && g.code == 3) codepageName = std::string(trim(g.value));
                else if (var == "$INSUNITS" && g.code == 70) res.insUnits = static_cast<int>(parseNum(g.value));
            }
        } else if (sect == "TABLES") {
            tableRaws = collect(gs, i, "ENDSEC");
        } else if (sect == "BLOCKS") {
            blockRaws = collect(gs, i, "ENDSEC");
        } else if (sect == "ENTITIES") {
            entityRaws = collect(gs, i, "ENDSEC");
        } else {
            while (i < gs.size() && !(gs[i].code == 0 && trim(gs[i].value) == "ENDSEC")) ++i;
        }
    }

    // Kodlash
    Codepage cp = Codepage::Cp1252;
    if (res.version >= "AC1021") {
        cp = Codepage::Utf8;
    } else if (res.version.empty() && codepageName.empty() && looksLikeUtf8(text)) {
        cp = Codepage::Utf8;  // HEADER'siz fayl (ko'p skript/GIS eksportlari) — odatda UTF-8
    } else {
        bool ok = true;
        cp = dxftext::fromDxfName(codepageName, &ok);
        if (!ok) res.warnings.push_back({"dxf.warn.codepage", {codepageName}});
    }
    res.encoding = std::string(dxftext::name(cp));
    res.release = std::string(dxfReleaseName(res.version));

    auto decodeName = [cp](std::string_view s) { return dxftext::decodeUnicodeEscapes(dxftext::toUtf8(trim(s), cp)); };

    // Qatlamlar jadvali
    std::string_view table;
    std::set<std::string> seenLayers;
    for (const auto& r : tableRaws) {
        if (r.type == "TABLE") { table = r.find(2) ? trim(*r.find(2)) : std::string_view{}; continue; }
        if (r.type == "ENDTAB") { table = {}; continue; }
        if (table == "DIMSTYLE" && r.type == "DIMSTYLE") {
            const double lf = r.num(144, 1.0);
            res.dimStyleLfac[utf8::toUpper(decodeName(r.find(2).value_or("")))] = lf != 0 ? std::abs(lf) : 1.0;
            continue;
        }
        if (table != "LAYER" || r.type != "LAYER") continue;
        Layer l;
        l.name = decodeName(r.find(2).value_or(""));
        if (l.name.empty() || !seenLayers.insert(l.name).second) continue;
        const int col = r.inum(62, 7);
        l.visible = col >= 0;
        l.color = std::abs(col);
        if (l.color < 1 || l.color > 255) l.color = 7;
        const int fl = r.inum(70);
        l.frozen = (fl & 1) != 0;
        l.locked = (fl & 4) != 0;
        res.layers.push_back(l);
    }

    // Bloklar
    Block* cur = nullptr;
    std::vector<Raw> body;
    for (auto& r : blockRaws) {
        if (r.type == "BLOCK") {
            Block b;
            b.name = decodeName(r.find(2).value_or(""));
            b.base = r.pt(10);
            b.flags = r.inum(70);
            cur = &blocks[utf8::toUpper(b.name)];
            *cur = std::move(b);
            body.clear();
        } else if (r.type == "ENDBLK") {
            if (cur) cur->nodes = groupNodes(body);
            cur = nullptr;
            body.clear();
        } else if (cur) {
            body.push_back(std::move(r));
        }
    }

    // Obyektlar
    auto nodes = groupNodes(entityRaws);
    Converter conv(opt, res, cp, blocks);
    conv.run(nodes);
    {
        std::vector<double> hs;
        for (const auto& e : res.entities)
            if (e->type() == EntityType::Text) hs.push_back(static_cast<const TextEntity&>(*e).height());
        if (!hs.empty()) {
            std::nth_element(hs.begin(), hs.begin() + hs.size() / 2, hs.end());
            res.medianTextHeight = hs[hs.size() / 2];
        }
    }
    for (const auto& name : conv.usedLayers())
        if (seenLayers.insert(name).second) res.layers.push_back(Layer{name});
    return res;
}

DxfImportResult readDxfFile(const std::string& path, const DxfImportOptions& opt) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw Error("dxf.err.open", {path});
    std::stringstream ss;
    ss << f.rdbuf();
    return readDxf(ss.str(), opt);
}

std::unique_ptr<CompositeCommand> makeImportCommand(const Document& doc, DxfImportResult& r, std::size_t* newLayers) {
    auto comp = std::make_unique<CompositeCommand>("IMPORT");
    std::size_t nl = 0;
    std::set<std::string> taken;
    for (auto& b : r.blocks) {
        std::string name = b->name;
        for (int k = 1; doc.block(name) || taken.count(utf8::toUpper(name)); ++k) name = b->name + "$" + std::to_string(k);
        b->name = name;
        taken.insert(utf8::toUpper(name));
    }
    for (const auto& l : r.layers)
        if (!doc.layer(l.name)) {
            comp->add(std::make_unique<SetLayerCommand>(l));
            ++nl;
        }
    for (const auto& b : r.blocks) comp->add(std::make_unique<AddBlockCommand>(b));
    for (auto& e : r.entities) comp->add(std::make_unique<AddEntityCommand>(std::move(e), "IMPORT"));
    r.entities.clear();
    if (newLayers) *newLayers = nl;
    return comp;
}

} // namespace asiacad
