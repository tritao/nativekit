/*
 * Win32 behavior in this file is informed by the wxWidgets MSW donor files
 * enumerated in tools/upstream-lock.json at its pinned revision. Adaptations are
 * licensed under the wxWindows Library Licence 3.1; see licenses/wxWidgets.txt.
 */

#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_notification.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include "core/error.hpp"
#include "core/boundary.hpp"
#include "core/runtime.hpp"

#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>

#if defined(NK_HAS_WEBVIEW2)
#include <WebView2.h>
#include <wrl.h>
#endif

#include <atomic>
#include <algorithm>
#include <cstddef>
#include <cwchar>
#include <cstring>
#include <functional>
#include <memory>
#include <limits>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t window_class_name[] = L"NativeKitWindow";
constexpr UINT notification_message = WM_APP + 42;
ATOM window_class = 0;
HWND notification_window = nullptr;

struct WinNotification {
    nk_request_id request = NK_INVALID_REQUEST_ID;
    UINT id = 0;
};

std::unordered_map<nk_request_id, WinNotification> notifications;
std::unordered_map<UINT, nk_request_id> notification_ids;
std::atomic<UINT> next_notification_id{1};

struct WinDialogContext {
    nk_request_id request = NK_INVALID_REQUEST_ID;
    uint32_t kind = 0;
    HWND parent = nullptr;
    std::wstring title;
    std::wstring initial_path;
    std::wstring suggested_name;
    std::wstring message;
    uint32_t flags = 0;
    uint32_t message_kind = 0;
    uint32_t buttons = 0;
    std::vector<std::pair<std::wstring, std::wstring>> filters;
    std::atomic<DWORD> thread_id{0};
    std::atomic<bool> canceled{false};
    std::atomic<bool> complete{false};
    std::thread worker;
    uint64_t generation = 0;
};

std::mutex dialogs_mutex;
std::unordered_map<nk_request_id, std::shared_ptr<WinDialogContext>> dialogs;

#if defined(NK_HAS_WEBVIEW2)
using Microsoft::WRL::ComPtr;

template <typename Interface, const IID *InterfaceId, typename... Arguments>
class ComCallback final : public Interface {
  public:
    explicit ComCallback(std::function<HRESULT(Arguments...)> function)
        : function_(std::move(function)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void **output) override {
        if (!output)
            return E_POINTER;
        *output = nullptr;
        if (InlineIsEqualGUID(id, IID_IUnknown) || InlineIsEqualGUID(id, *InterfaceId)) {
            *output = static_cast<Interface *>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = --references_;
        if (!remaining)
            delete this;
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Arguments... arguments) override {
        return function_(arguments...);
    }

  private:
    std::atomic<ULONG> references_{1};
    std::function<HRESULT(Arguments...)> function_;
};

template <typename Interface, const IID *InterfaceId, typename... Arguments, typename Function>
ComPtr<Interface> make_callback(Function &&function) {
    ComPtr<Interface> result;
    result.Attach(new ComCallback<Interface, InterfaceId, Arguments...>(
        std::function<HRESULT(Arguments...)>(std::forward<Function>(function))));
    return result;
}

using CreateWebViewEnvironment =
    HRESULT(STDAPICALLTYPE *)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions *,
                              ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler *);
using GetWebViewVersion = HRESULT(STDAPICALLTYPE *)(PCWSTR, LPWSTR *);

HMODULE webview2_loader = nullptr;
CreateWebViewEnvironment create_webview_environment = nullptr;
GetWebViewVersion get_webview_version = nullptr;
bool webview_com_initialized = false;
std::atomic<uint32_t> pending_webview_creations{0};

enum class WebViewCommandKind { navigate, html, evaluate };

struct WebViewCommand {
    WebViewCommandKind kind;
    std::wstring value;
    std::wstring auxiliary;
    nk_request_id request = NK_INVALID_REQUEST_ID;
};

struct WinWebViewResource final : nk::core::Resource {
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle parent = NK_INVALID_HANDLE;
    RECT bounds{};
    bool visible = true;
    bool devtools = false;
    bool failed = false;
    bool ready = false;
    bool navigation_policy = false;
    std::atomic<bool> creation_pending{false};
    uint64_t generation = 0;
    std::wstring initial_url;
    std::wstring policy_bypass_url;
    std::unordered_set<uint64_t> policy_cancelled_navigation_ids;
    std::vector<WebViewCommand> pending;
    ComPtr<ICoreWebView2Environment> environment;
    ComPtr<ICoreWebView2Controller> controller;
    ComPtr<ICoreWebView2> webview;
    ~WinWebViewResource() override {
        if (controller)
            controller->Close();
    }
};

struct WinNavigationDecision {
    nk_handle source;
    std::wstring url;
};

std::unordered_map<nk_request_id, WinNavigationDecision> navigation_decisions;
std::unordered_map<nk_request_id, nk_handle> evaluations;
#endif

struct WinWindowResource final : nk::core::Resource {
    HWND window = nullptr;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle owner = NK_INVALID_HANDLE;
    bool modal = false;
    bool modal_active = false;
    uint32_t active_modal_children = 0;
    bool fullscreen = false;
    WINDOWPLACEMENT placement{};
    LONG_PTR windowed_style = 0;
    int32_t min_width = 0, min_height = 0, max_width = 0, max_height = 0;
    bool drops_enabled = false;
    std::vector<nk_handle> children;
    std::vector<nk_handle> owned_windows;
    ~WinWindowResource() override {
        if (window && IsWindow(window))
            DestroyWindow(window);
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

std::wstring wide(const char *text) {
    if (!text || !*text)
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (!size)
        return {};
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), size);
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
}

bool copy_wide(const char *source, std::wstring &destination) {
    destination = wide(source);
    return !source || !*source || !destination.empty();
}

std::string utf8(const wchar_t *text) {
    if (!text || !*text)
        return {};
    const int size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, nullptr, 0, nullptr, nullptr);
    if (!size)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1, result.data(), size, nullptr,
                        nullptr);
    result.resize(static_cast<std::size_t>(size - 1));
    return result;
}

#if defined(NK_HAS_WEBVIEW2)
std::wstring html_attribute(const std::wstring &value) {
    std::wstring result;
    result.reserve(value.size());
    for (const wchar_t character : value) {
        if (character == L'&')
            result += L"&amp;";
        else if (character == L'\"')
            result += L"&quot;";
        else if (character == L'<')
            result += L"&lt;";
        else if (character == L'>')
            result += L"&gt;";
        else
            result += character;
    }
    return result;
}
#endif

template <typename T> std::vector<std::byte> bytes_of(const T &value) {
    const auto *first = reinterpret_cast<const std::byte *>(&value);
    return {first, first + sizeof(value)};
}

std::vector<std::byte> text_bytes(const std::string &value) {
    const auto *first = reinterpret_cast<const std::byte *>(value.data());
    return {first, first + value.size()};
}

template <typename Header>
std::vector<std::byte> string_list_payload(Header header, const std::vector<std::string> &strings,
                                           uint32_t Header::*offset_member) {
    header.*offset_member = sizeof(Header);
    std::size_t total = sizeof(Header);
    for (const auto &value : strings)
        total += value.size() + 1;
    std::vector<std::byte> result(total);
    std::memcpy(result.data(), &header, sizeof(header));
    std::size_t cursor = sizeof(Header);
    for (const auto &value : strings) {
        std::memcpy(result.data() + cursor, value.c_str(), value.size() + 1);
        cursor += value.size() + 1;
    }
    return result;
}

bool open_clipboard() {
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (OpenClipboard(nullptr))
            return true;
        Sleep(5);
    }
    return false;
}

struct ClipboardCloseGuard {
    ~ClipboardCloseGuard() { CloseClipboard(); }
};

struct GlobalUnlockGuard {
    HGLOBAL memory;
    ~GlobalUnlockGuard() { GlobalUnlock(memory); }
};

std::wstring absolute_path(const char *path) {
    const auto native = wide(path);
    if (native.empty())
        return {};
    const DWORD size = GetFullPathNameW(native.c_str(), 0, nullptr, nullptr);
    if (!size)
        return {};
    std::wstring result(size, L'\0');
    const DWORD written = GetFullPathNameW(native.c_str(), size, result.data(), nullptr);
    if (!written || written >= size)
        return {};
    result.resize(written);
    return result;
}

void emit_drop_files(WinWindowResource &resource, HDROP drop) noexcept {
    try {
        POINT point{};
        DragQueryPoint(drop, &point);
        const UINT count = DragQueryFileW(drop, 0xffffffffu, nullptr, 0);
        std::vector<std::string> paths;
        paths.reserve(count);
        for (UINT index = 0; index < count; ++index) {
            const UINT length = DragQueryFileW(drop, index, nullptr, 0);
            std::wstring path(static_cast<std::size_t>(length) + 1, L'\0');
            if (DragQueryFileW(drop, index, path.data(), length + 1)) {
                path.resize(length);
                paths.push_back(utf8(path.c_str()));
            }
        }
        if (!paths.empty()) {
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_DROP_FILES;
            event.source = resource.handle;
            event.data_count = static_cast<uint32_t>(paths.size());
            nk_drop_data header{point.x, point.y, static_cast<uint32_t>(paths.size()), 0};
            event.data = string_list_payload(header, paths, &nk_drop_data::strings_offset);
            nk::core::push_event(std::move(event));
        }
    } catch (...) {
    }
    DragFinish(drop);
}

#if defined(NK_HAS_WEBVIEW2)
void update_child_bounds(WinWindowResource &parent) noexcept;
#endif

void emit_notification(nk_event_kind kind, nk_request_id request, uint32_t flags = 0) noexcept {
    nk::core::callback_boundary([&] {
        nk::core::QueuedEvent event;
        event.kind = kind;
        event.request_id = request;
        event.flags = flags;
        nk::core::push_event(std::move(event));
    });
}

void remove_notification(nk_request_id request) noexcept {
    const auto found = notifications.find(request);
    if (found == notifications.end())
        return;
    NOTIFYICONDATAW icon{};
    icon.cbSize = sizeof(icon);
    icon.hWnd = notification_window;
    icon.uID = found->second.id;
    Shell_NotifyIconW(NIM_DELETE, &icon);
    notification_ids.erase(found->second.id);
    notifications.erase(found);
}

LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    if (window == notification_window && message == notification_message) {
        const auto found = notification_ids.find(static_cast<UINT>(wparam));
        if (found == notification_ids.end())
            return 0;
        const auto request = found->second;
        const UINT event = LOWORD(lparam);
        if (event == NIN_BALLOONUSERCLICK) {
            emit_notification(NK_EVENT_NOTIFICATION_ACTIVATED, request);
        } else if (event == NIN_BALLOONTIMEOUT || event == NIN_BALLOONHIDE) {
            remove_notification(request);
            emit_notification(NK_EVENT_NOTIFICATION_DISMISSED, request, event);
        }
        return 0;
    }
    auto *resource =
        reinterpret_cast<WinWindowResource *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lparam);
        resource = static_cast<WinWindowResource *>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(resource));
    }
    if (resource && resource->handle != NK_INVALID_HANDLE) {
        if (message == WM_GETMINMAXINFO) {
            auto *info = reinterpret_cast<MINMAXINFO *>(lparam);
            if (resource->min_width)
                info->ptMinTrackSize.x = resource->min_width;
            if (resource->min_height)
                info->ptMinTrackSize.y = resource->min_height;
            if (resource->max_width)
                info->ptMaxTrackSize.x = resource->max_width;
            if (resource->max_height)
                info->ptMaxTrackSize.y = resource->max_height;
            return 0;
        }
        if (message == WM_DROPFILES && resource->drops_enabled) {
            emit_drop_files(*resource, reinterpret_cast<HDROP>(wparam));
            return 0;
        }
        if (message == WM_CLOSE) {
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_WINDOW_CLOSE;
            event.source = resource->handle;
            nk::core::push_event(std::move(event));
            return 0;
        }
        if (message == WM_SIZE) {
            nk::core::callback_boundary([&] {
                const nk_window_resize_event size{static_cast<int32_t>(LOWORD(lparam)),
                                                  static_cast<int32_t>(HIWORD(lparam))};
                nk::core::QueuedEvent event;
                event.kind = NK_EVENT_WINDOW_RESIZE;
                event.source = resource->handle;
                event.data = bytes_of(size);
                nk::core::push_event(std::move(event));
            });
            nk::core::callback_boundary([&] {
                nk_window_state state{sizeof(state), 0, {0, 0}};
                if (IsWindowVisible(window))
                    state.flags |= NK_WINDOW_STATE_VISIBLE;
                if (GetForegroundWindow() == window)
                    state.flags |= NK_WINDOW_STATE_ACTIVE;
                if (IsIconic(window))
                    state.flags |= NK_WINDOW_STATE_MINIMIZED;
                if (IsZoomed(window))
                    state.flags |= NK_WINDOW_STATE_MAXIMIZED;
                if (resource->fullscreen)
                    state.flags |= NK_WINDOW_STATE_FULLSCREEN;
                nk::core::QueuedEvent event;
                event.kind = NK_EVENT_WINDOW_STATE_CHANGED;
                event.source = resource->handle;
                event.data = bytes_of(state);
                nk::core::push_event(std::move(event));
            });
        }
        if (message == WM_DPICHANGED) {
            nk::core::callback_boundary([&] {
                const nk_window_scale_event scale{static_cast<float>(HIWORD(wparam)) / 96.0f};
                nk::core::QueuedEvent event;
                event.kind = NK_EVENT_WINDOW_SCALE_CHANGED;
                event.source = resource->handle;
                event.data = bytes_of(scale);
                nk::core::push_event(std::move(event));
            });
            const auto *suggested = reinterpret_cast<const RECT *>(lparam);
            SetWindowPos(window, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
#if defined(NK_HAS_WEBVIEW2)
            update_child_bounds(*resource);
#endif
            return 0;
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool ensure_window_class() {
    if (window_class)
        return true;
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

bool ensure_notification_window() {
    if (notification_window)
        return true;
    if (!ensure_window_class())
        return false;
    notification_window =
        CreateWindowExW(0, window_class_name, L"NativeKit notifications", 0, 0, 0, 0, 0,
                        HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    return notification_window != nullptr;
}

template <std::size_t Size>
void copy_notification_text(wchar_t (&destination)[Size], const std::wstring &source) {
    const auto count = std::min(source.size(), Size - 1);
    std::wmemcpy(destination, source.data(), count);
    destination[count] = L'\0';
}

std::shared_ptr<WinWindowResource> get_window(nk_handle handle) {
    return std::dynamic_pointer_cast<WinWindowResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::window));
}

UINT query_window_dpi(HWND window) {
    using Function = UINT(WINAPI *)(HWND);
    const FARPROC address = GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    Function function = nullptr;
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function ? function(window) : 96;
}

#if defined(NK_HAS_WEBVIEW2)
std::shared_ptr<WinWebViewResource> get_webview(nk_handle handle) {
    return std::dynamic_pointer_cast<WinWebViewResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::webview));
}

bool load_webview2() {
    if (create_webview_environment && get_webview_version)
        return true;
    if (!webview2_loader)
        webview2_loader = LoadLibraryW(L"WebView2Loader.dll");
    if (!webview2_loader)
        return false;
    const FARPROC create =
        GetProcAddress(webview2_loader, "CreateCoreWebView2EnvironmentWithOptions");
    const FARPROC version =
        GetProcAddress(webview2_loader, "GetAvailableCoreWebView2BrowserVersionString");
    if (!create || !version)
        return false;
    static_assert(sizeof(create_webview_environment) == sizeof(create));
    static_assert(sizeof(get_webview_version) == sizeof(version));
    std::memcpy(&create_webview_environment, &create, sizeof(create));
    std::memcpy(&get_webview_version, &version, sizeof(version));
    return true;
}

bool webview2_available() {
    if (!load_webview2())
        return false;
    LPWSTR version = nullptr;
    const HRESULT result = get_webview_version(nullptr, &version);
    CoTaskMemFree(version);
    return SUCCEEDED(result);
}

uint32_t navigation_error(COREWEBVIEW2_WEB_ERROR_STATUS status) {
    if (status >= COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_COMMON_NAME_IS_INCORRECT &&
        status <= COREWEBVIEW2_WEB_ERROR_STATUS_CERTIFICATE_IS_INVALID)
        return NK_NAVIGATION_ERROR_SECURITY;
    if (status == COREWEBVIEW2_WEB_ERROR_STATUS_OPERATION_CANCELED)
        return NK_NAVIGATION_ERROR_CANCELLED;
    if (status == COREWEBVIEW2_WEB_ERROR_STATUS_VALID_AUTHENTICATION_CREDENTIALS_REQUIRED ||
        status == COREWEBVIEW2_WEB_ERROR_STATUS_VALID_PROXY_AUTHENTICATION_REQUIRED)
        return NK_NAVIGATION_ERROR_AUTH;
    if (status >= COREWEBVIEW2_WEB_ERROR_STATUS_SERVER_UNREACHABLE &&
        status <= COREWEBVIEW2_WEB_ERROR_STATUS_HOST_NAME_NOT_RESOLVED)
        return NK_NAVIGATION_ERROR_CONNECTION;
    if (status == COREWEBVIEW2_WEB_ERROR_STATUS_ERROR_HTTP_INVALID_SERVER_RESPONSE ||
        status == COREWEBVIEW2_WEB_ERROR_STATUS_REDIRECT_FAILED)
        return NK_NAVIGATION_ERROR_REQUEST;
    return NK_NAVIGATION_ERROR_OTHER;
}

std::wstring javascript_literal(const std::wstring &value) {
    constexpr wchar_t hex[] = L"0123456789abcdef";
    std::wstring result = L"\"";
    for (const wchar_t character : value) {
        if (character == L'\"' || character == L'\\') {
            result += L'\\';
            result += character;
        } else if (character == L'\n')
            result += L"\\n";
        else if (character == L'\r')
            result += L"\\r";
        else if (character == L'\t')
            result += L"\\t";
        else if (character < 0x20) {
            result += L"\\u";
            result += hex[(character >> 12) & 0xf];
            result += hex[(character >> 8) & 0xf];
            result += hex[(character >> 4) & 0xf];
            result += hex[character & 0xf];
        } else
            result += character;
    }
    result += L'\"';
    return result;
}

int hex_value(wchar_t value) {
    if (value >= L'0' && value <= L'9')
        return value - L'0';
    if (value >= L'a' && value <= L'f')
        return value - L'a' + 10;
    if (value >= L'A' && value <= L'F')
        return value - L'A' + 10;
    return -1;
}

std::wstring decode_json_string(const wchar_t *value) {
    if (!value)
        return {};
    const std::wstring input(value);
    if (input.size() < 2 || input.front() != L'\"' || input.back() != L'\"')
        return input;
    std::wstring result;
    for (std::size_t index = 1; index + 1 < input.size(); ++index) {
        wchar_t character = input[index];
        if (character != L'\\' || index + 1 >= input.size() - 1) {
            result += character;
            continue;
        }
        character = input[++index];
        if (character == L'b')
            result += L'\b';
        else if (character == L'f')
            result += L'\f';
        else if (character == L'n')
            result += L'\n';
        else if (character == L'r')
            result += L'\r';
        else if (character == L't')
            result += L'\t';
        else if (character == L'u' && index + 4 < input.size() - 1) {
            wchar_t decoded = 0;
            bool valid = true;
            for (int digit = 0; digit < 4; ++digit) {
                const int part = hex_value(input[index + 1 + digit]);
                if (part < 0) {
                    valid = false;
                    break;
                }
                decoded = static_cast<wchar_t>((decoded << 4) | part);
            }
            if (valid) {
                result += decoded;
                index += 4;
            } else
                result += character;
        } else
            result += character;
    }
    return result;
}

RECT physical_bounds(const WinWebViewResource &resource) {
    const auto parent = get_window(resource.parent);
    const int dpi = parent ? static_cast<int>(query_window_dpi(parent->window)) : 96;
    return {MulDiv(resource.bounds.left, dpi, 96), MulDiv(resource.bounds.top, dpi, 96),
            MulDiv(resource.bounds.right, dpi, 96), MulDiv(resource.bounds.bottom, dpi, 96)};
}

LONG coordinate_end(int32_t origin, int32_t extent) {
    const int64_t value = static_cast<int64_t>(origin) + extent;
    if (value > std::numeric_limits<LONG>::max())
        return std::numeric_limits<LONG>::max();
    if (value < std::numeric_limits<LONG>::min())
        return std::numeric_limits<LONG>::min();
    return static_cast<LONG>(value);
}

void apply_webview_bounds(const std::shared_ptr<WinWebViewResource> &resource) {
    if (resource->controller)
        resource->controller->put_Bounds(physical_bounds(*resource));
}

void update_child_bounds(WinWindowResource &parent) noexcept {
    for (const auto handle : parent.children) {
        const auto child = get_webview(handle);
        if (child)
            apply_webview_bounds(child);
    }
}

std::wstring html_document(std::wstring html, const std::wstring &base_url) {
    if (!base_url.empty())
        html = L"<head><base href=\"" + html_attribute(base_url) + L"\"></head>" + html;
    return html;
}

void emit_webview_text(nk_event_kind kind, nk_handle source, const wchar_t *value,
                       nk_result result = NK_OK, uint32_t flags = 0,
                       nk_request_id request = NK_INVALID_REQUEST_ID) noexcept {
    try {
        nk::core::QueuedEvent event;
        event.kind = kind;
        event.source = source;
        event.request_id = request;
        event.result = result;
        event.flags = flags;
        event.data = text_bytes(utf8(value));
        nk::core::push_event(std::move(event));
    } catch (...) {
    }
}

HRESULT execute_script(const std::shared_ptr<WinWebViewResource> &resource,
                       const std::wstring &script, nk_request_id request) {
    const auto wrapped = L"(()=>{const v=(0,eval)(" + javascript_literal(script) +
                         L");const j=JSON.stringify(v);if(j===undefined)throw new TypeError("
                         L"'JavaScript result is not JSON-serializable');return j;})()";
    auto completion =
        make_callback<ICoreWebView2ExecuteScriptCompletedHandler,
                      &IID_ICoreWebView2ExecuteScriptCompletedHandler, HRESULT, LPCWSTR>(
            [handle = resource->handle, request,
             generation = resource->generation](HRESULT error, LPCWSTR result) -> HRESULT {
                try {
                    if (!nk::core::is_runtime_generation(generation))
                        return S_OK;
                    const auto pending = evaluations.find(request);
                    if (pending == evaluations.end() || pending->second != handle)
                        return S_OK;
                    evaluations.erase(pending);
                    // ExecuteScript reports JavaScript exceptions as a successful
                    // COM call whose result is the unquoted JSON literal null.  A
                    // script that evaluates to JavaScript null is returned by our
                    // wrapper as the JSON string "null", so it remains distinct.
                    if (FAILED(error) || !result || std::wstring_view(result) == L"null") {
                        emit_webview_text(NK_EVENT_WEBVIEW_EVAL_COMPLETE, handle,
                                          L"JavaScript evaluation failed", NK_ERROR_UNKNOWN, 0,
                                          request);
                    } else {
                        const auto decoded = decode_json_string(result);
                        emit_webview_text(NK_EVENT_WEBVIEW_EVAL_COMPLETE, handle, decoded.c_str(),
                                          NK_OK, 0, request);
                    }
                } catch (...) {
                    emit_webview_text(NK_EVENT_WEBVIEW_EVAL_COMPLETE, handle,
                                      L"could not decode JavaScript result", NK_ERROR_OUT_OF_MEMORY,
                                      0, request);
                }
                return S_OK;
            });
    return resource->webview->ExecuteScript(wrapped.c_str(), completion.Get());
}

void flush_webview_commands(const std::shared_ptr<WinWebViewResource> &resource) {
    auto commands = std::move(resource->pending);
    resource->pending.clear();
    for (const auto &command : commands) {
        HRESULT result = E_FAIL;
        if (command.kind == WebViewCommandKind::navigate)
            result = resource->webview->Navigate(command.value.c_str());
        else if (command.kind == WebViewCommandKind::html)
            result = resource->webview->NavigateToString(
                html_document(command.value, command.auxiliary).c_str());
        else {
            try {
                result = execute_script(resource, command.value, command.request);
            } catch (...) {
                result = E_OUTOFMEMORY;
            }
        }
        if (FAILED(result) && command.kind == WebViewCommandKind::evaluate) {
            evaluations.erase(command.request);
            emit_webview_text(NK_EVENT_WEBVIEW_EVAL_COMPLETE, resource->handle,
                              L"could not evaluate JavaScript", NK_ERROR_UNKNOWN, 0,
                              command.request);
        }
    }
}

void fail_webview(const std::shared_ptr<WinWebViewResource> &resource,
                  const wchar_t *message) noexcept {
    resource->failed = true;
    for (const auto &command : resource->pending) {
        if (command.kind == WebViewCommandKind::evaluate) {
            evaluations.erase(command.request);
            emit_webview_text(NK_EVENT_WEBVIEW_EVAL_COMPLETE, resource->handle, message,
                              NK_ERROR_UNKNOWN, 0, command.request);
        }
    }
    resource->pending.clear();
    emit_webview_text(NK_EVENT_WEBVIEW_PROCESS_TERMINATED, resource->handle, message,
                      NK_ERROR_UNKNOWN);
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

void complete_webview_creation(const std::shared_ptr<WinWebViewResource> &resource) noexcept {
    if (resource->creation_pending.exchange(false))
        --pending_webview_creations;
}

void configure_webview(const std::shared_ptr<WinWebViewResource> &resource) {
    EventRegistrationToken token{};
    auto starting = make_callback<ICoreWebView2NavigationStartingEventHandler,
                                  &IID_ICoreWebView2NavigationStartingEventHandler, ICoreWebView2 *,
                                  ICoreWebView2NavigationStartingEventArgs *>(
        [resource](ICoreWebView2 *, ICoreWebView2NavigationStartingEventArgs *args) -> HRESULT {
            if (!nk::core::is_runtime_generation(resource->generation) ||
                !resource->navigation_policy || !get_webview(resource->handle))
                return S_OK;
            LPWSTR raw_url = nullptr;
            if (FAILED(args->get_Uri(&raw_url)))
                return S_OK;
            std::wstring url = raw_url ? raw_url : L"";
            CoTaskMemFree(raw_url);
            if (!resource->policy_bypass_url.empty() && resource->policy_bypass_url == url) {
                resource->policy_bypass_url.clear();
                return S_OK;
            }
            nk_request_id pending_request = NK_INVALID_REQUEST_ID;
            UINT64 pending_navigation = 0;
            bool tracked_navigation = false;
            try {
                pending_request = nk::core::next_request_id();
                nk::core::QueuedEvent event;
                event.kind = NK_EVENT_WEBVIEW_NAVIGATION_REQUEST;
                event.source = resource->handle;
                event.request_id = pending_request;
                event.data = text_bytes(utf8(url.c_str()));
                navigation_decisions.emplace(pending_request,
                                             WinNavigationDecision{resource->handle, url});
                tracked_navigation = SUCCEEDED(args->get_NavigationId(&pending_navigation));
                if (tracked_navigation)
                    resource->policy_cancelled_navigation_ids.insert(pending_navigation);
                if (FAILED(args->put_Cancel(TRUE))) {
                    navigation_decisions.erase(pending_request);
                    if (tracked_navigation)
                        resource->policy_cancelled_navigation_ids.erase(pending_navigation);
                    return S_OK;
                }
                if (nk::core::push_event(std::move(event)) != NK_OK) {
                    navigation_decisions.erase(pending_request);
                    if (tracked_navigation)
                        resource->policy_cancelled_navigation_ids.erase(pending_navigation);
                    args->put_Cancel(FALSE);
                }
            } catch (...) {
                if (pending_request)
                    navigation_decisions.erase(pending_request);
                if (tracked_navigation)
                    resource->policy_cancelled_navigation_ids.erase(pending_navigation);
                return S_OK;
            }
            return S_OK;
        });
    resource->webview->add_NavigationStarting(starting.Get(), &token);
    auto navigation = make_callback<ICoreWebView2NavigationCompletedEventHandler,
                                    &IID_ICoreWebView2NavigationCompletedEventHandler,
                                    ICoreWebView2 *, ICoreWebView2NavigationCompletedEventArgs *>(
        [resource](ICoreWebView2 *sender,
                   ICoreWebView2NavigationCompletedEventArgs *args) -> HRESULT {
            if (!nk::core::is_runtime_generation(resource->generation) ||
                !get_webview(resource->handle))
                return S_OK;
            UINT64 navigation_id = 0;
            if (SUCCEEDED(args->get_NavigationId(&navigation_id)) &&
                resource->policy_cancelled_navigation_ids.erase(navigation_id))
                return S_OK;
            BOOL successful = FALSE;
            args->get_IsSuccess(&successful);
            LPWSTR source = nullptr;
            sender->get_Source(&source);
            if (successful) {
                emit_webview_text(NK_EVENT_WEBVIEW_NAVIGATED, resource->handle, source);
            } else {
                COREWEBVIEW2_WEB_ERROR_STATUS status = COREWEBVIEW2_WEB_ERROR_STATUS_UNKNOWN;
                args->get_WebErrorStatus(&status);
                emit_webview_text(NK_EVENT_WEBVIEW_NAVIGATION_FAILED, resource->handle,
                                  L"WebView2 navigation failed", NK_ERROR_UNKNOWN,
                                  navigation_error(status));
            }
            CoTaskMemFree(source);
            return S_OK;
        });
    resource->webview->add_NavigationCompleted(navigation.Get(), &token);
    auto title =
        make_callback<ICoreWebView2DocumentTitleChangedEventHandler,
                      &IID_ICoreWebView2DocumentTitleChangedEventHandler, ICoreWebView2 *,
                      IUnknown *>([resource](ICoreWebView2 *sender, IUnknown *) -> HRESULT {
            if (!nk::core::is_runtime_generation(resource->generation) ||
                !get_webview(resource->handle))
                return S_OK;
            LPWSTR value = nullptr;
            sender->get_DocumentTitle(&value);
            emit_webview_text(NK_EVENT_WEBVIEW_TITLE_CHANGED, resource->handle, value);
            CoTaskMemFree(value);
            return S_OK;
        });
    resource->webview->add_DocumentTitleChanged(title.Get(), &token);
    auto message = make_callback<ICoreWebView2WebMessageReceivedEventHandler,
                                 &IID_ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2 *,
                                 ICoreWebView2WebMessageReceivedEventArgs *>(
        [resource](ICoreWebView2 *, ICoreWebView2WebMessageReceivedEventArgs *args) -> HRESULT {
            if (!nk::core::is_runtime_generation(resource->generation) ||
                !get_webview(resource->handle))
                return S_OK;
            LPWSTR value = nullptr;
            args->get_WebMessageAsJson(&value);
            emit_webview_text(NK_EVENT_WEBVIEW_MESSAGE, resource->handle, value);
            CoTaskMemFree(value);
            return S_OK;
        });
    resource->webview->add_WebMessageReceived(message.Get(), &token);
    auto process = make_callback<ICoreWebView2ProcessFailedEventHandler,
                                 &IID_ICoreWebView2ProcessFailedEventHandler, ICoreWebView2 *,
                                 ICoreWebView2ProcessFailedEventArgs *>(
        [resource](ICoreWebView2 *, ICoreWebView2ProcessFailedEventArgs *args) -> HRESULT {
            if (!nk::core::is_runtime_generation(resource->generation) ||
                !get_webview(resource->handle))
                return S_OK;
            COREWEBVIEW2_PROCESS_FAILED_KIND kind =
                COREWEBVIEW2_PROCESS_FAILED_KIND_BROWSER_PROCESS_EXITED;
            args->get_ProcessFailedKind(&kind);
            emit_webview_text(NK_EVENT_WEBVIEW_PROCESS_TERMINATED, resource->handle, nullptr,
                              NK_ERROR_UNKNOWN, static_cast<uint32_t>(kind));
            return S_OK;
        });
    resource->webview->add_ProcessFailed(process.Get(), &token);

    ComPtr<ICoreWebView2Settings> settings;
    if (SUCCEEDED(resource->webview->get_Settings(&settings)))
        settings->put_AreDevToolsEnabled(resource->devtools ? TRUE : FALSE);
    resource->webview->AddScriptToExecuteOnDocumentCreated(
        LR"JS((()=>{if(!window.webkit)window.webkit={};if(!window.webkit.messageHandlers)window.webkit.messageHandlers={};window.webkit.messageHandlers.nativekit={postMessage:v=>window.chrome.webview.postMessage(v)};})();)JS",
        nullptr);
    apply_webview_bounds(resource);
    resource->controller->put_IsVisible(resource->visible ? TRUE : FALSE);
    resource->ready = true;
    emit_webview_text(NK_EVENT_WEBVIEW_READY, resource->handle, nullptr);
    if (!resource->initial_url.empty())
        resource->webview->Navigate(resource->initial_url.c_str());
    flush_webview_commands(resource);
}

void begin_webview_creation(const std::shared_ptr<WinWebViewResource> &resource) {
    ++pending_webview_creations;
    resource->creation_pending = true;
    auto environment_handler =
        make_callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler,
                      &IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT,
                      ICoreWebView2Environment *>([resource](HRESULT error,
                                                             ICoreWebView2Environment *environment)
                                                      -> HRESULT {
            try {
                if (!nk::core::is_runtime_generation(resource->generation)) {
                    complete_webview_creation(resource);
                    return S_OK;
                }
                if (FAILED(error) || !environment || !get_webview(resource->handle)) {
                    fail_webview(resource, L"WebView2 environment creation failed");
                    complete_webview_creation(resource);
                    return S_OK;
                }
                resource->environment = environment;
                auto controller_handler =
                    make_callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                                  &IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                                  HRESULT, ICoreWebView2Controller *>(
                        [resource](HRESULT controller_error,
                                   ICoreWebView2Controller *controller) -> HRESULT {
                            struct Completion {
                                std::shared_ptr<WinWebViewResource> resource;
                                ~Completion() { complete_webview_creation(resource); }
                            } completion{resource};
                            if (!nk::core::is_runtime_generation(resource->generation))
                                return S_OK;
                            if (FAILED(controller_error) || !controller ||
                                !get_webview(resource->handle)) {
                                fail_webview(resource, L"WebView2 controller creation failed");
                                return S_OK;
                            }
                            resource->controller = controller;
                            if (FAILED(controller->get_CoreWebView2(&resource->webview))) {
                                fail_webview(resource, L"WebView2 instance creation failed");
                                return S_OK;
                            }
                            try {
                                configure_webview(resource);
                            } catch (...) {
                                fail_webview(resource, L"WebView2 event setup failed");
                            }
                            return S_OK;
                        });
                const auto parent = get_window(resource->parent);
                if (!parent || FAILED(environment->CreateCoreWebView2Controller(
                                   parent->window, controller_handler.Get()))) {
                    fail_webview(resource, L"WebView2 controller request failed");
                    complete_webview_creation(resource);
                }
                return S_OK;
            } catch (...) {
                fail_webview(resource, L"WebView2 controller setup failed");
                complete_webview_creation(resource);
                return E_OUTOFMEMORY;
            }
        });
    const HRESULT result =
        create_webview_environment(nullptr, nullptr, nullptr, environment_handler.Get());
    if (FAILED(result)) {
        complete_webview_creation(resource);
        fail_webview(resource, L"WebView2 environment request failed");
    }
}
#endif

std::vector<std::byte> dialog_paths_payload(const std::vector<std::string> &paths, bool accepted) {
    const std::size_t offsets_offset = sizeof(nk_dialog_paths);
    const std::size_t strings_offset = offsets_offset + paths.size() * sizeof(uint32_t);
    std::size_t total = strings_offset;
    for (const auto &path : paths)
        total += path.size() + 1;
    std::vector<std::byte> result(total);
    const nk_dialog_paths header{accepted ? 1u : 0u, static_cast<uint32_t>(paths.size()),
                                 static_cast<uint32_t>(offsets_offset),
                                 static_cast<uint32_t>(strings_offset)};
    std::memcpy(result.data(), &header, sizeof(header));
    std::size_t cursor = strings_offset;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto offset = static_cast<uint32_t>(cursor);
        std::memcpy(result.data() + offsets_offset + index * sizeof(offset), &offset,
                    sizeof(offset));
        std::memcpy(result.data() + cursor, paths[index].c_str(), paths[index].size() + 1);
        cursor += paths[index].size() + 1;
    }
    return result;
}

template <typename T> void release(T *&value) {
    if (value)
        value->Release();
    value = nullptr;
}

void emit_file_completion(const WinDialogContext &context, std::vector<std::string> paths,
                          bool accepted, nk_result result = NK_OK) {
    if (!nk::core::is_runtime_generation(context.generation))
        return;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_DIALOG_COMPLETE;
    event.request_id = context.request;
    event.flags = context.kind;
    event.result = result;
    event.data_count = static_cast<uint32_t>(paths.size());
    event.data = dialog_paths_payload(paths, accepted);
    nk::core::push_event(std::move(event));
}

void run_file_dialog(const std::shared_ptr<WinDialogContext> &context) noexcept {
    context->thread_id = GetCurrentThreadId();
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileDialog *dialog = nullptr;
    HRESULT status = E_FAIL;
    if (SUCCEEDED(initialized)) {
        if (context->kind == NK_DIALOG_SAVE_FILE)
            status = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IFileSaveDialog, reinterpret_cast<void **>(&dialog));
        else
            status = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IFileOpenDialog, reinterpret_cast<void **>(&dialog));
    }
    std::vector<std::string> paths;
    bool accepted = false;
    if (SUCCEEDED(status) && dialog) {
        FILEOPENDIALOGOPTIONS options = 0;
        dialog->GetOptions(&options);
        options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
        if (context->kind == NK_DIALOG_SELECT_DIRECTORY)
            options |= FOS_PICKFOLDERS;
        if (context->kind == NK_DIALOG_OPEN_FILE && (context->flags & NK_DIALOG_ALLOW_MULTIPLE))
            options |= FOS_ALLOWMULTISELECT;
        if (context->flags & NK_DIALOG_SHOW_HIDDEN)
            options |= FOS_FORCESHOWHIDDEN;
        if (context->kind == NK_DIALOG_SAVE_FILE && !(context->flags & NK_DIALOG_CONFIRM_OVERWRITE))
            options &= ~FOS_OVERWRITEPROMPT;
        dialog->SetOptions(options);
        if (!context->title.empty())
            dialog->SetTitle(context->title.c_str());
        if (!context->suggested_name.empty())
            dialog->SetFileName(context->suggested_name.c_str());
        if (!context->initial_path.empty()) {
            IShellItem *initial = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(context->initial_path.c_str(), nullptr,
                                                      IID_IShellItem,
                                                      reinterpret_cast<void **>(&initial)))) {
                dialog->SetFolder(initial);
                release(initial);
            }
        }
        std::vector<COMDLG_FILTERSPEC> specifications;
        specifications.reserve(context->filters.size());
        for (const auto &filter : context->filters)
            specifications.push_back({filter.first.c_str(), filter.second.c_str()});
        if (!specifications.empty())
            dialog->SetFileTypes(static_cast<UINT>(specifications.size()), specifications.data());
        if (context->canceled)
            status = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        else
            status = dialog->Show(context->parent);
        if (SUCCEEDED(status)) {
            accepted = true;
            if (context->kind == NK_DIALOG_OPEN_FILE &&
                (context->flags & NK_DIALOG_ALLOW_MULTIPLE)) {
                IFileOpenDialog *open_dialog = nullptr;
                IShellItemArray *items = nullptr;
                if (SUCCEEDED(dialog->QueryInterface(IID_IFileOpenDialog,
                                                     reinterpret_cast<void **>(&open_dialog))) &&
                    SUCCEEDED(open_dialog->GetResults(&items))) {
                    DWORD count = 0;
                    items->GetCount(&count);
                    for (DWORD index = 0; index < count; ++index) {
                        IShellItem *item = nullptr;
                        PWSTR path = nullptr;
                        if (SUCCEEDED(items->GetItemAt(index, &item)) &&
                            SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                            paths.push_back(utf8(path));
                        CoTaskMemFree(path);
                        release(item);
                    }
                }
                release(items);
                release(open_dialog);
            } else {
                IShellItem *item = nullptr;
                PWSTR path = nullptr;
                if (SUCCEEDED(dialog->GetResult(&item)) &&
                    SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))
                    paths.push_back(utf8(path));
                CoTaskMemFree(path);
                release(item);
            }
        }
    }
    const bool canceled = status == HRESULT_FROM_WIN32(ERROR_CANCELLED) || context->canceled;
    context->complete = true;
    if (!nk::core::is_runtime_generation(context->generation))
        return;
    try {
        emit_file_completion(*context, std::move(paths), accepted && !canceled,
                             (SUCCEEDED(status) || canceled) ? NK_OK : NK_ERROR_UNKNOWN);
    } catch (...) {
    }
    release(dialog);
    if (SUCCEEDED(initialized))
        CoUninitialize();
}

