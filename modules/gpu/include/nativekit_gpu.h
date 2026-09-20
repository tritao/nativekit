#ifndef NATIVEKIT_GPU_H
#define NATIVEKIT_GPU_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include <stddef.h>
#include <stdint.h>

#include "nativekit_graphics.h"

/* ------------------------------------------------------------------------- */
/* Export visibility                                                         */
/* ------------------------------------------------------------------------- */

#if defined(_WIN32)
#if defined(NK_STATIC)
#define NKGPU_API
#elif defined(NKGPU_BUILDING_LIBRARY)
#define NKGPU_API __declspec(dllexport)
#else
#define NKGPU_API __declspec(dllimport)
#endif
#else
#define NKGPU_API __attribute__((visibility("default")))
#endif

#if defined(_MSC_VER)
#define NKGPU_DEPRECATED(message) __declspec(deprecated(message))
#elif defined(__cplusplus) && defined(__clang__)
#define NKGPU_DEPRECATED(message) [[deprecated(message)]]
#elif defined(__GNUC__) || defined(__clang__)
#define NKGPU_DEPRECATED(message) __attribute__((deprecated(message)))
#else
#define NKGPU_DEPRECATED(message)
#endif

/* ------------------------------------------------------------------------- */
/* Binding annotations                                                       */
/* ------------------------------------------------------------------------- */

#if defined(__clang__)
#define NKGPU_OUT __attribute__((annotate("hxi:out")))
#define NKGPU_UTF8 __attribute__((annotate("hxi:utf8")))
#define NKGPU_RETURNS_BORROWED_UTF8 __attribute__((annotate("hxi:returns_borrowed_utf8")))
#define NKGPU_HANDLE_ANNOTATION __attribute__((annotate("hxi:handle")))
#else
#define NKGPU_OUT
#define NKGPU_UTF8
#define NKGPU_RETURNS_BORROWED_UTF8
#define NKGPU_HANDLE_ANNOTATION
#endif

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NativeKit's GPU adapter for rendering through a NativeKit graphics surface.
 * A normal build includes the configured graphics backend; desktop Linux
 * builds with NK_BUILD_GPU_BACKEND_MATRIX include both GLCore and GLES3
 * runtimes and dispatch according to each surface's graphics API.
 *
 * Create a NativeKit window and surface first, then create a GPU renderer
 * for that surface. Renderer handles may coexist when their surfaces use a
 * compatible graphics device/share group; calls and frames are serialized on
 * the graphics thread, with at most one active frame at a time. Resources
 * belong to the renderer that created them. Each window frame must be enclosed
 * by nkgpu_begin_frame() and nkgpu_end_frame(). The selected runtime backend is
 * derived from the surface's graphics API, not from a global build-time choice
 * when multiple runtimes are compiled into the library.
 *
 * The renderer alternates between Ready, a window frame, and active render
 * passes. Begin operations require Ready or a frame with no active pass. A
 * standalone offscreen pass must end with nkgpu_end_render_target(); a pass
 * inside a window frame must end with nkgpu_end_pass(). These end operations
 * are not interchangeable. Drawing and binding operations require an active
 * pass. Resource creation and destruction require no active pass, except image
 * and sampler creation and image updates, which may also occur inside the
 * active renderer's pass for streaming texture workloads. Destroying an idle
 * renderer invalidates all of its remaining resources and unfinished builders;
 * destroying a resource with a different renderer returns
 * NKGPU_ERROR_INVALID_HANDLE.
 *
 * The public renderer lifecycle is Ready -> FrameActive -> Ready for window
 * frames and Ready -> RenderTargetActive -> Ready for standalone render-target
 * passes. A fatal backend/device failure moves the renderer to Lost. Lost
 * renderers reject rendering and resource creation with
 * NKGPU_ERROR_DEVICE_LOST; destroying them remains safe. Surface resize and
 * framebuffer/DPR changes do not imply device loss.
 *
 * Functions return NKGPU_OK on success. On failure, call nkgpu_last_error()
 * immediately for a diagnostic string. Handles are value types; do not free,
 * modify, or compare their internal id fields as pointers.
 */

/* ------------------------------------------------------------------------- */
/* Handles and core types                                                    */
/* ------------------------------------------------------------------------- */

/** Portable limits used by descriptor arrays in the NativeKit GPU ABI. */
enum {
    NKGPU_MAX_COLOR_ATTACHMENTS = 4,
    NKGPU_MAX_VERTEX_BUFFERS = 8,
};

/** Source-compatible alias for a NativeKit window or surface handle. */
typedef nk_handle nkgpu_nativekit_handle;

/** Result returned by a GPU adapter operation; zero is success. */
typedef int32_t nkgpu_result;
#ifdef __cplusplus
#define NKGPU_HANDLE(name)                                                                         \
    typedef struct name {                                                                          \
        /** Opaque generation-checked handle value owned by the GPU backend. */                    \
        uint32_t id;                                                                               \
        operator uint32_t() const {                                                                \
            return id;                                                                             \
        }                                                                                          \
        name &operator=(uint32_t value) {                                                          \
            id = value;                                                                            \
            return *this;                                                                          \
        }                                                                                          \
    } name NKGPU_HANDLE_ANNOTATION
#else
#define NKGPU_HANDLE(name)                                                                         \
    typedef struct name {                                                                          \
        /** Opaque generation-checked handle value owned by the GPU backend. */                    \
        uint32_t id;                                                                               \
    } name NKGPU_HANDLE_ANNOTATION
#endif
/** Renderer handle returned by nkgpu_renderer_create(). It owns the adapter
 * resources and unfinished builders created with it; renderer destruction
 * releases all of them. */
NKGPU_HANDLE(nkgpu_renderer);
/** Buffer handle returned by nkgpu_buffer_create() or nkgpu_buffer_end(). */
NKGPU_HANDLE(nkgpu_buffer);
/** Shader handle returned by nkgpu_shader_create() or nkgpu_shader_end(). */
NKGPU_HANDLE(nkgpu_shader);
/** Pipeline handle returned by nkgpu_pipeline_end(). */
NKGPU_HANDLE(nkgpu_pipeline);
/** Temporary builder handle returned by nkgpu_buffer_begin_kind(). */
NKGPU_HANDLE(nkgpu_buffer_builder);
/** Temporary builder handle returned by nkgpu_shader_begin(). */
NKGPU_HANDLE(nkgpu_shader_builder);
/** Temporary builder handle returned by nkgpu_pipeline_begin(). */
NKGPU_HANDLE(nkgpu_pipeline_builder);
/** Temporary builder handle returned by nkgpu_uniforms_begin(). */
NKGPU_HANDLE(nkgpu_uniform_builder);
/** Image handle returned by nkgpu_image_end(). */
NKGPU_HANDLE(nkgpu_image);
/** Temporary builder handle returned by nkgpu_image_begin(). */
NKGPU_HANDLE(nkgpu_image_builder);
/** Sampler handle returned by nkgpu_sampler_create(). */
NKGPU_HANDLE(nkgpu_sampler);
/** Offscreen render target owned by a renderer. */
NKGPU_HANDLE(nkgpu_render_target);
/** Sealed-submission batch handle returned by nkgpu_batch_begin(). */
NKGPU_HANDLE(nkgpu_batch);
/** Asynchronous readback handle returned by nkgpu_readback_begin_image(). */
NKGPU_HANDLE(nkgpu_readback);
#undef NKGPU_HANDLE
#undef NKGPU_HANDLE_ANNOTATION

/* ------------------------------------------------------------------------- */
/* Result codes                                                              */
/* ------------------------------------------------------------------------- */

enum NK_ENUM(nkgpu_result) {
    /** The operation completed successfully. */
    NKGPU_OK = 0,
    /** An unexpected NativeKit or graphics backend failure occurred. */
    NKGPU_ERROR_UNKNOWN = -1,
    /** An argument was null, zero, out of range, or otherwise invalid. */
    NKGPU_ERROR_INVALID_ARGUMENT = -2,
    /** A handle is stale, invalid, or belongs to a different renderer. */
    NKGPU_ERROR_INVALID_HANDLE = -3,
    /** The operation is not valid in the current renderer or frame state. */
    NKGPU_ERROR_WRONG_STATE = -4,
    /** The renderer's graphics device or backend has been lost. */
    NKGPU_ERROR_DEVICE_LOST = -5,
    /** A backend or adapter resource allocation failed. */
    NKGPU_ERROR_OUT_OF_MEMORY = -6,
    /** The call was made from a thread that does not satisfy the executor it requires. */
    NKGPU_ERROR_WRONG_THREAD = -7,
    /** The selected backend does not expose the requested optional operation. */
    NKGPU_ERROR_UNSUPPORTED = -8,
};

/** State of an asynchronous GPU readback. */
typedef uint32_t nkgpu_readback_state;
enum NK_ENUM(nkgpu_readback_state) {
    NKGPU_READBACK_PENDING = 1,
    NKGPU_READBACK_READY = 2,
    NKGPU_READBACK_FAILED = 3,
};

