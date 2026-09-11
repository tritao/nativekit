#ifndef NATIVEKIT_DIALOG_H
#define NATIVEKIT_DIALOG_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit.h"

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Dialog and message constants                                              */
/* ------------------------------------------------------------------------- */

/** Operation encoded in a dialog completion event's flags. */
typedef uint32_t nk_dialog_operation;
enum NK_ENUM(nk_dialog_operation) {
    /** Select one or more local files. */
    NK_DIALOG_OPEN_FILE = 1,
    /** Select a destination for a local file. */
    NK_DIALOG_SAVE_FILE = 2,
    /** Select a local directory. */
    NK_DIALOG_SELECT_DIRECTORY = 3,
    /** Show a native message and collect the selected button. */
    NK_DIALOG_MESSAGE = 4,
    /** Select one or more URI resources. */
    NK_DIALOG_OPEN_RESOURCE = 5,
    /** Select a destination URI resource. */
    NK_DIALOG_SAVE_RESOURCE = 6,
    /** Select a URI resource directory. */
    NK_DIALOG_SELECT_RESOURCE_DIRECTORY = 7
};

/** Optional behavior flags for file and resource dialogs. */
typedef uint32_t nk_dialog_flags;
enum {
    /** Allow selecting more than one item in an open dialog. */
    NK_DIALOG_ALLOW_MULTIPLE = 1u << 0,
    /** Ask for confirmation before replacing an existing destination. */
    NK_DIALOG_CONFIRM_OVERWRITE = 1u << 1,
    /** Show hidden files and directories where supported. */
    NK_DIALOG_SHOW_HIDDEN = 1u << 2
};

/** Visual and semantic kind of a message dialog. */
typedef uint32_t nk_message_kind;
enum NK_ENUM(nk_message_kind) {
    /** Informational message. */
    NK_MESSAGE_INFO = 0,
    /** Warning message. */
    NK_MESSAGE_WARNING = 1,
    /** Error message. */
    NK_MESSAGE_ERROR = 2,
    /** Question that expects a choice. */
    NK_MESSAGE_QUESTION = 3
};

/** Button flags accepted by a message dialog. */
typedef uint32_t nk_message_buttons;
enum {
    /** Show an OK button. */
    NK_MESSAGE_BUTTON_OK = 1u << 0,
    /** Show a Cancel button. */
    NK_MESSAGE_BUTTON_CANCEL = 1u << 1,
    /** Show a Yes button. */
    NK_MESSAGE_BUTTON_YES = 1u << 2,
    /** Show a No button. */
    NK_MESSAGE_BUTTON_NO = 1u << 3
};

/** Button value returned in a message-dialog completion payload. */
typedef uint32_t nk_message_result;
enum NK_ENUM(nk_message_result) {
    /** No button was selected, normally because the dialog was cancelled. */
    NK_MESSAGE_RESULT_NONE = 0,
    /** The OK button was selected. */
    NK_MESSAGE_RESULT_OK = 1,
    /** The Cancel button was selected. */
    NK_MESSAGE_RESULT_CANCEL = 2,
    /** The Yes button was selected. */
    NK_MESSAGE_RESULT_YES = 3,
    /** The No button was selected. */
    NK_MESSAGE_RESULT_NO = 4
};

/* ------------------------------------------------------------------------- */
/* Dialog options and event data                                             */
/* ------------------------------------------------------------------------- */

/** One user-visible name and glob pattern set for a file dialog. */
typedef struct nk_dialog_filter {
    /** Optional label shown for the filter. */
    const char *name NK_NULLABLE_UTF8;
    /** Semicolon-separated UTF-8 glob patterns, for example "*.png;*.jpg". */
    const char *patterns NK_UTF8;
} nk_dialog_filter;

