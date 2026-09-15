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
#include <limits>
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

constexpr int k_appearance_supported = 1;
constexpr int k_appearance_dark = 1 << 1;
constexpr int k_appearance_high_contrast = 1 << 2;

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

// Preserve JavaScript operators in the embedded EM_JS bodies.
// clang-format off
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

EM_JS(void, nk_web_configure_text_input,
      (const char *selector, int active, int flags, int input_type, int action,
       const char *text, int text_start, int document_length, int selection_start,
       int selection_end, int composition_start, int composition_end, float cursor_x,
       float cursor_y, float cursor_width, float cursor_height), {
          const canvas = document.querySelector(UTF8ToString(selector));
          if (!canvas)
              return;

          const id = "__nativekit_text_input";
          const multiline = !!(flags & 1);
          let input = document.getElementById(id);
          const expectedTag = multiline ? "TEXTAREA" : "INPUT";
          if (input && input.tagName !== expectedTag) {
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
              const emit = (type, value, start, end) => {
                  if (!input._nkActive || !Module.ccall)
                      return;
                  Module.ccall("nk_web_host_text_input_event", null,
                               ["number", "string", "number", "number"],
                               [type, value || "", start || 0, end || 0]);
              };
              input.addEventListener("compositionstart", () => {
                  input._nkComposing = true;
              });
              input.addEventListener("compositionupdate", event => {
                  input._nkComposing = true;
                  emit(0, event.data || "", 0, 0);
              });
              input.addEventListener("compositionend", event => {
                  input._nkComposing = false;
                  input._nkIgnoreInput = true;
                  if (event.data)
                      emit(1, event.data, 0, 0);
                  else
                      emit(4, "", 0, 0);
              });
              input.addEventListener("input", event => {
                  if (input._nkIgnoreInput) {
                      input._nkIgnoreInput = false;
                      return;
                  }
                  if (input._nkComposing)
                      return;
                  if (event.inputType === "deleteContentBackward")
                      emit(2, "", 0, 0);
                  else if (event.inputType === "deleteContentForward")
                      emit(3, "", 0, 0);
                  else if (event.inputType === "insertLineBreak")
                      emit(1, "\n", 0, 0);
                  else if (event.data !== null)
                      emit(1, event.data, 0, 0);
              });
              const codePointToUtf16 = (value, position) => {
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
              const emitSelection = () => {
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
          input.type = input_type === 5 ? "password"
                     : input_type === 1 ? "email"
                     : input_type === 2 ? "url"
                     : input_type === 3 ? "number"
                     : input_type === 4 ? "tel" : "text";
          input.autocomplete = "off";
          input.autocorrect = (flags & 2) ? "on" : "off";
          input.autocapitalize = (flags & 4) ? "sentences" : "off";
          input.enterKeyHint = ["enter", "done", "go", "next", "search", "send", "enter"][action] || "enter";
          input.inputMode = input_type === 3 ? "decimal"
                         : input_type === 4 ? "tel"
                         : input_type === 1 ? "email"
                         : input_type === 2 ? "url" : "text";
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
              if (document.activeElement !== input)
                  input.focus({preventScroll: true});
          } else if (document.activeElement === input) {
              input.blur();
          }
      });

EM_JS(void, nk_web_set_accessibility_tree,
      (const char *selector, double surface, int width, int height, int visible, double focus,
       const char *json), {
          const canvas = document.querySelector(UTF8ToString(selector));
          if (!canvas)
              return;

          const id = "__nativekit_accessibility_" + String(surface);
          let tree = null;
          try {
              tree = JSON.parse(UTF8ToString(json));
          } catch (error) {
              return;
          }
          if (!tree || !Array.isArray(tree.nodes) || tree.nodes.length === 0) {
              const old = document.getElementById(id);
              if (old) {
                  if (old._nkLayout) {
                      window.removeEventListener("resize", old._nkLayout);
                      window.removeEventListener("scroll", old._nkLayout, true);
                  }
                  if (old._nkResizeObserver)
                      old._nkResizeObserver.disconnect();
                  if (old._nkFocusedElement) {
                      old._nkFocusedElement._nkSuppressFocus = true;
                      old._nkFocusedElement.blur();
                      old._nkFocusedElement._nkSuppressFocus = false;
                  }
                  old.remove();
              }
              return;
          }

          let container = document.getElementById(id);
          if (!container) {
              container = document.createElement("div");
              container.id = id;
              container.setAttribute("role", "group");
              container.setAttribute("aria-label", "NativeKit custom surface");
              container.tabIndex = -1;
              container.style.position = "fixed";
              container.style.zIndex = "2147483646";
              container.style.pointerEvents = "none";
              container.style.opacity = "0";
              container.style.margin = "0";
              container.style.padding = "0";
              container.style.border = "0";
              container.style.outline = "none";
              document.body.appendChild(container);
          }

          const actionIds = tree.actions || {};
          const roleNames = {
              group: "group",
              button: "button",
              checkbox: "checkbox",
              radio: "radio",
              text_field: "textbox",
              link: "link",
              image: "img",
              heading: "heading",
              list: "list",
              list_item: "listitem",
              slider: "slider",
              scroll_area: "region",
              dialog: "dialog",
              menu: "menu",
              menu_bar: "menubar",
              menu_item: "menuitem",
              tab_list: "tablist",
              tab: "tab",
              tab_panel: "tabpanel",
              switch: "switch",
              progress_bar: "progressbar",
              combo_box: "combobox",
              collection: "group",
              collection_item: "listitem",
              grid: "grid",
              row: "row",
              cell: "gridcell",
              column_header: "columnheader",
              row_header: "rowheader",
              tree: "tree",
              tree_item: "treeitem",
              separator: "separator",
              toolbar: "toolbar",
              status: "status",
              alert: "alert"
          };
          const setBoolean = (element, name, value) => {
              if (value)
                  element.setAttribute(name, "true");
              else
                  element.removeAttribute(name);
          };
          const emit = (node, name, value, start, end, granularity) => {
              const action = actionIds[name];
              if (action === undefined || !Module.ccall ||
                  (node.disabled && name !== "focus" && name !== "clear_focus"))
                  return;
              Module.ccall("nk_web_host_accessibility_action", null,
                           ["number", "number", "number", "string", "number", "number", "number"],
                           [surface, node.id, action, value || "", start, end, granularity || 0]);
          };
          const codePointOffset = (value, utf16Offset) =>
              Array.from(value.slice(0, utf16Offset)).length;
          const utf16Offset = (value, codepoints) => {
              let offset = 0;
              let count = 0;
              for (const character of value) {
                  if (count >= codepoints)
                      break;
                  offset += character.length;
                  count++;
              }
              return offset;
          };
          const can = (node, name) => node.actions && node.actions.indexOf(name) >= 0;

          container._nkSurfaceWidth = width;
          container._nkSurfaceHeight = height;
          container._nkActionIds = actionIds;
          container._nkFocus = focus;
          container.replaceChildren();
          const elements = new Map();
          for (const node of tree.nodes) {
              const element = document.createElement(node.role === "text_field"
                                                          ? (node.multiline ? "textarea" : "input")
                                                    : node.role === "button" ? "button" : "div");
              const role = roleNames[node.role];
              if (role)
                  element.setAttribute("role", role);
              element.dataset.nativekitAccessibilityNode = String(node.id);
              element.dataset.nativekitAccessibilityParent = String(node.parent);
              element._nkNode = node;
              element.style.position = "absolute";
              element.style.boxSizing = "border-box";
              element.style.pointerEvents = "none";
              element.style.margin = "0";
              element.style.padding = "0";
              element.style.border = "0";
              element.style.background = "transparent";
              element.style.color = "transparent";
              element.style.outline = "none";
              element.tabIndex = node.focusable || node.canFocus ? 0 : -1;
              if (node.label)
                  element.setAttribute("aria-label", node.label);
              if (node.role === "text_field") {
                  if (element.tagName === "INPUT")
                      element.type = node.password ? "password" : "text";
                  element.value = node.value || "";
                  element.readOnly = !!node.readOnly;
                  if (node.selectionStart !== null && node.selectionEnd !== null) {
                      const start = Math.max(0, node.selectionStart - node.textStart);
                      const end = Math.max(start, node.selectionEnd - node.textStart);
                      element.setSelectionRange(utf16Offset(element.value, start),
                                                utf16Offset(element.value, end));
                  }
              } else if (node.value) {
                  element.textContent = node.value;
              }
              if (node.value && node.role !== "text_field")
                  element.setAttribute("aria-valuetext", node.value);
              if (node.role === "slider" || node.role === "progress_bar") {
                  element.setAttribute("aria-valuenow", String(node.numericValue));
                  element.setAttribute("aria-valuemin", String(node.numericMinimum));
                  element.setAttribute("aria-valuemax", String(node.numericMaximum));
              }
              if (node.role === "checkbox" || node.role === "radio" || node.role === "switch")
                  element.setAttribute("aria-checked", node.checked ? "true" : "false");
              setBoolean(element, "aria-selected", node.selected);
              setBoolean(element, "aria-disabled", node.disabled);
              setBoolean(element, "aria-readonly", node.readOnly);
              setBoolean(element, "aria-multiline", node.multiline);
              setBoolean(element, "aria-expanded", node.expanded);
              setBoolean(element, "aria-modal", node.modal);
              setBoolean(element, "aria-required", node.required);
              setBoolean(element, "aria-invalid", node.invalid);
              setBoolean(element, "aria-busy", node.busy);
              if (node.hasPopup)
                  element.setAttribute("aria-haspopup", "true");
              if (node.orientation)
                  element.setAttribute("aria-orientation", node.orientation);
              if (node.hierarchyLevel)
                  element.setAttribute("aria-level", String(node.hierarchyLevel));
              if (node.positionInSet)
                  element.setAttribute("aria-posinset", String(node.positionInSet));
              if (node.setSize)
                  element.setAttribute("aria-setsize", String(node.setSize));
              if (node.rowCount)
                  element.setAttribute("aria-rowcount", String(node.rowCount));
              if (node.columnCount)
                  element.setAttribute("aria-colcount", String(node.columnCount));
              if (node.rowIndex !== null)
                  element.setAttribute("aria-rowindex", String(node.rowIndex + 1));
              if (node.columnIndex !== null)
                  element.setAttribute("aria-colindex", String(node.columnIndex + 1));
              if (node.rowSpan)
                  element.setAttribute("aria-rowspan", String(node.rowSpan));
              if (node.columnSpan)
                  element.setAttribute("aria-colspan", String(node.columnSpan));
              if (node.role === "status")
                  element.setAttribute("aria-live", "polite");
              else if (node.role === "alert")
                  element.setAttribute("aria-live", "assertive");
              if (node.textRanges && node.textRanges.length)
                  element.dataset.nativekitAccessibilityTextRanges = JSON.stringify(node.textRanges);

              element.addEventListener("focus", () => {
                  if (!element._nkSuppressFocus && can(node, "focus"))
                      emit(node, "focus", "", -1, -1, 0);
              });
              element.addEventListener("click", event => {
                  event.preventDefault();
                  event.stopPropagation();
                  let action = "activate";
                  if ((node.role === "checkbox" || node.role === "switch") && can(node, "toggle"))
                      action = "toggle";
                  else if ((node.role === "radio" || node.role === "tab" ||
                            node.role === "list_item" || node.role === "collection_item") &&
                           can(node, "select"))
                      action = "select";
                  else if (node.role === "tree_item" && node.expanded && can(node, "collapse"))
                      action = "collapse";
                  else if (node.role === "tree_item" && !node.expanded && can(node, "expand"))
                      action = "expand";
                  if (can(node, action))
                      emit(node, action, "", -1, -1, 0);
              });
              element.addEventListener("contextmenu", event => {
                  if (!can(node, "show_context_menu"))
                      return;
                  event.preventDefault();
                  event.stopPropagation();
                  emit(node, "show_context_menu", "", -1, -1, 0);
              });
              element.addEventListener("keydown", event => {
                  if (element.tagName === "BUTTON" || element.tagName === "A")
                      return;
                  if (event.key === "Enter" || event.key === " ") {
                      event.preventDefault();
                      event.stopPropagation();
                      let action = "activate";
                      if ((node.role === "checkbox" || node.role === "switch") && can(node, "toggle"))
                          action = "toggle";
                      else if (node.role === "tree_item" && !node.expanded && can(node, "expand"))
                          action = "expand";
                      if (can(node, action))
                          emit(node, action, "", -1, -1, 0);
                  } else if (event.key === "ArrowUp" || event.key === "ArrowRight") {
                      event.preventDefault();
                      event.stopPropagation();
                      const action = can(node, "increment") ? "increment"
                                    : can(node, "scroll_backward") ? "scroll_backward" : null;
                      if (action)
                          emit(node, action, "", -1, -1, 0);
                  } else if (event.key === "ArrowDown" || event.key === "ArrowLeft") {
                      event.preventDefault();
                      event.stopPropagation();
                      const action = can(node, "decrement") ? "decrement"
                                    : can(node, "scroll_forward") ? "scroll_forward" : null;
                      if (action)
                          emit(node, action, "", -1, -1, 0);
                  } else if (event.key === "PageDown") {
                      event.preventDefault();
                      event.stopPropagation();
                      if (can(node, "scroll_forward"))
                          emit(node, "scroll_forward", "", -1, -1, 0);
                  } else if (event.key === "PageUp") {
                      event.preventDefault();
                      event.stopPropagation();
                      if (can(node, "scroll_backward"))
                          emit(node, "scroll_backward", "", -1, -1, 0);
                  }
              });
              if (node.role === "text_field") {
                  element.addEventListener("input", () =>
                      can(node, "set_value") && emit(node, "set_value", element.value, -1, -1, 0));
                  const emitSelection = () => {
                      const start = node.textStart + codePointOffset(element.value, element.selectionStart);
                      const end = node.textStart + codePointOffset(element.value, element.selectionEnd);
                      if (can(node, "set_selection"))
                          emit(node, "set_selection", "", start, end, 0);
                  };
                  element.addEventListener("select", emitSelection);
                  element.addEventListener("keyup", emitSelection);
              }
              elements.set(String(node.id), element);
          }
          for (const node of tree.nodes) {
              const element = elements.get(String(node.id));
              const parent = node.parent === 0 ? container : elements.get(String(node.parent));
              (parent || container).appendChild(element);
          }

          const layout = () => {
              const rect = canvas.getBoundingClientRect();
              container.style.left = rect.left + "px";
              container.style.top = rect.top + "px";
              container.style.width = rect.width + "px";
              container.style.height = rect.height + "px";
              const scaleX = width > 0 ? rect.width / width : 1;
              const scaleY = height > 0 ? rect.height / height : 1;
              for (const node of tree.nodes) {
                  const element = elements.get(String(node.id));
                  if (!element)
                      continue;
                  element.style.left = node.x * scaleX + "px";
                  element.style.top = node.y * scaleY + "px";
                  element.style.width = Math.max(0, node.width * scaleX) + "px";
                  element.style.height = Math.max(0, node.height * scaleY) + "px";
              }
          };
          if (container._nkLayout) {
              window.removeEventListener("resize", container._nkLayout);
              window.removeEventListener("scroll", container._nkLayout, true);
          }
          if (container._nkResizeObserver)
              container._nkResizeObserver.disconnect();
          container._nkLayout = layout;
          window.addEventListener("resize", layout);
          window.addEventListener("scroll", layout, true);
          if (typeof ResizeObserver !== "undefined") {
              container._nkResizeObserver = new ResizeObserver(layout);
              container._nkResizeObserver.observe(canvas);
          }
          container.style.display = visible ? "" : "none";
          layout();
          const focusElement = elements.get(String(focus));
          const previousFocus = container._nkFocusedElement;
          if (previousFocus && previousFocus !== focusElement) {
              previousFocus._nkSuppressFocus = true;
              previousFocus.blur();
              previousFocus._nkSuppressFocus = false;
          }
          if (focusElement && document.activeElement !== focusElement) {
              focusElement._nkSuppressFocus = true;
              focusElement.focus({preventScroll: true});
              focusElement._nkSuppressFocus = false;
          }
          container._nkFocusedElement = focusElement || null;
      });

EM_JS(void, nk_web_clear_accessibility_tree, (double surface), {
    const id = "__nativekit_accessibility_" + String(surface);
    const container = document.getElementById(id);
    if (!container)
        return;
    if (container._nkLayout) {
        window.removeEventListener("resize", container._nkLayout);
        window.removeEventListener("scroll", container._nkLayout, true);
    }
    if (container._nkResizeObserver)
        container._nkResizeObserver.disconnect();
    if (container._nkFocusedElement) {
        container._nkFocusedElement._nkSuppressFocus = true;
        container._nkFocusedElement.blur();
        container._nkFocusedElement._nkSuppressFocus = false;
    }
    container.remove();
});

EM_JS(void, nk_web_set_accessibility_visible, (double surface, int visible), {
    const container = document.getElementById("__nativekit_accessibility_" + String(surface));
    if (container)
        container.style.display = visible ? "" : "none";
});

EM_JS(int, nk_web_set_clipboard_text, (const char *text), {
    const value = UTF8ToString(text);
    if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(value).catch(() => {});
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
    const complete = (result, value) => {
        if (Module.ccall)
            Module.ccall("nk_web_host_clipboard_text_complete", null,
                         ["number", "number", "string"], [request, result, value || ""]);
    };
    if (!navigator.clipboard || !navigator.clipboard.readText) {
        complete(-4, "");
        return;
    }
    navigator.clipboard.readText().then(value => complete(0, value)).catch(() => complete(-1, ""));
});

EM_JS(void, nk_web_fetch_resource, (const char *uri, double request), {
    const complete = (result, pointer, size) => {
        if (Module.ccall)
            Module.ccall("nk_web_host_resource_complete", null,
                         ["number", "number", "number", "number"],
                         [request, result, pointer || 0, size || 0]);
    };
    try {
        fetch(UTF8ToString(uri), {credentials: "same-origin"}).then(response => {
            if (!response.ok) {
                complete(-1, 0, 0);
                return;
            }
            return response.arrayBuffer().then(buffer => {
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
        }).catch(() => complete(-1, 0, 0));
    } catch (error) {
        complete(-1, 0, 0);
    }
});

EM_JS(int, nk_web_display_orientation_supported, (), {
    return typeof screen !== "undefined" && !!screen.orientation &&
                   typeof screen.orientation.addEventListener === "function"
               ? 1
               : 0;
});

EM_JS(int, nk_web_get_display_orientation,
      (int unknown, int portrait, int portrait_upside_down, int landscape_left,
       int landscape_right), {
          if (typeof screen === "undefined" || !screen.orientation)
              return unknown;
          const orientation = screen.orientation;
          const type = typeof orientation.type === "string" ? orientation.type : "";
          if (type === "portrait-primary")
              return portrait;
          if (type === "portrait-secondary")
              return portrait_upside_down;
          if (type === "landscape-primary")
              return landscape_left;
          if (type === "landscape-secondary")
              return landscape_right;

          const angle = Number(orientation.angle);
          if (!Number.isFinite(angle))
              return unknown;
          const normalized = ((angle % 360) + 360) % 360;
          if (normalized === 0)
              return portrait;
          if (normalized === 90)
              return landscape_left;
          if (normalized === 180)
              return portrait_upside_down;
          if (normalized === 270)
              return landscape_right;
          return unknown;
      });

EM_JS(int, nk_web_install_display_orientation_callback,
      (int unknown, int portrait, int portrait_upside_down, int landscape_left,
       int landscape_right), {
          if (typeof screen === "undefined" || !screen.orientation ||
              typeof screen.orientation.addEventListener !== "function" || !Module.ccall)
              return 0;
          const old = globalThis.__nativekitOrientation;
          if (old && old.target && old.listener)
              old.target.removeEventListener("change", old.listener);

          const target = screen.orientation;
          const orientationCode = () => {
              const type = typeof target.type === "string" ? target.type : "";
              if (type === "portrait-primary")
                  return portrait;
              if (type === "portrait-secondary")
                  return portrait_upside_down;
              if (type === "landscape-primary")
                  return landscape_left;
              if (type === "landscape-secondary")
                  return landscape_right;
              const angle = Number(target.angle);
              if (!Number.isFinite(angle))
                  return unknown;
              const normalized = ((angle % 360) + 360) % 360;
              if (normalized === 0)
                  return portrait;
              if (normalized === 90)
                  return landscape_left;
              if (normalized === 180)
                  return portrait_upside_down;
              if (normalized === 270)
                  return landscape_right;
              return unknown;
          };
          const listener = () => {
              if (Module.ccall)
                  Module.ccall("nk_web_host_display_orientation_changed", null, ["number"],
                               [orientationCode()]);
          };
          target.addEventListener("change", listener);
          globalThis.__nativekitOrientation = {target, listener};
          return 1;
      });

EM_JS(void, nk_web_remove_display_orientation_callback, (), {
    const state = globalThis.__nativekitOrientation;
    if (state && state.target && state.listener)
        state.target.removeEventListener("change", state.listener);
    delete globalThis.__nativekitOrientation;
});

EM_JS(int, nk_web_copy_locale, (char *buffer, int capacity), {
    let value = "";
    if (typeof navigator !== "undefined") {
        if (typeof navigator.language === "string" && navigator.language)
            value = navigator.language;
        else if (Array.isArray(navigator.languages) && navigator.languages.length &&
                 typeof navigator.languages[0] === "string")
            value = navigator.languages[0];
    }
    if (!value)
        return 0;
    const required = lengthBytesUTF8(value) + 1;
    if (!buffer || capacity < required)
        return required;
    stringToUTF8(value, buffer, required);
    return required;
});

EM_JS(int, nk_web_get_appearance_flags, (int supported, int dark, int high_contrast), {
    if (typeof window === "undefined" || typeof window.matchMedia !== "function")
        return 0;
    const darkQuery = window.matchMedia("(prefers-color-scheme: dark)");
    const forcedColorsQuery = window.matchMedia("(forced-colors: active)");
    const contrastQuery = window.matchMedia("(prefers-contrast: more)");
    let result = supported;
    if (darkQuery.matches)
        result |= dark;
    if (forcedColorsQuery.matches || contrastQuery.matches)
        result |= high_contrast;
    return result;
});

EM_JS(int, nk_web_keep_awake_supported, (), {
    return typeof navigator !== "undefined" && !!navigator.wakeLock &&
                   typeof navigator.wakeLock.request === "function"
               ? 1
               : 0;
});

EM_JS(int, nk_web_keep_awake_apply, (int enabled), {
    const key = "__nativekitKeepAwake";
    const state = globalThis[key] || (globalThis[key] = {
        requested: false,
        pending: false,
        sentinel: null,
        visibilityListener: null,
        request: null
    });

    if (!enabled) {
        state.requested = false;
        if (state.visibilityListener) {
            document.removeEventListener("visibilitychange", state.visibilityListener);
            state.visibilityListener = null;
        }
        const sentinel = state.sentinel;
        state.sentinel = null;
        if (sentinel && typeof sentinel.release === "function")
            Promise.resolve(sentinel.release()).catch(() => {});
        return 1;
    }

    if (typeof navigator === "undefined" || !navigator.wakeLock ||
        typeof navigator.wakeLock.request !== "function")
        return 0;

    state.requested = true;
    if (!state.request) {
        state.request = () => {
            if (!state.requested || document.visibilityState !== "visible" ||
                state.pending || state.sentinel)
                return;
            state.pending = true;
            let promise;
            try {
                promise = navigator.wakeLock.request("screen");
            } catch (error) {
                state.pending = false;
                return;
            }
            Promise.resolve(promise).then(sentinel => {
                state.pending = false;
                if (!state.requested) {
                    if (sentinel && typeof sentinel.release === "function")
                        Promise.resolve(sentinel.release()).catch(() => {});
                    return;
                }
                state.sentinel = sentinel;
                if (sentinel && typeof sentinel.addEventListener === "function")
                    sentinel.addEventListener("release", () => {
                        if (state.sentinel === sentinel)
                            state.sentinel = null;
                        if (state.requested)
                            state.request();
                    });
            }).catch(() => {
                state.pending = false;
            });
        };
    }
    if (!state.visibilityListener) {
        state.visibilityListener = () => state.request();
        document.addEventListener("visibilitychange", state.visibilityListener);
    }
    state.request();
    return 1;
});

// clang-format on

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

extern "C" EMSCRIPTEN_KEEPALIVE void nk_web_host_accessibility_action(
    uint32_t surface, uint32_t node, uint32_t action, const char *value, int selection_start,
    int selection_end, int granularity) {
    if (!host_state.callbacks.accessibility_action)
        return;
    nk::web::AccessibilityActionEvent event{};
    event.surface = static_cast<nk_handle>(surface);
    event.node = static_cast<nk_accessibility_node_id>(node);
    event.action = static_cast<nk_accessibility_action>(action);
    event.value = value;
    event.selection_start = selection_start < 0
                                ? NK_ACCESSIBILITY_TEXT_POSITION_NONE
                                : static_cast<nk_accessibility_text_position>(selection_start);
    event.selection_end = selection_end < 0
                              ? NK_ACCESSIBILITY_TEXT_POSITION_NONE
                              : static_cast<nk_accessibility_text_position>(selection_end);
    event.granularity = static_cast<nk_accessibility_text_granularity>(
        granularity < 0 ? 0 : granularity);
    host_state.callbacks.accessibility_action(event, host_state.user_data);
}

extern "C" EMSCRIPTEN_KEEPALIVE void
nk_web_host_display_orientation_changed(int orientation) {
    if (host_state.callbacks.display_orientation)
        host_state.callbacks.display_orientation(static_cast<nk_orientation>(orientation),
                                                 host_state.user_data);
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

void set_accessibility_tree(nk_handle surface, int32_t width, int32_t height, bool visible,
                            nk_accessibility_node_id focus, const char *json) noexcept {
    if (json)
        nk_web_set_accessibility_tree(canvas_selector(), static_cast<double>(surface), width,
                                      height, visible ? 1 : 0, static_cast<double>(focus), json);
}

void clear_accessibility_tree(nk_handle surface) noexcept {
    nk_web_clear_accessibility_tree(static_cast<double>(surface));
}

void set_accessibility_visible(nk_handle surface, bool visible) noexcept {
    nk_web_set_accessibility_visible(static_cast<double>(surface), visible ? 1 : 0);
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

bool display_orientation_supported() noexcept {
    return nk_web_display_orientation_supported() != 0;
}

nk_orientation display_orientation() noexcept {
    return static_cast<nk_orientation>(nk_web_get_display_orientation(
        NK_ORIENTATION_UNKNOWN, NK_ORIENTATION_PORTRAIT, NK_ORIENTATION_PORTRAIT_UPSIDE_DOWN,
        NK_ORIENTATION_LANDSCAPE_LEFT, NK_ORIENTATION_LANDSCAPE_RIGHT));
}

nk_result copy_locale(char *buffer, uint32_t *inout_size) noexcept {
    if (!inout_size)
        return NK_ERROR_INVALID_ARGUMENT;
    const auto capacity = *inout_size;
    const auto js_capacity = std::min<uint32_t>(capacity, std::numeric_limits<int>::max());
    const auto result = nk_web_copy_locale(buffer, static_cast<int>(js_capacity));
    if (result <= 0)
        return NK_ERROR_UNSUPPORTED;
    *inout_size = static_cast<uint32_t>(result);
    return buffer && capacity >= *inout_size ? NK_OK : NK_ERROR_BUFFER_TOO_SMALL;
}

bool appearance_supported() noexcept {
    return nk_web_get_appearance_flags(k_appearance_supported, k_appearance_dark,
                                       k_appearance_high_contrast) != 0;
}

bool get_appearance(nk_system_appearance *out_appearance) noexcept {
    if (!out_appearance)
        return false;
    const auto flags = nk_web_get_appearance_flags(k_appearance_supported, k_appearance_dark,
                                                   k_appearance_high_contrast);
    if (!flags)
        return false;
    out_appearance->color_scheme = (flags & k_appearance_dark) ? NK_COLOR_SCHEME_DARK
                                                                : NK_COLOR_SCHEME_LIGHT;
    out_appearance->high_contrast = (flags & k_appearance_high_contrast) ? 1u : 0u;
    return true;
}

bool keep_awake_supported() noexcept {
    return nk_web_keep_awake_supported() != 0;
}

bool keep_awake_apply(bool enabled) noexcept {
    return nk_web_keep_awake_apply(enabled ? 1 : 0) != 0;
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
    if (host_state.callbacks.display_orientation)
        nk_web_install_display_orientation_callback(
            NK_ORIENTATION_UNKNOWN, NK_ORIENTATION_PORTRAIT,
            NK_ORIENTATION_PORTRAIT_UPSIDE_DOWN, NK_ORIENTATION_LANDSCAPE_LEFT,
            NK_ORIENTATION_LANDSCAPE_RIGHT);
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
    nk_web_remove_display_orientation_callback();
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
