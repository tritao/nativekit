#ifndef NATIVEKIT_NOTIFICATION_H
#define NATIVEKIT_NOTIFICATION_H

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
/* Notification flags and options                                            */
/* ------------------------------------------------------------------------- */

enum {
    /** Do not show notification sound or vibration when supported by the backend. */
    NK_NOTIFICATION_SILENT = 1u << 0
};

/** Options for an asynchronous desktop notification request. */
typedef struct nk_notification_options {
    /** Set to sizeof(nk_notification_options) before passing the structure. */
    uint32_t struct_size;
    /** Bitwise OR of NK_NOTIFICATION_* flags. */
    uint32_t flags;
    /** Required UTF-8 notification title. */
    const char *title NK_UTF8;
    /** Optional UTF-8 notification body. */
    const char *body NK_NULLABLE_UTF8;
    /** Optional platform-resolved icon name or absolute file path. */
    const char *icon NK_NULLABLE_UTF8;
    /** Requested display duration in milliseconds; zero uses the platform default. */
    uint32_t timeout_ms;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for future notification options; set all elements to zero. */
    uint64_t reserved2[2];
} nk_notification_options;

/* ------------------------------------------------------------------------- */
/* Notification operations                                                   */
/* ------------------------------------------------------------------------- */

/*
 * Requests a desktop notification. Strings are copied before return. Success
 * means the request was accepted for asynchronous processing; delivery or
 * failure is reported with the same request ID through the event queue.
 * `icon` is an optional platform-resolved icon name or absolute file path.
 */
NK_API nk_result NK_CALL nk_notification_show(const nk_notification_options *options,
                                              nk_request_id *out_request);

/* Removes a delivered notification. A successful close emits DISMISSED. */
NK_API nk_result NK_CALL nk_notification_close(nk_request_id request);

#ifdef __cplusplus
}
#endif

#endif
