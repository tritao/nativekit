#ifndef NATIVEKIT_HAPTICS_H
#define NATIVEKIT_HAPTICS_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One replacement system vibration effect. */
typedef struct nk_haptic_vibration {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint32_t period_ms;
    uint32_t duration_ms;
    float intensity;
    uint32_t reserved;
} nk_haptic_vibration;

/** Starts a replacement system vibration effect. */
NK_API nk_result NK_CALL nk_haptic_vibrate(const nk_haptic_vibration *options);
/** Stops the active system vibration effect. */
NK_API nk_result NK_CALL nk_haptic_stop(void);

#ifdef __cplusplus
}
#endif

#endif
