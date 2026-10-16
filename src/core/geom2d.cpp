#include <asiacad/core/dimension.h>
#include <asiacad/core/entities.h>
#include <asiacad/core/geom2d.h>

namespace asiacad::geom {

Prim Prim::seg(const Vec3& a, const Vec3& b) {
    Prim p;
    p.kind = Kind::Seg;
    p.a = a;
    p.b = b;
    return p;
}

Prim Prim::arc(const Vec3& c, double r, double start, double sweep) {
    Prim p;
    p.kind = Kind::Arc;
    p.c = c;
    p.r = r;
    p.start = start;
    p.sweep = sweep;
    p.a = {c.x + r * std::cos(start), c.y + r * std::sin(start), c.z};
    p.b = {c.x + r * std::cos(start + sweep), c.y + r * std::sin(start + sweep), c.z};
    return p;
}

bool Prim::containsAngle(double ang, double* t) const {
    double d = sweep >= 0 ? normalizeAngle(ang - start) : normalizeAngle(start - ang);
    if (d > 2 * kPi - 1e-9) d = 0;
    const double s = std::abs(sweep);
    if (isFullCircle()) {
        if (t) *t = d / (2 * kPi);
        return true;
    }
    if (d > s + 1e-12) return false;
    if (t) *t = s > 0 ? d / s : 0;
    return true;
}

Vec3 Prim::pointAt(double t) const {
    if (kind == Kind::Seg) return a + (b - a) * t;
    const double ang = start + sweep * t;
    return {c.x + r * std::cos(ang), c.y + r * std::sin(ang), a.z + (b.z - a.z) * t};
}

BBox Prim::bbox() const {
    BBox box;
    box.expand(a);
    box.expand(b);
    if (kind == Kind::Arc)
        for (int q = 0; q < 4; ++q)
            if (containsAngle(q * kPi / 2)) box.expand(Vec3{c.x + r * std::cos(q * kPi / 2), c.y + r * std::sin(q * kPi / 2), c.z});
    return box;
}

std::vector<Prim> primitivesOf(const IEntity& e) {
    std::vector<Prim> out;
    switch (e.type()) {
        case EntityType::Line: {
            const auto& l = static_cast<const LineEntity&>(e);
            out.push_back(Prim::seg(l.startPoint(), l.endPoint()));
            break;
        }
        case EntityType::Circle: {
            const auto& c = static_cast<const CircleEntity&>(e);
            out.push_back(Prim::arc(c.center(), c.radius(), 0, 2 * kPi));
            break;
        }
        case EntityType::Arc: {
            const auto& a = static_cast<const ArcEntity&>(e);
            out.push_back(Prim::arc(a.center(), a.radius(), a.startAngle(), a.sweep()));
            break;
        }
        case EntityType::Polyline:
            for (const auto& s : static_cast<const PolylineEntity&>(e).explodeSegments()) {
                auto p = primitivesOf(*s);
                out.insert(out.end(), p.begin(), p.end());
            }
            break;
        case EntityType::Insert:
            for (const auto& s : static_cast<const InsertEntity&>(e).explode()) {
                auto p = primitivesOf(*s);
                out.insert(out.end(), p.begin(), p.end());
            }
            break;
        case EntityType::Hatch:
            for (const auto& l : static_cast<const HatchEntity&>(e).loops()) {
                auto p = primitivesOf(l);
                out.insert(out.end(), p.begin(), p.end());
            }
            break;
        case EntityType::Dimension:
            for (const auto& s : static_cast<const DimensionEntity&>(e).explode()) {
                if (s->type() == EntityType::Hatch) continue;  // strelka uchburchagi — tanlashda chiziq yetarli
                auto p = primitivesOf(*s);
                out.insert(out.end(), p.begin(), p.end());
            }
            break;
        case EntityType::Point:
        case EntityType::Text:
            break;
    }
    return out;
}

namespace {
double cross2(const Vec3& u, const Vec3& v) { return u.x * v.y - u.y * v.x; }

void segSeg(const Prim& p, const Prim& q, std::vector<Vec3>& out) {
    const Vec3 r = p.b - p.a, s = q.b - q.a;
    const double den = cross2(r, s);
    const double scale = std::max(r.length2d() * s.length2d(), 1e-300);
    if (std::abs(den) <= 1e-12 * scale) return;  // parallel
    const Vec3 qp = q.a - p.a;
    const double t = cross2(qp, s) / den, u = cross2(qp, r) / den;
    const double e = 1e-9;
    if (t < -e || t > 1 + e || u < -e || u > 1 + e) return;
    out.push_back(p.pointAt(std::clamp(t, 0.0, 1.0)));
}

// To'g'ri chiziq (seg) va aylana kesishuvi: parametrlar
void segArc(const Prim& s, const Prim& a, std::vector<Vec3>& out) {
    const Vec3 d = s.b - s.a, f = s.a - a.c;
    const double A = d.x * d.x + d.y * d.y;
    if (A < 1e-300) return;
    const double B = 2 * (f.x * d.x + f.y * d.y);
    const double C = f.x * f.x + f.y * f.y - a.r * a.r;
    double disc = B * B - 4 * A * C;
    const double tol = 1e-12 * std::max(B * B, std::abs(4 * A * C));
    if (disc < -tol) return;
    if (disc < 0) disc = 0;
    const double sq = std::sqrt(disc);
    const double ts[2] = {(-B - sq) / (2 * A), (-B + sq) / (2 * A)};
    for (int k = 0; k < (sq == 0 ? 1 : 2); ++k) {
        const double t = ts[k];
        if (t < -1e-9 || t > 1 + 1e-9) continue;
        const Vec3 p = s.pointAt(std::clamp(t, 0.0, 1.0));
        if (a.containsAngle(std::atan2(p.y - a.c.y, p.x - a.c.x))) out.push_back(p);
    }
}

void arcArc(const Prim& p, const Prim& q, std::vector<Vec3>& out) {
    const double dx = q.c.x - p.c.x, dy = q.c.y - p.c.y;
    const double d = std::hypot(dx, dy);
    if (d < 1e-12) return;  // konsentrik
    const double eps = 1e-9 * std::max({p.r, q.r, d});
    if (d > p.r + q.r + eps || d < std::abs(p.r - q.r) - eps) return;
    const double aa = (p.r * p.r - q.r * q.r + d * d) / (2 * d);
    const double h2 = p.r * p.r - aa * aa;
    const double h = h2 > 0 ? std::sqrt(h2) : 0.0;
    const Vec3 m{p.c.x + aa * dx / d, p.c.y + aa * dy / d, p.c.z};
    const Vec3 cand[2] = {{m.x + h * dy / d, m.y - h * dx / d, p.c.z}, {m.x - h * dy / d, m.y + h * dx / d, p.c.z}};
    for (int k = 0; k < (h > 0 ? 2 : 1); ++k) {
        const Vec3& x = cand[k];
        if (p.containsAngle(std::atan2(x.y - p.c.y, x.x - p.c.x)) && q.containsAngle(std::atan2(x.y - q.c.y, x.x - q.c.x)))
            out.push_back(x);
    }
}
} // namespace

void intersect(const Prim& p, const Prim& q, std::vector<Vec3>& out) {
    using K = Prim::Kind;
    if (p.kind == K::Seg && q.kind == K::Seg) segSeg(p, q, out);
    else if (p.kind == K::Seg) segArc(p, q, out);
    else if (q.kind == K::Seg) segArc(q, p, out);
    else arcArc(p, q, out);
}

Vec3 closestPoint(const Prim& p, const Vec3& pt) {
    if (p.kind == Prim::Kind::Seg) {
        const Vec3 d = p.b - p.a;
        const double L2 = d.x * d.x + d.y * d.y;
        if (L2 < 1e-300) return p.a;
        const double t = std::clamp(((pt.x - p.a.x) * d.x + (pt.y - p.a.y) * d.y) / L2, 0.0, 1.0);
        return p.pointAt(t);
    }
    double t = 0;
    if (p.containsAngle(std::atan2(pt.y - p.c.y, pt.x - p.c.x), &t)) return p.pointAt(t);
    return (pt - p.a).length2d() <= (pt - p.b).length2d() ? p.a : p.b;
}

double distance2d(const Prim& p, const Vec3& pt) { return (closestPoint(p, pt) - pt).length2d(); }

bool perpendicularFoot(const Prim& p, const Vec3& from, Vec3& out) {
    if (p.kind == Prim::Kind::Seg) {
        const Vec3 d = p.b - p.a;
        const double L2 = d.x * d.x + d.y * d.y;
        if (L2 < 1e-300) return false;
        const double t = ((from.x - p.a.x) * d.x + (from.y - p.a.y) * d.y) / L2;
        out = p.a + d * t;  // cheksiz chiziqqa (AutoCAD PER ham shunday)
        return true;
    }
    const double dx = from.x - p.c.x, dy = from.y - p.c.y, L = std::hypot(dx, dy);
    if (L < 1e-12) return false;
    bool found = false;
    double best = 0;
    for (double sgn : {1.0, -1.0}) {
        const Vec3 x{p.c.x + sgn * p.r * dx / L, p.c.y + sgn * p.r * dy / L, p.c.z};
        if (!p.containsAngle(std::atan2(x.y - p.c.y, x.x - p.c.x))) continue;
        const double dd = (x - from).length2d();
        if (!found || dd < best) { out = x; best = dd; found = true; }
    }
    return found;
}

void tangentPoints(const Prim& p, const Vec3& from, std::vector<Vec3>& out) {
    if (p.kind != Prim::Kind::Arc) return;
    const double dx = from.x - p.c.x, dy = from.y - p.c.y, d = std::hypot(dx, dy);
    if (d <= p.r * (1 + 1e-12)) return;
    const double base = std::atan2(dy, dx), alpha = std::acos(p.r / d);
    for (double ang : {base + alpha, base - alpha})
        if (p.containsAngle(ang)) out.push_back({p.c.x + p.r * std::cos(ang), p.c.y + p.r * std::sin(ang), p.c.z});
}

bool hitsBox(const Prim& p, const BBox& box) {
    auto inside = [&](const Vec3& q) { return q.x >= box.min.x && q.x <= box.max.x && q.y >= box.min.y && q.y <= box.max.y; };
    if (inside(p.a) || inside(p.b)) return true;
    if (!p.bbox().intersects2d(box)) return false;
    const Vec3 c0{box.min.x, box.min.y, 0}, c1{box.max.x, box.min.y, 0}, c2{box.max.x, box.max.y, 0}, c3{box.min.x, box.max.y, 0};
    const Prim edges[4] = {Prim::seg(c0, c1), Prim::seg(c1, c2), Prim::seg(c2, c3), Prim::seg(c3, c0)};
    std::vector<Vec3> hits;
    for (const auto& e : edges) {
        intersect(p, e, hits);
        if (!hits.empty()) return true;
    }
    return false;
}

} // namespace asiacad::geom
