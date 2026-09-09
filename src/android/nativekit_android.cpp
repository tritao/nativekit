#include "nativekit_mobile.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"

#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

JavaVM *java_vm = nullptr;

struct AndroidHost final : nk::core::Resource {
    jobject view_group = nullptr;
    nk_mobile_lifecycle_state lifecycle = NK_MOBILE_LIFECYCLE_ACTIVE;
};

struct AndroidWebView final : nk::core::Resource {
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle host = NK_INVALID_HANDLE;
    jobject view = nullptr;
    bool navigation_policy = false;
};

struct NavigationDecision {
    nk_handle webview;
    std::string url;
};

std::unordered_map<nk_handle, std::shared_ptr<AndroidHost>> hosts;
std::unordered_map<nk_handle, std::shared_ptr<AndroidWebView>> webviews;
std::unordered_map<nk_request_id, nk_handle> evaluations;
std::unordered_map<nk_request_id, NavigationDecision> navigation_decisions;

std::vector<std::byte> bytes(const char *value) {
    if (!value)
        return {};
    const auto *first = reinterpret_cast<const std::byte *>(value);
    return {first, first + std::char_traits<char>::length(value)};
}

template <typename T> std::vector<std::byte> bytes_of(const T &value) {
    const auto *first = reinterpret_cast<const std::byte *>(&value);
    return {first, first + sizeof(value)};
}

std::string to_utf8(JNIEnv *env, jstring value) {
    if (!value)
        return {};
    const auto length = env->GetStringLength(value);
    const auto *characters = env->GetStringChars(value, nullptr);
    if (!characters)
        return {};
    std::string result;
    result.reserve(static_cast<std::size_t>(length));
    for (jsize index = 0; index < length; ++index) {
        std::uint32_t codepoint = characters[index];
        if (codepoint >= 0xd800 && codepoint <= 0xdbff && index + 1 < length) {
            const std::uint32_t low = characters[index + 1];
            if (low >= 0xdc00 && low <= 0xdfff) {
                codepoint = UINT32_C(0x10000) + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                ++index;
            }
        }
        if (codepoint <= 0x7f) {
            result.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            result.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            result.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            result.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }
    env->ReleaseStringChars(value, characters);
    return result;
}

jstring from_utf8(JNIEnv *env, const char *value) {
    if (!value)
        return nullptr;
    const auto length = std::char_traits<char>::length(value);
    auto array = env->NewByteArray(static_cast<jsize>(length));
    if (!array)
        return nullptr;
    env->SetByteArrayRegion(array, 0, static_cast<jsize>(length),
                            reinterpret_cast<const jbyte *>(value));
    auto string_class = env->FindClass("java/lang/String");
    auto constructor = string_class
                           ? env->GetMethodID(string_class, "<init>", "([BLjava/lang/String;)V")
                           : nullptr;
    auto encoding = env->NewStringUTF("UTF-8");
    auto result =
        constructor
            ? static_cast<jstring>(env->NewObject(string_class, constructor, array, encoding))
            : nullptr;
    if (encoding)
        env->DeleteLocalRef(encoding);
    if (string_class)
        env->DeleteLocalRef(string_class);
    env->DeleteLocalRef(array);
    return result;
}

JNIEnv *environment() {
    if (!java_vm)
        return nullptr;
    JNIEnv *env = nullptr;
    if (java_vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6) != JNI_OK)
        return nullptr;
    return env;
}

bool clear_java_exception(JNIEnv *env, const char *operation) {
    if (!env->ExceptionCheck())
        return false;
    env->ExceptionClear();
    nk::core::set_error(operation);
    return true;
}

jclass bridge_class(JNIEnv *env) {
    auto *bridge = env->FindClass("io/nativekit/NativeKitBridge");
    if (clear_java_exception(env, "Android NativeKitBridge class is unavailable"))
        return nullptr;
    return bridge;
}

