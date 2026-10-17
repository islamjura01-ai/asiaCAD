#include <asiacad/bim/kj_project.h>

#include <asiacad/core/entities.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <functional>
#include <set>

namespace asiacad::bim {

const char* kjViewKey(KjViewKind k) {
    switch (k) {
        case KjViewKind::FoundMain: return "FOUND_MAIN";
        case KjViewKind::FoundExtraBottom: return "FOUND_BOT";
        case KjViewKind::FoundExtraTop: return "FOUND_TOP";
        case KjViewKind::FoundVertical: return "FOUND_VERT";
        case KjViewKind::FoundSupports: return "FOUND_SUP";
        case KjViewKind::Outlets: return "OUTLETS";
        case KjViewKind::Columns: return "COLUMNS";
        case KjViewKind::SlabMain: return "SLAB_MAIN";
        case KjViewKind::SlabExtraBottom: return "SLAB_BOT";
        case KjViewKind::SlabExtraTop: return "SLAB_TOP";
        case KjViewKind::Other: break;
    }
    return "OTHER";
}

namespace {

struct Txt {
    Vec3 c;
    double h;
    std::string s, u;  // asl va katta harf
};
struct Circ {
    Vec3 c;
    double r;
    std::string layer;
};
struct Poly {
    std::vector<Vec3> pts;  // yopiq bo'lsa oxirgi = birinchi emas
    bool closed;
    std::string layer;
    BBox box;
    double area;
};
struct Seg {
    Vec3 a, b;
    std::string layer;
};
struct Scene {
    std::vector<Txt> texts;
    std::vector<Circ> circles;
    std::vector<Poly> polys;
    std::vector<Seg> lines;
};

double shoelace(const std::vector<Vec3>& v) {
    double a = 0;
    for (std::size_t i = 0; i < v.size(); ++i) {
        const Vec3& p = v[i];
        const Vec3& q = v[(i + 1) % v.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return a / 2;
}
bool inPoly(const std::vector<Vec3>& poly, double x, double y) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec3 &a = poly[i], &b = poly[j];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) in = !in;
    }
    return in;
}
Vec3 center(const BBox& b) { return {(b.min.x + b.max.x) / 2, (b.min.y + b.max.y) / 2, 0}; }
bool inBox(const BBox& b, const Vec3& p) { return p.x >= b.min.x && p.x <= b.max.x && p.y >= b.min.y && p.y <= b.max.y; }

Scene collect(const Document& doc) {
    Scene sc;
    std::function<void(const IEntity&)> visit = [&](const IEntity& e) {
        switch (e.type()) {
            case EntityType::Insert:
                for (const auto& c : static_cast<const InsertEntity&>(e).explode()) visit(*c);
                break;
            case EntityType::Text: {
                const auto& t = static_cast<const TextEntity&>(e);
                if (t.text().empty()) break;
                sc.texts.push_back({center(t.bbox()), t.height(), t.text(), utf8::toUpper(t.text())});
                break;
            }
            case EntityType::Circle: {
                const auto& c = static_cast<const CircleEntity&>(e);
                sc.circles.push_back({c.center(), c.radius(), e.layer()});
                break;
            }
            case EntityType::Line: {
                const auto& l = static_cast<const LineEntity&>(e);
                sc.lines.push_back({l.startPoint(), l.endPoint(), e.layer()});
                break;
            }
            case EntityType::Polyline: {
                const auto& p = static_cast<const PolylineEntity&>(e);
                Poly q;
                q.pts = p.vertices();
                q.closed = p.isClosed();
                if (q.pts.size() < 2) break;
                if (q.pts.size() > 3 && q.pts.front().distanceTo(q.pts.back()) < 1e-9) {
                    q.pts.pop_back();
                    q.closed = true;
                }
                q.layer = e.layer();
                for (const auto& v : q.pts) q.box.expand(v);
                q.area = std::abs(shoelace(q.pts));
                sc.polys.push_back(std::move(q));
                break;
            }
            default: break;
        }
    };
    doc.forEach([&](const IEntity& e) { visit(e); });
    return sc;
}

// ---- matn yordamchilari
bool has(const std::string& u, std::initializer_list<const char*> ks) {
    for (const char* k : ks)
        if (u.find(k) != std::string::npos) return true;
    return false;
}
std::string trimmed(const std::string& s) {
    const auto a = s.find_first_not_of(" \t");
    if (a == std::string::npos) return {};
    const auto b = s.find_last_not_of(" \t");
    return s.substr(a, b - a + 1);
}
bool isLetter(char32_t c) { return (c >= U'А' && c <= U'Я') || c == U'Ё' || (c >= U'A' && c <= U'Z'); }
// O'q nomi: "1", "12", "1Р", "11Р", "1/1", "А", "Б/1", "А'" ... ; family: 0 — raqamli, 1 — raqam+harf (1Р), 2 — harfli
bool axisName(const std::string& s0, int& family) {
    const std::u32string s = utf8::decode(trimmed(s0));
    if (s.empty() || s.size() > 4) return false;
    std::size_t i = 0;
    if (s[0] >= U'0' && s[0] <= U'9') {
        while (i < s.size() && s[i] >= U'0' && s[i] <= U'9') ++i;
        if (i > 2) return false;
        family = 0;
        if (i < s.size() && isLetter(s[i])) { family = 1; ++i; }
    } else if (isLetter(s[0])) {
        family = 2;
        i = 1;
        if (i < s.size() && isLetter(s[i])) return false;  // "АБ" — o'q emas
    } else {
        return false;
    }
    if (i < s.size() && (s[i] == U'/' || s[i] == U'\'')) {
        ++i;
        while (i < s.size() && s[i] >= U'0' && s[i] <= U'9') ++i;
    }
    return i == s.size();
}
double layerDia(const std::string& layer) {  // "d 20", "d22", "Ø12", "D16" -> m
    const std::u32string s = utf8::decode(trimmed(layer));
    if (s.empty()) return 0;
    std::size_t i = 0;
    if (s[0] == U'd' || s[0] == U'D' || s[0] == U'д' || s[0] == U'Д' || s[0] == U'Ø' || s[0] == U'ø') i = 1;
    else return 0;
    while (i < s.size() && (s[i] == U' ' || s[i] == U'_' || s[i] == U'-')) ++i;
    int v = 0, n = 0;
    while (i < s.size() && s[i] >= U'0' && s[i] <= U'9' && n < 3) { v = v * 10 + int(s[i] - U'0'); ++i; ++n; }
    if (n == 0 || i != s.size() || v < 4 || v > 50) return 0;
    return v / 1000.0;
}
bool rebarLayer(const std::string& l) { return layerDia(l) > 0 || isRebarLayer(l); }
// "ш.200", "шаг 200", "ш.200х200", "@200" -> m
double spacingOf(const std::string& u) {
    for (const char* k : {"ШАГ", "Ш.", "QADAM", "STEP", "S="}) {
        auto p = u.find(k);
        if (p == std::string::npos) continue;
        p += std::strlen(k);
        while (p < u.size() && (u[p] == ' ' || u[p] == '.' || u[p] == '=')) ++p;
        int v = 0, n = 0;
        while (p < u.size() && std::isdigit(static_cast<unsigned char>(u[p]))) { v = v * 10 + (u[p] - '0'); ++p; ++n; }
        if (n >= 2 && v >= 50 && v <= 1000) return v / 1000.0;
    }
    return 0;
}
KjViewKind classify(const std::string& u) {
    if (has(u, {"ВЫПУСК", "VYPUSK", "CHIQISH"})) return KjViewKind::Outlets;
    if (has(u, {"КАРКАС", "KARKAS"})) return KjViewKind::FoundSupports;
    const bool found = has(u, {"ФУНДАМЕНТ", "FUNDAMENT", "FOUNDATION"});
    const bool extra = has(u, {"ДОПОЛНИТ", "QO'SHIMCHA", "QOʻSHIMCHA", "ADDITIONAL", "ДОП."});
    const bool top = has(u, {"ВЕРХН", "YUQORI", "TOP"});
    if (found) {
        if (has(u, {"ВЕРТИКАЛ", "VERTIKAL", "VERTICAL"})) return KjViewKind::FoundVertical;
        if (extra) return top ? KjViewKind::FoundExtraTop : KjViewKind::FoundExtraBottom;
        return KjViewKind::FoundMain;
    }
    if (has(u, {"КОЛОНН", "USTUN", "COLUMN"}) || (has(u, {"СТЕН", "DEVOR", "WALL"}) && !has(u, {"ПЛИТ", "PLITA", "SLAB"})))
        return KjViewKind::Columns;
    if (has(u, {"ПЕРЕКРЫТ", "ПОКРЫТ", "ПЛИТ", "YOPMA", "PLITA", "SLAB"})) {
        if (extra) return top ? KjViewKind::SlabExtraTop : KjViewKind::SlabExtraBottom;
        return KjViewKind::SlabMain;
    }
    return KjViewKind::Other;
}

// ---- o'q pufakchalari
struct Bubble {
    Vec3 c;
    double r;
    std::string name;
    int family;
};
std::vector<Bubble> findBubbles(const Scene& sc) {
    std::vector<const Circ*> cs;
    for (const auto& c : sc.circles) cs.push_back(&c);
    std::sort(cs.begin(), cs.end(), [](auto* a, auto* b) { return a->c.x < b->c.x; });
    std::vector<Bubble> out;
    for (const auto& t : sc.texts) {
        int fam = 0;
        if (!axisName(t.s, fam)) continue;
        const double rmax = t.h * 2.5;
        auto it = std::lower_bound(cs.begin(), cs.end(), t.c.x - rmax, [](const Circ* c, double x) { return c->c.x < x; });
        const Circ* best = nullptr;
        for (; it != cs.end() && (*it)->c.x <= t.c.x + rmax; ++it) {
            const Circ* c = *it;
            if (c->r < t.h * 0.5 || c->r > rmax) continue;
            if (std::hypot(c->c.x - t.c.x, c->c.y - t.c.y) > c->r * 0.5) continue;
            if (!best || c->r < best->r) best = c;
        }
        if (best) out.push_back({best->c, best->r, utf8::toUpper(trimmed(t.s)), fam});
    }
    return out;
}

double median(std::vector<double> v) {
    if (v.empty()) return 0;
    std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
    return v[v.size() / 2];
}

double kjClusterK() { const char* e = std::getenv("KJ_K"); return e ? std::atof(e) : 30; }
struct RawView {
    KjView v;
    std::vector<Bubble> bubbles;
};

}  // namespace

