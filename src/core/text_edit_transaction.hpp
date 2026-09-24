#ifndef NATIVEKIT_CORE_TEXT_EDIT_TRANSACTION_HPP
#define NATIVEKIT_CORE_TEXT_EDIT_TRANSACTION_HPP

#include "nativekit_input.h"

namespace nk::core {

/** Infers the default edit grouping from its shape and the previous selection. */
inline nk_text_edit_history_kind infer_text_edit_history_kind(
    nk_text_edit_action action, nk_text_position current_selection_start,
    nk_text_position current_selection_end, nk_text_position replacement_start,
    nk_text_position replacement_end, bool composition_active = false) noexcept {
    if (composition_active && action == NK_TEXT_EDIT_COMMIT)
        return NK_TEXT_EDIT_HISTORY_COMPOSITION;

    switch (action) {
    case NK_TEXT_EDIT_COMPOSE:
        return NK_TEXT_EDIT_HISTORY_COMPOSITION;
    case NK_TEXT_EDIT_COMMIT:
        if (current_selection_start == NK_TEXT_POSITION_NONE ||
            current_selection_end == NK_TEXT_POSITION_NONE)
            return NK_TEXT_EDIT_HISTORY_GENERIC;
        return current_selection_start == current_selection_end &&
                       replacement_start == current_selection_start &&
                       replacement_end == current_selection_end
                   ? NK_TEXT_EDIT_HISTORY_TYPING
                   : NK_TEXT_EDIT_HISTORY_GENERIC;
    case NK_TEXT_EDIT_DELETE:
        if (current_selection_start == NK_TEXT_POSITION_NONE ||
            current_selection_end == NK_TEXT_POSITION_NONE ||
            current_selection_start != current_selection_end)
            return NK_TEXT_EDIT_HISTORY_GENERIC;
        if (replacement_end == current_selection_start)
            return NK_TEXT_EDIT_HISTORY_DELETE_BACKWARD;
        if (replacement_start == current_selection_start)
            return NK_TEXT_EDIT_HISTORY_DELETE_FORWARD;
        return NK_TEXT_EDIT_HISTORY_GENERIC;
    default:
        return NK_TEXT_EDIT_HISTORY_GENERIC;
    }
}

} // namespace nk::core

#endif
