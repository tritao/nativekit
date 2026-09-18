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
#import <os/log.h>
#import <UIKit/UIKit.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <vector>

namespace {

void report_stage(const char *stage) {
    std::fprintf(stderr, "NATIVEKIT_IOS_RUNTIME_STAGE=%s\n", stage);
    std::fflush(stderr);
    os_log(OS_LOG_DEFAULT, "NATIVEKIT_IOS_RUNTIME_STAGE=%{public}s", stage);
}

NSString *runtime_result_path() {
    return [NSHomeDirectory()
        stringByAppendingPathComponent:@"Documents/nativekit-ios-runtime-result.txt"];
}

bool write_runtime_result(bool success) {
    NSError *error = nil;
    NSString *result = success ? @"PASS\n" : @"FAIL\n";
    if ([result writeToFile:runtime_result_path()
                 atomically:YES
                   encoding:NSUTF8StringEncoding
                      error:&error]) {
        return true;
    }
    std::fprintf(stderr, "Could not write iOS runtime result: %s\n",
                 error.localizedDescription.UTF8String);
    return false;
}

void clear_runtime_result() {
    NSError *error = nil;
    if (![[NSFileManager defaultManager] removeItemAtPath:runtime_result_path() error:&error] &&
        error.code != NSFileNoSuchFileError) {
        std::fprintf(stderr, "Could not clear iOS runtime result: %s\n",
                     error.localizedDescription.UTF8String);
    }
}

bool check_result(const char *operation, nk_result result) {
    if (result == NK_OK)
        return true;
    std::fprintf(stderr, "%s returned %d: %s\n", operation, result, nk_last_error());
    return false;
}

bool wait_for_text_edit(nk_surface surface, nk_text_edit_action action, nk_event *out_event) {
    if (!out_event)
        return false;
    for (int attempt = 0; attempt < 500; ++attempt) {
        nk_event event = {};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK)
            return false;
        if (event.kind == NK_EVENT_TEXT_EDIT && event.source == surface) {
            nk_text_edit_event edit = {};
            if (event.data_size < sizeof(edit)) {
                nk_event_release(&event);
                return false;
            }
            std::memcpy(&edit, event.data, sizeof(edit));
            if (edit.action == action) {
                *out_event = event;
                return true;
            }
        }
        const bool empty = event.kind == NK_EVENT_NONE;
        nk_event_release(&event);
        if (empty)
            usleep(10000);
    }
    return false;
}

bool verify_text_edit(const nk_event &event, nk_text_edit_action action,
                      nk_text_position replace_start, nk_text_position replace_end,
                      nk_text_position selection_start, nk_text_position selection_end,
                      nk_text_position composition_start, nk_text_position composition_end,
                      const char *expected_text, nk_text_edit_history_kind history_kind) {
    nk_text_edit_event edit = {};
    if (event.data_size < sizeof(edit))
        return false;
    std::memcpy(&edit, event.data, sizeof(edit));
    if (edit.action != action || edit.replace_start != replace_start ||
        edit.replace_end != replace_end || edit.selection_start != selection_start ||
        edit.selection_end != selection_end || edit.composition_start != composition_start ||
        edit.composition_end != composition_end || edit.history_kind != history_kind)
        return false;
    const char *text = nullptr;
    uint32_t text_length = 0;
    if (nk_text_edit_event_text(&event, &text, &text_length) != NK_OK)
        return false;
    const auto expected_length = static_cast<uint32_t>(std::strlen(expected_text));
    return text_length == expected_length &&
           (text_length == 0 || std::memcmp(text, expected_text, text_length) == 0);
}

UITextView *find_text_input_view(UIView *root) {
    if ([root conformsToProtocol:@protocol(UITextInput)])
        return (UITextView *)root;
    for (UIView *subview in root.subviews) {
        if (UITextView *result = find_text_input_view(subview))
            return result;
    }
    return nil;
}

