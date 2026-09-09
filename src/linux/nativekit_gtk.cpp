/*
 * Platform behavior in this file is informed by the wxWidgets GTK donor files
 * enumerated in tools/upstream-lock.json at its pinned revision. Adaptations are
 * licensed under the wxWindows Library Licence 3.1; see licenses/wxWidgets.txt.
 */

#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_graphics.h"
#include "nativekit_input.h"
#include "linux/joystick.hpp"
#include "nativekit_monitor.h"
#include "nativekit_notification.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"

#include <gtk/gtk.h>
#include <gdk/gdkconfig.h>
#ifdef GDK_WINDOWING_WAYLAND
#include <gdk/gdkwayland.h>
#endif
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#include <webkit2/webkit2.h>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

// GtkFixed normally derives its preferred size from its children. NativeKit
// children have explicit pixel bounds, so doing that turns those bounds into a
// top-level window minimum. Keep GtkFixed's positioning behavior while making
// the container itself freely shrinkable.
struct NkFixed {
    GtkFixed parent;
};

struct NkFixedClass {
    GtkFixedClass parent_class;
};

G_DEFINE_TYPE(NkFixed, nk_fixed, GTK_TYPE_FIXED)

void nk_fixed_get_preferred_width(GtkWidget *, gint *minimum, gint *natural) {
    *minimum = 1;
    *natural = 1;
}

void nk_fixed_get_preferred_height(GtkWidget *, gint *minimum, gint *natural) {
    *minimum = 1;
    *natural = 1;
}

void nk_fixed_class_init(NkFixedClass *klass) {
    auto *widget_class = GTK_WIDGET_CLASS(klass);
    widget_class->get_preferred_width = nk_fixed_get_preferred_width;
    widget_class->get_preferred_height = nk_fixed_get_preferred_height;
}

void nk_fixed_init(NkFixed *) {}

GtkWidget *nk_fixed_new() {
    return GTK_WIDGET(g_object_new(nk_fixed_get_type(), nullptr));
}

struct GtkCursorResource final : nk::core::Resource {
    GdkCursor *cursor = nullptr;
    nk_handle handle = NK_INVALID_HANDLE;

    ~GtkCursorResource() override {
        if (cursor)
            g_object_unref(cursor);
    }
};

struct GtkMonitorResource final : nk::core::Resource {
    GdkMonitor *monitor = nullptr;
    nk_handle handle = NK_INVALID_HANDLE;
    std::string name;

    ~GtkMonitorResource() override {
        if (monitor)
            g_object_unref(monitor);
    }
};

struct GtkWindowResource final : nk::core::Resource {
    GtkWidget *window = nullptr;
    GtkWidget *container = nullptr;
    GtkIMContext *im_context = nullptr;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle owner = NK_INVALID_HANDLE;
    std::vector<nk_handle> children;
    std::vector<nk_handle> surfaces;
    std::vector<nk_handle> owned_windows;
    bool drops_enabled = false;
    uint64_t generation = 0;
    std::array<nk_input_action, NK_KEY_LAST + 1> keys{};
    std::array<nk_input_action, NK_POINTER_BUTTON_LAST + 1> buttons{};
    double pointer_x = 0.0;
    double pointer_y = 0.0;
    nk_cursor_mode cursor_mode = NK_CURSOR_MODE_NORMAL;
    std::shared_ptr<GtkCursorResource> cursor;
    bool pointer_grabbed = false;
    bool hovered = false;
    uint32_t state_flags = 0;
    bool geometry_known = false;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t min_width = 0;
    int32_t min_height = 0;
    int32_t max_width = 0;
    int32_t max_height = 0;
    int32_t aspect_numerator = 0;
    int32_t aspect_denominator = 0;

    ~GtkWindowResource() override {
        if (pointer_grabbed && window) {
            if (GdkDisplay *display = gtk_widget_get_display(window))
                gdk_seat_ungrab(gdk_display_get_default_seat(display));
        }
        if (window)
            gtk_widget_destroy(window);
        if (im_context)
            g_object_unref(im_context);
    }
};

struct GtkSurfaceResource final : nk::core::Resource {
    GtkWidget *widget = nullptr;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle parent = NK_INVALID_HANDLE;
    uint64_t generation = 0;
    nk_graphics_api api = NK_GRAPHICS_OPENGL;
    uint32_t major_version = 0;
    uint32_t minor_version = 0;
    uint32_t flags = 0;
    uint32_t share_dependents = 0;
    std::shared_ptr<GtkSurfaceResource> shared_surface;

    ~GtkSurfaceResource() override {
        if (widget)
            gtk_widget_destroy(widget);
    }
};

struct GtkWebViewResource final : nk::core::Resource {
    GtkWidget *widget = nullptr;
    WebKitUserContentManager *content_manager = nullptr;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle parent = NK_INVALID_HANDLE;
    bool navigation_policy = false;
    uint64_t generation = 0;

    ~GtkWebViewResource() override {
        if (widget)
            gtk_widget_destroy(widget);
        if (content_manager)
            g_object_unref(content_manager);
    }
};

struct EvalContext {
    nk_handle source;
    nk_request_id request;
    uint64_t generation;
};

struct DialogContext {
    GObject *object = nullptr;
    nk_request_id request = NK_INVALID_REQUEST_ID;
    nk_handle parent = NK_INVALID_HANDLE;
    uint32_t kind = 0;
    bool native_dialog = false;
    uint64_t generation = 0;
};

struct ClipboardRequest {
    nk_request_id request;
    nk_event_kind event_kind;
    uint64_t generation;
};

struct ClipboardFileOwner {
    std::vector<std::string> uris;
    std::vector<char *> pointers;
};

struct NavigationDecision {
    nk_handle source;
    WebKitPolicyDecision *decision;
};

struct NotificationRequest {
    uint32_t server_id = 0;
    bool canceled = false;
    uint64_t generation = 0;
};

struct NotificationContext {
    nk_request_id request;
    uint64_t generation;
};

bool gtk_initialized = false;
bool clipboard_owned = false;
GdkDisplay *monitor_display = nullptr;
gulong monitor_added_signal = 0;
gulong monitor_removed_signal = 0;
std::unordered_map<GdkMonitor *, nk_handle> monitor_handles;
std::unordered_map<nk_request_id, DialogContext *> dialogs;
std::unordered_map<nk_request_id, NavigationDecision> navigation_decisions;
std::unordered_map<nk_request_id, nk_handle> evaluations;
std::unordered_map<nk_request_id, NotificationRequest> notifications;
std::unordered_map<uint32_t, nk_request_id> notification_ids;
GDBusConnection *notification_bus = nullptr;
guint notification_action_subscription = 0;
guint notification_closed_subscription = 0;

nk_result fail(nk_result result, std::string_view message) {
    nk::core::set_error(message);
    return result;
}

nk_result enter_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

bool ensure_gtk() {
    if (gtk_initialized)
        return true;
    int argc = 0;
    char **argv = nullptr;
    gtk_initialized = gtk_init_check(&argc, &argv) != FALSE;
    if (!gtk_initialized)
        nk::core::set_error("GTK could not connect to a display");
    return gtk_initialized;
}

std::vector<std::byte> bytes(const char *text) {
    if (!text)
        return {};
    const auto size = std::strlen(text);
    const auto *first = reinterpret_cast<const std::byte *>(text);
    return {first, first + size};
}

std::string javascript_literal(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string result{"\""};
    result.reserve(value.size() + 2);
    for (const unsigned char character : value) {
        if (character == '"' || character == '\\') {
            result += '\\';
            result += static_cast<char>(character);
        } else if (character == '\n')
            result += "\\n";
        else if (character == '\r')
            result += "\\r";
        else if (character == '\t')
            result += "\\t";
        else if (character < 0x20) {
            result += "\\u00";
            result += hex[(character >> 4) & 0xf];
            result += hex[character & 0xf];
        } else {
            result += static_cast<char>(character);
        }
    }
    result += '"';
    return result;
}

template <typename T> std::vector<std::byte> bytes_of(const T &value) {
    const auto *first = reinterpret_cast<const std::byte *>(&value);
    return {first, first + sizeof(value)};
}

