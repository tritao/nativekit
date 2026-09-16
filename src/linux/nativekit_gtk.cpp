#include "nativekit_clipboard.h"
#include "nativekit_accessibility.h"
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
#include "core/graphics_frame_target.hpp"
#include "core/graphics_image_registry.h"
#include "core/runtime.hpp"
#include "core/system_internal.hpp"
#include "core/resource_events.hpp"

#include <gtk/gtk.h>
#include <atk/atk.h>
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
#include <cmath>
#include <cstddef>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <unistd.h>
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
    GdkWindow *foreign_window = nullptr;
    nk_native_window_kind foreign_kind = NK_NATIVE_WINDOW_UNKNOWN;
    uintptr_t foreign_display = 0;
    uintptr_t foreign_surface = 0;
    GtkIMContext *im_context = nullptr;
    std::string text_input_text;
    nk_text_input_state text_input_state{};
    bool text_input_active = false;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle owner = NK_INVALID_HANDLE;
    std::vector<nk_handle> children;
    std::vector<nk_handle> surfaces;
    std::vector<nk_handle> owned_windows;
    std::vector<nk_window_decoration_region> decoration_regions;
    bool drops_enabled = false;
    bool wrapped = false;
    bool decorated = true;
    bool resizable = false;
    uint64_t generation = 0;
    std::array<nk_input_action, NK_KEY_LAST + 1> keys{};
    std::array<nk_input_action, NK_POINTER_BUTTON_LAST + 1> buttons{};
    double pointer_x = 0.0;
    double pointer_y = 0.0;
    nk_cursor_mode cursor_mode = NK_CURSOR_MODE_NORMAL;
    std::shared_ptr<GtkCursorResource> cursor;
    GdkCursor *decoration_cursor = nullptr;
    uint32_t decoration_cursor_shape = 0;
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
        if (pointer_grabbed) {
            GdkDisplay *display = nullptr;
            if (foreign_window)
                display = gdk_window_get_display(foreign_window);
            else if (window)
                display = gtk_widget_get_display(window);
            if (display)
                gdk_seat_ungrab(gdk_display_get_default_seat(display));
        }
        if (foreign_window)
            g_object_unref(foreign_window);
        if (decoration_cursor)
            g_object_unref(decoration_cursor);
        if (window && !wrapped)
            gtk_widget_destroy(window);
        if (im_context)
            g_object_unref(im_context);
    }
};

bool begin_decoration_drag(GtkWindowResource &resource, GdkEventButton &event);
nk_result apply_pointer_cursor(GtkWindowResource &resource);

struct GtkAccessibilityTextRange {
    nk_accessibility_text_position start = 0;
    nk_accessibility_text_position end = 0;
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

struct GtkAccessibilityNode {
    nk_accessibility_node_id id = NK_ACCESSIBILITY_ROOT;
    nk_accessibility_node_id parent = NK_ACCESSIBILITY_ROOT;
    uint32_t child_index = 0;
    nk_accessibility_role role = NK_ACCESSIBILITY_GROUP;
    nk_accessibility_states states = 0;
    nk_accessibility_actions actions = 0;
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
    std::string label;
    std::string value;
    double numeric_value = 0;
    double numeric_minimum = 0;
    double numeric_maximum = 0;
    nk_accessibility_text_position text_start = 0;
    nk_accessibility_text_position document_length = 0;
    nk_accessibility_text_position selection_start = NK_ACCESSIBILITY_TEXT_POSITION_NONE;
    nk_accessibility_text_position selection_end = NK_ACCESSIBILITY_TEXT_POSITION_NONE;
    uint32_t set_size = 0;
    uint32_t position_in_set = 0;
    uint32_t row_count = 0;
    uint32_t column_count = 0;
    uint32_t row_index = NK_ACCESSIBILITY_INDEX_NONE;
    uint32_t column_index = NK_ACCESSIBILITY_INDEX_NONE;
    uint32_t row_span = 0;
    uint32_t column_span = 0;
    uint32_t hierarchy_level = 0;
    nk_accessibility_orientation orientation = NK_ACCESSIBILITY_ORIENTATION_UNSPECIFIED;
    std::vector<GtkAccessibilityTextRange> text_ranges;
};

constexpr const char *k_accessibility_resource_data = "nativekit-surface-resource";

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
    nk_surface_frame_callback frame_callback = nullptr;
    void *frame_user_data = nullptr;
    guint frame_tick = 0;
    std::shared_ptr<GtkSurfaceResource> shared_surface;
    std::unordered_map<nk_accessibility_node_id, GtkAccessibilityNode> accessibility_nodes;
    nk_accessibility_node_id accessibility_focus = NK_ACCESSIBILITY_ROOT;

    ~GtkSurfaceResource() override {
        if (widget && frame_tick)
            gtk_widget_remove_tick_callback(widget, frame_tick);
        if (widget) {
            g_object_set_data(G_OBJECT(widget), k_accessibility_resource_data, nullptr);
            gtk_widget_destroy(widget);
        }
    }
};

struct NkAccessibilityRoot;
struct NkAccessibilityElement;

struct NkAccessibilityRoot {
    AtkObject parent;
    GtkWidget *widget = nullptr;
    GPtrArray *children = nullptr;
};

struct NkAccessibilityRootClass {
    AtkObjectClass parent_class;
};

struct NkAccessibilityElement {
    AtkObject parent;
    NkAccessibilityRoot *root = nullptr;
    nk_accessibility_node_id id = NK_ACCESSIBILITY_ROOT;
    GPtrArray *children = nullptr;
};

struct NkAccessibilityElementClass {
    AtkObjectClass parent_class;
};

struct NkGLArea {
    GtkGLArea parent;
};

struct NkGLAreaClass {
    GtkGLAreaClass parent_class;
};

static void gtk_accessibility_action_init(AtkActionIface *iface);
static void gtk_accessibility_component_init(AtkComponentIface *iface);
static void gtk_accessibility_text_init(AtkTextIface *iface);
static void gtk_accessibility_value_init(AtkValueIface *iface);
static void refresh_gtk_accessibility(GtkSurfaceResource &resource);

G_DEFINE_TYPE(NkAccessibilityRoot, nk_accessibility_root, ATK_TYPE_OBJECT)
G_DEFINE_TYPE_WITH_CODE(NkAccessibilityElement, nk_accessibility_element, ATK_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(ATK_TYPE_ACTION, gtk_accessibility_action_init)
                            G_IMPLEMENT_INTERFACE(ATK_TYPE_COMPONENT,
                                                  gtk_accessibility_component_init)
                                G_IMPLEMENT_INTERFACE(ATK_TYPE_TEXT, gtk_accessibility_text_init)
                                    G_IMPLEMENT_INTERFACE(ATK_TYPE_VALUE,
                                                          gtk_accessibility_value_init))

const GtkSurfaceResource *accessibility_resource(const NkAccessibilityRoot *root) {
    if (!root || !root->widget)
        return nullptr;
    return static_cast<const GtkSurfaceResource *>(
        g_object_get_data(G_OBJECT(root->widget), k_accessibility_resource_data));
}

GtkSurfaceResource *accessibility_resource(NkAccessibilityRoot *root) {
    return const_cast<GtkSurfaceResource *>(
        accessibility_resource(const_cast<const NkAccessibilityRoot *>(root)));
}

const GtkAccessibilityNode *accessibility_node(const NkAccessibilityElement *element) {
    const auto *resource = accessibility_resource(element ? element->root : nullptr);
    if (!resource)
        return nullptr;
    const auto found = resource->accessibility_nodes.find(element->id);
    return found == resource->accessibility_nodes.end() ? nullptr : &found->second;
}

AtkRole gtk_accessibility_role(nk_accessibility_role role) {
    switch (role) {
    case NK_ACCESSIBILITY_BUTTON:
        return ATK_ROLE_PUSH_BUTTON;
    case NK_ACCESSIBILITY_CHECKBOX:
        return ATK_ROLE_CHECK_BOX;
    case NK_ACCESSIBILITY_RADIO:
        return ATK_ROLE_RADIO_BUTTON;
    case NK_ACCESSIBILITY_TEXT:
        return ATK_ROLE_STATIC;
    case NK_ACCESSIBILITY_TEXT_FIELD:
        return ATK_ROLE_ENTRY;
    case NK_ACCESSIBILITY_LINK:
        return ATK_ROLE_LINK;
    case NK_ACCESSIBILITY_IMAGE:
        return ATK_ROLE_IMAGE;
    case NK_ACCESSIBILITY_HEADING:
        return ATK_ROLE_HEADING;
    case NK_ACCESSIBILITY_LIST:
        return ATK_ROLE_LIST;
    case NK_ACCESSIBILITY_LIST_ITEM:
        return ATK_ROLE_LIST_ITEM;
    case NK_ACCESSIBILITY_SLIDER:
        return ATK_ROLE_SLIDER;
    case NK_ACCESSIBILITY_SCROLL_AREA:
        return ATK_ROLE_SCROLL_PANE;
    case NK_ACCESSIBILITY_DIALOG:
        return ATK_ROLE_DIALOG;
    case NK_ACCESSIBILITY_MENU:
        return ATK_ROLE_MENU;
    case NK_ACCESSIBILITY_MENU_BAR:
        return ATK_ROLE_MENU_BAR;
    case NK_ACCESSIBILITY_MENU_ITEM:
        return ATK_ROLE_MENU_ITEM;
    case NK_ACCESSIBILITY_TAB_LIST:
        return ATK_ROLE_PAGE_TAB_LIST;
    case NK_ACCESSIBILITY_TAB:
        return ATK_ROLE_PAGE_TAB;
    case NK_ACCESSIBILITY_TAB_PANEL:
        return ATK_ROLE_PANEL;
    case NK_ACCESSIBILITY_SWITCH:
        return ATK_ROLE_TOGGLE_BUTTON;
    case NK_ACCESSIBILITY_PROGRESS_BAR:
        return ATK_ROLE_PROGRESS_BAR;
    case NK_ACCESSIBILITY_COMBO_BOX:
        return ATK_ROLE_COMBO_BOX;
    case NK_ACCESSIBILITY_GRID:
        return ATK_ROLE_TABLE;
    case NK_ACCESSIBILITY_ROW:
        return ATK_ROLE_TABLE_ROW;
    case NK_ACCESSIBILITY_CELL:
        return ATK_ROLE_TABLE_CELL;
    case NK_ACCESSIBILITY_COLUMN_HEADER:
        return ATK_ROLE_COLUMN_HEADER;
    case NK_ACCESSIBILITY_ROW_HEADER:
        return ATK_ROLE_ROW_HEADER;
    case NK_ACCESSIBILITY_TREE:
        return ATK_ROLE_TREE;
    case NK_ACCESSIBILITY_TREE_ITEM:
        return ATK_ROLE_TREE_ITEM;
    case NK_ACCESSIBILITY_SEPARATOR:
        return ATK_ROLE_SEPARATOR;
    case NK_ACCESSIBILITY_TOOLBAR:
        return ATK_ROLE_TOOL_BAR;
    case NK_ACCESSIBILITY_STATUS:
        return ATK_ROLE_STATUSBAR;
    case NK_ACCESSIBILITY_ALERT:
        return ATK_ROLE_ALERT;
    case NK_ACCESSIBILITY_COLLECTION:
    case NK_ACCESSIBILITY_COLLECTION_ITEM:
    case NK_ACCESSIBILITY_GROUP:
    default:
        return role == NK_ACCESSIBILITY_COLLECTION_ITEM ? ATK_ROLE_LIST_ITEM : ATK_ROLE_PANEL;
    }
}

AtkStateSet *gtk_accessibility_state_set(const GtkAccessibilityNode *node,
                                         const GtkSurfaceResource *resource) {
    auto *states = atk_state_set_new();
    if (!node)
        return states;
    if (!(node->states & NK_ACCESSIBILITY_DISABLED)) {
        atk_state_set_add_state(states, ATK_STATE_ENABLED);
        atk_state_set_add_state(states, ATK_STATE_SENSITIVE);
    }
    if (node->states & NK_ACCESSIBILITY_FOCUSABLE)
        atk_state_set_add_state(states, ATK_STATE_FOCUSABLE);
    if ((node->states & NK_ACCESSIBILITY_FOCUSED) ||
        (resource && resource->accessibility_focus == node->id))
        atk_state_set_add_state(states, ATK_STATE_FOCUSED);
    if (node->states & NK_ACCESSIBILITY_SELECTED)
        atk_state_set_add_state(states, ATK_STATE_SELECTED);
    if (node->states & NK_ACCESSIBILITY_CHECKED)
        atk_state_set_add_state(states, ATK_STATE_CHECKED);
    if (node->states & NK_ACCESSIBILITY_READ_ONLY)
        atk_state_set_add_state(states, ATK_STATE_READ_ONLY);
    else if (node->role == NK_ACCESSIBILITY_TEXT_FIELD)
        atk_state_set_add_state(states, ATK_STATE_EDITABLE);
    if (node->states & NK_ACCESSIBILITY_MULTILINE)
        atk_state_set_add_state(states, ATK_STATE_MULTI_LINE);
    if (node->states & NK_ACCESSIBILITY_PASSWORD)
        atk_state_set_add_state(states, ATK_STATE_READ_ONLY);
    if (node->states & NK_ACCESSIBILITY_EXPANDED)
        atk_state_set_add_state(states, ATK_STATE_EXPANDED);
    else if (node->actions & (NK_ACCESSIBILITY_CAN_EXPAND | NK_ACCESSIBILITY_CAN_COLLAPSE))
        atk_state_set_add_state(states, ATK_STATE_COLLAPSED);
    if (node->states & NK_ACCESSIBILITY_MODAL)
        atk_state_set_add_state(states, ATK_STATE_MODAL);
    if (node->states & NK_ACCESSIBILITY_REQUIRED)
        atk_state_set_add_state(states, ATK_STATE_REQUIRED);
    if (node->states & NK_ACCESSIBILITY_INVALID)
        atk_state_set_add_state(states, ATK_STATE_INVALID_ENTRY);
    if (node->states & NK_ACCESSIBILITY_BUSY)
        atk_state_set_add_state(states, ATK_STATE_BUSY);
    if (node->states & NK_ACCESSIBILITY_HAS_POPUP)
        atk_state_set_add_state(states, ATK_STATE_HAS_POPUP);
    if (node->orientation == NK_ACCESSIBILITY_ORIENTATION_HORIZONTAL)
        atk_state_set_add_state(states, ATK_STATE_HORIZONTAL);
    else if (node->orientation == NK_ACCESSIBILITY_ORIENTATION_VERTICAL)
        atk_state_set_add_state(states, ATK_STATE_VERTICAL);
    if (resource && resource->widget && gtk_widget_get_visible(resource->widget)) {
        atk_state_set_add_state(states, ATK_STATE_VISIBLE);
        if (gtk_widget_is_drawable(resource->widget))
            atk_state_set_add_state(states, ATK_STATE_SHOWING);
    }
    if (node->actions & NK_ACCESSIBILITY_CAN_SET_SELECTION)
        atk_state_set_add_state(states, ATK_STATE_SELECTABLE_TEXT);
    return states;
}

struct GtkAccessibilityActionSpec {
    nk_accessibility_action action;
    nk_accessibility_actions bit;
    const char *name;
};

