#import <Foundation/Foundation.h>

#include "transport/secure_websocket.hpp"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <vector>

/*
 * NSURLSessionWebSocketTask (macOS 10.15, iOS 13): the URL loading system
 * performs the TLS handshake against the system trust store (and App Transport
 * Security), the upgrade, pings and framing, and this backend exchanges
 * complete messages. Delegate callbacks and completion handlers run on the
 * session's serial queue; the blocking calls wait for them on a condition.
 */

namespace {

/** Connection state shared with the delegate and completion handlers. */
struct SocketState {
    std::mutex mutex;
    std::condition_variable condition;
    bool opened = false;
    bool ended = false;
    bool cancelled = false;
    nk_result failure = NK_OK;
    std::string selected_protocol;
};

nk_result map_error(NSError *error) noexcept {
    if (!error || ![error.domain isEqualToString:NSURLErrorDomain])
        return NK_TRANSPORT_ERROR_CONNECTION;
    switch (error.code) {
    case NSURLErrorCannotFindHost:
    case NSURLErrorDNSLookupFailed:
        return NK_TRANSPORT_ERROR_DNS;
    case NSURLErrorTimedOut:
        return NK_TRANSPORT_ERROR_TIMEOUT;
    case NSURLErrorCancelled:
        return NK_TRANSPORT_ERROR_CANCELED;
    case NSURLErrorBadServerResponse:
        return NK_TRANSPORT_ERROR_PROTOCOL;
    default:
        /* Refused connections and failed certificate checks alike. */
        return NK_TRANSPORT_ERROR_CONNECTION;
    }
}

} // namespace

API_AVAILABLE(macos(10.15), ios(13.0))
@interface NKSecureWebSocketDelegate : NSObject <NSURLSessionWebSocketDelegate> {
  @public
    std::shared_ptr<SocketState> state;
}
@end

@implementation NKSecureWebSocketDelegate

- (void)URLSession:(NSURLSession *)session
          webSocketTask:(NSURLSessionWebSocketTask *)webSocketTask
    didOpenWithProtocol:(NSString *)protocol {
    (void)session;
    (void)webSocketTask;
    std::lock_guard lock(state->mutex);
    state->opened = true;
    if (const char *value = protocol ? [protocol UTF8String] : nullptr)
        state->selected_protocol = value;
    state->condition.notify_all();
}

- (void)URLSession:(NSURLSession *)session
       webSocketTask:(NSURLSessionWebSocketTask *)webSocketTask
    didCloseWithCode:(NSURLSessionWebSocketCloseCode)closeCode
              reason:(NSData *)reason {
    (void)session;
    (void)webSocketTask;
    (void)closeCode;
    (void)reason;
    std::lock_guard lock(state->mutex);
    state->ended = true;
    state->condition.notify_all();
}

- (void)URLSession:(NSURLSession *)session
                    task:(NSURLSessionTask *)task
    didCompleteWithError:(NSError *)error {
    (void)session;
    (void)task;
    std::lock_guard lock(state->mutex);
    if (error && state->failure == NK_OK)
        state->failure = map_error(error);
    state->ended = true;
    state->condition.notify_all();
}

@end

namespace nk::transport {
namespace {

class API_AVAILABLE(macos(10.15), ios(13.0)) URLSessionWebSocket final : public SecureWebSocket {
  public:
    URLSessionWebSocket() : state_(std::make_shared<SocketState>()) {}

    ~URLSessionWebSocket() override { cancel(); }

    nk_result connect(const SecureEndpoint &endpoint) noexcept override {
        @autoreleasepool {
            std::string text = "wss://";
            const bool literal_v6 =
                endpoint.host.find(':') != std::string::npos && endpoint.host.front() != '[';
            text += literal_v6 ? "[" + endpoint.host + "]" : endpoint.host;
            text += ":" + std::to_string(endpoint.port) + endpoint.path;
            NSString *string = [NSString stringWithUTF8String:text.c_str()];
            NSURL *url = string ? [NSURL URLWithString:string] : nil;
            if (!url)
                return NK_TRANSPORT_ERROR_PROTOCOL;
            const NSTimeInterval timeout = endpoint.timeout_ms / 1000.0;
            NSMutableURLRequest *request =
                [NSMutableURLRequest requestWithURL:url
                                        cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                    timeoutInterval:timeout];
            if (!endpoint.subprotocols.empty())
                [request setValue:[NSString stringWithUTF8String:endpoint.subprotocols.c_str()]
                    forHTTPHeaderField:@"Sec-WebSocket-Protocol"];

            NSURLSessionConfiguration *configuration =
                [NSURLSessionConfiguration ephemeralSessionConfiguration];
            /* The request timeout restarts whenever data arrives; an idle WebSocket must stay
               open, so the handshake deadline is enforced below instead. */
            configuration.timeoutIntervalForRequest = 7 * 24 * 60 * 60;
            NKSecureWebSocketDelegate *delegate = [[NKSecureWebSocketDelegate alloc] init];
            delegate->state = state_;
            NSOperationQueue *queue = [[NSOperationQueue alloc] init];
            queue.maxConcurrentOperationCount = 1;
            NSURLSession *session = [NSURLSession sessionWithConfiguration:configuration
                                                                  delegate:delegate
                                                             delegateQueue:queue];
            NSURLSessionWebSocketTask *task = [session webSocketTaskWithRequest:request];
            task.maximumMessageSize = 64 * 1024 * 1024;
            {
                std::lock_guard lock(state_->mutex);
                if (state_->cancelled) {
                    [session invalidateAndCancel];
                    return NK_TRANSPORT_ERROR_CANCELED;
                }
                session_ = session;
                task_ = task;
            }
            [task resume];

            std::unique_lock lock(state_->mutex);
            const bool settled = state_->condition.wait_for(
                lock, std::chrono::milliseconds(endpoint.timeout_ms),
                [this] { return state_->opened || state_->ended || state_->cancelled; });
            if (state_->cancelled)
                return NK_TRANSPORT_ERROR_CANCELED;
            if (!settled) {
                lock.unlock();
                cancel();
                return NK_TRANSPORT_ERROR_TIMEOUT;
            }
            if (!state_->opened) {
                return state_->failure != NK_OK ? state_->failure : NK_TRANSPORT_ERROR_CONNECTION;
            }
            if (!state_->selected_protocol.empty() &&
                !subprotocol_offered(endpoint.subprotocols, state_->selected_protocol))
                return NK_TRANSPORT_ERROR_PROTOCOL;
            return NK_OK;
        }
    }

