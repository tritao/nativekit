#include "nativekit.h"
#include "nativekit_accessibility.h"
#include "nativekit_clipboard.h"
#include "nativekit_graphics.h"
#include "nativekit_input.h"
#include "nativekit_joystick.h"
#include "nativekit_mobile.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_webview.h"
#include "nativekit_window.h"

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
    const nk_capabilities expected = NK_CAP_MOBILE_HOST | NK_CAP_WEBVIEW | NK_CAP_METAL_SURFACE |
                                     NK_CAP_INPUT | NK_CAP_RESOURCE_IO | NK_CAP_CLIPBOARD |
                                     NK_CAP_SHELL | NK_CAP_SYSTEM_APPEARANCE | NK_CAP_NOTIFICATION |
                                     NK_CAP_ACCESSIBILITY | NK_CAP_DRAG_DROP |
                                     NK_CAP_RESOURCE_SHARING | NK_CAP_JOYSTICK | NK_CAP_SYSTEM_INFO |
                                     NK_CAP_APPLICATION_PATH | NK_CAP_APPLICATION_STORAGE |
                                     NK_CAP_KEEP_AWAKE | NK_CAP_DEVICE_ORIENTATION |
                                     NK_CAP_DISPLAY_ORIENTATION | NK_CAP_SURFACE_FRAME_CALLBACK;
    if (capabilities == expected)
        return true;
    std::fprintf(stderr, "iOS runtime capability mask changed: expected 0x%llx, got 0x%llx\n",
                 static_cast<unsigned long long>(expected),
                 static_cast<unsigned long long>(capabilities));
    return false;
}

struct NKIOSFrameState {
    nk_surface surface = NK_INVALID_HANDLE;
    int count = 0;
    int32_t width = 0;
    int32_t height = 0;
};

void NK_CALL on_frame(nk_surface surface, int32_t width, int32_t height, void *user_data) {
    auto *state = static_cast<NKIOSFrameState *>(user_data);
    if (!state)
        return;
    state->surface = surface;
    state->width = width;
    state->height = height;
    ++state->count;
}

} // namespace

enum class NKRuntimeStage {
    wait_frame,
    verify_frame_stop,
    wait_html_navigation,
    wait_url_navigation,
    wait_evaluation,
    wait_clipboard,
    dismiss_share
};

@interface NKRuntimeTestRunner : NSObject {
    __strong UIView *_hostView;
    __strong NSTimer *_timer;
    nk_mobile_host _host;
    nk_surface _surface;
    nk_webview _webview;
    nk_request_id _evaluation;
    nk_request_id _clipboardRequest;
    NKRuntimeStage _stage;
    NKIOSFrameState _frameState;
    int _frameCountAtStop;
    int _frameStopTicks;
    int _shareTicks;
    bool _initialized;
    bool _finished;
}
- (instancetype)initWithHostView:(UIView *)hostView;
- (void)start;
- (void)poll:(NSTimer *)timer;
@end

@implementation NKRuntimeTestRunner

- (instancetype)initWithHostView:(UIView *)hostView {
    self = [super init];
    if (self) {
        _hostView = hostView;
        _host = NK_INVALID_HANDLE;
        _surface = NK_INVALID_HANDLE;
        _webview = NK_INVALID_HANDLE;
        _evaluation = NK_INVALID_REQUEST_ID;
        _clipboardRequest = NK_INVALID_REQUEST_ID;
        _stage = NKRuntimeStage::wait_frame;
        _frameState = {};
        _frameCountAtStop = 0;
        _frameStopTicks = 0;
        _shareTicks = 0;
        _initialized = false;
        _finished = false;
    }
    return self;
}

