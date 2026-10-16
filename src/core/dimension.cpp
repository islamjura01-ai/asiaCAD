#include <asiacad/core/dimension.h>

#include <asiacad/core/error.h>

#include <format>

namespace asiacad {

namespace {

Vec3 unit(const Vec3& v) {
    const double l = v.length2d();
    return l < 1e-15 ? Vec3{1, 0, 0} : Vec3{v.x / l, v.y / l, 0};
}
Vec3 perp(const Vec3& u) { return {-u.y, u.x, 0}; }
Vec3 rotate45(const Vec3& u) {
    const double c = std::sqrt(0.5);
    return {u.x * c - u.y * c, u.x * c + u.y * c, 0};
}

// Matn o'qilishi uchun burchak (-90°, 90°]
double readable(double a) {
    a = std::remainder(a, 2 * kPi);  // (-π, π]
    if (a > kPi / 2 + 1e-9) a -= kPi;
    if (a <= -kPi / 2 + 1e-9) a += kPi;
    return a;
}

std::string number(double v, int dec) {
    if (dec < 0) dec = std::abs(v - std::round(v)) < 5e-7 ? 0 : 2;  // avto: butun bo'lsa — kasrsiz
    std::string s = std::format("{:.{}f}", v, dec);
    for (auto& c : s) if (c == '.') c = ',';
    return s;
}

} // namespace

std::string_view toString(DimKind k) {
    switch (k) {
        case DimKind::Aligned: return "ALIGNED";
        case DimKind::Rotated: return "ROTATED";
        case DimKind::Radius: return "RADIUS";
        case DimKind::Diameter: return "DIAMETER";
    }
    return "ALIGNED";
}

bool dimKindFromString(std::string_view s, DimKind& out) {
    for (DimKind k : {DimKind::Aligned, DimKind::Rotated, DimKind::Radius, DimKind::Diameter})
        if (toString(k) == s) { out = k; return true; }
    return false;
}

DimensionEntity::DimensionEntity(DimKind kind, const Vec3& p1, const Vec3& p2, const Vec3& loc, double h, int dec,
                                 double rot)
    : kind_(kind), p1_(p1), p2_(p2), loc_(loc), h_(h), dec_(std::clamp(dec, -1, 6)), rot_(rot) {
    if (!(h_ > 0)) throw Error("err.height_positive");
    if (p1_.distanceTo(p2_) < 1e-12) throw Error("err.dim_zero");
}

DimensionEntity DimensionEntity::linear(const Vec3& p1, const Vec3& p2, const Vec3& loc, double h, int dec) {
    const double xmin = std::min(p1.x, p2.x), xmax = std::max(p1.x, p2.x);
    const double ymin = std::min(p1.y, p2.y), ymax = std::max(p1.y, p2.y);
    const bool insideX = loc.x >= xmin && loc.x <= xmax, insideY = loc.y >= ymin && loc.y <= ymax;
    bool horizontal;
    if (insideX && !insideY) horizontal = true;
    else if (insideY && !insideX) horizontal = false;
    else {
        const Vec3 m = (p1 + p2) * 0.5;
        horizontal = std::abs(loc.y - m.y) >= std::abs(loc.x - m.x);
    }
    return DimensionEntity(DimKind::Rotated, p1, p2, loc, h, dec, horizontal ? 0.0 : kPi / 2);
}

double DimensionEntity::measurement() const {
    switch (kind_) {
        case DimKind::Aligned:
        case DimKind::Diameter: return p1_.distanceTo(p2_);
        case DimKind::Radius: return (p2_ - p1_).length2d();
        case DimKind::Rotated: return std::abs((p2_ - p1_).dot({std::cos(rot_), std::sin(rot_), 0}));
    }
    return 0;
}

std::string DimensionEntity::text() const {
    const std::string v = number(measurement(), dec_);
    if (kind_ == DimKind::Radius) return "R" + v;
    if (kind_ == DimKind::Diameter) return "\xC3\x98" + v;  // Ø
    return v;
}

DimensionEntity DimensionEntity::withPoints(const Vec3& p1, const Vec3& p2, const Vec3& loc) const {
    DimensionEntity d(kind_, p1, p2, loc, h_, dec_, rot_);
    d.setLayer(layer());
    d.setColor(color());
    return d;
}

std::vector<std::unique_ptr<IEntity>> DimensionEntity::explode() const {
    std::vector<std::unique_ptr<IEntity>> out;
    auto line = [&](const Vec3& a, const Vec3& b) {
        if (a.distanceTo(b) > 1e-12) out.push_back(std::make_unique<LineEntity>(a, b));
    };
    auto arrow = [&](const Vec3& tip, const Vec3& dirOut) {  // uchi tip'da, dirOut — strelka qarab turgan tomon
        const Vec3 u = unit(dirOut), n = perp(u);
        const double len = h_ * 1.0, half = h_ / 6.0;
        const Vec3 base = tip - u * len;
        std::vector<PolylineEntity> loops;
        loops.emplace_back(std::vector<Vec3>{tip, base + n * half, base - n * half}, true);
        out.push_back(std::make_unique<HatchEntity>(std::move(loops), "SOLID"));
    };
    auto label = [&](const Vec3& a, const Vec3& b) {  // a-b kesma ustiga matn
        const Vec3 d = b - a;
        const double ang = readable(std::atan2(d.y, d.x));
        const Vec3 up = perp({std::cos(ang), std::sin(ang), 0});
        out.push_back(std::make_unique<TextEntity>((a + b) * 0.5 + up * (h_ * 0.35), h_, text(), ang, TextHAlign::Center,
                                                   TextVAlign::Bottom));
    };

    if (kind_ == DimKind::Aligned || kind_ == DimKind::Rotated) {
        const Vec3 u = kind_ == DimKind::Aligned ? unit(p2_ - p1_) : Vec3{std::cos(rot_), std::sin(rot_), 0};
        const Vec3 n = perp(u);
        const Vec3 d1 = p1_ + n * (loc_ - p1_).dot(n), d2 = p2_ + n * (loc_ - p2_).dot(n);
        const double gap = h_ * 0.3, ext = h_ * 0.5, over = h_ * 0.6, tick = h_ * 0.5;
        for (const auto& [p, d] : {std::pair{p1_, d1}, std::pair{p2_, d2}}) {  // chiqarish chiziqlari
            const Vec3 v = d - p;
            const double L = v.length2d();
            if (L < 1e-12) continue;
            const Vec3 dv = v / L;
            line(p + dv * std::min(gap, L), d + dv * ext);
        }
        const Vec3 ud = d1.distanceTo(d2) > 1e-12 ? unit(d2 - d1) : u;
        line(d1 - ud * over, d2 + ud * over);  // o'lcham chizig'i chiqarish chiziqlaridan biroz o'tadi
        const Vec3 t = rotate45(ud);
        line(d1 - t * tick, d1 + t * tick);  // 45° serif
        line(d2 - t * tick, d2 + t * tick);
        label(d1, d2);
    } else if (kind_ == DimKind::Radius) {
        const Vec3 c = p1_;
        const double r = measurement();
        const Vec3 dir = loc_.distanceTo(c) > 1e-12 ? unit(loc_ - c) : unit(p2_ - c);
        const Vec3 a = c + dir * r;
        const bool outside = (loc_ - c).length2d() > r;
        line(c, outside ? loc_ : a);
        arrow(a, dir);
        if (outside) label(a, loc_);
        else label(c, a);
    } else {  // Diameter
        const Vec3 c = (p1_ + p2_) * 0.5;
        const double r = measurement() / 2;
        const Vec3 dir = loc_.distanceTo(c) > 1e-12 ? unit(loc_ - c) : unit(p2_ - p1_);
        const Vec3 a1 = c - dir * r, a2 = c + dir * r;
        const bool outside = (loc_ - c).length2d() > r;
        line(a1, outside ? loc_ : a2);
        arrow(a1, dir * -1.0);
        arrow(a2, dir);
        if (outside) label(a2, loc_);
        else label(c, a2);
    }
    for (auto& e : out) {
        e->setLayer(layer());
        e->setColor(color());
    }
    return out;
}

BBox DimensionEntity::bbox() const {
    BBox b;
    for (const auto& e : explode()) b.expand(e->bbox());
    b.expand(p1_);
    b.expand(p2_);
    return b;
}

void DimensionEntity::transform(const Transform& t) {
    p1_ = t.apply(p1_);
    p2_ = t.apply(p2_);
    loc_ = t.apply(loc_);
    h_ *= t.uniformScale();
    if (kind_ == DimKind::Rotated) {
        const Vec3 u = t.applyVector({std::cos(rot_), std::sin(rot_), 0});
        rot_ = std::atan2(u.y, u.x);
    }
}

void DimensionEntity::writeDxf(DxfWriter& w) const {
    for (const auto& e : explode()) e->writeDxf(w);
}

std::string DimensionEntity::describe() const {
    return std::format("DIMENSION {} \"{}\" p1=({:.4f}, {:.4f}) p2=({:.4f}, {:.4f}) h={:.3f}", toString(kind_), text(),
                       p1_.x, p1_.y, p2_.x, p2_.y, h_);
}

} // namespace asiacad
