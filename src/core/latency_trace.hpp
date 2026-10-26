#pragma once

#include <cstdint>

namespace nk::core {

/**
 * Input-latency trace: when the environment variable NK_LATENCY_TRACE names a file, NativeKit appends one line per
 * event that decides how fast the app reacts to input: an OS input message taken off the queue, a wait for events
 * starting and ending, and an on-demand frame starting and ending. Each line is `<kind> <ticks> <a> <b>`; the first
 * line names the clock. On Windows the ticks are QueryPerformanceCounter values, which other processes (an input hook
 * recording when the OS received each event) and HashLink's GC trace share, so the files can be merged on one
 * timeline. With the variable unset every call is one relaxed load.
 *
 * Kinds: M mouse move (screen x, y), B mouse button (screen x, y), K key (virtual key, 1 down / 0 up),
 * W wait begins (timeout ms), w wait ends, F frame begins, f frame ends.
 */
bool latency_trace_enabled() noexcept;
void latency_trace(char kind, std::int64_t a = 0, std::int64_t b = 0) noexcept;

/** Writes buffered lines out; called before each wait so a killed process loses at most one loop's events. */
void latency_trace_flush() noexcept;

} // namespace nk::core
