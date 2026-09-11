#include "nativekit_graphics.h"
#include "nativekit_clipboard.h"
#include "nativekit_input.h"
#include "nativekit_window.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"
#include "platform/web/host.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

struct WebSurfaceResource;

struct WebCursorResource final : nk::core::Resource {
    nk_handle handle = NK_INVALID_HANDLE;
    nk_cursor_shape shape = NK_CURSOR_ARROW;
};

template <typename T> std::vector<std::byte> bytes_of(const T &value) {
    const auto *first = reinterpret_cast<const std::byte *>(&value);
    return {first, first + sizeof(value)};
}

struct WebWindowResource final : nk::core::Resource {
    nk_handle handle = NK_INVALID_HANDLE;
    uint64_t generation = 0;
    std::string title;
    std::vector<nk_handle> surfaces;
    std::array<nk_input_action, NK_KEY_LAST + 1> keys{};
    std::array<nk_input_action, NK_POINTER_BUTTON_LAST + 1> buttons{};
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t framebuffer_width = 0;
    int32_t framebuffer_height = 0;
    float scale = 1.0f;
    double pointer_x = 0.0;
    double pointer_y = 0.0;
    bool visible = false;
    bool focused = true;
    bool hovered = false;
    bool resizable = true;
    bool fullscreen = false;
    nk_cursor_mode cursor_mode = NK_CURSOR_MODE_NORMAL;
    std::shared_ptr<WebCursorResource> cursor;
    nk_handle text_input_surface = NK_INVALID_HANDLE;
};

struct WebSurfaceResource final : nk::core::Resource {
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle parent = NK_INVALID_HANDLE;
    uint64_t generation = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t framebuffer_width = 0;
    int32_t framebuffer_height = 0;
    bool visible = true;
    bool context_lost = false;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
    nk_surface_frame_callback frame_callback = nullptr;
    void *frame_user_data = nullptr;
    bool text_input_active = false;
    bool text_input_state_set = false;
    nk_text_input_state text_input_state{};
    std::string text_input_text;

    ~WebSurfaceResource() override {
        if (context)
            nk::web::destroy_webgl_context(context);
    }
};

std::weak_ptr<WebWindowResource> active_window;

nk_result invalid_argument(const char *message) {
    nk::core::clear_error();
    nk::core::set_error(message);
    return NK_ERROR_INVALID_ARGUMENT;
}

nk_result invalid_handle(const char *message) {
    nk::core::clear_error();
    nk::core::set_error(message);
    return NK_ERROR_INVALID_HANDLE;
}

nk_result unsupported(const char *message) {
    nk::core::clear_error();
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    nk::core::set_error(message);
    return NK_ERROR_UNSUPPORTED;
}

template <typename Resource>
std::shared_ptr<Resource> get_resource(nk_handle handle, nk::core::ResourceType type,
                                       const char *message) {
    auto resource = nk::core::handles().get(handle, type);
    if (!resource) {
        nk::core::set_error(message);
        return {};
    }
    return std::dynamic_pointer_cast<Resource>(std::move(resource));
}

std::shared_ptr<WebWindowResource> get_window(nk_handle handle) {
    return get_resource<WebWindowResource>(handle, nk::core::ResourceType::window,
                                           "invalid web window handle");
}

std::shared_ptr<WebSurfaceResource> get_surface(nk_handle handle) {
    return get_resource<WebSurfaceResource>(handle, nk::core::ResourceType::surface,
                                            "invalid web surface handle");
}

uint32_t utf8_codepoints(const std::string &text) {
    uint32_t result = 0;
    for (const auto character : text)
        result += (static_cast<unsigned char>(character) & 0xc0u) != 0x80u;
    return result;
}

std::size_t utf8_byte_offset(const std::string &text, uint32_t codepoint) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        if ((static_cast<unsigned char>(text[offset]) & 0xc0u) != 0x80u) {
            if (codepoint == 0)
                return offset;
            --codepoint;
        }
        ++offset;
    }
    return offset;
}

void configure_text_input(WebSurfaceResource &surface) {
    nk::web::TextInputConfig config{};
    config.active = surface.text_input_active;
    config.text = surface.text_input_text.c_str();
    if (surface.text_input_state_set) {
        const auto &state = surface.text_input_state;
        config.flags = state.flags;
        config.input_type = state.input_type;
        config.action = state.action;
        config.text_start = state.text_start;
        config.document_length = state.document_length;
        config.selection_start = state.selection_start;
        config.selection_end = state.selection_end;
        config.composition_start = state.composition_start;
        config.composition_end = state.composition_end;
        config.cursor_x = state.cursor_x;
        config.cursor_y = state.cursor_y;
        config.cursor_width = state.cursor_width;
        config.cursor_height = state.cursor_height;
    }
    nk::web::configure_text_input(config);
}

void update_text_input_state(WebSurfaceResource &surface, nk_text_position replace_start,
                             nk_text_position replace_end, const std::string &text,
                             nk_text_position selection_start, nk_text_position selection_end,
                             nk_text_position composition_start,
                             nk_text_position composition_end) {
    if (!surface.text_input_state_set)
        return;
    auto &state = surface.text_input_state;
    const auto text_end = static_cast<uint64_t>(state.text_start) +
                          utf8_codepoints(surface.text_input_text);
    if (replace_start < state.text_start || replace_end < replace_start ||
        replace_end > text_end)
        return;
    const auto relative_start = replace_start - state.text_start;
    const auto relative_end = replace_end - state.text_start;
    const auto byte_start = utf8_byte_offset(surface.text_input_text, relative_start);
    const auto byte_end = utf8_byte_offset(surface.text_input_text, relative_end);
    surface.text_input_text.replace(byte_start, byte_end - byte_start, text);
    const auto removed = replace_end - replace_start;
    const auto inserted = utf8_codepoints(text);
    const auto document_delta = static_cast<int64_t>(inserted) - removed;
    if (document_delta < 0)
        state.document_length -= static_cast<uint32_t>(-document_delta);
    else
        state.document_length += static_cast<uint32_t>(document_delta);
    state.selection_start = selection_start;
    state.selection_end = selection_end;
    state.composition_start = composition_start;
    state.composition_end = composition_end;
    state.text = surface.text_input_text.c_str();
}

