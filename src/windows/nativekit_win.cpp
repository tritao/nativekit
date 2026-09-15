#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_graphics.h"
#include "nativekit_input.h"
#include "nativekit_notification.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_monitor.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include "core/error.hpp"
#include "core/boundary.hpp"
#include "core/graphics_frame_target.hpp"
#include "core/graphics_image_registry.h"
#include "core/runtime.hpp"
#include "platform/resource_events.hpp"
#include "windows/joystick.hpp"

#define UNICODE
#define _UNICODE
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <imm.h>
#include <windowsx.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include "nativekit_win_accessibility.hpp"

#if defined(NK_HAS_WEBVIEW2)
#include <WebView2.h>
#include <wrl.h>
#endif

#include <atomic>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
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

using Microsoft::WRL::ComPtr;

constexpr wchar_t window_class_name[] = L"NativeKitWindow";
constexpr wchar_t surface_class_name[] = L"NativeKitD3D11Surface";
constexpr UINT notification_message = WM_APP + 42;
constexpr UINT_PTR surface_frame_timer = 1;
ATOM window_class = 0;
ATOM surface_window_class = 0;
HWND notification_window = nullptr;

UINT query_window_dpi(HWND window);

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
    bool resources = false;
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
    int32_t aspect_numerator = 0;
    int32_t aspect_denominator = 0;
    bool resizable = false;
    bool decorated = true;
    bool drops_enabled = false;
    bool mouse_passthrough = false;
    std::array<nk_input_action, NK_KEY_LAST + 1> keys{};
    std::array<nk_input_action, NK_POINTER_BUTTON_LAST + 1> pointer_buttons{};
    double pointer_x = 0.0;
    double pointer_y = 0.0;
    nk_cursor_mode cursor_mode = NK_CURSOR_MODE_NORMAL;
    std::shared_ptr<struct WinCursorResource> cursor;
    bool pointer_captured = false;
    bool pointer_tracking = false;
    std::string text_input_text;
    nk_text_input_state text_input_state{};
    bool text_input_active = false;
    bool text_composing = false;
    nk_text_position text_composition_start = NK_TEXT_POSITION_NONE;
    nk_text_position text_composition_end = NK_TEXT_POSITION_NONE;
    wchar_t pending_high_surrogate = 0;
    uint32_t skip_ime_characters = 0;
    std::unordered_map<uint32_t, nk_touch_tool> active_touch_pointers;
    std::vector<nk_handle> children;
    std::vector<nk_handle> surfaces;
    std::vector<nk_handle> owned_windows;
    ~WinWindowResource() override {
        if (pointer_captured && GetCapture() == window)
            ReleaseCapture();
        if (window && IsWindow(window))
            DestroyWindow(window);
    }
};

struct WinMonitorResource final : nk::core::Resource {
    HMONITOR monitor = nullptr;
    std::string name;
    nk_handle handle = NK_INVALID_HANDLE;
};

std::unordered_map<HMONITOR, nk_handle> monitor_handles;

struct WinSurfaceResource final : nk::core::Resource {
    HWND window = nullptr;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle parent = NK_INVALID_HANDLE;
    nk_handle device_handle = NK_INVALID_HANDLE;
    nk_graphics_api api = NK_GRAPHICS_D3D11;
    nk_surface_flags flags = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t framebuffer_width = 0;
    int32_t framebuffer_height = 0;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain1> swapchain;
    ComPtr<ID3D11RenderTargetView> render_target;
    ComPtr<ID3D11Texture2D> depth_texture;
    ComPtr<ID3D11DepthStencilView> depth_stencil_target;
    std::shared_ptr<WinSurfaceResource> shared_surface;
    uint32_t share_dependents = 0;
    nk_surface_frame_callback frame_callback = nullptr;
    void *frame_user_data = nullptr;
    bool frame_prepared = false;
    bool ready = false;
    bool lost_reported = false;
    bool destroying = false;

    ~WinSurfaceResource() override {
        if (window && IsWindow(window))
            DestroyWindow(window);
    }
};

struct WinCursorResource final : nk::core::Resource {
    HCURSOR cursor = nullptr;
    nk_handle handle = NK_INVALID_HANDLE;
    bool owned = false;

    ~WinCursorResource() override {
        if (owned && cursor)
            DestroyCursor(cursor);
    }
};

std::shared_ptr<WinSurfaceResource> get_surface(nk_handle handle);
bool set_surface_native_bounds(WinSurfaceResource &surface);

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

void queue_input_event(nk_event_kind kind, nk_handle source, std::vector<std::byte> data,
                       uint32_t flags = 0) {
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = source;
    event.flags = flags;
    event.data = std::move(data);
    nk::core::push_event(std::move(event));
}

nk_modifiers current_modifiers() {
    nk_modifiers result = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000)
        result |= NK_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000)
        result |= NK_MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000)
        result |= NK_MOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000)
        result |= NK_MOD_SUPER;
    if (GetKeyState(VK_CAPITAL) & 1)
        result |= NK_MOD_CAPS_LOCK;
    if (GetKeyState(VK_NUMLOCK) & 1)
        result |= NK_MOD_NUM_LOCK;
    return result;
}

nk_key key_from_windows(WPARAM virtual_key, LPARAM message_data) {
    if (virtual_key >= '0' && virtual_key <= '9')
        return static_cast<nk_key>(NK_KEY_0 + virtual_key - '0');
    if (virtual_key >= 'A' && virtual_key <= 'Z')
        return static_cast<nk_key>(NK_KEY_A + virtual_key - 'A');
    if (virtual_key >= VK_F1 && virtual_key <= VK_F24)
        return static_cast<nk_key>(NK_KEY_F1 + virtual_key - VK_F1);
    if (virtual_key >= VK_NUMPAD0 && virtual_key <= VK_NUMPAD9)
        return static_cast<nk_key>(NK_KEY_KP_0 + virtual_key - VK_NUMPAD0);
    switch (virtual_key) {
    case VK_SPACE:
        return NK_KEY_SPACE;
    case VK_OEM_7:
        return NK_KEY_APOSTROPHE;
    case VK_OEM_COMMA:
        return NK_KEY_COMMA;
    case VK_OEM_MINUS:
        return NK_KEY_MINUS;
    case VK_OEM_PERIOD:
        return NK_KEY_PERIOD;
    case VK_OEM_2:
        return NK_KEY_SLASH;
    case VK_OEM_1:
        return NK_KEY_SEMICOLON;
    case VK_OEM_PLUS:
        return NK_KEY_EQUAL;
    case VK_OEM_4:
        return NK_KEY_LEFT_BRACKET;
    case VK_OEM_5:
        return NK_KEY_BACKSLASH;
    case VK_OEM_6:
        return NK_KEY_RIGHT_BRACKET;
    case VK_OEM_3:
        return NK_KEY_GRAVE_ACCENT;
    case VK_ESCAPE:
        return NK_KEY_ESCAPE;
    case VK_RETURN:
        return (message_data & (1ll << 24)) ? NK_KEY_KP_ENTER : NK_KEY_ENTER;
    case VK_TAB:
        return NK_KEY_TAB;
    case VK_BACK:
        return NK_KEY_BACKSPACE;
    case VK_INSERT:
        return NK_KEY_INSERT;
    case VK_DELETE:
        return NK_KEY_DELETE;
    case VK_RIGHT:
        return NK_KEY_RIGHT;
    case VK_LEFT:
        return NK_KEY_LEFT;
    case VK_DOWN:
        return NK_KEY_DOWN;
    case VK_UP:
        return NK_KEY_UP;
    case VK_PRIOR:
        return NK_KEY_PAGE_UP;
    case VK_NEXT:
        return NK_KEY_PAGE_DOWN;
    case VK_HOME:
        return NK_KEY_HOME;
    case VK_END:
        return NK_KEY_END;
    case VK_CAPITAL:
        return NK_KEY_CAPS_LOCK;
    case VK_SCROLL:
        return NK_KEY_SCROLL_LOCK;
    case VK_NUMLOCK:
        return NK_KEY_NUM_LOCK;
    case VK_SNAPSHOT:
        return NK_KEY_PRINT_SCREEN;
    case VK_PAUSE:
        return NK_KEY_PAUSE;
    case VK_DECIMAL:
        return NK_KEY_KP_DECIMAL;
    case VK_DIVIDE:
        return NK_KEY_KP_DIVIDE;
    case VK_MULTIPLY:
        return NK_KEY_KP_MULTIPLY;
    case VK_SUBTRACT:
        return NK_KEY_KP_SUBTRACT;
    case VK_ADD:
        return NK_KEY_KP_ADD;
    case VK_OEM_NEC_EQUAL:
        return NK_KEY_KP_EQUAL;
    case VK_LSHIFT:
        return NK_KEY_LEFT_SHIFT;
    case VK_RSHIFT:
        return NK_KEY_RIGHT_SHIFT;
    case VK_SHIFT: {
        const UINT key =
            MapVirtualKeyW(static_cast<UINT>((message_data >> 16) & 0xff), MAPVK_VSC_TO_VK_EX);
        return key == VK_RSHIFT ? NK_KEY_RIGHT_SHIFT : NK_KEY_LEFT_SHIFT;
    }
    case VK_LCONTROL:
        return NK_KEY_LEFT_CONTROL;
    case VK_RCONTROL:
        return NK_KEY_RIGHT_CONTROL;
    case VK_CONTROL:
        return (message_data & (1ll << 24)) ? NK_KEY_RIGHT_CONTROL : NK_KEY_LEFT_CONTROL;
    case VK_LMENU:
        return NK_KEY_LEFT_ALT;
    case VK_RMENU:
        return NK_KEY_RIGHT_ALT;
    case VK_MENU:
        return (message_data & (1ll << 24)) ? NK_KEY_RIGHT_ALT : NK_KEY_LEFT_ALT;
    case VK_LWIN:
        return NK_KEY_LEFT_SUPER;
    case VK_RWIN:
        return NK_KEY_RIGHT_SUPER;
    case VK_APPS:
        return NK_KEY_MENU;
    default:
        return NK_KEY_UNKNOWN;
    }
}

uint32_t windows_scancode(LPARAM message_data) {
    const uint32_t scancode = static_cast<uint32_t>((message_data >> 16) & 0xff);
    return scancode | ((message_data & (1ll << 24)) ? 0x100u : 0u);
}

bool decode_utf8(std::string_view text, std::vector<uint32_t> *out = nullptr) {
    if (out)
        out->clear();
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<uint8_t>(text[index]);
        uint32_t value = 0;
        std::size_t count = 0;
        if (first < 0x80) {
            value = first;
            count = 1;
        } else if (first >= 0xc2 && first <= 0xdf) {
            value = first & 0x1fu;
            count = 2;
        } else if (first >= 0xe0 && first <= 0xef) {
            value = first & 0x0fu;
            count = 3;
        } else if (first >= 0xf0 && first <= 0xf4) {
            value = first & 0x07u;
            count = 4;
        } else {
            return false;
        }
        if (index + count > text.size())
            return false;
        for (std::size_t offset = 1; offset < count; ++offset) {
            const auto next = static_cast<uint8_t>(text[index + offset]);
            if ((next & 0xc0u) != 0x80u)
                return false;
            value = (value << 6) | (next & 0x3fu);
        }
        if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) ||
            (count == 4 && value < 0x10000) || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff))
            return false;
        if (out)
            out->push_back(value);
        index += count;
    }
    return true;
}

