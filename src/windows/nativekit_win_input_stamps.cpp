#include "windows/input_stamps.hpp"

#include "core/thread_name.hpp"
#include "nativekit_time.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>

namespace nk::windows_input_stamps {
namespace {

Log stamps;
std::mutex lifecycle_mutex;
HANDLE thread = nullptr;
DWORD thread_id = 0;
std::atomic<bool> running{false};
/* Set after a failed start or when NK_INPUT_STAMP_HOOK=0, so the UI thread does not retry. */
std::atomic<bool> given_up{false};

LRESULT CALLBACK mouse_hook(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        const std::uint64_t now = nk_time_now_ns();
        const auto &info = *reinterpret_cast<const MSLLHOOKSTRUCT *>(lparam);
        Stamp stamp;
        stamp.time_ms = info.time;
        stamp.x = info.pt.x;
        stamp.y = info.pt.y;
        stamp.time_ns = now;
        switch (wparam) {
        case WM_MOUSEMOVE:
            stamp.kind = Kind::move;
            stamps.record(stamp);
            break;
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
            stamp.kind = Kind::button;
            stamp.code = button_code(static_cast<std::uint32_t>(wparam),
                                     wparam == WM_XBUTTONDOWN || wparam == WM_XBUTTONUP
                                         ? HIWORD(info.mouseData)
                                         : 0u);
            stamps.record(stamp);
            break;
        default:
            break;
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK keyboard_hook(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        const std::uint64_t now = nk_time_now_ns();
        const auto &info = *reinterpret_cast<const KBDLLHOOKSTRUCT *>(lparam);
        Stamp stamp;
        stamp.kind = Kind::key;
        stamp.time_ms = info.time;
        stamp.code = key_code(info.scanCode, (info.flags & LLKHF_EXTENDED) != 0,
                              (info.flags & LLKHF_UP) != 0);
        stamp.time_ns = now;
        stamps.record(stamp);
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

struct StartContext {
    HANDLE ready;
    bool hooked = false;
};

/* Windows calls a low-level hook on the thread that installed it, through that thread's message
 * loop, and holds the input until the hook returns. This thread does nothing else, so the stamp is
 * taken as soon as the system hands over the input. */
DWORD WINAPI hook_thread(void *parameter) {
    auto *context = static_cast<StartContext *>(parameter);
    nk::core::name_current_thread("nk input stamps");
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    MSG message;
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE); // create the queue
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&mouse_hook), &module);
    HHOOK mouse = SetWindowsHookExW(WH_MOUSE_LL, mouse_hook, module, 0);
    HHOOK keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, keyboard_hook, module, 0);
    context->hooked = mouse || keyboard;
    SetEvent(context->ready); // `context` is gone after this
    if (mouse || keyboard) {
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
            DispatchMessageW(&message);
    }
    if (mouse)
        UnhookWindowsHookEx(mouse);
    if (keyboard)
        UnhookWindowsHookEx(keyboard);
    return 0;
}

bool disabled_by_environment() noexcept {
    wchar_t value[8];
    const DWORD length = GetEnvironmentVariableW(L"NK_INPUT_STAMP_HOOK", value, 8);
    return length == 1 && value[0] == L'0';
}

} // namespace

void ensure_started() noexcept {
    if (running.load(std::memory_order_acquire) || given_up.load(std::memory_order_relaxed))
        return;
    const std::lock_guard<std::mutex> lock(lifecycle_mutex);
    if (running.load(std::memory_order_relaxed) || given_up.load(std::memory_order_relaxed))
        return;
    if (disabled_by_environment()) {
        given_up.store(true, std::memory_order_relaxed);
        return;
    }
    StartContext context{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!context.ready) {
        given_up.store(true, std::memory_order_relaxed);
        return;
    }
    DWORD id = 0;
    HANDLE started = CreateThread(nullptr, 64 * 1024, hook_thread, &context, 0, &id);
    if (started)
        WaitForSingleObject(context.ready, INFINITE);
    CloseHandle(context.ready);
    if (!started || !context.hooked) {
        if (started) {
            WaitForSingleObject(started, 1000);
            CloseHandle(started);
        }
        given_up.store(true, std::memory_order_relaxed);
        return;
    }
    thread = started;
    thread_id = id;
    running.store(true, std::memory_order_release);
}

void shutdown() noexcept {
    const std::lock_guard<std::mutex> lock(lifecycle_mutex);
    given_up.store(false, std::memory_order_relaxed);
    if (!thread)
        return;
    running.store(false, std::memory_order_release);
    PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
    WaitForSingleObject(thread, 2000);
    CloseHandle(thread);
    thread = nullptr;
    thread_id = 0;
}

bool active() noexcept {
    return running.load(std::memory_order_acquire);
}

std::uint64_t take(Kind kind, std::uint32_t time_ms, std::int32_t x, std::int32_t y,
                   std::uint32_t code) noexcept {
    if (!running.load(std::memory_order_acquire))
        return 0;
    return stamps.take(kind, time_ms, x, y, code);
}

} // namespace nk::windows_input_stamps
