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
    NK_ACCESSIBILITY_ROOT = 0,
    /** Sentinel for an omitted row or column index. */
    NK_ACCESSIBILITY_INDEX_NONE = 0xffffffffu
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
    NK_ACCESSIBILITY_SCROLL_AREA = 12,
    /** Modal or modeless dialog window. */
    NK_ACCESSIBILITY_DIALOG = 13,
    /** Popup or application menu. */
    NK_ACCESSIBILITY_MENU = 14,
    /** Application menu bar. */
    NK_ACCESSIBILITY_MENU_BAR = 15,
    /** Command or choice inside a menu. */
    NK_ACCESSIBILITY_MENU_ITEM = 16,
    /** Container for a set of tabs. */
    NK_ACCESSIBILITY_TAB_LIST = 17,
    /** Selectable tab in a tab list. */
    NK_ACCESSIBILITY_TAB = 18,
    /** Content associated with a tab. */
    NK_ACCESSIBILITY_TAB_PANEL = 19,
    /** On/off switch control. */
    NK_ACCESSIBILITY_SWITCH = 20,
    /** Read-only progress indicator. */
    NK_ACCESSIBILITY_PROGRESS_BAR = 21,
    /** Editable selection from a list of options. */
    NK_ACCESSIBILITY_COMBO_BOX = 22,
    /** Generic collection of related items. */
    NK_ACCESSIBILITY_COLLECTION = 23,
    /** Item in a collection. */
    NK_ACCESSIBILITY_COLLECTION_ITEM = 24,
    /** Two-dimensional collection of cells. */
    NK_ACCESSIBILITY_GRID = 25,
    /** Row in a grid or table. */
    NK_ACCESSIBILITY_ROW = 26,
    /** Cell in a grid or table. */
    NK_ACCESSIBILITY_CELL = 27,
    /** Header describing a grid column. */
    NK_ACCESSIBILITY_COLUMN_HEADER = 28,
    /** Header describing a grid row. */
    NK_ACCESSIBILITY_ROW_HEADER = 29,
    /** Hierarchical collection of tree items. */
    NK_ACCESSIBILITY_TREE = 30,
    /** Item in a hierarchical tree. */
    NK_ACCESSIBILITY_TREE_ITEM = 31,
    /** Non-interactive visual or semantic separator. */
    NK_ACCESSIBILITY_SEPARATOR = 32,
    /** Group of commonly used commands. */
    NK_ACCESSIBILITY_TOOLBAR = 33,
    /** Status information about the application or document. */
    NK_ACCESSIBILITY_STATUS = 34,
    /** Important message that should be announced promptly. */
    NK_ACCESSIBILITY_ALERT = 35
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
    NK_ACCESSIBILITY_EXPANDED = 1u << 8,
    /** The node or one of its descendants is modal. */
    NK_ACCESSIBILITY_MODAL = 1u << 9,
    /** The node must be populated before its form can be submitted. */
    NK_ACCESSIBILITY_REQUIRED = 1u << 10,
    /** The current value or input is invalid. */
    NK_ACCESSIBILITY_INVALID = 1u << 11,
    /** The node or its contents are being updated. */
    NK_ACCESSIBILITY_BUSY = 1u << 12,
    /** The node exposes a popup or expandable popup content. */
    NK_ACCESSIBILITY_HAS_POPUP = 1u << 13
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
    NK_ACCESSIBILITY_CAN_MOVE_PREVIOUS = 1u << 9,
    /** The node accepts NK_ACCESSIBILITY_ACTION_TOGGLE. */
    NK_ACCESSIBILITY_CAN_TOGGLE = 1u << 10,
    /** The node accepts NK_ACCESSIBILITY_ACTION_SELECT. */
    NK_ACCESSIBILITY_CAN_SELECT = 1u << 11,
    /** The node accepts NK_ACCESSIBILITY_ACTION_DESELECT. */
    NK_ACCESSIBILITY_CAN_DESELECT = 1u << 12,
    /** The node accepts NK_ACCESSIBILITY_ACTION_EXPAND. */
    NK_ACCESSIBILITY_CAN_EXPAND = 1u << 13,
    /** The node accepts NK_ACCESSIBILITY_ACTION_COLLAPSE. */
    NK_ACCESSIBILITY_CAN_COLLAPSE = 1u << 14,
    /** The node accepts NK_ACCESSIBILITY_ACTION_DISMISS. */
    NK_ACCESSIBILITY_CAN_DISMISS = 1u << 15,
    /** The node accepts NK_ACCESSIBILITY_ACTION_SHOW_CONTEXT_MENU. */
    NK_ACCESSIBILITY_CAN_SHOW_CONTEXT_MENU = 1u << 16,
    /** The node accepts NK_ACCESSIBILITY_ACTION_SCROLL_INTO_VIEW. */
    NK_ACCESSIBILITY_CAN_SCROLL_INTO_VIEW = 1u << 17
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
    NK_ACCESSIBILITY_ACTION_MOVE_PREVIOUS = 11,
    /** Toggle a switch or other binary control. */
    NK_ACCESSIBILITY_ACTION_TOGGLE = 12,
    /** Select this item. */
    NK_ACCESSIBILITY_ACTION_SELECT = 13,
    /** Remove selection from this item. */
    NK_ACCESSIBILITY_ACTION_DESELECT = 14,
    /** Expand this item. */
    NK_ACCESSIBILITY_ACTION_EXPAND = 15,
    /** Collapse this item. */
    NK_ACCESSIBILITY_ACTION_COLLAPSE = 16,
    /** Dismiss this popup or transient element. */
    NK_ACCESSIBILITY_ACTION_DISMISS = 17,
    /** Show this item's context menu. */
    NK_ACCESSIBILITY_ACTION_SHOW_CONTEXT_MENU = 18,
    /** Scroll the item into the visible viewport. */
    NK_ACCESSIBILITY_ACTION_SCROLL_INTO_VIEW = 19
};

