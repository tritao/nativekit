#include "windows/nativekit_win_menu.hpp"

#include "core/error.hpp"
#include "core/menu_internal.hpp"
#include "core/runtime.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

struct WindowsMenuState {
    nk_menu handle = NK_INVALID_HANDLE;
    HMENU menu = nullptr;
    HACCEL accelerators = nullptr;
    std::unordered_set<HWND> windows;
    std::unordered_map<UINT, nk_menu_item> items;
};

WindowsMenuState state;

std::shared_ptr<nk::core::MenuItemResource> item(nk_menu_item handle) {
    return std::static_pointer_cast<nk::core::MenuItemResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::menu_item));
}

std::wstring wide(const std::string &value) {
    if (value.empty())
        return {};
    const int size =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, nullptr, 0);
    if (!size)
        return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.c_str(), -1, result.data(), size))
        return {};
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
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

WORD virtual_key(nk_key key) {
    if (key >= NK_KEY_0 && key <= NK_KEY_9)
        return static_cast<WORD>('0' + key - NK_KEY_0);
    if (key >= NK_KEY_A && key <= NK_KEY_Z)
        return static_cast<WORD>('A' + key - NK_KEY_A);
    if (key >= NK_KEY_F1 && key <= NK_KEY_F24)
        return static_cast<WORD>(VK_F1 + key - NK_KEY_F1);
    if (key >= NK_KEY_KP_0 && key <= NK_KEY_KP_9)
        return static_cast<WORD>(VK_NUMPAD0 + key - NK_KEY_KP_0);
    switch (key) {
    case NK_KEY_SPACE:
        return VK_SPACE;
    case NK_KEY_APOSTROPHE:
        return VK_OEM_7;
    case NK_KEY_COMMA:
        return VK_OEM_COMMA;
    case NK_KEY_MINUS:
        return VK_OEM_MINUS;
    case NK_KEY_PERIOD:
        return VK_OEM_PERIOD;
    case NK_KEY_SLASH:
        return VK_OEM_2;
    case NK_KEY_SEMICOLON:
        return VK_OEM_1;
    case NK_KEY_EQUAL:
        return VK_OEM_PLUS;
    case NK_KEY_LEFT_BRACKET:
        return VK_OEM_4;
    case NK_KEY_BACKSLASH:
        return VK_OEM_5;
    case NK_KEY_RIGHT_BRACKET:
        return VK_OEM_6;
    case NK_KEY_GRAVE_ACCENT:
        return VK_OEM_3;
    case NK_KEY_ESCAPE:
        return VK_ESCAPE;
    case NK_KEY_ENTER:
        return VK_RETURN;
    case NK_KEY_TAB:
        return VK_TAB;
    case NK_KEY_BACKSPACE:
        return VK_BACK;
    case NK_KEY_INSERT:
        return VK_INSERT;
    case NK_KEY_DELETE:
        return VK_DELETE;
    case NK_KEY_RIGHT:
        return VK_RIGHT;
    case NK_KEY_LEFT:
        return VK_LEFT;
    case NK_KEY_DOWN:
        return VK_DOWN;
    case NK_KEY_UP:
        return VK_UP;
    case NK_KEY_PAGE_UP:
        return VK_PRIOR;
    case NK_KEY_PAGE_DOWN:
        return VK_NEXT;
    case NK_KEY_HOME:
        return VK_HOME;
    case NK_KEY_END:
        return VK_END;
    case NK_KEY_KP_DECIMAL:
        return VK_DECIMAL;
    case NK_KEY_KP_DIVIDE:
        return VK_DIVIDE;
    case NK_KEY_KP_MULTIPLY:
        return VK_MULTIPLY;
    case NK_KEY_KP_SUBTRACT:
        return VK_SUBTRACT;
    case NK_KEY_KP_ADD:
        return VK_ADD;
    case NK_KEY_KP_ENTER:
        return VK_RETURN;
    case NK_KEY_KP_EQUAL:
        return VK_OEM_PLUS;
    default:
        return 0;
    }
}