void update_window_state(GtkWindowResource &resource, uint32_t flags) {
    if (resource.state_flags == flags)
        return;
    resource.state_flags = flags;
    const nk_window_state payload{sizeof(payload), flags, {0, 0}};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_WINDOW_STATE_CHANGED;
    event.source = resource.handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

void on_window_map(GtkWidget *, gpointer data) {
    nk::core::callback_boundary([&] {
        auto &resource = *static_cast<GtkWindowResource *>(data);
        if (!nk::core::is_runtime_generation(resource.generation))
            return;
        update_window_state(resource, resource.state_flags | NK_WINDOW_STATE_VISIBLE);
    });
}

void on_window_unmap(GtkWidget *, gpointer data) {
    nk::core::callback_boundary([&] {
        auto &resource = *static_cast<GtkWindowResource *>(data);
        if (!nk::core::is_runtime_generation(resource.generation))
            return;
        update_window_state(resource, resource.state_flags &
                                          ~(NK_WINDOW_STATE_VISIBLE | NK_WINDOW_STATE_ACTIVE));
    });
}

nk_modifiers modifiers(GdkModifierType state) {
    nk_modifiers result = 0;
    if (state & GDK_SHIFT_MASK)
        result |= NK_MOD_SHIFT;
    if (state & GDK_CONTROL_MASK)
        result |= NK_MOD_CONTROL;
    if (state & GDK_MOD1_MASK)
        result |= NK_MOD_ALT;
    if (state & GDK_SUPER_MASK)
        result |= NK_MOD_SUPER;
    if (state & GDK_LOCK_MASK)
        result |= NK_MOD_CAPS_LOCK;
    return result;
}

nk_key key_from_gdk(guint value) {
    if (value >= GDK_KEY_a && value <= GDK_KEY_z)
        return NK_KEY_A + value - GDK_KEY_a;
    if (value >= GDK_KEY_A && value <= GDK_KEY_Z)
        return NK_KEY_A + value - GDK_KEY_A;
    if (value >= GDK_KEY_0 && value <= GDK_KEY_9)
        return NK_KEY_0 + value - GDK_KEY_0;
    if (value >= GDK_KEY_F1 && value <= GDK_KEY_F25)
        return NK_KEY_F1 + value - GDK_KEY_F1;
    if (value >= GDK_KEY_KP_0 && value <= GDK_KEY_KP_9)
        return NK_KEY_KP_0 + value - GDK_KEY_KP_0;
    switch (value) {
    case GDK_KEY_space: return NK_KEY_SPACE;
    case GDK_KEY_apostrophe: return NK_KEY_APOSTROPHE;
    case GDK_KEY_comma: return NK_KEY_COMMA;
    case GDK_KEY_minus: return NK_KEY_MINUS;
    case GDK_KEY_period: return NK_KEY_PERIOD;
    case GDK_KEY_slash: return NK_KEY_SLASH;
    case GDK_KEY_semicolon: return NK_KEY_SEMICOLON;
    case GDK_KEY_equal: return NK_KEY_EQUAL;
    case GDK_KEY_bracketleft: return NK_KEY_LEFT_BRACKET;
    case GDK_KEY_backslash: return NK_KEY_BACKSLASH;
    case GDK_KEY_bracketright: return NK_KEY_RIGHT_BRACKET;
    case GDK_KEY_grave: return NK_KEY_GRAVE_ACCENT;
    case GDK_KEY_Escape: return NK_KEY_ESCAPE;
    case GDK_KEY_Return: return NK_KEY_ENTER;
    case GDK_KEY_KP_Enter: return NK_KEY_KP_ENTER;
    case GDK_KEY_KP_Decimal: return NK_KEY_KP_DECIMAL;
    case GDK_KEY_KP_Divide: return NK_KEY_KP_DIVIDE;
    case GDK_KEY_KP_Multiply: return NK_KEY_KP_MULTIPLY;
    case GDK_KEY_KP_Subtract: return NK_KEY_KP_SUBTRACT;
    case GDK_KEY_KP_Add: return NK_KEY_KP_ADD;
    case GDK_KEY_KP_Equal: return NK_KEY_KP_EQUAL;
    case GDK_KEY_Tab:
    case GDK_KEY_ISO_Left_Tab: return NK_KEY_TAB;
    case GDK_KEY_BackSpace: return NK_KEY_BACKSPACE;
    case GDK_KEY_Insert: return NK_KEY_INSERT;
    case GDK_KEY_Delete: return NK_KEY_DELETE;
    case GDK_KEY_Right: return NK_KEY_RIGHT;
    case GDK_KEY_Left: return NK_KEY_LEFT;
    case GDK_KEY_Down: return NK_KEY_DOWN;
    case GDK_KEY_Up: return NK_KEY_UP;
    case GDK_KEY_Page_Up: return NK_KEY_PAGE_UP;
    case GDK_KEY_Page_Down: return NK_KEY_PAGE_DOWN;
    case GDK_KEY_Home: return NK_KEY_HOME;
    case GDK_KEY_End: return NK_KEY_END;
    case GDK_KEY_Caps_Lock: return NK_KEY_CAPS_LOCK;
    case GDK_KEY_Scroll_Lock: return NK_KEY_SCROLL_LOCK;
    case GDK_KEY_Num_Lock: return NK_KEY_NUM_LOCK;
    case GDK_KEY_Print: return NK_KEY_PRINT_SCREEN;
    case GDK_KEY_Pause: return NK_KEY_PAUSE;
    case GDK_KEY_Shift_L: return NK_KEY_LEFT_SHIFT;
    case GDK_KEY_Control_L: return NK_KEY_LEFT_CONTROL;
    case GDK_KEY_Alt_L: return NK_KEY_LEFT_ALT;
    case GDK_KEY_Super_L: return NK_KEY_LEFT_SUPER;
    case GDK_KEY_Shift_R: return NK_KEY_RIGHT_SHIFT;
    case GDK_KEY_Control_R: return NK_KEY_RIGHT_CONTROL;
    case GDK_KEY_Alt_R: return NK_KEY_RIGHT_ALT;
    case GDK_KEY_Super_R: return NK_KEY_RIGHT_SUPER;
    case GDK_KEY_Menu: return NK_KEY_MENU;
    default: return NK_KEY_UNKNOWN;
    }
}

nk_pointer_button button_from_gdk(guint button) {
    switch (button) {
    case 1: return NK_POINTER_BUTTON_LEFT;
    case 2: return NK_POINTER_BUTTON_MIDDLE;
    case 3: return NK_POINTER_BUTTON_RIGHT;
    case 8: return NK_POINTER_BUTTON_4;
    case 9: return NK_POINTER_BUTTON_5;
    default:
        return button > 0 && button <= NK_POINTER_BUTTON_LAST + 1 ? button - 1
                                                                  : UINT32_MAX;
    }
}

void on_text_commit(GtkIMContext *, gchar *text, gpointer data) {
    nk::core::callback_boundary([&] {
        auto *resource = static_cast<GtkWindowResource *>(data);
        if (!nk::core::is_runtime_generation(resource->generation))
            return;
        const gchar *cursor = text;
        while (cursor && *cursor) {
            const gunichar codepoint = g_utf8_get_char_validated(cursor, -1);
            if (codepoint == static_cast<gunichar>(-1) ||
                codepoint == static_cast<gunichar>(-2))
                return;
            const nk_text_input_event payload{codepoint, 0};
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_TEXT_INPUT;
            event.source = resource->handle;
            event.data = bytes_of(payload);
            nk::core::push_event(std::move(event));
            cursor = g_utf8_next_char(cursor);
        }
    });
}

void on_window_realize(GtkWidget *widget, gpointer data) {
    auto *resource = static_cast<GtkWindowResource *>(data);
    if (resource->im_context)
        gtk_im_context_set_client_window(resource->im_context, gtk_widget_get_window(widget));
}

gboolean on_input_focus(GtkWidget *, GdkEventFocus *focus, gpointer data) {
    auto *resource = static_cast<GtkWindowResource *>(data);
    if (resource->im_context) {
        if (focus->in)
            gtk_im_context_focus_in(resource->im_context);
        else {
            gtk_im_context_focus_out(resource->im_context);
            gtk_im_context_reset(resource->im_context);
        }
    }
    return FALSE;
}

gboolean on_key(GtkWidget *, GdkEventKey *key_event, gpointer data) {
    gboolean im_handled = FALSE;
    nk::core::callback_boundary([&] {
        auto *resource = static_cast<GtkWindowResource *>(data);
        if (!nk::core::is_runtime_generation(resource->generation))
            return;
        const nk_key key = key_from_gdk(key_event->keyval);
        nk_input_action action = key_event->type == GDK_KEY_RELEASE ? NK_INPUT_RELEASE
                                                                    : NK_INPUT_PRESS;
        if (key != NK_KEY_UNKNOWN) {
            if (action == NK_INPUT_PRESS && resource->keys[key] == NK_INPUT_PRESS)
                action = NK_INPUT_REPEAT;
            resource->keys[key] = action == NK_INPUT_RELEASE ? NK_INPUT_RELEASE : NK_INPUT_PRESS;
        }
        const nk_key_event payload{key, key_event->hardware_keycode, action,
                                   modifiers(static_cast<GdkModifierType>(key_event->state))};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_KEY;
        event.source = resource->handle;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
        if (resource->im_context)
            im_handled = gtk_im_context_filter_keypress(resource->im_context, key_event);
    });
    return im_handled;
}

gboolean on_pointer_move(GtkWidget *, GdkEventMotion *motion, gpointer data) {
    nk::core::callback_boundary([&] {
        auto *resource = static_cast<GtkWindowResource *>(data);
        resource->pointer_x = motion->x;
        resource->pointer_y = motion->y;
        const nk_pointer_move_event payload{motion->x, motion->y};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_POINTER_MOVE;
        event.source = resource->handle;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    });
    return FALSE;
}

gboolean on_pointer_button(GtkWidget *, GdkEventButton *button_event, gpointer data) {
    nk::core::callback_boundary([&] {
        auto *resource = static_cast<GtkWindowResource *>(data);
        const auto button = button_from_gdk(button_event->button);
        if (button == UINT32_MAX)
            return;
        const nk_input_action action = button_event->type == GDK_BUTTON_RELEASE
                                           ? NK_INPUT_RELEASE
                                           : NK_INPUT_PRESS;
        resource->buttons[button] = action;
        resource->pointer_x = button_event->x;
        resource->pointer_y = button_event->y;
        const nk_pointer_button_event payload{
            button, action, modifiers(static_cast<GdkModifierType>(button_event->state)), 0,
            button_event->x, button_event->y};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_POINTER_BUTTON;
        event.source = resource->handle;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    });
    return FALSE;
}

gboolean on_pointer_scroll(GtkWidget *, GdkEventScroll *scroll, gpointer data) {
    nk::core::callback_boundary([&] {
        auto *resource = static_cast<GtkWindowResource *>(data);
        double x = 0.0;
        double y = 0.0;
        if (scroll->direction == GDK_SCROLL_SMOOTH)
            gdk_event_get_scroll_deltas(reinterpret_cast<GdkEvent *>(scroll), &x, &y);
        else if (scroll->direction == GDK_SCROLL_UP)
            y = -1.0;
        else if (scroll->direction == GDK_SCROLL_DOWN)
            y = 1.0;
        else if (scroll->direction == GDK_SCROLL_LEFT)
            x = -1.0;
        else if (scroll->direction == GDK_SCROLL_RIGHT)
            x = 1.0;
        const nk_pointer_scroll_event payload{x, y};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_POINTER_SCROLL;
        event.source = resource->handle;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    });
    return FALSE;
}

gboolean on_pointer_crossing(GtkWidget *, GdkEventCrossing *crossing, gpointer data) {
    auto *resource = static_cast<GtkWindowResource *>(data);
    resource->hovered = crossing->type == GDK_ENTER_NOTIFY;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_POINTER_ENTER;
    event.source = resource->handle;
    event.flags = resource->hovered ? 1u : 0u;
    nk::core::push_event(std::move(event));
    return FALSE;
}

gboolean on_surface_render(GtkGLArea *, GdkGLContext *, gpointer) { return TRUE; }

GdkGLContext *on_surface_create_context(GtkGLArea *area, gpointer data) {
    auto *resource = static_cast<GtkSurfaceResource *>(data);
    if (resource->shared_surface) {
        GdkGLContext *shared =
            gtk_gl_area_get_context(GTK_GL_AREA(resource->shared_surface->widget));
        return shared ? GDK_GL_CONTEXT(g_object_ref(shared)) : nullptr;
    }
    GError *error = nullptr;
    GdkWindow *native = gtk_widget_get_window(GTK_WIDGET(area));
    GdkGLContext *context = native ? gdk_window_create_gl_context(native, &error) : nullptr;
    if (context) {
        gdk_gl_context_set_use_es(context, resource->api == NK_GRAPHICS_OPENGL_ES);
        if (resource->major_version)
            gdk_gl_context_set_required_version(context, resource->major_version,
                                                resource->minor_version);
        gdk_gl_context_set_debug_enabled(
            context, (resource->flags & NK_SURFACE_DEBUG_CONTEXT) != 0);
        gdk_gl_context_set_forward_compatible(
            context, (resource->flags & NK_SURFACE_FORWARD_COMPATIBLE) != 0);
        if (!gdk_gl_context_realize(context, &error)) {
            g_object_unref(context);
            context = nullptr;
        }
    }
    if (error) {
        gtk_gl_area_set_error(area, error);
        g_error_free(error);
    }
    return context;
}

void on_surface_resize(GtkGLArea *area, gint width, gint height, gpointer data) {
    nk::core::callback_boundary([&] {
        auto *resource = static_cast<GtkSurfaceResource *>(data);
        if (!nk::core::is_runtime_generation(resource->generation))
            return;
        const int scale = gtk_widget_get_scale_factor(GTK_WIDGET(area));
        const nk_surface_resize_event payload{width, height, width * scale, height * scale};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_SURFACE_RESIZE;
        event.source = resource->handle;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    });
}

gboolean on_window_delete(GtkWidget *, GdkEvent *, gpointer data) {
    const auto *resource = static_cast<GtkWindowResource *>(data);
    if (!nk::core::is_runtime_generation(resource->generation))
        return TRUE;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_WINDOW_CLOSE;
    event.source = resource->handle;
    nk::core::push_event(std::move(event));
    return TRUE;
}

gboolean on_window_configure(GtkWidget *, GdkEventConfigure *configure, gpointer data) {
    nk::core::callback_boundary([&] {
        auto *resource = static_cast<GtkWindowResource *>(data);
        if (!nk::core::is_runtime_generation(resource->generation))
            return;
        if (!resource->geometry_known || resource->width != configure->width ||
            resource->height != configure->height) {
            resource->width = configure->width;
            resource->height = configure->height;
            const nk_window_resize_event payload{configure->width, configure->height};
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_WINDOW_RESIZE;
            event.source = resource->handle;
            event.data = bytes_of(payload);
            nk::core::push_event(std::move(event));
            const int scale = gtk_widget_get_scale_factor(resource->window);
            const nk_window_framebuffer_resize_event framebuffer{
                configure->width * scale, configure->height * scale};
            nk::core::QueuedEvent framebuffer_event;
            framebuffer_event.kind = NK_EVENT_WINDOW_FRAMEBUFFER_RESIZE;
            framebuffer_event.source = resource->handle;
            framebuffer_event.data = bytes_of(framebuffer);
            nk::core::push_event(std::move(framebuffer_event));
        }
        bool position_available = true;
#ifdef GDK_WINDOWING_WAYLAND
        position_available =
            !GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(resource->window));
#endif
        if (position_available &&
            (!resource->geometry_known || resource->x != configure->x ||
             resource->y != configure->y)) {
            resource->x = configure->x;
            resource->y = configure->y;
            const nk_window_move_event payload{configure->x, configure->y};
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_WINDOW_MOVE;
            event.source = resource->handle;
            event.data = bytes_of(payload);
            nk::core::push_event(std::move(event));
        }
        resource->geometry_known = true;
    });
    return FALSE;
}

void on_window_scale(GtkWidget *widget, GParamSpec *, gpointer data) {
    nk::core::callback_boundary([&] {
        const auto *resource = static_cast<GtkWindowResource *>(data);
        if (!nk::core::is_runtime_generation(resource->generation))
            return;
        const nk_window_scale_event payload{
            static_cast<float>(gtk_widget_get_scale_factor(widget))};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WINDOW_SCALE_CHANGED;
        event.source = resource->handle;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
        const int scale = gtk_widget_get_scale_factor(widget);
        const nk_window_framebuffer_resize_event framebuffer{
            gtk_widget_get_allocated_width(widget) * scale,
            gtk_widget_get_allocated_height(widget) * scale};
        nk::core::QueuedEvent framebuffer_event;
        framebuffer_event.kind = NK_EVENT_WINDOW_FRAMEBUFFER_RESIZE;
        framebuffer_event.source = resource->handle;
        framebuffer_event.data = bytes_of(framebuffer);
        nk::core::push_event(std::move(framebuffer_event));
    });
}

gboolean on_window_state(GtkWidget *, GdkEventWindowState *state, gpointer data) {
    nk::core::callback_boundary([&] {
        auto *resource = static_cast<GtkWindowResource *>(data);
        if (!nk::core::is_runtime_generation(resource->generation))
            return;
        if ((state->changed_mask & GDK_WINDOW_STATE_FOCUSED) &&
            !(state->new_window_state & GDK_WINDOW_STATE_FOCUSED)) {
            for (nk_key key = 1; key <= NK_KEY_LAST; ++key) {
                if (resource->keys[key] != NK_INPUT_PRESS)
                    continue;
                resource->keys[key] = NK_INPUT_RELEASE;
                const nk_key_event released{key, 0, NK_INPUT_RELEASE, 0};
                nk::core::QueuedEvent release_event;
                release_event.kind = NK_EVENT_KEY;
                release_event.source = resource->handle;
                release_event.flags = 1u; /* Synthetic focus-loss release. */
                release_event.data = bytes_of(released);
                nk::core::push_event(std::move(release_event));
            }
            for (nk_pointer_button button = 0; button <= NK_POINTER_BUTTON_LAST; ++button) {
                if (resource->buttons[button] != NK_INPUT_PRESS)
                    continue;
                resource->buttons[button] = NK_INPUT_RELEASE;
                const nk_pointer_button_event released{button, NK_INPUT_RELEASE, 0, 0,
                                                       resource->pointer_x, resource->pointer_y};
                nk::core::QueuedEvent release_event;
                release_event.kind = NK_EVENT_POINTER_BUTTON;
                release_event.source = resource->handle;
                release_event.flags = 1u;
                release_event.data = bytes_of(released);
                nk::core::push_event(std::move(release_event));
            }
        }
        uint32_t flags = resource->state_flags &
                         (NK_WINDOW_STATE_VISIBLE | NK_WINDOW_STATE_ATTENTION_REQUESTED);
        if (state->new_window_state & GDK_WINDOW_STATE_FOCUSED)
            flags |= NK_WINDOW_STATE_ACTIVE;
        if (state->new_window_state & GDK_WINDOW_STATE_ICONIFIED)
            flags |= NK_WINDOW_STATE_MINIMIZED;
        if (state->new_window_state & GDK_WINDOW_STATE_MAXIMIZED)
            flags |= NK_WINDOW_STATE_MAXIMIZED;
        if (state->new_window_state & GDK_WINDOW_STATE_FULLSCREEN)
            flags |= NK_WINDOW_STATE_FULLSCREEN;
        if (flags & NK_WINDOW_STATE_ACTIVE) {
            flags &= ~NK_WINDOW_STATE_ATTENTION_REQUESTED;
            gtk_window_set_urgency_hint(GTK_WINDOW(resource->window), FALSE);
        }
        update_window_state(*resource, flags);
    });
    return FALSE;
}

uint32_t navigation_error_category(const GError *error) {
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
                   ? NK_NAVIGATION_ERROR_SECURITY
                   : NK_NAVIGATION_ERROR_REQUEST;
    }
    return NK_NAVIGATION_ERROR_OTHER;
}

gboolean on_webview_load_failed(WebKitWebView *, WebKitLoadEvent, const char *, GError *error,
                                gpointer data) {
    nk::core::callback_boundary([&] {
        const auto *resource = static_cast<GtkWebViewResource *>(data);
        if (!nk::core::is_runtime_generation(resource->generation))
            return;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WEBVIEW_NAVIGATION_FAILED;
        event.source = resource->handle;
        event.result = NK_ERROR_UNKNOWN;
        event.flags = navigation_error_category(error);
        event.data = bytes(error->message);
        nk::core::push_event(std::move(event));
    });
    return FALSE;
}

void on_webview_process_terminated(WebKitWebView *, WebKitWebProcessTerminationReason reason,
                                   gpointer data) {
    const auto *resource = static_cast<GtkWebViewResource *>(data);
    if (!nk::core::is_runtime_generation(resource->generation))
        return;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_WEBVIEW_PROCESS_TERMINATED;
    event.source = resource->handle;
    event.result = NK_ERROR_UNKNOWN;
    event.flags = static_cast<uint32_t>(reason);
    nk::core::push_event(std::move(event));
}

void on_webview_load(WebKitWebView *view, WebKitLoadEvent load_event, gpointer data) {
    if (load_event != WEBKIT_LOAD_FINISHED)
        return;
    nk::core::callback_boundary([&] {
        const auto *resource = static_cast<GtkWebViewResource *>(data);
        if (!nk::core::is_runtime_generation(resource->generation))
            return;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WEBVIEW_NAVIGATED;
        event.source = resource->handle;
        event.data = bytes(webkit_web_view_get_uri(view));
        nk::core::push_event(std::move(event));
    });
}

void on_webview_title(WebKitWebView *view, GParamSpec *, gpointer data) {
    nk::core::callback_boundary([&] {
        const auto *resource = static_cast<GtkWebViewResource *>(data);
        if (!nk::core::is_runtime_generation(resource->generation))
            return;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WEBVIEW_TITLE_CHANGED;
        event.source = resource->handle;
        event.data = bytes(webkit_web_view_get_title(view));
        nk::core::push_event(std::move(event));
    });
}

void on_webview_message(WebKitUserContentManager *, WebKitJavascriptResult *result, gpointer data) {
    char *string = nullptr;
    nk::core::callback_boundary([&] {
        const auto *resource = static_cast<GtkWebViewResource *>(data);
        if (!nk::core::is_runtime_generation(resource->generation))
            return;
        JSCValue *value = webkit_javascript_result_get_js_value(result);
        string = jsc_value_to_json(value, 0);
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WEBVIEW_MESSAGE;
        event.source = resource->handle;
        if (string) {
            event.data = bytes(string);
        } else {
            event.result = NK_ERROR_UNKNOWN;
            event.data = bytes("JavaScript message is not JSON-serializable");
        }
        nk::core::push_event(std::move(event));
    });
    g_free(string);
}

