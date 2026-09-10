#ifndef NATIVEKIT_RESOURCE_H
#define NATIVEKIT_RESOURCE_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit_dialog.h"

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Resource flags and input types                                            */
/* ------------------------------------------------------------------------- */

/** Access and persistence flags attached to a URI resource. */
typedef uint32_t nk_resource_flags;
enum {
    /** The resource may be opened for reading. */
    NK_RESOURCE_READABLE = 1u << 0,
    /** The resource may be opened for writing. */
    NK_RESOURCE_WRITABLE = 1u << 1,
    /** The access grant is retained beyond the originating operation. */
    NK_RESOURCE_PERSISTED = 1u << 2
};

/** Access and creation flags accepted by nk_resource_open(). */
typedef uint32_t nk_resource_open_flags;
enum {
    /** Open the stream for reading. */
    NK_RESOURCE_OPEN_READ = 1u << 0,
    /** Open the stream for writing. */
    NK_RESOURCE_OPEN_WRITE = 1u << 1,
    /** Create the resource when it does not already exist. */
    NK_RESOURCE_OPEN_CREATE = 1u << 2,
    /** Truncate an existing resource before writing. */
    NK_RESOURCE_OPEN_TRUNCATE = 1u << 3
};

/** Capabilities reported for an opened resource stream. */
typedef uint32_t nk_resource_stream_flags;
enum {
    /** The stream accepts reads. */
    NK_RESOURCE_STREAM_READABLE = 1u << 0,
    /** The stream accepts writes. */
    NK_RESOURCE_STREAM_WRITABLE = 1u << 1,
    /** The stream supports position changes. */
    NK_RESOURCE_STREAM_SEEKABLE = 1u << 2,
    /** The stream size field contains a known byte count. */
    NK_RESOURCE_STREAM_SIZE_KNOWN = 1u << 3
};

/** Reference point used by nk_resource_seek(). */
typedef uint32_t nk_seek_origin;

enum {
    /** Offset from the beginning of the stream. */
    NK_SEEK_START = 0,
    /** Offset from the current stream position. */
    NK_SEEK_CURRENT = 1,
    /** Offset from the end of the stream. */
    NK_SEEK_END = 2
};

/** URI identity and metadata for a resource. */
typedef struct nk_resource {
    /** Set to sizeof(nk_resource) before passing the structure. */
    uint32_t struct_size;
    /** Access and persistence flags known by the producer. */
    nk_resource_flags flags;
    /** Required absolute UTF-8 URI; this is not necessarily a filesystem path. */
    const char *uri NK_UTF8;
    /** Optional UTF-8 MIME type. */
    const char *mime_type NK_NULLABLE_UTF8;
    /** Optional UTF-8 name suitable for display. */
    const char *display_name NK_NULLABLE_UTF8;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for future resource metadata; set all elements to zero. */
    uint64_t reserved2[2];
} nk_resource;

/** Text and URI resources supplied to the platform share UI. */
typedef struct nk_share_options {
    /** Set to sizeof(nk_share_options) before sharing. */
    uint32_t struct_size;
    /** Reserved; set to zero. */
    uint32_t flags;
    /** Optional title shown by the share UI. */
    const char *title NK_NULLABLE_UTF8;
    /** Optional UTF-8 text to share. */
    const char *text NK_NULLABLE_UTF8;
    /** Caller-owned resources read during the call and copied by NativeKit. */
    const nk_resource *resources NK_BORROWED_ARRAY(resource_count);
    /** Number of entries in `resources`. */
    uint32_t resource_count;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for future share options; set all elements to zero. */
    uint64_t reserved2[2];
} nk_share_options;

/* ------------------------------------------------------------------------- */
/* Resource event data                                                       */
/* ------------------------------------------------------------------------- */

/** Header and item table stored in resource-bearing event data. */
typedef struct nk_resource_list {
    /** 1 when the user accepted the operation, and 0 when it was cancelled. */
    nk_bool accepted;
    /** Number of resource items in the following item table. */
    uint32_t item_count;
    /** Byte offset from the payload start to the nk_resource_item table. */
    uint32_t items_offset;
    /** Byte offset from the payload start to packed NUL-terminated strings. */
    uint32_t strings_offset;
} nk_resource_list;