std::wstring key_name(nk_key key) {
    if (key >= NK_KEY_0 && key <= NK_KEY_9)
        return std::wstring(1, static_cast<wchar_t>('0' + key - NK_KEY_0));
    if (key >= NK_KEY_A && key <= NK_KEY_Z)
        return std::wstring(1, static_cast<wchar_t>('A' + key - NK_KEY_A));
    if (key >= NK_KEY_F1 && key <= NK_KEY_F24)
        return L"F" + std::to_wstring(key - NK_KEY_F1 + 1);
    if (key >= NK_KEY_KP_0 && key <= NK_KEY_KP_9)
        return L"Num " + std::to_wstring(key - NK_KEY_KP_0);
    switch (key) {
    case NK_KEY_SPACE:
        return L"Space";
    case NK_KEY_APOSTROPHE:
        return L"'";
    case NK_KEY_COMMA:
        return L",";
    case NK_KEY_MINUS:
        return L"-";
    case NK_KEY_PERIOD:
        return L".";
    case NK_KEY_SLASH:
        return L"/";
    case NK_KEY_SEMICOLON:
        return L";";
    case NK_KEY_EQUAL:
        return L"=";
    case NK_KEY_LEFT_BRACKET:
        return L"[";
    case NK_KEY_BACKSLASH:
        return L"\\";
    case NK_KEY_RIGHT_BRACKET:
        return L"]";
    case NK_KEY_GRAVE_ACCENT:
        return L"`";
    case NK_KEY_ESCAPE:
        return L"Esc";
    case NK_KEY_ENTER:
    case NK_KEY_KP_ENTER:
        return L"Enter";
    case NK_KEY_TAB:
        return L"Tab";
    case NK_KEY_BACKSPACE:
        return L"Backspace";
    case NK_KEY_INSERT:
        return L"Insert";
    case NK_KEY_DELETE:
        return L"Delete";
    case NK_KEY_RIGHT:
        return L"Right";
    case NK_KEY_LEFT:
        return L"Left";
    case NK_KEY_DOWN:
        return L"Down";
    case NK_KEY_UP:
        return L"Up";
    case NK_KEY_PAGE_UP:
        return L"Page Up";
    case NK_KEY_PAGE_DOWN:
        return L"Page Down";
    case NK_KEY_HOME:
        return L"Home";
    case NK_KEY_END:
        return L"End";
    case NK_KEY_KP_DECIMAL:
        return L"Num .";
    case NK_KEY_KP_DIVIDE:
        return L"Num /";
    case NK_KEY_KP_MULTIPLY:
        return L"Num *";
    case NK_KEY_KP_SUBTRACT:
        return L"Num -";
    case NK_KEY_KP_ADD:
        return L"Num +";
    case NK_KEY_KP_EQUAL:
        return L"Num =";
    default:
        return {};
    }
}

std::wstring shortcut_label(nk_menu_shortcut shortcut) {
    if (shortcut.key == NK_KEY_UNKNOWN)
        return {};
    std::wstring result;
    const bool control = (shortcut.modifiers & NK_MENU_MOD_PRIMARY) != 0 ||
                         (shortcut.modifiers & NK_MENU_MOD_CONTROL) != 0;
    if (control)
        result += L"Ctrl+";
    if (shortcut.modifiers & NK_MENU_MOD_SHIFT)
        result += L"Shift+";
    if (shortcut.modifiers & NK_MENU_MOD_ALT)
        result += L"Alt+";
    result += key_name(shortcut.key);
    return result;
}

std::wstring item_label(const nk::core::MenuItemResource &resource) {
    auto result = wide(resource.label);
    auto shortcut = resource.shortcut;
    if (shortcut.key == NK_KEY_UNKNOWN)
        shortcut = default_role_shortcut(resource.role);
    if (const auto suffix = shortcut_label(shortcut); !suffix.empty()) {
        result += L"\t";
        result += suffix;
    }
    return result;
}

struct BuildState {
    UINT next_id = 1;
    std::unordered_map<UINT, nk_menu_item> items;
    std::vector<ACCEL> accelerators;
};

bool allocate_id(BuildState &build, nk_menu_item handle, UINT &out_id) {
    while (build.next_id <= 0xffffu && build.items.count(build.next_id) != 0)
        ++build.next_id;
    if (build.next_id > 0xffffu)
        return false;
    out_id = build.next_id++;
    build.items.emplace(out_id, handle);
    return true;
}

