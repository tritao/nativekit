#include "nativekit_tray.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

// The Windows backend implements trays in nativekit_win.cpp. Every other
// backend reports them as unsupported so applications can degrade cleanly.
#if !defined(NK_BACKEND_WINDOWS)
namespace {

nk_result unsupported() {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    nk::core::set_error("tray icons are not implemented by this backend");
    return NK_ERROR_UNSUPPORTED;
}

} // namespace

extern "C" {

nk_result NK_CALL nk_tray_create(const nk_tray_options *, nk_tray *out_tray) {
    if (out_tray)
        *out_tray = NK_INVALID_HANDLE;
    return unsupported();
}

nk_result NK_CALL nk_tray_destroy(nk_tray) {
    return unsupported();
}

nk_result NK_CALL nk_tray_set_tooltip(nk_tray, const char *) {
    return unsupported();
}

nk_result NK_CALL nk_tray_show_menu(nk_tray, nk_menu) {
    return unsupported();
}
}
#endif