gboolean on_webview_policy(WebKitWebView *, WebKitPolicyDecision *decision,
                           WebKitPolicyDecisionType type, gpointer data) {
    auto *resource = static_cast<GtkWebViewResource *>(data);
    if (!nk::core::is_runtime_generation(resource->generation))
        return FALSE;
    if (!resource->navigation_policy || type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION)
        return FALSE;
    bool completed = false;
    nk_request_id request_id = NK_INVALID_REQUEST_ID;
    WebKitPolicyDecision *retained = nullptr;
    bool inserted = false;
    nk::core::callback_boundary([&] {
        auto *navigation = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
        auto *action = webkit_navigation_policy_decision_get_navigation_action(navigation);
        auto *request = webkit_navigation_action_get_request(action);
        request_id = nk::core::next_request_id();
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WEBVIEW_NAVIGATION_REQUEST;
        event.source = resource->handle;
        event.request_id = request_id;
        event.data = bytes(webkit_uri_request_get_uri(request));
        retained = WEBKIT_POLICY_DECISION(g_object_ref(decision));
        navigation_decisions.emplace(request_id, NavigationDecision{resource->handle, retained});
        inserted = true;
        if (nk::core::push_event(std::move(event)) != NK_OK) {
            navigation_decisions.erase(request_id);
            inserted = false;
            webkit_policy_decision_use(decision);
            g_object_unref(retained);
            retained = nullptr;
        }
        completed = true;
    });
    if (!completed) {
        if (inserted)
            navigation_decisions.erase(request_id);
        if (retained)
            g_object_unref(retained);
        webkit_policy_decision_use(decision);
    }
    return TRUE;
}

void cancel_navigation_decisions(nk_handle source) {
    for (auto item = navigation_decisions.begin(); item != navigation_decisions.end();) {
        if (item->second.source == source) {
            webkit_policy_decision_ignore(item->second.decision);
            g_object_unref(item->second.decision);
            item = navigation_decisions.erase(item);
        } else {
            ++item;
        }
    }
}

void on_eval_complete(GObject *object, GAsyncResult *result, gpointer data) {
    std::unique_ptr<EvalContext> context(static_cast<EvalContext *>(data));
    if (!nk::core::is_runtime_generation(context->generation))
        return;
    const auto pending = evaluations.find(context->request);
    if (pending == evaluations.end() || pending->second != context->source)
        return;
    evaluations.erase(pending);
    if (!nk::core::handles().get(context->source, nk::core::ResourceType::webview))
        return;
    GError *error = nullptr;
    JSCValue *value =
        webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(object), result, &error);
    char *string = nullptr;
    nk::core::callback_boundary([&] {
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
    });
    g_free(string);
    if (error)
        g_error_free(error);
    if (value)
        g_object_unref(value);
}

void cancel_evaluations(nk_handle source) noexcept {
    for (auto item = evaluations.begin(); item != evaluations.end();) {
        if (source && item->second != source) {
            ++item;
            continue;
        }
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WEBVIEW_EVAL_COMPLETE;
        event.source = item->second;
        event.request_id = item->first;
        event.result = NK_ERROR_INVALID_REQUEST;
        nk::core::push_event(std::move(event));
        item = evaluations.erase(item);
    }
}

std::shared_ptr<GtkWindowResource> window(nk_handle handle) {
    return std::dynamic_pointer_cast<GtkWindowResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::window));
}

void apply_geometry_hints(const GtkWindowResource &resource) {
    GdkGeometry geometry{};
    // GtkFixed propagates child size requests as its preferred size. Keep a
    // minimal explicit hint even when the caller did not request a limit so a
    // WebView or surface does not become an accidental window minimum.
    geometry.min_width = std::max(resource.min_width, 1);
    geometry.min_height = std::max(resource.min_height, 1);
    geometry.max_width = resource.max_width ? resource.max_width : G_MAXINT;
    geometry.max_height = resource.max_height ? resource.max_height : G_MAXINT;
    GdkWindowHints hints = GDK_HINT_MIN_SIZE;
    if (resource.max_width || resource.max_height)
        hints = static_cast<GdkWindowHints>(hints | GDK_HINT_MAX_SIZE);
    if (resource.aspect_numerator) {
        geometry.min_aspect =
            static_cast<double>(resource.aspect_numerator) / resource.aspect_denominator;
        geometry.max_aspect = geometry.min_aspect;
        hints = static_cast<GdkWindowHints>(hints | GDK_HINT_ASPECT);
    }
    gtk_window_set_geometry_hints(GTK_WINDOW(resource.window), nullptr, &geometry, hints);
}

std::shared_ptr<GtkWebViewResource> webview(nk_handle handle) {
    return std::dynamic_pointer_cast<GtkWebViewResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::webview));
}

std::shared_ptr<GtkSurfaceResource> surface(nk_handle handle) {
    return std::dynamic_pointer_cast<GtkSurfaceResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::surface));
}

std::shared_ptr<GtkCursorResource> cursor(nk_handle handle) {
    return std::dynamic_pointer_cast<GtkCursorResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::cursor));
}

std::shared_ptr<GtkMonitorResource> monitor(nk_handle handle) {
    return std::dynamic_pointer_cast<GtkMonitorResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::monitor));
}

std::string monitor_name(GdkMonitor *native) {
    const char *manufacturer = gdk_monitor_get_manufacturer(native);
    const char *model = gdk_monitor_get_model(native);
    if (manufacturer && *manufacturer && model && *model)
        return std::string(manufacturer) + " " + model;
    if (model && *model)
        return model;
    if (manufacturer && *manufacturer)
        return manufacturer;
    return "Unknown monitor";
}

nk_handle register_monitor(GdkMonitor *native) {
    const auto found = monitor_handles.find(native);
    if (found != monitor_handles.end())
        return found->second;
    auto resource = std::make_shared<GtkMonitorResource>();
    resource->monitor = GDK_MONITOR(g_object_ref(native));
    resource->name = monitor_name(native);
    resource->handle =
        nk::core::handles().insert(nk::core::ResourceType::monitor, resource);
    if (resource->handle != NK_INVALID_HANDLE)
        monitor_handles.emplace(native, resource->handle);
    return resource->handle;
}

void on_monitor_added(GdkDisplay *, GdkMonitor *native, gpointer) {
    nk::core::callback_boundary([&] {
        const nk_handle handle = register_monitor(native);
        if (handle == NK_INVALID_HANDLE)
            return;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_MONITOR_CONNECTED;
        event.source = handle;
        nk::core::push_event(std::move(event));
    });
}

void on_monitor_removed(GdkDisplay *, GdkMonitor *native, gpointer) {
    nk::core::callback_boundary([&] {
        const auto found = monitor_handles.find(native);
        if (found == monitor_handles.end())
            return;
        const nk_handle handle = found->second;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_MONITOR_DISCONNECTED;
        event.source = handle;
        nk::core::push_event(std::move(event));
        monitor_handles.erase(found);
        nk::core::handles().erase(handle, nk::core::ResourceType::monitor);
    });
}

nk_result ensure_monitors() {
    if (!ensure_gtk())
        return NK_ERROR_UNSUPPORTED;
    if (monitor_display)
        return NK_OK;
    monitor_display = gdk_display_get_default();
    if (!monitor_display)
        return fail(NK_ERROR_UNSUPPORTED, "GTK has no display for monitor enumeration");
    const int count = gdk_display_get_n_monitors(monitor_display);
    for (int index = 0; index < count; ++index) {
        if (register_monitor(gdk_display_get_monitor(monitor_display, index)) ==
            NK_INVALID_HANDLE)
            return fail(NK_ERROR_OUT_OF_MEMORY, "monitor handle registry is full");
    }
    monitor_added_signal =
        g_signal_connect(monitor_display, "monitor-added", G_CALLBACK(on_monitor_added), nullptr);
    monitor_removed_signal =
        g_signal_connect(monitor_display, "monitor-removed", G_CALLBACK(on_monitor_removed),
                         nullptr);
    return NK_OK;
}

GdkCursor *blank_cursor(GdkDisplay *display) {
    static GdkDisplay *cached_display = nullptr;
    static GdkCursor *cached_cursor = nullptr;
    if (cached_display != display) {
        if (cached_cursor)
            g_object_unref(cached_cursor);
        cached_display = display;
        cached_cursor = gdk_cursor_new_from_name(display, "none");
        if (!cached_cursor)
            cached_cursor = gdk_cursor_new_for_display(display, GDK_BLANK_CURSOR);
    }
    return cached_cursor;
}

GdkCursor *effective_cursor(const GtkWindowResource &resource, GdkDisplay *display) {
    if (resource.cursor_mode == NK_CURSOR_MODE_HIDDEN)
        return blank_cursor(display);
    return resource.cursor ? resource.cursor->cursor : nullptr;
}

nk_result apply_cursor(GtkWindowResource &resource) {
    gtk_widget_realize(resource.window);
    GdkWindow *native = gtk_widget_get_window(resource.window);
    if (!native)
        return fail(NK_ERROR_UNKNOWN, "GTK window is not realized");
    GdkDisplay *display = gdk_window_get_display(native);
    gdk_window_set_cursor(native, effective_cursor(resource, display));
    return NK_OK;
}

nk_result apply_cursor_mode(GtkWindowResource &resource, nk_cursor_mode mode) {
    if (mode > NK_CURSOR_MODE_DISABLED)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid cursor mode");
    if (mode == NK_CURSOR_MODE_DISABLED)
        return fail(NK_ERROR_UNSUPPORTED,
                    "GTK does not provide portable disabled relative pointer motion");
    gtk_widget_realize(resource.window);
    GdkWindow *native = gtk_widget_get_window(resource.window);
    if (!native)
        return fail(NK_ERROR_UNKNOWN, "GTK window is not realized");
    GdkDisplay *display = gdk_window_get_display(native);
    GdkSeat *seat = gdk_display_get_default_seat(display);
    if (resource.pointer_grabbed) {
        gdk_seat_ungrab(seat);
        resource.pointer_grabbed = false;
    }
    const auto previous = resource.cursor_mode;
    resource.cursor_mode = mode;
    GdkCursor *native_cursor = effective_cursor(resource, display);
    if (mode == NK_CURSOR_MODE_CAPTURED) {
        const auto status =
            gdk_seat_grab(seat, native, GDK_SEAT_CAPABILITY_POINTER, TRUE, native_cursor, nullptr,
                          nullptr, nullptr);
        if (status != GDK_GRAB_SUCCESS) {
            resource.cursor_mode = previous;
            gdk_window_set_cursor(native, effective_cursor(resource, display));
            return fail(NK_ERROR_UNSUPPORTED, "GTK could not capture the pointer");
        }
        resource.pointer_grabbed = true;
    }
    gdk_window_set_cursor(native, native_cursor);
    return NK_OK;
}

std::vector<std::byte> dialog_paths_payload(const std::vector<std::string> &paths, bool accepted) {
    const auto offsets_offset = sizeof(nk_dialog_paths);
    const auto strings_offset = offsets_offset + paths.size() * sizeof(uint32_t);
    std::size_t total = strings_offset;
    for (const auto &path : paths)
        total += path.size() + 1;
    std::vector<std::byte> result(total);
    const nk_dialog_paths header{accepted ? 1u : 0u, static_cast<uint32_t>(paths.size()),
                                 static_cast<uint32_t>(offsets_offset),
                                 static_cast<uint32_t>(strings_offset)};
    std::memcpy(result.data(), &header, sizeof(header));
    std::size_t cursor = strings_offset;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto offset = static_cast<uint32_t>(cursor);
        std::memcpy(result.data() + offsets_offset + index * sizeof(offset), &offset,
                    sizeof(offset));
        std::memcpy(result.data() + cursor, paths[index].c_str(), paths[index].size() + 1);
        cursor += paths[index].size() + 1;
    }
    return result;
}

template <typename Header>
std::vector<std::byte> string_list_payload(Header header, const std::vector<std::string> &strings,
                                           uint32_t Header::*offset_member) {
    header.*offset_member = sizeof(Header);
    std::size_t total = sizeof(Header);
    for (const auto &string : strings)
        total += string.size() + 1;
    std::vector<std::byte> result(total);
    std::memcpy(result.data(), &header, sizeof(header));
    std::size_t cursor = sizeof(Header);
    for (const auto &string : strings) {
        std::memcpy(result.data() + cursor, string.c_str(), string.size() + 1);
        cursor += string.size() + 1;
    }
    return result;
}

void on_clipboard_text(GtkClipboard *, const gchar *text, gpointer data) {
    std::unique_ptr<ClipboardRequest> request(static_cast<ClipboardRequest *>(data));
    if (!nk::core::is_runtime_generation(request->generation))
        return;
    nk::core::callback_boundary([&] {
        nk::core::QueuedEvent event;
        event.kind = request->event_kind;
        event.request_id = request->request;
        event.data = bytes(text);
        nk::core::push_event(std::move(event));
    });
}

void on_clipboard_uris(GtkClipboard *, gchar **uris, gpointer data) {
    std::unique_ptr<ClipboardRequest> request(static_cast<ClipboardRequest *>(data));
    if (!nk::core::is_runtime_generation(request->generation))
        return;
    nk::core::callback_boundary([&] {
        std::vector<std::string> paths;
        for (gchar **uri = uris; uri && *uri; ++uri) {
            char *path = g_filename_from_uri(*uri, nullptr, nullptr);
            if (path) {
                paths.emplace_back(path);
                g_free(path);
            }
        }
        nk::core::QueuedEvent event;
        event.kind = request->event_kind;
        event.request_id = request->request;
        event.data_count = static_cast<uint32_t>(paths.size());
        nk_clipboard_files header{static_cast<uint32_t>(paths.size()), 0};
        event.data = string_list_payload(header, paths, &nk_clipboard_files::strings_offset);
        nk::core::push_event(std::move(event));
    });
}

void provide_clipboard_files(GtkClipboard *, GtkSelectionData *selection, guint, gpointer data) {
    auto *owner = static_cast<ClipboardFileOwner *>(data);
    gtk_selection_data_set_uris(selection, owner->pointers.data());
}

void clear_clipboard_files(GtkClipboard *, gpointer data) {
    clipboard_owned = false;
    delete static_cast<ClipboardFileOwner *>(data);
}

enum { drop_target_uri = 1, drop_target_text = 2 };

void on_drag_data_received(GtkWidget *, GdkDragContext *context, gint x, gint y,
                           GtkSelectionData *selection, guint info, guint time, gpointer data) {
    bool completed = false;
    nk::core::callback_boundary([&] {
        const auto *resource = static_cast<GtkWindowResource *>(data);
        std::vector<std::string> items;
        nk_event_kind kind = NK_EVENT_DROP_TEXT;
        if (info == drop_target_uri) {
            kind = NK_EVENT_DROP_FILES;
            gchar **uris = gtk_selection_data_get_uris(selection);
            for (gchar **uri = uris; uri && *uri; ++uri) {
                char *path = g_filename_from_uri(*uri, nullptr, nullptr);
                if (path) {
                    items.emplace_back(path);
                    g_free(path);
                }
            }
            g_strfreev(uris);
        } else {
            gchar *text = reinterpret_cast<gchar *>(gtk_selection_data_get_text(selection));
            if (text) {
                items.emplace_back(text);
                g_free(text);
            }
        }
        if (!items.empty()) {
            nk::core::QueuedEvent event;
            event.kind = kind;
            event.source = resource->handle;
            event.data_count = static_cast<uint32_t>(items.size());
            nk_drop_data header{x, y, static_cast<uint32_t>(items.size()), 0};
            event.data = string_list_payload(header, items, &nk_drop_data::strings_offset);
            nk::core::push_event(std::move(event));
        }
        gtk_drag_finish(context, !items.empty(), FALSE, time);
        completed = true;
    });
    if (!completed)
        gtk_drag_finish(context, FALSE, FALSE, time);
}

