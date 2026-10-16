#pragma once
// AsiaCAD — blok ta'rifi. Ta'rif yaratilgandan keyin o'zgarmaydi (shared_ptr<const> orqali ulashiladi):
// barcha INSERT'lar bitta ob'ektga ishora qiladi, nusxalash arzon.
#include <asiacad/core/entity.h>

#include <memory>
#include <string>
#include <vector>

namespace asiacad {

struct BlockDef {
    std::string name;
    Vec3 base;  // blok koordinatalaridagi tayanch nuqta
    // Blok koordinatalarida. Qatlam "0" va rang BYBLOCK (0) — INSERT'dan meros oladi.
    std::vector<std::unique_ptr<IEntity>> entities;

    bool isAnonymous() const { return !name.empty() && name.front() == '*'; }
};

using BlockPtr = std::shared_ptr<const BlockDef>;

} // namespace asiacad
