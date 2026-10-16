#pragma once
// AsiaCAD — Document: entity'lar, qatlamlar (layer) va ID boshqaruvi
#include <asiacad/core/block.h>
#include <asiacad/core/entity.h>
#include <asiacad/core/spatial_index.h>

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace asiacad {

struct Layer {
    std::string name;
    int color = 7;  // ACI: 7 = oq/qora
    bool visible = true;
    bool locked = false;
    bool frozen = false;
};

class Document {
public:
    Document();

    // ---- Entity'lar ----
    // Yangi ID beradi. Entity qatlami mavjud bo'lmasa avtomatik yaratiladi.
    EntityId add(std::unique_ptr<IEntity> e);
    // Fayldan yuklash uchun: berilgan ID bilan oxiriga qo'shadi (ID band yoki 0 bo'lsa Error)
    void addWithId(std::unique_ptr<IEntity> e, EntityId id);
    void reserveIds(EntityId next) { nextId_ = std::max(nextId_, next); }
    // Undo/Redo uchun: entity'ni avvalgi ID va tartib pozitsiyasi bilan qaytaradi
    void restore(std::unique_ptr<IEntity> e, std::size_t orderIndex);
    // O'chiradi va egalikni qaytaradi (undo uchun). Topilmasa nullptr.
    std::unique_ptr<IEntity> take(EntityId id, std::size_t* orderIndexOut = nullptr);
    // Obyektni o'sha ID va tartib o'rnida almashtiradi, eskisini qaytaradi (tutqich tahriri, undo)
    std::unique_ptr<IEntity> replace(EntityId id, std::unique_ptr<IEntity> e);

    IEntity* find(EntityId id);
    const IEntity* find(EntityId id) const;
    bool contains(EntityId id) const { return entities_.count(id) != 0; }
    std::size_t size() const { return order_.size(); }

    // Chizilish tartibida aylanish
    void forEach(const std::function<void(const IEntity&)>& fn) const;
    std::vector<EntityId> ids() const { return order_; }
    std::vector<EntityId> idsOnLayer(const std::string& layer) const;

    BBox extents(bool visibleOnly = true) const;

    // ---- Fazoviy so'rov (R-tree, avtomatik sinxronlanadi) ----
    // Qutisi `box` bilan kesishadigan obyektlar (XY). visibleOnly — o'chiq/muzlatilgan qatlamlarsiz.
    std::vector<EntityId> query(const BBox& box, bool visibleOnly = true) const;
    // Joyida o'zgartirilgan obyekt haqida xabar (TransformCommand va h.k.) — indeks yangilanadi
    void noteChanged(EntityId id) { pending_.push_back(id); }
    bool isVisible(EntityId id) const;

    // ---- Qatlamlar ----
    bool addLayer(const Layer& l);  // mavjud bo'lsa false
    std::optional<Layer> layer(const std::string& name) const;
    bool setLayer(const Layer& l);  // mavjudini yangilaydi
    bool removeLayer(const std::string& name);  // "0" va bo'sh bo'lmagan qatlam o'chirilmaydi
    const std::map<std::string, Layer>& layers() const { return layers_; }
    bool isEditable(EntityId id) const;  // qatlam locked/frozen emasligi

    // ---- Bloklar (nom katta-kichik harfga sezgir emas) ----
    bool addBlock(BlockPtr b);  // nom band bo'lsa false; blok qatlamlari yo'q bo'lsa yaratiladi
    bool removeBlock(const std::string& name);
    BlockPtr block(const std::string& name) const;
    const std::map<std::string, BlockPtr>& blocks() const { return blocks_; }  // kalit — katta harfdagi nom
    std::size_t blockUsage(const std::string& name) const;  // modelda nechta INSERT

    EntityId peekNextId() const { return nextId_; }

private:
    std::unordered_map<EntityId, std::unique_ptr<IEntity>> entities_;
    std::vector<EntityId> order_;
    std::map<std::string, Layer> layers_;
    std::map<std::string, BlockPtr> blocks_;
    EntityId nextId_ = 1;

    // Indeks: qurilgan daraxt + undan keyin o'zgarganlar (stale_ — daraxtdagi eskirgan yozuvlar,
    // dirty_ — ularning hozirgi qutilari). O'zgarishlar ko'payganda daraxt qayta quriladi.
    void syncIndex() const;
    mutable SpatialIndex tree_;
    mutable bool treeBuilt_ = false;
    mutable std::vector<EntityId> pending_;
    mutable std::unordered_set<EntityId> stale_;
    mutable std::unordered_map<EntityId, BBox> dirty_;
};

} // namespace asiacad
