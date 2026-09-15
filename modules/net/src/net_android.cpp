#include "net_backend.hpp"

#include "android/nativekit_android_internal.hpp"

#include <jni.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t io_buffer_size = 64u * 1024u;

std::mutex active_connections_mutex;
std::unordered_map<nk_request_id, jobject> active_connections;

struct AndroidClientState {
    std::mutex mutex;
    struct Cookie {
        std::string name;
        std::string value;
        std::string domain;
        std::string path;
        bool secure = false;
    };
    std::vector<Cookie> cookies;
};

struct JniMethods {
    jclass url = nullptr;
    jclass connection = nullptr;
    jclass input_stream = nullptr;
    jclass output_stream = nullptr;
    jclass map = nullptr;
    jclass set = nullptr;
    jclass iterator = nullptr;
    jclass entry = nullptr;
    jclass uri = nullptr;
    jclass inet_socket_address = nullptr;
    jclass proxy = nullptr;
    jclass proxy_type = nullptr;
    jclass list = nullptr;

    jmethodID url_constructor = nullptr;
    jmethodID url_relative_constructor = nullptr;
    jmethodID url_open_connection = nullptr;
    jmethodID url_open_connection_proxy = nullptr;
    jmethodID url_to_string = nullptr;
    jmethodID url_host = nullptr;
    jmethodID url_path = nullptr;
    jmethodID set_follow_redirects = nullptr;
    jmethodID set_connect_timeout = nullptr;
    jmethodID set_read_timeout = nullptr;
    jmethodID set_use_caches = nullptr;
    jmethodID set_request_method = nullptr;
    jmethodID set_request_property = nullptr;
    jmethodID set_do_output = nullptr;
    jmethodID get_output_stream = nullptr;
    jmethodID get_response_code = nullptr;
    jmethodID get_header_fields = nullptr;
    jmethodID get_input_stream = nullptr;
    jmethodID get_error_stream = nullptr;
    jmethodID disconnect = nullptr;
    jmethodID output_write = nullptr;
    jmethodID output_close = nullptr;
    jmethodID input_read = nullptr;
    jmethodID input_close = nullptr;
    jmethodID map_entry_set = nullptr;
    jmethodID set_iterator = nullptr;
    jmethodID iterator_has_next = nullptr;
    jmethodID iterator_next = nullptr;
    jmethodID entry_get_key = nullptr;
    jmethodID entry_get_value = nullptr;
    jmethodID list_size = nullptr;
    jmethodID list_get = nullptr;
    jmethodID uri_constructor = nullptr;
    jmethodID uri_host = nullptr;
    jmethodID uri_port = nullptr;
    jmethodID inet_create_unresolved = nullptr;
    jmethodID proxy_constructor = nullptr;
    jfieldID proxy_http = nullptr;
    jfieldID proxy_type_socks = nullptr;

