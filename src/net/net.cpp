#include "nativekit_net.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"
#include "net_backend.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using nk::net::HttpClientResource;
using nk::net::OwnedHeader;
using nk::net::RequestConfig;
using nk::net::RequestContext;
using nk::net::RequestPtr;

constexpr std::size_t max_url_size = 8192;
constexpr std::size_t max_header_count = 1024;
constexpr std::size_t max_header_value_size = 64u * 1024u;
constexpr uint64_t default_max_response_size = 16u * 1024u * 1024u;
constexpr uint64_t default_max_header_size = 64u * 1024u;
constexpr uint64_t default_stream_buffer_size = 256u * 1024u;
constexpr uint32_t default_redirect_limit = 10;
constexpr uint32_t default_timeout_ms = 30000;
constexpr uint32_t known_client_flags =
    NK_HTTP_CLIENT_ALLOW_HTTP | NK_HTTP_CLIENT_ALLOW_HTTPS_TO_HTTP;
constexpr uint32_t known_tls_flags =
    NK_HTTP_TLS_DISABLE_PEER_VERIFICATION | NK_HTTP_TLS_DISABLE_HOSTNAME_VERIFICATION;

std::mutex request_mutex;
std::condition_variable worker_condition;
std::unordered_map<nk_request_id, RequestPtr> requests;
std::size_t active_workers = 0;
bool shutting_down = false;

struct RequestRegistrationGuard {
    RequestPtr request;
    std::shared_ptr<HttpClientResource> client;
    bool active = true;

    ~RequestRegistrationGuard() { rollback(); }

    void commit() noexcept { active = false; }

    void rollback() noexcept {
        if (!active || !request)
            return;
        {
            std::lock_guard lock(request_mutex);
            const auto found = requests.find(request->id);
            if (found != requests.end() && found->second == request) {
                requests.erase(found);
                if (active_workers != 0)
                    --active_workers;
                worker_condition.notify_all();
            }
        }
        if (client) {
            std::lock_guard lock(client->requests_mutex);
            client->requests.erase(request->id);
        }
        if (request->stream)
            nk::core::handles().erase(request->stream, nk::core::ResourceType::http_stream);
        active = false;
    }
};

struct HttpStreamResource final : nk::core::Resource {
    RequestPtr request;
};

struct WireResponse {
    uint32_t magic;
    uint32_t version;
    uint32_t status_code;
    uint32_t flags;
    uint32_t header_count;
    uint32_t reserved0;
    uint64_t content_length;
    uint32_t stream;
    uint32_t reserved1;
    uint64_t headers_offset;
    uint64_t headers_size;
    uint64_t body_offset;
    uint64_t body_size;
    uint64_t reserved[2];
};

struct WireHeader {
    uint64_t name_offset;
    uint64_t name_size;
    uint64_t value_offset;
    uint64_t value_size;
};

constexpr uint32_t wire_magic = 0x4e4b4852; // "NKHR"
constexpr uint32_t wire_version = 1;

nk_result fail(nk_result result, const char *message) {
    nk::core::set_error(message);
    return result;
}

bool valid_struct(uint32_t actual, std::size_t expected) {
    return actual >= expected;
}

bool valid_nested_struct(uint32_t actual, std::size_t expected) {
    return actual == 0 || actual >= expected;
}

bool bounded_c_string(const char *value, std::size_t limit, std::string &out,
                      bool nullable = false) {
    if (!value) {
        if (nullable) {
            out.clear();
            return true;
        }
        return false;
    }
    std::size_t length = 0;
    while (length < limit && value[length] != '\0')
        ++length;
    if (length == limit)
        return false;
    out.assign(value, length);
    return true;
}

bool valid_header_text(std::string_view value, bool name) {
    if (value.empty() && name)
        return false;
    if (value.size() > max_header_value_size)
        return false;
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (name) {
            const bool token = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                               (byte >= '0' && byte <= '9') || byte == '!' || byte == '#' ||
                               byte == '$' || byte == '%' || byte == '&' || byte == '\'' ||
                               byte == '*' || byte == '+' || byte == '-' || byte == '.' ||
                               byte == '^' || byte == '_' || byte == '`' || byte == '|' ||
                               byte == '~';
            if (!token)
                return false;
        } else if (byte == '\r' || byte == '\n' || byte == 0x7f || (byte < 0x20 && byte != '\t')) {
            return false;
        }
    }
    return true;
}

