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

/* URI inputs are copied before return. These functions are UI-thread-only. */
NK_API nk_result NK_CALL nk_shell_open_resource(const nk_resource *resource);
NK_API nk_result NK_CALL nk_share(const nk_share_options *options);
NK_API nk_result NK_CALL nk_clipboard_set_resources(const nk_resource *resources,
                                                    uint32_t resource_count);
NK_API nk_result NK_CALL nk_clipboard_read_resources(nk_request_id *out_request);

/* Resource dialogs return NK_EVENT_DIALOG_COMPLETE with nk_resource_list data. */
NK_API nk_result NK_CALL nk_dialog_open_resource(nk_handle parent,
                                                 const nk_file_dialog_options *options,
                                                 nk_request_id *out_request);
NK_API nk_result NK_CALL nk_dialog_save_resource(nk_handle parent,
                                                 const nk_file_dialog_options *options,
                                                 nk_request_id *out_request);
NK_API nk_result NK_CALL nk_dialog_select_resource_directory(
    nk_handle parent, const nk_file_dialog_options *options, nk_request_id *out_request);

/* Returned views remain owned by the event until nk_event_release(). */
NK_API nk_result NK_CALL nk_resource_event_item(const nk_event *event, uint32_t index,
                                                nk_resource_view *out_resource);

#ifdef __cplusplus
}
#endif

#endif
