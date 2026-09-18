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
 * NativeKit GPU owns rendering commands and resources. For GL,
 * nk_surface_make_current() selects the context; explicit APIs use it to
 * acquire the current frame target. Call nk_surface_present() after submitting
 * the frame. Surface operations are UI-thread-only.
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
 * The callback runs on the UI thread. The backend presents the frame after the
 * callback returns. `user_data` is the nullable value supplied to
 * nk_surface_set_frame_callback().
 */
typedef void(NK_CALL *nk_surface_frame_callback)(nk_surface surface, int32_t framebuffer_width,
                                                 int32_t framebuffer_height,
                                                 void *NK_NULLABLE user_data);
typedef nk_surface_frame_callback NK_NULLABLE nk_nullable_surface_frame_callback;

/**
 * Scheduling mode of a graphics surface frame callback.
 */
typedef uint32_t nk_surface_frame_mode;

enum NK_ENUM(nk_surface_frame_mode) {
    /**
     * The backend invokes the frame callback every vsync while one is
     * installed. This is the default and the mode game-loop callers expect.
     */
    NK_SURFACE_FRAME_CONTINUOUS = 0,
    /**
     * The backend invokes the frame callback only while a frame is requested
     * with nk_surface_request_frame(), and stops scheduling work while idle.
     */
    NK_SURFACE_FRAME_ON_DEMAND = 1
};

/**
 * Token identifying one acquired surface frame.
 *
 * A token is returned by nk_surface_acquire_frame() and stays valid until it is
 * passed to nk_surface_present_frame() or nk_surface_cancel_frame(). Tokens are
 * generation-checked: a token whose surface has been destroyed, or whose frame
 * already ended, is rejected with NK_ERROR_INVALID_HANDLE. Zero is never valid.
 */
