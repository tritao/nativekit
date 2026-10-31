// A tray menu must not stop the caller's loop: nk_tray_show_menu returns while the popup is open,
// events still flow, a choice is reported after the nk_menu was destroyed, and destroying the tray
// closes the popup.

#include "nativekit.h"
#include "nativekit_menu.h"
#include "nativekit_time.h"
#include "nativekit_tray.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdlib>

namespace {

void check(bool condition, const char *what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        std::exit(1);
    }
}

BOOL CALLBACK find_popup(HWND window, LPARAM result) {
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    wchar_t name[16] = {};
    GetClassNameW(window, name, 16);
    if (process == GetCurrentProcessId() && IsWindowVisible(window) &&
        lstrcmpW(name, L"#32768") == 0) {
        *reinterpret_cast<HWND *>(result) = window;
        return FALSE;
    }
    return TRUE;
}

// This process's visible popup menu window, or null.
HWND open_popup() {
    HWND found = nullptr;
    EnumWindows(find_popup, reinterpret_cast<LPARAM>(&found));
    return found;
}

// Waits up to 10 ms for the next event; false when none came.
bool next_event(nk_event &event) {
    nk_wait_events_timeout(0.01);
    event = {};
    event.struct_size = sizeof(event);
    if (nk_poll_event(&event) != NK_OK || event.kind == NK_EVENT_NONE) {
        nk_event_release(&event);
        return false;
    }
    return true;
}

HWND wait_for_popup(bool open) {
    for (int attempt = 0; attempt < 300; ++attempt) {
        HWND popup = open_popup();
        if ((popup != nullptr) == open)
            return popup;
        nk_event event;
        if (next_event(event))
            nk_event_release(&event);
    }
    return open_popup();
}

nk_menu build_menu(nk_menu_item *out_item) {
    nk_menu_options menu_options = {};
    menu_options.struct_size = sizeof(menu_options);
    nk_menu menu = NK_INVALID_HANDLE;
    check(nk_menu_create(&menu_options, &menu) == NK_OK, "nk_menu_create");
    nk_menu_item_options options = {};
    options.struct_size = sizeof(options);
    options.label = "Stop hosting";
    options.command_id = 7;
    check(nk_menu_add_item(menu, NK_INVALID_HANDLE, &options, out_item) == NK_OK,
          "nk_menu_add_item");
    return menu;
}

} // namespace

int main() {
    nk_init_options init = {};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    init.application_name = "NativeKit Windows tray menu test";
    check(nk_init(&init) == NK_OK, "nk_init");

    nk_tray_options tray_options = {};
    tray_options.struct_size = sizeof(tray_options);
    tray_options.tooltip = "NativeKit tray menu test";
    nk_tray tray = NK_INVALID_HANDLE;
    if (nk_tray_create(&tray_options, &tray) != NK_OK) {
        std::printf("SKIP: no notification area\n");
        nk_shutdown();
        return 0;
    }

    // 1. The call returns with the popup open; the caller's loop keeps running and can post itself
    // work.
    nk_menu_item item = NK_INVALID_HANDLE;
    nk_menu menu = build_menu(&item);
    check(nk_tray_show_menu(tray, menu) == NK_OK, "nk_tray_show_menu");
    check(nk_menu_destroy(menu) == NK_OK, "the menu can be destroyed while its popup is open");
    HWND popup = wait_for_popup(true);
    check(popup != nullptr, "the popup did not open");
    check(GetWindowThreadProcessId(popup, nullptr) != GetCurrentThreadId(),
          "the popup runs on the caller's thread");
    nk_event event = {};
    event.struct_size = sizeof(event);
    check(nk_poll_event(&event) == NK_OK, "events can be polled while the menu is open");
    nk_event_release(&event);
    check(nk_tray_show_menu(tray, NK_INVALID_HANDLE) == NK_OK,
          "a second show while one is open is a no-op");

    // 2. A choice made in the popup arrives as an event, although its nk_menu is gone.
    PostMessageW(popup, WM_KEYDOWN, VK_DOWN, 0);
    PostMessageW(popup, WM_KEYDOWN, VK_RETURN, 0);
    bool activated = false;
    for (int attempt = 0; attempt < 300 && !activated; ++attempt) {
        nk_event next;
        if (next_event(next)) {
            activated =
                next.kind == NK_EVENT_MENU_ITEM_ACTIVATED && next.source == item &&
                next.data_size >= 8 &&
                reinterpret_cast<const nk_menu_item_activated_event *>(next.data)->command_id == 7;
            nk_event_release(&next);
        }
    }
    check(activated, "the chosen item was not reported");
    check(wait_for_popup(false) == nullptr, "the popup stayed open after a choice");

    // 3. Destroying the tray closes an open popup without a choice.
    menu = build_menu(&item);
    check(nk_tray_show_menu(tray, menu) == NK_OK, "nk_tray_show_menu again");
    check(nk_menu_destroy(menu) == NK_OK, "nk_menu_destroy");
    check(wait_for_popup(true) != nullptr, "the second popup did not open");
    check(nk_tray_destroy(tray) == NK_OK, "nk_tray_destroy");
    check(open_popup() == nullptr, "destroying the tray left its popup open");
    for (int attempt = 0; attempt < 20; ++attempt) {
        nk_event next;
        if (next_event(next)) {
            check(next.kind != NK_EVENT_MENU_ITEM_ACTIVATED, "a closed popup reported a choice");
            nk_event_release(&next);
        }
    }
    nk_shutdown();
    std::printf("PASS: tray menu runs beside the caller's loop\n");
    return 0;
}
