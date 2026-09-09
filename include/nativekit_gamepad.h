#ifndef NATIVEKIT_GAMEPAD_H
#define NATIVEKIT_GAMEPAD_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nk_gamepad_button;
enum {
    NK_GAMEPAD_BUTTON_A = 0,
    NK_GAMEPAD_BUTTON_B,
    NK_GAMEPAD_BUTTON_X,
    NK_GAMEPAD_BUTTON_Y,
    NK_GAMEPAD_BUTTON_LEFT_BUMPER,
    NK_GAMEPAD_BUTTON_RIGHT_BUMPER,
    NK_GAMEPAD_BUTTON_BACK,
    NK_GAMEPAD_BUTTON_START,
    NK_GAMEPAD_BUTTON_GUIDE,
    NK_GAMEPAD_BUTTON_LEFT_THUMB,
    NK_GAMEPAD_BUTTON_RIGHT_THUMB,
    NK_GAMEPAD_BUTTON_DPAD_UP,
    NK_GAMEPAD_BUTTON_DPAD_RIGHT,
    NK_GAMEPAD_BUTTON_DPAD_DOWN,
    NK_GAMEPAD_BUTTON_DPAD_LEFT,
    NK_GAMEPAD_BUTTON_COUNT
};

typedef uint32_t nk_gamepad_axis;
enum {
    NK_GAMEPAD_AXIS_LEFT_X = 0,
    NK_GAMEPAD_AXIS_LEFT_Y,
    NK_GAMEPAD_AXIS_RIGHT_X,
    NK_GAMEPAD_AXIS_RIGHT_Y,
    NK_GAMEPAD_AXIS_LEFT_TRIGGER,
    NK_GAMEPAD_AXIS_RIGHT_TRIGGER,
    NK_GAMEPAD_AXIS_COUNT
};

typedef struct nk_gamepad_state {
    uint32_t struct_size;
    uint8_t buttons[NK_GAMEPAD_BUTTON_COUNT];
    float axes[NK_GAMEPAD_AXIS_COUNT];
    uint64_t reserved[4];
} nk_gamepad_state;

typedef struct nk_gamepad_axis_event {
    nk_gamepad_axis axis;
    float value;
} nk_gamepad_axis_event;

typedef struct nk_gamepad_button_event {
    nk_gamepad_button button;
    uint32_t pressed;
} nk_gamepad_button_event;

typedef uint32_t nk_gamepad_flags;
enum { NK_GAMEPAD_TRIGGER_ZERO_TO_ONE = 1u << 0 };

typedef struct nk_gamepad_options {
    uint32_t struct_size;
    float stick_dead_zone;
    float trigger_dead_zone;
    nk_gamepad_flags flags;
    uint32_t reserved;
    uint64_t reserved2[2];
} nk_gamepad_options;

typedef uint32_t nk_gamepad_mapping_source;
enum {
    NK_GAMEPAD_MAPPING_BUILT_IN = 1,
    NK_GAMEPAD_MAPPING_APPLICATION = 2
};

/* Adds or replaces one SDL/GLFW controller mapping for its 32-character GUID. */
NK_API nk_result NK_CALL nk_gamepad_add_mapping(const char *mapping);
/*
 * Adds newline-separated mappings, ignoring blank lines, comments, and entries
 * for other platforms. The update is atomic if any applicable line is invalid.
 */
NK_API nk_result NK_CALL nk_gamepad_add_mappings(const char *database,
                                                 uint32_t *out_added);
NK_API nk_result NK_CALL nk_gamepad_is_mapped(nk_handle joystick, uint32_t *out_mapped);
NK_API nk_result NK_CALL
nk_gamepad_get_mapping_source(nk_handle joystick, nk_gamepad_mapping_source *out_source);
/* Returns the pinned SDL_GameControllerDB Git revision used for built-ins. */
NK_API nk_result NK_CALL nk_gamepad_get_builtin_database_revision(char *buffer,
                                                                  uint32_t *inout_size);
NK_API nk_result NK_CALL nk_gamepad_get_name(nk_handle joystick, char *buffer,
                                             uint32_t *inout_size);
NK_API nk_result NK_CALL nk_gamepad_get_state(nk_handle joystick,
                                              nk_gamepad_state *out_state);
/* Sets process-wide normalization used by state queries and gamepad events. */
NK_API nk_result NK_CALL nk_gamepad_set_options(const nk_gamepad_options *options);
NK_API nk_result NK_CALL nk_gamepad_get_options(nk_gamepad_options *out_options);

#ifdef __cplusplus
}
#endif

#endif
