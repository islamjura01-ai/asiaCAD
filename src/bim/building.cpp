#include <asiacad/bim/building.h>

#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/json.h>
#include <asiacad/core/modify.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <format>
#include <sstream>

namespace asiacad::bim {

namespace {

constexpr double kPI = 3.14159265358979323846;

double signedArea(const std::vector<Vec3>& v) {
    double a = 0;
    for (std::size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) a += (v[j].x * v[i].y - v[i].x * v[j].y);
    return a / 2;
}
double ringLen(const std::vector<Vec3>& v) {
    double l = 0;
    for (std::size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) l += std::hypot(v[i].x - v[j].x, v[i].y - v[j].y);
    return l;
}
bool inside(const std::vector<Vec3>& poly, const Vec3& p) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > p.y) != (poly[j].y > p.y) &&
            p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x)
            in = !in;
    return in;
}
Vec3 centroid(const std::vector<Vec3>& v) {
    Vec3 c;
    for (const auto& p : v) c = c + p;
    return v.empty() ? c : c / static_cast<double>(v.size());
}

// Yopiq kontur nuqtalari (yoylar bo'linadi); bo'lmasa bo'sh
std::vector<Vec3> ringOf(const IEntity& e) {
    std::vector<Vec3> out;
    if (auto* pl = dynamic_cast<const PolylineEntity*>(&e)) {
        if (!pl->isClosed() && !(pl->vertices().size() > 3 && pl->vertices().front().distanceTo(pl->vertices().back()) < 1e-6))
            return out;
        const PolylineEntity t = pl->hasBulges() ? pl->tessellated(deg2rad(10)) : *pl;
        out = t.vertices();
        if (out.size() > 1 && out.front().distanceTo(out.back()) < 1e-9) out.pop_back();
    } else if (auto* c = dynamic_cast<const CircleEntity*>(&e)) {
        for (int i = 0; i < 24; ++i) {
            const double a = 2 * kPI * i / 24;
            out.push_back(c->center() + Vec3{c->radius() * std::cos(a), c->radius() * std::sin(a), 0});
        }
    }
    for (auto& p : out) p.z = 0;
    return out;
}

std::vector<Vec3> offsetRing(const std::vector<Vec3>& ring, double d) {
    if (d <= 0 || ring.size() < 3) return ring;
    PolylineEntity pl(ring, true);
    // tashqariga: kontur markazidan uzoqroq nuqta tomonga
    const BBox b = pl.bbox();
    const Vec3 far{b.max.x + (b.max.x - b.min.x) + 10, b.max.y + (b.max.y - b.min.y) + 10, 0};
    try {
        auto e = offsetEntity(pl, d, far);
        if (auto* o = dynamic_cast<PolylineEntity*>(e.get())) {
            auto v = o->vertices();
            for (auto& p : v) p.z = 0;
            return v;
        }
    } catch (...) {
    }
    return ring;
}

std::vector<Vec3> bboxRing(const std::vector<std::vector<Vec3>>& rings) {
    BBox b;
    for (const auto& r : rings)
        for (const auto& p : r) b.expand(p);
    if (!b.valid()) return {};
    return {{b.min.x, b.min.y, 0}, {b.max.x, b.min.y, 0}, {b.max.x, b.max.y, 0}, {b.min.x, b.max.y, 0}};
}

