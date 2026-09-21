#include "render_internal.hpp"

#include <algorithm>

namespace nkscene::render_internal {

void rebuild_batches(RenderPlan &plan) {
    plan.batches_.clear();
    plan.batches_by_geometry_.clear();
    plan.batches_by_material_.clear();
    for (const auto &item : plan.items_) {
        auto found = std::find_if(
            plan.batches_.begin(), plan.batches_.end(), [&](const InstanceBatch &batch) {
                return batch.geometry == item.geometry && batch.material == item.material;
            });
        if (found == plan.batches_.end()) {
            plan.batches_.push_back({item.geometry, item.material, {item.occurrence}});
            const auto index = plan.batches_.size() - 1;
            plan.batches_by_geometry_[item.geometry].push_back(index);
            plan.batches_by_material_[item.material].push_back(index);
        } else {
            found->instances.push_back(item.occurrence);
        }
    }
}

} // namespace nkscene::render_internal