    bool initialize(JNIEnv *env) {
        url = env->FindClass("java/net/URL");
        connection = env->FindClass("java/net/HttpURLConnection");
        input_stream = env->FindClass("java/io/InputStream");
        output_stream = env->FindClass("java/io/OutputStream");
        map = env->FindClass("java/util/Map");
        set = env->FindClass("java/util/Set");
        iterator = env->FindClass("java/util/Iterator");
        entry = env->FindClass("java/util/Map$Entry");
        uri = env->FindClass("java/net/URI");
        inet_socket_address = env->FindClass("java/net/InetSocketAddress");
        proxy = env->FindClass("java/net/Proxy");
        proxy_type = env->FindClass("java/net/Proxy$Type");
        list = env->FindClass("java/util/List");
        if (env->ExceptionCheck() || !url || !connection || !input_stream || !output_stream || !map || !set ||
            !iterator || !entry || !uri || !inet_socket_address || !proxy || !proxy_type || !list)
            return false;

        url_constructor = env->GetMethodID(url, "<init>", "(Ljava/lang/String;)V");
        url_relative_constructor = env->GetMethodID(url, "<init>", "(Ljava/net/URL;Ljava/lang/String;)V");
        url_open_connection = env->GetMethodID(url, "openConnection", "()Ljava/net/URLConnection;");
        url_open_connection_proxy =
            env->GetMethodID(url, "openConnection", "(Ljava/net/Proxy;)Ljava/net/URLConnection;");
        url_to_string = env->GetMethodID(url, "toString", "()Ljava/lang/String;");
        url_host = env->GetMethodID(url, "getHost", "()Ljava/lang/String;");
        url_path = env->GetMethodID(url, "getPath", "()Ljava/lang/String;");
        set_follow_redirects = env->GetMethodID(connection, "setInstanceFollowRedirects", "(Z)V");
        set_connect_timeout = env->GetMethodID(connection, "setConnectTimeout", "(I)V");
        set_read_timeout = env->GetMethodID(connection, "setReadTimeout", "(I)V");
        set_use_caches = env->GetMethodID(connection, "setUseCaches", "(Z)V");
        set_request_method = env->GetMethodID(connection, "setRequestMethod", "(Ljava/lang/String;)V");
        set_request_property =
            env->GetMethodID(connection, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V");
        set_do_output = env->GetMethodID(connection, "setDoOutput", "(Z)V");
        get_output_stream = env->GetMethodID(connection, "getOutputStream", "()Ljava/io/OutputStream;");
        get_response_code = env->GetMethodID(connection, "getResponseCode", "()I");
        get_header_fields = env->GetMethodID(connection, "getHeaderFields", "()Ljava/util/Map;");
        get_input_stream = env->GetMethodID(connection, "getInputStream", "()Ljava/io/InputStream;");
        get_error_stream = env->GetMethodID(connection, "getErrorStream", "()Ljava/io/InputStream;");
        disconnect = env->GetMethodID(connection, "disconnect", "()V");
        output_write = env->GetMethodID(output_stream, "write", "([BII)V");
        output_close = env->GetMethodID(output_stream, "close", "()V");
        input_read = env->GetMethodID(input_stream, "read", "([BII)I");
        input_close = env->GetMethodID(input_stream, "close", "()V");
        map_entry_set = env->GetMethodID(map, "entrySet", "()Ljava/util/Set;");
        set_iterator = env->GetMethodID(set, "iterator", "()Ljava/util/Iterator;");
        iterator_has_next = env->GetMethodID(iterator, "hasNext", "()Z");
        iterator_next = env->GetMethodID(iterator, "next", "()Ljava/lang/Object;");
        entry_get_key = env->GetMethodID(entry, "getKey", "()Ljava/lang/Object;");
        entry_get_value = env->GetMethodID(entry, "getValue", "()Ljava/lang/Object;");
        list_size = env->GetMethodID(list, "size", "()I");
        list_get = env->GetMethodID(list, "get", "(I)Ljava/lang/Object;");
        uri_constructor = env->GetMethodID(uri, "<init>", "(Ljava/lang/String;)V");
        uri_host = env->GetMethodID(uri, "getHost", "()Ljava/lang/String;");
        uri_port = env->GetMethodID(uri, "getPort", "()I");
        inet_create_unresolved = env->GetStaticMethodID(
            inet_socket_address, "createUnresolved", "(Ljava/lang/String;I)Ljava/net/InetSocketAddress;");
        proxy_constructor =
            env->GetMethodID(proxy, "<init>", "(Ljava/net/Proxy$Type;Ljava/net/SocketAddress;)V");
        proxy_http = env->GetStaticFieldID(proxy_type, "HTTP", "Ljava/net/Proxy$Type;");
        proxy_type_socks = env->GetStaticFieldID(proxy_type, "SOCKS", "Ljava/net/Proxy$Type;");
        return !env->ExceptionCheck() && url_constructor && url_relative_constructor && url_open_connection &&
               url_open_connection_proxy && url_to_string && url_host && url_path && set_follow_redirects &&
               set_read_timeout && set_use_caches && set_request_method && set_request_property && set_do_output && get_output_stream &&
               get_response_code && get_header_fields && get_input_stream && get_error_stream && disconnect &&
               output_write && output_close && input_read && input_close && map_entry_set && set_iterator &&
               iterator_has_next && iterator_next && entry_get_key && entry_get_value && list_size && list_get &&
               uri_constructor && uri_host && uri_port && inet_create_unresolved && proxy_constructor && proxy_http &&
               proxy_type_socks;
    }
};

bool register_connection(JNIEnv *env, nk_request_id request, jobject connection) {
    auto *global = env->NewGlobalRef(connection);
    if (env->ExceptionCheck() || !global)
        return false;
    try {
        std::lock_guard lock(active_connections_mutex);
        const auto inserted = active_connections.emplace(request, global);
        if (!inserted.second) {
            env->DeleteGlobalRef(global);
            return false;
        }
        return true;
    } catch (...) {
        env->DeleteGlobalRef(global);
        return false;
    }
}

void unregister_connection(JNIEnv *env, nk_request_id request) {
    jobject connection = nullptr;
    {
        std::lock_guard lock(active_connections_mutex);
        const auto found = active_connections.find(request);
        if (found == active_connections.end())
            return;
        connection = found->second;
        active_connections.erase(found);
    }
    env->DeleteGlobalRef(connection);
}

struct ActiveConnection final {
    JNIEnv *env;
    nk_request_id request;