const char *cursor_name(nk_cursor_shape shape) {
    switch (shape) {
    case NK_CURSOR_ARROW: return "default";
    case NK_CURSOR_IBEAM: return "text";
    case NK_CURSOR_CROSSHAIR: return "crosshair";
    case NK_CURSOR_HAND: return "pointer";
    case NK_CURSOR_HORIZONTAL_RESIZE: return "ew-resize";
    case NK_CURSOR_VERTICAL_RESIZE: return "ns-resize";
    case NK_CURSOR_NWSE_RESIZE: return "nwse-resize";
    case NK_CURSOR_NESW_RESIZE: return "nesw-resize";
    case NK_CURSOR_MOVE: return "move";
    case NK_CURSOR_NOT_ALLOWED: return "not-allowed";
    default: return nullptr;
    }
}

void apply_cursor(WebWindowResource &window) {
    if (window.cursor_mode != NK_CURSOR_MODE_NORMAL) {
        nk::web::set_cursor("none");
        return;
    }
    nk::web::set_cursor(window.cursor ? cursor_name(window.cursor->shape) : "default");
}

void queue_window_state(WebWindowResource &window) {
    nk_window_state payload{sizeof(payload), 0, {0, 0}};
    if (window.visible)
        payload.flags |= NK_WINDOW_STATE_VISIBLE;
    if (window.focused)
        payload.flags |= NK_WINDOW_STATE_ACTIVE;
    if (window.fullscreen)
        payload.flags |= NK_WINDOW_STATE_FULLSCREEN;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_WINDOW_STATE_CHANGED;
    event.source = window.handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

void queue_surface_resize(WebSurfaceResource &surface) {
    const nk_surface_resize_event payload{surface.width, surface.height,
                                          surface.framebuffer_width,
                                          surface.framebuffer_height};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_SURFACE_RESIZE;
    event.source = surface.handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

void release_pressed_input(WebWindowResource &window) {
    for (nk_key key = 1; key <= NK_KEY_LAST; ++key) {
        if (window.keys[key] != NK_INPUT_PRESS)
            continue;
        window.keys[key] = NK_INPUT_RELEASE;
        const nk_key_event payload{key, 0, NK_INPUT_RELEASE, 0};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_KEY;
        event.source = window.handle;
        event.flags = 1u;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    }
    for (nk_pointer_button button = 0; button <= NK_POINTER_BUTTON_LAST; ++button) {
        if (window.buttons[button] != NK_INPUT_PRESS)
            continue;
        window.buttons[button] = NK_INPUT_RELEASE;
        const nk_pointer_button_event payload{button, NK_INPUT_RELEASE, 0, 0, window.pointer_x,
                                             window.pointer_y};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_POINTER_BUTTON;
        event.source = window.handle;
        event.flags = 1u;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    }
}

void apply_canvas_size(WebWindowResource &window, const nk::web::CanvasSize &size) {
    const bool logical_changed = window.width != size.width || window.height != size.height;
    const bool framebuffer_changed = window.framebuffer_width != size.framebuffer_width ||
                                     window.framebuffer_height != size.framebuffer_height;
    const bool scale_changed = window.scale != size.scale;
    window.width = size.width;
    window.height = size.height;
    window.framebuffer_width = size.framebuffer_width;
    window.framebuffer_height = size.framebuffer_height;
    window.scale = size.scale;

    if (logical_changed) {
        const nk_window_resize_event payload{window.width, window.height};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WINDOW_RESIZE;
        event.source = window.handle;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    }
    if (framebuffer_changed) {
        const nk_window_framebuffer_resize_event payload{window.framebuffer_width,
                                                         window.framebuffer_height};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WINDOW_FRAMEBUFFER_RESIZE;
        event.source = window.handle;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    }
    if (scale_changed) {
        const nk_window_scale_event payload{window.scale};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_WINDOW_SCALE_CHANGED;
        event.source = window.handle;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    }

    for (const nk_handle surface_handle : window.surfaces) {
        auto surface = get_surface(surface_handle);
        if (!surface)
            continue;
        if (surface->width != window.width || surface->height != window.height ||
            surface->framebuffer_width != window.framebuffer_width ||
            surface->framebuffer_height != window.framebuffer_height) {
            surface->width = window.width;
            surface->height = window.height;
            surface->framebuffer_width = window.framebuffer_width;
            surface->framebuffer_height = window.framebuffer_height;
            queue_surface_resize(*surface);
        }
    }
}

void sync_canvas_size(WebWindowResource &window) {
    nk::web::CanvasSize size{};
    if (nk::web::canvas_size(&size)) {
        if (size.framebuffer_width != window.framebuffer_width ||
            size.framebuffer_height != window.framebuffer_height)
            nk::web::set_canvas_framebuffer_size(size);
        apply_canvas_size(window, size);
    }
}

nk_key key_from_dom(uint32_t key_code, uint32_t location) {
    if (key_code >= 65 && key_code <= 90)
        return NK_KEY_A + key_code - 65;
    if (key_code >= 48 && key_code <= 57)
        return NK_KEY_0 + key_code - 48;
    if (key_code >= 112 && key_code <= 123)
        return NK_KEY_F1 + key_code - 112;
    if (location == 3 && key_code >= 96 && key_code <= 105)
        return NK_KEY_KP_0 + key_code - 96;
    switch (key_code) {
    case 32: return NK_KEY_SPACE;
    case 186: return NK_KEY_SEMICOLON;
    case 187: return location == 3 ? NK_KEY_KP_EQUAL : NK_KEY_EQUAL;
    case 188: return NK_KEY_COMMA;
    case 189: return location == 3 ? NK_KEY_KP_SUBTRACT : NK_KEY_MINUS;
    case 190: return NK_KEY_PERIOD;
    case 191: return NK_KEY_SLASH;
    case 192: return NK_KEY_GRAVE_ACCENT;
    case 219: return NK_KEY_LEFT_BRACKET;
    case 220: return NK_KEY_BACKSLASH;
    case 221: return NK_KEY_RIGHT_BRACKET;
    case 222: return NK_KEY_APOSTROPHE;
    case 8: return NK_KEY_BACKSPACE;
    case 9: return NK_KEY_TAB;
    case 13: return location == 3 ? NK_KEY_KP_ENTER : NK_KEY_ENTER;
    case 16: return location == 2 ? NK_KEY_RIGHT_SHIFT : NK_KEY_LEFT_SHIFT;
    case 17: return location == 2 ? NK_KEY_RIGHT_CONTROL : NK_KEY_LEFT_CONTROL;
    case 18: return location == 2 ? NK_KEY_RIGHT_ALT : NK_KEY_LEFT_ALT;
    case 27: return NK_KEY_ESCAPE;
    case 33: return NK_KEY_PAGE_UP;
    case 34: return NK_KEY_PAGE_DOWN;
    case 35: return NK_KEY_END;
    case 36: return NK_KEY_HOME;
    case 37: return NK_KEY_LEFT;
    case 38: return NK_KEY_UP;
    case 40: return NK_KEY_DOWN;
    case 44: return NK_KEY_PRINT_SCREEN;
    case 45: return NK_KEY_INSERT;
    case 46: return location == 3 ? NK_KEY_KP_DECIMAL : NK_KEY_DELETE;
    case 91: return NK_KEY_LEFT_SUPER;
    case 93: return NK_KEY_RIGHT_SUPER;
    case 106: return NK_KEY_KP_MULTIPLY;
    case 107: return NK_KEY_KP_ADD;
    case 109: return NK_KEY_KP_SUBTRACT;
    case 111: return NK_KEY_KP_DIVIDE;
    case 144: return NK_KEY_NUM_LOCK;
    case 145: return NK_KEY_SCROLL_LOCK;
    case 19: return NK_KEY_PAUSE;
    default: return NK_KEY_UNKNOWN;
    }
}

nk_pointer_button button_from_dom(int32_t button) {
    switch (button) {
    case 0: return NK_POINTER_BUTTON_LEFT;
    case 1: return NK_POINTER_BUTTON_MIDDLE;
    case 2: return NK_POINTER_BUTTON_RIGHT;
    case 3: return NK_POINTER_BUTTON_4;
    case 4: return NK_POINTER_BUTTON_5;
    default: return UINT32_MAX;
    }
}

void on_resize(const nk::web::CanvasSize &size, void *user_data) {
    nk::core::callback_boundary([&] {
        auto *window = static_cast<WebWindowResource *>(user_data);
        if (window && nk::core::is_runtime_generation(window->generation)) {
            nk::web::set_canvas_framebuffer_size(size);
            apply_canvas_size(*window, size);
        }
    });
}

void on_key(const nk::web::KeyEvent &event, void *user_data) {
    nk::core::callback_boundary([&] {
        auto *window = static_cast<WebWindowResource *>(user_data);
        if (!window || !nk::core::is_runtime_generation(window->generation))
            return;
        if (event.type == nk::web::KeyEventType::character) {
            if (event.char_code == 0)
                return;
            const nk_text_input_event payload{event.char_code, 0};
            nk::core::QueuedEvent queued;
            queued.kind = NK_EVENT_TEXT_INPUT;
            queued.source = window->handle;
            queued.data = bytes_of(payload);
            nk::core::push_event(std::move(queued));
            return;
        }
        const nk_key key = key_from_dom(event.key_code, event.location);
        const nk_input_action action = event.type == nk::web::KeyEventType::up
                                           ? NK_INPUT_RELEASE
                                           : (event.repeat || (key <= NK_KEY_LAST &&
                                                               window->keys[key] == NK_INPUT_PRESS))
                                                 ? NK_INPUT_REPEAT
                                                 : NK_INPUT_PRESS;
        if (key <= NK_KEY_LAST)
            window->keys[key] = action == NK_INPUT_RELEASE ? NK_INPUT_RELEASE : NK_INPUT_PRESS;
        const nk_key_event payload{key, event.key_code, action, event.modifiers};
        nk::core::QueuedEvent queued;
        queued.kind = NK_EVENT_KEY;
        queued.source = window->handle;
        queued.data = bytes_of(payload);
        nk::core::push_event(std::move(queued));
    });
}

void on_pointer(const nk::web::PointerEvent &event, void *user_data) {
    nk::core::callback_boundary([&] {
        auto *window = static_cast<WebWindowResource *>(user_data);
        if (!window || !nk::core::is_runtime_generation(window->generation))
            return;
        window->x = 0;
        window->y = 0;
        if (event.type == nk::web::PointerEventType::enter ||
            event.type == nk::web::PointerEventType::leave) {
            window->hovered = event.type == nk::web::PointerEventType::enter;
            nk::core::QueuedEvent queued;
            queued.kind = NK_EVENT_POINTER_ENTER;
            queued.source = window->handle;
            queued.flags = window->hovered ? 1u : 0u;
            nk::core::push_event(std::move(queued));
            return;
        }
        if (event.type == nk::web::PointerEventType::wheel) {
            const nk_pointer_scroll_event payload{event.wheel_x, event.wheel_y};
            nk::core::QueuedEvent queued;
            queued.kind = NK_EVENT_POINTER_SCROLL;
            queued.source = window->handle;
            queued.data = bytes_of(payload);
            nk::core::push_event(std::move(queued));
            return;
        }
        if (event.type == nk::web::PointerEventType::move) {
            window->pointer_x = event.x;
            window->pointer_y = event.y;
            const nk_pointer_move_event payload{event.x, event.y};
            nk::core::QueuedEvent queued;
            queued.kind = NK_EVENT_POINTER_MOVE;
            queued.source = window->handle;
            queued.data = bytes_of(payload);
            nk::core::push_event(std::move(queued));
            return;
        }
        const auto button = button_from_dom(event.button);
        if (button == UINT32_MAX)
            return;
        const nk_input_action action = event.type == nk::web::PointerEventType::up
                                           ? NK_INPUT_RELEASE
                                           : NK_INPUT_PRESS;
        window->buttons[button] = action;
        window->pointer_x = event.x;
        window->pointer_y = event.y;
        const nk_pointer_button_event payload{button, action, event.modifiers, 0, event.x,
                                              event.y};
        nk::core::QueuedEvent queued;
        queued.kind = NK_EVENT_POINTER_BUTTON;
        queued.source = window->handle;
        queued.data = bytes_of(payload);
        nk::core::push_event(std::move(queued));
    });
}

void on_touch(const nk::web::TouchEvent &event, void *user_data) {
    nk::core::callback_boundary([&] {
        auto *window = static_cast<WebWindowResource *>(user_data);
        if (!window || !nk::core::is_runtime_generation(window->generation))
            return;
        const nk_touch_action action = event.type == nk::web::TouchEventType::begin
                                           ? NK_TOUCH_BEGIN
                                           : event.type == nk::web::TouchEventType::end
                                                 ? NK_TOUCH_END
                                                 : event.type == nk::web::TouchEventType::cancel
                                                       ? NK_TOUCH_CANCEL
                                                       : NK_TOUCH_MOVE;
        const nk_touch_event payload{event.identifier, action, NK_TOUCH_TOOL_FINGER,
                                     event.modifiers, event.x, event.y, event.pressure, 0.0f,
                                     0.0f, 0};
        nk::core::QueuedEvent queued;
        queued.kind = NK_EVENT_TOUCH;
        queued.source = window->handle;
        queued.data = bytes_of(payload);
        nk::core::push_event(std::move(queued));
    });
}

void queue_text_edit(WebSurfaceResource &surface, nk_text_edit_action action,
                     const std::string &text, nk_text_position replace_start,
                     nk_text_position replace_end, nk_text_position selection_start,
                     nk_text_position selection_end, nk_text_position composition_start,
                     nk_text_position composition_end) {
    nk_text_edit_event payload{};
    payload.action = action;
    payload.text_offset = text.empty() ? 0u : sizeof(payload);
    payload.text_length = static_cast<uint32_t>(text.size());
    payload.replace_start = replace_start;
    payload.replace_end = replace_end;
    payload.selection_start = selection_start;
    payload.selection_end = selection_end;
    payload.composition_start = composition_start;
    payload.composition_end = composition_end;

    nk::core::QueuedEvent queued;
    queued.kind = NK_EVENT_TEXT_EDIT;
    queued.source = surface.handle;
    queued.data.resize(sizeof(payload) + text.size() + (text.empty() ? 0u : 1u));
    std::memcpy(queued.data.data(), &payload, sizeof(payload));
    if (!text.empty())
        std::memcpy(queued.data.data() + sizeof(payload), text.c_str(), text.size() + 1);
    if (nk::core::push_event(std::move(queued)) == NK_OK)
        update_text_input_state(surface, replace_start, replace_end, text, selection_start,
                                selection_end, composition_start, composition_end);
}

void on_text_input(const nk::web::TextInputEvent &event, void *user_data) {
    nk::core::callback_boundary([&] {
        auto *window = static_cast<WebWindowResource *>(user_data);
        if (!window || !nk::core::is_runtime_generation(window->generation) ||
            window->text_input_surface == NK_INVALID_HANDLE)
            return;
        auto surface = get_surface(window->text_input_surface);
        if (!surface || !surface->text_input_active || !surface->text_input_state_set)
            return;

        const auto &state = surface->text_input_state;
        const auto text_end = static_cast<nk_text_position>(
            static_cast<uint64_t>(state.text_start) + utf8_codepoints(surface->text_input_text));
        const bool has_composition = state.composition_start != NK_TEXT_POSITION_NONE &&
                                     state.composition_end != NK_TEXT_POSITION_NONE &&
                                     state.composition_start <= state.composition_end &&
                                     state.composition_start >= state.text_start &&
                                     state.composition_end <= text_end;
        nk_text_position replace_start = state.selection_start;
        nk_text_position replace_end = state.selection_end;
        nk_text_position selection_start = state.selection_start;
        nk_text_position selection_end = state.selection_end;
        nk_text_position composition_start = state.composition_start;
        nk_text_position composition_end = state.composition_end;
        nk_text_edit_action action = NK_TEXT_EDIT_COMMIT;
        std::string text = event.text ? event.text : "";

        switch (event.type) {
        case nk::web::TextInputEventType::compose:
            if (has_composition) {
                replace_start = state.composition_start;
                replace_end = state.composition_end;
            }
            selection_start = replace_start + utf8_codepoints(text);
            selection_end = selection_start;
            composition_start = replace_start;
            composition_end = selection_start;
            action = NK_TEXT_EDIT_COMPOSE;
            break;
        case nk::web::TextInputEventType::commit:
            if (has_composition) {
                replace_start = state.composition_start;
                replace_end = state.composition_end;
            }
            selection_start = replace_start + utf8_codepoints(text);
            selection_end = selection_start;
            composition_start = NK_TEXT_POSITION_NONE;
            composition_end = NK_TEXT_POSITION_NONE;
            action = NK_TEXT_EDIT_COMMIT;
            break;
        case nk::web::TextInputEventType::delete_backward:
            action = NK_TEXT_EDIT_DELETE;
            if (replace_start == replace_end) {
                if (replace_start <= state.text_start)
                    return;
                --replace_start;
                replace_end = replace_start + 1;
            }
            selection_start = replace_start;
            selection_end = replace_start;
            composition_start = NK_TEXT_POSITION_NONE;
            composition_end = NK_TEXT_POSITION_NONE;
            break;
        case nk::web::TextInputEventType::delete_forward:
            action = NK_TEXT_EDIT_DELETE;
            if (replace_start == replace_end) {
                if (replace_end >= text_end)
                    return;
                replace_end++;
            }
            selection_end = replace_start;
            selection_start = replace_start;
            composition_start = NK_TEXT_POSITION_NONE;
            composition_end = NK_TEXT_POSITION_NONE;
            break;
        case nk::web::TextInputEventType::finish_composition:
            replace_start = state.selection_start;
            replace_end = replace_start;
            selection_start = replace_start;
            selection_end = replace_start;
            composition_start = NK_TEXT_POSITION_NONE;
            composition_end = NK_TEXT_POSITION_NONE;
            action = NK_TEXT_EDIT_FINISH_COMPOSITION;
            text.clear();
            break;
        case nk::web::TextInputEventType::selection: {
            const auto relative_start = std::min(event.selection_start, text_end - state.text_start);
            const auto relative_end = std::min(event.selection_end, text_end - state.text_start);
            selection_start = state.text_start + std::min(relative_start, relative_end);
            selection_end = state.text_start + std::max(relative_start, relative_end);
            replace_start = state.selection_start;
            replace_end = state.selection_end;
            action = NK_TEXT_EDIT_SET_SELECTION;
            break;
        }
        }

        queue_text_edit(*surface, action, text, replace_start, replace_end, selection_start,
                        selection_end, composition_start, composition_end);
    });
}

void on_focus(bool focused, void *user_data) {
    nk::core::callback_boundary([&] {
        auto *window = static_cast<WebWindowResource *>(user_data);
        if (!window || !nk::core::is_runtime_generation(window->generation))
            return;
        if (!focused)
            release_pressed_input(*window);
        if (window->focused != focused) {
            window->focused = focused;
            queue_window_state(*window);
        }
    });
}

void on_context(bool restored, void *user_data) {
    nk::core::callback_boundary([&] {
        auto *window = static_cast<WebWindowResource *>(user_data);
        if (!window || !nk::core::is_runtime_generation(window->generation))
            return;
        for (const nk_handle surface_handle : window->surfaces) {
            auto surface = get_surface(surface_handle);
            if (!surface)
                continue;
            surface->context_lost = !restored;
            nk::core::QueuedEvent event;
            event.kind = restored ? NK_EVENT_SURFACE_READY : NK_EVENT_SURFACE_LOST;
            event.source = surface->handle;
            nk::core::push_event(std::move(event));
        }
    });
}

void on_pointer_lock(bool active, void *user_data) {
    nk::core::callback_boundary([&] {
        auto *window = static_cast<WebWindowResource *>(user_data);
        if (!window || !nk::core::is_runtime_generation(window->generation))
            return;
        if (!active && window->cursor_mode != NK_CURSOR_MODE_NORMAL) {
            window->cursor_mode = NK_CURSOR_MODE_NORMAL;
            apply_cursor(*window);
        }
    });
}

EM_BOOL frame_loop(double, void *user_data) {
    auto *surface = static_cast<WebSurfaceResource *>(user_data);
    if (!surface || !nk::core::is_runtime_generation(surface->generation) ||
        !surface->frame_callback || surface->context_lost)
        return EM_TRUE;
    auto window = get_window(surface->parent);
    if (!window || !nk::web::make_context_current(surface->context))
        return EM_TRUE;
    sync_canvas_size(*window);
    nk::core::callback_boundary([&] {
        if (surface->frame_callback)
            surface->frame_callback(surface->handle, surface->framebuffer_width,
                                     surface->framebuffer_height, surface->frame_user_data);
    });
    return surface->frame_callback ? EM_TRUE : EM_FALSE;
}

void remove_surface_from_window(WebSurfaceResource &surface) {
    auto window = get_window(surface.parent);
    if (!window)
        return;
    window->surfaces.erase(
        std::remove(window->surfaces.begin(), window->surfaces.end(), surface.handle),
        window->surfaces.end());
}

void shutdown_web() noexcept {
    nk::web::stop_frame_loop();
    nk::web::remove_callbacks();
    active_window.reset();
}

} // namespace

namespace nk::backend {

void pump_events() noexcept {
    try {
        if (auto window = active_window.lock())
            sync_canvas_size(*window);
    } catch (...) {
    }
}

void shutdown() noexcept {
    shutdown_web();
}

} // namespace nk::backend

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_WINDOW | NK_CAP_INPUT | NK_CAP_OPENGL_ES_SURFACE | NK_CAP_WINDOW_GEOMETRY |
           NK_CAP_RESOURCE_IO;
}

