#pragma once

#include "nativekit_accessibility.h"
#include "nativekit_dialog.h"
#include "nativekit_input.h"
#include "nativekit_notification.h"

#include <array>
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

enum class KeyEventType : uint8_t { down, up, character };

struct KeyEvent {
    KeyEventType type = KeyEventType::down;
    uint32_t key_code = 0;
    uint32_t location = 0;
    uint32_t char_code = 0;
    uint32_t modifiers = 0;
    bool repeat = false;
};

enum class PointerEventType : uint8_t { move, down, up, enter, leave, wheel };

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

enum class TouchEventType : uint8_t { begin, move, end, cancel };

struct TouchEvent {
    TouchEventType type = TouchEventType::move;
    uint32_t identifier = 0;
    double x = 0.0;
    double y = 0.0;
    float pressure = 1.0f;
    uint32_t modifiers = 0;
};

enum class TextInputEventType : uint8_t {
    compose,
    commit,
    delete_backward,
    delete_forward,
    finish_composition,
    selection
};

struct TextInputEvent {
    TextInputEventType type = TextInputEventType::commit;
    const char *text = nullptr;
    uint32_t selection_start = 0;
    uint32_t selection_end = 0;
};

struct AccessibilityActionEvent {
    nk_handle surface = NK_INVALID_HANDLE;
    nk_accessibility_node_id node = NK_ACCESSIBILITY_ROOT;
    nk_accessibility_action action = 0;
    const char *value = nullptr;
    nk_accessibility_text_position selection_start = NK_ACCESSIBILITY_TEXT_POSITION_NONE;
    nk_accessibility_text_position selection_end = NK_ACCESSIBILITY_TEXT_POSITION_NONE;
    nk_accessibility_text_granularity granularity = 0;
};

struct ResourceDropEvent {
    float x = 0.0f;
    float y = 0.0f;
    const char *uris = nullptr;
    const char *text = nullptr;
};

struct ResourceDialogEvent {
    nk_request_id request = NK_INVALID_REQUEST_ID;
    uint32_t kind = 0;
    nk_result result = NK_OK;
    bool accepted = false;
    const char *uris = nullptr;
};

struct NotificationEvent {
    nk_request_id request = NK_INVALID_REQUEST_ID;
    nk_event_kind kind = NK_EVENT_NOTIFICATION_FAILED;
    nk_result result = NK_ERROR_UNKNOWN;
};

struct GamepadStateEvent {
    int32_t index = -1;
    bool connected = false;
    bool standard = false;
    const char *id = nullptr;
    std::array<float, 6> axes{};
    std::array<uint8_t, 17> buttons{};
};

struct TextInputConfig {
    bool active = false;
    uint32_t flags = 0;
    uint32_t input_type = 0;
    uint32_t action = 0;
    const char *text = "";
    uint32_t text_start = 0;
    uint32_t document_length = 0;
    uint32_t selection_start = 0;
    uint32_t selection_end = 0;
    uint32_t composition_start = NK_TEXT_POSITION_NONE;
    uint32_t composition_end = NK_TEXT_POSITION_NONE;
    float cursor_x = 0.0f;
    float cursor_y = 0.0f;
    float cursor_width = 0.0f;
    float cursor_height = 0.0f;
};

struct HostCallbacks {
    void (*resize)(const CanvasSize &, void *) = nullptr;
    void (*key)(const KeyEvent &, void *) = nullptr;
    void (*pointer)(const PointerEvent &, void *) = nullptr;
    void (*touch)(const TouchEvent &, void *) = nullptr;
    void (*text_input)(const TextInputEvent &, void *) = nullptr;
    void (*focus)(bool focused, void *) = nullptr;
    void (*context)(bool restored, void *) = nullptr;
    void (*pointer_lock)(bool active, void *) = nullptr;
    void (*drop)(const ResourceDropEvent &, void *) = nullptr;
    void (*resource_dialog)(const ResourceDialogEvent &, void *) = nullptr;
    void (*notification)(const NotificationEvent &, void *) = nullptr;
    void (*gamepad)(const GamepadStateEvent &, void *) = nullptr;
    void (*accessibility_action)(const AccessibilityActionEvent &, void *) = nullptr;
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
void set_canvas_size_limits(int32_t min_width, int32_t min_height, int32_t max_width,
                            int32_t max_height) noexcept;
void set_canvas_aspect_ratio(int32_t numerator, int32_t denominator) noexcept;
void set_canvas_resizable(bool enabled) noexcept;
void set_canvas_opacity(float opacity) noexcept;
void set_canvas_mouse_passthrough(bool enabled) noexcept;
bool set_canvas_visible(bool visible) noexcept;
bool set_title(const char *title) noexcept;
bool set_cursor(const char *cursor) noexcept;
bool open_url(const char *url) noexcept;
uint32_t appearance() noexcept;
void configure_text_input(const TextInputConfig &config) noexcept;
void set_accessibility_tree(nk_handle surface, int32_t width, int32_t height, bool visible,
                            nk_accessibility_node_id focus, const char *json) noexcept;
void clear_accessibility_tree(nk_handle surface) noexcept;
void set_accessibility_visible(nk_handle surface, bool visible) noexcept;
bool set_clipboard_text(const char *text) noexcept;
bool set_clipboard_resources(const char *uris) noexcept;
bool read_clipboard_text(nk_request_id request) noexcept;
bool read_clipboard_resources(nk_request_id request) noexcept;
bool share(const char *title, const char *text, const char *uris) noexcept;
bool pick_resources(nk_request_id request, uint32_t kind, bool multiple, const char *title,
                    const char *accept, const char *suggested_name) noexcept;
bool has_resource_handle(const char *uri) noexcept;
bool write_resource(const char *uri, const void *data, uint32_t size) noexcept;
bool show_notification(nk_request_id request, const char *title, const char *body, const char *icon,
                       bool silent) noexcept;
bool close_notification(nk_request_id request) noexcept;
bool poll_gamepads() noexcept;
bool fetch_resource(const char *uri, nk_request_id request) noexcept;

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
