#import <Cocoa/Cocoa.h>
#import <Carbon/Carbon.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UserNotifications/UserNotifications.h>
#import <WebKit/WebKit.h>

#include "nativekit_clipboard.h"
#include "nativekit_accessibility.h"
#include "nativekit_dialog.h"
#include "nativekit_graphics.h"
#include "nativekit_input.h"
#include "nativekit_monitor.h"
#include "nativekit_notification.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/graphics_frame_target.hpp"
#include "core/graphics_image_registry.h"
#include "core/runtime.hpp"
#include "core/system_internal.hpp"
#include "platform/resource_events.hpp"
#include "macos/joystick.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

@interface NKWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) void *resource;
@end

@interface NKContentView : NSView <NSDraggingDestination, NSTextInputClient>
@property(nonatomic, assign) void *resource;
@end

@interface NKMetalSurfaceView : NSView
@end

@interface NKMacAccessibilityElement : NSAccessibilityElement
@property(nonatomic, assign) nk_handle surface;
@property(nonatomic, assign) nk_accessibility_node_id node;
@property(nonatomic, weak) id nativeParent;
@property(nonatomic, weak) id nativeContainer;
@property(nonatomic, strong) NSArray<NKMacAccessibilityElement *> *nativeChildren;
@property(nonatomic, assign) NSRect nativeFrame;
@end

@interface NKMacAccessibilityContainer : NSView
@property(nonatomic, assign) nk_handle surface;
@property(nonatomic, strong) NSArray<NKMacAccessibilityElement *> *elements;
@property(nonatomic, strong) NSArray<NKMacAccessibilityElement *> *allElements;
@end

@interface NKWebViewDelegate : NSObject <WKNavigationDelegate, WKScriptMessageHandler>
@property(nonatomic, assign) void *resource;
@end

@interface NKNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

NSString *const NKMacAccessibilityHeadingRole = @"AXHeading";
NSString *const NKMacAccessibilityFrameAttribute = @"AXFrame";
NSString *const NKMacAccessibilityScrollToVisibleAction = @"AXScrollToVisible";

namespace {

struct MacCursorResource;
struct MacSurfaceResource;

struct MacAccessibilityTextRange {
    nk_accessibility_text_position start = 0;
    nk_accessibility_text_position end = 0;
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

struct MacAccessibilityNode {
    nk_accessibility_node_id id = NK_ACCESSIBILITY_ROOT;
    nk_accessibility_node_id parent = NK_ACCESSIBILITY_ROOT;
    uint32_t child_index = 0;
    nk_accessibility_role role = NK_ACCESSIBILITY_GROUP;
    nk_accessibility_states states = 0;
    nk_accessibility_actions actions = 0;
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
    std::string label;
    std::string value;
    double numeric_value = 0;
    double numeric_minimum = 0;
    double numeric_maximum = 0;
    nk_accessibility_text_position text_start = 0;
    nk_accessibility_text_position document_length = 0;
    nk_accessibility_text_position selection_start = NK_ACCESSIBILITY_TEXT_POSITION_NONE;
    nk_accessibility_text_position selection_end = NK_ACCESSIBILITY_TEXT_POSITION_NONE;
    uint32_t set_size = 0;
    uint32_t position_in_set = 0;
    uint32_t row_count = 0;
    uint32_t column_count = 0;
    uint32_t row_index = NK_ACCESSIBILITY_INDEX_NONE;
    uint32_t column_index = NK_ACCESSIBILITY_INDEX_NONE;
    uint32_t row_span = 0;
    uint32_t column_span = 0;
    uint32_t hierarchy_level = 0;
    nk_accessibility_orientation orientation = NK_ACCESSIBILITY_ORIENTATION_UNSPECIFIED;
    std::vector<MacAccessibilityTextRange> text_ranges;
};

void refresh_mac_accessibility_elements(MacSurfaceResource &resource) noexcept;

struct MacWindowResource final : nk::core::Resource {
    __strong NSWindow *window = nil;
    __strong NKContentView *content = nil;
    __strong NSView *native_content = nil;
    __strong NKWindowDelegate *delegate = nil;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle owner = NK_INVALID_HANDLE;
    bool modal = false;
    bool owns_window = true;
    bool sheet_active = false;
    bool child_attached = false;
    bool fullscreen_target = false;
    bool fullscreen_transitioning = false;
    bool fullscreen_reposition_pending = false;
    bool fullscreen_reenter_after_reposition = false;
    NSPoint fullscreen_target_origin = NSZeroPoint;
    std::array<nk_input_action, NK_KEY_LAST + 1> keys{};
    std::array<nk_input_action, NK_POINTER_BUTTON_LAST + 1> pointer_buttons{};
    double pointer_x = 0.0;
    double pointer_y = 0.0;
    bool hovered = false;
    nk_cursor_mode cursor_mode = NK_CURSOR_MODE_NORMAL;
    std::shared_ptr<MacCursorResource> cursor;
    bool pointer_captured = false;
    bool cursor_hidden = false;
    std::string text_input_text;
    nk_text_input_state text_input_state{};
    bool text_input_active = false;
    bool text_composing = false;
    nk_text_position text_composition_start = NK_TEXT_POSITION_NONE;
    nk_text_position text_composition_end = NK_TEXT_POSITION_NONE;
    std::string marked_text;
    NSRange marked_native_range{NSNotFound, 0};
    NSRange selected_native_range{0, 0};
    uint32_t next_touch_pointer_id = 1;
    std::unordered_map<NSUInteger, uint32_t> touch_pointers;
    std::unordered_map<uint64_t, uint32_t> tablet_pointers;
    std::vector<nk_handle> children;
    std::vector<nk_handle> surfaces;
    std::vector<nk_handle> owned_windows;
    ~MacWindowResource() override {
        if (pointer_captured) {
            CGAssociateMouseAndMouseCursorPosition(true);
            pointer_captured = false;
        }
        if (cursor_hidden) {
            [NSCursor unhide];
            cursor_hidden = false;
        }
        if (window && owns_window) {
            content.resource = nullptr;
            window.delegate = nil;
            [window orderOut:nil];
            [window close];
        }
    }
};

void request_fullscreen_transition(MacWindowResource &resource, bool enabled) {
    if (!resource.window)
        return;
    resource.fullscreen_target = enabled;
    if (resource.fullscreen_transitioning)
        return;
    const bool current = (resource.window.styleMask & NSWindowStyleMaskFullScreen) != 0;
    if (current == enabled)
        return;
    resource.fullscreen_transitioning = true;
    [resource.window toggleFullScreen:nil];
}

void finish_fullscreen_transition(MacWindowResource &resource) {
    resource.fullscreen_transitioning = false;
    const bool current = (resource.window.styleMask & NSWindowStyleMaskFullScreen) != 0;
    if (!current && resource.fullscreen_reposition_pending) {
        [resource.window setFrameOrigin:resource.fullscreen_target_origin];
        resource.fullscreen_reposition_pending = false;
        if (resource.fullscreen_reenter_after_reposition) {
            resource.fullscreen_reenter_after_reposition = false;
            request_fullscreen_transition(resource, true);
            return;
        }
    } else if (current && resource.fullscreen_reposition_pending) {
        resource.fullscreen_reposition_pending = false;
    }
    if (current != resource.fullscreen_target)
        request_fullscreen_transition(resource, resource.fullscreen_target);
}

struct MacMonitorResource final : nk::core::Resource {
    CGDirectDisplayID display = kCGNullDirectDisplay;
    std::string name;
    nk_handle handle = NK_INVALID_HANDLE;
};

std::unordered_map<CGDirectDisplayID, nk_handle> monitor_handles;
std::unordered_map<CGDirectDisplayID, nk_orientation> monitor_orientations;

void poll_monitor_orientations();

NSView *window_content_view(const MacWindowResource &resource) {
    return resource.owns_window ? resource.content : resource.native_content;
}

struct MacSurfaceResource final : nk::core::Resource {
    __strong NKMetalSurfaceView *view = nil;
    __strong NKMacAccessibilityContainer *accessibility_container = nil;
    __strong CAMetalLayer *layer = nil;
    __strong id<MTLDevice> device = nil;
    __strong id<MTLCommandQueue> queue = nil;
    __strong id<CAMetalDrawable> drawable = nil;
    __strong id<MTLTexture> depth_stencil = nil;
    __strong NSTimer *frame_timer = nil;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle parent = NK_INVALID_HANDLE;
    nk_handle device_handle = NK_INVALID_HANDLE;
    nk_graphics_api api = NK_GRAPHICS_METAL;
    nk_surface_flags flags = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t framebuffer_width = 0;
    int32_t framebuffer_height = 0;
    std::shared_ptr<MacSurfaceResource> shared_surface;
    uint32_t share_dependents = 0;
    nk_surface_frame_callback frame_callback = nullptr;
    void *frame_user_data = nullptr;
    bool frame_prepared = false;
    bool ready = false;
    bool lost_reported = false;
    bool destroying = false;
    std::unordered_map<nk_accessibility_node_id, MacAccessibilityNode> accessibility_nodes;
    nk_accessibility_node_id accessibility_focus = NK_ACCESSIBILITY_ROOT;

    ~MacSurfaceResource() override {
        [frame_timer invalidate];
        drawable = nil;
        accessibility_container.surface = NK_INVALID_HANDLE;
        [accessibility_container removeFromSuperview];
        accessibility_container = nil;
        if (view)
            [view removeFromSuperview];
    }
};

struct MacCursorResource final : nk::core::Resource {
    __strong NSCursor *cursor = nil;
    nk_handle handle = NK_INVALID_HANDLE;
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
__strong NSMutableSet<NSSharingServicePicker *> *sharing_pickers = nil;
__strong NSMutableDictionary<NSString *, NSURL *> *security_scoped_urls = nil;
bool notification_center_initialized = false;

void retain_security_scope(NSURL *url) {
    if (!url.fileURL || !url.absoluteString.length)
        return;
    if (!security_scoped_urls)
        security_scoped_urls = [NSMutableDictionary dictionary];
    NSString *key = url.absoluteString;
    if (security_scoped_urls[key])
        return;
    if ([url startAccessingSecurityScopedResource])
        security_scoped_urls[key] = url;
}

void release_security_scopes() {
    for (NSURL *url in security_scoped_urls.allValues)
        [url stopAccessingSecurityScopedResource];
    [security_scoped_urls removeAllObjects];
    security_scoped_urls = nil;
}

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

NSArray<NSURL *> *pasteboard_resource_urls(NSPasteboard *pasteboard) {
    NSDictionary *options = @{NSPasteboardURLReadingFileURLsOnlyKey : @NO};
    NSArray *values = [pasteboard readObjectsForClasses:@[ [NSURL class] ] options:options];
    NSMutableArray<NSURL *> *resources = [NSMutableArray array];
    for (id value in values)
        if ([value isKindOfClass:[NSURL class]] && ((NSURL *)value).scheme.length)
            [resources addObject:value];
    if (!resources.count) {
        NSString *url_text = [pasteboard stringForType:NSPasteboardTypeURL];
        NSURL *url = url_text.length ? [NSURL URLWithString:url_text] : nil;
        if (url.scheme.length)
            [resources addObject:url];
    }
    return resources;
}

bool emit_drop(MacWindowResource &resource, id<NSDraggingInfo> information) noexcept {
    return nk::core::callback_boundary_or(false, [&]() -> bool {
        NSPasteboard *pasteboard = information.draggingPasteboard;
        NSArray<NSURL *> *urls = pasteboard_file_urls(pasteboard);
        std::vector<std::string> items;
        std::vector<nk::platform::ResourceValue> resources;
        nk_event_kind kind = NK_EVENT_DROP_FILES;
        if (urls.count) {
            for (NSURL *url in urls) {
                const bool scoped = [url startAccessingSecurityScopedResource];
                const auto path = utf8(url.path);
                items.push_back(path);
                resources.push_back(nk::platform::resource_from_uri(
                    utf8(url.absoluteString), NK_RESOURCE_READABLE, {},
                    url.lastPathComponent ? utf8(url.lastPathComponent) : std::string{}));
                if (scoped)
                    [url stopAccessingSecurityScopedResource];
            }
        } else {
            NSArray<NSURL *> *resource_urls = pasteboard_resource_urls(pasteboard);
            if (resource_urls.count) {
                for (NSURL *url in resource_urls) {
                    const bool scoped = [url startAccessingSecurityScopedResource];
                    if (url.fileURL)
                        items.push_back(utf8(url.path));
                    resources.push_back(nk::platform::resource_from_uri(
                        utf8(url.absoluteString), NK_RESOURCE_READABLE, {},
                        url.lastPathComponent ? utf8(url.lastPathComponent) : std::string{}));
                    if (scoped)
                        [url stopAccessingSecurityScopedResource];
                }
                if (items.empty())
                    kind = NK_EVENT_DROP_FILES;
            } else {
                NSString *text = [pasteboard stringForType:NSPasteboardTypeString];
                if (!text)
                    return false;
                kind = NK_EVENT_DROP_TEXT;
                items.push_back(utf8(text));
            }
        }
        NSView *content_view = window_content_view(resource);
        const NSPoint point = [content_view convertPoint:information.draggingLocation fromView:nil];
        if (!items.empty()) {
            nk::core::QueuedEvent event;
            event.kind = kind;
            event.source = resource.handle;
            event.data_count = static_cast<uint32_t>(items.size());
            nk_drop_data header{static_cast<int32_t>(point.x), static_cast<int32_t>(point.y),
                                static_cast<uint32_t>(items.size()), 0};
            event.data = string_list_payload(header, items, &nk_drop_data::strings_offset);
            if (nk::core::push_event(std::move(event)) != NK_OK)
                return false;
        }
        if (!resources.empty()) {
            nk::core::QueuedEvent resource_event;
            resource_event.kind = NK_EVENT_RESOURCE_DROP;
            resource_event.source = resource.handle;
            resource_event.data_count = static_cast<uint32_t>(resources.size());
            resource_event.data = nk::platform::resource_drop_payload(
                static_cast<float>(point.x), static_cast<float>(point.y), {}, resources);
            if (nk::core::push_event(std::move(resource_event)) != NK_OK)
                return false;
        }
        return true;
    });
}

std::shared_ptr<MacWindowResource> window(nk_handle handle) {
    return std::dynamic_pointer_cast<MacWindowResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::window));
}

std::shared_ptr<MacSurfaceResource> surface(nk_handle handle) {
    return std::dynamic_pointer_cast<MacSurfaceResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::surface));
}

std::shared_ptr<MacMonitorResource> monitor(nk_handle handle) {
    return std::dynamic_pointer_cast<MacMonitorResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::monitor));
}

CGDirectDisplayID display_id(NSScreen *screen) {
    NSNumber *number = screen.deviceDescription[NSDeviceDescriptionKey(@"NSScreenNumber")];
    return number ? static_cast<CGDirectDisplayID>(number.unsignedIntValue) : kCGNullDirectDisplay;
}

NSScreen *screen_for_display(CGDirectDisplayID display) {
    for (NSScreen *screen in NSScreen.screens)
        if (display_id(screen) == display)
            return screen;
    return nil;
}

std::string monitor_name(NSScreen *screen, CGDirectDisplayID display) {
    NSString *name = screen.localizedName;
    if (name.length)
        return utf8(name);
    return "Display " + std::to_string(static_cast<unsigned int>(display));
}

nk_handle register_monitor(NSScreen *screen, CGDirectDisplayID display) {
    const auto found = monitor_handles.find(display);
    if (found != monitor_handles.end())
        return found->second;
    auto resource = std::make_shared<MacMonitorResource>();
    resource->display = display;
    resource->name = monitor_name(screen, display);
    resource->handle = nk::core::handles().insert(nk::core::ResourceType::monitor, resource);
    if (resource->handle != NK_INVALID_HANDLE) {
        monitor_handles.emplace(display, resource->handle);
        monitor_orientations.emplace(display, NK_ORIENTATION_UNKNOWN);
    }
    return resource->handle;
}

nk_orientation monitor_orientation(CGDirectDisplayID display) {
    const auto rotation = static_cast<int>(std::lround(CGDisplayRotation(display)));
    if (rotation == 90)
        return NK_ORIENTATION_PORTRAIT;
    if (rotation == 180)
        return NK_ORIENTATION_LANDSCAPE_LEFT;
    if (rotation == 270)
        return NK_ORIENTATION_PORTRAIT_UPSIDE_DOWN;
    const auto width = CGDisplayPixelsWide(display);
    const auto height = CGDisplayPixelsHigh(display);
    return width == height  ? NK_ORIENTATION_UNKNOWN
           : width > height ? NK_ORIENTATION_LANDSCAPE_RIGHT
                            : NK_ORIENTATION_PORTRAIT;
}

void poll_monitor_orientations() {
    nk::core::callback_boundary([] {
        for (const auto &[display, handle] : monitor_handles) {
            const auto current = monitor_orientation(display);
            const auto found = monitor_orientations.find(display);
            if (found == monitor_orientations.end()) {
                monitor_orientations.emplace(display, current);
                continue;
            }
            if (found->second == current)
                continue;
            found->second = current;
            const nk_orientation_event payload{sizeof(payload), current, 0, {0, 0}};
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_DISPLAY_ORIENTATION_CHANGED;
            event.source = handle;
            event.data = bytes_of(payload);
            nk::core::push_event(std::move(event));
        }
    });
}

nk_result refresh_monitors() {
    const bool had_monitors = !monitor_handles.empty();
    std::unordered_set<CGDirectDisplayID> current;
    for (NSScreen *screen in NSScreen.screens) {
        const CGDirectDisplayID display = display_id(screen);
        if (display == kCGNullDirectDisplay)
            continue;
        current.insert(display);
        if (monitor_handles.find(display) != monitor_handles.end())
            continue;
        const nk_handle handle = register_monitor(screen, display);
        if (handle == NK_INVALID_HANDLE)
            return fail(NK_ERROR_OUT_OF_MEMORY, "monitor handle registry is full");
        if (had_monitors) {
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_MONITOR_CONNECTED;
            event.source = handle;
            nk::core::push_event(std::move(event));
        }
    }
    for (auto iterator = monitor_handles.begin(); iterator != monitor_handles.end();) {
        if (current.find(iterator->first) != current.end()) {
            ++iterator;
            continue;
        }
        const nk_handle handle = iterator->second;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_MONITOR_DISCONNECTED;
        event.source = handle;
        nk::core::push_event(std::move(event));
        nk::core::handles().erase(handle, nk::core::ResourceType::monitor);
        monitor_orientations.erase(iterator->first);
        iterator = monitor_handles.erase(iterator);
    }
    return NK_OK;
}

nk_video_mode make_video_mode(CGDisplayModeRef native) {
    nk_video_mode result{};
    result.struct_size = sizeof(result);
    result.width = static_cast<int32_t>(CGDisplayModeGetPixelWidth(native));
    result.height = static_cast<int32_t>(CGDisplayModeGetPixelHeight(native));
    result.refresh_rate = CGDisplayModeGetRefreshRate(native);
    return result;
}

uint64_t metal_object_token(id object) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>((__bridge void *)object));
}

void emit_surface_lost(MacSurfaceResource &resource) {
    if (resource.lost_reported || resource.destroying)
        return;
    resource.lost_reported = true;
    resource.frame_prepared = false;
    resource.drawable = nil;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_SURFACE_LOST;
    event.source = resource.handle;
    nk::core::push_event(std::move(event));
}

void emit_surface_resize(MacSurfaceResource &resource) {
    const nk_surface_resize_event payload{resource.width, resource.height,
                                          resource.framebuffer_width, resource.framebuffer_height};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_SURFACE_RESIZE;
    event.source = resource.handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

void sync_surface_drawable_size(MacSurfaceResource &resource) {
    if (!resource.view || !resource.layer)
        return;
    const CGFloat scale = resource.view.window ? resource.view.window.backingScaleFactor : 1.0;
    const NSSize view_size = resource.view.bounds.size;
    resource.layer.contentsScale = scale > 0.0 ? scale : 1.0;
    resource.layer.drawableSize =
        CGSizeMake(std::max(0.0, view_size.width * scale), std::max(0.0, view_size.height * scale));
    const int32_t width = static_cast<int32_t>(resource.layer.drawableSize.width);
    const int32_t height = static_cast<int32_t>(resource.layer.drawableSize.height);
    if (resource.framebuffer_width == width && resource.framebuffer_height == height)
        return;
    const bool changed = resource.framebuffer_width != 0 || resource.framebuffer_height != 0;
    resource.framebuffer_width = width;
    resource.framebuffer_height = height;
    resource.depth_stencil = nil;
    resource.drawable = nil;
    resource.frame_prepared = false;
    if (resource.ready && changed)
        emit_surface_resize(resource);
}

bool set_surface_native_bounds(MacSurfaceResource &resource) {
    if (!resource.view)
        return false;
    [resource.view setFrame:NSMakeRect(resource.x, resource.y, resource.width, resource.height)];
    if (resource.accessibility_container)
        [resource.accessibility_container
            setFrame:NSMakeRect(resource.x, resource.y, resource.width, resource.height)];
    sync_surface_drawable_size(resource);
    return true;
}

void update_window_surfaces(MacWindowResource &resource) {
    for (const nk_handle handle : resource.surfaces)
        if (auto child = surface(handle))
            sync_surface_drawable_size(*child);
}

bool surface_frame_available(const MacSurfaceResource &resource) {
    auto parent = window(resource.parent);
    return resource.view && resource.layer && resource.device && resource.queue && parent &&
           !parent->window.miniaturized && parent->window.visible &&
           (parent->window.occlusionState & NSWindowOcclusionStateVisible) != 0 &&
           !resource.view.hidden && resource.framebuffer_width > 0 &&
           resource.framebuffer_height > 0;
}

bool ensure_surface_depth_target(MacSurfaceResource &resource, int32_t width, int32_t height) {
    if (!(resource.flags & (NK_SURFACE_DEPTH | NK_SURFACE_STENCIL))) {
        resource.depth_stencil = nil;
        return true;
    }
    if (resource.depth_stencil && resource.depth_stencil.width == static_cast<NSUInteger>(width) &&
        resource.depth_stencil.height == static_cast<NSUInteger>(height))
        return true;
    const MTLPixelFormat format = MTLPixelFormatDepth32Float_Stencil8;
    MTLTextureDescriptor *descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:format
                                                           width:static_cast<NSUInteger>(width)
                                                          height:static_cast<NSUInteger>(height)
                                                       mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget;
    descriptor.storageMode = MTLStorageModePrivate;
    resource.depth_stencil = [resource.device newTextureWithDescriptor:descriptor];
    if (!resource.depth_stencil) {
        if (resource.ready)
            emit_surface_lost(resource);
        nk::core::set_error("could not allocate the Metal depth/stencil target");
        return false;
    }
    return true;
}

std::shared_ptr<MacCursorResource> cursor(nk_handle handle) {
    return std::dynamic_pointer_cast<MacCursorResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::cursor));
}