constexpr GtkAccessibilityActionSpec k_accessibility_actions[] = {
    {NK_ACCESSIBILITY_ACTION_ACTIVATE, NK_ACCESSIBILITY_CAN_ACTIVATE, "activate"},
    {NK_ACCESSIBILITY_ACTION_FOCUS, NK_ACCESSIBILITY_CAN_FOCUS, "focus"},
    {NK_ACCESSIBILITY_ACTION_CLEAR_FOCUS, NK_ACCESSIBILITY_CAN_FOCUS, "clear-focus"},
    {NK_ACCESSIBILITY_ACTION_SET_VALUE, NK_ACCESSIBILITY_CAN_SET_VALUE, "set-value"},
    {NK_ACCESSIBILITY_ACTION_SET_SELECTION, NK_ACCESSIBILITY_CAN_SET_SELECTION, "set-selection"},
    {NK_ACCESSIBILITY_ACTION_INCREMENT, NK_ACCESSIBILITY_CAN_INCREMENT, "increment"},
    {NK_ACCESSIBILITY_ACTION_DECREMENT, NK_ACCESSIBILITY_CAN_DECREMENT, "decrement"},
    {NK_ACCESSIBILITY_ACTION_SCROLL_FORWARD, NK_ACCESSIBILITY_CAN_SCROLL_FORWARD, "scroll-forward"},
    {NK_ACCESSIBILITY_ACTION_SCROLL_BACKWARD, NK_ACCESSIBILITY_CAN_SCROLL_BACKWARD,
     "scroll-backward"},
    {NK_ACCESSIBILITY_ACTION_MOVE_NEXT, NK_ACCESSIBILITY_CAN_MOVE_NEXT, "move-next"},
    {NK_ACCESSIBILITY_ACTION_MOVE_PREVIOUS, NK_ACCESSIBILITY_CAN_MOVE_PREVIOUS, "move-previous"},
    {NK_ACCESSIBILITY_ACTION_TOGGLE, NK_ACCESSIBILITY_CAN_TOGGLE, "toggle"},
    {NK_ACCESSIBILITY_ACTION_SELECT, NK_ACCESSIBILITY_CAN_SELECT, "select"},
    {NK_ACCESSIBILITY_ACTION_DESELECT, NK_ACCESSIBILITY_CAN_DESELECT, "deselect"},
    {NK_ACCESSIBILITY_ACTION_EXPAND, NK_ACCESSIBILITY_CAN_EXPAND, "expand"},
    {NK_ACCESSIBILITY_ACTION_COLLAPSE, NK_ACCESSIBILITY_CAN_COLLAPSE, "collapse"},
    {NK_ACCESSIBILITY_ACTION_DISMISS, NK_ACCESSIBILITY_CAN_DISMISS, "dismiss"},
    {NK_ACCESSIBILITY_ACTION_SHOW_CONTEXT_MENU, NK_ACCESSIBILITY_CAN_SHOW_CONTEXT_MENU,
     "show-context-menu"},
    {NK_ACCESSIBILITY_ACTION_SCROLL_INTO_VIEW, NK_ACCESSIBILITY_CAN_SCROLL_INTO_VIEW,
     "scroll-into-view"},
};

nk_accessibility_actions gtk_accessibility_action_bit(nk_accessibility_action action) {
    for (const auto &spec : k_accessibility_actions)
        if (spec.action == action)
            return spec.bit;
    return 0;
}

nk_result emit_gtk_accessibility_action(
    NkAccessibilityRoot *root, nk_accessibility_node_id node, nk_accessibility_action action,
    const std::string &value = {},
    nk_accessibility_text_position selection_start = NK_ACCESSIBILITY_TEXT_POSITION_NONE,
    nk_accessibility_text_position selection_end = NK_ACCESSIBILITY_TEXT_POSITION_NONE,
    nk_accessibility_text_granularity granularity = 0) noexcept {
    return nk::core::callback_boundary_or<nk_result>(NK_ERROR_UNKNOWN, [&]() -> nk_result {
        auto *resource = accessibility_resource(root);
        if (!resource)
            return NK_ERROR_INVALID_HANDLE;
        const auto found = resource->accessibility_nodes.find(node);
        const auto required = gtk_accessibility_action_bit(action);
        if (found == resource->accessibility_nodes.end() || !required ||
            !(found->second.actions & required))
            return NK_ERROR_UNSUPPORTED;
        if (action == NK_ACCESSIBILITY_ACTION_FOCUS)
            resource->accessibility_focus = node;
        else if (action == NK_ACCESSIBILITY_ACTION_CLEAR_FOCUS &&
                 resource->accessibility_focus == node)
            resource->accessibility_focus = NK_ACCESSIBILITY_ROOT;
        nk_accessibility_action_event payload{};
        payload.node_id = node;
        payload.action = action;
        payload.value_offset = value.empty() ? 0u : sizeof(payload);
        payload.value_length = static_cast<uint32_t>(value.size());
        payload.selection_start = selection_start;
        payload.selection_end = selection_end;
        payload.granularity = granularity;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_ACCESSIBILITY_ACTION;
        event.source = resource->handle;
        event.data.resize(sizeof(payload) + value.size() + (value.empty() ? 0u : 1u));
        std::memcpy(event.data.data(), &payload, sizeof(payload));
        if (!value.empty())
            std::memcpy(event.data.data() + sizeof(payload), value.c_str(), value.size() + 1);
        return nk::core::push_event(std::move(event));
    });
}

const char *gtk_accessibility_element_name(AtkObject *object) {
    const auto *node = accessibility_node(reinterpret_cast<NkAccessibilityElement *>(object));
    return node && !node->label.empty() ? node->label.c_str() : nullptr;
}

const char *gtk_accessibility_element_description(AtkObject *object) {
    const auto *node = accessibility_node(reinterpret_cast<NkAccessibilityElement *>(object));
    return node && !node->value.empty() ? node->value.c_str() : nullptr;
}

AtkStateSet *gtk_accessibility_element_state_set(AtkObject *object) {
    auto *element = reinterpret_cast<NkAccessibilityElement *>(object);
    return gtk_accessibility_state_set(accessibility_node(element),
                                       accessibility_resource(element ? element->root : nullptr));
}

gint gtk_accessibility_element_n_children(AtkObject *object) {
    const auto *element = reinterpret_cast<NkAccessibilityElement *>(object);
    return element && element->children ? static_cast<gint>(element->children->len) : 0;
}

AtkObject *gtk_accessibility_element_ref_child(AtkObject *object, gint index) {
    auto *element = reinterpret_cast<NkAccessibilityElement *>(object);
    if (!element || !element->children || index < 0 ||
        static_cast<guint>(index) >= element->children->len)
        return nullptr;
    return reinterpret_cast<AtkObject *>(g_object_ref(g_ptr_array_index(element->children, index)));
}

gint gtk_accessibility_element_index(AtkObject *object) {
    auto *parent = atk_object_get_parent(object);
    if (!parent)
        return -1;
    for (gint index = 0; index < atk_object_get_n_accessible_children(parent); ++index) {
        auto *child = atk_object_ref_accessible_child(parent, index);
        const bool match = child == object;
        if (child)
            g_object_unref(child);
        if (match)
            return index;
    }
    return -1;
}

AtkObject *gtk_accessibility_root_ref_child(AtkObject *object, gint index) {
    auto *root = reinterpret_cast<NkAccessibilityRoot *>(object);
    if (!root || !root->children || index < 0 || static_cast<guint>(index) >= root->children->len)
        return nullptr;
    return reinterpret_cast<AtkObject *>(g_object_ref(g_ptr_array_index(root->children, index)));
}

gint gtk_accessibility_root_n_children(AtkObject *object) {
    auto *root = reinterpret_cast<NkAccessibilityRoot *>(object);
    return root && root->children ? static_cast<gint>(root->children->len) : 0;
}

const char *gtk_accessibility_root_name(AtkObject *) {
    return "NativeKit surface";
}

AtkStateSet *gtk_accessibility_root_state_set(AtkObject *object) {
    auto *root = reinterpret_cast<NkAccessibilityRoot *>(object);
    auto *states = atk_state_set_new();
    if (root && root->widget) {
        if (gtk_widget_get_visible(root->widget))
            atk_state_set_add_state(states, ATK_STATE_VISIBLE);
        if (gtk_widget_is_drawable(root->widget))
            atk_state_set_add_state(states, ATK_STATE_SHOWING);
        if (gtk_widget_get_sensitive(root->widget)) {
            atk_state_set_add_state(states, ATK_STATE_ENABLED);
            atk_state_set_add_state(states, ATK_STATE_SENSITIVE);
        }
    }
    return states;
}

void nk_accessibility_root_finalize(GObject *object) {
    auto *root = reinterpret_cast<NkAccessibilityRoot *>(object);
    if (root->children)
        g_ptr_array_unref(root->children);
    G_OBJECT_CLASS(nk_accessibility_root_parent_class)->finalize(object);
}

void nk_accessibility_root_class_init(NkAccessibilityRootClass *klass) {
    auto *object_class = ATK_OBJECT_CLASS(klass);
    object_class->get_name = gtk_accessibility_root_name;
    object_class->get_n_children = gtk_accessibility_root_n_children;
    object_class->ref_child = gtk_accessibility_root_ref_child;
    object_class->get_role = [](AtkObject *) { return ATK_ROLE_PANEL; };
    object_class->ref_state_set = gtk_accessibility_root_state_set;
    auto *gobject_class = G_OBJECT_CLASS(klass);
    gobject_class->finalize = nk_accessibility_root_finalize;
}

void nk_accessibility_root_init(NkAccessibilityRoot *root) {
    root->children = g_ptr_array_new_with_free_func([](gpointer child) {
        if (child)
            g_object_unref(child);
    });
}

void nk_accessibility_element_finalize(GObject *object) {
    auto *element = reinterpret_cast<NkAccessibilityElement *>(object);
    if (element->children)
        g_ptr_array_unref(element->children);
    G_OBJECT_CLASS(nk_accessibility_element_parent_class)->finalize(object);
}

void nk_accessibility_element_class_init(NkAccessibilityElementClass *klass) {
    auto *object_class = ATK_OBJECT_CLASS(klass);
    object_class->get_name = gtk_accessibility_element_name;
    object_class->get_description = gtk_accessibility_element_description;
    object_class->get_n_children = gtk_accessibility_element_n_children;
    object_class->ref_child = gtk_accessibility_element_ref_child;
    object_class->get_index_in_parent = gtk_accessibility_element_index;
    object_class->get_role = [](AtkObject *object) {
        const auto *node = accessibility_node(reinterpret_cast<NkAccessibilityElement *>(object));
        return node ? gtk_accessibility_role(node->role) : ATK_ROLE_UNKNOWN;
    };
    object_class->ref_state_set = gtk_accessibility_element_state_set;
    G_OBJECT_CLASS(klass)->finalize = nk_accessibility_element_finalize;
}

void nk_accessibility_element_init(NkAccessibilityElement *element) {
    element->children = g_ptr_array_new_with_free_func([](gpointer child) {
        if (child)
            g_object_unref(child);
    });
}

G_DEFINE_TYPE(NkGLArea, nk_gl_area, GTK_TYPE_GL_AREA)

void destroy_accessibility_root(gpointer data) {
    if (data)
        g_object_unref(data);
}

AtkObject *nk_gl_area_get_accessible(GtkWidget *widget) {
    auto *root = reinterpret_cast<NkAccessibilityRoot *>(
        g_object_get_data(G_OBJECT(widget), "nativekit-accessibility-root"));
    if (!root) {
        root = reinterpret_cast<NkAccessibilityRoot *>(
            g_object_new(nk_accessibility_root_get_type(), nullptr));
        root->widget = widget;
        atk_object_initialize(ATK_OBJECT(root), widget);
        g_object_set_data_full(G_OBJECT(widget), "nativekit-accessibility-root", root,
                               destroy_accessibility_root);
    }
    return ATK_OBJECT(root);
}

void nk_gl_area_class_init(NkGLAreaClass *klass) {
    GTK_WIDGET_CLASS(klass)->get_accessible = nk_gl_area_get_accessible;
}

void nk_gl_area_init(NkGLArea *) {}

GtkWidget *nk_gl_area_new() {
    return GTK_WIDGET(g_object_new(nk_gl_area_get_type(), nullptr));
}

const GtkAccessibilityNode *accessibility_node_from_object(AtkObject *object) {
    return accessibility_node(reinterpret_cast<NkAccessibilityElement *>(object));
}

bool accessibility_action_for_index(const GtkAccessibilityNode &node, gint index,
                                    nk_accessibility_action &action) {
    if (index < 0)
        return false;
    for (const auto &spec : k_accessibility_actions) {
        if (!(node.actions & spec.bit))
            continue;
        if (index-- == 0) {
            action = spec.action;
            return true;
        }
    }
    return false;
}

gint gtk_accessibility_action_count(AtkAction *action) {
    const auto *node = accessibility_node_from_object(ATK_OBJECT(action));
    if (!node)
        return 0;
    gint count = 0;
    for (const auto &spec : k_accessibility_actions)
        if (node->actions & spec.bit)
            ++count;
    return count;
}

const GtkAccessibilityActionSpec *gtk_accessibility_action_spec(AtkAction *action, gint index) {
    const auto *node = accessibility_node_from_object(ATK_OBJECT(action));
    if (!node || index < 0)
        return nullptr;
    for (const auto &spec : k_accessibility_actions) {
        if (!(node->actions & spec.bit))
            continue;
        if (index-- == 0)
            return &spec;
    }
    return nullptr;
}

gboolean gtk_accessibility_do_action(AtkAction *action, gint index) {
    auto *element = reinterpret_cast<NkAccessibilityElement *>(ATK_OBJECT(action));
    const auto *node = accessibility_node(element);
    nk_accessibility_action requested = 0;
    if (!node || !accessibility_action_for_index(*node, index, requested))
        return FALSE;
    return emit_gtk_accessibility_action(element->root, element->id, requested) == NK_OK;
}

const gchar *gtk_accessibility_action_name(AtkAction *action, gint index) {
    const auto *spec = gtk_accessibility_action_spec(action, index);
    return spec ? spec->name : nullptr;
}

const gchar *gtk_accessibility_action_description(AtkAction *action, gint index) {
    return gtk_accessibility_action_name(action, index);
}

void gtk_accessibility_action_init(AtkActionIface *iface) {
    iface->do_action = gtk_accessibility_do_action;
    iface->get_n_actions = gtk_accessibility_action_count;
    iface->get_description = gtk_accessibility_action_description;
    iface->get_name = gtk_accessibility_action_name;
    iface->get_localized_name = gtk_accessibility_action_name;
}

void accessibility_origin(const NkAccessibilityElement &element, AtkCoordType coords, gint &x,
                          gint &y);

const GtkAccessibilityNode *gtk_accessibility_text_node(AtkText *text) {
    return accessibility_node(reinterpret_cast<NkAccessibilityElement *>(ATK_OBJECT(text)));
}

gint gtk_accessibility_text_length(const GtkAccessibilityNode *node) {
    return node ? static_cast<gint>(g_utf8_strlen(node->value.c_str(), -1)) : 0;
}

gchar *gtk_accessibility_text_slice(const GtkAccessibilityNode *node, gint start, gint end) {
    const gint length = gtk_accessibility_text_length(node);
    if (!node || start < 0 || end < start || start > length)
        return g_strdup("");
    end = std::min(end, length);
    const auto *begin = g_utf8_offset_to_pointer(node->value.c_str(), start);
    const auto *finish = g_utf8_offset_to_pointer(node->value.c_str(), end);
    return g_strndup(begin, static_cast<gsize>(finish - begin));
}

gchar *gtk_accessibility_text_get(AtkText *text, gint start, gint end) {
    return gtk_accessibility_text_slice(gtk_accessibility_text_node(text), start, end);
}

gunichar gtk_accessibility_text_character(AtkText *text, gint offset) {
    const auto *node = gtk_accessibility_text_node(text);
    if (!node || offset < 0 || offset >= gtk_accessibility_text_length(node))
        return 0;
    return g_utf8_get_char(g_utf8_offset_to_pointer(node->value.c_str(), offset));
}

void gtk_accessibility_text_boundary(const GtkAccessibilityNode *node, gint offset,
                                     AtkTextGranularity granularity, gint *start, gint *end) {
    const gint length = gtk_accessibility_text_length(node);
    offset = std::clamp(offset, 0, std::max(length - 1, 0));
    gint result_start = offset;
    gint result_end = std::min(offset + 1, length);
    if (!node || !length) {
        result_start = result_end = 0;
    } else if (granularity == ATK_TEXT_GRANULARITY_WORD) {
        const auto points = [&](gint index) {
            return g_utf8_get_char(g_utf8_offset_to_pointer(node->value.c_str(), index));
        };
        const bool word = g_unichar_isalnum(points(offset)) != FALSE;
        while (result_start > 0 && (g_unichar_isalnum(points(result_start - 1)) != FALSE) == word)
            --result_start;
        while (result_end < length && (g_unichar_isalnum(points(result_end)) != FALSE) == word)
            ++result_end;
    } else if (granularity == ATK_TEXT_GRANULARITY_LINE ||
               granularity == ATK_TEXT_GRANULARITY_PARAGRAPH) {
        while (result_start > 0 && g_utf8_get_char(g_utf8_offset_to_pointer(
                                       node->value.c_str(), result_start - 1)) != '\n')
            --result_start;
        while (result_end < length &&
               g_utf8_get_char(g_utf8_offset_to_pointer(node->value.c_str(), result_end)) != '\n')
            ++result_end;
    } else if (granularity == ATK_TEXT_GRANULARITY_SENTENCE) {
        auto terminal = [](gunichar point) { return point == '.' || point == '!' || point == '?'; };
        while (result_start > 0 && !terminal(g_utf8_get_char(g_utf8_offset_to_pointer(
                                       node->value.c_str(), result_start - 1))))
            --result_start;
        while (result_end < length && !terminal(g_utf8_get_char(g_utf8_offset_to_pointer(
                                          node->value.c_str(), result_end - 1))))
            ++result_end;
    }
    if (start)
        *start = result_start;
    if (end)
        *end = result_end;
}