/** Generic orientation for collections and other directional semantics. */
typedef uint32_t nk_accessibility_orientation;
enum NK_ENUM(nk_accessibility_orientation) {
    /** No orientation is specified. */
    NK_ACCESSIBILITY_ORIENTATION_UNSPECIFIED = 0,
    /** Items progress along the horizontal axis. */
    NK_ACCESSIBILITY_ORIENTATION_HORIZONTAL = 1,
    /** Items progress along the vertical axis. */
    NK_ACCESSIBILITY_ORIENTATION_VERTICAL = 2
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
    uint32_t struct_size NK_STRUCT_SIZE;
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
    /* Keep future additions at the tail. Readers should use struct_size and
       treat metadata beyond the supplied size as unspecified. */
    /** Total logical items in a collection; zero means unspecified. */
    uint32_t set_size;
    /** One-based item position in its collection; zero means unspecified. */
    uint32_t position_in_set;
    /** Total rows in a grid; zero means unspecified. */
    uint32_t row_count;
    /** Total columns in a grid; zero means unspecified. */
    uint32_t column_count;
    /** Zero-based row index, or NK_ACCESSIBILITY_INDEX_NONE if unspecified. */
    uint32_t row_index;
    /** Zero-based column index, or NK_ACCESSIBILITY_INDEX_NONE if unspecified. */
    uint32_t column_index;
    /** Number of rows occupied by a grid cell; zero means unspecified. */
    uint32_t row_span;
    /** Number of columns occupied by a grid cell; zero means unspecified. */
    uint32_t column_span;
    /** One-based depth in a hierarchy; zero means unspecified. */
    uint32_t hierarchy_level;
    /** Generic collection orientation. */
    nk_accessibility_orientation orientation;
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
    uint32_t struct_size NK_STRUCT_SIZE;
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
NK_API nk_result NK_CALL nk_surface_accessibility_set_node(nk_surface surface,
                                                           const nk_accessibility_node *node);

/**
 * Removes a virtual node and all of its descendants.
 *
 * `node` must identify an existing non-root node. The operation runs on the UI
 * thread and requires NK_CAP_ACCESSIBILITY.
 */
NK_API nk_result NK_CALL nk_surface_accessibility_remove_node(nk_surface surface,
                                                              nk_accessibility_node_id node);

/** Removes all virtual nodes from a surface's accessibility tree. */
NK_API nk_result NK_CALL nk_surface_accessibility_clear(nk_surface surface);

/**
 * Moves accessibility focus to a virtual node.
 *
 * Pass NK_ACCESSIBILITY_ROOT to clear virtual focus. Any non-root node must
 * already exist in the surface's tree.
 */
NK_API nk_result NK_CALL nk_surface_accessibility_set_focus(nk_surface surface,
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
NK_API nk_result NK_CALL nk_surface_accessibility_update(nk_surface surface,
                                                         const nk_accessibility_update *update);

/**
 * Applies an atomic accessibility update whose removed-node list is supplied
 * as a packed little-endian byte array of 32-bit node IDs. This helper exists
 * for language bindings that can safely marshal byte buffers but cannot
 * represent a pointer to a primitive array inside `nk_accessibility_update`.
 * The update's `removed_nodes` and `removed_node_count` fields are replaced by
 * the supplied array; all other fields are applied unchanged.
 */
NK_API nk_result NK_CALL nk_surface_accessibility_update_with_removed_ids(
    nk_surface surface, const nk_accessibility_update *update,
    const uint8_t *removed_node_ids NK_BORROWED_ARRAY(removed_node_id_byte_count),
    uint32_t removed_node_id_byte_count);

/**
 * Supplies screen-reader geometry for text ranges in a node's value.
 *
 * Ranges are borrowed during the call and copied on success. They must be
 * ordered, non-overlapping, non-empty, and use document code-point offsets.
 * Pass a zero count to clear the node's current range geometry.
 */
NK_API nk_result NK_CALL nk_surface_accessibility_set_text_ranges(
    nk_surface surface, nk_accessibility_node_id node,
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
