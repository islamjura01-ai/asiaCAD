#pragma once
// AsiaCAD — XY bo'yicha R-tree (STR bulk loading). Document ichida ishlatiladi:
// to'liq qurish O(n log n), so'rov O(log n + k). O'zgarishlar orasida qayta qurilmaydi —
// Document o'zgargan obyektlarni alohida "dirty" ro'yxatda ushlaydi (qarang: Document::query).
#include <asiacad/core/entity.h>

#include <vector>

namespace asiacad {

class SpatialIndex {
public:
    struct Item {
        BBox box;
        EntityId id;
    };

    void build(std::vector<Item> items);
    void clear() { nodes_.clear(); items_.clear(); root_ = -1; }
    // Qutisi `box` bilan XY'da kesishadigan barcha elementlar
    void query(const BBox& box, std::vector<EntityId>& out) const;
    std::size_t size() const { return items_.size(); }

private:
    static constexpr int kFanout = 16;
    struct Node {
        BBox box;
        int first = 0, count = 0;  // bolalar: leaf bo'lsa items_ da, aks holda nodes_ da
        bool leaf = true;
    };
    std::vector<Node> nodes_;
    std::vector<Item> items_;
    int root_ = -1;
};

} // namespace asiacad
