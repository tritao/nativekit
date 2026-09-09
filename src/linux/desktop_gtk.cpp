/*
 * Platform behavior in this file is informed by wxWidgets
 * src/gtk/webview_webkit2.cpp and src/gtk/window.cpp at the revision recorded
 * in tools/upstream-lock.json. Adaptations are licensed under the wxWindows
 * Library Licence 3.1; see licenses/wxWidgets.txt.
 */

#include "nativekit_window.h"
#include "nativekit_webview.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct GtkWindowResource final : nk::core::Resource {
    GtkWidget* window = nullptr;
    GtkWidget* container = nullptr;
    nk_handle handle = NK_INVALID_HANDLE;
    std::vector<nk_handle> children;

    ~GtkWindowResource() override {
        if (window) gtk_widget_destroy(window);
    }
};

struct GtkWebViewResource final : nk::core::Resource {
    GtkWidget* widget = nullptr;
    WebKitUserContentManager* content_manager = nullptr;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle parent = NK_INVALID_HANDLE;

    ~GtkWebViewResource() override {
        if (widget) gtk_widget_destroy(widget);
        if (content_manager) g_object_unref(content_manager);
    }
};

struct EvalContext {
    nk_handle source;
    nk_request_id request;
};

bool gtk_initialized = false;
std::atomic<nk_request_id> next_request{1};

nk_result fail(nk_result result, std::string_view message) {
    nk::core::set_error(message);
    return result;
}

nk_result enter_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

bool ensure_gtk() {
    if (gtk_initialized) return true;
    int argc = 0;
    char** argv = nullptr;
    gtk_initialized = gtk_init_check(&argc, &argv) != FALSE;
    if (!gtk_initialized) nk::core::set_error("GTK could not connect to a display");
    return gtk_initialized;
}

std::vector<std::byte> bytes(const char* text) {
    if (!text) return {};
    const auto size = std::strlen(text);
    const auto* first = reinterpret_cast<const std::byte*>(text);
    return {first, first + size};
}

template<typename T>
std::vector<std::byte> bytes_of(const T& value) {
    const auto* first = reinterpret_cast<const std::byte*>(&value);
    return {first, first + sizeof(value)};
}

gboolean on_window_delete(GtkWidget*, GdkEvent*, gpointer data) {
    const auto* resource = static_cast<GtkWindowResource*>(data);
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_WINDOW_CLOSE;
    event.source = resource->handle;
    nk::core::push_event(std::move(event));
    return TRUE;
}

gboolean on_window_configure(GtkWidget*, GdkEventConfigure* configure, gpointer data) {
    try {
        const auto* resource = static_cast<GtkWindowResource*>(data);
        const nk_window_resize_event payload{configure->width, configure->height};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WINDOW_RESIZE;
        event.source = resource->handle;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    } catch (...) {}
    return FALSE;
}

void on_window_scale(GtkWidget* widget, GParamSpec*, gpointer data) {
    try {
        const auto* resource = static_cast<GtkWindowResource*>(data);
        const nk_window_scale_event payload{
            static_cast<float>(gtk_widget_get_scale_factor(widget))};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WINDOW_SCALE_CHANGED;
        event.source = resource->handle;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    } catch (...) {}
}

uint32_t navigation_error_category(const GError* error) {
    if (error->domain == WEBKIT_NETWORK_ERROR) {
        switch (error->code) {
            case WEBKIT_NETWORK_ERROR_UNKNOWN_PROTOCOL:
                return NK_NAVIGATION_ERROR_REQUEST;
            case WEBKIT_NETWORK_ERROR_CANCELLED:
                return NK_NAVIGATION_ERROR_CANCELLED;
            case WEBKIT_NETWORK_ERROR_FILE_DOES_NOT_EXIST:
                return NK_NAVIGATION_ERROR_NOT_FOUND;
            case WEBKIT_NETWORK_ERROR_TRANSPORT:
                return NK_NAVIGATION_ERROR_CONNECTION;
            default:
                break;
        }
    } else if (error->domain == WEBKIT_POLICY_ERROR) {
        return error->code == WEBKIT_POLICY_ERROR_CANNOT_USE_RESTRICTED_PORT
            ? NK_NAVIGATION_ERROR_SECURITY : NK_NAVIGATION_ERROR_REQUEST;
    }
    return NK_NAVIGATION_ERROR_OTHER;
}