    ~ActiveConnection() {
        unregister_connection(env, request);
    }
};

bool append_utf8(std::string &output, uint32_t codepoint) {
    if (codepoint <= 0x7f) {
        output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else if (codepoint <= 0x10ffff) {
        output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
    } else {
        return false;
    }
    return true;
}

bool utf8_to_utf16(std::string_view input, std::vector<jchar> &output) {
    output.clear();
    output.reserve(input.size());
    for (std::size_t index = 0; index < input.size();) {
        const auto first = static_cast<unsigned char>(input[index++]);
        uint32_t codepoint = 0;
        std::size_t continuation = 0;
        if (first < 0x80) {
            codepoint = first;
        } else if (first >= 0xc2 && first <= 0xdf) {
            codepoint = first & 0x1f;
            continuation = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            codepoint = first & 0x0f;
            continuation = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            codepoint = first & 0x07;
            continuation = 3;
        } else {
            return false;
        }
        if (index + continuation > input.size())
            return false;
        for (std::size_t count = 0; count < continuation; ++count) {
            const auto byte = static_cast<unsigned char>(input[index++]);
            if ((byte & 0xc0) != 0x80)
                return false;
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }
        if ((continuation == 2 && codepoint < 0x800) || (continuation == 3 && codepoint < 0x10000) ||
            codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
            return false;
        if (codepoint <= 0xffff) {
            output.push_back(static_cast<jchar>(codepoint));
        } else {
            codepoint -= 0x10000;
            output.push_back(static_cast<jchar>(0xd800 | (codepoint >> 10)));
            output.push_back(static_cast<jchar>(0xdc00 | (codepoint & 0x3ff)));
        }
    }
    return true;
}

jstring java_string(JNIEnv *env, std::string_view value) {
    std::vector<jchar> utf16;
    if (!utf8_to_utf16(value, utf16))
        return nullptr;
    return env->NewString(utf16.empty() ? nullptr : utf16.data(), static_cast<jsize>(utf16.size()));
}

std::string native_string(JNIEnv *env, jstring value) {
    if (!value)
        return {};
    const auto length = env->GetStringLength(value);
    const auto *chars = env->GetStringChars(value, nullptr);
    if (!chars)
        return {};
    std::string result;
    result.reserve(static_cast<std::size_t>(length));
    for (jsize index = 0; index < length; ++index) {
        uint32_t codepoint = chars[index];
        if (codepoint >= 0xd800 && codepoint <= 0xdbff && index + 1 < length &&
            chars[index + 1] >= 0xdc00 && chars[index + 1] <= 0xdfff) {
            codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (chars[++index] - 0xdc00);
        } else if (codepoint >= 0xd800 && codepoint <= 0xdfff) {
            codepoint = 0xfffd;
        }
        if (!append_utf8(result, codepoint))
            result.clear();
    }
    env->ReleaseStringChars(value, chars);
    return result;
}

bool contains(std::string_view value, std::string_view needle) {
    return value.find(needle) != std::string_view::npos;
}

nk_result java_exception(JNIEnv *env) {
    if (!env->ExceptionCheck())
        return NK_OK;
    auto *throwable = env->ExceptionOccurred();
    env->ExceptionClear();
    std::string name;
    if (throwable) {
        auto *throwable_class = env->GetObjectClass(throwable);
        auto *class_class = env->FindClass("java/lang/Class");
        auto get_name = class_class ? env->GetMethodID(class_class, "getName", "()Ljava/lang/String;") : nullptr;
        if (get_name && !env->ExceptionCheck()) {
            auto *class_name = static_cast<jstring>(env->CallObjectMethod(throwable_class, get_name));
            if (!env->ExceptionCheck())
                name = native_string(env, class_name);
            if (class_name)
                env->DeleteLocalRef(class_name);
        }
        if (class_class)
            env->DeleteLocalRef(class_class);
        if (throwable_class)
            env->DeleteLocalRef(throwable_class);
        env->DeleteLocalRef(throwable);
        env->ExceptionClear();
    }
    if (contains(name, "UnknownHost") || contains(name, "UnknownService"))
        return NK_HTTP_ERROR_DNS;
    if (contains(name, "Timeout"))
        return NK_HTTP_ERROR_TIMEOUT;
    if (contains(name, "SSL") || contains(name, "Certificate") || contains(name, "Hostname"))
        return NK_HTTP_ERROR_TLS;
    if (contains(name, "Proxy"))
        return NK_HTTP_ERROR_PROXY;
    if (contains(name, "Protocol") || contains(name, "MalformedURL"))
        return NK_HTTP_ERROR_PROTOCOL;
    return NK_HTTP_ERROR_CONNECTION;
}

bool lower_equal(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index])))
            return false;
    return true;
}

bool https_url(std::string_view url) {
    return url.size() >= 8 && lower_equal(url.substr(0, 8), "https://");
}

bool http_url(std::string_view url) {
    return url.size() >= 7 && lower_equal(url.substr(0, 7), "http://");
}

const char *method_name(nk_http_method method) {
    switch (method) {
    case NK_HTTP_METHOD_GET:
        return "GET";
    case NK_HTTP_METHOD_POST:
        return "POST";
    case NK_HTTP_METHOD_PUT:
        return "PUT";
    case NK_HTTP_METHOD_PATCH:
        return "PATCH";
    case NK_HTTP_METHOD_DELETE:
        return "DELETE";
    case NK_HTTP_METHOD_HEAD:
        return "HEAD";
    case NK_HTTP_METHOD_OPTIONS:
        return "OPTIONS";
    case NK_HTTP_METHOD_TRACE:
        return "TRACE";
    case NK_HTTP_METHOD_CONNECT:
        return "CONNECT";
    default:
        return nullptr;
    }
}

std::shared_ptr<AndroidClientState> client_state(const nk::net::RequestPtr &request) {
    if (request->client->config.cookie_policy != NK_HTTP_COOKIES_SESSION)
        return {};
    std::lock_guard lock(request->client->backend_mutex);
    if (request->client->backend_state)
        return std::static_pointer_cast<AndroidClientState>(request->client->backend_state);
    auto state = std::make_shared<AndroidClientState>();
    request->client->backend_state = state;
    return state;
}

bool has_header(const std::vector<nk::net::OwnedHeader> &headers, std::string_view name) {
    return std::any_of(headers.begin(), headers.end(), [&](const auto &header) {
        return lower_equal(header.name, name);
    });
}

