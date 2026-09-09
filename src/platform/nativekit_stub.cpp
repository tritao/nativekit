#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_notification.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

namespace {
nk_result unsupported() {
    const auto thread = nk::core::require_ui_thread();
    if (thread != NK_OK)
        return thread;
    nk::core::set_error("this NativeKit build has no desktop backend");
    return NK_ERROR_UNSUPPORTED;
}
} // namespace

namespace nk::backend {
#if !defined(NK_STUB_ANDROID)
void pump_events() noexcept {}
void shutdown() noexcept {}
#endif
} // namespace nk::backend

extern "C" {
#if !defined(NK_STUB_ANDROID)
nk_capabilities NK_CALL nk_get_capabilities(void) {
    return 0;
}
#endif
nk_result NK_CALL nk_window_create(const nk_window_options *, nk_handle *) {
    return unsupported();
}
nk_result NK_CALL nk_window_destroy(nk_handle) {
    return unsupported();
}
nk_result NK_CALL nk_window_show(nk_handle, uint32_t) {
    return unsupported();
}
nk_result NK_CALL nk_window_set_title(nk_handle, const char *) {
    return unsupported();
}
nk_result NK_CALL nk_window_set_bounds(nk_handle, int32_t, int32_t, int32_t, int32_t) {
    return unsupported();
}
nk_result NK_CALL nk_window_get_scale(nk_handle, float *) {
    return unsupported();
}
nk_result NK_CALL nk_window_get_state(nk_handle, nk_window_state *) {
    return unsupported();
}
nk_result NK_CALL nk_window_minimize(nk_handle) {
    return unsupported();
}
nk_result NK_CALL nk_window_maximize(nk_handle) {
    return unsupported();
}
nk_result NK_CALL nk_window_restore(nk_handle) {
    return unsupported();
}
nk_result NK_CALL nk_window_activate(nk_handle) {
    return unsupported();
}
nk_result NK_CALL nk_window_set_fullscreen(nk_handle, uint32_t) {
    return unsupported();
}
nk_result NK_CALL nk_window_request_attention(nk_handle) {
    return unsupported();
}
nk_result NK_CALL nk_window_set_size_limits(nk_handle, const nk_window_size_limits *) {
    return unsupported();
}
nk_result NK_CALL nk_window_get_native(nk_handle, nk_native_window *) {
    return unsupported();
}
nk_result NK_CALL nk_window_wrap_native(const nk_native_window *, nk_handle *) {
    return unsupported();
}
#if !defined(NK_STUB_ANDROID)
nk_result NK_CALL nk_webview_create(nk_handle, const nk_webview_options *, nk_handle *) {
    return unsupported();
}
nk_result NK_CALL nk_webview_destroy(nk_handle) {
    return unsupported();
}
nk_result NK_CALL nk_webview_show(nk_handle, uint32_t) {
    return unsupported();
}
nk_result NK_CALL nk_webview_set_bounds(nk_handle, int32_t, int32_t, int32_t, int32_t) {
    return unsupported();
}
nk_result NK_CALL nk_webview_navigate(nk_handle, const char *) {
    return unsupported();
}
nk_result NK_CALL nk_webview_set_html(nk_handle, const char *, const char *) {
    return unsupported();
}
nk_result NK_CALL nk_webview_eval(nk_handle, const char *, nk_request_id *) {
    return unsupported();
}
nk_result NK_CALL nk_webview_navigation_decide(nk_request_id, uint32_t) {
    return unsupported();
}
#endif
nk_result NK_CALL nk_dialog_open_file(nk_handle, const nk_file_dialog_options *, nk_request_id *) {
    return unsupported();
}
nk_result NK_CALL nk_dialog_save_file(nk_handle, const nk_file_dialog_options *, nk_request_id *) {
    return unsupported();
}
nk_result NK_CALL nk_dialog_select_directory(nk_handle, const nk_file_dialog_options *,
                                             nk_request_id *) {
    return unsupported();
}
nk_result NK_CALL nk_dialog_message(nk_handle, const nk_message_dialog_options *, nk_request_id *) {
    return unsupported();
}
nk_result NK_CALL nk_dialog_cancel(nk_request_id) {
    return unsupported();
}
nk_result NK_CALL nk_shell_open_url(const char *) {
    return unsupported();
}
nk_result NK_CALL nk_shell_open_file(const char *) {
    return unsupported();
}
nk_result NK_CALL nk_shell_reveal_file(const char *) {
    return unsupported();
}
nk_result NK_CALL nk_system_directory(nk_system_directory_kind, char *, uint32_t *) {
    return unsupported();
}
nk_result NK_CALL nk_system_locale(char *, uint32_t *) {
    return unsupported();
}
nk_result NK_CALL nk_system_get_appearance(nk_system_appearance *) {
    return unsupported();
}
nk_result NK_CALL nk_clipboard_set_text(const char *) {
    return unsupported();
}
nk_result NK_CALL nk_clipboard_set_files(const char *const *, uint32_t) {
    return unsupported();
}
nk_result NK_CALL nk_clipboard_read_text(nk_request_id *) {
    return unsupported();
}
nk_result NK_CALL nk_clipboard_read_files(nk_request_id *) {
    return unsupported();
}
nk_result NK_CALL nk_window_set_drop_enabled(nk_handle, uint32_t) {
    return unsupported();
}
nk_result NK_CALL nk_notification_show(const nk_notification_options *, nk_request_id *) {
    return unsupported();
}
nk_result NK_CALL nk_notification_close(nk_request_id) {
    return unsupported();
}
}