/** Observable renderer lifecycle state. */
typedef uint32_t nkgpu_renderer_state;
enum NK_ENUM(nkgpu_renderer_state) {
    /** The renderer is ready to begin a frame or standalone target pass. */
    NKGPU_RENDERER_READY = 0,
    /** A window frame is active, including frames between render passes. */
    NKGPU_RENDERER_FRAME_ACTIVE = 1,
    /** A standalone offscreen target pass is active. */
    NKGPU_RENDERER_RENDER_TARGET_ACTIVE = 2,
    /** The backend/device is unusable; only diagnostics and destruction remain. */
    NKGPU_RENDERER_LOST = 3,
};

/** Resource, upload, and submission counters for one renderer. */
typedef struct nkgpu_renderer_stats {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t frames;
    uint64_t passes;
    uint64_t draw_calls;
    uint64_t buffers_live;
    uint64_t images_live;
    uint64_t samplers_live;
    uint64_t shaders_live;
    uint64_t pipelines_live;
    uint64_t render_targets_live;
    uint64_t buffer_bytes;
    uint64_t image_bytes;
    uint64_t render_target_bytes;
    uint64_t upload_bytes;
    uint64_t resource_creations;
    uint64_t resource_destructions;
    uint64_t surface_recreations;
    uint64_t device_losses;
    uint64_t failed_allocations;
} nkgpu_renderer_stats;

/** Backend selected by the renderer's NativeKit surface. */
typedef uint32_t nkgpu_backend;
enum NK_ENUM(nkgpu_backend) {
    /** Desktop OpenGL core runtime. */
    NKGPU_BACKEND_GLCORE = 1,
    /** OpenGL ES 3 runtime. */
    NKGPU_BACKEND_GLES3 = 2,
    /** Direct3D 11 runtime. */
    NKGPU_BACKEND_D3D11 = 3,
    /** Metal runtime. */
    NKGPU_BACKEND_METAL = 4,
};

/* ------------------------------------------------------------------------- */
/* Graphics and command types                                                */
/* ------------------------------------------------------------------------- */

/** Vertex attribute data format used by nkgpu_pipeline_attribute(). */
typedef uint32_t nkgpu_vertex_format;

enum NK_ENUM(nkgpu_vertex_format) {
    /** A single 32-bit floating-point vertex component. */
    NKGPU_VERTEXFORMAT_FLOAT = 1,
    /** Two 32-bit floating-point vertex components. */
    NKGPU_VERTEXFORMAT_FLOAT2 = 2,
    /** Three 32-bit floating-point vertex components. */
    NKGPU_VERTEXFORMAT_FLOAT3 = 3,
    /** Four 32-bit floating-point vertex components. */
    NKGPU_VERTEXFORMAT_FLOAT4 = 4,
    /** Four normalized unsigned-byte vertex components. */
    NKGPU_VERTEXFORMAT_UBYTE4N = 5,
    NKGPU_VERTEXFORMAT_INT = 6,
    NKGPU_VERTEXFORMAT_INT2 = 7,
    NKGPU_VERTEXFORMAT_INT3 = 8,
    NKGPU_VERTEXFORMAT_INT4 = 9,
    NKGPU_VERTEXFORMAT_UINT = 10,
    NKGPU_VERTEXFORMAT_UINT2 = 11,
    NKGPU_VERTEXFORMAT_UINT3 = 12,
    NKGPU_VERTEXFORMAT_UINT4 = 13,
    NKGPU_VERTEXFORMAT_BYTE4 = 14,
    NKGPU_VERTEXFORMAT_BYTE4N = 15,
    NKGPU_VERTEXFORMAT_UBYTE4 = 16,
    NKGPU_VERTEXFORMAT_SHORT2 = 17,
    NKGPU_VERTEXFORMAT_SHORT2N = 18,
    NKGPU_VERTEXFORMAT_USHORT2 = 19,
    NKGPU_VERTEXFORMAT_USHORT2N = 20,
    NKGPU_VERTEXFORMAT_SHORT4 = 21,
    NKGPU_VERTEXFORMAT_SHORT4N = 22,
    NKGPU_VERTEXFORMAT_USHORT4 = 23,
    NKGPU_VERTEXFORMAT_USHORT4N = 24,
    NKGPU_VERTEXFORMAT_HALF2 = 25,
    NKGPU_VERTEXFORMAT_HALF4 = 26,
};

/** Whether a vertex buffer advances per vertex or per instance. */
typedef uint32_t nkgpu_vertex_step;
enum NK_ENUM(nkgpu_vertex_step) {
    NKGPU_VERTEXSTEP_PER_VERTEX = 1,
    NKGPU_VERTEXSTEP_PER_INSTANCE = 2,
};

/** Primitive topology used by a graphics pipeline. */
typedef uint32_t nkgpu_primitive_type;
enum NK_ENUM(nkgpu_primitive_type) {
    NKGPU_PRIMITIVETYPE_POINTS = 1,
    NKGPU_PRIMITIVETYPE_LINES = 2,
    NKGPU_PRIMITIVETYPE_LINE_STRIP = 3,
    NKGPU_PRIMITIVETYPE_TRIANGLES = 4,
    NKGPU_PRIMITIVETYPE_TRIANGLE_STRIP = 5,
};

/** The intended use of a buffer created by nkgpu_buffer_begin_kind(). */
typedef uint32_t nkgpu_buffer_usage;

enum NK_FLAGS(nkgpu_buffer_usage) {
    /** Use the buffer as vertex data. */
    NKGPU_BUFFER_VERTEX = 1,
    /** Use the buffer as index data. */
    NKGPU_BUFFER_INDEX = 2,
    /** Use the buffer as a compute storage buffer. */
    NKGPU_BUFFER_STORAGE = 1u << 2,
    /** Use the buffer for uniform/constant data. */
    NKGPU_BUFFER_UNIFORM = 1u << 3,
    /** Use the buffer for transfer or staging operations. */
    NKGPU_BUFFER_TRANSFER = 1u << 4,
};

/** Descriptor for a general GPU buffer. */
typedef struct nkgpu_buffer_desc {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint32_t size;
    nkgpu_buffer_usage usage;
    const uint8_t *data;
    uint32_t data_size;
    uint32_t dynamic_update;
    uint32_t stream;
} nkgpu_buffer_desc;

/** How an applied index buffer is interpreted by the pipeline. */
typedef uint32_t nkgpu_index_type;

enum NK_ENUM(nkgpu_index_type) {
    /** Do not use indexed drawing for the pipeline. */
    NKGPU_INDEXTYPE_NONE = 0,
    /** Interpret index elements as unsigned 16-bit values. */
    NKGPU_INDEXTYPE_UINT16 = 1,
    /** Interpret index elements as unsigned 32-bit values. */
    NKGPU_INDEXTYPE_UINT32 = 2,
};

/** Blend factors accepted by generic pipeline state. */
typedef uint32_t nkgpu_blend_factor;
enum NK_ENUM(nkgpu_blend_factor) {
    NKGPU_BLENDFACTOR_ZERO = 1,
    NKGPU_BLENDFACTOR_ONE = 2,
    NKGPU_BLENDFACTOR_SRC_ALPHA = 3,
    NKGPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA = 4,
    NKGPU_BLENDFACTOR_SRC_COLOR = 5,
    NKGPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR = 6,
    NKGPU_BLENDFACTOR_DST_COLOR = 7,
    NKGPU_BLENDFACTOR_ONE_MINUS_DST_COLOR = 8,
    NKGPU_BLENDFACTOR_DST_ALPHA = 9,
    NKGPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA = 10,
    NKGPU_BLENDFACTOR_SRC_ALPHA_SATURATED = 11,
    NKGPU_BLENDFACTOR_BLEND_COLOR = 12,
    NKGPU_BLENDFACTOR_ONE_MINUS_BLEND_COLOR = 13,
    NKGPU_BLENDFACTOR_BLEND_ALPHA = 14,
    NKGPU_BLENDFACTOR_ONE_MINUS_BLEND_ALPHA = 15,
};

/** Blend operations accepted by generic pipeline state. */
typedef uint32_t nkgpu_blend_op;
enum NK_ENUM(nkgpu_blend_op) {
    NKGPU_BLENDOP_ADD = 1,
    NKGPU_BLENDOP_SUBTRACT = 2,
    NKGPU_BLENDOP_REVERSE_SUBTRACT = 3,
    NKGPU_BLENDOP_MIN = 4,
    NKGPU_BLENDOP_MAX = 5,
};

/** Comparison function used by depth and stencil state. */
typedef uint32_t nkgpu_compare_func;
enum NK_ENUM(nkgpu_compare_func) {
    NKGPU_COMPAREFUNC_ALWAYS = 1,
    NKGPU_COMPAREFUNC_LESS_EQUAL = 2,
    NKGPU_COMPAREFUNC_EQUAL = 3,
    NKGPU_COMPAREFUNC_NOT_EQUAL = 4,
    NKGPU_COMPAREFUNC_NEVER = 5,
    NKGPU_COMPAREFUNC_LESS = 6,
    NKGPU_COMPAREFUNC_GREATER = 7,
    NKGPU_COMPAREFUNC_GREATER_EQUAL = 8,
};

