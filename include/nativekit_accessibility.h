#ifndef NATIVEKIT_ACCESSIBILITY_H
#define NATIVEKIT_ACCESSIBILITY_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit.h"

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NativeKit's virtual accessibility-tree API.
 *
 * Use a graphics surface's accessibility functions to expose application
 * semantics to the platform screen reader. The tree is owned by the caller;
 * NativeKit copies node data and strings during each call. All tree updates
 * are UI-thread-only and require NK_CAP_ACCESSIBILITY on the active backend.
 * Node geometry uses surface-local logical pixels. Text offsets use Unicode
 * code points, while event payload byte lengths are UTF-8 byte counts.
 */

/* ------------------------------------------------------------------------- */
/* Accessibility identifiers and enums                                       */
/* ------------------------------------------------------------------------- */

/** Positive identifier for a caller-owned virtual accessibility node. */
typedef uint32_t nk_accessibility_node_id;
enum {
    /** The platform-provided host root; it cannot be removed or replaced. */
    NK_ACCESSIBILITY_ROOT = 0
};

/** Semantic role announced for an accessibility node. */
typedef uint32_t nk_accessibility_role;
enum NK_ENUM(nk_accessibility_role) {
    /** Generic grouping node. */
    NK_ACCESSIBILITY_GROUP = 0,
    /** Activatable button. */
    NK_ACCESSIBILITY_BUTTON = 1,
    /** Checkable toggle control. */
    NK_ACCESSIBILITY_CHECKBOX = 2,
    /** Mutually exclusive selectable control. */
    NK_ACCESSIBILITY_RADIO = 3,
    /** Static text content. */
    NK_ACCESSIBILITY_TEXT = 4,
    /** Editable text control. */
    NK_ACCESSIBILITY_TEXT_FIELD = 5,
    /** Navigable hyperlink. */
    NK_ACCESSIBILITY_LINK = 6,
    /** Informational image. */
    NK_ACCESSIBILITY_IMAGE = 7,
    /** Heading in the semantic document. */
    NK_ACCESSIBILITY_HEADING = 8,
    /** Container representing a list. */
    NK_ACCESSIBILITY_LIST = 9,
    /** Item inside a list. */
    NK_ACCESSIBILITY_LIST_ITEM = 10,
    /** Numeric or ranged value control. */
    NK_ACCESSIBILITY_SLIDER = 11,
    /** Scrollable content container. */
    NK_ACCESSIBILITY_SCROLL_AREA = 12
};

/** Bit flags describing the current state of an accessibility node. */
typedef uint32_t nk_accessibility_states;
enum NK_FLAGS(nk_accessibility_states) {
    /** The node can receive accessibility focus. */
    NK_ACCESSIBILITY_FOCUSABLE = 1u << 0,
    /** The node currently has accessibility focus. */
    NK_ACCESSIBILITY_FOCUSED = 1u << 1,
    /** The node is selected. */
    NK_ACCESSIBILITY_SELECTED = 1u << 2,
    /** A checkable node is checked. */
    NK_ACCESSIBILITY_CHECKED = 1u << 3,
    /** The node is unavailable for interaction. */
    NK_ACCESSIBILITY_DISABLED = 1u << 4,
    /** The node's value cannot be edited. */
    NK_ACCESSIBILITY_READ_ONLY = 1u << 5,
    /** The node's value can contain multiple lines. */
    NK_ACCESSIBILITY_MULTILINE = 1u << 6,
    /** The node's value should be announced as protected content. */
    NK_ACCESSIBILITY_PASSWORD = 1u << 7,
    /** A node such as a group or tree item is expanded. */
    NK_ACCESSIBILITY_EXPANDED = 1u << 8
};