void queue_input_event(nk_event_kind kind, nk_handle source, std::vector<std::byte> data,
                       uint32_t flags = 0) {
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = source;
    event.flags = flags;
    event.data = std::move(data);
    nk::core::push_event(std::move(event));
}

bool decode_utf8(std::string_view text, std::vector<uint32_t> &codepoints) {
    codepoints.clear();
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<uint8_t>(text[index]);
        uint32_t value = 0;
        std::size_t count = 0;
        if (first < 0x80) {
            value = first;
            count = 1;
        } else if (first >= 0xc2 && first <= 0xdf) {
            value = first & 0x1fu;
            count = 2;
        } else if (first >= 0xe0 && first <= 0xef) {
            value = first & 0x0fu;
            count = 3;
        } else if (first >= 0xf0 && first <= 0xf4) {
            value = first & 0x07u;
            count = 4;
        } else {
            return false;
        }
        if (index + count > text.size())
            return false;
        for (std::size_t part = 1; part < count; ++part) {
            const auto next = static_cast<uint8_t>(text[index + part]);
            if ((next & 0xc0u) != 0x80u)
                return false;
            value = (value << 6) | (next & 0x3fu);
        }
        if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) ||
            (count == 4 && value < 0x10000) || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff))
            return false;
        codepoints.push_back(value);
        index += count;
    }
    return true;
}

NSUInteger utf16_offset_for_codepoint(const std::vector<uint32_t> &codepoints,
                                      uint32_t codepoint_index) {
    NSUInteger result = 0;
    const auto count = std::min<std::size_t>(codepoint_index, codepoints.size());
    for (std::size_t index = 0; index < count; ++index)
        result += codepoints[index] > 0xffff ? 2u : 1u;
    return result;
}

uint32_t codepoint_index_for_utf16(const std::vector<uint32_t> &codepoints,
                                   NSUInteger utf16_index) {
    NSUInteger offset = 0;
    uint32_t result = 0;
    for (const auto codepoint : codepoints) {
        const NSUInteger units = codepoint > 0xffff ? 2u : 1u;
        if (offset + units > utf16_index)
            break;
        offset += units;
        ++result;
    }
    return result;
}

constexpr nk_accessibility_states mac_accessibility_states =
    NK_ACCESSIBILITY_FOCUSABLE | NK_ACCESSIBILITY_FOCUSED | NK_ACCESSIBILITY_SELECTED |
    NK_ACCESSIBILITY_CHECKED | NK_ACCESSIBILITY_DISABLED | NK_ACCESSIBILITY_READ_ONLY |
    NK_ACCESSIBILITY_MULTILINE | NK_ACCESSIBILITY_PASSWORD | NK_ACCESSIBILITY_EXPANDED |
    NK_ACCESSIBILITY_MODAL | NK_ACCESSIBILITY_REQUIRED | NK_ACCESSIBILITY_INVALID |
    NK_ACCESSIBILITY_BUSY | NK_ACCESSIBILITY_HAS_POPUP;

constexpr nk_accessibility_actions mac_accessibility_actions =
    NK_ACCESSIBILITY_CAN_ACTIVATE | NK_ACCESSIBILITY_CAN_FOCUS | NK_ACCESSIBILITY_CAN_SET_VALUE |
    NK_ACCESSIBILITY_CAN_SET_SELECTION | NK_ACCESSIBILITY_CAN_INCREMENT |
    NK_ACCESSIBILITY_CAN_DECREMENT | NK_ACCESSIBILITY_CAN_SCROLL_FORWARD |
    NK_ACCESSIBILITY_CAN_SCROLL_BACKWARD | NK_ACCESSIBILITY_CAN_MOVE_NEXT |
    NK_ACCESSIBILITY_CAN_MOVE_PREVIOUS | NK_ACCESSIBILITY_CAN_TOGGLE | NK_ACCESSIBILITY_CAN_SELECT |
    NK_ACCESSIBILITY_CAN_DESELECT | NK_ACCESSIBILITY_CAN_EXPAND | NK_ACCESSIBILITY_CAN_COLLAPSE |
    NK_ACCESSIBILITY_CAN_DISMISS | NK_ACCESSIBILITY_CAN_SHOW_CONTEXT_MENU |
    NK_ACCESSIBILITY_CAN_SCROLL_INTO_VIEW;

bool copy_mac_accessibility_node(
    const nk_accessibility_node &node,
    const std::unordered_map<nk_accessibility_node_id, MacAccessibilityNode> &nodes,
    MacAccessibilityNode &copy) {
    std::vector<uint32_t> value_codepoints;
    if (!decode_utf8(node.value ? node.value : "", value_codepoints) || !valid_utf8(node.label) ||
        node.struct_size < sizeof(node) || node.id == NK_ACCESSIBILITY_ROOT ||
        node.role > NK_ACCESSIBILITY_ALERT ||
        node.orientation > NK_ACCESSIBILITY_ORIENTATION_VERTICAL ||
        (node.states & ~mac_accessibility_states) || (node.actions & ~mac_accessibility_actions) ||
        !std::isfinite(node.x) || !std::isfinite(node.y) || !std::isfinite(node.width) ||
        !std::isfinite(node.height) || node.width < 0 || node.height < 0 ||
        !std::isfinite(node.numeric_value) || !std::isfinite(node.numeric_minimum) ||
        !std::isfinite(node.numeric_maximum) ||
        (node.role == NK_ACCESSIBILITY_SLIDER &&
         (node.numeric_minimum > node.numeric_maximum ||
          node.numeric_value < node.numeric_minimum || node.numeric_value > node.numeric_maximum)))
        return false;

    const uint64_t text_end = static_cast<uint64_t>(node.text_start) + value_codepoints.size();
    const bool no_selection = node.selection_start == NK_ACCESSIBILITY_TEXT_POSITION_NONE &&
                              node.selection_end == NK_ACCESSIBILITY_TEXT_POSITION_NONE;
    const bool valid_selection = node.selection_start != NK_ACCESSIBILITY_TEXT_POSITION_NONE &&
                                 node.selection_end != NK_ACCESSIBILITY_TEXT_POSITION_NONE &&
                                 node.selection_start <= node.selection_end &&
                                 node.selection_start >= node.text_start &&
                                 node.selection_end <= text_end;
    if (text_end > node.document_length || (!no_selection && !valid_selection) ||
        (node.parent_id != NK_ACCESSIBILITY_ROOT && nodes.find(node.parent_id) == nodes.end()))
        return false;

    auto ancestor = node.parent_id;
    for (std::size_t depth = 0; ancestor != NK_ACCESSIBILITY_ROOT; ++depth) {
        if (ancestor == node.id || depth > nodes.size())
            return false;
        const auto parent = nodes.find(ancestor);
        if (parent == nodes.end())
            break;
        ancestor = parent->second.parent;
    }

    copy.id = node.id;
    copy.parent = node.parent_id;
    copy.child_index = node.child_index;
    copy.role = node.role;
    copy.states = node.states;
    copy.actions = node.actions;
    copy.x = node.x;
    copy.y = node.y;
    copy.width = node.width;
    copy.height = node.height;
    copy.label = node.label ? node.label : "";
    copy.value = node.value ? node.value : "";
    copy.numeric_value = node.numeric_value;
    copy.numeric_minimum = node.numeric_minimum;
    copy.numeric_maximum = node.numeric_maximum;
    copy.text_start = node.text_start;
    copy.document_length = node.document_length;
    copy.selection_start = node.selection_start;
    copy.selection_end = node.selection_end;
    copy.set_size = node.set_size;
    copy.position_in_set = node.position_in_set;
    copy.row_count = node.row_count;
    copy.column_count = node.column_count;
    copy.row_index = node.row_index;
    copy.column_index = node.column_index;
    copy.row_span = node.row_span;
    copy.column_span = node.column_span;
    copy.hierarchy_level = node.hierarchy_level;
    copy.orientation = node.orientation;
    return true;
}

void remove_mac_accessibility_descendants(
    std::unordered_map<nk_accessibility_node_id, MacAccessibilityNode> &nodes,
    nk_accessibility_node_id node) {
    std::vector<nk_accessibility_node_id> pending{node};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        for (const auto &[candidate, value] : nodes)
            if (value.parent == pending[index])
                pending.push_back(candidate);
    }
    for (const auto id : pending)
        nodes.erase(id);
}

nk_accessibility_actions mac_accessibility_action_bit(nk_accessibility_action action) {
    switch (action) {
    case NK_ACCESSIBILITY_ACTION_ACTIVATE:
        return NK_ACCESSIBILITY_CAN_ACTIVATE;
    case NK_ACCESSIBILITY_ACTION_FOCUS:
    case NK_ACCESSIBILITY_ACTION_CLEAR_FOCUS:
        return NK_ACCESSIBILITY_CAN_FOCUS;
    case NK_ACCESSIBILITY_ACTION_SET_VALUE:
        return NK_ACCESSIBILITY_CAN_SET_VALUE;
    case NK_ACCESSIBILITY_ACTION_SET_SELECTION:
        return NK_ACCESSIBILITY_CAN_SET_SELECTION;
    case NK_ACCESSIBILITY_ACTION_INCREMENT:
        return NK_ACCESSIBILITY_CAN_INCREMENT;
    case NK_ACCESSIBILITY_ACTION_DECREMENT:
        return NK_ACCESSIBILITY_CAN_DECREMENT;
    case NK_ACCESSIBILITY_ACTION_SCROLL_FORWARD:
        return NK_ACCESSIBILITY_CAN_SCROLL_FORWARD;
    case NK_ACCESSIBILITY_ACTION_SCROLL_BACKWARD:
        return NK_ACCESSIBILITY_CAN_SCROLL_BACKWARD;
    case NK_ACCESSIBILITY_ACTION_MOVE_NEXT:
        return NK_ACCESSIBILITY_CAN_MOVE_NEXT;
    case NK_ACCESSIBILITY_ACTION_MOVE_PREVIOUS:
        return NK_ACCESSIBILITY_CAN_MOVE_PREVIOUS;
    case NK_ACCESSIBILITY_ACTION_TOGGLE:
        return NK_ACCESSIBILITY_CAN_TOGGLE;
    case NK_ACCESSIBILITY_ACTION_SELECT:
        return NK_ACCESSIBILITY_CAN_SELECT;
    case NK_ACCESSIBILITY_ACTION_DESELECT:
        return NK_ACCESSIBILITY_CAN_DESELECT;
    case NK_ACCESSIBILITY_ACTION_EXPAND:
        return NK_ACCESSIBILITY_CAN_EXPAND;
    case NK_ACCESSIBILITY_ACTION_COLLAPSE:
        return NK_ACCESSIBILITY_CAN_COLLAPSE;
    case NK_ACCESSIBILITY_ACTION_DISMISS:
        return NK_ACCESSIBILITY_CAN_DISMISS;
    case NK_ACCESSIBILITY_ACTION_SHOW_CONTEXT_MENU:
        return NK_ACCESSIBILITY_CAN_SHOW_CONTEXT_MENU;
    case NK_ACCESSIBILITY_ACTION_SCROLL_INTO_VIEW:
        return NK_ACCESSIBILITY_CAN_SCROLL_INTO_VIEW;
    default:
        return 0;
    }
}

nk_result emit_mac_accessibility_action(
    nk_handle surface_handle, nk_accessibility_node_id node, nk_accessibility_action action,
    const std::string &value = {},
    nk_accessibility_text_position selection_start = NK_ACCESSIBILITY_TEXT_POSITION_NONE,
    nk_accessibility_text_position selection_end = NK_ACCESSIBILITY_TEXT_POSITION_NONE,
    nk_accessibility_text_granularity granularity = 0) noexcept {
    return nk::core::callback_boundary_or<nk_result>(NK_ERROR_UNKNOWN, [&]() -> nk_result {
        auto resource = surface(surface_handle);
        if (!resource || resource->destroying)
            return NK_ERROR_INVALID_HANDLE;
        const auto found = resource->accessibility_nodes.find(node);
        const auto required = mac_accessibility_action_bit(action);
        if (found == resource->accessibility_nodes.end() || !required ||
            !(found->second.actions & required))
            return NK_ERROR_UNSUPPORTED;
        if (action == NK_ACCESSIBILITY_ACTION_FOCUS)
            resource->accessibility_focus = node;
        else if (action == NK_ACCESSIBILITY_ACTION_CLEAR_FOCUS &&
                 resource->accessibility_focus == node)
            resource->accessibility_focus = NK_ACCESSIBILITY_ROOT;
        nk_accessibility_action_event payload{};
        payload.node_id = node;
        payload.action = action;
        payload.value_offset = value.empty() ? 0u : sizeof(payload);
        payload.value_length = static_cast<uint32_t>(value.size());
        payload.selection_start = selection_start;
        payload.selection_end = selection_end;
        payload.granularity = granularity;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_ACCESSIBILITY_ACTION;
        event.source = surface_handle;
        event.data.resize(sizeof(payload) + value.size() + (value.empty() ? 0u : 1u));
        std::memcpy(event.data.data(), &payload, sizeof(payload));
        if (!value.empty())
            std::memcpy(event.data.data() + sizeof(payload), value.c_str(), value.size() + 1);
        return nk::core::push_event(std::move(event));
    });
}

NSString *mac_accessibility_role(nk_accessibility_role role) {
    switch (role) {
    case NK_ACCESSIBILITY_BUTTON:
        return NSAccessibilityButtonRole;
    case NK_ACCESSIBILITY_CHECKBOX:
        return NSAccessibilityCheckBoxRole;
    case NK_ACCESSIBILITY_RADIO:
        return NSAccessibilityRadioButtonRole;
    case NK_ACCESSIBILITY_TEXT:
    case NK_ACCESSIBILITY_STATUS:
    case NK_ACCESSIBILITY_ALERT:
        return NSAccessibilityStaticTextRole;
    case NK_ACCESSIBILITY_HEADING:
        return NKMacAccessibilityHeadingRole;
    case NK_ACCESSIBILITY_TEXT_FIELD:
        return NSAccessibilityTextFieldRole;
    case NK_ACCESSIBILITY_LINK:
        return NSAccessibilityLinkRole;
    case NK_ACCESSIBILITY_IMAGE:
        return NSAccessibilityImageRole;
    case NK_ACCESSIBILITY_LIST:
        return NSAccessibilityListRole;
    case NK_ACCESSIBILITY_LIST_ITEM:
    case NK_ACCESSIBILITY_ROW:
        return NSAccessibilityRowRole;
    case NK_ACCESSIBILITY_SLIDER:
        return NSAccessibilitySliderRole;
    case NK_ACCESSIBILITY_SCROLL_AREA:
        return NSAccessibilityScrollAreaRole;
    case NK_ACCESSIBILITY_DIALOG:
        return NSAccessibilityGroupRole;
    case NK_ACCESSIBILITY_MENU:
        return NSAccessibilityMenuRole;
    case NK_ACCESSIBILITY_MENU_BAR:
        return NSAccessibilityMenuBarRole;
    case NK_ACCESSIBILITY_MENU_ITEM:
        return NSAccessibilityMenuItemRole;
    case NK_ACCESSIBILITY_TAB_LIST:
        return NSAccessibilityTabGroupRole;
    case NK_ACCESSIBILITY_TAB:
        return NSAccessibilityRadioButtonRole;
    case NK_ACCESSIBILITY_TAB_PANEL:
        return NSAccessibilityGroupRole;
    case NK_ACCESSIBILITY_SWITCH:
        return NSAccessibilityCheckBoxRole;
    case NK_ACCESSIBILITY_PROGRESS_BAR:
        return NSAccessibilityProgressIndicatorRole;
    case NK_ACCESSIBILITY_COMBO_BOX:
        return NSAccessibilityComboBoxRole;
    case NK_ACCESSIBILITY_GRID:
    case NK_ACCESSIBILITY_COLLECTION:
        return NSAccessibilityTableRole;
    case NK_ACCESSIBILITY_CELL:
        return NSAccessibilityCellRole;
    case NK_ACCESSIBILITY_COLUMN_HEADER:
    case NK_ACCESSIBILITY_ROW_HEADER:
        return NSAccessibilityStaticTextRole;
    case NK_ACCESSIBILITY_TREE:
        return NSAccessibilityOutlineRole;
    case NK_ACCESSIBILITY_TREE_ITEM:
        return NSAccessibilityRowRole;
    case NK_ACCESSIBILITY_SEPARATOR:
        return NSAccessibilitySplitterRole;
    case NK_ACCESSIBILITY_TOOLBAR:
        return NSAccessibilityToolbarRole;
    case NK_ACCESSIBILITY_COLLECTION_ITEM:
    case NK_ACCESSIBILITY_GROUP:
    default:
        return NSAccessibilityGroupRole;
    }
}

NSRect mac_accessibility_screen_frame(const MacSurfaceResource &resource, float x, float y,
                                      float width, float height) {
    const NSRect local = NSMakeRect(x, y, width, height);
    if (!resource.view || !resource.view.window)
        return local;
    const NSRect window_rect = [resource.view convertRect:local toView:nil];
    return [resource.view.window convertRectToScreen:window_rect];
}

NSRange mac_accessibility_value_range(const MacAccessibilityNode &node) {
    std::vector<uint32_t> codepoints;
    if (!decode_utf8(node.value, codepoints))
        return NSMakeRange(NSNotFound, 0);
    if (node.selection_start == NK_ACCESSIBILITY_TEXT_POSITION_NONE ||
        node.selection_end == NK_ACCESSIBILITY_TEXT_POSITION_NONE ||
        node.selection_start < node.text_start || node.selection_end < node.text_start)
        return NSMakeRange(NSNotFound, 0);
    const auto start = static_cast<uint64_t>(node.selection_start - node.text_start);
    const auto end = static_cast<uint64_t>(node.selection_end - node.text_start);
    if (end > codepoints.size() || start > end)
        return NSMakeRange(NSNotFound, 0);
    const auto first = utf16_offset_for_codepoint(codepoints, static_cast<uint32_t>(start));
    const auto last = utf16_offset_for_codepoint(codepoints, static_cast<uint32_t>(end));
    return NSMakeRange(first, last - first);
}

bool mac_accessibility_codepoint_range(const MacAccessibilityNode &node, NSRange range,
                                       nk_accessibility_text_position &start,
                                       nk_accessibility_text_position &end) {
    std::vector<uint32_t> codepoints;
    if (!decode_utf8(node.value, codepoints))
        return false;
    const auto length = utf16_offset_for_codepoint(codepoints, codepoints.size());
    if (range.location == NSNotFound || range.location > length ||
        range.length > length - range.location)
        return false;
    start = node.text_start + codepoint_index_for_utf16(codepoints, range.location);
    end = node.text_start + codepoint_index_for_utf16(codepoints, range.location + range.length);
    return true;
}

