#ifndef NATIVEKIT_VULKAN_H
#define NATIVEKIT_VULKAN_H

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit.h"

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Vulkan types                                                              */
/* ------------------------------------------------------------------------- */

/** Application-owned VkSurfaceKHR bits returned by NativeKit. */
typedef uint64_t nk_vulkan_surface;
/** Sentinel used when no Vulkan surface has been created. */
#define NK_INVALID_VULKAN_SURFACE ((nk_vulkan_surface)0)

/* ------------------------------------------------------------------------- */
/* Vulkan capability and extension queries                                   */
/* ------------------------------------------------------------------------- */

/* Returns non-zero when a Vulkan loader and vkGetInstanceProcAddr are available. */
NK_API nk_bool NK_CALL nk_vulkan_supported(void);
/*
 * Returns the instance extensions required by the presentation resource. Pass
 * a window on desktop or an NK_GRAPHICS_VULKAN child surface on Android.
 * The returned string pointers have process lifetime. Pass NULL to query count.
 */
NK_API nk_result NK_CALL nk_vulkan_get_required_instance_extensions(
    nk_handle window, const char **extensions, uint32_t *inout_count);

/* ------------------------------------------------------------------------- */
/* Vulkan surface lifecycle                                                  */
/* ------------------------------------------------------------------------- */

/*
 * Creates a VkSurfaceKHR from the desktop window or Android Vulkan child
 * surface. `instance` is a VkInstance cast to void*. `allocator`
 * may point to VkAllocationCallbacks. The application owns the returned surface
 * and must destroy it before destroying the VkInstance.
 */
NK_API nk_result NK_CALL nk_vulkan_create_surface(nk_handle window, void *instance,
                                                  const void *allocator,
                                                  nk_vulkan_surface *out_surface);
/** Destroys an application-owned Vulkan surface before its VkInstance. */
NK_API nk_result NK_CALL nk_vulkan_destroy_surface(void *instance, nk_vulkan_surface surface,
                                                   const void *allocator);

#ifdef __cplusplus
}
#endif

#endif
