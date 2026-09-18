#include "net_backend.hpp"

#include <emscripten.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::mutex fetch_mutex;
std::unordered_map<nk_request_id, nk::net::RequestPtr> fetch_requests;

nk::net::RequestPtr find_request(nk_request_id id) {
    std::lock_guard lock(fetch_mutex);
    const auto found = fetch_requests.find(id);
    return found == fetch_requests.end() ? nk::net::RequestPtr{} : found->second;
}

void erase_request(nk_request_id id) {
    std::lock_guard lock(fetch_mutex);
    fetch_requests.erase(id);
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

std::string trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
        value.remove_suffix(1);
    return std::string(value);
}

uint64_t content_length(const std::vector<nk::net::OwnedHeader> &headers) {
    for (const auto &header : headers) {
        if (!lower_equal(header.name, "content-length"))
            continue;
        const auto value = trim(header.value);
        if (value.empty())
            return NK_HTTP_CONTENT_LENGTH_UNKNOWN;
        uint64_t result = 0;
        for (const auto character : value) {
            if (character < '0' || character > '9' ||
                result > (UINT64_MAX - static_cast<uint64_t>(character - '0')) / 10)
                return NK_HTTP_CONTENT_LENGTH_UNKNOWN;
            result = result * 10 + static_cast<uint64_t>(character - '0');
        }
        return result;
    }
    return NK_HTTP_CONTENT_LENGTH_UNKNOWN;
}

const char *method_name(nk_http_method method) {
    switch (method) {
    case NK_HTTP_METHOD_GET:
        return "GET";
    case NK_HTTP_METHOD_POST:
        return "POST";
    case NK_HTTP_METHOD_PUT:
        return "PUT";
    case NK_HTTP_METHOD_PATCH:
        return "PATCH";
    case NK_HTTP_METHOD_DELETE:
        return "DELETE";
    case NK_HTTP_METHOD_HEAD:
        return "HEAD";
    case NK_HTTP_METHOD_OPTIONS:
        return "OPTIONS";
    case NK_HTTP_METHOD_TRACE:
        return "TRACE";
    case NK_HTTP_METHOD_CONNECT:
        return "CONNECT";
    default:
        return nullptr;
    }
}

bool scheme_is(std::string_view url, std::string_view scheme) {
    const auto separator = url.find("://");
    if (separator == std::string_view::npos || separator != scheme.size())
        return false;
    return lower_equal(url.substr(0, separator), scheme);
}

bool progress_due(nk::net::RequestContext &request) {
    const auto now = std::chrono::steady_clock::now();
    if (request.last_progress.time_since_epoch().count() != 0 &&
        now - request.last_progress < std::chrono::milliseconds(50))
        return false;
    request.last_progress = now;
    return true;
}

