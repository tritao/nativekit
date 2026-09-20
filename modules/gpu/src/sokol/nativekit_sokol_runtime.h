#ifndef NATIVEKIT_SOKOL_RUNTIME_H
#define NATIVEKIT_SOKOL_RUNTIME_H

#include "nativekit_sokol_backend_config.h"
#include "nativekit_graphics.h"
#include "sokol_gfx.h"

#if defined(NK_SOKOL_RUNTIME_PREFIX)
#define NK_SOKOL_RUNTIME_CAT2(a, b) a##b
#define NK_SOKOL_RUNTIME_CAT(a, b) NK_SOKOL_RUNTIME_CAT2(a, b)
#define nk_sokol_runtime_acquire NK_SOKOL_RUNTIME_CAT(NK_SOKOL_RUNTIME_PREFIX, acquire)
#define nk_sokol_runtime_is_compatible NK_SOKOL_RUNTIME_CAT(NK_SOKOL_RUNTIME_PREFIX, is_compatible)
#define nk_sokol_runtime_release NK_SOKOL_RUNTIME_CAT(NK_SOKOL_RUNTIME_PREFIX, release)
#define nk_sokol_external_image_create                                                             \
    NK_SOKOL_RUNTIME_CAT(NK_SOKOL_RUNTIME_PREFIX, external_image_create)
#define nk_sokol_external_image_release                                                            \
    NK_SOKOL_RUNTIME_CAT(NK_SOKOL_RUNTIME_PREFIX, external_image_release)
#define nk_sokol_external_image_resolve                                                            \
    NK_SOKOL_RUNTIME_CAT(NK_SOKOL_RUNTIME_PREFIX, external_image_resolve)
#define nk_sokol_transfer_get_api NK_SOKOL_RUNTIME_CAT(NK_SOKOL_RUNTIME_PREFIX, transfer_get_api)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Internal NativeKit-wide lease for Sokol's process-global runtime. */
/*
 * Every NativeKit Sokol owner must use compatible environment defaults. The
 * first owner establishes the native-device and color/depth/sample identity;
 * later owners retain the lease only when they request the same configuration
 * on that native device.  The logical NativeKit device remains a per-surface
 * lifetime token and is therefore not suitable as the runtime identity.
 */
int nk_sokol_runtime_acquire(const sg_desc *desc, nk_graphics_device device,
                             uint64_t native_device);
int nk_sokol_runtime_is_compatible(const sg_desc *desc, nk_graphics_device device,
                                   uint64_t native_device);
void nk_sokol_runtime_release(void);

/* Shared, retained sampled images used to bridge Sokol producers to consumers. */
uint32_t nk_sokol_external_image_create(sg_image image, sg_view view, int32_t width,
                                        int32_t height);
void nk_sokol_external_image_release(uint32_t image);
int nk_sokol_external_image_resolve(uint32_t image, sg_view *out_view, int32_t *out_width,
                                    int32_t *out_height);

typedef struct nk_sokol_transfer_api nk_sokol_transfer_api;

struct nk_sokol_transfer_api {
    uint32_t (*buffer_copy)(sg_buffer source, uint32_t source_offset, sg_buffer destination,
                            uint32_t destination_offset, uint32_t size);
    uint32_t (*image_copy)(sg_image source, uint32_t source_mip, uint32_t source_layer,
                           uint32_t source_x, uint32_t source_y, sg_image destination,
                           uint32_t destination_mip, uint32_t destination_layer,
                           uint32_t destination_x, uint32_t destination_y, uint32_t width,
                           uint32_t height);
    uint32_t (*buffer_to_image)(sg_buffer source, uint32_t source_offset, uint32_t row_pitch,
                                sg_image destination, uint32_t mip_level, uint32_t layer,
                                uint32_t x, uint32_t y, uint32_t width, uint32_t height);
    uint32_t (*image_to_buffer)(sg_image source, uint32_t mip_level, uint32_t layer, uint32_t x,
                                uint32_t y, uint32_t width, uint32_t height, sg_buffer destination,
                                uint32_t destination_offset, uint32_t row_pitch);
    uint32_t (*readback_begin)(sg_image source, uint32_t mip_level, uint32_t layer, uint32_t x,
                               uint32_t y, uint32_t width, uint32_t height);
    uint32_t (*readback_status)(uint32_t readback);
    uint32_t (*readback_size)(uint32_t readback);
    uint32_t (*readback_row_pitch)(uint32_t readback);
    int (*readback_read)(uint32_t readback, void *destination, uint32_t size);
    void (*readback_destroy)(uint32_t readback);
    int (*begin_pass)(void);
    int (*end_pass)(void);
};

const nk_sokol_transfer_api *nk_sokol_transfer_get_api(void);
#if defined(NK_SOKOL_BACKEND_D3D11)
const nk_sokol_transfer_api *nk_sokol_d3d11_transfer_get_api(void);
void nk_sokol_d3d11_transfer_shutdown(void);
#elif defined(NK_SOKOL_BACKEND_METAL)
const nk_sokol_transfer_api *nk_sokol_metal_transfer_get_api(void);
void nk_sokol_metal_transfer_shutdown(void);
#endif

#ifdef __cplusplus
}
#endif

#endif