void refresh_mac_accessibility_elements(MacSurfaceResource &resource) noexcept {
    nk::core::callback_boundary([&] {
        if (!resource.accessibility_container)
            return;
        NSMutableArray<NKMacAccessibilityElement *> *all_elements =
            [NSMutableArray arrayWithCapacity:resource.accessibility_nodes.size()];
        NSMutableArray<NKMacAccessibilityElement *> *root_elements = [NSMutableArray array];
        std::unordered_map<nk_accessibility_node_id, NKMacAccessibilityElement *> elements_by_id;
        std::function<void(nk_accessibility_node_id)> append_children =
            [&](nk_accessibility_node_id parent) {
                std::vector<nk_accessibility_node_id> children;
                for (const auto &[id, node] : resource.accessibility_nodes)
                    if (node.parent == parent)
                        children.push_back(id);
                std::sort(children.begin(), children.end(), [&](auto lhs, auto rhs) {
                    const auto &left = resource.accessibility_nodes.at(lhs);
                    const auto &right = resource.accessibility_nodes.at(rhs);
                    return left.child_index == right.child_index
                               ? lhs < rhs
                               : left.child_index < right.child_index;
                });
                for (const auto id : children) {
                    const auto &node = resource.accessibility_nodes.at(id);
                    auto element = [[NKMacAccessibilityElement alloc] init];
                    if (!element)
                        continue;
                    element.surface = resource.handle;
                    element.node = id;
                    element.nativeContainer = resource.accessibility_container;
                    element.nativeFrame = mac_accessibility_screen_frame(resource, node.x, node.y,
                                                                         node.width, node.height);
                    element.accessibilityElement = YES;
                    element.accessibilityRole = mac_accessibility_role(node.role);
                    element.accessibilityLabel =
                        node.label.empty() ? nil : string(node.label.c_str());
                    element.accessibilityEnabled = (node.states & NK_ACCESSIBILITY_DISABLED) == 0;
                    element.accessibilitySelected = (node.states & NK_ACCESSIBILITY_SELECTED) != 0;
                    element.accessibilityExpanded = (node.states & NK_ACCESSIBILITY_EXPANDED) != 0;
                    element.accessibilityRequired = (node.states & NK_ACCESSIBILITY_REQUIRED) != 0;
                    element.accessibilityProtectedContent =
                        (node.states & NK_ACCESSIBILITY_PASSWORD) != 0;
                    element.accessibilityValueDescription =
                        node.value.empty() ? nil : string(node.value.c_str());
                    if (node.orientation == NK_ACCESSIBILITY_ORIENTATION_HORIZONTAL)
                        element.accessibilityOrientation = NSAccessibilityOrientationHorizontal;
                    else if (node.orientation == NK_ACCESSIBILITY_ORIENTATION_VERTICAL)
                        element.accessibilityOrientation = NSAccessibilityOrientationVertical;
                    if (node.role == NK_ACCESSIBILITY_CHECKBOX ||
                        node.role == NK_ACCESSIBILITY_SWITCH)
                        element.accessibilityValue =
                            @((node.states & NK_ACCESSIBILITY_CHECKED) != 0);
                    else if (node.role == NK_ACCESSIBILITY_SLIDER ||
                             node.role == NK_ACCESSIBILITY_PROGRESS_BAR)
                        element.accessibilityValue = @(node.numeric_value);
                    else
                        element.accessibilityValue =
                            node.value.empty() ? nil : string(node.value.c_str());
                    element.accessibilityFrame = element.nativeFrame;
                    elements_by_id.emplace(id, element);
                    [all_elements addObject:element];
                    if (parent == NK_ACCESSIBILITY_ROOT)
                        [root_elements addObject:element];
                    append_children(id);
                }
            };
        append_children(NK_ACCESSIBILITY_ROOT);
        for (const auto &[node_id, node] : resource.accessibility_nodes) {
            const auto element = elements_by_id.find(node_id);
            if (element == elements_by_id.end())
                continue;
            NSObject *parent = resource.accessibility_container;
            if (node.parent != NK_ACCESSIBILITY_ROOT) {
                const auto parent_element = elements_by_id.find(node.parent);
                if (parent_element != elements_by_id.end())
                    parent = parent_element->second;
            }
            element->second.nativeParent = parent;
            element->second.accessibilityParent = parent;
            NSMutableArray<NKMacAccessibilityElement *> *children = [NSMutableArray array];
            std::vector<nk_accessibility_node_id> child_ids;
            for (const auto &[child_id, child] : resource.accessibility_nodes)
                if (child.parent == node_id)
                    child_ids.push_back(child_id);
            std::sort(child_ids.begin(), child_ids.end(), [&](auto lhs, auto rhs) {
                const auto &left = resource.accessibility_nodes.at(lhs);
                const auto &right = resource.accessibility_nodes.at(rhs);
                return left.child_index == right.child_index ? lhs < rhs
                                                             : left.child_index < right.child_index;
            });
            for (const auto child_id : child_ids) {
                const auto child = elements_by_id.find(child_id);
                if (child != elements_by_id.end())
                    [children addObject:child->second];
            }
            element->second.nativeChildren = children;
            element->second.accessibilityChildren = children;
            NSMutableArray<NKMacAccessibilityElement *> *selected_children = [NSMutableArray array];
            for (NKMacAccessibilityElement *child in children) {
                const auto child_node = resource.accessibility_nodes.find(child.node);
                if (child_node != resource.accessibility_nodes.end() &&
                    (child_node->second.states & NK_ACCESSIBILITY_SELECTED))
                    [selected_children addObject:child];
            }
            element->second.accessibilitySelectedChildren = selected_children;
        }
        resource.accessibility_container.elements = root_elements;
        resource.accessibility_container.allElements = all_elements;
        resource.accessibility_container.accessibilityChildren = root_elements;
        NSAccessibilityPostNotification(resource.accessibility_container,
                                        NSAccessibilityLayoutChangedNotification);
    });
}

NSString *const NKMacAccessibilityToggleAction = @"NativeKitToggle";
NSString *const NKMacAccessibilitySelectAction = @"NativeKitSelect";
NSString *const NKMacAccessibilityDeselectAction = @"NativeKitDeselect";
NSString *const NKMacAccessibilityExpandAction = @"NativeKitExpand";
NSString *const NKMacAccessibilityCollapseAction = @"NativeKitCollapse";
NSString *const NKMacAccessibilityDismissAction = @"NativeKitDismiss";
NSString *const NKMacAccessibilityMoveNextAction = @"NativeKitMoveNext";
NSString *const NKMacAccessibilityMovePreviousAction = @"NativeKitMovePrevious";
NSString *const NKMacAccessibilityScrollForwardAction = @"NativeKitScrollForward";
NSString *const NKMacAccessibilityScrollBackwardAction = @"NativeKitScrollBackward";
NSString *const NKMacAccessibilityScrollIntoViewAction = @"NativeKitScrollIntoView";
NSString *const NKMacAccessibilitySetValueAction = @"NativeKitSetValue";
NSString *const NKMacAccessibilitySetSelectionAction = @"NativeKitSetSelection";
NSString *const NKMacAccessibilityShowContextMenuAction = @"NativeKitShowContextMenu";

nk_accessibility_action mac_accessibility_action_for_name(NSString *name) {
    if ([name isEqualToString:NKMacAccessibilityToggleAction])
        return NK_ACCESSIBILITY_ACTION_TOGGLE;
    if ([name isEqualToString:NKMacAccessibilitySelectAction])
        return NK_ACCESSIBILITY_ACTION_SELECT;
    if ([name isEqualToString:NKMacAccessibilityDeselectAction])
        return NK_ACCESSIBILITY_ACTION_DESELECT;
    if ([name isEqualToString:NKMacAccessibilityExpandAction])
        return NK_ACCESSIBILITY_ACTION_EXPAND;
    if ([name isEqualToString:NKMacAccessibilityCollapseAction])
        return NK_ACCESSIBILITY_ACTION_COLLAPSE;
    if ([name isEqualToString:NKMacAccessibilityDismissAction])
        return NK_ACCESSIBILITY_ACTION_DISMISS;
    if ([name isEqualToString:NKMacAccessibilityMoveNextAction])
        return NK_ACCESSIBILITY_ACTION_MOVE_NEXT;
    if ([name isEqualToString:NKMacAccessibilityMovePreviousAction])
        return NK_ACCESSIBILITY_ACTION_MOVE_PREVIOUS;
    if ([name isEqualToString:NKMacAccessibilityScrollForwardAction])
        return NK_ACCESSIBILITY_ACTION_SCROLL_FORWARD;
    if ([name isEqualToString:NKMacAccessibilityScrollBackwardAction])
        return NK_ACCESSIBILITY_ACTION_SCROLL_BACKWARD;
    if ([name isEqualToString:NKMacAccessibilityScrollIntoViewAction])
        return NK_ACCESSIBILITY_ACTION_SCROLL_INTO_VIEW;
    if ([name isEqualToString:NKMacAccessibilitySetValueAction])
        return NK_ACCESSIBILITY_ACTION_SET_VALUE;
    if ([name isEqualToString:NKMacAccessibilitySetSelectionAction])
        return NK_ACCESSIBILITY_ACTION_SET_SELECTION;
    if ([name isEqualToString:NKMacAccessibilityShowContextMenuAction])
        return NK_ACCESSIBILITY_ACTION_SHOW_CONTEXT_MENU;
    return 0;
}

std::vector<uint32_t> current_text_codepoints(const MacWindowResource &resource) {
    std::vector<uint32_t> result;
    decode_utf8(resource.text_input_text, result);
    return result;
}

NSRange native_range_for_positions(const MacWindowResource &resource, nk_text_position start,
                                   nk_text_position end) {
    const auto points = current_text_codepoints(resource);
    const auto local_start =
        start == NK_TEXT_POSITION_NONE || start < resource.text_input_state.text_start
            ? 0u
            : start - resource.text_input_state.text_start;
    const auto local_end =
        end == NK_TEXT_POSITION_NONE || end < resource.text_input_state.text_start
            ? local_start
            : end - resource.text_input_state.text_start;
    const NSUInteger start_offset = utf16_offset_for_codepoint(points, local_start);
    const NSUInteger end_offset = utf16_offset_for_codepoint(points, local_end);
    return NSMakeRange(start_offset, end_offset >= start_offset ? end_offset - start_offset : 0);
}

bool codepoint_range_for_native_range(const MacWindowResource &resource, NSRange range,
                                      nk_text_position &out_start, nk_text_position &out_end) {
    if (range.location == NSNotFound)
        return false;
    const auto points = current_text_codepoints(resource);
    const auto total_units =
        utf16_offset_for_codepoint(points, static_cast<uint32_t>(points.size()));
    const uint64_t range_end = static_cast<uint64_t>(range.location) + range.length;
    if (range_end > total_units)
        return false;
    const auto local_start = codepoint_index_for_utf16(points, range.location);
    const auto local_end = codepoint_index_for_utf16(points, static_cast<NSUInteger>(range_end));
    out_start = static_cast<nk_text_position>(resource.text_input_state.text_start + local_start);
    out_end = static_cast<nk_text_position>(resource.text_input_state.text_start + local_end);
    return true;
}

void update_text_snapshot(MacWindowResource &resource, nk_text_position replace_start,
                          nk_text_position replace_end, std::string_view inserted) {
    std::vector<uint32_t> old_codepoints;
    if (!decode_utf8(resource.text_input_text, old_codepoints) ||
        replace_start < resource.text_input_state.text_start || replace_end < replace_start ||
        static_cast<uint64_t>(replace_end) >
            static_cast<uint64_t>(resource.text_input_state.text_start) + old_codepoints.size())
        return;
    std::vector<uint32_t> inserted_codepoints;
    if (!decode_utf8(inserted, inserted_codepoints))
        return;
    const auto first =
        static_cast<std::size_t>(replace_start - resource.text_input_state.text_start);
    const auto last = static_cast<std::size_t>(replace_end - resource.text_input_state.text_start);
    const auto byte_offset = [&](std::size_t codepoint_index) {
        std::size_t bytes = 0;
        for (std::size_t index = 0; index < codepoint_index; ++index) {
            const auto value = old_codepoints[index];
            bytes += value < 0x80 ? 1u : (value < 0x800 ? 2u : (value < 0x10000 ? 3u : 4u));
        }
        return bytes;
    };
    std::string updated = resource.text_input_text;
    const auto first_byte = byte_offset(first);
    const auto last_byte = byte_offset(last);
    updated.replace(first_byte, last_byte - first_byte, inserted.data(), inserted.size());
    const int64_t delta =
        static_cast<int64_t>(inserted_codepoints.size()) - static_cast<int64_t>(last - first);
    resource.text_input_text = std::move(updated);
    resource.text_input_state.text = resource.text_input_text.c_str();
    resource.text_input_state.document_length = static_cast<nk_text_position>(std::max<int64_t>(
        0, static_cast<int64_t>(resource.text_input_state.document_length) + delta));
}

void emit_text_edit(MacWindowResource &resource, nk_text_edit_event payload,
                    const std::string &text = {}) {
    payload.text_offset = text.empty() ? 0u : sizeof(payload);
    payload.text_length = static_cast<uint32_t>(text.size());
    std::vector<std::byte> data(sizeof(payload) + text.size() + (text.empty() ? 0u : 1u));
    std::memcpy(data.data(), &payload, sizeof(payload));
    if (!text.empty())
        std::memcpy(data.data() + sizeof(payload), text.c_str(), text.size() + 1);
    queue_input_event(NK_EVENT_TEXT_EDIT, resource.handle, std::move(data));
}

void apply_text_edit_state(MacWindowResource &resource, nk_text_edit_action action,
                           nk_text_position replace_start, nk_text_position replace_end,
                           const std::string &text, nk_text_position selection_start,
                           nk_text_position selection_end, nk_text_position composition_start,
                           nk_text_position composition_end) {
    if (replace_start != NK_TEXT_POSITION_NONE && replace_end != NK_TEXT_POSITION_NONE)
        update_text_snapshot(resource, replace_start, replace_end, text);
    resource.text_input_state.selection_start = selection_start;
    resource.text_input_state.selection_end = selection_end;
    resource.text_input_state.composition_start = composition_start;
    resource.text_input_state.composition_end = composition_end;
    resource.text_composing = composition_start != NK_TEXT_POSITION_NONE;
    resource.text_composition_start = composition_start;
    resource.text_composition_end = composition_end;
    if (resource.text_composing) {
        resource.marked_text = text;
        resource.marked_native_range =
            native_range_for_positions(resource, composition_start, composition_end);
    } else {
        resource.marked_text.clear();
        resource.marked_native_range = NSMakeRange(NSNotFound, 0);
    }
    resource.selected_native_range =
        native_range_for_positions(resource, selection_start, selection_end);
    nk_text_edit_event payload{};
    payload.action = action;
    payload.replace_start = replace_start;
    payload.replace_end = replace_end;
    payload.selection_start = selection_start;
    payload.selection_end = selection_end;
    payload.composition_start = composition_start;
    payload.composition_end = composition_end;
    emit_text_edit(resource, payload, text);
}

nk_text_position text_replacement_start(const MacWindowResource &resource) {
    return resource.text_composing ? resource.text_composition_start
                                   : resource.text_input_state.selection_start;
}

nk_text_position text_replacement_end(const MacWindowResource &resource) {
    return resource.text_composing ? resource.text_composition_end
                                   : resource.text_input_state.selection_end;
}