UINT item_state(const nk::core::MenuItemResource &resource) {
    UINT result = MFS_ENABLED;
    if (resource.flags & NK_MENU_ITEM_DISABLED)
        result |= MFS_DISABLED | MFS_GRAYED;
    if (resource.flags & NK_MENU_ITEM_CHECKED)
        result |= MFS_CHECKED;
    return result;
}

bool append_item(HMENU parent, const std::shared_ptr<nk::core::MenuItemResource> &resource,
                 BuildState &build) {
    if (!resource)
        return true;
    if (resource->flags & NK_MENU_ITEM_HIDDEN)
        return true;
    if (resource->kind == NK_MENU_ITEM_SEPARATOR)
        return AppendMenuW(parent, MF_SEPARATOR, 0, nullptr) != FALSE;
    const auto label = item_label(*resource);
    if (label.empty())
        return false;
    if (resource->kind == NK_MENU_ITEM_SUBMENU) {
        HMENU submenu = CreatePopupMenu();
        if (!submenu)
            return false;
        for (const auto child : resource->children) {
            if (!append_item(submenu, item(child), build)) {
                DestroyMenu(submenu);
                return false;
            }
        }
        MENUITEMINFOW native{};
        native.cbSize = sizeof(native);
        native.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_STRING | MIIM_SUBMENU;
        native.fType = MFT_STRING;
        native.fState = item_state(*resource);
        native.hSubMenu = submenu;
        native.dwTypeData = const_cast<wchar_t *>(label.c_str());
        native.cch = static_cast<UINT>(label.size());
        if (!InsertMenuItemW(parent, GetMenuItemCount(parent), TRUE, &native)) {
            DestroyMenu(submenu);
            return false;
        }
        return true;
    }
    UINT id = 0;
    if (!allocate_id(build, resource->handle, id))
        return false;
    MENUITEMINFOW native{};
    native.cbSize = sizeof(native);
    native.fMask = MIIM_FTYPE | MIIM_ID | MIIM_STATE | MIIM_STRING;
    native.fType = MFT_STRING;
    if (resource->kind == NK_MENU_ITEM_RADIO)
        native.fType |= MFT_RADIOCHECK;
    native.wID = id;
    native.fState = item_state(*resource);
    native.dwTypeData = const_cast<wchar_t *>(label.c_str());
    native.cch = static_cast<UINT>(label.size());
    if (!InsertMenuItemW(parent, GetMenuItemCount(parent), TRUE, &native))
        return false;

    auto shortcut = resource->shortcut;
    if (shortcut.key == NK_KEY_UNKNOWN)
        shortcut = default_role_shortcut(resource->role);
    const auto key = virtual_key(shortcut.key);
    if (key) {
        BYTE modifiers = FVIRTKEY;
        if ((shortcut.modifiers & NK_MENU_MOD_PRIMARY) ||
            (shortcut.modifiers & NK_MENU_MOD_CONTROL))
            modifiers |= FCONTROL;
        if (shortcut.modifiers & NK_MENU_MOD_SHIFT)
            modifiers |= FSHIFT;
        if (shortcut.modifiers & NK_MENU_MOD_ALT)
            modifiers |= FALT;
        build.accelerators.push_back(ACCEL{modifiers, key, static_cast<WORD>(id)});
    }
    return true;
}

bool build_menu(const std::shared_ptr<nk::core::MenuResource> &resource, HMENU &out_menu,
                HACCEL &out_accelerators, std::unordered_map<UINT, nk_menu_item> &out_items) {
    BuildState build;
    HMENU menu = CreateMenu();
    if (!menu)
        return false;
    for (const auto child : resource->children) {
        if (!append_item(menu, item(child), build)) {
            DestroyMenu(menu);
            return false;
        }
    }
    HACCEL accelerators = nullptr;
    if (!build.accelerators.empty()) {
        accelerators = CreateAcceleratorTableW(build.accelerators.data(),
                                               static_cast<int>(build.accelerators.size()));
        if (!accelerators) {
            DestroyMenu(menu);
            return false;
        }
    }
    out_menu = menu;
    out_accelerators = accelerators;
    out_items = std::move(build.items);
    return true;
}

