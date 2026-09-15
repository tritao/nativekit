#include "nativekit.h"
#include "nativekit_window.h"

#include <cstdint>
#include <cstdio>

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
    cap(NK_CAP_WINDOW) | cap(NK_CAP_WEBVIEW) | cap(NK_CAP_FILE_DIALOG) | cap(NK_CAP_CLIPBOARD) |
    cap(NK_CAP_DRAG_DROP) | cap(NK_CAP_SHELL) | cap(NK_CAP_SYSTEM_APPEARANCE) |
    cap(NK_CAP_EXPORT_NATIVE_WINDOW) | cap(NK_CAP_WRAP_NATIVE_WINDOW) | cap(NK_CAP_NOTIFICATION) |
    cap(NK_CAP_MOBILE_HOST) | cap(NK_CAP_INPUT) | cap(NK_CAP_OPENGL_SURFACE) |
    cap(NK_CAP_OPENGL_ES_SURFACE) | cap(NK_CAP_CURSOR) | cap(NK_CAP_POINTER_CAPTURE) |
    cap(NK_CAP_WINDOW_GEOMETRY) | cap(NK_CAP_WINDOW_STYLING) | cap(NK_CAP_MONITOR) |
    cap(NK_CAP_MONITOR_FULLSCREEN) | cap(NK_CAP_JOYSTICK) | cap(NK_CAP_RESOURCE_SHARING) |
    cap(NK_CAP_RESOURCE_IO) | cap(NK_CAP_VULKAN_SURFACE) | cap(NK_CAP_ACCESSIBILITY) |
    cap(NK_CAP_D3D11_SURFACE) | cap(NK_CAP_METAL_SURFACE) | cap(NK_CAP_SYSTEM_INFO) |
    cap(NK_CAP_APPLICATION_PATH) | cap(NK_CAP_APPLICATION_STORAGE) | cap(NK_CAP_SYSTEM_FONTS) |
    cap(NK_CAP_KEEP_AWAKE) | cap(NK_CAP_DEVICE_ORIENTATION) | cap(NK_CAP_DISPLAY_ORIENTATION) |
    cap(NK_CAP_HTTP_CLIENT) | cap(NK_CAP_HTTP_STREAMING);

#if defined(NK_BUILD_NET) && defined(NK_NET_BACKEND_STREAMING)
constexpr mask k_net_required = cap(NK_CAP_HTTP_CLIENT) | cap(NK_CAP_HTTP_STREAMING);
constexpr mask k_net_deferred = 0;
#elif defined(NK_BUILD_NET) && defined(NK_NET_BACKEND_FETCH)
constexpr mask k_net_required = cap(NK_CAP_HTTP_CLIENT);
constexpr mask k_net_deferred = cap(NK_CAP_HTTP_STREAMING);
#else
constexpr mask k_net_required = 0;
constexpr mask k_net_deferred = cap(NK_CAP_HTTP_CLIENT) | cap(NK_CAP_HTTP_STREAMING);
#endif

constexpr mask k_new_system_capabilities =
    cap(NK_CAP_SYSTEM_INFO) | cap(NK_CAP_APPLICATION_PATH) |
    cap(NK_CAP_APPLICATION_STORAGE) | cap(NK_CAP_SYSTEM_FONTS) | cap(NK_CAP_KEEP_AWAKE) |
    cap(NK_CAP_DEVICE_ORIENTATION) | cap(NK_CAP_DISPLAY_ORIENTATION);

constexpr mask k_linux_system_capabilities =
    k_new_system_capabilities & ~cap(NK_CAP_DEVICE_ORIENTATION);
constexpr mask k_mobile_system_capabilities =
    cap(NK_CAP_SYSTEM_INFO) | cap(NK_CAP_APPLICATION_STORAGE) | cap(NK_CAP_KEEP_AWAKE) |
    cap(NK_CAP_DEVICE_ORIENTATION) | cap(NK_CAP_DISPLAY_ORIENTATION);
constexpr mask k_ios_system_capabilities =
    k_mobile_system_capabilities | cap(NK_CAP_APPLICATION_PATH);

constexpr mask k_desktop_common =
    cap(NK_CAP_WINDOW) | cap(NK_CAP_FILE_DIALOG) | cap(NK_CAP_CLIPBOARD) | cap(NK_CAP_DRAG_DROP) |
    cap(NK_CAP_SHELL) | cap(NK_CAP_SYSTEM_APPEARANCE) | cap(NK_CAP_EXPORT_NATIVE_WINDOW) |
    cap(NK_CAP_NOTIFICATION) | cap(NK_CAP_INPUT) | cap(NK_CAP_CURSOR) |
    cap(NK_CAP_POINTER_CAPTURE) | cap(NK_CAP_RESOURCE_IO);

