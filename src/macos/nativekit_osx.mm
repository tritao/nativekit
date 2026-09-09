/*
 * Cocoa behavior in this file is informed by the wxWidgets OSX donor files
 * enumerated in tools/upstream-lock.json at its pinned revision. Adaptations are
 * licensed under the wxWindows Library Licence 3.1; see licenses/wxWidgets.txt.
 */

#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>

#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include "core/error.hpp"
#include "core/runtime.hpp"

#include <algorithm>
#include <cstddef>
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
@property(nonatomic, assign) void* resource;
@end

@interface NKContentView : NSView <NSDraggingDestination>
@property(nonatomic, assign) void* resource;
@end

@interface NKWebViewDelegate : NSObject <WKNavigationDelegate, WKScriptMessageHandler>
@property(nonatomic, assign) void* resource;
@end

namespace {

struct MacWindowResource final : nk::core::Resource {
    __strong NSWindow* window = nil;
    __strong NKContentView* content = nil;
    __strong NKWindowDelegate* delegate = nil;
    nk_handle handle = NK_INVALID_HANDLE;
    std::vector<nk_handle> children;
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
    __strong WKWebView* view = nil;
    __strong WKUserContentController* content_controller = nil;
    __strong NKWebViewDelegate* delegate = nil;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle parent = NK_INVALID_HANDLE;
    bool observing_title = false;
    ~MacWebViewResource() override {
        if (view) {
            if (observing_title) [view removeObserver:delegate forKeyPath:@"title"];
            view.navigationDelegate = nil;
            [content_controller removeScriptMessageHandlerForName:@"nativekit"];
            delegate.resource = nullptr;
            [view stopLoading];
            [view removeFromSuperview];
        }
    }
};

struct DialogContext {
    nk_request_id request = NK_INVALID_REQUEST_ID;
    uint32_t kind = 0;
    __strong NSWindow* parent = nil;
    __strong id dialog = nil;
    std::vector<uint32_t> message_results;
};

std::mutex dialogs_mutex;
std::unordered_map<nk_request_id, std::shared_ptr<DialogContext>> dialogs;

void cancel_dialog_context(const std::shared_ptr<DialogContext>& context) {
    if ([context->dialog isKindOfClass:[NSSavePanel class]])
        [(NSSavePanel*)context->dialog cancel:nil];
    else if ([context->dialog isKindOfClass:[NSAlert class]]) {
        NSAlert* alert = (NSAlert*)context->dialog;
        if (context->parent) [context->parent endSheet:alert.window returnCode:NSModalResponseCancel];
        else [NSApp abortModal];
    }
}

void cancel_dialogs_for_parent(NSWindow* parent) {
    std::vector<std::shared_ptr<DialogContext>> matching;
    {
        std::lock_guard lock(dialogs_mutex);
        for (const auto& item : dialogs)
            if (item.second->parent == parent) matching.push_back(item.second);
    }
    for (const auto& context : matching) cancel_dialog_context(context);
}

nk_result fail(nk_result result, std::string_view message) {
    nk::core::set_error(message);
    return result;
}

nk_result enter_ui() {
    nk::core::clear_error();
    return nk::core::require_ui_thread();
}

NSString* string(const char* value) {
    if (!value) return nil;
    return [[NSString alloc] initWithBytes:value
                                    length:std::strlen(value)
                                  encoding:NSUTF8StringEncoding];
}

bool valid_utf8(const char* value) {
    return !value || !*value || string(value) != nil;
}

std::string utf8(NSString* value) {
    if (!value) return {};
    const char* bytes = value.UTF8String;
    return bytes ? std::string(bytes) : std::string();
}

template<typename T>
std::vector<std::byte> bytes_of(const T& value) {
    const auto* first = reinterpret_cast<const std::byte*>(&value);
    return {first, first + sizeof(value)};
}

std::vector<std::byte> text_bytes(const std::string& value) {
    const auto* first = reinterpret_cast<const std::byte*>(value.data());
    return {first, first + value.size()};
}

template<typename Header>
std::vector<std::byte> string_list_payload(Header header,
                                           const std::vector<std::string>& strings,
                                           uint32_t Header::*offset_member) {
    header.*offset_member = sizeof(Header);
    std::size_t total = sizeof(Header);
    for (const auto& value : strings) total += value.size() + 1;
    std::vector<std::byte> result(total);
    std::memcpy(result.data(), &header, sizeof(header));
    std::size_t cursor = sizeof(Header);
    for (const auto& value : strings) {
        std::memcpy(result.data() + cursor, value.c_str(), value.size() + 1);
        cursor += value.size() + 1;
    }
    return result;
}

NSArray<NSURL*>* pasteboard_file_urls(NSPasteboard* pasteboard) {
    NSDictionary* options = @{NSPasteboardURLReadingFileURLsOnlyKey: @YES};
    NSArray* values = [pasteboard readObjectsForClasses:@[[NSURL class]] options:options];
    NSMutableArray<NSURL*>* files = [NSMutableArray array];
    for (NSURL* value in values)
        if (value.fileURL) [files addObject:value];
    return files;
}

bool emit_drop(MacWindowResource& resource, id<NSDraggingInfo> information) noexcept {
    try {
        NSPasteboard* pasteboard = information.draggingPasteboard;
        NSArray<NSURL*>* urls = pasteboard_file_urls(pasteboard);
        std::vector<std::string> items;
        nk_event_kind kind = NK_EVENT_DROP_FILES;
        if (urls.count) {
            for (NSURL* url in urls) {
                const bool scoped = [url startAccessingSecurityScopedResource];
                items.push_back(utf8(url.path));
                if (scoped) [url stopAccessingSecurityScopedResource];
            }
        } else {
            NSString* text = [pasteboard stringForType:NSPasteboardTypeString];
            if (!text) return false;
            kind = NK_EVENT_DROP_TEXT;
            items.push_back(utf8(text));
        }
        const NSPoint point = [resource.content convertPoint:information.draggingLocation
                                                   fromView:nil];
        nk::core::QueuedEvent event;
        event.kind = kind;
        event.source = resource.handle;
        event.data_count = static_cast<uint32_t>(items.size());
        nk_drop_data header{
            static_cast<int32_t>(point.x),
            static_cast<int32_t>(point.y),
            static_cast<uint32_t>(items.size()), 0};
        event.data = string_list_payload(header, items, &nk_drop_data::strings_offset);
        return nk::core::push_event(std::move(event)) == NK_OK;
    } catch (...) {
        return false;
    }
}

std::shared_ptr<MacWindowResource> window(nk_handle handle) {
    return std::dynamic_pointer_cast<MacWindowResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::window));
}