bool copy_headers(const nk_http_header *input, uint32_t count, std::vector<OwnedHeader> &output) {
    if (count > max_header_count || (count != 0 && !input))
        return false;
    output.clear();
    output.reserve(count);
    for (uint32_t index = 0; index < count; ++index) {
        const auto &header = input[index];
        if (!header.name || !header.value || header.name_size == 0 ||
            header.name_size > max_header_value_size || header.value_size > max_header_value_size)
            return false;
        std::string name(header.name, header.name_size);
        std::string value(header.value, header.value_size);
        if (!valid_header_text(name, true) || !valid_header_text(value, false))
            return false;
        output.push_back({std::move(name), std::move(value)});
    }
    return true;
}

bool valid_method(nk_http_method method) {
    return method >= NK_HTTP_METHOD_GET && method <= NK_HTTP_METHOD_CONNECT;
}

bool url_scheme(const std::string &url, bool &https) {
    auto separator = url.find("://");
    if (separator == std::string::npos)
        return false;
    std::string scheme = url.substr(0, separator);
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (scheme == "https") {
        https = true;
        return true;
    }
    if (scheme == "http") {
        https = false;
        return true;
    }
    return false;
}

bool copy_proxy(const nk_http_proxy_options &input, nk::net::ProxyConfig &output) {
    if (!valid_nested_struct(input.struct_size, sizeof(input)) || input.kind > NK_HTTP_PROXY_SOCKS5)
        return false;
    output = {};
    output.kind = input.kind;
    if (input.kind == NK_HTTP_PROXY_NONE)
        return !input.url && !input.username && !input.password;
    return bounded_c_string(input.url, max_url_size, output.url) && !output.url.empty() &&
           bounded_c_string(input.username, max_header_value_size, output.username, true) &&
           bounded_c_string(input.password, max_header_value_size, output.password, true);
}

bool copy_tls(const nk_http_tls_options &input, nk::net::TlsConfig &output) {
    if (!valid_nested_struct(input.struct_size, sizeof(input)) ||
        input.minimum_version > NK_HTTP_TLS_1_3)
        return false;
    output = {};
    output.flags = input.flags;
    output.minimum_version = input.minimum_version;
    return bounded_c_string(input.ca_bundle_path, max_url_size, output.ca_bundle_path, true);
}

bool copy_client_options(const nk_http_client_options &input, nk::net::ClientConfig &output,
                         nk_result &error) {
    if (!valid_struct(input.struct_size, sizeof(input)) ||
        (input.flags & ~known_client_flags) != 0 ||
        (input.proxy && !valid_struct(input.proxy->struct_size, sizeof(*input.proxy))) ||
        (input.tls && !valid_struct(input.tls->struct_size, sizeof(*input.tls)))) {
        error = NK_ERROR_INVALID_ARGUMENT;
        return false;
    }
    if ((input.proxy && !copy_proxy(*input.proxy, output.proxy)) ||
        (input.tls && !copy_tls(*input.tls, output.tls)) ||
        !copy_headers(input.default_headers, input.default_header_count, output.default_headers)) {
        error = NK_ERROR_INVALID_ARGUMENT;
        return false;
    }
    if (input.cookie_policy > NK_HTTP_COOKIES_PERSISTENT ||
        input.cache_policy > NK_HTTP_CACHE_PERSISTENT) {
        error = NK_ERROR_INVALID_ARGUMENT;
        return false;
    }
    if (input.cookie_policy == NK_HTTP_COOKIES_PERSISTENT ||
        input.cache_policy != NK_HTTP_CACHE_DISABLED) {
        error = NK_ERROR_UNSUPPORTED;
        return false;
    }
    output.flags = input.flags;
    output.redirect_limit =
        input.redirect_limit == 0 ? default_redirect_limit : input.redirect_limit;
    output.timeout_ms = input.timeout_ms == 0 ? default_timeout_ms : input.timeout_ms;
    output.max_response_size =
        input.max_response_size == 0 ? default_max_response_size : input.max_response_size;
    output.max_header_size =
        input.max_header_size == 0 ? default_max_header_size : input.max_header_size;
    output.stream_buffer_size =
        input.stream_buffer_size == 0 ? default_stream_buffer_size : input.stream_buffer_size;
    output.cookie_policy = input.cookie_policy;
    output.cache_policy = input.cache_policy;
    if ((input.tls && (input.tls->flags & ~known_tls_flags) != 0) ||
        output.redirect_limit > static_cast<uint64_t>(std::numeric_limits<long>::max()) ||
        output.timeout_ms > static_cast<uint64_t>(std::numeric_limits<long>::max()) ||
        output.max_response_size > std::numeric_limits<std::size_t>::max() ||
        output.max_header_size > std::numeric_limits<std::size_t>::max() ||
        output.stream_buffer_size == 0)
        return error = NK_ERROR_INVALID_ARGUMENT, false;
    error = NK_OK;
    return true;
}