gchar *gtk_accessibility_text_string_at_offset(AtkText *text, gint offset,
                                               AtkTextGranularity granularity, gint *start,
                                               gint *end) {
    const auto *node = gtk_accessibility_text_node(text);
    gint local_start = 0;
    gint local_end = 0;
    gtk_accessibility_text_boundary(node, offset, granularity, &local_start, &local_end);
    if (start)
        *start = local_start;
    if (end)
        *end = local_end;
    return gtk_accessibility_text_slice(node, local_start, local_end);
}

gint gtk_accessibility_text_caret(AtkText *text) {
    auto *element = reinterpret_cast<NkAccessibilityElement *>(ATK_OBJECT(text));
    const auto *node = gtk_accessibility_text_node(text);
    if (!element || !node || node->selection_end == NK_ACCESSIBILITY_TEXT_POSITION_NONE ||
        node->selection_end < node->text_start)
        return -1;
    return static_cast<gint>(node->selection_end - node->text_start);
}

gint gtk_accessibility_text_count(AtkText *text) {
    return gtk_accessibility_text_length(gtk_accessibility_text_node(text));
}

void gtk_accessibility_text_extents(AtkText *text, gint offset, gint *x, gint *y, gint *width,
                                    gint *height, AtkCoordType coords) {
    auto *element = reinterpret_cast<NkAccessibilityElement *>(ATK_OBJECT(text));
    const auto *node = gtk_accessibility_text_node(text);
    gint left = 0;
    gint top = 0;
    if (element)
        accessibility_origin(*element, coords, left, top);
    if (x)
        *x = left + (node ? static_cast<gint>(node->x) : 0);
    if (y)
        *y = top + (node ? static_cast<gint>(node->y) : 0);
    if (width)
        *width = node ? static_cast<gint>(node->width) : 0;
    if (height)
        *height = node ? static_cast<gint>(node->height) : 0;
    if (!node)
        return;
    const auto absolute =
        node->text_start + static_cast<nk_accessibility_text_position>(std::max(offset, 0));
    for (const auto &range : node->text_ranges) {
        if (absolute >= range.start && absolute < range.end) {
            if (x)
                *x = left + static_cast<gint>(range.x);
            if (y)
                *y = top + static_cast<gint>(range.y);
            if (width)
                *width = static_cast<gint>(range.width);
            if (height)
                *height = static_cast<gint>(range.height);
            return;
        }
    }
}

void gtk_accessibility_text_range_extents(AtkText *text, gint start, gint end, AtkCoordType coords,
                                          AtkTextRectangle *rectangle) {
    if (!rectangle)
        return;
    gtk_accessibility_text_extents(text, start, &rectangle->x, &rectangle->y, &rectangle->width,
                                   &rectangle->height, coords);
    const auto *node = gtk_accessibility_text_node(text);
    if (!node)
        return;
    const gint length = gtk_accessibility_text_length(node);
    start = std::clamp(start, 0, length);
    end = std::clamp(end, start, length);
    auto *element = reinterpret_cast<NkAccessibilityElement *>(ATK_OBJECT(text));
    gint origin_x = 0;
    gint origin_y = 0;
    if (element)
        accessibility_origin(*element, coords, origin_x, origin_y);
    bool found = false;
    for (const auto &range : node->text_ranges) {
        const auto local_start =
            static_cast<nk_accessibility_text_position>(start) + node->text_start;
        const auto local_end = static_cast<nk_accessibility_text_position>(end) + node->text_start;
        if (range.end <= local_start || range.start >= local_end)
            continue;
        const gint x = origin_x + static_cast<gint>(range.x);
        const gint y = origin_y + static_cast<gint>(range.y);
        const gint right = x + static_cast<gint>(range.width);
        const gint bottom = y + static_cast<gint>(range.height);
        if (!found) {
            rectangle->x = x;
            rectangle->y = y;
            rectangle->width = right - x;
            rectangle->height = bottom - y;
            found = true;
        } else {
            const gint old_right = rectangle->x + rectangle->width;
            const gint old_bottom = rectangle->y + rectangle->height;
            rectangle->x = std::min(rectangle->x, x);
            rectangle->y = std::min(rectangle->y, y);
            rectangle->width = std::max(old_right, right) - rectangle->x;
            rectangle->height = std::max(old_bottom, bottom) - rectangle->y;
        }
    }
}

gint gtk_accessibility_text_offset_at_point(AtkText *text, gint x, gint y, AtkCoordType coords) {
    const auto *node = gtk_accessibility_text_node(text);
    auto *element = reinterpret_cast<NkAccessibilityElement *>(ATK_OBJECT(text));
    if (!node || !element)
        return -1;
    gint origin_x = 0;
    gint origin_y = 0;
    accessibility_origin(*element, coords, origin_x, origin_y);
    for (const auto &range : node->text_ranges) {
        const gint left = origin_x + static_cast<gint>(range.x);
        const gint top = origin_y + static_cast<gint>(range.y);
        if (x >= left && y >= top && x < left + static_cast<gint>(range.width) &&
            y < top + static_cast<gint>(range.height))
            return static_cast<gint>(range.start - node->text_start);
    }
    return -1;
}

gint gtk_accessibility_text_selection_count(AtkText *text) {
    const auto *node = gtk_accessibility_text_node(text);
    return node && node->selection_start != NK_ACCESSIBILITY_TEXT_POSITION_NONE &&
                   node->selection_end != NK_ACCESSIBILITY_TEXT_POSITION_NONE
               ? 1
               : 0;
}

gchar *gtk_accessibility_text_selection(AtkText *text, gint index, gint *start, gint *end) {
    const auto *node = gtk_accessibility_text_node(text);
    if (!node || index != 0 || gtk_accessibility_text_selection_count(text) == 0)
        return g_strdup("");
    const gint local_start = static_cast<gint>(node->selection_start - node->text_start);
    const gint local_end = static_cast<gint>(node->selection_end - node->text_start);
    if (start)
        *start = local_start;
    if (end)
        *end = local_end;
    return gtk_accessibility_text_slice(node, local_start, local_end);
}

gboolean gtk_accessibility_text_set_selection(AtkText *text, gint index, gint start, gint end) {
    auto *element = reinterpret_cast<NkAccessibilityElement *>(ATK_OBJECT(text));
    const auto *node = gtk_accessibility_text_node(text);
    const gint length = gtk_accessibility_text_length(node);
    if (!element || !node || index != 0 || start < 0 || end < start || end > length ||
        !(node->actions & NK_ACCESSIBILITY_CAN_SET_SELECTION))
        return FALSE;
    const auto absolute_start =
        node->text_start + static_cast<nk_accessibility_text_position>(start);
    const auto absolute_end = node->text_start + static_cast<nk_accessibility_text_position>(end);
    return emit_gtk_accessibility_action(element->root, element->id,
                                         NK_ACCESSIBILITY_ACTION_SET_SELECTION, {}, absolute_start,
                                         absolute_end) == NK_OK;
}

gboolean gtk_accessibility_text_add_selection(AtkText *text, gint start, gint end) {
    return gtk_accessibility_text_set_selection(text, 0, start, end);
}

gboolean gtk_accessibility_text_remove_selection(AtkText *, gint) {
    return FALSE;
}

gboolean gtk_accessibility_text_set_caret(AtkText *text, gint offset) {
    return gtk_accessibility_text_set_selection(text, 0, offset, offset);
}

AtkTextGranularity gtk_accessibility_text_legacy_granularity(AtkTextBoundary boundary) {
    switch (boundary) {
    case ATK_TEXT_BOUNDARY_WORD_START:
    case ATK_TEXT_BOUNDARY_WORD_END:
        return ATK_TEXT_GRANULARITY_WORD;
    case ATK_TEXT_BOUNDARY_SENTENCE_START:
    case ATK_TEXT_BOUNDARY_SENTENCE_END:
        return ATK_TEXT_GRANULARITY_SENTENCE;
    case ATK_TEXT_BOUNDARY_LINE_START:
    case ATK_TEXT_BOUNDARY_LINE_END:
        return ATK_TEXT_GRANULARITY_LINE;
    case ATK_TEXT_BOUNDARY_CHAR:
    default:
        return ATK_TEXT_GRANULARITY_CHAR;
    }
}

gchar *gtk_accessibility_text_after_offset(AtkText *text, gint offset, AtkTextBoundary boundary,
                                           gint *start, gint *end) {
    const auto *node = gtk_accessibility_text_node(text);
    gint local_start = 0;
    gint local_end = 0;
    gtk_accessibility_text_boundary(node, offset + 1,
                                    gtk_accessibility_text_legacy_granularity(boundary),
                                    &local_start, &local_end);
    if (start)
        *start = local_start;
    if (end)
        *end = local_end;
    return gtk_accessibility_text_slice(node, local_start, local_end);
}

gchar *gtk_accessibility_text_at_offset(AtkText *text, gint offset, AtkTextBoundary boundary,
                                        gint *start, gint *end) {
    return gtk_accessibility_text_string_at_offset(
        text, offset, gtk_accessibility_text_legacy_granularity(boundary), start, end);
}

gchar *gtk_accessibility_text_before_offset(AtkText *text, gint offset, AtkTextBoundary boundary,
                                            gint *start, gint *end) {
    const auto *node = gtk_accessibility_text_node(text);
    gint local_start = 0;
    gint local_end = 0;
    gtk_accessibility_text_boundary(node, std::max(offset - 1, 0),
                                    gtk_accessibility_text_legacy_granularity(boundary),
                                    &local_start, &local_end);
    if (start)
        *start = local_start;
    if (end)
        *end = local_end;
    return gtk_accessibility_text_slice(node, local_start, local_end);
}

void gtk_accessibility_text_init(AtkTextIface *iface) {
    iface->get_text = gtk_accessibility_text_get;
    iface->get_text_after_offset = gtk_accessibility_text_after_offset;
    iface->get_text_at_offset = gtk_accessibility_text_at_offset;
    iface->get_text_before_offset = gtk_accessibility_text_before_offset;
    iface->get_character_at_offset = gtk_accessibility_text_character;
    iface->get_caret_offset = gtk_accessibility_text_caret;
    iface->get_character_extents = gtk_accessibility_text_extents;
    iface->get_character_count = gtk_accessibility_text_count;
    iface->get_offset_at_point = gtk_accessibility_text_offset_at_point;
    iface->get_n_selections = gtk_accessibility_text_selection_count;
    iface->get_selection = gtk_accessibility_text_selection;
    iface->add_selection = gtk_accessibility_text_add_selection;
    iface->remove_selection = gtk_accessibility_text_remove_selection;
    iface->set_selection = gtk_accessibility_text_set_selection;
    iface->set_caret_offset = gtk_accessibility_text_set_caret;
    iface->get_range_extents = gtk_accessibility_text_range_extents;
    iface->get_string_at_offset = gtk_accessibility_text_string_at_offset;
}

void accessibility_origin(const NkAccessibilityElement &element, AtkCoordType coords, gint &x,
                          gint &y) {
    x = 0;
    y = 0;
    auto *root = element.root;
    if (!root || !root->widget)
        return;
    if (coords == ATK_XY_SCREEN) {
        if (auto *native = gtk_widget_get_window(root->widget))
            gdk_window_get_origin(native, &x, &y);
    } else if (coords == ATK_XY_WINDOW) {
        auto *top = gtk_widget_get_toplevel(root->widget);
        if (top && top != root->widget)
            gtk_widget_translate_coordinates(root->widget, top, 0, 0, &x, &y);
    }
}

void gtk_accessibility_component_extents(AtkComponent *component, gint *x, gint *y, gint *width,
                                         gint *height, AtkCoordType coords) {
    auto *element = reinterpret_cast<NkAccessibilityElement *>(ATK_OBJECT(component));
    const auto *node = accessibility_node(element);
    gint origin_x = 0;
    gint origin_y = 0;
    if (element)
        accessibility_origin(*element, coords, origin_x, origin_y);
    if (x)
        *x = origin_x + (node ? static_cast<gint>(node->x) : 0);
    if (y)
        *y = origin_y + (node ? static_cast<gint>(node->y) : 0);
    if (width)
        *width = node ? static_cast<gint>(node->width) : 0;
    if (height)
        *height = node ? static_cast<gint>(node->height) : 0;
}

gboolean gtk_accessibility_component_contains(AtkComponent *component, gint x, gint y,
                                              AtkCoordType coords) {
    gint left = 0;
    gint top = 0;
    gint width = 0;
    gint height = 0;
    gtk_accessibility_component_extents(component, &left, &top, &width, &height, coords);
    return x >= left && y >= top && x < left + width && y < top + height;
}

gboolean gtk_accessibility_component_grab_focus(AtkComponent *component) {
    auto *element = reinterpret_cast<NkAccessibilityElement *>(ATK_OBJECT(component));
    return element && emit_gtk_accessibility_action(element->root, element->id,
                                                    NK_ACCESSIBILITY_ACTION_FOCUS) == NK_OK;
}

gboolean gtk_accessibility_component_scroll_to(AtkComponent *component, AtkScrollType) {
    auto *element = reinterpret_cast<NkAccessibilityElement *>(ATK_OBJECT(component));
    return element &&
           emit_gtk_accessibility_action(element->root, element->id,
                                         NK_ACCESSIBILITY_ACTION_SCROLL_INTO_VIEW) == NK_OK;
}

gboolean gtk_accessibility_component_scroll_to_point(AtkComponent *component, AtkCoordType, gint,
                                                     gint) {
    return gtk_accessibility_component_scroll_to(component, ATK_SCROLL_ANYWHERE);
}

AtkObject *gtk_accessibility_component_at_point(AtkComponent *component, gint x, gint y,
                                                AtkCoordType coords) {
    auto *element = reinterpret_cast<NkAccessibilityElement *>(ATK_OBJECT(component));
    if (!element || !gtk_accessibility_component_contains(component, x, y, coords))
        return nullptr;
    for (guint index = 0; element->children && index < element->children->len; ++index) {
        auto *child = ATK_COMPONENT(g_ptr_array_index(element->children, index));
        if (gtk_accessibility_component_contains(child, x, y, coords))
            return reinterpret_cast<AtkObject *>(g_object_ref(child));
    }
    return reinterpret_cast<AtkObject *>(g_object_ref(element));
}

AtkLayer gtk_accessibility_component_layer(AtkComponent *) {
    return ATK_LAYER_WIDGET;
}

gint gtk_accessibility_component_mdi_zorder(AtkComponent *) {
    return 0;
}

gdouble gtk_accessibility_component_alpha(AtkComponent *) {
    return 1.0;
}

void gtk_accessibility_component_init(AtkComponentIface *iface) {
    iface->contains = gtk_accessibility_component_contains;
    iface->ref_accessible_at_point = gtk_accessibility_component_at_point;
    iface->get_extents = gtk_accessibility_component_extents;
    iface->get_position = [](AtkComponent *component, gint *x, gint *y, AtkCoordType coords) {
        gtk_accessibility_component_extents(component, x, y, nullptr, nullptr, coords);
    };
    iface->get_size = [](AtkComponent *component, gint *width, gint *height) {
        gtk_accessibility_component_extents(component, nullptr, nullptr, width, height,
                                            ATK_XY_PARENT);
    };
    iface->grab_focus = gtk_accessibility_component_grab_focus;
    iface->get_layer = gtk_accessibility_component_layer;
    iface->get_mdi_zorder = gtk_accessibility_component_mdi_zorder;
    iface->get_alpha = gtk_accessibility_component_alpha;
    iface->scroll_to = gtk_accessibility_component_scroll_to;
    iface->scroll_to_point = gtk_accessibility_component_scroll_to_point;
}

void gtk_accessibility_value_current(AtkValue *value, GValue *out) {
    const auto *node = accessibility_node_from_object(ATK_OBJECT(value));
    if (!out)
        return;
    g_value_init(out, G_TYPE_DOUBLE);
    g_value_set_double(out, node ? node->numeric_value : 0.0);
}

void gtk_accessibility_value_maximum(AtkValue *value, GValue *out) {
    const auto *node = accessibility_node_from_object(ATK_OBJECT(value));
    if (!out)
        return;
    g_value_init(out, G_TYPE_DOUBLE);
    g_value_set_double(out, node ? node->numeric_maximum : 0.0);
}

