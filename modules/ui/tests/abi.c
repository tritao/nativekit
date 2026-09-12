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
    nkui_resource image = {0};
    const uint8_t pixels[] = {255, 0, 0, 255};
    if (nkui_path_create(path_elements, 4, &path) != NKUI_OK ||
        nkui_paint_create_solid((nkui_color){1.0f, 0.0f, 0.0f, 1.0f}, &paint) != NKUI_OK ||
        nkui_image_create(1, 1, NKUI_IMAGE_RGBA8, pixels, sizeof(pixels), &image) != NKUI_OK)
        return 2;
    nkui_resource_command draw = {{NKUI_COMMAND_DRAW_PATH, NKUI_COMMAND_VERSION, sizeof(draw)},
                                  path};
    nkui_stroke_path_command stroke = {
        {NKUI_COMMAND_STROKE_PATH, NKUI_COMMAND_VERSION, sizeof(stroke)}, path, 4.0f,
        NKUI_PATH_LINE_CAP_ROUND, NKUI_PATH_LINE_JOIN_MITER, 10.0f};
    nkui_layer_command begin = {
        {NKUI_COMMAND_BEGIN_LAYER, NKUI_COMMAND_VERSION, sizeof(begin)}, 0.5f, 1};
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
    nkui_text_metrics metrics = {0};
    nkui_text_position position = {0};
    nkui_text_caret caret = {0};
    if (nkui_text_layout_measure(layout, &metrics) != NKUI_OK || metrics.width <= 0.0f ||
        nkui_text_layout_hit_test(layout, 0.0f, 0.0f, &position) != NKUI_OK ||
        nkui_text_layout_caret(layout, position, &caret) != NKUI_OK)
        return 8;
    text_style.letter_spacing = 0.0f;
    paragraph_style.alignment = NKUI_TEXT_ALIGN_START;
    if (nkui_text_layout_update(layout, "NativeKit updated مرحبا", 280.0f, &text_style,
                                 &paragraph_style) != NKUI_OK)
        return 8;
    if (nkui_resource_destroy(layout) != NKUI_OK ||
        nkui_text_layout_measure(layout, &metrics) != NKUI_ERROR_INVALID_HANDLE ||
        nkui_resource_destroy(fonts) != NKUI_OK)
        return 9;
    if (nkui_resource_destroy(image) != NKUI_OK || nkui_resource_destroy(paint) != NKUI_OK ||
        nkui_resource_destroy(path) != NKUI_OK ||
        nkui_resource_destroy(path) != NKUI_ERROR_INVALID_HANDLE)
        return 10;
    return 0;
}