void emit_committed_text(MacWindowResource &resource, const std::string &text) {
    std::vector<uint32_t> points;
    if (!decode_utf8(text, points))
        return;
    if (!resource.text_input_active) {
        for (const auto point : points) {
            const nk_text_input_event payload{point, 0};
            queue_input_event(NK_EVENT_TEXT_INPUT, resource.handle, bytes_of(payload));
        }
        resource.text_composing = false;
        resource.text_composition_start = NK_TEXT_POSITION_NONE;
        resource.text_composition_end = NK_TEXT_POSITION_NONE;
        resource.text_input_state.composition_start = NK_TEXT_POSITION_NONE;
        resource.text_input_state.composition_end = NK_TEXT_POSITION_NONE;
        resource.marked_text.clear();
        resource.marked_native_range = NSMakeRange(NSNotFound, 0);
        return;
    }
    const auto start = text_replacement_start(resource);
    const auto end = text_replacement_end(resource);
    const auto selection = static_cast<nk_text_position>(start + points.size());
    apply_text_edit_state(resource, NK_TEXT_EDIT_COMMIT, start, end, text, selection, selection,
                          NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
}

void finish_text_composition(MacWindowResource &resource) {
    if (!resource.text_composing)
        return;
    auto selection_start = resource.text_input_state.selection_start;
    auto selection_end = resource.text_input_state.selection_end;
    if (selection_start == NK_TEXT_POSITION_NONE)
        selection_start = selection_end = resource.text_composition_end;
    apply_text_edit_state(resource, NK_TEXT_EDIT_FINISH_COMPOSITION, NK_TEXT_POSITION_NONE,
                          NK_TEXT_POSITION_NONE, {}, selection_start, selection_end,
                          NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
}

nk_modifiers modifiers_from_native(NSEventModifierFlags flags) {
    nk_modifiers result = 0;
    if (flags & NSEventModifierFlagShift)
        result |= NK_MOD_SHIFT;
    if (flags & NSEventModifierFlagControl)
        result |= NK_MOD_CONTROL;
    if (flags & NSEventModifierFlagOption)
        result |= NK_MOD_ALT;
    if (flags & NSEventModifierFlagCommand)
        result |= NK_MOD_SUPER;
    if (flags & NSEventModifierFlagCapsLock)
        result |= NK_MOD_CAPS_LOCK;
    return result;
}

nk_key key_from_cocoa(unsigned short code) {
    switch (code) {
    case kVK_ANSI_A:
        return NK_KEY_A;
    case kVK_ANSI_S:
        return NK_KEY_S;
    case kVK_ANSI_D:
        return NK_KEY_D;
    case kVK_ANSI_F:
        return NK_KEY_F;
    case kVK_ANSI_H:
        return NK_KEY_H;
    case kVK_ANSI_G:
        return NK_KEY_G;
    case kVK_ANSI_Z:
        return NK_KEY_Z;
    case kVK_ANSI_X:
        return NK_KEY_X;
    case kVK_ANSI_C:
        return NK_KEY_C;
    case kVK_ANSI_V:
        return NK_KEY_V;
    case kVK_ANSI_B:
        return NK_KEY_B;
    case kVK_ANSI_Q:
        return NK_KEY_Q;
    case kVK_ANSI_W:
        return NK_KEY_W;
    case kVK_ANSI_E:
        return NK_KEY_E;
    case kVK_ANSI_R:
        return NK_KEY_R;
    case kVK_ANSI_Y:
        return NK_KEY_Y;
    case kVK_ANSI_T:
        return NK_KEY_T;
    case kVK_ANSI_1:
        return NK_KEY_1;
    case kVK_ANSI_2:
        return NK_KEY_2;
    case kVK_ANSI_3:
        return NK_KEY_3;
    case kVK_ANSI_4:
        return NK_KEY_4;
    case kVK_ANSI_6:
        return NK_KEY_6;
    case kVK_ANSI_5:
        return NK_KEY_5;
    case kVK_ANSI_Equal:
        return NK_KEY_EQUAL;
    case kVK_ANSI_9:
        return NK_KEY_9;
    case kVK_ANSI_7:
        return NK_KEY_7;
    case kVK_ANSI_Minus:
        return NK_KEY_MINUS;
    case kVK_ANSI_8:
        return NK_KEY_8;
    case kVK_ANSI_0:
        return NK_KEY_0;
    case kVK_ANSI_RightBracket:
        return NK_KEY_RIGHT_BRACKET;
    case kVK_ANSI_O:
        return NK_KEY_O;
    case kVK_ANSI_U:
        return NK_KEY_U;
    case kVK_ANSI_LeftBracket:
        return NK_KEY_LEFT_BRACKET;
    case kVK_ANSI_I:
        return NK_KEY_I;
    case kVK_ANSI_P:
        return NK_KEY_P;
    case kVK_Return:
        return NK_KEY_ENTER;
    case kVK_ANSI_L:
        return NK_KEY_L;
    case kVK_ANSI_J:
        return NK_KEY_J;
    case kVK_ANSI_Quote:
        return NK_KEY_APOSTROPHE;
    case kVK_ANSI_K:
        return NK_KEY_K;
    case kVK_ANSI_Semicolon:
        return NK_KEY_SEMICOLON;
    case kVK_ANSI_Backslash:
        return NK_KEY_BACKSLASH;
    case kVK_ANSI_Comma:
        return NK_KEY_COMMA;
    case kVK_ANSI_Slash:
        return NK_KEY_SLASH;
    case kVK_ANSI_N:
        return NK_KEY_N;
    case kVK_ANSI_M:
        return NK_KEY_M;
    case kVK_ANSI_Period:
        return NK_KEY_PERIOD;
    case kVK_Tab:
        return NK_KEY_TAB;
    case kVK_Space:
        return NK_KEY_SPACE;
    case kVK_ANSI_Grave:
        return NK_KEY_GRAVE_ACCENT;
    case kVK_Delete:
        return NK_KEY_BACKSPACE;
    case kVK_Escape:
        return NK_KEY_ESCAPE;
    case kVK_RightCommand:
        return NK_KEY_RIGHT_SUPER;
    case kVK_Command:
        return NK_KEY_LEFT_SUPER;
    case kVK_Shift:
        return NK_KEY_LEFT_SHIFT;
    case kVK_CapsLock:
        return NK_KEY_CAPS_LOCK;
    case kVK_Option:
        return NK_KEY_LEFT_ALT;
    case kVK_Control:
        return NK_KEY_LEFT_CONTROL;
    case kVK_RightShift:
        return NK_KEY_RIGHT_SHIFT;
    case kVK_RightOption:
        return NK_KEY_RIGHT_ALT;
    case kVK_RightControl:
        return NK_KEY_RIGHT_CONTROL;
    case kVK_F17:
        return NK_KEY_F17;
    case kVK_ANSI_KeypadDecimal:
        return NK_KEY_KP_DECIMAL;
    case kVK_ANSI_KeypadMultiply:
        return NK_KEY_KP_MULTIPLY;
    case kVK_ANSI_KeypadPlus:
        return NK_KEY_KP_ADD;
    case kVK_ANSI_KeypadClear:
        return NK_KEY_NUM_LOCK;
    case kVK_ANSI_KeypadDivide:
        return NK_KEY_KP_DIVIDE;
    case kVK_ANSI_KeypadEnter:
        return NK_KEY_KP_ENTER;
    case kVK_ANSI_KeypadMinus:
        return NK_KEY_KP_SUBTRACT;
    case kVK_F18:
        return NK_KEY_F18;
    case kVK_F19:
        return NK_KEY_F19;
    case kVK_ANSI_KeypadEquals:
        return NK_KEY_KP_EQUAL;
    case kVK_ANSI_Keypad0:
        return NK_KEY_KP_0;
    case kVK_ANSI_Keypad1:
        return NK_KEY_KP_1;
    case kVK_ANSI_Keypad2:
        return NK_KEY_KP_2;
    case kVK_ANSI_Keypad3:
        return NK_KEY_KP_3;
    case kVK_ANSI_Keypad4:
        return NK_KEY_KP_4;
    case kVK_ANSI_Keypad5:
        return NK_KEY_KP_5;
    case kVK_ANSI_Keypad6:
        return NK_KEY_KP_6;
    case kVK_ANSI_Keypad7:
        return NK_KEY_KP_7;
    case kVK_F20:
        return NK_KEY_F20;
    case kVK_ANSI_Keypad8:
        return NK_KEY_KP_8;
    case kVK_ANSI_Keypad9:
        return NK_KEY_KP_9;
    case kVK_F5:
        return NK_KEY_F5;
    case kVK_F6:
        return NK_KEY_F6;
    case kVK_F7:
        return NK_KEY_F7;
    case kVK_F3:
        return NK_KEY_F3;
    case kVK_F8:
        return NK_KEY_F8;
    case kVK_F9:
        return NK_KEY_F9;
    case kVK_F11:
        return NK_KEY_F11;
    case kVK_F13:
        return NK_KEY_F13;
    case kVK_F16:
        return NK_KEY_F16;
    case kVK_F14:
        return NK_KEY_F14;
    case kVK_F10:
        return NK_KEY_F10;
    case kVK_F12:
        return NK_KEY_F12;
    case kVK_F15:
        return NK_KEY_F15;
    case kVK_Help:
        return NK_KEY_INSERT;
    case kVK_Home:
        return NK_KEY_HOME;
    case kVK_PageUp:
        return NK_KEY_PAGE_UP;
    case kVK_ForwardDelete:
        return NK_KEY_DELETE;
    case kVK_F4:
        return NK_KEY_F4;
    case kVK_End:
        return NK_KEY_END;
    case kVK_F2:
        return NK_KEY_F2;
    case kVK_PageDown:
        return NK_KEY_PAGE_DOWN;
    case kVK_F1:
        return NK_KEY_F1;
    case kVK_LeftArrow:
        return NK_KEY_LEFT;
    case kVK_RightArrow:
        return NK_KEY_RIGHT;
    case kVK_DownArrow:
        return NK_KEY_DOWN;
    case kVK_UpArrow:
        return NK_KEY_UP;
    default:
        return NK_KEY_UNKNOWN;
    }
}

void emit_key_transition(MacWindowResource &resource, NSEvent *event, nk_input_action action) {
    const auto key = key_from_cocoa(event.keyCode);
    if (key != NK_KEY_UNKNOWN)
        resource.keys[key] = action == NK_INPUT_RELEASE ? NK_INPUT_RELEASE : NK_INPUT_PRESS;
    const nk_key_event payload{key, event.keyCode, action,
                               modifiers_from_native(event.modifierFlags)};
    queue_input_event(NK_EVENT_KEY, resource.handle, bytes_of(payload));
}

void emit_pointer_enter(MacWindowResource &resource, bool entered) {
    queue_input_event(NK_EVENT_POINTER_ENTER, resource.handle, {}, entered ? 1u : 0u);
}

NSPoint local_pointer_position(NKContentView *view, NSEvent *event) {
    return [view convertPoint:event.locationInWindow fromView:nil];
}

nk_pointer_button pointer_button_from_cocoa(NSEvent *event) {
    if (event.type == NSEventTypeLeftMouseDown || event.type == NSEventTypeLeftMouseUp ||
        event.type == NSEventTypeLeftMouseDragged)
        return NK_POINTER_BUTTON_LEFT;
    if (event.type == NSEventTypeRightMouseDown || event.type == NSEventTypeRightMouseUp ||
        event.type == NSEventTypeRightMouseDragged)
        return NK_POINTER_BUTTON_RIGHT;
    const NSInteger button = event.buttonNumber;
    if (button == 2)
        return NK_POINTER_BUTTON_MIDDLE;
    if (button >= 3 && button <= 7)
        return static_cast<nk_pointer_button>(button);
    return UINT32_MAX;
}

void emit_pointer_button(MacWindowResource &resource, nk_pointer_button button,
                         nk_input_action action, nk_modifiers modifiers) {
    if (button > NK_POINTER_BUTTON_LAST)
        return;
    resource.pointer_buttons[button] = action;
    const nk_pointer_button_event payload{
        button, action, modifiers, 0, resource.pointer_x, resource.pointer_y};
    queue_input_event(NK_EVENT_POINTER_BUTTON, resource.handle, bytes_of(payload));
}

void emit_touch(MacWindowResource &resource, uint32_t pointer_id, nk_touch_action action,
                nk_touch_tool tool, NSPoint location, nk_modifiers modifiers, float pressure = 1.f,
                float tilt_x = 0.f, float tilt_y = 0.f, uint32_t flags = 0) {
    const nk_touch_event payload{pointer_id, action,   tool,   modifiers, location.x,
                                 location.y, pressure, tilt_x, tilt_y,    0};
    queue_input_event(NK_EVENT_TOUCH, resource.handle, bytes_of(payload), flags);
}

void set_cursor_hidden(MacWindowResource &resource, bool hidden) {
    if (resource.cursor_hidden == hidden)
        return;
    if (hidden)
        [NSCursor hide];
    else
        [NSCursor unhide];
    resource.cursor_hidden = hidden;
}

void apply_cursor(MacWindowResource &resource) {
    if (resource.cursor_mode == NK_CURSOR_MODE_HIDDEN ||
        resource.cursor_mode == NK_CURSOR_MODE_DISABLED)
        set_cursor_hidden(resource, true);
    else {
        set_cursor_hidden(resource, false);
        NSCursor *selected = resource.cursor ? resource.cursor->cursor : [NSCursor arrowCursor];
        [selected set];
    }
}

NSCursor *diagonal_resize_cursor(bool northwest_southeast) {
    NSImage *image = [[NSImage alloc] initWithSize:NSMakeSize(32, 32)];
    [image lockFocus];
    NSBezierPath *path = [NSBezierPath bezierPath];
    path.lineWidth = 2.0;
    if (northwest_southeast) {
        [path moveToPoint:NSMakePoint(5, 5)];
        [path lineToPoint:NSMakePoint(27, 27)];
        [path moveToPoint:NSMakePoint(5, 5)];
        [path lineToPoint:NSMakePoint(5, 12)];
        [path moveToPoint:NSMakePoint(5, 5)];
        [path lineToPoint:NSMakePoint(12, 5)];
        [path moveToPoint:NSMakePoint(27, 27)];
        [path lineToPoint:NSMakePoint(20, 27)];
        [path moveToPoint:NSMakePoint(27, 27)];
        [path lineToPoint:NSMakePoint(27, 20)];
    } else {
        [path moveToPoint:NSMakePoint(5, 27)];
        [path lineToPoint:NSMakePoint(27, 5)];
        [path moveToPoint:NSMakePoint(5, 27)];
        [path lineToPoint:NSMakePoint(5, 20)];
        [path moveToPoint:NSMakePoint(5, 27)];
        [path lineToPoint:NSMakePoint(12, 27)];
        [path moveToPoint:NSMakePoint(27, 5)];
        [path lineToPoint:NSMakePoint(20, 5)];
        [path moveToPoint:NSMakePoint(27, 5)];
        [path lineToPoint:NSMakePoint(27, 12)];
    }
    [[NSColor whiteColor] setStroke];
    path.lineWidth = 5.0;
    [path stroke];
    [[NSColor blackColor] setStroke];
    path.lineWidth = 2.0;
    [path stroke];
    [image unlockFocus];
    return [[NSCursor alloc] initWithImage:image hotSpot:NSMakePoint(16, 16)];
}

void release_cursor_capture(MacWindowResource &resource) {
    if (resource.pointer_captured) {
        CGAssociateMouseAndMouseCursorPosition(true);
        resource.pointer_captured = false;
    }
    set_cursor_hidden(resource, false);
}

bool apply_cursor_mode(MacWindowResource &resource, nk_cursor_mode mode) {
    const auto old_mode = resource.cursor_mode;
    if (resource.pointer_captured && mode != NK_CURSOR_MODE_CAPTURED &&
        mode != NK_CURSOR_MODE_DISABLED) {
        CGAssociateMouseAndMouseCursorPosition(true);
        resource.pointer_captured = false;
    }
    if ((mode == NK_CURSOR_MODE_CAPTURED || mode == NK_CURSOR_MODE_DISABLED) &&
        !resource.pointer_captured) {
        if (CGAssociateMouseAndMouseCursorPosition(false) != kCGErrorSuccess) {
            resource.cursor_mode = old_mode;
            if (resource.cursor_mode != NK_CURSOR_MODE_NORMAL)
                set_cursor_hidden(resource, true);
            return false;
        }
        resource.pointer_captured = true;
    }
    resource.cursor_mode = mode;
    apply_cursor(resource);
    return true;
}

void reset_window_input(MacWindowResource &resource) {
    if (resource.text_input_active) {
        finish_text_composition(resource);
    } else {
        resource.text_composing = false;
        resource.text_composition_start = NK_TEXT_POSITION_NONE;
        resource.text_composition_end = NK_TEXT_POSITION_NONE;
        resource.text_input_state.composition_start = NK_TEXT_POSITION_NONE;
        resource.text_input_state.composition_end = NK_TEXT_POSITION_NONE;
        resource.marked_text.clear();
        resource.marked_native_range = NSMakeRange(NSNotFound, 0);
    }
    for (nk_key key = 1; key <= NK_KEY_LAST; ++key) {
        if (resource.keys[key] != NK_INPUT_PRESS)
            continue;
        resource.keys[key] = NK_INPUT_RELEASE;
        const nk_key_event payload{key, 0, NK_INPUT_RELEASE, 0};
        queue_input_event(NK_EVENT_KEY, resource.handle, bytes_of(payload), 1u);
    }
    for (nk_pointer_button button = 0; button <= NK_POINTER_BUTTON_LAST; ++button) {
        if (resource.pointer_buttons[button] != NK_INPUT_PRESS)
            continue;
        resource.pointer_buttons[button] = NK_INPUT_RELEASE;
        const nk_pointer_button_event payload{button, NK_INPUT_RELEASE,   0,
                                              0,      resource.pointer_x, resource.pointer_y};
        queue_input_event(NK_EVENT_POINTER_BUTTON, resource.handle, bytes_of(payload), 1u);
    }
    for (const auto &[identity, pointer_id] : resource.touch_pointers) {
        (void)identity;
        const nk_touch_event payload{pointer_id,
                                     NK_TOUCH_CANCEL,
                                     NK_TOUCH_TOOL_FINGER,
                                     0,
                                     resource.pointer_x,
                                     resource.pointer_y,
                                     0.f,
                                     0.f,
                                     0.f,
                                     0};
        queue_input_event(NK_EVENT_TOUCH, resource.handle, bytes_of(payload), 1u);
    }
    resource.touch_pointers.clear();
    for (const auto &[device, pointer_id] : resource.tablet_pointers) {
        (void)device;
        emit_touch(resource, pointer_id, NK_TOUCH_CANCEL, NK_TOUCH_TOOL_STYLUS,
                   NSMakePoint(resource.pointer_x, resource.pointer_y), 0, 0.f, 0.f, 0.f, 1u);
    }
    resource.tablet_pointers.clear();
    release_cursor_capture(resource);
}

void emit_touch_event(NKContentView *view, NSEvent *event, nk_touch_action action) {
    auto *resource = static_cast<MacWindowResource *>(view.resource);
    if (!resource)
        return;
    for (NSTouch *touch in [event touchesMatchingPhase:NSTouchPhaseAny inView:view]) {
        if (touch.type != NSTouchTypeDirect)
            continue;
        const NSUInteger identity = touch.identity.hash;
        auto found = resource->touch_pointers.find(identity);
        if (action == NK_TOUCH_BEGIN) {
            if (found == resource->touch_pointers.end()) {
                uint32_t pointer_id = resource->next_touch_pointer_id++;
                if (!pointer_id)
                    pointer_id = resource->next_touch_pointer_id++;
                found = resource->touch_pointers.emplace(identity, pointer_id).first;
            }
        } else if (found == resource->touch_pointers.end()) {
            continue;
        }
        const auto location = [touch locationInView:view];
        emit_touch(*resource, found->second, action, NK_TOUCH_TOOL_FINGER, location,
                   modifiers_from_native(event.modifierFlags));
        if (action == NK_TOUCH_END || action == NK_TOUCH_CANCEL)
            resource->touch_pointers.erase(found);
    }
}

void emit_tablet_event(NKContentView *view, NSEvent *event) {
    auto *resource = static_cast<MacWindowResource *>(view.resource);
    if (!resource)
        return;
    const uint64_t device = static_cast<uint64_t>(event.deviceID);
    auto found = resource->tablet_pointers.find(device);
    bool began = false;
    if (event.pressure > 0.f) {
        if (found == resource->tablet_pointers.end()) {
            uint32_t pointer_id = resource->next_touch_pointer_id++;
            if (!pointer_id)
                pointer_id = resource->next_touch_pointer_id++;
            found = resource->tablet_pointers.emplace(device, pointer_id).first;
            began = true;
        }
    } else if (found == resource->tablet_pointers.end()) {
        return;
    }
    const auto location = local_pointer_position(view, event);
    resource->pointer_x = location.x;
    resource->pointer_y = location.y;
    const auto tool = event.pointingDeviceType == NSPointingDeviceTypeEraser ? NK_TOUCH_TOOL_ERASER
                                                                             : NK_TOUCH_TOOL_STYLUS;
    const float pressure = std::max(0.f, std::min(1.f, event.pressure));
    const auto action =
        event.pressure > 0.f ? (began ? NK_TOUCH_BEGIN : NK_TOUCH_MOVE) : NK_TOUCH_END;
    const auto tilt = event.tilt;
    emit_touch(*resource, found->second, action, tool, location,
               modifiers_from_native(event.modifierFlags), pressure, static_cast<float>(tilt.x),
               static_cast<float>(tilt.y));
    if (action == NK_TOUCH_END)
        resource->tablet_pointers.erase(found);
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
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_DIALOG_RESOURCES_COMPLETE;
        event.request_id = request;
        event.flags = context->kind;
        const auto access =
            context->kind == NK_DIALOG_OPEN_RESOURCE ? NK_RESOURCE_READABLE : NK_RESOURCE_WRITABLE;
        std::vector<nk::platform::ResourceValue> resources;
        resources.reserve(urls.count);
        if (accepted) {
            for (NSURL *url in urls) {
                if (!url.absoluteString.length)
                    continue;
                retain_security_scope(url);
                resources.push_back(nk::platform::resource_from_uri(
                    utf8(url.absoluteString), access, {},
                    url.lastPathComponent ? utf8(url.lastPathComponent) : std::string{}));
            }
        }
        event.data_count = static_cast<uint32_t>(resources.size());
        event.data = nk::platform::resource_payload(accepted, resources);
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
        if (path) {
            NSURL *url = [NSURL URLWithString:path];
            panel.directoryURL =
                url.isFileURL && !url.hasDirectoryPath ? url.URLByDeletingLastPathComponent : url;
        }
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
    if (kind == NK_DIALOG_SAVE_RESOURCE) {
        panel = [NSSavePanel savePanel];
    } else {
        NSOpenPanel *open = [NSOpenPanel openPanel];
        open.canChooseDirectories = kind == NK_DIALOG_SELECT_RESOURCE_DIRECTORY;
        open.canChooseFiles = kind != NK_DIALOG_SELECT_RESOURCE_DIRECTORY;
        open.allowsMultipleSelection =
            kind == NK_DIALOG_OPEN_RESOURCE && (options->flags & NK_DIALOG_ALLOW_MULTIPLE);
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
    if (kind == NK_DIRECTORY_APPLICATION)
        return NSBundle.mainBundle.bundleURL;
    if (kind == NK_DIRECTORY_APPLICATION_STORAGE) {
        NSURL *base = [manager URLForDirectory:NSApplicationSupportDirectory
                                      inDomain:NSUserDomainMask
                             appropriateForURL:nil
                                        create:NO
                                         error:nil];
        const auto id = nk::core::system_application_id();
        NSString *component = id.empty() ? NSBundle.mainBundle.bundleIdentifier
                                         : [NSString stringWithUTF8String:id.c_str()];
        return base && component ? [base URLByAppendingPathComponent:component isDirectory:YES]
                                 : nil;
    }
    if (kind == NK_DIRECTORY_FONTS) {
        NSURL *fonts = [NSURL fileURLWithPath:@"/System/Library/Fonts" isDirectory:YES];
        BOOL is_directory = NO;
        return [manager fileExistsAtPath:fonts.path isDirectory:&is_directory] && is_directory
                   ? fonts
                   : nil;
    }
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

nk_result unsupported(const char *message) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    return fail(NK_ERROR_UNSUPPORTED, message ? message : "macOS service is unavailable");
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
    update_window_surfaces(*resource);
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
    if (r) {
        nk::core::callback_boundary([&] {
            apply_cursor_mode(*r, r->cursor_mode);
            if (r->text_input_active)
                [r->window makeFirstResponder:r->content];
        });
        emit_window_state(*r);
    }
}
- (void)windowDidResignKey:(NSNotification *)notification {
    (void)notification;
    auto *r = static_cast<MacWindowResource *>(_resource);
    if (r) {
        nk::core::callback_boundary([&] { reset_window_input(*r); });
        emit_window_state(*r);
    }
}
- (void)windowDidChangeBackingProperties:(NSNotification *)notification {
    (void)notification;
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource || !resource->handle)
        return;
    update_window_surfaces(*resource);
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_WINDOW_SCALE_CHANGED;
    event.source = resource->handle;
    event.data =
        bytes_of(nk_window_scale_event{static_cast<float>(resource->window.backingScaleFactor)});
    nk::core::push_event(std::move(event));
}
- (void)windowDidChangeScreen:(NSNotification *)notification {
    (void)notification;
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource || !resource->handle)
        return;
    update_window_surfaces(*resource);
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_WINDOW_SCALE_CHANGED;
    event.source = resource->handle;
    event.data =
        bytes_of(nk_window_scale_event{static_cast<float>(resource->window.backingScaleFactor)});
    nk::core::push_event(std::move(event));
}
- (void)windowDidEnterFullScreen:(NSNotification *)notification {
    (void)notification;
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return;
    finish_fullscreen_transition(*resource);
    emit_window_state(*resource);
}
- (void)windowDidExitFullScreen:(NSNotification *)notification {
    (void)notification;
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return;
    finish_fullscreen_transition(*resource);
    emit_window_state(*resource);
}
@end

@implementation NKContentView
- (BOOL)isFlipped {
    return YES;
}
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (void)updateTrackingAreas {
    [super updateTrackingAreas];
    for (NSTrackingArea *area in self.trackingAreas)
        [self removeTrackingArea:area];
    const NSTrackingAreaOptions options = NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved |
                                          NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                       options:options
                                                         owner:self
                                                      userInfo:nil]];
}
- (void)mouseEntered:(NSEvent *)event {
    (void)event;
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (resource) {
        nk::core::callback_boundary([&] {
            resource->hovered = true;
            apply_cursor(*resource);
            emit_pointer_enter(*resource, true);
        });
    }
}
- (void)mouseExited:(NSEvent *)event {
    (void)event;
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (resource)
        nk::core::callback_boundary([&] {
            resource->hovered = false;
            emit_pointer_enter(*resource, false);
        });
}
- (void)mouseMoved:(NSEvent *)event {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        if (resource->pointer_captured) {
            resource->pointer_x += event.deltaX;
            resource->pointer_y -= event.deltaY;
        } else {
            const NSPoint point = local_pointer_position(self, event);
            resource->pointer_x = point.x;
            resource->pointer_y = point.y;
        }
        const nk_pointer_move_event payload{resource->pointer_x, resource->pointer_y};
        queue_input_event(NK_EVENT_POINTER_MOVE, resource->handle, bytes_of(payload));
    });
}
- (void)mouseDragged:(NSEvent *)event {
    [self mouseMoved:event];
}
- (void)rightMouseDragged:(NSEvent *)event {
    [self mouseMoved:event];
}
- (void)otherMouseDragged:(NSEvent *)event {
    [self mouseMoved:event];
}
- (void)mouseDown:(NSEvent *)event {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        const NSPoint point = local_pointer_position(self, event);
        resource->pointer_x = point.x;
        resource->pointer_y = point.y;
        emit_pointer_button(*resource, NK_POINTER_BUTTON_LEFT, NK_INPUT_PRESS,
                            modifiers_from_native(event.modifierFlags));
    });
}
- (void)mouseUp:(NSEvent *)event {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        const NSPoint point = local_pointer_position(self, event);
        resource->pointer_x = point.x;
        resource->pointer_y = point.y;
        emit_pointer_button(*resource, NK_POINTER_BUTTON_LEFT, NK_INPUT_RELEASE,
                            modifiers_from_native(event.modifierFlags));
    });
}
- (void)rightMouseDown:(NSEvent *)event {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        const NSPoint point = local_pointer_position(self, event);
        resource->pointer_x = point.x;
        resource->pointer_y = point.y;
        emit_pointer_button(*resource, NK_POINTER_BUTTON_RIGHT, NK_INPUT_PRESS,
                            modifiers_from_native(event.modifierFlags));
    });
}
- (void)rightMouseUp:(NSEvent *)event {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        const NSPoint point = local_pointer_position(self, event);
        resource->pointer_x = point.x;
        resource->pointer_y = point.y;
        emit_pointer_button(*resource, NK_POINTER_BUTTON_RIGHT, NK_INPUT_RELEASE,
                            modifiers_from_native(event.modifierFlags));
    });
}
- (void)otherMouseDown:(NSEvent *)event {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    const auto button = pointer_button_from_cocoa(event);
    if (!resource || button > NK_POINTER_BUTTON_LAST)
        return;
    nk::core::callback_boundary([&] {
        const NSPoint point = local_pointer_position(self, event);
        resource->pointer_x = point.x;
        resource->pointer_y = point.y;
        emit_pointer_button(*resource, button, NK_INPUT_PRESS,
                            modifiers_from_native(event.modifierFlags));
    });
}
- (void)otherMouseUp:(NSEvent *)event {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    const auto button = pointer_button_from_cocoa(event);
    if (!resource || button > NK_POINTER_BUTTON_LAST)
        return;
    nk::core::callback_boundary([&] {
        const NSPoint point = local_pointer_position(self, event);
        resource->pointer_x = point.x;
        resource->pointer_y = point.y;
        emit_pointer_button(*resource, button, NK_INPUT_RELEASE,
                            modifiers_from_native(event.modifierFlags));
    });
}
- (void)scrollWheel:(NSEvent *)event {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        const NSPoint point = local_pointer_position(self, event);
        resource->pointer_x = point.x;
        resource->pointer_y = point.y;
        const double x = event.hasPreciseScrollingDeltas ? event.scrollingDeltaX : event.deltaX;
        const double y = event.hasPreciseScrollingDeltas ? -event.scrollingDeltaY : -event.deltaY;
        const nk_pointer_scroll_event payload{x, y};
        queue_input_event(NK_EVENT_POINTER_SCROLL, resource->handle, bytes_of(payload));
    });
}
- (void)keyDown:(NSEvent *)event {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        emit_key_transition(*resource, event, event.isARepeat ? NK_INPUT_REPEAT : NK_INPUT_PRESS);
        [self interpretKeyEvents:@[ event ]];
    });
}
- (void)keyUp:(NSEvent *)event {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (resource)
        nk::core::callback_boundary(
            [&] { emit_key_transition(*resource, event, NK_INPUT_RELEASE); });
}
- (void)flagsChanged:(NSEvent *)event {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        const auto key = key_from_cocoa(event.keyCode);
        if (key == NK_KEY_UNKNOWN)
            return;
        const auto action =
            resource->keys[key] == NK_INPUT_PRESS ? NK_INPUT_RELEASE : NK_INPUT_PRESS;
        emit_key_transition(*resource, event, action);
    });
}
- (void)tabletPoint:(NSEvent *)event {
    nk::core::callback_boundary([&] { emit_tablet_event(self, event); });
}
- (void)touchesBeganWithEvent:(NSEvent *)event {
    nk::core::callback_boundary([&] { emit_touch_event(self, event, NK_TOUCH_BEGIN); });
}
- (void)touchesMovedWithEvent:(NSEvent *)event {
    nk::core::callback_boundary([&] { emit_touch_event(self, event, NK_TOUCH_MOVE); });
}
- (void)touchesEndedWithEvent:(NSEvent *)event {
    nk::core::callback_boundary([&] { emit_touch_event(self, event, NK_TOUCH_END); });
}
- (void)touchesCancelledWithEvent:(NSEvent *)event {
    nk::core::callback_boundary([&] { emit_touch_event(self, event, NK_TOUCH_CANCEL); });
}
- (BOOL)hasMarkedText {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    return resource && resource->text_composing;
}
- (NSRange)markedRange {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource || !resource->text_composing)
        return NSMakeRange(NSNotFound, 0);
    if (resource->text_input_state.text &&
        resource->text_composition_start != NK_TEXT_POSITION_NONE)
        return native_range_for_positions(*resource, resource->text_composition_start,
                                          resource->text_composition_end);
    return resource->marked_native_range;
}
- (NSRange)selectedRange {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return NSMakeRange(NSNotFound, 0);
    if (resource->text_input_state.text &&
        resource->text_input_state.selection_start != NK_TEXT_POSITION_NONE)
        return native_range_for_positions(*resource, resource->text_input_state.selection_start,
                                          resource->text_input_state.selection_end);
    return resource->selected_native_range;
}
- (void)setMarkedText:(id)value
        selectedRange:(NSRange)selectionRange
     replacementRange:(NSRange)replacementRange {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        NSString *text_value =
            [value isKindOfClass:[NSAttributedString class]]
                ? [(NSAttributedString *)value string]
                : ([value isKindOfClass:[NSString class]] ? value : [value description]);
        const auto text = utf8(text_value ?: @"");
        std::vector<uint32_t> points;
        if (!decode_utf8(text, points))
            return;
        nk_text_position start = 0;
        nk_text_position end = 0;
        if (!codepoint_range_for_native_range(*resource, replacementRange, start, end)) {
            start = text_replacement_start(*resource);
            end = text_replacement_end(*resource);
        }
        const auto selection_begin =
            selectionRange.location == NSNotFound
                ? points.size()
                : codepoint_index_for_utf16(
                      points,
                      std::min<NSUInteger>(selectionRange.location,
                                           utf16_offset_for_codepoint(points, points.size())));
        const auto selection_finish =
            selectionRange.location == NSNotFound
                ? selection_begin
                : codepoint_index_for_utf16(
                      points,
                      std::min<NSUInteger>(selectionRange.location + selectionRange.length,
                                           utf16_offset_for_codepoint(points, points.size())));
        const auto composition_end = static_cast<nk_text_position>(start + points.size());
        const auto absolute_selection_start =
            static_cast<nk_text_position>(start + selection_begin);
        const auto absolute_selection_end = static_cast<nk_text_position>(start + selection_finish);
        if (resource->text_input_active) {
            apply_text_edit_state(*resource, NK_TEXT_EDIT_COMPOSE, start, end, text,
                                  absolute_selection_start, absolute_selection_end, start,
                                  composition_end);
        } else {
            update_text_snapshot(*resource, start, end, text);
            resource->text_composing = true;
            resource->text_composition_start = start;
            resource->text_composition_end = composition_end;
            resource->text_input_state.composition_start = start;
            resource->text_input_state.composition_end = composition_end;
            resource->text_input_state.selection_start = absolute_selection_start;
            resource->text_input_state.selection_end = absolute_selection_end;
            resource->marked_text = text;
            resource->marked_native_range =
                NSMakeRange(replacementRange.location == NSNotFound ? 0 : replacementRange.location,
                            text_value.length);
            resource->selected_native_range =
                NSMakeRange(resource->marked_native_range.location +
                                (selectionRange.location == NSNotFound ? text_value.length
                                                                       : selectionRange.location),
                            selectionRange.location == NSNotFound ? 0 : selectionRange.length);
        }
    });
}
- (void)unmarkText {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        if (resource->text_input_active)
            finish_text_composition(*resource);
        else {
            resource->text_composing = false;
            resource->text_composition_start = NK_TEXT_POSITION_NONE;
            resource->text_composition_end = NK_TEXT_POSITION_NONE;
            resource->text_input_state.composition_start = NK_TEXT_POSITION_NONE;
            resource->text_input_state.composition_end = NK_TEXT_POSITION_NONE;
            resource->marked_text.clear();
            resource->marked_native_range = NSMakeRange(NSNotFound, 0);
        }
    });
}
- (NSArray<NSString *> *)validAttributesForMarkedText {
    return @[];
}
- (NSAttributedString *)attributedSubstringForProposedRange:(NSRange)range
                                                actualRange:(NSRangePointer)actualRange {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource || !resource->text_input_state.text || range.location == NSNotFound)
        return nil;
    NSString *text = string(resource->text_input_text.c_str());
    if (static_cast<uint64_t>(range.location) + range.length > text.length)
        return nil;
    if (actualRange)
        *actualRange = range;
    return [[NSAttributedString alloc] initWithString:[text substringWithRange:range]];
}
- (void)insertText:(id)value replacementRange:(NSRange)replacementRange {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        NSString *text_value =
            [value isKindOfClass:[NSAttributedString class]]
                ? [(NSAttributedString *)value string]
                : ([value isKindOfClass:[NSString class]] ? value : [value description]);
        const auto text = utf8(text_value ?: @"");
        std::vector<uint32_t> points;
        if (!decode_utf8(text, points))
            return;
        if (resource->text_input_active) {
            nk_text_position start = 0;
            nk_text_position end = 0;
            if (!codepoint_range_for_native_range(*resource, replacementRange, start, end)) {
                start = text_replacement_start(*resource);
                end = text_replacement_end(*resource);
            }
            const auto selection = static_cast<nk_text_position>(start + points.size());
            apply_text_edit_state(*resource, NK_TEXT_EDIT_COMMIT, start, end, text, selection,
                                  selection, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
        } else {
            emit_committed_text(*resource, text);
        }
    });
}
- (NSUInteger)characterIndexForPoint:(NSPoint)screenPoint {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource)
        return NSNotFound;
    const NSPoint windowPoint = [self.window convertPointFromScreen:screenPoint];
    const NSPoint localPoint = [self convertPoint:windowPoint fromView:nil];
    const NSRect caret =
        NSMakeRect(resource->text_input_state.cursor_x, resource->text_input_state.cursor_y,
                   std::max(1.f, resource->text_input_state.cursor_width),
                   std::max(1.f, resource->text_input_state.cursor_height));
    if (!NSPointInRect(localPoint, caret))
        return NSNotFound;
    return self.selectedRange.location;
}
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actualRange {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource || !self.window)
        return NSZeroRect;
    if (actualRange)
        *actualRange = range;
    NSRect rect =
        NSMakeRect(resource->text_input_state.cursor_x, resource->text_input_state.cursor_y,
                   std::max(1.f, resource->text_input_state.cursor_width),
                   std::max(1.f, resource->text_input_state.cursor_height));
    return [self.window convertRectToScreen:[self convertRect:rect toView:nil]];
}
- (void)doCommandBySelector:(SEL)selector {
    auto *resource = static_cast<MacWindowResource *>(_resource);
    if (!resource || !resource->text_input_active)
        return;
    nk::core::callback_boundary([&] {
        const auto start = resource->text_input_state.selection_start;
        const auto end = resource->text_input_state.selection_end;
        if (selector == @selector(deleteBackward:) || selector == @selector(deleteForward:)) {
            const bool backward = selector == @selector(deleteBackward:);
            nk_text_position replace_start = std::min(start, end);
            nk_text_position replace_end = std::max(start, end);
            if (replace_start == replace_end) {
                if (backward && replace_start > 0)
                    --replace_start;
                else if (!backward && replace_end < resource->text_input_state.document_length)
                    ++replace_end;
            }
            if (replace_start != replace_end)
                apply_text_edit_state(*resource, NK_TEXT_EDIT_DELETE, replace_start, replace_end,
                                      {}, replace_start, replace_start, NK_TEXT_POSITION_NONE,
                                      NK_TEXT_POSITION_NONE);
        } else if (selector == @selector(insertNewline:)) {
            apply_text_edit_state(*resource, NK_TEXT_EDIT_COMMIT, text_replacement_start(*resource),
                                  text_replacement_end(*resource), "\n",
                                  text_replacement_start(*resource) + 1,
                                  text_replacement_start(*resource) + 1, NK_TEXT_POSITION_NONE,
                                  NK_TEXT_POSITION_NONE);
        } else if (selector == @selector(insertTab:)) {
            apply_text_edit_state(*resource, NK_TEXT_EDIT_COMMIT, text_replacement_start(*resource),
                                  text_replacement_end(*resource), "\t",
                                  text_replacement_start(*resource) + 1,
                                  text_replacement_start(*resource) + 1, NK_TEXT_POSITION_NONE,
                                  NK_TEXT_POSITION_NONE);
        }
    });
}
- (NSInteger)conversationIdentifier {
    return static_cast<NSInteger>(reinterpret_cast<uintptr_t>(self));
}
- (NSWindowLevel)windowLevel {
    return self.window.level;
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

@implementation NKMacAccessibilityElement
- (BOOL)accessibilityIsIgnored {
    return NO;
}

- (id)accessibilityAttributeValue:(NSString *)attribute {
    auto resource = surface(self.surface);
    if (!resource)
        return nil;
    const auto found = resource->accessibility_nodes.find(self.node);
    if (found == resource->accessibility_nodes.end())
        return nil;
    const auto &node = found->second;
    if ([attribute isEqualToString:NSAccessibilityRoleAttribute])
        return mac_accessibility_role(node.role);
    if ([attribute isEqualToString:NSAccessibilityRoleDescriptionAttribute])
        return NSAccessibilityRoleDescription(mac_accessibility_role(node.role), nil);
    if ([attribute isEqualToString:NSAccessibilityTitleAttribute] ||
        [attribute isEqualToString:NSAccessibilityDescriptionAttribute])
        return node.label.empty() ? nil : string(node.label.c_str());
    if ([attribute isEqualToString:NSAccessibilityValueAttribute]) {
        if (node.role == NK_ACCESSIBILITY_CHECKBOX || node.role == NK_ACCESSIBILITY_SWITCH)
            return @((node.states & NK_ACCESSIBILITY_CHECKED) != 0);
        if (node.role == NK_ACCESSIBILITY_SLIDER || node.role == NK_ACCESSIBILITY_PROGRESS_BAR)
            return @(node.numeric_value);
        return node.value.empty() ? nil : string(node.value.c_str());
    }
    if ([attribute isEqualToString:NSAccessibilityValueDescriptionAttribute])
        return node.value.empty() ? nil : string(node.value.c_str());
    if ([attribute isEqualToString:NSAccessibilityEnabledAttribute])
        return @((node.states & NK_ACCESSIBILITY_DISABLED) == 0);
    if ([attribute isEqualToString:NSAccessibilityFocusedAttribute])
        return
            @(resource->accessibility_focus == node.id || (node.states & NK_ACCESSIBILITY_FOCUSED));
    if ([attribute isEqualToString:NSAccessibilitySelectedAttribute])
        return @((node.states & NK_ACCESSIBILITY_SELECTED) != 0);
    if ([attribute isEqualToString:NSAccessibilityExpandedAttribute])
        return @((node.states & NK_ACCESSIBILITY_EXPANDED) != 0);
    if ([attribute isEqualToString:NSAccessibilityRequiredAttribute])
        return @((node.states & NK_ACCESSIBILITY_REQUIRED) != 0);
    if ([attribute isEqualToString:NSAccessibilityModalAttribute])
        return @((node.states & NK_ACCESSIBILITY_MODAL) != 0);
    if ([attribute isEqualToString:NSAccessibilityContainsProtectedContentAttribute])
        return @((node.states & NK_ACCESSIBILITY_PASSWORD) != 0);
    if ([attribute isEqualToString:NSAccessibilityOrientationAttribute]) {
        if (node.orientation == NK_ACCESSIBILITY_ORIENTATION_HORIZONTAL)
            return NSAccessibilityHorizontalOrientationValue;
        if (node.orientation == NK_ACCESSIBILITY_ORIENTATION_VERTICAL)
            return NSAccessibilityVerticalOrientationValue;
        return NSAccessibilityUnknownOrientationValue;
    }
    if ([attribute isEqualToString:NKMacAccessibilityFrameAttribute]) {
        const NSRect frame =
            mac_accessibility_screen_frame(*resource, node.x, node.y, node.width, node.height);
        return [NSValue valueWithRect:frame];
    }
    if ([attribute isEqualToString:NSAccessibilityPositionAttribute]) {
        const NSRect frame =
            mac_accessibility_screen_frame(*resource, node.x, node.y, node.width, node.height);
        return [NSValue valueWithPoint:frame.origin];
    }
    if ([attribute isEqualToString:NSAccessibilitySizeAttribute])
        return [NSValue valueWithSize:NSMakeSize(node.width, node.height)];
    if ([attribute isEqualToString:NSAccessibilityParentAttribute])
        return self.nativeParent;
    if ([attribute isEqualToString:NSAccessibilityChildrenAttribute])
        return self.nativeChildren ?: @[];
    if ([attribute isEqualToString:NSAccessibilitySelectedChildrenAttribute]) {
        NSMutableArray<NKMacAccessibilityElement *> *selected = [NSMutableArray array];
        for (NKMacAccessibilityElement *element in self.nativeChildren) {
            const auto child = resource->accessibility_nodes.find(element.node);
            if (child != resource->accessibility_nodes.end() &&
                (child->second.states & NK_ACCESSIBILITY_SELECTED))
                [selected addObject:element];
        }
        return selected;
    }
    if ([attribute isEqualToString:NSAccessibilityMinValueAttribute])
        return @(node.numeric_minimum);
    if ([attribute isEqualToString:NSAccessibilityMaxValueAttribute])
        return @(node.numeric_maximum);
    if ([attribute isEqualToString:NSAccessibilitySelectedTextRangeAttribute]) {
        const NSRange range = mac_accessibility_value_range(node);
        return range.location == NSNotFound ? nil : [NSValue valueWithRange:range];
    }
    if ([attribute isEqualToString:NSAccessibilityNumberOfCharactersAttribute] ||
        [attribute isEqualToString:NSAccessibilityVisibleCharacterRangeAttribute]) {
        std::vector<uint32_t> codepoints;
        if (!decode_utf8(node.value, codepoints))
            return nil;
        const NSUInteger length = utf16_offset_for_codepoint(codepoints, codepoints.size());
        if ([attribute isEqualToString:NSAccessibilityNumberOfCharactersAttribute])
            return @(length);
        return [NSValue valueWithRange:NSMakeRange(0, length)];
    }
    return nil;
}

- (id)accessibilityAttributeValue:(NSString *)attribute forParameter:(id)parameter {
    auto resource = surface(self.surface);
    if (!resource)
        return nil;
    const auto found = resource->accessibility_nodes.find(self.node);
    if (found == resource->accessibility_nodes.end())
        return nil;
    const auto &node = found->second;
    if (![parameter isKindOfClass:[NSValue class]])
        return nil;
    if ([attribute isEqualToString:NSAccessibilityStringForRangeParameterizedAttribute]) {
        const NSRange range = [parameter rangeValue];
        NSString *value = string(node.value.c_str());
        if (!value || range.location == NSNotFound || range.location > value.length ||
            range.length > value.length - range.location)
            return nil;
        return [value substringWithRange:range];
    }
    if ([attribute isEqualToString:NSAccessibilityBoundsForRangeParameterizedAttribute]) {
        const NSRange requested = [parameter rangeValue];
        std::vector<uint32_t> codepoints;
        if (!decode_utf8(node.value, codepoints))
            return nil;
        for (const auto &text_range : node.text_ranges) {
            if (text_range.start < node.text_start || text_range.end < text_range.start)
                continue;
            const auto local_start = static_cast<uint32_t>(text_range.start - node.text_start);
            const auto local_end = static_cast<uint32_t>(text_range.end - node.text_start);
            const NSUInteger start = utf16_offset_for_codepoint(codepoints, local_start);
            const NSRange native =
                NSMakeRange(start, utf16_offset_for_codepoint(codepoints, local_end) - start);
            if (NSIntersectionRange(requested, native).length == 0)
                continue;
            return [NSValue
                valueWithRect:mac_accessibility_screen_frame(*resource, text_range.x, text_range.y,
                                                             text_range.width, text_range.height)];
        }
        return nil;
    }
    if ([attribute isEqualToString:NSAccessibilityRangeForPositionParameterizedAttribute]) {
        const NSPoint point = [parameter pointValue];
        std::vector<uint32_t> codepoints;
        if (!decode_utf8(node.value, codepoints))
            return nil;
        for (const auto &text_range : node.text_ranges) {
            const NSRect frame = mac_accessibility_screen_frame(
                *resource, text_range.x, text_range.y, text_range.width, text_range.height);
            if (!NSPointInRect(point, frame) || text_range.start < node.text_start ||
                text_range.end < text_range.start)
                continue;
            const auto local_start = static_cast<uint32_t>(text_range.start - node.text_start);
            const auto local_end = static_cast<uint32_t>(text_range.end - node.text_start);
            const NSUInteger start = utf16_offset_for_codepoint(codepoints, local_start);
            return [NSValue valueWithRange:NSMakeRange(start, utf16_offset_for_codepoint(
                                                                  codepoints, local_end) -
                                                                  start)];
        }
    }
    return nil;
}

- (NSArray<NSString *> *)accessibilityAttributeNames {
    return @[
        NSAccessibilityRoleAttribute,
        NSAccessibilityRoleDescriptionAttribute,
        NSAccessibilityTitleAttribute,
        NSAccessibilityDescriptionAttribute,
        NSAccessibilityValueAttribute,
        NSAccessibilityValueDescriptionAttribute,
        NSAccessibilityEnabledAttribute,
        NSAccessibilityFocusedAttribute,
        NSAccessibilitySelectedAttribute,
        NSAccessibilityExpandedAttribute,
        NSAccessibilityRequiredAttribute,
        NSAccessibilityModalAttribute,
        NSAccessibilityContainsProtectedContentAttribute,
        NSAccessibilityOrientationAttribute,
        NKMacAccessibilityFrameAttribute,
        NSAccessibilityPositionAttribute,
        NSAccessibilitySizeAttribute,
        NSAccessibilityParentAttribute,
        NSAccessibilityChildrenAttribute,
        NSAccessibilitySelectedChildrenAttribute,
        NSAccessibilityMinValueAttribute,
        NSAccessibilityMaxValueAttribute,
        NSAccessibilitySelectedTextRangeAttribute,
        NSAccessibilityNumberOfCharactersAttribute,
        NSAccessibilityVisibleCharacterRangeAttribute,
        NSAccessibilityStringForRangeParameterizedAttribute,
        NSAccessibilityBoundsForRangeParameterizedAttribute,
        NSAccessibilityRangeForPositionParameterizedAttribute
    ];
}

- (BOOL)accessibilityIsAttributeSettable:(NSString *)attribute {
    auto resource = surface(self.surface);
    if (!resource)
        return NO;
    const auto found = resource->accessibility_nodes.find(self.node);
    if (found == resource->accessibility_nodes.end())
        return NO;
    const auto actions = found->second.actions;
    if ([attribute isEqualToString:NSAccessibilityFocusedAttribute])
        return (actions & NK_ACCESSIBILITY_CAN_FOCUS) != 0;
    if ([attribute isEqualToString:NSAccessibilityValueAttribute])
        return (actions & NK_ACCESSIBILITY_CAN_SET_VALUE) != 0;
    if ([attribute isEqualToString:NSAccessibilitySelectedTextRangeAttribute])
        return (actions & NK_ACCESSIBILITY_CAN_SET_SELECTION) != 0;
    if ([attribute isEqualToString:NSAccessibilitySelectedAttribute])
        return (actions & (NK_ACCESSIBILITY_CAN_SELECT | NK_ACCESSIBILITY_CAN_DESELECT)) != 0;
    if ([attribute isEqualToString:NSAccessibilityExpandedAttribute])
        return (actions & (NK_ACCESSIBILITY_CAN_EXPAND | NK_ACCESSIBILITY_CAN_COLLAPSE)) != 0;
    return NO;
}

- (void)accessibilitySetValue:(id)value forAttribute:(NSString *)attribute {
    auto resource = surface(self.surface);
    if (!resource)
        return;
    const auto found = resource->accessibility_nodes.find(self.node);
    if (found == resource->accessibility_nodes.end())
        return;
    const auto &node = found->second;
    if ([attribute isEqualToString:NSAccessibilityFocusedAttribute] &&
        [value respondsToSelector:@selector(boolValue)]) {
        const auto action =
            [value boolValue] ? NK_ACCESSIBILITY_ACTION_FOCUS : NK_ACCESSIBILITY_ACTION_CLEAR_FOCUS;
        if (emit_mac_accessibility_action(self.surface, self.node, action) == NK_OK)
            NSAccessibilityPostNotification(self,
                                            NSAccessibilityFocusedUIElementChangedNotification);
    } else if ([attribute isEqualToString:NSAccessibilityValueAttribute]) {
        const std::string text =
            utf8([value isKindOfClass:[NSString class]] ? value : [value description]);
        emit_mac_accessibility_action(self.surface, self.node, NK_ACCESSIBILITY_ACTION_SET_VALUE,
                                      text);
    } else if ([attribute isEqualToString:NSAccessibilitySelectedTextRangeAttribute] &&
               [value isKindOfClass:[NSValue class]]) {
        nk_accessibility_text_position start = NK_ACCESSIBILITY_TEXT_POSITION_NONE;
        nk_accessibility_text_position end = NK_ACCESSIBILITY_TEXT_POSITION_NONE;
        if (mac_accessibility_codepoint_range(node, [value rangeValue], start, end))
            emit_mac_accessibility_action(self.surface, self.node,
                                          NK_ACCESSIBILITY_ACTION_SET_SELECTION, {}, start, end);
    } else if ([attribute isEqualToString:NSAccessibilitySelectedAttribute] &&
               [value respondsToSelector:@selector(boolValue)]) {
        emit_mac_accessibility_action(self.surface, self.node,
                                      [value boolValue] ? NK_ACCESSIBILITY_ACTION_SELECT
                                                        : NK_ACCESSIBILITY_ACTION_DESELECT);
    } else if ([attribute isEqualToString:NSAccessibilityExpandedAttribute] &&
               [value respondsToSelector:@selector(boolValue)]) {
        emit_mac_accessibility_action(self.surface, self.node,
                                      [value boolValue] ? NK_ACCESSIBILITY_ACTION_EXPAND
                                                        : NK_ACCESSIBILITY_ACTION_COLLAPSE);
    }
}

- (NSArray<NSString *> *)accessibilityActionNames {
    auto resource = surface(self.surface);
    if (!resource)
        return @[];
    const auto found = resource->accessibility_nodes.find(self.node);
    if (found == resource->accessibility_nodes.end())
        return @[];
    const auto actions = found->second.actions;
    NSMutableArray<NSString *> *result = [NSMutableArray array];
    if (actions & NK_ACCESSIBILITY_CAN_ACTIVATE)
        [result addObject:NSAccessibilityPressAction];
    if (actions & NK_ACCESSIBILITY_CAN_INCREMENT)
        [result addObject:NSAccessibilityIncrementAction];
    if (actions & NK_ACCESSIBILITY_CAN_DECREMENT)
        [result addObject:NSAccessibilityDecrementAction];
    if (actions & NK_ACCESSIBILITY_CAN_SHOW_CONTEXT_MENU)
        [result addObject:NSAccessibilityShowMenuAction];
    if (actions & NK_ACCESSIBILITY_CAN_SCROLL_INTO_VIEW)
        [result addObject:NKMacAccessibilityScrollToVisibleAction];
    auto add = [&](nk_accessibility_actions bit, NSString *name) {
        if (actions & bit)
            [result addObject:name];
    };
    add(NK_ACCESSIBILITY_CAN_SET_VALUE, NKMacAccessibilitySetValueAction);
    add(NK_ACCESSIBILITY_CAN_SET_SELECTION, NKMacAccessibilitySetSelectionAction);
    add(NK_ACCESSIBILITY_CAN_SCROLL_FORWARD, NKMacAccessibilityScrollForwardAction);
    add(NK_ACCESSIBILITY_CAN_SCROLL_BACKWARD, NKMacAccessibilityScrollBackwardAction);
    add(NK_ACCESSIBILITY_CAN_MOVE_NEXT, NKMacAccessibilityMoveNextAction);
    add(NK_ACCESSIBILITY_CAN_MOVE_PREVIOUS, NKMacAccessibilityMovePreviousAction);
    add(NK_ACCESSIBILITY_CAN_TOGGLE, NKMacAccessibilityToggleAction);
    add(NK_ACCESSIBILITY_CAN_SELECT, NKMacAccessibilitySelectAction);
    add(NK_ACCESSIBILITY_CAN_DESELECT, NKMacAccessibilityDeselectAction);
    add(NK_ACCESSIBILITY_CAN_EXPAND, NKMacAccessibilityExpandAction);
    add(NK_ACCESSIBILITY_CAN_COLLAPSE, NKMacAccessibilityCollapseAction);
    add(NK_ACCESSIBILITY_CAN_DISMISS, NKMacAccessibilityDismissAction);
    return result;
}

- (void)accessibilityPerformAction:(NSString *)action {
    nk_accessibility_action requested = 0;
    if ([action isEqualToString:NSAccessibilityPressAction])
        requested = NK_ACCESSIBILITY_ACTION_ACTIVATE;
    else if ([action isEqualToString:NSAccessibilityIncrementAction])
        requested = NK_ACCESSIBILITY_ACTION_INCREMENT;
    else if ([action isEqualToString:NSAccessibilityDecrementAction])
        requested = NK_ACCESSIBILITY_ACTION_DECREMENT;
    else if ([action isEqualToString:NSAccessibilityShowMenuAction])
        requested = NK_ACCESSIBILITY_ACTION_SHOW_CONTEXT_MENU;
    else if ([action isEqualToString:NKMacAccessibilityScrollToVisibleAction])
        requested = NK_ACCESSIBILITY_ACTION_SCROLL_INTO_VIEW;
    else
        requested = mac_accessibility_action_for_name(action);
    if (requested)
        emit_mac_accessibility_action(self.surface, self.node, requested);
}

- (NSString *)accessibilityActionDescription:(NSString *)action {
    if ([action isEqualToString:NSAccessibilityPressAction])
        return @"Activate";
    if ([action isEqualToString:NSAccessibilityIncrementAction])
        return @"Increment";
    if ([action isEqualToString:NSAccessibilityDecrementAction])
        return @"Decrement";
    if ([action isEqualToString:NSAccessibilityShowMenuAction])
        return @"Show menu";
    if ([action isEqualToString:NKMacAccessibilityScrollToVisibleAction])
        return @"Scroll into view";
    if ([action isEqualToString:NKMacAccessibilitySetValueAction])
        return @"Set value";
    if ([action isEqualToString:NKMacAccessibilitySetSelectionAction])
        return @"Set selection";
    if ([action isEqualToString:NKMacAccessibilityToggleAction])
        return @"Toggle";
    if ([action isEqualToString:NKMacAccessibilitySelectAction])
        return @"Select";
    if ([action isEqualToString:NKMacAccessibilityDeselectAction])
        return @"Deselect";
    if ([action isEqualToString:NKMacAccessibilityExpandAction])
        return @"Expand";
    if ([action isEqualToString:NKMacAccessibilityCollapseAction])
        return @"Collapse";
    if ([action isEqualToString:NKMacAccessibilityDismissAction])
        return @"Dismiss";
    if ([action isEqualToString:NKMacAccessibilityMoveNextAction])
        return @"Next";
    if ([action isEqualToString:NKMacAccessibilityMovePreviousAction])
        return @"Previous";
    if ([action isEqualToString:NKMacAccessibilityScrollForwardAction])
        return @"Scroll forward";
    if ([action isEqualToString:NKMacAccessibilityScrollBackwardAction])
        return @"Scroll backward";
    return action;
}
@end

@implementation NKMacAccessibilityContainer
- (instancetype)initWithFrame:(NSRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.elements = @[];
        self.allElements = @[];
        self.accessibilityElement = YES;
        self.accessibilityRole = NSAccessibilityGroupRole;
        self.hidden = NO;
    }
    return self;
}

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)acceptsFirstResponder {
    return NO;
}

