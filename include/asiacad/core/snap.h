#pragma once
// AsiaCAD — obyektga bog'lanish (OSNAP) va tanlash (oyna / kesib o'tuvchi / ko'rsatish).
// Ikkalasi ham Document::query (R-tree) orqali faqat yaqin obyektlarni ko'radi.
#include <asiacad/core/document.h>

#include <string_view>
#include <vector>

namespace asiacad {

enum SnapMode : unsigned {
    SnapEnd = 1u << 0,   // uchlar, poliliniya uchlari
    SnapMid = 1u << 1,   // o'rta (kesma va yoy)
    SnapCen = 1u << 2,   // aylana/yoy markazi
    SnapQua = 1u << 3,   // kvadrant (0°, 90°, 180°, 270°)
    SnapInt = 1u << 4,   // kesishish
    SnapPer = 1u << 5,   // perpendikulyar (oldingi nuqtadan)
    SnapTan = 1u << 6,   // urinma (oldingi nuqtadan)
    SnapNea = 1u << 7,   // eng yaqin nuqta egri ustida
    SnapNod = 1u << 8,   // POINT obyekti
    SnapIns = 1u << 9,   // TEXT/INSERT qo'yilish nuqtasi
    SnapAll = (1u << 10) - 1,
};

struct SnapResult {
    SnapMode mode;
    Vec3 point;
    EntityId id;     // qaysi obyektdan (INT — birinchisi)
    double distance; // kursorgacha (XY)
};

std::string_view snapName(SnapMode m);  // "END", "MID", ...
SnapMode snapFromName(std::string_view canonical);  // noma'lum — 0

// Kursor atrofidagi `aperture` radiusdagi barcha nomzodlar. Tartib: avval aniq turlar (NEA eng oxirida),
// so'ng masofa bo'yicha — birinchisi AutoCAD tanlaydigan nuqta. `from` — PER/TAN uchun oldingi nuqta.
std::vector<SnapResult> findSnaps(const Document& doc, const Vec3& cursor, double aperture, unsigned modes,
                                  const Vec3* from = nullptr);

// Oyna (window): obyekt to'liq ichida. Kesib o'tuvchi (crossing): obyekt geometriyasi to'rtburchakka tegadi.
std::vector<EntityId> selectWindow(const Document& doc, const BBox& box, bool crossing);
// Ko'rsatish (pick): `tol` ichidagi eng yaqin obyekt; topilmasa kInvalidId
EntityId pickEntity(const Document& doc, const Vec3& p, double tol);

} // namespace asiacad
