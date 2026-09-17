#ifndef NATIVEKIT_UI_H
#define NATIVEKIT_UI_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit_graphics.h"

/* ------------------------------------------------------------------------- */
/* Export visibility                                                         */
/* ------------------------------------------------------------------------- */

#if defined(_WIN32)
#if defined(NK_STATIC)
#define NKUI_API
#elif defined(NKUI_BUILDING_LIBRARY)
#define NKUI_API __declspec(dllexport)
#else
#define NKUI_API __declspec(dllimport)
#endif
#else
#define NKUI_API __attribute__((visibility("default")))
#endif

#if defined(__clang__)
#define NKUI_OUT __attribute__((annotate("hxi:out")))
#define NKUI_INOUT __attribute__((annotate("hxi:inout")))
#define NKUI_OUT_BUFFER(size_parameter) __attribute__((annotate("hxi:out_buffer")))
#define NKUI_IN_ARRAY(count_parameter) __attribute__((annotate("hxi:in_array")))
#define NKUI_UTF8 __attribute__((annotate("hxi:utf8")))
#define NKUI_NULLABLE_UTF8 __attribute__((annotate("hxi:nullable_utf8")))
#else
#define NKUI_OUT
#define NKUI_INOUT
#define NKUI_OUT_BUFFER(size_parameter)
#define NKUI_IN_ARRAY(count_parameter)
#define NKUI_UTF8
#define NKUI_NULLABLE_UTF8
#endif

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NativeKit's optional retained UI and rendering API.
 *
 * Build a display list from validated command records, create resources such
 * as paths, paints, images, and text layouts, then render the list into a
 * NativeKit OpenGL surface. Resources may be released by the caller after
 * submission; a display list retains the resources it references until it is
 * reset or destroyed. All input arrays and strings are copied on success.
 *
 * The public ABI uses fixed-width values and opaque handles. Functions return
 * NKUI_OK on success and otherwise return a specific nkui_result value.
 */

/* ------------------------------------------------------------------------- */
/* Version, result codes, and handles                                        */
/* ------------------------------------------------------------------------- */

/** Version of the stable NativeKit UI C ABI. */
enum {
    /** Current UI ABI version. */
    NKUI_API_VERSION = 7
};

/** Result returned by a NativeKit UI operation. */
typedef int32_t nkui_result;
enum NK_ENUM(nkui_result) {
    /** The operation completed successfully. */
    NKUI_OK = 0,
    /** An argument was null, out of range, or otherwise invalid. */
    NKUI_ERROR_INVALID_ARGUMENT = -1,
    /** A handle is invalid, stale, or has already been destroyed. */
    NKUI_ERROR_INVALID_HANDLE = -2,
    /** A display-list command stream failed structural or semantic validation. */
    NKUI_ERROR_INVALID_TRANSACTION = -3,
    /** The operation could not allocate the required memory. */
    NKUI_ERROR_OUT_OF_MEMORY = -4,
    /** The graphics backend could not initialize or execute the render. */
    NKUI_ERROR_RENDERING = -5
};

/** Opaque handle for a retained display list. */
NK_DECLARE_HANDLE(nkui_display_list);

/** Opaque handle for a path, paint, image, font collection, or text layout. */
NK_DECLARE_HANDLE(nkui_resource);

/** Opaque handle for a renderer and its backend resources. */
NK_DECLARE_HANDLE(nkui_renderer);

/* ------------------------------------------------------------------------- */
/* Frame and command types                                                   */
/* ------------------------------------------------------------------------- */

/** Describes the logical layout space and physical framebuffer target for one render. */
typedef struct nkui_frame_info {
    /** Set to sizeof(nkui_frame_info) or a larger compatible structure size. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Width of the UI's logical coordinate space in logical pixels. */
    float logical_width;
    /** Height of the UI's logical coordinate space in logical pixels. */
    float logical_height;
    /** Width of the target framebuffer in physical pixels. */
    int32_t framebuffer_width;
    /** Height of the target framebuffer in physical pixels. */
    int32_t framebuffer_height;
    /** Physical pixels per logical pixel; must be positive. */
    float pixel_scale;
} nkui_frame_info;

