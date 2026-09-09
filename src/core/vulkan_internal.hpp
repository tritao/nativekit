#pragma once

#include "nativekit_window.h"

#include <cstdint>

namespace nk::core::vulkan {
inline const char *platform_extension(nk_native_window_kind kind) noexcept {
    if (kind == NK_NATIVE_WINDOW_X11)
        return "VK_KHR_xlib_surface";
    if (kind == NK_NATIVE_WINDOW_WAYLAND)
        return "VK_KHR_wayland_surface";
    return nullptr;
}
} // namespace nk::core::vulkan
