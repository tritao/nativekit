#include "display_list.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace nkui {
namespace {

constexpr uint32_t kind_shift = 28;
constexpr uint32_t generation_shift = 16;
constexpr uint32_t generation_mask = 0x0FFF;
constexpr uint32_t slot_mask = 0xFFFF;
constexpr uint32_t max_scope_depth = 1024;

template <class T> T command(CommandOpcode opcode) {
    T value{};
    value.header.opcode = opcode;
    value.header.version = 1;
    value.header.size = sizeof(T);
    return value;
}

bool finite(float value) {
    return std::isfinite(value);
}

bool valid_rect(float x, float y, float width, float height) {
    return finite(x) && finite(y) && finite(width) && finite(height) && width >= 0.0f &&
           height >= 0.0f;
}

bool valid_box_shadow(const DrawBoxShadowCommand &value) {
    if (!valid_rect(value.x, value.y, value.width, value.height) || value.width <= 0.0f ||
        value.height <= 0.0f || !finite(value.offset_x) || !finite(value.offset_y) ||
        !finite(value.blur_sigma) || value.blur_sigma < 0.0f || !finite(value.spread))
        return false;
    for (const float radius : value.radii)
        if (!finite(radius) || radius < 0.0f)
            return false;
    for (const float component : value.color)
        if (!finite(component) || component < 0.0f || component > 1.0f)
            return false;
    return true;
}

bool valid_composite(CompositeMode mode) {
    return mode == CompositeMode::SourceOver;
}

bool valid_effect_kind(EffectKind kind) {
    return kind == EffectKind::None || kind == EffectKind::ColorMatrix ||
           kind == EffectKind::Blur || kind == EffectKind::DropShadow;
}

bool valid_custom_effect_descriptor_impl(const CustomEffectDescriptor &effect) {
    if (!effect.registration_id || effect.parameter_count > kCustomEffectParameterComponents ||
        effect.pass_count != 1 || effect.sampling_inputs != 1)
        return false;
    for (const float value : effect.ink_overflow)
        if (!finite(value) || value < 0.0f)
            return false;
    for (const float value : effect.parameters)
        if (!finite(value))
            return false;
    return true;
}

bool valid_effect(const EffectDescriptor &effect) {
    if (!valid_effect_kind(effect.kind))
        return false;
    if (effect.kind == EffectKind::None)
        return true;
    if (effect.kind == EffectKind::Blur || effect.kind == EffectKind::DropShadow) {
        for (float value : effect.color_matrix)
            if (!finite(value))
                return false;
        if (effect.color_matrix[0] < 0.0f || effect.color_matrix[1] != 0.0f)
            return false;
        if (effect.kind == EffectKind::DropShadow)
            for (size_t index = 4; index < 8; ++index)
                if (effect.color_matrix[index] < 0.0f || effect.color_matrix[index] > 1.0f)
                    return false;
        return true;
    }
    for (float value : effect.color_matrix)
        if (!finite(value))
            return false;
    return true;
}

bool valid_effect_op(const EffectOpCommand &operation) {
    if (operation.kind == EffectKind::Custom) {
        return valid_custom_effect_descriptor_impl(operation.custom);
    }
    if (operation.kind != EffectKind::ColorMatrix && operation.kind != EffectKind::Blur &&
        operation.kind != EffectKind::DropShadow)
        return false;
    EffectDescriptor descriptor;
    descriptor.kind = operation.kind;
    descriptor.color_matrix = operation.color_matrix;
    return valid_effect(descriptor);
}

bool valid_mask(const MaskDescriptor &mask);

bool valid_layer_base(float opacity, CompositeMode mode, float x, float y, float width,
                      float height, uint32_t flags) {
    return finite(opacity) && opacity >= 0.0f && opacity <= 1.0f && valid_composite(mode) &&
           !(flags & ~(LayerIsolated | LayerHasBounds)) &&
           (!(flags & LayerHasBounds) ||
            ((flags & LayerIsolated) && valid_rect(x, y, width, height) && width > 0.0f &&
             height > 0.0f));
}

bool valid_layer_v1(const BeginLayerV1Command &value) {
    return valid_layer_base(value.opacity, value.mode, value.x, value.y, value.width, value.height,
                            value.flags);
}

bool valid_layer_unbounded_v1(const BeginLayerUnboundedV1Command &value) {
    return finite(value.opacity) && value.opacity >= 0.0f && value.opacity <= 1.0f &&
           valid_composite(value.mode);
}

bool valid_layer_effect_v1(const BeginLayerEffectV1Command &value) {
    return valid_layer_v1(value.base) && valid_effect(value.effect) &&
           (value.effect.kind == EffectKind::None || (value.base.flags & LayerIsolated));
}

bool valid_layer_mask_v1(const BeginLayerMaskV1Command &value) {
    return valid_layer_v1(value.base) && valid_effect(value.effect) && valid_mask(value.mask) &&
           ((value.effect.kind == EffectKind::None && value.mask.kind == MaskKind::None) ||
            (value.base.flags & LayerIsolated));
}

bool valid_layer_backdrop_v1(const BeginLayerBackdropV1Command &value) {
    return valid_layer_v1(value.base) && valid_effect(value.effect) &&
           valid_effect(value.backdrop_effect) && valid_mask(value.mask) &&
           ((value.effect.kind == EffectKind::None &&
             value.backdrop_effect.kind == EffectKind::None && value.mask.kind == MaskKind::None) ||
            (value.base.flags & LayerIsolated));
}

bool valid_layer_custom_v1(const BeginLayerCustomEffectV1Command &value) {
    return valid_layer_v1(value.base) && valid_custom_effect_descriptor_impl(value.effect) &&
           (value.base.flags & LayerIsolated);
}

bool valid_layer_v2(const uint8_t *record, uint32_t size) {
    if (!record || size < sizeof(BeginLayerCommand))
        return false;
    BeginLayerCommand value{};
    std::memcpy(&value, record, sizeof(value));
    if (!valid_layer_base(value.opacity, value.mode, value.x, value.y, value.width, value.height,
                          value.flags) ||
        !valid_mask(value.mask) || value.foreground_count > kEffectProgramMaxOps ||
        value.backdrop_count > kEffectProgramMaxOps)
        return false;
    const size_t operation_count =
        static_cast<size_t>(value.foreground_count) + static_cast<size_t>(value.backdrop_count);
    const size_t expected_size =
        sizeof(BeginLayerCommand) + operation_count * sizeof(EffectOpCommand);
    if (expected_size != size ||
        ((value.foreground_count || value.backdrop_count) && !(value.flags & LayerIsolated)))
        return false;
    const auto *operations = record + sizeof(BeginLayerCommand);
    for (size_t index = 0; index < operation_count; ++index) {
        EffectOpCommand operation{};
        std::memcpy(&operation, operations + index * sizeof(operation), sizeof(operation));
        if (!valid_effect_op(operation))
            return false;
    }
    return true;
}

bool valid_mask_kind(MaskKind kind) {
    return kind == MaskKind::None || kind == MaskKind::Rectangle || kind == MaskKind::RoundedRect ||
           kind == MaskKind::Circle || kind == MaskKind::LinearGradient || kind == MaskKind::Image;
}

bool valid_mask(const MaskDescriptor &mask) {
    if (!valid_mask_kind(mask.kind))
        return false;
    if (mask.kind == MaskKind::None)
        return true;
    for (float value : mask.values)
        if (!finite(value))
            return false;
    if (mask.kind != MaskKind::Image && mask.image.value != 0)
        return false;
    if (mask.kind == MaskKind::Image)
        return is_resource_id(mask.image, ResourceKind::Image);
    if ((mask.kind == MaskKind::RoundedRect || mask.kind == MaskKind::Circle) &&
        mask.values[0] < 0.0f)
        return false;
    if (mask.kind == MaskKind::LinearGradient && (mask.values[4] < 0.0f || mask.values[4] > 1.0f ||
                                                  mask.values[5] < 0.0f || mask.values[5] > 1.0f))
        return false;
    return true;
}

bool valid_line_cap(uint32_t line_cap) {
    return line_cap <= 2;
}

bool valid_line_join(uint32_t line_join) {
    return line_join == 1 || line_join == 3 || line_join == 4;
}

bool fail(ValidationError *error, size_t offset, uint32_t index, const char *message) {
    if (error)
        *error = {offset, index, message};
    return false;
}

template <class T> const T *read_command(const uint8_t *record, size_t record_size) {
    return record_size == sizeof(T) ? reinterpret_cast<const T *>(record) : nullptr;
}

template <class T> T read(const uint8_t *record) {
    T value{};
    std::memcpy(&value, record, sizeof(value));
    return value;
}

} // namespace