void gtk_accessibility_value_minimum(AtkValue *value, GValue *out) {
    const auto *node = accessibility_node_from_object(ATK_OBJECT(value));
    if (!out)
        return;
    g_value_init(out, G_TYPE_DOUBLE);
    g_value_set_double(out, node ? node->numeric_minimum : 0.0);
}

void gtk_accessibility_value_set(AtkValue *value, gdouble new_value);

gboolean gtk_accessibility_value_set_current(AtkValue *value, const GValue *new_value) {
    if (!new_value || !G_VALUE_HOLDS_DOUBLE(new_value))
        return FALSE;
    gtk_accessibility_value_set(ATK_VALUE(value), g_value_get_double(new_value));
    return TRUE;
}

void gtk_accessibility_value_and_text(AtkValue *value, gdouble *out_value, gchar **out_text) {
    const auto *node = accessibility_node_from_object(ATK_OBJECT(value));
    if (out_value)
        *out_value = node ? node->numeric_value : 0.0;
    if (out_text)
        *out_text = node && !node->value.empty() ? g_strdup(node->value.c_str()) : nullptr;
}

AtkRange *gtk_accessibility_value_range(AtkValue *value) {
    const auto *node = accessibility_node_from_object(ATK_OBJECT(value));
    return node ? atk_range_new(node->numeric_minimum, node->numeric_maximum, nullptr) : nullptr;
}

gdouble gtk_accessibility_value_increment(AtkValue *) {
    return 0.0;
}

void gtk_accessibility_value_set(AtkValue *value, const gdouble new_value) {
    auto *element = reinterpret_cast<NkAccessibilityElement *>(ATK_OBJECT(value));
    if (!element)
        return;
    char text[G_ASCII_DTOSTR_BUF_SIZE];
    g_ascii_dtostr(text, sizeof(text), new_value);
    emit_gtk_accessibility_action(element->root, element->id, NK_ACCESSIBILITY_ACTION_SET_VALUE,
                                  text);
}

void gtk_accessibility_value_init(AtkValueIface *iface) {
    iface->get_current_value = gtk_accessibility_value_current;
    iface->get_maximum_value = gtk_accessibility_value_maximum;
    iface->get_minimum_value = gtk_accessibility_value_minimum;
    iface->set_current_value = gtk_accessibility_value_set_current;
    iface->get_value_and_text = gtk_accessibility_value_and_text;
    iface->get_range = gtk_accessibility_value_range;
    iface->get_increment = gtk_accessibility_value_increment;
    iface->set_value = gtk_accessibility_value_set;
}

void refresh_gtk_accessibility(GtkSurfaceResource &resource) {
    if (!resource.widget)
        return;
    auto *root =
        reinterpret_cast<NkAccessibilityRoot *>(gtk_widget_get_accessible(resource.widget));
    if (!root || !root->children)
        return;
    g_ptr_array_set_size(root->children, 0);
    std::function<void(AtkObject *, GPtrArray *, nk_accessibility_node_id)> append_children =
        [&](AtkObject *parent, GPtrArray *children, nk_accessibility_node_id parent_id) {
            std::vector<nk_accessibility_node_id> ids;
            for (const auto &[id, node] : resource.accessibility_nodes)
                if (node.parent == parent_id)
                    ids.push_back(id);
            std::sort(ids.begin(), ids.end(), [&](auto left, auto right) {
                const auto &a = resource.accessibility_nodes.at(left);
                const auto &b = resource.accessibility_nodes.at(right);
                return a.child_index == b.child_index ? left < right
                                                      : a.child_index < b.child_index;
            });
            for (const auto id : ids) {
                auto *element = static_cast<NkAccessibilityElement *>(
                    g_object_new(nk_accessibility_element_get_type(), nullptr));
                if (!element)
                    continue;
                element->root = root;
                element->id = id;
                atk_object_initialize(ATK_OBJECT(element), resource.widget);
                atk_object_set_parent(ATK_OBJECT(element), parent);
                g_ptr_array_add(children, element);
                append_children(ATK_OBJECT(element), element->children, id);
            }
        };
    append_children(ATK_OBJECT(root), root->children, NK_ACCESSIBILITY_ROOT);
    g_signal_emit_by_name(root, "children-changed", 0u, nullptr);
}

bool copy_gtk_accessibility_node(
    const nk_accessibility_node &node,
    const std::unordered_map<nk_accessibility_node_id, GtkAccessibilityNode> &nodes,
    GtkAccessibilityNode &copy) {
    const char *value = node.value ? node.value : "";
    if (node.struct_size < sizeof(node) || node.id == NK_ACCESSIBILITY_ROOT ||
        node.role > NK_ACCESSIBILITY_ALERT ||
        node.orientation > NK_ACCESSIBILITY_ORIENTATION_VERTICAL ||
        !g_utf8_validate(value, -1, nullptr) ||
        (node.label && !g_utf8_validate(node.label, -1, nullptr)) ||
        (node.states &
         ~(NK_ACCESSIBILITY_FOCUSABLE | NK_ACCESSIBILITY_FOCUSED | NK_ACCESSIBILITY_SELECTED |
           NK_ACCESSIBILITY_CHECKED | NK_ACCESSIBILITY_DISABLED | NK_ACCESSIBILITY_READ_ONLY |
           NK_ACCESSIBILITY_MULTILINE | NK_ACCESSIBILITY_PASSWORD | NK_ACCESSIBILITY_EXPANDED |
           NK_ACCESSIBILITY_MODAL | NK_ACCESSIBILITY_REQUIRED | NK_ACCESSIBILITY_INVALID |
           NK_ACCESSIBILITY_BUSY | NK_ACCESSIBILITY_HAS_POPUP)) ||
        (node.actions &
         ~(NK_ACCESSIBILITY_CAN_ACTIVATE | NK_ACCESSIBILITY_CAN_FOCUS |
           NK_ACCESSIBILITY_CAN_SET_VALUE | NK_ACCESSIBILITY_CAN_SET_SELECTION |
           NK_ACCESSIBILITY_CAN_INCREMENT | NK_ACCESSIBILITY_CAN_DECREMENT |
           NK_ACCESSIBILITY_CAN_SCROLL_FORWARD | NK_ACCESSIBILITY_CAN_SCROLL_BACKWARD |
           NK_ACCESSIBILITY_CAN_MOVE_NEXT | NK_ACCESSIBILITY_CAN_MOVE_PREVIOUS |
           NK_ACCESSIBILITY_CAN_TOGGLE | NK_ACCESSIBILITY_CAN_SELECT |
           NK_ACCESSIBILITY_CAN_DESELECT | NK_ACCESSIBILITY_CAN_EXPAND |
           NK_ACCESSIBILITY_CAN_COLLAPSE | NK_ACCESSIBILITY_CAN_DISMISS |
           NK_ACCESSIBILITY_CAN_SHOW_CONTEXT_MENU | NK_ACCESSIBILITY_CAN_SCROLL_INTO_VIEW)) ||
        !std::isfinite(node.x) || !std::isfinite(node.y) || !std::isfinite(node.width) ||
        !std::isfinite(node.height) || node.width < 0 || node.height < 0 ||
        !std::isfinite(node.numeric_value) || !std::isfinite(node.numeric_minimum) ||
        !std::isfinite(node.numeric_maximum) ||
        (node.role == NK_ACCESSIBILITY_SLIDER &&
         (node.numeric_minimum > node.numeric_maximum ||
          node.numeric_value < node.numeric_minimum || node.numeric_value > node.numeric_maximum)))
        return false;
    const auto value_length = static_cast<uint64_t>(g_utf8_strlen(value, -1));
    const uint64_t text_end = static_cast<uint64_t>(node.text_start) + value_length;
    const bool no_selection = node.selection_start == NK_ACCESSIBILITY_TEXT_POSITION_NONE &&
                              node.selection_end == NK_ACCESSIBILITY_TEXT_POSITION_NONE;
    const bool valid_selection = node.selection_start != NK_ACCESSIBILITY_TEXT_POSITION_NONE &&
                                 node.selection_end != NK_ACCESSIBILITY_TEXT_POSITION_NONE &&
                                 node.selection_start <= node.selection_end &&
                                 node.selection_start >= node.text_start &&
                                 node.selection_end <= text_end;
    if (text_end > node.document_length || (!no_selection && !valid_selection) ||
        (node.parent_id != NK_ACCESSIBILITY_ROOT && nodes.find(node.parent_id) == nodes.end()))
        return false;
    auto ancestor = node.parent_id;
    for (std::size_t depth = 0; ancestor != NK_ACCESSIBILITY_ROOT; ++depth) {
        if (ancestor == node.id || depth > nodes.size())
            return false;
        const auto parent = nodes.find(ancestor);
        if (parent == nodes.end())
            break;
        ancestor = parent->second.parent;
    }
    copy.id = node.id;
    copy.parent = node.parent_id;
    copy.child_index = node.child_index;
    copy.role = node.role;
    copy.states = node.states;
    copy.actions = node.actions;
    copy.x = node.x;
    copy.y = node.y;
    copy.width = node.width;
    copy.height = node.height;
    copy.label = node.label ? node.label : "";
    copy.value = value;
    copy.numeric_value = node.numeric_value;
    copy.numeric_minimum = node.numeric_minimum;
    copy.numeric_maximum = node.numeric_maximum;
    copy.text_start = node.text_start;
    copy.document_length = node.document_length;
    copy.selection_start = node.selection_start;
    copy.selection_end = node.selection_end;
    copy.set_size = node.set_size;
    copy.position_in_set = node.position_in_set;
    copy.row_count = node.row_count;
    copy.column_count = node.column_count;
    copy.row_index = node.row_index;
    copy.column_index = node.column_index;
    copy.row_span = node.row_span;
    copy.column_span = node.column_span;
    copy.hierarchy_level = node.hierarchy_level;
    copy.orientation = node.orientation;
    return true;
}

void remove_gtk_accessibility_descendants(
    std::unordered_map<nk_accessibility_node_id, GtkAccessibilityNode> &nodes,
    nk_accessibility_node_id node) {
    std::vector<nk_accessibility_node_id> pending{node};
    for (std::size_t index = 0; index < pending.size(); ++index)
        for (const auto &[id, value] : nodes)
            if (value.parent == pending[index])
                pending.push_back(id);
    for (const auto id : pending)
        nodes.erase(id);
}

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
    std::string text;
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
std::unordered_map<GdkMonitor *, nk_orientation> monitor_orientations;
std::unordered_map<nk_request_id, DialogContext *> dialogs;
std::unordered_map<nk_request_id, NavigationDecision> navigation_decisions;
std::unordered_map<nk_request_id, nk_handle> evaluations;
std::unordered_map<nk_request_id, NotificationRequest> notifications;
std::unordered_map<uint32_t, nk_request_id> notification_ids;
GDBusConnection *notification_bus = nullptr;
guint notification_action_subscription = 0;
guint notification_closed_subscription = 0;
GDBusConnection *keep_awake_bus = nullptr;
gchar *keep_awake_handle = nullptr;

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
    case GDK_KEY_space:
        return NK_KEY_SPACE;
    case GDK_KEY_apostrophe:
        return NK_KEY_APOSTROPHE;
    case GDK_KEY_comma:
        return NK_KEY_COMMA;
    case GDK_KEY_minus:
        return NK_KEY_MINUS;
    case GDK_KEY_period:
        return NK_KEY_PERIOD;
    case GDK_KEY_slash:
        return NK_KEY_SLASH;
    case GDK_KEY_semicolon:
        return NK_KEY_SEMICOLON;
    case GDK_KEY_equal:
        return NK_KEY_EQUAL;
    case GDK_KEY_bracketleft:
        return NK_KEY_LEFT_BRACKET;
    case GDK_KEY_backslash:
        return NK_KEY_BACKSLASH;
    case GDK_KEY_bracketright:
        return NK_KEY_RIGHT_BRACKET;
    case GDK_KEY_grave:
        return NK_KEY_GRAVE_ACCENT;
    case GDK_KEY_Escape:
        return NK_KEY_ESCAPE;
    case GDK_KEY_Return:
        return NK_KEY_ENTER;
    case GDK_KEY_KP_Enter:
        return NK_KEY_KP_ENTER;
    case GDK_KEY_KP_Decimal:
        return NK_KEY_KP_DECIMAL;
    case GDK_KEY_KP_Divide:
        return NK_KEY_KP_DIVIDE;
    case GDK_KEY_KP_Multiply:
        return NK_KEY_KP_MULTIPLY;
    case GDK_KEY_KP_Subtract:
        return NK_KEY_KP_SUBTRACT;
    case GDK_KEY_KP_Add:
        return NK_KEY_KP_ADD;
    case GDK_KEY_KP_Equal:
        return NK_KEY_KP_EQUAL;
    case GDK_KEY_Tab:
    case GDK_KEY_ISO_Left_Tab:
        return NK_KEY_TAB;
    case GDK_KEY_BackSpace:
        return NK_KEY_BACKSPACE;
    case GDK_KEY_Insert:
        return NK_KEY_INSERT;
    case GDK_KEY_Delete:
        return NK_KEY_DELETE;
    case GDK_KEY_Right:
        return NK_KEY_RIGHT;
    case GDK_KEY_Left:
        return NK_KEY_LEFT;
    case GDK_KEY_Down:
        return NK_KEY_DOWN;
    case GDK_KEY_Up:
        return NK_KEY_UP;
    case GDK_KEY_Page_Up:
        return NK_KEY_PAGE_UP;
    case GDK_KEY_Page_Down:
        return NK_KEY_PAGE_DOWN;
    case GDK_KEY_Home:
        return NK_KEY_HOME;
    case GDK_KEY_End:
        return NK_KEY_END;
    case GDK_KEY_Caps_Lock:
        return NK_KEY_CAPS_LOCK;
    case GDK_KEY_Scroll_Lock:
        return NK_KEY_SCROLL_LOCK;
    case GDK_KEY_Num_Lock:
        return NK_KEY_NUM_LOCK;
    case GDK_KEY_Print:
        return NK_KEY_PRINT_SCREEN;
    case GDK_KEY_Pause:
        return NK_KEY_PAUSE;
    case GDK_KEY_Shift_L:
        return NK_KEY_LEFT_SHIFT;
    case GDK_KEY_Control_L:
        return NK_KEY_LEFT_CONTROL;
    case GDK_KEY_Alt_L:
        return NK_KEY_LEFT_ALT;
    case GDK_KEY_Super_L:
        return NK_KEY_LEFT_SUPER;
    case GDK_KEY_Shift_R:
        return NK_KEY_RIGHT_SHIFT;
    case GDK_KEY_Control_R:
        return NK_KEY_RIGHT_CONTROL;
    case GDK_KEY_Alt_R:
        return NK_KEY_RIGHT_ALT;
    case GDK_KEY_Super_R:
        return NK_KEY_RIGHT_SUPER;
    case GDK_KEY_Menu:
        return NK_KEY_MENU;
    default:
        return NK_KEY_UNKNOWN;
    }
}