void dispose_dialog(DialogContext *context) {
    dialogs.erase(context->request);
    g_signal_handlers_disconnect_by_data(context->object, context);
    if (context->native_dialog)
        gtk_native_dialog_hide(GTK_NATIVE_DIALOG(context->object));
    else
        gtk_widget_hide(GTK_WIDGET(context->object));
    g_object_unref(context->object);
    delete context;
}

void emit_file_dialog_completion(DialogContext *context, int response) {
    const bool accepted = response == GTK_RESPONSE_ACCEPT || response == GTK_RESPONSE_OK;
    std::vector<std::string> paths;
    if (accepted) {
        GSList *filenames = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER(context->object));
        for (GSList *item = filenames; item; item = item->next) {
            paths.emplace_back(static_cast<const char *>(item->data));
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
    case GTK_RESPONSE_OK:
        return NK_MESSAGE_RESULT_OK;
    case GTK_RESPONSE_YES:
        return NK_MESSAGE_RESULT_YES;
    case GTK_RESPONSE_NO:
        return NK_MESSAGE_RESULT_NO;
    case GTK_RESPONSE_CANCEL:
    case GTK_RESPONSE_DELETE_EVENT:
        return NK_MESSAGE_RESULT_CANCEL;
    default:
        return NK_MESSAGE_RESULT_NONE;
    }
}

void on_dialog_response(GObject *, int response, gpointer data) {
    auto *context = static_cast<DialogContext *>(data);
    if (!nk::core::is_runtime_generation(context->generation)) {
        dispose_dialog(context);
        return;
    }
    nk::core::callback_boundary([&] {
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
    });
    dispose_dialog(context);
}

void cancel_dialog(DialogContext *context, bool emit_event) {
    if (emit_event) {
        nk::core::callback_boundary([&] {
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
        });
    }
    dispose_dialog(context);
}

void cancel_dialogs_for_parent(nk_handle parent, bool emit_event) {
    std::vector<nk_request_id> requests;
    for (const auto &[request, dialog] : dialogs) {
        if (dialog->parent == parent)
            requests.push_back(request);
    }
    for (const auto request : requests)
        cancel_dialog(dialogs.at(request), emit_event);
}

void add_filters(GtkFileChooser *chooser, const nk_file_dialog_options *options) {
    for (uint32_t index = 0; index < options->filter_count; ++index) {
        const auto &definition = options->filters[index];
        if (!definition.patterns)
            continue;
        GtkFileFilter *filter = gtk_file_filter_new();
        if (definition.name)
            gtk_file_filter_set_name(filter, definition.name);
        char **patterns = g_strsplit(definition.patterns, ";", -1);
        for (char **pattern = patterns; pattern && *pattern; ++pattern) {
            if (**pattern)
                gtk_file_filter_add_pattern(filter, *pattern);
        }
        g_strfreev(patterns);
        gtk_file_chooser_add_filter(chooser, filter);
    }
}

nk_result start_file_dialog(nk_handle parent_handle, const nk_file_dialog_options *options,
                            nk_request_id *out_request, uint32_t kind) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!options || options->struct_size < sizeof(*options) || !out_request ||
        (options->filter_count && !options->filters)) {
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid file dialog options");
    }
    std::shared_ptr<GtkWindowResource> parent;
    if (parent_handle != NK_INVALID_HANDLE) {
        parent = window(parent_handle);
        if (!parent)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
    }
    if (!ensure_gtk())
        return NK_ERROR_UNSUPPORTED;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
    if (kind == NK_DIALOG_SAVE_FILE)
        action = GTK_FILE_CHOOSER_ACTION_SAVE;
    if (kind == NK_DIALOG_SELECT_DIRECTORY)
        action = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER;
    GtkFileChooserNative *chooser = gtk_file_chooser_native_new(
        options->title ? options->title : "", parent ? GTK_WINDOW(parent->window) : nullptr, action,
        nullptr, nullptr);
    if (!chooser)
        return fail(NK_ERROR_UNKNOWN, "could not create native file dialog");
    std::unique_ptr<GObject, decltype(&g_object_unref)> chooser_owner(G_OBJECT(chooser),
                                                                      &g_object_unref);
    auto context = std::make_unique<DialogContext>();
    context->object = G_OBJECT(chooser);
    context->request = nk::core::next_request_id();
    context->generation = nk::core::runtime_generation();
    context->parent = parent_handle;
    context->kind = kind;
    context->native_dialog = true;
    auto *interface = GTK_FILE_CHOOSER(chooser);
    gtk_file_chooser_set_select_multiple(
        interface, kind == NK_DIALOG_OPEN_FILE && (options->flags & NK_DIALOG_ALLOW_MULTIPLE));
    gtk_file_chooser_set_do_overwrite_confirmation(
        interface, (options->flags & NK_DIALOG_CONFIRM_OVERWRITE) != 0);
    gtk_file_chooser_set_show_hidden(interface, (options->flags & NK_DIALOG_SHOW_HIDDEN) != 0);
    if (options->initial_path) {
        if (g_file_test(options->initial_path, G_FILE_TEST_IS_DIR))
            gtk_file_chooser_set_current_folder(interface, options->initial_path);
        else
            gtk_file_chooser_set_filename(interface, options->initial_path);
    }
    if (options->suggested_name && kind == NK_DIALOG_SAVE_FILE)
        gtk_file_chooser_set_current_name(interface, options->suggested_name);
    if (kind != NK_DIALOG_SELECT_DIRECTORY)
        add_filters(interface, options);
    dialogs.emplace(context->request, context.get());
    g_signal_connect(chooser, "response", G_CALLBACK(on_dialog_response), context.get());
    gtk_native_dialog_show(GTK_NATIVE_DIALOG(chooser));
    *out_request = context->request;
    chooser_owner.release();
    context.release();
    return NK_OK;
}

nk_result invalid_handle(const char *type) {
    (void)type;
    nk::core::set_error("invalid or stale resource handle");
    return NK_ERROR_INVALID_HANDLE;
}

nk_result copy_utf8(const char *value, char *buffer, uint32_t *inout_size) {
    if (!value || !inout_size)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid string output arguments");
    const auto length = std::strlen(value);
    if (length >= std::numeric_limits<uint32_t>::max())
        return fail(NK_ERROR_UNKNOWN, "system string is too large");
    const auto required = static_cast<uint32_t>(length + 1);
    const auto capacity = *inout_size;
    *inout_size = required;
    if (!buffer || capacity < required)
        return fail(NK_ERROR_BUFFER_TOO_SMALL, "output buffer is too small");
    std::memcpy(buffer, value, required);
    return NK_OK;
}

nk_result launch_uri(const char *uri) {
    GError *error = nullptr;
    if (g_app_info_launch_default_for_uri(uri, nullptr, &error))
        return NK_OK;
    nk::core::set_error(error && error->message ? error->message
                                                : "desktop could not launch the URI");
    if (error)
        g_error_free(error);
    return NK_ERROR_UNKNOWN;
}

nk_result open_path(const char *path) {
    if (!path || !*path)
        return fail(NK_ERROR_INVALID_ARGUMENT, "path must not be empty");
    char *absolute = g_canonicalize_filename(path, nullptr);
    GError *error = nullptr;
    char *uri = g_filename_to_uri(absolute, nullptr, &error);
    g_free(absolute);
    if (!uri) {
        nk::core::set_error(error && error->message ? error->message : "invalid file path");
        if (error)
            g_error_free(error);
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto result = launch_uri(uri);
    g_free(uri);
    return result;
}

const char *system_directory_path(nk_system_directory_kind kind) {
    switch (kind) {
    case NK_DIRECTORY_HOME:
        return g_get_home_dir();
    case NK_DIRECTORY_DESKTOP:
        return g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP);
    case NK_DIRECTORY_DOCUMENTS:
        return g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
    case NK_DIRECTORY_DOWNLOADS:
        return g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD);
    case NK_DIRECTORY_CACHE:
        return g_get_user_cache_dir();
    case NK_DIRECTORY_CONFIG:
        return g_get_user_config_dir();
    case NK_DIRECTORY_DATA:
        return g_get_user_data_dir();
    case NK_DIRECTORY_TEMP:
        return g_get_tmp_dir();
    default:
        return nullptr;
    }
}

void emit_notification(nk_event_kind kind, nk_request_id request, nk_result result = NK_OK,
                       const char *text = nullptr) noexcept {
    nk::core::callback_boundary([&] {
        nk::core::QueuedEvent event;
        event.kind = kind;
        event.request_id = request;
        event.result = result;
        if (text)
            event.data = bytes(text);
        nk::core::push_event(std::move(event));
    });
}

void close_server_notification(uint32_t server_id) {
    if (!notification_bus || !server_id)
        return;
    g_dbus_connection_call(notification_bus, "org.freedesktop.Notifications",
                           "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
                           "CloseNotification", g_variant_new("(u)", server_id), nullptr,
                           G_DBUS_CALL_FLAGS_NONE, -1, nullptr, nullptr, nullptr);
}

void on_notification_signal(GDBusConnection *, const gchar *, const gchar *, const gchar *,
                            const gchar *signal, GVariant *parameters, gpointer) {
    nk::core::callback_boundary([&] {
        uint32_t server_id = 0;
        if (std::strcmp(signal, "ActionInvoked") == 0) {
            const char *action = nullptr;
            g_variant_get(parameters, "(u&s)", &server_id, &action);
            const auto found = notification_ids.find(server_id);
            if (found != notification_ids.end())
                emit_notification(NK_EVENT_NOTIFICATION_ACTIVATED, found->second, NK_OK, action);
            return;
        }
        uint32_t reason = 0;
        g_variant_get(parameters, "(uu)", &server_id, &reason);
        const auto found = notification_ids.find(server_id);
        if (found == notification_ids.end())
            return;
        const auto request = found->second;
        notification_ids.erase(found);
        notifications.erase(request);
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_NOTIFICATION_DISMISSED;
        event.request_id = request;
        event.flags = reason;
        nk::core::push_event(std::move(event));
    });
}

bool ensure_notification_bus() {
    if (notification_bus)
        return true;
    GError *error = nullptr;
    notification_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!notification_bus) {
        nk::core::set_error(error && error->message
                                ? error->message
                                : "desktop notification service is unavailable");
        if (error)
            g_error_free(error);
        return false;
    }
    notification_action_subscription = g_dbus_connection_signal_subscribe(
        notification_bus, "org.freedesktop.Notifications", "org.freedesktop.Notifications",
        "ActionInvoked", "/org/freedesktop/Notifications", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        on_notification_signal, nullptr, nullptr);
    notification_closed_subscription = g_dbus_connection_signal_subscribe(
        notification_bus, "org.freedesktop.Notifications", "org.freedesktop.Notifications",
        "NotificationClosed", "/org/freedesktop/Notifications", nullptr, G_DBUS_SIGNAL_FLAGS_NONE,
        on_notification_signal, nullptr, nullptr);
    return true;
}

void on_notification_shown(GObject *object, GAsyncResult *result, gpointer data) {
    std::unique_ptr<NotificationContext> context(static_cast<NotificationContext *>(data));
    GError *error = nullptr;
    GVariant *reply = g_dbus_connection_call_finish(G_DBUS_CONNECTION(object), result, &error);
    uint32_t server_id = 0;
    bool completed = false;
    nk::core::callback_boundary([&] {
        const auto found = notifications.find(context->request);
        if (found == notifications.end() || !nk::core::is_runtime_generation(context->generation))
            return;
        if (!reply) {
            emit_notification(NK_EVENT_NOTIFICATION_FAILED, context->request, NK_ERROR_UNKNOWN,
                              error && error->message ? error->message
                                                      : "notification delivery failed");
            notifications.erase(found);
            completed = true;
            return;
        }
        g_variant_get(reply, "(u)", &server_id);
        if (!server_id) {
            emit_notification(NK_EVENT_NOTIFICATION_FAILED, context->request, NK_ERROR_UNKNOWN,
                              "notification service returned an invalid identifier");
            notifications.erase(found);
            completed = true;
            return;
        }
        if (found->second.canceled) {
            close_server_notification(server_id);
            notifications.erase(found);
            completed = true;
            return;
        }
        found->second.server_id = server_id;
        notification_ids.emplace(server_id, context->request);
        emit_notification(NK_EVENT_NOTIFICATION_DELIVERED, context->request);
        completed = true;
    });
    if (!completed) {
        if (server_id)
            close_server_notification(server_id);
        notifications.erase(context->request);
        if (nk::core::is_runtime_generation(context->generation))
            emit_notification(NK_EVENT_NOTIFICATION_FAILED, context->request,
                              NK_ERROR_OUT_OF_MEMORY,
                              "could not retain desktop notification state");
    }
    if (reply)
        g_variant_unref(reply);
    if (error)
        g_error_free(error);
}

} // namespace

namespace nk::backend {
void pump_events() noexcept {
    nk::linux_joystick::pump();
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
}

void shutdown() noexcept {
    nk::linux_joystick::shutdown();
    while (!dialogs.empty())
        cancel_dialog(dialogs.begin()->second, false);
    while (!navigation_decisions.empty()) {
        auto item = navigation_decisions.begin();
        webkit_policy_decision_ignore(item->second.decision);
        g_object_unref(item->second.decision);
        navigation_decisions.erase(item);
    }
    cancel_evaluations(NK_INVALID_HANDLE);
    for (const auto &[request, notification] : notifications) {
        (void)request;
        close_server_notification(notification.server_id);
    }
    notifications.clear();
    notification_ids.clear();
    if (notification_bus) {
        if (notification_action_subscription)
            g_dbus_connection_signal_unsubscribe(notification_bus,
                                                 notification_action_subscription);
        if (notification_closed_subscription)
            g_dbus_connection_signal_unsubscribe(notification_bus,
                                                 notification_closed_subscription);
        g_object_unref(notification_bus);
        notification_bus = nullptr;
        notification_action_subscription = 0;
        notification_closed_subscription = 0;
    }
    if (gtk_initialized && clipboard_owned) {
        GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
        gtk_clipboard_set_can_store(clipboard, nullptr, 0);
        gtk_clipboard_store(clipboard);
        gtk_clipboard_clear(clipboard);
        clipboard_owned = false;
    }
    if (monitor_display) {
        if (monitor_added_signal)
            g_signal_handler_disconnect(monitor_display, monitor_added_signal);
        if (monitor_removed_signal)
            g_signal_handler_disconnect(monitor_display, monitor_removed_signal);
        monitor_handles.clear();
        monitor_display = nullptr;
        monitor_added_signal = 0;
        monitor_removed_signal = 0;
    }
    nk::core::handles().clear();
    pump_events();
}
} // namespace nk::backend

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_WINDOW | NK_CAP_WEBVIEW | NK_CAP_FILE_DIALOG | NK_CAP_CLIPBOARD |
           NK_CAP_DRAG_DROP | NK_CAP_SHELL | NK_CAP_SYSTEM_APPEARANCE |
           NK_CAP_EXPORT_NATIVE_WINDOW | NK_CAP_NOTIFICATION | NK_CAP_INPUT |
           NK_CAP_OPENGL_SURFACE | NK_CAP_OPENGL_ES_SURFACE | NK_CAP_CURSOR |
           NK_CAP_POINTER_CAPTURE | NK_CAP_WINDOW_GEOMETRY | NK_CAP_WINDOW_STYLING |
           NK_CAP_MONITOR | NK_CAP_MONITOR_FULLSCREEN | NK_CAP_JOYSTICK | NK_CAP_RESOURCE_IO |
           NK_CAP_VULKAN_SURFACE;
}

