#include <asiacad/core/draft.h>

#include <asiacad/core/error.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace asiacad {

namespace {
constexpr double kTol = 1e-9;
double cross2(const Vec3& a, const Vec3& b) { return a.x * b.y - a.y * b.x; }
Vec3 unit2(const Vec3& v) {
    const double l = v.length2d();
    return l < 1e-300 ? Vec3{} : Vec3{v.x / l, v.y / l, 0};
}
template <class T>
std::unique_ptr<T> like(std::unique_ptr<T> e, const IEntity& src) {
    e->setLayer(src.layer());
    e->setColor(src.color());
    return e;
}

// ---- poliliniya segmentlari (to'g'ri yoki bulge yoy) — uzunlik va nuqta
struct Seg {
    Vec3 a, b;
    double bulge = 0;
    double length() const {
        const double c = (b - a).length2d();
        if (std::abs(bulge) < 1e-12 || c < 1e-15) return c;
        const double th = 4 * std::atan(std::abs(bulge));
        return c / (2 * std::sin(th / 2)) * th;
    }
    // f ∈ [0,1] — yoy uzunligi bo'yicha
    Vec3 at(double f) const {
        if (std::abs(bulge) < 1e-12) return a + (b - a) * f;
        const double th = 4 * std::atan(bulge);
        const double c = (b - a).length2d();
        const double r = c / (2 * std::sin(std::abs(th) / 2));
        const Vec3 m = (a + b) * 0.5;
        const Vec3 u = unit2(b - a);
        const Vec3 n{-u.y, u.x, 0};
        const double h = c / 2 * (1 - bulge * bulge) / (2 * bulge);  // markazgacha (chap normal bo'yicha, ishorali)
        const Vec3 cen = m + n * h;
        const double a0 = std::atan2(a.y - cen.y, a.x - cen.x);
        const double ang = a0 + th * f;
        return {cen.x + r * std::cos(ang), cen.y + r * std::sin(ang), a.z + (b.z - a.z) * f};
    }
    // [f0, f1] qismining bulge'i
    double subBulge(double f0, double f1) const {
        if (std::abs(bulge) < 1e-12) return 0;
        const double th = 4 * std::atan(bulge) * (f1 - f0);
        return std::tan(th / 4);
    }
};

std::vector<Seg> segsOf(const PolylineEntity& p) {
    std::vector<Seg> s;
    const auto& v = p.vertices();
    const auto& bl = p.bulges();
    const std::size_t n = v.size();
    if (n < 2) return s;
    const std::size_t m = p.isClosed() ? n : n - 1;
    for (std::size_t i = 0; i < m; ++i) s.push_back({v[i], v[(i + 1) % n], i < bl.size() ? bl[i] : 0});
    return s;
}

// [s0, s1] (uzunlik bo'yicha) qism — yangi ochiq poliliniya
std::unique_ptr<PolylineEntity> subPath(const std::vector<Seg>& segs, double s0, double s1) {
    std::vector<Vec3> pts;
    std::vector<double> bl;
    double acc = 0;
    for (const auto& sg : segs) {
        const double L = sg.length();
        const double a = acc, b = acc + L;
        acc = b;
        if (b <= s0 + kTol || a >= s1 - kTol || L < 1e-15) continue;
        const double f0 = std::clamp((s0 - a) / L, 0.0, 1.0), f1 = std::clamp((s1 - a) / L, 0.0, 1.0);
        const Vec3 p0 = sg.at(f0), p1 = sg.at(f1);
        if (pts.empty() || pts.back().distanceTo(p0) > 1e-9) {
            pts.push_back(p0);
            bl.push_back(0);
        }
        bl.back() = sg.subBulge(f0, f1);
        pts.push_back(p1);
        bl.push_back(0);
    }
    if (pts.size() < 2) return nullptr;
    return std::make_unique<PolylineEntity>(std::move(pts), std::move(bl), false);
}

double totalLen(const std::vector<Seg>& s) {
    double L = 0;
    for (const auto& x : s) L += x.length();
    return L;
}
// nuqtaning yo'l bo'ylab masofasi (eng yaqin)
double stationOf(const std::vector<Seg>& segs, const Vec3& p) {
    double best = 1e300, bestS = 0, acc = 0;
    for (const auto& sg : segs) {
        const double L = sg.length();
        const int n = std::abs(sg.bulge) < 1e-12 ? 1 : 64;
        if (n == 1) {
            const Vec3 d = sg.b - sg.a;
            const double l2 = d.x * d.x + d.y * d.y;
            const double t = l2 > 0 ? std::clamp(((p.x - sg.a.x) * d.x + (p.y - sg.a.y) * d.y) / l2, 0.0, 1.0) : 0;
            const double dd = (sg.at(t) - p).length2d();
            if (dd < best) { best = dd; bestS = acc + t * L; }
        } else {
            // yoy: ikki bosqichli qidiruv
            double bt = 0, bd = 1e300;
            for (int k = 0; k <= n; ++k) {
                const double t = static_cast<double>(k) / n;
                const double dd = (sg.at(t) - p).length2d();
                if (dd < bd) { bd = dd; bt = t; }
            }
            double lo = std::max(0.0, bt - 1.0 / n), hi = std::min(1.0, bt + 1.0 / n);
            for (int it = 0; it < 40; ++it) {
                const double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3;
                if ((sg.at(m1) - p).length2d() < (sg.at(m2) - p).length2d()) hi = m2;
                else lo = m1;
            }
            const double t = (lo + hi) / 2, dd = (sg.at(t) - p).length2d();
            if (dd < best) { best = dd; bestS = acc + t * L; }
        }
        acc += L;
    }
    return bestS;
}

std::vector<Seg> segsOfCurve(const IEntity& e, bool& closed) {
    closed = false;
    if (auto* l = dynamic_cast<const LineEntity*>(&e)) return {{l->startPoint(), l->endPoint(), 0}};
    if (auto* a = dynamic_cast<const ArcEntity*>(&e)) return {{a->startPoint(), a->endPoint(), std::tan(a->sweep() / 4)}};
    if (auto* p = dynamic_cast<const PolylineEntity*>(&e)) {
        closed = p->isClosed();
        return segsOf(*p);
    }
    if (auto* c = dynamic_cast<const CircleEntity*>(&e)) {
        closed = true;
        const Vec3 o = c->center();
        const double r = c->radius();
        return {{{o.x + r, o.y, o.z}, {o.x - r, o.y, o.z}, 1.0}, {{o.x - r, o.y, o.z}, {o.x + r, o.y, o.z}, 1.0}};
    }
    throw Error("err.modify_unsupported", {std::string(toString(e.type()))});
}
}  // namespace