constexpr backend_contract platform_contract() {
#if defined(NK_PARITY_BACKEND_LINUX)
    return {"Linux/GTK",
            k_desktop_common | cap(NK_CAP_WEBVIEW) | cap(NK_CAP_OPENGL_SURFACE) |
                cap(NK_CAP_OPENGL_ES_SURFACE) | cap(NK_CAP_WINDOW_GEOMETRY) |
                cap(NK_CAP_WINDOW_STYLING) | cap(NK_CAP_MONITOR) | cap(NK_CAP_MONITOR_FULLSCREEN) |
                cap(NK_CAP_JOYSTICK) | cap(NK_CAP_VULKAN_SURFACE) | k_linux_system_capabilities,
            cap(NK_CAP_WRAP_NATIVE_WINDOW) | cap(NK_CAP_RESOURCE_SHARING) |
                cap(NK_CAP_ACCESSIBILITY) | cap(NK_CAP_D3D11_SURFACE) | cap(NK_CAP_METAL_SURFACE) |
                cap(NK_CAP_MOBILE_HOST) | cap(NK_CAP_DEVICE_ORIENTATION),
            0, 0};
#elif defined(NK_PARITY_BACKEND_WINDOWS)
    return {"Windows",
            k_desktop_common | cap(NK_CAP_WINDOW_GEOMETRY) | cap(NK_CAP_WINDOW_STYLING) |
                cap(NK_CAP_D3D11_SURFACE) | cap(NK_CAP_ACCESSIBILITY) | cap(NK_CAP_MONITOR) |
                cap(NK_CAP_MONITOR_FULLSCREEN) | cap(NK_CAP_JOYSTICK) | k_new_system_capabilities,
            cap(NK_CAP_RESOURCE_SHARING) | cap(NK_CAP_WRAP_NATIVE_WINDOW) |
                cap(NK_CAP_OPENGL_SURFACE) | cap(NK_CAP_OPENGL_ES_SURFACE) |
                cap(NK_CAP_VULKAN_SURFACE) | cap(NK_CAP_METAL_SURFACE) | cap(NK_CAP_MOBILE_HOST),
            0, cap(NK_CAP_WEBVIEW)};
#elif defined(NK_PARITY_BACKEND_MACOS)
    return {"macOS",
            k_desktop_common | cap(NK_CAP_WEBVIEW) | cap(NK_CAP_WINDOW_GEOMETRY) |
                cap(NK_CAP_WINDOW_STYLING) | cap(NK_CAP_METAL_SURFACE) | cap(NK_CAP_MONITOR) |
                cap(NK_CAP_MONITOR_FULLSCREEN) | cap(NK_CAP_JOYSTICK) | k_new_system_capabilities,
            cap(NK_CAP_RESOURCE_SHARING) | cap(NK_CAP_ACCESSIBILITY) |
                cap(NK_CAP_WRAP_NATIVE_WINDOW) | cap(NK_CAP_OPENGL_SURFACE) |
                cap(NK_CAP_OPENGL_ES_SURFACE) | cap(NK_CAP_VULKAN_SURFACE) |
                cap(NK_CAP_D3D11_SURFACE) | cap(NK_CAP_MOBILE_HOST),
            0, 0};
#elif defined(NK_PARITY_BACKEND_ANDROID)
    return {"Android",
            cap(NK_CAP_MOBILE_HOST) | cap(NK_CAP_WEBVIEW) | cap(NK_CAP_FILE_DIALOG) |
                cap(NK_CAP_CLIPBOARD) | cap(NK_CAP_DRAG_DROP) | cap(NK_CAP_SHELL) |
                cap(NK_CAP_SYSTEM_APPEARANCE) | cap(NK_CAP_NOTIFICATION) | cap(NK_CAP_INPUT) |
                cap(NK_CAP_OPENGL_ES_SURFACE) | cap(NK_CAP_VULKAN_SURFACE) |
                cap(NK_CAP_RESOURCE_SHARING) | cap(NK_CAP_RESOURCE_IO) | cap(NK_CAP_JOYSTICK) |
                cap(NK_CAP_ACCESSIBILITY) | k_mobile_system_capabilities,
            cap(NK_CAP_APPLICATION_PATH) | cap(NK_CAP_SYSTEM_FONTS),
            cap(NK_CAP_WINDOW) | cap(NK_CAP_EXPORT_NATIVE_WINDOW) | cap(NK_CAP_WRAP_NATIVE_WINDOW) |
                cap(NK_CAP_WINDOW_GEOMETRY) | cap(NK_CAP_WINDOW_STYLING) | cap(NK_CAP_MONITOR) |
                cap(NK_CAP_MONITOR_FULLSCREEN) | cap(NK_CAP_CURSOR) | cap(NK_CAP_POINTER_CAPTURE) |
                cap(NK_CAP_OPENGL_SURFACE) | cap(NK_CAP_D3D11_SURFACE) | cap(NK_CAP_METAL_SURFACE),
            0};
#elif defined(NK_PARITY_BACKEND_IOS)
    return {"iOS",
            cap(NK_CAP_MOBILE_HOST) | cap(NK_CAP_WEBVIEW) | cap(NK_CAP_METAL_SURFACE) |
                cap(NK_CAP_INPUT) | cap(NK_CAP_RESOURCE_IO) | cap(NK_CAP_CLIPBOARD) |
                cap(NK_CAP_SHELL) | cap(NK_CAP_SYSTEM_APPEARANCE) | cap(NK_CAP_NOTIFICATION) |
                k_ios_system_capabilities | cap(NK_CAP_FILE_DIALOG),
            cap(NK_CAP_DRAG_DROP) | cap(NK_CAP_RESOURCE_SHARING) | cap(NK_CAP_JOYSTICK) |
                cap(NK_CAP_ACCESSIBILITY),
            cap(NK_CAP_WINDOW) | cap(NK_CAP_EXPORT_NATIVE_WINDOW) | cap(NK_CAP_WRAP_NATIVE_WINDOW) |
                cap(NK_CAP_WINDOW_GEOMETRY) | cap(NK_CAP_WINDOW_STYLING) | cap(NK_CAP_MONITOR) |
                cap(NK_CAP_MONITOR_FULLSCREEN) | cap(NK_CAP_CURSOR) | cap(NK_CAP_POINTER_CAPTURE) |
                cap(NK_CAP_OPENGL_SURFACE) | cap(NK_CAP_OPENGL_ES_SURFACE) |
                cap(NK_CAP_VULKAN_SURFACE) | cap(NK_CAP_D3D11_SURFACE),
            0};
#elif defined(NK_PARITY_BACKEND_WEB)
    return {"Web",
            cap(NK_CAP_WINDOW) | cap(NK_CAP_INPUT) | cap(NK_CAP_OPENGL_ES_SURFACE) |
                cap(NK_CAP_CURSOR) | cap(NK_CAP_POINTER_CAPTURE) | cap(NK_CAP_CLIPBOARD) |
                cap(NK_CAP_WINDOW_GEOMETRY) | cap(NK_CAP_RESOURCE_IO) | cap(NK_CAP_SYSTEM_INFO),
            cap(NK_CAP_WINDOW_STYLING) | cap(NK_CAP_FILE_DIALOG) | cap(NK_CAP_DRAG_DROP) |
                cap(NK_CAP_SHELL) | cap(NK_CAP_NOTIFICATION) | cap(NK_CAP_RESOURCE_SHARING) |
                cap(NK_CAP_JOYSTICK) | cap(NK_CAP_ACCESSIBILITY) |
                cap(NK_CAP_APPLICATION_PATH) | cap(NK_CAP_APPLICATION_STORAGE) |
                cap(NK_CAP_SYSTEM_FONTS) | cap(NK_CAP_DEVICE_ORIENTATION),
            cap(NK_CAP_WEBVIEW) | cap(NK_CAP_MOBILE_HOST) | cap(NK_CAP_EXPORT_NATIVE_WINDOW) |
                cap(NK_CAP_WRAP_NATIVE_WINDOW) | cap(NK_CAP_MONITOR) |
                cap(NK_CAP_MONITOR_FULLSCREEN) | cap(NK_CAP_OPENGL_SURFACE) |
                cap(NK_CAP_VULKAN_SURFACE) | cap(NK_CAP_D3D11_SURFACE) | cap(NK_CAP_METAL_SURFACE),
            cap(NK_CAP_SYSTEM_APPEARANCE) | cap(NK_CAP_KEEP_AWAKE) |
                cap(NK_CAP_DISPLAY_ORIENTATION)};
#else
    return {"fallback stub", cap(NK_CAP_RESOURCE_IO),
            k_known_capabilities & ~(cap(NK_CAP_RESOURCE_IO) | k_new_system_capabilities), 0,
            k_new_system_capabilities};
#endif
}