std::shared_ptr<MacWebViewResource> webview(nk_handle handle) {
    return std::dynamic_pointer_cast<MacWebViewResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::webview));
}

void emit_webview_text(nk_event_kind kind, nk_handle source, NSString* text,
                       nk_result result = NK_OK, uint32_t flags = 0,
                       nk_request_id request = NK_INVALID_REQUEST_ID) noexcept {
    try {
        if (!webview(source)) return;
        nk::core::QueuedEvent event;
        event.kind = kind;
        event.source = source;
        event.result = result;
        event.flags = flags;
        event.request_id = request;
        event.data = text_bytes(utf8(text));
        nk::core::push_event(std::move(event));
    } catch (...) {}
}

uint32_t navigation_error_category(NSError* error) {
    if (![error.domain isEqualToString:NSURLErrorDomain]) return NK_NAVIGATION_ERROR_OTHER;
    switch (error.code) {
        case NSURLErrorCancelled: return NK_NAVIGATION_ERROR_CANCELLED;
        case NSURLErrorBadURL:
        case NSURLErrorUnsupportedURL: return NK_NAVIGATION_ERROR_REQUEST;
        case NSURLErrorUserAuthenticationRequired:
        case NSURLErrorUserCancelledAuthentication: return NK_NAVIGATION_ERROR_AUTH;
        case NSURLErrorServerCertificateHasBadDate:
        case NSURLErrorServerCertificateUntrusted:
        case NSURLErrorServerCertificateHasUnknownRoot:
        case NSURLErrorServerCertificateNotYetValid:
        case NSURLErrorSecureConnectionFailed: return NK_NAVIGATION_ERROR_SECURITY;
        case NSURLErrorFileDoesNotExist:
        case NSURLErrorResourceUnavailable: return NK_NAVIGATION_ERROR_NOT_FOUND;
        case NSURLErrorTimedOut:
        case NSURLErrorCannotFindHost:
        case NSURLErrorCannotConnectToHost:
        case NSURLErrorNetworkConnectionLost:
        case NSURLErrorDNSLookupFailed:
        case NSURLErrorNotConnectedToInternet: return NK_NAVIGATION_ERROR_CONNECTION;
        default: return NK_NAVIGATION_ERROR_OTHER;
    }
}

std::vector<std::byte> dialog_paths_payload(const std::vector<std::string>& paths,
                                            bool accepted) {
    const std::size_t offsets_offset = sizeof(nk_dialog_paths);
    const std::size_t strings_offset = offsets_offset + paths.size() * sizeof(uint32_t);
    std::size_t total = strings_offset;
    for (const auto& path : paths) total += path.size() + 1;
    std::vector<std::byte> result(total);
    const nk_dialog_paths header{
        accepted ? 1u : 0u, static_cast<uint32_t>(paths.size()),
        static_cast<uint32_t>(offsets_offset), static_cast<uint32_t>(strings_offset)};
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
                        NSArray<NSURL*>* urls) noexcept {
    try {
        std::shared_ptr<DialogContext> context;
        {
            std::lock_guard lock(dialogs_mutex);
            const auto found = dialogs.find(request);
            if (found == dialogs.end()) return;
            context = found->second;
            dialogs.erase(found);
        }
        const bool accepted = response == NSModalResponseOK;
        std::vector<std::string> paths;
        if (accepted) {
            for (NSURL* url in urls) paths.push_back(utf8(url.path));
        }
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_DIALOG_COMPLETE;
        event.request_id = request;
        event.flags = context->kind;
        event.data_count = static_cast<uint32_t>(paths.size());
        event.data = dialog_paths_payload(paths, accepted);
        nk::core::push_event(std::move(event));
    } catch (...) {}
}

