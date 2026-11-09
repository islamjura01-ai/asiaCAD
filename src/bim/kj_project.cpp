#include <asiacad/bim/kj_project.h>

#include <asiacad/core/entities.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <functional>
#include <optional>
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
// sarlavha emas: jadval va izoh sarlavhalari ("Спецификация элементов на плиту Пм-1", "Ведомость расхода стали")
bool notTitle(const std::string& u) { return has(u, {"СПЕЦИФИКАЦ", "ВЕДОМОСТ", "ПРИМЕЧАН", "SPETSIFIKATSIYA", "SPECIFICATION", "QAYDNOMA", "IZOH"}); }
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
    if (has(u, {"КОЛОНН", "USTUN", "COLUMN", "ПИЛОН"}) || (has(u, {"СТЕН", "DEVOR", "WALL"}) && !has(u, {"ПЛИТ", "PLITA", "SLAB"})) ||
        (has(u, {"ВЕРТИКАЛЬН", "VERTIKAL", "VERTICAL"}) && has(u, {"ЭЛЕМЕНТ", "ELEMENT"})))   // "Схема расположения вертикальных элементов"
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
    std::map<int, std::vector<Bubble>> groups0;
    for (std::size_t i = 0; i < bubbles.size(); ++i) groups0[root(static_cast<int>(i))].push_back(bubbles[i]);
    // Yonma-yon (yoki ustma-ust) turgan ko'rinishlar bitta klasterga tushishi mumkin. Belgisi: bitta qatorda (bir xil Y) bir
    // xil o'q nomlari ikki marta ("Ж … Р Ж … Р") — ikkinchi ketma-ketlik boshlanishidan kesiladi (ustunda — Y bo'yicha).
    std::vector<std::vector<Bubble>> groups;
    std::function<void(std::vector<Bubble>, int)> splitRec = [&](std::vector<Bubble> g, int depth) {
        double rm = 0;
        for (const auto& b : g) rm += b.r;
        rm /= std::max<std::size_t>(1, g.size());
        for (int axis = 0; axis < 2 && depth < 8 && g.size() >= 8; ++axis) {
            // axis 0: qatorlar (bir xil Y), X bo'yicha kesish; axis 1: ustunlar (bir xil X), Y bo'yicha
            auto along = [&](const Bubble& b) { return axis == 0 ? b.c.x : b.c.y; };
            auto across = [&](const Bubble& b) { return axis == 0 ? b.c.y : b.c.x; };
            std::vector<const Bubble*> s;
            for (const auto& b : g) s.push_back(&b);
            std::sort(s.begin(), s.end(), [&](auto* a, auto* b) { return across(*a) < across(*b); });
            std::optional<double> cut;
            for (std::size_t i = 0; i < s.size() && !cut;) {
                std::size_t j = i + 1;
                while (j < s.size() && across(*s[j]) - across(*s[i]) < rm * 4) ++j;   // yonma-yon ko'rinishlar qatori biroz siljigan bo'lishi mumkin
                if (j - i >= 6) {
                    std::vector<const Bubble*> line(s.begin() + static_cast<std::ptrdiff_t>(i), s.begin() + static_cast<std::ptrdiff_t>(j));
                    std::sort(line.begin(), line.end(), [&](auto* a, auto* b) { return along(*a) < along(*b); });
                    std::map<std::string, int> cnt;
                    for (auto* b : line) ++cnt[b->name];
                    int dup = 0;
                    for (const auto& [n, c] : cnt) dup += c >= 2;
                    if (dup >= 3) {
                        std::set<std::string> seen;
                        for (std::size_t k = 0; k < line.size(); ++k) {
                            if (!seen.insert(line[k]->name).second && k > 0) {
                                cut = (along(*line[k]) + along(*line[k - 1])) / 2;
                                break;
                            }
                        }
                    }
                }
                i = j;
            }
            if (!cut) continue;
            std::vector<Bubble> a, b;
            for (const auto& x : g) (along(x) < *cut ? a : b).push_back(x);
            if (a.size() < 4 || b.size() < 4) continue;
            splitRec(std::move(a), depth + 1);
            splitRec(std::move(b), depth + 1);
            return;
        }
        groups.push_back(std::move(g));
    };
    for (auto& [g, all] : groups0) splitRec(std::move(all), 0);

    for (auto& all : groups) {
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
                !has(t.u, {"ПЛАН", "СХЕМА", "АРМИРОВАН", "ОПАЛУБ", "РАСПОЛОЖ", "PLAN", "SXEMA", "ARMATURA", "SCHEME", "LAYOUT", "REINFORCEMENT"}) ||
                notTitle(t.u))
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
        const Txt* tt = nullptr;
        for (const auto& c : cands)
            if (c.h >= maxH * 0.9 && c.d < bestD) { bestD = c.d; tt = c.t; }
        if (tt) {
            // ko'p qatorli sarlavha ("Схема расположения монолитной плиты Пм-1" + "(нижнее дополнительное армирование по Х)"):
            // shu shriftdagi, ustma-ust (2.2 qator ichida) va gorizontal yaqin qatorlar qo'shiladi
            std::vector<const Txt*> block{tt};
            for (bool grew = true; grew && block.size() < 4;) {
                grew = false;
                for (const auto& t : sc.texts) {
                    if (std::find(block.begin(), block.end(), &t) != block.end()) continue;
                    if (std::abs(t.h - tt->h) > tt->h * 0.35 || std::abs(t.c.x - tt->c.x) > std::max(w * 0.5, tt->h * 15) || notTitle(t.u)) continue;
                    bool nearOne = false;
                    for (const Txt* b : block) nearOne = nearOne || (std::abs(t.c.y - b->c.y) < tt->h * 2.2 && std::abs(t.c.y - b->c.y) > tt->h * 0.5);
                    if (!nearOne || block.size() >= 4) continue;
                    block.push_back(&t);
                    grew = true;
                }
            }
            std::sort(block.begin(), block.end(), [](const Txt* a, const Txt* b) { return a->c.y > b->c.y; });
            for (const Txt* b : block) v.title += (v.title.empty() ? "" : " ") + trimmed(b->s);
        }
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
    // Hech bir ko'rinishda "1"/"А" yo'q (masalan Ж…Р va 2/1…2/5 o'qlari): ko'p ko'rinishlarda uchraydigan birinchi
    // (eng chapdagi / eng pastdagi) o'qlar kesishmasi umumiy boshlang'ich nuqta
    if (std::none_of(out.begin(), out.end(), [](const KjView& v) { return v.aligned; })) {
        std::map<std::string, int> nx, ny;
        for (const auto& v : out) {
            for (const auto& [n, x] : v.axesX) ++nx[n];
            for (const auto& [n, y] : v.axesY) ++ny[n];
        }
        auto pick = [&](const std::map<std::string, int>& cnt, bool isX) {
            int best = 0;
            for (const auto& [n, c] : cnt) best = std::max(best, c);
            std::string name;
            double bestC = 1e18;
            for (const auto& v : out) {   // eng ko'p uchraydiganlar orasida — o'z ko'rinishida eng chapdagi / pastdagi
                const auto& ax = isX ? v.axesX : v.axesY;
                for (const auto& [n, c] : ax)
                    if (cnt.at(n) >= best && cnt.at(n) >= 2 && c < bestC) { bestC = c; name = n; }
            }
            return name;
        };
        const std::string rx = pick(nx, true), ry = pick(ny, false);
        if (!rx.empty() && !ry.empty())
            for (auto& v : out) {
                const auto ix = v.axesX.find(rx);
                const auto iy = v.axesY.find(ry);
                if (ix == v.axesX.end() || iy == v.axesY.end()) continue;
                const Vec3 o{v.origin.x + ix->second / v.scale, v.origin.y + iy->second / v.scale, 0};
                for (auto& [n, x] : v.axesX) x -= (o.x - v.origin.x) * v.scale;
                for (auto& [n, y] : v.axesY) y -= (o.y - v.origin.y) * v.scale;
                v.origin = o;
                v.aligned = true;
            }
    }
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
    std::string cls = "A400";
    int group = -1;              // joriy plita guruhi: sterjenlar keyin uning boshqa belgilariga ko'chiriladi
    std::vector<int> barGroup;   // r.bars bilan parallel
    std::map<std::tuple<int, int, int, long, std::string>, ScheduleRow> rows;  // kind, level, role, d(mm), cls

    void row(const Bar& b) {
        auto& row = rows[{static_cast<int>(b.kind), b.level, static_cast<int>(b.role), std::lround(b.d * 1000), b.cls}];
        row.kind = b.kind;
        row.level = b.level;
        row.role = b.role;
        row.d = b.d;
        row.cls = b.cls;
        row.count += 1;
        row.totalLength += b.length;
    }
    void bar(ElemKind k, int level, BarRole role, double d, const std::string& c, std::vector<Vec3> pts, double extraLen = 0) {
        double L = 0;
        for (std::size_t i = 1; i < pts.size(); ++i) L += pts[i - 1].distanceTo(pts[i]);
        // savdo uzunligidan uzun sterjenlar — nahlyostka (40d) har ulanishga
        const double stock = 11.7;
        if (L > stock) L += std::floor(L / stock) * 40 * d;
        L += extraLen;
        r.bars.push_back({k, level, role, d, c, std::move(pts), L});
        barGroup.push_back(group);
        row(r.bars.back());
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

bool isFormwork(const std::string& u) { return has(u, {"ОПАЛУБ", "QOLIP", "FORMWORK", "KALIP"}); }
// Izoh/annotatsiya qatlamlari: kontur bo'laklarini ulashda hisobga olinmaydi
bool annoLayer(const std::string& l) {
    const std::string u = utf8::toUpper(l);
    return has(u, {"DIM", "РАЗМЕР", "ANNO", "AXIS", "ОСИ", "ОСЬ", "TEXT", "ТЕКСТ", "ВЫНОС", "LEADER", "HATCH", "ШТРИХ", "_TX"});
}

// Plita markasi sarlavhada: "Пм-3", "ФПм-1", "П-2", "PM-1", "S-1" (katta harfda; yo'q — ""). shown — chizmadagidek
std::string slabTag(const std::string& title, std::string* shown = nullptr) {
    const std::u32string s = utf8::decode(title);
    const std::u32string u = utf8::decode(utf8::toUpper(title));
    if (s.size() != u.size()) return {};
    for (std::size_t i = 0; i < u.size();) {
        if (!isLetter(u[i])) {
            ++i;
            continue;
        }
        std::size_t j = i;
        while (j < u.size() && isLetter(u[j])) ++j;
        const std::size_t len = j - i;
        const bool dash = j < u.size() && (u[j] == U'-' || u[j] == U'–' || u[j] == U'—');
        std::size_t q = j + (dash ? 1 : 0);
        const std::size_t k = q;
        while (q < u.size() && u[q] >= U'0' && u[q] <= U'9') ++q;
        const char32_t c0 = u[i], c1 = len > 1 ? u[i + 1] : 0;
        const bool okL = len <= 4 && (c0 == U'П' || c0 == U'P' || c0 == U'S' || ((c0 == U'Ф' || c0 == U'F') && (c1 == U'П' || c1 == U'P')));
        if (dash && okL && q > k && q - k <= 3) {
            if (q < u.size() && isLetter(u[q])) {   // "Пм-3а" — bitta harf qo'shimchasi
                if (q + 1 < u.size() && isLetter(u[q + 1])) {
                    i = j;
                    continue;
                }
                ++q;
            }
            if (shown) *shown = utf8::encode(s.substr(i, q - i));
            return utf8::encode(u.substr(i, q - i));
        }
        i = j;
    }
    return {};
}

// Sarlavhadagi belgilar: "на отм. +5,300; +9,300; ..." (bir nechta), "на отм. +26,300" / "на отм. -5.150" (bitta)
std::vector<double> titleLevels(const std::string& title, double floorHeight) {
    if (auto ls = parseLevelSet(title); !ls.empty()) return expandLevelSet(ls, floorHeight);
    if (!has(utf8::toUpper(title), {"ОТМ", "OTM", "BELGI", "LEVEL", "KOT", "ELEV"})) return {};
    double v;
    if (parseLevelText(title, v) && std::abs(v) < 200) return {v};
    return {};
}

struct LP {
    std::vector<Vec3> pts;
    bool closed;
    std::string layer;
    double area;
    BBox box;
};
Vec3 toLocal(const KjView& v, const Vec3& q) { return {(q.x - v.origin.x) * v.scale, (q.y - v.origin.y) * v.scale, 0}; }
// Ko'rinishdagi poliliniyalar — mahalliy (o'qlar bo'yicha, haqiqiy o'lchamda)
std::vector<LP> localPolys(const Scene& sc, const KjView& v) {
    std::vector<LP> lps;
    for (const auto& q : sc.polys) {
        if (!inBox(v.box, center(q.box))) continue;
        if (q.box.max.x - q.box.min.x > (v.box.max.x - v.box.min.x) * 1.05) continue;
        LP lp;
        for (const auto& x : q.pts) {
            lp.pts.push_back(toLocal(v, x));
            lp.box.expand(lp.pts.back());
        }
        lp.closed = q.closed;
        lp.layer = q.layer;
        lp.area = q.area * v.scale * v.scale;
        lps.push_back(std::move(lp));
    }
    return lps;
}

struct Outline {
    std::vector<std::vector<Vec3>> rings;   // [0] — kontur (soat strelkasiga qarshi), qolganlari — teshiklar
    double area = 0, net = 0;               // kontur maydoni; teshiklarsiz
    std::string layer;
};
// Kontur — eng katta yopiq (yoki deyarli yopiq) poliliniya. Qolip chizmalarida kontur ko'pincha ochiq poliliniya + uni
// yopuvchi alohida chiziq: bir qatlamdagi ochiq bo'laklar uchma-uch ulanib yopilsa — ular ham nomzod.
// Teshiklar — shu qatlamdagi ichki yopiq konturlar (≥ 1 m²; ustma-ust chizilganlardan — kattasi).
Outline findOutline(const Scene& sc, const KjView& v, const std::vector<LP>& lps) {
    struct Cand {
        std::vector<Vec3> pts;
        std::string layer;
        double area;
    };
    std::vector<Cand> cands;
    const double vw = (v.box.max.x - v.box.min.x) * v.scale, vh = (v.box.max.y - v.box.min.y) * v.scale;
    const double vdiag = std::hypot(vw, vh);
    for (const auto& q : lps) {
        if (q.pts.size() < 4 || rebarLayer(q.layer)) continue;
        const double diag = std::hypot(q.box.max.x - q.box.min.x, q.box.max.y - q.box.min.y);
        if (!q.closed && q.pts.front().distanceTo(q.pts.back()) > diag * 0.02) continue;
        cands.push_back({q.pts, q.layer, q.area});
    }
    struct Piece {
        std::vector<Vec3> pts;
        std::string layer;
        double len;
        bool used = false;
    };
    std::vector<Piece> pieces;
    auto plen = [](const std::vector<Vec3>& pts) {
        double s = 0;
        for (std::size_t i = 1; i < pts.size(); ++i) s += pts[i - 1].distanceTo(pts[i]);
        return s;
    };
    for (const auto& q : lps)
        if (!q.closed && q.pts.size() >= 2 && !rebarLayer(q.layer) && !annoLayer(q.layer)) pieces.push_back({q.pts, q.layer, plen(q.pts)});
    std::vector<std::size_t> seeds;
    for (std::size_t i = 0; i < pieces.size(); ++i)
        if (pieces[i].pts.size() >= 3 && pieces[i].len >= vdiag * 0.3) seeds.push_back(i);
    if (!seeds.empty())
        for (const auto& l : sc.lines) {
            if (rebarLayer(l.layer) || annoLayer(l.layer) || !inBox(v.box, Vec3{(l.a.x + l.b.x) / 2, (l.a.y + l.b.y) / 2, 0})) continue;
            std::vector<Vec3> pts{toLocal(v, l.a), toLocal(v, l.b)};
            const double len = pts[0].distanceTo(pts[1]);
            if (len > 1e-6) pieces.push_back({std::move(pts), l.layer, len});
        }
    std::sort(seeds.begin(), seeds.end(), [&](std::size_t a, std::size_t b) { return pieces[a].len > pieces[b].len; });
    const double tol = std::max(0.005, vdiag * 2e-4);
    for (const std::size_t s : seeds) {
        if (pieces[s].used) continue;
        std::vector<Vec3> ch = pieces[s].pts;
        std::vector<std::size_t> taken{s};
        for (int it = 0; it < 64 && ch.front().distanceTo(ch.back()) > tol; ++it) {
            bool grew = false;
            for (std::size_t j = 0; j < pieces.size() && !grew; ++j) {
                const Piece& q = pieces[j];
                if (q.used || q.layer != pieces[s].layer || std::find(taken.begin(), taken.end(), j) != taken.end()) continue;
                if (q.pts.front().distanceTo(ch.back()) <= tol) ch.insert(ch.end(), q.pts.begin() + 1, q.pts.end());
                else if (q.pts.back().distanceTo(ch.back()) <= tol) ch.insert(ch.end(), q.pts.rbegin() + 1, q.pts.rend());
                else if (q.pts.back().distanceTo(ch.front()) <= tol) ch.insert(ch.begin(), q.pts.begin(), q.pts.end() - 1);
                else if (q.pts.front().distanceTo(ch.front()) <= tol) ch.insert(ch.begin(), q.pts.rbegin(), q.pts.rend() - 1);
                else continue;
                taken.push_back(j);
                grew = true;
            }
            if (!grew) break;
        }
        if (ch.size() < 5 || ch.front().distanceTo(ch.back()) > tol) continue;
        ch.pop_back();
        BBox b;
        for (const auto& x : ch) b.expand(x);
        if (b.max.x - b.min.x > vw * 1.05) continue;
        for (const std::size_t j : taken) pieces[j].used = true;
        const double a = std::abs(shoelace(ch));
        cands.push_back({std::move(ch), pieces[s].layer, a});
    }
    const Cand* best = nullptr;
    for (const auto& c : cands)
        if (!best || c.area > best->area) best = &c;
    Outline o;
    if (!best || best->area < 4) return o;
    o.rings.push_back(best->pts);
    if (shoelace(o.rings[0]) < 0) std::reverse(o.rings[0].begin(), o.rings[0].end());
    o.area = o.net = best->area;
    o.layer = best->layer;
    std::vector<const LP*> hs;
    for (const auto& q : lps) {
        if (!q.closed || q.layer != o.layer || q.area < 1.0 || q.area > best->area * 0.3) continue;
        const Vec3 c = center(q.box);
        if (inPoly(o.rings[0], c.x, c.y)) hs.push_back(&q);
    }
    std::sort(hs.begin(), hs.end(), [](const LP* a, const LP* b) { return a->area > b->area; });
    for (const LP* q : hs) {
        const Vec3 c = center(q->box);
        bool over = false;
        for (std::size_t i = 1; i < o.rings.size() && !over; ++i) over = inPoly(o.rings[i], c.x, c.y);
        if (over) continue;
        auto h = q->pts;
        if (shoelace(h) > 0) std::reverse(h.begin(), h.end());
        o.rings.push_back(std::move(h));
        o.net -= q->area;
    }
    return o;
}

// to'rtburchak (ustun / devor / zona)
struct Rect {
    Vec3 o, u, w;
    double a, b;
    Vec3 c;
};  // o + u*[0..a] + w*[0..b]
bool rectOf(const LP& q, Rect& rc) {
    if (!q.closed || q.pts.size() != 4) return false;
    const Vec3 e1 = q.pts[1] - q.pts[0], e2 = q.pts[2] - q.pts[1];
    const double a = e1.length(), b = e2.length();
    if (a < 1e-6 || b < 1e-6) return false;
    if (std::abs((e1.x * e2.x + e1.y * e2.y) / (a * b)) > 0.02) return false;
    if (q.pts[3].distanceTo(q.pts[0] + e2) > (a + b) * 0.01) return false;
    rc = {q.pts[0], e1 * (1 / a), e2 * (1 / b), a, b, center(q.box)};
    return true;
}

// Vertikal element (ustun / devor) — mahalliy koordinatada
struct Vert {
    std::vector<Vec3> ring;
    bool wall = false;
    std::string layer;
    Vec3 c;
    double area = 0;
};
// Ko'rinishdagi ustun va devorlar: to'rtburchaklar (ustun — kichik tomoni 0.2..1.2, tomonlar nisbati ≤ 4: 400×1350
// "Колонна Км-4" ham ustun; devor — yupqa va cho'ziqroq) va
// murakkab yupqa ko'pburchaklar (L/U shaklli yadro devori, qiya bo'lak). layers — faqat shu qatlamlardagilar
std::vector<Vert> findVerticals(const Scene& sc, const KjView& v, const std::vector<LP>& lps, const std::set<std::string>* layers = nullptr) {
    std::vector<Vec3> exclude;   // "Приямок" yozuvlari: yonidagi kvadratga yaqin element — chuqurcha, ustun emas
    std::vector<Vec3> marks;     // belgi yozuvlari ("-5.150"): ularning ramkasi ustun emas
    for (const auto& t : sc.texts) {
        if (!inBox(v.box, t.c)) continue;
        if (has(t.u, {"ПРИЯМОК", "ОТВЕРСТ", "ПРОЕМ", "ПРОЁМ", "ЛЮК", "CHUQURCHA", "TESHIK"})) exclude.push_back(toLocal(v, t.c));
        double lv;
        if (utf8::length(t.s) <= 12 && parseLevelText(t.s, lv)) marks.push_back(toLocal(v, t.c));
    }
    std::vector<Vert> cand;
    for (const auto& q : lps) {
        if (!q.closed || rebarLayer(q.layer) || (layers && !layers->count(q.layer))) continue;
        Vert e;
        e.layer = q.layer;
        Rect rc;
        if (rectOf(q, rc)) {
            constexpr double eps = 1e-3;   // 1200 × 1800 chizmada 1.2000001 bo'lishi mumkin
            const double a = std::min(rc.a, rc.b), b = std::max(rc.a, rc.b);
            const bool col = a >= 0.2 - eps && a <= 1.2 + eps && b / a <= 4 + eps;
            const bool wall = !col && a >= 0.12 - eps && a <= 0.6 + eps && b / a > 4 && b <= 40 + eps;
            if (!col && !wall) continue;
            e.ring = {rc.o, rc.o + rc.u * rc.a, rc.o + rc.u * rc.a + rc.w * rc.b, rc.o + rc.w * rc.b};
            e.wall = !col;
            e.c = rc.c;
            e.area = rc.a * rc.b;
        } else if (q.pts.size() >= 5) {
            double per = 0;
            for (std::size_t i = 0; i < q.pts.size(); ++i) per += q.pts[i].distanceTo(q.pts[(i + 1) % q.pts.size()]);
            const double th = per > 0 ? 2 * q.area / per : 0;   // yupqa ko'pburchak qalinligi
            if (q.area < 0.1 || q.area > 30 || th < 0.12 || th > 0.8) continue;
            e.ring = q.pts;
            e.wall = true;
            e.c = center(q.box);
            e.area = q.area;
        } else {
            continue;
        }
        if (shoelace(e.ring) < 0) std::reverse(e.ring.begin(), e.ring.end());
        if (std::any_of(marks.begin(), marks.end(), [&](const Vec3& m) { return inPoly(e.ring, m.x, m.y); })) continue;
        bool dup = false;
        for (const auto& o : cand) dup = dup || (o.c.distanceTo(e.c) < 0.05 && std::abs(o.area - e.area) < 0.01);
        if (!dup) cand.push_back(std::move(e));
    }
    std::vector<bool> drop(cand.size(), false);
    for (const auto& x : exclude) {
        int bi = -1;
        double bd = 8;
        for (std::size_t i = 0; i < cand.size(); ++i) {
            if (cand[i].wall) continue;
            const double d = cand[i].c.distanceTo(x);
            if (d < bd) {
                bd = d;
                bi = static_cast<int>(i);
            }
        }
        if (bi >= 0) drop[bi] = true;
    }
    std::vector<Vert> out;
    for (std::size_t i = 0; i < cand.size(); ++i)
        if (!drop[i]) out.push_back(std::move(cand[i]));
    return out;
}

// Ko'rinishdagi armatura yozuvlari
struct Marks {
    std::map<std::string, int> votes;
    std::map<std::string, BarMark> of;
    std::vector<std::pair<Vec3, double>> spacings;   // mahalliy (joy, qadam)
    std::set<long> dias;
    double step = 0;                                 // eng ko'p uchraydigan qadam
    std::string best() const {
        std::string k;
        int bc = 0;
        for (const auto& [key, c] : votes)
            if (c > bc) {
                bc = c;
                k = key;
            }
        return k;
    }
};
Marks viewMarks(const Scene& sc, const KjView& v, double defStep) {
    Marks M;
    struct MT {
        Vec3 c;
        double h;
        BarMark m;
    };
    std::vector<MT> ms;
    struct ST {
        Vec3 c;
        double h, s;
    };
    std::vector<ST> sts;
    for (const auto& t : sc.texts) {
        if (!inBox(v.box, t.c)) continue;
        const double s = spacingOf(t.u);
        if (s > 0) {
            M.spacings.push_back({toLocal(v, t.c), s});
            sts.push_back({t.c, t.h, s});
        }
        BarMark m;
        if (parseBarMark(t.s, m)) {
            if (m.spacing == 0) m.spacing = s;
            ms.push_back({t.c, t.h, m});
        }
    }
    // "Ø14 A500С" va "ш. 200х200" — alohida yozuvlar (yoki multiliderning alohida qatorlari): qadamsiz belgiga yaqin (≈3 qator
    // ichidagi) qadam yozuvi
    for (auto& x : ms) {
        if (x.m.spacing > 0) continue;
        double bd = 1e300;
        for (const auto& st : sts) {
            const double d = st.c.distanceTo(x.c);
            if (d < bd && d <= std::max(x.h, st.h) * 3.5) {
                bd = d;
                x.m.spacing = st.s;
            }
        }
    }
    for (const auto& x : ms) {
        M.dias.insert(std::lround(x.m.d * 1000));
        const std::string k = std::format("{}|{}|{}", std::lround(x.m.d * 1000), x.m.cls, std::lround(x.m.spacing * 1000));
        M.votes[k]++;
        M.of[k] = x.m;
    }
    std::map<double, int> spVotes;
    for (const auto& [q, sp] : M.spacings) spVotes[sp]++;
    M.step = defStep;
    int bc = 0;
    for (auto [s, c] : spVotes)
        if (c > bc) {
            bc = c;
            M.step = s;
        }
    return M;
}

// Plita turi (Пм-1, Пм-3 ...): ko'rinishlari, konturi, belgilari, qalinligi
struct Group {
    std::string tag, shown;
    std::vector<KjView*> views;
    KjView* form = nullptr;    // kontur (qolip chizmasi)
    KjView* meshV = nullptr;   // asosiy to'r
    Outline ol;
    std::vector<double> levels;   // plita usti (nisbiy), o'sish tartibida
    std::vector<int> floors;      // levels bilan parallel
    double t = 0;
    bool tFound = false;
};

}  // namespace

