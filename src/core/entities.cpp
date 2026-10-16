#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/io/dxf_writer.h>

#include <format>
#include <stdexcept>

namespace asiacad {

std::string_view toString(EntityType t) {
    switch (t) {
        case EntityType::Point: return "POINT";
        case EntityType::Line: return "LINE";
        case EntityType::Circle: return "CIRCLE";
        case EntityType::Arc: return "ARC";
        case EntityType::Polyline: return "POLYLINE";
        case EntityType::Text: return "TEXT";
        case EntityType::Insert: return "INSERT";
        case EntityType::Hatch: return "HATCH";
        case EntityType::Dimension: return "DIMENSION";
    }
    return "UNKNOWN";
}

namespace {
std::string fmt(const Vec3& p) { return std::format("({:.4f}, {:.4f}, {:.4f})", p.x, p.y, p.z); }

Vec3 closestOnSegment(const Vec3& a, const Vec3& b, const Vec3& p) {
    const Vec3 ab = b - a;
    const double len2 = ab.dot(ab);
    if (len2 < kEps * kEps) return a;
    const double t = std::clamp((p - a).dot(ab) / len2, 0.0, 1.0);
    return a + ab * t;
}

std::size_t utf8Length(std::string_view s) {
    std::size_t n = 0;
    for (unsigned char ch : s) if ((ch & 0xC0) != 0x80) ++n;
    return n;
}
} // namespace

// ---------------- Point ----------------
BBox PointEntity::bbox() const { BBox b; b.expand(pos_); return b; }
std::string PointEntity::describe() const { return std::format("POINT {}", fmt(pos_)); }
void PointEntity::writeDxf(DxfWriter& w) const {
    w.beginEntity("POINT", *this);
    w.point(10, pos_);
}

// ---------------- Line ----------------
BBox LineEntity::bbox() const { BBox b; b.expand(a_); b.expand(b_); return b; }
std::string LineEntity::describe() const {
    return std::format("LINE {} -> {}  L={:.4f}", fmt(a_), fmt(b_), length());
}
Vec3 LineEntity::pointAtDistance(double s) const {
    const double L = length();
    if (L < kEps) return a_;
    return a_ + (b_ - a_) * (std::clamp(s, 0.0, L) / L);
}
Vec3 LineEntity::closestPoint(const Vec3& p) const { return closestOnSegment(a_, b_, p); }
void LineEntity::writeDxf(DxfWriter& w) const {
    w.beginEntity("LINE", *this);
    w.point(10, a_);
    w.point(11, b_);
}

// ---------------- Circle ----------------
BBox CircleEntity::bbox() const {
    BBox b;
    b.expand(center_ - Vec3{radius_, radius_, 0});
    b.expand(center_ + Vec3{radius_, radius_, 0});
    return b;
}
void CircleEntity::transform(const Transform& t) {
    center_ = t.apply(center_);
    radius_ *= t.uniformScale();
}
std::string CircleEntity::describe() const {
    return std::format("CIRCLE c={} r={:.4f}  A={:.4f}", fmt(center_), radius_, area());
}
Vec3 CircleEntity::pointAtDistance(double s) const {
    const double a = radius_ > kEps ? s / radius_ : 0.0;
    return center_ + Vec3{radius_ * std::cos(a), radius_ * std::sin(a), 0};
}
Vec3 CircleEntity::closestPoint(const Vec3& p) const {
    Vec3 d{p.x - center_.x, p.y - center_.y, 0};
    const double L = d.length2d();
    if (L < kEps) return startPoint();
    return center_ + d * (radius_ / L);
}
void CircleEntity::writeDxf(DxfWriter& w) const {
    w.beginEntity("CIRCLE", *this);
    w.point(10, center_);
    w.group(40, radius_);
}

// ---------------- Arc ----------------
ArcEntity::ArcEntity(const Vec3& c, double r, double startRad, double endRad)
    : center_(c), radius_(r), start_(normalizeAngle(startRad)), end_(normalizeAngle(endRad)) {}

ArcEntity ArcEntity::fromThreePoints(const Vec3& p1, const Vec3& p2, const Vec3& p3) {
    const double ax = p1.x, ay = p1.y, bx = p2.x, by = p2.y, cx = p3.x, cy = p3.y;
    const double d = 2.0 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by));
    if (std::abs(d) < 1e-12) throw Error("core.collinear");
    const double a2 = ax * ax + ay * ay, b2 = bx * bx + by * by, c2 = cx * cx + cy * cy;
    const Vec3 center{(a2 * (by - cy) + b2 * (cy - ay) + c2 * (ay - by)) / d,
                      (a2 * (cx - bx) + b2 * (ax - cx) + c2 * (bx - ax)) / d, p1.z};
    const double r = Vec3(ax - center.x, ay - center.y).length2d();
    const double a1 = std::atan2(ay - center.y, ax - center.x);
    const double a3 = std::atan2(cy - center.y, cx - center.x);
    const double turn = (p2 - p1).cross(p3 - p2).z;  // >0 => CCW
    return turn > 0 ? ArcEntity(center, r, a1, a3) : ArcEntity(center, r, a3, a1);
}

