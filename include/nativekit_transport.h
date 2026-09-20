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
    NK_TRANSPORT_NO_DELAY = 1u << 1
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
} nk_transport_options;

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
    NK_TRANSPORT_ERROR_ADDRESS_IN_USE = -207
};

/* ------------------------------------------------------------------------- */
/* Lifecycle and data APIs                                                   */
/* ------------------------------------------------------------------------- */

/** Starts an asynchronous connection and returns its handle immediately. */
NK_API nk_result NK_CALL nk_transport_connect(const nk_transport_options *options,
                                              nk_transport *out_transport NK_OUT NK_OWNED);

/** Binds an endpoint and reports accepted transports through the event queue. */
NK_API nk_result NK_CALL nk_transport_listen(const nk_transport_options *options,
                                             nk_listener *out_listener NK_OUT NK_OWNED);

/** Copies bytes into the bounded asynchronous send queue. This call is thread-safe. */
NK_API nk_result NK_CALL nk_transport_send(nk_transport transport,
                                           const void *data NK_BORROWED_BUFFER(size),
                                           uint64_t size);

/** Reads currently buffered application bytes without blocking. This call is thread-safe. */
NK_API nk_result NK_CALL nk_transport_receive(nk_transport transport, void *data, uint64_t size,
                                              uint64_t *out_received NK_OUT);

/** Stops a transport, joins its worker, and releases its handle. This call is thread-safe. */
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
