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

enum {
    NK_JOYSTICK_HAT_CENTERED = 0,
    NK_JOYSTICK_HAT_UP = 1,
    NK_JOYSTICK_HAT_RIGHT = 2,
    NK_JOYSTICK_HAT_DOWN = 4,
    NK_JOYSTICK_HAT_LEFT = 8
};

typedef struct nk_joystick_axis_event {
    uint32_t axis;
    float value;
} nk_joystick_axis_event;

typedef struct nk_joystick_button_event {
    uint32_t button;
    /** 1 while the button is pressed, and 0 when it is released. */
    nk_bool pressed;
} nk_joystick_button_event;

typedef struct nk_joystick_hat_event {
    uint32_t hat;
    uint32_t value;
} nk_joystick_hat_event;

/* ------------------------------------------------------------------------- */
/* Joystick enumeration and state                                            */
/* ------------------------------------------------------------------------- */

/*
 * Writes connected joystick handles. Pass NULL to query the required count.
 * Hotplug is reported with NK_EVENT_JOYSTICK_CONNECTED and
 * NK_EVENT_JOYSTICK_DISCONNECTED.
 */
NK_API nk_result NK_CALL nk_joystick_list(nk_handle *joysticks, uint32_t *inout_count);
NK_API nk_result NK_CALL nk_joystick_get_name(nk_handle joystick, char *buffer,
                                              uint32_t *inout_size);
/* Returns a 32-character SDL-compatible device GUID plus a trailing NUL. */
NK_API nk_result NK_CALL nk_joystick_get_guid(nk_handle joystick, char *buffer,
                                              uint32_t *inout_size);
/*
 * Each state function accepts NULL to query its element count. Axis values are
 * normalized to [-1, 1], buttons are 0 or 1, and hats use NK_JOYSTICK_HAT_*.
 */
NK_API nk_result NK_CALL nk_joystick_get_axes(nk_handle joystick, float *axes,
                                              uint32_t *inout_count);
NK_API nk_result NK_CALL nk_joystick_get_buttons(nk_handle joystick, uint8_t *buttons,
                                                 uint32_t *inout_count);
NK_API nk_result NK_CALL nk_joystick_get_hats(nk_handle joystick, uint8_t *hats,
                                              uint32_t *inout_count);

/* ------------------------------------------------------------------------- */
/* Joystick diagnostics                                                      */
/* ------------------------------------------------------------------------- */

/*
 * Returns a transport warning, such as a missing /dev/input directory,
 * insufficient device permissions, or unavailable inotify monitoring. An
 * empty string means that no transport problem has been observed.
 */
NK_API nk_result NK_CALL nk_joystick_get_diagnostics(char *buffer, uint32_t *inout_size);

#ifdef __cplusplus
}
#endif

#endif
