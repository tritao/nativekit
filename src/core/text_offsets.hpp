#ifndef NATIVEKIT_CORE_TEXT_OFFSETS_HPP
#define NATIVEKIT_CORE_TEXT_OFFSETS_HPP

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace nk::core {

/** Cached UTF-8/code-point/UTF-16 boundaries for one platform text window. */
struct TextOffsetMap {
    bool assign(std::string_view text) noexcept {
        try {
            utf8_offsets.clear();
            utf16_offsets.clear();
            if (text.size() < std::numeric_limits<std::size_t>::max()) {
                utf8_offsets.reserve(text.size() + 1);
                utf16_offsets.reserve(text.size() + 1);
            }
            utf8_offsets.push_back(0);
            utf16_offsets.push_back(0);
        } catch (...) {
            clear();
            return false;
        }

        std::size_t offset = 0;
        std::size_t utf16 = 0;
        while (offset < text.size()) {
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
                return invalid();
            }
            if (length > text.size() - offset)
                return invalid();
            for (std::size_t index = 1; index < length; ++index) {
                const auto next = static_cast<uint8_t>(text[offset + index]);
                if ((next & 0xc0u) != 0x80u)
                    return invalid();
                codepoint = (codepoint << 6) | (next & 0x3fu);
            }
            if ((length == 2 && codepoint < 0x80u) || (length == 3 && codepoint < 0x800u) ||
                (length == 4 && codepoint < 0x10000u) || codepoint > 0x10ffffu ||
                (codepoint >= 0xd800u && codepoint <= 0xdfffu))
                return invalid();
            const std::size_t units = codepoint > 0xffffu ? 2u : 1u;
            if (utf16 > std::numeric_limits<std::size_t>::max() - units)
                return invalid();
            offset += length;
            utf16 += units;
            try {
                utf8_offsets.push_back(offset);
                utf16_offsets.push_back(utf16);
            } catch (...) {
                clear();
                return false;
            }
        }
        return true;
    }

    void clear() noexcept {
        utf8_offsets.clear();
        utf16_offsets.clear();
    }

    bool valid() const noexcept {
        return !utf8_offsets.empty() && utf8_offsets.size() == utf16_offsets.size();
    }

    std::size_t codepointCount() const noexcept { return valid() ? utf8_offsets.size() - 1 : 0; }

    bool utf8ByteOffset(uint32_t codepoint, std::size_t *out) const noexcept {
        if (!out || !valid() || codepoint >= utf8_offsets.size())
            return false;
        *out = utf8_offsets[codepoint];
        return true;
    }

    bool utf16CodeUnitOffset(uint32_t codepoint, std::size_t *out) const noexcept {
        if (!out || !valid() || codepoint >= utf16_offsets.size())
            return false;
        *out = utf16_offsets[codepoint];
        return true;
    }

    /** Converts only exact UTF-16 boundaries; positions inside a surrogate pair fail. */
    bool codepointOffsetForUtf16(std::size_t utf16, uint32_t *out) const noexcept {
        if (!out || !valid())
            return false;
        const auto found = std::lower_bound(utf16_offsets.begin(), utf16_offsets.end(), utf16);
        if (found == utf16_offsets.end() || *found != utf16)
            return false;
        *out = static_cast<uint32_t>(found - utf16_offsets.begin());
        return true;
    }

  private:
    bool invalid() noexcept {
        clear();
        return false;
    }

    std::vector<std::size_t> utf8_offsets;
    std::vector<std::size_t> utf16_offsets;
};

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
