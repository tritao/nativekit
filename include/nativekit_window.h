#ifndef NATIVEKIT_WINDOW_H
#define NATIVEKIT_WINDOW_H

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
/* Capabilities and native-window types                                      */
/* ------------------------------------------------------------------------- */

/** Bitmask describing the optional facilities exposed by the active backend. */
typedef uint64_t nk_capabilities;

enum {
    /** The backend can create and manage NativeKit-owned top-level windows. */
    NK_CAP_WINDOW = UINT64_C(1) << 0,
    /** The backend can create embedded WebViews. */
    NK_CAP_WEBVIEW = UINT64_C(1) << 1,
    /** The backend can show native file dialogs. */
    NK_CAP_FILE_DIALOG = UINT64_C(1) << 2,
    /** The backend can read and write the system clipboard. */
    NK_CAP_CLIPBOARD = UINT64_C(1) << 3,
    /** The backend can report and accept drag-and-drop data. */
    NK_CAP_DRAG_DROP = UINT64_C(1) << 4,
    /** The backend can open files, URLs, or other resources with the system shell. */
    NK_CAP_SHELL = UINT64_C(1) << 5,
    /** The backend can query or observe the system appearance. */
    NK_CAP_SYSTEM_APPEARANCE = UINT64_C(1) << 6,
    /** The backend can export a descriptor for a NativeKit-owned native window. */
    NK_CAP_EXPORT_NATIVE_WINDOW = UINT64_C(1) << 7,
    /** The backend can wrap a caller-owned native window descriptor. */
    NK_CAP_WRAP_NATIVE_WINDOW = UINT64_C(1) << 8,
    /** The backend can post native system notifications. */
    NK_CAP_NOTIFICATION = UINT64_C(1) << 9,
    /** The backend supports a mobile host integration. */
    NK_CAP_MOBILE_HOST = UINT64_C(1) << 10,
    /** The backend can deliver keyboard, text, pointer, or touch input events. */
    NK_CAP_INPUT = UINT64_C(1) << 11,
    /** The backend can create or expose an OpenGL surface. */
    NK_CAP_OPENGL_SURFACE = UINT64_C(1) << 12,
    /** The backend can create or expose an OpenGL ES surface. */
    NK_CAP_OPENGL_ES_SURFACE = UINT64_C(1) << 13,
    /** The backend supports cursor creation or window cursor control. */
    NK_CAP_CURSOR = UINT64_C(1) << 14,
    /** The backend supports capturing the pointer to a window. */
    NK_CAP_POINTER_CAPTURE = UINT64_C(1) << 15,
    /** The backend supports querying and changing window geometry. */
    NK_CAP_WINDOW_GEOMETRY = UINT64_C(1) << 16,
    /** The backend supports changing window style or behavior at runtime. */
    NK_CAP_WINDOW_STYLING = UINT64_C(1) << 17,
    /** The backend can enumerate and query monitors. */
    NK_CAP_MONITOR = UINT64_C(1) << 18,
    /** The backend supports monitor-specific fullscreen operations. */
    NK_CAP_MONITOR_FULLSCREEN = UINT64_C(1) << 19,
    /** The backend can enumerate and query raw joysticks. */
    NK_CAP_JOYSTICK = UINT64_C(1) << 20,
    /** The backend supports sharing resources with another NativeKit subsystem. */
    NK_CAP_RESOURCE_SHARING = UINT64_C(1) << 21,
    /** The backend supports NativeKit resource I/O. */
    NK_CAP_RESOURCE_IO = UINT64_C(1) << 22,
    /** The backend can create or expose a Vulkan surface. */
    NK_CAP_VULKAN_SURFACE = UINT64_C(1) << 23,
    /** The backend supports NativeKit accessibility integration. */
    NK_CAP_ACCESSIBILITY = UINT64_C(1) << 24
};

/** Discriminator identifying the platform representation in nk_native_window. */
typedef uint32_t nk_native_window_kind;

enum {
    /** No platform representation is available. */
    NK_NATIVE_WINDOW_UNKNOWN = 0,
    /** The native window value is a Win32 HWND. */
    NK_NATIVE_WINDOW_WIN32 = 1,
    /** The native window and optional view values are Cocoa objects. */
    NK_NATIVE_WINDOW_COCOA = 2,
    /** The display and window values identify an X11 display and window. */
    NK_NATIVE_WINDOW_X11 = 3,
    /** The display and window values identify a Wayland display and surface. */
    NK_NATIVE_WINDOW_WAYLAND = 4
};

