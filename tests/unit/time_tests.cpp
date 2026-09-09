#include "nativekit.h"
#include "nativekit_time.h"

#include <cassert>
#include <chrono>
#include <thread>

namespace {
void drain_events() {
    for (;;) {
        nk_event event{};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        const auto kind = event.kind;
        nk_event_release(&event);
        if (kind == NK_EVENT_NONE)
            return;
    }
}
} // namespace

int main() {
    const auto nanoseconds = nk_time_now_ns();
    const auto seconds = nk_time_seconds();
    assert(nk_time_now_ns() >= nanoseconds);
    assert(nk_time_seconds() >= seconds);

    nk_init_options options{};
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    assert(nk_init(&options) == NK_OK);
    drain_events();

    std::thread waker([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        nk_wake_events();
    });
    const auto wake_start = std::chrono::steady_clock::now();
    assert(nk_wait_events_timeout(1.0) == NK_OK);
    const auto wake_elapsed = std::chrono::steady_clock::now() - wake_start;
    waker.join();
    assert(wake_elapsed >= std::chrono::milliseconds(10));
    assert(wake_elapsed < std::chrono::milliseconds(500));

    const auto timeout_start = std::chrono::steady_clock::now();
    assert(nk_wait_events_timeout(0.04) == NK_OK);
    const auto timeout_elapsed = std::chrono::steady_clock::now() - timeout_start;
    assert(timeout_elapsed >= std::chrono::milliseconds(25));
    assert(timeout_elapsed < std::chrono::milliseconds(300));
    assert(nk_wait_events_timeout(-1.0) == NK_ERROR_INVALID_ARGUMENT);

    nk_result wrong_thread = NK_OK;
    std::thread wrong([&] { wrong_thread = nk_wait_events_timeout(0.0); });
    wrong.join();
    assert(wrong_thread == NK_ERROR_WRONG_THREAD);
    nk_shutdown();
    return 0;
}
