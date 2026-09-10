#ifndef NATIVEKIT_NOTIFICATION_H
#define NATIVEKIT_NOTIFICATION_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { NK_NOTIFICATION_SILENT = 1u << 0 };

typedef struct nk_notification_options {
    uint32_t struct_size;
    uint32_t flags;
    const char *title NK_UTF8;
    const char *body NK_NULLABLE_UTF8;
    const char *icon NK_NULLABLE_UTF8;
    uint32_t timeout_ms;
    uint32_t reserved;
    uint64_t reserved2[2];
} nk_notification_options;

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