nk_result NK_CALL nk_clipboard_set_text(const char *text) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!text)
        return invalid_argument("web clipboard text is null");
    if (!nk::web::set_clipboard_text(text))
        return unsupported("browser clipboard write is unavailable");
    return NK_OK;
}

nk_result NK_CALL nk_clipboard_read_text(nk_request_id *out_request) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_request)
        return invalid_argument("web clipboard request output is null");
    *out_request = NK_INVALID_REQUEST_ID;
    const auto request = nk::core::next_request_id();
    if (!nk::web::read_clipboard_text(request))
        return unsupported("browser clipboard read is unavailable");
    *out_request = request;
    return NK_OK;
}

nk_result NK_CALL nk_window_create(const nk_window_options *options, nk_handle *out_window) {
    return nk::core::result_boundary("unexpected error while creating web window", [&]() -> nk_result {
        if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
            return result;
        if (!options || options->struct_size < sizeof(nk_window_options) || !out_window ||
            options->width <= 0 || options->height <= 0)
            return invalid_argument("invalid web window options");
        if (options->owner != NK_INVALID_HANDLE || (options->flags & NK_WINDOW_MODAL))
            return unsupported("owned and modal web windows are not supported");
        if (!active_window.expired())
            return NK_ERROR_ALREADY_INITIALIZED;

        auto window = std::make_shared<WebWindowResource>();
        window->generation = nk::core::runtime_generation();
        window->title = options->title ? options->title : "";
        window->visible = (options->flags & NK_WINDOW_HIDDEN) == 0;
        window->resizable = (options->flags & NK_WINDOW_RESIZABLE) != 0;
        const auto handle = nk::core::handles().insert(nk::core::ResourceType::window, window);
        if (handle == NK_INVALID_HANDLE)
            return NK_ERROR_OUT_OF_MEMORY;
        window->handle = handle;
        active_window = window;
        if (!nk::web::set_canvas_size(options->width, options->height)) {
            active_window.reset();
            nk::core::handles().erase(handle, nk::core::ResourceType::window);
            return NK_ERROR_UNKNOWN;
        }
        nk::web::HostCallbacks callbacks{};
        callbacks.resize = on_resize;
        callbacks.key = on_key;
        callbacks.pointer = on_pointer;
        callbacks.touch = on_touch;
        callbacks.text_input = on_text_input;
        callbacks.focus = on_focus;
        callbacks.context = on_context;
        callbacks.pointer_lock = on_pointer_lock;
        nk::web::install_callbacks(callbacks, window.get());
        nk::web::set_canvas_visible(window->visible);
        if (!window->title.empty())
            nk::web::set_title(window->title.c_str());
        nk::web::CanvasSize size{};
        nk::web::canvas_size(&size);
        apply_canvas_size(*window, size);
        queue_window_state(*window);
        *out_window = handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_window_destroy(nk_handle handle) {
    return nk::core::result_boundary("unexpected error while destroying web window", [&]() -> nk_result {
        if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
            return result;
        auto window = get_window(handle);
        if (!window)
            return invalid_handle("invalid web window handle");
        const auto surfaces = window->surfaces;
        for (const auto surface : surfaces)
            nk_surface_destroy(surface);
        if (active_window.lock() == window)
            shutdown_web();
        if (!nk::core::handles().erase(handle, nk::core::ResourceType::window))
            return invalid_handle("web window was already destroyed");
        return NK_OK;
    });
}

nk_result NK_CALL nk_window_show(nk_handle handle, nk_bool visible) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    window->visible = visible != 0;
    nk::web::set_canvas_visible(window->visible);
    queue_window_state(*window);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_title(nk_handle handle, const char *title) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!title)
        return invalid_argument("web window title is null");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    window->title = title;
    return nk::web::set_title(window->title.c_str()) ? NK_OK : NK_ERROR_UNKNOWN;
}

nk_result NK_CALL nk_window_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                       int32_t height) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (x != 0 || y != 0)
        return unsupported("browser canvas positions are controlled by the page");
    if (width <= 0 || height <= 0)
        return invalid_argument("web window size must be positive");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    if (!nk::web::set_canvas_size(width, height))
        return NK_ERROR_UNKNOWN;
    sync_canvas_size(*window);
    return NK_OK;
}

