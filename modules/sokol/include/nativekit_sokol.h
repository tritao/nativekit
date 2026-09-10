#ifndef NATIVEKIT_SOKOL_H
#define NATIVEKIT_SOKOL_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------------- */
/* Export visibility                                                         */
/* ------------------------------------------------------------------------- */

#if defined(_WIN32)
#define NKS_API __declspec(dllexport)
#else
#define NKS_API __attribute__((visibility("default")))
#endif

/* ------------------------------------------------------------------------- */
/* Binding annotations                                                       */
/* ------------------------------------------------------------------------- */

#if defined(__clang__)
#define NKS_OUT __attribute__((annotate("hxi:out")))
#define NKS_UTF8 __attribute__((annotate("hxi:utf8")))
#define NKS_RETURNS_BORROWED_UTF8 __attribute__((annotate("hxi:returns_borrowed_utf8")))
#else
#define NKS_OUT
#define NKS_UTF8
#define NKS_RETURNS_BORROWED_UTF8
#endif

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Handles and core types                                                    */
/* ------------------------------------------------------------------------- */

typedef uint32_t nks_nativekit_handle;
typedef int32_t nks_result;
#ifdef __cplusplus
#define NKS_HANDLE(name)                                                                           \
    typedef struct name {                                                                          \
        uint32_t id;                                                                               \
        operator uint32_t() const {                                                                \
            return id;                                                                             \
        }                                                                                          \
        name &operator=(uint32_t value) {                                                          \
            id = value;                                                                            \
            return *this;                                                                          \
        }                                                                                          \
    } name
#else
#define NKS_HANDLE(name)                                                                           \
    typedef struct name {                                                                          \
        uint32_t id;                                                                               \
    } name
#endif
NKS_HANDLE(nks_renderer);
NKS_HANDLE(nks_buffer);
NKS_HANDLE(nks_shader);
NKS_HANDLE(nks_pipeline);
NKS_HANDLE(nks_buffer_builder);
NKS_HANDLE(nks_shader_builder);
NKS_HANDLE(nks_pipeline_builder);
NKS_HANDLE(nks_uniform_builder);
NKS_HANDLE(nks_image);
NKS_HANDLE(nks_image_builder);
NKS_HANDLE(nks_sampler);
#undef NKS_HANDLE

/* ------------------------------------------------------------------------- */
/* Result codes                                                              */
/* ------------------------------------------------------------------------- */

#define NKS_OK 0
#define NKS_ERROR_UNKNOWN -1
#define NKS_ERROR_INVALID_ARGUMENT -2
#define NKS_ERROR_INVALID_HANDLE -3
#define NKS_ERROR_WRONG_STATE -4

/* ------------------------------------------------------------------------- */
/* Graphics and command types                                                */
/* ------------------------------------------------------------------------- */

typedef uint32_t nks_vertex_format;
#define NKS_VERTEXFORMAT_FLOAT 1
#define NKS_VERTEXFORMAT_FLOAT2 2
#define NKS_VERTEXFORMAT_FLOAT3 3
#define NKS_VERTEXFORMAT_FLOAT4 4

typedef uint32_t nks_buffer_usage;
#define NKS_BUFFER_VERTEX 1
#define NKS_BUFFER_INDEX 2
typedef uint32_t nks_index_type;
#define NKS_INDEXTYPE_NONE 0
#define NKS_INDEXTYPE_UINT16 1
#define NKS_INDEXTYPE_UINT32 2
typedef uint32_t nks_shader_stage;
#define NKS_SHADERSTAGE_VERTEX 1
#define NKS_SHADERSTAGE_FRAGMENT 2
typedef uint32_t nks_uniform_type;
#define NKS_UNIFORMTYPE_FLOAT 1
#define NKS_UNIFORMTYPE_FLOAT2 2
#define NKS_UNIFORMTYPE_FLOAT3 3
#define NKS_UNIFORMTYPE_FLOAT4 4
#define NKS_UNIFORMTYPE_INT 5
#define NKS_UNIFORMTYPE_INT2 6
#define NKS_UNIFORMTYPE_INT3 7
#define NKS_UNIFORMTYPE_INT4 8
#define NKS_UNIFORMTYPE_MAT4 9
typedef uint32_t nks_filter;
#define NKS_FILTER_NEAREST 1
#define NKS_FILTER_LINEAR 2
typedef uint32_t nks_wrap;
#define NKS_WRAP_REPEAT 1
#define NKS_WRAP_CLAMP_TO_EDGE 2

