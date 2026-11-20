#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_time.h"
#include "nativekit_window.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include <assert.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

/* A resized window keeps its UI at its own size: the surface's drawable follows the new bounds,
   and until a frame at that size is presented, the previous one is not scaled into them. */

static nk_native_window native_window(nk_window window) {
    nk_native_window native = {0};
    native.struct_size = sizeof(native);
    assert(nk_window_get_native(window, &native) == NK_OK);
    return native;
}

static NSView *surface_view(nk_window window) {
    NSView *content = (__bridge NSView *)(void *)native_window(window).view;
    for (NSView *child in content.subviews)
        if ([child.layer isKindOfClass:[CAMetalLayer class]])
            return child;
    return nil;
}

/* Delivers events until a resize of `kind` from `source` arrives; returns false on timeout. */
static bool wait_for_resize(nk_event_kind kind, nk_handle source, void *payload, size_t size) {
    for (int attempt = 0; attempt < 300; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        const bool match = event.kind == kind && event.source == source;
        if (match) {
            assert(event.data_size >= size);
            memcpy(payload, event.data, size);
        }
        nk_event_release(&event);
        if (match)
            return true;
        usleep(10000);
    }
    return false;
}

static void check_drawable(nk_surface surface, CAMetalLayer *layer, int32_t width, int32_t height,
                           double scale) {
    const int32_t expected_width = (int32_t)lround(width * scale);
    const int32_t expected_height = (int32_t)lround(height * scale);
    assert((int32_t)layer.drawableSize.width == expected_width);
    assert((int32_t)layer.drawableSize.height == expected_height);
    int32_t framebuffer_width = 0, framebuffer_height = 0;
    assert(nk_surface_get_framebuffer_size(surface, &framebuffer_width, &framebuffer_height) ==
           NK_OK);
    assert(framebuffer_width == expected_width && framebuffer_height == expected_height);
}

static void resize_window(nk_window window, nk_surface surface, CAMetalLayer *layer, int32_t width,
                          int32_t height, double scale) {
    const NSRect frame = ((__bridge NSWindow *)(void *)native_window(window).window).frame;
    assert(nk_window_set_bounds(window, (int32_t)frame.origin.x, (int32_t)frame.origin.y, width,
                                height) == NK_OK);
    nk_window_resize_event resized = {0};
    assert(wait_for_resize(NK_EVENT_WINDOW_RESIZE, window, &resized, sizeof(resized)));
    assert(resized.width == width && resized.height == height);

    /* The host answers a window resize by resizing its surface, as DesktopUiHost does. */
    assert(nk_surface_set_bounds(surface, 0, 0, resized.width, resized.height) == NK_OK);
    nk_surface_resize_event surface_resized = {0};
    assert(wait_for_resize(NK_EVENT_SURFACE_RESIZE, surface, &surface_resized,
                           sizeof(surface_resized)));
    assert(surface_resized.width == width && surface_resized.height == height);
    assert(surface_resized.framebuffer_width == (int32_t)lround(width * scale));
    assert(surface_resized.framebuffer_height == (int32_t)lround(height * scale));
    assert(NSEqualSizes(layer.bounds.size, NSMakeSize(width, height)));
    check_drawable(surface, layer, width, height, scale);
}

int main(void) {
    @autoreleasepool {
        nk_init_options init = {0};
        init.struct_size = sizeof(init);
        init.api_version = NK_API_VERSION;
        assert(nk_init(&init) == NK_OK);

        nk_window_options window_options = {0};
        window_options.struct_size = sizeof(window_options);
        window_options.width = 320;
        window_options.height = 240;
        window_options.flags = NK_WINDOW_RESIZABLE | NK_WINDOW_BORDERLESS;
        nk_window window = NK_INVALID_HANDLE;
        assert(nk_window_create(&window_options, &window) == NK_OK);

        nk_surface_options surface_options = {0};
        surface_options.struct_size = sizeof(surface_options);
        surface_options.api = NK_GRAPHICS_METAL;
        surface_options.width = 320;
        surface_options.height = 240;
        nk_surface surface = NK_INVALID_HANDLE;
        const nk_result created = nk_surface_create(window, &surface_options, &surface);
        if (created == NK_ERROR_UNSUPPORTED) {
            assert(nk_window_destroy(window) == NK_OK);
            nk_shutdown();
            return 77;
        }
        assert(created == NK_OK);

        NSView *view = surface_view(window);
        assert(view);
        CAMetalLayer *layer = (CAMetalLayer *)view.layer;

        /* A drawable that no longer matches the bounds stays at its own size, anchored top-left,
           instead of being scaled to fit them. */
        assert(view.layerContentsPlacement == NSViewLayerContentsPlacementTopLeft);
        assert(![layer.contentsGravity isEqualToString:kCAGravityResize]);
        assert(![layer.contentsGravity isEqualToString:kCAGravityResizeAspect]);
        assert(![layer.contentsGravity isEqualToString:kCAGravityResizeAspectFill]);

        /* The framebuffer is reported only once the window is on screen and not occluded. */
        int32_t framebuffer_width = 0, framebuffer_height = 0;
        assert(nk_window_activate(window) == NK_OK);
        for (int attempt = 0; attempt < 200 && framebuffer_width == 0; ++attempt) {
            assert(nk_wait_events_timeout(0.01) == NK_OK);
            assert(nk_surface_get_framebuffer_size(surface, &framebuffer_width,
                                                   &framebuffer_height) == NK_OK);
        }
        if (framebuffer_width == 0) {
            assert(nk_surface_destroy(surface) == NK_OK);
            assert(nk_window_destroy(window) == NK_OK);
            nk_shutdown();
            return 77;
        }

        float scale = 0.0f;
        assert(nk_window_get_scale(window, &scale) == NK_OK && scale > 0.0f);
        check_drawable(surface, layer, 320, 240, scale);

        resize_window(window, surface, layer, 200, 150, scale);
        resize_window(window, surface, layer, 360, 260, scale);

        assert(nk_surface_destroy(surface) == NK_OK);
        assert(nk_window_destroy(window) == NK_OK);
        nk_shutdown();
    }
    return 0;
}
