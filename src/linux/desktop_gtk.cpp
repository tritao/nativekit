/*
 * Platform behavior in this file is informed by wxWidgets
 * src/gtk/webview_webkit2.cpp and src/gtk/window.cpp at the revision recorded
 * in tools/upstream-lock.json. Adaptations are licensed under the wxWindows
 * Library Licence 3.1; see licenses/wxWidgets.txt.
 */

#include "nativekit_window.h"
#include "nativekit_webview.h"
#include "nativekit_dialog.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>
#include <string>
#include <unordered_map>
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

struct DialogContext {
    GObject* object = nullptr;
    nk_request_id request = NK_INVALID_REQUEST_ID;
    nk_handle parent = NK_INVALID_HANDLE;
    uint32_t kind = 0;
    bool native_dialog = false;
};

bool gtk_initialized = false;
std::unordered_map<nk_request_id, DialogContext*> dialogs;

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

std::vector<std::byte> dialog_paths_payload(const std::vector<std::string>& paths,
                                            bool accepted) {
    const auto offsets_offset = sizeof(nk_dialog_paths);
    const auto strings_offset = offsets_offset + paths.size() * sizeof(uint32_t);
    std::size_t total = strings_offset;
    for (const auto& path : paths) total += path.size() + 1;
    std::vector<std::byte> result(total);
    const nk_dialog_paths header{
        accepted ? 1u : 0u,
        static_cast<uint32_t>(paths.size()),
        static_cast<uint32_t>(offsets_offset),
        static_cast<uint32_t>(strings_offset)};
    std::memcpy(result.data(), &header, sizeof(header));
    std::size_t cursor = strings_offset;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto offset = static_cast<uint32_t>(cursor);
        std::memcpy(result.data() + offsets_offset + index * sizeof(offset),
                    &offset, sizeof(offset));
        std::memcpy(result.data() + cursor, paths[index].c_str(), paths[index].size() + 1);
        cursor += paths[index].size() + 1;
    }
    return result;
}

void dispose_dialog(DialogContext* context) {
    dialogs.erase(context->request);
    g_signal_handlers_disconnect_by_data(context->object, context);
    if (context->native_dialog)
        gtk_native_dialog_hide(GTK_NATIVE_DIALOG(context->object));
    else
        gtk_widget_hide(GTK_WIDGET(context->object));
    g_object_unref(context->object);
    delete context;
}

void emit_file_dialog_completion(DialogContext* context, int response) {
    const bool accepted = response == GTK_RESPONSE_ACCEPT || response == GTK_RESPONSE_OK;
    std::vector<std::string> paths;
    if (accepted) {
        GSList* filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(context->object));
        for (GSList* item = filenames; item; item = item->next) {
            paths.emplace_back(static_cast<const char*>(item->data));
            g_free(item->data);
        }
        g_slist_free(filenames);
    }
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_DIALOG_COMPLETE;
    event.request_id = context->request;
    event.flags = context->kind;
    event.data_count = static_cast<uint32_t>(paths.size());
    event.data = dialog_paths_payload(paths, accepted);
    nk::core::push_event(std::move(event));
}

uint32_t message_result(int response) {
    switch (response) {
        case GTK_RESPONSE_OK: return NK_MESSAGE_RESULT_OK;
        case GTK_RESPONSE_YES: return NK_MESSAGE_RESULT_YES;
        case GTK_RESPONSE_NO: return NK_MESSAGE_RESULT_NO;
        case GTK_RESPONSE_CANCEL:
        case GTK_RESPONSE_DELETE_EVENT: return NK_MESSAGE_RESULT_CANCEL;
        default: return NK_MESSAGE_RESULT_NONE;
    }
}

void on_dialog_response(GObject*, int response, gpointer data) {
    auto* context = static_cast<DialogContext*>(data);
    try {
        if (context->kind == NK_DIALOG_MESSAGE) {
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_DIALOG_COMPLETE;
            event.request_id = context->request;
            event.flags = context->kind;
            const nk_dialog_message_result payload{message_result(response)};
            event.data = bytes_of(payload);
            nk::core::push_event(std::move(event));
        } else {
            emit_file_dialog_completion(context, response);
        }
    } catch (...) {}
    dispose_dialog(context);
}