bool valid_custom_effect_descriptor(const CustomEffectDescriptor &effect) {
    return valid_custom_effect_descriptor_impl(effect);
}

bool decode_effect_op(const EffectOpCommand &command, EffectOp &operation) {
    if (!valid_effect_op(command))
        return false;
    operation = {};
    operation.kind = command.kind;
    switch (command.kind) {
    case EffectKind::ColorMatrix:
        operation.color_matrix = command.color_matrix;
        break;
    case EffectKind::Blur:
        operation.blur_sigma = command.color_matrix[0];
        break;
    case EffectKind::DropShadow:
        operation.drop_shadow.sigma = command.color_matrix[0];
        operation.drop_shadow.offset_x = command.color_matrix[2];
        operation.drop_shadow.offset_y = command.color_matrix[3];
        std::copy_n(command.color_matrix.begin() + 4, operation.drop_shadow.color.size(),
                    operation.drop_shadow.color.begin());
        break;
    case EffectKind::Custom:
        operation.custom = command.custom;
        break;
    default:
        return false;
    }
    return true;
}

ResourceId make_resource_id(ResourceKind kind, uint16_t generation, uint16_t slot) {
    if (!slot || !generation || generation > generation_mask)
        return {};
    return {(uint32_t(kind) << kind_shift) | (uint32_t(generation) << generation_shift) | slot};
}

