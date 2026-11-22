#define UNICODE
#define _UNICODE
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winhttp.h>

#include "transport/secure_websocket.hpp"

#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <vector>

/*
 * WinHTTP's WebSocket client (Windows 8 and later): WinHTTP performs the TLS
 * handshake against the system trust store, the upgrade and the framing, and
 * this backend exchanges complete messages. All calls are synchronous; closing
 * a handle from another thread cancels the calls waiting on it.
 */

namespace nk::transport {
namespace {

std::wstring wide(std::string_view value, bool &valid) {
    valid = false;
    if (value.size() > static_cast<std::size_t>(INT_MAX))
        return {};
    if (value.empty()) {
        valid = true;
        return {};
    }
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                            static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), length) != length)
        return {};
    valid = true;
    return result;
}

std::string narrow(const wchar_t *value, std::size_t length) {
    if (length == 0 || length > static_cast<std::size_t>(INT_MAX))
        return {};
    const auto size = WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(length), nullptr, 0,
                                          nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, static_cast<int>(length), result.data(), size, nullptr,
                        nullptr);
    return result;
}

nk_result map_error(DWORD error) noexcept {
    switch (error) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return NK_TRANSPORT_ERROR_DNS;
    case ERROR_WINHTTP_TIMEOUT:
        return NK_TRANSPORT_ERROR_TIMEOUT;
    case ERROR_WINHTTP_OPERATION_CANCELLED:
        return NK_TRANSPORT_ERROR_CANCELED;
    default:
        /* Refused connections and failed certificate checks alike. */
        return NK_TRANSPORT_ERROR_CONNECTION;
    }
}

class WinHttpWebSocket final : public SecureWebSocket {
  public:
    ~WinHttpWebSocket() override { cancel(); }

    nk_result connect(const SecureEndpoint &endpoint) noexcept override {
#if NK_ENABLE_NO_EXCEPTIONS
        return connect_impl(endpoint);
#else
        try {
            return connect_impl(endpoint);
        } catch (const std::bad_alloc &) {
            return NK_ERROR_OUT_OF_MEMORY;
        } catch (...) {
            return NK_TRANSPORT_ERROR_CONNECTION;
        }
#endif
    }