/** Preparation and geometry-cache counters for one renderer. */
typedef struct nkui_renderer_stats {
    /** Set to sizeof(nkui_renderer_stats) when returned by the API. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Number of NanoVG fill/stroke preparations performed after cache misses. */
    uint64_t path_preparations;
    /** Number of retained path geometry cache hits. */
    uint64_t path_cache_hits;
    /** Number of retained path geometry cache misses. */
    uint64_t path_cache_misses;
    /** Number of vertices produced by direct path preparation. */
    uint64_t path_vertices_generated;
    /** Vector capacity bytes allocated for newly prepared path geometry. */
    uint64_t path_geometry_bytes_allocated;
    /** Nanoseconds spent inside NanoVG path preparation. */
    uint64_t path_tessellation_nanoseconds;
    /** Vector capacity bytes currently retained by the path geometry cache. */
    uint64_t path_geometry_bytes_retained;
    /** Render-plan commands submitted through this renderer. */
    uint64_t render_plan_commands;
    /** Display-list render calls submitted through this renderer. */
    uint64_t display_list_count;
    /** Cumulative display-list bytes processed by this renderer. */
    uint64_t display_list_bytes;
    /** Completed GPU window and offscreen frames. */
    uint64_t gpu_frames;
    /** GPU passes submitted by this renderer. */
    uint64_t gpu_passes;
    /** GPU draw calls submitted by this renderer. */
    uint64_t gpu_draw_calls;
    /** Live GPU buffer count. */
    uint64_t buffers_live;
    /** Live GPU image count, excluding render-target images. */
    uint64_t images_live;
    /** Live GPU sampler count. */
    uint64_t samplers_live;
    /** Live GPU shader count. */
    uint64_t shaders_live;
    /** Live GPU pipeline count. */
    uint64_t pipelines_live;
    /** Live offscreen render-target count. */
    uint64_t render_targets_live;
    /** Estimated live GPU buffer bytes. */
    uint64_t buffer_bytes;
    /** Estimated live GPU image bytes, excluding render targets. */
    uint64_t image_bytes;
    /** Estimated render-target color and depth storage bytes. */
    uint64_t render_target_bytes;
    /** Bytes uploaded into GPU resources. */
    uint64_t upload_bytes;
    /** Successfully created GPU resources. */
    uint64_t resource_creations;
    /** GPU resource destruction requests completed. */
    uint64_t resource_destructions;
    /** Surface/device identity changes observed by the GPU adapter. */
    uint64_t surface_recreations;
    /** Fatal device-loss transitions. */
    uint64_t device_losses;
    /** Failed GPU resource allocations. */
    uint64_t failed_allocations;
    /** Live text atlas pages. */
    uint64_t atlas_pages;
    /** Live text atlas bytes mirrored by the renderer. */
    uint64_t atlas_bytes;
    /** Atlas page uploads issued by the UI renderer. */
    uint64_t glyph_uploads;
    /** Monotonic text raster-scale generation observed by the renderer. */
    uint64_t atlas_scale_generation;
    /** Retained text-layout cache hits. */
    uint64_t text_layout_cache_hits;
    /** Retained text-layout cache misses. */
    uint64_t text_layout_cache_misses;
    /** Custom paint nodes compiled by layout sessions. */
    uint64_t custom_paint_nodes;
    /** Display-list bytes used by custom paint nodes. */
    uint64_t custom_paint_bytes;
    /** Glyphs rasterized into the retained text atlas. */
    uint64_t glyphs_rasterized;
    /** Atlas page generations created or rebuilt on the GPU. */
    uint64_t atlas_rebuilds;
    /** Atlas partial subregion updates; currently zero when full uploads are used. */
    uint64_t atlas_partial_updates;
    /** Bytes dirtied by glyph rasterization and submitted for atlas upload. */
    uint64_t atlas_dirty_upload_bytes;
    /** Effect passes compiled by the UI compositor. */
    uint64_t effect_passes;
    /** Mask passes compiled by the UI compositor. */
    uint64_t mask_passes;
    /** Backdrop effect passes compiled by the UI compositor. */
    uint64_t backdrop_passes;
    /** Isolated layers compiled by the UI compositor. */
    uint64_t isolated_layers;
    /** Bounded layers compiled by the UI compositor. */
    uint64_t bounded_layers;
    /** Transient render-target acquisitions served by the renderer pool. */
    uint64_t transient_target_pool_hits;
    /** Transient render-target acquisitions that required allocation. */
    uint64_t transient_target_pool_misses;
    /** Number of transient targets retained in the pool at the last query. */
    uint64_t transient_target_pool_count;
    /** Estimated bytes retained by pooled transient render targets. */
    uint64_t transient_target_pool_bytes;
    /** Effect results served by the persistent renderer cache. */
    uint64_t effect_cache_hits;
    /** Effect results that required a new render pass. */
    uint64_t effect_cache_misses;
    /** Number of effect results retained by the renderer cache. */
    uint64_t effect_cache_entries;
    /** Estimated bytes retained by cached effect results. */
    uint64_t effect_cache_bytes;
} nkui_renderer_stats;

/** 16-bit opcode identifying one display-list command record. */
typedef uint16_t nkui_command_opcode;

/** Display-list command opcode constants. */
enum NK_ENUM(nkui_command_opcode) {
    /** Replace the current transform with a six-value affine matrix. */
    NKUI_COMMAND_SET_TRANSFORM = 1,
    /** Select the paint resource used by subsequent drawing commands. */
    NKUI_COMMAND_SET_PAINT = 2,
    /** Set the alpha multiplier applied to subsequent drawing commands. */
    NKUI_COMMAND_SET_GLOBAL_ALPHA = 3,
    /** Select the compositing mode used by subsequent drawing commands. */
    NKUI_COMMAND_SET_COMPOSITE_MODE = 4,
    /** Save the current transform, paint, alpha, composite, and clip state. */
    NKUI_COMMAND_PUSH_STATE = 5,
    /** Restore the most recently saved drawing state. */
    NKUI_COMMAND_POP_STATE = 6,
    /** Intersect the current clip with a logical-space rectangle. */
    NKUI_COMMAND_CLIP_RECT = 7,
    /** Fill a previously created path resource. */
    NKUI_COMMAND_DRAW_PATH = 8,
    /** Draw an image resource into a logical-space rectangle. */
    NKUI_COMMAND_DRAW_IMAGE = 9,
    /** Draw a text layout at a logical-space position. */
    NKUI_COMMAND_DRAW_TEXT_LAYOUT = 10,
    /** Start a composited layer with an opacity and composite mode. */
    NKUI_COMMAND_BEGIN_LAYER = 11,
    /** End the most recently started layer. */
    NKUI_COMMAND_END_LAYER = 12,
    /** Composite a render-target resource into a logical-space rectangle. */
    NKUI_COMMAND_DRAW_RENDER_TARGET = 13,
    /** Stroke a path using the current paint and the command's stroke style. */
    NKUI_COMMAND_STROKE_PATH = 14,
    /** Paint a geometry-based rounded-rectangle shadow. */
    NKUI_COMMAND_DRAW_BOX_SHADOW = 15
};

/** Version value required in every command header. */
enum { NKUI_COMMAND_VERSION = 1 };
/** Version emitted for the variable-length NKUI_COMMAND_BEGIN_LAYER record. */
enum { NKUI_LAYER_COMMAND_VERSION = 2 };

/** Alpha compositing mode supported by the current UI renderer. */
typedef uint32_t nkui_composite_mode;
enum NK_ENUM(nkui_composite_mode) {
    /** Draw new content over the existing destination. */
    NKUI_COMPOSITE_SOURCE_OVER = 1
};

/** Flags describing why a layer needs an isolated render target. */
typedef uint32_t nkui_layer_flags;
enum NK_ENUM(nkui_layer_flags) {
    /** Allocate a separate target even when opacity is 1. */
    NKUI_LAYER_ISOLATED = 1u << 0,
    /** The four bounds fields identify the logical target region. */
    NKUI_LAYER_HAS_BOUNDS = 1u << 1
};

