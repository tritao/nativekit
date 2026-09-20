#include "linux/nativekit_gtk_menu.hpp"

#include "core/error.hpp"
#include "core/menu_internal.hpp"
#include "core/runtime.hpp"
#include "core/system_internal.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct GtkMenuState {
    GtkApplication *application = nullptr;
    GMenu *app_menu = nullptr;
    GMenu *menubar = nullptr;
    nk_menu handle = NK_INVALID_HANDLE;
    std::unordered_map<nk_menu_item, std::string> actions;
};

GtkMenuState state;

std::shared_ptr<nk::core::MenuItemResource> item(nk_menu_item handle) {
    return std::static_pointer_cast<nk::core::MenuItemResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::menu_item));
}

std::string action_name(nk_menu_item handle) {
    return "nk_" + std::to_string(handle);
}

std::string action_detailed_name(nk_menu_item handle) {
    return "app." + action_name(handle);
}

nk_menu_shortcut default_role_shortcut(nk_menu_item_role role) {
    switch (role) {
    case NK_MENU_ROLE_PREFERENCES:
        return {NK_KEY_COMMA, NK_MENU_MOD_PRIMARY};
    case NK_MENU_ROLE_UNDO:
        return {NK_KEY_Z, NK_MENU_MOD_PRIMARY};
    case NK_MENU_ROLE_REDO:
        return {NK_KEY_Z, NK_MENU_MOD_PRIMARY | NK_MENU_MOD_SHIFT};
    case NK_MENU_ROLE_CUT:
        return {NK_KEY_X, NK_MENU_MOD_PRIMARY};
    case NK_MENU_ROLE_COPY:
        return {NK_KEY_C, NK_MENU_MOD_PRIMARY};
    case NK_MENU_ROLE_PASTE:
        return {NK_KEY_V, NK_MENU_MOD_PRIMARY};
    case NK_MENU_ROLE_SELECT_ALL:
        return {NK_KEY_A, NK_MENU_MOD_PRIMARY};
    case NK_MENU_ROLE_QUIT:
        return {NK_KEY_Q, NK_MENU_MOD_PRIMARY};
    default:
        return {NK_KEY_UNKNOWN, 0};
    }
}

guint keyval(nk_key key) {
    if (key >= NK_KEY_A && key <= NK_KEY_Z)
        return GDK_KEY_a + (key - NK_KEY_A);
    if (key >= NK_KEY_0 && key <= NK_KEY_9)
        return GDK_KEY_0 + (key - NK_KEY_0);
    if (key >= NK_KEY_F1 && key <= NK_KEY_F24)
        return GDK_KEY_F1 + (key - NK_KEY_F1);
    if (key >= NK_KEY_KP_0 && key <= NK_KEY_KP_9)
        return GDK_KEY_KP_0 + (key - NK_KEY_KP_0);
    switch (key) {
    case NK_KEY_SPACE:
        return GDK_KEY_space;
    case NK_KEY_APOSTROPHE:
        return GDK_KEY_apostrophe;
    case NK_KEY_COMMA:
        return GDK_KEY_comma;
    case NK_KEY_MINUS:
        return GDK_KEY_minus;
    case NK_KEY_PERIOD:
        return GDK_KEY_period;
    case NK_KEY_SLASH:
        return GDK_KEY_slash;
    case NK_KEY_SEMICOLON:
        return GDK_KEY_semicolon;
    case NK_KEY_EQUAL:
        return GDK_KEY_equal;
    case NK_KEY_LEFT_BRACKET:
        return GDK_KEY_bracketleft;
    case NK_KEY_BACKSLASH:
        return GDK_KEY_backslash;
    case NK_KEY_RIGHT_BRACKET:
        return GDK_KEY_bracketright;
    case NK_KEY_GRAVE_ACCENT:
        return GDK_KEY_grave;
    case NK_KEY_ESCAPE:
        return GDK_KEY_Escape;
    case NK_KEY_ENTER:
        return GDK_KEY_Return;
    case NK_KEY_TAB:
        return GDK_KEY_Tab;
    case NK_KEY_BACKSPACE:
        return GDK_KEY_BackSpace;
    case NK_KEY_INSERT:
        return GDK_KEY_Insert;
    case NK_KEY_DELETE:
        return GDK_KEY_Delete;
    case NK_KEY_RIGHT:
        return GDK_KEY_Right;
    case NK_KEY_LEFT:
        return GDK_KEY_Left;
    case NK_KEY_DOWN:
        return GDK_KEY_Down;
    case NK_KEY_UP:
        return GDK_KEY_Up;
    case NK_KEY_PAGE_UP:
        return GDK_KEY_Page_Up;
    case NK_KEY_PAGE_DOWN:
        return GDK_KEY_Page_Down;
    case NK_KEY_HOME:
        return GDK_KEY_Home;
    case NK_KEY_END:
        return GDK_KEY_End;
    case NK_KEY_KP_DECIMAL:
        return GDK_KEY_KP_Decimal;
    case NK_KEY_KP_DIVIDE:
        return GDK_KEY_KP_Divide;
    case NK_KEY_KP_MULTIPLY:
        return GDK_KEY_KP_Multiply;
    case NK_KEY_KP_SUBTRACT:
        return GDK_KEY_KP_Subtract;
    case NK_KEY_KP_ADD:
        return GDK_KEY_KP_Add;
    case NK_KEY_KP_ENTER:
        return GDK_KEY_KP_Enter;
    case NK_KEY_KP_EQUAL:
        return GDK_KEY_KP_Equal;
    default:
        return 0;
    }
}

