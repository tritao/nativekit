#ifndef NATIVEKIT_GAMEPAD_H
#define NATIVEKIT_GAMEPAD_H

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
/* Gamepad buttons, axes, and state                                          */
/* ------------------------------------------------------------------------- */

/** Canonical button index in nk_gamepad_state and gamepad events. */
typedef uint32_t nk_gamepad_button;
enum NK_ENUM(nk_gamepad_button) {
    /** South face button. */
    NK_GAMEPAD_BUTTON_A = 0,
    /** East face button. */
    NK_GAMEPAD_BUTTON_B,
    /** West face button. */
    NK_GAMEPAD_BUTTON_X,
    /** North face button. */
    NK_GAMEPAD_BUTTON_Y,
    /** Left shoulder button. */
    NK_GAMEPAD_BUTTON_LEFT_BUMPER,
    /** Right shoulder button. */
    NK_GAMEPAD_BUTTON_RIGHT_BUMPER,
    /** Back, select, or view button. */
    NK_GAMEPAD_BUTTON_BACK,
    /** Start, menu, or pause button. */
    NK_GAMEPAD_BUTTON_START,
    /** Guide, home, or platform button. */
    NK_GAMEPAD_BUTTON_GUIDE,
    /** Left stick click. */
    NK_GAMEPAD_BUTTON_LEFT_THUMB,
    /** Right stick click. */
    NK_GAMEPAD_BUTTON_RIGHT_THUMB,
    /** Up on the D-pad. */
    NK_GAMEPAD_BUTTON_DPAD_UP,
    /** Right on the D-pad. */
    NK_GAMEPAD_BUTTON_DPAD_RIGHT,
    /** Down on the D-pad. */
    NK_GAMEPAD_BUTTON_DPAD_DOWN,
    /** Left on the D-pad. */
    NK_GAMEPAD_BUTTON_DPAD_LEFT,
    /** Number of canonical buttons. */
    NK_GAMEPAD_BUTTON_COUNT
};

/** Canonical axis index in nk_gamepad_state and gamepad events. */
typedef uint32_t nk_gamepad_axis;
enum NK_ENUM(nk_gamepad_axis) {
    /** Left stick horizontal axis. */
    NK_GAMEPAD_AXIS_LEFT_X = 0,
    /** Left stick vertical axis. */
    NK_GAMEPAD_AXIS_LEFT_Y,
    /** Right stick horizontal axis. */
    NK_GAMEPAD_AXIS_RIGHT_X,
    /** Right stick vertical axis. */
    NK_GAMEPAD_AXIS_RIGHT_Y,
    /** Left trigger axis. */
    NK_GAMEPAD_AXIS_LEFT_TRIGGER,
    /** Right trigger axis. */
    NK_GAMEPAD_AXIS_RIGHT_TRIGGER,
    /** Number of canonical axes. */
    NK_GAMEPAD_AXIS_COUNT
};

/** Normalized canonical gamepad state for one mapped joystick. */
typedef struct nk_gamepad_state {
    /** Set to sizeof(nk_gamepad_state) before requesting state. */
    uint32_t struct_size;
    /** Canonical button states, each normalized to zero or one. */
    uint8_t buttons[NK_GAMEPAD_BUTTON_COUNT];
    /** Canonical axis values, normally normalized to [-1, 1]. */
    float axes[NK_GAMEPAD_AXIS_COUNT];
    /** Reserved for future state data; set all elements to zero. */
    uint64_t reserved[4];
} nk_gamepad_state;

/** Payload of NK_EVENT_GAMEPAD_AXIS. */
typedef struct nk_gamepad_axis_event {
    /** Canonical axis index. */
    nk_gamepad_axis axis;
    /** Normalized axis value. */
    float value;
} nk_gamepad_axis_event;

/** Payload of NK_EVENT_GAMEPAD_BUTTON. */
typedef struct nk_gamepad_button_event {
    /** Canonical button index. */
    nk_gamepad_button button;
    /** 1 while the button is pressed, and 0 when it is released. */
    nk_bool pressed;
} nk_gamepad_button_event;

/** Options controlling canonical gamepad normalization. */
typedef uint32_t nk_gamepad_flags;
enum NK_FLAGS(nk_gamepad_flags) {
    /** Report trigger axes in [0, 1] instead of the default [-1, 1]. */
    NK_GAMEPAD_TRIGGER_ZERO_TO_ONE = 1u << 0
};

/** Process-wide normalization options for mapped gamepads. */
typedef struct nk_gamepad_options {
    /** Set to sizeof(nk_gamepad_options) before setting or getting options. */
    uint32_t struct_size;
    /** Radial dead-zone fraction for both sticks, in [0, 1). */
    float stick_dead_zone;
    /** Dead-zone fraction for both triggers, in [0, 1). */
    float trigger_dead_zone;
    /** Bitwise OR of NK_GAMEPAD_* flags. */
    nk_gamepad_flags flags;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for future normalization options; set all elements to zero. */
    uint64_t reserved2[2];
} nk_gamepad_options;

/* ------------------------------------------------------------------------- */
/* Gamepad mapping and query APIs                                            */
/* ------------------------------------------------------------------------- */

/** Origin of the mapping currently selected for a joystick. */
typedef uint32_t nk_gamepad_mapping_source;
enum NK_ENUM(nk_gamepad_mapping_source) {
    /** Mapping came from NativeKit's built-in database. */
    NK_GAMEPAD_MAPPING_BUILT_IN = 1,
    /** Mapping was supplied by the application. */
    NK_GAMEPAD_MAPPING_APPLICATION = 2
};

/** Adds or replaces one SDL/GLFW controller mapping for its 32-character GUID. */
NK_API nk_result NK_CALL nk_gamepad_add_mapping(const char *mapping NK_UTF8);
/**
 * Adds newline-separated mappings, ignoring blank lines, comments, and entries
 * for other platforms. The update is atomic if any applicable line is invalid.
 */
NK_API nk_result NK_CALL nk_gamepad_add_mappings(const char *database NK_UTF8, uint32_t *out_added);
/** Reports whether a joystick has an active canonical gamepad mapping. */
NK_API nk_result NK_CALL nk_gamepad_is_mapped(nk_handle joystick, nk_bool *out_mapped);
/** Reports whether the active mapping is built-in or application-supplied. */
NK_API nk_result NK_CALL nk_gamepad_get_mapping_source(nk_handle joystick,
                                                       nk_gamepad_mapping_source *out_source);
/** Returns the pinned SDL_GameControllerDB Git revision used for built-ins. */
NK_API nk_result NK_CALL nk_gamepad_get_builtin_database_revision(char *buffer,
                                                                  uint32_t *inout_size);
/** Copies the mapped gamepad name into a caller-owned UTF-8 buffer. */
NK_API nk_result NK_CALL nk_gamepad_get_name(nk_handle joystick, char *buffer,
                                             uint32_t *inout_size);
/** Returns the current normalized state of a mapped joystick. */
NK_API nk_result NK_CALL nk_gamepad_get_state(nk_handle joystick, nk_gamepad_state *out_state);
/** Sets process-wide normalization used by state queries and gamepad events. */
NK_API nk_result NK_CALL nk_gamepad_set_options(const nk_gamepad_options *options);
/** Returns the process-wide gamepad normalization options. */
NK_API nk_result NK_CALL nk_gamepad_get_options(nk_gamepad_options *out_options);

#ifdef __cplusplus
}
#endif

#endif