double ArcEntity::sweep() const {
    const double s = normalizeAngle(end_ - start_);
    return s < 1e-12 ? 2.0 * kPi : s;
}
bool ArcEntity::containsAngle(double a) const { return normalizeAngle(a - start_) <= sweep() + 1e-12; }
Vec3 ArcEntity::pointAtAngle(double a) const {
    return center_ + Vec3{radius_ * std::cos(a), radius_ * std::sin(a), 0};
}
BBox ArcEntity::bbox() const {
    BBox b;
    b.expand(startPoint());
    b.expand(endPoint());
    for (int q = 0; q < 4; ++q) {
        const double a = q * kPi / 2.0;
        if (containsAngle(a)) b.expand(pointAtAngle(a));
    }
    return b;
}
void ArcEntity::transform(const Transform& t) {
    Vec3 s = t.apply(startPoint()), e = t.apply(endPoint());
    center_ = t.apply(center_);
    radius_ *= t.uniformScale();
    if (t.isMirroring()) std::swap(s, e);  // ko'zguda yo'nalish teskari bo'ladi
    start_ = normalizeAngle(std::atan2(s.y - center_.y, s.x - center_.x));
    end_ = normalizeAngle(std::atan2(e.y - center_.y, e.x - center_.x));
}
std::string ArcEntity::describe() const {
    return std::format("ARC c={} r={:.4f} {:.4f}°→{:.4f}°  L={:.4f}", fmt(center_), radius_, rad2deg(start_),
                       rad2deg(end_), length());
}
Vec3 ArcEntity::pointAtDistance(double s) const {
    const double a = start_ + std::clamp(s, 0.0, length()) / radius_;
    return pointAtAngle(a);
}
Vec3 ArcEntity::closestPoint(const Vec3& p) const {
    const double a = std::atan2(p.y - center_.y, p.x - center_.x);
    if (containsAngle(a)) return pointAtAngle(a);
    const Vec3 s = startPoint(), e = endPoint();
    return p.distanceTo(s) <= p.distanceTo(e) ? s : e;
}
void ArcEntity::writeDxf(DxfWriter& w) const {
    w.beginEntity("ARC", *this);
    w.point(10, center_);
    w.group(40, radius_);
    w.group(50, rad2deg(start_));
    w.group(51, rad2deg(end_));
}

// ---------------- Polyline ----------------
PolylineEntity::PolylineEntity(std::vector<Vec3> pts, bool closed)
    : pts_(std::move(pts)), bulges_(pts_.size(), 0.0), closed_(closed) {}

PolylineEntity::PolylineEntity(std::vector<Vec3> pts, std::vector<double> bulges, bool closed)
    : pts_(std::move(pts)), bulges_(std::move(bulges)), closed_(closed) {
    bulges_.resize(pts_.size(), 0.0);
}

