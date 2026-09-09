#include "nativekit_input.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#if !defined(NK_BACKEND_GTK)
namespace {
nk_result unsupported_input() {
    nk::core::clear_error();
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    nk::core::set_error("input state queries are not implemented by this backend");
    return NK_ERROR_UNSUPPORTED;
}
} // namespace

extern "C" {

nk_result NK_CALL nk_key_get_state(nk_handle, nk_key, nk_input_action *) {
    return unsupported_input();
}

nk_result NK_CALL nk_pointer_button_get_state(nk_handle, nk_pointer_button, nk_input_action *) {
    return unsupported_input();
}

nk_result NK_CALL nk_pointer_get_position(nk_handle, double *, double *) {
    return unsupported_input();
}

}
#endif
