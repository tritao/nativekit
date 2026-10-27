#include "windows/input_stamps.hpp"

#include "core/thread_name.hpp"
#include "nativekit_time.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <mutex>

namespace nk::windows_input_stamps {
namespace {

/** A message's kind, and the transition the hook reports for it (a double click is a press). */
struct Transition {
    Kind kind;
    std::uint32_t message;
};

std::optional<Transition> transition(std::uint32_t message) noexcept {
    switch (message) {
    case WM_MOUSEMOVE:
        return Transition{Kind::move, message};
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
        return Transition{Kind::button, message};
    case WM_LBUTTONDBLCLK:
        return Transition{Kind::button, WM_LBUTTONDOWN};
    case WM_RBUTTONDBLCLK:
        return Transition{Kind::button, WM_RBUTTONDOWN};
    case WM_MBUTTONDBLCLK:
        return Transition{Kind::button, WM_MBUTTONDOWN};
    case WM_XBUTTONDBLCLK:
        return Transition{Kind::button, WM_XBUTTONDOWN};
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
        return Transition{Kind::key, message};
    default:
        return std::nullopt;
    }
}

/** Whether tick time `a` is before `b`, across the 49.7-day wrap. */
bool earlier(std::uint32_t a, std::uint32_t b) noexcept {
    return static_cast<std::int32_t>(a - b) < 0;
}

} // namespace

bool is_input_message(std::uint32_t message) noexcept {
    return transition(message).has_value();
}

std::optional<Input> mouse_input(std::uint32_t message, std::uint32_t xbutton, std::int32_t x,
                                 std::int32_t y, std::uint32_t time_ms) noexcept {
    const auto found = transition(message);
    if (!found || found->kind == Kind::key)
        return std::nullopt;
    const bool x_button = found->message == WM_XBUTTONDOWN || found->message == WM_XBUTTONUP;
    const std::uint32_t code =
        found->kind == Kind::button ? found->message << 8 | (x_button ? xbutton & 0xffu : 0u) : 0u;
    return Input{found->kind, time_ms, x, y, code};
}

std::optional<Input> key_input(std::uint32_t message, std::uint32_t scan_code, bool extended,
                               std::uint32_t time_ms) noexcept {
    const auto found = transition(message);
    if (!found || found->kind != Kind::key)
        return std::nullopt;
    const bool released = message == WM_KEYUP || message == WM_SYSKEYUP;
    const std::uint32_t code =
        (scan_code & 0xffu) | (extended ? 0x100u : 0u) | (released ? 0x200u : 0u);
    return Input{Kind::key, time_ms, 0, 0, code};
}

void StampRing::record(const Stamp &stamp) noexcept {
    const std::uint64_t number = recorded_.load(std::memory_order_relaxed);
    Slot &slot = slots_[number % capacity];
    slot.sequence.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    slot.kind.store(static_cast<std::uint8_t>(stamp.input.kind), std::memory_order_relaxed);
    slot.time_ms.store(stamp.input.time_ms, std::memory_order_relaxed);
    slot.x.store(stamp.input.x, std::memory_order_relaxed);
    slot.y.store(stamp.input.y, std::memory_order_relaxed);
    slot.code.store(stamp.input.code, std::memory_order_relaxed);
    slot.time_ns.store(stamp.time_ns, std::memory_order_relaxed);
    slot.sequence.store(number + 1, std::memory_order_release);
    recorded_.store(number + 1, std::memory_order_release);
}

bool StampRing::read(std::uint64_t number, Stamp &out) const noexcept {
    const Slot &slot = slots_[number % capacity];
    if (slot.sequence.load(std::memory_order_acquire) != number + 1)
        return false;
    out.input.kind = static_cast<Kind>(slot.kind.load(std::memory_order_relaxed));
    out.input.time_ms = slot.time_ms.load(std::memory_order_relaxed);
    out.input.x = slot.x.load(std::memory_order_relaxed);
    out.input.y = slot.y.load(std::memory_order_relaxed);
    out.input.code = slot.code.load(std::memory_order_relaxed);
    out.time_ns = slot.time_ns.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    // Overwritten while it was read: it is older than the ring holds anyway.
    return slot.sequence.load(std::memory_order_relaxed) == number + 1;
}

std::uint64_t StampRing::match(const Input &input) noexcept {
    const std::uint64_t end = recorded_.load(std::memory_order_acquire);
    const std::uint64_t begin = end > capacity ? end - capacity : 0;
    const bool consumed = input.kind != Kind::move;
    std::optional<std::uint64_t> at_position;
    std::optional<std::uint64_t> at_time;
    Stamp stamp;
    // Newest first. Hooks run in input order, so the scan stops at the first stamp before the tick.
    for (std::uint64_t number = end; number-- > begin;) {
        if (!read(number, stamp))
            continue;
        if (earlier(stamp.input.time_ms, input.time_ms))
            break;
        if (stamp.input.kind != input.kind || stamp.input.time_ms != input.time_ms ||
            stamp.input.code != input.code)
            continue;
        if (consumed && matched_[number % capacity] == number + 1)
            continue;
        const bool same_position =
            input.kind == Kind::key || (stamp.input.x == input.x && stamp.input.y == input.y);
        // A move keeps the newest it finds; a button or key keeps overwriting, ending at the oldest.
        if (same_position && (consumed || !at_position))
            at_position = number;
        if (consumed || !at_time)
            at_time = number;
    }
    const auto chosen = at_position ? at_position : at_time;
    if (!chosen || !read(*chosen, stamp))
        return 0;
    if (consumed)
        matched_[*chosen % capacity] = *chosen + 1;
    return stamp.time_ns;
}

std::optional<std::uint32_t> StampRing::newest_time_ms() const noexcept {
    const std::uint64_t end = recorded_.load(std::memory_order_acquire);
    Stamp stamp;
    if (end == 0 || !read(end - 1, stamp))
        return std::nullopt;
    return stamp.input.time_ms;
}

namespace {

StampRing ring;
std::mutex lifecycle_mutex;
HANDLE hook_thread_handle = nullptr;
DWORD hook_thread_id = 0;
std::atomic<bool> hooks_running{false};
/* Set when NK_INPUT_STAMP_HOOK=0, after a failed start, or while a stopped thread has not exited,
   so updates do not retry until shutdown(). */
std::atomic<bool> given_up{false};
/* Set by match() when a button or key arrived that no hook saw: Windows removed a hook. */
std::atomic<bool> hooks_lost{false};
std::atomic<std::uint32_t> starts{0};
std::uint64_t last_start_ns = 0;

constexpr DWORD stop_timeout_ms = 2000;
constexpr std::uint64_t restart_interval_ns = 1000000000u;

LRESULT CALLBACK mouse_hook(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        const std::uint64_t now = nk_time_now_ns();
        const auto &info = *reinterpret_cast<const MSLLHOOKSTRUCT *>(lparam);
        if (const auto input = mouse_input(static_cast<std::uint32_t>(wparam),
                                           HIWORD(info.mouseData), info.pt.x, info.pt.y, info.time))
            ring.record(Stamp{*input, now});
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

LRESULT CALLBACK keyboard_hook(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION) {
        const std::uint64_t now = nk_time_now_ns();
        const auto &info = *reinterpret_cast<const KBDLLHOOKSTRUCT *>(lparam);
        if (const auto input = key_input(static_cast<std::uint32_t>(wparam), info.scanCode,
                                         (info.flags & LLKHF_EXTENDED) != 0, info.time))
            ring.record(Stamp{*input, now});
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

struct StartContext {
    HANDLE ready;
    bool hooked = false;
};

/**
 * Windows calls a low-level hook on the thread that installed it, through that thread's message
 * loop, and holds the input until the hook returns. This thread does nothing else, so the stamp is
 * taken as soon as the system hands over the input.
 */
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
    // Both or neither: a missing hook would make every input it covers look like a lost hook.
    context->hooked = mouse && keyboard;
    const bool hooked = context->hooked;
    SetEvent(context->ready); // `context` is gone after this
    if (hooked) {
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

/* The calls below hold lifecycle_mutex. */

void start_locked() noexcept {
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
    last_start_ns = nk_time_now_ns();
    if (!started) {
        given_up.store(true, std::memory_order_relaxed);
        return;
    }
    hook_thread_handle = started;
    hook_thread_id = id;
    if (!context.hooked) {
        // The thread unhooks what it got and exits on its own.
        given_up.store(true, std::memory_order_relaxed);
        return;
    }
    hooks_lost.store(false, std::memory_order_relaxed);
    starts.fetch_add(1, std::memory_order_relaxed);
    hooks_running.store(true, std::memory_order_release);
}

/** Stops the thread; false when it has not exited in time, so it may still hold its hooks. */
bool stop_locked() noexcept {
    hooks_running.store(false, std::memory_order_release);
    if (!hook_thread_handle)
        return true;
    PostThreadMessageW(hook_thread_id, WM_QUIT, 0, 0);
    if (WaitForSingleObject(hook_thread_handle, stop_timeout_ms) != WAIT_OBJECT_0) {
        // Never start a second thread beside one that may still hold its hooks.
        given_up.store(true, std::memory_order_relaxed);
        return false;
    }
    CloseHandle(hook_thread_handle);
    hook_thread_handle = nullptr;
    hook_thread_id = 0;
    return true;
}

} // namespace

void update(bool tracing) noexcept {
    const bool is_running = hooks_running.load(std::memory_order_acquire);
    if (tracing ? is_running && !hooks_lost.load(std::memory_order_relaxed)
                : !is_running)
        return;
    if (given_up.load(std::memory_order_relaxed))
        return;
    const std::lock_guard<std::mutex> lock(lifecycle_mutex);
    if (!tracing) {
        stop_locked();
        return;
    }
    if (hooks_running.load(std::memory_order_relaxed)) {
        if (!hooks_lost.load(std::memory_order_relaxed) ||
            nk_time_now_ns() - last_start_ns < restart_interval_ns)
            return;
        if (!stop_locked())
            return;
    }
    start_locked();
}

void shutdown() noexcept {
    const std::lock_guard<std::mutex> lock(lifecycle_mutex);
    if (stop_locked())
        given_up.store(false, std::memory_order_relaxed);
}

bool running() noexcept {
    return hooks_running.load(std::memory_order_acquire);
}

std::uint32_t hook_starts() noexcept {
    return starts.load(std::memory_order_relaxed);
}

std::uint64_t match(const Input &input) noexcept {
    if (!hooks_running.load(std::memory_order_acquire))
        return 0;
    const std::uint64_t stamp = ring.match(input);
    if (stamp == 0 && input.kind != Kind::move) {
        // A hook runs before the system queues the input, so it has always recorded the tick of a
        // button or key by the time the message arrives. Moves are left out: the system makes some
        // up (after a window moves, or SetCursorPos) without a hook seeing them.
        const auto newest = ring.newest_time_ms();
        if (!newest || earlier(*newest, input.time_ms))
            hooks_lost.store(true, std::memory_order_relaxed);
    }
    return stamp;
}

} // namespace nk::windows_input_stamps
