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

#include "net_backend.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

struct WinClientState {
    HINTERNET session = nullptr;

    ~WinClientState() {
        if (session)
            WinHttpCloseHandle(session);
    }
};

struct ActiveRequest {
    HINTERNET handle = nullptr;
    std::atomic<bool> closed{false};
};

std::mutex active_requests_mutex;
std::unordered_map<nk_request_id, std::shared_ptr<ActiveRequest>> active_requests;

void close_active(const std::shared_ptr<ActiveRequest> &active) noexcept {
    if (active && !active->closed.exchange(true, std::memory_order_acq_rel) && active->handle)
        WinHttpCloseHandle(active->handle);
}

void set_active(nk_request_id id, const std::shared_ptr<ActiveRequest> &active) {
    std::lock_guard lock(active_requests_mutex);
    active_requests[id] = active;
}

void clear_active(nk_request_id id, const std::shared_ptr<ActiveRequest> &active) {
    std::lock_guard lock(active_requests_mutex);
    const auto found = active_requests.find(id);
    if (found != active_requests.end() && found->second == active)
        active_requests.erase(found);
}

std::wstring wide(std::string_view value, bool &valid) {
    valid = false;
    if (value.size() > static_cast<std::size_t>(INT_MAX))
        return {};
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

std::string utf8(const wchar_t *value, std::size_t length, bool &valid) {
    valid = false;
    if (!value || length > static_cast<std::size_t>(INT_MAX))
        return {};
    const auto bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value,
                                           static_cast<int>(length), nullptr, 0, nullptr, nullptr);
    if (bytes <= 0)
        return {};
    std::string result(static_cast<std::size_t>(bytes), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, static_cast<int>(length),
                            result.data(), bytes, nullptr, nullptr) != bytes)
        return {};
    valid = true;
    return result;
}

bool lower_equal(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index])))
            return false;
    return true;
}

bool scheme_is(std::string_view url, std::string_view scheme) {
    const auto separator = url.find("://");
    return separator != std::string_view::npos && lower_equal(url.substr(0, separator), scheme);
}

struct ParsedUrl {
    std::wstring full;
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = INTERNET_DEFAULT_HTTP_PORT;
    bool secure = false;
};

bool parse_url(std::string_view value, ParsedUrl &out) {
    bool valid = false;
    out.full = wide(value, valid);
    if (!valid || out.full.empty())
        return false;
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    if (!WinHttpCrackUrl(out.full.c_str(), 0, 0, &components) || !components.lpszHostName ||
        components.dwHostNameLength == 0 || !components.lpszScheme || components.dwSchemeLength == 0)
        return false;
    bool host_valid = false;
    const auto host = utf8(components.lpszHostName, components.dwHostNameLength, host_valid);
    if (!host_valid)
        return false;
    out.host = wide(host, valid);
    if (!valid)
        return false;
    out.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    if (components.nScheme != INTERNET_SCHEME_HTTP && !out.secure)
        return false;
    out.port = components.nPort != 0
                   ? components.nPort
                   : (out.secure ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT);
    out.path.clear();
    if (components.lpszUrlPath && components.dwUrlPathLength != 0)
        out.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.lpszExtraInfo && components.dwExtraInfoLength != 0)
        out.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    if (out.path.empty())
        out.path = L"/";
    else if (out.path.front() != L'/')
        out.path.insert(out.path.begin(), L'/');
    return true;
}

std::string origin(std::string_view url) {
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos)
        return {};
    const auto authority_end = url.find_first_of("/?#", scheme_end + 3);
    return std::string(url.substr(0, authority_end == std::string_view::npos ? url.size() : authority_end));
}

std::string resolve_redirect(std::string_view base, std::string_view location) {
    if (scheme_is(location, "http") || scheme_is(location, "https"))
        return std::string(location);
    const auto base_origin = origin(base);
    if (base_origin.empty() || location.empty())
        return {};
    if (location.substr(0, 2) == "//") {
        const auto scheme_end = base.find("://");
        return std::string(base.substr(0, scheme_end)) + ":" + std::string(location);
    }
    if (location.front() == '/')
        return base_origin + std::string(location);
    const auto query = base.find_first_of("?#");
    const auto path_end = base.find_last_of('/', query == std::string_view::npos ? base.size() : query);
    const auto prefix_end = path_end == std::string_view::npos ? base_origin.size() : path_end + 1;
    return std::string(base.substr(0, prefix_end)) + std::string(location);
}

std::string trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1);
    return std::string(value);
}