// ---------------------------------------------------------------- Draw
std::unique_ptr<PolylineEntity> makeRectangle(const Vec3& p1, const Vec3& p2, double r, double ch) {
    const double x0 = std::min(p1.x, p2.x), x1 = std::max(p1.x, p2.x), y0 = std::min(p1.y, p2.y), y1 = std::max(p1.y, p2.y);
    const double w = x1 - x0, h = y1 - y0, z = p1.z;
    if (w < kTol || h < kTol) throw Error("err.rect_size");
    const double k = std::max(r, ch);
    if (k * 2 > std::min(w, h) + 1e-9) throw Error("err.rect_corner");
    if (k <= 0) return std::make_unique<PolylineEntity>(std::vector<Vec3>{{x0, y0, z}, {x1, y0, z}, {x1, y1, z}, {x0, y1, z}}, true);
    const double b = r > 0 ? std::tan(kPi / 8) : 0;  // 90° yoy
    std::vector<Vec3> v{{x0 + k, y0, z}, {x1 - k, y0, z}, {x1, y0 + k, z}, {x1, y1 - k, z}, {x1 - k, y1, z}, {x0 + k, y1, z}, {x0, y1 - k, z}, {x0, y0 + k, z}};
    std::vector<double> bl{0, b, 0, b, 0, b, 0, b};
    return std::make_unique<PolylineEntity>(std::move(v), std::move(bl), true);
}

