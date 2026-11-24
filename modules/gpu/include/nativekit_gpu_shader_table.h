#ifndef NATIVEKIT_GPU_SHADER_TABLE_H
#define NATIVEKIT_GPU_SHADER_TABLE_H

#include <stddef.h>
#include "nativekit_gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nkgpu_shader_table_attribute {
    uint32_t slot;
    const char *glsl_name;
    const char *hlsl_semantic;
    uint32_t semantic_index;
} nkgpu_shader_table_attribute;

typedef struct nkgpu_shader_table_uniform {
    uint32_t block;
    uint32_t member;
    const char *name;
    nkgpu_uniform_type type;
    uint32_t count;
} nkgpu_shader_table_uniform;

typedef struct nkgpu_shader_table_block {
    uint32_t slot;
    nkgpu_shader_stage stage;
    uint32_t size;
} nkgpu_shader_table_block;

typedef struct nkgpu_shader_table_texture {
    uint32_t view;
    uint32_t sampler;
    nkgpu_shader_stage stage;
    nkgpu_image_type type;
    const char *name;
} nkgpu_shader_table_texture;

typedef struct nkgpu_shader_table_entry {
    nkgpu_backend backend;
    uint32_t metal_ios;
    nkgpu_shader_language language;
    const char *vertex;
    const char *fragment;
    const nkgpu_shader_table_attribute *attributes;
    uint32_t attribute_count;
    const nkgpu_shader_table_block *blocks;
    uint32_t block_count;
    const nkgpu_shader_table_uniform *uniforms;
    uint32_t uniform_count;
    const nkgpu_shader_table_texture *textures;
    uint32_t texture_count;
} nkgpu_shader_table_entry;

/** Creates the program variant for the renderer's backend. Source and metadata are copied. */
NKGPU_API nkgpu_result nkgpu_shader_create_from_table(nkgpu_renderer renderer,
    const nkgpu_shader_table_entry *entries, uint32_t count, nkgpu_shader *out_shader);

#ifdef __cplusplus
}
#endif
#endif
