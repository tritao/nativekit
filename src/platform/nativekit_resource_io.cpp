#include "nativekit_resource.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <mutex>
#include <string>

namespace {
struct FileResourceStream final : nk::core::Resource {
    std::mutex mutex;
    std::fstream file;
    uint32_t flags = 0;
    uint64_t size = UINT64_MAX;
};

nk_result fail(nk_result result, const char *message) {
    nk::core::set_error(message);
    return result;
}

int hex_digit(char value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    value = static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
    return value >= 'a' && value <= 'f' ? value - 'a' + 10 : -1;
}

bool file_uri_path(const char *uri, std::string &path) {
    if (!uri)
        return false;
    std::string value(uri);
    if (value.size() < 7)
        return false;
    std::string scheme = value.substr(0, 7);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (scheme != "file://")
        return false;
    auto encoded = value.substr(7);
    if (!encoded.empty() && encoded.front() != '/')
        return false; // Remote file authorities are not process-local paths.
    path.clear();
    for (std::size_t index = 0; index < encoded.size(); ++index) {
        if (encoded[index] != '%') {
            path.push_back(encoded[index]);
            continue;
        }
        if (index + 2 >= encoded.size())
            return false;
        const auto high = hex_digit(encoded[index + 1]);
        const auto low = hex_digit(encoded[index + 2]);
        if (high < 0 || low < 0 || (high == 0 && low == 0))
            return false;
        path.push_back(static_cast<char>((high << 4) | low));
        index += 2;
    }
#if defined(_WIN32)
    if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) &&
        path[2] == ':')
        path.erase(path.begin());
#endif
    return !path.empty();
}

std::shared_ptr<FileResourceStream> stream(nk_handle handle) {
    return std::dynamic_pointer_cast<FileResourceStream>(
        nk::core::handles().get(handle, nk::core::ResourceType::resource_stream));
}
} // namespace

extern "C" {
nk_result NK_CALL nk_resource_set_persisted_access(const nk_resource *resource,
                                                   uint32_t access_flags,
                                                   uint32_t *out_flags) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!resource || resource->struct_size < sizeof(nk_resource) || !resource->uri ||
        !*resource->uri || !out_flags ||
        (access_flags & ~(NK_RESOURCE_READABLE | NK_RESOURCE_WRITABLE)) != 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "persisted resource access arguments are invalid");
    return fail(NK_ERROR_UNSUPPORTED, "persisted URI access is unavailable on this platform");
}

nk_result NK_CALL nk_resource_get_persisted_access(const nk_resource *resource,
                                                   uint32_t *out_flags) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!resource || resource->struct_size < sizeof(nk_resource) || !resource->uri ||
        !*resource->uri || !out_flags)
        return fail(NK_ERROR_INVALID_ARGUMENT, "persisted resource access arguments are invalid");
    return fail(NK_ERROR_UNSUPPORTED, "persisted URI access is unavailable on this platform");
}