/** Effect descriptors currently supported by a layer command. */
typedef uint32_t nkui_effect_kind;
enum NK_ENUM(nkui_effect_kind) {
    /** The layer has no sampled effect. */
    NKUI_EFFECT_NONE = 0,
    /** Apply the supplied row-major 4x5 color matrix. */
    NKUI_EFFECT_COLOR_MATRIX = 1,
    /** Apply a separable Gaussian blur; the compact operation stores sigma in slot 0. */
    NKUI_EFFECT_BLUR = 2,
    /** Apply a subtree alpha drop shadow; the compact operation stores sigma, offset, and RGBA. */
    NKUI_EFFECT_DROP_SHADOW = 3,
    /** Apply a renderer-registered custom effect. */
    NKUI_EFFECT_CUSTOM = 4
};

/** Maximum number of float components carried by one custom effect. */
enum { NKUI_CUSTOM_EFFECT_PARAMETER_COMPONENTS = 20 };
/** Maximum number of ordered operations in either layer effect program. */
enum { NKUI_EFFECT_PROGRAM_MAX_OPS = 8 };

/** Backend-neutral descriptor for a renderer-owned custom effect. */
typedef struct nkui_custom_effect_descriptor {
    /** Positive ID of the native effect registration. */
    uint32_t registration_id;
    /** Number of meaningful entries in parameters. */
    uint32_t parameter_count;
    /** Reserved; must be 1 until multipass custom effects are introduced. */
    uint32_t pass_count;
    /** Reserved; must be 1 (source input) until explicit custom graphs are introduced. */
    uint32_t sampling_inputs;
    /** Logical ink expansion: left, top, right, bottom. */
    float ink_overflow[4];
    /** Typed parameters flattened according to the native registration schema. */
    float parameters[NKUI_CUSTOM_EFFECT_PARAMETER_COMPONENTS];
} nkui_custom_effect_descriptor;

/** One compact wire operation in a foreground or backdrop effect program. */
typedef struct nkui_effect_op_command {
    /** Operation kind; custom operations use the descriptor below. */
    nkui_effect_kind kind;
    /** Matrix values, or compact scalar/color parameters decoded by the native compositor. */
    float color_matrix[20];
    /** Renderer-owned custom operation data; ignored for non-custom kinds. */
    nkui_custom_effect_descriptor custom;
} nkui_effect_op_command;

/** Native shader implementation registered for a custom effect ID. */
typedef struct nkui_custom_effect_registration {
    /** Set to sizeof(nkui_custom_effect_registration). */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Positive application-defined ID carried by nkui_custom_effect_descriptor. */
    uint32_t registration_id;
    /** Diagnostic name; copied by the renderer registration. */
    const char *name NKUI_UTF8;
    /** GLSL 4.10 fragment source, or NULL when the backend does not use it. */
    const char *glsl410_fragment NKUI_NULLABLE_UTF8;
    /** GLSL ES 3.00 fragment source, or NULL when the backend does not use it. */
    const char *glsl300es_fragment NKUI_NULLABLE_UTF8;
    /** HLSL shader-model-5 fragment source, or NULL when the backend does not use it. */
    const char *hlsl5_fragment NKUI_NULLABLE_UTF8;
    /** Metal fragment source, or NULL when the backend does not use it. */
    const char *metal_macos_fragment NKUI_NULLABLE_UTF8;
    /** Number of meaningful float components in the custom descriptor. */
    uint32_t parameter_components;
    /** Reserved; must be 1; the current runtime supports one registered pass. */
    uint32_t pass_count;
    /** Reserved; must be 1 (source input). */
    uint32_t sampling_inputs;
    /** Logical ink expansion: left, top, right, bottom. */
    float ink_overflow[4];
} nkui_custom_effect_registration;

/** Source-alpha masks are separate composition inputs, not sequential effects. */
typedef uint32_t nkui_mask_kind;
enum NK_ENUM(nkui_mask_kind) {
    NKUI_MASK_NONE = 0,
    NKUI_MASK_RECTANGLE = 1,
    NKUI_MASK_ROUNDED_RECT = 2,
    NKUI_MASK_CIRCLE = 3,
    NKUI_MASK_LINEAR_GRADIENT = 4,
    NKUI_MASK_IMAGE = 5
};

/** Fixed header present at the start of every display-list command record. */
typedef struct nkui_command_header {
    /** One of the NKUI_COMMAND_* opcode values. */
    nkui_command_opcode opcode;
    /** Must match the opcode: NKUI_COMMAND_VERSION, or NKUI_LAYER_COMMAND_VERSION for BeginLayer.
     */
    uint16_t version;
    /** Total record size in bytes, including this header; must be 4-byte aligned. */
    uint32_t size;
} nkui_command_header;

/** Summary of the command stream currently stored in a display list. */
typedef struct nkui_transaction_info {
    /** Set to sizeof(nkui_transaction_info) when returned by the API. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** UI ABI version that produced this summary. */
    uint32_t api_version;
    /** Number of command-stream bytes currently stored. */
    uint32_t command_bytes;
    /** Number of validated command records currently stored. */
    uint32_t command_count;
} nkui_transaction_info;

/** Payload for NKUI_COMMAND_SET_TRANSFORM. */
typedef struct nkui_transform_command {
    /** Command record header. */
    nkui_command_header header;
    /** Affine matrix [a, b, c, d, tx, ty]; x'=a*x+c*y+tx and y'=b*x+d*y+ty. */
    float matrix[6];
} nkui_transform_command;

/** Payload for NKUI_COMMAND_SET_PAINT and resource-only draw commands. */
typedef struct nkui_resource_command {
    /** Command record header. */
    nkui_command_header header;
    /** Paint or path resource selected or drawn by this command. */
    nkui_resource resource;
} nkui_resource_command;

