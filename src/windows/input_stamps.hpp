#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

/*
 * Precise OS stamps for the input the UI thread dequeues.
 *
 * A window message carries only GetMessageTime(), which counts milliseconds on the system tick
 * (15.625 ms; timeBeginPeriod does not refine it), so an age computed from it is off by up to a
 * tick. Low-level mouse and keyboard hooks run on the raw input thread's schedule, before the input
 * reaches any queue, and get the same tick time the message will carry. A thread that does nothing
 * but receive those hooks stamps each input with the steady clock (QueryPerformanceCounter) within
 * tens of microseconds of the system taking it; the UI thread then finds the stamp for a message by
 * its tick time and what it is (position, button, key).
 */
namespace nk::windows_input_stamps {

enum class Kind : std::uint8_t { move, button, key };

/** One input as a low-level hook saw it. */
struct Stamp {
    Kind kind = Kind::move;
    /** The tick time, as GetMessageTime() will report it for the message. */
    std::uint32_t time_ms = 0;
    /** Screen position in physical pixels (move, button). */
    std::int32_t x = 0;
    std::int32_t y = 0;
    /** Which transition (button, key); see button_code() and key_code(). */
    std::uint32_t code = 0;
    /** When the hook ran, on nk_time_now_ns()'s clock. */
    std::uint64_t time_ns = 0;
};

/** A button transition's code from its message (WM_LBUTTONDOWN, ...) and, for X buttons, which. */
constexpr std::uint32_t button_code(std::uint32_t down_or_up_message, std::uint32_t xbutton) {
    return (down_or_up_message << 8) | (xbutton & 0xffu);
}

/** A key transition's code: scan code, extended-key flag and whether the key went up. */
constexpr std::uint32_t key_code(std::uint32_t scan_code, bool extended, bool released) {
    return (scan_code & 0xffu) | (extended ? 0x100u : 0u) | (released ? 0x200u : 0u);
}

/**
 * The most recent stamps. The hook thread records, the UI thread looks up; both hold the lock only
 * for a copy or a scan of the ring.
 */
class Log {
  public:
    static constexpr std::size_t capacity = 512;

    void record(const Stamp &stamp) noexcept;

    /**
     * The stamp, on nk_time_now_ns()'s clock, of the input a dequeued message stands for, or 0 when
     * no hook saw it. A move is the newest one at that time and position: the system merges moves,
     * and the message it builds carries the last. A button or key is the oldest at that time with
     * that code (and, for a button, position) not already taken, so repeated keys each get their
     * own.
     */
    std::uint64_t take(Kind kind, std::uint32_t time_ms, std::int32_t x, std::int32_t y,
                       std::uint32_t code) noexcept;

  private:
    struct Entry {
        Stamp stamp;
        bool taken = false;
    };
    std::mutex mutex_;
    std::array<Entry, capacity> ring_{};
    std::size_t next_ = 0;
    std::size_t count_ = 0;
};

/**
 * Starts the hook thread if it is not running and NK_INPUT_STAMP_HOOK is not "0". Cheap once it
 * runs. While the hooks are installed every input on the desktop passes through them, so a process
 * stopped in a debugger delays all input until Windows times the hook out.
 */
void ensure_started() noexcept;
/** Removes the hooks and joins the thread. */
void shutdown() noexcept;
/** Whether the hooks are installed. */
bool active() noexcept;
/** See Log::take(); 0 when the hook thread is not running. */
std::uint64_t take(Kind kind, std::uint32_t time_ms, std::int32_t x, std::int32_t y,
                   std::uint32_t code) noexcept;

} // namespace nk::windows_input_stamps
