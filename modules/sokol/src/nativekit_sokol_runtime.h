#ifndef NATIVEKIT_SOKOL_RUNTIME_H
#define NATIVEKIT_SOKOL_RUNTIME_H

#include "nativekit_sokol_backend_config.h"
#include "sokol_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal NativeKit-wide lease for Sokol's process-global runtime. */
/*
 * Every NativeKit Sokol owner must use compatible environment defaults. The
 * first owner establishes the color/depth/sample configuration; later owners
 * retain the lease only when they request the same configuration.
 */
int nk_sokol_runtime_acquire(const sg_desc *desc);
void nk_sokol_runtime_release(void);

#ifdef __cplusplus
}
#endif

#endif
