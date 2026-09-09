/*
 * Win32 behavior in this file is informed by the wxWidgets MSW donor files
 * enumerated in tools/upstream-lock.json at its pinned revision. Adaptations are
 * licensed under the wxWindows Library Licence 3.1; see licenses/wxWidgets.txt.
 */

#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t window_class_name[] = L"NativeKitWindow";
ATOM window_class = 0;

struct WinWindowResource final : nk::core::Resource {
    HWND window = nullptr;
    nk_handle handle = NK_INVALID_HANDLE;
    ~WinWindowResource() override {
        if (window && IsWindow(window)) DestroyWindow(window);
    }
};

nk_result fail(nk_result result, std::string_view message) {
    nk::core::set_error(message);
    return result;
}

nk_result enter_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

std::wstring wide(const char* text) {
    if (!text || !*text) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (!size) return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), size);
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
}

template<typename T>
std::vector<std::byte> bytes_of(const T& value) {
    const auto* first = reinterpret_cast<const std::byte*>(&value);
    return {first, first + sizeof(value)};
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* resource = reinterpret_cast<WinWindowResource*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        resource = static_cast<WinWindowResource*>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(resource));
    }
    if (resource && resource->handle != NK_INVALID_HANDLE) {
        if (message == WM_CLOSE) {
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_WINDOW_CLOSE;
            event.source = resource->handle;
            nk::core::push_event(std::move(event));
            return 0;
        }
        if (message == WM_SIZE) {
            try {
                const nk_window_resize_event size{
                    static_cast<int32_t>(LOWORD(lparam)),
                    static_cast<int32_t>(HIWORD(lparam))};
                nk::core::QueuedEvent event;
                event.kind = NK_EVENT_WINDOW_RESIZE;
                event.source = resource->handle;
                event.data = bytes_of(size);
                nk::core::push_event(std::move(event));
            } catch (...) {}
        }
        if (message == WM_DPICHANGED) {
            try {
                const nk_window_scale_event scale{
                    static_cast<float>(HIWORD(wparam)) / 96.0f};
                nk::core::QueuedEvent event;
                event.kind = NK_EVENT_WINDOW_SCALE_CHANGED;
                event.source = resource->handle;
                event.data = bytes_of(scale);
                nk::core::push_event(std::move(event));
            } catch (...) {}
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            return 0;
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool ensure_window_class() {
    if (window_class) return true;
    WNDCLASSEXW definition{};
    definition.cbSize = sizeof(definition);
    definition.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    definition.lpfnWndProc = window_proc;
    definition.hInstance = GetModuleHandleW(nullptr);
    definition.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    definition.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    definition.lpszClassName = window_class_name;
    window_class = RegisterClassExW(&definition);
    return window_class != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

std::shared_ptr<WinWindowResource> get_window(nk_handle handle) {
    return std::dynamic_pointer_cast<WinWindowResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::window));
}

nk_result unsupported() {
    const auto thread = enter_ui();
    if (thread != NK_OK) return thread;
    return fail(NK_ERROR_UNSUPPORTED, "this Windows service is not implemented yet");
}

}

namespace nk::backend {
void pump_events() noexcept {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void shutdown() noexcept {
    nk::core::handles().clear();
    pump_events();
}
}

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_WINDOW | NK_CAP_EXPORT_NATIVE_WINDOW;
}

nk_result NK_CALL nk_window_create(const nk_window_options* options, nk_handle* out_window) {
    try {
        if (const auto result = enter_ui(); result != NK_OK) return result;
        if (!options || options->struct_size < sizeof(*options) || !out_window ||
            options->width <= 0 || options->height <= 0)
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid window options");
        *out_window = NK_INVALID_HANDLE;
        if (!ensure_window_class()) return fail(NK_ERROR_UNKNOWN, "could not register Win32 window class");
        auto resource = std::make_shared<WinWindowResource>();
        const auto title = wide(options->title);
        DWORD style = WS_OVERLAPPEDWINDOW;
        if (!(options->flags & NK_WINDOW_RESIZABLE))
            style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        RECT bounds{0, 0, options->width, options->height};
        AdjustWindowRectEx(&bounds, style, FALSE, 0);
        resource->window = CreateWindowExW(
            0, window_class_name, title.c_str(), style,
            CW_USEDEFAULT, CW_USEDEFAULT, bounds.right - bounds.left,
            bounds.bottom - bounds.top, nullptr, nullptr, GetModuleHandleW(nullptr),
            resource.get());
        if (!resource->window) return fail(NK_ERROR_UNKNOWN, "could not create Win32 window");
        resource->handle = nk::core::handles().insert(nk::core::ResourceType::window, resource);
        if (resource->handle == NK_INVALID_HANDLE)
            return fail(NK_ERROR_OUT_OF_MEMORY, "window handle registry is full");
        if (!(options->flags & NK_WINDOW_HIDDEN)) ShowWindow(resource->window, SW_SHOW);
        *out_window = resource->handle;
        return NK_OK;
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while creating window");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while creating window");
    }
}

