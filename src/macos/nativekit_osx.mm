#import <Cocoa/Cocoa.h>
#import <UserNotifications/UserNotifications.h>
#import <WebKit/WebKit.h>

#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_notification.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <cfloat>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

@interface NKWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) void *resource;
@end

@interface NKContentView : NSView <NSDraggingDestination>
@property(nonatomic, assign) void *resource;
@end

@interface NKWebViewDelegate : NSObject <WKNavigationDelegate, WKScriptMessageHandler>
@property(nonatomic, assign) void *resource;
@end

@interface NKNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

namespace {

struct MacWindowResource final : nk::core::Resource {
    __strong NSWindow *window = nil;
    __strong NKContentView *content = nil;
    __strong NKWindowDelegate *delegate = nil;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle owner = NK_INVALID_HANDLE;
    bool modal = false;
    bool sheet_active = false;
    bool child_attached = false;
    std::vector<nk_handle> children;
    std::vector<nk_handle> owned_windows;
    ~MacWindowResource() override {
        if (window) {
            content.resource = nullptr;
            window.delegate = nil;
            [window orderOut:nil];
            [window close];
        }
    }
};

struct MacWebViewResource final : nk::core::Resource {
    __strong WKWebView *view = nil;
    __strong WKUserContentController *content_controller = nil;
    __strong NKWebViewDelegate *delegate = nil;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle parent = NK_INVALID_HANDLE;
    bool observing_title = false;
    bool navigation_policy = false;
    uint64_t generation = 0;
    ~MacWebViewResource() override {
        if (view) {
            if (observing_title)
                [view removeObserver:delegate forKeyPath:@"title"];
            view.navigationDelegate = nil;
            [content_controller removeScriptMessageHandlerForName:@"nativekit"];
            delegate.resource = nullptr;
            [view stopLoading];
            [view removeFromSuperview];
        }
    }
};

struct MacNavigationDecision {
    nk_handle source = NK_INVALID_HANDLE;
    __strong void (^handler)(WKNavigationActionPolicy) = nil;
};

struct DialogContext {
    nk_request_id request = NK_INVALID_REQUEST_ID;
    uint32_t kind = 0;
    __strong NSWindow *parent = nil;
    __strong id dialog = nil;
    std::vector<uint32_t> message_results;
    uint64_t generation = 0;
};

std::mutex dialogs_mutex;
std::unordered_map<nk_request_id, std::shared_ptr<DialogContext>> dialogs;
std::unordered_map<nk_request_id, MacNavigationDecision> navigation_decisions;
std::unordered_map<nk_request_id, nk_handle> evaluations;
std::mutex notifications_mutex;
std::unordered_map<nk_request_id, uint64_t> notifications;
__strong NKNotificationDelegate *notification_delegate = nil;

void cancel_dialog_context(const std::shared_ptr<DialogContext> &context) {
    if ([context->dialog isKindOfClass:[NSSavePanel class]])
        [(NSSavePanel *)context->dialog cancel:nil];
    else if ([context->dialog isKindOfClass:[NSAlert class]]) {
        NSAlert *alert = (NSAlert *)context->dialog;
        if (context->parent)
            [context->parent endSheet:alert.window returnCode:NSModalResponseCancel];
        else
            [NSApp abortModal];
    }
}

void cancel_dialogs_for_parent(NSWindow *parent) {
    std::vector<std::shared_ptr<DialogContext>> matching;
    {
        std::lock_guard lock(dialogs_mutex);
        for (const auto &item : dialogs)
            if (item.second->parent == parent)
                matching.push_back(item.second);
    }
    for (const auto &context : matching)
        cancel_dialog_context(context);
}

nk_result fail(nk_result result, std::string_view message) {
    nk::core::set_error(message);
    return result;
}

nk_result enter_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

NSString *string(const char *value) {
    if (!value)
        return nil;
    return [[NSString alloc] initWithBytes:value
                                    length:std::strlen(value)
                                  encoding:NSUTF8StringEncoding];
}

bool valid_utf8(const char *value) {
    return !value || !*value || string(value) != nil;
}

std::string utf8(NSString *value) {
    if (!value)
        return {};
    const char *bytes = value.UTF8String;
    return bytes ? std::string(bytes) : std::string();
}

NSString *javascript_json_wrapper(NSString *source) {
    NSError *error = nil;
    NSData *encoded = [NSJSONSerialization dataWithJSONObject:@[ source ] options:0 error:&error];
    if (!encoded || error)
        return nil;
    NSString *array = [[NSString alloc] initWithData:encoded encoding:NSUTF8StringEncoding];
    NSString *literal = [array substringWithRange:NSMakeRange(1, array.length - 2)];
    return [NSString
        stringWithFormat:
            @"(()=>{const v=(0,eval)(%@);const j=JSON.stringify(v);"
             "if(j===undefined)throw new TypeError('JavaScript result is not JSON-serializable');"
             "return j;})()",
            literal];
}

NSString *json_text(id value) {
    NSError *error = nil;
    NSData *encoded = [NSJSONSerialization dataWithJSONObject:value
                                                      options:NSJSONWritingFragmentsAllowed
                                                        error:&error];
    if (!encoded || error)
        return nil;
    return [[NSString alloc] initWithData:encoded encoding:NSUTF8StringEncoding];
}

template <typename T> std::vector<std::byte> bytes_of(const T &value) {
    const auto *first = reinterpret_cast<const std::byte *>(&value);
    return {first, first + sizeof(value)};
}

std::vector<std::byte> text_bytes(const std::string &value) {
    const auto *first = reinterpret_cast<const std::byte *>(value.data());
    return {first, first + value.size()};
}

template <typename Header>
std::vector<std::byte> string_list_payload(Header header, const std::vector<std::string> &strings,
                                           uint32_t Header::*offset_member) {
    header.*offset_member = sizeof(Header);
    std::size_t total = sizeof(Header);
    for (const auto &value : strings)
        total += value.size() + 1;
    std::vector<std::byte> result(total);
    std::memcpy(result.data(), &header, sizeof(header));
    std::size_t cursor = sizeof(Header);
    for (const auto &value : strings) {
        std::memcpy(result.data() + cursor, value.c_str(), value.size() + 1);
        cursor += value.size() + 1;
    }
    return result;
}

NSArray<NSURL *> *pasteboard_file_urls(NSPasteboard *pasteboard) {
    NSDictionary *options = @{NSPasteboardURLReadingFileURLsOnlyKey : @YES};
    NSArray *values = [pasteboard readObjectsForClasses:@[ [NSURL class] ] options:options];
    NSMutableArray<NSURL *> *files = [NSMutableArray array];
    for (NSURL *value in values)
        if (value.fileURL)
            [files addObject:value];
    return files;
}

bool emit_drop(MacWindowResource &resource, id<NSDraggingInfo> information) noexcept {
    return nk::core::callback_boundary_or(false, [&]() -> bool {
        NSPasteboard *pasteboard = information.draggingPasteboard;
        NSArray<NSURL *> *urls = pasteboard_file_urls(pasteboard);
        std::vector<std::string> items;
        nk_event_kind kind = NK_EVENT_DROP_FILES;
        if (urls.count) {
            for (NSURL *url in urls) {
                const bool scoped = [url startAccessingSecurityScopedResource];
                items.push_back(utf8(url.path));
                if (scoped)
                    [url stopAccessingSecurityScopedResource];
            }
        } else {
            NSString *text = [pasteboard stringForType:NSPasteboardTypeString];
            if (!text)
                return false;
            kind = NK_EVENT_DROP_TEXT;
            items.push_back(utf8(text));
        }
        const NSPoint point = [resource.content convertPoint:information.draggingLocation
                                                    fromView:nil];
        nk::core::QueuedEvent event;
        event.kind = kind;
        event.source = resource.handle;
        event.data_count = static_cast<uint32_t>(items.size());
        nk_drop_data header{static_cast<int32_t>(point.x), static_cast<int32_t>(point.y),
                            static_cast<uint32_t>(items.size()), 0};
        event.data = string_list_payload(header, items, &nk_drop_data::strings_offset);
        return nk::core::push_event(std::move(event)) == NK_OK;
    });
}

std::shared_ptr<MacWindowResource> window(nk_handle handle) {
    return std::dynamic_pointer_cast<MacWindowResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::window));
}

