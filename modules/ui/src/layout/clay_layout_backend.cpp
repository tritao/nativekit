#include "layout/layout_engine.h"

#include "prepare/text_engine.h"

#define CLAY_IMPLEMENTATION
#include "clay.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>

namespace nkui {
namespace {

constexpr std::size_t kAutomaticTextLayoutCacheEntries = 128;
constexpr std::size_t kIntrinsicMeasureCacheEntries = 4096;

Clay_Color clay_color(LayoutColor color) {
    return {color.red * 255.0f, color.green * 255.0f, color.blue * 255.0f, color.alpha * 255.0f};
}

Clay_SizingAxis clay_axis(LayoutAxis axis) {
    switch (axis.sizing) {
    case LayoutSizing::Grow: {
        Clay_SizingAxis result = CLAY_SIZING_GROW(axis.min, axis.max);
        result.growWeight = axis.grow_weight;
        return result;
    }
    case LayoutSizing::Fixed:
        return CLAY_SIZING_FIXED(axis.value);
    case LayoutSizing::Percent:
        return CLAY_SIZING_PERCENT(axis.value);
    case LayoutSizing::Fit:
    default:
        return CLAY_SIZING_FIT(axis.min, axis.max);
    }
}

uint16_t clay_text_value(float value, bool allow_zero = false) {
    if (!std::isfinite(value) || value <= 0.0f)
        return allow_zero ? 0 : 1;
    return static_cast<uint16_t>(std::min(static_cast<long>(std::lround(value)),
                                          static_cast<long>(std::numeric_limits<uint16_t>::max())));
}

TextLayoutOptions text_options_for_node(const LayoutNode *node, Clay_TextElementConfig *config) {
    TextLayoutOptions options;
    if (node) {
        options.font_size = node->text_style.font_size;
        options.letter_spacing = node->text_style.letter_spacing;
        options.line_height = node->paragraph_style.line_height;
        options.family = node->text_style.family;
        options.wrap = node->paragraph_style.wrap;
        options.alignment = node->paragraph_style.alignment;
        options.direction = node->paragraph_style.direction;
        return options;
    }
    if (config) {
        options.font_size = config->fontSize > 0 ? static_cast<float>(config->fontSize) : 16.0f;
        options.letter_spacing = static_cast<float>(config->letterSpacing);
        options.line_height = static_cast<float>(config->lineHeight);
        options.family = static_cast<FontFamily>(config->fontId);
        options.wrap = config->wrapMode == CLAY_TEXT_WRAP_NONE       ? TextWrapMode::None
                       : config->wrapMode == CLAY_TEXT_WRAP_NEWLINES ? TextWrapMode::Word
                                                                     : TextWrapMode::WordCharacter;
        options.alignment = config->textAlignment == CLAY_TEXT_ALIGN_CENTER  ? TextAlignment::Center
                            : config->textAlignment == CLAY_TEXT_ALIGN_RIGHT ? TextAlignment::End
                                                                             : TextAlignment::Start;
    }
    return options;
}

Clay_TextElementConfigWrapMode clay_wrap(TextWrapMode wrap) {
    return wrap == TextWrapMode::None   ? CLAY_TEXT_WRAP_NONE
           : wrap == TextWrapMode::Word ? CLAY_TEXT_WRAP_NEWLINES
                                        : CLAY_TEXT_WRAP_WORDS;
}

Clay_TextAlignment clay_alignment(TextAlignment alignment) {
    return alignment == TextAlignment::Center ? CLAY_TEXT_ALIGN_CENTER
           : alignment == TextAlignment::End  ? CLAY_TEXT_ALIGN_RIGHT
                                              : CLAY_TEXT_ALIGN_LEFT;
}

} // namespace

struct LayoutEngine::Impl {
    explicit Impl(std::size_t initial_capacity, std::shared_ptr<FontCollection> fonts = {})
        : text(fonts ? std::move(fonts) : std::make_shared<FontCollection>()) {
        initialize_context(initial_capacity);
    }

    ~Impl() {
        if (context && Clay_GetCurrentContext() == context)
            Clay_SetCurrentContext(nullptr);
    }

    bool initialize_context(std::size_t capacity) {
        constexpr std::size_t max_capacity =
            static_cast<std::size_t>(std::numeric_limits<int32_t>::max()) - 1;
        if (capacity == 0 || capacity > max_capacity)
            return false;
        element_capacity = capacity;
        Clay_SetCurrentContext(nullptr);
        context = nullptr;
        Clay_SetMaxElementCount(static_cast<int32_t>(capacity + 1));
        const std::size_t cache_words =
            capacity > (max_capacity - 32) / 8 ? max_capacity : capacity * 8 + 32;
        Clay_SetMaxMeasureTextCacheWordCount(static_cast<int32_t>(cache_words));
        clay_memory.resize(Clay_MinMemorySize());
        if (clay_memory.empty())
            return false;
        Clay_ErrorHandler error_handler{};
        error_handler.errorHandlerFunction = [](Clay_ErrorData data) {
            auto *state = static_cast<Impl *>(data.userData);
            state->clay_error = data.errorText.chars ? data.errorText.chars : "Clay error";
        };
        error_handler.userData = this;
        context = Clay_Initialize(
            Clay_CreateArenaWithCapacityAndMemory(clay_memory.size(), clay_memory.data()),
            {1.0f, 1.0f}, error_handler);
        if (!context)
            return false;
        Clay_SetMeasureTextFunction(measure_text, this);
        Clay_SetMeasureTextIntrinsicFunction(measure_intrinsic_text, this);
        Clay_SetMeasureElementFunction(measure_element, this);
        Clay_SetLayoutTextFunction(layout_text, this);
        // NativeKit applies node transforms after Clay has generated its command stream.
        // Clay's viewport culling only sees the untransformed bounds, so it can discard
        // content that a scroll transform subsequently moves into view.
        Clay_SetCullingEnabled(false);
        return true;
    }

    bool reserve_elements(std::size_t count) {
        if (count <= element_capacity)
            return true;
        constexpr std::size_t max_capacity =
            static_cast<std::size_t>(std::numeric_limits<int32_t>::max()) - 1;
        if (count > max_capacity)
            return false;
        std::size_t capacity = element_capacity ? element_capacity : 1;
        while (capacity < count) {
            if (capacity > max_capacity / 2) {
                capacity = max_capacity;
                break;
            }
            capacity *= 2;
        }
        return initialize_context(capacity);
    }

    bool valid() const;
    bool add_font(const char *path, FontFamily family);
    bool add_font_from_data(const char *name, const void *data, std::size_t bytes,
                            FontFamily family);
    bool add_system_fallbacks();
    bool layout(const std::vector<LayoutNode> &nodes, float width, float height,
                float delta_seconds, LayoutSnapshot &out, LayoutError *error);