void collect(const IEntity& e, ElemKind k, PlanInput& in) {
    if (k == ElemKind::Ignore) return;
    if (auto* ins = dynamic_cast<const InsertEntity*>(&e)) {
        for (const auto& part : ins->explode()) collect(*part, k, in);
        return;
    }
    auto ring = ringOf(e);
    if (ring.size() >= 3) {
        if (signedArea(ring) < 0) std::reverse(ring.begin(), ring.end());
        switch (k) {
            case ElemKind::Column: in.columns.push_back(ring); break;
            case ElemKind::Wall: in.walls.push_back(ring); break;
            case ElemKind::Slab:
            case ElemKind::Roof: in.slabs.push_back(ring); break;
            case ElemKind::Opening: in.openings.push_back(ring); break;
            case ElemKind::Outline: in.outlines.push_back(ring); break;
            case ElemKind::FoundationSlab: in.foundations.push_back(ring); break;
            case ElemKind::Lean: in.leans.push_back(ring); break;
            case ElemKind::StripFooting: in.strips.push_back(ring); break;
            default: break;
        }
        return;
    }
    if (k != ElemKind::Wall) return;
    if (auto* l = dynamic_cast<const LineEntity*>(&e)) in.wallLines.push_back({l->startPoint(), l->endPoint()});
    else if (auto* pl = dynamic_cast<const PolylineEntity*>(&e)) {
        const auto& v = pl->vertices();
        for (std::size_t i = 0; i + 1 < v.size(); ++i) in.wallLines.push_back({v[i], v[i + 1]});
    }
}

std::vector<Vec3> wallRect(const Vec3& a, const Vec3& b, double t) {
    const double l = std::hypot(b.x - a.x, b.y - a.y);
    if (l < 1e-9) return {};
    const Vec3 n{-(b.y - a.y) / l * t / 2, (b.x - a.x) / l * t / 2, 0};
    return {Vec3{a.x, a.y, 0} - n, Vec3{b.x, b.y, 0} - n, Vec3{b.x, b.y, 0} + n, Vec3{a.x, a.y, 0} + n};
}

} // namespace

const char* kindKey(ElemKind k) {
    switch (k) {
        case ElemKind::Lean: return "LEAN";
        case ElemKind::FoundationSlab: return "FOUND";
        case ElemKind::StripFooting: return "STRIP";
        case ElemKind::Column: return "COLUMN";
        case ElemKind::Wall: return "WALL";
        case ElemKind::Slab: return "SLAB";
        case ElemKind::Roof: return "ROOF";
        case ElemKind::Opening: return "OPENING";
        case ElemKind::Outline: return "OUTLINE";
        case ElemKind::Ignore: return "IGNORE";
    }
    return "IGNORE";
}

bool kindFromKey(const std::string& s, ElemKind& out) {
    for (ElemKind k : {ElemKind::Lean, ElemKind::FoundationSlab, ElemKind::StripFooting, ElemKind::Column, ElemKind::Wall, ElemKind::Slab,
                       ElemKind::Roof, ElemKind::Opening, ElemKind::Outline, ElemKind::Ignore})
        if (s == kindKey(k)) { out = k; return true; }
    return false;
}

ElemKind guessKind(const std::string& layer) {
    const std::string u = utf8::toUpper(layer);
    auto has = [&](std::initializer_list<const char*> keys) {
        for (const char* k : keys)
            if (u.find(utf8::toUpper(k)) != std::string::npos) return true;
        return false;
    };
    if (has({"PADBETON", "ПОДБЕТОН", "ПАДБЕТОН", "LEAN", "GROBETON", "ГРОБЕТОН", "TESVIYE"})) return ElemKind::Lean;
    if (has({"LENTA", "ЛЕНТ", "STRIP"})) return ElemKind::StripFooting;
    if (has({"FUNDAMENT", "ФУНДАМЕНТ", "FOUND", "TEMEL", "ІРГЕТАС", "ПОДУШК"})) return ElemKind::FoundationSlab;
    if (has({"USTUN", "УСТУН", "КОЛОН", "COLUMN", "COL_", "KOLON", "ПИЛОН", "PILON", "БАҒАН", "ТИРЕК"})) return ElemKind::Column;
    if (has({"DEVOR", "ДЕВОР", "СТЕН", "WALL", "DUVAR", "ҚАБЫРҒА", "ДУБАЛ", "ДИАФРАГМ"})) return ElemKind::Wall;
    if (has({"TOM", "КРОВЛ", "ПОКРЫТ", "ROOF", "ÇATI", "ЧАТЫР", "ШАТЫР"})) return ElemKind::Roof;
    if (has({"PLITA", "ПЛИТ", "SLAB", "DÖŞEME", "ПЕРЕКРЫТ", "ЁПМА", "YOPMA"})) return ElemKind::Slab;
    if (has({"TESHIK", "ТЕШИК", "ПРОЕМ", "ПРОЁМ", "ОТВЕРСТ", "OPENING", "BOŞLUK"})) return ElemKind::Opening;
    if (has({"KONTUR", "КОНТУР", "OUTLINE", "GABARIT", "ГАБАРИТ"})) return ElemKind::Outline;
    return ElemKind::Ignore;
}

