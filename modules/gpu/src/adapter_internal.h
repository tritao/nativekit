#ifndef NATIVEKIT_GPU_ADAPTER_INTERNAL_H
#define NATIVEKIT_GPU_ADAPTER_INTERNAL_H

#include "nativekit_gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/* UI rendering leaves presentation to the surface owner after committing. */
NKGPU_API nkgpu_result NK_CALL nkgpu_end_frame_deferred_present(nkgpu_renderer renderer);

#ifdef __cplusplus
}
#endif

#endif