UINT message_box_type(const WinDialogContext &context) {
    UINT type = MB_TASKMODAL;
    if (context.message_kind == NK_MESSAGE_WARNING)
        type |= MB_ICONWARNING;
    else if (context.message_kind == NK_MESSAGE_ERROR)
        type |= MB_ICONERROR;
    else if (context.message_kind == NK_MESSAGE_QUESTION)
        type |= MB_ICONQUESTION;
    else
        type |= MB_ICONINFORMATION;
    const bool yes_no = (context.buttons & (NK_MESSAGE_BUTTON_YES | NK_MESSAGE_BUTTON_NO)) != 0;
    if (yes_no)
        type |= (context.buttons & NK_MESSAGE_BUTTON_CANCEL) ? MB_YESNOCANCEL : MB_YESNO;
    else
        type |= (context.buttons & NK_MESSAGE_BUTTON_CANCEL) ? MB_OKCANCEL : MB_OK;
    return type;
}

void run_message_dialog(const std::shared_ptr<WinDialogContext> &context) noexcept {
    context->thread_id = GetCurrentThreadId();
    int response = IDCANCEL;
    if (!context->canceled)
        response = MessageBoxW(context->parent, context->message.c_str(), context->title.c_str(),
                               message_box_type(*context));
    uint32_t button = NK_MESSAGE_RESULT_NONE;
    if (context->canceled || response == IDCANCEL)
        button = NK_MESSAGE_RESULT_CANCEL;
    else if (response == IDOK)
        button = NK_MESSAGE_RESULT_OK;
    else if (response == IDYES)
        button = NK_MESSAGE_RESULT_YES;
    else if (response == IDNO)
        button = NK_MESSAGE_RESULT_NO;
    context->complete = true;
    try {
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_DIALOG_COMPLETE;
        event.request_id = context->request;
        event.flags = context->kind;
        event.result = response ? NK_OK : NK_ERROR_UNKNOWN;
        event.data = bytes_of(nk_dialog_message_result{button});
        nk::core::push_event(std::move(event));
    } catch (...) {
    }
}