- (void)finish:(bool)success {
    if (_finished)
        return;
    _finished = true;
    [_timer invalidate];
    _timer = nil;
    if (_webview != NK_INVALID_HANDLE)
        success = check_result("nk_webview_destroy", nk_webview_destroy(_webview)) && success;
    if (_surface != NK_INVALID_HANDLE)
        success = check_result("nk_surface_destroy", nk_surface_destroy(_surface)) && success;
    if (_host != NK_INVALID_HANDLE)
        success = check_result("nk_mobile_host_destroy", nk_mobile_host_destroy(_host)) && success;
    if (_initialized)
        nk_shutdown();
    std::fprintf(stdout, "NATIVEKIT_IOS_RUNTIME_RESULT=%s\n", success ? "PASS" : "FAIL");
    std::fflush(stdout);
    std::exit(success ? EXIT_SUCCESS : EXIT_FAILURE);
}

- (void)fail {
    [self finish:false];
}

- (void)beginWebView {
    nk_webview_options options = {};
    options.struct_size = sizeof(options);
    options.flags = NK_WEBVIEW_HIDDEN;
    options.width = 320;
    options.height = 240;
    if (!check_result("nk_webview_create", nk_webview_create(_host, &options, &_webview)) ||
        !check_result("nk_webview_set_bounds", nk_webview_set_bounds(_webview, 0, 0, 320, 240)) ||
        !check_result("nk_webview_show", nk_webview_show(_webview, 1)) ||
        !check_result("nk_webview_set_html",
                      nk_webview_set_html(
                          _webview,
                          "<html><head><title>runtime</title></head><body>ready</body></html>",
                          "https://nativekit.invalid/"))) {
        [self fail];
        return;
    }
    _stage = NKRuntimeStage::wait_html_navigation;
}

- (void)beginEvaluation {
    if (!check_result("nk_webview_eval", nk_webview_eval(_webview, "1 + 1", &_evaluation))) {
        [self fail];
        return;
    }
    _stage = NKRuntimeStage::wait_evaluation;
}

- (void)beginServices {
    nk_system_appearance appearance = {};
    appearance.struct_size = sizeof(appearance);
    if (!check_result("nk_system_get_appearance", nk_system_get_appearance(&appearance)) ||
        !check_result("nk_clipboard_set_text", nk_clipboard_set_text("iOS runtime"))) {
        [self fail];
        return;
    }

    nk_resource resource = {};
    resource.struct_size = sizeof(resource);
    resource.flags = NK_RESOURCE_READABLE;
    resource.uri = "https://example.com/nativekit-ios-resource";
    resource.mime_type = "text/plain";
    resource.display_name = "NativeKit iOS resource";
    if (!check_result("nk_clipboard_set_resources", nk_clipboard_set_resources(&resource, 1)) ||
        !check_result("nk_clipboard_read_resources",
                      nk_clipboard_read_resources(&_clipboardRequest))) {
        [self fail];
        return;
    }
    _stage = NKRuntimeStage::wait_clipboard;
}

- (void)finishShare {
    UIViewController *presented = _hostView.window.rootViewController.presentedViewController;
    [presented dismissViewControllerAnimated:NO completion:nil];
    uint32_t joystick_count = 0;
    const nk_result joystick_query = nk_joystick_list(nullptr, &joystick_count);
    if (joystick_query != NK_OK && joystick_query != NK_ERROR_BUFFER_TOO_SMALL) {
        std::fprintf(stderr, "nk_joystick_list returned %d: %s\n", joystick_query, nk_last_error());
        [self fail];
        return;
    }
    if (joystick_count != 0) {
        std::vector<nk_joystick> joysticks(joystick_count);
        uint32_t capacity = joystick_count;
        if (!check_result("nk_joystick_list(values)",
                          nk_joystick_list(joysticks.data(), &capacity))) {
            [self fail];
            return;
        }
    }
    [self finish:true];
}

