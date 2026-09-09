#ifndef NATIVEKIT_MOBILE_H
#define NATIVEKIT_MOBILE_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nk_mobile_host_kind;
typedef uint32_t nk_mobile_lifecycle_state;

enum { NK_MOBILE_HOST_ANDROID_VIEW_GROUP = 1, NK_MOBILE_HOST_UIKIT_VIEW = 2 };

enum {
    NK_MOBILE_LIFECYCLE_ACTIVE = 1,
    NK_MOBILE_LIFECYCLE_INACTIVE = 2,
    NK_MOBILE_LIFECYCLE_BACKGROUND = 3
};

typedef struct nk_mobile_host_options {
    uint32_t struct_size;
    nk_mobile_host_kind kind;
    uintptr_t platform_context;
    uintptr_t native_view;
    uint64_t reserved[2];
} nk_mobile_host_options;

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

#ifdef __cplusplus
}
#endif

#endif