/** Stencil operation used when a fragment passes or fails a test. */
typedef uint32_t nkgpu_stencil_op;
enum NK_ENUM(nkgpu_stencil_op) {
    NKGPU_STENCILOP_KEEP = 1,
    NKGPU_STENCILOP_ZERO = 2,
    NKGPU_STENCILOP_INVERT = 3,
    NKGPU_STENCILOP_INCREMENT_WRAP = 4,
    NKGPU_STENCILOP_DECREMENT_WRAP = 5,
};

/** Culling mode for a generic graphics pipeline. */
typedef uint32_t nkgpu_cull_mode;
enum NK_ENUM(nkgpu_cull_mode) {
    NKGPU_CULLMODE_NONE = 1,
    NKGPU_CULLMODE_BACK = 2,
    NKGPU_CULLMODE_FRONT = 3,
};

/** Front-face winding used with back-face culling. */
typedef uint32_t nkgpu_face_winding;
enum NK_ENUM(nkgpu_face_winding) { NKGPU_FACEWINDING_CCW = 1, NKGPU_FACEWINDING_CW = 2 };

/** Color channels written by a pipeline. */
typedef uint32_t nkgpu_color_write_mask;
enum NK_FLAGS(nkgpu_color_write_mask) {
    NKGPU_COLORMASK_NONE = 0,
    NKGPU_COLORMASK_R = 1u << 0,
    NKGPU_COLORMASK_G = 1u << 1,
    NKGPU_COLORMASK_B = 1u << 2,
    NKGPU_COLORMASK_A = 1u << 3,
    NKGPU_COLORMASK_RGBA = NKGPU_COLORMASK_R | NKGPU_COLORMASK_G | NKGPU_COLORMASK_B |
                           NKGPU_COLORMASK_A,
};

/** Blend configuration for one color attachment. */
typedef struct nkgpu_blend_state {
    uint32_t enabled;
    nkgpu_blend_factor src_rgb;
    nkgpu_blend_factor dst_rgb;
    nkgpu_blend_op op_rgb;
    nkgpu_blend_factor src_alpha;
    nkgpu_blend_factor dst_alpha;
    nkgpu_blend_op op_alpha;
} nkgpu_blend_state;

/** Operations applied to one stencil face. */
typedef struct nkgpu_stencil_face_state {
    nkgpu_compare_func compare;
    nkgpu_stencil_op fail_op;
    nkgpu_stencil_op depth_fail_op;
    nkgpu_stencil_op pass_op;
} nkgpu_stencil_face_state;

/** Stencil test configuration for both polygon faces. */
typedef struct nkgpu_stencil_state {
    uint32_t enabled;
    uint8_t read_mask;
    uint8_t write_mask;
    uint8_t reference;
    uint8_t reserved;
    nkgpu_stencil_face_state front;
    nkgpu_stencil_face_state back;
} nkgpu_stencil_state;

/** Pixel storage accepted by nkgpu_image_create_desc(). */
typedef uint32_t nkgpu_image_format;
enum NK_ENUM(nkgpu_image_format) {
    NKGPU_IMAGEFORMAT_R8 = 1,
    NKGPU_IMAGEFORMAT_RGBA8 = 2,
    NKGPU_IMAGEFORMAT_RG8 = 3,
    NKGPU_IMAGEFORMAT_BGRA8 = 4,
    NKGPU_IMAGEFORMAT_R16F = 5,
    NKGPU_IMAGEFORMAT_RG16F = 6,
    NKGPU_IMAGEFORMAT_RGBA16F = 7,
    NKGPU_IMAGEFORMAT_R32F = 8,
    NKGPU_IMAGEFORMAT_RGBA32F = 9,
    NKGPU_IMAGEFORMAT_R32_UINT = 10,
    NKGPU_IMAGEFORMAT_DEPTH16 = 11,
    NKGPU_IMAGEFORMAT_DEPTH24_STENCIL8 = 12,
    NKGPU_IMAGEFORMAT_DEPTH32F = 13,
};

/** Intended uses of an image. Values may be combined. */
typedef uint32_t nkgpu_image_usage;
enum NK_FLAGS(nkgpu_image_usage) {
    NKGPU_IMAGE_SAMPLED = 1u << 0,
    NKGPU_IMAGE_RENDER_TARGET = 1u << 1,
    NKGPU_IMAGE_DEPTH_STENCIL = 1u << 2,
    NKGPU_IMAGE_STORAGE = 1u << 3,
};

/** Descriptor for a general 2D image or image array. */
typedef struct nkgpu_image_desc {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint32_t width;
    uint32_t height;
    nkgpu_image_format format;
    nkgpu_image_usage usage;
    uint32_t mip_count;
    uint32_t sample_count;
    uint32_t layer_count;
    const uint8_t *data;
    uint32_t data_size;
    uint32_t row_pitch;
    uint32_t dynamic_update;
} nkgpu_image_desc;

/** Describes a buffer-to-buffer transfer. */
typedef struct nkgpu_buffer_copy_desc {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkgpu_buffer source;
    uint32_t source_offset;
    nkgpu_buffer destination;
    uint32_t destination_offset;
    uint32_t size;
} nkgpu_buffer_copy_desc;

/** Describes a 2D image-region transfer. Coordinates use NativeKit's top-left origin. */
typedef struct nkgpu_image_copy_desc {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkgpu_image source;
    uint32_t source_mip;
    uint32_t source_layer;
    uint32_t source_x;
    uint32_t source_y;
    nkgpu_image destination;
    uint32_t destination_mip;
    uint32_t destination_layer;
    uint32_t destination_x;
    uint32_t destination_y;
    uint32_t width;
    uint32_t height;
} nkgpu_image_copy_desc;

/** Describes a buffer-to-image transfer. Coordinates use NativeKit's top-left origin. */
typedef struct nkgpu_buffer_image_copy_desc {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkgpu_buffer buffer;
    uint32_t buffer_offset;
    uint32_t row_pitch;
    nkgpu_image image;
    uint32_t mip_level;
    uint32_t layer;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} nkgpu_buffer_image_copy_desc;

/** Describes one asynchronous image readback request. */
typedef struct nkgpu_image_readback_desc {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkgpu_image image;
    uint32_t mip_level;
    uint32_t layer;
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} nkgpu_image_readback_desc;

/** Reports readback state and the tightly packed result layout. */
typedef struct nkgpu_readback_info {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkgpu_readback_state state;
    uint32_t size;
    uint32_t row_pitch;
    uint32_t width;
    uint32_t height;
} nkgpu_readback_info;

/** An RGBA clear color used by render-pass actions. */
typedef struct nkgpu_color {
    float r;
    float g;
    float b;
    float a;
} nkgpu_color;

typedef uint32_t nkgpu_load_action;
enum NK_ENUM(nkgpu_load_action) {
    NKGPU_LOADACTION_LOAD = 1,
    NKGPU_LOADACTION_CLEAR = 2,
    NKGPU_LOADACTION_DISCARD = 3,
};

typedef uint32_t nkgpu_store_action;
enum NK_ENUM(nkgpu_store_action) {
    NKGPU_STOREACTION_STORE = 1,
    NKGPU_STOREACTION_DISCARD = 2,
};

/** Load/store behavior for one render-pass attachment. */
typedef struct nkgpu_attachment_action {
    nkgpu_load_action load_action;
    nkgpu_store_action store_action;
    nkgpu_color clear_color;
    float clear_depth;
    uint32_t clear_stencil;
} nkgpu_attachment_action;

/** One color attachment and its optional multisample resolve image. */
typedef struct nkgpu_color_attachment {
    nkgpu_image image;
    nkgpu_image resolve_image;
    nkgpu_attachment_action action;
} nkgpu_color_attachment;

/** General render-pass description. */
typedef struct nkgpu_render_pass_desc {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint32_t color_count;
    nkgpu_color_attachment colors[NKGPU_MAX_COLOR_ATTACHMENTS];
    nkgpu_image depth_stencil;
    nkgpu_attachment_action depth_stencil_action;
} nkgpu_render_pass_desc;

/** Shader stage associated with a uniform block or texture binding. */
typedef uint32_t nkgpu_shader_stage;

enum NK_ENUM(nkgpu_shader_stage) {
    /** The vertex shader stage. */
    NKGPU_SHADERSTAGE_VERTEX = 1,
    /** The fragment shader stage. */
    NKGPU_SHADERSTAGE_FRAGMENT = 2,
    /** The compute shader stage. */
    NKGPU_SHADERSTAGE_COMPUTE = 3,
};

/** Source language accepted by the shader creation functions. */
typedef uint32_t nkgpu_shader_language;

enum NK_ENUM(nkgpu_shader_language) {
    /** GLSL source for OpenGL and OpenGL ES backends. */
    NKGPU_SHADERLANGUAGE_GLSL = 1,
    /** HLSL shader-model-5 source for the Direct3D 11 backend. */
    NKGPU_SHADERLANGUAGE_HLSL5 = 2,
    /** Metal Shading Language source for the Metal backend. */
    NKGPU_SHADERLANGUAGE_MSL = 3,
};

