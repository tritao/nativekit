#include <emscripten/emscripten.h>
#include <emscripten/websocket.h>

extern "C" void nk_websocket_opened(nk_transport handle);
extern "C" int nk_websocket_received(nk_transport handle, const void *data, uint32_t size);
extern "C" void nk_websocket_writable(nk_transport handle);
extern "C" void nk_websocket_terminal(nk_transport handle, nk_result result);

namespace {
constexpr uint64_t web_default_buffer = 4u * 1024u * 1024u;

struct WebTransport final : nk::core::Resource {
    nk_transport handle = NK_INVALID_HANDLE;
    uint64_t generation = 0;
    uint64_t receive_limit = web_default_buffer;
    uint64_t incoming_bytes = 0;
    bool open = false;
    bool closed = false;
    nk_result terminal_result = NK_OK;
    bool data_pending = false;
    bool writable_poll_pending = false;
    EMSCRIPTEN_WEBSOCKET_T socket = 0;
    uint64_t send_limit = web_default_buffer;
    std::deque<std::vector<uint8_t>> incoming;
};

nk_result web_fail(nk_result result, const char *message) {
    nk::core::set_error(message);
    return result;
}

std::shared_ptr<WebTransport> web_get(nk_transport handle) {
    return std::static_pointer_cast<WebTransport>(
        nk::core::handles().get(handle, nk::core::ResourceType::transport));
}

bool web_event(nk_event_kind kind, nk_handle source, nk_result result = NK_OK) {
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = source;
    event.result = result;
    return nk::core::push_event(std::move(event)) == NK_OK;
}

bool web_data_event(const std::shared_ptr<WebTransport> &transport) {
    if (transport->data_pending || !transport->open || !transport->incoming_bytes)
        return true;
    nk_transport_data_event payload{};
    payload.struct_size = sizeof(payload);
    payload.available = transport->incoming_bytes;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_TRANSPORT_DATA;
    event.source = transport->handle;
    event.data.resize(sizeof(payload));
    std::memcpy(event.data.data(), &payload, sizeof(payload));
    if (nk::core::push_event(std::move(event)) != NK_OK)
        return false;
    transport->data_pending = true;
    return true;
}

void *web_user_data(nk_transport handle) {
    return reinterpret_cast<void *>(static_cast<uintptr_t>(handle));
}

nk_transport web_handle(void *user_data) {
    return static_cast<nk_transport>(reinterpret_cast<uintptr_t>(user_data));
}

EM_BOOL web_open_callback(int, const EmscriptenWebSocketOpenEvent *, void *user_data) {
    nk_websocket_opened(web_handle(user_data));
    return EM_TRUE;
}

EM_BOOL web_message_callback(int, const EmscriptenWebSocketMessageEvent *event, void *user_data) {
    const auto handle = web_handle(user_data);
    if (event->isText || !nk_websocket_received(handle, event->data, event->numBytes)) {
        nk_websocket_terminal(handle, event->isText ? NK_TRANSPORT_ERROR_PROTOCOL
                                                   : NK_TRANSPORT_ERROR_RECEIVE_OVERFLOW);
        (void)emscripten_websocket_close(event->socket, 1009, "receive rejected");
    }
    return EM_TRUE;
}

EM_BOOL web_error_callback(int, const EmscriptenWebSocketErrorEvent *, void *user_data) {
    nk_websocket_terminal(web_handle(user_data), NK_TRANSPORT_ERROR_CONNECTION);
    return EM_TRUE;
}

EM_BOOL web_close_callback(int, const EmscriptenWebSocketCloseEvent *, void *user_data) {
    nk_websocket_terminal(web_handle(user_data), NK_TRANSPORT_ERROR_CLOSED);
    return EM_TRUE;
}

void web_poll_writable(void *user_data) {
    const auto handle = web_handle(user_data);
    auto transport = web_get(handle);
    if (!transport || transport->closed || !transport->open)
        return;
    size_t buffered = 0;
    if (emscripten_websocket_get_buffered_amount(transport->socket, &buffered) !=
        EMSCRIPTEN_RESULT_SUCCESS)
        return;
    if (buffered != 0) {
        emscripten_async_call(web_poll_writable, user_data, 10);
        return;
    }
    transport->writable_poll_pending = false;
    nk_websocket_writable(handle);
}

void web_connect_timeout(void *user_data) {
    const auto handle = web_handle(user_data);
    auto transport = web_get(handle);
    if (!transport || transport->closed || transport->open)
        return;
    nk_websocket_terminal(handle, NK_TRANSPORT_ERROR_TIMEOUT);
    (void)emscripten_websocket_close(transport->socket, 1000, "connect timeout");
}

int web_socket_create(uint32_t handle, const char *url, const char *protocols, uint32_t timeout,
                      double send_limit) {
    if (!emscripten_websocket_is_supported())
        return 0;
    EmscriptenWebSocketCreateAttributes attributes{};
    attributes.url = url;
    attributes.protocols = protocols;
    attributes.createOnMainThread = true;
    const auto socket = emscripten_websocket_new(&attributes);
    auto transport = web_get(handle);
    if (socket <= 0 || !transport)
        return 0;
    transport->socket = socket;
    transport->send_limit = static_cast<uint64_t>(send_limit);
    const auto user_data = web_user_data(handle);
    emscripten_websocket_set_onopen_callback(socket, user_data, web_open_callback);
    emscripten_websocket_set_onmessage_callback(socket, user_data, web_message_callback);
    emscripten_websocket_set_onerror_callback(socket, user_data, web_error_callback);
    emscripten_websocket_set_onclose_callback(socket, user_data, web_close_callback);
    emscripten_async_call(web_connect_timeout, user_data, static_cast<int>(timeout));
    return 1;
}

int web_socket_send(uint32_t handle, const void *data, uint32_t size) {
    auto transport = web_get(handle);
    if (!transport || !transport->open || transport->closed)
        return -1;
    size_t buffered = 0;
    if (emscripten_websocket_get_buffered_amount(transport->socket, &buffered) !=
            EMSCRIPTEN_RESULT_SUCCESS ||
        size > transport->send_limit - std::min<uint64_t>(transport->send_limit, buffered))
        return 0;
    if (emscripten_websocket_send_binary(transport->socket, const_cast<void *>(data), size) !=
        EMSCRIPTEN_RESULT_SUCCESS)
        return -1;
    if (!transport->writable_poll_pending) {
        transport->writable_poll_pending = true;
        emscripten_async_call(web_poll_writable, web_user_data(handle), 10);
    }
    return 1;
}

void web_socket_dispose(uint32_t handle) {
    auto transport = web_get(handle);
    if (!transport || transport->socket <= 0)
        return;
    (void)emscripten_websocket_close(transport->socket, 1000, "");
    (void)emscripten_websocket_delete(transport->socket);
    transport->socket = 0;
}

void web_finish(const std::shared_ptr<WebTransport> &transport, nk_result result) {
    if (transport->closed) return;
    transport->closed = true;
    transport->open = false;
    transport->terminal_result = result;
    if (result != NK_TRANSPORT_ERROR_CANCELED && result != NK_TRANSPORT_ERROR_CLOSED)
        (void)web_event(NK_EVENT_TRANSPORT_FAILED, transport->handle, result);
    (void)web_event(NK_EVENT_TRANSPORT_CLOSED, transport->handle, result);
}
} // namespace

