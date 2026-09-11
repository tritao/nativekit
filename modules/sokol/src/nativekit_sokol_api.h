#ifndef NATIVEKIT_SOKOL_API_H
#define NATIVEKIT_SOKOL_API_H

#include "sokol_gfx.h"

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
    int (*runtime_acquire)(const sg_desc *desc);
    void (*runtime_release)(void);
} nk_sokol_api;

const nk_sokol_api *nk_sokol_get_api(void);
const nk_sokol_api *nk_sokol_glcore_get_api(void);
const nk_sokol_api *nk_sokol_gles3_get_api(void);

#ifdef __cplusplus
}
#endif

#endif
