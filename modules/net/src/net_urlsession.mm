#import <Foundation/Foundation.h>
#import <CFNetwork/CFNetwork.h>

#include "net_backend.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

std::mutex active_tasks_mutex;

bool lower_equal(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t index = 0; index < left.size(); ++index)
        if (std::tolower(static_cast<unsigned char>(left[index])) !=
            std::tolower(static_cast<unsigned char>(right[index])))
            return false;
    return true;
}

bool https_url(NSString *url) {
    const auto *value = [url UTF8String];
    if (!value)
        return false;
    const std::string_view text(value);
    return text.size() >= 8 && lower_equal(text.substr(0, 8), "https://");
}

bool http_url(NSURL *url) {
    return lower_equal([[url scheme] UTF8String] ? [[url scheme] UTF8String] : "", "http");
}

std::string native_string(id value) {
    NSString *string = [value isKindOfClass:[NSString class]] ? value : [value description];
    const auto *utf8 = string ? [string UTF8String] : nullptr;
    return utf8 ? std::string(utf8) : std::string{};
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

uint64_t content_length(NSDictionary *headers, long long expected) {
    if (expected >= 0)
        return static_cast<uint64_t>(expected);
    for (id key in headers) {
        if (!lower_equal(native_string(key), "content-length"))
            continue;
        uint64_t result = 0;
        const auto value = native_string([headers objectForKey:key]);
        if (!value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char character) {
                return character >= '0' && character <= '9';
            })) {
            for (const auto character : value) {
                if (result > (UINT64_MAX - static_cast<uint64_t>(character - '0')) / 10)
                    return NK_HTTP_CONTENT_LENGTH_UNKNOWN;
                result = result * 10 + static_cast<uint64_t>(character - '0');
            }
            return result;
        }
    }
    return NK_HTTP_CONTENT_LENGTH_UNKNOWN;
}

std::vector<nk::net::OwnedHeader> response_headers(NSDictionary *headers) {
    std::vector<nk::net::OwnedHeader> result;
    for (id key in headers) {
        const auto name = native_string(key);
        const auto value = native_string([headers objectForKey:key]);
        result.push_back({name, value});
    }
    return result;
}

nk_result map_error(NSError *error, const nk::net::RequestPtr &request) {
    if (request->canceled.load(std::memory_order_acquire))
        return NK_HTTP_ERROR_CANCELED;
    if (!error)
        return NK_OK;
    const auto code = [error code];
    if ([error.domain isEqualToString:NSURLErrorDomain]) {
        switch (code) {
        case NSURLErrorCannotFindHost:
        case NSURLErrorDNSLookupFailed:
            return NK_HTTP_ERROR_DNS;
        case NSURLErrorTimedOut:
            return NK_HTTP_ERROR_TIMEOUT;
        case NSURLErrorCancelled:
            return NK_HTTP_ERROR_CANCELED;
        case NSURLErrorSecureConnectionFailed:
        case NSURLErrorServerCertificateHasBadDate:
        case NSURLErrorServerCertificateUntrusted:
        case NSURLErrorServerCertificateHasUnknownRoot:
        case NSURLErrorServerCertificateNotYetValid:
        case NSURLErrorClientCertificateRejected:
        case NSURLErrorClientCertificateRequired:
            return NK_HTTP_ERROR_TLS;
        case NSURLErrorHTTPTooManyRedirects:
            return NK_HTTP_ERROR_REDIRECT;
        case NSURLErrorBadURL:
        case NSURLErrorCannotParseResponse:
        case NSURLErrorDataLengthExceedsMaximum:
            return NK_HTTP_ERROR_PROTOCOL;
        default:
            return NK_HTTP_ERROR_CONNECTION;
        }
    }
    return NK_HTTP_ERROR_CONNECTION;
}

struct AppleClientState {
    __strong NSHTTPCookieStorage *cookie_storage = nil;
};

@class NKURLSessionDelegate;

struct AppleTaskState {
    __strong NSURLSession *session = nil;
    __strong NSURLSessionTask *task = nil;
    __strong NKURLSessionDelegate *delegate = nil;

    ~AppleTaskState() {
        [session invalidateAndCancel];
    }
};

std::unordered_map<nk_request_id, std::shared_ptr<AppleTaskState>> active_tasks;

std::shared_ptr<AppleClientState> client_state(const nk::net::RequestPtr &request) {
    if (request->client->config.cookie_policy != NK_HTTP_COOKIES_SESSION)
        return {};
    std::lock_guard lock(request->client->backend_mutex);
    if (request->client->backend_state)
        return std::static_pointer_cast<AppleClientState>(request->client->backend_state);
    auto state = std::make_shared<AppleClientState>();
    state->cookie_storage = [[NSHTTPCookieStorage alloc] init];
    request->client->backend_state = state;
    return state;
}

