#pragma once

#include "nativekit.h"

namespace nk::core::gamepad_events {
void update(nk_handle joystick, bool emit_changes) noexcept;
void disconnect(nk_handle joystick) noexcept;
void reset() noexcept;
} // namespace nk::core::gamepad_events