/** Data type used to describe a shader uniform member. */
typedef uint32_t nkgpu_uniform_type;

enum NK_ENUM(nkgpu_uniform_type) {
    /** One floating-point value. */
    NKGPU_UNIFORMTYPE_FLOAT = 1,
    /** Two floating-point values. */
    NKGPU_UNIFORMTYPE_FLOAT2 = 2,
    /** Three floating-point values. */
    NKGPU_UNIFORMTYPE_FLOAT3 = 3,
    /** Four floating-point values. */
    NKGPU_UNIFORMTYPE_FLOAT4 = 4,
    /** One integer value. */
    NKGPU_UNIFORMTYPE_INT = 5,
    /** Two integer values. */
    NKGPU_UNIFORMTYPE_INT2 = 6,
    /** Three integer values. */
    NKGPU_UNIFORMTYPE_INT3 = 7,
    /** Four integer values. */
    NKGPU_UNIFORMTYPE_INT4 = 8,
    /** A 4-by-4 matrix of floating-point values. */
    NKGPU_UNIFORMTYPE_MAT4 = 9,
};

/** Texture minification or magnification filter. */
typedef uint32_t nkgpu_filter;

enum NK_ENUM(nkgpu_filter) {
    /** Choose the nearest texel when sampling. */
    NKGPU_FILTER_NEAREST = 1,
    /** Interpolate neighboring texels when sampling. */
    NKGPU_FILTER_LINEAR = 2,
};

/** Texture-coordinate behavior outside the [0, 1] range. */
typedef uint32_t nkgpu_wrap;

enum NK_ENUM(nkgpu_wrap) {
    /** Repeat the texture at integer coordinate boundaries. */
    NKGPU_WRAP_REPEAT = 1,
    /** Clamp texture coordinates to the edge texels. */
    NKGPU_WRAP_CLAMP_TO_EDGE = 2,
};

/** Opcode stored in the first word of a packed command record. */
typedef uint32_t nkgpu_command;

enum NK_ENUM(nkgpu_command) {
    /** Apply a pipeline; payload: one nkgpu_pipeline handle. */
    NKGPU_COMMAND_APPLY_PIPELINE = 1,
    /** Apply a vertex buffer; payload: slot, buffer handle, and byte offset. */
    NKGPU_COMMAND_APPLY_VERTEX_BUFFER = 2,
    /** Apply an index buffer; payload: buffer handle and byte offset. */
    NKGPU_COMMAND_APPLY_INDEX_BUFFER = 3,
    /** Apply an image; payload: view slot and image handle. */
    NKGPU_COMMAND_APPLY_IMAGE = 4,
    /** Apply a sampler; payload: sampler slot and sampler handle. */
    NKGPU_COMMAND_APPLY_SAMPLER = 5,
    /** Apply uniform bytes; payload: block slot, byte count, and uniform bytes. */
    NKGPU_COMMAND_APPLY_UNIFORMS = 6,
    /** Draw primitives; payload: base element, element count, and instance count. */
    NKGPU_COMMAND_DRAW = 7,
    /** Apply a framebuffer scissor; payload: enabled flag, x, y, width, and height. */
    NKGPU_COMMAND_APPLY_SCISSOR = 8,
    /** Apply an external graphics image; payload: view slot and image handle. */
    NKGPU_COMMAND_APPLY_GRAPHICS_IMAGE = 9,
    /** Apply a viewport; payload: x, y, width, and height. */
    NKGPU_COMMAND_APPLY_VIEWPORT = 10,
    /** Apply a storage buffer; payload: view slot and buffer handle. */
    NKGPU_COMMAND_APPLY_STORAGE_BUFFER = 11,
    /** Apply a storage image; payload: view slot and image handle. */
    NKGPU_COMMAND_APPLY_STORAGE_IMAGE = 12,
    /** Dispatch compute workgroups; payload: x, y, and z group counts. */
    NKGPU_COMMAND_DISPATCH = 13,
    /** Copy a byte range between buffers; payload: source, source offset, destination, destination offset, size. */
    NKGPU_COMMAND_COPY_BUFFER = 14,
    /** Copy a 2D image region; payload matches nkgpu_image_copy_desc without struct_size. */
    NKGPU_COMMAND_COPY_IMAGE = 15,
    /** Upload a buffer region into an image; payload matches nkgpu_buffer_image_copy_desc without struct_size. */
    NKGPU_COMMAND_COPY_BUFFER_TO_IMAGE = 16,
    /** Download an image region into a buffer; payload matches nkgpu_buffer_image_copy_desc without struct_size. */
    NKGPU_COMMAND_COPY_IMAGE_TO_BUFFER = 17,
};

/** Current version for the explicitly versioned command-stream envelope. */
enum { NKGPU_COMMAND_STREAM_VERSION_1 = 1 };

/** Versioned wrapper for packed command records. */
typedef struct nkgpu_command_stream_desc {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint32_t version;
    const uint8_t *commands;
    uint32_t size;
} nkgpu_command_stream_desc;

/* ------------------------------------------------------------------------- */
/* Diagnostics                                                               */
/* ------------------------------------------------------------------------- */

/**
 * Returns the most recent adapter diagnostic.
 *
 * The returned UTF-8 string is borrowed and is overwritten by a later failed
 * call. It remains owned by the adapter; do not free it. Call this immediately
 * after any operation that does not return NKGPU_OK.
 */
NKGPU_API const char *nkgpu_last_error(void) NKGPU_RETURNS_BORROWED_UTF8;

/** Returns the build's default graphics API for a GPU-backed surface. */
NKGPU_API nk_graphics_api nkgpu_default_graphics_api(void);

/** Returns the backend selected for this renderer's NativeKit surface. */
NKGPU_API nkgpu_backend nkgpu_query_backend(nkgpu_renderer renderer);

/** Returns the NativeKit graphics API selected for this renderer's surface. */
NKGPU_API nk_graphics_api nkgpu_query_graphics_api(nkgpu_renderer renderer);

/** Returns the explicit lifecycle state of a renderer. */
NKGPU_API nkgpu_result nkgpu_renderer_get_state(nkgpu_renderer renderer,
                                                nkgpu_renderer_state *out_state NKGPU_OUT);

/** Returns resource and submission counters for a renderer. */
NKGPU_API nkgpu_result nkgpu_renderer_get_stats(nkgpu_renderer renderer,
                                                nkgpu_renderer_stats *out_stats NKGPU_OUT);

/** Reports optional backend features through a backend-agnostic envelope. */
typedef struct nkgpu_features {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint32_t mrt_count;
    uint32_t max_samples;
    uint32_t storage_buffer;
    uint32_t storage_image;
    uint32_t compute;
    uint32_t instancing;
    uint32_t buffer_copy;
    uint32_t image_copy;
    uint32_t image_readback;
} nkgpu_features;

/** Reports portable resource and binding limits for one renderer. */
typedef struct nkgpu_limits {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint32_t max_texture_size;
    uint32_t max_array_layers;
    uint32_t max_vertex_attributes;
    uint32_t max_color_attachments;
    uint32_t max_texture_bindings;
    uint32_t max_storage_buffer_bindings;
    uint32_t max_storage_image_bindings;
} nkgpu_limits;

/** Opaque backend tokens for advanced native integration. */
typedef struct nkgpu_native_context {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkgpu_backend backend;
    uint64_t device;
    uint64_t context;
} nkgpu_native_context;

NKGPU_API nkgpu_result nkgpu_query_features(nkgpu_renderer renderer,
                                            nkgpu_features *out_features NKGPU_OUT);
NKGPU_API nkgpu_result nkgpu_query_limits(nkgpu_renderer renderer,
                                          nkgpu_limits *out_limits NKGPU_OUT);

/** Returns opaque native device/context tokens for advanced backend integration. */
NKGPU_API nkgpu_result nkgpu_get_native_context(nkgpu_renderer renderer,
                                                nkgpu_native_context *out_context NKGPU_OUT);

/* ------------------------------------------------------------------------- */
/* Surface and renderer lifecycle                                            */
/* ------------------------------------------------------------------------- */

/**
 * Creates a surface for the selected graphics backend associated with a
 * NativeKit window.
 *
 * `nativekit_window` must be a valid NativeKit window handle, and `width` and
 * `height` must be positive. The current adapter requests the configured
 * graphics surface. On NKGPU_OK, writes the NativeKit surface handle to
 * `out_surface`; destroy it with nkgpu_surface_destroy() after destroying its
 * renderer. This convenience call requests the build's default API.
 */
NKGPU_API nkgpu_result nkgpu_surface_create(nk_window nativekit_window, int32_t width,
                                            int32_t height, nk_surface *out_surface NKGPU_OUT);

/**
 * Creates a child surface with an explicit graphics API. The matching runtime
 * must be included in this build; with the backend matrix enabled, GLCore and
 * GLES3 surfaces can coexist.
 */
NKGPU_API nkgpu_result nkgpu_surface_create_for_api(nk_window nativekit_window, nk_graphics_api api,
                                                    int32_t width, int32_t height,
                                                    nk_surface *out_surface NKGPU_OUT);

