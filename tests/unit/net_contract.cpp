#include "nativekit_net.h"

#include "nativekit_time.h"
#include "nativekit_window.h"

#include <arpa/inet.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

struct LocalServer {
    int socket = -1;
    uint16_t port = 0;
    std::atomic<bool> cancel_ready{false};
    std::thread worker;

    LocalServer() {
        socket = ::socket(AF_INET, SOCK_STREAM, 0);
        assert(socket >= 0);
        int reuse = 1;
        assert(::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        assert(::bind(socket, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0);
        assert(::listen(socket, 8) == 0);
        socklen_t address_size = sizeof(address);
        assert(::getsockname(socket, reinterpret_cast<sockaddr *>(&address), &address_size) == 0);
        port = ntohs(address.sin_port);
        worker = std::thread([this] { serve(); });
    }

    ~LocalServer() {
        if (socket >= 0)
            ::close(socket);
        if (worker.joinable())
            worker.join();
    }

    void serve() {
        for (int connection_index = 0; connection_index != 4; ++connection_index) {
            const int connection = ::accept(socket, nullptr, nullptr);
            if (connection < 0)
                return;
            std::string request;
            char buffer[1024];
            while (request.find("\r\n\r\n") == std::string::npos) {
                const auto count = ::recv(connection, buffer, sizeof(buffer), 0);
                if (count <= 0)
                    break;
                request.append(buffer, static_cast<std::size_t>(count));
            }
            const auto first_line_end = request.find("\r\n");
            const auto first_line = request.substr(0, first_line_end);
            const bool streaming = first_line.find("/stream") != std::string::npos;
            const bool limited = first_line.find("/limit") != std::string::npos;
            const bool canceled = first_line.find("/cancel") != std::string::npos;
            if (canceled) {
                cancel_ready.store(true, std::memory_order_release);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            const std::string body = streaming  ? "stream-body-0123456789"
                                     : limited  ? "0123456789"
                                     : canceled ? "cancel-body"
                                                : "buffered-body";
            const std::string response =
                streaming || limited || canceled
                    ? "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) +
                          "\r\nX-Mode: " +
                          (streaming ? "stream"
                           : limited ? "limit"
                                     : "cancel") +
                          "\r\n\r\n" + body
                    : "HTTP/1.1 404 Not Found\r\nContent-Length: " + std::to_string(body.size()) +
                          "\r\nX-Mode: buffered\r\n\r\n" + body;
            const auto *data = response.data();
            auto remaining = response.size();
            while (remaining != 0) {
                const auto count = ::send(connection, data, remaining, MSG_NOSIGNAL);
                if (count <= 0)
                    break;
                data += count;
                remaining -= static_cast<std::size_t>(count);
            }
            ::shutdown(connection, SHUT_RDWR);
            ::close(connection);
        }
    }
};

std::string url(uint16_t port, const char *path) {
    return "http://127.0.0.1:" + std::to_string(port) + path;
}

bool poll_until(nk_request_id request, nk_http_stream stream, std::string *complete_body,
                uint32_t *complete_status, uint32_t *complete_header_count, bool *saw_headers,
                nk_result expected_result, uint32_t expected_status) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool complete = false;
    while (!complete && std::chrono::steady_clock::now() < deadline) {
        nk_wait_events_timeout(0.05);
        for (;;) {
            nk_event event{};
            event.struct_size = sizeof(event);
            assert(nk_poll_event(&event) == NK_OK);
            if (event.kind == NK_EVENT_NONE) {
                nk_event_release(&event);
                break;
            }
            if (event.request_id != request) {
                nk_event_release(&event);
                continue;
            }
            if (event.kind == NK_EVENT_HTTP_HEADERS) {
                nk_http_response response{};
                response.struct_size = sizeof(response);
                assert(nk_http_event_response(&event, &response) == NK_OK);
                assert(response.status_code == expected_status);
                assert(response.stream == stream);
                *saw_headers = true;
            } else if (event.kind == NK_EVENT_HTTP_DATA_AVAILABLE) {
                assert(stream != NK_INVALID_HANDLE);
                nk_http_stream_info info{};
                info.struct_size = sizeof(info);
                assert(nk_http_stream_info_get(stream, &info) == NK_OK);
                char body[64];
                uint64_t read = 0;
                const auto read_result = nk_http_stream_read(stream, body, sizeof(body), &read);
                assert(read_result == NK_OK);
                assert(read != 0);
            } else if (event.kind == NK_EVENT_HTTP_COMPLETE) {
                assert(event.result == expected_result);
                nk_http_response response{};
                response.struct_size = sizeof(response);
                assert(nk_http_event_response(&event, &response) == NK_OK);
                if (complete_body && response.body_size != 0)
                    complete_body->assign(static_cast<const char *>(response.body),
                                          response.body_size);
                else if (complete_body)
                    complete_body->clear();
                if (response.header_count != 0) {
                    nk_http_header header{};
                    assert(nk_http_response_header(&response, 0, &header) == NK_OK);
                    assert(header.name_size != 0);
                }
                if (complete_status)
                    *complete_status = response.status_code;
                if (complete_header_count)
                    *complete_header_count = response.header_count;
                complete = true;
            }
            nk_event_release(&event);
        }
    }
    return complete;
}

} // namespace

int main() {
    static_assert(sizeof(nk_request_id) == 8);
    static_assert(NK_CAP_HTTP_STREAMING == (UINT64_C(1) << 35));

    LocalServer server;
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    assert((nk_get_capabilities() & (NK_CAP_HTTP_CLIENT | NK_CAP_HTTP_STREAMING)) ==
           (NK_CAP_HTTP_CLIENT | NK_CAP_HTTP_STREAMING));

    nk_http_client_options client_options{};
    client_options.struct_size = sizeof(client_options);
    client_options.flags = NK_HTTP_CLIENT_ALLOW_HTTP;
    client_options.stream_buffer_size = 4;
    nk_http_client client = NK_INVALID_HANDLE;
    assert(nk_http_client_create(&client_options, &client) == NK_OK);

    nk_http_request_options buffered_options{};
    buffered_options.struct_size = sizeof(buffered_options);
    buffered_options.method = NK_HTTP_METHOD_GET;
    const auto buffered_url = url(server.port, "/buffered");
    buffered_options.url = buffered_url.c_str();
    nk_request_id buffered_request = NK_INVALID_REQUEST_ID;
    nk_http_stream no_stream = NK_INVALID_HANDLE;
    assert(nk_http_request(client, &buffered_options, &buffered_request, &no_stream) == NK_OK);
    assert(no_stream == NK_INVALID_HANDLE);
    std::string buffered_body;
    uint32_t buffered_status = 0;
    uint32_t buffered_header_count = 0;
    bool saw_buffered_headers = false;
    assert(poll_until(buffered_request, NK_INVALID_HANDLE, &buffered_body, &buffered_status,
                      &buffered_header_count, &saw_buffered_headers, NK_OK, 404));
    assert(saw_buffered_headers);
    assert(buffered_status == 404);
    assert(buffered_body == "buffered-body");
    assert(buffered_header_count >= 2);

    nk_http_request_options stream_options{};
    stream_options.struct_size = sizeof(stream_options);
    stream_options.method = NK_HTTP_METHOD_GET;
    stream_options.mode = NK_HTTP_REQUEST_STREAMING;
    const auto stream_url = url(server.port, "/stream");
    stream_options.url = stream_url.c_str();
    nk_request_id stream_request = NK_INVALID_REQUEST_ID;
    nk_http_stream stream = NK_INVALID_HANDLE;
    assert(nk_http_request(client, &stream_options, &stream_request, &stream) == NK_OK);
    assert(stream != NK_INVALID_HANDLE);
    bool saw_stream_headers = false;
    assert(poll_until(stream_request, stream, nullptr, nullptr, nullptr, &saw_stream_headers, NK_OK,
                      200));
    assert(saw_stream_headers);
    nk_http_stream_info stream_info{};
    stream_info.struct_size = sizeof(stream_info);
    assert(nk_http_stream_info_get(stream, &stream_info) == NK_OK);
    assert(stream_info.flags & NK_HTTP_STREAM_COMPLETE);
    char remaining[64];
    uint64_t remaining_size = 0;
    assert(nk_http_stream_read(stream, remaining, sizeof(remaining), &remaining_size) == NK_OK);
    assert(nk_http_stream_close(stream) == NK_OK);

    nk_http_request_options limited_options{};
    limited_options.struct_size = sizeof(limited_options);
    limited_options.method = NK_HTTP_METHOD_GET;
    limited_options.max_response_size = 4;
    const auto limited_url = url(server.port, "/limit");
    limited_options.url = limited_url.c_str();
    nk_request_id limited_request = NK_INVALID_REQUEST_ID;
    assert(nk_http_request(client, &limited_options, &limited_request, nullptr) == NK_OK);
    std::string limited_body;
    bool saw_limited_headers = false;
    assert(poll_until(limited_request, NK_INVALID_HANDLE, &limited_body, nullptr, nullptr,
                      &saw_limited_headers, NK_HTTP_ERROR_RESPONSE_LIMIT, 200));
    assert(saw_limited_headers);
    assert(limited_body.empty());

    nk_http_request_options cancel_options{};
    cancel_options.struct_size = sizeof(cancel_options);
    cancel_options.method = NK_HTTP_METHOD_GET;
    const auto cancel_url = url(server.port, "/cancel");
    cancel_options.url = cancel_url.c_str();
    nk_request_id cancel_request = NK_INVALID_REQUEST_ID;
    assert(nk_http_request(client, &cancel_options, &cancel_request, nullptr) == NK_OK);
    const auto cancel_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!server.cancel_ready.load(std::memory_order_acquire) &&
           std::chrono::steady_clock::now() < cancel_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(server.cancel_ready.load(std::memory_order_acquire));
    assert(nk_http_cancel(cancel_request) == NK_OK);
    bool saw_canceled_headers = false;
    assert(poll_until(cancel_request, NK_INVALID_HANDLE, nullptr, nullptr, nullptr,
                      &saw_canceled_headers, NK_HTTP_ERROR_CANCELED, 200));

    assert(nk_http_client_destroy(client) == NK_OK);
    nk_shutdown();
    return 0;
}