bool run_text_input_contract(nk_surface surface, UIView *host_view) {
    UITextView *input = find_text_input_view(host_view);
    if (!input)
        return false;

    const char initial_text[] = "A\xF0\x9F\x98\x80\xE6\x97\xA5\xE6\x9C\AC";
    nk_text_input_state state = {};
    state.struct_size = sizeof(state);
    state.text = initial_text;
    state.document_length = 4;
    state.selection_start = 1;
    state.selection_end = 2;
    state.composition_start = NK_TEXT_POSITION_NONE;
    state.composition_end = NK_TEXT_POSITION_NONE;
    state.cursor_x = 32.0f;
    state.cursor_y = 48.0f;
    state.cursor_width = 1.0f;
    state.cursor_height = 18.0f;
    if (nk_surface_set_text_input_state(surface, &state) != NK_OK ||
        nk_surface_set_text_input_active(surface, 1) != NK_OK)
        return false;

    if (![input.text isEqualToString:@"A\U0001F600日本"] ||
        !NSEqualRanges(input.selectedRange, NSMakeRange(1, 2)))
        return false;
    UITextPosition *beginning = input.beginningOfDocument;
    UITextPosition *emoji_start = [input positionFromPosition:beginning offset:1];
    UITextPosition *emoji_end = [input positionFromPosition:beginning offset:3];
    UITextRange *emoji_range = [input textRangeFromPosition:emoji_start toPosition:emoji_end];
    if (!emoji_range || ![[input textInRange:emoji_range] isEqualToString:@"\U0001F600"])
        return false;

    [input setMarkedText:@"かな" selectedRange:NSMakeRange(2, 0)];
    nk_event compose = {};
    if (!wait_for_text_edit(surface, NK_TEXT_EDIT_COMPOSE, &compose) ||
        !verify_text_edit(compose, NK_TEXT_EDIT_COMPOSE, 1, 2, 2, 2, 1, 3,
                          "\xE3\x81\x8B\xE3\x81\xAA", NK_TEXT_EDIT_HISTORY_COMPOSITION)) {
        nk_event_release(&compose);
        return false;
    }
    nk_event_release(&compose);

    const char composed_text[] = "A\xE3\x81\x8B\xE3\x81\xAA\xE6\x97\xA5\xE6\x9C\xAC";
    state.text = composed_text;
    state.document_length = 5;
    state.selection_start = 3;
    state.selection_end = 3;
    state.composition_start = 1;
    state.composition_end = 3;
    if (nk_surface_set_text_input_state(surface, &state) != NK_OK ||
        !input.markedTextRange || !NSEqualRanges(input.selectedRange, NSMakeRange(3, 0)) ||
        ![[input textInRange:input.markedTextRange] isEqualToString:@"かな"])
        return false;
    beginning = input.beginningOfDocument;

    nk_text_input_range_rect composition_range = {
        sizeof(nk_text_input_range_rect), 80.0f, 100.0f, 40.0f, 18.0f, 1, 3};
    if (nk_surface_set_text_input_geometry(
            surface, 3, 3, 1, 3, nullptr, 0,
            reinterpret_cast<const uint8_t *>(&composition_range), sizeof(composition_range)) !=
        NK_OK)
        return false;
    const CGRect composition_rect = [input firstRectForRange:input.markedTextRange
                                                  actualRange:nullptr];
    if (composition_rect.origin.x != 80.0f || composition_rect.origin.y != 100.0f ||
        composition_rect.size.width != 40.0f || composition_rect.size.height != 18.0f)
        return false;
    UITextRange *hit_range = [input characterRangeAtPoint:CGPointMake(85.0f, 109.0f)];
    if (!hit_range || [input offsetFromPosition:beginning toPosition:hit_range.start] != 1 ||
        [input offsetFromPosition:beginning toPosition:hit_range.end] != 3)
        return false;

    [input setMarkedText:@"かなじ" selectedRange:NSMakeRange(3, 0)];
    nk_event compose_update = {};
    if (!wait_for_text_edit(surface, NK_TEXT_EDIT_COMPOSE, &compose_update) ||
        !verify_text_edit(compose_update, NK_TEXT_EDIT_COMPOSE, 1, 3, 4, 4, 1, 4,
                          "\xE3\x81\x8B\xE3\x81\xAA\xE3\x81\x98",
                          NK_TEXT_EDIT_HISTORY_COMPOSITION)) {
        nk_event_release(&compose_update);
        return false;
    }
    nk_event_release(&compose_update);

    const char updated_text[] =
        "A\xE3\x81\x8B\xE3\x81\xAA\xE3\x81\x98\xE6\x97\xA5\xE6\x9C\xAC";
    state.text = updated_text;
    state.document_length = 6;
    state.selection_start = 4;
    state.selection_end = 4;
    state.composition_start = 1;
    state.composition_end = 4;
    if (nk_surface_set_text_input_state(surface, &state) != NK_OK)
        return false;
    UITextRange *updated_marked = input.markedTextRange;
    beginning = input.beginningOfDocument;
    if (!updated_marked ||
        [input offsetFromPosition:beginning toPosition:updated_marked.start] != 1 ||
        [input offsetFromPosition:beginning toPosition:updated_marked.end] != 4 ||
        !NSEqualRanges(input.selectedRange, NSMakeRange(4, 0)))
        return false;

    [input unmarkText];
    nk_event cancel = {};
    if (!wait_for_text_edit(surface, NK_TEXT_EDIT_FINISH_COMPOSITION, &cancel) ||
        !verify_text_edit(cancel, NK_TEXT_EDIT_FINISH_COMPOSITION, NK_TEXT_POSITION_NONE,
                          NK_TEXT_POSITION_NONE, 4, 4, NK_TEXT_POSITION_NONE,
                          NK_TEXT_POSITION_NONE, "", NK_TEXT_EDIT_HISTORY_GENERIC)) {
        nk_event_release(&cancel);
        return false;
    }
    nk_event_release(&cancel);
    if (input.markedTextRange)
        return false;

    state.composition_start = NK_TEXT_POSITION_NONE;
    state.composition_end = NK_TEXT_POSITION_NONE;
    if (nk_surface_set_text_input_state(surface, &state) != NK_OK)
        return false;
    beginning = input.beginningOfDocument;
    UITextPosition *last_start = [input positionFromPosition:beginning offset:5];
    UITextPosition *last_end = [input positionFromPosition:beginning offset:6];
    UITextRange *last_range = [input textRangeFromPosition:last_start toPosition:last_end];
    [input replaceRange:last_range withText:@"終"];
    nk_event commit = {};
    if (!wait_for_text_edit(surface, NK_TEXT_EDIT_COMMIT, &commit) ||
        !verify_text_edit(commit, NK_TEXT_EDIT_COMMIT, 5, 6, 6, 6, NK_TEXT_POSITION_NONE,
                          NK_TEXT_POSITION_NONE, "\xE7\xB5\x82", NK_TEXT_EDIT_HISTORY_GENERIC)) {
        nk_event_release(&commit);
        return false;
    }
    nk_event_release(&commit);

    beginning = input.beginningOfDocument;
    UITextPosition *selection_position = [input positionFromPosition:beginning offset:1];
    [input setSelectedTextRange:[input textRangeFromPosition:selection_position
                                                     toPosition:selection_position]];
    nk_event selection = {};
    if (!wait_for_text_edit(surface, NK_TEXT_EDIT_SET_SELECTION, &selection) ||
        !verify_text_edit(selection, NK_TEXT_EDIT_SET_SELECTION, NK_TEXT_POSITION_NONE,
                          NK_TEXT_POSITION_NONE, 1, 1, NK_TEXT_POSITION_NONE,
                          NK_TEXT_POSITION_NONE, "", NK_TEXT_EDIT_HISTORY_GENERIC)) {
        nk_event_release(&selection);
        return false;
    }
    nk_event_release(&selection);

    [input deleteBackward];
    nk_event deletion = {};
    if (!wait_for_text_edit(surface, NK_TEXT_EDIT_DELETE, &deletion) ||
        !verify_text_edit(deletion, NK_TEXT_EDIT_DELETE, 0, 1, 0, 0, NK_TEXT_POSITION_NONE,
                          NK_TEXT_POSITION_NONE, "", NK_TEXT_EDIT_HISTORY_DELETE_BACKWARD)) {
        nk_event_release(&deletion);
        return false;
    }
    nk_event_release(&deletion);
    return nk_surface_set_text_input_active(surface, 0) == NK_OK;
}