    nk_result receive(std::vector<uint8_t> &message, std::size_t max_size) noexcept override {
        const HINTERNET socket = websocket();
        if (!socket)
            return NK_TRANSPORT_ERROR_CLOSED;
        message.clear();
        uint8_t buffer[16 * 1024];
        for (;;) {
            DWORD received = 0;
            WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
            const DWORD error =
                WinHttpWebSocketReceive(socket, buffer, sizeof(buffer), &received, &type);
            if (error != NO_ERROR)
                return cancelled() ? NK_TRANSPORT_ERROR_CANCELED : NK_TRANSPORT_ERROR_CLOSED;
            if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
                return NK_TRANSPORT_ERROR_CLOSED;
            if (received > max_size - message.size())
                return NK_TRANSPORT_ERROR_RECEIVE_OVERFLOW;
#if NK_ENABLE_NO_EXCEPTIONS
            message.insert(message.end(), buffer, buffer + received);
#else
            try {
                message.insert(message.end(), buffer, buffer + received);
            } catch (...) {
                return NK_TRANSPORT_ERROR_RECEIVE_OVERFLOW;
            }
#endif
            if (type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE ||
                type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
                return NK_OK;
        }
    }

    nk_result send(const uint8_t *data, std::size_t size, bool text) noexcept override {
        const HINTERNET socket = websocket();
        if (!socket)
            return NK_TRANSPORT_ERROR_CLOSED;
        if (size > MAXDWORD)
            return NK_ERROR_PAYLOAD_TOO_LARGE;
        const auto type = text ? WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE
                               : WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE;
        const DWORD error = WinHttpWebSocketSend(socket, type, const_cast<uint8_t *>(data),
                                                 static_cast<DWORD>(size));
        if (error == NO_ERROR)
            return NK_OK;
        return cancelled() ? NK_TRANSPORT_ERROR_CANCELED : NK_TRANSPORT_ERROR_CONNECTION;
    }

    void cancel() noexcept override {
        std::lock_guard lock(mutex_);
        cancelled_ = true;
        for (HINTERNET *handle : {&websocket_, &request_, &connection_, &session_}) {
            if (*handle) {
                WinHttpCloseHandle(*handle);
                *handle = nullptr;
            }
        }
    }

  private:
    bool cancelled() noexcept {
        std::lock_guard lock(mutex_);
        return cancelled_;
    }

    HINTERNET websocket() noexcept {
        std::lock_guard lock(mutex_);
        return websocket_;
    }

    /** Publishes a handle so cancel() can close it; false once cancelled. */
    bool keep(HINTERNET &slot, HINTERNET handle) noexcept {
        std::lock_guard lock(mutex_);
        if (cancelled_) {
            if (handle)
                WinHttpCloseHandle(handle);
            return false;
        }
        slot = handle;
        return handle != nullptr;
    }

    nk_result failure() noexcept {
        const DWORD error = GetLastError();
        return cancelled() ? NK_TRANSPORT_ERROR_CANCELED : map_error(error);
    }

    nk_result connect_impl(const SecureEndpoint &endpoint) {
        bool host_valid = false;
        bool path_valid = false;
        bool protocols_valid = false;
        const auto host = wide(endpoint.host, host_valid);
        const auto path = wide(endpoint.path, path_valid);
        const auto protocols = wide(endpoint.subprotocols, protocols_valid);
        if (!host_valid || !path_valid || !protocols_valid)
            return NK_TRANSPORT_ERROR_PROTOCOL;

        /* Calls use local copies: cancel() may close and clear the members at any time, and a
           closed handle makes the call fail instead of blocking. */
        HINTERNET session = WinHttpOpen(L"NativeKit/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!keep(session_, session))
            return failure();
        const int timeout = endpoint.timeout_ms > static_cast<uint32_t>(INT_MAX)
                                ? INT_MAX
                                : static_cast<int>(endpoint.timeout_ms);
        if (!WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout))
            return failure();
        HINTERNET connection = WinHttpConnect(session, host.c_str(), endpoint.port, 0);
        if (!keep(connection_, connection))
            return failure();
        HINTERNET request =
            WinHttpOpenRequest(connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!keep(request_, request))
            return failure();
        if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0))
            return failure();
        if (!protocols.empty()) {
            const std::wstring header = L"Sec-WebSocket-Protocol: " + protocols;
            if (!WinHttpAddRequestHeaders(request, header.c_str(),
                                          static_cast<DWORD>(header.size()),
                                          WINHTTP_ADDREQ_FLAG_ADD))
                return failure();
        }
        if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA,
                                0, 0, 0) ||
            !WinHttpReceiveResponse(request, nullptr))
            return failure();

        DWORD status = 0;
        DWORD status_size = sizeof(status);
        if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                                 WINHTTP_NO_HEADER_INDEX) ||
            status != HTTP_STATUS_SWITCH_PROTOCOLS)
            return cancelled() ? NK_TRANSPORT_ERROR_CANCELED : NK_TRANSPORT_ERROR_PROTOCOL;

        wchar_t selected[256]{};
        DWORD selected_size = sizeof(selected);
        if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Sec-WebSocket-Protocol", selected,
                                &selected_size, WINHTTP_NO_HEADER_INDEX)) {
            const auto chosen = narrow(selected, selected_size / sizeof(wchar_t));
            if (!chosen.empty() && !subprotocol_offered(endpoint.subprotocols, chosen))
                return NK_TRANSPORT_ERROR_PROTOCOL;
        }

        HINTERNET socket = WinHttpWebSocketCompleteUpgrade(request, 0);
        if (!keep(websocket_, socket))
            return failure();
        {
            std::lock_guard lock(mutex_);
            if (request_) {
                WinHttpCloseHandle(request_);
                request_ = nullptr;
            }
        }
        /* The handshake timeout no longer applies: receives wait until a message or close. */
        DWORD unlimited = 0;
        WinHttpSetOption(socket, WINHTTP_OPTION_RECEIVE_TIMEOUT, &unlimited, sizeof(unlimited));
        return NK_OK;
    }

    std::mutex mutex_;
    bool cancelled_ = false;
    HINTERNET session_ = nullptr;
    HINTERNET connection_ = nullptr;
    HINTERNET request_ = nullptr;
    HINTERNET websocket_ = nullptr;
};

} // namespace

bool secure_client_available() noexcept {
    return true;
}

std::unique_ptr<SecureWebSocket> create_secure_websocket() noexcept {
    return std::unique_ptr<SecureWebSocket>(new (std::nothrow) WinHttpWebSocket());
}

} // namespace nk::transport
