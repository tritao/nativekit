#include "nativekit_mobile.h"
#include "nativekit_window.h"

#include "core/event_queue.hpp"
#include "core/error.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"
#include "core/system_internal.hpp"

#import <UIKit/UIKit.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

@interface NKIOSHostObserver : NSObject
@property(nonatomic, assign) nk_handle host;
@property(nonatomic, weak) UIView *view;
- (void)orientationChanged:(NSNotification *)notification;
@end

namespace {

template <typename T> std::vector<std::byte> bytes_of(const T &value) {
    const auto *begin = reinterpret_cast<const std::byte *>(&value);
    return std::vector<std::byte>(begin, begin + sizeof(T));
}

struct IOSHost;
nk_orientation ios_orientation(UIDeviceOrientation orientation);
nk_orientation interface_orientation(UIInterfaceOrientation orientation);
void queue_geometry(const std::shared_ptr<IOSHost> &host);
void queue_orientation(const std::shared_ptr<IOSHost> &host);
void set_orientation_observing(const std::shared_ptr<IOSHost> &host, bool enabled);

struct IOSHost final : nk::core::Resource {
    nk_handle handle = NK_INVALID_HANDLE;
    __strong UIView *view = nil;
    __strong NKIOSHostObserver *observer = nil;
    nk_mobile_lifecycle_state lifecycle = NK_MOBILE_LIFECYCLE_ACTIVE;
    bool orientation_observing = false;
    nk_orientation last_display_orientation = NK_ORIENTATION_UNKNOWN;
};

std::unordered_map<nk_handle, std::shared_ptr<IOSHost>> hosts;
nk_orientation last_device_orientation = NK_ORIENTATION_UNKNOWN;

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
    if (@available(iOS 13.0, *))
        display = window.windowScene.interfaceOrientation;
    else
        display = UIApplication.sharedApplication.statusBarOrientation;
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

} // namespace

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

namespace nk::backend {

void pump_events() noexcept {}

void shutdown() noexcept {
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
    try {
        resource->handle = handle;
        hosts.emplace(handle, resource);
        observe_view(resource, handle);
    } catch (...) {
        nk::core::handles().erase(handle, nk::core::ResourceType::mobile_host);
        nk::core::set_error("could not retain the iOS mobile host");
        return NK_ERROR_OUT_OF_MEMORY;
    }
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
    stop_observing(found->second);
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
    resource->lifecycle = state;
    set_orientation_observing(resource, state != NK_MOBILE_LIFECYCLE_BACKGROUND);
    if (nk::core::system_keep_awake_held())
        (void)nk::core::system_backend::keep_awake_apply(has_active_host());
    return NK_OK;
}

nk_result mobile_host_dispatch_event(nk_handle handle, const nk_mobile_host_event &) {
    if (!host(handle)) {
        nk::core::set_error("invalid iOS mobile host handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    nk::core::set_error("iOS host events are not implemented yet");
    return NK_ERROR_UNSUPPORTED;
}

nk_result mobile_host_set_drop_enabled(nk_handle handle, bool) {
    if (!host(handle)) {
        nk::core::set_error("invalid iOS mobile host handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    nk::core::set_error("iOS host drops are not implemented yet");
    return NK_ERROR_UNSUPPORTED;
}

} // namespace nk::backend

namespace {

nk_result copy_output(NSString *value, char *buffer, uint32_t *inout_size) {
    if (!value)
        return NK_ERROR_UNSUPPORTED;
    if (!inout_size) {
        nk::core::set_error("iOS string size output must not be null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const char *text = value.UTF8String;
    const auto length = text ? std::strlen(text) : 0;
    if (length >= std::numeric_limits<uint32_t>::max())
        return NK_ERROR_UNKNOWN;
    const auto required = static_cast<uint32_t>(length + 1);
    const auto capacity = *inout_size;
    *inout_size = required;
    if (!buffer || capacity < required)
        return NK_ERROR_BUFFER_TOO_SMALL;
    std::memcpy(buffer, text, required);
    return NK_OK;
}

NSString *application_storage_path() {
    NSArray<NSURL *> *urls = [NSFileManager.defaultManager URLsForDirectory:NSApplicationSupportDirectory
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
    out_orientation.display = interface_orientation(UIApplication.sharedApplication.statusBarOrientation);
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
        NSArray<NSURL *> *urls = [NSFileManager.defaultManager URLsForDirectory:
            kind == NK_DIRECTORY_CONFIG ? NSLibraryDirectory : NSApplicationSupportDirectory
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
    appearance->color_scheme = UITraitCollection.currentTraitCollection.userInterfaceStyle ==
                                       UIUserInterfaceStyleDark
                                   ? NK_COLOR_SCHEME_DARK
                                   : NK_COLOR_SCHEME_LIGHT;
    appearance->high_contrast = UIAccessibilityIsDarkerSystemColorsEnabled();
    return NK_OK;
}

} // extern "C"

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_MOBILE_HOST | NK_CAP_RESOURCE_IO | NK_CAP_SYSTEM_INFO |
           NK_CAP_APPLICATION_PATH | NK_CAP_APPLICATION_STORAGE | NK_CAP_KEEP_AWAKE |
           NK_CAP_DEVICE_ORIENTATION | NK_CAP_DISPLAY_ORIENTATION |
           nk::core::optional_capabilities();
}
}
