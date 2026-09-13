#include "platform/web/host.h"

#include "core/event_queue.hpp"
#include "core/runtime.hpp"
#include "nativekit_web_config.h"

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>
#include <emscripten/html5_webgl.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

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
    key.type = event_type == EMSCRIPTEN_EVENT_KEYPRESS ? nk::web::KeyEventType::character
               : event_type == EMSCRIPTEN_EVENT_KEYUP  ? nk::web::KeyEventType::up
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
    pointer.type = event_type == EMSCRIPTEN_EVENT_MOUSEDOWN    ? nk::web::PointerEventType::down
                   : event_type == EMSCRIPTEN_EVENT_MOUSEUP    ? nk::web::PointerEventType::up
                   : event_type == EMSCRIPTEN_EVENT_MOUSEENTER ? nk::web::PointerEventType::enter
                   : event_type == EMSCRIPTEN_EVENT_MOUSELEAVE ? nk::web::PointerEventType::leave
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
    const auto type = event_type == EMSCRIPTEN_EVENT_TOUCHSTART    ? nk::web::TouchEventType::begin
                      : event_type == EMSCRIPTEN_EVENT_TOUCHEND    ? nk::web::TouchEventType::end
                      : event_type == EMSCRIPTEN_EVENT_TOUCHCANCEL ? nk::web::TouchEventType::cancel
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
        host_state.callbacks.focus(event_type == EMSCRIPTEN_EVENT_FOCUS, host_state.user_data);
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

EM_JS(void, nk_web_set_document_title, (const char *title),
      { document.title = UTF8ToString(title); });

EM_JS(void, nk_web_set_canvas_cursor, (const char *selector, const char *cursor), {
    const canvas = document.querySelector(UTF8ToString(selector));
    if (canvas)
        canvas.style.cursor = UTF8ToString(cursor);
});