void finish_message_dialog(nk_request_id request, NSInteger response) noexcept {
    try {
        std::shared_ptr<DialogContext> context;
        {
            std::lock_guard lock(dialogs_mutex);
            const auto found = dialogs.find(request);
            if (found == dialogs.end()) return;
            context = found->second;
            dialogs.erase(found);
        }
        uint32_t result = NK_MESSAGE_RESULT_CANCEL;
        const NSInteger index = response - NSAlertFirstButtonReturn;
        if (index >= 0 && static_cast<std::size_t>(index) < context->message_results.size())
            result = context->message_results[static_cast<std::size_t>(index)];
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_DIALOG_COMPLETE;
        event.request_id = request;
        event.flags = NK_DIALOG_MESSAGE;
        event.data = bytes_of(nk_dialog_message_result{result});
        nk::core::push_event(std::move(event));
    } catch (...) {}
}

void configure_file_panel(NSSavePanel* panel, const nk_file_dialog_options* options) {
    panel.title = string(options->title) ?: @"";
    panel.showsHiddenFiles = (options->flags & NK_DIALOG_SHOW_HIDDEN) != 0;
    if (options->suggested_name) panel.nameFieldStringValue = string(options->suggested_name) ?: @"";
    if (options->initial_path) {
        NSString* path = string(options->initial_path);
        if (path) panel.directoryURL = [NSURL fileURLWithPath:path isDirectory:YES];
    }
    NSMutableArray<NSString*>* extensions = [NSMutableArray array];
    for (uint32_t index = 0; index < options->filter_count; ++index) {
        NSString* patterns = string(options->filters[index].patterns);
        for (NSString* pattern in [patterns componentsSeparatedByString:@";"]) {
            if ([pattern hasPrefix:@"*."] && pattern.length > 2)
                [extensions addObject:[pattern substringFromIndex:2]];
        }
    }
    if (extensions.count) panel.allowedFileTypes = extensions;
}

nk_result start_file_dialog(nk_handle parent_handle,
                            const nk_file_dialog_options* options,
                            nk_request_id* out_request, uint32_t kind) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
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
        if (!parent) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
    }
    auto context = std::make_shared<DialogContext>();
    context->request = nk::core::next_request_id();
    context->kind = kind;
    context->parent = parent ? parent->window : nil;
    NSSavePanel* panel = nil;
    if (kind == NK_DIALOG_SAVE_FILE) {
        panel = [NSSavePanel savePanel];
    } else {
        NSOpenPanel* open = [NSOpenPanel openPanel];
        open.canChooseDirectories = kind == NK_DIALOG_SELECT_DIRECTORY;
        open.canChooseFiles = kind != NK_DIALOG_SELECT_DIRECTORY;
        open.allowsMultipleSelection = kind == NK_DIALOG_OPEN_FILE &&
                                       (options->flags & NK_DIALOG_ALLOW_MULTIPLE);
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
        NSArray<NSURL*>* urls = @[];
        if ([panel isKindOfClass:[NSOpenPanel class]]) urls = [(NSOpenPanel*)panel URLs];
        else if (panel.URL) urls = @[panel.URL];
        finish_file_dialog(request, response, urls);
    };
    if (parent) [panel beginSheetModalForWindow:parent->window completionHandler:completion];
    else [panel beginWithCompletionHandler:completion];
    *out_request = request;
    return NK_OK;
}

nk_result copy_output(NSString* value, char* buffer, uint32_t* inout_size) {
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

NSURL* directory_url(nk_system_directory_kind kind) {
    NSFileManager* manager = NSFileManager.defaultManager;
    NSSearchPathDirectory directory;
    switch (kind) {
        case NK_DIRECTORY_DESKTOP: directory = NSDesktopDirectory; break;
        case NK_DIRECTORY_DOCUMENTS: directory = NSDocumentDirectory; break;
        case NK_DIRECTORY_DOWNLOADS: directory = NSDownloadsDirectory; break;
        case NK_DIRECTORY_CACHE: directory = NSCachesDirectory; break;
        case NK_DIRECTORY_CONFIG: directory = NSLibraryDirectory; break;
        case NK_DIRECTORY_DATA: directory = NSApplicationSupportDirectory; break;
        default: return nil;
    }
    return [manager URLForDirectory:directory inDomain:NSUserDomainMask
                   appropriateForURL:nil create:NO error:nil];
}

nk_result unsupported() {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    return fail(NK_ERROR_UNSUPPORTED, "this macOS service is not implemented yet");
}

} // namespace

