#ifndef NATIVEKIT_INPUT_H
#define NATIVEKIT_INPUT_H

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

/* ------------------------------------------------------------------------- */
/* Keyboard input types                                                      */
/* ------------------------------------------------------------------------- */

/** Transition reported for a key or pointer button. */
typedef uint32_t nk_input_action;
enum NK_ENUM(nk_input_action) {
    /** The key or button was released. */
    NK_INPUT_RELEASE = 0,
    /** The key or button was pressed. */
    NK_INPUT_PRESS = 1,
    /** The key is being held and generated a repeat event. */
    NK_INPUT_REPEAT = 2
};

/** Modifier keys active when an input event was generated. */
typedef uint32_t nk_modifiers;
enum NK_FLAGS(nk_modifiers) {
    /** The Shift modifier is active. */
    NK_MOD_SHIFT = 1u << 0,
    /** The Control modifier is active. */
    NK_MOD_CONTROL = 1u << 1,
    /** The Alt modifier is active. */
    NK_MOD_ALT = 1u << 2,
    /** The platform's Super, Command, or Windows modifier is active. */
    NK_MOD_SUPER = 1u << 3,
    /** Caps Lock is active. */
    NK_MOD_CAPS_LOCK = 1u << 4,
    /** Num Lock is active. */
    NK_MOD_NUM_LOCK = 1u << 5
};