std::vector<KjView> detectKjViews(const Document& doc, const std::vector<DxfDimInfo>& dims) {
    const Scene sc = collect(doc);
    const auto bubbles = findBubbles(sc);
    std::vector<KjView> out;
    if (bubbles.size() < 4) return out;
    std::vector<double> rs;
    for (const auto& b : bubbles) rs.push_back(b.r);
    const double thr = median(rs) * kjClusterK();
    // bir bog'lamli klasterlash (X bo'yicha saralangan — oddiy birlashtirish)
    std::vector<int> parent(bubbles.size());
    for (std::size_t i = 0; i < parent.size(); ++i) parent[i] = static_cast<int>(i);
    std::function<int(int)> root = [&](int i) { return parent[i] == i ? i : parent[i] = root(parent[i]); };
    std::vector<std::size_t> idx(bubbles.size());
    for (std::size_t i = 0; i < idx.size(); ++i) idx[i] = i;
    std::sort(idx.begin(), idx.end(), [&](auto a, auto b) { return bubbles[a].c.x < bubbles[b].c.x; });
    for (std::size_t a = 0; a < idx.size(); ++a)
        for (std::size_t b = a + 1; b < idx.size() && bubbles[idx[b]].c.x - bubbles[idx[a]].c.x <= thr; ++b)
            if (std::hypot(bubbles[idx[a]].c.x - bubbles[idx[b]].c.x, bubbles[idx[a]].c.y - bubbles[idx[b]].c.y) <= thr)
                parent[root(static_cast<int>(idx[a]))] = root(static_cast<int>(idx[b]));
    std::map<int, std::vector<Bubble>> groups;
    for (std::size_t i = 0; i < bubbles.size(); ++i) groups[root(static_cast<int>(i))].push_back(bubbles[i]);

    for (auto& [g, all] : groups) {
        if (all.size() < 4) continue;
        KjView v;
        double rmed = 0;
        for (const auto& b : all) rmed += b.r;
        rmed /= all.size();
        // Plan pufakchalari qator (bir xil Y — vertikal o'qlar) va ustun (bir xil X — gorizontal o'qlar) bo'lib turadi;
        // kesim va tugunlardagi pufakchalar qisqa qatorlar — tashlab yuboriladi.
        auto lines = [&](bool rows) {
            std::vector<const Bubble*> s;
            for (const auto& b : all) s.push_back(&b);
            std::sort(s.begin(), s.end(), [&](auto* p, auto* q) { return rows ? p->c.y < q->c.y : p->c.x < q->c.x; });
            std::vector<std::vector<const Bubble*>> out;
            for (auto* b : s) {
                const double c = rows ? b->c.y : b->c.x;
                if (!out.empty() && std::abs(c - (rows ? out.back().front()->c.y : out.back().front()->c.x)) < rmed * 0.5) out.back().push_back(b);
                else out.push_back({b});
            }
            auto distinct = [](const std::vector<const Bubble*>& l) {
                std::set<std::string> n;
                for (auto* b : l) n.insert(b->name);
                return n.size();
            };
            std::size_t mx = 0;
            for (const auto& l : out) mx = std::max(mx, distinct(l));
            std::vector<std::vector<const Bubble*>> sel;
            for (auto& l : out)
                if (distinct(l) >= 2 && distinct(l) * 2 >= mx) sel.push_back(std::move(l));
            return sel;
        };
        const auto rows = lines(true), cols = lines(false);
        std::map<std::string, std::vector<double>> vx, hy;   // vertikal o'qlar X, gorizontal o'qlar Y
        std::vector<const Bubble*> used;
        for (const auto& l : rows)
            for (auto* b : l) { vx[b->name].push_back(b->c.x); used.push_back(b); }
        for (const auto& l : cols)
            for (auto* b : l) { hy[b->name].push_back(b->c.y); used.push_back(b); }
        if (used.size() < 4) {  // qator/ustun yo'q — barcha pufakchalar, raqamli vertikal, harfli gorizontal
            used.clear();
            for (const auto& b : all) {
                (b.family == 2 ? hy[b.name] : vx[b.name]).push_back(b.family == 2 ? b.c.y : b.c.x);
                used.push_back(&b);
            }
        }
        for (auto* b : used) v.box.expand(b->c);
        v.box.min = v.box.min - Vec3{rmed * 2, rmed * 2, 0};
        v.box.max = v.box.max + Vec3{rmed * 2, rmed * 2, 0};
        v.box.min.z = v.box.max.z = 0;
        // masshtab: ko'rinishdagi o'lchamlarning eng ko'p DIMLFAC'i
        std::map<long, int> votes;
        for (const auto& d : dims) {
            const Vec3 m{(d.p1.x + d.p2.x) / 2, (d.p1.y + d.p2.y) / 2, 0};
            if (inBox(v.box, m) && d.p1.distanceTo(d.p2) > 1e-9) votes[std::lround(d.lfac * 1000)]++;
        }
        long best = 1000;
        int bc = 0;
        for (auto [k, c] : votes)
            if (c > bc) { bc = c; best = k; }
        v.scale = best / 1000.0;
        std::map<std::string, double> ax, ay;
        for (auto& [n, xs] : vx) ax[n] = median(xs);
        for (auto& [n, ys] : hy) ay[n] = median(ys);
        const auto i1 = ax.find("1");
        const auto iA = ay.find(utf8::toUpper("А"));
        const auto iAl = ay.find("A");
        v.aligned = i1 != ax.end() && (iA != ay.end() || iAl != ay.end());
        v.origin = {i1 != ax.end() ? i1->second : v.box.min.x,
                    iA != ay.end() ? iA->second : (iAl != ay.end() ? iAl->second : v.box.min.y), 0};
        for (const auto& [n, x] : ax) v.axesX[n] = (x - v.origin.x) * v.scale;
        for (const auto& [n, y] : ay) v.axesY[n] = (y - v.origin.y) * v.scale;
        // sarlavha: "ПЛАН"/"СХЕМА" so'zli eng yaqin matn (odatda tepada)
        const double w = v.box.max.x - v.box.min.x, h = v.box.max.y - v.box.min.y;
        struct Cand { double d, h; const Txt* t; };
        std::vector<Cand> cands;
        double maxH = 0;
        for (const auto& t : sc.texts) {
            if (utf8::length(t.s) < 15 ||
                !has(t.u, {"ПЛАН", "СХЕМА", "АРМИРОВАН", "ОПАЛУБ", "РАСПОЛОЖ", "PLAN", "SXEMA", "ARMATURA", "SCHEME", "LAYOUT", "REINFORCEMENT"}))
                continue;
            if (t.c.x < v.box.min.x - w * 0.3 || t.c.x > v.box.max.x + w * 0.3) continue;
            const double dx = std::max({v.box.min.x - t.c.x, 0.0, t.c.x - v.box.max.x});
            const double dy = std::max({v.box.min.y - t.c.y, 0.0, t.c.y - v.box.max.y});
            const double d = std::hypot(dx, dy) + (t.c.y < v.box.min.y ? h * 0.1 : 0);  // pastdagilar biroz kamroq afzal
            if (d > std::max(w, h) * 0.4) continue;
            cands.push_back({d, t.h, &t});
            maxH = std::max(maxH, t.h);
        }
        // sarlavha — eng katta shriftli (izohlar kichikroq), ular orasida eng yaqini
        double bestD = 1e18;
        for (const auto& c : cands)
            if (c.h >= maxH * 0.9 && c.d < bestD) { bestD = c.d; v.title = c.t->s; }
        v.kind = classify(utf8::toUpper(v.title));
        for (const auto& t : sc.texts) {
            if (!inBox(v.box, t.c)) continue;
            double lv;
            if (parseLevelText(t.s, lv) && std::abs(lv) < 200) v.levels.push_back(lv);
        }
        std::sort(v.levels.begin(), v.levels.end());
        v.levels.erase(std::unique(v.levels.begin(), v.levels.end(), [](double a, double b) { return std::abs(a - b) < 1e-6; }), v.levels.end());
        out.push_back(std::move(v));
    }
    std::sort(out.begin(), out.end(), [](const KjView& a, const KjView& b) { return a.box.min.x < b.box.min.x || (a.box.min.x == b.box.min.x && a.box.max.y > b.box.max.y); });
    // "1"/"А" topilmagan ko'rinish: boshqa ko'rinish bilan umumiy o'q nomlari orqali
    const KjView* ref = nullptr;
    for (const auto& v : out)
        if (v.aligned && (!ref || v.axesX.size() + v.axesY.size() > ref->axesX.size() + ref->axesY.size())) ref = &v;
    if (ref)
        for (auto& v : out) {
            if (v.aligned) continue;
            Vec3 o = v.origin;
            bool okx = false, oky = false;
            for (const auto& [n, x] : v.axesX)
                if (auto it = ref->axesX.find(n); it != ref->axesX.end()) {
                    o.x = v.origin.x + (x - it->second) / v.scale;
                    okx = true;
                    break;
                }
            for (const auto& [n, y] : v.axesY)
                if (auto it = ref->axesY.find(n); it != ref->axesY.end()) {
                    o.y = v.origin.y + (y - it->second) / v.scale;
                    oky = true;
                    break;
                }
            if (!okx || !oky) continue;
            for (auto& [n, x] : v.axesX) x -= (o.x - v.origin.x) * v.scale;
            for (auto& [n, y] : v.axesY) y -= (o.y - v.origin.y) * v.scale;
            v.origin = o;
            v.aligned = true;
        }
    return out;
}

