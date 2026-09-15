#include "nativekit_mobile.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#include "core/event_queue.hpp"
#include "core/error.hpp"
#include "core/graphics_frame_target.hpp"
#include "core/graphics_image_registry.h"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"
#include "core/system_internal.hpp"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <algorithm>
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

@interface NKIOSSurfaceTimer : NSObject
@property(nonatomic, assign) nk_handle surface;
- (void)tick:(CADisplayLink *)link;
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
struct IOSSurface;
void update_host_surfaces(const std::shared_ptr<IOSHost> &host);

struct IOSHost final : nk::core::Resource {
    nk_handle handle = NK_INVALID_HANDLE;
    __strong UIView *view = nil;
    __strong NKIOSHostObserver *observer = nil;
    nk_mobile_lifecycle_state lifecycle = NK_MOBILE_LIFECYCLE_ACTIVE;
    bool orientation_observing = false;
    nk_orientation last_display_orientation = NK_ORIENTATION_UNKNOWN;
    std::vector<nk_handle> surfaces;
};

struct IOSSurface final : nk::core::Resource {
    __strong UIView *host_view = nil;
    __strong CAMetalLayer *layer = nil;
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
    bool frame_prepared = false;
    bool ready = false;
    bool lost_reported = false;
    bool destroying = false;

    ~IOSSurface() override {
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

std::unordered_map<nk_handle, std::shared_ptr<IOSHost>> hosts;
std::unordered_map<nk_handle, std::shared_ptr<IOSSurface>> surfaces;
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

std::shared_ptr<IOSSurface> surface(nk_handle handle) {
    const auto found = surfaces.find(handle);
    return found == surfaces.end() ? nullptr : found->second;
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
    const CGFloat scale = resource.host_view.contentScaleFactor > 0
                              ? resource.host_view.contentScaleFactor
                              : 1.0;
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
    sync_surface_drawable_size(resource);
    return true;
}

void update_host_surfaces(const std::shared_ptr<IOSHost> &resource) {
    if (!resource)
        return;
    for (const nk_handle handle : resource->surfaces)
        if (auto child = surface(handle)) {
            child->host_view = resource->view;
            sync_surface_drawable_size(*child);
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
        if (nk_surface_make_current(handle) != NK_OK)
            return;
        auto active = surface(handle);
        if (!active || !active->frame_callback)
            return;
        active->frame_callback(handle, active->framebuffer_width, active->framebuffer_height,
                               active->frame_user_data);
        if (active->frame_prepared)
            nk_surface_present(handle);
    });
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

@implementation NKIOSSurfaceTimer
- (void)tick:(CADisplayLink *)link {
    (void)link;
    frame_tick(self.surface);
}
@end

namespace nk::backend {

void pump_events() noexcept {}

void shutdown() noexcept {
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
    const auto child_surfaces = found->second->surfaces;
    for (auto iter = child_surfaces.rbegin(); iter != child_surfaces.rend(); ++iter)
        if (surface(*iter)) {
            const auto result = nk_surface_destroy(*iter);
            if (result != NK_OK)
                return result;
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
    const bool becoming_unavailable = resource->lifecycle == NK_MOBILE_LIFECYCLE_ACTIVE &&
                                      state != NK_MOBILE_LIFECYCLE_ACTIVE;
    resource->lifecycle = state;
    set_orientation_observing(resource, state != NK_MOBILE_LIFECYCLE_BACKGROUND);
    if (nk::core::system_keep_awake_held())
        (void)nk::core::system_backend::keep_awake_apply(has_active_host());
    if (becoming_unavailable)
        for (const nk_handle child_handle : resource->surfaces)
            if (auto child = surface(child_handle))
                emit_surface_lost(*child);
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
            [parent->view.layer addSublayer:resource->layer];
            if (!set_surface_native_bounds(*resource)) {
                nk::core::set_error("could not attach the iOS Metal layer");
                return NK_ERROR_UNKNOWN;
            }
            if (resource->framebuffer_width > 0 && resource->framebuffer_height > 0 &&
                !ensure_surface_depth_target(*resource, resource->framebuffer_width,
                                              resource->framebuffer_height))
                return NK_ERROR_UNSUPPORTED;
            resource->handle = nk::core::handles().insert(nk::core::ResourceType::surface, resource);
            if (!resource->handle) {
                nk::core::set_error("iOS graphics surface handle registry is full");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            if (!resource->device_handle)
                resource->device_handle = resource->handle;
            try {
                surfaces.emplace(resource->handle, resource);
                parent->surfaces.push_back(resource->handle);
            } catch (...) {
                surfaces.erase(resource->handle);
                nk::core::handles().erase(resource->handle, nk::core::ResourceType::surface);
                nk::core::set_error("could not retain the iOS Metal surface");
                return NK_ERROR_OUT_OF_MEMORY;
            }
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
    return set_surface_native_bounds(*resource) ? NK_OK
                                                 : (nk::core::set_error(
                                                       "could not resize the iOS Metal layer"),
                                                    NK_ERROR_UNKNOWN);
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
    target.native_target = prepared && resource->drawable
                               ? metal_object_token(resource->drawable.texture)
                               : 0;
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

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_MOBILE_HOST | NK_CAP_RESOURCE_IO | NK_CAP_SYSTEM_INFO |
           NK_CAP_METAL_SURFACE | NK_CAP_APPLICATION_PATH | NK_CAP_APPLICATION_STORAGE |
           NK_CAP_KEEP_AWAKE | NK_CAP_DEVICE_ORIENTATION | NK_CAP_DISPLAY_ORIENTATION |
           nk::core::optional_capabilities();
}
}
