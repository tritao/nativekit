#include "display_list.h"

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

bool valid_composite(CompositeMode mode) {
    return mode == CompositeMode::SourceOver;
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

} // namespace

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

bool DisplayList::assign_validated(const uint8_t *data, size_t size) {
    if (!validate_display_list(data, size))
        return false;
    try {
        if (size)
            bytes_.assign(data, data + size);
        else
            bytes_.clear();
    } catch (...) {
        return false;
    }
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
    try {
        bytes_.resize(capacity);
    } catch (...) {
        return false;
    }
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

bool DisplayList::stroke_path(ResourceId path, float width, uint32_t line_cap,
                               uint32_t line_join, float miter_limit) {
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
    return append(value);
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
        if (header.version != 1)
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
                !finite(value->width) || value->width <= 0.0f ||
                !valid_line_cap(value->line_cap) || !valid_line_join(value->line_join) ||
                !finite(value->miter_limit) || value->miter_limit <= 0.0f)
                return fail(error, offset, index, "invalid stroke path command");
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
            const auto *value = read_command<BeginLayerCommand>(record, header.size);
            if (!value || layer_depth == max_scope_depth || !finite(value->opacity) ||
                value->opacity < 0.0f || value->opacity > 1.0f || !valid_composite(value->mode))
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
