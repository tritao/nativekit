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
#if defined(NKUI_BUILDING_LIBRARY)
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
#define NKUI_IN_ARRAY(count_parameter) __attribute__((annotate("hxi:in_array")))
#define NKUI_UTF8 __attribute__((annotate("hxi:utf8")))
#else
#define NKUI_OUT
#define NKUI_INOUT
#define NKUI_IN_ARRAY(count_parameter)
#define NKUI_UTF8
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
    NKUI_API_VERSION = 2
};

/** Result returned by a NativeKit UI operation. */
typedef enum nkui_result {
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
} nkui_result;

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
    uint32_t struct_size;
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
    uint32_t struct_size;
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
} nkui_renderer_stats;

/** 16-bit opcode identifying one display-list command record. */
typedef uint16_t nkui_command_opcode;

/** Display-list command opcode and record version constants. */
enum {
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
    /** Version value required in every command header. */
    NKUI_COMMAND_VERSION = 1
};

/** Alpha compositing mode supported by the current UI renderer. */
typedef enum nkui_composite_mode {
    /** Draw new content over the existing destination. */
    NKUI_COMPOSITE_SOURCE_OVER = 1
} nkui_composite_mode;

/** Fixed header present at the start of every display-list command record. */
typedef struct nkui_command_header {
    /** One of the NKUI_COMMAND_* opcode values. */
    nkui_command_opcode opcode;
    /** Must be NKUI_COMMAND_VERSION. */
    uint16_t version;
    /** Total record size in bytes, including this header; must be 4-byte aligned. */
    uint32_t size;
} nkui_command_header;

/** Summary of the command stream currently stored in a display list. */
typedef struct nkui_transaction_info {
    /** Set to sizeof(nkui_transaction_info) when returned by the API. */
    uint32_t struct_size;
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
typedef enum nkui_path_line_cap {
    /** Stop the stroke at the endpoint. */
    NKUI_PATH_LINE_CAP_BUTT = 0,
    /** Add a semicircular cap at the endpoint. */
    NKUI_PATH_LINE_CAP_ROUND = 1,
    /** Extend the stroke by half its width at the endpoint. */
    NKUI_PATH_LINE_CAP_SQUARE = 2
} nkui_path_line_cap;

/** Join style used where two stroked segments meet. */
typedef enum nkui_path_line_join {
    /** Join segments with a circular arc. */
    NKUI_PATH_LINE_JOIN_ROUND = 1,
    /** Join segments with a clipped corner. */
    NKUI_PATH_LINE_JOIN_BEVEL = 3,
    /** Extend the outer edges to their intersection, subject to the miter limit. */
    NKUI_PATH_LINE_JOIN_MITER = 4
} nkui_path_line_join;

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

/** Payload for NKUI_COMMAND_BEGIN_LAYER. */
typedef struct nkui_layer_command {
    /** Command record header. */
    nkui_command_header header;
    /** Layer opacity in the inclusive range 0..1. Values below 1 isolate the layer. */
    float opacity;
    /** Compositing mode used when the layer is applied. */
    nkui_composite_mode composite_mode;
} nkui_layer_command;

/* ------------------------------------------------------------------------- */
/* Text and layout types                                                      */
/* ------------------------------------------------------------------------- */

/** Font role used when adding a font file to a collection. */
typedef enum nkui_font_family {
    /** General-purpose text font. */
    NKUI_FONT_FAMILY_DEFAULT = 0,
    /** Font intended to provide emoji glyphs. */
    NKUI_FONT_FAMILY_EMOJI = 1
} nkui_font_family;

/** Word-breaking policy used by both explicit and automatic text layouts. */
typedef enum nkui_text_wrap {
    /** Do not wrap the paragraph. */
    NKUI_TEXT_WRAP_NONE = 0,
    /** Wrap at word boundaries. */
    NKUI_TEXT_WRAP_WORD = 1,
    /** Wrap at word or character boundaries. */
    NKUI_TEXT_WRAP_WORD_CHARACTER = 2
} nkui_text_wrap;

/** Horizontal alignment of paragraph lines. */
typedef enum nkui_text_alignment {
    NKUI_TEXT_ALIGN_START = 0,
    NKUI_TEXT_ALIGN_CENTER = 1,
    NKUI_TEXT_ALIGN_END = 2
} nkui_text_alignment;

/** Base direction requested for a paragraph. */
typedef enum nkui_text_direction {
    NKUI_TEXT_DIRECTION_AUTO = 0,
    NKUI_TEXT_DIRECTION_LTR = 1,
    NKUI_TEXT_DIRECTION_RTL = 2
} nkui_text_direction;

/** Font and inline spacing inputs shared by explicit and tree-owned layouts. */
typedef struct nkui_text_style {
    /** Set to sizeof(nkui_text_style) or a larger compatible structure size. */
    uint32_t struct_size;
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
    uint32_t struct_size;
    /** Explicit line height, or zero for the natural line height. */
    float line_height;
    nkui_text_wrap wrap;
    nkui_text_alignment alignment;
    nkui_text_direction direction;
} nkui_paragraph_style;

/** Bounding rectangle returned for a text layout. */
typedef struct nkui_text_metrics {
    /** Set to sizeof(nkui_text_metrics) when returned by the API. */
    uint32_t struct_size;
    /** Left edge of the bounds in layout coordinates. */
    float x;
    /** Top edge of the bounds in layout coordinates. */
    float y;
    /** Width of the laid-out text. */
    float width;
    /** Height of the laid-out text. */
    float height;
} nkui_text_metrics;

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
    uint32_t struct_size;
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
    uint32_t direction;
} nkui_text_caret;