bool is_resource_id(ResourceId id, ResourceKind kind) {
    return (id.value >> kind_shift) == uint32_t(kind) &&
           ((id.value >> generation_shift) & generation_mask) != 0 && (id.value & slot_mask) != 0;
}

DisplayList::DisplayList(size_t initial_capacity) {
    if (initial_capacity)
        bytes_.resize(initial_capacity);
}

void DisplayList::reset() {
    size_ = 0;
    command_count_ = 0;
}

const uint8_t *DisplayList::data() const {
    return bytes_.data();
}

size_t DisplayList::size() const {
    return size_;
}

size_t DisplayList::capacity() const {
    return bytes_.size();
}

uint32_t DisplayList::growth_count() const {
    return growth_count_;
}

uint32_t DisplayList::command_count() const {
    return command_count_;
}

bool DisplayList::has_backdrop_effects() const {
    size_t offset = 0;
    while (offset < size_) {
        CommandHeader header{};
        std::memcpy(&header, bytes_.data() + offset, sizeof(header));
        if (header.opcode == CommandOpcode::BeginLayer) {
            if (header.version == kLayerCommandVersion &&
                header.size >= sizeof(BeginLayerCommand)) {
                BeginLayerCommand value{};
                std::memcpy(&value, bytes_.data() + offset, sizeof(value));
                if (value.backdrop_count)
                    return true;
            } else if (header.version == 1 && header.size == sizeof(BeginLayerBackdropV1Command))
                return read<BeginLayerBackdropV1Command>(bytes_.data() + offset)
                           .backdrop_effect.kind != EffectKind::None;
        }
        if (header.size < sizeof(CommandHeader) || header.size > size_ - offset)
            return false;
        offset += header.size;
    }
    return false;
}

bool DisplayList::assign_validated(const uint8_t *data, size_t size) {
    if (!validate_display_list(data, size))
        return false;
    if (size)
        bytes_.assign(data, data + size);
    else
        bytes_.clear();
    size_ = size;
    command_count_ = 0;
    size_t offset = 0;
    while (offset < size_) {
        CommandHeader header{};
        std::memcpy(&header, bytes_.data() + offset, sizeof(header));
        offset += header.size;
        ++command_count_;
    }
    return true;
}