nk_result NK_CALL nk_window_get_scale(nk_handle handle, float *out_scale) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_scale)
        return invalid_argument("web window scale output is null");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    sync_canvas_size(*window);
    *out_scale = window->scale;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_state(nk_handle handle, nk_window_state *out_state) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_state || out_state->struct_size < sizeof(nk_window_state))
        return invalid_argument("invalid web window state output");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    const auto size = out_state->struct_size;
    *out_state = {size, 0, {0, 0}};
    if (window->visible)
        out_state->flags |= NK_WINDOW_STATE_VISIBLE;
    if (window->focused)
        out_state->flags |= NK_WINDOW_STATE_ACTIVE;
    if (window->fullscreen)
        out_state->flags |= NK_WINDOW_STATE_FULLSCREEN;
    return NK_OK;
}

nk_result NK_CALL nk_window_is_focused(nk_handle handle, nk_bool *out_focused) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_focused)
        return invalid_argument("web window focus output is null");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    *out_focused = window->focused ? 1u : 0u;
    return NK_OK;
}

nk_result NK_CALL nk_window_is_visible(nk_handle handle, nk_bool *out_visible) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_visible)
        return invalid_argument("web window visibility output is null");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    *out_visible = window->visible ? 1u : 0u;
    return NK_OK;
}

