#include "nativekit_mobile.h"
#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_notification.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"

#include <jni.h>

#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
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
struct DialogRequest {
    uint32_t kind;
    bool resources;
};

struct ResourceValue {
    uint32_t flags = 0;
    std::string uri;
    std::string mime_type;
    std::string display_name;
};

std::unordered_map<nk_request_id, DialogRequest> file_dialogs;
std::unordered_map<nk_request_id, bool> notifications;

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

jobjectArray resource_strings(JNIEnv *env, const nk_resource *resources, uint32_t count,
                              const char *nk_resource::*member) {
    auto string_class = env->FindClass("java/lang/String");
    if (!string_class)
        return nullptr;
    auto result = env->NewObjectArray(static_cast<jsize>(count), string_class, nullptr);
    env->DeleteLocalRef(string_class);
    for (uint32_t index = 0; result && index < count; ++index) {
        auto value = from_utf8(env, resources[index].*member);
        if (value) {
            env->SetObjectArrayElement(result, static_cast<jsize>(index), value);
            env->DeleteLocalRef(value);
        }
    }
    return result;
}

nk_result validate_resources(const nk_resource *resources, uint32_t count, bool allow_empty) {
    if (count > static_cast<uint32_t>(INT_MAX) || (!count && !allow_empty) ||
        (count && !resources)) {
        nk::core::set_error("resource list is missing or empty");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    for (uint32_t index = 0; index < count; ++index) {
        if (resources[index].struct_size < sizeof(nk_resource) || !resources[index].uri ||
            !*resources[index].uri || !std::strchr(resources[index].uri, ':')) {
            nk::core::set_error("resource descriptor or URI is invalid");
            return NK_ERROR_INVALID_ARGUMENT;
        }
    }
    return NK_OK;
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

std::shared_ptr<AndroidHost> context_host() {
    return hosts.empty() ? nullptr : hosts.begin()->second;
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

std::vector<std::byte> dialog_payload(bool accepted, const std::vector<std::string> &uris) {
    const auto offsets_offset = sizeof(nk_dialog_paths);
    const auto strings_offset = offsets_offset + uris.size() * sizeof(uint32_t);
    std::size_t size = strings_offset;
    for (const auto &uri : uris)
        size += uri.size() + 1;
    std::vector<std::byte> result(size);
    const nk_dialog_paths header{accepted ? 1u : 0u, static_cast<uint32_t>(uris.size()),
                                 static_cast<uint32_t>(offsets_offset),
                                 static_cast<uint32_t>(strings_offset)};
    std::memcpy(result.data(), &header, sizeof(header));
    std::size_t cursor = strings_offset;
    for (std::size_t index = 0; index < uris.size(); ++index) {
        const auto offset = static_cast<uint32_t>(cursor);
        std::memcpy(result.data() + offsets_offset + index * sizeof(offset), &offset,
                    sizeof(offset));
        std::memcpy(result.data() + cursor, uris[index].c_str(), uris[index].size() + 1);
        cursor += uris[index].size() + 1;
    }
    return result;
}

std::vector<std::byte> resource_payload(bool accepted,
                                        const std::vector<ResourceValue> &resources) {
    const auto items_offset = sizeof(nk_resource_list);
    const auto strings_offset = items_offset + resources.size() * sizeof(nk_resource_item);
    std::size_t size = strings_offset;
    for (const auto &resource : resources) {
        size += resource.uri.size() + 1;
        if (!resource.mime_type.empty())
            size += resource.mime_type.size() + 1;
        if (!resource.display_name.empty())
            size += resource.display_name.size() + 1;
    }
    std::vector<std::byte> result(size);
    const nk_resource_list header{accepted ? 1u : 0u,
                                  static_cast<uint32_t>(resources.size()),
                                  static_cast<uint32_t>(items_offset),
                                  static_cast<uint32_t>(strings_offset)};
    std::memcpy(result.data(), &header, sizeof(header));
    std::size_t cursor = strings_offset;
    for (std::size_t index = 0; index < resources.size(); ++index) {
        const auto &resource = resources[index];
        nk_resource_item item{};
        item.flags = resource.flags;
        auto append = [&](const std::string &value, uint32_t &offset) {
            if (value.empty())
                return;
            offset = static_cast<uint32_t>(cursor);
            std::memcpy(result.data() + cursor, value.c_str(), value.size() + 1);
            cursor += value.size() + 1;
        };
        append(resource.uri, item.uri_offset);
        append(resource.mime_type, item.mime_type_offset);
        append(resource.display_name, item.display_name_offset);
        std::memcpy(result.data() + items_offset + index * sizeof(item), &item, sizeof(item));
    }
    return result;
}

nk_result start_file_dialog(uint32_t kind, bool resources, nk_handle parent,
                            const nk_file_dialog_options *options, nk_request_id *out_request) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    if (!options || options->struct_size < sizeof(nk_file_dialog_options) || !out_request ||
        (options->filter_count && !options->filters)) {
        nk::core::set_error("file dialog options or output is missing or invalid");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto host_resource = parent ? host(parent) : context_host();
    if (!host_resource) {
        nk::core::set_error("Android file dialogs require an attached mobile host");
        return parent ? NK_ERROR_INVALID_HANDLE : NK_ERROR_UNSUPPORTED;
    }
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(
        bridge, "startFileDialog",
        "(Landroid/view/ViewGroup;JIILjava/lang/String;Ljava/lang/String;[Ljava/lang/String;)Z");
    auto title = from_utf8(env, options->title);
    auto suggested_name = from_utf8(env, options->suggested_name);
    auto string_class = env->FindClass("java/lang/String");
    auto patterns = string_class ? env->NewObjectArray(static_cast<jsize>(options->filter_count),
                                                       string_class, nullptr)
                                 : nullptr;
    for (uint32_t index = 0; patterns && index < options->filter_count; ++index) {
        auto pattern = from_utf8(env, options->filters[index].patterns);
        env->SetObjectArrayElement(patterns, static_cast<jsize>(index), pattern);
        if (pattern)
            env->DeleteLocalRef(pattern);
    }
    const auto request = nk::core::next_request_id();
    const auto started =
        method && env->CallStaticBooleanMethod(bridge, method, host_resource->view_group,
                                               static_cast<jlong>(request), static_cast<jint>(kind),
                                               static_cast<jint>(options->flags), title,
                                               suggested_name, patterns);
    if (patterns)
        env->DeleteLocalRef(patterns);
    if (string_class)
        env->DeleteLocalRef(string_class);
    if (suggested_name)
        env->DeleteLocalRef(suggested_name);
    if (title)
        env->DeleteLocalRef(title);
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android file dialog launch failed") || !started) {
        nk::core::set_error("Android could not launch the system document picker");
        return NK_ERROR_UNKNOWN;
    }
    file_dialogs.emplace(request, DialogRequest{kind, resources});
    *out_request = request;
    return NK_OK;
}

nk_result copy_string_result(const std::string &value, char *buffer, uint32_t *inout_size) {
    if (!inout_size) {
        nk::core::set_error("string buffer size is null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    if (value.size() >= std::numeric_limits<uint32_t>::max())
        return NK_ERROR_OUT_OF_MEMORY;
    const auto required = static_cast<uint32_t>(value.size() + 1);
    const auto capacity = *inout_size;
    *inout_size = required;
    if (!buffer || capacity < required) {
        nk::core::set_error("string buffer is too small");
        return NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, value.c_str(), required);
    return NK_OK;
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
    file_dialogs.clear();
    notifications.clear();
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
    return NK_CAP_MOBILE_HOST | NK_CAP_WEBVIEW | NK_CAP_FILE_DIALOG | NK_CAP_CLIPBOARD |
           NK_CAP_SHELL | NK_CAP_SYSTEM_APPEARANCE | NK_CAP_NOTIFICATION |
           NK_CAP_RESOURCE_SHARING;
}

nk_result NK_CALL nk_shell_open_url(const char *url) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    if (!url || !*url) {
        nk::core::set_error("URL must not be empty");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto host_resource = context_host();
    if (!host_resource) {
        nk::core::set_error("Android URL opening requires an attached mobile host");
        return NK_ERROR_UNSUPPORTED;
    }
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method =
        env->GetStaticMethodID(bridge, "openUrl", "(Landroid/view/ViewGroup;Ljava/lang/String;)I");
    auto value = from_utf8(env, url);
    const auto result =
        method ? env->CallStaticIntMethod(bridge, method, host_resource->view_group, value)
               : static_cast<jint>(NK_ERROR_UNKNOWN);
    if (value)
        env->DeleteLocalRef(value);
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android URL opening failed"))
        return NK_ERROR_UNKNOWN;
    if (result != NK_OK)
        nk::core::set_error(result == NK_ERROR_INVALID_ARGUMENT
                                ? "URL is invalid"
                                : "no Android activity could open the URL");
    return result;
}

nk_result NK_CALL nk_shell_open_resource(const nk_resource *resource) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    if (const auto valid = validate_resources(resource, resource ? 1u : 0u, false);
        valid != NK_OK)
        return valid;
    auto host_resource = context_host();
    if (!host_resource)
        return NK_ERROR_UNSUPPORTED;
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(
        bridge, "openResource",
        "(Landroid/view/ViewGroup;Ljava/lang/String;Ljava/lang/String;)I");
    auto uri = from_utf8(env, resource->uri);
    auto mime = from_utf8(env, resource->mime_type);
    const auto result = method ? env->CallStaticIntMethod(bridge, method,
                                                          host_resource->view_group, uri, mime)
                               : static_cast<jint>(NK_ERROR_UNKNOWN);
    if (mime)
        env->DeleteLocalRef(mime);
    if (uri)
        env->DeleteLocalRef(uri);
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android resource opening failed"))
        return NK_ERROR_UNKNOWN;
    return result;
}

nk_result NK_CALL nk_share(const nk_share_options *options) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    if (!options || options->struct_size < sizeof(nk_share_options) ||
        (!options->text && options->resource_count == 0)) {
        nk::core::set_error("share options must contain text or resources");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    if (const auto valid =
            validate_resources(options->resources, options->resource_count, true);
        valid != NK_OK)
        return valid;
    auto host_resource = context_host();
    if (!host_resource)
        return NK_ERROR_UNSUPPORTED;
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(
        bridge, "share",
        "(Landroid/view/ViewGroup;Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)I");
    auto title = from_utf8(env, options->title);
    auto text = from_utf8(env, options->text);
    auto uris = resource_strings(env, options->resources, options->resource_count,
                                 &nk_resource::uri);
    auto mimes = resource_strings(env, options->resources, options->resource_count,
                                  &nk_resource::mime_type);
    auto names = resource_strings(env, options->resources, options->resource_count,
                                  &nk_resource::display_name);
    const auto result = method ? env->CallStaticIntMethod(
                                     bridge, method, host_resource->view_group, title, text, uris,
                                     mimes, names)
                               : static_cast<jint>(NK_ERROR_UNKNOWN);
    if (names)
        env->DeleteLocalRef(names);
    if (mimes)
        env->DeleteLocalRef(mimes);
    if (uris)
        env->DeleteLocalRef(uris);
    if (text)
        env->DeleteLocalRef(text);
    if (title)
        env->DeleteLocalRef(title);
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android sharing failed"))
        return NK_ERROR_UNKNOWN;
    return result;
}

nk_result NK_CALL nk_system_directory(nk_system_directory_kind kind, char *buffer,
                                      uint32_t *inout_size) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    auto host_resource = context_host();
    if (!host_resource)
        return NK_ERROR_UNSUPPORTED;
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(bridge, "systemDirectory",
                                         "(Landroid/view/ViewGroup;I)Ljava/lang/String;");
    auto value = method ? static_cast<jstring>(env->CallStaticObjectMethod(
                              bridge, method, host_resource->view_group, static_cast<jint>(kind)))
                        : nullptr;
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android directory lookup failed"))
        return NK_ERROR_UNKNOWN;
    if (!value) {
        nk::core::set_error("system directory is unavailable on Android");
        return NK_ERROR_UNSUPPORTED;
    }
    const auto text = to_utf8(env, value);
    env->DeleteLocalRef(value);
    return copy_string_result(text, buffer, inout_size);
}

nk_result NK_CALL nk_system_locale(char *buffer, uint32_t *inout_size) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    auto host_resource = context_host();
    if (!host_resource)
        return NK_ERROR_UNSUPPORTED;
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(bridge, "systemLocale",
                                         "(Landroid/view/ViewGroup;)Ljava/lang/String;");
    auto value = method ? static_cast<jstring>(env->CallStaticObjectMethod(
                              bridge, method, host_resource->view_group))
                        : nullptr;
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android locale lookup failed") || !value)
        return NK_ERROR_UNKNOWN;
    const auto text = to_utf8(env, value);
    env->DeleteLocalRef(value);
    return copy_string_result(text, buffer, inout_size);
}

nk_result NK_CALL nk_system_get_appearance(nk_system_appearance *appearance) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    if (!appearance || appearance->struct_size < sizeof(nk_system_appearance)) {
        nk::core::set_error("appearance output is missing or too small");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto host_resource = context_host();
    if (!host_resource)
        return NK_ERROR_UNSUPPORTED;
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(bridge, "systemAppearance", "(Landroid/view/ViewGroup;)I");
    const auto value =
        method ? env->CallStaticIntMethod(bridge, method, host_resource->view_group) : 0;
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android appearance lookup failed"))
        return NK_ERROR_UNKNOWN;
    appearance->color_scheme = static_cast<uint32_t>(value & 0xff);
    appearance->high_contrast = (value & 0x100) != 0;
    return NK_OK;
}

nk_result NK_CALL nk_clipboard_set_text(const char *text) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    if (!text) {
        nk::core::set_error("clipboard text must not be null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto host_resource = context_host();
    if (!host_resource) {
        nk::core::set_error("Android clipboard access requires an attached mobile host");
        return NK_ERROR_UNSUPPORTED;
    }
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(bridge, "setClipboardText",
                                         "(Landroid/view/ViewGroup;Ljava/lang/String;)Z");
    auto value = from_utf8(env, text);
    const auto accepted =
        method && env->CallStaticBooleanMethod(bridge, method, host_resource->view_group, value);
    if (value)
        env->DeleteLocalRef(value);
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android clipboard write failed") || !accepted) {
        nk::core::set_error("Android rejected clipboard text");
        return NK_ERROR_UNKNOWN;
    }
    return NK_OK;
}

nk_result NK_CALL nk_clipboard_read_text(nk_request_id *out_request) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    if (!out_request) {
        nk::core::set_error("clipboard request output is null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto host_resource = context_host();
    if (!host_resource) {
        nk::core::set_error("Android clipboard access requires an attached mobile host");
        return NK_ERROR_UNSUPPORTED;
    }
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(bridge, "clipboardText",
                                         "(Landroid/view/ViewGroup;)Ljava/lang/String;");
    auto value = method ? static_cast<jstring>(env->CallStaticObjectMethod(
                              bridge, method, host_resource->view_group))
                        : nullptr;
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android clipboard read failed"))
        return NK_ERROR_UNKNOWN;
    const bool has_value = value != nullptr;
    const auto text = to_utf8(env, value);
    if (value)
        env->DeleteLocalRef(value);
    const auto request = nk::core::next_request_id();
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_CLIPBOARD_TEXT_COMPLETE;
    event.request_id = request;
    if (has_value)
        event.data = bytes(text.c_str());
    const auto queued = nk::core::push_event(std::move(event));
    if (queued != NK_OK)
        return queued;
    *out_request = request;
    return NK_OK;
}

nk_result NK_CALL nk_clipboard_set_resources(const nk_resource *resources,
                                             uint32_t resource_count) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    if (const auto valid = validate_resources(resources, resource_count, false); valid != NK_OK)
        return valid;
    auto host_resource = context_host();
    if (!host_resource)
        return NK_ERROR_UNSUPPORTED;
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(
        bridge, "setClipboardResources",
        "(Landroid/view/ViewGroup;[Ljava/lang/String;[Ljava/lang/String;)Z");
    auto uris = resource_strings(env, resources, resource_count, &nk_resource::uri);
    auto names = resource_strings(env, resources, resource_count, &nk_resource::display_name);
    const auto accepted = method && env->CallStaticBooleanMethod(
                                        bridge, method, host_resource->view_group, uris, names);
    if (names)
        env->DeleteLocalRef(names);
    if (uris)
        env->DeleteLocalRef(uris);
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android resource clipboard write failed") ||
        !accepted)
        return NK_ERROR_UNKNOWN;
    return NK_OK;
}

