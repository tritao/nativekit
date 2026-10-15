#include "nativekit.h"
#include "nativekit_transport.h"
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <string>
#include <thread>

// Invoked by tools/test-secure-transport.py with a loopback TLS fixture and a private CA bundle.
int main(int argc, char **argv) {
    assert(argc == 2);
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    nk_transport_options options{};
    options.struct_size = sizeof(options);
    options.kind = NK_TRANSPORT_WEBSOCKET;
    options.flags = NK_TRANSPORT_SECURE | NK_TRANSPORT_MESSAGES | NK_TRANSPORT_TEXT;
    options.host = "localhost";
    options.port = static_cast<uint16_t>(std::atoi(argv[1]));
    options.path = "/messages";
    nk_transport transport;
    assert(nk_transport_connect(&options, &transport) == NK_OK);
    const std::string large(20000, 'x');
    const std::string expected[] = {large, "", "world"};
    bool connected = false;
    int count = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (count < 3 && std::chrono::steady_clock::now() < deadline) {
        nk_event event{};
        event.struct_size = sizeof(event);
        while (nk_poll_event(&event) == NK_OK && event.kind != NK_EVENT_NONE) {
            assert(event.kind != NK_EVENT_TRANSPORT_FAILED);
            if (event.kind == NK_EVENT_TRANSPORT_CONNECTED) {
                connected = true;
                for (const auto &message : expected)
                    assert(nk_transport_send(transport, message.data(), message.size()) == NK_OK);
            }
            nk_event_release(&event);
            event.struct_size = sizeof(event);
        }
        nk_event_release(&event);
        if (!connected) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        char buffer[25000];
        uint64_t size;
        const auto code = nk_transport_receive(transport, buffer, sizeof(buffer), &size);
        if (code == NK_TRANSPORT_ERROR_WOULD_BLOCK) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        assert(code == NK_OK);
        assert(std::string(buffer, size) == expected[count++]);
    }
    assert(count == 3);
    assert(nk_transport_close(transport) == NK_OK);
    nk_shutdown();
}
