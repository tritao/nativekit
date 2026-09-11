#pragma once

#include <cstdint>

#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

namespace nk::web {

struct CanvasSize {
    int32_t width = 0;
    int32_t height = 0;
    int32_t framebuffer_width = 0;
    int32_t framebuffer_height = 0;
    float scale = 1.0f;
};

enum class KeyEventType : uint8_t {
    down,
    up,
    character
};

struct KeyEvent {
    KeyEventType type = KeyEventType::down;
    uint32_t key_code = 0;
    uint32_t location = 0;
    uint32_t char_code = 0;
    uint32_t modifiers = 0;
    bool repeat = false;
};

enum class PointerEventType : uint8_t {
    move,
    down,
    up,
    enter,
    leave,
    wheel
};

struct PointerEvent {
    PointerEventType type = PointerEventType::move;
    int32_t button = -1;
    uint32_t buttons = 0;
    uint32_t modifiers = 0;
    double x = 0.0;
    double y = 0.0;
    double wheel_x = 0.0;
    double wheel_y = 0.0;
};

enum class TouchEventType : uint8_t {
    begin,
    move,
    end,
    cancel
};

struct TouchEvent {
    TouchEventType type = TouchEventType::move;
    uint32_t identifier = 0;
    double x = 0.0;
    double y = 0.0;
    float pressure = 1.0f;
    uint32_t modifiers = 0;
};

struct HostCallbacks {
    void (*resize)(const CanvasSize &, void *) = nullptr;
    void (*key)(const KeyEvent &, void *) = nullptr;
    void (*pointer)(const PointerEvent &, void *) = nullptr;
    void (*touch)(const TouchEvent &, void *) = nullptr;
    void (*focus)(bool focused, void *) = nullptr;
    void (*context)(bool restored, void *) = nullptr;
    void (*pointer_lock)(bool active, void *) = nullptr;
};

struct WebGLContextOptions {
    bool alpha = false;
    bool depth = false;
    bool stencil = false;
    bool debug = false;
};

const char *canvas_selector() noexcept;
bool canvas_size(CanvasSize *out_size) noexcept;
bool set_canvas_framebuffer_size(const CanvasSize &size) noexcept;
bool set_canvas_size(int32_t width, int32_t height) noexcept;
bool set_canvas_visible(bool visible) noexcept;
bool set_title(const char *title) noexcept;
bool set_cursor(const char *cursor) noexcept;

bool create_webgl_context(const WebGLContextOptions &options,
                          EMSCRIPTEN_WEBGL_CONTEXT_HANDLE *out_context) noexcept;
void destroy_webgl_context(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context) noexcept;
bool make_context_current(EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context) noexcept;

bool request_fullscreen() noexcept;
bool exit_fullscreen() noexcept;
bool request_pointer_lock() noexcept;
bool exit_pointer_lock() noexcept;

bool install_callbacks(const HostCallbacks &callbacks, void *user_data) noexcept;
void remove_callbacks() noexcept;

using FrameCallback = EM_BOOL (*)(double time, void *user_data);
bool start_frame_loop(FrameCallback callback, void *user_data) noexcept;
void stop_frame_loop() noexcept;

} // namespace nk::web
