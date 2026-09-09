#include "nativekit.h"
#include "nativekit_notification.h"

#include <assert.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    nk_notification_options options = {0};
    options.struct_size = sizeof(options);
    options.title = "NativeKit notification test";
    options.body = "A missing desktop service must complete with failure.";
    nk_request_id request = NK_INVALID_REQUEST_ID;
    assert(nk_notification_show(&options, &request) == NK_OK);
    assert(request != NK_INVALID_REQUEST_ID);

    int failed = 0;
    for (int attempt = 0; attempt < 500 && !failed; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_NOTIFICATION_FAILED) {
            assert(event.request_id == request);
            assert(event.result != NK_OK);
            assert(event.data && event.data_size > 0);
            failed = 1;
        }
        nk_event_release(&event);
        if (!failed)
            usleep(10000);
    }
    assert(failed);
    assert(nk_notification_close(request) == NK_ERROR_INVALID_REQUEST);
    nk_shutdown();
    return 0;
}
