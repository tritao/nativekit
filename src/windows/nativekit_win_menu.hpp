#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace nk::backend {

void menu_backend_shutdown() noexcept;
void menu_backend_window_created(HWND window) noexcept;
void menu_backend_window_destroying(HWND window) noexcept;
bool menu_backend_handle_message(HWND window, UINT message, WPARAM wparam, LPARAM lparam) noexcept;
bool menu_backend_translate_accelerator(MSG &message) noexcept;

} // namespace nk::backend
