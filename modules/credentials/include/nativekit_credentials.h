#ifndef NATIVEKIT_CREDENTIALS_H
#define NATIVEKIT_CREDENTIALS_H

#include <stdint.h>
#include "nativekit.h"

#if defined(_WIN32)
#if defined(NKC_STATIC)
#define NKC_API
#elif defined(NKC_BUILDING_LIBRARY)
#define NKC_API __declspec(dllexport)
#else
#define NKC_API __declspec(dllimport)
#endif
#else
#define NKC_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum portable size of one stored credential value. */
enum { NK_CREDENTIALS_MAX_SECRET_BYTES = 2048 };

/**
 * Store or replace an opaque secret for this service and account key.
 * Calls may block while the operating-system credential service responds;
 * call them from a worker thread, not a UI thread. Secret values are never
 * included in diagnostics.
 */
NKC_API nk_result NK_CALL nk_credentials_set(const char *service NK_UTF8,
                                              const char *account NK_UTF8,
                                              const uint8_t *secret NK_IN_ARRAY(secret_size),
                                              uint32_t secret_size);

/**
 * Retrieve a secret. Set `secret` to NULL and `*inout_size` to zero to query
 * the required size; this returns NK_ERROR_BUFFER_TOO_SMALL when the item exists.
 * A short buffer is never partially written. `*inout_size` is always set
 * to zero on a missing item and to the required size when the item exists.
 */
NKC_API nk_result NK_CALL nk_credentials_get(const char *service NK_UTF8,
                                              const char *account NK_UTF8,
                                              uint8_t *NK_NULLABLE secret NK_OUT_BUFFER(inout_size),
                                              uint32_t *inout_size NK_INOUT);

/** Delete a stored secret. A missing item returns NK_ERROR_NOT_FOUND. */
NKC_API nk_result NK_CALL nk_credentials_delete(const char *service NK_UTF8,
                                                 const char *account NK_UTF8);

/** Thread-local diagnostic for the most recent credentials call. */
NKC_API const char *NK_CALL nk_credentials_last_error(void) NK_RETURNS_BORROWED_UTF8;

#ifdef __cplusplus
}
#endif

#endif