@implementation NKWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
    (void)sender;
    auto* resource = static_cast<MacWindowResource*>(_resource);
    if (resource && resource->handle) {
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WINDOW_CLOSE;
        event.source = resource->handle;
        nk::core::push_event(std::move(event));
    }
    return NO;
}
- (void)windowDidResize:(NSNotification*)notification {
    (void)notification;
    auto* resource = static_cast<MacWindowResource*>(_resource);
    if (!resource || !resource->handle) return;
    const NSSize size = resource->window.contentView.bounds.size;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_WINDOW_RESIZE;
    event.source = resource->handle;
    event.data = bytes_of(nk_window_resize_event{
        static_cast<int32_t>(size.width), static_cast<int32_t>(size.height)});
    nk::core::push_event(std::move(event));
}
- (void)windowDidChangeBackingProperties:(NSNotification*)notification {
    (void)notification;
    auto* resource = static_cast<MacWindowResource*>(_resource);
    if (!resource || !resource->handle) return;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_WINDOW_SCALE_CHANGED;
    event.source = resource->handle;
    event.data = bytes_of(nk_window_scale_event{
        static_cast<float>(resource->window.backingScaleFactor)});
    nk::core::push_event(std::move(event));
}
@end

@implementation NKContentView
- (BOOL)isFlipped { return YES; }
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    NSPasteboard* pasteboard = sender.draggingPasteboard;
    return pasteboard_file_urls(pasteboard).count ||
           [pasteboard availableTypeFromArray:@[NSPasteboardTypeString]]
        ? NSDragOperationCopy : NSDragOperationNone;
}
- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender {
    (void)sender;
    return _resource != nullptr;
}
- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    auto* resource = static_cast<MacWindowResource*>(_resource);
    return resource && emit_drop(*resource, sender);
}
@end

@implementation NKWebViewDelegate
- (void)webView:(WKWebView*)view didFinishNavigation:(WKNavigation*)navigation {
    (void)navigation;
    auto* resource = static_cast<MacWebViewResource*>(_resource);
    if (resource)
        emit_webview_text(NK_EVENT_WEBVIEW_NAVIGATED, resource->handle,
                          view.URL.absoluteString);
}
- (void)webView:(WKWebView*)view didFailNavigation:(WKNavigation*)navigation
      withError:(NSError*)error {
    (void)view;
    (void)navigation;
    auto* resource = static_cast<MacWebViewResource*>(_resource);
    if (resource)
        emit_webview_text(NK_EVENT_WEBVIEW_NAVIGATION_FAILED, resource->handle,
                          error.localizedDescription, NK_ERROR_UNKNOWN,
                          navigation_error_category(error));
}
- (void)webView:(WKWebView*)view didFailProvisionalNavigation:(WKNavigation*)navigation
      withError:(NSError*)error {
    [self webView:view didFailNavigation:navigation withError:error];
}
- (void)webViewWebContentProcessDidTerminate:(WKWebView*)view {
    auto* resource = static_cast<MacWebViewResource*>(_resource);
    if (resource)
        emit_webview_text(NK_EVENT_WEBVIEW_PROCESS_TERMINATED, resource->handle,
                          @"WebKit content process terminated", NK_ERROR_UNKNOWN);
    (void)view;
}
- (void)userContentController:(WKUserContentController*)controller
      didReceiveScriptMessage:(WKScriptMessage*)message {
    (void)controller;
    auto* resource = static_cast<MacWebViewResource*>(_resource);
    if (!resource) return;
    NSString* value = [message.body isKindOfClass:[NSString class]]
        ? (NSString*)message.body : [message.body description];
    emit_webview_text(NK_EVENT_WEBVIEW_MESSAGE, resource->handle, value);
}
- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey,id>*)change
                       context:(void*)context {
    (void)change;
    (void)context;
    auto* resource = static_cast<MacWebViewResource*>(_resource);
    if (resource && [keyPath isEqualToString:@"title"])
        emit_webview_text(NK_EVENT_WEBVIEW_TITLE_CHANGED, resource->handle,
                          ((WKWebView*)object).title);
}
@end

namespace nk::backend {
void pump_events() noexcept {
    @autoreleasepool {
        NSEvent* event = nil;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:NSDate.distantPast
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES]))
            [NSApp sendEvent:event];
        [NSApp updateWindows];
    }
}