nk_result NK_CALL nk_clipboard_read_resources(nk_request_id *out_request) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    if (!out_request)
        return NK_ERROR_INVALID_ARGUMENT;
    auto host_resource = context_host();
    if (!host_resource)
        return NK_ERROR_UNSUPPORTED;
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(
        bridge, "clipboardResources", "(Landroid/view/ViewGroup;)[Ljava/lang/String;");
    auto values = method ? static_cast<jobjectArray>(env->CallStaticObjectMethod(
                               bridge, method, host_resource->view_group))
                         : nullptr;
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android resource clipboard read failed"))
        return NK_ERROR_UNKNOWN;
    std::vector<ResourceValue> resources;
    const auto count = values ? env->GetArrayLength(values) : 0;
    resources.reserve(static_cast<std::size_t>(count));
    for (jsize index = 0; index < count; ++index) {
        auto value = static_cast<jstring>(env->GetObjectArrayElement(values, index));
        ResourceValue resource;
        resource.uri = to_utf8(env, value);
        resource.flags = NK_RESOURCE_READABLE;
        resources.push_back(std::move(resource));
        if (value)
            env->DeleteLocalRef(value);
    }
    if (values)
        env->DeleteLocalRef(values);
    const auto request = nk::core::next_request_id();
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE;
    event.request_id = request;
    event.data = resource_payload(false, resources);
    const auto queued = nk::core::push_event(std::move(event));
    if (queued != NK_OK)
        return queued;
    *out_request = request;
    return NK_OK;
}

