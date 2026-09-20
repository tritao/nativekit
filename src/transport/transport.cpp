#include "nativekit_transport.h"

#include "core/boundary.hpp"
#include "core/event_queue.hpp"
#include "core/error.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <cstring>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#if defined(__EMSCRIPTEN__)

namespace nk::transport {
nk_capabilities capabilities() noexcept {
    return 0;
}
void shutdown() noexcept {}
} // namespace nk::transport

extern "C" {

nk_result NK_CALL nk_transport_connect(const nk_transport_options *, nk_transport *out_transport) {
    if (out_transport)
        *out_transport = NK_INVALID_HANDLE;
    return NK_ERROR_UNSUPPORTED;
}
nk_result NK_CALL nk_transport_listen(const nk_transport_options *, nk_listener *out_listener) {
    if (out_listener)
        *out_listener = NK_INVALID_HANDLE;
    return NK_ERROR_UNSUPPORTED;
}
nk_result NK_CALL nk_transport_send(nk_transport, const void *, uint64_t) {
    return NK_ERROR_UNSUPPORTED;
}
nk_result NK_CALL nk_transport_receive(nk_transport, void *, uint64_t, uint64_t *out_received) {
    if (out_received)
        *out_received = 0;
    return NK_ERROR_UNSUPPORTED;
}
nk_result NK_CALL nk_transport_close(nk_transport) {
    return NK_ERROR_UNSUPPORTED;
}
nk_result NK_CALL nk_listener_close(nk_listener) {
    return NK_ERROR_UNSUPPORTED;
}
nk_result NK_CALL nk_transport_event_data(const nk_event *, nk_transport_data_event *) {
    return NK_ERROR_UNSUPPORTED;
}
nk_result NK_CALL nk_transport_event_accepted(const nk_event *, nk_transport_accepted_event *) {
    return NK_ERROR_UNSUPPORTED;
}

} // extern "C"

#else

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <netinet/tcp.h>
#include <unistd.h>
#endif

namespace {

using clock_type = std::chrono::steady_clock;

#if defined(_WIN32)
using socket_type = SOCKET;
using socket_length_type = int;
constexpr socket_type invalid_socket = INVALID_SOCKET;
constexpr int send_flags = 0;
#else
using socket_type = int;
using socket_length_type = socklen_t;
constexpr socket_type invalid_socket = -1;
#if defined(MSG_NOSIGNAL)
constexpr int send_flags = MSG_NOSIGNAL;
#else
constexpr int send_flags = 0;
#endif
#endif

constexpr std::size_t max_option_string = 4096;
constexpr std::size_t max_http_header = 64u * 1024u;
constexpr std::size_t max_message = 16u * 1024u * 1024u;
constexpr std::size_t read_chunk = 16u * 1024u;
constexpr std::size_t max_udp_datagram = 64u * 1024u;
constexpr uint64_t default_buffer_size = 4u * 1024u * 1024u;
constexpr uint32_t default_timeout_ms = 5000;
constexpr uint32_t default_backlog = 16;
constexpr char websocket_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

struct TransportOptions {
    nk_transport_kind kind = NK_TRANSPORT_TCP;
    nk_transport_flags flags = 0;
    std::string host;
    uint16_t port = 0;
    std::string path;
    uint32_t timeout_ms = default_timeout_ms;
    uint32_t backlog = default_backlog;
    uint64_t receive_buffer_size = default_buffer_size;
    uint64_t send_buffer_size = default_buffer_size;
};

nk_result fail(nk_result result, const char *message) noexcept {
    nk::core::set_error(message);
    return result;
}

bool copy_string(const char *value, std::string &out, bool nullable = true) {
    if (!value) {
        if (nullable) {
            out.clear();
            return true;
        }
        return false;
    }
    std::size_t length = 0;
    while (length < max_option_string && value[length] != '\0')
        ++length;
    if (length == max_option_string)
        return false;
    out.assign(value, length);
    return true;
}

bool valid_kind(nk_transport_kind kind) {
    return kind == NK_TRANSPORT_TCP || kind == NK_TRANSPORT_UDP || kind == NK_TRANSPORT_WEBSOCKET ||
           kind == NK_TRANSPORT_LOCAL;
}

bool copy_options(const nk_transport_options *input, TransportOptions &output, bool listening) {
    if (!input || input->struct_size < sizeof(*input) || !valid_kind(input->kind) ||
        (input->flags & ~(NK_TRANSPORT_REUSE_ADDRESS | NK_TRANSPORT_NO_DELAY)) != 0 ||
        input->reserved0 != 0 || input->reserved[0] != 0 || input->reserved[1] != 0)
        return false;
    if (!copy_string(input->host, output.host) || !copy_string(input->path, output.path))
        return false;
    if (input->kind == NK_TRANSPORT_LOCAL) {
#if defined(_WIN32)
        return false;
#else
        if (output.path.empty() || output.path.size() >= sizeof(sockaddr_un::sun_path))
            return false;
#endif
    } else if (listening && !output.host.empty() && output.host.size() >= max_option_string) {
        return false;
    }
    if (input->kind == NK_TRANSPORT_WEBSOCKET) {
        if (output.path.empty())
            output.path = "/";
        if (output.path.front() != '/')
            output.path.insert(output.path.begin(), '/');
    } else if (input->kind != NK_TRANSPORT_LOCAL && !output.path.empty()) {
        return false;
    }
    output.kind = input->kind;
    output.flags = input->flags;
    output.port = input->port;
    output.timeout_ms = input->timeout_ms == 0 ? default_timeout_ms : input->timeout_ms;
    output.backlog = input->backlog == 0 ? default_backlog : input->backlog;
    output.receive_buffer_size =
        input->receive_buffer_size == 0 ? default_buffer_size : input->receive_buffer_size;
    output.send_buffer_size =
        input->send_buffer_size == 0 ? default_buffer_size : input->send_buffer_size;
    if (output.receive_buffer_size > max_message * 64u ||
        output.send_buffer_size > max_message * 64u)
        return false;
    if (!listening && output.kind != NK_TRANSPORT_LOCAL && output.host.empty())
        return false;
    return true;
}

#if defined(_WIN32)
std::once_flag socket_start_once;
int socket_start_result = 0;
void initialize_sockets() {
    std::call_once(socket_start_once, [] {
        WSADATA data{};
        socket_start_result = WSAStartup(MAKEWORD(2, 2), &data);
    });
}
#else
void initialize_sockets() {}
#endif

int socket_error() noexcept {
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool socket_would_block(int error) noexcept {
#if defined(_WIN32)
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS || error == WSAEALREADY;
#else
    return error == EAGAIN || error == EWOULDBLOCK || error == EINPROGRESS || error == EALREADY;
#endif
}

bool socket_interrupted(int error) noexcept {
#if defined(_WIN32)
    return error == WSAEINTR;
#else
    return error == EINTR;
#endif
}

void close_socket(socket_type socket) noexcept {
    if (socket == invalid_socket)
        return;
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

void shutdown_socket(socket_type socket) noexcept {
    if (socket == invalid_socket)
        return;
#if defined(_WIN32)
    shutdown(socket, SD_BOTH);
#else
    shutdown(socket, SHUT_RDWR);
#endif
}

bool set_nonblocking(socket_type socket) noexcept {
#if defined(_WIN32)
    u_long value = 1;
    return ioctlsocket(socket, FIONBIO, &value) == 0;
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    return flags >= 0 && fcntl(socket, F_SETFL, flags | O_NONBLOCK) == 0;
#endif
}

bool set_no_delay(socket_type socket) noexcept {
    int value = 1;
    return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&value),
                      sizeof(value)) == 0;
}

int wait_socket(socket_type socket, bool readable, bool writable, uint32_t timeout_ms) noexcept {
    if (socket == invalid_socket)
        return -1;
#if defined(_WIN32)
    fd_set read_set{};
    fd_set write_set{};
    fd_set error_set{};
    if (readable) {
        FD_ZERO(&read_set);
        FD_SET(socket, &read_set);
    }
    if (writable) {
        FD_ZERO(&write_set);
        FD_SET(socket, &write_set);
    }
    FD_ZERO(&error_set);
    FD_SET(socket, &error_set);
    timeval timeout{};
    timeout.tv_sec = static_cast<long>(timeout_ms / 1000u);
    timeout.tv_usec = static_cast<long>((timeout_ms % 1000u) * 1000u);
    const int result = ::select(0, readable ? &read_set : nullptr, writable ? &write_set : nullptr,
                                &error_set, &timeout);
    if (result <= 0)
        return result;
    int ready = 0;
    if ((readable && FD_ISSET(socket, &read_set)) || FD_ISSET(socket, &error_set))
        ready |= 1;
    if ((writable && FD_ISSET(socket, &write_set)) || FD_ISSET(socket, &error_set))
        ready |= 2;
    return ready;
#else
    pollfd descriptor{};
    descriptor.fd = socket;
    descriptor.events = static_cast<short>((readable ? POLLIN : 0) | (writable ? POLLOUT : 0));
    const auto timeout = timeout_ms > static_cast<uint32_t>(std::numeric_limits<int>::max())
                             ? std::numeric_limits<int>::max()
                             : static_cast<int>(timeout_ms);
    const int result = poll(&descriptor, 1, timeout);
    if (result <= 0)
        return result;
    int ready = 0;
    if ((descriptor.revents & (POLLIN | POLLERR | POLLHUP)) != 0)
        ready |= 1;
    if ((descriptor.revents & (POLLOUT | POLLERR)) != 0)
        ready |= 2;
    return ready;
#endif
}

nk_result map_connect_error(int error) noexcept {
#if defined(_WIN32)
    if (error == WSAETIMEDOUT)
#else
    if (error == ETIMEDOUT)
#endif
        return NK_TRANSPORT_ERROR_TIMEOUT;
    return NK_TRANSPORT_ERROR_CONNECTION;
}

nk_result map_bind_error(int error) noexcept {
#if defined(_WIN32)
    if (error == WSAEADDRINUSE)
#else
    if (error == EADDRINUSE)
#endif
        return NK_TRANSPORT_ERROR_ADDRESS_IN_USE;
    return NK_TRANSPORT_ERROR_CONNECTION;
}

bool checked_add(std::size_t first, std::size_t second, std::size_t &result) {
    if (second > std::numeric_limits<std::size_t>::max() - first)
        return false;
    result = first + second;
    return true;
}

/* Small self-contained SHA-1 implementation used only for RFC 6455's
 * Sec-WebSocket-Accept value. */
class Sha1 {
  public:
    void update(const uint8_t *data, std::size_t size) {
        while (size != 0) {
            const auto amount = std::min(size, block_.size() - used_);
            std::memcpy(block_.data() + used_, data, amount);
            used_ += amount;
            data += amount;
            size -= amount;
            total_ += amount;
            if (used_ == block_.size()) {
                transform(block_.data());
                used_ = 0;
            }
        }
    }

