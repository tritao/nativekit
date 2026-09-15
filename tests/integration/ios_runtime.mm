#include "nativekit.h"
#include "nativekit_accessibility.h"
#include "nativekit_graphics.h"
#include "nativekit_input.h"
#include "nativekit_joystick.h"
#include "nativekit_mobile.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"

#import <dispatch/dispatch.h>
#import <UIKit/UIKit.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

bool check_result(const char *operation, nk_result result) {
    if (result == NK_OK)
        return true;
    std::fprintf(stderr, "%s returned %d: %s\n", operation, result, nk_last_error());
    return false;
}

bool check_capabilities(nk_capabilities capabilities) {
    const nk_capabilities required = NK_CAP_MOBILE_HOST | NK_CAP_WEBVIEW | NK_CAP_FILE_DIALOG |
                                     NK_CAP_METAL_SURFACE | NK_CAP_INPUT | NK_CAP_RESOURCE_IO |
                                     NK_CAP_CLIPBOARD | NK_CAP_SHELL | NK_CAP_SYSTEM_APPEARANCE |
                                     NK_CAP_NOTIFICATION | NK_CAP_ACCESSIBILITY | NK_CAP_DRAG_DROP |
                                     NK_CAP_RESOURCE_SHARING | NK_CAP_JOYSTICK;
    if ((capabilities & required) == required)
        return true;
    std::fprintf(stderr, "iOS runtime is missing capabilities: expected 0x%llx, got 0x%llx\n",
                 static_cast<unsigned long long>(required),
                 static_cast<unsigned long long>(capabilities));
    return false;
}

void NK_CALL on_frame(nk_surface surface, int32_t width, int32_t height, void *user_data) {
    (void)surface;
    if (width > 0 && height > 0)
        ++*static_cast<int *>(user_data);
}

bool wait_for_frame_callbacks(int *count) {
    for (int attempt = 0; attempt < 30 && *count == 0; ++attempt)
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    if (*count > 0)
        return true;
    std::fprintf(stderr, "iOS Metal surface frame callback did not run\n");
    return false;
}

bool wait_for_evaluation(nk_request_id request) {
    for (int attempt = 0; attempt < 250; ++attempt) {
        nk_event event = {};
        event.struct_size = sizeof(event);
        if (!check_result("nk_poll_event", nk_poll_event(&event)))
            return false;
        if (event.kind == NK_EVENT_WEBVIEW_EVAL_COMPLETE && event.request_id == request) {
            const bool valid = event.result == NK_OK && event.data_size == 1 && event.data &&
                               std::memcmp(event.data, "2", 1) == 0;
            if (!valid)
                std::fprintf(stderr, "iOS WebView evaluation returned an unexpected result\n");
            nk_event_release(&event);
            return valid;
        }
        nk_event_release(&event);
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
    std::fprintf(stderr, "timed out waiting for iOS WebView evaluation\n");
    return false;
}

bool wait_for_navigation(nk_webview webview) {
    for (int attempt = 0; attempt < 250; ++attempt) {
        nk_event event = {};
        event.struct_size = sizeof(event);
        if (!check_result("nk_poll_event", nk_poll_event(&event)))
            return false;
        if (event.source == webview && event.kind == NK_EVENT_WEBVIEW_NAVIGATED) {
            nk_event_release(&event);
            return true;
        }
        if (event.source == webview && event.kind == NK_EVENT_WEBVIEW_NAVIGATION_FAILED) {
            std::fprintf(stderr, "iOS WebView navigation failed\n");
            nk_event_release(&event);
            return false;
        }
        nk_event_release(&event);
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
    std::fprintf(stderr, "timed out waiting for iOS WebView navigation\n");
    return false;
}

bool wait_for_resource_clipboard(nk_request_id request, const char *expected_uri) {
    for (int attempt = 0; attempt < 250; ++attempt) {
        nk_event event = {};
        event.struct_size = sizeof(event);
        if (!check_result("nk_poll_event", nk_poll_event(&event)))
            return false;
        if (event.kind == NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE && event.request_id == request) {
            nk_resource_view resource = {};
            resource.struct_size = sizeof(resource);
            const bool valid = event.data_count == 1 &&
                               nk_resource_event_item(&event, 0, &resource) == NK_OK &&
                               resource.uri && std::strcmp(resource.uri, expected_uri) == 0 &&
                               (resource.flags & NK_RESOURCE_READABLE) != 0;
            if (!valid)
                std::fprintf(stderr, "iOS resource clipboard returned an unexpected result\n");
            nk_event_release(&event);
            return valid;
        }
        nk_event_release(&event);
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.01]];
    }
    std::fprintf(stderr, "timed out waiting for iOS resource clipboard\n");
    return false;
}

