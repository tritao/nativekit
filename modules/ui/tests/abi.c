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
    nkui_resource path = {(UINT32_C(1) << 28) | (UINT32_C(1) << 16) | UINT32_C(1)};
    nkui_resource_command draw = {{NKUI_COMMAND_DRAW_PATH, NKUI_COMMAND_VERSION, sizeof(draw)},
                                  path};
    nkui_layer_command begin = {
        {NKUI_COMMAND_BEGIN_LAYER, NKUI_COMMAND_VERSION, sizeof(begin)}, 0.5f, 1};
    nkui_command_header end = {NKUI_COMMAND_END_LAYER, NKUI_COMMAND_VERSION, sizeof(end)};
    uint8_t commands[sizeof(draw) + sizeof(begin) + sizeof(end)];
    memcpy(commands, &draw, sizeof(draw));
    memcpy(commands + sizeof(draw), &begin, sizeof(begin));
    memcpy(commands + sizeof(draw) + sizeof(begin), &end, sizeof(end));
    if (nkui_display_list_submit(list, commands, sizeof(commands)) != NKUI_OK)
        return 3;
    nkui_transaction_info info = {0};
    if (nkui_display_list_get_info(list, &info) != NKUI_OK || info.command_count != 3 ||
        info.command_bytes != sizeof(commands))
        return 4;
    commands[2] = 2;
    if (nkui_display_list_submit(list, commands, sizeof(commands)) !=
            NKUI_ERROR_INVALID_TRANSACTION ||
        nkui_display_list_get_info(list, &info) != NKUI_OK || info.command_count != 3)
        return 5;
    if (nkui_display_list_destroy(list) != NKUI_OK ||
        nkui_display_list_reset(list) != NKUI_ERROR_INVALID_HANDLE)
        return 6;
    nkui_resource fonts = {0};
    nkui_resource layout = {0};
    if (nkui_font_collection_create(&fonts) != NKUI_OK ||
        nkui_font_collection_add(fonts, NKUI_TEST_FONT_PATH, NKUI_FONT_FAMILY_DEFAULT) != NKUI_OK ||
        nkui_text_layout_create(fonts, "NativeKit مرحبا", 300.0f, 24.0f, &layout) != NKUI_OK)
        return 7;
    nkui_text_metrics metrics = {0};
    nkui_text_position position = {0};
    nkui_text_caret caret = {0};
    if (nkui_text_layout_measure(layout, &metrics) != NKUI_OK || metrics.width <= 0.0f ||
        nkui_text_layout_hit_test(layout, 0.0f, 0.0f, &position) != NKUI_OK ||
        nkui_text_layout_caret(layout, position, &caret) != NKUI_OK)
        return 8;
    if (nkui_resource_destroy(layout) != NKUI_OK ||
        nkui_text_layout_measure(layout, &metrics) != NKUI_ERROR_INVALID_HANDLE ||
        nkui_resource_destroy(fonts) != NKUI_OK)
        return 9;
    return 0;
}
