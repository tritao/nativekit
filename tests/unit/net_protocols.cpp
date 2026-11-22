#include "nativekit_net.h"
#include "nativekit_time.h"
#include "nativekit_window.h"

#include "net/curl_runtime.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define NK_CHECK(expression)                                                                       \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #expression);  \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

/*
 * Invoked by tools/test-curl-protocols.py with a loopback HTTPS server whose /redirect answers
 * with a redirect to the plain HTTP server's /target. Checks that libcurl's protocol
 * restrictions follow the client flags, through the protocol-string options or (with
 * "bitmask") the bitmask options older libcurl versions need.
 */

namespace {

struct Outcome {
    nk_result result = NK_ERROR_UNKNOWN;
    uint32_t status = 0;
    std::string body;
};

Outcome fetch(uint32_t flags, const std::string &url, const char *ca_file) {
    nk_http_tls_options tls{};
    tls.struct_size = sizeof(tls);
    tls.ca_bundle_path = ca_file;
    nk_http_client_options client_options{};
    client_options.struct_size = sizeof(client_options);
    client_options.flags = flags;
    client_options.tls = &tls;
    nk_http_client client = NK_INVALID_HANDLE;
    NK_CHECK(nk_http_client_create(&client_options, &client) == NK_OK);
    nk_http_request_options options{};
    options.struct_size = sizeof(options);
    options.method = NK_HTTP_METHOD_GET;
    options.url = url.c_str();
    nk_request_id request = NK_INVALID_REQUEST_ID;
    nk_http_stream stream = NK_INVALID_HANDLE;
    NK_CHECK(nk_http_request(client, &options, &request, &stream) == NK_OK);
    Outcome outcome;
    bool complete = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!complete && std::chrono::steady_clock::now() < deadline) {
        nk_wait_events_timeout(0.05);
        nk_event event{};
        event.struct_size = sizeof(event);
        while (nk_poll_event(&event) == NK_OK && event.kind != NK_EVENT_NONE) {
            if (event.request_id == request && event.kind == NK_EVENT_HTTP_COMPLETE) {
                outcome.result = event.result;
                nk_http_response response{};
                response.struct_size = sizeof(response);
                if (nk_http_event_response(&event, &response) == NK_OK) {
                    outcome.status = response.status_code;
                    if (response.body_size)
                        outcome.body.assign(static_cast<const char *>(response.body),
                                            response.body_size);
                }
                complete = true;
            }
            nk_event_release(&event);
            event.struct_size = sizeof(event);
        }
        nk_event_release(&event);
    }
    NK_CHECK(complete);
    NK_CHECK(nk_http_client_destroy(client) == NK_OK);
    return outcome;
}

bool reached_target(const Outcome &outcome) {
    return outcome.result == NK_OK && outcome.status == 200 && outcome.body == "target";
}

} // namespace

int main(int argc, char **argv) {
    NK_CHECK(argc == 4 || argc == 5);
    const std::string https_base = std::string("https://localhost:") + argv[1];
    const char *ca_file = argv[3];
    const bool bitmask = argc == 5 && std::strcmp(argv[4], "bitmask") == 0;
    nk::core::use_curl_protocol_bitmask_for_testing(bitmask);

    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    NK_CHECK(nk_init(&init) == NK_OK);
    NK_CHECK((nk_get_capabilities() & NK_CAP_HTTP_CLIENT) != 0);

    /* HTTPS itself is always allowed. */
    NK_CHECK(reached_target(fetch(0, https_base + "/target", ca_file)));
    const std::string redirect = https_base + "/redirect";
    /* HTTP refused when only HTTPS is allowed, even where redirects to HTTP are. */
    NK_CHECK(!reached_target(fetch(NK_HTTP_CLIENT_ALLOW_HTTPS_TO_HTTP, redirect, ca_file)));
    /* An HTTPS-to-HTTP redirect refused unless allowed... */
    NK_CHECK(!reached_target(fetch(NK_HTTP_CLIENT_ALLOW_HTTP, redirect, ca_file)));
    /* ...and followed once both are allowed. */
    NK_CHECK(reached_target(
        fetch(NK_HTTP_CLIENT_ALLOW_HTTP | NK_HTTP_CLIENT_ALLOW_HTTPS_TO_HTTP, redirect, ca_file)));
    /* Plain HTTP itself once allowed. */
    const std::string http_target = std::string("http://127.0.0.1:") + argv[2] + "/target";
    NK_CHECK(reached_target(fetch(NK_HTTP_CLIENT_ALLOW_HTTP, http_target, ca_file)));

    nk_shutdown();
    std::printf("protocol restrictions hold (%s options)\n", bitmask ? "bitmask" : "string");
    return 0;
}