bool check_capabilities(nk_capabilities capabilities) {
    const nk_capabilities expected =
        NK_CAP_MOBILE_HOST | NK_CAP_WEBVIEW | NK_CAP_METAL_SURFACE | NK_CAP_INPUT |
        NK_CAP_RESOURCE_IO | NK_CAP_CLIPBOARD | NK_CAP_SHELL | NK_CAP_SYSTEM_APPEARANCE |
        NK_CAP_NOTIFICATION | NK_CAP_ACCESSIBILITY | NK_CAP_DRAG_DROP | NK_CAP_RESOURCE_SHARING |
        NK_CAP_JOYSTICK | NK_CAP_SYSTEM_INFO | NK_CAP_APPLICATION_PATH |
        NK_CAP_APPLICATION_STORAGE | NK_CAP_KEEP_AWAKE | NK_CAP_DEVICE_ORIENTATION |
        NK_CAP_DISPLAY_ORIENTATION | NK_CAP_SURFACE_FRAME_CALLBACK | NK_CAP_HTTP_CLIENT |
        NK_CAP_HTTP_STREAMING;
    if (capabilities == expected)
        return true;
    std::fprintf(stderr, "iOS runtime capability mask changed: expected 0x%llx, got 0x%llx\n",
                 static_cast<unsigned long long>(expected),
                 static_cast<unsigned long long>(capabilities));
    return false;
}

