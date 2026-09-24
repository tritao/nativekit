#include "nativekit.h"
#include "nativekit_system.h"
#include "nativekit_window.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

#ifndef NK_CAPABILITY_SNAPSHOT_FILE
#define NK_CAPABILITY_SNAPSHOT_FILE "capability-snapshots.txt"
#endif

namespace {

using mask = std::uint64_t;

struct backend_contract {
    const char *name;
    mask required;
    mask deferred;
    mask not_applicable;
    mask optional;
};

constexpr mask cap(nk_capabilities value) {
    return static_cast<mask>(value);
}

constexpr mask k_known_capabilities =
    cap(NK_CAP_WINDOW) | cap(NK_CAP_WEBVIEW) | cap(NK_CAP_CLIPBOARD) | cap(NK_CAP_DRAG_DROP) |
    cap(NK_CAP_SHELL) | cap(NK_CAP_SYSTEM_APPEARANCE) | cap(NK_CAP_EXPORT_NATIVE_WINDOW) |
    cap(NK_CAP_WRAP_NATIVE_WINDOW) | cap(NK_CAP_NOTIFICATION) | cap(NK_CAP_MOBILE_HOST) |
    cap(NK_CAP_INPUT) | cap(NK_CAP_OPENGL_SURFACE) | cap(NK_CAP_OPENGL_ES_SURFACE) |
    cap(NK_CAP_CURSOR) | cap(NK_CAP_POINTER_CAPTURE) | cap(NK_CAP_WINDOW_GEOMETRY) |
    cap(NK_CAP_WINDOW_STYLING) | cap(NK_CAP_MONITOR) | cap(NK_CAP_MONITOR_FULLSCREEN) |
    cap(NK_CAP_JOYSTICK) | cap(NK_CAP_RESOURCE_SHARING) | cap(NK_CAP_RESOURCE_IO) |
    cap(NK_CAP_VULKAN_SURFACE) | cap(NK_CAP_ACCESSIBILITY) | cap(NK_CAP_D3D11_SURFACE) |
    cap(NK_CAP_METAL_SURFACE) | cap(NK_CAP_SYSTEM_INFO) | cap(NK_CAP_APPLICATION_PATH) |
    cap(NK_CAP_APPLICATION_STORAGE) | cap(NK_CAP_SYSTEM_FONTS) | cap(NK_CAP_KEEP_AWAKE) |
    cap(NK_CAP_DEVICE_ORIENTATION) | cap(NK_CAP_DISPLAY_ORIENTATION) | cap(NK_CAP_HTTP_CLIENT) |
    cap(NK_CAP_HTTP_STREAMING) | cap(NK_CAP_SURFACE_FRAME_CALLBACK) |
    cap(NK_CAP_WINDOW_CUSTOM_DECORATIONS) | cap(NK_CAP_SENSORS) | cap(NK_CAP_HAPTICS) |
    cap(NK_CAP_GAMEPAD_RUMBLE) | cap(NK_CAP_NATIVE_VIEW) | cap(NK_CAP_FILE_WATCH) |
    cap(NK_CAP_CLIPBOARD_WATCH) | cap(NK_CAP_APPLICATION_MENU) | cap(NK_CAP_TRANSPORT) |
    cap(NK_CAP_DYNAMIC_LIBRARY);

constexpr mask k_new_system_capabilities =
    cap(NK_CAP_SYSTEM_INFO) | cap(NK_CAP_APPLICATION_PATH) | cap(NK_CAP_APPLICATION_STORAGE) |
    cap(NK_CAP_SYSTEM_FONTS) | cap(NK_CAP_KEEP_AWAKE) | cap(NK_CAP_DEVICE_ORIENTATION) |
    cap(NK_CAP_DISPLAY_ORIENTATION);

constexpr mask k_linux_system_capabilities =
    k_new_system_capabilities & ~(cap(NK_CAP_DEVICE_ORIENTATION) | cap(NK_CAP_KEEP_AWAKE));
constexpr mask k_mobile_system_capabilities =
    cap(NK_CAP_SYSTEM_INFO) | cap(NK_CAP_APPLICATION_STORAGE) | cap(NK_CAP_KEEP_AWAKE) |
    cap(NK_CAP_DEVICE_ORIENTATION) | cap(NK_CAP_DISPLAY_ORIENTATION);
constexpr mask k_android_system_capabilities =
    k_mobile_system_capabilities | cap(NK_CAP_APPLICATION_PATH) | cap(NK_CAP_SYSTEM_FONTS);
constexpr mask k_ios_system_capabilities =
    k_mobile_system_capabilities | cap(NK_CAP_APPLICATION_PATH);

constexpr mask k_desktop_common =
    cap(NK_CAP_WINDOW) | cap(NK_CAP_CLIPBOARD) | cap(NK_CAP_DRAG_DROP) | cap(NK_CAP_SHELL) |
    cap(NK_CAP_SYSTEM_APPEARANCE) | cap(NK_CAP_EXPORT_NATIVE_WINDOW) | cap(NK_CAP_NOTIFICATION) |
    cap(NK_CAP_INPUT) | cap(NK_CAP_CURSOR) | cap(NK_CAP_POINTER_CAPTURE) | cap(NK_CAP_RESOURCE_IO) |
    cap(NK_CAP_SURFACE_FRAME_CALLBACK) | cap(NK_CAP_DYNAMIC_LIBRARY);

constexpr backend_contract platform_contract() {
#if defined(NK_PARITY_BACKEND_LINUX)
    return {"Linux/GTK",
            k_desktop_common | cap(NK_CAP_WEBVIEW) | cap(NK_CAP_OPENGL_SURFACE) |
                cap(NK_CAP_OPENGL_ES_SURFACE) | cap(NK_CAP_WINDOW_GEOMETRY) |
                cap(NK_CAP_WINDOW_STYLING) | cap(NK_CAP_WINDOW_CUSTOM_DECORATIONS) |
                cap(NK_CAP_MONITOR) | cap(NK_CAP_MONITOR_FULLSCREEN) | cap(NK_CAP_JOYSTICK) |
                cap(NK_CAP_GAMEPAD_RUMBLE) | cap(NK_CAP_RESOURCE_SHARING) |
                cap(NK_CAP_VULKAN_SURFACE) | cap(NK_CAP_ACCESSIBILITY) |
                cap(NK_CAP_WRAP_NATIVE_WINDOW) | cap(NK_CAP_NATIVE_VIEW) | cap(NK_CAP_FILE_WATCH) |
                cap(NK_CAP_CLIPBOARD_WATCH) | cap(NK_CAP_APPLICATION_MENU) |
                k_linux_system_capabilities,
            cap(NK_CAP_D3D11_SURFACE) | cap(NK_CAP_METAL_SURFACE) | cap(NK_CAP_MOBILE_HOST) |
                cap(NK_CAP_DEVICE_ORIENTATION),
            0, cap(NK_CAP_KEEP_AWAKE)};
#elif defined(NK_PARITY_BACKEND_WINDOWS)
    return {"Windows",
            k_desktop_common | cap(NK_CAP_WINDOW_GEOMETRY) | cap(NK_CAP_WINDOW_STYLING) |
                cap(NK_CAP_WINDOW_CUSTOM_DECORATIONS) | cap(NK_CAP_D3D11_SURFACE) |
                cap(NK_CAP_ACCESSIBILITY) | cap(NK_CAP_MONITOR) | cap(NK_CAP_MONITOR_FULLSCREEN) |
                cap(NK_CAP_JOYSTICK) | cap(NK_CAP_GAMEPAD_RUMBLE) | cap(NK_CAP_RESOURCE_SHARING) |
                cap(NK_CAP_WRAP_NATIVE_WINDOW) | cap(NK_CAP_APPLICATION_MENU) |
                k_new_system_capabilities,
            cap(NK_CAP_OPENGL_SURFACE) | cap(NK_CAP_OPENGL_ES_SURFACE) |
                cap(NK_CAP_VULKAN_SURFACE) | cap(NK_CAP_METAL_SURFACE) | cap(NK_CAP_MOBILE_HOST) |
                cap(NK_CAP_NATIVE_VIEW) | cap(NK_CAP_FILE_WATCH) | cap(NK_CAP_CLIPBOARD_WATCH),
            0, cap(NK_CAP_WEBVIEW)};
#elif defined(NK_PARITY_BACKEND_MACOS)
    return {"macOS",
            k_desktop_common | cap(NK_CAP_WEBVIEW) | cap(NK_CAP_WINDOW_GEOMETRY) |
                cap(NK_CAP_WINDOW_STYLING) | cap(NK_CAP_WINDOW_CUSTOM_DECORATIONS) |
                cap(NK_CAP_METAL_SURFACE) | cap(NK_CAP_MONITOR) | cap(NK_CAP_MONITOR_FULLSCREEN) |
                cap(NK_CAP_JOYSTICK) | cap(NK_CAP_RESOURCE_SHARING) | cap(NK_CAP_ACCESSIBILITY) |
                cap(NK_CAP_WRAP_NATIVE_WINDOW) | cap(NK_CAP_APPLICATION_MENU) |
                k_new_system_capabilities,
            cap(NK_CAP_OPENGL_SURFACE) | cap(NK_CAP_OPENGL_ES_SURFACE) |
                cap(NK_CAP_VULKAN_SURFACE) | cap(NK_CAP_D3D11_SURFACE) | cap(NK_CAP_MOBILE_HOST) |
                cap(NK_CAP_NATIVE_VIEW),
            cap(NK_CAP_FILE_WATCH) | cap(NK_CAP_CLIPBOARD_WATCH), 0};
#elif defined(NK_PARITY_BACKEND_ANDROID)
    return {"Android",
            cap(NK_CAP_MOBILE_HOST) | cap(NK_CAP_WEBVIEW) | cap(NK_CAP_CLIPBOARD) |
                cap(NK_CAP_DRAG_DROP) | cap(NK_CAP_SHELL) | cap(NK_CAP_SYSTEM_APPEARANCE) |
                cap(NK_CAP_NOTIFICATION) | cap(NK_CAP_INPUT) | cap(NK_CAP_OPENGL_ES_SURFACE) |
                cap(NK_CAP_VULKAN_SURFACE) | cap(NK_CAP_RESOURCE_SHARING) |
                cap(NK_CAP_RESOURCE_IO) | cap(NK_CAP_JOYSTICK) | cap(NK_CAP_SENSORS) |
                cap(NK_CAP_HAPTICS) | cap(NK_CAP_GAMEPAD_RUMBLE) | cap(NK_CAP_ACCESSIBILITY) |
                k_android_system_capabilities | cap(NK_CAP_SURFACE_FRAME_CALLBACK) |
                cap(NK_CAP_DYNAMIC_LIBRARY),
            cap(NK_CAP_NATIVE_VIEW) | cap(NK_CAP_FILE_WATCH) | cap(NK_CAP_CLIPBOARD_WATCH),
            cap(NK_CAP_WINDOW) | cap(NK_CAP_EXPORT_NATIVE_WINDOW) | cap(NK_CAP_WRAP_NATIVE_WINDOW) |
                cap(NK_CAP_WINDOW_GEOMETRY) | cap(NK_CAP_WINDOW_STYLING) | cap(NK_CAP_MONITOR) |
                cap(NK_CAP_MONITOR_FULLSCREEN) | cap(NK_CAP_CURSOR) | cap(NK_CAP_POINTER_CAPTURE) |
                cap(NK_CAP_OPENGL_SURFACE) | cap(NK_CAP_D3D11_SURFACE) | cap(NK_CAP_METAL_SURFACE) |
                cap(NK_CAP_WINDOW_CUSTOM_DECORATIONS) | cap(NK_CAP_APPLICATION_MENU),
            0};
#elif defined(NK_PARITY_BACKEND_IOS)
    return {"iOS",
            cap(NK_CAP_MOBILE_HOST) | cap(NK_CAP_WEBVIEW) | cap(NK_CAP_METAL_SURFACE) |
                cap(NK_CAP_INPUT) | cap(NK_CAP_RESOURCE_IO) | cap(NK_CAP_CLIPBOARD) |
                cap(NK_CAP_SHELL) | cap(NK_CAP_SYSTEM_APPEARANCE) | cap(NK_CAP_NOTIFICATION) |
                k_ios_system_capabilities | cap(NK_CAP_ACCESSIBILITY) | cap(NK_CAP_DRAG_DROP) |
                cap(NK_CAP_RESOURCE_SHARING) | cap(NK_CAP_JOYSTICK) | cap(NK_CAP_SENSORS) |
                cap(NK_CAP_HAPTICS) | cap(NK_CAP_GAMEPAD_RUMBLE) |
                cap(NK_CAP_SURFACE_FRAME_CALLBACK) | cap(NK_CAP_DYNAMIC_LIBRARY),
            cap(NK_CAP_NATIVE_VIEW),
            cap(NK_CAP_WINDOW) | cap(NK_CAP_EXPORT_NATIVE_WINDOW) | cap(NK_CAP_WRAP_NATIVE_WINDOW) |
                cap(NK_CAP_WINDOW_GEOMETRY) | cap(NK_CAP_WINDOW_STYLING) | cap(NK_CAP_MONITOR) |
                cap(NK_CAP_MONITOR_FULLSCREEN) | cap(NK_CAP_CURSOR) | cap(NK_CAP_POINTER_CAPTURE) |
                cap(NK_CAP_OPENGL_SURFACE) | cap(NK_CAP_OPENGL_ES_SURFACE) |
                cap(NK_CAP_SYSTEM_FONTS) | cap(NK_CAP_VULKAN_SURFACE) | cap(NK_CAP_D3D11_SURFACE) |
                cap(NK_CAP_WINDOW_CUSTOM_DECORATIONS) | cap(NK_CAP_APPLICATION_MENU),
            cap(NK_CAP_FILE_WATCH) | cap(NK_CAP_CLIPBOARD_WATCH)};
#elif defined(NK_PARITY_BACKEND_WEB)
    return {"Web",
            cap(NK_CAP_WINDOW) | cap(NK_CAP_INPUT) | cap(NK_CAP_OPENGL_ES_SURFACE) |
                cap(NK_CAP_CURSOR) | cap(NK_CAP_POINTER_CAPTURE) | cap(NK_CAP_CLIPBOARD) |
                cap(NK_CAP_DRAG_DROP) | cap(NK_CAP_WINDOW_GEOMETRY) | cap(NK_CAP_WINDOW_STYLING) |
                cap(NK_CAP_RESOURCE_SHARING) | cap(NK_CAP_RESOURCE_IO) | cap(NK_CAP_SYSTEM_INFO) |
                cap(NK_CAP_ACCESSIBILITY) | cap(NK_CAP_SHELL) | cap(NK_CAP_NOTIFICATION) |
                cap(NK_CAP_JOYSTICK) | cap(NK_CAP_SURFACE_FRAME_CALLBACK) |
                cap(NK_CAP_TRANSPORT),
            cap(NK_CAP_NATIVE_VIEW),
            cap(NK_CAP_WEBVIEW) | cap(NK_CAP_MOBILE_HOST) | cap(NK_CAP_EXPORT_NATIVE_WINDOW) |
                cap(NK_CAP_WRAP_NATIVE_WINDOW) | cap(NK_CAP_MONITOR) |
                cap(NK_CAP_MONITOR_FULLSCREEN) | cap(NK_CAP_OPENGL_SURFACE) |
                cap(NK_CAP_VULKAN_SURFACE) | cap(NK_CAP_D3D11_SURFACE) | cap(NK_CAP_METAL_SURFACE) |
                cap(NK_CAP_APPLICATION_PATH) | cap(NK_CAP_APPLICATION_STORAGE) |
                cap(NK_CAP_SYSTEM_FONTS) | cap(NK_CAP_WINDOW_CUSTOM_DECORATIONS) |
                cap(NK_CAP_FILE_WATCH) | cap(NK_CAP_CLIPBOARD_WATCH) |
                cap(NK_CAP_APPLICATION_MENU) | cap(NK_CAP_DYNAMIC_LIBRARY),
            cap(NK_CAP_SYSTEM_APPEARANCE) | cap(NK_CAP_KEEP_AWAKE) |
                cap(NK_CAP_DEVICE_ORIENTATION) | cap(NK_CAP_DISPLAY_ORIENTATION) |
                cap(NK_CAP_SENSORS) | cap(NK_CAP_HAPTICS) | cap(NK_CAP_GAMEPAD_RUMBLE)};
#else
    return {"fallback stub", cap(NK_CAP_RESOURCE_IO) | cap(NK_CAP_DYNAMIC_LIBRARY),
            k_known_capabilities & ~(cap(NK_CAP_RESOURCE_IO) | k_new_system_capabilities |
                                     cap(NK_CAP_APPLICATION_MENU)),
            cap(NK_CAP_APPLICATION_MENU), k_new_system_capabilities};
#endif
}

constexpr std::array<std::string_view, 7> k_contract_backend_names = {
    "Linux/GTK", "Windows", "macOS", "Android", "iOS", "Web", "fallback-stub"};

const char *current_backend_name() {
#if defined(NK_PARITY_BACKEND_LINUX)
    return "Linux/GTK";
#elif defined(NK_PARITY_BACKEND_WINDOWS)
    return "Windows";
#elif defined(NK_PARITY_BACKEND_MACOS)
    return "macOS";
#elif defined(NK_PARITY_BACKEND_ANDROID)
    return "Android";
#elif defined(NK_PARITY_BACKEND_IOS)
    return "iOS";
#elif defined(NK_PARITY_BACKEND_WEB)
    return "Web";
#else
    return "fallback-stub";
#endif
}

struct capability_name {
    std::string_view name;
    nk_capabilities value;
};

constexpr std::array<capability_name, 46> k_capability_names = {{
    {"WINDOW", NK_CAP_WINDOW},
    {"WEBVIEW", NK_CAP_WEBVIEW},
    {"CLIPBOARD", NK_CAP_CLIPBOARD},
    {"DRAG_DROP", NK_CAP_DRAG_DROP},
    {"SHELL", NK_CAP_SHELL},
    {"SYSTEM_APPEARANCE", NK_CAP_SYSTEM_APPEARANCE},
    {"EXPORT_NATIVE_WINDOW", NK_CAP_EXPORT_NATIVE_WINDOW},
    {"WRAP_NATIVE_WINDOW", NK_CAP_WRAP_NATIVE_WINDOW},
    {"NOTIFICATION", NK_CAP_NOTIFICATION},
    {"MOBILE_HOST", NK_CAP_MOBILE_HOST},
    {"INPUT", NK_CAP_INPUT},
    {"OPENGL_SURFACE", NK_CAP_OPENGL_SURFACE},
    {"OPENGL_ES_SURFACE", NK_CAP_OPENGL_ES_SURFACE},
    {"CURSOR", NK_CAP_CURSOR},
    {"POINTER_CAPTURE", NK_CAP_POINTER_CAPTURE},
    {"WINDOW_GEOMETRY", NK_CAP_WINDOW_GEOMETRY},
    {"WINDOW_STYLING", NK_CAP_WINDOW_STYLING},
    {"WINDOW_CUSTOM_DECORATIONS", NK_CAP_WINDOW_CUSTOM_DECORATIONS},
    {"NATIVE_VIEW", NK_CAP_NATIVE_VIEW},
    {"MONITOR", NK_CAP_MONITOR},
    {"MONITOR_FULLSCREEN", NK_CAP_MONITOR_FULLSCREEN},
    {"JOYSTICK", NK_CAP_JOYSTICK},
    {"RESOURCE_SHARING", NK_CAP_RESOURCE_SHARING},
    {"RESOURCE_IO", NK_CAP_RESOURCE_IO},
    {"VULKAN_SURFACE", NK_CAP_VULKAN_SURFACE},
    {"ACCESSIBILITY", NK_CAP_ACCESSIBILITY},
    {"D3D11_SURFACE", NK_CAP_D3D11_SURFACE},
    {"METAL_SURFACE", NK_CAP_METAL_SURFACE},
    {"SYSTEM_INFO", NK_CAP_SYSTEM_INFO},
    {"APPLICATION_PATH", NK_CAP_APPLICATION_PATH},
    {"APPLICATION_STORAGE", NK_CAP_APPLICATION_STORAGE},
    {"SYSTEM_FONTS", NK_CAP_SYSTEM_FONTS},
    {"KEEP_AWAKE", NK_CAP_KEEP_AWAKE},
    {"DEVICE_ORIENTATION", NK_CAP_DEVICE_ORIENTATION},
    {"DISPLAY_ORIENTATION", NK_CAP_DISPLAY_ORIENTATION},
    {"HTTP_CLIENT", NK_CAP_HTTP_CLIENT},
    {"HTTP_STREAMING", NK_CAP_HTTP_STREAMING},
    {"SURFACE_FRAME_CALLBACK", NK_CAP_SURFACE_FRAME_CALLBACK},
    {"SENSORS", NK_CAP_SENSORS},
    {"HAPTICS", NK_CAP_HAPTICS},
    {"GAMEPAD_RUMBLE", NK_CAP_GAMEPAD_RUMBLE},
    {"FILE_WATCH", NK_CAP_FILE_WATCH},
    {"CLIPBOARD_WATCH", NK_CAP_CLIPBOARD_WATCH},
    {"APPLICATION_MENU", NK_CAP_APPLICATION_MENU},
    {"TRANSPORT", NK_CAP_TRANSPORT},
    {"DYNAMIC_LIBRARY", NK_CAP_DYNAMIC_LIBRARY},
}};

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return value;
}