nk_result NK_CALL nk_dialog_open_file(nk_handle parent, const nk_file_dialog_options *options,
                                      nk_request_id *out_request) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    (void)parent;
    (void)options;
    (void)out_request;
    nk::core::set_error("Android document selections are URIs; use nk_dialog_open_resource");
    return NK_ERROR_UNSUPPORTED;
}

nk_result NK_CALL nk_dialog_save_file(nk_handle parent, const nk_file_dialog_options *options,
                                      nk_request_id *out_request) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    (void)parent;
    (void)options;
    (void)out_request;
    nk::core::set_error("Android document selections are URIs; use nk_dialog_save_resource");
    return NK_ERROR_UNSUPPORTED;
}

nk_result NK_CALL nk_dialog_select_directory(nk_handle parent,
                                             const nk_file_dialog_options *options,
                                             nk_request_id *out_request) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    (void)parent;
    (void)options;
    (void)out_request;
    nk::core::set_error(
        "Android document selections are URIs; use nk_dialog_select_resource_directory");
    return NK_ERROR_UNSUPPORTED;
}

nk_result NK_CALL nk_dialog_open_resource(nk_handle parent,
                                          const nk_file_dialog_options *options,
                                          nk_request_id *out_request) {
    return start_file_dialog(NK_DIALOG_OPEN_FILE, true, parent, options, out_request);
}