/** Normalized physical or logical keyboard key identifier. */
typedef uint32_t nk_key;
enum NK_ENUM(nk_key) {
    /** No supported key could be identified. */
    NK_KEY_UNKNOWN = 0,
    /** The Space key. */
    NK_KEY_SPACE = 32,
    /** The apostrophe key. */
    NK_KEY_APOSTROPHE = 39,
    /** The comma key. */
    NK_KEY_COMMA = 44,
    /** The minus key. */
    NK_KEY_MINUS = 45,
    /** The period key. */
    NK_KEY_PERIOD = 46,
    /** The slash key. */
    NK_KEY_SLASH = 47,
    /** The 0 key. */
    NK_KEY_0 = 48,
    /** The 1 key. */
    NK_KEY_1 = 49,
    /** The 2 key. */
    NK_KEY_2 = 50,
    /** The 3 key. */
    NK_KEY_3 = 51,
    /** The 4 key. */
    NK_KEY_4 = 52,
    /** The 5 key. */
    NK_KEY_5 = 53,
    /** The 6 key. */
    NK_KEY_6 = 54,
    /** The 7 key. */
    NK_KEY_7 = 55,
    /** The 8 key. */
    NK_KEY_8 = 56,
    /** The 9 key. */
    NK_KEY_9 = 57,
    /** The semicolon key. */
    NK_KEY_SEMICOLON = 59,
    /** The equals key. */
    NK_KEY_EQUAL = 61,
    /** The A key. */
    NK_KEY_A = 65,
    /** The B key. */
    NK_KEY_B = 66,
    /** The C key. */
    NK_KEY_C = 67,
    /** The D key. */
    NK_KEY_D = 68,
    /** The E key. */
    NK_KEY_E = 69,
    /** The F key. */
    NK_KEY_F = 70,
    /** The G key. */
    NK_KEY_G = 71,
    /** The H key. */
    NK_KEY_H = 72,
    /** The I key. */
    NK_KEY_I = 73,
    /** The J key. */
    NK_KEY_J = 74,
    /** The K key. */
    NK_KEY_K = 75,
    /** The L key. */
    NK_KEY_L = 76,
    /** The M key. */
    NK_KEY_M = 77,
    /** The N key. */
    NK_KEY_N = 78,
    /** The O key. */
    NK_KEY_O = 79,
    /** The P key. */
    NK_KEY_P = 80,
    /** The Q key. */
    NK_KEY_Q = 81,
    /** The R key. */
    NK_KEY_R = 82,
    /** The S key. */
    NK_KEY_S = 83,
    /** The T key. */
    NK_KEY_T = 84,
    /** The U key. */
    NK_KEY_U = 85,
    /** The V key. */
    NK_KEY_V = 86,
    /** The W key. */
    NK_KEY_W = 87,
    /** The X key. */
    NK_KEY_X = 88,
    /** The Y key. */
    NK_KEY_Y = 89,
    /** The Z key. */
    NK_KEY_Z = 90,
    /** The left bracket key. */
    NK_KEY_LEFT_BRACKET = 91,
    /** The backslash key. */
    NK_KEY_BACKSLASH = 92,
    /** The right bracket key. */
    NK_KEY_RIGHT_BRACKET = 93,
    /** The grave-accent key. */
    NK_KEY_GRAVE_ACCENT = 96,
    /** The Escape key. */
    NK_KEY_ESCAPE = 256,
    /** The Enter or Return key. */
    NK_KEY_ENTER = 257,
    /** The Tab key. */
    NK_KEY_TAB = 258,
    /** The Backspace key. */
    NK_KEY_BACKSPACE = 259,
    /** The Insert key. */
    NK_KEY_INSERT = 260,
    /** The Delete key. */
    NK_KEY_DELETE = 261,
    /** The Right Arrow key. */
    NK_KEY_RIGHT = 262,
    /** The Left Arrow key. */
    NK_KEY_LEFT = 263,
    /** The Down Arrow key. */
    NK_KEY_DOWN = 264,
    /** The Up Arrow key. */
    NK_KEY_UP = 265,
    /** The Page Up key. */
    NK_KEY_PAGE_UP = 266,
    /** The Page Down key. */
    NK_KEY_PAGE_DOWN = 267,
    /** The Home key. */
    NK_KEY_HOME = 268,
    /** The End key. */
    NK_KEY_END = 269,
    /** The Caps Lock key. */
    NK_KEY_CAPS_LOCK = 280,
    /** The Scroll Lock key. */
    NK_KEY_SCROLL_LOCK = 281,
    /** The Num Lock key. */
    NK_KEY_NUM_LOCK = 282,
    /** The Print Screen key. */
    NK_KEY_PRINT_SCREEN = 283,
    /** The Pause key. */
    NK_KEY_PAUSE = 284,
    /** The F1 key. */
    NK_KEY_F1 = 290,
    /** The F2 key. */
    NK_KEY_F2 = 291,
    /** The F3 key. */
    NK_KEY_F3 = 292,
    /** The F4 key. */
    NK_KEY_F4 = 293,
    /** The F5 key. */
    NK_KEY_F5 = 294,
    /** The F6 key. */
    NK_KEY_F6 = 295,
    /** The F7 key. */
    NK_KEY_F7 = 296,
    /** The F8 key. */
    NK_KEY_F8 = 297,
    /** The F9 key. */
    NK_KEY_F9 = 298,
    /** The F10 key. */
    NK_KEY_F10 = 299,
    /** The F11 key. */
    NK_KEY_F11 = 300,
    /** The F12 key. */
    NK_KEY_F12 = 301,
    /** The F13 key. */
    NK_KEY_F13 = 302,
    /** The F14 key. */
    NK_KEY_F14 = 303,
    /** The F15 key. */
    NK_KEY_F15 = 304,
    /** The F16 key. */
    NK_KEY_F16 = 305,
    /** The F17 key. */
    NK_KEY_F17 = 306,
    /** The F18 key. */
    NK_KEY_F18 = 307,
    /** The F19 key. */
    NK_KEY_F19 = 308,
    /** The F20 key. */
    NK_KEY_F20 = 309,
    /** The F21 key. */
    NK_KEY_F21 = 310,
    /** The F22 key. */
    NK_KEY_F22 = 311,
    /** The F23 key. */
    NK_KEY_F23 = 312,
    /** The F24 key. */
    NK_KEY_F24 = 313,
    /** The F25 key. */
    NK_KEY_F25 = 314,
    /** The numeric keypad 0 key. */
    NK_KEY_KP_0 = 320,
    /** The numeric keypad 1 key. */
    NK_KEY_KP_1 = 321,
    /** The numeric keypad 2 key. */
    NK_KEY_KP_2 = 322,
    /** The numeric keypad 3 key. */
    NK_KEY_KP_3 = 323,
    /** The numeric keypad 4 key. */
    NK_KEY_KP_4 = 324,
    /** The numeric keypad 5 key. */
    NK_KEY_KP_5 = 325,
    /** The numeric keypad 6 key. */
    NK_KEY_KP_6 = 326,
    /** The numeric keypad 7 key. */
    NK_KEY_KP_7 = 327,
    /** The numeric keypad 8 key. */
    NK_KEY_KP_8 = 328,
    /** The numeric keypad 9 key. */
    NK_KEY_KP_9 = 329,
    /** The numeric keypad decimal key. */
    NK_KEY_KP_DECIMAL = 330,
    /** The numeric keypad divide key. */
    NK_KEY_KP_DIVIDE = 331,
    /** The numeric keypad multiply key. */
    NK_KEY_KP_MULTIPLY = 332,
    /** The numeric keypad subtract key. */
    NK_KEY_KP_SUBTRACT = 333,
    /** The numeric keypad add key. */
    NK_KEY_KP_ADD = 334,
    /** The numeric keypad Enter key. */
    NK_KEY_KP_ENTER = 335,
    /** The numeric keypad equals key. */
    NK_KEY_KP_EQUAL = 336,
    /** The left Shift key. */
    NK_KEY_LEFT_SHIFT = 340,
    /** The left Control key. */
    NK_KEY_LEFT_CONTROL = 341,
    /** The left Alt key. */
    NK_KEY_LEFT_ALT = 342,
    /** The left Super, Command, or Windows key. */
    NK_KEY_LEFT_SUPER = 343,
    /** The right Shift key. */
    NK_KEY_RIGHT_SHIFT = 344,
    /** The right Control key. */
    NK_KEY_RIGHT_CONTROL = 345,
    /** The right Alt key. */
    NK_KEY_RIGHT_ALT = 346,
    /** The right Super, Command, or Windows key. */
    NK_KEY_RIGHT_SUPER = 347,
    /** The Menu or context-menu key. */
    NK_KEY_MENU = 348,
    /** Highest key value; useful for sizing key-state arrays. */
    NK_KEY_LAST = NK_KEY_MENU
};

