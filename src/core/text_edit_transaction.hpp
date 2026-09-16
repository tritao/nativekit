#ifndef NATIVEKIT_CORE_TEXT_EDIT_TRANSACTION_HPP
#define NATIVEKIT_CORE_TEXT_EDIT_TRANSACTION_HPP

#include "nativekit_input.h"

#include <string>

namespace nk::core {

/** One normalized native-to-editor text mutation and resulting metadata. */
struct TextEditTransaction {
    nk_text_edit_action action = NK_TEXT_EDIT_SET_SELECTION;
    nk_text_position replacement_start = NK_TEXT_POSITION_NONE;
    nk_text_position replacement_end = NK_TEXT_POSITION_NONE;
    std::string replacement_text;
    nk_text_position selection_start = NK_TEXT_POSITION_NONE;
    nk_text_position selection_end = NK_TEXT_POSITION_NONE;
    nk_text_position composition_start = NK_TEXT_POSITION_NONE;
    nk_text_position composition_end = NK_TEXT_POSITION_NONE;

    bool valid() const noexcept {
        if (selection_start == NK_TEXT_POSITION_NONE || selection_end == NK_TEXT_POSITION_NONE ||
            selection_start > selection_end)
            return false;

        const bool has_replacement = replacement_start != NK_TEXT_POSITION_NONE &&
                                     replacement_end != NK_TEXT_POSITION_NONE;
        if ((replacement_start == NK_TEXT_POSITION_NONE) !=
                (replacement_end == NK_TEXT_POSITION_NONE) ||
            (has_replacement && replacement_start > replacement_end))
            return false;

        const bool has_composition = composition_start != NK_TEXT_POSITION_NONE &&
                                     composition_end != NK_TEXT_POSITION_NONE;
        if ((composition_start == NK_TEXT_POSITION_NONE) !=
                (composition_end == NK_TEXT_POSITION_NONE) ||
            (has_composition && composition_start > composition_end))
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
