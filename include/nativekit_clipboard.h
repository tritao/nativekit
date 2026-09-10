#ifndef NATIVEKIT_CLIPBOARD_H
#define NATIVEKIT_CLIPBOARD_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Header at the start of NK_EVENT_CLIPBOARD_FILES_COMPLETE data. */
typedef struct nk_clipboard_files {
    uint32_t path_count;
    uint32_t strings_offset;
} nk_clipboard_files;

/* Header at the start of NK_EVENT_DROP_FILES and NK_EVENT_DROP_TEXT data. */
typedef struct nk_drop_data {
    int32_t x;
    int32_t y;
    uint32_t item_count;
    uint32_t strings_offset;
} nk_drop_data;

/* Copies UTF-8 text into the system clipboard. UI thread only. */
NK_API nk_result NK_CALL nk_clipboard_set_text(const char *text);

/* Copies local paths into the clipboard as a URI list. UI thread only. */
NK_API nk_result NK_CALL nk_clipboard_set_files(const char *const *paths, uint32_t path_count);

/*
 * Starts an asynchronous clipboard read. Completion is delivered through the
 * corresponding NK_EVENT_CLIPBOARD_* event with the returned request ID.
 * Missing clipboard content completes successfully with empty event data.
 */
NK_API nk_result NK_CALL nk_clipboard_read_text(nk_request_id *out_request NK_OUT);
NK_API nk_result NK_CALL nk_clipboard_read_files(nk_request_id *out_request NK_OUT);

/* Enables or disables the text and/or local-file drop formats supported by the backend. */
NK_API nk_result NK_CALL nk_window_set_drop_enabled(nk_handle window, uint32_t enabled);

/* Returned UTF-8 views remain owned by the event until nk_event_release(). */
NK_API nk_result NK_CALL nk_clipboard_event_file(const nk_event *event, uint32_t index,
                                                 const char **out_path, uint32_t *out_length);
NK_API nk_result NK_CALL nk_drop_event_item(const nk_event *event, uint32_t index,
                                            const char **out_item, uint32_t *out_length);

#ifdef __cplusplus
}
#endif

#endif
