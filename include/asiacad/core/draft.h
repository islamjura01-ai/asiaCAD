#pragma once
// AsiaCAD — chizmachilik asboblari (AutoCAD "Home" lentasidagi Draw / Modify / Annotation to'plami):
//  Draw:     RECTANG (yumaloq/faska burchak), POLYGON (ichki/tashqi), ELLIPSE, SPLINE (silliq egri — fit nuqtalar orqali)
//  Modify:   STRETCH (kesuvchi oyna), ARRAY (to'g'ri burchakli / qutbiy / yo'l bo'ylab), CHAMFER, JOIN, BREAK,
//            LENGTHEN (uchini uzaytirish/qisqartirish)
//  Annotate: LEADER (strelka + matn), DIMANGULAR (burchak o'lchami)
// Funksiyalar hujjatni o'zgartirmaydi — yangi obyektlar yoki almashtirishlar qaytaradi; buyruq qatlami undo bilan qo'llaydi.
#include <asiacad/core/entities.h>

#include <memory>
#include <optional>
#include <vector>

namespace asiacad {

using EntityList = std::vector<std::unique_ptr<IEntity>>;

// ---- Draw
// r > 0 — yumaloq burchaklar (bulge), chamfer > 0 — faska; ikkalasi 0 — o'tkir
std::unique_ptr<PolylineEntity> makeRectangle(const Vec3& p1, const Vec3& p2, double r = 0, double chamfer = 0);
// n tomonli muntazam ko'pburchak: radius — markazdan uchgacha (inscribed=true) yoki tomongacha (false); rot — birinchi uch burchagi
std::unique_ptr<PolylineEntity> makePolygon(const Vec3& c, int n, double radius, bool inscribed, double rotRad = 0);
// Ellips (poliliniya yaqinlashuvi, segmentlar soni — aniqlik 1/1000 dan yaxshi)
std::unique_ptr<PolylineEntity> makeEllipse(const Vec3& c, double a, double b, double rotRad = 0, int segments = 96);
// Silliq egri: centripetal Catmull–Rom, fit nuqtalardan o'tadi
std::unique_ptr<PolylineEntity> makeSpline(const std::vector<Vec3>& fit, bool closed = false, int perSpan = 16);

// ---- Modify
// STRETCH: kesuvchi oyna [lo, hi] ichidagi uchlar d ga suriladi; obyekt butunlay ichida bo'lsa — to'liq suriladi.
// Ta'sir qilmasa nullptr.
std::unique_ptr<IEntity> stretchEntity(const IEntity& e, const Vec3& lo, const Vec3& hi, const Vec3& d);
// ARRAY: nusxalar uchun almashtirishlar (asl nusxa kirmaydi)
std::vector<Transform> arrayRect(int rows, int cols, double dx, double dy);
// rotateItems=false — obyekt burilmaydi, base (tanlov markazi) aylana bo'ylab ko'chadi
std::vector<Transform> arrayPolar(const Vec3& center, int count, double fillDeg = 360, bool rotateItems = true, const Vec3& base = {});
// Yo'l bo'ylab: base — tanlov tayanch nuqtasi; count ta (boshidan oxirigacha teng) yoki spacing > 0; align — yo'nalish bo'yicha burish
std::vector<Transform> arrayPath(const IEntity& path, const Vec3& base, int count, double spacing = 0, bool align = true);
// CHAMFER: ikki chiziq — d1, d2 masofada kesiladi (d=0 — burchakkacha)
struct ChamferResult {
    std::unique_ptr<IEntity> first, second, bevel;
};
ChamferResult chamferLines(const LineEntity& l1, const Vec3& pick1, const LineEntity& l2, const Vec3& pick2, double d1, double d2);
// JOIN: uchlari tutashgan chiziq/yoy/poliliniyalardan bitta poliliniya (tol ichida); bitta zanjir bo'lmasa — nullopt
std::unique_ptr<PolylineEntity> joinCurves(const std::vector<const IEntity*>& parts, double tol = 1e-6);
// BREAK: p1..p2 orasidagi qism olib tashlanadi (p2 = p1 — nuqtada ikkiga bo'linadi). LINE, ARC, CIRCLE, POLYLINE.
EntityList breakCurve(const IEntity& e, const Vec3& p1, const Vec3& p2);
// LENGTHEN: pick yaqinidagi uchini delta ga uzaytirish (manfiy — qisqartirish). LINE, ARC, ochiq POLYLINE.
std::unique_ptr<IEntity> lengthenCurve(const IEntity& e, const Vec3& pick, double delta);

// ---- Annotate
EntityList makeLeader(const std::vector<Vec3>& pts, const std::string& text, double textHeight);
// Burchak o'lchami: uch (vertex), ikki nur nuqtasi, yoy joyi; matn — gradus (0.1° yoki ° ' ")
EntityList makeAngularDim(const Vec3& vertex, const Vec3& p1, const Vec3& p2, const Vec3& loc, double textHeight, bool dms = false);

} // namespace asiacad