void shutdown() noexcept {
    std::vector<std::shared_ptr<DialogContext>> pending;
    {
        std::lock_guard lock(dialogs_mutex);
        for (const auto& item : dialogs) pending.push_back(item.second);
    }
    for (const auto& context : pending) cancel_dialog_context(context);
    pump_events();
    dialogs.clear();
    nk::core::handles().clear();
}
}

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_WINDOW | NK_CAP_FILE_DIALOG | NK_CAP_CLIPBOARD | NK_CAP_WEBVIEW |
           NK_CAP_DRAG_DROP | NK_CAP_SHELL |
           NK_CAP_SYSTEM_APPEARANCE | NK_CAP_EXPORT_NATIVE_WINDOW;
}

nk_result NK_CALL nk_window_create(const nk_window_options* options,
                                   nk_handle* out_window) {
    try {
        if (const auto result = enter_ui(); result != NK_OK) return result;
        if (!options || options->struct_size < sizeof(*options) || !out_window ||
            options->width <= 0 || options->height <= 0)
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid window options");
        if (!valid_utf8(options->title))
            return fail(NK_ERROR_INVALID_ARGUMENT, "window title is not valid UTF-8");
        *out_window = NK_INVALID_HANDLE;
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        auto resource = std::make_shared<MacWindowResource>();
        NSWindowStyleMask style = NSWindowStyleMaskTitled |
                                  NSWindowStyleMaskClosable |
                                  NSWindowStyleMaskMiniaturizable;
        if (options->flags & NK_WINDOW_RESIZABLE) style |= NSWindowStyleMaskResizable;
        resource->window = [[NSWindow alloc]
            initWithContentRect:NSMakeRect(0, 0, options->width, options->height)
                      styleMask:style backing:NSBackingStoreBuffered defer:NO];
        if (!resource->window) return fail(NK_ERROR_UNKNOWN, "could not create Cocoa window");
        resource->window.title = string(options->title) ?: @"";
        resource->window.releasedWhenClosed = NO;
        resource->content = [[NKContentView alloc]
            initWithFrame:NSMakeRect(0, 0, options->width, options->height)];
        resource->content.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        resource->content.resource = resource.get();
        resource->window.contentView = resource->content;
        resource->delegate = [NKWindowDelegate new];
        resource->delegate.resource = resource.get();
        resource->window.delegate = resource->delegate;
        resource->handle = nk::core::handles().insert(nk::core::ResourceType::window, resource);
        if (!resource->handle) return fail(NK_ERROR_OUT_OF_MEMORY, "window handle registry is full");
        [resource->window center];
        if (!(options->flags & NK_WINDOW_HIDDEN)) [resource->window makeKeyAndOrderFront:nil];
        *out_window = resource->handle;
        return NK_OK;
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while creating window");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while creating window");
    }
}

nk_result NK_CALL nk_window_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = window(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const auto children = resource->children;
    for (const auto child : children) nk_webview_destroy(child);
    cancel_dialogs_for_parent(resource->window);
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
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = window(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    visible ? [resource->window makeKeyAndOrderFront:nil] : [resource->window orderOut:nil];
    return NK_OK;
}

nk_result NK_CALL nk_window_set_title(nk_handle handle, const char* title) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = window(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    NSString* value = string(title);
    if (title && *title && !value) return fail(NK_ERROR_INVALID_ARGUMENT, "title is not valid UTF-8");
    resource->window.title = value ?: @"";
    return NK_OK;
}

nk_result NK_CALL nk_window_set_bounds(nk_handle handle, int32_t x, int32_t y,
                                       int32_t width, int32_t height) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (width <= 0 || height <= 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "window dimensions must be positive");
    auto resource = window(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    [resource->window setFrameOrigin:NSMakePoint(x, y)];
    [resource->window setContentSize:NSMakeSize(width, height)];
    return NK_OK;
}

nk_result NK_CALL nk_window_get_scale(nk_handle handle, float* out_scale) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (!out_scale) return fail(NK_ERROR_INVALID_ARGUMENT, "scale output must not be null");
    auto resource = window(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    *out_scale = static_cast<float>(resource->window.backingScaleFactor);
    return NK_OK;
}

nk_result NK_CALL nk_window_get_native(nk_handle handle, nk_native_window* out_native) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (!out_native || out_native->struct_size < sizeof(*out_native))
        return fail(NK_ERROR_INVALID_ARGUMENT, "native window output is missing or too small");
    auto resource = window(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const uint32_t size = out_native->struct_size;
    *out_native = {};
    out_native->struct_size = size;
    out_native->kind = NK_NATIVE_WINDOW_COCOA;
    out_native->window = reinterpret_cast<uintptr_t>((__bridge void*)resource->window);
    out_native->view = reinterpret_cast<uintptr_t>((__bridge void*)resource->content);
    return NK_OK;
}

nk_result NK_CALL nk_window_wrap_native(const nk_native_window*, nk_handle*) { return unsupported(); }
nk_result NK_CALL nk_webview_create(nk_handle parent_handle,
                                    const nk_webview_options* options,
                                    nk_handle* out_webview) {
    try {
        if (const auto result = enter_ui(); result != NK_OK) return result;
        if (!options || options->struct_size < sizeof(*options) || !out_webview ||
            options->width <= 0 || options->height <= 0)
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid WebView options");
        if (!valid_utf8(options->initial_url))
            return fail(NK_ERROR_INVALID_ARGUMENT, "initial URL is not valid UTF-8");
        *out_webview = NK_INVALID_HANDLE;
        auto parent = window(parent_handle);
        if (!parent) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
        parent->children.reserve(parent->children.size() + 1);
        NSURL* initial_url = nil;
        if (options->initial_url) {
            initial_url = [NSURL URLWithString:string(options->initial_url)];
            if (!initial_url) return fail(NK_ERROR_INVALID_ARGUMENT, "initial URL is invalid");
        }

        auto resource = std::make_shared<MacWebViewResource>();
        resource->parent = parent_handle;
        resource->content_controller = [WKUserContentController new];
        resource->delegate = [NKWebViewDelegate new];
        [resource->content_controller addScriptMessageHandler:resource->delegate
                                                         name:@"nativekit"];
        WKWebViewConfiguration* configuration = [WKWebViewConfiguration new];
        configuration.userContentController = resource->content_controller;
        resource->view = [[WKWebView alloc]
            initWithFrame:NSMakeRect(options->x, options->y, options->width, options->height)
            configuration:configuration];
        if (!resource->view) return fail(NK_ERROR_UNKNOWN, "could not create WKWebView");
        resource->view.autoresizingMask = NSViewMaxXMargin | NSViewMaxYMargin;
        resource->view.navigationDelegate = resource->delegate;
        resource->handle = nk::core::handles().insert(
            nk::core::ResourceType::webview, resource);
        if (!resource->handle)
            return fail(NK_ERROR_OUT_OF_MEMORY, "WebView handle registry is full");
        resource->delegate.resource = resource.get();
        [resource->view addObserver:resource->delegate forKeyPath:@"title"
                           options:NSKeyValueObservingOptionNew context:nullptr];
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
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while creating WebView");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while creating WebView");
    }
}

