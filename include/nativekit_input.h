#ifndef NATIVEKIT_INPUT_H
#define NATIVEKIT_INPUT_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nk_input_action;
enum { NK_INPUT_RELEASE = 0, NK_INPUT_PRESS = 1, NK_INPUT_REPEAT = 2 };

typedef uint32_t nk_modifiers;
enum {
    NK_MOD_SHIFT = 1u << 0,
    NK_MOD_CONTROL = 1u << 1,
    NK_MOD_ALT = 1u << 2,
    NK_MOD_SUPER = 1u << 3,
    NK_MOD_CAPS_LOCK = 1u << 4,
    NK_MOD_NUM_LOCK = 1u << 5
};

typedef uint32_t nk_key;
enum {
    NK_KEY_UNKNOWN = 0,
    NK_KEY_SPACE = 32,
    NK_KEY_APOSTROPHE = 39,
    NK_KEY_COMMA = 44,
    NK_KEY_MINUS = 45,
    NK_KEY_PERIOD = 46,
    NK_KEY_SLASH = 47,
    NK_KEY_0 = 48,
    NK_KEY_1 = 49,
    NK_KEY_2 = 50,
    NK_KEY_3 = 51,
    NK_KEY_4 = 52,
    NK_KEY_5 = 53,
    NK_KEY_6 = 54,
    NK_KEY_7 = 55,
    NK_KEY_8 = 56,
    NK_KEY_9 = 57,
    NK_KEY_SEMICOLON = 59,
    NK_KEY_EQUAL = 61,
    NK_KEY_A = 65,
    NK_KEY_B = 66,
    NK_KEY_C = 67,
    NK_KEY_D = 68,
    NK_KEY_E = 69,
    NK_KEY_F = 70,
    NK_KEY_G = 71,
    NK_KEY_H = 72,
    NK_KEY_I = 73,
    NK_KEY_J = 74,
    NK_KEY_K = 75,
    NK_KEY_L = 76,
    NK_KEY_M = 77,
    NK_KEY_N = 78,
    NK_KEY_O = 79,
    NK_KEY_P = 80,
    NK_KEY_Q = 81,
    NK_KEY_R = 82,
    NK_KEY_S = 83,
    NK_KEY_T = 84,
    NK_KEY_U = 85,
    NK_KEY_V = 86,
    NK_KEY_W = 87,
    NK_KEY_X = 88,
    NK_KEY_Y = 89,
    NK_KEY_Z = 90,
    NK_KEY_LEFT_BRACKET = 91,
    NK_KEY_BACKSLASH = 92,
    NK_KEY_RIGHT_BRACKET = 93,
    NK_KEY_GRAVE_ACCENT = 96,
    NK_KEY_ESCAPE = 256,
    NK_KEY_ENTER = 257,
    NK_KEY_TAB = 258,
    NK_KEY_BACKSPACE = 259,
    NK_KEY_INSERT = 260,
    NK_KEY_DELETE = 261,
    NK_KEY_RIGHT = 262,
    NK_KEY_LEFT = 263,
    NK_KEY_DOWN = 264,
    NK_KEY_UP = 265,
    NK_KEY_PAGE_UP = 266,
    NK_KEY_PAGE_DOWN = 267,
    NK_KEY_HOME = 268,
    NK_KEY_END = 269,
    NK_KEY_CAPS_LOCK = 280,
    NK_KEY_SCROLL_LOCK = 281,
    NK_KEY_NUM_LOCK = 282,
    NK_KEY_PRINT_SCREEN = 283,
    NK_KEY_PAUSE = 284,
    NK_KEY_F1 = 290,
    NK_KEY_F12 = 301,
    NK_KEY_F13 = 302,
    NK_KEY_F14 = 303,
    NK_KEY_F15 = 304,
    NK_KEY_F16 = 305,
    NK_KEY_F17 = 306,
    NK_KEY_F18 = 307,
    NK_KEY_F19 = 308,
    NK_KEY_F20 = 309,
    NK_KEY_F21 = 310,
    NK_KEY_F22 = 311,
    NK_KEY_F23 = 312,
    NK_KEY_F24 = 313,
    NK_KEY_F25 = 314,
    NK_KEY_KP_0 = 320,
    NK_KEY_KP_1 = 321,
    NK_KEY_KP_2 = 322,
    NK_KEY_KP_3 = 323,
    NK_KEY_KP_4 = 324,
    NK_KEY_KP_5 = 325,
    NK_KEY_KP_6 = 326,
    NK_KEY_KP_7 = 327,
    NK_KEY_KP_8 = 328,
    NK_KEY_KP_9 = 329,
    NK_KEY_KP_DECIMAL = 330,
    NK_KEY_KP_DIVIDE = 331,
    NK_KEY_KP_MULTIPLY = 332,
    NK_KEY_KP_SUBTRACT = 333,
    NK_KEY_KP_ADD = 334,
    NK_KEY_KP_ENTER = 335,
    NK_KEY_KP_EQUAL = 336,
    NK_KEY_LEFT_SHIFT = 340,
    NK_KEY_LEFT_CONTROL = 341,
    NK_KEY_LEFT_ALT = 342,
    NK_KEY_LEFT_SUPER = 343,
    NK_KEY_RIGHT_SHIFT = 344,
    NK_KEY_RIGHT_CONTROL = 345,
    NK_KEY_RIGHT_ALT = 346,
    NK_KEY_RIGHT_SUPER = 347,
    NK_KEY_MENU = 348,
    NK_KEY_LAST = NK_KEY_MENU
};

