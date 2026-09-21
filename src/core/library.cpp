#include "nativekit_library.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#if defined(_WIN32)
#define NK_LIBRARY_WINDOWS 1
#include <windows.h>
#elif defined(NK_BACKEND_ANDROID) || defined(NK_BACKEND_IOS) ||                                    \
    (!defined(NK_BACKEND_WEB) && (defined(__unix__) || defined(__APPLE__)))
#define NK_LIBRARY_POSIX 1
#include <dlfcn.h>
#endif

namespace {

struct DynamicLibrary final : nk::core::Resource {
#if defined(NK_LIBRARY_WINDOWS)
    HMODULE handle = nullptr;
#elif defined(NK_LIBRARY_POSIX)
    void *handle = nullptr;
#endif

    ~DynamicLibrary() override {
#if defined(NK_LIBRARY_WINDOWS)
        if (handle)
            FreeLibrary(handle);
#elif defined(NK_LIBRARY_POSIX)
        if (handle)
            dlclose(handle);
#endif
    }
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
    const auto encoded = value.substr(7);
    if (encoded.empty() || encoded.front() != '/')
        return false; // Remote authorities and relative paths are not process-local paths.
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
#if defined(NK_LIBRARY_WINDOWS)
    if (path.size() >= 3 && path[0] == '/' && std::isalpha(static_cast<unsigned char>(path[1])) &&
        path[2] == ':')
        path.erase(path.begin());
#endif
    return !path.empty();
}

#if defined(NK_LIBRARY_WINDOWS)
bool utf8_to_wide(const std::string &value, std::wstring &wide) {
    if (value.empty())
        return false;
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                         static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0)
        return false;
    wide.resize(static_cast<std::size_t>(size));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                               static_cast<int>(value.size()), wide.data(), size) == size;
}
#endif

#if defined(NK_BACKEND_IOS)
bool ios_framework_executable(const std::string &path) {
    constexpr std::string_view marker = ".framework/";
    const auto marker_start = path.rfind(marker);
    if (marker_start == std::string::npos)
        return false;

    const auto bundle_start = path.rfind('/', marker_start == 0 ? 0 : marker_start - 1);
    const auto name_start = bundle_start == std::string::npos ? 0 : bundle_start + 1;
    const auto bundle_name = path.substr(name_start, marker_start - name_start);
    const auto executable = path.substr(marker_start + marker.size());
    return !bundle_name.empty() && !executable.empty() &&
           executable.find('/') == std::string::npos && executable == bundle_name;
}
#endif

std::shared_ptr<DynamicLibrary> library(nk_library handle) {
    return std::dynamic_pointer_cast<DynamicLibrary>(
        nk::core::handles().get(static_cast<nk_handle>(handle), nk::core::ResourceType::library));
}

bool has_native_handle(const DynamicLibrary &library) {
#if defined(NK_LIBRARY_WINDOWS) || defined(NK_LIBRARY_POSIX)
    return library.handle != nullptr;
#else
    (void)library;
    return false;
#endif
}

} // namespace

namespace nk::core {

nk_capabilities dynamic_library_capabilities() noexcept {
#if defined(NK_LIBRARY_WINDOWS) || defined(NK_LIBRARY_POSIX)
    return NK_CAP_DYNAMIC_LIBRARY;
#else
    return 0;
#endif
}

} // namespace nk::core

extern "C" {

nk_result NK_CALL nk_library_open(const nk_resource *resource, nk_library *out_library) {
    return nk::core::result_boundary(
        "unexpected error while opening dynamic library", [&]() -> nk_result {
            nk::core::clear_error();
            if (!resource || resource->struct_size < sizeof(nk_resource) || !resource->uri ||
                !*resource->uri || !out_library) {
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "dynamic library resource arguments are invalid");
            }
            *out_library = NK_INVALID_HANDLE;
            if ((nk::core::dynamic_library_capabilities() & NK_CAP_DYNAMIC_LIBRARY) == 0)
                return fail(NK_ERROR_UNSUPPORTED,
                            "dynamic libraries are unavailable on this platform");
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;

            std::string path;
            if (!file_uri_path(resource->uri, path))
                return fail(NK_ERROR_UNSUPPORTED,
                            "dynamic libraries require an absolute local file URI");

            auto loaded = std::make_shared<DynamicLibrary>();
#if defined(NK_LIBRARY_WINDOWS)
            std::wstring wide_path;
            if (!utf8_to_wide(path, wide_path))
                return fail(NK_ERROR_INVALID_ARGUMENT, "dynamic library URI is not valid UTF-8");
            loaded->handle = LoadLibraryW(wide_path.c_str());
#elif defined(NK_LIBRARY_POSIX)
#if defined(NK_BACKEND_IOS)
            if (!ios_framework_executable(path))
                return fail(NK_ERROR_UNSUPPORTED,
                            "iOS dynamic libraries require an embedded framework executable");
#endif
            loaded->handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
            if (!has_native_handle(*loaded))
                return fail(NK_ERROR_NOT_FOUND, "could not load dynamic library");

            const auto handle =
                nk::core::handles().insert(nk::core::ResourceType::library, std::move(loaded));
            if (handle == NK_INVALID_HANDLE)
                return fail(NK_ERROR_OUT_OF_MEMORY, "could not allocate dynamic library handle");
            *out_library = static_cast<nk_library>(handle);
            return NK_OK;
        });
}

nk_result NK_CALL nk_library_symbol(nk_library library_handle, const char *name,
                                    void **out_symbol) {
    return nk::core::result_boundary(
        "unexpected error while resolving dynamic library symbol", [&]() -> nk_result {
            nk::core::clear_error();
            if (!name || !*name || !out_symbol)
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "dynamic library symbol arguments are invalid");
            *out_symbol = nullptr;
            if ((nk::core::dynamic_library_capabilities() & NK_CAP_DYNAMIC_LIBRARY) == 0)
                return fail(NK_ERROR_UNSUPPORTED,
                            "dynamic libraries are unavailable on this platform");
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            const auto loaded = library(library_handle);
            if (!loaded)
                return fail(NK_ERROR_INVALID_HANDLE, "invalid dynamic library handle");

#if defined(NK_LIBRARY_WINDOWS)
            const auto symbol = GetProcAddress(loaded->handle, name);
            if (!symbol)
                return fail(NK_ERROR_NOT_FOUND, "dynamic library symbol was not found");
            static_assert(sizeof(*out_symbol) == sizeof(symbol));
            std::memcpy(out_symbol, &symbol, sizeof(*out_symbol));
#elif defined(NK_LIBRARY_POSIX)
            dlerror();
            *out_symbol = dlsym(loaded->handle, name);
            if (const auto error = dlerror()) {
                *out_symbol = nullptr;
                (void)error;
                return fail(NK_ERROR_NOT_FOUND, "dynamic library symbol was not found");
            }
#endif
            return NK_OK;
        });
}

nk_result NK_CALL nk_library_close(nk_library library_handle) {
    nk::core::clear_error();
    if ((nk::core::dynamic_library_capabilities() & NK_CAP_DYNAMIC_LIBRARY) == 0)
        return fail(NK_ERROR_UNSUPPORTED, "dynamic libraries are unavailable on this platform");
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!nk::core::handles().erase(static_cast<nk_handle>(library_handle),
                                   nk::core::ResourceType::library))
        return fail(NK_ERROR_INVALID_HANDLE, "invalid dynamic library handle");
    return NK_OK;
}

} // extern "C"

#undef NK_LIBRARY_POSIX
#undef NK_LIBRARY_WINDOWS