nk_result NK_CALL nk_window_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = get_window(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    SetWindowLongPtrW(resource->window, GWLP_USERDATA, 0);
    DestroyWindow(resource->window);
    resource->window = nullptr;
    nk::core::handles().erase(handle, nk::core::ResourceType::window);
    return NK_OK;
}

nk_result NK_CALL nk_window_show(nk_handle handle, uint32_t visible) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = get_window(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    ShowWindow(resource->window, visible ? SW_SHOW : SW_HIDE);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_title(nk_handle handle, const char* title) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = get_window(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const auto value = wide(title);
    if (title && *title && value.empty()) return fail(NK_ERROR_INVALID_ARGUMENT, "title is not valid UTF-8");
    return SetWindowTextW(resource->window, value.c_str()) ? NK_OK
        : fail(NK_ERROR_UNKNOWN, "could not set window title");
}

nk_result NK_CALL nk_window_set_bounds(nk_handle handle, int32_t x, int32_t y,
                                       int32_t width, int32_t height) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (width <= 0 || height <= 0) return fail(NK_ERROR_INVALID_ARGUMENT, "window dimensions must be positive");
    auto resource = get_window(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    return SetWindowPos(resource->window, nullptr, x, y, width, height,
                        SWP_NOACTIVATE | SWP_NOZORDER) ? NK_OK
        : fail(NK_ERROR_UNKNOWN, "could not set window bounds");
}

nk_result NK_CALL nk_window_get_scale(nk_handle handle, float* out_scale) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (!out_scale) return fail(NK_ERROR_INVALID_ARGUMENT, "scale output must not be null");
    auto resource = get_window(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    using GetDpiForWindowFunction = UINT(WINAPI*)(HWND);
    const FARPROC address = GetProcAddress(
        GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    GetDpiForWindowFunction function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    *out_scale = static_cast<float>(function ? function(resource->window) : 96) / 96.0f;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_native(nk_handle handle, nk_native_window* out_native) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (!out_native || out_native->struct_size < sizeof(*out_native))
        return fail(NK_ERROR_INVALID_ARGUMENT, "native window output is missing or too small");
    auto resource = get_window(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const auto size = out_native->struct_size;
    *out_native = {};
    out_native->struct_size = size;
    out_native->kind = NK_NATIVE_WINDOW_WIN32;
    out_native->window = reinterpret_cast<uintptr_t>(resource->window);
    return NK_OK;
}

nk_result NK_CALL nk_window_wrap_native(const nk_native_window*, nk_handle*) { return unsupported(); }
nk_result NK_CALL nk_webview_create(nk_handle, const nk_webview_options*, nk_handle*) { return unsupported(); }
nk_result NK_CALL nk_webview_destroy(nk_handle) { return unsupported(); }
nk_result NK_CALL nk_webview_show(nk_handle, uint32_t) { return unsupported(); }
nk_result NK_CALL nk_webview_set_bounds(nk_handle, int32_t, int32_t, int32_t, int32_t) { return unsupported(); }
nk_result NK_CALL nk_webview_navigate(nk_handle, const char*) { return unsupported(); }
nk_result NK_CALL nk_webview_set_html(nk_handle, const char*, const char*) { return unsupported(); }
nk_result NK_CALL nk_webview_eval(nk_handle, const char*, nk_request_id*) { return unsupported(); }
nk_result NK_CALL nk_dialog_open_file(nk_handle, const nk_file_dialog_options*, nk_request_id*) { return unsupported(); }
nk_result NK_CALL nk_dialog_save_file(nk_handle, const nk_file_dialog_options*, nk_request_id*) { return unsupported(); }
nk_result NK_CALL nk_dialog_select_directory(nk_handle, const nk_file_dialog_options*, nk_request_id*) { return unsupported(); }
nk_result NK_CALL nk_dialog_message(nk_handle, const nk_message_dialog_options*, nk_request_id*) { return unsupported(); }
nk_result NK_CALL nk_dialog_cancel(nk_request_id) { return unsupported(); }
nk_result NK_CALL nk_clipboard_set_text(const char*) { return unsupported(); }
nk_result NK_CALL nk_clipboard_set_files(const char* const*, uint32_t) { return unsupported(); }
nk_result NK_CALL nk_clipboard_read_text(nk_request_id*) { return unsupported(); }
nk_result NK_CALL nk_clipboard_read_files(nk_request_id*) { return unsupported(); }
nk_result NK_CALL nk_window_set_drop_enabled(nk_handle, uint32_t) { return unsupported(); }
nk_result NK_CALL nk_shell_open_url(const char*) { return unsupported(); }
nk_result NK_CALL nk_shell_open_file(const char*) { return unsupported(); }
nk_result NK_CALL nk_shell_reveal_file(const char*) { return unsupported(); }
nk_result NK_CALL nk_system_directory(nk_system_directory_kind, char*, uint32_t*) { return unsupported(); }
nk_result NK_CALL nk_system_locale(char*, uint32_t*) { return unsupported(); }
nk_result NK_CALL nk_system_get_appearance(nk_system_appearance*) { return unsupported(); }

}