std::size_t PolylineEntity::segmentCount() const {
    if (pts_.size() < 2) return 0;
    return closed_ ? pts_.size() : pts_.size() - 1;
}
bool PolylineEntity::hasBulges() const {
    for (std::size_t i = 0; i < segmentCount(); ++i)
        if (segIsArc(i)) return true;
    return false;
}
bool PolylineEntity::is3d() const {
    for (const auto& p : pts_)
        if (!almostEqual(p.z, pts_.front().z)) return true;
    return false;
}
bool PolylineEntity::segIsArc(std::size_t i) const {
    return std::abs(bulges_[i]) > 1e-12 && (segEnd(i) - segStart(i)).length2d() > kEps;
}
PolylineEntity::ArcSeg PolylineEntity::arcOf(std::size_t i) const {
    const Vec3 p = segStart(i), q = segEnd(i);
    const double b = bulges_[i];
    const double dx = q.x - p.x, dy = q.y - p.y;
    const double L = std::hypot(dx, dy);
    const double theta = 4.0 * std::atan(b);
    const double r = L / (2.0 * std::abs(std::sin(theta / 2.0)));
    const double off = (L / 2.0) * ((1.0 - b * b) / (2.0 * b));  // markazning vatar o'rtasidan chapga siljishi
    const Vec3 c{(p.x + q.x) / 2.0 - dy / L * off, (p.y + q.y) / 2.0 + dx / L * off, p.z};
    return {c, r, std::atan2(p.y - c.y, p.x - c.x), theta};
}
double PolylineEntity::segLength(std::size_t i, bool in3d) const {
    const Vec3 p = segStart(i), q = segEnd(i);
    if (!segIsArc(i)) return in3d ? p.distanceTo(q) : (q - p).length2d();
    const auto a = arcOf(i);
    const double l2 = a.radius * std::abs(a.sweep);
    return in3d ? std::hypot(l2, q.z - p.z) : l2;
}
Vec3 PolylineEntity::segPointAt(std::size_t i, double s) const {
    const Vec3 p = segStart(i), q = segEnd(i);
    const double L = segLength(i, true);
    const double t = L < kEps ? 0.0 : std::clamp(s / L, 0.0, 1.0);
    if (!segIsArc(i)) return p + (q - p) * t;
    const auto a = arcOf(i);
    const double ang = a.startAngle + a.sweep * t;
    return {a.center.x + a.radius * std::cos(ang), a.center.y + a.radius * std::sin(ang), p.z + (q.z - p.z) * t};
}
Vec3 PolylineEntity::segClosest(std::size_t i, const Vec3& pt) const {
    const Vec3 p = segStart(i), q = segEnd(i);
    if (!segIsArc(i)) return closestOnSegment(p, q, pt);
    const auto a = arcOf(i);
    const double ang = std::atan2(pt.y - a.center.y, pt.x - a.center.x);
    const double d = a.sweep > 0 ? normalizeAngle(ang - a.startAngle) : normalizeAngle(a.startAngle - ang);
    if (d <= std::abs(a.sweep) + 1e-12) {
        const double t = d / std::abs(a.sweep);
        return {a.center.x + a.radius * std::cos(ang), a.center.y + a.radius * std::sin(ang), p.z + (q.z - p.z) * t};
    }
    return pt.distanceTo(p) <= pt.distanceTo(q) ? p : q;
}

BBox PolylineEntity::bbox() const {
    BBox b;
    for (const auto& p : pts_) b.expand(p);
    for (std::size_t i = 0; i < segmentCount(); ++i) {
        if (!segIsArc(i)) continue;
        const auto a = arcOf(i);
        for (int k = 0; k < 4; ++k) {
            const double ang = k * kPi / 2.0;
            const double d = a.sweep > 0 ? normalizeAngle(ang - a.startAngle) : normalizeAngle(a.startAngle - ang);
            if (d <= std::abs(a.sweep)) b.expand(Vec3{a.center.x + a.radius * std::cos(ang), a.center.y + a.radius * std::sin(ang), a.center.z});
        }
    }
    return b;
}
void PolylineEntity::transform(const Transform& t) {
    if (!t.isConformal() && hasBulges()) *this = tessellated();  // affin cho'zilishda yoy ellipsga aylanadi
    for (auto& p : pts_) p = t.apply(p);
    if (t.isMirroring())
        for (auto& b : bulges_) b = -b;
}
double PolylineEntity::length() const {
    double L = 0;
    for (std::size_t i = 0; i < segmentCount(); ++i) L += segLength(i, true);
    return L;
}
double PolylineEntity::length2d() const {
    double L = 0;
    for (std::size_t i = 0; i < segmentCount(); ++i) L += segLength(i, false);
    return L;
}
double PolylineEntity::signedArea() const {
    if (!closed_ || pts_.size() < 2) return 0.0;
    double s = 0;
    for (std::size_t i = 0; i < pts_.size(); ++i) {
        const auto& p = pts_[i];
        const auto& q = pts_[(i + 1) % pts_.size()];
        s += p.x * q.y - q.x * p.y;
    }
    double a = s * 0.5;
    for (std::size_t i = 0; i < segmentCount(); ++i) {
        if (!segIsArc(i)) continue;
        const auto arc = arcOf(i);
        a += 0.5 * arc.radius * arc.radius * (arc.sweep - std::sin(arc.sweep));  // doira segmenti
    }
    return a;
}
double PolylineEntity::area() const { return std::abs(signedArea()); }

