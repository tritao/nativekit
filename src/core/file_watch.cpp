#include "nativekit_file_watch.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"

#include <cstddef>

namespace {

nk_result enter_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

bool valid_options(const nk_file_watch_options *options, nk_file_watch *out_watch) {
    return options && out_watch &&
           options->struct_size >= offsetof(nk_file_watch_options, reserved) && options->flags == 0;
}

} // namespace

#if !defined(__linux__) || defined(__ANDROID__) || defined(ANDROID)
namespace nk::backend {

nk_result file_watch_create(const nk_file_watch_options *, nk_file_watch *) noexcept {
    nk::core::set_error("file watching is unavailable on this backend");
    return NK_ERROR_UNSUPPORTED;
}

nk_result file_watch_add_directory(nk_file_watch, const char *, nk_bool) noexcept {
    nk::core::set_error("file watching is unavailable on this backend");
    return NK_ERROR_UNSUPPORTED;
}

nk_result file_watch_remove_directory(nk_file_watch, const char *) noexcept {
    nk::core::set_error("file watching is unavailable on this backend");
    return NK_ERROR_UNSUPPORTED;
}

nk_result file_watch_destroy(nk_file_watch) noexcept {
    nk::core::set_error("file watching is unavailable on this backend");
    return NK_ERROR_UNSUPPORTED;
}

} // namespace nk::backend
#endif

extern "C" {

nk_result NK_CALL nk_file_watch_create(const nk_file_watch_options *options,
                                       nk_file_watch *out_watch) {
    return nk::core::result_boundary("unexpected error while creating file watcher",
                                     [&]() -> nk_result {
                                         if (const auto result = enter_ui(); result != NK_OK)
                                             return result;
                                         if (!valid_options(options, out_watch)) {
                                             nk::core::set_error("invalid file-watch options");
                                             return NK_ERROR_INVALID_ARGUMENT;
                                         }
                                         *out_watch = NK_INVALID_HANDLE;
                                         return nk::backend::file_watch_create(options, out_watch);
                                     });
}

nk_result NK_CALL nk_file_watch_add_directory(nk_file_watch watch, const char *path,
                                              nk_bool recursive) {
    return nk::core::result_boundary(
        "unexpected error while adding file-watch directory", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            return nk::backend::file_watch_add_directory(watch, path, recursive);
        });
}

nk_result NK_CALL nk_file_watch_remove_directory(nk_file_watch watch, const char *path) {
    return nk::core::result_boundary(
        "unexpected error while removing file-watch directory", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            return nk::backend::file_watch_remove_directory(watch, path);
        });
}

nk_result NK_CALL nk_file_watch_destroy(nk_file_watch watch) {
    return nk::core::result_boundary("unexpected error while destroying file watcher",
                                     [&]() -> nk_result {
                                         if (const auto result = enter_ui(); result != NK_OK)
                                             return result;
                                         return nk::backend::file_watch_destroy(watch);
                                     });
}
}
