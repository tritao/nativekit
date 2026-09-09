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
#include <shobjidl.h>
#include <shellapi.h>

#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr wchar_t window_class_name[] = L"NativeKitWindow";
ATOM window_class = 0;

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
};

std::mutex dialogs_mutex;
std::unordered_map<nk_request_id, std::shared_ptr<WinDialogContext>> dialogs;

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

bool copy_wide(const char* source, std::wstring& destination) {
    destination = wide(source);
    return !source || !*source || !destination.empty();
}

std::string utf8(const wchar_t* text) {
    if (!text || !*text) return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                                         nullptr, 0, nullptr, nullptr);
    if (!size) return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, -1,
                        result.data(), size, nullptr, nullptr);
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

std::vector<std::byte> dialog_paths_payload(const std::vector<std::string>& paths,
                                            bool accepted) {
    const std::size_t offsets_offset = sizeof(nk_dialog_paths);
    const std::size_t strings_offset = offsets_offset + paths.size() * sizeof(uint32_t);
    std::size_t total = strings_offset;
    for (const auto& path : paths) total += path.size() + 1;
    std::vector<std::byte> result(total);
    const nk_dialog_paths header{
        accepted ? 1u : 0u, static_cast<uint32_t>(paths.size()),
        static_cast<uint32_t>(offsets_offset), static_cast<uint32_t>(strings_offset)};
    std::memcpy(result.data(), &header, sizeof(header));
    std::size_t cursor = strings_offset;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto offset = static_cast<uint32_t>(cursor);
        std::memcpy(result.data() + offsets_offset + index * sizeof(offset),
                    &offset, sizeof(offset));
        std::memcpy(result.data() + cursor, paths[index].c_str(), paths[index].size() + 1);
        cursor += paths[index].size() + 1;
    }
    return result;
}

template<typename T>
void release(T*& value) {
    if (value) value->Release();
    value = nullptr;
}

void emit_file_completion(const WinDialogContext& context,
                          std::vector<std::string> paths, bool accepted,
                          nk_result result = NK_OK) {
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_DIALOG_COMPLETE;
    event.request_id = context.request;
    event.flags = context.kind;
    event.result = result;
    event.data_count = static_cast<uint32_t>(paths.size());
    event.data = dialog_paths_payload(paths, accepted);
    nk::core::push_event(std::move(event));
}

