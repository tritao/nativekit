#include "nativekit_file_watch.h"
#include "nativekit_window.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int make_temporary_directory(char *directory, size_t size) {
    char base[MAX_PATH];
    const DWORD length = GetTempPathA(MAX_PATH, base);
    if (!length || length >= MAX_PATH)
        return 0;
    snprintf(directory, size, "%snativekit-file-watch-%lu-%lu", base, GetCurrentProcessId(),
             GetTickCount());
    return CreateDirectoryA(directory, NULL) != 0;
}

static void sleep_briefly(void) {
    Sleep(10);
}
static void remove_directory(const char *path) {
    RemoveDirectoryA(path);
}
#else
#include <sys/stat.h>
#include <unistd.h>

static int make_temporary_directory(char *directory, size_t size) {
    snprintf(directory, size, "/tmp/nativekit-file-watch-XXXXXX");
    return mkdtemp(directory) != NULL;
}

static void sleep_briefly(void) {
    usleep(10000);
}
static void remove_directory(const char *path) {
    rmdir(path);
}
#endif

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

static int make_child_directory(const char *parent, const char *name, char *out, size_t size) {
    snprintf(out, size, "%s/%s", parent, name);
#ifdef _WIN32
    return CreateDirectoryA(out, NULL) != 0;
#else
    return mkdir(out, 0700) == 0;
#endif
}

static void write_file(const char *path) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite("watch", 1, 5, file) == 5);
    fclose(file);
}

/* Polls until an event of `kind` names `path`, or `attempts` polls pass; counts events naming
 * `unwanted`. */
static int await_path(nk_event_kind kind, const char *path, int attempts, const char *unwanted,
                      int *unwanted_seen) {
    for (int attempt = 0; attempt < attempts; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        const int found = event.kind == kind && path && has_path(&event, path);
        if (unwanted && unwanted_seen && event.kind == NK_EVENT_FILE_CHANGED &&
            has_path(&event, unwanted))
            ++*unwanted_seen;
        nk_event_release(&event);
        if (found)
            return 1;
        sleep_briefly();
    }
    return 0;
}

/* A recursive add reports READY for its directory once everything under it is watched, then reports
 * changes deep in the tree; an excluded subtree reports nothing, including a directory created in
 * it afterwards. */
static void recursive_ready_and_exclusion(const char *root) {
    char tree[512], a[512], b[512], skip[512], inner[512];
    assert(make_child_directory(root, "tree", tree, sizeof(tree)));
    assert(make_child_directory(tree, "a", a, sizeof(a)));
    assert(make_child_directory(a, "b", b, sizeof(b)));
    assert(make_child_directory(tree, "skip", skip, sizeof(skip)));
    assert(make_child_directory(skip, "inner", inner, sizeof(inner)));
    nk_file_watch_options options = {0};
    options.struct_size = sizeof(options);
    nk_file_watch watch = NK_INVALID_HANDLE;
    assert(nk_file_watch_create(&options, &watch) == NK_OK);
    assert(nk_file_watch_exclude_directory(watch, skip) == NK_OK);
    assert(nk_file_watch_add_directory(watch, tree, 1) == NK_OK);
    assert(await_path(NK_EVENT_FILE_WATCH_READY, tree, 200, NULL, NULL));

    char deep[600], hidden[600], created[600], created_file[700];
    snprintf(deep, sizeof(deep), "%s/deep.txt", b);
    snprintf(hidden, sizeof(hidden), "%s/hidden.txt", inner);
    write_file(hidden);
    assert(make_child_directory(skip, "created", created, sizeof(created)));
    snprintf(created_file, sizeof(created_file), "%s/later.txt", created);
    write_file(created_file);
    write_file(deep);
    int unwanted = 0;
    assert(await_path(NK_EVENT_FILE_CHANGED, deep, 100, hidden, &unwanted));
    /* Changes in the excluded subtree would have arrived before the later write's. */
    assert(unwanted == 0);
    int created_seen = 0;
    (void)await_path(NK_EVENT_FILE_CHANGED, NULL, 20, created_file, &created_seen);
    assert(created_seen == 0);
    assert(nk_file_watch_destroy(watch) == NK_OK);
    remove(deep);
    remove(hidden);
    remove(created_file);
    remove_directory(created);
    remove_directory(inner);
    remove_directory(skip);
    remove_directory(b);
    remove_directory(a);
    remove_directory(tree);
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
    char directory[256];
    assert(make_temporary_directory(directory, sizeof(directory)));
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    if (!(nk_get_capabilities() & NK_CAP_FILE_WATCH)) {
        nk_shutdown();
        remove_directory(directory);
        return 77;
    }
    nk_file_watch_options options = {0};
    options.struct_size = sizeof(options);
    nk_file_watch watch = NK_INVALID_HANDLE;
    assert(nk_file_watch_create(&options, &watch) == NK_OK);
    assert(nk_file_watch_add_directory(watch, directory, 1) == NK_OK);

    char path[512];
    snprintf(path, sizeof(path), "%s/item.txt", directory);
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fwrite("watch", 1, 5, file) == 5);
    fclose(file);

    int received = 0;
    for (int attempt = 0; attempt < 100 && !received; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_FILE_CHANGED && has_path(&event, path))
            received = 1;
        nk_event_release(&event);
        sleep_briefly();
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
        sleep_briefly();
    }
    assert(renamed);
    assert(nk_file_watch_destroy(watch) == NK_OK);
    assert(nk_file_watch_destroy(watch) == NK_ERROR_INVALID_HANDLE);
    recursive_ready_and_exclusion(directory);
    nk_shutdown();
    remove(path);
    remove(renamed_path);
    remove_directory(directory);
    return 0;
}