    std::array<uint8_t, 20> finish() {
        const uint64_t bits = total_ * 8u;
        block_[used_++] = 0x80;
        if (used_ > 56) {
            std::fill(block_.begin() + static_cast<std::ptrdiff_t>(used_), block_.end(), 0);
            transform(block_.data());
            used_ = 0;
        }
        std::fill(block_.begin() + static_cast<std::ptrdiff_t>(used_), block_.begin() + 56, 0);
        for (unsigned index = 0; index != 8; ++index)
            block_[56 + index] = static_cast<uint8_t>(bits >> (56u - index * 8u));
        transform(block_.data());
        std::array<uint8_t, 20> result{};
        for (unsigned index = 0; index != 5; ++index) {
            result[index * 4] = static_cast<uint8_t>(state_[index] >> 24);
            result[index * 4 + 1] = static_cast<uint8_t>(state_[index] >> 16);
            result[index * 4 + 2] = static_cast<uint8_t>(state_[index] >> 8);
            result[index * 4 + 3] = static_cast<uint8_t>(state_[index]);
        }
        return result;
    }

  private:
    static uint32_t rotate(uint32_t value, unsigned amount) {
        return (value << amount) | (value >> (32u - amount));
    }

    void transform(const uint8_t *block) {
        uint32_t words[80]{};
        for (unsigned index = 0; index != 16; ++index) {
            words[index] = (static_cast<uint32_t>(block[index * 4]) << 24) |
                           (static_cast<uint32_t>(block[index * 4 + 1]) << 16) |
                           (static_cast<uint32_t>(block[index * 4 + 2]) << 8) |
                           static_cast<uint32_t>(block[index * 4 + 3]);
        }
        for (unsigned index = 16; index != 80; ++index)
            words[index] = rotate(
                words[index - 3] ^ words[index - 8] ^ words[index - 14] ^ words[index - 16], 1);
        uint32_t a = state_[0];
        uint32_t b = state_[1];
        uint32_t c = state_[2];
        uint32_t d = state_[3];
        uint32_t e = state_[4];
        for (unsigned index = 0; index != 80; ++index) {
            uint32_t function = 0;
            uint32_t constant = 0;
            if (index < 20) {
                function = (b & c) | ((~b) & d);
                constant = 0x5a827999;
            } else if (index < 40) {
                function = b ^ c ^ d;
                constant = 0x6ed9eba1;
            } else if (index < 60) {
                function = (b & c) | (b & d) | (c & d);
                constant = 0x8f1bbcdc;
            } else {
                function = b ^ c ^ d;
                constant = 0xca62c1d6;
            }
            const uint32_t next = rotate(a, 5) + function + e + constant + words[index];
            e = d;
            d = c;
            c = rotate(b, 30);
            b = a;
            a = next;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
    }

    std::array<uint8_t, 64> block_{};
    std::size_t used_ = 0;
    uint64_t total_ = 0;
    std::array<uint32_t, 5> state_ = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
};

std::string base64(const uint8_t *data, std::size_t size) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((size + 2) / 3 * 4);
    for (std::size_t index = 0; index < size; index += 3) {
        const uint32_t first = data[index];
        const uint32_t second = index + 1 < size ? data[index + 1] : 0;
        const uint32_t third = index + 2 < size ? data[index + 2] : 0;
        const uint32_t value = (first << 16) | (second << 8) | third;
        result.push_back(alphabet[(value >> 18) & 63]);
        result.push_back(alphabet[(value >> 12) & 63]);
        result.push_back(index + 1 < size ? alphabet[(value >> 6) & 63] : '=');
        result.push_back(index + 2 < size ? alphabet[value & 63] : '=');
    }
    return result;
}

std::string websocket_accept(std::string_view key) {
    std::string value(key);
    value += websocket_guid;
    Sha1 sha;
    sha.update(reinterpret_cast<const uint8_t *>(value.data()), value.size());
    const auto digest = sha.finish();
    return base64(digest.data(), digest.size());
}

std::string lower_copy(std::string_view value) {
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

std::string header_value(std::string_view headers, std::string_view name) {
    const std::string wanted = lower_copy(name);
    std::size_t position = 0;
    while (position < headers.size()) {
        const auto end = headers.find("\r\n", position);
        const auto line_end = end == std::string_view::npos ? headers.size() : end;
        const auto colon = headers.find(':', position);
        if (colon != std::string_view::npos && colon < line_end &&
            lower_copy(headers.substr(position, colon - position)) == wanted) {
            std::size_t value_start = colon + 1;
            while (value_start < line_end &&
                   (headers[value_start] == ' ' || headers[value_start] == '\t'))
                ++value_start;
            std::size_t value_end = line_end;
            while (value_end > value_start &&
                   (headers[value_end - 1] == ' ' || headers[value_end - 1] == '\t'))
                --value_end;
            return std::string(headers.substr(value_start, value_end - value_start));
        }
        if (end == std::string_view::npos)
            break;
        position = end + 2;
    }
    return {};
}

bool header_has_token(std::string_view value, std::string_view token) {
    const std::string wanted = lower_copy(token);
    std::size_t position = 0;
    while (position < value.size()) {
        const auto end = value.find(',', position);
        const auto part_end = end == std::string_view::npos ? value.size() : end;
        std::size_t first = position;
        while (first < part_end && (value[first] == ' ' || value[first] == '\t'))
            ++first;
        std::size_t last = part_end;
        while (last > first && (value[last - 1] == ' ' || value[last - 1] == '\t'))
            --last;
        if (lower_copy(value.substr(first, last - first)) == wanted)
            return true;
        if (end == std::string_view::npos)
            break;
        position = end + 1;
    }
    return false;
}

struct SocketAddress {
    sockaddr_storage storage{};
    socket_length_type size = 0;
};

nk_result connect_socket(const TransportOptions &options, socket_type &out_socket) {
    initialize_sockets();
#if defined(_WIN32)
    if (socket_start_result != 0)
        return NK_ERROR_UNSUPPORTED;
#endif
    if (options.kind == NK_TRANSPORT_LOCAL) {
#if defined(_WIN32)
        return NK_ERROR_UNSUPPORTED;
#else
        socket_type socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket == invalid_socket)
            return NK_TRANSPORT_ERROR_CONNECTION;
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, options.path.data(), options.path.size());
        if (::connect(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
            const auto error = socket_error();
            close_socket(socket);
            return map_connect_error(error);
        }
        if (!set_nonblocking(socket)) {
            close_socket(socket);
            return NK_TRANSPORT_ERROR_CONNECTION;
        }
        out_socket = socket;
        return NK_OK;
#endif
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = options.kind == NK_TRANSPORT_UDP ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = options.kind == NK_TRANSPORT_UDP ? IPPROTO_UDP : IPPROTO_TCP;
    const std::string service = std::to_string(options.port);
    addrinfo *addresses = nullptr;
    if (getaddrinfo(options.host.c_str(), service.c_str(), &hints, &addresses) != 0)
        return NK_TRANSPORT_ERROR_DNS;

    nk_result result = NK_TRANSPORT_ERROR_CONNECTION;
    for (addrinfo *address = addresses; address; address = address->ai_next) {
        const auto socket =
            ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket == invalid_socket)
            continue;
        if (!set_nonblocking(socket)) {
            close_socket(socket);
            continue;
        }
        if ((options.flags & NK_TRANSPORT_NO_DELAY) != 0 && options.kind != NK_TRANSPORT_UDP)
            (void)set_no_delay(socket);
        const int connected = ::connect(socket, address->ai_addr,
                                        static_cast<socket_length_type>(address->ai_addrlen));
        if (connected == 0) {
            out_socket = socket;
            result = NK_OK;
            break;
        }
        const auto error = socket_error();
        if (socket_would_block(error)) {
            const auto ready = wait_socket(socket, false, true, options.timeout_ms);
            if (ready < 0) {
                result = map_connect_error(socket_error());
            } else if (ready == 0) {
                result = NK_TRANSPORT_ERROR_TIMEOUT;
            } else {
                int socket_result = 0;
                socket_length_type length = sizeof(socket_result);
                if (getsockopt(socket, SOL_SOCKET, SO_ERROR,
                               reinterpret_cast<char *>(&socket_result), &length) == 0 &&
                    socket_result == 0) {
                    out_socket = socket;
                    result = NK_OK;
                    break;
                }
                result = map_connect_error(socket_result);
            }
        } else {
            result = map_connect_error(error);
        }
        close_socket(socket);
    }
    freeaddrinfo(addresses);
    return result;
}

nk_result create_listener_socket(const TransportOptions &options, socket_type &out_socket,
                                 std::string &local_path) {
    initialize_sockets();
#if defined(_WIN32)
    if (socket_start_result != 0)
        return NK_ERROR_UNSUPPORTED;
#endif
    if (options.kind == NK_TRANSPORT_LOCAL) {
#if defined(_WIN32)
        return NK_ERROR_UNSUPPORTED;
#else
        socket_type socket = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket == invalid_socket)
            return NK_TRANSPORT_ERROR_CONNECTION;
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, options.path.data(), options.path.size());
        ::unlink(options.path.c_str());
        if (::bind(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
            ::listen(socket, static_cast<int>(options.backlog)) != 0 || !set_nonblocking(socket)) {
            const auto error = socket_error();
            close_socket(socket);
            ::unlink(options.path.c_str());
            return map_bind_error(error);
        }
        local_path = options.path;
        out_socket = socket;
        return NK_OK;
#endif
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = options.kind == NK_TRANSPORT_UDP ? SOCK_DGRAM : SOCK_STREAM;
    hints.ai_protocol = options.kind == NK_TRANSPORT_UDP ? IPPROTO_UDP : IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    const std::string service = std::to_string(options.port);
    addrinfo *addresses = nullptr;
    const char *host = options.host.empty() ? nullptr : options.host.c_str();
    if (getaddrinfo(host, service.c_str(), &hints, &addresses) != 0)
        return NK_TRANSPORT_ERROR_DNS;

    nk_result result = NK_TRANSPORT_ERROR_CONNECTION;
    for (addrinfo *address = addresses; address; address = address->ai_next) {
        const auto socket =
            ::socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (socket == invalid_socket)
            continue;
        if ((options.flags & NK_TRANSPORT_REUSE_ADDRESS) != 0) {
            int value = 1;
            (void)setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
                             reinterpret_cast<const char *>(&value), sizeof(value));
        }
        if (::bind(socket, address->ai_addr,
                   static_cast<socket_length_type>(address->ai_addrlen)) != 0) {
            result = map_bind_error(socket_error());
            close_socket(socket);
            continue;
        }
        if (options.kind != NK_TRANSPORT_UDP &&
            ::listen(socket, static_cast<int>(options.backlog)) != 0) {
            result = map_bind_error(socket_error());
            close_socket(socket);
            continue;
        }
        if (!set_nonblocking(socket)) {
            close_socket(socket);
            continue;
        }
        out_socket = socket;
        result = NK_OK;
        break;
    }
    freeaddrinfo(addresses);
    return result;
}