bool run_nativekit_tests(UIView *host_view) {
    nk_init_options init = {};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (!check_result("nk_init", nk_init(&init)))
        return false;

    nk_mobile_host host = NK_INVALID_HANDLE;
    nk_surface surface = NK_INVALID_HANDLE;
    nk_webview webview = NK_INVALID_HANDLE;
    bool success = true;
    do {
        if (!check_capabilities(nk_get_capabilities())) {
            success = false;
            break;
        }

        nk_mobile_host_options host_options = {};
        host_options.struct_size = sizeof(host_options);
        host_options.kind = NK_MOBILE_HOST_UIKIT_VIEW;
        host_options.native_view = reinterpret_cast<uintptr_t>((__bridge void *)host_view);
        if (!check_result("nk_mobile_host_attach", nk_mobile_host_attach(&host_options, &host))) {
            success = false;
            break;
        }
        if (!check_result("nk_mobile_host_set_drop_enabled(true)",
                          nk_mobile_host_set_drop_enabled(host, 1)) ||
            !check_result("nk_mobile_host_set_drop_enabled(false)",
                          nk_mobile_host_set_drop_enabled(host, 0))) {
            success = false;
            break;
        }
        if (!check_result("nk_mobile_host_set_lifecycle(active)",
                          nk_mobile_host_set_lifecycle(host, NK_MOBILE_LIFECYCLE_ACTIVE))) {
            success = false;
            break;
        }

        nk_surface_options surface_options = {};
        surface_options.struct_size = sizeof(surface_options);
        surface_options.flags = NK_SURFACE_DEPTH | NK_SURFACE_STENCIL;
        surface_options.api = NK_GRAPHICS_METAL;
        surface_options.width = 320;
        surface_options.height = 240;
        if (!check_result("nk_surface_create",
                          nk_surface_create(host, &surface_options, &surface))) {
            success = false;
            break;
        }
        if (!check_result("nk_surface_show", nk_surface_show(surface, 1)) ||
            !check_result("nk_surface_set_bounds",
                          nk_surface_set_bounds(surface, 0, 0, 320, 240)) ||
            !check_result("nk_surface_make_current", nk_surface_make_current(surface))) {
            success = false;
            break;
        }
        nk_surface_frame_target target = {};
        target.struct_size = sizeof(target);
        if (!check_result("nk_surface_get_frame_target",
                          nk_surface_get_frame_target(surface, &target)) ||
            target.api != NK_GRAPHICS_METAL || target.width <= 0 || target.height <= 0 ||
            target.device.id == 0 || target.native_target == 0 || target.native_device == 0 ||
            target.native_context == 0 || target.native_present_target == 0 ||
            !check_result("nk_surface_present", nk_surface_present(surface))) {
            success = false;
            break;
        }
        int frame_count = 0;
        if (!check_result("nk_surface_set_frame_callback",
                          nk_surface_set_frame_callback(surface, on_frame, &frame_count)) ||
            !wait_for_frame_callbacks(&frame_count) ||
            !check_result("nk_surface_set_frame_callback(clear)",
                          nk_surface_set_frame_callback(surface, nullptr, nullptr))) {
            success = false;
            break;
        }

        nk_text_input_state text_state = {};
        text_state.struct_size = sizeof(text_state);
        text_state.text = "hello";
        text_state.document_length = 5;
        text_state.selection_start = 5;
        text_state.selection_end = 5;
        text_state.composition_start = NK_TEXT_POSITION_NONE;
        text_state.composition_end = NK_TEXT_POSITION_NONE;
        text_state.cursor_width = 1.0f;
        text_state.cursor_height = 18.0f;
        if (!check_result("nk_surface_set_text_input_state",
                          nk_surface_set_text_input_state(surface, &text_state)) ||
            !check_result("nk_surface_set_text_input_active(true)",
                          nk_surface_set_text_input_active(surface, 1)) ||
            !check_result("nk_surface_set_text_input_active(false)",
                          nk_surface_set_text_input_active(surface, 0))) {
            success = false;
            break;
        }

        nk_accessibility_node node = {};
        node.struct_size = sizeof(node);
        node.id = 1;
        node.parent_id = NK_ACCESSIBILITY_ROOT;
        node.role = NK_ACCESSIBILITY_BUTTON;
        node.states = NK_ACCESSIBILITY_FOCUSABLE;
        node.actions = NK_ACCESSIBILITY_CAN_ACTIVATE | NK_ACCESSIBILITY_CAN_FOCUS;
        node.width = 160;
        node.height = 32;
        node.label = "Runtime test button";
        node.value = "hello";
        node.document_length = 5;
        nk_accessibility_text_range range = {0, 5, 0, 0, 160, 32};
        if (!check_result("nk_surface_accessibility_set_node",
                          nk_surface_accessibility_set_node(surface, &node)) ||
            !check_result("nk_surface_accessibility_set_focus",
                          nk_surface_accessibility_set_focus(surface, node.id)) ||
            !check_result("nk_surface_accessibility_set_text_ranges",
                          nk_surface_accessibility_set_text_ranges(surface, node.id, &range, 1)) ||
            !check_result("nk_surface_accessibility_clear",
                          nk_surface_accessibility_clear(surface))) {
            success = false;
            break;
        }

        nk_webview_options webview_options = {};
        webview_options.struct_size = sizeof(webview_options);
        webview_options.flags = NK_WEBVIEW_HIDDEN;
        webview_options.width = 320;
        webview_options.height = 240;
        if (!check_result("nk_webview_create",
                          nk_webview_create(host, &webview_options, &webview)) ||
            !check_result("nk_webview_set_bounds",
                          nk_webview_set_bounds(webview, 0, 0, 320, 240)) ||
            !check_result("nk_webview_set_html",
                          nk_webview_set_html(webview, "<title>runtime</title>", nullptr)) ||
            !check_result("nk_webview_show", nk_webview_show(webview, 1))) {
            success = false;
            break;
        }
        nk_request_id evaluation = NK_INVALID_REQUEST_ID;
        if (!wait_for_navigation(webview) ||
            !check_result("nk_webview_eval", nk_webview_eval(webview, "1 + 1", &evaluation)) ||
            !wait_for_evaluation(evaluation)) {
            success = false;
            break;
        }

        nk_system_appearance appearance = {};
        appearance.struct_size = sizeof(appearance);
        if (!check_result("nk_system_get_appearance", nk_system_get_appearance(&appearance)) ||
            !check_result("nk_clipboard_set_text", nk_clipboard_set_text("iOS runtime"))) {
            success = false;
            break;
        }

        nk_resource clipboard_resource = {};
        clipboard_resource.struct_size = sizeof(clipboard_resource);
        clipboard_resource.flags = NK_RESOURCE_READABLE;
        clipboard_resource.uri = "https://example.com/nativekit-ios-resource";
        clipboard_resource.mime_type = "text/plain";
        clipboard_resource.display_name = "NativeKit iOS resource";
        nk_request_id clipboard_request = NK_INVALID_REQUEST_ID;
        if (!check_result("nk_clipboard_set_resources",
                          nk_clipboard_set_resources(&clipboard_resource, 1)) ||
            !check_result("nk_clipboard_read_resources",
                          nk_clipboard_read_resources(&clipboard_request)) ||
            !wait_for_resource_clipboard(clipboard_request, clipboard_resource.uri)) {
            success = false;
            break;
        }

        nk_share_options share = {};
        share.struct_size = sizeof(share);
        share.text = "NativeKit iOS share";
        if (!check_result("nk_share", nk_share(&share))) {
            success = false;
            break;
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
        [host_view.window.rootViewController.presentedViewController
            dismissViewControllerAnimated:NO
                               completion:nil];

        uint32_t joystick_count = 0;
        const nk_result joystick_query = nk_joystick_list(nullptr, &joystick_count);
        if (joystick_query != NK_OK && joystick_query != NK_ERROR_BUFFER_TOO_SMALL) {
            std::fprintf(stderr, "nk_joystick_list returned %d: %s\n", joystick_query,
                         nk_last_error());
            success = false;
            break;
        }
        if (joystick_count != 0) {
            std::vector<nk_joystick> joysticks(joystick_count);
            uint32_t capacity = joystick_count;
            if (!check_result("nk_joystick_list(values)",
                              nk_joystick_list(joysticks.data(), &capacity))) {
                success = false;
                break;
            }
        }
    } while (false);

    if (webview != NK_INVALID_HANDLE)
        success = check_result("nk_webview_destroy", nk_webview_destroy(webview)) && success;
    if (surface != NK_INVALID_HANDLE)
        success = check_result("nk_surface_destroy", nk_surface_destroy(surface)) && success;
    if (host != NK_INVALID_HANDLE)
        success = check_result("nk_mobile_host_destroy", nk_mobile_host_destroy(host)) && success;
    nk_shutdown();
    return success;
}

} // namespace

@interface NKRuntimeViewController : UIViewController
@end

@implementation NKRuntimeViewController
- (void)viewDidLoad {
    [super viewDidLoad];
    self.view.backgroundColor = UIColor.systemBackgroundColor;
}
@end

@interface NKRuntimeAppDelegate : UIResponder <UIApplicationDelegate>
@property(nonatomic, strong) UIWindow *window;
@end

@implementation NKRuntimeAppDelegate
- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions {
    (void)application;
    (void)launchOptions;
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [NKRuntimeViewController new];
    [self.window makeKeyAndVisible];
    dispatch_async(dispatch_get_main_queue(), ^{
      UIView *host_view = self.window.rootViewController.view;
      [host_view layoutIfNeeded];
      const bool success = run_nativekit_tests(host_view);
      std::fprintf(stdout, "NATIVEKIT_IOS_RUNTIME_RESULT=%s\n", success ? "PASS" : "FAIL");
      std::fflush(stdout);
      std::exit(success ? EXIT_SUCCESS : EXIT_FAILURE);
    });
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([NKRuntimeAppDelegate class]));
    }
}
