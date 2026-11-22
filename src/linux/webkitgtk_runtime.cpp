#include "linux/webkitgtk_runtime.hpp"

#include <dlfcn.h>

#include <cstring>

namespace nk::webkitgtk {
namespace {

template <typename Function> bool resolve(void *library, const char *name, Function &function) {
    void *address = dlsym(library, name);
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return address != nullptr;
}

#define NK_WEBKIT_RESOLVE(name) resolve(library, #name, api.name)

bool load(Api &api) noexcept {
    /* WebKitGTK 4.1 links the same GTK 3 as NativeKit; the 6.0 API is GTK 4 and cannot be used. */
    void *library = dlopen("libwebkit2gtk-4.1.so.0", RTLD_NOW | RTLD_LOCAL);
    if (!library)
        return false;
    /* JavaScriptCoreGTK is a dependency of WebKitGTK, so the handle resolves its symbols too. */
    const bool resolved =
        NK_WEBKIT_RESOLVE(webkit_web_view_get_type) &&
        NK_WEBKIT_RESOLVE(webkit_policy_decision_get_type) &&
        NK_WEBKIT_RESOLVE(webkit_navigation_policy_decision_get_type) &&
        NK_WEBKIT_RESOLVE(webkit_network_error_quark) &&
        NK_WEBKIT_RESOLVE(webkit_policy_error_quark) &&
        NK_WEBKIT_RESOLVE(webkit_user_content_manager_new) &&
        NK_WEBKIT_RESOLVE(webkit_user_content_manager_register_script_message_handler) &&
        NK_WEBKIT_RESOLVE(webkit_web_view_new_with_user_content_manager) &&
        NK_WEBKIT_RESOLVE(webkit_web_view_get_settings) &&
        NK_WEBKIT_RESOLVE(webkit_settings_set_enable_developer_extras) &&
        NK_WEBKIT_RESOLVE(webkit_web_view_load_uri) &&
        NK_WEBKIT_RESOLVE(webkit_web_view_load_html) &&
        NK_WEBKIT_RESOLVE(webkit_web_view_get_uri) &&
        NK_WEBKIT_RESOLVE(webkit_web_view_get_title) &&
        NK_WEBKIT_RESOLVE(webkit_web_view_can_go_back) &&
        NK_WEBKIT_RESOLVE(webkit_web_view_can_go_forward) &&
        NK_WEBKIT_RESOLVE(webkit_web_view_go_back) &&
        NK_WEBKIT_RESOLVE(webkit_web_view_go_forward) &&
        NK_WEBKIT_RESOLVE(webkit_web_view_reload) &&
        NK_WEBKIT_RESOLVE(webkit_web_view_stop_loading) &&
        NK_WEBKIT_RESOLVE(webkit_web_view_evaluate_javascript) &&
        NK_WEBKIT_RESOLVE(webkit_web_view_evaluate_javascript_finish) &&
        NK_WEBKIT_RESOLVE(webkit_javascript_result_get_js_value) &&
        NK_WEBKIT_RESOLVE(webkit_navigation_policy_decision_get_navigation_action) &&
        NK_WEBKIT_RESOLVE(webkit_navigation_action_get_request) &&
        NK_WEBKIT_RESOLVE(webkit_uri_request_get_uri) &&
        NK_WEBKIT_RESOLVE(webkit_policy_decision_use) &&
        NK_WEBKIT_RESOLVE(webkit_policy_decision_ignore) &&
        NK_WEBKIT_RESOLVE(jsc_value_to_string) && NK_WEBKIT_RESOLVE(jsc_value_to_json);
    /* The library stays loaded even when an entry point is missing: WebKit registers GObject
       types and process-wide state that cannot be unloaded. */
    return resolved;
}

#undef NK_WEBKIT_RESOLVE

} // namespace

const Api *api() noexcept {
    static Api loaded{};
    static const bool available = load(loaded);
    return available ? &loaded : nullptr;
}

} // namespace nk::webkitgtk
