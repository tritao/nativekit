#include "nativekit_graphics.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#if defined(NK_BACKEND_ANDROID)
extern "C" nk_result NK_CALL nk_surface_set_frame_callback(nk_handle, nk_surface_frame_callback,
                                                           void *) {
    nk::core::clear_error();
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    nk::core::set_error("surface frame callbacks are not implemented by the Android backend");
    return NK_ERROR_UNSUPPORTED;
}
#endif