void emit_text_codepoint(WinWindowResource &resource, uint32_t codepoint) {
    if (!codepoint || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
        return;
    const nk_text_input_event payload{codepoint, 0};
    queue_input_event(NK_EVENT_TEXT_INPUT, resource.handle, bytes_of(payload));
}

void emit_text_edit(WinWindowResource &resource, nk_text_edit_event payload,
                    const std::string &text = {}) {
    payload.text_offset = text.empty() ? 0u : sizeof(payload);
    payload.text_length = static_cast<uint32_t>(text.size());
    std::vector<std::byte> bytes(sizeof(payload) + text.size() + (text.empty() ? 0u : 1u));
    std::memcpy(bytes.data(), &payload, sizeof(payload));
    if (!text.empty())
        std::memcpy(bytes.data() + sizeof(payload), text.c_str(), text.size() + 1);
    queue_input_event(NK_EVENT_TEXT_EDIT, resource.handle, std::move(bytes));
}

nk_modifiers modifiers_after_key(nk_key key, nk_input_action action) {
    auto result = current_modifiers();
    const bool down = action != NK_INPUT_RELEASE;
    auto set = [&](nk_modifiers modifier, bool value) {
        if (value)
            result |= modifier;
        else
            result &= ~modifier;
    };
    switch (key) {
    case NK_KEY_LEFT_SHIFT:
    case NK_KEY_RIGHT_SHIFT:
        set(NK_MOD_SHIFT, down);
        break;
    case NK_KEY_LEFT_CONTROL:
    case NK_KEY_RIGHT_CONTROL:
        set(NK_MOD_CONTROL, down);
        break;
    case NK_KEY_LEFT_ALT:
    case NK_KEY_RIGHT_ALT:
        set(NK_MOD_ALT, down);
        break;
    case NK_KEY_LEFT_SUPER:
    case NK_KEY_RIGHT_SUPER:
        set(NK_MOD_SUPER, down);
        break;
    default:
        break;
    }
    return result;
}

double dpi_scale(HWND window) {
    return std::max(1.0, static_cast<double>(query_window_dpi(window)) / 96.0);
}

nk_pointer_button pointer_button_from_windows(UINT message, WPARAM wparam) {
    switch (message) {
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        return NK_POINTER_BUTTON_LEFT;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
        return NK_POINTER_BUTTON_RIGHT;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        return NK_POINTER_BUTTON_MIDDLE;
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        return HIWORD(wparam) == XBUTTON1 ? NK_POINTER_BUTTON_4 : NK_POINTER_BUTTON_5;
    default:
        return UINT32_MAX;
    }
}

bool pointer_button_message(UINT message) {
    return message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_RBUTTONDOWN ||
           message == WM_RBUTTONUP || message == WM_MBUTTONDOWN || message == WM_MBUTTONUP ||
           message == WM_XBUTTONDOWN || message == WM_XBUTTONUP;
}

void update_pointer_position(WinWindowResource &resource, LPARAM coordinates) {
    const auto scale = dpi_scale(resource.window);
    resource.pointer_x = static_cast<double>(GET_X_LPARAM(coordinates)) / scale;
    resource.pointer_y = static_cast<double>(GET_Y_LPARAM(coordinates)) / scale;
}

void emit_pointer_button(WinWindowResource &resource, nk_pointer_button button,
                         nk_input_action action, nk_modifiers modifiers) {
    if (button > NK_POINTER_BUTTON_LAST)
        return;
    resource.pointer_buttons[button] = action;
    const nk_pointer_button_event payload{
        button, action, modifiers, 0, resource.pointer_x, resource.pointer_y};
    queue_input_event(NK_EVENT_POINTER_BUTTON, resource.handle, bytes_of(payload));
}

void apply_cursor(WinWindowResource &resource) {
    if (resource.cursor_mode == NK_CURSOR_MODE_HIDDEN ||
        resource.cursor_mode == NK_CURSOR_MODE_DISABLED)
        SetCursor(nullptr);
    else
        SetCursor(resource.cursor ? resource.cursor->cursor : LoadCursorW(nullptr, IDC_ARROW));
}

void emit_window_state(WinWindowResource &resource) {
    nk_window_state state{sizeof(state), 0, {0, 0}};
    if (IsWindowVisible(resource.window))
        state.flags |= NK_WINDOW_STATE_VISIBLE;
    if (GetForegroundWindow() == resource.window)
        state.flags |= NK_WINDOW_STATE_ACTIVE;
    if (IsIconic(resource.window))
        state.flags |= NK_WINDOW_STATE_MINIMIZED;
    if (IsZoomed(resource.window))
        state.flags |= NK_WINDOW_STATE_MAXIMIZED;
    if (resource.fullscreen)
        state.flags |= NK_WINDOW_STATE_FULLSCREEN;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_WINDOW_STATE_CHANGED;
    event.source = resource.handle;
    event.data = bytes_of(state);
    nk::core::push_event(std::move(event));
}

uint32_t codepoint_count(std::string_view text) {
    std::vector<uint32_t> values;
    return decode_utf8(text, &values) ? static_cast<uint32_t>(values.size()) : 0u;
}

uint32_t codepoint_count(std::wstring_view text) {
    uint32_t count = 0;
    for (std::size_t index = 0; index < text.size(); ++count, ++index) {
        if (text[index] >= 0xd800 && text[index] <= 0xdbff && index + 1 < text.size() &&
            text[index + 1] >= 0xdc00 && text[index + 1] <= 0xdfff)
            ++index;
    }
    return count;
}

void apply_text_edit_state(WinWindowResource &resource, nk_text_edit_action action,
                           nk_text_position replace_start, nk_text_position replace_end,
                           std::string_view text, nk_text_position selection_start,
                           nk_text_position selection_end, nk_text_position composition_start,
                           nk_text_position composition_end) {
    resource.text_input_state.selection_start = selection_start;
    resource.text_input_state.selection_end = selection_end;
    resource.text_input_state.composition_start = composition_start;
    resource.text_input_state.composition_end = composition_end;
    resource.text_composing = composition_start != NK_TEXT_POSITION_NONE;
    resource.text_composition_start = composition_start;
    resource.text_composition_end = composition_end;
    nk_text_edit_event payload{};
    payload.action = action;
    payload.replace_start = replace_start;
    payload.replace_end = replace_end;
    payload.selection_start = selection_start;
    payload.selection_end = selection_end;
    payload.composition_start = composition_start;
    payload.composition_end = composition_end;
    emit_text_edit(resource, payload, std::string(text));
}

void emit_text_deletion(WinWindowResource &resource, bool backward) {
    auto start = std::min(resource.text_input_state.selection_start,
                          resource.text_input_state.selection_end);
    auto end = std::max(resource.text_input_state.selection_start,
                        resource.text_input_state.selection_end);
    if (start == end) {
        if (backward && start > 0)
            --start;
        else if (!backward && end < resource.text_input_state.document_length)
            ++end;
    }
    if (start == end)
        return;
    apply_text_edit_state(resource, NK_TEXT_EDIT_DELETE, start, end, {}, start, start,
                          NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
}

nk_text_position text_replacement_start(const WinWindowResource &resource) {
    return resource.text_composing ? resource.text_composition_start
                                   : resource.text_input_state.selection_start;
}

nk_text_position text_replacement_end(const WinWindowResource &resource) {
    return resource.text_composing ? resource.text_composition_end
                                   : resource.text_input_state.selection_end;
}

void emit_committed_utf8(WinWindowResource &resource, const std::string &text) {
    std::vector<uint32_t> codepoints;
    if (!decode_utf8(text, &codepoints))
        return;
    if (!resource.text_input_active) {
        for (const auto codepoint : codepoints)
            emit_text_codepoint(resource, codepoint);
        return;
    }
    const auto start = text_replacement_start(resource);
    const auto end = text_replacement_end(resource);
    const auto cursor = static_cast<nk_text_position>(start + codepoints.size());
    apply_text_edit_state(resource, NK_TEXT_EDIT_COMMIT, start, end, text, cursor, cursor,
                          NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
}

void finish_text_composition(WinWindowResource &resource) {
    if (!resource.text_composing)
        return;
    auto selection_start = resource.text_input_state.selection_start;
    auto selection_end = resource.text_input_state.selection_end;
    if (selection_start == NK_TEXT_POSITION_NONE)
        selection_start = selection_end = resource.text_composition_end;
    apply_text_edit_state(resource, NK_TEXT_EDIT_FINISH_COMPOSITION, NK_TEXT_POSITION_NONE,
                          NK_TEXT_POSITION_NONE, {}, selection_start, selection_end,
                          NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
}

bool get_ime_string(HIMC context, DWORD index, std::wstring &value) {
    const LONG bytes = ImmGetCompositionStringW(context, index, nullptr, 0);
    if (bytes < 0 || (bytes % static_cast<LONG>(sizeof(wchar_t))) != 0)
        return false;
    value.resize(static_cast<std::size_t>(bytes) / sizeof(wchar_t));
    return !bytes || ImmGetCompositionStringW(context, index, value.data(),
                                              static_cast<DWORD>(bytes)) == bytes;
}

void emit_key_transition(WinWindowResource &resource, WPARAM virtual_key, LPARAM message_data,
                         nk_input_action action) {
    const nk_key key = key_from_windows(virtual_key, message_data);
    if (key != NK_KEY_UNKNOWN)
        resource.keys[key] = action == NK_INPUT_RELEASE ? NK_INPUT_RELEASE : NK_INPUT_PRESS;
    const nk_key_event payload{key, windows_scancode(message_data), action,
                               modifiers_after_key(key, action)};
    queue_input_event(NK_EVENT_KEY, resource.handle, bytes_of(payload));
}

void reset_window_input(WinWindowResource &resource) {
    finish_text_composition(resource);
    resource.pending_high_surrogate = 0;
    for (nk_key key = 1; key <= NK_KEY_LAST; ++key) {
        if (resource.keys[key] != NK_INPUT_PRESS)
            continue;
        resource.keys[key] = NK_INPUT_RELEASE;
        const nk_key_event payload{key, 0, NK_INPUT_RELEASE, 0};
        queue_input_event(NK_EVENT_KEY, resource.handle, bytes_of(payload), 1u);
    }
    for (nk_pointer_button button = 0; button <= NK_POINTER_BUTTON_LAST; ++button) {
        if (resource.pointer_buttons[button] != NK_INPUT_PRESS)
            continue;
        resource.pointer_buttons[button] = NK_INPUT_RELEASE;
        const nk_pointer_button_event payload{button, NK_INPUT_RELEASE,   0,
                                              0,      resource.pointer_x, resource.pointer_y};
        queue_input_event(NK_EVENT_POINTER_BUTTON, resource.handle, bytes_of(payload), 1u);
    }
    for (const auto &[pointer_id, tool] : resource.active_touch_pointers) {
        const nk_touch_event payload{pointer_id,
                                     NK_TOUCH_CANCEL,
                                     tool,
                                     0,
                                     resource.pointer_x,
                                     resource.pointer_y,
                                     0.f,
                                     0.f,
                                     0.f,
                                     0};
        queue_input_event(NK_EVENT_TOUCH, resource.handle, bytes_of(payload), 1u);
    }
    resource.active_touch_pointers.clear();
    if (resource.pointer_captured) {
        resource.pointer_captured = false;
        if (GetCapture() == resource.window)
            ReleaseCapture();
    }
}

void emit_text_character(WinWindowResource &resource, wchar_t character) {
    if (character >= 0xd800 && character <= 0xdbff) {
        if (resource.pending_high_surrogate)
            emit_committed_utf8(resource, "\xef\xbf\xbd");
        resource.pending_high_surrogate = character;
        return;
    }
    uint32_t codepoint = character;
    if (character >= 0xdc00 && character <= 0xdfff) {
        if (!resource.pending_high_surrogate) {
            emit_committed_utf8(resource, "\xef\xbf\xbd");
            return;
        }
        codepoint =
            0x10000u + ((resource.pending_high_surrogate - 0xd800u) << 10) + (character - 0xdc00u);
        resource.pending_high_surrogate = 0;
    } else if (resource.pending_high_surrogate) {
        emit_committed_utf8(resource, "\xef\xbf\xbd");
        resource.pending_high_surrogate = 0;
    }
    if (codepoint == '\r')
        codepoint = '\n';
    std::string text;
    if (codepoint < 0x80)
        text.push_back(static_cast<char>(codepoint));
    else if (codepoint < 0x800) {
        text.push_back(static_cast<char>(0xc0u | (codepoint >> 6)));
        text.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else if (codepoint < 0x10000) {
        text.push_back(static_cast<char>(0xe0u | (codepoint >> 12)));
        text.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
        text.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    } else {
        text.push_back(static_cast<char>(0xf0u | (codepoint >> 18)));
        text.push_back(static_cast<char>(0x80u | ((codepoint >> 12) & 0x3fu)));
        text.push_back(static_cast<char>(0x80u | ((codepoint >> 6) & 0x3fu)));
        text.push_back(static_cast<char>(0x80u | (codepoint & 0x3fu)));
    }
    emit_committed_utf8(resource, text);
}

bool is_promoted_pointer_mouse() {
    constexpr ULONG_PTR pointer_signature = 0xff515700u;
    return (GetMessageExtraInfo() & 0xffffff00u) == pointer_signature;
}

bool handle_pointer_message(WinWindowResource &resource, UINT message, WPARAM wparam) {
    if (message != WM_POINTERDOWN && message != WM_POINTERUPDATE && message != WM_POINTERUP)
        return false;
    const UINT32 pointer_id = GET_POINTERID_WPARAM(wparam);
    POINTER_INPUT_TYPE type = PT_POINTER;
    if (!GetPointerType(pointer_id, &type) || type == PT_MOUSE)
        return false;
    POINTER_INFO pointer{};
    if (!GetPointerInfo(pointer_id, &pointer))
        return true;
    POINT point = pointer.ptPixelLocation;
    ScreenToClient(resource.window, &point);
    const auto scale = dpi_scale(resource.window);
    const double x = point.x / scale;
    const double y = point.y / scale;
    resource.pointer_x = x;
    resource.pointer_y = y;
    nk_touch_tool tool = type == PT_PEN ? NK_TOUCH_TOOL_STYLUS : NK_TOUCH_TOOL_FINGER;
    float pressure = type == PT_PEN ? 0.f : 1.f;
    float tilt_x = 0.f;
    float tilt_y = 0.f;
    if (type == PT_PEN) {
        POINTER_PEN_INFO pen{};
        if (GetPointerPenInfo(pointer_id, &pen)) {
            if (pen.penFlags & (PEN_FLAG_ERASER | PEN_FLAG_INVERTED))
                tool = NK_TOUCH_TOOL_ERASER;
            if (pen.penMask & PEN_MASK_PRESSURE)
                pressure = static_cast<float>(pen.pressure) / 1024.f;
            if (pen.penMask & PEN_MASK_TILT_X)
                tilt_x = static_cast<float>(pen.tiltX) / 90.f;
            if (pen.penMask & PEN_MASK_TILT_Y)
                tilt_y = static_cast<float>(pen.tiltY) / 90.f;
        }
    } else if (type == PT_TOUCH) {
        POINTER_TOUCH_INFO touch{};
        if (GetPointerTouchInfo(pointer_id, &touch) && (touch.touchMask & TOUCH_MASK_PRESSURE))
            pressure = static_cast<float>(touch.pressure) / 1024.f;
    }
    nk_modifiers modifiers = 0;
    if (pointer.dwKeyStates & MK_SHIFT)
        modifiers |= NK_MOD_SHIFT;
    if (pointer.dwKeyStates & MK_CONTROL)
        modifiers |= NK_MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000)
        modifiers |= NK_MOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000)
        modifiers |= NK_MOD_SUPER;
    if (GetKeyState(VK_CAPITAL) & 1)
        modifiers |= NK_MOD_CAPS_LOCK;
    if (GetKeyState(VK_NUMLOCK) & 1)
        modifiers |= NK_MOD_NUM_LOCK;

    auto found = resource.active_touch_pointers.find(pointer_id);
    nk_touch_action action = NK_TOUCH_MOVE;
    if ((pointer.pointerFlags & POINTER_FLAG_CANCELED) != 0) {
        if (found == resource.active_touch_pointers.end())
            return true;
        action = NK_TOUCH_CANCEL;
    } else if (message == WM_POINTERDOWN) {
        action = NK_TOUCH_BEGIN;
        resource.active_touch_pointers[pointer_id] = tool;
    } else if (message == WM_POINTERUP) {
        action = NK_TOUCH_END;
        if (found == resource.active_touch_pointers.end())
            return true;
        tool = found->second;
    } else if ((pointer.pointerFlags & POINTER_FLAG_INCONTACT) == 0) {
        return true;
    } else if (found == resource.active_touch_pointers.end()) {
        action = NK_TOUCH_BEGIN;
        resource.active_touch_pointers[pointer_id] = tool;
    } else {
        tool = found->second;
    }
    const nk_touch_event payload{pointer_id, action,   tool,   modifiers, x,
                                 y,          pressure, tilt_x, tilt_y,    0};
    queue_input_event(NK_EVENT_TOUCH, resource.handle, bytes_of(payload));
    if (action == NK_TOUCH_END || action == NK_TOUCH_CANCEL)
        resource.active_touch_pointers.erase(pointer_id);
    return true;
}

void handle_ime_composition(WinWindowResource &resource, LPARAM flags) {
    HIMC context = ImmGetContext(resource.window);
    if (!context)
        return;
    if (flags & GCS_RESULTSTR) {
        std::wstring result;
        if (get_ime_string(context, GCS_RESULTSTR, result)) {
            resource.skip_ime_characters += static_cast<uint32_t>(result.size());
            const auto committed = utf8(result.c_str());
            emit_committed_utf8(resource, committed);
            resource.text_composing = false;
            resource.text_composition_start = NK_TEXT_POSITION_NONE;
            resource.text_composition_end = NK_TEXT_POSITION_NONE;
            resource.text_input_state.composition_start = NK_TEXT_POSITION_NONE;
            resource.text_input_state.composition_end = NK_TEXT_POSITION_NONE;
        }
    } else if ((flags & GCS_COMPSTR) && resource.text_input_active) {
        std::wstring composing;
        if (get_ime_string(context, GCS_COMPSTR, composing)) {
            const auto value = utf8(composing.c_str());
            const auto start = text_replacement_start(resource);
            const auto end = text_replacement_end(resource);
            LONG cursor_units = static_cast<LONG>(composing.size());
            if (flags & GCS_CURSORPOS) {
                const LONG current = ImmGetCompositionStringW(context, GCS_CURSORPOS, nullptr, 0);
                if (current >= 0)
                    cursor_units = current;
            }
            cursor_units = std::max<LONG>(
                0, std::min<LONG>(cursor_units, static_cast<LONG>(composing.size())));
            const auto cursor = static_cast<nk_text_position>(
                start + codepoint_count(std::wstring_view(composing).substr(0, cursor_units)));
            const auto finish = static_cast<nk_text_position>(start + codepoint_count(composing));
            apply_text_edit_state(resource, NK_TEXT_EDIT_COMPOSE, start, end, value, cursor, cursor,
                                  start, finish);
        }
    }
    ImmReleaseContext(resource.window, context);
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

UINT resource_clipboard_format() {
    static const UINT format = RegisterClipboardFormatW(L"text/uri-list");
    return format;
}

HGLOBAL clipboard_bytes(const std::string &value) {
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, value.size() + 1);
    if (!memory)
        return nullptr;
    auto *destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        return nullptr;
    }
    std::memcpy(destination, value.data(), value.size());
    GlobalUnlock(memory);
    return memory;
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
        std::vector<nk::platform::ResourceValue> resources;
        paths.reserve(count);
        resources.reserve(count);
        for (UINT index = 0; index < count; ++index) {
            const UINT length = DragQueryFileW(drop, index, nullptr, 0);
            std::wstring path(static_cast<std::size_t>(length) + 1, L'\0');
            if (DragQueryFileW(drop, index, path.data(), length + 1)) {
                path.resize(length);
                auto value = utf8(path.c_str());
                if (!value.empty()) {
                    paths.push_back(value);
                    resources.push_back(nk::platform::resource_from_file_path(
                        value, NK_RESOURCE_READABLE));
                }
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
            if (!resources.empty()) {
                nk::core::QueuedEvent resource_event;
                resource_event.kind = NK_EVENT_RESOURCE_DROP;
                resource_event.source = resource.handle;
                resource_event.data_count = static_cast<uint32_t>(resources.size());
                resource_event.data = nk::platform::resource_drop_payload(
                    static_cast<float>(point.x), static_cast<float>(point.y), {}, resources);
                nk::core::push_event(std::move(resource_event));
            }
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
        if (message == WM_SETFOCUS) {
            if (resource->cursor_mode == NK_CURSOR_MODE_CAPTURED) {
                SetCapture(window);
                resource->pointer_captured = GetCapture() == window;
            }
            nk::core::callback_boundary([&] { emit_window_state(*resource); });
        }
        if (message == WM_KILLFOCUS) {
            nk::core::callback_boundary([&] {
                reset_window_input(*resource);
                emit_window_state(*resource);
            });
        }
        if (message == WM_CAPTURECHANGED && reinterpret_cast<HWND>(lparam) != window)
            resource->pointer_captured = false;
        bool pointer_handled = false;
        nk::core::callback_boundary(
            [&] { pointer_handled = handle_pointer_message(*resource, message, wparam); });
        if (pointer_handled)
            return 0;
        if (is_promoted_pointer_mouse() &&
            (message == WM_MOUSEMOVE || pointer_button_message(message) ||
             message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL))
            return message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ? TRUE : 0;
        if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_KEYUP ||
            message == WM_SYSKEYUP) {
            nk::core::callback_boundary([&] {
                const bool released = message == WM_KEYUP || message == WM_SYSKEYUP;
                const nk_key key = key_from_windows(wparam, lparam);
                nk_input_action action = released ? NK_INPUT_RELEASE : NK_INPUT_PRESS;
                if (!released && (lparam & (1ll << 30)) && key != NK_KEY_UNKNOWN)
                    action = NK_INPUT_REPEAT;
                emit_key_transition(*resource, wparam, lparam, action);
                if (!released && resource->text_input_active) {
                    if (key == NK_KEY_BACKSPACE)
                        emit_text_deletion(*resource, true);
                    else if (key == NK_KEY_DELETE)
                        emit_text_deletion(*resource, false);
                }
            });
            if (message == WM_KEYDOWN || message == WM_KEYUP)
                return 0;
        }
        if (message == WM_CHAR) {
            if (wparam == '\b')
                return 0;
            nk::core::callback_boundary(
                [&] { emit_text_character(*resource, static_cast<wchar_t>(wparam)); });
            return 0;
        }
        if (message == WM_UNICHAR) {
            if (wparam == UNICODE_NOCHAR)
                return TRUE;
            nk::core::callback_boundary([&] {
                const auto codepoint = static_cast<uint32_t>(wparam);
                if (codepoint <= 0xffff) {
                    emit_text_character(*resource, static_cast<wchar_t>(codepoint));
                    return;
                }
                const uint32_t value = codepoint - 0x10000;
                emit_text_character(*resource, static_cast<wchar_t>(0xd800u + (value >> 10)));
                emit_text_character(*resource, static_cast<wchar_t>(0xdc00u + (value & 0x3ffu)));
            });
            return 0;
        }
        if (message == WM_IME_STARTCOMPOSITION) {
            resource->text_composing = false;
            resource->skip_ime_characters = 0;
        }
        if (message == WM_IME_COMPOSITION) {
            nk::core::callback_boundary([&] { handle_ime_composition(*resource, lparam); });
            return 0;
        }
        if (message == WM_IME_ENDCOMPOSITION) {
            nk::core::callback_boundary([&] { finish_text_composition(*resource); });
        }
        if (message == WM_IME_CHAR) {
            nk::core::callback_boundary([&] {
                if (resource->skip_ime_characters)
                    --resource->skip_ime_characters;
                else
                    emit_text_character(*resource, static_cast<wchar_t>(wparam));
            });
            return 0;
        }
        if (message == WM_MOUSEMOVE) {
            nk::core::callback_boundary([&] {
                if (!resource->pointer_tracking) {
                    TRACKMOUSEEVENT tracking{sizeof(tracking), TME_LEAVE, window, 0};
                    TrackMouseEvent(&tracking);
                    resource->pointer_tracking = true;
                    queue_input_event(NK_EVENT_POINTER_ENTER, resource->handle, {}, 1u);
                }
                update_pointer_position(*resource, lparam);
                const nk_pointer_move_event payload{resource->pointer_x, resource->pointer_y};
                queue_input_event(NK_EVENT_POINTER_MOVE, resource->handle, bytes_of(payload));
            });
            return 0;
        }
        if (message == WM_MOUSELEAVE) {
            resource->pointer_tracking = false;
            nk::core::callback_boundary(
                [&] { queue_input_event(NK_EVENT_POINTER_ENTER, resource->handle, {}, 0u); });
            return 0;
        }
        if (pointer_button_message(message)) {
            nk::core::callback_boundary([&] {
                update_pointer_position(*resource, lparam);
                const bool released = message == WM_LBUTTONUP || message == WM_RBUTTONUP ||
                                      message == WM_MBUTTONUP || message == WM_XBUTTONUP;
                emit_pointer_button(*resource, pointer_button_from_windows(message, wparam),
                                    released ? NK_INPUT_RELEASE : NK_INPUT_PRESS,
                                    current_modifiers());
            });
            return message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ? TRUE : 0;
        }
        if (message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) {
            nk::core::callback_boundary([&] {
                POINT position{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
                ScreenToClient(window, &position);
                const auto scale = dpi_scale(window);
                resource->pointer_x = position.x / scale;
                resource->pointer_y = position.y / scale;
                const double amount = static_cast<double>(GET_WHEEL_DELTA_WPARAM(wparam)) /
                                      static_cast<double>(WHEEL_DELTA);
                const nk_pointer_scroll_event payload = message == WM_MOUSEWHEEL
                                                            ? nk_pointer_scroll_event{0.0, -amount}
                                                            : nk_pointer_scroll_event{amount, 0.0};
                queue_input_event(NK_EVENT_POINTER_SCROLL, resource->handle, bytes_of(payload));
            });
            return 0;
        }
        if (message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) {
            apply_cursor(*resource);
            return TRUE;
        }
        if (message == WM_NCHITTEST && resource->mouse_passthrough)
            return HTTRANSPARENT;
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
        if (message == WM_SIZING && resource->aspect_numerator && resource->aspect_denominator) {
            auto *bounds = reinterpret_cast<RECT *>(lparam);
            const double aspect = static_cast<double>(resource->aspect_numerator) /
                                  static_cast<double>(resource->aspect_denominator);
            const LONG width = bounds->right - bounds->left;
            const LONG height = bounds->bottom - bounds->top;
            const LONG width_for_height = static_cast<LONG>(std::lround(height * aspect));
            const LONG height_for_width = static_cast<LONG>(std::lround(width / aspect));
            switch (wparam) {
            case WMSZ_LEFT:
            case WMSZ_TOPLEFT:
            case WMSZ_BOTTOMLEFT:
                bounds->left = bounds->right - width_for_height;
                break;
            case WMSZ_RIGHT:
            case WMSZ_TOPRIGHT:
            case WMSZ_BOTTOMRIGHT:
                bounds->right = bounds->left + width_for_height;
                break;
            case WMSZ_TOP:
                bounds->top = bounds->bottom - height_for_width;
                break;
            case WMSZ_BOTTOM:
                bounds->bottom = bounds->top + height_for_width;
                break;
            default:
                break;
            }
            return TRUE;
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
            for (const auto surface_handle : resource->surfaces)
                if (auto child_surface = get_surface(surface_handle))
                    set_surface_native_bounds(*child_surface);
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

std::shared_ptr<WinMonitorResource> get_monitor(nk_handle handle) {
    return std::dynamic_pointer_cast<WinMonitorResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::monitor));
}

std::shared_ptr<WinSurfaceResource> get_surface(nk_handle handle) {
    return std::dynamic_pointer_cast<WinSurfaceResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::surface));
}

void emit_surface_lost(WinSurfaceResource &surface) {
    if (surface.lost_reported || surface.destroying)
        return;
    surface.lost_reported = true;
    surface.frame_prepared = false;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_SURFACE_LOST;
    event.source = surface.handle;
    nk::core::push_event(std::move(event));
}

void emit_surface_resize(WinSurfaceResource &surface) {
    const nk_surface_resize_event payload{surface.width, surface.height, surface.framebuffer_width,
                                          surface.framebuffer_height};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_SURFACE_RESIZE;
    event.source = surface.handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

bool d3d11_device_failure(const WinSurfaceResource &surface, HRESULT result) {
    return result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET ||
           result == DXGI_ERROR_DEVICE_HUNG || result == DXGI_ERROR_DRIVER_INTERNAL_ERROR ||
           (surface.device && FAILED(surface.device->GetDeviceRemovedReason()));
}

bool create_d3d11_device(WinSurfaceResource &surface) {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (surface.flags & NK_SURFACE_DEBUG_CONTEXT)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL level{};
    HRESULT result =
        D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                          D3D11_SDK_VERSION, &surface.device, &level, &surface.context);
    if (FAILED(result) && !(surface.flags & NK_SURFACE_DEBUG_CONTEXT)) {
        surface.device.Reset();
        surface.context.Reset();
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                                   D3D11_SDK_VERSION, &surface.device, &level, &surface.context);
    }
    if (FAILED(result)) {
        nk::core::set_error("could not create a Direct3D 11 device");
        return false;
    }
    return true;
}

bool create_d3d11_swapchain(WinSurfaceResource &surface, int32_t width, int32_t height) {
    ComPtr<IDXGIDevice> dxgi_device;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(surface.device.As(&dxgi_device)) || FAILED(dxgi_device->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        nk::core::set_error("could not query the Direct3D DXGI factory");
        return false;
    }
    DXGI_SWAP_CHAIN_DESC1 descriptor{};
    descriptor.Width = static_cast<UINT>(width);
    descriptor.Height = static_cast<UINT>(height);
    descriptor.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    descriptor.SampleDesc.Count = 1;
    descriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    descriptor.BufferCount = 2;
    descriptor.Scaling = DXGI_SCALING_STRETCH;
    // FLIP_SEQUENTIAL is supported by the Windows 8 minimum declared above;
    // FLIP_DISCARD would silently raise the runtime requirement to Windows 10.
    descriptor.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    descriptor.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    const HRESULT result = factory->CreateSwapChainForHwnd(
        surface.device.Get(), surface.window, &descriptor, nullptr, nullptr, &surface.swapchain);
    if (FAILED(result)) {
        nk::core::set_error("could not create the Direct3D DXGI swapchain");
        return false;
    }
    factory->MakeWindowAssociation(surface.window, DXGI_MWA_NO_ALT_ENTER);
    return true;
}

bool rebuild_surface_targets(WinSurfaceResource &surface, int32_t width, int32_t height) {
    const int32_t previous_width = surface.framebuffer_width;
    const int32_t previous_height = surface.framebuffer_height;
    surface.frame_prepared = false;
    if (width <= 0 || height <= 0) {
        surface.render_target.Reset();
        surface.depth_stencil_target.Reset();
        surface.depth_texture.Reset();
        surface.framebuffer_width = 0;
        surface.framebuffer_height = 0;
        if (previous_width != 0 || previous_height != 0)
            emit_surface_resize(surface);
        return true;
    }
    if (surface.device && FAILED(surface.device->GetDeviceRemovedReason())) {
        emit_surface_lost(surface);
        nk::core::set_error("the Direct3D 11 device was removed");
        return false;
    }
    if (!surface.swapchain) {
        if (!create_d3d11_swapchain(surface, width, height))
            return false;
    } else if (surface.framebuffer_width != width || surface.framebuffer_height != height) {
        surface.context->OMSetRenderTargets(0, nullptr, nullptr);
        surface.render_target.Reset();
        surface.depth_stencil_target.Reset();
        surface.depth_texture.Reset();
        const HRESULT result = surface.swapchain->ResizeBuffers(
            0, static_cast<UINT>(width), static_cast<UINT>(height), DXGI_FORMAT_UNKNOWN, 0);
        if (FAILED(result)) {
            if (d3d11_device_failure(surface, result))
                emit_surface_lost(surface);
            nk::core::set_error("could not resize the Direct3D DXGI swapchain");
            return false;
        }
    }

    ComPtr<ID3D11Texture2D> backbuffer;
    if (FAILED(surface.swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))) ||
        FAILED(surface.device->CreateRenderTargetView(backbuffer.Get(), nullptr,
                                                      &surface.render_target))) {
        nk::core::set_error("could not create the Direct3D swapchain render target");
        return false;
    }
    if (surface.flags & (NK_SURFACE_DEPTH | NK_SURFACE_STENCIL)) {
        D3D11_TEXTURE2D_DESC depth_descriptor{};
        depth_descriptor.Width = static_cast<UINT>(width);
        depth_descriptor.Height = static_cast<UINT>(height);
        depth_descriptor.MipLevels = 1;
        depth_descriptor.ArraySize = 1;
        depth_descriptor.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depth_descriptor.SampleDesc.Count = 1;
        depth_descriptor.Usage = D3D11_USAGE_DEFAULT;
        depth_descriptor.BindFlags = D3D11_BIND_DEPTH_STENCIL;
        if (FAILED(surface.device->CreateTexture2D(&depth_descriptor, nullptr,
                                                   &surface.depth_texture)) ||
            FAILED(surface.device->CreateDepthStencilView(surface.depth_texture.Get(), nullptr,
                                                          &surface.depth_stencil_target))) {
            nk::core::set_error("could not create the Direct3D depth/stencil target");
            return false;
        }
    }
    surface.framebuffer_width = width;
    surface.framebuffer_height = height;
    if (surface.ready && (previous_width != width || previous_height != height))
        emit_surface_resize(surface);
    return true;
}