std::map<std::string, ElemKind> autoLayerMap(const Document& doc) {
    std::map<std::string, ElemKind> m;
    for (const auto& [name, l] : doc.layers()) m[name] = guessKind(name);
    return m;
}

namespace {
// "h=200", "δ=200", "t=0,2" — plita qalinligi (m); "+3.000", "отм. +3,000", "▽+3.000", "±0.000" — belgi
bool parseThickness(const std::string& raw, double& v) {
    std::string s = raw;
    for (auto& c : s) if (c == ',') c = '.';
    for (const char* mk : {"h=", "H=", "\xCE\xB4=", "\xCE\xB4 =", "t=", "\xD0\xBD=", "\xD0\x9D="}) {
        const auto p = s.find(mk);
        if (p == std::string::npos) continue;
        try {
            std::size_t used = 0;
            double x = std::stod(s.substr(p + std::strlen(mk)), &used);
            if (used == 0) continue;
            if (x >= 30 && x <= 3000) x /= 1000;
            if (x > 0.03 && x < 3) { v = x; return true; }
        } catch (...) {
        }
    }
    return false;
}
bool parseLevel(const std::string& raw, double& v) {
    std::string s = raw;
    for (auto& c : s) if (c == ',') c = '.';
    if (s.find("\xC2\xB1" "0.000") != std::string::npos) { v = 0; return true; }
    for (std::size_t p = 0; p < s.size(); ++p) {
        if (s[p] != '+' && s[p] != '-') continue;
        std::size_t q = p + 1;
        while (q < s.size() && std::isdigit(static_cast<unsigned char>(s[q]))) ++q;
        if (q == p + 1 || q >= s.size() || s[q] != '.') continue;
        std::size_t r = q + 1;
        while (r < s.size() && std::isdigit(static_cast<unsigned char>(s[r]))) ++r;
        if (r - q - 1 != 3) continue;  // belgilar uch kasr bilan yoziladi
        v = std::stod(s.substr(p, r - p));
        return true;
    }
    return false;
}
} // namespace

PlanInput readPlan(const Document& doc, const std::map<std::string, ElemKind>& layerMap) {
    PlanInput in;
    std::vector<std::pair<Vec3, double>> thick;
    std::map<double, int> levels;
    std::function<void(const IEntity&)> texts = [&](const IEntity& e) {
        if (auto* ins = dynamic_cast<const InsertEntity*>(&e)) {
            for (const auto& part : ins->explode()) texts(*part);
            for (const auto& a : ins->attributes()) texts(a.text);
            return;
        }
        auto* t = dynamic_cast<const TextEntity*>(&e);
        if (!t) return;
        double v;
        if (parseThickness(t->text(), v)) thick.push_back({t->insertion(), v});
        else if (parseLevel(t->text(), v)) levels[std::round(v * 1000) / 1000] += 1;
    };
    doc.forEach([&](const IEntity& e) {
        auto it = layerMap.find(e.layer());
        const ElemKind k = it != layerMap.end() ? it->second : guessKind(e.layer());
        collect(e, k, in);
        texts(e);
    });
    // qalinlik: plita ichidagi (yoki yagona) yozuv
    for (const auto& [pt, v] : thick) {
        bool inSlab = in.slabs.empty();
        for (const auto& sl : in.slabs) inSlab = inSlab || inside(sl, pt);
        if (inSlab) { in.slabT = v; break; }
    }
    // belgi: eng ko'p uchragan (teng bo'lsa — eng balandi)
    int best = 0;
    for (const auto& [lv, n] : levels)
        if (n >= best) { best = n; in.levelTop = lv; }
    return in;
}