std::shared_ptr<MacWebViewResource> webview(nk_handle handle) {
    return std::dynamic_pointer_cast<MacWebViewResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::webview));
}

void emit_webview_text(nk_event_kind kind, nk_handle source, NSString *text,
                       nk_result result = NK_OK, uint32_t flags = 0,
                       nk_request_id request = NK_INVALID_REQUEST_ID) noexcept {
    nk::core::callback_boundary([&] {
        auto resource = webview(source);
        if (!resource || !nk::core::is_runtime_generation(resource->generation))
            return;
        nk::core::QueuedEvent event;
        event.kind = kind;
        event.source = source;
        event.result = result;
        event.flags = flags;
        event.request_id = request;
        event.data = text_bytes(utf8(text));
        nk::core::push_event(std::move(event));
    });
}

void cancel_navigation_decisions(nk_handle source) {
    for (auto item = navigation_decisions.begin(); item != navigation_decisions.end();) {
        if (item->second.source == source) {
            item->second.handler(WKNavigationActionPolicyCancel);
            item = navigation_decisions.erase(item);
        } else {
            ++item;
        }
    }
}

void cancel_evaluations(nk_handle source) noexcept {
    for (auto item = evaluations.begin(); item != evaluations.end();) {
        if (source && item->second != source) {
            ++item;
            continue;
        }
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WEBVIEW_EVAL_COMPLETE;
        event.source = item->second;
        event.request_id = item->first;
        event.result = NK_ERROR_INVALID_REQUEST;
        nk::core::push_event(std::move(event));
        item = evaluations.erase(item);
    }
}

uint32_t navigation_error_category(NSError *error) {
    if (![error.domain isEqualToString:NSURLErrorDomain])
        return NK_NAVIGATION_ERROR_OTHER;
    switch (error.code) {
    case NSURLErrorCancelled:
        return NK_NAVIGATION_ERROR_CANCELLED;
    case NSURLErrorBadURL:
    case NSURLErrorUnsupportedURL:
        return NK_NAVIGATION_ERROR_REQUEST;
    case NSURLErrorUserAuthenticationRequired:
    case NSURLErrorUserCancelledAuthentication:
        return NK_NAVIGATION_ERROR_AUTH;
    case NSURLErrorServerCertificateHasBadDate:
    case NSURLErrorServerCertificateUntrusted:
    case NSURLErrorServerCertificateHasUnknownRoot:
    case NSURLErrorServerCertificateNotYetValid:
    case NSURLErrorSecureConnectionFailed:
        return NK_NAVIGATION_ERROR_SECURITY;
    case NSURLErrorFileDoesNotExist:
    case NSURLErrorResourceUnavailable:
        return NK_NAVIGATION_ERROR_NOT_FOUND;
    case NSURLErrorTimedOut:
    case NSURLErrorCannotFindHost:
    case NSURLErrorCannotConnectToHost:
    case NSURLErrorNetworkConnectionLost:
    case NSURLErrorDNSLookupFailed:
    case NSURLErrorNotConnectedToInternet:
        return NK_NAVIGATION_ERROR_CONNECTION;
    default:
        return NK_NAVIGATION_ERROR_OTHER;
    }
}

std::vector<std::byte> dialog_paths_payload(const std::vector<std::string> &paths, bool accepted) {
    const std::size_t offsets_offset = sizeof(nk_dialog_paths);
    const std::size_t strings_offset = offsets_offset + paths.size() * sizeof(uint32_t);
    std::size_t total = strings_offset;
    for (const auto &path : paths)
        total += path.size() + 1;
    std::vector<std::byte> result(total);
    const nk_dialog_paths header{accepted ? 1u : 0u, static_cast<uint32_t>(paths.size()),
                                 static_cast<uint32_t>(offsets_offset),
                                 static_cast<uint32_t>(strings_offset)};
    std::memcpy(result.data(), &header, sizeof(header));
    std::size_t cursor = strings_offset;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        const auto offset = static_cast<uint32_t>(cursor);
        std::memcpy(result.data() + offsets_offset + index * sizeof(offset), &offset,
                    sizeof(offset));
        std::memcpy(result.data() + cursor, paths[index].c_str(), paths[index].size() + 1);
        cursor += paths[index].size() + 1;
    }
    return result;
}

void finish_file_dialog(nk_request_id request, NSInteger response,
                        NSArray<NSURL *> *urls) noexcept {
    nk::core::callback_boundary([&] {
        std::shared_ptr<DialogContext> context;
        {
            std::lock_guard lock(dialogs_mutex);
            const auto found = dialogs.find(request);
            if (found == dialogs.end())
                return;
            context = found->second;
            dialogs.erase(found);
        }
        if (!nk::core::is_runtime_generation(context->generation))
            return;
        const bool accepted = response == NSModalResponseOK;
        std::vector<std::string> paths;
        if (accepted) {
            for (NSURL *url in urls)
                paths.push_back(utf8(url.path));
        }
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_DIALOG_PATHS_COMPLETE;
        event.request_id = request;
        event.flags = context->kind;
        event.data_count = static_cast<uint32_t>(paths.size());
        event.data = dialog_paths_payload(paths, accepted);
        nk::core::push_event(std::move(event));
    });
}

void finish_message_dialog(nk_request_id request, NSInteger response) noexcept {
    nk::core::callback_boundary([&] {
        std::shared_ptr<DialogContext> context;
        {
            std::lock_guard lock(dialogs_mutex);
            const auto found = dialogs.find(request);
            if (found == dialogs.end())
                return;
            context = found->second;
            dialogs.erase(found);
        }
        if (!nk::core::is_runtime_generation(context->generation))
            return;
        uint32_t result = NK_MESSAGE_RESULT_CANCEL;
        const NSInteger index = response - NSAlertFirstButtonReturn;
        if (index >= 0 && static_cast<std::size_t>(index) < context->message_results.size())
            result = context->message_results[static_cast<std::size_t>(index)];
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_DIALOG_MESSAGE_COMPLETE;
        event.request_id = request;
        event.flags = NK_DIALOG_MESSAGE;
        event.data = bytes_of(nk_dialog_message_result{result});
        nk::core::push_event(std::move(event));
    });
}

void configure_file_panel(NSSavePanel *panel, const nk_file_dialog_options *options) {
    panel.title = string(options->title) ?: @"";
    panel.showsHiddenFiles = (options->flags & NK_DIALOG_SHOW_HIDDEN) != 0;
    if (options->suggested_name)
        panel.nameFieldStringValue = string(options->suggested_name) ?: @"";
    if (options->initial_path) {
        NSString *path = string(options->initial_path);
        if (path)
            panel.directoryURL = [NSURL fileURLWithPath:path isDirectory:YES];
    }
    NSMutableArray<NSString *> *extensions = [NSMutableArray array];
    for (uint32_t index = 0; index < options->filter_count; ++index) {
        NSString *patterns = string(options->filters[index].patterns);
        for (NSString *pattern in [patterns componentsSeparatedByString:@";"]) {
            if ([pattern hasPrefix:@"*."] && pattern.length > 2)
                [extensions addObject:[pattern substringFromIndex:2]];
        }
    }
    if (extensions.count)
        panel.allowedFileTypes = extensions;
}