void release_native_menu(HMENU menu, HACCEL accelerators) {
    for (const auto window : state.windows) {
        if (IsWindow(window)) {
            SetMenu(window, nullptr);
            DrawMenuBar(window);
        }
    }
    if (menu)
        DestroyMenu(menu);
    if (accelerators)
        DestroyAcceleratorTable(accelerators);
}

nk_result rebuild(const std::shared_ptr<nk::core::MenuResource> &resource) noexcept {
    HMENU menu = nullptr;
    HACCEL accelerators = nullptr;
    std::unordered_map<UINT, nk_menu_item> items;
    if (!build_menu(resource, menu, accelerators, items)) {
        nk::core::set_error("Windows could not create the native application menu");
        return NK_ERROR_UNKNOWN;
    }
    const auto old_menu = state.menu;
    const auto old_accelerators = state.accelerators;
    for (const auto window : state.windows) {
        if (IsWindow(window)) {
            SetMenu(window, menu);
            DrawMenuBar(window);
        }
    }
    state.menu = menu;
    state.accelerators = accelerators;
    state.items = std::move(items);
    state.handle = resource->handle;
    if (old_menu)
        DestroyMenu(old_menu);
    if (old_accelerators)
        DestroyAcceleratorTable(old_accelerators);
    return NK_OK;
}

void detach() noexcept {
    release_native_menu(state.menu, state.accelerators);
    state.menu = nullptr;
    state.accelerators = nullptr;
    state.items.clear();
    state.handle = NK_INVALID_HANDLE;
}

} // namespace

namespace nk::backend {

nk_result menu_install(const std::shared_ptr<nk::core::MenuResource> &menu) noexcept {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    return rebuild(menu);
}

void menu_detach(const std::shared_ptr<nk::core::MenuResource> &menu) noexcept {
    if (!menu || menu->handle != state.handle)
        return;
    detach();
}

nk_result menu_item_added(const std::shared_ptr<nk::core::MenuResource> &menu,
                          const std::shared_ptr<nk::core::MenuItemResource> &) noexcept {
    if (!menu || menu->handle != state.handle)
        return NK_OK;
    return rebuild(menu);
}

void menu_item_removed(const std::shared_ptr<nk::core::MenuResource> &menu,
                       const std::shared_ptr<nk::core::MenuItemResource> &) noexcept {
    if (menu && menu->handle == state.handle)
        (void)rebuild(menu);
}

nk_result menu_item_changed(const std::shared_ptr<nk::core::MenuResource> &menu,
                            const std::shared_ptr<nk::core::MenuItemResource> &) noexcept {
    if (!menu || menu->handle != state.handle)
        return NK_OK;
    return rebuild(menu);
}

void menu_backend_window_created(HWND window) noexcept {
    if (!window)
        return;
    state.windows.insert(window);
    if (state.menu) {
        SetMenu(window, state.menu);
        DrawMenuBar(window);
    }
}

void menu_backend_window_destroying(HWND window) noexcept {
    if (!window)
        return;
    if (state.menu && IsWindow(window)) {
        SetMenu(window, nullptr);
        DrawMenuBar(window);
    }
    state.windows.erase(window);
}

bool menu_backend_handle_message(HWND, UINT message, WPARAM wparam, LPARAM lparam) noexcept {
    if (message != WM_COMMAND || lparam != 0)
        return false;
    const auto found = state.items.find(LOWORD(wparam));
    if (found == state.items.end())
        return false;
    nk::core::menu_item_activated(found->second);
    return true;
}

bool menu_backend_translate_accelerator(MSG &message) noexcept {
    if (!state.accelerators || !message.hwnd)
        return false;
    const auto root = GetAncestor(message.hwnd, GA_ROOT);
    if (!root || state.windows.find(root) == state.windows.end())
        return false;
    return TranslateAcceleratorW(root, state.accelerators, &message) != 0;
}

void menu_backend_shutdown() noexcept {
    detach();
    state.windows.clear();
}

} // namespace nk::backend
