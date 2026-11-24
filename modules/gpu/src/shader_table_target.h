#ifndef NATIVEKIT_SHADER_TABLE_TARGET_H
#define NATIVEKIT_SHADER_TABLE_TARGET_H

#include "nativekit_gpu_shader_table.h"

/** The backend's preferred table target, and the one it accepts when the table lacks it. */
typedef struct shader_table_target {
    nkgpu_shader_target target;
    nkgpu_shader_target fallback;
    nkgpu_shader_language language;
} shader_table_target;

static inline shader_table_target shader_table_target_for_backend(nkgpu_backend backend,
                                                                    int metal_ios) {
    shader_table_target result = {(nkgpu_shader_target)0, (nkgpu_shader_target)0, (nkgpu_shader_language)0};
    switch (backend) {
    case NKGPU_BACKEND_GLCORE:
        result.target = NKGPU_SHADER_TARGET_GLSL430;
        result.fallback = NKGPU_SHADER_TARGET_GLSL410;
        result.language = NKGPU_SHADERLANGUAGE_GLSL;
        break;
    case NKGPU_BACKEND_GLES3:
        result.target = NKGPU_SHADER_TARGET_GLSL310ES;
        result.fallback = NKGPU_SHADER_TARGET_GLSL300ES;
        result.language = NKGPU_SHADERLANGUAGE_GLSL;
        break;
    case NKGPU_BACKEND_D3D11:
        result.target = result.fallback = NKGPU_SHADER_TARGET_HLSL5;
        result.language = NKGPU_SHADERLANGUAGE_HLSL5;
        break;
    case NKGPU_BACKEND_METAL:
        result.target = result.fallback =
            metal_ios ? NKGPU_SHADER_TARGET_METAL_IOS : NKGPU_SHADER_TARGET_METAL_MACOS;
        result.language = NKGPU_SHADERLANGUAGE_MSL;
        break;
    default:
        break;
    }
    return result;
}

#endif
