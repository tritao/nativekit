#pragma once

#include "core/handle_registry.hpp"
#include "nativekit_net.h"
#include "nativekit_resource.h"
#include "nativekit_window.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace nk::net {

struct OwnedHeader {
    std::string name;
    std::string value;
};

struct ProxyConfig {
    nk_http_proxy_kind kind = NK_HTTP_PROXY_NONE;
    std::string url;
    std::string username;
    std::string password;
};

struct TlsConfig {
    uint32_t flags = 0;
    nk_http_tls_version minimum_version = NK_HTTP_TLS_DEFAULT;
    std::string ca_bundle_path;
};

struct ClientConfig {
    uint32_t flags = 0;
    uint32_t redirect_limit = 10;
    uint32_t timeout_ms = 30000;
    uint64_t max_response_size = 16u * 1024u * 1024u;
    uint64_t max_header_size = 64u * 1024u;
    uint64_t stream_buffer_size = 256u * 1024u;
    nk_http_cookie_policy cookie_policy = NK_HTTP_COOKIES_DISABLED;
    nk_http_cache_policy cache_policy = NK_HTTP_CACHE_DISABLED;
    ProxyConfig proxy;
    TlsConfig tls;
    std::vector<OwnedHeader> default_headers;
};

struct RequestConfig {
    nk_http_method method = NK_HTTP_METHOD_GET;
    std::string url;
    std::vector<OwnedHeader> headers;
    std::vector<std::byte> body;
    nk_resource_stream upload_stream = NK_INVALID_HANDLE;
    uint64_t upload_size = 0;
    nk_http_request_mode mode = NK_HTTP_REQUEST_BUFFERED;
    uint32_t redirect_limit = 10;
    uint32_t timeout_ms = 30000;
    uint64_t max_response_size = 16u * 1024u * 1024u;
    uint64_t stream_buffer_size = 256u * 1024u;
};

struct HttpClientResource final : nk::core::Resource {
    ClientConfig config;
    std::mutex backend_mutex;
    std::shared_ptr<void> backend_state;
    std::mutex requests_mutex;
    std::unordered_set<nk_request_id> requests;
};

struct RequestContext final : std::enable_shared_from_this<RequestContext> {
    nk_request_id id = NK_INVALID_REQUEST_ID;
    uint64_t generation = 0;
    std::shared_ptr<HttpClientResource> client;
    RequestConfig request;

    std::atomic<bool> canceled{false};
    std::mutex mutex;
    std::condition_variable condition;
    std::deque<std::vector<std::byte>> chunks;
    uint64_t available = 0;
    uint64_t received = 0;
    uint64_t total = NK_HTTP_CONTENT_LENGTH_UNKNOWN;
    bool stream_complete = false;
    bool stream_closed = false;
    nk_result stream_result = NK_OK;
    bool data_event_pending = false;

    uint32_t status_code = 0;
    uint32_t response_flags = 0;
    uint64_t content_length = NK_HTTP_CONTENT_LENGTH_UNKNOWN;
    std::vector<OwnedHeader> response_headers;
    std::vector<std::byte> response_body;
    uint64_t response_header_bytes = 0;
    bool response_limit = false;
    bool headers_emitted = false;
    nk_http_stream stream = NK_INVALID_HANDLE;
    uint64_t upload_position = 0;
    nk_result upload_result = NK_OK;
    std::chrono::steady_clock::time_point last_progress{};
    bool headers_event_pending = false;
};

using RequestPtr = std::shared_ptr<RequestContext>;

/* Common lifecycle and event helpers used by each private transport. */
nk_result backend_start(const RequestPtr &request) noexcept;
void backend_shutdown() noexcept;
nk_capabilities capabilities() noexcept;
void shutdown() noexcept;

void emit_headers(const RequestPtr &request) noexcept;
void emit_data_available(const RequestPtr &request) noexcept;
void emit_progress(const RequestPtr &request, uint64_t downloaded, uint64_t download_total,
                   uint64_t uploaded, uint64_t upload_total) noexcept;
nk_result receive_response_headers(const RequestPtr &request, uint32_t status,
                                   std::vector<OwnedHeader> headers,
                                   uint64_t content_length) noexcept;
nk_result receive_response_data(const RequestPtr &request, const std::byte *data,
                                std::size_t size) noexcept;
void set_response_total(const RequestPtr &request, uint64_t total) noexcept;
void complete_request(const RequestPtr &request, nk_result result) noexcept;
void backend_cancel(const RequestPtr &request) noexcept;
void worker_finished(const RequestPtr &request) noexcept;

} // namespace nk::net
