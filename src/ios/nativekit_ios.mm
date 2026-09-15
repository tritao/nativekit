#include "nativekit_mobile.h"
#include "nativekit_window.h"

#include "core/event_queue.hpp"
#include "core/error.hpp"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"

#import <UIKit/UIKit.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

@interface NKIOSHostObserver : NSObject
@property(nonatomic, assign) nk_handle host;
@property(nonatomic, weak) UIView *view;
@end

namespace {

template <typename T> std::vector<std::byte> bytes_of(const T &value) {
    const auto *begin = reinterpret_cast<const std::byte *>(&value);
    return std::vector<std::byte>(begin, begin + sizeof(T));
}

struct IOSHost;
void queue_geometry(const std::shared_ptr<IOSHost> &host);

struct IOSHost final : nk::core::Resource {
    nk_handle handle = NK_INVALID_HANDLE;
    __strong UIView *view = nil;
    __strong NKIOSHostObserver *observer = nil;
    nk_mobile_lifecycle_state lifecycle = NK_MOBILE_LIFECYCLE_ACTIVE;
};

std::unordered_map<nk_handle, std::shared_ptr<IOSHost>> hosts;

std::shared_ptr<IOSHost> host(nk_handle handle) {
    return std::dynamic_pointer_cast<IOSHost>(
        nk::core::handles().get(handle, nk::core::ResourceType::mobile_host));
}

void stop_observing(const std::shared_ptr<IOSHost> &resource) {
    if (!resource || !resource->view || !resource->observer)
        return;
    [resource->view removeObserver:resource->observer forKeyPath:@"bounds"];
    [resource->view removeObserver:resource->observer forKeyPath:@"safeAreaInsets"];
    [resource->view removeObserver:resource->observer forKeyPath:@"contentScaleFactor"];
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

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_MOBILE_HOST | NK_CAP_RESOURCE_IO;
}
}