LRESULT CALLBACK surface_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto *surface =
        reinterpret_cast<WinSurfaceResource *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lparam);
        surface = static_cast<WinSurfaceResource *>(create->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(surface));
    }
    if (surface && message == WM_GETOBJECT) {
        LRESULT result = 0;
        if (nk::windows::accessibility_handle_getobject(window, wparam, lparam, &result))
            return result;
    }
    if (surface && message == WM_MOUSEACTIVATE)
        return MA_NOACTIVATE;
    if (surface && message == WM_NCHITTEST)
        return HTTRANSPARENT;
    if (surface && message == WM_TIMER && wparam == surface_frame_timer &&
        surface->frame_callback && !surface->destroying) {
        auto active = get_surface(surface->handle);
        if (!active)
            return 0;
        nk::core::callback_boundary([&] {
            if (nk_surface_make_current(active->handle) != NK_OK)
                return;
            const auto callback = active->frame_callback;
            void *user_data = active->frame_user_data;
            callback(active->handle, active->framebuffer_width, active->framebuffer_height,
                     user_data);
            if (active->frame_prepared)
                nk_surface_present(active->handle);
        });
        return 0;
    }
    if (surface && message == WM_SIZE && surface->handle != NK_INVALID_HANDLE &&
        !surface->destroying) {
        nk::core::callback_boundary([&] {
            if (!rebuild_surface_targets(*surface, static_cast<int32_t>(LOWORD(lparam)),
                                         static_cast<int32_t>(HIWORD(lparam))))
                emit_surface_lost(*surface);
        });
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

bool ensure_surface_window_class() {
    if (surface_window_class)
        return true;
    WNDCLASSEXW definition{};
    definition.cbSize = sizeof(definition);
    definition.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    definition.lpfnWndProc = surface_window_proc;
    definition.hInstance = GetModuleHandleW(nullptr);
    definition.lpszClassName = surface_class_name;
    surface_window_class = RegisterClassExW(&definition);
    return surface_window_class != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool set_surface_native_bounds(WinSurfaceResource &surface) {
    auto parent = get_window(surface.parent);
    if (!parent)
        return false;
    const UINT dpi = query_window_dpi(parent->window);
    const int x = MulDiv(surface.x, static_cast<int>(dpi), 96);
    const int y = MulDiv(surface.y, static_cast<int>(dpi), 96);
    const int width = MulDiv(surface.width, static_cast<int>(dpi), 96);
    const int height = MulDiv(surface.height, static_cast<int>(dpi), 96);
    return SetWindowPos(surface.window, nullptr, x, y, width, height,
                        SWP_NOACTIVATE | SWP_NOZORDER) != 0;
}

bool surface_frame_available(const WinSurfaceResource &surface) {
    auto parent = get_window(surface.parent);
    if (!parent || IsIconic(parent->window) || !IsWindowVisible(parent->window) ||
        !IsWindowVisible(surface.window) || !surface.framebuffer_width ||
        !surface.framebuffer_height || !surface.swapchain)
        return false;
    return surface.swapchain->Present(0, DXGI_PRESENT_TEST) != DXGI_STATUS_OCCLUDED;
}

std::shared_ptr<WinCursorResource> get_cursor(nk_handle handle) {
    return std::dynamic_pointer_cast<WinCursorResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::cursor));
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
    event.kind = context.resources ? NK_EVENT_DIALOG_RESOURCES_COMPLETE
                                   : NK_EVENT_DIALOG_PATHS_COMPLETE;
    event.request_id = context.request;
    event.flags = context.kind;
    event.result = result;
    event.data_count = static_cast<uint32_t>(paths.size());
    if (context.resources) {
        const auto access = context.kind == NK_DIALOG_OPEN_RESOURCE
                                ? NK_RESOURCE_READABLE
                                : NK_RESOURCE_WRITABLE;
        std::vector<nk::platform::ResourceValue> resources;
        resources.reserve(paths.size());
        for (auto &path : paths)
            resources.push_back(nk::platform::resource_from_file_path(std::move(path), access));
        event.data = nk::platform::resource_payload(accepted, resources);
    } else {
        event.data = dialog_paths_payload(paths, accepted);
    }
    nk::core::push_event(std::move(event));
}

