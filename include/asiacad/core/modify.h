#pragma once
// AsiaCAD — tahrir amallari geometriyasi: OFFSET, TRIM, EXTEND, FILLET.
// Funksiyalar hujjatni o'zgartirmaydi — yangi obyektlar qaytaradi (qatlam/rang manbadan); buyruq qatlami
// ularni ReplaceEntityCommand / AddEntityCommand / DeleteCommand orqali bitta undo qadamida qo'llaydi.
// Qo'llab-quvvatlanadi: LINE, ARC, CIRCLE, POLYLINE (bulge'li ham). Xatolar — Error(kalit).
#include <asiacad/core/entities.h>
#include <asiacad/core/geom2d.h>

#include <memory>
#include <vector>

namespace asiacad {

// OFFSET: `side` nuqtasi tomoniga d masofada parallel nusxa (d > 0).
//   Poliliniya: to'g'ri segmentlar burchakda kesishguncha uzaytiriladi (miter), yoy segmentlari konsentrik.
std::unique_ptr<IEntity> offsetEntity(const IEntity& e, double d, const Vec3& side);

// TRIM: `pick` yaqinidagi qism eng yaqin ikki kesishish (kesuvchi `edges` bilan) orasida olib tashlanadi.
//   Natija: 0, 1 yoki 2 bo'lak. Yopiq egri (aylana, yopiq poliliniya) uchun kamida 2 kesishish kerak.
std::vector<std::unique_ptr<IEntity>> trimEntity(const IEntity& e, const std::vector<geom::Prim>& edges, const Vec3& pick);

// EXTEND: `pick`ga yaqin uchi chegara (`edges`) bilan birinchi kesishishgacha uzaytiriladi.
std::unique_ptr<IEntity> extendEntity(const IEntity& e, const std::vector<geom::Prim>& edges, const Vec3& pick);

// FILLET: ikki chiziq tutashtiriladi. pick1/pick2 — saqlanadigan tomonlar. r = 0 — o'tkir burchak.
struct FilletResult {
    std::unique_ptr<IEntity> first, second;
    std::unique_ptr<IEntity> arc;  // r = 0 bo'lsa nullptr
};
FilletResult filletLines(const LineEntity& l1, const Vec3& pick1, const LineEntity& l2, const Vec3& pick2, double r);

} // namespace asiacad