double Prism::area() const {
    double a = std::abs(signedArea(outline));
    for (const auto& h : holes) a -= std::abs(signedArea(h));
    return std::max(0.0, a);
}
double Prism::perimeter() const {
    double l = ringLen(outline);
    for (const auto& h : holes) l += ringLen(h);
    return l;
}
double Prism::formwork() const {
    const double h = z1 - z0;
    switch (kind) {
        case ElemKind::Column:
        case ElemKind::Wall: return ringLen(outline) * h;                // yon sirtlar
        case ElemKind::Slab:
        case ElemKind::Roof: return area() + perimeter() * h;           // pastki sirt + chetlar
        default: return ringLen(outline) * h;                            // fundament/padbetonka — chetlar
    }
}

BuildingModel buildModel(const PlanInput& plan, const BuildingParams& p) { return buildModel(plan, p, FloorPlans{}); }

BuildingModel buildModel(const PlanInput& typical, const BuildingParams& p, const FloorPlans& fp) {
    BuildingModel m;
    auto issue = [&](bool err, const char* key, std::vector<std::string> a = {}) { m.issues.push_back({err, key, std::move(a)}); };
    if (p.floors < 1 || p.floorHeight <= p.slabT + 0.5) issue(true, "bim.chk.params");
    auto planFor = [&](int k) -> const PlanInput& {
        auto it = fp.floors.find(k);
        return it != fp.floors.end() ? it->second : typical;
    };
    auto wallsOf = [&](const PlanInput& pl) {
        std::vector<std::vector<Vec3>> w = pl.walls;
        for (const auto& [a, b] : pl.wallLines)
            if (auto r = wallRect(a, b, p.wallT); !r.empty()) w.push_back(r);
        return w;
    };
    auto outlinesOf = [&](const PlanInput& pl, bool report) {
        std::vector<std::vector<Vec3>> o = pl.outlines;
        if (o.empty()) o = pl.slabs;
        if (o.empty()) {
            std::vector<std::vector<Vec3>> v = pl.columns;
            for (const auto& w : wallsOf(pl)) v.push_back(w);
            if (auto b = bboxRing(v); !b.empty()) o.push_back(b);
            if (report) issue(false, "bim.chk.no_outline");
        }
        return o;
    };
    const PlanInput& base = planFor(1);
    const auto walls = wallsOf(base);
    if (base.columns.empty() && walls.empty()) issue(true, "bim.chk.no_vertical");
    const auto outlines = outlinesOf(base, true);

    // ---- fundament va padbetonka
    const PlanInput* fplan = fp.foundation ? &*fp.foundation : nullptr;
    const double foundT = fplan && fplan->slabT > 0 ? fplan->slabT : p.foundT;
    const double ftop = fplan && fplan->levelTop ? *fplan->levelTop : p.foundTop;  // fundament plani belgisi
    const double fBot = ftop - foundT;
    auto lean = [&](const std::vector<Vec3>& f) {
        m.prisms.push_back({ElemKind::Lean, 0, offsetRing(f, p.leanOverhang), {}, fBot - p.leanT, fBot, p.classLean});
    };
    const PlanInput& fsrc = fplan ? *fplan : base;
    if (!fsrc.foundations.empty() || !fsrc.strips.empty()) {
        // aniq konturlar (fundament plani)
        for (const auto& f : fsrc.foundations) m.prisms.push_back({ElemKind::FoundationSlab, 0, f, {}, fBot, ftop, p.classFound});
        for (const auto& f : fsrc.strips) m.prisms.push_back({ElemKind::StripFooting, 0, f, {}, fBot, ftop, p.classFound});
        if (!fsrc.leans.empty())
            for (const auto& l : fsrc.leans) m.prisms.push_back({ElemKind::Lean, 0, l, {}, fBot - p.leanT, fBot, p.classLean});
        else {
            for (const auto& f : fsrc.foundations) lean(f);
            for (const auto& f : fsrc.strips) lean(f);
        }
    } else if (!p.stripFoundation) {
        for (const auto& o : outlines) {
            const auto f = offsetRing(o, p.foundOverhang);
            m.prisms.push_back({ElemKind::FoundationSlab, 0, f, {}, fBot, ftop, p.classFound});
            lean(f);
        }
    } else {
        // lenta: devorlar ostida kengligi stripWidth; ustunlar ostida kvadrat yostiq (2×stripWidth)
        for (const auto& w : walls) {
            const auto f = offsetRing(w, std::max(0.0, (p.stripWidth - p.wallT) / 2));
            m.prisms.push_back({ElemKind::StripFooting, 0, f, {}, fBot, ftop, p.classFound});
            lean(f);
        }
        for (const auto& c : base.columns) {
            const Vec3 cc = centroid(c);
            const double h = p.stripWidth;
            std::vector<Vec3> pad{{cc.x - h, cc.y - h, 0}, {cc.x + h, cc.y - h, 0}, {cc.x + h, cc.y + h, 0}, {cc.x - h, cc.y + h, 0}};
            m.prisms.push_back({ElemKind::StripFooting, 0, pad, {}, fBot, ftop, p.classFound});
            lean(pad);
        }
    }
    // ---- qavatlar (har qavat o'z plani; belgilar qolip chizmasidan yoki qavat balandligidan)
    double prevTop = 0;
    std::vector<std::vector<Vec3>> prevVerticals;
    for (int k = 1; k <= p.floors; ++k) {
        const PlanInput& pl = planFor(k);
        const bool own = fp.floors.count(k) != 0;
        const bool roof = k == p.floors;
        const double t = pl.slabT > 0 ? pl.slabT : (roof ? p.roofT : p.slabT);
        const double top = pl.levelTop && own ? *pl.levelTop : prevTop + p.floorHeight;
        const double z0 = k == 1 ? ftop : prevTop;
        if (top - t <= z0 + 0.5) issue(true, "bim.chk.level", {std::to_string(k), std::format("{:.3f}", top)});
        const auto w = wallsOf(pl);
        if (own && pl.columns.empty() && w.empty()) issue(false, "bim.chk.no_vertical_floor", {std::to_string(k)});
        for (const auto& c : pl.columns) m.prisms.push_back({ElemKind::Column, k, c, {}, z0, top - t, p.classFrame});
        for (const auto& wl : w) m.prisms.push_back({ElemKind::Wall, k, wl, {}, z0, top - t, p.classFrame});
        auto slabs = pl.slabs.empty() ? outlinesOf(pl, false) : pl.slabs;
        for (const auto& s : slabs) {
            Prism pr{roof ? ElemKind::Roof : ElemKind::Slab, k, s, {}, top - t, top, p.classSlab};
            for (const auto& o : pl.openings)
                if (inside(s, centroid(o))) pr.holes.push_back(o);
            m.prisms.push_back(pr);
        }
        // tayanch: yuqori qavat ustuni pastdagi ustun/devorga tushishi kerak
        if (k >= 2 && !fp.floors.empty())
            for (std::size_t i = 0; i < pl.columns.size(); ++i) {
                const Vec3 c = centroid(pl.columns[i]);
                bool ok = false;
                for (const auto& v : prevVerticals) ok = ok || inside(v, c);
                if (!ok) issue(true, "bim.chk.unsupported", {std::to_string(k), std::format("{:.2f}; {:.2f}", c.x, c.y)});
            }
        prevVerticals = pl.columns;
        prevVerticals.insert(prevVerticals.end(), w.begin(), w.end());
        prevTop = top;
    }
    m.heightTop = prevTop;
    const PlanInput& plan = base;
    const auto slabs = base.slabs.empty() ? outlines : base.slabs;

    // ---- tekshiruvlar
    for (std::size_t i = 0; i < plan.columns.size(); ++i) {
        const Vec3 c = centroid(plan.columns[i]);
        bool in = false;
        for (const auto& s : slabs) in = in || inside(s, c);
        if (!in) issue(false, "bim.chk.outside", {std::to_string(i + 1)});
        const double a = std::abs(signedArea(plan.columns[i]));
        if (a < 0.0625 - 1e-9) issue(false, "bim.chk.small_column", {std::to_string(i + 1), std::format("{:.3f}", a)});
    }
    if (p.slabT < 0.06) issue(true, "bim.chk.thin_slab", {std::format("{:.2f}", p.slabT)});

    computePto(m);
    return m;
}