std::unique_ptr<PolylineEntity> makePolygon(const Vec3& c, int n, double radius, bool inscribed, double rot) {
    if (n < 3 || n > 1024) throw Error("err.polygon_sides");
    if (radius <= 0) throw Error("err.radius_positive");
    const double R = inscribed ? radius : radius / std::cos(kPi / n);
    // tashqi (tomongacha) holatda — tomon o'rtasi rot yo'nalishida bo'lsin: uchlar yarim qadam siljiydi
    const double a0 = inscribed ? rot : rot + kPi / n;
    std::vector<Vec3> v;
    for (int i = 0; i < n; ++i) v.push_back({c.x + R * std::cos(a0 + 2 * kPi * i / n), c.y + R * std::sin(a0 + 2 * kPi * i / n), c.z});
    return std::make_unique<PolylineEntity>(std::move(v), true);
}

std::unique_ptr<PolylineEntity> makeEllipse(const Vec3& c, double a, double b, double rot, int segs) {
    if (a <= 0 || b <= 0) throw Error("err.radius_positive");
    segs = std::clamp(segs, 16, 4096);
    std::vector<Vec3> v;
    const double cs = std::cos(rot), sn = std::sin(rot);
    for (int i = 0; i < segs; ++i) {
        const double t = 2 * kPi * i / segs;
        const double x = a * std::cos(t), y = b * std::sin(t);
        v.push_back({c.x + x * cs - y * sn, c.y + x * sn + y * cs, c.z});
    }
    return std::make_unique<PolylineEntity>(std::move(v), true);
}

std::unique_ptr<PolylineEntity> makeSpline(const std::vector<Vec3>& fit0, bool closed, int per) {
    std::vector<Vec3> fit;
    for (const auto& p : fit0)
        if (fit.empty() || fit.back().distanceTo(p) > kTol) fit.push_back(p);
    if (closed && fit.size() > 2 && fit.front().distanceTo(fit.back()) < kTol) fit.pop_back();
    if (fit.size() < 2) throw Error("err.spline_points");
    if (fit.size() == 2) return std::make_unique<PolylineEntity>(fit, false);
    const std::size_t n = fit.size();
    auto P = [&](long i) -> Vec3 {
        if (closed) return fit[static_cast<std::size_t>((i % static_cast<long>(n) + static_cast<long>(n)) % static_cast<long>(n))];
        if (i < 0) return fit[0] * 2.0 - fit[1];
        if (i >= static_cast<long>(n)) return fit[n - 1] * 2.0 - fit[n - 2];
        return fit[static_cast<std::size_t>(i)];
    };
    std::vector<Vec3> out;
    const long spans = closed ? static_cast<long>(n) : static_cast<long>(n) - 1;
    for (long i = 0; i < spans; ++i) {
        const Vec3 p0 = P(i - 1), p1 = P(i), p2 = P(i + 1), p3 = P(i + 2);
        // centripetal parametrlash (alpha = 0.5) — o'z-o'zini kesmaydi, keskin burilishda ham yaxshi
        auto tj = [](double ti, const Vec3& a, const Vec3& b) { return ti + std::sqrt(std::max(1e-12, a.distanceTo(b))); };
        const double t0 = 0, t1 = tj(t0, p0, p1), t2 = tj(t1, p1, p2), t3 = tj(t2, p2, p3);
        for (int k = 0; k < per; ++k) {
            const double t = t1 + (t2 - t1) * k / per;
            const Vec3 a1 = p0 * ((t1 - t) / (t1 - t0)) + p1 * ((t - t0) / (t1 - t0));
            const Vec3 a2 = p1 * ((t2 - t) / (t2 - t1)) + p2 * ((t - t1) / (t2 - t1));
            const Vec3 a3 = p2 * ((t3 - t) / (t3 - t2)) + p3 * ((t - t2) / (t3 - t2));
            const Vec3 b1 = a1 * ((t2 - t) / (t2 - t0)) + a2 * ((t - t0) / (t2 - t0));
            const Vec3 b2 = a2 * ((t3 - t) / (t3 - t1)) + a3 * ((t - t1) / (t3 - t1));
            out.push_back(b1 * ((t2 - t) / (t2 - t1)) + b2 * ((t - t1) / (t2 - t1)));
        }
    }
    if (!closed) out.push_back(fit.back());
    return std::make_unique<PolylineEntity>(std::move(out), closed);
}

