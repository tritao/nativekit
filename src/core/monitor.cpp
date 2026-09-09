#include "nativekit_monitor.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#if !defined(NK_BACKEND_GTK)
namespace {
nk_result unsupported_monitor() {
    nk::core::clear_error();
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    nk::core::set_error("monitor operations are not implemented by this backend");
    return NK_ERROR_UNSUPPORTED;
}
} // namespace

extern "C" {
nk_result NK_CALL nk_monitor_list(nk_handle *, uint32_t *) { return unsupported_monitor(); }
nk_result NK_CALL nk_monitor_get_primary(nk_handle *) { return unsupported_monitor(); }
nk_result NK_CALL nk_monitor_get_name(nk_handle, char *, uint32_t *) {
    return unsupported_monitor();
}
nk_result NK_CALL nk_monitor_get_geometry(nk_handle, nk_monitor_geometry *) {
    return unsupported_monitor();
}
nk_result NK_CALL nk_monitor_get_current_mode(nk_handle, nk_video_mode *) {
    return unsupported_monitor();
}
nk_result NK_CALL nk_monitor_get_modes(nk_handle, nk_video_mode *, uint32_t *) {
    return unsupported_monitor();
}
nk_result NK_CALL nk_window_set_fullscreen_monitor(nk_handle, nk_handle) {
    return unsupported_monitor();
}
}
#endif
