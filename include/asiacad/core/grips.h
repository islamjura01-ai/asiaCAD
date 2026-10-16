#pragma once
// AsiaCAD — tutqichlar (grips): tanlangan obyektning tahrir nuqtalari.
//   LINE     0 boshi, 1 oxiri, 2 o'rtasi (butun chiziqni siljitadi)
//   CIRCLE   0 markaz (siljitish), 1..4 kvadrantlar (radius)
//   ARC      0 markaz (siljitish), 1 boshi, 2 o'rtasi, 3 oxiri (uch nuqta orqali qayta quriladi)
//   POLYLINE har bir uch (bulge saqlanadi)
//   POINT / TEXT / INSERT — joylashuv nuqtasi (siljitish)
//   HATCH    barcha konturlarning uchlari ketma-ket
//   DIMENSION 0 p1, 1 p2, 2 o'lcham chizig'i joyi
#include <asiacad/core/commands.h>
#include <asiacad/core/entity.h>

#include <memory>
#include <vector>

namespace asiacad {

std::vector<Vec3> gripPoints(const IEntity& e);
// Yangi obyekt (ID'siz, qatlam/rang saqlangan). Indeks noto'g'ri — Error(core.grip_index),
// geometriya buziladi (nol radius, kollinear yoy) — Error(core.grip_invalid).
std::unique_ptr<IEntity> withGripMoved(const IEntity& e, std::size_t index, const Vec3& to);

// Obyektni o'sha ID va tartib o'rnida boshqasiga almashtirish (undo — eskisini qaytarish)
class ReplaceEntityCommand final : public ICommand {
public:
    ReplaceEntityCommand(EntityId id, std::unique_ptr<IEntity> repl, std::string name = "GRIP")
        : id_(id), other_(std::move(repl)), name_(std::move(name)) {}
    void execute(Document& doc) override;
    void undo(Document& doc) override;
    std::string name() const override { return name_; }

private:
    EntityId id_;
    std::unique_ptr<IEntity> other_;  // hujjatda bo'lmagan versiya
    std::string name_;
};

} // namespace asiacad
