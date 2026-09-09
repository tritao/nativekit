#ifndef NATIVEKIT_WINDOW_H
#define NATIVEKIT_WINDOW_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t nk_capabilities;

enum {
    NK_CAP_WINDOW = UINT64_C(1) << 0,
    NK_CAP_WEBVIEW = UINT64_C(1) << 1,
    NK_CAP_FILE_DIALOG = UINT64_C(1) << 2,
    NK_CAP_CLIPBOARD = UINT64_C(1) << 3,
    NK_CAP_DRAG_DROP = UINT64_C(1) << 4,
    NK_CAP_SHELL = UINT64_C(1) << 5,
    NK_CAP_SYSTEM_APPEARANCE = UINT64_C(1) << 6,
    NK_CAP_EXPORT_NATIVE_WINDOW = UINT64_C(1) << 7,
    NK_CAP_WRAP_NATIVE_WINDOW = UINT64_C(1) << 8
};

typedef uint32_t nk_native_window_kind;

enum {
    NK_NATIVE_WINDOW_UNKNOWN = 0,
    NK_NATIVE_WINDOW_WIN32 = 1,
    NK_NATIVE_WINDOW_COCOA = 2,
    NK_NATIVE_WINDOW_X11 = 3,
    NK_NATIVE_WINDOW_WAYLAND = 4
};

typedef struct nk_native_window {
    uint32_t struct_size;
    nk_native_window_kind kind;
    uint32_t flags; /* Reserved for platform-neutral interop guarantees. */
    uint32_t reserved;
    uintptr_t display;
    uintptr_t window;
    uintptr_t view;
    uintptr_t reserved2[3];
} nk_native_window;

enum {
    NK_WINDOW_RESIZABLE = 1u << 0,
    NK_WINDOW_HIDDEN = 1u << 1
};

typedef struct nk_window_options {
    uint32_t struct_size;
    uint32_t flags;
    int32_t width;
    int32_t height;
    const char *title;
    uint64_t reserved[2];
} nk_window_options;

/* Payload of NK_EVENT_WINDOW_RESIZE. Dimensions are logical pixels. */
typedef struct nk_window_resize_event {
    int32_t width;
    int32_t height;
} nk_window_resize_event;

/* Payload of NK_EVENT_WINDOW_SCALE_CHANGED. */
typedef struct nk_window_scale_event {
    float scale;
} nk_window_scale_event;

/* Returns process-wide capabilities of the compiled platform backend. */
NK_API nk_capabilities NK_CALL nk_get_capabilities(void);

/*
 * Creates a NativeKit-owned top-level window on the UI thread. Dimensions are
 * logical pixels and must be positive. `title` is nullable UTF-8. On success,
 * `out_window` receives a non-zero generation-checked handle.
 */
NK_API nk_result NK_CALL nk_window_create(
    const nk_window_options *options,
    nk_handle *out_window);
NK_API nk_result NK_CALL nk_window_destroy(nk_handle window);

/* Shows when `visible` is non-zero and hides otherwise. UI thread only. */
NK_API nk_result NK_CALL nk_window_show(nk_handle window, uint32_t visible);

/* Copies the nullable UTF-8 title before returning. UI thread only. */
NK_API nk_result NK_CALL nk_window_set_title(nk_handle window, const char *title);

/* Moves and resizes a top-level window in logical pixels. UI thread only. */
NK_API nk_result NK_CALL nk_window_set_bounds(
    nk_handle window, int32_t x, int32_t y, int32_t width, int32_t height);

/* Writes the current logical-to-device-pixel scale. UI thread only. */
NK_API nk_result NK_CALL nk_window_get_scale(nk_handle window, float *out_scale);

/*
 * Returns a borrowed platform descriptor. Its pointer-sized values are valid
 * only while the NativeKit window is alive and must never be freed by callers.
 * This is an explicit interoperability escape hatch, not a portable resource.
 */
NK_API nk_result NK_CALL nk_window_get_native(
    nk_handle window, nk_native_window *out_native);

/*
 * Attaches NativeKit to a caller-owned native window. Destroying the returned
 * handle only detaches NativeKit. Backends return NK_ERROR_UNSUPPORTED until
 * they can guarantee correct event and ownership behavior for the given kind.
 */
NK_API nk_result NK_CALL nk_window_wrap_native(
    const nk_native_window *native, nk_handle *out_window);

#ifdef __cplusplus
}
#endif

#endif