bool copy_request_options(const nk_http_request_options &input, const HttpClientResource &client,
                          RequestConfig &output, nk_result &error) {
    if (!valid_struct(input.struct_size, sizeof(input)) || !valid_method(input.method) ||
        !input.url || input.mode > NK_HTTP_REQUEST_STREAMING ||
        (input.upload_size != 0 && input.upload_stream == NK_INVALID_HANDLE) ||
        (input.body_size != 0 && !input.body) ||
        (input.body && input.body_size > std::numeric_limits<std::size_t>::max()) ||
        (input.body && input.upload_stream != NK_INVALID_HANDLE) ||
        (input.upload_stream != NK_INVALID_HANDLE && input.body_size != 0)) {
        error = NK_ERROR_INVALID_ARGUMENT;
        return false;
    }
    output = {};
    output.method = input.method;
    output.mode = input.mode;
    output.upload_stream = input.upload_stream;
    output.upload_size = input.upload_size;
    if (!bounded_c_string(input.url, max_url_size, output.url)) {
        error = NK_ERROR_INVALID_ARGUMENT;
        return false;
    }
    bool https = false;
    if (!url_scheme(output.url, https) ||
        (!https && !(client.config.flags & NK_HTTP_CLIENT_ALLOW_HTTP))) {
        error = NK_ERROR_INVALID_ARGUMENT;
        return false;
    }
    if (!copy_headers(input.headers, input.header_count, output.headers)) {
        error = NK_ERROR_INVALID_ARGUMENT;
        return false;
    }
    if (input.body_size != 0) {
        const auto *begin = static_cast<const std::byte *>(input.body);
        output.body.assign(begin, begin + static_cast<std::size_t>(input.body_size));
    }
    output.redirect_limit =
        input.redirect_limit == 0 ? client.config.redirect_limit : input.redirect_limit;
    output.timeout_ms = input.timeout_ms == 0 ? client.config.timeout_ms : input.timeout_ms;
    output.max_response_size =
        input.max_response_size == 0 ? client.config.max_response_size : input.max_response_size;
    output.stream_buffer_size =
        input.stream_buffer_size == 0 ? client.config.stream_buffer_size : input.stream_buffer_size;
    if (output.max_response_size == 0 || output.stream_buffer_size == 0 ||
        output.timeout_ms > static_cast<uint64_t>(std::numeric_limits<long>::max()) ||
        output.redirect_limit > static_cast<uint64_t>(std::numeric_limits<long>::max()) ||
        output.max_response_size > std::numeric_limits<std::size_t>::max() ||
        output.stream_buffer_size > std::numeric_limits<std::size_t>::max()) {
        error = NK_ERROR_INVALID_ARGUMENT;
        return false;
    }
    if (output.upload_stream != NK_INVALID_HANDLE) {
        nk_resource_stream_info info{};
        info.struct_size = sizeof(info);
        if (nk_resource_stream_info_get(output.upload_stream, &info) != NK_OK ||
            !(info.flags & NK_RESOURCE_STREAM_READABLE)) {
            error = NK_ERROR_INVALID_HANDLE;
            return false;
        }
        if (output.upload_size == 0 && (info.flags & NK_RESOURCE_STREAM_SIZE_KNOWN))
            output.upload_size = info.size;
    }
    error = NK_OK;
    return true;
}

std::shared_ptr<HttpClientResource> client_resource(nk_http_client handle) {
    return std::dynamic_pointer_cast<HttpClientResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::http_client));
}

std::shared_ptr<HttpStreamResource> stream_resource(nk_http_stream handle) {
    return std::dynamic_pointer_cast<HttpStreamResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::http_stream));
}

RequestPtr request_resource(nk_request_id request) {
    std::lock_guard lock(request_mutex);
    const auto found = requests.find(request);
    return found == requests.end() ? RequestPtr{} : found->second;
}

void cancel_request(const RequestPtr &request) {
    if (!request)
        return;
    request->canceled.store(true, std::memory_order_release);
    request->condition.notify_all();
    nk::net::backend_cancel(request);
}

bool checked_range(uint64_t offset, uint64_t size, uint64_t total) {
    return offset <= total && size <= total - offset;
}

