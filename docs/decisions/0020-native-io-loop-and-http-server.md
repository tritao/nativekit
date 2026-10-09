# ADR 0020: Native I/O loop on libuv, and an HTTP server

## Status

Proposed. Accepted if the spike at the end of this record passes.

## Context

Every native I/O resource in NativeKit owns a thread today:

| Subsystem | Today |
| --- | --- |
| TCP, WebSocket and local-socket transports and listeners | one thread per connection and per listener, blocking `poll`/`select` |
| Secure WebSocket client (libwebsockets) | one thread per connection, `lws_service` every 25 ms |
| Linux HTTP client (libcurl) | one detached thread per request, `curl_easy_perform` |
| File watching | one thread per watch |
| PTY | one monitor thread per PTY, polling every 20 ms |

Completions reach the application through the event queue (ADR 0002). That
model is simple and fits desktop applications with a handful of resources.

Beartooth needs an HTTP server: a game server's asset endpoint, an asset
service with streamed uploads and watched channels, and Studio play-test
endpoints. That means hundreds to thousands of keep-alive connections,
Server-Sent Events watchers, request bodies consumed while they arrive, and
ranged file responses. A thread per connection doesn't serve that, and ADR
0011 leaves server sockets out of the HTTP client.

## Decision

### libuv is NativeKit's private I/O loop on native targets

- NativeKit vendors and pins libuv. It never appears in the public ABI
  (ADR 0001); applications keep handles, request IDs and events.
- One I/O thread runs the loop. Other threads hand it work with
  `uv_async_send`. Completions are posted to the event queue exactly as
  worker threads post them today, so applications see no change in model.
- Web keeps its Fetch and WebSocket host bridges. libuv is native only.
- CPU work stays on the core worker pool and native tasks (ADR 0012, ADR
  0019), not on libuv's thread pool.
- One libuv per process. A host that also links another copy, such as
  HashLink's `uv.hdll`, builds it against NativeKit's copy or disables it.

### `nk_http_server` is built on libuv and llhttp

- llhttp (Node's HTTP/1.1 parser, MIT) parses requests. NativeKit enforces
  its own limits in llhttp's callbacks: request line, header bytes, header
  count, body size, idle and header timeouts, connections per server.
- HTTP/1.1 only, with keep-alive and pipelining. No TLS and no HTTP/2: a
  reverse proxy or CDN in front terminates them.
- Requests that need the application are announced with
  `NK_EVENT_HTTP_SERVER_REQUEST`. Request bodies are bounded pull streams,
  like `nk_http_stream` (ADR 0011), so a slow application applies
  backpressure instead of buffering without limit.
- Responses are given whole, streamed with bounded buffers, or as a file.
  File responses, with `Range`, are served entirely on the I/O thread
  (`uv_fs_sendfile`), so file bytes never pass through the application's
  heap or thread.
- Server-Sent Events are a streamed response with keep-alive comments.
- A WebSocket upgrade can be handed to `nk_transport` once transports run on
  the same loop, so one port serves HTTP and WebSocket.
- The capability is advertised as `NK_CAP_HTTP_SERVER` only where the native
  loop exists; Web never advertises it.

### Migration of existing subsystems

Each step is independent and lands only when it is measured to be no worse:

1. `nk_http_server`: new code, nothing migrates.
2. TCP, WebSocket and local-socket transports move onto the loop, ending the
   thread per connection. llhttp parses the WebSocket handshake on both sides.
3. The secure WebSocket client runs on the same loop through libwebsockets'
   libuv integration (`LWS_WITH_LIBUV`), or is replaced by a simpler client
   if one proves sufficient. libuv is the only libwebsockets event-loop
   backend NativeKit uses.
4. PTY I/O uses `uv_poll` on the PTY's descriptor, ending the 20 ms poll.
5. The Linux libcurl client may move to the loop through the `multi_socket`
   API. Android, Apple and Windows keep their platform HTTP stacks (ADR 0011).

File watching keeps its own backends: libuv's watcher is not recursive on
Linux. UI events, GPU submission and the core worker pool are unaffected.

## Rejected alternatives

- **An own reactor** over epoll, kqueue and IOCP or WSAPoll. It re-implements
  the hardest parts of libuv (Windows completion ports, timers, wake-ups,
  DNS, ranged `sendfile` on three platforms) and keeps them NativeKit's to
  maintain on every platform.
- **libwebsockets as the HTTP server.** It owns its event loop and
  connection lifecycle behind a large multi-role callback API; bridging that
  into the event queue adds a second runtime model. Its TLS and HTTP/2 are
  the reverse proxy's job here.
- **Exposing libuv handles or loops publicly.** It would break ADR 0001 and
  tie applications to one I/O library.

## Spike

On Linux, Windows and macOS, a prototype `nk_http_server`:

- serves 1,000 keep-alive clients and 1,000 Server-Sent Events watchers from
  one I/O thread;
- consumes uploads as streams and hashes them as they arrive;
- serves `Range` requests for large files with `uv_fs_sendfile`;
- rejects oversized and malformed requests at each limit without affecting
  other connections;
- links into a Haxeon application next to HashLink with a single libuv and no
  duplicate symbols.

Record throughput, latency percentiles, memory per connection and the cost of
the hop between the I/O thread and the event queue. If any criterion fails,
record why and revisit this decision before migrating existing subsystems.

## Consequences

- libuv and llhttp become vendored native dependencies, both MIT.
- The HTTP server is the first libuv user; the other subsystems migrate only
  after the spike, at which point libuv is required on native targets.
- Web behavior and the public event model do not change.
