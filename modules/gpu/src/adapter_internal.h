#ifndef NATIVEKIT_GPU_ADAPTER_INTERNAL_H
#define NATIVEKIT_GPU_ADAPTER_INTERNAL_H

#include "nativekit_gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UI rendering leaves presentation to the surface owner after committing. */
NKGPU_API nkgpu_result NK_CALL nkgpu_end_frame_deferred_present(nkgpu_renderer renderer);

/* Creates a renderer from a complete platform-acquired target on RENDER. */
NKGPU_API nkgpu_result NK_CALL nkgpu_renderer_create_for_frame_target(
    nk_surface surface, const nk_surface_frame_target *frame_target, nkgpu_renderer *out_renderer);

#ifdef __cplusplus
}
#endif

#endif