// clang-format off
EM_JS(void, start_fetch,
      (double id, const char *url, const char *method, const char *headers, uintptr_t body,
       uint32_t body_size, double max_header_size, double max_response_size,
       uint32_t redirect_limit, uint32_t allow_https_to_http),
      {
          const states = Module['NativeKitFetches'] || (Module['NativeKitFetches'] = new Map());
          const state = {controller : new AbortController(), failed : 0};
          states.set(id, state);
          const header_text = UTF8ToString(headers);
          const header_lines = header_text.length === 0 ? [] : header_text.split('\n');
          const request_headers = new Headers();
          for (let index = 0; index + 1 < header_lines.length; index += 2)
              request_headers.append(header_lines[index], header_lines[index + 1]);
          const request_url = UTF8ToString(url);
          let request_method = UTF8ToString(method);
          let request_body =
              body_size === 0 ? null : HEAPU8.slice(Number(body), Number(body) + body_size);
          const call_headers = (status, response, redirected) => {
              let text = '';
              response.headers.forEach((value, name) => { text += name + '\n' + value + '\n'; });
              if (lengthBytesUTF8(text) > Number(max_header_size))
                  return -106;
              const length = lengthBytesUTF8(text) + 1;
              const pointer = _malloc(length);
              stringToUTF8(text, pointer, length);
              const result = Module.ccall('nk_net_fetch_headers', 'number',
                                          [ 'number', 'number', 'number', 'number' ],
                                          [ id, status, pointer, redirected ? 1 : 0 ]);
              _free(pointer);
              return result;
          };
          const call_data = (bytes, total) => {
              if (bytes.length > Number(max_response_size))
                  return -106;
              const pointer = _malloc(bytes.length);
              HEAPU8.set(bytes, pointer);
              const result = Module.ccall('nk_net_fetch_data', 'number',
                                          [ 'number', 'number', 'number', 'number' ],
                                          [ id, pointer, bytes.length, total ]);
              _free(pointer);
              return result;
          };
          const finish = (result) => {
              if (!states.has(id))
                  return;
              states.delete(id);
              Module.ccall('nk_net_fetch_complete', null, [ 'number', 'number' ], [ id, result ]);
          };
          (async() =>
               {
                   let current_url = request_url;
                   let redirects = 0;
                   for (;;) {
                       const request_options = {
                           method : request_method,
                           headers : request_headers,
                           signal : state.controller.signal,
                           redirect : 'manual',
                           credentials : 'omit',
                           cache : 'no-store'
                       };
                       if (request_body !== null)
                           request_options.body = request_body.slice();
                       const response = await fetch(current_url, request_options);
                       if (response.type === 'opaqueredirect') {
                           state.failed = -107;
                           throw new Error('opaque redirect cannot be inspected');
                       }
                       const location = response.headers.get('location');
                       if (response.status >= 300 && response.status <= 399 &&
                           location !== null) {
                           if (redirects >= redirect_limit) {
                               state.failed = -107;
                               throw new Error('redirect limit exceeded');
                           }
                           let next_url = '';
                           try {
                               next_url = new URL(location, current_url).href;
                           } catch (error) {
                               state.failed = -107;
                               throw new Error('invalid redirect URL');
                           }
                           const downgrade = current_url.substring(0, 8).toLowerCase() ===
                                                  'https://' &&
                                              next_url.substring(0, 7).toLowerCase() === 'http://';
                           if (downgrade && !allow_https_to_http) {
                               state.failed = -107;
                               throw new Error('HTTPS to HTTP redirect rejected');
                           }
                           if (response.status === 301 || response.status === 302 ||
                               response.status === 303) {
                               if (request_method !== 'GET' && request_method !== 'HEAD') {
                                   request_method = 'GET';
                                   request_body = null;
                               }
                           }
                           current_url = next_url;
                           redirects++;
                           continue;
                       }
                       let result = call_headers(response.status, response, redirects !== 0);
                       if (result !== 0) {
                           state.failed = result;
                           throw new Error('NativeKit response headers rejected');
                       }
                       const total_header = response.headers.get('content-length');
                       const total = total_header === null ? -1 : Number(total_header);
                       if (!response.body) {
                           finish(0);
                           return;
                       }
                       const reader = response.body.getReader();
                       for (;;) {
                           const part = await reader.read();
                           if (part.done)
                               break;
                           result = call_data(part.value, Number.isFinite(total) ? total : -1);
                           if (result !== 0) {
                               state.failed = result;
                               await reader.cancel();
                               throw new Error('NativeKit response body rejected');
                           }
                       }
                       finish(0);
                       return;
                   }
               })()
              .catch((error) => {
                  const result = state.failed !== 0 ? state.failed
                                                    : error && error.name === 'AbortError' ? -104 : -101;
                  finish(result);
              });
      });
// clang-format on

EM_JS(void, cancel_fetch, (double id), {
    const states = Module['NativeKitFetches'];
    if (states && states.has(id))
        states.get(id).controller.abort();
});

} // namespace

