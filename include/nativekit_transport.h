#ifndef NATIVEKIT_TRANSPORT_H
#define NATIVEKIT_TRANSPORT_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Handles, kinds, and options                                               */
/* ------------------------------------------------------------------------- */

/** A connected or connecting byte/datagram transport. */
typedef uint32_t nk_transport NK_HANDLE NK_HANDLE_DESTROY(nk_transport_close);
/** A listening endpoint that produces NK_EVENT_TRANSPORT_ACCEPTED events. */
typedef uint32_t nk_listener NK_HANDLE NK_HANDLE_DESTROY(nk_listener_close);

typedef uint32_t nk_transport_kind;
typedef uint32_t nk_transport_flags;
typedef uint32_t nk_transport_capabilities;

enum NK_ENUM(nk_transport_kind) {
    /** Reliable ordered byte stream over an IP socket. */
    NK_TRANSPORT_TCP = 1,
    /** Connected datagrams over an IP socket. */
    NK_TRANSPORT_UDP = 2,
    /** RFC 6455 WebSocket frames over a plain TCP connection. */
    NK_TRANSPORT_WEBSOCKET = 3,
    /** Reliable byte stream over a local operating-system socket. */
    NK_TRANSPORT_LOCAL = 4
};

enum NK_FLAGS(nk_transport_flags) {
    /** Allow a listener to reuse a recently released local address. */
    NK_TRANSPORT_REUSE_ADDRESS = 1u << 0,
    /** Request TCP_NODELAY for TCP and WebSocket connections. */
    NK_TRANSPORT_NO_DELAY = 1u << 1,
    /** Use TLS for a WebSocket client connection (wss://). */
    NK_TRANSPORT_SECURE = 1u << 2
};

enum NK_FLAGS(nk_transport_capabilities) {
    /** The kind can create outgoing connections on this platform. */
    NK_TRANSPORT_CAP_CLIENT = 1u << 0,
    /** The kind can create listeners on this platform. */
    NK_TRANSPORT_CAP_LISTENER = 1u << 1,
    /** Outgoing connections of this kind support NK_TRANSPORT_SECURE. */
    NK_TRANSPORT_CAP_SECURE_CLIENT = 1u << 2,
    /** Listeners of this kind support NK_TRANSPORT_SECURE. */
    NK_TRANSPORT_CAP_SECURE_LISTENER = 1u << 3,
    /** WebSocket subprotocol offers and negotiation are supported. */
    NK_TRANSPORT_CAP_SUBPROTOCOL = 1u << 4
};

/** Native transport configuration. Strings are read only during the call. */
typedef struct nk_transport_options {
    /** Set to sizeof(nk_transport_options) before calling. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Socket or framing protocol to use. */
    nk_transport_kind kind;
    /** Combination of NK_TRANSPORT_* flags. */
    nk_transport_flags flags;
    /** Host name or numeric address for IP transports; nullable when listening. */
    const char *host NK_NULLABLE_UTF8;
    /** TCP/UDP port. A zero port requests an ephemeral port for a listener. */
    uint16_t port;
    /** Reserved; initialize to zero. */
    uint16_t reserved0;
    /** Unix socket path for NK_TRANSPORT_LOCAL, or WebSocket resource path. */
    const char *path NK_NULLABLE_UTF8;
    /** Connection and handshake timeout; zero selects five seconds. */
    uint32_t timeout_ms;
    /** Listener backlog; zero selects the platform default. */
    uint32_t backlog;
    /** Maximum queued application bytes received by this transport. */
    uint64_t receive_buffer_size;
    /** Maximum queued application bytes waiting to be sent. */
    uint64_t send_buffer_size;
    /** Reserved for compatible extensions; initialize to zero. */
    uint64_t reserved[2];
    /**
     * Optional comma-separated WebSocket subprotocol tokens, in preference order.
     * Browsers pass these tokens to the WebSocket constructor. Native listeners select
     * the first client token that they also advertise. Authentication data does not
     * belong here and should be sent as application protocol messages.
     */
    const char *subprotocols NK_NULLABLE_UTF8;
    /** Reserved for compatible extensions; initialize to zero. */
    uint64_t reserved2[2];
} nk_transport_options;

/** Size accepted from callers built against the original transport options ABI. */
#define NK_TRANSPORT_OPTIONS_V1_SIZE ((uint32_t)offsetof(nk_transport_options, subprotocols))

/** Bytes available when NK_EVENT_TRANSPORT_DATA is delivered. */
typedef struct nk_transport_data_event {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint64_t available;
    uint64_t reserved[2];
} nk_transport_data_event;

/** Transport created for an NK_EVENT_TRANSPORT_ACCEPTED event. */
typedef struct nk_transport_accepted_event {
    uint32_t struct_size NK_STRUCT_SIZE;
    nk_transport transport;
    nk_transport_kind kind;
    uint32_t reserved0;
    uint64_t reserved[2];
} nk_transport_accepted_event;

