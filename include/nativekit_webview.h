#ifndef NATIVEKIT_WEBVIEW_H
#define NATIVEKIT_WEBVIEW_H

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
/* WebView flags and navigation errors                                       */
/* ------------------------------------------------------------------------- */

enum {
    /** Enable the platform's WebView developer tools or inspector when supported. */
    NK_WEBVIEW_DEVTOOLS = 1u << 0,
    /** Create the WebView hidden until nk_webview_show() enables it. */
    NK_WEBVIEW_HIDDEN = 1u << 1,
    /** Pause navigations for decisions delivered through NK_EVENT_WEBVIEW_NAVIGATION_REQUEST. */
    NK_WEBVIEW_NAVIGATION_POLICY = 1u << 2
};

/* Values stored in nk_event.flags for NK_EVENT_WEBVIEW_NAVIGATION_FAILED. */
enum {
    /** No more specific navigation failure category is available. */
    NK_NAVIGATION_ERROR_OTHER = 0,
    /** The request was malformed or rejected before transport. */
    NK_NAVIGATION_ERROR_REQUEST = 1,
    /** Authentication was required or rejected. */
    NK_NAVIGATION_ERROR_AUTH = 2,
    /** The request was blocked by a security policy. */
    NK_NAVIGATION_ERROR_SECURITY = 3,
    /** The requested resource was not found. */
    NK_NAVIGATION_ERROR_NOT_FOUND = 4,
    /** The connection failed. */
    NK_NAVIGATION_ERROR_CONNECTION = 5,
    /** The navigation was cancelled. */
    NK_NAVIGATION_ERROR_CANCELLED = 6
};

/* ------------------------------------------------------------------------- */
/* WebView options and messaging                                             */
/* ------------------------------------------------------------------------- */

/** Options used to create a native child WebView. */
typedef struct nk_webview_options {
    /** Set to sizeof(nk_webview_options) before creating the WebView. */
    uint32_t struct_size;
    /** Bitwise OR of NK_WEBVIEW_* flags. */
    uint32_t flags;
    /** Child position in the parent's logical coordinates. */
    int32_t x;
    /** Child position in the parent's logical coordinates. */
    int32_t y;
    /** Initial logical width; must be positive. */
    int32_t width;
    /** Initial logical height; must be positive. */
    int32_t height;
    /** Optional UTF-8 URL loaded after creation. */
    const char *initial_url NK_NULLABLE_UTF8;
    /** Reserved for future WebView options; set all elements to zero. */
    uint64_t reserved[2];
} nk_webview_options;

/*
 * Pages may send a value to NativeKit with:
 *
 *   window.webkit.messageHandlers.nativekit.postMessage(value)
 *
 * The value is serialized as JSON and delivered as UTF-8 in an
 * NK_EVENT_WEBVIEW_MESSAGE event. Strings therefore include JSON quotes.
 * Values unsupported by JSON are rejected by the page bridge or produce an
 * event with a failing result. This bridge name is stable across backends.
 */

/* ------------------------------------------------------------------------- */
/* WebView lifecycle and navigation                                          */
/* ------------------------------------------------------------------------- */

/*
 * Creates a native child WebView inside `parent`. Bounds are logical pixels;
 * width and height must be positive. `initial_url` is nullable UTF-8. Creation
 * may finish asynchronously; NK_EVENT_WEBVIEW_READY reports when the native
 * controller can display content. Operations issued before readiness are
 * retained in call order by asynchronous backends.
 */
NK_API nk_result NK_CALL nk_webview_create(nk_handle parent, const nk_webview_options *options,
                                           nk_handle *out_webview NK_OUT);
/** Destroys a WebView and cancels its pending evaluations and navigation decisions. */
NK_API nk_result NK_CALL nk_webview_destroy(nk_handle webview);

/** Shows or hides a WebView without destroying it. */
NK_API nk_result NK_CALL nk_webview_show(nk_handle webview, nk_bool visible);
/** Changes a WebView's logical position and size within its parent. */
NK_API nk_result NK_CALL nk_webview_set_bounds(nk_handle webview, int32_t x, int32_t y,
                                               int32_t width, int32_t height);
/** Starts loading a UTF-8 URL in the WebView. */
NK_API nk_result NK_CALL nk_webview_navigate(nk_handle webview, const char *url NK_UTF8);
/** Replaces the document with UTF-8 HTML, using an optional base URL. */
NK_API nk_result NK_CALL nk_webview_set_html(nk_handle webview, const char *html NK_UTF8,
                                             const char *base_url NK_NULLABLE_UTF8);
/** Reports whether the WebView currently has a back-history entry. */
NK_API nk_result NK_CALL nk_webview_can_go_back(nk_handle webview,
                                                nk_bool *out_can_go_back NK_OUT);
/** Reports whether the WebView currently has a forward-history entry. */
NK_API nk_result NK_CALL nk_webview_can_go_forward(nk_handle webview,
                                                   nk_bool *out_can_go_forward NK_OUT);
/** Submits a request to navigate to the previous history entry. */
NK_API nk_result NK_CALL nk_webview_go_back(nk_handle webview);
/** Submits a request to navigate to the next history entry. */
NK_API nk_result NK_CALL nk_webview_go_forward(nk_handle webview);
/** Reloads the current WebView document. */
NK_API nk_result NK_CALL nk_webview_reload(nk_handle webview);
/** Stops the current WebView load. */
NK_API nk_result NK_CALL nk_webview_stop(nk_handle webview);

/* ------------------------------------------------------------------------- */
/* WebView scripting and navigation policy                                   */
/* ------------------------------------------------------------------------- */

/*
 * Starts JavaScript evaluation. Completion is reported as
 * NK_EVENT_WEBVIEW_EVAL_COMPLETE with the returned request ID. Event data is
 * UTF-8 JSON for the result, or usually an error message when the event result
 * is not NK_OK. Cancellation events have empty data. Top-level `undefined`,
 * functions, and symbols, plus BigInt values and cyclic objects, are not
 * JSON-serializable and complete with an error. Normal JSON.stringify rules
 * apply to unsupported values nested in arrays or objects.
 * Destroying the WebView before completion emits exactly one final event with
 * NK_ERROR_INVALID_REQUEST, allowing consumers to settle pending futures.
 */
NK_API nk_result NK_CALL nk_webview_eval(nk_handle webview, const char *script NK_UTF8,
                                         nk_request_id *out_request NK_OUT);

/*
 * Resolves an NK_EVENT_WEBVIEW_NAVIGATION_REQUEST. Navigation-policy events
 * are emitted only for WebViews created with NK_WEBVIEW_NAVIGATION_POLICY.
 * Event data is the proposed URL. Pending requests are cancelled when their
 * WebView is destroyed; each request may be resolved exactly once.
 */
NK_API nk_result NK_CALL nk_webview_navigation_decide(nk_request_id request, nk_bool allow);

#ifdef __cplusplus
}
#endif

#endif