nk_result NK_CALL nk_window_minimize(nk_handle) {
    return unsupported("browser canvases cannot be minimized by NativeKit");
}
nk_result NK_CALL nk_window_maximize(nk_handle) {
    return unsupported("browser canvases cannot be maximized by NativeKit");
}
nk_result NK_CALL nk_window_restore(nk_handle) {
    return unsupported("browser canvases cannot be restored by NativeKit");
}
nk_result NK_CALL nk_window_activate(nk_handle) {
    return unsupported("browser canvas activation is controlled by the page");
}

nk_result NK_CALL nk_window_set_fullscreen(nk_handle handle, nk_bool enabled) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    const bool success = enabled ? nk::web::request_fullscreen() : nk::web::exit_fullscreen();
    if (!success)
        return unsupported("fullscreen requires a browser gesture and page permission");
    window->fullscreen = enabled != 0;
    queue_window_state(*window);
    return NK_OK;
}

nk_result NK_CALL nk_window_request_attention(nk_handle) {
    return unsupported("browser canvases have no window-manager attention state");
}

nk_result NK_CALL nk_window_set_size_limits(nk_handle, const nk_window_size_limits *) {
    return unsupported("browser canvas size limits are controlled by page CSS");
}

nk_result NK_CALL nk_window_get_native(nk_handle, nk_native_window *) {
    return unsupported("browser windows have no NativeKit native-window descriptor");
}

