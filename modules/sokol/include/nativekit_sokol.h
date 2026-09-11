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
#define NKS_HANDLE_ANNOTATION __attribute__((annotate("hxi:handle")))
#else
#define NKS_OUT
#define NKS_UTF8
#define NKS_RETURNS_BORROWED_UTF8
#define NKS_HANDLE_ANNOTATION
#endif

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NativeKit's Sokol adapter for rendering through a NativeKit OpenGL or GLES3
 * surface, selected by the build's NK_SOKOL_BACKEND option.
 *
 * Create a NativeKit window and surface first, then create one Sokol renderer
 * for that surface. Resources belong to the renderer that created them. Each
 * frame must be enclosed by nks_begin_frame() and nks_end_frame(). The current
 * adapter supports one renderer at a time and initializes the selected
 * graphics context for the surface.
 *
 * Functions return NKS_OK on success. On failure, call nks_last_error()
 * immediately for a diagnostic string. Handles are value types; do not free,
 * modify, or compare their internal id fields as pointers.
 */

/* ------------------------------------------------------------------------- */
/* Handles and core types                                                    */
/* ------------------------------------------------------------------------- */

/** A NativeKit window or surface handle accepted by the Sokol adapter. */
typedef uint32_t nks_nativekit_handle;

/** Result returned by a Sokol adapter operation; zero is success. */
typedef int32_t nks_result;
#ifdef __cplusplus
#define NKS_HANDLE(name)                                                                           \
    typedef struct name {                                                                          \
        /** Opaque generation-checked handle value owned by the Sokol backend. */                  \
        uint32_t id;                                                                               \
        operator uint32_t() const {                                                                \
            return id;                                                                             \
        }                                                                                          \
        name &operator=(uint32_t value) {                                                          \
            id = value;                                                                            \
            return *this;                                                                          \
        }                                                                                          \
    } name NKS_HANDLE_ANNOTATION
#else
#define NKS_HANDLE(name)                                                                           \
    typedef struct name {                                                                          \
        /** Opaque generation-checked handle value owned by the Sokol backend. */                  \
        uint32_t id;                                                                               \
    } name NKS_HANDLE_ANNOTATION
#endif
/** Renderer handle returned by nks_renderer_create(). It owns the adapter resources created with it. */
NKS_HANDLE(nks_renderer);
/** Buffer handle returned by nks_buffer_create() or nks_buffer_end(). */
NKS_HANDLE(nks_buffer);
/** Shader handle returned by nks_shader_create() or nks_shader_end(). */
NKS_HANDLE(nks_shader);
/** Pipeline handle returned by nks_pipeline_end(). */
NKS_HANDLE(nks_pipeline);
/** Temporary builder handle returned by nks_buffer_begin_kind(). */
NKS_HANDLE(nks_buffer_builder);
/** Temporary builder handle returned by nks_shader_begin(). */
NKS_HANDLE(nks_shader_builder);
/** Temporary builder handle returned by nks_pipeline_begin(). */
NKS_HANDLE(nks_pipeline_builder);
/** Temporary builder handle returned by nks_uniforms_begin(). */
NKS_HANDLE(nks_uniform_builder);
/** Image handle returned by nks_image_end(). */
NKS_HANDLE(nks_image);
/** Temporary builder handle returned by nks_image_begin(). */
NKS_HANDLE(nks_image_builder);
/** Sampler handle returned by nks_sampler_create(). */
NKS_HANDLE(nks_sampler);
#undef NKS_HANDLE
#undef NKS_HANDLE_ANNOTATION

/* ------------------------------------------------------------------------- */
/* Result codes                                                              */
/* ------------------------------------------------------------------------- */

enum {
    /** The operation completed successfully. */
    NKS_OK = 0,
/** An unexpected NativeKit, Sokol, or graphics backend failure occurred. */
    NKS_ERROR_UNKNOWN = -1,
/** An argument was null, zero, out of range, or otherwise invalid. */
    NKS_ERROR_INVALID_ARGUMENT = -2,
/** A handle is stale, invalid, or belongs to a different renderer. */
    NKS_ERROR_INVALID_HANDLE = -3,
/** The operation is not valid in the current renderer or frame state. */
    NKS_ERROR_WRONG_STATE = -4,
};