nk_result start_file_dialog(nk_handle parent_handle, const nk_file_dialog_options *options,
                            nk_request_id *out_request, uint32_t kind) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!options || options->struct_size < sizeof(*options) || !out_request ||
        (options->filter_count && !options->filters))
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid file dialog options");
    if (!valid_utf8(options->title) || !valid_utf8(options->initial_path) ||
        !valid_utf8(options->suggested_name))
        return fail(NK_ERROR_INVALID_ARGUMENT, "file dialog option is not valid UTF-8");
    for (uint32_t index = 0; index < options->filter_count; ++index) {
        if (!valid_utf8(options->filters[index].name) ||
            !valid_utf8(options->filters[index].patterns))
            return fail(NK_ERROR_INVALID_ARGUMENT, "file dialog filter is not valid UTF-8");
    }
    std::shared_ptr<MacWindowResource> parent;
    if (parent_handle) {
        parent = window(parent_handle);
        if (!parent)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
    }
    auto context = std::make_shared<DialogContext>();
    context->request = nk::core::next_request_id();
    context->generation = nk::core::runtime_generation();
    context->kind = kind;
    context->parent = parent ? parent->window : nil;
    NSSavePanel *panel = nil;
    if (kind == NK_DIALOG_SAVE_FILE) {
        panel = [NSSavePanel savePanel];
    } else {
        NSOpenPanel *open = [NSOpenPanel openPanel];
        open.canChooseDirectories = kind == NK_DIALOG_SELECT_DIRECTORY;
        open.canChooseFiles = kind != NK_DIALOG_SELECT_DIRECTORY;
        open.allowsMultipleSelection =
            kind == NK_DIALOG_OPEN_FILE && (options->flags & NK_DIALOG_ALLOW_MULTIPLE);
        panel = open;
    }
    configure_file_panel(panel, options);
    context->dialog = panel;
    {
        std::lock_guard lock(dialogs_mutex);
        dialogs.emplace(context->request, context);
    }
    const nk_request_id request = context->request;
    void (^completion)(NSModalResponse) = ^(NSModalResponse response) {
      NSArray<NSURL *> *urls = @[];
      if ([panel isKindOfClass:[NSOpenPanel class]])
          urls = [(NSOpenPanel *)panel URLs];
      else if (panel.URL)
          urls = @[ panel.URL ];
      finish_file_dialog(request, response, urls);
    };
    if (parent)
        [panel beginSheetModalForWindow:parent->window completionHandler:completion];
    else
        [panel beginWithCompletionHandler:completion];
    *out_request = request;
    return NK_OK;
}

nk_result copy_output(NSString *value, char *buffer, uint32_t *inout_size) {
    if (!value || !inout_size)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid string output arguments");
    const auto result = utf8(value);
    if (result.size() >= std::numeric_limits<uint32_t>::max())
        return fail(NK_ERROR_UNKNOWN, "system string is too large");
    const auto required = static_cast<uint32_t>(result.size() + 1);
    const auto capacity = *inout_size;
    *inout_size = required;
    if (!buffer || capacity < required)
        return fail(NK_ERROR_BUFFER_TOO_SMALL, "output buffer is too small");
    std::memcpy(buffer, result.c_str(), required);
    return NK_OK;
}

NSURL *directory_url(nk_system_directory_kind kind) {
    NSFileManager *manager = NSFileManager.defaultManager;
    NSSearchPathDirectory directory;
    switch (kind) {
    case NK_DIRECTORY_DESKTOP:
        directory = NSDesktopDirectory;
        break;
    case NK_DIRECTORY_DOCUMENTS:
        directory = NSDocumentDirectory;
        break;
    case NK_DIRECTORY_DOWNLOADS:
        directory = NSDownloadsDirectory;
        break;
    case NK_DIRECTORY_CACHE:
        directory = NSCachesDirectory;
        break;
    case NK_DIRECTORY_CONFIG:
        directory = NSLibraryDirectory;
        break;
    case NK_DIRECTORY_DATA:
        directory = NSApplicationSupportDirectory;
        break;
    default:
        return nil;
    }
    return [manager URLForDirectory:directory
                           inDomain:NSUserDomainMask
                  appropriateForURL:nil
                             create:NO
                              error:nil];
}

nk_result unsupported() {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    return fail(NK_ERROR_UNSUPPORTED, "this macOS service is not implemented yet");
}

NSString *notification_identifier(nk_request_id request) {
    return [NSString stringWithFormat:@"%llu", static_cast<unsigned long long>(request)];
}

nk_request_id notification_request(NSString *identifier) {
    if (!identifier.length)
        return NK_INVALID_REQUEST_ID;
    return static_cast<nk_request_id>(std::strtoull(identifier.UTF8String, nullptr, 10));
}

void emit_notification(nk_event_kind kind, nk_request_id request, nk_result result = NK_OK,
                       NSString *text = nil) noexcept {
    nk::core::callback_boundary([&] {
        nk::core::QueuedEvent event;
        event.kind = kind;
        event.request_id = request;
        event.result = result;
        if (text)
            event.data = text_bytes(utf8(text));
        nk::core::push_event(std::move(event));
    });
}

bool take_notification(nk_request_id request, uint64_t *generation = nullptr) {
    std::lock_guard lock(notifications_mutex);
    const auto found = notifications.find(request);
    if (found == notifications.end())
        return false;
    if (generation)
        *generation = found->second;
    notifications.erase(found);
    return true;
}

bool has_notification(nk_request_id request, uint64_t generation) {
    std::lock_guard lock(notifications_mutex);
    const auto found = notifications.find(request);
    return found != notifications.end() && found->second == generation;
}

void emit_window_state(MacWindowResource &resource) noexcept {
    nk::core::callback_boundary([&] {
        nk_window_state state{sizeof(state), 0, {0, 0}};
        if (resource.window.visible)
            state.flags |= NK_WINDOW_STATE_VISIBLE;
        if (resource.window.keyWindow)
            state.flags |= NK_WINDOW_STATE_ACTIVE;
        if (resource.window.miniaturized)
            state.flags |= NK_WINDOW_STATE_MINIMIZED;
        if (resource.window.zoomed)
            state.flags |= NK_WINDOW_STATE_MAXIMIZED;
        if (resource.window.styleMask & NSWindowStyleMaskFullScreen)
            state.flags |= NK_WINDOW_STATE_FULLSCREEN;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WINDOW_STATE_CHANGED;
        event.source = resource.handle;
        event.data = bytes_of(state);
        nk::core::push_event(std::move(event));
    });
}

} // namespace

@implementation NKNotificationDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
    didReceiveNotificationResponse:(UNNotificationResponse *)response
             withCompletionHandler:(void (^)(void))completionHandler {
    (void)center;
    const auto request = notification_request(response.notification.request.identifier);
    uint64_t generation = 0;
    if (take_notification(request, &generation) && nk::core::is_runtime_generation(generation)) {
        const bool dismissed =
            [response.actionIdentifier isEqualToString:UNNotificationDismissActionIdentifier];
        emit_notification(
            dismissed ? NK_EVENT_NOTIFICATION_DISMISSED : NK_EVENT_NOTIFICATION_ACTIVATED, request);
    }
    completionHandler();
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
    (void)center;
    (void)notification;
    completionHandler(UNNotificationPresentationOptionAlert |
                      UNNotificationPresentationOptionSound);
}
@end

@implementation NKWindowDelegate
- (BOOL)windowShouldClose:(NSWindow *)sender {
    (void)sender;
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (resource && resource->handle) {
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WINDOW_CLOSE;
        event.source = resource->handle;
        nk::core::push_event(std::move(event));
    }
    return NO;
}
- (void)windowDidResize:(NSNotification *)notification {
    (void)notification;
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource || !resource->handle)
        return;
    const NSSize size = resource->window.contentView.bounds.size;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_WINDOW_RESIZE;
    event.source = resource->handle;
    event.data = bytes_of(nk_window_resize_event{static_cast<int32_t>(size.width),
                                                 static_cast<int32_t>(size.height)});
    nk::core::push_event(std::move(event));
    emit_window_state(*resource);
}
- (void)windowDidMiniaturize:(NSNotification *)notification {
    (void)notification;
    auto *r = static_cast<MacWindowResource *>(_resource);
    if (r)
        emit_window_state(*r);
}
- (void)windowDidDeminiaturize:(NSNotification *)notification {
    (void)notification;
    auto *r = static_cast<MacWindowResource *>(_resource);
    if (r)
        emit_window_state(*r);
}
- (void)windowDidBecomeKey:(NSNotification *)notification {
    (void)notification;
    auto *r = static_cast<MacWindowResource *>(_resource);
    if (r)
        emit_window_state(*r);
}
- (void)windowDidResignKey:(NSNotification *)notification {
    (void)notification;
    auto *r = static_cast<MacWindowResource *>(_resource);
    if (r)
        emit_window_state(*r);
}
- (void)windowDidChangeBackingProperties:(NSNotification *)notification {
    (void)notification;
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource || !resource->handle)
        return;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_WINDOW_SCALE_CHANGED;
    event.source = resource->handle;
    event.data =
        bytes_of(nk_window_scale_event{static_cast<float>(resource->window.backingScaleFactor)});
    nk::core::push_event(std::move(event));
}
@end