@interface NKURLSessionDelegate : NSObject <NSURLSessionDataDelegate, NSURLSessionTaskDelegate> {
@public
    nk::net::RequestPtr request;
    std::vector<nk::net::OwnedHeader> pending_headers;
    uint32_t pending_status;
    uint64_t pending_length;
    uint32_t redirects;
    uint64_t downloaded;
    nk_result forced_result;
    bool response_started;
}
- (instancetype)initWithRequest:(const nk::net::RequestPtr &)value;
@end

@interface NKUploadInputStream : NSInputStream {
@private
    nk::net::RequestPtr request;
    __strong NSError *stream_error;
    BOOL opened;
}
- (instancetype)initWithRequest:(const nk::net::RequestPtr &)value;
@end

@implementation NKUploadInputStream

- (instancetype)initWithRequest:(const nk::net::RequestPtr &)value {
    self = [super init];
    if (self) {
        request = value;
        opened = NO;
    }
    return self;
}

- (void)open {
    opened = YES;
}

- (void)close {
    opened = NO;
}

- (NSInteger)read:(uint8_t *)buffer maxLength:(NSUInteger)length {
    if (!opened)
        [self open];
    if (!request || request->canceled.load(std::memory_order_acquire))
        return 0;
    uint64_t amount = 0;
    const auto result = nk_resource_read(request->request.upload_stream, buffer, length, &amount);
    if (result != NK_OK) {
        request->upload_result = result;
        stream_error = [NSError errorWithDomain:@"NativeKit.HTTP"
                                             code:static_cast<NSInteger>(result)
                                         userInfo:nil];
        return -1;
    }
    request->upload_position += amount;
    return static_cast<NSInteger>(amount);
}

- (BOOL)getBuffer:(uint8_t **)buffer length:(NSUInteger *)length {
    (void)buffer;
    (void)length;
    return NO;
}

- (BOOL)hasBytesAvailable {
    return opened && request && !request->canceled.load(std::memory_order_acquire);
}

- (NSStreamStatus)streamStatus {
    if (stream_error)
        return NSStreamStatusError;
    return opened ? NSStreamStatusOpen : NSStreamStatusNotOpen;
}

- (NSError *)streamError {
    return stream_error;
}

@end

@implementation NKURLSessionDelegate

