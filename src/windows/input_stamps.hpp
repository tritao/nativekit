#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

/*
 * Precise OS stamps for the input the UI thread dequeues.
 *
 * A window message carries only GetMessageTime(), which counts milliseconds on the system tick
 * (15.625 ms; timeBeginPeriod does not refine it), so an age computed from it is off by up to a
 * tick. Low-level mouse and keyboard hooks run before the input reaches any queue and get the same
 * tick time the message will carry. A thread that does nothing but receive those hooks stamps each
 * input with the steady clock (QueryPerformanceCounter) within tens of microseconds of the system
 * taking it; the UI thread then finds the stamp for a message by its tick time and what it is
 * (position, button, key).
 *
 * This header stays free of <windows.h> (nativekit_win.cpp sets UNICODE after its includes), so
 * messages and their fields are plain integers here.
 */
namespace nk::windows_input_stamps {

enum class Kind : std::uint8_t { move, button, key };

/** What an input is, as both its low-level hook and its window message describe it. */
struct Input {
    Kind kind = Kind::move;
    /** The tick time, as GetMessageTime() reports it for the message. */
    std::uint32_t time_ms = 0;
    /** Screen position in physical pixels (move, button). */
    std::int32_t x = 0;
    std::int32_t y = 0;
    /** Which transition: the button message and X button, or the scan code, extended and up flags. */
    std::uint32_t code = 0;
};

/** An input and when its hook ran, on nk_time_now_ns()'s clock. */
struct Stamp {
    Input input;
    std::uint64_t time_ns = 0;
};

/** Whether a window message is a pointer move, button or key that the hooks stamp. */
bool is_input_message(std::uint32_t message) noexcept;
/**
 * The input a mouse message stands for: WM_MOUSEMOVE or a button message, from a hook (where
 * `message` is its wParam) or a window. A double click is the press the hook saw. `xbutton` is
 * HIWORD of the hook's mouseData or of the message's wParam; it is ignored for other buttons.
 */
std::optional<Input> mouse_input(std::uint32_t message, std::uint32_t xbutton, std::int32_t x,
                                 std::int32_t y, std::uint32_t time_ms) noexcept;
/** The input a WM_KEYDOWN, WM_KEYUP, WM_SYSKEYDOWN or WM_SYSKEYUP stands for. */
std::optional<Input> key_input(std::uint32_t message, std::uint32_t scan_code, bool extended,
                               std::uint32_t time_ms) noexcept;

/**
 * The most recent stamps, without a lock: one thread records (the hook thread, which must never
 * wait on the UI thread) and one thread matches (the UI thread).
 */
class StampRing {
  public:
    static constexpr std::size_t capacity = 512;

    /** Recording thread only. */
    void record(const Stamp &stamp) noexcept;

    /**
     * The stamp of the input a dequeued message stands for, or 0 when none was recorded at its tick
     * time. Matching thread only. A move is the newest at that time: the system merges moves and
     * the message carries the last. A button or key is the oldest at that time with its code not
     * matched before, so repeated keys each get their own. A stamp at the message's position wins;
     * otherwise any at its time and code does, because the hook sees a position before the system
     * clips the cursor to the screen or a clip rectangle, and a window that is not per-monitor DPI
     * aware gets scaled coordinates.
     */
    std::uint64_t match(const Input &input) noexcept;

    /** The tick time of the newest stamp, or nothing before the first. */
    std::optional<std::uint32_t> newest_time_ms() const noexcept;

  private:
    /* A slot is a seqlock: `sequence` is the number of the stamp in it plus one, or 0 while the
       recording thread rewrites it. */
    struct Slot {
        std::atomic<std::uint64_t> sequence{0};
        std::atomic<std::uint8_t> kind{0};
        std::atomic<std::uint32_t> time_ms{0};
        std::atomic<std::int32_t> x{0};
        std::atomic<std::int32_t> y{0};
        std::atomic<std::uint32_t> code{0};
        std::atomic<std::uint64_t> time_ns{0};
    };

    bool read(std::uint64_t number, Stamp &out) const noexcept;

    std::array<Slot, capacity> slots_{};
    std::atomic<std::uint64_t> recorded_{0};
    /* Matching thread only: per slot, the number plus one of the stamp a button or key took. */
    std::array<std::uint64_t, capacity> matched_{};
};

/**
 * Starts the hooks while `tracing` and stops them when it is not, unless NK_INPUT_STAMP_HOOK is
 * "0" or they failed to start. Called on each pump; costs an atomic load when nothing changes.
 * While the hooks are installed every input on the desktop passes through them, so a traced
 * process stopped in a debugger delays all input until Windows times the hook out. Windows then
 * removes the hook without telling it; match() notices when a button or key arrives that no hook
 * saw, and the next update reinstalls the hooks (at most once a second).
 */
void update(bool tracing) noexcept;
/** Removes the hooks and lets a later update() start them again. */
void shutdown() noexcept;
/** Whether the hooks are installed. */
bool running() noexcept;
/** How many times the hooks were installed, for tests and diagnostics. */
std::uint32_t hook_starts() noexcept;
/** StampRing::match() on the hooks' stamps; 0 when the hooks are not running. */
std::uint64_t match(const Input &input) noexcept;

} // namespace nk::windows_input_stamps
