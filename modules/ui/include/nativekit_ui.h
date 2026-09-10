#ifndef NATIVEKIT_UI_H
#define NATIVEKIT_UI_H

#include <stdint.h>

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
#else
#define NKUI_OUT
#define NKUI_IN_ARRAY(count_parameter)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NKUI_API_VERSION UINT32_C(1)

typedef int32_t nkui_result;
#define NKUI_OK INT32_C(0)
#define NKUI_ERROR_INVALID_ARGUMENT INT32_C(-1)
#define NKUI_ERROR_INVALID_HANDLE INT32_C(-2)
#define NKUI_ERROR_INVALID_TRANSACTION INT32_C(-3)
#define NKUI_ERROR_OUT_OF_MEMORY INT32_C(-4)

typedef struct nkui_display_list {
    uint32_t id;
} nkui_display_list;

typedef struct nkui_resource {
    uint32_t id;
} nkui_resource;

typedef uint16_t nkui_command_opcode;
#define NKUI_COMMAND_SET_TRANSFORM UINT16_C(1)
#define NKUI_COMMAND_SET_PAINT UINT16_C(2)
#define NKUI_COMMAND_SET_GLOBAL_ALPHA UINT16_C(3)
#define NKUI_COMMAND_SET_COMPOSITE_MODE UINT16_C(4)
#define NKUI_COMMAND_PUSH_STATE UINT16_C(5)
#define NKUI_COMMAND_POP_STATE UINT16_C(6)
#define NKUI_COMMAND_CLIP_RECT UINT16_C(7)
#define NKUI_COMMAND_DRAW_PATH UINT16_C(8)
#define NKUI_COMMAND_DRAW_IMAGE UINT16_C(9)
#define NKUI_COMMAND_DRAW_TEXT_LAYOUT UINT16_C(10)
#define NKUI_COMMAND_BEGIN_LAYER UINT16_C(11)
#define NKUI_COMMAND_END_LAYER UINT16_C(12)
#define NKUI_COMMAND_DRAW_RENDER_TARGET UINT16_C(13)

#define NKUI_COMMAND_VERSION UINT16_C(1)

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
    uint32_t mode;
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
    uint32_t composite_mode;
} nkui_layer_command;

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

#ifdef __cplusplus
}
#endif

#endif
