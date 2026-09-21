#ifndef NATIVEKIT_LIBRARY_H
#define NATIVEKIT_LIBRARY_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit_resource.h"

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/** Generation-checked handle for a loaded dynamic library. */
typedef uint32_t nk_library NK_HANDLE NK_HANDLE_DESTROY(nk_library_close);

/* Library operations require an initialized runtime and the application executor. */

/**
 * Loads a local dynamic library identified by an absolute file URI resource.
 * Android accepts libraries accessible to the application sandbox. iOS accepts
 * only the executable inside an embedded, code-signed `.framework` bundle;
 * standalone or downloaded dynamic libraries are rejected.
 * The returned handle keeps the library loaded until nk_library_close() or
 * nk_shutdown(). This operation is available when NK_CAP_DYNAMIC_LIBRARY is
 * reported by nk_get_capabilities().
 */
NK_API nk_result NK_CALL nk_library_open(const nk_resource *resource,
                                         nk_library *out_library NK_OUT NK_OWNED);

/**
 * Resolves a symbol in a loaded library. The returned address is borrowed
 * from the library and becomes invalid after nk_library_close().
 */
NK_API nk_result NK_CALL nk_library_symbol(nk_library library, const char *name NK_UTF8,
                                           void **out_symbol NK_OUT);

/** Closes a loaded library and invalidates its handle. */
NK_API nk_result NK_CALL nk_library_close(nk_library library);

#ifdef __cplusplus
}
#endif

#endif
