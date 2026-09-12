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

/** Identity of a graphics context and its explicitly shared contexts. */
NK_DECLARE_HANDLE(nk_graphics_device);

/**
 * NativeKit's graphics-surface API.
 *
 * A surface is a child rendering target associated with a NativeKit window or
 * mobile host. Create it with nk_surface_create(), make it current before
 * issuing graphics commands, and call nk_surface_present() when the frame is
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
    NK_GRAPHICS_VULKAN = 3
};

/** Backend-neutral metadata for a sampled graphics image. */
typedef struct nk_graphics_image_info {
    /** Set to sizeof(nk_graphics_image_info) before calling the query API. */
    uint32_t struct_size;
    /** Graphics backend that owns the image. */
    nk_graphics_api api;
    /** Context-sharing device that owns the image. */
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
    uint32_t struct_size;
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
    /** Optional compatible surface whose graphics context should be shared. */
    nk_handle share_surface;
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
typedef void(NK_CALL *nk_surface_frame_callback)(nk_handle surface,
                                                 int32_t framebuffer_width,
                                                 int32_t framebuffer_height,
                                                 void *user_data);

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
 * Callers must make the surface current before requesting this descriptor.
 * For OpenGL and OpenGL ES, `native_target` is the current draw framebuffer
 * name, with zero representing the default framebuffer. Other graphics APIs
 * may use the field for a backend-native target handle in a future backend.
 */
typedef struct nk_surface_frame_target {
    /** Set to sizeof(nk_surface_frame_target) or a larger compatible size. */
    uint32_t struct_size;
    /** Graphics API that owns the target. */
    nk_graphics_api api;
    /** Current framebuffer width in device pixels. */
    int32_t width;
    /** Current framebuffer height in device pixels. */
    int32_t height;
    /** Backend-native target token; opaque to NativeKit callers. */
    uint64_t native_target;
    /** Identity of the graphics context/share group current for this target. */
    nk_graphics_device device;
    /** Reserved for future target metadata; set to zero. */
    uint32_t reserved[3];
} nk_surface_frame_target;

/* ------------------------------------------------------------------------- */
/* Surface lifecycle and rendering                                           */
/* ------------------------------------------------------------------------- */

/**
 * Creates a graphics surface inside a NativeKit window or mobile host.
 *
 * Context configuration is fixed at creation. Desktop backends support
 * OpenGL and OpenGL ES; Android supports OpenGL ES and Vulkan presentation
 * views. A share_surface must remain alive until every surface sharing it has
 * been destroyed. Vulkan views do not support GL context flags or sharing.
 * On NK_OK, writes the new surface handle to `out_surface`.
 */
NK_API nk_result NK_CALL nk_surface_create(nk_handle window,
                                           const nk_surface_options *options,
                                           nk_handle *out_surface NK_OUT);

/** Destroys a graphics surface; its handle becomes invalid. */
NK_API nk_result NK_CALL nk_surface_destroy(nk_handle surface);

/** Shows or hides a surface without destroying it. */
NK_API nk_result NK_CALL nk_surface_show(nk_handle surface, nk_bool visible);

/**
 * Changes a surface's logical position and size within its parent.
 *
 * Width and height must be positive. The backend reports resulting framebuffer
 * dimensions through NK_EVENT_SURFACE_RESIZE or the frame callback.
 */
NK_API nk_result NK_CALL nk_surface_set_bounds(nk_handle surface, int32_t x, int32_t y,
                                               int32_t width, int32_t height);

/** Makes the surface's graphics context and framebuffer current on the UI thread. */
NK_API nk_result NK_CALL nk_surface_make_current(nk_handle surface);

/**
 * Presents drawing performed since the last make-current call.
 *
 * On GTK this schedules composition of the GtkGLArea framebuffer instead of
 * swapping a caller-owned native surface.
 */
NK_API nk_result NK_CALL nk_surface_present(nk_handle surface);

/**
 * Installs or removes the surface's frame callback.
 *
 * The callback runs on the UI thread while the framebuffer is current. Do not
 * call nk_surface_present() recursively from it. Pass NULL to detach it; the
 * `user_data` value is not retained after the callback is removed.
 */
NK_API nk_result NK_CALL nk_surface_set_frame_callback(
    nk_handle surface, nk_surface_frame_callback callback, void *user_data);

/* ------------------------------------------------------------------------- */
/* Surface queries                                                           */
/* ------------------------------------------------------------------------- */

/** Returns the current framebuffer size in device pixels. */
NK_API nk_result NK_CALL nk_surface_get_framebuffer_size(nk_handle surface,
                                                         int32_t *out_width NK_OUT,
                                                         int32_t *out_height NK_OUT);

/** Returns the current backend-native render target for a surface frame. */
NK_API nk_result NK_CALL nk_surface_get_frame_target(
    nk_handle surface, nk_surface_frame_target *out_target NK_OUT);

/** Retains a sampled image reference. Each successful retain requires one release. */
NK_API nk_result NK_CALL nk_graphics_image_retain(nk_graphics_image image);

/** Releases a sampled image reference; the handle becomes invalid after its final release. */
NK_API nk_result NK_CALL nk_graphics_image_release(nk_graphics_image image);

/** Queries backend, owning context/share group, and dimensions for a sampled image. */
NK_API nk_result NK_CALL nk_graphics_image_get_info(
    nk_graphics_image image, nk_graphics_image_info *out_info NK_OUT);

/** Retains a graphics device; its root NativeKit surface cannot be destroyed until release. */
NK_API nk_result NK_CALL nk_graphics_device_retain(nk_graphics_device device);

/** Releases a graphics-device reference acquired by nk_graphics_device_retain(). */
NK_API nk_result NK_CALL nk_graphics_device_release(nk_graphics_device device);

/**
 * Resolves a graphics function for the current surface context.
 *
 * `name` is a non-null UTF-8 function name. On NK_OK, writes a callable
 * function pointer to `out_proc`; the pointer remains valid only while the
 * associated graphics context and loader remain valid.
 */
NK_API nk_result NK_CALL nk_surface_get_proc_address(nk_handle surface, const char *name NK_UTF8,
                                                     nk_graphics_proc *out_proc);

#ifdef __cplusplus
}
#endif

#endif
