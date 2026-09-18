#include "skribidi_document_engine.h"

#include "skribidi/skb_attributes.h"
#include "skribidi/skb_common.h"
#include "skribidi/skb_editor.h"
#include "skribidi/skb_text.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace nkui {

namespace {

constexpr int32_t kTemporaryBlockSize = 512 * 1024;

skb_attribute_set_t make_text_attributes(const TextLayoutOptions &options,
                                         skb_attribute_t *attributes) {
    attributes[0] = skb_attribute_make_font_size(options.font_size);
    attributes[1] = skb_attribute_make_font_family(static_cast<uint8_t>(options.family));
    attributes[2] = skb_attribute_make_letter_spacing(options.letter_spacing);
    attributes[3] = skb_attribute_make_line_height(
        options.line_height > 0.0f ? SKB_LINE_HEIGHT_ABSOLUTE : SKB_LINE_HEIGHT_NORMAL,
        options.line_height);
    attributes[4] = skb_attribute_make_paint_color(SKB_PAINT_TEXT, SKB_PAINT_STATE_DEFAULT,
                                                    skb_rgba(255, 255, 255, 255));
    return {.attributes = attributes, .attributes_count = 5};
}

skb_attribute_set_t make_layout_attributes(const TextLayoutOptions &options,
                                           skb_attribute_t *attributes) {
    const auto wrap = options.wrap == TextWrapMode::None   ? SKB_WRAP_NONE
                      : options.wrap == TextWrapMode::Word ? SKB_WRAP_WORD
                                                            : SKB_WRAP_WORD_CHAR;
    const auto alignment = options.alignment == TextAlignment::Center ? SKB_ALIGN_CENTER
                          : options.alignment == TextAlignment::End  ? SKB_ALIGN_END
                                                                      : SKB_ALIGN_START;
    const auto direction = options.direction == TextDirection::Ltr   ? SKB_DIRECTION_LTR
                           : options.direction == TextDirection::Rtl ? SKB_DIRECTION_RTL
                                                                       : SKB_DIRECTION_AUTO;
    attributes[0] = skb_attribute_make_text_wrap(wrap);
    attributes[1] = skb_attribute_make_horizontal_align(alignment);
    attributes[2] = skb_attribute_make_text_base_direction(direction);
    return {.attributes = attributes, .attributes_count = 3};
}

} // namespace

SkribidiDocumentEngine::SkribidiDocumentEngine(std::shared_ptr<SkribidiFontCollection> fonts,
                                               const char *initial_text, float width,
                                               const TextLayoutOptions &options)
    : fonts_(std::move(fonts)) {
    if (!fonts_ || !fonts_->valid() || !std::isfinite(width) || width <= 0.0f ||
        !std::isfinite(options.font_size) || options.font_size <= 0.0f ||
        !std::isfinite(options.letter_spacing) || !std::isfinite(options.line_height) ||
        options.line_height < 0.0f)
        return;

    temporary_ = skb_temp_alloc_create(kTemporaryBlockSize);
    if (!temporary_)
        return;

    skb_attribute_t text_attributes[5];
    skb_attribute_t layout_attributes[3];
    const skb_editor_params_t params = {
        .font_collection = fonts_->native_handle(),
        .icon_collection = nullptr,
        .attribute_collection = nullptr,
        .editor_width = width,
        .editor_height = 1000000.0f,
        .layout_attributes = make_layout_attributes(options, layout_attributes),
        .paragraph_attributes = make_text_attributes(options, text_attributes),
        .composition_attributes = {},
        .caret_mode = SKB_CARET_MODE_SIMPLE,
        .editor_behavior = SKB_BEHAVIOR_DEFAULT,
        .max_undo_levels = 100,
    };
    editor_ = skb_editor_create(&params);
    if (!editor_)
        return;

    skb_editor_set_text_utf8(editor_, temporary_, initial_text ? initial_text : "", -1);
    const int32_t initial_length = skb_editor_get_text_utf32_count(editor_);
    skb_editor_set_selection(editor_, {{initial_length, SKB_AFFINITY_NONE},
                                       {initial_length, SKB_AFFINITY_NONE}});
    composition_ = {-1, -1};
}

SkribidiDocumentEngine::~SkribidiDocumentEngine() {
    if (editor_)
        skb_editor_destroy(editor_);
    if (temporary_)
        skb_temp_alloc_destroy(temporary_);
}

bool SkribidiDocumentEngine::valid() const {
    return fonts_ && fonts_->valid() && temporary_ && editor_;
}

bool SkribidiDocumentEngine::valid_range(int32_t start, int32_t end) const {
    return start >= 0 && end >= start && end <= document_length();
}