/** Mouse or pointer button identifier. */
typedef uint32_t nk_pointer_button;
enum NK_ENUM(nk_pointer_button) {
    /** The primary or left pointer button. */
    NK_POINTER_BUTTON_LEFT = 0,
    /** The secondary or right pointer button. */
    NK_POINTER_BUTTON_RIGHT = 1,
    /** The middle pointer button. */
    NK_POINTER_BUTTON_MIDDLE = 2,
    /** The fourth pointer button. */
    NK_POINTER_BUTTON_4 = 3,
    /** The fifth pointer button. */
    NK_POINTER_BUTTON_5 = 4,
    /** Highest pointer-button value reserved for state queries. */
    NK_POINTER_BUTTON_LAST = 7
};

/** Payload of NK_EVENT_KEY. Scancodes remain platform-specific. */
typedef struct nk_key_event {
    /** Normalized key identifier, or NK_KEY_UNKNOWN when it is unavailable. */
    nk_key key;
    /** Platform hardware scancode, for applications that need physical keys. */
    uint32_t scancode;
    /** Press, release, or repeat transition. */
    nk_input_action action;
    /** Modifier keys active for this event. */
    nk_modifiers modifiers;
} nk_key_event;

/** Payload of NK_EVENT_TEXT_INPUT containing committed Unicode text. */
typedef struct nk_text_input_event {
    /** Unicode code point committed by the keyboard or input method. */
    uint32_t codepoint;
    /** Reserved for future use; initialize to zero. */
    uint32_t reserved;
} nk_text_input_event;

/* ------------------------------------------------------------------------- */
/* Text input and IME types                                                  */
/* ------------------------------------------------------------------------- */

/** Operation represented by an NK_EVENT_TEXT_EDIT transaction. */
typedef uint32_t nk_text_edit_action;
enum NK_ENUM(nk_text_edit_action) {
    /** Update the active composition without committing it. */
    NK_TEXT_EDIT_COMPOSE = 1,
    /** Replace the reported range with committed UTF-8 text. */
    NK_TEXT_EDIT_COMMIT = 2,
    /** Delete the reported range. */
    NK_TEXT_EDIT_DELETE = 3,
    /** Move or extend the selection to the reported range. */
    NK_TEXT_EDIT_SET_SELECTION = 4,
    /** Finish the active composition without further replacement text. */
    NK_TEXT_EDIT_FINISH_COMPOSITION = 5,
    /** Set or update the active composition range. */
    NK_TEXT_EDIT_SET_COMPOSITION = 6
};

/** Absolute Unicode code-point position used by the text-input API. */
typedef uint32_t nk_text_position;
enum {
    /** Sentinel meaning that a position, such as an absent composition, is not present. */
    NK_TEXT_POSITION_NONE = 0xffffffffu
};

/** Keyboard purpose hint for a custom text editor. */
typedef uint32_t nk_text_input_type;
enum NK_ENUM(nk_text_input_type) {
    /** General text input. */
    NK_TEXT_INPUT_TEXT = 0,
    /** Email address input. */
    NK_TEXT_INPUT_EMAIL = 1,
    /** URL input. */
    NK_TEXT_INPUT_URL = 2,
    /** Numeric input. */
    NK_TEXT_INPUT_NUMBER = 3,
    /** Telephone number input. */
    NK_TEXT_INPUT_PHONE = 4,
    /** Password input; the platform may obscure entered text. */
    NK_TEXT_INPUT_PASSWORD = 5
};