void run_file_dialog(const std::shared_ptr<WinDialogContext> &context) noexcept {
    context->thread_id = GetCurrentThreadId();
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileDialog *dialog = nullptr;
    HRESULT status = E_FAIL;
    if (SUCCEEDED(initialized)) {
        if (context->kind == NK_DIALOG_SAVE_FILE || context->kind == NK_DIALOG_SAVE_RESOURCE)
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
        if (context->kind == NK_DIALOG_SELECT_DIRECTORY ||
            context->kind == NK_DIALOG_SELECT_RESOURCE_DIRECTORY)
            options |= FOS_PICKFOLDERS;
        if ((context->kind == NK_DIALOG_OPEN_FILE || context->kind == NK_DIALOG_OPEN_RESOURCE) &&
            (context->flags & NK_DIALOG_ALLOW_MULTIPLE))
            options |= FOS_ALLOWMULTISELECT;
        if (context->flags & NK_DIALOG_SHOW_HIDDEN)
            options |= FOS_FORCESHOWHIDDEN;
        if ((context->kind == NK_DIALOG_SAVE_FILE || context->kind == NK_DIALOG_SAVE_RESOURCE) &&
            !(context->flags & NK_DIALOG_CONFIRM_OVERWRITE))
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
            if ((context->kind == NK_DIALOG_OPEN_FILE || context->kind == NK_DIALOG_OPEN_RESOURCE) &&
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
        event.kind = NK_EVENT_DIALOG_MESSAGE_COMPLETE;
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
                            nk_request_id *out_request, uint32_t kind, bool resources = false) {
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
    context->resources = resources;
    context->flags = options->flags;
    if (!copy_wide(options->title, context->title) ||
        !copy_wide(options->suggested_name, context->suggested_name))
        return fail(NK_ERROR_INVALID_ARGUMENT, "file dialog option is not valid UTF-8");
    std::string initial_path;
    if (options->initial_path && resources &&
        nk::platform::file_path_from_uri(options->initial_path, initial_path)) {
        context->initial_path = wide(initial_path.c_str());
        if (context->initial_path.empty())
            return fail(NK_ERROR_INVALID_ARGUMENT, "resource dialog initial URI is invalid");
    } else if (!copy_wide(options->initial_path, context->initial_path)) {
        return fail(NK_ERROR_INVALID_ARGUMENT, "file dialog option is not valid UTF-8");
    }
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

BOOL CALLBACK enumerate_monitors(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto *monitors = reinterpret_cast<std::vector<HMONITOR> *>(data);
    monitors->push_back(monitor);
    return TRUE;
}

std::vector<HMONITOR> connected_monitors() {
    std::vector<HMONITOR> result;
    EnumDisplayMonitors(nullptr, nullptr, enumerate_monitors,
                        reinterpret_cast<LPARAM>(&result));
    return result;
}

std::string monitor_name(HMONITOR native) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(native, &info))
        return "Unknown monitor";
    const auto result = utf8(info.szDevice);
    return result.empty() ? "Unknown monitor" : result;
}

nk_handle register_monitor(HMONITOR native) {
    const auto found = monitor_handles.find(native);
    if (found != monitor_handles.end())
        return found->second;
    auto resource = std::make_shared<WinMonitorResource>();
    resource->monitor = native;
    resource->name = monitor_name(native);
    resource->handle = nk::core::handles().insert(nk::core::ResourceType::monitor, resource);
    if (resource->handle != NK_INVALID_HANDLE)
        monitor_handles.emplace(native, resource->handle);
    return resource->handle;
}

nk_result refresh_monitors() {
    const auto native_monitors = connected_monitors();
    const bool had_monitors = !monitor_handles.empty();
    std::unordered_set<HMONITOR> current(native_monitors.begin(), native_monitors.end());
    for (const auto native : native_monitors) {
        if (monitor_handles.find(native) != monitor_handles.end())
            continue;
        const nk_handle handle = register_monitor(native);
        if (handle == NK_INVALID_HANDLE)
            return fail(NK_ERROR_OUT_OF_MEMORY, "monitor handle registry is full");
        if (had_monitors) {
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_MONITOR_CONNECTED;
            event.source = handle;
            nk::core::push_event(std::move(event));
        }
    }
    for (auto iterator = monitor_handles.begin(); iterator != monitor_handles.end();) {
        if (current.find(iterator->first) != current.end()) {
            ++iterator;
            continue;
        }
        const nk_handle handle = iterator->second;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_MONITOR_DISCONNECTED;
        event.source = handle;
        nk::core::push_event(std::move(event));
        nk::core::handles().erase(handle, nk::core::ResourceType::monitor);
        iterator = monitor_handles.erase(iterator);
    }
    return NK_OK;
}

UINT monitor_dpi() {
    const UINT dpi = GetDpiForSystem();
    return dpi ? dpi : 96;
}

bool monitor_info(HMONITOR native, MONITORINFOEXW &out_info) {
    out_info = {};
    out_info.cbSize = sizeof(out_info);
    return GetMonitorInfoW(native, &out_info) != FALSE;
}

nk_video_mode video_mode(const DEVMODEW &native) {
    nk_video_mode result{};
    result.struct_size = sizeof(result);
    result.width = static_cast<int32_t>(native.dmPelsWidth);
    result.height = static_cast<int32_t>(native.dmPelsHeight);
    result.refresh_rate = native.dmDisplayFrequency;
    result.red_bits = 0;
    result.green_bits = 0;
    result.blue_bits = 0;
    return result;
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
    nk::windows_joystick::pump();
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    reap_dialogs();
}

void shutdown() noexcept {
    nk::windows_joystick::shutdown();
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
    monitor_handles.clear();
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
    nk_capabilities capabilities =
        NK_CAP_WINDOW | NK_CAP_FILE_DIALOG | NK_CAP_CLIPBOARD | NK_CAP_DRAG_DROP | NK_CAP_SHELL |
        NK_CAP_SYSTEM_APPEARANCE | NK_CAP_EXPORT_NATIVE_WINDOW | NK_CAP_NOTIFICATION |
        NK_CAP_RESOURCE_SHARING | NK_CAP_RESOURCE_IO | NK_CAP_INPUT | NK_CAP_CURSOR |
        NK_CAP_POINTER_CAPTURE |
        NK_CAP_WINDOW_GEOMETRY | NK_CAP_WINDOW_STYLING | NK_CAP_D3D11_SURFACE |
        NK_CAP_ACCESSIBILITY | NK_CAP_MONITOR | NK_CAP_MONITOR_FULLSCREEN | NK_CAP_JOYSTICK;
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
        resource->resizable = (options->flags & NK_WINDOW_RESIZABLE) != 0;
        resource->decorated = (options->flags & NK_WINDOW_BORDERLESS) == 0;
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
    for (const auto surface_handle : resource->surfaces) {
        auto child_surface = get_surface(surface_handle);
        if (!child_surface)
            continue;
        if (child_surface->share_dependents ||
            nk_core_graphics_device_has_references(
                nk_graphics_device{child_surface->device_handle}))
            return fail(NK_ERROR_INVALID_REQUEST,
                        "window still owns a shared or retained graphics surface");
    }
    const auto owned_windows = resource->owned_windows;
    for (const auto owned : owned_windows)
        nk_window_destroy(owned);
    const auto surfaces = resource->surfaces;
    for (auto iter = surfaces.rbegin(); iter != surfaces.rend(); ++iter)
        nk_surface_destroy(*iter);
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

nk_result NK_CALL nk_window_get_content_scale(nk_handle handle,
                                              nk_window_content_scale *out_scale) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_scale || out_scale->struct_size < sizeof(*out_scale))
        return fail(NK_ERROR_INVALID_ARGUMENT, "content scale output is missing or too small");
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const auto size = out_scale->struct_size;
    const float scale = static_cast<float>(query_window_dpi(resource->window)) / 96.0f;
    *out_scale = {};
    out_scale->struct_size = size;
    out_scale->x = scale;
    out_scale->y = scale;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_position(nk_handle handle, int32_t *out_x, int32_t *out_y) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_x || !out_y)
        return fail(NK_ERROR_INVALID_ARGUMENT, "window position outputs must not be null");
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    RECT bounds{};
    if (!GetWindowRect(resource->window, &bounds))
        return fail(NK_ERROR_UNKNOWN, "could not query window position");
    const UINT dpi = query_window_dpi(resource->window);
    *out_x = MulDiv(bounds.left, 96, static_cast<int>(dpi));
    *out_y = MulDiv(bounds.top, 96, static_cast<int>(dpi));
    return NK_OK;
}

