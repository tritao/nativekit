#pragma once

#include "nativekit_gamepad.h"
#include "nativekit_haptics.h"

namespace nk::core::haptics_backend {
nk_result vibrate(const nk_haptic_vibration &options) noexcept;
nk_result stop_vibration() noexcept;
nk_result gamepad_rumble(nk_joystick joystick, const nk_gamepad_rumble_options &options) noexcept;
nk_result stop_gamepad_rumble(nk_joystick joystick) noexcept;
} // namespace nk::core::haptics_backend
