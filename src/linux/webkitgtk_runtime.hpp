#pragma once

#include <gtk/gtk.h>

/*
 * The part of WebKitGTK 4.1's C API that the GTK backend uses, declared here so
 * NativeKit builds without WebKitGTK's development files and loads
 * libwebkit2gtk-4.1.so.0 the first time a WebView is created. A system without
 * it reports NK_CAP_WEBVIEW as unavailable. Types keep WebKit's own struct tags
 * and the values match webkit2/webkit2.h; the webkitgtk_abi test compares them
 * with the real header wherever it is installed.
 */

typedef struct _WebKitWebView WebKitWebView;
typedef struct _WebKitSettings WebKitSettings;
typedef struct _WebKitUserContentManager WebKitUserContentManager;
typedef struct _WebKitJavascriptResult WebKitJavascriptResult;
typedef struct _WebKitPolicyDecision WebKitPolicyDecision;
typedef struct _WebKitNavigationPolicyDecision WebKitNavigationPolicyDecision;
typedef struct _WebKitNavigationAction WebKitNavigationAction;
typedef struct _WebKitURIRequest WebKitURIRequest;
typedef struct _JSCValue JSCValue;

namespace nk::webkitgtk {

/* WebKitLoadEvent */
constexpr int load_finished = 3;
/* WebKitPolicyDecisionType */
constexpr int policy_decision_type_navigation_action = 0;
/* WebKitNetworkError */
constexpr int network_error_transport = 300;
constexpr int network_error_unknown_protocol = 301;
constexpr int network_error_cancelled = 302;
constexpr int network_error_file_does_not_exist = 303;
/* WebKitPolicyError */
constexpr int policy_error_cannot_use_restricted_port = 103;

/** WebKitGTK and JavaScriptCoreGTK entry points, resolved once from the installed library. */
struct Api {
    GType (*webkit_web_view_get_type)();
    GType (*webkit_policy_decision_get_type)();
    GType (*webkit_navigation_policy_decision_get_type)();
    GQuark (*webkit_network_error_quark)();
    GQuark (*webkit_policy_error_quark)();

    WebKitUserContentManager *(*webkit_user_content_manager_new)();
    gboolean (*webkit_user_content_manager_register_script_message_handler)(
        WebKitUserContentManager *, const gchar *);
    GtkWidget *(*webkit_web_view_new_with_user_content_manager)(WebKitUserContentManager *);
    WebKitSettings *(*webkit_web_view_get_settings)(WebKitWebView *);
    void (*webkit_settings_set_enable_developer_extras)(WebKitSettings *, gboolean);
    void (*webkit_web_view_load_uri)(WebKitWebView *, const gchar *);
    void (*webkit_web_view_load_html)(WebKitWebView *, const gchar *, const gchar *);
    const gchar *(*webkit_web_view_get_uri)(WebKitWebView *);
    const gchar *(*webkit_web_view_get_title)(WebKitWebView *);
    gboolean (*webkit_web_view_can_go_back)(WebKitWebView *);
    gboolean (*webkit_web_view_can_go_forward)(WebKitWebView *);
    void (*webkit_web_view_go_back)(WebKitWebView *);
    void (*webkit_web_view_go_forward)(WebKitWebView *);
    void (*webkit_web_view_reload)(WebKitWebView *);
    void (*webkit_web_view_stop_loading)(WebKitWebView *);
    void (*webkit_web_view_evaluate_javascript)(WebKitWebView *, const char *, gssize, const char *,
                                                const char *, GCancellable *, GAsyncReadyCallback,
                                                gpointer);
    JSCValue *(*webkit_web_view_evaluate_javascript_finish)(WebKitWebView *, GAsyncResult *,
                                                            GError **);
    JSCValue *(*webkit_javascript_result_get_js_value)(WebKitJavascriptResult *);
    WebKitNavigationAction *(*webkit_navigation_policy_decision_get_navigation_action)(
        WebKitNavigationPolicyDecision *);
    WebKitURIRequest *(*webkit_navigation_action_get_request)(WebKitNavigationAction *);
    const gchar *(*webkit_uri_request_get_uri)(WebKitURIRequest *);
    void (*webkit_policy_decision_use)(WebKitPolicyDecision *);
    void (*webkit_policy_decision_ignore)(WebKitPolicyDecision *);
    char *(*jsc_value_to_string)(JSCValue *);
    char *(*jsc_value_to_json)(JSCValue *, guint);
};

/**
 * Loads libwebkit2gtk-4.1.so.0 on first use, on the UI thread after GTK is initialized.
 * Returns null when it is not installed or lacks an entry point NativeKit uses.
 */
const Api *api() noexcept;

/** Checked casts, as WebKit's WEBKIT_WEB_VIEW() and friends; valid once api() succeeded. */
inline WebKitWebView *web_view(gpointer object) noexcept {
    return G_TYPE_CHECK_INSTANCE_CAST(object, api()->webkit_web_view_get_type(), WebKitWebView);
}
inline WebKitPolicyDecision *policy_decision(gpointer object) noexcept {
    return G_TYPE_CHECK_INSTANCE_CAST(object, api()->webkit_policy_decision_get_type(),
                                      WebKitPolicyDecision);
}
inline WebKitNavigationPolicyDecision *navigation_policy_decision(gpointer object) noexcept {
    return G_TYPE_CHECK_INSTANCE_CAST(object, api()->webkit_navigation_policy_decision_get_type(),
                                      WebKitNavigationPolicyDecision);
}

} // namespace nk::webkitgtk
