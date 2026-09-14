#ifndef NATIVEKIT_GRAPHICS_H
#define NATIVEKIT_GRAPHICS_H

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

/** Opaque sampled image shared between NativeKit graphics producers and consumers. */
NK_DECLARE_HANDLE(nk_graphics_image);

/** Identity of a graphics device and surfaces that explicitly share it. */
NK_DECLARE_HANDLE(nk_graphics_device);

/**
 * NativeKit's graphics-surface API.
 *
 * A surface is a child rendering target associated with a NativeKit window or
 * mobile host. NativeKit core owns its platform presentation resources;
 * NativeKit GPU owns rendering commands and resources. Prepare a frame with
 * nk_surface_make_current(), then call nk_surface_present() when the frame is
 * ready. Surface operations are UI-thread-only.
 */

/* ------------------------------------------------------------------------- */
/* Graphics and surface data                                                 */
/* ------------------------------------------------------------------------- */

/** Graphics API requested when creating a surface. */
typedef uint32_t nk_graphics_api;
enum NK_ENUM(nk_graphics_api) {
    /** Desktop or compatible OpenGL. */
    NK_GRAPHICS_OPENGL = 1,
    /** OpenGL ES, typically used on mobile or embedded systems. */
    NK_GRAPHICS_OPENGL_ES = 2,
    /** Vulkan presentation surface. */
    NK_GRAPHICS_VULKAN = 3,
    /** Direct3D 11 presentation surface. */
    NK_GRAPHICS_D3D11 = 4,
    /** Metal presentation surface. */
    NK_GRAPHICS_METAL = 5
};

/** Backend-neutral metadata for a sampled graphics image. */
typedef struct nk_graphics_image_info {
    /** Set to sizeof(nk_graphics_image_info) before calling the query API. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Graphics backend that owns the image. */
    nk_graphics_api api;
    /** Graphics device that owns the image. */
    nk_graphics_device device;
    /** Sampled image width in pixels. */
    int32_t width;
    /** Sampled image height in pixels. */
    int32_t height;
    /** Reserved for future image metadata; set to zero. */
    uint32_t reserved[2];
} nk_graphics_image_info;

/** Optional surface creation and presentation flags. */
typedef uint32_t nk_surface_flags;
enum NK_FLAGS(nk_surface_flags) {
    /** Create the surface hidden until explicitly shown. */
    NK_SURFACE_HIDDEN = 1u << 0,
    /** Request an alpha-capable surface where the backend supports it. */
    NK_SURFACE_ALPHA = 1u << 1,
    /** Request a depth buffer. */
    NK_SURFACE_DEPTH = 1u << 2,
    /** Request a stencil buffer. */
    NK_SURFACE_STENCIL = 1u << 3,
    /** Request additional graphics validation or debug output. */
    NK_SURFACE_DEBUG_CONTEXT = 1u << 4,
    /** Request a forward-compatible OpenGL context. */
    NK_SURFACE_FORWARD_COMPATIBLE = 1u << 5
};

/** Options used to create a NativeKit graphics surface. */
typedef struct nk_surface_options {
    /** Set to sizeof(nk_surface_options) or a larger compatible size. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Bitwise OR of NK_SURFACE_* flags. */
    nk_surface_flags flags;
    /** Graphics API and context family to request. */
    nk_graphics_api api;
    /** Major context/API version, when applicable. */
    uint32_t major_version;
    /** Minor context/API version, when applicable. */
    uint32_t minor_version;
    /** Child-surface x position in the parent window's logical coordinates. */
    int32_t x;
    /** Child-surface y position in the parent window's logical coordinates. */
    int32_t y;
    /** Initial logical width; must be positive. */
    int32_t width;
    /** Initial logical height; must be positive. */
    int32_t height;
    /** Optional compatible surface whose graphics device should be shared. */
    nk_surface share_surface;
    /** Reserved; set to zero. */
    uint32_t reserved;
    /** Reserved for future options; set all elements to zero. */
    uint64_t reserved2[2];
} nk_surface_options;

/** Function-pointer type returned by nk_surface_get_proc_address(). */
typedef void(NK_CALL *nk_graphics_proc)(void);

/**
 * Callback invoked while a surface framebuffer is current and ready to draw.
 *
 * The callback runs on the UI thread. `user_data` is the value supplied to
 * nk_surface_set_frame_callback().
 */
typedef void(NK_CALL *nk_surface_frame_callback)(nk_surface surface, int32_t framebuffer_width,
                                                 int32_t framebuffer_height, void *user_data);
typedef nk_surface_frame_callback NK_NULLABLE nk_nullable_surface_frame_callback;

/** Payload of NK_EVENT_SURFACE_RESIZE. */
typedef struct nk_surface_resize_event {
    /** New logical surface width. */
    int32_t width;
    /** New logical surface height. */
    int32_t height;
    /** New framebuffer width in device pixels. */
    int32_t framebuffer_width;
    /** New framebuffer height in device pixels. */
    int32_t framebuffer_height;
} nk_surface_resize_event;

/**
 * Backend-native render target for the current surface frame.
 *
 * Callers must prepare the frame with nk_surface_make_current() before
 * requesting this descriptor. For OpenGL and OpenGL ES, `native_target` is
 * the current draw framebuffer name, with zero representing the default
 * framebuffer. For explicit APIs, it is a borrowed backend-native color target
 * token. The appended tokens are borrowed from the surface; target and present
 * tokens are valid only for the prepared frame. NativeKit callers must not
 * release or retain these tokens.
 */