nk_result NK_CALL nk_dialog_save_resource(nk_handle parent,
                                          const nk_file_dialog_options *options,
                                          nk_request_id *out_request) {
    return start_file_dialog(NK_DIALOG_SAVE_FILE, true, parent, options, out_request);
}

nk_result NK_CALL nk_dialog_select_resource_directory(
    nk_handle parent, const nk_file_dialog_options *options, nk_request_id *out_request) {
    return start_file_dialog(NK_DIALOG_SELECT_DIRECTORY, true, parent, options, out_request);
}

nk_result NK_CALL nk_dialog_cancel(nk_request_id request) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    const auto found = file_dialogs.find(request);
    if (found == file_dialogs.end()) {
        nk::core::set_error("invalid Android file dialog request");
        return NK_ERROR_INVALID_REQUEST;
    }
    const auto dialog = found->second;
    file_dialogs.erase(found);
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (bridge) {
        auto method = env->GetStaticMethodID(bridge, "cancelFileDialog", "(J)V");
        if (method)
            env->CallStaticVoidMethod(bridge, method, static_cast<jlong>(request));
        env->DeleteLocalRef(bridge);
        clear_java_exception(env, "Android file dialog cancellation failed");
    }
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_DIALOG_COMPLETE;
    event.flags = dialog.kind;
    event.request_id = request;
    event.data = dialog.resources ? resource_payload(false, {}) : dialog_payload(false, {});
    return nk::core::push_event(std::move(event));
}