/** Transport-specific asynchronous error values stored in event.result. */
typedef int32_t nk_transport_error;
enum NK_ENUM(nk_transport_error) {
    NK_TRANSPORT_ERROR_DNS = -200,
    NK_TRANSPORT_ERROR_CONNECTION = -201,
    NK_TRANSPORT_ERROR_TIMEOUT = -202,
    NK_TRANSPORT_ERROR_PROTOCOL = -203,
    NK_TRANSPORT_ERROR_CANCELED = -204,
    NK_TRANSPORT_ERROR_WOULD_BLOCK = -205,
    NK_TRANSPORT_ERROR_CLOSED = -206,
    NK_TRANSPORT_ERROR_ADDRESS_IN_USE = -207,
    NK_TRANSPORT_ERROR_RECEIVE_OVERFLOW = -208
};

/* ------------------------------------------------------------------------- */
/* Lifecycle and data APIs                                                   */
/* ------------------------------------------------------------------------- */

/**
 * Reports support for one transport kind without starting an operation.
 * Unknown kinds return NK_ERROR_INVALID_ARGUMENT and clear out_capabilities.
 */
NK_API nk_result NK_CALL nk_transport_query_capabilities(
    nk_transport_kind kind, nk_transport_capabilities *out_capabilities NK_OUT);

/**
 * Starts an asynchronous connection and returns its handle immediately.
 *
 * The call must be made on NativeKit's UI thread. Completion is asynchronous: the first
 * event for the handle is CONNECTED, or FAILED followed by CLOSED. After CONNECTED,
 * DATA and WRITABLE may repeat. There is exactly one CLOSED terminal event. Cancellation
 * produces CLOSED with NK_TRANSPORT_ERROR_CANCELED and no FAILED event. No event is
 * emitted for a handle after its CLOSED event.
 */
NK_API nk_result NK_CALL nk_transport_connect(const nk_transport_options *options,
                                              nk_transport *out_transport NK_OUT NK_OWNED);

/** Binds an endpoint and reports accepted transports through the event queue. */
NK_API nk_result NK_CALL nk_transport_listen(const nk_transport_options *options,
                                             nk_listener *out_listener NK_OUT NK_OWNED);

/**
 * Copies bytes into the bounded asynchronous send queue. This call is thread-safe.
 * TCP, local, and WebSocket transports expose one reliable ordered byte stream; a send
 * does not define a receive boundary. UDP preserves datagram boundaries. WebSocket frame
 * boundaries are deliberately hidden. NK_ERROR_QUEUE_FULL means no bytes were accepted;
 * WRITABLE is emitted after a previously non-empty queue drains. In browsers the limit
 * includes bytes reported by WebSocket.bufferedAmount, but cannot bound buffering held
 * internally by the user agent.
 */
NK_API nk_result NK_CALL nk_transport_send(nk_transport transport,
                                           const void *data NK_BORROWED_BUFFER(size),
                                           uint64_t size);

/**
 * Reads currently buffered application bytes without blocking. This call is thread-safe.
 * DATA reports a snapshot of available bytes and may be coalesced. Call receive until it
 * returns NK_TRANSPORT_ERROR_WOULD_BLOCK. Receive overflow closes the connection with a
 * failure; application bytes are never silently discarded.
 */
NK_API nk_result NK_CALL nk_transport_receive(nk_transport transport, void *data, uint64_t size,
                                              uint64_t *out_received NK_OUT);

/**
 * Cancels connection/negotiation or closes an open connection without disposing its handle.
 * This call is thread-safe and idempotent until the terminal CLOSED event is observed.
 */
NK_API nk_result NK_CALL nk_transport_cancel(nk_transport transport);

/**
 * Cancels if necessary, waits for backend callbacks/workers, and releases the handle.
 * This call is thread-safe. Queued events can still name the numeric disposed handle;
 * callers must not use such events to access the disposed transport.
 */
NK_API nk_result NK_CALL nk_transport_close(nk_transport transport);

/** Stops a listener, joins its accept worker, and releases its handle. */
NK_API nk_result NK_CALL nk_listener_close(nk_listener listener);

/** Decodes the payload of NK_EVENT_TRANSPORT_DATA. */
NK_API nk_result NK_CALL nk_transport_event_data(const nk_event *event,
                                                 nk_transport_data_event *out_data NK_OUT);

/** Decodes the payload of NK_EVENT_TRANSPORT_ACCEPTED. */
NK_API nk_result NK_CALL nk_transport_event_accepted(
    const nk_event *event, nk_transport_accepted_event *out_accepted NK_OUT);

#ifdef __cplusplus
}
#endif

#endif
