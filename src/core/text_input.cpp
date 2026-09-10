#include "nativekit_input.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#include <cstring>
#include <limits>

extern "C" nk_result NK_CALL nk_text_edit_event_text(const nk_event *event,
                                                       const char **out_text,
                                                       uint32_t *out_length) {
    nk::core::clear_error();
    if (!event || event->kind != NK_EVENT_TEXT_EDIT || !event->data ||
        event->data_size < sizeof(nk_text_edit_event) || !out_text || !out_length ||
        event->data_size > std::numeric_limits<std::size_t>::max()) {
        nk::core::set_error("invalid text-edit event arguments");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    nk_text_edit_event edit{};
    std::memcpy(&edit, event->data, sizeof(edit));
    if (edit.text_length == 0) {
        *out_text = nullptr;
        *out_length = 0;
        return edit.text_offset == 0 ? NK_OK : NK_ERROR_INVALID_ARGUMENT;
    }
    const auto size = static_cast<std::size_t>(event->data_size);
    if (edit.text_offset < sizeof(edit) || edit.text_offset > size ||
        edit.text_length > size - edit.text_offset) {
        nk::core::set_error("malformed text-edit event payload");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    *out_text = static_cast<const char *>(event->data) + edit.text_offset;
    *out_length = edit.text_length;
    return NK_OK;
}

#if !defined(NK_BACKEND_ANDROID)
namespace {
nk_result unsupported_text_input() {
    nk::core::clear_error();
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    nk::core::set_error("custom text input is not implemented by this backend");
    return NK_ERROR_UNSUPPORTED;
}
} // namespace

extern "C" {

nk_result NK_CALL nk_surface_set_text_input_state(nk_handle,
                                                   const nk_text_input_state *) {
    return unsupported_text_input();
}

nk_result NK_CALL nk_surface_set_text_input_active(nk_handle, uint32_t) {
    return unsupported_text_input();
}

}
#endif
