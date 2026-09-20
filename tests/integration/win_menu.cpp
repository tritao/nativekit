#include "nativekit.h"
#include "nativekit_menu.h"
#include "nativekit_window.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cassert>
#include <cwchar>
#include <string>

namespace {

bool poll_menu_event(nk_menu_item item) {
    for (int attempt = 0; attempt < 32; ++attempt) {
        nk_event event = {};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_MENU_ITEM_ACTIVATED && event.source == item) {
            nk_event_release(&event);
            return true;
        }
        nk_event_release(&event);
    }
    return false;
}

std::wstring menu_label(HMENU menu, UINT position) {
    wchar_t text[128] = {};
    MENUITEMINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STRING;
    info.dwTypeData = text;
    info.cch = static_cast<UINT>(sizeof(text) / sizeof(text[0]));
    assert(GetMenuItemInfoW(menu, position, TRUE, &info) != FALSE);
    return text;
}

} // namespace

int main() {
    nk_init_options init = {};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    init.application_name = "NativeKit Windows menu test";
    assert(nk_init(&init) == NK_OK);
    assert((nk_get_capabilities() & NK_CAP_APPLICATION_MENU) != 0);

    nk_menu_options menu_options = {};
    menu_options.struct_size = sizeof(menu_options);
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
    command_options.command_id = 42;
    command_options.shortcut.key = NK_KEY_W;
    command_options.shortcut.modifiers = NK_MENU_MOD_PRIMARY;
    nk_menu_item command = NK_INVALID_HANDLE;
    assert(nk_menu_add_item(menu, submenu, &command_options, &command) == NK_OK);

    nk_menu_item_options checkbox_options = {};
    checkbox_options.struct_size = sizeof(checkbox_options);
    checkbox_options.kind = NK_MENU_ITEM_CHECKBOX;
    checkbox_options.label = "Show Toolbar";
    nk_menu_item checkbox = NK_INVALID_HANDLE;
    assert(nk_menu_add_item(menu, submenu, &checkbox_options, &checkbox) == NK_OK);

    nk_window_options window_options = {};
    window_options.struct_size = sizeof(window_options);
    window_options.flags = NK_WINDOW_HIDDEN | NK_WINDOW_RESIZABLE;
    window_options.width = 640;
    window_options.height = 480;
    window_options.title = "NativeKit Windows menu test";
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);

    assert(nk_application_set_menu(menu) == NK_OK);
    nk_native_window native = {};
    native.struct_size = sizeof(native);
    assert(nk_window_get_native(window, &native) == NK_OK);
    assert(native.kind == NK_NATIVE_WINDOW_WIN32 && native.window != 0);
    const auto hwnd = reinterpret_cast<HWND>(native.window);
    HMENU root = GetMenu(hwnd);
    assert(root != nullptr && GetMenuItemCount(root) == 1);
    HMENU file = GetSubMenu(root, 0);
    assert(file != nullptr && GetMenuItemCount(file) == 2);
    assert(menu_label(file, 0).find(L"Close\tCtrl+W") != std::wstring::npos);

    const UINT command_id = GetMenuItemID(file, 0);
    assert(command_id != static_cast<UINT>(-1));
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(command_id, 0), 0);
    assert(poll_menu_event(command));

    const UINT checkbox_id = GetMenuItemID(file, 1);
    assert(checkbox_id != static_cast<UINT>(-1));
    SendMessageW(hwnd, WM_COMMAND, MAKEWPARAM(checkbox_id, 0), 0);
    assert(poll_menu_event(checkbox));
    root = GetMenu(hwnd);
    file = GetSubMenu(root, 0);
    MENUITEMINFOW checkbox_state = {};
    checkbox_state.cbSize = sizeof(checkbox_state);
    checkbox_state.fMask = MIIM_STATE;
    assert(GetMenuItemInfoW(file, 1, TRUE, &checkbox_state) != FALSE);
    assert((checkbox_state.fState & MFS_CHECKED) != 0);

    assert(nk_menu_item_set_label(command, "Close Window") == NK_OK);
    root = GetMenu(hwnd);
    file = GetSubMenu(root, 0);
    assert(menu_label(file, 0).find(L"Close Window\tCtrl+W") != std::wstring::npos);

    nk_window second_window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &second_window) == NK_OK);
    nk_native_window second_native = {};
    second_native.struct_size = sizeof(second_native);
    assert(nk_window_get_native(second_window, &second_native) == NK_OK);
    assert(GetMenu(reinterpret_cast<HWND>(second_native.window)) != nullptr);

    assert(nk_application_set_menu(NK_INVALID_HANDLE) == NK_OK);
    assert(GetMenu(hwnd) == nullptr);
    assert(GetMenu(reinterpret_cast<HWND>(second_native.window)) == nullptr);
    assert(nk_window_destroy(second_window) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    assert(nk_menu_destroy(menu) == NK_OK);
    nk_shutdown();
    return 0;
}
