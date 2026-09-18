#ifndef NATIVEKIT_CORE_TEXT_INPUT_CONTRACT_HPP
#define NATIVEKIT_CORE_TEXT_INPUT_CONTRACT_HPP

#include "nativekit_input.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

namespace nk::core {

struct TextInputStateValidation {
    uint32_t text_codepoints = 0;
    nk_text_position text_end = 0;
};

/** Counts Unicode scalar values and rejects malformed UTF-8. */
inline bool decode_utf8_codepoint_count(std::string_view text, uint32_t *out_count) noexcept {
    if (!out_count)
        return false;
    uint64_t count = 0;
    for (std::size_t offset = 0; offset < text.size();) {
        const auto first = static_cast<uint8_t>(text[offset]);
        uint32_t codepoint = 0;
        std::size_t length = 0;
        if (first <= 0x7fu) {
            codepoint = first;
            length = 1;
        } else if (first >= 0xc2u && first <= 0xdfu) {
            codepoint = first & 0x1fu;
            length = 2;
        } else if (first >= 0xe0u && first <= 0xefu) {
            codepoint = first & 0x0fu;
            length = 3;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            codepoint = first & 0x07u;
            length = 4;
        } else {
            return false;
        }
        if (length > text.size() - offset)
            return false;
        for (std::size_t index = 1; index < length; ++index) {
            const auto next = static_cast<uint8_t>(text[offset + index]);
            if ((next & 0xc0u) != 0x80u)
                return false;
            codepoint = (codepoint << 6) | (next & 0x3fu);
        }
        if ((length == 2 && codepoint < 0x80u) || (length == 3 && codepoint < 0x800u) ||
            (length == 4 && codepoint < 0x10000u) || codepoint > 0x10ffffu ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu))
            return false;
        if (++count > std::numeric_limits<uint32_t>::max())
            return false;
        offset += length;
    }
    *out_count = static_cast<uint32_t>(count);
    return true;
}

/** Validates a platform text-input window and its absolute ranges. */
inline bool validate_text_input_state(const nk_text_input_state &state, std::string_view text,
                                      TextInputStateValidation *out = nullptr) noexcept {
    if (state.struct_size < sizeof(state))
        return false;
    uint32_t text_codepoints = 0;
    if (!decode_utf8_codepoint_count(text, &text_codepoints))
        return false;
    const uint64_t text_end = static_cast<uint64_t>(state.text_start) + text_codepoints;
    if (text_end > std::numeric_limits<nk_text_position>::max())
        return false;
    const bool no_composition = state.composition_start == NK_TEXT_POSITION_NONE &&
                                state.composition_end == NK_TEXT_POSITION_NONE;
    const bool valid_composition = state.composition_start != NK_TEXT_POSITION_NONE &&
                                   state.composition_end != NK_TEXT_POSITION_NONE &&
                                   state.composition_start <= state.composition_end &&
                                   state.composition_start >= state.text_start &&
                                   state.composition_end <= text_end;
    const bool valid_cursor = std::isfinite(state.cursor_x) && std::isfinite(state.cursor_y) &&
                              std::isfinite(state.cursor_width) &&
                              std::isfinite(state.cursor_height) && state.cursor_width >= 0.0f &&
                              state.cursor_height >= 0.0f;
    if (state.text_start > state.document_length || text_end > state.document_length ||
        state.selection_start > state.selection_end || state.selection_start < state.text_start ||
        state.selection_end > text_end || (!no_composition && !valid_composition) ||
        (state.flags & ~(NK_TEXT_INPUT_MULTILINE | NK_TEXT_INPUT_AUTOCORRECT |
                         NK_TEXT_INPUT_CAPITALIZE_SENTENCES)) != 0 ||
        state.input_type > NK_TEXT_INPUT_PASSWORD || state.action > NK_TEXT_INPUT_ACTION_NONE ||
        !valid_cursor)
        return false;
    if (out) {
        out->text_codepoints = text_codepoints;
        out->text_end = static_cast<nk_text_position>(text_end);
    }
    return true;
}

} // namespace nk::core

#endif
