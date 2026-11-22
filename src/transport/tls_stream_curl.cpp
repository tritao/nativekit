#include "transport/secure_websocket.hpp"

#include "core/thread_name.hpp"
#include "net/curl_runtime.hpp"

#include <array>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>
#include <string>
#include <thread>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * libcurl connects and verifies the TLS session (CURLOPT_CONNECT_ONLY) and
 * then carries raw bytes with curl_easy_send/curl_easy_recv. Those calls exist
 * in every libcurl since 7.18, unlike curl's WebSocket API, which distributions
 * still build without; NativeKit's own WebSocket handshake and framing run on
 * top, as for ws://. One pump thread moves bytes between the TLS session and
 * a local socket pair whose other end the transport polls like a TCP socket.
 */

namespace nk::transport {
namespace {

constexpr unsigned int minimum_curl = 0x072d00; /* 7.45: CURLINFO_ACTIVESOCKET */
constexpr std::size_t pump_chunk = 16u * 1024u;

#if defined(MSG_NOSIGNAL)
constexpr int local_send_flags = MSG_NOSIGNAL | MSG_DONTWAIT;
#else
constexpr int local_send_flags = MSG_DONTWAIT;
#endif

const curl::Api *usable_curl() noexcept {
    const auto *api = curl::api();
    if (!api || api->version_num < minimum_curl || (api->features & curl::version_ssl) == 0)
        return nullptr;
    return api;
}

int abort_when_stopping(void *user_data, curl::Offset, curl::Offset, curl::Offset,
                        curl::Offset) noexcept {
    return static_cast<const std::atomic<bool> *>(user_data)->load(std::memory_order_acquire) ? 1
                                                                                              : 0;
}

nk_result map_connect_error(curl::Code code) noexcept {
    switch (code) {
    case curl::couldnt_resolve_host:
    case curl::couldnt_resolve_proxy:
        return NK_TRANSPORT_ERROR_DNS;
    case curl::operation_timedout:
        return NK_TRANSPORT_ERROR_TIMEOUT;
    case curl::aborted_by_callback:
        return NK_TRANSPORT_ERROR_CANCELED;
    default:
        /* Refused connections and failed certificate or host name checks alike. */
        return NK_TRANSPORT_ERROR_CONNECTION;
    }
}

std::string https_url(const SecureEndpoint &endpoint) {
    std::string url = "https://";
    const bool literal_v6 =
        endpoint.host.find(':') != std::string::npos && endpoint.host.front() != '[';
    if (literal_v6)
        url += '[';
    url += endpoint.host;
    if (literal_v6)
        url += ']';
    url += ':' + std::to_string(endpoint.port) + '/';
    return url;
}

class CurlTlsStream final : public TlsStream {
  public:
    CurlTlsStream(const curl::Api &api, curl::Handle *handle, curl::Socket tls_socket,
                  int local_socket)
        : api_(api), handle_(handle), tls_socket_(tls_socket), local_(local_socket) {}

    ~CurlTlsStream() override {
        /* Wakes the pump wherever it waits; it then stops and the TLS session closes. */
        ::shutdown(local_, SHUT_RDWR);
        if (pump_.joinable())
            pump_.join();
        ::close(local_);
        api_.easy_cleanup(handle_);
    }

    bool start() noexcept {
#if NK_ENABLE_NO_EXCEPTIONS
        pump_ = std::thread([this] { run(); });
        return true;
#else
        try {
            pump_ = std::thread([this] { run(); });
            return true;
        } catch (...) {
            return false;
        }
#endif
    }

  private:
    struct Pending {
        std::array<unsigned char, pump_chunk> bytes{};
        std::size_t size = 0;
        std::size_t offset = 0;

        bool empty() const noexcept { return offset == size; }
        void clear() noexcept { size = offset = 0; }
    };

    void run() noexcept {
        nk::core::name_current_thread("nk.tls");
        Pending to_local;
        Pending to_tls;
        bool tls_open = true;
        for (;;) {
            bool progress = false;
            /* Drain the TLS session before waiting: decrypted bytes it buffers do not make
               its socket readable again. */
            if (tls_open && to_local.empty()) {
                std::size_t received = 0;
                const auto code = api_.easy_recv(handle_, to_local.bytes.data(),
                                                 to_local.bytes.size(), &received);
                if (code == curl::ok && received > 0) {
                    to_local.size = received;
                    to_local.offset = 0;
                    progress = true;
                } else if (code != curl::again) {
                    tls_open = false;
                }
            }
            if (!to_local.empty()) {
                const auto sent = ::send(local_, to_local.bytes.data() + to_local.offset,
                                         to_local.size - to_local.offset, local_send_flags);
                if (sent > 0) {
                    to_local.offset += static_cast<std::size_t>(sent);
                    if (to_local.empty())
                        to_local.clear();
                    progress = true;
                } else if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    break;
                }
            }
            if (!tls_open && to_local.empty())
                break;
            if (to_tls.empty()) {
                const auto received =
                    ::recv(local_, to_tls.bytes.data(), to_tls.bytes.size(), MSG_DONTWAIT);
                if (received > 0) {
                    to_tls.size = static_cast<std::size_t>(received);
                    to_tls.offset = 0;
                    progress = true;
                } else if (received == 0 ||
                           (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
                    break;
                }
            }
            if (!to_tls.empty()) {
                std::size_t sent = 0;
                const auto code = api_.easy_send(handle_, to_tls.bytes.data() + to_tls.offset,
                                                 to_tls.size - to_tls.offset, &sent);
                if (code == curl::ok) {
                    to_tls.offset += sent;
                    if (to_tls.empty())
                        to_tls.clear();
                    progress = progress || sent > 0;
                } else if (code != curl::again) {
                    break;
                }
            }
            if (progress)
                continue;
            pollfd descriptors[2]{};
            descriptors[0].events = static_cast<short>((tls_open && to_local.empty() ? POLLIN : 0) |
                                                       (!to_tls.empty() ? POLLOUT : 0));
            /* A negative descriptor is skipped, so a hung-up socket nobody waits on cannot spin
               the loop. */
            descriptors[0].fd = descriptors[0].events != 0 ? tls_socket_ : -1;
            descriptors[1].fd = local_;
            descriptors[1].events = static_cast<short>((to_tls.empty() ? POLLIN : 0) |
                                                       (!to_local.empty() ? POLLOUT : 0));
            if (::poll(descriptors, 2, -1) < 0 && errno != EINTR)
                break;
            /* The transport closed its end, or the stream is being destroyed. */
            if ((descriptors[1].revents & (POLLHUP | POLLERR | POLLNVAL)) != 0)
                break;
        }
        /* The transport reads end of stream (or a failure) from its end of the pair. */
        ::shutdown(local_, SHUT_RDWR);
    }