/** Optional behavior requested from the platform text input method. */
typedef uint32_t nk_text_input_flags;
enum NK_FLAGS(nk_text_input_flags) {
    /** The editor accepts multiple lines. */
    NK_TEXT_INPUT_MULTILINE = 1u << 0,
    /** Ask the platform to provide autocorrection. */
    NK_TEXT_INPUT_AUTOCORRECT = 1u << 1,
    /** Ask the platform to capitalize sentence starts. */
    NK_TEXT_INPUT_CAPITALIZE_SENTENCES = 1u << 2
};

/** Action requested for the keyboard's enter or action key. */
typedef uint32_t nk_text_input_action;
enum NK_ENUM(nk_text_input_action) {
    /** Use the platform or editor default action. */
    NK_TEXT_INPUT_ACTION_DEFAULT = 0,
    /** Complete the current editing operation. */
    NK_TEXT_INPUT_ACTION_DONE = 1,
    /** Submit a navigation or URL operation. */
    NK_TEXT_INPUT_ACTION_GO = 2,
    /** Move focus to the next editor. */
    NK_TEXT_INPUT_ACTION_NEXT = 3,
    /** Start a search operation. */
    NK_TEXT_INPUT_ACTION_SEARCH = 4,
    /** Send or submit the current content. */
    NK_TEXT_INPUT_ACTION_SEND = 5,
    /** Do not request a specific action key. */
    NK_TEXT_INPUT_ACTION_NONE = 6
};

/*
 * Positions are absolute Unicode code-point indices in the editor document.
 * COMPOSE, COMMIT, and DELETE replace [replace_start, replace_end) with the
 * UTF-8 text stored at text_offset.
 * Selection and composition positions describe the state after the edit.
 */
/** Payload of NK_EVENT_TEXT_EDIT; positions are absolute code-point indices. */
typedef struct nk_text_edit_event {
    /** Operation to apply to the editor's text model. */
    nk_text_edit_action action;
    /** Byte offset of replacement UTF-8 text in the event payload, or zero. */
    uint32_t text_offset;
    /** Byte length of replacement UTF-8 text, excluding its trailing NUL. */
    uint32_t text_length;
    /** Start of the document range to replace. */
    nk_text_position replace_start;
    /** Exclusive end of the document range to replace. */
    nk_text_position replace_end;
    /** Selection start after applying the transaction. */
    nk_text_position selection_start;
    /** Exclusive selection end after applying the transaction. */
    nk_text_position selection_end;
    /** Active composition start after applying the transaction, or NONE. */
    nk_text_position composition_start;
    /** Exclusive active composition end after applying the transaction, or NONE. */
    nk_text_position composition_end;
    /** Reserved for future use; initialize to zero. */
    uint32_t reserved[3];
} nk_text_edit_event;

/** State published by a custom editor to synchronize the platform IME. */
typedef struct nk_text_input_state {
    /** Set to sizeof(nk_text_input_state) before calling NativeKit. */
    uint32_t struct_size;
    /** Text-input behavior requested from the platform IME. */
    nk_text_input_flags flags;
    /** NUL-terminated UTF-8 window into the document, used during the call. */
    const char *text NK_UTF8;
    /** Absolute code-point index of the first code point in text. */
    nk_text_position text_start;
    /** Total document length in Unicode code points. */
    nk_text_position document_length;
    /** Absolute selection start; must be within the supplied text window. */
    nk_text_position selection_start;
    /** Exclusive absolute selection end; must be within the supplied text window. */
    nk_text_position selection_end;
    /** Absolute composition start, or NK_TEXT_POSITION_NONE. */
    nk_text_position composition_start;
    /** Exclusive absolute composition end, or NK_TEXT_POSITION_NONE. */
    nk_text_position composition_end;
    /** Keyboard purpose requested for the editor. */
    nk_text_input_type input_type;
    /** Enter-key action requested for the editor. */
    nk_text_input_action action;
    /** Cursor x coordinate in surface-local logical pixels. */
    float cursor_x;
    /** Cursor y coordinate in surface-local logical pixels. */
    float cursor_y;
    /** Cursor width in surface-local logical pixels. */
    float cursor_width;
    /** Cursor height in surface-local logical pixels. */
    float cursor_height;
    /** Reserved for future use; initialize to zero. */
    uint64_t reserved[2];
} nk_text_input_state;

