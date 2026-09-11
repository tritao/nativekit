#include "platform/web/host.h"

#include "nativekit_web_config.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

#include <algorithm>
#include <cmath>

#ifndef NK_WEB_CANVAS_SELECTOR
#define NK_WEB_CANVAS_SELECTOR "#canvas"
#endif

namespace {

struct HostState {
    nk::web::HostCallbacks callbacks{};
    void *user_data = nullptr;
    bool installed = false;
};

HostState host_state;
nk::web::FrameCallback frame_callback = nullptr;
void *frame_user_data = nullptr;
bool frame_loop_active = false;
int32_t requested_width = 300;
int32_t requested_height = 150;

uint32_t modifiers(const EmscriptenKeyboardEvent &event) {
    uint32_t result = 0;
    if (event.shiftKey)
        result |= 1u << 0;
    if (event.ctrlKey)
        result |= 1u << 1;
    if (event.altKey)
        result |= 1u << 2;
    if (event.metaKey)
        result |= 1u << 3;
    return result;
}

uint32_t modifiers(const EmscriptenMouseEvent &event) {
    uint32_t result = 0;
    if (event.shiftKey)
        result |= 1u << 0;
    if (event.ctrlKey)
        result |= 1u << 1;
    if (event.altKey)
        result |= 1u << 2;
    if (event.metaKey)
        result |= 1u << 3;
    return result;
}

uint32_t modifiers(const EmscriptenWheelEvent &event) {
    uint32_t result = 0;
    if (event.mouse.shiftKey)
        result |= 1u << 0;
    if (event.mouse.ctrlKey)
        result |= 1u << 1;
    if (event.mouse.altKey)
        result |= 1u << 2;
    if (event.mouse.metaKey)
        result |= 1u << 3;
    return result;
}

uint32_t modifiers(const EmscriptenTouchEvent &event) {
    uint32_t result = 0;
    if (event.shiftKey)
        result |= 1u << 0;
    if (event.ctrlKey)
        result |= 1u << 1;
    if (event.altKey)
        result |= 1u << 2;
    if (event.metaKey)
        result |= 1u << 3;
    return result;
}

EM_BOOL resize_callback(int, const EmscriptenUiEvent *, void *) {
    nk::web::CanvasSize size{};
    if (nk::web::canvas_size(&size) && host_state.callbacks.resize)
        host_state.callbacks.resize(size, host_state.user_data);
    return EM_TRUE;
}

EM_BOOL key_callback(int event_type, const EmscriptenKeyboardEvent *event, void *) {
    if (!event || !host_state.callbacks.key)
        return EM_FALSE;
    nk::web::KeyEvent key{};
    key.type = event_type == EMSCRIPTEN_EVENT_KEYPRESS
                   ? nk::web::KeyEventType::character
                   : event_type == EMSCRIPTEN_EVENT_KEYUP ? nk::web::KeyEventType::up
                                                          : nk::web::KeyEventType::down;
    key.key_code = event->keyCode;
    key.location = event->location;
    key.char_code = event->charCode;
    key.modifiers = modifiers(*event);
    key.repeat = event->repeat != 0;
    host_state.callbacks.key(key, host_state.user_data);
    return EM_TRUE;
}

EM_BOOL mouse_callback(int event_type, const EmscriptenMouseEvent *event, void *) {
    if (!event || !host_state.callbacks.pointer)
        return EM_FALSE;
    nk::web::PointerEvent pointer{};
    pointer.type = event_type == EMSCRIPTEN_EVENT_MOUSEDOWN
                       ? nk::web::PointerEventType::down
                       : event_type == EMSCRIPTEN_EVENT_MOUSEUP
                             ? nk::web::PointerEventType::up
                             : event_type == EMSCRIPTEN_EVENT_MOUSEENTER
                                   ? nk::web::PointerEventType::enter
                                   : event_type == EMSCRIPTEN_EVENT_MOUSELEAVE
                                         ? nk::web::PointerEventType::leave
                                         : nk::web::PointerEventType::move;
    pointer.button = event->button;
    pointer.buttons = event->buttons;
    pointer.modifiers = modifiers(*event);
    pointer.x = event->targetX;
    pointer.y = event->targetY;
    host_state.callbacks.pointer(pointer, host_state.user_data);
    return EM_TRUE;
}

EM_BOOL wheel_callback(int, const EmscriptenWheelEvent *event, void *) {
    if (!event || !host_state.callbacks.pointer)
        return EM_FALSE;
    nk::web::PointerEvent pointer{};
    pointer.type = nk::web::PointerEventType::wheel;
    pointer.modifiers = modifiers(*event);
    pointer.wheel_x = event->deltaX;
    pointer.wheel_y = event->deltaY;
    if (event->deltaMode == 1) {
        pointer.wheel_x *= 16.0;
        pointer.wheel_y *= 16.0;
    } else if (event->deltaMode == 2) {
        pointer.wheel_x *= 100.0;
        pointer.wheel_y *= 100.0;
    }
    host_state.callbacks.pointer(pointer, host_state.user_data);
    return EM_TRUE;
}

EM_BOOL touch_callback(int event_type, const EmscriptenTouchEvent *event, void *) {
    if (!event || !host_state.callbacks.touch)
        return EM_FALSE;
    const auto type = event_type == EMSCRIPTEN_EVENT_TOUCHSTART
                          ? nk::web::TouchEventType::begin
                          : event_type == EMSCRIPTEN_EVENT_TOUCHEND
                                ? nk::web::TouchEventType::end
                                : event_type == EMSCRIPTEN_EVENT_TOUCHCANCEL
                                      ? nk::web::TouchEventType::cancel
                                      : nk::web::TouchEventType::move;
    for (int index = 0; index < event->numTouches; ++index) {
        const auto &point = event->touches[index];
        if (!point.isChanged || !point.onTarget)
            continue;
        nk::web::TouchEvent touch{};
        touch.type = type;
        touch.identifier = static_cast<uint32_t>(point.identifier);
        touch.x = point.targetX;
        touch.y = point.targetY;
        touch.modifiers = modifiers(*event);
        host_state.callbacks.touch(touch, host_state.user_data);
    }
    return EM_TRUE;
}

EM_BOOL focus_callback(int event_type, const EmscriptenFocusEvent *, void *) {
    if (host_state.callbacks.focus)
        host_state.callbacks.focus(event_type == EMSCRIPTEN_EVENT_FOCUS,
                                   host_state.user_data);
    return EM_TRUE;
}

EM_BOOL context_callback(int event_type, const void *, void *) {
    if (host_state.callbacks.context)
        host_state.callbacks.context(event_type == EMSCRIPTEN_EVENT_WEBGLCONTEXTRESTORED,
                                     host_state.user_data);
    return EM_TRUE;
}

bool pointer_lock_callback(int, const EmscriptenPointerlockChangeEvent *event, void *) {
    if (host_state.callbacks.pointer_lock)
        host_state.callbacks.pointer_lock(event && event->isActive, host_state.user_data);
    return true;
}

EM_BOOL frame_callback_adapter(double time, void *) {
    if (!frame_loop_active || !frame_callback)
        return EM_FALSE;
    return frame_callback(time, frame_user_data);
}

} // namespace

