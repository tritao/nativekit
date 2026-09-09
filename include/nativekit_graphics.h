#ifndef NATIVEKIT_GRAPHICS_H
#define NATIVEKIT_GRAPHICS_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t nk_graphics_api;
enum {
    NK_GRAPHICS_OPENGL = 1,
    NK_GRAPHICS_OPENGL_ES = 2
};

enum {
    NK_SURFACE_HIDDEN = 1u << 0,
    NK_SURFACE_ALPHA = 1u << 1,
    NK_SURFACE_DEPTH = 1u << 2,
    NK_SURFACE_STENCIL = 1u << 3,
    NK_SURFACE_DEBUG_CONTEXT = 1u << 4,
    NK_SURFACE_FORWARD_COMPATIBLE = 1u << 5
};

typedef struct nk_surface_options {
    uint32_t struct_size;
    uint32_t flags;
    nk_graphics_api api;
    uint32_t major_version;
    uint32_t minor_version;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint64_t reserved[2];
} nk_surface_options;

typedef struct nk_surface_resize_event {
    int32_t width;
    int32_t height;
    int32_t framebuffer_width;
    int32_t framebuffer_height;
} nk_surface_resize_event;

/*
 * Creates a GTK-backed graphics surface inside a NativeKit window. Context
 * configuration is fixed at creation. The initial implementation supports
 * OpenGL and OpenGL ES through GtkGLArea.
 */
NK_API nk_result NK_CALL nk_surface_create(nk_handle window,
                                           const nk_surface_options *options,
                                           nk_handle *out_surface);
NK_API nk_result NK_CALL nk_surface_destroy(nk_handle surface);
NK_API nk_result NK_CALL nk_surface_show(nk_handle surface, uint32_t visible);
NK_API nk_result NK_CALL nk_surface_set_bounds(nk_handle surface, int32_t x, int32_t y,
                                               int32_t width, int32_t height);

/*
 * Makes the surface context and its GTK-managed framebuffer current. This is a
 * UI-thread operation in the GTK backend.
 */
NK_API nk_result NK_CALL nk_surface_make_current(nk_handle surface);

/*
 * Presents drawing performed since the last make-current call. On GTK this
 * schedules composition of the GtkGLArea framebuffer instead of swapping a
 * caller-owned native surface.
 */
NK_API nk_result NK_CALL nk_surface_present(nk_handle surface);

NK_API nk_result NK_CALL nk_surface_get_framebuffer_size(nk_handle surface,
                                                         int32_t *out_width,
                                                         int32_t *out_height);

#ifdef __cplusplus
}
#endif

#endif