std::shared_ptr<AndroidHost> host(nk_handle handle) {
    return std::dynamic_pointer_cast<AndroidHost>(
        nk::core::handles().get(handle, nk::core::ResourceType::mobile_host));
}

std::shared_ptr<AndroidWebView> webview(nk_handle handle) {
    return std::dynamic_pointer_cast<AndroidWebView>(
        nk::core::handles().get(handle, nk::core::ResourceType::webview));
}

nk_result require_thread() {
    return nk::core::require_ui_thread();
}

nk_result java_void_webview(const std::shared_ptr<AndroidWebView> &resource, const char *name,
                            const char *signature, jvalue *arguments) {
    auto *env = environment();
    if (!env) {
        nk::core::set_error("Android JNI environment is unavailable on the UI thread");
        return NK_ERROR_WRONG_THREAD;
    }
    auto *bridge = bridge_class(env);
    if (!bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(bridge, name, signature);
    if (!method || clear_java_exception(env, "Android NativeKitBridge method is unavailable")) {
        env->DeleteLocalRef(bridge);
        return NK_ERROR_UNKNOWN;
    }
    env->CallStaticVoidMethodA(bridge, method, arguments);
    env->DeleteLocalRef(bridge);
    if (clear_java_exception(env, "Android WebView operation failed"))
        return NK_ERROR_UNKNOWN;
    (void)resource;
    return NK_OK;
}

void emit_text(nk_event_kind kind, nk_handle source, const char *text, nk_result result = NK_OK,
               nk_request_id request = NK_INVALID_REQUEST_ID, uint32_t flags = 0) {
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = source;
    event.result = result;
    event.request_id = request;
    event.flags = flags;
    event.data = bytes(text);
    nk::core::push_event(std::move(event));
}

void cancel_webview_requests(nk_handle source) {
    for (auto item = evaluations.begin(); item != evaluations.end();) {
        if (item->second != source) {
            ++item;
            continue;
        }
        emit_text(NK_EVENT_WEBVIEW_EVAL_COMPLETE, source, nullptr, NK_ERROR_INVALID_REQUEST,
                  item->first);
        item = evaluations.erase(item);
    }
    for (auto item = navigation_decisions.begin(); item != navigation_decisions.end();) {
        if (item->second.webview == source)
            item = navigation_decisions.erase(item);
        else
            ++item;
    }
}

nk_result destroy_webview(nk_handle handle) {
    const auto found = webviews.find(handle);
    if (found == webviews.end()) {
        nk::core::set_error("invalid Android WebView handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    auto *env = environment();
    if (env && found->second->view) {
        jvalue arguments[1]{};
        arguments[0].l = found->second->view;
        java_void_webview(found->second, "destroy", "(Landroid/webkit/WebView;)V", arguments);
        env->DeleteGlobalRef(found->second->view);
        found->second->view = nullptr;
    }
    cancel_webview_requests(handle);
    webviews.erase(found);
    nk::core::handles().erase(handle, nk::core::ResourceType::webview);
    return NK_OK;
}

bool abandon_webview(nk_handle handle) {
    const auto found = webviews.find(handle);
    if (found == webviews.end())
        return false;
    if (auto *env = environment(); env && found->second->view)
        env->DeleteGlobalRef(found->second->view);
    found->second->view = nullptr;
    cancel_webview_requests(handle);
    webviews.erase(found);
    nk::core::handles().erase(handle, nk::core::ResourceType::webview);
    return true;
}

} // namespace

namespace nk::backend {

void pump_events() noexcept {}

void shutdown() noexcept {
    while (!webviews.empty())
        destroy_webview(webviews.begin()->first);
    auto *env = environment();
    if (env) {
        for (auto &[handle, resource] : hosts) {
            (void)handle;
            if (resource->view_group)
                env->DeleteGlobalRef(resource->view_group);
        }
    }
    hosts.clear();
    evaluations.clear();
    navigation_decisions.clear();
}

nk_result mobile_host_attach(const nk_mobile_host_options &options, nk_handle &out_host) {
    if (options.kind != NK_MOBILE_HOST_ANDROID_VIEW_GROUP || !options.platform_context ||
        !options.native_view) {
        nk::core::set_error("Android host requires a JNIEnv and ViewGroup");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto *env = reinterpret_cast<JNIEnv *>(options.platform_context);
    auto view = reinterpret_cast<jobject>(options.native_view);
    jclass group_class = env->FindClass("android/view/ViewGroup");
    if (!group_class || clear_java_exception(env, "android.view.ViewGroup is unavailable") ||
        !env->IsInstanceOf(view, group_class)) {
        if (group_class)
            env->DeleteLocalRef(group_class);
        nk::core::set_error("native_view is not an android.view.ViewGroup");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    env->DeleteLocalRef(group_class);
    if (env->GetJavaVM(&java_vm) != JNI_OK) {
        nk::core::set_error("could not retain the Android Java VM");
        return NK_ERROR_UNKNOWN;
    }
    auto resource = std::make_shared<AndroidHost>();
    resource->view_group = env->NewGlobalRef(view);
    if (!resource->view_group) {
        nk::core::set_error("could not retain the Android ViewGroup");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    const auto handle = nk::core::handles().insert(nk::core::ResourceType::mobile_host, resource);
    if (!handle) {
        env->DeleteGlobalRef(resource->view_group);
        resource->view_group = nullptr;
        nk::core::set_error("could not allocate a mobile host handle");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    hosts.emplace(handle, resource);
    out_host = handle;
    return NK_OK;
}

nk_result mobile_host_destroy(nk_handle handle) {
    const auto thread = require_thread();
    if (thread != NK_OK)
        return thread;
    const auto found = hosts.find(handle);
    if (found == hosts.end()) {
        nk::core::set_error("invalid Android mobile host handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    std::vector<nk_handle> children;
    for (const auto &[child, resource] : webviews)
        if (resource->host == handle)
            children.push_back(child);
    for (const auto child : children)
        destroy_webview(child);
    if (auto *env = environment(); env && found->second->view_group)
        env->DeleteGlobalRef(found->second->view_group);
    found->second->view_group = nullptr;
    hosts.erase(found);
    nk::core::handles().erase(handle, nk::core::ResourceType::mobile_host);
    return NK_OK;
}

nk_result mobile_host_set_lifecycle(nk_handle handle, nk_mobile_lifecycle_state state) {
    const auto thread = require_thread();
    if (thread != NK_OK)
        return thread;
    auto resource = host(handle);
    if (!resource) {
        nk::core::set_error("invalid Android mobile host handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (state < NK_MOBILE_LIFECYCLE_ACTIVE || state > NK_MOBILE_LIFECYCLE_BACKGROUND) {
        nk::core::set_error("invalid mobile lifecycle state");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    resource->lifecycle = state;
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(bridge, "setLifecycle", "(Landroid/view/ViewGroup;I)V");
    if (!method || clear_java_exception(env, "Android lifecycle bridge is unavailable")) {
        env->DeleteLocalRef(bridge);
        return NK_ERROR_UNKNOWN;
    }
    env->CallStaticVoidMethod(bridge, method, resource->view_group, static_cast<jint>(state));
    env->DeleteLocalRef(bridge);
    return clear_java_exception(env, "Android lifecycle update failed") ? NK_ERROR_UNKNOWN : NK_OK;
}

} // namespace nk::backend

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_MOBILE_HOST | NK_CAP_WEBVIEW;
}

nk_result NK_CALL nk_webview_create(nk_handle parent, const nk_webview_options *options,
                                    nk_handle *out_webview) {
    return nk::core::result_boundary(
        "unexpected error while creating an Android WebView", [&]() -> nk_result {
            const auto thread = require_thread();
            if (thread != NK_OK)
                return thread;
            auto parent_resource = host(parent);
            if (!parent_resource) {
                nk::core::set_error("Android WebView parent is not a mobile host");
                return NK_ERROR_INVALID_HANDLE;
            }
            if (!options || options->struct_size < sizeof(nk_webview_options) || !out_webview ||
                options->width <= 0 || options->height <= 0) {
                nk::core::set_error("invalid Android WebView options");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            *out_webview = NK_INVALID_HANDLE;
            auto resource = std::make_shared<AndroidWebView>();
            resource->host = parent;
            resource->navigation_policy = (options->flags & NK_WEBVIEW_NAVIGATION_POLICY) != 0;
            const auto handle =
                nk::core::handles().insert(nk::core::ResourceType::webview, resource);
            if (!handle)
                return NK_ERROR_OUT_OF_MEMORY;
            resource->handle = handle;

            auto *env = environment();
            auto *bridge = env ? bridge_class(env) : nullptr;
            if (!env || !bridge) {
                nk::core::handles().erase(handle, nk::core::ResourceType::webview);
                return NK_ERROR_UNKNOWN;
            }
            auto method = env->GetStaticMethodID(
                bridge, "create",
                "(Landroid/view/ViewGroup;JIIIIILjava/lang/String;)Landroid/webkit/WebView;");
            auto url = from_utf8(env, options->initial_url);
            jobject view =
                method
                    ? env->CallStaticObjectMethod(
                          bridge, method, parent_resource->view_group, static_cast<jlong>(handle),
                          static_cast<jint>(options->flags), static_cast<jint>(options->x),
                          static_cast<jint>(options->y), static_cast<jint>(options->width),
                          static_cast<jint>(options->height), url)
                    : nullptr;
            if (url)
                env->DeleteLocalRef(url);
            env->DeleteLocalRef(bridge);
            if (!method || clear_java_exception(env, "Android WebView creation failed") || !view) {
                nk::core::handles().erase(handle, nk::core::ResourceType::webview);
                return NK_ERROR_UNKNOWN;
            }
            resource->view = env->NewGlobalRef(view);
            if (!resource->view) {
                jvalue arguments[1]{};
                arguments[0].l = view;
                java_void_webview(resource, "destroy", "(Landroid/webkit/WebView;)V", arguments);
                env->DeleteLocalRef(view);
                nk::core::handles().erase(handle, nk::core::ResourceType::webview);
                return NK_ERROR_OUT_OF_MEMORY;
            }
            env->DeleteLocalRef(view);
            webviews.emplace(handle, resource);
            *out_webview = handle;
            nk::core::QueuedEvent ready;
            ready.kind = NK_EVENT_WEBVIEW_READY;
            ready.source = handle;
            nk::core::push_event(std::move(ready));
            return NK_OK;
        });
}

nk_result NK_CALL nk_webview_destroy(nk_handle handle) {
    const auto thread = require_thread();
    return thread == NK_OK ? destroy_webview(handle) : thread;
}

nk_result NK_CALL nk_webview_show(nk_handle handle, uint32_t visible) {
    const auto thread = require_thread();
    auto resource = webview(handle);
    if (thread != NK_OK)
        return thread;
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    jvalue arguments[2]{};
    arguments[0].l = resource->view;
    arguments[1].z = visible ? JNI_TRUE : JNI_FALSE;
    return java_void_webview(resource, "show", "(Landroid/webkit/WebView;Z)V", arguments);
}

nk_result NK_CALL nk_webview_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                        int32_t height) {
    const auto thread = require_thread();
    auto resource = webview(handle);
    if (thread != NK_OK)
        return thread;
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    if (width <= 0 || height <= 0)
        return NK_ERROR_INVALID_ARGUMENT;
    jvalue arguments[5]{};
    arguments[0].l = resource->view;
    arguments[1].i = x;
    arguments[2].i = y;
    arguments[3].i = width;
    arguments[4].i = height;
    return java_void_webview(resource, "setBounds", "(Landroid/webkit/WebView;IIII)V", arguments);
}

nk_result NK_CALL nk_webview_navigate(nk_handle handle, const char *url) {
    const auto thread = require_thread();
    auto resource = webview(handle);
    if (thread != NK_OK)
        return thread;
    if (!resource || !url)
        return !resource ? NK_ERROR_INVALID_HANDLE : NK_ERROR_INVALID_ARGUMENT;
    auto *env = environment();
    auto value = from_utf8(env, url);
    jvalue arguments[3]{};
    arguments[0].l = resource->view;
    arguments[1].l = value;
    arguments[2].z = JNI_FALSE;
    const auto result = java_void_webview(
        resource, "navigate", "(Landroid/webkit/WebView;Ljava/lang/String;Z)V", arguments);
    env->DeleteLocalRef(value);
    return result;
}

nk_result NK_CALL nk_webview_set_html(nk_handle handle, const char *html, const char *base_url) {
    const auto thread = require_thread();
    auto resource = webview(handle);
    if (thread != NK_OK)
        return thread;
    if (!resource || !html)
        return !resource ? NK_ERROR_INVALID_HANDLE : NK_ERROR_INVALID_ARGUMENT;
    auto *env = environment();
    auto html_value = from_utf8(env, html);
    auto base_value = from_utf8(env, base_url);
    jvalue arguments[3]{};
    arguments[0].l = resource->view;
    arguments[1].l = html_value;
    arguments[2].l = base_value;
    const auto result = java_void_webview(
        resource, "setHtml", "(Landroid/webkit/WebView;Ljava/lang/String;Ljava/lang/String;)V",
        arguments);
    env->DeleteLocalRef(html_value);
    if (base_value)
        env->DeleteLocalRef(base_value);
    return result;
}

nk_result NK_CALL nk_webview_eval(nk_handle handle, const char *script,
                                  nk_request_id *out_request) {
    const auto thread = require_thread();
    auto resource = webview(handle);
    if (thread != NK_OK)
        return thread;
    if (!resource || !script || !out_request)
        return !resource ? NK_ERROR_INVALID_HANDLE : NK_ERROR_INVALID_ARGUMENT;
    const auto request = nk::core::next_request_id();
    auto *env = environment();
    auto value = from_utf8(env, script);
    jvalue arguments[3]{};
    arguments[0].l = resource->view;
    arguments[1].j = static_cast<jlong>(request);
    arguments[2].l = value;
    evaluations.emplace(request, handle);
    const auto result = java_void_webview(
        resource, "evaluate", "(Landroid/webkit/WebView;JLjava/lang/String;)V", arguments);
    env->DeleteLocalRef(value);
    if (result != NK_OK) {
        evaluations.erase(request);
        return result;
    }
    *out_request = request;
    return NK_OK;
}

nk_result NK_CALL nk_webview_navigation_decide(nk_request_id request, uint32_t allow) {
    const auto thread = require_thread();
    if (thread != NK_OK)
        return thread;
    const auto found = navigation_decisions.find(request);
    if (found == navigation_decisions.end())
        return NK_ERROR_INVALID_REQUEST;
    const auto decision = std::move(found->second);
    navigation_decisions.erase(found);
    if (!allow)
        return NK_OK;
    auto resource = webview(decision.webview);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    auto *env = environment();
    auto value = from_utf8(env, decision.url.c_str());
    jvalue arguments[3]{};
    arguments[0].l = resource->view;
    arguments[1].l = value;
    arguments[2].z = JNI_TRUE;
    const auto result = java_void_webview(
        resource, "navigate", "(Landroid/webkit/WebView;Ljava/lang/String;Z)V", arguments);
    env->DeleteLocalRef(value);
    return result;
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitHost_nativeInitialize(JNIEnv *env, jclass) {
    nk_init_options options{};
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    const auto result = nk_init(&options);
    if (result != NK_OK && result != NK_ERROR_ALREADY_INITIALIZED) {
        auto exception = env->FindClass("java/lang/IllegalStateException");
        env->ThrowNew(exception, nk_last_error());
        env->DeleteLocalRef(exception);
    }
}

JNIEXPORT jlong JNICALL Java_io_nativekit_NativeKitHost_nativeAttach(JNIEnv *env, jclass,
                                                                     jobject container) {
    nk_mobile_host_options options{};
    options.struct_size = sizeof(options);
    options.kind = NK_MOBILE_HOST_ANDROID_VIEW_GROUP;
    options.platform_context = reinterpret_cast<uintptr_t>(env);
    options.native_view = reinterpret_cast<uintptr_t>(container);
    nk_handle result = NK_INVALID_HANDLE;
    if (nk_mobile_host_attach(&options, &result) != NK_OK)
        return 0;
    return static_cast<jlong>(result);
}

JNIEXPORT jlong JNICALL Java_io_nativekit_NativeKitHost_nativeCreateWebView(JNIEnv *env, jclass,
                                                                            jlong host_handle,
                                                                            jint width, jint height,
                                                                            jstring initial_url) {
    const auto url = to_utf8(env, initial_url);
    nk_webview_options options{};
    options.struct_size = sizeof(options);
    options.width = width;
    options.height = height;
    options.initial_url = initial_url ? url.c_str() : nullptr;
    nk_handle result = NK_INVALID_HANDLE;
    const auto status = nk_webview_create(static_cast<nk_handle>(host_handle), &options, &result);
    return status == NK_OK ? static_cast<jlong>(result) : 0;
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitHost_nativeSetLifecycle(JNIEnv *, jclass,
                                                                          jlong handle,
                                                                          jint state) {
    nk_mobile_host_set_lifecycle(static_cast<nk_handle>(handle),
                                 static_cast<nk_mobile_lifecycle_state>(state));
}

JNIEXPORT jobject JNICALL Java_io_nativekit_NativeKitHost_nativePollEvent(JNIEnv *env, jclass) {
    jobject result = nullptr;
    nk::core::callback_boundary([&] {
        nk_event event{};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK || event.kind == NK_EVENT_NONE)
            return;

        auto event_class = env->FindClass("io/nativekit/NativeKitEvent");
        auto constructor =
            event_class ? env->GetMethodID(event_class, "<init>", "(IJIJI[B)V") : nullptr;
        jbyteArray data = nullptr;
        if (event.data && event.data_size) {
            data = env->NewByteArray(static_cast<jsize>(event.data_size));
            if (data)
                env->SetByteArrayRegion(data, 0, static_cast<jsize>(event.data_size),
                                        static_cast<const jbyte *>(event.data));
        }
        if (constructor) {
            result = env->NewObject(
                event_class, constructor, static_cast<jint>(event.kind),
                static_cast<jlong>(event.source), static_cast<jint>(event.flags),
                static_cast<jlong>(event.request_id), static_cast<jint>(event.result), data);
        }
        if (data)
            env->DeleteLocalRef(data);
        if (event_class)
            env->DeleteLocalRef(event_class);
        nk_event_release(&event);
    });
    return result;
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitHost_nativeDestroy(JNIEnv *, jclass,
                                                                     jlong handle) {
    nk_mobile_host_destroy(static_cast<nk_handle>(handle));
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnMessage(JNIEnv *env, jclass,
                                                                         jlong handle,
                                                                         jstring json) {
    nk::core::callback_boundary([&] {
        if (!webview(static_cast<nk_handle>(handle)))
            return;
        const auto value = to_utf8(env, json);
        emit_text(NK_EVENT_WEBVIEW_MESSAGE, static_cast<nk_handle>(handle), value.c_str(),
                  json ? NK_OK : NK_ERROR_UNKNOWN);
    });
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnNavigated(JNIEnv *env, jclass,
                                                                           jlong handle,
                                                                           jstring url,
                                                                           jstring title) {
    nk::core::callback_boundary([&] {
        if (!webview(static_cast<nk_handle>(handle)))
            return;
        const auto url_value = to_utf8(env, url);
        emit_text(NK_EVENT_WEBVIEW_NAVIGATED, static_cast<nk_handle>(handle), url_value.c_str());
        const auto title_value = to_utf8(env, title);
        emit_text(NK_EVENT_WEBVIEW_TITLE_CHANGED, static_cast<nk_handle>(handle),
                  title_value.c_str());
    });
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnEvaluation(
    JNIEnv *env, jclass, jlong handle, jlong request, jstring result, jstring error) {
    nk::core::callback_boundary([&] {
        const auto found = evaluations.find(static_cast<nk_request_id>(request));
        if (found == evaluations.end() || found->second != static_cast<nk_handle>(handle))
            return;
        evaluations.erase(found);
        jstring text = error ? error : result;
        const auto value = to_utf8(env, text);
        emit_text(NK_EVENT_WEBVIEW_EVAL_COMPLETE, static_cast<nk_handle>(handle), value.c_str(),
                  error ? NK_ERROR_UNKNOWN : NK_OK, static_cast<nk_request_id>(request));
    });
}

JNIEXPORT jboolean JNICALL Java_io_nativekit_NativeKitBridge_nativeOnNavigationRequest(
    JNIEnv *env, jclass, jlong handle, jstring url) {
    bool retained = false;
    nk::core::callback_boundary([&] {
        auto resource = webview(static_cast<nk_handle>(handle));
        if (!resource || !resource->navigation_policy)
            return;
        const auto value = to_utf8(env, url);
        const auto request = nk::core::next_request_id();
        navigation_decisions.emplace(request,
                                     NavigationDecision{static_cast<nk_handle>(handle), value});
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WEBVIEW_NAVIGATION_REQUEST;
        event.source = static_cast<nk_handle>(handle);
        event.request_id = request;
        event.data = bytes(value.c_str());
        if (nk::core::push_event(std::move(event)) != NK_OK)
            navigation_decisions.erase(request);
        else
            retained = true;
    });
    return retained ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnNavigationFailed(
    JNIEnv *env, jclass, jlong handle, jint category, jstring message) {
    nk::core::callback_boundary([&] {
        if (!webview(static_cast<nk_handle>(handle)))
            return;
        const auto stable_category =
            category >= NK_NAVIGATION_ERROR_OTHER && category <= NK_NAVIGATION_ERROR_CANCELLED
                ? static_cast<uint32_t>(category)
                : static_cast<uint32_t>(NK_NAVIGATION_ERROR_OTHER);
        const auto value = to_utf8(env, message);
        emit_text(NK_EVENT_WEBVIEW_NAVIGATION_FAILED, static_cast<nk_handle>(handle), value.c_str(),
                  NK_ERROR_UNKNOWN, NK_INVALID_REQUEST_ID, stable_category);
    });
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnRenderProcessGone(
    JNIEnv *, jclass, jlong handle, jboolean crashed) {
    nk::core::callback_boundary([&] {
        const auto source = static_cast<nk_handle>(handle);
        if (!abandon_webview(source))
            return;
        emit_text(NK_EVENT_WEBVIEW_PROCESS_TERMINATED, source, nullptr, NK_ERROR_UNKNOWN,
                  NK_INVALID_REQUEST_ID, crashed ? 1u : 0u);
    });
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnGeometry(
    JNIEnv *, jclass, jlong handle, jint width, jint height, jfloat scale, jint inset_left,
    jint inset_top, jint inset_right, jint inset_bottom, jint keyboard_bottom) {
    nk::core::callback_boundary([&] {
        if (!host(static_cast<nk_handle>(handle)))
            return;
        const nk_mobile_host_geometry geometry{sizeof(nk_mobile_host_geometry),
                                               width,
                                               height,
                                               scale,
                                               inset_left,
                                               inset_top,
                                               inset_right,
                                               inset_bottom,
                                               keyboard_bottom,
                                               {0, 0}};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_MOBILE_HOST_GEOMETRY_CHANGED;
        event.source = static_cast<nk_handle>(handle);
        event.data = bytes_of(geometry);
        nk::core::push_event(std::move(event));
    });
}

} // extern "C"
