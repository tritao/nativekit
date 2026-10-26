#pragma once

#include "nativekit_trace.h"

#include <cstdint>

namespace nk::core {

/**
 * NativeKit's trace points forward to the sink installed with nk_set_trace_sink(), or, without one,
 * to a text file named by NK_LATENCY_TRACE. With neither, or while the host's switch is off, a
 * trace point is one load or two. Building with NK_TRACE=OFF removes the trace points.
 */
const nk_trace_sink *active_trace_sink() noexcept;

/** A zone ends on the sink it began on, even if another sink was installed meanwhile. */
class TraceZone {
  public:
    explicit TraceZone(const nk_trace_location *location) noexcept : sink_(active_trace_sink()) {
        if (sink_)
            sink_->zone_begin(sink_->user_data, location);
    }
    ~TraceZone() {
        if (sink_)
            sink_->zone_end(sink_->user_data);
    }
    TraceZone(const TraceZone &) = delete;
    TraceZone &operator=(const TraceZone &) = delete;

  private:
    const nk_trace_sink *sink_;
};

inline bool trace_active() noexcept {
    return active_trace_sink() != nullptr;
}

inline void trace_event(nk_trace_event_kind kind, std::uint64_t os_time_ns, std::int64_t a,
                        std::int64_t b) noexcept {
    if (const nk_trace_sink *sink = active_trace_sink())
        sink->event(sink->user_data, kind, os_time_ns, a, b);
}

/**
 * Writes out the text-file fallback's buffered lines. Called before each wait, so a killed process
 * loses little.
 */
void trace_flush() noexcept;

} // namespace nk::core

#define NK_TRACE_CONCAT_INNER(a, b) a##b
#define NK_TRACE_CONCAT(a, b) NK_TRACE_CONCAT_INNER(a, b)

#if NK_TRACE_ENABLED
/** Traces the rest of the enclosing scope as a zone named `name` (a string literal). */
#define NK_TRACE_ZONE(name)                                                                        \
    static const nk_trace_location NK_TRACE_CONCAT(nk_trace_location_, __LINE__){                  \
        name, __func__, __FILE__, static_cast<std::uint32_t>(__LINE__), 0};                        \
    const nk::core::TraceZone NK_TRACE_CONCAT(nk_trace_zone_, __LINE__)(                           \
        &NK_TRACE_CONCAT(nk_trace_location_, __LINE__))
#define NK_TRACE_EVENT(kind, os_time_ns, a, b) nk::core::trace_event((kind), (os_time_ns), (a), (b))
#define NK_TRACE_ACTIVE() nk::core::trace_active()
#else
#define NK_TRACE_ZONE(name) static_cast<void>(0)
/* The arguments stay referenced, unevaluated, so their helpers do not become unused. */
#define NK_TRACE_EVENT(kind, os_time_ns, a, b)                                                     \
    static_cast<void>(sizeof((kind), (os_time_ns), (a), (b)))
#define NK_TRACE_ACTIVE() false
#endif