// ---------------------------------------------------------------- Modify
std::unique_ptr<IEntity> stretchEntity(const IEntity& e, const Vec3& lo0, const Vec3& hi0, const Vec3& d) {
    const Vec3 lo{std::min(lo0.x, hi0.x), std::min(lo0.y, hi0.y), 0}, hi{std::max(lo0.x, hi0.x), std::max(lo0.y, hi0.y), 0};
    auto in = [&](const Vec3& p) { return p.x >= lo.x - kTol && p.x <= hi.x + kTol && p.y >= lo.y - kTol && p.y <= hi.y + kTol; };
    const BBox b = e.bbox();
    if (!b.valid()) return nullptr;
    if (in(b.min) && in(b.max)) {  // butunlay ichida — siljitish
        auto c = e.clone();
        c->transform(Transform::translation(d));
        return c;
    }
    if (auto* l = dynamic_cast<const LineEntity*>(&e)) {
        const bool a = in(l->startPoint()), bb = in(l->endPoint());
        if (!a && !bb) return nullptr;
        return like(std::make_unique<LineEntity>(l->startPoint() + (a ? d : Vec3{}), l->endPoint() + (bb ? d : Vec3{})), e);
    }
    if (auto* p = dynamic_cast<const PolylineEntity*>(&e)) {
        auto v = p->vertices();
        bool any = false;
        for (auto& q : v)
            if (in(q)) { q = q + d; any = true; }
        if (!any) return nullptr;
        return like(std::make_unique<PolylineEntity>(std::move(v), p->bulges(), p->isClosed()), e);
    }
    if (auto* a = dynamic_cast<const ArcEntity*>(&e)) {
        // AutoCAD kabi: bir uchi oynada — uch nuqta orqali qayta quriladi (o'rtasi d/2 ga)
        const bool s = in(a->startPoint()), t = in(a->endPoint());
        if (!s && !t) return nullptr;
        const Vec3 p0 = a->startPoint() + (s ? d : Vec3{}), p2 = a->endPoint() + (t ? d : Vec3{});
        const Vec3 pm = a->pointAtDistance(a->length() / 2) + d * 0.5;
        try {
            return like(std::make_unique<ArcEntity>(ArcEntity::fromThreePoints(p0, pm, p2)), e);
        } catch (...) {
            return like(std::make_unique<LineEntity>(p0, p2), e);
        }
    }
    return nullptr;  // aylana, matn, blok — faqat butunlay ichida bo'lsa
}

std::vector<Transform> arrayRect(int rows, int cols, double dx, double dy) {
    if (rows < 1 || cols < 1 || rows * cols > 100000) throw Error("err.count_min");
    std::vector<Transform> t;
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < cols; ++c)
            if (r || c) t.push_back(Transform::translation({c * dx, r * dy, 0}));
    return t;
}

std::vector<Transform> arrayPolar(const Vec3& c, int n, double fill, bool rot, const Vec3& base) {
    if (n < 2 || n > 100000) throw Error("err.count_min");
    std::vector<Transform> t;
    const bool full = std::abs(std::abs(fill) - 360) < 1e-9;
    const double step = deg2rad(fill) / (full ? n : n - 1);
    for (int i = 1; i < n; ++i) {
        const Transform r = Transform::rotation(c, step * i);
        // burilmasin: tayanch nuqta aylana bo'ylab ko'chadi, obyekt o'z yo'nalishini saqlaydi
        t.push_back(rot ? r : Transform::translation(r.apply(base) - base));
    }
    return t;
}

