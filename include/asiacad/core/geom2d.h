#pragma once
// AsiaCAD — XY tekislikdagi geometrik primitivlar: kesma va yoy.
// Har qanday egri (LINE, ARC, CIRCLE, bulge'li POLYLINE, INSERT ichidagi geometriya, HATCH konturi)
// shu ikki turga ajratiladi; kesishish, eng yaqin nuqta, to'rtburchak bilan kesishish — shular ustida.
#include <asiacad/core/entity.h>

#include <vector>

namespace asiacad::geom {

struct Prim {
    enum class Kind { Seg, Arc } kind = Kind::Seg;
    Vec3 a, b;           // Seg: uchlari; Arc: boshi va oxiri
    Vec3 c;              // Arc: markaz
    double r = 0;        // Arc: radius
    double start = 0;    // Arc: boshlang'ich burchak
    double sweep = 0;    // Arc: ishorali (CCW > 0), to'liq aylana — 2π

    static Prim seg(const Vec3& a, const Vec3& b);
    static Prim arc(const Vec3& c, double r, double start, double sweep);
    BBox bbox() const;
    bool isFullCircle() const { return kind == Kind::Arc && std::abs(sweep) >= 2 * kPi - 1e-12; }
    // Burchak yoy ichidami (sweep yo'nalishida); t ∈ [0,1] parametrini qaytaradi
    bool containsAngle(double ang, double* t = nullptr) const;
    Vec3 pointAt(double t) const;  // t ∈ [0,1]
    Vec3 mid() const { return pointAt(0.5); }
};

// Entity'ni WCS'dagi primitivlarga ajratish (INSERT — rekursiv, HATCH — konturlar)
std::vector<Prim> primitivesOf(const IEntity& e);

// XY kesishish nuqtalari (z — birinchi primitivdan)
void intersect(const Prim& p, const Prim& q, std::vector<Vec3>& out);

Vec3 closestPoint(const Prim& p, const Vec3& pt);
double distance2d(const Prim& p, const Vec3& pt);
// Perpendikulyar asosi: kesma uchun cheksiz to'g'ri chiziqqa (AutoCAD PER), yoy uchun — yoy ustida bo'lsa
bool perpendicularFoot(const Prim& p, const Vec3& from, Vec3& out);
// Tashqi nuqtadan aylana/yoyga urinma nuqtalari
void tangentPoints(const Prim& p, const Vec3& from, std::vector<Vec3>& out);

// Primitiv to'rtburchakni kesadimi yoki uning ichidami (crossing tanlash)
bool hitsBox(const Prim& p, const BBox& box);

} // namespace asiacad::geom
