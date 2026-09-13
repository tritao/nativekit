#include "layout/layout_engine.h"

#include "prepare/skribidi_adapter.h"

#define CLAY_IMPLEMENTATION
#include "clay.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>

namespace nkui {
namespace {

constexpr std::size_t kAutomaticTextLayoutCacheEntries = 128;

Clay_Color clay_color(LayoutColor color) {
    return {color.red * 255.0f, color.green * 255.0f, color.blue * 255.0f, color.alpha * 255.0f};
}

Clay_SizingAxis clay_axis(LayoutAxis axis) {
    switch (axis.sizing) {
    case LayoutSizing::Grow:
        return CLAY_SIZING_GROW(axis.value);
    case LayoutSizing::Fixed:
        return CLAY_SIZING_FIXED(axis.value);
    case LayoutSizing::Percent:
        return CLAY_SIZING_PERCENT(axis.value);
    case LayoutSizing::Fit:
    default:
        return CLAY_SIZING_FIT(axis.value);
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
    explicit Impl(std::size_t max_nodes_value, std::shared_ptr<SkribidiFontCollection> fonts = {})
        : max_nodes(max_nodes_value),
          text(fonts ? std::move(fonts) : std::make_shared<SkribidiFontCollection>()) {
        Clay_SetMaxElementCount(static_cast<int32_t>(max_nodes + 1));
        Clay_SetMaxMeasureTextCacheWordCount(static_cast<int32_t>(max_nodes * 8 + 32));
        clay_memory.resize(Clay_MinMemorySize());
        if (clay_memory.empty())
            return;

        Clay_ErrorHandler error_handler{};
        error_handler.errorHandlerFunction = [](Clay_ErrorData data) {
            auto *state = static_cast<Impl *>(data.userData);
            state->clay_error = data.errorText.chars ? data.errorText.chars : "Clay error";
        };
        error_handler.userData = this;
        context = Clay_Initialize(
            Clay_CreateArenaWithCapacityAndMemory(clay_memory.size(), clay_memory.data()),
            {1.0f, 1.0f}, error_handler);
        if (context) {
            Clay_SetMeasureTextFunction(measure_text, this);
            Clay_SetMeasureTextIntrinsicFunction(measure_intrinsic_text, this);
            Clay_SetLayoutTextFunction(layout_text, this);
        }
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
    static Clay_TextLayoutResult layout_text(Clay_StringSlice text, Clay_TextElementConfig *config,
                                             float available_width, void *user_data);

    std::size_t max_nodes = 0;
    std::vector<char> clay_memory;
    Clay_Context *context = nullptr;
    SkribidiAdapter text;
    const std::vector<LayoutNode> *nodes = nullptr;
    std::vector<std::vector<std::size_t>> children;
    std::vector<Clay_ElementId> element_ids;
    std::unordered_map<TextLayoutId, LayoutTextLayout> text_layouts;
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
    TextRect bounds;
    if (!state.text.measure_intrinsic_utf8(value.c_str(), options, &bounds))
        return result;
    result.unwrappedDimensions = {bounds.width, config->lineHeight > 0
                                                    ? static_cast<float>(config->lineHeight)
                                                    : bounds.height};
    // External paragraph engines may break at character boundaries, so the
    // safe lower bound is zero unless the engine exposes a stronger one.
    result.minWidth = 0.0f;
    return result;
}

Clay_TextLayoutResult LayoutEngine::Impl::layout_text(Clay_StringSlice text,
                                                      Clay_TextElementConfig *config,
                                                      float available_width, void *user_data) {
    auto &state = *static_cast<Impl *>(user_data);
    Clay_TextLayoutResult result{};
    if (!config || text.length < 0 || (!text.chars && text.length != 0) ||
        !std::isfinite(available_width) || available_width <= 0.0f)
        return result;

    try {
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
        state.text_layouts[shaped.id] = std::move(native_layout);
        result.success = true;
        result.dimensions = {available_width, shaped.bounds.height};
        result.lineCount = static_cast<int32_t>(state.callback_lines.size());
        result.lines = state.callback_lines.data();
        result.layoutId = shaped.id;
        return result;
    } catch (...) {
        return {};
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
    declaration.layout.childAlignment = {
        static_cast<Clay_LayoutAlignmentX>(node.style.child_align_x),
        static_cast<Clay_LayoutAlignmentY>(node.style.child_align_y)};
    if (node.style.positioning == LayoutPositioning::Absolute) {
        declaration.floating.offset = {node.style.position_x, node.style.position_y};
        declaration.floating.zIndex = static_cast<int16_t>(node.style.z_index);
        declaration.floating.attachPoints = {
            CLAY_ATTACH_POINT_LEFT_TOP, CLAY_ATTACH_POINT_LEFT_TOP};
        declaration.floating.attachTo = CLAY_ATTACH_TO_PARENT;
        declaration.floating.clipTo = node.style.clip_to_parent
            ? CLAY_CLIP_TO_ATTACHED_PARENT
            : CLAY_CLIP_TO_NONE;
    }
    declaration.backgroundColor = clay_color(node.style.background);
    declaration.cornerRadius = {node.style.radius_top_left, node.style.radius_top_right,
                                node.style.radius_bottom_left, node.style.radius_bottom_right};
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
    return std::isfinite(transform.a) && std::isfinite(transform.b) &&
           std::isfinite(transform.c) && std::isfinite(transform.d) &&
           std::isfinite(transform.tx) && std::isfinite(transform.ty);
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
    return {left, top, std::max({x0, x1, x2, x3}) - left,
            std::max({y0, y1, y2, y3}) - top};
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

LayoutColor color_from(Clay_Color color) {
    return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
}

void append_primitive(LayoutSnapshot &snapshot, const Clay_RenderCommand &command) {
    LayoutPrimitive primitive{};
    primitive.node_id =
        command.userData ? static_cast<const LayoutNode *>(command.userData)->id : command.id;
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
        const auto *node = static_cast<const LayoutNode *>(command.userData);
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
        !std::isfinite(delta_seconds) || width <= 0.0f || height <= 0.0f ||
        nodes.size() > max_nodes) {
        if (error)
            error->message = "invalid layout input";
        return false;
    }

    auto &state = *this;
    state.nodes = &nodes;
    state.children.assign(nodes.size(), {});
    state.element_ids.resize(nodes.size());
    state.clay_error.clear();
    state.text_layouts.clear();
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
    const LayoutRect viewport{0.0f, 0.0f, width, height};
    const auto resolve_geometry = [&](auto &&self, std::size_t index,
                                      LayoutTransform parent_transform, bool parent_visible,
                                      LayoutRect parent_clip) -> bool {
        const auto &node = nodes[index];
        const LayoutRect node_bounds_rect = node_bounds[index];
        const LayoutTransform to_origin = translated(node_bounds_rect.x, node_bounds_rect.y);
        const LayoutTransform from_origin = translated(-node_bounds_rect.x, -node_bounds_rect.y);
        const LayoutTransform local_transform =
            compose(compose(to_origin, node.style.transform), from_origin);
        const LayoutTransform transform = compose(parent_transform, local_transform);
        const bool visible = parent_visible && node.style.visible;
        const LayoutRect transformed = transform_bounds(node_bounds[index], transform);
        LayoutItem item{};
        item.id = node.id;
        item.visual_kind = node.visual_kind;
        item.bounds = node_bounds[index];
        item.clip_bounds = parent_clip;
        item.transform = transform;
        item.visible = visible;
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
        const auto text_layout =
            std::find_if(out.text_layouts.begin(), out.text_layouts.end(),
                         [&](const LayoutTextLayout &layout) {
                             return layout.node_id == node.id && layout.has_baseline;
                         });
        if (text_layout != out.text_layouts.end()) {
            item.has_baseline = true;
            item.baseline = item.bounds.y + text_layout->first_line_baseline;
        }
        out.items[index] = item;

        LayoutRect child_clip = parent_clip;
        if (node.style.clip_horizontal || node.style.clip_vertical) {
            child_clip = intersect_axes(child_clip, transformed, node.style.clip_horizontal,
                                        node.style.clip_vertical);
        }
        for (const std::size_t child : state.children[index]) {
            if (!self(self, child, transform, visible, child_clip))
                return false;
        }
        return true;
    };
    if (!resolve_geometry(resolve_geometry, root, LayoutTransform{}, true, viewport))
        return false;

    state.text.prune_layout_cache(retained_text_layouts, kAutomaticTextLayoutCacheEntries);

    for (int32_t index = 0; index < commands.length; ++index) {
        const Clay_RenderCommand *command =
            Clay_RenderCommandArray_Get(const_cast<Clay_RenderCommandArray *>(&commands), index);
        if (command) {
            append_primitive(out, *command);
            auto &primitive = out.primitives.back();
            const auto item = std::find_if(out.items.begin(), out.items.end(),
                                           [&](const LayoutItem &value) {
                                               return value.id == primitive.node_id;
                                           });
            if (item != out.items.end()) {
                primitive.transform = item->transform;
                primitive.visible = item->visible;
            }
        }
    }

    return true;
}

LayoutEngine::LayoutEngine(std::size_t max_nodes) : impl_(std::make_unique<Impl>(max_nodes)) {}

LayoutEngine::LayoutEngine(std::shared_ptr<SkribidiFontCollection> fonts, std::size_t max_nodes)
    : impl_(std::make_unique<Impl>(max_nodes, std::move(fonts))) {}

LayoutEngine::~LayoutEngine() = default;

bool LayoutEngine::valid() const {
    return impl_ && impl_->valid();
}

SkribidiAdapter *LayoutEngine::text_adapter() {
    return impl_ ? &impl_->text : nullptr;
}

const SkribidiAdapter *LayoutEngine::text_adapter() const {
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
