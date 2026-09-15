#ifndef NATIVEKIT_NET_H
#define NATIVEKIT_NET_H

#include "nativekit.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Handles, methods, and policies                                            */
/* ------------------------------------------------------------------------- */

/** A configured HTTP session. */
typedef uint32_t nk_http_client NK_HANDLE NK_HANDLE_DESTROY(nk_http_client_destroy);
/** A pull-based streaming HTTP response body. */
typedef uint32_t nk_http_stream NK_HANDLE NK_HANDLE_DESTROY(nk_http_stream_close);

typedef uint32_t nk_http_method;
typedef uint32_t nk_http_request_mode;
typedef uint32_t nk_http_proxy_kind;
typedef uint32_t nk_http_cookie_policy;
typedef uint32_t nk_http_cache_policy;
typedef uint32_t nk_http_tls_version;

enum NK_ENUM(nk_http_method) {
    NK_HTTP_METHOD_GET = 1,
    NK_HTTP_METHOD_POST = 2,
    NK_HTTP_METHOD_PUT = 3,
    NK_HTTP_METHOD_PATCH = 4,
    NK_HTTP_METHOD_DELETE = 5,
    NK_HTTP_METHOD_HEAD = 6,
    NK_HTTP_METHOD_OPTIONS = 7,
    NK_HTTP_METHOD_TRACE = 8,
    NK_HTTP_METHOD_CONNECT = 9
};

enum NK_ENUM(nk_http_request_mode) {
    /** Buffer the complete response and expose it from the completion event. */
    NK_HTTP_REQUEST_BUFFERED = 0,
    /** Expose the response body through nk_http_stream. */
    NK_HTTP_REQUEST_STREAMING = 1
};

enum NK_ENUM(nk_http_proxy_kind) {
    NK_HTTP_PROXY_NONE = 0,
    NK_HTTP_PROXY_HTTP = 1,
    NK_HTTP_PROXY_HTTPS = 2,
    NK_HTTP_PROXY_SOCKS5 = 3
};

enum NK_ENUM(nk_http_cookie_policy) {
    /** Do not retain or send cookies. */
    NK_HTTP_COOKIES_DISABLED = 0,
    /** Retain cookies in this client for its lifetime only. */
    NK_HTTP_COOKIES_SESSION = 1,
    /** Retain cookies across client lifetimes; not enabled by default. */
    NK_HTTP_COOKIES_PERSISTENT = 2
};

enum NK_ENUM(nk_http_cache_policy) {
    /** Do not use an HTTP response cache. */
    NK_HTTP_CACHE_DISABLED = 0,
    /** Use an in-memory cache owned by this client. */
    NK_HTTP_CACHE_MEMORY = 1,
    /** Use a persistent cache owned by the application. */
    NK_HTTP_CACHE_PERSISTENT = 2
};

enum NK_ENUM(nk_http_tls_version) {
    NK_HTTP_TLS_DEFAULT = 0,
    NK_HTTP_TLS_1_2 = 12,
    NK_HTTP_TLS_1_3 = 13
};

/** Client flags. HTTP is deliberately opt-in for a client. */
enum {
    /** Permit plain http:// URLs. */
    NK_HTTP_CLIENT_ALLOW_HTTP = 1u << 0,
    /** Permit an HTTPS response to redirect down to plain HTTP. */
    NK_HTTP_CLIENT_ALLOW_HTTPS_TO_HTTP = 1u << 1
};

/** TLS flags. Verification is enabled when neither disable flag is present. */
enum {
    NK_HTTP_TLS_DISABLE_PEER_VERIFICATION = 1u << 0,
    NK_HTTP_TLS_DISABLE_HOSTNAME_VERIFICATION = 1u << 1
};

/** Stream state flags returned by nk_http_stream_info_get(). */
enum {
    NK_HTTP_STREAM_DATA_AVAILABLE = 1u << 0,
    NK_HTTP_STREAM_COMPLETE = 1u << 1,
    NK_HTTP_STREAM_CLOSED = 1u << 2,
    NK_HTTP_STREAM_FAILED = 1u << 3
};