void computePto(BuildingModel& m) {
    m.pto.clear();
    m.byConcrete.clear();
    m.totalVolume = m.totalFormwork = 0;
    std::map<std::pair<int, int>, PtoRow> rows;
    for (const auto& pr : m.prisms) {
        auto& r = rows[{pr.level, static_cast<int>(pr.kind)}];
        r.kind = pr.kind;
        r.level = pr.level;
        r.count += 1;
        r.volume += pr.volume();
        r.formwork += pr.kind == ElemKind::Lean ? 0.0 : pr.formwork();
        r.concrete = pr.concrete;
        m.totalVolume += pr.volume();
        m.totalFormwork += pr.kind == ElemKind::Lean ? 0.0 : pr.formwork();
        m.byConcrete[pr.concrete] += pr.volume();
    }
    for (auto& [k, r] : rows) m.pto.push_back(r);
}

bool parseLevelText(const std::string& s, double& v) { return parseLevel(s, v); }

std::string paramsToJson(const BuildingParams& p, const std::map<std::string, ElemKind>& lm) {
    auto esc = [](const std::string& x) {
        std::string o;
        for (char c : x) {
            if (c == '"' || c == '\\') o += '\\';
            o += c;
        }
        return o;
    };
    std::ostringstream os;
    os << std::format("{{\n  \"format\": \"asiacad-building\", \"version\": 1,\n  \"floors\": {}, \"H\": {}, \"slab\": {}, \"roof\": {}, "
                      "\"foundTop\": {}, \"foundT\": {}, \"foundOver\": {}, \"strip\": {}, \"stripW\": {}, \"lean\": {}, "
                      "\"leanOver\": {}, \"wallT\": {},\n  \"classes\": [\"{}\", \"{}\", \"{}\", \"{}\"],\n  \"layers\": {{",
                      p.floors, p.floorHeight, p.slabT, p.roofT, p.foundTop, p.foundT, p.foundOverhang, p.stripFoundation ? "true" : "false",
                      p.stripWidth, p.leanT, p.leanOverhang, p.wallT, esc(p.classLean), esc(p.classFound), esc(p.classFrame), esc(p.classSlab));
    bool first = true;
    for (const auto& [l, k] : lm) {
        os << (first ? "" : ", ") << std::format("\"{}\": \"{}\"", esc(l), kindKey(k));
        first = false;
    }
    os << "}\n}\n";
    return os.str();
}