    static Clay_Dimensions measure_text(Clay_StringSlice text, Clay_TextElementConfig *config,
                                        void *user_data);
    static Clay_TextIntrinsicDimensions
    measure_intrinsic_text(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data);
    static Clay_MeasureResult measure_element(Clay_ElementId id,
                                              Clay_MeasureConstraints constraints, void *user_data);
    static Clay_TextLayoutResult layout_text(Clay_StringSlice text, Clay_TextElementConfig *config,
                                             float available_width, void *user_data);

    std::size_t element_capacity = 0;
    std::vector<char> clay_memory;
    Clay_Context *context = nullptr;
    TextEngine text;
    const std::vector<LayoutNode> *nodes = nullptr;
    std::vector<std::vector<std::size_t>> children;
    std::vector<Clay_ElementId> element_ids;
    std::unordered_map<TextLayoutId, LayoutTextLayout> text_layouts;
    struct MeasureCacheKey {
        uint32_t node_id = 0;
        uint32_t version = 0;
        std::array<uint32_t, 4> constraints{};

        bool operator==(const MeasureCacheKey &other) const {
            return node_id == other.node_id && version == other.version &&
                   constraints == other.constraints;
        }
    };
    struct MeasureCacheKeyHash {
        std::size_t operator()(const MeasureCacheKey &key) const {
            std::size_t hash = key.node_id;
            hash = (hash * 0x9E3779B1u) ^ key.version;
            for (uint32_t value : key.constraints)
                hash = (hash * 0x9E3779B1u) ^ value;
            return hash;
        }
    };
    LayoutMeasureCallback measure_callback;
    struct MeasureNodeInfo {
        uint32_t node_id = 0;
        uint32_t version = 0;
    };
    std::unordered_map<uint32_t, MeasureNodeInfo> measure_node_ids;
    std::unordered_map<MeasureCacheKey, LayoutMeasureResult, MeasureCacheKeyHash> measure_cache;
    uint64_t measure_requests = 0;
    uint64_t measure_cache_hits = 0;
    uint64_t measure_cache_misses = 0;
    uint64_t measure_callback_calls = 0;
    std::unordered_map<uint32_t, LayoutMeasureResult> frame_measurements;
    std::vector<Clay_TextLayoutLine> callback_lines;
    std::string clay_error;
};

Clay_Dimensions LayoutEngine::Impl::measure_text(Clay_StringSlice text,
                                                 Clay_TextElementConfig *config, void *user_data) {
    if (!config || text.length < 0 || (!text.chars && text.length != 0))
        return {0.0f, 0.0f};

    const Clay_TextIntrinsicDimensions intrinsic = measure_intrinsic_text(text, config, user_data);
    return {intrinsic.unwrappedDimensions.width, intrinsic.unwrappedDimensions.height};
}

Clay_TextIntrinsicDimensions
LayoutEngine::Impl::measure_intrinsic_text(Clay_StringSlice text, Clay_TextElementConfig *config,
                                           void *user_data) {
    auto &state = *static_cast<Impl *>(user_data);
    Clay_TextIntrinsicDimensions result{};
    if (!config || text.length < 0 || (!text.chars && text.length != 0))
        return result;

    const std::string value(text.chars ? text.chars : "", static_cast<std::size_t>(text.length));
    const auto *node = static_cast<const LayoutNode *>(config->userData);
    TextLayoutOptions options = text_options_for_node(node, config);
    options.wrap = TextWrapMode::None;
    TextIntrinsicMetrics metrics;
    if (!state.text.measure_intrinsic_utf8(value.c_str(), options, &metrics))
        return result;
    result.unwrappedDimensions = {metrics.bounds.width, config->lineHeight > 0
                                                            ? static_cast<float>(config->lineHeight)
                                                            : metrics.bounds.height};
    result.baseline = metrics.baseline;
    result.hasBaseline = metrics.has_baseline && std::isfinite(metrics.baseline);
    // External paragraph engines may break at character boundaries, so the
    // safe lower bound is zero unless the engine exposes a stronger one.
    result.minWidth = 0.0f;
    return result;
}

Clay_MeasureResult LayoutEngine::Impl::measure_element(Clay_ElementId id,
                                                       Clay_MeasureConstraints constraints,
                                                       void *user_data) {
    auto &state = *static_cast<Impl *>(user_data);
    Clay_MeasureResult result{};
    if (!state.measure_callback)
        return result;
    ++state.measure_requests;

    const auto node = state.measure_node_ids.find(id.id);
    const MeasureNodeInfo node_info =
        node == state.measure_node_ids.end() ? MeasureNodeInfo{id.id, 0} : node->second;

    const MeasureCacheKey key{node_info.node_id,
                              node_info.version,
                              {std::bit_cast<uint32_t>(constraints.minWidth),
                               std::bit_cast<uint32_t>(constraints.maxWidth),
                               std::bit_cast<uint32_t>(constraints.minHeight),
                               std::bit_cast<uint32_t>(constraints.maxHeight)}};
    const auto cached = state.measure_cache.find(key);
    LayoutMeasureResult measured;
    if (cached != state.measure_cache.end()) {
        ++state.measure_cache_hits;
        measured = cached->second;
    } else {
        ++state.measure_cache_misses;
        ++state.measure_callback_calls;
        measured = state.measure_callback(node_info.node_id,
                                          {constraints.minWidth, constraints.maxWidth,
                                           constraints.minHeight, constraints.maxHeight});
        if (std::isfinite(measured.width) && std::isfinite(measured.height) &&
            measured.width >= 0.0f && measured.height >= 0.0f) {
            if (state.measure_cache.size() >= kIntrinsicMeasureCacheEntries)
                state.measure_cache.clear();
            state.measure_cache.emplace(key, measured);
            state.frame_measurements[node_info.node_id] = measured;
        }
    }
    if (cached != state.measure_cache.end())
        state.frame_measurements[node_info.node_id] = measured;
    result.dimensions = {measured.width, measured.height};
    result.baseline = measured.baseline;
    result.hasBaseline = measured.has_baseline;
    return result;
}

Clay_TextLayoutResult LayoutEngine::Impl::layout_text(Clay_StringSlice text,
                                                      Clay_TextElementConfig *config,
                                                      float available_width, void *user_data) {
    auto &state = *static_cast<Impl *>(user_data);
    Clay_TextLayoutResult result{};
    if (!config || text.length < 0 || (!text.chars && text.length != 0))
        return result;

    // An empty editor still contributes a text node to the UI tree. Clay may
    // measure it with zero width, and no glyph layout or paragraph lines are
    // needed until the first character is inserted.
    if (text.length == 0) {
        result.success = true;
        result.dimensions = {
            std::isfinite(available_width) ? std::max(0.0f, available_width) : 0.0f,
            config->lineHeight > 0 ? static_cast<float>(config->lineHeight) : 0.0f};
        return result;
    }

    if (!std::isfinite(available_width))
        return result;
    if (available_width <= 0.0f) {
        result.success = true;
        result.dimensions = {0.0f, config->lineHeight > 0 ? static_cast<float>(config->lineHeight)
                                                          : 0.0f};
        return result;
    }

    {
        const std::string value(text.chars ? text.chars : "",
                                static_cast<std::size_t>(text.length));
        const auto *node = static_cast<const LayoutNode *>(config->userData);
        const TextLayoutOptions options = text_options_for_node(node, config);

        TextLayoutResult shaped;
        if (!state.text.layout_utf8(value.c_str(), available_width, options, &shaped) ||
            shaped.id == 0)
            return result;

        LayoutTextLayout native_layout;
        native_layout.id = shaped.id;
        native_layout.node_id =
            config->userData ? static_cast<const LayoutNode *>(config->userData)->id : 0;
        native_layout.text = value;
        native_layout.width = available_width;
        native_layout.height = shaped.bounds.height;
        const TextCaret first_caret = state.text.caret({0, 0});
        native_layout.first_line_baseline = first_caret.y;
        native_layout.has_baseline = std::isfinite(first_caret.y);
        native_layout.text_style.family = options.family;
        native_layout.text_style.font_size = options.font_size;
        native_layout.text_style.letter_spacing = options.letter_spacing;
        native_layout.paragraph_style.wrap = options.wrap;
        native_layout.paragraph_style.alignment = options.alignment;
        native_layout.paragraph_style.line_height = options.line_height;
        native_layout.paragraph_style.direction = options.direction;
        native_layout.lines.reserve(shaped.lines.size());
        state.callback_lines.clear();
        state.callback_lines.reserve(shaped.lines.size());
        for (const TextLayoutLine &line : shaped.lines) {
            if (line.text_offset > value.size() ||
                line.text_length > value.size() - line.text_offset ||
                line.text_length > static_cast<std::size_t>(INT32_MAX))
                return result;
            native_layout.lines.push_back(
                {line.text_offset,
                 line.text_length,
                 {line.bounds.x, line.bounds.y, line.bounds.width, line.bounds.height}});
            const char *line_chars = text.chars ? text.chars + line.text_offset : nullptr;
            state.callback_lines.push_back(
                {{line.bounds.width, line.bounds.height},
                 {static_cast<int32_t>(line.text_length), line_chars, text.chars},
                 {line.bounds.x, 0.0f}});
        }
        result.baseline = native_layout.first_line_baseline;
        result.hasBaseline = native_layout.has_baseline && std::isfinite(result.baseline);
        state.text_layouts[shaped.id] = std::move(native_layout);
        result.success = true;
        result.dimensions = {available_width, shaped.bounds.height};
        result.lineCount = static_cast<int32_t>(state.callback_lines.size());
        result.lines = state.callback_lines.data();
        result.layoutId = shaped.id;
        return result;
    }
}

namespace {

Clay_ElementId element_id(uint32_t id) {
    return Clay_GetElementIdWithIndex(CLAY_STRING("NativeKitNode"), id);
}

Clay_ElementDeclaration declaration_for(const LayoutNode &node) {
    Clay_ElementDeclaration declaration{};
    declaration.layout.sizing.width = clay_axis(node.style.width);
    declaration.layout.sizing.height = clay_axis(node.style.height);
    declaration.layout.layoutDirection = node.style.direction == LayoutDirection::LeftToRight
                                             ? CLAY_LEFT_TO_RIGHT
                                             : CLAY_TOP_TO_BOTTOM;
    declaration.layout.padding = {node.style.padding_left, node.style.padding_right,
                                  node.style.padding_top, node.style.padding_bottom};
    declaration.layout.childGap = node.style.child_gap;
    declaration.layout.rowGap = node.style.row_gap;
    declaration.layout.columnGap = node.style.column_gap;
    declaration.layout.wrapMode =
        static_cast<Clay_LayoutWrapMode>(static_cast<uint8_t>(node.style.wrap_mode));
    declaration.layout.alignSelf =
        static_cast<Clay_AlignSelf>(static_cast<uint8_t>(node.style.align_self));
    declaration.layout.childAlignment = {
        static_cast<Clay_LayoutAlignmentX>(static_cast<uint8_t>(node.style.child_align_x)),
        static_cast<Clay_LayoutAlignmentY>(static_cast<uint8_t>(node.style.child_align_y))};
    declaration.layout.childDistribution =
        static_cast<Clay_ChildDistribution>(node.style.child_distribution);
    declaration.aspectRatio.aspectRatio = node.style.aspect_ratio;
    if (node.style.positioning == LayoutPositioning::Absolute) {
        declaration.floating.offset = {node.style.position_x, node.style.position_y};
        declaration.floating.zIndex = static_cast<int16_t>(node.style.z_index);
        declaration.floating.attachPoints = {CLAY_ATTACH_POINT_LEFT_TOP,
                                             CLAY_ATTACH_POINT_LEFT_TOP};
        declaration.floating.attachTo = CLAY_ATTACH_TO_PARENT;
        declaration.floating.clipTo =
            node.style.clip_to_parent ? CLAY_CLIP_TO_ATTACHED_PARENT : CLAY_CLIP_TO_NONE;
    }
    declaration.backgroundColor = clay_color(node.style.background);
    declaration.cornerRadius = {node.style.radius_top_left, node.style.radius_top_right,
                                node.style.radius_bottom_left, node.style.radius_bottom_right};
    if (node.visual_kind == LayoutVisualKind::Custom)
        declaration.custom.customData = const_cast<LayoutNode *>(&node);
    declaration.clip.horizontal = node.style.clip_horizontal;
    declaration.clip.vertical = node.style.clip_vertical;
    declaration.userData = const_cast<LayoutNode *>(&node);
    return declaration;
}

template <typename LayoutState> void append_node(LayoutState &state, std::size_t index) {
    const auto &node = (*state.nodes)[index];
    const Clay_ElementId id = state.element_ids[index];
    Clay__OpenElementWithId(id);
    Clay__ConfigureOpenElement(declaration_for(node));

    if (node.visual_kind == LayoutVisualKind::Text) {
        Clay_TextElementConfig text_config{};
        text_config.userData = const_cast<LayoutNode *>(&node);
        text_config.textColor = clay_color(node.text_color);
        text_config.fontId = static_cast<uint16_t>(node.text_style.family);
        text_config.fontSize = clay_text_value(node.text_style.font_size);
        text_config.letterSpacing = clay_text_value(node.text_style.letter_spacing, true);
        text_config.lineHeight = clay_text_value(node.paragraph_style.line_height, true);
        text_config.wrapMode = clay_wrap(node.paragraph_style.wrap);
        text_config.textAlignment = clay_alignment(node.paragraph_style.alignment);
        Clay__OpenTextElement({false, static_cast<int32_t>(node.text.size()), node.text.c_str()},
                              text_config);
    }

    for (const std::size_t child : state.children[index])
        append_node(state, child);

    Clay__CloseElement();
}

LayoutRect rect_from(Clay_BoundingBox bounds) {
    return {bounds.x, bounds.y, bounds.width, bounds.height};
}

LayoutTransform compose(LayoutTransform parent, LayoutTransform local) {
    return {parent.a * local.a + parent.c * local.b,
            parent.b * local.a + parent.d * local.b,
            parent.a * local.c + parent.c * local.d,
            parent.b * local.c + parent.d * local.d,
            parent.a * local.tx + parent.c * local.ty + parent.tx,
            parent.b * local.tx + parent.d * local.ty + parent.ty};
}

LayoutTransform translated(float x, float y) {
    return {1.0f, 0.0f, 0.0f, 1.0f, x, y};
}

bool finite_transform(const LayoutTransform &transform) {
    return std::isfinite(transform.a) && std::isfinite(transform.b) && std::isfinite(transform.c) &&
           std::isfinite(transform.d) && std::isfinite(transform.tx) && std::isfinite(transform.ty);
}

bool axis_aligned(const LayoutTransform &transform) {
    return std::abs(transform.b) <= 0.00001f && std::abs(transform.c) <= 0.00001f;
}

LayoutRect transform_bounds(LayoutRect rect, LayoutTransform transform) {
    const auto x = [&](float px, float py) {
        return transform.a * px + transform.c * py + transform.tx;
    };
    const auto y = [&](float px, float py) {
        return transform.b * px + transform.d * py + transform.ty;
    };
    const float x0 = x(rect.x, rect.y);
    const float x1 = x(rect.x + rect.width, rect.y);
    const float x2 = x(rect.x, rect.y + rect.height);
    const float x3 = x(rect.x + rect.width, rect.y + rect.height);
    const float y0 = y(rect.x, rect.y);
    const float y1 = y(rect.x + rect.width, rect.y);
    const float y2 = y(rect.x, rect.y + rect.height);
    const float y3 = y(rect.x + rect.width, rect.y + rect.height);
    const float left = std::min({x0, x1, x2, x3});
    const float top = std::min({y0, y1, y2, y3});
    return {left, top, std::max({x0, x1, x2, x3}) - left, std::max({y0, y1, y2, y3}) - top};
}

bool inverse_transform(LayoutTransform transform, LayoutTransform &out) {
    const float determinant = transform.a * transform.d - transform.b * transform.c;
    if (!finite_transform(transform) || !std::isfinite(determinant) ||
        std::abs(determinant) < 0.000001f)
        return false;
    const float reciprocal = 1.0f / determinant;
    out = {transform.d * reciprocal,
           -transform.b * reciprocal,
           -transform.c * reciprocal,
           transform.a * reciprocal,
           (transform.c * transform.ty - transform.d * transform.tx) * reciprocal,
           (transform.b * transform.tx - transform.a * transform.ty) * reciprocal};
    return finite_transform(out);
}

LayoutRect intersect_axes(LayoutRect clip, LayoutRect bounds, bool horizontal, bool vertical) {
    if (horizontal) {
        const float right = std::min(clip.x + clip.width, bounds.x + bounds.width);
        clip.x = std::max(clip.x, bounds.x);
        clip.width = std::max(0.0f, right - clip.x);
    }
    if (vertical) {
        const float bottom = std::min(clip.y + clip.height, bounds.y + bounds.height);
        clip.y = std::max(clip.y, bounds.y);
        clip.height = std::max(0.0f, bottom - clip.y);
    }
    return clip;
}

LayoutRect intersect_rect(LayoutRect left, LayoutRect right) {
    return {std::max(left.x, right.x), std::max(left.y, right.y),
            std::max(0.0f, std::min(left.x + left.width, right.x + right.width) -
                               std::max(left.x, right.x)),
            std::max(0.0f, std::min(left.y + left.height, right.y + right.height) -
                               std::max(left.y, right.y))};
}

bool has_area(LayoutRect rect) {
    return rect.width > 0.0f && rect.height > 0.0f;
}

LayoutRect union_rect(LayoutRect left, LayoutRect right) {
    if (!has_area(left))
        return right;
    if (!has_area(right))
        return left;
    const float x = std::min(left.x, right.x);
    const float y = std::min(left.y, right.y);
    const float right_edge = std::max(left.x + left.width, right.x + right.width);
    const float bottom_edge = std::max(left.y + left.height, right.y + right.height);
    return {x, y, right_edge - x, bottom_edge - y};
}

LayoutColor color_from(Clay_Color color) {
    return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
}

void append_primitive(LayoutSnapshot &snapshot, const Clay_RenderCommand &command) {
    LayoutPrimitive primitive{};
    const auto *node = command.userData ? static_cast<const LayoutNode *>(command.userData) : nullptr;
    primitive.node_id = node ? node->id : command.id;
    if (node) {
        primitive.content_revision = node->content_revision;
        primitive.geometry_revision = node->geometry_revision;
        primitive.composite_revision = node->composite_revision;
    }
    primitive.bounds = rect_from(command.boundingBox);

    switch (command.commandType) {
    case CLAY_RENDER_COMMAND_TYPE_RECTANGLE:
        primitive.kind = LayoutPrimitiveKind::Rectangle;
        primitive.color = color_from(command.renderData.rectangle.backgroundColor);
        primitive.radius_top_left = command.renderData.rectangle.cornerRadius.topLeft;
        primitive.radius_top_right = command.renderData.rectangle.cornerRadius.topRight;
        primitive.radius_bottom_left = command.renderData.rectangle.cornerRadius.bottomLeft;
        primitive.radius_bottom_right = command.renderData.rectangle.cornerRadius.bottomRight;
        break;
    case CLAY_RENDER_COMMAND_TYPE_BORDER:
        primitive.kind = LayoutPrimitiveKind::Border;
        primitive.color = color_from(command.renderData.border.color);
        break;
    case CLAY_RENDER_COMMAND_TYPE_TEXT: {
        primitive.kind = LayoutPrimitiveKind::Text;
        primitive.color = color_from(command.renderData.text.textColor);
        if (command.renderData.text.stringContents.length > 0)
            primitive.text.assign(
                command.renderData.text.stringContents.chars,
                static_cast<std::size_t>(command.renderData.text.stringContents.length));
        if (node) {
            primitive.text_style = node->text_style;
            primitive.paragraph_style = node->paragraph_style;
        } else {
            primitive.text_style.family = static_cast<FontFamily>(command.renderData.text.fontId);
            primitive.text_style.font_size = static_cast<float>(command.renderData.text.fontSize);
            primitive.text_style.letter_spacing =
                static_cast<float>(command.renderData.text.letterSpacing);
            primitive.paragraph_style.line_height =
                static_cast<float>(command.renderData.text.lineHeight);
            primitive.paragraph_style.wrap = TextWrapMode::None;
        }
        primitive.text_layout_id = command.renderData.text.textLayoutId;
        primitive.text_line_index = command.renderData.text.textLineIndex;
        break;
    }
    case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
        primitive.kind = LayoutPrimitiveKind::Custom;
        break;
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START:
        primitive.kind = LayoutPrimitiveKind::ClipBegin;
        break;
    case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
        primitive.kind = LayoutPrimitiveKind::ClipEnd;
        break;
    default:
        return;
    }
    snapshot.primitives.push_back(std::move(primitive));
}

} // namespace

bool LayoutEngine::Impl::valid() const {
    return context && text.valid();
}

bool LayoutEngine::Impl::add_font(const char *path, FontFamily family) {
    return text.add_font(path, family);
}

bool LayoutEngine::Impl::add_font_from_data(const char *name, const void *data, std::size_t bytes,
                                            FontFamily family) {
    return text.add_font_from_data(name, data, bytes, family);
}

bool LayoutEngine::Impl::add_system_fallbacks() {
    return text.add_system_fallbacks();
}

bool LayoutEngine::Impl::layout(const std::vector<LayoutNode> &nodes, float width, float height,
                                float delta_seconds, LayoutSnapshot &out, LayoutError *error) {
    if (error)
        *error = {};
    out = {};
    if (!valid() || nodes.empty() || !std::isfinite(width) || !std::isfinite(height) ||
        !std::isfinite(delta_seconds) || width <= 0.0f || height <= 0.0f) {
        if (error)
            error->message = "invalid layout input";
        return false;
    }

    auto &state = *this;
    std::size_t required_elements = nodes.size();
    for (const auto &node : nodes) {
        if (node.visual_kind == LayoutVisualKind::Text) {
            if (required_elements == std::numeric_limits<std::size_t>::max()) {
                if (error)
                    error->message = "layout element count overflow";
                return false;
            }
            ++required_elements;
        }
    }
    // Clay also allocates its root container, and its array keeps one slot
    // available while checking capacity during OpenElement.
    if (required_elements == std::numeric_limits<std::size_t>::max()) {
        if (error)
            error->message = "layout element count overflow";
        return false;
    }
    ++required_elements;
    if (!state.reserve_elements(required_elements)) {
        if (error)
            error->message = "layout capacity could not grow for this submission";
        return false;
    }
    Clay_SetCurrentContext(state.context);
    state.nodes = &nodes;
    state.children.assign(nodes.size(), {});
    state.element_ids.resize(nodes.size());
    state.measure_node_ids.clear();
    state.clay_error.clear();
    state.text_layouts.clear();
    state.frame_measurements.clear();
    state.callback_lines.clear();

    std::size_t root = nodes.size();
    std::vector<uint8_t> marks(nodes.size(), 0);
    std::unordered_map<uint32_t, std::size_t> ids;
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto &node = nodes[index];
        if (!node.id || !ids.emplace(node.id, index).second) {
            if (error) {
                error->node_index = index;
                error->message = "layout node IDs must be unique and non-zero";
            }
            return false;
        }
        state.element_ids[index] = element_id(node.id);
        state.measure_node_ids[state.element_ids[index].id] = {node.id, node.measure_version};
        if (node.parent < 0) {
            if (root != nodes.size()) {
                if (error) {
                    error->node_index = index;
                    error->message = "layout tree must have exactly one root";
                }
                return false;
            }
            root = index;
        } else if (static_cast<std::size_t>(node.parent) >= nodes.size()) {
            if (error) {
                error->node_index = index;
                error->message = "layout node parent is out of range";
            }
            return false;
        } else {
            state.children[static_cast<std::size_t>(node.parent)].push_back(index);
        }
    }
    if (root == nodes.size()) {
        if (error)
            error->message = "layout tree must have exactly one root";
        return false;
    }