bool check_directory(nk_system_directory_kind kind, const char *name) {
    uint32_t size = 0;
    if (nk_system_directory(kind, nullptr, &size) != NK_ERROR_BUFFER_TOO_SMALL || size <= 1) {
        std::fprintf(stderr, "%s did not provide a path size\n", name);
        return false;
    }
    std::vector<char> path(size);
    if (nk_system_directory(kind, path.data(), &size) != NK_OK || path[0] == '\0') {
        std::fprintf(stderr, "%s did not provide a path\n", name);
        return false;
    }
    return true;
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
    report_stage("finish.begin");
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
    success = write_runtime_result(success) && success;
    report_stage(success ? "finish.pass" : "finish.fail");
    std::fprintf(stdout, "NATIVEKIT_IOS_RUNTIME_RESULT=%s\n", success ? "PASS" : "FAIL");
    std::fflush(stdout);
    std::exit(success ? EXIT_SUCCESS : EXIT_FAILURE);
}

- (void)fail {
    [self finish:false];
}

- (void)beginWebView {
    report_stage("webview.create.begin");
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
    report_stage("webview.create.complete");
    _stage = NKRuntimeStage::wait_html_navigation;
}

- (void)beginEvaluation {
    report_stage("webview.evaluation.begin");
    if (!check_result("nk_webview_eval", nk_webview_eval(_webview, "1 + 1", &_evaluation))) {
        [self fail];
        return;
    }
    report_stage("webview.evaluation.complete");
    _stage = NKRuntimeStage::wait_evaluation;
}

- (void)beginServices {
    report_stage("services.begin");
    if (!check_directory(NK_DIRECTORY_APPLICATION, "iOS application directory") ||
        !check_directory(NK_DIRECTORY_APPLICATION_STORAGE, "iOS application storage")) {
        [self fail];
        return;
    }
    uint32_t font_size = 0;
    if (nk_system_directory(NK_DIRECTORY_FONTS, nullptr, &font_size) != NK_ERROR_UNSUPPORTED) {
        std::fprintf(stderr, "iOS system font directory should be unavailable\n");
        [self fail];
        return;
    }
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
    report_stage("services.clipboard.complete");
    _stage = NKRuntimeStage::wait_clipboard;
}