    const curl::Api &api_;
    curl::Handle *handle_;
    curl::Socket tls_socket_;
    int local_;
    std::thread pump_;
};

bool make_local_pair(int (&pair)[2]) noexcept {
    int type = SOCK_STREAM;
#if defined(SOCK_CLOEXEC)
    type |= SOCK_CLOEXEC;
#endif
    if (::socketpair(AF_UNIX, type, 0, pair) != 0)
        return false;
    for (const int end : pair) {
        const int flags = fcntl(end, F_GETFL, 0);
        if (fcntl(end, F_SETFD, FD_CLOEXEC) != 0 || flags < 0 ||
            fcntl(end, F_SETFL, flags | O_NONBLOCK) != 0) {
            ::close(pair[0]);
            ::close(pair[1]);
            return false;
        }
    }
    return true;
}

} // namespace

bool secure_client_available() noexcept {
    return usable_curl() != nullptr;
}

nk_result open_tls_stream(const SecureEndpoint &endpoint, const std::atomic<bool> &stopping,
                          std::unique_ptr<TlsStream> &out_stream, int &out_socket) noexcept {
    const auto *api = usable_curl();
    if (!api)
        return NK_ERROR_UNSUPPORTED;
    curl::Handle *handle = api->easy_init();
    if (!handle)
        return NK_ERROR_OUT_OF_MEMORY;
    std::string url;
#if NK_ENABLE_NO_EXCEPTIONS
    url = https_url(endpoint);
#else
    try {
        url = https_url(endpoint);
    } catch (...) {
        api->easy_cleanup(handle);
        return NK_ERROR_OUT_OF_MEMORY;
    }
#endif
    const long timeout = static_cast<long>(endpoint.timeout_ms);
    curl::XferInfoCallback progress = abort_when_stopping;
    bool configured = api->easy_setopt(handle, curl::opt_url, url.c_str()) == curl::ok &&
                      api->easy_setopt(handle, curl::opt_connect_only, 1L) == curl::ok &&
                      api->easy_setopt(handle, curl::opt_nosignal, 1L) == curl::ok &&
                      api->easy_setopt(handle, curl::opt_connecttimeout_ms, timeout) == curl::ok &&
                      api->easy_setopt(handle, curl::opt_timeout_ms, timeout) == curl::ok &&
                      api->easy_setopt(handle, curl::opt_noprogress, 0L) == curl::ok &&
                      api->easy_setopt(handle, curl::opt_xferinfofunction, progress) == curl::ok &&
                      api->easy_setopt(handle, curl::opt_xferinfodata,
                                       const_cast<std::atomic<bool> *>(&stopping)) == curl::ok;
    /* OpenSSL's trust-store variables, which libcurl ignores once it was built with a default
       bundle. A process that sets them expects every TLS client to use them. */
    if (const char *file = std::getenv("SSL_CERT_FILE"); configured && file && *file)
        configured = api->easy_setopt(handle, curl::opt_cainfo, file) == curl::ok;
    if (const char *directory = std::getenv("SSL_CERT_DIR"); configured && directory && *directory)
        configured = api->easy_setopt(handle, curl::opt_capath, directory) == curl::ok;
    if (!configured) {
        api->easy_cleanup(handle);
        return NK_TRANSPORT_ERROR_CONNECTION;
    }
    const auto code = api->easy_perform(handle);
    if (code != curl::ok) {
        api->easy_cleanup(handle);
        return stopping.load(std::memory_order_acquire) ? NK_TRANSPORT_ERROR_CANCELED
                                                        : map_connect_error(code);
    }
    curl::Socket tls_socket = -1;
    int pair[2]{-1, -1};
    if (api->easy_getinfo(handle, curl::info_activesocket, &tls_socket) != curl::ok ||
        tls_socket < 0 || !make_local_pair(pair)) {
        api->easy_cleanup(handle);
        return NK_TRANSPORT_ERROR_CONNECTION;
    }
    std::unique_ptr<CurlTlsStream> stream(new (std::nothrow)
                                              CurlTlsStream(*api, handle, tls_socket, pair[1]));
    if (!stream) {
        ::close(pair[0]);
        ::close(pair[1]);
        api->easy_cleanup(handle);
        return NK_ERROR_OUT_OF_MEMORY;
    }
    if (!stream->start()) {
        ::close(pair[0]);
        return NK_ERROR_OUT_OF_MEMORY;
    }
    out_stream = std::move(stream);
    out_socket = pair[0];
    return NK_OK;
}

} // namespace nk::transport