/** Borrowed platform handles for interoperating with a NativeKit window. */
typedef struct nk_native_window {
    /** Set to sizeof(nk_native_window) before passing this structure to NativeKit. */
    uint32_t struct_size;
    /** Identifies which platform-specific fields are populated. */
    nk_native_window_kind kind;
    /** Reserved for future platform-neutral interoperation flags; initialize to zero. */
    uint32_t flags;
    /** Reserved for future use; initialize to zero. */
    uint32_t reserved;
    /** Platform display connection, when the selected kind uses one. */
    uintptr_t display;
    /** Platform window or surface handle. */
    uintptr_t window;
    /** Optional platform view handle, currently used by Cocoa. */
    uintptr_t view;
    /** Reserved pointer-sized values; initialize to zero. */
    uintptr_t reserved2[3];
} nk_native_window;

/* ------------------------------------------------------------------------- */
/* Window options and state types                                            */
/* ------------------------------------------------------------------------- */

enum {
    /** The window can be resized by the user. */
    NK_WINDOW_RESIZABLE = 1u << 0,
    /** The window is initially hidden. */
    NK_WINDOW_HIDDEN = 1u << 1,
    /** The window is created without standard decorations. */
    NK_WINDOW_BORDERLESS = 1u << 2,
    /** The window is modal with respect to its owner. */
    NK_WINDOW_MODAL = 1u << 3
};

/** Category of a NativeKit-owned top-level window. */
typedef uint32_t nk_window_kind;

enum {
    /** A regular application window. */
    NK_WINDOW_NORMAL = 0,
    /** A utility or tool window. */
    NK_WINDOW_UTILITY = 1
};

/** Options for creating a NativeKit-owned top-level window. */
typedef struct nk_window_options {
    /** Set to sizeof(nk_window_options) before calling nk_window_create. */
    uint32_t struct_size;
    /** Combination of NK_WINDOW_* creation flags. */
    uint32_t flags;
    /** Initial client width in logical pixels; must be positive. */
    int32_t width;
    /** Initial client height in logical pixels; must be positive. */
    int32_t height;
    /** Optional NUL-terminated UTF-8 title, copied by the create call. */
    const char *title NK_NULLABLE_UTF8;
    /** Optional owner window; owned windows are destroyed with their owner. */
    nk_handle owner;
    /** Window category, such as NK_WINDOW_NORMAL or NK_WINDOW_UTILITY. */
    nk_window_kind kind;
    /** Reserved for future options; initialize to zero. */
    uint64_t reserved;
} nk_window_options;

/** Payload of NK_EVENT_WINDOW_RESIZE; dimensions are logical pixels. */
typedef struct nk_window_resize_event {
    /** New client width in logical pixels. */
    int32_t width;
    /** New client height in logical pixels. */
    int32_t height;
} nk_window_resize_event;

/** Payload of NK_EVENT_WINDOW_MOVE; coordinates are logical screen pixels. */
typedef struct nk_window_move_event {
    /** New logical x coordinate of the top-level window. */
    int32_t x;
    /** New logical y coordinate of the top-level window. */
    int32_t y;
} nk_window_move_event;

/** Payload of NK_EVENT_WINDOW_FRAMEBUFFER_RESIZE; dimensions are device pixels. */
typedef struct nk_window_framebuffer_resize_event {
    /** New framebuffer width in device pixels. */
    int32_t width;
    /** New framebuffer height in device pixels. */
    int32_t height;
} nk_window_framebuffer_resize_event;

/** Payload of NK_EVENT_WINDOW_SCALE_CHANGED. */
typedef struct nk_window_scale_event {
    /** New logical-to-device-pixel scale factor. */
    float scale;
} nk_window_scale_event;

/** Per-axis content scale returned for a window. */
typedef struct nk_window_content_scale {
    /** Set to sizeof(nk_window_content_scale) before calling the query API. */
    uint32_t struct_size;
    /** Horizontal logical-to-device-pixel scale factor. */
    float x;
    /** Vertical logical-to-device-pixel scale factor. */
    float y;
    /** Reserved for future use; initialize to zero. */
    uint32_t reserved;
    /** Reserved pointer-sized values; initialize to zero. */
    uint64_t reserved2[2];
} nk_window_content_scale;

/** Non-client frame extents returned in logical screen pixels. */
typedef struct nk_window_frame_extents {
    /** Set to sizeof(nk_window_frame_extents) before calling the query API. */
    uint32_t struct_size;
    /** Distance from the client area's left edge to the window frame. */
    int32_t left;
    /** Distance from the client area's top edge to the window frame. */
    int32_t top;
    /** Distance from the client area's right edge to the window frame. */
    int32_t right;
    /** Distance from the client area's bottom edge to the window frame. */
    int32_t bottom;
    /** Reserved for future use; initialize to zero. */
    uint32_t reserved;
    /** Reserved pointer-sized values; initialize to zero. */
    uint64_t reserved2[2];
} nk_window_frame_extents;

