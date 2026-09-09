#include "nativekit_dialog.h"

#include "core/error.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

extern "C" nk_result NK_CALL nk_dialog_event_path(const nk_event *event, uint32_t index,
                                                  const char **out_path, uint32_t *out_length) {
    nk::core::clear_error();
    if (!event || event->kind != NK_EVENT_DIALOG_COMPLETE || !event->data || !out_path ||
        !out_length || event->data_size < sizeof(nk_dialog_paths)) {
        nk::core::set_error("invalid dialog event path arguments");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    if (event->data_size > std::numeric_limits<std::size_t>::max()) {
        nk::core::set_error("dialog path payload is too large");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    nk_dialog_paths payload_storage{};
    std::memcpy(&payload_storage, event->data, sizeof(payload_storage));
    const auto *payload = &payload_storage;
    const auto size = static_cast<std::size_t>(event->data_size);
    const auto offset_bytes = static_cast<std::size_t>(payload->path_count) * sizeof(uint32_t);
    if (index >= payload->path_count || payload->offsets_offset > size ||
        offset_bytes > size - payload->offsets_offset || payload->strings_offset > size ||
        payload->offsets_offset + offset_bytes > payload->strings_offset) {
        nk::core::set_error("malformed dialog path payload");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto *bytes = static_cast<const unsigned char *>(event->data);
    uint32_t offset = 0;
    std::memcpy(&offset, bytes + payload->offsets_offset + index * sizeof(uint32_t),
                sizeof(offset));
    if (offset < payload->strings_offset || offset >= size) {
        nk::core::set_error("malformed dialog path offset");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto *path = reinterpret_cast<const char *>(bytes + offset);
    const auto remaining = size - offset;
    const auto *end = static_cast<const char *>(std::memchr(path, '\0', remaining));
    if (!end) {
        nk::core::set_error("unterminated dialog path");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    *out_path = path;
    *out_length = static_cast<uint32_t>(end - path);
    return NK_OK;
}