gboolean on_webview_load_failed(WebKitWebView*, WebKitLoadEvent, const char*,
                                GError* error, gpointer data) {
    try {
        const auto* resource = static_cast<GtkWebViewResource*>(data);
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WEBVIEW_NAVIGATION_FAILED;
        event.source = resource->handle;
        event.result = NK_ERROR_UNKNOWN;
        event.flags = navigation_error_category(error);
        event.data = bytes(error->message);
        nk::core::push_event(std::move(event));
    } catch (...) {}
    return FALSE;
}

void on_webview_process_terminated(WebKitWebView*,
                                   WebKitWebProcessTerminationReason reason,
                                   gpointer data) {
    const auto* resource = static_cast<GtkWebViewResource*>(data);
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_WEBVIEW_PROCESS_TERMINATED;
    event.source = resource->handle;
    event.result = NK_ERROR_UNKNOWN;
    event.flags = static_cast<uint32_t>(reason);
    nk::core::push_event(std::move(event));
}

void on_webview_load(WebKitWebView* view, WebKitLoadEvent load_event, gpointer data) {
    if (load_event != WEBKIT_LOAD_FINISHED) return;
    try {
        const auto* resource = static_cast<GtkWebViewResource*>(data);
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WEBVIEW_NAVIGATED;
        event.source = resource->handle;
        event.data = bytes(webkit_web_view_get_uri(view));
        nk::core::push_event(std::move(event));
    } catch (...) {}
}

void on_webview_title(WebKitWebView* view, GParamSpec*, gpointer data) {
    try {
        const auto* resource = static_cast<GtkWebViewResource*>(data);
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WEBVIEW_TITLE_CHANGED;
        event.source = resource->handle;
        event.data = bytes(webkit_web_view_get_title(view));
        nk::core::push_event(std::move(event));
    } catch (...) {}
}

void on_webview_message(WebKitUserContentManager*, WebKitJavascriptResult* result,
                        gpointer data) {
    char* string = nullptr;
    try {
        const auto* resource = static_cast<GtkWebViewResource*>(data);
        JSCValue* value = webkit_javascript_result_get_js_value(result);
        string = jsc_value_to_string(value);
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WEBVIEW_MESSAGE;
        event.source = resource->handle;
        event.data = bytes(string);
        nk::core::push_event(std::move(event));
    } catch (...) {
        /* WebKit callbacks must never allow a C++ exception to escape. */
    }
    g_free(string);
}

void on_eval_complete(GObject* object, GAsyncResult* result, gpointer data) {
    std::unique_ptr<EvalContext> context(static_cast<EvalContext*>(data));
    if (!nk::core::handles().get(context->source, nk::core::ResourceType::webview)) return;
    GError* error = nullptr;
    JSCValue* value = webkit_web_view_evaluate_javascript_finish(
        WEBKIT_WEB_VIEW(object), result, &error);
    char* string = nullptr;
    try {
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WEBVIEW_EVAL_COMPLETE;
        event.source = context->source;
        event.request_id = context->request;
        if (error) {
            event.result = NK_ERROR_UNKNOWN;
            event.data = bytes(error->message);
        } else if (value) {
            string = jsc_value_to_string(value);
            event.data = bytes(string);
        }
        nk::core::push_event(std::move(event));
    } catch (...) {
        /* GLib callbacks must never allow a C++ exception to escape. */
    }
    g_free(string);
    if (error) g_error_free(error);
    if (value) g_object_unref(value);
}

std::shared_ptr<GtkWindowResource> window(nk_handle handle) {
    return std::dynamic_pointer_cast<GtkWindowResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::window));
}

std::shared_ptr<GtkWebViewResource> webview(nk_handle handle) {
    return std::dynamic_pointer_cast<GtkWebViewResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::webview));
}

nk_result invalid_handle(const char* type) {
    (void)type;
    nk::core::set_error("invalid or stale resource handle");
    return NK_ERROR_INVALID_HANDLE;
}

}

namespace nk::backend {
void pump_events() noexcept {
    if (!gtk_initialized) return;
    while (g_main_context_iteration(nullptr, FALSE)) {}
}

void shutdown() noexcept {
    nk::core::handles().clear();
    pump_events();
}
}

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_WINDOW | NK_CAP_WEBVIEW;
}

