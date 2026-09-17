#ifndef NATIVEKIT_CORE_TEXT_INPUT_GEOMETRY_HPP
#define NATIVEKIT_CORE_TEXT_INPUT_GEOMETRY_HPP

#include "nativekit_input.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace nk::core {

struct TextInputGeometry {
    nk_text_position selection_start = 0;
    nk_text_position selection_end = 0;
    nk_text_position composition_start = NK_TEXT_POSITION_NONE;
    nk_text_position composition_end = NK_TEXT_POSITION_NONE;
    std::vector<nk_text_input_rect> selection_rects;
    std::vector<nk_text_input_rect> composition_rects;
};

/**
 * Selects the best platform anchor for candidate or composition UI.
 *
 * Composition geometry takes precedence because it identifies the active IME
 * clause. A non-collapsed selection is the next best anchor, with the
 * published caret rectangle as the fallback for collapsed selections and
 * backends that do not receive range geometry.
 */
inline nk_text_input_rect text_input_anchor_rect(
    const nk_text_input_state &state, const std::vector<nk_text_input_rect> &selection_rects,
    const std::vector<nk_text_input_rect> &composition_rects) noexcept {
    nk_text_input_rect fallback{sizeof(nk_text_input_rect), state.cursor_x, state.cursor_y,
                                state.cursor_width, state.cursor_height};
    if (state.composition_start != NK_TEXT_POSITION_NONE &&
        state.composition_end != NK_TEXT_POSITION_NONE && !composition_rects.empty())
        return composition_rects.front();
    if (state.selection_start != state.selection_end && !selection_rects.empty())
        return selection_rects.front();
    return fallback;
}

inline bool decode_text_input_rects(const uint8_t *bytes, uint32_t byte_count,
                                    std::vector<nk_text_input_rect> *out) noexcept {
    if (!out || (byte_count != 0 && !bytes) || byte_count % sizeof(nk_text_input_rect) != 0)
        return false;
    try {
        out->resize(byte_count / sizeof(nk_text_input_rect));
    } catch (...) {
        return false;
    }
    for (std::size_t index = 0; index < out->size(); ++index) {
        nk_text_input_rect rect{};
        std::memcpy(&rect, bytes + index * sizeof(rect), sizeof(rect));
        if (rect.struct_size < sizeof(rect) || !std::isfinite(rect.x) ||
            !std::isfinite(rect.y) || !std::isfinite(rect.width) ||
            !std::isfinite(rect.height) || rect.width < 0.0f || rect.height < 0.0f)
            return false;
        (*out)[index] = rect;
    }
    return true;
}

inline bool decode_text_input_geometry(
    nk_text_position selection_start, nk_text_position selection_end,
    nk_text_position composition_start, nk_text_position composition_end,
    const uint8_t *selection_rects, uint32_t selection_rect_bytes,
    const uint8_t *composition_rects, uint32_t composition_rect_bytes,
    TextInputGeometry *out) noexcept {
    if (!out || selection_start > selection_end) return false;
    const bool no_composition = composition_start == NK_TEXT_POSITION_NONE &&
                                composition_end == NK_TEXT_POSITION_NONE;
    const bool valid_composition = composition_start != NK_TEXT_POSITION_NONE &&
                                   composition_end != NK_TEXT_POSITION_NONE &&
                                   composition_start <= composition_end;
    if (!no_composition && !valid_composition) return false;
    TextInputGeometry decoded;
    decoded.selection_start = selection_start;
    decoded.selection_end = selection_end;
    decoded.composition_start = composition_start;
    decoded.composition_end = composition_end;
    if (!decode_text_input_rects(selection_rects, selection_rect_bytes,
                                 &decoded.selection_rects) ||
        !decode_text_input_rects(composition_rects, composition_rect_bytes,
                                 &decoded.composition_rects))
        return false;
    try {
        *out = std::move(decoded);
    } catch (...) {
        return false;
    }
    return true;
}

inline bool text_input_geometry_matches_state(const TextInputGeometry &geometry,
                                              const nk_text_input_state &state) noexcept {
    return geometry.selection_start == state.selection_start &&
           geometry.selection_end == state.selection_end &&
           geometry.composition_start == state.composition_start &&
           geometry.composition_end == state.composition_end;
}

} // namespace nk::core

#endif