- (NSView *)hitTest:(NSPoint)point {
    (void)point;
    return nil;
}

- (BOOL)accessibilityIsIgnored {
    return NO;
}

- (id)accessibilityAttributeValue:(NSString *)attribute {
    if ([attribute isEqualToString:NSAccessibilityRoleAttribute])
        return NSAccessibilityGroupRole;
    if ([attribute isEqualToString:NSAccessibilityRoleDescriptionAttribute])
        return NSAccessibilityRoleDescription(NSAccessibilityGroupRole, nil);
    if ([attribute isEqualToString:NSAccessibilityChildrenAttribute])
        return self.elements ?: @[];
    if ([attribute isEqualToString:NSAccessibilityEnabledAttribute])
        return @YES;
    if ([attribute isEqualToString:NKMacAccessibilityFrameAttribute]) {
        const NSRect local = self.bounds;
        if (self.window)
            return [NSValue valueWithRect:[self.window convertRectToScreen:[self convertRect:local
                                                                                      toView:nil]]];
        return [NSValue valueWithRect:local];
    }
    if ([attribute isEqualToString:NSAccessibilityParentAttribute])
        return self.superview;
    if ([attribute isEqualToString:NSAccessibilityFocusedUIElementAttribute]) {
        auto resource = surface(self.surface);
        if (!resource || resource->accessibility_focus == NK_ACCESSIBILITY_ROOT)
            return nil;
        for (NKMacAccessibilityElement *element in self.allElements)
            if (element.node == resource->accessibility_focus)
                return element;
        return nil;
    }
    return nil;
}