nk_result NK_CALL nk_window_get_size(nk_handle handle, int32_t *out_width, int32_t *out_height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_width || !out_height)
        return fail(NK_ERROR_INVALID_ARGUMENT, "window size outputs must not be null");
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    RECT client{};
    if (!GetClientRect(resource->window, &client))
        return fail(NK_ERROR_UNKNOWN, "could not query window size");
    const UINT dpi = query_window_dpi(resource->window);
    *out_width = MulDiv(client.right - client.left, 96, static_cast<int>(dpi));
    *out_height = MulDiv(client.bottom - client.top, 96, static_cast<int>(dpi));
    return NK_OK;
}

nk_result NK_CALL nk_window_get_framebuffer_size(nk_handle handle, int32_t *out_width,
                                                 int32_t *out_height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_width || !out_height)
        return fail(NK_ERROR_INVALID_ARGUMENT, "framebuffer size outputs must not be null");
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    RECT client{};
    if (!GetClientRect(resource->window, &client))
        return fail(NK_ERROR_UNKNOWN, "could not query framebuffer size");
    *out_width = client.right - client.left;
    *out_height = client.bottom - client.top;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_frame_extents(nk_handle handle,
                                              nk_window_frame_extents *out_extents) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_extents || out_extents->struct_size < sizeof(*out_extents))
        return fail(NK_ERROR_INVALID_ARGUMENT,
                    "window frame extents output is missing or too small");
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    RECT outer{};
    RECT client{};
    POINT client_origin{0, 0};
    if (!GetWindowRect(resource->window, &outer) || !GetClientRect(resource->window, &client) ||
        !ClientToScreen(resource->window, &client_origin))
        return fail(NK_ERROR_UNKNOWN, "could not query window frame extents");
    const int32_t left = client_origin.x - outer.left;
    const int32_t top = client_origin.y - outer.top;
    const int32_t right = outer.right - client_origin.x - (client.right - client.left);
    const int32_t bottom = outer.bottom - client_origin.y - (client.bottom - client.top);
    const UINT dpi = query_window_dpi(resource->window);
    const auto size = out_extents->struct_size;
    *out_extents = {};
    out_extents->struct_size = size;
    out_extents->left = MulDiv(left, 96, static_cast<int>(dpi));
    out_extents->top = MulDiv(top, 96, static_cast<int>(dpi));
    out_extents->right = MulDiv(right, 96, static_cast<int>(dpi));
    out_extents->bottom = MulDiv(bottom, 96, static_cast<int>(dpi));
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

nk_result NK_CALL nk_window_is_focused(nk_handle h, uint32_t *out_focused) {
    if (!out_focused)
        return fail(NK_ERROR_INVALID_ARGUMENT, "focus output must not be null");
    nk_window_state state{sizeof(state), 0, {0, 0}};
    const auto result = nk_window_get_state(h, &state);
    if (result == NK_OK)
        *out_focused = (state.flags & NK_WINDOW_STATE_ACTIVE) ? 1u : 0u;
    return result;
}

nk_result NK_CALL nk_window_is_visible(nk_handle h, uint32_t *out_visible) {
    if (!out_visible)
        return fail(NK_ERROR_INVALID_ARGUMENT, "visibility output must not be null");
    nk_window_state state{sizeof(state), 0, {0, 0}};
    const auto result = nk_window_get_state(h, &state);
    if (result == NK_OK)
        *out_visible = (state.flags & NK_WINDOW_STATE_VISIBLE) ? 1u : 0u;
    return result;
}

nk_result NK_CALL nk_key_get_state(nk_handle handle, nk_key key, nk_input_action *out_action) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_action || key == NK_KEY_UNKNOWN || key > NK_KEY_LAST)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid key state query");
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    *out_action = resource->keys[key];
    return NK_OK;
}

nk_result NK_CALL nk_pointer_button_get_state(nk_handle handle, nk_pointer_button button,
                                              nk_input_action *out_action) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_action || button > NK_POINTER_BUTTON_LAST)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid pointer button state query");
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    *out_action = resource->pointer_buttons[button];
    return NK_OK;
}

nk_result NK_CALL nk_pointer_get_position(nk_handle handle, double *out_x, double *out_y) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_x || !out_y)
        return fail(NK_ERROR_INVALID_ARGUMENT, "pointer position outputs must not be null");
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    *out_x = resource->pointer_x;
    *out_y = resource->pointer_y;
    return NK_OK;
}

nk_result NK_CALL nk_cursor_create_standard(nk_cursor_shape shape, nk_handle *out_cursor) {
    return nk::core::result_boundary(
        "unexpected error while creating standard cursor", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!out_cursor)
                return fail(NK_ERROR_INVALID_ARGUMENT, "cursor output must not be null");
            *out_cursor = NK_INVALID_HANDLE;
            LPCWSTR identifier = nullptr;
            switch (shape) {
            case NK_CURSOR_ARROW:
                identifier = IDC_ARROW;
                break;
            case NK_CURSOR_IBEAM:
                identifier = IDC_IBEAM;
                break;
            case NK_CURSOR_CROSSHAIR:
                identifier = IDC_CROSS;
                break;
            case NK_CURSOR_HAND:
                identifier = IDC_HAND;
                break;
            case NK_CURSOR_HORIZONTAL_RESIZE:
                identifier = IDC_SIZEWE;
                break;
            case NK_CURSOR_VERTICAL_RESIZE:
                identifier = IDC_SIZENS;
                break;
            case NK_CURSOR_NWSE_RESIZE:
                identifier = IDC_SIZENWSE;
                break;
            case NK_CURSOR_NESW_RESIZE:
                identifier = IDC_SIZENESW;
                break;
            case NK_CURSOR_MOVE:
                identifier = IDC_SIZEALL;
                break;
            case NK_CURSOR_NOT_ALLOWED:
                identifier = IDC_NO;
                break;
            default:
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid standard cursor shape");
            }
            auto resource = std::make_shared<WinCursorResource>();
            resource->cursor = LoadCursorW(nullptr, identifier);
            if (!resource->cursor)
                return fail(NK_ERROR_UNSUPPORTED, "Windows does not provide the requested cursor");
            resource->handle = nk::core::handles().insert(nk::core::ResourceType::cursor, resource);
            if (resource->handle == NK_INVALID_HANDLE)
                return fail(NK_ERROR_OUT_OF_MEMORY, "cursor handle registry is full");
            *out_cursor = resource->handle;
            return NK_OK;
        });
}