nk_pointer_button button_from_gdk(guint button) {
    switch (button) {
    case 1:
        return NK_POINTER_BUTTON_LEFT;
    case 2:
        return NK_POINTER_BUTTON_MIDDLE;
    case 3:
        return NK_POINTER_BUTTON_RIGHT;
    case 8:
        return NK_POINTER_BUTTON_4;
    case 9:
        return NK_POINTER_BUTTON_5;
    default:
        return button > 0 && button <= NK_POINTER_BUTTON_LAST + 1 ? button - 1 : UINT32_MAX;
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
            if (codepoint == static_cast<gunichar>(-1) || codepoint == static_cast<gunichar>(-2))
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
        nk_input_action action =
            key_event->type == GDK_KEY_RELEASE ? NK_INPUT_RELEASE : NK_INPUT_PRESS;
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
        apply_pointer_cursor(*resource);
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
    // GTK emits GDK_2BUTTON_PRESS/GDK_3BUTTON_PRESS in addition to the ordinary
    // GDK_BUTTON_PRESS for that physical click. Forwarding both makes a double-click
    // look like a triple-click to consumers that count press/release transitions.
    if (button_event->type == GDK_2BUTTON_PRESS || button_event->type == GDK_3BUTTON_PRESS)
        return TRUE;

    nk::core::callback_boundary([&] {
        auto *resource = static_cast<GtkWindowResource *>(data);
        if (begin_decoration_drag(*resource, *button_event))
            return;
        const auto button = button_from_gdk(button_event->button);
        if (button == UINT32_MAX)
            return;
        const nk_input_action action =
            button_event->type == GDK_BUTTON_RELEASE ? NK_INPUT_RELEASE : NK_INPUT_PRESS;
        resource->buttons[button] = action;
        resource->pointer_x = button_event->x;
        resource->pointer_y = button_event->y;
        const nk_pointer_button_event payload{
            button, action,          modifiers(static_cast<GdkModifierType>(button_event->state)),
            0,      button_event->x, button_event->y};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_POINTER_BUTTON;
        event.source = resource->handle;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    });
    return TRUE;
}

gboolean on_pointer_scroll(GtkWidget *, GdkEventScroll *scroll, gpointer data) {
    nk::core::callback_boundary([&] {
        constexpr double logical_pixels_per_scroll_unit = 40.0;
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
        x *= logical_pixels_per_scroll_unit;
        y *= logical_pixels_per_scroll_unit;
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
    if (resource->hovered)
        apply_pointer_cursor(*resource);
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_POINTER_ENTER;
    event.source = resource->handle;
    event.flags = resource->hovered ? 1u : 0u;
    nk::core::push_event(std::move(event));
    return FALSE;
}

gboolean on_surface_tick(GtkWidget *widget, GdkFrameClock *, gpointer data) {
    auto *resource = static_cast<GtkSurfaceResource *>(data);
    if (!resource || !nk::core::is_runtime_generation(resource->generation) ||
        !resource->frame_callback) {
        if (resource)
            resource->frame_tick = 0;
        return G_SOURCE_REMOVE;
    }
    gtk_gl_area_queue_render(GTK_GL_AREA(widget));
    return G_SOURCE_CONTINUE;
}

gboolean on_surface_render(GtkGLArea *area, GdkGLContext *, gpointer data) {
    auto *resource = static_cast<GtkSurfaceResource *>(data);
    if (!resource || !nk::core::is_runtime_generation(resource->generation) ||
        !resource->frame_callback)
        return TRUE;
    const int scale = gtk_widget_get_scale_factor(GTK_WIDGET(area));
    nk::core::callback_boundary([&] {
        const auto callback = resource->frame_callback;
        void *user_data = resource->frame_user_data;
        if (!callback || !nk::core::is_runtime_generation(resource->generation))
            return;
        callback(resource->handle, gtk_widget_get_allocated_width(GTK_WIDGET(area)) * scale,
                 gtk_widget_get_allocated_height(GTK_WIDGET(area)) * scale, user_data);
    });
    return TRUE;
}

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
        gdk_gl_context_set_debug_enabled(context,
                                         (resource->flags & NK_SURFACE_DEBUG_CONTEXT) != 0);
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
            const nk_window_framebuffer_resize_event framebuffer{configure->width * scale,
                                                                 configure->height * scale};
            nk::core::QueuedEvent framebuffer_event;
            framebuffer_event.kind = NK_EVENT_WINDOW_FRAMEBUFFER_RESIZE;
            framebuffer_event.source = resource->handle;
            framebuffer_event.data = bytes_of(framebuffer);
            nk::core::push_event(std::move(framebuffer_event));
        }
        bool position_available = true;
#ifdef GDK_WINDOWING_WAYLAND
        position_available = !GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(resource->window));
#endif
        if (position_available && (!resource->geometry_known || resource->x != configure->x ||
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
                const nk_pointer_button_event released{
                    button, NK_INPUT_RELEASE, 0, 0, resource->pointer_x, resource->pointer_y};
                nk::core::QueuedEvent release_event;
                release_event.kind = NK_EVENT_POINTER_BUTTON;
                release_event.source = resource->handle;
                release_event.flags = 1u;
                release_event.data = bytes_of(released);
                nk::core::push_event(std::move(release_event));
            }
        }
        uint32_t flags =
            resource->state_flags & (NK_WINDOW_STATE_VISIBLE | NK_WINDOW_STATE_ATTENTION_REQUESTED);
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

nk_window_decoration_region_kind
decoration_region_at(const std::vector<nk_window_decoration_region> &regions, float x, float y) {
    for (auto iter = regions.rbegin(); iter != regions.rend(); ++iter) {
        if (x >= iter->x && y >= iter->y && x < iter->x + iter->width && y < iter->y + iter->height)
            return iter->kind;
    }
    return NK_WINDOW_DECORATION_CLIENT;
}

uint32_t decoration_cursor_shape_at(const std::vector<nk_window_decoration_region> &regions,
                                    float x, float y) {
    for (auto iter = regions.rbegin(); iter != regions.rend(); ++iter) {
        if (x >= iter->x && y >= iter->y && x < iter->x + iter->width && y < iter->y + iter->height)
            return iter->cursor_shape;
    }
    return 0;
}

bool begin_decoration_drag(GtkWindowResource &resource, GdkEventButton &event) {
    if (resource.decorated || resource.wrapped || event.type != GDK_BUTTON_PRESS ||
        event.button != 1)
        return false;
    const auto kind = decoration_region_at(resource.decoration_regions, static_cast<float>(event.x),
                                           static_cast<float>(event.y));
    if (kind == NK_WINDOW_DECORATION_DRAG) {
        gtk_window_begin_move_drag(GTK_WINDOW(resource.window), event.button, event.x_root,
                                   event.y_root, event.time);
        return true;
    }
    GdkWindowEdge edge = GDK_WINDOW_EDGE_NORTH;
    switch (kind) {
    case NK_WINDOW_DECORATION_RESIZE_NORTH:
        edge = GDK_WINDOW_EDGE_NORTH;
        break;
    case NK_WINDOW_DECORATION_RESIZE_SOUTH:
        edge = GDK_WINDOW_EDGE_SOUTH;
        break;
    case NK_WINDOW_DECORATION_RESIZE_WEST:
        edge = GDK_WINDOW_EDGE_WEST;
        break;
    case NK_WINDOW_DECORATION_RESIZE_EAST:
        edge = GDK_WINDOW_EDGE_EAST;
        break;
    case NK_WINDOW_DECORATION_RESIZE_NORTHWEST:
        edge = GDK_WINDOW_EDGE_NORTH_WEST;
        break;
    case NK_WINDOW_DECORATION_RESIZE_NORTHEAST:
        edge = GDK_WINDOW_EDGE_NORTH_EAST;
        break;
    case NK_WINDOW_DECORATION_RESIZE_SOUTHWEST:
        edge = GDK_WINDOW_EDGE_SOUTH_WEST;
        break;
    case NK_WINDOW_DECORATION_RESIZE_SOUTHEAST:
        edge = GDK_WINDOW_EDGE_SOUTH_EAST;
        break;
    case NK_WINDOW_DECORATION_CLIENT:
    default:
        return false;
    }
    if (!resource.resizable)
        return false;
    gtk_window_begin_resize_drag(GTK_WINDOW(resource.window), edge, event.button, event.x_root,
                                 event.y_root, event.time);
    return true;
}

nk_result require_gdk_wrapper(const GtkWindowResource &resource) {
    if (resource.wrapped && !resource.foreign_window)
        return fail(
            NK_ERROR_UNSUPPORTED,
            "Wayland native wrappers only support native descriptor access and destruction");
    return NK_OK;
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
    if (resource.wrapped && resource.foreign_window)
        gdk_window_set_geometry_hints(resource.foreign_window, &geometry, hints);
    else if (!resource.wrapped)
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

std::shared_ptr<GtkWindowResource> text_input_window(nk_handle target) {
    if (auto resource = window(target))
        return resource;
    auto graphics_surface = surface(target);
    return graphics_surface ? window(graphics_surface->parent) : nullptr;
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

nk_orientation monitor_orientation(GdkMonitor *native) {
    GdkRectangle geometry{};
    gdk_monitor_get_geometry(native, &geometry);
    if (geometry.width == geometry.height)
        return NK_ORIENTATION_UNKNOWN;
    return geometry.width > geometry.height ? NK_ORIENTATION_LANDSCAPE_RIGHT
                                            : NK_ORIENTATION_PORTRAIT;
}

void poll_monitor_orientations() {
    for (const auto &[native, handle] : monitor_handles) {
        const auto current = monitor_orientation(native);
        const auto found = monitor_orientations.find(native);
        if (found == monitor_orientations.end()) {
            monitor_orientations.emplace(native, current);
            continue;
        }
        if (found->second == current)
            continue;
        found->second = current;
        const nk_orientation_event payload{sizeof(payload), current, 0, {0, 0}};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_DISPLAY_ORIENTATION_CHANGED;
        event.source = handle;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    }
}

nk_handle register_monitor(GdkMonitor *native) {
    const auto found = monitor_handles.find(native);
    if (found != monitor_handles.end())
        return found->second;
    auto resource = std::make_shared<GtkMonitorResource>();
    resource->monitor = GDK_MONITOR(g_object_ref(native));
    resource->name = monitor_name(native);
    resource->handle = nk::core::handles().insert(nk::core::ResourceType::monitor, resource);
    if (resource->handle != NK_INVALID_HANDLE) {
        monitor_handles.emplace(native, resource->handle);
        monitor_orientations.emplace(native, monitor_orientation(native));
    }
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
        monitor_orientations.erase(native);
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
        if (register_monitor(gdk_display_get_monitor(monitor_display, index)) == NK_INVALID_HANDLE)
            return fail(NK_ERROR_OUT_OF_MEMORY, "monitor handle registry is full");
    }
    monitor_added_signal =
        g_signal_connect(monitor_display, "monitor-added", G_CALLBACK(on_monitor_added), nullptr);
    monitor_removed_signal = g_signal_connect(monitor_display, "monitor-removed",
                                              G_CALLBACK(on_monitor_removed), nullptr);
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

GdkWindow *native_window(GtkWindowResource &resource) {
    if (resource.wrapped)
        return resource.foreign_window;
    gtk_widget_realize(resource.window);
    return gtk_widget_get_window(resource.window);
}

nk_result apply_cursor(GtkWindowResource &resource) {
    if (const auto result = require_gdk_wrapper(resource); result != NK_OK)
        return result;
    GdkWindow *native = native_window(resource);
    if (!native)
        return fail(NK_ERROR_UNKNOWN, "GTK window is not realized");
    GdkDisplay *display = gdk_window_get_display(native);
    gdk_window_set_cursor(native, effective_cursor(resource, display));
    return NK_OK;
}

uint32_t default_decoration_cursor_shape(nk_window_decoration_region_kind kind) {
    switch (kind) {
    case NK_WINDOW_DECORATION_DRAG:
        return NK_CURSOR_MOVE;
    case NK_WINDOW_DECORATION_RESIZE_NORTH:
    case NK_WINDOW_DECORATION_RESIZE_SOUTH:
        return NK_CURSOR_VERTICAL_RESIZE;
    case NK_WINDOW_DECORATION_RESIZE_WEST:
    case NK_WINDOW_DECORATION_RESIZE_EAST:
        return NK_CURSOR_HORIZONTAL_RESIZE;
    case NK_WINDOW_DECORATION_RESIZE_NORTHWEST:
    case NK_WINDOW_DECORATION_RESIZE_SOUTHEAST:
        return NK_CURSOR_NWSE_RESIZE;
    case NK_WINDOW_DECORATION_RESIZE_NORTHEAST:
    case NK_WINDOW_DECORATION_RESIZE_SOUTHWEST:
        return NK_CURSOR_NESW_RESIZE;
    case NK_WINDOW_DECORATION_CLIENT:
    default:
        return 0;
    }
}

const char *cursor_name(uint32_t shape) {
    switch (shape) {
    case NK_CURSOR_ARROW:
        return "default";
    case NK_CURSOR_IBEAM:
        return "text";
    case NK_CURSOR_CROSSHAIR:
        return "crosshair";
    case NK_CURSOR_HAND:
        return "pointer";
    case NK_CURSOR_HORIZONTAL_RESIZE:
        return "ew-resize";
    case NK_CURSOR_VERTICAL_RESIZE:
        return "ns-resize";
    case NK_CURSOR_NWSE_RESIZE:
        return "nwse-resize";
    case NK_CURSOR_NESW_RESIZE:
        return "nesw-resize";
    case NK_CURSOR_MOVE:
        return "move";
    case NK_CURSOR_NOT_ALLOWED:
        return "not-allowed";
    default:
        return nullptr;
    }
}

GdkCursor *decoration_cursor(GtkWindowResource &resource, GdkDisplay *display, uint32_t shape) {
    if (resource.decoration_cursor_shape == shape)
        return resource.decoration_cursor;
    if (resource.decoration_cursor)
        g_object_unref(resource.decoration_cursor);
    resource.decoration_cursor = nullptr;
    resource.decoration_cursor_shape = shape;
    if (const auto *name = cursor_name(shape))
        resource.decoration_cursor = gdk_cursor_new_from_name(display, name);
    return resource.decoration_cursor;
}

nk_result apply_pointer_cursor(GtkWindowResource &resource) {
    if (resource.cursor_mode != NK_CURSOR_MODE_NORMAL || resource.decorated || resource.wrapped)
        return apply_cursor(resource);
    GdkWindow *native = native_window(resource);
    if (!native)
        return fail(NK_ERROR_UNKNOWN, "GTK window is not realized");
    GdkDisplay *display = gdk_window_get_display(native);
    const auto kind =
        decoration_region_at(resource.decoration_regions, static_cast<float>(resource.pointer_x),
                             static_cast<float>(resource.pointer_y));
    auto shape = decoration_cursor_shape_at(resource.decoration_regions,
                                            static_cast<float>(resource.pointer_x),
                                            static_cast<float>(resource.pointer_y));
    if (shape == 0)
        shape = default_decoration_cursor_shape(kind);
    if (shape == 0) {
        decoration_cursor(resource, display, 0);
        return apply_cursor(resource);
    }
    GdkCursor *cursor = effective_cursor(resource, display);
    if (auto *decoration = decoration_cursor(resource, display, shape))
        cursor = decoration;
    gdk_window_set_cursor(native, cursor);
    return NK_OK;
}

nk_result apply_cursor_mode(GtkWindowResource &resource, nk_cursor_mode mode) {
    if (mode > NK_CURSOR_MODE_DISABLED)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid cursor mode");
    if (mode == NK_CURSOR_MODE_DISABLED)
        return fail(NK_ERROR_UNSUPPORTED,
                    "GTK does not provide portable disabled relative pointer motion");
    if (const auto result = require_gdk_wrapper(resource); result != NK_OK)
        return result;
    GdkWindow *native = native_window(resource);
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
        const auto status = gdk_seat_grab(seat, native, GDK_SEAT_CAPABILITY_POINTER, TRUE,
                                          native_cursor, nullptr, nullptr, nullptr);
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
        if (request->event_kind == NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE) {
            std::vector<nk::platform::ResourceValue> resources;
            for (gchar **uri = uris; uri && *uri; ++uri) {
                if (!*uri || !**uri)
                    continue;
                resources.push_back(nk::platform::resource_from_uri(*uri, NK_RESOURCE_READABLE));
            }
            nk::core::QueuedEvent event;
            event.kind = request->event_kind;
            event.request_id = request->request;
            event.data_count = static_cast<uint32_t>(resources.size());
            event.data = nk::platform::resource_payload(false, resources);
            nk::core::push_event(std::move(event));
            return;
        }
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

void provide_clipboard_files(GtkClipboard *, GtkSelectionData *selection, guint info,
                             gpointer data) {
    auto *owner = static_cast<ClipboardFileOwner *>(data);
    if (info == 2)
        gtk_selection_data_set_text(selection, owner->text.c_str(), -1);
    else
        gtk_selection_data_set_uris(selection, owner->pointers.data());
}

void clear_clipboard_files(GtkClipboard *, gpointer data) {
    clipboard_owned = false;
    delete static_cast<ClipboardFileOwner *>(data);
}

nk_result set_clipboard_uris(std::vector<std::string> uris, const char *text = nullptr) {
    if (!ensure_gtk())
        return NK_ERROR_UNSUPPORTED;
    auto owner = std::make_unique<ClipboardFileOwner>();
    owner->uris = std::move(uris);
    if (text)
        owner->text = text;
    owner->pointers.reserve(owner->uris.size() + 1);
    for (auto &uri : owner->uris)
        owner->pointers.push_back(uri.data());
    owner->pointers.push_back(nullptr);
    GtkTargetEntry targets[] = {{const_cast<gchar *>("text/uri-list"), 0, 1},
                                {const_cast<gchar *>("UTF8_STRING"), 0, 2}};
    const auto target_count = text ? 2u : 1u;
    if (!gtk_clipboard_set_with_data(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), targets,
                                     target_count, provide_clipboard_files, clear_clipboard_files,
                                     owner.get()))
        return fail(NK_ERROR_UNKNOWN, "desktop rejected clipboard ownership");
    owner.release();
    clipboard_owned = true;
    return NK_OK;
}

enum { drop_target_uri = 1, drop_target_text = 2 };

void on_drag_data_received(GtkWidget *, GdkDragContext *context, gint x, gint y,
                           GtkSelectionData *selection, guint info, guint time, gpointer data) {
    bool completed = false;
    nk::core::callback_boundary([&] {
        const auto *resource = static_cast<GtkWindowResource *>(data);
        std::vector<std::string> items;
        std::vector<nk::platform::ResourceValue> resources;
        nk_event_kind kind = NK_EVENT_DROP_TEXT;
        if (info == drop_target_uri) {
            kind = NK_EVENT_DROP_FILES;
            gchar **uris = gtk_selection_data_get_uris(selection);
            for (gchar **uri = uris; uri && *uri; ++uri) {
                if (*uri && **uri)
                    resources.push_back(
                        nk::platform::resource_from_uri(*uri, NK_RESOURCE_READABLE));
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
        if (!resources.empty()) {
            nk::core::QueuedEvent resource_event;
            resource_event.kind = NK_EVENT_RESOURCE_DROP;
            resource_event.source = resource->handle;
            resource_event.data_count = static_cast<uint32_t>(resources.size());
            resource_event.data = nk::platform::resource_drop_payload(
                static_cast<float>(x), static_cast<float>(y), {}, resources);
            nk::core::push_event(std::move(resource_event));
        }
        gtk_drag_finish(context, !items.empty() || !resources.empty(), FALSE, time);
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
    event.kind = NK_EVENT_DIALOG_RESOURCES_COMPLETE;
    event.request_id = context->request;
    event.flags = context->kind;
    event.data_count = static_cast<uint32_t>(paths.size());
    const auto access =
        context->kind == NK_DIALOG_OPEN_RESOURCE ? NK_RESOURCE_READABLE : NK_RESOURCE_WRITABLE;
    std::vector<nk::platform::ResourceValue> resources;
    resources.reserve(paths.size());
    for (auto &path : paths)
        resources.push_back(nk::platform::resource_from_file_path(std::move(path), access));
    event.data = nk::platform::resource_payload(accepted, resources);
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
            event.kind = NK_EVENT_DIALOG_MESSAGE_COMPLETE;
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
                event.kind = NK_EVENT_DIALOG_MESSAGE_COMPLETE;
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
        if (parent->wrapped)
            return fail(NK_ERROR_UNSUPPORTED,
                        "GTK file dialogs cannot use a wrapped window as their parent");
    }
    if (!ensure_gtk())
        return NK_ERROR_UNSUPPORTED;
    GtkFileChooserAction action = GTK_FILE_CHOOSER_ACTION_OPEN;
    if (kind == NK_DIALOG_SAVE_RESOURCE)
        action = GTK_FILE_CHOOSER_ACTION_SAVE;
    if (kind == NK_DIALOG_SELECT_RESOURCE_DIRECTORY)
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
        interface, kind == NK_DIALOG_OPEN_RESOURCE && (options->flags & NK_DIALOG_ALLOW_MULTIPLE));
    gtk_file_chooser_set_do_overwrite_confirmation(
        interface, (options->flags & NK_DIALOG_CONFIRM_OVERWRITE) != 0);
    gtk_file_chooser_set_show_hidden(interface, (options->flags & NK_DIALOG_SHOW_HIDDEN) != 0);
    if (options->initial_path) {
        char *resource_path = g_filename_from_uri(options->initial_path, nullptr, nullptr);
        if (resource_path && g_file_test(resource_path, G_FILE_TEST_IS_DIR))
            gtk_file_chooser_set_current_folder(interface, resource_path);
        else if (resource_path)
            gtk_file_chooser_set_filename(interface, resource_path);
        g_free(resource_path);
    }
    if (options->suggested_name && kind == NK_DIALOG_SAVE_RESOURCE)
        gtk_file_chooser_set_current_name(interface, options->suggested_name);
    if (kind != NK_DIALOG_SELECT_RESOURCE_DIRECTORY)
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

std::string system_directory_path(nk_system_directory_kind kind) {
    switch (kind) {
    case NK_DIRECTORY_HOME:
        return g_get_home_dir() ? g_get_home_dir() : "";
    case NK_DIRECTORY_DESKTOP:
        return g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP)
                   ? g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP)
                   : "";
    case NK_DIRECTORY_DOCUMENTS:
        return g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS)
                   ? g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS)
                   : "";
    case NK_DIRECTORY_DOWNLOADS:
        return g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD)
                   ? g_get_user_special_dir(G_USER_DIRECTORY_DOWNLOAD)
                   : "";
    case NK_DIRECTORY_CACHE:
        return g_get_user_cache_dir() ? g_get_user_cache_dir() : "";
    case NK_DIRECTORY_CONFIG:
        return g_get_user_config_dir() ? g_get_user_config_dir() : "";
    case NK_DIRECTORY_DATA:
        return g_get_user_data_dir() ? g_get_user_data_dir() : "";
    case NK_DIRECTORY_TEMP:
        return g_get_tmp_dir() ? g_get_tmp_dir() : "";
    case NK_DIRECTORY_APPLICATION: {
        std::array<char, 4096> executable{};
        const auto length = readlink("/proc/self/exe", executable.data(), executable.size() - 1);
        if (length <= 0)
            return {};
        executable[static_cast<std::size_t>(length)] = '\0';
        char *directory = g_path_get_dirname(executable.data());
        std::string result = directory ? directory : "";
        g_free(directory);
        return result;
    }
    case NK_DIRECTORY_APPLICATION_STORAGE: {
        const auto id = nk::core::system_application_id();
        const auto base = g_get_user_data_dir();
        if (id.empty() || !base)
            return {};
        return std::string(base) + G_DIR_SEPARATOR_S + id;
    }
    case NK_DIRECTORY_FONTS: {
        const auto *const *system_data = g_get_system_data_dirs();
        if (system_data) {
            for (std::size_t index = 0; system_data[index]; ++index) {
                const auto candidate =
                    std::string(system_data[index]) + G_DIR_SEPARATOR_S + "fonts";
                if (g_file_test(candidate.c_str(), G_FILE_TEST_IS_DIR))
                    return candidate;
            }
        }
        const auto candidate = std::string("/usr/share/fonts");
        return g_file_test(candidate.c_str(), G_FILE_TEST_IS_DIR) ? candidate : std::string();
    }
    default:
        return {};
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

namespace nk::core::system_backend {

bool keep_awake_supported() noexcept {
    GError *error = nullptr;
    auto *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!bus) {
        if (error)
            g_error_free(error);
        return false;
    }
    auto *reply = g_dbus_connection_call_sync(
        bus, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.DBus.Introspectable", "Introspect", nullptr, G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error);
    bool supported = false;
    if (reply) {
        const char *xml = nullptr;
        g_variant_get(reply, "(&s)", &xml);
        supported = xml && std::strstr(xml, "org.freedesktop.portal.Inhibit") != nullptr;
        g_variant_unref(reply);
    }
    if (error)
        g_error_free(error);
    g_object_unref(bus);
    return supported;
}

nk_result keep_awake_apply(bool enabled) noexcept {
    if (!enabled) {
        if (keep_awake_handle && keep_awake_bus) {
            g_dbus_connection_call_sync(keep_awake_bus, "org.freedesktop.portal.Desktop",
                                        keep_awake_handle, "org.freedesktop.portal.Request",
                                        "Close", nullptr, nullptr, G_DBUS_CALL_FLAGS_NONE, 1000,
                                        nullptr, nullptr);
        }
        g_clear_pointer(&keep_awake_handle, g_free);
        g_clear_object(&keep_awake_bus);
        return NK_OK;
    }
    if (keep_awake_handle)
        return NK_OK;
    GError *error = nullptr;
    keep_awake_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, &error);
    if (!keep_awake_bus) {
        if (error)
            g_error_free(error);
        return NK_ERROR_UNSUPPORTED;
    }
    GVariantBuilder options;
    g_variant_builder_init(&options, G_VARIANT_TYPE_VARDICT);
    GVariant *reply = g_dbus_connection_call_sync(
        keep_awake_bus, "org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop",
        "org.freedesktop.portal.Inhibit", "Inhibit", g_variant_new("(sua{sv})", "", 8u, &options),
        nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &error);
    if (!reply) {
        if (error)
            g_error_free(error);
        g_clear_object(&keep_awake_bus);
        return NK_ERROR_UNSUPPORTED;
    }
    g_variant_get(reply, "(o)", &keep_awake_handle);
    g_variant_unref(reply);
    if (!keep_awake_handle || !*keep_awake_handle) {
        g_clear_pointer(&keep_awake_handle, g_free);
        g_clear_object(&keep_awake_bus);
        return NK_ERROR_UNSUPPORTED;
    }
    return NK_OK;
}

} // namespace nk::core::system_backend

namespace nk::backend {
void pump_events() noexcept {
    nk::linux_joystick::pump();
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
    nk::core::callback_boundary([] { poll_monitor_orientations(); });
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
        monitor_orientations.clear();
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
    auto capabilities =
        NK_CAP_WINDOW | NK_CAP_WEBVIEW | NK_CAP_CLIPBOARD | NK_CAP_DRAG_DROP | NK_CAP_SHELL |
        NK_CAP_SYSTEM_APPEARANCE | NK_CAP_EXPORT_NATIVE_WINDOW | NK_CAP_NOTIFICATION |
        NK_CAP_INPUT | NK_CAP_OPENGL_SURFACE | NK_CAP_OPENGL_ES_SURFACE | NK_CAP_CURSOR |
        NK_CAP_POINTER_CAPTURE | NK_CAP_WINDOW_GEOMETRY | NK_CAP_WINDOW_STYLING | NK_CAP_MONITOR |
        NK_CAP_MONITOR_FULLSCREEN | NK_CAP_JOYSTICK | NK_CAP_RESOURCE_SHARING | NK_CAP_RESOURCE_IO |
        NK_CAP_VULKAN_SURFACE | NK_CAP_SYSTEM_INFO | NK_CAP_APPLICATION_PATH |
        NK_CAP_APPLICATION_STORAGE | NK_CAP_SYSTEM_FONTS | NK_CAP_DISPLAY_ORIENTATION |
        NK_CAP_ACCESSIBILITY | NK_CAP_WRAP_NATIVE_WINDOW | NK_CAP_SURFACE_FRAME_CALLBACK |
        NK_CAP_WINDOW_CUSTOM_DECORATIONS;
    if (nk::core::system_backend::keep_awake_supported())
        capabilities |= NK_CAP_KEEP_AWAKE;
    return capabilities | nk::core::optional_capabilities();
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
        if (owner && owner->wrapped)
            return fail(NK_ERROR_UNSUPPORTED,
                        "GTK child windows cannot use a wrapped window as their owner");
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
        g_signal_connect(resource->window, "map", G_CALLBACK(on_window_map), resource.get());
        g_signal_connect(resource->window, "unmap", G_CALLBACK(on_window_unmap), resource.get());
        g_signal_connect(resource->window, "realize", G_CALLBACK(on_window_realize),
                         resource.get());
        g_signal_connect(resource->window, "focus-in-event", G_CALLBACK(on_input_focus),
                         resource.get());
        g_signal_connect(resource->window, "focus-out-event", G_CALLBACK(on_input_focus),
                         resource.get());
        g_signal_connect(resource->im_context, "commit", G_CALLBACK(on_text_commit),
                         resource.get());
        gtk_widget_add_events(resource->window,
                              GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK | GDK_POINTER_MOTION_MASK |
                                  GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                                  GDK_SCROLL_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK);
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
    if (resource->wrapped) {
        if (resource->pointer_grabbed) {
            if (resource->foreign_window) {
                if (GdkDisplay *display = gdk_window_get_display(resource->foreign_window))
                    gdk_seat_ungrab(gdk_display_get_default_seat(display));
            }
            resource->pointer_grabbed = false;
        }
        if (resource->foreign_window)
            g_object_unref(resource->foreign_window);
        resource->foreign_window = nullptr;
        nk::core::handles().erase(handle, nk::core::ResourceType::window);
        return NK_OK;
    }
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
    if (resource->wrapped) {
        if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
            return result;
        visible ? gdk_window_show(resource->foreign_window)
                : gdk_window_hide(resource->foreign_window);
        return NK_OK;
    }
    visible ? gtk_widget_show_all(resource->window) : gtk_widget_hide(resource->window);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_title(nk_handle handle, const char *title) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    if (resource->wrapped) {
        if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
            return result;
        gdk_window_set_title(resource->foreign_window, title ? title : "");
        return NK_OK;
    }
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
    if (resource->wrapped) {
        if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
            return result;
        gdk_window_move_resize(resource->foreign_window, x, y, width, height);
        return NK_OK;
    }
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
    if (resource->wrapped) {
        if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
            return result;
        *out_scale = static_cast<float>(gdk_window_get_scale_factor(resource->foreign_window));
        return NK_OK;
    }
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
    if (resource->wrapped) {
        if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
            return result;
        const float scale =
            static_cast<float>(gdk_window_get_scale_factor(resource->foreign_window));
        const auto size = out_scale->struct_size;
        *out_scale = {};
        out_scale->struct_size = size;
        out_scale->x = scale;
        out_scale->y = scale;
        return NK_OK;
    }
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
    if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
        return result;
#ifdef GDK_WINDOWING_WAYLAND
    if (!resource->wrapped && GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(resource->window)))
        return fail(NK_ERROR_UNSUPPORTED, "Wayland does not expose global window positions");
#endif
    if (resource->wrapped) {
        if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
            return result;
        gint x = 0;
        gint y = 0;
        gdk_window_get_origin(resource->foreign_window, &x, &y);
        *out_x = x;
        *out_y = y;
        return NK_OK;
    }
    gint x = 0;
    gint y = 0;
    gtk_window_get_position(GTK_WINDOW(resource->window), &x, &y);
    *out_x = x;
    *out_y = y;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_size(nk_handle handle, int32_t *out_width, int32_t *out_height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_width || !out_height)
        return fail(NK_ERROR_INVALID_ARGUMENT, "window size outputs must not be null");
    auto resource = window(handle);
    if (!resource)
        return invalid_handle("window");
    if (resource->wrapped) {
        if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
            return result;
        gdk_window_get_geometry(resource->foreign_window, nullptr, nullptr, out_width, out_height);
        return NK_OK;
    }
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
    if (resource->wrapped) {
        if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
            return result;
        gint width = 0;
        gint height = 0;
        gdk_window_get_geometry(resource->foreign_window, nullptr, nullptr, &width, &height);
        const int scale = gdk_window_get_scale_factor(resource->foreign_window);
        *out_width = width * scale;
        *out_height = height * scale;
        return NK_OK;
    }
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
    if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
        return result;
#ifdef GDK_WINDOWING_WAYLAND
    if (!resource->wrapped && GDK_IS_WAYLAND_DISPLAY(gtk_widget_get_display(resource->window)))
        return fail(NK_ERROR_UNSUPPORTED, "Wayland does not expose window frame extents");
#endif
    GdkWindow *native = native_window(*resource);
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
    if (resource->wrapped) {
        if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
            return result;
        const auto state = gdk_window_get_state(resource->foreign_window);
        if (state & GDK_WINDOW_STATE_WITHDRAWN)
            out->flags = 0;
        else {
            out->flags = NK_WINDOW_STATE_VISIBLE;
            if (state & GDK_WINDOW_STATE_ICONIFIED)
                out->flags |= NK_WINDOW_STATE_MINIMIZED;
            if (state & GDK_WINDOW_STATE_MAXIMIZED)
                out->flags |= NK_WINDOW_STATE_MAXIMIZED;
            if (state & GDK_WINDOW_STATE_FULLSCREEN)
                out->flags |= NK_WINDOW_STATE_FULLSCREEN;
        }
    } else {
        out->flags = resource->state_flags;
    }
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
    case NK_CURSOR_ARROW:
        name = "default";
        break;
    case NK_CURSOR_IBEAM:
        name = "text";
        break;
    case NK_CURSOR_CROSSHAIR:
        name = "crosshair";
        break;
    case NK_CURSOR_HAND:
        name = "pointer";
        break;
    case NK_CURSOR_HORIZONTAL_RESIZE:
        name = "ew-resize";
        break;
    case NK_CURSOR_VERTICAL_RESIZE:
        name = "ns-resize";
        break;
    case NK_CURSOR_NWSE_RESIZE:
        name = "nwse-resize";
        break;
    case NK_CURSOR_NESW_RESIZE:
        name = "nesw-resize";
        break;
    case NK_CURSOR_MOVE:
        name = "move";
        break;
    case NK_CURSOR_NOT_ALLOWED:
        name = "not-allowed";
        break;
    default:
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid standard cursor shape");
    }
    if (!ensure_gtk())
        return NK_ERROR_UNSUPPORTED;
    auto resource = std::make_shared<GtkCursorResource>();
    resource->cursor = gdk_cursor_new_from_name(gdk_display_get_default(), name);
    if (!resource->cursor)
        return fail(NK_ERROR_UNSUPPORTED, "cursor shape is unavailable");
    resource->handle = nk::core::handles().insert(nk::core::ResourceType::cursor, resource);
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
        resource->cursor = gdk_cursor_new_from_pixbuf(gdk_display_get_default(), pixbuf,
                                                      image->hotspot_x, image->hotspot_y);
        g_object_unref(pixbuf);
        if (!resource->cursor)
            return fail(NK_ERROR_UNSUPPORTED, "GTK could not create the custom cursor");
        resource->handle = nk::core::handles().insert(nk::core::ResourceType::cursor, resource);
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
               : apply_pointer_cursor(*resource);
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

uint32_t NK_CALL nk_raw_pointer_motion_supported(void) {
    return 0;
}

nk_result NK_CALL nk_surface_set_text_input_state(nk_handle handle,
                                                  const nk_text_input_state *state) {
    return nk::core::result_boundary(
        "unexpected error while setting GTK text input state", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!state || state->struct_size < sizeof(*state))
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid text input state");
            const char *text = state->text ? state->text : "";
            if (!g_utf8_validate(text, -1, nullptr))
                return fail(NK_ERROR_INVALID_ARGUMENT, "text input state text is not valid UTF-8");
            const auto text_length = static_cast<uint64_t>(g_utf8_strlen(text, -1));
            const auto text_end = static_cast<uint64_t>(state->text_start) + text_length;
            const bool no_composition = state->composition_start == NK_TEXT_POSITION_NONE &&
                                        state->composition_end == NK_TEXT_POSITION_NONE;
            const bool valid_composition = state->composition_start != NK_TEXT_POSITION_NONE &&
                                           state->composition_end != NK_TEXT_POSITION_NONE &&
                                           state->composition_start <= state->composition_end &&
                                           state->composition_start >= state->text_start &&
                                           state->composition_end <= text_end;
            const bool valid_cursor =
                std::isfinite(state->cursor_x) && std::isfinite(state->cursor_y) &&
                std::isfinite(state->cursor_width) && std::isfinite(state->cursor_height) &&
                state->cursor_width >= 0.0f && state->cursor_height >= 0.0f;
            if (text_end > state->document_length ||
                state->selection_start > state->selection_end ||
                state->selection_start < state->text_start || state->selection_end > text_end ||
                (!no_composition && !valid_composition) ||
                (state->flags & ~(NK_TEXT_INPUT_MULTILINE | NK_TEXT_INPUT_AUTOCORRECT |
                                  NK_TEXT_INPUT_CAPITALIZE_SENTENCES)) ||
                state->input_type > NK_TEXT_INPUT_PASSWORD ||
                state->action > NK_TEXT_INPUT_ACTION_NONE || !valid_cursor)
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "text input state ranges or hints are invalid");
            auto resource = text_input_window(handle);
            if (!resource)
                return invalid_handle("text input target");
            resource->text_input_text = text;
            resource->text_input_state = *state;
            resource->text_input_state.text = resource->text_input_text.c_str();
            if (resource->im_context) {
                GdkRectangle cursor{
                    static_cast<gint>(std::lround(state->cursor_x)),
                    static_cast<gint>(std::lround(state->cursor_y)),
                    std::max(static_cast<gint>(std::lround(state->cursor_width)), 1),
                    std::max(static_cast<gint>(std::lround(state->cursor_height)), 1)};
                gtk_im_context_set_cursor_location(resource->im_context, &cursor);
            }
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_set_text_input_active(nk_handle handle, uint32_t active) {
    return nk::core::result_boundary(
        "unexpected error while changing GTK text input", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (active > 1)
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "text input active state must be zero or one");
            auto resource = text_input_window(handle);
            if (!resource)
                return invalid_handle("text input target");
            resource->text_input_active = active != 0;
            if (!resource->im_context)
                return NK_OK;
            if (resource->text_input_active) {
                gtk_im_context_focus_in(resource->im_context);
                gtk_widget_grab_focus(resource->window);
            } else {
                gtk_im_context_reset(resource->im_context);
                gtk_im_context_focus_out(resource->im_context);
            }
            return NK_OK;
        });
}