- (NSArray<NSString *> *)accessibilityAttributeNames {
    return @[
        NSAccessibilityRoleAttribute, NSAccessibilityRoleDescriptionAttribute,
        NSAccessibilityChildrenAttribute, NSAccessibilityEnabledAttribute,
        NKMacAccessibilityFrameAttribute, NSAccessibilityParentAttribute,
        NSAccessibilityFocusedUIElementAttribute
    ];
}

- (BOOL)accessibilityIsAttributeSettable:(NSString *)attribute {
    (void)attribute;
    return NO;
}

- (id)accessibilityHitTest:(NSPoint)point {
    for (NKMacAccessibilityElement *element in self.allElements) {
        NSValue *frame = [element accessibilityAttributeValue:NKMacAccessibilityFrameAttribute];
        if (frame && NSPointInRect(point, frame.rectValue))
            return element;
    }
    return self;
}
@end

@implementation NKMetalSurfaceView
- (BOOL)isFlipped {
    return YES;
}
- (BOOL)acceptsFirstResponder {
    return NO;
}
- (NSView *)hitTest:(NSPoint)point {
    (void)point;
    return nil;
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

namespace {
__strong id keep_awake_activity = nil;
}

namespace nk::core::system_backend {

nk_result keep_awake_apply(bool enabled) noexcept {
    @autoreleasepool {
        NSProcessInfo *process = NSProcessInfo.processInfo;
        if (enabled && !keep_awake_activity) {
            keep_awake_activity =
                [process beginActivityWithOptions:NSActivityIdleDisplaySleepDisabled
                                           reason:@"NativeKit keep-awake"];
            return keep_awake_activity ? NK_OK : NK_ERROR_UNSUPPORTED;
        }
        if (!enabled && keep_awake_activity) {
            [process endActivity:keep_awake_activity];
            keep_awake_activity = nil;
        }
        return NK_OK;
    }
}

nk_result get_orientation(nk_system_orientation &out_orientation) noexcept {
    const auto size = out_orientation.struct_size;
    out_orientation = {};
    out_orientation.struct_size = size;
    const auto display = CGMainDisplayID();
    const auto rotation = static_cast<int>(std::lround(CGDisplayRotation(display)));
    if (rotation == 90)
        out_orientation.display = NK_ORIENTATION_PORTRAIT;
    else if (rotation == 180)
        out_orientation.display = NK_ORIENTATION_LANDSCAPE_LEFT;
    else if (rotation == 270)
        out_orientation.display = NK_ORIENTATION_PORTRAIT_UPSIDE_DOWN;
    else {
        const auto width = CGDisplayPixelsWide(display);
        const auto height = CGDisplayPixelsHigh(display);
        out_orientation.display = width == height  ? NK_ORIENTATION_UNKNOWN
                                  : width > height ? NK_ORIENTATION_LANDSCAPE_RIGHT
                                                   : NK_ORIENTATION_PORTRAIT;
    }
    return NK_OK;
}

nk_result get_string(nk_system_string_kind kind, std::string &out_value) {
    switch (kind) {
    case NK_SYSTEM_STRING_PLATFORM_VERSION: {
        const auto *value = NSProcessInfo.processInfo.operatingSystemVersionString.UTF8String;
        if (!value)
            return NK_ERROR_UNSUPPORTED;
        out_value = value;
        return NK_OK;
    }
    case NK_SYSTEM_STRING_DEVICE_VENDOR:
        out_value = "Apple";
        return NK_OK;
    default:
        return NK_ERROR_UNSUPPORTED;
    }
}

} // namespace nk::core::system_backend

namespace nk::backend {
void pump_events() noexcept {
    nk::macos_joystick::pump();
    @autoreleasepool {
        NSEvent *event = nil;
        while ((event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                           untilDate:NSDate.distantPast
                                              inMode:NSDefaultRunLoopMode
                                             dequeue:YES]))
            [NSApp sendEvent:event];
        [NSApp updateWindows];
    }
    poll_monitor_orientations();
}

void shutdown() noexcept {
    nk::macos_joystick::shutdown();
    NSMutableArray<NSString *> *notification_identifiers = [NSMutableArray array];
    {
        std::lock_guard lock(notifications_mutex);
        for (const auto &[request, generation] : notifications) {
            (void)generation;
            [notification_identifiers addObject:notification_identifier(request)];
        }
        notifications.clear();
    }
    if (notification_center_initialized) {
        UNUserNotificationCenter *notification_center =
            UNUserNotificationCenter.currentNotificationCenter;
        [notification_center
            removePendingNotificationRequestsWithIdentifiers:notification_identifiers];
        [notification_center removeDeliveredNotificationsWithIdentifiers:notification_identifiers];
        if (notification_center.delegate == notification_delegate)
            notification_center.delegate = nil;
        notification_center_initialized = false;
    }
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
    release_security_scopes();
    dialogs.clear();
    monitor_handles.clear();
    monitor_orientations.clear();
    nk::core::handles().clear();
}
} // namespace nk::backend

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_WINDOW | NK_CAP_CLIPBOARD | NK_CAP_WEBVIEW | NK_CAP_DRAG_DROP | NK_CAP_SHELL |
           NK_CAP_SYSTEM_APPEARANCE | NK_CAP_EXPORT_NATIVE_WINDOW | NK_CAP_NOTIFICATION |
           NK_CAP_RESOURCE_IO | NK_CAP_INPUT | NK_CAP_CURSOR | NK_CAP_POINTER_CAPTURE |
           NK_CAP_WINDOW_GEOMETRY | NK_CAP_WINDOW_STYLING | NK_CAP_METAL_SURFACE | NK_CAP_MONITOR |
           NK_CAP_MONITOR_FULLSCREEN | NK_CAP_JOYSTICK | NK_CAP_SYSTEM_INFO |
           NK_CAP_APPLICATION_PATH | NK_CAP_APPLICATION_STORAGE | NK_CAP_SYSTEM_FONTS |
           NK_CAP_KEEP_AWAKE | NK_CAP_DISPLAY_ORIENTATION | NK_CAP_ACCESSIBILITY |
           NK_CAP_RESOURCE_SHARING | NK_CAP_WRAP_NATIVE_WINDOW | nk::core::optional_capabilities();
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
        resource->content.acceptsTouchEvents = YES;
        resource->content.wantsRestingTouches = YES;
        resource->window.acceptsMouseMovedEvents = YES;
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
    for (const nk_handle surface_handle : resource->surfaces) {
        auto child_surface = surface(surface_handle);
        if (child_surface && (child_surface->share_dependents ||
                              nk_core_graphics_device_has_references(
                                  nk_graphics_device{child_surface->device_handle})))
            return fail(NK_ERROR_INVALID_REQUEST,
                        "window still owns a shared or retained graphics surface");
    }
    const auto owned_windows = resource->owned_windows;
    for (const auto owned : owned_windows)
        nk_window_destroy(owned);
    const auto surfaces = resource->surfaces;
    for (auto iter = surfaces.rbegin(); iter != surfaces.rend(); ++iter)
        nk_surface_destroy(*iter);
    const auto children = resource->children;
    for (const auto child : children)
        nk_webview_destroy(child);
    cancel_dialogs_for_parent(resource->window);
    if (!resource->owns_window) {
        resource->window = nil;
        resource->native_content = nil;
        nk::core::handles().erase(handle, nk::core::ResourceType::window);
        return NK_OK;
    }
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

nk_result NK_CALL nk_window_get_content_scale(nk_handle handle,
                                              nk_window_content_scale *out_scale) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_scale || out_scale->struct_size < sizeof(*out_scale))
        return fail(NK_ERROR_INVALID_ARGUMENT, "content scale output is missing or too small");
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const auto size = out_scale->struct_size;
    const float scale = static_cast<float>(resource->window.backingScaleFactor);
    *out_scale = {};
    out_scale->struct_size = size;
    out_scale->x = scale;
    out_scale->y = scale;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_position(nk_handle handle, int32_t *out_x, int32_t *out_y) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_x || !out_y)
        return fail(NK_ERROR_INVALID_ARGUMENT, "window position outputs must not be null");
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const NSPoint origin = resource->window.frame.origin;
    *out_x = static_cast<int32_t>(std::lround(origin.x));
    *out_y = static_cast<int32_t>(std::lround(origin.y));
    return NK_OK;
}

nk_result NK_CALL nk_window_get_size(nk_handle handle, int32_t *out_width, int32_t *out_height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_width || !out_height)
        return fail(NK_ERROR_INVALID_ARGUMENT, "window size outputs must not be null");
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const NSSize size = resource->window.contentView.bounds.size;
    *out_width = static_cast<int32_t>(std::lround(size.width));
    *out_height = static_cast<int32_t>(std::lround(size.height));
    return NK_OK;
}

nk_result NK_CALL nk_window_get_framebuffer_size(nk_handle handle, int32_t *out_width,
                                                 int32_t *out_height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_width || !out_height)
        return fail(NK_ERROR_INVALID_ARGUMENT, "framebuffer size outputs must not be null");
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const NSSize size = resource->window.contentView.bounds.size;
    const CGFloat scale = resource->window.backingScaleFactor;
    *out_width = static_cast<int32_t>(std::lround(size.width * scale));
    *out_height = static_cast<int32_t>(std::lround(size.height * scale));
    return NK_OK;
}

nk_result NK_CALL nk_window_get_frame_extents(nk_handle handle,
                                              nk_window_frame_extents *out_extents) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_extents || out_extents->struct_size < sizeof(*out_extents))
        return fail(NK_ERROR_INVALID_ARGUMENT,
                    "window frame extents output is missing or too small");
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    const NSRect frame = resource->window.frame;
    NSView *content_view = window_content_view(*resource);
    if (!content_view)
        return fail(NK_ERROR_UNSUPPORTED, "Cocoa window has no content view");
    const NSRect content = [resource->window convertRectToScreen:content_view.frame];
    const auto size = out_extents->struct_size;
    *out_extents = {};
    out_extents->struct_size = size;
    out_extents->left = static_cast<int32_t>(std::lround(content.origin.x - frame.origin.x));
    out_extents->bottom = static_cast<int32_t>(std::lround(content.origin.y - frame.origin.y));
    out_extents->right = static_cast<int32_t>(std::lround(NSMaxX(frame) - NSMaxX(content)));
    out_extents->top = static_cast<int32_t>(std::lround(NSMaxY(frame) - NSMaxY(content)));
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

nk_result NK_CALL nk_key_get_state(nk_handle handle, nk_key key, nk_input_action *out_action) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_action || key == NK_KEY_UNKNOWN || key > NK_KEY_LAST)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid key state query");
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    *out_action = resource->keys[key];
    return NK_OK;
}

nk_result NK_CALL nk_pointer_button_get_state(nk_handle handle, nk_pointer_button button,
                                              nk_input_action *out_action) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_action || button > NK_POINTER_BUTTON_LAST)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid pointer button state query");
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    *out_action = resource->pointer_buttons[button];
    return NK_OK;
}

nk_result NK_CALL nk_pointer_get_position(nk_handle handle, double *out_x, double *out_y) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_x || !out_y)
        return fail(NK_ERROR_INVALID_ARGUMENT, "pointer position outputs must not be null");
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    *out_x = resource->pointer_x;
    *out_y = resource->pointer_y;
    return NK_OK;
}

nk_result NK_CALL nk_cursor_create_standard(nk_cursor_shape shape, nk_handle *out_cursor) {
    return nk::core::result_boundary(
        "unexpected error while creating standard cursor", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!out_cursor)
                return fail(NK_ERROR_INVALID_ARGUMENT, "cursor output must not be null");
            *out_cursor = NK_INVALID_HANDLE;
            NSCursor *native = nil;
            switch (shape) {
            case NK_CURSOR_ARROW:
                native = [NSCursor arrowCursor];
                break;
            case NK_CURSOR_IBEAM:
                native = [NSCursor IBeamCursor];
                break;
            case NK_CURSOR_CROSSHAIR:
                native = [NSCursor crosshairCursor];
                break;
            case NK_CURSOR_HAND:
                native = [NSCursor pointingHandCursor];
                break;
            case NK_CURSOR_HORIZONTAL_RESIZE:
                native = [NSCursor resizeLeftRightCursor];
                break;
            case NK_CURSOR_VERTICAL_RESIZE:
                native = [NSCursor resizeUpDownCursor];
                break;
            case NK_CURSOR_NWSE_RESIZE:
                native = diagonal_resize_cursor(true);
                break;
            case NK_CURSOR_NESW_RESIZE:
                native = diagonal_resize_cursor(false);
                break;
            case NK_CURSOR_MOVE:
                native = [NSCursor closedHandCursor];
                break;
            case NK_CURSOR_NOT_ALLOWED:
                native = [NSCursor operationNotAllowedCursor];
                break;
            default:
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid standard cursor shape");
            }
            auto resource = std::make_shared<MacCursorResource>();
            resource->cursor = native;
            resource->handle = nk::core::handles().insert(nk::core::ResourceType::cursor, resource);
            if (resource->handle == NK_INVALID_HANDLE)
                return fail(NK_ERROR_OUT_OF_MEMORY, "cursor handle registry is full");
            *out_cursor = resource->handle;
            return NK_OK;
        });
}