/** End-cap style used when stroking an open path. */
typedef uint32_t nkui_path_line_cap;
enum NK_ENUM(nkui_path_line_cap) {
    /** Stop the stroke at the endpoint. */
    NKUI_PATH_LINE_CAP_BUTT = 0,
    /** Add a semicircular cap at the endpoint. */
    NKUI_PATH_LINE_CAP_ROUND = 1,
    /** Extend the stroke by half its width at the endpoint. */
    NKUI_PATH_LINE_CAP_SQUARE = 2
};

/** Join style used where two stroked segments meet. */
typedef uint32_t nkui_path_line_join;
enum NK_ENUM(nkui_path_line_join) {
    /** Join segments with a circular arc. */
    NKUI_PATH_LINE_JOIN_ROUND = 1,
    /** Join segments with a clipped corner. */
    NKUI_PATH_LINE_JOIN_BEVEL = 3,
    /** Extend the outer edges to their intersection, subject to the miter limit. */
    NKUI_PATH_LINE_JOIN_MITER = 4
};

/** Payload for NKUI_COMMAND_STROKE_PATH. */
typedef struct nkui_stroke_path_command {
    /** Command record header. */
    nkui_command_header header;
    /** Path resource to stroke. */
    nkui_resource path;
    /** Stroke width in logical pixels; must be positive and finite. */
    float width;
    /** End-cap style for open contours. */
    nkui_path_line_cap line_cap;
    /** Join style for connected segments. */
    nkui_path_line_join line_join;
    /** Positive miter limit used by NKUI_PATH_LINE_JOIN_MITER. */
    float miter_limit;
} nkui_stroke_path_command;

/** Payload for NKUI_COMMAND_DRAW_BOX_SHADOW. */
typedef struct nkui_draw_box_shadow_command {
    nkui_command_header header;
    float x;
    float y;
    float width;
    float height;
    float offset_x;
    float offset_y;
    /** Gaussian sigma; high-level ShadowBlur values are converted before encoding. */
    float blur_sigma;
    float spread;
    float radii[4];
    float color[4];
} nkui_draw_box_shadow_command;

/** Payload for NKUI_COMMAND_SET_GLOBAL_ALPHA. */
typedef struct nkui_scalar_command {
    /** Command record header. */
    nkui_command_header header;
    /** Alpha multiplier in the inclusive range 0..1. */
    float value;
} nkui_scalar_command;

/** Payload for NKUI_COMMAND_SET_COMPOSITE_MODE. */
typedef struct nkui_composite_command {
    /** Command record header. */
    nkui_command_header header;
    /** Compositing mode for subsequent drawing. */
    nkui_composite_mode mode;
} nkui_composite_command;

/** Payload for NKUI_COMMAND_CLIP_RECT. */
typedef struct nkui_rect_command {
    /** Command record header. */
    nkui_command_header header;
    /** Left edge in logical coordinates. */
    float x;
    /** Top edge in logical coordinates. */
    float y;
    /** Rectangle width; must be non-negative and finite. */
    float width;
    /** Rectangle height; must be non-negative and finite. */
    float height;
} nkui_rect_command;

/** Payload for image, text-layout, and render-target draw commands. */
typedef struct nkui_draw_rect_command {
    /** Command record header. */
    nkui_command_header header;
    /** Resource drawn by the command. */
    nkui_resource resource;
    /** Left edge in logical coordinates. */
    float x;
    /** Top edge in logical coordinates. */
    float y;
    /** Draw width; must be non-negative and finite. */
    float width;
    /** Draw height; must be non-negative and finite. */
    float height;
} nkui_draw_rect_command;

/** Descriptor for the source-alpha mask of an isolated layer. */
typedef struct nkui_mask_descriptor {
    /** Shape, gradient, or image mask kind. */
    nkui_mask_kind kind;
    /** Image resource used only by NKUI_MASK_IMAGE. */
    nkui_resource image;
    /** Kind-specific values; coordinates are normalized for gradients. */
    float values[8];
} nkui_mask_descriptor;

/** Fixed prefix for the variable-length version 2 NKUI_COMMAND_BEGIN_LAYER record. */
typedef struct nkui_layer_command {
    /** Command record header. */
    nkui_command_header header;
    /** Layer opacity in the inclusive range 0..1. Values below 1 isolate the layer. */
    float opacity;
    /** Compositing mode used when the layer is applied. */
    nkui_composite_mode composite_mode;
    /** Left edge of the optional bounded target in logical coordinates. */
    float x;
    /** Top edge of the optional bounded target in logical coordinates. */
    float y;
    /** Width of the optional bounded target; must be positive when present. */
    float width;
    /** Height of the optional bounded target; must be positive when present. */
    float height;
    /** Combination of nkui_layer_flags. */
    nkui_layer_flags flags;
    /** Source-alpha mask; use NKUI_MASK_NONE when no mask is present. */
    nkui_mask_descriptor mask;
    /** Number of active foreground operations following this prefix. */
    uint32_t foreground_count;
    /** Number of active backdrop operations following the foreground operations. */
    uint32_t backdrop_count;
} nkui_layer_command;

/** Version 1 unbounded layer record retained for wire decoding. */
typedef struct nkui_layer_v1_unbounded_command {
    nkui_command_header header;
    float opacity;
    nkui_composite_mode composite_mode;
} nkui_layer_v1_unbounded_command;

/** Common version 1 bounded layer prefix retained for wire decoding. */
typedef struct nkui_layer_v1_command {
    nkui_command_header header;
    float opacity;
    nkui_composite_mode composite_mode;
    float x;
    float y;
    float width;
    float height;
    nkui_layer_flags flags;
} nkui_layer_v1_command;

typedef struct nkui_layer_v1_effect_command {
    nkui_layer_v1_command base;
    nkui_effect_kind effect_kind;
    float effect_matrix[20];
} nkui_layer_v1_effect_command;

typedef struct nkui_layer_v1_mask_command {
    nkui_layer_v1_command base;
    nkui_effect_kind effect_kind;
    float effect_matrix[20];
    nkui_mask_descriptor mask;
} nkui_layer_v1_mask_command;