- (void)finishShare {
    report_stage("share.complete");
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
        if (_frameState.surface != _surface || _frameState.width <= 0 || _frameState.height <= 0) {
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
        report_stage("surface.first_frame");
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
            report_stage("webview.html_navigated");
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
            report_stage("webview.url_navigated");
            nk_event_release(&event);
            [self beginEvaluation];
            return;
        }
        if (_stage == NKRuntimeStage::wait_evaluation &&
            event.kind == NK_EVENT_WEBVIEW_EVAL_COMPLETE && event.request_id == _evaluation) {
            report_stage("webview.evaluation.event");
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
            report_stage("services.clipboard.event");
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
    report_stage("runner.start");
    nk_init_options init = {};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    report_stage("nk_init.begin");
    if (!check_result("nk_init", nk_init(&init))) {
        [self fail];
        return;
    }
    _initialized = true;
    report_stage("nk_init.complete");
    if (!check_capabilities(nk_get_capabilities())) {
        [self fail];
        return;
    }

    nk_mobile_host_options host_options = {};
    host_options.struct_size = sizeof(host_options);
    host_options.kind = NK_MOBILE_HOST_UIKIT_VIEW;
    host_options.native_view = reinterpret_cast<uintptr_t>((__bridge void *)_hostView);
    report_stage("mobile_host.attach.begin");
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
    report_stage("mobile_host.attach.complete");

    nk_surface_options surface_options = {};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.flags = NK_SURFACE_DEPTH | NK_SURFACE_STENCIL;
    surface_options.api = NK_GRAPHICS_METAL;
    surface_options.width = 320;
    surface_options.height = 240;
    report_stage("surface.create.begin");
    if (!check_result("nk_surface_create", nk_surface_create(_host, &surface_options, &_surface)) ||
        !check_result("nk_surface_show", nk_surface_show(_surface, 1)) ||
        !check_result("nk_surface_set_bounds", nk_surface_set_bounds(_surface, 0, 0, 320, 240)) ||
        !check_result("nk_surface_make_current", nk_surface_make_current(_surface))) {
        [self fail];
        return;
    }
    report_stage("surface.create.complete");
    nk_surface_frame_target target = {};
    target.struct_size = sizeof(target);
    report_stage("surface.frame_target.begin");
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
    report_stage("surface.frame_target.complete");

    report_stage("text_input.begin");
    if (!run_text_input_contract(_surface, _hostView)) {
        std::fprintf(stderr, "iOS text input contract failed: %s\n", nk_last_error());
        [self fail];
        return;
    }
    report_stage("text_input.complete");

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
    report_stage("accessibility.begin");
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
    report_stage("accessibility.complete");
    _timer = [NSTimer scheduledTimerWithTimeInterval:0.01
                                              target:self
                                            selector:@selector(poll:)
                                            userInfo:nil
                                             repeats:YES];
    report_stage("runner.polling");
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
    report_stage("application.did_finish.begin");
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    self.window.rootViewController = [NKRuntimeViewController new];
    [self.window makeKeyAndVisible];
    dispatch_async(dispatch_get_main_queue(), ^{
      UIView *host_view = self.window.rootViewController.view;
      [host_view layoutIfNeeded];
      self.runner = [[NKRuntimeTestRunner alloc] initWithHostView:host_view];
      [self.runner start];
      report_stage("application.runner.started");
    });
    report_stage("application.did_finish.complete");
    return YES;
}
@end

int main(int argc, char *argv[]) {
    @autoreleasepool {
        clear_runtime_result();
        report_stage("main");
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([NKRuntimeAppDelegate class]));
    }
}
