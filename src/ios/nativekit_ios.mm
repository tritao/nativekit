#include "nativekit_accessibility.h"
#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_mobile.h"
#include "nativekit_graphics.h"
#include "nativekit_input.h"
#include "nativekit_notification.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

#include "core/event_queue.hpp"
#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/frame_request.hpp"
#include "core/graphics_frame_target.hpp"
#include "core/graphics_image_registry.h"
#include "core/handle_registry.hpp"
#include "core/haptics_internal.hpp"
#include "core/runtime.hpp"
#include "core/sensor_internal.hpp"
#include "core/system_internal.hpp"
#include "ios/joystick.hpp"
#include "core/resource_events.hpp"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <UserNotifications/UserNotifications.h>
#import <WebKit/WebKit.h>
#import <dispatch/dispatch.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

@interface NKIOSHostObserver : NSObject
@property(nonatomic, assign) nk_handle host;
@property(nonatomic, weak) UIView *view;
- (void)orientationChanged:(NSNotification *)notification;
@end

@interface NKIOSSurfaceTimer : NSObject
@property(nonatomic, assign) nk_handle surface;
- (void)tick:(CADisplayLink *)link;
@end

@interface NKIOSInputView : UITextView
@property(nonatomic, assign) nk_handle surface;
- (void)handleHover:(UIHoverGestureRecognizer *)gesture;
@end

@interface NKIOSAccessibilityElement : UIAccessibilityElement
@property(nonatomic, assign) nk_handle surface;
@property(nonatomic, assign) nk_accessibility_node_id node;
@end

@interface NKIOSAccessibilityContainer : UIView
@property(nonatomic, assign) nk_handle surface;
@property(nonatomic, strong) NSArray<NKIOSAccessibilityElement *> *elements;
@end

@interface NKIOSWebViewDelegate : NSObject <WKNavigationDelegate, WKScriptMessageHandler>
@property(nonatomic, assign) void *resource;
@end

@interface NKIOSNotificationDelegate : NSObject <UNUserNotificationCenterDelegate>
@end

@interface NKIOSDocumentPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property(nonatomic, assign) nk_request_id request;
@end

@interface NKIOSDropDelegate : NSObject <UIDropInteractionDelegate>
@property(nonatomic, assign) nk_handle host;
@end

namespace {

template <typename T> std::vector<std::byte> bytes_of(const T &value) {
    const auto *begin = reinterpret_cast<const std::byte *>(&value);
    return std::vector<std::byte>(begin, begin + sizeof(T));
}

struct IOSHost;
nk_orientation ios_orientation(UIDeviceOrientation orientation);
nk_orientation interface_orientation(UIInterfaceOrientation orientation);
struct IOSSurface;
struct IOSWebView;
struct IOSTouchState {
    uint32_t pointer_id = 0;
    nk_touch_tool tool = NK_TOUCH_TOOL_FINGER;
    double x = 0;
    double y = 0;
    float pressure = 0;
    float tilt_x = 0;
    float tilt_y = 0;
};
void queue_geometry(const std::shared_ptr<IOSHost> &host);
void queue_orientation(const std::shared_ptr<IOSHost> &host);
void set_orientation_observing(const std::shared_ptr<IOSHost> &host, bool enabled);
struct IOSSurface;
void update_host_surfaces(const std::shared_ptr<IOSHost> &host);
void reset_surface_input(IOSSurface &surface, uint32_t event_flags = 1u);

struct IOSHost final : nk::core::Resource {
    nk_handle handle = NK_INVALID_HANDLE;
    __strong UIView *view = nil;
    __strong NKIOSHostObserver *observer = nil;
    __strong NKIOSDropDelegate *drop_delegate = nil;
    __strong UIDropInteraction *drop_interaction = nil;
    nk_mobile_lifecycle_state lifecycle = NK_MOBILE_LIFECYCLE_ACTIVE;
    bool orientation_observing = false;
    nk_orientation last_display_orientation = NK_ORIENTATION_UNKNOWN;
    bool drops_enabled = false;
    std::vector<nk_handle> surfaces;
    std::vector<nk_handle> webviews;

    ~IOSHost() override {
        if (view && drop_interaction)
            [view removeInteraction:drop_interaction];
        drop_interaction = nil;
        drop_delegate = nil;
    }
};

struct IOSAccessibilityTextRange {
    nk_accessibility_text_position start = 0;
    nk_accessibility_text_position end = 0;
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

struct IOSAccessibilityNode {
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
    std::vector<IOSAccessibilityTextRange> text_ranges;
};

struct IOSSurface final : nk::core::Resource {
    __strong UIView *host_view = nil;
    __strong CAMetalLayer *layer = nil;
    __strong NKIOSInputView *input_view = nil;
    __strong NKIOSAccessibilityContainer *accessibility_container = nil;
    __strong id<MTLDevice> device = nil;
    __strong id<MTLCommandQueue> queue = nil;
    __strong id<CAMetalDrawable> drawable = nil;
    __strong id<MTLTexture> depth_stencil = nil;
    __strong CADisplayLink *frame_timer = nil;
    __strong NKIOSSurfaceTimer *frame_timer_target = nil;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle parent = NK_INVALID_HANDLE;
    nk_handle device_handle = NK_INVALID_HANDLE;
    nk_surface_flags flags = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t framebuffer_width = 0;
    int32_t framebuffer_height = 0;
    std::shared_ptr<IOSSurface> shared_surface;
    uint32_t share_dependents = 0;
    nk_surface_frame_callback frame_callback = nullptr;
    void *frame_user_data = nullptr;
    nk::core::FrameRequestState frame_requests;
    bool frame_prepared = false;
    bool ready = false;
    bool lost_reported = false;
    bool destroying = false;
    std::array<nk_input_action, NK_KEY_LAST + 1> keys{};
    std::array<nk_input_action, NK_POINTER_BUTTON_LAST + 1> pointer_buttons{};
    double pointer_x = 0;
    double pointer_y = 0;
    std::unordered_map<uintptr_t, IOSTouchState> touch_pointers;
    uint32_t next_touch_pointer_id = 1;
    std::string text_input_text;
    nk_text_input_state text_input_state{};
    bool text_input_active = false;
    bool text_composing = false;
    nk_text_position text_composition_start = NK_TEXT_POSITION_NONE;
    nk_text_position text_composition_end = NK_TEXT_POSITION_NONE;
    std::string marked_text;
    NSRange marked_native_range{NSNotFound, 0};
    bool syncing_input_view = false;
    std::unordered_map<nk_accessibility_node_id, IOSAccessibilityNode> accessibility_nodes;
    nk_accessibility_node_id accessibility_focus = NK_ACCESSIBILITY_ROOT;

    ~IOSSurface() override {
        input_view.surface = NK_INVALID_HANDLE;
        [input_view removeFromSuperview];
        input_view = nil;
        accessibility_container.surface = NK_INVALID_HANDLE;
        [accessibility_container removeFromSuperview];
        accessibility_container = nil;
        [frame_timer invalidate];
        frame_timer = nil;
        frame_timer_target = nil;
        drawable = nil;
        if (layer)
            [layer removeFromSuperlayer];
        layer = nil;
        host_view = nil;
    }
};

struct IOSWebView final : nk::core::Resource {
    __strong WKWebView *view = nil;
    __strong WKUserContentController *content_controller = nil;
    __strong NKIOSWebViewDelegate *delegate = nil;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle parent = NK_INVALID_HANDLE;
    bool observing_title = false;
    bool navigation_policy = false;
    uint64_t generation = 0;

    ~IOSWebView() override {
        if (!view)
            return;
        if (observing_title)
            [view removeObserver:delegate forKeyPath:@"title"];
        view.navigationDelegate = nil;
        [content_controller removeScriptMessageHandlerForName:@"nativekit"];
        delegate.resource = nullptr;
        [view stopLoading];
        [view removeFromSuperview];
        view = nil;
    }
};

std::unordered_map<nk_handle, std::shared_ptr<IOSHost>> hosts;
std::unordered_map<nk_handle, std::shared_ptr<IOSSurface>> surfaces;
nk_orientation last_device_orientation = NK_ORIENTATION_UNKNOWN;
std::unordered_map<nk_handle, std::shared_ptr<IOSWebView>> webviews;

struct IOSNotification {
    uint64_t generation = 0;
    bool silent = false;
};

std::mutex notifications_mutex;
std::unordered_map<nk_request_id, IOSNotification> notifications;
__strong NKIOSNotificationDelegate *notification_delegate = nil;
bool notification_center_initialized = false;

struct IOSResourceValue {
    uint32_t flags = 0;
    std::string uri;
    std::string mime_type;
    std::string display_name;
};

struct IOSDialogContext {
    nk_request_id request = NK_INVALID_REQUEST_ID;
    nk_handle parent = NK_INVALID_HANDLE;
    uint32_t kind = 0;
    uint64_t generation = 0;
    __strong UIViewController *presenter = nil;
    __strong UIViewController *dialog = nil;
    __strong UIDocumentPickerViewController *picker = nil;
    __strong NKIOSDocumentPickerDelegate *picker_delegate = nil;
    __strong NSURL *temporary_url = nil;
    std::vector<uint32_t> message_results;

