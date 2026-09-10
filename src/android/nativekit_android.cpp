#include "nativekit_mobile.h"
#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_graphics.h"
#include "nativekit_gamepad.h"
#include "nativekit_input.h"
#include "nativekit_joystick.h"
#include "nativekit_notification.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/gamepad_events.hpp"
#include "core/runtime.hpp"
#include "android/nativekit_android_internal.hpp"

#include <jni.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <dlfcn.h>

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <climits>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
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

struct AndroidSurface final : nk::core::Resource {
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle host = NK_INVALID_HANDLE;
    jobject view = nullptr;
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLConfig config = nullptr;
    EGLContext context = EGL_NO_CONTEXT;
    EGLSurface surface = EGL_NO_SURFACE;
    ANativeWindow *window = nullptr;
    nk_graphics_api api = NK_GRAPHICS_OPENGL_ES;
    uint32_t major_version = 2;
    uint32_t minor_version = 0;
    uint32_t context_flags = 0;
    std::shared_ptr<AndroidSurface> shared_surface;
    uint32_t share_dependents = 0;
    bool destroying = false;
    std::array<nk_input_action, NK_KEY_LAST + 1> keys{};
    std::array<nk_input_action, NK_POINTER_BUTTON_LAST + 1> pointer_buttons{};
    double pointer_x = 0;
    double pointer_y = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t framebuffer_width = 0;
    int32_t framebuffer_height = 0;
};

struct AndroidJoystick final : nk::core::Resource {
    nk_handle handle = NK_INVALID_HANDLE;
    int32_t device_id = 0;
    std::string name;
    std::string guid;
    std::array<float, NK_GAMEPAD_AXIS_COUNT> axes{};
    std::array<uint8_t, NK_GAMEPAD_BUTTON_COUNT> buttons{};
};

struct NavigationDecision {
    nk_handle webview;
    std::string url;
};

std::unordered_map<nk_handle, std::shared_ptr<AndroidHost>> hosts;
std::unordered_map<nk_handle, std::shared_ptr<AndroidWebView>> webviews;
std::unordered_map<nk_handle, std::shared_ptr<AndroidSurface>> surfaces;
std::unordered_map<int32_t, std::shared_ptr<AndroidJoystick>> joysticks;
EGLDisplay egl_display = EGL_NO_DISPLAY;
std::unordered_map<nk_request_id, nk_handle> evaluations;
std::unordered_map<nk_request_id, NavigationDecision> navigation_decisions;
struct DialogRequest {
    uint32_t operation;
    bool resources;
};

enum class AndroidPickerMode : jint { open = 1, save = 2, directory = 3 };
enum class AndroidDialogCommand : jint { open = 101, save = 102, directory = 103 };

struct ResourceValue {
    uint32_t flags = 0;
    std::string uri;
    std::string mime_type;
    std::string display_name;
};

struct AndroidResourceStream final : nk::core::Resource {
    ~AndroidResourceStream() override {
        if (fd >= 0)
            ::close(fd);
    }