bool DisplayList::reserve_record(size_t record_size) {
    if (record_size > std::numeric_limits<size_t>::max() - size_)
        return false;
    const size_t required = size_ + record_size;
    if (required <= bytes_.size())
        return true;
    size_t capacity = bytes_.empty() ? 64 : bytes_.size();
    while (capacity < required) {
        if (capacity > std::numeric_limits<size_t>::max() / 2) {
            capacity = required;
            break;
        }
        capacity *= 2;
    }
    bytes_.resize(capacity);
    ++growth_count_;
    return true;
}

template <class T> bool DisplayList::append(const T &value) {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(alignof(T) <= alignof(uint64_t));
    if (!reserve_record(sizeof(T)))
        return false;
    std::memcpy(bytes_.data() + size_, &value, sizeof(T));
    size_ += sizeof(T);
    ++command_count_;
    return true;
}

bool DisplayList::append_layer(BeginLayerCommand value,
                               const std::vector<EffectOpCommand> &foreground,
                               const std::vector<EffectOpCommand> &backdrop) {
    if (foreground.size() > kEffectProgramMaxOps || backdrop.size() > kEffectProgramMaxOps)
        return false;
    const size_t operation_count = foreground.size() + backdrop.size();
    if (operation_count >
        (std::numeric_limits<uint32_t>::max() - sizeof(value)) / sizeof(EffectOpCommand))
        return false;
    const size_t record_size = sizeof(value) + operation_count * sizeof(EffectOpCommand);
    value.header.version = kLayerCommandVersion;
    value.header.size = static_cast<uint32_t>(record_size);
    value.foreground_count = static_cast<uint32_t>(foreground.size());
    value.backdrop_count = static_cast<uint32_t>(backdrop.size());
    if (!reserve_record(record_size))
        return false;
    std::memcpy(bytes_.data() + size_, &value, sizeof(value));
    size_t offset = size_ + sizeof(value);
    if (!foreground.empty()) {
        std::memcpy(bytes_.data() + offset, foreground.data(),
                    foreground.size() * sizeof(EffectOpCommand));
        offset += foreground.size() * sizeof(EffectOpCommand);
    }
    if (!backdrop.empty())
        std::memcpy(bytes_.data() + offset, backdrop.data(),
                    backdrop.size() * sizeof(EffectOpCommand));
    size_ += record_size;
    ++command_count_;
    return true;
}

EffectOpCommand effect_operation(const EffectDescriptor &effect) {
    EffectOpCommand operation{};
    operation.kind = effect.kind;
    operation.color_matrix = effect.color_matrix;
    return operation;
}

EffectOpCommand custom_operation(const CustomEffectDescriptor &effect) {
    EffectOpCommand operation{};
    operation.kind = EffectKind::Custom;
    operation.custom = effect;
    return operation;
}

bool DisplayList::set_transform(const float matrix[6]) {
    auto value = command<SetTransformCommand>(CommandOpcode::SetTransform);
    std::memcpy(value.matrix, matrix, sizeof(value.matrix));
    return append(value);
}

bool DisplayList::set_paint(ResourceId paint) {
    auto value = command<SetPaintCommand>(CommandOpcode::SetPaint);
    value.paint = paint;
    return append(value);
}

bool DisplayList::set_global_alpha(float alpha) {
    auto value = command<SetGlobalAlphaCommand>(CommandOpcode::SetGlobalAlpha);
    value.alpha = alpha;
    return append(value);
}

bool DisplayList::set_composite_mode(CompositeMode mode) {
    auto value = command<SetCompositeModeCommand>(CommandOpcode::SetCompositeMode);
    value.mode = mode;
    return append(value);
}

bool DisplayList::push_state() {
    return append(command<ScopeCommand>(CommandOpcode::PushState));
}

bool DisplayList::pop_state() {
    return append(command<ScopeCommand>(CommandOpcode::PopState));
}

