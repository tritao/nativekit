#include "net_backend.hpp"
#include "net/curl_progress.hpp"
#include "net/curl_runtime.hpp"

#include "core/error.hpp"
#include "core/thread_name.hpp"

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

namespace curl = nk::curl;

/* curl::opt_protocols_str and curl::opt_redir_protocols_str arrived in 7.85. */
constexpr unsigned int minimum_curl = 0x075500;

std::mutex curl_share_data_mutex;

/** The loaded libcurl when it is recent enough for this backend, otherwise null. */
const curl::Api *curl_api() noexcept {
    const auto *api = curl::api();
    return api && api->version_num >= minimum_curl ? api : nullptr;
}

struct CurlClientState {
    const curl::Api *api = nullptr;
    curl::Share *share = nullptr;

    ~CurlClientState() {
        if (share)
            api->share_cleanup(share);
    }
};

void curl_share_lock(curl::Handle *, curl::LockData, curl::LockAccess, void *) {
    curl_share_data_mutex.lock();
}

void curl_share_unlock(curl::Handle *, curl::LockData, void *) {
    curl_share_data_mutex.unlock();
}

std::shared_ptr<CurlClientState> client_state(const curl::Api &api,
                                              const nk::net::RequestPtr &request) {
    std::lock_guard lock(request->client->backend_mutex);
    if (request->client->backend_state)
        return std::static_pointer_cast<CurlClientState>(request->client->backend_state);
    auto state = std::make_shared<CurlClientState>();
    state->api = &api;
    state->share = api.share_init();
    const curl::LockCallback lock_share = curl_share_lock;
    const curl::UnlockCallback unlock_share = curl_share_unlock;
    if (!state->share ||
        api.share_setopt(state->share, curl::shareopt_share, curl::lock_data_cookie) !=
            curl::share_ok ||
        api.share_setopt(state->share, curl::shareopt_lockfunc, lock_share) != curl::share_ok ||
        api.share_setopt(state->share, curl::shareopt_unlockfunc, unlock_share) != curl::share_ok) {
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
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r' ||
                              value.back() == '\n'))
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
#if NK_ENABLE_NO_EXCEPTIONS
    return header_callback_impl(data, size, count, user_data);
#else
    try {
        return header_callback_impl(data, size, count, user_data);
    } catch (...) {
        auto &request = *static_cast<nk::net::RequestContext *>(user_data);
        std::lock_guard lock(request.mutex);
        request.response_limit = true;
        return 0;
    }
#endif
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
        if (std::chrono::steady_clock::now() >= request.deadline) {
            request.timed_out.store(true, std::memory_order_release);
            return 0;
        }
        if (!request.condition.wait_until(lock, request.deadline, [&] {
                return request.canceled.load(std::memory_order_acquire) || request.stream_closed ||
                       request.available < request.request.stream_buffer_size;
            })) {
            request.timed_out.store(true, std::memory_order_release);
            return 0;
        }
        if (request.canceled.load(std::memory_order_acquire) || request.stream_closed)
            return 0;
        if (std::chrono::steady_clock::now() >= request.deadline) {
            request.timed_out.store(true, std::memory_order_release);
            return 0;
        }
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
#if NK_ENABLE_NO_EXCEPTIONS
    return write_callback_impl(data, size, count, user_data);
#else
    try {
        return write_callback_impl(data, size, count, user_data);
    } catch (...) {
        auto &request = *static_cast<nk::net::RequestContext *>(user_data);
        std::lock_guard lock(request.mutex);
        request.response_limit = true;
        return 0;
    }
#endif
}

std::size_t read_callback_impl(char *data, std::size_t size, std::size_t count, void *user_data) {
    auto &request = *static_cast<nk::net::RequestContext *>(user_data);
    const auto capacity = size * count;
    if (request.canceled.load(std::memory_order_acquire))
        return curl::readfunc_abort;
    uint64_t amount = 0;
    const auto result = nk_resource_read(request.request.upload_stream, data, capacity, &amount);
    if (result != NK_OK) {
        request.upload_result = result;
        return curl::readfunc_abort;
    }
    request.upload_position += amount;
    return static_cast<std::size_t>(amount);
}

std::size_t read_callback(char *data, std::size_t size, std::size_t count,
                          void *user_data) noexcept {
#if NK_ENABLE_NO_EXCEPTIONS
    return read_callback_impl(data, size, count, user_data);
#else
    try {
        return read_callback_impl(data, size, count, user_data);
    } catch (...) {
        auto &request = *static_cast<nk::net::RequestContext *>(user_data);
        request.upload_result = NK_ERROR_UNKNOWN;
        return curl::readfunc_abort;
    }
#endif
}