BOOL CALLBACK close_thread_window(HWND window, LPARAM) {
    PostMessageW(window, WM_CLOSE, 0, 0);
    return TRUE;
}

void request_dialog_close(const std::shared_ptr<WinDialogContext> &context) {
    context->canceled = true;
    const DWORD thread = context->thread_id;
    if (thread)
        EnumThreadWindows(thread, close_thread_window, 0);
}

void cancel_dialogs_for_parent(HWND parent) {
    std::lock_guard lock(dialogs_mutex);
    for (const auto &[request, context] : dialogs) {
        (void)request;
        if (context->parent == parent && !context->complete)
            request_dialog_close(context);
    }
}

void reap_dialogs() noexcept {
    std::vector<std::shared_ptr<WinDialogContext>> finished;
    {
        std::lock_guard lock(dialogs_mutex);
        for (auto iterator = dialogs.begin(); iterator != dialogs.end();) {
            if (iterator->second->canceled && !iterator->second->complete)
                request_dialog_close(iterator->second);
            if (iterator->second->complete) {
                finished.push_back(iterator->second);
                iterator = dialogs.erase(iterator);
            } else
                ++iterator;
        }
    }
    for (auto &context : finished)
        if (context->worker.joinable())
            context->worker.join();
}

nk_result start_file_dialog(nk_handle parent_handle, const nk_file_dialog_options *options,
                            nk_request_id *out_request, uint32_t kind) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!options || options->struct_size < sizeof(*options) || !out_request ||
        (options->filter_count && !options->filters))
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid file dialog options");
    auto context = std::make_shared<WinDialogContext>();
    if (parent_handle != NK_INVALID_HANDLE) {
        auto parent = get_window(parent_handle);
        if (!parent)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
        context->parent = parent->window;
    }
    context->request = nk::core::next_request_id();
    context->generation = nk::core::runtime_generation();
    context->kind = kind;
    context->flags = options->flags;
    if (!copy_wide(options->title, context->title) ||
        !copy_wide(options->initial_path, context->initial_path) ||
        !copy_wide(options->suggested_name, context->suggested_name))
        return fail(NK_ERROR_INVALID_ARGUMENT, "file dialog option is not valid UTF-8");
    for (uint32_t index = 0; index < options->filter_count; ++index) {
        const auto &filter = options->filters[index];
        if (!filter.patterns)
            continue;
        std::wstring name;
        std::wstring patterns;
        if (!copy_wide(filter.name ? filter.name : filter.patterns, name) ||
            !copy_wide(filter.patterns, patterns))
            return fail(NK_ERROR_INVALID_ARGUMENT, "file dialog filter is not valid UTF-8");
        context->filters.emplace_back(std::move(name), std::move(patterns));
    }
    try {
        {
            std::lock_guard lock(dialogs_mutex);
            dialogs.emplace(context->request, context);
        }
        try {
            context->worker = std::thread(run_file_dialog, context);
        } catch (...) {
            std::lock_guard lock(dialogs_mutex);
            dialogs.erase(context->request);
            throw;
        }
    } catch (...) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "could not start file dialog");
    }
    *out_request = context->request;
    return NK_OK;
}