nk_result NK_CALL nk_window_create(const nk_window_options* options, nk_handle* out_window) {
    try {
        if (const auto result = enter_ui(); result != NK_OK) return result;
        if (!options || options->struct_size < sizeof(*options) || !out_window ||
            options->width <= 0 || options->height <= 0) {
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid window options");
        }
        *out_window = NK_INVALID_HANDLE;
        if (!ensure_gtk()) return NK_ERROR_UNSUPPORTED;
        auto resource = std::make_shared<GtkWindowResource>();
        resource->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        g_object_add_weak_pointer(G_OBJECT(resource->window),
                                  reinterpret_cast<gpointer*>(&resource->window));
        resource->container = gtk_fixed_new();
        gtk_container_add(GTK_CONTAINER(resource->window), resource->container);
        gtk_window_set_default_size(GTK_WINDOW(resource->window), options->width, options->height);
        gtk_window_set_resizable(GTK_WINDOW(resource->window),
                                 (options->flags & NK_WINDOW_RESIZABLE) != 0);
        gtk_window_set_title(GTK_WINDOW(resource->window), options->title ? options->title : "");
        resource->handle = nk::core::handles().insert(nk::core::ResourceType::window, resource);
        if (resource->handle == NK_INVALID_HANDLE) {
            gtk_widget_destroy(resource->window);
            return fail(NK_ERROR_OUT_OF_MEMORY, "window handle registry is full");
        }
        g_signal_connect(resource->window, "delete-event", G_CALLBACK(on_window_delete), resource.get());
        g_signal_connect(resource->window, "configure-event", G_CALLBACK(on_window_configure), resource.get());
        g_signal_connect(resource->window, "notify::scale-factor", G_CALLBACK(on_window_scale), resource.get());
        if ((options->flags & NK_WINDOW_HIDDEN) == 0) gtk_widget_show_all(resource->window);
        *out_window = resource->handle;
        return NK_OK;
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while creating window");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while creating window");
    }
}

nk_result NK_CALL nk_window_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = window(handle);
    if (!resource) return invalid_handle("window");
    for (const auto child : resource->children) nk::core::handles().erase(child, nk::core::ResourceType::webview);
    g_signal_handlers_disconnect_by_data(resource->window, resource.get());
    gtk_widget_destroy(resource->window);
    resource->window = nullptr;
    resource->container = nullptr;
    nk::core::handles().erase(handle, nk::core::ResourceType::window);
    return NK_OK;
}

nk_result NK_CALL nk_window_show(nk_handle handle, uint32_t visible) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = window(handle);
    if (!resource) return invalid_handle("window");
    visible ? gtk_widget_show_all(resource->window) : gtk_widget_hide(resource->window);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_title(nk_handle handle, const char* title) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = window(handle);
    if (!resource) return invalid_handle("window");
    gtk_window_set_title(GTK_WINDOW(resource->window), title ? title : "");
    return NK_OK;
}

nk_result NK_CALL nk_window_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width, int32_t height) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (width <= 0 || height <= 0) return fail(NK_ERROR_INVALID_ARGUMENT, "window dimensions must be positive");
    auto resource = window(handle);
    if (!resource) return invalid_handle("window");
    gtk_window_move(GTK_WINDOW(resource->window), x, y);
    gtk_window_resize(GTK_WINDOW(resource->window), width, height);
    return NK_OK;
}

nk_result NK_CALL nk_window_get_scale(nk_handle handle, float* out_scale) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (!out_scale) return fail(NK_ERROR_INVALID_ARGUMENT, "scale output must not be null");
    auto resource = window(handle);
    if (!resource) return invalid_handle("window");
    *out_scale = static_cast<float>(gtk_widget_get_scale_factor(resource->window));
    return NK_OK;
}