/** Bit flags describing actions supported by an accessibility node. */
typedef uint32_t nk_accessibility_actions;
enum NK_FLAGS(nk_accessibility_actions) {
    /** The node accepts NK_ACCESSIBILITY_ACTION_ACTIVATE. */
    NK_ACCESSIBILITY_CAN_ACTIVATE = 1u << 0,
    /** The node accepts focus and clear-focus actions. */
    NK_ACCESSIBILITY_CAN_FOCUS = 1u << 1,
    /** The node accepts a new textual or numeric value. */
    NK_ACCESSIBILITY_CAN_SET_VALUE = 1u << 2,
    /** The node accepts a new text selection. */
    NK_ACCESSIBILITY_CAN_SET_SELECTION = 1u << 3,
    /** The node accepts an increment action. */
    NK_ACCESSIBILITY_CAN_INCREMENT = 1u << 4,
    /** The node accepts a decrement action. */
    NK_ACCESSIBILITY_CAN_DECREMENT = 1u << 5,
    /** The node accepts forward scrolling. */
    NK_ACCESSIBILITY_CAN_SCROLL_FORWARD = 1u << 6,
    /** The node accepts backward scrolling. */
    NK_ACCESSIBILITY_CAN_SCROLL_BACKWARD = 1u << 7,
    /** The node accepts moving to the next semantic item. */
    NK_ACCESSIBILITY_CAN_MOVE_NEXT = 1u << 8,
    /** The node accepts moving to the previous semantic item. */
    NK_ACCESSIBILITY_CAN_MOVE_PREVIOUS = 1u << 9
};

/** Action requested by a platform accessibility service. */
typedef uint32_t nk_accessibility_action;
enum NK_ENUM(nk_accessibility_action) {
    /** Activate the node. */
    NK_ACCESSIBILITY_ACTION_ACTIVATE = 1,
    /** Move accessibility focus to the node. */
    NK_ACCESSIBILITY_ACTION_FOCUS = 2,
    /** Clear accessibility focus from the node. */
    NK_ACCESSIBILITY_ACTION_CLEAR_FOCUS = 3,
    /** Set the node's value from the event's optional UTF-8 text. */
    NK_ACCESSIBILITY_ACTION_SET_VALUE = 4,
    /** Set the node's selection from the event positions. */
    NK_ACCESSIBILITY_ACTION_SET_SELECTION = 5,
    /** Increase the node's value. */
    NK_ACCESSIBILITY_ACTION_INCREMENT = 6,
    /** Decrease the node's value. */
    NK_ACCESSIBILITY_ACTION_DECREMENT = 7,
    /** Scroll the node forward. */
    NK_ACCESSIBILITY_ACTION_SCROLL_FORWARD = 8,
    /** Scroll the node backward. */
    NK_ACCESSIBILITY_ACTION_SCROLL_BACKWARD = 9,
    /** Move to the next semantic item. */
    NK_ACCESSIBILITY_ACTION_MOVE_NEXT = 10,
    /** Move to the previous semantic item. */
    NK_ACCESSIBILITY_ACTION_MOVE_PREVIOUS = 11
};

/** Text unit requested for a selection or navigation action. */
typedef uint32_t nk_accessibility_text_granularity;
enum NK_ENUM(nk_accessibility_text_granularity) {
    /** One Unicode code point. */
    NK_ACCESSIBILITY_GRANULARITY_CHARACTER = 1,
    /** One word. */
    NK_ACCESSIBILITY_GRANULARITY_WORD = 2,
    /** One line. */
    NK_ACCESSIBILITY_GRANULARITY_LINE = 3,
    /** One paragraph. */
    NK_ACCESSIBILITY_GRANULARITY_PARAGRAPH = 4,
    /** One page. */
    NK_ACCESSIBILITY_GRANULARITY_PAGE = 5
};

/** Unicode code-point offset used by accessibility text APIs. */
typedef uint32_t nk_accessibility_text_position;
enum {
    /** Sentinel meaning that no text position or selection is supplied. */
    NK_ACCESSIBILITY_TEXT_POSITION_NONE = 0xffffffffu
};

/* ------------------------------------------------------------------------- */
/* Accessibility data structures                                             */
/* ------------------------------------------------------------------------- */

/**
 * One virtual node in a surface's accessibility tree.
 *
 * Set `struct_size` before passing the node. IDs must be positive and stable
 * for the lifetime of the semantic item. A node's parent must already exist;
 * use `child_index` to define its order among siblings. The label and value
 * strings are nullable UTF-8 and are copied during the update.
 */