bool parse_capability_list(std::string_view text, mask &out, const char *backend,
                           const char *category) {
    out = 0;
    text = trim(text);
    if (text.empty() || text == "NONE")
        return true;
    while (!text.empty()) {
        const auto separator = text.find(',');
        const auto token = trim(text.substr(0, separator));
        const auto found =
            std::find_if(k_capability_names.begin(), k_capability_names.end(),
                         [token](const capability_name &item) { return item.name == token; });
        if (token.empty() || found == k_capability_names.end()) {
            std::fprintf(stderr, "%s snapshot has unknown %s capability '%.*s'\n", backend,
                         category, static_cast<int>(token.size()), token.data());
            return false;
        }
        const auto value = cap(found->value);
        if (out & value) {
            std::fprintf(stderr, "%s snapshot repeats %s capability '%.*s'\n", backend, category,
                         static_cast<int>(token.size()), token.data());
            return false;
        }
        out |= value;
        if (separator == std::string_view::npos)
            break;
        text.remove_prefix(separator + 1);
    }
    return true;
}

bool check_disjoint_masks(const char *backend, mask required, mask deferred, mask not_applicable,
                          mask optional) {
    const mask categories = required | deferred | not_applicable | optional;
    const mask overlap = (required & deferred) | (required & not_applicable) |
                         (required & optional) | (deferred & not_applicable) |
                         (deferred & optional) | (not_applicable & optional);
    if (categories != k_known_capabilities) {
        std::fprintf(stderr, "%s contract does not classify every capability bit: 0x%llx\n",
                     backend, static_cast<unsigned long long>(k_known_capabilities ^ categories));
        return false;
    }
    if (overlap != 0) {
        std::fprintf(stderr, "%s contract has overlapping categories: 0x%llx\n", backend,
                     static_cast<unsigned long long>(overlap));
        return false;
    }
    return true;
}

