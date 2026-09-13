#ifndef NATIVEKIT_JOYSTICK_H
#define NATIVEKIT_JOYSTICK_H

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
/* Joystick event types                                                      */
/* ------------------------------------------------------------------------- */

typedef uint32_t nk_joystick_hat_flags;
enum NK_FLAGS(nk_joystick_hat_flags) {
    /** The hat is centered; no direction is active. */
    NK_JOYSTICK_HAT_CENTERED = 0,
    /** The hat is pressed upward. */
    NK_JOYSTICK_HAT_UP = 1,
    /** The hat is pressed to the right. */
    NK_JOYSTICK_HAT_RIGHT = 2,
    /** The hat is pressed downward. */
    NK_JOYSTICK_HAT_DOWN = 4,
    /** The hat is pressed to the left. */
    NK_JOYSTICK_HAT_LEFT = 8
};

/** Payload of NK_EVENT_JOYSTICK_AXIS. */
typedef struct nk_joystick_axis_event {
    /** Zero-based raw joystick axis index. */
    uint32_t axis;
    /** Axis value normalized to [-1, 1]. */
    float value;
} nk_joystick_axis_event;

/** Payload of NK_EVENT_JOYSTICK_BUTTON. */
typedef struct nk_joystick_button_event {
    /** Zero-based raw joystick button index. */
    uint32_t button;
    /** 1 while the button is pressed, and 0 when it is released. */
    nk_bool pressed;
} nk_joystick_button_event;

/** Payload of NK_EVENT_JOYSTICK_HAT. */
typedef struct nk_joystick_hat_event {
    /** Zero-based raw joystick hat index. */
    uint32_t hat;
    /** Bitwise OR of NK_JOYSTICK_HAT_* direction values. */
    nk_joystick_hat_flags value;
} nk_joystick_hat_event;

/* ------------------------------------------------------------------------- */
/* Joystick enumeration and state                                            */
/* ------------------------------------------------------------------------- */

/**
 * Writes connected joystick handles. Pass NULL to query the required count.
 * Hotplug is reported with NK_EVENT_JOYSTICK_CONNECTED and
 * NK_EVENT_JOYSTICK_DISCONNECTED.
 */
NK_API nk_result NK_CALL nk_joystick_list(nk_handle *joysticks, uint32_t *inout_count);
/** Copies the device name into a caller-owned UTF-8 buffer. */
NK_API nk_result NK_CALL nk_joystick_get_name(nk_handle joystick, char *buffer,
                                              uint32_t *inout_size);
/** Returns a 32-character SDL-compatible device GUID plus a trailing NUL. */
NK_API nk_result NK_CALL nk_joystick_get_guid(nk_handle joystick, char *buffer,
                                              uint32_t *inout_size);
/**
 * Each state function accepts NULL to query its element count. Axis values are
 * normalized to [-1, 1], buttons are 0 or 1, and hats use NK_JOYSTICK_HAT_*.
 */
NK_API nk_result NK_CALL nk_joystick_get_axes(nk_handle joystick, float *axes,
                                              uint32_t *inout_count);
/** Copies the current button states as zero or one bytes. */
NK_API nk_result NK_CALL nk_joystick_get_buttons(nk_handle joystick, uint8_t *buttons,
                                                 uint32_t *inout_count);
/** Copies the current hat direction flags. */
NK_API nk_result NK_CALL nk_joystick_get_hats(nk_handle joystick, uint8_t *hats,
                                              uint32_t *inout_count);

/* ------------------------------------------------------------------------- */
/* Joystick diagnostics                                                      */
/* ------------------------------------------------------------------------- */

/**
 * Returns a transport warning, such as a missing /dev/input directory,
 * insufficient device permissions, or unavailable inotify monitoring. An
 * empty string means that no transport problem has been observed.
 */
NK_API nk_result NK_CALL nk_joystick_get_diagnostics(char *buffer, uint32_t *inout_size);

#ifdef __cplusplus
}
#endif

#endif