nk_result NK_CALL nk_webview_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = webview(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    auto parent = window(resource->parent);
    if (parent) {
        auto& children = parent->children;
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
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = webview(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    resource->view.hidden = !visible;
    return NK_OK;
}

nk_result NK_CALL nk_webview_set_bounds(nk_handle handle, int32_t x, int32_t y,
                                        int32_t width, int32_t height) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (width <= 0 || height <= 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "WebView dimensions must be positive");
    auto resource = webview(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    resource->view.frame = NSMakeRect(x, y, width, height);
    return NK_OK;
}

nk_result NK_CALL nk_webview_navigate(nk_handle handle, const char* url) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = webview(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    NSString* value = string(url);
    NSURL* target = value ? [NSURL URLWithString:value] : nil;
    if (!target) return fail(NK_ERROR_INVALID_ARGUMENT, "URL is null, invalid UTF-8, or malformed");
    [resource->view loadRequest:[NSURLRequest requestWithURL:target]];
    return NK_OK;
}

nk_result NK_CALL nk_webview_set_html(nk_handle handle, const char* html,
                                      const char* base_url) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = webview(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
    NSString* document = string(html);
    NSString* base = string(base_url);
    if (!document || (base_url && !base))
        return fail(NK_ERROR_INVALID_ARGUMENT, "HTML or base URL is invalid UTF-8");
    NSURL* base_target = base.length ? [NSURL URLWithString:base] : nil;
    if (base.length && !base_target)
        return fail(NK_ERROR_INVALID_ARGUMENT, "base URL is malformed");
    [resource->view loadHTMLString:document baseURL:base_target];
    return NK_OK;
}

nk_result NK_CALL nk_webview_eval(nk_handle handle, const char* script,
                                  nk_request_id* out_request) {
    try {
        if (const auto result = enter_ui(); result != NK_OK) return result;
        if (!out_request) return fail(NK_ERROR_INVALID_ARGUMENT, "evaluation request output is null");
        *out_request = NK_INVALID_REQUEST_ID;
        auto resource = webview(handle);
        if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale WebView handle");
        NSString* source = string(script);
        if (!source) return fail(NK_ERROR_INVALID_ARGUMENT, "script is null or invalid UTF-8");
        const auto request = nk::core::next_request_id();
        [resource->view evaluateJavaScript:source completionHandler:^(id value, NSError* error) {
            if (error)
                emit_webview_text(NK_EVENT_WEBVIEW_EVAL_COMPLETE, handle,
                                  error.localizedDescription, NK_ERROR_UNKNOWN, 0, request);
            else
                emit_webview_text(NK_EVENT_WEBVIEW_EVAL_COMPLETE, handle,
                                  value ? [value description] : @"", NK_OK, 0, request);
        }];
        *out_request = request;
        return NK_OK;
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while evaluating JavaScript");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while evaluating JavaScript");
    }
}

nk_result NK_CALL nk_dialog_open_file(nk_handle parent,
                                      const nk_file_dialog_options* options,
                                      nk_request_id* request) {
    try { return start_file_dialog(parent, options, request, NK_DIALOG_OPEN_FILE); }
    catch (...) { return fail(NK_ERROR_OUT_OF_MEMORY, "could not start open dialog"); }
}
nk_result NK_CALL nk_dialog_save_file(nk_handle parent,
                                      const nk_file_dialog_options* options,
                                      nk_request_id* request) {
    try { return start_file_dialog(parent, options, request, NK_DIALOG_SAVE_FILE); }
    catch (...) { return fail(NK_ERROR_OUT_OF_MEMORY, "could not start save dialog"); }
}
nk_result NK_CALL nk_dialog_select_directory(nk_handle parent,
                                             const nk_file_dialog_options* options,
                                             nk_request_id* request) {
    try { return start_file_dialog(parent, options, request, NK_DIALOG_SELECT_DIRECTORY); }
    catch (...) { return fail(NK_ERROR_OUT_OF_MEMORY, "could not start directory dialog"); }
}

nk_result NK_CALL nk_dialog_message(nk_handle parent_handle,
                                    const nk_message_dialog_options* options,
                                    nk_request_id* out_request) {
    try {
        if (const auto result = enter_ui(); result != NK_OK) return result;
        if (!options || options->struct_size < sizeof(*options) || !options->message || !out_request)
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid message dialog options");
        if (!valid_utf8(options->title) || !valid_utf8(options->message))
            return fail(NK_ERROR_INVALID_ARGUMENT, "message dialog option is not valid UTF-8");
        auto parent = parent_handle ? window(parent_handle) : nullptr;
        if (parent_handle && !parent)
            return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
        auto context = std::make_shared<DialogContext>();
        context->request = nk::core::next_request_id();
        context->kind = NK_DIALOG_MESSAGE;
        context->parent = parent ? parent->window : nil;
        NSAlert* alert = [NSAlert new];
        alert.messageText = string(options->title) ?: @"";
        alert.informativeText = string(options->message) ?: @"";
        if (options->kind == NK_MESSAGE_WARNING) alert.alertStyle = NSAlertStyleWarning;
        else if (options->kind == NK_MESSAGE_ERROR) alert.alertStyle = NSAlertStyleCritical;
        else alert.alertStyle = NSAlertStyleInformational;
        auto add_button = [&](uint32_t flag, NSString* label, uint32_t result) {
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
            [alert beginSheetModalForWindow:parent->window completionHandler:^(NSModalResponse response) {
                finish_message_dialog(request, response);
            }];
        } else {
            dispatch_async(dispatch_get_main_queue(), ^{
                finish_message_dialog(request, [alert runModal]);
            });
        }
        *out_request = request;
        return NK_OK;
    } catch (...) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "could not start message dialog");
    }
}

