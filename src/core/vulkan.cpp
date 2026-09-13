#include "nativekit_vulkan.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"
#include "core/vulkan_internal.hpp"
#include "nativekit_window.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

#if defined(NK_BACKEND_GTK) || defined(NK_BACKEND_ANDROID)
#include <dlfcn.h>
#endif
#if defined(NK_BACKEND_ANDROID)
#include "android/nativekit_android_internal.hpp"
#include <android/native_window.h>
#include <unordered_map>
#endif

namespace {
nk_result fail_loader(const char *message) {
    nk::core::set_error(message);
    return NK_ERROR_UNSUPPORTED;
}

#if defined(NK_BACKEND_GTK)
using VkInstance = void *;
using VkSurfaceKHR = std::uint64_t;
using VkResult = std::int32_t;
using VkFlags = std::uint32_t;
using VkStructureType = std::int32_t;
using GetInstanceProcAddr = void *(*)(VkInstance, const char *);
using DestroySurface = void (*)(VkInstance, VkSurfaceKHR, const void *);

constexpr VkResult vk_success = 0;
constexpr VkStructureType vk_structure_type_xlib_surface_create_info = 1000004000;
constexpr VkStructureType vk_structure_type_wayland_surface_create_info = 1000006000;

struct XlibSurfaceCreateInfo {
    VkStructureType s_type;
    const void *next;
    VkFlags flags;
    void *display;
    unsigned long window;
};

struct WaylandSurfaceCreateInfo {
    VkStructureType s_type;
    const void *next;
    VkFlags flags;
    void *display;
    void *surface;
};

using CreateXlibSurface = VkResult (*)(VkInstance, const XlibSurfaceCreateInfo *, const void *,
                                       VkSurfaceKHR *);
using CreateWaylandSurface = VkResult (*)(VkInstance, const WaylandSurfaceCreateInfo *,
                                          const void *, VkSurfaceKHR *);

void *vulkan_library() {
    static void *library = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    return library;
}

GetInstanceProcAddr get_instance_proc_addr() {
    static auto function = reinterpret_cast<GetInstanceProcAddr>(
        vulkan_library() ? dlsym(vulkan_library(), "vkGetInstanceProcAddr") : nullptr);
    return function;
}

nk_result native_window(nk_handle handle, nk_native_window &native) {
    native.struct_size = sizeof(native);
    const auto result = nk_window_get_native(handle, &native);
    if (result != NK_OK)
        return result;
    if (!nk::core::vulkan::platform_extension(native.kind)) {
        nk::core::set_error("the window's display backend cannot create Vulkan surfaces");
        return NK_ERROR_UNSUPPORTED;
    }
    return NK_OK;
}
#endif

#if defined(NK_BACKEND_ANDROID)
using VkInstance = void *;
using VkSurfaceKHR = std::uint64_t;
using VkResult = std::int32_t;
using VkFlags = std::uint32_t;
using VkStructureType = std::int32_t;
using GetInstanceProcAddr = void *(*)(VkInstance, const char *);
using DestroySurface = void (*)(VkInstance, VkSurfaceKHR, const void *);
constexpr VkResult vk_success = 0;
constexpr VkStructureType vk_structure_type_android_surface_create_info = 1000008000;

struct AndroidSurfaceCreateInfo {
    VkStructureType s_type;
    const void *next;
    VkFlags flags;
    ANativeWindow *window;
};

using CreateAndroidSurface = VkResult (*)(VkInstance, const AndroidSurfaceCreateInfo *,
                                          const void *, VkSurfaceKHR *);

std::unordered_map<VkSurfaceKHR, ANativeWindow *> android_surface_windows;

void *vulkan_library() {
    static void *library = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    return library;
}

GetInstanceProcAddr get_instance_proc_addr() {
    static auto function = reinterpret_cast<GetInstanceProcAddr>(
        vulkan_library() ? dlsym(vulkan_library(), "vkGetInstanceProcAddr") : nullptr);
    return function;
}
#endif
} // namespace