bool send_all(socket_type socket, const void *data, std::size_t size, uint32_t timeout_ms,
              const std::atomic<bool> &stopping) {
    const auto *bytes = static_cast<const uint8_t *>(data);
    std::size_t position = 0;
    while (position < size && !stopping.load(std::memory_order_acquire)) {
#if defined(_WIN32)
        const int sent = ::send(socket, reinterpret_cast<const char *>(bytes + position),
                                static_cast<int>(size - position), 0);
#else
        const ssize_t sent = ::send(socket, bytes + position, size - position, send_flags);
#endif
        if (sent > 0) {
            position += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && socket_would_block(socket_error())) {
            const auto ready = wait_socket(socket, false, true, timeout_ms);
            if (ready <= 0)
                return false;
            continue;
        }
        if (sent < 0 && socket_interrupted(socket_error()))
            continue;
        return false;
    }
    return position == size;
}

class ListenerResource;

class TransportResource final : public nk::core::Resource,
                                public std::enable_shared_from_this<TransportResource> {
  public:
    TransportOptions options;
    uint64_t generation = 0;
    nk_transport handle = NK_INVALID_HANDLE;
    bool server_side = false;
    bool udp_server = false;
    std::weak_ptr<ListenerResource> listener;
    nk_listener listener_handle = NK_INVALID_HANDLE;
    socket_type socket = invalid_socket;
    SocketAddress udp_peer{};
    bool udp_peer_valid = false;

    std::atomic<bool> stopping{false};
    std::mutex socket_mutex;
    std::mutex mutex;
    std::condition_variable condition;
    std::thread worker;

    enum class State { connecting, open, closed };
    State state = State::connecting;
    nk_result terminal_result = NK_OK;
    std::deque<std::vector<uint8_t>> incoming;
    std::deque<std::vector<uint8_t>> outgoing;
    uint64_t incoming_bytes = 0;
    uint64_t outgoing_bytes = 0;
    std::size_t outgoing_offset = 0;
    bool data_event_pending = false;
    bool writable_event_pending = false;
    std::vector<uint8_t> wire_input;
    std::vector<uint8_t> ws_fragment;
    uint8_t ws_fragment_opcode = 0;

    ~TransportResource() override {
        stop_and_join();
        std::lock_guard lock(socket_mutex);
        close_socket(socket);
        socket = invalid_socket;
    }

    bool start() noexcept {
#if NK_ENABLE_NO_EXCEPTIONS
        worker = std::thread([self = shared_from_this()] { self->run(); });
        return true;
#else
        try {
            worker = std::thread([self = shared_from_this()] { self->run_safely(); });
            return true;
        } catch (...) {
            return false;
        }
#endif
    }

    void stop_and_join() noexcept {
        stopping.store(true, std::memory_order_release);
        condition.notify_all();
        {
            std::lock_guard lock(socket_mutex);
            shutdown_socket(socket);
        }
        if (worker.joinable() && worker.get_id() != std::this_thread::get_id())
            worker.join();
    }

    void run_safely() noexcept {
#if NK_ENABLE_NO_EXCEPTIONS
        run();
#else
        try {
            run();
        } catch (...) {
            finish(NK_ERROR_UNKNOWN);
        }
#endif
    }

    void emit_data() noexcept;
    void emit_writable() noexcept;
    void emit_connected() noexcept;
    void receive_application(const uint8_t *data, std::size_t size) noexcept;
    bool parse_websocket_frames() noexcept;
    bool read_socket() noexcept;
    bool write_socket() noexcept;
    nk_result websocket_handshake() noexcept;
    nk_result open_client_socket() noexcept;
    void finish(nk_result result) noexcept;
    void run() noexcept;

    bool read_http_headers(std::string &headers) noexcept {
        const auto deadline = clock_type::now() + std::chrono::milliseconds(options.timeout_ms);
        for (;;) {
            const auto position =
                std::search(wire_input.begin(), wire_input.end(), "\r\n\r\n", "\r\n\r\n" + 4);
            if (position != wire_input.end()) {
                const auto end = static_cast<std::size_t>(position - wire_input.begin()) + 4;
                headers.assign(reinterpret_cast<const char *>(wire_input.data()), end);
                wire_input.erase(wire_input.begin(),
                                 wire_input.begin() + static_cast<std::ptrdiff_t>(end));
                return true;
            }
            if (wire_input.size() > max_http_header)
                return false;
            if (clock_type::now() >= deadline)
                return false;
            if (wait_socket(socket, true, false, 50) <= 0)
                continue;
            std::array<uint8_t, read_chunk> bytes{};
#if defined(_WIN32)
            const int count = ::recv(socket, reinterpret_cast<char *>(bytes.data()),
                                     static_cast<int>(bytes.size()), 0);
#else
            const ssize_t count = ::recv(socket, bytes.data(), bytes.size(), 0);
#endif
            if (count <= 0)
                return false;
            wire_input.insert(wire_input.end(), bytes.begin(),
                              bytes.begin() + static_cast<std::ptrdiff_t>(count));
        }
    }
};