nk_result NK_CALL nk_window_wrap_native(const nk_native_window *, nk_handle *) {
    return unsupported("browser windows cannot wrap native-window descriptors");
}

nk_result NK_CALL nk_surface_create(nk_handle window_handle, const nk_surface_options *options,
                                    nk_handle *out_surface) {
    return nk::core::result_boundary("unexpected error while creating web surface", [&]() -> nk_result {
        if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
            return result;
        if (!options || options->struct_size < sizeof(nk_surface_options) || !out_surface ||
            options->width <= 0 || options->height <= 0)
            return invalid_argument("invalid web surface options");
        if (options->api != NK_GRAPHICS_OPENGL && options->api != NK_GRAPHICS_OPENGL_ES)
            return unsupported("the first web backend supports WebGL2 only");
        if (options->share_surface != NK_INVALID_HANDLE)
            return unsupported("shared WebGL surfaces are not supported yet");
        auto window = get_window(window_handle);
        if (!window)
            return invalid_handle("invalid web surface parent window");
        if (!window->surfaces.empty())
            return unsupported("a browser canvas supports one surface initially");
        if (!nk::web::set_canvas_size(options->width, options->height))
            return NK_ERROR_UNKNOWN;

        nk::web::WebGLContextOptions context_options{};
        context_options.alpha = (options->flags & NK_SURFACE_ALPHA) != 0;
        context_options.depth = (options->flags & NK_SURFACE_DEPTH) != 0;
        context_options.stencil = (options->flags & NK_SURFACE_STENCIL) != 0;
        EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
        if (!nk::web::create_webgl_context(context_options, &context)) {
            nk::core::set_error("could not create a WebGL2 context for the NativeKit canvas");
            return NK_ERROR_UNKNOWN;
        }
        auto surface = std::make_shared<WebSurfaceResource>();
        surface->parent = window_handle;
        surface->generation = nk::core::runtime_generation();
        surface->context = context;
        const auto handle = nk::core::handles().insert(nk::core::ResourceType::surface, surface);
        if (handle == NK_INVALID_HANDLE) {
            nk::web::destroy_webgl_context(context);
            return NK_ERROR_OUT_OF_MEMORY;
        }
        surface->handle = handle;
        window->surfaces.push_back(handle);
        sync_canvas_size(*window);
        surface->width = window->width;
        surface->height = window->height;
        surface->framebuffer_width = window->framebuffer_width;
        surface->framebuffer_height = window->framebuffer_height;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_SURFACE_READY;
        event.source = handle;
        nk::core::push_event(std::move(event));
        *out_surface = handle;
        return NK_OK;
    });
}

nk_result NK_CALL nk_surface_destroy(nk_handle handle) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    auto surface = get_surface(handle);
    if (!surface)
        return invalid_handle("invalid web surface handle");
    if (surface->text_input_active) {
        if (auto window = get_window(surface->parent)) {
            if (window->text_input_surface == surface->handle) {
                window->text_input_surface = NK_INVALID_HANDLE;
                surface->text_input_active = false;
                configure_text_input(*surface);
            }
        }
    }
    if (surface->frame_callback && active_window.lock())
        nk::web::stop_frame_loop();
    surface->frame_callback = nullptr;
    surface->frame_user_data = nullptr;
    remove_surface_from_window(*surface);
    return nk::core::handles().erase(handle, nk::core::ResourceType::surface)
               ? NK_OK
               : invalid_handle("web surface was already destroyed");
}

