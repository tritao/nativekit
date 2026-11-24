#pragma once

#include "nativekit_transport.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

/*
 * Secure WebSocket clients use the TLS stack the platform already provides.
 * Each build compiles exactly one of two shapes:
 *
 * NK_SECURE_WEBSOCKET_STREAM: the platform supplies a verified TLS byte stream
 * (libcurl on Linux, javax.net.ssl on Android). NativeKit runs its own
 * WebSocket handshake and framing over it, as it does for ws://.
 *
 * NK_SECURE_WEBSOCKET_MESSAGES: the platform supplies a whole WebSocket client
 * (WinHTTP on Windows, NSURLSessionWebSocketTask on Apple platforms) that
 * exchanges complete messages.
 *
 * Builds with neither report no secure client capability.
 */

namespace nk::transport {

/** Where a secure WebSocket client connects. */
struct SecureEndpoint {
    std::string host;
    uint16_t port = 443;
    /** Resource path and query; begins with '/'. */
    std::string path;
    /** Comma-separated subprotocol offer; empty offers none. */
    std::string subprotocols;
    uint32_t timeout_ms = 0;
};

/** Whether this system can open secure WebSocket clients. May load the platform TLS library. */
bool secure_client_available() noexcept;

#if defined(NK_SECURE_WEBSOCKET_STREAM)
/**
 * A TLS client connection carried over a local stream socket. The transport
 * writes plaintext to its end of the socket and reads the peer's plaintext from
 * it. Destroying the stream closes the TLS connection and joins its threads;
 * the transport closes its own end first.
 */
class TlsStream {
  public:
    virtual ~TlsStream() = default;
};

/**
 * Connects to the endpoint and verifies the server's certificate and host name.
 * On success the caller owns *out_socket, a non-blocking local stream socket.
 * Returns NK_OK, NK_ERROR_UNSUPPORTED or an NK_TRANSPORT_ERROR_* value.
 */
nk_result open_tls_stream(const SecureEndpoint &endpoint, const std::atomic<bool> &stopping,
                          std::unique_ptr<TlsStream> &out_stream, int &out_socket) noexcept;
#endif

#if defined(NK_SECURE_WEBSOCKET_MESSAGES)
/**
 * A platform WebSocket client over TLS. connect, receive and send block the
 * calling thread; one thread may receive while another sends. cancel may be
 * called from any thread and makes every pending and later call return.
 */
class SecureWebSocket {
  public:
    virtual ~SecureWebSocket() = default;
    /** Opens the connection and completes the handshake. Returns NK_OK or NK_TRANSPORT_ERROR_*. */
    virtual nk_result connect(const SecureEndpoint &endpoint) noexcept = 0;
    /**
     * Waits for the next complete message. Returns NK_TRANSPORT_ERROR_CLOSED when the
     * connection ends and NK_TRANSPORT_ERROR_RECEIVE_OVERFLOW for a message over max_size.
     */
    virtual nk_result receive(std::vector<uint8_t> &message, std::size_t max_size) noexcept = 0;
    /** Sends one complete text or binary message. */
    virtual nk_result send(const uint8_t *data, std::size_t size, bool text) noexcept = 0;
    /** Aborts the connection. */
    virtual void cancel() noexcept = 0;
};

/** Creates the platform client, or returns null when the platform cannot provide one. */
std::unique_ptr<SecureWebSocket> create_secure_websocket() noexcept;
#endif

/** Whether a comma-separated offer contains the token, compared case-insensitively. */
bool subprotocol_offered(std::string_view offer, std::string_view token) noexcept;

} // namespace nk::transport