    ~IOSDialogContext() {
        if (temporary_url)
            [NSFileManager.defaultManager removeItemAtURL:temporary_url error:nil];
    }
};

std::mutex dialogs_mutex;
std::unordered_map<nk_request_id, std::shared_ptr<IOSDialogContext>> dialogs;
__strong NSMutableDictionary<NSString *, NSURL *> *security_scoped_urls = nil;

struct IOSNavigationDecision {
    nk_handle source = NK_INVALID_HANDLE;
    __strong void (^handler)(WKNavigationActionPolicy) = nil;
};

std::unordered_map<nk_request_id, IOSNavigationDecision> navigation_decisions;
std::unordered_map<nk_request_id, nk_handle> evaluations;

std::shared_ptr<IOSHost> host(nk_handle handle) {
    return std::dynamic_pointer_cast<IOSHost>(
        nk::core::handles().get(handle, nk::core::ResourceType::mobile_host));
}

bool has_active_host() {
    for (const auto &[handle, resource] : hosts) {
        (void)handle;
        if (resource->view && resource->lifecycle != NK_MOBILE_LIFECYCLE_BACKGROUND)
            return true;
    }
    return false;
}

UIInterfaceOrientation current_display_orientation() {
    for (const auto &[handle, resource] : hosts) {
        (void)handle;
        if (!resource->view || resource->lifecycle == NK_MOBILE_LIFECYCLE_BACKGROUND)
            continue;
        UIWindow *window = resource->view.window;
        if (window.windowScene)
            return window.windowScene.interfaceOrientation;
    }
    return UIInterfaceOrientationUnknown;
}

std::shared_ptr<IOSSurface> surface(nk_handle handle) {
    const auto found = surfaces.find(handle);
    return found == surfaces.end() ? nullptr : found->second;
}

/* An on-demand surface pauses its display link instead of tearing it down. */
void arm_surface_frames(const std::shared_ptr<IOSSurface> &resource) {
    if (!resource || !resource->frame_timer)
        return;
    resource->frame_timer.paused = NO;
}

void disarm_surface_frames(const std::shared_ptr<IOSSurface> &resource) {
    if (!resource || !resource->frame_timer)
        return;
    resource->frame_timer.paused = YES;
}

std::shared_ptr<IOSWebView> webview(nk_handle handle) {
    return std::dynamic_pointer_cast<IOSWebView>(
        nk::core::handles().get(handle, nk::core::ResourceType::webview));
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

NSString *native_string(const char *value) {
    return value ? [NSString stringWithUTF8String:value] : nil;
}

std::string utf8_string(NSString *value) {
    if (!value)
        return {};
    const char *bytes = value.UTF8String;
    return bytes ? std::string(bytes) : std::string();
}

bool valid_utf8(const char *value) {
    return !value || native_string(value) != nil;
}

std::vector<std::byte> text_bytes(const std::string &value) {
    const auto *begin = reinterpret_cast<const std::byte *>(value.data());
    return {begin, begin + value.size()};
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
            event.data = text_bytes(utf8_string(text));
        nk::core::push_event(std::move(event));
    });
}

bool take_notification(nk_request_id request, IOSNotification *value = nullptr) {
    std::lock_guard lock(notifications_mutex);
    const auto found = notifications.find(request);
    if (found == notifications.end())
        return false;
    if (value)
        *value = found->second;
    notifications.erase(found);
    return true;
}

bool has_notification(nk_request_id request, uint64_t generation) {
    std::lock_guard lock(notifications_mutex);
    const auto found = notifications.find(request);
    return found != notifications.end() && found->second.generation == generation;
}

bool get_notification(nk_request_id request, IOSNotification &value) {
    std::lock_guard lock(notifications_mutex);
    const auto found = notifications.find(request);
    if (found == notifications.end())
        return false;
    value = found->second;
    return true;
}

std::vector<std::byte> resource_payload(bool accepted,
                                        const std::vector<IOSResourceValue> &resources) {
    const auto items_offset = sizeof(nk_resource_list);
    const auto strings_offset = items_offset + resources.size() * sizeof(nk_resource_item);
    std::size_t total = strings_offset;
    for (const auto &resource : resources) {
        total += resource.uri.size() + 1;
        if (!resource.mime_type.empty())
            total += resource.mime_type.size() + 1;
        if (!resource.display_name.empty())
            total += resource.display_name.size() + 1;
    }
    std::vector<std::byte> result(total);
    const nk_resource_list header{accepted ? 1u : 0u, static_cast<uint32_t>(resources.size()),
                                  static_cast<uint32_t>(items_offset),
                                  static_cast<uint32_t>(strings_offset)};
    std::memcpy(result.data(), &header, sizeof(header));
    std::size_t cursor = strings_offset;
    for (std::size_t index = 0; index < resources.size(); ++index) {
        const auto &resource = resources[index];
        nk_resource_item item{};
        item.flags = resource.flags;
        auto append = [&](const std::string &value, uint32_t &offset) {
            if (value.empty())
                return;
            offset = static_cast<uint32_t>(cursor);
            std::memcpy(result.data() + cursor, value.c_str(), value.size() + 1);
            cursor += value.size() + 1;
        };
        append(resource.uri, item.uri_offset);
        append(resource.mime_type, item.mime_type_offset);
        append(resource.display_name, item.display_name_offset);
        std::memcpy(result.data() + items_offset + index * sizeof(item), &item, sizeof(item));
    }
    return result;
}

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

UIViewController *view_controller_for_view(UIView *view) {
    if (!view)
        return nil;
    UIResponder *responder = view;
    while (responder) {
        if ([responder isKindOfClass:[UIViewController class]])
            return (UIViewController *)responder;
        responder = responder.nextResponder;
    }
    return view.window.rootViewController;
}

UIViewController *top_view_controller(UIViewController *controller) {
    while (controller.presentedViewController &&
           !controller.presentedViewController.isBeingDismissed)
        controller = controller.presentedViewController;
    return controller;
}

nk_result ios_fail(nk_result result, const char *message) {
    nk::core::set_error(message);
    return result;
}

nk_result copy_output(NSString *value, char *buffer, uint32_t *inout_size) {
    if (!value || !inout_size)
        return ios_fail(NK_ERROR_INVALID_ARGUMENT, "invalid string output arguments");
    const auto result = utf8_string(value);
    if (result.size() >= std::numeric_limits<uint32_t>::max())
        return ios_fail(NK_ERROR_UNKNOWN, "system string is too large");
    const auto required = static_cast<uint32_t>(result.size() + 1);
    const auto capacity = *inout_size;
    *inout_size = required;
    if (!buffer || capacity < required)
        return ios_fail(NK_ERROR_BUFFER_TOO_SMALL, "output buffer is too small");
    std::memcpy(buffer, result.c_str(), required);
    return NK_OK;
}

constexpr nk_accessibility_states ios_accessibility_states =
    NK_ACCESSIBILITY_FOCUSABLE | NK_ACCESSIBILITY_FOCUSED | NK_ACCESSIBILITY_SELECTED |
    NK_ACCESSIBILITY_CHECKED | NK_ACCESSIBILITY_DISABLED | NK_ACCESSIBILITY_READ_ONLY |
    NK_ACCESSIBILITY_MULTILINE | NK_ACCESSIBILITY_PASSWORD | NK_ACCESSIBILITY_EXPANDED |
    NK_ACCESSIBILITY_MODAL | NK_ACCESSIBILITY_REQUIRED | NK_ACCESSIBILITY_INVALID |
    NK_ACCESSIBILITY_BUSY | NK_ACCESSIBILITY_HAS_POPUP;

constexpr nk_accessibility_actions ios_accessibility_actions =
    NK_ACCESSIBILITY_CAN_ACTIVATE | NK_ACCESSIBILITY_CAN_FOCUS | NK_ACCESSIBILITY_CAN_SET_VALUE |
    NK_ACCESSIBILITY_CAN_SET_SELECTION | NK_ACCESSIBILITY_CAN_INCREMENT |
    NK_ACCESSIBILITY_CAN_DECREMENT | NK_ACCESSIBILITY_CAN_SCROLL_FORWARD |
    NK_ACCESSIBILITY_CAN_SCROLL_BACKWARD | NK_ACCESSIBILITY_CAN_MOVE_NEXT |
    NK_ACCESSIBILITY_CAN_MOVE_PREVIOUS | NK_ACCESSIBILITY_CAN_TOGGLE | NK_ACCESSIBILITY_CAN_SELECT |
    NK_ACCESSIBILITY_CAN_DESELECT | NK_ACCESSIBILITY_CAN_EXPAND | NK_ACCESSIBILITY_CAN_COLLAPSE |
    NK_ACCESSIBILITY_CAN_DISMISS | NK_ACCESSIBILITY_CAN_SHOW_CONTEXT_MENU |
    NK_ACCESSIBILITY_CAN_SCROLL_INTO_VIEW;

bool copy_accessibility_node(
    const nk_accessibility_node &node,
    const std::unordered_map<nk_accessibility_node_id, IOSAccessibilityNode> &nodes,
    IOSAccessibilityNode &copy) {
    std::vector<uint32_t> value_codepoints;
    if (!decode_utf8(node.value ? node.value : "", value_codepoints) || !valid_utf8(node.label) ||
        node.struct_size < sizeof(node) || node.id == NK_ACCESSIBILITY_ROOT ||
        node.role > NK_ACCESSIBILITY_ALERT ||
        node.orientation > NK_ACCESSIBILITY_ORIENTATION_VERTICAL ||
        (node.states & ~ios_accessibility_states) || (node.actions & ~ios_accessibility_actions) ||
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

void remove_accessibility_descendants(
    std::unordered_map<nk_accessibility_node_id, IOSAccessibilityNode> &nodes,
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

UIAccessibilityTraits accessibility_traits(const IOSAccessibilityNode &node) {
    UIAccessibilityTraits traits = UIAccessibilityTraitNone;
    switch (node.role) {
    case NK_ACCESSIBILITY_BUTTON:
    case NK_ACCESSIBILITY_CHECKBOX:
    case NK_ACCESSIBILITY_RADIO:
    case NK_ACCESSIBILITY_SWITCH:
        traits |= UIAccessibilityTraitButton;
        break;
    case NK_ACCESSIBILITY_LINK:
        traits |= UIAccessibilityTraitLink;
        break;
    case NK_ACCESSIBILITY_IMAGE:
        traits |= UIAccessibilityTraitImage;
        break;
    case NK_ACCESSIBILITY_HEADING:
        traits |= UIAccessibilityTraitHeader;
        break;
    case NK_ACCESSIBILITY_TEXT:
    case NK_ACCESSIBILITY_TEXT_FIELD:
        traits |= UIAccessibilityTraitStaticText;
        break;
    case NK_ACCESSIBILITY_SLIDER:
        traits |= UIAccessibilityTraitAdjustable;
        break;
    case NK_ACCESSIBILITY_TAB_LIST:
        traits |= UIAccessibilityTraitTabBar;
        break;
    case NK_ACCESSIBILITY_MENU_ITEM:
        traits |= UIAccessibilityTraitButton;
        break;
    case NK_ACCESSIBILITY_PROGRESS_BAR:
    case NK_ACCESSIBILITY_SCROLL_AREA:
        traits |= UIAccessibilityTraitStaticText;
        break;
    default:
        break;
    }
    if (node.states & (NK_ACCESSIBILITY_SELECTED | NK_ACCESSIBILITY_CHECKED))
        traits |= UIAccessibilityTraitSelected;
    if (node.states & NK_ACCESSIBILITY_DISABLED)
        traits |= UIAccessibilityTraitNotEnabled;
    return traits;
}

nk_accessibility_actions accessibility_action_bit(nk_accessibility_action action) {
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

nk_result emit_accessibility_action(
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
        const auto required = accessibility_action_bit(action);
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

void refresh_accessibility_elements(IOSSurface &resource) noexcept {
    nk::core::callback_boundary([&] {
        if (!resource.accessibility_container)
            return;
        NSMutableArray<NKIOSAccessibilityElement *> *elements =
            [NSMutableArray arrayWithCapacity:resource.accessibility_nodes.size()];
        std::function<void(nk_accessibility_node_id)> append_children =
            [&](nk_accessibility_node_id parent) {
                std::vector<nk_accessibility_node_id> children;
                for (const auto &[id, node] : resource.accessibility_nodes)
                    if (node.parent == parent)
                        children.push_back(id);
                std::sort(children.begin(), children.end(), [&](auto lhs, auto rhs) {
                    const auto left = resource.accessibility_nodes.find(lhs);
                    const auto right = resource.accessibility_nodes.find(rhs);
                    if (left == resource.accessibility_nodes.end() ||
                        right == resource.accessibility_nodes.end())
                        return lhs < rhs;
                    return left->second.child_index == right->second.child_index
                               ? lhs < rhs
                               : left->second.child_index < right->second.child_index;
                });
                for (const auto id : children) {
                    const auto found = resource.accessibility_nodes.find(id);
                    if (found == resource.accessibility_nodes.end())
                        continue;
                    const auto &node = found->second;
                    auto element = [[NKIOSAccessibilityElement alloc]
                        initWithAccessibilityContainer:resource.accessibility_container];
                    if (!element)
                        continue;
                    element.surface = resource.handle;
                    element.node = id;
                    element.accessibilityLabel =
                        node.label.empty() ? nil : native_string(node.label.c_str());
                    element.accessibilityValue =
                        node.value.empty() ? nil : native_string(node.value.c_str());
                    element.accessibilityTraits = accessibility_traits(node);
                    const CGRect local = CGRectMake(node.x, node.y, node.width, node.height);
                    element.accessibilityFrame =
                        resource.input_view ? [resource.input_view convertRect:local toView:nil]
                                            : local;
                    [elements addObject:element];
                    append_children(id);
                }
            };
        append_children(NK_ACCESSIBILITY_ROOT);
        resource.accessibility_container.elements = elements;
        UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification,
                                        resource.accessibility_container);
    });
}

nk_result begin_accessibility_value_edit(nk_handle surface_handle,
                                         nk_accessibility_node_id node) noexcept {
    return nk::core::callback_boundary_or<nk_result>(NK_ERROR_UNKNOWN, [&]() -> nk_result {
        auto resource = surface(surface_handle);
        if (!resource || resource->destroying)
            return NK_ERROR_INVALID_HANDLE;
        const auto found = resource->accessibility_nodes.find(node);
        if (found == resource->accessibility_nodes.end() ||
            !(found->second.actions & NK_ACCESSIBILITY_CAN_SET_VALUE))
            return NK_ERROR_UNSUPPORTED;
        auto presenter = top_view_controller(view_controller_for_view(resource->input_view));
        if (!presenter)
            return NK_ERROR_UNSUPPORTED;
        NSString *label = native_string(found->second.label.c_str());
        if (!label)
            label = @"";
        NSString *value = native_string(found->second.value.c_str());
        if (!value)
            value = @"";
        const bool password = (found->second.states & NK_ACCESSIBILITY_PASSWORD) != 0;
        UIAlertController *alert =
            [UIAlertController alertControllerWithTitle:label
                                                message:@"Enter a new value"
                                         preferredStyle:UIAlertControllerStyleAlert];
        if (!alert)
            return NK_ERROR_OUT_OF_MEMORY;
        [alert addTextFieldWithConfigurationHandler:^(UITextField *field) {
          field.text = value;
          field.secureTextEntry = password;
        }];
        __weak UIAlertController *weak_alert = alert;
        [alert addAction:[UIAlertAction actionWithTitle:@"Cancel"
                                                  style:UIAlertActionStyleCancel
                                                handler:nil]];
        [alert addAction:[UIAlertAction
                             actionWithTitle:@"Set"
                                       style:UIAlertActionStyleDefault
                                     handler:^(UIAlertAction *) {
                                       UIAlertController *strong_alert = weak_alert;
                                       NSString *text = strong_alert.textFields.firstObject.text;
                                       if (!text)
                                           text = @"";
                                       emit_accessibility_action(surface_handle, node,
                                                                 NK_ACCESSIBILITY_ACTION_SET_VALUE,
                                                                 utf8_string(text));
                                     }]];
        [presenter presentViewController:alert animated:YES completion:nil];
        return NK_OK;
    });
}

nk_result open_ios_url(NSURL *url) {
    if (!url || !url.scheme.length)
        return ios_fail(NK_ERROR_INVALID_ARGUMENT, "URL must contain a valid URI scheme");
    UIApplication *application = UIApplication.sharedApplication;
    if (!application)
        return ios_fail(NK_ERROR_UNSUPPORTED, "iOS application services are unavailable");
    [application openURL:url options:@{} completionHandler:nil];
    return NK_OK;
}

NSString *javascript_json_wrapper(NSString *source) {
    NSError *error = nil;
    NSData *encoded = [NSJSONSerialization dataWithJSONObject:@[ source ] options:0 error:&error];
    if (!encoded || error)
        return nil;
    NSString *array = [[NSString alloc] initWithData:encoded encoding:NSUTF8StringEncoding];
    if (!array || array.length < 2)
        return nil;
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
        event.data = text_bytes(utf8_string(text));
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

NSRange native_range_for_positions(const IOSSurface &resource, nk_text_position start,
                                   nk_text_position end) {
    std::vector<uint32_t> points;
    decode_utf8(resource.text_input_text, points);
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

bool codepoint_range_for_native_range(const IOSSurface &resource, NSRange range,
                                      nk_text_position &out_start, nk_text_position &out_end) {
    if (range.location == NSNotFound)
        return false;
    std::vector<uint32_t> points;
    if (!decode_utf8(resource.text_input_text, points))
        return false;
    if (points.size() > std::numeric_limits<uint32_t>::max())
        return false;
    const NSUInteger total_units =
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

void sync_input_view(IOSSurface &resource) {
    if (!resource.input_view)
        return;
    NSString *text = native_string(resource.text_input_text.c_str());
    if (!text)
        return;
    const auto selection =
        native_range_for_positions(resource, resource.text_input_state.selection_start,
                                   resource.text_input_state.selection_end);
    resource.syncing_input_view = true;
    resource.input_view.text = text;
    resource.input_view.selectedRange = selection;
    resource.input_view.hidden = (resource.flags & NK_SURFACE_HIDDEN) != 0;
    resource.syncing_input_view = false;
    [resource.input_view reloadInputViews];
}

void update_text_snapshot(IOSSurface &resource, nk_text_position replace_start,
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
    updated.replace(byte_offset(first), byte_offset(last) - byte_offset(first), inserted.data(),
                    inserted.size());
    const int64_t delta =
        static_cast<int64_t>(inserted_codepoints.size()) - static_cast<int64_t>(last - first);
    resource.text_input_text = std::move(updated);
    resource.text_input_state.text = resource.text_input_text.c_str();
    resource.text_input_state.document_length = static_cast<nk_text_position>(std::max<int64_t>(
        0, static_cast<int64_t>(resource.text_input_state.document_length) + delta));
}

void emit_text_edit(IOSSurface &resource, nk_text_edit_event payload,
                    const std::string &text = {}) {
    payload.text_offset = text.empty() ? 0u : sizeof(payload);
    payload.text_length = static_cast<uint32_t>(text.size());
    std::vector<std::byte> data(sizeof(payload) + text.size() + (text.empty() ? 0u : 1u));
    std::memcpy(data.data(), &payload, sizeof(payload));
    if (!text.empty())
        std::memcpy(data.data() + sizeof(payload), text.c_str(), text.size() + 1);
    queue_input_event(NK_EVENT_TEXT_EDIT, resource.handle, std::move(data));
}

void apply_text_edit_state(IOSSurface &resource, nk_text_edit_action action,
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
    sync_input_view(resource);
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

nk_text_position text_replacement_start(const IOSSurface &resource) {
    return resource.text_composing ? resource.text_composition_start
                                   : resource.text_input_state.selection_start;
}

nk_text_position text_replacement_end(const IOSSurface &resource) {
    return resource.text_composing ? resource.text_composition_end
                                   : resource.text_input_state.selection_end;
}

void emit_committed_text(IOSSurface &resource, const std::string &text) {
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

void finish_text_composition(IOSSurface &resource) {
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

nk_modifiers modifiers_from_native(UIKeyModifierFlags flags) {
    nk_modifiers result = 0;
    if (flags & UIKeyModifierShift)
        result |= NK_MOD_SHIFT;
    if (flags & UIKeyModifierControl)
        result |= NK_MOD_CONTROL;
    if (flags & UIKeyModifierAlternate)
        result |= NK_MOD_ALT;
    if (flags & UIKeyModifierCommand)
        result |= NK_MOD_SUPER;
    if (flags & UIKeyModifierAlphaShift)
        result |= NK_MOD_CAPS_LOCK;
    return result;
}

nk_key key_from_hid(UIKeyboardHIDUsage code) {
    if (code >= UIKeyboardHIDUsageKeyboardA && code <= UIKeyboardHIDUsageKeyboardZ)
        return static_cast<nk_key>(NK_KEY_A + code - UIKeyboardHIDUsageKeyboardA);
    if (code >= UIKeyboardHIDUsageKeyboard1 && code <= UIKeyboardHIDUsageKeyboard0) {
        constexpr nk_key digits[] = {NK_KEY_1, NK_KEY_2, NK_KEY_3, NK_KEY_4, NK_KEY_5,
                                     NK_KEY_6, NK_KEY_7, NK_KEY_8, NK_KEY_9, NK_KEY_0};
        return digits[code - UIKeyboardHIDUsageKeyboard1];
    }
    if (code >= UIKeyboardHIDUsageKeyboardF1 && code <= UIKeyboardHIDUsageKeyboardF12)
        return static_cast<nk_key>(NK_KEY_F1 + code - UIKeyboardHIDUsageKeyboardF1);
    if (code >= UIKeyboardHIDUsageKeyboardF13 && code <= UIKeyboardHIDUsageKeyboardF24)
        return static_cast<nk_key>(NK_KEY_F13 + code - UIKeyboardHIDUsageKeyboardF13);
    switch (code) {
    case UIKeyboardHIDUsageKeyboardSpacebar:
        return NK_KEY_SPACE;
    case UIKeyboardHIDUsageKeyboardHyphen:
        return NK_KEY_MINUS;
    case UIKeyboardHIDUsageKeyboardEqualSign:
        return NK_KEY_EQUAL;
    case UIKeyboardHIDUsageKeyboardOpenBracket:
        return NK_KEY_LEFT_BRACKET;
    case UIKeyboardHIDUsageKeyboardCloseBracket:
        return NK_KEY_RIGHT_BRACKET;
    case UIKeyboardHIDUsageKeyboardBackslash:
        return NK_KEY_BACKSLASH;
    case UIKeyboardHIDUsageKeyboardSemicolon:
        return NK_KEY_SEMICOLON;
    case UIKeyboardHIDUsageKeyboardQuote:
        return NK_KEY_APOSTROPHE;
    case UIKeyboardHIDUsageKeyboardGraveAccentAndTilde:
        return NK_KEY_GRAVE_ACCENT;
    case UIKeyboardHIDUsageKeyboardComma:
        return NK_KEY_COMMA;
    case UIKeyboardHIDUsageKeyboardPeriod:
        return NK_KEY_PERIOD;
    case UIKeyboardHIDUsageKeyboardSlash:
        return NK_KEY_SLASH;
    case UIKeyboardHIDUsageKeyboardReturnOrEnter:
        return NK_KEY_ENTER;
    case UIKeyboardHIDUsageKeyboardEscape:
        return NK_KEY_ESCAPE;
    case UIKeyboardHIDUsageKeyboardDeleteOrBackspace:
        return NK_KEY_BACKSPACE;
    case UIKeyboardHIDUsageKeyboardTab:
        return NK_KEY_TAB;
    case UIKeyboardHIDUsageKeyboardCapsLock:
        return NK_KEY_CAPS_LOCK;
    case UIKeyboardHIDUsageKeyboardPrintScreen:
        return NK_KEY_PRINT_SCREEN;
    case UIKeyboardHIDUsageKeyboardScrollLock:
        return NK_KEY_SCROLL_LOCK;
    case UIKeyboardHIDUsageKeyboardPause:
        return NK_KEY_PAUSE;
    case UIKeyboardHIDUsageKeyboardInsert:
        return NK_KEY_INSERT;
    case UIKeyboardHIDUsageKeyboardHome:
        return NK_KEY_HOME;
    case UIKeyboardHIDUsageKeyboardPageUp:
        return NK_KEY_PAGE_UP;
    case UIKeyboardHIDUsageKeyboardDeleteForward:
        return NK_KEY_DELETE;
    case UIKeyboardHIDUsageKeyboardEnd:
        return NK_KEY_END;
    case UIKeyboardHIDUsageKeyboardPageDown:
        return NK_KEY_PAGE_DOWN;
    case UIKeyboardHIDUsageKeyboardRightArrow:
        return NK_KEY_RIGHT;
    case UIKeyboardHIDUsageKeyboardLeftArrow:
        return NK_KEY_LEFT;
    case UIKeyboardHIDUsageKeyboardDownArrow:
        return NK_KEY_DOWN;
    case UIKeyboardHIDUsageKeyboardUpArrow:
        return NK_KEY_UP;
    case UIKeyboardHIDUsageKeypadNumLock:
        return NK_KEY_NUM_LOCK;
    case UIKeyboardHIDUsageKeypadSlash:
        return NK_KEY_KP_DIVIDE;
    case UIKeyboardHIDUsageKeypadAsterisk:
        return NK_KEY_KP_MULTIPLY;
    case UIKeyboardHIDUsageKeypadHyphen:
        return NK_KEY_KP_SUBTRACT;
    case UIKeyboardHIDUsageKeypadPlus:
        return NK_KEY_KP_ADD;
    case UIKeyboardHIDUsageKeypadEnter:
        return NK_KEY_KP_ENTER;
    case UIKeyboardHIDUsageKeypad1:
        return NK_KEY_KP_1;
    case UIKeyboardHIDUsageKeypad2:
        return NK_KEY_KP_2;
    case UIKeyboardHIDUsageKeypad3:
        return NK_KEY_KP_3;
    case UIKeyboardHIDUsageKeypad4:
        return NK_KEY_KP_4;
    case UIKeyboardHIDUsageKeypad5:
        return NK_KEY_KP_5;
    case UIKeyboardHIDUsageKeypad6:
        return NK_KEY_KP_6;
    case UIKeyboardHIDUsageKeypad7:
        return NK_KEY_KP_7;
    case UIKeyboardHIDUsageKeypad8:
        return NK_KEY_KP_8;
    case UIKeyboardHIDUsageKeypad9:
        return NK_KEY_KP_9;
    case UIKeyboardHIDUsageKeypad0:
        return NK_KEY_KP_0;
    case UIKeyboardHIDUsageKeypadPeriod:
        return NK_KEY_KP_DECIMAL;
    case UIKeyboardHIDUsageKeypadEqualSign:
        return NK_KEY_KP_EQUAL;
    case UIKeyboardHIDUsageKeyboardLeftControl:
        return NK_KEY_LEFT_CONTROL;
    case UIKeyboardHIDUsageKeyboardLeftShift:
        return NK_KEY_LEFT_SHIFT;
    case UIKeyboardHIDUsageKeyboardLeftAlt:
        return NK_KEY_LEFT_ALT;
    case UIKeyboardHIDUsageKeyboardLeftGUI:
        return NK_KEY_LEFT_SUPER;
    case UIKeyboardHIDUsageKeyboardRightControl:
        return NK_KEY_RIGHT_CONTROL;
    case UIKeyboardHIDUsageKeyboardRightShift:
        return NK_KEY_RIGHT_SHIFT;
    case UIKeyboardHIDUsageKeyboardRightAlt:
        return NK_KEY_RIGHT_ALT;
    case UIKeyboardHIDUsageKeyboardRightGUI:
        return NK_KEY_RIGHT_SUPER;
    default:
        return NK_KEY_UNKNOWN;
    }
}

void emit_key_transition(IOSSurface &resource, UIKey *key, nk_input_action action) {
    if (!key)
        return;
    const auto code = key.keyCode;
    const auto normalized = key_from_hid(code);
    const auto modifiers = modifiers_from_native(key.modifierFlags);
    if (normalized != NK_KEY_UNKNOWN)
        resource.keys[normalized] = action == NK_INPUT_RELEASE ? NK_INPUT_RELEASE : NK_INPUT_PRESS;
    const nk_key_event payload{normalized, static_cast<uint32_t>(code), action, modifiers};
    queue_input_event(NK_EVENT_KEY, resource.handle, bytes_of(payload));
}

void emit_pointer_move(IOSSurface &resource, double x, double y) {
    resource.pointer_x = x;
    resource.pointer_y = y;
    const nk_pointer_move_event payload{x, y};
    queue_input_event(NK_EVENT_POINTER_MOVE, resource.handle, bytes_of(payload));
}

void emit_pointer_button(IOSSurface &resource, nk_pointer_button button, nk_input_action action,
                         nk_modifiers modifiers, double x, double y, uint32_t flags = 0) {
    resource.pointer_buttons[button] = action;
    resource.pointer_x = x;
    resource.pointer_y = y;
    const nk_pointer_button_event payload{button, action, modifiers, 0, x, y};
    queue_input_event(NK_EVENT_POINTER_BUTTON, resource.handle, bytes_of(payload), flags);
}

void emit_touch(IOSSurface &resource, const IOSTouchState &touch, nk_touch_action action,
                nk_modifiers modifiers, uint32_t flags = 0) {
    const nk_touch_event payload{touch.pointer_id, action,  touch.tool,     modifiers,
                                 touch.x,          touch.y, touch.pressure, touch.tilt_x,
                                 touch.tilt_y,     0};
    queue_input_event(NK_EVENT_TOUCH, resource.handle, bytes_of(payload), flags);
}

bool is_indirect_pointer(UITouch *touch) {
    return touch.type == UITouchTypeIndirect || touch.type == UITouchTypeIndirectPointer;
}

IOSTouchState touch_state_for(UITouch *touch, UIView *view, uint32_t pointer_id) {
    const CGPoint point = [touch locationInView:view];
    const CGFloat maximum_force = touch.maximumPossibleForce;
    const CGFloat force = touch.force;
    const float pressure = maximum_force > 0 && force > 0
                               ? static_cast<float>(std::min<CGFloat>(1.0, force / maximum_force))
                               : 1.0f;
    IOSTouchState result;
    result.pointer_id = pointer_id;
    result.tool = (touch.type == UITouchTypePencil || touch.type == UITouchTypeStylus)
                      ? NK_TOUCH_TOOL_STYLUS
                      : NK_TOUCH_TOOL_FINGER;
    result.x = point.x;
    result.y = point.y;
    result.pressure = pressure;
    if (result.tool == NK_TOUCH_TOOL_STYLUS) {
        const CGFloat tilt = std::max<CGFloat>(0, 1.5707963267948966 - touch.altitudeAngle);
        const CGFloat azimuth = [touch azimuthAngleInView:view];
        result.tilt_x = static_cast<float>(std::sin(tilt) * std::cos(azimuth));
        result.tilt_y = static_cast<float>(std::sin(tilt) * std::sin(azimuth));
    }
    return result;
}

void emit_touch_transition(NKIOSInputView *view, NSSet<UITouch *> *touches, nk_touch_action action,
                           UIEvent *event) {
    auto resource = surface(view.surface);
    if (!resource || resource->destroying)
        return;
    const auto modifiers = modifiers_from_native(event.modifierFlags);
    for (UITouch *touch in touches) {
        const auto identity = reinterpret_cast<uintptr_t>((__bridge void *)touch);
        auto found = resource->touch_pointers.find(identity);
        if (action == NK_TOUCH_BEGIN) {
            if (found == resource->touch_pointers.end()) {
                uint32_t pointer_id = resource->next_touch_pointer_id++;
                if (!pointer_id)
                    pointer_id = resource->next_touch_pointer_id++;
                found = resource->touch_pointers
                            .emplace(identity, touch_state_for(touch, view, pointer_id))
                            .first;
            }
        } else if (found == resource->touch_pointers.end()) {
            continue;
        }
        found->second = touch_state_for(touch, view, found->second.pointer_id);
        if (is_indirect_pointer(touch)) {
            if (action == NK_TOUCH_MOVE)
                emit_pointer_move(*resource, found->second.x, found->second.y);
            else if (action == NK_TOUCH_BEGIN)
                emit_pointer_button(*resource, NK_POINTER_BUTTON_LEFT, NK_INPUT_PRESS, modifiers,
                                    found->second.x, found->second.y);
            else if (action == NK_TOUCH_END || action == NK_TOUCH_CANCEL)
                emit_pointer_button(*resource, NK_POINTER_BUTTON_LEFT, NK_INPUT_RELEASE, modifiers,
                                    found->second.x, found->second.y,
                                    action == NK_TOUCH_CANCEL ? 1u : 0u);
        } else {
            emit_touch(*resource, found->second, action, modifiers,
                       action == NK_TOUCH_CANCEL ? 1u : 0u);
        }
        if (action == NK_TOUCH_END || action == NK_TOUCH_CANCEL)
            resource->touch_pointers.erase(found);
    }
}

UITextRange *native_text_range(NKIOSInputView *view, NSRange range) {
    if (range.location == NSNotFound)
        return nil;
    UITextPosition *start = [view positionFromPosition:view.beginningOfDocument
                                                offset:static_cast<NSInteger>(range.location)];
    UITextPosition *end = [view positionFromPosition:start
                                              offset:static_cast<NSInteger>(range.length)];
    return start && end ? [view textRangeFromPosition:start toPosition:end] : nil;
}

NSRange native_range_for_text_range(NKIOSInputView *view, UITextRange *range) {
    if (!range)
        return NSMakeRange(NSNotFound, 0);
    const NSInteger start = [view offsetFromPosition:view.beginningOfDocument
                                          toPosition:range.start];
    const NSInteger end = [view offsetFromPosition:view.beginningOfDocument toPosition:range.end];
    if (start < 0 || end < start)
        return NSMakeRange(NSNotFound, 0);
    return NSMakeRange(static_cast<NSUInteger>(start), static_cast<NSUInteger>(end - start));
}

void set_input_traits(IOSSurface &resource) {
    if (!resource.input_view)
        return;
    switch (resource.text_input_state.input_type) {
    case NK_TEXT_INPUT_EMAIL:
        resource.input_view.keyboardType = UIKeyboardTypeEmailAddress;
        break;
    case NK_TEXT_INPUT_URL:
        resource.input_view.keyboardType = UIKeyboardTypeURL;
        break;
    case NK_TEXT_INPUT_NUMBER:
        resource.input_view.keyboardType = UIKeyboardTypeNumbersAndPunctuation;
        break;
    case NK_TEXT_INPUT_PHONE:
        resource.input_view.keyboardType = UIKeyboardTypePhonePad;
        break;
    default:
        resource.input_view.keyboardType = UIKeyboardTypeDefault;
        break;
    }
    resource.input_view.secureTextEntry =
        resource.text_input_state.input_type == NK_TEXT_INPUT_PASSWORD;
    resource.input_view.autocorrectionType =
        (resource.text_input_state.flags & NK_TEXT_INPUT_AUTOCORRECT) ? UITextAutocorrectionTypeYes
                                                                      : UITextAutocorrectionTypeNo;
    resource.input_view.autocapitalizationType =
        (resource.text_input_state.flags & NK_TEXT_INPUT_CAPITALIZE_SENTENCES)
            ? UITextAutocapitalizationTypeSentences
            : UITextAutocapitalizationTypeNone;
    if (resource.text_input_state.flags & NK_TEXT_INPUT_MULTILINE) {
        resource.input_view.returnKeyType = UIReturnKeyDefault;
    } else {
        switch (resource.text_input_state.action) {
        case NK_TEXT_INPUT_ACTION_DONE:
            resource.input_view.returnKeyType = UIReturnKeyDone;
            break;
        case NK_TEXT_INPUT_ACTION_GO:
            resource.input_view.returnKeyType = UIReturnKeyGo;
            break;
        case NK_TEXT_INPUT_ACTION_NEXT:
            resource.input_view.returnKeyType = UIReturnKeyNext;
            break;
        case NK_TEXT_INPUT_ACTION_SEARCH:
            resource.input_view.returnKeyType = UIReturnKeySearch;
            break;
        case NK_TEXT_INPUT_ACTION_SEND:
            resource.input_view.returnKeyType = UIReturnKeySend;
            break;
        case NK_TEXT_INPUT_ACTION_NONE:
            resource.input_view.returnKeyType = UIReturnKeyDefault;
            break;
        default:
            resource.input_view.returnKeyType = UIReturnKeyDefault;
            break;
        }
    }
    [resource.input_view reloadInputViews];
}

void reset_surface_input(IOSSurface &resource, uint32_t event_flags) {
    if (resource.text_input_active)
        finish_text_composition(resource);
    else {
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
        queue_input_event(NK_EVENT_KEY, resource.handle, bytes_of(payload), event_flags);
    }
    for (nk_pointer_button button = 0; button <= NK_POINTER_BUTTON_LAST; ++button) {
        if (resource.pointer_buttons[button] != NK_INPUT_PRESS)
            continue;
        const nk_pointer_button_event payload{button, NK_INPUT_RELEASE,   0,
                                              0,      resource.pointer_x, resource.pointer_y};
        resource.pointer_buttons[button] = NK_INPUT_RELEASE;
        queue_input_event(NK_EVENT_POINTER_BUTTON, resource.handle, bytes_of(payload), event_flags);
    }
    for (const auto &[identity, touch] : resource.touch_pointers) {
        (void)identity;
        emit_touch(resource, touch, NK_TOUCH_CANCEL, 0, event_flags);
    }
    resource.touch_pointers.clear();
}

void stop_observing(const std::shared_ptr<IOSHost> &resource) {
    if (!resource || !resource->view || !resource->observer)
        return;
    [resource->view removeObserver:resource->observer forKeyPath:@"bounds"];
    [resource->view removeObserver:resource->observer forKeyPath:@"safeAreaInsets"];
    [resource->view removeObserver:resource->observer forKeyPath:@"contentScaleFactor"];
    set_orientation_observing(resource, false);
    resource->observer.view = nil;
    resource->observer = nil;
}

void queue_geometry(const std::shared_ptr<IOSHost> &resource) {
    if (!resource || !resource->view)
        return;
    const auto view = resource->view;
    const auto bounds = view.bounds;
    const auto insets = view.safeAreaInsets;
    const auto scale = view.contentScaleFactor > 0 ? view.contentScaleFactor : 1.0f;
    const nk_mobile_host_geometry geometry{
        sizeof(nk_mobile_host_geometry),
        static_cast<int32_t>(std::lround(CGRectGetWidth(bounds))),
        static_cast<int32_t>(std::lround(CGRectGetHeight(bounds))),
        static_cast<float>(scale),
        static_cast<int32_t>(std::lround(insets.left)),
        static_cast<int32_t>(std::lround(insets.top)),
        static_cast<int32_t>(std::lround(insets.right)),
        static_cast<int32_t>(std::lround(insets.bottom)),
        0,
        {0, 0}};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_MOBILE_HOST_GEOMETRY_CHANGED;
    event.source = resource->handle;
    event.data = bytes_of(geometry);
    nk::core::push_event(std::move(event));
    update_host_surfaces(resource);
}

void queue_orientation(const std::shared_ptr<IOSHost> &resource) {
    if (!resource || !resource->view || resource->lifecycle == NK_MOBILE_LIFECYCLE_BACKGROUND)
        return;
    const auto device = ios_orientation(UIDevice.currentDevice.orientation);
    if (device != NK_ORIENTATION_UNKNOWN && device != last_device_orientation) {
        last_device_orientation = device;
        const nk_orientation_event payload{sizeof(nk_orientation_event), device, 0, {0, 0}};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_DEVICE_ORIENTATION_CHANGED;
        event.source = NK_INVALID_HANDLE;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    }
    UIInterfaceOrientation display = UIInterfaceOrientationUnknown;
    UIWindow *window = resource->view.window;
    if (window.windowScene)
        display = window.windowScene.interfaceOrientation;
    const auto orientation = interface_orientation(display);
    if (orientation == NK_ORIENTATION_UNKNOWN || orientation == resource->last_display_orientation)
        return;
    resource->last_display_orientation = orientation;
    const nk_orientation_event payload{sizeof(nk_orientation_event), orientation, 0, {0, 0}};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_DISPLAY_ORIENTATION_CHANGED;
    event.source = resource->handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

void set_orientation_observing(const std::shared_ptr<IOSHost> &resource, bool enabled) {
    if (!resource || !resource->observer || resource->orientation_observing == enabled)
        return;
    UIDevice *device = UIDevice.currentDevice;
    if (enabled) {
        [device beginGeneratingDeviceOrientationNotifications];
        [NSNotificationCenter.defaultCenter addObserver:resource->observer
                                               selector:@selector(orientationChanged:)
                                                   name:UIDeviceOrientationDidChangeNotification
                                                 object:device];
    } else {
        [NSNotificationCenter.defaultCenter removeObserver:resource->observer
                                                      name:UIDeviceOrientationDidChangeNotification
                                                    object:device];
        [device endGeneratingDeviceOrientationNotifications];
    }
    resource->orientation_observing = enabled;
}

void observe_view(const std::shared_ptr<IOSHost> &resource, nk_handle handle) {
    auto *observer = [NKIOSHostObserver new];
    observer.host = handle;
    observer.view = resource->view;
    resource->observer = observer;
    [resource->view addObserver:observer forKeyPath:@"bounds" options:0 context:nullptr];
    [resource->view addObserver:observer forKeyPath:@"safeAreaInsets" options:0 context:nullptr];
    [resource->view addObserver:observer
                     forKeyPath:@"contentScaleFactor"
                        options:0
                        context:nullptr];
    set_orientation_observing(resource, true);
}

uint64_t metal_object_token(id object) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>((__bridge void *)object));
}

void emit_surface_lost(IOSSurface &resource) {
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

void emit_surface_resize(IOSSurface &resource) {
    const nk_surface_resize_event payload{resource.width, resource.height,
                                          resource.framebuffer_width, resource.framebuffer_height};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_SURFACE_RESIZE;
    event.source = resource.handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

void sync_surface_drawable_size(IOSSurface &resource) {
    if (!resource.host_view || !resource.layer)
        return;
    const CGFloat scale =
        resource.host_view.contentScaleFactor > 0 ? resource.host_view.contentScaleFactor : 1.0;
    const CGSize size = resource.layer.bounds.size;
    resource.layer.contentsScale = scale;
    resource.layer.drawableSize = CGSizeMake(std::max<CGFloat>(0, size.width * scale),
                                             std::max<CGFloat>(0, size.height * scale));
    const int32_t framebuffer_width = static_cast<int32_t>(resource.layer.drawableSize.width);
    const int32_t framebuffer_height = static_cast<int32_t>(resource.layer.drawableSize.height);
    if (resource.framebuffer_width == framebuffer_width &&
        resource.framebuffer_height == framebuffer_height)
        return;
    const bool changed = resource.framebuffer_width != 0 || resource.framebuffer_height != 0;
    resource.framebuffer_width = framebuffer_width;
    resource.framebuffer_height = framebuffer_height;
    resource.depth_stencil = nil;
    resource.drawable = nil;
    resource.frame_prepared = false;
    if (resource.ready && changed)
        emit_surface_resize(resource);
}

bool set_surface_native_bounds(IOSSurface &resource) {
    if (!resource.layer)
        return false;
    resource.layer.frame = CGRectMake(resource.x, resource.y, resource.width, resource.height);
    if (resource.input_view)
        resource.input_view.frame = resource.layer.frame;
    if (resource.accessibility_container)
        resource.accessibility_container.frame = resource.layer.frame;
    sync_surface_drawable_size(resource);
    refresh_accessibility_elements(resource);
    return true;
}

void update_host_surfaces(const std::shared_ptr<IOSHost> &resource) {
    if (!resource)
        return;
    for (const nk_handle handle : resource->surfaces)
        if (auto child = surface(handle)) {
            child->host_view = resource->view;
            sync_surface_drawable_size(*child);
            refresh_accessibility_elements(*child);
        }
}

bool surface_frame_available(const IOSSurface &resource) {
    auto parent = host(resource.parent);
    return resource.layer && resource.device && resource.queue && parent && parent->view &&
           parent->lifecycle == NK_MOBILE_LIFECYCLE_ACTIVE && !parent->view.hidden &&
           !resource.layer.hidden && resource.framebuffer_width > 0 &&
           resource.framebuffer_height > 0;
}

bool ensure_surface_depth_target(IOSSurface &resource, int32_t width, int32_t height) {
    if (!(resource.flags & (NK_SURFACE_DEPTH | NK_SURFACE_STENCIL))) {
        resource.depth_stencil = nil;
        return true;
    }
    if (resource.depth_stencil && resource.depth_stencil.width == static_cast<NSUInteger>(width) &&
        resource.depth_stencil.height == static_cast<NSUInteger>(height))
        return true;
    MTLTextureDescriptor *descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
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

void frame_tick(nk_handle handle) noexcept {
    nk::core::callback_boundary([&] {
        auto resource = surface(handle);
        if (!resource || !resource->frame_callback || resource->destroying)
            return;
        if (!resource->frame_requests.should_draw()) {
            disarm_surface_frames(resource);
            return;
        }
        resource->frame_requests.begin_frame();
        if (nk_surface_make_current(handle) != NK_OK)
            return;
        auto active = surface(handle);
        if (!active || !active->frame_callback)
            return;
        const auto callback = active->frame_callback;
        void *user_data = active->frame_user_data;
        callback(handle, active->framebuffer_width, active->framebuffer_height, user_data);
        auto current = surface(handle);
        if (current && current->frame_prepared)
            nk_surface_present(handle);
    });
}

} // namespace

void finish_document_dialog(nk_request_id request, bool accepted, NSArray<NSURL *> *urls) noexcept;
void finish_message_dialog(nk_request_id request, nk_message_result result) noexcept;
void cancel_dialogs_for_parent(nk_handle parent);

@implementation NKIOSHostObserver
- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id> *)change
                       context:(void *)context {
    (void)keyPath;
    (void)object;
    (void)change;
    (void)context;
    if (self.host == NK_INVALID_HANDLE)
        return;
    queue_geometry(host(self.host));
    queue_orientation(host(self.host));
}

- (void)orientationChanged:(NSNotification *)notification {
    (void)notification;
    if (self.host == NK_INVALID_HANDLE)
        return;
    queue_orientation(host(self.host));
}
@end

@implementation NKIOSSurfaceTimer
- (void)tick:(CADisplayLink *)link {
    (void)link;
    frame_tick(self.surface);
}
@end

@implementation NKIOSAccessibilityElement
- (BOOL)accessibilityActivate {
    return emit_accessibility_action(self.surface, self.node, NK_ACCESSIBILITY_ACTION_ACTIVATE) ==
           NK_OK;
}

- (void)accessibilityElementDidBecomeFocused {
    emit_accessibility_action(self.surface, self.node, NK_ACCESSIBILITY_ACTION_FOCUS);
}

- (void)accessibilityElementDidLoseFocus {
    emit_accessibility_action(self.surface, self.node, NK_ACCESSIBILITY_ACTION_CLEAR_FOCUS);
}

- (void)accessibilityIncrement {
    emit_accessibility_action(self.surface, self.node, NK_ACCESSIBILITY_ACTION_INCREMENT);
}

- (void)accessibilityDecrement {
    emit_accessibility_action(self.surface, self.node, NK_ACCESSIBILITY_ACTION_DECREMENT);
}

- (BOOL)accessibilityScroll:(UIAccessibilityScrollDirection)direction {
    nk_accessibility_action action = NK_ACCESSIBILITY_ACTION_SCROLL_FORWARD;
    switch (direction) {
    case UIAccessibilityScrollDirectionUp:
    case UIAccessibilityScrollDirectionLeft:
    case UIAccessibilityScrollDirectionPrevious:
        action = NK_ACCESSIBILITY_ACTION_SCROLL_BACKWARD;
        break;
    case UIAccessibilityScrollDirectionDown:
    case UIAccessibilityScrollDirectionRight:
    case UIAccessibilityScrollDirectionNext:
        action = NK_ACCESSIBILITY_ACTION_SCROLL_FORWARD;
        break;
    }
    return emit_accessibility_action(self.surface, self.node, action) == NK_OK;
}

- (NSArray<UIAccessibilityCustomAction *> *)accessibilityCustomActions {
    auto resource = surface(self.surface);
    if (!resource)
        return @[];
    const auto found = resource->accessibility_nodes.find(self.node);
    if (found == resource->accessibility_nodes.end())
        return @[];
    const auto actions = found->second.actions;
    NSMutableArray<UIAccessibilityCustomAction *> *result = [NSMutableArray array];
    const nk_handle surface_handle = self.surface;
    const nk_accessibility_node_id node = self.node;
    auto add_action = [&](nk_accessibility_action action, NSString *title) {
        if (!(actions & accessibility_action_bit(action)))
            return;
        [result addObject:[[UIAccessibilityCustomAction alloc]
                               initWithName:title
                              actionHandler:^BOOL(UIAccessibilityCustomAction *custom_action) {
                                (void)custom_action;
                                if (action == NK_ACCESSIBILITY_ACTION_SET_VALUE)
                                    return begin_accessibility_value_edit(surface_handle, node) ==
                                           NK_OK;
                                return emit_accessibility_action(surface_handle, node, action) ==
                                       NK_OK;
                              }]];
    };
    add_action(NK_ACCESSIBILITY_ACTION_SET_VALUE, @"Edit value");
    add_action(NK_ACCESSIBILITY_ACTION_SET_SELECTION, @"Set selection");
    add_action(NK_ACCESSIBILITY_ACTION_SCROLL_FORWARD, @"Scroll forward");
    add_action(NK_ACCESSIBILITY_ACTION_SCROLL_BACKWARD, @"Scroll backward");
    add_action(NK_ACCESSIBILITY_ACTION_MOVE_NEXT, @"Next");
    add_action(NK_ACCESSIBILITY_ACTION_MOVE_PREVIOUS, @"Previous");
    add_action(NK_ACCESSIBILITY_ACTION_TOGGLE, @"Toggle");
    add_action(NK_ACCESSIBILITY_ACTION_SELECT, @"Select");
    add_action(NK_ACCESSIBILITY_ACTION_DESELECT, @"Deselect");
    add_action(NK_ACCESSIBILITY_ACTION_EXPAND, @"Expand");
    add_action(NK_ACCESSIBILITY_ACTION_COLLAPSE, @"Collapse");
    add_action(NK_ACCESSIBILITY_ACTION_DISMISS, @"Dismiss");
    add_action(NK_ACCESSIBILITY_ACTION_SHOW_CONTEXT_MENU, @"Show menu");
    add_action(NK_ACCESSIBILITY_ACTION_SCROLL_INTO_VIEW, @"Scroll into view");
    return result;
}
@end

@implementation NKIOSAccessibilityContainer
- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = UIColor.clearColor;
        self.userInteractionEnabled = NO;
        self.isAccessibilityElement = NO;
        self.elements = @[];
    }
    return self;
}

- (NSInteger)accessibilityElementCount {
    return static_cast<NSInteger>(self.elements.count);
}

- (id)accessibilityElementAtIndex:(NSInteger)index {
    if (index < 0 || static_cast<NSUInteger>(index) >= self.elements.count)
        return nil;
    return self.elements[static_cast<NSUInteger>(index)];
}

- (NSInteger)indexOfAccessibilityElement:(id)element {
    const NSUInteger index = [self.elements indexOfObject:element];
    return index == NSNotFound ? NSNotFound : static_cast<NSInteger>(index);
}
@end

@implementation NKIOSInputView
- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = UIColor.clearColor;
        self.textColor = UIColor.clearColor;
        self.tintColor = UIColor.clearColor;
        self.editable = YES;
        self.selectable = NO;
        self.scrollEnabled = NO;
        self.userInteractionEnabled = YES;
        self.multipleTouchEnabled = YES;
        self.textContainerInset = UIEdgeInsetsZero;
        self.textContainer.lineFragmentPadding = 0;
        auto *hover = [[UIHoverGestureRecognizer alloc] initWithTarget:self
                                                                action:@selector(handleHover:)];
        hover.cancelsTouchesInView = NO;
        [self addGestureRecognizer:hover];
    }
    return self;
}

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (void)handleHover:(UIHoverGestureRecognizer *)gesture {
    auto resource = surface(self.surface);
    if (!resource || resource->destroying)
        return;
    const CGPoint point = [gesture locationInView:self];
    switch (gesture.state) {
    case UIGestureRecognizerStateBegan: {
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_POINTER_ENTER;
        event.source = resource->handle;
        event.flags = 1u;
        nk::core::push_event(std::move(event));
        emit_pointer_move(*resource, point.x, point.y);
        break;
    }
    case UIGestureRecognizerStateChanged:
        emit_pointer_move(*resource, point.x, point.y);
        break;
    case UIGestureRecognizerStateEnded:
    case UIGestureRecognizerStateCancelled:
    case UIGestureRecognizerStateFailed: {
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_POINTER_ENTER;
        event.source = resource->handle;
        event.flags = 0;
        nk::core::push_event(std::move(event));
        break;
    }
    default:
        break;
    }
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    emit_touch_transition(self, touches, NK_TOUCH_BEGIN, event);
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    emit_touch_transition(self, touches, NK_TOUCH_MOVE, event);
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    emit_touch_transition(self, touches, NK_TOUCH_END, event);
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    emit_touch_transition(self, touches, NK_TOUCH_CANCEL, event);
}

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    auto resource = surface(self.surface);
    if (resource && !resource->destroying)
        for (UIPress *press in presses)
            if (press.key)
                emit_key_transition(*resource, press.key, NK_INPUT_PRESS);
    [super pressesBegan:presses withEvent:event];
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    auto resource = surface(self.surface);
    if (resource && !resource->destroying)
        for (UIPress *press in presses)
            if (press.key)
                emit_key_transition(*resource, press.key, NK_INPUT_RELEASE);
    [super pressesEnded:presses withEvent:event];
}

- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    auto resource = surface(self.surface);
    if (resource && !resource->destroying)
        for (UIPress *press in presses)
            if (press.key)
                emit_key_transition(*resource, press.key, NK_INPUT_RELEASE);
    [super pressesCancelled:presses withEvent:event];
}

- (UITextRange *)markedTextRange {
    auto resource = surface(self.surface);
    if (!resource || !resource->text_composing)
        return nil;
    return native_text_range(self,
                             native_range_for_positions(*resource, resource->text_composition_start,
                                                        resource->text_composition_end));
}

- (void)setMarkedText:(NSString *)markedText selectedRange:(NSRange)selectedRange {
    auto resource = surface(self.surface);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        const std::string text = utf8_string(markedText ? markedText : @"");
        std::vector<uint32_t> points;
        if (!decode_utf8(text, points))
            return;
        NSRange replacement =
            resource->text_composing
                ? native_range_for_positions(*resource, resource->text_composition_start,
                                             resource->text_composition_end)
                : self.selectedRange;
        nk_text_position start = 0;
        nk_text_position end = 0;
        if (!codepoint_range_for_native_range(*resource, replacement, start, end)) {
            start = text_replacement_start(*resource);
            end = text_replacement_end(*resource);
        }
        if (points.size() > std::numeric_limits<uint32_t>::max())
            return;
        const NSUInteger text_units =
            utf16_offset_for_codepoint(points, static_cast<uint32_t>(points.size()));
        const NSUInteger selected_start =
            selectedRange.location == NSNotFound
                ? text_units
                : std::min<NSUInteger>(selectedRange.location, text_units);
        const NSUInteger selected_end =
            selectedRange.location == NSNotFound
                ? selected_start
                : std::min<NSUInteger>(selectedRange.location + selectedRange.length, text_units);
        const auto selection_start = static_cast<nk_text_position>(
            start + codepoint_index_for_utf16(points, selected_start));
        const auto selection_end =
            static_cast<nk_text_position>(start + codepoint_index_for_utf16(points, selected_end));
        const auto composition_end = static_cast<nk_text_position>(start + points.size());
        if (resource->text_input_active) {
            apply_text_edit_state(*resource, NK_TEXT_EDIT_COMPOSE, start, end, text,
                                  selection_start, selection_end, start, composition_end);
        } else {
            update_text_snapshot(*resource, start, end, text);
            resource->text_composing = true;
            resource->text_composition_start = start;
            resource->text_composition_end = composition_end;
            resource->text_input_state.composition_start = start;
            resource->text_input_state.composition_end = composition_end;
            resource->text_input_state.selection_start = selection_start;
            resource->text_input_state.selection_end = selection_end;
            resource->marked_text = text;
            resource->marked_native_range = NSMakeRange(replacement.location, text_units);
            sync_input_view(*resource);
        }
    });
}

- (void)unmarkText {
    auto resource = surface(self.surface);
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

- (void)insertText:(NSString *)text {
    auto resource = surface(self.surface);
    if (!resource)
        return;
    nk::core::callback_boundary(
        [&] { emit_committed_text(*resource, utf8_string(text ? text : @"")); });
}

- (void)deleteBackward {
    auto resource = surface(self.surface);
    if (!resource || !resource->text_input_active)
        return;
    nk::core::callback_boundary([&] {
        nk_text_position replace_start = std::min(resource->text_input_state.selection_start,
                                                  resource->text_input_state.selection_end);
        nk_text_position replace_end = std::max(resource->text_input_state.selection_start,
                                                resource->text_input_state.selection_end);
        if (replace_start == replace_end && replace_start > resource->text_input_state.text_start)
            --replace_start;
        if (replace_start == replace_end)
            return;
        apply_text_edit_state(*resource, NK_TEXT_EDIT_DELETE, replace_start, replace_end, {},
                              replace_start, replace_start, NK_TEXT_POSITION_NONE,
                              NK_TEXT_POSITION_NONE);
    });
}

- (void)replaceRange:(UITextRange *)range withText:(NSString *)text {
    auto resource = surface(self.surface);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        nk_text_position start = 0;
        nk_text_position end = 0;
        if (!codepoint_range_for_native_range(*resource, native_range_for_text_range(self, range),
                                              start, end))
            return;
        const std::string value = utf8_string(text ? text : @"");
        std::vector<uint32_t> points;
        if (!decode_utf8(value, points))
            return;
        const auto selection = static_cast<nk_text_position>(start + points.size());
        if (resource->text_input_active)
            apply_text_edit_state(*resource, NK_TEXT_EDIT_COMMIT, start, end, value, selection,
                                  selection, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
        else
            emit_committed_text(*resource, value);
    });
}

- (void)setSelectedTextRange:(UITextRange *)range {
    [super setSelectedTextRange:range];
    auto resource = surface(self.surface);
    if (!resource || resource->syncing_input_view || !resource->text_input_active || !range)
        return;
    nk::core::callback_boundary([&] {
        nk_text_position start = 0;
        nk_text_position end = 0;
        if (!codepoint_range_for_native_range(*resource, native_range_for_text_range(self, range),
                                              start, end) ||
            (start == resource->text_input_state.selection_start &&
             end == resource->text_input_state.selection_end))
            return;
        apply_text_edit_state(*resource, NK_TEXT_EDIT_SET_SELECTION, NK_TEXT_POSITION_NONE,
                              NK_TEXT_POSITION_NONE, {}, start, end,
                              resource->text_input_state.composition_start,
                              resource->text_input_state.composition_end);
    });
}

- (CGRect)firstRectForRange:(UITextRange *)range {
    (void)range;
    auto resource = surface(self.surface);
    if (!resource)
        return CGRectZero;
    return CGRectMake(resource->text_input_state.cursor_x, resource->text_input_state.cursor_y,
                      std::max(1.f, resource->text_input_state.cursor_width),
                      std::max(1.f, resource->text_input_state.cursor_height));
}

- (CGRect)caretRectForPosition:(UITextPosition *)position {
    (void)position;
    auto resource = surface(self.surface);
    if (!resource)
        return CGRectZero;
    return CGRectMake(resource->text_input_state.cursor_x, resource->text_input_state.cursor_y,
                      std::max(1.f, resource->text_input_state.cursor_width),
                      std::max(1.f, resource->text_input_state.cursor_height));
}
@end

@implementation NKIOSWebViewDelegate
- (void)webView:(WKWebView *)view
    decidePolicyForNavigationAction:(WKNavigationAction *)action
                    decisionHandler:(void (^)(WKNavigationActionPolicy))decisionHandler {
    auto *resource = static_cast<IOSWebView *>(self.resource);
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
        event.data = text_bytes(utf8_string(action.request.URL.absoluteString));
        navigation_decisions.emplace(
            request, IOSNavigationDecision{resource->handle, [decisionHandler copy]});
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
    auto *resource = static_cast<IOSWebView *>(self.resource);
    if (resource)
        emit_webview_text(NK_EVENT_WEBVIEW_NAVIGATED, resource->handle, view.URL.absoluteString);
}

- (void)webView:(WKWebView *)view
    didFailNavigation:(WKNavigation *)navigation
            withError:(NSError *)error {
    (void)view;
    (void)navigation;
    auto *resource = static_cast<IOSWebView *>(self.resource);
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
    auto *resource = static_cast<IOSWebView *>(self.resource);
    if (resource)
        emit_webview_text(NK_EVENT_WEBVIEW_PROCESS_TERMINATED, resource->handle,
                          @"WebKit content process terminated", NK_ERROR_UNKNOWN);
    (void)view;
}

- (void)userContentController:(WKUserContentController *)controller
      didReceiveScriptMessage:(WKScriptMessage *)message {
    (void)controller;
    auto *resource = static_cast<IOSWebView *>(self.resource);
    if (!resource)
        return;
    NSString *value = json_text(message.body);
    emit_webview_text(NK_EVENT_WEBVIEW_MESSAGE, resource->handle,
                      value ? value : @"JavaScript message is not JSON-serializable",
                      value ? NK_OK : NK_ERROR_UNKNOWN);
}

- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id> *)change
                       context:(void *)context {
    (void)change;
    (void)context;
    auto *resource = static_cast<IOSWebView *>(self.resource);
    if (resource && [keyPath isEqualToString:@"title"])
        emit_webview_text(NK_EVENT_WEBVIEW_TITLE_CHANGED, resource->handle,
                          ((WKWebView *)object).title);
}
@end

IOSResourceValue resource_value_from_url(NSURL *url, uint32_t kind) {
    IOSResourceValue result;
    if (!url)
        return result;
    result.uri = utf8_string(url.absoluteString);
    result.display_name = utf8_string(url.lastPathComponent);
    result.flags = kind == NK_DIALOG_SAVE_RESOURCE ? NK_RESOURCE_WRITABLE : NK_RESOURCE_READABLE;
    if (kind == NK_DIALOG_SELECT_RESOURCE_DIRECTORY)
        result.flags |= NK_RESOURCE_WRITABLE;
    retain_security_scope(url);
    return result;
}

@implementation NKIOSDropDelegate
- (BOOL)dropInteraction:(UIDropInteraction *)interaction
       canHandleSession:(id<UIDropSession>)session {
    (void)interaction;
    return [session hasItemsConformingToTypeIdentifiers:@[
        @"public.file-url", @"public.url", @"public.plain-text"
    ]];
}

- (UIDropProposal *)dropInteraction:(UIDropInteraction *)interaction
                   sessionDidUpdate:(id<UIDropSession>)session {
    (void)interaction;
    (void)session;
    return [[UIDropProposal alloc] initWithDropOperation:UIDropOperationCopy];
}

- (void)dropInteraction:(UIDropInteraction *)interaction performDrop:(id<UIDropSession>)session {
    (void)interaction;
    const auto host_handle = self.host;
    const auto generation = nk::core::runtime_generation();
    auto resource = host(host_handle);
    if (!resource || !resource->view)
        return;
    const CGPoint location = [session locationInView:resource->view];
    dispatch_group_t group = dispatch_group_create();
    NSMutableArray<NSURL *> *urls = [NSMutableArray array];
    NSMutableArray<NSString *> *texts = [NSMutableArray array];
    for (UIDragItem *item in session.items) {
        NSItemProvider *provider = item.itemProvider;
        NSString *type = nil;
        if ([provider hasItemConformingToTypeIdentifier:@"public.file-url"])
            type = @"public.file-url";
        else if ([provider hasItemConformingToTypeIdentifier:@"public.url"])
            type = @"public.url";
        else if ([provider hasItemConformingToTypeIdentifier:@"public.plain-text"])
            type = @"public.plain-text";
        if (!type)
            continue;

        dispatch_group_enter(group);
        if ([type isEqualToString:@"public.file-url"] || [type isEqualToString:@"public.url"]) {
            [provider loadObjectOfClass:[NSURL class]
                      completionHandler:^(id<NSItemProviderReading> object, NSError *error) {
                        (void)error;
                        if ([object isKindOfClass:[NSURL class]]) {
                            @synchronized(urls) {
                                [urls addObject:(NSURL *)object];
                            }
                        }
                        dispatch_group_leave(group);
                      }];
        } else {
            [provider loadObjectOfClass:[NSString class]
                      completionHandler:^(id<NSItemProviderReading> object, NSError *error) {
                        (void)error;
                        if ([object isKindOfClass:[NSString class]]) {
                            @synchronized(texts) {
                                [texts addObject:(NSString *)object];
                            }
                        }
                        dispatch_group_leave(group);
                      }];
        }
    }
    dispatch_group_notify(group, dispatch_get_main_queue(), ^{
      nk::core::callback_boundary([&] {
          if (!nk::core::is_runtime_generation(generation))
              return;
          auto target = host(host_handle);
          if (!target || !target->view)
              return;
          std::vector<nk::platform::ResourceValue> resources;
          @synchronized(urls) {
              resources.reserve(urls.count);
              for (NSURL *url in urls) {
                  auto value = resource_value_from_url(url, NK_DIALOG_OPEN_RESOURCE);
                  if (!value.uri.empty())
                      resources.push_back(nk::platform::resource_from_uri(
                          std::move(value.uri), value.flags, std::move(value.mime_type),
                          std::move(value.display_name)));
              }
          }
          std::string text;
          @synchronized(texts) {
              for (NSString *value in texts) {
                  if (!text.empty())
                      text.push_back('\n');
                  text += utf8_string(value);
              }
          }
          if (resources.empty() && text.empty())
              return;
          nk::core::QueuedEvent event;
          event.kind = NK_EVENT_RESOURCE_DROP;
          event.source = host_handle;
          event.data_count = static_cast<uint32_t>(resources.size());
          event.data = nk::platform::resource_drop_payload(
              static_cast<float>(location.x), static_cast<float>(location.y), text, resources);
          nk::core::push_event(std::move(event));
      });
    });
}
@end

void finish_document_dialog(nk_request_id request, bool accepted, NSArray<NSURL *> *urls) noexcept {
    nk::core::callback_boundary([&] {
        std::shared_ptr<IOSDialogContext> context;
        {
            std::lock_guard lock(dialogs_mutex);
            const auto found = dialogs.find(request);
            if (found == dialogs.end())
                return;
            context = found->second;
            dialogs.erase(found);
        }
        if (context->picker)
            context->picker.delegate = nil;
        if (context->temporary_url)
            [NSFileManager.defaultManager removeItemAtURL:context->temporary_url error:nil];
        if (!nk::core::is_runtime_generation(context->generation))
            return;
        std::vector<IOSResourceValue> resources;
        if (accepted) {
            resources.reserve(urls.count);
            for (NSURL *url in urls) {
                auto resource = resource_value_from_url(url, context->kind);
                if (!resource.uri.empty())
                    resources.push_back(std::move(resource));
            }
        }
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_DIALOG_RESOURCES_COMPLETE;
        event.request_id = request;
        event.flags = context->kind;
        event.data = resource_payload(accepted, resources);
        nk::core::push_event(std::move(event));
    });
}

void finish_message_dialog(nk_request_id request, nk_message_result result) noexcept {
    nk::core::callback_boundary([&] {
        std::shared_ptr<IOSDialogContext> context;
        {
            std::lock_guard lock(dialogs_mutex);
            const auto found = dialogs.find(request);
            if (found == dialogs.end())
                return;
            context = found->second;
            dialogs.erase(found);
        }
        if (context->dialog)
            context->dialog = nil;
        if (!nk::core::is_runtime_generation(context->generation))
            return;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_DIALOG_MESSAGE_COMPLETE;
        event.request_id = request;
        event.flags = NK_DIALOG_MESSAGE;
        event.data = bytes_of(nk_dialog_message_result{result});
        nk::core::push_event(std::move(event));
    });
}

nk_result cancel_dialog_request(nk_request_id request) {
    std::shared_ptr<IOSDialogContext> context;
    {
        std::lock_guard lock(dialogs_mutex);
        const auto found = dialogs.find(request);
        if (!request || found == dialogs.end())
            return ios_fail(NK_ERROR_INVALID_REQUEST, "invalid or completed iOS dialog request");
        context = found->second;
        dialogs.erase(found);
    }
    if (context->picker) {
        context->picker.delegate = nil;
        [context->picker dismissViewControllerAnimated:YES completion:nil];
    } else if (context->dialog) {
        [context->dialog dismissViewControllerAnimated:YES completion:nil];
    }
    if (context->temporary_url)
        [NSFileManager.defaultManager removeItemAtURL:context->temporary_url error:nil];
    if (!nk::core::is_runtime_generation(context->generation))
        return NK_OK;
    nk::core::QueuedEvent event;
    event.request_id = request;
    event.flags = context->kind;
    if (context->kind == NK_DIALOG_MESSAGE) {
        event.kind = NK_EVENT_DIALOG_MESSAGE_COMPLETE;
        event.data = bytes_of(nk_dialog_message_result{NK_MESSAGE_RESULT_NONE});
    } else {
        event.kind = NK_EVENT_DIALOG_RESOURCES_COMPLETE;
        event.data = resource_payload(false, {});
    }
    const auto result = nk::core::push_event(std::move(event));
    return result == NK_OK ? NK_OK : ios_fail(result, "could not queue iOS dialog cancellation");
}

void cancel_dialogs_for_parent(nk_handle parent) {
    std::vector<nk_request_id> requests;
    {
        std::lock_guard lock(dialogs_mutex);
        for (const auto &[request, context] : dialogs)
            if (context->parent == parent)
                requests.push_back(request);
    }
    for (const auto request : requests)
        cancel_dialog_request(request);
}

std::shared_ptr<IOSHost> dialog_host(nk_handle parent) {
    if (parent)
        return host(parent);
    return hosts.empty() ? nullptr : hosts.begin()->second;
}

UIViewController *dialog_presenter(nk_handle parent, nk_handle &out_parent) {
    auto resource = dialog_host(parent);
    if (!resource)
        return nil;
    out_parent = resource->handle;
    return top_view_controller(view_controller_for_view(resource->view));
}

NSArray<UTType *> *document_content_types(const nk_file_dialog_options *options, uint32_t kind) {
    NSMutableArray<UTType *> *types = [NSMutableArray array];
    if (kind == NK_DIALOG_SELECT_RESOURCE_DIRECTORY) {
        UTType *folder = [UTType typeWithIdentifier:@"public.folder"];
        if (folder)
            [types addObject:folder];
        return types;
    }
    for (uint32_t index = 0; index < options->filter_count; ++index) {
        NSString *patterns = native_string(options->filters[index].patterns);
        for (NSString *pattern in [patterns componentsSeparatedByString:@";"]) {
            NSString *value = [pattern
                stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
            NSRange marker = [value rangeOfString:@"*."];
            if (marker.location == NSNotFound)
                continue;
            NSString *extension = [value substringFromIndex:marker.location + marker.length];
            if (!extension.length || [extension rangeOfString:@"*"].location != NSNotFound)
                continue;
            UTType *type = [UTType typeWithFilenameExtension:extension.lowercaseString];
            if (type && ![types containsObject:type])
                [types addObject:type];
        }
    }
    if (!types.count) {
        UTType *item = [UTType typeWithIdentifier:@"public.item"];
        if (item)
            [types addObject:item];
    }
    return types;
}

nk_result start_resource_dialog(nk_handle parent_handle, const nk_file_dialog_options *options,
                                nk_request_id *out_request, uint32_t kind) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    constexpr nk_dialog_flags supported_flags =
        NK_DIALOG_ALLOW_MULTIPLE | NK_DIALOG_CONFIRM_OVERWRITE | NK_DIALOG_SHOW_HIDDEN;
    if (!options || options->struct_size < sizeof(*options) || !out_request ||
        (options->flags & ~supported_flags) != 0 || (options->filter_count && !options->filters))
        return ios_fail(NK_ERROR_INVALID_ARGUMENT, "invalid iOS resource dialog options");
    if (!valid_utf8(options->title) || !valid_utf8(options->initial_path) ||
        !valid_utf8(options->suggested_name))
        return ios_fail(NK_ERROR_INVALID_ARGUMENT, "iOS resource dialog option is not valid UTF-8");
    for (uint32_t index = 0; index < options->filter_count; ++index)
        if (!valid_utf8(options->filters[index].name) ||
            !valid_utf8(options->filters[index].patterns))
            return ios_fail(NK_ERROR_INVALID_ARGUMENT,
                            "iOS resource dialog filter is not valid UTF-8");
    *out_request = NK_INVALID_REQUEST_ID;
    nk_handle dialog_parent = NK_INVALID_HANDLE;
    UIViewController *presenter = dialog_presenter(parent_handle, dialog_parent);
    if (parent_handle && !dialog_parent)
        return ios_fail(NK_ERROR_INVALID_HANDLE, "invalid iOS resource dialog parent");
    if (!presenter)
        return ios_fail(NK_ERROR_UNSUPPORTED,
                        "iOS resource dialogs require an attached host view controller");

    auto context = std::make_shared<IOSDialogContext>();
    context->request = nk::core::next_request_id();
    context->parent = dialog_parent;
    context->kind = kind;
    context->generation = nk::core::runtime_generation();
    UIDocumentPickerViewController *picker = nil;
    if (kind == NK_DIALOG_SAVE_RESOURCE) {
        NSString *name = native_string(options->suggested_name);
        name = name.lastPathComponent;
        if (!name.length)
            name = @"NativeKit Document";
        NSString *filename =
            [NSString stringWithFormat:@"nativekit-%@-%@",
                                       NSProcessInfo.processInfo.globallyUniqueString, name];
        NSURL *temporary_url = [NSURL
            fileURLWithPath:[NSTemporaryDirectory() stringByAppendingPathComponent:filename]];
        if (![[NSData data] writeToURL:temporary_url atomically:YES])
            return ios_fail(NK_ERROR_UNKNOWN, "could not create the iOS document export source");
        context->temporary_url = temporary_url;
        if (@available(iOS 14.0, *))
            picker = [[UIDocumentPickerViewController alloc] initForExportingURLs:@[ temporary_url ]
                                                                           asCopy:YES];
        else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            picker = [[UIDocumentPickerViewController alloc]
                initWithURL:temporary_url
                     inMode:UIDocumentPickerModeExportToService];
#pragma clang diagnostic pop
        }
    } else if (@available(iOS 14.0, *)) {
        picker = [[UIDocumentPickerViewController alloc]
            initForOpeningContentTypes:document_content_types(options, kind)
                                asCopy:NO];
    } else {
        NSArray<NSString *> *types = kind == NK_DIALOG_SELECT_RESOURCE_DIRECTORY
                                         ? @[ @"public.folder" ]
                                         : @[ @"public.item" ];
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        picker =
            [[UIDocumentPickerViewController alloc] initWithDocumentTypes:types
                                                                   inMode:UIDocumentPickerModeOpen];
#pragma clang diagnostic pop
    }
    if (!picker)
        return ios_fail(NK_ERROR_UNKNOWN, "could not create the iOS document picker");
    picker.title = native_string(options->title);
    if (!picker.title)
        picker.title = @"";
    picker.allowsMultipleSelection =
        kind == NK_DIALOG_OPEN_RESOURCE && (options->flags & NK_DIALOG_ALLOW_MULTIPLE) != 0;
    auto delegate = [NKIOSDocumentPickerDelegate new];
    if (!delegate)
        return ios_fail(NK_ERROR_OUT_OF_MEMORY,
                        "could not create the iOS document picker delegate");
    delegate.request = context->request;
    context->picker = picker;
    context->picker_delegate = delegate;
    context->presenter = presenter;
    picker.delegate = delegate;
    {
        std::lock_guard lock(dialogs_mutex);
        dialogs.emplace(context->request, context);
    }
    [presenter presentViewController:picker animated:YES completion:nil];
    *out_request = context->request;
    return NK_OK;
}

nk_result start_message_dialog(nk_handle parent_handle, const nk_message_dialog_options *options,
                               nk_request_id *out_request) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    constexpr nk_message_buttons supported_buttons = NK_MESSAGE_BUTTON_OK |
                                                     NK_MESSAGE_BUTTON_CANCEL |
                                                     NK_MESSAGE_BUTTON_YES | NK_MESSAGE_BUTTON_NO;
    if (!options || options->struct_size < sizeof(*options) || !options->message || !out_request ||
        options->kind > NK_MESSAGE_QUESTION || (options->buttons & ~supported_buttons) != 0 ||
        !valid_utf8(options->title) || !valid_utf8(options->message))
        return ios_fail(NK_ERROR_INVALID_ARGUMENT, "invalid iOS message dialog options");
    *out_request = NK_INVALID_REQUEST_ID;
    nk_handle dialog_parent = NK_INVALID_HANDLE;
    UIViewController *presenter = dialog_presenter(parent_handle, dialog_parent);
    if (parent_handle && !dialog_parent)
        return ios_fail(NK_ERROR_INVALID_HANDLE, "invalid iOS message dialog parent");
    if (!presenter)
        return ios_fail(NK_ERROR_UNSUPPORTED,
                        "iOS message dialogs require an attached host view controller");
    auto context = std::make_shared<IOSDialogContext>();
    context->request = nk::core::next_request_id();
    context->parent = dialog_parent;
    context->kind = NK_DIALOG_MESSAGE;
    context->generation = nk::core::runtime_generation();
    const auto request = context->request;
    UIAlertControllerStyle style = UIAlertControllerStyleAlert;
    NSString *title = native_string(options->title);
    if (!title)
        title = @"";
    UIAlertController *alert =
        [UIAlertController alertControllerWithTitle:title
                                            message:native_string(options->message)
                                     preferredStyle:style];
    if (!alert)
        return ios_fail(NK_ERROR_OUT_OF_MEMORY, "could not create the iOS message dialog");
    auto add_action = [&](uint32_t flag, NSString *title, nk_message_result result) {
        if (!(options->buttons & flag))
            return;
        UIAlertActionStyle action_style =
            flag == NK_MESSAGE_BUTTON_CANCEL ? UIAlertActionStyleCancel : UIAlertActionStyleDefault;
        [alert addAction:[UIAlertAction actionWithTitle:title
                                                  style:action_style
                                                handler:^(UIAlertAction *) {
                                                  finish_message_dialog(request, result);
                                                }]];
    };
    add_action(NK_MESSAGE_BUTTON_OK, @"OK", NK_MESSAGE_RESULT_OK);
    add_action(NK_MESSAGE_BUTTON_YES, @"Yes", NK_MESSAGE_RESULT_YES);
    add_action(NK_MESSAGE_BUTTON_NO, @"No", NK_MESSAGE_RESULT_NO);
    add_action(NK_MESSAGE_BUTTON_CANCEL, @"Cancel", NK_MESSAGE_RESULT_CANCEL);
    if (!alert.actions.count)
        add_action(NK_MESSAGE_BUTTON_OK, @"OK", NK_MESSAGE_RESULT_OK);
    context->dialog = alert;
    context->presenter = presenter;
    {
        std::lock_guard lock(dialogs_mutex);
        dialogs.emplace(context->request, context);
    }
    [presenter presentViewController:alert animated:YES completion:nil];
    *out_request = context->request;
    return NK_OK;
}

