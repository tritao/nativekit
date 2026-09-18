#include "nativekit_ui.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifndef NKUI_TEST_FONT_PATH
#error NKUI_TEST_FONT_PATH is required
#endif

namespace {

std::string document_text(nkui_resource document) {
    uint32_t bytes = 0;
    if (nkui_text_document_get_text(document, nullptr, &bytes) != NKUI_OK)
        return {};
    std::vector<uint8_t> buffer(bytes);
    if (nkui_text_document_get_text(document, buffer.data(), &bytes) != NKUI_OK)
        return {};
    return std::string(reinterpret_cast<const char *>(buffer.data()), bytes);
}

} // namespace

int main() {
    nkui_resource fonts = {0};
    if (nkui_font_collection_create(&fonts) != NKUI_OK ||
        nkui_font_collection_add(fonts, NKUI_TEST_FONT_PATH, NKUI_FONT_FAMILY_DEFAULT) != NKUI_OK)
        return 1;

    const nkui_text_style text_style = {sizeof(nkui_text_style), NKUI_FONT_FAMILY_DEFAULT, 18.0f,
                                        0.0f};
    const nkui_paragraph_style paragraph_style = {sizeof(nkui_paragraph_style), 0.0f,
                                                  NKUI_TEXT_WRAP_WORD_CHARACTER,
                                                  NKUI_TEXT_ALIGN_START, NKUI_TEXT_DIRECTION_AUTO};
    nkui_resource document = {0};
    if (nkui_text_document_create(fonts, "a\xCC\x81\xF0\x9F\x99\x82", 240.0f, &text_style,
                                  &paragraph_style, &document) != NKUI_OK)
        return 2;

    int32_t length = -1;
    nkui_text_document_selection selection = {0};
    if (nkui_text_document_get_length(document, &length) != NKUI_OK || length != 3 ||
        nkui_text_document_get_selection(document, &selection) != NKUI_OK || selection.start != 3 ||
        selection.end != 3)
        return 3;

    const auto insert_status =
        nkui_text_document_apply_edit(document, 3, 3, " 日本", 6, 6, 0, 0, -1, -1, 0);
    if (insert_status != NKUI_OK || document_text(document) != "a\xCC\x81\xF0\x9F\x99\x82 日本") {
        return 4;
    }

    const auto composition_status =
        nkui_text_document_apply_edit(document, 6, 6, "語", 7, 7, 0, 1, 6, 7, 6);
    if (composition_status != NKUI_OK) {
        return 5;
    }
    nkui_text_document_composition composition = {0};
    if (nkui_text_document_get_composition(document, &composition) != NKUI_OK ||
        !composition.active || composition.start != 6 || composition.end != 7 ||
        document_text(document) != "a\xCC\x81\xF0\x9F\x99\x82 日本語") {
        return 6;
    }

    nkui_text_position start = {0, 0};
    nkui_text_position end = {7, 0};
    uint32_t layout_bytes = 0;
    if (nkui_text_document_get_layout(document, start, end, nullptr, &layout_bytes) != NKUI_OK ||
        layout_bytes < sizeof(nkui_text_rect) || layout_bytes % sizeof(nkui_text_rect) != 0)
        return 7;
    std::vector<uint8_t> layout(layout_bytes);
    if (nkui_text_document_get_layout(document, start, end, layout.data(), &layout_bytes) !=
            NKUI_OK ||
        reinterpret_cast<const nkui_text_rect *>(layout.data())->struct_size !=
            sizeof(nkui_text_rect))
        return 8;

    uint32_t range_layout_bytes = 0;
    if (nkui_text_document_get_range_layout(document, start, end, nullptr, &range_layout_bytes) !=
            NKUI_OK ||
        range_layout_bytes < sizeof(nkui_text_range_rect) ||
        range_layout_bytes % sizeof(nkui_text_range_rect) != 0)
        return 9;
    std::vector<uint8_t> range_layout(range_layout_bytes);
    if (nkui_text_document_get_range_layout(document, start, end, range_layout.data(),
                                            &range_layout_bytes) != NKUI_OK)
        return 10;
    for (std::size_t offset = 0; offset < range_layout.size();
         offset += sizeof(nkui_text_range_rect)) {
        nkui_text_range_rect rectangle = {};
        std::memcpy(&rectangle, range_layout.data() + offset, sizeof(rectangle));
        if (rectangle.struct_size != sizeof(nkui_text_range_rect) || rectangle.range_start < 0 ||
            rectangle.range_end < rectangle.range_start || rectangle.range_end > 7)
            return 11;
    }

    nkui_resource multiline_document = {0};
    if (nkui_text_document_create(fonts, "a\xCC\x81\xF0\x9F\x99\x82\n日本語", 240.0f, &text_style,
                                  &paragraph_style, &multiline_document) != NKUI_OK)
        return 12;
    nkui_text_position multiline_start = {0, 0};
    nkui_text_position multiline_end = {7, 0};
    uint32_t multiline_bytes = 0;
    if (nkui_text_document_get_range_layout(multiline_document, multiline_start, multiline_end,
                                            nullptr, &multiline_bytes) != NKUI_OK ||
        multiline_bytes < sizeof(nkui_text_range_rect) ||
        multiline_bytes % sizeof(nkui_text_range_rect) != 0)
        return 13;
    std::vector<uint8_t> multiline_layout(multiline_bytes);
    if (nkui_text_document_get_range_layout(multiline_document, multiline_start, multiline_end,
                                            multiline_layout.data(), &multiline_bytes) != NKUI_OK)
        return 14;
    bool has_fragment_range = false;
    for (std::size_t offset = 0; offset < multiline_layout.size();
         offset += sizeof(nkui_text_range_rect)) {
        nkui_text_range_rect rectangle = {};
        std::memcpy(&rectangle, multiline_layout.data() + offset, sizeof(rectangle));
        if (rectangle.struct_size != sizeof(nkui_text_range_rect) || rectangle.range_start < 0 ||
            rectangle.range_end < rectangle.range_start || rectangle.range_end > 7)
            return 15;
        has_fragment_range |= rectangle.range_start != multiline_start.offset ||
                              rectangle.range_end != multiline_end.offset;
    }
    if (!has_fragment_range || nkui_resource_destroy(multiline_document) != NKUI_OK)
        return 16;

    uint32_t surrounding_bytes = 0;
    nkui_text_document_range surrounding_range = {0};
    if (nkui_text_document_get_surrounding_range(document, 2, 0, &surrounding_range) != NKUI_OK ||
        surrounding_range.struct_size != sizeof(surrounding_range) ||
        surrounding_range.start != 5 || surrounding_range.end != 7 ||
        nkui_text_document_get_surrounding_text(document, 2, 0, nullptr, &surrounding_bytes) !=
            NKUI_OK ||
        surrounding_bytes != 6)
        return 17;
    std::vector<uint8_t> surrounding(surrounding_bytes);
    if (nkui_text_document_get_surrounding_text(document, 2, 0, surrounding.data(),
                                                &surrounding_bytes) != NKUI_OK ||
        std::string(reinterpret_cast<const char *>(surrounding.data()), surrounding_bytes) !=
            "本語")
        return 18;

    nkui_text_position hit = {0};
    nkui_text_caret caret = {0};
    if (nkui_text_document_hit_test(document, 1.0f, 1.0f, &hit) != NKUI_OK || hit.offset < 0 ||
        hit.offset > 7 || nkui_text_document_caret(document, hit, &caret) != NKUI_OK)
        return 19;

    if (nkui_text_document_cancel_composition(document) != NKUI_OK ||
        document_text(document) != "a\xCC\x81\xF0\x9F\x99\x82 日本" ||
        nkui_text_document_get_composition(document, &composition) != NKUI_OK ||
        composition.active ||
        nkui_text_document_apply_edit(document, 6, 6, "語", 7, 7, 0, 1, 6, 7, 6) != NKUI_OK ||
        nkui_text_document_commit_composition(document) != NKUI_OK ||
        nkui_text_document_get_composition(document, &composition) != NKUI_OK ||
        composition.active || nkui_text_document_undo(document) != NKUI_OK ||
        document_text(document) != "a\xCC\x81\xF0\x9F\x99\x82 日本" ||
        nkui_text_document_redo(document) != NKUI_OK ||
        document_text(document) != "a\xCC\x81\xF0\x9F\x99\x82 日本語")
        return 20;

    if (nkui_text_document_apply_edit(document, 99, 99, "x", 0, 0, 0, 0, -1, -1, 0) !=
        NKUI_ERROR_INVALID_ARGUMENT)
        return 21;

    if (nkui_resource_destroy(document) != NKUI_OK || nkui_resource_destroy(fonts) != NKUI_OK)
        return 22;
    return 0;
}
