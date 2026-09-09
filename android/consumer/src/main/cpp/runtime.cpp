#include "nativekit.h"
#include "nativekit_webview.h"

#include <jni.h>

extern "C" JNIEXPORT jlong JNICALL
Java_io_nativekit_consumer_MainActivity_nativeProbe(JNIEnv *, jclass, jlong host) {
    nk_webview_options options{};
    options.struct_size = sizeof(options);
    options.width = 320;
    options.height = 240;
    options.initial_url = "data:text/html,<h1>NativeKit source consumer</h1>";
    nk_handle webview = NK_INVALID_HANDLE;
    if (nk_webview_create(static_cast<nk_handle>(host), &options, &webview) != NK_OK)
        return 0;
    return static_cast<jlong>(nk_api_version()) << 32 | webview;
}
