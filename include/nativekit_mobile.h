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

/** Native container type accepted by nk_mobile_host_attach(). */
typedef uint32_t nk_mobile_host_kind;
/** Lifecycle state mirrored from the host application's platform container. */
typedef uint32_t nk_mobile_lifecycle_state;
/** Platform event type accepted by nk_mobile_host_dispatch_event(). */
typedef uint32_t nk_mobile_host_event_kind;

enum {
    /** Android android.view.ViewGroup; requires Android JNI values. */
    NK_MOBILE_HOST_ANDROID_VIEW_GROUP = 1,
    /** iOS UIView container; reserved for the UIKit backend. */
    NK_MOBILE_HOST_UIKIT_VIEW = 2
};

enum {
    /** The host is active and may receive input and rendering callbacks. */
    NK_MOBILE_LIFECYCLE_ACTIVE = 1,
    /** The host is visible but temporarily inactive. */
    NK_MOBILE_LIFECYCLE_INACTIVE = 2,
    /** The host is backgrounded and should pause active work. */
    NK_MOBILE_LIFECYCLE_BACKGROUND = 3
};

enum {
    /** Android Intent supplied with a current JNIEnv* and Intent jobject. */
    NK_MOBILE_HOST_EVENT_ANDROID_INTENT = 1
};

/* ------------------------------------------------------------------------- */
/* Mobile host data                                                          */
/* ------------------------------------------------------------------------- */

/** Platform values used to attach a caller-owned mobile container. */
typedef struct nk_mobile_host_options {
    /** Set to sizeof(nk_mobile_host_options) before attaching. */
    uint32_t struct_size;
    /** Kind of native container described by the remaining fields. */
    nk_mobile_host_kind kind;
    /** Platform context used only during attachment, such as Android JNIEnv*. */
    uintptr_t platform_context;
    /** Native container used only during attachment, such as an Android ViewGroup. */
    uintptr_t native_view;
    /** Reserved for future host options; set all elements to zero. */
    uint64_t reserved[2];
} nk_mobile_host_options;

/** Payload of NK_EVENT_MOBILE_HOST_GEOMETRY_CHANGED, in logical pixels. */
typedef struct nk_mobile_host_geometry {
    /** Set to sizeof(nk_mobile_host_geometry). */
    uint32_t struct_size;
    /** Host width in logical pixels. */
    int32_t width;
    /** Host height in logical pixels. */
    int32_t height;
    /** Scale from logical pixels to device pixels. */
    float scale;
    /** Left safe-area inset in logical pixels. */
    int32_t inset_left;
    /** Top safe-area inset in logical pixels. */
    int32_t inset_top;
    /** Right safe-area inset in logical pixels. */
    int32_t inset_right;
    /** Bottom safe-area inset in logical pixels. */
    int32_t inset_bottom;
    /** Software-keyboard inset at the bottom in logical pixels. */
    int32_t keyboard_bottom;
    /** Reserved for future geometry information; set all elements to zero. */
    uint64_t reserved[2];
} nk_mobile_host_geometry;

/** Platform-owned event values forwarded by a mobile host. */
typedef struct nk_mobile_host_event {
    /** Set to sizeof(nk_mobile_host_event) before dispatching. */
    uint32_t struct_size;
    /** Platform event kind. */
    nk_mobile_host_event_kind kind;
    /** Platform context used while dispatching, such as Android JNIEnv*. */
    uintptr_t platform_context;
    /** Platform event object, such as an Android Intent jobject. */
    uintptr_t native_event;
    /** Reserved for future event data; set all elements to zero. */
    uint64_t reserved[2];
} nk_mobile_host_event;

/* ------------------------------------------------------------------------- */
/* Mobile host lifecycle                                                     */
/* ------------------------------------------------------------------------- */

/**
 * Attaches NativeKit to a caller-owned mobile container. The caller retains
 * ownership and must keep the container alive until nk_mobile_host_destroy().
 *
 * On Android, platform_context is a JNIEnv* and native_view is a local or
 * global jobject referring to an android.view.ViewGroup. Both values are used
 * only during this call; NativeKit retains its own global reference.
 */
NK_API nk_result NK_CALL nk_mobile_host_attach(const nk_mobile_host_options *options,
                                               nk_handle *out_host);

/** Destroys all child WebViews, releases native references, and detaches. */
NK_API nk_result NK_CALL nk_mobile_host_destroy(nk_handle host);

/** Mirrors the lifecycle state owned by the host Activity or view controller. */
NK_API nk_result NK_CALL nk_mobile_host_set_lifecycle(nk_handle host,
                                                      nk_mobile_lifecycle_state state);

/* ------------------------------------------------------------------------- */
/* Mobile host events and drops                                              */
/* ------------------------------------------------------------------------- */

/**
 * Forwards a platform-owned host event while it is valid. Android supplies the
 * current JNIEnv* and an Intent jobject. Recognized content is copied into the
 * NativeKit event queue before this function returns.
 */
NK_API nk_result NK_CALL nk_mobile_host_dispatch_event(nk_handle host,
                                                       const nk_mobile_host_event *event);

/** Enables or disables URI/text drops onto the caller-owned host container. */
NK_API nk_result NK_CALL nk_mobile_host_set_drop_enabled(nk_handle host, nk_bool enabled);

#ifdef __cplusplus
}
#endif

#endif
