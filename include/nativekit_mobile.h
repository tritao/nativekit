#ifndef NATIVEKIT_MOBILE_H
#define NATIVEKIT_MOBILE_H

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
/* Mobile host kinds and lifecycle                                           */
/* ------------------------------------------------------------------------- */

typedef uint32_t nk_mobile_host_kind;
typedef uint32_t nk_mobile_lifecycle_state;
typedef uint32_t nk_mobile_host_event_kind;

enum { NK_MOBILE_HOST_ANDROID_VIEW_GROUP = 1, NK_MOBILE_HOST_UIKIT_VIEW = 2 };

enum {
    NK_MOBILE_LIFECYCLE_ACTIVE = 1,
    NK_MOBILE_LIFECYCLE_INACTIVE = 2,
    NK_MOBILE_LIFECYCLE_BACKGROUND = 3
};

enum { NK_MOBILE_HOST_EVENT_ANDROID_INTENT = 1 };

/* ------------------------------------------------------------------------- */
/* Mobile host data                                                          */
/* ------------------------------------------------------------------------- */

typedef struct nk_mobile_host_options {
    uint32_t struct_size;
    nk_mobile_host_kind kind;
    uintptr_t platform_context;
    uintptr_t native_view;
    uint64_t reserved[2];
} nk_mobile_host_options;

/* Payload of NK_EVENT_MOBILE_HOST_GEOMETRY_CHANGED, in logical pixels. */
typedef struct nk_mobile_host_geometry {
    uint32_t struct_size;
    int32_t width;
    int32_t height;
    float scale;
    int32_t inset_left;
    int32_t inset_top;
    int32_t inset_right;
    int32_t inset_bottom;
    int32_t keyboard_bottom;
    uint64_t reserved[2];
} nk_mobile_host_geometry;

typedef struct nk_mobile_host_event {
    uint32_t struct_size;
    nk_mobile_host_event_kind kind;
    uintptr_t platform_context;
    uintptr_t native_event;
    uint64_t reserved[2];
} nk_mobile_host_event;

/* ------------------------------------------------------------------------- */
/* Mobile host lifecycle                                                     */
/* ------------------------------------------------------------------------- */

/*
 * Attaches NativeKit to a caller-owned mobile container. The caller retains
 * ownership and must keep the container alive until nk_mobile_host_destroy().
 *
 * On Android, platform_context is a JNIEnv* and native_view is a local or
 * global jobject referring to an android.view.ViewGroup. Both values are used
 * only during this call; NativeKit retains its own global reference.
 */
NK_API nk_result NK_CALL nk_mobile_host_attach(const nk_mobile_host_options *options,
                                               nk_handle *out_host);

/* Destroys all child WebViews, releases native references, and detaches. */
NK_API nk_result NK_CALL nk_mobile_host_destroy(nk_handle host);

/* Mirrors the lifecycle state owned by the host Activity or view controller. */
NK_API nk_result NK_CALL nk_mobile_host_set_lifecycle(nk_handle host,
                                                      nk_mobile_lifecycle_state state);

/* ------------------------------------------------------------------------- */
/* Mobile host events and drops                                              */
/* ------------------------------------------------------------------------- */

/*
 * Forwards a platform-owned host event while it is valid. Android supplies the
 * current JNIEnv* and an Intent jobject. Recognized content is copied into the
 * NativeKit event queue before this function returns.
 */
NK_API nk_result NK_CALL nk_mobile_host_dispatch_event(nk_handle host,
                                                       const nk_mobile_host_event *event);

/* Enables or disables URI/text drops onto the caller-owned host container. */
NK_API nk_result NK_CALL nk_mobile_host_set_drop_enabled(nk_handle host, nk_bool enabled);

#ifdef __cplusplus
}
#endif

#endif
