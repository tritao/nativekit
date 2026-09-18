#ifndef NATIVEKIT_CORE_TEXT_EDIT_TRANSACTION_HPP
#define NATIVEKIT_CORE_TEXT_EDIT_TRANSACTION_HPP

#include "nativekit_input.h"

#include <string>

namespace nk::core {

inline constexpr nk_text_position text_position_none =
    static_cast<nk_text_position>(NK_TEXT_POSITION_NONE);

/** One normalized native-to-editor text mutation and resulting metadata. */
struct TextEditTransaction {
    nk_text_edit_action action = NK_TEXT_EDIT_SET_SELECTION;
    nk_text_position replacement_start = text_position_none;
    nk_text_position replacement_end = text_position_none;
    std::string replacement_text;
    nk_text_position selection_start = text_position_none;
    nk_text_position selection_end = text_position_none;
    nk_text_position composition_start = text_position_none;
    nk_text_position composition_end = text_position_none;
    /** Caret affinity for the resulting selection focus. */
    uint32_t selection_affinity = 0;

    bool valid() const noexcept {
        if (selection_start == text_position_none || selection_end == text_position_none ||
            selection_start > selection_end)
            return false;

        const bool has_replacement =
            replacement_start != text_position_none && replacement_end != text_position_none;
        if ((replacement_start == text_position_none) != (replacement_end == text_position_none) ||
            (has_replacement && replacement_start > replacement_end))
            return false;

        const bool has_composition =
            composition_start != text_position_none && composition_end != text_position_none;
        if ((composition_start == text_position_none) != (composition_end == text_position_none) ||
            (has_composition && composition_start > composition_end))
            return false;
        if (selection_affinity > 4u)
            return false;

        switch (action) {
        case NK_TEXT_EDIT_COMPOSE:
            return has_replacement && has_composition;
        case NK_TEXT_EDIT_COMMIT:
            return has_replacement;
        case NK_TEXT_EDIT_DELETE:
            return has_replacement && replacement_text.empty();
        case NK_TEXT_EDIT_SET_SELECTION:
            return !has_replacement && replacement_text.empty();
        case NK_TEXT_EDIT_FINISH_COMPOSITION:
            return !has_replacement && !has_composition && replacement_text.empty();
        case NK_TEXT_EDIT_SET_COMPOSITION:
            return !has_replacement && replacement_text.empty();
        default:
            return false;
        }
    }
};

} // namespace nk::core

#endif
