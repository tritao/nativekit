# ADR 0011: Optional NativeKit HTTP networking module

## Status

Accepted. The first implementation adds the stable C contract and private
Android, Apple, Windows, Linux, and Web transports. Browser Fetch currently
advertises buffered HTTP only; streaming remains a separate capability until
the WASM bridge can provide non-blocking backpressure.

## Decision

NativeKit owns portable HTTP semantics in an optional `NativeKit::net` module.
The public boundary is `include/nativekit_net.h`; no platform or third-party
networking type crosses it.

The selected transport is:

| Platform | Transport |
| --- | --- |
| Android | `HttpsURLConnection` through JNI |
| iOS/macOS | shared `NSURLSession` Objective-C++ adapter |
| Windows | WinHTTP |
| Linux | pinned libcurl, with an explicit system-libcurl option |
| Web/WASM | Fetch host bridge |

The module exposes configured `nk_http_client` sessions, asynchronous
`nk_request_id` operations, event-owned `nk_http_response` views, and bounded
pull-based `nk_http_stream` responses. HTTP status codes are response data;
transport failures use distinct `NK_HTTP_ERROR_*` values.

## Event and lifecycle contract

Requests use NativeKit's central event queue:

- `NK_EVENT_HTTP_HEADERS` announces final response metadata.
- `NK_EVENT_HTTP_DATA_AVAILABLE` announces readable stream data and is not
  emitted once per network packet.
- `NK_EVENT_HTTP_PROGRESS` is coalesced progress data.
- `NK_EVENT_HTTP_COMPLETE` is the exactly-once terminal event.

Worker threads never call application code. Buffered responses enforce a
maximum response size. Streaming responses apply bounded buffering and block
the transport callback when the application has not drained the stream.

Certificate and hostname verification are enabled by default. Plain HTTP and
HTTPS-to-HTTP redirects require explicit client flags. Cookies and caches are
disabled unless requested, and diagnostics never include credentials.

## Capability contract

`NK_CAP_HTTP_CLIENT` and `NK_CAP_HTTP_STREAMING` are reserved in the common
capability mask. They are advertised by `nk_get_capabilities()` only when the
optional module is built and its selected backend implements the feature;
Fetch advertises only the client bit until streaming is non-blocking on the
browser main thread.

Raw sockets, DNS APIs, server sockets, persistent background transfers,
credential storage, retries, parsing formats, WebSockets, and WebRTC are out
of scope. Background transfers will use a separate persistent `nk_transfer`
API after this module stabilizes.