uint64_t parse_length(std::string_view value) {
    const auto cleaned = trim(value);
    if (cleaned.empty())
        return NK_HTTP_CONTENT_LENGTH_UNKNOWN;
    uint64_t result = 0;
    for (const auto character : cleaned) {
        if (character < '0' || character > '9' ||
            result > (UINT64_MAX - static_cast<uint64_t>(character - '0')) / 10)
            return NK_HTTP_CONTENT_LENGTH_UNKNOWN;
        result = result * 10 + static_cast<uint64_t>(character - '0');
    }
    return result;
}

struct ResponseHeaders {
    std::vector<nk::net::OwnedHeader> values;
    std::string location;
    uint64_t content_length = NK_HTTP_CONTENT_LENGTH_UNKNOWN;
};

bool parse_headers(const std::vector<wchar_t> &raw, ResponseHeaders &out) {
    std::size_t begin = 0;
    bool first_line = true;
    while (begin < raw.size() && raw[begin] != L'\0') {
        std::size_t end = begin;
        while (end + 1 < raw.size() && !(raw[end] == L'\r' && raw[end + 1] == L'\n'))
            ++end;
        if (end == begin)
            break;
        if (!first_line) {
            std::size_t separator = begin;
            while (separator < end && raw[separator] != L':')
                ++separator;
            if (separator != end) {
                bool name_valid = false;
                bool value_valid = false;
                auto name = utf8(raw.data() + begin, separator - begin, name_valid);
                auto value = utf8(raw.data() + separator + 1, end - separator - 1, value_valid);
                if (!name_valid || !value_valid)
                    return false;
                name = trim(name);
                value = trim(value);
                if (name.empty())
                    return false;
                if (lower_equal(name, "location"))
                    out.location = value;
                if (lower_equal(name, "content-length"))
                    out.content_length = parse_length(value);
                out.values.push_back({std::move(name), std::move(value)});
            }
        }
        first_line = false;
        begin = end + (end + 1 < raw.size() ? 2 : 0);
    }
    return true;
}

nk_result map_error(DWORD error, const nk::net::RequestContext &request) {
    if (request.canceled.load(std::memory_order_acquire))
        return NK_HTTP_ERROR_CANCELED;
    switch (error) {
    case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        return NK_HTTP_ERROR_DNS;
    case ERROR_WINHTTP_CANNOT_CONNECT:
    case ERROR_WINHTTP_CONNECTION_ERROR:
    case ERROR_WINHTTP_CONNECTION_ABORTED:
    case ERROR_WINHTTP_CONNECTION_RESET:
        return NK_HTTP_ERROR_CONNECTION;
    case ERROR_WINHTTP_SECURE_FAILURE:
    case ERROR_WINHTTP_CLIENT_AUTH_CERT_NEEDED:
        return NK_HTTP_ERROR_TLS;
    case ERROR_WINHTTP_TIMEOUT:
        return NK_HTTP_ERROR_TIMEOUT;
    case ERROR_WINHTTP_REDIRECT_FAILED:
        return NK_HTTP_ERROR_REDIRECT;
    case ERROR_WINHTTP_INVALID_URL:
    case ERROR_WINHTTP_INVALID_SERVER_RESPONSE:
        return NK_HTTP_ERROR_PROTOCOL;
    case ERROR_WINHTTP_UNABLE_TO_DOWNLOAD_SCRIPT:
    case ERROR_WINHTTP_AUTODETECTION_FAILED:
        return NK_HTTP_ERROR_PROXY;
    default:
        return NK_HTTP_ERROR_CONNECTION;
    }
}

