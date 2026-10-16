#include <asiacad/core/modify.h>

#include <asiacad/core/error.h>

#include <algorithm>

namespace asiacad {

using geom::Prim;

namespace {

constexpr double kTol = 1e-9;

[[noreturn]] void unsupported(const IEntity& e) { throw Error("err.modify_unsupported", {std::string(toString(e.type()))}); }

Vec3 unit2(const Vec3& v) {
    const double l = v.length2d();
    return l < 1e-300 ? Vec3{} : Vec3{v.x / l, v.y / l, 0};
}
double cross2(const Vec3& a, const Vec3& b) { return a.x * b.y - a.y * b.x; }
Vec3 leftNormal(const Vec3& u) { return {-u.y, u.x, 0}; }

double lenOf(const Prim& p) { return p.kind == Prim::Kind::Seg ? (p.b - p.a).length2d() : p.r * std::abs(p.sweep); }

// Poliliniya segmenti yo'nalishli primitiv sifatida (CW yoy — manfiy sweep)
Prim bulgeSeg(const Vec3& p0, const Vec3& p1, double bulge) {
    if (std::abs(bulge) < 1e-12 || p0.distanceTo(p1) < 1e-15) return Prim::seg(p0, p1);
    const double theta = 4 * std::atan(bulge);
    const double half = (p1 - p0).length2d() / 2;
    const double r = half * (1 + bulge * bulge) / (2 * std::abs(bulge));
    // markaz vatar o'rtasidan chap normal bo'yicha: half·(1−b²)/(2b) (ishora b dan)
    const Vec3 c = (p0 + p1) * 0.5 + leftNormal(unit2(p1 - p0)) * (half * (1 - bulge * bulge) / (2 * bulge));
    const double start = std::atan2(p0.y - c.y, p0.x - c.x);
    Prim pr = Prim::arc(c, r, start, theta);
    pr.a = p0;  // yaxlitlash xatosiz uchlar
    pr.b = p1;
    return pr;
}

struct Path {
    std::vector<Prim> prims;
    std::vector<double> starts;  // har primitivning boshlang'ich yoy uzunligi
    double length = 0;
    bool closed = false;
};

Path makePath(std::vector<Prim> prims, bool closed) {
    Path p;
    p.closed = closed;
    for (auto& pr : prims) {
        p.starts.push_back(p.length);
        p.length += lenOf(pr);
        p.prims.push_back(pr);
    }
    return p;
}

Path pathOf(const IEntity& e) {
    switch (e.type()) {
        case EntityType::Line: {
            const auto& l = static_cast<const LineEntity&>(e);
            return makePath({Prim::seg(l.startPoint(), l.endPoint())}, false);
        }
        case EntityType::Arc: {
            const auto& a = static_cast<const ArcEntity&>(e);
            return makePath({Prim::arc(a.center(), a.radius(), a.startAngle(), a.sweep())}, false);
        }
        case EntityType::Circle: {
            const auto& c = static_cast<const CircleEntity&>(e);
            return makePath({Prim::arc(c.center(), c.radius(), 0, 2 * kPi)}, true);
        }
        case EntityType::Polyline: {
            const auto& pl = static_cast<const PolylineEntity&>(e);
            const auto& v = pl.vertices();
            const auto& b = pl.bulges();
            std::vector<Prim> pr;
            const std::size_t n = pl.isClosed() ? v.size() : (v.empty() ? 0 : v.size() - 1);
            for (std::size_t i = 0; i < n; ++i) {
                const Vec3& p0 = v[i];
                const Vec3& p1 = v[(i + 1) % v.size()];
                if (p0.distanceTo(p1) < 1e-15) continue;
                pr.push_back(bulgeSeg(p0, p1, i < b.size() ? b[i] : 0.0));
            }
            if (pr.empty()) unsupported(e);
            return makePath(std::move(pr), pl.isClosed());
        }
        default: unsupported(e);
    }
}

// Primitiv ustidagi nuqtaning lokal parametri t ∈ [0,1]
double localT(const Prim& p, const Vec3& q) {
    if (p.kind == Prim::Kind::Seg) {
        const Vec3 d = p.b - p.a;
        const double L2 = d.x * d.x + d.y * d.y;
        return L2 < 1e-300 ? 0.0 : std::clamp(((q.x - p.a.x) * d.x + (q.y - p.a.y) * d.y) / L2, 0.0, 1.0);
    }
    double t = 0;
    if (!p.containsAngle(std::atan2(q.y - p.c.y, q.x - p.c.x), &t))
        t = (q - p.a).length2d() <= (q - p.b).length2d() ? 0.0 : 1.0;
    return t;
}

// Egri bo'yicha eng yaqin nuqtaning yoy uzunligi parametri
double paramOf(const Path& path, const Vec3& q, std::size_t* primOut = nullptr) {
    double best = 1e300, s = 0;
    for (std::size_t i = 0; i < path.prims.size(); ++i) {
        const double d = geom::distance2d(path.prims[i], q);
        if (d < best) {
            best = d;
            s = path.starts[i] + localT(path.prims[i], q) * lenOf(path.prims[i]);
            if (primOut) *primOut = i;
        }
    }
    return s;
}

Prim subPrim(const Prim& p, double t0, double t1) {
    if (p.kind == Prim::Kind::Seg) return Prim::seg(p.pointAt(t0), p.pointAt(t1));
    Prim r = Prim::arc(p.c, p.r, p.start + p.sweep * t0, p.sweep * (t1 - t0));
    if (t0 == 0) r.a = p.a;
    if (t1 == 1) r.b = p.b;
    return r;
}

// [s0, s1] oralig'idagi bo'lak (s0 < s1)
std::vector<Prim> subPath(const Path& path, double s0, double s1) {
    std::vector<Prim> out;
    for (std::size_t i = 0; i < path.prims.size(); ++i) {
        const double a = path.starts[i], L = lenOf(path.prims[i]), b = a + L;
        if (b <= s0 + kTol || a >= s1 - kTol || L < 1e-15) continue;
        const double t0 = std::clamp((s0 - a) / L, 0.0, 1.0), t1 = std::clamp((s1 - a) / L, 0.0, 1.0);
        if ((t1 - t0) * L < 1e-12) continue;
        out.push_back(subPrim(path.prims[i], t0, t1));
    }
    return out;
}

std::unique_ptr<PolylineEntity> polylineFrom(const std::vector<Prim>& prims, bool closed) {
    std::vector<Vec3> v;
    std::vector<double> bul;
    for (const auto& p : prims) {
        v.push_back(p.a);
        bul.push_back(p.kind == Prim::Kind::Seg ? 0.0 : std::tan(p.sweep / 4));
    }
    if (!closed && !prims.empty()) {
        v.push_back(prims.back().b);
        bul.push_back(0.0);
    }
    return std::make_unique<PolylineEntity>(std::move(v), std::move(bul), closed);
}

std::unique_ptr<IEntity> arcFrom(const Prim& p) {
    const double s = p.sweep >= 0 ? p.start : p.start + p.sweep;
    return std::make_unique<ArcEntity>(p.c, p.r, normalizeAngle(s), normalizeAngle(s + std::abs(p.sweep)));
}

// Bo'lakni manba turida qaytarish (chiziq — chiziq, yoy/aylana — yoy, poliliniya — poliliniya)
std::unique_ptr<IEntity> entityFrom(const IEntity& src, const std::vector<Prim>& prims) {
    std::unique_ptr<IEntity> out;
    if (prims.empty()) return out;
    if (src.type() == EntityType::Polyline) out = polylineFrom(prims, false);
    else if (src.type() == EntityType::Line) out = std::make_unique<LineEntity>(prims.front().a, prims.back().b);
    else {  // yoy/aylana bo'laklari bitta markazli — umumiy yoy
        const Prim& f = prims.front();
        double sweep = 0;
        for (const auto& p : prims) sweep += p.sweep;
        out = arcFrom(Prim::arc(f.c, f.r, f.start, sweep));
    }
    out->setLayer(src.layer());
    out->setColor(src.color());
    return out;
}

// Ikki primitiv kesishishlarining egri bo'yicha parametrlari
std::vector<double> cutParams(const Path& path, const std::vector<Prim>& edges) {
    std::vector<double> s;
    std::vector<Vec3> pts;
    for (std::size_t i = 0; i < path.prims.size(); ++i) {
        const Prim& p = path.prims[i];
        const double L = lenOf(p);
        for (const auto& e : edges) {
            pts.clear();
            geom::intersect(p, e, pts);
            for (const auto& x : pts) s.push_back(path.starts[i] + localT(p, x) * L);
        }
    }
    std::sort(s.begin(), s.end());
    const double eps = 1e-9 * std::max(1.0, path.length);
    s.erase(std::unique(s.begin(), s.end(), [&](double a, double b) { return std::abs(a - b) <= eps; }), s.end());
    if (!path.closed)
        s.erase(std::remove_if(s.begin(), s.end(), [&](double v) { return v <= eps || v >= path.length - eps; }), s.end());
    else if (s.size() >= 2 && s.back() >= path.length - eps && s.front() <= eps)
        s.pop_back();  // yopiq egrining boshi = oxiri
    return s;
}

// Chegaralar qamrovi bo'yicha "cheksiz" nur uzunligi
double farLength(const std::vector<Prim>& edges, const Vec3& from) {
    BBox b;
    for (const auto& e : edges) b.expand(e.bbox());
    b.expand(from);
    return 4 * ((b.max - b.min).length2d() + 1.0);
}

} // namespace

// ============================================================ OFFSET
std::unique_ptr<IEntity> offsetEntity(const IEntity& e, double d, const Vec3& side) {
    if (!(d > 0)) throw Error("err.distance_positive");
    std::unique_ptr<IEntity> out;
    switch (e.type()) {
        case EntityType::Line: {
            const auto& l = static_cast<const LineEntity&>(e);
            const Vec3 u = unit2(l.endPoint() - l.startPoint());
            if (u.length2d() == 0) unsupported(e);
            const double sgn = cross2(u, side - l.startPoint()) >= 0 ? 1.0 : -1.0;
            const Vec3 n = leftNormal(u) * (sgn * d);
            out = std::make_unique<LineEntity>(l.startPoint() + n, l.endPoint() + n);
            break;
        }
        case EntityType::Circle:
        case EntityType::Arc: {
            Vec3 c;
            double r;
            if (auto* ci = dynamic_cast<const CircleEntity*>(&e)) { c = ci->center(); r = ci->radius(); }
            else { auto& a = static_cast<const ArcEntity&>(e); c = a.center(); r = a.radius(); }
            const double nr = (side - c).length2d() > r ? r + d : r - d;
            if (nr <= 1e-12) throw Error("err.offset_radius");
            if (auto* a = dynamic_cast<const ArcEntity*>(&e)) out = std::make_unique<ArcEntity>(c, nr, a->startAngle(), a->endAngle());
            else out = std::make_unique<CircleEntity>(c, nr);
            break;
        }
        case EntityType::Polyline: {
            const Path path = pathOf(e);
            std::size_t k = 0;
            paramOf(path, side, &k);
            const Prim& near = path.prims[k];
            double sgn;
            if (near.kind == Prim::Kind::Seg) {
                const Vec3 q = geom::closestPoint(near, side);
                sgn = cross2(near.b - near.a, side - q) >= 0 ? 1.0 : -1.0;
            } else {
                const bool inside = (side - near.c).length2d() < near.r;
                sgn = (inside == (near.sweep > 0)) ? 1.0 : -1.0;  // CCW yoyning chap tomoni — ichkari
            }
            const double delta = sgn * d;  // chapga musbat
            std::vector<Prim> off;
            for (const auto& p : path.prims) {
                if (p.kind == Prim::Kind::Seg) {
                    const Vec3 n = leftNormal(unit2(p.b - p.a)) * delta;
                    off.push_back(Prim::seg(p.a + n, p.b + n));
                } else {
                    const double nr = p.r - delta * (p.sweep > 0 ? 1.0 : -1.0);
                    if (nr <= 1e-12) throw Error("err.offset_radius");
                    off.push_back(Prim::arc(p.c, nr, p.start, p.sweep));
                }
            }
            // Tutashuvlar: to'g'ri-to'g'ri — kesishguncha; urinma — o'zi; boshqasi — to'g'ri ko'prik
            std::vector<Prim> joined;
            const std::size_t n = off.size();
            const std::size_t junctions = path.closed ? n : n - 1;
            std::vector<Prim> work = off;
            std::vector<bool> bridgeAfter(n, false);
            for (std::size_t j = 0; j < junctions; ++j) {
                Prim& A = work[j];
                Prim& B = work[(j + 1) % n];
                if (A.b.distanceTo(B.a) < 1e-9 * std::max(1.0, d)) { B.a = A.b; continue; }
                if (A.kind == Prim::Kind::Seg && B.kind == Prim::Kind::Seg) {
                    const Vec3 u = A.b - A.a, v = B.b - B.a;
                    const double den = cross2(u, v);
                    if (std::abs(den) > 1e-12 * u.length2d() * v.length2d()) {
                        const double t = cross2(B.a - A.a, v) / den;
                        const Vec3 x = A.a + u * t;
                        A.b = x;
                        B.a = x;
                        continue;
                    }
                }
                bridgeAfter[j] = true;
            }
            for (std::size_t i = 0; i < n; ++i) {
                joined.push_back(work[i]);
                if (bridgeAfter[i]) joined.push_back(Prim::seg(work[i].b, work[(i + 1) % n].a));
            }
            out = polylineFrom(joined, path.closed);
            break;
        }
        default: unsupported(e);
    }
    out->setLayer(e.layer());
    out->setColor(e.color());
    return out;
}

// ============================================================ TRIM
std::vector<std::unique_ptr<IEntity>> trimEntity(const IEntity& e, const std::vector<Prim>& edges, const Vec3& pick) {
    const Path path = pathOf(e);
    const auto cuts = cutParams(path, edges);
    std::vector<std::unique_ptr<IEntity>> out;
    const double sp = paramOf(path, pick);
    if (!path.closed) {
        if (cuts.empty()) throw Error("err.trim_nothing");
        auto lo = std::find_if(cuts.rbegin(), cuts.rend(), [&](double c) { return c < sp; });
        auto hi = std::find_if(cuts.begin(), cuts.end(), [&](double c) { return c > sp; });
        if (lo != cuts.rend()) out.push_back(entityFrom(e, subPath(path, 0, *lo)));
        if (hi != cuts.end()) out.push_back(entityFrom(e, subPath(path, *hi, path.length)));
        return out;
    }
    if (cuts.size() < 2) throw Error("err.trim_nothing");
    double lo = cuts.back(), hi = cuts.front();  // tanlangan nuqta oxirgi va birinchi kesish orasida (aylanib)
    for (std::size_t i = 0; i + 1 < cuts.size(); ++i)
        if (sp > cuts[i] && sp < cuts[i + 1]) { lo = cuts[i]; hi = cuts[i + 1]; }
    // qoladigan qism: hi dan oldinga lo gacha
    std::vector<Prim> keep = hi < lo ? subPath(path, hi, lo) : subPath(path, hi, path.length);
    if (hi >= lo) {
        auto tail = subPath(path, 0, lo);
        keep.insert(keep.end(), tail.begin(), tail.end());
    }
    if (e.type() == EntityType::Circle) {
        const Prim& c = path.prims.front();
        auto arc = std::make_unique<ArcEntity>(c.c, c.r, normalizeAngle(hi / c.r), normalizeAngle(lo / c.r));
        arc->setLayer(e.layer());
        arc->setColor(e.color());
        out.push_back(std::move(arc));
    } else {
        out.push_back(entityFrom(e, keep));
    }
    return out;
}

// ============================================================ EXTEND
std::unique_ptr<IEntity> extendEntity(const IEntity& e, const std::vector<Prim>& edges, const Vec3& pick) {
    std::vector<Vec3> pts;
    // Nurni chegaralar bilan kesib, eng yaqin musbat masofa
    auto nearestOnRay = [&](const Vec3& from, const Vec3& dir, double& dist) {
        const Prim ray = Prim::seg(from, from + dir * farLength(edges, from));
        bool found = false;
        for (const auto& ed : edges) {
            pts.clear();
            geom::intersect(ray, ed, pts);
            for (const auto& x : pts) {
                const double s = (x - from).dot(dir);
                if (s > 1e-9 && (!found || s < dist)) { dist = s; found = true; }
            }
        }
        return found;
    };
    std::unique_ptr<IEntity> out;
    switch (e.type()) {
        case EntityType::Line: {
            const auto& l = static_cast<const LineEntity&>(e);
            const bool atEnd = pick.distanceTo(l.endPoint()) <= pick.distanceTo(l.startPoint());
            const Vec3 from = atEnd ? l.endPoint() : l.startPoint();
            const Vec3 dir = unit2(atEnd ? l.endPoint() - l.startPoint() : l.startPoint() - l.endPoint());
            double dist = 0;
            if (!nearestOnRay(from, dir, dist)) throw Error("err.extend_nothing");
            const Vec3 to = from + dir * dist;
            out = atEnd ? std::make_unique<LineEntity>(l.startPoint(), to) : std::make_unique<LineEntity>(to, l.endPoint());
            break;
        }
        case EntityType::Arc: {
            const auto& a = static_cast<const ArcEntity&>(e);
            const bool atEnd = pick.distanceTo(a.endPoint()) <= pick.distanceTo(a.startPoint());
            const double rest = 2 * kPi - a.sweep();
            if (rest < 1e-9) throw Error("err.extend_nothing");
            const Prim free = atEnd ? Prim::arc(a.center(), a.radius(), a.endAngle(), rest)
                                    : Prim::arc(a.center(), a.radius(), a.startAngle(), -rest);
            double best = 1e300;
            for (const auto& ed : edges) {
                pts.clear();
                geom::intersect(free, ed, pts);
                for (const auto& x : pts) {
                    double t = 0;
                    if (free.containsAngle(std::atan2(x.y - free.c.y, x.x - free.c.x), &t) && t * rest > 1e-9)
                        best = std::min(best, t * rest);
                }
            }
            if (best > 2 * kPi) throw Error("err.extend_nothing");
            out = atEnd ? std::make_unique<ArcEntity>(a.center(), a.radius(), a.startAngle(), a.endAngle() + best)
                        : std::make_unique<ArcEntity>(a.center(), a.radius(), a.startAngle() - best, a.endAngle());
            break;
        }
        case EntityType::Polyline: {
            const auto& pl = static_cast<const PolylineEntity&>(e);
            auto v = pl.vertices();
            auto b = pl.bulges();
            if (pl.isClosed() || v.size() < 2) throw Error("err.extend_nothing");
            const bool atEnd = pick.distanceTo(v.back()) <= pick.distanceTo(v.front());
            const std::size_t i0 = atEnd ? v.size() - 2 : 1, i1 = atEnd ? v.size() - 1 : 0;
            const std::size_t segIdx = atEnd ? v.size() - 2 : 0;
            if (segIdx < b.size() && std::abs(b[segIdx]) > 1e-12) unsupported(e);  // yoyli uchi — keyingi bosqich
            const Vec3 dir = unit2(v[i1] - v[i0]);
            double dist = 0;
            if (!nearestOnRay(v[i1], dir, dist)) throw Error("err.extend_nothing");
            v[i1] = v[i1] + dir * dist;
            out = std::make_unique<PolylineEntity>(std::move(v), std::move(b), false);
            break;
        }
        default: unsupported(e);
    }
    out->setLayer(e.layer());
    out->setColor(e.color());
    return out;
}

// ============================================================ FILLET
FilletResult filletLines(const LineEntity& l1, const Vec3& pick1, const LineEntity& l2, const Vec3& pick2, double r) {
    if (r < 0) throw Error("err.fillet_radius");
    const Vec3 a1 = l1.startPoint(), b1 = l1.endPoint(), a2 = l2.startPoint(), b2 = l2.endPoint();
    const Vec3 d1 = b1 - a1, d2 = b2 - a2;
    const double den = cross2(d1, d2);
    if (std::abs(den) <= 1e-12 * d1.length2d() * d2.length2d()) throw Error("err.fillet_parallel");
    const Vec3 x = a1 + d1 * (cross2(a2 - a1, d2) / den);
    auto keepDir = [&](const Vec3& a, const Vec3& b, const Vec3& pick, Vec3& far) {
        Vec3 u = unit2(b - a);
        const double sp = (pick - x).dot(u);
        if (std::abs(sp) < 1e-12) {  // tanlov kesishish ustida — uzoq uchi tomoni
            far = (a - x).length2d() >= (b - x).length2d() ? a : b;
            return unit2(far - x);
        }
        if (sp < 0) u = u * -1.0;
        far = (a - x).dot(u) >= (b - x).dot(u) ? a : b;
        return u;
    };
    Vec3 far1, far2;
    const Vec3 u1 = keepDir(a1, b1, pick1, far1), u2 = keepDir(a2, b2, pick2, far2);
    const double avail1 = (far1 - x).dot(u1), avail2 = (far2 - x).dot(u2);
    if (avail1 <= 1e-12 || avail2 <= 1e-12) throw Error("err.fillet_radius");
    const double theta = std::acos(std::clamp(u1.dot(u2), -1.0, 1.0));
    if (theta < 1e-9 || theta > kPi - 1e-9) throw Error("err.fillet_parallel");
    const double t = r / std::tan(theta / 2);
    if (t > avail1 + 1e-9 || t > avail2 + 1e-9) throw Error("err.fillet_radius");
    const Vec3 t1 = x + u1 * t, t2 = x + u2 * t;
    auto rebuild = [](const LineEntity& src, const Vec3& far, const Vec3& tp) {
        const bool startIsFar = src.startPoint().distanceTo(far) < src.endPoint().distanceTo(far);
        auto l = startIsFar ? std::make_unique<LineEntity>(far, tp) : std::make_unique<LineEntity>(tp, far);
        l->setLayer(src.layer());
        l->setColor(src.color());
        return l;
    };
    FilletResult res;
    res.first = rebuild(l1, far1, t1);
    res.second = rebuild(l2, far2, t2);
    if (r > 0) {
        const Vec3 bis = unit2(u1 + u2);
        const Vec3 c = x + bis * (r / std::sin(theta / 2));
        const Vec3 mid = c - bis * r;
        auto arc = std::make_unique<ArcEntity>(ArcEntity::fromThreePoints(t1, mid, t2));
        arc->setLayer(l1.layer());
        arc->setColor(l1.color());
        res.arc = std::move(arc);
    }
    return res;
}

} // namespace asiacad