    nk_result receive(std::vector<uint8_t> &message, std::size_t max_size) noexcept override {
        NSURLSessionWebSocketTask *task = active_task();
        if (!task)
            return NK_TRANSPORT_ERROR_CLOSED;
        struct Received {
            bool done = false;
            nk_result result = NK_OK;
            std::vector<uint8_t> bytes;
        };
        auto received = std::make_shared<Received>();
        auto state = state_;
        [task receiveMessageWithCompletionHandler:^(NSURLSessionWebSocketMessage *value,
                                                    NSError *error) {
          std::lock_guard lock(state->mutex);
          if (error || !value) {
              received->result = NK_TRANSPORT_ERROR_CLOSED;
          } else {
              NSData *data = value.type == NSURLSessionWebSocketMessageTypeString
                                 ? [value.string dataUsingEncoding:NSUTF8StringEncoding]
                                 : value.data;
              const auto size = static_cast<std::size_t>(data.length);
              if (size > max_size) {
                  received->result = NK_TRANSPORT_ERROR_RECEIVE_OVERFLOW;
              } else {
                  const auto *bytes = static_cast<const uint8_t *>(data.bytes);
                  received->bytes.assign(bytes, bytes + size);
              }
          }
          received->done = true;
          state->condition.notify_all();
        }];
        std::unique_lock lock(state_->mutex);
        state_->condition.wait(lock, [&] { return received->done || state_->cancelled; });
        if (!received->done)
            return NK_TRANSPORT_ERROR_CANCELED;
        if (received->result == NK_OK)
            message.swap(received->bytes);
        return received->result;
    }

    nk_result send(const uint8_t *data, std::size_t size, bool text) noexcept override {
        NSURLSessionWebSocketTask *task = active_task();
        if (!task)
            return NK_TRANSPORT_ERROR_CLOSED;
        NSURLSessionWebSocketMessage *message = nil;
        @autoreleasepool {
            if (text) {
                NSString *string = [[NSString alloc] initWithBytes:data
                                                            length:size
                                                          encoding:NSUTF8StringEncoding];
                if (!string)
                    return NK_ERROR_INVALID_ARGUMENT;
                message = [[NSURLSessionWebSocketMessage alloc] initWithString:string];
            } else {
                message = [[NSURLSessionWebSocketMessage alloc]
                    initWithData:[NSData dataWithBytes:data length:size]];
            }
        }
        auto done = std::make_shared<bool>(false);
        auto failed = std::make_shared<bool>(false);
        auto state = state_;
        [task sendMessage:message
            completionHandler:^(NSError *error) {
              std::lock_guard lock(state->mutex);
              *failed = error != nil;
              *done = true;
              state->condition.notify_all();
            }];
        std::unique_lock lock(state_->mutex);
        state_->condition.wait(lock, [&] { return *done || state_->cancelled; });
        if (!*done)
            return NK_TRANSPORT_ERROR_CANCELED;
        return *failed ? NK_TRANSPORT_ERROR_CONNECTION : NK_OK;
    }

    void cancel() noexcept override {
        NSURLSession *session = nil;
        {
            std::lock_guard lock(state_->mutex);
            state_->cancelled = true;
            session = session_;
            session_ = nil;
            task_ = nil;
            state_->condition.notify_all();
        }
        /* Cancels the task and releases the session's strong reference to its delegate. */
        [session invalidateAndCancel];
    }

  private:
    NSURLSessionWebSocketTask *active_task() noexcept {
        std::lock_guard lock(state_->mutex);
        return state_->cancelled ? nil : task_;
    }

    std::shared_ptr<SocketState> state_;
    NSURLSession *session_ = nil;
    NSURLSessionWebSocketTask *task_ = nil;
};

} // namespace

bool secure_client_available() noexcept {
    if (@available(macOS 10.15, iOS 13.0, *))
        return true;
    return false;
}

std::unique_ptr<SecureWebSocket> create_secure_websocket() noexcept {
    if (@available(macOS 10.15, iOS 13.0, *))
        return std::unique_ptr<SecureWebSocket>(new (std::nothrow) URLSessionWebSocket());
    return nullptr;
}

} // namespace nk::transport