@implementation NKContentView
- (BOOL)isFlipped {
    return YES;
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    NSPasteboard *pasteboard = sender.draggingPasteboard;
    return pasteboard_file_urls(pasteboard).count ||
                   [pasteboard availableTypeFromArray:@[ NSPasteboardTypeString ]]
               ? NSDragOperationCopy
               : NSDragOperationNone;
}
- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender {
    (void)sender;
    return _resource != nullptr;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    return resource && emit_drop(*resource, sender);
}
@end

@implementation NKWebViewDelegate
- (void)webView:(WKWebView *)view
    decidePolicyForNavigationAction:(WKNavigationAction *)action
                    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    (void)view;
    auto *resource = static_cast<MacWebViewResource *>(_resource);
    if (!resource || !resource->navigation_policy ||
        (action.targetFrame && !action.targetFrame.mainFrame)) {
        decisionHandler(WKNavigationActionPolicyAllow);
        return;
    }
    bool completed = false;
    nk_request_id request = NK_INVALID_REQUEST_ID;
    bool inserted = false;
    nk::core::callback_boundary([&] {
        request = nk::core::next_request_id();
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WEBVIEW_NAVIGATION_REQUEST;
        event.source = resource->handle;
        event.request_id = request;
        event.data = text_bytes(utf8(action.request.URL.absoluteString));
        navigation_decisions.emplace(
            request, MacNavigationDecision{resource->handle, [decisionHandler copy]});
        inserted = true;
        if (nk::core::push_event(std::move(event)) != NK_OK) {
            navigation_decisions.erase(request);
            inserted = false;
            decisionHandler(WKNavigationActionPolicyAllow);
            completed = true;
            return;
        }
        completed = true;
    });
    if (!completed) {
        if (inserted)
            navigation_decisions.erase(request);
        decisionHandler(WKNavigationActionPolicyAllow);
    }
}
- (void)webView:(WKWebView *)view didFinishNavigation:(WKNavigation *)navigation {
    (void)navigation;
    auto *resource = static_cast<MacWebViewResource *>(_resource);
    if (resource)
        emit_webview_text(NK_EVENT_WEBVIEW_NAVIGATED, resource->handle, view.URL.absoluteString);
}
- (void)webView:(WKWebView *)view
    didFailNavigation:(WKNavigation *)navigation
            withError:(NSError *)error {
    (void)view;
    (void)navigation;
    auto *resource = static_cast<MacWebViewResource *>(_resource);
    if (resource)
        emit_webview_text(NK_EVENT_WEBVIEW_NAVIGATION_FAILED, resource->handle,
                          error.localizedDescription, NK_ERROR_UNKNOWN,
                          navigation_error_category(error));
}
- (void)webView:(WKWebView *)view
    didFailProvisionalNavigation:(WKNavigation *)navigation
                       withError:(NSError *)error {
    [self webView:view didFailNavigation:navigation withError:error];
}
- (void)webViewWebContentProcessDidTerminate:(WKWebView *)view {
    auto *resource = static_cast<MacWebViewResource *>(_resource);
    if (resource)
        emit_webview_text(NK_EVENT_WEBVIEW_PROCESS_TERMINATED, resource->handle,
                          @"WebKit content process terminated", NK_ERROR_UNKNOWN);
    (void)view;
}
- (void)userContentController:(WKUserContentController *)controller
      didReceiveScriptMessage:(WKScriptMessage *)message {
    (void)controller;
    auto *resource = static_cast<MacWebViewResource *>(_resource);
    if (!resource)
        return;
    NSString *value = json_text(message.body);
    emit_webview_text(NK_EVENT_WEBVIEW_MESSAGE, resource->handle,
                      value ?: @"JavaScript message is not JSON-serializable",
                      value ? NK_OK : NK_ERROR_UNKNOWN);
}
- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id> *)change
                       context:(void *)context {
    (void)change;
    (void)context;
    auto *resource = static_cast<MacWebViewResource *>(_resource);
    if (resource && [keyPath isEqualToString:@"title"])
        emit_webview_text(NK_EVENT_WEBVIEW_TITLE_CHANGED, resource->handle,
                          ((WKWebView *)object).title);
}
@end

namespace nk::backend {
void pump_events() noexcept {
    @autoreleasepool {
        NSEvent *event = nil;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:NSDate.distantPast
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES]))
            [NSApp sendEvent:event];
        [NSApp updateWindows];
    }
}

void shutdown() noexcept {
    NSMutableArray<NSString *> *notification_identifiers = [NSMutableArray array];
    {
        std::lock_guard lock(notifications_mutex);
        for (const auto &[request, generation] : notifications) {
            (void)generation;
            [notification_identifiers addObject:notification_identifier(request)];
        }
        notifications.clear();
    }
    UNUserNotificationCenter *notification_center =
        UNUserNotificationCenter.currentNotificationCenter;
    [notification_center removePendingNotificationRequestsWithIdentifiers:notification_identifiers];
    [notification_center removeDeliveredNotificationsWithIdentifiers:notification_identifiers];
    if (notification_center.delegate == notification_delegate)
        notification_center.delegate = nil;
    notification_delegate = nil;
    std::vector<std::shared_ptr<DialogContext>> pending;
    {
        std::lock_guard lock(dialogs_mutex);
        for (const auto &item : dialogs)
            pending.push_back(item.second);
    }
    for (const auto &context : pending)
        cancel_dialog_context(context);
    for (const auto &item : navigation_decisions)
        item.second.handler(WKNavigationActionPolicyCancel);
    navigation_decisions.clear();
    cancel_evaluations(NK_INVALID_HANDLE);
    pump_events();
    dialogs.clear();
    nk::core::handles().clear();
}
} // namespace nk::backend

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_WINDOW | NK_CAP_FILE_DIALOG | NK_CAP_CLIPBOARD | NK_CAP_WEBVIEW |
           NK_CAP_DRAG_DROP | NK_CAP_SHELL | NK_CAP_SYSTEM_APPEARANCE |
           NK_CAP_EXPORT_NATIVE_WINDOW | NK_CAP_NOTIFICATION | NK_CAP_RESOURCE_IO;
}

nk_result NK_CALL nk_window_create(const nk_window_options *options, nk_handle *out_window) {
    return nk::core::result_boundary("unexpected error while creating window", [&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!options || options->struct_size < sizeof(*options) || !out_window ||
            options->width <= 0 || options->height <= 0 || options->kind > NK_WINDOW_UTILITY ||
            ((options->flags & NK_WINDOW_MODAL) && !options->owner))
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid window options");
        if (!valid_utf8(options->title))
            return fail(NK_ERROR_INVALID_ARGUMENT, "window title is not valid UTF-8");
        *out_window = NK_INVALID_HANDLE;
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        auto owner = options->owner ? window(options->owner) : nullptr;
        if (options->owner && !owner)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale owner window handle");
        if (owner)
            owner->owned_windows.reserve(owner->owned_windows.size() + 1);
        auto resource = std::make_shared<MacWindowResource>();
        resource->owner = options->owner;
        resource->modal = (options->flags & NK_WINDOW_MODAL) != 0;
        NSWindowStyleMask style = (options->flags & NK_WINDOW_BORDERLESS)
                                      ? NSWindowStyleMaskBorderless
                                      : NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                            NSWindowStyleMaskMiniaturizable;
        if (options->kind == NK_WINDOW_UTILITY)
            style |= NSWindowStyleMaskUtilityWindow;
        if (options->flags & NK_WINDOW_RESIZABLE)
            style |= NSWindowStyleMaskResizable;
        resource->window =
            [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, options->width, options->height)
                                        styleMask:style
                                          backing:NSBackingStoreBuffered
                                            defer:NO];
        if (!resource->window)
            return fail(NK_ERROR_UNKNOWN, "could not create Cocoa window");
        resource->window.title = string(options->title) ?: @"";
        resource->window.releasedWhenClosed = NO;
        resource->content =
            [[NKContentView alloc] initWithFrame:NSMakeRect(0, 0, options->width, options->height)];
        resource->content.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        resource->content.resource = resource.get();
        resource->window.contentView = resource->content;
        resource->delegate = [NKWindowDelegate new];
        resource->delegate.resource = resource.get();
        resource->window.delegate = resource->delegate;
        resource->handle = nk::core::handles().insert(nk::core::ResourceType::window, resource);
        if (!resource->handle)
            return fail(NK_ERROR_OUT_OF_MEMORY, "window handle registry is full");
        if (owner)
            owner->owned_windows.push_back(resource->handle);
        [resource->window center];
        if (!(options->flags & NK_WINDOW_HIDDEN)) {
            if (resource->modal && owner) {
                [owner->window beginSheet:resource->window completionHandler:nil];
                resource->sheet_active = true;
            } else {
                if (owner) {
                    [owner->window addChildWindow:resource->window ordered:NSWindowAbove];
                    resource->child_attached = true;
                }
                [resource->window makeKeyAndOrderFront:nil];
            }
        }
        *out_window = resource->handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_window_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const auto owned_windows = resource->owned_windows;
    for (const auto owned : owned_windows)
        nk_window_destroy(owned);
    const auto children = resource->children;
    for (const auto child : children)
        nk_webview_destroy(child);
    cancel_dialogs_for_parent(resource->window);
    if (auto owner = window(resource->owner)) {
        if (resource->sheet_active)
            [owner->window endSheet:resource->window];
        else if (resource->child_attached)
            [owner->window removeChildWindow:resource->window];
        auto &owned = owner->owned_windows;
        owned.erase(std::remove(owned.begin(), owned.end(), handle), owned.end());
    }
    resource->content.resource = nullptr;
    resource->delegate.resource = nullptr;
    resource->window.delegate = nil;
    [resource->window orderOut:nil];
    [resource->window close];
    resource->window = nil;
    nk::core::handles().erase(handle, nk::core::ResourceType::window);
    return NK_OK;
}