typedef struct nkui_layer_v1_backdrop_command {
    nkui_layer_v1_command base;
    nkui_effect_kind effect_kind;
    float effect_matrix[20];
    nkui_mask_descriptor mask;
    nkui_effect_kind backdrop_effect_kind;
    float backdrop_effect_matrix[20];
} nkui_layer_v1_backdrop_command;

typedef struct nkui_layer_v1_custom_effect_command {
    nkui_layer_v1_command base;
    nkui_custom_effect_descriptor effect;
} nkui_layer_v1_custom_effect_command;

/* ------------------------------------------------------------------------- */
/* Text and layout types                                                      */
/* ------------------------------------------------------------------------- */

/** Font role used when adding a font file to a collection. */
typedef uint32_t nkui_font_family;
enum NK_ENUM(nkui_font_family) {
    /** General-purpose text font. */
    NKUI_FONT_FAMILY_DEFAULT = 0,
    /** Font intended to provide emoji glyphs. */
    NKUI_FONT_FAMILY_EMOJI = 1
};

/** Word-breaking policy used by both explicit and automatic text layouts. */
typedef uint32_t nkui_text_wrap;
enum NK_ENUM(nkui_text_wrap) {
    /** Do not wrap the paragraph. */
    NKUI_TEXT_WRAP_NONE = 0,
    /** Wrap at word boundaries. */
    NKUI_TEXT_WRAP_WORD = 1,
    /** Wrap at word or character boundaries. */
    NKUI_TEXT_WRAP_WORD_CHARACTER = 2
};

/** Horizontal alignment of paragraph lines. */
typedef uint32_t nkui_text_alignment;
enum NK_ENUM(nkui_text_alignment) {
    NKUI_TEXT_ALIGN_START = 0,
    NKUI_TEXT_ALIGN_CENTER = 1,
    NKUI_TEXT_ALIGN_END = 2
};

/** Base direction requested for a paragraph. */
typedef uint32_t nkui_text_direction;
enum NK_ENUM(nkui_text_direction) {
    NKUI_TEXT_DIRECTION_AUTO = 0,
    NKUI_TEXT_DIRECTION_LTR = 1,
    NKUI_TEXT_DIRECTION_RTL = 2
};

/** Text navigation convention used by text editors. */
typedef uint32_t nkui_text_navigation_behavior;
enum NK_ENUM(nkui_text_navigation_behavior) {
    /** Standard Control-arrow word and paragraph movement. */
    NKUI_TEXT_NAVIGATION_BEHAVIOR_STANDARD = 0,
    /** macOS Option-arrow word and paragraph movement. */
    NKUI_TEXT_NAVIGATION_BEHAVIOR_MACOS = 1
};

/** Font and inline spacing inputs shared by explicit and tree-owned layouts. */
typedef struct nkui_text_style {
    /** Set to sizeof(nkui_text_style) or a larger compatible structure size. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Font role selected from the owning font collection. */
    nkui_font_family family;
    /** Font size in logical pixels; must be positive and finite. */
    float font_size;
    /** Additional horizontal spacing between adjacent characters. */
    float letter_spacing;
} nkui_text_style;

/** Paragraph-level wrapping, alignment, and line-height inputs. */
typedef struct nkui_paragraph_style {
    /** Set to sizeof(nkui_paragraph_style) or a larger compatible structure size. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Explicit line height, or zero for the natural line height. */
    float line_height;
    nkui_text_wrap wrap;
    nkui_text_alignment alignment;
    nkui_text_direction direction;
} nkui_paragraph_style;

/** Bounding rectangle returned for a text layout. */
typedef struct nkui_text_metrics {
    /** Set to sizeof(nkui_text_metrics) when returned by the API. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Left edge of the bounds in layout coordinates. */
    float x;
    /** Top edge of the bounds in layout coordinates. */
    float y;
    /** Width of the laid-out text. */
    float width;
    /** Height of the laid-out text. */
    float height;
} nkui_text_metrics;

/** Flags returned in nkui_text_intrinsic_metrics.flags. */
typedef uint32_t nkui_text_intrinsic_flags;
enum NK_FLAGS(nkui_text_intrinsic_flags) {
    /** The first-baseline value is valid. */
    NKUI_TEXT_INTRINSIC_HAS_BASELINE = 1u << 0
};

/** Legal intrinsic widths and natural baseline metrics for a text layout. */
typedef struct nkui_text_intrinsic_metrics {
    /** Set to sizeof(nkui_text_intrinsic_metrics) when returned by the API. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Largest segment that cannot legally break under the paragraph policy. */
    float min_content_width;
    /** Width of the unwrapped paragraph. */
    float max_content_width;
    /** Natural height before an external width constraint is applied. */
    float natural_height;
    /** First baseline relative to the text layout origin. */
    float first_baseline;
    nkui_text_intrinsic_flags flags;
} nkui_text_intrinsic_metrics;

/** A text caret position measured in Unicode code points. */
typedef struct nkui_text_position {
    /** Zero-based code-point offset in the original UTF-8 text. */
    int32_t offset;
    /** Caret affinity returned by hit testing; pass it back unchanged to caret queries. */
    uint32_t affinity;
} nkui_text_position;

/** Visual geometry and direction for a text caret. */
typedef struct nkui_text_caret {
    /** Set to sizeof(nkui_text_caret) when returned by the API. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** X coordinate of the caret baseline origin. */
    float x;
    /** Y coordinate of the caret baseline origin. */
    float y;
    /** Signed distance from the baseline to the top of the caret. */
    float ascender;
    /** Distance from the baseline to the bottom of the caret. */
    float descender;
    /** Horizontal caret slope per unit of vertical distance. */
    float slope;
    /** Text-direction code at the caret; preserve it when selecting a visual direction. */
    nkui_text_direction direction;
} nkui_text_caret;

/** One visual rectangle covered by a text selection. */
typedef struct nkui_text_rect {
    /** Set to sizeof(nkui_text_rect) in the returned buffer. */
    uint32_t struct_size NK_STRUCT_SIZE;
    float x;
    float y;
    float width;
    float height;
} nkui_text_rect;

/* ------------------------------------------------------------------------- */
/* Path, color, and image types                                              */
/* ------------------------------------------------------------------------- */

