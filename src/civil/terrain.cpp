#include <asiacad/civil/terrain.h>

#include <asiacad/civil/road.h>
#include <asiacad/civil/utility.h>
#include <asiacad/core/block.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <unordered_map>

namespace asiacad::civil {

namespace {
constexpr double kPI = 3.14159265358979323846;

bool inPoly(const std::vector<Vec3>& poly, double x, double y) {
    bool in = false;
    for (std::size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > y) != (poly[j].y > y) && x < (poly[j].x - poly[i].x) * (y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
    return in;
}
std::string comma(std::string s) {
    for (auto& c : s)
        if (c == '.') c = ',';
    return s;
}
std::string f2(double v) { return std::format("{:.2f}", v); }
std::string f3(double v) { return std::format("{:.3f}", v); }

struct Out {
    Entities& es;
    void put(std::unique_ptr<IEntity> e, const std::string& layer, int color = kColorByLayer) {
        e->setLayer(layer);
        e->setColor(color);
        es.push_back(std::move(e));
    }
    void line(const Vec3& a, const Vec3& b, const std::string& l, int c = kColorByLayer) { put(std::make_unique<LineEntity>(a, b), l, c); }
    void poly(std::vector<Vec3> v, bool closed, const std::string& l, int c = kColorByLayer) {
        if (v.size() >= 2) put(std::make_unique<PolylineEntity>(std::move(v), closed), l, c);
    }
    void text(const Vec3& p, double h, const std::string& s, const std::string& l, int c = kColorByLayer, double rot = 0,
              TextHAlign ha = TextHAlign::Left, TextVAlign va = TextVAlign::Bottom) {
        put(std::make_unique<TextEntity>(p, h, s, rot, ha, va), l, c);
    }
};

// chiziqli funksiyaning uchburchakdagi musbat qismi integrali (maydon A, uch qiymat)
double posPart(double A, double a, double b, double c) {
    double h[3] = {a, b, c};
    std::sort(h, h + 3);   // h0 ≤ h1 ≤ h2
    if (h[0] >= 0) return A * (h[0] + h[1] + h[2]) / 3;
    if (h[2] <= 0) return 0;
    if (h[1] <= 0)  // bitta musbat
        return A * h[2] * h[2] * h[2] / (3 * (h[2] - h[0]) * (h[2] - h[1]));
    // bitta manfiy: jami − manfiy qism
    const double neg = A * h[0] * h[0] * h[0] / (3 * (h[0] - h[1]) * (h[0] - h[2]));   // ≤ 0
    return A * (h[0] + h[1] + h[2]) / 3 - neg;
}
// ikki ro'yxat (masofa, qiymat) — umumiy masofalarda ayirma integrali (musbat / manfiy), chiziqli bo'laklar
double interp(const std::vector<std::pair<double, double>>& v, double x, bool& ok) {
    ok = false;
    if (v.empty() || x < v.front().first - 1e-9 || x > v.back().first + 1e-9) return 0;
    for (std::size_t i = 1; i < v.size(); ++i)
        if (x <= v[i].first + 1e-9) {
            const double d = v[i].first - v[i - 1].first;
            const double t = d > 1e-12 ? (x - v[i - 1].first) / d : 0;
            ok = true;
            return v[i - 1].second + t * (v[i].second - v[i - 1].second);
        }
    ok = true;
    return v.back().second;
}
}  // namespace

// ---------------------------------------------------------------- TIN (Bowyer–Watson, x bo'yicha saralangan — faol uchburchaklar)
Tin Tin::build(std::vector<Vec3> in, const std::vector<Vec3>* boundary, double maxEdge) {
    Tin t;
    std::sort(in.begin(), in.end(), [](const Vec3& a, const Vec3& b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    for (const auto& p : in)   // takrorlar (1 mm) — oxirgisi qoladi
        if (t.pts_.empty() || std::hypot(t.pts_.back().x - p.x, t.pts_.back().y - p.y) > 1e-3) t.pts_.push_back(p);
    // x bo'yicha saralangan bo'lsa ham bir xil x da yaqin nuqtalar uzoqroq qolishi mumkin — to'liq tekshiruv shart emas
    const int n = static_cast<int>(t.pts_.size());
    if (n < 3) return t;
    BBox b;
    for (const auto& p : t.pts_) b.expand(p);
    const double dmax = std::max({b.max.x - b.min.x, b.max.y - b.min.y, 1e-3});
    const double mx = (b.min.x + b.max.x) / 2, my = (b.min.y + b.max.y) / 2;
    std::vector<Vec3> P = t.pts_;
    P.push_back({mx - 20 * dmax, my - dmax, 0});
    P.push_back({mx, my + 20 * dmax, 0});
    P.push_back({mx + 20 * dmax, my - dmax, 0});
    struct T { int a, b, c; double cx, cy, r2; };
    auto make = [&](int a, int bb, int c) {
        const Vec3 &A = P[a], &B = P[bb], &C = P[c];
        const double d = 2 * (A.x * (B.y - C.y) + B.x * (C.y - A.y) + C.x * (A.y - B.y));
        T tr{a, bb, c, 0, 0, 1e300};
        if (std::abs(d) > 1e-18) {
            const double a2 = A.x * A.x + A.y * A.y, b2 = B.x * B.x + B.y * B.y, c2 = C.x * C.x + C.y * C.y;
            tr.cx = (a2 * (B.y - C.y) + b2 * (C.y - A.y) + c2 * (A.y - B.y)) / d;
            tr.cy = (a2 * (C.x - B.x) + b2 * (A.x - C.x) + c2 * (B.x - A.x)) / d;
            tr.r2 = (A.x - tr.cx) * (A.x - tr.cx) + (A.y - tr.cy) * (A.y - tr.cy);
        } else {
            tr.cx = (A.x + B.x + C.x) / 3;
            tr.cy = (A.y + B.y + C.y) / 3;
        }
        return tr;
    };
    std::vector<T> active{make(n, n + 1, n + 2)}, done;
    std::vector<std::pair<int, int>> edges;
    for (int i = 0; i < n; ++i) {
        const Vec3& p = P[i];
        edges.clear();
        std::vector<T> keep;
        keep.reserve(active.size() + 4);
        for (const auto& tr : active) {
            const double dx = p.x - tr.cx;
            if (dx > 0 && dx * dx > tr.r2) { done.push_back(tr); continue; }   // endi hech qachon buzilmaydi
            const double dy = p.y - tr.cy;
            if (dx * dx + dy * dy <= tr.r2 * (1 + 1e-12)) {
                edges.push_back({tr.a, tr.b});
                edges.push_back({tr.b, tr.c});
                edges.push_back({tr.c, tr.a});
            } else keep.push_back(tr);
        }
        std::map<std::pair<int, int>, int> cnt;
        for (auto e : edges) ++cnt[{std::min(e.first, e.second), std::max(e.first, e.second)}];
        for (auto e : edges)
            if (cnt[{std::min(e.first, e.second), std::max(e.first, e.second)}] == 1) keep.push_back(make(e.first, e.second, i));
        active.swap(keep);
    }
    done.insert(done.end(), active.begin(), active.end());
    for (const auto& tr : done) {
        if (tr.a >= n || tr.b >= n || tr.c >= n) continue;
        const Vec3 &A = t.pts_[tr.a], &B = t.pts_[tr.b], &C = t.pts_[tr.c];
        const double cr = (B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x);
        if (std::abs(cr) < 1e-12) continue;
        if (maxEdge > 0 && (std::hypot(A.x - B.x, A.y - B.y) > maxEdge || std::hypot(B.x - C.x, B.y - C.y) > maxEdge || std::hypot(C.x - A.x, C.y - A.y) > maxEdge))
            continue;
        if (boundary && boundary->size() >= 3 && !inPoly(*boundary, (A.x + B.x + C.x) / 3, (A.y + B.y + C.y) / 3)) continue;
        t.tri_.push_back(cr > 0 ? std::array<int, 3>{tr.a, tr.b, tr.c} : std::array<int, 3>{tr.a, tr.c, tr.b});
    }
    t.index();
    return t;
}

void Tin::index() {
    box_ = {};
    for (const auto& tr : tri_)
        for (int k : tr) box_.expand(pts_[k]);
    grid_.clear();
    if (tri_.empty()) return;
    const double W = std::max(1e-6, box_.max.x - box_.min.x), H = std::max(1e-6, box_.max.y - box_.min.y);
    cell_ = std::max(1e-3, std::sqrt(W * H / static_cast<double>(tri_.size())) * 1.5);
    gx_ = std::min(2000, static_cast<int>(W / cell_) + 1);
    gy_ = std::min(2000, static_cast<int>(H / cell_) + 1);
    cell_ = std::max(W / gx_, H / gy_) * 1.0000001;
    gx_ = static_cast<int>(W / cell_) + 1;
    gy_ = static_cast<int>(H / cell_) + 1;
    grid_.assign(static_cast<std::size_t>(gx_ * gy_), {});
    for (int k = 0; k < static_cast<int>(tri_.size()); ++k) {
        BBox tb;
        for (int v : tri_[k]) tb.expand(pts_[v]);
        const int i0 = static_cast<int>((tb.min.x - box_.min.x) / cell_), i1 = static_cast<int>((tb.max.x - box_.min.x) / cell_);
        const int j0 = static_cast<int>((tb.min.y - box_.min.y) / cell_), j1 = static_cast<int>((tb.max.y - box_.min.y) / cell_);
        for (int j = std::max(0, j0); j <= std::min(gy_ - 1, j1); ++j)
            for (int i = std::max(0, i0); i <= std::min(gx_ - 1, i1); ++i) grid_[static_cast<std::size_t>(i + j * gx_)].push_back(k);
    }
}

bool Tin::z(double x, double y, double& out) const {
    if (tri_.empty() || x < box_.min.x - 1e-9 || y < box_.min.y - 1e-9 || x > box_.max.x + 1e-9 || y > box_.max.y + 1e-9) return false;
    const int i = std::clamp(static_cast<int>((x - box_.min.x) / cell_), 0, gx_ - 1), j = std::clamp(static_cast<int>((y - box_.min.y) / cell_), 0, gy_ - 1);
    for (int k : grid_[static_cast<std::size_t>(i + j * gx_)]) {
        const Vec3 &A = pts_[tri_[k][0]], &B = pts_[tri_[k][1]], &C = pts_[tri_[k][2]];
        const double d = (B.y - C.y) * (A.x - C.x) + (C.x - B.x) * (A.y - C.y);
        const double l1 = ((B.y - C.y) * (x - C.x) + (C.x - B.x) * (y - C.y)) / d;
        const double l2 = ((C.y - A.y) * (x - C.x) + (A.x - C.x) * (y - C.y)) / d;
        const double l3 = 1 - l1 - l2;
        if (l1 >= -1e-9 && l2 >= -1e-9 && l3 >= -1e-9) {
            out = l1 * A.z + l2 * B.z + l3 * C.z;
            return true;
        }
    }
    return false;
}

double Tin::area() const {
    double a = 0;
    for (const auto& t : tri_) {
        const Vec3 &A = pts_[t[0]], &B = pts_[t[1]], &C = pts_[t[2]];
        a += std::abs((B.x - A.x) * (C.y - A.y) - (B.y - A.y) * (C.x - A.x)) / 2;
    }
    return a;
}

std::vector<std::vector<Vec3>> Tin::contours(double L) const {
    // har uchburchakda kesma; uchlar — qirra (min, max indeks) bo'yicha — zanjirlash aniq
    using Key = std::pair<int, int>;
    std::map<Key, Vec3> at;
    std::multimap<Key, Key> adj;
    for (const auto& t : tri_) {
        std::vector<Key> hit;
        for (int e = 0; e < 3; ++e) {
            const int a = t[e], b = t[(e + 1) % 3];
            const double za = pts_[a].z - L, zb = pts_[b].z - L;
            const double sa = za == 0 ? 1e-12 : za, sb = zb == 0 ? 1e-12 : zb;
            if ((sa > 0) == (sb > 0)) continue;
            const Key k{std::min(a, b), std::max(a, b)};
            const double u = sa / (sa - sb);
            at[k] = Vec3{pts_[a].x + u * (pts_[b].x - pts_[a].x), pts_[a].y + u * (pts_[b].y - pts_[a].y), L};
            hit.push_back(k);
        }
        if (hit.size() == 2) {
            adj.insert({hit[0], hit[1]});
            adj.insert({hit[1], hit[0]});
        }
    }
    std::vector<std::vector<Vec3>> out;
    std::set<Key> used;
    auto walk = [&](Key start, std::vector<Key>& chain) {
        Key cur = start;
        while (true) {
            used.insert(cur);
            chain.push_back(cur);
            Key next{-1, -1};
            auto r = adj.equal_range(cur);
            for (auto it = r.first; it != r.second; ++it)
                if (!used.count(it->second)) { next = it->second; break; }
            if (next.first < 0) break;
            cur = next;
        }
    };
    // avval ochiq zanjirlar (bitta qo'shnili uchlardan), keyin yopiqlari
    for (int pass = 0; pass < 2; ++pass)
        for (const auto& [k, p] : at) {
            if (used.count(k)) continue;
            if (pass == 0 && adj.count(k) != 1) continue;
            std::vector<Key> ch;
            walk(k, ch);
            std::vector<Vec3> v;
            for (const auto& c : ch) v.push_back(at[c]);
            if (pass == 1 && v.size() > 2) v.push_back(v.front());
            if (v.size() >= 2) out.push_back(std::move(v));
        }
    return out;
}

// ---------------------------------------------------------------- manba nuqtalar
std::vector<Vec3> terrainPoints(const Document& doc, const std::vector<EntityId>& ids) {
    std::vector<Vec3> out;
    std::function<void(const IEntity&)> visit = [&](const IEntity& e) {
        const std::string& L = e.layer();
        if (L.rfind("SYOMKA_", 0) == 0 || L.rfind("DEV_", 0) == 0 || L.rfind("KARTOGRAMMA", 0) == 0 || L.rfind("PROFIL", 0) == 0 || L.rfind("RAZREZ", 0) == 0)
            return;   // o'z yozuvlarimiz
        if (auto* ins = dynamic_cast<const InsertEntity*>(&e)) {
            Vec3 p = ins->insertionPoint();
            for (const auto& a : ins->attributes()) {
                double v;
                if (parseUtilLabel(a.text.text(), v) == 'z') p.z = v;
            }
            out.push_back(p);
            return;
        }
        if (auto* p = dynamic_cast<const PointEntity*>(&e)) { out.push_back(p->position()); return; }
        if (auto* t = dynamic_cast<const TextEntity*>(&e)) {
            double v;
            if (parseUtilLabel(t->text(), v) == 'z') out.push_back({t->insertion().x, t->insertion().y, v});
            return;
        }
        if (auto* l = dynamic_cast<const LineEntity*>(&e)) {
            for (const Vec3& q : {l->startPoint(), l->endPoint()})
                if (std::abs(q.z) > 1e-9) out.push_back(q);
            return;
        }
        if (auto* pl = dynamic_cast<const PolylineEntity*>(&e)) {
            const auto v = pl->vertices();
            bool z = false;
            for (const auto& q : v) z = z || std::abs(q.z) > 1e-9;
            if (!z) return;
            // gorizontal: uchlar + uzun segmentlarda oraliq nuqtalar (TIN tekis "zinapoya" bo'lmasin)
            for (std::size_t i = 0; i < v.size(); ++i) {
                out.push_back(v[i]);
                if (i + 1 < v.size()) {
                    const double L2 = std::hypot(v[i + 1].x - v[i].x, v[i + 1].y - v[i].y);
                    const int k = static_cast<int>(L2 / 10);
                    for (int j = 1; j <= k; ++j) out.push_back(v[i] + (v[i + 1] - v[i]) * (j / (k + 1.0)));
                }
            }
        }
    };
    for (EntityId id : ids)
        if (const IEntity* e = doc.find(id)) visit(*e);
    return out;
}

std::vector<Vec3> pathOf(const IEntity& e) {
    if (auto* l = dynamic_cast<const LineEntity*>(&e)) return {l->startPoint(), l->endPoint()};
    if (auto* pl = dynamic_cast<const PolylineEntity*>(&e)) {
        auto v = pl->tessellated().vertices();
        if (pl->isClosed() && !v.empty()) v.push_back(v.front());
        return v;
    }
    if (auto* a = dynamic_cast<const ArcEntity*>(&e)) {
        std::vector<Vec3> v;
        const int n = std::max(4, static_cast<int>(a->sweep() / (kPI / 72)));
        for (int i = 0; i <= n; ++i) v.push_back(a->pointAtAngle(a->startAngle() + a->sweep() * i / n));
        return v;
    }
    return {};
}

bool Design::z(double x, double y, double& out) const {
    if (tin) return tin->z(x, y, out);
    if (plane) {
        out = z0 + ix * (x - base.x) + iy * (y - base.y);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- bo'ylama profil
namespace {
struct Walk {
    std::vector<Vec3> p;
    std::vector<double> s;
    explicit Walk(const std::vector<Vec3>& v) {
        for (const auto& q : v)
            if (p.empty() || std::hypot(q.x - p.back().x, q.y - p.back().y) > 1e-9) p.push_back(q);
        s.push_back(0);
        for (std::size_t i = 1; i < p.size(); ++i) s.push_back(s.back() + std::hypot(p[i].x - p[i - 1].x, p[i].y - p[i - 1].y));
    }
    double length() const { return s.empty() ? 0 : s.back(); }
    void at(double st, Vec3& pt, Vec3& tan) const {
        st = std::clamp(st, 0.0, length());
        std::size_t i = 1;
        while (i + 1 < s.size() && s[i] < st) ++i;
        const double d = s[i] - s[i - 1], t = d > 1e-12 ? (st - s[i - 1]) / d : 0;
        pt = p[i - 1] + (p[i] - p[i - 1]) * t;
        tan = Vec3{p[i].x - p[i - 1].x, p[i].y - p[i - 1].y, 0} / std::max(1e-12, d);
    }
};
// piket qadamidagi va burilish nuqtalaridagi stansiyalar
std::vector<double> stations(const Walk& w, double step, double pk0) {
    std::vector<double> st{0};
    if (step > 0) {
        double first = std::ceil((pk0 + 1e-9) / step) * step - pk0;
        for (double x = first; x < w.length() - 1e-6; x += step)
            if (x > 1e-6) st.push_back(x);
    }
    for (std::size_t i = 1; i + 1 < w.s.size(); ++i) st.push_back(w.s[i]);
    st.push_back(w.length());
    std::sort(st.begin(), st.end());
    st.erase(std::unique(st.begin(), st.end(), [](double a, double b) { return std::abs(a - b) < 0.05; }), st.end());
    return st;
}
}  // namespace

std::vector<ProfilePoint> sampleProfile(const std::vector<Vec3>& path, const Tin& eg, const Design& fg, const ProfileSpec& p) {
    const Walk w(path);
    if (w.p.size() < 2) throw Error("terr.err.path", {});
    const auto st = stations(w, p.step, p.pk0);
    std::set<double> stSet(st.begin(), st.end());
    const double fine = std::clamp(w.length() / 4000, 0.25, std::max(0.25, p.step / 10));
    std::vector<double> all = st;
    for (double x = 0; x < w.length(); x += fine) all.push_back(x);
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end(), [](double a, double b) { return std::abs(a - b) < 1e-6; }), all.end());
    std::vector<ProfilePoint> out;
    for (double s : all) {
        ProfilePoint pp;
        pp.s = s;
        Vec3 pt, tan;
        w.at(s, pt, tan);
        pp.hasG = eg.z(pt.x, pt.y, pp.zg);
        pp.hasD = fg.z(pt.x, pt.y, pp.zd);
        pp.station = std::any_of(st.begin(), st.end(), [&](double x) { return std::abs(x - s) < 1e-6; });
        out.push_back(pp);
    }
    // to'g'ri chiziqdagi oraliq nuqtalar olib tashlanadi (1 mm)
    std::vector<ProfilePoint> r;
    for (std::size_t i = 0; i < out.size(); ++i) {
        if (i > 0 && i + 1 < out.size() && !out[i].station) {
            const auto &a = r.back(), &b = out[i], &c = out[i + 1];
            auto lin = [&](double za, double zb, double zc) { return std::abs(za + (zc - za) * (b.s - a.s) / std::max(1e-12, c.s - a.s) - zb) < 1e-3; };
            if (a.hasG == b.hasG && b.hasG == c.hasG && a.hasD == b.hasD && b.hasD == c.hasD && (!b.hasG || lin(a.zg, b.zg, c.zg)) && (!b.hasD || lin(a.zd, b.zd, c.zd)))
                continue;
        }
        r.push_back(out[i]);
    }
    return r;
}

Entities drawProfile(const std::vector<ProfilePoint>& pr, const ProfileSpec& p, const std::function<std::string(const std::string&)>& tr) {
    Entities es;
    Out o{es};
    if (pr.empty()) return es;
    const double k = p.hScale / 1000;          // qog'oz mm -> chizma m
    const double ex = p.hScale / p.vScale;     // vertikal bo'rttirish
    const double th = p.textH * k;
    double zmin = 1e300, zmax = -1e300;
    for (const auto& q : pr) {
        if (q.hasG) { zmin = std::min(zmin, q.zg); zmax = std::max(zmax, q.zg); }
        if (q.hasD) { zmin = std::min(zmin, q.zd); zmax = std::max(zmax, q.zd); }
    }
    if (zmin > zmax) throw Error("terr.err.nodata", {});
    double datum = p.datum;
    if (std::isnan(datum)) datum = std::floor(zmin) - 1;
    const double L = pr.back().s;
    const Vec3 O = p.at;
    // jadval qatorlari (yuqoridan pastga): loyiha, yer, ishchi belgi, masofa, PK
    const double rh[5] = {15 * k, 15 * k, 15 * k, 10 * k, 10 * k};
    const char* rk[5] = {"terr.prof.design", "terr.prof.ground", "terr.prof.work", "terr.prof.dist", "terr.prof.pk"};
    const double headW = 45 * k;
    double tableH = 0;
    for (double h : rh) tableH += h;
    const double y0 = O.y + tableH;   // grafik pastki chizig'i (shartli gorizont)
    auto X = [&](double s) { return O.x + s; };
    auto Y = [&](double z) { return y0 + (z - datum) * ex; };
    const double top = Y(zmax) + 3 * th;
    // jadval chiziqlari
    double y = y0;
    o.line({X(0) - headW, y0, 0}, {X(L), y0, 0}, "PROFIL_JADVAL");
    std::vector<double> rowY;
    for (int r = 0; r < 5; ++r) {
        rowY.push_back(y);
        o.text({X(0) - headW + th * 0.5, y - rh[r] / 2, 0}, th * 0.9, tr(rk[r]), "PROFIL_JADVAL", kColorByLayer, 0, TextHAlign::Left, TextVAlign::Middle);
        y -= rh[r];
        o.line({X(0) - headW, y, 0}, {X(L), y, 0}, "PROFIL_JADVAL");
    }
    rowY.push_back(y);
    o.line({X(0) - headW, y0, 0}, {X(0) - headW, y, 0}, "PROFIL_JADVAL");
    o.line({X(0), y0, 0}, {X(0), y, 0}, "PROFIL_JADVAL");
    o.line({X(L), top, 0}, {X(L), y, 0}, "PROFIL_JADVAL");
    o.line({X(0), y0, 0}, {X(0), top, 0}, "PROFIL_JADVAL");
    // shartli gorizont va belgilar shkalasi
    o.text({X(0) - headW, y0 + th * 0.5, 0}, th, tr("terr.prof.datum") + " " + comma(f2(datum)), "PROFIL_YOZUV");
    const double tick = std::max(1.0, std::pow(10, std::floor(std::log10(std::max(1.0, (zmax - datum) / 5)))));
    for (double z = datum + tick; z <= zmax + tick * 0.5; z += tick) {
        o.line({X(0) - th, Y(z), 0}, {X(0), Y(z), 0}, "PROFIL_JADVAL");
        o.text({X(0) - th * 1.4, Y(z), 0}, th * 0.8, comma(f2(z)), "PROFIL_YOZUV", kColorByLayer, 0, TextHAlign::Right, TextVAlign::Middle);
    }
    // chiziqlar (yer — jigarrang, loyiha — qizil); uzilishlar hisobga olinadi
    auto drawLine = [&](bool design) {
        std::vector<Vec3> run;
        for (const auto& q : pr) {
            const bool has = design ? q.hasD : q.hasG;
            if (has) run.push_back({X(q.s), Y(design ? q.zd : q.zg), 0});
            else { o.poly(run, false, design ? "PROFIL_LOYIHA" : "PROFIL_YER", design ? 1 : 32); run.clear(); }
        }
        o.poly(run, false, design ? "PROFIL_LOYIHA" : "PROFIL_YER", design ? 1 : 32);
    };
    drawLine(false);
    drawLine(true);
    // stansiyalar: vertikal chiziqlar va jadval qiymatlari (90° burilgan)
    double prevS = -1;
    for (const auto& q : pr) {
        if (!q.station) continue;
        const double zt = q.hasD && q.hasG ? std::max(q.zd, q.zg) : (q.hasD ? q.zd : (q.hasG ? q.zg : datum));
        o.line({X(q.s), y0, 0}, {X(q.s), Y(zt), 0}, "PROFIL_JADVAL", 8);
        auto cellText = [&](int r, const std::string& s, int col = kColorByLayer) {
            o.text({X(q.s) - th * 0.15, rowY[static_cast<std::size_t>(r)] - rh[r] + th * 0.4, 0}, th * 0.8, s, "PROFIL_YOZUV", col, kPI / 2, TextHAlign::Left, TextVAlign::Bottom);
        };
        if (q.hasD) cellText(0, comma(f2(q.zd)), 1);
        if (q.hasG) cellText(1, comma(f2(q.zg)));
        if (q.hasD && q.hasG) {
            const double h = q.zd - q.zg;
            cellText(2, (h >= 0 ? "+" : "") + comma(f2(h)), h >= 0 ? 1 : 5);
        }
        // jadvalda stansiya chegarasi
        o.line({X(q.s), rowY[3], 0}, {X(q.s), rowY[5], 0}, "PROFIL_JADVAL");
        if (prevS >= 0 && q.s - prevS > 1e-6)
            o.text({X((prevS + q.s) / 2), rowY[3] - rh[3] / 2, 0}, th * 0.8, comma(std::format("{:.2f}", q.s - prevS)), "PROFIL_YOZUV", kColorByLayer, 0, TextHAlign::Center,
                   TextVAlign::Middle);
        const double pkv = p.pk0 + q.s;
        if (std::abs(pkv / 100 - std::round(pkv / 100)) < 1e-6)
            o.text({X(q.s), rowY[4] - rh[4] / 2, 0}, th * 0.9, std::format("{}", static_cast<long long>(std::llround(pkv / 100))), "PROFIL_YOZUV", kColorByLayer, 0,
                   TextHAlign::Center, TextVAlign::Middle);
        else
            cellText(4, comma(std::format("+{:.2f}", std::fmod(pkv, 100.0))));
        prevS = q.s;
    }
    o.text({X(0), top + th, 0}, th * 1.4, tr("terr.prof.title") + std::format("  1:{:g} / 1:{:g}", p.hScale, p.vScale), "PROFIL_YOZUV");
    return es;
}

// ---------------------------------------------------------------- ko'ndalang kesimlar
std::vector<XSection> sampleSections(const std::vector<Vec3>& path, const Tin& eg, const Design& fg, const SectionSpec& p) {
    const Walk w(path);
    if (w.p.size() < 2) throw Error("terr.err.path", {});
    if (p.step <= 0 || p.left < 0 || p.right < 0 || p.left + p.right <= 0) throw Error("terr.err.params", {});
    std::vector<double> st;
    {
        const double first = std::ceil((p.pk0 + 1e-9) / p.step) * p.step - p.pk0;
        st.push_back(0);
        for (double x = first; x < w.length() - 1e-6; x += p.step)
            if (x > 1e-6) st.push_back(x);
        if (w.length() - st.back() > 1e-3) st.push_back(w.length());
    }
    std::vector<XSection> out;
    const double ds = std::max(0.05, p.sample);
    for (double s : st) {
        XSection x;
        x.s = s;
        Vec3 tan;
        w.at(s, x.c, tan);
        x.n = {-tan.y, tan.x, 0};
        std::vector<double> offs;
        for (double o = -p.left; o < p.right - 1e-9; o += ds) offs.push_back(o);
        offs.push_back(p.right);
        offs.push_back(0);
        std::sort(offs.begin(), offs.end());
        offs.erase(std::unique(offs.begin(), offs.end(), [](double a, double b) { return std::abs(a - b) < 1e-9; }), offs.end());
        for (double o : offs) {
            const Vec3 q = x.c - x.n * o;   // chap — manfiy masofa
            double z;
            if (eg.z(q.x, q.y, z)) x.g.push_back({o, z});
            if (fg.z(q.x, q.y, z)) x.d.push_back({o, z});
        }
        // maydonlar: umumiy oraliqda (loyiha − yer), chiziqli bo'laklar (nol kesishishi bilan)
        std::vector<double> xs;
        for (const auto& a : x.g) xs.push_back(a.first);
        for (const auto& a : x.d) xs.push_back(a.first);
        std::sort(xs.begin(), xs.end());
        xs.erase(std::unique(xs.begin(), xs.end(), [](double a, double b) { return std::abs(a - b) < 1e-9; }), xs.end());
        for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
            bool o1, o2, o3, o4;
            const double g1 = interp(x.g, xs[i], o1), g2 = interp(x.g, xs[i + 1], o2), d1 = interp(x.d, xs[i], o3), d2 = interp(x.d, xs[i + 1], o4);
            if (!(o1 && o2 && o3 && o4)) continue;
            const double h1 = d1 - g1, h2 = d2 - g2, L = xs[i + 1] - xs[i];
            if (h1 >= 0 && h2 >= 0) x.fill += (h1 + h2) / 2 * L;
            else if (h1 <= 0 && h2 <= 0) x.cut += -(h1 + h2) / 2 * L;
            else {
                const double t = h1 / (h1 - h2);
                if (h1 > 0) { x.fill += h1 * t * L / 2; x.cut += -h2 * (1 - t) * L / 2; }
                else { x.cut += -h1 * t * L / 2; x.fill += h2 * (1 - t) * L / 2; }
            }
        }
        out.push_back(std::move(x));
    }
    return out;
}

SectionVolumes sectionVolumes(const std::vector<XSection>& xs) {
    SectionVolumes v;
    for (std::size_t i = 0; i + 1 < xs.size(); ++i) {
        const double L = xs[i + 1].s - xs[i].s;
        v.fillV.push_back((xs[i].fill + xs[i + 1].fill) / 2 * L);
        v.cutV.push_back((xs[i].cut + xs[i + 1].cut) / 2 * L);
        v.fill += v.fillV.back();
        v.cut += v.cutV.back();
    }
    return v;
}

Entities drawSectionMarks(const std::vector<XSection>& xs, const SectionSpec& p) {
    Entities es;
    Out o{es};
    const double th = std::max(0.2, (p.left + p.right) / 40);
    for (const auto& x : xs) {
        const Vec3 a = x.c + x.n * p.left, b = x.c - x.n * p.right;
        o.line(a, b, "RAZREZ_IZ", 6);
        const double rot = std::atan2(b.y - a.y, b.x - a.x);
        o.text(a + x.n * (th * 0.4), th, pk(p.pk0 + x.s), "RAZREZ_IZ", 6, rot, TextHAlign::Left, TextVAlign::Bottom);
    }
    return es;
}

Entities drawSections(const std::vector<XSection>& xs, const SectionVolumes& v, const SectionSpec& p, const std::function<std::string(const std::string&)>& tr) {
    Entities es;
    Out o{es};
    if (xs.empty()) return es;
    const double k = p.scale / 1000;
    const double th = p.textH * k;
    double hmax = 2;
    for (const auto& x : xs) {
        double lo = 1e300, hi = -1e300;
        for (const auto& a : x.g) { lo = std::min(lo, a.second); hi = std::max(hi, a.second); }
        for (const auto& a : x.d) { lo = std::min(lo, a.second); hi = std::max(hi, a.second); }
        if (lo <= hi) hmax = std::max(hmax, hi - lo);
    }
    const double W = p.left + p.right + 12 * th, H = hmax + 12 * th;
    const int cols = std::max(1, p.cols);
    for (std::size_t i = 0; i < xs.size(); ++i) {
        const auto& x = xs[i];
        const double bx = p.at.x + static_cast<double>(i % static_cast<std::size_t>(cols)) * W;
        const double by = p.at.y - static_cast<double>(i / static_cast<std::size_t>(cols)) * H;
        double lo = 1e300, hi = -1e300;
        for (const auto& a : x.g) { lo = std::min(lo, a.second); hi = std::max(hi, a.second); }
        for (const auto& a : x.d) { lo = std::min(lo, a.second); hi = std::max(hi, a.second); }
        if (lo > hi) { lo = 0; hi = 1; }
        const double datum = std::floor(lo - 1);
        const double cx = bx + 6 * th + p.left;   // o'q
        const double base = by - H + 5 * th;      // shartli gorizont
        auto P = [&](double off, double z) { return Vec3{cx + off, base + (z - datum), 0}; };
        o.poly({{bx, by, 0}, {bx + W, by, 0}, {bx + W, by - H, 0}, {bx, by - H, 0}}, true, "RAZREZ_RAMKA", 8);
        // to'kma / kesma maydonlari — shtrix
        std::vector<double> xsAll;
        for (const auto& a : x.g) xsAll.push_back(a.first);
        for (const auto& a : x.d) xsAll.push_back(a.first);
        std::sort(xsAll.begin(), xsAll.end());
        xsAll.erase(std::unique(xsAll.begin(), xsAll.end(), [](double a, double b) { return std::abs(a - b) < 1e-9; }), xsAll.end());
        std::vector<std::array<double, 3>> pts;   // off, g, d (umumiy)
        for (double off : xsAll) {
            bool a, b;
            const double g = interp(x.g, off, a), d = interp(x.d, off, b);
            if (a && b) pts.push_back({off, g, d});
        }
        // nol kesishishlarini qo'shib, ishorasi bir xil bo'laklarga ajratish
        std::vector<std::array<double, 3>> q;
        for (std::size_t j = 0; j < pts.size(); ++j) {
            if (j > 0) {
                const double h1 = pts[j - 1][2] - pts[j - 1][1], h2 = pts[j][2] - pts[j][1];
                if (h1 * h2 < 0) {
                    const double t = h1 / (h1 - h2);
                    const double off = pts[j - 1][0] + t * (pts[j][0] - pts[j - 1][0]);
                    const double g = pts[j - 1][1] + t * (pts[j][1] - pts[j - 1][1]);
                    q.push_back({off, g, g});
                }
            }
            q.push_back(pts[j]);
        }
        auto flush = [&](std::vector<std::array<double, 3>>& run, int sign) {
            if (run.size() >= 2 && sign != 0) {
                std::vector<Vec3> poly;
                for (const auto& r : run) poly.push_back(P(r[0], r[2]));
                for (auto it = run.rbegin(); it != run.rend(); ++it) poly.push_back(P((*it)[0], (*it)[1]));
                double area = 0;
                for (std::size_t m = 0; m < poly.size(); ++m) {
                    const auto& a = poly[m];
                    const auto& b = poly[(m + 1) % poly.size()];
                    area += a.x * b.y - b.x * a.y;
                }
                if (std::abs(area) > 1e-6) {
                    PolylineEntity loop(poly, true);
                    o.put(std::make_unique<HatchEntity>(std::vector<PolylineEntity>{loop}, "ANSI31", std::max(0.05, th * 0.25)), sign > 0 ? "RAZREZ_TOKMA" : "RAZREZ_KESMA",
                          sign > 0 ? 1 : 5);
                }
            }
            run.clear();
        };
        std::vector<std::array<double, 3>> run;
        int sign = 0;
        for (const auto& r : q) {
            const double h = r[2] - r[1];
            const int sg = h > 1e-9 ? 1 : (h < -1e-9 ? -1 : 0);
            if (sg == 0) {
                run.push_back(r);
                flush(run, sign);
                run.push_back(r);
                sign = 0;
                continue;
            }
            if (sign != 0 && sg != sign) {
                flush(run, sign);
            }
            sign = sg;
            run.push_back(r);
        }
        flush(run, sign);
        // chiziqlar
        std::vector<Vec3> g, d;
        for (const auto& a : x.g) g.push_back(P(a.first, a.second));
        for (const auto& a : x.d) d.push_back(P(a.first, a.second));
        o.poly(g, false, "RAZREZ_YER", 32);
        o.poly(d, false, "RAZREZ_LOYIHA", 1);
        o.line(P(-p.left, datum), P(p.right, datum), "RAZREZ_RAMKA", 8);
        o.line(P(0, datum), P(0, hi + 0.5), "RAZREZ_RAMKA", 8);
        o.text({bx + th, base, 0}, th * 0.8, comma(f2(datum)), "RAZREZ_YOZUV", kColorByLayer, 0, TextHAlign::Left, TextVAlign::Bottom);
        // o'qdagi belgilar
        bool a1, a2;
        const double gz = interp(x.g, 0, a1), dz = interp(x.d, 0, a2);
        if (a2) o.text(P(0.3 * th, dz) + Vec3{0, th * 0.5, 0}, th * 0.8, comma(f2(dz)), "RAZREZ_YOZUV", 1);
        if (a1) o.text(P(0.3 * th, gz) - Vec3{0, th * 1.4, 0}, th * 0.8, comma(f2(gz)), "RAZREZ_YOZUV");
        o.text({cx, by - th * 1.6, 0}, th * 1.2, pk(p.pk0 + x.s), "RAZREZ_YOZUV", kColorByLayer, 0, TextHAlign::Center, TextVAlign::Bottom);
        o.text({cx, base - th * 2.2, 0}, th * 0.85,
               tr("terr.sec.fill") + " " + comma(f2(x.fill)) + " m\xC2\xB2   " + tr("terr.sec.cut") + " " + comma(f2(x.cut)) + " m\xC2\xB2", "RAZREZ_YOZUV",
               kColorByLayer, 0, TextHAlign::Center, TextVAlign::Bottom);
    }
    // hajmlar vedomosti (kesimlar ostida)
    const std::size_t rows = (xs.size() + static_cast<std::size_t>(cols) - 1) / static_cast<std::size_t>(cols);
    const double ty = p.at.y - static_cast<double>(rows) * H - 4 * th, rh = 8 * k;
    const double cw[7] = {30 * k, 36 * k, 36 * k, 22 * k, 36 * k, 36 * k, 0};
    const char* hk[6] = {"terr.col.pk", "terr.col.fillA", "terr.col.cutA", "terr.col.len", "terr.col.fillV", "terr.col.cutV"};
    double TW = 0;
    for (int c = 0; c < 6; ++c) TW += cw[c];
    o.text({p.at.x, ty + th * 0.6, 0}, th * 1.2, tr("terr.sec.table"), "RAZREZ_JADVAL");
    auto row = [&](int r, const std::vector<std::string>& v, int color = kColorByLayer) {
        double xx = p.at.x;
        for (std::size_t c = 0; c < 6 && c < v.size(); ++c) {
            o.text({xx + th * 0.4, ty - rh * (r + 0.5), 0}, th * (r == 0 ? 0.7 : 0.8), v[c], "RAZREZ_JADVAL", color, 0, TextHAlign::Left, TextVAlign::Middle);
            xx += cw[c];
        }
    };
    const int nr = static_cast<int>(xs.size()) + 2;
    for (int r = 0; r <= nr; ++r) o.line({p.at.x, ty - rh * r, 0}, {p.at.x + TW, ty - rh * r, 0}, "RAZREZ_JADVAL");
    double xx = p.at.x;
    for (int c = 0; c <= 6; ++c) {
        o.line({xx, ty, 0}, {xx, ty - rh * nr, 0}, "RAZREZ_JADVAL");
        if (c < 6) xx += cw[c];
    }
    std::vector<std::string> h;
    for (const char* key : hk) h.push_back(tr(key));
    row(0, h);
    for (std::size_t i = 0; i < xs.size(); ++i) {
        std::vector<std::string> r{pk(p.pk0 + xs[i].s), comma(f2(xs[i].fill)), comma(f2(xs[i].cut))};
        if (i + 1 < xs.size()) {
            r.push_back(comma(f2(xs[i + 1].s - xs[i].s)));
            r.push_back(comma(std::format("{:.1f}", v.fillV[i])));
            r.push_back(comma(std::format("{:.1f}", v.cutV[i])));
        }
        row(static_cast<int>(i) + 1, r);
    }
    row(nr - 1, {tr("terr.total"), "", "", comma(f2(xs.back().s - xs.front().s)), comma(std::format("{:.1f}", v.fill)), comma(std::format("{:.1f}", v.cut))});
    return es;
}

// ---------------------------------------------------------------- kartogramma
CartoResult cartogram(const Tin& eg, Design fg, const CartoSpec& s) {
    if (eg.empty()) throw Error("terr.err.noeg", {});
    if (s.cell <= 0) throw Error("terr.err.params", {});
    CartoResult r;
    r.cell = s.cell;
    BBox b;
    if (s.boundary.size() >= 3)
        for (const auto& q : s.boundary) b.expand(q);
    else b = eg.bounds();
    const double a = s.cell;
    r.origin = s.origin ? *s.origin : Vec3{b.min.x, b.min.y, 0};   // to'r maydon burchagidan
    r.nx = std::max(1, static_cast<int>(std::ceil((b.max.x - r.origin.x) / a - 1e-9)));
    r.ny = std::max(1, static_cast<int>(std::ceil((b.max.y - r.origin.y) / a - 1e-9)));
    if (static_cast<long long>(r.nx) * r.ny > 4000000) throw Error("terr.err.toomany", {std::to_string(r.nx), std::to_string(r.ny)});
    // yer belgilari
    r.nodes.resize(static_cast<std::size_t>((r.nx + 1) * (r.ny + 1)));
    for (int j = 0; j <= r.ny; ++j)
        for (int i = 0; i <= r.nx; ++i) {
            auto& n = r.nodes[static_cast<std::size_t>(i + j * (r.nx + 1))];
            n.p = {r.origin.x + i * a, r.origin.y + j * a, 0};
            n.ok = eg.z(n.p.x, n.p.y, n.zg);
        }
    // kvadratlar: chegaradagi ulushi (8×8 nuqta)
    r.cells.resize(static_cast<std::size_t>(r.nx * r.ny));
    std::vector<double> frac(r.cells.size(), 0);
    for (int j = 0; j < r.ny; ++j)
        for (int i = 0; i < r.nx; ++i) {
            const auto idx = static_cast<std::size_t>(i + j * r.nx);
            const bool nodesOk = r.node(i, j).ok && r.node(i + 1, j).ok && r.node(i, j + 1).ok && r.node(i + 1, j + 1).ok;
            if (!nodesOk) continue;
            double f = 1;
            if (s.boundary.size() >= 3) {
                int in = 0;
                for (int u = 0; u < 8; ++u)
                    for (int v = 0; v < 8; ++v) in += inPoly(s.boundary, r.origin.x + (i + (u + 0.5) / 8) * a, r.origin.y + (j + (v + 0.5) / 8) * a);
                f = in / 64.0;
            }
            frac[idx] = f;
            r.cells[idx].ok = f > 0;
        }
    // balans tekisligi (gorizontal yoki berilgan nishabliklar bilan): sof hajm H0 bo'yicha chiziqli
    if (s.balance) {
        double sA = 0, sZ = 0;
        const double ix = fg.plane ? fg.ix : 0, iy = fg.plane ? fg.iy : 0;
        const Vec3 base = fg.plane ? fg.base : r.origin;
        for (int j = 0; j < r.ny; ++j)
            for (int i = 0; i < r.nx; ++i) {
                const auto idx = static_cast<std::size_t>(i + j * r.nx);
                if (!r.cells[idx].ok) continue;
                double m = 0;
                for (auto [di, dj] : {std::pair{0, 0}, {1, 0}, {0, 1}, {1, 1}}) {
                    const auto& n = r.node(i + di, j + dj);
                    m += n.zg - (ix * (n.p.x - base.x) + iy * (n.p.y - base.y));
                }
                const double A = frac[idx] * a * a;
                sA += A;
                sZ += A * m / 4;
            }
        if (sA <= 0) throw Error("terr.err.nodata", {});
        r.balanceZ = sZ / sA;
        fg.plane = true;
        fg.tin = nullptr;
        fg.z0 = r.balanceZ;
        fg.ix = ix;
        fg.iy = iy;
        fg.base = base;
    }
    if (!fg.valid()) throw Error("terr.err.nofg", {});
    for (auto& n : r.nodes) {
        if (!n.ok) continue;
        n.ok = fg.z(n.p.x, n.p.y, n.zd);
        n.h = n.zd - n.zg;
    }
    r.colFill.assign(static_cast<std::size_t>(r.nx), 0);
    r.colCut.assign(static_cast<std::size_t>(r.nx), 0);
    for (int j = 0; j < r.ny; ++j)
        for (int i = 0; i < r.nx; ++i) {
            const auto idx = static_cast<std::size_t>(i + j * r.nx);
            auto& c = r.cells[idx];
            if (!c.ok) continue;
            const auto &n00 = r.node(i, j), &n10 = r.node(i + 1, j), &n01 = r.node(i, j + 1), &n11 = r.node(i + 1, j + 1);
            if (!(n00.ok && n10.ok && n01.ok && n11.ok)) { c.ok = false; continue; }
            // to'rt uchburchakli prizma (markaz — o'rtacha)
            const double hc = (n00.h + n10.h + n01.h + n11.h) / 4, At = a * a / 4;
            const double hs[4] = {n00.h, n10.h, n11.h, n01.h};
            double fill = 0, tot = 0;
            for (int t = 0; t < 4; ++t) {
                fill += posPart(At, hs[t], hs[(t + 1) % 4], hc);
                tot += At * (hs[t] + hs[(t + 1) % 4] + hc) / 3;
            }
            c.area = frac[idx] * a * a;
            c.fill = fill * frac[idx];
            c.cut = (tot - fill) * frac[idx];   // manfiy (kesma)
            r.fill += c.fill;
            r.cut += c.cut;
            r.area += c.area;
            r.colFill[static_cast<std::size_t>(i)] += c.fill;
            r.colCut[static_cast<std::size_t>(i)] += c.cut;
        }
    // nol chizig'i: kvadratdagi to'rt uchburchakda h = 0 kesmalar, keyin zanjir
    std::vector<std::pair<Vec3, Vec3>> segs;
    for (int j = 0; j < r.ny; ++j)
        for (int i = 0; i < r.nx; ++i) {
            if (!r.at(i, j).ok) continue;
            const CartoNode* ns[4] = {&r.node(i, j), &r.node(i + 1, j), &r.node(i + 1, j + 1), &r.node(i, j + 1)};
            const double hc = (ns[0]->h + ns[1]->h + ns[2]->h + ns[3]->h) / 4;
            const Vec3 pc{r.origin.x + (i + 0.5) * a, r.origin.y + (j + 0.5) * a, 0};
            for (int t = 0; t < 4; ++t) {
                const Vec3 P3[3] = {ns[t]->p, ns[(t + 1) % 4]->p, pc};
                double H3[3] = {ns[t]->h, ns[(t + 1) % 4]->h, hc};
                for (auto& h : H3)
                    if (h == 0) h = 1e-12;
                std::vector<Vec3> cr;
                for (int e = 0; e < 3; ++e) {
                    const double h1 = H3[e], h2 = H3[(e + 1) % 3];
                    if ((h1 > 0) != (h2 > 0)) {
                        const double u = h1 / (h1 - h2);
                        cr.push_back(P3[e] + (P3[(e + 1) % 3] - P3[e]) * u);
                    }
                }
                if (cr.size() == 2) segs.push_back({cr[0], cr[1]});
            }
        }
    // zanjirlash (uchlar 1 mm aniqlikda)
    auto key = [](const Vec3& p) { return std::pair<long long, long long>{std::llround(p.x * 1000), std::llround(p.y * 1000)}; };
    std::multimap<std::pair<long long, long long>, std::size_t> ends;
    for (std::size_t k = 0; k < segs.size(); ++k) {
        ends.insert({key(segs[k].first), k});
        ends.insert({key(segs[k].second), k});
    }
    std::vector<bool> used(segs.size());
    for (int pass = 0; pass < 2; ++pass)
        for (std::size_t k = 0; k < segs.size(); ++k) {
            if (used[k]) continue;
            if (pass == 0 && ends.count(key(segs[k].first)) != 1 && ends.count(key(segs[k].second)) != 1) continue;
            std::vector<Vec3> ch;
            // ochiq uchdan boshlash
            Vec3 cur = ends.count(key(segs[k].first)) == 1 ? segs[k].first : segs[k].second;
            ch.push_back(cur);
            std::size_t sk = k;
            while (true) {
                used[sk] = true;
                const Vec3 nxt = key(segs[sk].first) == key(cur) ? segs[sk].second : segs[sk].first;
                ch.push_back(nxt);
                cur = nxt;
                std::size_t found = segs.size();
                auto rg = ends.equal_range(key(cur));
                for (auto it = rg.first; it != rg.second; ++it)
                    if (!used[it->second]) { found = it->second; break; }
                if (found == segs.size()) break;
                sk = found;
            }
            r.zero.push_back(std::move(ch));
        }
    return r;
}

Entities drawCartogram(const CartoResult& r, const CartoSpec& s, const std::function<std::string(const std::string&)>& tr) {
    Entities es;
    Out o{es};
    const double a = r.cell;
    const double th = s.textH > 0 ? s.textH : a / 12;
    // to'r: ok kvadratlarning qirralari (takrorsiz)
    std::set<std::tuple<int, int, int>> edges;   // (i, j, 0 — gorizontal / 1 — vertikal)
    for (int j = 0; j < r.ny; ++j)
        for (int i = 0; i < r.nx; ++i)
            if (r.at(i, j).ok) {
                edges.insert({i, j, 0});
                edges.insert({i, j + 1, 0});
                edges.insert({i, j, 1});
                edges.insert({i + 1, j, 1});
            }
    for (const auto& [i, j, d] : edges) {
        const Vec3 p0{r.origin.x + i * a, r.origin.y + j * a, 0};
        o.line(p0, d == 0 ? p0 + Vec3{a, 0, 0} : p0 + Vec3{0, a, 0}, "KARTOGRAMMA_TOR", 8);
    }
    // tugunlar: ishchi belgi (chap yuqori), loyiha (o'ng yuqori), yer (o'ng past)
    std::vector<bool> used(r.nodes.size());
    for (int j = 0; j < r.ny; ++j)
        for (int i = 0; i < r.nx; ++i)
            if (r.at(i, j).ok)
                for (auto [di, dj] : {std::pair{0, 0}, {1, 0}, {0, 1}, {1, 1}}) used[static_cast<std::size_t>(i + di + (j + dj) * (r.nx + 1))] = true;
    for (std::size_t k = 0; k < r.nodes.size(); ++k) {
        if (!used[k]) continue;
        const auto& n = r.nodes[k];
        const double h = n.h;
        o.text(n.p + Vec3{-th * 0.25, th * 0.3, 0}, th * 0.8, (h >= 0 ? "+" : "") + comma(f2(h)), "KARTOGRAMMA_ISHCHI", h >= 0 ? 1 : 5, 0, TextHAlign::Right,
               TextVAlign::Bottom);
        o.text(n.p + Vec3{th * 0.25, th * 0.3, 0}, th * 0.75, comma(f2(n.zd)), "KARTOGRAMMA_LOYIHA", 1, 0, TextHAlign::Left, TextVAlign::Bottom);
        o.text(n.p + Vec3{th * 0.25, -th * 0.3, 0}, th * 0.75, comma(f2(n.zg)), "KARTOGRAMMA_YER", kColorByLayer, 0, TextHAlign::Left, TextVAlign::Top);
    }
    // kvadrat hajmlari
    for (int j = 0; j < r.ny; ++j)
        for (int i = 0; i < r.nx; ++i) {
            const auto& c = r.at(i, j);
            if (!c.ok) continue;
            const Vec3 pc{r.origin.x + (i + 0.5) * a, r.origin.y + (j + 0.5) * a, 0};
            const bool f = c.fill > 0.05, k = c.cut < -0.05;
            if (f && k) {
                o.text(pc + Vec3{0, th * 0.15, 0}, th, "+" + comma(std::format("{:.1f}", c.fill)), "KARTOGRAMMA_HAJM", 1, 0, TextHAlign::Center, TextVAlign::Bottom);
                o.text(pc - Vec3{0, th * 0.15, 0}, th, comma(std::format("{:.1f}", c.cut)), "KARTOGRAMMA_HAJM", 5, 0, TextHAlign::Center, TextVAlign::Top);
            } else {
                const double v = f ? c.fill : c.cut;
                o.text(pc, th, (v > 0 ? "+" : "") + comma(std::format("{:.1f}", v)), "KARTOGRAMMA_HAJM", v >= 0 ? 1 : 5, 0, TextHAlign::Center, TextVAlign::Middle);
            }
        }
    // nol chizig'i
    for (const auto& z : r.zero) o.poly(z, false, "KARTOGRAMMA_NOL", 3);
    // ustun raqamlari va yig'indilar
    int jmax = -1;
    for (int j = 0; j < r.ny; ++j)
        for (int i = 0; i < r.nx; ++i)
            if (r.at(i, j).ok) jmax = std::max(jmax, j);
    const double yTop = r.origin.y + (jmax + 1) * a + th * 2.5;
    const double yb = r.origin.y - th * 3, rh = th * 2.2;
    o.text({r.origin.x - th * 0.5, yb - rh * 0.5, 0}, th * 0.9, tr("terr.carto.fill"), "KARTOGRAMMA_JADVAL", 1, 0, TextHAlign::Right, TextVAlign::Middle);
    o.text({r.origin.x - th * 0.5, yb - rh * 1.5, 0}, th * 0.9, tr("terr.carto.cut"), "KARTOGRAMMA_JADVAL", 5, 0, TextHAlign::Right, TextVAlign::Middle);
    for (int i = 0; i < r.nx; ++i) {
        const double x = r.origin.x + (i + 0.5) * a;
        o.text({x, yTop, 0}, th, std::to_string(i + 1), "KARTOGRAMMA_JADVAL", kColorByLayer, 0, TextHAlign::Center, TextVAlign::Bottom);
        o.text({x, yb - rh * 0.5, 0}, th * 0.9, "+" + comma(std::format("{:.1f}", r.colFill[static_cast<std::size_t>(i)])), "KARTOGRAMMA_JADVAL", 1, 0,
               TextHAlign::Center, TextVAlign::Middle);
        o.text({x, yb - rh * 1.5, 0}, th * 0.9, comma(std::format("{:.1f}", r.colCut[static_cast<std::size_t>(i)])), "KARTOGRAMMA_JADVAL", 5, 0, TextHAlign::Center,
               TextVAlign::Middle);
    }
    for (int k = 0; k <= 2; ++k) o.line({r.origin.x, yb - rh * k, 0}, {r.origin.x + r.nx * a, yb - rh * k, 0}, "KARTOGRAMMA_JADVAL", 8);
    for (int i = 0; i <= r.nx; ++i) o.line({r.origin.x + i * a, yb, 0}, {r.origin.x + i * a, yb - 2 * rh, 0}, "KARTOGRAMMA_JADVAL", 8);
    const double ys = yb - rh * 2 - th * 2;
    o.text({r.origin.x, ys, 0}, th * 1.1,
           tr("terr.carto.total") + "  +" + comma(std::format("{:.1f}", r.fill)) + "  /  " + comma(std::format("{:.1f}", r.cut)) + " m\xC2\xB3;   " + tr("terr.carto.balance") +
               " " + comma(std::format("{:+.1f}", std::abs(r.fill + r.cut) < 0.05 ? 0.0 : r.fill + r.cut)) + " m\xC2\xB3;   S = " + comma(std::format("{:.1f}", r.area)) + " m\xC2\xB2",
           "KARTOGRAMMA_JADVAL", kColorByLayer, 0, TextHAlign::Left, TextVAlign::Top);
    if (!std::isnan(r.balanceZ))
        o.text({r.origin.x, ys - th * 1.8, 0}, th * 1.1, tr("terr.carto.h0") + " " + comma(f3(r.balanceZ)), "KARTOGRAMMA_JADVAL", kColorByLayer, 0, TextHAlign::Left,
               TextVAlign::Top);
    return es;
}

VolumeResult volumeBetween(const Tin& eg, Design fg, const std::vector<Vec3>& boundary, double cell, bool balance) {
    VolumeResult v;
    if (eg.empty()) throw Error("terr.err.noeg", {});
    BBox b;
    if (boundary.size() >= 3)
        for (const auto& q : boundary) b.expand(q);
    else b = eg.bounds();
    const double area = std::max(1e-6, (b.max.x - b.min.x) * (b.max.y - b.min.y));
    v.cell = cell > 0 ? cell : std::max(0.1, std::sqrt(area / 90000));
    CartoSpec s;
    s.cell = v.cell;
    s.boundary = boundary;
    s.balance = balance;
    s.origin = Vec3{b.min.x, b.min.y, 0};
    const auto r = cartogram(eg, fg, s);
    v.fill = r.fill;
    v.cut = r.cut;
    v.area = r.area;
    v.balanceZ = r.balanceZ;
    return v;
}

// ---------------------------------------------------------------- Excel
xlsx::Workbook profileWorkbook(const std::vector<ProfilePoint>& pr, const ProfileSpec& p, const std::function<std::string(const std::string&)>& tr) {
    using C = xlsx::Cell;
    xlsx::Workbook wb;
    auto& s = wb.add(tr("terr.prof.title").substr(0, 31));
    s.add({C::s(tr("terr.col.pk"), 1), C::s(tr("terr.col.s"), 1), C::s(tr("terr.prof.ground"), 1), C::s(tr("terr.prof.design"), 1), C::s(tr("terr.prof.work"), 1)});
    s.widths = {14, 10, 12, 12, 12};
    for (const auto& q : pr) {
        if (!q.station) continue;
        std::vector<C> r{C::s(pk(p.pk0 + q.s)), C::n(q.s, 2), q.hasG ? C::n(q.zg, 3) : C::s(""), q.hasD ? C::n(q.zd, 3) : C::s("")};
        if (q.hasG && q.hasD) r.push_back(C::n(q.zd - q.zg, 3));
        s.add(r);
    }
    return wb;
}

xlsx::Workbook sectionsWorkbook(const std::vector<XSection>& xs, const SectionVolumes& v, const SectionSpec& p, const std::function<std::string(const std::string&)>& tr) {
    using C = xlsx::Cell;
    xlsx::Workbook wb;
    auto& s = wb.add(tr("terr.sec.table").substr(0, 31));
    s.add({C::s(tr("terr.col.pk"), 1), C::s(tr("terr.col.fillA"), 1), C::s(tr("terr.col.cutA"), 1), C::s(tr("terr.col.len"), 1), C::s(tr("terr.col.fillV"), 1),
           C::s(tr("terr.col.cutV"), 1)});
    s.widths = {14, 14, 14, 10, 14, 14};
    for (std::size_t i = 0; i < xs.size(); ++i) {
        std::vector<C> r{C::s(pk(p.pk0 + xs[i].s)), C::n(xs[i].fill, 2), C::n(xs[i].cut, 2)};
        if (i + 1 < xs.size()) {
            // formulalar: (F1+F2)/2·L — Excel o'zi qayta hisoblaydi
            const int row = static_cast<int>(i) + 1;   // 0-asosli qator (sarlavhadan keyin)
            r.push_back(C::n(xs[i + 1].s - xs[i].s, 2));
            r.push_back(C::f(std::format("({}+{})/2*{}", xlsx::ref(row, 1), xlsx::ref(row + 1, 1), xlsx::ref(row, 3)), v.fillV[i], 2));
            r.push_back(C::f(std::format("({}+{})/2*{}", xlsx::ref(row, 2), xlsx::ref(row + 1, 2), xlsx::ref(row, 3)), v.cutV[i], 2));
        }
        s.add(r);
    }
    const int last = static_cast<int>(xs.size());
    s.add({C::s(tr("terr.total"), 4), C::s(""), C::s(""), C::f(std::format("SUM(D2:D{})", last + 1), xs.back().s - xs.front().s, 4),
           C::f(std::format("SUM(E2:E{})", last + 1), v.fill, 4), C::f(std::format("SUM(F2:F{})", last + 1), v.cut, 4)});
    // kesim nuqtalari
    auto& d = wb.add(tr("terr.sheet.points").substr(0, 31));
    d.add({C::s(tr("terr.col.pk"), 1), C::s(tr("terr.col.off"), 1), C::s(tr("terr.prof.ground"), 1), C::s(tr("terr.prof.design"), 1)});
    d.widths = {14, 10, 12, 12};
    for (const auto& x : xs) {
        std::set<double> offs;
        for (const auto& a : x.g) offs.insert(a.first);
        for (const auto& a : x.d) offs.insert(a.first);
        for (double off : offs) {
            bool a, b;
            const double g = interp(x.g, off, a), z = interp(x.d, off, b);
            d.add({C::s(pk(p.pk0 + x.s)), C::n(off, 2), a ? C::n(g, 3) : C::s(""), b ? C::n(z, 3) : C::s("")});
        }
    }
    return wb;
}

xlsx::Workbook cartogramWorkbook(const CartoResult& r, const std::function<std::string(const std::string&)>& tr) {
    using C = xlsx::Cell;
    xlsx::Workbook wb;
    auto& n = wb.add(tr("terr.sheet.nodes").substr(0, 31));
    n.add({C::s("i", 1), C::s("j", 1), C::s("X", 1), C::s("Y", 1), C::s(tr("terr.prof.ground"), 1), C::s(tr("terr.prof.design"), 1), C::s(tr("terr.prof.work"), 1)});
    n.widths = {6, 6, 14, 14, 12, 12, 12};
    for (int j = 0; j <= r.ny; ++j)
        for (int i = 0; i <= r.nx; ++i) {
            const auto& q = r.node(i, j);
            if (!q.ok) continue;
            n.add({C::n(i + 1, 5), C::n(j + 1, 5), C::n(q.p.x, 3), C::n(q.p.y, 3), C::n(q.zg, 3), C::n(q.zd, 3), C::n(q.h, 3)});
        }
    auto& c = wb.add(tr("terr.sheet.cells").substr(0, 31));
    c.add({C::s("i", 1), C::s("j", 1), C::s(tr("terr.col.area"), 1), C::s(tr("terr.col.fillV"), 1), C::s(tr("terr.col.cutV"), 1)});
    c.widths = {6, 6, 12, 14, 14};
    for (int j = 0; j < r.ny; ++j)
        for (int i = 0; i < r.nx; ++i) {
            const auto& q = r.at(i, j);
            if (q.ok) c.add({C::n(i + 1, 5), C::n(j + 1, 5), C::n(q.area, 2), C::n(q.fill, 2), C::n(q.cut, 2)});
        }
    auto& u = wb.add(tr("terr.sheet.cols").substr(0, 31));
    u.add({C::s(tr("terr.col.col"), 1), C::s(tr("terr.col.fillV"), 1), C::s(tr("terr.col.cutV"), 1)});
    u.widths = {10, 14, 14};
    for (int i = 0; i < r.nx; ++i) u.add({C::n(i + 1, 5), C::n(r.colFill[static_cast<std::size_t>(i)], 2), C::n(r.colCut[static_cast<std::size_t>(i)], 2)});
    u.add({C::s(tr("terr.total"), 4), C::f(std::format("SUM(B2:B{})", r.nx + 1), r.fill, 4), C::f(std::format("SUM(C2:C{})", r.nx + 1), r.cut, 4)});
    auto& x = wb.add(tr("terr.sheet.summary").substr(0, 31));
    x.autoFilter = false;
    x.widths = {34, 16};
    x.add({C::s(tr("terr.carto.cell"), 1), C::n(r.cell, 2)});
    x.add({C::s(tr("terr.col.area"), 1), C::n(r.area, 2)});
    x.add({C::s(tr("terr.carto.fill"), 1), C::n(r.fill, 2)});
    x.add({C::s(tr("terr.carto.cut"), 1), C::n(r.cut, 2)});
    x.add({C::s(tr("terr.carto.balance"), 1), C::n(r.fill + r.cut, 2)});
    if (!std::isnan(r.balanceZ)) x.add({C::s(tr("terr.carto.h0"), 1), C::n(r.balanceZ, 3)});
    return wb;
}

} // namespace asiacad::civil