nk_result NK_CALL nk_window_show(nk_handle handle, uint32_t visible) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    auto owner = window(resource->owner);
    if (visible) {
        if (resource->modal && owner && !resource->sheet_active) {
            [owner->window beginSheet:resource->window completionHandler:nil];
            resource->sheet_active = true;
        } else {
            if (owner && !resource->modal && !resource->child_attached) {
                [owner->window addChildWindow:resource->window ordered:NSWindowAbove];
                resource->child_attached = true;
            }
            [resource->window makeKeyAndOrderFront:nil];
        }
    } else {
        if (resource->sheet_active && owner) {
            [owner->window endSheet:resource->window];
            resource->sheet_active = false;
        } else {
            if (owner && resource->child_attached) {
                [owner->window removeChildWindow:resource->window];
                resource->child_attached = false;
            }
            [resource->window orderOut:nil];
        }
    }
    return NK_OK;
}

nk_result NK_CALL nk_window_set_title(nk_handle handle, const char *title) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    NSString *value = string(title);
    if (title && *title && !value)
        return fail(NK_ERROR_INVALID_ARGUMENT, "title is not valid UTF-8");
    resource->window.title = value ?: @"";
    return NK_OK;
}

nk_result NK_CALL nk_window_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                       int32_t height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (width <= 0 || height <= 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "window dimensions must be positive");
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    [resource->window setFrameOrigin:NSMakePoint(x, y)];
    [resource->window setContentSize:NSMakeSize(width, height)];
    return NK_OK;
}

nk_result NK_CALL nk_window_get_scale(nk_handle handle, float *out_scale) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_scale)
        return fail(NK_ERROR_INVALID_ARGUMENT, "scale output must not be null");
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    *out_scale = static_cast<float>(resource->window.backingScaleFactor);
    return NK_OK;
}

nk_result NK_CALL nk_window_get_state(nk_handle h, nk_window_state *out) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!out || out->struct_size < sizeof(*out))
        return fail(NK_ERROR_INVALID_ARGUMENT, "window state output is missing or too small");
    auto w = window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    auto size = out->struct_size;
    *out = {};
    out->struct_size = size;
    if (w->window.visible)
        out->flags |= NK_WINDOW_STATE_VISIBLE;
    if (w->window.keyWindow)
        out->flags |= NK_WINDOW_STATE_ACTIVE;
    if (w->window.miniaturized)
        out->flags |= NK_WINDOW_STATE_MINIMIZED;
    if (w->window.zoomed)
        out->flags |= NK_WINDOW_STATE_MAXIMIZED;
    if (w->window.styleMask & NSWindowStyleMaskFullScreen)
        out->flags |= NK_WINDOW_STATE_FULLSCREEN;
    return NK_OK;
}

nk_result NK_CALL nk_window_is_focused(nk_handle h, uint32_t *out_focused) {
    if (!out_focused)
        return fail(NK_ERROR_INVALID_ARGUMENT, "focus output must not be null");
    nk_window_state state{sizeof(state), 0, {0, 0}};
    const auto result = nk_window_get_state(h, &state);
    if (result == NK_OK)
        *out_focused = (state.flags & NK_WINDOW_STATE_ACTIVE) ? 1u : 0u;
    return result;
}

nk_result NK_CALL nk_window_is_visible(nk_handle h, uint32_t *out_visible) {
    if (!out_visible)
        return fail(NK_ERROR_INVALID_ARGUMENT, "visibility output must not be null");
    nk_window_state state{sizeof(state), 0, {0, 0}};
    const auto result = nk_window_get_state(h, &state);
    if (result == NK_OK)
        *out_visible = (state.flags & NK_WINDOW_STATE_VISIBLE) ? 1u : 0u;
    return result;
}
nk_result NK_CALL nk_window_minimize(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    [w->window miniaturize:nil];
    return NK_OK;
}
nk_result NK_CALL nk_window_maximize(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    if (!w->window.zoomed)
        [w->window zoom:nil];
    return NK_OK;
}
nk_result NK_CALL nk_window_restore(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    if (w->window.miniaturized)
        [w->window deminiaturize:nil];
    if (w->window.zoomed)
        [w->window zoom:nil];
    return NK_OK;
}
nk_result NK_CALL nk_window_activate(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    [NSApp activateIgnoringOtherApps:YES];
    [w->window makeKeyAndOrderFront:nil];
    return NK_OK;
}
nk_result NK_CALL nk_window_set_fullscreen(nk_handle h, uint32_t enabled) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    bool current = (w->window.styleMask & NSWindowStyleMaskFullScreen) != 0;
    if (current != !!enabled)
        [w->window toggleFullScreen:nil];
    return NK_OK;
}
nk_result NK_CALL nk_window_request_attention(nk_handle h) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!window(h))
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    [NSApp requestUserAttention:NSCriticalRequest];
    return NK_OK;
}
nk_result NK_CALL nk_window_set_size_limits(nk_handle h, const nk_window_size_limits *l) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!l || l->struct_size < sizeof(*l) || l->min_width < 0 || l->min_height < 0 ||
        l->max_width < 0 || l->max_height < 0 || (l->max_width && l->max_width < l->min_width) ||
        (l->max_height && l->max_height < l->min_height))
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid window size limits");
    auto w = window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    w->window.contentMinSize = NSMakeSize(l->min_width, l->min_height);
    w->window.contentMaxSize =
        NSMakeSize(l->max_width ? l->max_width : FLT_MAX, l->max_height ? l->max_height : FLT_MAX);
    return NK_OK;
}

nk_result NK_CALL nk_window_get_native(nk_handle handle, nk_native_window *out_native) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_native || out_native->struct_size < sizeof(*out_native))
        return fail(NK_ERROR_INVALID_ARGUMENT, "native window output is missing or too small");
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const uint32_t size = out_native->struct_size;
    *out_native = {};
    out_native->struct_size = size;
    out_native->kind = NK_NATIVE_WINDOW_COCOA;
    out_native->window = reinterpret_cast<uintptr_t>((__bridge void *)resource->window);
    out_native->view = reinterpret_cast<uintptr_t>((__bridge void *)resource->content);
    return NK_OK;
}