std::string base64(std::string_view value) {
    constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((value.size() + 2) / 3 * 4);
    for (std::size_t index = 0; index < value.size();) {
        const auto first = static_cast<unsigned char>(value[index++]);
        const bool has_second = index < value.size();
        const auto second = has_second ? static_cast<unsigned char>(value[index++]) : 0;
        const bool has_third = index < value.size();
        const auto third = has_third ? static_cast<unsigned char>(value[index++]) : 0;
        result.push_back(alphabet[first >> 2]);
        result.push_back(alphabet[((first & 0x03) << 4) | (second >> 4)]);
        result.push_back(has_second ? alphabet[((second & 0x0f) << 2) | (third >> 6)] : '=');
        result.push_back(has_third ? alphabet[third & 0x3f] : '=');
    }
    return result;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t");
    if (first == std::string::npos)
        return {};
    const auto last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

bool domain_matches(std::string_view host, std::string_view domain) {
    return host == domain || (host.size() > domain.size() && host.substr(host.size() - domain.size()) == domain &&
                              host[host.size() - domain.size() - 1] == '.');
}

bool path_matches(std::string_view path, std::string_view cookie_path) {
    if (cookie_path == "/")
        return true;
    if (path.size() < cookie_path.size() || path.substr(0, cookie_path.size()) != cookie_path)
        return false;
    return path.size() == cookie_path.size() || cookie_path.back() == '/' || path[cookie_path.size()] == '/';
}

std::string cookie_header(const std::shared_ptr<AndroidClientState> &state, std::string_view host,
                          std::string_view path, bool secure) {
    if (!state)
        return {};
    std::lock_guard lock(state->mutex);
    std::string result;
    for (const auto &cookie : state->cookies) {
        if (!domain_matches(host, cookie.domain) || !path_matches(path, cookie.path) || (cookie.secure && !secure))
            continue;
        if (!result.empty())
            result += "; ";
        result += cookie.name + "=" + cookie.value;
    }
    return result;
}

void store_cookies(const std::shared_ptr<AndroidClientState> &state,
                   const std::vector<nk::net::OwnedHeader> &headers, std::string_view host,
                   std::string_view path, bool secure) {
    if (!state)
        return;
    std::lock_guard lock(state->mutex);
    for (const auto &header : headers) {
        if (!lower_equal(header.name, "set-cookie"))
            continue;
        const auto separator = header.value.find(';');
        const auto pair = trim(header.value.substr(0, separator));
        const auto equals = pair.find('=');
        if (equals == std::string::npos || equals == 0)
            continue;
        auto name = trim(pair.substr(0, equals));
        auto value = pair.substr(equals + 1);
        if (name.empty() || name.find_first_of(" \t\r\n") != std::string::npos ||
            value.find_first_of("\r\n") != std::string::npos)
            continue;
        std::string domain(host);
        std::string cookie_path = path.empty() ? "/" : std::string(path);
        const auto last_slash = cookie_path.find_last_of('/');
        cookie_path = last_slash == std::string::npos || last_slash == 0 ? "/" : cookie_path.substr(0, last_slash);
        bool cookie_secure = false;
        bool delete_cookie = value.empty();
        std::size_t attribute_start = separator == std::string::npos ? header.value.size() : separator + 1;
        while (attribute_start < header.value.size()) {
            const auto attribute_end = header.value.find(';', attribute_start);
            const auto attribute = trim(header.value.substr(attribute_start, attribute_end - attribute_start));
            const auto attribute_equals = attribute.find('=');
            const auto attribute_name = trim(attribute.substr(0, attribute_equals));
            const auto attribute_value = attribute_equals == std::string::npos
                                             ? std::string{}
                                             : trim(attribute.substr(attribute_equals + 1));
            if (lower_equal(attribute_name, "domain") && !attribute_value.empty()) {
                domain = attribute_value;
                if (!domain.empty() && domain.front() == '.')
                    domain.erase(domain.begin());
                std::transform(domain.begin(), domain.end(), domain.begin(), [](unsigned char character) {
                    return static_cast<char>(std::tolower(character));
                });
                if (!domain_matches(host, domain))
                    domain.clear();
            } else if (lower_equal(attribute_name, "path") && !attribute_value.empty() &&
                       attribute_value.front() == '/') {
                cookie_path = attribute_value;
            } else if (lower_equal(attribute_name, "secure")) {
                cookie_secure = true;
            } else if (lower_equal(attribute_name, "max-age") && attribute_value == "0") {
                delete_cookie = true;
            }
            if (attribute_end == std::string::npos)
                break;
            attribute_start = attribute_end + 1;
        }
        if (domain.empty() || (cookie_secure && !secure))
            continue;
        const auto existing = std::find_if(state->cookies.begin(), state->cookies.end(), [&](const auto &cookie) {
            return cookie.name == name && cookie.domain == domain && cookie.path == cookie_path;
        });
        if (delete_cookie) {
            if (existing != state->cookies.end())
                state->cookies.erase(existing);
        } else if (existing != state->cookies.end()) {
            existing->value = std::move(value);
            existing->secure = cookie_secure;
        } else {
            state->cookies.push_back({std::move(name), std::move(value), std::move(domain), std::move(cookie_path),
                                      cookie_secure});
        }
    }
}

nk_result url_components(JNIEnv *env, const JniMethods &methods, jobject url, std::string &host,
                         std::string &path) {
    auto *java_host = static_cast<jstring>(env->CallObjectMethod(url, methods.url_host));
    auto *java_path = static_cast<jstring>(env->CallObjectMethod(url, methods.url_path));
    if (const auto error = java_exception(env); error != NK_OK) {
        if (java_host)
            env->DeleteLocalRef(java_host);
        if (java_path)
            env->DeleteLocalRef(java_path);
        return error;
    }
    host = native_string(env, java_host);
    path = native_string(env, java_path);
    if (path.empty())
        path = "/";
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (java_host)
        env->DeleteLocalRef(java_host);
    if (java_path)
        env->DeleteLocalRef(java_path);
    return NK_OK;
}

bool parse_content_length(std::string_view value, uint64_t &result) {
    if (value.empty())
        return false;
    uint64_t parsed = 0;
    for (const auto character : value) {
        if (character < '0' || character > '9' ||
            parsed > (UINT64_MAX - static_cast<uint64_t>(character - '0')) / 10)
            return false;
        parsed = parsed * 10 + static_cast<uint64_t>(character - '0');
    }
    result = parsed;
    return true;
}

nk_result response_headers(JNIEnv *env, const JniMethods &methods, jobject connection,
                           std::vector<nk::net::OwnedHeader> &headers, std::string &location,
                           uint64_t &content_length, uint64_t max_header_size) {
    auto *fields = env->CallObjectMethod(connection, methods.get_header_fields);
    if (const auto error = java_exception(env); error != NK_OK)
        return error;
    if (!fields)
        return NK_HTTP_ERROR_PROTOCOL;
    auto *entries = env->CallObjectMethod(fields, methods.map_entry_set);
    if (const auto error = java_exception(env); error != NK_OK) {
        env->DeleteLocalRef(fields);
        return error;
    }
    auto *iterator = env->CallObjectMethod(entries, methods.set_iterator);
    if (const auto error = java_exception(env); error != NK_OK) {
        env->DeleteLocalRef(entries);
        env->DeleteLocalRef(fields);
        return error;
    }
    while (env->CallBooleanMethod(iterator, methods.iterator_has_next) == JNI_TRUE) {
        if (const auto error = java_exception(env); error != NK_OK) {
            env->DeleteLocalRef(iterator);
            env->DeleteLocalRef(entries);
            env->DeleteLocalRef(fields);
            return error;
        }
        auto *map_entry = env->CallObjectMethod(iterator, methods.iterator_next);
        if (const auto error = java_exception(env); error != NK_OK) {
            env->DeleteLocalRef(iterator);
            env->DeleteLocalRef(entries);
            env->DeleteLocalRef(fields);
            return error;
        }
        auto *key = static_cast<jstring>(env->CallObjectMethod(map_entry, methods.entry_get_key));
        auto *values = env->CallObjectMethod(map_entry, methods.entry_get_value);
        if (const auto error = java_exception(env); error != NK_OK) {
            if (key)
                env->DeleteLocalRef(key);
            if (values)
                env->DeleteLocalRef(values);
            env->DeleteLocalRef(map_entry);
            env->DeleteLocalRef(iterator);
            env->DeleteLocalRef(entries);
            env->DeleteLocalRef(fields);
            return error;
        }
        if (key && values) {
            const auto name = native_string(env, key);
            const auto count = env->CallIntMethod(values, methods.list_size);
            if (const auto error = java_exception(env); error != NK_OK) {
                env->DeleteLocalRef(key);
                env->DeleteLocalRef(values);
                env->DeleteLocalRef(map_entry);
                env->DeleteLocalRef(iterator);
                env->DeleteLocalRef(entries);
                env->DeleteLocalRef(fields);
                return error;
            }
            for (jint index = 0; index < count; ++index) {
                auto *value = static_cast<jstring>(env->CallObjectMethod(values, methods.list_get, index));
                if (const auto error = java_exception(env); error != NK_OK) {
                    env->DeleteLocalRef(key);
                    env->DeleteLocalRef(values);
                    env->DeleteLocalRef(map_entry);
                    env->DeleteLocalRef(iterator);
                    env->DeleteLocalRef(entries);
                    env->DeleteLocalRef(fields);
                    return error;
                }
                if (value) {
                    auto text = native_string(env, value);
                    uint64_t header_bytes = 0;
                    for (const auto &header : headers)
                        header_bytes += header.name.size() + header.value.size();
                    const auto next_header_bytes = static_cast<uint64_t>(name.size()) + text.size();
                    if (headers.size() >= 1024 || header_bytes > max_header_size ||
                        next_header_bytes > max_header_size - header_bytes) {
                        env->DeleteLocalRef(value);
                        env->DeleteLocalRef(key);
                        env->DeleteLocalRef(values);
                        env->DeleteLocalRef(map_entry);
                        env->DeleteLocalRef(iterator);
                        env->DeleteLocalRef(entries);
                        env->DeleteLocalRef(fields);
                        return NK_HTTP_ERROR_RESPONSE_LIMIT;
                    }
                    headers.push_back({name, text});
                    if (lower_equal(name, "location"))
                        location = text;
                    if (lower_equal(name, "content-length"))
                        parse_content_length(text, content_length);
                    env->DeleteLocalRef(value);
                }
            }
        }
        if (key)
            env->DeleteLocalRef(key);
        if (values)
            env->DeleteLocalRef(values);
        env->DeleteLocalRef(map_entry);
    }
    const auto error = java_exception(env);
    env->DeleteLocalRef(iterator);
    env->DeleteLocalRef(entries);
    env->DeleteLocalRef(fields);
    return error;
}

jobject create_proxy(JNIEnv *env, const JniMethods &methods, const nk::net::ProxyConfig &config,
                     nk_result &error) {
    auto *specification = java_string(env, config.url);
    if (!specification) {
        error = NK_HTTP_ERROR_PROXY;
        return nullptr;
    }
    auto *parsed = env->NewObject(methods.uri, methods.uri_constructor, specification);
    env->DeleteLocalRef(specification);
    if ((error = java_exception(env)) != NK_OK || !parsed)
        return nullptr;
    auto *host = static_cast<jstring>(env->CallObjectMethod(parsed, methods.uri_host));
    const auto port = env->CallIntMethod(parsed, methods.uri_port);
    if ((error = java_exception(env)) != NK_OK || !host || port <= 0 || port > 65535) {
        if (host)
            env->DeleteLocalRef(host);
        env->DeleteLocalRef(parsed);
        if (error == NK_OK)
            error = NK_HTTP_ERROR_PROXY;
        return nullptr;
    }
    auto *address = env->CallStaticObjectMethod(methods.inet_socket_address,
                                                methods.inet_create_unresolved, host, port);
    auto *type = env->GetStaticObjectField(methods.proxy_type,
                                           config.kind == NK_HTTP_PROXY_SOCKS5 ? methods.proxy_type_socks
                                                                                 : methods.proxy_http);
    auto *result = address && type ? env->NewObject(methods.proxy, methods.proxy_constructor, type, address) : nullptr;
    error = java_exception(env);
    if (!result && error == NK_OK)
        error = NK_HTTP_ERROR_PROXY;
    if (type)
        env->DeleteLocalRef(type);
    if (address)
        env->DeleteLocalRef(address);
    env->DeleteLocalRef(host);
    env->DeleteLocalRef(parsed);
    return result;
}

nk_result set_request_headers(JNIEnv *env, const JniMethods &methods, jobject connection,
                              const nk::net::RequestPtr &request,
                              const std::shared_ptr<AndroidClientState> &state, std::string_view host,
                              std::string_view path, bool secure) {
    std::vector<nk::net::OwnedHeader> headers = request->client->config.default_headers;
    headers.insert(headers.end(), request->request.headers.begin(), request->request.headers.end());
    const auto cookies = cookie_header(state, host, path, secure);
    if (!cookies.empty() && !has_header(headers, "cookie"))
        headers.push_back({"Cookie", cookies});
    if (request->client->config.proxy.kind != NK_HTTP_PROXY_SOCKS5 &&
        !request->client->config.proxy.username.empty() &&
        !has_header(headers, "proxy-authorization")) {
        const auto credentials = request->client->config.proxy.username + ":" +
                                 request->client->config.proxy.password;
        headers.push_back({"Proxy-Authorization", "Basic " + base64(credentials)});
    }
    for (const auto &header : headers) {
        auto *name = java_string(env, header.name);
        auto *value = java_string(env, header.value);
        if (!name || !value) {
            if (name)
                env->DeleteLocalRef(name);
            if (value)
                env->DeleteLocalRef(value);
            return NK_HTTP_ERROR_PROTOCOL;
        }
        env->CallVoidMethod(connection, methods.set_request_property, name, value);
        env->DeleteLocalRef(name);
        env->DeleteLocalRef(value);
        if (const auto error = java_exception(env); error != NK_OK)
            return error;
    }
    return NK_OK;
}

nk_result upload_body(JNIEnv *env, const JniMethods &methods, jobject connection,
                      const nk::net::RequestPtr &request) {
    auto *output = env->CallObjectMethod(connection, methods.get_output_stream);
    if (const auto error = java_exception(env); error != NK_OK)
        return error;
    if (!output)
        return NK_HTTP_ERROR_PROTOCOL;
    std::vector<std::byte> buffer(io_buffer_size);
    auto *bytes = env->NewByteArray(static_cast<jsize>(buffer.size()));
    if (const auto error = java_exception(env); error != NK_OK || !bytes) {
        env->DeleteLocalRef(output);
        return error == NK_OK ? NK_ERROR_OUT_OF_MEMORY : error;
    }
    auto write = [&](const std::byte *data, std::size_t size) -> nk_result {
        if (size > static_cast<std::size_t>(std::numeric_limits<jsize>::max()))
            return NK_ERROR_INVALID_ARGUMENT;
        env->SetByteArrayRegion(bytes, 0, static_cast<jsize>(size), reinterpret_cast<const jbyte *>(data));
        if (const auto error = java_exception(env); error != NK_OK)
            return error;
        env->CallVoidMethod(output, methods.output_write, bytes, 0, static_cast<jint>(size));
        return java_exception(env);
    };
    nk_result result = NK_OK;
    if (!request->request.body.empty()) {
        result = write(request->request.body.data(), request->request.body.size());
        if (result == NK_OK)
            nk::net::emit_progress(request, 0, request->total, request->request.body.size(),
                                   request->request.body.size());
    } else if (request->request.upload_stream != NK_INVALID_HANDLE) {
        uint64_t uploaded = 0;
        while (request->request.upload_size == 0 || uploaded < request->request.upload_size) {
            const auto capacity = request->request.upload_size == 0
                                      ? buffer.size()
                                      : std::min<uint64_t>(buffer.size(), request->request.upload_size - uploaded);
            uint64_t read = 0;
            result = nk_resource_read(request->request.upload_stream, buffer.data(), capacity, &read);
            if (result != NK_OK)
                break;
            if (read == 0) {
                if (request->request.upload_size != 0 && uploaded != request->request.upload_size)
                    result = NK_HTTP_ERROR_PROTOCOL;
                break;
            }
            result = write(buffer.data(), static_cast<std::size_t>(read));
            if (result != NK_OK)
                break;
            uploaded += read;
            nk::net::emit_progress(request, 0, request->total, uploaded, request->request.upload_size);
        }
    }
    env->CallVoidMethod(output, methods.output_close);
    if (result == NK_OK)
        result = java_exception(env);
    else
        env->ExceptionClear();
    env->DeleteLocalRef(bytes);
    env->DeleteLocalRef(output);
    return result;
}

nk_result perform(const nk::net::RequestPtr &request) {
    if (request->client->config.tls.flags != 0 || request->client->config.tls.minimum_version != NK_HTTP_TLS_DEFAULT ||
        !request->client->config.tls.ca_bundle_path.empty())
        return NK_ERROR_UNSUPPORTED;
    if (request->request.timeout_ms > static_cast<uint64_t>(std::numeric_limits<jint>::max()))
        return NK_ERROR_INVALID_ARGUMENT;

    bool attached = false;
    auto *env = nk::backend::android_jni_attach(&attached);
    if (!env)
        return NK_ERROR_UNSUPPORTED;
    struct Detach final {
        bool attached;
        ~Detach() { nk::backend::android_jni_detach(attached); }
    } detach{attached};

    JniMethods methods;
    if (!methods.initialize(env)) {
        java_exception(env);
        return NK_ERROR_UNSUPPORTED;
    }
    const auto *method = method_name(request->request.method);
    if (!method)
        return NK_HTTP_ERROR_PROTOCOL;
    auto state = client_state(request);
    std::string current_url = request->request.url;
    auto *url_text = java_string(env, current_url);
    if (!url_text)
        return NK_HTTP_ERROR_PROTOCOL;
    auto *url = env->NewObject(methods.url, methods.url_constructor, url_text);
    env->DeleteLocalRef(url_text);
    if (const auto error = java_exception(env); error != NK_OK || !url)
        return error == NK_OK ? NK_HTTP_ERROR_PROTOCOL : error;
    nk_result result = NK_OK;
    uint32_t redirects = 0;
    for (;;) {
        nk_result proxy_error = NK_OK;
        jobject proxy = nullptr;
        if (request->client->config.proxy.kind != NK_HTTP_PROXY_NONE) {
            proxy = create_proxy(env, methods, request->client->config.proxy, proxy_error);
            if (!proxy) {
                result = proxy_error;
                break;
            }
        }
        auto *connection = proxy
                               ? env->CallObjectMethod(url, methods.url_open_connection_proxy, proxy)
                               : env->CallObjectMethod(url, methods.url_open_connection);
        if (proxy)
            env->DeleteLocalRef(proxy);
        if ((result = java_exception(env)) != NK_OK || !connection) {
            if (connection)
                env->DeleteLocalRef(connection);
            if (result == NK_OK)
                result = NK_HTTP_ERROR_CONNECTION;
            break;
        }
        if (!register_connection(env, request->id, connection)) {
            result = java_exception(env);
            if (result == NK_OK)
                result = NK_ERROR_OUT_OF_MEMORY;
            env->DeleteLocalRef(connection);
            break;
        }
        ActiveConnection active_connection{env, request->id};
        env->CallVoidMethod(connection, methods.set_follow_redirects, JNI_FALSE);
        env->CallVoidMethod(connection, methods.set_connect_timeout,
                            static_cast<jint>(request->request.timeout_ms));
        env->CallVoidMethod(connection, methods.set_read_timeout,
                            static_cast<jint>(request->request.timeout_ms));
        env->CallVoidMethod(connection, methods.set_use_caches, JNI_FALSE);
        auto *java_method = java_string(env, method);
        if (!java_method) {
            env->DeleteLocalRef(connection);
            result = NK_HTTP_ERROR_PROTOCOL;
            break;
        }
        env->CallVoidMethod(connection, methods.set_request_method, java_method);
        env->DeleteLocalRef(java_method);
        if ((result = java_exception(env)) != NK_OK) {
            env->DeleteLocalRef(connection);
            break;
        }
        std::string host;
        std::string path;
        result = url_components(env, methods, url, host, path);
        if (result != NK_OK) {
            env->DeleteLocalRef(connection);
            break;
        }
        result = set_request_headers(env, methods, connection, request, state, host, path,
                                     https_url(current_url));
        if (result != NK_OK) {
            env->DeleteLocalRef(connection);
            break;
        }
        if (!request->request.body.empty() || request->request.upload_stream != NK_INVALID_HANDLE) {
            env->CallVoidMethod(connection, methods.set_do_output, JNI_TRUE);
            if ((result = java_exception(env)) == NK_OK)
                result = upload_body(env, methods, connection, request);
            if (result != NK_OK) {
                env->DeleteLocalRef(connection);
                break;
            }
        }
        const auto status = env->CallIntMethod(connection, methods.get_response_code);
        if ((result = java_exception(env)) != NK_OK) {
            env->DeleteLocalRef(connection);
            break;
        }
        std::vector<nk::net::OwnedHeader> headers;
        std::string location;
        uint64_t content_length = NK_HTTP_CONTENT_LENGTH_UNKNOWN;
        result = response_headers(env, methods, connection, headers, location, content_length,
                                  request->client->config.max_header_size);
        if (result != NK_OK) {
            env->DeleteLocalRef(connection);
            break;
        }
        store_cookies(state, headers, host, path, https_url(current_url));
        const auto status_code = status >= 100 && status <= 599 ? static_cast<uint32_t>(status) : 0u;
        const bool redirect = status >= 300 && status <= 399 && !location.empty();
        if (redirect && redirects < request->request.redirect_limit) {
            auto *location_text = java_string(env, location);
            auto *next_url = location_text
                                 ? env->NewObject(methods.url, methods.url_relative_constructor, url, location_text)
                                 : nullptr;
            if (location_text)
                env->DeleteLocalRef(location_text);
            if ((result = java_exception(env)) != NK_OK || !next_url) {
                env->DeleteLocalRef(connection);
                if (next_url)
                    env->DeleteLocalRef(next_url);
                if (result == NK_OK)
                    result = NK_HTTP_ERROR_REDIRECT;
                break;
            }
            auto *next_text = static_cast<jstring>(env->CallObjectMethod(next_url, methods.url_to_string));
            if ((result = java_exception(env)) != NK_OK || !next_text) {
                env->DeleteLocalRef(next_url);
                env->DeleteLocalRef(connection);
                if (result == NK_OK)
                    result = NK_HTTP_ERROR_REDIRECT;
                break;
            }
            const auto next_url_text = native_string(env, next_text);
            const bool insecure_redirect = https_url(current_url) && http_url(next_url_text) &&
                                           !(request->client->config.flags & NK_HTTP_CLIENT_ALLOW_HTTPS_TO_HTTP);
            env->DeleteLocalRef(next_text);
            if (insecure_redirect || (!https_url(next_url_text) && !http_url(next_url_text))) {
                result = nk::net::receive_response_headers(request, status_code, std::move(headers), content_length);
                if (result == NK_OK)
                    result = NK_HTTP_ERROR_REDIRECT;
                env->DeleteLocalRef(next_url);
                env->DeleteLocalRef(connection);
                break;
            }
            env->DeleteLocalRef(url);
            url = next_url;
            current_url = next_url_text;
            ++redirects;
            env->CallVoidMethod(connection, methods.disconnect);
            env->ExceptionClear();
            env->DeleteLocalRef(connection);
            continue;
        }
        if (redirect) {
            result = nk::net::receive_response_headers(request, status_code, std::move(headers), content_length);
            if (result == NK_OK)
                result = NK_HTTP_ERROR_REDIRECT;
            env->DeleteLocalRef(connection);
            break;
        }
        if (redirects != 0) {
            std::lock_guard lock(request->mutex);
            request->response_flags |= NK_HTTP_RESPONSE_REDIRECTED;
        }
        result = nk::net::receive_response_headers(request, status_code, std::move(headers), content_length);
        if (result != NK_OK) {
            env->DeleteLocalRef(connection);
            break;
        }
        auto *input = status >= 400 ? env->CallObjectMethod(connection, methods.get_error_stream) : nullptr;
        if ((result = java_exception(env)) != NK_OK) {
            env->DeleteLocalRef(connection);
            break;
        }
        if (!input)
            input = env->CallObjectMethod(connection, methods.get_input_stream);
        if ((result = java_exception(env)) != NK_OK) {
            env->DeleteLocalRef(connection);
            break;
        }
        if (input) {
            std::vector<std::byte> buffer(io_buffer_size);
            auto *bytes = env->NewByteArray(static_cast<jsize>(buffer.size()));
            if ((result = java_exception(env)) != NK_OK || !bytes) {
                env->DeleteLocalRef(input);
                env->DeleteLocalRef(connection);
                if (result == NK_OK)
                    result = NK_ERROR_OUT_OF_MEMORY;
                break;
            }
            uint64_t downloaded = 0;
            for (;;) {
                if (request->canceled.load(std::memory_order_acquire)) {
                    result = NK_HTTP_ERROR_CANCELED;
                    break;
                }
                const auto read = env->CallIntMethod(input, methods.input_read, bytes, 0,
                                                     static_cast<jint>(buffer.size()));
                if ((result = java_exception(env)) != NK_OK)
                    break;
                if (read < 0)
                    break;
                if (read == 0)
                    continue;
                env->GetByteArrayRegion(bytes, 0, read, reinterpret_cast<jbyte *>(buffer.data()));
                if ((result = java_exception(env)) != NK_OK)
                    break;
                result = nk::net::receive_response_data(request, buffer.data(), static_cast<std::size_t>(read));
                if (result != NK_OK)
                    break;
                downloaded += static_cast<uint64_t>(read);
                nk::net::emit_progress(request, downloaded, content_length, 0, 0);
            }
            env->CallVoidMethod(input, methods.input_close);
            if (result == NK_OK)
                result = java_exception(env);
            else
                env->ExceptionClear();
            env->DeleteLocalRef(bytes);
            env->DeleteLocalRef(input);
        }
        env->CallVoidMethod(connection, methods.disconnect);
        env->ExceptionClear();
        env->DeleteLocalRef(connection);
        break;
    }
    env->DeleteLocalRef(url);
    return result;
}

void perform_request(nk::net::RequestPtr request) noexcept {
    nk_result result = NK_HTTP_ERROR_CONNECTION;
    try {
        result = perform(request);
    } catch (const std::bad_alloc &) {
        result = NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        result = NK_ERROR_UNKNOWN;
    }
    nk::net::complete_request(request, result);
    nk::net::worker_finished(request);
    request.reset();
}

} // namespace

