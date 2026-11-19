#import <Cocoa/Cocoa.h>
#include "nativekit.h"
#include "nativekit_window.h"
#include <cstdio>

int main() {
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
    bool close_requested = false;
    for (int i = 0; i < 100; ++i) {
        nk_event event{};
        event.struct_size = sizeof(event);
        nk_poll_event(&event);
        close_requested |= event.kind == NK_EVENT_WINDOW_CLOSE && event.source == window;
        nk_event_release(&event);
        if (close_requested) break;
    }
    nk_window_destroy(window);
    nk_shutdown();
    return close_requested ? 0 : 4;
}
