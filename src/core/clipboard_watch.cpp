#include "nativekit_clipboard.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"

#include <cstddef>

namespace {

nk_result enter_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

bool valid_options(const nk_clipboard_watch_options *options,
                  nk_clipboard_watch *out_watch) {
    return options && out_watch &&
           options->struct_size >= offsetof(nk_clipboard_watch_options, reserved) &&
           options->flags == 0;
}

} // namespace

#if !defined(NK_BACKEND_GTK)
namespace nk::backend {

nk_result clipboard_watch_start(const nk_clipboard_watch_options *, nk_clipboard_watch *) noexcept {
    nk::core::set_error("clipboard change observation is unavailable on this backend");
    return NK_ERROR_UNSUPPORTED;
}

nk_result clipboard_watch_stop(nk_clipboard_watch) noexcept {
    nk::core::set_error("clipboard change observation is unavailable on this backend");
    return NK_ERROR_UNSUPPORTED;
}

} // namespace nk::backend
#endif

extern "C" {

nk_result NK_CALL nk_clipboard_watch_start(const nk_clipboard_watch_options *options,
                                           nk_clipboard_watch *out_watch) {
    return nk::core::result_boundary(
        "unexpected error while starting clipboard watcher", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!valid_options(options, out_watch)) {
                nk::core::set_error("invalid clipboard-watch options");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            *out_watch = NK_INVALID_HANDLE;
            return nk::backend::clipboard_watch_start(options, out_watch);
        });
}

nk_result NK_CALL nk_clipboard_watch_stop(nk_clipboard_watch watch) {
    return nk::core::result_boundary(
        "unexpected error while stopping clipboard watcher", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            return nk::backend::clipboard_watch_stop(watch);
        });
}

}
