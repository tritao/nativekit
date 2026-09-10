#ifndef NATIVEKIT_DIALOG_H
#define NATIVEKIT_DIALOG_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NK_DIALOG_OPEN_FILE = 1,
    NK_DIALOG_SAVE_FILE = 2,
    NK_DIALOG_SELECT_DIRECTORY = 3,
    NK_DIALOG_MESSAGE = 4
};

enum {
    NK_DIALOG_ALLOW_MULTIPLE = 1u << 0,
    NK_DIALOG_CONFIRM_OVERWRITE = 1u << 1,
    NK_DIALOG_SHOW_HIDDEN = 1u << 2
};

enum { NK_MESSAGE_INFO = 0, NK_MESSAGE_WARNING = 1, NK_MESSAGE_ERROR = 2, NK_MESSAGE_QUESTION = 3 };

enum {
    NK_MESSAGE_BUTTON_OK = 1u << 0,
    NK_MESSAGE_BUTTON_CANCEL = 1u << 1,
    NK_MESSAGE_BUTTON_YES = 1u << 2,
    NK_MESSAGE_BUTTON_NO = 1u << 3
};

enum {
    NK_MESSAGE_RESULT_NONE = 0,
    NK_MESSAGE_RESULT_OK = 1,
    NK_MESSAGE_RESULT_CANCEL = 2,
    NK_MESSAGE_RESULT_YES = 3,
    NK_MESSAGE_RESULT_NO = 4
};

typedef struct nk_dialog_filter {
    const char *name;
    /* Semicolon-separated glob patterns, for example "*.png;*.jpg". */
    const char *patterns;
} nk_dialog_filter;

typedef struct nk_file_dialog_options {
    uint32_t struct_size;
    uint32_t flags;
    const char *title;
    const char *initial_path;
    const char *suggested_name;
    const nk_dialog_filter *filters;
    uint32_t filter_count;
    uint32_t reserved;
} nk_file_dialog_options;

typedef struct nk_message_dialog_options {
    uint32_t struct_size;
    uint32_t kind;
    uint32_t buttons;
    uint32_t reserved;
    const char *title;
    const char *message;
} nk_message_dialog_options;

/* Header at the start of NK_EVENT_DIALOG_COMPLETE data for file dialogs. */
typedef struct nk_dialog_paths {
    uint32_t accepted;
    uint32_t path_count;
    uint32_t offsets_offset;
    uint32_t strings_offset;
} nk_dialog_paths;

/* Payload of NK_EVENT_DIALOG_COMPLETE for message dialogs. */
typedef struct nk_dialog_message_result {
    uint32_t button;
} nk_dialog_message_result;

/*
 * Starts a non-blocking native dialog on the UI thread. `parent` may be zero;
 * otherwise it must be a live window. All strings and filters are copied before
 * the function returns. Completion uses NK_EVENT_DIALOG_COMPLETE, with the
 * dialog kind in event.flags and the returned request ID in event.request_id.
 */
NK_API nk_result NK_CALL nk_dialog_open_file(nk_handle parent,
                                             const nk_file_dialog_options *options,
                                             nk_request_id *out_request NK_OUT);
NK_API nk_result NK_CALL nk_dialog_save_file(nk_handle parent,
                                             const nk_file_dialog_options *options,
                                             nk_request_id *out_request NK_OUT);
NK_API nk_result NK_CALL nk_dialog_select_directory(nk_handle parent,
                                                    const nk_file_dialog_options *options,
                                                    nk_request_id *out_request NK_OUT);
NK_API nk_result NK_CALL nk_dialog_message(nk_handle parent,
                                           const nk_message_dialog_options *options,
                                           nk_request_id *out_request NK_OUT);

/* Cancels a pending dialog. Its completion event is still emitted. */
NK_API nk_result NK_CALL nk_dialog_cancel(nk_request_id request);

/*
 * Validates and returns a path from a dialog event. The returned UTF-8 view is
 * owned by the event and remains valid until nk_event_release().
 */
NK_API nk_result NK_CALL nk_dialog_event_path(const nk_event *event, uint32_t index,
                                              const char **out_path, uint32_t *out_length);

#ifdef __cplusplus
}
#endif

#endif
