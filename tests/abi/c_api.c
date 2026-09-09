#include "nativekit.h"
#include "nativekit_dialog.h"
#include "nativekit_clipboard.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"
#include "nativekit_system.h"

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
    uint32_t home_size = 0;
    nk_result home_result = nk_system_directory(NK_DIRECTORY_HOME, NULL, &home_size);
    assert(home_result == NK_ERROR_BUFFER_TOO_SMALL || home_result == NK_ERROR_UNSUPPORTED);
    if (home_result == NK_ERROR_BUFFER_TOO_SMALL) {
        assert(home_size > 1);
        char home[4096];
        uint32_t home_capacity = sizeof(home);
        assert(nk_system_directory(NK_DIRECTORY_HOME, home, &home_capacity) == NK_OK);
        assert(home[0] != '\0');
    }
    uint32_t locale_size = 0;
    nk_result locale_result = nk_system_locale(NULL, &locale_size);
    assert(locale_result == NK_ERROR_BUFFER_TOO_SMALL || locale_result == NK_ERROR_UNSUPPORTED);
    if (locale_result == NK_ERROR_BUFFER_TOO_SMALL) assert(locale_size > 1);
    struct {
        nk_clipboard_files header;
        char path[10];
    } clipboard_data = {{1, sizeof(nk_clipboard_files)}, "/tmp/file"};
    nk_event clipboard_event = {0};
    clipboard_event.struct_size = sizeof(clipboard_event);
    clipboard_event.kind = NK_EVENT_CLIPBOARD_FILES_COMPLETE;
    clipboard_event.data = &clipboard_data;
    clipboard_event.data_size = sizeof(clipboard_data);
    const char *clipboard_path = NULL;
    uint32_t clipboard_path_length = 0;
    assert(nk_clipboard_event_file(&clipboard_event, 0, &clipboard_path,
                                   &clipboard_path_length) == NK_OK);
    assert(clipboard_path_length == 9);
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
