#include "nativekit_joystick.h"

#include "core/error.hpp"

#if !defined(NK_BACKEND_GTK)
namespace {
nk_result unsupported() {
    nk::core::set_error("joysticks are not supported by this backend");
    return NK_ERROR_UNSUPPORTED;
}
} // namespace

extern "C" {
nk_result NK_CALL nk_joystick_list(nk_handle *, uint32_t *) { return unsupported(); }
nk_result NK_CALL nk_joystick_get_name(nk_handle, char *, uint32_t *) { return unsupported(); }
nk_result NK_CALL nk_joystick_get_guid(nk_handle, char *, uint32_t *) { return unsupported(); }
nk_result NK_CALL nk_joystick_get_axes(nk_handle, float *, uint32_t *) { return unsupported(); }
nk_result NK_CALL nk_joystick_get_buttons(nk_handle, uint8_t *, uint32_t *) {
    return unsupported();
}
nk_result NK_CALL nk_joystick_get_hats(nk_handle, uint8_t *, uint32_t *) {
    return unsupported();
}
}
#endif
