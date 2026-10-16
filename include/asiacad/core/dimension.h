#pragma once
// AsiaCAD — o'lcham (DIMENSION). GOST 2.307 / SPDS uslubi: chiqarish chiziqlari, o'lcham chizig'i,
// 45° qiya chiziqcha (serif), radius/diametrda strelka, matn o'lcham chizig'i ustida.
//   Aligned  — p1-p2 ga parallel
//   Rotated  — berilgan burchak bo'yicha proyeksiya (DIMLINEAR: 0° gorizontal / 90° vertikal)
//   Radius   — p1 markaz, p2 aylanadagi nuqta
//   Diameter — p1, p2 diametr uchlari
// Ko'rinish explode() orqali oddiy obyektlardan quriladi (chizish, DXF R12 eksport, tanlash).
#include <asiacad/core/entities.h>

#include <memory>
#include <string>
#include <vector>

namespace asiacad {

enum class DimKind { Aligned, Rotated, Radius, Diameter };

std::string_view toString(DimKind k);
bool dimKindFromString(std::string_view s, DimKind& out);

class DimensionEntity final : public IEntity {
public:
    DimensionEntity(DimKind kind, const Vec3& p1, const Vec3& p2, const Vec3& loc, double textHeight,
                    int decimals = -1, double rotationRad = 0.0);  // decimals -1 — avto
    // DIMLINEAR: joylashuv nuqtasiga qarab gorizontal yoki vertikal
    static DimensionEntity linear(const Vec3& p1, const Vec3& p2, const Vec3& loc, double textHeight, int decimals = -1);

    EntityType type() const override { return EntityType::Dimension; }
    std::unique_ptr<IEntity> clone() const override { return std::make_unique<DimensionEntity>(*this); }
    BBox bbox() const override;
    void transform(const Transform& t) override;
    void writeDxf(DxfWriter& w) const override;  // R12: tayyor geometriya sifatida
    std::string describe() const override;

    DimKind kind() const { return kind_; }
    const Vec3& p1() const { return p1_; }
    const Vec3& p2() const { return p2_; }
    const Vec3& location() const { return loc_; }
    double textHeight() const { return h_; }
    int decimals() const { return dec_; }
    double rotation() const { return rot_; }

    double measurement() const;
    std::string text() const;  // "1250", "R25", "Ø50" (o'nli ajratgich — vergul, GOST)

    std::vector<std::unique_ptr<IEntity>> explode() const;
    // Shu turdagi yangi nusxa: bitta tayanch nuqta almashtirilgan (tutqich uchun)
    DimensionEntity withPoints(const Vec3& p1, const Vec3& p2, const Vec3& loc) const;

private:
    DimKind kind_;
    Vec3 p1_, p2_, loc_;
    double h_;
    int dec_;
    double rot_;
};

} // namespace asiacad
