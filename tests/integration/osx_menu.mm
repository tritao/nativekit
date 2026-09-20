#import <Cocoa/Cocoa.h>

#include "nativekit.h"
#include "nativekit_menu.h"
#include "nativekit_window.h"

#include <cassert>

int main() {
    nk_init_options init = {};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    init.application_name = "NativeKit menu test";
    assert(nk_init(&init) == NK_OK);
    assert((nk_get_capabilities() & NK_CAP_APPLICATION_MENU) != 0);

    nk_menu_options menu_options = {};
    menu_options.struct_size = sizeof(menu_options);
    menu_options.title = "NativeKit";
    nk_menu menu = NK_INVALID_HANDLE;
    assert(nk_menu_create(&menu_options, &menu) == NK_OK);
    nk_menu_item_options submenu_options = {};
    submenu_options.struct_size = sizeof(submenu_options);
    submenu_options.kind = NK_MENU_ITEM_SUBMENU;
    submenu_options.label = "File";
    nk_menu_item submenu = NK_INVALID_HANDLE;
    assert(nk_menu_add_item(menu, NK_INVALID_HANDLE, &submenu_options, &submenu) == NK_OK);
    nk_menu_item_options command_options = {};
    command_options.struct_size = sizeof(command_options);
    command_options.label = "Close";
    command_options.command_id = 7;
    nk_menu_item command = NK_INVALID_HANDLE;
    assert(nk_menu_add_item(menu, submenu, &command_options, &command) == NK_OK);
    assert(nk_application_set_menu(menu) == NK_OK);

    NSMenu *native_root = NSApp.mainMenu;
    assert(native_root != nil && native_root.numberOfItems == 1);
    NSMenuItem *native_submenu = [native_root itemAtIndex:0];
    assert(native_submenu.submenu != nil && native_submenu.submenu.numberOfItems == 1);
    NSMenuItem *native_command = [native_submenu.submenu itemAtIndex:0];
    [native_command.target performSelector:native_command.action withObject:native_command];

    nk_event event = {};
    event.struct_size = sizeof(event);
    assert(nk_poll_event(&event) == NK_OK);
    assert(event.kind == NK_EVENT_MENU_ITEM_ACTIVATED);
    assert(event.source == command);
    assert(event.data_size == sizeof(nk_menu_item_activated_event));
    const auto *payload = static_cast<const nk_menu_item_activated_event *>(event.data);
    assert(payload->command_id == 7 && payload->item == command);
    nk_event_release(&event);

    assert(nk_application_set_menu(NK_INVALID_HANDLE) == NK_OK);
    assert(nk_menu_destroy(menu) == NK_OK);
    nk_shutdown();
    return 0;
}