extern "C" {

uint32_t NK_CALL nk_vulkan_supported(void) {
#if defined(NK_BACKEND_GTK) || defined(NK_BACKEND_ANDROID)
    return get_instance_proc_addr() ? 1u : 0u;
#else
    return 0;
#endif
}

nk_result NK_CALL nk_vulkan_get_required_instance_extensions(nk_handle window,
                                                             const char **extensions,
                                                             uint32_t *inout_count) {
    return nk::core::result_boundary(
        "unexpected error while querying Vulkan extensions", [&]() -> nk_result {
            nk::core::clear_error();
            if (!inout_count) {
                nk::core::set_error("inout_count is required");
                return NK_ERROR_INVALID_ARGUMENT;
            }
#if defined(NK_BACKEND_GTK)
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            if (!get_instance_proc_addr())
                return fail_loader("Vulkan loader is unavailable");
            nk_native_window native{};
            if (const auto result = native_window(window, native); result != NK_OK)
                return result;
            static const char *surface = "VK_KHR_surface";
            const char *required[] = {surface, nk::core::vulkan::platform_extension(native.kind)};
            if (!extensions || *inout_count < 2) {
                *inout_count = 2;
                return NK_ERROR_BUFFER_TOO_SMALL;
            }
            extensions[0] = required[0];
            extensions[1] = required[1];
            *inout_count = 2;
            return NK_OK;
#elif defined(NK_BACKEND_ANDROID)
                                         if (const auto result = nk::core::require_ui_thread();
                                             result != NK_OK)
                                             return result;
                                         if (!get_instance_proc_addr())
                                             return fail_loader("Vulkan loader is unavailable");
                                         ANativeWindow *native = nullptr;
                                         if (const auto result = nk::backend::android_vulkan_window(
                                                 window, &native, false);
                                             result != NK_OK)
                                             return result;
                                         static const char *required[] = {"VK_KHR_surface",
                                                                          "VK_KHR_android_surface"};
                                         if (!extensions || *inout_count < 2) {
                                             *inout_count = 2;
                                             return NK_ERROR_BUFFER_TOO_SMALL;
                                         }
                                         extensions[0] = required[0];
                                         extensions[1] = required[1];
                                         *inout_count = 2;
                                         return NK_OK;
#else
                                         (void)window;
                                         (void)extensions;
                                         return fail_loader("Vulkan surfaces are unsupported");
#endif
        });
}

nk_result NK_CALL nk_vulkan_create_surface(nk_handle window, void *instance, const void *allocator,
                                           nk_vulkan_surface *out_surface) {
    return nk::core::result_boundary(
        "unexpected error while creating a Vulkan surface", [&]() -> nk_result {
            nk::core::clear_error();
#if defined(NK_BACKEND_GTK)
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            if (!instance || !out_surface) {
                nk::core::set_error("instance and out_surface are required");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            *out_surface = NK_INVALID_VULKAN_SURFACE;
            const auto get_proc = get_instance_proc_addr();
            if (!get_proc)
                return fail_loader("Vulkan loader is unavailable");
            nk_native_window native{};
            if (const auto result = native_window(window, native); result != NK_OK)
                return result;
            VkSurfaceKHR surface = 0;
            VkResult result = -1;
            if (native.kind == NK_NATIVE_WINDOW_X11) {
                const auto create = reinterpret_cast<CreateXlibSurface>(
                    get_proc(instance, "vkCreateXlibSurfaceKHR"));
                if (!create)
                    return fail_loader("VK_KHR_xlib_surface is not enabled");
                const XlibSurfaceCreateInfo info{vk_structure_type_xlib_surface_create_info,
                                                 nullptr, 0,
                                                 reinterpret_cast<void *>(native.display),
                                                 static_cast<unsigned long>(native.window)};
                result = create(instance, &info, allocator, &surface);
            } else {
                const auto create = reinterpret_cast<CreateWaylandSurface>(
                    get_proc(instance, "vkCreateWaylandSurfaceKHR"));
                if (!create)
                    return fail_loader("VK_KHR_wayland_surface is not enabled");
                const WaylandSurfaceCreateInfo info{vk_structure_type_wayland_surface_create_info,
                                                    nullptr, 0,
                                                    reinterpret_cast<void *>(native.display),
                                                    reinterpret_cast<void *>(native.window)};
                result = create(instance, &info, allocator, &surface);
            }
            if (result != vk_success) {
                nk::core::set_error("Vulkan surface creation failed");
                return NK_ERROR_UNKNOWN;
            }
            *out_surface = surface;
            return NK_OK;
#elif defined(NK_BACKEND_ANDROID)
                                         if (const auto result = nk::core::require_ui_thread();
                                             result != NK_OK)
                                             return result;
                                         if (!instance || !out_surface) {
                                             nk::core::set_error(
                                                 "instance and out_surface are required");
                                             return NK_ERROR_INVALID_ARGUMENT;
                                         }
                                         *out_surface = NK_INVALID_VULKAN_SURFACE;
                                         const auto get_proc = get_instance_proc_addr();
                                         if (!get_proc)
                                             return fail_loader("Vulkan loader is unavailable");
                                         ANativeWindow *native = nullptr;
                                         if (const auto result = nk::backend::android_vulkan_window(
                                                 window, &native, true);
                                             result != NK_OK)
                                             return result;
                                         const auto create = reinterpret_cast<CreateAndroidSurface>(
                                             get_proc(instance, "vkCreateAndroidSurfaceKHR"));
                                         if (!create)
                                             return fail_loader(
                                                 "VK_KHR_android_surface is not enabled");
                                         const AndroidSurfaceCreateInfo info{
                                             vk_structure_type_android_surface_create_info,
                                             nullptr, 0, native};
                                         VkSurfaceKHR surface = 0;
                                         if (create(instance, &info, allocator, &surface) !=
                                             vk_success) {
                                             nk::core::set_error(
                                                 "Android Vulkan surface creation failed");
                                             return NK_ERROR_UNKNOWN;
                                         }
                                         ANativeWindow_acquire(native);
                                         android_surface_windows.emplace(surface, native);
                                         *out_surface = surface;
                                         return NK_OK;
#else
                                         (void)window;
                                         (void)instance;
                                         (void)allocator;
                                         (void)out_surface;
                                         return fail_loader("Vulkan surfaces are unsupported");
#endif
        });
}

nk_result NK_CALL nk_vulkan_destroy_surface(void *instance, nk_vulkan_surface surface,
                                            const void *allocator) {
    return nk::core::result_boundary(
        "unexpected error while destroying a Vulkan surface", [&]() -> nk_result {
            nk::core::clear_error();
#if defined(NK_BACKEND_GTK) || defined(NK_BACKEND_ANDROID)
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            if (!instance || !surface) {
                nk::core::set_error("instance and surface are required");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            const auto get_proc = get_instance_proc_addr();
            if (!get_proc)
                return fail_loader("Vulkan loader is unavailable");
            const auto destroy =
                reinterpret_cast<DestroySurface>(get_proc(instance, "vkDestroySurfaceKHR"));
            if (!destroy)
                return fail_loader("VK_KHR_surface is not enabled on the instance");
            destroy(instance, surface, allocator);
#if defined(NK_BACKEND_ANDROID)
            if (const auto found = android_surface_windows.find(surface);
                found != android_surface_windows.end()) {
                ANativeWindow_release(found->second);
                android_surface_windows.erase(found);
            }
#endif
            return NK_OK;
#else
                                         (void)instance;
                                         (void)surface;
                                         (void)allocator;
                                         return fail_loader("Vulkan surfaces are unsupported");
#endif
        });
}
}
