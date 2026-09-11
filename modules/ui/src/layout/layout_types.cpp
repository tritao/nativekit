#include "layout/layout_types.h"

#include <algorithm>

namespace nkui {

const LayoutItem *LayoutSnapshot::find(uint32_t id) const {
    const auto found = std::find_if(items.begin(), items.end(), [id](const LayoutItem &item) {
        return item.id == id;
    });
    return found == items.end() ? nullptr : &*found;
}

std::optional<uint32_t> LayoutSnapshot::hit_test(float x, float y) const {
    const auto contains = [x, y](const LayoutItem &item) {
        return x >= item.bounds.x && y >= item.bounds.y &&
               x < item.bounds.x + item.bounds.width &&
               y < item.bounds.y + item.bounds.height;
    };
    for (auto item = items.rbegin(); item != items.rend(); ++item) {
        if (item->kind == LayoutNodeKind::Button && contains(*item))
            return item->id;
    }
    for (auto item = items.rbegin(); item != items.rend(); ++item) {
        if (contains(*item))
            return item->id;
    }
    return std::nullopt;
}

} // namespace nkui