int progress_callback_impl(void *user_data, curl::Offset download_total, curl::Offset downloaded,
                           curl::Offset upload_total, curl::Offset uploaded) {
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

int progress_callback(void *user_data, curl::Offset download_total, curl::Offset downloaded,
                      curl::Offset upload_total, curl::Offset uploaded) noexcept {
#if NK_ENABLE_NO_EXCEPTIONS
    return progress_callback_impl(user_data, download_total, downloaded, upload_total, uploaded);
#else
    try {
        return progress_callback_impl(user_data, download_total, downloaded, upload_total,
                                      uploaded);
    } catch (...) {
        return 1;
    }
#endif
}

nk_result map_curl_error(curl::Code code, const nk::net::RequestContext &request) {
    if (request.canceled.load(std::memory_order_acquire))
        return NK_HTTP_ERROR_CANCELED;
    if (request.timed_out.load(std::memory_order_acquire))
        return NK_HTTP_ERROR_TIMEOUT;
    if (request.response_limit)
        return NK_HTTP_ERROR_RESPONSE_LIMIT;
    if (request.upload_result != NK_OK)
        return NK_HTTP_ERROR_PROTOCOL;
    switch (code) {
    case curl::couldnt_resolve_host:
        return NK_HTTP_ERROR_DNS;
    case curl::couldnt_resolve_proxy:
        return NK_HTTP_ERROR_PROXY;
    case curl::proxy:
        return NK_HTTP_ERROR_PROXY;
    case curl::couldnt_connect:
    case curl::interface_failed:
    case curl::no_connection_available:
        return NK_HTTP_ERROR_CONNECTION;
    case curl::ssl_connect_error:
    case curl::ssl_certproblem:
    case curl::ssl_cipher:
    case curl::ssl_issuer_error:
    case curl::peer_failed_verification:
    case curl::use_ssl_failed:
        return NK_HTTP_ERROR_TLS;
    case curl::operation_timedout:
        return NK_HTTP_ERROR_TIMEOUT;
    case curl::too_many_redirects:
        return NK_HTTP_ERROR_REDIRECT;
    case curl::http_returned_error:
    case curl::http2:
    case curl::http3:
    case curl::partial_file:
    case curl::recv_error:
    case curl::send_error:
        return NK_HTTP_ERROR_PROTOCOL;
    case curl::ok:
        return NK_OK;
    default:
        return NK_HTTP_ERROR_CONNECTION;
    }
}

nk_result perform_impl(const curl::Api &api, nk::net::RequestPtr request) {
    nk_result result = NK_HTTP_ERROR_CONNECTION;
    curl::Handle *handle = api.easy_init();
    curl::Slist *header_list = nullptr;
    struct CurlCleanup {
        const curl::Api &api;
        curl::Handle *&handle;
        curl::Slist *&header_list;

        ~CurlCleanup() {
            if (header_list)
                api.slist_free_all(header_list);
            if (handle)
                api.easy_cleanup(handle);
        }
    } cleanup{api, handle, header_list};
    std::shared_ptr<CurlClientState> state;
    if (handle) {
        auto add_headers = [&](const std::vector<nk::net::OwnedHeader> &headers) {
            for (const auto &header : headers) {
                std::string line = header.name + ": " + header.value;
                auto *next = api.slist_append(header_list, line.c_str());
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
            auto set_option = [&](curl::Option option, auto value) {
                return api.easy_setopt(handle, option, value) == curl::ok;
            };
            const curl::DataCallback write_function = write_callback;
            const curl::DataCallback header_function = header_callback;
            const curl::DataCallback read_function = read_callback;
            const curl::XferInfoCallback progress_function = progress_callback;
            bool options_ok =
                set_option(curl::opt_url, request->request.url.c_str()) &&
                set_option(curl::opt_customrequest, method) &&
                set_option(curl::opt_followlocation, 1L) &&
                set_option(curl::opt_maxredirs,
                           static_cast<long>(request->request.redirect_limit)) &&
                set_option(curl::opt_timeout_ms, static_cast<long>(request->request.timeout_ms)) &&
                set_option(curl::opt_connecttimeout_ms,
                           static_cast<long>(request->request.timeout_ms)) &&
                set_option(curl::opt_nosignal, 1L) && set_option(curl::opt_failonerror, 0L) &&
                set_option(curl::opt_protocols_str,
                           (request->client->config.flags & NK_HTTP_CLIENT_ALLOW_HTTP)
                               ? "http,https"
                               : "https") &&
                set_option(curl::opt_redir_protocols_str,
                           (request->client->config.flags & NK_HTTP_CLIENT_ALLOW_HTTPS_TO_HTTP)
                               ? "http,https"
                               : "https");
            if (header_list)
                options_ok = set_option(curl::opt_httpheader, header_list) && options_ok;
            options_ok = set_option(curl::opt_writefunction, write_function) && options_ok;
            options_ok = set_option(curl::opt_writedata, request.get()) && options_ok;
            options_ok = set_option(curl::opt_headerfunction, header_function) && options_ok;
            options_ok = set_option(curl::opt_headerdata, request.get()) && options_ok;
            options_ok = set_option(curl::opt_xferinfofunction, progress_function) && options_ok;
            options_ok = set_option(curl::opt_xferinfodata, request.get()) && options_ok;
            options_ok = set_option(curl::opt_noprogress, 0L) && options_ok;

            const auto &tls = request->client->config.tls;
            options_ok =
                set_option(curl::opt_ssl_verifypeer,
                           (tls.flags & NK_HTTP_TLS_DISABLE_PEER_VERIFICATION) ? 0L : 1L) &&
                options_ok;
            options_ok =
                set_option(curl::opt_ssl_verifyhost,
                           (tls.flags & NK_HTTP_TLS_DISABLE_HOSTNAME_VERIFICATION) ? 0L : 2L) &&
                options_ok;
            if (!tls.ca_bundle_path.empty())
                options_ok = set_option(curl::opt_cainfo, tls.ca_bundle_path.c_str()) && options_ok;
            if (tls.minimum_version == NK_HTTP_TLS_1_2)
                options_ok =
                    set_option(curl::opt_sslversion, curl::sslversion_tlsv1_2) && options_ok;
            if (tls.minimum_version == NK_HTTP_TLS_1_3)
                options_ok =
                    set_option(curl::opt_sslversion, curl::sslversion_tlsv1_3) && options_ok;

            const auto &proxy = request->client->config.proxy;
            if (proxy.kind != NK_HTTP_PROXY_NONE) {
                options_ok = set_option(curl::opt_proxy, proxy.url.c_str()) && options_ok;
                options_ok = set_option(curl::opt_proxytype, proxy.kind == NK_HTTP_PROXY_HTTP
                                                                 ? curl::proxy_http
                                                             : proxy.kind == NK_HTTP_PROXY_HTTPS
                                                                 ? curl::proxy_https
                                                                 : curl::proxy_socks5_hostname) &&
                             options_ok;
                if (!proxy.username.empty())
                    options_ok =
                        set_option(curl::opt_proxyusername, proxy.username.c_str()) && options_ok;
                if (!proxy.password.empty())
                    options_ok =
                        set_option(curl::opt_proxypassword, proxy.password.c_str()) && options_ok;
            }
            if (request->client->config.cookie_policy == NK_HTTP_COOKIES_SESSION) {
                state = client_state(api, request);
                options_ok = state && set_option(curl::opt_share, state->share) && options_ok;
                options_ok = set_option(curl::opt_cookiefile, "") && options_ok;
            }

            if (request->request.method == NK_HTTP_METHOD_HEAD)
                options_ok = set_option(curl::opt_nobody, 1L) && options_ok;
            if (!request->request.body.empty()) {
                options_ok =
                    set_option(curl::opt_postfields, request->request.body.data()) && options_ok;
                options_ok = set_option(curl::opt_postfieldsize_large,
                                        static_cast<curl::Offset>(request->request.body.size())) &&
                             options_ok;
            }
            if (request->request.upload_stream != NK_INVALID_HANDLE) {
                options_ok = set_option(curl::opt_upload, 1L) && options_ok;
                options_ok = set_option(curl::opt_readfunction, read_function) && options_ok;
                options_ok = set_option(curl::opt_readdata, request.get()) && options_ok;
                if (request->request.upload_size != 0)
                    options_ok =
                        set_option(curl::opt_infilesize_large,
                                   static_cast<curl::Offset>(request->request.upload_size)) &&
                        options_ok;
            }

            if (!options_ok) {
                result = static_cast<nk_result>(NK_HTTP_ERROR_PROTOCOL);
            } else {
                const auto curl_result = api.easy_perform(handle);
                long response_code = 0;
                api.easy_getinfo(handle, curl::info_response_code, &response_code);
                long redirect_count = 0;
                api.easy_getinfo(handle, curl::info_redirect_count, &redirect_count);
                curl::Offset content_length = -1;
                api.easy_getinfo(handle, curl::info_content_length_download_t, &content_length);
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
    return result;
}

void perform(const curl::Api &api, nk::net::RequestPtr request) noexcept {
#if NK_ENABLE_NO_EXCEPTIONS
    const auto result = perform_impl(api, request);
#else
    nk_result result = NK_ERROR_UNKNOWN;
    try {
        result = perform_impl(api, request);
    } catch (const std::bad_alloc &) {
        result = NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        result = NK_ERROR_UNKNOWN;
    }
#endif
    nk::net::complete_request(request, result);
    nk::net::worker_finished(request);
    request.reset();
}

} // namespace

namespace nk::net {

nk_capabilities capabilities() noexcept {
    return curl_api() ? NK_CAP_HTTP_CLIENT | NK_CAP_HTTP_STREAMING : 0;
}

nk_result backend_start(const RequestPtr &request) noexcept {
    const auto *api = curl_api();
    if (!api)
        return NK_ERROR_UNSUPPORTED;
#if NK_ENABLE_NO_EXCEPTIONS
    std::thread([api, request] {
        nk::core::name_current_thread("nk.net.request");
        perform(*api, request);
    }).detach();
    return NK_OK;
#else
    try {
        std::thread([api, request] {
            nk::core::name_current_thread("nk.net.request");
            perform(*api, request);
        }).detach();
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return NK_ERROR_UNKNOWN;
    }
#endif
}

void backend_cancel(const RequestPtr &) noexcept {}

/* libcurl stays loaded and initialized for the process (see curl_runtime.hpp). */
void backend_shutdown() noexcept {}

} // namespace nk::net