nk_result NK_CALL nk_notification_show(const nk_notification_options *options,
                                       nk_request_id *out_request) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    if (!options || options->struct_size < sizeof(nk_notification_options) || !out_request ||
        !options->title || !*options->title) {
        nk::core::set_error("invalid notification options");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto host_resource = context_host();
    if (!host_resource) {
        nk::core::set_error("Android notifications require an attached mobile host");
        return NK_ERROR_UNSUPPORTED;
    }
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(
        bridge, "showNotification",
        "(Landroid/view/ViewGroup;JILjava/lang/String;Ljava/lang/String;I)Z");
    auto title = from_utf8(env, options->title);
    auto body = from_utf8(env, options->body);
    const auto request = nk::core::next_request_id();
    notifications.emplace(request, false);
    const auto timeout = options->timeout_ms > static_cast<uint32_t>(INT_MAX)
                             ? INT_MAX
                             : static_cast<jint>(options->timeout_ms);
    const auto started =
        method && env->CallStaticBooleanMethod(
                      bridge, method, host_resource->view_group, static_cast<jlong>(request),
                      static_cast<jint>(options->flags), title, body, timeout);
    if (body)
        env->DeleteLocalRef(body);
    if (title)
        env->DeleteLocalRef(title);
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android notification launch failed") || !started) {
        notifications.erase(request);
        nk::core::set_error("Android could not start notification delivery");
        return NK_ERROR_UNKNOWN;
    }
    *out_request = request;
    return NK_OK;
}

