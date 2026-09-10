#ifndef NATIVEKIT_UI_H
#define NATIVEKIT_UI_H

#include "nativekit.h"

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
#define NKUI_IN_ARRAY(count_parameter) __attribute__((annotate("hxi:in_array")))
#define NKUI_UTF8 __attribute__((annotate("hxi:utf8")))
#else
#define NKUI_OUT
#define NKUI_IN_ARRAY(count_parameter)
#define NKUI_UTF8
#endif

#ifdef __cplusplus
extern "C" {
#endif

enum { NKUI_API_VERSION = 1 };

typedef enum nkui_result {
    NKUI_OK = 0,
    NKUI_ERROR_INVALID_ARGUMENT = -1,
    NKUI_ERROR_INVALID_HANDLE = -2,
    NKUI_ERROR_INVALID_TRANSACTION = -3,
    NKUI_ERROR_OUT_OF_MEMORY = -4,
    NKUI_ERROR_RENDERING = -5
} nkui_result;

typedef struct nkui_display_list {
    uint32_t id;
} nkui_display_list;

typedef struct nkui_resource {
    uint32_t id;
} nkui_resource;

typedef struct nkui_renderer {
    uint32_t id;
} nkui_renderer;

/* Describes both the logical layout space and the physical framebuffer target. */
typedef struct nkui_frame_info {
    uint32_t struct_size;
    float logical_width;
    float logical_height;
    int32_t framebuffer_width;
    int32_t framebuffer_height;
    float pixel_scale;
} nkui_frame_info;

typedef uint16_t nkui_command_opcode;
enum {
    NKUI_COMMAND_SET_TRANSFORM = 1,
    NKUI_COMMAND_SET_PAINT = 2,
    NKUI_COMMAND_SET_GLOBAL_ALPHA = 3,
    NKUI_COMMAND_SET_COMPOSITE_MODE = 4,
    NKUI_COMMAND_PUSH_STATE = 5,
    NKUI_COMMAND_POP_STATE = 6,
    NKUI_COMMAND_CLIP_RECT = 7,
    NKUI_COMMAND_DRAW_PATH = 8,
    NKUI_COMMAND_DRAW_IMAGE = 9,
    NKUI_COMMAND_DRAW_TEXT_LAYOUT = 10,
    NKUI_COMMAND_BEGIN_LAYER = 11,
    NKUI_COMMAND_END_LAYER = 12,
    NKUI_COMMAND_DRAW_RENDER_TARGET = 13,
    NKUI_COMMAND_VERSION = 1
};

typedef enum nkui_composite_mode { NKUI_COMPOSITE_SOURCE_OVER = 1 } nkui_composite_mode;

typedef struct nkui_command_header {
    nkui_command_opcode opcode;
    uint16_t version;
    uint32_t size;
} nkui_command_header;

typedef struct nkui_transaction_info {
    uint32_t struct_size;
    uint32_t api_version;
    uint32_t command_bytes;
    uint32_t command_count;
} nkui_transaction_info;

typedef struct nkui_transform_command {
    nkui_command_header header;
    float matrix[6];
} nkui_transform_command;

typedef struct nkui_resource_command {
    nkui_command_header header;
    nkui_resource resource;
} nkui_resource_command;

typedef struct nkui_scalar_command {
    nkui_command_header header;
    float value;
} nkui_scalar_command;

typedef struct nkui_composite_command {
    nkui_command_header header;
    nkui_composite_mode mode;
} nkui_composite_command;

typedef struct nkui_rect_command {
    nkui_command_header header;
    float x;
    float y;
    float width;
    float height;
} nkui_rect_command;

typedef struct nkui_draw_rect_command {
    nkui_command_header header;
    nkui_resource resource;
    float x;
    float y;
    float width;
    float height;
} nkui_draw_rect_command;

typedef struct nkui_layer_command {
    nkui_command_header header;
    float opacity;
    nkui_composite_mode composite_mode;
} nkui_layer_command;

typedef enum nkui_font_family {
    NKUI_FONT_FAMILY_DEFAULT = 0,
    NKUI_FONT_FAMILY_EMOJI = 1
} nkui_font_family;

typedef struct nkui_text_metrics {
    uint32_t struct_size;
    float x;
    float y;
    float width;
    float height;
} nkui_text_metrics;

typedef struct nkui_text_position {
    int32_t offset;
    uint32_t affinity;
} nkui_text_position;

typedef struct nkui_text_caret {
    uint32_t struct_size;
    float x;
    float y;
    float ascender;
    float descender;
    float slope;
    uint32_t direction;
} nkui_text_caret;

typedef enum nkui_path_verb {
    NKUI_PATH_MOVE_TO = 1,
    NKUI_PATH_LINE_TO = 2,
    NKUI_PATH_BEZIER_TO = 3,
    NKUI_PATH_QUADRATIC_TO = 4,
    NKUI_PATH_ARC_TO = 5,
    NKUI_PATH_CLOSE = 6
} nkui_path_verb;

typedef struct nkui_path_element {
    nkui_path_verb verb;
    float values[6];
} nkui_path_element;

typedef struct nkui_color {
    float red;
    float green;
    float blue;
    float alpha;
} nkui_color;

typedef enum nkui_image_format {
    NKUI_IMAGE_FORMAT_INVALID = 0,
    NKUI_IMAGE_R8 = 1,
    NKUI_IMAGE_RGBA8 = 2
} nkui_image_format;

/* The initial stable seam while the retained-tree transaction ABI is designed. */
NKUI_API uint32_t nkui_api_version(void);
NKUI_API nkui_result nkui_display_list_create(nkui_display_list *out_list NKUI_OUT);
NKUI_API nkui_result nkui_display_list_destroy(nkui_display_list list);
NKUI_API nkui_result nkui_display_list_reset(nkui_display_list list);
/* Validates the complete byte stream before atomically replacing the retained list. */
NKUI_API nkui_result nkui_display_list_submit(nkui_display_list list,
                                              const uint8_t *commands NKUI_IN_ARRAY(command_bytes),
                                              uint32_t command_bytes);
NKUI_API nkui_result nkui_display_list_get_info(nkui_display_list list,
                                                nkui_transaction_info *out_info NKUI_OUT);
NKUI_API nkui_result nkui_font_collection_create(nkui_resource *out_fonts NKUI_OUT);
NKUI_API nkui_result nkui_font_collection_add(nkui_resource fonts, const char *path NKUI_UTF8,
                                              nkui_font_family family);
NKUI_API nkui_result nkui_text_layout_create(nkui_resource fonts, const char *text NKUI_UTF8,
                                             float width, float font_size,
                                             nkui_resource *out_layout NKUI_OUT);
NKUI_API nkui_result nkui_text_layout_measure(nkui_resource layout,
                                              nkui_text_metrics *out_metrics NKUI_OUT);
NKUI_API nkui_result nkui_text_layout_hit_test(nkui_resource layout, float x, float y,
                                               nkui_text_position *out_position NKUI_OUT);
NKUI_API nkui_result nkui_text_layout_caret(nkui_resource layout, nkui_text_position position,
                                            nkui_text_caret *out_caret NKUI_OUT);
NKUI_API nkui_result nkui_path_create(const nkui_path_element *elements NKUI_IN_ARRAY(count),
                                      uint32_t count, nkui_resource *out_path NKUI_OUT);
NKUI_API nkui_result nkui_paint_create_solid(nkui_color color, nkui_resource *out_paint NKUI_OUT);
NKUI_API nkui_result nkui_image_create(uint32_t width, uint32_t height, nkui_image_format format,
                                       const uint8_t *pixels NKUI_IN_ARRAY(pixel_bytes),
                                       uint32_t pixel_bytes, nkui_resource *out_image NKUI_OUT);
NKUI_API nkui_result nkui_renderer_create(nkui_renderer *out_renderer NKUI_OUT);
NKUI_API nkui_result nkui_renderer_destroy(nkui_renderer renderer);
/* Makes the NativeKit surface current and renders one frame. Presentation remains explicit. */
NKUI_API nkui_result nkui_renderer_render(nkui_renderer renderer, nkui_display_list list,
                                          nk_handle surface);
/* Renders with explicit logical and framebuffer dimensions. Presentation remains explicit. */
NKUI_API nkui_result nkui_renderer_render_frame(nkui_renderer renderer, nkui_display_list list,
                                                nk_handle surface,
                                                const nkui_frame_info *frame_info);
NKUI_API nkui_result nkui_resource_destroy(nkui_resource resource);

#ifdef __cplusplus
}
#endif

#endif