std::vector<Transform> arrayPath(const IEntity& path, const Vec3& base, int count, double spacing, bool align) {
    bool closed = false;
    const auto segs = segsOfCurve(path, closed);
    const double L = totalLen(segs);
    if (L <= kTol) throw Error("err.count_min");
    std::vector<double> ss;
    if (spacing > 0) {
        for (double s = 0; s <= L + 1e-9 && ss.size() < 100000; s += spacing) ss.push_back(s);
    } else {
        if (count < 2) throw Error("err.count_min");
        const int div = closed ? count : count - 1;
        for (int i = 0; i < count; ++i) ss.push_back(L * i / div);
    }
    auto at = [&](double s, Vec3& p, double& ang) {
        double acc = 0;
        for (const auto& sg : segs) {
            const double l = sg.length();
            if (s <= acc + l + 1e-12 || &sg == &segs.back()) {
                const double f = l > 0 ? std::clamp((s - acc) / l, 0.0, 1.0) : 0;
                p = sg.at(f);
                const Vec3 q = sg.at(std::min(1.0, f + 1e-4)), r = sg.at(std::max(0.0, f - 1e-4));
                ang = std::atan2(q.y - r.y, q.x - r.x);
                return;
            }
            acc += l;
        }
    };
    Vec3 p0;
    double a0 = 0;
    at(0, p0, a0);
    std::vector<Transform> t;
    for (double s : ss) {
        Vec3 p;
        double a = 0;
        at(s, p, a);
        // tayanch nuqta yo'l nuqtasiga; align — boshlang'ich yo'nalishga nisbatan burilish
        Transform tr = Transform::translation(p - base);
        if (align) tr = Transform::rotation(p, a - a0) * tr;
        if (s > 1e-9 || p.distanceTo(base) > 1e-9) t.push_back(tr);
    }
    return t;
}

ChamferResult chamferLines(const LineEntity& l1, const Vec3& pick1, const LineEntity& l2, const Vec3& pick2, double d1, double d2) {
    if (d1 < 0 || d2 < 0) throw Error("err.chamfer_dist");
    const Vec3 a1 = l1.startPoint(), b1 = l1.endPoint(), a2 = l2.startPoint(), b2 = l2.endPoint();
    const Vec3 v1 = b1 - a1, v2 = b2 - a2;
    const double den = cross2(v1, v2);
    if (std::abs(den) <= 1e-12 * v1.length2d() * v2.length2d()) throw Error("err.fillet_parallel");
    const Vec3 x = a1 + v1 * (cross2(a2 - a1, v2) / den);
    auto side = [&](const Vec3& a, const Vec3& b, const Vec3& pick, Vec3& far) {
        Vec3 u = unit2(b - a);
        if ((pick - x).dot(u) < 0) u = u * -1.0;
        far = (a - x).dot(u) >= (b - x).dot(u) ? a : b;
        return u;
    };
    Vec3 f1, f2;
    const Vec3 u1 = side(a1, b1, pick1, f1), u2 = side(a2, b2, pick2, f2);
    if (d1 > (f1 - x).dot(u1) + 1e-9 || d2 > (f2 - x).dot(u2) + 1e-9) throw Error("err.chamfer_dist");
    const Vec3 t1 = x + u1 * d1, t2 = x + u2 * d2;
    ChamferResult r;
    r.first = like(std::make_unique<LineEntity>(f1, t1), l1);
    r.second = like(std::make_unique<LineEntity>(f2, t2), l2);
    if (t1.distanceTo(t2) > kTol) r.bevel = like(std::make_unique<LineEntity>(t1, t2), l1);
    return r;
}