    const auto visit = [&](auto &&self, std::size_t index) -> bool {
        if (marks[index] == 1)
            return false;
        if (marks[index] == 2)
            return true;
        marks[index] = 1;
        for (const std::size_t child : state.children[index]) {
            if (!self(self, child))
                return false;
        }
        marks[index] = 2;
        return true;
    };
    if (!visit(visit, root) ||
        std::any_of(marks.begin(), marks.end(), [](uint8_t mark) { return mark != 2; })) {
        if (error)
            error->message = "layout tree contains a cycle or disconnected node";
        return false;
    }

    Clay_SetLayoutDimensions({width, height});
    Clay_BeginLayout();
    append_node(state, root);
    const Clay_RenderCommandArray commands = Clay_EndLayout(delta_seconds);
    if (!state.clay_error.empty()) {
        if (error)
            error->message = state.clay_error.c_str();
        return false;
    }

    std::vector<LayoutRect> node_bounds(nodes.size());
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const Clay_ElementData data = Clay_GetElementData(state.element_ids[index]);
        if (!data.found) {
            if (error) {
                error->node_index = index;
                error->message = "layout engine did not resolve a submitted node";
            }
            return false;
        }
        node_bounds[index] = rect_from(data.boundingBox);
    }

    out.text_layouts.reserve(state.text_layouts.size());
    std::vector<TextLayoutId> retained_text_layouts;
    retained_text_layouts.reserve(state.text_layouts.size());
    for (const auto &entry : state.text_layouts) {
        retained_text_layouts.push_back(entry.first);
        out.text_layouts.push_back(entry.second);
    }

    out.items.resize(nodes.size());
    out.child_indices.clear();
    out.child_indices.reserve(nodes.size() > 0 ? nodes.size() - 1 : 0);
    const LayoutRect viewport{0.0f, 0.0f, width, height};
    const auto resolve_geometry = [&](auto &&self, std::size_t index,
                                      LayoutTransform parent_transform, bool parent_visible,
                                      LayoutRect parent_clip, LayoutRect parent_bounds) -> bool {
        const auto &node = nodes[index];
        const LayoutRect node_bounds_rect = node_bounds[index];
        const float origin_x =
            node_bounds_rect.x + node_bounds_rect.width * node.style.transform_origin_x;
        const float origin_y =
            node_bounds_rect.y + node_bounds_rect.height * node.style.transform_origin_y;
        const LayoutTransform to_origin = translated(origin_x, origin_y);
        const LayoutTransform from_origin = translated(-origin_x, -origin_y);
        const LayoutTransform local_transform =
            compose(compose(to_origin, node.style.transform), from_origin);
        const LayoutTransform transform = compose(parent_transform, local_transform);
        const bool visible = parent_visible && node.style.visible;
        const LayoutRect transformed = transform_bounds(node_bounds[index], transform);
        LayoutRect item_clip = parent_clip;
        if (node.style.positioning == LayoutPositioning::Absolute && node.style.clip_to_parent) {
            const LayoutRect transformed_parent_bounds =
                transform_bounds(parent_bounds, parent_transform);
            item_clip = intersect_axes(item_clip, transformed_parent_bounds, true, true);
        }
        LayoutItem item{};
        item.id = node.id;
        item.parent_id = node.parent >= 0 ? nodes[static_cast<std::size_t>(node.parent)].id : 0;
        item.index = static_cast<uint32_t>(index);
        item.parent_index =
            node.parent >= 0 ? static_cast<uint32_t>(node.parent) : kInvalidLayoutIndex;
        item.child_offset = static_cast<uint32_t>(out.child_indices.size());
        item.child_count = static_cast<uint32_t>(state.children[index].size());
        for (const std::size_t child : state.children[index])
            out.child_indices.push_back(static_cast<uint32_t>(child));
        item.visual_kind = node.visual_kind;
        item.bounds = node_bounds[index];
        item.clip_bounds = item_clip;
        item.transform = transform;
        item.local_bounds = {0.0f, 0.0f, node_bounds[index].width, node_bounds[index].height};
        item.world_bounds = transformed;
        item.z_index = node.style.z_index;
        item.positioned_absolute = node.style.positioning == LayoutPositioning::Absolute;
        item.visible = visible;
        item.hit_self = node.hit_self;
        item.hit_children = node.hit_children;
        item.content_revision = node.content_revision;
        item.geometry_revision = node.geometry_revision;
        item.composite_revision = node.composite_revision;
        const float determinant = transform.a * transform.d - transform.b * transform.c;
        if (!finite_transform(transform) || !std::isfinite(determinant) ||
            std::abs(determinant) < 0.000001f || !std::isfinite(transformed.x) ||
            !std::isfinite(transformed.y) || !std::isfinite(transformed.width) ||
            !std::isfinite(transformed.height)) {
            if (error) {
                error->node_index = index;
                error->message = "layout transform is not finite and invertible";
            }
            return false;
        }
        if (!inverse_transform(transform, item.inverse_transform)) {
            if (error) {
                error->node_index = index;
                error->message = "layout transform inverse could not be resolved";
            }
            return false;
        }
        if (!axis_aligned(transform)) {
            if (node.style.clip_horizontal || node.style.clip_vertical) {
                if (error) {
                    error->node_index = index;
                    error->message = "rotated or skewed clipping is not supported";
                }
                return false;
            }
        }
        const float content_x = node_bounds[index].x + node.style.padding_left;
        const float content_y = node_bounds[index].y + node.style.padding_top;
        bool has_child = false;
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        for (const std::size_t child : state.children[index]) {
            const LayoutRect bounds = node_bounds[child];
            const float child_left = bounds.x - content_x;
            const float child_top = bounds.y - content_y;
            if (!has_child) {
                left = child_left;
                top = child_top;
                right = child_left + bounds.width;
                bottom = child_top + bounds.height;
                has_child = true;
            } else {
                left = std::min(left, child_left);
                top = std::min(top, child_top);
                right = std::max(right, child_left + bounds.width);
                bottom = std::max(bottom, child_top + bounds.height);
            }
        }
        if (has_child)
            item.content_bounds = {left, top, right - left, bottom - top};
        const auto text_layout = std::find_if(
            out.text_layouts.begin(), out.text_layouts.end(), [&](const LayoutTextLayout &layout) {
                return layout.node_id == node.id && layout.has_baseline;
            });
        if (text_layout != out.text_layouts.end()) {
            item.has_baseline = true;
            item.baseline = item.bounds.y + text_layout->first_line_baseline;
        }
        const auto measured = state.frame_measurements.find(node.id);
        if (measured != state.frame_measurements.end() && measured->second.has_baseline &&
            std::isfinite(measured->second.baseline) && measured->second.baseline >= 0.0f &&
            measured->second.baseline <= item.bounds.height) {
            item.has_baseline = true;
            item.baseline = item.bounds.y + measured->second.baseline;
        }
        out.items[index] = item;

        LayoutRect child_clip = item_clip;
        if (node.style.clip_horizontal || node.style.clip_vertical) {
            child_clip = intersect_axes(child_clip, transformed, node.style.clip_horizontal,
                                        node.style.clip_vertical);
        }
        for (const std::size_t child : state.children[index]) {
            if (!self(self, child, transform, visible, child_clip, node_bounds_rect))
                return false;
        }
        LayoutRect subtree = item.hit_self && item.visible
                                 ? intersect_rect(item.world_bounds, item.clip_bounds)
                                 : LayoutRect{};
        if (item.hit_children && item.visible) {
            for (const std::size_t child : state.children[index])
                subtree = union_rect(subtree, out.items[child].subtree_hit_bounds);
        }
        item.subtree_hit_bounds = subtree;
        out.items[index] = item;
        return true;
    };
    if (!resolve_geometry(resolve_geometry, root, LayoutTransform{}, true, viewport, viewport))
        return false;

    state.text.prune_layout_cache(retained_text_layouts, kAutomaticTextLayoutCacheEntries);

    for (int32_t index = 0; index < commands.length; ++index) {
        const Clay_RenderCommand *command =
            Clay_RenderCommandArray_Get(const_cast<Clay_RenderCommandArray *>(&commands), index);
        if (command) {
            append_primitive(out, *command);
            auto &primitive = out.primitives.back();
            const auto item =
                std::find_if(out.items.begin(), out.items.end(), [&](const LayoutItem &value) {
                    return value.id == primitive.node_id;
                });
            if (item != out.items.end()) {
                primitive.transform = item->transform;
                primitive.visible = item->visible;
            }
        }
    }

    // Clay emits a custom-element marker before that element's clip and
    // background commands. Move each marker to the end of its own contiguous
    // command group so custom content appears above its box and before its
    // children, while preserving Clay's z-index ordering.
    for (std::size_t index = 0; index < out.primitives.size(); ++index) {
        if (out.primitives[index].kind != LayoutPrimitiveKind::Custom)
            continue;
        const uint32_t node_id = out.primitives[index].node_id;
        std::size_t insertion = index + 1;
        while (insertion < out.primitives.size() && out.primitives[insertion].node_id == node_id)
            ++insertion;
        if (insertion > index + 1) {
            auto marker = std::move(out.primitives[index]);
            out.primitives.erase(out.primitives.begin() + index);
            out.primitives.insert(out.primitives.begin() + insertion - 1, std::move(marker));
            index = insertion - 1;
        }
    }

    // Publish one native paint-order key for every resolved node. A node's
    // absolute-positioned ancestor is its stacking owner: the entire owned
    // subtree compares as one layer against ordinary flow content, while
    // declaration/DFS order remains stable within an equal layer. This is
    // the same ordering used by geometric picking and by Clay's render roots.
    struct PaintOrderKey {
        int32_t layer_z = 0;
        bool floating = false;
        uint64_t owner_order = 0;
        uint64_t traversal_order = 0;
    };
    std::vector<PaintOrderKey> paint_keys(out.items.size());
    uint64_t traversal_order = 0;
    const auto collect_paint_order = [&](auto &&self, std::size_t index, int32_t layer_z,
                                         bool floating, uint64_t owner_order) -> void {
        const auto &node = nodes[index];
        const uint64_t node_order = traversal_order++;
        if (node.style.positioning == LayoutPositioning::Absolute) {
            layer_z = node.style.z_index;
            floating = true;
            owner_order = node_order;
        }
        paint_keys[index] = {layer_z, floating, owner_order, node_order};
        for (const std::size_t child : state.children[index])
            self(self, child, layer_z, floating, owner_order);
    };
    collect_paint_order(collect_paint_order, root, 0, false, 0);

    std::vector<std::size_t> paint_indices(out.items.size());
    for (std::size_t index = 0; index < paint_indices.size(); ++index)
        paint_indices[index] = index;
    std::stable_sort(paint_indices.begin(), paint_indices.end(),
                     [&](std::size_t left, std::size_t right) {
                         const auto &left_key = paint_keys[left];
                         const auto &right_key = paint_keys[right];
                         if (left_key.layer_z != right_key.layer_z)
                             return left_key.layer_z < right_key.layer_z;
                         if (left_key.floating != right_key.floating)
                             return !left_key.floating;
                         if (left_key.owner_order != right_key.owner_order)
                             return left_key.owner_order < right_key.owner_order;
                         return left_key.traversal_order < right_key.traversal_order;
                     });
    for (std::size_t order = 0; order < paint_indices.size(); ++order)
        out.items[paint_indices[order]].paint_order = order + 1;

    // Hit testing can walk children in reverse paint order and stop once a
    // confirmed candidate outranks every remaining sibling subtree. Keep this
    // ordering beside the resolved snapshot so queries do not sort per event.
    out.hit_child_indices.clear();
    out.hit_child_indices.reserve(out.child_indices.size());
    const auto build_hit_order = [&](auto &&self, std::size_t index) -> uint64_t {
        auto &item = out.items[index];
        std::vector<uint32_t> children;
        children.reserve(item.child_count);
        for (uint32_t child_offset = 0; child_offset < item.child_count; ++child_offset) {
            const std::size_t child_slot = static_cast<std::size_t>(item.child_offset) + child_offset;
            if (child_slot >= out.child_indices.size())
                continue;
            const uint32_t child = out.child_indices[child_slot];
            if (child >= out.items.size())
                continue;
            self(self, child);
            children.push_back(child);
        }
        std::sort(children.begin(), children.end(), [&](uint32_t left, uint32_t right) {
            if (out.items[left].subtree_paint_order != out.items[right].subtree_paint_order)
                return out.items[left].subtree_paint_order > out.items[right].subtree_paint_order;
            return left > right;
        });
        item.hit_child_offset = static_cast<uint32_t>(out.hit_child_indices.size());
        item.hit_child_count = static_cast<uint32_t>(children.size());
        out.hit_child_indices.insert(out.hit_child_indices.end(), children.begin(), children.end());

        uint64_t maximum = item.visible && item.hit_self ? item.paint_order : 0;
        if (item.visible && item.hit_children)
            for (const uint32_t child : children)
                maximum = std::max(maximum, out.items[child].subtree_paint_order);
        item.subtree_paint_order = maximum;
        return maximum;
    };
    build_hit_order(build_hit_order, root);

    return true;
}

