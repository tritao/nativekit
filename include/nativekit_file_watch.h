#ifndef NATIVEKIT_FILE_WATCH_H
#define NATIVEKIT_FILE_WATCH_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Change represented by an NK_EVENT_FILE_CHANGED payload. */
typedef uint32_t nk_file_change_type;
enum NK_ENUM(nk_file_change_type) {
    /** A file or directory was created. */
    NK_FILE_CHANGE_ADDED = 1,
    /** A file or directory was removed. */
    NK_FILE_CHANGE_REMOVED = 2,
    /** A file or directory's contents or metadata changed. */
    NK_FILE_CHANGE_MODIFIED = 3,
    /** A rename pair was reconstructed. */
    NK_FILE_CHANGE_MOVED = 4
};

/** Kind of filesystem item represented by a file event. */
typedef uint32_t nk_file_item_kind;
enum NK_ENUM(nk_file_item_kind) {
    /** The item is a regular file. */
    NK_FILE_ITEM_FILE = 1,
    /** The item is a directory. */
    NK_FILE_ITEM_DIRECTORY = 2,
    /** The backend could not classify the item. */
    NK_FILE_ITEM_UNKNOWN = 3
};

/** Flags carried by nk_file_changed_event. */
typedef uint32_t nk_file_event_flags;
enum NK_FLAGS(nk_file_event_flags) {
    /** The backend could not identify the complete item transition. */
    NK_FILE_EVENT_FLAG_RESCAN_REQUIRED = 1u << 0,
    /** The event was produced after a native queue overflow. */
    NK_FILE_EVENT_FLAG_OVERFLOW = 1u << 1
};

/* Short aliases retained for callers that use event-oriented naming. */
#define NK_FILE_EVENT_RESCAN_REQUIRED NK_FILE_EVENT_FLAG_RESCAN_REQUIRED
#define NK_FILE_EVENT_OVERFLOW NK_FILE_EVENT_FLAG_OVERFLOW

/** Options for creating a file watcher. */
typedef struct nk_file_watch_options {
    /** Set to sizeof(nk_file_watch_options) before calling create. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Reserved for compatible options; initialize to zero. */
    uint32_t flags;
    /** Reserved for compatible extensions; initialize to zero. */
    uint64_t reserved[2];
} nk_file_watch_options;

/**
 * Payload stored in NK_EVENT_FILE_CHANGED and NK_EVENT_FILE_WATCH_OVERFLOW.
 * `path_offset` and `old_path_offset` are byte offsets from this header to
 * NUL-terminated UTF-8 strings. An absent old path has offset and length zero.
 */
typedef struct nk_file_changed_event {
    nk_file_change_type change_type;
    nk_file_item_kind item_kind;
    nk_file_event_flags flags;
    uint32_t path_offset;
    uint32_t path_length;
    uint32_t old_path_offset;
    uint32_t old_path_length;
} nk_file_changed_event;

/** Creates a watcher. The returned handle is generation checked. */
NK_API nk_result NK_CALL nk_file_watch_create(const nk_file_watch_options *options,
                                              nk_file_watch *out_watch NK_OUT NK_OWNED);

/** Adds an absolute UTF-8 native filesystem directory to a watcher. */
NK_API nk_result NK_CALL nk_file_watch_add_directory(nk_file_watch watch, const char *path NK_UTF8,
                                                     nk_bool recursive);

/** Removes a previously added absolute UTF-8 native filesystem directory. */
NK_API nk_result NK_CALL nk_file_watch_remove_directory(nk_file_watch watch,
                                                        const char *path NK_UTF8);

/** Stops the backend and destroys a watcher. */
NK_API nk_result NK_CALL nk_file_watch_destroy(nk_file_watch watch);

#ifdef __cplusplus
}
#endif

#endif