nk_result NK_CALL nk_window_wrap_native(const nk_native_window *, nk_handle *) {
    return unsupported();
}
nk_result NK_CALL nk_webview_create(nk_handle parent_handle, const nk_webview_options *options,
                                    nk_handle *out_webview) {
    return nk::core::result_boundary("unexpected error while creating WebView", [&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!options || options->struct_size < sizeof(*options) || !out_webview ||
            options->width <= 0 || options->height <= 0)
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid WebView options");
        if (!valid_utf8(options->initial_url))
            return fail(NK_ERROR_INVALID_ARGUMENT, "initial URL is not valid UTF-8");
        *out_webview = NK_INVALID_HANDLE;
        auto parent = window(parent_handle);
        if (!parent)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
        parent->children.reserve(parent->children.size() + 1);
        NSURL *initial_url = nil;
        if (options->initial_url) {
            initial_url = [NSURL URLWithString:string(options->initial_url)];
            if (!initial_url)
                return fail(NK_ERROR_INVALID_ARGUMENT, "initial URL is invalid");
        }

        auto resource = std::make_shared<MacWebViewResource>();
        resource->parent = parent_handle;
        resource->generation = nk::core::runtime_generation();
        resource->content_controller = [WKUserContentController new];
        resource->delegate = [NKWebViewDelegate new];
        [resource->content_controller addScriptMessageHandler:resource->delegate name:@"nativekit"];
        WKWebViewConfiguration *configuration = [WKWebViewConfiguration new];
        configuration.userContentController = resource->content_controller;
        resource->view = [[WKWebView alloc]
            initWithFrame:NSMakeRect(options->x, options->y, options->width, options->height)
            configuration:configuration];
        if (!resource->view)
            return fail(NK_ERROR_UNKNOWN, "could not create WKWebView");
        resource->view.autoresizingMask = NSViewMaxXMargin | NSViewMaxYMargin;
        resource->view.navigationDelegate = resource->delegate;
        resource->handle = nk::core::handles().insert(nk::core::ResourceType::webview, resource);
        if (!resource->handle)
            return fail(NK_ERROR_OUT_OF_MEMORY, "WebView handle registry is full");
        resource->delegate.resource = resource.get();
        [resource->view addObserver:resource->delegate
                         forKeyPath:@"title"
                            options:NSKeyValueObservingOptionNew
                            context:nullptr];
        resource->observing_title = true;
        if (@available(macOS 13.3, *))
            resource->view.inspectable = (options->flags & NK_WEBVIEW_DEVTOOLS) != 0;
        resource->view.hidden = (options->flags & NK_WEBVIEW_HIDDEN) != 0;
        [parent->content addSubview:resource->view];
        parent->children.push_back(resource->handle);
        *out_webview = resource->handle;

        emit_webview_text(NK_EVENT_WEBVIEW_READY, resource->handle, nil);
        if (initial_url)
            [resource->view loadRequest:[NSURLRequest requestWithURL:initial_url]];
        return NK_OK;
    });
}

nk_result NK_CALL nk_webview_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    cancel_navigation_decisions(handle);
    cancel_evaluations(handle);
    auto parent = window(resource->parent);
    if (parent) {
        auto &children = parent->children;
        children.erase(std::remove(children.begin(), children.end(), handle), children.end());
    }
    if (resource->observing_title) {
        [resource->view removeObserver:resource->delegate forKeyPath:@"title"];
        resource->observing_title = false;
    }
    resource->view.navigationDelegate = nil;
    [resource->content_controller removeScriptMessageHandlerForName:@"nativekit"];
    resource->delegate.resource = nullptr;
    [resource->view stopLoading];
    [resource->view removeFromSuperview];
    resource->view = nil;
    nk::core::handles().erase(handle, nk::core::ResourceType::webview);
    return NK_OK;
}

nk_result NK_CALL nk_webview_show(nk_handle handle, uint32_t visible) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    resource->view.hidden = !visible;
    return NK_OK;
}

nk_result NK_CALL nk_webview_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                        int32_t height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (width <= 0 || height <= 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "WebView dimensions must be positive");
    auto resource = webview(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    resource->view.frame = NSMakeRect(x, y, width, height);
    return NK_OK;
}

nk_result NK_CALL nk_webview_navigate(nk_handle handle, const char *url) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    NSString *value = string(url);
    NSURL *target = value ? [NSURL URLWithString:value] : nil;
    if (!target)
        return fail(NK_ERROR_INVALID_ARGUMENT, "URL is null, invalid UTF-8, or malformed");
    [resource->view loadRequest:[NSURLRequest requestWithURL:target]];
    return NK_OK;
}

nk_result NK_CALL nk_webview_set_html(nk_handle handle, const char *html, const char *base_url) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    NSString *document = string(html);
    NSString *base = string(base_url);
    if (!document || (base_url && !base))
        return fail(NK_ERROR_INVALID_ARGUMENT, "HTML or base URL is invalid UTF-8");
    NSURL *base_target = base.length ? [NSURL URLWithString:base] : nil;
    if (base.length && !base_target)
        return fail(NK_ERROR_INVALID_ARGUMENT, "base URL is malformed");
    [resource->view loadHTMLString:document baseURL:base_target];
    return NK_OK;
}

nk_result NK_CALL nk_webview_can_go_back(nk_handle handle, uint32_t *out_can_go_back) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource || !out_can_go_back)
        return fail(!resource ? NK_ERROR_INVALID_HANDLE : NK_ERROR_INVALID_ARGUMENT,
                    !resource ? "invalid or stale WebView handle" : "history output is null");
    *out_can_go_back = resource->view.canGoBack ? 1u : 0u;
    return NK_OK;
}

nk_result NK_CALL nk_webview_can_go_forward(nk_handle handle, uint32_t *out_can_go_forward) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource || !out_can_go_forward)
        return fail(!resource ? NK_ERROR_INVALID_HANDLE : NK_ERROR_INVALID_ARGUMENT,
                    !resource ? "invalid or stale WebView handle" : "history output is null");
    *out_can_go_forward = resource->view.canGoForward ? 1u : 0u;
    return NK_OK;
}

nk_result NK_CALL nk_webview_go_back(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    [resource->view goBack];
    return NK_OK;
}

nk_result NK_CALL nk_webview_go_forward(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    [resource->view goForward];
    return NK_OK;
}

nk_result NK_CALL nk_webview_reload(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    [resource->view reload];
    return NK_OK;
}

nk_result NK_CALL nk_webview_stop(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = webview(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    [resource->view stopLoading];
    return NK_OK;
}

nk_result NK_CALL nk_webview_eval(nk_handle handle, const char *script,
                                  nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while evaluating JavaScript", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!out_request)
                return fail(NK_ERROR_INVALID_ARGUMENT, "evaluation request output is null");
            *out_request = NK_INVALID_REQUEST_ID;
            auto resource = webview(handle);
            if (!resource)
                return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
            NSString *source = string(script);
            if (!source)
                return fail(NK_ERROR_INVALID_ARGUMENT, "script is null or invalid UTF-8");
            NSString *wrapped = javascript_json_wrapper(source);
            if (!wrapped)
                return fail(NK_ERROR_OUT_OF_MEMORY, "could not encode JavaScript source");
            const auto request = nk::core::next_request_id();
            const auto generation = nk::core::runtime_generation();
            evaluations.emplace(request, handle);
            [resource->view
                evaluateJavaScript:wrapped
                 completionHandler:^(id value, NSError *error) {
                   if (!nk::core::is_runtime_generation(generation))
                       return;
                   const auto pending = evaluations.find(request);
                   if (pending == evaluations.end() || pending->second != handle)
                       return;
                   evaluations.erase(pending);
                   if (error)
                       emit_webview_text(NK_EVENT_WEBVIEW_EVAL_COMPLETE, handle,
                                         error.localizedDescription, NK_ERROR_UNKNOWN, 0, request);
                   else
                       emit_webview_text(NK_EVENT_WEBVIEW_EVAL_COMPLETE, handle,
                                         [value isKindOfClass:[NSString class]] ? value : @"",
                                         NK_OK, 0, request);
                 }];
            *out_request = request;
            return NK_OK;
        });
}

nk_result NK_CALL nk_webview_navigation_decide(nk_request_id request, uint32_t allow) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    const auto item = navigation_decisions.find(request);
    if (item == navigation_decisions.end())
        return fail(NK_ERROR_INVALID_REQUEST, "invalid or completed navigation request");
    auto handler = item->second.handler;
    navigation_decisions.erase(item);
    handler(allow ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
    return NK_OK;
}