bool LayoutEngine::update_transforms(const std::vector<LayoutNode> &nodes,
                                     LayoutSnapshot &snapshot, LayoutError *error) {
    if (error)
        *error = {};
    if (nodes.empty() || snapshot.items.size() != nodes.size()) {
        if (error)
            error->message = "transform update does not match the submitted layout";
        return false;
    }

    std::size_t root = nodes.size();
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto &item = snapshot.items[index];
        const auto &node = nodes[index];
        if (item.id != node.id || item.index != index ||
            (node.parent < 0) != (item.parent_index == kInvalidLayoutIndex) ||
            (node.parent >= 0 && static_cast<std::size_t>(node.parent) != item.parent_index)) {
            if (error) {
                error->node_index = index;
                error->message = "transform update changed the layout tree";
            }
            return false;
        }
        if (node.parent < 0) {
            if (root != nodes.size()) {
                if (error) {
                    error->node_index = index;
                    error->message = "transform update has multiple roots";
                }
                return false;
            }
            root = index;
        }
    }
    if (root == nodes.size()) {
        if (error)
            error->message = "transform update has no root";
        return false;
    }

    const LayoutRect viewport = snapshot.items[root].clip_bounds;
    std::vector<uint8_t> visited(nodes.size(), 0);
    const auto resolve_geometry = [&](auto &&self, std::size_t index,
                                      LayoutTransform parent_transform, bool parent_visible,
                                      LayoutRect parent_clip, LayoutRect parent_bounds) -> bool {
        if (index >= nodes.size() || visited[index]) {
            if (error) {
                error->node_index = index < nodes.size() ? index : 0;
                error->message = "transform update contains an invalid layout tree";
            }
            return false;
        }
        visited[index] = 1;
        auto &item = snapshot.items[index];
        const auto &node = nodes[index];
        const LayoutRect node_bounds_rect = item.bounds;
        const float origin_x =
            node_bounds_rect.x + node_bounds_rect.width * node.style.transform_origin_x;
        const float origin_y =
            node_bounds_rect.y + node_bounds_rect.height * node.style.transform_origin_y;
        const LayoutTransform to_origin = translated(origin_x, origin_y);
        const LayoutTransform from_origin = translated(-origin_x, -origin_y);
        const LayoutTransform local_transform =
            compose(compose(to_origin, node.style.transform), from_origin);
        const LayoutTransform transform = compose(parent_transform, local_transform);
        const bool visible = parent_visible && node.style.visible;
        const LayoutRect transformed = transform_bounds(node_bounds_rect, transform);
        LayoutRect item_clip = parent_clip;
        if (node.style.positioning == LayoutPositioning::Absolute && node.style.clip_to_parent) {
            const LayoutRect transformed_parent_bounds =
                transform_bounds(parent_bounds, parent_transform);
            item_clip = intersect_axes(item_clip, transformed_parent_bounds, true, true);
        }
        const float determinant = transform.a * transform.d - transform.b * transform.c;
        if (!finite_transform(transform) || !std::isfinite(determinant) ||
            std::abs(determinant) < 0.000001f || !std::isfinite(transformed.x) ||
            !std::isfinite(transformed.y) || !std::isfinite(transformed.width) ||
            !std::isfinite(transformed.height)) {
            if (error) {
                error->node_index = index;
                error->message = "layout transform is not finite and invertible";
            }
            return false;
        }
        if (!inverse_transform(transform, item.inverse_transform)) {
            if (error) {
                error->node_index = index;
                error->message = "layout transform inverse could not be resolved";
            }
            return false;
        }
        if (!axis_aligned(transform) && (node.style.clip_horizontal || node.style.clip_vertical)) {
            if (error) {
                error->node_index = index;
                error->message = "rotated or skewed clipping is not supported";
            }
            return false;
        }

        item.clip_bounds = item_clip;
        item.transform = transform;
        item.world_bounds = transformed;
        item.visible = visible;
        item.hit_self = node.hit_self;
        item.hit_children = node.hit_children;
        item.content_revision = node.content_revision;
        item.geometry_revision = node.geometry_revision;
        item.composite_revision = node.composite_revision;

        LayoutRect child_clip = item_clip;
        if (node.style.clip_horizontal || node.style.clip_vertical)
            child_clip = intersect_axes(child_clip, transformed, node.style.clip_horizontal,
                                        node.style.clip_vertical);
        for (uint32_t child_offset = 0; child_offset < item.child_count; ++child_offset) {
            const std::size_t child_slot = static_cast<std::size_t>(item.child_offset) + child_offset;
            if (child_slot >= snapshot.child_indices.size()) {
                if (error) {
                    error->node_index = index;
                    error->message = "transform update has invalid child indices";
                }
                return false;
            }
            const std::size_t child = snapshot.child_indices[child_slot];
            if (child >= nodes.size() || nodes[child].parent != static_cast<int32_t>(index) ||
                !self(self, child, transform, visible, child_clip, node_bounds_rect))
                return false;
        }
        LayoutRect subtree = item.hit_self && item.visible
                                 ? intersect_rect(item.world_bounds, item.clip_bounds)
                                 : LayoutRect{};
        if (item.hit_children && item.visible) {
            for (uint32_t child_offset = 0; child_offset < item.child_count; ++child_offset) {
                const std::size_t child_slot = static_cast<std::size_t>(item.child_offset) + child_offset;
                if (child_slot >= snapshot.child_indices.size()) {
                    if (error) {
                        error->node_index = index;
                        error->message = "transform update has invalid child indices";
                    }
                    return false;
                }
                const std::size_t child = snapshot.child_indices[child_slot];
                if (child >= nodes.size()) {
                    if (error) {
                        error->node_index = index;
                        error->message = "transform update has an invalid child";
                    }
                    return false;
                }
                subtree = union_rect(subtree, snapshot.items[child].subtree_hit_bounds);
            }
        }
        item.subtree_hit_bounds = subtree;
        return true;
    };

    if (!resolve_geometry(resolve_geometry, root, LayoutTransform{}, true, viewport, viewport) ||
        std::any_of(visited.begin(), visited.end(), [](uint8_t value) { return value == 0; })) {
        if (error && !error->message)
            error->message = "transform update did not visit every layout node";
        return false;
    }

    for (auto &primitive : snapshot.primitives) {
        const auto item = std::find_if(snapshot.items.begin(), snapshot.items.end(),
                                       [&](const LayoutItem &value) {
                                           return value.id == primitive.node_id;
                                       });
        if (item == snapshot.items.end())
            continue;
        primitive.transform = item->transform;
        primitive.visible = item->visible;
        primitive.content_revision = item->content_revision;
        primitive.geometry_revision = item->geometry_revision;
        primitive.composite_revision = item->composite_revision;
    }
    return true;
}