/* ------------------------------------------------------------------------- */
/* Graphics and command types                                                */
/* ------------------------------------------------------------------------- */

/** Vertex attribute data format used by nks_pipeline_attribute(). */
typedef uint32_t nks_vertex_format;

enum {
/** A single 32-bit floating-point vertex component. */
    NKS_VERTEXFORMAT_FLOAT = 1,
/** Two 32-bit floating-point vertex components. */
    NKS_VERTEXFORMAT_FLOAT2 = 2,
/** Three 32-bit floating-point vertex components. */
    NKS_VERTEXFORMAT_FLOAT3 = 3,
/** Four 32-bit floating-point vertex components. */
    NKS_VERTEXFORMAT_FLOAT4 = 4,
};

/** The intended use of a buffer created by nks_buffer_begin_kind(). */
typedef uint32_t nks_buffer_usage;

enum {
/** Use the buffer as vertex data. */
    NKS_BUFFER_VERTEX = 1,
/** Use the buffer as index data. */
    NKS_BUFFER_INDEX = 2,
};

/** How an applied index buffer is interpreted by the pipeline. */
typedef uint32_t nks_index_type;

enum {
/** Do not use indexed drawing for the pipeline. */
    NKS_INDEXTYPE_NONE = 0,
/** Interpret index elements as unsigned 16-bit values. */
    NKS_INDEXTYPE_UINT16 = 1,
/** Interpret index elements as unsigned 32-bit values. */
    NKS_INDEXTYPE_UINT32 = 2,
};

/** Shader stage associated with a uniform block or texture binding. */
typedef uint32_t nks_shader_stage;

enum {
/** The vertex shader stage. */
    NKS_SHADERSTAGE_VERTEX = 1,
/** The fragment shader stage. */
    NKS_SHADERSTAGE_FRAGMENT = 2,
};

/** Data type used to describe a shader uniform member. */
typedef uint32_t nks_uniform_type;

enum {
/** One floating-point value. */
    NKS_UNIFORMTYPE_FLOAT = 1,
/** Two floating-point values. */
    NKS_UNIFORMTYPE_FLOAT2 = 2,
/** Three floating-point values. */
    NKS_UNIFORMTYPE_FLOAT3 = 3,
/** Four floating-point values. */
    NKS_UNIFORMTYPE_FLOAT4 = 4,
/** One integer value. */
    NKS_UNIFORMTYPE_INT = 5,
/** Two integer values. */
    NKS_UNIFORMTYPE_INT2 = 6,
/** Three integer values. */
    NKS_UNIFORMTYPE_INT3 = 7,
/** Four integer values. */
    NKS_UNIFORMTYPE_INT4 = 8,
/** A 4-by-4 matrix of floating-point values. */
    NKS_UNIFORMTYPE_MAT4 = 9,
};

/** Texture minification or magnification filter. */
typedef uint32_t nks_filter;

enum {
/** Choose the nearest texel when sampling. */
    NKS_FILTER_NEAREST = 1,
/** Interpolate neighboring texels when sampling. */
    NKS_FILTER_LINEAR = 2,
};

/** Texture-coordinate behavior outside the [0, 1] range. */
typedef uint32_t nks_wrap;

enum {
/** Repeat the texture at integer coordinate boundaries. */
    NKS_WRAP_REPEAT = 1,
/** Clamp texture coordinates to the edge texels. */
    NKS_WRAP_CLAMP_TO_EDGE = 2,
};

/** Opcode stored in the first word of a packed command record. */
typedef uint32_t nks_command;

enum {
/** Apply a pipeline; payload: one nks_pipeline handle. */
    NKS_COMMAND_APPLY_PIPELINE = 1,
/** Apply a vertex buffer; payload: slot, buffer handle, and byte offset. */
    NKS_COMMAND_APPLY_VERTEX_BUFFER = 2,
/** Apply an index buffer; payload: buffer handle and byte offset. */
    NKS_COMMAND_APPLY_INDEX_BUFFER = 3,
/** Apply an image; payload: view slot and image handle. */
    NKS_COMMAND_APPLY_IMAGE = 4,
/** Apply a sampler; payload: sampler slot and sampler handle. */
    NKS_COMMAND_APPLY_SAMPLER = 5,
/** Apply uniform bytes; payload: block slot, byte count, and uniform bytes. */
    NKS_COMMAND_APPLY_UNIFORMS = 6,
/** Draw primitives; payload: base element, element count, and instance count. */
    NKS_COMMAND_DRAW = 7,
};