class ListenerResource final : public nk::core::Resource,
                               public std::enable_shared_from_this<ListenerResource> {
  public:
    TransportOptions options;
    uint64_t generation = 0;
    nk_listener handle = NK_INVALID_HANDLE;
    socket_type socket = invalid_socket;
    std::string local_path;
    std::atomic<bool> stopping{false};
    std::mutex socket_mutex;
    std::thread worker;

    ~ListenerResource() override {
        stop_and_join();
        std::lock_guard lock(socket_mutex);
        close_socket(socket);
        socket = invalid_socket;
        if (!local_path.empty()) {
#if !defined(_WIN32)
            ::unlink(local_path.c_str());
#endif
        }
    }

    bool start() noexcept {
#if NK_ENABLE_NO_EXCEPTIONS
        worker = std::thread([self = shared_from_this()] { self->run(); });
        return true;
#else
        try {
            worker = std::thread([self = shared_from_this()] { self->run_safely(); });
            return true;
        } catch (...) {
            return false;
        }
#endif
    }

    void stop_and_join() noexcept {
        stopping.store(true, std::memory_order_release);
        {
            std::lock_guard lock(socket_mutex);
            shutdown_socket(socket);
        }
        if (worker.joinable() && worker.get_id() != std::this_thread::get_id())
            worker.join();
    }

    void run_safely() noexcept {
#if NK_ENABLE_NO_EXCEPTIONS
        run();
#else
        try {
            run();
        } catch (...) {
            emit_failure(NK_ERROR_UNKNOWN);
        }
#endif
    }

    void emit_failure(nk_result result) noexcept;
    void run() noexcept;
};

bool emit_simple(nk_event_kind kind, nk_handle source, nk_result result = NK_OK) noexcept {
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = source;
    event.result = result;
    return nk::core::push_event(std::move(event)) == NK_OK;
}

template <typename Payload>
bool emit_payload(nk_event_kind kind, nk_handle source, nk_result result,
                  const Payload &payload) noexcept {
#if NK_ENABLE_NO_EXCEPTIONS
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = source;
    event.result = result;
    event.data.resize(sizeof(payload));
    std::memcpy(event.data.data(), &payload, sizeof(payload));
    return nk::core::push_event(std::move(event)) == NK_OK;
#else
    try {
        nk::core::QueuedEvent event;
        event.kind = kind;
        event.source = source;
        event.result = result;
        event.data.resize(sizeof(payload));
        std::memcpy(event.data.data(), &payload, sizeof(payload));
        return nk::core::push_event(std::move(event)) == NK_OK;
    } catch (...) {
        return false;
    }
#endif
}

std::shared_ptr<TransportResource> get_transport(nk_transport handle) {
    return std::static_pointer_cast<TransportResource>(
        nk::core::handles().get(static_cast<nk_handle>(handle), nk::core::ResourceType::transport));
}

std::shared_ptr<ListenerResource> get_listener(nk_listener handle) {
    return std::static_pointer_cast<ListenerResource>(
        nk::core::handles().get(static_cast<nk_handle>(handle), nk::core::ResourceType::listener));
}