std::unique_ptr<PolylineEntity> joinCurves(const std::vector<const IEntity*>& parts, double tol) {
    struct Piece { std::vector<Seg> s; bool used = false; };
    std::vector<Piece> ps;
    for (const IEntity* e : parts) {
        bool closed = false;
        auto s = segsOfCurve(*e, closed);
        if (closed || s.empty()) continue;
        ps.push_back({s});
    }
    if (ps.empty()) return nullptr;
    auto reversed = [](std::vector<Seg> s) {
        std::reverse(s.begin(), s.end());
        for (auto& x : s) { std::swap(x.a, x.b); x.bulge = -x.bulge; }
        return s;
    };
    std::vector<Seg> chain = ps[0].s;
    ps[0].used = true;
    for (bool grew = true; grew;) {
        grew = false;
        for (auto& p : ps) {
            if (p.used) continue;
            const Vec3 h = chain.front().a, t = chain.back().b;
            if (p.s.front().a.distanceTo(t) <= tol) chain.insert(chain.end(), p.s.begin(), p.s.end());
            else if (p.s.back().b.distanceTo(t) <= tol) { auto r = reversed(p.s); chain.insert(chain.end(), r.begin(), r.end()); }
            else if (p.s.back().b.distanceTo(h) <= tol) chain.insert(chain.begin(), p.s.begin(), p.s.end());
            else if (p.s.front().a.distanceTo(h) <= tol) { auto r = reversed(p.s); chain.insert(chain.begin(), r.begin(), r.end()); }
            else continue;
            p.used = grew = true;
        }
    }
    for (const auto& p : ps)
        if (!p.used) return nullptr;  // bitta zanjirga sig'madi
    const bool closed = chain.size() > 1 && chain.front().a.distanceTo(chain.back().b) <= tol;
    std::vector<Vec3> v;
    std::vector<double> bl;
    for (const auto& s : chain) { v.push_back(s.a); bl.push_back(s.bulge); }
    if (!closed) { v.push_back(chain.back().b); bl.push_back(0); }
    auto out = std::make_unique<PolylineEntity>(std::move(v), std::move(bl), closed);
    out->setLayer(parts.front()->layer());
    out->setColor(parts.front()->color());
    return out;
}

EntityList breakCurve(const IEntity& e, const Vec3& p1, const Vec3& p2) {
    bool closed = false;
    const auto segs = segsOfCurve(e, closed);
    const double L = totalLen(segs);
    double s1 = stationOf(segs, p1), s2 = stationOf(segs, p2);
    EntityList out;
    auto push = [&](std::unique_ptr<PolylineEntity> p) {
        if (!p) return;
        // bitta segment — LINE/ARC sifatida qaytariladi (manba turi saqlansin)
        if (p->vertices().size() == 2) {
            const double b = p->bulges().empty() ? 0 : p->bulges()[0];
            if (std::abs(b) < 1e-12) { out.push_back(like(std::make_unique<LineEntity>(p->vertices()[0], p->vertices()[1]), e)); return; }
            const Seg sg{p->vertices()[0], p->vertices()[1], b};
            out.push_back(like(std::make_unique<ArcEntity>(ArcEntity::fromThreePoints(sg.a, sg.at(0.5), sg.b)), e));
            return;
        }
        out.push_back(like(std::move(p), e));
    };
    if (!closed) {
        if (s1 > s2) std::swap(s1, s2);
        if (s2 - s1 < kTol) {  // nuqtada bo'lish
            if (s1 <= kTol || s1 >= L - kTol) throw Error("err.break_end");
            push(subPath(segs, 0, s1));
            push(subPath(segs, s1, L));
            return out;
        }
        if (s1 > kTol) push(subPath(segs, 0, s1));
        if (s2 < L - kTol) push(subPath(segs, s2, L));
        return out;
    }
    // yopiq: p1 dan p2 gacha (yo'l yo'nalishida) olib tashlanadi, qolgani — bitta ochiq bo'lak
    if (std::abs(s2 - s1) < kTol) throw Error("err.break_closed");
    std::vector<Seg> twice = segs;
    twice.insert(twice.end(), segs.begin(), segs.end());
    const double from = s2, to = s1 < s2 ? s1 + L : s1;
    push(subPath(twice, from, to));
    return out;
}