/* ------------------------------------------------------------------------- */
/* Diagnostics                                                               */
/* ------------------------------------------------------------------------- */

/**
 * Returns the most recent adapter diagnostic.
 *
 * The returned UTF-8 string is borrowed and is overwritten by a later failed
 * call. It remains owned by the adapter; do not free it. Call this immediately
 * after any operation that does not return NKS_OK.
 */
NKS_API const char *nks_last_error(void) NKS_RETURNS_BORROWED_UTF8;

/* ------------------------------------------------------------------------- */
/* Surface and renderer lifecycle                                            */
/* ------------------------------------------------------------------------- */

/**
 * Creates a surface for the selected Sokol graphics backend associated with a
 * NativeKit window.
 *
 * `nativekit_window` must be a valid NativeKit window handle, and `width` and
 * `height` must be positive. The current adapter requests the configured
 * OpenGL or GLES3 surface. On NKS_OK, writes the NativeKit surface handle to
 * `out_surface`; destroy it with nks_surface_destroy() after destroying its
 * renderer.
 */
NKS_API nks_result nks_surface_create(nks_nativekit_handle nativekit_window, int32_t width,
                                      int32_t height, nks_nativekit_handle *out_surface NKS_OUT);

/**
 * Requests a new size for a Sokol surface.
 *
 * `width` and `height` are the surface's logical dimensions and must be
 * positive. The framebuffer may have a different pixel size; begin each frame
 * with nks_begin_frame() so the adapter can query the current framebuffer.
 */
NKS_API nks_result nks_surface_resize(nks_nativekit_handle surface, int32_t width, int32_t height);

/**
 * Destroys a Sokol surface.
 *
 * Destroy the renderer first. The surface handle becomes invalid after this
 * call and must not be reused.
 */
NKS_API nks_result nks_surface_destroy(nks_nativekit_handle surface);

/**
 * Creates the Sokol renderer for a ready NativeKit surface.
 *
 * The current adapter supports one renderer at a time. On NKS_OK, writes a
 * renderer handle to `out_renderer`; all buffers, shaders, pipelines, images,
 * samplers, and builders created through it belong to that renderer.
 */
NKS_API nks_result nks_renderer_create(nks_nativekit_handle nativekit_surface,
                                       nks_renderer *out_renderer NKS_OUT);

/**
 * Destroys a renderer and all resources still owned by it.
 *
 * The renderer must not have an active frame. This also releases unfinished
 * builders, so retain a resource handle only while its renderer is alive.
 */
NKS_API nks_result nks_renderer_destroy(nks_renderer renderer);

/* ------------------------------------------------------------------------- */
/* Buffer APIs                                                               */
/* ------------------------------------------------------------------------- */

/**
 * Creates a GPU buffer from an existing byte array.
 *
 * The adapter copies `size` bytes from `data`. Both must be non-null and
 * non-zero. Use nks_buffer_begin_kind() when the buffer must be explicitly
 * marked as a vertex or index buffer. On NKS_OK, writes the new handle to
 * `out_buffer`.
 */
NKS_API nks_result nks_buffer_create(nks_renderer renderer, const uint8_t *data, uint32_t size,
                                     nks_buffer *out_buffer NKS_OUT);

/**
 * Starts building a zero-initialized vertex buffer of `size` bytes.
 *
 * This is shorthand for nks_buffer_begin_kind() with NKS_BUFFER_VERTEX. Fill
 * the builder with nks_buffer_write_f32() or nks_buffer_write_u16(), then call
 * nks_buffer_end(), which consumes the builder and creates the GPU buffer.
 */
NKS_API nks_result nks_buffer_begin(nks_renderer renderer, uint32_t size,
                                    nks_buffer_builder *out_builder NKS_OUT);