nk_result NK_CALL nk_cursor_create_custom(const nk_cursor_image *image, nk_handle *out_cursor) {
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!image || image->struct_size < sizeof(*image) || !out_cursor || !image->rgba ||
            image->width <= 0 || image->height <= 0 || image->width > INT_MAX / 4 ||
            image->stride < image->width * 4 || image->hotspot_x < 0 || image->hotspot_y < 0 ||
            image->hotspot_x >= image->width || image->hotspot_y >= image->height)
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid custom cursor image");
        *out_cursor = NK_INVALID_HANDLE;
        BITMAPV5HEADER header{};
        header.bV5Size = sizeof(header);
        header.bV5Width = image->width;
        header.bV5Height = -image->height;
        header.bV5Planes = 1;
        header.bV5BitCount = 32;
        header.bV5Compression = BI_BITFIELDS;
        header.bV5RedMask = 0x00ff0000;
        header.bV5GreenMask = 0x0000ff00;
        header.bV5BlueMask = 0x000000ff;
        header.bV5AlphaMask = 0xff000000;
        void *pixels = nullptr;
        HDC device = GetDC(nullptr);
        HBITMAP color = CreateDIBSection(device, reinterpret_cast<BITMAPINFO *>(&header),
                                         DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (device)
            ReleaseDC(nullptr, device);
        if (!color || !pixels) {
            if (color)
                DeleteObject(color);
            return fail(NK_ERROR_OUT_OF_MEMORY, "could not allocate custom cursor pixels");
        }
        const auto *source = static_cast<const uint8_t *>(image->rgba);
        auto *destination = static_cast<uint8_t *>(pixels);
        for (int32_t y = 0; y < image->height; ++y) {
            const auto *source_row = source + static_cast<std::size_t>(y) * image->stride;
            auto *destination_row = destination + static_cast<std::size_t>(y) * image->width * 4;
            for (int32_t x = 0; x < image->width; ++x) {
                destination_row[x * 4 + 0] = source_row[x * 4 + 2];
                destination_row[x * 4 + 1] = source_row[x * 4 + 1];
                destination_row[x * 4 + 2] = source_row[x * 4 + 0];
                destination_row[x * 4 + 3] = source_row[x * 4 + 3];
            }
        }
        HBITMAP mask = CreateBitmap(image->width, image->height, 1, 1, nullptr);
        if (!mask) {
            DeleteObject(color);
            return fail(NK_ERROR_OUT_OF_MEMORY, "could not allocate custom cursor mask");
        }
        ICONINFO info{};
        info.fIcon = FALSE;
        info.xHotspot = static_cast<DWORD>(image->hotspot_x);
        info.yHotspot = static_cast<DWORD>(image->hotspot_y);
        info.hbmMask = mask;
        info.hbmColor = color;
        HCURSOR native = static_cast<HCURSOR>(CreateIconIndirect(&info));
        DeleteObject(mask);
        DeleteObject(color);
        if (!native)
            return fail(NK_ERROR_UNSUPPORTED, "Windows could not create the custom cursor");
        auto resource = std::make_shared<WinCursorResource>();
        resource->cursor = native;
        resource->owned = true;
        resource->handle = nk::core::handles().insert(nk::core::ResourceType::cursor, resource);
        if (resource->handle == NK_INVALID_HANDLE)
            return fail(NK_ERROR_OUT_OF_MEMORY, "cursor handle registry is full");
        *out_cursor = resource->handle;
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while creating custom cursor");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while creating custom cursor");
    }
}

nk_result NK_CALL nk_cursor_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!get_cursor(handle))
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale cursor handle");
    nk::core::handles().erase(handle, nk::core::ResourceType::cursor);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_cursor(nk_handle window_handle, nk_handle cursor_handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = get_window(window_handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    auto selected = cursor_handle == NK_INVALID_HANDLE ? nullptr : get_cursor(cursor_handle);
    if (cursor_handle != NK_INVALID_HANDLE && !selected)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale cursor handle");
    resource->cursor = std::move(selected);
    apply_cursor(*resource);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_cursor_mode(nk_handle handle, nk_cursor_mode mode) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (mode > NK_CURSOR_MODE_DISABLED)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid cursor mode");
    if (mode == NK_CURSOR_MODE_DISABLED)
        return fail(NK_ERROR_UNSUPPORTED, "Windows backend does not provide raw relative motion");
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    if (mode == NK_CURSOR_MODE_CAPTURED) {
        SetCapture(resource->window);
        if (GetCapture() != resource->window)
            return fail(NK_ERROR_UNSUPPORTED, "Windows could not capture the pointer");
        resource->pointer_captured = true;
    } else if (resource->pointer_captured) {
        resource->pointer_captured = false;
        if (GetCapture() == resource->window)
            ReleaseCapture();
    }
    resource->cursor_mode = mode;
    apply_cursor(*resource);
    return NK_OK;
}

nk_result NK_CALL nk_window_get_cursor_mode(nk_handle handle, nk_cursor_mode *out_mode) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_mode)
        return fail(NK_ERROR_INVALID_ARGUMENT, "cursor mode output must not be null");
    auto resource = get_window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    *out_mode = resource->cursor_mode;
    return NK_OK;
}

uint32_t NK_CALL nk_raw_pointer_motion_supported(void) {
    return 0;
}

nk_result NK_CALL nk_surface_set_text_input_state(nk_handle handle,
                                                  const nk_text_input_state *state) {
    return nk::core::result_boundary(
        "unexpected error while setting text input state", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!state || state->struct_size < sizeof(*state) || !state->text)
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid text input state");
            std::vector<uint32_t> codepoints;
            const std::string_view text(state->text);
            if (!decode_utf8(text, &codepoints))
                return fail(NK_ERROR_INVALID_ARGUMENT, "text input state text is not valid UTF-8");
            const uint64_t text_end = static_cast<uint64_t>(state->text_start) + codepoints.size();
            const bool no_composition = state->composition_start == NK_TEXT_POSITION_NONE &&
                                        state->composition_end == NK_TEXT_POSITION_NONE;
            const bool valid_composition = state->composition_start != NK_TEXT_POSITION_NONE &&
                                           state->composition_end != NK_TEXT_POSITION_NONE &&
                                           state->composition_start <= state->composition_end &&
                                           state->composition_start >= state->text_start &&
                                           state->composition_end <= text_end;
            const bool valid_cursor =
                std::isfinite(state->cursor_x) && std::isfinite(state->cursor_y) &&
                std::isfinite(state->cursor_width) && std::isfinite(state->cursor_height) &&
                state->cursor_width >= 0.f && state->cursor_height >= 0.f;
            if ((state->flags & ~(NK_TEXT_INPUT_MULTILINE | NK_TEXT_INPUT_AUTOCORRECT |
                                  NK_TEXT_INPUT_CAPITALIZE_SENTENCES)) ||
                state->text_start > state->document_length || text_end > state->document_length ||
                state->selection_start > state->selection_end ||
                state->selection_start < state->text_start || state->selection_end > text_end ||
                (!no_composition && !valid_composition) ||
                state->input_type > NK_TEXT_INPUT_PASSWORD ||
                state->action > NK_TEXT_INPUT_ACTION_NONE || !valid_cursor)
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "text input state ranges or hints are invalid");
            auto resource = get_window(handle);
            if (!resource)
                return fail(NK_ERROR_INVALID_HANDLE,
                            "text input state requires a desktop window on this backend");
            resource->text_input_text = text;
            resource->text_input_state = *state;
            resource->text_input_state.text = resource->text_input_text.c_str();
            resource->text_composition_start = state->composition_start;
            resource->text_composition_end = state->composition_end;
            resource->text_composing = state->composition_start != NK_TEXT_POSITION_NONE;
            HIMC context = ImmGetContext(resource->window);
            if (context) {
                const auto scale = dpi_scale(resource->window);
                COMPOSITIONFORM composition{};
                composition.dwStyle = CFS_POINT;
                composition.ptCurrentPos.x =
                    static_cast<LONG>(std::lround(state->cursor_x * scale));
                composition.ptCurrentPos.y =
                    static_cast<LONG>(std::lround(state->cursor_y * scale));
                ImmSetCompositionWindow(context, &composition);
                CANDIDATEFORM candidate{};
                candidate.dwIndex = 0;
                candidate.dwStyle = CFS_CANDIDATEPOS;
                candidate.ptCurrentPos = composition.ptCurrentPos;
                ImmSetCandidateWindow(context, &candidate);
                ImmReleaseContext(resource->window, context);
            }
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_set_text_input_active(nk_handle handle, uint32_t active) {
    return nk::core::result_boundary(
        "unexpected error while changing text input", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (active > 1)
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "text input active state must be zero or one");
            auto resource = get_window(handle);
            if (!resource)
                return fail(NK_ERROR_INVALID_HANDLE,
                            "text input activation requires a desktop window on this backend");
            resource->text_input_active = active != 0;
            if (!resource->text_input_active) {
                HIMC context = ImmGetContext(resource->window);
                if (context) {
                    ImmNotifyIME(context, NI_COMPOSITIONSTR, CPS_CANCEL, 0);
                    ImmReleaseContext(resource->window, context);
                }
                finish_text_composition(*resource);
            }
            return NK_OK;
        });
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

nk_result NK_CALL nk_monitor_list(nk_handle *monitors, uint32_t *inout_count) {
    return nk::core::result_boundary(
        "unexpected error while enumerating monitors", [&]() -> nk_result {
            if (const auto r = enter_ui(); r != NK_OK)
                return r;
            if (!inout_count)
                return fail(NK_ERROR_INVALID_ARGUMENT, "monitor count must not be null");
            if (const auto r = refresh_monitors(); r != NK_OK)
                return r;
            const uint32_t required = static_cast<uint32_t>(monitor_handles.size());
            const uint32_t capacity = *inout_count;
            *inout_count = required;
            if (!monitors || capacity < required)
                return required ? fail(NK_ERROR_BUFFER_TOO_SMALL,
                                       "monitor handle buffer is too small")
                                 : NK_OK;
            uint32_t index = 0;
            for (const auto native : connected_monitors())
                monitors[index++] = monitor_handles.at(native);
            return NK_OK;
        });
}

nk_result NK_CALL nk_monitor_get_primary(nk_handle *out_monitor) {
    return nk::core::result_boundary(
        "unexpected error while finding primary monitor", [&]() -> nk_result {
            if (const auto r = enter_ui(); r != NK_OK)
                return r;
            if (!out_monitor)
                return fail(NK_ERROR_INVALID_ARGUMENT, "monitor output must not be null");
            *out_monitor = NK_INVALID_HANDLE;
            if (const auto r = refresh_monitors(); r != NK_OK)
                return r;
            for (const auto native : connected_monitors()) {
                MONITORINFOEXW info{};
                if (monitor_info(native, info) && (info.dwFlags & MONITORINFOF_PRIMARY)) {
                    *out_monitor = monitor_handles.at(native);
                    return NK_OK;
                }
            }
            return fail(NK_ERROR_UNSUPPORTED, "Windows reports no connected primary monitor");
        });
}

nk_result NK_CALL nk_monitor_get_name(nk_handle handle, char *buffer, uint32_t *inout_size) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto resource = get_monitor(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale monitor handle");
    return copy_utf8_output(resource->name, buffer, inout_size);
}

nk_result NK_CALL nk_monitor_get_geometry(nk_handle handle, nk_monitor_geometry *out_geometry) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!out_geometry || out_geometry->struct_size < sizeof(*out_geometry))
        return fail(NK_ERROR_INVALID_ARGUMENT, "monitor geometry output is missing or too small");
    auto resource = get_monitor(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale monitor handle");
    MONITORINFOEXW info{};
    if (!monitor_info(resource->monitor, info))
        return fail(NK_ERROR_INVALID_HANDLE, "monitor is no longer connected");
    const UINT dpi = monitor_dpi();
    const auto size = out_geometry->struct_size;
    *out_geometry = {};
    out_geometry->struct_size = size;
    out_geometry->x = MulDiv(info.rcMonitor.left, 96, static_cast<int>(dpi));
    out_geometry->y = MulDiv(info.rcMonitor.top, 96, static_cast<int>(dpi));
    out_geometry->width = MulDiv(info.rcMonitor.right - info.rcMonitor.left, 96,
                                 static_cast<int>(dpi));
    out_geometry->height = MulDiv(info.rcMonitor.bottom - info.rcMonitor.top, 96,
                                  static_cast<int>(dpi));
    out_geometry->work_x = MulDiv(info.rcWork.left, 96, static_cast<int>(dpi));
    out_geometry->work_y = MulDiv(info.rcWork.top, 96, static_cast<int>(dpi));
    out_geometry->work_width = MulDiv(info.rcWork.right - info.rcWork.left, 96,
                                      static_cast<int>(dpi));
    out_geometry->work_height = MulDiv(info.rcWork.bottom - info.rcWork.top, 96,
                                       static_cast<int>(dpi));
    HDC device = CreateDCW(info.szDevice, info.szDevice, nullptr, nullptr);
    if (device) {
        out_geometry->width_mm = GetDeviceCaps(device, HORZSIZE);
        out_geometry->height_mm = GetDeviceCaps(device, VERTSIZE);
        DeleteDC(device);
    }
    out_geometry->scale_x = static_cast<float>(dpi) / 96.0f;
    out_geometry->scale_y = out_geometry->scale_x;
    return NK_OK;
}

