#include "nativekit.h"
#include "nativekit_transport.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <emscripten/emscripten.h>

static nk_transport transport;
static int received;
static double deadline;

static void tick(void) {
    assert(emscripten_get_now() < deadline);
    nk_event event = {0};
    event.struct_size = sizeof(event);
    while (nk_poll_event(&event) == NK_OK && event.kind != NK_EVENT_NONE) {
        assert(event.kind != NK_EVENT_TRANSPORT_FAILED);
        if (event.kind == NK_EVENT_TRANSPORT_CONNECTED) {
            assert(nk_transport_send(transport, "hello", 5) == NK_OK);
            assert(nk_transport_send(transport, NULL, 0) == NK_OK);
            assert(nk_transport_send(transport, "world", 5) == NK_OK);
        }
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
    nk_event_release(&event);
    for (;;) {
        char buffer[16];
        uint64_t size = 0;
        nk_result code = nk_transport_receive(transport, buffer, 2, &size);
        if (code == NK_TRANSPORT_ERROR_WOULD_BLOCK)
            break;
        if (received != 1) {
            assert(code == NK_ERROR_PAYLOAD_TOO_LARGE && size == 5);
            assert(nk_transport_receive(transport, buffer, sizeof(buffer), &size) == NK_OK);
            assert(size == 5 && memcmp(buffer, received == 0 ? "hello" : "world", 5) == 0);
        } else {
            assert(code == NK_OK && size == 0);
        }
        if (++received == 3) {
            assert(nk_transport_close(transport) == NK_OK);
            nk_shutdown();
            emscripten_cancel_main_loop();
            EM_ASM({ document.documentElement.dataset.nativekitMessages = "passed"; });
            return;
        }
    }
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    nk_transport_options options = {0};
    options.struct_size = sizeof(options);
    options.kind = NK_TRANSPORT_WEBSOCKET;
    options.flags = NK_TRANSPORT_MESSAGES | NK_TRANSPORT_TEXT;
    options.host = "127.0.0.1";
    options.port =
        (uint16_t)EM_ASM_INT({ return Number(new URLSearchParams(location.search).get('port')); });
    options.path = "/messages";
    assert(nk_transport_connect(&options, &transport) == NK_OK);
    deadline = emscripten_get_now() + 10000;
    emscripten_set_main_loop(tick, 60, 0);
    return 0;
}