void paramsFromJson(const std::string& text, BuildingParams& p, std::map<std::string, ElemKind>& lm) {
    json::Value root;
    try {
        root = json::parse(text);
    } catch (const json::ParseError& e) {
        throw Error("bim.err.format", {e.what()});
    }
    if (!root.isObject()) throw Error("bim.err.format", {"root"});
    auto num = [&](const char* k, double d) {
        const json::Value* v = root.find(k);
        return v && v->isNumber() ? v->num() : d;
    };
    p.floors = static_cast<int>(num("floors", p.floors));
    p.floorHeight = num("H", p.floorHeight);
    p.slabT = num("slab", p.slabT);
    p.roofT = num("roof", p.roofT);
    p.foundTop = num("foundTop", p.foundTop);
    p.foundT = num("foundT", p.foundT);
    p.foundOverhang = num("foundOver", p.foundOverhang);
    if (const json::Value* v = root.find("strip"); v && v->isBool()) p.stripFoundation = v->boolean();
    p.stripWidth = num("stripW", p.stripWidth);
    p.leanT = num("lean", p.leanT);
    p.leanOverhang = num("leanOver", p.leanOverhang);
    p.wallT = num("wallT", p.wallT);
    if (const json::Value* c = root.find("classes"); c && c->isArray() && c->arr().size() == 4) {
        std::string* dst[4] = {&p.classLean, &p.classFound, &p.classFrame, &p.classSlab};
        for (int i = 0; i < 4; ++i)
            if (c->arr()[static_cast<std::size_t>(i)].isString()) *dst[i] = c->arr()[static_cast<std::size_t>(i)].str();
    }
    if (const json::Value* l = root.find("layers"); l && l->isObject())
        for (const auto& [name, v] : l->obj()) {
            ElemKind k;
            if (v.isString() && kindFromKey(v.str(), k)) lm[name] = k;
        }
}

