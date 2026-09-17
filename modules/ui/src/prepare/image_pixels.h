#ifndef NATIVEKIT_UI_PREPARE_IMAGE_PIXELS_H
#define NATIVEKIT_UI_PREPARE_IMAGE_PIXELS_H

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nkui {

inline uint8_t premultiply_channel(uint8_t channel, uint8_t alpha) {
    return static_cast<uint8_t>((static_cast<uint32_t>(channel) * alpha + 127u) / 255u);
}

inline std::vector<uint8_t> prepare_rgba8_pixels(const std::vector<uint8_t> &source) {
    std::vector<uint8_t> result = source;
    for (std::size_t offset = 0; offset + 3 < result.size(); offset += 4) {
        const uint8_t alpha = result[offset + 3];
        result[offset + 0] = premultiply_channel(result[offset + 0], alpha);
        result[offset + 1] = premultiply_channel(result[offset + 1], alpha);
        result[offset + 2] = premultiply_channel(result[offset + 2], alpha);
    }
    return result;
}

inline std::vector<uint8_t> prepare_alpha8_pixels(const std::vector<uint8_t> &source) {
    std::vector<uint8_t> result(source.size() * 4);
    for (std::size_t index = 0; index < source.size(); ++index) {
        const uint8_t alpha = source[index];
        result[index * 4 + 0] = alpha;
        result[index * 4 + 1] = alpha;
        result[index * 4 + 2] = alpha;
        result[index * 4 + 3] = alpha;
    }
    return result;
}

} // namespace nkui

#endif
