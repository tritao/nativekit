#ifndef NATIVEKIT_SOKOL_RUNTIME_H
#define NATIVEKIT_SOKOL_RUNTIME_H

#define SOKOL_GLCORE
#include "sokol_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Internal NativeKit-wide lease for Sokol's process-global runtime. */
int nk_sokol_runtime_acquire(const sg_desc *desc);
void nk_sokol_runtime_release(void);

#ifdef __cplusplus
}
#endif

#endif
