#include "nativekit_ui_layout.h"

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

    const auto record = [&](uint32_t index) {
        return NKUI_LAYOUT_TRANSACTION_HEADER_BYTES +
               static_cast<std::size_t>(index) * NKUI_LAYOUT_NODE_RECORD_BYTES;
    };
    write_u32(bytes, record(0) + 0, 1);
    write_i32(bytes, record(0) + 4, -1);
    write_u32(bytes, record(0) + 8, NKUI_LAYOUT_NODE_BOX);
    write_u32(bytes, record(0) + 12, NKUI_LAYOUT_SIZING_FIXED);
    write_float(bytes, record(0) + 16, 256.0f);
    write_u32(bytes, record(0) + 20, NKUI_LAYOUT_SIZING_FIXED);
    write_float(bytes, record(0) + 24, 192.0f);
    write_u32(bytes, record(0) + 28, NKUI_LAYOUT_DIRECTION_TOP_TO_BOTTOM);
    write_color(bytes, record(0) + 52, 0.1f, 0.1f, 0.1f, 1.0f);
    write_u32(bytes, record(0) + 88, string_offset);

    write_u32(bytes, record(1) + 0, 2);
    write_i32(bytes, record(1) + 4, 0);
    write_u32(bytes, record(1) + 8, NKUI_LAYOUT_NODE_BUTTON);
    write_u32(bytes, record(1) + 12, NKUI_LAYOUT_SIZING_FIXED);
    write_float(bytes, record(1) + 16, 160.0f);
    write_u32(bytes, record(1) + 20, NKUI_LAYOUT_SIZING_FIXED);
    write_float(bytes, record(1) + 24, 64.0f);
    write_u32(bytes, record(1) + 28, NKUI_LAYOUT_DIRECTION_TOP_TO_BOTTOM);
    write_color(bytes, record(1) + 52, 0.2f, 0.5f, 0.9f, 1.0f);
    write_u32(bytes, record(1) + 88, string_offset);

    write_u32(bytes, record(2) + 0, 3);
    write_i32(bytes, record(2) + 4, 1);
    write_u32(bytes, record(2) + 8, NKUI_LAYOUT_NODE_TEXT);
    write_u32(bytes, record(2) + 12, NKUI_LAYOUT_SIZING_FIT);
    write_u32(bytes, record(2) + 20, NKUI_LAYOUT_SIZING_FIT);
    write_u32(bytes, record(2) + 28, NKUI_LAYOUT_DIRECTION_TOP_TO_BOTTOM);
    write_u32(bytes, record(2) + 88, string_offset);
    write_u32(bytes, record(2) + 92, sizeof(text) - 1);
    write_color(bytes, record(2) + 96, 1.0f, 1.0f, 1.0f, 1.0f);
    write_u32(bytes, record(2) + 116, 18);
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
    const nkui_result initial_status = nkui_layout_session_submit(
        session, bytes.data(), bytes.size(), 256.0f, 192.0f, 0.0f, 0.0f, 0, 1.0f / 60.0f);
    if (initial_status != NKUI_OK) {
        std::cerr << "initial submit failed: " << initial_status << "\n";
        return 4;
    }
    uint32_t event_count = 0;
    if (nkui_layout_session_get_event_count(session, &event_count) != NKUI_OK || event_count != 0)
        return 5;

    if (nkui_layout_session_submit(session, bytes.data(), bytes.size(), 256.0f, 192.0f, 8.0f,
                                    8.0f, 1, 1.0f / 60.0f) != NKUI_OK ||
        nkui_layout_session_submit(session, bytes.data(), bytes.size(), 256.0f, 192.0f, 8.0f,
                                    8.0f, 0, 1.0f / 60.0f) != NKUI_OK ||
        nkui_layout_session_get_event_count(session, &event_count) != NKUI_OK || event_count != 1)
        return 6;

    nkui_layout_event event{};
    if (nkui_layout_session_get_event(session, 0, &event) != NKUI_OK ||
        event.kind != NKUI_LAYOUT_EVENT_BUTTON_ACTIVATED || event.node_id != 2)
        return 7;

    auto invalid = bytes;
    write_u32(invalid, 0, NKUI_LAYOUT_TRANSACTION_VERSION + 1);
    if (nkui_layout_session_submit(session, invalid.data(), invalid.size(), 256.0f, 192.0f, 0.0f,
                                    0.0f, 0, 0.0f) != NKUI_ERROR_INVALID_TRANSACTION)
        return 8;
    if (nkui_layout_session_destroy(session) != NKUI_OK ||
        nkui_layout_session_get_event_count(session, &event_count) != NKUI_ERROR_INVALID_HANDLE ||
        nkui_resource_destroy(fonts) != NKUI_OK)
        return 9;

    std::cout << "PASS: Haxe layout transaction ABI\n";
    return 0;
#endif
}
