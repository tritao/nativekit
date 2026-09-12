#ifndef NATIVEKIT_SOKOL_RUNTIME_H
#define NATIVEKIT_SOKOL_RUNTIME_H

#include "nativekit_sokol_backend_config.h"
#include "nativekit_graphics.h"
#include "sokol_gfx.h"

#if defined(NK_SOKOL_RUNTIME_PREFIX)
#define NK_SOKOL_RUNTIME_CAT2(a, b) a##b
#define NK_SOKOL_RUNTIME_CAT(a, b) NK_SOKOL_RUNTIME_CAT2(a, b)
#define nk_sokol_runtime_acquire \
    NK_SOKOL_RUNTIME_CAT(NK_SOKOL_RUNTIME_PREFIX, acquire)
#define nk_sokol_runtime_release \
    NK_SOKOL_RUNTIME_CAT(NK_SOKOL_RUNTIME_PREFIX, release)
#define nk_sokol_external_image_create \
    NK_SOKOL_RUNTIME_CAT(NK_SOKOL_RUNTIME_PREFIX, external_image_create)
#define nk_sokol_external_image_release \
    NK_SOKOL_RUNTIME_CAT(NK_SOKOL_RUNTIME_PREFIX, external_image_release)
#define nk_sokol_external_image_resolve \
    NK_SOKOL_RUNTIME_CAT(NK_SOKOL_RUNTIME_PREFIX, external_image_resolve)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Internal NativeKit-wide lease for Sokol's process-global runtime. */
/*
 * Every NativeKit Sokol owner must use compatible environment defaults. The
 * first owner establishes the color/depth/sample configuration; later owners
 * retain the lease only when they request the same configuration.
 */
int nk_sokol_runtime_acquire(const sg_desc *desc, nk_graphics_device device);
void nk_sokol_runtime_release(void);

/* Shared, retained sampled images used to bridge Sokol producers to consumers. */
uint32_t nk_sokol_external_image_create(sg_image image, sg_view view, int32_t width,
                                        int32_t height);
void nk_sokol_external_image_release(uint32_t image);
int nk_sokol_external_image_resolve(uint32_t image, sg_view *out_view, int32_t *out_width,
                                    int32_t *out_height);

#ifdef __cplusplus
}
#endif

#endif
