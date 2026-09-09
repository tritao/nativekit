#ifndef NATIVEKIT_WEBVIEW_H
#define NATIVEKIT_WEBVIEW_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    NK_WEBVIEW_DEVTOOLS = 1u << 0,
    NK_WEBVIEW_HIDDEN = 1u << 1
};

typedef struct nk_webview_options {
    uint32_t struct_size;
    uint32_t flags;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    const char *initial_url;
    uint64_t reserved[2];
} nk_webview_options;

/*
 * Creates a native child WebView inside `parent`. Bounds are logical pixels;
 * width and height must be positive. `initial_url` is nullable UTF-8.
 */
NK_API nk_result NK_CALL nk_webview_create(
    nk_handle parent,
    const nk_webview_options *options,
    nk_handle *out_webview);
NK_API nk_result NK_CALL nk_webview_destroy(nk_handle webview);

/* All WebView operations are UI-thread-only and copy string input. */
NK_API nk_result NK_CALL nk_webview_show(nk_handle webview, uint32_t visible);
NK_API nk_result NK_CALL nk_webview_set_bounds(
    nk_handle webview, int32_t x, int32_t y, int32_t width, int32_t height);
NK_API nk_result NK_CALL nk_webview_navigate(nk_handle webview, const char *url);
NK_API nk_result NK_CALL nk_webview_set_html(
    nk_handle webview, const char *html, const char *base_url);

/*
 * Starts JavaScript evaluation. Completion is reported as
 * NK_EVENT_WEBVIEW_EVAL_COMPLETE with the returned request ID. Event data is
 * the UTF-8 string representation of the result, or an error message when the
 * event result is not NK_OK.
 */
NK_API nk_result NK_CALL nk_webview_eval(
    nk_handle webview, const char *script, nk_request_id *out_request);

#ifdef __cplusplus
}
#endif

#endif