Vec3 PolylineEntity::pointAtDistance(double s) const {
    if (pts_.empty()) return {};
    s = std::max(0.0, s);
    for (std::size_t i = 0; i < segmentCount(); ++i) {
        const double L = segLength(i, true);
        if (s <= L) return segPointAt(i, s);
        s -= L;
    }
    return endPoint();
}
Vec3 PolylineEntity::closestPoint(const Vec3& p) const {
    if (pts_.empty()) return {};
    if (pts_.size() == 1) return pts_.front();
    Vec3 best = pts_.front();
    double bestD = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < segmentCount(); ++i) {
        const Vec3 c = segClosest(i, p);
        const double d = c.distanceTo(p);
        if (d < bestD) { bestD = d; best = c; }
    }
    return best;
}
PolylineEntity PolylineEntity::tessellated(double maxStep) const {
    std::vector<Vec3> out;
    for (std::size_t i = 0; i < segmentCount(); ++i) {
        out.push_back(segStart(i));
        if (!segIsArc(i)) continue;
        const auto a = arcOf(i);
        const int n = std::max(1, static_cast<int>(std::ceil(std::abs(a.sweep) / maxStep)));
        const double L = segLength(i, true);
        for (int k = 1; k < n; ++k) out.push_back(segPointAt(i, L * k / n));
    }
    if (!closed_ && !pts_.empty()) out.push_back(pts_.back());
    if (pts_.size() == 1) out = pts_;
    PolylineEntity r(std::move(out), closed_);
    r.setLayer(layer());
    r.setColor(color());
    return r;
}
std::string PolylineEntity::describe() const {
    auto s = std::format("POLYLINE n={} {} L={:.4f} L2d={:.4f}", pts_.size(), closed_ ? "closed" : "open", length(),
                         length2d());
    std::size_t arcs = 0;
    for (std::size_t i = 0; i < segmentCount(); ++i) arcs += segIsArc(i) ? 1 : 0;
    if (arcs) s += std::format(" arcs={}", arcs);
    if (closed_) s += std::format(" A={:.4f}", area());
    return s;
}
void PolylineEntity::writeDxf(DxfWriter& w) const {
    // R12: POLYLINE + VERTEX + SEQEND. 70-flag: 1=yopiq, 8=3D poliliniya. 3D'da bulge yo'q — yoylar bo'linadi.
    const bool d3 = is3d();
    if (d3 && hasBulges()) {
        tessellated().writeDxf(w);
        return;
    }
    w.beginEntity("POLYLINE", *this);
    w.group(66, 1);
    w.point(10, Vec3{0, 0, d3 ? 0.0 : (pts_.empty() ? 0.0 : pts_.front().z)});
    w.group(70, (closed_ ? 1 : 0) | (d3 ? 8 : 0));
    for (std::size_t i = 0; i < pts_.size(); ++i) {
        w.beginEntity("VERTEX", *this);
        w.point(10, pts_[i]);
        if (!d3 && std::abs(bulges_[i]) > 1e-12) w.group(42, bulges_[i]);
        if (d3) w.group(70, 32);
    }
    w.beginEntity("SEQEND", *this);
}