/**
 * Starts building a zero-initialized vertex or index buffer.
 *
 * `usage` must be NKS_BUFFER_VERTEX or NKS_BUFFER_INDEX. The returned builder
 * owns a temporary `size`-byte array until nks_buffer_end() or renderer
 * destruction. On NKS_OK, writes the builder handle to `out_builder`.
 */
NKS_API nks_result nks_buffer_begin_kind(nks_renderer renderer, uint32_t size,
                                         nks_buffer_usage usage,
                                         nks_buffer_builder *out_builder NKS_OUT);

/**
 * Writes one 32-bit floating-point value at a byte offset in a buffer builder.
 *
 * The four-byte write must fit within the builder's allocated range. The
 * offset is relative to the beginning of the buffer and is not adjusted for
 * vertex layout or alignment.
 */
NKS_API nks_result nks_buffer_write_f32(nks_buffer_builder builder, uint32_t offset, float value);

/**
 * Writes one unsigned 16-bit value at a byte offset in a buffer builder.
 *
 * `value` must be at most 65535, and the two-byte write must fit within the
 * builder's allocated range. The value is passed as uint32_t so bindings can
 * use one common integer type for pixel and buffer writes.
 */
NKS_API nks_result nks_buffer_write_u16(nks_buffer_builder builder, uint32_t offset,
                                        uint32_t value);

/**
 * Uploads a completed buffer and consumes its builder.
 *
 * On NKS_OK, writes the GPU buffer handle to `out_buffer`. The builder is no
 * longer valid after this call, including when GPU creation reports an error.
 */
NKS_API nks_result nks_buffer_end(nks_buffer_builder builder, nks_buffer *out_buffer NKS_OUT);

/** Destroys a buffer owned by `renderer`; the handle becomes invalid. */
NKS_API nks_result nks_buffer_destroy(nks_renderer renderer, nks_buffer buffer);

/* ------------------------------------------------------------------------- */
/* Shader APIs                                                               */
/* ------------------------------------------------------------------------- */

/**
 * Creates a shader from NUL-terminated vertex and fragment shader source.
 *
 * The source strings are UTF-8 GLSL for the configured OpenGL or GLES3
 * backend. Both stages are required. On NKS_OK, writes the shader handle to
 * `out_shader`.
 */
NKS_API nks_result nks_shader_create(nks_renderer renderer, const char *vertex_source NKS_UTF8,
                                     const char *fragment_source NKS_UTF8,
                                     nks_shader *out_shader NKS_OUT);

/** Destroys a shader owned by `renderer`; the handle becomes invalid. */
NKS_API nks_result nks_shader_destroy(nks_renderer renderer, nks_shader shader);

/**
 * Starts building a shader with UTF-8 vertex and fragment source strings.
 *
 * Unlike nks_shader_create(), this lets you describe uniform blocks, members,
 * and texture bindings before nks_shader_end() creates the shader. The source
 * strings are copied, so they may be released after this call succeeds.
 */
NKS_API nks_result nks_shader_begin(nks_renderer renderer, const char *vertex_source NKS_UTF8,
                                    const char *fragment_source NKS_UTF8,
                                    nks_shader_builder *out_builder NKS_OUT);

/**
 * Describes one shader uniform block in a shader builder.
 *
 * `slot` is the block binding slot, `stage` selects the vertex or fragment
 * shader, and `size` is the block's byte size. The uniform data later supplied
 * to nks_apply_uniforms() must use the same layout and size.
 */
NKS_API nks_result nks_shader_uniform_block(nks_shader_builder builder, uint32_t slot,
                                            nks_shader_stage stage, uint32_t size);

/**
 * Describes one member of a shader uniform block.
 *
 * `name` is the UTF-8 GLSL uniform member name. `array_count` describes an
 * array and zero is treated as one element. The member's byte layout still
 * follows the shader backend's uniform packing rules.
 */
NKS_API nks_result nks_shader_uniform(nks_shader_builder builder, uint32_t block_slot,
                                      uint32_t member_index, const char *name NKS_UTF8,
                                      nks_uniform_type type, uint32_t array_count);

