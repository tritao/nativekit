#pragma once

#include "nativekit_gamepad.h"

namespace nk::ios_joystick {
void pump() noexcept;
void shutdown() noexcept;
bool standard_gamepad(nk_handle handle) noexcept;
nk_result standard_gamepad_state(nk_handle handle, nk_gamepad_state *out_state) noexcept;
} // namespace nk::ios_joystick