bool DisplayList::clip_rect(float x, float y, float width, float height) {
    auto value = command<ClipRectCommand>(CommandOpcode::ClipRect);
    value.x = x;
    value.y = y;
    value.width = width;
    value.height = height;
    return append(value);
}

bool DisplayList::draw_path(ResourceId path) {
    auto value = command<DrawResourceCommand>(CommandOpcode::DrawPath);
    value.resource = path;
    return append(value);
}

bool DisplayList::stroke_path(ResourceId path, float width, uint32_t line_cap, uint32_t line_join,
                              float miter_limit) {
    auto value = command<StrokePathCommand>(CommandOpcode::StrokePath);
    value.path = path;
    value.width = width;
    value.line_cap = line_cap;
    value.line_join = line_join;
    value.miter_limit = miter_limit;
    return append(value);
}

bool DisplayList::draw_image(ResourceId image, float x, float y, float width, float height) {
    auto value = command<DrawRectResourceCommand>(CommandOpcode::DrawImage);
    value.resource = image;
    value.x = x;
    value.y = y;
    value.width = width;
    value.height = height;
    return append(value);
}

bool DisplayList::draw_text_layout(ResourceId layout, float x, float y) {
    auto value = command<DrawRectResourceCommand>(CommandOpcode::DrawTextLayout);
    value.resource = layout;
    value.x = x;
    value.y = y;
    return append(value);
}

bool DisplayList::begin_layer(float opacity, CompositeMode mode) {
    auto value = command<BeginLayerCommand>(CommandOpcode::BeginLayer);
    value.opacity = opacity;
    value.mode = mode;
    return append_layer(value, {}, {});
}

bool DisplayList::begin_layer(float opacity, const LayerBounds &bounds, CompositeMode mode) {
    auto value = command<BeginLayerCommand>(CommandOpcode::BeginLayer);
    value.opacity = opacity;
    value.mode = mode;
    value.x = bounds.x;
    value.y = bounds.y;
    value.width = bounds.width;
    value.height = bounds.height;
    value.flags = LayerIsolated | LayerHasBounds;
    return append_layer(value, {}, {});
}

bool DisplayList::begin_layer(float opacity, const EffectDescriptor &effect, CompositeMode mode) {
    auto value = command<BeginLayerCommand>(CommandOpcode::BeginLayer);
    value.opacity = opacity;
    value.mode = mode;
    value.flags = LayerIsolated;
    const auto operation = effect_operation(effect);
    return effect.kind == EffectKind::None ? append_layer(value, {}, {})
                                           : append_layer(value, {operation}, {});
}

bool DisplayList::begin_layer(float opacity, const LayerBounds &bounds,
                              const EffectDescriptor &effect, CompositeMode mode) {
    auto value = command<BeginLayerCommand>(CommandOpcode::BeginLayer);
    value.opacity = opacity;
    value.mode = mode;
    value.x = bounds.x;
    value.y = bounds.y;
    value.width = bounds.width;
    value.height = bounds.height;
    value.flags = LayerIsolated | LayerHasBounds;
    const auto operation = effect_operation(effect);
    return effect.kind == EffectKind::None ? append_layer(value, {}, {})
                                           : append_layer(value, {operation}, {});
}

bool DisplayList::begin_layer(float opacity, const MaskDescriptor &mask, CompositeMode mode) {
    auto value = command<BeginLayerCommand>(CommandOpcode::BeginLayer);
    value.opacity = opacity;
    value.mode = mode;
    value.flags = LayerIsolated;
    value.mask = mask;
    return append_layer(value, {}, {});
}

bool DisplayList::begin_layer(float opacity, const LayerBounds &bounds, const MaskDescriptor &mask,
                              CompositeMode mode) {
    auto value = command<BeginLayerCommand>(CommandOpcode::BeginLayer);
    value.opacity = opacity;
    value.mode = mode;
    value.x = bounds.x;
    value.y = bounds.y;
    value.width = bounds.width;
    value.height = bounds.height;
    value.flags = LayerIsolated | LayerHasBounds;
    value.mask = mask;
    return append_layer(value, {}, {});
}