typedef uint32_t nks_command;
#define NKS_COMMAND_APPLY_PIPELINE 1
#define NKS_COMMAND_APPLY_VERTEX_BUFFER 2
#define NKS_COMMAND_APPLY_INDEX_BUFFER 3
#define NKS_COMMAND_APPLY_IMAGE 4
#define NKS_COMMAND_APPLY_SAMPLER 5
#define NKS_COMMAND_APPLY_UNIFORMS 6
#define NKS_COMMAND_DRAW 7

/* ------------------------------------------------------------------------- */
/* Diagnostics                                                               */
/* ------------------------------------------------------------------------- */

NKS_API const char *nks_last_error(void) NKS_RETURNS_BORROWED_UTF8;

/* ------------------------------------------------------------------------- */
/* Surface and renderer lifecycle                                            */
/* ------------------------------------------------------------------------- */

NKS_API nks_result nks_surface_create(nks_nativekit_handle nativekit_window, int32_t width,
                                      int32_t height, nks_nativekit_handle *out_surface NKS_OUT);
NKS_API nks_result nks_surface_resize(nks_nativekit_handle surface, int32_t width, int32_t height);
NKS_API nks_result nks_surface_destroy(nks_nativekit_handle surface);
NKS_API nks_result nks_renderer_create(nks_nativekit_handle nativekit_surface,
                                       nks_renderer *out_renderer NKS_OUT);
NKS_API nks_result nks_renderer_destroy(nks_renderer renderer);

/* ------------------------------------------------------------------------- */
/* Buffer APIs                                                               */
/* ------------------------------------------------------------------------- */

NKS_API nks_result nks_buffer_create(nks_renderer renderer, const uint8_t *data, uint32_t size,
                                     nks_buffer *out_buffer NKS_OUT);
NKS_API nks_result nks_buffer_begin(nks_renderer renderer, uint32_t size,
                                    nks_buffer_builder *out_builder NKS_OUT);
NKS_API nks_result nks_buffer_begin_kind(nks_renderer renderer, uint32_t size,
                                         nks_buffer_usage usage,
                                         nks_buffer_builder *out_builder NKS_OUT);
NKS_API nks_result nks_buffer_write_f32(nks_buffer_builder builder, uint32_t offset, float value);
NKS_API nks_result nks_buffer_write_u16(nks_buffer_builder builder, uint32_t offset,
                                        uint32_t value);
NKS_API nks_result nks_buffer_end(nks_buffer_builder builder, nks_buffer *out_buffer NKS_OUT);
NKS_API nks_result nks_buffer_destroy(nks_renderer renderer, nks_buffer buffer);

/* ------------------------------------------------------------------------- */
/* Shader APIs                                                               */
/* ------------------------------------------------------------------------- */

NKS_API nks_result nks_shader_create(nks_renderer renderer, const char *vertex_source NKS_UTF8,
                                     const char *fragment_source NKS_UTF8,
                                     nks_shader *out_shader NKS_OUT);
NKS_API nks_result nks_shader_destroy(nks_renderer renderer, nks_shader shader);
NKS_API nks_result nks_shader_begin(nks_renderer renderer, const char *vertex_source NKS_UTF8,
                                    const char *fragment_source NKS_UTF8,
                                    nks_shader_builder *out_builder NKS_OUT);
NKS_API nks_result nks_shader_uniform_block(nks_shader_builder builder, uint32_t slot,
                                            nks_shader_stage stage, uint32_t size);
