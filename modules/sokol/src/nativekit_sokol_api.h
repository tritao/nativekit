#ifndef NATIVEKIT_SOKOL_API_H
#define NATIVEKIT_SOKOL_API_H

#include "sokol_gfx.h"
#include "nativekit_graphics.h"

#if defined(NK_SOKOL_API_PREFIX)
#define NK_SOKOL_API_CAT2(a, b) a##b
#define NK_SOKOL_API_CAT(a, b) NK_SOKOL_API_CAT2(a, b)
#define nk_sokol_get_api NK_SOKOL_API_CAT(NK_SOKOL_API_PREFIX, get_api)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** NativeKit lease plus the dispatch table for one Sokol runtime. */
typedef struct nk_sokol_api {
    const sg_api *gfx;
    int (*runtime_acquire)(const sg_desc *desc, nk_graphics_device device);
    void (*runtime_release)(void);
    uint32_t (*external_image_create)(sg_image image, sg_view view, int32_t width,
                                      int32_t height);
    void (*external_image_release)(uint32_t image);
    int (*external_image_resolve)(uint32_t image, sg_view *out_view, int32_t *out_width,
                                  int32_t *out_height);
} nk_sokol_api;

const nk_sokol_api *nk_sokol_get_api(void);
const nk_sokol_api *nk_sokol_glcore_get_api(void);
const nk_sokol_api *nk_sokol_gles3_get_api(void);

#ifdef __cplusplus
}
#endif

#endif