std::vector<uint8_t> websocket_frame(const void *data, std::size_t size, bool mask,
                                     uint8_t opcode) {
    std::vector<uint8_t> result;
    const std::size_t header_size = size < 126 ? 2 : size <= 0xffff ? 4 : 10;
    result.resize(header_size + (mask ? 4 : 0) + size);
    result[0] = static_cast<uint8_t>(0x80 | (opcode & 0x0f));
    std::size_t position = 2;
    if (size < 126) {
        result[1] = static_cast<uint8_t>(size | (mask ? 0x80 : 0));
    } else if (size <= 0xffff) {
        result[1] = static_cast<uint8_t>(126 | (mask ? 0x80 : 0));
        result[2] = static_cast<uint8_t>(size >> 8);
        result[3] = static_cast<uint8_t>(size);
        position = 4;
    } else {
        result[1] = static_cast<uint8_t>(127 | (mask ? 0x80 : 0));
        for (unsigned index = 0; index != 8; ++index)
            result[2 + index] =
                static_cast<uint8_t>(static_cast<uint64_t>(size) >> (56 - index * 8));
        position = 10;
    }
    std::array<uint8_t, 4> mask_key{};
    if (mask) {
        static std::atomic<uint32_t> sequence{0x13579bdf};
        const uint32_t value = sequence.fetch_add(0x9e3779b9, std::memory_order_relaxed);
        mask_key = {static_cast<uint8_t>(value >> 24), static_cast<uint8_t>(value >> 16),
                    static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value)};
        std::memcpy(result.data() + position, mask_key.data(), mask_key.size());
        position += mask_key.size();
    }
    if (size != 0)
        std::memcpy(result.data() + position, data, size);
    if (mask) {
        for (std::size_t index = 0; index != size; ++index)
            result[position + index] ^= mask_key[index % mask_key.size()];
    }
    return result;
}

void queue_control(TransportResource &transport, uint8_t opcode, const void *data,
                   std::size_t size) noexcept {
#if NK_ENABLE_NO_EXCEPTIONS
    if (size > 125)
        return;
    auto frame = websocket_frame(data, size, !transport.server_side, opcode);
    std::lock_guard lock(transport.mutex);
    if (transport.outgoing_bytes + frame.size() <= transport.options.send_buffer_size) {
        transport.outgoing_bytes += frame.size();
        transport.outgoing.push_back(std::move(frame));
        transport.condition.notify_all();
    }
#else
    try {
        if (size > 125)
            return;
        auto frame = websocket_frame(data, size, !transport.server_side, opcode);
        std::lock_guard lock(transport.mutex);
        if (transport.outgoing_bytes + frame.size() <= transport.options.send_buffer_size) {
            transport.outgoing_bytes += frame.size();
            transport.outgoing.push_back(std::move(frame));
            transport.condition.notify_all();
        }
    } catch (...) {
    }
#endif
}

void TransportResource::emit_data() noexcept {
    nk_transport_data_event payload{};
    payload.struct_size = sizeof(payload);
    {
        std::lock_guard lock(mutex);
        if (data_event_pending || incoming_bytes == 0 || state != State::open)
            return;
        payload.available = incoming_bytes;
        data_event_pending = true;
    }
    if (!emit_payload(NK_EVENT_TRANSPORT_DATA, handle, NK_OK, payload)) {
        std::lock_guard lock(mutex);
        data_event_pending = false;
    }
}

void TransportResource::emit_writable() noexcept {
    bool should_emit = false;
    {
        std::lock_guard lock(mutex);
        if (!writable_event_pending && outgoing.empty() && state == State::open) {
            writable_event_pending = true;
            should_emit = true;
        }
    }
    if (should_emit && !emit_simple(NK_EVENT_TRANSPORT_WRITABLE, handle)) {
        std::lock_guard lock(mutex);
        writable_event_pending = false;
    }
}

void TransportResource::emit_connected() noexcept {
    if (auto parent = listener.lock()) {
        nk_transport_accepted_event accepted{};
        accepted.struct_size = sizeof(accepted);
        accepted.transport = handle;
        accepted.kind = options.kind;
        (void)emit_payload(NK_EVENT_TRANSPORT_ACCEPTED, parent->handle, NK_OK, accepted);
    }
    (void)emit_simple(NK_EVENT_TRANSPORT_CONNECTED, handle);
}

void TransportResource::receive_application(const uint8_t *data, std::size_t size) noexcept {
    if (size == 0)
        return;
#if NK_ENABLE_NO_EXCEPTIONS
    {
        std::lock_guard lock(mutex);
        if (state != State::open ||
            size >
                options.receive_buffer_size - std::min(options.receive_buffer_size, incoming_bytes))
            return;
        incoming.emplace_back(data, data + size);
        incoming_bytes += size;
    }
#else
    try {
        std::lock_guard lock(mutex);
        if (state != State::open ||
            size >
                options.receive_buffer_size - std::min(options.receive_buffer_size, incoming_bytes))
            return;
        incoming.emplace_back(data, data + size);
        incoming_bytes += size;
    } catch (...) {
        return;
    }
#endif
    emit_data();
}

bool TransportResource::parse_websocket_frames() noexcept {
    while (wire_input.size() >= 2) {
        const uint8_t first = wire_input[0];
        const uint8_t second = wire_input[1];
        const bool final = (first & 0x80) != 0;
        const uint8_t opcode = first & 0x0f;
        const bool masked = (second & 0x80) != 0;
        uint64_t size = second & 0x7f;
        std::size_t header_size = 2;
        if (size == 126) {
            if (wire_input.size() < 4)
                return true;
            size = (static_cast<uint64_t>(wire_input[2]) << 8) | wire_input[3];
            header_size = 4;
        } else if (size == 127) {
            if (wire_input.size() < 10)
                return true;
            size = 0;
            for (unsigned index = 0; index != 8; ++index)
                size = (size << 8) | wire_input[2 + index];
            header_size = 10;
        }
        if (size > max_message || (opcode >= 8 && (!final || size > 125)))
            return false;
        const std::size_t mask_size = masked ? 4 : 0;
        std::size_t frame_size = 0;
        if (size > std::numeric_limits<std::size_t>::max() - header_size - mask_size ||
            !checked_add(header_size + mask_size, static_cast<std::size_t>(size), frame_size))
            return false;
        if (wire_input.size() < frame_size)
            return true;
        const bool expected_mask = server_side;
        if (masked != expected_mask)
            return false;
        std::array<uint8_t, 4> mask_key{};
        if (masked)
            std::memcpy(mask_key.data(), wire_input.data() + header_size, mask_key.size());
        const std::size_t payload_offset = header_size + mask_size;
        std::vector<uint8_t> payload(static_cast<std::size_t>(size));
        if (size != 0)
            std::memcpy(payload.data(), wire_input.data() + payload_offset,
                        static_cast<std::size_t>(size));
        if (masked) {
            for (std::size_t index = 0; index != payload.size(); ++index)
                payload[index] ^= mask_key[index % mask_key.size()];
        }
        wire_input.erase(wire_input.begin(),
                         wire_input.begin() + static_cast<std::ptrdiff_t>(frame_size));

        if (opcode == 8) {
            queue_control(*this, 8, payload.data(), payload.size());
            return false;
        }
        if (opcode == 9) {
            queue_control(*this, 10, payload.data(), payload.size());
            continue;
        }
        if (opcode == 10)
            continue;
        if (opcode == 0) {
            if (ws_fragment_opcode == 0)
                return false;
            ws_fragment.insert(ws_fragment.end(), payload.begin(), payload.end());
            if (final) {
                receive_application(ws_fragment.data(), ws_fragment.size());
                ws_fragment.clear();
                ws_fragment_opcode = 0;
            }
            continue;
        }
        if (opcode != 1 && opcode != 2)
            return false;
        if (ws_fragment_opcode != 0)
            return false;
        if (final) {
            receive_application(payload.data(), payload.size());
        } else {
            ws_fragment_opcode = opcode;
            ws_fragment = std::move(payload);
        }
    }
    return true;
}