NKS_API nks_result nks_shader_uniform(nks_shader_builder builder, uint32_t block_slot,
                                      uint32_t member_index, const char *name NKS_UTF8,
                                      nks_uniform_type type, uint32_t array_count);
NKS_API nks_result nks_shader_texture(nks_shader_builder builder, uint32_t view_slot,
                                      uint32_t sampler_slot, nks_shader_stage stage,
                                      const char *name NKS_UTF8);
NKS_API nks_result nks_shader_end(nks_shader_builder builder, nks_shader *out_shader NKS_OUT);

/* ------------------------------------------------------------------------- */
/* Pipeline APIs                                                             */
/* ------------------------------------------------------------------------- */

NKS_API nks_result nks_pipeline_begin(nks_renderer renderer, nks_shader shader, uint32_t stride,
                                      nks_pipeline_builder *out_builder NKS_OUT);
NKS_API nks_result nks_pipeline_attribute(nks_pipeline_builder builder, uint32_t location,
                                          uint32_t buffer_index, uint32_t offset,
                                          nks_vertex_format format);
NKS_API nks_result nks_pipeline_index_type(nks_pipeline_builder builder, nks_index_type type);
NKS_API nks_result nks_pipeline_end(nks_pipeline_builder builder,
                                    nks_pipeline *out_pipeline NKS_OUT);
NKS_API nks_result nks_pipeline_destroy(nks_renderer renderer, nks_pipeline pipeline);

/* ------------------------------------------------------------------------- */
/* Frame and resource APIs                                                   */
/* ------------------------------------------------------------------------- */

NKS_API nks_result nks_begin_frame(nks_renderer renderer);
NKS_API nks_result nks_apply_pipeline(nks_renderer renderer, nks_pipeline pipeline);
NKS_API nks_result nks_apply_vertex_buffer(nks_renderer renderer, uint32_t slot, nks_buffer buffer,
                                           uint32_t offset);
NKS_API nks_result nks_apply_index_buffer(nks_renderer renderer, nks_buffer buffer,
                                          uint32_t offset);
NKS_API nks_result nks_uniforms_begin(nks_renderer renderer, uint32_t size,
                                      nks_uniform_builder *out_builder NKS_OUT);
NKS_API nks_result nks_uniforms_write_f32(nks_uniform_builder builder, uint32_t offset,
                                          float value);
NKS_API nks_result nks_apply_uniforms(nks_renderer renderer, uint32_t slot,
                                      nks_uniform_builder builder);
NKS_API nks_result nks_image_begin(nks_renderer renderer, uint32_t width, uint32_t height,
                                   nks_image_builder *out_builder NKS_OUT);
NKS_API nks_result nks_image_write_rgba8(nks_image_builder builder, uint32_t x, uint32_t y,
                                         uint32_t red, uint32_t green, uint32_t blue,
                                         uint32_t alpha);
NKS_API nks_result nks_image_end(nks_image_builder builder, nks_image *out_image NKS_OUT);
NKS_API nks_result nks_image_destroy(nks_renderer renderer, nks_image image);
NKS_API nks_result nks_sampler_create(nks_renderer renderer, nks_filter min_filter,
                                      nks_filter mag_filter, nks_wrap wrap_u, nks_wrap wrap_v,
                                      nks_sampler *out_sampler NKS_OUT);
NKS_API nks_result nks_sampler_destroy(nks_renderer renderer, nks_sampler sampler);
NKS_API nks_result nks_apply_image(nks_renderer renderer, uint32_t slot, nks_image image);
NKS_API nks_result nks_apply_sampler(nks_renderer renderer, uint32_t slot, nks_sampler sampler);
NKS_API nks_result nks_draw(nks_renderer renderer, uint32_t base_element, uint32_t element_count,
                            uint32_t instance_count);
/* Commands are little-endian records: uint32 opcode, uint32 byte_size, then payload. */
NKS_API nks_result nks_submit_commands(nks_renderer renderer, const uint8_t *commands,
                                       uint32_t size);
NKS_API nks_result nks_end_frame(nks_renderer renderer);

#ifdef __cplusplus
}
#endif

#endif
