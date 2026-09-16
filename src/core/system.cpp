#include "nativekit_system.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"
#include "core/system_internal.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_set>

#if defined(NK_BACKEND_LINUX) || defined(NK_BACKEND_GTK)
#include <sys/utsname.h>
#endif

#if defined(NK_BACKEND_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

std::mutex system_mutex;
bool system_active = false;
std::string application_id;
std::string application_name;
std::unordered_set<nk_keep_awake> keep_awake_leases;
nk_keep_awake next_keep_awake = 1;

constexpr std::size_t init_application_id_end =
    offsetof(nk_init_options, application_id) + sizeof(nk_init_options::application_id);
constexpr std::size_t init_application_name_end =
    offsetof(nk_init_options, application_name) + sizeof(nk_init_options::application_name);

bool has_init_field(const nk_init_options *options, std::size_t end) {
    return options && options->struct_size >= end;
}

std::string sanitize_application_id(const char *value) {
    if (!value || !*value)
        return {};
    std::string result;
    result.reserve(std::strlen(value));
    for (const auto character : std::string(value)) {
        const bool safe = (character >= 'a' && character <= 'z') ||
                          (character >= 'A' && character <= 'Z') ||
                          (character >= '0' && character <= '9') || character == '.' ||
                          character == '_' || character == '-';
        result.push_back(safe ? character : '_');
    }
    if (result == "." || result == "..")
        result = "_";
    return result;
}

const char *platform_name() {
#if defined(NK_BACKEND_ANDROID)
    return "Android";
#elif defined(NK_BACKEND_IOS)
    return "iOS";
#elif defined(NK_BACKEND_WINDOWS)
    return "Windows";
#elif defined(NK_BACKEND_MACOS)
    return "macOS";
#elif defined(NK_BACKEND_LINUX) || defined(NK_BACKEND_GTK)
    return "Linux";
#elif defined(NK_BACKEND_WEB)
    return "Web";
#else
    return "";
#endif
}

nk_system_platform platform() {
#if defined(NK_BACKEND_ANDROID)
    return NK_SYSTEM_PLATFORM_ANDROID;
#elif defined(NK_BACKEND_IOS)
    return NK_SYSTEM_PLATFORM_IOS;
#elif defined(NK_BACKEND_WINDOWS)
    return NK_SYSTEM_PLATFORM_WINDOWS;
#elif defined(NK_BACKEND_MACOS)
    return NK_SYSTEM_PLATFORM_MACOS;
#elif defined(NK_BACKEND_LINUX) || defined(NK_BACKEND_GTK)
    return NK_SYSTEM_PLATFORM_LINUX;
#elif defined(NK_BACKEND_WEB)
    return NK_SYSTEM_PLATFORM_WEB;
#else
    return NK_SYSTEM_PLATFORM_UNKNOWN;
#endif
}

nk_system_endianness endianness() {
    const std::uint16_t value = 0x0102;
    const auto *bytes = reinterpret_cast<const unsigned char *>(&value);
    if (bytes[0] == 0x02 && bytes[1] == 0x01)
        return NK_SYSTEM_ENDIAN_LITTLE;
    if (bytes[0] == 0x01 && bytes[1] == 0x02)
        return NK_SYSTEM_ENDIAN_BIG;
    return NK_SYSTEM_ENDIAN_UNKNOWN;
}

std::string platform_version() {
#if defined(NK_BACKEND_LINUX) || defined(NK_BACKEND_GTK)
    std::ifstream release_file("/etc/os-release");
    std::string line;
    while (std::getline(release_file, line)) {
        constexpr char key[] = "PRETTY_NAME=";
        if (line.rfind(key, 0) != 0)
            continue;
        auto value = line.substr(sizeof(key) - 1);
        if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
            value = value.substr(1, value.size() - 2);
        if (!value.empty())
            return value;
    }
    struct utsname native {};
    if (uname(&native) == 0)
        return native.release;
#endif
    return {};
}

#if defined(NK_BACKEND_LINUX) || defined(NK_BACKEND_GTK)
std::string linux_device_string(const char *path) {
    std::ifstream input(path);
    std::string value;
    if (!std::getline(input, value))
        return {};
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    const auto first = std::find_if_not(value.begin(), value.end(), [](char character) {
        return std::isspace(static_cast<unsigned char>(character));
    });
    value.erase(value.begin(), first);
    return value;
}
#endif

