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
    NK_CAP_WRAP_NATIVE_WINDOW = UINT64_C(1) << 8,
    NK_CAP_NOTIFICATION = UINT64_C(1) << 9,
    NK_CAP_MOBILE_HOST = UINT64_C(1) << 10,
    NK_CAP_INPUT = UINT64_C(1) << 11,
    NK_CAP_OPENGL_SURFACE = UINT64_C(1) << 12,
    NK_CAP_OPENGL_ES_SURFACE = UINT64_C(1) << 13,
    NK_CAP_CURSOR = UINT64_C(1) << 14,
    NK_CAP_POINTER_CAPTURE = UINT64_C(1) << 15,
    NK_CAP_WINDOW_GEOMETRY = UINT64_C(1) << 16,
    NK_CAP_WINDOW_STYLING = UINT64_C(1) << 17,
    NK_CAP_MONITOR = UINT64_C(1) << 18,
    NK_CAP_MONITOR_FULLSCREEN = UINT64_C(1) << 19,
    NK_CAP_JOYSTICK = UINT64_C(1) << 20,
    NK_CAP_RESOURCE_SHARING = UINT64_C(1) << 21,
    NK_CAP_RESOURCE_IO = UINT64_C(1) << 22,
    NK_CAP_VULKAN_SURFACE = UINT64_C(1) << 23
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
    NK_WINDOW_HIDDEN = 1u << 1,
    NK_WINDOW_BORDERLESS = 1u << 2,
    NK_WINDOW_MODAL = 1u << 3
};

typedef uint32_t nk_window_kind;

enum { NK_WINDOW_NORMAL = 0, NK_WINDOW_UTILITY = 1 };

typedef struct nk_window_options {
    uint32_t struct_size;
    uint32_t flags;
    int32_t width;
    int32_t height;
    const char *title;
    nk_handle owner;
    nk_window_kind kind;
    uint64_t reserved;
} nk_window_options;

/* Payload of NK_EVENT_WINDOW_RESIZE. Dimensions are logical pixels. */
typedef struct nk_window_resize_event {
    int32_t width;
    int32_t height;
} nk_window_resize_event;

typedef struct nk_window_move_event {
    int32_t x;
    int32_t y;
} nk_window_move_event;

typedef struct nk_window_framebuffer_resize_event {
    int32_t width;
    int32_t height;
} nk_window_framebuffer_resize_event;

/* Payload of NK_EVENT_WINDOW_SCALE_CHANGED. */
typedef struct nk_window_scale_event {
    float scale;
} nk_window_scale_event;

typedef struct nk_window_content_scale {
    uint32_t struct_size;
    float x;
    float y;
    uint32_t reserved;
    uint64_t reserved2[2];
} nk_window_content_scale;

typedef struct nk_window_frame_extents {
    uint32_t struct_size;
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
    uint32_t reserved;
    uint64_t reserved2[2];
} nk_window_frame_extents;

enum {
    NK_WINDOW_STATE_VISIBLE = 1u << 0,
    NK_WINDOW_STATE_ACTIVE = 1u << 1,
    NK_WINDOW_STATE_MINIMIZED = 1u << 2,
    NK_WINDOW_STATE_MAXIMIZED = 1u << 3,
    NK_WINDOW_STATE_FULLSCREEN = 1u << 4,
    /* The application has requested compositor attention and focus has not returned. */
    NK_WINDOW_STATE_ATTENTION_REQUESTED = 1u << 5
};

typedef struct nk_window_state {
    uint32_t struct_size;
    uint32_t flags;
    uint64_t reserved[2];
} nk_window_state;

typedef struct nk_window_size_limits {
    uint32_t struct_size;
    int32_t min_width;
    int32_t min_height;
    int32_t max_width;
    int32_t max_height;
    uint64_t reserved[2];
} nk_window_size_limits;

/* Returns process-wide capabilities of the compiled platform backend. */
NK_API nk_capabilities NK_CALL nk_get_capabilities(void);

/*
 * Creates a NativeKit-owned top-level window on the UI thread. Dimensions are
 * logical pixels and must be positive. `title` is nullable UTF-8. On success,
 * `out_window` receives a non-zero generation-checked handle. `owner` may name
 * an existing top-level window; owned windows are destroyed recursively with
 * their owner. Modal windows require an owner. Window ownership is deliberately
 * shallow and does not introduce a general-purpose widget hierarchy.
 */