nk_result NK_CALL nk_notification_close(nk_request_id request) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    const auto found = notifications.find(request);
    if (!request || found == notifications.end()) {
        nk::core::set_error("invalid or completed Android notification request");
        return NK_ERROR_INVALID_REQUEST;
    }
    notifications.erase(found);
    if (auto host_resource = context_host()) {
        auto *env = environment();
        auto *bridge = env ? bridge_class(env) : nullptr;
        if (bridge) {
            auto method =
                env->GetStaticMethodID(bridge, "closeNotification", "(Landroid/view/ViewGroup;J)V");
            if (method)
                env->CallStaticVoidMethod(bridge, method, host_resource->view_group,
                                          static_cast<jlong>(request));
            env->DeleteLocalRef(bridge);
            clear_java_exception(env, "Android notification close failed");
        }
    }
    emit_text(NK_EVENT_NOTIFICATION_DISMISSED, NK_INVALID_HANDLE, nullptr, NK_OK, request);
    return NK_OK;
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

JNIEXPORT jint JNICALL Java_io_nativekit_NativeKitHost_nativeOpenUrl(JNIEnv *env, jclass,
                                                                     jstring url) {
    const auto value = to_utf8(env, url);
    return nk_shell_open_url(url ? value.c_str() : nullptr);
}

JNIEXPORT jint JNICALL Java_io_nativekit_NativeKitHost_nativeSetClipboardText(JNIEnv *env, jclass,
                                                                              jstring text) {
    const auto value = to_utf8(env, text);
    return nk_clipboard_set_text(text ? value.c_str() : nullptr);
}