@implementation NKIOSDocumentPickerDelegate
- (void)documentPicker:(UIDocumentPickerViewController *)controller
    didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    (void)controller;
    finish_document_dialog(self.request, true, urls);
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    (void)controller;
    finish_document_dialog(self.request, false, @[]);
}
@end

@implementation NKIOSNotificationDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter *)center
    didReceiveNotificationResponse:(UNNotificationResponse *)response
             withCompletionHandler:(void (^)(void))completionHandler {
    (void)center;
    const auto request = notification_request(response.notification.request.identifier);
    IOSNotification notification;
    if (take_notification(request, &notification) &&
        nk::core::is_runtime_generation(notification.generation)) {
        const bool dismissed =
            [response.actionIdentifier isEqualToString:UNNotificationDismissActionIdentifier];
        NSString *action = nil;
        if (!dismissed &&
            ![response.actionIdentifier isEqualToString:UNNotificationDefaultActionIdentifier])
            action = response.actionIdentifier;
        emit_notification(dismissed ? NK_EVENT_NOTIFICATION_DISMISSED
                                    : NK_EVENT_NOTIFICATION_ACTIVATED,
                          request, NK_OK, action);
    }
    completionHandler();
}

- (void)userNotificationCenter:(UNUserNotificationCenter *)center
       willPresentNotification:(UNNotification *)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
    (void)center;
    const auto request = notification_request(notification.request.identifier);
    IOSNotification value;
    if (!get_notification(request, value) || !nk::core::is_runtime_generation(value.generation)) {
        completionHandler(UNNotificationPresentationOptionNone);
        return;
    }
    UNNotificationPresentationOptions options;
    if (@available(iOS 14.0, *)) {
        options = UNNotificationPresentationOptionList | UNNotificationPresentationOptionBanner;
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        options = UNNotificationPresentationOptionAlert;
#pragma clang diagnostic pop
    }
    if (!value.silent)
        options |= UNNotificationPresentationOptionSound;
    completionHandler(options);
}
@end