KjProject buildKjProject(const Document& doc, const std::vector<DxfDimInfo>& dims, const KjParams& p, const std::vector<KjView>* given) {
    KjProject r;
    r.views = given ? *given : detectKjViews(doc, dims);
    auto issue = [&](bool err, const std::string& k, std::vector<std::string> a = {}) { r.issues.push_back({err, k, std::move(a)}); };
    auto info = [&](const std::string& k, std::vector<std::string> a = {}) {
        issue(false, k, std::move(a));
        r.issues.back().info = true;
    };
    if (r.views.empty()) {
        issue(true, "kj.chk.no_views");
        return r;
    }
    const Scene sc = collect(doc);
    auto f2 = [](double x) { return std::format("{:.2f}", x); };
    auto f3 = [](double x) { return std::format("{:+.3f}", x); };
    auto isSlab = [](KjViewKind k) { return k == KjViewKind::SlabMain || k == KjViewKind::SlabExtraBottom || k == KjViewKind::SlabExtraTop; };
    auto isFound = [](KjViewKind k) {
        return k == KjViewKind::FoundMain || k == KjViewKind::FoundExtraBottom || k == KjViewKind::FoundExtraTop || k == KjViewKind::FoundVertical ||
               k == KjViewKind::FoundSupports || k == KjViewKind::Outlets;
    };
    // ko'rinish hududi: o'zi va pastidagi kesimlar ("Фрагмент монолитной плиты": usti / osti belgilari)
    auto region = [](const KjView& v, const Vec3& q) {
        const double w = v.box.max.x - v.box.min.x, h = v.box.max.y - v.box.min.y;
        return q.x >= v.box.min.x - w * 0.15 && q.x <= v.box.max.x + w * 0.15 && q.y >= v.box.min.y - h * 0.6 && q.y <= v.box.max.y + h * 0.15;
    };
    struct LvT {
        Vec3 c;
        double v;
    };
    std::vector<LvT> lvTexts;   // belgi yozuvlari ("-0.700", "+5.100")
    for (const auto& t : sc.texts) {
        double l;
        if (utf8::length(t.s) <= 24 && parseLevelText(t.s, l) && std::abs(l) < 200) lvTexts.push_back({t.c, l});
    }

    // ---- plita turlari: sarlavhadagi markasi bo'yicha ("Пм-1", "Пм-3"); markasiz ko'rinish — eng yaqin guruhga
    std::vector<Group> groups;
    {
        std::map<std::string, std::size_t> idx;
        std::vector<KjView*> untagged;
        for (auto& v : r.views) {
            if (!v.use || !isSlab(v.kind)) continue;
            std::string shown;
            const std::string tag = slabTag(v.title, &shown);
            if (tag.empty()) {
                untagged.push_back(&v);
                continue;
            }
            const auto [it, ins] = idx.try_emplace(tag, groups.size());
            if (ins) {
                groups.emplace_back();
                groups.back().tag = tag;
                groups.back().shown = shown;
            }
            groups[it->second].views.push_back(&v);
        }
        if (groups.empty() && !untagged.empty()) groups.emplace_back();
        for (KjView* v : untagged) {
            std::size_t bi = 0;
            double bd = 1e300;
            for (std::size_t g = 0; g < groups.size(); ++g)
                for (const KjView* o : groups[g].views)
                    if (const double d = center(o->box).distanceTo(center(v->box)); d < bd) {
                        bd = d;
                        bi = g;
                    }
            groups[bi].views.push_back(v);
        }
        for (auto& g : groups) {
            for (KjView* v : g.views)
                if (v->kind == KjViewKind::SlabMain && isFormwork(utf8::toUpper(v->title))) {
                    g.form = v;
                    break;
                }
            // asosiy to'r — yozuvlari ko'p bo'lgan ko'rinish (teng bo'lsa — qolip chizmasi emasi)
            int bv = -1;
            for (KjView* v : g.views) {
                if (v->kind != KjViewKind::SlabMain) continue;
                int n = 0;
                for (const auto& [k, c] : viewMarks(sc, *v, p.meshStep).votes) n = std::max(n, c);
                if (n > bv || (n == bv && g.meshV == g.form && v != g.form)) {
                    bv = n;
                    g.meshV = v;
                }
            }
            if (!g.form) g.form = g.meshV;
            if (g.form) g.ol = findOutline(sc, *g.form, localPolys(sc, *g.form));
            if (g.ol.rings.empty() && g.meshV && g.meshV != g.form) g.ol = findOutline(sc, *g.meshV, localPolys(sc, *g.meshV));
        }
    }
    const bool multi = groups.size() > 1;

    // ---- plita belgilari
    double slabTop = p.slabTop, slabT = p.slabT;
    if (!multi) {
        // bitta plita: (ust, ost) belgilar jufti va tipik qavatlar — plita/ustun ko'rinishlari atrofidagi yozuvlardan
        std::map<long, int> lvCount;
        std::map<std::vector<long>, int> typVotes;   // tipik qavatlar to'plamlari (oraliq belgisi + belgilar, mm)
        for (const auto& t : sc.texts) {
            bool near = false;
            for (const auto& v : r.views) {
                if (!(isSlab(v.kind) || v.kind == KjViewKind::Columns)) continue;
                const double w = v.box.max.x - v.box.min.x, h = v.box.max.y - v.box.min.y;
                near = near || (t.c.x > v.box.min.x - w * 0.5 && t.c.x < v.box.max.x + w * 0.5 && t.c.y > v.box.min.y - h * 0.5 &&
                                t.c.y < v.box.max.y + h * 0.5);
            }
            double l;
            if (near && parseLevelText(t.s, l) && std::abs(l) < 200) lvCount[std::lround(l * 1000)]++;
            if (near)
                if (auto ls = parseLevelSet(t.s); !ls.empty()) {
                    std::vector<long> key{ls.range ? 1L : 0L};
                    for (double x : ls.levels) key.push_back(std::lround(x * 1000));
                    ++typVotes[key];
                }
        }
        // ko'rinish sarlavhalari ham ("Опалубочный план плиты перекрытия на отм. +11.300...+23.400")
        for (const auto& v : r.views)
            if (isSlab(v.kind) || v.kind == KjViewKind::Columns)
                if (auto ls = parseLevelSet(v.title); !ls.empty()) {
                    std::vector<long> key{ls.range ? 1L : 0L};
                    for (double x : ls.levels) key.push_back(std::lround(x * 1000));
                    typVotes[key] += 2;
                }
        {
            int best = 0;
            LevelSet bestSet;
            for (const auto& [key, n] : typVotes) {
                if (n < best || (n == best && key.size() <= bestSet.levels.size() + 1)) continue;
                best = n;
                bestSet.range = key[0] == 1;
                bestSet.levels.clear();
                for (std::size_t i = 1; i < key.size(); ++i) bestSet.levels.push_back(key[i] / 1000.0);
            }
            if (!bestSet.empty()) r.typical = expandLevelSet(bestSet, p.floorHeight);
            if (r.typical.size() < 2) r.typical.clear();
        }
        // plita: (ust, ost) belgilar jufti, farqi 0.12..0.45 — ikkalasi ham ko'p uchraydigan ("+3.050" / "+2.850")
        if (slabTop <= 0 && !r.typical.empty() && r.typical.front() > 0) slabTop = r.typical.front();   // birinchi tipik qavat
        if (slabTop <= 0 || slabT <= 0) {
            int bestScore = 0;
            double bt = 0, bb = 0;
            for (auto [kt, ct] : lvCount) {
                if (slabTop <= 0 && (kt < 1000 || kt > 12000)) continue;
                if (slabTop > 0 && std::abs(kt - std::lround(slabTop * 1000)) > 1) continue;
                for (auto [kb, cb] : lvCount) {
                    const long d = kt - kb;
                    if (d < 120 || d > 450) continue;
                    const int s = std::min(ct, cb);
                    if (s > bestScore || (s == bestScore && kt > std::lround(bt * 1000))) {
                        bestScore = s;
                        bt = kt / 1000.0;
                        bb = kb / 1000.0;
                    }
                }
            }
            if (slabTop <= 0) slabTop = bestScore > 0 ? bt : 0;
            if (slabT <= 0 && bestScore > 0) slabT = bt - bb;
            if (slabTop <= 0) {
                int bc = 0;
                for (auto [k, c] : lvCount)
                    if (k > 1000 && k < 12000 && c > bc) {
                        bc = c;
                        slabTop = k / 1000.0;
                    }
            }
            if (slabTop <= 0) {
                slabTop = 3.0;
                issue(false, "kj.chk.assumed_level", {"3.000"});
            }
            if (slabT <= 0) slabT = 0.2;
        }
        if (!(r.typical.size() >= 2 && std::abs(slabTop - r.typical.front()) < 0.005)) r.typical.clear();
        if (groups.empty()) groups.emplace_back();   // plita ko'rinishi yo'q — ustunlar balandligi uchun belgi
        groups[0].levels = r.typical.empty() ? std::vector<double>{slabTop} : r.typical;
        groups[0].t = slabT;
        groups[0].tFound = true;
    } else {
        // har plita turi: sarlavhadagi belgi(lar) ("Пм-3 на отм. +5,300; +9,300 ..."), aks holda qolip chizmasida kontur ichidagi va
        // kesimdagi eng ko'p belgi
        for (auto& g : groups) {
            std::vector<const KjView*> order;
            if (g.form) order.push_back(g.form);
            for (const KjView* v : g.views)
                if (v != g.form) order.push_back(v);
            for (const KjView* v : order)
                if (auto lv = titleLevels(v->title, p.floorHeight); !lv.empty()) {
                    g.levels = lv;
                    break;
                }
            if (!g.levels.empty() || !g.form) continue;
            std::map<long, int> votes;
            const KjView& fv = *g.form;
            for (const auto& lt : lvTexts) {
                if (inBox(fv.box, lt.c)) {
                    const Vec3 q = toLocal(fv, lt.c);
                    if (!g.ol.rings.empty() && inPoly(g.ol.rings[0], q.x, q.y)) votes[std::lround(lt.v * 1000)] += 2;
                } else if (region(fv, lt.c) && lt.c.y < fv.box.min.y) {
                    votes[std::lround(lt.v * 1000)] += 1;
                }
            }
            int bc = 0;
            for (auto [k, c] : votes)
                if (c > bc) {
                    bc = c;
                    g.levels = {k / 1000.0};
                }
        }
        double hi = -1e300;
        for (const auto& g : groups)
            for (double l : g.levels) hi = std::max(hi, l);
        for (auto& g : groups)
            if (g.levels.empty()) {
                g.levels = {hi > -1e299 ? hi + p.floorHeight : 3.0};
                issue(false, "kj.chk.slab_no_level", {g.shown, f3(g.levels[0])});
            }
        // qalinligi: plita usti belgisidan 0.12..0.45 pastdagi belgi (kesimdagi "osti") — guruh ko'rinishlari hududida
        for (auto& g : groups) {
            if (p.slabT > 0) {
                g.t = p.slabT;
                g.tFound = true;
                continue;
            }
            std::map<long, int> tv;
            for (const auto& lt : lvTexts) {
                bool in = false;
                for (const KjView* v : g.views) in = in || region(*v, lt.c);
                if (!in) continue;
                for (double top : g.levels)
                    if (const long d = std::lround((top - lt.v) * 1000); d >= 120 && d <= 450) tv[d]++;
            }
            int bc = 0;
            for (auto [d, c] : tv)
                if (c > bc) {
                    bc = c;
                    g.t = d / 1000.0;
                }
            g.tFound = bc > 0;
        }
    }

    // ---- qavatlar: barcha plitalar usti belgilari; 1.5 m dan yaqinlari — bitta qavat (Пм-1 −0.700 va Пм-2 −0.150)
    std::vector<std::pair<double, double>> floorLH;
    {
        std::vector<double> all;
        for (const auto& g : groups) all.insert(all.end(), g.levels.begin(), g.levels.end());
        std::sort(all.begin(), all.end());
        for (double l : all) {
            if (!floorLH.empty() && l - floorLH.back().second < 1.5) floorLH.back().second = l;
            else floorLH.push_back({l, l});
        }
    }
    auto floorOf = [&](double l) {
        for (std::size_t i = 0; i < floorLH.size(); ++i)
            if (l >= floorLH[i].first - 1e-6 && l <= floorLH[i].second + 1e-6) return static_cast<int>(i) + 1;
        return 1;
    };
    const int F = std::max(1, static_cast<int>(floorLH.size()));
    for (auto& g : groups) {
        g.floors.clear();
        for (double l : g.levels) g.floors.push_back(floorOf(l));
    }
    if (multi)   // qalinligi topilmagan plita: shu qavatdagi boshqa plitaniki, aks holda eng ko'p uchraydigani
        for (auto& g : groups) {
            if (g.tFound) continue;
            const Group* same = nullptr;
            for (const auto& o : groups)
                if (&o != &g && o.tFound && o.floors.front() == g.floors.front()) {
                    same = &o;
                    break;
                }
            if (same) {
                g.t = same->t;
                issue(false, "kj.chk.slab_t_same", {g.shown, same->shown, f2(g.t)});
            } else {
                std::map<long, int> c;
                for (const auto& o : groups)
                    if (o.tFound) c[std::lround(o.t * 1000)]++;
                g.t = 0.2;
                int bc = 0;
                for (auto [k, n] : c)
                    if (n > bc) {
                        bc = n;
                        g.t = k / 1000.0;
                    }
                issue(false, "kj.chk.slab_t_assumed", {g.shown, f2(g.t)});
            }
        }

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
            if (c > bc) {
                bc = c;
                concrete = k;
            }
        if (concrete.empty()) concrete = "B25";
    }
    r.concrete = concrete;
    // ±0.000 absolyut belgisi va u nimaga tengligi (fundament usti / toza pol)
    const auto dat = findDatum(doc);
    if (dat) {
        r.datumText = dat->text;
        r.datumFromDrawing = true;
    }
    r.datum = p.datumAuto && dat ? dat->abs : p.datum;
    r.datumKnown = (p.datumAuto && dat) || !p.datumAuto || p.datum != 0;
    if (dat && !p.datumAuto && std::abs(dat->abs - p.datum) > 0.005)
        issue(false, "kj.chk.datum_differs", {std::format("{:.3f}", p.datum), std::format("{:.3f}", dat->abs)});
    if (!r.datumKnown) issue(false, "kj.chk.no_datum");  // bino: keyin ALIGN/MOVE bilan joylashtirish mumkin

    // ---- fundament usti va qalinligi: izohdan (±0.000 = fundament usti), ustunlar / vypusklar sxemasi sarlavhasidan
    // ("Схема расположения вертикальных элементов на отм. -5.150") yoki fundament ko'rinishlaridagi (usti, osti) juftidan
    const bool fTopFromDatum = p.foundTopAuto && dat && dat->ref == 1;
    double fTop = p.foundTop, foundT = p.foundT;
    std::optional<double> topDrawing, botDrawing;
    std::map<long, int> fc;   // fundament ko'rinishlari hududidagi belgilar
    for (const auto& lt : lvTexts)
        for (const auto& v : r.views)
            if (v.use && isFound(v.kind) && region(v, lt.c)) {
                fc[std::lround(lt.v * 1000)]++;
                break;
            }
    auto bottomFor = [&](long top) -> std::optional<double> {
        int bc = 0;
        long bb = 0;
        for (auto [k, c] : fc)
            if (const long d = top - k; d >= 250 && d <= 3000 && c > bc) {
                bc = c;
                bb = k;
            }
        if (bc == 0) return std::nullopt;
        return bb / 1000.0;
    };
    if (p.foundTopAuto && !fTopFromDatum) {
        for (const auto& v : r.views)
            if (v.use && (v.kind == KjViewKind::Columns || v.kind == KjViewKind::Outlets))
                if (auto lv = titleLevels(v.title, p.floorHeight); lv.size() == 1) {
                    topDrawing = lv[0];
                    break;
                }
        if (!topDrawing) {
            int bs = 0;
            long bt = 0;
            for (auto [kt, ct] : fc)
                for (auto [kb, cb] : fc) {
                    const long d = kt - kb;
                    if (d < 250 || d > 3000) continue;
                    const int s = std::min(ct, cb);
                    if (s > bs || (s == bs && kt > bt)) {
                        bs = s;
                        bt = kt;
                    }
                }
            if (bs > 0) topDrawing = bt / 1000.0;
        }
    }
    if (fTopFromDatum) {
        fTop = 0;
        botDrawing = bottomFor(0);
    } else if (topDrawing) {
        fTop = *topDrawing;
        botDrawing = bottomFor(std::lround(fTop * 1000));
    }
    if (botDrawing) foundT = fTop - *botDrawing;
    const double fBot = fTop - foundT;
    r.foundTop = fTop;
    r.foundT = foundT;
    if (botDrawing) info("kj.chk.found_drawing", {f3(fTop), f3(fBot), f2(foundT)});
    else if (fTopFromDatum) issue(false, "kj.chk.found_t_assumed", {f2(p.foundT)});
    else if (topDrawing) issue(false, "kj.chk.found_top_drawing", {f3(fTop), f2(foundT)});
    else issue(false, "kj.chk.found_assumed", {std::format("{:.3f}", fTop), f2(foundT)});

    // fundament: qolip chizmasi — kontur, yozuvlari ko'p ko'rinish — asosiy to'r
    KjView *foundForm = nullptr, *foundMesh = nullptr;
    {
        int bv = -1;
        for (auto& v : r.views) {
            if (!v.use || v.kind != KjViewKind::FoundMain) continue;
            if (!foundForm && isFormwork(utf8::toUpper(v.title))) foundForm = &v;
            int n = 0;
            for (const auto& [k, c] : viewMarks(sc, v, p.meshStep).votes) n = std::max(n, c);
            if (n > bv || (n == bv && foundMesh == foundForm && &v != foundForm)) {
                bv = n;
                foundMesh = &v;
            }
        }
        if (!foundForm) foundForm = foundMesh;
    }
    Outline foundOl;
    if (foundForm) foundOl = findOutline(sc, *foundForm, localPolys(sc, *foundForm));
    if (foundOl.rings.empty() && foundMesh && foundMesh != foundForm) foundOl = findOutline(sc, *foundMesh, localPolys(sc, *foundMesh));

    Ctx cx{p, r, "A400", -1, {}, {}};

    // ---- har ko'rinish
    std::map<const KjView*, int> gOf;
    for (std::size_t g = 0; g < groups.size(); ++g)
        for (const KjView* v : groups[g].views) gOf[v] = static_cast<int>(g);
    struct ColView {
        const KjView* v;
        std::vector<Vert> verts;
    };
    std::vector<ColView> colViews;
    double stirS = p.stirS, stirSEnd = p.stirSEnd;   // ustunlar sxemasi atrofidagi "100/200" dan
    struct Dots {
        double d;
        std::vector<Vec3> pts;
        KjViewKind kind;
        int group;
    };
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
        auto L = [&](const Vec3& q) { return toLocal(v, q); };
        const auto it = gOf.find(&v);
        Group* G = it != gOf.end() ? &groups[static_cast<std::size_t>(it->second)] : nullptr;
        cx.group = it != gOf.end() ? it->second : -1;
        const Marks M = viewMarks(sc, v, p.meshStep);
        const auto lps = localPolys(sc, v);
        auto nearestSpacing = [&](const Vec3& at, double def) {
            double bd = 1e18, s = def;
            for (const auto& [q, sp] : M.spacings)
                if (double d = q.distanceTo(at); d < bd) {
                    bd = d;
                    s = sp;
                }
            return bd < 12 ? s : def;
        };
        auto mainMesh = [&](const std::vector<std::vector<Vec3>>& rings, ElemKind ek, int level, double z0, double z1) {
            const std::string bestK = M.best();
            BarMark m;
            if (!bestK.empty()) m = M.of.at(bestK);
            else issue(false, "kj.chk.no_mark", {v.title});
            const double d = m.d > 0 ? m.d : 0.012;
            const std::string cls = m.cls.empty() ? "A400" : m.cls;
            const double s = m.spacing > 0 ? m.spacing : M.step;
            v.mainMark = std::format("\xC3\x98{} {} s{}", std::lround(d * 1000), cls, std::lround(s * 1000));
            mesh(cx, rings, ek, level, d, cls, s, z0, z1);
            if (!m.cls.empty()) cx.cls = m.cls;
        };
        if (v.kind == KjViewKind::FoundMain) {
            if (&v == foundForm) {
                v.outlineArea = foundOl.area;
                if (foundOl.rings.empty()) issue(true, "kj.chk.no_outline", {v.title});
            }
            if (&v == foundMesh && !foundOl.rings.empty()) mainMesh(foundOl.rings, ElemKind::FoundationSlab, 0, fBot, fTop);
        }
        if (v.kind == KjViewKind::SlabMain && G) {
            if (&v == G->form) {
                v.outlineArea = G->ol.area;
                if (G->ol.rings.empty()) issue(true, "kj.chk.no_outline", {v.title});
            }
            if (&v == G->meshV && !G->ol.rings.empty())
                mainMesh(G->ol.rings, ElemKind::Slab, G->floors.front(), G->levels.front() - G->t, G->levels.front());
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
                if (a >= 50 && b > a && b <= 400) {
                    stirSEnd = a / 1000.0;
                    stirS = b / 1000.0;
                }
            }
            ColView cv{&v, findVerticals(sc, v, lps)};
            for (const auto& e : cv.verts) ++(e.wall ? v.walls : v.columns);
            colViews.push_back(std::move(cv));
        }
        // ---- armatura qatlamlari
        const bool found = isFound(v.kind) && v.kind != KjViewKind::FoundSupports;
        const bool slab = isSlab(v.kind);
        if (!found && !slab) continue;
        const int lvl = found ? 0 : (G ? G->floors.front() : 1);
        const double z1 = found ? fTop : (G ? G->levels.front() : slabTop), z0 = found ? fBot : z1 - (G ? G->t : slabT);
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
                g.group = cx.group;
                g.pts.push_back(L(c.c));
            }
            for (auto& [k, g] : byD) {
                v.dots += static_cast<int>(g.pts.size());
                if (!M.dias.empty() && !M.dias.count(k)) issue(false, "kj.chk.dia_label", {v.title, std::to_string(k)});
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
                    if (z.a * z.b < ba) {
                        ba = z.a * z.b;
                        bi = static_cast<int>(i);
                    }
                }
                const BarRole role = (std::abs(dir.x) >= std::abs(dir.y)) ? (top ? BarRole::TopX : BarRole::BottomX)
                                                                           : (top ? BarRole::TopY : BarRole::BottomY);
                const double zz = top ? z1 - p.cover - d / 2 - (role == BarRole::TopY ? d : 0) : z0 + p.cover + d / 2 + (role == BarRole::BottomY ? d : 0);
                std::string zcls = cx.cls;
                for (const auto& [k, m] : M.of)
                    if (std::lround(m.d * 1000) == std::lround(d * 1000) && !m.cls.empty()) zcls = m.cls;
                if (bi < 0) {
                    cx.bar(ek, lvl, role, d, zcls, {Vec3{a.x, a.y, zz}, Vec3{b.x, b.y, zz}});
                    continue;
                }
                const auto& z = zones[static_cast<std::size_t>(bi)];
                const bool alongU = std::abs(dir.x * z.u.x + dir.y * z.u.y) > 0.5;
                if (!done.insert({bi, alongU ? 0 : 1, std::lround(d * 1000)}).second) continue;
                ++v.zones;
                const Vec3 U = alongU ? z.u : z.w, W = alongU ? z.w : z.u;
                const double lu = alongU ? z.a : z.b, lw = alongU ? z.b : z.a;
                const double s = nearestSpacing(z.c, M.step);
                const int n = std::max(1, static_cast<int>(std::lround(lw / s)));
                for (int i = 0; i < n; ++i) {
                    const Vec3 o = z.o + W * ((i + 0.5) * lw / n);
                    const Vec3 e = o + U * lu;
                    cx.bar(ek, lvl, role, d, zcls, {Vec3{o.x, o.y, zz}, Vec3{e.x, e.y, zz}});
                }
            }
        }
    }
    cx.group = -1;
    const auto& foundRings = foundOl.rings;

    // ---- qavatlar bo'yicha ustun va devorlar
    // 1-qavat asosi: fundament usti; tipik qavatli (fundamentsiz) KJ da — birinchi tipik plitadan bir qavat pastda
    const double typStep = r.typical.size() >= 2 ? (r.typical.back() - r.typical.front()) / static_cast<double>(r.typical.size() - 1) : 0;
    double base1 = fTop;
    if (!multi && typStep > 0) base1 = std::max(foundRings.empty() ? -1e9 : fTop, slabTop - typStep);
    if (multi && foundRings.empty()) base1 = floorLH.empty() ? fTop : floorLH.front().first - p.floorHeight;
    struct GL {
        const Group* g;
        double L;
    };
    auto groupsAt = [&](int f) {
        std::vector<GL> out;
        for (const auto& g : groups)
            for (std::size_t i = 0; i < g.levels.size(); ++i)
                if (g.floors[i] == f) out.push_back({&g, g.levels[i]});
        return out;
    };
    auto covers = [](const Group& g, const Vec3& c) { return g.ol.rings.empty() || inPoly(g.ol.rings[0], c.x, c.y); };
    // element ustidagi plita: (plita osti, plita usti) — qoplaydiganlarning eng balandi
    auto topAt = [&](int f, const Vec3& c) {
        std::pair<double, double> best{-1e300, 0}, any{-1e300, 0};
        for (const auto& [g, L] : groupsAt(f)) {
            const double b = g->ol.rings.empty() ? L : L - g->t;
            if (b > any.first) any = {b, L};
            if (covers(*g, c) && b > best.first) best = {b, L};
        }
        return best.first > -1e299 ? best : any;
    };
    auto baseAt = [&](int f, const Vec3& c) {
        if (f <= 1) return base1;
        double best = -1e300, any = -1e300;
        for (const auto& [g, L] : groupsAt(f - 1)) {
            any = std::max(any, L);
            if (covers(*g, c)) best = std::max(best, L);
        }
        return best > -1e299 ? best : any;
    };
    std::vector<std::vector<Vert>> fv(static_cast<std::size_t>(F) + 1);
    auto addVert = [](std::vector<Vert>& to, const Vert& e) {
        for (const auto& o : to)
            if (o.c.distanceTo(e.c) < 0.05 && std::abs(o.area - e.area) < 0.01) return;
        to.push_back(e);
    };
    std::set<std::string> vLayers;   // ustunlar sxemasidagi vertikal elementlar qatlamlari
    for (const auto& cv : colViews) {
        int f = 1;   // sxema belgisi ("на отм. +5.300") qaysi qavat plitasi ustiga to'g'ri kelsa — keyingi qavat
        if (auto lv = titleLevels(cv.v->title, p.floorHeight); lv.size() == 1 && std::abs(lv[0] - base1) > 0.05)
            for (int k = 1; k < F; ++k)
                for (const auto& [g, L] : groupsAt(k))
                    if (std::abs(L - lv[0]) < 0.05) f = k + 1;
        for (const auto& e : cv.verts) {
            addVert(fv[static_cast<std::size_t>(f)], e);
            vLayers.insert(e.layer);
        }
    }
    std::vector<int> fromForm, copied;
    int outside = 0;
    for (int f = 2; f <= F; ++f) {
        auto& cur = fv[static_cast<std::size_t>(f)];
        if (!cur.empty()) continue;
        // bir nechta plita turi: shu qavat plitasining qolip chizmasidagi (ustunlar sxemasidagi qatlamlarda) elementlar; yo'q bo'lsa —
        // pastki qavatdagidek. Yuqori qavatlarda plita konturidan tashqaridagilar (pastki qavat ayvoni ustunlari) davom etmaydi.
        std::vector<Vert> src;
        if (multi && !vLayers.empty()) {
            std::set<const KjView*> seen;
            for (const auto& [g, L] : groupsAt(f))
                if (g->form && seen.insert(g->form).second)
                    for (const auto& e : findVerticals(sc, *g->form, localPolys(sc, *g->form), &vLayers)) addVert(src, e);
        }
        const bool own = !src.empty();
        if (!own) src = fv[static_cast<std::size_t>(f - 1)];
        bool rings = false;
        for (const auto& [g, L] : groupsAt(f)) rings = rings || !g->ol.rings.empty();
        int out = 0;
        for (const auto& e : src) {
            bool in = !rings;
            for (const auto& [g, L] : groupsAt(f)) in = in || (!g->ol.rings.empty() && inPoly(g->ol.rings[0], e.c.x, e.c.y));
            if (in) cur.push_back(e);
            else ++out;
        }
        outside = std::max(outside, out);
        if (!cur.empty()) (own ? fromForm : copied).push_back(f);
    }
    // ---- ustun karkaslari (vypusklar ustun ichida bo'lsa — asosiy sterjenlar shu joyda, ust plitaga kiradi)
    int caged = 0, nColumns = 0;
    for (int f = 1; f <= F; ++f)
        for (const auto& e : fv[static_cast<std::size_t>(f)]) {
            if (e.wall) continue;
            ++nColumns;
            if (!p.columnCages) continue;
            std::vector<std::pair<Vec3, double>> in;
            for (const auto& g : dots)
                if (g.kind == KjViewKind::Outlets)
                    for (const auto& q : g.pts)
                        if (inPoly(e.ring, q.x, q.y)) in.push_back({q, g.d});
            if (in.empty()) continue;
            ++caged;
            const double colBase = baseAt(f, e.c);
            const auto [colTop0, slabL] = topAt(f, e.c);
            bool rings = false;
            for (const auto& [g, L] : groupsAt(f)) rings = rings || !g->ol.rings.empty();
            const double zTop = rings ? slabL - p.cover : colTop0;
            for (const auto& [q, d] : in) cx.bar(ElemKind::Column, f, BarRole::Main, d, cx.cls, {Vec3{q.x, q.y, colBase}, Vec3{q.x, q.y, zTop}});
            BBox b;
            for (const auto& q : e.ring) b.expand(q);
            const double inset = p.cover + p.stirD / 2;
            if (b.max.x - b.min.x < 2 * inset + 0.05 || b.max.y - b.min.y < 2 * inset + 0.05) continue;
            const double H = colTop0 - colBase, endZ = std::max(0.5, H / 6);
            std::vector<double> zs;
            for (double z = 0.05; z < H - 0.04; z += (z < endZ || z > H - endZ) ? stirSEnd : stirS) zs.push_back(colBase + z);
            for (double z : zs)
                cx.bar(ElemKind::Column, f, BarRole::Stirrup, p.stirD, "A240",
                       {Vec3{b.min.x + inset, b.min.y + inset, z}, Vec3{b.max.x - inset, b.min.y + inset, z},
                        Vec3{b.max.x - inset, b.max.y - inset, z}, Vec3{b.min.x + inset, b.max.y - inset, z},
                        Vec3{b.min.x + inset, b.min.y + inset, z}},
                       0.2);  // ilgaklar
        }
    if (p.columnCages && caged == 0 && nColumns > 0) issue(false, "kj.chk.no_cages");

    // ---- vertikal sterjenlar
    for (const auto& g : dots) {
        cx.group = g.group;
        const Group* G = g.group >= 0 ? &groups[static_cast<std::size_t>(g.group)] : nullptr;
        for (const auto& q : g.pts) {
            switch (g.kind) {
                case KjViewKind::Outlets:
                    cx.bar(ElemKind::Column, 0, BarRole::Main, g.d, cx.cls, {Vec3{q.x, q.y, fTop - p.outletIn}, Vec3{q.x, q.y, fTop + p.outletOut}});
                    break;
                case KjViewKind::FoundVertical:
                    cx.bar(ElemKind::FoundationSlab, 0, BarRole::Vertical, g.d, cx.cls,
                           {Vec3{q.x, q.y, fBot + p.cover}, Vec3{q.x, q.y, fTop - p.cover}});
                    break;
                default: {
                    const double top = G ? G->levels.front() : slabTop, t = G ? G->t : slabT;
                    cx.bar(ElemKind::Slab, G ? G->floors.front() : 1, BarRole::Vertical, g.d, cx.cls,
                           {Vec3{q.x, q.y, top - t + p.cover}, Vec3{q.x, q.y, top - p.cover}});
                    break;
                }
            }
        }
    }
    cx.group = -1;

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
    for (int wall = 0; wall < 2; ++wall)
        for (int f = 1; f <= F; ++f)
            for (const auto& e : fv[static_cast<std::size_t>(f)]) {
                if (e.wall != (wall == 1)) continue;
                const double z0 = baseAt(f, e.c), z1 = topAt(f, e.c).first;
                if (z1 - z0 > 0.05) prism(e.wall ? ElemKind::Wall : ElemKind::Column, f, {e.ring}, z0, z1, concrete);
            }
    double heightTop = -1e300;
    for (const auto& g : groups)
        for (std::size_t i = 0; i < g.levels.size(); ++i) {
            heightTop = std::max(heightTop, g.levels[i]);
            if (!g.ol.rings.empty()) prism(ElemKind::Slab, g.floors[i], g.ol.rings, g.levels[i] - g.t, g.levels[i], concrete);
        }
    r.bm.heightTop = heightTop > -1e299 ? heightTop : slabTop;
    // ---- plita turining armaturasi (birinchi belgisida qurilgan) — uning boshqa belgilariga ("Пм-3": +9.300 ... +21.300)
    {
        const std::size_t nb = r.bars.size();
        for (std::size_t gi = 0; gi < groups.size(); ++gi) {
            const auto& g = groups[gi];
            for (std::size_t i = 1; i < g.levels.size(); ++i) {
                const double dz = g.levels[i] - g.levels.front();
                for (std::size_t j = 0; j < nb; ++j) {
                    if (cx.barGroup[j] != static_cast<int>(gi)) continue;
                    Bar b = r.bars[j];
                    b.level = g.floors[i];
                    for (auto& q : b.pts) q.z += dz;
                    cx.row(b);
                    r.bars.push_back(std::move(b));
                    cx.barGroup.push_back(-1);
                }
            }
        }
    }
    // qo'llanilgan plita belgisi (1-qavatning eng katta plitasi)
    {
        const Group* m = nullptr;
        for (const auto& g : groups)
            if (!g.levels.empty() && g.floors.front() == 1 && (!m || g.ol.area > m->ol.area)) m = &g;
        if (m) {
            slabTop = m->levels.front();
            slabT = m->t;
        }
    }
    r.slabTop = slabTop;
    r.slabT = slabT;
    if (!multi && typStep > 0) {
        issue(false, "kj.chk.typical", {std::format("{:+.3f}", r.typical.front()), std::format("{:+.3f}", r.typical.back()),
                                        std::to_string(r.typical.size()), std::format("{:.3f}", typStep)});
        r.issues.back().info = true;
    }
    if (multi) {
        // tipik: eng ko'p belgili plita turi (PTO qatorlarida qavat nomi; GUI)
        for (const auto& g : groups)
            if (g.levels.size() >= 2 && g.levels.size() > r.typical.size()) r.typical = g.levels;
        if (!fromForm.empty() || !copied.empty() || outside > 0) {
            auto list = [](const std::vector<int>& v) {
                std::string s;
                for (int f : v) s += (s.empty() ? "" : ", ") + std::to_string(f);
                return s;
            };
            if (!fromForm.empty()) info("kj.chk.vert_form", {list(fromForm)});
            if (!copied.empty()) info("kj.chk.vert_copied", {list(copied)});
            if (outside > 0) info("kj.chk.vert_outside", {std::to_string(outside)});
        }
    }
    for (const auto& g : groups) {
        if (g.views.empty()) continue;
        KjSlab s;
        s.tag = g.shown;
        s.levels = g.levels;
        s.floors = g.floors;
        s.t = g.t;
        s.area = g.ol.net;
        s.mark = g.meshV ? g.meshV->mainMark : std::string();
        r.slabs.push_back(std::move(s));
    }
    computePto(r.bm);
    // ---- absolyut belgilar va geodezik bog'lash (PTO hajmlari o'zgarmaydi)
    {
        r.georef = p.originX != 0 || p.originY != 0 || p.rotDeg != 0;
        const double a = p.rotDeg * 3.14159265358979323846 / 180, ca = std::cos(a), sa = std::sin(a);
        auto xf = [&](Vec3& q) {
            const double x = q.x, y = q.y;
            q.x = p.originX + x * ca - y * sa;
            q.y = p.originY + x * sa + y * ca;
            q.z += r.datum;
        };
        auto xf2 = [&](Vec3& q) {  // prizma konturi: z ishlatilmaydi
            const double z = q.z;
            xf(q);
            q.z = z;
        };
        for (auto& pr : r.bm.prisms) {
            for (auto& q : pr.outline) xf2(q);
            for (auto& h : pr.holes)
                for (auto& q : h) xf2(q);
            pr.z0 += r.datum;
            pr.z1 += r.datum;
        }
        for (auto& b : r.bars)
            for (auto& q : b.pts) xf(q);
        r.bm.heightTop += r.datum;
        r.bm.zeroAbs = r.datum;
        r.bm.zeroAbsKnown = r.datumKnown;
        if (!r.georef) {   // lokal boshlang'ich nuqta — tekislangan ko'rinishdagi 0 ga teng o'qlar ("1"/"А" yoki "Ж"/"2/5")
            std::string nx = "1", ny = "\xD0\x90";
            for (const auto& v : r.views) {
                if (!v.aligned) continue;
                for (const auto& [n, x] : v.axesX)
                    if (std::abs(x) < 1e-6) nx = n;
                for (const auto& [n, y] : v.axesY)
                    if (std::abs(y) < 1e-6) ny = n;
                break;
            }
            issue(false, "kj.chk.local_xy", {"\xC2\xAB" + nx + "\xC2\xBB", "\xC2\xAB" + ny + "\xC2\xBB"});
        }
    }
    if (foundRings.empty() && r.typical.empty() && !multi) issue(true, "kj.chk.no_found");   // tipik qavatlar KJ sida fundament bo'lmaydi
    if (nColumns == 0) issue(false, "kj.chk.no_columns");
    // ustunlar plita ichidami (1-qavat)
    {
        int out = 0;
        bool rings = false;
        for (const auto& [g, L] : groupsAt(1)) rings = rings || !g->ol.rings.empty();
        if (rings)
            for (const auto& e : fv[1]) {
                if (e.wall) continue;
                bool in = false;
                for (const auto& [g, L] : groupsAt(1)) in = in || (!g->ol.rings.empty() && inPoly(g->ol.rings[0], e.c.x, e.c.y));
                out += !in;
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
    // chizmadagi spetsifikatsiya — bitta plita (bir qavat) uchun deb olinadi: shu jadval qaysi plita turi yonida bo'lsa,
    // uning belgilari soniga (tipik qavatlar) ko'paytiriladi
    BBox specBox;
    r.spec = readSpecification(doc, &specBox);
    std::size_t mult = r.typical.size();
    if (multi) {
        mult = 1;
        if (specBox.valid())
            for (const auto& g : groups)
                for (const KjView* v : g.views)
                    if (region(*v, center(specBox))) mult = std::max<std::size_t>(1, g.levels.size());
    }
    if (mult > 1)
        for (auto& s : r.spec) {
            s.count *= static_cast<int>(mult);
            s.mass *= static_cast<double>(mult);
        }
    for (const auto& s : r.spec) r.specMass += s.mass;
    for (const auto& v : r.views)
        if (v.use && !v.title.empty() && (v.kind == KjViewKind::Other || v.kind == KjViewKind::FoundSupports))
            issue(false, "kj.chk.skipped", {v.title});
    return r;
}

}  // namespace asiacad::bim