nk_result NK_CALL nk_dialog_cancel(nk_request_id request) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
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

nk_result NK_CALL nk_shell_open_url(const char* url) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    NSString* value = string(url);
    NSURL* native = value ? [NSURL URLWithString:value] : nil;
    if (!native || !native.scheme.length)
        return fail(NK_ERROR_INVALID_ARGUMENT, "URL must contain a valid URI scheme");
    return [NSWorkspace.sharedWorkspace openURL:native] ? NK_OK
        : fail(NK_ERROR_UNKNOWN, "macOS could not open URL");
}

nk_result NK_CALL nk_shell_open_file(const char* path) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    NSString* value = string(path);
    if (!value.length) return fail(NK_ERROR_INVALID_ARGUMENT, "path must not be empty");
    return [NSWorkspace.sharedWorkspace openURL:[NSURL fileURLWithPath:value]] ? NK_OK
        : fail(NK_ERROR_UNKNOWN, "macOS could not open file");
}

nk_result NK_CALL nk_shell_reveal_file(const char* path) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    NSString* value = string(path);
    if (!value.length) return fail(NK_ERROR_INVALID_ARGUMENT, "path must not be empty");
    [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:
        @[[NSURL fileURLWithPath:value]]];
    return NK_OK;
}

nk_result NK_CALL nk_system_directory(nk_system_directory_kind kind,
                                      char* buffer, uint32_t* inout_size) {
    nk::core::clear_error();
    NSString* path = nil;
    if (kind == NK_DIRECTORY_HOME) path = NSHomeDirectory();
    else if (kind == NK_DIRECTORY_TEMP) path = NSTemporaryDirectory();
    else path = directory_url(kind).path;
    if (!path) return fail(NK_ERROR_UNSUPPORTED, "system directory is unavailable");
    return copy_output(path, buffer, inout_size);
}