namespace nk::backend {

void pump_events() noexcept {
    nk::ios_joystick::pump();
}

void shutdown() noexcept {
    nk::core::sensor_backend::shutdown();
    (void)nk::core::haptics_backend::stop_vibration();
    nk::ios_joystick::shutdown();
    NSMutableArray<NSString *> *notification_identifiers = [NSMutableArray array];
    {
        std::lock_guard lock(notifications_mutex);
        for (const auto &[request, notification] : notifications) {
            (void)notification;
            [notification_identifiers addObject:notification_identifier(request)];
        }
        notifications.clear();
    }
    if (notification_center_initialized) {
        UNUserNotificationCenter *center = UNUserNotificationCenter.currentNotificationCenter;
        [center removePendingNotificationRequestsWithIdentifiers:notification_identifiers];
        [center removeDeliveredNotificationsWithIdentifiers:notification_identifiers];
        if (center.delegate == notification_delegate)
            center.delegate = nil;
        notification_center_initialized = false;
    }
    notification_delegate = nil;
    std::vector<std::shared_ptr<IOSDialogContext>> pending_dialogs;
    {
        std::lock_guard lock(dialogs_mutex);
        for (const auto &[request, context] : dialogs) {
            (void)request;
            pending_dialogs.push_back(context);
        }
        dialogs.clear();
    }
    for (const auto &context : pending_dialogs) {
        if (context->picker) {
            context->picker.delegate = nil;
            [context->picker dismissViewControllerAnimated:NO completion:nil];
        } else if (context->dialog) {
            [context->dialog dismissViewControllerAnimated:NO completion:nil];
        }
        if (context->temporary_url)
            [NSFileManager.defaultManager removeItemAtURL:context->temporary_url error:nil];
    }
    release_security_scopes();
    for (auto &[request, decision] : navigation_decisions) {
        (void)request;
        decision.handler(WKNavigationActionPolicyCancel);
    }
    navigation_decisions.clear();
    cancel_evaluations(NK_INVALID_HANDLE);
    evaluations.clear();
    for (auto &[handle, resource] : webviews) {
        resource->delegate.resource = nullptr;
        resource->view.navigationDelegate = nil;
        [resource->content_controller removeScriptMessageHandlerForName:@"nativekit"];
        [resource->view stopLoading];
        [resource->view removeFromSuperview];
        nk::core::handles().erase(handle, nk::core::ResourceType::webview);
    }
    webviews.clear();
    for (auto &[handle, resource] : surfaces) {
        resource->destroying = true;
        resource->frame_prepared = false;
        [resource->frame_timer invalidate];
        resource->frame_timer = nil;
        resource->frame_timer_target = nil;
        resource->drawable = nil;
        resource->depth_stencil = nil;
        if (resource->layer)
            [resource->layer removeFromSuperlayer];
        nk::core::handles().erase(handle, nk::core::ResourceType::surface);
    }
    surfaces.clear();
    for (auto &[handle, resource] : hosts) {
        stop_observing(resource);
        nk::core::handles().erase(handle, nk::core::ResourceType::mobile_host);
    }
    hosts.clear();
    last_device_orientation = NK_ORIENTATION_UNKNOWN;
}

nk_result mobile_host_attach(const nk_mobile_host_options &options, nk_handle &out_host) {
    if (options.kind != NK_MOBILE_HOST_UIKIT_VIEW || !options.native_view) {
        nk::core::set_error("iOS host requires a UIView");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto *view = (__bridge UIView *)(void *)options.native_view;
    if (![view isKindOfClass:[UIView class]]) {
        nk::core::set_error("native_view is not a UIView");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = std::make_shared<IOSHost>();
    resource->handle = NK_INVALID_HANDLE;
    resource->view = view;
    const auto handle = nk::core::handles().insert(nk::core::ResourceType::mobile_host, resource);
    if (!handle) {
        nk::core::set_error("could not allocate an iOS mobile host handle");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    resource->handle = handle;
    hosts.emplace(handle, resource);
    observe_view(resource, handle);
    out_host = handle;
    queue_geometry(resource);
    return NK_OK;
}

nk_result mobile_host_destroy(nk_handle handle) {
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    const auto found = hosts.find(handle);
    if (found == hosts.end()) {
        nk::core::set_error("invalid iOS mobile host handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    cancel_dialogs_for_parent(handle);
    const auto child_surfaces = found->second->surfaces;
    const auto child_webviews = found->second->webviews;
    for (auto iter = child_webviews.rbegin(); iter != child_webviews.rend(); ++iter)
        if (webview(*iter)) {
            const auto result = nk_webview_destroy(*iter);
            if (result != NK_OK)
                return result;
        }
    for (auto iter = child_surfaces.rbegin(); iter != child_surfaces.rend(); ++iter)
        if (surface(*iter)) {
            const auto result = nk_surface_destroy(*iter);
            if (result != NK_OK)
                return result;
        }
    stop_observing(found->second);
    if (found->second->drop_interaction)
        [found->second->view removeInteraction:found->second->drop_interaction];
    found->second->drop_interaction = nil;
    found->second->drop_delegate = nil;
    found->second->view = nil;
    hosts.erase(found);
    if (nk::core::system_keep_awake_held())
        (void)nk::core::system_backend::keep_awake_apply(has_active_host());
    nk::core::handles().erase(handle, nk::core::ResourceType::mobile_host);
    return NK_OK;
}

nk_result mobile_host_set_lifecycle(nk_handle handle, nk_mobile_lifecycle_state state) {
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = host(handle);
    if (!resource) {
        nk::core::set_error("invalid iOS mobile host handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (state < NK_MOBILE_LIFECYCLE_ACTIVE || state > NK_MOBILE_LIFECYCLE_BACKGROUND) {
        nk::core::set_error("invalid mobile lifecycle state");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const bool becoming_unavailable =
        resource->lifecycle == NK_MOBILE_LIFECYCLE_ACTIVE && state != NK_MOBILE_LIFECYCLE_ACTIVE;
    resource->lifecycle = state;
    if (state == NK_MOBILE_LIFECYCLE_BACKGROUND)
        (void)nk::core::haptics_backend::stop_vibration();
    set_orientation_observing(resource, state != NK_MOBILE_LIFECYCLE_BACKGROUND);
    if (nk::core::system_keep_awake_held())
        (void)nk::core::system_backend::keep_awake_apply(has_active_host());
    if (becoming_unavailable)
        for (const nk_handle child_handle : resource->surfaces)
            if (auto child = surface(child_handle)) {
                reset_surface_input(*child);
                emit_surface_lost(*child);
            }
    return NK_OK;
}

nk_result mobile_host_dispatch_event(nk_handle handle, const nk_mobile_host_event &) {
    if (!host(handle)) {
        nk::core::set_error("invalid iOS mobile host handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    nk::core::set_error("iOS host event forwarding uses native UIKit adapters");
    return NK_ERROR_UNSUPPORTED;
}

nk_result mobile_host_set_drop_enabled(nk_handle handle, bool enabled) {
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = host(handle);
    if (!resource) {
        nk::core::set_error("invalid iOS mobile host handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (resource->drops_enabled == enabled)
        return NK_OK;
    if (!enabled) {
        if (resource->drop_interaction)
            [resource->view removeInteraction:resource->drop_interaction];
        resource->drop_interaction = nil;
        resource->drop_delegate = nil;
        resource->drops_enabled = false;
        return NK_OK;
    }
    auto delegate = [NKIOSDropDelegate new];
    UIDropInteraction *interaction = [[UIDropInteraction alloc] initWithDelegate:delegate];
    if (!delegate || !interaction) {
        nk::core::set_error("could not create iOS drop interaction");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    delegate.host = handle;
    resource->drop_delegate = delegate;
    resource->drop_interaction = interaction;
    resource->drops_enabled = true;
    [resource->view addInteraction:interaction];
    return NK_OK;
}

} // namespace nk::backend

namespace {

NSString *application_storage_path() {
    NSArray<NSURL *> *urls =
        [NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory
                                             inDomains:NSUserDomainMask];
    if (!urls.count)
        return nil;
    const auto id = nk::core::system_application_id();
    NSString *component = id.empty() ? NSBundle.mainBundle.bundleIdentifier
                                     : [NSString stringWithUTF8String:id.c_str()];
    return component ? [urls[0].path stringByAppendingPathComponent:component] : nil;
}

nk_orientation ios_orientation(UIDeviceOrientation orientation) {
    switch (orientation) {
    case UIDeviceOrientationPortrait:
        return NK_ORIENTATION_PORTRAIT;
    case UIDeviceOrientationPortraitUpsideDown:
        return NK_ORIENTATION_PORTRAIT_UPSIDE_DOWN;
    case UIDeviceOrientationLandscapeLeft:
        return NK_ORIENTATION_LANDSCAPE_LEFT;
    case UIDeviceOrientationLandscapeRight:
        return NK_ORIENTATION_LANDSCAPE_RIGHT;
    case UIDeviceOrientationFaceUp:
        return NK_ORIENTATION_FACE_UP;
    case UIDeviceOrientationFaceDown:
        return NK_ORIENTATION_FACE_DOWN;
    default:
        return NK_ORIENTATION_UNKNOWN;
    }
}

nk_orientation interface_orientation(UIInterfaceOrientation orientation) {
    switch (orientation) {
    case UIInterfaceOrientationPortrait:
        return NK_ORIENTATION_PORTRAIT;
    case UIInterfaceOrientationPortraitUpsideDown:
        return NK_ORIENTATION_PORTRAIT_UPSIDE_DOWN;
    case UIInterfaceOrientationLandscapeLeft:
        return NK_ORIENTATION_LANDSCAPE_LEFT;
    case UIInterfaceOrientationLandscapeRight:
        return NK_ORIENTATION_LANDSCAPE_RIGHT;
    default:
        return NK_ORIENTATION_UNKNOWN;
    }
}

} // namespace

namespace nk::core::system_backend {

nk_result keep_awake_apply(bool enabled) noexcept {
    if (enabled && !has_active_host())
        return NK_ERROR_UNSUPPORTED;
    UIApplication.sharedApplication.idleTimerDisabled = enabled;
    return NK_OK;
}

nk_result get_orientation(nk_system_orientation &out_orientation) noexcept {
    const auto size = out_orientation.struct_size;
    out_orientation = {};
    out_orientation.struct_size = size;
    out_orientation.device = ios_orientation(UIDevice.currentDevice.orientation);
    out_orientation.display = interface_orientation(current_display_orientation());
    return NK_OK;
}

nk_result get_string(nk_system_string_kind kind, std::string &out_value) {
    NSString *value = nil;
    switch (kind) {
    case NK_SYSTEM_STRING_PLATFORM_VERSION:
        value = NSProcessInfo.processInfo.operatingSystemVersionString;
        break;
    case NK_SYSTEM_STRING_DEVICE_VENDOR:
        out_value = "Apple";
        return NK_OK;
    case NK_SYSTEM_STRING_DEVICE_MODEL:
        value = UIDevice.currentDevice.model;
        break;
    default:
        return NK_ERROR_UNSUPPORTED;
    }
    const auto *utf8 = value.UTF8String;
    if (!utf8)
        return NK_ERROR_UNSUPPORTED;
    out_value = utf8;
    return NK_OK;
}

} // namespace nk::core::system_backend

extern "C" {

nk_result NK_CALL nk_system_directory(nk_system_directory_kind kind, char *buffer,
                                      uint32_t *inout_size) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    NSString *path = nil;
    switch (kind) {
    case NK_DIRECTORY_HOME:
        path = NSHomeDirectory();
        break;
    case NK_DIRECTORY_TEMP:
        path = NSTemporaryDirectory();
        break;
    case NK_DIRECTORY_CONFIG:
    case NK_DIRECTORY_DATA: {
        NSArray<NSURL *> *urls = [NSFileManager.defaultManager
            URLsForDirectory:kind == NK_DIRECTORY_CONFIG ? NSLibraryDirectory
                                                         : NSApplicationSupportDirectory
                   inDomains:NSUserDomainMask];
        path = urls.count ? urls[0].path : nil;
        break;
    }
    case NK_DIRECTORY_APPLICATION:
        path = NSBundle.mainBundle.bundlePath;
        break;
    case NK_DIRECTORY_APPLICATION_STORAGE:
        path = application_storage_path();
        break;
    default:
        return NK_ERROR_UNSUPPORTED;
    }
    return copy_output(path, buffer, inout_size);
}

nk_result NK_CALL nk_system_locale(char *buffer, uint32_t *inout_size) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    return copy_output(NSLocale.currentLocale.localeIdentifier, buffer, inout_size);
}

nk_result NK_CALL nk_system_get_appearance(nk_system_appearance *appearance) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!appearance || appearance->struct_size < sizeof(*appearance))
        return NK_ERROR_INVALID_ARGUMENT;
    const auto size = appearance->struct_size;
    *appearance = {};
    appearance->struct_size = size;
    appearance->color_scheme =
        UITraitCollection.currentTraitCollection.userInterfaceStyle == UIUserInterfaceStyleDark
            ? NK_COLOR_SCHEME_DARK
            : NK_COLOR_SCHEME_LIGHT;
    appearance->high_contrast = UIAccessibilityDarkerSystemColorsEnabled() ? 1u : 0u;
    return NK_OK;
}

} // extern "C"

extern "C" {

nk_result NK_CALL nk_surface_create(nk_handle parent_handle, const nk_surface_options *options,
                                    nk_handle *out_surface) {
    return nk::core::result_boundary(
        "unexpected error while creating an iOS Metal surface", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            constexpr nk_surface_flags supported_flags = NK_SURFACE_HIDDEN | NK_SURFACE_ALPHA |
                                                         NK_SURFACE_DEPTH | NK_SURFACE_STENCIL |
                                                         NK_SURFACE_DEBUG_CONTEXT;
            if (!options || options->struct_size < sizeof(*options) || !out_surface ||
                options->width <= 0 || options->height <= 0 || options->api != NK_GRAPHICS_METAL ||
                (options->flags & ~supported_flags) != 0) {
                nk::core::set_error("invalid iOS Metal surface options");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            *out_surface = NK_INVALID_HANDLE;
            auto parent = host(parent_handle);
            if (!parent)
                return (nk::core::set_error("invalid or stale iOS mobile host handle"),
                        NK_ERROR_INVALID_HANDLE);
            auto shared = options->share_surface ? surface(options->share_surface) : nullptr;
            if (options->share_surface && !shared) {
                nk::core::set_error("invalid shared graphics surface");
                return NK_ERROR_INVALID_HANDLE;
            }
            if (shared && shared->device_handle == NK_INVALID_HANDLE) {
                nk::core::set_error("shared graphics surface has no Metal device");
                return NK_ERROR_INVALID_REQUEST;
            }
            if (shared && shared->share_dependents == UINT32_MAX) {
                nk::core::set_error("graphics surface has too many dependents");
                return NK_ERROR_INVALID_REQUEST;
            }
            parent->surfaces.reserve(parent->surfaces.size() + 1);
            auto resource = std::make_shared<IOSSurface>();
            resource->parent = parent_handle;
            resource->host_view = parent->view;
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
            if (!resource->device || !resource->queue) {
                nk::core::set_error("could not create an iOS Metal device and command queue");
                return NK_ERROR_UNSUPPORTED;
            }
            resource->layer = [CAMetalLayer layer];
            resource->layer.device = resource->device;
            resource->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            resource->layer.framebufferOnly = YES;
            resource->layer.opaque = (options->flags & NK_SURFACE_ALPHA) == 0;
            resource->layer.hidden = (options->flags & NK_SURFACE_HIDDEN) != 0;
            resource->input_view = [[NKIOSInputView alloc] initWithFrame:CGRectZero];
            if (!resource->input_view) {
                nk::core::set_error("could not create the iOS input surface");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            resource->accessibility_container =
                [[NKIOSAccessibilityContainer alloc] initWithFrame:CGRectZero];
            if (!resource->accessibility_container) {
                nk::core::set_error("could not create the iOS accessibility container");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            resource->input_view.hidden = (options->flags & NK_SURFACE_HIDDEN) != 0;
            resource->accessibility_container.hidden = resource->input_view.hidden;
            [parent->view.layer addSublayer:resource->layer];
            [parent->view addSubview:resource->input_view];
            [parent->view addSubview:resource->accessibility_container];
            for (const nk_handle webview_handle : parent->webviews)
                if (auto child = webview(webview_handle))
                    [parent->view bringSubviewToFront:child->view];
            if (!set_surface_native_bounds(*resource)) {
                nk::core::set_error("could not attach the iOS Metal layer");
                return NK_ERROR_UNKNOWN;
            }
            if (resource->framebuffer_width > 0 && resource->framebuffer_height > 0 &&
                !ensure_surface_depth_target(*resource, resource->framebuffer_width,
                                             resource->framebuffer_height))
                return NK_ERROR_UNSUPPORTED;
            resource->handle =
                nk::core::handles().insert(nk::core::ResourceType::surface, resource);
            if (!resource->handle) {
                nk::core::set_error("iOS graphics surface handle registry is full");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            if (!resource->device_handle)
                resource->device_handle = resource->handle;
            resource->input_view.surface = resource->handle;
            resource->accessibility_container.surface = resource->handle;
            surfaces.emplace(resource->handle, resource);
            parent->surfaces.push_back(resource->handle);
            if (shared)
                ++shared->share_dependents;
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
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (resource->share_dependents) {
        nk::core::set_error("graphics surface is still shared by another surface");
        return NK_ERROR_INVALID_REQUEST;
    }
    if (nk_core_graphics_device_has_references(nk_graphics_device{resource->device_handle})) {
        nk::core::set_error("graphics surface still owns retained GPU resources");
        return NK_ERROR_INVALID_REQUEST;
    }
    reset_surface_input(*resource);
    [resource->input_view resignFirstResponder];
    resource->destroying = true;
    resource->frame_prepared = false;
    [resource->frame_timer invalidate];
    resource->frame_timer = nil;
    resource->frame_timer_target = nil;
    resource->drawable = nil;
    resource->depth_stencil = nil;
    if (auto parent = host(resource->parent)) {
        auto &children = parent->surfaces;
        children.erase(std::remove(children.begin(), children.end(), handle), children.end());
    }
    if (resource->shared_surface)
        --resource->shared_surface->share_dependents;
    if (resource->layer)
        [resource->layer removeFromSuperlayer];
    surfaces.erase(handle);
    nk::core::handles().erase(handle, nk::core::ResourceType::surface);
    return NK_OK;
}

nk_result NK_CALL nk_surface_show(nk_handle handle, uint32_t visible) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (visible > 1) {
        nk::core::set_error("surface visibility must be zero or one");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    resource->layer.hidden = visible == 0;
    resource->input_view.hidden = visible == 0;
    resource->accessibility_container.hidden = visible == 0;
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                        int32_t height) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (width <= 0 || height <= 0) {
        nk::core::set_error("graphics surface dimensions must be positive");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    resource->x = x;
    resource->y = y;
    resource->width = width;
    resource->height = height;
    return set_surface_native_bounds(*resource)
               ? NK_OK
               : (nk::core::set_error("could not resize the iOS Metal layer"), NK_ERROR_UNKNOWN);
}

nk_result NK_CALL nk_surface_make_current(nk_handle handle) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (resource->frame_prepared && resource->drawable)
        return NK_OK;
    sync_surface_drawable_size(*resource);
    if (!surface_frame_available(*resource)) {
        nk::core::set_error("iOS Metal surface has no drawable frame");
        return NK_ERROR_INVALID_REQUEST;
    }
    resource->drawable = [resource->layer nextDrawable];
    if (!resource->drawable) {
        nk::core::set_error("iOS Metal drawable is temporarily unavailable");
        return NK_ERROR_INVALID_REQUEST;
    }
    const int32_t width = static_cast<int32_t>(resource->drawable.texture.width);
    const int32_t height = static_cast<int32_t>(resource->drawable.texture.height);
    if (!ensure_surface_depth_target(*resource, width, height))
        return NK_ERROR_UNKNOWN;
    resource->framebuffer_width = width;
    resource->framebuffer_height = height;
    resource->frame_prepared = true;
    if (resource->lost_reported) {
        resource->lost_reported = false;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_SURFACE_READY;
        event.source = resource->handle;
        nk::core::push_event(std::move(event));
    }
    return NK_OK;
}

nk_result NK_CALL nk_surface_present(nk_handle handle) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (!resource->frame_prepared) {
        nk::core::set_error("iOS Metal surface has no prepared frame");
        return NK_ERROR_INVALID_REQUEST;
    }
    resource->drawable = nil;
    resource->frame_prepared = false;
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_frame_callback(nk_handle handle,
                                                nk_surface_frame_callback callback,
                                                void *user_data) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    [resource->frame_timer invalidate];
    resource->frame_timer = nil;
    resource->frame_timer_target = nil;
    resource->frame_callback = callback;
    resource->frame_user_data = callback ? user_data : nullptr;
    if (!callback)
        return NK_OK;
    auto target = [NKIOSSurfaceTimer new];
    target.surface = handle;
    auto timer = [CADisplayLink displayLinkWithTarget:target selector:@selector(tick:)];
    [timer addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    resource->frame_timer_target = target;
    resource->frame_timer = timer;
    if (!resource->frame_requests.continuous() && !resource->frame_requests.pending())
        disarm_surface_frames(resource);
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_frame_mode(nk_handle handle, nk_surface_frame_mode mode) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (mode != NK_SURFACE_FRAME_CONTINUOUS && mode != NK_SURFACE_FRAME_ON_DEMAND) {
        nk::core::set_error("unknown graphics surface frame mode");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    resource->frame_requests.set_continuous(mode == NK_SURFACE_FRAME_CONTINUOUS);
    if (resource->frame_requests.continuous() || resource->frame_requests.pending())
        arm_surface_frames(resource);
    else
        disarm_surface_frames(resource);
    return NK_OK;
}

nk_result NK_CALL nk_surface_request_frame(nk_handle handle) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    resource->frame_requests.request();
    arm_surface_frames(resource);
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_framebuffer_size(nk_handle handle, int32_t *out_width,
                                                  int32_t *out_height) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!out_width || !out_height) {
        nk::core::set_error("framebuffer size outputs must not be null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
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
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!nk::core::surface_frame_target_output_valid(out_target)) {
        nk::core::set_error("frame-target output is missing or too small");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
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
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!name || !*name || !out_proc) {
        nk::core::set_error("invalid graphics procedure query");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    *out_proc = nullptr;
    if (!surface(handle)) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    nk::core::set_error("Metal surfaces do not expose GL procedure addresses");
    return NK_ERROR_UNSUPPORTED;
}

nk_result NK_CALL nk_webview_create(nk_handle parent_handle, const nk_webview_options *options,
                                    nk_webview *out_webview) {
    return nk::core::result_boundary(
        "unexpected error while creating an iOS WebView", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            constexpr nk_webview_flags supported_flags =
                NK_WEBVIEW_DEVTOOLS | NK_WEBVIEW_HIDDEN | NK_WEBVIEW_NAVIGATION_POLICY;
            if (!options || options->struct_size < sizeof(*options) || !out_webview ||
                options->width <= 0 || options->height <= 0 ||
                (options->flags & ~supported_flags) != 0) {
                nk::core::set_error("invalid iOS WebView options");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            if (!valid_utf8(options->initial_url)) {
                nk::core::set_error("iOS WebView URL is not valid UTF-8");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            *out_webview = NK_INVALID_HANDLE;
            auto parent = host(parent_handle);
            if (!parent) {
                nk::core::set_error("iOS WebView parent is not a mobile host");
                return NK_ERROR_INVALID_HANDLE;
            }
            NSURL *initial_url = nil;
            if (options->initial_url) {
                NSString *value = native_string(options->initial_url);
                initial_url = value ? [NSURL URLWithString:value] : nil;
                if (!initial_url) {
                    nk::core::set_error("iOS WebView URL is malformed");
                    return NK_ERROR_INVALID_ARGUMENT;
                }
            }
            parent->webviews.reserve(parent->webviews.size() + 1);
            auto resource = std::make_shared<IOSWebView>();
            resource->parent = parent_handle;
            resource->navigation_policy = (options->flags & NK_WEBVIEW_NAVIGATION_POLICY) != 0;
            resource->generation = nk::core::runtime_generation();
            resource->content_controller = [WKUserContentController new];
            resource->delegate = [NKIOSWebViewDelegate new];
            if (!resource->content_controller || !resource->delegate) {
                nk::core::set_error("could not create iOS WebView support objects");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            [resource->content_controller addScriptMessageHandler:resource->delegate
                                                             name:@"nativekit"];
            WKWebViewConfiguration *configuration = [WKWebViewConfiguration new];
            configuration.userContentController = resource->content_controller;
            resource->view = [[WKWebView alloc]
                initWithFrame:CGRectMake(options->x, options->y, options->width, options->height)
                configuration:configuration];
            if (!resource->view) {
                nk::core::set_error("could not create iOS WKWebView");
                return NK_ERROR_UNKNOWN;
            }
            resource->view.navigationDelegate = resource->delegate;
            resource->view.hidden = (options->flags & NK_WEBVIEW_HIDDEN) != 0;
            if (@available(iOS 16.4, *))
                resource->view.inspectable = (options->flags & NK_WEBVIEW_DEVTOOLS) != 0;
            resource->handle =
                nk::core::handles().insert(nk::core::ResourceType::webview, resource);
            if (!resource->handle) {
                nk::core::set_error("iOS WebView handle registry is full");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            resource->delegate.resource = resource.get();
            [resource->view addObserver:resource->delegate
                             forKeyPath:@"title"
                                options:NSKeyValueObservingOptionNew
                                context:nullptr];
            resource->observing_title = true;
            [parent->view addSubview:resource->view];
            webviews.emplace(resource->handle, resource);
            parent->webviews.push_back(resource->handle);
            *out_webview = resource->handle;
            emit_webview_text(NK_EVENT_WEBVIEW_READY, resource->handle, nil);
            if (initial_url)
                [resource->view loadRequest:[NSURLRequest requestWithURL:initial_url]];
            return NK_OK;
        });
}

nk_result NK_CALL nk_webview_destroy(nk_webview handle) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = webview(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS WebView handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    cancel_navigation_decisions(handle);
    cancel_evaluations(handle);
    if (auto parent = host(resource->parent)) {
        auto &children = parent->webviews;
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
    webviews.erase(handle);
    nk::core::handles().erase(handle, nk::core::ResourceType::webview);
    return NK_OK;
}

nk_result NK_CALL nk_webview_show(nk_webview handle, uint32_t visible) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (visible > 1) {
        nk::core::set_error("WebView visibility must be zero or one");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = webview(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS WebView handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    resource->view.hidden = visible == 0;
    return NK_OK;
}

nk_result NK_CALL nk_webview_set_bounds(nk_webview handle, int32_t x, int32_t y, int32_t width,
                                        int32_t height) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (width <= 0 || height <= 0) {
        nk::core::set_error("WebView dimensions must be positive");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = webview(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS WebView handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    resource->view.frame = CGRectMake(x, y, width, height);
    return NK_OK;
}

nk_result NK_CALL nk_webview_navigate(nk_webview handle, const char *url) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = webview(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS WebView handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    NSString *value = native_string(url);
    NSURL *target = value ? [NSURL URLWithString:value] : nil;
    if (!target) {
        nk::core::set_error("URL is null, invalid UTF-8, or malformed");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    [resource->view loadRequest:[NSURLRequest requestWithURL:target]];
    return NK_OK;
}

nk_result NK_CALL nk_webview_set_html(nk_webview handle, const char *html, const char *base_url) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = webview(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS WebView handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    NSString *document = native_string(html);
    NSString *base = native_string(base_url);
    if (!document || (base_url && !base)) {
        nk::core::set_error("HTML or base URL is invalid UTF-8");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    NSURL *base_target = base.length ? [NSURL URLWithString:base] : nil;
    if (base.length && !base_target) {
        nk::core::set_error("base URL is malformed");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    [resource->view loadHTMLString:document baseURL:base_target];
    return NK_OK;
}

nk_result NK_CALL nk_webview_can_go_back(nk_webview handle, uint32_t *out_can_go_back) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = webview(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS WebView handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (!out_can_go_back) {
        nk::core::set_error("history output is null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    *out_can_go_back = resource->view.canGoBack ? 1u : 0u;
    return NK_OK;
}

nk_result NK_CALL nk_webview_can_go_forward(nk_webview handle, uint32_t *out_can_go_forward) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = webview(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS WebView handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (!out_can_go_forward) {
        nk::core::set_error("history output is null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    *out_can_go_forward = resource->view.canGoForward ? 1u : 0u;
    return NK_OK;
}

nk_result NK_CALL nk_webview_go_back(nk_webview handle) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = webview(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS WebView handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    [resource->view goBack];
    return NK_OK;
}

nk_result NK_CALL nk_webview_go_forward(nk_webview handle) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = webview(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS WebView handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    [resource->view goForward];
    return NK_OK;
}

nk_result NK_CALL nk_webview_reload(nk_webview handle) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = webview(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS WebView handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    [resource->view reload];
    return NK_OK;
}

nk_result NK_CALL nk_webview_stop(nk_webview handle) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = webview(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS WebView handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    [resource->view stopLoading];
    return NK_OK;
}

nk_result NK_CALL nk_webview_eval(nk_webview handle, const char *script,
                                  nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while evaluating iOS JavaScript", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (!out_request) {
                nk::core::set_error("evaluation request output is null");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            *out_request = NK_INVALID_REQUEST_ID;
            auto resource = webview(handle);
            if (!resource) {
                nk::core::set_error("invalid or stale iOS WebView handle");
                return NK_ERROR_INVALID_HANDLE;
            }
            NSString *source = native_string(script);
            if (!source) {
                nk::core::set_error("script is null or invalid UTF-8");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            NSString *wrapped = javascript_json_wrapper(source);
            if (!wrapped) {
                nk::core::set_error("could not encode JavaScript source");
                return NK_ERROR_OUT_OF_MEMORY;
            }
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
                                         [value isKindOfClass:[NSString class]] ? value
                                                                                : json_text(value),
                                         NK_OK, 0, request);
                 }];
            *out_request = request;
            return NK_OK;
        });
}

nk_result NK_CALL nk_webview_navigation_decide(nk_request_id request, uint32_t allow) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (allow > 1) {
        nk::core::set_error("navigation decision must be zero or one");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const auto item = navigation_decisions.find(request);
    if (item == navigation_decisions.end()) {
        nk::core::set_error("invalid or completed navigation request");
        return NK_ERROR_INVALID_REQUEST;
    }
    auto handler = item->second.handler;
    navigation_decisions.erase(item);
    handler(allow ? WKNavigationActionPolicyAllow : WKNavigationActionPolicyCancel);
    return NK_OK;
}

nk_result NK_CALL nk_key_get_state(nk_handle handle, nk_key key, nk_input_action *out_action) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!out_action || key == NK_KEY_UNKNOWN || key > NK_KEY_LAST) {
        nk::core::set_error("invalid key state query");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    *out_action = resource->keys[key];
    return NK_OK;
}

nk_result NK_CALL nk_pointer_button_get_state(nk_handle handle, nk_pointer_button button,
                                              nk_input_action *out_action) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!out_action || button > NK_POINTER_BUTTON_LAST) {
        nk::core::set_error("invalid pointer button state query");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    *out_action = resource->pointer_buttons[button];
    return NK_OK;
}

nk_result NK_CALL nk_pointer_get_position(nk_handle handle, double *out_x, double *out_y) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!out_x || !out_y) {
        nk::core::set_error("pointer position outputs must not be null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    *out_x = resource->pointer_x;
    *out_y = resource->pointer_y;
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_text_input_state(nk_handle handle,
                                                  const nk_text_input_state *state) {
    return nk::core::result_boundary(
        "unexpected error while setting iOS text input state", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (!state || state->struct_size < sizeof(*state)) {
                nk::core::set_error("iOS text input state is missing or too small");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            const char *text = state->text ? state->text : "";
            NSString *native_text = native_string(text);
            std::vector<uint32_t> points;
            if (!native_text || !decode_utf8(text, points)) {
                nk::core::set_error("iOS text input state text is not valid UTF-8");
                return NK_ERROR_INVALID_ARGUMENT;
            }
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
                state->action > NK_TEXT_INPUT_ACTION_NONE || !valid_cursor) {
                nk::core::set_error("iOS text input ranges or hints are invalid");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            auto resource = surface(handle);
            if (!resource) {
                nk::core::set_error("invalid or stale iOS graphics surface handle");
                return NK_ERROR_INVALID_HANDLE;
            }
            resource->text_input_text = text;
            resource->text_input_state = *state;
            resource->text_input_state.text = resource->text_input_text.c_str();
            resource->text_composition_start = state->composition_start;
            resource->text_composition_end = state->composition_end;
            resource->text_composing = !no_composition;
            resource->marked_native_range =
                resource->text_composing
                    ? native_range_for_positions(*resource, state->composition_start,
                                                 state->composition_end)
                    : NSMakeRange(NSNotFound, 0);
            if (resource->text_composing)
                resource->marked_text =
                    utf8_string([native_text substringWithRange:resource->marked_native_range]);
            else
                resource->marked_text.clear();
            set_input_traits(*resource);
            sync_input_view(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_set_text_input_active(nk_handle handle, uint32_t active) {
    return nk::core::result_boundary(
        "unexpected error while changing iOS text input", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (active > 1) {
                nk::core::set_error("text input active state must be zero or one");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            auto resource = surface(handle);
            if (!resource) {
                nk::core::set_error("invalid or stale iOS graphics surface handle");
                return NK_ERROR_INVALID_HANDLE;
            }
            if (active) {
                resource->text_input_active = true;
                set_input_traits(*resource);
                if (![resource->input_view becomeFirstResponder]) {
                    resource->text_input_active = false;
                    nk::core::set_error("iOS could not activate the text input responder");
                    return NK_ERROR_UNKNOWN;
                }
            } else {
                if (resource->text_input_active)
                    finish_text_composition(*resource);
                resource->text_input_active = false;
                [resource->input_view resignFirstResponder];
                if (resource->text_composing) {
                    resource->text_composing = false;
                    resource->text_composition_start = NK_TEXT_POSITION_NONE;
                    resource->text_composition_end = NK_TEXT_POSITION_NONE;
                    resource->text_input_state.composition_start = NK_TEXT_POSITION_NONE;
                    resource->text_input_state.composition_end = NK_TEXT_POSITION_NONE;
                    resource->marked_text.clear();
                    resource->marked_native_range = NSMakeRange(NSNotFound, 0);
                }
            }
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_set_node(nk_handle handle,
                                                    const nk_accessibility_node *node) {
    return nk::core::result_boundary(
        "unexpected error while setting an iOS accessibility node", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            auto resource = surface(handle);
            if (!resource)
                return ios_fail(NK_ERROR_INVALID_HANDLE, "invalid or stale iOS surface handle");
            if (!node)
                return ios_fail(NK_ERROR_INVALID_ARGUMENT, "iOS accessibility node is missing");
            IOSAccessibilityNode copy;
            if (!copy_accessibility_node(*node, resource->accessibility_nodes, copy))
                return ios_fail(NK_ERROR_INVALID_ARGUMENT, "invalid iOS accessibility node");
            if (const auto old = resource->accessibility_nodes.find(node->id);
                old != resource->accessibility_nodes.end())
                copy.text_ranges = old->second.text_ranges;
            resource->accessibility_nodes[node->id] = std::move(copy);
            refresh_accessibility_elements(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_remove_node(nk_handle handle,
                                                       nk_accessibility_node_id node) {
    return nk::core::result_boundary(
        "unexpected error while removing an iOS accessibility node", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            auto resource = surface(handle);
            if (!resource)
                return ios_fail(NK_ERROR_INVALID_HANDLE, "invalid or stale iOS surface handle");
            if (!node ||
                resource->accessibility_nodes.find(node) == resource->accessibility_nodes.end())
                return ios_fail(NK_ERROR_INVALID_ARGUMENT,
                                "invalid or unknown iOS accessibility node");
            remove_accessibility_descendants(resource->accessibility_nodes, node);
            if (resource->accessibility_nodes.find(resource->accessibility_focus) ==
                resource->accessibility_nodes.end())
                resource->accessibility_focus = NK_ACCESSIBILITY_ROOT;
            refresh_accessibility_elements(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_clear(nk_handle handle) {
    return nk::core::result_boundary(
        "unexpected error while clearing iOS accessibility nodes", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            auto resource = surface(handle);
            if (!resource)
                return ios_fail(NK_ERROR_INVALID_HANDLE, "invalid or stale iOS surface handle");
            resource->accessibility_nodes.clear();
            resource->accessibility_focus = NK_ACCESSIBILITY_ROOT;
            refresh_accessibility_elements(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_set_focus(nk_handle handle,
                                                     nk_accessibility_node_id node) {
    return nk::core::result_boundary(
        "unexpected error while focusing an iOS accessibility node", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            auto resource = surface(handle);
            if (!resource)
                return ios_fail(NK_ERROR_INVALID_HANDLE, "invalid or stale iOS surface handle");
            if (node != NK_ACCESSIBILITY_ROOT &&
                resource->accessibility_nodes.find(node) == resource->accessibility_nodes.end())
                return ios_fail(NK_ERROR_INVALID_ARGUMENT,
                                "cannot focus an unknown iOS accessibility node");
            resource->accessibility_focus = node;
            refresh_accessibility_elements(*resource);
            if (node == NK_ACCESSIBILITY_ROOT) {
                UIAccessibilityPostNotification(UIAccessibilityScreenChangedNotification,
                                                resource->accessibility_container);
            } else if (resource->accessibility_container) {
                for (NKIOSAccessibilityElement *element in resource->accessibility_container
                         .elements)
                    if (element.node == node) {
                        UIAccessibilityPostNotification(UIAccessibilityScreenChangedNotification,
                                                        element);
                        break;
                    }
            }
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_update(nk_handle handle,
                                                  const nk_accessibility_update *update) {
    return nk::core::result_boundary(
        "unexpected error while updating iOS accessibility nodes", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            auto resource = surface(handle);
            if (!resource)
                return ios_fail(NK_ERROR_INVALID_HANDLE, "invalid or stale iOS surface handle");
            if (!update || update->struct_size < sizeof(*update) ||
                (update->flags & ~NK_ACCESSIBILITY_UPDATE_FOCUS) ||
                (update->node_count && !update->nodes) ||
                (update->removed_node_count && !update->removed_nodes))
                return ios_fail(NK_ERROR_INVALID_ARGUMENT, "invalid iOS accessibility update");

            auto nodes = resource->accessibility_nodes;
            for (uint32_t index = 0; index < update->removed_node_count; ++index) {
                const auto removed = update->removed_nodes[index];
                if (!removed || nodes.find(removed) == nodes.end())
                    return ios_fail(NK_ERROR_INVALID_ARGUMENT,
                                    "iOS accessibility update removes an unknown node");
                remove_accessibility_descendants(nodes, removed);
            }
            for (uint32_t index = 0; index < update->node_count; ++index) {
                const auto &node = update->nodes[index];
                IOSAccessibilityNode copy;
                if (!copy_accessibility_node(node, nodes, copy))
                    return ios_fail(NK_ERROR_INVALID_ARGUMENT,
                                    "invalid node in iOS accessibility update");
                if (const auto old = nodes.find(node.id); old != nodes.end())
                    copy.text_ranges = old->second.text_ranges;
                nodes[node.id] = std::move(copy);
            }
            if ((update->flags & NK_ACCESSIBILITY_UPDATE_FOCUS) &&
                update->focus != NK_ACCESSIBILITY_ROOT && nodes.find(update->focus) == nodes.end())
                return ios_fail(NK_ERROR_INVALID_ARGUMENT,
                                "iOS accessibility update focuses an unknown node");
            resource->accessibility_nodes = std::move(nodes);
            if (update->flags & NK_ACCESSIBILITY_UPDATE_FOCUS)
                resource->accessibility_focus = update->focus;
            else if (resource->accessibility_focus != NK_ACCESSIBILITY_ROOT &&
                     resource->accessibility_nodes.find(resource->accessibility_focus) ==
                         resource->accessibility_nodes.end())
                resource->accessibility_focus = NK_ACCESSIBILITY_ROOT;
            refresh_accessibility_elements(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_set_text_ranges(
    nk_handle handle, nk_accessibility_node_id node, const nk_accessibility_text_range *ranges,
    uint32_t range_count) {
    return nk::core::result_boundary(
        "unexpected error while setting iOS accessibility text ranges", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            auto resource = surface(handle);
            if (!resource)
                return ios_fail(NK_ERROR_INVALID_HANDLE, "invalid or stale iOS surface handle");
            const auto found = resource->accessibility_nodes.find(node);
            if (!node || found == resource->accessibility_nodes.end() || (range_count && !ranges))
                return ios_fail(NK_ERROR_INVALID_ARGUMENT, "invalid iOS accessibility text ranges");
            std::vector<IOSAccessibilityTextRange> copy;
            copy.reserve(range_count);
            nk_accessibility_text_position previous = 0;
            for (uint32_t index = 0; index < range_count; ++index) {
                const auto &range = ranges[index];
                if (range.start >= range.end || range.start < previous || !std::isfinite(range.x) ||
                    !std::isfinite(range.y) || !std::isfinite(range.width) ||
                    !std::isfinite(range.height) || range.width < 0 || range.height < 0)
                    return ios_fail(NK_ERROR_INVALID_ARGUMENT,
                                    "invalid or unordered iOS accessibility text ranges");
                copy.push_back(
                    {range.start, range.end, range.x, range.y, range.width, range.height});
                previous = range.end;
            }
            found->second.text_ranges = std::move(copy);
            refresh_accessibility_elements(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_shell_open_url(const char *url) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!valid_utf8(url))
        return ios_fail(NK_ERROR_INVALID_ARGUMENT, "URL is not valid UTF-8");
    NSString *value = native_string(url);
    NSURL *native = value ? [NSURL URLWithString:value] : nil;
    return open_ios_url(native);
}

nk_result NK_CALL nk_shell_open_file(const char *path) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!valid_utf8(path))
        return ios_fail(NK_ERROR_INVALID_ARGUMENT, "file path is not valid UTF-8");
    NSString *value = native_string(path);
    if (!value.length)
        return ios_fail(NK_ERROR_INVALID_ARGUMENT, "file path must not be empty");
    if (![value isAbsolutePath])
        value = [NSFileManager.defaultManager.currentDirectoryPath
            stringByAppendingPathComponent:value];
    return open_ios_url([NSURL fileURLWithPath:value.stringByStandardizingPath]);
}

nk_result NK_CALL nk_shell_reveal_file(const char *path) {
    // iOS has no user-visible filesystem browser. Opening the file through the
    // installed document handler is the closest platform-level equivalent.
    return nk_shell_open_file(path);
}

nk_result NK_CALL nk_shell_open_resource(const nk_resource *resource) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!resource || resource->struct_size < sizeof(*resource) || !resource->uri ||
        !*resource->uri || !valid_utf8(resource->uri) || !valid_utf8(resource->mime_type) ||
        !valid_utf8(resource->display_name))
        return ios_fail(NK_ERROR_INVALID_ARGUMENT, "resource descriptor is invalid");
    return nk_shell_open_url(resource->uri);
}

nk_result NK_CALL nk_clipboard_set_text(const char *text) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!text)
        return ios_fail(NK_ERROR_INVALID_ARGUMENT, "clipboard text must not be null");
    NSString *value = native_string(text);
    if (!value)
        return ios_fail(NK_ERROR_INVALID_ARGUMENT, "clipboard text is not valid UTF-8");
    UIPasteboard.generalPasteboard.string = value;
    return NK_OK;
}

nk_result NK_CALL nk_share(const nk_share_options *options) {
    return nk::core::result_boundary(
        "unexpected error while sharing iOS resources", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (!options || options->struct_size < sizeof(*options) || options->flags != 0 ||
                (!options->text && options->resource_count == 0) || !valid_utf8(options->title) ||
                !valid_utf8(options->text))
                return ios_fail(NK_ERROR_INVALID_ARGUMENT, "invalid or empty iOS share options");
            if (const auto result = nk::platform::validate_resources(options->resources,
                                                                     options->resource_count, true);
                result != NK_OK)
                return result;
            NSMutableArray *items = [NSMutableArray arrayWithCapacity:options->resource_count + 1];
            if (options->text) {
                NSString *text = native_string(options->text);
                [items addObject:text ? text : @""];
            }
            for (uint32_t index = 0; index < options->resource_count; ++index) {
                NSURL *url = [NSURL URLWithString:native_string(options->resources[index].uri)];
                if (!url || !url.scheme.length)
                    return ios_fail(NK_ERROR_INVALID_ARGUMENT, "resource URI is not a valid URL");
                retain_security_scope(url);
                [items addObject:url];
            }
            nk_handle ignored_parent = NK_INVALID_HANDLE;
            UIViewController *presenter = dialog_presenter(NK_INVALID_HANDLE, ignored_parent);
            (void)ignored_parent;
            if (!presenter)
                return ios_fail(NK_ERROR_UNSUPPORTED,
                                "iOS sharing requires an attached host view controller");
            UIActivityViewController *controller =
                [[UIActivityViewController alloc] initWithActivityItems:items
                                                  applicationActivities:nil];
            if (!controller)
                return ios_fail(NK_ERROR_OUT_OF_MEMORY, "could not create the iOS share sheet");
            if (controller.popoverPresentationController) {
                controller.popoverPresentationController.sourceView = presenter.view;
                controller.popoverPresentationController.sourceRect = presenter.view.bounds;
            }
            [presenter presentViewController:controller animated:YES completion:nil];
            return NK_OK;
        });
}

nk_result NK_CALL nk_clipboard_set_resources(const nk_resource *resources,
                                             uint32_t resource_count) {
    return nk::core::result_boundary(
        "unexpected error while writing iOS resource clipboard", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (const auto result =
                    nk::platform::validate_resources(resources, resource_count, false);
                result != NK_OK)
                return result;
            NSMutableArray<NSURL *> *urls = [NSMutableArray arrayWithCapacity:resource_count];
            for (uint32_t index = 0; index < resource_count; ++index) {
                NSURL *url = [NSURL URLWithString:native_string(resources[index].uri)];
                if (!url || !url.scheme.length)
                    return ios_fail(NK_ERROR_INVALID_ARGUMENT, "resource URI is not a valid URL");
                retain_security_scope(url);
                [urls addObject:url];
            }
            UIPasteboard.generalPasteboard.URLs = urls;
            return NK_OK;
        });
}

nk_result NK_CALL nk_clipboard_read_resources(nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while reading iOS resource clipboard", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (!out_request)
                return ios_fail(NK_ERROR_INVALID_ARGUMENT, "clipboard request output is null");
            *out_request = NK_INVALID_REQUEST_ID;
            std::vector<IOSResourceValue> resources;
            NSArray<NSURL *> *clipboard_urls = UIPasteboard.generalPasteboard.URLs;
            if (!clipboard_urls)
                clipboard_urls = @[];
            for (NSURL *url in clipboard_urls) {
                auto resource = resource_value_from_url(url, NK_DIALOG_OPEN_RESOURCE);
                if (!resource.uri.empty())
                    resources.push_back(std::move(resource));
            }
            const auto request = nk::core::next_request_id();
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE;
            event.request_id = request;
            event.data_count = static_cast<uint32_t>(resources.size());
            event.data = resource_payload(false, resources);
            const auto result = nk::core::push_event(std::move(event));
            if (result != NK_OK)
                return ios_fail(result, "could not queue iOS resource clipboard result");
            *out_request = request;
            return NK_OK;
        });
}

nk_result NK_CALL nk_clipboard_set_files(const char *const *paths, uint32_t path_count) {
    return nk::core::result_boundary(
        "unexpected error while writing iOS clipboard files", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (!paths || !path_count)
                return ios_fail(NK_ERROR_INVALID_ARGUMENT, "clipboard file list must not be empty");
            NSMutableArray<NSURL *> *urls = [NSMutableArray arrayWithCapacity:path_count];
            for (uint32_t index = 0; index < path_count; ++index) {
                if (!valid_utf8(paths[index]))
                    return ios_fail(NK_ERROR_INVALID_ARGUMENT,
                                    "clipboard file path is not valid UTF-8");
                NSString *path = native_string(paths[index]);
                if (!path.length)
                    return ios_fail(NK_ERROR_INVALID_ARGUMENT, "clipboard file path is empty");
                if (![path isAbsolutePath])
                    path = [NSFileManager.defaultManager.currentDirectoryPath
                        stringByAppendingPathComponent:path];
                [urls addObject:[NSURL fileURLWithPath:path.stringByStandardizingPath]];
            }
            UIPasteboard.generalPasteboard.URLs = urls;
            return NK_OK;
        });
}

nk_result NK_CALL nk_clipboard_read_text(nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while reading iOS clipboard text", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (!out_request)
                return ios_fail(NK_ERROR_INVALID_ARGUMENT, "clipboard request output is null");
            *out_request = NK_INVALID_REQUEST_ID;
            const auto request = nk::core::next_request_id();
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_CLIPBOARD_TEXT_COMPLETE;
            event.request_id = request;
            event.data = text_bytes(utf8_string(UIPasteboard.generalPasteboard.string));
            const auto result = nk::core::push_event(std::move(event));
            if (result != NK_OK)
                return ios_fail(result, "could not queue iOS clipboard text result");
            *out_request = request;
            return NK_OK;
        });
}

nk_result NK_CALL nk_clipboard_read_files(nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while reading iOS clipboard files", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (!out_request)
                return ios_fail(NK_ERROR_INVALID_ARGUMENT, "clipboard request output is null");
            *out_request = NK_INVALID_REQUEST_ID;
            std::vector<std::string> paths;
            NSArray<NSURL *> *clipboard_urls = UIPasteboard.generalPasteboard.URLs;
            if (!clipboard_urls)
                clipboard_urls = @[];
            for (NSURL *url in clipboard_urls) {
                if (!url.isFileURL || !url.path.length)
                    continue;
                paths.push_back(utf8_string(url.path));
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
                return ios_fail(result, "could not queue iOS clipboard file result");
            *out_request = request;
            return NK_OK;
        });
}

nk_result NK_CALL nk_dialog_open_resource(nk_handle parent, const nk_file_dialog_options *options,
                                          nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while opening an iOS resource dialog", [&]() -> nk_result {
            return start_resource_dialog(parent, options, out_request, NK_DIALOG_OPEN_RESOURCE);
        });
}

nk_result NK_CALL nk_dialog_save_resource(nk_handle parent, const nk_file_dialog_options *options,
                                          nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while opening an iOS resource save dialog", [&]() -> nk_result {
            return start_resource_dialog(parent, options, out_request, NK_DIALOG_SAVE_RESOURCE);
        });
}

nk_result NK_CALL nk_dialog_select_resource_directory(nk_handle parent,
                                                      const nk_file_dialog_options *options,
                                                      nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while opening an iOS resource directory dialog", [&]() -> nk_result {
            return start_resource_dialog(parent, options, out_request,
                                         NK_DIALOG_SELECT_RESOURCE_DIRECTORY);
        });
}

nk_result NK_CALL nk_dialog_message(nk_handle parent, const nk_message_dialog_options *options,
                                    nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while opening an iOS message dialog",
        [&]() -> nk_result { return start_message_dialog(parent, options, out_request); });
}

nk_result NK_CALL nk_dialog_cancel(nk_request_id request) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    return cancel_dialog_request(request);
}

nk_result NK_CALL nk_notification_show(const nk_notification_options *options,
                                       nk_request_id *out_request) {
    return nk::core::result_boundary(
        "unexpected error while showing an iOS notification", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (!options || options->struct_size < sizeof(*options) || !out_request ||
                !options->title || !*options->title ||
                (options->flags & ~NK_NOTIFICATION_SILENT) != 0 || !valid_utf8(options->title) ||
                !valid_utf8(options->body) || !valid_utf8(options->icon))
                return ios_fail(NK_ERROR_INVALID_ARGUMENT, "invalid iOS notification options");
            *out_request = NK_INVALID_REQUEST_ID;
            NSString *title = native_string(options->title);
            NSString *body = native_string(options->body);
            if (!body)
                body = @"";
            UNMutableNotificationContent *content = [UNMutableNotificationContent new];
            if (!title || !content)
                return ios_fail(NK_ERROR_OUT_OF_MEMORY,
                                "could not allocate iOS notification content");
            content.title = title;
            content.body = body;
            content.categoryIdentifier = @"nativekit.default";
            const bool silent = (options->flags & NK_NOTIFICATION_SILENT) != 0;
            if (!silent)
                content.sound = UNNotificationSound.defaultSound;
            if (options->icon && *options->icon) {
                NSString *path = native_string(options->icon);
                if (!path || !path.isAbsolutePath ||
                    ![NSFileManager.defaultManager fileExistsAtPath:path])
                    return ios_fail(NK_ERROR_INVALID_ARGUMENT,
                                    "iOS notification icon path does not exist");
                NSError *error = nil;
                UNNotificationAttachment *attachment =
                    [UNNotificationAttachment attachmentWithIdentifier:@"icon"
                                                                   URL:[NSURL fileURLWithPath:path]
                                                               options:nil
                                                                 error:&error];
                if (!attachment)
                    return ios_fail(NK_ERROR_INVALID_ARGUMENT,
                                    "iOS notification icon could not be attached");
                content.attachments = @[ attachment ];
            }
            UNUserNotificationCenter *center = UNUserNotificationCenter.currentNotificationCenter;
            if (!center)
                return ios_fail(NK_ERROR_UNSUPPORTED, "iOS notification services are unavailable");
            const auto request = nk::core::next_request_id();
            const auto generation = nk::core::runtime_generation();
            {
                std::lock_guard lock(notifications_mutex);
                notifications.emplace(request, IOSNotification{generation, silent});
            }
            if (!notification_delegate)
                notification_delegate = [NKIOSNotificationDelegate new];
            if (!notification_delegate) {
                take_notification(request);
                return ios_fail(NK_ERROR_OUT_OF_MEMORY,
                                "could not allocate the iOS notification delegate");
            }
            notification_center_initialized = true;
            center.delegate = notification_delegate;
            UNNotificationRequest *native_request =
                [UNNotificationRequest requestWithIdentifier:notification_identifier(request)
                                                     content:content
                                                     trigger:nil];
            if (!native_request) {
                take_notification(request);
                return ios_fail(NK_ERROR_OUT_OF_MEMORY,
                                "could not allocate the iOS notification request");
            }
            const UNAuthorizationOptions authorization_options =
                silent ? UNAuthorizationOptionAlert
                       : (UNAuthorizationOptionAlert | UNAuthorizationOptionSound);
            [center
                requestAuthorizationWithOptions:authorization_options
                              completionHandler:^(BOOL granted, NSError *error) {
                                if (!has_notification(request, generation) ||
                                    !nk::core::is_runtime_generation(generation))
                                    return;
                                if (!granted) {
                                    take_notification(request);
                                    NSString *description = error.localizedDescription;
                                    if (!description)
                                        description = @"iOS notification permission was denied";
                                    emit_notification(NK_EVENT_NOTIFICATION_FAILED, request,
                                                      NK_ERROR_UNSUPPORTED, description);
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
                                  [center
                                      addNotificationRequest:native_request
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
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!request || !take_notification(request))
        return ios_fail(NK_ERROR_INVALID_REQUEST, "invalid or completed iOS notification request");
    NSArray<NSString *> *identifiers = @[ notification_identifier(request) ];
    UNUserNotificationCenter *center = UNUserNotificationCenter.currentNotificationCenter;
    [center removePendingNotificationRequestsWithIdentifiers:identifiers];
    [center removeDeliveredNotificationsWithIdentifiers:identifiers];
    emit_notification(NK_EVENT_NOTIFICATION_DISMISSED, request);
    return NK_OK;
}

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_MOBILE_HOST | NK_CAP_RESOURCE_IO | NK_CAP_SYSTEM_INFO | NK_CAP_METAL_SURFACE |
           NK_CAP_APPLICATION_PATH | NK_CAP_APPLICATION_STORAGE | NK_CAP_KEEP_AWAKE |
           NK_CAP_DEVICE_ORIENTATION | NK_CAP_DISPLAY_ORIENTATION | NK_CAP_SENSORS |
           NK_CAP_HAPTICS | NK_CAP_GAMEPAD_RUMBLE | NK_CAP_INPUT | NK_CAP_WEBVIEW |
           NK_CAP_CLIPBOARD | NK_CAP_SHELL | NK_CAP_SYSTEM_APPEARANCE | NK_CAP_NOTIFICATION |
           NK_CAP_ACCESSIBILITY | NK_CAP_DRAG_DROP | NK_CAP_RESOURCE_SHARING | NK_CAP_JOYSTICK |
           NK_CAP_SURFACE_FRAME_CALLBACK | nk::core::optional_capabilities();
}
}