nk_result NK_CALL nk_webview_create(nk_handle parent_handle, const nk_webview_options* options, nk_handle* out_webview) {
    try {
        if (const auto result = enter_ui(); result != NK_OK) return result;
        if (!options || options->struct_size < sizeof(*options) || !out_webview ||
            options->width <= 0 || options->height <= 0) {
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid WebView options");
        }
        *out_webview = NK_INVALID_HANDLE;
        auto parent = window(parent_handle);
        if (!parent) return invalid_handle("parent window");
        auto resource = std::make_shared<GtkWebViewResource>();
        resource->content_manager = webkit_user_content_manager_new();
        if (!webkit_user_content_manager_register_script_message_handler(
                resource->content_manager, "nativekit")) {
            return fail(NK_ERROR_UNKNOWN, "could not register the NativeKit JavaScript bridge");
        }
        resource->widget = webkit_web_view_new_with_user_content_manager(resource->content_manager);
        g_object_add_weak_pointer(G_OBJECT(resource->widget),
                                  reinterpret_cast<gpointer*>(&resource->widget));
        resource->parent = parent_handle;
        gtk_widget_set_size_request(resource->widget, options->width, options->height);
        gtk_fixed_put(GTK_FIXED(parent->container), resource->widget, options->x, options->y);
        resource->handle = nk::core::handles().insert(nk::core::ResourceType::webview, resource);
        if (resource->handle == NK_INVALID_HANDLE) {
            gtk_widget_destroy(resource->widget);
            return fail(NK_ERROR_OUT_OF_MEMORY, "WebView handle registry is full");
        }
        parent->children.push_back(resource->handle);
        g_signal_connect(resource->widget, "load-changed", G_CALLBACK(on_webview_load), resource.get());
        g_signal_connect(resource->widget, "load-failed", G_CALLBACK(on_webview_load_failed), resource.get());
        g_signal_connect(resource->widget, "notify::title", G_CALLBACK(on_webview_title), resource.get());
        g_signal_connect(resource->widget, "web-process-terminated",
                         G_CALLBACK(on_webview_process_terminated), resource.get());
        g_signal_connect(resource->content_manager, "script-message-received::nativekit",
                         G_CALLBACK(on_webview_message), resource.get());
        auto* settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(resource->widget));
        webkit_settings_set_enable_developer_extras(settings, (options->flags & NK_WEBVIEW_DEVTOOLS) != 0);
        if (options->initial_url) webkit_web_view_load_uri(WEBKIT_WEB_VIEW(resource->widget), options->initial_url);
        if ((options->flags & NK_WEBVIEW_HIDDEN) == 0) gtk_widget_show(resource->widget);
        *out_webview = resource->handle;
        return NK_OK;
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while creating WebView");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while creating WebView");
    }
}

nk_result NK_CALL nk_webview_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = webview(handle);
    if (!resource) return invalid_handle("WebView");
    g_signal_handlers_disconnect_by_data(resource->widget, resource.get());
    g_signal_handlers_disconnect_by_data(resource->content_manager, resource.get());
    gtk_widget_destroy(resource->widget);
    resource->widget = nullptr;
    nk::core::handles().erase(handle, nk::core::ResourceType::webview);
    return NK_OK;
}

nk_result NK_CALL nk_webview_show(nk_handle handle, uint32_t visible) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = webview(handle);
    if (!resource) return invalid_handle("WebView");
    visible ? gtk_widget_show(resource->widget) : gtk_widget_hide(resource->widget);
    return NK_OK;
}

nk_result NK_CALL nk_webview_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width, int32_t height) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (width <= 0 || height <= 0) return fail(NK_ERROR_INVALID_ARGUMENT, "WebView dimensions must be positive");
    auto resource = webview(handle);
    if (!resource) return invalid_handle("WebView");
    auto parent = window(resource->parent);
    if (!parent) return invalid_handle("parent window");
    gtk_fixed_move(GTK_FIXED(parent->container), resource->widget, x, y);
    gtk_widget_set_size_request(resource->widget, width, height);
    return NK_OK;
}

nk_result NK_CALL nk_webview_navigate(nk_handle handle, const char* url) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (!url) return fail(NK_ERROR_INVALID_ARGUMENT, "URL must not be null");
    auto resource = webview(handle);
    if (!resource) return invalid_handle("WebView");
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(resource->widget), url);
    return NK_OK;
}

nk_result NK_CALL nk_webview_set_html(nk_handle handle, const char* html, const char* base_url) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (!html) return fail(NK_ERROR_INVALID_ARGUMENT, "HTML must not be null");
    auto resource = webview(handle);
    if (!resource) return invalid_handle("WebView");
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(resource->widget), html, base_url);
    return NK_OK;
}

nk_result NK_CALL nk_webview_eval(nk_handle handle, const char* script, nk_request_id* out_request) {
    try {
        if (const auto result = enter_ui(); result != NK_OK) return result;
        if (!script || !out_request) return fail(NK_ERROR_INVALID_ARGUMENT, "invalid JavaScript evaluation arguments");
        auto resource = webview(handle);
        if (!resource) return invalid_handle("WebView");
        const auto request = next_request.fetch_add(1, std::memory_order_relaxed);
        auto context = std::make_unique<EvalContext>(EvalContext{handle, request});
        webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(resource->widget), script, -1,
                                            nullptr, nullptr, nullptr, on_eval_complete,
                                            context.release());
        *out_request = request;
        return NK_OK;
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while evaluating JavaScript");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while evaluating JavaScript");
    }
}

}