    std::mutex mutex;
    int fd = -1;
    uint32_t flags = 0;
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

std::string controller_guid(const std::string &descriptor) {
    auto hash = [&](uint64_t seed) {
        uint64_t value = seed;
        for (const unsigned char byte : descriptor) {
            value ^= byte;
            value *= UINT64_C(1099511628211);
        }
        return value;
    };
    const uint64_t first = hash(UINT64_C(1469598103934665603));
    const uint64_t second = hash(UINT64_C(1099511628211));
    char guid[33]{};
    std::snprintf(guid, sizeof(guid), "%016llx%016llx",
                  static_cast<unsigned long long>(first),
                  static_cast<unsigned long long>(second));
    return guid;
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

std::shared_ptr<AndroidSurface> surface(nk_handle handle) {
    return std::dynamic_pointer_cast<AndroidSurface>(
        nk::core::handles().get(handle, nk::core::ResourceType::surface));
}

std::shared_ptr<AndroidJoystick> joystick(nk_handle handle) {
    return std::dynamic_pointer_cast<AndroidJoystick>(
        nk::core::handles().get(handle, nk::core::ResourceType::joystick));
}

std::shared_ptr<AndroidJoystick> joystick_device(int32_t device) {
    const auto found = joysticks.find(device);
    return found == joysticks.end() ? nullptr : found->second;
}

std::shared_ptr<AndroidResourceStream> resource_stream(nk_handle handle) {
    return std::dynamic_pointer_cast<AndroidResourceStream>(
        nk::core::handles().get(handle, nk::core::ResourceType::resource_stream));
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

nk_result java_void_surface(const std::shared_ptr<AndroidSurface> &resource, const char *name,
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
    if (method)
        env->CallStaticVoidMethodA(bridge, method, arguments);
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android graphics surface operation failed"))
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

std::vector<std::byte> received_share_payload(const std::string &text,
                                              const std::string &subject,
                                              const std::vector<ResourceValue> &resources) {
    auto packed_resources = resource_payload(false, resources);
    const auto prefix = sizeof(nk_received_share);
    nk_resource_list list{};
    std::memcpy(&list, packed_resources.data(), sizeof(list));
    list.items_offset += prefix;
    list.strings_offset += prefix;
    std::memcpy(packed_resources.data(), &list, sizeof(list));
    for (uint32_t index = 0; index < list.item_count; ++index) {
        nk_resource_item item{};
        const auto offset = sizeof(nk_resource_list) + index * sizeof(item);
        std::memcpy(&item, packed_resources.data() + offset, sizeof(item));
        item.uri_offset += prefix;
        if (item.mime_type_offset)
            item.mime_type_offset += prefix;
        if (item.display_name_offset)
            item.display_name_offset += prefix;
        std::memcpy(packed_resources.data() + offset, &item, sizeof(item));
    }
    std::vector<std::byte> result(prefix + packed_resources.size() +
                                  (text.empty() ? 0 : text.size() + 1) +
                                  (subject.empty() ? 0 : subject.size() + 1));
    nk_received_share share{};
    share.resources_offset = prefix;
    std::memcpy(result.data() + prefix, packed_resources.data(), packed_resources.size());
    auto cursor = prefix + packed_resources.size();
    if (!text.empty()) {
        share.text_offset = static_cast<uint32_t>(cursor);
        std::memcpy(result.data() + cursor, text.c_str(), text.size() + 1);
        cursor += text.size() + 1;
    }
    if (!subject.empty()) {
        share.subject_offset = static_cast<uint32_t>(cursor);
        std::memcpy(result.data() + cursor, subject.c_str(), subject.size() + 1);
    }
    std::memcpy(result.data(), &share, sizeof(share));
    return result;
}

std::vector<std::byte> resource_drop_payload(float x, float y, const std::string &text,
                                             const std::vector<ResourceValue> &resources) {
    auto packed_resources = resource_payload(false, resources);
    const auto prefix = sizeof(nk_resource_drop);
    nk_resource_list list{};
    std::memcpy(&list, packed_resources.data(), sizeof(list));
    list.items_offset += prefix;
    list.strings_offset += prefix;
    std::memcpy(packed_resources.data(), &list, sizeof(list));
    for (uint32_t index = 0; index < list.item_count; ++index) {
        nk_resource_item item{};
        const auto offset = sizeof(nk_resource_list) + index * sizeof(item);
        std::memcpy(&item, packed_resources.data() + offset, sizeof(item));
        item.uri_offset += prefix;
        if (item.mime_type_offset)
            item.mime_type_offset += prefix;
        if (item.display_name_offset)
            item.display_name_offset += prefix;
        std::memcpy(packed_resources.data() + offset, &item, sizeof(item));
    }
    std::vector<std::byte> result(prefix + packed_resources.size() +
                                  (text.empty() ? 0 : text.size() + 1));
    nk_resource_drop drop{};
    drop.resources_offset = prefix;
    drop.x = x;
    drop.y = y;
    std::memcpy(result.data() + prefix, packed_resources.data(), packed_resources.size());
    if (!text.empty()) {
        drop.text_offset = static_cast<uint32_t>(prefix + packed_resources.size());
        std::memcpy(result.data() + drop.text_offset, text.c_str(), text.size() + 1);
    }
    std::memcpy(result.data(), &drop, sizeof(drop));
    return result;
}

nk_result start_file_dialog(uint32_t operation, bool resources, nk_handle parent,
                            const nk_file_dialog_options *options, nk_request_id *out_request) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    if (!options || options->struct_size < sizeof(nk_file_dialog_options) || !out_request ||
        (options->filter_count && !options->filters)) {
        nk::core::set_error("file dialog options or output is missing or invalid");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    AndroidPickerMode mode;
    switch (operation) {
    case NK_DIALOG_OPEN_FILE:
    case NK_DIALOG_OPEN_RESOURCE:
        mode = AndroidPickerMode::open;
        break;
    case NK_DIALOG_SAVE_FILE:
    case NK_DIALOG_SAVE_RESOURCE:
        mode = AndroidPickerMode::save;
        break;
    case NK_DIALOG_SELECT_DIRECTORY:
    case NK_DIALOG_SELECT_RESOURCE_DIRECTORY:
        mode = AndroidPickerMode::directory;
        break;
    default:
        nk::core::set_error("invalid Android dialog operation");
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
                                               static_cast<jlong>(request), static_cast<jint>(mode),
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
    file_dialogs.emplace(request, DialogRequest{operation, resources});
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

void release_surface_window(AndroidSurface &resource) {
    const bool was_ready = resource.window != nullptr;
    if (resource.surface != EGL_NO_SURFACE) {
        if (eglGetCurrentContext() == resource.context)
            eglMakeCurrent(resource.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(resource.display, resource.surface);
        resource.surface = EGL_NO_SURFACE;
    }
    if (resource.window) {
        ANativeWindow_release(resource.window);
        resource.window = nullptr;
    }
    resource.framebuffer_width = 0;
    resource.framebuffer_height = 0;
    if (was_ready && !resource.destroying) {
        nk::core::QueuedEvent lost;
        lost.kind = NK_EVENT_SURFACE_LOST;
        lost.source = resource.handle;
        nk::core::push_event(std::move(lost));
    }
}

nk_result destroy_surface(nk_handle handle) {
    const auto found = surfaces.find(handle);
    if (found == surfaces.end()) {
        nk::core::set_error("invalid Android graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    auto resource = found->second;
    if (resource->share_dependents) {
        nk::core::set_error("graphics surface is still shared by another surface");
        return NK_ERROR_INVALID_REQUEST;
    }
    resource->destroying = true;
    if (auto *env = environment(); env && resource->view) {
        jvalue arguments[1]{};
        arguments[0].l = resource->view;
        java_void_surface(resource, "destroySurface", "(Landroid/view/SurfaceView;)V", arguments);
        env->DeleteGlobalRef(resource->view);
        resource->view = nullptr;
    }
    release_surface_window(*resource);
    if (resource->context != EGL_NO_CONTEXT) {
        eglDestroyContext(resource->display, resource->context);
        resource->context = EGL_NO_CONTEXT;
    }
    resource->display = EGL_NO_DISPLAY;
    if (resource->shared_surface)
        --resource->shared_surface->share_dependents;
    surfaces.erase(found);
    nk::core::handles().erase(handle, nk::core::ResourceType::surface);
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

nk_result mobile_host_set_drop_enabled(nk_handle handle, bool enabled);

nk_result android_vulkan_window(nk_handle handle, ANativeWindow **out_window,
                                bool require_ready) {
    if (!out_window) {
        nk::core::set_error("Android native-window output is required");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    *out_window = nullptr;
    auto resource = surface(handle);
    if (!resource || resource->api != NK_GRAPHICS_VULKAN) {
        nk::core::set_error("handle is not an Android Vulkan surface");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (require_ready && !resource->window) {
        nk::core::set_error("Android Vulkan surface is not ready");
        return NK_ERROR_INVALID_REQUEST;
    }
    *out_window = resource->window;
    return NK_OK;
}

bool android_standard_gamepad(nk_handle handle) { return joystick(handle) != nullptr; }

nk_result android_gamepad_state(nk_handle handle, nk_gamepad_state *out_state) {
    auto resource = joystick(handle);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    if (!out_state || out_state->struct_size < sizeof(*out_state))
        return NK_ERROR_INVALID_ARGUMENT;
    const auto struct_size = out_state->struct_size;
    *out_state = {};
    out_state->struct_size = struct_size;
    std::copy(resource->axes.begin(), resource->axes.end(), out_state->axes);
    std::copy(resource->buttons.begin(), resource->buttons.end(), out_state->buttons);
    return NK_OK;
}

void pump_events() noexcept {}

void shutdown() noexcept {
    while (!surfaces.empty()) {
        const auto leaf = std::find_if(surfaces.begin(), surfaces.end(),
                                       [](const auto &item) {
                                           return item.second->share_dependents == 0;
                                       });
        if (leaf == surfaces.end())
            break;
        destroy_surface(leaf->first);
    }
    while (!webviews.empty())
        destroy_webview(webviews.begin()->first);
    for (const auto &[device, resource] : joysticks) {
        nk::core::gamepad_events::disconnect(resource->handle);
        nk::core::handles().erase(resource->handle, nk::core::ResourceType::joystick);
    }
    joysticks.clear();
    if (egl_display != EGL_NO_DISPLAY) {
        eglTerminate(egl_display);
        egl_display = EGL_NO_DISPLAY;
    }
    auto *env = environment();
    if (env) {
        for (auto &[handle, resource] : hosts) {
            mobile_host_set_drop_enabled(handle, false);
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
    for (;;) {
        const auto leaf = std::find_if(surfaces.begin(), surfaces.end(),
                                       [handle](const auto &item) {
                                           return item.second->host == handle &&
                                                  item.second->share_dependents == 0;
                                       });
        if (leaf == surfaces.end())
            break;
        destroy_surface(leaf->first);
    }
    for (const auto &[child, resource] : webviews)
        if (resource->host == handle)
            children.push_back(child);
    for (const auto child : children)
        destroy_webview(child);
    mobile_host_set_drop_enabled(handle, false);
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

nk_result mobile_host_dispatch_event(nk_handle handle, const nk_mobile_host_event &event) {
    auto resource = host(handle);
    if (!resource) {
        nk::core::set_error("invalid Android mobile host handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (event.kind != NK_MOBILE_HOST_EVENT_ANDROID_INTENT || !event.platform_context ||
        !event.native_event) {
        nk::core::set_error("Android host event requires a JNIEnv and Intent");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto *env = reinterpret_cast<JNIEnv *>(event.platform_context);
    auto intent = reinterpret_cast<jobject>(event.native_event);
    auto *bridge = bridge_class(env);
    if (!bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(
        bridge, "dispatchIntent", "(Landroid/view/ViewGroup;JLandroid/content/Intent;)Z");
    const auto handled = method && env->CallStaticBooleanMethod(
                                       bridge, method, resource->view_group,
                                       static_cast<jlong>(handle), intent);
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android intent dispatch failed"))
        return NK_ERROR_UNKNOWN;
    return handled ? NK_OK : NK_ERROR_UNSUPPORTED;
}

nk_result mobile_host_set_drop_enabled(nk_handle handle, bool enabled) {
    auto resource = host(handle);
    if (!resource) {
        nk::core::set_error("invalid Android mobile host handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(bridge, "setDropEnabled",
                                         "(Landroid/view/ViewGroup;JZ)V");
    if (method)
        env->CallStaticVoidMethod(bridge, method, resource->view_group,
                                  static_cast<jlong>(handle), enabled ? JNI_TRUE : JNI_FALSE);
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android drop configuration failed"))
        return NK_ERROR_UNKNOWN;
    return NK_OK;
}

} // namespace nk::backend

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_MOBILE_HOST | NK_CAP_WEBVIEW | NK_CAP_FILE_DIALOG | NK_CAP_CLIPBOARD |
           NK_CAP_DRAG_DROP |
           NK_CAP_SHELL | NK_CAP_SYSTEM_APPEARANCE | NK_CAP_NOTIFICATION |
           NK_CAP_RESOURCE_SHARING | NK_CAP_RESOURCE_IO | NK_CAP_OPENGL_ES_SURFACE |
           NK_CAP_VULKAN_SURFACE | NK_CAP_INPUT | NK_CAP_JOYSTICK;
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
        "(Landroid/view/ViewGroup;[Ljava/lang/String;[Ljava/lang/String;[Ljava/lang/String;)Z");
    auto uris = resource_strings(env, resources, resource_count, &nk_resource::uri);
    auto mimes = resource_strings(env, resources, resource_count, &nk_resource::mime_type);
    auto names = resource_strings(env, resources, resource_count, &nk_resource::display_name);
    const auto accepted = method && env->CallStaticBooleanMethod(
                                        bridge, method, host_resource->view_group, uris, mimes,
                                        names);
    if (names)
        env->DeleteLocalRef(names);
    if (uris)
        env->DeleteLocalRef(uris);
    if (mimes)
        env->DeleteLocalRef(mimes);
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
    const auto value_count = values ? env->GetArrayLength(values) : 0;
    const auto count = value_count / 3;
    resources.reserve(static_cast<std::size_t>(count));
    for (jsize index = 0; index < count; ++index) {
        auto value = static_cast<jstring>(env->GetObjectArrayElement(values, index * 3));
        auto mime = static_cast<jstring>(env->GetObjectArrayElement(values, index * 3 + 1));
        auto name = static_cast<jstring>(env->GetObjectArrayElement(values, index * 3 + 2));
        ResourceValue resource;
        resource.uri = to_utf8(env, value);
        resource.mime_type = to_utf8(env, mime);
        resource.display_name = to_utf8(env, name);
        resource.flags = NK_RESOURCE_READABLE;
        resources.push_back(std::move(resource));
        if (name)
            env->DeleteLocalRef(name);
        if (mime)
            env->DeleteLocalRef(mime);
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

nk_result NK_CALL nk_resource_open(const nk_resource *resource, uint32_t flags,
                                   nk_handle *out_stream) {
    return nk::core::result_boundary("unexpected error while opening Android resource",
                                     [&]() -> nk_result {
        if (const auto thread = require_thread(); thread != NK_OK)
            return thread;
        if (!resource || resource->struct_size < sizeof(nk_resource) || !resource->uri ||
            !*resource->uri || !out_stream ||
            (flags & (NK_RESOURCE_OPEN_READ | NK_RESOURCE_OPEN_WRITE)) == 0 ||
            (flags & ~(NK_RESOURCE_OPEN_READ | NK_RESOURCE_OPEN_WRITE | NK_RESOURCE_OPEN_CREATE |
                       NK_RESOURCE_OPEN_TRUNCATE)) != 0 ||
            ((flags & (NK_RESOURCE_OPEN_CREATE | NK_RESOURCE_OPEN_TRUNCATE)) != 0 &&
             (flags & NK_RESOURCE_OPEN_WRITE) == 0)) {
            nk::core::set_error("resource open arguments are invalid");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        auto host_resource = context_host();
        if (!host_resource) {
            nk::core::set_error("Android resource opening requires an attached mobile host");
            return NK_ERROR_UNSUPPORTED;
        }
        auto *env = environment();
        auto *bridge = env ? bridge_class(env) : nullptr;
        if (!env || !bridge)
            return NK_ERROR_UNKNOWN;
        auto method = env->GetStaticMethodID(
            bridge, "openResourceFd", "(Landroid/view/ViewGroup;Ljava/lang/String;I)I");
        auto uri = from_utf8(env, resource->uri);
        const auto fd = method ? env->CallStaticIntMethod(bridge, method,
                                                          host_resource->view_group, uri,
                                                          static_cast<jint>(flags))
                               : -1;
        if (uri)
            env->DeleteLocalRef(uri);
        env->DeleteLocalRef(bridge);
        if (!method || clear_java_exception(env, "Android resource provider open failed") ||
            fd < 0) {
            nk::core::set_error("Android could not open the resource URI");
            return NK_ERROR_UNKNOWN;
        }
        auto stream = std::make_shared<AndroidResourceStream>();
        stream->fd = fd;
        if (flags & NK_RESOURCE_OPEN_READ)
            stream->flags |= NK_RESOURCE_STREAM_READABLE;
        if (flags & NK_RESOURCE_OPEN_WRITE)
            stream->flags |= NK_RESOURCE_STREAM_WRITABLE;
        if (::lseek(fd, 0, SEEK_CUR) >= 0)
            stream->flags |= NK_RESOURCE_STREAM_SEEKABLE;
        const auto handle =
            nk::core::handles().insert(nk::core::ResourceType::resource_stream, stream);
        if (!handle) {
            nk::core::set_error("could not allocate Android resource stream handle");
            return NK_ERROR_OUT_OF_MEMORY;
        }
        *out_stream = handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_resource_get_persisted_access(const nk_resource *resource,
                                                   uint32_t *out_flags) {
    return nk::core::result_boundary("unexpected error while querying persisted URI access",
                                     [&]() -> nk_result {
        if (const auto thread = require_thread(); thread != NK_OK)
            return thread;
        if (!resource || resource->struct_size < sizeof(nk_resource) || !resource->uri ||
            !*resource->uri || !out_flags) {
            nk::core::set_error("persisted resource access arguments are invalid");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        auto host_resource = context_host();
        if (!host_resource)
            return NK_ERROR_UNSUPPORTED;
        auto *env = environment();
        auto *bridge = env ? bridge_class(env) : nullptr;
        if (!env || !bridge)
            return NK_ERROR_UNKNOWN;
        auto method = env->GetStaticMethodID(
            bridge, "persistedResourceAccess", "(Landroid/view/ViewGroup;Ljava/lang/String;)I");
        auto uri = from_utf8(env, resource->uri);
        const auto flags = method ? env->CallStaticIntMethod(
                                        bridge, method, host_resource->view_group, uri)
                                  : NK_ERROR_UNKNOWN;
        if (uri)
            env->DeleteLocalRef(uri);
        env->DeleteLocalRef(bridge);
        if (!method || clear_java_exception(env, "Android persisted URI query failed"))
            return NK_ERROR_UNKNOWN;
        if (flags < 0) {
            nk::core::set_error(flags == NK_ERROR_UNSUPPORTED
                                    ? "Android URI access cannot be persisted"
                                    : "Android persisted URI query failed");
            return flags;
        }
        *out_flags = static_cast<uint32_t>(flags);
        return NK_OK;
    });
}

nk_result NK_CALL nk_resource_set_persisted_access(const nk_resource *resource,
                                                   uint32_t access_flags,
                                                   uint32_t *out_flags) {
    return nk::core::result_boundary("unexpected error while updating persisted URI access",
                                     [&]() -> nk_result {
        if (const auto thread = require_thread(); thread != NK_OK)
            return thread;
        if (!resource || resource->struct_size < sizeof(nk_resource) || !resource->uri ||
            !*resource->uri || !out_flags ||
            (access_flags & ~(NK_RESOURCE_READABLE | NK_RESOURCE_WRITABLE)) != 0) {
            nk::core::set_error("persisted resource access arguments are invalid");
            return NK_ERROR_INVALID_ARGUMENT;
        }
        auto host_resource = context_host();
        if (!host_resource)
            return NK_ERROR_UNSUPPORTED;
        auto *env = environment();
        auto *bridge = env ? bridge_class(env) : nullptr;
        if (!env || !bridge)
            return NK_ERROR_UNKNOWN;
        auto method = env->GetStaticMethodID(
            bridge, "setPersistedResourceAccess",
            "(Landroid/view/ViewGroup;Ljava/lang/String;I)I");
        auto uri = from_utf8(env, resource->uri);
        const auto flags = method ? env->CallStaticIntMethod(
                                        bridge, method, host_resource->view_group, uri,
                                        static_cast<jint>(access_flags))
                                  : NK_ERROR_UNKNOWN;
        if (uri)
            env->DeleteLocalRef(uri);
        env->DeleteLocalRef(bridge);
        if (!method || clear_java_exception(env, "Android persisted URI update failed"))
            return NK_ERROR_UNKNOWN;
        if (flags < 0) {
            nk::core::set_error(flags == NK_ERROR_UNSUPPORTED
                                    ? "Android URI access cannot be persisted"
                                    : "Android persisted URI update failed");
            return flags;
        }
        *out_flags = static_cast<uint32_t>(flags);
        return NK_OK;
    });
}

nk_result NK_CALL nk_resource_stream_info_get(nk_handle handle,
                                              nk_resource_stream_info *out_info) {
    if (!out_info || out_info->struct_size < sizeof(nk_resource_stream_info)) {
        nk::core::set_error("resource stream info output is invalid");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto stream = resource_stream(handle);
    if (!stream) {
        nk::core::set_error("invalid Android resource stream handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    std::lock_guard lock(stream->mutex);
    struct stat status {};
    out_info->flags = stream->flags;
    out_info->size = UINT64_MAX;
    if (::fstat(stream->fd, &status) == 0 && S_ISREG(status.st_mode) && status.st_size >= 0) {
        out_info->size = static_cast<uint64_t>(status.st_size);
        out_info->flags |= NK_RESOURCE_STREAM_SIZE_KNOWN;
    }
    return NK_OK;
}

nk_result NK_CALL nk_resource_read(nk_handle handle, void *buffer, uint64_t size,
                                   uint64_t *out_read) {
    if ((!buffer && size) || !out_read || size > static_cast<uint64_t>(SSIZE_MAX)) {
        nk::core::set_error("resource read arguments are invalid");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto stream = resource_stream(handle);
    if (!stream) {
        nk::core::set_error("invalid Android resource stream handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (!(stream->flags & NK_RESOURCE_STREAM_READABLE)) {
        nk::core::set_error("Android resource stream is not readable");
        return NK_ERROR_UNSUPPORTED;
    }
    std::lock_guard lock(stream->mutex);
    ssize_t count = 0;
    do {
        count = ::read(stream->fd, buffer, static_cast<std::size_t>(size));
    } while (count < 0 && errno == EINTR);
    if (count < 0) {
        nk::core::set_error("Android resource read failed");
        return NK_ERROR_UNKNOWN;
    }
    *out_read = static_cast<uint64_t>(count);
    return NK_OK;
}

nk_result NK_CALL nk_resource_write(nk_handle handle, const void *buffer, uint64_t size,
                                    uint64_t *out_written) {
    if ((!buffer && size) || !out_written || size > static_cast<uint64_t>(SSIZE_MAX)) {
        nk::core::set_error("resource write arguments are invalid");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto stream = resource_stream(handle);
    if (!stream) {
        nk::core::set_error("invalid Android resource stream handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (!(stream->flags & NK_RESOURCE_STREAM_WRITABLE)) {
        nk::core::set_error("Android resource stream is not writable");
        return NK_ERROR_UNSUPPORTED;
    }
    std::lock_guard lock(stream->mutex);
    ssize_t count = 0;
    do {
        count = ::write(stream->fd, buffer, static_cast<std::size_t>(size));
    } while (count < 0 && errno == EINTR);
    if (count < 0) {
        nk::core::set_error("Android resource write failed");
        return NK_ERROR_UNKNOWN;
    }
    *out_written = static_cast<uint64_t>(count);
    return NK_OK;
}

nk_result NK_CALL nk_resource_seek(nk_handle handle, int64_t offset, nk_seek_origin origin,
                                   uint64_t *out_position) {
    if (!out_position || origin > NK_SEEK_END) {
        nk::core::set_error("resource seek arguments are invalid");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto stream = resource_stream(handle);
    if (!stream) {
        nk::core::set_error("invalid Android resource stream handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (!(stream->flags & NK_RESOURCE_STREAM_SEEKABLE)) {
        nk::core::set_error("Android resource stream is not seekable");
        return NK_ERROR_UNSUPPORTED;
    }
    const int whence = origin == NK_SEEK_START ? SEEK_SET
                       : origin == NK_SEEK_CURRENT ? SEEK_CUR
                                                   : SEEK_END;
    std::lock_guard lock(stream->mutex);
    const auto position = ::lseek(stream->fd, static_cast<off_t>(offset), whence);
    if (position < 0) {
        nk::core::set_error("Android resource seek failed");
        return errno == ESPIPE ? NK_ERROR_UNSUPPORTED : NK_ERROR_UNKNOWN;
    }
    *out_position = static_cast<uint64_t>(position);
    return NK_OK;
}

nk_result NK_CALL nk_resource_close(nk_handle handle) {
    if (!nk::core::handles().erase(handle, nk::core::ResourceType::resource_stream)) {
        nk::core::set_error("invalid Android resource stream handle");
        return NK_ERROR_INVALID_HANDLE;
    }
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
    return start_file_dialog(NK_DIALOG_OPEN_RESOURCE, true, parent, options, out_request);
}

nk_result NK_CALL nk_dialog_save_resource(nk_handle parent,
                                          const nk_file_dialog_options *options,
                                          nk_request_id *out_request) {
    return start_file_dialog(NK_DIALOG_SAVE_RESOURCE, true, parent, options, out_request);
}

nk_result NK_CALL nk_dialog_select_resource_directory(
    nk_handle parent, const nk_file_dialog_options *options, nk_request_id *out_request) {
    return start_file_dialog(NK_DIALOG_SELECT_RESOURCE_DIRECTORY, true, parent, options, out_request);
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
    event.kind = dialog.resources ? NK_EVENT_DIALOG_RESOURCES_COMPLETE
                                  : NK_EVENT_DIALOG_PATHS_COMPLETE;
    event.flags = dialog.operation;
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

nk_result NK_CALL nk_key_get_state(nk_handle handle, nk_key key, nk_input_action *out_action) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    if (!out_action || key > NK_KEY_LAST)
        return NK_ERROR_INVALID_ARGUMENT;
    *out_action = resource->keys[key];
    return NK_OK;
}

nk_result NK_CALL nk_pointer_button_get_state(nk_handle handle, nk_pointer_button button,
                                              nk_input_action *out_action) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    if (!out_action || button > NK_POINTER_BUTTON_LAST)
        return NK_ERROR_INVALID_ARGUMENT;
    *out_action = resource->pointer_buttons[button];
    return NK_OK;
}

nk_result NK_CALL nk_pointer_get_position(nk_handle handle, double *out_x, double *out_y) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    if (!out_x || !out_y)
        return NK_ERROR_INVALID_ARGUMENT;
    *out_x = resource->pointer_x;
    *out_y = resource->pointer_y;
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_text_input_state(nk_handle handle,
                                                   const nk_text_input_state *state) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    if (!state || state->struct_size < sizeof(nk_text_input_state) || !state->text) {
        nk::core::set_error("text input state is missing or too small");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    uint32_t codepoints = 0;
    for (const auto *cursor = reinterpret_cast<const unsigned char *>(state->text); *cursor;
         ++cursor)
        codepoints += (*cursor & 0xc0u) != 0x80u;
    const bool no_composition = state->composition_start == NK_TEXT_POSITION_NONE &&
                                state->composition_end == NK_TEXT_POSITION_NONE;
    const bool valid_composition = state->composition_start != NK_TEXT_POSITION_NONE &&
                                   state->composition_end != NK_TEXT_POSITION_NONE &&
                                   state->composition_start <= state->composition_end &&
                                   state->composition_end <= codepoints;
    if (state->selection_start > state->selection_end || state->selection_end > codepoints ||
        (!no_composition && !valid_composition)) {
        nk::core::set_error("text input ranges are inconsistent with the supplied text");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto *env = environment();
    auto text = env ? from_utf8(env, state->text) : nullptr;
    if (!env || !text)
        return NK_ERROR_OUT_OF_MEMORY;
    jvalue arguments[6]{};
    arguments[0].l = resource->view;
    arguments[1].l = text;
    arguments[2].i = static_cast<jint>(state->selection_start);
    arguments[3].i = static_cast<jint>(state->selection_end);
    arguments[4].i = state->composition_start == NK_TEXT_POSITION_NONE
                         ? -1
                         : static_cast<jint>(state->composition_start);
    arguments[5].i = state->composition_end == NK_TEXT_POSITION_NONE
                         ? -1
                         : static_cast<jint>(state->composition_end);
    const auto result = java_void_surface(
        resource, "setSurfaceTextInputState", "(Landroid/view/SurfaceView;Ljava/lang/String;IIII)V",
        arguments);
    env->DeleteLocalRef(text);
    return result;
}

nk_result NK_CALL nk_surface_set_text_input_active(nk_handle handle, uint32_t active) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    if (active > 1) {
        nk::core::set_error("text input active state must be zero or one");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    jvalue arguments[2]{};
    arguments[0].l = resource->view;
    arguments[1].z = active ? JNI_TRUE : JNI_FALSE;
    return java_void_surface(resource, "setSurfaceTextInputActive",
                             "(Landroid/view/SurfaceView;Z)V", arguments);
}

nk_result NK_CALL nk_joystick_list(nk_handle *output, uint32_t *inout_count) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    if (!inout_count)
        return NK_ERROR_INVALID_ARGUMENT;
    const auto required = static_cast<uint32_t>(joysticks.size());
    if (!output || *inout_count < required) {
        *inout_count = required;
        return required ? NK_ERROR_BUFFER_TOO_SMALL : NK_OK;
    }
    uint32_t index = 0;
    for (const auto &[device, resource] : joysticks)
        output[index++] = resource->handle;
    *inout_count = required;
    return NK_OK;
}

nk_result NK_CALL nk_joystick_get_name(nk_handle handle, char *buffer, uint32_t *inout_size) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    auto resource = joystick(handle);
    return resource ? copy_string_result(resource->name, buffer, inout_size)
                    : NK_ERROR_INVALID_HANDLE;
}

nk_result NK_CALL nk_joystick_get_guid(nk_handle handle, char *buffer, uint32_t *inout_size) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    auto resource = joystick(handle);
    return resource ? copy_string_result(resource->guid, buffer, inout_size)
                    : NK_ERROR_INVALID_HANDLE;
}

nk_result NK_CALL nk_joystick_get_axes(nk_handle handle, float *axes, uint32_t *inout_count) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    auto resource = joystick(handle);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    if (!inout_count)
        return NK_ERROR_INVALID_ARGUMENT;
    if (!axes || *inout_count < resource->axes.size()) {
        *inout_count = resource->axes.size();
        return NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::copy(resource->axes.begin(), resource->axes.end(), axes);
    *inout_count = resource->axes.size();
    return NK_OK;
}

nk_result NK_CALL nk_joystick_get_buttons(nk_handle handle, uint8_t *buttons,
                                          uint32_t *inout_count) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    auto resource = joystick(handle);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    if (!inout_count)
        return NK_ERROR_INVALID_ARGUMENT;
    if (!buttons || *inout_count < resource->buttons.size()) {
        *inout_count = resource->buttons.size();
        return NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::copy(resource->buttons.begin(), resource->buttons.end(), buttons);
    *inout_count = resource->buttons.size();
    return NK_OK;
}

nk_result NK_CALL nk_joystick_get_hats(nk_handle handle, uint8_t *hats, uint32_t *inout_count) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    if (!joystick(handle))
        return NK_ERROR_INVALID_HANDLE;
    if (!inout_count)
        return NK_ERROR_INVALID_ARGUMENT;
    *inout_count = 0;
    (void)hats;
    return NK_OK;
}

nk_result NK_CALL nk_joystick_get_diagnostics(char *buffer, uint32_t *inout_size) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    return copy_string_result({}, buffer, inout_size);
}

nk_result NK_CALL nk_surface_create(nk_handle parent, const nk_surface_options *options,
                                    nk_handle *out_surface) {
    return nk::core::result_boundary(
        "unexpected error while creating an Android graphics surface", [&]() -> nk_result {
            if (const auto thread = require_thread(); thread != NK_OK)
                return thread;
            auto parent_resource = host(parent);
            if (!parent_resource) {
                nk::core::set_error("Android graphics surface parent is not a mobile host");
                return NK_ERROR_INVALID_HANDLE;
            }
            if (!options || options->struct_size < sizeof(*options) || !out_surface ||
                options->width <= 0 || options->height <= 0 ||
                (options->api != NK_GRAPHICS_OPENGL_ES &&
                 options->api != NK_GRAPHICS_VULKAN)) {
                nk::core::set_error("invalid Android graphics surface options");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            *out_surface = NK_INVALID_HANDLE;
            const bool vulkan = options->api == NK_GRAPHICS_VULKAN;
            const uint32_t major =
                vulkan ? 0 : (options->major_version ? options->major_version : 2);
            if ((!vulkan && major != 2 && major != 3) || options->minor_version != 0 ||
                (options->flags & (NK_SURFACE_DEBUG_CONTEXT | NK_SURFACE_FORWARD_COMPATIBLE))) {
                nk::core::set_error("unsupported Android graphics surface configuration");
                return NK_ERROR_UNSUPPORTED;
            }
            if (vulkan && (options->major_version || options->share_surface ||
                           (options->flags & (NK_SURFACE_DEPTH | NK_SURFACE_STENCIL)))) {
                nk::core::set_error("Vulkan surfaces do not accept GL context options");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            auto shared = options->share_surface ? surface(options->share_surface) : nullptr;
            if (options->share_surface && !shared) {
                nk::core::set_error("invalid shared Android graphics surface");
                return NK_ERROR_INVALID_HANDLE;
            }
            if (shared && (shared->host != parent || shared->api != options->api ||
                           shared->major_version != major)) {
                nk::core::set_error("shared surfaces must use identical context options");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (!vulkan && egl_display == EGL_NO_DISPLAY) {
                egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
                if (egl_display == EGL_NO_DISPLAY || !eglInitialize(egl_display, nullptr, nullptr)) {
                    egl_display = EGL_NO_DISPLAY;
                    nk::core::set_error("Android EGL display initialization failed");
                    return NK_ERROR_UNSUPPORTED;
                }
            }
            EGLConfig config = nullptr;
            EGLContext context = EGL_NO_CONTEXT;
            if (!vulkan) {
                EGLint attributes[] = {
                    EGL_SURFACE_TYPE,
                    EGL_WINDOW_BIT,
                    EGL_RENDERABLE_TYPE,
                    major == 3 ? 0x0040 : EGL_OPENGL_ES2_BIT,
                    EGL_RED_SIZE,
                    8,
                    EGL_GREEN_SIZE,
                    8,
                    EGL_BLUE_SIZE,
                    8,
                    EGL_ALPHA_SIZE,
                    (options->flags & NK_SURFACE_ALPHA) ? 8 : 0,
                    EGL_DEPTH_SIZE,
                    (options->flags & NK_SURFACE_DEPTH) ? 16 : 0,
                    EGL_STENCIL_SIZE,
                    (options->flags & NK_SURFACE_STENCIL) ? 8 : 0,
                    EGL_NONE};
                EGLint config_count = 0;
                if (!eglChooseConfig(egl_display, attributes, &config, 1, &config_count) ||
                    config_count == 0) {
                    nk::core::set_error("Android EGL configuration is unavailable");
                    return NK_ERROR_UNSUPPORTED;
                }
                EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION,
                                               static_cast<EGLint>(major), EGL_NONE};
                eglBindAPI(EGL_OPENGL_ES_API);
                context = eglCreateContext(egl_display, config,
                                           shared ? shared->context : EGL_NO_CONTEXT,
                                           context_attributes);
                if (context == EGL_NO_CONTEXT) {
                    nk::core::set_error("Android OpenGL ES context creation failed");
                    return NK_ERROR_UNSUPPORTED;
                }
            }
            auto resource = std::make_shared<AndroidSurface>();
            resource->host = parent;
            resource->display = egl_display;
            resource->config = config;
            resource->context = context;
            resource->api = options->api;
            resource->major_version = major;
            resource->minor_version = options->minor_version;
            resource->context_flags = options->flags &
                                      (NK_SURFACE_DEBUG_CONTEXT |
                                       NK_SURFACE_FORWARD_COMPATIBLE);
            resource->shared_surface = shared;
            const auto handle =
                nk::core::handles().insert(nk::core::ResourceType::surface, resource);
            if (!handle) {
                if (context != EGL_NO_CONTEXT)
                    eglDestroyContext(egl_display, context);
                return NK_ERROR_OUT_OF_MEMORY;
            }
            resource->handle = handle;
            surfaces.emplace(handle, resource);
            if (shared)
                ++shared->share_dependents;
            auto *env = environment();
            auto *bridge = env ? bridge_class(env) : nullptr;
            auto method = bridge ? env->GetStaticMethodID(
                                       bridge, "createSurface",
                                       "(Landroid/view/ViewGroup;JIIIII)Landroid/view/SurfaceView;")
                                  : nullptr;
            jobject view = method ? env->CallStaticObjectMethod(
                                        bridge, method, parent_resource->view_group,
                                        static_cast<jlong>(handle),
                                        static_cast<jint>(options->flags),
                                        static_cast<jint>(options->x),
                                        static_cast<jint>(options->y),
                                        static_cast<jint>(options->width),
                                        static_cast<jint>(options->height))
                                  : nullptr;
            if (bridge)
                env->DeleteLocalRef(bridge);
            if (!method || clear_java_exception(env, "Android SurfaceView creation failed") ||
                !view) {
                destroy_surface(handle);
                return NK_ERROR_UNKNOWN;
            }
            resource->view = env->NewGlobalRef(view);
            if (!resource->view) {
                jvalue arguments[1]{};
                arguments[0].l = view;
                java_void_surface(resource, "destroySurface", "(Landroid/view/SurfaceView;)V",
                                  arguments);
                env->DeleteLocalRef(view);
                destroy_surface(handle);
                return NK_ERROR_OUT_OF_MEMORY;
            }
            env->DeleteLocalRef(view);
            *out_surface = handle;
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_destroy(nk_handle handle) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    return destroy_surface(handle);
}

nk_result NK_CALL nk_surface_show(nk_handle handle, uint32_t visible) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    jvalue arguments[2]{};
    arguments[0].l = resource->view;
    arguments[1].z = visible ? JNI_TRUE : JNI_FALSE;
    return java_void_surface(resource, "showSurface", "(Landroid/view/SurfaceView;Z)V",
                             arguments);
}

nk_result NK_CALL nk_surface_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                        int32_t height) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
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
    return java_void_surface(resource, "setSurfaceBounds",
                             "(Landroid/view/SurfaceView;IIII)V", arguments);
}

nk_result NK_CALL nk_surface_make_current(nk_handle handle) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    if (resource->api != NK_GRAPHICS_OPENGL_ES) {
        nk::core::set_error("operation requires an Android OpenGL ES surface");
        return NK_ERROR_UNSUPPORTED;
    }
    if (resource->surface == EGL_NO_SURFACE) {
        nk::core::set_error("Android graphics surface is not ready");
        return NK_ERROR_INVALID_REQUEST;
    }
    if (!eglMakeCurrent(resource->display, resource->surface, resource->surface,
                        resource->context)) {
        nk::core::set_error("could not make the Android EGL context current");
        return NK_ERROR_UNKNOWN;
    }
    return NK_OK;
}

nk_result NK_CALL nk_surface_present(nk_handle handle) {
    if (const auto result = nk_surface_make_current(handle); result != NK_OK)
        return result;
    auto resource = surface(handle);
    if (!eglSwapBuffers(resource->display, resource->surface)) {
        nk::core::set_error("could not present the Android EGL surface");
        return NK_ERROR_UNKNOWN;
    }
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_framebuffer_size(nk_handle handle, int32_t *out_width,
                                                  int32_t *out_height) {
    if (const auto thread = require_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    if (!out_width || !out_height)
        return NK_ERROR_INVALID_ARGUMENT;
    if (!resource->window)
        return NK_ERROR_INVALID_REQUEST;
    *out_width = resource->framebuffer_width;
    *out_height = resource->framebuffer_height;
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_proc_address(nk_handle handle, const char *name,
                                              nk_graphics_proc *out_proc) {
    if (!name || !*name || !out_proc)
        return NK_ERROR_INVALID_ARGUMENT;
    *out_proc = nullptr;
    if (const auto result = nk_surface_make_current(handle); result != NK_OK)
        return result;
    void *address = reinterpret_cast<void *>(eglGetProcAddress(name));
    static void *gles = dlopen("libGLESv2.so", RTLD_LAZY | RTLD_LOCAL);
    if (!address && gles)
        address = dlsym(gles, name);
    if (!address) {
        nk::core::set_error("OpenGL ES procedure is unavailable");
        return NK_ERROR_UNSUPPORTED;
    }
    *out_proc = reinterpret_cast<nk_graphics_proc>(address);
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

static nk_result android_webview_history_query(nk_handle handle, const char *method_name,
                                               uint32_t *out_value) {
    const auto thread = require_thread();
    auto resource = webview(handle);
    if (thread != NK_OK)
        return thread;
    if (!resource || !out_value)
        return !resource ? NK_ERROR_INVALID_HANDLE : NK_ERROR_INVALID_ARGUMENT;
    auto *env = environment();
    auto *bridge = env ? bridge_class(env) : nullptr;
    if (!env || !bridge)
        return NK_ERROR_UNKNOWN;
    auto method = env->GetStaticMethodID(bridge, method_name, "(Landroid/webkit/WebView;)Z");
    const auto value = method && env->CallStaticBooleanMethod(bridge, method, resource->view);
    env->DeleteLocalRef(bridge);
    if (!method || clear_java_exception(env, "Android WebView history query failed"))
        return NK_ERROR_UNKNOWN;
    *out_value = value ? 1u : 0u;
    return NK_OK;
}

static nk_result android_webview_command(nk_handle handle, const char *method_name) {
    const auto thread = require_thread();
    auto resource = webview(handle);
    if (thread != NK_OK)
        return thread;
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    jvalue arguments[1]{};
    arguments[0].l = resource->view;
    return java_void_webview(resource, method_name, "(Landroid/webkit/WebView;)V", arguments);
}
nk_result NK_CALL nk_webview_can_go_back(nk_handle handle, uint32_t *out_can_go_back) {
    return android_webview_history_query(handle, "canGoBack", out_can_go_back);
}

nk_result NK_CALL nk_webview_can_go_forward(nk_handle handle, uint32_t *out_can_go_forward) {
    return android_webview_history_query(handle, "canGoForward", out_can_go_forward);
}

nk_result NK_CALL nk_webview_go_back(nk_handle handle) {
    return android_webview_command(handle, "goBack");
}

nk_result NK_CALL nk_webview_go_forward(nk_handle handle) {
    return android_webview_command(handle, "goForward");
}

nk_result NK_CALL nk_webview_reload(nk_handle handle) {
    return android_webview_command(handle, "reload");
}

nk_result NK_CALL nk_webview_stop(nk_handle handle) {
    return android_webview_command(handle, "stop");
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

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnSurfaceCreated(
    JNIEnv *env, jclass, jlong handle_value, jobject java_surface) {
    auto resource = surface(static_cast<nk_handle>(handle_value));
    if (!resource || !java_surface)
        return;
    release_surface_window(*resource);
    resource->window = ANativeWindow_fromSurface(env, java_surface);
    if (!resource->window)
        return;
    if (resource->api == NK_GRAPHICS_OPENGL_ES) {
        EGLint visual_id = 0;
        eglGetConfigAttrib(resource->display, resource->config, EGL_NATIVE_VISUAL_ID, &visual_id);
        ANativeWindow_setBuffersGeometry(resource->window, 0, 0, visual_id);
        resource->surface =
            eglCreateWindowSurface(resource->display, resource->config, resource->window, nullptr);
        if (resource->surface == EGL_NO_SURFACE) {
            ANativeWindow_release(resource->window);
            resource->window = nullptr;
            return;
        }
    }
    nk::core::QueuedEvent ready;
    ready.kind = NK_EVENT_SURFACE_READY;
    ready.source = resource->handle;
    nk::core::push_event(std::move(ready));
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnSurfaceChanged(
    JNIEnv *, jclass, jlong handle_value, jint width, jint height, jint framebuffer_width,
    jint framebuffer_height) {
    auto resource = surface(static_cast<nk_handle>(handle_value));
    if (!resource)
        return;
    resource->width = width;
    resource->height = height;
    resource->framebuffer_width = framebuffer_width;
    resource->framebuffer_height = framebuffer_height;
    const nk_surface_resize_event payload{width, height, framebuffer_width, framebuffer_height};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_SURFACE_RESIZE;
    event.source = resource->handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnSurfaceDestroyed(
    JNIEnv *, jclass, jlong handle_value) {
    auto resource = surface(static_cast<nk_handle>(handle_value));
    if (resource)
        release_surface_window(*resource);
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnTouch(
    JNIEnv *, jclass, jlong handle_value, jint pointer_id, jint action, jint tool, jfloat x,
    jfloat y, jfloat pressure, jfloat tilt_x, jfloat tilt_y, jint modifiers) {
    auto resource = surface(static_cast<nk_handle>(handle_value));
    if (!resource)
        return;
    const nk_touch_event payload{static_cast<uint32_t>(pointer_id),
                                 static_cast<nk_touch_action>(action),
                                 static_cast<nk_touch_tool>(tool),
                                 static_cast<nk_modifiers>(modifiers),
                                 x,
                                 y,
                                 pressure,
                                 tilt_x,
                                 tilt_y,
                                 0};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_TOUCH;
    event.source = resource->handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnPointerMove(
    JNIEnv *, jclass, jlong handle_value, jfloat x, jfloat y) {
    auto resource = surface(static_cast<nk_handle>(handle_value));
    if (!resource)
        return;
    resource->pointer_x = x;
    resource->pointer_y = y;
    const nk_pointer_move_event payload{x, y};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_POINTER_MOVE;
    event.source = resource->handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnPointerEnter(
    JNIEnv *, jclass, jlong handle_value, jboolean entered) {
    auto resource = surface(static_cast<nk_handle>(handle_value));
    if (!resource)
        return;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_POINTER_ENTER;
    event.source = resource->handle;
    event.flags = entered ? 1u : 0u;
    nk::core::push_event(std::move(event));
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnPointerButton(
    JNIEnv *, jclass, jlong handle_value, jint button, jboolean pressed, jint modifiers,
    jfloat x, jfloat y) {
    auto resource = surface(static_cast<nk_handle>(handle_value));
    if (!resource || button < 0 || button > NK_POINTER_BUTTON_LAST)
        return;
    const auto action = pressed ? NK_INPUT_PRESS : NK_INPUT_RELEASE;
    resource->pointer_buttons[static_cast<std::size_t>(button)] = action;
    resource->pointer_x = x;
    resource->pointer_y = y;
    const nk_pointer_button_event payload{static_cast<nk_pointer_button>(button), action,
                                          static_cast<nk_modifiers>(modifiers), 0, x, y};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_POINTER_BUTTON;
    event.source = resource->handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnPointerScroll(
    JNIEnv *, jclass, jlong handle_value, jfloat x, jfloat y) {
    auto resource = surface(static_cast<nk_handle>(handle_value));
    if (!resource)
        return;
    const nk_pointer_scroll_event payload{x, y};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_POINTER_SCROLL;
    event.source = resource->handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnKey(
    JNIEnv *, jclass, jlong handle_value, jint key, jint scancode, jint action, jint modifiers) {
    auto resource = surface(static_cast<nk_handle>(handle_value));
    if (!resource || key < 0 || key > NK_KEY_LAST)
        return;
    resource->keys[static_cast<std::size_t>(key)] = static_cast<nk_input_action>(action);
    const nk_key_event payload{static_cast<nk_key>(key), static_cast<uint32_t>(scancode),
                               static_cast<nk_input_action>(action),
                               static_cast<nk_modifiers>(modifiers)};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_KEY;
    event.source = resource->handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnText(JNIEnv *, jclass,
                                                                      jlong handle_value,
                                                                      jint codepoint) {
    auto resource = surface(static_cast<nk_handle>(handle_value));
    if (!resource || codepoint <= 0 || codepoint > 0x10ffff ||
        (codepoint >= 0xd800 && codepoint <= 0xdfff))
        return;
    const nk_text_input_event payload{static_cast<uint32_t>(codepoint), 0};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_TEXT_INPUT;
    event.source = resource->handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnTextEdit(
    JNIEnv *env, jclass, jlong handle_value, jint action, jstring text, jint replace_start,
    jint replace_end, jint selection_start, jint selection_end, jint composition_start,
    jint composition_end) {
    auto resource = surface(static_cast<nk_handle>(handle_value));
    if (!resource || action < NK_TEXT_EDIT_COMPOSE || action > NK_TEXT_EDIT_SET_COMPOSITION)
        return;
    const auto value = to_utf8(env, text);
    nk_text_edit_event payload{};
    payload.action = static_cast<nk_text_edit_action>(action);
    payload.text_offset = value.empty() ? 0u : sizeof(payload);
    payload.text_length = static_cast<uint32_t>(value.size());
    auto position = [](jint input) -> nk_text_position {
        return input < 0 ? NK_TEXT_POSITION_NONE : static_cast<nk_text_position>(input);
    };
    payload.replace_start = position(replace_start);
    payload.replace_end = position(replace_end);
    payload.selection_start = position(selection_start);
    payload.selection_end = position(selection_end);
    payload.composition_start = position(composition_start);
    payload.composition_end = position(composition_end);
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_TEXT_EDIT;
    event.source = resource->handle;
    event.data.resize(sizeof(payload) + value.size() + (value.empty() ? 0u : 1u));
    std::memcpy(event.data.data(), &payload, sizeof(payload));
    if (!value.empty())
        std::memcpy(event.data.data() + sizeof(payload), value.c_str(), value.size() + 1);
    nk::core::push_event(std::move(event));
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnGamepadAxis(
    JNIEnv *, jclass, jlong handle_value, jint device, jint axis, jfloat value) {
    auto resource = surface(static_cast<nk_handle>(handle_value));
    auto controller = joystick_device(device);
    if (!resource || !controller || axis < 0 || axis >= NK_GAMEPAD_AXIS_COUNT)
        return;
    controller->axes[static_cast<std::size_t>(axis)] = value;
    const nk_joystick_axis_event raw{static_cast<uint32_t>(axis), value};
    nk::core::QueuedEvent raw_event;
    raw_event.kind = NK_EVENT_JOYSTICK_AXIS;
    raw_event.source = controller->handle;
    raw_event.data = bytes_of(raw);
    nk::core::push_event(std::move(raw_event));
    nk::core::gamepad_events::update(controller->handle, true);
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnGamepadButton(
    JNIEnv *, jclass, jlong handle_value, jint device, jint button, jboolean pressed) {
    auto resource = surface(static_cast<nk_handle>(handle_value));
    auto controller = joystick_device(device);
    if (!resource || !controller || button < 0 || button >= NK_GAMEPAD_BUTTON_COUNT)
        return;
    controller->buttons[static_cast<std::size_t>(button)] = pressed ? 1u : 0u;
    const nk_joystick_button_event raw{static_cast<uint32_t>(button), pressed ? 1u : 0u};
    nk::core::QueuedEvent raw_event;
    raw_event.kind = NK_EVENT_JOYSTICK_BUTTON;
    raw_event.source = controller->handle;
    raw_event.data = bytes_of(raw);
    nk::core::push_event(std::move(raw_event));
    nk::core::gamepad_events::update(controller->handle, true);
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnGamepadConnected(
    JNIEnv *env, jclass, jint device, jstring name, jstring descriptor) {
    const auto device_name = to_utf8(env, name);
    const auto device_descriptor = to_utf8(env, descriptor);
    if (auto existing = joystick_device(device)) {
        existing->name = device_name;
        existing->guid = controller_guid(device_descriptor);
        return;
    }
    auto resource = std::make_shared<AndroidJoystick>();
    resource->device_id = device;
    resource->name = device_name.empty() ? "Android game controller" : device_name;
    resource->guid = controller_guid(device_descriptor);
    resource->handle =
        nk::core::handles().insert(nk::core::ResourceType::joystick, resource);
    if (!resource->handle)
        return;
    joysticks.emplace(device, resource);
    nk::core::gamepad_events::update(resource->handle, false);
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_JOYSTICK_CONNECTED;
    event.source = resource->handle;
    nk::core::push_event(std::move(event));
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnGamepadDisconnected(
    JNIEnv *, jclass, jint device) {
    const auto found = joysticks.find(device);
    if (found == joysticks.end())
        return;
    const auto handle = found->second->handle;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_JOYSTICK_DISCONNECTED;
    event.source = handle;
    nk::core::push_event(std::move(event));
    nk::core::gamepad_events::disconnect(handle);
    nk::core::handles().erase(handle, nk::core::ResourceType::joystick);
    joysticks.erase(found);
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

JNIEXPORT jboolean JNICALL Java_io_nativekit_NativeKitHost_nativeHandleBack(JNIEnv *, jclass,
                                                                            jlong handle) {
    uint32_t can_go_back = 0;
    if (nk_webview_can_go_back(static_cast<nk_handle>(handle), &can_go_back) != NK_OK ||
        !can_go_back)
        return JNI_FALSE;
    return nk_webview_go_back(static_cast<nk_handle>(handle)) == NK_OK ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitHost_nativeSetLifecycle(JNIEnv *, jclass,
                                                                          jlong handle,
                                                                          jint state) {
    nk_mobile_host_set_lifecycle(static_cast<nk_handle>(handle),
                                 static_cast<nk_mobile_lifecycle_state>(state));
}

JNIEXPORT jint JNICALL Java_io_nativekit_NativeKitHost_nativeDispatchIntent(JNIEnv *env, jclass,
                                                                             jlong handle,
                                                                             jobject intent) {
    nk_mobile_host_event event{};
    event.struct_size = sizeof(event);
    event.kind = NK_MOBILE_HOST_EVENT_ANDROID_INTENT;
    event.platform_context = reinterpret_cast<uintptr_t>(env);
    event.native_event = reinterpret_cast<uintptr_t>(intent);
    return nk_mobile_host_dispatch_event(static_cast<nk_handle>(handle), &event);
}

JNIEXPORT jint JNICALL Java_io_nativekit_NativeKitHost_nativeSetDropEnabled(JNIEnv *, jclass,
                                                                            jlong handle,
                                                                            jboolean enabled) {
    return nk_mobile_host_set_drop_enabled(static_cast<nk_handle>(handle),
                                           enabled == JNI_TRUE ? 1u : 0u);
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
    JNIEnv *env, jclass, jlong host_handle, jint command, jstring title,
    jstring suggested_name) {
    const auto title_value = to_utf8(env, title);
    const auto name_value = to_utf8(env, suggested_name);
    nk_file_dialog_options options{};
    options.struct_size = sizeof(options);
    options.title = title ? title_value.c_str() : nullptr;
    options.suggested_name = suggested_name ? name_value.c_str() : nullptr;
    nk_request_id request = NK_INVALID_REQUEST_ID;
    nk_result result = NK_ERROR_INVALID_ARGUMENT;
    if (command == static_cast<jint>(AndroidDialogCommand::open))
        result = nk_dialog_open_resource(static_cast<nk_handle>(host_handle), &options, &request);
    else if (command == static_cast<jint>(AndroidDialogCommand::save))
        result = nk_dialog_save_resource(static_cast<nk_handle>(host_handle), &options, &request);
    else if (command == static_cast<jint>(AndroidDialogCommand::directory))
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
    JNIEnv *env, jclass, jlong request, jboolean accepted, jobjectArray values,
    jobjectArray mime_types, jobjectArray display_names, jintArray resource_flags) {
    nk::core::callback_boundary([&] {
        const auto request_id = static_cast<nk_request_id>(request);
        const auto found = file_dialogs.find(request_id);
        if (found == file_dialogs.end())
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
        event.kind = dialog.resources ? NK_EVENT_DIALOG_RESOURCES_COMPLETE
                                      : NK_EVENT_DIALOG_PATHS_COMPLETE;
        event.flags = dialog.operation;
        event.request_id = request_id;
        if (dialog.resources) {
            std::vector<ResourceValue> resources;
            resources.reserve(uris.size());
            jint *flags = resource_flags ? env->GetIntArrayElements(resource_flags, nullptr)
                                         : nullptr;
            const auto flag_count = resource_flags ? env->GetArrayLength(resource_flags) : 0;
            const auto mime_count = mime_types ? env->GetArrayLength(mime_types) : 0;
            const auto name_count = display_names ? env->GetArrayLength(display_names) : 0;
            for (std::size_t index = 0; index < uris.size(); ++index) {
                ResourceValue resource;
                resource.uri = std::move(uris[index]);
                auto mime = index < static_cast<std::size_t>(mime_count)
                                ? static_cast<jstring>(env->GetObjectArrayElement(
                                      mime_types, static_cast<jsize>(index)))
                                : nullptr;
                auto name = index < static_cast<std::size_t>(name_count)
                                ? static_cast<jstring>(env->GetObjectArrayElement(
                                      display_names, static_cast<jsize>(index)))
                                : nullptr;
                resource.mime_type = to_utf8(env, mime);
                resource.display_name = to_utf8(env, name);
                if (flags && index < static_cast<std::size_t>(flag_count))
                    resource.flags = static_cast<uint32_t>(flags[index]);
                resources.push_back(std::move(resource));
                if (name)
                    env->DeleteLocalRef(name);
                if (mime)
                    env->DeleteLocalRef(mime);
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

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnIncomingIntent(
    JNIEnv *env, jclass, jlong host_handle, jint kind, jstring text, jstring subject,
    jobjectArray uris, jobjectArray mime_types, jobjectArray display_names,
    jintArray resource_flags) {
    nk::core::callback_boundary([&] {
        const auto source = static_cast<nk_handle>(host_handle);
        if (!host(source) || (kind != 1 && kind != 2))
            return;
        const auto count = uris ? env->GetArrayLength(uris) : 0;
        const auto mime_count = mime_types ? env->GetArrayLength(mime_types) : 0;
        const auto name_count = display_names ? env->GetArrayLength(display_names) : 0;
        const auto flag_count = resource_flags ? env->GetArrayLength(resource_flags) : 0;
        jint *flags = resource_flags ? env->GetIntArrayElements(resource_flags, nullptr) : nullptr;
        std::vector<ResourceValue> resources;
        resources.reserve(static_cast<std::size_t>(count));
        for (jsize index = 0; index < count; ++index) {
            auto uri = static_cast<jstring>(env->GetObjectArrayElement(uris, index));
            auto mime = index < mime_count
                            ? static_cast<jstring>(env->GetObjectArrayElement(mime_types, index))
                            : nullptr;
            auto name = index < name_count
                            ? static_cast<jstring>(env->GetObjectArrayElement(display_names, index))
                            : nullptr;
            ResourceValue resource;
            resource.uri = to_utf8(env, uri);
            resource.mime_type = to_utf8(env, mime);
            resource.display_name = to_utf8(env, name);
            if (flags && index < flag_count)
                resource.flags = static_cast<uint32_t>(flags[index]);
            if (!resource.uri.empty())
                resources.push_back(std::move(resource));
            if (mime)
                env->DeleteLocalRef(mime);
            if (name)
                env->DeleteLocalRef(name);
            if (uri)
                env->DeleteLocalRef(uri);
        }
        if (flags)
            env->ReleaseIntArrayElements(resource_flags, flags, JNI_ABORT);
        nk::core::QueuedEvent event;
        event.kind = kind == 1 ? NK_EVENT_RESOURCE_OPENED : NK_EVENT_SHARE_RECEIVED;
        event.source = source;
        if (kind == 1) {
            if (resources.empty())
                return;
            event.data = resource_payload(false, resources);
        } else {
            const auto text_value = to_utf8(env, text);
            const auto subject_value = to_utf8(env, subject);
            event.data = received_share_payload(text_value, subject_value, resources);
        }
        nk::core::push_event(std::move(event));
    });
}

JNIEXPORT void JNICALL Java_io_nativekit_NativeKitBridge_nativeOnResourceDrop(
    JNIEnv *env, jclass, jlong host_handle, jfloat x, jfloat y, jstring text, jobjectArray uris,
    jobjectArray mime_types, jobjectArray display_names, jintArray resource_flags) {
    nk::core::callback_boundary([&] {
        const auto source = static_cast<nk_handle>(host_handle);
        if (!host(source))
            return;
        const auto count = uris ? env->GetArrayLength(uris) : 0;
        const auto mime_count = mime_types ? env->GetArrayLength(mime_types) : 0;
        const auto name_count = display_names ? env->GetArrayLength(display_names) : 0;
        const auto flag_count = resource_flags ? env->GetArrayLength(resource_flags) : 0;
        jint *flags = resource_flags ? env->GetIntArrayElements(resource_flags, nullptr) : nullptr;
        std::vector<ResourceValue> resources;
        resources.reserve(static_cast<std::size_t>(count));
        for (jsize index = 0; index < count; ++index) {
            auto uri = static_cast<jstring>(env->GetObjectArrayElement(uris, index));
            auto mime = index < mime_count
                            ? static_cast<jstring>(env->GetObjectArrayElement(mime_types, index))
                            : nullptr;
            auto name = index < name_count
                            ? static_cast<jstring>(env->GetObjectArrayElement(display_names, index))
                            : nullptr;
            ResourceValue resource;
            resource.uri = to_utf8(env, uri);
            resource.mime_type = to_utf8(env, mime);
            resource.display_name = to_utf8(env, name);
            if (flags && index < flag_count)
                resource.flags = static_cast<uint32_t>(flags[index]);
            if (!resource.uri.empty())
                resources.push_back(std::move(resource));
            if (name)
                env->DeleteLocalRef(name);
            if (mime)
                env->DeleteLocalRef(mime);
            if (uri)
                env->DeleteLocalRef(uri);
        }
        if (flags)
            env->ReleaseIntArrayElements(resource_flags, flags, JNI_ABORT);
        const auto text_value = to_utf8(env, text);
        if (resources.empty() && text_value.empty())
            return;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_RESOURCE_DROP;
        event.source = source;
        event.data = resource_drop_payload(x, y, text_value, resources);
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