nk_result NK_CALL nk_window_minimize(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return invalid_handle("window");
    if (w->wrapped) {
        if (const auto result = require_gdk_wrapper(*w); result != NK_OK)
            return result;
        gdk_window_iconify(w->foreign_window);
        return NK_OK;
    }
    gtk_window_iconify(GTK_WINDOW(w->window));
    return NK_OK;
}
nk_result NK_CALL nk_window_maximize(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return invalid_handle("window");
    if (w->wrapped) {
        if (const auto result = require_gdk_wrapper(*w); result != NK_OK)
            return result;
        gdk_window_maximize(w->foreign_window);
        return NK_OK;
    }
    gtk_window_maximize(GTK_WINDOW(w->window));
    return NK_OK;
}
nk_result NK_CALL nk_window_restore(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return invalid_handle("window");
    if (w->wrapped) {
        if (const auto result = require_gdk_wrapper(*w); result != NK_OK)
            return result;
        gdk_window_deiconify(w->foreign_window);
        gdk_window_unmaximize(w->foreign_window);
        gdk_window_unfullscreen(w->foreign_window);
        return NK_OK;
    }
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
    if (w->wrapped) {
        if (const auto result = require_gdk_wrapper(*w); result != NK_OK)
            return result;
        gdk_window_raise(w->foreign_window);
        gdk_window_focus(w->foreign_window, GDK_CURRENT_TIME);
        return NK_OK;
    }
    gtk_window_present(GTK_WINDOW(w->window));
    return NK_OK;
}
nk_result NK_CALL nk_window_set_fullscreen(nk_handle h, uint32_t enabled) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return invalid_handle("window");
    if (w->wrapped) {
        if (const auto result = require_gdk_wrapper(*w); result != NK_OK)
            return result;
        enabled ? gdk_window_fullscreen(w->foreign_window)
                : gdk_window_unfullscreen(w->foreign_window);
        return NK_OK;
    }
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
    if (w->wrapped)
        return fail(NK_ERROR_UNSUPPORTED, "wrapped GTK windows do not support attention hints");
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
    if (const auto result = require_gdk_wrapper(*w); result != NK_OK)
        return result;
    w->min_width = limits->min_width;
    w->min_height = limits->min_height;
    w->max_width = limits->max_width;
    w->max_height = limits->max_height;
    apply_geometry_hints(*w);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_aspect_ratio(nk_handle h, int32_t numerator, int32_t denominator) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if ((numerator == 0) != (denominator == 0) || numerator < 0 || denominator < 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid window aspect ratio");
    auto resource = window(h);
    if (!resource)
        return invalid_handle("window");
    if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
        return result;
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
    if (resource->wrapped) {
        if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
            return result;
        resource->resizable = enabled != 0;
        return NK_OK;
    }
    gtk_window_set_resizable(GTK_WINDOW(resource->window), enabled != 0);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_decorated(nk_handle h, uint32_t enabled) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(h);
    if (!resource)
        return invalid_handle("window");
    if (resource->wrapped) {
        if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
            return result;
        resource->decorated = enabled != 0;
        gdk_window_set_decorations(resource->foreign_window,
                                   enabled ? GDK_DECOR_ALL : static_cast<GdkWMDecoration>(0));
        return NK_OK;
    }
    resource->decorated = enabled != 0;
    gtk_window_set_decorated(GTK_WINDOW(resource->window), enabled != 0);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_decoration_regions(nk_handle h,
                                                   const nk_window_decoration_region *regions,
                                                   uint32_t region_count) {
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (region_count && !regions)
            return fail(NK_ERROR_INVALID_ARGUMENT, "decoration regions are missing");
        auto resource = window(h);
        if (!resource)
            return invalid_handle("window");
        if (resource->wrapped)
            return fail(NK_ERROR_UNSUPPORTED,
                        "custom decoration regions require a NativeKit-owned window");
        for (uint32_t index = 0; index < region_count; ++index) {
            const auto &region = regions[index];
            if (!std::isfinite(region.x) || !std::isfinite(region.y) ||
                !std::isfinite(region.width) || !std::isfinite(region.height) || region.x < 0.0f ||
                region.y < 0.0f || region.width <= 0.0f || region.height <= 0.0f ||
                region.kind > NK_WINDOW_DECORATION_RESIZE_SOUTHEAST ||
                region.cursor_shape > NK_CURSOR_NOT_ALLOWED)
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid decoration region");
        }
        if (region_count == 0)
            resource->decoration_regions.clear();
        else
            resource->decoration_regions.assign(regions, regions + region_count);
        if (resource->hovered)
            apply_pointer_cursor(*resource);
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while setting decoration regions");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while setting decoration regions");
    }
}