bool response_payload(const RequestPtr &request, bool include_body, std::vector<std::byte> &data) {
    std::lock_guard lock(request->mutex);
    const auto header_bytes =
        static_cast<uint64_t>(request->response_headers.size()) * sizeof(WireHeader);
    uint64_t string_bytes = 0;
    for (const auto &header : request->response_headers) {
        if (header.name.size() > UINT32_MAX || header.value.size() > UINT32_MAX)
            return false;
        if (string_bytes >
            std::numeric_limits<uint64_t>::max() - header.name.size() - header.value.size())
            return false;
        string_bytes += header.name.size() + header.value.size();
    }
    const auto body_bytes = include_body ? request->response_body.size() : 0;
    if (header_bytes > std::numeric_limits<uint64_t>::max() - string_bytes)
        return false;
    const uint64_t headers_size = header_bytes + string_bytes;
    if (sizeof(WireResponse) > std::numeric_limits<uint64_t>::max() - headers_size)
        return false;
    const uint64_t body_offset = sizeof(WireResponse) + headers_size;
    if (body_bytes > std::numeric_limits<uint64_t>::max() - body_offset)
        return false;
    const uint64_t total_size = body_offset + body_bytes;
    if (headers_size > std::numeric_limits<std::size_t>::max() ||
        total_size > std::numeric_limits<std::size_t>::max())
        return false;

    data.resize(static_cast<std::size_t>(total_size));
    WireResponse wire{};
    wire.magic = wire_magic;
    wire.version = wire_version;
    wire.status_code = request->status_code;
    wire.flags = request->response_flags;
    wire.header_count = static_cast<uint32_t>(request->response_headers.size());
    wire.content_length = request->content_length;
    wire.stream = request->stream;
    wire.headers_offset = sizeof(WireResponse);
    wire.headers_size = headers_size;
    wire.body_offset = body_bytes == 0 ? 0 : body_offset;
    wire.body_size = body_bytes;
    std::memcpy(data.data(), &wire, sizeof(wire));

    auto *headers = data.data() + sizeof(WireResponse);
    uint64_t string_offset = header_bytes;
    for (std::size_t index = 0; index < request->response_headers.size(); ++index) {
        const auto &header = request->response_headers[index];
        WireHeader wire_header{};
        wire_header.name_offset = string_offset;
        wire_header.name_size = header.name.size();
        std::memcpy(headers + string_offset, header.name.data(), header.name.size());
        string_offset += wire_header.name_size;
        wire_header.value_offset = string_offset;
        wire_header.value_size = header.value.size();
        std::memcpy(headers + string_offset, header.value.data(), header.value.size());
        string_offset += wire_header.value_size;
        std::memcpy(headers + index * sizeof(WireHeader), &wire_header, sizeof(wire_header));
    }
    if (body_bytes != 0)
        std::memcpy(data.data() + body_offset, request->response_body.data(), body_bytes);
    return true;
}

void finish_event(const RequestPtr &request, nk_result result) noexcept {
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_HTTP_COMPLETE;
    event.source = request->stream;
    event.request_id = request->id;
    event.result = result;
    if (!response_payload(request, request->request.mode == NK_HTTP_REQUEST_BUFFERED, event.data))
        return;
    nk::core::push_event(std::move(event));
}

} // namespace