typedef struct nk_accessibility_node {
    /** Set to sizeof(nk_accessibility_node) or a larger compatible size. */
    uint32_t struct_size;
    /** Stable positive ID chosen by the application. */
    nk_accessibility_node_id id;
    /** Parent ID, or NK_ACCESSIBILITY_ROOT for a top-level node. */
    nk_accessibility_node_id parent_id;
    /** Zero-based order among the parent's children. */
    uint32_t child_index;
    /** Role announced to the platform accessibility service. */
    nk_accessibility_role role;
    /** Current NK_ACCESSIBILITY_* state flags. */
    nk_accessibility_states states;
    /** Supported NK_ACCESSIBILITY_CAN_* action flags. */
    nk_accessibility_actions actions;
    /** Reserved; set to zero. */
    uint32_t reserved0;
    /** Left edge in surface-local logical pixels. */
    float x;
    /** Top edge in surface-local logical pixels. */
    float y;
    /** Node width in logical pixels; must be non-negative. */
    float width;
    /** Node height in logical pixels; must be non-negative. */
    float height;
    /** Human-facing accessible name; nullable UTF-8 copied during the call. */
    const char *label NK_NULLABLE_UTF8;
    /** Optional textual value; nullable UTF-8 copied during the call. */
    const char *value NK_NULLABLE_UTF8;
    /** Current numeric value, used especially by slider roles. */
    double numeric_value;
    /** Minimum numeric value for a ranged role. */
    double numeric_minimum;
    /** Maximum numeric value for a ranged role. */
    double numeric_maximum;
    /** Code-point offset where `value` begins in the complete document. */
    nk_accessibility_text_position text_start;
    /** Total code-point length of the complete document. */
    nk_accessibility_text_position document_length;
    /** Selection start in document coordinates, or NONE for no selection. */
    nk_accessibility_text_position selection_start;
    /** Selection end in document coordinates, or NONE for no selection. */
    nk_accessibility_text_position selection_end;
} nk_accessibility_node;

/**
 * Payload of NK_EVENT_ACCESSIBILITY_ACTION.
 *
 * Selection positions are absolute Unicode code-point offsets in the
 * document. Use nk_accessibility_action_event_value() to safely obtain the
 * optional UTF-8 value associated with the action.
 */
typedef struct nk_accessibility_action_event {
    /** ID of the node that received the platform action. */
    nk_accessibility_node_id node_id;
    /** Requested accessibility action. */
    nk_accessibility_action action;
    /** Byte offset of the optional NUL-terminated UTF-8 value in event data. */
    uint32_t value_offset;
    /** UTF-8 value length in bytes, excluding its NUL terminator. */
    uint32_t value_length;
    /** Selection start supplied by the platform, or NONE. */
    nk_accessibility_text_position selection_start;
    /** Selection end supplied by the platform, or NONE. */
    nk_accessibility_text_position selection_end;
    /** Requested unit for navigation or selection. */
    nk_accessibility_text_granularity granularity;
    /** Reserved; set to zero. */
    uint32_t reserved;
} nk_accessibility_action_event;

/** Flags selecting optional parts of an accessibility tree update. */
typedef uint32_t nk_accessibility_update_flags;
enum NK_FLAGS(nk_accessibility_update_flags) {
    /** Apply the `focus` field as part of the same atomic update. */
    NK_ACCESSIBILITY_UPDATE_FOCUS = 1u << 0
};

/**
 * Atomic batch of accessibility node replacements, removals, and focus.
 *
 * Arrays are borrowed for the duration of the call and copied on success.
 * Removed nodes are removed recursively. Replacement nodes are processed in
 * array order, so parents must precede their children. Invalid batches leave
 * the existing tree unchanged.
 */
typedef struct nk_accessibility_update {
    /** Set to sizeof(nk_accessibility_update) or a larger compatible size. */
    uint32_t struct_size;
    /** Optional NK_ACCESSIBILITY_UPDATE_* flags. */
    nk_accessibility_update_flags flags;
    /** Nodes to insert or replace; borrowed during the call. */
    const nk_accessibility_node *nodes NK_BORROWED_ARRAY(node_count);
    /** Number of entries in `nodes`. */
    uint32_t node_count;
    /** Node IDs to remove recursively; borrowed during the call. */
    const nk_accessibility_node_id *removed_nodes NK_IN_ARRAY(removed_node_count);
    /** Number of entries in `removed_nodes`. */
    uint32_t removed_node_count;
    /** Node to focus when NK_ACCESSIBILITY_UPDATE_FOCUS is set; root clears focus. */
    nk_accessibility_node_id focus;
    /** Reserved; set to zero. */
    uint32_t reserved;
} nk_accessibility_update;