nk_result NK_CALL nk_monitor_get_current_mode(nk_handle handle, nk_video_mode *out_mode) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!out_mode || out_mode->struct_size < sizeof(*out_mode))
        return fail(NK_ERROR_INVALID_ARGUMENT, "video mode output is missing or too small");
    auto resource = get_monitor(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale monitor handle");
    MONITORINFOEXW info{};
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!monitor_info(resource->monitor, info) ||
        !EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode))
        return fail(NK_ERROR_UNSUPPORTED, "current monitor mode is unavailable");
    const auto size = out_mode->struct_size;
    *out_mode = video_mode(mode);
    out_mode->struct_size = size;
    return NK_OK;
}

nk_result NK_CALL nk_monitor_get_modes(nk_handle handle, nk_video_mode *modes,
                                       uint32_t *inout_count) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!inout_count)
        return fail(NK_ERROR_INVALID_ARGUMENT, "video mode count must not be null");
    auto resource = get_monitor(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale monitor handle");
    MONITORINFOEXW info{};
    if (!monitor_info(resource->monitor, info))
        return fail(NK_ERROR_INVALID_HANDLE, "monitor is no longer connected");
    std::vector<nk_video_mode> available;
    for (DWORD index = 0;; ++index) {
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsW(info.szDevice, index, &mode))
            break;
        available.push_back(video_mode(mode));
    }
    const uint32_t required = static_cast<uint32_t>(available.size());
    const uint32_t capacity = *inout_count;
    *inout_count = required;
    if (!modes || capacity < required)
        return required ? fail(NK_ERROR_BUFFER_TOO_SMALL, "video mode buffer is too small") : NK_OK;
    std::copy(available.begin(), available.end(), modes);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_fullscreen_monitor(nk_handle window_handle,
                                                   nk_handle monitor_handle) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto window = get_window(window_handle);
    if (!window)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    if (monitor_handle == NK_INVALID_HANDLE)
        return nk_window_set_fullscreen(window_handle, 0);
    auto selected = get_monitor(monitor_handle);
    if (!selected)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale monitor handle");
    MONITORINFOEXW info{};
    if (!monitor_info(selected->monitor, info))
        return fail(NK_ERROR_INVALID_HANDLE, "monitor is no longer connected");
    if (!window->fullscreen) {
        window->placement.length = sizeof(window->placement);
        if (!GetWindowPlacement(window->window, &window->placement))
            return fail(NK_ERROR_UNKNOWN, "could not save window placement");
        window->windowed_style = GetWindowLongPtrW(window->window, GWL_STYLE);
    }
    SetWindowLongPtrW(window->window, GWL_STYLE, window->windowed_style & ~WS_OVERLAPPEDWINDOW);
    if (!SetWindowPos(window->window, HWND_TOP, info.rcMonitor.left, info.rcMonitor.top,
                      info.rcMonitor.right - info.rcMonitor.left,
                      info.rcMonitor.bottom - info.rcMonitor.top,
                      SWP_FRAMECHANGED | SWP_NOOWNERZORDER))
        return fail(NK_ERROR_UNKNOWN, "could not enter fullscreen on monitor");
    window->fullscreen = true;
    return NK_OK;
}

nk_result NK_CALL nk_window_set_aspect_ratio(nk_handle h, int32_t numerator,
                                              int32_t denominator) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if ((numerator == 0) != (denominator == 0) || numerator < 0 || denominator < 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid window aspect ratio");
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    w->aspect_numerator = numerator;
    w->aspect_denominator = denominator;
    return NK_OK;
}

nk_result NK_CALL nk_window_set_resizable(nk_handle h, uint32_t enabled) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    LONG_PTR style = GetWindowLongPtrW(w->window, GWL_STYLE);
    if (enabled)
        style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    else
        style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    SetWindowLongPtrW(w->window, GWL_STYLE, style);
    if (!SetWindowPos(w->window, nullptr, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                          SWP_FRAMECHANGED))
        return fail(NK_ERROR_UNKNOWN, "could not change window resizable state");
    w->resizable = enabled != 0;
    return NK_OK;
}

nk_result NK_CALL nk_window_set_decorated(nk_handle h, uint32_t enabled) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    constexpr LONG_PTR decoration_style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX |
                                          WS_MAXIMIZEBOX | WS_THICKFRAME;
    LONG_PTR style = GetWindowLongPtrW(w->window, GWL_STYLE);
    if (enabled) {
        style |= WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        if (w->resizable)
            style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    } else {
        style &= ~decoration_style;
    }
    SetWindowLongPtrW(w->window, GWL_STYLE, style);
    if (!SetWindowPos(w->window, nullptr, 0, 0, 0, 0,
                      SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                          SWP_FRAMECHANGED))
        return fail(NK_ERROR_UNKNOWN, "could not change window decoration state");
    w->decorated = enabled != 0;
    return NK_OK;
}

nk_result NK_CALL nk_window_set_floating(nk_handle h, uint32_t enabled) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    return SetWindowPos(w->window, enabled ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)
               ? NK_OK
               : fail(NK_ERROR_UNKNOWN, "could not change window floating state");
}

nk_result NK_CALL nk_window_set_opacity(nk_handle h, float opacity) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!(opacity >= 0.0f && opacity <= 1.0f))
        return fail(NK_ERROR_INVALID_ARGUMENT, "window opacity must be between zero and one");
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const LONG_PTR extended_style = GetWindowLongPtrW(w->window, GWL_EXSTYLE);
    if (!(extended_style & WS_EX_LAYERED))
        SetWindowLongPtrW(w->window, GWL_EXSTYLE, extended_style | WS_EX_LAYERED);
    return SetLayeredWindowAttributes(w->window, 0, static_cast<BYTE>(std::lround(opacity * 255.0f)),
                                      LWA_ALPHA)
               ? NK_OK
               : fail(NK_ERROR_UNKNOWN, "could not change window opacity");
}

nk_result NK_CALL nk_window_set_mouse_passthrough(nk_handle h, uint32_t enabled) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    w->mouse_passthrough = enabled != 0;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_hovered(nk_handle h, uint32_t *out_hovered) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!out_hovered)
        return fail(NK_ERROR_INVALID_ARGUMENT, "hovered output must not be null");
    auto w = get_window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    *out_hovered = w->pointer_tracking ? 1u : 0u;
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

nk_result NK_CALL nk_surface_create(nk_handle parent_handle, const nk_surface_options *options,
                                    nk_handle *out_surface) {
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        constexpr nk_surface_flags supported_flags = NK_SURFACE_HIDDEN | NK_SURFACE_ALPHA |
                                                     NK_SURFACE_DEPTH | NK_SURFACE_STENCIL |
                                                     NK_SURFACE_DEBUG_CONTEXT;
        if (!options || options->struct_size < sizeof(*options) || !out_surface ||
            options->width <= 0 || options->height <= 0 || options->api != NK_GRAPHICS_D3D11 ||
            (options->flags & ~supported_flags) != 0)
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid Direct3D surface options");
        *out_surface = NK_INVALID_HANDLE;
        auto parent = get_window(parent_handle);
        if (!parent)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
        auto shared = options->share_surface ? get_surface(options->share_surface) : nullptr;
        if (options->share_surface && !shared)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid shared graphics surface");
        if (shared && shared->api != NK_GRAPHICS_D3D11)
            return fail(NK_ERROR_INVALID_ARGUMENT,
                        "shared surfaces must use the same graphics API");
        if (shared && shared->share_dependents == UINT32_MAX)
            return fail(NK_ERROR_INVALID_REQUEST, "graphics surface has too many dependents");
        if (!ensure_surface_window_class())
            return fail(NK_ERROR_UNKNOWN, "could not register the Direct3D surface window class");
        parent->surfaces.reserve(parent->surfaces.size() + 1);
        auto resource = std::make_shared<WinSurfaceResource>();
        resource->parent = parent_handle;
        resource->flags = options->flags;
        resource->x = options->x;
        resource->y = options->y;
        resource->width = options->width;
        resource->height = options->height;
        resource->shared_surface = shared;
        if (shared) {
            resource->device = shared->device;
            resource->context = shared->context;
            resource->device_handle = shared->device_handle;
        } else if (!create_d3d11_device(*resource)) {
            return NK_ERROR_UNSUPPORTED;
        }

        const UINT dpi = query_window_dpi(parent->window);
        const int x = MulDiv(resource->x, static_cast<int>(dpi), 96);
        const int y = MulDiv(resource->y, static_cast<int>(dpi), 96);
        const int width = MulDiv(resource->width, static_cast<int>(dpi), 96);
        const int height = MulDiv(resource->height, static_cast<int>(dpi), 96);
        const DWORD style = WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS |
                            ((options->flags & NK_SURFACE_HIDDEN) ? 0 : WS_VISIBLE);
        resource->window =
            CreateWindowExW(WS_EX_NOACTIVATE, surface_class_name, L"", style, x, y, width, height,
                            parent->window, nullptr, GetModuleHandleW(nullptr), resource.get());
        if (!resource->window)
            return fail(NK_ERROR_UNKNOWN, "could not create the Direct3D child surface");
        resource->handle = nk::core::handles().insert(nk::core::ResourceType::surface, resource);
        if (resource->handle == NK_INVALID_HANDLE)
            return fail(NK_ERROR_OUT_OF_MEMORY, "graphics surface handle registry is full");
        if (!resource->device_handle)
            resource->device_handle = resource->handle;
        parent->surfaces.push_back(resource->handle);
        if (shared)
            ++shared->share_dependents;
        if (!rebuild_surface_targets(*resource, width, height)) {
            nk_surface_destroy(resource->handle);
            return fail(NK_ERROR_UNSUPPORTED, "could not initialize the Direct3D surface targets");
        }
        resource->ready = true;
        if (nk::windows::accessibility_attach(resource->window, resource->handle) != NK_OK) {
            nk_surface_destroy(resource->handle);
            return fail(NK_ERROR_OUT_OF_MEMORY,
                        "could not initialize Direct3D surface accessibility");
        }
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_SURFACE_READY;
        event.source = resource->handle;
        nk::core::push_event(std::move(event));
        *out_surface = resource->handle;
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while creating Direct3D surface");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while creating Direct3D surface");
    }
}

nk_result NK_CALL nk_surface_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = get_surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    if (resource->share_dependents)
        return fail(NK_ERROR_INVALID_REQUEST,
                    "graphics surface is still shared by another surface");
    if (nk_core_graphics_device_has_references(nk_graphics_device{resource->device_handle}))
        return fail(NK_ERROR_INVALID_REQUEST, "graphics surface still owns retained GPU resources");
    resource->destroying = true;
    resource->frame_prepared = false;
    resource->context->OMSetRenderTargets(0, nullptr, nullptr);
    resource->render_target.Reset();
    resource->depth_stencil_target.Reset();
    resource->depth_texture.Reset();
    resource->swapchain.Reset();
    nk::windows::accessibility_detach(resource->window);
    if (resource->window && IsWindow(resource->window))
        DestroyWindow(resource->window);
    resource->window = nullptr;
    if (auto parent = get_window(resource->parent)) {
        auto &surfaces = parent->surfaces;
        surfaces.erase(std::remove(surfaces.begin(), surfaces.end(), handle), surfaces.end());
    }
    if (resource->shared_surface)
        --resource->shared_surface->share_dependents;
    nk::core::handles().erase(handle, nk::core::ResourceType::surface);
    return NK_OK;
}

nk_result NK_CALL nk_surface_show(nk_handle handle, uint32_t visible) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (visible > 1)
        return fail(NK_ERROR_INVALID_ARGUMENT, "surface visibility must be zero or one");
    auto resource = get_surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    ShowWindow(resource->window, visible ? SW_SHOWNA : SW_HIDE);
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                        int32_t height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (width <= 0 || height <= 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "graphics surface dimensions must be positive");
    auto resource = get_surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    resource->x = x;
    resource->y = y;
    resource->width = width;
    resource->height = height;
    return set_surface_native_bounds(*resource)
               ? NK_OK
               : fail(NK_ERROR_UNKNOWN, "could not resize the Direct3D child surface");
}

