#include "net_backend.hpp"
#include "net/curl_progress.hpp"

#include "core/error.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>

namespace {

std::mutex curl_mutex;
bool curl_initialized = false;
std::mutex curl_share_data_mutex;

struct CurlClientState {
    CURLSH *share = nullptr;

    ~CurlClientState() {
        if (share)
            curl_share_cleanup(share);
    }
};

void curl_share_lock(CURL *, curl_lock_data, curl_lock_access, void *) {
    curl_share_data_mutex.lock();
}

void curl_share_unlock(CURL *, curl_lock_data, void *) {
    curl_share_data_mutex.unlock();
}

bool initialize_curl() {
    if (curl_initialized)
        return true;
    curl_initialized = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    return curl_initialized;
}

std::shared_ptr<CurlClientState> client_state(const nk::net::RequestPtr &request) {
    std::lock_guard lock(request->client->backend_mutex);
    if (request->client->backend_state)
        return std::static_pointer_cast<CurlClientState>(request->client->backend_state);
    auto state = std::make_shared<CurlClientState>();
    state->share = curl_share_init();
    if (!state->share ||
        curl_share_setopt(state->share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE) != CURLSHE_OK ||
        curl_share_setopt(state->share, CURLSHOPT_LOCKFUNC, curl_share_lock) != CURLSHE_OK ||
        curl_share_setopt(state->share, CURLSHOPT_UNLOCKFUNC, curl_share_unlock) != CURLSHE_OK) {
        return {};
    }
    request->client->backend_state = state;
    return state;
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

bool starts_with(std::string_view value, std::string_view prefix) {
    return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
}

uint32_t parse_status(std::string_view line) {
    const auto first_space = line.find(' ');
    if (first_space == std::string_view::npos)
        return 0;
    std::size_t index = first_space + 1;
    uint32_t result = 0;
    for (int digits = 0; digits < 3 && index < line.size(); ++digits, ++index) {
        if (line[index] < '0' || line[index] > '9')
            return 0;
        result = result * 10 + static_cast<uint32_t>(line[index] - '0');
    }
    return result >= 100 && result <= 599 ? result : 0;
}

std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1);
    return value;
}

void clear_response_headers(nk::net::RequestContext &request, uint32_t status) {
    request.status_code = status;
    request.response_headers.clear();
    request.response_header_bytes = 0;
    request.content_length = NK_HTTP_CONTENT_LENGTH_UNKNOWN;
}

std::size_t header_callback_impl(char *data, std::size_t size, std::size_t count, void *user_data) {
    auto &request = *static_cast<nk::net::RequestContext *>(user_data);
    const auto length = size * count;
    const std::string_view line(data, length);
    std::lock_guard lock(request.mutex);
    if (request.canceled.load(std::memory_order_acquire) || request.response_limit)
        return 0;
    if (starts_with(line, "HTTP/") || starts_with(line, "http/")) {
        clear_response_headers(request, parse_status(line));
        return length;
    }
    if (line == "\r\n" || line == "\n")
        return length;
    const auto separator = line.find(':');
    if (separator == std::string_view::npos)
        return length; // Ignore informational text that is not a header field.
    const auto name = trim(line.substr(0, separator));
    const auto value = trim(line.substr(separator + 1));
    request.response_header_bytes += name.size() + value.size();
    if (request.response_header_bytes > request.client->config.max_header_size ||
        request.response_headers.size() >= 1024 || name.empty()) {
        request.response_limit = true;
        return 0;
    }
    request.response_headers.push_back({std::string(name), std::string(value)});
    bool content_length = name.size() == 14;
    if (content_length) {
        for (std::size_t index = 0; index < name.size(); ++index) {
            if (std::tolower(static_cast<unsigned char>(name[index])) !=
                std::tolower(static_cast<unsigned char>("Content-Length"[index]))) {
                content_length = false;
                break;
            }
        }
    }
    if (content_length) {
        uint64_t parsed = 0;
        for (const auto character : value) {
            if (character < '0' || character > '9' ||
                parsed > (UINT64_MAX - static_cast<uint64_t>(character - '0')) / 10) {
                parsed = UINT64_MAX;
                break;
            }
            parsed = parsed * 10 + static_cast<uint64_t>(character - '0');
        }
        request.content_length = parsed;
    }
    return length;
}

std::size_t header_callback(char *data, std::size_t size, std::size_t count,
                            void *user_data) noexcept {
    return header_callback_impl(data, size, count, user_data);
}

