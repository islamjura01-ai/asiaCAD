#include <asiacad/core/document.h>

#include <cctype>
#include <asiacad/core/entities.h>
#include <asiacad/core/error.h>
#include <asiacad/i18n/utf8.h>

#include <algorithm>
#include <stdexcept>

namespace asiacad {

Document::Document() { layers_["0"] = Layer{"0", 7}; }

EntityId Document::add(std::unique_ptr<IEntity> e) {
    if (!e) throw std::invalid_argument("Document::add: null entity");
    if (!layers_.count(e->layer())) layers_[e->layer()] = Layer{e->layer()};
    const EntityId id = nextId_++;
    e->id_ = id;
    order_.push_back(id);
    entities_.emplace(id, std::move(e));
    pending_.push_back(id);
    return id;
}

void Document::addWithId(std::unique_ptr<IEntity> e, EntityId id) {
    if (!e) throw std::invalid_argument("Document::addWithId: null entity");
    if (id == kInvalidId || entities_.count(id)) throw Error("acad.err.duplicate_id", {std::to_string(id)});
    if (!layers_.count(e->layer())) layers_[e->layer()] = Layer{e->layer()};
    e->id_ = id;
    order_.push_back(id);
    entities_.emplace(id, std::move(e));
    pending_.push_back(id);
    nextId_ = std::max(nextId_, id + 1);
}

void Document::restore(std::unique_ptr<IEntity> e, std::size_t orderIndex) {
    if (!e || e->id_ == kInvalidId) throw std::invalid_argument("Document::restore: invalid entity");
    if (entities_.count(e->id_)) throw std::logic_error("Document::restore: ID band");
    if (!layers_.count(e->layer())) layers_[e->layer()] = Layer{e->layer()};
    const EntityId id = e->id_;
    order_.insert(order_.begin() + static_cast<std::ptrdiff_t>(std::min(orderIndex, order_.size())), id);
    entities_.emplace(id, std::move(e));
    pending_.push_back(id);
    nextId_ = std::max(nextId_, id + 1);
}

std::unique_ptr<IEntity> Document::take(EntityId id, std::size_t* orderIndexOut) {
    auto it = entities_.find(id);
    if (it == entities_.end()) return nullptr;
    auto pos = std::find(order_.begin(), order_.end(), id);
    if (orderIndexOut) *orderIndexOut = static_cast<std::size_t>(pos - order_.begin());
    order_.erase(pos);
    auto e = std::move(it->second);
    entities_.erase(it);
    pending_.push_back(id);
    return e;
}

std::unique_ptr<IEntity> Document::replace(EntityId id, std::unique_ptr<IEntity> e) {
    if (!e) throw std::invalid_argument("Document::replace: null");
    std::size_t idx = 0;
    auto old = take(id, &idx);
    if (!old) throw std::invalid_argument("Document::replace: not found");
    e->id_ = id;
    restore(std::move(e), idx);
    return old;
}

IEntity* Document::find(EntityId id) {
    auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : it->second.get();
}
const IEntity* Document::find(EntityId id) const {
    auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : it->second.get();
}

void Document::forEach(const std::function<void(const IEntity&)>& fn) const {
    for (EntityId id : order_) fn(*entities_.at(id));
}

std::vector<EntityId> Document::idsOnLayer(const std::string& layer) const {
    std::vector<EntityId> out;
    for (EntityId id : order_)
        if (entities_.at(id)->layer() == layer) out.push_back(id);
    return out;
}

BBox Document::extents(bool visibleOnly) const {
    BBox b;
    for (EntityId id : order_) {
        const auto& e = *entities_.at(id);
        if (visibleOnly) {
            const auto& l = layers_.at(e.layer());
            if (!l.visible || l.frozen) continue;
        }
        b.expand(e.bbox());
    }
    return b;
}

bool Document::addLayer(const Layer& l) {
    if (l.name.empty() || layers_.count(l.name)) return false;
    layers_[l.name] = l;
    return true;
}
std::optional<Layer> Document::layer(const std::string& name) const {
    auto it = layers_.find(name);
    if (it == layers_.end()) return std::nullopt;
    return it->second;
}
bool Document::setLayer(const Layer& l) {
    auto it = layers_.find(l.name);
    if (it == layers_.end()) return false;
    it->second = l;
    return true;
}
bool Document::removeLayer(const std::string& name) {
    if (name == "0" || !layers_.count(name) || !idsOnLayer(name).empty()) return false;
    layers_.erase(name);
    return true;
}
bool Document::isVisible(EntityId id) const {
    const IEntity* e = find(id);
    if (!e) return false;
    auto it = layers_.find(e->layer());
    return it == layers_.end() || (it->second.visible && !it->second.frozen);
}

void Document::syncIndex() const {
    if (!treeBuilt_ || pending_.size() + dirty_.size() > std::max<std::size_t>(1024, entities_.size() / 8)) {
        std::vector<SpatialIndex::Item> items;
        items.reserve(entities_.size());
        for (const auto& [id, e] : entities_) items.push_back({e->bbox(), id});
        tree_.build(std::move(items));
        treeBuilt_ = true;
        pending_.clear();
        stale_.clear();
        dirty_.clear();
        return;
    }
    for (EntityId id : pending_) {
        stale_.insert(id);
        if (const IEntity* e = find(id)) dirty_[id] = e->bbox();
        else dirty_.erase(id);
    }
    pending_.clear();
}

std::vector<EntityId> Document::query(const BBox& box, bool visibleOnly) const {
    syncIndex();
    std::vector<EntityId> raw, out;
    tree_.query(box, raw);
    for (EntityId id : raw)
        if (!stale_.count(id)) out.push_back(id);
    for (const auto& [id, b] : dirty_)
        if (b.intersects2d(box)) out.push_back(id);
    if (visibleOnly) std::erase_if(out, [&](EntityId id) { return !isVisible(id); });
    return out;
}

bool Document::addBlock(BlockPtr b) {
    if (!b || b->name.empty()) return false;
    const std::string key = utf8::toUpper(b->name);
    if (blocks_.count(key)) return false;
    for (const auto& e : b->entities)
        if (!layers_.count(e->layer())) layers_[e->layer()] = Layer{e->layer()};
    blocks_.emplace(key, std::move(b));
    return true;
}
bool Document::removeBlock(const std::string& name) { return blocks_.erase(utf8::toUpper(name)) != 0; }
BlockPtr Document::block(const std::string& name) const {
    auto it = blocks_.find(utf8::toUpper(name));
    return it == blocks_.end() ? nullptr : it->second;
}
std::size_t Document::blockUsage(const std::string& name) const {
    const BlockPtr b = block(name);
    std::size_t n = 0;
    for (const auto& [id, e] : entities_)
        if (auto* ins = dynamic_cast<const InsertEntity*>(e.get()); ins && ins->block() == b) ++n;
    return n;
}

bool Document::isEditable(EntityId id) const {
    const IEntity* e = find(id);
    if (!e) return false;
    const auto& l = layers_.at(e->layer());
    return !l.locked && !l.frozen;
}

const Sheet* Document::sheet(const std::string& name) const {
    auto up = [](std::string v) { for (auto& c : v) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); return v; };
    for (const auto& s : sheets_)
        if (up(s.name) == up(name)) return &s;
    return nullptr;
}

} // namespace asiacad