nk_result copy_string(const std::string &value, char *buffer, uint32_t *inout_size) {
    if (!inout_size) {
        nk::core::set_error("system string size output must not be null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    if (value.size() >= std::numeric_limits<uint32_t>::max()) {
        nk::core::set_error("system string is too large");
        return NK_ERROR_UNKNOWN;
    }
    const auto required = static_cast<uint32_t>(value.size() + 1);
    const auto capacity = *inout_size;
    *inout_size = required;
    if (!buffer || capacity < required) {
        nk::core::set_error("system string output buffer is too small");
        return NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, value.c_str(), required);
    return NK_OK;
}

nk_result require_system_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

nk_result system_string(nk_system_string_kind kind, std::string &out_value) {
    switch (kind) {
    case NK_SYSTEM_STRING_PLATFORM_NAME:
        out_value = platform_name();
        return NK_OK;
    case NK_SYSTEM_STRING_PLATFORM_VERSION:
        out_value = platform_version();
        if (out_value.empty())
            (void)nk::core::system_backend::get_string(kind, out_value);
        return NK_OK;
    case NK_SYSTEM_STRING_PLATFORM_LABEL:
        out_value = platform_name();
        return NK_OK;
    case NK_SYSTEM_STRING_DEVICE_VENDOR:
    case NK_SYSTEM_STRING_DEVICE_MODEL:
        /* Device identity is deliberately empty when the backend cannot report it safely. */
        return nk::core::system_backend::get_string(kind, out_value);
    case NK_SYSTEM_STRING_APPLICATION_ID:
        out_value = nk::core::system_application_id();
        return NK_OK;
    case NK_SYSTEM_STRING_APPLICATION_NAME:
        out_value = nk::core::system_application_name();
        return NK_OK;
    default:
        return NK_ERROR_UNSUPPORTED;
    }
}

bool valid_system_string_kind(nk_system_string_kind kind) {
    return kind >= NK_SYSTEM_STRING_PLATFORM_NAME && kind <= NK_SYSTEM_STRING_APPLICATION_NAME;
}

} // namespace

namespace nk::core {

void system_initialize(const nk_init_options *options) {
    std::string id;
    std::string name;
    if (has_init_field(options, init_application_id_end))
        id = sanitize_application_id(options->application_id);
    if (has_init_field(options, init_application_name_end) && options->application_name)
        name = options->application_name;

    std::lock_guard lock(system_mutex);
    application_id = std::move(id);
    application_name = std::move(name);
    keep_awake_leases.clear();
    system_active = true;
}

void system_shutdown() noexcept {
    bool had_leases = false;
    {
        std::lock_guard lock(system_mutex);
        had_leases = !keep_awake_leases.empty();
        keep_awake_leases.clear();
        application_id.clear();
        application_name.clear();
        system_active = false;
    }
    if (had_leases)
        (void)system_backend::keep_awake_apply(false);
}

std::string system_application_id() {
    std::lock_guard lock(system_mutex);
    return application_id;
}

std::string system_application_name() {
    std::lock_guard lock(system_mutex);
    return application_name;
}

bool system_keep_awake_held() {
    std::lock_guard lock(system_mutex);
    return !keep_awake_leases.empty();
}

namespace system_backend {

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
bool keep_awake_supported() noexcept {
#if defined(NK_BACKEND_WINDOWS) || defined(NK_BACKEND_MACOS) || defined(NK_BACKEND_ANDROID) ||     \
    defined(NK_BACKEND_IOS)
    return true;
#else
    return false;
#endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
nk_result
keep_awake_apply(bool enabled) noexcept {
#if defined(NK_BACKEND_LINUX) || defined(NK_BACKEND_GTK)
    /* The GTK backend owns the desktop session; portal integration can refine this hook. */
    (void)enabled;
    return NK_OK;
#elif defined(NK_BACKEND_WINDOWS)
    const auto state =
        SetThreadExecutionState(enabled ? ES_CONTINUOUS | ES_DISPLAY_REQUIRED : ES_CONTINUOUS);
    if (!state) {
        nk::core::set_error("Windows could not update display execution state");
        return NK_ERROR_UNKNOWN;
    }
    return NK_OK;
#else
    return NK_ERROR_UNSUPPORTED;
#endif
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
nk_result
get_orientation(nk_system_orientation &out_orientation) noexcept {
    const auto size = out_orientation.struct_size;
    out_orientation = {};
    out_orientation.struct_size = size;
#if defined(NK_BACKEND_WINDOWS)
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode))
        return NK_ERROR_UNSUPPORTED;
    if (mode.dmFields & DM_DISPLAYORIENTATION) {
        switch (mode.dmDisplayOrientation) {
        case DMDO_90:
            out_orientation.display = NK_ORIENTATION_PORTRAIT;
            break;
        case DMDO_180:
            out_orientation.display = NK_ORIENTATION_LANDSCAPE_LEFT;
            break;
        case DMDO_270:
            out_orientation.display = NK_ORIENTATION_PORTRAIT_UPSIDE_DOWN;
            break;
        default:
            out_orientation.display = mode.dmPelsWidth >= mode.dmPelsHeight
                                          ? NK_ORIENTATION_LANDSCAPE_RIGHT
                                          : NK_ORIENTATION_PORTRAIT;
            break;
        }
    }
    return NK_OK;
#elif defined(NK_BACKEND_GTK) || defined(NK_BACKEND_MACOS) || defined(NK_BACKEND_ANDROID) ||       \
    defined(NK_BACKEND_IOS)
    return NK_OK;
#else
    return NK_ERROR_UNSUPPORTED;
#endif
}

#if defined(NK_BACKEND_LINUX) || defined(NK_BACKEND_GTK)
nk_result get_string(nk_system_string_kind kind, std::string &out_value) {
    const char *path = nullptr;
    switch (kind) {
    case NK_SYSTEM_STRING_DEVICE_VENDOR:
        path = "/sys/class/dmi/id/sys_vendor";
        break;
    case NK_SYSTEM_STRING_DEVICE_MODEL:
        path = "/sys/class/dmi/id/product_name";
        break;
    default:
        return NK_ERROR_UNSUPPORTED;
    }
    out_value = linux_device_string(path);
    return out_value.empty() ? NK_ERROR_UNSUPPORTED : NK_OK;
}
#else
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
nk_result
get_string(nk_system_string_kind, std::string &out_value) {
    out_value.clear();
    return NK_ERROR_UNSUPPORTED;
}
#endif

} // namespace system_backend

} // namespace nk::core

