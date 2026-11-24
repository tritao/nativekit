#ifndef NATIVEKIT_GPU_SHADER_TABLE_H
#define NATIVEKIT_GPU_SHADER_TABLE_H

#include <stddef.h>
#include "nativekit_gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct nkgpu_shader_table_attribute {
    uint32_t slot;
    const char *glsl_name NK_UTF8;
    const char *hlsl_semantic NK_UTF8;
    uint32_t semantic_index;
} nkgpu_shader_table_attribute;

typedef struct nkgpu_shader_table_uniform {
    uint32_t block;
    uint32_t member;
    const char *name NK_UTF8;
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
    const char *name NK_UTF8;
} nkgpu_shader_table_texture;

typedef struct nkgpu_shader_table_storage_buffer {
    uint32_t view;
    nkgpu_shader_stage stage;
    uint32_t readonly;
    const char *name NK_UTF8;
} nkgpu_shader_table_storage_buffer;

/**
 * GLSL 4.3 and GLSL ES 3.1 add compute and storage buffers. A GL renderer uses
 * them when the table has them and otherwise falls back to GLSL 4.1 or ES 3.0.
 */
typedef enum nkgpu_shader_target {
    NKGPU_SHADER_TARGET_GLSL410 = 1,
    NKGPU_SHADER_TARGET_GLSL300ES,
    NKGPU_SHADER_TARGET_HLSL5,
    NKGPU_SHADER_TARGET_METAL_MACOS,
    NKGPU_SHADER_TARGET_METAL_IOS,
    NKGPU_SHADER_TARGET_GLSL430,
    NKGPU_SHADER_TARGET_GLSL310ES,
} nkgpu_shader_target;

/**
 * One target's program: vertex and fragment sources, or a compute source with
 * the others NULL. `threads_*` is the compute threadgroup size Metal needs.
 */
typedef struct nkgpu_shader_table_entry {
    nkgpu_shader_target target;
    const char *vertex NK_NULLABLE_UTF8;
    const char *fragment NK_NULLABLE_UTF8;
    const char *compute NK_NULLABLE_UTF8;
    uint32_t threads_x;
    uint32_t threads_y;
    uint32_t threads_z;
    const nkgpu_shader_table_attribute *attributes NK_BORROWED_ARRAY(attribute_count);
    uint32_t attribute_count;
    const nkgpu_shader_table_block *blocks NK_BORROWED_ARRAY(block_count);
    uint32_t block_count;
    const nkgpu_shader_table_uniform *uniforms NK_BORROWED_ARRAY(uniform_count);
    uint32_t uniform_count;
    const nkgpu_shader_table_texture *textures NK_BORROWED_ARRAY(texture_count);
    uint32_t texture_count;
    const nkgpu_shader_table_storage_buffer *storage_buffers NK_BORROWED_ARRAY(storage_buffer_count);
    uint32_t storage_buffer_count;
} nkgpu_shader_table_entry;

/** Creates the program variant for the renderer's backend. Source and metadata are copied. */
NKGPU_API nkgpu_result nkgpu_shader_create_from_table(nkgpu_renderer renderer,
    const nkgpu_shader_table_entry *entries NK_IN_ARRAY(count), uint32_t count, nkgpu_shader *out_shader NK_OUT);

#ifdef __cplusplus
}
#endif
#endif
