#include "nativekit.h"
#include "nativekit_resource.h"
#include "nativekit_webview.h"

#include <jni.h>

#include <cstring>

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

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeResourceClipboardProbe(JNIEnv *, jclass) {
    nk_resource input{};
    input.struct_size = sizeof(input);
    input.uri = "content://io.nativekit.consumer/test";
    input.mime_type = "text/plain";
    input.display_name = "NativeKit resource";
    if (nk_clipboard_set_resources(&input, 1) != NK_OK)
        return 1;
    nk_request_id request = NK_INVALID_REQUEST_ID;
    if (nk_clipboard_read_resources(&request) != NK_OK)
        return 2;
    nk_event event{};
    event.struct_size = sizeof(event);
    for (int attempt = 0; attempt < 64; ++attempt) {
        if (nk_poll_event(&event) != NK_OK)
            return 3;
        if (event.kind == NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE && event.request_id == request)
            break;
        if (event.kind == NK_EVENT_NONE)
            return 3;
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
    if (event.kind != NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE || event.request_id != request)
        return 4;
    nk_resource_view output{};
    output.struct_size = sizeof(output);
    const auto decoded = nk_resource_event_item(&event, 0, &output);
    const auto input_length = std::strlen(input.uri);
    const bool matches = decoded == NK_OK && output.uri_length == input_length &&
                         std::memcmp(output.uri, input.uri, input_length) == 0;
    nk_event_release(&event);
    return matches ? 0 : decoded != NK_OK ? 5 : 6;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeResourceStreamProbe(JNIEnv *, jclass) {
    nk_resource resource{};
    resource.struct_size = sizeof(resource);
    resource.uri = "content://io.nativekit.consumer.resources/probe";
    nk_handle stream = NK_INVALID_HANDLE;
    const uint32_t mode = NK_RESOURCE_OPEN_READ | NK_RESOURCE_OPEN_WRITE |
                          NK_RESOURCE_OPEN_CREATE | NK_RESOURCE_OPEN_TRUNCATE;
    if (nk_resource_open(&resource, mode, &stream) != NK_OK)
        return 1;
    const char message[] = "NativeKit content stream";
    uint64_t written = 0;
    if (nk_resource_write(stream, message, sizeof(message) - 1, &written) != NK_OK ||
        written != sizeof(message) - 1) {
        nk_resource_close(stream);
        return 2;
    }
    uint64_t position = 0;
    if (nk_resource_seek(stream, 0, NK_SEEK_START, &position) != NK_OK || position != 0) {
        nk_resource_close(stream);
        return 3;
    }
    char result[sizeof(message)]{};
    uint64_t read = 0;
    if (nk_resource_read(stream, result, sizeof(message) - 1, &read) != NK_OK ||
        read != sizeof(message) - 1 || std::memcmp(result, message, read) != 0) {
        nk_resource_close(stream);
        return 4;
    }
    nk_resource_stream_info info{};
    info.struct_size = sizeof(info);
    if (nk_resource_stream_info_get(stream, &info) != NK_OK ||
        !(info.flags & NK_RESOURCE_STREAM_SEEKABLE) ||
        !(info.flags & NK_RESOURCE_STREAM_SIZE_KNOWN) || info.size != sizeof(message) - 1) {
        nk_resource_close(stream);
        return 5;
    }
    if (nk_resource_close(stream) != NK_OK || nk_resource_close(stream) != NK_ERROR_INVALID_HANDLE)
        return 6;
    return 0;
}