typedef uint32_t nk_pointer_button;
enum {
    NK_POINTER_BUTTON_LEFT = 0,
    NK_POINTER_BUTTON_RIGHT = 1,
    NK_POINTER_BUTTON_MIDDLE = 2,
    NK_POINTER_BUTTON_4 = 3,
    NK_POINTER_BUTTON_5 = 4,
    NK_POINTER_BUTTON_LAST = 7
};

typedef struct nk_key_event {
    nk_key key;
    uint32_t scancode;
    nk_input_action action;
    nk_modifiers modifiers;
} nk_key_event;

typedef struct nk_text_input_event {
    uint32_t codepoint;
    uint32_t reserved;
} nk_text_input_event;

typedef uint32_t nk_text_edit_action;
enum {
    NK_TEXT_EDIT_COMPOSE = 1,
    NK_TEXT_EDIT_COMMIT = 2,
    NK_TEXT_EDIT_DELETE = 3,
    NK_TEXT_EDIT_SET_SELECTION = 4,
    NK_TEXT_EDIT_FINISH_COMPOSITION = 5,
    NK_TEXT_EDIT_SET_COMPOSITION = 6
};

typedef uint32_t nk_text_position;
enum { NK_TEXT_POSITION_NONE = 0xffffffffu };

/*
 * Positions are Unicode code-point indices in the text most recently supplied
 * with nk_surface_set_text_input_state(). COMPOSE, COMMIT, and DELETE replace
 * [replace_start, replace_end) with the UTF-8 text stored at text_offset.
 * Selection and composition positions describe the state after the edit.
 */
typedef struct nk_text_edit_event {
    nk_text_edit_action action;
    uint32_t text_offset;
    uint32_t text_length;
    nk_text_position replace_start;
    nk_text_position replace_end;
    nk_text_position selection_start;
    nk_text_position selection_end;
    nk_text_position composition_start;
    nk_text_position composition_end;
    uint32_t reserved[3];
} nk_text_edit_event;

typedef struct nk_text_input_state {
    uint32_t struct_size;
    uint32_t flags;
    const char *text NK_UTF8;
    nk_text_position selection_start;
    nk_text_position selection_end;
    nk_text_position composition_start;
    nk_text_position composition_end;
    uint64_t reserved[2];
} nk_text_input_state;

/* Returns a borrowed UTF-8 view valid until nk_event_release(). */
NK_API nk_result NK_CALL nk_text_edit_event_text(const nk_event *event,
                                                 const char **out_text,
                                                 uint32_t *out_length);

typedef struct nk_pointer_move_event {
    double x;
    double y;
} nk_pointer_move_event;

