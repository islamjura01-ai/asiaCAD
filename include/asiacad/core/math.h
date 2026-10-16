#pragma once
// AsiaCAD — asosiy geometrik tiplar (Vec3, BBox, Transform)
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace asiacad {

inline constexpr double kEps = 1e-9;
inline constexpr double kPi = std::numbers::pi;

inline bool almostEqual(double a, double b, double eps = kEps) { return std::abs(a - b) <= eps; }
inline double deg2rad(double d) { return d * kPi / 180.0; }
inline double rad2deg(double r) { return r * 180.0 / kPi; }

// Burchakni [0, 2π) oralig'iga keltirish
inline double normalizeAngle(double a) {
    a = std::fmod(a, 2.0 * kPi);
    if (a < 0) a += 2.0 * kPi;
    if (a >= 2.0 * kPi - 1e-12) a = 0.0;  // -1e-16 kabi shovqin 2π ga aylanmasin
    return a;
}

struct Vec3 {
    double x = 0, y = 0, z = 0;

    constexpr Vec3() = default;
    constexpr Vec3(double x_, double y_, double z_ = 0) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(double s) const { return {x * s, y * s, z * s}; }
    Vec3 operator/(double s) const { return {x / s, y / s, z / s}; }
    Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }

    double dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3 cross(const Vec3& o) const { return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x}; }
    double length() const { return std::sqrt(dot(*this)); }
    double length2d() const { return std::hypot(x, y); }
    double distanceTo(const Vec3& o) const { return (*this - o).length(); }

    bool equals(const Vec3& o, double eps = kEps) const {
        return almostEqual(x, o.x, eps) && almostEqual(y, o.y, eps) && almostEqual(z, o.z, eps);
    }
};

struct BBox {
    Vec3 min{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max()};
    Vec3 max{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest()};

    bool valid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    void expand(const Vec3& p) {
        min = {std::min(min.x, p.x), std::min(min.y, p.y), std::min(min.z, p.z)};
        max = {std::max(max.x, p.x), std::max(max.y, p.y), std::max(max.z, p.z)};
    }
    void expand(const BBox& b) {
        if (b.valid()) { expand(b.min); expand(b.max); }
    }
    bool intersects2d(const BBox& b) const {
        return valid() && b.valid() && min.x <= b.max.x && max.x >= b.min.x && min.y <= b.max.y && max.y >= b.min.y;
    }
};

// XY tekislikda affin almashtirish (+ Z siljitish). CAD MOVE/ROTATE/SCALE/MIRROR uchun yetarli.
//  | a  b  tx |
//  | c  d  ty |      z' = z * sz + tz
struct Transform {
    double a = 1, b = 0, c = 0, d = 1, tx = 0, ty = 0;
    double sz = 1, tz = 0;

    static Transform translation(const Vec3& v) {
        Transform t; t.tx = v.x; t.ty = v.y; t.tz = v.z; return t;
    }
    static Transform rotation(const Vec3& center, double angleRad) {
        const double cs = std::cos(angleRad), sn = std::sin(angleRad);
        Transform t;
        t.a = cs; t.b = -sn; t.c = sn; t.d = cs;
        t.tx = center.x - cs * center.x + sn * center.y;
        t.ty = center.y - sn * center.x - cs * center.y;
        return t;
    }
    static Transform scaling(const Vec3& base, double s) {
        Transform t;
        t.a = s; t.d = s; t.sz = s;
        t.tx = base.x * (1 - s); t.ty = base.y * (1 - s); t.tz = base.z * (1 - s);
        return t;
    }
    // p1-p2 chizig'iga nisbatan ko'zgu
    static Transform mirror(const Vec3& p1, const Vec3& p2) {
        const double ang = std::atan2(p2.y - p1.y, p2.x - p1.x);
        const double c2 = std::cos(2 * ang), s2 = std::sin(2 * ang);
        Transform t;
        t.a = c2; t.b = s2; t.c = s2; t.d = -c2;
        t.tx = p1.x - (c2 * p1.x + s2 * p1.y);
        t.ty = p1.y - (s2 * p1.x - c2 * p1.y);
        return t;
    }

    // Koordinata boshiga nisbatan har o'q bo'yicha alohida masshtab (blok INSERT uchun)
    static Transform scaling3(double sx, double sy, double szz) {
        Transform t;
        t.a = sx; t.d = sy; t.sz = szz;
        return t;
    }
    static Transform rotationAboutOrigin(double angleRad) { return rotation({0, 0, 0}, angleRad); }

    // Kompozitsiya: (A * B).apply(p) == A.apply(B.apply(p))
    Transform operator*(const Transform& o) const {
        Transform r;
        r.a = a * o.a + b * o.c;
        r.b = a * o.b + b * o.d;
        r.c = c * o.a + d * o.c;
        r.d = c * o.b + d * o.d;
        r.tx = a * o.tx + b * o.ty + tx;
        r.ty = c * o.tx + d * o.ty + ty;
        r.sz = sz * o.sz;
        r.tz = sz * o.tz + tz;
        return r;
    }

    // Burchaklarni saqlaydimi (aylana aylanaligicha qoladimi): R·s yoki ko'zgu·R·s
    bool isConformal(double eps = 1e-9) const {
        const double n1 = a * a + c * c, n2 = b * b + d * d;
        const double scale = std::max({n1, n2, 1e-300});
        return std::abs(n1 - n2) <= eps * scale && std::abs(a * b + c * d) <= eps * scale;
    }

    Vec3 apply(const Vec3& p) const {
        return {a * p.x + b * p.y + tx, c * p.x + d * p.y + ty, p.z * sz + tz};
    }
    Vec3 applyVector(const Vec3& v) const { return {a * v.x + b * v.y, c * v.x + d * v.y, v.z * sz}; }

    double determinant() const { return a * d - b * c; }
    bool isMirroring() const { return determinant() < 0; }
    // Bir xil masshtab koeffitsienti (rotation*uniform scale uchun aniq)
    double uniformScale() const { return std::sqrt(std::abs(determinant())); }
    // X o'qining burilish burchagi
    double rotationAngle() const { return std::atan2(c, a); }
};

} // namespace asiacad
