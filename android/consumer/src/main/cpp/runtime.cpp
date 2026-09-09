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
    const bool uri_matches = decoded == NK_OK && output.uri_length == input_length &&
                             std::memcmp(output.uri, input.uri, input_length) == 0;
    const bool mime_matches = output.mime_type_length == 10 &&
                              std::memcmp(output.mime_type, "text/plain", 10) == 0;
    const bool name_matches = output.display_name_length == 4 &&
                              std::memcmp(output.display_name, "test", 4) == 0;
    nk_event_release(&event);
    return decoded != NK_OK ? 5 : !uri_matches ? 6 : !mime_matches ? 7 : !name_matches ? 8 : 0;
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

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativePersistedResourceProbe(JNIEnv *, jclass) {
    nk_resource resource{};
    resource.struct_size = sizeof(resource);
    resource.uri = "content://io.nativekit.consumer.resources/not-persisted";
    uint32_t flags = UINT32_MAX;
    if (nk_resource_get_persisted_access(&resource, &flags) != NK_OK || flags != 0)
        return 1;
    flags = UINT32_MAX;
    if (nk_resource_set_persisted_access(&resource, 0, &flags) != NK_OK || flags != 0)
        return 2;
    if (nk_resource_set_persisted_access(&resource, NK_RESOURCE_PERSISTED, &flags) !=
        NK_ERROR_INVALID_ARGUMENT)
        return 3;
    return 0;
}

namespace {
bool poll_kind(nk_event_kind kind, nk_event &event) {
    event.struct_size = sizeof(event);
    for (int attempt = 0; attempt < 64; ++attempt) {
        if (nk_poll_event(&event) != NK_OK)
            return false;
        if (event.kind == kind)
            return true;
        if (event.kind == NK_EVENT_NONE)
            return false;
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
    return false;
}
} // namespace

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeIncomingShareProbe(JNIEnv *, jclass) {
    nk_event event{};
    if (!poll_kind(NK_EVENT_SHARE_RECEIVED, event))
        return 1;
    const char *text = nullptr;
    const char *subject = nullptr;
    uint32_t text_length = 0;
    uint32_t subject_length = 0;
    if (nk_share_event_text(&event, &text, &text_length) != NK_OK || text_length != 11 ||
        std::memcmp(text, "shared text", text_length) != 0 ||
        nk_share_event_subject(&event, &subject, &subject_length) != NK_OK ||
        subject_length != 14 || std::memcmp(subject, "shared subject", subject_length) != 0) {
        nk_event_release(&event);
        return 2;
    }
    for (uint32_t index = 0; index < 2; ++index) {
        nk_resource_view resource{};
        resource.struct_size = sizeof(resource);
        if (nk_resource_event_item(&event, index, &resource) != NK_OK ||
            !(resource.flags & NK_RESOURCE_READABLE) || resource.mime_type_length != 24 ||
            std::memcmp(resource.mime_type, "application/octet-stream", 24) != 0 ||
            resource.display_name_length != 3 ||
            std::memcmp(resource.display_name, index == 0 ? "one" : "two", 3) != 0) {
            nk_event_release(&event);
            return 3;
        }
    }
    nk_resource_view extra{};
    extra.struct_size = sizeof(extra);
    const bool exactly_two = nk_resource_event_item(&event, 2, &extra) == NK_ERROR_INVALID_ARGUMENT;
    nk_event_release(&event);
    return exactly_two ? 0 : 4;
}

extern "C" JNIEXPORT jint JNICALL
Java_io_nativekit_consumer_MainActivity_nativeIncomingViewProbe(JNIEnv *, jclass) {
    nk_event event{};
    if (!poll_kind(NK_EVENT_RESOURCE_OPENED, event))
        return 1;
    nk_resource_view resource{};
    resource.struct_size = sizeof(resource);
    const char expected[] = "content://io.nativekit.consumer.resources/viewed";
    const auto decoded = nk_resource_event_item(&event, 0, &resource);
    const bool matches = decoded == NK_OK && resource.uri_length == sizeof(expected) - 1 &&
                         std::memcmp(resource.uri, expected, sizeof(expected) - 1) == 0 &&
                         (resource.flags & NK_RESOURCE_READABLE) &&
                         resource.mime_type_length == 24 &&
                         std::memcmp(resource.mime_type, "application/octet-stream", 24) == 0 &&
                         resource.display_name_length == 6 &&
                         std::memcmp(resource.display_name, "viewed", 6) == 0;
    nk_event_release(&event);
    return matches ? 0 : 2;
}
