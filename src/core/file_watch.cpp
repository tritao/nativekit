#include "nativekit_file_watch.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/file_watch.hpp"
#include "core/runtime.hpp"

#include <cstddef>
#include <cstring>

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

namespace nk::core {

std::vector<std::byte> file_changed_payload(nk_file_change_type change, nk_file_item_kind kind,
                                            nk_file_event_flags flags, std::string_view path,
                                            std::string_view old_path) {
    nk_file_changed_event header{};
    header.change_type = change;
    header.item_kind = kind;
    header.flags = flags;
    header.path_offset = path.empty() ? 0u : static_cast<uint32_t>(sizeof(header));
    header.path_length = static_cast<uint32_t>(path.size());
    header.old_path_offset = old_path.empty() ? 0u : header.path_offset + header.path_length + 1u;
    header.old_path_length = static_cast<uint32_t>(old_path.size());
    std::size_t size = sizeof(header);
    if (!path.empty())
        size = header.path_offset + path.size() + 1u;
    if (!old_path.empty())
        size = header.old_path_offset + old_path.size() + 1u;
    std::vector<std::byte> result(size);
    std::memcpy(result.data(), &header, sizeof(header));
    if (!path.empty())
        std::memcpy(result.data() + header.path_offset, path.data(), path.size());
    if (!old_path.empty())
        std::memcpy(result.data() + header.old_path_offset, old_path.data(), old_path.size());
    return result;
}

QueuedEvent file_watch_overflow_event(nk_file_watch source) {
    QueuedEvent event;
    event.kind = NK_EVENT_FILE_WATCH_OVERFLOW;
    event.source = source;
    event.data =
        file_changed_payload(NK_FILE_CHANGE_MODIFIED, NK_FILE_ITEM_UNKNOWN,
                             NK_FILE_EVENT_FLAG_OVERFLOW | NK_FILE_EVENT_FLAG_RESCAN_REQUIRED, {});
    return event;
}

QueuedEvent file_watch_ready_event(nk_file_watch source, std::string_view path,
                                   nk_request_id registration) {
    QueuedEvent event;
    event.kind = NK_EVENT_FILE_WATCH_READY;
    event.source = source;
    event.request_id = registration;
    event.data = file_changed_payload(NK_FILE_CHANGE_ADDED, NK_FILE_ITEM_DIRECTORY, 0, path);
    return event;
}

} // namespace nk::core

#if (!defined(__linux__) || defined(__ANDROID__) || defined(ANDROID)) && !defined(_WIN32)
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

nk_result file_watch_exclude_directory(nk_file_watch, const char *) noexcept {
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

nk_result NK_CALL nk_file_watch_exclude_directory(nk_file_watch watch, const char *path) {
    return nk::core::result_boundary(
        "unexpected error while excluding file-watch directory", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            return nk::backend::file_watch_exclude_directory(watch, path);
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