- (instancetype)initWithRequest:(const nk::net::RequestPtr &)value {
    self = [super init];
    if (self) {
        request = value;
        pending_status = 0;
        pending_length = NK_HTTP_CONTENT_LENGTH_UNKNOWN;
        redirects = 0;
        forced_result = NK_OK;
        response_started = false;
    }
    return self;
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task
    didReceiveResponse:(NSURLResponse *)response
     completionHandler:(void (^)(NSURLSessionResponseDisposition disposition))completionHandler {
    auto *http = [response isKindOfClass:[NSHTTPURLResponse class]] ? (NSHTTPURLResponse *)response : nil;
    if (!http) {
        forced_result = NK_HTTP_ERROR_PROTOCOL;
        completionHandler(NSURLSessionResponseCancel);
        return;
    }
    const auto status = static_cast<uint32_t>([http statusCode]);
    auto *headers = [http allHeaderFields];
    auto values = response_headers(headers);
    std::string location;
    for (const auto &header : values)
        if (lower_equal(header.name, "location"))
            location = header.value;
    const auto length = content_length(headers, [response expectedContentLength]);
    if (status >= 300 && status <= 399 && !location.empty()) {
        pending_headers = std::move(values);
        pending_status = status;
        pending_length = length;
        completionHandler(NSURLSessionResponseAllow);
        return;
    }
    if (redirects != 0) {
        std::lock_guard lock(request->mutex);
        request->response_flags |= NK_HTTP_RESPONSE_REDIRECTED;
    }
    const auto result = nk::net::receive_response_headers(request, status, std::move(values), length);
    if (result != NK_OK) {
        forced_result = result;
        completionHandler(NSURLSessionResponseCancel);
        return;
    }
    response_started = true;
    downloaded = 0;
    completionHandler(NSURLSessionResponseAllow);
    (void)session;
    (void)task;
}

- (void)URLSession:(NSURLSession *)session dataTask:(NSURLSessionDataTask *)task didReceiveData:(NSData *)data {
    const auto result = nk::net::receive_response_data(
        request, static_cast<const std::byte *>([data bytes]), [data length]);
    if (result != NK_OK) {
        forced_result = result;
        [task cancel];
        return;
    }
    downloaded += [data length];
    nk::net::emit_progress(request, downloaded, request->total, 0, 0);
    (void)session;
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task
    didSendBodyData:(int64_t)bytesSent totalBytesSent:(int64_t)totalBytesSent
    totalBytesExpectedToSend:(int64_t)totalBytesExpectedToSend {
    nk::net::emit_progress(request, 0, request->total, static_cast<uint64_t>(totalBytesSent),
                           totalBytesExpectedToSend < 0 ? NK_HTTP_CONTENT_LENGTH_UNKNOWN
                                                         : static_cast<uint64_t>(totalBytesExpectedToSend));
    (void)session;
    (void)task;
    (void)bytesSent;
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task
    needNewBodyStream:(void (^)(NSInputStream *))completionHandler {
    if (request->request.upload_stream == NK_INVALID_HANDLE) {
        completionHandler(nil);
        return;
    }
    uint64_t position = 0;
    if (nk_resource_seek(request->request.upload_stream, 0, NK_SEEK_START, &position) != NK_OK) {
        forced_result = NK_HTTP_ERROR_PROTOCOL;
        completionHandler(nil);
        return;
    }
    request->upload_position = position;
    completionHandler([[NKUploadInputStream alloc] initWithRequest:request]);
    (void)session;
    (void)task;
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task
    didReceiveChallenge:(NSURLAuthenticationChallenge *)challenge
      completionHandler:(void (^)(NSURLSessionAuthChallengeDisposition, NSURLCredential *))completionHandler {
    auto *space = [challenge protectionSpace];
    if ([space.authenticationMethod isEqualToString:NSURLAuthenticationMethodHTTPProxy] &&
        !request->client->config.proxy.username.empty()) {
        NSString *username = [NSString stringWithUTF8String:request->client->config.proxy.username.c_str()];
        NSString *password = [NSString stringWithUTF8String:request->client->config.proxy.password.c_str()];
        auto *credential = [NSURLCredential credentialWithUser:username
                                                       password:password
                                                    persistence:NSURLCredentialPersistenceNone];
        completionHandler(NSURLSessionAuthChallengeUseCredential, credential);
        return;
    }
    completionHandler(NSURLSessionAuthChallengePerformDefaultHandling, nil);
    (void)session;
    (void)task;
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task
    willPerformHTTPRedirection:(NSHTTPURLResponse *)response
                   newRequest:(NSURLRequest *)newRequest
            completionHandler:(void (^)(NSURLRequest *))completionHandler {
    auto *next_url = [newRequest URL];
    const bool insecure = https_url([[response URL] absoluteString]) && http_url(next_url) &&
                          !(request->client->config.flags & NK_HTTP_CLIENT_ALLOW_HTTPS_TO_HTTP);
    if (!next_url || insecure || redirects >= request->request.redirect_limit) {
        if (pending_status != 0) {
            const auto result = nk::net::receive_response_headers(request, pending_status, std::move(pending_headers), pending_length);
            if (result != NK_OK)
                forced_result = result;
        }
        forced_result = forced_result == NK_OK ? NK_HTTP_ERROR_REDIRECT : forced_result;
        [task cancel];
        completionHandler(nil);
        return;
    }
    ++redirects;
    pending_headers.clear();
    pending_status = 0;
    pending_length = NK_HTTP_CONTENT_LENGTH_UNKNOWN;
    completionHandler(newRequest);
    (void)session;
}

- (void)URLSession:(NSURLSession *)session task:(NSURLSessionTask *)task
    didCompleteWithError:(NSError *)error {
    if (!response_started && pending_status != 0 && forced_result == NK_OK) {
        const auto result = nk::net::receive_response_headers(request, pending_status, std::move(pending_headers), pending_length);
        if (result != NK_OK)
            forced_result = result;
        else
            response_started = true;
    }
    auto result = forced_result != NK_OK ? forced_result : map_error(error, request);
    if (result == NK_OK && request->canceled.load(std::memory_order_acquire))
        result = NK_HTTP_ERROR_CANCELED;
    nk::net::complete_request(request, result);
    {
        std::lock_guard lock(active_tasks_mutex);
        active_tasks.erase(request->id);
    }
    nk::net::worker_finished(request);
    [session finishTasksAndInvalidate];
    (void)task;
}

@end

namespace nk::net {

nk_capabilities capabilities() noexcept {
    return NK_CAP_HTTP_CLIENT | NK_CAP_HTTP_STREAMING;
}

nk_result backend_start(const RequestPtr &request) noexcept {
    try {
        if (request->client->config.tls.flags != 0 ||
            request->client->config.tls.minimum_version != NK_HTTP_TLS_DEFAULT ||
            !request->client->config.tls.ca_bundle_path.empty())
            return NK_ERROR_UNSUPPORTED;
        const auto *method = method_name(request->request.method);
        if (!method)
            return NK_ERROR_INVALID_ARGUMENT;
        NSString *url_text = [NSString stringWithUTF8String:request->request.url.c_str()];
        NSURL *url = [NSURL URLWithString:url_text];
        if (!url)
            return NK_ERROR_INVALID_ARGUMENT;
        const auto timeout = static_cast<NSTimeInterval>(request->request.timeout_ms) / 1000.0;
        auto *url_request = [NSMutableURLRequest requestWithURL:url
                                                     cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                 timeoutInterval:timeout];
        [url_request setHTTPMethod:[NSString stringWithUTF8String:method]];
        for (const auto &header : request->client->config.default_headers)
            [url_request setValue:[NSString stringWithUTF8String:header.value.c_str()]
                forHTTPHeaderField:[NSString stringWithUTF8String:header.name.c_str()]];
        for (const auto &header : request->request.headers)
            [url_request setValue:[NSString stringWithUTF8String:header.value.c_str()]
                forHTTPHeaderField:[NSString stringWithUTF8String:header.name.c_str()]];
        if (!request->request.body.empty())
            [url_request setHTTPBody:[NSData dataWithBytes:request->request.body.data()
                                                      length:request->request.body.size()]];
        if (request->request.upload_stream != NK_INVALID_HANDLE)
            [url_request setHTTPBodyStream:[[NKUploadInputStream alloc] initWithRequest:request]];

        auto cookies = client_state(request);
        auto *configuration = [NSURLSessionConfiguration ephemeralSessionConfiguration];
        configuration.HTTPShouldSetCookies = cookies != nullptr;
        configuration.HTTPShouldHandleCookies = cookies != nullptr;
        configuration.HTTPCookieStorage = cookies ? cookies->cookie_storage : nil;
        configuration.URLCache = nil;
        configuration.requestCachePolicy = NSURLRequestReloadIgnoringLocalCacheData;
        configuration.URLCredentialStorage = nil;
        if (request->client->config.proxy.kind != NK_HTTP_PROXY_NONE) {
            NSURL *proxy_url = [NSURL URLWithString:[NSString stringWithUTF8String:
                                                           request->client->config.proxy.url.c_str()]];
            NSString *host = [proxy_url host];
            NSNumber *port = [proxy_url port];
            if (!proxy_url || !host || host.length == 0 || !port || port.integerValue <= 0 ||
                port.integerValue > 65535)
                return NK_HTTP_ERROR_PROXY;
            auto *proxy = [NSMutableDictionary dictionary];
            auto set_proxy = [&](CFStringRef enable, CFStringRef host_key, CFStringRef port_key) {
                proxy[(__bridge NSString *)enable] = @YES;
                proxy[(__bridge NSString *)host_key] = host;
                proxy[(__bridge NSString *)port_key] = port;
            };
            switch (request->client->config.proxy.kind) {
            case NK_HTTP_PROXY_HTTP:
                set_proxy(kCFNetworkProxiesHTTPEnable, kCFNetworkProxiesHTTPProxy,
                          kCFNetworkProxiesHTTPPort);
                break;
            case NK_HTTP_PROXY_HTTPS:
                set_proxy(kCFNetworkProxiesHTTPSEnable, kCFNetworkProxiesHTTPSProxy,
                          kCFNetworkProxiesHTTPSPort);
                break;
            case NK_HTTP_PROXY_SOCKS5:
                set_proxy(kCFNetworkProxiesSOCKSEnable, kCFNetworkProxiesSOCKSProxy,
                          kCFNetworkProxiesSOCKSPort);
                break;
            default:
                return NK_ERROR_INVALID_ARGUMENT;
            }
            configuration.connectionProxyDictionary = proxy;
        }
        auto *delegate_queue = [[NSOperationQueue alloc] init];
        delegate_queue.maxConcurrentOperationCount = 1;
        auto *delegate = [[NKURLSessionDelegate alloc] initWithRequest:request];
        auto *session = [[NSURLSession alloc] initWithConfiguration:configuration
                                                            delegate:delegate
                                                       delegateQueue:delegate_queue];
        auto *task = [session dataTaskWithRequest:url_request];
        if (!task) {
            [session invalidateAndCancel];
            return NK_ERROR_OUT_OF_MEMORY;
        }
        auto state = std::make_shared<AppleTaskState>();
        state->session = session;
        state->task = task;
        state->delegate = delegate;
        delegate->request = request;
        {
            std::lock_guard lock(active_tasks_mutex);
            active_tasks.emplace(request->id, state);
        }
        [task resume];
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return NK_ERROR_OUT_OF_MEMORY;
    } catch (...) {
        return NK_ERROR_UNKNOWN;
    }
}

void backend_cancel(const RequestPtr &request) noexcept {
    std::lock_guard lock(active_tasks_mutex);
    const auto found = active_tasks.find(request->id);
    if (found != active_tasks.end() && found->second->task)
        [found->second->task cancel];
}

void backend_shutdown() noexcept {
    std::lock_guard lock(active_tasks_mutex);
    for (const auto &[id, state] : active_tasks) {
        (void)id;
        if (state->task)
            [state->task cancel];
    }
}

} // namespace nk::net