- (void)poll:(NSTimer *)timer {
    (void)timer;
    if (_finished)
        return;
    if (_stage == NKRuntimeStage::wait_frame) {
        if (_frameState.count == 0)
            return;
        if (_frameState.surface != _surface || _frameState.width <= 0 ||
            _frameState.height <= 0) {
            std::fprintf(stderr, "iOS frame callback returned an invalid frame\n");
            [self fail];
            return;
        }
        if (!check_result("nk_surface_set_frame_callback(stop)",
                          nk_surface_set_frame_callback(_surface, nullptr, nullptr))) {
            [self fail];
            return;
        }
        _frameCountAtStop = _frameState.count;
        _frameStopTicks = 0;
        _stage = NKRuntimeStage::verify_frame_stop;
        return;
    }
    if (_stage == NKRuntimeStage::verify_frame_stop) {
        if (_frameState.count != _frameCountAtStop) {
            std::fprintf(stderr, "iOS frame callback continued after being disabled\n");
            [self fail];
            return;
        }
        if (++_frameStopTicks < 5)
            return;
        [self beginWebView];
        return;
    }
    if (_stage == NKRuntimeStage::dismiss_share) {
        if (++_shareTicks >= 5)
            [self finishShare];
        return;
    }

    for (;;) {
        nk_event event = {};
        event.struct_size = sizeof(event);
        if (!check_result("nk_poll_event", nk_poll_event(&event))) {
            nk_event_release(&event);
            [self fail];
            return;
        }
        if (event.kind == NK_EVENT_NONE) {
            nk_event_release(&event);
            return;
        }
        if ((_stage == NKRuntimeStage::wait_html_navigation ||
             _stage == NKRuntimeStage::wait_url_navigation) &&
            event.source == _webview && event.kind == NK_EVENT_WEBVIEW_NAVIGATION_FAILED) {
            std::fprintf(stderr, "iOS WebView navigation failed\n");
            nk_event_release(&event);
            [self fail];
            return;
        }
        if (_stage == NKRuntimeStage::wait_html_navigation && event.source == _webview &&
            event.kind == NK_EVENT_WEBVIEW_NAVIGATED) {
            const nk_result result = nk_webview_navigate(_webview, "about:blank");
            nk_event_release(&event);
            if (!check_result("nk_webview_navigate", result)) {
                [self fail];
                return;
            }
            _stage = NKRuntimeStage::wait_url_navigation;
            continue;
        }
        if (_stage == NKRuntimeStage::wait_url_navigation && event.source == _webview &&
            event.kind == NK_EVENT_WEBVIEW_NAVIGATED) {
            nk_event_release(&event);
            [self beginEvaluation];
            return;
        }
        if (_stage == NKRuntimeStage::wait_evaluation &&
            event.kind == NK_EVENT_WEBVIEW_EVAL_COMPLETE && event.request_id == _evaluation) {
            const bool valid = event.result == NK_OK && event.data_size == 1 && event.data &&
                               std::memcmp(event.data, "2", 1) == 0;
            if (!valid)
                std::fprintf(stderr, "iOS WebView evaluation returned an unexpected result\n");
            nk_event_release(&event);
            if (!valid) {
                [self fail];
                return;
            }
            [self beginServices];
            return;
        }
        if (_stage == NKRuntimeStage::wait_clipboard &&
            event.kind == NK_EVENT_CLIPBOARD_RESOURCES_COMPLETE &&
            event.request_id == _clipboardRequest) {
            nk_resource_view resource = {};
            resource.struct_size = sizeof(resource);
            const bool valid =
                event.result == NK_OK && event.data_count == 1 &&
                nk_resource_event_item(&event, 0, &resource) == NK_OK && resource.uri &&
                std::strcmp(resource.uri, "https://example.com/nativekit-ios-resource") == 0 &&
                (resource.flags & NK_RESOURCE_READABLE) != 0;
            if (!valid)
                std::fprintf(stderr, "iOS resource clipboard returned an unexpected result\n");
            nk_event_release(&event);
            if (!valid) {
                [self fail];
                return;
            }
            nk_share_options share = {};
            share.struct_size = sizeof(share);
            share.text = "NativeKit iOS share";
            if (!check_result("nk_share", nk_share(&share))) {
                [self fail];
                return;
            }
            _shareTicks = 0;
            _stage = NKRuntimeStage::dismiss_share;
            return;
        }
        nk_event_release(&event);
    }
}