nk_result NK_CALL nk_dialog_open_file(nk_handle parent, const nk_file_dialog_options *options,
                                      nk_request_id *request) {
    return nk::core::result_boundary(
        "unexpected error while opening file dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, request, NK_DIALOG_OPEN_FILE);
        });
}
nk_result NK_CALL nk_dialog_save_file(nk_handle parent, const nk_file_dialog_options *options,
                                      nk_request_id *request) {
    return nk::core::result_boundary(
        "unexpected error while opening save dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, request, NK_DIALOG_SAVE_FILE);
        });
}
nk_result NK_CALL nk_dialog_select_directory(nk_handle parent,
                                             const nk_file_dialog_options *options,
                                             nk_request_id *request) {
    return nk::core::result_boundary(
        "unexpected error while opening directory dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, request, NK_DIALOG_SELECT_DIRECTORY);
        });
}

nk_result NK_CALL nk_dialog_message(nk_handle parent_handle,
                                    const nk_message_dialog_options *options,
                                    nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while opening message dialog", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!options || options->struct_size < sizeof(*options) || !options->message ||
                !out_request)
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid message dialog options");
            if (!valid_utf8(options->title) || !valid_utf8(options->message))
                return fail(NK_ERROR_INVALID_ARGUMENT, "message dialog option is not valid UTF-8");
            auto parent = parent_handle ? window(parent_handle) : nullptr;
            if (parent_handle && !parent)
                return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
            auto context = std::make_shared<DialogContext>();
            context->request = nk::core::next_request_id();
            context->generation = nk::core::runtime_generation();
            context->kind = NK_DIALOG_MESSAGE;
            context->parent = parent ? parent->window : nil;
            NSAlert *alert = [NSAlert new];
            alert.messageText = string(options->title) ?: @"";
            alert.informativeText = string(options->message) ?: @"";
            if (options->kind == NK_MESSAGE_WARNING)
                alert.alertStyle = NSAlertStyleWarning;
            else if (options->kind == NK_MESSAGE_ERROR)
                alert.alertStyle = NSAlertStyleCritical;
            else
                alert.alertStyle = NSAlertStyleInformational;
            auto add_button = [&](uint32_t flag, NSString *label, uint32_t result) {
                if (options->buttons & flag) {
                    [alert addButtonWithTitle:label];
                    context->message_results.push_back(result);
                }
            };
            add_button(NK_MESSAGE_BUTTON_OK, @"OK", NK_MESSAGE_RESULT_OK);
            add_button(NK_MESSAGE_BUTTON_YES, @"Yes", NK_MESSAGE_RESULT_YES);
            add_button(NK_MESSAGE_BUTTON_NO, @"No", NK_MESSAGE_RESULT_NO);
            add_button(NK_MESSAGE_BUTTON_CANCEL, @"Cancel", NK_MESSAGE_RESULT_CANCEL);
            if (context->message_results.empty()) {
                [alert addButtonWithTitle:@"OK"];
                context->message_results.push_back(NK_MESSAGE_RESULT_OK);
            }
            context->dialog = alert;
            {
                std::lock_guard lock(dialogs_mutex);
                dialogs.emplace(context->request, context);
            }
            const nk_request_id request = context->request;
            if (parent) {
                [alert beginSheetModalForWindow:parent->window
                              completionHandler:^(NSModalResponse response) {
                                finish_message_dialog(request, response);
                              }];
            } else {
                dispatch_async(dispatch_get_main_queue(), ^{
                  finish_message_dialog(request, [alert runModal]);
                });
            }
            *out_request = request;
            return NK_OK;
        });
}

nk_result NK_CALL nk_dialog_cancel(nk_request_id request) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    std::shared_ptr<DialogContext> context;
    {
        std::lock_guard lock(dialogs_mutex);
        const auto found = dialogs.find(request);
        if (!request || found == dialogs.end())
            return fail(NK_ERROR_INVALID_REQUEST, "invalid or completed dialog request");
        context = found->second;
    }
    cancel_dialog_context(context);
    return NK_OK;
}

nk_result NK_CALL nk_shell_open_url(const char *url) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    NSString *value = string(url);
    NSURL *native = value ? [NSURL URLWithString:value] : nil;
    if (!native || !native.scheme.length)
        return fail(NK_ERROR_INVALID_ARGUMENT, "URL must contain a valid URI scheme");
    return [NSWorkspace.sharedWorkspace openURL:native]
               ? NK_OK
               : fail(NK_ERROR_UNKNOWN, "macOS could not open URL");
}

nk_result NK_CALL nk_shell_open_resource(const nk_resource *resource) {
    if (!resource || resource->struct_size < sizeof(nk_resource))
        return fail(NK_ERROR_INVALID_ARGUMENT, "resource descriptor is invalid");
    return nk_shell_open_url(resource->uri);
}

nk_result NK_CALL nk_share(const nk_share_options *) {
    return fail(NK_ERROR_UNSUPPORTED, "resource sharing is not implemented by the macOS backend");
}

nk_result NK_CALL nk_clipboard_set_resources(const nk_resource *, uint32_t) {
    return fail(NK_ERROR_UNSUPPORTED, "resource clipboard is not implemented by the macOS backend");
}

nk_result NK_CALL nk_clipboard_read_resources(nk_request_id *) {
    return fail(NK_ERROR_UNSUPPORTED, "resource clipboard is not implemented by the macOS backend");
}

nk_result NK_CALL nk_dialog_open_resource(nk_handle, const nk_file_dialog_options *, nk_request_id *) {
    return fail(NK_ERROR_UNSUPPORTED, "resource dialogs are not implemented by the macOS backend");
}

nk_result NK_CALL nk_dialog_save_resource(nk_handle, const nk_file_dialog_options *, nk_request_id *) {
    return fail(NK_ERROR_UNSUPPORTED, "resource dialogs are not implemented by the macOS backend");
}

nk_result NK_CALL nk_dialog_select_resource_directory(nk_handle, const nk_file_dialog_options *, nk_request_id *) {
    return fail(NK_ERROR_UNSUPPORTED, "resource dialogs are not implemented by the macOS backend");
}

nk_result NK_CALL nk_shell_open_file(const char *path) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    NSString *value = string(path);
    if (!value.length)
        return fail(NK_ERROR_INVALID_ARGUMENT, "path must not be empty");
    return [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:value]]
               ? NK_OK
               : fail(NK_ERROR_UNKNOWN, "macOS could not open file");
}

nk_result NK_CALL nk_shell_reveal_file(const char *path) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    NSString *value = string(path);
    if (!value.length)
        return fail(NK_ERROR_INVALID_ARGUMENT, "path must not be empty");
    [NSWorkspace.sharedWorkspace
        activateFileViewerSelectingURLs:@[ [NSURL fileURLWithPath:value] ]];
    return NK_OK;
}

nk_result NK_CALL nk_system_directory(nk_system_directory_kind kind, char *buffer,
                                      uint32_t *inout_size) {
    nk::core::clear_error();
    NSString *path = nil;
    if (kind == NK_DIRECTORY_HOME)
        path = NSHomeDirectory();
    else if (kind == NK_DIRECTORY_TEMP)
        path = NSTemporaryDirectory();
    else
        path = directory_url(kind).path;
    if (!path)
        return fail(NK_ERROR_UNSUPPORTED, "system directory is unavailable");
    return copy_output(path, buffer, inout_size);
}

nk_result NK_CALL nk_system_locale(char *buffer, uint32_t *inout_size) {
    nk::core::clear_error();
    return copy_output(NSLocale.currentLocale.localeIdentifier, buffer, inout_size);
}

nk_result NK_CALL nk_system_get_appearance(nk_system_appearance *appearance) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!appearance || appearance->struct_size < sizeof(*appearance))
        return fail(NK_ERROR_INVALID_ARGUMENT, "appearance output is missing or too small");
    const uint32_t size = appearance->struct_size;
    *appearance = {};
    appearance->struct_size = size;
    NSAppearanceName match = [NSApp.effectiveAppearance
        bestMatchFromAppearancesWithNames:@[ NSAppearanceNameAqua, NSAppearanceNameDarkAqua ]];
    appearance->color_scheme = [match isEqualToString:NSAppearanceNameDarkAqua]
                                   ? NK_COLOR_SCHEME_DARK
                                   : NK_COLOR_SCHEME_LIGHT;
    appearance->high_contrast =
        NSWorkspace.sharedWorkspace.accessibilityDisplayShouldIncreaseContrast;
    return NK_OK;
}