std::vector<std::unique_ptr<IEntity>> drawPtoTable(const BuildingModel& m, const Vec3& o, double h, const PtoLabels& L) {
    std::vector<std::unique_ptr<IEntity>> out;
    const double rh = h * 2.4;
    const double cw[6] = {h * 12, h * 18, h * 6, h * 9, h * 9, h * 6};
    double W = 0;
    for (double c : cw) W += c;
    auto put = [&](std::unique_ptr<IEntity> e) {
        e->setLayer("PTO");
        out.push_back(std::move(e));
    };
    auto line = [&](double x0, double y0, double x1, double y1) { put(std::make_unique<LineEntity>(Vec3{x0, y0, 0}, Vec3{x1, y1, 0})); };
    auto cell = [&](int col, double y, const std::string& s, bool right = false) {
        double x = o.x;
        for (int i = 0; i < col; ++i) x += cw[i];
        const Vec3 at = right ? Vec3{x + cw[col] - h * 0.5, y - rh / 2, 0} : Vec3{x + h * 0.5, y - rh / 2, 0};
        put(std::make_unique<TextEntity>(at, h, s, 0, right ? TextHAlign::Right : TextHAlign::Left, TextVAlign::Middle));
    };
    double y = o.y;
    put(std::make_unique<TextEntity>(Vec3{o.x, y + h * 0.8, 0}, h * 1.3, L.title));
    line(o.x, y, o.x + W, y);
    const std::string hdr[6] = {L.level, L.element, L.count, L.volume, L.formwork, L.klass};
    for (int c = 0; c < 6; ++c) cell(c, y, hdr[c], c >= 2 && c <= 4);
    y -= rh;
    line(o.x, y, o.x + W, y);
    for (const auto& r : m.pto) {
        cell(0, y, L.levelName ? L.levelName(r.level) : std::to_string(r.level));
        cell(1, y, L.kindName ? L.kindName(r.kind) : kindKey(r.kind));
        cell(2, y, std::to_string(r.count), true);
        cell(3, y, std::format("{:.2f}", r.volume), true);
        cell(4, y, r.kind == ElemKind::Lean ? std::string("-") : std::format("{:.2f}", r.formwork), true);
        cell(5, y, r.concrete);
        y -= rh;
        line(o.x, y, o.x + W, y);
    }
    cell(1, y, L.total);
    cell(3, y, std::format("{:.2f}", m.totalVolume), true);
    cell(4, y, std::format("{:.2f}", m.totalFormwork), true);
    y -= rh;
    line(o.x, y, o.x + W, y);
    double x = o.x;
    for (int c = 0; c <= 6; ++c) {
        line(x, o.y, x, y);
        if (c < 6) x += cw[c];
    }
    return out;
}

} // namespace asiacad::bim