- (void)start {
    nk_init_options init = {};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (!check_result("nk_init", nk_init(&init))) {
        [self fail];
        return;
    }
    _initialized = true;
    if (!check_capabilities(nk_get_capabilities())) {
        [self fail];
        return;
    }

    nk_mobile_host_options host_options = {};
    host_options.struct_size = sizeof(host_options);
    host_options.kind = NK_MOBILE_HOST_UIKIT_VIEW;
    host_options.native_view = reinterpret_cast<uintptr_t>((__bridge void *)_hostView);
    if (!check_result("nk_mobile_host_attach", nk_mobile_host_attach(&host_options, &_host)) ||
        !check_result("nk_mobile_host_set_drop_enabled(true)",
                      nk_mobile_host_set_drop_enabled(_host, 1)) ||
        !check_result("nk_mobile_host_set_drop_enabled(false)",
                      nk_mobile_host_set_drop_enabled(_host, 0)) ||
        !check_result("nk_mobile_host_set_lifecycle(active)",
                      nk_mobile_host_set_lifecycle(_host, NK_MOBILE_LIFECYCLE_ACTIVE))) {
        [self fail];
        return;
    }

    nk_surface_options surface_options = {};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.flags = NK_SURFACE_DEPTH | NK_SURFACE_STENCIL;
    surface_options.api = NK_GRAPHICS_METAL;
    surface_options.width = 320;
    surface_options.height = 240;
    if (!check_result("nk_surface_create", nk_surface_create(_host, &surface_options, &_surface)) ||
        !check_result("nk_surface_show", nk_surface_show(_surface, 1)) ||
        !check_result("nk_surface_set_bounds", nk_surface_set_bounds(_surface, 0, 0, 320, 240)) ||
        !check_result("nk_surface_make_current", nk_surface_make_current(_surface))) {
        [self fail];
        return;
    }
    nk_surface_frame_target target = {};
    target.struct_size = sizeof(target);
    if (!check_result("nk_surface_get_frame_target",
                      nk_surface_get_frame_target(_surface, &target)) ||
        target.api != NK_GRAPHICS_METAL || target.width <= 0 || target.height <= 0 ||
        target.device.id == 0 || target.native_target == 0 || target.native_device == 0 ||
        target.native_context == 0 || target.native_present_target == 0 ||
        !check_result("nk_surface_present", nk_surface_present(_surface)) ||
        !check_result("nk_surface_set_frame_callback",
                      nk_surface_set_frame_callback(_surface, on_frame, &_frameState))) {
        [self fail];
        return;
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
                      nk_surface_set_text_input_state(_surface, &text_state)) ||
        !check_result("nk_surface_set_text_input_active(true)",
                      nk_surface_set_text_input_active(_surface, 1)) ||
        !check_result("nk_surface_set_text_input_active(false)",
                      nk_surface_set_text_input_active(_surface, 0))) {
        [self fail];
        return;
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
                      nk_surface_accessibility_set_node(_surface, &node)) ||
        !check_result("nk_surface_accessibility_set_focus",
                      nk_surface_accessibility_set_focus(_surface, node.id)) ||
        !check_result("nk_surface_accessibility_set_text_ranges",
                      nk_surface_accessibility_set_text_ranges(_surface, node.id, &range, 1)) ||
        !check_result("nk_surface_accessibility_clear", nk_surface_accessibility_clear(_surface))) {
        [self fail];
        return;
    }
    _timer = [NSTimer scheduledTimerWithTimeInterval:0.01
                                              target:self
                                            selector:@selector(poll:)
                                            userInfo:nil
                                             repeats:YES];
}
@end

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
@property(nonatomic, strong) NKRuntimeTestRunner *runner;
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
      self.runner = [[NKRuntimeTestRunner alloc] initWithHostView:host_view];
      [self.runner start];
    });
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([NKRuntimeAppDelegate class]));
    }
}