bool TransportResource::read_socket() noexcept {
    if (options.kind == NK_TRANSPORT_UDP && udp_server) {
        std::array<uint8_t, read_chunk> bytes{};
        sockaddr_storage peer{};
        socket_length_type peer_size = sizeof(peer);
#if defined(_WIN32)
        const int count = ::recvfrom(socket, reinterpret_cast<char *>(bytes.data()),
                                     static_cast<int>(bytes.size()), 0,
                                     reinterpret_cast<sockaddr *>(&peer), &peer_size);
#else
        const ssize_t count = ::recvfrom(socket, bytes.data(), bytes.size(), 0,
                                         reinterpret_cast<sockaddr *>(&peer), &peer_size);
#endif
        if (count > 0) {
            {
                std::lock_guard lock(mutex);
                std::memcpy(&udp_peer.storage, &peer, peer_size);
                udp_peer.size = peer_size;
                udp_peer_valid = true;
            }
            receive_application(bytes.data(), static_cast<std::size_t>(count));
            return true;
        }
        return count == 0 || socket_would_block(socket_error()) ||
               socket_interrupted(socket_error());
    }

    std::array<uint8_t, read_chunk> bytes{};
#if defined(_WIN32)
    const int count =
        ::recv(socket, reinterpret_cast<char *>(bytes.data()), static_cast<int>(bytes.size()), 0);
#else
    const ssize_t count = ::recv(socket, bytes.data(), bytes.size(), 0);
#endif
    if (count > 0) {
        if (options.kind == NK_TRANSPORT_WEBSOCKET) {
            wire_input.insert(wire_input.end(), bytes.begin(),
                              bytes.begin() + static_cast<std::ptrdiff_t>(count));
            return parse_websocket_frames();
        }
        receive_application(bytes.data(), static_cast<std::size_t>(count));
        return true;
    }
    if (count == 0)
        return false;
    if (socket_interrupted(socket_error()) || socket_would_block(socket_error()))
        return true;
    return false;
}

bool TransportResource::write_socket() noexcept {
    std::unique_lock lock(mutex);
    while (!outgoing.empty()) {
        auto &chunk = outgoing.front();
        const auto remaining = chunk.size() - outgoing_offset;
        lock.unlock();
#if defined(_WIN32)
        int sent = 0;
        if (options.kind == NK_TRANSPORT_UDP && udp_server && udp_peer_valid) {
            sent = ::sendto(socket, reinterpret_cast<const char *>(chunk.data() + outgoing_offset),
                            static_cast<int>(remaining), 0,
                            reinterpret_cast<const sockaddr *>(&udp_peer.storage), udp_peer.size);
        } else {
            sent = ::send(socket, reinterpret_cast<const char *>(chunk.data() + outgoing_offset),
                          static_cast<int>(remaining), 0);
        }
#else
        const ssize_t sent =
            options.kind == NK_TRANSPORT_UDP && udp_server && udp_peer_valid
                ? ::sendto(socket, chunk.data() + outgoing_offset, remaining, 0,
                           reinterpret_cast<const sockaddr *>(&udp_peer.storage), udp_peer.size)
                : ::send(socket, chunk.data() + outgoing_offset, remaining, send_flags);
#endif
        const auto error = sent < 0 ? socket_error() : 0;
        lock.lock();
        if (sent > 0) {
            outgoing_offset += static_cast<std::size_t>(sent);
            if (outgoing_offset == chunk.size()) {
                outgoing_bytes -= chunk.size();
                outgoing.pop_front();
                outgoing_offset = 0;
            }
            continue;
        }
        if (sent < 0 && (socket_would_block(error) || socket_interrupted(error)))
            return true;
        return false;
    }
    const bool notify = !writable_event_pending;
    lock.unlock();
    if (notify)
        emit_writable();
    return true;
}

nk_result TransportResource::open_client_socket() noexcept {
    socket_type connected = invalid_socket;
    const auto result = connect_socket(options, connected);
    if (result != NK_OK)
        return result;
    std::lock_guard lock(socket_mutex);
    socket = connected;
    return NK_OK;
}

nk_result TransportResource::websocket_handshake() noexcept {
    if (server_side) {
        std::string request;
        if (!read_http_headers(request))
            return NK_TRANSPORT_ERROR_PROTOCOL;
        const auto key = header_value(request, "sec-websocket-key");
        const auto upgrade = header_value(request, "upgrade");
        const auto connection = header_value(request, "connection");
        if (request.rfind("GET ", 0) != 0 || key.empty() ||
            !header_has_token(upgrade, "websocket") || !header_has_token(connection, "upgrade"))
            return NK_TRANSPORT_ERROR_PROTOCOL;
        const auto accept = websocket_accept(key);
        const std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                                     "Upgrade: websocket\r\n"
                                     "Connection: Upgrade\r\n"
                                     "Sec-WebSocket-Accept: " +
                                     accept + "\r\n\r\n";
        if (!send_all(socket, response.data(), response.size(), options.timeout_ms, stopping))
            return NK_TRANSPORT_ERROR_CONNECTION;
        if (!parse_websocket_frames())
            return static_cast<nk_result>(NK_TRANSPORT_ERROR_PROTOCOL);
        return NK_OK;
    }

    const std::string key = "dGhlIHNhbXBsZSBub25jZQ==";
    const std::string host = options.host + ":" + std::to_string(options.port);
    const std::string request = "GET " + options.path +
                                " HTTP/1.1\r\n"
                                "Host: " +
                                host +
                                "\r\n"
                                "Upgrade: websocket\r\n"
                                "Connection: Upgrade\r\n"
                                "Sec-WebSocket-Key: " +
                                key +
                                "\r\n"
                                "Sec-WebSocket-Version: 13\r\n\r\n";
    if (!send_all(socket, request.data(), request.size(), options.timeout_ms, stopping))
        return NK_TRANSPORT_ERROR_CONNECTION;
    std::string response;
    if (!read_http_headers(response))
        return NK_TRANSPORT_ERROR_PROTOCOL;
    if (response.rfind("HTTP/1.1 101", 0) != 0 ||
        !header_has_token(header_value(response, "upgrade"), "websocket") ||
        !header_has_token(header_value(response, "connection"), "upgrade") ||
        header_value(response, "sec-websocket-accept") != websocket_accept(key))
        return NK_TRANSPORT_ERROR_PROTOCOL;
    if (!parse_websocket_frames())
        return static_cast<nk_result>(NK_TRANSPORT_ERROR_PROTOCOL);
    return NK_OK;
}

void TransportResource::finish(nk_result result) noexcept {
    {
        std::lock_guard lock(mutex);
        if (state == State::closed)
            return;
        state = State::closed;
        terminal_result = result;
    }
    {
        std::lock_guard lock(socket_mutex);
        shutdown_socket(socket);
        close_socket(socket);
        socket = invalid_socket;
    }
    if (result != NK_OK && result != NK_TRANSPORT_ERROR_CANCELED)
        (void)emit_simple(NK_EVENT_TRANSPORT_FAILED, handle, result);
    (void)emit_simple(NK_EVENT_TRANSPORT_CLOSED, handle,
                      result == NK_OK ? NK_TRANSPORT_ERROR_CLOSED : result);
}