extern "C" {

nk_result NK_CALL nk_system_get_info(nk_system_info *out_info) {
    return nk::core::result_boundary(
        "unexpected error while querying system information", [&]() -> nk_result {
            if (const auto result = require_system_ui(); result != NK_OK)
                return result;
            if (!out_info || out_info->struct_size < sizeof(*out_info)) {
                nk::core::set_error("system information output is missing or too small");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            const auto size = out_info->struct_size;
            *out_info = {};
            out_info->struct_size = size;
            out_info->platform = platform();
            out_info->endianness = endianness();
            out_info->mobile = out_info->platform == NK_SYSTEM_PLATFORM_ANDROID ||
                               out_info->platform == NK_SYSTEM_PLATFORM_IOS;
            return NK_OK;
        });
}

nk_result NK_CALL nk_system_get_string(nk_system_string_kind kind, char *buffer,
                                       uint32_t *inout_size) {
    return nk::core::result_boundary(
        "unexpected error while querying a system string", [&]() -> nk_result {
            if (const auto result = require_system_ui(); result != NK_OK)
                return result;
            if (!valid_system_string_kind(kind)) {
                nk::core::set_error("unknown system string kind");
                return NK_ERROR_UNSUPPORTED;
            }
            std::string value;
            const auto result = system_string(kind, value);
            if (result != NK_OK) {
                nk::core::set_error("system string is unavailable");
                return result;
            }
            return copy_string(value, buffer, inout_size);
        });
}

nk_result NK_CALL nk_system_keep_awake_acquire(const nk_keep_awake_options *options,
                                               nk_keep_awake *out_lock) {
    return nk::core::result_boundary(
        "unexpected error while acquiring keep-awake lease", [&]() -> nk_result {
            if (const auto result = require_system_ui(); result != NK_OK)
                return result;
            if (!options || options->struct_size < sizeof(*options) || !out_lock) {
                nk::core::set_error("keep-awake options or output is missing or too small");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (!options->flags || (options->flags & ~NK_KEEP_AWAKE_DISPLAY)) {
                nk::core::set_error("unsupported keep-awake flags");
                return NK_ERROR_UNSUPPORTED;
            }
            *out_lock = 0;
            std::lock_guard lock(system_mutex);
            if (!system_active) {
                nk::core::set_error("NativeKit system state is not initialized");
                return NK_ERROR_NOT_INITIALIZED;
            }
            nk_keep_awake token = next_keep_awake++;
            if (!token)
                token = next_keep_awake++;
            const bool first = keep_awake_leases.empty();
            if (first) {
                const auto result = nk::core::system_backend::keep_awake_apply(true);
                if (result != NK_OK)
                    return result;
            }
            keep_awake_leases.insert(token);
            *out_lock = token;
            return NK_OK;
        });
}

nk_result NK_CALL nk_system_keep_awake_release(nk_keep_awake lock) {
    return nk::core::result_boundary(
        "unexpected error while releasing keep-awake lease", [&]() -> nk_result {
            if (const auto result = require_system_ui(); result != NK_OK)
                return result;
            if (!lock) {
                nk::core::set_error("keep-awake lease is invalid");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            std::lock_guard guard(system_mutex);
            const auto found = keep_awake_leases.find(lock);
            if (found == keep_awake_leases.end()) {
                nk::core::set_error("keep-awake lease is stale or already released");
                return NK_ERROR_INVALID_HANDLE;
            }
            const bool last = keep_awake_leases.size() == 1;
            if (last) {
                const auto result = nk::core::system_backend::keep_awake_apply(false);
                if (result != NK_OK)
                    return result;
            }
            keep_awake_leases.erase(found);
            return NK_OK;
        });
}

nk_result NK_CALL nk_system_get_orientation(nk_system_orientation *out_orientation) {
    return nk::core::result_boundary(
        "unexpected error while querying orientation", [&]() -> nk_result {
            if (const auto result = require_system_ui(); result != NK_OK)
                return result;
            if (!out_orientation || out_orientation->struct_size < sizeof(*out_orientation)) {
                nk::core::set_error("system orientation output is missing or too small");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            return nk::core::system_backend::get_orientation(*out_orientation);
        });
}

} // extern "C"
