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

stamps::Input move(std::uint32_t time_ms, int x, int y) {
    return *stamps::mouse_input(WM_MOUSEMOVE, 0, x, y, time_ms);
}

stamps::Input button(UINT message, std::uint32_t xbutton, std::uint32_t time_ms, int x, int y) {
    return *stamps::mouse_input(message, xbutton, x, y, time_ms);
}

stamps::Input key(UINT message, std::uint32_t scan_code, bool extended, std::uint32_t time_ms) {
    return *stamps::key_input(message, scan_code, extended, time_ms);
}

void the_hook_and_the_window_describe_an_input_alike() {
    // A double click is the press the hook saw.
    const auto press = button(WM_LBUTTONDOWN, 0, 100, 5, 6);
    const auto double_click = button(WM_LBUTTONDBLCLK, 0, 100, 5, 6);
    assert(press.kind == stamps::Kind::button && double_click.code == press.code);
    assert(button(WM_XBUTTONDBLCLK, XBUTTON2, 100, 5, 6).code ==
           button(WM_XBUTTONDOWN, XBUTTON2, 100, 5, 6).code);
    // The X button counts only for X button messages.
    assert(button(WM_XBUTTONDOWN, XBUTTON1, 100, 5, 6).code !=
           button(WM_XBUTTONDOWN, XBUTTON2, 100, 5, 6).code);
    assert(button(WM_LBUTTONDOWN, XBUTTON2, 100, 5, 6).code == press.code);
    assert(button(WM_LBUTTONUP, 0, 100, 5, 6).code != press.code);
    // Alt turns a key message into its SYS form; both are the same transition.
    assert(key(WM_SYSKEYDOWN, 0x1e, false, 1).code == key(WM_KEYDOWN, 0x1e, false, 1).code);
    assert(key(WM_KEYUP, 0x1e, false, 1).code != key(WM_KEYDOWN, 0x1e, false, 1).code);
    assert(key(WM_KEYDOWN, 0x1d, true, 1).code != key(WM_KEYDOWN, 0x1d, false, 1).code);
    // Each describes only its own messages.
    assert(!stamps::mouse_input(WM_KEYDOWN, 0, 0, 0, 1));
    assert(!stamps::key_input(WM_MOUSEMOVE, 0, false, 1));
    assert(!stamps::mouse_input(WM_MOUSEWHEEL, 0, 0, 0, 1));
    assert(stamps::is_input_message(WM_RBUTTONDBLCLK) && stamps::is_input_message(WM_SYSKEYUP));
    assert(!stamps::is_input_message(WM_MOUSEWHEEL) && !stamps::is_input_message(WM_CHAR));
}

void moves_match_the_newest_at_their_time() {
    stamps::StampRing ring;
    ring.record({move(100, 10, 20), 1000});
    ring.record({move(100, 11, 20), 2000});
    ring.record({move(100, 10, 20), 3000}); // back where it was within the same tick
    ring.record({move(116, 12, 20), 4000});
    assert(ring.match(move(100, 10, 20)) == 3000);
    // A move is not used up: the system can deliver the same position again.
    assert(ring.match(move(100, 10, 20)) == 3000);
    assert(ring.match(move(100, 11, 20)) == 2000);
    assert(ring.match(move(116, 12, 20)) == 4000);
    // The hook saw the position before the cursor was clipped: the newest at that time.
    assert(ring.match(move(100, 0, 20)) == 3000);
    // Another tick or kind: no stamp, so the caller falls back to the tick.
    assert(ring.match(move(101, 10, 20)) == 0);
    assert(ring.match(button(WM_LBUTTONDOWN, 0, 100, 10, 20)) == 0);
}

void keys_match_the_oldest_unmatched_once_each() {
    stamps::StampRing ring;
    // Auto-repeat: three presses of the same key inside one tick.
    ring.record({key(WM_KEYDOWN, 0x1e, false, 200), 1000});
    ring.record({key(WM_KEYDOWN, 0x1e, false, 200), 1500});
    ring.record({key(WM_KEYUP, 0x1e, false, 200), 1700});
    ring.record({key(WM_KEYDOWN, 0x1e, false, 200), 1900});
    assert(ring.match(key(WM_KEYDOWN, 0x1e, false, 200)) == 1000);
    assert(ring.match(key(WM_KEYDOWN, 0x1e, false, 200)) == 1500);
    assert(ring.match(key(WM_KEYUP, 0x1e, false, 200)) == 1700);
    assert(ring.match(key(WM_KEYDOWN, 0x1e, false, 200)) == 1900);
    assert(ring.match(key(WM_KEYDOWN, 0x1e, false, 200)) == 0);
    // The extended flag tells right Ctrl from left Ctrl.
    ring.record({key(WM_KEYDOWN, 0x1d, true, 216), 2000});
    assert(ring.match(key(WM_KEYDOWN, 0x1d, false, 216)) == 0);
    assert(ring.match(key(WM_KEYDOWN, 0x1d, true, 216)) == 2000);
}