EM_JS(void, nk_web_set_canvas_css_size, (const char *selector, int width, int height), {
    const canvas = document.querySelector(UTF8ToString(selector));
    if (canvas) {
        canvas.style.width = width + "px";
        canvas.style.height = height + "px";
    }
});

EM_JS(void, nk_web_set_canvas_visible, (const char *selector, int visible), {
    const canvas = document.querySelector(UTF8ToString(selector));
    if (canvas)
        canvas.style.visibility = visible ? "visible" : "hidden";
});

EM_JS(void, nk_web_set_document_title, (const char *title), {
    document.title = UTF8ToString(title);
});

EM_JS(void, nk_web_set_canvas_cursor, (const char *selector, const char *cursor), {
    const canvas = document.querySelector(UTF8ToString(selector));
    if (canvas)
        canvas.style.cursor = UTF8ToString(cursor);
});

namespace nk::web {

const char *canvas_selector() noexcept {
    return NK_WEB_CANVAS_SELECTOR;
}

bool canvas_size(CanvasSize *out_size) noexcept {
    if (!out_size)
        return false;
    double width = 0.0;
    double height = 0.0;
    if (emscripten_get_element_css_size(canvas_selector(), &width, &height) !=
            EMSCRIPTEN_RESULT_SUCCESS ||
        width < 1.0 || height < 1.0) {
        width = requested_width;
        height = requested_height;
    }
    const double scale = std::max(1.0, emscripten_get_device_pixel_ratio());
    out_size->width = std::max(1, static_cast<int32_t>(std::lround(width)));
    out_size->height = std::max(1, static_cast<int32_t>(std::lround(height)));
    out_size->framebuffer_width =
        std::max(1, static_cast<int32_t>(std::lround(width * scale)));
    out_size->framebuffer_height =
        std::max(1, static_cast<int32_t>(std::lround(height * scale)));
    out_size->scale = static_cast<float>(scale);
    return true;
}

bool set_canvas_size(int32_t width, int32_t height) noexcept {
    if (width <= 0 || height <= 0)
        return false;
    requested_width = width;
    requested_height = height;
    nk_web_set_canvas_css_size(canvas_selector(), width, height);
    CanvasSize size{};
    if (!canvas_size(&size))
        return false;
    return set_canvas_framebuffer_size(size);
}

bool set_canvas_framebuffer_size(const CanvasSize &size) noexcept {
    return emscripten_set_canvas_element_size(canvas_selector(), size.framebuffer_width,
                                              size.framebuffer_height) ==
           EMSCRIPTEN_RESULT_SUCCESS;
}

bool set_canvas_visible(bool visible) noexcept {
    nk_web_set_canvas_visible(canvas_selector(), visible ? 1 : 0);
    return true;
}

bool set_title(const char *title) noexcept {
    if (!title)
        return false;
    nk_web_set_document_title(title);
    return true;
}

bool set_cursor(const char *cursor) noexcept {
    if (!cursor)
        return false;
    nk_web_set_canvas_cursor(canvas_selector(), cursor);
    return true;
}

bool create_webgl_context(const WebGLContextOptions &options,
                          EMSCRIPTEN_WEBGL_CONTEXT_HANDLE *out_context) noexcept {
    if (!out_context)
        return false;
    EmscriptenWebGLContextAttributes attributes{};
    emscripten_webgl_init_context_attributes(&attributes);
    attributes.alpha = options.alpha;
    attributes.depth = options.depth;
    attributes.stencil = options.stencil;
    attributes.antialias = false;
    attributes.enableExtensionsByDefault = true;
    attributes.majorVersion = 2;
    attributes.minorVersion = 0;
    const auto context = emscripten_webgl_create_context(canvas_selector(), &attributes);
    if (context <= 0)
        return false;
    if (emscripten_webgl_make_context_current(context) != EMSCRIPTEN_RESULT_SUCCESS) {
        emscripten_webgl_destroy_context(context);
        return false;
    }
    *out_context = context;
    return true;
}

void destroy_webgl_context(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context) noexcept {
    if (context > 0)
        emscripten_webgl_destroy_context(context);
}

bool make_context_current(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context) noexcept {
    return context > 0 &&
           emscripten_webgl_make_context_current(context) == EMSCRIPTEN_RESULT_SUCCESS;
}

bool request_fullscreen() noexcept {
    return emscripten_request_fullscreen(canvas_selector(), EM_TRUE) ==
           EMSCRIPTEN_RESULT_SUCCESS;
}

bool exit_fullscreen() noexcept {
    return emscripten_exit_fullscreen() == EMSCRIPTEN_RESULT_SUCCESS;
}

bool request_pointer_lock() noexcept {
    return emscripten_request_pointerlock(canvas_selector(), EM_TRUE) ==
           EMSCRIPTEN_RESULT_SUCCESS;
}

bool exit_pointer_lock() noexcept {
    return emscripten_exit_pointerlock() == EMSCRIPTEN_RESULT_SUCCESS;
}

bool install_callbacks(const HostCallbacks &callbacks, void *user_data) noexcept {
    remove_callbacks();
    host_state.callbacks = callbacks;
    host_state.user_data = user_data;
    host_state.installed = true;
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE,
                                   resize_callback);
    emscripten_set_mousedown_callback(canvas_selector(), &host_state, EM_TRUE, mouse_callback);
    emscripten_set_mouseup_callback(canvas_selector(), &host_state, EM_TRUE, mouse_callback);
    emscripten_set_mousemove_callback(canvas_selector(), &host_state, EM_TRUE, mouse_callback);
    emscripten_set_mouseenter_callback(canvas_selector(), &host_state, EM_TRUE, mouse_callback);
    emscripten_set_mouseleave_callback(canvas_selector(), &host_state, EM_TRUE, mouse_callback);
    emscripten_set_wheel_callback(canvas_selector(), &host_state, EM_TRUE, wheel_callback);
    emscripten_set_touchstart_callback(canvas_selector(), &host_state, EM_TRUE, touch_callback);
    emscripten_set_touchend_callback(canvas_selector(), &host_state, EM_TRUE, touch_callback);
    emscripten_set_touchmove_callback(canvas_selector(), &host_state, EM_TRUE, touch_callback);
    emscripten_set_touchcancel_callback(canvas_selector(), &host_state, EM_TRUE, touch_callback);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE,
                                    key_callback);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE,
                                  key_callback);
    emscripten_set_keypress_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE,
                                     key_callback);
    emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE,
                                  focus_callback);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE,
                                 focus_callback);
    emscripten_set_webglcontextlost_callback(canvas_selector(), &host_state, EM_TRUE,
                                             context_callback);
    emscripten_set_webglcontextrestored_callback(canvas_selector(), &host_state, EM_TRUE,
                                                 context_callback);
    emscripten_set_pointerlockchange_callback(canvas_selector(), &host_state, EM_TRUE,
                                              pointer_lock_callback);
    return true;
}