/**
 * Requests a new size for a GPU surface.
 *
 * `width` and `height` are the surface's logical dimensions and must be
 * positive. The framebuffer may have a different pixel size; begin each frame
 * with nkgpu_begin_frame() so the adapter can query the current framebuffer.
 */
NKGPU_API nkgpu_result nkgpu_surface_resize(nk_surface surface, int32_t width, int32_t height);

/**
 * Destroys a GPU surface.
 *
 * Destroy the renderer first. The surface handle becomes invalid after this
 * call and must not be reused.
 */
NKGPU_API nkgpu_result nkgpu_surface_destroy(nk_surface surface);

/**
 * Creates the GPU renderer for a ready NativeKit surface.
 *
 * Multiple renderer handles may coexist for compatible graphics devices.
 * Calls are serialized on the graphics thread, and only one renderer may have
 * an active frame at a time. On NKGPU_OK, writes a renderer handle to
 * `out_renderer`; all buffers, shaders, pipelines, images, samplers, and
 * builders created through it belong to that renderer.
 */
NKGPU_API nkgpu_result nkgpu_renderer_create(nk_surface nativekit_surface,
                                             nkgpu_renderer *out_renderer NKGPU_OUT);

/**
 * Destroys a renderer and all resources still owned by it.
 *
 * The renderer must not have an active frame. This also releases unfinished
 * builders, so retain a resource handle only while its renderer is alive.
 */
NKGPU_API nkgpu_result nkgpu_renderer_destroy(nkgpu_renderer renderer);

/**
 * Begins a general offscreen render pass inside an active frame.
 *
 * The attachment images must have been created with the corresponding image
 * usage flags. Up to NKGPU_MAX_COLOR_ATTACHMENTS color images are supported;
 * each may name a single-sample resolve image for MSAA.
 */
NKGPU_API nkgpu_result nkgpu_begin_render_pass(nkgpu_renderer renderer,
                                               const nkgpu_render_pass_desc *desc);

/** Creates a sampled RGBA8 offscreen target, optionally with depth/stencil storage. */
NKGPU_API NKGPU_DEPRECATED("use nkgpu_image_create_desc and nkgpu_begin_render_pass")
nkgpu_result nkgpu_render_target_create(nkgpu_renderer renderer, uint32_t width,
                                        uint32_t height, uint32_t depth_stencil,
                                        nkgpu_render_target *out_target NKGPU_OUT);

/** Returns a borrowed generic image handle for the target's sampled color attachment. */
NKGPU_API NKGPU_DEPRECATED("use the image handle returned by nkgpu_image_create_desc")
nkgpu_result nkgpu_render_target_get_image(nkgpu_renderer renderer, nkgpu_render_target target,
                                           nk_graphics_image *out_image NKGPU_OUT);

/** Destroys a render target; imported image references remain valid until released. */
NKGPU_API NKGPU_DEPRECATED("use nkgpu_image_destroy")
nkgpu_result nkgpu_render_target_destroy(nkgpu_renderer renderer, nkgpu_render_target target);

/**
 * Begins drawing to an offscreen target without presenting the window surface.
 * `clear` must be zero or one; one clears the color attachment. The renderer
 * must be idle. Pair with nkgpu_end_render_target() before any other pass.
 */
NKGPU_API NKGPU_DEPRECATED("use nkgpu_begin_render_pass")
nkgpu_result nkgpu_begin_render_target(nkgpu_renderer renderer, nkgpu_render_target target,
                                       uint32_t clear);

/** Ends and commits the active offscreen target pass without presenting. */
NKGPU_API NKGPU_DEPRECATED("use nkgpu_end_pass")
nkgpu_result nkgpu_end_render_target(nkgpu_renderer renderer);

/* ------------------------------------------------------------------------- */
/* Buffer APIs                                                               */
/* ------------------------------------------------------------------------- */

/**
 * Creates a GPU buffer from an existing byte array.
 *
 * The adapter copies `size` bytes from `data`. Both must be non-null and
 * non-zero. Use nkgpu_buffer_begin_kind() when the buffer must be explicitly
 * marked as a vertex or index buffer. On NKGPU_OK, writes the new handle to
 * `out_buffer`.
 */
NKGPU_API nkgpu_result nkgpu_buffer_create(nkgpu_renderer renderer, const uint8_t *data,
                                           uint32_t size, nkgpu_buffer *out_buffer NKGPU_OUT);

/** Creates a buffer from a general descriptor. */
NKGPU_API nkgpu_result nkgpu_buffer_create_desc(nkgpu_renderer renderer,
                                                const nkgpu_buffer_desc *desc,
                                                nkgpu_buffer *out_buffer NKGPU_OUT);

/**
 * Starts building a zero-initialized vertex buffer of `size` bytes.
 *
 * This is shorthand for nkgpu_buffer_begin_kind() with NKGPU_BUFFER_VERTEX. Fill
 * the builder with nkgpu_buffer_write_f32() or nkgpu_buffer_write_u16(), then call
 * nkgpu_buffer_end(), which consumes the builder and creates the GPU buffer.
 */
NKGPU_API nkgpu_result nkgpu_buffer_begin(nkgpu_renderer renderer, uint32_t size,
                                          nkgpu_buffer_builder *out_builder NKGPU_OUT);

/**
 * Starts building a zero-initialized vertex or index buffer.
 *
 * `usage` must be NKGPU_BUFFER_VERTEX or NKGPU_BUFFER_INDEX. The returned builder
 * owns a temporary `size`-byte array until nkgpu_buffer_end() or renderer
 * destruction. On NKGPU_OK, writes the builder handle to `out_builder`.
 */
NKGPU_API nkgpu_result nkgpu_buffer_begin_kind(nkgpu_renderer renderer, uint32_t size,
                                               nkgpu_buffer_usage usage,
                                               nkgpu_buffer_builder *out_builder NKGPU_OUT);

/**
 * Writes one 32-bit floating-point value at a byte offset in a buffer builder.
 *
 * The four-byte write must fit within the builder's allocated range. The
 * offset is relative to the beginning of the buffer and is not adjusted for
 * vertex layout or alignment.
 */
NKGPU_API nkgpu_result nkgpu_buffer_write_f32(nkgpu_buffer_builder builder, uint32_t offset,
                                              float value);

/**
 * Writes one unsigned 16-bit value at a byte offset in a buffer builder.
 *
 * `value` must be at most 65535, and the two-byte write must fit within the
 * builder's allocated range. The value is passed as uint32_t so bindings can
 * use one common integer type for pixel and buffer writes.
 */
NKGPU_API nkgpu_result nkgpu_buffer_write_u16(nkgpu_buffer_builder builder, uint32_t offset,
                                              uint32_t value);

/**
 * Uploads a completed buffer and consumes its builder.
 *
 * On NKGPU_OK, writes the GPU buffer handle to `out_buffer`. The builder is no
 * longer valid after this call, including when GPU creation reports an error.
 */
NKGPU_API nkgpu_result nkgpu_buffer_end(nkgpu_buffer_builder builder,
                                        nkgpu_buffer *out_buffer NKGPU_OUT);

/** Destroys a buffer owned by `renderer`; the handle becomes invalid. */
NKGPU_API nkgpu_result nkgpu_buffer_destroy(nkgpu_renderer renderer, nkgpu_buffer buffer);

/** Updates an arbitrary byte range of a dynamic buffer. */
NKGPU_API nkgpu_result nkgpu_buffer_update(nkgpu_renderer renderer, nkgpu_buffer buffer,
                                           uint32_t offset, const uint8_t *data, uint32_t size);

/* ------------------------------------------------------------------------- */
/* Shader APIs                                                               */
/* ------------------------------------------------------------------------- */

/**
 * Creates a shader from NUL-terminated vertex and fragment shader source.
 *
 * `language` must match the renderer backend: GLSL for OpenGL, HLSL5 for D3D11,
 * or MSL for Metal. HLSL5 and MSL source stages must each expose a `main`
 * entry point. Both stages are required. Shader builders provide the backend
 * binding metadata needed for uniforms, textures, and vertex inputs.
 * On NKGPU_OK, writes the shader handle to `out_shader`.
 */
NKGPU_API nkgpu_result nkgpu_shader_create(nkgpu_renderer renderer, nkgpu_shader_language language,
                                           const char *vertex_source NKGPU_UTF8,
                                           const char *fragment_source NKGPU_UTF8,
                                           nkgpu_shader *out_shader NKGPU_OUT);

/** Destroys a shader owned by `renderer`; the handle becomes invalid. */
NKGPU_API nkgpu_result nkgpu_shader_destroy(nkgpu_renderer renderer, nkgpu_shader shader);

/**
 * Starts building a shader with UTF-8 vertex and fragment source strings.
 *
 * `language` must match the renderer backend: GLSL for OpenGL, HLSL5 for D3D11,
 * or MSL for Metal. HLSL5 and MSL source stages must each expose a `main`
 * entry point. Unlike nkgpu_shader_create(), this lets
 * you describe uniform blocks, members, and texture bindings before
 * nkgpu_shader_end() creates the shader. The source strings are copied, so
 * they may be released after this call succeeds.
 */