/** Geometry operation used by one element of a path. */
typedef uint32_t nkui_path_verb;
enum NK_ENUM(nkui_path_verb) {
    /** Move the current point; values[0..1] are x and y. */
    NKUI_PATH_MOVE_TO = 1,
    /** Add a straight segment; values[0..1] are x and y. */
    NKUI_PATH_LINE_TO = 2,
    /** Add a cubic Bézier segment; values contain two controls and an endpoint. */
    NKUI_PATH_BEZIER_TO = 3,
    /** Add a quadratic Bézier segment; values contain one control and an endpoint. */
    NKUI_PATH_QUADRATIC_TO = 4,
    /** Add a tangent arc; values contain the two tangent points and radius. */
    NKUI_PATH_ARC_TO = 5,
    /** Close the current contour; values are ignored. */
    NKUI_PATH_CLOSE = 6
};

/** One path operation supplied to nkui_path_create(). */
typedef struct nkui_path_element {
    /** Operation encoded by this element. */
    nkui_path_verb verb;
    /** Up to six finite coordinates; the active count depends on `verb`. */
    float values[6];
} nkui_path_element;

/** A normalized RGBA paint color. */
typedef struct nkui_color {
    /** Red component, normally in the range 0..1. */
    float red;
    /** Green component, normally in the range 0..1. */
    float green;
    /** Blue component, normally in the range 0..1. */
    float blue;
    /** Alpha component, normally in the range 0..1. */
    float alpha;
} nkui_color;

/** Maximum number of stops accepted by a linear gradient paint. */
enum { NKUI_GRADIENT_MAX_STOPS = 8 };

/** One normalized color stop in a gradient paint. */
typedef struct nkui_gradient_stop {
    /** Position along the gradient line, in the inclusive range 0..1. */
    float offset;
    /** Color at this stop. */
    nkui_color color;
} nkui_gradient_stop;

/** Pixel format accepted by nkui_image_create(). */
typedef uint32_t nkui_image_format;
enum NK_ENUM(nkui_image_format) {
    /** Invalid format; cannot be used to create an image. */
    NKUI_IMAGE_FORMAT_INVALID = 0,
    /** One byte per pixel; rendered as white with the byte used as alpha. */
    NKUI_IMAGE_R8 = 1,
    /** Four bytes per pixel in red, green, blue, alpha order. */
    NKUI_IMAGE_RGBA8 = 2
};

/** Sampling filter used when an image is scaled. */
typedef uint32_t nkui_image_filter;
enum NK_ENUM(nkui_image_filter) { NKUI_IMAGE_FILTER_LINEAR = 1, NKUI_IMAGE_FILTER_NEAREST = 2 };

/* ------------------------------------------------------------------------- */
/* Display-list APIs                                                         */
/* ------------------------------------------------------------------------- */

/** Returns the UI ABI version implemented by this module. */
NKUI_API uint32_t nkui_api_version(void);

/**
 * Creates an empty display list.
 *
 * On NKUI_OK, writes the list handle to `out_list`. The list remains valid
 * until nkui_display_list_destroy() and can be replaced repeatedly with
 * nkui_display_list_submit().
 */
NKUI_API nkui_result nkui_display_list_create(nkui_display_list *out_list NKUI_OUT);

/**
 * Destroys a display list and releases its retained resource references.
 *
 * The list handle becomes invalid. Resources explicitly destroyed by the
 * caller are kept alive until this release if the list still references them.
 */
NKUI_API nkui_result nkui_display_list_destroy(nkui_display_list list);

/**
 * Clears a display list while keeping its handle valid.
 *
 * Any resources retained by the previous command stream are released. The
 * list can then accept a new stream with nkui_display_list_submit().
 */
NKUI_API nkui_result nkui_display_list_reset(nkui_display_list list);

/**
 * Validates and atomically replaces a display list's command stream.
 *
 * `commands` contains native-layout command records and may be NULL only when
 * `command_bytes` is zero; submitting zero bytes clears the list. Every record
 * must have a valid nkui_command_header, version, size, and resource kind.
 * Validation happens before replacement, so an invalid stream leaves the old
 * list unchanged. The command bytes are copied, and referenced resources are
 * retained until the next successful replacement, reset, or destruction.
 */
NKUI_API nkui_result nkui_display_list_submit(nkui_display_list list,
                                              const uint8_t *commands NKUI_IN_ARRAY(command_bytes),
                                              uint32_t command_bytes);

/**
 * Returns the byte and command counts for a display list.
 *
 * On NKUI_OK, writes a structure whose `struct_size` is set to
 * sizeof(nkui_transaction_info) and whose `api_version` identifies the ABI.
 */
NKUI_API nkui_result nkui_display_list_get_info(nkui_display_list list,
                                                nkui_transaction_info *out_info NKUI_OUT);

/* ------------------------------------------------------------------------- */
/* Fonts and text-layout APIs                                                */
/* ------------------------------------------------------------------------- */

/** Creates an empty font collection and writes its resource handle. */
NKUI_API nkui_result nkui_font_collection_create(nkui_resource *out_fonts NKUI_OUT);

/**
 * Adds a font file to a collection.
 *
 * `path` is a non-empty UTF-8 path and is copied by the module. Add at least
 * one font before creating a layout; multiple fonts can provide fallback
 * glyphs, with emoji fonts selected using NKUI_FONT_FAMILY_EMOJI.
 */
NKUI_API nkui_result nkui_font_collection_add(nkui_resource fonts, const char *path NKUI_UTF8,
                                              nkui_font_family family);

/** Adds a copied TTF/OTF byte buffer, suitable for asynchronously fetched browser assets. */
NKUI_API nkui_result
nkui_font_collection_add_data(nkui_resource fonts, const char *name NKUI_UTF8,
                              const uint8_t *font_data NKUI_IN_ARRAY(font_bytes),
                              uint32_t font_bytes, nkui_font_family family);

/** Enables best-effort platform system-font fallback discovery for new layouts. */
NKUI_API nkui_result nkui_font_collection_add_system_fallbacks(nkui_resource fonts);

