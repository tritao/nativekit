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
    NK_CAP_DRAG_DROP = UINT64_C(1) << 4
};

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

#ifdef __cplusplus
}
#endif

#endif
