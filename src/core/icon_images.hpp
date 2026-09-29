#pragma once

#include "nativekit_window.h"

#include <cstdint>

namespace nk::core {

inline bool valid_icon_images(const uint8_t *pixels, uint32_t byte_count,
                              const nk_icon_image *images, uint32_t image_count) {
    if (!pixels || !images || image_count == 0 || image_count > 16)
        return false;
    for (uint32_t index = 0; index < image_count; ++index) {
        const auto &image = images[index];
        if (image.width <= 0 || image.height <= 0 || image.width > INT32_MAX / 4 ||
            image.stride < image.width * 4 ||
            static_cast<uint64_t>(image.offset) +
                    static_cast<uint64_t>(image.stride) * image.height > byte_count)
            return false;
    }
    return true;
}

} // namespace nk::core
