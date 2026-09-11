#include "nativekit_input.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#if !defined(NK_BACKEND_GTK) && !defined(NK_BACKEND_ANDROID) && !defined(NK_BACKEND_WEB)
namespace {
nk_result unsupported_input() {
    nk::core::clear_error();
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    nk::core::set_error("input state queries are not implemented by this backend");
    return NK_ERROR_UNSUPPORTED;
}
} // namespace

extern "C" {

nk_result NK_CALL nk_key_get_state(nk_handle, nk_key, nk_input_action *) {
    return unsupported_input();
}

nk_result NK_CALL nk_pointer_button_get_state(nk_handle, nk_pointer_button, nk_input_action *) {
    return unsupported_input();
}

nk_result NK_CALL nk_pointer_get_position(nk_handle, double *, double *) {
    return unsupported_input();
}
nk_result NK_CALL nk_cursor_create_standard(nk_cursor_shape, nk_handle *) {
    return unsupported_input();
}
nk_result NK_CALL nk_cursor_create_custom(const nk_cursor_image *, nk_handle *) {
    return unsupported_input();
}
nk_result NK_CALL nk_cursor_destroy(nk_handle) { return unsupported_input(); }
nk_result NK_CALL nk_window_set_cursor(nk_handle, nk_handle) { return unsupported_input(); }
nk_result NK_CALL nk_window_set_cursor_mode(nk_handle, nk_cursor_mode) {
    return unsupported_input();
}
nk_result NK_CALL nk_window_get_cursor_mode(nk_handle, nk_cursor_mode *) {
    return unsupported_input();
}
uint32_t NK_CALL nk_raw_pointer_motion_supported(void) { return 0; }

}
#endif