nk_result NK_CALL nk_surface_make_current(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = get_surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    if (resource->lost_reported || !resource->device ||
        FAILED(resource->device->GetDeviceRemovedReason())) {
        emit_surface_lost(*resource);
        return fail(NK_ERROR_INVALID_REQUEST, "Direct3D surface device is unavailable");
    }
    if (resource->frame_prepared)
        return NK_OK;
    if (!surface_frame_available(*resource))
        return fail(NK_ERROR_INVALID_REQUEST, "Direct3D surface has no drawable frame");
    resource->frame_prepared = true;
    return NK_OK;
}

nk_result NK_CALL nk_surface_present(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = get_surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    if (!resource->frame_prepared)
        return fail(NK_ERROR_INVALID_REQUEST, "Direct3D surface has no prepared frame");
    resource->frame_prepared = false;
    const HRESULT result = resource->swapchain->Present(1, 0);
    if (result == DXGI_STATUS_OCCLUDED)
        return NK_OK;
    if (FAILED(result)) {
        if (d3d11_device_failure(*resource, result))
            emit_surface_lost(*resource);
        return fail(NK_ERROR_UNKNOWN, "could not present the Direct3D surface");
    }
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_frame_callback(nk_handle handle,
                                                nk_surface_frame_callback callback,
                                                void *user_data) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = get_surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    resource->frame_callback = callback;
    resource->frame_user_data = callback ? user_data : nullptr;
    if (callback)
        SetTimer(resource->window, surface_frame_timer, 16, nullptr);
    else
        KillTimer(resource->window, surface_frame_timer);
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_framebuffer_size(nk_handle handle, int32_t *out_width,
                                                  int32_t *out_height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_width || !out_height)
        return fail(NK_ERROR_INVALID_ARGUMENT, "framebuffer size outputs must not be null");
    auto resource = get_surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    if (surface_frame_available(*resource)) {
        *out_width = resource->framebuffer_width;
        *out_height = resource->framebuffer_height;
    } else {
        *out_width = 0;
        *out_height = 0;
    }
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_frame_target(nk_handle handle,
                                              nk_surface_frame_target *out_target) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!nk::core::surface_frame_target_output_valid(out_target))
        return fail(NK_ERROR_INVALID_ARGUMENT, "frame-target output is missing or too small");
    auto resource = get_surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    nk_surface_frame_target target{};
    target.struct_size = out_target->struct_size;
    target.api = NK_GRAPHICS_D3D11;
    const bool prepared = resource->frame_prepared;
    target.width = prepared ? resource->framebuffer_width : 0;
    target.height = prepared ? resource->framebuffer_height : 0;
    target.native_target =
        prepared && resource->render_target
            ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(resource->render_target.Get()))
            : 0;
    target.device.id = resource->device_handle;
    target.native_device =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(resource->device.Get()));
    target.native_context =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(resource->context.Get()));
    target.native_depth_stencil_target =
        static_cast<uint64_t>(reinterpret_cast<uintptr_t>(resource->depth_stencil_target.Get()));
    target.native_present_target =
        prepared ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(resource->swapchain.Get()))
                 : 0;
    nk::core::write_surface_frame_target(out_target, target);
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_proc_address(nk_handle handle, const char *name,
                                              nk_graphics_proc *out_proc) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!name || !*name || !out_proc)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid graphics procedure query");
    *out_proc = nullptr;
    if (!get_surface(handle))
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    return fail(NK_ERROR_UNSUPPORTED, "Direct3D surfaces do not expose GL procedure addresses");
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

static nk_result webview_history_query(nk_handle handle, bool forward, uint32_t *out_value) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = get_webview(handle);
    if (!resource || !out_value)
        return fail(!resource ? NK_ERROR_INVALID_HANDLE : NK_ERROR_INVALID_ARGUMENT,
                    !resource ? "invalid or stale WebView handle" : "history output is null");
    if (!resource->webview) {
        if (resource->failed)
            return fail(NK_ERROR_UNKNOWN, "WebView2 initialization failed");
        *out_value = 0;
        return NK_OK;
    }
    BOOL value = FALSE;
    const auto status = forward ? resource->webview->get_CanGoForward(&value)
                                : resource->webview->get_CanGoBack(&value);
    if (FAILED(status))
        return fail(NK_ERROR_UNKNOWN, "WebView2 history query failed");
    *out_value = value ? 1u : 0u;
    return NK_OK;
}

static nk_result webview_history_command(nk_handle handle, uint32_t command) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = get_webview(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    if (!resource->webview)
        return fail(resource->failed ? NK_ERROR_UNKNOWN : NK_ERROR_UNSUPPORTED,
                    resource->failed ? "WebView2 initialization failed"
                                     : "WebView history is unavailable before readiness");
    HRESULT status = E_INVALIDARG;
    switch (command) {
    case 0:
        status = resource->webview->GoBack();
        break;
    case 1:
        status = resource->webview->GoForward();
        break;
    case 2:
        status = resource->webview->Reload();
        break;
    case 3:
        status = resource->webview->Stop();
        break;
    }
    return SUCCEEDED(status) ? NK_OK : fail(NK_ERROR_UNKNOWN, "WebView2 history command failed");
}
nk_result NK_CALL nk_webview_can_go_back(nk_handle handle, uint32_t *out_can_go_back) {
    return webview_history_query(handle, false, out_can_go_back);
}
nk_result NK_CALL nk_webview_can_go_forward(nk_handle handle, uint32_t *out_can_go_forward) {
    return webview_history_query(handle, true, out_can_go_forward);
}
nk_result NK_CALL nk_webview_go_back(nk_handle handle) {
    return webview_history_command(handle, 0);
}
nk_result NK_CALL nk_webview_go_forward(nk_handle handle) {
    return webview_history_command(handle, 1);
}
nk_result NK_CALL nk_webview_reload(nk_handle handle) {
    return webview_history_command(handle, 2);
}
nk_result NK_CALL nk_webview_stop(nk_handle handle) {
    return webview_history_command(handle, 3);
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
nk_result NK_CALL nk_webview_can_go_back(nk_handle, uint32_t *) {
    return unsupported();
}
nk_result NK_CALL nk_webview_can_go_forward(nk_handle, uint32_t *) {
    return unsupported();
}
nk_result NK_CALL nk_webview_go_back(nk_handle) {
    return unsupported();
}
nk_result NK_CALL nk_webview_go_forward(nk_handle) {
    return unsupported();
}
nk_result NK_CALL nk_webview_reload(nk_handle) {
    return unsupported();
}
nk_result NK_CALL nk_webview_stop(nk_handle) {
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

nk_result NK_CALL nk_clipboard_set_resources(const nk_resource *resources,
                                             uint32_t resource_count) {
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (const auto result = nk::platform::validate_resources(resources, resource_count, false);
            result != NK_OK)
            return result;
        std::string uri_list;
        for (uint32_t index = 0; index < resource_count; ++index) {
            uri_list += resources[index].uri;
            uri_list += "\r\n";
        }
        auto memory = clipboard_bytes(uri_list);
        if (!memory)
            return fail(NK_ERROR_OUT_OF_MEMORY, "could not allocate resource clipboard data");
        if (!open_clipboard()) {
            GlobalFree(memory);
            return fail(NK_ERROR_UNKNOWN, "clipboard is busy");
        }
        EmptyClipboard();
        if (!SetClipboardData(resource_clipboard_format(), memory)) {
            CloseClipboard();
            GlobalFree(memory);
            return fail(NK_ERROR_UNKNOWN, "Windows rejected resource clipboard data");
        }
        CloseClipboard();
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while writing resource clipboard");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while writing resource clipboard");
    }
}

nk_result NK_CALL nk_clipboard_read_resources(nk_request_id *out_request) {
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!out_request)
            return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard request output is null");
        *out_request = NK_INVALID_REQUEST_ID;
        std::vector<nk::platform::ResourceValue> resources;
        if (IsClipboardFormatAvailable(resource_clipboard_format())) {
            if (!open_clipboard())
                return fail(NK_ERROR_UNKNOWN, "clipboard is busy");
            ClipboardCloseGuard close;
            HANDLE memory = GetClipboardData(resource_clipboard_format());
            const auto *value = memory ? static_cast<const char *>(GlobalLock(memory)) : nullptr;
            if (!memory || !value)
                return fail(NK_ERROR_UNKNOWN, "could not read resource clipboard");
            GlobalUnlockGuard unlock{static_cast<HGLOBAL>(memory)};
            const std::string list(value);
            std::size_t start = 0;
            while (start < list.size()) {
                const auto end = list.find('\n', start);
                const auto line_end = end == std::string::npos ? list.size() : end;
                std::string uri = list.substr(start, line_end - start);
                if (!uri.empty() && uri.back() == '\r')
                    uri.pop_back();
                if (!uri.empty() && nk::platform::valid_utf8(uri) && uri.find(':') != std::string::npos)
                    resources.push_back(nk::platform::resource_from_uri(
                        std::move(uri), NK_RESOURCE_READABLE));
                start = end == std::string::npos ? list.size() : end + 1;
            }
        } else if (IsClipboardFormatAvailable(CF_HDROP)) {
            if (!open_clipboard())
                return fail(NK_ERROR_UNKNOWN, "clipboard is busy");
            ClipboardCloseGuard close;
            HDROP drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
            if (drop) {
                const UINT count = DragQueryFileW(drop, 0xffffffffu, nullptr, 0);
                for (UINT index = 0; index < count; ++index) {
                    const UINT length = DragQueryFileW(drop, index, nullptr, 0);
                    std::wstring path(static_cast<std::size_t>(length) + 1, L'\0');
                    if (DragQueryFileW(drop, index, path.data(), length + 1)) {
                        path.resize(length);
                        const auto value = utf8(path.c_str());
                        if (!value.empty())
                            resources.push_back(nk::platform::resource_from_file_path(
                                value, NK_RESOURCE_READABLE));
                    }
                }
            }
        }
        const auto request = nk::core::next_request_id();
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE;
        event.request_id = request;
        event.data_count = static_cast<uint32_t>(resources.size());
        event.data = nk::platform::resource_payload(false, resources);
        const auto result = nk::core::push_event(std::move(event));
        if (result != NK_OK)
            return fail(result, "could not queue resource clipboard result");
        *out_request = request;
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while reading resource clipboard");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while reading resource clipboard");
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

nk_result NK_CALL nk_share(const nk_share_options *options) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!options || options->struct_size < sizeof(*options) || options->flags != 0 ||
        (!options->text && options->resource_count == 0))
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid or empty share options");
    if (const auto result = nk::platform::validate_resources(options->resources,
                                                              options->resource_count, true);
        result != NK_OK)
        return result;
    // Win32 applications without a package identity cannot populate the
    // Windows Share contract portably. Preserve the URI payload on the system
    // clipboard so it remains available to shell-aware desktop applications.
    if (!options->resource_count)
        return nk_clipboard_set_text(options->text);
    const auto result = nk_clipboard_set_resources(options->resources, options->resource_count);
    if (result != NK_OK || !options->text)
        return result;
    const auto value = wide(options->text);
    if (*options->text && value.empty())
        return fail(NK_ERROR_INVALID_ARGUMENT, "share text is not valid UTF-8");
    const std::size_t bytes = (value.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory)
        return fail(NK_ERROR_OUT_OF_MEMORY, "could not allocate share text");
    void *destination = GlobalLock(memory);
    if (!destination) {
        GlobalFree(memory);
        return fail(NK_ERROR_UNKNOWN, "could not lock share text");
    }
    std::memcpy(destination, value.c_str(), bytes);
    GlobalUnlock(memory);
    if (!open_clipboard()) {
        GlobalFree(memory);
        return fail(NK_ERROR_UNKNOWN, "clipboard is busy");
    }
    if (!SetClipboardData(CF_UNICODETEXT, memory)) {
        CloseClipboard();
        GlobalFree(memory);
        return fail(NK_ERROR_UNKNOWN, "Windows rejected share text");
    }
    CloseClipboard();
    return NK_OK;
}

nk_result NK_CALL nk_dialog_open_resource(nk_handle parent,
                                          const nk_file_dialog_options *options,
                                          nk_request_id *request) {
    return nk::core::result_boundary(
        "unexpected error while opening resource dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, request, NK_DIALOG_OPEN_RESOURCE, true);
        });
}

nk_result NK_CALL nk_dialog_save_resource(nk_handle parent,
                                          const nk_file_dialog_options *options,
                                          nk_request_id *request) {
    return nk::core::result_boundary(
        "unexpected error while opening resource save dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, request, NK_DIALOG_SAVE_RESOURCE, true);
        });
}

nk_result NK_CALL nk_dialog_select_resource_directory(
    nk_handle parent, const nk_file_dialog_options *options, nk_request_id *request) {
    return nk::core::result_boundary(
        "unexpected error while opening resource directory dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, request,
                                     NK_DIALOG_SELECT_RESOURCE_DIRECTORY, true);
        });
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
