#pragma once

#include "nativekit.h"
#include "nativekit_gamepad.h"

struct ANativeWindow;

namespace nk::backend {

nk_result android_vulkan_window(nk_handle surface, ANativeWindow **out_window,
                                bool require_ready);
bool android_standard_gamepad(nk_handle joystick);
nk_result android_gamepad_state(nk_handle joystick, nk_gamepad_state *out_state);

} // namespace nk::backend
