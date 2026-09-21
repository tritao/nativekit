#include "render_internal.hpp"

#include <algorithm>
#include <cassert>

namespace nkscene::render_internal {

void rebuild_batches(RenderPlan &plan) {
    plan.batches_.clear();
    plan.batches_by_geometry_.clear();
    plan.batches_by_material_.clear();
    plan.batch_by_key_.clear();
    plan.item_batch_.assign(plan.items_.size(), RenderPlan::invalid_item_index);
    plan.item_batch_position_.assign(plan.items_.size(), RenderPlan::invalid_item_index);
    plan.batch_by_key_.reserve(plan.items_.size());
    for (std::size_t item_index = 0; item_index < plan.items_.size(); ++item_index) {
        const auto &item = plan.items_[item_index];
        const RenderPlan::BatchKey key{item.geometry, item.material};
        const auto [found, inserted] = plan.batch_by_key_.try_emplace(key, plan.batches_.size());
        if (inserted) {
            plan.batches_.push_back({item.geometry, item.material, {}});
            plan.batches_by_geometry_[item.geometry].push_back(found->second);
            plan.batches_by_material_[item.material].push_back(found->second);
        }
        auto &instances = plan.batches_[found->second].instances;
        plan.item_batch_[item_index] = found->second;
        plan.item_batch_position_[item_index] = instances.size();
        instances.push_back(item.occurrence);
    }
}

namespace {

template <class Key>
void remove_batch_index(std::unordered_map<Key, std::vector<std::size_t>> &index, Key key,
                        std::size_t batch_index) {
    const auto found = index.find(key);
    if (found == index.end())
        return;
    auto &batches = found->second;
    batches.erase(std::remove(batches.begin(), batches.end(), batch_index), batches.end());
    if (batches.empty())
        index.erase(found);
}

template <class Key>
void replace_batch_index(std::unordered_map<Key, std::vector<std::size_t>> &index, Key key,
                         std::size_t old_index, std::size_t new_index) {
    const auto found = index.find(key);
    if (found == index.end())
        return;
    for (auto &index_value : found->second)
        if (index_value == old_index)
            index_value = new_index;
}

} // namespace

std::size_t move_item_batch(RenderPlan &plan, std::size_t item_index, GeometryId geometry,
                            MaterialId material) {
    assert(item_index < plan.items_.size());
    const auto old_batch_index = plan.item_batch_[item_index];
    const auto old_key = RenderPlan::BatchKey{plan.batches_[old_batch_index].geometry,
                                              plan.batches_[old_batch_index].material};
    const RenderPlan::BatchKey new_key{geometry, material};
    if (old_key == new_key)
        return 0;

    auto &old_instances = plan.batches_[old_batch_index].instances;
    const auto old_position = plan.item_batch_position_[item_index];
    assert(old_position < old_instances.size());
    const auto moved_occurrence = old_instances.back();
    old_instances[old_position] = moved_occurrence;
    old_instances.pop_back();
    if (moved_occurrence != plan.items_[item_index].occurrence) {
        const auto moved_item_index = plan.item_index(moved_occurrence);
        assert(moved_item_index != RenderPlan::invalid_item_index);
        plan.item_batch_position_[moved_item_index] = old_position;
    }

    if (old_instances.empty()) {
        remove_batch_index(plan.batches_by_geometry_, old_key.geometry, old_batch_index);
        remove_batch_index(plan.batches_by_material_, old_key.material, old_batch_index);
        plan.batch_by_key_.erase(old_key);
        const auto last_batch_index = plan.batches_.size() - 1;
        if (old_batch_index != last_batch_index) {
            const auto moved_key =
                RenderPlan::BatchKey{plan.batches_.back().geometry, plan.batches_.back().material};
            plan.batches_[old_batch_index] = std::move(plan.batches_.back());
            plan.batch_by_key_[moved_key] = old_batch_index;
            replace_batch_index(plan.batches_by_geometry_, moved_key.geometry, last_batch_index,
                                old_batch_index);
            replace_batch_index(plan.batches_by_material_, moved_key.material, last_batch_index,
                                old_batch_index);
            for (const auto occurrence : plan.batches_[old_batch_index].instances) {
                const auto moved_item_index = plan.item_index(occurrence);
                assert(moved_item_index != RenderPlan::invalid_item_index);
                plan.item_batch_[moved_item_index] = old_batch_index;
            }
        }
        plan.batches_.pop_back();
    }

    const auto [found, inserted] = plan.batch_by_key_.try_emplace(new_key, plan.batches_.size());
    if (inserted) {
        plan.batches_.push_back({geometry, material, {}});
        plan.batches_by_geometry_[geometry].push_back(found->second);
        plan.batches_by_material_[material].push_back(found->second);
    }
    auto &new_instances = plan.batches_[found->second].instances;
    plan.item_batch_[item_index] = found->second;
    plan.item_batch_position_[item_index] = new_instances.size();
    new_instances.push_back(plan.items_[item_index].occurrence);
    return 2;
}

} // namespace nkscene::render_internal