extern "C" {

EMSCRIPTEN_KEEPALIVE int32_t nk_net_fetch_headers(double id, int32_t status, const char *lines,
                                                  int32_t redirected) {
    try {
        const auto request = find_request(static_cast<nk_request_id>(id));
        if (!request)
            return NK_HTTP_ERROR_CANCELED;
        std::vector<nk::net::OwnedHeader> headers;
        std::string_view text(lines ? lines : "");
        std::size_t index = 0;
        while (index < text.size()) {
            const auto name_end = text.find('\n', index);
            if (name_end == std::string_view::npos)
                return NK_HTTP_ERROR_PROTOCOL;
            const auto value_begin = name_end + 1;
            const auto value_end = text.find('\n', value_begin);
            if (value_end == std::string_view::npos)
                return NK_HTTP_ERROR_PROTOCOL;
            auto name = trim(text.substr(index, name_end - index));
            auto value = trim(text.substr(value_begin, value_end - value_begin));
            headers.push_back({std::move(name), std::move(value)});
            index = value_end + 1;
        }
        const auto length = content_length(headers);
        if (redirected) {
            std::lock_guard lock(request->mutex);
            request->response_flags |= NK_HTTP_RESPONSE_REDIRECTED;
        }
        return nk::net::receive_response_headers(request, static_cast<uint32_t>(status),
                                                 std::move(headers), length);
    } catch (const std::bad_alloc &) {
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return NK_HTTP_ERROR_PROTOCOL;
    }
}

EMSCRIPTEN_KEEPALIVE int32_t nk_net_fetch_data(double id, const void *data, uint32_t size,
                                               double total) {
    try {
        const auto request = find_request(static_cast<nk_request_id>(id));
        if (!request)
            return NK_HTTP_ERROR_CANCELED;
        if (total >= 0 && total <= static_cast<double>(UINT64_MAX))
            nk::net::set_response_total(request, static_cast<uint64_t>(total));
        const auto result = nk::net::receive_response_data(
            request, static_cast<const std::byte *>(data), static_cast<std::size_t>(size));
        if (result == NK_OK && progress_due(*request))
            nk::net::emit_progress(request, request->received, request->total, 0, 0);
        return result;
    } catch (const std::bad_alloc &) {
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return NK_HTTP_ERROR_PROTOCOL;
    }
}

EMSCRIPTEN_KEEPALIVE void nk_net_fetch_complete(double id, int32_t result) {
    const auto request_id = static_cast<nk_request_id>(id);
    const auto request = find_request(request_id);
    if (!request)
        return;
    nk::net::complete_request(request, static_cast<nk_result>(result));
    erase_request(request_id);
    nk::net::worker_finished(request);
}

} // extern "C"

namespace nk::net {

nk_capabilities capabilities() noexcept {
    return NK_CAP_HTTP_CLIENT;
}

nk_result backend_start(const RequestPtr &request) noexcept {
    try {
        if (request->client->config.cookie_policy != NK_HTTP_COOKIES_DISABLED ||
            request->client->config.cache_policy != NK_HTTP_CACHE_DISABLED ||
            request->client->config.proxy.kind != NK_HTTP_PROXY_NONE ||
            request->client->config.tls.flags != 0 ||
            request->client->config.tls.minimum_version != NK_HTTP_TLS_DEFAULT ||
            !request->client->config.tls.ca_bundle_path.empty() ||
            request->request.upload_stream != NK_INVALID_HANDLE)
            return NK_ERROR_UNSUPPORTED;
        if (request->request.body.size() > UINT32_MAX)
            return NK_ERROR_INVALID_ARGUMENT;
        const auto *method = method_name(request->request.method);
        if (!method)
            return NK_ERROR_INVALID_ARGUMENT;
        std::string headers;
        for (const auto &header : request->client->config.default_headers) {
            headers += header.name;
            headers += '\n';
            headers += header.value;
            headers += '\n';
        }
        for (const auto &header : request->request.headers) {
            headers += header.name;
            headers += '\n';
            headers += header.value;
            headers += '\n';
        }
        {
            std::lock_guard lock(fetch_mutex);
            fetch_requests.emplace(request->id, request);
        }
        start_fetch(static_cast<double>(request->id), request->request.url.c_str(), method,
                    headers.c_str(),
                    request->request.body.empty()
                        ? 0
                        : reinterpret_cast<uintptr_t>(request->request.body.data()),
                    static_cast<uint32_t>(request->request.body.size()),
                    request->client->config.max_header_size, request->request.max_response_size,
                    request->request.redirect_limit,
                    (request->client->config.flags & NK_HTTP_CLIENT_ALLOW_HTTPS_TO_HTTP) != 0);
        return NK_OK;
    } catch (const std::bad_alloc &) {
        erase_request(request->id);
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        erase_request(request->id);
        return NK_ERROR_UNKNOWN;
    }
}

void backend_cancel(const RequestPtr &request) noexcept {
    cancel_fetch(static_cast<double>(request->id));
    /* AbortController settles the Fetch promise asynchronously. Complete the
     * NativeKit request here as well so nk_shutdown() cannot block the browser
     * main thread waiting for that promise to run. The later JS completion is
     * harmless because the request has already been removed from fetch_requests. */
    nk_net_fetch_complete(static_cast<double>(request->id), NK_HTTP_ERROR_CANCELED);
}

void backend_shutdown() noexcept {}

} // namespace nk::net
