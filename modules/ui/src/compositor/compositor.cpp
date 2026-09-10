#include "compositor.h"

#include <cstring>
#include <vector>

namespace nkui {
namespace {

struct Layer {
    ResourceId parent_target{};
    ResourceId layer_target{};
    float opacity = 1.0f;
    bool isolated = false;
};

bool fail(CompositorError *error, uint32_t index, const char *message) {
    if (error)
        *error = {index, message};
    return false;
}

template <class T> T read(const uint8_t *record) {
    T value{};
    std::memcpy(&value, record, sizeof(value));
    return value;
}

RenderPass &continue_pass(RenderPlan &plan, ResourceId target) {
    const bool seen = [&] {
        for (const auto &pass : plan.passes)
            if (pass.target.value == target.value)
                return true;
        return false;
    }();
    plan.passes.push_back({target, seen, {}});
    return plan.passes.back();
}

} // namespace

ResourceId Compositor::allocate_transient_target() {
    if (!next_target_slot_)
        next_target_slot_ = 0x8000;
    return make_resource_id(ResourceKind::RenderTarget, 1, next_target_slot_++);
}

bool Compositor::compile(const DisplayList &display_list, ResourceId main_target, RenderPlan &plan,
                         CompositorError *error) {
    ValidationError validation{};
    if (!is_resource_id(main_target, ResourceKind::RenderTarget) ||
        !validate_display_list(display_list.data(), display_list.size(), &validation))
        return fail(error, validation.command_index, "invalid compositor input");
    plan = {};
    std::vector<Layer> layers;
    ResourceId current_target = main_target;
    RenderPass *pass = &continue_pass(plan, current_target);
    size_t offset = 0;
    uint32_t index = 0;
    while (offset < display_list.size()) {
        const auto header = read<CommandHeader>(display_list.data() + offset);
        const uint8_t *record = display_list.data() + offset;
        switch (header.opcode) {
        case CommandOpcode::DrawPath: {
            const auto value = read<DrawResourceCommand>(record);
            pass->commands.push_back({RenderCommandKind::Path, value.resource});
            break;
        }
        case CommandOpcode::DrawImage: {
            const auto value = read<DrawRectResourceCommand>(record);
            pass->commands.push_back({RenderCommandKind::Image, value.resource, value.x, value.y,
                                      value.width, value.height});
            break;
        }
        case CommandOpcode::DrawTextLayout: {
            const auto value = read<DrawRectResourceCommand>(record);
            pass->commands.push_back({RenderCommandKind::GlyphBatch, value.resource, value.x,
                                      value.y, value.width, value.height});
            break;
        }
        case CommandOpcode::DrawRenderTarget: {
            const auto value = read<DrawRectResourceCommand>(record);
            if (value.resource.value == current_target.value)
                return fail(error, index, "render target cannot sample itself");
            pass->commands.push_back({RenderCommandKind::CompositeTarget, value.resource, value.x,
                                      value.y, value.width, value.height});
            plan.dependencies.push_back({value.resource, current_target});
            break;
        }
        case CommandOpcode::BeginLayer: {
            const auto value = read<BeginLayerCommand>(record);
            const bool isolated = value.opacity < 1.0f;
            const ResourceId parent_target = current_target;
            ResourceId layer_target = current_target;
            if (isolated) {
                layer_target = allocate_transient_target();
                current_target = layer_target;
                pass = &continue_pass(plan, current_target);
            }
            layers.push_back({parent_target, layer_target, value.opacity, isolated});
            break;
        }
        case CommandOpcode::EndLayer: {
            const Layer layer = layers.back();
            layers.pop_back();
            if (layer.isolated) {
                current_target = layer.parent_target;
                pass = &continue_pass(plan, current_target);
                pass->commands.push_back({RenderCommandKind::CompositeTarget, layer.layer_target,
                                          0.0f, 0.0f, 0.0f, 0.0f, layer.opacity});
                plan.dependencies.push_back({layer.layer_target, current_target});
            }
            break;
        }
        default:
            break;
        }
        offset += header.size;
        ++index;
    }
    if (error)
        *error = {};
    return true;
}

} // namespace nkui