void buttons_prefer_their_position() {
    stamps::StampRing ring;
    ring.record({button(WM_LBUTTONDOWN, 0, 300, 9, 9), 900});
    ring.record({button(WM_LBUTTONDOWN, 0, 300, 5, 6), 1000});
    ring.record({button(WM_XBUTTONDOWN, XBUTTON2, 300, 5, 6), 1100});
    assert(ring.match(button(WM_XBUTTONDOWN, XBUTTON1, 300, 5, 6)) == 0);
    assert(ring.match(button(WM_XBUTTONDOWN, XBUTTON2, 300, 5, 6)) == 1100);
    // The one at the message's position, though an older one at another position is unmatched.
    assert(ring.match(button(WM_LBUTTONDOWN, 0, 300, 5, 6)) == 1000);
    // Elsewhere (clipped or scaled): the oldest left at that time.
    assert(ring.match(button(WM_LBUTTONDOWN, 0, 300, 1, 1)) == 900);
    assert(ring.match(button(WM_LBUTTONDOWN, 0, 300, 5, 6)) == 0);
}

void the_ring_forgets_the_oldest_and_its_matches() {
    auto ring = std::make_unique<stamps::StampRing>();
    assert(!ring->newest_time_ms());
    for (std::uint32_t i = 0; i < stamps::StampRing::capacity + 10; ++i)
        ring->record({key(WM_KEYDOWN, 0x1e, false, i), 1000 + i});
    const auto last = static_cast<std::uint32_t>(stamps::StampRing::capacity + 9);
    assert(ring->newest_time_ms() == last);
    assert(ring->match(key(WM_KEYDOWN, 0x1e, false, 9)) == 0);
    assert(ring->match(key(WM_KEYDOWN, 0x1e, false, 10)) == 1010);
    assert(ring->match(key(WM_KEYDOWN, 0x1e, false, last)) == 1000 + last);
    // A slot reused for a newer stamp is not still marked as matched.
    for (std::uint32_t i = 0; i < stamps::StampRing::capacity; ++i)
        ring->record({key(WM_KEYDOWN, 0x1e, false, 5000 + i), 7000 + i});
    assert(ring->match(key(WM_KEYDOWN, 0x1e, false, 5010)) == 7010);
}

void tick_times_wrap() {
    stamps::StampRing ring;
    ring.record({key(WM_KEYDOWN, 0x1e, false, 0xfffffff0u), 1000});
    ring.record({key(WM_KEYDOWN, 0x1e, false, 0x10u), 2000});
    assert(ring.match(key(WM_KEYDOWN, 0x1e, false, 0xfffffff0u)) == 1000);
    assert(ring.match(key(WM_KEYDOWN, 0x1e, false, 0x10u)) == 2000);
}

/* Installs and removes the real hooks. Sends no input; whatever the desktop's own input records
   cannot match the ticks used here. */
void the_hooks_follow_tracing() {
    assert(!stamps::running());
    stamps::update(false);
    assert(!stamps::running());
    stamps::update(true);
    assert(stamps::running());
    const auto starts = stamps::hook_starts();
    stamps::update(true);
    assert(stamps::running() && stamps::hook_starts() == starts);
    // Tracing stops: so do the hooks, and they start again with it.
    stamps::update(false);
    assert(!stamps::running());
    assert(stamps::match(key(WM_KEYDOWN, 0x1e, false, 1)) == 0);
    stamps::update(true);
    assert(stamps::running() && stamps::hook_starts() == starts + 1);
    stamps::shutdown();
    assert(!stamps::running());
    stamps::shutdown();
    // It starts again after a shutdown, as after nk_shutdown() and a second nk_init().
    stamps::update(true);
    assert(stamps::running());
    stamps::shutdown();

    SetEnvironmentVariableW(L"NK_INPUT_STAMP_HOOK", L"0");
    stamps::update(true);
    assert(!stamps::running());
    stamps::shutdown();
    SetEnvironmentVariableW(L"NK_INPUT_STAMP_HOOK", nullptr);
}

/* A key whose tick is past every stamp means Windows removed a hook; the hooks are reinstalled, but
   not more than once a second. */
void lost_hooks_are_reinstalled() {
    stamps::update(true);
    assert(stamps::running());
    const auto starts = stamps::hook_starts();
    const auto future = static_cast<std::uint32_t>(GetTickCount()) + 600000u;
    assert(stamps::match(key(WM_KEYDOWN, 0x1e, false, future)) == 0);
    stamps::update(true);
    assert(stamps::hook_starts() == starts);
    Sleep(1100);
    stamps::update(true);
    assert(stamps::running() && stamps::hook_starts() == starts + 1);
    // A move no hook saw is not taken for a lost hook: the system makes some up.
    assert(stamps::match(move(future, 0, 0)) == 0);
    Sleep(1100);
    stamps::update(true);
    assert(stamps::hook_starts() == starts + 1);
    stamps::shutdown();
}

} // namespace

int main() {
    the_hook_and_the_window_describe_an_input_alike();
    moves_match_the_newest_at_their_time();
    keys_match_the_oldest_unmatched_once_each();
    buttons_prefer_their_position();
    the_ring_forgets_the_oldest_and_its_matches();
    tick_times_wrap();
    the_hooks_follow_tracing();
    lost_hooks_are_reinstalled();
    std::puts("input stamp tests passed");
    return 0;
}