NK_API nk_result NK_CALL nk_window_create(const nk_window_options *options, nk_handle *out_window);
NK_API nk_result NK_CALL nk_window_destroy(nk_handle window);

/* Shows when `visible` is non-zero and hides otherwise. UI thread only. */
NK_API nk_result NK_CALL nk_window_show(nk_handle window, uint32_t visible);

/* Copies the nullable UTF-8 title before returning. UI thread only. */
NK_API nk_result NK_CALL nk_window_set_title(nk_handle window, const char *title);

/* Moves and resizes a top-level window in logical pixels. UI thread only. */
NK_API nk_result NK_CALL nk_window_set_bounds(nk_handle window, int32_t x, int32_t y, int32_t width,
                                              int32_t height);

/* Writes the current logical-to-device-pixel scale. UI thread only. */
NK_API nk_result NK_CALL nk_window_get_scale(nk_handle window, float *out_scale);
NK_API nk_result NK_CALL nk_window_get_content_scale(nk_handle window,
                                                     nk_window_content_scale *out_scale);
NK_API nk_result NK_CALL nk_window_get_position(nk_handle window, int32_t *out_x,
                                                int32_t *out_y);
NK_API nk_result NK_CALL nk_window_get_size(nk_handle window, int32_t *out_width,
                                            int32_t *out_height);
NK_API nk_result NK_CALL nk_window_get_framebuffer_size(nk_handle window,
                                                        int32_t *out_width,
                                                        int32_t *out_height);
NK_API nk_result NK_CALL nk_window_get_frame_extents(nk_handle window,
                                                     nk_window_frame_extents *out_extents);
NK_API nk_result NK_CALL nk_window_get_state(nk_handle window, nk_window_state *out_state);
NK_API nk_result NK_CALL nk_window_is_focused(nk_handle window, uint32_t *out_focused);
NK_API nk_result NK_CALL nk_window_is_visible(nk_handle window, uint32_t *out_visible);
NK_API nk_result NK_CALL nk_window_minimize(nk_handle window);
NK_API nk_result NK_CALL nk_window_maximize(nk_handle window);
NK_API nk_result NK_CALL nk_window_restore(nk_handle window);
NK_API nk_result NK_CALL nk_window_activate(nk_handle window);
NK_API nk_result NK_CALL nk_window_set_fullscreen(nk_handle window, uint32_t enabled);
NK_API nk_result NK_CALL nk_window_request_attention(nk_handle window);
/* Zero disables the corresponding constraint; maxima must not be below minima. */
NK_API nk_result NK_CALL nk_window_set_size_limits(nk_handle window,
                                                   const nk_window_size_limits *limits);
/* Passing zero for both values disables the aspect-ratio constraint. */
NK_API nk_result NK_CALL nk_window_set_aspect_ratio(nk_handle window, int32_t numerator,
                                                    int32_t denominator);
NK_API nk_result NK_CALL nk_window_set_resizable(nk_handle window, uint32_t enabled);
NK_API nk_result NK_CALL nk_window_set_decorated(nk_handle window, uint32_t enabled);
NK_API nk_result NK_CALL nk_window_set_floating(nk_handle window, uint32_t enabled);
NK_API nk_result NK_CALL nk_window_set_opacity(nk_handle window, float opacity);
NK_API nk_result NK_CALL nk_window_set_mouse_passthrough(nk_handle window, uint32_t enabled);
NK_API nk_result NK_CALL nk_window_get_hovered(nk_handle window, uint32_t *out_hovered);

/*
 * Returns a borrowed platform descriptor. Its pointer-sized values are valid
 * only while the NativeKit window is alive and must never be freed by callers.
 * This is an explicit interoperability escape hatch, not a portable resource.
 */
NK_API nk_result NK_CALL nk_window_get_native(nk_handle window, nk_native_window *out_native);

/*
 * Attaches NativeKit to a caller-owned native window. Destroying the returned
 * handle only detaches NativeKit. Backends return NK_ERROR_UNSUPPORTED until
 * they can guarantee correct event and ownership behavior for the given kind.
 */
NK_API nk_result NK_CALL nk_window_wrap_native(const nk_native_window *native,
                                               nk_handle *out_window);

#ifdef __cplusplus
}
#endif

#endif