/**
 * Shapes and lays out a UTF-8 string within a maximum width.
 *
 * `fonts` must be a valid collection containing at least one font. `width` and
 * `font_size` are positive logical-pixel values. On NKUI_OK, writes a text
 * layout resource to `out_layout`; the input string is consumed while the
 * layout is built. A null text pointer represents an empty UTF-8 string.
 */
NKUI_API nkui_result nkui_text_layout_create(nkui_resource fonts,
                                             const char *text NKUI_NULLABLE_UTF8, float width,
                                             float font_size, nkui_resource *out_layout NKUI_OUT);

/** Creates a text layout using the shared semantic text and paragraph styles; null text is empty.
 */
NKUI_API nkui_result nkui_text_layout_create_styled(nkui_resource fonts,
                                                    const char *text NKUI_NULLABLE_UTF8,
                                                    float width, const nkui_text_style *text_style,
                                                    const nkui_paragraph_style *paragraph_style,
                                                    nkui_resource *out_layout NKUI_OUT);

/** Re-shapes an existing layout while retaining its native resource handle; null text is empty. */
NKUI_API nkui_result nkui_text_layout_update(nkui_resource layout,
                                             const char *text NKUI_NULLABLE_UTF8, float width,
                                             const nkui_text_style *text_style,
                                             const nkui_paragraph_style *paragraph_style);

/** Re-shapes an existing layout with new UTF-8 text while retaining its handle and style; null text
 * is empty. */
NKUI_API nkui_result nkui_text_layout_set_text(nkui_resource layout,
                                               const char *text NKUI_NULLABLE_UTF8);

/** Sets the vertex color used when the layout is drawn through Canvas.drawText(). */
NKUI_API nkui_result nkui_text_layout_set_color(nkui_resource layout, nkui_color color);

/** Returns the layout bounds in `out_metrics`. */
NKUI_API nkui_result nkui_text_layout_measure(nkui_resource layout,
                                              nkui_text_metrics *out_metrics NKUI_OUT);

/** Returns legal intrinsic widths, natural height, and optional first baseline. */
NKUI_API nkui_result nkui_text_layout_intrinsic_metrics(
    nkui_resource layout, nkui_text_intrinsic_metrics *out_metrics NKUI_OUT);

/**
 * Converts a logical-space point into the nearest text caret position.
 *
 * On NKUI_OK, writes a code-point offset and affinity to `out_position`.
 * Coordinates are relative to the text layout's origin; pass the returned
 * position to nkui_text_layout_caret() when its visual geometry is needed.
 */
NKUI_API nkui_result nkui_text_layout_hit_test(nkui_resource layout, float x, float y,
                                               nkui_text_position *out_position NKUI_OUT);
/** Converts a shaped text position to its affinity-aware code-point insertion offset. */
NKUI_API nkui_result nkui_text_layout_position_offset(nkui_resource layout,
                                                      nkui_text_position position,
                                                      int32_t *out_offset NKUI_OUT);

/**
 * Returns visual caret geometry for a text position.
 *
 * `position` should normally be a value returned by
 * nkui_text_layout_hit_test(). On NKUI_OK, writes the caret geometry to
 * `out_caret`.
 */
NKUI_API nkui_result nkui_text_layout_caret(nkui_resource layout, nkui_text_position position,
                                            nkui_text_caret *out_caret NKUI_OUT);

/**
 * Returns the visual rectangles covered by a code-point selection.
 *
 * Query the required buffer size by passing NULL. On NKUI_OK, `inout_bytes` is
 * set to the exact byte count. Each `nkui_text_rect` record has its
 * `struct_size` field populated. Coordinates are relative to the text origin.
 */
NKUI_API nkui_result nkui_text_layout_get_selection_rects(
    nkui_resource layout, nkui_text_position start, nkui_text_position end,
    uint8_t *out_buffer NKUI_OUT_BUFFER(inout_bytes), uint32_t *inout_bytes NKUI_INOUT);

/** Returns the next grapheme boundary at or after `offset`. */
NKUI_API nkui_result nkui_text_layout_next_grapheme(nkui_resource layout, int32_t offset,
                                                    int32_t *out_offset NKUI_OUT);

/** Returns the previous grapheme boundary at or before `offset`. */
NKUI_API nkui_result nkui_text_layout_previous_grapheme(nkui_resource layout, int32_t offset,
                                                        int32_t *out_offset NKUI_OUT);

/** Returns the nearest grapheme boundary to `offset`. */
NKUI_API nkui_result nkui_text_layout_align_grapheme(nkui_resource layout, int32_t offset,
                                                     int32_t *out_offset NKUI_OUT);

/** Returns the Skribidi word range containing the code-point `offset`. */
NKUI_API nkui_result nkui_text_layout_word_range_at(nkui_resource layout, int32_t offset,
                                                    int32_t *out_start NKUI_OUT,
                                                    int32_t *out_end NKUI_OUT);

/** Returns the visual line range containing the code-point `offset`. */
NKUI_API nkui_result nkui_text_layout_line_range_at(nkui_resource layout, int32_t offset,
                                                    int32_t *out_start NKUI_OUT,
                                                    int32_t *out_end NKUI_OUT);

/**
 * Moves to a word boundary. `direction` must be -1 or +1. The behavior selects
 * standard Control-arrow or macOS Option-arrow word movement.
 */
NKUI_API nkui_result nkui_text_layout_move_word(nkui_resource layout, int32_t offset,
                                                int32_t direction,
                                                nkui_text_navigation_behavior behavior,
                                                int32_t *out_offset NKUI_OUT);

/** Moves to a paragraph boundary using Control-arrow or macOS Option-arrow conventions. */
NKUI_API nkui_result nkui_text_layout_move_paragraph(nkui_resource layout, int32_t offset,
                                                     int32_t direction,
                                                     nkui_text_navigation_behavior behavior,
                                                     int32_t *out_offset NKUI_OUT);

/** Returns a half-open insertion-offset range for the word under a hit-tested position. */
NKUI_API nkui_result nkui_text_layout_word_range(nkui_resource layout, nkui_text_position position,
                                                 int32_t *out_start NKUI_OUT,
                                                 int32_t *out_end NKUI_OUT);

