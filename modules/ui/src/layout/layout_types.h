#ifndef NATIVEKIT_UI_LAYOUT_TYPES_H
#define NATIVEKIT_UI_LAYOUT_TYPES_H

#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace nkui {

enum class LayoutNodeKind : uint8_t {
    Box = 1,
    Text,
    Button,
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

enum class LayoutSizing : uint8_t {
    Fit = 0,
    Grow,
    Fixed,
    Percent,
};

struct LayoutAxis {
    LayoutSizing sizing = LayoutSizing::Fit;
    float value = 0.0f;
};

struct LayoutColor {
    float red = 0.0f;
    float green = 0.0f;
    float blue = 0.0f;
    float alpha = 0.0f;
};

struct LayoutStyle {
    LayoutAxis width{};
    LayoutAxis height{};
    LayoutDirection direction = LayoutDirection::TopToBottom;
    uint16_t padding_left = 0;
    uint16_t padding_right = 0;
    uint16_t padding_top = 0;
    uint16_t padding_bottom = 0;
    uint16_t child_gap = 0;
    LayoutColor background{};
    float radius_top_left = 0.0f;
    float radius_top_right = 0.0f;
    float radius_bottom_left = 0.0f;
    float radius_bottom_right = 0.0f;
    bool clip_horizontal = false;
    bool clip_vertical = false;
};

/** A flat, frame-scoped semantic UI tree. Parent indices refer to this array. */
struct LayoutNode {
    uint32_t id = 0;
    int32_t parent = -1;
    LayoutNodeKind kind = LayoutNodeKind::Box;
    LayoutStyle style{};
    std::string text;
    LayoutColor text_color{1.0f, 1.0f, 1.0f, 1.0f};
    TextStyle text_style{};
    ParagraphStyle paragraph_style{};
};

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
    LayoutNodeKind kind = LayoutNodeKind::Box;
    LayoutRect bounds{};
    bool hovered = false;
};

enum class LayoutPrimitiveKind : uint8_t {
    Rectangle = 1,
    Border,
    Text,
    ClipBegin,
    ClipEnd,
};

struct LayoutPrimitive {
    LayoutPrimitiveKind kind = LayoutPrimitiveKind::Rectangle;
    uint32_t node_id = 0;
    LayoutRect bounds{};
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
    TextStyle text_style{};
    ParagraphStyle paragraph_style{};
    std::vector<LayoutTextLine> lines;
};

struct LayoutEvent {
    enum class Kind : uint8_t {
        ButtonActivated = 1,
    };

    Kind kind = Kind::ButtonActivated;
    uint32_t node_id = 0;
};

struct LayoutSnapshot {
    std::vector<LayoutItem> items;
    std::vector<LayoutPrimitive> primitives;
    std::vector<LayoutTextLayout> text_layouts;
    std::vector<LayoutEvent> events;

    const LayoutItem *find(uint32_t id) const {
        const auto found = std::find_if(items.begin(), items.end(),
                                        [id](const LayoutItem &item) { return item.id == id; });
        return found == items.end() ? nullptr : &*found;
    }

    std::optional<uint32_t> hit_test(float x, float y) const {
        const auto contains = [x, y](const LayoutItem &item) {
            return x >= item.bounds.x && y >= item.bounds.y &&
                   x < item.bounds.x + item.bounds.width && y < item.bounds.y + item.bounds.height;
        };
        for (auto item = items.rbegin(); item != items.rend(); ++item) {
            if (item->kind == LayoutNodeKind::Button && contains(*item))
                return item->id;
        }
        for (auto item = items.rbegin(); item != items.rend(); ++item) {
            if (contains(*item))
                return item->id;
        }
        return std::nullopt;
    }
};

} // namespace nkui

#endif