nk_result NK_CALL nk_clipboard_set_text(const char *text) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!text)
        return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard text must not be null");
    NSString *value = string(text);
    if (!value)
        return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard text is not valid UTF-8");
    NSPasteboard *pasteboard = NSPasteboard.generalPasteboard;
    [pasteboard clearContents];
    return [pasteboard setString:value forType:NSPasteboardTypeString]
               ? NK_OK
               : fail(NK_ERROR_UNKNOWN, "macOS rejected clipboard text");
}

nk_result NK_CALL nk_clipboard_set_files(const char *const *paths, uint32_t path_count) {
    return nk::core::result_boundary(
        "unexpected error while writing clipboard files", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!paths || !path_count)
                return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard file list must not be empty");
            NSMutableArray<NSURL *> *urls = [NSMutableArray arrayWithCapacity:path_count];
            for (uint32_t index = 0; index < path_count; ++index) {
                NSString *path = string(paths[index]);
                if (!path.length)
                    return fail(NK_ERROR_INVALID_ARGUMENT,
                                "clipboard path is empty or invalid UTF-8");
                if (![path isAbsolutePath])
                    path = [NSFileManager.defaultManager.currentDirectoryPath
                        stringByAppendingPathComponent:path];
                [urls addObject:[NSURL fileURLWithPath:path.stringByStandardizingPath]];
            }
            NSPasteboard *pasteboard = NSPasteboard.generalPasteboard;
            [pasteboard clearContents];
            return [pasteboard writeObjects:urls]
                       ? NK_OK
                       : fail(NK_ERROR_UNKNOWN, "macOS rejected clipboard files");
        });
}

nk_result NK_CALL nk_clipboard_read_text(nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while reading clipboard text", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!out_request)
                return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard request output is null");
            *out_request = NK_INVALID_REQUEST_ID;
            const auto request = nk::core::next_request_id();
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_CLIPBOARD_TEXT_COMPLETE;
            event.request_id = request;
            event.data = text_bytes(
                utf8([NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString]));
            const auto result = nk::core::push_event(std::move(event));
            if (result != NK_OK)
                return fail(result, "could not queue clipboard text result");
            *out_request = request;
            return NK_OK;
        });
}

nk_result NK_CALL nk_clipboard_read_files(nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while reading clipboard files", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!out_request)
                return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard request output is null");
            *out_request = NK_INVALID_REQUEST_ID;
            std::vector<std::string> paths;
            for (NSURL *url in pasteboard_file_urls(NSPasteboard.generalPasteboard)) {
                const bool scoped = [url startAccessingSecurityScopedResource];
                paths.push_back(utf8(url.path));
                if (scoped)
                    [url stopAccessingSecurityScopedResource];
            }
            const auto request = nk::core::next_request_id();
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_CLIPBOARD_FILES_COMPLETE;
            event.request_id = request;
            event.data_count = static_cast<uint32_t>(paths.size());
            nk_clipboard_files header{static_cast<uint32_t>(paths.size()), 0};
            event.data = string_list_payload(header, paths, &nk_clipboard_files::strings_offset);
            const auto result = nk::core::push_event(std::move(event));
            if (result != NK_OK)
                return fail(result, "could not queue clipboard file result");
            *out_request = request;
            return NK_OK;
        });
}

nk_result NK_CALL nk_window_set_drop_enabled(nk_handle handle, uint32_t enabled) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    if (enabled)
        [resource->content
            registerForDraggedTypes:@[ NSPasteboardTypeFileURL, NSPasteboardTypeString ]];
    else
        [resource->content unregisterDraggedTypes];
    return NK_OK;
}

nk_result NK_CALL nk_notification_show(const nk_notification_options *options,
                                       nk_request_id *out_request) {
    return nk::core::result_boundary("unexpected error while showing notification", [&]() -> nk_result {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!options || options->struct_size < sizeof(*options) || !out_request ||
            !options->title || !*options->title || !valid_utf8(options->title) ||
            !valid_utf8(options->body) || !valid_utf8(options->icon))
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid notification options");
        *out_request = NK_INVALID_REQUEST_ID;
        NSString *title = string(options->title);
        NSString *body = string(options->body) ?: @"";
        UNMutableNotificationContent *content = [UNMutableNotificationContent new];
        content.title = title;
        content.body = body;
        content.categoryIdentifier = @"nativekit.default";
        if (!(options->flags & NK_NOTIFICATION_SILENT))
            content.sound = UNNotificationSound.defaultSound;
        if (options->icon && *options->icon) {
            NSString *path = string(options->icon);
            if (![path isAbsolutePath] || ![NSFileManager.defaultManager fileExistsAtPath:path])
                return fail(NK_ERROR_INVALID_ARGUMENT, "notification icon path does not exist");
            NSError *attachment_error = nil;
            UNNotificationAttachment *attachment =
                [UNNotificationAttachment attachmentWithIdentifier:@"icon"
                                                               URL:[NSURL fileURLWithPath:path]
                                                           options:nil
                                                             error:&attachment_error];
            if (!attachment)
                return fail(NK_ERROR_INVALID_ARGUMENT, "notification icon could not be attached");
            content.attachments = @[ attachment ];
        }
        const auto request = nk::core::next_request_id();
        const auto generation = nk::core::runtime_generation();
        {
            std::lock_guard lock(notifications_mutex);
            notifications.emplace(request, generation);
        }
        UNUserNotificationCenter *center = UNUserNotificationCenter.currentNotificationCenter;
        if (!notification_delegate)
            notification_delegate = [NKNotificationDelegate new];
        center.delegate = notification_delegate;
        UNNotificationRequest *native_request =
            [UNNotificationRequest requestWithIdentifier:notification_identifier(request)
                                                 content:content
                                                 trigger:nil];
        [center
            requestAuthorizationWithOptions:(UNAuthorizationOptionAlert |
                                             UNAuthorizationOptionSound)
                          completionHandler:^(BOOL granted, NSError *error) {
                            if (!has_notification(request, generation) ||
                                !nk::core::is_runtime_generation(generation))
                                return;
                            if (!granted) {
                                take_notification(request);
                                emit_notification(NK_EVENT_NOTIFICATION_FAILED, request,
                                                  NK_ERROR_UNSUPPORTED,
                                                  error.localizedDescription
                                                      ?: @"notification permission was denied");
                                return;
                            }
                            [center getNotificationCategoriesWithCompletionHandler:^(
                                        NSSet<UNNotificationCategory *> *categories) {
                              if (!has_notification(request, generation) ||
                                  !nk::core::is_runtime_generation(generation))
                                  return;
                              NSMutableSet<UNNotificationCategory *> *updated =
                                  [categories mutableCopy];
                              [updated
                                  addObject:
                                      [UNNotificationCategory
                                          categoryWithIdentifier:@"nativekit.default"
                                                         actions:@[]
                                               intentIdentifiers:@[]
                                                         options:
                                                             UNNotificationCategoryOptionCustomDismissAction]];
                              [center setNotificationCategories:updated];
                              [center addNotificationRequest:native_request
                                       withCompletionHandler:^(NSError *delivery_error) {
                                         if (!has_notification(request, generation) ||
                                             !nk::core::is_runtime_generation(generation))
                                             return;
                                         if (delivery_error) {
                                             take_notification(request);
                                             emit_notification(NK_EVENT_NOTIFICATION_FAILED,
                                                               request, NK_ERROR_UNKNOWN,
                                                               delivery_error.localizedDescription);
                                         } else {
                                             emit_notification(NK_EVENT_NOTIFICATION_DELIVERED,
                                                               request);
                                         }
                                       }];
                            }];
                          }];
        *out_request = request;
        return NK_OK;
    });
}

nk_result NK_CALL nk_notification_close(nk_request_id request) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!request || !take_notification(request))
        return fail(NK_ERROR_INVALID_REQUEST, "invalid or completed notification request");
    NSArray<NSString *> *identifiers = @[ notification_identifier(request) ];
    UNUserNotificationCenter *center = UNUserNotificationCenter.currentNotificationCenter;
    [center removePendingNotificationRequestsWithIdentifiers:identifiers];
    [center removeDeliveredNotificationsWithIdentifiers:identifiers];
    emit_notification(NK_EVENT_NOTIFICATION_DISMISSED, request);
    return NK_OK;
}
}
