#include "linux/webkitgtk_runtime.hpp"

#include <webkit2/webkit2.h>

#include <type_traits>

/* NativeKit loads WebKitGTK at run time from its own declarations; they must match its ABI. */

namespace webkit = nk::webkitgtk;

static_assert(webkit::load_finished == WEBKIT_LOAD_FINISHED);
static_assert(webkit::policy_decision_type_navigation_action ==
              WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION);
static_assert(webkit::network_error_transport == WEBKIT_NETWORK_ERROR_TRANSPORT);
static_assert(webkit::network_error_unknown_protocol == WEBKIT_NETWORK_ERROR_UNKNOWN_PROTOCOL);
static_assert(webkit::network_error_cancelled == WEBKIT_NETWORK_ERROR_CANCELLED);
static_assert(webkit::network_error_file_does_not_exist ==
              WEBKIT_NETWORK_ERROR_FILE_DOES_NOT_EXIST);
static_assert(webkit::policy_error_cannot_use_restricted_port ==
              WEBKIT_POLICY_ERROR_CANNOT_USE_RESTRICTED_PORT);
static_assert(sizeof(WebKitLoadEvent) == sizeof(int));
static_assert(sizeof(WebKitPolicyDecisionType) == sizeof(int));
static_assert(sizeof(WebKitWebProcessTerminationReason) == sizeof(int));

#define NK_CHECK_ENTRY(name)                                                                       \
    static_assert(std::is_same_v<decltype(webkit::Api::name), decltype(&::name)>, #name);
NK_CHECK_ENTRY(webkit_web_view_get_type)
NK_CHECK_ENTRY(webkit_policy_decision_get_type)
NK_CHECK_ENTRY(webkit_navigation_policy_decision_get_type)
NK_CHECK_ENTRY(webkit_network_error_quark)
NK_CHECK_ENTRY(webkit_policy_error_quark)
NK_CHECK_ENTRY(webkit_user_content_manager_new)
NK_CHECK_ENTRY(webkit_user_content_manager_register_script_message_handler)
NK_CHECK_ENTRY(webkit_web_view_new_with_user_content_manager)
NK_CHECK_ENTRY(webkit_web_view_get_settings)
NK_CHECK_ENTRY(webkit_settings_set_enable_developer_extras)
NK_CHECK_ENTRY(webkit_web_view_load_uri)
NK_CHECK_ENTRY(webkit_web_view_load_html)
NK_CHECK_ENTRY(webkit_web_view_get_uri)
NK_CHECK_ENTRY(webkit_web_view_get_title)
NK_CHECK_ENTRY(webkit_web_view_can_go_back)
NK_CHECK_ENTRY(webkit_web_view_can_go_forward)
NK_CHECK_ENTRY(webkit_web_view_go_back)
NK_CHECK_ENTRY(webkit_web_view_go_forward)
NK_CHECK_ENTRY(webkit_web_view_reload)
NK_CHECK_ENTRY(webkit_web_view_stop_loading)
NK_CHECK_ENTRY(webkit_web_view_evaluate_javascript)
NK_CHECK_ENTRY(webkit_web_view_evaluate_javascript_finish)
NK_CHECK_ENTRY(webkit_javascript_result_get_js_value)
NK_CHECK_ENTRY(webkit_navigation_policy_decision_get_navigation_action)
NK_CHECK_ENTRY(webkit_navigation_action_get_request)
NK_CHECK_ENTRY(webkit_uri_request_get_uri)
NK_CHECK_ENTRY(webkit_policy_decision_use)
NK_CHECK_ENTRY(webkit_policy_decision_ignore)
NK_CHECK_ENTRY(jsc_value_to_string)
NK_CHECK_ENTRY(jsc_value_to_json)
#undef NK_CHECK_ENTRY

/* Every check above is made by the compiler. */
int main() {
    return 0;
}