NKGPU_API nkgpu_result nkgpu_shader_begin(nkgpu_renderer renderer, nkgpu_shader_language language,
                                          const char *vertex_source NKGPU_UTF8,
                                          const char *fragment_source NKGPU_UTF8,
                                          nkgpu_shader_builder *out_builder NKGPU_OUT);

/** Starts building a compute-only shader with one UTF-8 source string. */
NKGPU_API nkgpu_result nkgpu_shader_begin_compute(nkgpu_renderer renderer,
                                                  nkgpu_shader_language language,
                                                  const char *compute_source NKGPU_UTF8,
                                                  nkgpu_shader_builder *out_builder NKGPU_OUT);

/**
 * Describes a vertex input for a shader builder.
 *
 * `location` matches the vertex attribute index used by the pipeline. The GLSL
 * name is used by GL backends; the HLSL semantic and index are used by D3D11.
 * Metal source declares its own `[[attribute(location)]]` mapping.
 */
NKGPU_API nkgpu_result nkgpu_shader_attribute(nkgpu_shader_builder builder, uint32_t location,
                                              const char *glsl_name NKGPU_UTF8,
                                              const char *hlsl_semantic NKGPU_UTF8,
                                              uint32_t hlsl_semantic_index);

/**
 * Describes one shader uniform block in a shader builder.
 *
 * `slot` is the block binding slot, `stage` selects the vertex, fragment, or
 * compute shader, and `size` is the block's byte size. The uniform data later supplied
 * to nkgpu_apply_uniforms() must use the same layout and size.
 */
NKGPU_API nkgpu_result nkgpu_shader_uniform_block(nkgpu_shader_builder builder, uint32_t slot,
                                                  nkgpu_shader_stage stage, uint32_t size);

/**
 * Describes one member of a shader uniform block.
 *
 * `name` is the UTF-8 GLSL uniform member name. HLSL5 and MSL use the block
 * size and caller-provided bytes directly. `array_count` describes an array
 * and zero is treated as one element. Cross-backend blocks use std140 layout.
 */
NKGPU_API nkgpu_result nkgpu_shader_uniform(nkgpu_shader_builder builder, uint32_t block_slot,
                                            uint32_t member_index, const char *name NKGPU_UTF8,
                                            nkgpu_uniform_type type, uint32_t array_count);

/**
 * Describes a 2D filtering texture binding in a shader builder.
 *
 * `view_slot` and `sampler_slot` are the slots used later by nkgpu_apply_image()
 * and nkgpu_apply_sampler(). `name` is the UTF-8 GLSL combined texture/sampler
 * name; HLSL5 and MSL use the view and sampler slots directly. `stage` selects
 * the vertex or fragment shader.
 */
NKGPU_API nkgpu_result nkgpu_shader_texture(nkgpu_shader_builder builder, uint32_t view_slot,
                                            uint32_t sampler_slot, nkgpu_shader_stage stage,
                                            const char *name NKGPU_UTF8);

/** Describes a storage-buffer binding with explicit read-only metadata. */
NKGPU_API nkgpu_result nkgpu_shader_storage_buffer(nkgpu_shader_builder builder, uint32_t view_slot,
                                                   nkgpu_shader_stage stage, uint32_t readonly);

/** Describes a compute storage-image binding and its access format. */
NKGPU_API nkgpu_result nkgpu_shader_storage_image(nkgpu_shader_builder builder, uint32_t view_slot,
                                                  nkgpu_image_format format, uint32_t writeonly);

/**
 * Creates a shader from a shader builder and consumes the builder.
 *
 * On NKGPU_OK, writes the shader handle to `out_shader`. The builder is no
 * longer valid after this call, including when shader creation fails.
 */
NKGPU_API nkgpu_result nkgpu_shader_end(nkgpu_shader_builder builder,
                                        nkgpu_shader *out_shader NKGPU_OUT);

/* ------------------------------------------------------------------------- */
/* Pipeline APIs                                                             */
/* ------------------------------------------------------------------------- */

/**
 * Starts building a pipeline for a shader.
 *
 * `stride` is the byte distance between vertices in vertex buffer slot zero.
 * The shader must belong to `renderer`. Add vertex attributes and, when
 * needed, an index type before calling nkgpu_pipeline_end().
 */
NKGPU_API nkgpu_result nkgpu_pipeline_begin(nkgpu_renderer renderer, nkgpu_shader shader,
                                            uint32_t stride,
                                            nkgpu_pipeline_builder *out_builder NKGPU_OUT);

/** Starts building a compute pipeline for a compute shader. */
NKGPU_API nkgpu_result nkgpu_pipeline_begin_compute(nkgpu_renderer renderer, nkgpu_shader shader,
                                                    nkgpu_pipeline_builder *out_builder NKGPU_OUT);

/** Converts a pipeline builder into a compute pipeline descriptor. */
NKGPU_API nkgpu_result nkgpu_pipeline_compute(nkgpu_pipeline_builder builder);

/**
 * Configures one vertex attribute in a pipeline builder.
 *
 * `location` is the shader attribute location, `buffer_index` selects the
 * vertex buffer slot, and `offset` is the attribute's byte offset within one
 * vertex. Call this once for each attribute consumed by the shader.
 */
NKGPU_API nkgpu_result nkgpu_pipeline_attribute(nkgpu_pipeline_builder builder, uint32_t location,
                                                uint32_t buffer_index, uint32_t offset,
                                                nkgpu_vertex_format format);

/** Configures the stride and step rate of a vertex-buffer slot. */
NKGPU_API nkgpu_result nkgpu_pipeline_vertex_buffer(nkgpu_pipeline_builder builder,
                                                    uint32_t buffer_index, uint32_t stride,
                                                    nkgpu_vertex_step step, uint32_t step_rate);

/** Selects the primitive topology of a graphics pipeline. */
NKGPU_API nkgpu_result nkgpu_pipeline_primitive_type(nkgpu_pipeline_builder builder,
                                                     nkgpu_primitive_type primitive_type);

/**
 * Enables depth testing and writing for a pipeline. Pipelines target the window
 * pass depth/stencil format by default, with depth testing and writing disabled.
 * Call with `enabled = 0` when drawing to an offscreen target without depth storage.
 */
NKGPU_API nkgpu_result nkgpu_pipeline_depth_stencil(nkgpu_pipeline_builder builder,
                                                    uint32_t enabled);

/** Sets explicit depth compare, write, and polygon-offset state. */
typedef struct nkgpu_depth_state {
    uint32_t enabled;
    nkgpu_compare_func compare;
    uint32_t write_enabled;
    float bias;
    float bias_slope_scale;
    float bias_clamp;
} nkgpu_depth_state;

NKGPU_API nkgpu_result nkgpu_pipeline_depth(nkgpu_pipeline_builder builder,
                                            const nkgpu_depth_state *state);

/** Sets independent RGB and alpha blend operations for a pipeline. */
NKGPU_API nkgpu_result nkgpu_pipeline_blend(nkgpu_pipeline_builder builder,
                                            const nkgpu_blend_state *state);

/** Sets per-face stencil testing and operations for a pipeline. */
NKGPU_API nkgpu_result nkgpu_pipeline_stencil(nkgpu_pipeline_builder builder,
                                              const nkgpu_stencil_state *state);

/** Selects the front-face culling mode and winding for a pipeline. */
NKGPU_API nkgpu_result nkgpu_pipeline_cull_mode(nkgpu_pipeline_builder builder,
                                                nkgpu_cull_mode mode, nkgpu_face_winding winding);

/** Selects the color channels written by a pipeline. */
NKGPU_API nkgpu_result nkgpu_pipeline_color_write_mask(nkgpu_pipeline_builder builder,
                                                       nkgpu_color_write_mask mask);

/** Sets the color format and write state for one MRT pipeline slot. */
NKGPU_API nkgpu_result nkgpu_pipeline_color_target(nkgpu_pipeline_builder builder,
                                                   uint32_t color_index,
                                                   nkgpu_image_format format,
                                                   nkgpu_color_write_mask write_mask,
                                                   const nkgpu_blend_state *blend);

/** Sets the multisample count and optional alpha-to-coverage state. */
NKGPU_API nkgpu_result nkgpu_pipeline_multisample(nkgpu_pipeline_builder builder,
                                                  uint32_t sample_count,
                                                  uint32_t alpha_to_coverage);

/**
 * Selects how the pipeline interprets an applied index buffer.
 *
 * Use NKGPU_INDEXTYPE_NONE for non-indexed drawing, or select the element width
 * used by the buffer passed to nkgpu_apply_index_buffer().
 */
NKGPU_API nkgpu_result nkgpu_pipeline_index_type(nkgpu_pipeline_builder builder,
                                                 nkgpu_index_type type);

/**
 * Creates a pipeline from a pipeline builder and consumes the builder.
 *
 * On NKGPU_OK, writes the pipeline handle to `out_pipeline`. The builder is no
 * longer valid after this call, including when pipeline creation fails.
 */