void cancel_dialog(DialogContext* context, bool emit_event) {
    if (emit_event) {
        try {
            if (context->kind == NK_DIALOG_MESSAGE) {
                nk::core::QueuedEvent event;
                event.kind = NK_EVENT_DIALOG_COMPLETE;
                event.request_id = context->request;
                event.flags = context->kind;
                const nk_dialog_message_result payload{NK_MESSAGE_RESULT_CANCEL};
                event.data = bytes_of(payload);
                nk::core::push_event(std::move(event));
            } else {
                emit_file_dialog_completion(context, GTK_RESPONSE_CANCEL);
            }
        } catch (...) {}
    }
    dispose_dialog(context);
}

void cancel_dialogs_for_parent(nk_handle parent, bool emit_event) {
    std::vector<nk_request_id> requests;
    for (const auto& [request, dialog] : dialogs) {
        if (dialog->parent == parent) requests.push_back(request);
    }
    for (const auto request : requests) cancel_dialog(dialogs.at(request), emit_event);
}

void add_filters(GtkFileChooser* chooser, const nk_file_dialog_options* options) {
    for (uint32_t index = 0; index < options->filter_count; ++index) {
        const auto& definition = options->filters[index];
        if (!definition.patterns) continue;
        GtkFileFilter* filter = gtk_file_filter_new();
        if (definition.name) gtk_file_filter_set_name(filter, definition.name);
        char** patterns = g_strsplit(definition.patterns, ";", -1);
        for (char** pattern = patterns; pattern && *pattern; ++pattern) {
            if (**pattern) gtk_file_filter_add_pattern(filter, *pattern);
        }
        g_strfreev(patterns);
        gtk_file_chooser_add_filter(chooser, filter);
    }
}

nk_result start_file_dialog(nk_handle parent_handle,
                            const nk_file_dialog_options* options,
                            nk_request_id* out_request, uint32_t kind) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (!options || options->struct_size < sizeof(*options) || !out_request ||
        (options->filter_count && !options->filters)) {
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid file dialog options");
    }
    std::shared_ptr<GtkWindowResource> parent;
    if (parent_handle != NK_INVALID_HANDLE) {
        parent = window(parent_handle);
        if (!parent) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
    }
    if (!ensure_gtk()) return NK_ERROR_UNSUPPORTED;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
    if (kind == NK_DIALOG_SAVE_FILE) action = GTK_FILE_CHOOSER_ACTION_SAVE;
    if (kind == NK_DIALOG_SELECT_DIRECTORY) action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
    GtkFileChooserNative* chooser = gtk_file_chooser_native_new(
        options->title ? options->title : "",
        parent ? GTK_WINDOW(parent->window) : nullptr,
        action, nullptr, nullptr);
    if (!chooser) return fail(NK_ERROR_UNKNOWN, "could not create native file dialog");
    std::unique_ptr<GObject, decltype(&g_object_unref)> chooser_owner(
        G_OBJECT(chooser), &g_object_unref);
    auto context = std::make_unique<DialogContext>();
    context->object = G_OBJECT(chooser);
    context->request = nk::core::next_request_id();
    context->parent = parent_handle;
    context->kind = kind;
    context->native_dialog = true;
    auto* interface = GTK_FILE_CHOOSER(chooser);
    gtk_file_chooser_set_select_multiple(
        interface, kind == NK_DIALOG_OPEN_FILE && (options->flags & NK_DIALOG_ALLOW_MULTIPLE));
    gtk_file_chooser_set_do_overwrite_confirmation(
        interface, (options->flags & NK_DIALOG_CONFIRM_OVERWRITE) != 0);
    gtk_file_chooser_set_show_hidden(
        interface, (options->flags & NK_DIALOG_SHOW_HIDDEN) != 0);
    if (options->initial_path) {
        if (g_file_test(options->initial_path, G_FILE_TEST_IS_DIR))
            gtk_file_chooser_set_current_folder(interface, options->initial_path);
        else
            gtk_file_chooser_set_filename(interface, options->initial_path);
    }
    if (options->suggested_name && kind == NK_DIALOG_SAVE_FILE)
        gtk_file_chooser_set_current_name(interface, options->suggested_name);
    add_filters(interface, options);
    dialogs.emplace(context->request, context.get());
    g_signal_connect(chooser, "response", G_CALLBACK(on_dialog_response), context.get());
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
    *out_request = context->request;
    chooser_owner.release();
    context.release();
    return NK_OK;
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
    while (!dialogs.empty()) cancel_dialog(dialogs.begin()->second, false);
    nk::core::handles().clear();
    pump_events();
}
}

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_WINDOW | NK_CAP_WEBVIEW | NK_CAP_FILE_DIALOG;
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
    cancel_dialogs_for_parent(handle, true);
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
        const auto request = nk::core::next_request_id();
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