typedef struct nk_surface_frame_target {
    /** Set to sizeof(nk_surface_frame_target) or a larger compatible size. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Graphics API that owns the target. */
    nk_graphics_api api;
    /** Current framebuffer width in device pixels. */
    int32_t width;
    /** Current framebuffer height in device pixels. */
    int32_t height;
    /** Backend-native target token; opaque to NativeKit callers. */
    uint64_t native_target;
    /** Identity of the graphics device that owns this target. */
    nk_graphics_device device;
    /** Reserved for compatibility with the original frame-target descriptor; set to zero. */
    uint32_t reserved[3];
    /** Borrowed native device token (ID3D11Device* or id<MTLDevice> where applicable). */
    uint64_t native_device;
    /** Borrowed execution token (ID3D11DeviceContext* or id<MTLCommandQueue> where applicable). */
    uint64_t native_context;
    /** Borrowed depth/stencil target token; zero when no attachment is requested. */
    uint64_t native_depth_stencil_target;
    /** Borrowed presentation token (for example IDXGISwapChain* or CAMetalDrawable*). */
    uint64_t native_present_target;
} nk_surface_frame_target;

/* ------------------------------------------------------------------------- */
/* Surface lifecycle and rendering                                           */
/* ------------------------------------------------------------------------- */

/**
 * Creates a graphics surface inside a NativeKit window or mobile host.
 *
 * Backend configuration is fixed at creation. A share_surface must remain
 * alive until every surface sharing its graphics device has been destroyed.
 * The requested API determines which platform backends are available.
 * On NK_OK, writes the new surface handle to `out_surface`.
 */
NK_API nk_result NK_CALL nk_surface_create(nk_window window, const nk_surface_options *options,
                                           nk_surface *out_surface NK_OUT NK_OWNED);

/** Destroys a graphics surface; its handle becomes invalid. */
NK_API nk_result NK_CALL nk_surface_destroy(nk_surface surface);

/** Shows or hides a surface without destroying it. */
NK_API nk_result NK_CALL nk_surface_show(nk_surface surface, nk_bool visible);

/**
 * Changes a surface's logical position and size within its parent.
 *
 * Width and height must be positive. The backend reports resulting framebuffer
 * dimensions through NK_EVENT_SURFACE_RESIZE or the frame callback.
 */
NK_API nk_result NK_CALL nk_surface_set_bounds(nk_surface surface, int32_t x, int32_t y,
                                               int32_t width, int32_t height);

/**
 * Prepares the surface for rendering on the UI thread. OpenGL backends make
 * their context and framebuffer current. Explicit backends acquire or prepare
 * the frame's presentation targets; they do not expose a thread-current
 * graphics context.
 */
NK_API nk_result NK_CALL nk_surface_make_current(nk_surface surface);

/**
 * Presents drawing performed since the last make-current call.
 *
 * The backend presents the surface's prepared frame. On GTK this schedules
 * composition of the GtkGLArea framebuffer instead of swapping a
 * caller-owned native surface.
 */
NK_API nk_result NK_CALL nk_surface_present(nk_surface surface);

/**
 * Installs or removes the surface's frame callback.
 *
 * The callback runs on the UI thread while the framebuffer is current. Do not
 * call nk_surface_present() recursively from it. Pass NULL to detach it; the
 * `user_data` value is not retained after the callback is removed.
 */
NK_API nk_result NK_CALL nk_surface_set_frame_callback(
    nk_surface surface, nk_nullable_surface_frame_callback callback NK_RETAINED,
    void *NK_NULLABLE user_data);

/* ------------------------------------------------------------------------- */
/* Surface queries                                                           */
/* ------------------------------------------------------------------------- */

/** Returns the current framebuffer size in device pixels. */
NK_API nk_result NK_CALL nk_surface_get_framebuffer_size(nk_surface surface,
                                                         int32_t *out_width NK_OUT,
                                                         int32_t *out_height NK_OUT);

/** Returns the current backend-native render target for a surface frame. */
NK_API nk_result NK_CALL nk_surface_get_frame_target(nk_surface surface,
                                                     nk_surface_frame_target *out_target NK_OUT);

/** Retains a sampled image reference. Each successful retain requires one release. */
NK_API nk_result NK_CALL nk_graphics_image_retain(nk_graphics_image image);

/** Releases a sampled image reference; the handle becomes invalid after its final release. */
NK_API nk_result NK_CALL nk_graphics_image_release(nk_graphics_image image);

/** Queries backend, owning context/share group, and dimensions for a sampled image. */
NK_API nk_result NK_CALL nk_graphics_image_get_info(nk_graphics_image image,
                                                    nk_graphics_image_info *out_info NK_OUT);

/** Retains a graphics device; its root NativeKit surface cannot be destroyed until release. */
NK_API nk_result NK_CALL nk_graphics_device_retain(nk_graphics_device device);

/** Releases a graphics-device reference acquired by nk_graphics_device_retain(). */
NK_API nk_result NK_CALL nk_graphics_device_release(nk_graphics_device device);

/**
 * Resolves a graphics function for the current OpenGL surface context.
 *
 * `name` is a non-null UTF-8 function name. On NK_OK, writes a callable
 * function pointer to `out_proc`; the pointer remains valid only while the
 * associated graphics context and loader remain valid. Explicit backends such
 * as D3D11 and Metal return NK_ERROR_UNSUPPORTED.
 */
NK_API nk_result NK_CALL nk_surface_get_proc_address(nk_surface surface, const char *name NK_UTF8,
                                                     nk_graphics_proc *out_proc);

#ifdef __cplusplus
}
#endif

#endif
