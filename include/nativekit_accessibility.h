#ifndef NATIVEKIT_ACCESSIBILITY_H
#define NATIVEKIT_ACCESSIBILITY_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nk_accessibility_node_id;
enum { NK_ACCESSIBILITY_ROOT = 0 };

typedef uint32_t nk_accessibility_role;
enum {
    NK_ACCESSIBILITY_GROUP = 0,
    NK_ACCESSIBILITY_BUTTON = 1,
    NK_ACCESSIBILITY_CHECKBOX = 2,
    NK_ACCESSIBILITY_RADIO = 3,
    NK_ACCESSIBILITY_TEXT = 4,
    NK_ACCESSIBILITY_TEXT_FIELD = 5,
    NK_ACCESSIBILITY_LINK = 6,
    NK_ACCESSIBILITY_IMAGE = 7,
    NK_ACCESSIBILITY_HEADING = 8,
    NK_ACCESSIBILITY_LIST = 9,
    NK_ACCESSIBILITY_LIST_ITEM = 10,
    NK_ACCESSIBILITY_SLIDER = 11,
    NK_ACCESSIBILITY_SCROLL_AREA = 12
};

typedef uint32_t nk_accessibility_states;
enum {
    NK_ACCESSIBILITY_FOCUSABLE = 1u << 0,
    NK_ACCESSIBILITY_FOCUSED = 1u << 1,
    NK_ACCESSIBILITY_SELECTED = 1u << 2,
    NK_ACCESSIBILITY_CHECKED = 1u << 3,
    NK_ACCESSIBILITY_DISABLED = 1u << 4,
    NK_ACCESSIBILITY_READ_ONLY = 1u << 5,
    NK_ACCESSIBILITY_MULTILINE = 1u << 6,
    NK_ACCESSIBILITY_PASSWORD = 1u << 7,
    NK_ACCESSIBILITY_EXPANDED = 1u << 8
};

typedef uint32_t nk_accessibility_actions;
enum {
    NK_ACCESSIBILITY_CAN_ACTIVATE = 1u << 0,
    NK_ACCESSIBILITY_CAN_FOCUS = 1u << 1,
    NK_ACCESSIBILITY_CAN_SET_VALUE = 1u << 2,
    NK_ACCESSIBILITY_CAN_SET_SELECTION = 1u << 3,
    NK_ACCESSIBILITY_CAN_INCREMENT = 1u << 4,
    NK_ACCESSIBILITY_CAN_DECREMENT = 1u << 5,
    NK_ACCESSIBILITY_CAN_SCROLL_FORWARD = 1u << 6,
    NK_ACCESSIBILITY_CAN_SCROLL_BACKWARD = 1u << 7
};

typedef uint32_t nk_accessibility_action;
enum {
    NK_ACCESSIBILITY_ACTION_ACTIVATE = 1,
    NK_ACCESSIBILITY_ACTION_FOCUS = 2,
    NK_ACCESSIBILITY_ACTION_CLEAR_FOCUS = 3,
    NK_ACCESSIBILITY_ACTION_SET_VALUE = 4,
    NK_ACCESSIBILITY_ACTION_SET_SELECTION = 5,
    NK_ACCESSIBILITY_ACTION_INCREMENT = 6,
    NK_ACCESSIBILITY_ACTION_DECREMENT = 7,
    NK_ACCESSIBILITY_ACTION_SCROLL_FORWARD = 8,
    NK_ACCESSIBILITY_ACTION_SCROLL_BACKWARD = 9
};

typedef uint32_t nk_accessibility_text_position;
enum { NK_ACCESSIBILITY_TEXT_POSITION_NONE = 0xffffffffu };

typedef struct nk_accessibility_node {
    uint32_t struct_size;
    nk_accessibility_node_id id;
    nk_accessibility_node_id parent_id;
    uint32_t child_index;
    nk_accessibility_role role;
    nk_accessibility_states states;
    nk_accessibility_actions actions;
    uint32_t reserved0;
    float x;
    float y;
    float width;
    float height;
    const char *label NK_NULLABLE_UTF8;
    const char *value NK_NULLABLE_UTF8;
    double numeric_value;
    double numeric_minimum;
    double numeric_maximum;
    uint64_t reserved[2];
} nk_accessibility_node;

/* Selection positions are Unicode code-point offsets in the node's value. */
typedef struct nk_accessibility_action_event {
    nk_accessibility_node_id node_id;
    nk_accessibility_action action;
    uint32_t value_offset;
    uint32_t value_length;
    nk_accessibility_text_position selection_start;
    nk_accessibility_text_position selection_end;
    uint32_t reserved[2];
} nk_accessibility_action_event;

/* Copies and incrementally inserts or replaces one virtual semantic node. */
NK_API nk_result NK_CALL nk_surface_accessibility_set_node(
    nk_handle surface, const nk_accessibility_node *node);
/* Removes a node and its descendants. Root cannot be removed. */
NK_API nk_result NK_CALL nk_surface_accessibility_remove_node(
    nk_handle surface, nk_accessibility_node_id node);
/* Removes all virtual nodes and disables the semantic tree. */
NK_API nk_result NK_CALL nk_surface_accessibility_clear(nk_handle surface);
/* Moves accessibility focus; root clears virtual focus. */
NK_API nk_result NK_CALL nk_surface_accessibility_set_focus(
    nk_handle surface, nk_accessibility_node_id node);

/* Returns a borrowed UTF-8 view valid until nk_event_release(). */
NK_API nk_result NK_CALL nk_accessibility_action_event_value(
    const nk_event *event, const char **out_value, uint32_t *out_length);

#ifdef __cplusplus
}
#endif

#endif