bool DisplayList::begin_layer(float opacity, const LayerBounds &bounds,
                              const EffectDescriptor &effect, const MaskDescriptor &mask,
                              CompositeMode mode) {
    auto value = command<BeginLayerCommand>(CommandOpcode::BeginLayer);
    value.opacity = opacity;
    value.mode = mode;
    value.x = bounds.x;
    value.y = bounds.y;
    value.width = bounds.width;
    value.height = bounds.height;
    value.flags = LayerIsolated | LayerHasBounds;
    value.mask = mask;
    const auto operation = effect_operation(effect);
    return effect.kind == EffectKind::None ? append_layer(value, {}, {})
                                           : append_layer(value, {operation}, {});
}

bool DisplayList::begin_layer(float opacity, const LayerBounds &bounds,
                              const EffectDescriptor &effect, const MaskDescriptor &mask,
                              const EffectDescriptor &backdrop_effect, CompositeMode mode) {
    auto value = command<BeginLayerCommand>(CommandOpcode::BeginLayer);
    value.opacity = opacity;
    value.mode = mode;
    value.x = bounds.x;
    value.y = bounds.y;
    value.width = bounds.width;
    value.height = bounds.height;
    value.flags = LayerIsolated | LayerHasBounds;
    value.mask = mask;
    const auto foreground_operation = effect_operation(effect);
    const auto backdrop_operation = effect_operation(backdrop_effect);
    const std::vector<EffectOpCommand> foreground =
        effect.kind == EffectKind::None ? std::vector<EffectOpCommand>{}
                                        : std::vector<EffectOpCommand>{foreground_operation};
    const std::vector<EffectOpCommand> backdrop =
        backdrop_effect.kind == EffectKind::None ? std::vector<EffectOpCommand>{}
                                                 : std::vector<EffectOpCommand>{backdrop_operation};
    return append_layer(value, foreground, backdrop);
}

bool DisplayList::begin_layer(float opacity, const EffectDescriptor &effect,
                              const MaskDescriptor &mask, const EffectDescriptor &backdrop_effect,
                              CompositeMode mode) {
    auto value = command<BeginLayerCommand>(CommandOpcode::BeginLayer);
    value.opacity = opacity;
    value.mode = mode;
    value.flags = LayerIsolated;
    value.mask = mask;
    const auto foreground_operation = effect_operation(effect);
    const auto backdrop_operation = effect_operation(backdrop_effect);
    const std::vector<EffectOpCommand> foreground =
        effect.kind == EffectKind::None ? std::vector<EffectOpCommand>{}
                                        : std::vector<EffectOpCommand>{foreground_operation};
    const std::vector<EffectOpCommand> backdrop =
        backdrop_effect.kind == EffectKind::None ? std::vector<EffectOpCommand>{}
                                                 : std::vector<EffectOpCommand>{backdrop_operation};
    return append_layer(value, foreground, backdrop);
}

bool DisplayList::begin_layer(float opacity, const LayerBounds &bounds,
                              const std::vector<EffectOpCommand> &foreground,
                              const MaskDescriptor &mask,
                              const std::vector<EffectOpCommand> &backdrop, CompositeMode mode) {
    if (foreground.size() > kEffectProgramMaxOps || backdrop.size() > kEffectProgramMaxOps)
        return false;
    auto value = command<BeginLayerCommand>(CommandOpcode::BeginLayer);
    value.opacity = opacity;
    value.mode = mode;
    value.x = bounds.x;
    value.y = bounds.y;
    value.width = bounds.width;
    value.height = bounds.height;
    value.flags = LayerIsolated | LayerHasBounds;
    value.mask = mask;
    return append_layer(value, foreground, backdrop);
}

bool DisplayList::begin_layer(float opacity, const CustomEffectDescriptor &effect,
                              CompositeMode mode) {
    auto value = command<BeginLayerCommand>(CommandOpcode::BeginLayer);
    value.opacity = opacity;
    value.mode = mode;
    value.flags = LayerIsolated;
    return append_layer(value, {custom_operation(effect)}, {});
}