nk_result NK_CALL nk_window_create(const nk_window_options *options, nk_handle *out_window) {
    return nk::core::result_boundary("unexpected error while creating window", [&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!options || options->struct_size < sizeof(*options) || !out_window ||
            options->width <= 0 || options->height <= 0 || options->kind > NK_WINDOW_UTILITY ||
            ((options->flags & NK_WINDOW_MODAL) && !options->owner)) {
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid window options");
        }
        *out_window = NK_INVALID_HANDLE;
        if (!ensure_gtk())
            return NK_ERROR_UNSUPPORTED;
        if (const auto result = ensure_monitors(); result != NK_OK)
            return result;
        auto owner = options->owner ? window(options->owner) : nullptr;
        if (options->owner && !owner)
            return invalid_handle("owner window");
        if (owner)
            owner->owned_windows.reserve(owner->owned_windows.size() + 1);
        auto resource = std::make_shared<GtkWindowResource>();
        resource->owner = options->owner;
        resource->generation = nk::core::runtime_generation();
        resource->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        resource->im_context = gtk_im_multicontext_new();
        g_object_add_weak_pointer(G_OBJECT(resource->window),
                                  reinterpret_cast<gpointer *>(&resource->window));
        resource->container = nk_fixed_new();
        gtk_container_add(GTK_CONTAINER(resource->window), resource->container);
        gtk_window_set_default_size(GTK_WINDOW(resource->window), options->width, options->height);
        apply_geometry_hints(*resource);
        gtk_window_set_resizable(GTK_WINDOW(resource->window),
                                 (options->flags & NK_WINDOW_RESIZABLE) != 0);
        gtk_window_set_decorated(GTK_WINDOW(resource->window),
                                 (options->flags & NK_WINDOW_BORDERLESS) == 0);
        gtk_window_set_modal(GTK_WINDOW(resource->window), (options->flags & NK_WINDOW_MODAL) != 0);
        if (options->kind == NK_WINDOW_UTILITY)
            gtk_window_set_type_hint(GTK_WINDOW(resource->window), GDK_WINDOW_TYPE_HINT_UTILITY);
        if (owner)
            gtk_window_set_transient_for(GTK_WINDOW(resource->window), GTK_WINDOW(owner->window));
        gtk_window_set_title(GTK_WINDOW(resource->window), options->title ? options->title : "");
        resource->handle = nk::core::handles().insert(nk::core::ResourceType::window, resource);
        if (resource->handle == NK_INVALID_HANDLE) {
            gtk_widget_destroy(resource->window);
            return fail(NK_ERROR_OUT_OF_MEMORY, "window handle registry is full");
        }
        if (owner)
            owner->owned_windows.push_back(resource->handle);
        g_signal_connect(resource->window, "delete-event", G_CALLBACK(on_window_delete),
                         resource.get());
        g_signal_connect(resource->window, "configure-event", G_CALLBACK(on_window_configure),
                         resource.get());
        g_signal_connect(resource->window, "notify::scale-factor", G_CALLBACK(on_window_scale),
                         resource.get());
        g_signal_connect(resource->window, "window-state-event", G_CALLBACK(on_window_state),
                         resource.get());
        g_signal_connect(resource->window, "map", G_CALLBACK(on_window_map),
                         resource.get());
        g_signal_connect(resource->window, "unmap", G_CALLBACK(on_window_unmap),
                         resource.get());
        g_signal_connect(resource->window, "realize", G_CALLBACK(on_window_realize),
                         resource.get());
        g_signal_connect(resource->window, "focus-in-event", G_CALLBACK(on_input_focus),
                         resource.get());
        g_signal_connect(resource->window, "focus-out-event", G_CALLBACK(on_input_focus),
                         resource.get());
        g_signal_connect(resource->im_context, "commit", G_CALLBACK(on_text_commit),
                         resource.get());
        gtk_widget_add_events(resource->window,
                              GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK |
                                  GDK_POINTER_MOTION_MASK | GDK_BUTTON_PRESS_MASK |
                                  GDK_BUTTON_RELEASE_MASK | GDK_SCROLL_MASK |
                                  GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
        g_signal_connect(resource->window, "key-press-event", G_CALLBACK(on_key), resource.get());
        g_signal_connect(resource->window, "key-release-event", G_CALLBACK(on_key), resource.get());
        g_signal_connect(resource->window, "motion-notify-event", G_CALLBACK(on_pointer_move),
                         resource.get());
        g_signal_connect(resource->window, "button-press-event", G_CALLBACK(on_pointer_button),
                         resource.get());
        g_signal_connect(resource->window, "button-release-event", G_CALLBACK(on_pointer_button),
                         resource.get());
        g_signal_connect(resource->window, "scroll-event", G_CALLBACK(on_pointer_scroll),
                         resource.get());
        g_signal_connect(resource->window, "enter-notify-event", G_CALLBACK(on_pointer_crossing),
                         resource.get());
        g_signal_connect(resource->window, "leave-notify-event", G_CALLBACK(on_pointer_crossing),
                         resource.get());
        if ((options->flags & NK_WINDOW_HIDDEN) == 0)
            gtk_widget_show_all(resource->window);
        *out_window = resource->handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_window_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    const auto owned_windows = resource->owned_windows;
    for (const auto owned : owned_windows)
        nk_window_destroy(owned);
    cancel_dialogs_for_parent(handle, true);
    const auto children = resource->children;
    for (const auto child : children)
        nk_webview_destroy(child);
    const auto surfaces = resource->surfaces;
    for (auto child = surfaces.rbegin(); child != surfaces.rend(); ++child)
        nk_surface_destroy(*child);
    if (resource->pointer_grabbed) {
        GdkDisplay *display = gtk_widget_get_display(resource->window);
        gdk_seat_ungrab(gdk_display_get_default_seat(display));
        resource->pointer_grabbed = false;
    }
    g_signal_handlers_disconnect_by_data(resource->window, resource.get());
    gtk_widget_destroy(resource->window);
    resource->window = nullptr;
    resource->container = nullptr;
    if (auto owner = window(resource->owner)) {
        auto &owned = owner->owned_windows;
        owned.erase(std::remove(owned.begin(), owned.end(), handle), owned.end());
    }
    nk::core::handles().erase(handle, nk::core::ResourceType::window);
    return NK_OK;
}

nk_result NK_CALL nk_window_show(nk_handle handle, uint32_t visible) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    visible ? gtk_widget_show_all(resource->window) : gtk_widget_hide(resource->window);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_title(nk_handle handle, const char *title) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    gtk_window_set_title(GTK_WINDOW(resource->window), title ? title : "");
    return NK_OK;
}

nk_result NK_CALL nk_window_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                       int32_t height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (width <= 0 || height <= 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "window dimensions must be positive");
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    gtk_window_move(GTK_WINDOW(resource->window), x, y);
    gtk_window_resize(GTK_WINDOW(resource->window), width, height);
    return NK_OK;
}

nk_result NK_CALL nk_window_get_scale(nk_handle handle, float *out_scale) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_scale)
        return fail(NK_ERROR_INVALID_ARGUMENT, "scale output must not be null");
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    *out_scale = static_cast<float>(gtk_widget_get_scale_factor(resource->window));
    return NK_OK;
}

nk_result NK_CALL nk_window_get_content_scale(nk_handle handle,
                                              nk_window_content_scale *out_scale) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_scale || out_scale->struct_size < sizeof(*out_scale))
        return fail(NK_ERROR_INVALID_ARGUMENT, "content scale output is missing or too small");
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    const float scale = static_cast<float>(gtk_widget_get_scale_factor(resource->window));
    const auto size = out_scale->struct_size;
    *out_scale = {};
    out_scale->struct_size = size;
    out_scale->x = scale;
    out_scale->y = scale;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_position(nk_handle handle, int32_t *out_x, int32_t *out_y) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_x || !out_y)
        return fail(NK_ERROR_INVALID_ARGUMENT, "window position outputs must not be null");
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(resource->window)))
        return fail(NK_ERROR_UNSUPPORTED, "Wayland does not expose global window positions");
#endif
    gint x = 0;
    gint y = 0;
    gtk_window_get_position(GTK_WINDOW(resource->window), &x, &y);
    *out_x = x;
    *out_y = y;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_size(nk_handle handle, int32_t *out_width,
                                     int32_t *out_height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_width || !out_height)
        return fail(NK_ERROR_INVALID_ARGUMENT, "window size outputs must not be null");
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    gtk_window_get_size(GTK_WINDOW(resource->window), out_width, out_height);
    return NK_OK;
}

nk_result NK_CALL nk_window_get_framebuffer_size(nk_handle handle, int32_t *out_width,
                                                 int32_t *out_height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_width || !out_height)
        return fail(NK_ERROR_INVALID_ARGUMENT, "framebuffer size outputs must not be null");
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    gint width = 0;
    gint height = 0;
    gtk_window_get_size(GTK_WINDOW(resource->window), &width, &height);
    const int scale = gtk_widget_get_scale_factor(resource->window);
    *out_width = width * scale;
    *out_height = height * scale;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_frame_extents(nk_handle handle,
                                              nk_window_frame_extents *out_extents) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_extents || out_extents->struct_size < sizeof(*out_extents))
        return fail(NK_ERROR_INVALID_ARGUMENT,
                    "window frame extents output is missing or too small");
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(resource->window)))
        return fail(NK_ERROR_UNSUPPORTED, "Wayland does not expose window frame extents");
#endif
    gtk_widget_realize(resource->window);
    GdkWindow *native = gtk_widget_get_window(resource->window);
    if (!native)
        return fail(NK_ERROR_UNKNOWN, "GTK window has no native surface");
    GdkRectangle frame{};
    gdk_window_get_frame_extents(native, &frame);
    gint origin_x = 0;
    gint origin_y = 0;
    gdk_window_get_origin(native, &origin_x, &origin_y);
    const int width = gdk_window_get_width(native);
    const int height = gdk_window_get_height(native);
    const auto size = out_extents->struct_size;
    *out_extents = {};
    out_extents->struct_size = size;
    out_extents->left = std::max(0, origin_x - frame.x);
    out_extents->top = std::max(0, origin_y - frame.y);
    out_extents->right = std::max(0, frame.width - width - out_extents->left);
    out_extents->bottom = std::max(0, frame.height - height - out_extents->top);
    return NK_OK;
}

nk_result NK_CALL nk_window_get_state(nk_handle handle, nk_window_state *out) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out || out->struct_size < sizeof(*out))
        return fail(NK_ERROR_INVALID_ARGUMENT, "window state output is missing or too small");
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    const auto size = out->struct_size;
    *out = {};
    out->struct_size = size;
    out->flags = resource->state_flags;
    return NK_OK;
}

nk_result NK_CALL nk_window_is_focused(nk_handle handle, uint32_t *out_focused) {
    if (!out_focused)
        return fail(NK_ERROR_INVALID_ARGUMENT, "focus output must not be null");
    nk_window_state state{sizeof(state), 0, {0, 0}};
    const auto result = nk_window_get_state(handle, &state);
    if (result == NK_OK)
        *out_focused = (state.flags & NK_WINDOW_STATE_ACTIVE) ? 1u : 0u;
    return result;
}

nk_result NK_CALL nk_window_is_visible(nk_handle handle, uint32_t *out_visible) {
    if (!out_visible)
        return fail(NK_ERROR_INVALID_ARGUMENT, "visibility output must not be null");
    nk_window_state state{sizeof(state), 0, {0, 0}};
    const auto result = nk_window_get_state(handle, &state);
    if (result == NK_OK)
        *out_visible = (state.flags & NK_WINDOW_STATE_VISIBLE) ? 1u : 0u;
    return result;
}

nk_result NK_CALL nk_key_get_state(nk_handle handle, nk_key key, nk_input_action *out_action) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_action || key == NK_KEY_UNKNOWN || key > NK_KEY_LAST)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid key state query");
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    *out_action = resource->keys[key];
    return NK_OK;
}

nk_result NK_CALL nk_pointer_button_get_state(nk_handle handle, nk_pointer_button button,
                                               nk_input_action *out_action) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_action || button > NK_POINTER_BUTTON_LAST)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid pointer button state query");
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    *out_action = resource->buttons[button];
    return NK_OK;
}

nk_result NK_CALL nk_pointer_get_position(nk_handle handle, double *out_x, double *out_y) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_x || !out_y)
        return fail(NK_ERROR_INVALID_ARGUMENT, "pointer position outputs must not be null");
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    *out_x = resource->pointer_x;
    *out_y = resource->pointer_y;
    return NK_OK;
}

nk_result NK_CALL nk_cursor_create_standard(nk_cursor_shape shape, nk_handle *out_cursor) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_cursor)
        return fail(NK_ERROR_INVALID_ARGUMENT, "cursor output must not be null");
    *out_cursor = NK_INVALID_HANDLE;
    const char *name = nullptr;
    switch (shape) {
    case NK_CURSOR_ARROW: name = "default"; break;
    case NK_CURSOR_IBEAM: name = "text"; break;
    case NK_CURSOR_CROSSHAIR: name = "crosshair"; break;
    case NK_CURSOR_HAND: name = "pointer"; break;
    case NK_CURSOR_HORIZONTAL_RESIZE: name = "ew-resize"; break;
    case NK_CURSOR_VERTICAL_RESIZE: name = "ns-resize"; break;
    case NK_CURSOR_NWSE_RESIZE: name = "nwse-resize"; break;
    case NK_CURSOR_NESW_RESIZE: name = "nesw-resize"; break;
    case NK_CURSOR_MOVE: name = "move"; break;
    case NK_CURSOR_NOT_ALLOWED: name = "not-allowed"; break;
    default: return fail(NK_ERROR_INVALID_ARGUMENT, "invalid standard cursor shape");
    }
    if (!ensure_gtk())
        return NK_ERROR_UNSUPPORTED;
    auto resource = std::make_shared<GtkCursorResource>();
    resource->cursor = gdk_cursor_new_from_name(gdk_display_get_default(), name);
    if (!resource->cursor)
        return fail(NK_ERROR_UNSUPPORTED, "cursor shape is unavailable");
    resource->handle =
        nk::core::handles().insert(nk::core::ResourceType::cursor, resource);
    if (resource->handle == NK_INVALID_HANDLE)
        return fail(NK_ERROR_OUT_OF_MEMORY, "cursor handle registry is full");
    *out_cursor = resource->handle;
    return NK_OK;
}