/** One resource entry in a packed resource-bearing event payload. */
typedef struct nk_resource_item {
    /** Access and persistence flags for this resource. */
    nk_resource_flags flags;
    /** Byte offset to the required URI string, relative to the payload start. */
    uint32_t uri_offset;
    /** Byte offset to the optional MIME string, or zero when absent. */
    uint32_t mime_type_offset;
    /** Byte offset to the optional display-name string, or zero when absent. */
    uint32_t display_name_offset;
} nk_resource_item;

/** Borrowed resource metadata decoded from an event payload. */
typedef struct nk_resource_view {
    /** Set to sizeof(nk_resource_view) before decoding an item. */
    uint32_t struct_size;
    /** Access and persistence flags for this resource. */
    nk_resource_flags flags;
    /** Borrowed UTF-8 URI; valid until nk_event_release(). */
    const char *uri;
    /** URI length in bytes, excluding its trailing NUL. */
    uint32_t uri_length;
    /** Borrowed optional MIME string, or NULL when absent. */
    const char *mime_type;
    /** MIME length in bytes, excluding its trailing NUL. */
    uint32_t mime_type_length;
    /** Borrowed optional display name, or NULL when absent. */
    const char *display_name;
    /** Display-name length in bytes, excluding its trailing NUL. */
    uint32_t display_name_length;
    /** Reserved for future decoded metadata; set all elements to zero. */
    uint64_t reserved[2];
} nk_resource_view;

/** Capabilities and size information for an opened resource stream. */
typedef struct nk_resource_stream_info {
    /** Set to sizeof(nk_resource_stream_info) before querying. */
    uint32_t struct_size;
    /** Bitwise OR of NK_RESOURCE_STREAM_* capabilities. */
    nk_resource_stream_flags flags;
    /** Size in bytes, or UINT64_MAX when the provider cannot report it. */
    uint64_t size;
    /** Reserved for future stream information; set all elements to zero. */
    uint64_t reserved[2];
} nk_resource_stream_info;

/** Header at the start of NK_EVENT_SHARE_RECEIVED data. */
typedef struct nk_received_share {
    /** Byte offset to the embedded nk_resource_list. */
    uint32_t resources_offset;
    /** Byte offset to optional shared UTF-8 text, or zero when absent. */
    uint32_t text_offset;
    /** Byte offset to optional UTF-8 subject text, or zero when absent. */
    uint32_t subject_offset;
    /** Reserved; set to zero. */
    uint32_t reserved;
} nk_received_share;

/** Header at the start of NK_EVENT_RESOURCE_DROP data. Coordinates are logical pixels. */
typedef struct nk_resource_drop {
    /** Byte offset to the embedded nk_resource_list. */
    uint32_t resources_offset;
    /** Byte offset to optional dropped UTF-8 text, or zero when absent. */
    uint32_t text_offset;
    /** Drop x coordinate in the host's logical pixels. */
    float x;
    /** Drop y coordinate in the host's logical pixels. */
    float y;
    /** Reserved for future drop information; set all elements to zero. */
    uint64_t reserved[2];
} nk_resource_drop;

/* ------------------------------------------------------------------------- */
/* Resource sharing and dialog APIs                                          */
/* ------------------------------------------------------------------------- */

/** URI inputs are copied before return. These functions are UI-thread-only. */
NK_API nk_result NK_CALL nk_shell_open_resource(const nk_resource *resource);
/** Launches the platform share UI with optional text and URI resources. */
NK_API nk_result NK_CALL nk_share(const nk_share_options *options);
/** Copies URI resources into the system clipboard. */
NK_API nk_result NK_CALL nk_clipboard_set_resources(const nk_resource *resources NK_IN_ARRAY(resource_count),
                                                    uint32_t resource_count);
/** Starts an asynchronous read of URI resources from the system clipboard. */
NK_API nk_result NK_CALL nk_clipboard_read_resources(nk_request_id *out_request NK_OUT);