std::shared_ptr<WinClientState> client_state(const nk::net::RequestPtr &request,
                                             nk_result &result) {
    std::lock_guard lock(request->client->backend_mutex);
    if (request->client->backend_state)
        return std::static_pointer_cast<WinClientState>(request->client->backend_state);
    const auto &proxy = request->client->config.proxy;
    if (proxy.kind == NK_HTTP_PROXY_SOCKS5) {
        result = NK_ERROR_UNSUPPORTED;
        return {};
    }
    bool proxy_valid = proxy.kind == NK_HTTP_PROXY_NONE;
    const auto proxy_text = wide(proxy.url, proxy_valid);
    if (proxy.kind != NK_HTTP_PROXY_NONE && !proxy_valid) {
        result = NK_ERROR_INVALID_ARGUMENT;
        return {};
    }
    const auto access = proxy.kind == NK_HTTP_PROXY_NONE ? WINHTTP_ACCESS_TYPE_DEFAULT_PROXY
                                                          : WINHTTP_ACCESS_TYPE_NAMED_PROXY;
    auto *session = WinHttpOpen(L"NativeKit/1", access,
                                proxy.kind == NK_HTTP_PROXY_NONE ? nullptr : proxy_text.c_str(),
                                nullptr, 0);
    if (!session) {
        result = map_error(GetLastError(), *request);
        return {};
    }
    if (!proxy.username.empty()) {
        bool user_valid = false;
        bool password_valid = false;
        const auto user = wide(proxy.username, user_valid);
        const auto password = wide(proxy.password, password_valid);
        if (!user_valid || !password_valid ||
            !WinHttpSetOption(session, WINHTTP_OPTION_PROXY_USERNAME,
                              user.data(), static_cast<DWORD>((user.size() + 1) * sizeof(wchar_t))) ||
            !WinHttpSetOption(session, WINHTTP_OPTION_PROXY_PASSWORD, password.data(),
                              static_cast<DWORD>((password.size() + 1) * sizeof(wchar_t)))) {
            WinHttpCloseHandle(session);
            result = NK_HTTP_ERROR_PROXY;
            return {};
        }
    }
    if (request->client->config.cookie_policy == NK_HTTP_COOKIES_DISABLED) {
        DWORD features = WINHTTP_DISABLE_COOKIES;
        if (!WinHttpSetOption(session, WINHTTP_OPTION_DISABLE_FEATURE, &features, sizeof(features))) {
            WinHttpCloseHandle(session);
            result = NK_ERROR_UNSUPPORTED;
            return {};
        }
    }
    auto state = std::make_shared<WinClientState>();
    state->session = session;
    request->client->backend_state = state;
    result = NK_OK;
    return state;
}

bool request_headers(const nk::net::RequestPtr &request, std::wstring &output) {
    auto append = [&](const std::vector<nk::net::OwnedHeader> &headers) {
        for (const auto &header : headers) {
            bool name_valid = false;
            bool value_valid = false;
            const auto name = wide(header.name, name_valid);
            const auto value = wide(header.value, value_valid);
            if (!name_valid || !value_valid)
                return false;
            output.append(name);
            output.append(L": ");
            output.append(value);
            output.append(L"\r\n");
        }
        return true;
    };
    return append(request->client->config.default_headers) && append(request->request.headers);
}

bool configure_security(HINTERNET request, const nk::net::RequestPtr &context) {
    const auto &tls = context->client->config.tls;
    if (tls.flags != 0) {
        DWORD flags = 0;
        if (tls.flags & NK_HTTP_TLS_DISABLE_PEER_VERIFICATION)
            flags |= SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                     SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
        if (tls.flags & NK_HTTP_TLS_DISABLE_HOSTNAME_VERIFICATION)
            flags |= SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
        if (!WinHttpSetOption(request, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags)))
            return false;
    }
    if (tls.minimum_version != NK_HTTP_TLS_DEFAULT) {
        DWORD protocols = 0;
        if (tls.minimum_version == NK_HTTP_TLS_1_2)
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
        else if (tls.minimum_version == NK_HTTP_TLS_1_3)
            protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#else
        else
            return false;
#endif
        if (!WinHttpSetOption(request, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                              sizeof(protocols)))
            return false;
    }
    return true;
}

bool query_headers(HINTERNET request, const nk::net::RequestPtr &context, ResponseHeaders &out) {
    DWORD characters = 0;
    SetLastError(ERROR_SUCCESS);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                            WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &characters, nullptr))
        return false;
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || characters == 0)
        return false;
    const auto maximum = context->client->config.max_header_size >
                                 (std::numeric_limits<DWORD>::max() - 2u) / 2u
                             ? std::numeric_limits<DWORD>::max()
                             : static_cast<DWORD>(context->client->config.max_header_size * 2u + 2u);
    if (characters > maximum)
        return false;
    std::vector<wchar_t> raw(characters);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                             WINHTTP_HEADER_NAME_BY_INDEX, raw.data(), &characters, nullptr))
        return false;
    raw.resize(characters);
    return parse_headers(raw, out);
}

bool set_timeout(HINTERNET request, uint32_t timeout_ms) {
    const auto timeout = static_cast<int>(std::min<uint64_t>(timeout_ms, INT_MAX));
    return WinHttpSetTimeouts(request, timeout, timeout, timeout, timeout) != FALSE;
}