bool load_contract(backend_contract &contract) {
    std::ifstream input(NK_CAPABILITY_SNAPSHOT_FILE);
    if (!input) {
        std::fprintf(stderr, "could not open capability snapshot: %s\n",
                     NK_CAPABILITY_SNAPSHOT_FILE);
        return false;
    }
    std::string line;
    std::vector<std::string> seen_backends;
    bool found_backend = false;
    while (std::getline(input, line)) {
        const auto content = trim(line);
        if (content.empty() || content.front() == '#')
            continue;
        std::istringstream fields_stream{std::string(content)};
        std::vector<std::string> fields;
        std::string field;
        while (std::getline(fields_stream, field, '|'))
            fields.push_back(std::move(field));
        if (fields.size() != 5) {
            std::fprintf(stderr, "malformed capability snapshot row: %s\n", line.c_str());
            return false;
        }
        const auto backend = trim(fields[0]);
        if (std::find(k_contract_backend_names.begin(), k_contract_backend_names.end(), backend) ==
            k_contract_backend_names.end()) {
            std::fprintf(stderr, "unknown capability snapshot backend '%.*s'\n",
                         static_cast<int>(backend.size()), backend.data());
            return false;
        }
        if (std::find(seen_backends.begin(), seen_backends.end(), backend) != seen_backends.end()) {
            std::fprintf(stderr, "duplicate capability snapshot row for %.*s\n",
                         static_cast<int>(backend.size()), backend.data());
            return false;
        }
        seen_backends.emplace_back(backend);
        mask required = 0;
        mask deferred = 0;
        mask not_applicable = 0;
        mask optional = 0;
        const std::string backend_name(backend);
        if (!parse_capability_list(fields[1], required, backend_name.c_str(), "required") ||
            !parse_capability_list(fields[2], deferred, backend_name.c_str(), "deferred") ||
            !parse_capability_list(fields[3], not_applicable, backend_name.c_str(),
                                   "not-applicable") ||
            !parse_capability_list(fields[4], optional, backend_name.c_str(), "optional") ||
            !check_disjoint_masks(backend_name.c_str(), required, deferred, not_applicable,
                                  optional))
            return false;
        if (backend != contract.name)
            continue;
        found_backend = true;
        contract.required = required;
        contract.deferred = deferred;
        contract.not_applicable = not_applicable;
        contract.optional = optional;
    }
    for (const auto expected : k_contract_backend_names) {
        if (std::find(seen_backends.begin(), seen_backends.end(), expected) ==
            seen_backends.end()) {
            std::fprintf(stderr, "capability snapshot has no row for %.*s\n",
                         static_cast<int>(expected.size()), expected.data());
            return false;
        }
    }
    if (!found_backend) {
        std::fprintf(stderr, "capability snapshot has no row for %s\n", contract.name);
        return false;
    }
    return true;
}