nk_result NK_CALL nk_cursor_create_custom(const nk_cursor_image *image, nk_handle *out_cursor) {
    return nk::core::result_boundary("unexpected error while creating cursor", [&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!image || image->struct_size < sizeof(*image) || !out_cursor || !image->rgba ||
            image->width <= 0 || image->height <= 0 || image->width > INT_MAX / 4 ||
            image->stride < image->width * 4 || image->hotspot_x < 0 || image->hotspot_y < 0 ||
            image->hotspot_x >= image->width || image->hotspot_y >= image->height)
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid custom cursor image");
        *out_cursor = NK_INVALID_HANDLE;
        if (!ensure_gtk())
            return NK_ERROR_UNSUPPORTED;
        GdkPixbuf *pixbuf =
            gdk_pixbuf_new(GDK_COLORSPACE_RGB, TRUE, 8, image->width, image->height);
        if (!pixbuf)
            return fail(NK_ERROR_OUT_OF_MEMORY, "could not allocate custom cursor pixels");
        const auto *source = static_cast<const guchar *>(image->rgba);
        guchar *destination = gdk_pixbuf_get_pixels(pixbuf);
        const int destination_stride = gdk_pixbuf_get_rowstride(pixbuf);
        for (int y = 0; y < image->height; ++y)
            std::memcpy(destination + y * destination_stride, source + y * image->stride,
                        static_cast<std::size_t>(image->width) * 4);
        auto resource = std::make_shared<GtkCursorResource>();
        resource->cursor =
            gdk_cursor_new_from_pixbuf(gdk_display_get_default(), pixbuf, image->hotspot_x,
                                       image->hotspot_y);
        g_object_unref(pixbuf);
        if (!resource->cursor)
            return fail(NK_ERROR_UNSUPPORTED, "GTK could not create the custom cursor");
        resource->handle =
            nk::core::handles().insert(nk::core::ResourceType::cursor, resource);
        if (resource->handle == NK_INVALID_HANDLE)
            return fail(NK_ERROR_OUT_OF_MEMORY, "cursor handle registry is full");
        *out_cursor = resource->handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_cursor_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!cursor(handle))
        return invalid_handle("cursor");
    nk::core::handles().erase(handle, nk::core::ResourceType::cursor);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_cursor(nk_handle window_handle, nk_handle cursor_handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(window_handle);
    if (!resource)
        return invalid_handle("window");
    auto selected = cursor_handle ? cursor(cursor_handle) : nullptr;
    if (cursor_handle && !selected)
        return invalid_handle("cursor");
    resource->cursor = std::move(selected);
    return resource->cursor_mode == NK_CURSOR_MODE_CAPTURED
               ? apply_cursor_mode(*resource, resource->cursor_mode)
               : apply_cursor(*resource);
}

nk_result NK_CALL nk_window_set_cursor_mode(nk_handle handle, nk_cursor_mode mode) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    return apply_cursor_mode(*resource, mode);
}

nk_result NK_CALL nk_window_get_cursor_mode(nk_handle handle, nk_cursor_mode *out_mode) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_mode)
        return fail(NK_ERROR_INVALID_ARGUMENT, "cursor mode output must not be null");
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    *out_mode = resource->cursor_mode;
    return NK_OK;
}

uint32_t NK_CALL nk_raw_pointer_motion_supported(void) { return 0; }

nk_result NK_CALL nk_window_minimize(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return invalid_handle("window");
    gtk_window_iconify(GTK_WINDOW(w->window));
    return NK_OK;
}
nk_result NK_CALL nk_window_maximize(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return invalid_handle("window");
    gtk_window_maximize(GTK_WINDOW(w->window));
    return NK_OK;
}
nk_result NK_CALL nk_window_restore(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return invalid_handle("window");
    gtk_window_deiconify(GTK_WINDOW(w->window));
    gtk_window_unmaximize(GTK_WINDOW(w->window));
    gtk_window_unfullscreen(GTK_WINDOW(w->window));
    return NK_OK;
}
nk_result NK_CALL nk_window_activate(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return invalid_handle("window");
    gtk_window_present(GTK_WINDOW(w->window));
    return NK_OK;
}
nk_result NK_CALL nk_window_set_fullscreen(nk_handle h, uint32_t enabled) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return invalid_handle("window");
    enabled ? gtk_window_fullscreen(GTK_WINDOW(w->window))
            : gtk_window_unfullscreen(GTK_WINDOW(w->window));
    return NK_OK;
}
nk_result NK_CALL nk_window_request_attention(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return invalid_handle("window");
    if ((w->state_flags & (NK_WINDOW_STATE_ACTIVE | NK_WINDOW_STATE_VISIBLE)) !=
        (NK_WINDOW_STATE_ACTIVE | NK_WINDOW_STATE_VISIBLE)) {
        gtk_window_set_urgency_hint(GTK_WINDOW(w->window), TRUE);
        update_window_state(*w, w->state_flags | NK_WINDOW_STATE_ATTENTION_REQUESTED);
    }
    return NK_OK;
}
nk_result NK_CALL nk_window_set_size_limits(nk_handle h, const nk_window_size_limits *limits) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!limits || limits->struct_size < sizeof(*limits) || limits->min_width < 0 ||
        limits->min_height < 0 || limits->max_width < 0 || limits->max_height < 0 ||
        (limits->max_width && limits->max_width < limits->min_width) ||
        (limits->max_height && limits->max_height < limits->min_height))
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid window size limits");
    auto w = window(h);
    if (!w)
        return invalid_handle("window");
    w->min_width = limits->min_width;
    w->min_height = limits->min_height;
    w->max_width = limits->max_width;
    w->max_height = limits->max_height;
    apply_geometry_hints(*w);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_aspect_ratio(nk_handle h, int32_t numerator,
                                             int32_t denominator) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if ((numerator == 0) != (denominator == 0) || numerator < 0 || denominator < 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid window aspect ratio");
    auto resource = window(h);
    if (!resource)
        return invalid_handle("window");
    resource->aspect_numerator = numerator;
    resource->aspect_denominator = denominator;
    apply_geometry_hints(*resource);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_resizable(nk_handle h, uint32_t enabled) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(h);
    if (!resource)
        return invalid_handle("window");
    gtk_window_set_resizable(GTK_WINDOW(resource->window), enabled != 0);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_decorated(nk_handle h, uint32_t enabled) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(h);
    if (!resource)
        return invalid_handle("window");
    gtk_window_set_decorated(GTK_WINDOW(resource->window), enabled != 0);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_floating(nk_handle h, uint32_t enabled) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(h);
    if (!resource)
        return invalid_handle("window");
    gtk_window_set_keep_above(GTK_WINDOW(resource->window), enabled != 0);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_opacity(nk_handle h, float opacity) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!(opacity >= 0.0f && opacity <= 1.0f))
        return fail(NK_ERROR_INVALID_ARGUMENT, "window opacity must be between zero and one");
    auto resource = window(h);
    if (!resource)
        return invalid_handle("window");
    gtk_widget_set_opacity(resource->window, opacity);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_mouse_passthrough(nk_handle h, uint32_t enabled) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(h);
    if (!resource)
        return invalid_handle("window");
    gtk_widget_realize(resource->window);
    GdkWindow *native = gtk_widget_get_window(resource->window);
    if (!native)
        return fail(NK_ERROR_UNKNOWN, "GTK window has no native surface");
    gdk_window_set_pass_through(native, enabled != 0);
    return NK_OK;
}

nk_result NK_CALL nk_window_get_hovered(nk_handle h, uint32_t *out_hovered) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_hovered)
        return fail(NK_ERROR_INVALID_ARGUMENT, "hover output must not be null");
    auto resource = window(h);
    if (!resource)
        return invalid_handle("window");
    *out_hovered = resource->hovered ? 1u : 0u;
    return NK_OK;
}

nk_result NK_CALL nk_monitor_list(nk_handle *monitors, uint32_t *inout_count) {
    return nk::core::result_boundary("unexpected error while enumerating monitors",
                                     [&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!inout_count)
            return fail(NK_ERROR_INVALID_ARGUMENT, "monitor count must not be null");
        if (const auto result = ensure_monitors(); result != NK_OK)
            return result;
        const uint32_t required =
            static_cast<uint32_t>(gdk_display_get_n_monitors(monitor_display));
        const uint32_t capacity = *inout_count;
        *inout_count = required;
        if (!monitors || capacity < required)
            return required ? fail(NK_ERROR_BUFFER_TOO_SMALL,
                                   "monitor handle buffer is too small")
                            : NK_OK;
        for (uint32_t index = 0; index < required; ++index) {
            GdkMonitor *native =
                gdk_display_get_monitor(monitor_display, static_cast<int>(index));
            const auto found = monitor_handles.find(native);
            if (found == monitor_handles.end())
                return fail(NK_ERROR_UNKNOWN, "monitor registry is inconsistent");
            monitors[index] = found->second;
        }
        return NK_OK;
    });
}

nk_result NK_CALL nk_monitor_get_primary(nk_handle *out_monitor) {
    return nk::core::result_boundary("unexpected error while finding primary monitor",
                                     [&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!out_monitor)
            return fail(NK_ERROR_INVALID_ARGUMENT, "monitor output must not be null");
        *out_monitor = NK_INVALID_HANDLE;
        if (const auto result = ensure_monitors(); result != NK_OK)
            return result;
        GdkMonitor *native = gdk_display_get_primary_monitor(monitor_display);
        if (!native && gdk_display_get_n_monitors(monitor_display) > 0)
            native = gdk_display_get_monitor(monitor_display, 0);
        if (!native)
            return fail(NK_ERROR_UNSUPPORTED, "GTK reports no connected monitors");
        const auto found = monitor_handles.find(native);
        if (found == monitor_handles.end())
            return fail(NK_ERROR_UNKNOWN, "primary monitor is not registered");
        *out_monitor = found->second;
        return NK_OK;
    });
}

nk_result NK_CALL nk_monitor_get_name(nk_handle handle, char *buffer, uint32_t *inout_size) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = monitor(handle);
    if (!resource)
        return invalid_handle("monitor");
    return copy_utf8(resource->name.c_str(), buffer, inout_size);
}

nk_result NK_CALL nk_monitor_get_geometry(nk_handle handle,
                                          nk_monitor_geometry *out_geometry) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_geometry || out_geometry->struct_size < sizeof(*out_geometry))
        return fail(NK_ERROR_INVALID_ARGUMENT,
                    "monitor geometry output is missing or too small");
    auto resource = monitor(handle);
    if (!resource)
        return invalid_handle("monitor");
    GdkRectangle geometry{};
    GdkRectangle workarea{};
    gdk_monitor_get_geometry(resource->monitor, &geometry);
    gdk_monitor_get_workarea(resource->monitor, &workarea);
    const auto size = out_geometry->struct_size;
    *out_geometry = {};
    out_geometry->struct_size = size;
    out_geometry->x = geometry.x;
    out_geometry->y = geometry.y;
    out_geometry->width = geometry.width;
    out_geometry->height = geometry.height;
    out_geometry->work_x = workarea.x;
    out_geometry->work_y = workarea.y;
    out_geometry->work_width = workarea.width;
    out_geometry->work_height = workarea.height;
    out_geometry->width_mm = gdk_monitor_get_width_mm(resource->monitor);
    out_geometry->height_mm = gdk_monitor_get_height_mm(resource->monitor);
    const float scale = static_cast<float>(gdk_monitor_get_scale_factor(resource->monitor));
    out_geometry->scale_x = scale;
    out_geometry->scale_y = scale;
    return NK_OK;
}

nk_result NK_CALL nk_monitor_get_current_mode(nk_handle handle, nk_video_mode *out_mode) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_mode || out_mode->struct_size < sizeof(*out_mode))
        return fail(NK_ERROR_INVALID_ARGUMENT, "video mode output is missing or too small");
    auto resource = monitor(handle);
    if (!resource)
        return invalid_handle("monitor");
    GdkRectangle geometry{};
    gdk_monitor_get_geometry(resource->monitor, &geometry);
    const int scale = gdk_monitor_get_scale_factor(resource->monitor);
    const auto size = out_mode->struct_size;
    *out_mode = {};
    out_mode->struct_size = size;
    out_mode->width = geometry.width * scale;
    out_mode->height = geometry.height * scale;
    const int refresh_rate = gdk_monitor_get_refresh_rate(resource->monitor);
    out_mode->refresh_rate = refresh_rate > 0 ? refresh_rate / 1000.0 : 0.0;
    return NK_OK;
}

nk_result NK_CALL nk_monitor_get_modes(nk_handle handle, nk_video_mode *modes,
                                       uint32_t *inout_count) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!inout_count)
        return fail(NK_ERROR_INVALID_ARGUMENT, "video mode count must not be null");
    if (!monitor(handle))
        return invalid_handle("monitor");
    const uint32_t capacity = *inout_count;
    *inout_count = 1;
    if (!modes || capacity < 1)
        return fail(NK_ERROR_BUFFER_TOO_SMALL, "video mode buffer is too small");
    return nk_monitor_get_current_mode(handle, &modes[0]);
}

nk_result NK_CALL nk_window_set_fullscreen_monitor(nk_handle window_handle,
                                                   nk_handle monitor_handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto window_resource = window(window_handle);
    if (!window_resource)
        return invalid_handle("window");
    if (monitor_handle == NK_INVALID_HANDLE) {
        gtk_window_unfullscreen(GTK_WINDOW(window_resource->window));
        return NK_OK;
    }
    auto monitor_resource = monitor(monitor_handle);
    if (!monitor_resource)
        return invalid_handle("monitor");
    gtk_widget_realize(window_resource->window);
    GdkWindow *native = gtk_widget_get_window(window_resource->window);
    if (!native)
        return fail(NK_ERROR_UNKNOWN, "GTK window has no native surface");
    GdkDisplay *display = gdk_window_get_display(native);
    int monitor_index = -1;
    const int monitor_count = gdk_display_get_n_monitors(display);
    for (int index = 0; index < monitor_count; ++index) {
        if (gdk_display_get_monitor(display, index) == monitor_resource->monitor) {
            monitor_index = index;
            break;
        }
    }
    if (monitor_index < 0)
        return fail(NK_ERROR_INVALID_HANDLE, "monitor is not connected to this display");
    gdk_window_fullscreen_on_monitor(native, monitor_index);
    return NK_OK;
}

nk_result NK_CALL nk_window_get_native(nk_handle handle, nk_native_window *out_native) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_native || out_native->struct_size < sizeof(*out_native))
        return fail(NK_ERROR_INVALID_ARGUMENT, "native window output is missing or too small");
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    gtk_widget_realize(resource->window);
    GdkWindow *native = gtk_widget_get_window(resource->window);
    if (!native)
        return fail(NK_ERROR_UNKNOWN, "GTK window has no native surface");
    const auto size = out_native->struct_size;
    *out_native = {};
    out_native->struct_size = size;
    GdkDisplay *display = gdk_window_get_display(native);
#ifdef GDK_WINDOWING_X11
    if (GDK_IS_X11_WINDOW(native)) {
        out_native->kind = NK_NATIVE_WINDOW_X11;
        out_native->display = reinterpret_cast<uintptr_t>(gdk_x11_display_get_xdisplay(display));
        out_native->window = static_cast<uintptr_t>(gdk_x11_window_get_xid(native));
        return NK_OK;
    }
#endif
#ifdef GDK_WINDOWING_WAYLAND
    if (GDK_IS_WAYLAND_WINDOW(native)) {
        out_native->kind = NK_NATIVE_WINDOW_WAYLAND;
        out_native->display =
            reinterpret_cast<uintptr_t>(gdk_wayland_display_get_wl_display(display));
        out_native->window = reinterpret_cast<uintptr_t>(gdk_wayland_window_get_wl_surface(native));
        return NK_OK;
    }
#endif
    return fail(NK_ERROR_UNSUPPORTED, "GTK display backend is not interoperable");
}

nk_result NK_CALL nk_window_wrap_native(const nk_native_window *native, nk_handle *out_window) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!native || native->struct_size < sizeof(*native) || !out_window || !native->window)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid native window descriptor");
    *out_window = NK_INVALID_HANDLE;
    return fail(NK_ERROR_UNSUPPORTED,
                "wrapping caller-owned windows is not safe in the GTK backend yet");
}