nk_result unsupported() {
    const auto thread = enter_ui();
    if (thread != NK_OK)
        return thread;
    return fail(NK_ERROR_UNSUPPORTED, "this Windows service is not implemented yet");
}

nk_result copy_utf8_output(const std::string &value, char *buffer, uint32_t *inout_size) {
    if (!inout_size)
        return fail(NK_ERROR_INVALID_ARGUMENT, "output size must not be null");
    if (value.size() >= std::numeric_limits<uint32_t>::max())
        return fail(NK_ERROR_UNKNOWN, "system string is too large");
    const auto required = static_cast<uint32_t>(value.size() + 1);
    const auto capacity = *inout_size;
    *inout_size = required;
    if (!buffer || capacity < required)
        return fail(NK_ERROR_BUFFER_TOO_SMALL, "output buffer is too small");
    std::memcpy(buffer, value.c_str(), required);
    return NK_OK;
}

bool valid_uri_scheme(const char *value) {
    if (!value || !((*value >= 'A' && *value <= 'Z') || (*value >= 'a' && *value <= 'z')))
        return false;
    for (++value; *value && *value != ':'; ++value) {
        const bool valid = (*value >= 'A' && *value <= 'Z') || (*value >= 'a' && *value <= 'z') ||
                           (*value >= '0' && *value <= '9') || *value == '+' || *value == '-' ||
                           *value == '.';
        if (!valid)
            return false;
    }
    return *value == ':';
}

nk_result shell_open(const char *value, bool require_scheme) {
    if (!value || !*value || (require_scheme && !valid_uri_scheme(value)))
        return fail(NK_ERROR_INVALID_ARGUMENT, require_scheme
                                                   ? "URL must contain a valid URI scheme"
                                                   : "path must not be empty");
    const auto native = wide(value);
    if (native.empty())
        return fail(NK_ERROR_INVALID_ARGUMENT, "value is not valid UTF-8");
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(nullptr, L"open", native.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    return result > 32 ? NK_OK : fail(NK_ERROR_UNKNOWN, "Windows could not open the value");
}

const KNOWNFOLDERID *directory_id(nk_system_directory_kind kind) {
    switch (kind) {
    case NK_DIRECTORY_HOME:
        return &FOLDERID_Profile;
    case NK_DIRECTORY_DESKTOP:
        return &FOLDERID_Desktop;
    case NK_DIRECTORY_DOCUMENTS:
        return &FOLDERID_Documents;
    case NK_DIRECTORY_DOWNLOADS:
        return &FOLDERID_Downloads;
    case NK_DIRECTORY_CACHE:
        return &FOLDERID_LocalAppData;
    case NK_DIRECTORY_CONFIG:
        return &FOLDERID_RoamingAppData;
    case NK_DIRECTORY_DATA:
        return &FOLDERID_LocalAppData;
    default:
        return nullptr;
    }
}

nk_result get_system_directory(nk_system_directory_kind kind, std::string &output) {
    if (kind == NK_DIRECTORY_TEMP) {
        const DWORD size = GetTempPathW(0, nullptr);
        if (!size)
            return fail(NK_ERROR_UNSUPPORTED, "temporary directory is unavailable");
        std::wstring path(size, L'\0');
        const DWORD written = GetTempPathW(size, path.data());
        if (!written || written >= size)
            return fail(NK_ERROR_UNKNOWN, "could not read temporary directory");
        path.resize(written);
        while (path.size() > 1 && (path.back() == L'\\' || path.back() == L'/'))
            path.pop_back();
        output = utf8(path.c_str());
        return output.empty() ? fail(NK_ERROR_UNKNOWN, "could not encode temporary directory")
                              : NK_OK;
    }
    const auto *id = directory_id(kind);
    if (!id)
        return fail(NK_ERROR_UNSUPPORTED, "unknown system directory");
    PWSTR path = nullptr;
    const HRESULT result = SHGetKnownFolderPath(*id, KF_FLAG_DEFAULT, nullptr, &path);
    if (FAILED(result) || !path) {
        CoTaskMemFree(path);
        return fail(NK_ERROR_UNSUPPORTED, "system directory is unavailable");
    }
    output = utf8(path);
    CoTaskMemFree(path);
    return output.empty() ? fail(NK_ERROR_UNKNOWN, "could not encode system directory") : NK_OK;
}

} // namespace

namespace nk::backend {
void pump_events() noexcept {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    reap_dialogs();
}

void shutdown() noexcept {
    while (!notifications.empty())
        remove_notification(notifications.begin()->first);
    if (notification_window) {
        DestroyWindow(notification_window);
        notification_window = nullptr;
    }
    std::vector<std::shared_ptr<WinDialogContext>> pending;
    {
        std::lock_guard lock(dialogs_mutex);
        for (const auto &[request, context] : dialogs) {
            (void)request;
            pending.push_back(context);
            request_dialog_close(context);
        }
        dialogs.clear();
    }
    for (auto &context : pending) {
        while (!context->complete) {
            request_dialog_close(context);
            Sleep(1);
        }
        if (context->worker.joinable())
            context->worker.join();
    }
#if defined(NK_HAS_WEBVIEW2)
    navigation_decisions.clear();
    cancel_evaluations(NK_INVALID_HANDLE);
#endif
    nk::core::handles().clear();
    pump_events();
#if defined(NK_HAS_WEBVIEW2)
    const ULONGLONG deadline = GetTickCount64() + 10000;
    while (pending_webview_creations && GetTickCount64() < deadline) {
        pump_events();
        Sleep(1);
    }
    if (!pending_webview_creations) {
        if (webview_com_initialized) {
            CoUninitialize();
            webview_com_initialized = false;
        }
        create_webview_environment = nullptr;
        get_webview_version = nullptr;
        if (webview2_loader)
            FreeLibrary(webview2_loader);
        webview2_loader = nullptr;
    }
#endif
}
} // namespace nk::backend

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    nk_capabilities capabilities = NK_CAP_WINDOW | NK_CAP_FILE_DIALOG | NK_CAP_CLIPBOARD |
                                   NK_CAP_DRAG_DROP | NK_CAP_SHELL | NK_CAP_SYSTEM_APPEARANCE |
                                   NK_CAP_EXPORT_NATIVE_WINDOW | NK_CAP_NOTIFICATION;
#if defined(NK_HAS_WEBVIEW2)
    if (webview2_available())
        capabilities |= NK_CAP_WEBVIEW;
#endif
    return capabilities;
}

nk_result NK_CALL nk_window_create(const nk_window_options *options, nk_handle *out_window) {
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!options || options->struct_size < sizeof(*options) || !out_window ||
            options->width <= 0 || options->height <= 0 || options->kind > NK_WINDOW_UTILITY ||
            ((options->flags & NK_WINDOW_MODAL) && !options->owner))
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid window options");
        *out_window = NK_INVALID_HANDLE;
        if (!ensure_window_class())
            return fail(NK_ERROR_UNKNOWN, "could not register Win32 window class");
        auto owner = options->owner ? get_window(options->owner) : nullptr;
        if (options->owner && !owner)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale owner window handle");
        if (owner)
            owner->owned_windows.reserve(owner->owned_windows.size() + 1);
        auto resource = std::make_shared<WinWindowResource>();
        resource->owner = options->owner;
        resource->modal = (options->flags & NK_WINDOW_MODAL) != 0;
        const auto title = wide(options->title);
        DWORD style = (options->flags & NK_WINDOW_BORDERLESS) ? WS_POPUP : WS_OVERLAPPEDWINDOW;
        DWORD extended_style = options->kind == NK_WINDOW_UTILITY ? WS_EX_TOOLWINDOW : 0;
        if (!(options->flags & NK_WINDOW_RESIZABLE))
            style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
        RECT bounds{0, 0, options->width, options->height};
        AdjustWindowRectEx(&bounds, style, FALSE, extended_style);
        resource->window = CreateWindowExW(
            extended_style, window_class_name, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
            bounds.right - bounds.left, bounds.bottom - bounds.top, owner ? owner->window : nullptr,
            nullptr, GetModuleHandleW(nullptr), resource.get());
        if (!resource->window)
            return fail(NK_ERROR_UNKNOWN, "could not create Win32 window");
        resource->handle = nk::core::handles().insert(nk::core::ResourceType::window, resource);
        if (resource->handle == NK_INVALID_HANDLE)
            return fail(NK_ERROR_OUT_OF_MEMORY, "window handle registry is full");
        if (owner)
            owner->owned_windows.push_back(resource->handle);
        if (!(options->flags & NK_WINDOW_HIDDEN)) {
            if (resource->modal && owner) {
                if (owner->active_modal_children++ == 0)
                    EnableWindow(owner->window, FALSE);
                resource->modal_active = true;
            }
            ShowWindow(resource->window, SW_SHOW);
        }
        *out_window = resource->handle;
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while creating window");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while creating window");
    }
}