bool DisplayList::begin_layer(float opacity, const LayerBounds &bounds,
                              const CustomEffectDescriptor &effect, CompositeMode mode) {
    auto value = command<BeginLayerCommand>(CommandOpcode::BeginLayer);
    value.opacity = opacity;
    value.mode = mode;
    value.x = bounds.x;
    value.y = bounds.y;
    value.width = bounds.width;
    value.height = bounds.height;
    value.flags = LayerIsolated | LayerHasBounds;
    return append_layer(value, {custom_operation(effect)}, {});
}

bool DisplayList::end_layer() {
    return append(command<ScopeCommand>(CommandOpcode::EndLayer));
}

bool DisplayList::draw_render_target(ResourceId target, float x, float y, float width,
                                     float height) {
    auto value = command<DrawRectResourceCommand>(CommandOpcode::DrawRenderTarget);
    value.resource = target;
    value.x = x;
    value.y = y;
    value.width = width;
    value.height = height;
    return append(value);
}

bool DisplayList::draw_box_shadow(float x, float y, float width, float height, float offset_x,
                                  float offset_y, float blur_sigma, float spread,
                                  const float radii[4], const float color[4]) {
    if (!radii || !color)
        return false;
    auto value = command<DrawBoxShadowCommand>(CommandOpcode::DrawBoxShadow);
    value.x = x;
    value.y = y;
    value.width = width;
    value.height = height;
    value.offset_x = offset_x;
    value.offset_y = offset_y;
    value.blur_sigma = blur_sigma;
    value.spread = spread;
    std::copy_n(radii, 4, value.radii);
    std::copy_n(color, 4, value.color);
    return valid_box_shadow(value) && append(value);
}