bool check_disjoint(const backend_contract &contract) {
    return check_disjoint_masks(contract.name, contract.required, contract.deferred,
                                contract.not_applicable, contract.optional);
}

nk_result read_system_string(nk_system_string_kind kind, std::string &value) {
    uint32_t size = 0;
    const auto query = nk_system_get_string(kind, nullptr, &size);
    if (query != NK_ERROR_BUFFER_TOO_SMALL || !size)
        return query;
    std::vector<char> buffer(size, '\0');
    auto capacity = size;
    const auto result = nk_system_get_string(kind, buffer.data(), &capacity);
    if (result == NK_OK)
        value.assign(buffer.data());
    return result;
}

bool probe_system_identity() {
    nk_system_info info{};
    info.struct_size = sizeof(info);
    if (nk_system_get_info(&info) != NK_OK ||
        (info.endianness != NK_SYSTEM_ENDIAN_LITTLE && info.endianness != NK_SYSTEM_ENDIAN_BIG)) {
        std::fprintf(stderr, "system info did not report a stable platform identity\n");
        return false;
    }
    if (info.platform == NK_SYSTEM_PLATFORM_UNKNOWN)
        return true;

    constexpr std::array required = {
        NK_SYSTEM_STRING_PLATFORM_NAME, NK_SYSTEM_STRING_PLATFORM_VERSION,
        NK_SYSTEM_STRING_PLATFORM_LABEL, NK_SYSTEM_STRING_APPLICATION_ID,
        NK_SYSTEM_STRING_APPLICATION_NAME};
    for (const auto kind : required) {
        std::string value;
        if (read_system_string(kind, value) != NK_OK || value.empty()) {
            std::fprintf(stderr, "required system string %u is unavailable\n",
                         static_cast<unsigned>(kind));
            return false;
        }
    }

    for (const auto kind : {NK_SYSTEM_STRING_DEVICE_VENDOR, NK_SYSTEM_STRING_DEVICE_MODEL}) {
        std::string value;
        const auto result = read_system_string(kind, value);
        if (result != NK_OK && result != NK_ERROR_UNSUPPORTED) {
            std::fprintf(stderr, "device system string %u returned %d\n",
                         static_cast<unsigned>(kind), result);
            return false;
        }
        if (result == NK_OK && value.empty()) {
            std::fprintf(stderr, "device system string %u was reported empty\n",
                         static_cast<unsigned>(kind));
            return false;
        }
    }
    return true;
}

} // namespace