nk_result NK_CALL nk_window_set_floating(nk_handle h, uint32_t enabled) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(h);
    if (!resource)
        return invalid_handle("window");
    if (resource->wrapped) {
        if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
            return result;
        gdk_window_set_keep_above(resource->foreign_window, enabled != 0);
        return NK_OK;
    }
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
    if (resource->wrapped) {
        if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
            return result;
        gdk_window_set_opacity(resource->foreign_window, opacity);
        return NK_OK;
    }
    gtk_widget_set_opacity(resource->window, opacity);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_mouse_passthrough(nk_handle h, uint32_t enabled) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(h);
    if (!resource)
        return invalid_handle("window");
    if (const auto result = require_gdk_wrapper(*resource); result != NK_OK)
        return result;
    GdkWindow *native = native_window(*resource);
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
    return nk::core::result_boundary(
        "unexpected error while enumerating monitors", [&]() -> nk_result {
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
                return required
                           ? fail(NK_ERROR_BUFFER_TOO_SMALL, "monitor handle buffer is too small")
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
    return nk::core::result_boundary(
        "unexpected error while finding primary monitor", [&]() -> nk_result {
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

nk_result NK_CALL nk_monitor_get_geometry(nk_handle handle, nk_monitor_geometry *out_geometry) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_geometry || out_geometry->struct_size < sizeof(*out_geometry))
        return fail(NK_ERROR_INVALID_ARGUMENT, "monitor geometry output is missing or too small");
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
        if (const auto result = require_gdk_wrapper(*window_resource); result != NK_OK)
            return result;
        if (window_resource->wrapped)
            gdk_window_unfullscreen(window_resource->foreign_window);
        else
            gtk_window_unfullscreen(GTK_WINDOW(window_resource->window));
        return NK_OK;
    }
    auto monitor_resource = monitor(monitor_handle);
    if (!monitor_resource)
        return invalid_handle("monitor");
    if (const auto result = require_gdk_wrapper(*window_resource); result != NK_OK)
        return result;
    GdkWindow *native = native_window(*window_resource);
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
    const auto size = out_native->struct_size;
    if (resource->wrapped) {
        *out_native = {};
        out_native->struct_size = size;
        if (resource->foreign_kind == NK_NATIVE_WINDOW_WAYLAND) {
            out_native->kind = NK_NATIVE_WINDOW_WAYLAND;
            out_native->display = resource->foreign_display;
            out_native->window = resource->foreign_surface;
            return NK_OK;
        }
#ifdef GDK_WINDOWING_X11
        if (!resource->foreign_window)
            return fail(NK_ERROR_UNSUPPORTED, "wrapped GTK window has no native GDK window");
        out_native->kind = NK_NATIVE_WINDOW_X11;
        GdkDisplay *display = gdk_window_get_display(resource->foreign_window);
        out_native->display = reinterpret_cast<uintptr_t>(gdk_x11_display_get_xdisplay(display));
        out_native->window =
            static_cast<uintptr_t>(gdk_x11_window_get_xid(resource->foreign_window));
        return NK_OK;
#else
        return fail(NK_ERROR_UNSUPPORTED, "wrapped GTK window is not an X11 window");
#endif
    }
    gtk_widget_realize(resource->window);
    GdkWindow *native = gtk_widget_get_window(resource->window);
    if (!native)
        return fail(NK_ERROR_UNKNOWN, "GTK window has no native surface");
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
    return nk::core::result_boundary(
        "unexpected error while wrapping native GTK window", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!native || native->struct_size < sizeof(*native) || !out_window ||
                (native->kind != NK_NATIVE_WINDOW_X11 &&
                 native->kind != NK_NATIVE_WINDOW_WAYLAND) ||
                !native->display || !native->window)
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid native window descriptor");
            *out_window = NK_INVALID_HANDLE;
            if (!ensure_gtk())
                return NK_ERROR_UNSUPPORTED;
#ifdef GDK_WINDOWING_WAYLAND
            if (native->kind == NK_NATIVE_WINDOW_WAYLAND) {
                GdkDisplay *display = gdk_display_get_default();
                if (!display || !GDK_IS_WAYLAND_DISPLAY(display) ||
                    reinterpret_cast<uintptr_t>(gdk_wayland_display_get_wl_display(display)) !=
                        native->display)
                    return fail(NK_ERROR_INVALID_ARGUMENT,
                                "Wayland display is not attached to GTK");
                auto resource = std::make_shared<GtkWindowResource>();
                resource->foreign_kind = NK_NATIVE_WINDOW_WAYLAND;
                resource->foreign_display = native->display;
                resource->foreign_surface = native->window;
                resource->wrapped = true;
                resource->decorated = true;
                resource->resizable = true;
                resource->generation = nk::core::runtime_generation();
                resource->handle =
                    nk::core::handles().insert(nk::core::ResourceType::window, resource);
                if (resource->handle == NK_INVALID_HANDLE)
                    return fail(NK_ERROR_OUT_OF_MEMORY, "window handle registry is full");
                *out_window = resource->handle;
                return NK_OK;
            }
#else
            if (native->kind == NK_NATIVE_WINDOW_WAYLAND)
                return fail(NK_ERROR_UNSUPPORTED, "GTK was built without Wayland interoperability");
#endif
#ifdef GDK_WINDOWING_X11
            auto *display = gdk_x11_lookup_xdisplay(reinterpret_cast<Display *>(native->display));
            if (!display)
                return fail(NK_ERROR_INVALID_ARGUMENT, "X11 display is not attached to GTK");
            auto *foreign = gdk_x11_window_foreign_new_for_display(
                display, static_cast<Window>(native->window));
            if (!foreign)
                return fail(NK_ERROR_INVALID_ARGUMENT, "X11 native window is not valid");
            auto resource = std::make_shared<GtkWindowResource>();
            resource->foreign_window = foreign;
            resource->foreign_kind = NK_NATIVE_WINDOW_X11;
            resource->wrapped = true;
            resource->decorated = true;
            resource->resizable = true;
            resource->generation = nk::core::runtime_generation();
            resource->handle = nk::core::handles().insert(nk::core::ResourceType::window, resource);
            if (resource->handle == NK_INVALID_HANDLE) {
                g_object_unref(foreign);
                resource->foreign_window = nullptr;
                return fail(NK_ERROR_OUT_OF_MEMORY, "window handle registry is full");
            }
            *out_window = resource->handle;
            return NK_OK;
#else
            return fail(NK_ERROR_UNSUPPORTED, "GTK was built without X11 interoperability");
#endif
        });
}