// ---------------- Text ----------------
double TextEntity::estimatedWidth() const { return height_ * 0.6 * static_cast<double>(utf8Length(text_)); }
Vec3 TextEntity::localOffsetToBaselineLeft() const {
    const double w = estimatedWidth();
    const double x = h_ == TextHAlign::Left ? 0.0 : h_ == TextHAlign::Center ? -w / 2.0 : -w;
    const double y = v_ == TextVAlign::Middle ? -height_ / 2.0 : v_ == TextVAlign::Top ? -height_ : 0.0;
    return {x, y, 0};
}
static Vec3 rotate2d(const Vec3& v, double a) {
    return {v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a), v.z};
}
BBox TextEntity::bbox() const {
    const Vec3 bl = ins_ + rotate2d(localOffsetToBaselineLeft(), rot_);
    const Vec3 dx = rotate2d({estimatedWidth(), 0, 0}, rot_);
    const Vec3 dy = rotate2d({0, height_, 0}, rot_);
    BBox b;
    b.expand(bl); b.expand(bl + dx); b.expand(bl + dy); b.expand(bl + dx + dy);
    return b;
}
void TextEntity::transform(const Transform& t) {
    ins_ = t.apply(ins_);
    height_ *= t.uniformScale();
    Vec3 dir = t.applyVector({std::cos(rot_), std::sin(rot_), 0});
    if (t.isMirroring()) {
        // MIRRTEXT=0 xulqi: matn o'qiladigan holatda qoladi, tekislash ko'zguga mos almashadi
        if (dir.x < -kEps) {
            dir = dir * -1.0;
            if (h_ == TextHAlign::Left) h_ = TextHAlign::Right;
            else if (h_ == TextHAlign::Right) h_ = TextHAlign::Left;
        } else {
            if (v_ == TextVAlign::Top) v_ = TextVAlign::Bottom;
            else if (v_ == TextVAlign::Baseline || v_ == TextVAlign::Bottom) v_ = TextVAlign::Top;
        }
    }
    rot_ = normalizeAngle(std::atan2(dir.y, dir.x));
}
std::string TextEntity::describe() const {
    static const char* hs[] = {"L", "C", "R"};
    static const char* vs[] = {"B", "Bot", "M", "T"};
    return std::format("TEXT \"{}\" at {} h={:.3f} rot={:.2f}° {}{}", text_, fmt(ins_), height_, rad2deg(rot_),
                       hs[static_cast<int>(h_)], vs[static_cast<int>(v_)]);
}
void TextEntity::writeDxf(DxfWriter& w) const {
    const bool aligned = h_ != TextHAlign::Left || v_ != TextVAlign::Baseline;
    w.beginEntity("TEXT", *this);
    w.point(10, aligned ? ins_ + rotate2d(localOffsetToBaselineLeft(), rot_) : ins_);
    w.group(40, height_);
    w.group(1, w.encode(text_));
    if (!almostEqual(rot_, 0.0)) w.group(50, rad2deg(rot_));
    if (aligned) {
        if (h_ != TextHAlign::Left) w.group(72, static_cast<int>(h_));
        w.point(11, ins_);
        if (v_ != TextVAlign::Baseline) w.group(73, static_cast<int>(v_));
    }
}

void TextEntity::writeDxfAttrib(DxfWriter& w, const std::string& tag) const {
    const bool aligned = h_ != TextHAlign::Left || v_ != TextVAlign::Baseline;
    w.beginEntity("ATTRIB", *this);
    w.point(10, aligned ? ins_ + rotate2d(localOffsetToBaselineLeft(), rot_) : ins_);
    w.group(40, height_);
    w.group(1, w.encode(text_));
    w.group(2, w.encode(tag));
    w.group(70, 0);
    if (!almostEqual(rot_, 0.0)) w.group(50, rad2deg(rot_));
    if (aligned) {
        if (h_ != TextHAlign::Left) w.group(72, static_cast<int>(h_));
        w.point(11, ins_);
        if (v_ != TextVAlign::Baseline) w.group(74, static_cast<int>(v_));
    }
}