GdkModifierType shortcut_modifiers(nk_menu_modifiers modifiers) {
    GdkModifierType result = static_cast<GdkModifierType>(0);
    if (modifiers & NK_MENU_MOD_PRIMARY)
        result = static_cast<GdkModifierType>(result | GDK_CONTROL_MASK);
    if (modifiers & NK_MENU_MOD_SHIFT)
        result = static_cast<GdkModifierType>(result | GDK_SHIFT_MASK);
    if (modifiers & NK_MENU_MOD_ALT)
        result = static_cast<GdkModifierType>(result | GDK_MOD1_MASK);
    if (modifiers & NK_MENU_MOD_CONTROL)
        result = static_cast<GdkModifierType>(result | GDK_CONTROL_MASK);
    return result;
}

std::string accelerator(nk_menu_shortcut shortcut) {
    if (shortcut.key == NK_KEY_UNKNOWN)
        return {};
    const auto value = keyval(shortcut.key);
    if (!value)
        return {};
    gchar *name = gtk_accelerator_name(value, shortcut_modifiers(shortcut.modifiers));
    if (!name)
        return {};
    std::string result{name};
    g_free(name);
    return result;
}

std::string item_accelerator(const nk::core::MenuItemResource &resource) {
    auto shortcut = resource.shortcut;
    if (shortcut.key == NK_KEY_UNKNOWN)
        shortcut = default_role_shortcut(resource.role);
    return accelerator(shortcut);
}

void on_action_activate(GSimpleAction *, GVariant *, gpointer data) {
    nk::core::menu_item_activated(static_cast<nk_menu_item>(GPOINTER_TO_UINT(data)));
}