namespace nk::net {

nk_capabilities capabilities() noexcept {
    bool attached = false;
    auto *env = nk::backend::android_jni_attach(&attached);
    if (!env)
        return 0;
    nk::backend::android_jni_detach(attached);
    return NK_CAP_HTTP_CLIENT | NK_CAP_HTTP_STREAMING;
}

nk_result backend_start(const RequestPtr &request) noexcept {
    bool attached = false;
    auto *env = nk::backend::android_jni_attach(&attached);
    if (!env)
        return NK_ERROR_UNSUPPORTED;
    nk::backend::android_jni_detach(attached);
    try {
        std::thread([request] { perform_request(request); }).detach();
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return NK_ERROR_UNKNOWN;
    }
}

void backend_cancel(const RequestPtr &request) noexcept {
    if (!request)
        return;
    std::lock_guard lock(active_connections_mutex);
    const auto found = active_connections.find(request->id);
    if (found == active_connections.end())
        return;
    bool attached = false;
    auto *env = nk::backend::android_jni_attach(&attached);
    if (!env)
        return;
    auto *connection_class = env->GetObjectClass(found->second);
    auto disconnect = connection_class ? env->GetMethodID(connection_class, "disconnect", "()V") : nullptr;
    if (disconnect)
        env->CallVoidMethod(found->second, disconnect);
    env->ExceptionClear();
    if (connection_class)
        env->DeleteLocalRef(connection_class);
    nk::backend::android_jni_detach(attached);
}

void backend_shutdown() noexcept {}

} // namespace nk::net