void TransportResource::run() noexcept {
    nk_result result = NK_OK;
    if (!server_side && !udp_server) {
        result = open_client_socket();
        if (result == NK_OK && options.kind == NK_TRANSPORT_WEBSOCKET)
            result = websocket_handshake();
    } else if (server_side && options.kind == NK_TRANSPORT_WEBSOCKET) {
        result = websocket_handshake();
    }
    if (result != NK_OK) {
        finish(result);
        return;
    }
    {
        std::lock_guard lock(mutex);
        state = State::open;
    }
    emit_connected();
    emit_writable();

    while (!stopping.load(std::memory_order_acquire)) {
        bool has_output = false;
        {
            std::lock_guard lock(mutex);
            has_output = !outgoing.empty();
        }
        const auto ready = wait_socket(socket, true, has_output, 100);
        if (stopping.load(std::memory_order_acquire))
            break;
        if (ready < 0) {
            result = NK_TRANSPORT_ERROR_CONNECTION;
            break;
        }
        if ((ready & 1) != 0 && !read_socket()) {
            result = NK_TRANSPORT_ERROR_CLOSED;
            break;
        }
        if ((ready & 2) != 0 && !write_socket()) {
            result = NK_TRANSPORT_ERROR_CONNECTION;
            break;
        }
    }
    if (stopping.load(std::memory_order_acquire))
        result = NK_TRANSPORT_ERROR_CANCELED;
    finish(result);
}

void ListenerResource::emit_failure(nk_result result) noexcept {
    (void)emit_simple(NK_EVENT_TRANSPORT_FAILED, handle, result);
}

void ListenerResource::run() noexcept {
    if (options.kind == NK_TRANSPORT_UDP) {
        for (;;) {
            if (stopping.load(std::memory_order_acquire))
                return;
            const auto ready = wait_socket(socket, true, false, 100);
            if (ready < 0) {
                emit_failure(NK_TRANSPORT_ERROR_CONNECTION);
                return;
            }
            if (ready == 0)
                continue;
            std::array<uint8_t, max_udp_datagram> probe{};
            sockaddr_storage peer{};
            socket_length_type peer_size = sizeof(peer);
#if defined(_WIN32)
            const int count = ::recvfrom(socket, reinterpret_cast<char *>(probe.data()),
                                         static_cast<int>(probe.size()), MSG_PEEK,
                                         reinterpret_cast<sockaddr *>(&peer), &peer_size);
#else
            const ssize_t count = ::recvfrom(socket, probe.data(), probe.size(), MSG_PEEK,
                                             reinterpret_cast<sockaddr *>(&peer), &peer_size);
#endif
            if (count < 0) {
                if (socket_would_block(socket_error()) || socket_interrupted(socket_error()))
                    continue;
                emit_failure(NK_TRANSPORT_ERROR_CONNECTION);
                return;
            }
            auto transport = std::make_shared<TransportResource>();
            transport->options = options;
            transport->generation = generation;
            transport->server_side = true;
            transport->udp_server = true;
            transport->listener = shared_from_this();
            transport->listener_handle = handle;
            transport->udp_peer.storage = peer;
            transport->udp_peer.size = peer_size;
            transport->udp_peer_valid = true;
            transport->socket = socket;
            socket = invalid_socket;
            const auto transport_handle =
                nk::core::handles().insert(nk::core::ResourceType::transport, transport);
            transport->handle = static_cast<nk_transport>(transport_handle);
            if (transport_handle == NK_INVALID_HANDLE || !transport->start()) {
                if (transport_handle != NK_INVALID_HANDLE)
                    nk::core::handles().erase(transport_handle, nk::core::ResourceType::transport);
                close_socket(transport->socket);
                transport->socket = invalid_socket;
                emit_failure(NK_ERROR_OUT_OF_MEMORY);
                return;
            }
            return;
        }
    }

    while (!stopping.load(std::memory_order_acquire)) {
        const auto ready = wait_socket(socket, true, false, 100);
        if (stopping.load(std::memory_order_acquire))
            return;
        if (ready < 0) {
            emit_failure(NK_TRANSPORT_ERROR_CONNECTION);
            return;
        }
        if (ready == 0)
            continue;
        sockaddr_storage address{};
        socket_length_type address_size = sizeof(address);
        const auto accepted_socket =
            ::accept(socket, reinterpret_cast<sockaddr *>(&address), &address_size);
        if (accepted_socket == invalid_socket) {
            if (socket_would_block(socket_error()) || socket_interrupted(socket_error()))
                continue;
            emit_failure(NK_TRANSPORT_ERROR_CONNECTION);
            return;
        }
        if (!set_nonblocking(accepted_socket)) {
            close_socket(accepted_socket);
            continue;
        }
        auto transport = std::make_shared<TransportResource>();
        transport->options = options;
        transport->generation = generation;
        transport->server_side = true;
        transport->listener = shared_from_this();
        transport->listener_handle = handle;
        transport->socket = accepted_socket;
        const auto transport_handle =
            nk::core::handles().insert(nk::core::ResourceType::transport, transport);
        transport->handle = static_cast<nk_transport>(transport_handle);
        if (transport_handle == NK_INVALID_HANDLE || !transport->start()) {
            if (transport_handle != NK_INVALID_HANDLE)
                nk::core::handles().erase(transport_handle, nk::core::ResourceType::transport);
            close_socket(accepted_socket);
            continue;
        }
    }
}

} // namespace

namespace nk::transport {
nk_capabilities capabilities() noexcept {
    return NK_CAP_TRANSPORT;
}

void shutdown() noexcept {
    const auto listeners = nk::core::handles().handles_of_type(nk::core::ResourceType::listener);
    for (const auto handle : listeners) {
        if (auto listener = std::static_pointer_cast<ListenerResource>(
                nk::core::handles().get(handle, nk::core::ResourceType::listener))) {
            listener->stop_and_join();
            nk::core::handles().erase(handle, nk::core::ResourceType::listener);
        }
    }
    const auto transports = nk::core::handles().handles_of_type(nk::core::ResourceType::transport);
    for (const auto handle : transports) {
        if (auto transport = std::static_pointer_cast<TransportResource>(
                nk::core::handles().get(handle, nk::core::ResourceType::transport))) {
            transport->stop_and_join();
            nk::core::handles().erase(handle, nk::core::ResourceType::transport);
        }
    }
}
} // namespace nk::transport

