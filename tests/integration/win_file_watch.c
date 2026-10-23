#include "nativekit_file_watch.h"
#include "nativekit_window.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Windows file-watch contract: change kinds and paths, registration scoping,
 * ancestor renames, re-arm cost, lost directories, floods and teardown. */

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);          \
            exit(1);                                                                               \
        }                                                                                          \
    } while (0)

typedef struct record {
    nk_event_kind kind;
    nk_file_watch source;
    nk_file_changed_event payload;
    char *path;
    char *old_path;
    double seconds;
} record;

static record *records;
static size_t record_count;
static size_t record_capacity;
static LARGE_INTEGER frequency;

static double now(void) {
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
}

static char *copy_string(const char *data, uint32_t offset, uint32_t length, size_t size) {
    if (!offset || offset + length > size)
        return NULL;
    char *result = malloc(length + 1u);
    CHECK(result != NULL);
    memcpy(result, data + offset, length);
    result[length] = '\0';
    return result;
}

static void clear_records(void) {
    for (size_t index = 0; index < record_count; ++index) {
        free(records[index].path);
        free(records[index].old_path);
    }
    record_count = 0;
}

/* Moves every queued file-watch event into the record log. */
static void pump(void) {
    for (;;) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        CHECK(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_NONE) {
            nk_event_release(&event);
            return;
        }
        if ((event.kind == NK_EVENT_FILE_CHANGED || event.kind == NK_EVENT_FILE_WATCH_OVERFLOW) &&
            event.data && event.data_size >= sizeof(nk_file_changed_event)) {
            if (record_count == record_capacity) {
                record_capacity = record_capacity ? record_capacity * 2 : 256;
                records = realloc(records, record_capacity * sizeof(record));
                CHECK(records != NULL);
            }
            record *entry = &records[record_count++];
            entry->kind = event.kind;
            entry->source = (nk_file_watch)event.source;
            memcpy(&entry->payload, event.data, sizeof(entry->payload));
            entry->path = copy_string(event.data, entry->payload.path_offset,
                                      entry->payload.path_length, event.data_size);
            entry->old_path = copy_string(event.data, entry->payload.old_path_offset,
                                          entry->payload.old_path_length, event.data_size);
            entry->seconds = now();
        }
        nk_event_release(&event);
    }
}

static const record *find(nk_file_change_type change, const char *path, const char *old_path) {
    for (size_t index = 0; index < record_count; ++index) {
        const record *entry = &records[index];
        if (entry->kind == NK_EVENT_FILE_CHANGED && entry->payload.change_type == change &&
            entry->path && strcmp(entry->path, path) == 0 &&
            (!old_path || (entry->old_path && strcmp(entry->old_path, old_path) == 0)))
            return entry;
    }
    return NULL;
}

static const record *find_with_prefix(const char *prefix) {
    const size_t length = strlen(prefix);
    for (size_t index = 0; index < record_count; ++index)
        if (records[index].path && strncmp(records[index].path, prefix, length) == 0)
            return &records[index];
    return NULL;
}

static int has_overflow(void) {
    for (size_t index = 0; index < record_count; ++index)
        if (records[index].kind == NK_EVENT_FILE_WATCH_OVERFLOW)
            return 1;
    return 0;
}

static const record *await(nk_file_change_type change, const char *path, const char *old_path) {
    const double deadline = now() + 5.0;
    for (;;) {
        pump();
        const record *found = find(change, path, old_path);
        if (found || now() > deadline)
            return found;
        nk_wait_events_timeout(0.05);
    }
}

/* Pumps for a quiet period so stray events from a step can be cleared. */
static void settle(double seconds) {
    const double deadline = now() + seconds;
    while (now() < deadline) {
        pump();
        nk_wait_events_timeout(0.02);
    }
}

static wchar_t *widen(const char *text) {
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *result = malloc((size_t)size * sizeof(wchar_t));
    CHECK(result != NULL);
    MultiByteToWideChar(CP_UTF8, 0, text, -1, result, size);
    return result;
}

static void make_directory(const char *path) {
    wchar_t *wide = widen(path);
    CHECK(CreateDirectoryW(wide, NULL));
    free(wide);
}