/** Response metadata flags. */
enum {
    NK_HTTP_RESPONSE_REDIRECTED = 1u << 0,
    NK_HTTP_RESPONSE_FROM_CACHE = 1u << 1,
    NK_HTTP_RESPONSE_HAS_BODY = 1u << 2
};

/** Value used when the response length is not known in advance. */
#define NK_HTTP_CONTENT_LENGTH_UNKNOWN UINT64_MAX

/* ------------------------------------------------------------------------- */
/* Request and response structures                                           */
/* ------------------------------------------------------------------------- */

/** UTF-8 header view. Sizes exclude a trailing NUL and are authoritative. */
typedef struct nk_http_header {
    const char *name NK_UTF8;
    uint32_t name_size;
    const char *value NK_UTF8;
    uint32_t value_size;
} nk_http_header;

/** Explicit proxy configuration. Empty proxy URL means no proxy. */
typedef struct nk_http_proxy_options {
    uint32_t struct_size NK_STRUCT_SIZE;
    nk_http_proxy_kind kind;
    const char *url NK_NULLABLE_UTF8;
    const char *username NK_NULLABLE_UTF8;
    const char *password NK_NULLABLE_UTF8;
    uint64_t reserved[2];
} nk_http_proxy_options;

/** TLS policy shared by backends where the selected setting is supported. */
typedef struct nk_http_tls_options {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint32_t flags;
    nk_http_tls_version minimum_version;
    const char *ca_bundle_path NK_NULLABLE_UTF8;
    uint64_t reserved[2];
} nk_http_tls_options;

/** Session configuration copied by nk_http_client_create(). */
typedef struct nk_http_client_options {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint32_t flags;
    uint32_t redirect_limit;
    uint32_t timeout_ms;
    uint64_t max_response_size;
    uint64_t max_header_size;
    uint64_t stream_buffer_size;
    const nk_http_header *default_headers NK_IN_ARRAY(default_header_count);
    uint32_t default_header_count;
    /** Optional proxy configuration copied before return. */
    const nk_http_proxy_options * NK_NULLABLE proxy;
    nk_http_cookie_policy cookie_policy;
    nk_http_cache_policy cache_policy;
    /** Optional TLS configuration copied before return. */
    const nk_http_tls_options * NK_NULLABLE tls;
    uint64_t reserved[2];
} nk_http_client_options;

/** Request configuration copied before nk_http_request() returns. */
typedef struct nk_http_request_options {
    uint32_t struct_size NK_STRUCT_SIZE;
    nk_http_method method;
    const char *url NK_UTF8;
    const nk_http_header *headers NK_IN_ARRAY(header_count);
    uint32_t header_count;
    const void *body NK_BORROWED_BUFFER(body_size);
    uint64_t body_size;
    /** Optional readable NativeKit resource stream used instead of body. */
    nk_resource_stream upload_stream;
    uint64_t upload_size;
    nk_http_request_mode mode;
    uint32_t redirect_limit;
    uint32_t timeout_ms;
    uint64_t max_response_size;
    uint64_t stream_buffer_size;
    uint64_t reserved[2];
} nk_http_request_options;

/** Response view valid until the event passed to nk_http_event_response() is released. */
typedef struct nk_http_response {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint32_t status_code;
    uint32_t flags;
    uint32_t header_count;
    uint64_t content_length;
    nk_http_stream stream;
    uint32_t reserved0;
    const void *headers NK_BORROWED_BUFFER(headers_size);
    uint64_t headers_size;
    const void *body NK_BORROWED_BUFFER(body_size);
    uint64_t body_size;
    uint64_t reserved[2];
} nk_http_response;

/** Pull-stream state and transfer outcome. */
typedef struct nk_http_stream_info {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint32_t flags;
    uint64_t available;
    uint64_t received;
    uint64_t total;
    nk_result result;
    uint32_t reserved0;
    uint64_t reserved[2];
} nk_http_stream_info;