typedef uint32_t nk_surface_frame NK_HANDLE;

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
 * GL callers make the surface current before requesting this descriptor.
 * Explicit API callers may query stable device, context and depth tokens before
 * preparing a frame; call nk_surface_make_current() before using the color or
 * presentation tokens. For OpenGL and OpenGL ES, `native_target` is the current
 * draw framebuffer name, with zero representing the default framebuffer. For
 * explicit APIs, it is a borrowed backend-native color target token. Tokens
 * are borrowed; device/context tokens last for the surface lifetime, the depth
 * token lasts until a resize, and color/presentation tokens last through the
 * prepared frame. NativeKit callers must not release or retain these tokens.
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
    /** Frame that owns this target, or NK_INVALID_HANDLE outside an acquired frame. */
    nk_surface_frame frame;
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
NK_API nk_result NK_CALL nk_surface_create(nk_handle parent, const nk_surface_options *options,
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
 * Call this after manual rendering when no frame callback is installed. For a
 * callback-driven surface, the backend presents automatically after the
 * callback returns.
 *
 * The backend presents the surface's prepared frame. Explicit APIs submit to
 * their native presentation target; GTK schedules composition of the
 * GtkGLArea framebuffer instead of swapping a caller-owned native surface.
 */
NK_API nk_result NK_CALL nk_surface_present(nk_surface surface);

/**
 * Installs or removes the surface's frame callback. When installed, the
 * backend schedules callbacks for continuous rendering and presents the frame
 * after each callback returns. The callback runs on the UI thread while
 * the framebuffer is current. Do not call nk_surface_present() recursively
 * from it. Pass NULL to detach it; the `user_data` value is not retained after
 * the callback is removed.
 *
 * Call nk_surface_set_frame_mode() with NK_SURFACE_FRAME_ON_DEMAND to render
 * only when the application asks for a frame instead of every vsync.
 */
NK_API nk_result NK_CALL nk_surface_set_frame_callback(
    nk_surface surface, nk_nullable_surface_frame_callback callback NK_RETAINED,
    void *NK_NULLABLE user_data);

/**
 * Selects how the backend schedules an installed frame callback.
 *
 * NK_SURFACE_FRAME_CONTINUOUS is the default and keeps the callback running
 * every vsync. NK_SURFACE_FRAME_ON_DEMAND invokes it only while a frame is
 * requested, so animation and state changes must call
 * nk_surface_request_frame() to keep producing frames. Switching modes never
 * loses a request that is already pending, and switching back to continuous
 * resumes rendering immediately. Call on the platform executor.
 *
 * @return NK_OK, NK_ERROR_INVALID_ARGUMENT for an unknown mode,
 *         NK_ERROR_INVALID_HANDLE for a stale surface, or
 *         NK_ERROR_WRONG_THREAD off the platform executor.
 */
NK_API nk_result NK_CALL nk_surface_set_frame_mode(nk_surface surface, nk_surface_frame_mode mode);

/**
 * Records that the surface needs a frame.
 *
 * Requests coalesce: any number of calls before the next frame produce one
 * frame callback. The backend chooses when that frame happens (vsync,
 * display link, or browser animation frame) and stops scheduling work while no
 * request is pending. A callback that requests another frame from inside itself
 * keeps an animation running without re-arming anything.
 *
 * The call is a no-op for a continuous surface. Without an installed frame
 * callback it only records the request, so the surface draws once a callback is
 * installed. Call on the platform executor; native work that runs on other
 * threads should use nk_dispatch_to_app() first.
 *
 * @return NK_OK, NK_ERROR_INVALID_HANDLE for a stale surface, or
 *         NK_ERROR_WRONG_THREAD off the platform executor.
 */
NK_API nk_result NK_CALL nk_surface_request_frame(nk_surface surface);

/* ------------------------------------------------------------------------- */
/* Frame transactions                                                        */
/* ------------------------------------------------------------------------- */

/**
 * Opens one frame and returns its immutable render target.
 *
 * The transaction separates frame acquisition and presentation from rendering:
 * the platform executor opens the frame, any executor renders against the
 * target, and the platform executor presents or cancels it. `out_target` is
 * valid for the lifetime of the returned token and is not affected by later
 * bounds changes, so a renderer can work against a stable snapshot.
 *
 * Only one frame may be open on a surface at a time; a second acquire returns
 * NK_ERROR_INVALID_REQUEST. Destroying the surface cancels its open frame.
 *
 * @param surface Surface to open a frame on.
 * @param out_frame Receives the frame token on NK_OK.
 * @param out_target Zero-initialized target whose struct_size must cover the
 *                   full nk_surface_frame_target.
 * @return NK_OK, NK_ERROR_INVALID_ARGUMENT for malformed arguments,
 *         NK_ERROR_INVALID_HANDLE for a stale surface, NK_ERROR_INVALID_REQUEST
 *         when a frame is already open or no drawable frame exists, or
 *         NK_ERROR_WRONG_THREAD off the platform executor.
 */
NK_API nk_result NK_CALL nk_surface_acquire_frame(nk_surface surface,
                                                  nk_surface_frame *out_frame NK_OUT,
                                                  nk_surface_frame_target *out_target NK_INOUT);

/**
 * Presents the frame opened by nk_surface_acquire_frame() and closes it.
 *
 * The token is always consumed, including when presentation fails, so the
 * surface accepts a new frame afterwards. Call on the platform executor.
 *
 * @return NK_OK, NK_ERROR_INVALID_ARGUMENT for an invalid token,
 *         NK_ERROR_INVALID_HANDLE for a token that never existed, already
 *         ended, or belonged to a destroyed surface, or the backend result of
 *         the presentation itself.
 */
NK_API nk_result NK_CALL nk_surface_present_frame(nk_surface_frame frame);

/**
 * Closes the frame without presenting it.
 *
 * Use this when rendering failed or produced nothing usable: the token is
 * consumed, the surface accepts a new frame afterwards, and nothing is
 * submitted for display. Backends that hold a prepared native frame release it
 * on the next acquire or when the surface is destroyed. Call on the platform
 * executor.
 *
 * @return NK_OK, NK_ERROR_INVALID_ARGUMENT for an invalid token, or
 *         NK_ERROR_INVALID_HANDLE for a token that is not open.
 */
NK_API nk_result NK_CALL nk_surface_cancel_frame(nk_surface_frame frame);

/* ------------------------------------------------------------------------- */
/* Surface queries                                                           */
/* ------------------------------------------------------------------------- */

/**
 * Returns the current framebuffer size in device pixels. Returns zero
 * dimensions while the surface has no drawable frame, such as while hidden,
 * minimized or fully occluded.
 */
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