NKGPU_API nkgpu_result nkgpu_pipeline_end(nkgpu_pipeline_builder builder,
                                          nkgpu_pipeline *out_pipeline NKGPU_OUT);

/** Destroys a pipeline owned by `renderer`; the handle becomes invalid. */
NKGPU_API nkgpu_result nkgpu_pipeline_destroy(nkgpu_renderer renderer, nkgpu_pipeline pipeline);

/** Creates an uninitialized stream buffer for appends during an active pass. */
NKGPU_API nkgpu_result nkgpu_buffer_create_stream(nkgpu_renderer renderer, uint32_t capacity,
                                                  nkgpu_buffer_usage usage,
                                                  nkgpu_buffer *out_buffer NKGPU_OUT);

/** Appends bytes to a stream buffer and returns their byte offset. */
NKGPU_API nkgpu_result nkgpu_buffer_append(nkgpu_renderer renderer, nkgpu_buffer buffer,
                                           const uint8_t *data, uint32_t size,
                                           uint32_t *out_offset NKGPU_OUT);

/* ------------------------------------------------------------------------- */
/* Frame and resource APIs                                                   */
/* ------------------------------------------------------------------------- */

/**
 * Begins a frame for a renderer.
 *
 * This makes the renderer's surface current, refreshes its framebuffer size,
 * clears the frame, and resets resource bindings. Only one renderer may have
 * an active frame. Pair every successful call with nkgpu_end_frame(), and use
 * the apply and draw functions only between those calls.
 */
NKGPU_API nkgpu_result nkgpu_begin_frame(nkgpu_renderer renderer);

/** Begins a frame without opening a pass, for plans that contain several passes. */
NKGPU_API nkgpu_result nkgpu_frame_begin(nkgpu_renderer renderer);

/** Begins a compute pass inside an active frame. */
NKGPU_API nkgpu_result nkgpu_begin_compute_pass(nkgpu_renderer renderer);

/** Begins a transfer pass inside an active frame. */
NKGPU_API nkgpu_result nkgpu_begin_copy_pass(nkgpu_renderer renderer);

/** Begins a window-surface pass inside a frame. */
NKGPU_API nkgpu_result nkgpu_begin_window_pass(nkgpu_renderer renderer, uint32_t width,
                                               uint32_t height, uint32_t clear);

/** Begins an offscreen target pass inside a frame. */
NKGPU_API NKGPU_DEPRECATED("use nkgpu_begin_render_pass")
nkgpu_result nkgpu_begin_target_pass(nkgpu_renderer renderer, nkgpu_render_target target,
                                     uint32_t clear);

/** Ends the active pass while keeping the frame open. */
NKGPU_API nkgpu_result nkgpu_end_pass(nkgpu_renderer renderer);

/** Applies a framebuffer-pixel scissor rectangle, or disables scissoring. */
NKGPU_API nkgpu_result nkgpu_apply_scissor(nkgpu_renderer renderer, uint32_t enabled, int32_t x,
                                           int32_t y, int32_t width, int32_t height);

/** Applies a framebuffer-pixel viewport to the active render pass. */
NKGPU_API nkgpu_result nkgpu_apply_viewport(nkgpu_renderer renderer, int32_t x, int32_t y,
                                            int32_t width, int32_t height);

/** Applies a pipeline to the currently active frame. */
NKGPU_API nkgpu_result nkgpu_apply_pipeline(nkgpu_renderer renderer, nkgpu_pipeline pipeline);

/**
 * Binds a vertex buffer to a slot in the currently active frame.
 *
 * `offset` is a byte offset into the buffer. The slot must match the buffer
 * index configured with nkgpu_pipeline_attribute().
 */
NKGPU_API nkgpu_result nkgpu_apply_vertex_buffer(nkgpu_renderer renderer, uint32_t slot,
                                                 nkgpu_buffer buffer, uint32_t offset);

/** Binds an index buffer and byte offset to the currently active frame. */
NKGPU_API nkgpu_result nkgpu_apply_index_buffer(nkgpu_renderer renderer, nkgpu_buffer buffer,
                                                uint32_t offset);

/**
 * Starts a zero-initialized uniform byte block of `size` bytes.
 *
 * Fill it with nkgpu_uniforms_write_f32(), then pass it to
 * nkgpu_apply_uniforms(), which consumes the builder. The bytes must match the
 * uniform block layout declared for the shader.
 */
NKGPU_API nkgpu_result nkgpu_uniforms_begin(nkgpu_renderer renderer, uint32_t size,
                                            nkgpu_uniform_builder *out_builder NKGPU_OUT);

/** Writes one 32-bit floating-point uniform value at a byte offset. */
NKGPU_API nkgpu_result nkgpu_uniforms_write_f32(nkgpu_uniform_builder builder, uint32_t offset,
                                                float value);

/** Applies caller-owned uniform bytes to a block in the active pass. */
NKGPU_API nkgpu_result nkgpu_apply_uniform_data(nkgpu_renderer renderer, uint32_t slot,
                                                const uint8_t *data, uint32_t size);

/**
 * Applies a uniform builder to a block slot in the active frame.
 *
 * `slot` must be the block slot described with nkgpu_shader_uniform_block(). On
 * success, the builder's bytes have been submitted and the builder is
 * consumed; do not use it again.
 */
NKGPU_API nkgpu_result nkgpu_apply_uniforms(nkgpu_renderer renderer, uint32_t slot,
                                            nkgpu_uniform_builder builder);

/** Starts building a zero-initialized RGBA8 image of the requested size. */
NKGPU_API nkgpu_result nkgpu_image_begin(nkgpu_renderer renderer, uint32_t width, uint32_t height,
                                         nkgpu_image_builder *out_builder NKGPU_OUT);

/**
 * Writes one RGBA8 pixel into an image builder.
 *
 * `x` and `y` are zero-based pixel coordinates. Each color component must be
 * in the inclusive range 0..255.
 */
NKGPU_API nkgpu_result nkgpu_image_write_rgba8(nkgpu_image_builder builder, uint32_t x, uint32_t y,
                                               uint32_t red, uint32_t green, uint32_t blue,
                                               uint32_t alpha);

/**
 * Uploads an image and creates its texture view, consuming the builder.
 *
 * On NKGPU_OK, writes the image handle to `out_image`. The image can then be
 * bound with nkgpu_apply_image().
 */
NKGPU_API nkgpu_result nkgpu_image_end(nkgpu_image_builder builder,
                                       nkgpu_image *out_image NKGPU_OUT);

/** Creates an image from a general descriptor. */
NKGPU_API nkgpu_result nkgpu_image_create_desc(nkgpu_renderer renderer,
                                               const nkgpu_image_desc *desc,
                                               nkgpu_image *out_image NKGPU_OUT);

/** Creates an image from tightly packed pixels as a convenience wrapper. */
NKGPU_API nkgpu_result nkgpu_image_create(nkgpu_renderer renderer, uint32_t width, uint32_t height,
                                          nkgpu_image_format format, const uint8_t *pixels,
                                          uint32_t size, uint32_t dynamic_update,
                                          nkgpu_image *out_image NKGPU_OUT);

/** Updates an image rectangle from rows with the supplied byte pitch. */
NKGPU_API nkgpu_result nkgpu_image_update(nkgpu_renderer renderer, nkgpu_image image, uint32_t x,
                                          uint32_t y, uint32_t width, uint32_t height,
                                          const uint8_t *pixels, uint32_t row_pitch);

/** Destroys an image and its texture view; the handle becomes invalid. */
NKGPU_API nkgpu_result nkgpu_image_destroy(nkgpu_renderer renderer, nkgpu_image image);

/* ------------------------------------------------------------------------- */
/* Transfer and readback APIs                                                */
/* ------------------------------------------------------------------------- */

/** Copies an arbitrary byte range between buffers. */
NKGPU_API nkgpu_result nkgpu_buffer_copy(nkgpu_renderer renderer,
                                         const nkgpu_buffer_copy_desc *desc);

/** Copies a 2D region between compatible images. */
NKGPU_API nkgpu_result nkgpu_image_copy(nkgpu_renderer renderer,
                                        const nkgpu_image_copy_desc *desc);

/** Uploads a buffer region into an image. */
NKGPU_API nkgpu_result nkgpu_buffer_to_image(
    nkgpu_renderer renderer, const nkgpu_buffer_image_copy_desc *desc);

/** Downloads an image region into a buffer in top-to-bottom row order. */
NKGPU_API nkgpu_result nkgpu_image_to_buffer(
    nkgpu_renderer renderer, const nkgpu_buffer_image_copy_desc *desc);

/** Begins an asynchronous readback of an image rectangle. */
NKGPU_API nkgpu_result nkgpu_readback_begin_image(
    nkgpu_renderer renderer, const nkgpu_image_readback_desc *desc,
    nkgpu_readback *out_readback NKGPU_OUT);

/** Polls a readback without exposing backend synchronization objects. */
NKGPU_API nkgpu_result nkgpu_readback_query(nkgpu_renderer renderer,
                                             nkgpu_readback readback,
                                             nkgpu_readback_info *out_info NKGPU_OUT);