constexpr backend_contract current_contract() {
    auto contract = platform_contract();
    contract.required |= k_net_required;
    contract.deferred |= k_net_deferred;
    return contract;
}

bool check_disjoint(const backend_contract &contract) {
    const mask categories =
        contract.required | contract.deferred | contract.not_applicable | contract.optional;
    const mask overlap =
        (contract.required & contract.deferred) | (contract.required & contract.not_applicable) |
        (contract.required & contract.optional) | (contract.deferred & contract.not_applicable) |
        (contract.deferred & contract.optional) | (contract.not_applicable & contract.optional);
    if (categories != k_known_capabilities) {
        std::fprintf(stderr, "%s contract does not classify every capability bit: 0x%llx\n",
                     contract.name,
                     static_cast<unsigned long long>(k_known_capabilities ^ categories));
        return false;
    }
    if (overlap != 0) {
        std::fprintf(stderr, "%s contract has overlapping categories: 0x%llx\n", contract.name,
                     static_cast<unsigned long long>(overlap));
        return false;
    }
    return true;
}

} // namespace

int main() {
    const auto contract = current_contract();
    if (!check_disjoint(contract))
        return 1;

    nk_init_options options = {};
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    if (nk_init(&options) != NK_OK) {
        std::fprintf(stderr, "could not initialize NativeKit for %s parity test: %s\n",
                     contract.name, nk_last_error());
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

    nk_shutdown();
    return valid ? 0 : 1;
}