void run_file_dialog(const std::shared_ptr<WinDialogContext>& context) noexcept {
    context->thread_id = GetCurrentThreadId();
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IFileDialog* dialog = nullptr;
    HRESULT status = E_FAIL;
    if (SUCCEEDED(initialized)) {
        if (context->kind == NK_DIALOG_SAVE_FILE)
            status = CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IFileSaveDialog,
                                      reinterpret_cast<void**>(&dialog));
        else
            status = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_IFileOpenDialog,
                                      reinterpret_cast<void**>(&dialog));
    }
    std::vector<std::string> paths;
    bool accepted = false;
    if (SUCCEEDED(status) && dialog) {
        FILEOPENDIALOGOPTIONS options = 0;
        dialog->GetOptions(&options);
        options |= FOS_FORCEFILESYSTEM | FOS_NOCHANGEDIR;
        if (context->kind == NK_DIALOG_SELECT_DIRECTORY) options |= FOS_PICKFOLDERS;
        if (context->kind == NK_DIALOG_OPEN_FILE &&
            (context->flags & NK_DIALOG_ALLOW_MULTIPLE)) options |= FOS_ALLOWMULTISELECT;
        if (context->flags & NK_DIALOG_SHOW_HIDDEN) options |= FOS_FORCESHOWHIDDEN;
        if (context->kind == NK_DIALOG_SAVE_FILE &&
            !(context->flags & NK_DIALOG_CONFIRM_OVERWRITE)) options &= ~FOS_OVERWRITEPROMPT;
        dialog->SetOptions(options);
        if (!context->title.empty()) dialog->SetTitle(context->title.c_str());
        if (!context->suggested_name.empty()) dialog->SetFileName(context->suggested_name.c_str());
        if (!context->initial_path.empty()) {
            IShellItem* initial = nullptr;
            if (SUCCEEDED(SHCreateItemFromParsingName(context->initial_path.c_str(), nullptr,
                                                       IID_IShellItem,
                                                       reinterpret_cast<void**>(&initial)))) {
                dialog->SetFolder(initial);
                release(initial);
            }
        }
        std::vector<COMDLG_FILTERSPEC> specifications;
        specifications.reserve(context->filters.size());
        for (const auto& filter : context->filters)
            specifications.push_back({filter.first.c_str(), filter.second.c_str()});
        if (!specifications.empty())
            dialog->SetFileTypes(static_cast<UINT>(specifications.size()), specifications.data());
        if (context->canceled) status = HRESULT_FROM_WIN32(ERROR_CANCELLED);
        else status = dialog->Show(context->parent);
        if (SUCCEEDED(status)) {
            accepted = true;
            if (context->kind == NK_DIALOG_OPEN_FILE &&
                (context->flags & NK_DIALOG_ALLOW_MULTIPLE)) {
                IFileOpenDialog* open_dialog = nullptr;
                IShellItemArray* items = nullptr;
                if (SUCCEEDED(dialog->QueryInterface(IID_IFileOpenDialog,
                                                     reinterpret_cast<void**>(&open_dialog))) &&
                    SUCCEEDED(open_dialog->GetResults(&items))) {
                    DWORD count = 0;
                    items->GetCount(&count);
                    for (DWORD index = 0; index < count; ++index) {
                        IShellItem* item = nullptr;
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
                IShellItem* item = nullptr;
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
    try {
        emit_file_completion(*context, std::move(paths), accepted && !canceled,
                             (SUCCEEDED(status) || canceled) ? NK_OK : NK_ERROR_UNKNOWN);
    } catch (...) {}
    release(dialog);
    if (SUCCEEDED(initialized)) CoUninitialize();
    context->complete = true;
}

UINT message_box_type(const WinDialogContext& context) {
    UINT type = MB_TASKMODAL;
    if (context.message_kind == NK_MESSAGE_WARNING) type |= MB_ICONWARNING;
    else if (context.message_kind == NK_MESSAGE_ERROR) type |= MB_ICONERROR;
    else if (context.message_kind == NK_MESSAGE_QUESTION) type |= MB_ICONQUESTION;
    else type |= MB_ICONINFORMATION;
    const bool yes_no = (context.buttons & (NK_MESSAGE_BUTTON_YES | NK_MESSAGE_BUTTON_NO)) != 0;
    if (yes_no) type |= (context.buttons & NK_MESSAGE_BUTTON_CANCEL) ? MB_YESNOCANCEL : MB_YESNO;
    else type |= (context.buttons & NK_MESSAGE_BUTTON_CANCEL) ? MB_OKCANCEL : MB_OK;
    return type;
}

void run_message_dialog(const std::shared_ptr<WinDialogContext>& context) noexcept {
    context->thread_id = GetCurrentThreadId();
    int response = IDCANCEL;
    if (!context->canceled)
        response = MessageBoxW(context->parent, context->message.c_str(), context->title.c_str(),
                               message_box_type(*context));
    uint32_t button = NK_MESSAGE_RESULT_NONE;
    if (context->canceled || response == IDCANCEL) button = NK_MESSAGE_RESULT_CANCEL;
    else if (response == IDOK) button = NK_MESSAGE_RESULT_OK;
    else if (response == IDYES) button = NK_MESSAGE_RESULT_YES;
    else if (response == IDNO) button = NK_MESSAGE_RESULT_NO;
    try {
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_DIALOG_COMPLETE;
        event.request_id = context->request;
        event.flags = context->kind;
        event.result = response ? NK_OK : NK_ERROR_UNKNOWN;
        event.data = bytes_of(nk_dialog_message_result{button});
        nk::core::push_event(std::move(event));
    } catch (...) {}
    context->complete = true;
}

BOOL CALLBACK close_thread_window(HWND window, LPARAM) {
    PostMessageW(window, WM_CLOSE, 0, 0);
    return TRUE;
}

void request_dialog_close(const std::shared_ptr<WinDialogContext>& context) {
    context->canceled = true;
    const DWORD thread = context->thread_id;
    if (thread) EnumThreadWindows(thread, close_thread_window, 0);
}

void cancel_dialogs_for_parent(HWND parent) {
    std::lock_guard lock(dialogs_mutex);
    for (const auto& [request, context] : dialogs) {
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
            } else ++iterator;
        }
    }
    for (auto& context : finished)
        if (context->worker.joinable()) context->worker.join();
}

nk_result start_file_dialog(nk_handle parent_handle, const nk_file_dialog_options* options,
                            nk_request_id* out_request, uint32_t kind) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (!options || options->struct_size < sizeof(*options) || !out_request ||
        (options->filter_count && !options->filters))
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid file dialog options");
    auto context = std::make_shared<WinDialogContext>();
    if (parent_handle != NK_INVALID_HANDLE) {
        auto parent = get_window(parent_handle);
        if (!parent) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
        context->parent = parent->window;
    }
    context->request = nk::core::next_request_id();
    context->kind = kind;
    context->flags = options->flags;
    if (!copy_wide(options->title, context->title) ||
        !copy_wide(options->initial_path, context->initial_path) ||
        !copy_wide(options->suggested_name, context->suggested_name))
        return fail(NK_ERROR_INVALID_ARGUMENT, "file dialog option is not valid UTF-8");
    for (uint32_t index = 0; index < options->filter_count; ++index) {
        const auto& filter = options->filters[index];
        if (!filter.patterns) continue;
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
    reap_dialogs();
}

void shutdown() noexcept {
    std::vector<std::shared_ptr<WinDialogContext>> pending;
    {
        std::lock_guard lock(dialogs_mutex);
        for (const auto& [request, context] : dialogs) {
            (void)request;
            pending.push_back(context);
            request_dialog_close(context);
        }
        dialogs.clear();
    }
    for (auto& context : pending) {
        while (!context->complete) {
            request_dialog_close(context);
            Sleep(1);
        }
        if (context->worker.joinable()) context->worker.join();
    }
    nk::core::handles().clear();
    pump_events();
}
}

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_WINDOW | NK_CAP_FILE_DIALOG | NK_CAP_EXPORT_NATIVE_WINDOW;
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
    cancel_dialogs_for_parent(resource->window);
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
nk_result NK_CALL nk_dialog_open_file(nk_handle parent, const nk_file_dialog_options* options,
                                      nk_request_id* out_request) {
    try { return start_file_dialog(parent, options, out_request, NK_DIALOG_OPEN_FILE); }
    catch (...) { return fail(NK_ERROR_UNKNOWN, "unexpected error while opening file dialog"); }
}

nk_result NK_CALL nk_dialog_save_file(nk_handle parent, const nk_file_dialog_options* options,
                                      nk_request_id* out_request) {
    try { return start_file_dialog(parent, options, out_request, NK_DIALOG_SAVE_FILE); }
    catch (...) { return fail(NK_ERROR_UNKNOWN, "unexpected error while opening save dialog"); }
}

nk_result NK_CALL nk_dialog_select_directory(nk_handle parent,
                                             const nk_file_dialog_options* options,
                                             nk_request_id* out_request) {
    try { return start_file_dialog(parent, options, out_request, NK_DIALOG_SELECT_DIRECTORY); }
    catch (...) { return fail(NK_ERROR_UNKNOWN, "unexpected error while opening directory dialog"); }
}

nk_result NK_CALL nk_dialog_message(nk_handle parent_handle,
                                    const nk_message_dialog_options* options,
                                    nk_request_id* out_request) {
    try {
        if (const auto result = enter_ui(); result != NK_OK) return result;
        if (!options || options->struct_size < sizeof(*options) || !out_request || !options->message)
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid message dialog options");
        auto context = std::make_shared<WinDialogContext>();
        if (parent_handle != NK_INVALID_HANDLE) {
            auto parent = get_window(parent_handle);
            if (!parent) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
            context->parent = parent->window;
        }
        context->request = nk::core::next_request_id();
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
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while creating message dialog");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while creating message dialog");
    }
}

nk_result NK_CALL nk_dialog_cancel(nk_request_id request) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    std::lock_guard lock(dialogs_mutex);
    const auto found = dialogs.find(request);
    if (request == NK_INVALID_REQUEST_ID || found == dialogs.end() || found->second->complete)
        return fail(NK_ERROR_INVALID_REQUEST, "invalid or completed dialog request");
    request_dialog_close(found->second);
    return NK_OK;
}
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