enum {
    /** The window is visible. */
    NK_WINDOW_STATE_VISIBLE = 1u << 0,
    /** The window is active or focused. */
    NK_WINDOW_STATE_ACTIVE = 1u << 1,
    /** The window is minimized. */
    NK_WINDOW_STATE_MINIMIZED = 1u << 2,
    /** The window is maximized. */
    NK_WINDOW_STATE_MAXIMIZED = 1u << 3,
    /** The window is fullscreen. */
    NK_WINDOW_STATE_FULLSCREEN = 1u << 4,
    /** Attention was requested and focus has not yet returned to the window. */
    NK_WINDOW_STATE_ATTENTION_REQUESTED = 1u << 5
};

/** Current window state flags returned by nk_window_get_state. */
typedef struct nk_window_state {
    /** Set to sizeof(nk_window_state) before calling nk_window_get_state. */
    uint32_t struct_size;
    /** Combination of NK_WINDOW_STATE_* flags. */
    uint32_t flags;
    /** Reserved pointer-sized values; initialize to zero. */
    uint64_t reserved[2];
} nk_window_state;

/** Logical-pixel size constraints for a window. */
typedef struct nk_window_size_limits {
    /** Set to sizeof(nk_window_size_limits) before passing it to NativeKit. */
    uint32_t struct_size;
    /** Minimum client width; zero disables the minimum. */
    int32_t min_width;
    /** Minimum client height; zero disables the minimum. */
    int32_t min_height;
    /** Maximum client width; zero disables the maximum. */
    int32_t max_width;
    /** Maximum client height; zero disables the maximum. */
    int32_t max_height;
    /** Reserved pointer-sized values; initialize to zero. */
    uint64_t reserved[2];
} nk_window_size_limits;

/* ------------------------------------------------------------------------- */
/* Capability and window lifecycle APIs                                      */
/* ------------------------------------------------------------------------- */

/** Returns process-wide capabilities of the compiled platform backend. */
NK_API nk_capabilities NK_CALL nk_get_capabilities(void);

/**
 * Creates a NativeKit-owned top-level window on the UI thread. Dimensions are
 * logical pixels and must be positive. `title` is nullable UTF-8. On success,
 * `out_window` receives a non-zero generation-checked handle. `owner` may name
 * an existing top-level window; owned windows are destroyed recursively with
 * their owner. Modal windows require an owner. Window ownership is deliberately
 * shallow and does not introduce a general-purpose widget hierarchy.
 */
NK_API nk_result NK_CALL nk_window_create(const nk_window_options *options,
                                          nk_handle *out_window NK_OUT);

/**
 * Destroys a NativeKit-owned window and recursively destroys its owned windows.
 * The handle and any child WebView handles become invalid after this call.
 *
 * @param window Window handle to destroy.
 * @return NK_OK, or an error such as NK_ERROR_INVALID_HANDLE.
 */
NK_API nk_result NK_CALL nk_window_destroy(nk_handle window);

/* ------------------------------------------------------------------------- */
/* Window visibility, geometry, and state APIs                                */
/* ------------------------------------------------------------------------- */

/** Shows when `visible` is non-zero and hides otherwise. UI thread only. */
NK_API nk_result NK_CALL nk_window_show(nk_handle window, nk_bool visible);

/** Copies the nullable UTF-8 title before returning. UI thread only. */
NK_API nk_result NK_CALL nk_window_set_title(nk_handle window, const char *title NK_UTF8);

/** Moves and resizes a top-level window in logical pixels. UI thread only. */
NK_API nk_result NK_CALL nk_window_set_bounds(nk_handle window, int32_t x, int32_t y, int32_t width,
                                              int32_t height);

/** Writes the current logical-to-device-pixel scale. UI thread only. */
NK_API nk_result NK_CALL nk_window_get_scale(nk_handle window, float *out_scale NK_OUT);

/** On NK_OK, fills in the current horizontal and vertical content scale factors. */
NK_API nk_result NK_CALL nk_window_get_content_scale(nk_handle window,
                                                     nk_window_content_scale *out_scale NK_OUT);

/** On NK_OK, fills in the window's logical screen position. */
NK_API nk_result NK_CALL nk_window_get_position(nk_handle window, int32_t *out_x NK_OUT,
                                                int32_t *out_y NK_OUT);