/** Coalesced transfer progress carried by NK_EVENT_HTTP_PROGRESS. */
typedef struct nk_http_progress {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint64_t downloaded;
    uint64_t download_total;
    uint64_t uploaded;
    uint64_t upload_total;
    uint64_t reserved[2];
} nk_http_progress;

/* Network failures are reported as event.result values. HTTP status failures
 * remain successful responses and are represented by nk_http_response.status_code. */
typedef int32_t nk_http_error;
enum NK_ENUM(nk_http_error) {
    NK_HTTP_ERROR_DNS = -100,
    NK_HTTP_ERROR_CONNECTION = -101,
    NK_HTTP_ERROR_TLS = -102,
    NK_HTTP_ERROR_TIMEOUT = -103,
    NK_HTTP_ERROR_CANCELED = -104,
    NK_HTTP_ERROR_PROTOCOL = -105,
    NK_HTTP_ERROR_RESPONSE_LIMIT = -106,
    NK_HTTP_ERROR_REDIRECT = -107,
    NK_HTTP_ERROR_PROXY = -108,
    NK_HTTP_ERROR_WOULD_BLOCK = -109
};

/* ------------------------------------------------------------------------- */
/* Client, request, stream, and event APIs                                   */
/* ------------------------------------------------------------------------- */

/** Creates a session. All strings and headers are copied before return. */
NK_API nk_result NK_CALL nk_http_client_create(const nk_http_client_options *options,
                                               nk_http_client *out_client NK_OUT NK_OWNED);
/** Destroys a session and cancels its outstanding requests. */
NK_API nk_result NK_CALL nk_http_client_destroy(nk_http_client client);

/**
 * Starts one asynchronous HTTP request. `out_stream` may be null for callers
 * that do not need the streaming handle; it is set to NK_INVALID_HANDLE for a
 * buffered request.
 */
NK_API nk_result NK_CALL nk_http_request(nk_http_client client,
                                         const nk_http_request_options *options,
                                         nk_request_id *out_request NK_OUT,
                                         nk_http_stream *out_stream NK_OUT);
/** Cancels a request; its terminal event will report NK_HTTP_ERROR_CANCELED. */
NK_API nk_result NK_CALL nk_http_cancel(nk_request_id request);
/**
 * Returns the stream handle while the request remains active. Prefer the
 * `out_stream` result from nk_http_request() because a fast request can finish
 * before this lookup is made.
 */
NK_API nk_result NK_CALL nk_http_request_stream(nk_request_id request,
                                                nk_http_stream *out_stream NK_OUT);

/** Gets stream state without consuming data. This call is thread-safe. */
NK_API nk_result NK_CALL nk_http_stream_info_get(nk_http_stream stream,
                                                 nk_http_stream_info *out_info);
/** Reads currently buffered bytes without blocking. Zero means no data or EOF. */
NK_API nk_result NK_CALL nk_http_stream_read(nk_http_stream stream, void *buffer, uint64_t size,
                                             uint64_t *out_read);
/** Closes a stream and cancels its request. This call is thread-safe. */
NK_API nk_result NK_CALL nk_http_stream_close(nk_http_stream stream);

/** Decodes response metadata and body views from an HTTP headers/complete event. */
NK_API nk_result NK_CALL nk_http_event_response(const nk_event *event,
                                                nk_http_response *out_response NK_OUT);
/** Reads the typed progress payload from an HTTP progress event. */
NK_API nk_result NK_CALL nk_http_event_progress(const nk_event *event,
                                                nk_http_progress *out_progress NK_OUT);
/** Reads one response header from a response view. The result is event-owned. */
NK_API nk_result NK_CALL nk_http_response_header(const nk_http_response *response, uint32_t index,
                                                 nk_http_header *out_header NK_OUT);

#ifdef __cplusplus
}
#endif

#endif