namespace nk::net {

void emit_headers(const RequestPtr &request) noexcept {
    {
        std::lock_guard lock(request->mutex);
        if (request->headers_emitted || request->headers_event_pending || request->status_code == 0)
            return;
        request->headers_event_pending = true;
    }
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_HTTP_HEADERS;
    event.source = request->stream;
    event.request_id = request->id;
    if (!response_payload(request, false, event.data)) {
        std::lock_guard lock(request->mutex);
        request->headers_event_pending = false;
        return;
    }
    const auto result = nk::core::push_event(std::move(event));
    std::lock_guard lock(request->mutex);
    request->headers_event_pending = false;
    if (result == NK_OK)
        request->headers_emitted = true;
}

void emit_data_available(const RequestPtr &request) noexcept {
    {
        std::lock_guard lock(request->mutex);
        if (request->data_event_pending || request->available == 0 || request->stream_closed)
            return;
        request->data_event_pending = true;
    }
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_HTTP_DATA_AVAILABLE;
    event.source = request->stream;
    event.request_id = request->id;
    const auto result = nk::core::push_event(std::move(event));
    if (result != NK_OK) {
        std::lock_guard lock(request->mutex);
        request->data_event_pending = false;
    }
}

void emit_progress(const RequestPtr &request, uint64_t downloaded, uint64_t download_total,
                   uint64_t uploaded, uint64_t upload_total) noexcept {
    nk_http_progress progress{};
    progress.struct_size = sizeof(progress);
    progress.downloaded = downloaded;
    progress.download_total = download_total;
    progress.uploaded = uploaded;
    progress.upload_total = upload_total;
    const auto *begin = reinterpret_cast<const std::byte *>(&progress);
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_HTTP_PROGRESS;
    event.source = request->stream;
    event.request_id = request->id;
    event.data.assign(begin, begin + sizeof(progress));
    nk::core::push_event(std::move(event));
}

nk_result receive_response_headers(const RequestPtr &request, uint32_t status,
                                   std::vector<OwnedHeader> headers,
                                   uint64_t content_length) noexcept {
    if (status < 100 || status > 599)
        return NK_HTTP_ERROR_PROTOCOL;
    uint64_t header_bytes = 0;
    if (headers.size() > max_header_count)
        return NK_HTTP_ERROR_RESPONSE_LIMIT;
    for (const auto &header : headers) {
        if (!valid_header_text(header.name, true) || !valid_header_text(header.value, false) ||
            header_bytes >
                std::numeric_limits<uint64_t>::max() - header.name.size() - header.value.size())
            return NK_HTTP_ERROR_RESPONSE_LIMIT;
        header_bytes += header.name.size() + header.value.size();
    }
    if (header_bytes > request->client->config.max_header_size)
        return NK_HTTP_ERROR_RESPONSE_LIMIT;
    {
        std::lock_guard lock(request->mutex);
        if (request->canceled.load(std::memory_order_acquire))
            return NK_HTTP_ERROR_CANCELED;
        if (request->response_limit)
            return NK_HTTP_ERROR_RESPONSE_LIMIT;
        request->status_code = status;
        request->response_headers = std::move(headers);
        request->response_header_bytes = header_bytes;
        request->content_length = content_length;
        request->total = content_length;
    }
    emit_headers(request);
    return NK_OK;
}

nk_result receive_response_data(const RequestPtr &request, const std::byte *data,
                                std::size_t size) noexcept {
    if (!data && size != 0)
        return NK_HTTP_ERROR_PROTOCOL;
    if (request->canceled.load(std::memory_order_acquire))
        return NK_HTTP_ERROR_CANCELED;
    if (size == 0)
        return NK_OK;
    emit_headers(request);
    std::unique_lock lock(request->mutex);
    if (request->response_limit)
        return NK_HTTP_ERROR_RESPONSE_LIMIT;
    if (request->request.mode == NK_HTTP_REQUEST_BUFFERED) {
        if (request->response_body.size() > request->request.max_response_size ||
            size > request->request.max_response_size - request->response_body.size()) {
            request->response_limit = true;
            return NK_HTTP_ERROR_RESPONSE_LIMIT;
        }
        request->response_body.insert(request->response_body.end(), data, data + size);
        request->received += size;
        return NK_OK;
    }

    std::size_t offset = 0;
    while (offset < size) {
        if (std::chrono::steady_clock::now() >= request->deadline) {
            request->timed_out.store(true, std::memory_order_release);
            return NK_HTTP_ERROR_TIMEOUT;
        }
        if (!request->condition.wait_until(lock, request->deadline, [&] {
                return request->canceled.load(std::memory_order_acquire) ||
                       request->stream_closed ||
                       request->available < request->request.stream_buffer_size;
            })) {
            request->timed_out.store(true, std::memory_order_release);
            return NK_HTTP_ERROR_TIMEOUT;
        }
        if (request->canceled.load(std::memory_order_acquire))
            return NK_HTTP_ERROR_CANCELED;
        if (request->stream_closed)
            return NK_HTTP_ERROR_CANCELED;
        if (std::chrono::steady_clock::now() >= request->deadline) {
            request->timed_out.store(true, std::memory_order_release);
            return NK_HTTP_ERROR_TIMEOUT;
        }
        const auto remaining_capacity = request->request.stream_buffer_size - request->available;
        const auto amount = std::min<std::size_t>(size - offset, remaining_capacity);
        if (request->received > request->request.max_response_size ||
            amount > request->request.max_response_size - request->received) {
            request->response_limit = true;
            return NK_HTTP_ERROR_RESPONSE_LIMIT;
        }
        request->chunks.emplace_back(data + offset, data + offset + amount);
        request->available += amount;
        request->received += amount;
        offset += amount;
        lock.unlock();
        emit_data_available(request);
        lock.lock();
    }
    return NK_OK;
}

void set_response_total(const RequestPtr &request, uint64_t total) noexcept {
    std::lock_guard lock(request->mutex);
    request->content_length = total;
    request->total = total;
}

void complete_request(const RequestPtr &request, nk_result result) noexcept {
    bool emit = false;
    {
        std::lock_guard lock(request->mutex);
        if (request->stream_complete)
            return;
        if (request->canceled.load(std::memory_order_acquire))
            result = NK_HTTP_ERROR_CANCELED;
        request->stream_complete = true;
        request->stream_result = result;
        if (request->received != 0)
            request->response_flags |= NK_HTTP_RESPONSE_HAS_BODY;
        emit = true;
    }
    request->condition.notify_all();
    if (!emit)
        return;
    emit_headers(request);
    finish_event(request, result);
}

void worker_finished(const RequestPtr &request) noexcept {
    std::lock_guard lock(request_mutex);
    requests.erase(request->id);
    if (request->client) {
        std::lock_guard client_lock(request->client->requests_mutex);
        request->client->requests.erase(request->id);
    }
    if (active_workers != 0)
        --active_workers;
    worker_condition.notify_all();
}

void shutdown() noexcept {
    std::vector<RequestPtr> pending;
    {
        std::lock_guard lock(request_mutex);
        shutting_down = true;
        pending.reserve(requests.size());
        for (const auto &[id, request] : requests) {
            (void)id;
            pending.push_back(request);
        }
    }
    for (const auto &request : pending)
        cancel_request(request);
    {
        std::unique_lock lock(request_mutex);
        worker_condition.wait(lock, [] { return active_workers == 0; });
        shutting_down = false;
    }
}

} // namespace nk::net

