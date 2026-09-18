#include "nativekit_file_watch.h"
#include "nativekit_window.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int has_path(const nk_event *event, const char *path) {
    if (!event->data || event->data_size < sizeof(nk_file_changed_event))
        return 0;
    nk_file_changed_event payload;
    memcpy(&payload, event->data, sizeof(payload));
    if (payload.path_offset == 0 || payload.path_length != strlen(path) ||
        payload.path_offset + payload.path_length > event->data_size)
        return 0;
    return memcmp((const char *)event->data + payload.path_offset, path, payload.path_length) == 0;
}

static int has_rename(const nk_event *event, const char *path, const char *old_path) {
    if (event->kind != NK_EVENT_FILE_CHANGED || !event->data ||
        event->data_size < sizeof(nk_file_changed_event))
        return 0;
    nk_file_changed_event payload;
    memcpy(&payload, event->data, sizeof(payload));
    if (payload.change_type != NK_FILE_CHANGE_MOVED || payload.old_path_offset == 0 ||
        payload.path_length != strlen(path) || payload.old_path_length != strlen(old_path) ||
        payload.path_offset + payload.path_length > event->data_size ||
        payload.old_path_offset + payload.old_path_length > event->data_size)
        return 0;
    const int new_path_matches =
        memcmp((const char *)event->data + payload.path_offset, path, payload.path_length) == 0;
    const int old_path_matches = memcmp((const char *)event->data + payload.old_path_offset,
                                        old_path, payload.old_path_length) == 0;
    return new_path_matches && old_path_matches;
}

int main(void) {
    char directory[] = "/tmp/nativekit-file-watch-XXXXXX";
    assert(mkdtemp(directory) != NULL);
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    if (!(nk_get_capabilities() & NK_CAP_FILE_WATCH)) {
        nk_shutdown();
        rmdir(directory);
        return 77;
    }
    nk_file_watch_options options = {0};
    options.struct_size = sizeof(options);
    nk_file_watch watch = NK_INVALID_HANDLE;
    assert(nk_file_watch_create(&options, &watch) == NK_OK);
    assert(nk_file_watch_add_directory(watch, directory, 1) == NK_OK);

    char path[512];
    snprintf(path, sizeof(path), "%s/item.txt", directory);
    int descriptor = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0600);
    assert(descriptor >= 0);
    assert(write(descriptor, "watch", 5) == 5);
    close(descriptor);

    int received = 0;
    for (int attempt = 0; attempt < 100 && !received; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_FILE_CHANGED && has_path(&event, path))
            received = 1;
        nk_event_release(&event);
        usleep(10000);
    }
    assert(received);
    char renamed_path[512];
    snprintf(renamed_path, sizeof(renamed_path), "%s/renamed.txt", directory);
    assert(rename(path, renamed_path) == 0);
    int renamed = 0;
    for (int attempt = 0; attempt < 100 && !renamed; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        renamed = has_rename(&event, renamed_path, path);
        nk_event_release(&event);
        usleep(10000);
    }
    assert(renamed);
    assert(nk_file_watch_destroy(watch) == NK_OK);
    assert(nk_file_watch_destroy(watch) == NK_ERROR_INVALID_HANDLE);
    nk_shutdown();
    unlink(path);
    unlink(renamed_path);
    rmdir(directory);
    return 0;
}
