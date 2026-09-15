#ifndef NATIVEKIT_UI_LAYOUT_TYPES_H
#define NATIVEKIT_UI_LAYOUT_TYPES_H

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace nkui {

enum class LayoutVisualKind : uint8_t {
    Box = 1,
    Text,
    Image,
    Custom,
};

// NativeKit-owned font family selector. Concrete text engines translate this
// value to their own font collection model.
enum class FontFamily : uint8_t {
    Default = 0,
    Emoji = 1,
};

enum class TextWrapMode : uint8_t {
    None = 0,
    Word,
    WordCharacter,
};

enum class TextAlignment : uint8_t {
    Start = 0,
    Center,
    End,
};

enum class TextDirection : uint8_t {
    Auto = 0,
    Ltr,
    Rtl,
};

struct TextStyle {
    FontFamily family = FontFamily::Default;
    float font_size = 16.0f;
    float letter_spacing = 0.0f;
};

struct ParagraphStyle {
    TextWrapMode wrap = TextWrapMode::WordCharacter;
    TextAlignment alignment = TextAlignment::Start;
    float line_height = 0.0f;
    TextDirection direction = TextDirection::Auto;
};

using TextLayoutId = uint64_t;

enum class LayoutDirection : uint8_t {
    LeftToRight = 0,
    TopToBottom,
};

enum class LayoutAlignmentX : uint8_t {
    Start = 0,
    End,
    Center,
};

enum class LayoutAlignmentY : uint8_t {
    Start = 0,
    End,
    Center,
    Baseline,
};

enum class LayoutWrapMode : uint8_t {
    NoWrap = 0,
    Wrap,
};

enum class LayoutSelfAlignment : uint8_t {
    Inherit = 0,
    Start,
    End,
    Center,
    Baseline,
};

enum class LayoutDistribution : uint8_t {
    Start = 0,
    Center,
    End,
    SpaceBetween,
    SpaceAround,
    SpaceEvenly,
};

enum class LayoutPositioning : uint8_t {
    Flow = 0,
    Absolute,
};

enum class LayoutSizing : uint8_t {
    Fit = 0,
    Grow,
    Fixed,
    Percent,
};

struct LayoutAxis {
    LayoutSizing sizing = LayoutSizing::Fit;
    // Used by FIXED and PERCENT.
    float value = 0.0f;
    // FIT/GROW constraints are passed directly to Clay. A zero max means
    // unbounded, matching Clay's CLAY_SIZING_FIT/GROW convention.
    float min = 0.0f;
    float max = 0.0f;
    // Relative share of extra space for GROW sizing. Other sizing modes ignore
    // this field. The wire/API layer requires a positive finite value.
    float grow_weight = 1.0f;
};

struct LayoutColor {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 0.0f;
};

struct LayoutTransform {
    float a = 1.0f;
    float b = 0.0f;
    float c = 0.0f;
    float d = 1.0f;
    float tx = 0.0f;
    float ty = 0.0f;
};

struct LayoutStyle {
    LayoutAxis width{};
    LayoutAxis height{};
    float aspect_ratio = 0.0f;
    LayoutDirection direction = LayoutDirection::TopToBottom;
    float padding_left = 0.0f;
    float padding_right = 0.0f;
    float padding_top = 0.0f;
    float padding_bottom = 0.0f;
    float child_gap = 0.0f;
    float row_gap = 0.0f;
    float column_gap = 0.0f;
    LayoutWrapMode wrap_mode = LayoutWrapMode::NoWrap;
    LayoutSelfAlignment align_self = LayoutSelfAlignment::Inherit;
    LayoutAlignmentX child_align_x = LayoutAlignmentX::Start;
    LayoutAlignmentY child_align_y = LayoutAlignmentY::Start;
    LayoutDistribution child_distribution = LayoutDistribution::Start;
    LayoutPositioning positioning = LayoutPositioning::Flow;
    float position_x = 0.0f;
    float position_y = 0.0f;
    int32_t z_index = 0;
    bool clip_to_parent = true;
    LayoutColor background{};
    float radius_top_left = 0.0f;
    float radius_top_right = 0.0f;
    float radius_bottom_left = 0.0f;
    float radius_bottom_right = 0.0f;
    bool clip_horizontal = false;
    bool clip_vertical = false;
    bool visible = true;
    LayoutTransform transform{};
};

/** A flat, frame-scoped render/layout tree. Parent indices refer to this array. */
struct LayoutNode {
    uint32_t id = 0;
    int32_t parent = -1;
    LayoutVisualKind visual_kind = LayoutVisualKind::Box;
    LayoutStyle style{};
    std::string text;
    LayoutColor text_color{1.0f, 1.0f, 1.0f, 1.0f};
    TextStyle text_style{};
    ParagraphStyle paragraph_style{};
};

/** NativeKit-owned constraints for opaque external content measurement. */
struct LayoutMeasureConstraints {
    float min_width = 0.0f;
    float max_width = 0.0f;
    float min_height = 0.0f;
    float max_height = 0.0f;
};

/** NativeKit-owned intrinsic metrics for opaque external content. */
struct LayoutMeasureResult {
    float width = 0.0f;
    float height = 0.0f;
    float baseline = 0.0f;
    bool has_baseline = false;
};

using LayoutMeasureCallback =
    std::function<LayoutMeasureResult(uint32_t, const LayoutMeasureConstraints &)>;

struct LayoutError {
    std::size_t node_index = 0;
    const char *message = nullptr;
};

struct LayoutRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct LayoutItem {
    uint32_t id = 0;
    LayoutVisualKind visual_kind = LayoutVisualKind::Box;
    LayoutRect bounds{};
    LayoutRect clip_bounds{};
    LayoutRect content_bounds{};
    LayoutTransform transform{};
    float baseline = 0.0f;
    bool visible = true;
    bool has_baseline = false;
};

enum class LayoutPrimitiveKind : uint8_t {
    Rectangle = 1,
    Border,
    Text,
    ClipBegin,
    ClipEnd,
    Custom,
};

struct LayoutPrimitive {
    LayoutPrimitiveKind kind = LayoutPrimitiveKind::Rectangle;
    uint32_t node_id = 0;
    LayoutRect bounds{};
    LayoutTransform transform{};
    bool visible = true;
    LayoutColor color{};
    float radius_top_left = 0.0f;
    float radius_top_right = 0.0f;
    float radius_bottom_left = 0.0f;
    float radius_bottom_right = 0.0f;
    std::string text;
    TextStyle text_style{};
    ParagraphStyle paragraph_style{};
    TextLayoutId text_layout_id = 0;
    uint32_t text_line_index = 0;
};

struct LayoutTextLine {
    std::size_t text_offset = 0;
    std::size_t text_length = 0;
    LayoutRect bounds{};
};

struct LayoutTextLayout {
    TextLayoutId id = 0;
    uint32_t node_id = 0;
    std::string text;
    float width = 0.0f;
    float height = 0.0f;
    float first_line_baseline = 0.0f;
    bool has_baseline = false;
    TextStyle text_style{};
    ParagraphStyle paragraph_style{};
    std::vector<LayoutTextLine> lines;
};

struct LayoutSnapshot {
    std::vector<LayoutItem> items;
    std::vector<LayoutPrimitive> primitives;
    std::vector<LayoutTextLayout> text_layouts;

    const LayoutItem *find(uint32_t id) const {
        const auto found = std::find_if(items.begin(), items.end(),
                                        [id](const LayoutItem &item) { return item.id == id; });
        return found == items.end() ? nullptr : &*found;
    }
};

} // namespace nkui

#endif