/** Copies ready readback bytes into caller-owned memory. */
NKGPU_API nkgpu_result nkgpu_readback_read(nkgpu_renderer renderer, nkgpu_readback readback,
                                            uint8_t *data, uint32_t size,
                                            uint32_t *out_size NKGPU_OUT);

/** Destroys a readback object, whether pending or ready. */
NKGPU_API nkgpu_result nkgpu_readback_destroy(nkgpu_renderer renderer,
                                               nkgpu_readback readback);

/**
 * Creates a texture sampler with independent minification and magnification
 * filters and U/V wrap modes.
 *
 * On NKGPU_OK, writes the sampler handle to `out_sampler`; bind it with
 * nkgpu_apply_sampler().
 */
NKGPU_API nkgpu_result nkgpu_sampler_create(nkgpu_renderer renderer, nkgpu_filter min_filter,
                                            nkgpu_filter mag_filter, nkgpu_wrap wrap_u,
                                            nkgpu_wrap wrap_v,
                                            nkgpu_sampler *out_sampler NKGPU_OUT);

/** Destroys a sampler owned by `renderer`; the handle becomes invalid. */
NKGPU_API nkgpu_result nkgpu_sampler_destroy(nkgpu_renderer renderer, nkgpu_sampler sampler);

/** Binds an image view to a slot in the currently active frame. */
NKGPU_API nkgpu_result nkgpu_apply_image(nkgpu_renderer renderer, uint32_t slot, nkgpu_image image);

/** Binds a storage buffer view in the currently active render or compute pass. */
NKGPU_API nkgpu_result nkgpu_apply_storage_buffer(nkgpu_renderer renderer, uint32_t slot,
                                                  nkgpu_buffer buffer);

/** Binds a storage image view in the currently active compute pass. */
NKGPU_API nkgpu_result nkgpu_apply_storage_image(nkgpu_renderer renderer, uint32_t slot,
                                                 nkgpu_image image);

/** Applies a NativeKit graphics image after validating its runtime and device. */
NKGPU_API nkgpu_result nkgpu_apply_graphics_image(nkgpu_renderer renderer, uint32_t slot,
                                                  nk_graphics_image image);

/** Binds a sampler to a slot in the currently active frame. */
NKGPU_API nkgpu_result nkgpu_apply_sampler(nkgpu_renderer renderer, uint32_t slot,
                                           nkgpu_sampler sampler);

/**
 * Draws `element_count` elements for `instance_count` instances.
 *
 * A pipeline and the buffers required by it must already be applied in the
 * active frame. `base_element` is the first vertex or index to draw. The
 * binding state is submitted when this function is called.
 */
NKGPU_API nkgpu_result nkgpu_draw(nkgpu_renderer renderer, uint32_t base_element,
                                  uint32_t element_count, uint32_t instance_count);

/** Dispatches compute workgroups in the active compute pass. */
NKGPU_API nkgpu_result nkgpu_dispatch(nkgpu_renderer renderer, uint32_t x, uint32_t y,
                                      uint32_t z);

/**
 * Submits a packed little-endian command stream in the active frame.
 *
 * Each record starts with two little-endian uint32 values: an opcode and the
 * record's total byte size, including its 8-byte header. The payload layouts
 * are documented by the NKGPU_COMMAND_* constants. A uniform record contains
 * slot, byte count, and that many uniform bytes. The stream must contain whole
 * records with no trailing bytes; submission stops at the first invalid record.
 */
NKGPU_API nkgpu_result nkgpu_submit_commands(nkgpu_renderer renderer, const uint8_t *commands,
                                             uint32_t size);

/** Submits a command stream after checking its explicit ABI version. */
NKGPU_API nkgpu_result nkgpu_submit_command_stream(nkgpu_renderer renderer,
                                                   const nkgpu_command_stream_desc *desc);

/** Render pass kind recorded into a submission batch. */
typedef uint32_t nkgpu_batch_pass_kind;
enum NK_ENUM(nkgpu_batch_pass_kind) {
    /** The pass targets the renderer's window surface framebuffer. */
    NKGPU_BATCH_PASS_WINDOW = 1,
    /** The pass targets an offscreen render target. */
    NKGPU_BATCH_PASS_TARGET = 2,
    /** A compute pass with no render target. */
    NKGPU_BATCH_PASS_COMPUTE = 3,
    /** A transfer pass with no render target. */
    NKGPU_BATCH_PASS_COPY = 4,
};

/** One render pass recorded into a submission batch. */
typedef struct nkgpu_batch_pass {
    /** Set to sizeof(nkgpu_batch_pass) or a larger compatible size. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Selects how the pass target fields below are interpreted. */
    nkgpu_batch_pass_kind kind;
    /** Offscreen target for NKGPU_BATCH_PASS_TARGET; ignored for other pass kinds. */
    nkgpu_render_target target;
    /** Non-zero to clear the target at the start of the pass, zero to load it. */
    uint32_t clear;
    /** Framebuffer width for NKGPU_BATCH_PASS_WINDOW; ignored for target/compute passes. */
    uint32_t width;
    /** Framebuffer height for NKGPU_BATCH_PASS_WINDOW; ignored for target/compute passes. */
    uint32_t height;
} nkgpu_batch_pass;

/**
 * Starts recording a sealed submission batch.
 *
 * A batch records the passes and packed command records of one frame so the
 * submission can be validated, retained, and later replayed as a unit. The
 * renderer owns the batch; destroy it with nkgpu_batch_destroy().
 *
 * Recording does not touch GPU state, so a batch may be built while another
 * frame is active, including one on a different renderer.
 *
 * On NKGPU_OK, writes the batch handle to `out_batch`.
 */
NKGPU_API nkgpu_result nkgpu_batch_begin(nkgpu_renderer renderer, nkgpu_batch *out_batch NKGPU_OUT);

/**
 * Appends one render pass to a batch.
 *
 * Passes are replayed in append order. A window pass requires positive
 * `width`/`height`; a target pass requires a render target owned by the batch's
 * renderer. The target is retained by the batch.
 */
NKGPU_API nkgpu_result nkgpu_batch_append_pass(nkgpu_batch batch, const nkgpu_batch_pass *pass);

/**
 * Appends packed command records to the batch's most recent pass.
 *
 * The byte stream uses the same little-endian record format as
 * nkgpu_submit_commands(), and every handle it references is retained by the
 * batch. The stream is fully validated, including handle resolution, before it
 * is appended.
 */
NKGPU_API nkgpu_result nkgpu_batch_append_command(nkgpu_batch batch, const uint8_t *commands,
                                                  uint32_t size);

/** Appends a command stream after checking its explicit ABI version. */
NKGPU_API nkgpu_result nkgpu_batch_append_command_stream(
    nkgpu_batch batch, const nkgpu_command_stream_desc *desc);

/**
 * Freezes a batch.
 *
 * A sealed batch never changes: appending commands or passes afterwards is an
 * error rather than a silent no-op. Sealing requires at least one pass and may
 * be repeated; a batch is not submitted until it is sealed.
 */
NKGPU_API nkgpu_result nkgpu_batch_seal(nkgpu_batch batch);

/**
 * Binds the render-side context represented by an acquired frame target.
 *
 * This must be called on the render executor. Explicit APIs validate the
 * device/context binding carried by the target without consulting a surface.
 * Physical GL/EGL backends bind their retained context without consulting a
 * surface; aliased GTK/Web backends retain their current context.
 */
NKGPU_API nkgpu_result nkgpu_bind_frame_target(const nk_surface_frame_target *frame_target);

/**
 * Replays a sealed batch on its owning renderer against an already-acquired
 * immutable frame target.
 *
 * Submission does not call nk_surface_make_current() or
 * nk_surface_get_frame_target(). The caller acquires the target on the
 * platform executor, passes the immutable snapshot here on the render
 * executor, and presents or cancels the associated frame afterwards. The
 * batch must belong to
 * `renderer` and that renderer must have no active frame. Retained resources
 * stay valid for the whole submission even if the caller destroyed its own
 * handles, and a batch that fails validation is rejected before any GPU state
 * changes. A sealed batch may be submitted more than once.
 */
NKGPU_API nkgpu_result nkgpu_batch_submit(nkgpu_renderer renderer, nkgpu_batch batch,
                                          const nk_surface_frame_target *frame_target
#ifdef __cplusplus
                                          = nullptr
#endif
);

/**
 * Destroys a batch and releases every resource it retained.
 *
 * The handle becomes invalid after this call and must not be reused. Destroying
 * a renderer also destroys its batches.
 */
NKGPU_API nkgpu_result nkgpu_batch_destroy(nkgpu_batch batch);

/**
 * Ends the active frame, commits its GPU commands, and presents the surface.
 *
 * This must be called after a successful nkgpu_begin_frame(). The renderer is
 * ready for another frame only after this call succeeds.
 */
NKGPU_API nkgpu_result nkgpu_end_frame(nkgpu_renderer renderer);

#ifdef __cplusplus
}
#endif

#endif
