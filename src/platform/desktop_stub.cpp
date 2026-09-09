#include "nativekit_window.h"
#include "nativekit_webview.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

namespace {
nk_result unsupported() {
    const auto thread = nk::core::require_ui_thread();
    if (thread != NK_OK) return thread;
    nk::core::set_error("this NativeKit build has no desktop backend");
    return NK_ERROR_UNSUPPORTED;
}
}

namespace nk::backend {
void pump_events() noexcept {}
void shutdown() noexcept {}
}

extern "C" {
nk_capabilities NK_CALL nk_get_capabilities(void) { return 0; }
nk_result NK_CALL nk_window_create(const nk_window_options*, nk_handle*) { return unsupported(); }
nk_result NK_CALL nk_window_destroy(nk_handle) { return unsupported(); }
nk_result NK_CALL nk_window_show(nk_handle, uint32_t) { return unsupported(); }
nk_result NK_CALL nk_window_set_title(nk_handle, const char*) { return unsupported(); }
nk_result NK_CALL nk_window_set_bounds(nk_handle, int32_t, int32_t, int32_t, int32_t) { return unsupported(); }
nk_result NK_CALL nk_window_get_scale(nk_handle, float*) { return unsupported(); }
nk_result NK_CALL nk_webview_create(nk_handle, const nk_webview_options*, nk_handle*) { return unsupported(); }
nk_result NK_CALL nk_webview_destroy(nk_handle) { return unsupported(); }
nk_result NK_CALL nk_webview_show(nk_handle, uint32_t) { return unsupported(); }
nk_result NK_CALL nk_webview_set_bounds(nk_handle, int32_t, int32_t, int32_t, int32_t) { return unsupported(); }
nk_result NK_CALL nk_webview_navigate(nk_handle, const char*) { return unsupported(); }
nk_result NK_CALL nk_webview_set_html(nk_handle, const char*, const char*) { return unsupported(); }
nk_result NK_CALL nk_webview_eval(nk_handle, const char*, nk_request_id*) { return unsupported(); }
}