extern "C" {

nk_result NK_CALL nk_transport_connect(const nk_transport_options *options,
                                       nk_transport *out_transport) {
    return nk::core::result_boundary("transport connect", [&]() -> nk_result {
        if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
            return thread;
        if (!out_transport)
            return fail(NK_ERROR_INVALID_ARGUMENT, "transport output is null");
        *out_transport = NK_INVALID_HANDLE;
        TransportOptions copied;
        if (!copy_options(options, copied, false))
            return fail(NK_ERROR_INVALID_ARGUMENT, "transport options are invalid");
        auto transport = std::make_shared<TransportResource>();
        transport->options = std::move(copied);
        transport->generation = nk::core::runtime_generation();
        const auto handle =
            nk::core::handles().insert(nk::core::ResourceType::transport, transport);
        if (handle == NK_INVALID_HANDLE)
            return fail(NK_ERROR_OUT_OF_MEMORY, "transport handle allocation failed");
        transport->handle = static_cast<nk_transport>(handle);
        if (!transport->start()) {
            nk::core::handles().erase(handle, nk::core::ResourceType::transport);
            return fail(NK_ERROR_OUT_OF_MEMORY, "transport worker creation failed");
        }
        *out_transport = transport->handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_transport_listen(const nk_transport_options *options,
                                      nk_listener *out_listener) {
    return nk::core::result_boundary("transport listen", [&]() -> nk_result {
        if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
            return thread;
        if (!out_listener)
            return fail(NK_ERROR_INVALID_ARGUMENT, "listener output is null");
        *out_listener = NK_INVALID_HANDLE;
        TransportOptions copied;
        if (!copy_options(options, copied, true))
            return fail(NK_ERROR_INVALID_ARGUMENT, "listener options are invalid");
        auto listener = std::make_shared<ListenerResource>();
        listener->options = std::move(copied);
        listener->generation = nk::core::runtime_generation();
        nk_result result =
            create_listener_socket(listener->options, listener->socket, listener->local_path);
        if (result != NK_OK)
            return fail(result, "listener socket creation failed");
        const auto handle = nk::core::handles().insert(nk::core::ResourceType::listener, listener);
        if (handle == NK_INVALID_HANDLE) {
            close_socket(listener->socket);
            listener->socket = invalid_socket;
            return fail(NK_ERROR_OUT_OF_MEMORY, "listener handle allocation failed");
        }
        listener->handle = static_cast<nk_listener>(handle);
        if (!listener->start()) {
            nk::core::handles().erase(handle, nk::core::ResourceType::listener);
            close_socket(listener->socket);
            listener->socket = invalid_socket;
            return fail(NK_ERROR_OUT_OF_MEMORY, "listener worker creation failed");
        }
        *out_listener = listener->handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_transport_send(nk_transport handle, const void *data, uint64_t size) {
    return nk::core::result_boundary("transport send", [&]() -> nk_result {
        if (size != 0 && !data)
            return fail(NK_ERROR_INVALID_ARGUMENT, "transport data is null");
        if (size > max_message)
            return fail(NK_ERROR_PAYLOAD_TOO_LARGE, "transport payload is too large");
        auto transport = get_transport(handle);
        if (!transport)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid transport handle");

#if NK_ENABLE_NO_EXCEPTIONS
        std::vector<uint8_t> payload(static_cast<std::size_t>(size));
#else
        std::vector<uint8_t> payload;
        try {
            payload.resize(static_cast<std::size_t>(size));
        } catch (...) {
            return fail(NK_ERROR_OUT_OF_MEMORY, "transport payload allocation failed");
        }
#endif
        if (size != 0)
            std::memcpy(payload.data(), data, static_cast<std::size_t>(size));
        if (transport->options.kind == NK_TRANSPORT_WEBSOCKET)
            payload = websocket_frame(payload.data(), payload.size(), !transport->server_side, 2);

        std::lock_guard lock(transport->mutex);
        if (transport->state == TransportResource::State::closed)
            return fail(NK_ERROR_INVALID_REQUEST, "transport is closed");
        if (payload.size() > transport->options.send_buffer_size ||
            transport->outgoing_bytes > transport->options.send_buffer_size - payload.size())
            return fail(NK_ERROR_QUEUE_FULL, "transport send queue is full");
        transport->outgoing_bytes += payload.size();
        transport->outgoing.push_back(std::move(payload));
        transport->condition.notify_all();
        return NK_OK;
    });
}

nk_result NK_CALL nk_transport_receive(nk_transport handle, void *data, uint64_t size,
                                       uint64_t *out_received) {
    return nk::core::result_boundary("transport receive", [&]() -> nk_result {
        if (!out_received || (size != 0 && !data))
            return fail(NK_ERROR_INVALID_ARGUMENT, "transport receive arguments are invalid");
        *out_received = 0;
        auto transport = get_transport(handle);
        if (!transport)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid transport handle");
        std::lock_guard lock(transport->mutex);
        if (transport->incoming.empty()) {
            if (transport->state == TransportResource::State::closed)
                return transport->terminal_result == NK_OK ? NK_OK : transport->terminal_result;
            return NK_TRANSPORT_ERROR_WOULD_BLOCK;
        }
        auto &chunk = transport->incoming.front();
        const auto amount = std::min<std::size_t>(static_cast<std::size_t>(size), chunk.size());
        if (amount != 0)
            std::memcpy(data, chunk.data(), amount);
        *out_received = amount;
        transport->incoming_bytes -= amount;
        if (amount == chunk.size()) {
            transport->incoming.pop_front();
        } else {
            chunk.erase(chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(amount));
        }
        if (transport->incoming.empty())
            transport->data_event_pending = false;
        transport->condition.notify_all();
        return NK_OK;
    });
}

nk_result NK_CALL nk_transport_close(nk_transport handle) {
    return nk::core::result_boundary("transport close", [&]() -> nk_result {
        auto transport = get_transport(handle);
        if (!transport)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid transport handle");
        transport->stop_and_join();
        if (!nk::core::handles().erase(static_cast<nk_handle>(handle),
                                       nk::core::ResourceType::transport))
            return fail(NK_ERROR_INVALID_HANDLE, "invalid transport handle");
        return NK_OK;
    });
}

nk_result NK_CALL nk_listener_close(nk_listener handle) {
    return nk::core::result_boundary("listener close", [&]() -> nk_result {
        auto listener = get_listener(handle);
        if (!listener)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid listener handle");
        listener->stop_and_join();
        if (!nk::core::handles().erase(static_cast<nk_handle>(handle),
                                       nk::core::ResourceType::listener))
            return fail(NK_ERROR_INVALID_HANDLE, "invalid listener handle");
        return NK_OK;
    });
}

nk_result NK_CALL nk_transport_event_data(const nk_event *event,
                                          nk_transport_data_event *out_data) {
    if (!event || !out_data || out_data->struct_size < sizeof(*out_data) ||
        event->kind != NK_EVENT_TRANSPORT_DATA || !event->data ||
        event->data_size != sizeof(nk_transport_data_event))
        return fail(NK_ERROR_INVALID_ARGUMENT, "transport data event is invalid");
    std::memcpy(out_data, event->data, sizeof(*out_data));
    if (out_data->struct_size < sizeof(*out_data))
        return fail(NK_ERROR_INVALID_ARGUMENT, "transport data payload is malformed");
    out_data->struct_size = sizeof(*out_data);
    return NK_OK;
}

nk_result NK_CALL nk_transport_event_accepted(const nk_event *event,
                                              nk_transport_accepted_event *out_accepted) {
    if (!event || !out_accepted || out_accepted->struct_size < sizeof(*out_accepted) ||
        event->kind != NK_EVENT_TRANSPORT_ACCEPTED || !event->data ||
        event->data_size != sizeof(nk_transport_accepted_event))
        return fail(NK_ERROR_INVALID_ARGUMENT, "transport accepted event is invalid");
    std::memcpy(out_accepted, event->data, sizeof(*out_accepted));
    if (out_accepted->struct_size < sizeof(*out_accepted) || !valid_kind(out_accepted->kind) ||
        out_accepted->transport == NK_INVALID_HANDLE)
        return fail(NK_ERROR_INVALID_ARGUMENT, "transport accepted payload is malformed");
    out_accepted->struct_size = sizeof(*out_accepted);
    return NK_OK;
}

} // extern "C"

#endif