extern "C" {

nk_result NK_CALL nk_http_client_create(const nk_http_client_options *options,
                                        nk_http_client *out_client) {
    return nk::core::result_boundary(
        "unexpected error while creating HTTP client", [&]() -> nk_result {
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (!options || !out_client) {
                return fail(NK_ERROR_INVALID_ARGUMENT, "HTTP client options or output is null");
            }
            *out_client = NK_INVALID_HANDLE;
            nk::net::ClientConfig config;
            nk_result error = NK_OK;
            if (!copy_client_options(*options, config, error))
                return fail(error, "HTTP client options are invalid or unsupported");
            if (!(nk::net::capabilities() & NK_CAP_HTTP_CLIENT))
                return fail(NK_ERROR_UNSUPPORTED, "HTTP networking is unavailable on this backend");
            auto client = std::make_shared<HttpClientResource>();
            client->config = std::move(config);
            const auto handle =
                nk::core::handles().insert(nk::core::ResourceType::http_client, client);
            if (!handle)
                return fail(NK_ERROR_OUT_OF_MEMORY, "could not allocate HTTP client handle");
            *out_client = handle;
            return NK_OK;
        });
}

nk_result NK_CALL nk_http_client_destroy(nk_http_client handle) {
    return nk::core::result_boundary(
        "unexpected error while destroying HTTP client", [&]() -> nk_result {
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            auto client = client_resource(handle);
            if (!client)
                return fail(NK_ERROR_INVALID_HANDLE, "invalid HTTP client handle");
            std::vector<nk_request_id> ids;
            {
                std::lock_guard lock(client->requests_mutex);
                ids.assign(client->requests.begin(), client->requests.end());
            }
            for (const auto id : ids)
                cancel_request(request_resource(id));
            if (!nk::core::handles().erase(handle, nk::core::ResourceType::http_client))
                return fail(NK_ERROR_INVALID_HANDLE, "invalid HTTP client handle");
            return NK_OK;
        });
}

nk_result NK_CALL nk_http_request(nk_http_client client_handle,
                                  const nk_http_request_options *options,
                                  nk_request_id *out_request, nk_http_stream *out_stream) {
    return nk::core::result_boundary(
        "unexpected error while starting HTTP request", [&]() -> nk_result {
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (!options || !out_request)
                return fail(NK_ERROR_INVALID_ARGUMENT, "HTTP request options or output is null");
            *out_request = NK_INVALID_REQUEST_ID;
            if (out_stream)
                *out_stream = NK_INVALID_HANDLE;
            auto client = client_resource(client_handle);
            if (!client)
                return fail(NK_ERROR_INVALID_HANDLE, "invalid HTTP client handle");
            if (!(nk::net::capabilities() & NK_CAP_HTTP_CLIENT))
                return fail(NK_ERROR_UNSUPPORTED, "HTTP networking is unavailable on this backend");

            auto request = std::make_shared<RequestContext>();
            request->client = client;
            nk_result error = NK_OK;
            if (!copy_request_options(*options, *client, request->request, error))
                return fail(error, "HTTP request options are invalid");
            request->deadline = std::chrono::steady_clock::now() +
                                std::chrono::milliseconds(request->request.timeout_ms);
            if (request->request.mode == NK_HTTP_REQUEST_STREAMING &&
                !(nk::net::capabilities() & NK_CAP_HTTP_STREAMING))
                return fail(NK_ERROR_UNSUPPORTED, "streaming HTTP is unavailable on this backend");

            request->id = nk::core::next_request_id();
            request->generation = nk::core::runtime_generation();
            if (request->id == NK_INVALID_REQUEST_ID || request->generation == 0)
                return fail(NK_ERROR_INVALID_REQUEST, "could not allocate an HTTP request ID");

            if (request->request.mode == NK_HTTP_REQUEST_STREAMING) {
                auto stream = std::make_shared<HttpStreamResource>();
                stream->request = request;
                const auto stream_handle =
                    nk::core::handles().insert(nk::core::ResourceType::http_stream, stream);
                if (!stream_handle)
                    return fail(NK_ERROR_OUT_OF_MEMORY, "could not allocate HTTP stream handle");
                request->stream = stream_handle;
            }

            RequestRegistrationGuard registration{request, client};
            {
                std::lock_guard lock(request_mutex);
                if (shutting_down)
                    return fail(NK_ERROR_INVALID_REQUEST, "NativeKit is shutting down");
                const auto [_, inserted] = requests.emplace(request->id, request);
                if (!inserted)
                    return fail(NK_ERROR_INVALID_REQUEST, "HTTP request ID is already active");
                ++active_workers;
            }
            {
                std::lock_guard lock(client->requests_mutex);
                client->requests.insert(request->id);
            }
            const auto start_result = nk::net::backend_start(request);
            if (start_result != NK_OK) {
                return fail(start_result, "could not start HTTP request");
            }
            registration.commit();
            *out_request = request->id;
            if (out_stream)
                *out_stream = request->stream;
            return NK_OK;
        });
}

nk_result NK_CALL nk_http_cancel(nk_request_id request_id) {
    if (request_id == NK_INVALID_REQUEST_ID)
        return fail(NK_ERROR_INVALID_ARGUMENT, "HTTP request ID is invalid");
    const auto request = request_resource(request_id);
    if (!request)
        return fail(NK_ERROR_INVALID_REQUEST, "HTTP request is no longer active");
    cancel_request(request);
    return NK_OK;
}

nk_result NK_CALL nk_http_request_stream(nk_request_id request_id, nk_http_stream *out_stream) {
    if (!out_stream)
        return fail(NK_ERROR_INVALID_ARGUMENT, "HTTP stream output is null");
    *out_stream = NK_INVALID_HANDLE;
    const auto request = request_resource(request_id);
    if (!request)
        return fail(NK_ERROR_INVALID_REQUEST, "HTTP request is no longer active");
    if (request->request.mode != NK_HTTP_REQUEST_STREAMING)
        return fail(NK_ERROR_INVALID_ARGUMENT, "HTTP request is not streaming");
    *out_stream = request->stream;
    return NK_OK;
}

nk_result NK_CALL nk_http_stream_info_get(nk_http_stream handle, nk_http_stream_info *out_info) {
    if (!out_info || !valid_struct(out_info->struct_size, sizeof(*out_info)))
        return fail(NK_ERROR_INVALID_ARGUMENT, "HTTP stream info output is invalid");
    auto stream = stream_resource(handle);
    if (!stream)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid HTTP stream handle");
    std::lock_guard lock(stream->request->mutex);
    const auto struct_size = out_info->struct_size;
    *out_info = {};
    out_info->struct_size = struct_size;
    out_info->flags = 0;
    if (stream->request->available != 0)
        out_info->flags |= NK_HTTP_STREAM_DATA_AVAILABLE;
    if (stream->request->stream_complete)
        out_info->flags |= NK_HTTP_STREAM_COMPLETE;
    if (stream->request->stream_closed)
        out_info->flags |= NK_HTTP_STREAM_CLOSED;
    if (stream->request->stream_result != NK_OK)
        out_info->flags |= NK_HTTP_STREAM_FAILED;
    out_info->available = stream->request->available;
    out_info->received = stream->request->received;
    out_info->total = stream->request->total;
    out_info->result = stream->request->stream_result;
    return NK_OK;
}

nk_result NK_CALL nk_http_stream_read(nk_http_stream handle, void *buffer, uint64_t size,
                                      uint64_t *out_read) {
    if ((!buffer && size != 0) || !out_read)
        return fail(NK_ERROR_INVALID_ARGUMENT, "HTTP stream read arguments are invalid");
    *out_read = 0;
    auto stream = stream_resource(handle);
    if (!stream)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid HTTP stream handle");
    std::lock_guard lock(stream->request->mutex);
    if (size == 0)
        return NK_OK;
    if (stream->request->available == 0) {
        if (!stream->request->stream_complete)
            return NK_HTTP_ERROR_WOULD_BLOCK;
        return NK_OK;
    }
    auto *destination = static_cast<std::byte *>(buffer);
    while (size != 0 && !stream->request->chunks.empty()) {
        auto &chunk = stream->request->chunks.front();
        const auto amount = std::min<uint64_t>(size, chunk.size());
        std::memcpy(destination, chunk.data(), static_cast<std::size_t>(amount));
        destination += amount;
        size -= amount;
        *out_read += amount;
        stream->request->available -= amount;
        if (amount == chunk.size())
            stream->request->chunks.pop_front();
        else
            chunk.erase(chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(amount));
    }
    if (stream->request->available == 0)
        stream->request->data_event_pending = false;
    stream->request->condition.notify_all();
    return NK_OK;
}

nk_result NK_CALL nk_http_stream_close(nk_http_stream handle) {
    auto stream = stream_resource(handle);
    if (!stream)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid HTTP stream handle");
    {
        std::lock_guard lock(stream->request->mutex);
        stream->request->stream_closed = true;
    }
    cancel_request(stream->request);
    if (!nk::core::handles().erase(handle, nk::core::ResourceType::http_stream))
        return fail(NK_ERROR_INVALID_HANDLE, "invalid HTTP stream handle");
    return NK_OK;
}

nk_result NK_CALL nk_http_event_response(const nk_event *event, nk_http_response *out_response) {
    if (!event || !out_response ||
        !valid_struct(out_response->struct_size, sizeof(*out_response)) ||
        (event->kind != NK_EVENT_HTTP_HEADERS && event->kind != NK_EVENT_HTTP_COMPLETE) ||
        !event->data || event->data_size < sizeof(WireResponse))
        return fail(NK_ERROR_INVALID_ARGUMENT, "HTTP response event is invalid");
    WireResponse wire{};
    std::memcpy(&wire, event->data, sizeof(wire));
    if (wire.magic != wire_magic || wire.version != wire_version ||
        wire.header_count > max_header_count ||
        !checked_range(wire.headers_offset, wire.headers_size, event->data_size) ||
        wire.headers_size < static_cast<uint64_t>(wire.header_count) * sizeof(WireHeader) ||
        wire.headers_offset < sizeof(WireResponse) ||
        (wire.body_size != 0 &&
         (wire.body_offset < wire.headers_offset + wire.headers_size ||
          !checked_range(wire.body_offset, wire.body_size, event->data_size))))
        return fail(NK_ERROR_INVALID_ARGUMENT, "HTTP response event payload is malformed");
    *out_response = {};
    out_response->struct_size = sizeof(*out_response);
    out_response->status_code = wire.status_code;
    out_response->flags = wire.flags;
    out_response->header_count = wire.header_count;
    out_response->content_length = wire.content_length;
    out_response->stream = wire.stream;
    out_response->headers = static_cast<const std::byte *>(event->data) + wire.headers_offset;
    out_response->headers_size = wire.headers_size;
    if (wire.body_size != 0)
        out_response->body = static_cast<const std::byte *>(event->data) + wire.body_offset;
    out_response->body_size = wire.body_size;
    return NK_OK;
}

nk_result NK_CALL nk_http_event_progress(const nk_event *event, nk_http_progress *out_progress) {
    if (!event || !out_progress ||
        !valid_struct(out_progress->struct_size, sizeof(*out_progress)) ||
        event->kind != NK_EVENT_HTTP_PROGRESS || event->data_size != sizeof(nk_http_progress) ||
        !event->data)
        return fail(NK_ERROR_INVALID_ARGUMENT, "HTTP progress event is invalid");
    std::memcpy(out_progress, event->data, sizeof(*out_progress));
    if (out_progress->struct_size < sizeof(*out_progress))
        return fail(NK_ERROR_INVALID_ARGUMENT, "HTTP progress payload is malformed");
    out_progress->struct_size = sizeof(*out_progress);
    return NK_OK;
}

nk_result NK_CALL nk_http_response_header(const nk_http_response *response, uint32_t index,
                                          nk_http_header *out_header) {
    if (!response || !out_header || !valid_struct(response->struct_size, sizeof(*response)) ||
        index >= response->header_count || !response->headers ||
        response->headers_size < static_cast<uint64_t>(response->header_count) * sizeof(WireHeader))
        return fail(NK_ERROR_INVALID_ARGUMENT, "HTTP response header arguments are invalid");
    WireHeader wire{};
    const auto *headers = static_cast<const std::byte *>(response->headers);
    std::memcpy(&wire, headers + static_cast<std::size_t>(index) * sizeof(WireHeader),
                sizeof(wire));
    if (!checked_range(wire.name_offset, wire.name_size, response->headers_size) ||
        !checked_range(wire.value_offset, wire.value_size, response->headers_size) ||
        wire.name_offset < static_cast<uint64_t>(response->header_count) * sizeof(WireHeader) ||
        wire.value_offset < static_cast<uint64_t>(response->header_count) * sizeof(WireHeader) ||
        wire.name_size > UINT32_MAX || wire.value_size > UINT32_MAX)
        return fail(NK_ERROR_INVALID_ARGUMENT, "HTTP response header payload is malformed");
    *out_header = {};
    out_header->name = reinterpret_cast<const char *>(headers + wire.name_offset);
    out_header->name_size = wire.name_size;
    out_header->value = reinterpret_cast<const char *>(headers + wire.value_offset);
    out_header->value_size = wire.value_size;
    return NK_OK;
}

} // extern "C"