std::unique_ptr<IEntity> lengthenCurve(const IEntity& e, const Vec3& pick, double delta) {
    if (auto* l = dynamic_cast<const LineEntity*>(&e)) {
        const Vec3 a = l->startPoint(), b = l->endPoint();
        const bool endB = pick.distanceTo(b) <= pick.distanceTo(a);
        const Vec3 u = unit2(b - a);
        const double L = (b - a).length2d();
        if (L + delta <= kTol) throw Error("err.lengthen");
        return like(endB ? std::make_unique<LineEntity>(a, b + u * delta) : std::make_unique<LineEntity>(a - u * delta, b), e);
    }
    if (auto* ar = dynamic_cast<const ArcEntity*>(&e)) {
        const double dth = delta / ar->radius();
        if (ar->sweep() + dth <= 1e-9 || ar->sweep() + dth >= 2 * kPi) throw Error("err.lengthen");
        const bool endB = pick.distanceTo(ar->endPoint()) <= pick.distanceTo(ar->startPoint());
        return like(endB ? std::make_unique<ArcEntity>(ar->center(), ar->radius(), ar->startAngle(), ar->endAngle() + dth)
                         : std::make_unique<ArcEntity>(ar->center(), ar->radius(), ar->startAngle() - dth, ar->endAngle()),
                    e);
    }
    if (auto* p = dynamic_cast<const PolylineEntity*>(&e); p && !p->isClosed() && p->vertices().size() >= 2) {
        auto v = p->vertices();
        auto bl = p->bulges();
        const bool endB = pick.distanceTo(v.back()) <= pick.distanceTo(v.front());
        auto segs = segsOf(*p);
        const double L = totalLen(segs);
        if (L + delta <= kTol) throw Error("err.lengthen");
        if (delta < 0) {
            auto r = endB ? subPath(segs, 0, L + delta) : subPath(segs, -delta, L);
            return like(std::move(r), e);
        }
        // uzaytirish: chetki segment yo'nalishida to'g'ri davom
        if (endB) {
            const Seg& s = segs.back();
            const Vec3 u = unit2(s.at(1) - s.at(1 - 1e-4));
            v.push_back(v.back() + u * delta);
            if (bl.size() < v.size()) bl.resize(v.size(), 0);
        } else {
            const Seg& s = segs.front();
            const Vec3 u = unit2(s.at(0) - s.at(1e-4));
            v.insert(v.begin(), v.front() + u * delta);
            bl.insert(bl.begin(), 0);
        }
        return like(std::make_unique<PolylineEntity>(std::move(v), std::move(bl), false), e);
    }
    throw Error("err.modify_unsupported", {std::string(toString(e.type()))});
}