// ---------------- Polyline: explode / ichki nuqta ----------------
std::vector<std::unique_ptr<IEntity>> PolylineEntity::explodeSegments() const {
    std::vector<std::unique_ptr<IEntity>> out;
    for (std::size_t i = 0; i < segmentCount(); ++i) {
        std::unique_ptr<IEntity> e;
        if (segIsArc(i)) {
            const auto a = arcOf(i);
            e = a.sweep > 0 ? std::make_unique<ArcEntity>(a.center, a.radius, a.startAngle, a.startAngle + a.sweep)
                            : std::make_unique<ArcEntity>(a.center, a.radius, a.startAngle + a.sweep, a.startAngle);
        } else {
            e = std::make_unique<LineEntity>(segStart(i), segEnd(i));
        }
        e->setLayer(layer());
        e->setColor(color());
        out.push_back(std::move(e));
    }
    return out;
}

bool PolylineEntity::containsPoint2d(const Vec3& p) const {
    const PolylineEntity flat = hasBulges() ? tessellated(deg2rad(2.0)) : *this;
    const auto& v = flat.vertices();
    bool inside = false;
    for (std::size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) {
        if ((v[i].y > p.y) != (v[j].y > p.y) &&
            p.x < (v[j].x - v[i].x) * (p.y - v[i].y) / (v[j].y - v[i].y) + v[i].x)
            inside = !inside;
    }
    return inside;
}

// ---------------- yordamchilar ----------------
std::unique_ptr<PolylineEntity> toBulgePolyline(const IEntity& e) {
    std::unique_ptr<PolylineEntity> p;
    if (auto* c = dynamic_cast<const CircleEntity*>(&e)) {
        const Vec3 o = c->center();
        p = std::make_unique<PolylineEntity>(std::vector<Vec3>{o + Vec3{c->radius(), 0, 0}, o - Vec3{c->radius(), 0, 0}},
                                             std::vector<double>{1.0, 1.0}, true);
    } else if (auto* a = dynamic_cast<const ArcEntity*>(&e)) {
        if (a->sweep() >= 2 * kPi - 1e-12) {
            CircleEntity full(a->center(), a->radius());
            p = toBulgePolyline(full);
        } else {
            p = std::make_unique<PolylineEntity>(std::vector<Vec3>{a->startPoint(), a->endPoint()},
                                                 std::vector<double>{std::tan(a->sweep() / 4.0), 0.0}, false);
        }
    } else if (auto* q = dynamic_cast<const PolylineEntity*>(&e)) {
        p = std::make_unique<PolylineEntity>(*q);
    }
    if (p) {
        p->setLayer(e.layer());
        p->setColor(e.color());
    }
    return p;
}

std::unique_ptr<IEntity> transformedCopy(const IEntity& e, const Transform& t) {
    if (!t.isConformal() && (e.type() == EntityType::Circle || e.type() == EntityType::Arc)) {
        auto p = toBulgePolyline(e);
        p->transform(t);
        return p;
    }
    auto c = e.clone();
    c->transform(t);
    return c;
}

// ---------------- Insert ----------------
Transform InsertEntity::placement(const Vec3& base, const Vec3& ins, double sx, double sy, double sz, double rot) {
    return Transform::translation(ins) * Transform::rotationAboutOrigin(rot) * Transform::scaling3(sx, sy, sz) *
           Transform::translation(base * -1.0);
}

bool InsertEntity::decompose(double& sx, double& sy, double& rot) const {
    sx = std::hypot(m_.a, m_.c);
    if (sx < 1e-15) return false;
    rot = std::atan2(m_.c, m_.a);
    const double cs = std::cos(rot), sn = std::sin(rot);
    const double ox = cs * m_.b + sn * m_.d;  // R(-rot)·(b,d) = (0, sy) bo'lishi kerak
    sy = -sn * m_.b + cs * m_.d;
    return std::abs(ox) <= 1e-9 * std::max(sx, std::abs(sy));
}

BBox InsertEntity::bbox() const {
    BBox b;
    for (const auto& e : def_->entities) b.expand(transformedCopy(*e, m_)->bbox());
    for (const auto& a : attribs_) b.expand(a.text.bbox());
    if (!b.valid()) b.expand(insertionPoint());
    return b;
}

