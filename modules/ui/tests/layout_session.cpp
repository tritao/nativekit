#include "nativekit_ui_layout.h"

#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

void write_u32(std::vector<uint8_t> &bytes, std::size_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void write_i32(std::vector<uint8_t> &bytes, std::size_t offset, int32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void write_float(std::vector<uint8_t> &bytes, std::size_t offset, float value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void write_color(std::vector<uint8_t> &bytes, std::size_t offset, float red, float green,
                 float blue, float alpha) {
    write_float(bytes, offset, red);
    write_float(bytes, offset + 4, green);
    write_float(bytes, offset + 8, blue);
    write_float(bytes, offset + 12, alpha);
}

std::vector<uint8_t> transaction() {
    constexpr uint32_t node_count = 3;
    constexpr std::size_t string_offset = NKUI_LAYOUT_TRANSACTION_HEADER_BYTES +
                                           NKUI_LAYOUT_NODE_RECORD_BYTES * node_count;
    const char text[] = "Press";
    std::vector<uint8_t> bytes(string_offset + sizeof(text) - 1);
    write_u32(bytes, 0, NKUI_LAYOUT_TRANSACTION_VERSION);
    write_u32(bytes, 4, node_count);
    write_u32(bytes, 8, NKUI_LAYOUT_NODE_RECORD_BYTES);
    write_u32(bytes, 12, string_offset);

    for (uint32_t index = 0; index < node_count; ++index) {
        const std::size_t offset = NKUI_LAYOUT_TRANSACTION_HEADER_BYTES +
                                   static_cast<std::size_t>(index) * NKUI_LAYOUT_NODE_RECORD_BYTES;
        write_float(bytes, offset + NKUI_LAYOUT_NODE_TRANSFORM_A_OFFSET, 1.0f);
        write_float(bytes, offset + NKUI_LAYOUT_NODE_TRANSFORM_D_OFFSET, 1.0f);
        write_u32(bytes, offset + NKUI_LAYOUT_NODE_FLAGS_OFFSET, NKUI_LAYOUT_NODE_VISIBLE);
    }

    const auto record = [&](uint32_t index) {
        return NKUI_LAYOUT_TRANSACTION_HEADER_BYTES +
               static_cast<std::size_t>(index) * NKUI_LAYOUT_NODE_RECORD_BYTES;
    };
    write_u32(bytes, record(0) + NKUI_LAYOUT_NODE_ID_OFFSET, 1);
    write_i32(bytes, record(0) + NKUI_LAYOUT_NODE_PARENT_OFFSET, -1);
    write_u32(bytes, record(0) + NKUI_LAYOUT_NODE_VISUAL_KIND_OFFSET, NKUI_LAYOUT_VISUAL_BOX);
    write_u32(bytes, record(0) + NKUI_LAYOUT_NODE_WIDTH_SIZING_OFFSET, NKUI_LAYOUT_SIZING_FIXED);
    write_float(bytes, record(0) + NKUI_LAYOUT_NODE_WIDTH_VALUE_OFFSET, 256.0f);
    write_u32(bytes, record(0) + NKUI_LAYOUT_NODE_HEIGHT_SIZING_OFFSET, NKUI_LAYOUT_SIZING_FIXED);
    write_float(bytes, record(0) + NKUI_LAYOUT_NODE_HEIGHT_VALUE_OFFSET, 192.0f);
    write_u32(bytes, record(0) + NKUI_LAYOUT_NODE_DIRECTION_OFFSET, NKUI_LAYOUT_DIRECTION_TOP_TO_BOTTOM);
    write_color(bytes, record(0) + NKUI_LAYOUT_NODE_BACKGROUND_OFFSET, 0.1f, 0.1f, 0.1f, 1.0f);
    write_float(bytes, record(0) + NKUI_LAYOUT_NODE_FONT_SIZE_OFFSET, 16.0f);
    write_u32(bytes, record(0) + NKUI_LAYOUT_NODE_TEXT_OFFSET_OFFSET, string_offset);

    write_u32(bytes, record(1) + NKUI_LAYOUT_NODE_ID_OFFSET, 2);
    write_i32(bytes, record(1) + NKUI_LAYOUT_NODE_PARENT_OFFSET, 0);
    write_u32(bytes, record(1) + NKUI_LAYOUT_NODE_VISUAL_KIND_OFFSET, NKUI_LAYOUT_VISUAL_BOX);
    write_u32(bytes, record(1) + NKUI_LAYOUT_NODE_WIDTH_SIZING_OFFSET, NKUI_LAYOUT_SIZING_FIXED);
    write_float(bytes, record(1) + NKUI_LAYOUT_NODE_WIDTH_VALUE_OFFSET, 160.0f);
    write_u32(bytes, record(1) + NKUI_LAYOUT_NODE_HEIGHT_SIZING_OFFSET, NKUI_LAYOUT_SIZING_FIXED);
    write_float(bytes, record(1) + NKUI_LAYOUT_NODE_HEIGHT_VALUE_OFFSET, 64.0f);
    write_u32(bytes, record(1) + NKUI_LAYOUT_NODE_DIRECTION_OFFSET, NKUI_LAYOUT_DIRECTION_TOP_TO_BOTTOM);
    write_float(bytes, record(1) + NKUI_LAYOUT_NODE_TRANSFORM_TX_OFFSET, 12.0f);
    write_float(bytes, record(1) + NKUI_LAYOUT_NODE_TRANSFORM_TY_OFFSET, 20.0f);
    write_color(bytes, record(1) + NKUI_LAYOUT_NODE_BACKGROUND_OFFSET, 0.2f, 0.5f, 0.9f, 1.0f);
    write_float(bytes, record(1) + NKUI_LAYOUT_NODE_FONT_SIZE_OFFSET, 16.0f);
    write_u32(bytes, record(1) + NKUI_LAYOUT_NODE_TEXT_OFFSET_OFFSET, string_offset);

    write_u32(bytes, record(2) + NKUI_LAYOUT_NODE_ID_OFFSET, 3);
    write_i32(bytes, record(2) + NKUI_LAYOUT_NODE_PARENT_OFFSET, 1);
    write_u32(bytes, record(2) + NKUI_LAYOUT_NODE_VISUAL_KIND_OFFSET, NKUI_LAYOUT_VISUAL_TEXT);
    write_u32(bytes, record(2) + NKUI_LAYOUT_NODE_WIDTH_SIZING_OFFSET, NKUI_LAYOUT_SIZING_FIT);
    write_u32(bytes, record(2) + NKUI_LAYOUT_NODE_HEIGHT_SIZING_OFFSET, NKUI_LAYOUT_SIZING_FIT);
    write_u32(bytes, record(2) + NKUI_LAYOUT_NODE_DIRECTION_OFFSET, NKUI_LAYOUT_DIRECTION_TOP_TO_BOTTOM);
    write_u32(bytes, record(2) + NKUI_LAYOUT_NODE_TEXT_OFFSET_OFFSET, string_offset);
    write_u32(bytes, record(2) + NKUI_LAYOUT_NODE_TEXT_LENGTH_OFFSET, sizeof(text) - 1);
    write_color(bytes, record(2) + NKUI_LAYOUT_NODE_TEXT_COLOR_OFFSET, 1.0f, 1.0f, 1.0f, 1.0f);
    write_float(bytes, record(2) + NKUI_LAYOUT_NODE_FONT_SIZE_OFFSET, 18.0f);
    std::memcpy(bytes.data() + string_offset, text, sizeof(text) - 1);
    return bytes;
}

} // namespace

int main() {
#ifndef NKUI_TEST_FONT_PATH
    std::cerr << "NKUI_TEST_FONT_PATH is required\n";
    return 2;
#else
    nkui_resource fonts{};
    nkui_layout_session session{};
    if (nkui_font_collection_create(&fonts) != NKUI_OK ||
        nkui_font_collection_add(fonts, NKUI_TEST_FONT_PATH, NKUI_FONT_FAMILY_DEFAULT) != NKUI_OK ||
        nkui_layout_session_create(&session) != NKUI_OK ||
        nkui_layout_session_set_font_collection(session, fonts) != NKUI_OK)
        return 3;

    const auto bytes = transaction();
    nkui_layout_frame_input frame{sizeof(frame), 256.0f, 192.0f, 1.0f / 60.0f};
    uint32_t resolved_bytes = 0;
    if (nkui_layout_session_get_resolved_items(session, nullptr, &resolved_bytes) !=
        NKUI_ERROR_INVALID_ARGUMENT)
        return 12;
    const nkui_result initial_status = nkui_layout_session_submit(
        session, bytes.data(), bytes.size(), &frame);
    if (initial_status != NKUI_OK) {
        std::cerr << "initial submit failed: " << initial_status << "\n";
        return 4;
    }
    if (nkui_layout_session_get_resolved_items(session, nullptr, &resolved_bytes) != NKUI_OK ||
        resolved_bytes != 3 * NKUI_LAYOUT_RESOLVED_ITEM_BYTES)
        return 5;
    std::vector<uint8_t> resolved(resolved_bytes);
    if (nkui_layout_session_get_resolved_items(session, resolved.data(), &resolved_bytes) !=
            NKUI_OK ||
        resolved_bytes != resolved.size())
        return 6;
    nkui_layout_item root_item{};
    nkui_layout_item button_item{};
    nkui_layout_item text_item{};
    std::memcpy(&root_item, resolved.data(), sizeof(root_item));
    std::memcpy(&button_item, resolved.data() + sizeof(root_item), sizeof(button_item));
    std::memcpy(&text_item, resolved.data() + 2 * sizeof(root_item), sizeof(text_item));
    if (root_item.struct_size != sizeof(root_item) || root_item.node_id != 1 ||
        root_item.width != 256.0f || root_item.height != 192.0f ||
        root_item.content_width != 160.0f || root_item.content_height != 64.0f ||
        button_item.node_id != 2 || button_item.width != 160.0f || button_item.height != 64.0f ||
        button_item.transform[4] != 12.0f || button_item.transform[5] != 20.0f ||
        button_item.clip_x != 0.0f || button_item.clip_y != 0.0f ||
        button_item.clip_width != 256.0f || button_item.clip_height != 192.0f ||
        !(button_item.flags & NKUI_LAYOUT_RESOLVED_VISIBLE) || text_item.node_id != 3 ||
        text_item.width <= 0.0f || text_item.height <= 0.0f ||
        !(text_item.flags & NKUI_LAYOUT_RESOLVED_HAS_BASELINE))
        return 10;
    uint32_t undersized_bytes = 1;
    if (nkui_layout_session_get_resolved_items(session, resolved.data(), &undersized_bytes) !=
            NKUI_ERROR_INVALID_ARGUMENT ||
        undersized_bytes != resolved.size())
        return 11;

    auto centered = bytes;
    const std::size_t panel_record = NKUI_LAYOUT_TRANSACTION_HEADER_BYTES +
                                     NKUI_LAYOUT_NODE_RECORD_BYTES;
    write_u32(centered, panel_record + NKUI_LAYOUT_NODE_CHILD_ALIGNMENT_OFFSET,
              NKUI_LAYOUT_ALIGNMENT_CENTER | (NKUI_LAYOUT_ALIGNMENT_CENTER << 8));
    if (nkui_layout_session_submit(session, centered.data(), centered.size(), &frame) != NKUI_OK ||
        nkui_layout_session_get_resolved_items(session, resolved.data(), &resolved_bytes) !=
            NKUI_OK)
        return 16;
    std::memcpy(&button_item, resolved.data() + sizeof(root_item), sizeof(button_item));
    std::memcpy(&text_item, resolved.data() + 2 * sizeof(root_item), sizeof(text_item));
    if (std::abs(text_item.x -
                 (button_item.x + (button_item.width - text_item.width) * 0.5f)) > 0.01f ||
        std::abs(text_item.y -
                 (button_item.y + (button_item.height - text_item.height) * 0.5f)) > 0.01f)
        return 17;

    auto floating = bytes;
    const std::size_t text_record = NKUI_LAYOUT_TRANSACTION_HEADER_BYTES +
                                    2 * NKUI_LAYOUT_NODE_RECORD_BYTES;
    write_u32(floating, text_record + NKUI_LAYOUT_NODE_FLAGS_OFFSET,
              NKUI_LAYOUT_NODE_VISIBLE | NKUI_LAYOUT_NODE_FLOATING |
                  NKUI_LAYOUT_NODE_CLIP_TO_PARENT);
    write_float(floating, text_record + NKUI_LAYOUT_NODE_POSITION_X_OFFSET, 20.0f);
    write_float(floating, text_record + NKUI_LAYOUT_NODE_POSITION_Y_OFFSET, 10.0f);
    write_i32(floating, text_record + NKUI_LAYOUT_NODE_Z_INDEX_OFFSET, 9);
    if (nkui_layout_session_submit(session, floating.data(), floating.size(), &frame) != NKUI_OK ||
        nkui_layout_session_get_resolved_items(session, resolved.data(), &resolved_bytes) !=
            NKUI_OK)
        return 18;
    std::memcpy(&button_item, resolved.data() + sizeof(root_item), sizeof(button_item));
    std::memcpy(&text_item, resolved.data() + 2 * sizeof(root_item), sizeof(text_item));
    if (std::abs(text_item.x - button_item.x - 20.0f) > 0.01f ||
        std::abs(text_item.y - button_item.y - 10.0f) > 0.01f ||
        !(text_item.flags & NKUI_LAYOUT_RESOLVED_VISIBLE))
        return 19;

    auto hidden = bytes;
    write_u32(hidden, panel_record + NKUI_LAYOUT_NODE_FLAGS_OFFSET, 0);
    if (nkui_layout_session_submit(session, hidden.data(), hidden.size(), &frame) != NKUI_OK ||
        nkui_layout_session_get_resolved_items(session, resolved.data(), &resolved_bytes) !=
            NKUI_OK)
        return 14;
    std::memcpy(&button_item, resolved.data() + sizeof(root_item), sizeof(button_item));
    std::memcpy(&text_item, resolved.data() + 2 * sizeof(root_item), sizeof(text_item));
    if ((button_item.flags & NKUI_LAYOUT_RESOLVED_VISIBLE) ||
        (text_item.flags & NKUI_LAYOUT_RESOLVED_VISIBLE))
        return 15;

    auto invalid = bytes;
    write_u32(invalid, 0, NKUI_LAYOUT_TRANSACTION_VERSION + 1);
    if (nkui_layout_session_submit(session, invalid.data(), invalid.size(), &frame) !=
        NKUI_ERROR_INVALID_TRANSACTION)
        return 8;
    invalid = bytes;
    write_u32(invalid, panel_record + NKUI_LAYOUT_NODE_CHILD_ALIGNMENT_OFFSET,
              NKUI_LAYOUT_ALIGNMENT_CENTER | (3u << 8));
    if (nkui_layout_session_submit(session, invalid.data(), invalid.size(), &frame) !=
        NKUI_ERROR_INVALID_TRANSACTION)
        return 13;
    if (nkui_layout_session_destroy(session) != NKUI_OK ||
        nkui_layout_session_get_resolved_items(session, nullptr, &resolved_bytes) !=
            NKUI_ERROR_INVALID_HANDLE ||
        nkui_resource_destroy(fonts) != NKUI_OK)
        return 9;

    std::cout << "PASS: Haxe layout transaction ABI\n";
    return 0;
#endif
}
