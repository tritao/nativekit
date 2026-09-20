#include "nativekit.h"
#include "nativekit_menu.h"
#include "nativekit_time.h"
#include "nativekit_window.h"

#include <gtk/gtk.h>

#include <cassert>
#include <cstring>

namespace {

bool poll_activation(nk_menu_item item) {
    for (int attempt = 0; attempt != 20; ++attempt) {
        nk_event event{};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        const bool matched = event.kind == NK_EVENT_MENU_ITEM_ACTIVATED && event.source == item;
        nk_event_release(&event);
        if (matched)
            return true;
        nk_wait_events_timeout(0.01);
    }
    return false;
}

GMenuModel *section_at(GMenuModel *menu, gint index) {
    return g_menu_model_get_item_link(menu, index, G_MENU_LINK_SECTION);
}

} // namespace

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    init.application_id = "org.nativekit.gtk.menu.test";
    init.application_name = "NativeKit GTK menu test";
    assert(nk_init(&init) == NK_OK);
    assert((nk_get_capabilities() & NK_CAP_APPLICATION_MENU) != 0);

    nk_menu_options menu_options{};
    menu_options.struct_size = sizeof(menu_options);
    menu_options.title = "NativeKit";
    nk_menu menu = NK_INVALID_HANDLE;
    assert(nk_menu_create(&menu_options, &menu) == NK_OK);

    nk_menu_item_options about_options{};
    about_options.struct_size = sizeof(about_options);
    about_options.label = "About NativeKit";
    about_options.role = NK_MENU_ROLE_ABOUT;
    about_options.command_id = 1;
    nk_menu_item about = NK_INVALID_HANDLE;
    assert(nk_menu_add_item(menu, NK_INVALID_HANDLE, &about_options, &about) == NK_OK);

    nk_menu_item_options file_options{};
    file_options.struct_size = sizeof(file_options);
    file_options.kind = NK_MENU_ITEM_SUBMENU;
    file_options.label = "File";
    nk_menu_item file = NK_INVALID_HANDLE;
    assert(nk_menu_add_item(menu, NK_INVALID_HANDLE, &file_options, &file) == NK_OK);

    nk_menu_item_options command_options{};
    command_options.struct_size = sizeof(command_options);
    command_options.label = "Close";
    command_options.command_id = 2;
    command_options.shortcut.key = NK_KEY_W;
    command_options.shortcut.modifiers = NK_MENU_MOD_PRIMARY;
    nk_menu_item close = NK_INVALID_HANDLE;
    assert(nk_menu_add_item(menu, file, &command_options, &close) == NK_OK);
    assert(nk_application_set_menu(menu) == NK_OK);

    auto *application = g_application_get_default();
    assert(application != nullptr && GTK_IS_APPLICATION(application));
    auto *gtk_application = GTK_APPLICATION(application);
    auto *app_menu = gtk_application_get_app_menu(gtk_application);
    auto *menubar = gtk_application_get_menubar(gtk_application);
    assert(app_menu != nullptr && menubar != nullptr);
    assert(g_menu_model_get_n_items(app_menu) == 1);
    assert(g_menu_model_get_n_items(menubar) == 1);
    auto *bus = g_application_get_dbus_connection(application);
    assert(bus != nullptr);
    GError *introspection_error = nullptr;
    auto *introspection = g_dbus_connection_call_sync(
        bus, g_dbus_connection_get_unique_name(bus), "/org/nativekit/gtk/menu/test/menus/MenuBar",
        "org.freedesktop.DBus.Introspectable", "Introspect", nullptr, G_VARIANT_TYPE("(s)"),
        G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, &introspection_error);
    assert(introspection != nullptr && introspection_error == nullptr);
    g_variant_unref(introspection);

    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.flags = NK_WINDOW_HIDDEN;
    window_options.width = 320;
    window_options.height = 240;
    window_options.title = "NativeKit GTK menu window";
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);
    auto *windows = gtk_application_get_windows(gtk_application);
    assert(windows != nullptr && GTK_IS_APPLICATION_WINDOW(windows->data));
    assert(gtk_application_window_get_show_menubar(GTK_APPLICATION_WINDOW(windows->data)));

    auto *app_section = section_at(app_menu, 0);
    assert(app_section != nullptr);
    GVariant *about_action = g_menu_model_get_item_attribute_value(
        app_section, 0, G_MENU_ATTRIBUTE_ACTION, G_VARIANT_TYPE_STRING);
    assert(about_action != nullptr);
    const auto *about_action_name = g_variant_get_string(about_action, nullptr);
    assert(std::strncmp(about_action_name, "app.", 4) == 0);
    g_action_group_activate_action(G_ACTION_GROUP(application), about_action_name + 4, nullptr);
    g_variant_unref(about_action);
    assert(poll_activation(about));

    auto *menubar_section = section_at(menubar, 0);
    assert(menubar_section != nullptr);
    auto *file_model = g_menu_model_get_item_link(menubar_section, 0, G_MENU_LINK_SUBMENU);
    assert(file_model != nullptr);
    auto *file_section = section_at(file_model, 0);
    assert(file_section != nullptr);
    GVariant *close_action = g_menu_model_get_item_attribute_value(
        file_section, 0, G_MENU_ATTRIBUTE_ACTION, G_VARIANT_TYPE_STRING);
    assert(close_action != nullptr);
    const auto *close_action_name = g_variant_get_string(close_action, nullptr);
    assert(std::strncmp(close_action_name, "app.", 4) == 0);
    auto *accelerators = gtk_application_get_accels_for_action(gtk_application, close_action_name);
    assert(accelerators != nullptr && accelerators[0] != nullptr);
    g_strfreev(accelerators);
    g_action_group_activate_action(G_ACTION_GROUP(application), close_action_name + 4, nullptr);
    g_variant_unref(close_action);
    assert(poll_activation(close));

    g_object_unref(file_section);
    g_object_unref(file_model);
    g_object_unref(menubar_section);
    g_object_unref(app_section);
    assert(nk_window_destroy(window) == NK_OK);
    assert(nk_application_set_menu(NK_INVALID_HANDLE) == NK_OK);
    assert(nk_menu_destroy(menu) == NK_OK);
    nk_shutdown();
    return 0;
}