/* ------------------------------------------------------------------------- */
/* Path, color, and image types                                              */
/* ------------------------------------------------------------------------- */

/** Geometry operation used by one element of a path. */
typedef enum nkui_path_verb {
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
} nkui_path_verb;

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

/** Pixel format accepted by nkui_image_create(). */
typedef enum nkui_image_format {
    /** Invalid format; cannot be used to create an image. */
    NKUI_IMAGE_FORMAT_INVALID = 0,
    /** One byte per pixel; rendered as white with the byte used as alpha. */
    NKUI_IMAGE_R8 = 1,
    /** Four bytes per pixel in red, green, blue, alpha order. */
    NKUI_IMAGE_RGBA8 = 2
} nkui_image_format;

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
NKUI_API nkui_result nkui_font_collection_add_data(
    nkui_resource fonts, const char *name NKUI_UTF8,
    const uint8_t *font_data NKUI_IN_ARRAY(font_bytes), uint32_t font_bytes,
    nkui_font_family family);

/** Enables best-effort platform system-font fallback discovery for new layouts. */
NKUI_API nkui_result nkui_font_collection_add_system_fallbacks(nkui_resource fonts);

/**
 * Shapes and lays out a UTF-8 string within a maximum width.
 *
 * `fonts` must be a valid collection containing at least one font. `width` and
 * `font_size` are positive logical-pixel values. On NKUI_OK, writes a text
 * layout resource to `out_layout`; the input string is consumed while the
 * layout is built.
 */
NKUI_API nkui_result nkui_text_layout_create(nkui_resource fonts, const char *text NKUI_UTF8,
                                             float width, float font_size,
                                             nkui_resource *out_layout NKUI_OUT);

/** Creates a text layout using the shared semantic text and paragraph styles. */
NKUI_API nkui_result nkui_text_layout_create_styled(
    nkui_resource fonts, const char *text NKUI_UTF8, float width,
    const nkui_text_style *text_style, const nkui_paragraph_style *paragraph_style,
    nkui_resource *out_layout NKUI_OUT);

/** Re-shapes an existing layout while retaining its native resource handle. */
NKUI_API nkui_result nkui_text_layout_update(
    nkui_resource layout, const char *text NKUI_UTF8, float width,
    const nkui_text_style *text_style, const nkui_paragraph_style *paragraph_style);

/** Re-shapes an existing layout with new UTF-8 text while retaining its handle and style. */
NKUI_API nkui_result nkui_text_layout_set_text(nkui_resource layout, const char *text NKUI_UTF8);

/** Returns the layout bounds in `out_metrics`. */
NKUI_API nkui_result nkui_text_layout_measure(nkui_resource layout,
                                              nkui_text_metrics *out_metrics NKUI_OUT);

/**
 * Converts a logical-space point into the nearest text caret position.
 *
 * On NKUI_OK, writes a code-point offset and affinity to `out_position`.
 * Coordinates are relative to the text layout's origin; pass the returned
 * position to nkui_text_layout_caret() when its visual geometry is needed.
 */
NKUI_API nkui_result nkui_text_layout_hit_test(nkui_resource layout, float x, float y,
                                               nkui_text_position *out_position NKUI_OUT);

/**
 * Returns visual caret geometry for a text position.
 *
 * `position` should normally be a value returned by
 * nkui_text_layout_hit_test(). On NKUI_OK, writes the caret geometry to
 * `out_caret`.
 */
NKUI_API nkui_result nkui_text_layout_caret(nkui_resource layout, nkui_text_position position,
                                            nkui_text_caret *out_caret NKUI_OUT);

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
 * Creates an image by copying a tightly packed pixel array.
 *
 * For NKUI_IMAGE_R8, `pixel_bytes` must equal width*height. For
 * NKUI_IMAGE_RGBA8, it must equal width*height*4. Width and height must be
 * positive, and on NKUI_OK the image resource is written to `out_image`.
 */
NKUI_API nkui_result nkui_image_create(uint32_t width, uint32_t height, nkui_image_format format,
                                       const uint8_t *pixels NKUI_IN_ARRAY(pixel_bytes),
                                       uint32_t pixel_bytes, nkui_resource *out_image NKUI_OUT);

/** Imports a sampled NativeKit graphics image as a compositable Canvas surface. */
NKUI_API nkui_result nkui_graphics_surface_create(nk_graphics_image image,
                                                  nkui_resource *out_surface NKUI_OUT);

/* ------------------------------------------------------------------------- */
/* Renderer APIs                                                             */
/* ------------------------------------------------------------------------- */

/** Creates a renderer and writes its handle to `out_renderer`. */
NKUI_API nkui_result nkui_renderer_create(nkui_renderer *out_renderer NKUI_OUT);

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
                                          nk_handle surface);

/**
 * Renders a display list with explicit logical and framebuffer dimensions.
 *
 * `frame_info` must have a sufficient `struct_size`, positive finite logical
 * dimensions and pixel scale, and positive framebuffer dimensions. The
 * surface is made current and the list is validated and executed. Rendering
 * does not present the surface; call nk_surface_present() after NKUI_OK.
 */
NKUI_API nkui_result nkui_renderer_render_frame(nkui_renderer renderer, nkui_display_list list,
                                                nk_handle surface,
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
