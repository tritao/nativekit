#include "nativekit.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <assert.h>

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    assert((nk_get_capabilities() & (NK_CAP_WINDOW | NK_CAP_WEBVIEW)) ==
           (NK_CAP_WINDOW | NK_CAP_WEBVIEW));
    assert(nk_window_create(NULL, NULL) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_webview_navigate(NK_INVALID_HANDLE, NULL) == NK_ERROR_INVALID_ARGUMENT);

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.flags = NK_WINDOW_HIDDEN | NK_WINDOW_RESIZABLE;
    window_options.width = 640;
    window_options.height = 480;
    window_options.title = "NativeKit integration test";
    nk_handle window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);

    nk_webview_options webview_options = {0};
    webview_options.struct_size = sizeof(webview_options);
    webview_options.flags = NK_WEBVIEW_HIDDEN;
    webview_options.width = 640;
    webview_options.height = 480;
    nk_handle webview = NK_INVALID_HANDLE;
    assert(nk_webview_create(window, &webview_options, &webview) == NK_OK);
    assert(nk_webview_set_html(webview, "<title>NativeKit</title>", NULL) == NK_OK);

    /* Destroying a parent invalidates all of its borrowed child handles. */
    assert(nk_window_destroy(window) == NK_OK);
    assert(nk_webview_destroy(webview) == NK_ERROR_INVALID_HANDLE);
    assert(nk_window_destroy(window) == NK_ERROR_INVALID_HANDLE);

    nk_shutdown();
    return 0;
}