nk_result NK_CALL nk_surface_show(nk_handle handle, nk_bool visible) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    auto surface = get_surface(handle);
    if (!surface)
        return invalid_handle("invalid web surface handle");
    surface->visible = visible != 0;
    auto window = get_window(surface->parent);
    if (window)
        nk::web::set_canvas_visible(surface->visible && window->visible);
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                        int32_t height) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (x != 0 || y != 0 || width <= 0 || height <= 0)
        return invalid_argument("browser surface bounds must be positive and start at zero");
    auto surface = get_surface(handle);
    if (!surface)
        return invalid_handle("invalid web surface handle");
    auto window = get_window(surface->parent);
    if (!window)
        return invalid_handle("invalid web surface parent window");
    if (!nk::web::set_canvas_size(width, height))
        return NK_ERROR_UNKNOWN;
    sync_canvas_size(*window);
    return NK_OK;
}

nk_result NK_CALL nk_surface_make_current(nk_handle handle) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    auto surface = get_surface(handle);
    if (!surface)
        return invalid_handle("invalid web surface handle");
    return nk::web::make_context_current(surface->context) ? NK_OK : NK_ERROR_UNKNOWN;
}

nk_result NK_CALL nk_surface_present(nk_handle handle) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    auto surface = get_surface(handle);
    if (!surface)
        return invalid_handle("invalid web surface handle");
    if (surface->context_lost)
        return NK_ERROR_INVALID_REQUEST;
    /* WebGL's default framebuffer is presented by the browser's compositor. */
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_frame_callback(nk_handle handle,
                                                 nk_surface_frame_callback callback,
                                                 void *user_data) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    auto surface = get_surface(handle);
    if (!surface)
        return invalid_handle("invalid web surface handle");
    surface->frame_callback = callback;
    surface->frame_user_data = user_data;
    if (!callback) {
        nk::web::stop_frame_loop();
        return NK_OK;
    }
    if (!nk::web::start_frame_loop(frame_loop, surface.get())) {
        surface->frame_callback = nullptr;
        surface->frame_user_data = nullptr;
        nk::core::set_error("could not start the browser animation-frame loop");
        return NK_ERROR_UNKNOWN;
    }
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_text_input_state(nk_handle handle,
                                                   const nk_text_input_state *state) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!state || state->struct_size < sizeof(nk_text_input_state) || !state->text)
        return invalid_argument("text input state is missing or too small");
    const std::string text = state->text;
    uint32_t codepoints = utf8_codepoints(text);
    const uint64_t text_end = static_cast<uint64_t>(state->text_start) + codepoints;
    const bool no_composition = state->composition_start == NK_TEXT_POSITION_NONE &&
                                state->composition_end == NK_TEXT_POSITION_NONE;
    const bool valid_composition = state->composition_start != NK_TEXT_POSITION_NONE &&
                                   state->composition_end != NK_TEXT_POSITION_NONE &&
                                   state->composition_start <= state->composition_end &&
                                   state->composition_start >= state->text_start &&
                                   state->composition_end <= text_end;
    const bool valid_cursor = std::isfinite(state->cursor_x) &&
                              std::isfinite(state->cursor_y) &&
                              std::isfinite(state->cursor_width) &&
                              std::isfinite(state->cursor_height) &&
                              state->cursor_width >= 0.0f && state->cursor_height >= 0.0f;
    if (text_end > std::numeric_limits<nk_text_position>::max() ||
        state->text_start > state->document_length || text_end > state->document_length ||
        state->selection_start > state->selection_end ||
        state->selection_start < state->text_start || state->selection_end > text_end ||
        (!no_composition && !valid_composition) || state->input_type > NK_TEXT_INPUT_PASSWORD ||
        (state->flags & ~(NK_TEXT_INPUT_MULTILINE | NK_TEXT_INPUT_AUTOCORRECT |
                          NK_TEXT_INPUT_CAPITALIZE_SENTENCES)) != 0 ||
        state->action > NK_TEXT_INPUT_ACTION_NONE || !valid_cursor)
        return invalid_argument("text input ranges are inconsistent with the supplied text");
    auto surface = get_surface(handle);
    if (!surface)
        return invalid_handle("invalid web text-input surface handle");
    surface->text_input_text = text;
    surface->text_input_state = *state;
    surface->text_input_state.text = surface->text_input_text.c_str();
    surface->text_input_state_set = true;
    if (surface->text_input_active)
        configure_text_input(*surface);
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_text_input_active(nk_handle handle, nk_bool active) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (active > 1)
        return invalid_argument("text input active state must be zero or one");
    auto surface = get_surface(handle);
    if (!surface)
        return invalid_handle("invalid web text-input surface handle");
    auto window = get_window(surface->parent);
    if (!window)
        return invalid_handle("invalid web text-input window handle");
    if (active) {
        if (window->text_input_surface != NK_INVALID_HANDLE &&
            window->text_input_surface != surface->handle) {
            auto previous = get_surface(window->text_input_surface);
            if (previous) {
                previous->text_input_active = false;
                configure_text_input(*previous);
            }
        }
        window->text_input_surface = surface->handle;
        surface->text_input_active = true;
        configure_text_input(*surface);
    } else {
        surface->text_input_active = false;
        if (window->text_input_surface == surface->handle) {
            window->text_input_surface = NK_INVALID_HANDLE;
            configure_text_input(*surface);
        }
    }
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_framebuffer_size(nk_handle handle, int32_t *out_width,
                                                  int32_t *out_height) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_width || !out_height)
        return invalid_argument("web surface framebuffer output is null");
    auto surface = get_surface(handle);
    if (!surface)
        return invalid_handle("invalid web surface handle");
    if (auto window = get_window(surface->parent))
        sync_canvas_size(*window);
    *out_width = surface->framebuffer_width;
    *out_height = surface->framebuffer_height;
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_frame_target(nk_handle handle,
                                              nk_surface_frame_target *out_target) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_target || out_target->struct_size < sizeof(nk_surface_frame_target))
        return invalid_argument("invalid web surface frame target output");
    auto surface = get_surface(handle);
    if (!surface)
        return invalid_handle("invalid web surface handle");
    const auto size = out_target->struct_size;
    *out_target = {size, NK_GRAPHICS_OPENGL_ES, surface->framebuffer_width,
                   surface->framebuffer_height, 0, {0, 0}};
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_proc_address(nk_handle, const char *, nk_graphics_proc *) {
    return unsupported("WebGL functions are linked by Emscripten and have no proc-address table");
}

nk_result NK_CALL nk_key_get_state(nk_handle handle, nk_key key, nk_input_action *out_action) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_action || key > NK_KEY_LAST)
        return invalid_argument("invalid web key-state output");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web input window handle");
    *out_action = window->keys[key];
    return NK_OK;
}