void InsertEntity::transform(const Transform& t) {
    m_ = t * m_;
    for (auto& a : attribs_) a.text.transform(t);
}

std::vector<std::unique_ptr<IEntity>> InsertEntity::explode() const {
    std::vector<std::unique_ptr<IEntity>> out;
    for (const auto& e : def_->entities) {
        auto c = transformedCopy(*e, m_);
        if (c->layer() == "0") c->setLayer(layer());
        if (c->color() == kColorByBlock) c->setColor(color());
        out.push_back(std::move(c));
    }
    for (const auto& a : attribs_) out.push_back(a.text.clone());
    return out;
}

std::string InsertEntity::describe() const {
    double sx = 0, sy = 0, rot = 0;
    std::string s = std::format("INSERT \"{}\" at {}", def_->name, fmt(insertionPoint()));
    if (decompose(sx, sy, rot)) s += std::format(" scale=({:.4f}, {:.4f}) rot={:.2f}°", sx, sy, rad2deg(rot));
    else s += " skew";
    s += std::format(" objects={}", def_->entities.size());
    if (!attribs_.empty()) {
        s += " attribs:";
        for (const auto& a : attribs_) s += std::format(" {}={}", a.tag, a.text.text());
    }
    return s;
}

void InsertEntity::writeDxf(DxfWriter& w) const {
    double sx = 0, sy = 0, rot = 0;
    if (!decompose(sx, sy, rot)) {  // DXF INSERT qiyshiqlikni ifodalay olmaydi — explode qilib yoziladi
        for (const auto& e : explode()) e->writeDxf(w);
        return;
    }
    w.beginEntity("INSERT", *this);
    if (!attribs_.empty()) w.group(66, 1);
    w.group(2, w.encode(def_->name));
    w.point(10, insertionPoint());
    w.group(41, sx);
    w.group(42, sy);
    w.group(43, m_.sz);
    if (!almostEqual(rot, 0.0)) w.group(50, rad2deg(normalizeAngle(rot)));
    if (attribs_.empty()) return;
    for (const auto& a : attribs_) a.text.writeDxfAttrib(w, a.tag);
    w.beginEntity("SEQEND", *this);
}

// ---------------- Hatch ----------------
HatchEntity::HatchEntity(std::vector<PolylineEntity> loops, std::string pattern, double scale, double angle)
    : pattern_(std::move(pattern)), scale_(scale), angle_(angle) {
    for (auto& l : loops) {
        PolylineEntity closed(l.vertices(), l.bulges(), true);
        loops_.push_back(std::move(closed));
    }
    if (pattern_.empty()) pattern_ = "SOLID";
}

bool HatchEntity::isSolid() const {
    std::string u = pattern_;
    for (auto& c : u) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    return u == "SOLID";
}

double HatchEntity::area() const {
    double total = 0;
    for (std::size_t i = 0; i < loops_.size(); ++i) {
        if (loops_[i].vertices().size() < 2) continue;
        const Vec3 probe = loops_[i].pointAtDistance(loops_[i].length() * 1e-4);
        int depth = 0;
        for (std::size_t j = 0; j < loops_.size(); ++j)
            if (j != i && loops_[j].containsPoint2d(probe)) ++depth;
        total += (depth % 2 == 0 ? 1.0 : -1.0) * loops_[i].area();
    }
    return total;
}

BBox HatchEntity::bbox() const {
    BBox b;
    for (const auto& l : loops_) b.expand(l.bbox());
    return b;
}

void HatchEntity::transform(const Transform& t) {
    for (auto& l : loops_) l.transform(t);
    angle_ = normalizeAngle(angle_ + t.rotationAngle());
    scale_ *= t.uniformScale();
}

std::string HatchEntity::describe() const {
    return std::format("HATCH \"{}\" loops={} scale={:.3f} angle={:.2f}° A={:.4f}", pattern_, loops_.size(), scale_,
                       rad2deg(angle_), area());
}

void HatchEntity::writeDxf(DxfWriter& w) const {
    for (const auto& l : loops_) {
        PolylineEntity p = l;
        p.setLayer(layer());
        p.setColor(color());
        p.writeDxf(w);
    }
}

} // namespace asiacad