/** Resource dialogs return NK_EVENT_DIALOG_RESOURCES_COMPLETE with nk_resource_list data. */
NK_API nk_result NK_CALL nk_dialog_open_resource(nk_handle parent,
                                                 const nk_file_dialog_options *options,
                                                 nk_request_id *out_request NK_OUT);
/** Starts an asynchronous native URI-resource save dialog. */
NK_API nk_result NK_CALL nk_dialog_save_resource(nk_handle parent,
                                                 const nk_file_dialog_options *options,
                                                 nk_request_id *out_request NK_OUT);
/** Starts an asynchronous native URI-resource directory dialog. */
NK_API nk_result NK_CALL nk_dialog_select_resource_directory(
    nk_handle parent, const nk_file_dialog_options *options, nk_request_id *out_request NK_OUT);

/* ------------------------------------------------------------------------- */
/* Resource event helpers                                                    */
/* ------------------------------------------------------------------------- */

/** Returned views remain owned by the event until nk_event_release(). */
NK_API nk_result NK_CALL nk_resource_event_item(const nk_event *event, uint32_t index,
                                                nk_resource_view *out_resource);
/** Returns optional UTF-8 text from a received-share event. */
NK_API nk_result NK_CALL nk_share_event_text(const nk_event *event, const char **out_text,
                                             uint32_t *out_length);
/** Returns optional UTF-8 subject text from a received-share event. */
NK_API nk_result NK_CALL nk_share_event_subject(const nk_event *event, const char **out_subject,
                                                uint32_t *out_length);
/** Returns optional UTF-8 text from a resource-drop event. */
NK_API nk_result NK_CALL nk_resource_drop_event_text(const nk_event *event, const char **out_text,
                                                     uint32_t *out_length);

/* ------------------------------------------------------------------------- */
/* Persisted resource access                                                 */
/* ------------------------------------------------------------------------- */

/**
 * Controls durable URI access on platforms that support it. access_flags is a
 * combination of NK_RESOURCE_READABLE and NK_RESOURCE_WRITABLE; zero releases
 * all persisted access. out_flags receives the actual resource flags retained.
 * These functions are UI-thread-only.
 */
NK_API nk_result NK_CALL nk_resource_set_persisted_access(const nk_resource *resource,
                                                          nk_resource_flags access_flags,
                                                          nk_resource_flags *out_flags NK_OUT);
/** Queries the readable/writable access retained for a URI resource. */
NK_API nk_result NK_CALL nk_resource_get_persisted_access(const nk_resource *resource,
                                                          nk_resource_flags *out_flags NK_OUT);

/* ------------------------------------------------------------------------- */
/* Resource stream APIs                                                      */
/* ------------------------------------------------------------------------- */

/**
 * Opens a URI-backed stream on the UI thread. Stream operations copy bytes
 * synchronously and may be called from worker threads. A successful read may
 * return fewer bytes than requested; zero bytes means end of stream.
 */
NK_API nk_result NK_CALL nk_resource_open(const nk_resource *resource,
                                          nk_resource_open_flags flags,
                                          nk_handle *out_stream NK_OUT);
/** Returns capabilities and size information for a resource stream. */
NK_API nk_result NK_CALL nk_resource_stream_info_get(nk_handle stream,
                                                     nk_resource_stream_info *out_info);
/** Reads up to `size` bytes; a successful read may return fewer bytes. */
NK_API nk_result NK_CALL nk_resource_read(nk_handle stream, void *buffer, uint64_t size,
                                          uint64_t *out_read);
/** Writes up to `size` bytes; a successful write may transfer fewer bytes. */
NK_API nk_result NK_CALL nk_resource_write(nk_handle stream, const void *buffer, uint64_t size,
                                           uint64_t *out_written);
/** Moves the stream position and returns the resulting absolute byte offset. */
NK_API nk_result NK_CALL nk_resource_seek(nk_handle stream, int64_t offset,
                                          nk_seek_origin origin, uint64_t *out_position);
/** Closes a resource stream and invalidates its handle. */
NK_API nk_result NK_CALL nk_resource_close(nk_handle stream);

#ifdef __cplusplus
}
#endif

#endif
