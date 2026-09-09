#include "nativekit_time.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace {
using Clock = std::chrono::steady_clock;
constexpr auto pump_interval = std::chrono::milliseconds(10);

nk_result wait_until(const Clock::time_point *deadline) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    const auto sequence = nk::core::wake_sequence();
    for (;;) {
        nk::backend::pump_events();
        if (nk::core::events_pending() || nk::core::wake_sequence() != sequence)
            return NK_OK;
        auto duration = pump_interval;
        if (deadline) {
            const auto now = Clock::now();
            if (now >= *deadline)
                return NK_OK;
            duration = std::min(
                duration,
                std::chrono::duration_cast<std::chrono::milliseconds>(*deadline - now));
            if (duration.count() == 0)
                duration = std::chrono::milliseconds(1);
        }
        if (nk::core::wait_for_wake(sequence, duration))
            return NK_OK;
    }
}
} // namespace

extern "C" {
uint64_t NK_CALL nk_time_now_ns(void) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch())
            .count());
}

double NK_CALL nk_time_seconds(void) {
    return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

nk_result NK_CALL nk_wait_events(void) {
    return nk::core::result_boundary("unexpected error while waiting for events",
                                     [] { return wait_until(nullptr); });
}

nk_result NK_CALL nk_wait_events_timeout(double timeout_seconds) {
    return nk::core::result_boundary("unexpected error while waiting for events",
                                     [&]() -> nk_result {
                                         nk::core::clear_error();
                                         if (!std::isfinite(timeout_seconds) ||
                                             timeout_seconds < 0.0) {
                                             nk::core::set_error("invalid event wait timeout");
                                             return NK_ERROR_INVALID_ARGUMENT;
                                         }
                                         const auto deadline =
                                             Clock::now() + std::chrono::duration_cast<Clock::duration>(
                                                                std::chrono::duration<double>(
                                                                    timeout_seconds));
                                         return wait_until(&deadline);
                                     });
}

void NK_CALL nk_wake_events(void) {
    nk::core::wake_events();
}
}