void add_actions(const std::vector<nk_menu_item> &children) {
    for (const auto handle : children) {
        const auto resource = item(handle);
        if (!resource)
            continue;
        if (resource->kind == NK_MENU_ITEM_SUBMENU) {
            add_actions(resource->children);
            continue;
        }
        if (resource->kind == NK_MENU_ITEM_SEPARATOR)
            continue;
        const auto name = action_name(handle);
        GSimpleAction *action = nullptr;
        if (resource->kind == NK_MENU_ITEM_CHECKBOX || resource->kind == NK_MENU_ITEM_RADIO) {
            action = g_simple_action_new_stateful(
                name.c_str(), G_VARIANT_TYPE_BOOLEAN,
                g_variant_new_boolean((resource->flags & NK_MENU_ITEM_CHECKED) != 0));
        } else {
            action = g_simple_action_new(name.c_str(), nullptr);
        }
        if (!action)
            continue;
        g_signal_connect(action, "activate", G_CALLBACK(on_action_activate),
                         GUINT_TO_POINTER(handle));
        g_action_map_add_action(G_ACTION_MAP(state.application), G_ACTION(action));
        g_object_unref(action);
        state.actions.emplace(handle, name);
        const auto shortcut = item_accelerator(*resource);
        const auto detailed = action_detailed_name(handle);
        if (shortcut.empty()) {
            const char *empty_accels[] = {nullptr};
            gtk_application_set_accels_for_action(state.application, detailed.c_str(),
                                                  empty_accels);
        } else {
            const char *accels[] = {shortcut.c_str(), nullptr};
            gtk_application_set_accels_for_action(state.application, detailed.c_str(), accels);
        }
    }
}

void set_item_attributes(GMenuItem *native, const nk::core::MenuItemResource &resource) {
    if (resource.flags & NK_MENU_ITEM_DISABLED)
        g_menu_item_set_attribute(native, "enabled", "b", FALSE);
    if (resource.flags & NK_MENU_ITEM_HIDDEN)
        g_menu_item_set_attribute(native, "hidden", "b", TRUE);
    if (resource.kind == NK_MENU_ITEM_CHECKBOX)
        g_menu_item_set_attribute(native, "role", "s", "check");
    else if (resource.kind == NK_MENU_ITEM_RADIO)
        g_menu_item_set_attribute(native, "role", "s", "radio");
}

GMenuItem *make_item(const std::shared_ptr<nk::core::MenuItemResource> &resource) {
    if (!resource || resource->kind == NK_MENU_ITEM_SEPARATOR)
        return nullptr;
    const auto label = resource->label.c_str();
    GMenuItem *native = nullptr;
    if (resource->kind == NK_MENU_ITEM_SUBMENU) {
        auto *submenu = g_menu_new();
        GMenu *section = g_menu_new();
        for (const auto child : resource->children) {
            if (const auto child_resource = item(child)) {
                if (child_resource->kind == NK_MENU_ITEM_SEPARATOR) {
                    if (g_menu_model_get_n_items(G_MENU_MODEL(section)) > 0) {
                        g_menu_append_section(submenu, nullptr, G_MENU_MODEL(section));
                        g_object_unref(section);
                        section = g_menu_new();
                    }
                } else if (auto *child_native = make_item(child_resource)) {
                    g_menu_append_item(section, child_native);
                    g_object_unref(child_native);
                }
            }
        }
        if (g_menu_model_get_n_items(G_MENU_MODEL(section)) > 0)
            g_menu_append_section(submenu, nullptr, G_MENU_MODEL(section));
        g_object_unref(section);
        native = g_menu_item_new_submenu(label, G_MENU_MODEL(submenu));
        g_object_unref(submenu);
    } else {
        native = g_menu_item_new(label, action_detailed_name(resource->handle).c_str());
        set_item_attributes(native, *resource);
    }
    return native;
}

GMenu *build_menu(const std::vector<nk_menu_item> &children) {
    auto *menu = g_menu_new();
    auto *section = g_menu_new();
    for (const auto handle : children) {
        const auto resource = item(handle);
        if (!resource)
            continue;
        if (resource->kind == NK_MENU_ITEM_SEPARATOR) {
            if (g_menu_model_get_n_items(G_MENU_MODEL(section)) > 0) {
                g_menu_append_section(menu, nullptr, G_MENU_MODEL(section));
                g_object_unref(section);
                section = g_menu_new();
            }
            continue;
        }
        if (auto *native = make_item(resource)) {
            g_menu_append_item(section, native);
            g_object_unref(native);
        }
    }
    if (g_menu_model_get_n_items(G_MENU_MODEL(section)) > 0)
        g_menu_append_section(menu, nullptr, G_MENU_MODEL(section));
    g_object_unref(section);
    return menu;
}

