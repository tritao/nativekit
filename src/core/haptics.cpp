#include "nativekit_haptics.h"

#include "core/error.hpp"
#include "core/haptics_internal.hpp"
#include "core/runtime.hpp"

#include <cmath>

#if !defined(NK_BACKEND_WINDOWS) && !defined(NK_BACKEND_ANDROID) && !defined(NK_BACKEND_IOS) && \
    !defined(NK_BACKEND_WEB) && !defined(NK_BACKEND_GTK) && !defined(NK_BACKEND_MACOS)
namespace nk::core::haptics_backend {
nk_result vibrate(const nk_haptic_vibration &) noexcept { return NK_ERROR_UNSUPPORTED; }
nk_result stop_vibration() noexcept { return NK_ERROR_UNSUPPORTED; }
nk_result gamepad_rumble(nk_joystick, const nk_gamepad_rumble_options &) noexcept {
    return NK_ERROR_UNSUPPORTED;
}
nk_result stop_gamepad_rumble(nk_joystick) noexcept { return NK_ERROR_UNSUPPORTED; }
} // namespace nk::core::haptics_backend
#endif

namespace {
nk_result require_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

bool normalized(float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; }
} // namespace

extern "C" {

nk_result NK_CALL nk_haptic_vibrate(const nk_haptic_vibration *options) {
    if (const auto result = require_ui(); result != NK_OK)
        return result;
    if (!options || options->struct_size < sizeof(*options) || !options->duration_ms ||
        !normalized(options->intensity)) {
        nk::core::set_error("invalid haptic vibration options");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto result = nk::core::haptics_backend::vibrate(*options);
    if (result == NK_ERROR_UNSUPPORTED)
        nk::core::set_error("system haptics are unavailable");
    return result;
}

nk_result NK_CALL nk_haptic_stop(void) {
    if (const auto result = require_ui(); result != NK_OK)
        return result;
    const auto result = nk::core::haptics_backend::stop_vibration();
    if (result == NK_ERROR_UNSUPPORTED)
        nk::core::set_error("system haptics are unavailable");
    return result;
}

nk_result NK_CALL nk_gamepad_rumble(nk_joystick joystick,
                                    const nk_gamepad_rumble_options *options) {
    if (const auto result = require_ui(); result != NK_OK)
        return result;
    if (!options || options->struct_size < sizeof(*options) || !normalized(options->low_frequency) ||
        !normalized(options->high_frequency) || !options->duration_ms) {
        nk::core::set_error("invalid gamepad rumble options");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto result = nk::core::haptics_backend::gamepad_rumble(joystick, *options);
    if (result == NK_ERROR_UNSUPPORTED)
        nk::core::set_error("gamepad rumble is unavailable");
    return result;
}

nk_result NK_CALL nk_gamepad_stop_rumble(nk_joystick joystick) {
    if (const auto result = require_ui(); result != NK_OK)
        return result;
    const auto result = nk::core::haptics_backend::stop_gamepad_rumble(joystick);
    if (result == NK_ERROR_UNSUPPORTED)
        nk::core::set_error("gamepad rumble is unavailable");
    return result;
}

} // extern "C"
