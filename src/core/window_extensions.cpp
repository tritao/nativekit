#include "nativekit_window.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#if !defined(NK_BACKEND_GTK) && !defined(NK_BACKEND_WEB)
namespace {
nk_result unsupported_window_extension() {
    nk::core::clear_error();
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    nk::core::set_error("extended window operation is not implemented by this backend");
    return NK_ERROR_UNSUPPORTED;
}
} // namespace

extern "C" {
nk_result NK_CALL nk_window_get_content_scale(nk_handle, nk_window_content_scale *) {
    return unsupported_window_extension();
}
nk_result NK_CALL nk_window_get_position(nk_handle, int32_t *, int32_t *) {
    return unsupported_window_extension();
}
nk_result NK_CALL nk_window_get_size(nk_handle, int32_t *, int32_t *) {
    return unsupported_window_extension();
}
nk_result NK_CALL nk_window_get_framebuffer_size(nk_handle, int32_t *, int32_t *) {
    return unsupported_window_extension();
}
nk_result NK_CALL nk_window_get_frame_extents(nk_handle, nk_window_frame_extents *) {
    return unsupported_window_extension();
}
nk_result NK_CALL nk_window_set_aspect_ratio(nk_handle, int32_t, int32_t) {
    return unsupported_window_extension();
}
nk_result NK_CALL nk_window_set_resizable(nk_handle, uint32_t) {
    return unsupported_window_extension();
}
nk_result NK_CALL nk_window_set_decorated(nk_handle, uint32_t) {
    return unsupported_window_extension();
}
nk_result NK_CALL nk_window_set_floating(nk_handle, uint32_t) {
    return unsupported_window_extension();
}
nk_result NK_CALL nk_window_set_opacity(nk_handle, float) {
    return unsupported_window_extension();
}
nk_result NK_CALL nk_window_set_mouse_passthrough(nk_handle, uint32_t) {
    return unsupported_window_extension();
}
nk_result NK_CALL nk_window_get_hovered(nk_handle, uint32_t *) {
    return unsupported_window_extension();
}
}
#endif