bool SkribidiDocumentEngine::apply_edit(const SkribidiEditTransaction &transaction) {
    if (!valid() || !transaction.replacement_text ||
        !valid_range(transaction.replacement_start, transaction.replacement_end))
        return false;

    const std::size_t replacement_length = std::strlen(transaction.replacement_text);
    if (replacement_length > static_cast<std::size_t>(std::numeric_limits<int32_t>::max()))
        return false;
    const int32_t replacement_codepoints = skb_utf8_to_utf32_count(
        transaction.replacement_text, static_cast<int32_t>(replacement_length));
    if (replacement_codepoints < 0)
        return false;
    const int32_t resulting_length = document_length() -
                                     (transaction.replacement_end - transaction.replacement_start) +
                                     replacement_codepoints;
    if (transaction.selection_start < 0 || transaction.selection_end < transaction.selection_start ||
        transaction.selection_end > resulting_length)
        return false;
    if (transaction.has_composition &&
        (transaction.composition_start < 0 || transaction.composition_end < transaction.composition_start ||
         transaction.composition_end > resulting_length))
        return false;

    skb_temp_alloc_reset(temporary_);
    skb_text_t *replacement_text = skb_text_create();
    if (!replacement_text)
        return false;
    skb_text_append_utf8(replacement_text, transaction.replacement_text,
                         static_cast<int32_t>(replacement_length), skb_attribute_set_t{});

    const skb_text_range_t replacement_range = {
        {transaction.replacement_start, SKB_AFFINITY_NONE},
        {transaction.replacement_end, SKB_AFFINITY_NONE},
    };
    const skb_edit_transaction_t edit = {
        .replacement = replacement_range,
        .replacement_text = replacement_text,
        .resulting_selection = {
            {transaction.selection_start, SKB_AFFINITY_NONE},
            {transaction.selection_end,
             static_cast<skb_caret_affinity_t>(transaction.selection_affinity)},
        },
        .history_kind = SKB_EDIT_HISTORY_GENERIC,
    };
    const skb_result_t result = skb_editor_apply_transaction(editor_, temporary_, &edit);
    skb_text_destroy(replacement_text);
    if (result != SKB_RESULT_SUCCESS)
        return false;

    if (transaction.has_composition) {
        composition_ = {transaction.composition_start, transaction.composition_end};
        has_composition_ = true;
    } else {
        composition_ = {-1, -1};
        has_composition_ = false;
    }
    return true;
}

std::string SkribidiDocumentEngine::text_utf8() const {
    if (!valid())
        return {};
    const int32_t count = skb_editor_get_text_utf8_count(editor_);
    if (count <= 0)
        return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    const int32_t copied = skb_editor_get_text_utf8(editor_, result.data(), count);
    result.resize(static_cast<std::size_t>(std::max(0, copied)));
    return result;
}

int32_t SkribidiDocumentEngine::document_length() const {
    return valid() ? skb_editor_get_text_utf32_count(editor_) : 0;
}

TextRange SkribidiDocumentEngine::selection() const {
    if (!valid())
        return {};
    const skb_selection_t value = skb_editor_get_selection(editor_);
    return {value.anchor.offset, value.focus.offset};
}

uint8_t SkribidiDocumentEngine::selection_affinity() const {
    if (!valid())
        return 0;
    return static_cast<uint8_t>(skb_editor_get_selection(editor_).focus.affinity);
}

TextRange SkribidiDocumentEngine::composition() const {
    return composition_;
}

bool SkribidiDocumentEngine::has_composition() const {
    return has_composition_;
}

bool SkribidiDocumentEngine::undo() {
    if (!valid() || !skb_editor_can_undo(editor_))
        return false;
    skb_temp_alloc_reset(temporary_);
    skb_editor_undo(editor_, temporary_);
    composition_ = {-1, -1};
    has_composition_ = false;
    return true;
}

bool SkribidiDocumentEngine::redo() {
    if (!valid() || !skb_editor_can_redo(editor_))
        return false;
    skb_temp_alloc_reset(temporary_);
    skb_editor_redo(editor_, temporary_);
    composition_ = {-1, -1};
    has_composition_ = false;
    return true;
}

TextPosition SkribidiDocumentEngine::hit_test(float x, float y) const {
    if (!valid() || !std::isfinite(x) || !std::isfinite(y))
        return {};
    const skb_text_position_t value = skb_editor_hit_test(editor_, SKB_MOVEMENT_CARET, x, y);
    return {value.offset, static_cast<uint8_t>(value.affinity)};
}

TextCaret SkribidiDocumentEngine::caret(TextPosition position) const {
    if (!valid() || position.offset < 0 || position.offset > document_length())
        return {};
    const auto value = skb_editor_get_caret_info_at(
        editor_, {position.offset, static_cast<skb_caret_affinity_t>(position.affinity)});
    return {value.x, value.y, value.ascender, value.descender, value.slope, value.direction};
}

std::vector<TextRect> SkribidiDocumentEngine::selection_rects(TextPosition start,
                                                              TextPosition end) const {
    std::vector<TextRect> result;
    if (!valid() || start.offset < 0 || end.offset < 0 || start.offset > document_length() ||
        end.offset > document_length())
        return result;
    const auto collect = [](skb_rect2_t rect, void *context) {
        static_cast<std::vector<TextRect> *>(context)->push_back(
            {rect.x, rect.y, rect.width, rect.height});
    };
    skb_editor_iterate_text_range_bounds(
        editor_, {{start.offset, static_cast<skb_caret_affinity_t>(start.affinity)},
                  {end.offset, static_cast<skb_caret_affinity_t>(end.affinity)}},
        collect, &result);
    return result;
}

} // namespace nkui
