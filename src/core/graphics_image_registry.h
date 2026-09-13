#ifndef NATIVEKIT_CORE_GRAPHICS_IMAGE_REGISTRY_H
#define NATIVEKIT_CORE_GRAPHICS_IMAGE_REGISTRY_H

#include "nativekit_graphics.h"

typedef int(NK_CALL *nk_core_graphics_image_release_fn)(const void *runtime,
                                                        nk_graphics_device device,
                                                        uint64_t backend_image);

#ifdef __cplusplus
extern "C" {
#endif

NK_API nk_result NK_CALL nk_core_graphics_image_register(nk_graphics_api api,
                                                         nk_graphics_device device, int32_t width,
                                                         int32_t height, const void *runtime,
                                                         uint64_t backend_image,
                                                         nk_core_graphics_image_release_fn release,
                                                         nk_graphics_image *out_image NK_OUT);

NK_API nk_result NK_CALL nk_core_graphics_image_get_backend(nk_graphics_image image,
                                                            nk_graphics_image_info *out_info NK_OUT,
                                                            const void **out_runtime NK_OUT,
                                                            uint64_t *out_backend_image NK_OUT);

NK_API int NK_CALL nk_core_graphics_device_has_references(nk_graphics_device device);

#ifdef __cplusplus
}
#endif

#endif