/**
 * Describes a 2D filtering texture binding in a shader builder.
 *
 * `view_slot` and `sampler_slot` are the slots used later by nks_apply_image()
 * and nks_apply_sampler(). `name` is the UTF-8 GLSL combined texture/sampler
 * name, and `stage` selects the vertex or fragment shader.
 */
NKS_API nks_result nks_shader_texture(nks_shader_builder builder, uint32_t view_slot,
                                      uint32_t sampler_slot, nks_shader_stage stage,
                                      const char *name NKS_UTF8);

/**
 * Creates a shader from a shader builder and consumes the builder.
 *
 * On NKS_OK, writes the shader handle to `out_shader`. The builder is no
 * longer valid after this call, including when shader creation fails.
 */
NKS_API nks_result nks_shader_end(nks_shader_builder builder, nks_shader *out_shader NKS_OUT);

/* ------------------------------------------------------------------------- */
/* Pipeline APIs                                                             */
/* ------------------------------------------------------------------------- */

/**
 * Starts building a pipeline for a shader.
 *
 * `stride` is the byte distance between vertices in vertex buffer slot zero.
 * The shader must belong to `renderer`. Add vertex attributes and, when
 * needed, an index type before calling nks_pipeline_end().
 */
NKS_API nks_result nks_pipeline_begin(nks_renderer renderer, nks_shader shader, uint32_t stride,
                                      nks_pipeline_builder *out_builder NKS_OUT);

/**
 * Configures one vertex attribute in a pipeline builder.
 *
 * `location` is the shader attribute location, `buffer_index` selects the
 * vertex buffer slot, and `offset` is the attribute's byte offset within one
 * vertex. Call this once for each attribute consumed by the shader.
 */
NKS_API nks_result nks_pipeline_attribute(nks_pipeline_builder builder, uint32_t location,
                                          uint32_t buffer_index, uint32_t offset,
                                          nks_vertex_format format);

/**
 * Selects how the pipeline interprets an applied index buffer.
 *
 * Use NKS_INDEXTYPE_NONE for non-indexed drawing, or select the element width
 * used by the buffer passed to nks_apply_index_buffer().
 */
NKS_API nks_result nks_pipeline_index_type(nks_pipeline_builder builder, nks_index_type type);

/**
 * Creates a pipeline from a pipeline builder and consumes the builder.
 *
 * On NKS_OK, writes the pipeline handle to `out_pipeline`. The builder is no
 * longer valid after this call, including when pipeline creation fails.
 */
NKS_API nks_result nks_pipeline_end(nks_pipeline_builder builder,
                                    nks_pipeline *out_pipeline NKS_OUT);

/** Destroys a pipeline owned by `renderer`; the handle becomes invalid. */
NKS_API nks_result nks_pipeline_destroy(nks_renderer renderer, nks_pipeline pipeline);

/* ------------------------------------------------------------------------- */
/* Frame and resource APIs                                                   */
/* ------------------------------------------------------------------------- */

/**
 * Begins a frame for a renderer.
 *
 * This makes the renderer's surface current, refreshes its framebuffer size,
 * clears the frame, and resets resource bindings. Only one renderer may have
 * an active frame. Pair every successful call with nks_end_frame(), and use
 * the apply and draw functions only between those calls.
 */
NKS_API nks_result nks_begin_frame(nks_renderer renderer);

/** Applies a pipeline to the currently active frame. */
NKS_API nks_result nks_apply_pipeline(nks_renderer renderer, nks_pipeline pipeline);

/**
 * Binds a vertex buffer to a slot in the currently active frame.
 *
 * `offset` is a byte offset into the buffer. The slot must match the buffer
 * index configured with nks_pipeline_attribute().
 */
NKS_API nks_result nks_apply_vertex_buffer(nks_renderer renderer, uint32_t slot, nks_buffer buffer,
                                           uint32_t offset);

/** Binds an index buffer and byte offset to the currently active frame. */
NKS_API nks_result nks_apply_index_buffer(nks_renderer renderer, nks_buffer buffer,
                                          uint32_t offset);

/**
 * Starts a zero-initialized uniform byte block of `size` bytes.
 *
 * Fill it with nks_uniforms_write_f32(), then pass it to
 * nks_apply_uniforms(), which consumes the builder. The bytes must match the
 * uniform block layout declared for the shader.
 */