nk_result NK_CALL nk_dialog_open_file(nk_handle parent,
                                      const nk_file_dialog_options* options,
                                      nk_request_id* out_request) {
    try {
        return start_file_dialog(parent, options, out_request, NK_DIALOG_OPEN_FILE);
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while opening file dialog");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while opening file dialog");
    }
}

nk_result NK_CALL nk_dialog_save_file(nk_handle parent,
                                      const nk_file_dialog_options* options,
                                      nk_request_id* out_request) {
    try {
        return start_file_dialog(parent, options, out_request, NK_DIALOG_SAVE_FILE);
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while opening save dialog");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while opening save dialog");
    }
}

nk_result NK_CALL nk_dialog_select_directory(nk_handle parent,
                                             const nk_file_dialog_options* options,
                                             nk_request_id* out_request) {
    try {
        return start_file_dialog(parent, options, out_request, NK_DIALOG_SELECT_DIRECTORY);
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while opening directory dialog");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while opening directory dialog");
    }
}

nk_result NK_CALL nk_dialog_message(nk_handle parent_handle,
                                    const nk_message_dialog_options* options,
                                    nk_request_id* out_request) {
    try {
        if (const auto result = enter_ui(); result != NK_OK) return result;
        if (!options || options->struct_size < sizeof(*options) || !out_request || !options->message)
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid message dialog options");
        std::shared_ptr<GtkWindowResource> parent;
        if (parent_handle != NK_INVALID_HANDLE) {
            parent = window(parent_handle);
            if (!parent) return invalid_handle("parent window");
        }
        if (!ensure_gtk()) return NK_ERROR_UNSUPPORTED;
        GtkMessageType type = GTK_MESSAGE_INFO;
        if (options->kind == NK_MESSAGE_WARNING) type = GTK_MESSAGE_WARNING;
        if (options->kind == NK_MESSAGE_ERROR) type = GTK_MESSAGE_ERROR;
        if (options->kind == NK_MESSAGE_QUESTION) type = GTK_MESSAGE_QUESTION;
        GtkWidget* dialog = gtk_message_dialog_new(
            parent ? GTK_WINDOW(parent->window) : nullptr,
            static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            type, GTK_BUTTONS_NONE, "%s", options->message);
        g_object_ref_sink(dialog);
        if (options->title) gtk_window_set_title(GTK_WINDOW(dialog), options->title);
        const uint32_t buttons = options->buttons
            ? options->buttons : static_cast<uint32_t>(NK_MESSAGE_BUTTON_OK);
        if (buttons & NK_MESSAGE_BUTTON_OK)
            gtk_dialog_add_button(GTK_DIALOG(dialog), "_OK", GTK_RESPONSE_OK);
        if (buttons & NK_MESSAGE_BUTTON_CANCEL)
            gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
        if (buttons & NK_MESSAGE_BUTTON_YES)
            gtk_dialog_add_button(GTK_DIALOG(dialog), "_Yes", GTK_RESPONSE_YES);
        if (buttons & NK_MESSAGE_BUTTON_NO)
            gtk_dialog_add_button(GTK_DIALOG(dialog), "_No", GTK_RESPONSE_NO);
        std::unique_ptr<GObject, decltype(&g_object_unref)> dialog_owner(
            G_OBJECT(dialog), &g_object_unref);
        auto context = std::make_unique<DialogContext>();
        context->object = G_OBJECT(dialog);
        context->request = nk::core::next_request_id();
        context->parent = parent_handle;
        context->kind = NK_DIALOG_MESSAGE;
        dialogs.emplace(context->request, context.get());
        g_signal_connect(dialog, "response", G_CALLBACK(on_dialog_response), context.get());
        gtk_widget_show(dialog);
        *out_request = context->request;
        dialog_owner.release();
        context.release();
        return NK_OK;
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while opening message dialog");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while opening message dialog");
    }
}

nk_result NK_CALL nk_dialog_cancel(nk_request_id request) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    const auto found = dialogs.find(request);
    if (request == NK_INVALID_REQUEST_ID || found == dialogs.end())
        return fail(NK_ERROR_INVALID_REQUEST, "invalid or completed dialog request");
    cancel_dialog(found->second, true);
    return NK_OK;
}

}