nk_result NK_CALL nk_system_locale(char* buffer, uint32_t* inout_size) {
    nk::core::clear_error();
    return copy_output(NSLocale.currentLocale.localeIdentifier, buffer, inout_size);
}

nk_result NK_CALL nk_system_get_appearance(nk_system_appearance* appearance) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (!appearance || appearance->struct_size < sizeof(*appearance))
        return fail(NK_ERROR_INVALID_ARGUMENT, "appearance output is missing or too small");
    const uint32_t size = appearance->struct_size;
    *appearance = {};
    appearance->struct_size = size;
    NSAppearanceName match = [NSApp.effectiveAppearance
        bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
    appearance->color_scheme = [match isEqualToString:NSAppearanceNameDarkAqua]
        ? NK_COLOR_SCHEME_DARK : NK_COLOR_SCHEME_LIGHT;
    appearance->high_contrast = NSWorkspace.sharedWorkspace.accessibilityDisplayShouldIncreaseContrast;
    return NK_OK;
}

nk_result NK_CALL nk_clipboard_set_text(const char* text) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    if (!text) return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard text must not be null");
    NSString* value = string(text);
    if (!value) return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard text is not valid UTF-8");
    NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
    [pasteboard clearContents];
    return [pasteboard setString:value forType:NSPasteboardTypeString] ? NK_OK
        : fail(NK_ERROR_UNKNOWN, "macOS rejected clipboard text");
}

nk_result NK_CALL nk_clipboard_set_files(const char* const* paths, uint32_t path_count) {
    try {
        if (const auto result = enter_ui(); result != NK_OK) return result;
        if (!paths || !path_count)
            return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard file list must not be empty");
        NSMutableArray<NSURL*>* urls = [NSMutableArray arrayWithCapacity:path_count];
        for (uint32_t index = 0; index < path_count; ++index) {
            NSString* path = string(paths[index]);
            if (!path.length)
                return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard path is empty or invalid UTF-8");
            if (![path isAbsolutePath])
                path = [NSFileManager.defaultManager.currentDirectoryPath
                    stringByAppendingPathComponent:path];
            [urls addObject:[NSURL fileURLWithPath:path.stringByStandardizingPath]];
        }
        NSPasteboard* pasteboard = NSPasteboard.generalPasteboard;
        [pasteboard clearContents];
        return [pasteboard writeObjects:urls] ? NK_OK
            : fail(NK_ERROR_UNKNOWN, "macOS rejected clipboard files");
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while writing clipboard files");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while writing clipboard files");
    }
}

nk_result NK_CALL nk_clipboard_read_text(nk_request_id* out_request) {
    try {
        if (const auto result = enter_ui(); result != NK_OK) return result;
        if (!out_request) return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard request output is null");
        *out_request = NK_INVALID_REQUEST_ID;
        const auto request = nk::core::next_request_id();
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_CLIPBOARD_TEXT_COMPLETE;
        event.request_id = request;
        event.data = text_bytes(utf8(
            [NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString]));
        const auto result = nk::core::push_event(std::move(event));
        if (result != NK_OK) return fail(result, "could not queue clipboard text result");
        *out_request = request;
        return NK_OK;
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while reading clipboard text");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while reading clipboard text");
    }
}

nk_result NK_CALL nk_clipboard_read_files(nk_request_id* out_request) {
    try {
        if (const auto result = enter_ui(); result != NK_OK) return result;
        if (!out_request) return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard request output is null");
        *out_request = NK_INVALID_REQUEST_ID;
        std::vector<std::string> paths;
        for (NSURL* url in pasteboard_file_urls(NSPasteboard.generalPasteboard)) {
            const bool scoped = [url startAccessingSecurityScopedResource];
            paths.push_back(utf8(url.path));
            if (scoped) [url stopAccessingSecurityScopedResource];
        }
        const auto request = nk::core::next_request_id();
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_CLIPBOARD_FILES_COMPLETE;
        event.request_id = request;
        event.data_count = static_cast<uint32_t>(paths.size());
        nk_clipboard_files header{static_cast<uint32_t>(paths.size()), 0};
        event.data = string_list_payload(header, paths, &nk_clipboard_files::strings_offset);
        const auto result = nk::core::push_event(std::move(event));
        if (result != NK_OK) return fail(result, "could not queue clipboard file result");
        *out_request = request;
        return NK_OK;
    } catch (const std::bad_alloc&) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while reading clipboard files");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while reading clipboard files");
    }
}

nk_result NK_CALL nk_window_set_drop_enabled(nk_handle handle, uint32_t enabled) {
    if (const auto result = enter_ui(); result != NK_OK) return result;
    auto resource = window(handle);
    if (!resource) return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    if (enabled)
        [resource->content registerForDraggedTypes:@[NSPasteboardTypeFileURL,
                                                     NSPasteboardTypeString]];
    else
        [resource->content unregisterDraggedTypes];
    return NK_OK;
}

}
