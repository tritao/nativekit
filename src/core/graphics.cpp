#include "nativekit_graphics.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#if !defined(NK_BACKEND_GTK) && !defined(NK_BACKEND_ANDROID) && !defined(NK_BACKEND_WEB)
namespace {
nk_result unsupported_graphics() {
    nk::core::clear_error();
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    nk::core::set_error("graphics surfaces are not implemented by this backend");
    return NK_ERROR_UNSUPPORTED;
}
} // namespace

extern "C" {
nk_result NK_CALL nk_surface_create(nk_handle, const nk_surface_options *, nk_handle *) {
    return unsupported_graphics();
}
nk_result NK_CALL nk_surface_destroy(nk_handle) {
    return unsupported_graphics();
}
nk_result NK_CALL nk_surface_show(nk_handle, uint32_t) {
    return unsupported_graphics();
}
nk_result NK_CALL nk_surface_set_bounds(nk_handle, int32_t, int32_t, int32_t, int32_t) {
    return unsupported_graphics();
}
nk_result NK_CALL nk_surface_make_current(nk_handle) {
    return unsupported_graphics();
}
nk_result NK_CALL nk_surface_present(nk_handle) {
    return unsupported_graphics();
}
nk_result NK_CALL nk_surface_set_frame_callback(nk_handle, nk_surface_frame_callback, void *) {
    return unsupported_graphics();
}
nk_result NK_CALL nk_surface_get_framebuffer_size(nk_handle, int32_t *, int32_t *) {
    return unsupported_graphics();
}
nk_result NK_CALL nk_surface_get_frame_target(nk_handle, nk_surface_frame_target *) {
    return unsupported_graphics();
}
nk_result NK_CALL nk_surface_get_proc_address(nk_handle, const char *, nk_graphics_proc *) {
    return unsupported_graphics();
}
}
#endif