namespace nk::transport {
nk_capabilities capabilities() noexcept { return NK_CAP_TRANSPORT; }
void shutdown() noexcept {
    for (const auto handle :
         nk::core::handles().handles_of_type(nk::core::ResourceType::transport)) {
        if (auto transport = web_get(handle))
            transport->closed = true;
        web_socket_dispose(handle);
        nk::core::handles().erase(handle, nk::core::ResourceType::transport);
    }
}
} // namespace nk::transport

extern "C" {
EMSCRIPTEN_KEEPALIVE void nk_websocket_opened(nk_transport handle) {
    auto transport = web_get(handle);
    if (!transport || transport->closed ||
        !nk::core::is_runtime_generation(transport->generation))
        return;
    transport->open = true;
    (void)web_event(NK_EVENT_TRANSPORT_CONNECTED, handle);
    (void)web_event(NK_EVENT_TRANSPORT_WRITABLE, handle);
}

EMSCRIPTEN_KEEPALIVE int nk_websocket_received(nk_transport handle, const void *data,
                                               uint32_t size) {
    auto transport = web_get(handle);
    if (!transport || !transport->open || transport->closed ||
        !nk::core::is_runtime_generation(transport->generation) ||
        size > transport->receive_limit -
                   std::min(transport->receive_limit, transport->incoming_bytes))
        return 0;
    std::vector<uint8_t> bytes(size);
    if (size) std::memcpy(bytes.data(), data, size);
    transport->incoming_bytes += size;
    transport->incoming.push_back(std::move(bytes));
    return web_data_event(transport) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE void nk_websocket_writable(nk_transport handle) {
    auto transport = web_get(handle);
    if (transport && transport->open && !transport->closed)
        (void)web_event(NK_EVENT_TRANSPORT_WRITABLE, handle);
}

EMSCRIPTEN_KEEPALIVE void nk_websocket_terminal(nk_transport handle, nk_result result) {
    auto transport = web_get(handle);
    if (transport && nk::core::is_runtime_generation(transport->generation))
        web_finish(transport, result);
}

nk_result NK_CALL nk_transport_query_capabilities(nk_transport_kind kind,
                                                  nk_transport_capabilities *out) {
    if (!out) return web_fail(NK_ERROR_INVALID_ARGUMENT, "transport capability output is null");
    *out = 0;
    if (kind < NK_TRANSPORT_TCP || kind > NK_TRANSPORT_LOCAL)
        return web_fail(NK_ERROR_INVALID_ARGUMENT, "transport kind is invalid");
    if (kind == NK_TRANSPORT_WEBSOCKET)
        *out = NK_TRANSPORT_CAP_CLIENT | NK_TRANSPORT_CAP_SECURE_CLIENT |
               NK_TRANSPORT_CAP_SUBPROTOCOL;
    return NK_OK;
}

nk_result NK_CALL nk_transport_connect(const nk_transport_options *options,
                                       nk_transport *out_transport) {
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK) return thread;
    if (!out_transport) return web_fail(NK_ERROR_INVALID_ARGUMENT, "transport output is null");
    *out_transport = NK_INVALID_HANDLE;
    if (!options || options->struct_size < NK_TRANSPORT_OPTIONS_V1_SIZE ||
        (options->struct_size != NK_TRANSPORT_OPTIONS_V1_SIZE &&
         options->struct_size < sizeof(*options)) ||
        options->kind != NK_TRANSPORT_WEBSOCKET || !options->host || !*options->host ||
        options->reserved0 || options->reserved[0] || options->reserved[1] ||
        (options->flags & ~(NK_TRANSPORT_NO_DELAY | NK_TRANSPORT_SECURE)))
        return web_fail(NK_ERROR_INVALID_ARGUMENT, "browser transport options are invalid");
    const char *protocols = nullptr;
    if (options->struct_size >= sizeof(*options)) {
        if (options->reserved2[0] || options->reserved2[1])
            return web_fail(NK_ERROR_INVALID_ARGUMENT, "browser transport options are invalid");
        protocols = options->subprotocols;
    }
    const bool secure = (options->flags & NK_TRANSPORT_SECURE) != 0;
    const auto port = options->port ? options->port : static_cast<uint16_t>(secure ? 443 : 80);
    std::string url = secure ? "wss://" : "ws://";
    const std::string_view host(options->host);
    if (host.find(':') != std::string_view::npos && host.front() != '[')
        url += '[' + std::string(host) + ']';
    else
        url += host;
    url += ":" + std::to_string(port);
    const char *path = options->path && *options->path ? options->path : "/";
    if (*path != '/') url += '/';
    url += path;
    auto transport = std::make_shared<WebTransport>();
    transport->generation = nk::core::runtime_generation();
    transport->receive_limit = options->receive_buffer_size ? options->receive_buffer_size
                                                             : web_default_buffer;
    const auto send_limit =
        options->send_buffer_size ? options->send_buffer_size : web_default_buffer;
    const auto handle = nk::core::handles().insert(nk::core::ResourceType::transport, transport);
    if (handle == NK_INVALID_HANDLE)
        return web_fail(NK_ERROR_OUT_OF_MEMORY, "transport handle allocation failed");
    transport->handle = static_cast<nk_transport>(handle);
    if (!web_socket_create(handle, url.c_str(), protocols,
                           options->timeout_ms ? options->timeout_ms : 5000,
                           static_cast<double>(send_limit))) {
        nk::core::handles().erase(handle, nk::core::ResourceType::transport);
        return web_fail(NK_TRANSPORT_ERROR_CONNECTION, "browser WebSocket creation failed");
    }
    *out_transport = transport->handle;
    return NK_OK;
}

nk_result NK_CALL nk_transport_listen(const nk_transport_options *, nk_listener *out_listener) {
    if (out_listener) *out_listener = NK_INVALID_HANDLE;
    return NK_ERROR_UNSUPPORTED;
}

nk_result NK_CALL nk_transport_send(nk_transport handle, const void *data, uint64_t size) {
    if ((size && !data) || size > UINT32_MAX)
        return web_fail(NK_ERROR_INVALID_ARGUMENT, "transport data is invalid");
    auto transport = web_get(handle);
    if (!transport) return web_fail(NK_ERROR_INVALID_HANDLE, "invalid transport handle");
    if (!transport->open || transport->closed)
        return web_fail(NK_ERROR_INVALID_REQUEST, "transport is not open");
    const int result = web_socket_send(handle, data, static_cast<uint32_t>(size));
    return result > 0 ? NK_OK : result == 0 ? NK_ERROR_QUEUE_FULL : NK_ERROR_INVALID_REQUEST;
}

nk_result NK_CALL nk_transport_receive(nk_transport handle, void *data, uint64_t size,
                                       uint64_t *out_received) {
    if (!out_received || (size && !data))
        return web_fail(NK_ERROR_INVALID_ARGUMENT, "transport receive arguments are invalid");
    *out_received = 0;
    auto transport = web_get(handle);
    if (!transport) return web_fail(NK_ERROR_INVALID_HANDLE, "invalid transport handle");
    if (transport->incoming.empty())
        return transport->closed ? transport->terminal_result : NK_TRANSPORT_ERROR_WOULD_BLOCK;
    auto &chunk = transport->incoming.front();
    const auto amount = std::min<uint64_t>(size, chunk.size());
    if (amount) std::memcpy(data, chunk.data(), static_cast<std::size_t>(amount));
    *out_received = amount;
    transport->incoming_bytes -= amount;
    if (amount == chunk.size()) transport->incoming.pop_front();
    else chunk.erase(chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(amount));
    if (transport->incoming.empty()) transport->data_pending = false;
    return NK_OK;
}

nk_result NK_CALL nk_transport_cancel(nk_transport handle) {
    auto transport = web_get(handle);
    if (!transport) return web_fail(NK_ERROR_INVALID_HANDLE, "invalid transport handle");
    if (!transport->closed) {
        web_finish(transport, NK_TRANSPORT_ERROR_CANCELED);
        web_socket_dispose(handle);
    }
    return NK_OK;
}

nk_result NK_CALL nk_transport_close(nk_transport handle) {
    auto transport = web_get(handle);
    if (!transport) return web_fail(NK_ERROR_INVALID_HANDLE, "invalid transport handle");
    if (!transport->closed) web_finish(transport, NK_TRANSPORT_ERROR_CANCELED);
    web_socket_dispose(handle);
    nk::core::handles().erase(handle, nk::core::ResourceType::transport);
    return NK_OK;
}

nk_result NK_CALL nk_listener_close(nk_listener) { return NK_ERROR_UNSUPPORTED; }

nk_result NK_CALL nk_transport_event_data(const nk_event *event, nk_transport_data_event *out) {
    if (!event || !out || out->struct_size < sizeof(*out) ||
        event->kind != NK_EVENT_TRANSPORT_DATA ||
        !event->data || event->data_size != sizeof(*out))
        return web_fail(NK_ERROR_INVALID_ARGUMENT, "transport data event is invalid");
    std::memcpy(out, event->data, sizeof(*out));
    return NK_OK;
}

nk_result NK_CALL nk_transport_event_accepted(const nk_event *, nk_transport_accepted_event *) {
    return NK_ERROR_UNSUPPORTED;
}
} // extern "C"