bool validate_display_list(const uint8_t *data, size_t size, ValidationError *error) {
    if ((!data && size) || size > UINT32_MAX)
        return fail(error, 0, 0, "invalid display-list storage");
    size_t offset = 0;
    uint32_t index = 0;
    uint32_t state_depth = 0;
    uint32_t layer_depth = 0;
    while (offset < size) {
        if (size - offset < sizeof(CommandHeader))
            return fail(error, offset, index, "truncated command header");
        CommandHeader header{};
        std::memcpy(&header, data + offset, sizeof(header));
        const bool is_layer_v2 =
            header.opcode == CommandOpcode::BeginLayer && header.version == kLayerCommandVersion;
        if (header.version != 1 && !is_layer_v2)
            return fail(error, offset, index, "unsupported command version");
        if (header.size < sizeof(CommandHeader) || header.size > size - offset ||
            header.size % alignof(uint32_t) != 0)
            return fail(error, offset, index, "invalid command size");
        const uint8_t *record = data + offset;
        switch (header.opcode) {
        case CommandOpcode::SetTransform: {
            const auto *value = read_command<SetTransformCommand>(record, header.size);
            if (!value)
                return fail(error, offset, index, "invalid transform command");
            for (float component : value->matrix)
                if (!finite(component))
                    return fail(error, offset, index, "non-finite transform");
            break;
        }
        case CommandOpcode::SetPaint: {
            const auto *value = read_command<SetPaintCommand>(record, header.size);
            if (!value || !is_resource_id(value->paint, ResourceKind::Paint))
                return fail(error, offset, index, "invalid paint handle");
            break;
        }
        case CommandOpcode::SetGlobalAlpha: {
            const auto *value = read_command<SetGlobalAlphaCommand>(record, header.size);
            if (!value || !finite(value->alpha) || value->alpha < 0.0f || value->alpha > 1.0f)
                return fail(error, offset, index, "invalid global alpha");
            break;
        }
        case CommandOpcode::SetCompositeMode: {
            const auto *value = read_command<SetCompositeModeCommand>(record, header.size);
            if (!value || !valid_composite(value->mode))
                return fail(error, offset, index, "unsupported composite mode");
            break;
        }
        case CommandOpcode::PushState:
            if (!read_command<ScopeCommand>(record, header.size) || state_depth == max_scope_depth)
                return fail(error, offset, index, "invalid state push");
            ++state_depth;
            break;
        case CommandOpcode::PopState:
            if (!read_command<ScopeCommand>(record, header.size) || !state_depth)
                return fail(error, offset, index, "unbalanced state pop");
            --state_depth;
            break;
        case CommandOpcode::ClipRect: {
            const auto *value = read_command<ClipRectCommand>(record, header.size);
            if (!value || !valid_rect(value->x, value->y, value->width, value->height))
                return fail(error, offset, index, "invalid clip rectangle");
            break;
        }
        case CommandOpcode::DrawPath: {
            const auto *value = read_command<DrawResourceCommand>(record, header.size);
            if (!value || !is_resource_id(value->resource, ResourceKind::Path))
                return fail(error, offset, index, "invalid path handle");
            break;
        }
        case CommandOpcode::StrokePath: {
            const auto *value = read_command<StrokePathCommand>(record, header.size);
            if (!value || !is_resource_id(value->path, ResourceKind::Path) ||
                !finite(value->width) || value->width <= 0.0f || !valid_line_cap(value->line_cap) ||
                !valid_line_join(value->line_join) || !finite(value->miter_limit) ||
                value->miter_limit <= 0.0f)
                return fail(error, offset, index, "invalid stroke path command");
            break;
        }
        case CommandOpcode::DrawBoxShadow: {
            const auto *value = read_command<DrawBoxShadowCommand>(record, header.size);
            if (!value || !valid_box_shadow(*value))
                return fail(error, offset, index, "invalid box-shadow command");
            break;
        }
        case CommandOpcode::DrawImage:
        case CommandOpcode::DrawTextLayout:
        case CommandOpcode::DrawRenderTarget: {
            const auto *value = read_command<DrawRectResourceCommand>(record, header.size);
            ResourceKind kind = ResourceKind::Image;
            if (header.opcode == CommandOpcode::DrawTextLayout)
                kind = ResourceKind::TextLayout;
            else if (header.opcode == CommandOpcode::DrawRenderTarget)
                kind = ResourceKind::RenderTarget;
            if (!value || !is_resource_id(value->resource, kind) ||
                !valid_rect(value->x, value->y, value->width, value->height))
                return fail(error, offset, index, "invalid draw resource command");
            break;
        }
        case CommandOpcode::BeginLayer: {
            bool valid_layer = false;
            if (header.version == kLayerCommandVersion) {
                valid_layer = valid_layer_v2(record, header.size);
            } else if (header.size == sizeof(BeginLayerUnboundedV1Command)) {
                valid_layer = valid_layer_unbounded_v1(read<BeginLayerUnboundedV1Command>(record));
            } else if (header.size == sizeof(BeginLayerV1Command)) {
                valid_layer = valid_layer_v1(read<BeginLayerV1Command>(record));
            } else if (header.size == sizeof(BeginLayerEffectV1Command)) {
                valid_layer = valid_layer_effect_v1(read<BeginLayerEffectV1Command>(record));
            } else if (header.size == sizeof(BeginLayerMaskV1Command)) {
                valid_layer = valid_layer_mask_v1(read<BeginLayerMaskV1Command>(record));
            } else if (header.size == sizeof(BeginLayerBackdropV1Command)) {
                valid_layer = valid_layer_backdrop_v1(read<BeginLayerBackdropV1Command>(record));
            } else if (header.size == sizeof(BeginLayerCustomEffectV1Command)) {
                valid_layer = valid_layer_custom_v1(read<BeginLayerCustomEffectV1Command>(record));
            }
            if (!valid_layer || layer_depth == max_scope_depth)
                return fail(error, offset, index, "invalid layer begin");
            ++layer_depth;
            break;
        }
        case CommandOpcode::EndLayer:
            if (!read_command<ScopeCommand>(record, header.size) || !layer_depth)
                return fail(error, offset, index, "unbalanced layer end");
            --layer_depth;
            break;
        default:
            return fail(error, offset, index, "unknown command opcode");
        }
        offset += header.size;
        ++index;
    }
    if (state_depth)
        return fail(error, size, index, "unclosed state scope");
    if (layer_depth)
        return fail(error, size, index, "unclosed layer scope");
    if (error)
        *error = {};
    return true;
}

} // namespace nkui