nk_result NK_CALL nk_pointer_button_get_state(nk_handle handle, nk_pointer_button button,
                                              nk_input_action *out_action) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_action || button > NK_POINTER_BUTTON_LAST)
        return invalid_argument("invalid web pointer-state output");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web input window handle");
    *out_action = window->buttons[button];
    return NK_OK;
}

nk_result NK_CALL nk_pointer_get_position(nk_handle handle, double *out_x, double *out_y) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_x || !out_y)
        return invalid_argument("invalid web pointer-position output");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web input window handle");
    *out_x = window->pointer_x;
    *out_y = window->pointer_y;
    return NK_OK;
}

nk_result NK_CALL nk_cursor_create_standard(nk_cursor_shape shape, nk_handle *out_cursor) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_cursor)
        return invalid_argument("web cursor output is null");
    *out_cursor = NK_INVALID_HANDLE;
    if (!cursor_name(shape))
        return invalid_argument("invalid standard cursor shape");
    auto cursor = std::make_shared<WebCursorResource>();
    cursor->shape = shape;
    cursor->handle = nk::core::handles().insert(nk::core::ResourceType::cursor, cursor);
    if (cursor->handle == NK_INVALID_HANDLE)
        return invalid_argument("web cursor handle registry is full");
    *out_cursor = cursor->handle;
    return NK_OK;
}
nk_result NK_CALL nk_cursor_create_custom(const nk_cursor_image *, nk_handle *) {
    return unsupported("custom browser cursors are not implemented yet");
}
nk_result NK_CALL nk_cursor_destroy(nk_handle handle) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!get_resource<WebCursorResource>(handle, nk::core::ResourceType::cursor,
                                         "invalid web cursor handle"))
        return invalid_handle("invalid web cursor handle");
    return nk::core::handles().erase(handle, nk::core::ResourceType::cursor) ? NK_OK
                                                                              : NK_ERROR_INVALID_HANDLE;
}
nk_result NK_CALL nk_window_set_cursor(nk_handle window_handle, nk_handle cursor_handle) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    auto window = get_window(window_handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    std::shared_ptr<WebCursorResource> cursor;
    if (cursor_handle != NK_INVALID_HANDLE) {
        cursor = get_resource<WebCursorResource>(cursor_handle, nk::core::ResourceType::cursor,
                                                 "invalid web cursor handle");
        if (!cursor)
            return invalid_handle("invalid web cursor handle");
    }
    window->cursor = std::move(cursor);
    apply_cursor(*window);
    return NK_OK;
}
nk_result NK_CALL nk_window_set_cursor_mode(nk_handle handle, nk_cursor_mode mode) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (mode > NK_CURSOR_MODE_DISABLED)
        return invalid_argument("invalid web cursor mode");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    if (mode == NK_CURSOR_MODE_NORMAL || mode == NK_CURSOR_MODE_HIDDEN) {
        if (window->cursor_mode == NK_CURSOR_MODE_CAPTURED ||
            window->cursor_mode == NK_CURSOR_MODE_DISABLED)
            nk::web::exit_pointer_lock();
        window->cursor_mode = mode;
        apply_cursor(*window);
        return NK_OK;
    }
    if (!nk::web::request_pointer_lock())
        return unsupported("browser pointer lock requires a user gesture and page permission");
    window->cursor_mode = mode;
    apply_cursor(*window);
    return NK_OK;
}
nk_result NK_CALL nk_window_get_cursor_mode(nk_handle handle, nk_cursor_mode *out_mode) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_mode)
        return invalid_argument("web cursor mode output is null");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    *out_mode = window->cursor_mode;
    return NK_OK;
}
uint32_t NK_CALL nk_raw_pointer_motion_supported(void) {
    return 0;
}

nk_result NK_CALL nk_window_get_content_scale(nk_handle handle,
                                              nk_window_content_scale *out_scale) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_scale || out_scale->struct_size < sizeof(nk_window_content_scale))
        return invalid_argument("invalid web content-scale output");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    sync_canvas_size(*window);
    const auto size = out_scale->struct_size;
    *out_scale = {size, window->scale, window->scale, 0, {0, 0}};
    return NK_OK;
}

nk_result NK_CALL nk_window_get_position(nk_handle handle, int32_t *out_x, int32_t *out_y) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_x || !out_y)
        return invalid_argument("invalid web window-position output");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    *out_x = window->x;
    *out_y = window->y;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_size(nk_handle handle, int32_t *out_width, int32_t *out_height) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_width || !out_height)
        return invalid_argument("invalid web window-size output");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    sync_canvas_size(*window);
    *out_width = window->width;
    *out_height = window->height;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_framebuffer_size(nk_handle handle, int32_t *out_width,
                                                 int32_t *out_height) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_width || !out_height)
        return invalid_argument("invalid web window framebuffer output");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    sync_canvas_size(*window);
    *out_width = window->framebuffer_width;
    *out_height = window->framebuffer_height;
    return NK_OK;
}

nk_result NK_CALL nk_window_get_frame_extents(nk_handle handle,
                                              nk_window_frame_extents *out_extents) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_extents || out_extents->struct_size < sizeof(nk_window_frame_extents))
        return invalid_argument("invalid web frame-extents output");
    if (!get_window(handle))
        return invalid_handle("invalid web window handle");
    const auto size = out_extents->struct_size;
    *out_extents = {size, 0, 0, 0, 0, 0, {0, 0}};
    return NK_OK;
}

nk_result NK_CALL nk_window_set_aspect_ratio(nk_handle, int32_t, int32_t) {
    return unsupported("browser canvas aspect ratio is controlled by page CSS");
}

nk_result NK_CALL nk_window_set_resizable(nk_handle handle, nk_bool enabled) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    window->resizable = enabled != 0;
    return NK_OK;
}

nk_result NK_CALL nk_window_set_decorated(nk_handle, nk_bool) {
    return unsupported("browser canvas decorations are controlled by the page");
}
nk_result NK_CALL nk_window_set_floating(nk_handle, nk_bool) {
    return unsupported("browser canvases have no window-manager stacking state");
}
nk_result NK_CALL nk_window_set_opacity(nk_handle, float) {
    return unsupported("browser canvas opacity is controlled by page CSS");
}
nk_result NK_CALL nk_window_set_mouse_passthrough(nk_handle, nk_bool) {
    return unsupported("browser canvas pointer behavior is controlled by page CSS");
}

nk_result NK_CALL nk_window_get_hovered(nk_handle handle, nk_bool *out_hovered) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_hovered)
        return invalid_argument("invalid web hovered output");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    *out_hovered = window->hovered ? 1u : 0u;
    return NK_OK;
}

} // extern "C"
