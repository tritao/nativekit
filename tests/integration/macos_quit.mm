#import <Cocoa/Cocoa.h>
#include "nativekit.h"
#include "nativekit_window.h"
#include <cstdio>
#include <cstdlib>
#include <unistd.h>

namespace {
bool finished = false;

// [NSApp terminate:] exits the process when quit is not routed to the host. That must fail the
// test, not look like a clean pass.
void fail_on_unexpected_exit() {
    if (!finished) {
        std::fprintf(stderr, "Cocoa quit exited the process instead of asking the host\n");
        _exit(5);
    }
}
} // namespace

int main() {
    std::atexit(fail_on_unexpected_exit);
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK) return 1;
    nk_window_options options{};
    options.struct_size = sizeof(options);
    options.width = 160;
    options.height = 120;
    options.flags = NK_WINDOW_HIDDEN;
    options.title = "NativeKit quit regression";
    nk_window window = NK_INVALID_HANDLE;
    if (nk_window_create(&options, &window) != NK_OK) { nk_shutdown(); return 2; }
    if (![[NSApp delegate] respondsToSelector:@selector(applicationShouldTerminate:)]) {
        std::fprintf(stderr, "Cocoa quit must return to NativeKit for orderly shutdown\n");
        nk_window_destroy(window);
        nk_shutdown();
        return 3;
    }
    [NSApp terminate:nil];
    bool quit_requested = false;
    for (int i = 0; i < 100 && !quit_requested; ++i) {
        nk_event event{};
        event.struct_size = sizeof(event);
        nk_poll_event(&event);
        quit_requested = event.kind == NK_EVENT_APPLICATION_QUIT_REQUESTED;
        nk_event_release(&event);
    }
    nk_window_destroy(window);
    nk_shutdown();
    finished = true;
    return quit_requested ? 0 : 4;
}
