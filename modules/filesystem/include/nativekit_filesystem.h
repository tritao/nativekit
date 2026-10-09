#ifndef NATIVEKIT_FILESYSTEM_H
#define NATIVEKIT_FILESYSTEM_H

#include <stdint.h>
#include "nativekit.h"

#if defined(_WIN32)
#if defined(NKFS_STATIC)
#define NKFS_API
#elif defined(NKFS_BUILDING_LIBRARY)
#define NKFS_API __declspec(dllexport)
#else
#define NKFS_API __declspec(dllimport)
#endif
#else
#define NKFS_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque, generation-checked root or directory cursor handle. */
typedef uint32_t nk_filesystem_handle;

enum {
    NK_FILESYSTEM_MAX_PATH_BYTES = 32768,
    NK_FILESYSTEM_MAX_NAME_BYTES = 4096,
    NK_FILESYSTEM_MAX_READ_BYTES = 262144
};

typedef enum nk_filesystem_entry_kind {
    NK_FILESYSTEM_ENTRY_UNKNOWN = 0,
    NK_FILESYSTEM_ENTRY_REGULAR = 1,
    NK_FILESYSTEM_ENTRY_DIRECTORY = 2,
    NK_FILESYSTEM_ENTRY_SYMLINK = 3,
    NK_FILESYSTEM_ENTRY_OTHER = 4
} nk_filesystem_entry_kind;

/** Metadata returned by stat and directory iteration. Sizes and timestamps are byte/ns values. */
typedef struct nk_filesystem_entry {
    uint32_t struct_size NK_STRUCT_SIZE;
    nk_filesystem_entry_kind kind;
    uint64_t size;
    int64_t modified_unix_ns;
    uint64_t file_id_high;
    uint64_t file_id_low;
    int64_t changed_unix_ns;
    /** Non-zero means the native filename was not valid UTF-8 and cannot be addressed by this API. */
    uint32_t name_unsupported;
    uint32_t reserved;
} nk_filesystem_entry;

/**
 * Open an authorized directory root. The path is selected by the local owner and
 * is resolved once; the returned handle pins that directory identity.
 */
NKFS_API nk_result NK_CALL nk_filesystem_root_open(const char *absolute_path NK_UTF8,
                                                   nk_filesystem_handle *out_root NK_OUT);
/** Closing a root also invalidates cursors opened from it. */
NKFS_API nk_result NK_CALL nk_filesystem_root_close(nk_filesystem_handle root);

/**
 * Stat a slash-separated path relative to root. Empty path addresses the root.
 * Input paths must be canonical relative paths; the final symlink is reported
 * as a symlink entry, while intermediate links are followed only when contained
 * by root. The output struct_size must be set to sizeof(nk_filesystem_entry).
 */
NKFS_API nk_result NK_CALL nk_filesystem_stat(nk_filesystem_handle root,
                                              const char *relative_path NK_UTF8,
                                              nk_filesystem_entry *out_entry NK_OUT);

/** Open a directory relative to root. A final symlink is followed only in-root. */
NKFS_API nk_result NK_CALL nk_filesystem_directory_open(nk_filesystem_handle root,
                                                        const char *relative_path NK_UTF8,
                                                        nk_filesystem_handle *out_cursor NK_OUT);
/**
 * Read one entry. `inout_name_size` is capacity on input and required UTF-8
 * byte count (excluding NUL) on output. A short buffer does not consume the
 * entry. At end, `out_end` is set and no entry is written. Invalid native UTF-8
 * names are returned as explicit unsupported-name entries with an empty name.
 */
NKFS_API nk_result NK_CALL nk_filesystem_directory_next(nk_filesystem_handle cursor,
                                                        nk_filesystem_entry *out_entry NK_OUT,
                                                        char *name NK_OUT_BUFFER(inout_name_size),
                                                        uint32_t *inout_name_size NK_INOUT,
                                                        nk_bool *out_end NK_OUT);
NKFS_API nk_result NK_CALL nk_filesystem_directory_close(nk_filesystem_handle cursor);

/** Open an in-root regular file for bounded positional reads. Final symlinks are followed only in-root. */
NKFS_API nk_result NK_CALL nk_filesystem_file_open(nk_filesystem_handle root,
                                                   const char *relative_path NK_UTF8,
                                                   nk_filesystem_handle *out_file NK_OUT);
/** Read the opened file's current identity and metadata. The handle pins its original file identity. */
NKFS_API nk_result NK_CALL nk_filesystem_file_info(nk_filesystem_handle file,
                                                   nk_filesystem_entry *out_entry NK_OUT);
/**
 * Read at most `requested_bytes` from an absolute file offset. A null buffer
 * with zero capacity queries the required size; a short buffer is not written.
 * Requests are capped at NK_FILESYSTEM_MAX_READ_BYTES. EOF returns zero bytes.
 */
NKFS_API nk_result NK_CALL nk_filesystem_file_read(nk_filesystem_handle file,
                                                   uint64_t offset,
                                                   uint32_t requested_bytes,
                                                   uint8_t *out_bytes NK_OUT_BUFFER(inout_bytes),
                                                   uint32_t *inout_bytes NK_INOUT);
NKFS_API nk_result NK_CALL nk_filesystem_file_close(nk_filesystem_handle file);

/** Thread-local diagnostic for the most recent filesystem call. */
NKFS_API const char *NK_CALL nk_filesystem_last_error(void) NK_RETURNS_BORROWED_UTF8;

#ifdef __cplusplus
}
#endif

#endif