nk_result NK_CALL nk_cursor_create_custom(const nk_cursor_image *image, nk_handle *out_cursor) {
    return nk::core::result_boundary(
        "unexpected error while creating custom cursor", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!image || image->struct_size < sizeof(*image) || !out_cursor || !image->rgba ||
                image->width <= 0 || image->height <= 0 || image->width > INT_MAX / 4 ||
                image->stride < image->width * 4 || image->hotspot_x < 0 || image->hotspot_y < 0 ||
                image->hotspot_x >= image->width || image->hotspot_y >= image->height)
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid custom cursor image");
            *out_cursor = NK_INVALID_HANDLE;
            NSBitmapImageRep *bitmap =
                [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nil
                                                        pixelsWide:image->width
                                                        pixelsHigh:image->height
                                                     bitsPerSample:8
                                                   samplesPerPixel:4
                                                          hasAlpha:YES
                                                          isPlanar:NO
                                                    colorSpaceName:NSDeviceRGBColorSpace
                                                       bytesPerRow:0
                                                      bitsPerPixel:0];
            if (!bitmap)
                return fail(NK_ERROR_OUT_OF_MEMORY, "could not allocate custom cursor pixels");
            const auto *source = static_cast<const uint8_t *>(image->rgba);
            auto *destination = bitmap.bitmapData;
            const int stride = static_cast<int>(bitmap.bytesPerRow);
            for (int32_t y = 0; y < image->height; ++y) {
                const auto *source_row = source + static_cast<std::size_t>(y) * image->stride;
                auto *destination_row = destination + static_cast<std::size_t>(y) * stride;
                for (int32_t x = 0; x < image->width; ++x) {
                    const uint32_t alpha = source_row[x * 4 + 3];
                    destination_row[x * 4 + 0] =
                        static_cast<uint8_t>(source_row[x * 4 + 0] * alpha / 255u);
                    destination_row[x * 4 + 1] =
                        static_cast<uint8_t>(source_row[x * 4 + 1] * alpha / 255u);
                    destination_row[x * 4 + 2] =
                        static_cast<uint8_t>(source_row[x * 4 + 2] * alpha / 255u);
                    destination_row[x * 4 + 3] = static_cast<uint8_t>(alpha);
                }
            }
            NSImage *native_image =
                [[NSImage alloc] initWithSize:NSMakeSize(image->width, image->height)];
            [native_image addRepresentation:bitmap];
            auto resource = std::make_shared<MacCursorResource>();
            resource->cursor = [[NSCursor alloc]
                initWithImage:native_image
                      hotSpot:NSMakePoint(image->hotspot_x, image->height - image->hotspot_y)];
            if (!resource->cursor)
                return fail(NK_ERROR_UNSUPPORTED, "macOS could not create the custom cursor");
            resource->handle = nk::core::handles().insert(nk::core::ResourceType::cursor, resource);
            if (resource->handle == NK_INVALID_HANDLE)
                return fail(NK_ERROR_OUT_OF_MEMORY, "cursor handle registry is full");
            *out_cursor = resource->handle;
            return NK_OK;
        });
}

nk_result NK_CALL nk_cursor_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!cursor(handle))
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale cursor handle");
    nk::core::handles().erase(handle, nk::core::ResourceType::cursor);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_cursor(nk_handle window_handle, nk_handle cursor_handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = window(window_handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    auto selected = cursor_handle == NK_INVALID_HANDLE ? nullptr : cursor(cursor_handle);
    if (cursor_handle != NK_INVALID_HANDLE && !selected)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale cursor handle");
    resource->cursor = std::move(selected);
    apply_cursor(*resource);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_cursor_mode(nk_handle handle, nk_cursor_mode mode) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (mode > NK_CURSOR_MODE_DISABLED)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid cursor mode");
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    if (!apply_cursor_mode(*resource, mode))
        return fail(NK_ERROR_UNSUPPORTED, "macOS could not capture the pointer");
    return NK_OK;
}

nk_result NK_CALL nk_window_get_cursor_mode(nk_handle handle, nk_cursor_mode *out_mode) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_mode)
        return fail(NK_ERROR_INVALID_ARGUMENT, "cursor mode output must not be null");
    auto resource = window(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    *out_mode = resource->cursor_mode;
    return NK_OK;
}

uint32_t NK_CALL nk_raw_pointer_motion_supported(void) {
    return 0;
}

nk_result NK_CALL nk_surface_set_text_input_state(nk_handle handle,
                                                  const nk_text_input_state *state) {
    return nk::core::result_boundary(
        "unexpected error while setting text input state", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!state || state->struct_size < sizeof(*state) || !state->text)
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid text input state");
            NSString *native_text = string(state->text);
            std::vector<uint32_t> points;
            if (!native_text || !decode_utf8(state->text, points))
                return fail(NK_ERROR_INVALID_ARGUMENT, "text input state text is not valid UTF-8");
            const uint64_t text_end = static_cast<uint64_t>(state->text_start) + points.size();
            const bool no_composition = state->composition_start == NK_TEXT_POSITION_NONE &&
                                        state->composition_end == NK_TEXT_POSITION_NONE;
            const bool valid_composition = state->composition_start != NK_TEXT_POSITION_NONE &&
                                           state->composition_end != NK_TEXT_POSITION_NONE &&
                                           state->composition_start <= state->composition_end &&
                                           state->composition_start >= state->text_start &&
                                           state->composition_end <= text_end;
            const bool valid_cursor =
                std::isfinite(state->cursor_x) && std::isfinite(state->cursor_y) &&
                std::isfinite(state->cursor_width) && std::isfinite(state->cursor_height) &&
                state->cursor_width >= 0.f && state->cursor_height >= 0.f;
            if ((state->flags & ~(NK_TEXT_INPUT_MULTILINE | NK_TEXT_INPUT_AUTOCORRECT |
                                  NK_TEXT_INPUT_CAPITALIZE_SENTENCES)) ||
                state->text_start > state->document_length || text_end > state->document_length ||
                state->selection_start > state->selection_end ||
                state->selection_start < state->text_start || state->selection_end > text_end ||
                (!no_composition && !valid_composition) ||
                state->input_type > NK_TEXT_INPUT_PASSWORD ||
                state->action > NK_TEXT_INPUT_ACTION_NONE || !valid_cursor)
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "text input state ranges or hints are invalid");
            auto resource = window(handle);
            if (!resource)
                return fail(NK_ERROR_INVALID_HANDLE,
                            "text input state requires a desktop window on this backend");
            if (!resource->owns_window)
                return unsupported("wrapped Cocoa windows do not own NativeKit text input views");
            resource->text_input_text = state->text;
            resource->text_input_state = *state;
            resource->text_input_state.text = resource->text_input_text.c_str();
            resource->text_composition_start = state->composition_start;
            resource->text_composition_end = state->composition_end;
            resource->text_composing = state->composition_start != NK_TEXT_POSITION_NONE;
            resource->marked_native_range =
                resource->text_composing
                    ? native_range_for_positions(*resource, state->composition_start,
                                                 state->composition_end)
                    : NSMakeRange(NSNotFound, 0);
            resource->selected_native_range =
                native_range_for_positions(*resource, state->selection_start, state->selection_end);
            if (resource->text_composing) {
                const auto first = state->composition_start - state->text_start;
                const auto last = state->composition_end - state->text_start;
                const NSUInteger native_start = utf16_offset_for_codepoint(points, first);
                const NSUInteger native_end = utf16_offset_for_codepoint(points, last);
                NSString *state_string = string(resource->text_input_text.c_str());
                resource->marked_text = utf8([state_string
                    substringWithRange:NSMakeRange(native_start, native_end - native_start)]);
            } else {
                resource->marked_text.clear();
            }
            [[resource->content inputContext] invalidateCharacterCoordinates];
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_set_text_input_active(nk_handle handle, uint32_t active) {
    return nk::core::result_boundary(
        "unexpected error while changing text input", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (active > 1)
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "text input active state must be zero or one");
            auto resource = window(handle);
            if (!resource)
                return fail(NK_ERROR_INVALID_HANDLE,
                            "text input activation requires a desktop window on this backend");
            if (!resource->owns_window)
                return unsupported("wrapped Cocoa windows do not own NativeKit text input views");
            if (active) {
                resource->text_input_active = true;
                [resource->window makeFirstResponder:resource->content];
            } else {
                if (resource->text_input_active)
                    finish_text_composition(*resource);
                resource->text_input_active = false;
                if (resource->text_composing)
                    [resource->content unmarkText];
            }
            return NK_OK;
        });
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
    request_fullscreen_transition(*w, enabled != 0);
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

nk_result NK_CALL nk_monitor_list(nk_handle *monitors, uint32_t *inout_count) {
    return nk::core::result_boundary(
        "unexpected error while enumerating monitors", [&]() -> nk_result {
            if (const auto r = enter_ui(); r != NK_OK)
                return r;
            if (!inout_count)
                return fail(NK_ERROR_INVALID_ARGUMENT, "monitor count must not be null");
            if (const auto r = refresh_monitors(); r != NK_OK)
                return r;
            const uint32_t required = static_cast<uint32_t>(monitor_handles.size());
            const uint32_t capacity = *inout_count;
            *inout_count = required;
            if (!monitors || capacity < required)
                return required
                           ? fail(NK_ERROR_BUFFER_TOO_SMALL, "monitor handle buffer is too small")
                           : NK_OK;
            uint32_t index = 0;
            for (NSScreen *screen in NSScreen.screens) {
                const auto display = display_id(screen);
                if (display != kCGNullDirectDisplay)
                    monitors[index++] = monitor_handles.at(display);
            }
            return NK_OK;
        });
}

nk_result NK_CALL nk_monitor_get_primary(nk_handle *out_monitor) {
    return nk::core::result_boundary(
        "unexpected error while finding primary monitor", [&]() -> nk_result {
            if (const auto r = enter_ui(); r != NK_OK)
                return r;
            if (!out_monitor)
                return fail(NK_ERROR_INVALID_ARGUMENT, "monitor output must not be null");
            *out_monitor = NK_INVALID_HANDLE;
            if (const auto r = refresh_monitors(); r != NK_OK)
                return r;
            const auto found = monitor_handles.find(CGMainDisplayID());
            if (found == monitor_handles.end())
                return fail(NK_ERROR_UNSUPPORTED, "macOS reports no connected primary monitor");
            *out_monitor = found->second;
            return NK_OK;
        });
}

nk_result NK_CALL nk_monitor_get_name(nk_handle handle, char *buffer, uint32_t *inout_size) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto resource = monitor(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale monitor handle");
    NSString *name = [NSString stringWithUTF8String:resource->name.c_str()];
    return copy_output(name, buffer, inout_size);
}

nk_result NK_CALL nk_monitor_get_geometry(nk_handle handle, nk_monitor_geometry *out_geometry) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!out_geometry || out_geometry->struct_size < sizeof(*out_geometry))
        return fail(NK_ERROR_INVALID_ARGUMENT, "monitor geometry output is missing or too small");
    auto resource = monitor(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale monitor handle");
    NSScreen *screen = screen_for_display(resource->display);
    if (!screen)
        return fail(NK_ERROR_INVALID_HANDLE, "monitor is no longer connected");
    const NSRect geometry = screen.frame;
    const NSRect workarea = screen.visibleFrame;
    const CGFloat scale = screen.backingScaleFactor > 0.0 ? screen.backingScaleFactor : 1.0;
    const CGSize physical_size = CGDisplayScreenSize(resource->display);
    const auto size = out_geometry->struct_size;
    *out_geometry = {};
    out_geometry->struct_size = size;
    out_geometry->x = static_cast<int32_t>(std::lround(geometry.origin.x));
    out_geometry->y = static_cast<int32_t>(std::lround(geometry.origin.y));
    out_geometry->width = static_cast<int32_t>(std::lround(geometry.size.width));
    out_geometry->height = static_cast<int32_t>(std::lround(geometry.size.height));
    out_geometry->work_x = static_cast<int32_t>(std::lround(workarea.origin.x));
    out_geometry->work_y = static_cast<int32_t>(std::lround(workarea.origin.y));
    out_geometry->work_width = static_cast<int32_t>(std::lround(workarea.size.width));
    out_geometry->work_height = static_cast<int32_t>(std::lround(workarea.size.height));
    out_geometry->width_mm = static_cast<int32_t>(std::lround(physical_size.width));
    out_geometry->height_mm = static_cast<int32_t>(std::lround(physical_size.height));
    out_geometry->scale_x = static_cast<float>(scale);
    out_geometry->scale_y = static_cast<float>(scale);
    return NK_OK;
}

nk_result NK_CALL nk_monitor_get_current_mode(nk_handle handle, nk_video_mode *out_mode) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!out_mode || out_mode->struct_size < sizeof(*out_mode))
        return fail(NK_ERROR_INVALID_ARGUMENT, "video mode output is missing or too small");
    auto resource = monitor(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale monitor handle");
    CGDisplayModeRef native = CGDisplayCopyDisplayMode(resource->display);
    if (!native)
        return fail(NK_ERROR_UNSUPPORTED, "current monitor mode is unavailable");
    const auto size = out_mode->struct_size;
    *out_mode = make_video_mode(native);
    out_mode->struct_size = size;
    CGDisplayModeRelease(native);
    return NK_OK;
}

nk_result NK_CALL nk_monitor_get_modes(nk_handle handle, nk_video_mode *modes,
                                       uint32_t *inout_count) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!inout_count)
        return fail(NK_ERROR_INVALID_ARGUMENT, "video mode count must not be null");
    auto resource = monitor(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale monitor handle");
    CFArrayRef native_modes = CGDisplayCopyAllDisplayModes(resource->display, nullptr);
    if (!native_modes)
        return fail(NK_ERROR_UNSUPPORTED, "monitor modes are unavailable");
    std::vector<nk_video_mode> available;
    const CFIndex count = CFArrayGetCount(native_modes);
    available.reserve(static_cast<std::size_t>(count));
    for (CFIndex index = 0; index < count; ++index) {
        auto mode = static_cast<CGDisplayModeRef>(
            const_cast<void *>(CFArrayGetValueAtIndex(native_modes, index)));
        if (mode)
            available.push_back(make_video_mode(mode));
    }
    CFRelease(native_modes);
    const uint32_t required = static_cast<uint32_t>(available.size());
    const uint32_t capacity = *inout_count;
    *inout_count = required;
    if (!modes || capacity < required)
        return required ? fail(NK_ERROR_BUFFER_TOO_SMALL, "video mode buffer is too small") : NK_OK;
    std::copy(available.begin(), available.end(), modes);
    return NK_OK;
}

nk_result NK_CALL nk_monitor_get_orientation(nk_handle handle, nk_orientation *out_orientation) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!out_orientation)
        return fail(NK_ERROR_INVALID_ARGUMENT, "monitor orientation output must not be null");
    auto resource = monitor(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale monitor handle");
    const auto rotation = static_cast<int>(std::lround(CGDisplayRotation(resource->display)));
    if (rotation == 90)
        *out_orientation = NK_ORIENTATION_PORTRAIT;
    else if (rotation == 180)
        *out_orientation = NK_ORIENTATION_LANDSCAPE_LEFT;
    else if (rotation == 270)
        *out_orientation = NK_ORIENTATION_PORTRAIT_UPSIDE_DOWN;
    else {
        const auto width = CGDisplayPixelsWide(resource->display);
        const auto height = CGDisplayPixelsHigh(resource->display);
        *out_orientation = width == height  ? NK_ORIENTATION_UNKNOWN
                           : width > height ? NK_ORIENTATION_LANDSCAPE_RIGHT
                                            : NK_ORIENTATION_PORTRAIT;
    }
    return NK_OK;
}

nk_result NK_CALL nk_window_set_fullscreen_monitor(nk_handle window_handle,
                                                   nk_handle monitor_handle) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto window_resource = window(window_handle);
    if (!window_resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    if (monitor_handle == NK_INVALID_HANDLE) {
        window_resource->fullscreen_reposition_pending = false;
        window_resource->fullscreen_reenter_after_reposition = false;
        return nk_window_set_fullscreen(window_handle, 0);
    }
    auto monitor_resource = monitor(monitor_handle);
    if (!monitor_resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale monitor handle");
    NSScreen *screen = screen_for_display(monitor_resource->display);
    if (!screen)
        return fail(NK_ERROR_INVALID_HANDLE, "monitor is no longer connected");
    window_resource->fullscreen_target_origin = screen.frame.origin;
    window_resource->fullscreen_reposition_pending = true;
    const bool current = (window_resource->window.styleMask & NSWindowStyleMaskFullScreen) != 0;
    if (current) {
        window_resource->fullscreen_reenter_after_reposition = true;
        request_fullscreen_transition(*window_resource, false);
    } else {
        window_resource->fullscreen_reenter_after_reposition = false;
        [window_resource->window setFrameOrigin:screen.frame.origin];
        request_fullscreen_transition(*window_resource, true);
    }
    return NK_OK;
}

nk_result NK_CALL nk_window_set_aspect_ratio(nk_handle h, int32_t numerator, int32_t denominator) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if ((numerator == 0) != (denominator == 0) || numerator < 0 || denominator < 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid window aspect ratio");
    auto w = window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    [w->window setContentAspectRatio:(numerator ? NSMakeSize(numerator, denominator) : NSZeroSize)];
    return NK_OK;
}

nk_result NK_CALL nk_window_set_resizable(nk_handle h, uint32_t enabled) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    NSWindowStyleMask style = w->window.styleMask;
    if (enabled)
        style |= NSWindowStyleMaskResizable;
    else
        style &= ~NSWindowStyleMaskResizable;
    w->window.styleMask = style;
    return NK_OK;
}

nk_result NK_CALL nk_window_set_decorated(nk_handle h, uint32_t enabled) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    constexpr NSWindowStyleMask decorations =
        NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable;
    NSWindowStyleMask style = w->window.styleMask;
    if (enabled)
        style |= decorations;
    else
        style &= ~decorations;
    w->window.styleMask = style;
    return NK_OK;
}

nk_result NK_CALL nk_window_set_floating(nk_handle h, uint32_t enabled) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    w->window.level = enabled ? NSFloatingWindowLevel : NSNormalWindowLevel;
    return NK_OK;
}

nk_result NK_CALL nk_window_set_opacity(nk_handle h, float opacity) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!(opacity >= 0.0f && opacity <= 1.0f))
        return fail(NK_ERROR_INVALID_ARGUMENT, "window opacity must be between zero and one");
    auto w = window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    w->window.alphaValue = opacity;
    return NK_OK;
}

nk_result NK_CALL nk_window_set_mouse_passthrough(nk_handle h, uint32_t enabled) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    auto w = window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    w->window.ignoresMouseEvents = enabled != 0;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_hovered(nk_handle h, uint32_t *out_hovered) {
    if (const auto r = enter_ui(); r != NK_OK)
        return r;
    if (!out_hovered)
        return fail(NK_ERROR_INVALID_ARGUMENT, "hovered output must not be null");
    auto w = window(h);
    if (!w)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale window handle");
    *out_hovered = w->hovered ? 1u : 0u;
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
    out_native->view = reinterpret_cast<uintptr_t>((__bridge void *)window_content_view(*resource));
    return NK_OK;
}

nk_result NK_CALL nk_window_wrap_native(const nk_native_window *native, nk_handle *out_window) {
    try {
        if (const auto result = enter_ui(); result != NK_OK)
            return result;
        if (!native || native->struct_size < sizeof(*native) || !out_window ||
            native->kind != NK_NATIVE_WINDOW_COCOA || !native->window)
            return fail(NK_ERROR_INVALID_ARGUMENT, "invalid Cocoa native window descriptor");
        *out_window = NK_INVALID_HANDLE;
        id object = (__bridge id)(reinterpret_cast<void *>(native->window));
        if (![object isKindOfClass:[NSWindow class]])
            return fail(NK_ERROR_INVALID_ARGUMENT, "Cocoa native window is not an NSWindow");
        NSView *view = nil;
        if (native->view) {
            id view_object = (__bridge id)(reinterpret_cast<void *>(native->view));
            if (![view_object isKindOfClass:[NSView class]])
                return fail(NK_ERROR_INVALID_ARGUMENT, "Cocoa native view is not an NSView");
            view = (NSView *)view_object;
        }
        auto resource = std::make_shared<MacWindowResource>();
        resource->window = (NSWindow *)object;
        resource->native_content = view ?: resource->window.contentView;
        resource->owns_window = false;
        resource->handle = nk::core::handles().insert(nk::core::ResourceType::window, resource);
        if (resource->handle == NK_INVALID_HANDLE)
            return fail(NK_ERROR_OUT_OF_MEMORY, "window handle registry is full");
        *out_window = resource->handle;
        return NK_OK;
    } catch (const std::bad_alloc &) {
        return fail(NK_ERROR_OUT_OF_MEMORY, "out of memory while wrapping Cocoa window");
    } catch (...) {
        return fail(NK_ERROR_UNKNOWN, "unexpected error while wrapping Cocoa window");
    }
}