LayoutEngine::LayoutEngine(std::size_t initial_capacity)
    : impl_(std::make_unique<Impl>(initial_capacity)) {}

LayoutEngine::LayoutEngine(std::shared_ptr<FontCollection> fonts, std::size_t initial_capacity)
    : impl_(std::make_unique<Impl>(initial_capacity, std::move(fonts))) {}

LayoutEngine::~LayoutEngine() = default;

bool LayoutEngine::valid() const {
    return impl_ && impl_->valid();
}

TextEngine *LayoutEngine::text_engine() {
    return impl_ ? &impl_->text : nullptr;
}

const TextEngine *LayoutEngine::text_engine() const {
    return impl_ ? &impl_->text : nullptr;
}

bool LayoutEngine::add_font(const char *path, FontFamily family) {
    return impl_ && impl_->add_font(path, family);
}

bool LayoutEngine::add_font_from_data(const char *name, const void *data, std::size_t bytes,
                                      FontFamily family) {
    return impl_ && impl_->add_font_from_data(name, data, bytes, family);
}

bool LayoutEngine::add_system_fallbacks() {
    return impl_ && impl_->add_system_fallbacks();
}

void LayoutEngine::set_measure_callback(LayoutMeasureCallback callback) {
    if (impl_) {
        impl_->measure_callback = std::move(callback);
        impl_->measure_cache.clear();
    }
}

LayoutMeasureStats LayoutEngine::measure_stats() const {
    if (!impl_)
        return {};
    return {impl_->measure_requests,     impl_->measure_cache_hits,
            impl_->measure_cache_misses, impl_->measure_callback_calls,
            impl_->measure_cache.size(), kIntrinsicMeasureCacheEntries};
}

bool LayoutEngine::layout(const std::vector<LayoutNode> &nodes, float width, float height,
                          float delta_seconds, LayoutSnapshot &out, LayoutError *error) {
    if (!impl_) {
        if (error)
            error->message = "layout implementation is unavailable";
        return false;
    }
    return impl_->layout(nodes, width, height, delta_seconds, out, error);
}

} // namespace nkui
