#include "nativekit_clipboard.h"
#include "nativekit_window.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    if (!(nk_get_capabilities() & NK_CAP_CLIPBOARD_WATCH)) {
        nk_shutdown();
        return 77;
    }
    nk_clipboard_watch_options options = {0};
    options.struct_size = sizeof(options);
    nk_clipboard_watch watch = NK_INVALID_HANDLE;
    assert(nk_clipboard_watch_start(&options, &watch) == NK_OK);
    assert(nk_clipboard_set_text("NativeKit clipboard watcher") == NK_OK);
    int received = 0;
    for (int attempt = 0; attempt < 100 && !received; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_CLIPBOARD_CHANGED) {
            assert(event.data_size == sizeof(nk_clipboard_changed_event));
            const nk_clipboard_changed_event *payload =
                (const nk_clipboard_changed_event *)event.data;
            assert(payload->sequence != 0);
            received = 1;
        }
        nk_event_release(&event);
        usleep(10000);
    }
    assert(received);
    assert(nk_clipboard_watch_stop(watch) == NK_OK);
    assert(nk_clipboard_watch_stop(watch) == NK_ERROR_INVALID_HANDLE);
    nk_shutdown();
    return 0;
}