nk_result NK_CALL nk_surface_create(nk_handle parent_handle, const nk_surface_options *options,
                                    nk_handle *out_surface) {
    return nk::core::result_boundary(
        "unexpected error while creating Metal surface", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            constexpr nk_surface_flags supported_flags = NK_SURFACE_HIDDEN | NK_SURFACE_ALPHA |
                                                         NK_SURFACE_DEPTH | NK_SURFACE_STENCIL |
                                                         NK_SURFACE_DEBUG_CONTEXT;
            if (!options || options->struct_size < sizeof(*options) || !out_surface ||
                options->width <= 0 || options->height <= 0 || options->api != NK_GRAPHICS_METAL ||
                (options->flags & ~supported_flags) != 0)
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid Metal surface options");
            *out_surface = NK_INVALID_HANDLE;
            auto parent = window(parent_handle);
            if (!parent)
                return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale parent window handle");
            auto shared = options->share_surface ? surface(options->share_surface) : nullptr;
            if (options->share_surface && !shared)
                return fail(NK_ERROR_INVALID_HANDLE, "invalid shared graphics surface");
            if (shared && shared->api != NK_GRAPHICS_METAL)
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "shared surfaces must use the same graphics API");
            if (shared && shared->share_dependents == UINT32_MAX)
                return fail(NK_ERROR_INVALID_REQUEST, "graphics surface has too many dependents");
            NSView *parent_content = window_content_view(*parent);
            if (!parent_content)
                return fail(NK_ERROR_UNSUPPORTED, "wrapped Cocoa window has no content view");

            parent->surfaces.reserve(parent->surfaces.size() + 1);
            auto resource = std::make_shared<MacSurfaceResource>();
            resource->parent = parent_handle;
            resource->flags = options->flags;
            resource->x = options->x;
            resource->y = options->y;
            resource->width = options->width;
            resource->height = options->height;
            resource->shared_surface = shared;
            if (shared) {
                resource->device = shared->device;
                resource->queue = shared->queue;
                resource->device_handle = shared->device_handle;
            } else {
                resource->device = MTLCreateSystemDefaultDevice();
                if (resource->device)
                    resource->queue = [resource->device newCommandQueue];
            }
            if (!resource->device || !resource->queue)
                return fail(NK_ERROR_UNSUPPORTED,
                            "could not create a Metal device and command queue");

            resource->view = [[NKMetalSurfaceView alloc]
                initWithFrame:NSMakeRect(resource->x, resource->y, resource->width,
                                         resource->height)];
            resource->view.wantsLayer = YES;
            resource->layer = [CAMetalLayer layer];
            resource->layer.device = resource->device;
            resource->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            resource->layer.framebufferOnly = YES;
            resource->layer.opaque = (options->flags & NK_SURFACE_ALPHA) == 0;
            resource->view.layer = resource->layer;
            resource->view.hidden = (options->flags & NK_SURFACE_HIDDEN) != 0;
            resource->accessibility_container = [[NKMacAccessibilityContainer alloc]
                initWithFrame:NSMakeRect(resource->x, resource->y, resource->width,
                                         resource->height)];
            if (!resource->accessibility_container)
                return fail(NK_ERROR_OUT_OF_MEMORY,
                            "could not create the macOS accessibility container");
            resource->accessibility_container.hidden = resource->view.hidden;
            [parent_content addSubview:resource->view positioned:NSWindowAbove relativeTo:nil];
            [parent_content addSubview:resource->accessibility_container
                            positioned:NSWindowAbove
                            relativeTo:resource->view];
            set_surface_native_bounds(*resource);
            if (resource->framebuffer_width > 0 && resource->framebuffer_height > 0 &&
                !ensure_surface_depth_target(*resource, resource->framebuffer_width,
                                             resource->framebuffer_height))
                return fail(NK_ERROR_UNSUPPORTED,
                            "could not create the Metal depth/stencil target");
            resource->handle =
                nk::core::handles().insert(nk::core::ResourceType::surface, resource);
            if (resource->handle == NK_INVALID_HANDLE)
                return fail(NK_ERROR_OUT_OF_MEMORY, "graphics surface handle registry is full");
            if (!resource->device_handle)
                resource->device_handle = resource->handle;
            parent->surfaces.push_back(resource->handle);
            if (shared)
                ++shared->share_dependents;
            resource->accessibility_container.surface = resource->handle;
            refresh_mac_accessibility_elements(*resource);
            resource->ready = true;
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_SURFACE_READY;
            event.source = resource->handle;
            nk::core::push_event(std::move(event));
            *out_surface = resource->handle;
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_destroy(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    if (resource->share_dependents)
        return fail(NK_ERROR_INVALID_REQUEST,
                    "graphics surface is still shared by another surface");
    if (nk_core_graphics_device_has_references(nk_graphics_device{resource->device_handle}))
        return fail(NK_ERROR_INVALID_REQUEST, "graphics surface still owns retained GPU resources");
    resource->destroying = true;
    resource->frame_prepared = false;
    [resource->frame_timer invalidate];
    resource->frame_timer = nil;
    resource->drawable = nil;
    resource->depth_stencil = nil;
    resource->accessibility_container.surface = NK_INVALID_HANDLE;
    [resource->accessibility_container removeFromSuperview];
    resource->accessibility_container = nil;
    if (resource->view)
        [resource->view removeFromSuperview];
    resource->view = nil;
    resource->layer = nil;
    if (auto parent = window(resource->parent)) {
        auto &surfaces = parent->surfaces;
        surfaces.erase(std::remove(surfaces.begin(), surfaces.end(), handle), surfaces.end());
    }
    if (resource->shared_surface)
        --resource->shared_surface->share_dependents;
    nk::core::handles().erase(handle, nk::core::ResourceType::surface);
    return NK_OK;
}

nk_result NK_CALL nk_surface_show(nk_handle handle, uint32_t visible) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (visible > 1)
        return fail(NK_ERROR_INVALID_ARGUMENT, "surface visibility must be zero or one");
    auto resource = surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    resource->view.hidden = visible == 0;
    resource->accessibility_container.hidden = visible == 0;
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                        int32_t height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (width <= 0 || height <= 0)
        return fail(NK_ERROR_INVALID_ARGUMENT, "graphics surface dimensions must be positive");
    auto resource = surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    resource->x = x;
    resource->y = y;
    resource->width = width;
    resource->height = height;
    if (!set_surface_native_bounds(*resource))
        return fail(NK_ERROR_UNKNOWN, "could not resize the Metal child surface");
    refresh_mac_accessibility_elements(*resource);
    return NK_OK;
}

nk_result NK_CALL nk_surface_accessibility_set_node(nk_handle handle,
                                                    const nk_accessibility_node *node) {
    return nk::core::result_boundary(
        "unexpected error while setting a macOS accessibility node", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            auto resource = surface(handle);
            if (!resource)
                return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale Metal surface handle");
            if (!node)
                return fail(NK_ERROR_INVALID_ARGUMENT, "macOS accessibility node is missing");
            MacAccessibilityNode copy;
            if (!copy_mac_accessibility_node(*node, resource->accessibility_nodes, copy))
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid macOS accessibility node");
            if (const auto old = resource->accessibility_nodes.find(node->id);
                old != resource->accessibility_nodes.end())
                copy.text_ranges = old->second.text_ranges;
            resource->accessibility_nodes[node->id] = std::move(copy);
            refresh_mac_accessibility_elements(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_remove_node(nk_handle handle,
                                                       nk_accessibility_node_id node) {
    return nk::core::result_boundary(
        "unexpected error while removing a macOS accessibility node", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            auto resource = surface(handle);
            if (!resource)
                return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale Metal surface handle");
            if (!node ||
                resource->accessibility_nodes.find(node) == resource->accessibility_nodes.end())
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "invalid or unknown macOS accessibility node");
            remove_mac_accessibility_descendants(resource->accessibility_nodes, node);
            if (resource->accessibility_nodes.find(resource->accessibility_focus) ==
                resource->accessibility_nodes.end())
                resource->accessibility_focus = NK_ACCESSIBILITY_ROOT;
            refresh_mac_accessibility_elements(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_clear(nk_handle handle) {
    return nk::core::result_boundary(
        "unexpected error while clearing macOS accessibility nodes", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            auto resource = surface(handle);
            if (!resource)
                return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale Metal surface handle");
            resource->accessibility_nodes.clear();
            resource->accessibility_focus = NK_ACCESSIBILITY_ROOT;
            refresh_mac_accessibility_elements(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_set_focus(nk_handle handle,
                                                     nk_accessibility_node_id node) {
    return nk::core::result_boundary(
        "unexpected error while focusing a macOS accessibility node", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            auto resource = surface(handle);
            if (!resource)
                return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale Metal surface handle");
            if (node != NK_ACCESSIBILITY_ROOT &&
                resource->accessibility_nodes.find(node) == resource->accessibility_nodes.end())
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "cannot focus an unknown macOS accessibility node");
            resource->accessibility_focus = node;
            refresh_mac_accessibility_elements(*resource);
            if (node == NK_ACCESSIBILITY_ROOT) {
                NSAccessibilityPostNotification(resource->accessibility_container,
                                                NSAccessibilityFocusedUIElementChangedNotification);
            } else {
                for (NKMacAccessibilityElement *element in resource->accessibility_container
                         .allElements)
                    if (element.node == node) {
                        NSAccessibilityPostNotification(
                            element, NSAccessibilityFocusedUIElementChangedNotification);
                        break;
                    }
            }
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_update(nk_handle handle,
                                                  const nk_accessibility_update *update) {
    return nk::core::result_boundary(
        "unexpected error while updating macOS accessibility nodes", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            auto resource = surface(handle);
            if (!resource)
                return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale Metal surface handle");
            if (!update || update->struct_size < sizeof(*update) ||
                (update->flags & ~NK_ACCESSIBILITY_UPDATE_FOCUS) ||
                (update->node_count && !update->nodes) ||
                (update->removed_node_count && !update->removed_nodes))
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid macOS accessibility update");

            auto nodes = resource->accessibility_nodes;
            for (uint32_t index = 0; index < update->removed_node_count; ++index) {
                const auto removed = update->removed_nodes[index];
                if (!removed || nodes.find(removed) == nodes.end())
                    return fail(NK_ERROR_INVALID_ARGUMENT,
                                "macOS accessibility update removes an unknown node");
                remove_mac_accessibility_descendants(nodes, removed);
            }
            for (uint32_t index = 0; index < update->node_count; ++index) {
                const auto &node = update->nodes[index];
                MacAccessibilityNode copy;
                if (!copy_mac_accessibility_node(node, nodes, copy))
                    return fail(NK_ERROR_INVALID_ARGUMENT,
                                "invalid node in macOS accessibility update");
                if (const auto old = nodes.find(node.id); old != nodes.end())
                    copy.text_ranges = old->second.text_ranges;
                nodes[node.id] = std::move(copy);
            }
            if ((update->flags & NK_ACCESSIBILITY_UPDATE_FOCUS) &&
                update->focus != NK_ACCESSIBILITY_ROOT && nodes.find(update->focus) == nodes.end())
                return fail(NK_ERROR_INVALID_ARGUMENT,
                            "macOS accessibility update focuses an unknown node");
            resource->accessibility_nodes = std::move(nodes);
            if (update->flags & NK_ACCESSIBILITY_UPDATE_FOCUS)
                resource->accessibility_focus = update->focus;
            else if (resource->accessibility_focus != NK_ACCESSIBILITY_ROOT &&
                     resource->accessibility_nodes.find(resource->accessibility_focus) ==
                         resource->accessibility_nodes.end())
                resource->accessibility_focus = NK_ACCESSIBILITY_ROOT;
            refresh_mac_accessibility_elements(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_set_text_ranges(
    nk_handle handle, nk_accessibility_node_id node, const nk_accessibility_text_range *ranges,
    uint32_t range_count) {
    return nk::core::result_boundary(
        "unexpected error while setting macOS accessibility text ranges", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            auto resource = surface(handle);
            if (!resource)
                return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale Metal surface handle");
            const auto found = resource->accessibility_nodes.find(node);
            if (!node || found == resource->accessibility_nodes.end() || (range_count && !ranges))
                return fail(NK_ERROR_INVALID_ARGUMENT, "invalid macOS accessibility text ranges");
            std::vector<MacAccessibilityTextRange> copy;
            copy.reserve(range_count);
            nk_accessibility_text_position previous = 0;
            for (uint32_t index = 0; index < range_count; ++index) {
                const auto &range = ranges[index];
                if (range.start >= range.end || range.start < previous ||
                    range.end > found->second.document_length || !std::isfinite(range.x) ||
                    !std::isfinite(range.y) || !std::isfinite(range.width) ||
                    !std::isfinite(range.height) || range.width < 0 || range.height < 0)
                    return fail(NK_ERROR_INVALID_ARGUMENT,
                                "invalid or unordered macOS accessibility text ranges");
                copy.push_back(
                    {range.start, range.end, range.x, range.y, range.width, range.height});
                previous = range.end;
            }
            found->second.text_ranges = std::move(copy);
            refresh_mac_accessibility_elements(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_make_current(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    if (resource->frame_prepared && resource->drawable)
        return NK_OK;
    if (!surface_frame_available(*resource))
        return fail(NK_ERROR_INVALID_REQUEST, "Metal surface has no drawable frame");
    sync_surface_drawable_size(*resource);
    resource->drawable = [resource->layer nextDrawable];
    if (!resource->drawable)
        return fail(NK_ERROR_INVALID_REQUEST, "Metal drawable is temporarily unavailable");
    const int32_t width = static_cast<int32_t>(resource->drawable.texture.width);
    const int32_t height = static_cast<int32_t>(resource->drawable.texture.height);
    if (!ensure_surface_depth_target(*resource, width, height))
        return NK_ERROR_UNKNOWN;
    resource->framebuffer_width = width;
    resource->framebuffer_height = height;
    resource->frame_prepared = true;
    return NK_OK;
}

nk_result NK_CALL nk_surface_present(nk_handle handle) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    if (!resource->frame_prepared)
        return fail(NK_ERROR_INVALID_REQUEST, "Metal surface has no prepared frame");
    // Sokol schedules the drawable for presentation when its command buffer commits.
    resource->drawable = nil;
    resource->frame_prepared = false;
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_frame_callback(nk_handle handle,
                                                nk_surface_frame_callback callback,
                                                void *user_data) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    auto resource = surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    [resource->frame_timer invalidate];
    resource->frame_timer = nil;
    resource->frame_callback = callback;
    resource->frame_user_data = callback ? user_data : nullptr;
    if (!callback)
        return NK_OK;
    resource->frame_timer =
        [NSTimer scheduledTimerWithTimeInterval:1.0 / 60.0
                                        repeats:YES
                                          block:^(NSTimer *timer) {
                                            (void)timer;
                                            auto active = surface(handle);
                                            if (!active || active->frame_callback != callback) {
                                                [timer invalidate];
                                                return;
                                            }
                                            nk::core::callback_boundary([&] {
                                                if (nk_surface_make_current(handle) != NK_OK)
                                                    return;
                                                callback(handle, active->framebuffer_width,
                                                         active->framebuffer_height, user_data);
                                                if (active->frame_prepared)
                                                    nk_surface_present(handle);
                                            });
                                          }];
    return resource->frame_timer ? NK_OK
                                 : fail(NK_ERROR_UNKNOWN, "could not start the Metal frame timer");
}

nk_result NK_CALL nk_surface_get_framebuffer_size(nk_handle handle, int32_t *out_width,
                                                  int32_t *out_height) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!out_width || !out_height)
        return fail(NK_ERROR_INVALID_ARGUMENT, "framebuffer size outputs must not be null");
    auto resource = surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    sync_surface_drawable_size(*resource);
    if (surface_frame_available(*resource)) {
        *out_width = resource->framebuffer_width;
        *out_height = resource->framebuffer_height;
    } else {
        *out_width = 0;
        *out_height = 0;
    }
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_frame_target(nk_handle handle,
                                              nk_surface_frame_target *out_target) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!nk::core::surface_frame_target_output_valid(out_target))
        return fail(NK_ERROR_INVALID_ARGUMENT, "frame-target output is missing or too small");
    auto resource = surface(handle);
    if (!resource)
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    nk_surface_frame_target target{};
    target.struct_size = out_target->struct_size;
    target.api = NK_GRAPHICS_METAL;
    const bool prepared = resource->frame_prepared;
    target.width = prepared ? resource->framebuffer_width : 0;
    target.height = prepared ? resource->framebuffer_height : 0;
    target.native_target =
        prepared && resource->drawable ? metal_object_token(resource->drawable.texture) : 0;
    target.device.id = resource->device_handle;
    target.native_device = metal_object_token(resource->device);
    target.native_context = metal_object_token(resource->queue);
    target.native_depth_stencil_target = metal_object_token(resource->depth_stencil);
    target.native_present_target = prepared ? metal_object_token(resource->drawable) : 0;
    nk::core::write_surface_frame_target(out_target, target);
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_proc_address(nk_handle handle, const char *name,
                                              nk_graphics_proc *out_proc) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!name || !*name || !out_proc)
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid graphics procedure query");
    *out_proc = nullptr;
    if (!surface(handle))
        return fail(NK_ERROR_INVALID_HANDLE, "invalid or stale graphics surface handle");
    return fail(NK_ERROR_UNSUPPORTED, "Metal surfaces do not expose GL procedure addresses");
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
        resource->navigation_policy = (options->flags & NK_WEBVIEW_NAVIGATION_POLICY) != 0;
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
        NSView *parent_content = window_content_view(*parent);
        if (!parent_content)
            return fail(NK_ERROR_UNSUPPORTED, "wrapped Cocoa window has no content view");
        [parent_content addSubview:resource->view];
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
            const nk_handle webview_handle = handle;
            const auto request = nk::core::next_request_id();
            const auto generation = nk::core::runtime_generation();
            evaluations.emplace(request, webview_handle);
            [resource->view
                evaluateJavaScript:wrapped
                 completionHandler:^(id value, NSError *error) {
                   if (!nk::core::is_runtime_generation(generation))
                       return;
                   const auto pending = evaluations.find(request);
                   if (pending == evaluations.end() || pending->second != webview_handle)
                       return;
                   evaluations.erase(pending);
                   if (error)
                       emit_webview_text(NK_EVENT_WEBVIEW_EVAL_COMPLETE, webview_handle,
                                         error.localizedDescription, NK_ERROR_UNKNOWN, 0, request);
                   else
                       emit_webview_text(NK_EVENT_WEBVIEW_EVAL_COMPLETE, webview_handle,
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

nk_result NK_CALL nk_share(const nk_share_options *options) {
    if (const auto result = enter_ui(); result != NK_OK)
        return result;
    if (!options || options->struct_size < sizeof(*options) || options->flags != 0 ||
        (!options->text && options->resource_count == 0))
        return fail(NK_ERROR_INVALID_ARGUMENT, "invalid or empty share options");
    if (const auto result =
            nk::platform::validate_resources(options->resources, options->resource_count, true);
        result != NK_OK)
        return result;
    NSMutableArray *items = [NSMutableArray arrayWithCapacity:options->resource_count + 1];
    if (options->text) {
        NSString *text = string(options->text);
        if (!text)
            return fail(NK_ERROR_INVALID_ARGUMENT, "share text is not valid UTF-8");
        [items addObject:text];
    }
    for (uint32_t index = 0; index < options->resource_count; ++index) {
        NSURL *url = [NSURL URLWithString:string(options->resources[index].uri)];
        if (!url || !url.scheme.length)
            return fail(NK_ERROR_INVALID_ARGUMENT, "resource URI is not a valid URL");
        [items addObject:url];
    }
    NSWindow *window = NSApp.keyWindow ?: NSApp.mainWindow;
    NSView *anchor = window.contentView;
    if (!anchor)
        return fail(NK_ERROR_UNSUPPORTED, "macOS sharing requires an application window");
    NSSharingServicePicker *picker = [[NSSharingServicePicker alloc] initWithItems:items];
    if (!picker)
        return fail(NK_ERROR_OUT_OF_MEMORY, "could not create macOS share picker");
    if (!sharing_pickers)
        sharing_pickers = [NSMutableSet set];
    [sharing_pickers addObject:picker];
    [picker showRelativeToRect:anchor.bounds ofView:anchor preferredEdge:NSMinYEdge];
    return NK_OK;
}

nk_result NK_CALL nk_clipboard_set_resources(const nk_resource *resources,
                                             uint32_t resource_count) {
    return nk::core::result_boundary(
        "unexpected error while writing resource clipboard", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (const auto result =
                    nk::platform::validate_resources(resources, resource_count, false);
                result != NK_OK)
                return result;
            NSMutableArray<NSURL *> *urls = [NSMutableArray arrayWithCapacity:resource_count];
            for (uint32_t index = 0; index < resource_count; ++index) {
                NSURL *url = [NSURL URLWithString:string(resources[index].uri)];
                if (!url || !url.scheme.length)
                    return fail(NK_ERROR_INVALID_ARGUMENT, "resource URI is not a valid URL");
                [urls addObject:url];
            }
            NSPasteboard *pasteboard = NSPasteboard.generalPasteboard;
            [pasteboard clearContents];
            return [pasteboard writeObjects:urls]
                       ? NK_OK
                       : fail(NK_ERROR_UNKNOWN, "macOS rejected resource clipboard data");
        });
}

nk_result NK_CALL nk_clipboard_read_resources(nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while reading resource clipboard", [&]() -> nk_result {
            if (const auto result = enter_ui(); result != NK_OK)
                return result;
            if (!out_request)
                return fail(NK_ERROR_INVALID_ARGUMENT, "clipboard request output is null");
            *out_request = NK_INVALID_REQUEST_ID;
            std::vector<nk::platform::ResourceValue> resources;
            for (NSURL *url in pasteboard_resource_urls(NSPasteboard.generalPasteboard)) {
                if (url.fileURL)
                    retain_security_scope(url);
                resources.push_back(nk::platform::resource_from_uri(
                    utf8(url.absoluteString), NK_RESOURCE_READABLE, {},
                    url.lastPathComponent ? utf8(url.lastPathComponent) : std::string{}));
            }
            const auto request = nk::core::next_request_id();
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE;
            event.request_id = request;
            event.data_count = static_cast<uint32_t>(resources.size());
            event.data = nk::platform::resource_payload(false, resources);
            const auto result = nk::core::push_event(std::move(event));
            if (result != NK_OK)
                return fail(result, "could not queue resource clipboard result");
            *out_request = request;
            return NK_OK;
        });
}

nk_result NK_CALL nk_dialog_open_resource(nk_handle parent, const nk_file_dialog_options *options,
                                          nk_request_id *request) {
    return nk::core::result_boundary(
        "unexpected error while opening resource dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, request, NK_DIALOG_OPEN_RESOURCE);
        });
}

nk_result NK_CALL nk_dialog_save_resource(nk_handle parent, const nk_file_dialog_options *options,
                                          nk_request_id *request) {
    return nk::core::result_boundary(
        "unexpected error while opening resource save dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, request, NK_DIALOG_SAVE_RESOURCE);
        });
}

nk_result NK_CALL nk_dialog_select_resource_directory(nk_handle parent,
                                                      const nk_file_dialog_options *options,
                                                      nk_request_id *request) {
    return nk::core::result_boundary(
        "unexpected error while opening resource directory dialog", [&]() -> nk_result {
            return start_file_dialog(parent, options, request, NK_DIALOG_SELECT_RESOURCE_DIRECTORY);
        });
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
    NSView *content_view = window_content_view(*resource);
    if (!content_view)
        return fail(NK_ERROR_UNSUPPORTED, "wrapped Cocoa window has no content view");
    if (enabled)
        [content_view registerForDraggedTypes:@[ NSPasteboardTypeFileURL, NSPasteboardTypeString ]];
    else
        [content_view unregisterDraggedTypes];
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
        if (!NSBundle.mainBundle.bundleIdentifier.length)
            return fail(NK_ERROR_UNSUPPORTED, "notifications require a bundled macOS application");
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
        notification_center_initialized = true;
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