nk_result NK_CALL nk_window_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const auto owned_windows = resource->owned_windows;
    for (const auto owned : owned_windows)
        nk_window_destroy(owned);
    const auto children = resource->children;
    for (const auto child : children)
        nk_webview_destroy(child);
    cancel_dialogs_for_parent(resource->window);
    SetWindowLongPtrW(resource->window, GWLP_USERDATA, 0);
    DestroyWindow(resource->window);
    resource->window = nullptr;
    if (auto owner = get_window(resource->owner)) {
        if (resource->modal_active && owner->active_modal_children &&
            --owner->active_modal_children == 0)
            EnableWindow(owner->window, TRUE);
        auto &owned = owner->owned_windows;
        owned.erase(std::remove(owned.begin(), owned.end(), handle), owned.end());
    }
    nk::core::handles().erase(handle, nk::core::ResourceType::window);
    return NK_OK;
}

nk_result NK_CALL nk_window_show(nk_handle handle, uint32_t visible) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    if (auto owner = get_window(resource->owner); resource->modal && owner) {
        if (visible && !resource->modal_active) {
            if (owner->active_modal_children++ == 0)
                EnableWindow(owner->window, FALSE);
            resource->modal_active = true;
        } else if (!visible && resource->modal_active) {
            if (owner->active_modal_children && --owner->active_modal_children == 0)
                EnableWindow(owner->window, TRUE);
            resource->modal_active = false;
        }
    }
    ShowWindow(resource->window, visible ? SW_SHOW : SW_HIDE);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_title(nk_handle handle, const char *title) {
    return nk::core::result_boundary("unexpected error while setting window title", [&] {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        auto resource = get_window(handle);
        if (!resource)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
        const auto value = wide(title);
        if (title && *title && value.empty())
            return fail(NK_ERROR_INVALID_ARGUMENT, "title is not valid UTF-8");
        return SetWindowTextW(resource->window, value.c_str())
                   ? NK_OK
                   : fail(NK_ERROR_UNKNOWN, "could not set window title");
    });
}

nk_result NK_CALL nk_window_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                       int32_t height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (width <= 0 || height <= 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "window dimensions must be positive");
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    return SetWindowPos(resource->window, nullptr, x, y, width, height,
                        SWP_NOACTIVATE | SWP_NOZORDER)
               ? NK_OK
               : fail(NK_ERROR_UNKNOWN, "could not set window bounds");
}

nk_result NK_CALL nk_window_get_scale(nk_handle handle, float *out_scale) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_scale)
        return fail(NK_ERROR_INVALID_ARGUMENT, "scale output must not be null");
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    *out_scale = static_cast<float>(query_window_dpi(resource->window)) / 96.0f;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_state(nk_handle h, nk_window_state *out) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!out || out->struct_size < sizeof(*out))
        return fail(NK_ERROR_INVALID_ARGUMENT, "window state output is missing or too small");
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    auto size = out->struct_size;
    *out = {};
    out->struct_size = size;
    if (IsWindowVisible(w->window))
        out->flags |= NK_WINDOW_STATE_VISIBLE;
    if (GetForegroundWindow() == w->window)
        out->flags |= NK_WINDOW_STATE_ACTIVE;
    if (IsIconic(w->window))
        out->flags |= NK_WINDOW_STATE_MINIMIZED;
    if (IsZoomed(w->window))
        out->flags |= NK_WINDOW_STATE_MAXIMIZED;
    if (w->fullscreen)
        out->flags |= NK_WINDOW_STATE_FULLSCREEN;
    return NK_OK;
}
nk_result NK_CALL nk_window_minimize(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    ShowWindow(w->window, SW_MINIMIZE);
    return NK_OK;
}
nk_result NK_CALL nk_window_maximize(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    ShowWindow(w->window, SW_MAXIMIZE);
    return NK_OK;
}
nk_result NK_CALL nk_window_restore(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    if (w->fullscreen)
        nk_window_set_fullscreen(h, 0);
    ShowWindow(w->window, SW_RESTORE);
    return NK_OK;
}
nk_result NK_CALL nk_window_activate(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    ShowWindow(w->window, SW_SHOW);
    return SetForegroundWindow(w->window)
               ? NK_OK
               : fail(NK_ERROR_UNKNOWN, "Windows denied window activation");
}
nk_result NK_CALL nk_window_set_fullscreen(nk_handle h, uint32_t enabled) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    if (!!enabled == w->fullscreen)
        return NK_OK;
    if (enabled) {
        w->placement.length = sizeof(w->placement);
        GetWindowPlacement(w->window, &w->placement);
        w->windowed_style = GetWindowLongPtrW(w->window, GWL_STYLE);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        GetMonitorInfoW(MonitorFromWindow(w->window, MONITOR_DEFAULTTONEAREST), &monitor);
        SetWindowLongPtrW(w->window, GWL_STYLE, w->windowed_style & ~WS_OVERLAPPEDWINDOW);
        SetWindowPos(w->window, HWND_TOP, monitor.rcMonitor.left, monitor.rcMonitor.top,
                     monitor.rcMonitor.right - monitor.rcMonitor.left,
                     monitor.rcMonitor.bottom - monitor.rcMonitor.top,
                     SWP_FRAMECHANGED | SWP_NOOWNERZORDER);
        w->fullscreen = true;
    } else {
        SetWindowLongPtrW(w->window, GWL_STYLE, w->windowed_style);
        SetWindowPlacement(w->window, &w->placement);
        SetWindowPos(w->window, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER);
        w->fullscreen = false;
    }
    return NK_OK;
}
nk_result NK_CALL nk_window_request_attention(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    FLASHWINFO info{sizeof(info), w->window, FLASHW_TRAY | FLASHW_TIMERNOFG, 3, 0};
    return FlashWindowEx(&info) ? NK_OK
                                : fail(NK_ERROR_UNKNOWN, "could not request window attention");
}
nk_result NK_CALL nk_window_set_size_limits(nk_handle h, const nk_window_size_limits *l) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!l || l->struct_size < sizeof(*l) || l->min_width < 0 || l->min_height < 0 ||
        l->max_width < 0 || l->max_height < 0 || (l->max_width && l->max_width < l->min_width) ||
        (l->max_height && l->max_height < l->min_height))
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid window size limits");
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    w->min_width = l->min_width;
    w->min_height = l->min_height;
    w->max_width = l->max_width;
    w->max_height = l->max_height;
    SetWindowPos(w->window, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    return NK_OK;
}

nk_result NK_CALL nk_window_get_native(nk_handle handle, nk_native_window *out_native) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_native || out_native->struct_size < sizeof(*out_native))
        return fail(NK_ERROR_INVALID_ARGUMENT, "native window output is missing or too small");
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const auto size = out_native->struct_size;
    *out_native = {};
    out_native->struct_size = size;
    out_native->kind = NK_NATIVE_WINDOW_WIN32;
    out_native->window = reinterpret_cast<uintptr_t>(resource->window);
    return NK_OK;
}

nk_result NK_CALL nk_window_wrap_native(const nk_native_window *, nk_handle *) {
    return unsupported();
}

#if defined(NK_HAS_WEBVIEW2)
nk_result NK_CALL nk_webview_create(nk_handle parent_handle, const nk_webview_options *options,
                                    nk_handle *out_webview) {
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!options || options->struct_size < sizeof(*options) || !out_webview ||
            options->width <= 0 || options->height <= 0)
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid WebView options");
        *out_webview = NK_INVALID_HANDLE;
        auto parent = get_window(parent_handle);
        if (!parent)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
        if (!webview2_available())
            return fail(NK_ERROR_UNSUPPORTED, "WebView2 Runtime is not installed");
        if (!webview_com_initialized) {
            const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            if (FAILED(initialized))
                return fail(NK_ERROR_UNSUPPORTED, "WebView2 requires a COM STA UI thread");
            webview_com_initialized = true;
        }
        auto resource = std::make_shared<WinWebViewResource>();
        resource->parent = parent_handle;
        resource->generation = nk::core::runtime_generation();
        resource->bounds = {options->x, options->y, coordinate_end(options->x, options->width),
                            coordinate_end(options->y, options->height)};
        resource->visible = (options->flags & NK_WEBVIEW_HIDDEN) == 0;
        resource->devtools = (options->flags & NK_WEBVIEW_DEVTOOLS) != 0;
        resource->navigation_policy = (options->flags & NK_WEBVIEW_NAVIGATION_POLICY) != 0;
        if (!copy_wide(options->initial_url, resource->initial_url))
            return fail(NK_ERROR_INVALID_ARGUMENT, "initial URL is not valid UTF-8");
        resource->handle = nk::core::handles().insert(nk::core::ResourceType::webview, resource);
        if (resource->handle == NK_INVALID_HANDLE)
            return fail(NK_ERROR_OUT_OF_MEMORY, "WebView handle registry is full");
        parent->children.push_back(resource->handle);
        try {
            begin_webview_creation(resource);
        } catch (...) {
            parent->children.pop_back();
            nk::core::handles().erase(resource->handle, nk::core::ResourceType::webview);
            throw;
        }
        *out_webview = resource->handle;
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while creating WebView");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while creating WebView");
    }
}

nk_result NK_CALL nk_webview_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = get_webview(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    cancel_evaluations(handle);
    for (auto item = navigation_decisions.begin(); item != navigation_decisions.end();) {
        if (item->second.source == handle)
            item = navigation_decisions.erase(item);
        else
            ++item;
    }
    if (resource->controller)
        resource->controller->Close();
    resource->webview.Reset();
    resource->controller.Reset();
    resource->environment.Reset();
    if (auto parent = get_window(resource->parent)) {
        auto &children = parent->children;
        children.erase(std::remove(children.begin(), children.end(), handle), children.end());
    }
    nk::core::handles().erase(handle, nk::core::ResourceType::webview);
    return NK_OK;
}

nk_result NK_CALL nk_webview_show(nk_handle handle, uint32_t visible) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = get_webview(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    resource->visible = visible != 0;
    if (resource->controller)
        resource->controller->put_IsVisible(resource->visible ? TRUE : FALSE);
    return resource->failed ? fail(NK_ERROR_UNKNOWN, "WebView2 initialization failed") : NK_OK;
}

nk_result NK_CALL nk_webview_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                        int32_t height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (width <= 0 || height <= 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "WebView dimensions must be positive");
    auto resource = get_webview(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    resource->bounds = {x, y, coordinate_end(x, width), coordinate_end(y, height)};
    apply_webview_bounds(resource);
    return resource->failed ? fail(NK_ERROR_UNKNOWN, "WebView2 initialization failed") : NK_OK;
}