/** On NK_OK, fills in the window's logical client width and height. */
NK_API nk_result NK_CALL nk_window_get_size(nk_handle window, int32_t *out_width NK_OUT,
                                            int32_t *out_height NK_OUT);

/** On NK_OK, fills in the framebuffer width and height in device pixels. */
NK_API nk_result NK_CALL nk_window_get_framebuffer_size(nk_handle window,
                                                        int32_t *out_width NK_OUT,
                                                        int32_t *out_height NK_OUT);

/** On NK_OK, fills in the non-client frame extents in logical screen pixels. */
NK_API nk_result NK_CALL nk_window_get_frame_extents(nk_handle window,
                                                     nk_window_frame_extents *out_extents NK_OUT);

/** On NK_OK, fills in the current NK_WINDOW_STATE_* flags. */
NK_API nk_result NK_CALL nk_window_get_state(nk_handle window,
                                             nk_window_state *out_state NK_OUT);

/** Sets `*out_focused` to 1 if the window has focus, and 0 otherwise. */
NK_API nk_result NK_CALL nk_window_is_focused(nk_handle window, nk_bool *out_focused NK_OUT);

/** Sets `*out_visible` to 1 if the window is visible, and 0 otherwise. */
NK_API nk_result NK_CALL nk_window_is_visible(nk_handle window, nk_bool *out_visible NK_OUT);

/** Requests that the window manager minimize the window. */
NK_API nk_result NK_CALL nk_window_minimize(nk_handle window);

/** Requests that the window manager maximize the window. */
NK_API nk_result NK_CALL nk_window_maximize(nk_handle window);

/** Requests that the window manager restore a minimized or maximized window. */
NK_API nk_result NK_CALL nk_window_restore(nk_handle window);

/** Requests activation and focus for the window. */
NK_API nk_result NK_CALL nk_window_activate(nk_handle window);

/** Requests entering fullscreen when enabled is non-zero, or leaving it otherwise. */
NK_API nk_result NK_CALL nk_window_set_fullscreen(nk_handle window, nk_bool enabled);

/** Requests that the window manager draw the user's attention to the window. */
NK_API nk_result NK_CALL nk_window_request_attention(nk_handle window);

/* ------------------------------------------------------------------------- */
/* Window styling and constraints                                             */
/* ------------------------------------------------------------------------- */

/** Zero disables the corresponding constraint; maxima must not be below minima. */
NK_API nk_result NK_CALL nk_window_set_size_limits(nk_handle window,
                                                   const nk_window_size_limits *limits);
/** Passing zero for both values disables the aspect-ratio constraint. */
NK_API nk_result NK_CALL nk_window_set_aspect_ratio(nk_handle window, int32_t numerator,
                                                    int32_t denominator);

/** Enables or disables user resizing of the window. */
NK_API nk_result NK_CALL nk_window_set_resizable(nk_handle window, nk_bool enabled);

/** Enables or disables standard native window decorations. */
NK_API nk_result NK_CALL nk_window_set_decorated(nk_handle window, nk_bool enabled);

/** Enables or disables keeping the window above its peers. */
NK_API nk_result NK_CALL nk_window_set_floating(nk_handle window, nk_bool enabled);

/** Sets the window opacity; supported backends accept values from zero to one. */
NK_API nk_result NK_CALL nk_window_set_opacity(nk_handle window, float opacity);

/** Enables or disables passing pointer input through the window. */
NK_API nk_result NK_CALL nk_window_set_mouse_passthrough(nk_handle window, nk_bool enabled);

/** Sets `*out_hovered` to 1 when the pointer is over the window, and 0 otherwise. */
NK_API nk_result NK_CALL nk_window_get_hovered(nk_handle window, nk_bool *out_hovered NK_OUT);

/* ------------------------------------------------------------------------- */
/* Native-window interoperability                                            */
/* ------------------------------------------------------------------------- */

/**
 * Returns a borrowed platform descriptor. Its pointer-sized values are valid
 * only while the NativeKit window is alive and must never be freed by callers.
 * This is an explicit interoperability escape hatch, not a portable resource.
 */
NK_API nk_result NK_CALL nk_window_get_native(nk_handle window,
                                              nk_native_window *out_native NK_OUT);

/**
 * Attaches NativeKit to a caller-owned native window. Destroying the returned
 * handle only detaches NativeKit. Backends return NK_ERROR_UNSUPPORTED until
 * they can guarantee correct event and ownership behavior for the given kind.
 */
NK_API nk_result NK_CALL nk_window_wrap_native(const nk_native_window *native,
                                               nk_handle *out_window NK_OUT);

#ifdef __cplusplus
}
#endif

#endif