namespace {

struct Ctx {
    const KjParams& p;
    KjProject& r;
    double fTop, fBot, sTop, sBot;
    std::string cls = "A400";
    std::map<std::tuple<int, int, int, long, std::string>, ScheduleRow> rows;  // kind, level, role, d(mm), cls

    void bar(ElemKind k, int level, BarRole role, double d, const std::string& c, std::vector<Vec3> pts, double extraLen = 0) {
        double L = 0;
        for (std::size_t i = 1; i < pts.size(); ++i) L += pts[i - 1].distanceTo(pts[i]);
        // savdo uzunligidan uzun sterjenlar — nahlyostka (40d) har ulanishga
        const double stock = 11.7;
        if (L > stock) L += std::floor(L / stock) * 40 * d;
        L += extraLen;
        auto& row = rows[{static_cast<int>(k), level, static_cast<int>(role), std::lround(d * 1000), c}];
        row.kind = k;
        row.level = level;
        row.role = role;
        row.d = d;
        row.cls = c;
        row.count += 1;
        row.totalLength += L;
        r.bars.push_back({k, level, role, d, c, std::move(pts), L});
    }
};

// Ko'pburchakni gorizontal (dir=0) yoki vertikal (dir=1) to'g'ri chiziq bilan kesish: ichki oraliqlar
std::vector<std::pair<double, double>> scan(const std::vector<std::vector<Vec3>>& rings, int dir, double c) {
    std::vector<double> xs;
    for (const auto& ring : rings)
        for (std::size_t i = 0; i < ring.size(); ++i) {
            const Vec3& a = ring[i];
            const Vec3& b = ring[(i + 1) % ring.size()];
            const double a1 = dir == 0 ? a.y : a.x, b1 = dir == 0 ? b.y : b.x;
            const double a2 = dir == 0 ? a.x : a.y, b2 = dir == 0 ? b.x : b.y;
            if ((a1 > c) == (b1 > c)) continue;
            xs.push_back(a2 + (b2 - a2) * (c - a1) / (b1 - a1));
        }
    std::sort(xs.begin(), xs.end());
    std::vector<std::pair<double, double>> out;
    for (std::size_t i = 0; i + 1 < xs.size(); i += 2) out.push_back({xs[i], xs[i + 1]});
    return out;
}

void mesh(Ctx& cx, const std::vector<std::vector<Vec3>>& rings, ElemKind k, int level, double d, const std::string& cls, double s,
          double zBot, double zTop) {
    BBox b;
    for (const auto& v : rings[0]) b.expand(v);
    const double cov = cx.p.cover;
    for (int dir = 0; dir < 2; ++dir) {
        const double lo = (dir == 0 ? b.min.y : b.min.x) + cov + d / 2, hi = (dir == 0 ? b.max.y : b.max.x) - cov - d / 2;
        const int n = std::max(1, static_cast<int>(std::floor((hi - lo) / s + 1e-9)) + 1);
        for (int i = 0; i < n; ++i) {
            const double c = lo + i * s;
            for (auto [a0, a1] : scan(rings, dir, c)) {
                a0 += cov;
                a1 -= cov;
                if (a1 - a0 < 0.3) continue;
                const double zb = zBot + cov + d / 2 + (dir == 1 ? d : 0);
                const double zt = zTop - cov - d / 2 - (dir == 1 ? d : 0);
                auto P = [&](double t, double z) { return dir == 0 ? Vec3{t, c, z} : Vec3{c, t, z}; };
                cx.bar(k, level, dir == 0 ? BarRole::BottomX : BarRole::BottomY, d, cls, {P(a0, zb), P(a1, zb)});
                cx.bar(k, level, dir == 0 ? BarRole::TopX : BarRole::TopY, d, cls, {P(a0, zt), P(a1, zt)});
            }
        }
    }
}

}  // namespace

