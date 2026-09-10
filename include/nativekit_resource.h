#ifndef NATIVEKIT_RESOURCE_H
#define NATIVEKIT_RESOURCE_H

#include "nativekit_dialog.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NK_RESOURCE_READABLE = 1u << 0,
    NK_RESOURCE_WRITABLE = 1u << 1,
    NK_RESOURCE_PERSISTED = 1u << 2
};

enum {
    NK_RESOURCE_OPEN_READ = 1u << 0,
    NK_RESOURCE_OPEN_WRITE = 1u << 1,
    NK_RESOURCE_OPEN_CREATE = 1u << 2,
    NK_RESOURCE_OPEN_TRUNCATE = 1u << 3
};

enum {
    NK_RESOURCE_STREAM_READABLE = 1u << 0,
    NK_RESOURCE_STREAM_WRITABLE = 1u << 1,
    NK_RESOURCE_STREAM_SEEKABLE = 1u << 2,
    NK_RESOURCE_STREAM_SIZE_KNOWN = 1u << 3
};

typedef uint32_t nk_seek_origin;

enum { NK_SEEK_START = 0, NK_SEEK_CURRENT = 1, NK_SEEK_END = 2 };

typedef struct nk_resource {
    uint32_t struct_size;
    uint32_t flags;
    const char *uri;
    const char *mime_type;
    const char *display_name;
    uint32_t reserved;
    uint64_t reserved2[2];
} nk_resource;

typedef struct nk_share_options {
    uint32_t struct_size;
    uint32_t flags;
    const char *title;
    const char *text;
    const nk_resource *resources;
    uint32_t resource_count;
    uint32_t reserved;
    uint64_t reserved2[2];
} nk_share_options;

/* Header and item table stored in resource-bearing event data. */
typedef struct nk_resource_list {
    uint32_t accepted;
    uint32_t item_count;
    uint32_t items_offset;
    uint32_t strings_offset;
} nk_resource_list;

typedef struct nk_resource_item {
    uint32_t flags;
    uint32_t uri_offset;
    uint32_t mime_type_offset;
    uint32_t display_name_offset;
} nk_resource_item;

typedef struct nk_resource_view {
    uint32_t struct_size;
    uint32_t flags;
    const char *uri;
    uint32_t uri_length;
    const char *mime_type;
    uint32_t mime_type_length;
    const char *display_name;
    uint32_t display_name_length;
    uint64_t reserved[2];
} nk_resource_view;

typedef struct nk_resource_stream_info {
    uint32_t struct_size;
    uint32_t flags;
    uint64_t size;
    uint64_t reserved[2];
} nk_resource_stream_info;

/* Header at the start of NK_EVENT_SHARE_RECEIVED data. */
typedef struct nk_received_share {
    uint32_t resources_offset;
    uint32_t text_offset;
    uint32_t subject_offset;
    uint32_t reserved;
} nk_received_share;

/* Header at the start of NK_EVENT_RESOURCE_DROP data. Coordinates are logical pixels. */
typedef struct nk_resource_drop {
    uint32_t resources_offset;
    uint32_t text_offset;
    float x;
    float y;
    uint64_t reserved[2];
} nk_resource_drop;

/* URI inputs are copied before return. These functions are UI-thread-only. */
NK_API nk_result NK_CALL nk_shell_open_resource(const nk_resource *resource);
NK_API nk_result NK_CALL nk_share(const nk_share_options *options);
NK_API nk_result NK_CALL nk_clipboard_set_resources(const nk_resource *resources,
                                                    uint32_t resource_count);
NK_API nk_result NK_CALL nk_clipboard_read_resources(nk_request_id *out_request NK_OUT);

/* Resource dialogs return NK_EVENT_DIALOG_RESOURCES_COMPLETE with nk_resource_list data. */
NK_API nk_result NK_CALL nk_dialog_open_resource(nk_handle parent,
                                                 const nk_file_dialog_options *options,
                                                 nk_request_id *out_request NK_OUT);
NK_API nk_result NK_CALL nk_dialog_save_resource(nk_handle parent,
                                                 const nk_file_dialog_options *options,
                                                 nk_request_id *out_request NK_OUT);
NK_API nk_result NK_CALL nk_dialog_select_resource_directory(
    nk_handle parent, const nk_file_dialog_options *options, nk_request_id *out_request NK_OUT);

/* Returned views remain owned by the event until nk_event_release(). */
NK_API nk_result NK_CALL nk_resource_event_item(const nk_event *event, uint32_t index,
                                                nk_resource_view *out_resource);
NK_API nk_result NK_CALL nk_share_event_text(const nk_event *event, const char **out_text,
                                             uint32_t *out_length);
NK_API nk_result NK_CALL nk_share_event_subject(const nk_event *event, const char **out_subject,
                                                uint32_t *out_length);
NK_API nk_result NK_CALL nk_resource_drop_event_text(const nk_event *event, const char **out_text,
                                                     uint32_t *out_length);

/*
 * Controls durable URI access on platforms that support it. access_flags is a
 * combination of NK_RESOURCE_READABLE and NK_RESOURCE_WRITABLE; zero releases
 * all persisted access. out_flags receives the actual resource flags retained.
 * These functions are UI-thread-only.
 */
NK_API nk_result NK_CALL nk_resource_set_persisted_access(const nk_resource *resource,
                                                          uint32_t access_flags,
                                                          uint32_t *out_flags);
NK_API nk_result NK_CALL nk_resource_get_persisted_access(const nk_resource *resource,
                                                          uint32_t *out_flags);

/*
 * Opens a URI-backed stream on the UI thread. Stream operations copy bytes
 * synchronously and may be called from worker threads. A successful read may
 * return fewer bytes than requested; zero bytes means end of stream.
 */
NK_API nk_result NK_CALL nk_resource_open(const nk_resource *resource, uint32_t flags,
                                          nk_handle *out_stream);
NK_API nk_result NK_CALL nk_resource_stream_info_get(nk_handle stream,
                                                     nk_resource_stream_info *out_info);
NK_API nk_result NK_CALL nk_resource_read(nk_handle stream, void *buffer, uint64_t size,
                                          uint64_t *out_read);
NK_API nk_result NK_CALL nk_resource_write(nk_handle stream, const void *buffer, uint64_t size,
                                           uint64_t *out_written);
NK_API nk_result NK_CALL nk_resource_seek(nk_handle stream, int64_t offset,
                                          nk_seek_origin origin, uint64_t *out_position);
NK_API nk_result NK_CALL nk_resource_close(nk_handle stream);

#ifdef __cplusplus
}
#endif

#endif