typedef struct nk_pointer_button_event {
    nk_pointer_button button;
    nk_input_action action;
    nk_modifiers modifiers;
    uint32_t reserved;
    double x;
    double y;
} nk_pointer_button_event;

typedef struct nk_pointer_scroll_event {
    double x;
    double y;
} nk_pointer_scroll_event;

typedef uint32_t nk_touch_action;
enum {
    NK_TOUCH_BEGIN = 1,
    NK_TOUCH_MOVE = 2,
    NK_TOUCH_END = 3,
    NK_TOUCH_CANCEL = 4
};

typedef uint32_t nk_touch_tool;
enum {
    NK_TOUCH_TOOL_FINGER = 1,
    NK_TOUCH_TOOL_STYLUS = 2,
    NK_TOUCH_TOOL_ERASER = 3
};

typedef struct nk_touch_event {
    uint32_t pointer_id;
    nk_touch_action action;
    nk_touch_tool tool;
    nk_modifiers modifiers;
    double x;
    double y;
    float pressure;
    float tilt_x;
    float tilt_y;
    uint32_t reserved;
} nk_touch_event;

typedef uint32_t nk_cursor_shape;
enum {
    NK_CURSOR_ARROW = 1,
    NK_CURSOR_IBEAM = 2,
    NK_CURSOR_CROSSHAIR = 3,
    NK_CURSOR_HAND = 4,
    NK_CURSOR_HORIZONTAL_RESIZE = 5,
    NK_CURSOR_VERTICAL_RESIZE = 6,
    NK_CURSOR_NWSE_RESIZE = 7,
    NK_CURSOR_NESW_RESIZE = 8,
    NK_CURSOR_MOVE = 9,
    NK_CURSOR_NOT_ALLOWED = 10
};

typedef struct nk_cursor_image {
    uint32_t struct_size;
    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t hotspot_x;
    int32_t hotspot_y;
    const void *rgba;
    uint64_t reserved[2];
} nk_cursor_image;

typedef uint32_t nk_cursor_mode;
enum {
    NK_CURSOR_MODE_NORMAL = 0,
    NK_CURSOR_MODE_HIDDEN = 1,
    NK_CURSOR_MODE_CAPTURED = 2,
    NK_CURSOR_MODE_DISABLED = 3
};

/* State queries are UI-thread-only. */
NK_API nk_result NK_CALL nk_key_get_state(nk_handle window, nk_key key,
                                           nk_input_action *out_action);
NK_API nk_result NK_CALL nk_pointer_button_get_state(nk_handle window,
                                                      nk_pointer_button button,
                                                      nk_input_action *out_action);
NK_API nk_result NK_CALL nk_pointer_get_position(nk_handle window, double *out_x, double *out_y);

/* Synchronizes a custom editor with the platform IME. Positions are code points. */
NK_API nk_result NK_CALL nk_surface_set_text_input_state(
    nk_handle surface, const nk_text_input_state *state);
/* Activates or deactivates the software keyboard for a custom graphics surface. */
NK_API nk_result NK_CALL nk_surface_set_text_input_active(nk_handle surface, uint32_t active);

NK_API nk_result NK_CALL nk_cursor_create_standard(nk_cursor_shape shape,
                                                   nk_handle *out_cursor);
/* Pixel data is copied before return and must be RGBA8, top row first. */
NK_API nk_result NK_CALL nk_cursor_create_custom(const nk_cursor_image *image,
                                                 nk_handle *out_cursor);
NK_API nk_result NK_CALL nk_cursor_destroy(nk_handle cursor);
/* An invalid cursor handle restores the platform default cursor. */
NK_API nk_result NK_CALL nk_window_set_cursor(nk_handle window, nk_handle cursor);
NK_API nk_result NK_CALL nk_window_set_cursor_mode(nk_handle window, nk_cursor_mode mode);
NK_API nk_result NK_CALL nk_window_get_cursor_mode(nk_handle window,
                                                   nk_cursor_mode *out_mode);
NK_API uint32_t NK_CALL nk_raw_pointer_motion_supported(void);

#ifdef __cplusplus
}
#endif

#endif