// ---------------------------------------------------------------- Annotate
EntityList makeLeader(const std::vector<Vec3>& pts, const std::string& text, double h) {
    if (pts.size() < 2) throw Error("err.leader_points");
    if (h <= 0) throw Error("err.height_positive");
    EntityList out;
    // strelka: uchi pts[0], uzunligi 1.5h, yarim kengligi h/3 — to'ldirilgan (HATCH SOLID)
    const Vec3 u = unit2(pts[1] - pts[0]);
    const Vec3 n{-u.y, u.x, 0};
    const double al = 1.5 * h, aw = h / 3;
    const Vec3 tip = pts[0], b1 = tip + u * al + n * aw, b2 = tip + u * al - n * aw;
    out.push_back(std::make_unique<HatchEntity>(std::vector<PolylineEntity>{PolylineEntity({tip, b1, b2}, true)}, "SOLID"));
    std::vector<Vec3> line(pts.begin(), pts.end());
    // yozuv tokchasi: oxirgi segment yo'nalishida gorizontal, matn kengligicha
    const Vec3 last = pts.back();
    const bool right = pts.back().x >= pts[pts.size() - 2].x;
    const double tw = 0.62 * h * static_cast<double>(std::max<std::size_t>(1, text.size()));
    const Vec3 shelf = last + Vec3{right ? tw + h * 0.4 : -(tw + h * 0.4), 0, 0};
    if (!text.empty()) line.push_back(shelf);
    out.push_back(std::make_unique<PolylineEntity>(std::move(line), false));
    if (!text.empty()) {
        const Vec3 ins = right ? last + Vec3{h * 0.2, h * 0.3, 0} : shelf + Vec3{h * 0.2, h * 0.3, 0};
        out.push_back(std::make_unique<TextEntity>(ins, h, text));
    }
    return out;
}

EntityList makeAngularDim(const Vec3& c, const Vec3& p1, const Vec3& p2, const Vec3& loc, double h, bool dms) {
    if (h <= 0) throw Error("err.height_positive");
    const double r = (loc - c).length2d();
    if (r < kTol || (p1 - c).length2d() < kTol || (p2 - c).length2d() < kTol) throw Error("err.dim_angle");
    double a1 = std::atan2(p1.y - c.y, p1.x - c.x), a2 = std::atan2(p2.y - c.y, p2.x - c.x);
    const double al = std::atan2(loc.y - c.y, loc.x - c.x);
    auto norm = [](double a) {
        while (a < 0) a += 2 * kPi;
        while (a >= 2 * kPi) a -= 2 * kPi;
        return a;
    };
    // yoy a1 -> a2 (CCW) loc ni o'z ichiga olmasa — teskari tomon
    double sw = norm(a2 - a1);
    if (norm(al - a1) > sw + 1e-12) { std::swap(a1, a2); sw = norm(a2 - a1); }
    EntityList out;
    out.push_back(std::make_unique<ArcEntity>(c, r, a1, a1 + sw));
    // chiqarish chiziqlari: nur bo'ylab tayanch nuqtadan yoydan biroz o'tib
    for (double a : {a1, a1 + sw}) {
        const Vec3 d{std::cos(a), std::sin(a), 0};
        const Vec3 pr = std::abs(a - a1) < 1e-12 ? p1 : p2;
        const double dist = (pr - c).length2d();
        if (dist + h * 0.3 < r) out.push_back(std::make_unique<LineEntity>(c + d * (dist + h * 0.3), c + d * (r + h * 0.6)));
        // serif (45°) — GOST uslubida
        const Vec3 q = c + d * r, t{-d.y, d.x, 0};
        const Vec3 s = unit2(d + t) * (h * 0.5);
        out.push_back(std::make_unique<LineEntity>(q - s, q + s));
    }
    const double deg = rad2deg(sw);
    std::string txt;
    if (dms) {
        const int D = static_cast<int>(deg);
        const double mf = (deg - D) * 60;
        const int M = static_cast<int>(mf);
        const int S = static_cast<int>(std::round((mf - M) * 60));
        txt = std::format("{}\xC2\xB0{:02}'{:02}\"", D, M, S);
    } else {
        txt = std::format("{:.1f}\xC2\xB0", deg);
        for (auto& ch : txt) if (ch == '.') ch = ',';
    }
    const double am = a1 + sw / 2;
    const Vec3 tp = c + Vec3{std::cos(am), std::sin(am), 0} * (r + h * 0.5);
    double rot = am - kPi / 2;
    while (rot > kPi / 2) rot -= kPi;
    while (rot < -kPi / 2) rot += kPi;
    out.push_back(std::make_unique<TextEntity>(tp, h, txt, rot, TextHAlign::Center, TextVAlign::Bottom));
    return out;
}

} // namespace asiacad
