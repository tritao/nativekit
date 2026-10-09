#include "nativekit_filesystem.h"

#include <cstring>

namespace {
thread_local const char *last_error = "root-scoped filesystem access is unavailable on this platform";
}

extern "C" {

nk_result NK_CALL nk_filesystem_root_open(const char *, nk_filesystem_handle *out_root) {
    if (out_root)
        *out_root = 0;
    return out_root ? NK_ERROR_UNSUPPORTED : NK_ERROR_INVALID_ARGUMENT;
}

nk_result NK_CALL nk_filesystem_root_close(nk_filesystem_handle) { return NK_ERROR_UNSUPPORTED; }

nk_result NK_CALL nk_filesystem_stat(nk_filesystem_handle, const char *,
                                     nk_filesystem_entry *out_entry) {
    if (!out_entry)
        return NK_ERROR_INVALID_ARGUMENT;
    if (out_entry->struct_size == sizeof(nk_filesystem_entry)) {
        const auto size = out_entry->struct_size;
        std::memset(out_entry, 0, sizeof(*out_entry));
        out_entry->struct_size = size;
    }
    return NK_ERROR_UNSUPPORTED;
}

nk_result NK_CALL nk_filesystem_directory_open(nk_filesystem_handle, const char *,
                                               nk_filesystem_handle *out_cursor) {
    if (out_cursor)
        *out_cursor = 0;
    return out_cursor ? NK_ERROR_UNSUPPORTED : NK_ERROR_INVALID_ARGUMENT;
}

nk_result NK_CALL nk_filesystem_directory_next(nk_filesystem_handle, nk_filesystem_entry *out_entry,
                                               char *, uint32_t *inout_name_size,
                                               nk_bool *out_end) {
    if (!out_entry || !inout_name_size || !out_end)
        return NK_ERROR_INVALID_ARGUMENT;
    return NK_ERROR_UNSUPPORTED;
}

nk_result NK_CALL nk_filesystem_directory_close(nk_filesystem_handle) { return NK_ERROR_UNSUPPORTED; }

nk_result NK_CALL nk_filesystem_file_open(nk_filesystem_handle, const char *,
                                          nk_filesystem_handle *out_file) {
    if (out_file)
        *out_file = 0;
    return out_file ? NK_ERROR_UNSUPPORTED : NK_ERROR_INVALID_ARGUMENT;
}

nk_result NK_CALL nk_filesystem_file_info(nk_filesystem_handle,
                                          nk_filesystem_entry *out_entry) {
    if (!out_entry)
        return NK_ERROR_INVALID_ARGUMENT;
    if (out_entry->struct_size == sizeof(nk_filesystem_entry)) {
        const auto size = out_entry->struct_size;
        std::memset(out_entry, 0, sizeof(*out_entry));
        out_entry->struct_size = size;
    }
    return NK_ERROR_UNSUPPORTED;
}

nk_result NK_CALL nk_filesystem_file_read(nk_filesystem_handle, uint64_t, uint32_t,
                                          uint8_t *, uint32_t *inout_bytes) {
    if (!inout_bytes)
        return NK_ERROR_INVALID_ARGUMENT;
    *inout_bytes = 0;
    return NK_ERROR_UNSUPPORTED;
}

nk_result NK_CALL nk_filesystem_file_close(nk_filesystem_handle) { return NK_ERROR_UNSUPPORTED; }

const char *NK_CALL nk_filesystem_last_error(void) { return last_error; }

} // extern "C"