JNIEXPORT jlong JNICALL Java_io_nativekit_NativeKitHost_nativeReadClipboardText(JNIEnv *, jclass) {
    nk_request_id request = NK_INVALID_REQUEST_ID;
    return nk_clipboard_read_text(&request) == NK_OK ? static_cast<jlong>(request) : 0;
}

JNIEXPORT jlong JNICALL Java_io_nativekit_NativeKitHost_nativeStartFileDialog(
    JNIEnv *env, jclass, jlong host_handle, jint kind, jstring title, jstring suggested_name) {
    const auto title_value = to_utf8(env, title);
    const auto name_value = to_utf8(env, suggested_name);
    nk_file_dialog_options options{};
    options.struct_size = sizeof(options);
    options.title = title ? title_value.c_str() : nullptr;
    options.suggested_name = suggested_name ? name_value.c_str() : nullptr;
    nk_request_id request = NK_INVALID_REQUEST_ID;
    nk_result result = NK_ERROR_INVALID_ARGUMENT;
    if (kind == NK_DIALOG_OPEN_FILE)
        result = nk_dialog_open_resource(static_cast<nk_handle>(host_handle), &options, &request);
    else if (kind == NK_DIALOG_SAVE_FILE)
        result = nk_dialog_save_resource(static_cast<nk_handle>(host_handle), &options, &request);
    else if (kind == NK_DIALOG_SELECT_DIRECTORY)
        result = nk_dialog_select_resource_directory(static_cast<nk_handle>(host_handle), &options,
                                                     &request);
    return result == NK_OK ? static_cast<jlong>(request) : 0;
}

JNIEXPORT jint JNICALL Java_io_nativekit_NativeKitHost_nativeCancelDialog(JNIEnv *, jclass,
                                                                          jlong request) {
    return nk_dialog_cancel(static_cast<nk_request_id>(request));
}

JNIEXPORT jlong JNICALL Java_io_nativekit_NativeKitHost_nativeShowNotification(JNIEnv *env, jclass,
                                                                               jstring title,
                                                                               jstring body) {
    const auto title_value = to_utf8(env, title);
    const auto body_value = to_utf8(env, body);
    nk_notification_options options{};
    options.struct_size = sizeof(options);
    options.title = title ? title_value.c_str() : nullptr;
    options.body = body ? body_value.c_str() : nullptr;
    nk_request_id request = NK_INVALID_REQUEST_ID;
    return nk_notification_show(&options, &request) == NK_OK ? static_cast<jlong>(request) : 0;
}

JNIEXPORT jint JNICALL Java_io_nativekit_NativeKitHost_nativeCloseNotification(JNIEnv *, jclass,
                                                                               jlong request) {
    return nk_notification_close(static_cast<nk_request_id>(request));
}

JNIEXPORT jstring JNICALL Java_io_nativekit_NativeKitHost_nativeSystemDirectory(JNIEnv *env, jclass,
                                                                                jint kind) {
    uint32_t size = 0;
    if (nk_system_directory(static_cast<nk_system_directory_kind>(kind), nullptr, &size) !=
            NK_ERROR_BUFFER_TOO_SMALL ||
        size == 0)
        return nullptr;
    std::vector<char> value(size);
    return nk_system_directory(static_cast<nk_system_directory_kind>(kind), value.data(), &size) ==
                   NK_OK
               ? from_utf8(env, value.data())
               : nullptr;
}

JNIEXPORT jstring JNICALL Java_io_nativekit_NativeKitHost_nativeSystemLocale(JNIEnv *env, jclass) {
    uint32_t size = 0;
    if (nk_system_locale(nullptr, &size) != NK_ERROR_BUFFER_TOO_SMALL || size == 0)
        return nullptr;
    std::vector<char> value(size);
    return nk_system_locale(value.data(), &size) == NK_OK ? from_utf8(env, value.data()) : nullptr;
}