/* ------------------------------------------------------------------------- */
/* Text input event helpers                                                  */
/* ------------------------------------------------------------------------- */

/** Returns a borrowed UTF-8 view valid until nk_event_release(). */
NK_API nk_result NK_CALL nk_text_edit_event_text(const nk_event *event, const char **out_text,
                                                 uint32_t *out_length);

/* ------------------------------------------------------------------------- */
/* Pointer and touch event types                                             */
/* ------------------------------------------------------------------------- */

/** Payload of NK_EVENT_POINTER_MOVE in surface-local logical pixels. */
typedef struct nk_pointer_move_event {
    /** Pointer x coordinate in logical pixels. */
    double x;
    /** Pointer y coordinate in logical pixels. */
    double y;
} nk_pointer_move_event;

/** Payload of NK_EVENT_POINTER_BUTTON in surface-local logical pixels. */
typedef struct nk_pointer_button_event {
    /** Pointer button that changed. */
    nk_pointer_button button;
    /** Press or release transition. */
    nk_input_action action;
    /** Modifier keys active for this event. */
    nk_modifiers modifiers;
    /** Reserved for future use; initialize to zero. */
    uint32_t reserved;
    /** Pointer x coordinate in logical pixels. */
    double x;
    /** Pointer y coordinate in logical pixels. */
    double y;
} nk_pointer_button_event;

/** Payload of NK_EVENT_POINTER_SCROLL containing scroll deltas. */
typedef struct nk_pointer_scroll_event {
    /** Horizontal scroll delta; smooth devices may report fractional values. */
    double x;
    /** Vertical scroll delta; smooth devices may report fractional values. */
    double y;
} nk_pointer_scroll_event;

/** Touch contact transition. */
typedef uint32_t nk_touch_action;
enum NK_ENUM(nk_touch_action) {
    /** A new touch contact began. */
    NK_TOUCH_BEGIN = 1,
    /** An existing touch contact moved. */
    NK_TOUCH_MOVE = 2,
    /** An existing touch contact ended normally. */
    NK_TOUCH_END = 3,
    /** An existing touch contact was cancelled. */
    NK_TOUCH_CANCEL = 4
};

/** Tool used for a touch contact. */
typedef uint32_t nk_touch_tool;
enum NK_ENUM(nk_touch_tool) {
    /** A finger or ordinary touch contact. */
    NK_TOUCH_TOOL_FINGER = 1,
    /** A stylus or pen contact. */
    NK_TOUCH_TOOL_STYLUS = 2,
    /** An eraser end of a stylus. */
    NK_TOUCH_TOOL_ERASER = 3
};

/** Payload of NK_EVENT_TOUCH in surface-local logical pixels. */
typedef struct nk_touch_event {
    /** Identifier that remains stable for the lifetime of this contact. */
    uint32_t pointer_id;
    /** Begin, move, end, or cancellation transition. */
    nk_touch_action action;
    /** Finger, stylus, or eraser tool. */
    nk_touch_tool tool;
    /** Modifier keys active for this event. */
    nk_modifiers modifiers;
    /** Touch x coordinate in logical pixels. */
    double x;
    /** Touch y coordinate in logical pixels. */
    double y;
    /** Normalized pressure, when available; otherwise a backend default. */
    float pressure;
    /** Stylus tilt around the x axis, in backend-defined normalized units. */
    float tilt_x;
    /** Stylus tilt around the y axis, in backend-defined normalized units. */
    float tilt_y;
    /** Reserved for future use; initialize to zero. */
    uint32_t reserved;
} nk_touch_event;

/* ------------------------------------------------------------------------- */
/* Cursor types                                                              */
/* ------------------------------------------------------------------------- */

/** Standard cursor shape understood by the active platform backend. */
typedef uint32_t nk_cursor_shape;
enum NK_ENUM(nk_cursor_shape) {
    /** The platform's default arrow cursor. */
    NK_CURSOR_ARROW = 1,
    /** A text-entry or I-beam cursor. */
    NK_CURSOR_IBEAM = 2,
    /** A crosshair cursor. */
    NK_CURSOR_CROSSHAIR = 3,
    /** A link or hand cursor. */
    NK_CURSOR_HAND = 4,
    /** A horizontal-resize cursor. */
    NK_CURSOR_HORIZONTAL_RESIZE = 5,
    /** A vertical-resize cursor. */
    NK_CURSOR_VERTICAL_RESIZE = 6,
    /** A diagonal northwest-southeast resize cursor. */
    NK_CURSOR_NWSE_RESIZE = 7,
    /** A diagonal northeast-southwest resize cursor. */
    NK_CURSOR_NESW_RESIZE = 8,
    /** A move cursor. */
    NK_CURSOR_MOVE = 9,
    /** A cursor indicating that an operation is not allowed. */
    NK_CURSOR_NOT_ALLOWED = 10
};

