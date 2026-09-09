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

/* State queries are UI-thread-only. */
NK_API nk_result NK_CALL nk_key_get_state(nk_handle window, nk_key key,
                                           nk_input_action *out_action);
NK_API nk_result NK_CALL nk_pointer_button_get_state(nk_handle window,
                                                      nk_pointer_button button,
                                                      nk_input_action *out_action);
NK_API nk_result NK_CALL nk_pointer_get_position(nk_handle window, double *out_x, double *out_y);

#ifdef __cplusplus
}
#endif

#endif
