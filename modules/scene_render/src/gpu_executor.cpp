#include "nativekit_scene_render.h"

#include <unordered_map>
#include <utility>
#include <vector>

namespace nkscene {

struct NativeKitGpuExecutor::State {
    std::unordered_map<GeometryId, std::uint64_t> geometry_revisions;
    std::unordered_map<MaterialId, std::uint64_t> material_revisions;
    std::vector<GpuCommand> commands;
};

NativeKitGpuExecutor::NativeKitGpuExecutor() : state_(std::make_unique<State>()) {}

NativeKitGpuExecutor::~NativeKitGpuExecutor() = default;

NativeKitGpuExecutor::NativeKitGpuExecutor(NativeKitGpuExecutor &&) noexcept = default;

NativeKitGpuExecutor &NativeKitGpuExecutor::operator=(NativeKitGpuExecutor &&) noexcept = default;

GpuExecutionStats NativeKitGpuExecutor::execute(const RenderPlan &plan,
                                                 const SceneSnapshot &snapshot) {
    GpuExecutionStats stats;
    for (const auto &resource : snapshot.geometries()) {
        const auto found = state_->geometry_revisions.find(resource.id);
        if (found == state_->geometry_revisions.end()) {
            state_->geometry_revisions.emplace(resource.id, resource.revision);
            ++stats.geometry_resources_created;
        } else if (found->second != resource.revision) {
            found->second = resource.revision;
            ++stats.geometry_resources_updated;
        }
    }
    for (const auto &resource : snapshot.materials()) {
        const auto found = state_->material_revisions.find(resource.id);
        if (found == state_->material_revisions.end()) {
            state_->material_revisions.emplace(resource.id, resource.revision);
            ++stats.material_resources_created;
        } else if (found->second != resource.revision) {
            found->second = resource.revision;
            ++stats.material_resources_updated;
        }
    }

    state_->commands.clear();
    state_->commands.reserve(plan.items().size());
    for (const auto &item : plan.items()) {
        if (has_render_flag(item.flags, RenderFlags::Hidden))
            continue;
        state_->commands.push_back({item.occurrence, item.geometry, item.material,
                                    item.transformIndex});
    }
    stats.commands = state_->commands.size();
    stats.draw_calls = stats.commands;
    return stats;
}

std::span<const GpuCommand> NativeKitGpuExecutor::commands() const noexcept {
    return state_->commands;
}

} // namespace nkscene

namespace nkscene::render_internal {

/* The NativeKit GPU executor is intentionally deferred until Phase 6. */

} // namespace nkscene::render_internal