nk_result NK_CALL nk_surface_create(nk_handle parent_handle, const nk_surface_options *options,
                                    nk_handle *out_surface) {
    return nk::core::result_boundary("unexpected error while creating graphics surface",
                                     [&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!options || options->struct_size < sizeof(*options) || !out_surface ||
            options->width <= 0 || options->height <= 0 ||
            (options->api != NK_GRAPHICS_OPENGL &&
             options->api != NK_GRAPHICS_OPENGL_ES))
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid graphics surface options");
        *out_surface = NK_INVALID_HANDLE;
        auto parent = window(parent_handle);
        if (!parent)
            return invalid_handle("parent window");
        auto shared = options->share_surface ? surface(options->share_surface) : nullptr;
        if (options->share_surface && !shared)
            return invalid_handle("shared graphics surface");
        if (shared &&
            (shared->api != options->api || shared->major_version != options->major_version ||
             shared->minor_version != options->minor_version ||
             shared->flags !=
                 (options->flags &
                  (NK_SURFACE_DEBUG_CONTEXT | NK_SURFACE_FORWARD_COMPATIBLE))))
            return fail(NK_ERROR_INVALID_ARGUMENT,
                        "shared surfaces must use identical context options");
        parent->surfaces.reserve(parent->surfaces.size() + 1);
        auto resource = std::make_shared<GtkSurfaceResource>();
        resource->parent = parent_handle;
        resource->generation = nk::core::runtime_generation();
        resource->api = options->api;
        resource->major_version = options->major_version;
        resource->minor_version = options->minor_version;
        resource->flags =
            options->flags & (NK_SURFACE_DEBUG_CONTEXT | NK_SURFACE_FORWARD_COMPATIBLE);
        resource->shared_surface = std::move(shared);
        resource->widget = gtk_gl_area_new();
        g_object_add_weak_pointer(G_OBJECT(resource->widget),
                                  reinterpret_cast<gpointer *>(&resource->widget));
        auto *area = GTK_GL_AREA(resource->widget);
        gtk_gl_area_set_auto_render(area, FALSE);
        gtk_gl_area_set_use_es(area, options->api == NK_GRAPHICS_OPENGL_ES);
        if (options->major_version)
            gtk_gl_area_set_required_version(area, options->major_version,
                                             options->minor_version);
        gtk_gl_area_set_has_alpha(area, (options->flags & NK_SURFACE_ALPHA) != 0);
        gtk_gl_area_set_has_depth_buffer(area, (options->flags & NK_SURFACE_DEPTH) != 0);
        gtk_gl_area_set_has_stencil_buffer(area, (options->flags & NK_SURFACE_STENCIL) != 0);
        gtk_widget_set_size_request(resource->widget, options->width, options->height);
        gtk_fixed_put(GTK_FIXED(parent->container), resource->widget, options->x, options->y);
        resource->handle =
            nk::core::handles().insert(nk::core::ResourceType::surface, resource);
        if (resource->handle == NK_INVALID_HANDLE) {
            gtk_widget_destroy(resource->widget);
            return fail(NK_ERROR_OUT_OF_MEMORY, "graphics surface handle registry is full");
        }
        if (resource->shared_surface)
            ++resource->shared_surface->share_dependents;
        parent->surfaces.push_back(resource->handle);
        g_signal_connect(resource->widget, "create-context",
                         G_CALLBACK(on_surface_create_context), resource.get());
        g_signal_connect(resource->widget, "render", G_CALLBACK(on_surface_render), nullptr);
        g_signal_connect(resource->widget, "resize", G_CALLBACK(on_surface_resize),
                         resource.get());
        if ((options->flags & NK_SURFACE_HIDDEN) == 0)
            gtk_widget_show(resource->widget);
        gtk_widget_realize(resource->widget);
        gtk_gl_area_make_current(area);
        if (const GError *error = gtk_gl_area_get_error(area)) {
            parent->surfaces.pop_back();
            nk::core::handles().erase(resource->handle, nk::core::ResourceType::surface);
            if (resource->shared_surface)
                --resource->shared_surface->share_dependents;
            return fail(NK_ERROR_UNSUPPORTED, error->message);
        }
        nk::core::QueuedEvent ready;
        ready.kind = NK_EVENT_SURFACE_READY;
        ready.source = resource->handle;
        nk::core::push_event(std::move(ready));
        *out_surface = resource->handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_surface_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = surface(handle);
    if (!resource)
        return invalid_handle("graphics surface");
    if (resource->share_dependents)
        return fail(NK_ERROR_INVALID_REQUEST,
                    "graphics surface is still shared by another surface");
    g_signal_handlers_disconnect_by_data(resource->widget, resource.get());
    gtk_widget_destroy(resource->widget);
    resource->widget = nullptr;
    if (auto parent = window(resource->parent)) {
        auto &surfaces = parent->surfaces;
        surfaces.erase(std::remove(surfaces.begin(), surfaces.end(), handle), surfaces.end());
    }
    if (resource->shared_surface)
        --resource->shared_surface->share_dependents;
    nk::core::handles().erase(handle, nk::core::ResourceType::surface);
    return NK_OK;
}

nk_result NK_CALL nk_surface_show(nk_handle handle, uint32_t visible) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = surface(handle);
    if (!resource)
        return invalid_handle("graphics surface");
    visible ? gtk_widget_show(resource->widget) : gtk_widget_hide(resource->widget);
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                        int32_t height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (width <= 0 || height <= 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "graphics surface dimensions must be positive");
    auto resource = surface(handle);
    if (!resource)
        return invalid_handle("graphics surface");
    auto parent = window(resource->parent);
    if (!parent)
        return invalid_handle("parent window");
    gtk_fixed_move(GTK_FIXED(parent->container), resource->widget, x, y);
    gtk_widget_set_size_request(resource->widget, width, height);
    return NK_OK;
}

nk_result NK_CALL nk_surface_make_current(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = surface(handle);
    if (!resource)
        return invalid_handle("graphics surface");
    auto *area = GTK_GL_AREA(resource->widget);
    gtk_gl_area_make_current(area);
    if (const GError *error = gtk_gl_area_get_error(area))
        return fail(NK_ERROR_UNKNOWN, error->message);
    gtk_gl_area_attach_buffers(area);
    return NK_OK;
}

nk_result NK_CALL nk_surface_present(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = surface(handle);
    if (!resource)
        return invalid_handle("graphics surface");
    gtk_gl_area_queue_render(GTK_GL_AREA(resource->widget));
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_framebuffer_size(nk_handle handle, int32_t *out_width,
                                                  int32_t *out_height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_width || !out_height)
        return fail(NK_ERROR_INVALID_ARGUMENT, "framebuffer size outputs must not be null");
    auto resource = surface(handle);
    if (!resource)
        return invalid_handle("graphics surface");
    const int scale = gtk_widget_get_scale_factor(resource->widget);
    *out_width = gtk_widget_get_allocated_width(resource->widget) * scale;
    *out_height = gtk_widget_get_allocated_height(resource->widget) * scale;
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_proc_address(nk_handle handle, const char *name,
                                              nk_graphics_proc *out_proc) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!name || !*name || !out_proc)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid graphics procedure query");
    *out_proc = nullptr;
    if (const auto result = nk_surface_make_current(handle); result != NK_OK)
        return result;

    static void *gl_library = dlopen("libGL.so.1", RTLD_LAZY | RTLD_LOCAL);
    static void *gles_library = dlopen("libGLESv2.so.2", RTLD_LAZY | RTLD_LOCAL);
    void *address = dlsym(RTLD_DEFAULT, name);
    if (!address && gl_library)
        address = dlsym(gl_library, name);
    if (!address && gles_library)
        address = dlsym(gles_library, name);
    if (!address && gl_library) {
        using GlxGetProcAddress = void *(*)(const unsigned char *);
        GlxGetProcAddress resolver = nullptr;
        void *symbol = dlsym(gl_library, "glXGetProcAddressARB");
        static_assert(sizeof(resolver) == sizeof(symbol));
        std::memcpy(&resolver, &symbol, sizeof(resolver));
        if (resolver)
            address = resolver(reinterpret_cast<const unsigned char *>(name));
    }
    if (!address && gles_library) {
        using EglGetProcAddress = void *(*)(const char *);
        EglGetProcAddress resolver = nullptr;
        void *symbol = dlsym(gles_library, "eglGetProcAddress");
        static_assert(sizeof(resolver) == sizeof(symbol));
        std::memcpy(&resolver, &symbol, sizeof(resolver));
        if (resolver)
            address = resolver(name);
    }
    if (!address)
        return fail(NK_ERROR_UNSUPPORTED, "graphics procedure is unavailable");
    static_assert(sizeof(*out_proc) == sizeof(address));
    std::memcpy(out_proc, &address, sizeof(address));
    return NK_OK;
}

nk_result NK_CALL nk_webview_create(nk_handle parent_handle, const nk_webview_options *options,
                                    nk_handle *out_webview) {
    return nk::core::result_boundary("unexpected error while creating WebView", [&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!options || options->struct_size < sizeof(*options) || !out_webview ||
            options->width <= 0 || options->height <= 0) {
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid WebView options");
        }
        *out_webview = NK_INVALID_HANDLE;
        auto parent = window(parent_handle);
        if (!parent)
            return invalid_handle("parent window");
        auto resource = std::make_shared<GtkWebViewResource>();
        resource->generation = nk::core::runtime_generation();
        resource->content_manager = webkit_user_content_manager_new();
        if (!webkit_user_content_manager_register_script_message_handler(resource->content_manager,
                                                                         "nativekit")) {
            return fail(NK_ERROR_UNKNOWN, "could not register the NativeKit JavaScript bridge");
        }
        resource->widget = webkit_web_view_new_with_user_content_manager(resource->content_manager);
        g_object_add_weak_pointer(G_OBJECT(resource->widget),
                                  reinterpret_cast<gpointer *>(&resource->widget));
        resource->parent = parent_handle;
        resource->navigation_policy = (options->flags & NK_WEBVIEW_NAVIGATION_POLICY) != 0;
        gtk_widget_set_size_request(resource->widget, options->width, options->height);
        gtk_fixed_put(GTK_FIXED(parent->container), resource->widget, options->x, options->y);
        resource->handle = nk::core::handles().insert(nk::core::ResourceType::webview, resource);
        if (resource->handle == NK_INVALID_HANDLE) {
            gtk_widget_destroy(resource->widget);
            return fail(NK_ERROR_OUT_OF_MEMORY, "WebView handle registry is full");
        }
        parent->children.push_back(resource->handle);
        g_signal_connect(resource->widget, "load-changed", G_CALLBACK(on_webview_load),
                         resource.get());
        g_signal_connect(resource->widget, "load-failed", G_CALLBACK(on_webview_load_failed),
                         resource.get());
        g_signal_connect(resource->widget, "notify::title", G_CALLBACK(on_webview_title),
                         resource.get());
        g_signal_connect(resource->widget, "web-process-terminated",
                         G_CALLBACK(on_webview_process_terminated), resource.get());
        g_signal_connect(resource->content_manager, "script-message-received::nativekit",
                         G_CALLBACK(on_webview_message), resource.get());
        g_signal_connect(resource->widget, "decide-policy", G_CALLBACK(on_webview_policy),
                         resource.get());
        auto *settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(resource->widget));
        webkit_settings_set_enable_developer_extras(settings,
                                                    (options->flags & NK_WEBVIEW_DEVTOOLS) != 0);
        nk::core::QueuedEvent ready;
        ready.kind = NK_EVENT_WEBVIEW_READY;
        ready.source = resource->handle;
        nk::core::push_event(std::move(ready));
        if (options->initial_url)
            webkit_web_view_load_uri(WEBKIT_WEB_VIEW(resource->widget), options->initial_url);
        if ((options->flags & NK_WEBVIEW_HIDDEN) == 0)
            gtk_widget_show(resource->widget);
        *out_webview = resource->handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_webview_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource)
        return invalid_handle("WebView");
    cancel_navigation_decisions(handle);
    cancel_evaluations(handle);
    g_signal_handlers_disconnect_by_data(resource->widget, resource.get());
    g_signal_handlers_disconnect_by_data(resource->content_manager, resource.get());
    gtk_widget_destroy(resource->widget);
    resource->widget = nullptr;
    if (auto parent = window(resource->parent)) {
        auto &children = parent->children;
        children.erase(std::remove(children.begin(), children.end(), handle), children.end());
    }
    nk::core::handles().erase(handle, nk::core::ResourceType::webview);
    return NK_OK;
}

nk_result NK_CALL nk_webview_show(nk_handle handle, uint32_t visible) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource)
        return invalid_handle("WebView");
    visible ? gtk_widget_show(resource->widget) : gtk_widget_hide(resource->widget);
    return NK_OK;
}

nk_result NK_CALL nk_webview_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                        int32_t height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (width <= 0 || height <= 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "WebView dimensions must be positive");
    auto resource = webview(handle);
    if (!resource)
        return invalid_handle("WebView");
    auto parent = window(resource->parent);
    if (!parent)
        return invalid_handle("parent window");
    gtk_fixed_move(GTK_FIXED(parent->container), resource->widget, x, y);
    gtk_widget_set_size_request(resource->widget, width, height);
    return NK_OK;
}

nk_result NK_CALL nk_webview_navigate(nk_handle handle, const char *url) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!url)
        return fail(NK_ERROR_INVALID_ARGUMENT, "URL must not be null");
    auto resource = webview(handle);
    if (!resource)
        return invalid_handle("WebView");
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(resource->widget), url);
    return NK_OK;
}

nk_result NK_CALL nk_webview_set_html(nk_handle handle, const char *html, const char *base_url) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!html)
        return fail(NK_ERROR_INVALID_ARGUMENT, "HTML must not be null");
    auto resource = webview(handle);
    if (!resource)
        return invalid_handle("WebView");
    webkit_web_view_load_html(WEBKIT_WEB_VIEW(resource->widget), html, base_url);
    return NK_OK;
}

nk_result NK_CALL nk_webview_eval(nk_handle handle, const char *script,
                                  nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while evaluating JavaScript", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!script || !out_request)
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid JavaScript evaluation arguments");
            auto resource = webview(handle);
            if (!resource)
                return invalid_handle("WebView");
            const auto request = nk::core::next_request_id();
            auto context = std::make_unique<EvalContext>(
                EvalContext{handle, request, nk::core::runtime_generation()});
            const auto source = "(()=>{const v=(0,eval)(" + javascript_literal(script) +
                                ");const j=JSON.stringify(v);if(j===undefined)throw new TypeError("
                                "'JavaScript result is not JSON-serializable');return j;})()";
            evaluations.emplace(request, handle);
            webkit_web_view_evaluate_javascript(WEBKIT_WEB_VIEW(resource->widget), source.c_str(),
                                                -1, nullptr, nullptr, nullptr, on_eval_complete,
                                                context.release());
            *out_request = request;
            return NK_OK;
        });
}

nk_result NK_CALL nk_webview_navigation_decide(nk_request_id request, uint32_t allow) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    const auto item = navigation_decisions.find(request);
    if (item == navigation_decisions.end())
        return fail(NK_ERROR_INVALID_REQUEST, "invalid or completed navigation request");
    auto *decision = item->second.decision;
    navigation_decisions.erase(item);
    allow ? webkit_policy_decision_use(decision) : webkit_policy_decision_ignore(decision);
    g_object_unref(decision);
    return NK_OK;
}

nk_result NK_CALL nk_dialog_open_file(nk_handle parent, const nk_file_dialog_options *options,
                                      nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while opening file dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, out_request, NK_DIALOG_OPEN_FILE);
        });
}

nk_result NK_CALL nk_dialog_save_file(nk_handle parent, const nk_file_dialog_options *options,
                                      nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while opening save dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, out_request, NK_DIALOG_SAVE_FILE);
        });
}

