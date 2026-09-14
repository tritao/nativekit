#ifndef NATIVEKIT_CORE_GRAPHICS_FRAME_TARGET_HPP
#define NATIVEKIT_CORE_GRAPHICS_FRAME_TARGET_HPP

#include "nativekit_graphics.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

namespace nk::core {

/** The original descriptor prefix, ending immediately before appended tokens. */
constexpr uint32_t surface_frame_target_v1_size =
    static_cast<uint32_t>(offsetof(nk_surface_frame_target, native_device));

inline bool surface_frame_target_output_valid(const nk_surface_frame_target *out_target) {
    return out_target && out_target->struct_size >= surface_frame_target_v1_size;
}

/** Writes only the descriptor bytes requested by the caller's struct_size. */
inline void write_surface_frame_target(nk_surface_frame_target *out_target,
                                       nk_surface_frame_target target) {
    const uint32_t requested_size = out_target->struct_size;
    target.struct_size = requested_size;
    std::memcpy(out_target, &target,
                std::min<size_t>(requested_size, sizeof(nk_surface_frame_target)));
}

} // namespace nk::core

#endif
