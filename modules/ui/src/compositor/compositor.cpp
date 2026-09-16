#include "compositor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace nkui {
namespace {

struct Layer {
    ResourceId parent_target{};
    ResourceId layer_target{};
    float opacity = 1.0f;
    CompositeMode mode = CompositeMode::SourceOver;
    LayerBounds bounds{};
    bool has_bounds = false;
    float parent_origin_x = 0.0f;
    float parent_origin_y = 0.0f;
    RenderTargetDescriptor target_descriptor{};
    EffectDescriptor effect{};
    CustomEffectDescriptor custom_effect{};
    bool has_effect = false;
    MaskDescriptor mask{};
    bool has_mask = false;
    EffectDescriptor backdrop_effect{};
    bool has_backdrop = false;
    bool isolated = false;
};

struct LayerCommandValues {
    float opacity = 1.0f;
    CompositeMode mode = CompositeMode::SourceOver;
    LayerBounds bounds{};
    uint32_t flags = 0;
    bool has_bounds = false;
    EffectDescriptor effect{};
    CustomEffectDescriptor custom_effect{};
    bool has_effect = false;
    MaskDescriptor mask{};
    bool has_mask = false;
    EffectDescriptor backdrop_effect{};
    bool has_backdrop = false;
};

struct CanvasState {
    std::array<float, 6> transform{1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    float alpha = 1.0f;
    ResourceId paint{};
    CompositeMode composite = CompositeMode::SourceOver;
    bool has_scissor = false;
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
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

RenderPass &continue_pass(RenderPlan &plan, ResourceId target,
                          const RenderTargetDescriptor &descriptor = {}) {
    const bool seen = [&] {
        for (const auto &pass : plan.passes)
            if (pass.target.value == target.value)
                return true;
        return false;
    }();
    plan.passes.push_back({target, descriptor, seen, {}});
    return plan.passes.back();
}

void add_dependency(RenderPlan &plan, ResourceId producer, ResourceId consumer) {
    const auto found = std::find_if(plan.dependencies.begin(), plan.dependencies.end(),
                                    [producer, consumer](const RenderDependency &dependency) {
                                        return dependency.producer.value == producer.value &&
                                               dependency.consumer.value == consumer.value;
                                    });
    if (found == plan.dependencies.end())
        plan.dependencies.push_back({producer, consumer});
}

void apply_state(RenderCommand &command, const CanvasState &state, float target_origin_x,
                 float target_origin_y) {
    command.opacity *= state.alpha;
    command.transform = state.transform;
    // Commands retain their logical coordinates; only the target-local
    // transform and clip need to account for a bounded layer origin.
    command.transform[4] -= target_origin_x;
    command.transform[5] -= target_origin_y;
    command.paint = state.paint;
    command.composite = state.composite;
    command.has_scissor = state.has_scissor;
    command.scissor_x = state.x - target_origin_x;
    command.scissor_y = state.y - target_origin_y;
    command.scissor_width = state.width;
    command.scissor_height = state.height;
}

constexpr uint64_t kCacheHashOffset = UINT64_C(1469598103934665603);
constexpr uint64_t kCacheHashPrime = UINT64_C(1099511628211);

void hash_u32(uint64_t &hash, uint32_t value) {
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        hash ^= static_cast<uint8_t>(value >> shift);
        hash *= kCacheHashPrime;
    }
}

void hash_u64(uint64_t &hash, uint64_t value) {
    hash_u32(hash, static_cast<uint32_t>(value));
    hash_u32(hash, static_cast<uint32_t>(value >> 32));
}

void hash_float(uint64_t &hash, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    hash_u32(hash, bits);
}

void hash_command(uint64_t &hash, const RenderCommand &command) {
    hash_u32(hash, static_cast<uint32_t>(command.kind));
    hash_u32(hash, command.resource.value);
    hash_float(hash, command.x);
    hash_float(hash, command.y);
    hash_float(hash, command.width);
    hash_float(hash, command.height);
    hash_float(hash, command.opacity);
    for (const float value : command.transform)
        hash_float(hash, value);
    hash_u32(hash, command.paint.value);
    hash_u32(hash, static_cast<uint32_t>(command.composite));
    hash_u32(hash, command.has_scissor ? 1u : 0u);
    hash_float(hash, command.scissor_x);
    hash_float(hash, command.scissor_y);
    hash_float(hash, command.scissor_width);
    hash_float(hash, command.scissor_height);
    hash_float(hash, command.stroke_width);
    hash_u32(hash, command.line_cap);
    hash_u32(hash, command.line_join);
    hash_float(hash, command.miter_limit);
    hash_u32(hash, command.custom_payload ? 1u : 0u);
    hash_u64(hash, command.content_generation);
}

void hash_descriptor(uint64_t &hash, const RenderTargetDescriptor &descriptor) {
    hash_u32(hash, static_cast<uint32_t>(descriptor.width));
    hash_u32(hash, static_cast<uint32_t>(descriptor.height));
    hash_float(hash, descriptor.logical_width);
    hash_float(hash, descriptor.logical_height);
    hash_float(hash, descriptor.origin_x);
    hash_float(hash, descriptor.origin_y);
    hash_u32(hash, static_cast<uint32_t>(descriptor.format));
    hash_u32(hash, descriptor.sample_count);
    hash_u32(hash, descriptor.usage);
}

void hash_effect(uint64_t &hash, const RenderPass &pass) {
    hash_u32(hash, static_cast<uint32_t>(pass.effect.kind));
    for (const float value : pass.effect.color_matrix)
        hash_float(hash, value);
    hash_u32(hash, pass.custom_effect.registration_id);
    hash_u32(hash, pass.custom_effect.parameter_count);
    hash_u32(hash, pass.custom_effect.pass_count);
    hash_u32(hash, pass.custom_effect.sampling_inputs);
    for (const float value : pass.custom_effect.parameters)
        hash_float(hash, value);
    for (const float value : pass.custom_effect.ink_overflow)
        hash_float(hash, value);
    hash_u32(hash, pass.backdrop ? 1u : 0u);
    hash_u32(hash, pass.has_input_rect ? 1u : 0u);
    for (const float value : pass.input_rect)
        hash_float(hash, value);
}

void hash_mask(uint64_t &hash, const MaskDescriptor &mask) {
    hash_u32(hash, static_cast<uint32_t>(mask.kind));
    hash_u32(hash, mask.image.value);
    for (const float value : mask.values)
        hash_float(hash, value);
}

uint64_t target_content_hash(const std::unordered_map<uint32_t, uint64_t> &targets,
                             ResourceId target) {
    const auto found = targets.find(target.value);
    if (found != targets.end())
        return found->second;
    uint64_t hash = kCacheHashOffset;
    hash_u32(hash, target.value);
    return hash;
}

void assign_effect_cache_keys(RenderPlan &plan) {
    std::unordered_map<uint32_t, uint64_t> target_hashes;
    for (auto &pass : plan.passes) {
        if (pass.kind == RenderPassKind::Draw) {
            uint64_t hash = kCacheHashOffset;
            hash_u32(hash, static_cast<uint32_t>(pass.kind));
            hash_descriptor(hash, pass.target_descriptor);
            for (const auto &command : pass.commands)
                hash_command(hash, command);
            const auto previous = target_hashes.find(pass.target.value);
            if (previous != target_hashes.end())
                hash_u64(hash, previous->second);
            target_hashes[pass.target.value] = hash;
            continue;
        }
        if (pass.kind == RenderPassKind::Effect) {
            uint64_t hash = kCacheHashOffset;
            hash_u32(hash, static_cast<uint32_t>(pass.kind));
            hash_u64(hash, target_content_hash(target_hashes, pass.input_target));
            hash_effect(hash, pass);
            hash_descriptor(hash, pass.target_descriptor);
            target_hashes[pass.target.value] = pass.cache_key = hash ? hash : 1;
            continue;
        }
        uint64_t hash = kCacheHashOffset;
        hash_u32(hash, static_cast<uint32_t>(pass.kind));
        hash_u64(hash, target_content_hash(target_hashes, pass.input_target));
        hash_mask(hash, pass.mask);
        hash_descriptor(hash, pass.target_descriptor);
        target_hashes[pass.target.value] = hash;
    }
}

bool read_layer(const uint8_t *record, uint32_t size, LayerCommandValues &result) {
    if (size == sizeof(BeginLayerCustomEffectCommand)) {
        const auto value = read<BeginLayerCustomEffectCommand>(record);
        result.opacity = value.opacity;
        result.mode = value.mode;
        result.bounds = {value.x, value.y, value.width, value.height};
        result.flags = value.flags;
        result.has_bounds = (value.flags & LayerHasBounds) != 0;
        result.effect.kind = EffectKind::Custom;
        result.custom_effect = value.effect;
        result.has_effect = true;
        return true;
    }
    if (size == sizeof(BeginLayerBackdropCommand)) {
        const auto value = read<BeginLayerBackdropCommand>(record);
        result.opacity = value.opacity;
        result.mode = value.mode;
        result.bounds = {value.x, value.y, value.width, value.height};
        result.flags = value.flags;
        result.has_bounds = (value.flags & LayerHasBounds) != 0;
        result.effect = value.effect;
        result.has_effect = value.effect.kind != EffectKind::None;
        result.mask = value.mask;
        result.has_mask = value.mask.kind != MaskKind::None;
        result.backdrop_effect = value.backdrop_effect;
        result.has_backdrop = value.backdrop_effect.kind != EffectKind::None;
        return true;
    }
    if (size == sizeof(BeginLayerMaskCommand)) {
        const auto value = read<BeginLayerMaskCommand>(record);
        result.opacity = value.opacity;
        result.mode = value.mode;
        result.bounds = {value.x, value.y, value.width, value.height};
        result.flags = value.flags;
        result.has_bounds = (value.flags & LayerHasBounds) != 0;
        result.effect = value.effect;
        result.has_effect = value.effect.kind != EffectKind::None;
        result.mask = value.mask;
        result.has_mask = value.mask.kind != MaskKind::None;
        return true;
    }
    if (size == sizeof(BeginLayerEffectCommand)) {
        const auto value = read<BeginLayerEffectCommand>(record);
        result.opacity = value.opacity;
        result.mode = value.mode;
        result.bounds = {value.x, value.y, value.width, value.height};
        result.flags = value.flags;
        result.has_bounds = (value.flags & LayerHasBounds) != 0;
        result.effect = value.effect;
        result.has_effect = value.effect.kind != EffectKind::None;
        return true;
    }
    if (size == sizeof(BeginLayerCommand)) {
        const auto value = read<BeginLayerCommand>(record);
        result.opacity = value.opacity;
        result.mode = value.mode;
        result.bounds = {value.x, value.y, value.width, value.height};
        result.flags = value.flags;
        result.has_bounds = (value.flags & LayerHasBounds) != 0;
        return true;
    }
    if (size == sizeof(LegacyBeginLayerCommand)) {
        const auto value = read<LegacyBeginLayerCommand>(record);
        result.opacity = value.opacity;
        result.mode = value.mode;
        return true;
    }
    return false;
}

bool intersect_clip(CanvasState &state, const ClipRectCommand &clip) {
    const auto &m = state.transform;
    const float corners[4][2] = {{clip.x, clip.y},
                                 {clip.x + clip.width, clip.y},
                                 {clip.x, clip.y + clip.height},
                                 {clip.x + clip.width, clip.y + clip.height}};
    float min_x = INFINITY, min_y = INFINITY, max_x = -INFINITY, max_y = -INFINITY;
    for (const auto &corner : corners) {
        const float x = corner[0] * m[0] + corner[1] * m[2] + m[4];
        const float y = corner[0] * m[1] + corner[1] * m[3] + m[5];
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
    }
    if (!std::isfinite(min_x) || !std::isfinite(min_y) || !std::isfinite(max_x) ||
        !std::isfinite(max_y))
        return false;
    if (state.has_scissor) {
        min_x = std::max(min_x, state.x);
        min_y = std::max(min_y, state.y);
        max_x = std::min(max_x, state.x + state.width);
        max_y = std::min(max_y, state.y + state.height);
    }
    state.has_scissor = true;
    state.x = min_x;
    state.y = min_y;
    state.width = std::max(0.0f, max_x - min_x);
    state.height = std::max(0.0f, max_y - min_y);
    return true;
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
    // Transient IDs are frame-local structural slots. Reusing them across compilations lets the
    // backend pool matching GPU targets instead of growing one target per transaction.
    next_target_slot_ = (static_cast<uint16_t>(main_target.value) == 0x8000u) ? 0x8001 : 0x8000;
    std::vector<Layer> layers;
    CanvasState state;
    std::vector<CanvasState> states;
    ResourceId current_target = main_target;
    float current_origin_x = 0.0f;
    float current_origin_y = 0.0f;
    RenderPass *pass = &continue_pass(plan, current_target);
    size_t offset = 0;
    uint32_t index = 0;
    while (offset < display_list.size()) {
        const auto header = read<CommandHeader>(display_list.data() + offset);
        const uint8_t *record = display_list.data() + offset;
        switch (header.opcode) {
        case CommandOpcode::SetTransform: {
            const auto value = read<SetTransformCommand>(record);
            std::copy(std::begin(value.matrix), std::end(value.matrix), state.transform.begin());
            break;
        }
        case CommandOpcode::SetGlobalAlpha: {
            state.alpha = read<SetGlobalAlphaCommand>(record).alpha;
            break;
        }
        case CommandOpcode::SetPaint:
            state.paint = read<SetPaintCommand>(record).paint;
            break;
        case CommandOpcode::SetCompositeMode:
            state.composite = read<SetCompositeModeCommand>(record).mode;
            break;
        case CommandOpcode::PushState:
            states.push_back(state);
            break;
        case CommandOpcode::PopState:
            state = states.back();
            states.pop_back();
            break;
        case CommandOpcode::ClipRect:
            if (!intersect_clip(state, read<ClipRectCommand>(record)))
                return fail(error, index, "clip transform overflow");
            break;
        case CommandOpcode::DrawPath: {
            const auto value = read<DrawResourceCommand>(record);
            pass->commands.push_back({RenderCommandKind::Path, value.resource});
            apply_state(pass->commands.back(), state, current_origin_x, current_origin_y);
            break;
        }
        case CommandOpcode::StrokePath: {
            const auto value = read<StrokePathCommand>(record);
            pass->commands.push_back({RenderCommandKind::StrokePath, value.path});
            auto &command = pass->commands.back();
            command.stroke_width = value.width;
            command.line_cap = value.line_cap;
            command.line_join = value.line_join;
            command.miter_limit = value.miter_limit;
            apply_state(command, state, current_origin_x, current_origin_y);
            break;
        }
        case CommandOpcode::DrawImage: {
            const auto value = read<DrawRectResourceCommand>(record);
            pass->commands.push_back({RenderCommandKind::Image, value.resource, value.x, value.y,
                                      value.width, value.height});
            apply_state(pass->commands.back(), state, current_origin_x, current_origin_y);
            break;
        }
        case CommandOpcode::DrawTextLayout: {
            const auto value = read<DrawRectResourceCommand>(record);
            pass->commands.push_back({RenderCommandKind::GlyphBatch, value.resource, value.x,
                                      value.y, value.width, value.height});
            apply_state(pass->commands.back(), state, current_origin_x, current_origin_y);
            break;
        }
        case CommandOpcode::DrawRenderTarget: {
            const auto value = read<DrawRectResourceCommand>(record);
            if (value.resource.value == current_target.value)
                return fail(error, index, "render target cannot sample itself");
            pass->commands.push_back({RenderCommandKind::CompositeTarget, value.resource, value.x,
                                      value.y, value.width, value.height});
            apply_state(pass->commands.back(), state, current_origin_x, current_origin_y);
            add_dependency(plan, value.resource, current_target);
            break;
        }
        case CommandOpcode::BeginLayer: {
            LayerCommandValues value{};
            if (!read_layer(record, header.size, value))
                return fail(error, index, "invalid layer begin");
            const LayerBounds backdrop_bounds = value.bounds;
            if (value.has_bounds && value.has_effect && value.effect.kind == EffectKind::Custom) {
                const auto &overflow = value.custom_effect.ink_overflow;
                if (!std::isfinite(value.bounds.x - overflow[0]) ||
                    !std::isfinite(value.bounds.y - overflow[1]) ||
                    !std::isfinite(value.bounds.width + overflow[0] + overflow[2]) ||
                    !std::isfinite(value.bounds.height + overflow[1] + overflow[3]))
                    return fail(error, index, "custom effect bounds overflow");
                value.bounds.x -= overflow[0];
                value.bounds.y -= overflow[1];
                value.bounds.width += overflow[0] + overflow[2];
                value.bounds.height += overflow[1] + overflow[3];
            }
            if (value.has_bounds && value.has_effect &&
                (value.effect.kind == EffectKind::Blur ||
                 value.effect.kind == EffectKind::DropShadow)) {
                const float sigma = value.effect.color_matrix[0];
                const float spread = sigma * 3.0f;
                float left = spread;
                float top = spread;
                float right = spread;
                float bottom = spread;
                if (value.effect.kind == EffectKind::DropShadow) {
                    left = std::max(0.0f, spread - value.effect.color_matrix[2]);
                    top = std::max(0.0f, spread - value.effect.color_matrix[3]);
                    right = std::max(0.0f, spread + value.effect.color_matrix[2]);
                    bottom = std::max(0.0f, spread + value.effect.color_matrix[3]);
                }
                if (!std::isfinite(spread) || !std::isfinite(left) || !std::isfinite(top) ||
                    !std::isfinite(right) || !std::isfinite(bottom) ||
                    !std::isfinite(value.bounds.x - left) ||
                    !std::isfinite(value.bounds.y - top) ||
                    !std::isfinite(value.bounds.width + left + right) ||
                    !std::isfinite(value.bounds.height + top + bottom))
                    return fail(error, index, "effect bounds overflow");
                value.bounds.x -= left;
                value.bounds.y -= top;
                value.bounds.width += left + right;
                value.bounds.height += top + bottom;
            }
            const bool isolated = value.opacity < 1.0f || (value.flags & LayerIsolated) != 0 ||
                                  value.has_effect || value.has_mask || value.has_backdrop;
            if (isolated)
                ++plan.isolated_layers;
            if (value.has_bounds)
                ++plan.bounded_layers;
            const ResourceId parent_target = current_target;
            ResourceId layer_target = current_target;
            const float parent_origin_x = current_origin_x;
            const float parent_origin_y = current_origin_y;
            RenderTargetDescriptor layer_descriptor{};
            if (value.has_backdrop) {
                RenderTargetDescriptor backdrop_descriptor{};
                if (value.has_bounds) {
                    backdrop_descriptor.logical_width = backdrop_bounds.width;
                    backdrop_descriptor.logical_height = backdrop_bounds.height;
                    backdrop_descriptor.origin_x = backdrop_bounds.x;
                    backdrop_descriptor.origin_y = backdrop_bounds.y;
                }
                const auto append_backdrop_effect = [&](ResourceId target, ResourceId input,
                                                        EffectDescriptor effect,
                                                        bool source_region) {
                    RenderPass effect_pass;
                    effect_pass.target = target;
                    effect_pass.target_descriptor = backdrop_descriptor;
                    effect_pass.kind = RenderPassKind::Effect;
                    effect_pass.input_target = input;
                    effect_pass.effect = effect;
                    effect_pass.backdrop = true;
                    if (source_region) {
                        effect_pass.has_input_rect = true;
                        effect_pass.input_rect = {backdrop_bounds.x - parent_origin_x,
                                                  backdrop_bounds.y - parent_origin_y,
                                                  backdrop_bounds.width, backdrop_bounds.height};
                    }
                    plan.passes.push_back(std::move(effect_pass));
                };
                ResourceId backdrop_target = allocate_transient_target();
                if (value.backdrop_effect.kind == EffectKind::Blur ||
                    value.backdrop_effect.kind == EffectKind::DropShadow) {
                    const ResourceId horizontal_target = allocate_transient_target();
                    const ResourceId vertical_target = allocate_transient_target();
                    EffectDescriptor horizontal = value.backdrop_effect;
                    horizontal.color_matrix[1] = 0.0f;
                    append_backdrop_effect(horizontal_target, parent_target, horizontal,
                                           value.has_bounds);
                    EffectDescriptor vertical = value.backdrop_effect;
                    vertical.color_matrix[1] = 1.0f;
                    append_backdrop_effect(vertical_target, horizontal_target, vertical, false);
                    backdrop_target = vertical_target;
                } else {
                    append_backdrop_effect(backdrop_target, parent_target, value.backdrop_effect,
                                           value.has_bounds);
                }
                pass = &continue_pass(plan, parent_target);
                pass->commands.push_back(
                    {RenderCommandKind::CompositeTarget, backdrop_target,
                     value.has_bounds ? backdrop_bounds.x - parent_origin_x : 0.0f,
                     value.has_bounds ? backdrop_bounds.y - parent_origin_y : 0.0f,
                     value.has_bounds ? backdrop_bounds.width : 0.0f,
                     value.has_bounds ? backdrop_bounds.height : 0.0f});
                add_dependency(plan, backdrop_target, parent_target);
            }
            if (isolated) {
                layer_target = allocate_transient_target();
                current_target = layer_target;
                if (value.has_bounds) {
                    layer_descriptor.logical_width = value.bounds.width;
                    layer_descriptor.logical_height = value.bounds.height;
                    layer_descriptor.origin_x = value.bounds.x;
                    layer_descriptor.origin_y = value.bounds.y;
                    current_origin_x = value.bounds.x;
                    current_origin_y = value.bounds.y;
                } else {
                    current_origin_x = 0.0f;
                    current_origin_y = 0.0f;
                }
                pass = &continue_pass(plan, current_target, layer_descriptor);
            }
            layers.push_back({parent_target, layer_target, value.opacity, value.mode,
                              value.bounds, value.has_bounds, parent_origin_x, parent_origin_y,
                              layer_descriptor, value.effect, value.custom_effect,
                              value.has_effect, value.mask, value.has_mask,
                              value.backdrop_effect, value.has_backdrop, isolated});
            break;
        }
        case CommandOpcode::EndLayer: {
            const Layer layer = layers.back();
            layers.pop_back();
            if (layer.isolated) {
                current_target = layer.parent_target;
                current_origin_x = layer.parent_origin_x;
                current_origin_y = layer.parent_origin_y;
                ResourceId composite_target = layer.layer_target;
                if (layer.has_effect) {
                    const auto append_effect_pass = [&](ResourceId target, ResourceId input,
                                                         EffectDescriptor effect) {
                        RenderPass effect_pass;
                        effect_pass.target = target;
                        effect_pass.target_descriptor = layer.target_descriptor;
                        effect_pass.kind = RenderPassKind::Effect;
                        effect_pass.input_target = input;
                        effect_pass.effect = effect;
                        effect_pass.custom_effect = layer.custom_effect;
                        plan.passes.push_back(std::move(effect_pass));
                    };
                    if (layer.effect.kind == EffectKind::Blur ||
                        layer.effect.kind == EffectKind::DropShadow) {
                        const ResourceId horizontal_target = allocate_transient_target();
                        const ResourceId vertical_target = allocate_transient_target();
                        EffectDescriptor horizontal = layer.effect;
                        horizontal.color_matrix[1] = 0.0f;
                        append_effect_pass(horizontal_target, layer.layer_target, horizontal);
                        EffectDescriptor vertical = layer.effect;
                        vertical.color_matrix[1] = 1.0f;
                        append_effect_pass(vertical_target, horizontal_target, vertical);
                        composite_target = vertical_target;
                    } else {
                        const ResourceId effect_target = allocate_transient_target();
                        append_effect_pass(effect_target, layer.layer_target, layer.effect);
                        composite_target = effect_target;
                    }
                }
                const auto append_mask_pass = [&](ResourceId input) {
                    const ResourceId mask_target = allocate_transient_target();
                    RenderPass mask_pass;
                    mask_pass.target = mask_target;
                    mask_pass.target_descriptor = layer.target_descriptor;
                    mask_pass.kind = RenderPassKind::Mask;
                    mask_pass.input_target = input;
                    mask_pass.mask = layer.mask;
                    plan.passes.push_back(std::move(mask_pass));
                    return mask_target;
                };
                ResourceId original_target = layer.layer_target;
                if (layer.has_mask) {
                    composite_target = append_mask_pass(composite_target);
                    if (layer.has_effect && layer.effect.kind == EffectKind::DropShadow)
                        original_target = append_mask_pass(original_target);
                }
                pass = &continue_pass(plan, current_target);
                const auto append_composite = [&](ResourceId target) {
                    pass->commands.push_back(
                        {RenderCommandKind::CompositeTarget, target,
                         layer.has_bounds ? layer.bounds.x - current_origin_x : 0.0f,
                         layer.has_bounds ? layer.bounds.y - current_origin_y : 0.0f,
                         layer.has_bounds ? layer.bounds.width : 0.0f,
                         layer.has_bounds ? layer.bounds.height : 0.0f, layer.opacity});
                    pass->commands.back().composite = layer.mode;
                    pass->commands.back().has_scissor = state.has_scissor;
                    pass->commands.back().scissor_x = state.x - current_origin_x;
                    pass->commands.back().scissor_y = state.y - current_origin_y;
                    pass->commands.back().scissor_width = state.width;
                    pass->commands.back().scissor_height = state.height;
                    add_dependency(plan, target, current_target);
                };
                if (layer.has_effect && layer.effect.kind == EffectKind::DropShadow) {
                    append_composite(composite_target);
                    append_composite(original_target);
                } else {
                    append_composite(composite_target);
                }
            }
            break;
        }
        default:
            break;
        }
        offset += header.size;
        ++index;
    }
    assign_effect_cache_keys(plan);
    if (error)
        *error = {};
    return true;
}

} // namespace nkui
