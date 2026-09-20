#include "nativekit_transport.h"
#include "nativekit_time.h"
#include "nativekit_window.h"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>

namespace {

bool poll_transport_events(nk_transport client, nk_transport *accepted, bool *client_connected,
                           bool *server_connected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!*client_connected || !*server_connected || *accepted == NK_INVALID_HANDLE) &&
           std::chrono::steady_clock::now() < deadline) {
        assert(nk_wait_events_timeout(0.05) == NK_OK);
        for (;;) {
            nk_event event{};
            event.struct_size = sizeof(event);
            assert(nk_poll_event(&event) == NK_OK);
            if (event.kind == NK_EVENT_NONE) {
                nk_event_release(&event);
                break;
            }
            if (event.kind == NK_EVENT_TRANSPORT_ACCEPTED) {
                nk_transport_accepted_event accepted_event{};
                accepted_event.struct_size = sizeof(accepted_event);
                assert(nk_transport_event_accepted(&event, &accepted_event) == NK_OK);
                *accepted = accepted_event.transport;
            } else if (event.kind == NK_EVENT_TRANSPORT_CONNECTED) {
                if (event.source == client)
                    *client_connected = true;
                else if (*accepted != NK_INVALID_HANDLE && event.source == *accepted)
                    *server_connected = true;
            }
            nk_event_release(&event);
        }
    }
    return *client_connected && *server_connected && *accepted != NK_INVALID_HANDLE;
}

bool receive_message(nk_transport transport, const char *expected) {
    const auto expected_size = std::strlen(expected);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    auto try_receive = [&] {
        char buffer[64] = {};
        std::uint64_t received = 0;
        const auto result = nk_transport_receive(transport, buffer, sizeof(buffer), &received);
        return result == NK_OK && received == expected_size &&
               std::memcmp(buffer, expected, expected_size) == 0;
    };
    while (std::chrono::steady_clock::now() < deadline) {
        if (try_receive())
            return true;
        assert(nk_wait_events_timeout(0.05) == NK_OK);
        for (;;) {
            nk_event event{};
            event.struct_size = sizeof(event);
            assert(nk_poll_event(&event) == NK_OK);
            if (event.kind == NK_EVENT_NONE) {
                nk_event_release(&event);
                break;
            }
            if (event.kind == NK_EVENT_TRANSPORT_DATA && event.source == transport) {
                nk_transport_data_event data_event{};
                data_event.struct_size = sizeof(data_event);
                assert(nk_transport_event_data(&event, &data_event) == NK_OK);
                assert(data_event.available >= expected_size);
                if (try_receive()) {
                    nk_event_release(&event);
                    return true;
                }
            }
            nk_event_release(&event);
        }
    }
    return false;
}

std::uint16_t listen_port(nk_listener *out_listener, nk_transport_kind kind,
                          const char *path = nullptr) {
    for (std::uint16_t port = 39000; port != 39300; ++port) {
        nk_transport_options options{};
        options.struct_size = sizeof(options);
        options.kind = kind;
        options.flags = NK_TRANSPORT_REUSE_ADDRESS;
        options.host = "127.0.0.1";
        options.port = port;
        options.path = path;
        if (nk_transport_listen(&options, out_listener) == NK_OK)
            return port;
    }
    return 0;
}

void roundtrip(nk_transport_kind kind, const char *path = nullptr) {
    nk_listener listener = NK_INVALID_HANDLE;
    const auto port = listen_port(&listener, kind, path);
    assert(port != 0);

    nk_transport_options connect_options{};
    connect_options.struct_size = sizeof(connect_options);
    connect_options.kind = kind;
    connect_options.host = "127.0.0.1";
    connect_options.port = port;
    connect_options.path = path;
    connect_options.timeout_ms = 2000;
    nk_transport client = NK_INVALID_HANDLE;
    assert(nk_transport_connect(&connect_options, &client) == NK_OK);

    nk_transport server = NK_INVALID_HANDLE;
    bool client_connected = false;
    bool server_connected = false;
    const char request[] = "nativekit-transport";
    if (kind == NK_TRANSPORT_UDP)
        assert(nk_transport_send(client, request, sizeof(request) - 1) == NK_OK);
    assert(poll_transport_events(client, &server, &client_connected, &server_connected));

    if (kind != NK_TRANSPORT_UDP)
        assert(nk_transport_send(client, request, sizeof(request) - 1) == NK_OK);
    assert(receive_message(server, request));

    const char response[] = "transport-nativekit";
    assert(nk_transport_send(server, response, sizeof(response) - 1) == NK_OK);
    assert(receive_message(client, response));

    assert(nk_transport_close(client) == NK_OK);
    assert(nk_transport_close(server) == NK_OK);
    assert(nk_listener_close(listener) == NK_OK);
}

} // namespace

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    init.event_queue_capacity = 128;
    assert(nk_init(&init) == NK_OK);
    assert((nk_get_capabilities() & NK_CAP_TRANSPORT) != 0);

    roundtrip(NK_TRANSPORT_TCP);
    roundtrip(NK_TRANSPORT_UDP);
    roundtrip(NK_TRANSPORT_WEBSOCKET, "/nativekit");
    roundtrip(NK_TRANSPORT_LOCAL, "/tmp/nativekit-transport-contract.sock");
    nk_shutdown();
    return 0;
}