nk_result NK_CALL nk_surface_create(nk_handle parent_handle, const nk_surface_options *options,
                                    nk_handle *out_surface) {
    return nk::core::result_boundary(
        "unexpected error while creating graphics surface", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!options || options->struct_size < sizeof(*options) || !out_surface ||
                options->width <= 0 || options->height <= 0 ||
                (options->api != NK_GRAPHICS_OPENGL && options->api != NK_GRAPHICS_OPENGL_ES))
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid graphics surface options");
            *out_surface = NK_INVALID_HANDLE;
            auto parent = window(parent_handle);
            if (!parent)
                return invalid_handle("parent window");
            if (parent->wrapped)
                return fail(NK_ERROR_UNSUPPORTED,
                            "GTK graphics surfaces cannot attach to wrapped windows");
            auto shared = options->share_surface ? surface(options->share_surface) : nullptr;
            if (options->share_surface && !shared)
                return invalid_handle("shared graphics surface");
            if (shared &&
                (shared->api != options->api || shared->major_version != options->major_version ||
                 shared->minor_version != options->minor_version ||
                 shared->flags !=
                     (options->flags & (NK_SURFACE_DEBUG_CONTEXT | NK_SURFACE_FORWARD_COMPATIBLE))))
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
            resource->widget = nk_gl_area_new();
            g_object_set_data(G_OBJECT(resource->widget), k_accessibility_resource_data,
                              resource.get());
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
            g_signal_connect(resource->widget, "render", G_CALLBACK(on_surface_render),
                             resource.get());
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
    auto device_surface = resource.get();
    while (device_surface->shared_surface)
        device_surface = device_surface->shared_surface.get();
    if (device_surface == resource.get() &&
        nk_core_graphics_device_has_references(nk_graphics_device{resource->handle}))
        return fail(NK_ERROR_INVALID_REQUEST,
                    "graphics surface still owns retained sampled images");
    g_signal_handlers_disconnect_by_data(resource->widget, resource.get());
    g_object_set_data(G_OBJECT(resource->widget), k_accessibility_resource_data, nullptr);
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

nk_result NK_CALL nk_surface_accessibility_set_node(nk_handle handle,
                                                    const nk_accessibility_node *node) {
    return nk::core::result_boundary(
        "unexpected error while setting a Linux accessibility node", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            auto resource = surface(handle);
            if (!resource)
                return invalid_handle("graphics surface");
            if (!node)
                return fail(NK_ERROR_INVALID_ARGUMENT, "Linux accessibility node is missing");
            GtkAccessibilityNode copy;
            if (!copy_gtk_accessibility_node(*node, resource->accessibility_nodes, copy))
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid Linux accessibility node");
            if (const auto old = resource->accessibility_nodes.find(node->id);
                old != resource->accessibility_nodes.end())
                copy.text_ranges = old->second.text_ranges;
            resource->accessibility_nodes[node->id] = std::move(copy);
            refresh_gtk_accessibility(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_remove_node(nk_handle handle,
                                                       nk_accessibility_node_id node) {
    return nk::core::result_boundary(
        "unexpected error while removing a Linux accessibility node", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            auto resource = surface(handle);
            if (!resource)
                return invalid_handle("graphics surface");
            if (!node ||
                resource->accessibility_nodes.find(node) == resource->accessibility_nodes.end())
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "invalid or unknown Linux accessibility node");
            remove_gtk_accessibility_descendants(resource->accessibility_nodes, node);
            if (resource->accessibility_nodes.find(resource->accessibility_focus) ==
                resource->accessibility_nodes.end())
                resource->accessibility_focus = NK_ACCESSIBILITY_ROOT;
            refresh_gtk_accessibility(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_clear(nk_handle handle) {
    return nk::core::result_boundary("unexpected error while clearing Linux accessibility nodes",
                                     [&]() -> nk_result {
                                         if (const auto result = enter_ui(); result != NK_OK)
                                             return result;
                                         auto resource = surface(handle);
                                         if (!resource)
                                             return invalid_handle("graphics surface");
                                         resource->accessibility_nodes.clear();
                                         resource->accessibility_focus = NK_ACCESSIBILITY_ROOT;
                                         refresh_gtk_accessibility(*resource);
                                         return NK_OK;
                                     });
}

nk_result NK_CALL nk_surface_accessibility_set_focus(nk_handle handle,
                                                     nk_accessibility_node_id node) {
    return nk::core::result_boundary(
        "unexpected error while focusing a Linux accessibility node", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            auto resource = surface(handle);
            if (!resource)
                return invalid_handle("graphics surface");
            if (node != NK_ACCESSIBILITY_ROOT &&
                resource->accessibility_nodes.find(node) == resource->accessibility_nodes.end())
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "cannot focus an unknown Linux accessibility node");
            resource->accessibility_focus = node;
            refresh_gtk_accessibility(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_update(nk_handle handle,
                                                  const nk_accessibility_update *update) {
    return nk::core::result_boundary(
        "unexpected error while updating Linux accessibility nodes", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            auto resource = surface(handle);
            if (!resource)
                return invalid_handle("graphics surface");
            if (!update || update->struct_size < sizeof(*update) ||
                (update->flags & ~NK_ACCESSIBILITY_UPDATE_FOCUS) ||
                (update->node_count && !update->nodes) ||
                (update->removed_node_count && !update->removed_nodes))
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid Linux accessibility update");
            auto nodes = resource->accessibility_nodes;
            for (uint32_t index = 0; index < update->removed_node_count; ++index) {
                const auto removed = update->removed_nodes[index];
                if (!removed || nodes.find(removed) == nodes.end())
                    return fail(NK_ERROR_INVALID_ARGUMENT,
                                "Linux accessibility update removes an unknown node");
                remove_gtk_accessibility_descendants(nodes, removed);
            }
            for (uint32_t index = 0; index < update->node_count; ++index) {
                const auto &node = update->nodes[index];
                GtkAccessibilityNode copy;
                if (!copy_gtk_accessibility_node(node, nodes, copy)) {
                    const char *value = node.value ? node.value : "";
                    const bool valid_value = g_utf8_validate(value, -1, nullptr);
                    const uint64_t text_end =
                        valid_value ? static_cast<uint64_t>(node.text_start) +
                                          static_cast<uint64_t>(g_utf8_strlen(value, -1))
                                    : 0;
                    std::string reason = "invalid fields";
                    if (!valid_value)
                        reason = "value is not valid UTF-8";
                    else if (node.label && !g_utf8_validate(node.label, -1, nullptr))
                        reason = "label is not valid UTF-8";
                    else if (text_end > node.document_length)
                        reason = "text end " + std::to_string(text_end) +
                                 " exceeds document length " + std::to_string(node.document_length);
                    else if ((node.selection_start == NK_ACCESSIBILITY_TEXT_POSITION_NONE) !=
                                 (node.selection_end == NK_ACCESSIBILITY_TEXT_POSITION_NONE) ||
                             (node.selection_start != NK_ACCESSIBILITY_TEXT_POSITION_NONE &&
                              (node.selection_start > node.selection_end ||
                               node.selection_start < node.text_start ||
                               node.selection_end > text_end)))
                        reason = "selection is outside the text range";
                    else if (node.parent_id != NK_ACCESSIBILITY_ROOT &&
                             nodes.find(node.parent_id) == nodes.end())
                        reason = "parent " + std::to_string(node.parent_id) + " is unknown";
                    return fail(NK_ERROR_INVALID_ARGUMENT,
                                "invalid Linux accessibility node " + std::to_string(node.id) +
                                    " at update index " + std::to_string(index) + ": " + reason);
                }
                if (const auto old = nodes.find(node.id); old != nodes.end())
                    copy.text_ranges = old->second.text_ranges;
                nodes[node.id] = std::move(copy);
            }
            if ((update->flags & NK_ACCESSIBILITY_UPDATE_FOCUS) &&
                update->focus != NK_ACCESSIBILITY_ROOT && nodes.find(update->focus) == nodes.end())
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "Linux accessibility update focuses an unknown node");
            resource->accessibility_nodes = std::move(nodes);
            if (update->flags & NK_ACCESSIBILITY_UPDATE_FOCUS)
                resource->accessibility_focus = update->focus;
            else if (resource->accessibility_focus != NK_ACCESSIBILITY_ROOT &&
                     resource->accessibility_nodes.find(resource->accessibility_focus) ==
                         resource->accessibility_nodes.end())
                resource->accessibility_focus = NK_ACCESSIBILITY_ROOT;
            refresh_gtk_accessibility(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_set_text_ranges(
    nk_handle handle, nk_accessibility_node_id node, const nk_accessibility_text_range *ranges,
    uint32_t range_count) {
    return nk::core::result_boundary(
        "unexpected error while setting Linux accessibility text ranges", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            auto resource = surface(handle);
            if (!resource)
                return invalid_handle("graphics surface");
            const auto found = resource->accessibility_nodes.find(node);
            if (!node || found == resource->accessibility_nodes.end() || (range_count && !ranges))
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid Linux accessibility text ranges");
            std::vector<GtkAccessibilityTextRange> copy;
            copy.reserve(range_count);
            nk_accessibility_text_position previous = 0;
            for (uint32_t index = 0; index < range_count; ++index) {
                const auto &range = ranges[index];
                if (range.start >= range.end || range.start < previous ||
                    range.end > found->second.document_length || !std::isfinite(range.x) ||
                    !std::isfinite(range.y) || !std::isfinite(range.width) ||
                    !std::isfinite(range.height) || range.width < 0 || range.height < 0)
                    return fail(NK_ERROR_INVALID_ARGUMENT,
                                "invalid or unordered Linux accessibility text ranges");
                copy.push_back(
                    {range.start, range.end, range.x, range.y, range.width, range.height});
                previous = range.end;
            }
            found->second.text_ranges = std::move(copy);
            refresh_gtk_accessibility(*resource);
            return NK_OK;
        });
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

nk_result NK_CALL nk_surface_set_frame_callback(nk_handle handle,
                                                nk_surface_frame_callback callback,
                                                void *user_data) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = surface(handle);
    if (!resource)
        return invalid_handle("graphics surface");
    resource->frame_callback = callback;
    resource->frame_user_data = callback ? user_data : nullptr;
    if (resource->frame_tick) {
        gtk_widget_remove_tick_callback(resource->widget, resource->frame_tick);
        resource->frame_tick = 0;
    }
    gtk_gl_area_set_auto_render(GTK_GL_AREA(resource->widget), FALSE);
    if (callback) {
        resource->frame_tick = gtk_widget_add_tick_callback(resource->widget, on_surface_tick,
                                                            resource.get(), nullptr);
        gtk_gl_area_queue_render(GTK_GL_AREA(resource->widget));
    }
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

nk_result NK_CALL nk_surface_get_frame_target(nk_handle handle,
                                              nk_surface_frame_target *out_target) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!nk::core::surface_frame_target_output_valid(out_target))
        return fail(NK_ERROR_INVALID_ARGUMENT, "frame target output is missing or too small");
    auto resource = surface(handle);
    if (!resource)
        return invalid_handle("graphics surface");
    int32_t width = 0;
    int32_t height = 0;
    if (const auto result = nk_surface_get_framebuffer_size(handle, &width, &height);
        result != NK_OK)
        return result;
    nk_graphics_proc proc = nullptr;
    if (const auto result = nk_surface_get_proc_address(handle, "glGetIntegerv", &proc);
        result != NK_OK)
        return result;
    using GlGetIntegerv = void (*)(unsigned int, int *);
    auto get_integerv = reinterpret_cast<GlGetIntegerv>(proc);
    int framebuffer = 0;
    get_integerv(0x8CA6u, &framebuffer); // GL_DRAW_FRAMEBUFFER_BINDING
    nk_surface_frame_target target{};
    target.struct_size = out_target->struct_size;
    target.api = resource->api;
    target.width = width;
    target.height = height;
    target.native_target = static_cast<uint64_t>(static_cast<uint32_t>(framebuffer));
    auto device_surface = resource.get();
    while (device_surface->shared_surface)
        device_surface = device_surface->shared_surface.get();
    target.device.id = device_surface->handle;
    nk::core::write_surface_frame_target(out_target, target);
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
        if (parent->wrapped)
            return fail(NK_ERROR_UNSUPPORTED, "GTK WebViews cannot attach to wrapped windows");
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

nk_result NK_CALL nk_webview_can_go_back(nk_handle handle, uint32_t *out_can_go_back) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource || !out_can_go_back)
        return !resource ? invalid_handle("WebView")
                         : fail(NK_ERROR_INVALID_ARGUMENT, "history output is null");
    *out_can_go_back = webkit_web_view_can_go_back(WEBKIT_WEB_VIEW(resource->widget)) ? 1u : 0u;
    return NK_OK;
}

nk_result NK_CALL nk_webview_can_go_forward(nk_handle handle, uint32_t *out_can_go_forward) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource || !out_can_go_forward)
        return !resource ? invalid_handle("WebView")
                         : fail(NK_ERROR_INVALID_ARGUMENT, "history output is null");
    *out_can_go_forward =
        webkit_web_view_can_go_forward(WEBKIT_WEB_VIEW(resource->widget)) ? 1u : 0u;
    return NK_OK;
}

nk_result NK_CALL nk_webview_go_back(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource)
        return invalid_handle("WebView");
    webkit_web_view_go_back(WEBKIT_WEB_VIEW(resource->widget));
    return NK_OK;
}

nk_result NK_CALL nk_webview_go_forward(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource)
        return invalid_handle("WebView");
    webkit_web_view_go_forward(WEBKIT_WEB_VIEW(resource->widget));
    return NK_OK;
}

nk_result NK_CALL nk_webview_reload(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource)
        return invalid_handle("WebView");
    webkit_web_view_reload(WEBKIT_WEB_VIEW(resource->widget));
    return NK_OK;
}

nk_result NK_CALL nk_webview_stop(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource)
        return invalid_handle("WebView");
    webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(resource->widget));
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
                if (parent->wrapped)
                    return fail(NK_ERROR_UNSUPPORTED,
                                "GTK message dialogs cannot use a wrapped window as their parent");
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

nk_result NK_CALL nk_clipboard_set_resources(const nk_resource *resources,
                                             uint32_t resource_count) {
    return nk::core::result_boundary(
        "unexpected error while writing resource clipboard", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (const auto result =
                    nk::platform::validate_resources(resources, resource_count, false);
                result != NK_OK)
                return result;
            if (!ensure_gtk())
                return NK_ERROR_UNSUPPORTED;
            std::vector<std::string> uris;
            uris.reserve(resource_count);
            for (uint32_t index = 0; index < resource_count; ++index)
                uris.emplace_back(resources[index].uri);
            return set_clipboard_uris(std::move(uris));
        });
}

nk_result NK_CALL nk_clipboard_read_resources(nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while reading resource clipboard", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!out_request)
                return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard request output is null");
            if (!ensure_gtk())
                return NK_ERROR_UNSUPPORTED;
            auto request = std::make_unique<ClipboardRequest>();
            request->request = nk::core::next_request_id();
            request->event_kind = NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE;
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
    if (resource->wrapped)
        return fail(NK_ERROR_UNSUPPORTED, "GTK drops cannot attach to wrapped windows");
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

nk_result NK_CALL nk_share(const nk_share_options *options) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!options || options->struct_size < sizeof(*options) || options->flags != 0 ||
        (!options->text && options->resource_count == 0))
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid or empty share options");
    if (const auto result =
            nk::platform::validate_resources(options->resources, options->resource_count, true);
        result != NK_OK)
        return result;
    // Linux desktops have no common native share-sheet contract. Preserve the
    // URI-first payload through the desktop clipboard as the portable equivalent.
    if (!options->resource_count)
        return nk_clipboard_set_text(options->text);
    std::vector<std::string> uris;
    uris.reserve(options->resource_count);
    for (uint32_t index = 0; index < options->resource_count; ++index)
        uris.emplace_back(options->resources[index].uri);
    return set_clipboard_uris(std::move(uris), options->text);
}

nk_result NK_CALL nk_dialog_open_resource(nk_handle parent, const nk_file_dialog_options *options,
                                          nk_request_id *request) {
    return nk::core::result_boundary(
        "unexpected error while opening resource dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, request, NK_DIALOG_OPEN_RESOURCE);
        });
}

nk_result NK_CALL nk_dialog_save_resource(nk_handle parent, const nk_file_dialog_options *options,
                                          nk_request_id *request) {
    return nk::core::result_boundary(
        "unexpected error while opening resource save dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, request, NK_DIALOG_SAVE_RESOURCE);
        });
}

nk_result NK_CALL nk_dialog_select_resource_directory(nk_handle parent,
                                                      const nk_file_dialog_options *options,
                                                      nk_request_id *request) {
    return nk::core::result_boundary(
        "unexpected error while opening resource directory dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, request, NK_DIALOG_SELECT_RESOURCE_DIRECTORY);
        });
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
    const auto value = system_directory_path(kind);
    if (value.empty())
        return fail(NK_ERROR_UNSUPPORTED, "system directory is unavailable");
    return copy_utf8(value.c_str(), buffer, inout_size);
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

nk_result NK_CALL nk_monitor_get_orientation(nk_handle handle, nk_orientation *out_orientation) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_orientation)
        return fail(NK_ERROR_INVALID_ARGUMENT, "monitor orientation output must not be null");
    auto resource = monitor(handle);
    if (!resource)
        return invalid_handle("monitor");
    *out_orientation = monitor_orientation(resource->monitor);
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