static void write_file(const char *path, const char *contents) {
    wchar_t *wide = widen(path);
    HANDLE file =
        CreateFileW(wide, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    CHECK(file != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    CHECK(WriteFile(file, contents, (DWORD)strlen(contents), &written, NULL));
    CloseHandle(file);
    free(wide);
}

static BOOL move_path(const char *from, const char *to) {
    wchar_t *wide_from = widen(from), *wide_to = widen(to);
    const BOOL result = MoveFileW(wide_from, wide_to);
    free(wide_from);
    free(wide_to);
    return result;
}

static void delete_file(const char *path) {
    wchar_t *wide = widen(path);
    CHECK(DeleteFileW(wide));
    free(wide);
}

static void remove_tree(const char *path) {
    char native[1000], command[1200];
    snprintf(native, sizeof(native), "%s", path);
    for (char *character = native; *character; ++character)
        if (*character == '/')
            *character = '\\';
    snprintf(command, sizeof(command), "cmd /c rmdir /s /q \"%s\" 2>nul", native);
    (void)system(command);
}

static nk_file_watch create_watch(void) {
    nk_file_watch_options options = {0};
    options.struct_size = sizeof(options);
    nk_file_watch watch = NK_INVALID_HANDLE;
    CHECK(nk_file_watch_create(&options, &watch) == NK_OK);
    return watch;
}

static void join(char *result, size_t size, const char *base, const char *name) {
    snprintf(result, size, "%s/%s", base, name);
}

/* Change kinds, item kinds, renames, nested recursion, UTF-8 names and
 * latency through a forward-slash registration. */
static void check_changes(const char *root) {
    nk_file_watch watch = create_watch();
    CHECK(nk_file_watch_add_directory(watch, root, 1) == NK_OK);
    char file[600], renamed[600], directory[600], nested[600], unicode[600];
    join(file, sizeof(file), root, "item.txt");
    join(renamed, sizeof(renamed), root, "renamed.txt");
    join(directory, sizeof(directory), root, "sub");
    join(nested, sizeof(nested), root, "sub/nested.txt");
    join(unicode, sizeof(unicode), root, "\xC3\xBCnicode-\xE2\x9C\x93.txt");

    const double written = now();
    write_file(file, "one");
    const record *added = await(NK_FILE_CHANGE_ADDED, file, NULL);
    CHECK(added != NULL);
    CHECK(added->payload.item_kind == NK_FILE_ITEM_FILE);
    const double latency = added->seconds - written;
    printf("win_file_watch: change latency %.1f ms\n", latency * 1000.0);
    CHECK(latency < 0.2);

    clear_records();
    write_file(file, "two");
    CHECK(await(NK_FILE_CHANGE_MODIFIED, file, NULL) != NULL);

    CHECK(move_path(file, renamed));
    const record *moved = await(NK_FILE_CHANGE_MOVED, renamed, file);
    CHECK(moved != NULL);
    CHECK(moved->payload.item_kind == NK_FILE_ITEM_FILE);

    delete_file(renamed);
    CHECK(await(NK_FILE_CHANGE_REMOVED, renamed, NULL) != NULL);

    make_directory(directory);
    const record *made = await(NK_FILE_CHANGE_ADDED, directory, NULL);
    CHECK(made != NULL);
    CHECK(made->payload.item_kind == NK_FILE_ITEM_DIRECTORY);
    write_file(nested, "nested");
    CHECK(await(NK_FILE_CHANGE_ADDED, nested, NULL) != NULL);

    write_file(unicode, "utf-8");
    CHECK(await(NK_FILE_CHANGE_ADDED, unicode, NULL) != NULL);

    CHECK(nk_file_watch_destroy(watch) == NK_OK);
    CHECK(nk_file_watch_destroy(watch) == NK_ERROR_INVALID_HANDLE);
    settle(0.05);
    clear_records();
}

/* Non-recursive registrations report direct children and registered
 * directories only. A top handle on the root leaves a subdirectory free to be
 * renamed even while a directory below it is registered, and the registration
 * below the renamed directory then refuses a re-add. */
static void check_scoping(const char *root) {
    char a[600], deep[600], other[600], a_child[600], a_grandchild[600], root_child[600],
        moved_a[600], moved_deep[600];
    join(a, sizeof(a), root, "a");
    join(deep, sizeof(deep), root, "a/b");
    join(other, sizeof(other), root, "a/c");
    join(a_child, sizeof(a_child), root, "a/child.txt");
    join(a_grandchild, sizeof(a_grandchild), root, "a/c/grandchild.txt");
    join(root_child, sizeof(root_child), root, "child.txt");
    join(moved_a, sizeof(moved_a), root, "a-moved");
    join(moved_deep, sizeof(moved_deep), root, "a/b-moved");
    make_directory(a);
    make_directory(deep);
    make_directory(other);

    nk_file_watch watch = create_watch();
    CHECK(nk_file_watch_add_directory(watch, deep, 0) == NK_OK);
    CHECK(nk_file_watch_add_directory(watch, root, 0) == NK_OK);
    CHECK(nk_file_watch_add_directory(watch, a, 0) == NK_OK);

    write_file(a_grandchild, "too deep");
    write_file(a_child, "child of a");
    write_file(root_child, "child of root");
    CHECK(await(NK_FILE_CHANGE_ADDED, a_child, NULL) != NULL);
    CHECK(await(NK_FILE_CHANGE_ADDED, root_child, NULL) != NULL);
    settle(0.2);
    CHECK(find(NK_FILE_CHANGE_ADDED, a_grandchild, NULL) == NULL);

    CHECK(move_path(deep, moved_deep));
    CHECK(await(NK_FILE_CHANGE_MOVED, moved_deep, deep) != NULL);
    CHECK(move_path(moved_deep, deep));
    CHECK(await(NK_FILE_CHANGE_MOVED, deep, moved_deep) != NULL);
    CHECK(nk_file_watch_add_directory(watch, deep, 0) == NK_OK);

    CHECK(move_path(a, moved_a));
    CHECK(await(NK_FILE_CHANGE_MOVED, moved_a, a) != NULL);
    CHECK(nk_file_watch_add_directory(watch, deep, 0) == NK_ERROR_INVALID_ARGUMENT);
    CHECK(nk_file_watch_destroy(watch) == NK_OK);
    settle(0.05);
    clear_records();
}

/* Reported paths start with the registered spelling, whatever its
 * separators, and a registration matches any spelling of its path. */
static void check_spelling(const char *root) {
    char native[600], upper[600], file[700], expected[700];
    snprintf(native, sizeof(native), "%s", root);
    for (char *character = native; *character; ++character)
        if (*character == '/')
            *character = '\\';
    snprintf(upper, sizeof(upper), "%s/", root);
    for (char *character = upper; *character; ++character)
        if (*character >= 'a' && *character <= 'z')
            *character = (char)(*character - 'a' + 'A');
    nk_file_watch watch = create_watch();
    CHECK(nk_file_watch_add_directory(watch, native, 0) == NK_OK);
    CHECK(nk_file_watch_add_directory(watch, upper, 0) == NK_OK);
    join(file, sizeof(file), root, "spelled.txt");
    snprintf(expected, sizeof(expected), "%s/spelled.txt", native);
    write_file(file, "spelling");
    CHECK(await(NK_FILE_CHANGE_ADDED, expected, NULL) != NULL);
    CHECK(nk_file_watch_destroy(watch) == NK_OK);
    settle(0.05);
    clear_records();
}

/* Re-adding a directory as recursive extends it to its whole subtree. */
static void check_recursive_upgrade(const char *root) {
    char sub[600], before[700], after[700];
    join(sub, sizeof(sub), root, "sub");
    join(before, sizeof(before), root, "sub/before.txt");
    join(after, sizeof(after), root, "sub/after.txt");
    make_directory(sub);
    nk_file_watch watch = create_watch();
    CHECK(nk_file_watch_add_directory(watch, root, 0) == NK_OK);
    write_file(before, "unwatched");
    settle(0.2);
    CHECK(find(NK_FILE_CHANGE_ADDED, before, NULL) == NULL);
    CHECK(nk_file_watch_add_directory(watch, root, 1) == NK_OK);
    write_file(after, "watched");
    CHECK(await(NK_FILE_CHANGE_ADDED, after, NULL) != NULL);
    CHECK(nk_file_watch_destroy(watch) == NK_OK);
    settle(0.05);
    clear_records();
}

/* Re-adding a registered directory is a lookup that must not touch the
 * disk. */
static void check_rearm_cost(const char *root) {
    nk_file_watch watch = create_watch();
    CHECK(nk_file_watch_add_directory(watch, root, 1) == NK_OK);
    const double start = now();
    for (int index = 0; index < 10000; ++index)
        CHECK(nk_file_watch_add_directory(watch, root, 1) == NK_OK);
    const double elapsed = now() - start;
    printf("win_file_watch: 10000 re-adds in %.2f ms\n", elapsed * 1000.0);
    CHECK(elapsed < 0.05);
    CHECK(nk_file_watch_destroy(watch) == NK_OK);
}

static void check_registration_errors(const char *root) {
    char missing[600], file[600];
    join(missing, sizeof(missing), root, "missing");
    join(file, sizeof(file), root, "plain.txt");
    write_file(file, "not a directory");
    nk_file_watch watch = create_watch();
    CHECK(nk_file_watch_add_directory(watch, missing, 0) == NK_ERROR_INVALID_ARGUMENT);
    CHECK(nk_file_watch_add_directory(watch, "relative/path", 0) == NK_ERROR_INVALID_ARGUMENT);
    CHECK(nk_file_watch_add_directory(watch, file, 0) == NK_ERROR_INVALID_ARGUMENT);
    CHECK(nk_file_watch_remove_directory(watch, root) == NK_ERROR_INVALID_ARGUMENT);

    CHECK(nk_file_watch_add_directory(watch, root, 1) == NK_OK);
    CHECK(nk_file_watch_remove_directory(watch, root) == NK_OK);
    char silent[600];
    join(silent, sizeof(silent), root, "after-remove.txt");
    write_file(silent, "unwatched");
    settle(0.2);
    CHECK(find_with_prefix(silent) == NULL);
    CHECK(nk_file_watch_destroy(watch) == NK_OK);
    clear_records();
}

/* A deleted or renamed registration reports its removal, and re-adding it
 * fails so the caller can fall back to polling. That holds for a top with no
 * activity inside it, for a registration below an unregistered directory that
 * moves, and for a registered directory removed below an unregistered
 * parent. */
static void check_lost_directories(const char *root) {
    char top[600], idle[600], idle_moved[600], kept[600], unregistered[600], below[600],
        middle[600], middle_moved[600], nested[600], still[700];
    join(top, sizeof(top), root, "lost-top");
    join(idle, sizeof(idle), root, "idle-top");
    join(idle_moved, sizeof(idle_moved), root, "idle-top-moved");
    join(kept, sizeof(kept), root, "kept");
    join(unregistered, sizeof(unregistered), root, "kept/unregistered");
    join(below, sizeof(below), root, "kept/unregistered/lost-below");
    join(middle, sizeof(middle), root, "kept/middle");
    join(middle_moved, sizeof(middle_moved), root, "kept/middle-moved");
    join(nested, sizeof(nested), root, "kept/middle/nested");
    make_directory(top);
    make_directory(idle);
    make_directory(kept);
    make_directory(unregistered);
    make_directory(below);
    make_directory(middle);
    make_directory(nested);

    nk_file_watch watch = create_watch();
    CHECK(nk_file_watch_add_directory(watch, top, 1) == NK_OK);
    CHECK(nk_file_watch_add_directory(watch, idle, 1) == NK_OK);
    CHECK(nk_file_watch_add_directory(watch, kept, 0) == NK_OK);
    CHECK(nk_file_watch_add_directory(watch, below, 0) == NK_OK);
    CHECK(nk_file_watch_add_directory(watch, nested, 0) == NK_OK);

    remove_tree(top);
    const record *lost = await(NK_FILE_CHANGE_REMOVED, top, NULL);
    CHECK(lost != NULL);
    CHECK(lost->payload.flags & NK_FILE_EVENT_FLAG_RESCAN_REQUIRED);
    CHECK(nk_file_watch_add_directory(watch, top, 1) == NK_ERROR_INVALID_ARGUMENT);

    CHECK(move_path(idle, idle_moved));
    lost = await(NK_FILE_CHANGE_REMOVED, idle, NULL);
    CHECK(lost != NULL);
    CHECK(lost->payload.flags & NK_FILE_EVENT_FLAG_RESCAN_REQUIRED);
    CHECK(nk_file_watch_add_directory(watch, idle, 1) == NK_ERROR_INVALID_ARGUMENT);

    remove_tree(below);
    CHECK(await(NK_FILE_CHANGE_REMOVED, below, NULL) != NULL);
    CHECK(nk_file_watch_add_directory(watch, below, 0) == NK_ERROR_INVALID_ARGUMENT);

    CHECK(move_path(middle, middle_moved));
    CHECK(await(NK_FILE_CHANGE_MOVED, middle_moved, middle) != NULL);
    CHECK(nk_file_watch_add_directory(watch, nested, 0) == NK_ERROR_INVALID_ARGUMENT);

    CHECK(nk_file_watch_add_directory(watch, kept, 0) == NK_OK);
    join(still, sizeof(still), kept, "still-watched.txt");
    write_file(still, "kept");
    CHECK(await(NK_FILE_CHANGE_ADDED, still, NULL) != NULL);
    CHECK(nk_file_watch_destroy(watch) == NK_OK);
    settle(0.05);
    clear_records();
}

/* Every file in a burst is either reported or covered by an overflow. */
static void check_flood(const char *root) {
    enum { k_files = 5000 };
    char directory[600];
    join(directory, sizeof(directory), root, "flood");
    make_directory(directory);
    nk_file_watch watch = create_watch();
    CHECK(nk_file_watch_add_directory(watch, directory, 1) == NK_OK);
    const double start = now();
    char path[700];
    for (int index = 0; index < k_files; ++index) {
        snprintf(path, sizeof(path), "%s/file-%05d.txt", directory, index);
        write_file(path, "x");
        if (index % 256 == 0)
            pump();
    }
    snprintf(path, sizeof(path), "%s/file-%05d.txt", directory, k_files - 1);
    await(NK_FILE_CHANGE_ADDED, path, NULL);
    settle(0.2);
    int missing = 0;
    for (int index = 0; index < k_files; ++index) {
        snprintf(path, sizeof(path), "%s/file-%05d.txt", directory, index);
        if (!find(NK_FILE_CHANGE_ADDED, path, NULL))
            ++missing;
    }
    printf("win_file_watch: %d files in %.0f ms, %zu events, %d unreported, overflow %d\n", k_files,
           (now() - start) * 1000.0, record_count, missing, has_overflow());
    CHECK(missing == 0 || has_overflow());
    CHECK(nk_file_watch_destroy(watch) == NK_OK);
    clear_records();
}

/* Destroying a watch with undelivered changes, and shutting down with a live
 * one, must neither crash nor leak a blocked worker. */
static void check_teardown(const char *root) {
    char path[600];
    nk_file_watch busy = create_watch();
    CHECK(nk_file_watch_add_directory(busy, root, 1) == NK_OK);
    for (int index = 0; index < 64; ++index) {
        snprintf(path, sizeof(path), "%s/teardown-%d.txt", root, index);
        write_file(path, "pending");
    }
    CHECK(nk_file_watch_destroy(busy) == NK_OK);
    nk_file_watch live = create_watch();
    CHECK(nk_file_watch_add_directory(live, root, 1) == NK_OK);
    (void)live;
}

int main(void) {
    QueryPerformanceFrequency(&frequency);
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    CHECK(nk_init(&init) == NK_OK);
    CHECK(nk_get_capabilities() & NK_CAP_FILE_WATCH);

    char base[MAX_PATH], root[600];
    CHECK(GetTempPathA(MAX_PATH, base));
    snprintf(root, sizeof(root), "%snativekit-file-watch-windows-%lu", base, GetCurrentProcessId());
    remove_tree(root);
    for (char *character = root; *character; ++character)
        if (*character == '\\')
            *character = '/';
    make_directory(root);

    const char *steps[] = {"changes", "scoping", "spelling", "upgrade",
                           "rearm",   "errors",  "lost",     "flood"};
    void (*checks[])(const char *) = {check_changes,          check_scoping,
                                      check_spelling,         check_recursive_upgrade,
                                      check_rearm_cost,       check_registration_errors,
                                      check_lost_directories, check_flood};
    for (size_t index = 0; index < sizeof(checks) / sizeof(checks[0]); ++index) {
        char directory[700];
        snprintf(directory, sizeof(directory), "%s/%s", root, steps[index]);
        make_directory(directory);
        checks[index](directory);
    }
    char teardown[700];
    snprintf(teardown, sizeof(teardown), "%s/teardown", root);
    make_directory(teardown);
    check_teardown(teardown);
    nk_shutdown();
    remove_tree(root);
    clear_records();
    free(records);
    printf("win_file_watch: passed\n");
    return 0;
}