EM_JS(void, nk_web_configure_text_input,
      (const char *selector, int active, int flags, int input_type, int action, const char *text,
       int text_start, int document_length, int selection_start, int selection_end,
       int composition_start, int composition_end, float cursor_x, float cursor_y,
       float cursor_width, float cursor_height),
      {
          const canvas = document.querySelector(UTF8ToString(selector));
          if (!canvas)
              return;

          const id = "__nativekit_text_input";
          const multiline = !!(flags & 1);
          let input = document.getElementById(id);
          const expectedTag = multiline ? "TEXTAREA" : "INPUT";
          if (input &&input.tagName != = expectedTag) {
              input.remove();
              input = null;
          }
          if (!input) {
              input = document.createElement(multiline ? "textarea" : "input");
              input.id = id;
              input.setAttribute("aria-hidden", "true");
              input.tabIndex = -1;
              input.style.position = "fixed";
              input.style.zIndex = "-1";
              input.style.opacity = "0";
              input.style.pointerEvents = "none";
              input.style.border = "0";
              input.style.padding = "0";
              input.style.margin = "0";
              input.style.width = "1px";
              input.style.height = "1px";
              input.style.outline = "none";
              input._nkComposing = false;
              input._nkIgnoreInput = false;
              const emit = (type, value, start, end) = > {
                  if (!input._nkActive || !Module.ccall)
                      return;
                  Module.ccall("nk_web_host_text_input_event", null,
                               [ "number", "string", "number", "number" ],
                               [ type, value || "", start || 0, end || 0 ]);
              };
              input.addEventListener("compositionstart", () = > { input._nkComposing = true; });
              input.addEventListener(
                  "compositionupdate", event = > {
                      input._nkComposing = true;
                      emit(0, event.data || "", 0, 0);
                  });
              input.addEventListener(
                  "compositionend", event = > {
                      input._nkComposing = false;
                      input._nkIgnoreInput = true;
                      if (event.data)
                          emit(1, event.data, 0, 0);
                      else
                          emit(4, "", 0, 0);
                  });
              input.addEventListener(
                  "input", event = > {
                      if (input._nkIgnoreInput) {
                          input._nkIgnoreInput = false;
                          return;
                      }
                      if (input._nkComposing)
                          return;
                      if (event.inputType == = "deleteContentBackward")
                          emit(2, "", 0, 0);
                      else if (event.inputType == = "deleteContentForward")
                          emit(3, "", 0, 0);
                      else if (event.inputType == = "insertLineBreak")
                          emit(1, "\n", 0, 0);
                      else if (event.data != = null)
                          emit(1, event.data, 0, 0);
                  });
              const codePointToUtf16 = (value, position) = > {
                  let index = 0;
                  let count = 0;
                  for (const character of value) {
                      if (count >= position)
                          break;
                      index += character.length;
                      count++;
                  }
                  return index;
              };
              const emitSelection = () = > {
                  if (!input._nkActive)
                      return;
                  const value = input.value;
                  const start = Array.from(value.slice(0, input.selectionStart)).length;
                  const end = Array.from(value.slice(0, input.selectionEnd)).length;
                  emit(5, "", start, end);
              };
              input.addEventListener("select", emitSelection);
              input.addEventListener("keyup", emitSelection);
              input._nkCodePointToUtf16 = codePointToUtf16;
              canvas.parentElement.appendChild(input);
          }

          input._nkActive = !!active;
          input._nkTextStart = text_start;
          input._nkDocumentLength = document_length;
          input._nkCompositionStart = composition_start;
          input._nkCompositionEnd = composition_end;
          input.type = input_type ==
              = 5 ? "password"
                  : input_type == = 1 ? "email"
                                      : input_type ==
                                        = 2 ? "url"
                                            : input_type ==
                                              = 3 ? "number" : input_type == = 4 ? "tel" : "text";
          input.autocomplete = "off";
          input.autocorrect = (flags & 2) ? "on" : "off";
          input.autocapitalize = (flags & 4) ? "sentences" : "off";
          input.enterKeyHint =
              [ "enter", "done", "go", "next", "search", "send", "enter" ][action] || "enter";
          input.inputMode = input_type ==
              = 3 ? "decimal"
                  : input_type ==
                    = 4 ? "tel" : input_type == = 1 ? "email" : input_type == = 2 ? "url" : "text";
          input.value = UTF8ToString(text);
          input.style.left = Math.max(0, cursor_x) + "px";
          input.style.top = Math.max(0, cursor_y) + "px";
          input.style.width = Math.max(1, cursor_width) + "px";
          input.style.height = Math.max(1, cursor_height) + "px";
          const toUtf16 = input._nkCodePointToUtf16;
          const relativeStart = Math.max(0, selection_start - text_start);
          const relativeEnd = Math.max(relativeStart, selection_end - text_start);
          input.setSelectionRange(toUtf16(input.value, relativeStart),
                                  toUtf16(input.value, relativeEnd));
          if (active) {
              if (document.activeElement != = input)
                  input.focus({preventScroll : true});
          } else if (document.activeElement == = input) {
              input.blur();
          }
      });

EM_JS(int, nk_web_set_clipboard_text, (const char *text), {
    const value = UTF8ToString(text);
    if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(value).catch(() = > {});
        return 1;
    }
    const input = document.createElement("textarea");
    input.value = value;
    input.style.position = "fixed";
    input.style.opacity = "0";
    document.body.appendChild(input);
    input.focus();
    input.select();
    const copied = document.execCommand && document.execCommand("copy");
    input.remove();
    return copied ? 1 : 0;
});

EM_JS(void, nk_web_read_clipboard_text, (double request), {
    const complete = (result, value) = > {
        if (Module.ccall)
            Module.ccall("nk_web_host_clipboard_text_complete", null,
                         [ "number", "number", "string" ], [ request, result, value || "" ]);
    };
    if (!navigator.clipboard || !navigator.clipboard.readText) {
        complete(-4, "");
        return;
    }
    navigator.clipboard.readText()
        .then(value = > complete(0, value))
        .catch(() = > complete(-1, ""));
});

EM_JS(void, nk_web_fetch_resource, (const char *uri, double request), {
    const complete = (result, pointer, size) = > {
        if (Module.ccall)
            Module.ccall("nk_web_host_resource_complete", null,
                         [ "number", "number", "number", "number" ],
                         [ request, result, pointer || 0, size || 0 ]);
    };
    try {
        fetch(UTF8ToString(uri), {credentials : "same-origin"})
            .then(response = >
                             {
                                 if (!response.ok) {
                                     complete(-1, 0, 0);
                                     return;
                                 }
                                 return response.arrayBuffer().then(buffer = > {
                                     const bytes = new Uint8Array(buffer);
                                     if (bytes.length > 0xffffffff) {
                                         complete(-8, 0, 0);
                                         return;
                                     }
                                     const pointer = bytes.length ? _malloc(bytes.length) : 0;
                                     if (pointer)
                                         HEAPU8.set(bytes, pointer);
                                     complete(0, pointer, bytes.length);
                                     if (pointer)
                                         _free(pointer);
                                 });
                             })
            .catch(() = > complete(-1, 0, 0));
    } catch (error) {
        complete(-1, 0, 0);
    }
});