NKS_API nks_result nks_uniforms_begin(nks_renderer renderer, uint32_t size,
                                      nks_uniform_builder *out_builder NKS_OUT);

/** Writes one 32-bit floating-point uniform value at a byte offset. */
NKS_API nks_result nks_uniforms_write_f32(nks_uniform_builder builder, uint32_t offset,
                                          float value);

/**
 * Applies a uniform builder to a block slot in the active frame.
 *
 * `slot` must be the block slot described with nks_shader_uniform_block(). On
 * success, the builder's bytes have been submitted and the builder is
 * consumed; do not use it again.
 */
NKS_API nks_result nks_apply_uniforms(nks_renderer renderer, uint32_t slot,
                                      nks_uniform_builder builder);

/** Starts building a zero-initialized RGBA8 image of the requested size. */
NKS_API nks_result nks_image_begin(nks_renderer renderer, uint32_t width, uint32_t height,
                                   nks_image_builder *out_builder NKS_OUT);

/**
 * Writes one RGBA8 pixel into an image builder.
 *
 * `x` and `y` are zero-based pixel coordinates. Each color component must be
 * in the inclusive range 0..255.
 */
NKS_API nks_result nks_image_write_rgba8(nks_image_builder builder, uint32_t x, uint32_t y,
                                         uint32_t red, uint32_t green, uint32_t blue,
                                         uint32_t alpha);

/**
 * Uploads an image and creates its texture view, consuming the builder.
 *
 * On NKS_OK, writes the image handle to `out_image`. The image can then be
 * bound with nks_apply_image().
 */
NKS_API nks_result nks_image_end(nks_image_builder builder, nks_image *out_image NKS_OUT);

/** Destroys an image and its texture view; the handle becomes invalid. */
NKS_API nks_result nks_image_destroy(nks_renderer renderer, nks_image image);

/**
 * Creates a texture sampler with independent minification and magnification
 * filters and U/V wrap modes.
 *
 * On NKS_OK, writes the sampler handle to `out_sampler`; bind it with
 * nks_apply_sampler().
 */
NKS_API nks_result nks_sampler_create(nks_renderer renderer, nks_filter min_filter,
                                      nks_filter mag_filter, nks_wrap wrap_u, nks_wrap wrap_v,
                                      nks_sampler *out_sampler NKS_OUT);

/** Destroys a sampler owned by `renderer`; the handle becomes invalid. */
NKS_API nks_result nks_sampler_destroy(nks_renderer renderer, nks_sampler sampler);

/** Binds an image view to a slot in the currently active frame. */
NKS_API nks_result nks_apply_image(nks_renderer renderer, uint32_t slot, nks_image image);

/** Binds a sampler to a slot in the currently active frame. */
NKS_API nks_result nks_apply_sampler(nks_renderer renderer, uint32_t slot, nks_sampler sampler);

/**
 * Draws `element_count` elements for `instance_count` instances.
 *
 * A pipeline and the buffers required by it must already be applied in the
 * active frame. `base_element` is the first vertex or index to draw. The
 * binding state is submitted when this function is called.
 */
NKS_API nks_result nks_draw(nks_renderer renderer, uint32_t base_element, uint32_t element_count,
                            uint32_t instance_count);

/**
 * Submits a packed little-endian command stream in the active frame.
 *
 * Each record starts with two little-endian uint32 values: an opcode and the
 * record's total byte size, including its 8-byte header. The payload layouts
 * are documented by the NKS_COMMAND_* constants. A uniform record contains
 * slot, byte count, and that many uniform bytes. The stream must contain whole
 * records with no trailing bytes; submission stops at the first invalid record.
 */
NKS_API nks_result nks_submit_commands(nks_renderer renderer, const uint8_t *commands,
                                       uint32_t size);

/**
 * Ends the active frame, commits its Sokol commands, and presents the surface.
 *
 * This must be called after a successful nks_begin_frame(). The renderer is
 * ready for another frame only after this call succeeds.
 */
NKS_API nks_result nks_end_frame(nks_renderer renderer);

#ifdef __cplusplus
}
#endif

#endif