std::wstring method_name(nk_http_method method, bool &valid) {
    switch (method) {
    case NK_HTTP_METHOD_GET: valid = true; return L"GET";
    case NK_HTTP_METHOD_POST: valid = true; return L"POST";
    case NK_HTTP_METHOD_PUT: valid = true; return L"PUT";
    case NK_HTTP_METHOD_PATCH: valid = true; return L"PATCH";
    case NK_HTTP_METHOD_DELETE: valid = true; return L"DELETE";
    case NK_HTTP_METHOD_HEAD: valid = true; return L"HEAD";
    case NK_HTTP_METHOD_OPTIONS: valid = true; return L"OPTIONS";
    case NK_HTTP_METHOD_TRACE: valid = true; return L"TRACE";
    case NK_HTTP_METHOD_CONNECT: valid = true; return L"CONNECT";
    default: valid = false; return {};
    }
}

nk_result perform_impl(nk::net::RequestPtr request) {
    if (request->canceled.load(std::memory_order_acquire))
        return NK_HTTP_ERROR_CANCELED;
    if (!request->client->config.tls.ca_bundle_path.empty())
        return NK_ERROR_UNSUPPORTED;
    nk_result state_result = NK_OK;
    const auto state = client_state(request, state_result);
    if (!state)
        return state_result;

    bool valid_method = false;
    const auto method = method_name(request->request.method, valid_method);
    if (!valid_method)
        return NK_HTTP_ERROR_PROTOCOL;
    std::string current_url = request->request.url;
    uint32_t redirects = 0;
    bool redirected = false;
    for (;;) {
        if (request->canceled.load(std::memory_order_acquire))
            return NK_HTTP_ERROR_CANCELED;
        ParsedUrl url;
        if (!parse_url(current_url, url))
            return NK_HTTP_ERROR_PROTOCOL;
        std::wstring headers;
        if (!request_headers(request, headers))
            return NK_ERROR_INVALID_ARGUMENT;
        if (request->request.body.size() > UINT32_MAX)
            return NK_ERROR_INVALID_ARGUMENT;
        auto *connect = WinHttpConnect(state->session, url.host.c_str(), url.port, 0);
        if (!connect)
            return map_error(GetLastError(), *request);
        auto *win_request = WinHttpOpenRequest(connect, method.c_str(), url.path.c_str(), nullptr,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               url.secure ? WINHTTP_FLAG_SECURE : 0);
        if (!win_request) {
            const auto error = GetLastError();
            WinHttpCloseHandle(connect);
            return map_error(error, *request);
        }
        auto active = std::make_shared<ActiveRequest>();
        active->handle = win_request;
        set_active(request->id, active);
        auto close_request = [&] {
            clear_active(request->id, active);
            close_active(active);
            WinHttpCloseHandle(connect);
        };
        if (!set_timeout(win_request, request->request.timeout_ms) || !configure_security(win_request, request)) {
            close_request();
            return NK_HTTP_ERROR_TLS;
        }
        DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(win_request, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy,
                              sizeof(redirect_policy))) {
            const auto error = GetLastError();
            close_request();
            return map_error(error, *request);
        }
        const void *body = request->request.body.empty() ? nullptr : request->request.body.data();
        DWORD body_size = static_cast<DWORD>(request->request.body.size());
        DWORD total_length = body_size;
        if (request->request.upload_stream != NK_INVALID_HANDLE) {
            body = nullptr;
            body_size = 0;
            total_length = request->request.upload_size <= UINT32_MAX
                               ? static_cast<DWORD>(request->request.upload_size)
                               : WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH;
        }
        const auto header_length = headers.empty() ? 0 : static_cast<DWORD>(-1L);
        if (!WinHttpSendRequest(win_request,
                                headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
                                header_length, const_cast<void *>(body), body_size, total_length, 0)) {
            const auto error = GetLastError();
            close_request();
            return map_error(error, *request);
        }
        if (request->request.upload_stream != NK_INVALID_HANDLE) {
            std::vector<std::byte> buffer(64u * 1024u);
            for (;;) {
                if (request->canceled.load(std::memory_order_acquire)) {
                    close_request();
                    return NK_HTTP_ERROR_CANCELED;
                }
                uint64_t read = 0;
                const auto read_result = nk_resource_read(request->request.upload_stream, buffer.data(),
                                                          buffer.size(), &read);
                if (read_result != NK_OK) {
                    close_request();
                    return NK_HTTP_ERROR_PROTOCOL;
                }
                if (read == 0)
                    break;
                DWORD written = 0;
                if (!WinHttpWriteData(win_request, buffer.data(), static_cast<DWORD>(read), &written) ||
                    written != read) {
                    const auto error = GetLastError();
                    close_request();
                    return map_error(error, *request);
                }
                request->upload_position += written;
                nk::net::emit_progress(request, 0, request->total, request->upload_position,
                                       request->request.upload_size == 0 ? NK_HTTP_CONTENT_LENGTH_UNKNOWN
                                                                        : request->request.upload_size);
            }
        }
        if (!WinHttpReceiveResponse(win_request, nullptr)) {
            const auto error = GetLastError();
            close_request();
            return map_error(error, *request);
        }
        ResponseHeaders response;
        if (!query_headers(win_request, request, response)) {
            const auto error = GetLastError();
            close_request();
            return error == ERROR_WINHTTP_HEADER_SIZE_OVERFLOW ? NK_HTTP_ERROR_RESPONSE_LIMIT
                                                                : NK_HTTP_ERROR_PROTOCOL;
        }
        DWORD status = 0;
        DWORD status_size = sizeof(status);
        if (!WinHttpQueryHeaders(win_request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, nullptr)) {
            const auto error = GetLastError();
            close_request();
            return map_error(error, *request);
        }
        if (status >= 300 && status <= 399 && !response.location.empty()) {
            const auto next_url = resolve_redirect(current_url, response.location);
            const bool valid_next = scheme_is(next_url, "http") || scheme_is(next_url, "https");
            const bool downgrade = scheme_is(current_url, "https") && scheme_is(next_url, "http");
            if (next_url.empty() || !valid_next ||
                (downgrade && !(request->client->config.flags & NK_HTTP_CLIENT_ALLOW_HTTPS_TO_HTTP)) ||
                redirects >= request->request.redirect_limit) {
                close_request();
                return NK_HTTP_ERROR_REDIRECT;
            }
            if (request->request.upload_stream != NK_INVALID_HANDLE) {
                uint64_t position = 0;
                if (nk_resource_seek(request->request.upload_stream, 0, NK_SEEK_START, &position) != NK_OK) {
                    close_request();
                    return NK_HTTP_ERROR_REDIRECT;
                }
                request->upload_position = position;
            }
            ++redirects;
            redirected = true;
            current_url = next_url;
            close_request();
            continue;
        }
        if (redirected) {
            std::lock_guard lock(request->mutex);
            request->response_flags |= NK_HTTP_RESPONSE_REDIRECTED;
        }
        const auto header_result = nk::net::receive_response_headers(
            request, status, std::move(response.values), response.content_length);
        if (header_result != NK_OK) {
            close_request();
            return header_result;
        }
        std::vector<std::byte> buffer(64u * 1024u);
        for (;;) {
            if (request->canceled.load(std::memory_order_acquire)) {
                close_request();
                return NK_HTTP_ERROR_CANCELED;
            }
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(win_request, &available)) {
                const auto error = GetLastError();
                close_request();
                return map_error(error, *request);
            }
            if (available == 0)
                break;
            const auto amount = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
            DWORD received = 0;
            if (!WinHttpReadData(win_request, buffer.data(), amount, &received)) {
                const auto error = GetLastError();
                close_request();
                return map_error(error, *request);
            }
            if (received == 0)
                break;
            const auto data_result = nk::net::receive_response_data(
                request, buffer.data(), static_cast<std::size_t>(received));
            if (data_result != NK_OK) {
                close_request();
                return data_result;
            }
        }
        close_request();
        return NK_OK;
    }
}

void perform(nk::net::RequestPtr request) noexcept {
    try {
        nk::net::complete_request(request, perform_impl(request));
    } catch (const std::bad_alloc &) {
        nk::net::complete_request(request, NK_ERROR_OUT_OF_MEMORY);
    } catch (...) {
        nk::net::complete_request(request, NK_ERROR_UNKNOWN);
    }
    nk::net::worker_finished(request);
}

} // namespace

namespace nk::net {

nk_capabilities capabilities() noexcept {
    return NK_CAP_HTTP_CLIENT | NK_CAP_HTTP_STREAMING;
}

nk_result backend_start(const RequestPtr &request) noexcept {
    try {
        std::thread([request] { perform(request); }).detach();
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return NK_ERROR_UNKNOWN;
    }
}

void backend_cancel(const RequestPtr &request) noexcept {
    std::shared_ptr<ActiveRequest> active;
    {
        std::lock_guard lock(active_requests_mutex);
        const auto found = active_requests.find(request->id);
        if (found != active_requests.end())
            active = found->second;
    }
    close_active(active);
}

void backend_shutdown() noexcept {}

} // namespace nk::net