void clear_models() {
    if (!state.application)
        return;
    gtk_application_set_app_menu(state.application, nullptr);
    gtk_application_set_menubar(state.application, nullptr);
    for (const auto &[handle, name] : state.actions) {
        (void)handle;
        g_action_map_remove_action(G_ACTION_MAP(state.application), name.c_str());
    }
    state.actions.clear();
    g_clear_object(&state.app_menu);
    g_clear_object(&state.menubar);
}

void install_models(const std::shared_ptr<nk::core::MenuResource> &resource) {
    clear_models();
    add_actions(resource->children);
    std::vector<nk_menu_item> app_children;
    std::vector<nk_menu_item> menubar_children;
    for (const auto handle : resource->children) {
        const auto child = item(handle);
        if (!child)
            continue;
        if (child->role != NK_MENU_ROLE_NONE)
            app_children.push_back(handle);
        else
            menubar_children.push_back(handle);
    }
    state.app_menu = build_menu(app_children);
    state.menubar = build_menu(menubar_children);
    if (g_menu_model_get_n_items(G_MENU_MODEL(state.app_menu)) > 0)
        gtk_application_set_app_menu(state.application, G_MENU_MODEL(state.app_menu));
    gtk_application_set_menubar(state.application, G_MENU_MODEL(state.menubar));
    state.handle = resource->handle;
}

std::string application_id() {
    const auto configured = nk::core::system_application_id();
    if (!configured.empty() && g_application_id_is_valid(configured.c_str()))
        return configured;
    return "org.nativekit.application";
}

} // namespace

namespace nk::backend {

bool menu_backend_initialize() noexcept {
    if (state.application)
        return true;
    state.application = gtk_application_new(application_id().c_str(), G_APPLICATION_NON_UNIQUE);
    if (!state.application)
        return false;
    GError *error = nullptr;
    if (!g_application_register(G_APPLICATION(state.application), nullptr, &error)) {
        if (error) {
            nk::core::set_error(error->message);
            g_error_free(error);
        }
        g_clear_object(&state.application);
        return false;
    }
    return true;
}

GtkApplication *menu_backend_application() noexcept {
    return state.application;
}

nk_result menu_install(const std::shared_ptr<nk::core::MenuResource> &menu) noexcept {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!menu_backend_initialize())
        return NK_ERROR_UNSUPPORTED;
    install_models(menu);
    return NK_OK;
}

void menu_detach(const std::shared_ptr<nk::core::MenuResource> &menu) noexcept {
    if (!menu || menu->handle != state.handle)
        return;
    clear_models();
    state.handle = NK_INVALID_HANDLE;
}

nk_result menu_item_added(const std::shared_ptr<nk::core::MenuResource> &menu,
                          const std::shared_ptr<nk::core::MenuItemResource> &) noexcept {
    if (menu && menu->handle == state.handle)
        install_models(menu);
    return NK_OK;
}

void menu_item_removed(const std::shared_ptr<nk::core::MenuResource> &menu,
                       const std::shared_ptr<nk::core::MenuItemResource> &) noexcept {
    if (menu && menu->handle == state.handle)
        install_models(menu);
}

nk_result menu_item_changed(const std::shared_ptr<nk::core::MenuResource> &menu,
                            const std::shared_ptr<nk::core::MenuItemResource> &) noexcept {
    if (menu && menu->handle == state.handle)
        install_models(menu);
    return NK_OK;
}

void menu_backend_shutdown() noexcept {
    if (!state.application)
        return;
    clear_models();
    g_clear_object(&state.application);
    state.handle = NK_INVALID_HANDLE;
}

} // namespace nk::backend