extern "C" EMSCRIPTEN_KEEPALIVE void
nk_web_host_text_input_event(int type, const char *text, int selection_start, int selection_end) {
    if (!host_state.callbacks.text_input)
        return;
    nk::web::TextInputEvent event{};
    event.type = static_cast<nk::web::TextInputEventType>(type);
    event.text = text;
    event.selection_start = selection_start < 0 ? 0u : static_cast<uint32_t>(selection_start);
    event.selection_end = selection_end < 0 ? 0u : static_cast<uint32_t>(selection_end);
    host_state.callbacks.text_input(event, host_state.user_data);
}

extern "C" EMSCRIPTEN_KEEPALIVE void
nk_web_host_clipboard_text_complete(uint32_t request, nk_result result, const char *text) {
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_CLIPBOARD_TEXT_COMPLETE;
    event.request_id = static_cast<nk_request_id>(request);
    event.result = result;
    if (text && *text) {
        const auto length = std::strlen(text);
        const auto *first = reinterpret_cast<const std::byte *>(text);
        event.data.assign(first, first + length);
    }
    nk::core::push_event(std::move(event));
}

extern "C" EMSCRIPTEN_KEEPALIVE void
nk_web_host_resource_complete(uint32_t request, nk_result result, const void *data, uint32_t size) {
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_RESOURCE_DATA_COMPLETE;
    event.request_id = static_cast<nk_request_id>(request);
    event.result = result;
    if (data && size) {
        const auto *first = static_cast<const std::byte *>(data);
        event.data.assign(first, first + size);
    }
    nk::core::push_event(std::move(event));
}

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
    out_size->framebuffer_width = std::max(1, static_cast<int32_t>(std::lround(width * scale)));
    out_size->framebuffer_height = std::max(1, static_cast<int32_t>(std::lround(height * scale)));
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
                                              size.framebuffer_height) == EMSCRIPTEN_RESULT_SUCCESS;
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

void configure_text_input(const TextInputConfig &config) noexcept {
    nk_web_configure_text_input(
        canvas_selector(), config.active ? 1 : 0, static_cast<int>(config.flags),
        static_cast<int>(config.input_type), static_cast<int>(config.action), config.text,
        static_cast<int>(config.text_start), static_cast<int>(config.document_length),
        static_cast<int>(config.selection_start), static_cast<int>(config.selection_end),
        config.composition_start == NK_TEXT_POSITION_NONE
            ? -1
            : static_cast<int>(config.composition_start),
        config.composition_end == NK_TEXT_POSITION_NONE ? -1
                                                        : static_cast<int>(config.composition_end),
        config.cursor_x, config.cursor_y, config.cursor_width, config.cursor_height);
}

bool set_clipboard_text(const char *text) noexcept {
    return text && nk_web_set_clipboard_text(text) != 0;
}

bool read_clipboard_text(nk_request_id request) noexcept {
    nk_web_read_clipboard_text(static_cast<double>(request));
    return true;
}

bool fetch_resource(const char *uri, nk_request_id request) noexcept {
    if (!uri || request == NK_INVALID_REQUEST_ID)
        return false;
    nk_web_fetch_resource(uri, static_cast<double>(request));
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
    return emscripten_request_fullscreen(canvas_selector(), EM_TRUE) == EMSCRIPTEN_RESULT_SUCCESS;
}

bool exit_fullscreen() noexcept {
    return emscripten_exit_fullscreen() == EMSCRIPTEN_RESULT_SUCCESS;
}

bool request_pointer_lock() noexcept {
    return emscripten_request_pointerlock(canvas_selector(), EM_TRUE) == EMSCRIPTEN_RESULT_SUCCESS;
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
    emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE, nullptr);
    emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE, nullptr);
    emscripten_set_keypress_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, &host_state, EM_TRUE, nullptr);
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