/** RGBA8 pixels used to create a custom cursor. */
typedef struct nk_cursor_image {
    /** Set to sizeof(nk_cursor_image) before calling nk_cursor_create_custom. */
    uint32_t struct_size;
    /** Image width in pixels. */
    int32_t width;
    /** Image height in pixels. */
    int32_t height;
    /** Number of bytes between the starts of adjacent rows. */
    int32_t stride;
    /** Horizontal hotspot position in pixels. */
    int32_t hotspot_x;
    /** Vertical hotspot position in pixels. */
    int32_t hotspot_y;
    /** Pointer to width * height RGBA8 pixels, with the first row at the top. */
    const void *rgba;
    /** Reserved for future use; initialize to zero. */
    uint64_t reserved[2];
} nk_cursor_image;

/** Pointer presentation or capture mode for a window. */
typedef uint32_t nk_cursor_mode;
enum NK_ENUM(nk_cursor_mode) {
    /** Show the selected cursor normally. */
    NK_CURSOR_MODE_NORMAL = 0,
    /** Hide the cursor while it is over the window. */
    NK_CURSOR_MODE_HIDDEN = 1,
    /** Capture the pointer while it is interacting with the window. */
    NK_CURSOR_MODE_CAPTURED = 2,
    /** Request disabled or relative-pointer behavior; may be unsupported. */
    NK_CURSOR_MODE_DISABLED = 3
};

/* ------------------------------------------------------------------------- */
/* Input state and text input APIs                                           */
/* ------------------------------------------------------------------------- */

/** On NK_OK, returns the current action state for a key. Call on the UI thread. */
NK_API nk_result NK_CALL nk_key_get_state(nk_handle window, nk_key key,
                                          nk_input_action *out_action);

/** On NK_OK, returns the current action state of one pointer button. */
NK_API nk_result NK_CALL nk_pointer_button_get_state(nk_handle window, nk_pointer_button button,
                                                     nk_input_action *out_action);

/** On NK_OK, returns the current pointer position in surface-local logical pixels. */
NK_API nk_result NK_CALL nk_pointer_get_position(nk_handle window, double *out_x, double *out_y);

/** Synchronizes a custom editor with the platform IME using absolute code-point positions. */
NK_API nk_result NK_CALL nk_surface_set_text_input_state(nk_handle surface,
                                                         const nk_text_input_state *state);
/** Shows or hides the software keyboard for a custom graphics surface. */
NK_API nk_result NK_CALL nk_surface_set_text_input_active(nk_handle surface, nk_bool active);

/* ------------------------------------------------------------------------- */
/* Cursor APIs                                                               */
/* ------------------------------------------------------------------------- */

/** Creates a standard platform cursor and returns its NativeKit handle. */
NK_API nk_result NK_CALL nk_cursor_create_standard(nk_cursor_shape shape, nk_handle *out_cursor);

/** Creates a cursor from copied RGBA8 pixels, with the first row at the top. */
NK_API nk_result NK_CALL nk_cursor_create_custom(const nk_cursor_image *image,
                                                 nk_handle *out_cursor);

/** Destroys a cursor handle; a cursor already selected by a window remains usable. */
NK_API nk_result NK_CALL nk_cursor_destroy(nk_handle cursor);

/** Selects a cursor for a window; NK_INVALID_HANDLE restores the platform default. */
NK_API nk_result NK_CALL nk_window_set_cursor(nk_handle window, nk_handle cursor);

/** Changes how the window displays or captures the pointer. */
NK_API nk_result NK_CALL nk_window_set_cursor_mode(nk_handle window, nk_cursor_mode mode);

/** On NK_OK, returns the window's current pointer presentation mode. */
NK_API nk_result NK_CALL nk_window_get_cursor_mode(nk_handle window, nk_cursor_mode *out_mode);

/** Returns non-zero when the active backend provides raw relative pointer motion. */
NK_API nk_bool NK_CALL nk_raw_pointer_motion_supported(void);

#ifdef __cplusplus
}
#endif

#endif