std::size_t write_callback_impl(char *data, std::size_t size, std::size_t count, void *user_data) {
    auto &request = *static_cast<nk::net::RequestContext *>(user_data);
    const auto length = size * count;
    if (request.canceled.load(std::memory_order_acquire))
        return 0;
    nk::net::emit_headers(request.shared_from_this());

    std::unique_lock lock(request.mutex);
    if (request.response_limit)
        return 0;
    if (request.request.mode == NK_HTTP_REQUEST_BUFFERED) {
        if (request.response_body.size() > request.request.max_response_size ||
            length > request.request.max_response_size - request.response_body.size()) {
            request.response_limit = true;
            return 0;
        }
        const auto *begin = reinterpret_cast<const std::byte *>(data);
        request.response_body.insert(request.response_body.end(), begin, begin + length);
        request.received += length;
        return length;
    }

    std::size_t offset = 0;
    while (offset < length) {
        request.condition.wait(lock, [&] {
            return request.canceled.load(std::memory_order_acquire) || request.stream_closed ||
                   request.available < request.request.stream_buffer_size;
        });
        if (request.canceled.load(std::memory_order_acquire) || request.stream_closed)
            return 0;
        const auto remaining_capacity = request.request.stream_buffer_size - request.available;
        const auto amount = std::min<std::size_t>(length - offset, remaining_capacity);
        if (request.received > request.request.max_response_size ||
            amount > request.request.max_response_size - request.received) {
            request.response_limit = true;
            return 0;
        }
        const auto *begin = reinterpret_cast<const std::byte *>(data) + offset;
        request.chunks.emplace_back(begin, begin + amount);
        request.available += amount;
        request.received += amount;
        offset += amount;
        lock.unlock();
        nk::net::emit_data_available(request.shared_from_this());
        lock.lock();
    }
    return length;
}

std::size_t write_callback(char *data, std::size_t size, std::size_t count,
                           void *user_data) noexcept {
    return write_callback_impl(data, size, count, user_data);
}

std::size_t read_callback_impl(char *data, std::size_t size, std::size_t count, void *user_data) {
    auto &request = *static_cast<nk::net::RequestContext *>(user_data);
    const auto capacity = size * count;
    if (request.canceled.load(std::memory_order_acquire))
        return CURL_READFUNC_ABORT;
    uint64_t amount = 0;
    const auto result = nk_resource_read(request.request.upload_stream, data, capacity, &amount);
    if (result != NK_OK) {
        request.upload_result = result;
        return CURL_READFUNC_ABORT;
    }
    request.upload_position += amount;
    return static_cast<std::size_t>(amount);
}

std::size_t read_callback(char *data, std::size_t size, std::size_t count,
                          void *user_data) noexcept {
    return read_callback_impl(data, size, count, user_data);
}

int progress_callback_impl(void *user_data, curl_off_t download_total, curl_off_t downloaded,
                           curl_off_t upload_total, curl_off_t uploaded) {
    auto &request = *static_cast<nk::net::RequestContext *>(user_data);
    if (request.canceled.load(std::memory_order_acquire))
        return 1;
    const auto now = std::chrono::steady_clock::now();
    if (request.last_progress.time_since_epoch().count() != 0 &&
        now - request.last_progress < std::chrono::milliseconds(50))
        return 0;
    request.last_progress = now;
    const auto progress =
        nk::net::map_curl_progress(download_total, downloaded, upload_total, uploaded);
    nk::net::emit_progress(request.shared_from_this(), progress.downloaded, progress.download_total,
                           progress.uploaded, progress.upload_total);
    return 0;
}

int progress_callback(void *user_data, curl_off_t download_total, curl_off_t downloaded,
                      curl_off_t upload_total, curl_off_t uploaded) noexcept {
    return progress_callback_impl(user_data, download_total, downloaded, upload_total, uploaded);
}

nk_result map_curl_error(CURLcode code, const nk::net::RequestContext &request) {
    if (request.canceled.load(std::memory_order_acquire))
        return NK_HTTP_ERROR_CANCELED;
    if (request.response_limit)
        return NK_HTTP_ERROR_RESPONSE_LIMIT;
    if (request.upload_result != NK_OK)
        return NK_HTTP_ERROR_PROTOCOL;
    switch (code) {
    case CURLE_COULDNT_RESOLVE_HOST:
        return NK_HTTP_ERROR_DNS;
    case CURLE_COULDNT_RESOLVE_PROXY:
        return NK_HTTP_ERROR_PROXY;
    case CURLE_PROXY:
        return NK_HTTP_ERROR_PROXY;
    case CURLE_COULDNT_CONNECT:
    case CURLE_INTERFACE_FAILED:
    case CURLE_NO_CONNECTION_AVAILABLE:
        return NK_HTTP_ERROR_CONNECTION;
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_SSL_CERTPROBLEM:
    case CURLE_SSL_CIPHER:
    case CURLE_SSL_ISSUER_ERROR:
    case CURLE_PEER_FAILED_VERIFICATION:
    case CURLE_USE_SSL_FAILED:
        return NK_HTTP_ERROR_TLS;
    case CURLE_OPERATION_TIMEDOUT:
        return NK_HTTP_ERROR_TIMEOUT;
    case CURLE_TOO_MANY_REDIRECTS:
        return NK_HTTP_ERROR_REDIRECT;
    case CURLE_HTTP_RETURNED_ERROR:
    case CURLE_HTTP2:
    case CURLE_HTTP3:
    case CURLE_PARTIAL_FILE:
    case CURLE_RECV_ERROR:
    case CURLE_SEND_ERROR:
        return NK_HTTP_ERROR_PROTOCOL;
    case CURLE_OK:
        return NK_OK;
    default:
        return NK_HTTP_ERROR_CONNECTION;
    }
}