nk_result NK_CALL nk_resource_open(const nk_resource *resource, uint32_t flags,
                                   nk_handle *out_stream) {
    return nk::core::result_boundary("unexpected error while opening resource", [&]() -> nk_result {
        if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
            return result;
        if (!resource || resource->struct_size < sizeof(nk_resource) || !out_stream ||
            (flags & (NK_RESOURCE_OPEN_READ | NK_RESOURCE_OPEN_WRITE)) == 0 ||
            (flags & ~(NK_RESOURCE_OPEN_READ | NK_RESOURCE_OPEN_WRITE | NK_RESOURCE_OPEN_CREATE |
                       NK_RESOURCE_OPEN_TRUNCATE)) != 0 ||
            ((flags & (NK_RESOURCE_OPEN_CREATE | NK_RESOURCE_OPEN_TRUNCATE)) != 0 &&
             (flags & NK_RESOURCE_OPEN_WRITE) == 0))
            return fail(NK_ERROR_INVALID_ARGUMENT, "resource open arguments are invalid");
        std::string path;
        if (!file_uri_path(resource->uri, path))
            return fail(NK_ERROR_UNSUPPORTED, "desktop resource streams require a local file URI");
        std::ios::openmode mode = std::ios::binary;
        if (flags & NK_RESOURCE_OPEN_READ)
            mode |= std::ios::in;
        if (flags & NK_RESOURCE_OPEN_WRITE)
            mode |= std::ios::out;
        if ((flags & NK_RESOURCE_OPEN_WRITE) && !(flags & NK_RESOURCE_OPEN_READ) &&
            !(flags & NK_RESOURCE_OPEN_TRUNCATE))
            mode |= std::ios::in; // Prevent std::ios::out from truncating an existing file.
        if (flags & NK_RESOURCE_OPEN_TRUNCATE)
            mode |= std::ios::trunc;
        auto resource_stream = std::make_shared<FileResourceStream>();
        const auto native_path = std::filesystem::u8path(path);
        resource_stream->file.open(native_path, mode);
        if (!resource_stream->file && (flags & NK_RESOURCE_OPEN_CREATE) &&
            !(flags & NK_RESOURCE_OPEN_TRUNCATE)) {
            std::ofstream create(native_path, std::ios::binary | std::ios::app);
            create.close();
            resource_stream->file.clear();
            resource_stream->file.open(native_path, mode);
        }
        if (!resource_stream->file)
            return fail(NK_ERROR_UNKNOWN, "could not open local resource file");
        resource_stream->flags = NK_RESOURCE_STREAM_SEEKABLE;
        if (flags & NK_RESOURCE_OPEN_READ)
            resource_stream->flags |= NK_RESOURCE_STREAM_READABLE;
        if (flags & NK_RESOURCE_OPEN_WRITE)
            resource_stream->flags |= NK_RESOURCE_STREAM_WRITABLE;
        resource_stream->file.seekg(0, std::ios::end);
        auto end = resource_stream->file.tellg();
        if (end < 0) {
            resource_stream->file.clear();
            resource_stream->file.seekp(0, std::ios::end);
            end = resource_stream->file.tellp();
        }
        if (end >= 0) {
            resource_stream->size = static_cast<uint64_t>(end);
            resource_stream->flags |= NK_RESOURCE_STREAM_SIZE_KNOWN;
        }
        resource_stream->file.clear();
        if (flags & NK_RESOURCE_OPEN_READ)
            resource_stream->file.seekg(0, std::ios::beg);
        if (flags & NK_RESOURCE_OPEN_WRITE)
            resource_stream->file.seekp(0, std::ios::beg);
        const auto handle = nk::core::handles().insert(nk::core::ResourceType::resource_stream,
                                                       resource_stream);
        if (!handle)
            return fail(NK_ERROR_OUT_OF_MEMORY, "could not allocate resource stream handle");
        *out_stream = handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_resource_stream_info_get(nk_handle handle,
                                              nk_resource_stream_info *out_info) {
    if (!out_info || out_info->struct_size < sizeof(nk_resource_stream_info))
        return fail(NK_ERROR_INVALID_ARGUMENT, "resource stream info output is invalid");
    auto resource = stream(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid resource stream handle");
    std::lock_guard lock(resource->mutex);
    out_info->flags = resource->flags;
    out_info->size = resource->size;
    return NK_OK;
}

nk_result NK_CALL nk_resource_read(nk_handle handle, void *buffer, uint64_t size,
                                   uint64_t *out_read) {
    if ((!buffer && size) || !out_read ||
        size > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
        return fail(NK_ERROR_INVALID_ARGUMENT, "resource read arguments are invalid");
    auto resource = stream(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid resource stream handle");
    if (!(resource->flags & NK_RESOURCE_STREAM_READABLE))
        return fail(NK_ERROR_UNSUPPORTED, "resource stream is not readable");
    std::lock_guard lock(resource->mutex);
    resource->file.read(static_cast<char *>(buffer), static_cast<std::streamsize>(size));
    *out_read = static_cast<uint64_t>(resource->file.gcount());
    if (resource->file.bad())
        return fail(NK_ERROR_UNKNOWN, "resource read failed");
    resource->file.clear(resource->file.rdstate() & ~std::ios::failbit);
    return NK_OK;
}

nk_result NK_CALL nk_resource_write(nk_handle handle, const void *buffer, uint64_t size,
                                    uint64_t *out_written) {
    if ((!buffer && size) || !out_written ||
        size > static_cast<uint64_t>(std::numeric_limits<std::streamsize>::max()))
        return fail(NK_ERROR_INVALID_ARGUMENT, "resource write arguments are invalid");
    auto resource = stream(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid resource stream handle");
    if (!(resource->flags & NK_RESOURCE_STREAM_WRITABLE))
        return fail(NK_ERROR_UNSUPPORTED, "resource stream is not writable");
    std::lock_guard lock(resource->mutex);
    resource->file.write(static_cast<const char *>(buffer), static_cast<std::streamsize>(size));
    if (!resource->file)
        return fail(NK_ERROR_UNKNOWN, "resource write failed");
    *out_written = size;
    const auto position = resource->file.tellp();
    if (position >= 0 &&
        (!(resource->flags & NK_RESOURCE_STREAM_SIZE_KNOWN) ||
         static_cast<uint64_t>(position) > resource->size)) {
        resource->size = static_cast<uint64_t>(position);
        resource->flags |= NK_RESOURCE_STREAM_SIZE_KNOWN;
    }
    return NK_OK;
}

nk_result NK_CALL nk_resource_seek(nk_handle handle, int64_t offset, nk_seek_origin origin,
                                   uint64_t *out_position) {
    if (!out_position || origin > NK_SEEK_END)
        return fail(NK_ERROR_INVALID_ARGUMENT, "resource seek arguments are invalid");
    auto resource = stream(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid resource stream handle");
    std::lock_guard lock(resource->mutex);
    const auto direction = origin == NK_SEEK_START     ? std::ios::beg
                           : origin == NK_SEEK_CURRENT ? std::ios::cur
                                                       : std::ios::end;
    resource->file.clear();
    std::streampos position = -1;
    if (resource->flags & NK_RESOURCE_STREAM_READABLE) {
        resource->file.seekg(offset, direction);
        position = resource->file.tellg();
    } else if (resource->flags & NK_RESOURCE_STREAM_WRITABLE) {
        resource->file.seekp(offset, direction);
        position = resource->file.tellp();
    }
    if (position < 0)
        return fail(NK_ERROR_UNKNOWN, "resource seek failed");
    resource->file.clear();
    if (resource->flags & NK_RESOURCE_STREAM_READABLE)
        resource->file.seekg(position);
    if (resource->flags & NK_RESOURCE_STREAM_WRITABLE)
        resource->file.seekp(position);
    if (!resource->file)
        return fail(NK_ERROR_UNKNOWN, "resource seek failed");
    *out_position = static_cast<uint64_t>(position);
    return NK_OK;
}

nk_result NK_CALL nk_resource_close(nk_handle handle) {
    if (!nk::core::handles().erase(handle, nk::core::ResourceType::resource_stream))
        return fail(NK_ERROR_INVALID_HANDLE, "invalid resource stream handle");
    return NK_OK;
}
}
