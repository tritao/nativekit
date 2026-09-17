#include "nativekit_ui.h"

#include <string.h>

#ifndef NKUI_TEST_FONT_PATH
#error NKUI_TEST_FONT_PATH is required
#endif

int main(void) {
    if (nkui_api_version() != NKUI_API_VERSION)
        return 1;
    nkui_display_list list = {0};
    if (nkui_display_list_create(&list) != NKUI_OK || !list.id)
        return 2;
    const nkui_path_element path_elements[] = {
        {NKUI_PATH_MOVE_TO, {0.0f, 0.0f}},
        {NKUI_PATH_LINE_TO, {32.0f, 0.0f}},
        {NKUI_PATH_LINE_TO, {16.0f, 32.0f}},
        {NKUI_PATH_CLOSE, {0.0f}},
    };
    nkui_resource path = {0};
    nkui_resource paint = {0};
    nkui_resource gradient = {0};
    nkui_resource image = {0};
    const uint8_t pixels[] = {255, 0, 0, 255};
    const nkui_gradient_stop gradient_stops[] = {
        {0.0f, {1.0f, 0.0f, 0.0f, 1.0f}},
        {0.5f, {0.0f, 1.0f, 0.0f, 1.0f}},
        {1.0f, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    if (nkui_path_create(path_elements, 4, &path) != NKUI_OK ||
        nkui_paint_create_solid((nkui_color){1.0f, 0.0f, 0.0f, 1.0f}, &paint) != NKUI_OK ||
        nkui_paint_create_linear_gradient(0.0f, 0.0f, 32.0f, 0.0f, gradient_stops,
                                          sizeof(gradient_stops) / sizeof(gradient_stops[0]),
                                          &gradient) != NKUI_OK ||
        nkui_image_create(1, 1, NKUI_IMAGE_RGBA8, pixels, sizeof(pixels), &image) != NKUI_OK)
        return 2;
    nkui_gradient_stop invalid_stops[] = {gradient_stops[1], gradient_stops[0]};
    if (nkui_paint_create_linear_gradient(0.0f, 0.0f, 32.0f, 0.0f, invalid_stops, 2,
                                          &image) != NKUI_ERROR_INVALID_ARGUMENT ||
        nkui_paint_create_linear_gradient(0.0f, 0.0f, 32.0f, 0.0f, gradient_stops, 1,
                                          &image) != NKUI_ERROR_INVALID_ARGUMENT ||
        nkui_paint_create_linear_gradient(0.0f, 0.0f, 0.0f, 0.0f, gradient_stops, 3,
                                          &image) != NKUI_ERROR_INVALID_ARGUMENT)
        return 2;
    nkui_resource_command draw = {{NKUI_COMMAND_DRAW_PATH, NKUI_COMMAND_VERSION, sizeof(draw)},
                                  path};
    nkui_stroke_path_command stroke = {
        {NKUI_COMMAND_STROKE_PATH, NKUI_COMMAND_VERSION, sizeof(stroke)}, path, 4.0f,
        NKUI_PATH_LINE_CAP_ROUND, NKUI_PATH_LINE_JOIN_MITER, 10.0f};
    nkui_layer_command begin = {0};
    begin.header = (nkui_command_header){NKUI_COMMAND_BEGIN_LAYER, NKUI_LAYER_COMMAND_VERSION,
                                         sizeof(begin)};
    begin.opacity = 0.5f;
    begin.composite_mode = NKUI_COMPOSITE_SOURCE_OVER;
    begin.x = 4.0f;
    begin.y = 8.0f;
    begin.width = 32.0f;
    begin.height = 24.0f;
    begin.flags = NKUI_LAYER_ISOLATED | NKUI_LAYER_HAS_BOUNDS;
    nkui_command_header end = {NKUI_COMMAND_END_LAYER, NKUI_COMMAND_VERSION, sizeof(end)};
    uint8_t commands[sizeof(draw) + sizeof(stroke) + sizeof(begin) + sizeof(end)];
    memcpy(commands, &draw, sizeof(draw));
    memcpy(commands + sizeof(draw), &stroke, sizeof(stroke));
    memcpy(commands + sizeof(draw) + sizeof(stroke), &begin, sizeof(begin));
    memcpy(commands + sizeof(draw) + sizeof(stroke) + sizeof(begin), &end, sizeof(end));
    if (nkui_display_list_submit(list, commands, sizeof(commands)) != NKUI_OK)
        return 3;
    nkui_transaction_info info = {0};
    if (nkui_display_list_get_info(list, &info) != NKUI_OK || info.command_count != 4 ||
        info.command_bytes != sizeof(commands))
        return 4;
    commands[2] = 2;
    if (nkui_display_list_submit(list, commands, sizeof(commands)) !=
            NKUI_ERROR_INVALID_TRANSACTION ||
        nkui_display_list_get_info(list, &info) != NKUI_OK || info.command_count != 4)
        return 5;
    if (nkui_display_list_destroy(list) != NKUI_OK ||
        nkui_display_list_reset(list) != NKUI_ERROR_INVALID_HANDLE)
        return 6;
    nkui_resource fonts = {0};
    nkui_resource layout = {0};
    nkui_text_style text_style = {sizeof(text_style), NKUI_FONT_FAMILY_DEFAULT, 24.0f, -0.25f};
    nkui_paragraph_style paragraph_style = {sizeof(paragraph_style), 0.0f,
                                            NKUI_TEXT_WRAP_WORD_CHARACTER,
                                            NKUI_TEXT_ALIGN_CENTER, NKUI_TEXT_DIRECTION_AUTO};
    if (nkui_font_collection_create(&fonts) != NKUI_OK ||
        nkui_font_collection_add(fonts, NKUI_TEST_FONT_PATH, NKUI_FONT_FAMILY_DEFAULT) != NKUI_OK)
        return 7;
    nkui_resource empty_layout = {0};
    if (nkui_text_layout_create_styled(fonts, NULL, 120.0f, &text_style,
                                       &paragraph_style, &empty_layout) != NKUI_OK ||
        nkui_text_layout_set_text(empty_layout, NULL) != NKUI_OK ||
        nkui_text_layout_update(empty_layout, NULL, 120.0f, &text_style,
                                &paragraph_style) != NKUI_OK ||
        nkui_resource_destroy(empty_layout) != NKUI_OK)
        return 21;
    nkui_text_style invalid_text_style = text_style;
    nkui_paragraph_style invalid_paragraph_style = paragraph_style;
    invalid_text_style.family = (nkui_font_family)-1;
    if (nkui_text_layout_create_styled(fonts, "NativeKit مرحبا", 300.0f,
                                       &invalid_text_style, &paragraph_style, &layout) !=
        NKUI_ERROR_INVALID_ARGUMENT)
        return 12;
    invalid_text_style = text_style;
    invalid_paragraph_style.wrap = (nkui_text_wrap)-1;
    if (nkui_text_layout_create_styled(fonts, "NativeKit مرحبا", 300.0f,
                                       &invalid_text_style, &invalid_paragraph_style, &layout) !=
        NKUI_ERROR_INVALID_ARGUMENT)
        return 13;
    invalid_paragraph_style = paragraph_style;
    invalid_paragraph_style.alignment = (nkui_text_alignment)-1;
    if (nkui_text_layout_create_styled(fonts, "NativeKit مرحبا", 300.0f,
                                       &text_style, &invalid_paragraph_style, &layout) !=
        NKUI_ERROR_INVALID_ARGUMENT)
        return 14;
    invalid_paragraph_style = paragraph_style;
    invalid_paragraph_style.direction = (nkui_text_direction)-1;
    if (nkui_text_layout_create_styled(fonts, "NativeKit مرحبا", 300.0f,
                                       &text_style, &invalid_paragraph_style, &layout) !=
        NKUI_ERROR_INVALID_ARGUMENT)
        return 15;
    if (nkui_text_layout_create_styled(fonts, "NativeKit مرحبا", 300.0f, &text_style,
                                       &paragraph_style, &layout) != NKUI_OK)
        return 7;
    nkui_resource later_layout = {0};
    if (nkui_font_collection_add(fonts, NKUI_TEST_FONT_PATH, NKUI_FONT_FAMILY_DEFAULT) !=
            NKUI_OK ||
        nkui_text_layout_create(fonts, "copy-on-write", 200.0f, 16.0f, &later_layout) != NKUI_OK ||
        nkui_resource_destroy(later_layout) != NKUI_OK)
        return 16;
    nkui_text_metrics metrics = {0};
    nkui_text_position position = {0};
    nkui_text_caret caret = {0};
    if (nkui_text_layout_measure(layout, &metrics) != NKUI_OK || metrics.width <= 0.0f ||
        nkui_text_layout_hit_test(layout, 0.0f, 0.0f, &position) != NKUI_OK ||
        nkui_text_layout_caret(layout, position, &caret) != NKUI_OK)
        return 8;
    uint32_t selection_bytes = 0;
    const nkui_text_position selection_start = {0, 0};
    const nkui_text_position selection_end = {9, 0};
    if (nkui_text_layout_get_selection_rects(layout, selection_start, selection_end, NULL,
                                             &selection_bytes) != NKUI_OK ||
        selection_bytes < sizeof(nkui_text_rect) ||
        selection_bytes % sizeof(nkui_text_rect) != 0)
        return 17;
    nkui_text_rect selection_rects[16] = {0};
    uint32_t selection_capacity = (uint32_t)sizeof(selection_rects);
    if (selection_bytes > selection_capacity ||
        nkui_text_layout_get_selection_rects(layout, selection_start, selection_end,
                                             (uint8_t *)selection_rects,
                                             &selection_capacity) != NKUI_OK ||
        selection_capacity != selection_bytes || selection_rects[0].struct_size !=
                                                     sizeof(nkui_text_rect) ||
        selection_rects[0].width <= 0.0f || selection_rects[0].height <= 0.0f)
        return 18;
    uint32_t collapsed_bytes = 0;
    if (nkui_text_layout_get_selection_rects(layout, selection_start, selection_start, NULL,
                                             &collapsed_bytes) != NKUI_OK ||
        collapsed_bytes != 0 ||
        nkui_text_layout_get_selection_rects(layout, (nkui_text_position){0, 5}, selection_end,
                                             NULL, &collapsed_bytes) !=
            NKUI_ERROR_INVALID_ARGUMENT)
        return 19;
    nkui_resource grapheme_layout = {0};
    int32_t grapheme_next = -1;
    int32_t grapheme_previous = -1;
    const char grapheme_text[] = "a\xcc\x81" "b";
    if (nkui_text_layout_create(fonts, grapheme_text, 120.0f, 18.0f,
                                &grapheme_layout) != NKUI_OK ||
        nkui_text_layout_next_grapheme(grapheme_layout, 0, &grapheme_next) != NKUI_OK ||
        nkui_text_layout_previous_grapheme(grapheme_layout, 2, &grapheme_previous) != NKUI_OK ||
        grapheme_next != 2 || grapheme_previous != 0 ||
        nkui_text_layout_next_grapheme(grapheme_layout, -1, &grapheme_next) !=
            NKUI_ERROR_INVALID_ARGUMENT ||
        nkui_resource_destroy(grapheme_layout) != NKUI_OK)
        return 20;
    nkui_resource word_layout = {0};
    const char word_text[] = "one two caf\xc3\xa9\nnext";
    int32_t word_start = -1;
    int32_t word_end = -1;
    int32_t word_offset = -1;
    if (nkui_text_layout_create(fonts, word_text, 400.0f, 18.0f, &word_layout) != NKUI_OK)
        return 22;
    if (nkui_text_layout_word_range_at(word_layout, 5, &word_start, &word_end) != NKUI_OK ||
        word_start != 4 || word_end != 7)
        return 23;
    if (nkui_text_layout_move_word(word_layout, 0, 1, NKUI_TEXT_NAVIGATION_BEHAVIOR_STANDARD,
                                   &word_offset) != NKUI_OK || word_offset != 4)
        return 24;
    if (nkui_text_layout_move_word(word_layout, 4, 1, NKUI_TEXT_NAVIGATION_BEHAVIOR_MACOS,
                                   &word_offset) != NKUI_OK || word_offset != 7)
        return 25;
    if (nkui_text_layout_move_word(word_layout, 8, -1, NKUI_TEXT_NAVIGATION_BEHAVIOR_STANDARD,
                                   &word_offset) != NKUI_OK || word_offset != 4)
        return 26;
    if (nkui_text_layout_move_paragraph(word_layout, 5, 1,
                                        NKUI_TEXT_NAVIGATION_BEHAVIOR_STANDARD,
                                        &word_offset) != NKUI_OK || word_offset != 13)
        return 27;
    if (nkui_text_layout_move_paragraph(word_layout, 13, -1,
                                        NKUI_TEXT_NAVIGATION_BEHAVIOR_STANDARD,
                                        &word_offset) != NKUI_OK || word_offset != 0)
        return 28;
    if (nkui_text_layout_move_paragraph(word_layout, 5, 1,
                                        NKUI_TEXT_NAVIGATION_BEHAVIOR_MACOS,
                                        &word_offset) != NKUI_OK || word_offset != 12)
        return 29;
    if (nkui_text_layout_line_range_at(word_layout, 5, &word_start, &word_end) != NKUI_OK ||
        word_start > 5 || word_end <= 5)
        return 30;
    if (nkui_text_layout_word_range_at(word_layout, -1, &word_start, &word_end) !=
            NKUI_ERROR_INVALID_ARGUMENT ||
        nkui_text_layout_move_word(word_layout, 0, 0, NKUI_TEXT_NAVIGATION_BEHAVIOR_STANDARD,
                                   &word_offset) != NKUI_ERROR_INVALID_ARGUMENT)
        return 31;
    if (nkui_resource_destroy(word_layout) != NKUI_OK)
        return 32;
    text_style.letter_spacing = 0.0f;
    paragraph_style.alignment = NKUI_TEXT_ALIGN_START;
    if (nkui_text_layout_update(layout, "NativeKit updated مرحبا", 280.0f, &text_style,
                                 &paragraph_style) != NKUI_OK)
        return 8;
    if (nkui_resource_destroy(layout) != NKUI_OK ||
        nkui_text_layout_measure(layout, &metrics) != NKUI_ERROR_INVALID_HANDLE ||
        nkui_resource_destroy(fonts) != NKUI_OK)
        return 9;
    if (nkui_resource_destroy(image) != NKUI_OK || nkui_resource_destroy(gradient) != NKUI_OK ||
        nkui_resource_destroy(paint) != NKUI_OK ||
        nkui_resource_destroy(path) != NKUI_OK ||
        nkui_resource_destroy(path) != NKUI_ERROR_INVALID_HANDLE)
        return 10;
    return 0;
}