/** Screen-reader geometry for one non-empty text range in a node's value. */
typedef struct nk_accessibility_text_range {
    /** Inclusive start code-point offset in the complete document. */
    nk_accessibility_text_position start;
    /** Exclusive end code-point offset in the complete document. */
    nk_accessibility_text_position end;
    /** Left edge in surface-local logical pixels. */
    float x;
    /** Top edge in surface-local logical pixels. */
    float y;
    /** Range width in logical pixels; must be non-negative. */
    float width;
    /** Range height in logical pixels; must be non-negative. */
    float height;
} nk_accessibility_text_range;

/* ------------------------------------------------------------------------- */
/* Accessibility tree and text APIs                                          */
/* ------------------------------------------------------------------------- */

/**
 * Inserts or replaces one virtual semantic node.
 *
 * `node` is copied during the call. The node ID must be positive, its parent
 * must already exist (or be NK_ACCESSIBILITY_ROOT), and its text positions
 * must describe the supplied value window. Requires the UI thread and
 * NK_CAP_ACCESSIBILITY.
 */
NK_API nk_result NK_CALL nk_surface_accessibility_set_node(nk_handle surface,
                                                           const nk_accessibility_node *node);

/**
 * Removes a virtual node and all of its descendants.
 *
 * `node` must identify an existing non-root node. The operation runs on the UI
 * thread and requires NK_CAP_ACCESSIBILITY.
 */
NK_API nk_result NK_CALL nk_surface_accessibility_remove_node(nk_handle surface,
                                                              nk_accessibility_node_id node);

/** Removes all virtual nodes from a surface's accessibility tree. */
NK_API nk_result NK_CALL nk_surface_accessibility_clear(nk_handle surface);

/**
 * Moves accessibility focus to a virtual node.
 *
 * Pass NK_ACCESSIBILITY_ROOT to clear virtual focus. Any non-root node must
 * already exist in the surface's tree.
 */
NK_API nk_result NK_CALL nk_surface_accessibility_set_focus(nk_handle surface,
                                                            nk_accessibility_node_id node);

/**
 * Applies node replacements, recursive removals, and optional focus atomically.
 *
 * The update and its arrays are borrowed during the call. Removals are applied
 * first, then replacement nodes in array order; a replacement parent must
 * therefore already exist or precede its children in `nodes`. Set
 * NK_ACCESSIBILITY_UPDATE_FOCUS to apply `focus`, where ROOT clears focus.
 * Invalid input leaves the previous tree unchanged.
 */
NK_API nk_result NK_CALL nk_surface_accessibility_update(nk_handle surface,
                                                         const nk_accessibility_update *update);

/**
 * Supplies screen-reader geometry for text ranges in a node's value.
 *
 * Ranges are borrowed during the call and copied on success. They must be
 * ordered, non-overlapping, non-empty, and use document code-point offsets.
 * Pass a zero count to clear the node's current range geometry.
 */
NK_API nk_result NK_CALL nk_surface_accessibility_set_text_ranges(
    nk_handle surface, nk_accessibility_node_id node,
    const nk_accessibility_text_range *ranges NK_BORROWED_ARRAY(range_count), uint32_t range_count);

/* ------------------------------------------------------------------------- */
/* Accessibility event helpers                                               */
/* ------------------------------------------------------------------------- */

/**
 * Reads the optional UTF-8 value from an accessibility action event.
 *
 * The event must be NK_EVENT_ACCESSIBILITY_ACTION. On NK_OK, `out_value`
 * points into the event data and remains valid until nk_event_release(); do
 * not free or retain it. `out_length` receives the UTF-8 byte length excluding
 * the NUL terminator. For actions without a value, the pointer is NULL and
 * the length is zero.
 */
NK_API nk_result NK_CALL nk_accessibility_action_event_value(const nk_event *event,
                                                             const char **out_value,
                                                             uint32_t *out_length);

#ifdef __cplusplus
}
#endif

#endif