KjProject buildKjProject(const Document& doc, const std::vector<DxfDimInfo>& dims, const KjParams& p, const std::vector<KjView>* given) {
    KjProject r;
    r.views = given ? *given : detectKjViews(doc, dims);
    auto issue = [&](bool err, const std::string& k, std::vector<std::string> a = {}) { r.issues.push_back({err, k, std::move(a)}); };
    if (r.views.empty()) {
        issue(true, "kj.chk.no_views");
        return r;
    }
    const Scene sc = collect(doc);

    // ---- belgilar va beton sinfi (butun fayl bo'yicha)
    std::map<long, int> lvCount;   // faqat plita/ustun ko'rinishlari atrofidagi (kesimlar bilan) belgilar
    for (const auto& t : sc.texts) {
        bool near = false;
        for (const auto& v : r.views) {
            if (!(v.kind == KjViewKind::SlabMain || v.kind == KjViewKind::SlabExtraBottom || v.kind == KjViewKind::SlabExtraTop ||
                  v.kind == KjViewKind::Columns))
                continue;
            const double w = v.box.max.x - v.box.min.x, h = v.box.max.y - v.box.min.y;
            near = near || (t.c.x > v.box.min.x - w * 0.5 && t.c.x < v.box.max.x + w * 0.5 && t.c.y > v.box.min.y - h * 0.5 &&
                            t.c.y < v.box.max.y + h * 0.5);
        }
        double l;
        if (near && parseLevelText(t.s, l) && std::abs(l) < 200) lvCount[std::lround(l * 1000)]++;
    }
    // plita: (ust, ost) belgilar jufti, farqi 0.12..0.45 — ikkalasi ham ko'p uchraydigan ("+3.050" / "+2.850")
    double slabTop = p.slabTop, slabT = p.slabT;
    if (slabTop <= 0 || slabT <= 0) {
        int bestScore = 0;
        double bt = 0, bb = 0;
        for (auto [kt, ct] : lvCount) {
            if (kt < 1000 || kt > 12000) continue;
            if (p.slabTop > 0 && std::abs(kt - std::lround(p.slabTop * 1000)) > 1) continue;
            for (auto [kb, cb] : lvCount) {
                const long d = kt - kb;
                if (d < 120 || d > 450) continue;
                const int sc = std::min(ct, cb);
                if (sc > bestScore || (sc == bestScore && kt > std::lround(bt * 1000))) { bestScore = sc; bt = kt / 1000.0; bb = kb / 1000.0; }
            }
        }
        if (slabTop <= 0) slabTop = bestScore > 0 ? bt : 0;
        if (slabT <= 0 && bestScore > 0) slabT = bt - bb;
        if (slabTop <= 0) {
            int bc = 0;
            for (auto [k, c] : lvCount)
                if (k > 1000 && k < 12000 && c > bc) { bc = c; slabTop = k / 1000.0; }
        }
        if (slabTop <= 0) { slabTop = 3.0; issue(false, "kj.chk.assumed_level", {"3.000"}); }
        if (slabT <= 0) slabT = 0.2;
    }
    r.slabTop = slabTop;
    r.slabT = slabT;
    std::string concrete = p.concrete;
    if (concrete.empty()) {
        std::map<std::string, int> cc;
        for (const auto& t : sc.texts) {
            if (!has(t.u, {"БЕТОН", "BETON", "CONCRETE"})) continue;
            std::string s = t.u;
            for (const char* k : {"В", "B"}) {
                const std::string K = k;
                for (std::size_t q = s.find(K); q != std::string::npos; q = s.find(K, q + 1)) {
                    std::size_t e = q + K.size();
                    std::string num;
                    while (e < s.size() && (std::isdigit(static_cast<unsigned char>(s[e])) || (s[e] == '.' && !num.empty()))) num += s[e++];
                    if (num.empty()) continue;
                    const double v = std::atof(num.c_str());
                    if (v >= 10 && v <= 100) cc["B" + num]++;
                }
            }
        }
        int bc = 0;
        for (const auto& [k, c] : cc)
            if (c > bc) { bc = c; concrete = k; }
        if (concrete.empty()) concrete = "B25";
    }
    r.concrete = concrete;
    const double fTop = p.foundTop, fBot = p.foundTop - p.foundT;
    issue(false, "kj.chk.found_assumed", {std::format("{:.3f}", fTop), std::format("{:.2f}", p.foundT)});

    Ctx cx{p, r, fTop, fBot, slabTop, slabTop - slabT, "A400", {}};

    // ---- har ko'rinish
    std::vector<std::vector<Vec3>> foundRings, slabRings;
    std::vector<std::vector<Vec3>> columns, walls;
    double stirS = p.stirS, stirSEnd = p.stirSEnd;   // ustunlar sxemasi atrofidagi "100/200" dan
    struct Dots { double d; std::vector<Vec3> pts; KjViewKind kind; };
    std::vector<Dots> dots;
    for (auto& v : r.views) {
        if (!v.use) continue;
        struct Count {  // ko'rinishdan qo'shilgan sterjenlar (continue'larda ham)
            KjView& v;
            const std::vector<Bar>& bars;
            std::size_t b0;
            ~Count() { v.bars += static_cast<int>(bars.size() - b0); }
        } count{v, r.bars, r.bars.size()};
        if (!v.aligned && v.kind != KjViewKind::Other && v.kind != KjViewKind::FoundSupports) issue(false, "kj.chk.not_aligned", {v.title});
        auto L = [&](const Vec3& q) { return Vec3{(q.x - v.origin.x) * v.scale, (q.y - v.origin.y) * v.scale, 0}; };
        // matn belgilari
        std::map<std::string, int> markVotes;
        std::map<std::string, BarMark> markOf;
        std::vector<std::pair<Vec3, double>> spacings;   // (joy, qadam)
        std::set<long> labelDias;
        std::vector<Vec3> exclude;                       // "Приямок" yozuvlari
        for (const auto& t : sc.texts) {
            if (!inBox(v.box, t.c)) continue;
            if (has(t.u, {"ПРИЯМОК", "ОТВЕРСТ", "ПРОЕМ", "ПРОЁМ", "ЛЮК", "CHUQURCHA", "TESHIK"})) exclude.push_back(L(t.c));
            const double s = spacingOf(t.u);
            if (s > 0) spacings.push_back({L(t.c), s});
            BarMark m;
            if (parseBarMark(t.s, m)) {
                labelDias.insert(std::lround(m.d * 1000));
                if (m.spacing == 0) m.spacing = s;
                const std::string k = std::format("{}|{}|{}", std::lround(m.d * 1000), m.cls, std::lround(m.spacing * 1000));
                markVotes[k]++;
                markOf[k] = m;
            }
        }
        auto nearestSpacing = [&](const Vec3& at, double def) {
            double bd = 1e18, s = def;
            for (const auto& [q, sp] : spacings)
                if (double d = q.distanceTo(at); d < bd) { bd = d; s = sp; }
            return bd < 12 ? s : def;
        };
        std::map<double, int> spVotes;
        for (const auto& [q, sp] : spacings) spVotes[sp]++;
        double viewStep = p.meshStep;
        {
            int bc = 0;
            for (auto [s, c] : spVotes)
                if (c > bc) { bc = c; viewStep = s; }
        }

        // poliliniyalar (mahalliy, haqiqiy o'lchamda)
        struct LP { std::vector<Vec3> pts; bool closed; std::string layer; double area; BBox box; };
        std::vector<LP> lps;
        for (const auto& q : sc.polys) {
            if (!inBox(v.box, center(q.box))) continue;
            if (q.box.max.x - q.box.min.x > (v.box.max.x - v.box.min.x) * 1.05) continue;
            LP lp;
            for (const auto& x : q.pts) { lp.pts.push_back(L(x)); lp.box.expand(lp.pts.back()); }
            lp.closed = q.closed;
            lp.layer = q.layer;
            lp.area = q.area * v.scale * v.scale;
            lps.push_back(std::move(lp));
        }
        // kontur — eng katta (deyarli) yopiq poliliniya
        auto outline = [&]() -> std::vector<std::vector<Vec3>> {
            const LP* best = nullptr;
            for (const auto& q : lps) {
                if (q.pts.size() < 4 || rebarLayer(q.layer)) continue;
                const double diag = std::hypot(q.box.max.x - q.box.min.x, q.box.max.y - q.box.min.y);
                if (!q.closed && q.pts.front().distanceTo(q.pts.back()) > diag * 0.02) continue;
                if (!best || q.area > best->area) best = &q;
            }
            if (!best || best->area < 4) return {};
            std::vector<std::vector<Vec3>> rings{best->pts};
            if (shoelace(rings[0]) < 0) std::reverse(rings[0].begin(), rings[0].end());
            v.outlineArea = best->area;
            // teshiklar: shu qatlamdagi ichki yopiq konturlar (≥ 1 m²)
            for (const auto& q : lps) {
                if (&q == best || !q.closed || q.layer != best->layer || q.area < 1.0 || q.area > best->area * 0.3) continue;
                const Vec3 c = center(q.box);
                if (!inPoly(best->pts, c.x, c.y)) continue;
                auto h = q.pts;
                if (shoelace(h) > 0) std::reverse(h.begin(), h.end());
                rings.push_back(std::move(h));
            }
            return rings;
        };
        // to'rtburchak (ustun / devor / zona)
        struct Rect { Vec3 o, u, w; double a, b; Vec3 c; };  // o + u*[0..a] + w*[0..b]
        auto rectOf = [](const LP& q, Rect& rc) {
            if (!q.closed || q.pts.size() != 4) return false;
            const Vec3 e1 = q.pts[1] - q.pts[0], e2 = q.pts[2] - q.pts[1];
            const double a = e1.length(), b = e2.length();
            if (a < 1e-6 || b < 1e-6) return false;
            if (std::abs((e1.x * e2.x + e1.y * e2.y) / (a * b)) > 0.02) return false;
            if (q.pts[3].distanceTo(q.pts[0] + e2) > (a + b) * 0.01) return false;
            rc = {q.pts[0], e1 * (1 / a), e2 * (1 / b), a, b, center(q.box)};
            return true;
        };

        if (v.kind == KjViewKind::FoundMain || v.kind == KjViewKind::SlabMain) {
            auto rings = outline();
            if (rings.empty()) {
                issue(true, "kj.chk.no_outline", {v.title});
            } else {
                (v.kind == KjViewKind::FoundMain ? foundRings : slabRings) = rings;
                // asosiy to'r
                std::string bestK;
                int bc = 0;
                for (const auto& [k, c] : markVotes)
                    if (c > bc) { bc = c; bestK = k; }
                BarMark m;
                if (!bestK.empty()) m = markOf[bestK];
                else issue(false, "kj.chk.no_mark", {v.title});
                const double d = m.d > 0 ? m.d : 0.012;
                const std::string cls = m.cls.empty() ? "A400" : m.cls;
                const double s = m.spacing > 0 ? m.spacing : viewStep;
                v.mainMark = std::format("\xC3\x98{} {} s{}", std::lround(d * 1000), cls, std::lround(s * 1000));
                if (v.kind == KjViewKind::FoundMain) mesh(cx, rings, ElemKind::FoundationSlab, 0, d, cls, s, fBot, fTop);
                else mesh(cx, rings, ElemKind::Slab, 1, d, cls, s, slabTop - slabT, slabTop);
                if (!m.cls.empty()) cx.cls = m.cls;
            }
        }
        if (v.kind == KjViewKind::Columns) {
            const double w = v.box.max.x - v.box.min.x, h = v.box.max.y - v.box.min.y;
            for (const auto& t : sc.texts) {
                if (t.c.x < v.box.min.x - w * 0.5 || t.c.x > v.box.max.x + w * 0.5 || t.c.y < v.box.min.y - h * 0.5 || t.c.y > v.box.max.y + h * 0.5)
                    continue;
                const std::string q = trimmed(t.s);
                const auto sl = q.find('/');
                if (sl == std::string::npos || q.find_first_not_of("0123456789/") != std::string::npos) continue;
                const int a = std::atoi(q.substr(0, sl).c_str()), b = std::atoi(q.substr(sl + 1).c_str());
                if (a >= 50 && b > a && b <= 400) { stirSEnd = a / 1000.0; stirS = b / 1000.0; }
            }
            std::vector<Rect> cand;
            for (const auto& q : lps) {
                Rect rc;
                if (rebarLayer(q.layer) || !rectOf(q, rc)) continue;
                const double a = std::min(rc.a, rc.b), b = std::max(rc.a, rc.b);
                const bool col = a >= 0.2 && a <= 1.2 && b / a <= 3;
                const bool wall = a >= 0.12 && a <= 0.6 && b / a > 3 && b <= 40;
                if (!col && !wall) continue;
                bool dup = false;
                for (const auto& o : cand) dup = dup || (o.c.distanceTo(rc.c) < 0.05 && std::abs(o.a * o.b - rc.a * rc.b) < 0.01);
                if (!dup) cand.push_back(rc);
            }
            std::vector<bool> drop(cand.size(), false);
            for (const auto& e : exclude) {
                int bi = -1;
                double bd = 8;
                for (std::size_t i = 0; i < cand.size(); ++i) {
                    const double d = cand[i].c.distanceTo(e);
                    const double a = std::min(cand[i].a, cand[i].b), b = std::max(cand[i].a, cand[i].b);
                    if (b / a > 3) continue;
                    if (d < bd) { bd = d; bi = static_cast<int>(i); }
                }
                if (bi >= 0) drop[bi] = true;
            }
            for (std::size_t i = 0; i < cand.size(); ++i) {
                if (drop[i]) continue;
                const auto& rc = cand[i];
                std::vector<Vec3> ring{rc.o, rc.o + rc.u * rc.a, rc.o + rc.u * rc.a + rc.w * rc.b, rc.o + rc.w * rc.b};
                if (shoelace(ring) < 0) std::reverse(ring.begin(), ring.end());
                const double a = std::min(rc.a, rc.b), b = std::max(rc.a, rc.b);
                if (b / a > 3) { walls.push_back(ring); ++v.walls; }
                else { columns.push_back(ring); ++v.columns; }
            }
        }
        // ---- armatura qatlamlari
        const bool found = v.kind == KjViewKind::FoundMain || v.kind == KjViewKind::FoundExtraBottom || v.kind == KjViewKind::FoundExtraTop ||
                           v.kind == KjViewKind::FoundVertical || v.kind == KjViewKind::Outlets;
        const bool slab = v.kind == KjViewKind::SlabMain || v.kind == KjViewKind::SlabExtraBottom || v.kind == KjViewKind::SlabExtraTop;
        if (!found && !slab) continue;
        // aylanalar — vertikal sterjenlar
        if (v.kind == KjViewKind::FoundVertical || v.kind == KjViewKind::Outlets || v.kind == KjViewKind::SlabExtraBottom ||
            v.kind == KjViewKind::SlabExtraTop) {
            std::map<long, Dots> byD;
            for (const auto& c : sc.circles) {
                if (!inBox(v.box, c.c) || !rebarLayer(c.layer)) continue;
                const double rr = c.r * v.scale;
                if (rr > 0.03) continue;
                double d = layerDia(c.layer);
                if (d <= 0) d = std::round(rr * 2 * 1000) / 1000;
                auto& g = byD[std::lround(d * 1000)];
                g.d = d;
                g.kind = v.kind;
                g.pts.push_back(L(c.c));
            }
            for (auto& [k, g] : byD) {
                v.dots += static_cast<int>(g.pts.size());
                if (!labelDias.empty() && !labelDias.count(k))
                    issue(false, "kj.chk.dia_label", {v.title, std::to_string(k)});
                dots.push_back(std::move(g));
            }
        }
        // chiziqlar — qo'shimcha armatura zonalari
        if (v.kind == KjViewKind::FoundExtraBottom || v.kind == KjViewKind::FoundExtraTop || v.kind == KjViewKind::SlabExtraBottom ||
            v.kind == KjViewKind::SlabExtraTop) {
            std::vector<Rect> zones;
            for (const auto& q : lps) {
                Rect rc;
                if (rebarLayer(q.layer) || !rectOf(q, rc)) continue;
                if (std::min(rc.a, rc.b) < 0.5 || std::max(rc.a, rc.b) > 20) continue;
                zones.push_back(rc);
            }
            const bool top = v.kind == KjViewKind::FoundExtraTop || v.kind == KjViewKind::SlabExtraTop;
            const ElemKind ek = found ? ElemKind::FoundationSlab : ElemKind::Slab;
            const int lvl = found ? 0 : 1;
            const double z0 = found ? fBot : slabTop - slabT, z1 = found ? fTop : slabTop;
            std::set<std::tuple<int, int, long>> done;
            for (const auto& sg : sc.lines) {
                const double d = layerDia(sg.layer);
                if (d <= 0 || !inBox(v.box, Vec3{(sg.a.x + sg.b.x) / 2, (sg.a.y + sg.b.y) / 2, 0})) continue;
                const Vec3 a = L(sg.a), b = L(sg.b);
                const double len = a.distanceTo(b);
                if (len < 0.2) continue;
                ++v.lines;
                const Vec3 dir = (b - a) * (1 / len), mid = (a + b) * 0.5;
                int bi = -1;
                double ba = 1e18;
                for (std::size_t i = 0; i < zones.size(); ++i) {
                    const auto& z = zones[i];
                    const Vec3 rel = mid - z.o;
                    const double pu = rel.x * z.u.x + rel.y * z.u.y, pw = rel.x * z.w.x + rel.y * z.w.y;
                    if (pu < -0.05 || pu > z.a + 0.05 || pw < -0.05 || pw > z.b + 0.05) continue;
                    const double cu = std::abs(dir.x * z.u.x + dir.y * z.u.y);
                    const double ext = cu > 0.98 ? z.a : (cu < 0.02 ? z.b : -1);
                    if (ext < 0 || len < ext * 0.75 || len > ext * 1.3) continue;
                    if (z.a * z.b < ba) { ba = z.a * z.b; bi = static_cast<int>(i); }
                }
                const BarRole role = (std::abs(dir.x) >= std::abs(dir.y)) ? (top ? BarRole::TopX : BarRole::BottomX)
                                                                           : (top ? BarRole::TopY : BarRole::BottomY);
                const double zz = top ? z1 - p.cover - d / 2 - (role == BarRole::TopY ? d : 0) : z0 + p.cover + d / 2 + (role == BarRole::BottomY ? d : 0);
                std::string zcls = cx.cls;
                for (const auto& [k, m] : markOf)
                    if (std::lround(m.d * 1000) == std::lround(d * 1000) && !m.cls.empty()) zcls = m.cls;
                if (bi < 0) {
                    cx.bar(ek, lvl, role, d, zcls, {Vec3{a.x, a.y, zz}, Vec3{b.x, b.y, zz}});
                    continue;
                }
                const auto& z = zones[bi];
                const bool alongU = std::abs(dir.x * z.u.x + dir.y * z.u.y) > 0.5;
                if (!done.insert({bi, alongU ? 0 : 1, std::lround(d * 1000)}).second) continue;
                ++v.zones;
                const Vec3 U = alongU ? z.u : z.w, W = alongU ? z.w : z.u;
                const double lu = alongU ? z.a : z.b, lw = alongU ? z.b : z.a;
                const double s = nearestSpacing(z.c, viewStep);
                const int n = std::max(1, static_cast<int>(std::lround(lw / s)));
                for (int i = 0; i < n; ++i) {
                    const Vec3 o = z.o + W * ((i + 0.5) * lw / n);
                    const Vec3 e = o + U * lu;
                    cx.bar(ek, lvl, role, d, zcls, {Vec3{o.x, o.y, zz}, Vec3{e.x, e.y, zz}});
                }
            }
        }
    }

    // ---- ustun karkaslari (vypusklar ustun ichida bo'lsa — asosiy sterjenlar shu joyda, ust plitaga kiradi)
    const double colTop0 = slabRings.empty() ? slabTop : slabTop - slabT;
    if (p.columnCages) {
        int caged = 0;
        for (const auto& c : columns) {
            BBox b;
            for (const auto& q : c) b.expand(q);
            std::vector<std::pair<Vec3, double>> in;
            for (const auto& g : dots)
                if (g.kind == KjViewKind::Outlets)
                    for (const auto& q : g.pts)
                        if (inPoly(c, q.x, q.y)) in.push_back({q, g.d});
            if (in.empty()) continue;
            ++caged;
            const double zTop = slabRings.empty() ? colTop0 : slabTop - p.cover;
            for (const auto& [q, d] : in) cx.bar(ElemKind::Column, 1, BarRole::Main, d, cx.cls, {Vec3{q.x, q.y, fTop}, Vec3{q.x, q.y, zTop}});
            const double inset = p.cover + p.stirD / 2;
            if (b.max.x - b.min.x < 2 * inset + 0.05 || b.max.y - b.min.y < 2 * inset + 0.05) continue;
            const double H = colTop0 - fTop, endZ = std::max(0.5, H / 6);
            std::vector<double> zs;
            for (double z = 0.05; z < H - 0.04; z += (z < endZ || z > H - endZ) ? stirSEnd : stirS) zs.push_back(fTop + z);
            for (double z : zs)
                cx.bar(ElemKind::Column, 1, BarRole::Stirrup, p.stirD, "A240",
                       {Vec3{b.min.x + inset, b.min.y + inset, z}, Vec3{b.max.x - inset, b.min.y + inset, z},
                        Vec3{b.max.x - inset, b.max.y - inset, z}, Vec3{b.min.x + inset, b.max.y - inset, z},
                        Vec3{b.min.x + inset, b.min.y + inset, z}},
                       0.2);  // ilgaklar
        }
        if (caged == 0 && !columns.empty()) issue(false, "kj.chk.no_cages");
    }

    // ---- vertikal sterjenlar
    for (const auto& g : dots) {
        for (const auto& q : g.pts) {
            switch (g.kind) {
                case KjViewKind::Outlets:
                    cx.bar(ElemKind::Column, 0, BarRole::Main, g.d, cx.cls, {Vec3{q.x, q.y, fTop - p.outletIn}, Vec3{q.x, q.y, fTop + p.outletOut}});
                    break;
                case KjViewKind::FoundVertical:
                    cx.bar(ElemKind::FoundationSlab, 0, BarRole::Vertical, g.d, cx.cls,
                           {Vec3{q.x, q.y, fBot + p.cover}, Vec3{q.x, q.y, fTop - p.cover}});
                    break;
                default:
                    cx.bar(ElemKind::Slab, 1, BarRole::Vertical, g.d, cx.cls,
                           {Vec3{q.x, q.y, slabTop - slabT + p.cover}, Vec3{q.x, q.y, slabTop - p.cover}});
                    break;
            }
        }
    }

    // ---- beton
    auto prism = [&](ElemKind k, int lv, const std::vector<std::vector<Vec3>>& rings, double z0, double z1, const std::string& c) {
        Prism pr;
        pr.kind = k;
        pr.level = lv;
        pr.outline = rings[0];
        for (std::size_t i = 1; i < rings.size(); ++i) pr.holes.push_back(rings[i]);
        pr.z0 = z0;
        pr.z1 = z1;
        pr.concrete = c;
        r.bm.prisms.push_back(std::move(pr));
    };
    if (!foundRings.empty()) {
        prism(ElemKind::Lean, 0, {foundRings[0]}, fBot - p.leanT, fBot, "B7.5");
        prism(ElemKind::FoundationSlab, 0, foundRings, fBot, fTop, concrete);
    }
    const double colTop = slabRings.empty() ? slabTop : slabTop - slabT;
    for (const auto& c : columns) prism(ElemKind::Column, 1, {c}, fTop, colTop, concrete);
    for (const auto& w : walls) prism(ElemKind::Wall, 1, {w}, fTop, colTop, concrete);
    if (!slabRings.empty()) prism(ElemKind::Slab, 1, slabRings, slabTop - slabT, slabTop, concrete);
    r.bm.heightTop = slabTop;
    computePto(r.bm);
    if (foundRings.empty()) issue(true, "kj.chk.no_found");
    if (columns.empty()) issue(false, "kj.chk.no_columns");
    // ustunlar plita ichidami
    if (!slabRings.empty()) {
        int out = 0;
        for (const auto& c : columns) {
            BBox b;
            for (const auto& q : c) b.expand(q);
            const Vec3 m = center(b);
            if (!inPoly(slabRings[0], m.x, m.y)) ++out;
        }
        if (out) issue(false, "kj.chk.col_outside", {std::to_string(out)});
    }

    // ---- ведомость
    int pos = 0;
    for (auto& [k, row] : cx.rows) {
        row.pos = ++pos;
        row.length = row.count ? row.totalLength / row.count : 0;
        row.mass = row.totalLength * barMassPerM(row.d);
        r.massByDia[std::format("\xC3\x98{} {}", std::lround(row.d * 1000), row.cls)] += row.mass;
        r.totalMass += row.mass;
        r.schedule.push_back(row);
    }
    r.spec = readSpecification(doc);
    for (const auto& s : r.spec) r.specMass += s.mass;
    for (const auto& v : r.views)
        if (v.use && !v.title.empty() && (v.kind == KjViewKind::Other || v.kind == KjViewKind::FoundSupports))
            issue(false, "kj.chk.skipped", {v.title});
    return r;
}

}  // namespace asiacad::bim
