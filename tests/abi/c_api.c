#include "nativekit.h"
#include "nativekit_dialog.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <assert.h>
#include <string.h>

int main(void) {
    nk_init_options options = {0};
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    assert(nk_api_version() == NK_API_VERSION);
    assert(nk_init(&options) == NK_OK);
    (void)nk_get_capabilities();
    assert(nk_dialog_event_path(NULL, 0, NULL, NULL) == NK_ERROR_INVALID_ARGUMENT);
    struct {
        nk_dialog_paths header;
        uint32_t offset;
        char path[4];
    } packed = {{1, 1, sizeof(nk_dialog_paths),
                 sizeof(nk_dialog_paths) + sizeof(uint32_t)},
                sizeof(nk_dialog_paths) + sizeof(uint32_t), "abc"};
    nk_event packed_event = {0};
    packed_event.struct_size = sizeof(packed_event);
    packed_event.kind = NK_EVENT_DIALOG_COMPLETE;
    packed_event.data = &packed;
    packed_event.data_size = sizeof(packed);
    const char *decoded_path = NULL;
    uint32_t decoded_length = 0;
    assert(nk_dialog_event_path(&packed_event, 0, &decoded_path, &decoded_length) == NK_OK);
    assert(decoded_length == 3);
    assert(memcmp(decoded_path, "abc", 3) == 0);
    assert(nk_init(&options) == NK_ERROR_ALREADY_INITIALIZED);
    assert(strlen(nk_last_error()) > 0);

    nk_event event = {0};
    event.struct_size = sizeof(event);
    assert(nk_poll_event(&event) == NK_OK);
    assert(event.kind == NK_EVENT_NONE);
    nk_event_release(&event);

    nk_shutdown();
    assert(nk_poll_event(&event) == NK_ERROR_NOT_INITIALIZED);
    return 0;
}
