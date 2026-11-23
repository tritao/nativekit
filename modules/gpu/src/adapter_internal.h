#ifndef NATIVEKIT_GPU_ADAPTER_INTERNAL_H
#define NATIVEKIT_GPU_ADAPTER_INTERNAL_H

#include "nativekit_gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Begins a multi-pass frame from an immutable platform-acquired target. */
NKGPU_API nkgpu_result NK_CALL
nkgpu_frame_begin_with_target(nkgpu_renderer renderer, const nk_surface_frame_target *frame_target);

/* Creates a renderer from a complete platform-acquired target on RENDER. */
NKGPU_API nkgpu_result NK_CALL nkgpu_renderer_create_for_frame_target(
    nk_surface surface, const nk_surface_frame_target *frame_target, nkgpu_renderer *out_renderer);

#ifdef __cplusplus
}
#endif

#endif