nk_result NK_CALL nk_webview_navigate(nk_handle handle, const char *url) {
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!url)
            return fail(NK_ERROR_INVALID_ARGUMENT, "URL must not be null");
        auto resource = get_webview(handle);
        if (!resource)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
        const auto value = wide(url);
        if (*url && value.empty())
            return fail(NK_ERROR_INVALID_ARGUMENT, "URL is not valid UTF-8");
        if (!resource->webview) {
            if (resource->failed)
                return fail(NK_ERROR_UNKNOWN, "WebView2 initialization failed");
            resource->pending.push_back(
                WebViewCommand{WebViewCommandKind::navigate, value, {}, NK_INVALID_REQUEST_ID});
            return NK_OK;
        }
        return SUCCEEDED(resource->webview->Navigate(value.c_str()))
                   ? NK_OK
                   : fail(NK_ERROR_UNKNOWN, "WebView2 navigation request failed");
    } catch (const std::bad_alloc &) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while queuing navigation");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while navigating WebView");
    }
}

nk_result NK_CALL nk_webview_set_html(nk_handle handle, const char *html, const char *base_url) {
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!html)
            return fail(NK_ERROR_INVALID_ARGUMENT, "HTML must not be null");
        auto resource = get_webview(handle);
        if (!resource)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
        auto value = wide(html);
        if (*html && value.empty())
            return fail(NK_ERROR_INVALID_ARGUMENT, "HTML is not valid UTF-8");
        std::wstring base;
        if (!copy_wide(base_url, base))
            return fail(NK_ERROR_INVALID_ARGUMENT, "base URL is not valid UTF-8");
        if (!resource->webview) {
            if (resource->failed)
                return fail(NK_ERROR_UNKNOWN, "WebView2 initialization failed");
            resource->pending.push_back(WebViewCommand{WebViewCommandKind::html, std::move(value),
                                                       std::move(base), NK_INVALID_REQUEST_ID});
            return NK_OK;
        }
        const auto document = html_document(std::move(value), base);
        return SUCCEEDED(resource->webview->NavigateToString(document.c_str()))
                   ? NK_OK
                   : fail(NK_ERROR_UNKNOWN, "WebView2 HTML navigation failed");
    } catch (const std::bad_alloc &) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while queuing HTML");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while setting WebView HTML");
    }
}

nk_result NK_CALL nk_webview_eval(nk_handle handle, const char *script,
                                  nk_request_id *out_request) {
    nk_request_id request = NK_INVALID_REQUEST_ID;
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!script || !out_request)
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid JavaScript evaluation arguments");
        *out_request = NK_INVALID_REQUEST_ID;
        auto resource = get_webview(handle);
        if (!resource)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
        const auto value = wide(script);
        if (*script && value.empty())
            return fail(NK_ERROR_INVALID_ARGUMENT, "JavaScript is not valid UTF-8");
        request = nk::core::next_request_id();
        evaluations.emplace(request, handle);
        if (!resource->webview) {
            if (resource->failed)
                return fail(NK_ERROR_UNKNOWN, "WebView2 initialization failed");
            resource->pending.push_back(
                WebViewCommand{WebViewCommandKind::evaluate, value, {}, request});
            *out_request = request;
            return NK_OK;
        }
        const HRESULT result = execute_script(resource, value, request);
        if (FAILED(result)) {
            evaluations.erase(request);
            return fail(NK_ERROR_UNKNOWN, "could not evaluate JavaScript");
        }
        *out_request = request;
        return NK_OK;
    } catch (const std::bad_alloc &) {
        if (request)
            evaluations.erase(request);
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while evaluating JavaScript");
    } catch (...) {
        if (request)
            evaluations.erase(request);
        return fail(NK_ERROR_UNKNOWN, "unexpected error while evaluating JavaScript");
    }
}

nk_result NK_CALL nk_webview_navigation_decide(nk_request_id request, uint32_t allow) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    const auto item = navigation_decisions.find(request);
    if (item == navigation_decisions.end())
        return fail(NK_ERROR_INVALID_REQUEST, "invalid or completed navigation request");
    const auto decision = std::move(item->second);
    navigation_decisions.erase(item);
    auto resource = get_webview(decision.source);
    if (!resource || !resource->webview)
        return fail(NK_ERROR_INVALID_REQUEST, "navigation WebView is no longer available");
    if (allow) {
        resource->policy_bypass_url = decision.url;
        if (FAILED(resource->webview->Navigate(decision.url.c_str()))) {
            resource->policy_bypass_url.clear();
            return fail(NK_ERROR_UNKNOWN, "could not resume WebView navigation");
        }
    }
    return NK_OK;
}
#else
nk_result NK_CALL nk_webview_create(nk_handle, const nk_webview_options *, nk_handle *) {
    return unsupported();
}
nk_result NK_CALL nk_webview_destroy(nk_handle) {
    return unsupported();
}
nk_result NK_CALL nk_webview_show(nk_handle, uint32_t) {
    return unsupported();
}
nk_result NK_CALL nk_webview_set_bounds(nk_handle, int32_t, int32_t, int32_t, int32_t) {
    return unsupported();
}
nk_result NK_CALL nk_webview_navigate(nk_handle, const char *) {
    return unsupported();
}
nk_result NK_CALL nk_webview_set_html(nk_handle, const char *, const char *) {
    return unsupported();
}
nk_result NK_CALL nk_webview_eval(nk_handle, const char *, nk_request_id *) {
    return unsupported();
}
nk_result NK_CALL nk_webview_navigation_decide(nk_request_id, uint32_t) {
    return unsupported();
}
#endif
nk_result NK_CALL nk_dialog_open_file(nk_handle parent, const nk_file_dialog_options *options,
                                      nk_request_id *out_request) {
    try {
        return start_file_dialog(parent, options, out_request, NK_DIALOG_OPEN_FILE);
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while opening file dialog");
    }
}

nk_result NK_CALL nk_dialog_save_file(nk_handle parent, const nk_file_dialog_options *options,
                                      nk_request_id *out_request) {
    try {
        return start_file_dialog(parent, options, out_request, NK_DIALOG_SAVE_FILE);
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while opening save dialog");
    }
}

nk_result NK_CALL nk_dialog_select_directory(nk_handle parent,
                                             const nk_file_dialog_options *options,
                                             nk_request_id *out_request) {
    try {
        return start_file_dialog(parent, options, out_request, NK_DIALOG_SELECT_DIRECTORY);
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while opening directory dialog");
    }
}

nk_result NK_CALL nk_dialog_message(nk_handle parent_handle,
                                    const nk_message_dialog_options *options,
                                    nk_request_id *out_request) {
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!options || options->struct_size < sizeof(*options) || !out_request ||
            !options->message)
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid message dialog options");
        auto context = std::make_shared<WinDialogContext>();
        if (parent_handle != NK_INVALID_HANDLE) {
            auto parent = get_window(parent_handle);
            if (!parent)
                return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
            context->parent = parent->window;
        }
        context->request = nk::core::next_request_id();
        context->generation = nk::core::runtime_generation();
        context->kind = NK_DIALOG_MESSAGE;
        if (!copy_wide(options->title, context->title) ||
            !copy_wide(options->message, context->message))
            return fail(NK_ERROR_INVALID_ARGUMENT, "message dialog option is not valid UTF-8");
        context->message_kind = options->kind;
        context->buttons = options->buttons;
        {
            std::lock_guard lock(dialogs_mutex);
            dialogs.emplace(context->request, context);
        }
        try {
            context->worker = std::thread(run_message_dialog, context);
        } catch (...) {
            std::lock_guard lock(dialogs_mutex);
            dialogs.erase(context->request);
            throw;
        }
        *out_request = context->request;
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while creating message dialog");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while creating message dialog");
    }
}

nk_result NK_CALL nk_dialog_cancel(nk_request_id request) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    std::lock_guard lock(dialogs_mutex);
    const auto found = dialogs.find(request);
    if (request == NK_INVALID_REQUEST_ID || found == dialogs.end() || found->second->complete)
        return fail(NK_ERROR_INVALID_REQUEST, "invalid or completed dialog request");
    request_dialog_close(found->second);
    return NK_OK;
}

nk_result NK_CALL nk_clipboard_set_text(const char *text) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!text)
        return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard text must not be null");
    const auto value = wide(text);
    if (*text && value.empty())
        return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard text is not valid UTF-8");
    const std::size_t bytes = (value.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory)
        return fail(NK_ERROR_OUT_OF_MEMORY, "could not allocate clipboard text");
    void *destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        return fail(NK_ERROR_UNKNOWN, "could not lock clipboard text");
    }
    std::memcpy(destination, value.c_str(), bytes);
    GlobalUnlock(memory);
    if (!open_clipboard()) {
        GlobalFree(memory);
        return fail(NK_ERROR_UNKNOWN, "clipboard is busy");
    }
    EmptyClipboard();
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        CloseClipboard();
        GlobalFree(memory);
        return fail(NK_ERROR_UNKNOWN, "Windows rejected clipboard text");
    }
    CloseClipboard();
    return NK_OK;
}

nk_result NK_CALL nk_clipboard_set_files(const char *const *paths, uint32_t path_count) {
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!paths || path_count == 0)
            return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard file list must not be empty");
        std::vector<std::wstring> native_paths;
        native_paths.reserve(path_count);
        std::size_t character_count = 1;
        for (uint32_t index = 0; index < path_count; ++index) {
            if (!paths[index] || !*paths[index])
                return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard path must not be empty");
            auto path = absolute_path(paths[index]);
            if (path.empty())
                return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard path is not valid UTF-8");
            if (path.size() >= std::numeric_limits<std::size_t>::max() - character_count)
                return fail(NK_ERROR_OUT_OF_MEMORY, "clipboard file list is too large");
            character_count += path.size() + 1;
            native_paths.push_back(std::move(path));
        }
        if (character_count >
            (std::numeric_limits<std::size_t>::max() - sizeof(DROPFILES)) / sizeof(wchar_t))
            return fail(NK_ERROR_OUT_OF_MEMORY, "clipboard file list is too large");
        const std::size_t size = sizeof(DROPFILES) + character_count * sizeof(wchar_t);
        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, size);
        if (!memory)
            return fail(NK_ERROR_OUT_OF_MEMORY, "could not allocate clipboard files");
        auto *drop = static_cast<DROPFILES *>(GlobalLock(memory));
        if (!drop) {
            GlobalFree(memory);
            return fail(NK_ERROR_UNKNOWN, "could not lock clipboard files");
        }
        drop->pFiles = sizeof(DROPFILES);
        drop->fWide = TRUE;
        auto *cursor =
            reinterpret_cast<wchar_t *>(reinterpret_cast<std::byte *>(drop) + drop->pFiles);
        for (const auto &path : native_paths) {
            std::memcpy(cursor, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
            cursor += path.size() + 1;
        }
        *cursor = L'\0';
        GlobalUnlock(memory);
        if (!open_clipboard()) {
            GlobalFree(memory);
            return fail(NK_ERROR_UNKNOWN, "clipboard is busy");
        }
        EmptyClipboard();
        if (!SetClipboardData(CF_HDROP, memory)) {
            CloseClipboard();
            GlobalFree(memory);
            return fail(NK_ERROR_UNKNOWN, "Windows rejected clipboard files");
        }
        CloseClipboard();
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while writing clipboard files");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while writing clipboard files");
    }
}

