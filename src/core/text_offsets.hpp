#ifndef NATIVEKIT_CORE_TEXT_OFFSETS_HPP
#define NATIVEKIT_CORE_TEXT_OFFSETS_HPP

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nk::core {

/**
 * Converts an exact UTF-16 offset to a Unicode code-point offset.
 *
 * Offsets inside a surrogate pair and unpaired surrogate code units are
 * rejected so platform adapters cannot create an editor position that splits
 * one Unicode scalar value.
 */
template <typename Unit>
bool utf16_to_codepoint_offset(std::basic_string_view<Unit> text, std::size_t utf16_offset,
                               uint32_t *codepoint_offset) {
    static_assert(sizeof(Unit) == 2, "UTF-16 mapping requires 16-bit code units");
    if (!codepoint_offset || utf16_offset > text.size())
        return false;
    uint32_t count = 0;
    for (std::size_t index = 0; index < utf16_offset;) {
        const uint32_t value = static_cast<uint32_t>(text[index]);
        if (value >= 0xd800 && value <= 0xdbff) {
            if (index + 1 >= text.size() || static_cast<uint32_t>(text[index + 1]) < 0xdc00 ||
                static_cast<uint32_t>(text[index + 1]) > 0xdfff || index + 1 >= utf16_offset)
                return false;
            index += 2;
        } else if (value >= 0xdc00 && value <= 0xdfff) {
            return false;
        } else {
            ++index;
        }
        if (count == UINT32_MAX)
            return false;
        ++count;
    }
    *codepoint_offset = count;
    return true;
}

} // namespace nk::core

#endif