nk_result NK_CALL nk_dialog_select_directory(nk_handle parent,
                                             const nk_file_dialog_options *options,
                                             nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while opening directory dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, out_request, NK_DIALOG_SELECT_DIRECTORY);
        });
}

nk_result NK_CALL nk_dialog_message(nk_handle parent_handle,
                                    const nk_message_dialog_options *options,
                                    nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while opening message dialog", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!options || options->struct_size < sizeof(*options) || !out_request ||
                !options->message)
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid message dialog options");
            std::shared_ptr<GtkWindowResource> parent;
            if (parent_handle != NK_INVALID_HANDLE) {
                parent = window(parent_handle);
                if (!parent)
                    return invalid_handle("parent window");
            }
            if (!ensure_gtk())
                return NK_ERROR_UNSUPPORTED;
            GtkMessageType type = GTK_MESSAGE_INFO;
            if (options->kind == NK_MESSAGE_WARNING)
                type = GTK_MESSAGE_WARNING;
            if (options->kind == NK_MESSAGE_ERROR)
                type = GTK_MESSAGE_ERROR;
            if (options->kind == NK_MESSAGE_QUESTION)
                type = GTK_MESSAGE_QUESTION;
            GtkWidget *dialog = gtk_message_dialog_new(
                parent ? GTK_WINDOW(parent->window) : nullptr,
                static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
                type, GTK_BUTTONS_NONE, "%s", options->message);
            g_object_ref_sink(dialog);
            if (options->title)
                gtk_window_set_title(GTK_WINDOW(dialog), options->title);
            const uint32_t buttons =
                options->buttons ? options->buttons : static_cast<uint32_t>(NK_MESSAGE_BUTTON_OK);
            if (buttons & NK_MESSAGE_BUTTON_OK)
                gtk_dialog_add_button(GTK_DIALOG(dialog), "_OK", GTK_RESPONSE_OK);
            if (buttons & NK_MESSAGE_BUTTON_CANCEL)
                gtk_dialog_add_button(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
            if (buttons & NK_MESSAGE_BUTTON_YES)
                gtk_dialog_add_button(GTK_DIALOG(dialog), "_Yes", GTK_RESPONSE_YES);
            if (buttons & NK_MESSAGE_BUTTON_NO)
                gtk_dialog_add_button(GTK_DIALOG(dialog), "_No", GTK_RESPONSE_NO);
            std::unique_ptr<GObject, decltype(&g_object_unref)> dialog_owner(G_OBJECT(dialog),
                                                                             &g_object_unref);
            auto context = std::make_unique<DialogContext>();
            context->object = G_OBJECT(dialog);
            context->request = nk::core::next_request_id();
            context->generation = nk::core::runtime_generation();
            context->parent = parent_handle;
            context->kind = NK_DIALOG_MESSAGE;
            dialogs.emplace(context->request, context.get());
            g_signal_connect(dialog, "response", G_CALLBACK(on_dialog_response), context.get());
            gtk_widget_show(dialog);
            *out_request = context->request;
            dialog_owner.release();
            context.release();
            return NK_OK;
        });
}

nk_result NK_CALL nk_dialog_cancel(nk_request_id request) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    const auto found = dialogs.find(request);
    if (request == NK_INVALID_REQUEST_ID || found == dialogs.end())
        return fail(NK_ERROR_INVALID_REQUEST, "invalid or completed dialog request");
    cancel_dialog(found->second, true);
    return NK_OK;
}

nk_result NK_CALL nk_clipboard_set_text(const char *text) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!text)
        return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard text must not be null");
    if (!ensure_gtk())
        return NK_ERROR_UNSUPPORTED;
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
    clipboard_owned = true;
    return NK_OK;
}

nk_result NK_CALL nk_clipboard_set_files(const char *const *paths, uint32_t path_count) {
    return nk::core::result_boundary(
        "unexpected error while writing clipboard files", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!paths || path_count == 0)
                return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard file list must not be empty");
            if (!ensure_gtk())
                return NK_ERROR_UNSUPPORTED;
            auto owner = std::make_unique<ClipboardFileOwner>();
            owner->uris.reserve(path_count);
            for (uint32_t index = 0; index < path_count; ++index) {
                if (!paths[index] || !*paths[index])
                    return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard path must not be empty");
                char *absolute = g_canonicalize_filename(paths[index], nullptr);
                char *uri = g_filename_to_uri(absolute, nullptr, nullptr);
                g_free(absolute);
                if (!uri)
                    return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard path is invalid");
                owner->uris.emplace_back(uri);
                g_free(uri);
            }
            owner->pointers.reserve(owner->uris.size() + 1);
            for (auto &uri : owner->uris)
                owner->pointers.push_back(uri.data());
            owner->pointers.push_back(nullptr);
            GtkTargetEntry target{const_cast<gchar *>("text/uri-list"), 0, 0};
            if (!gtk_clipboard_set_with_data(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), &target, 1,
                                             provide_clipboard_files, clear_clipboard_files,
                                             owner.get()))
                return fail(NK_ERROR_UNKNOWN, "desktop rejected clipboard file ownership");
            owner.release();
            clipboard_owned = true;
            return NK_OK;
        });
}

nk_result NK_CALL nk_clipboard_read_text(nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while reading clipboard text", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!out_request)
                return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard request output is null");
            if (!ensure_gtk())
                return NK_ERROR_UNSUPPORTED;
            auto request = std::make_unique<ClipboardRequest>();
            request->request = nk::core::next_request_id();
            request->event_kind = NK_EVENT_CLIPBOARD_TEXT_COMPLETE;
            request->generation = nk::core::runtime_generation();
            *out_request = request->request;
            gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                                       on_clipboard_text, request.release());
            return NK_OK;
        });
}

nk_result NK_CALL nk_clipboard_read_files(nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while reading clipboard files", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!out_request)
                return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard request output is null");
            if (!ensure_gtk())
                return NK_ERROR_UNSUPPORTED;
            auto request = std::make_unique<ClipboardRequest>();
            request->request = nk::core::next_request_id();
            request->event_kind = NK_EVENT_CLIPBOARD_FILES_COMPLETE;
            request->generation = nk::core::runtime_generation();
            *out_request = request->request;
            gtk_clipboard_request_uris(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                                       on_clipboard_uris, request.release());
            return NK_OK;
        });
}

nk_result NK_CALL nk_window_set_drop_enabled(nk_handle handle, uint32_t enabled) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    if (!!enabled == resource->drops_enabled)
        return NK_OK;
    if (enabled) {
        GtkTargetEntry targets[] = {{const_cast<gchar *>("text/uri-list"), 0, drop_target_uri},
                                    {const_cast<gchar *>("UTF8_STRING"), 0, drop_target_text}};
        gtk_drag_dest_set(resource->window, GTK_DEST_DEFAULT_ALL, targets, 2, GDK_ACTION_COPY);
        g_signal_connect(resource->window, "drag-data-received", G_CALLBACK(on_drag_data_received),
                         resource.get());
    } else {
        g_signal_handlers_disconnect_by_func(
            resource->window, reinterpret_cast<gpointer>(on_drag_data_received), resource.get());
        gtk_drag_dest_unset(resource->window);
    }
    resource->drops_enabled = enabled != 0;
    return NK_OK;
}

nk_result NK_CALL nk_shell_open_url(const char *url) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!url || !*url)
        return fail(NK_ERROR_INVALID_ARGUMENT, "URL must contain a URI scheme");
    char *scheme = g_uri_parse_scheme(url);
    if (!scheme)
        return fail(NK_ERROR_INVALID_ARGUMENT, "URL must contain a URI scheme");
    g_free(scheme);
    return launch_uri(url);
}

nk_result NK_CALL nk_shell_open_resource(const nk_resource *resource) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!resource || resource->struct_size < sizeof(nk_resource) || !resource->uri ||
        !*resource->uri)
        return fail(NK_ERROR_INVALID_ARGUMENT, "resource URI must not be empty");
    return launch_uri(resource->uri);
}

nk_result NK_CALL nk_share(const nk_share_options *) {
    return fail(NK_ERROR_UNSUPPORTED, "resource sharing is not implemented by the GTK backend");
}

nk_result NK_CALL nk_clipboard_set_resources(const nk_resource *, uint32_t) {
    return fail(NK_ERROR_UNSUPPORTED,
                "resource clipboard is not implemented by the GTK backend");
}

nk_result NK_CALL nk_clipboard_read_resources(nk_request_id *) {
    return fail(NK_ERROR_UNSUPPORTED,
                "resource clipboard is not implemented by the GTK backend");
}

nk_result NK_CALL nk_dialog_open_resource(nk_handle, const nk_file_dialog_options *,
                                          nk_request_id *) {
    return fail(NK_ERROR_UNSUPPORTED, "resource dialogs are not implemented by the GTK backend");
}

nk_result NK_CALL nk_dialog_save_resource(nk_handle, const nk_file_dialog_options *,
                                          nk_request_id *) {
    return fail(NK_ERROR_UNSUPPORTED, "resource dialogs are not implemented by the GTK backend");
}

nk_result NK_CALL nk_dialog_select_resource_directory(nk_handle,
                                                      const nk_file_dialog_options *,
                                                      nk_request_id *) {
    return fail(NK_ERROR_UNSUPPORTED, "resource dialogs are not implemented by the GTK backend");
}

nk_result NK_CALL nk_shell_open_file(const char *path) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    return open_path(path);
}

nk_result NK_CALL nk_shell_reveal_file(const char *path) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!path || !*path)
        return fail(NK_ERROR_INVALID_ARGUMENT, "path must not be empty");
    char *absolute = g_canonicalize_filename(path, nullptr);
    GError *error = nullptr;
    char *uri = g_filename_to_uri(absolute, nullptr, &error);
    if (!uri) {
        g_free(absolute);
        nk::core::set_error(error && error->message ? error->message : "invalid file path");
        if (error)
            g_error_free(error);
        return NK_ERROR_INVALID_ARGUMENT;
    }
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    bool revealed = false;
    if (bus) {
        GVariantBuilder uris;
        g_variant_builder_init(&uris, G_VARIANT_TYPE("as"));
        g_variant_builder_add(&uris, "s", uri);
        GVariant *reply = g_dbus_connection_call_sync(
            bus, "org.freedesktop.FileManager1", "/org/freedesktop/FileManager1",
            "org.freedesktop.FileManager1", "ShowItems", g_variant_new("(ass)", &uris, ""), nullptr,
            G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr);
        revealed = reply != nullptr;
        if (reply)
            g_variant_unref(reply);
        g_object_unref(bus);
    }
    g_free(uri);
    if (revealed) {
        g_free(absolute);
        return NK_OK;
    }
    char *parent = g_path_get_dirname(absolute);
    g_free(absolute);
    const auto fallback = open_path(parent);
    g_free(parent);
    return fallback;
}

nk_result NK_CALL nk_system_directory(nk_system_directory_kind kind, char *buffer,
                                      uint32_t *inout_size) {
    nk::core::clear_error();
    const char *value = system_directory_path(kind);
    if (!value)
        return fail(NK_ERROR_UNSUPPORTED, "system directory is unavailable");
    return copy_utf8(value, buffer, inout_size);
}

nk_result NK_CALL nk_system_locale(char *buffer, uint32_t *inout_size) {
    nk::core::clear_error();
    const char *const *languages = g_get_language_names();
    if (!languages || !languages[0])
        return fail(NK_ERROR_UNSUPPORTED, "system locale is unavailable");
    return copy_utf8(languages[0], buffer, inout_size);
}

nk_result NK_CALL nk_system_get_appearance(nk_system_appearance *appearance) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!appearance || appearance->struct_size < sizeof(*appearance))
        return fail(NK_ERROR_INVALID_ARGUMENT, "appearance output is missing or too small");
    int argc = 0;
    char **argv = nullptr;
    if (!gtk_init_check(&argc, &argv))
        return fail(NK_ERROR_UNSUPPORTED, "GTK could not connect to a display");
    GtkSettings *settings = gtk_settings_get_default();
    if (!settings)
        return fail(NK_ERROR_UNSUPPORTED, "desktop appearance is unavailable");
    gboolean prefer_dark = FALSE;
    char *theme = nullptr;
    g_object_get(settings, "gtk-application-prefer-dark-theme", &prefer_dark, "gtk-theme-name",
                 &theme, nullptr);
    char *normalized = g_ascii_strdown(theme ? theme : "", -1);
    appearance->color_scheme = prefer_dark || std::strstr(normalized, "dark")
                                   ? NK_COLOR_SCHEME_DARK
                                   : NK_COLOR_SCHEME_LIGHT;
    appearance->high_contrast =
        std::strstr(normalized, "highcontrast") || std::strstr(normalized, "high-contrast");
    g_free(normalized);
    g_free(theme);
    return NK_OK;
}

nk_result NK_CALL nk_notification_show(const nk_notification_options *options,
                                       nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while showing notification", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!options || options->struct_size < sizeof(*options) || !out_request ||
                !options->title || !*options->title)
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid notification options");
            if (!g_utf8_validate(options->title, -1, nullptr) ||
                (options->body && !g_utf8_validate(options->body, -1, nullptr)) ||
                (options->icon && !g_utf8_validate(options->icon, -1, nullptr)))
                return fail(NK_ERROR_INVALID_ARGUMENT, "notification text is not valid UTF-8");
            *out_request = NK_INVALID_REQUEST_ID;
            if (!ensure_notification_bus())
                return NK_ERROR_UNSUPPORTED;
            const auto request = nk::core::next_request_id();
            const auto generation = nk::core::runtime_generation();
            auto context =
                std::make_unique<NotificationContext>(NotificationContext{request, generation});
            notifications.emplace(request, NotificationRequest{0, false, generation});
            GVariantBuilder actions;
            g_variant_builder_init(&actions, G_VARIANT_TYPE("as"));
            GVariantBuilder hints;
            g_variant_builder_init(&hints, G_VARIANT_TYPE("a{sv}"));
            if (options->flags & NK_NOTIFICATION_SILENT)
                g_variant_builder_add(&hints, "{sv}", "suppress-sound",
                                      g_variant_new_boolean(TRUE));
            const int timeout = options->timeout_ms > static_cast<uint32_t>(INT_MAX)
                                    ? INT_MAX
                                    : static_cast<int>(options->timeout_ms);
            g_dbus_connection_call(
                notification_bus, "org.freedesktop.Notifications", "/org/freedesktop/Notifications",
                "org.freedesktop.Notifications", "Notify",
                g_variant_new("(susss@as@a{sv}i)", "NativeKit", 0u,
                              options->icon ? options->icon : "", options->title,
                              options->body ? options->body : "", g_variant_builder_end(&actions),
                              g_variant_builder_end(&hints), timeout),
                G_VARIANT_TYPE("(u)"), G_DBUS_CALL_FLAGS_NONE, -1, nullptr, on_notification_shown,
                context.release());
            *out_request = request;
            return NK_OK;
        });
}

nk_result NK_CALL nk_notification_close(nk_request_id request) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    const auto found = notifications.find(request);
    if (!request || found == notifications.end())
        return fail(NK_ERROR_INVALID_REQUEST, "invalid or completed notification request");
    if (found->second.canceled)
        return fail(NK_ERROR_INVALID_REQUEST, "notification request is already closing");
    if (found->second.server_id) {
        close_server_notification(found->second.server_id);
        notification_ids.erase(found->second.server_id);
        notifications.erase(found);
    } else {
        found->second.canceled = true;
    }
    emit_notification(NK_EVENT_NOTIFICATION_DISMISSED, request);
    return NK_OK;
}
}