nk_result perform_impl(nk::net::RequestPtr request) {
    nk_result result = NK_HTTP_ERROR_CONNECTION;
    CURL *curl = curl_easy_init();
    curl_slist *header_list = nullptr;
    std::shared_ptr<CurlClientState> state;
    if (curl) {
        auto add_headers = [&](const std::vector<nk::net::OwnedHeader> &headers) {
            for (const auto &header : headers) {
                std::string line = header.name + ": " + header.value;
                auto *next = curl_slist_append(header_list, line.c_str());
                if (!next)
                    return false;
                header_list = next;
            }
            return true;
        };
        const auto *method = method_name(request->request.method);
        const bool configured = method && add_headers(request->client->config.default_headers) &&
                                add_headers(request->request.headers);
        if (!configured) {
            if (method)
                result = NK_ERROR_OUT_OF_MEMORY;
            else
                result = static_cast<nk_result>(NK_HTTP_ERROR_PROTOCOL);
        } else {
            auto set_option = [&](CURLoption option, auto value) {
                return curl_easy_setopt(curl, option, value) == CURLE_OK;
            };
            bool options_ok =
                set_option(CURLOPT_URL, request->request.url.c_str()) &&
                set_option(CURLOPT_CUSTOMREQUEST, method) &&
                set_option(CURLOPT_FOLLOWLOCATION, 1L) &&
                set_option(CURLOPT_MAXREDIRS, static_cast<long>(request->request.redirect_limit)) &&
                set_option(CURLOPT_TIMEOUT_MS, static_cast<long>(request->request.timeout_ms)) &&
                set_option(CURLOPT_CONNECTTIMEOUT_MS,
                           static_cast<long>(request->request.timeout_ms)) &&
                set_option(CURLOPT_NOSIGNAL, 1L) && set_option(CURLOPT_FAILONERROR, 0L) &&
                set_option(CURLOPT_PROTOCOLS_STR,
                           (request->client->config.flags & NK_HTTP_CLIENT_ALLOW_HTTP)
                               ? "http,https"
                               : "https") &&
                set_option(CURLOPT_REDIR_PROTOCOLS_STR,
                           (request->client->config.flags & NK_HTTP_CLIENT_ALLOW_HTTPS_TO_HTTP)
                               ? "http,https"
                               : "https");
            if (header_list)
                options_ok = set_option(CURLOPT_HTTPHEADER, header_list) && options_ok;
            options_ok = set_option(CURLOPT_WRITEFUNCTION, write_callback) && options_ok;
            options_ok = set_option(CURLOPT_WRITEDATA, request.get()) && options_ok;
            options_ok = set_option(CURLOPT_HEADERFUNCTION, header_callback) && options_ok;
            options_ok = set_option(CURLOPT_HEADERDATA, request.get()) && options_ok;
            options_ok = set_option(CURLOPT_XFERINFOFUNCTION, progress_callback) && options_ok;
            options_ok = set_option(CURLOPT_XFERINFODATA, request.get()) && options_ok;
            options_ok = set_option(CURLOPT_NOPROGRESS, 0L) && options_ok;

            const auto &tls = request->client->config.tls;
            bool unsupported_security_policy = false;
            options_ok =
                set_option(CURLOPT_SSL_VERIFYPEER,
                           (tls.flags & NK_HTTP_TLS_DISABLE_PEER_VERIFICATION) ? 0L : 1L) &&
                options_ok;
            options_ok =
                set_option(CURLOPT_SSL_VERIFYHOST,
                           (tls.flags & NK_HTTP_TLS_DISABLE_HOSTNAME_VERIFICATION) ? 0L : 2L) &&
                options_ok;
            if (!tls.ca_bundle_path.empty())
                options_ok = set_option(CURLOPT_CAINFO, tls.ca_bundle_path.c_str()) && options_ok;
            if (tls.minimum_version == NK_HTTP_TLS_1_2)
                options_ok = set_option(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2) && options_ok;
#if LIBCURL_VERSION_NUM >= 0x073400
            if (tls.minimum_version == NK_HTTP_TLS_1_3)
                options_ok = set_option(CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_3) && options_ok;
#else
            if (tls.minimum_version == NK_HTTP_TLS_1_3) {
                unsupported_security_policy = true;
                options_ok = false;
            }
#endif

            const auto &proxy = request->client->config.proxy;
            if (proxy.kind != NK_HTTP_PROXY_NONE) {
                options_ok = set_option(CURLOPT_PROXY, proxy.url.c_str()) && options_ok;
                options_ok =
                    set_option(CURLOPT_PROXYTYPE, proxy.kind == NK_HTTP_PROXY_HTTP ? CURLPROXY_HTTP
                                                  : proxy.kind == NK_HTTP_PROXY_HTTPS
                                                      ? CURLPROXY_HTTPS
                                                      : CURLPROXY_SOCKS5_HOSTNAME) &&
                    options_ok;
                if (!proxy.username.empty())
                    options_ok =
                        set_option(CURLOPT_PROXYUSERNAME, proxy.username.c_str()) && options_ok;
                if (!proxy.password.empty())
                    options_ok =
                        set_option(CURLOPT_PROXYPASSWORD, proxy.password.c_str()) && options_ok;
            }
            if (request->client->config.cookie_policy == NK_HTTP_COOKIES_SESSION) {
                state = client_state(request);
                options_ok = state && set_option(CURLOPT_SHARE, state->share) && options_ok;
                options_ok = set_option(CURLOPT_COOKIEFILE, "") && options_ok;
            }

            if (request->request.method == NK_HTTP_METHOD_HEAD)
                options_ok = set_option(CURLOPT_NOBODY, 1L) && options_ok;
            if (!request->request.body.empty()) {
                options_ok =
                    set_option(CURLOPT_POSTFIELDS, request->request.body.data()) && options_ok;
                options_ok = set_option(CURLOPT_POSTFIELDSIZE_LARGE,
                                        static_cast<curl_off_t>(request->request.body.size())) &&
                             options_ok;
            }
            if (request->request.upload_stream != NK_INVALID_HANDLE) {
                options_ok = set_option(CURLOPT_UPLOAD, 1L) && options_ok;
                options_ok = set_option(CURLOPT_READFUNCTION, read_callback) && options_ok;
                options_ok = set_option(CURLOPT_READDATA, request.get()) && options_ok;
                if (request->request.upload_size != 0)
                    options_ok =
                        set_option(CURLOPT_INFILESIZE_LARGE,
                                   static_cast<curl_off_t>(request->request.upload_size)) &&
                        options_ok;
            }

            if (!options_ok) {
                result = unsupported_security_policy
                             ? NK_ERROR_UNSUPPORTED
                             : static_cast<nk_result>(NK_HTTP_ERROR_PROTOCOL);
            } else {
                const auto curl_result = curl_easy_perform(curl);
                long response_code = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
                long redirect_count = 0;
                curl_easy_getinfo(curl, CURLINFO_REDIRECT_COUNT, &redirect_count);
                curl_off_t content_length = -1;
                curl_easy_getinfo(curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &content_length);
                {
                    std::lock_guard lock(request->mutex);
                    if (response_code >= 100 && response_code <= 599)
                        request->status_code = static_cast<uint32_t>(response_code);
                    if (content_length >= 0 &&
                        request->content_length == NK_HTTP_CONTENT_LENGTH_UNKNOWN)
                        request->content_length = static_cast<uint64_t>(content_length);
                    if (redirect_count > 0)
                        request->response_flags |= NK_HTTP_RESPONSE_REDIRECTED;
                    request->total = request->content_length;
                }
                result = map_curl_error(curl_result, *request);
                if (result == NK_OK && (response_code < 100 || response_code > 599))
                    result = NK_HTTP_ERROR_PROTOCOL;
            }
        }
    }
    if (header_list)
        curl_slist_free_all(header_list);
    if (curl)
        curl_easy_cleanup(curl);
    return result;
}

void perform(nk::net::RequestPtr request) noexcept {
    const auto result = perform_impl(request);
    nk::net::complete_request(request, result);
    nk::net::worker_finished(request);
    request.reset();
}

} // namespace

namespace nk::net {

nk_capabilities capabilities() noexcept {
    std::lock_guard lock(curl_mutex);
    return initialize_curl() ? NK_CAP_HTTP_CLIENT | NK_CAP_HTTP_STREAMING : 0;
}

nk_result backend_start(const RequestPtr &request) noexcept {
    {
        std::lock_guard lock(curl_mutex);
        if (!initialize_curl())
            return NK_ERROR_UNSUPPORTED;
    }
    std::thread([request] { perform(request); }).detach();
    return NK_OK;
}

void backend_cancel(const RequestPtr &) noexcept {}

void backend_shutdown() noexcept {
    std::lock_guard lock(curl_mutex);
    if (curl_initialized) {
        curl_global_cleanup();
        curl_initialized = false;
    }
}

} // namespace nk::net