/* ------------------------------------------------------------------------- */
/* Path, paint, and image APIs                                                */
/* ------------------------------------------------------------------------- */

/**
 * Creates a path by copying `count` path elements.
 *
 * The array must contain at least one drawing operation and all coordinates
 * used by each operation must be finite. On NKUI_OK, writes the path resource
 * to `out_path`.
 */
NKUI_API nkui_result nkui_path_create(const nkui_path_element *elements NKUI_IN_ARRAY(count),
                                      uint32_t count, nkui_resource *out_path NKUI_OUT);

/** Creates a solid paint from four finite RGBA components. */
NKUI_API nkui_result nkui_paint_create_solid(nkui_color color, nkui_resource *out_paint NKUI_OUT);

/**
 * Creates an immutable linear gradient paint.
 *
 * The gradient line runs from (`start_x`, `start_y`) to (`end_x`, `end_y`)
 * in the path's user coordinate space. `stops` must contain between two and
 * NKUI_GRADIENT_MAX_STOPS entries with finite, strictly increasing offsets in
 * the inclusive range 0..1. The endpoints must differ, and each color
 * component must be finite and in the inclusive range 0..1. The colors and
 * stop array are copied on success.
 */
NKUI_API nkui_result
nkui_paint_create_linear_gradient(float start_x, float start_y, float end_x, float end_y,
                                  const nkui_gradient_stop *stops NKUI_IN_ARRAY(stop_count),
                                  uint32_t stop_count, nkui_resource *out_paint NKUI_OUT);

/**
 * Creates an image by copying a tightly packed pixel array.
 *
 * For NKUI_IMAGE_R8, `pixel_bytes` must equal width*height. For
 * NKUI_IMAGE_RGBA8, it must equal width*height*4. Width and height must be
 * positive, and on NKUI_OK the image resource is written to `out_image`.
 */
NKUI_API nkui_result nkui_image_create(uint32_t width, uint32_t height, nkui_image_format format,
                                       const uint8_t *pixels NKUI_IN_ARRAY(pixel_bytes),
                                       uint32_t pixel_bytes, nkui_resource *out_image NKUI_OUT);

/** Creates an image with an explicit scaling filter. */
NKUI_API nkui_result nkui_image_create_filtered(uint32_t width, uint32_t height,
                                                nkui_image_format format,
                                                const uint8_t *pixels NKUI_IN_ARRAY(pixel_bytes),
                                                uint32_t pixel_bytes, nkui_image_filter filter,
                                                nkui_resource *out_image NKUI_OUT);

/** Decodes a PNG, JPEG, BMP, TGA, GIF, PSD, HDR, PIC, or PNM file as RGBA8. */
NKUI_API nkui_result nkui_image_load_file(const char *path NKUI_UTF8, nkui_image_filter filter,
                                          uint32_t *out_width NKUI_OUT,
                                          uint32_t *out_height NKUI_OUT,
                                          nkui_resource *out_image NKUI_OUT);

/** Imports a sampled NativeKit graphics image as a compositable Canvas surface. */
NKUI_API nkui_result nkui_graphics_surface_create(nk_graphics_image image,
                                                  nkui_resource *out_surface NKUI_OUT);

/* ------------------------------------------------------------------------- */
/* Renderer APIs                                                             */
/* ------------------------------------------------------------------------- */

/** Creates a renderer and writes its handle to `out_renderer`. */
NKUI_API nkui_result nkui_renderer_create(nkui_renderer *out_renderer NKUI_OUT);

/**
 * Registers one native-owned custom effect implementation.
 *
 * The supplied fragment source must use the standard NativeKit effect
 * interface: effect_fs_params at fragment uniform block 1, tex_smp at image
 * slot 0, and frag_color as the output. The renderer supplies the matching
 * fullscreen vertex shader and backend pipeline. Input strings are copied by
 * the renderer; registrations are recreated after device loss.
 */
NKUI_API nkui_result nkui_renderer_register_custom_effect(
    nkui_renderer renderer, const nkui_custom_effect_registration *registration);

/** Destroys a renderer and releases its backend caches. */
NKUI_API nkui_result nkui_renderer_destroy(nkui_renderer renderer);

/** Returns preparation and geometry-cache counters for a renderer. */
NKUI_API nkui_result nkui_renderer_get_stats(nkui_renderer renderer,
                                             nkui_renderer_stats *out_stats NKUI_OUT);

/**
 * Renders a display list using the surface's current framebuffer size.
 *
 * This convenience form uses a one-to-one logical-to-physical pixel scale.
 * It makes `surface` current and executes the list, but does not present it;
 * call nk_surface_present() after NKUI_OK.
 */
NKUI_API nkui_result nkui_renderer_render(nkui_renderer renderer, nkui_display_list list,
                                          nk_surface surface);

/**
 * Renders a display list with explicit logical and framebuffer dimensions.
 *
 * `frame_info` must have a sufficient `struct_size`, positive finite logical
 * dimensions and pixel scale, and positive framebuffer dimensions. The
 * surface is made current and the list is validated and executed. Rendering
 * does not present the surface; call nk_surface_present() after NKUI_OK.
 */
NKUI_API nkui_result nkui_renderer_render_frame(nkui_renderer renderer, nkui_display_list list,
                                                nk_surface surface,
                                                const nkui_frame_info *frame_info);

/** Renders a display list over the currently presented frame without clearing it. */
NKUI_API nkui_result nkui_renderer_render_frame_overlay(nkui_renderer renderer,
                                                        nkui_display_list list, nk_surface surface,
                                                        const nkui_frame_info *frame_info);

/**
 * Releases the caller's ownership of a UI resource.
 *
 * A submitted display list keeps a referenced resource alive until that list
 * is reset, replaced, or destroyed. After this call the handle cannot be used
 * in a new command stream, even if an existing list still retains it.
 */
NKUI_API nkui_result nkui_resource_destroy(nkui_resource resource);

#ifdef __cplusplus
}
#endif

#endif
