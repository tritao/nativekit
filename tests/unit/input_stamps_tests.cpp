#include "windows/input_stamps.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cassert>
#include <cstdio>
#include <memory>

namespace stamps = nk::windows_input_stamps;

namespace {

stamps::Stamp move(std::uint32_t time_ms, int x, int y, std::uint64_t ns) {
    stamps::Stamp stamp;
    stamp.kind = stamps::Kind::move;
    stamp.time_ms = time_ms;
    stamp.x = x;
    stamp.y = y;
    stamp.time_ns = ns;
    return stamp;
}

stamps::Stamp key(std::uint32_t time_ms, std::uint32_t code, std::uint64_t ns) {
    stamps::Stamp stamp;
    stamp.kind = stamps::Kind::key;
    stamp.time_ms = time_ms;
    stamp.code = code;
    stamp.time_ns = ns;
    return stamp;
}

stamps::Stamp button(std::uint32_t time_ms, int x, int y, std::uint32_t code, std::uint64_t ns) {
    stamps::Stamp stamp = move(time_ms, x, y, ns);
    stamp.kind = stamps::Kind::button;
    stamp.code = code;
    return stamp;
}

void moves_match_time_and_position_newest_first() {
    stamps::Log log;
    log.record(move(100, 10, 20, 1000));
    log.record(move(100, 11, 20, 2000));
    log.record(move(100, 10, 20, 3000)); // back where it was within the same tick
    log.record(move(116, 12, 20, 4000));
    assert(log.take(stamps::Kind::move, 100, 10, 20, 0) == 3000);
    // A move is not used up: the system can deliver the same position again.
    assert(log.take(stamps::Kind::move, 100, 10, 20, 0) == 3000);
    assert(log.take(stamps::Kind::move, 100, 11, 20, 0) == 2000);
    assert(log.take(stamps::Kind::move, 116, 12, 20, 0) == 4000);
    // Wrong tick, wrong position, or another kind: no stamp, so the caller falls back.
    assert(log.take(stamps::Kind::move, 101, 10, 20, 0) == 0);
    assert(log.take(stamps::Kind::move, 100, 13, 20, 0) == 0);
    assert(log.take(stamps::Kind::button, 100, 10, 20, 0) == 0);
}

void keys_are_taken_oldest_first_once_each() {
    stamps::Log log;
    const auto a_down = stamps::key_code(0x1e, false, false);
    const auto a_up = stamps::key_code(0x1e, false, true);
    // Auto-repeat: three presses of the same key inside one tick.
    log.record(key(200, a_down, 1000));
    log.record(key(200, a_down, 1500));
    log.record(key(200, a_up, 1700));
    log.record(key(200, a_down, 1900));
    assert(log.take(stamps::Kind::key, 200, 0, 0, a_down) == 1000);
    assert(log.take(stamps::Kind::key, 200, 0, 0, a_down) == 1500);
    assert(log.take(stamps::Kind::key, 200, 0, 0, a_up) == 1700);
    assert(log.take(stamps::Kind::key, 200, 0, 0, a_down) == 1900);
    assert(log.take(stamps::Kind::key, 200, 0, 0, a_down) == 0);
    // The extended flag tells right Ctrl from left Ctrl.
    log.record(key(216, stamps::key_code(0x1d, true, false), 2000));
    assert(log.take(stamps::Kind::key, 216, 0, 0, stamps::key_code(0x1d, false, false)) == 0);
    assert(log.take(stamps::Kind::key, 216, 0, 0, stamps::key_code(0x1d, true, false)) == 2000);
}

void buttons_match_code_and_position() {
    stamps::Log log;
    const auto left_down = stamps::button_code(WM_LBUTTONDOWN, 0);
    const auto x1_down = stamps::button_code(WM_XBUTTONDOWN, XBUTTON1);
    const auto x2_down = stamps::button_code(WM_XBUTTONDOWN, XBUTTON2);
    log.record(button(300, 5, 6, left_down, 1000));
    log.record(button(300, 5, 6, x2_down, 1100));
    assert(log.take(stamps::Kind::button, 300, 5, 6, x1_down) == 0);
    assert(log.take(stamps::Kind::button, 300, 5, 7, left_down) == 0);
    assert(log.take(stamps::Kind::button, 300, 5, 6, x2_down) == 1100);
    assert(log.take(stamps::Kind::button, 300, 5, 6, left_down) == 1000);
    assert(log.take(stamps::Kind::button, 300, 5, 6, left_down) == 0);
}

void the_ring_forgets_the_oldest() {
    auto log = std::make_unique<stamps::Log>();
    for (std::uint32_t i = 0; i < stamps::Log::capacity + 10; ++i)
        log->record(move(i, 0, 0, 1000 + i));
    assert(log->take(stamps::Kind::move, 9, 0, 0, 0) == 0);
    assert(log->take(stamps::Kind::move, 10, 0, 0, 0) == 1010);
    const auto last = static_cast<std::uint32_t>(stamps::Log::capacity + 9);
    assert(log->take(stamps::Kind::move, last, 0, 0, 0) == 1000 + last);
}

/* Installs and removes the real hooks. Sends no input: a stamp lookup for an input no hook saw
   comes back empty. */
void the_hook_thread_starts_and_stops() {
    stamps::ensure_started();
    assert(stamps::active());
    stamps::ensure_started();
    assert(stamps::active());
    assert(stamps::take(stamps::Kind::key, 1, 0, 0, 0) == 0);
    stamps::shutdown();
    assert(!stamps::active());
    assert(stamps::take(stamps::Kind::key, 1, 0, 0, 0) == 0);
    stamps::shutdown();
    // It starts again after a shutdown, as after nk_shutdown() and a second nk_init().
    stamps::ensure_started();
    assert(stamps::active());
    stamps::shutdown();

    SetEnvironmentVariableW(L"NK_INPUT_STAMP_HOOK", L"0");
    stamps::ensure_started();
    assert(!stamps::active());
    stamps::shutdown();
    SetEnvironmentVariableW(L"NK_INPUT_STAMP_HOOK", nullptr);
}

} // namespace

int main() {
    moves_match_time_and_position_newest_first();
    keys_are_taken_oldest_first_once_each();
    buttons_match_code_and_position();
    the_ring_forgets_the_oldest();
    the_hook_thread_starts_and_stops();
    std::puts("input stamp tests passed");
    return 0;
}
