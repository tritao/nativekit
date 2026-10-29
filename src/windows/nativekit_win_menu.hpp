#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "core/menu_internal.hpp"
#include "nativekit_menu.h"

#include <memory>
#include <unordered_map>

namespace nk::backend {

void menu_backend_shutdown() noexcept;
void menu_backend_window_created(HWND window) noexcept;
void menu_backend_window_destroying(HWND window) noexcept;
bool menu_backend_handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
bool menu_backend_translate_accelerator(MSG &message) noexcept;
/** Shows `menu` as a popup owned by `owner` at the pointer and reports the chosen item. */
nk_result menu_backend_popup(nk_menu menu, HWND owner) noexcept;

/**
 * A native copy of a menu for TrackPopupMenuEx with TPM_RETURNCMD. It keeps its items alive, so
 * the choice can be reported after the nk_menu is destroyed, and its HMENU may be shown from any
 * thread (menus are not bound to a thread; the owner window is).
 */
struct PopupMenu {
    HMENU menu = nullptr;
    std::unordered_map<UINT, std::shared_ptr<nk::core::MenuItemResource>> items;
};
/** Builds `out` from `menu` on the UI thread; the caller frees it with menu_backend_popup_free. */
nk_result menu_backend_popup_build(nk_menu menu, PopupMenu &out) noexcept;
/** Reports the item TrackPopupMenuEx returned (0: nothing chosen). UI thread. */
void menu_backend_popup_chosen(const PopupMenu &popup, UINT chosen) noexcept;
void menu_backend_popup_free(PopupMenu &popup) noexcept;

} // namespace nk::backend
