#include "nativekit_clipboard.h"

#include "core/error.hpp"

#include <cstddef>
#include <cstring>
#include <limits>

namespace {
nk_result string_at(const nk_event* event, uint32_t count, uint32_t strings_offset,
                    uint32_t index, const char** out, uint32_t* out_length) {
    if (!event->data || !out || !out_length || index >= count ||
        event->data_size > std::numeric_limits<std::size_t>::max() ||
        strings_offset >= event->data_size) {
        nk::core::set_error("invalid packed string-list arguments");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto* bytes = static_cast<const char*>(event->data);
    std::size_t cursor = strings_offset;
    const auto size = static_cast<std::size_t>(event->data_size);
    for (uint32_t current = 0; current <= index; ++current) {
        const auto remaining = size - cursor;
        const auto* end = static_cast<const char*>(std::memchr(bytes + cursor, '\0', remaining));
        if (!end) {
            nk::core::set_error("malformed packed string list");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        if (current == index) {
            const auto length = static_cast<std::size_t>(end - (bytes + cursor));
            if (length > std::numeric_limits<uint32_t>::max()) return NK_ERROR_INVALID_ARGUMENT;
            *out = bytes + cursor;
            *out_length = static_cast<uint32_t>(length);
            return NK_OK;
        }
        cursor = static_cast<std::size_t>(end - bytes) + 1;
        if (cursor >= size) break;
    }
    nk::core::set_error("malformed packed string list");
    return NK_ERROR_INVALID_ARGUMENT;
}
}

extern "C" {
nk_result NK_CALL nk_clipboard_event_file(const nk_event* event, uint32_t index,
                                          const char** out_path, uint32_t* out_length) {
    nk::core::clear_error();
    if (!event || event->kind != NK_EVENT_CLIPBOARD_FILES_COMPLETE ||
        event->data_size < sizeof(nk_clipboard_files)) return NK_ERROR_INVALID_ARGUMENT;
    nk_clipboard_files header{};
    std::memcpy(&header, event->data, sizeof(header));
    return string_at(event, header.path_count, header.strings_offset,
                     index, out_path, out_length);
}

nk_result NK_CALL nk_drop_event_item(const nk_event* event, uint32_t index,
                                     const char** out_item, uint32_t* out_length) {
    nk::core::clear_error();
    if (!event || (event->kind != NK_EVENT_DROP_FILES && event->kind != NK_EVENT_DROP_TEXT) ||
        event->data_size < sizeof(nk_drop_data)) return NK_ERROR_INVALID_ARGUMENT;
    nk_drop_data header{};
    std::memcpy(&header, event->data, sizeof(header));
    return string_at(event, header.item_count, header.strings_offset,
                     index, out_item, out_length);
}
}