#if defined(__EMSCRIPTEN__)
static void mark_browser_result(bool passed) {
    EM_ASM(
        { document.documentElement.dataset.nativekitPlatformParity = $0 ? "passed" : "failed"; },
        passed ? 1 : 0);
}
#endif

int main() {
    backend_contract contract{current_backend_name(), 0, 0, 0, 0};
    if (!load_contract(contract) || !check_disjoint(contract)) {
#if defined(__EMSCRIPTEN__)
        mark_browser_result(false);
#endif
        return 1;
    }

    nk_init_options options = {};
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    options.application_id = "org.nativekit.platform-parity";
    options.application_name = "NativeKit platform parity";
    if (nk_init(&options) != NK_OK) {
        std::fprintf(stderr, "could not initialize NativeKit for %s parity test: %s\n",
                     contract.name, nk_last_error());
#if defined(__EMSCRIPTEN__)
        mark_browser_result(false);
#endif
        return 1;
    }

    const mask actual = cap(nk_get_capabilities());
    const mask expected = contract.required;
    const mask allowed = expected | contract.optional;
    bool valid = true;
    if ((actual & expected) != expected) {
        std::fprintf(stderr,
                     "%s is missing required capability bits: expected 0x%llx, got 0x%llx\n",
                     contract.name, static_cast<unsigned long long>(expected),
                     static_cast<unsigned long long>(actual));
        valid = false;
    }
    if ((actual & contract.not_applicable) != 0) {
        std::fprintf(stderr, "%s advertises not-applicable capability bits: 0x%llx\n",
                     contract.name,
                     static_cast<unsigned long long>(actual & contract.not_applicable));
        valid = false;
    }
    if ((actual & contract.deferred) != 0) {
        std::fprintf(stderr, "%s advertises deferred capability bits: 0x%llx\n", contract.name,
                     static_cast<unsigned long long>(actual & contract.deferred));
        valid = false;
    }
    if ((actual & ~allowed) != 0) {
        std::fprintf(stderr, "%s advertises capability bits absent from its contract: 0x%llx\n",
                     contract.name, static_cast<unsigned long long>(actual & ~allowed));
        valid = false;
    }

    valid = probe_system_identity() && valid;
    nk_shutdown();
#if defined(__EMSCRIPTEN__)
    mark_browser_result(valid);
#endif
    return valid ? 0 : 1;
}
