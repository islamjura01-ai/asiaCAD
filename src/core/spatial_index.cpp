#include <asiacad/core/spatial_index.h>

#include <algorithm>
#include <cmath>

namespace asiacad {

namespace {
double cx(const BBox& b) { return (b.min.x + b.max.x) * 0.5; }
double cy(const BBox& b) { return (b.min.y + b.max.y) * 0.5; }

// STR: x bo'yicha tilimlarga, har tilim ichida y bo'yicha tartiblab, kFanout'dan guruhlash
template <class T, class GetBox>
void strSort(std::vector<T>& v, int fanout, GetBox box) {
    const std::size_t n = v.size();
    const std::size_t leaves = (n + fanout - 1) / fanout;
    const std::size_t slices = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(leaves))));
    const std::size_t perSlice = slices * fanout;
    std::sort(v.begin(), v.end(), [&](const T& a, const T& b) { return cx(box(a)) < cx(box(b)); });
    for (std::size_t s = 0; s < n; s += perSlice) {
        const auto e = v.begin() + static_cast<std::ptrdiff_t>(std::min(n, s + perSlice));
        std::sort(v.begin() + static_cast<std::ptrdiff_t>(s), e, [&](const T& a, const T& b) { return cy(box(a)) < cy(box(b)); });
    }
}
} // namespace

void SpatialIndex::build(std::vector<Item> items) {
    clear();
    items_ = std::move(items);
    if (items_.empty()) return;
    strSort(items_, kFanout, [](const Item& it) -> const BBox& { return it.box; });

    // Barglar
    std::vector<Node> level;
    for (std::size_t i = 0; i < items_.size(); i += kFanout) {
        Node n;
        n.first = static_cast<int>(i);
        n.count = static_cast<int>(std::min<std::size_t>(kFanout, items_.size() - i));
        n.leaf = true;
        for (int k = 0; k < n.count; ++k) n.box.expand(items_[i + static_cast<std::size_t>(k)].box);
        level.push_back(n);
    }
    // Yuqori darajalar: har daraja nodes_ oxiriga qo'shiladi
    while (level.size() > 1) {
        strSort(level, kFanout, [](const Node& n) -> const BBox& { return n.box; });
        const int base = static_cast<int>(nodes_.size());
        nodes_.insert(nodes_.end(), level.begin(), level.end());
        std::vector<Node> up;
        for (std::size_t i = 0; i < level.size(); i += kFanout) {
            Node n;
            n.first = base + static_cast<int>(i);
            n.count = static_cast<int>(std::min<std::size_t>(kFanout, level.size() - i));
            n.leaf = false;
            for (int k = 0; k < n.count; ++k) n.box.expand(level[i + static_cast<std::size_t>(k)].box);
            up.push_back(n);
        }
        level = std::move(up);
    }
    root_ = static_cast<int>(nodes_.size());
    nodes_.push_back(level.front());
}

void SpatialIndex::query(const BBox& box, std::vector<EntityId>& out) const {
    if (root_ < 0) return;
    int stack[256];
    int sp = 0;
    stack[sp++] = root_;
    while (sp > 0) {
        const Node& n = nodes_[static_cast<std::size_t>(stack[--sp])];
        if (!n.box.intersects2d(box)) continue;
        if (n.leaf) {
            for (int k = 0; k < n.count; ++k) {
                const Item& it = items_[static_cast<std::size_t>(n.first + k)];
                if (it.box.intersects2d(box)) out.push_back(it.id);
            }
        } else {
            for (int k = 0; k < n.count; ++k) stack[sp++] = n.first + k;
        }
    }
}

} // namespace asiacad