void remove_callbacks() noexcept {
    if (!host_state.installed)
        return;
    emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE, nullptr);
    emscripten_set_mousedown_callback(canvas_selector(), &host_state, EM_TRUE, nullptr);
    emscripten_set_mouseup_callback(canvas_selector(), &host_state, EM_TRUE, nullptr);
    emscripten_set_mousemove_callback(canvas_selector(), &host_state, EM_TRUE, nullptr);
    emscripten_set_mouseenter_callback(canvas_selector(), &host_state, EM_TRUE, nullptr);
    emscripten_set_mouseleave_callback(canvas_selector(), &host_state, EM_TRUE, nullptr);
    emscripten_set_wheel_callback(canvas_selector(), &host_state, EM_TRUE, nullptr);
    emscripten_set_touchstart_callback(canvas_selector(), &host_state, EM_TRUE, nullptr);
    emscripten_set_touchend_callback(canvas_selector(), &host_state, EM_TRUE, nullptr);
    emscripten_set_touchmove_callback(canvas_selector(), &host_state, EM_TRUE, nullptr);
    emscripten_set_touchcancel_callback(canvas_selector(), &host_state, EM_TRUE, nullptr);
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE,
                                    nullptr);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE, nullptr);
    emscripten_set_keypress_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE,
                                     nullptr);
    emscripten_set_focus_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE, nullptr);
    emscripten_set_blur_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE, nullptr);
    emscripten_set_webglcontextlost_callback(canvas_selector(), &host_state, EM_TRUE, nullptr);
    emscripten_set_webglcontextrestored_callback(canvas_selector(), &host_state, EM_TRUE, nullptr);
    emscripten_set_pointerlockchange_callback(canvas_selector(), &host_state, EM_TRUE, nullptr);
    host_state = {};
}

bool start_frame_loop(FrameCallback callback, void *user_data) noexcept {
    if (!callback)
        return false;
    frame_callback = callback;
    frame_user_data = user_data;
    if (frame_loop_active)
        return true;
    frame_loop_active = true;
    emscripten_request_animation_frame_loop(frame_callback_adapter, nullptr);
    return true;
}

void stop_frame_loop() noexcept {
    frame_loop_active = false;
    frame_callback = nullptr;
    frame_user_data = nullptr;
}

} // namespace nk::web
