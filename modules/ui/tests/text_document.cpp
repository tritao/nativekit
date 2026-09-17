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
    const nkui_paragraph_style paragraph_style = {
        sizeof(nkui_paragraph_style), 0.0f, NKUI_TEXT_WRAP_WORD_CHARACTER, NKUI_TEXT_ALIGN_START,
        NKUI_TEXT_DIRECTION_AUTO};
    nkui_resource document = {0};
    if (nkui_text_document_create(fonts, "a\xCC\x81\xF0\x9F\x99\x82", 240.0f, &text_style,
                                  &paragraph_style, &document) != NKUI_OK)
        return 2;

    int32_t length = -1;
    nkui_text_document_selection selection = {0};
    if (nkui_text_document_get_length(document, &length) != NKUI_OK || length != 3 ||
        nkui_text_document_get_selection(document, &selection) != NKUI_OK ||
        selection.start != 3 || selection.end != 3)
        return 3;

    const auto insert_status =
        nkui_text_document_apply_edit(document, 3, 3, " 日本", 6, 6, 0, 0, -1, -1);
    if (insert_status != NKUI_OK || document_text(document) != "a\xCC\x81\xF0\x9F\x99\x82 日本") {
        return 4;
    }

    const auto composition_status =
        nkui_text_document_apply_edit(document, 6, 6, "語", 7, 7, 0, 1, 6, 7);
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

    nkui_text_position hit = {0};
    nkui_text_caret caret = {0};
    if (nkui_text_document_hit_test(document, 1.0f, 1.0f, &hit) != NKUI_OK || hit.offset < 0 ||
        hit.offset > 7 || nkui_text_document_caret(document, hit, &caret) != NKUI_OK)
        return 9;

    if (nkui_text_document_undo(document) != NKUI_OK ||
        document_text(document) != "a\xCC\x81\xF0\x9F\x99\x82 日本" ||
        nkui_text_document_get_composition(document, &composition) != NKUI_OK ||
        composition.active || nkui_text_document_redo(document) != NKUI_OK ||
        document_text(document) != "a\xCC\x81\xF0\x9F\x99\x82 日本語")
        return 10;

    if (nkui_text_document_apply_edit(document, 99, 99, "x", 0, 0, 0, 0, -1, -1) !=
        NKUI_ERROR_INVALID_ARGUMENT)
        return 11;

    if (nkui_resource_destroy(document) != NKUI_OK || nkui_resource_destroy(fonts) != NKUI_OK)
        return 12;
    return 0;
}
