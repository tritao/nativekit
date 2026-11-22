#include "nativekit.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include <assert.h>

/* Run with an unusable libwebkit2gtk-4.1.so.0 first on the library path: windows still work,
   and WebViews are reported and refused as unsupported. */
int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    const nk_capabilities capabilities = nk_get_capabilities();
    assert((capabilities & NK_CAP_WINDOW) != 0);
    assert((capabilities & NK_CAP_WEBVIEW) == 0);

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 320;
    window_options.height = 240;
    window_options.title = "NativeKit WebView unavailable test";
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);

    nk_webview_options webview_options = {0};
    webview_options.struct_size = sizeof(webview_options);
    webview_options.width = 320;
    webview_options.height = 240;
    nk_webview webview = 1;
    assert(nk_webview_create(window, &webview_options, &webview) == NK_ERROR_UNSUPPORTED);
    assert(webview == NK_INVALID_HANDLE);
    assert(nk_webview_navigate(webview, "about:blank") == NK_ERROR_UNSUPPORTED);

    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