nk_result NK_CALL nk_clipboard_read_text(nk_request_id *out_request) {
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!out_request)
            return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard request output is null");
        *out_request = NK_INVALID_REQUEST_ID;
        std::string text;
        if (IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            if (!open_clipboard())
                return fail(NK_ERROR_UNKNOWN, "clipboard is busy");
            ClipboardCloseGuard close;
            HANDLE memory = GetClipboardData(CF_UNICODETEXT);
            const auto *value = memory ? static_cast<const wchar_t *>(GlobalLock(memory)) : nullptr;
            if (!memory || !value)
                return fail(NK_ERROR_UNKNOWN, "could not read clipboard text");
            GlobalUnlockGuard unlock{static_cast<HGLOBAL>(memory)};
            text = utf8(value);
        }
        const auto request = nk::core::next_request_id();
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_CLIPBOARD_TEXT_COMPLETE;
        event.request_id = request;
        event.data = text_bytes(text);
        const auto result = nk::core::push_event(std::move(event));
        if (result != NK_OK)
            return fail(result, "could not queue clipboard text result");
        *out_request = request;
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while reading clipboard text");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while reading clipboard text");
    }
}

nk_result NK_CALL nk_clipboard_read_files(nk_request_id *out_request) {
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!out_request)
            return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard request output is null");
        *out_request = NK_INVALID_REQUEST_ID;
        std::vector<std::string> paths;
        if (IsClipboardFormatAvailable(CF_HDROP)) {
            if (!open_clipboard())
                return fail(NK_ERROR_UNKNOWN, "clipboard is busy");
            ClipboardCloseGuard close;
            HDROP drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
            if (drop) {
                const UINT count = DragQueryFileW(drop, 0xffffffffu, nullptr, 0);
                paths.reserve(count);
                for (UINT index = 0; index < count; ++index) {
                    const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                    std::wstring path(static_cast<std::size_t>(length) + 1, L'\0');
                    if (DragQueryFileW(drop, index, path.data(), length + 1)) {
                        path.resize(length);
                        paths.push_back(utf8(path.c_str()));
                    }
                }
            }
            if (!drop)
                return fail(NK_ERROR_UNKNOWN, "could not read clipboard files");
        }
        const auto request = nk::core::next_request_id();
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_CLIPBOARD_FILES_COMPLETE;
        event.request_id = request;
        event.data_count = static_cast<uint32_t>(paths.size());
        nk_clipboard_files header{static_cast<uint32_t>(paths.size()), 0};
        event.data = string_list_payload(header, paths, &nk_clipboard_files::strings_offset);
        const auto result = nk::core::push_event(std::move(event));
        if (result != NK_OK)
            return fail(result, "could not queue clipboard file result");
        *out_request = request;
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while reading clipboard files");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while reading clipboard files");
    }
}

nk_result NK_CALL nk_window_set_drop_enabled(nk_handle handle, uint32_t enabled) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    resource->drops_enabled = enabled != 0;
    DragAcceptFiles(resource->window, enabled != 0);
    return NK_OK;
}
nk_result NK_CALL nk_shell_open_url(const char *url) {
    return nk::core::result_boundary("unexpected error while opening URL", [&] {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        return shell_open(url, true);
    });
}

nk_result NK_CALL nk_shell_open_resource(const nk_resource *resource) {
    if (!resource || resource->struct_size < sizeof(nk_resource))
        return fail(NK_ERROR_INVALID_ARGUMENT, "resource descriptor is invalid");
    return shell_open(resource->uri, true);
}

nk_result NK_CALL nk_share(const nk_share_options *) {
    return fail(NK_ERROR_UNSUPPORTED, "resource sharing is not implemented by the Windows backend");
}

nk_result NK_CALL nk_clipboard_set_resources(const nk_resource *, uint32_t) {
    return fail(NK_ERROR_UNSUPPORTED, "resource clipboard is not implemented by the Windows backend");
}

nk_result NK_CALL nk_clipboard_read_resources(nk_request_id *) {
    return fail(NK_ERROR_UNSUPPORTED, "resource clipboard is not implemented by the Windows backend");
}

nk_result NK_CALL nk_dialog_open_resource(nk_handle, const nk_file_dialog_options *, nk_request_id *) {
    return fail(NK_ERROR_UNSUPPORTED, "resource dialogs are not implemented by the Windows backend");
}

nk_result NK_CALL nk_dialog_save_resource(nk_handle, const nk_file_dialog_options *, nk_request_id *) {
    return fail(NK_ERROR_UNSUPPORTED, "resource dialogs are not implemented by the Windows backend");
}

nk_result NK_CALL nk_dialog_select_resource_directory(nk_handle, const nk_file_dialog_options *, nk_request_id *) {
    return fail(NK_ERROR_UNSUPPORTED, "resource dialogs are not implemented by the Windows backend");
}

nk_result NK_CALL nk_shell_open_file(const char *path) {
    return nk::core::result_boundary("unexpected error while opening file", [&] {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        return shell_open(path, false);
    });
}

nk_result NK_CALL nk_shell_reveal_file(const char *path) {
    return nk::core::result_boundary("unexpected error while revealing file", [&] {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!path || !*path)
            return fail(NK_ERROR_INVALID_ARGUMENT, "path must not be empty");
        const auto native = wide(path);
        if (native.empty())
            return fail(NK_ERROR_INVALID_ARGUMENT, "path is not valid UTF-8");
        PIDLIST_ABSOLUTE item = ILCreateFromPathW(native.c_str());
        if (!item)
            return fail(NK_ERROR_INVALID_ARGUMENT, "Windows could not resolve the file path");
        PIDLIST_ABSOLUTE folder = ILCloneFull(item);
        if (!folder) {
            ILFree(item);
            return fail(NK_ERROR_OUT_OF_MEMORY, "could not allocate shell item identifier");
        }
        PCUITEMID_CHILD child = ILFindLastID(item);
        ILRemoveLastID(folder);
        const HRESULT status = SHOpenFolderAndSelectItems(folder, 1, &child, 0);
        ILFree(folder);
        ILFree(item);
        return SUCCEEDED(status) ? NK_OK
                                 : fail(NK_ERROR_UNKNOWN, "Windows could not reveal the file");
    });
}

nk_result NK_CALL nk_system_directory(nk_system_directory_kind kind, char *buffer,
                                      uint32_t *inout_size) {
    return nk::core::result_boundary("unexpected error while reading system directory", [&] {
        nk::core::clear_error();
        std::string path;
        const auto result = get_system_directory(kind, path);
        return result == NK_OK ? copy_utf8_output(path, buffer, inout_size) : result;
    });
}

nk_result NK_CALL nk_system_locale(char *buffer, uint32_t *inout_size) {
    return nk::core::result_boundary("unexpected error while reading system locale", [&] {
        nk::core::clear_error();
        wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
        const int size = GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
        if (!size)
            return fail(NK_ERROR_UNKNOWN, "could not read system locale");
        const auto value = utf8(locale);
        if (value.empty())
            return fail(NK_ERROR_UNKNOWN, "could not encode system locale");
        return copy_utf8_output(value, buffer, inout_size);
    });
}

nk_result NK_CALL nk_system_get_appearance(nk_system_appearance *appearance) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!appearance || appearance->struct_size < sizeof(*appearance))
        return fail(NK_ERROR_INVALID_ARGUMENT, "appearance output is missing or too small");
    const auto struct_size = appearance->struct_size;
    *appearance = {};
    appearance->struct_size = struct_size;
    DWORD light_theme = 1;
    DWORD light_theme_size = sizeof(light_theme);
    const LSTATUS registry = RegGetValueW(
        HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light_theme, &light_theme_size);
    appearance->color_scheme = registry == ERROR_SUCCESS && light_theme == 0
                                   ? NK_COLOR_SCHEME_DARK
                                   : NK_COLOR_SCHEME_LIGHT;
    HIGHCONTRASTW high_contrast{};
    high_contrast.cbSize = sizeof(high_contrast);
    if (SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(high_contrast), &high_contrast, 0))
        appearance->high_contrast = (high_contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
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
            *out_request = NK_INVALID_REQUEST_ID;
            const auto title = wide(options->title);
            const auto body = wide(options->body);
            const auto icon_path = wide(options->icon);
            if (title.empty() || (options->body && *options->body && body.empty()) ||
                (options->icon && *options->icon && icon_path.empty()))
                return fail(NK_ERROR_INVALID_ARGUMENT, "notification text is not valid UTF-8");
            if (!ensure_notification_window())
                return fail(NK_ERROR_UNSUPPORTED, "Windows notification host is unavailable");
            UINT id = next_notification_id.fetch_add(1, std::memory_order_relaxed);
            if (!id)
                id = next_notification_id.fetch_add(1, std::memory_order_relaxed);
            const auto request = nk::core::next_request_id();
            NOTIFYICONDATAW icon{};
            icon.cbSize = sizeof(icon);
            icon.hWnd = notification_window;
            icon.uID = id;
            icon.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_INFO;
            icon.uCallbackMessage = notification_message;
            HICON loaded_icon = nullptr;
            if (!icon_path.empty())
                loaded_icon =
                    static_cast<HICON>(LoadImageW(nullptr, icon_path.c_str(), IMAGE_ICON, 0, 0,
                                                  LR_LOADFROMFILE | LR_DEFAULTSIZE));
            icon.hIcon = loaded_icon ? loaded_icon : LoadIconW(nullptr, IDI_APPLICATION);
            copy_notification_text(icon.szTip, title);
            copy_notification_text(icon.szInfoTitle, title);
            copy_notification_text(icon.szInfo, body);
            icon.dwInfoFlags = NIIF_INFO;
            if (options->flags & NK_NOTIFICATION_SILENT)
                icon.dwInfoFlags |= NIIF_NOSOUND;
            icon.uTimeout = options->timeout_ms;
            notifications.emplace(request, WinNotification{request, id});
            try {
                notification_ids.emplace(id, request);
            } catch (...) {
                notifications.erase(request);
                throw;
            }
            if (!Shell_NotifyIconW(NIM_ADD, &icon)) {
                notification_ids.erase(id);
                notifications.erase(request);
                if (loaded_icon)
                    DestroyIcon(loaded_icon);
                return fail(NK_ERROR_UNSUPPORTED,
                            "Windows notification area rejected the notification");
            }
            if (loaded_icon)
                DestroyIcon(loaded_icon);
            *out_request = request;
            emit_notification(NK_EVENT_NOTIFICATION_DELIVERED, request);
            return NK_OK;
        });
}

nk_result NK_CALL nk_notification_close(nk_request_id request) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!request || notifications.find(request) == notifications.end())
        return fail(NK_ERROR_INVALID_REQUEST, "invalid or completed notification request");
    remove_notification(request);
    emit_notification(NK_EVENT_NOTIFICATION_DISMISSED, request);
    return NK_OK;
}
}