/** Options shared by local-file and URI-resource dialogs. */
typedef struct nk_file_dialog_options {
    /** Set to sizeof(nk_file_dialog_options) before starting the dialog. */
    uint32_t struct_size;
    /** Bitwise OR of NK_DIALOG_* flags. */
    nk_dialog_flags flags;
    /** Optional dialog title. */
    const char *title NK_NULLABLE_UTF8;
    /** Optional initial local path or platform resource location. */
    const char *initial_path NK_NULLABLE_UTF8;
    /** Optional suggested filename for save operations. */
    const char *suggested_name NK_NULLABLE_UTF8;
    /** Caller-owned filters read during the call and copied by NativeKit. */
    const nk_dialog_filter *filters NK_BORROWED_ARRAY(filter_count);
    /** Number of entries in `filters`. */
    uint32_t filter_count;
    /** Reserved; set to zero. */
    uint32_t reserved;
} nk_file_dialog_options;

/** Options for a native message dialog. */
typedef struct nk_message_dialog_options {
    /** Set to sizeof(nk_message_dialog_options) before starting the dialog. */
    uint32_t struct_size;
    /** Message presentation kind. */
    nk_message_kind kind;
    /** Bitwise OR of NK_MESSAGE_BUTTON_* values; zero defaults to OK. */
    nk_message_buttons buttons;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Optional dialog title. */
    const char *title NK_NULLABLE_UTF8;
    /** Required UTF-8 message text. */
    const char *message NK_UTF8;
} nk_message_dialog_options;

/** Header at the start of NK_EVENT_DIALOG_PATHS_COMPLETE data. */
typedef struct nk_dialog_paths {
    /** 1 when the user accepted the dialog, and 0 when it was cancelled. */
    nk_bool accepted;
    /** Number of returned paths. */
    uint32_t path_count;
    /** Byte offset from the payload start to the uint32_t path-offset table. */
    uint32_t offsets_offset;
    /** Byte offset from the payload start to the packed NUL-terminated paths. */
    uint32_t strings_offset;
} nk_dialog_paths;

/** Payload of NK_EVENT_DIALOG_MESSAGE_COMPLETE. */
typedef struct nk_dialog_message_result {
    /** Button selected by the user, or NK_MESSAGE_RESULT_NONE on cancellation. */
    nk_message_result button;
} nk_dialog_message_result;

/* ------------------------------------------------------------------------- */
/* Dialog operations                                                         */
/* ------------------------------------------------------------------------- */

/**
 * Starts a non-blocking native dialog on the UI thread. `parent` may be zero;
 * otherwise it must be a live window. All strings and filters are copied before
 * the function returns. Completion uses NK_EVENT_DIALOG_PATHS_COMPLETE or
 * NK_EVENT_DIALOG_MESSAGE_COMPLETE, with the operation in event.flags and the
 * returned request ID in event.request_id.
 */
NK_API nk_result NK_CALL nk_dialog_open_file(nk_handle parent,
                                             const nk_file_dialog_options *options,
                                             nk_request_id *out_request NK_OUT);
/** Starts an asynchronous native save-file dialog. */
NK_API nk_result NK_CALL nk_dialog_save_file(nk_handle parent,
                                             const nk_file_dialog_options *options,
                                             nk_request_id *out_request NK_OUT);
/** Starts an asynchronous native directory-selection dialog. */
NK_API nk_result NK_CALL nk_dialog_select_directory(nk_handle parent,
                                                    const nk_file_dialog_options *options,
                                                    nk_request_id *out_request NK_OUT);
/** Starts an asynchronous native message dialog. */
NK_API nk_result NK_CALL nk_dialog_message(nk_handle parent,
                                           const nk_message_dialog_options *options,
                                           nk_request_id *out_request NK_OUT);

/** Cancels a pending dialog. Its completion event is still emitted. */
NK_API nk_result NK_CALL nk_dialog_cancel(nk_request_id request);

/* ------------------------------------------------------------------------- */
/* Dialog event helpers                                                      */
/* ------------------------------------------------------------------------- */

/**
 * Validates and returns a path from a dialog event. The returned UTF-8 view is
 * owned by the event and remains valid until nk_event_release().
 */
NK_API nk_result NK_CALL nk_dialog_event_path(const nk_event *event, uint32_t index,
                                              const char **out_path, uint32_t *out_length);

#ifdef __cplusplus
}
#endif

#endif