JNIEXPORT jint JNICALL Java_io_nativekit_NativeKitHost_nativeSystemAppearance(JNIEnv *, jclass) {
    nk_system_appearance appearance{};
    appearance.struct_size = sizeof(appearance);
    if (nk_system_get_appearance(&appearance) != NK_OK)
        return -1;
    return static_cast<jint>(appearance.color_scheme | (appearance.high_contrast ? 0x100 : 0));
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

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnFileDialog(
    JNIEnv *env, jclass, jlong request, jint kind, jboolean accepted, jobjectArray values,
    jintArray resource_flags) {
    nk::core::callback_boundary([&] {
        const auto request_id = static_cast<nk_request_id>(request);
        const auto found = file_dialogs.find(request_id);
        if (found == file_dialogs.end() || found->second.kind != static_cast<uint32_t>(kind))
            return;
        const auto dialog = found->second;
        file_dialogs.erase(found);
        std::vector<std::string> uris;
        const auto count = values ? env->GetArrayLength(values) : 0;
        uris.reserve(static_cast<std::size_t>(count));
        for (jsize index = 0; index < count; ++index) {
            auto value = static_cast<jstring>(env->GetObjectArrayElement(values, index));
            uris.push_back(to_utf8(env, value));
            if (value)
                env->DeleteLocalRef(value);
        }
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_DIALOG_COMPLETE;
        event.flags = static_cast<uint32_t>(kind);
        event.request_id = request_id;
        if (dialog.resources) {
            std::vector<ResourceValue> resources;
            resources.reserve(uris.size());
            jint *flags = resource_flags ? env->GetIntArrayElements(resource_flags, nullptr)
                                         : nullptr;
            const auto flag_count = resource_flags ? env->GetArrayLength(resource_flags) : 0;
            for (std::size_t index = 0; index < uris.size(); ++index) {
                ResourceValue resource;
                resource.uri = std::move(uris[index]);
                if (flags && index < static_cast<std::size_t>(flag_count))
                    resource.flags = static_cast<uint32_t>(flags[index]);
                resources.push_back(std::move(resource));
            }
            if (flags)
                env->ReleaseIntArrayElements(resource_flags, flags, JNI_ABORT);
            event.data = resource_payload(accepted == JNI_TRUE, resources);
        } else {
            event.data = dialog_payload(accepted == JNI_TRUE, uris);
        }
        nk::core::push_event(std::move(event));
    });
}

JNIEXPORT void JNICALL
Java_io_nativekit_NativeKitBridge_nativeOnNotificationDelivered(JNIEnv *, jclass, jlong request) {
    nk::core::callback_boundary([&] {
        const auto request_id = static_cast<nk_request_id>(request);
        const auto found = notifications.find(request_id);
        if (found == notifications.end())
            return;
        found->second = true;
        emit_text(NK_EVENT_NOTIFICATION_DELIVERED, NK_INVALID_HANDLE, nullptr, NK_OK, request_id);
    });
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnNotificationFailed(
    JNIEnv *env, jclass, jlong request, jstring message) {
    nk::core::callback_boundary([&] {
        const auto request_id = static_cast<nk_request_id>(request);
        if (notifications.erase(request_id) == 0)
            return;
        const auto text = to_utf8(env, message);
        emit_text(NK_EVENT_NOTIFICATION_FAILED, NK_INVALID_HANDLE, text.c_str(), NK_ERROR_UNKNOWN,
                  request_id);
    });
}

JNIEXPORT void JNICALL
Java_io_nativekit_NativeKitBridge_nativeOnNotificationActivated(JNIEnv *, jclass, jlong request) {
    nk::core::callback_boundary([&] {
        const auto request_id = static_cast<nk_request_id>(request);
        if (notifications.find(request_id) == notifications.end())
            return;
        emit_text(NK_EVENT_NOTIFICATION_ACTIVATED, NK_INVALID_HANDLE, nullptr, NK_OK, request_id);
    });
}

JNIEXPORT void JNICALL
Java_io_nativekit_NativeKitBridge_nativeOnNotificationDismissed(JNIEnv *, jclass, jlong request) {
    nk::core::callback_boundary([&] {
        const auto request_id = static_cast<nk_request_id>(request);
        if (notifications.erase(request_id) == 0)
            return;
        emit_text(NK_EVENT_NOTIFICATION_DISMISSED, NK_INVALID_HANDLE, nullptr, NK_OK, request_id);
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
