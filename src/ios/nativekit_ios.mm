#include "nativekit_mobile.h"
#include "nativekit_graphics.h"
#include "nativekit_input.h"
#include "nativekit_window.h"

#include "core/event_queue.hpp"
#include "core/error.hpp"
#include "core/graphics_frame_target.hpp"
#include "core/graphics_image_registry.h"
#include "core/handle_registry.hpp"
#include "core/runtime.hpp"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#import <UIKit/UIKit.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

@interface NKIOSHostObserver : NSObject
@property(nonatomic, assign) nk_handle host;
@property(nonatomic, weak) UIView *view;
@end

@interface NKIOSSurfaceTimer : NSObject
@property(nonatomic, assign) nk_handle surface;
- (void)tick:(CADisplayLink *)link;
@end

@interface NKIOSInputView : UITextView
@property(nonatomic, assign) nk_handle surface;
- (void)handleHover:(UIHoverGestureRecognizer *)gesture;
@end

namespace {

template <typename T> std::vector<std::byte> bytes_of(const T &value) {
    const auto *begin = reinterpret_cast<const std::byte *>(&value);
    return std::vector<std::byte>(begin, begin + sizeof(T));
}

struct IOSHost;
struct IOSSurface;
struct IOSTouchState {
    uint32_t pointer_id = 0;
    nk_touch_tool tool = NK_TOUCH_TOOL_FINGER;
    double x = 0;
    double y = 0;
    float pressure = 0;
    float tilt_x = 0;
    float tilt_y = 0;
};
void queue_geometry(const std::shared_ptr<IOSHost> &host);
void update_host_surfaces(const std::shared_ptr<IOSHost> &host);
void reset_surface_input(IOSSurface &surface, uint32_t event_flags = 1u);

struct IOSHost final : nk::core::Resource {
    nk_handle handle = NK_INVALID_HANDLE;
    __strong UIView *view = nil;
    __strong NKIOSHostObserver *observer = nil;
    nk_mobile_lifecycle_state lifecycle = NK_MOBILE_LIFECYCLE_ACTIVE;
    std::vector<nk_handle> surfaces;
};

struct IOSSurface final : nk::core::Resource {
    __strong UIView *host_view = nil;
    __strong CAMetalLayer *layer = nil;
    __strong NKIOSInputView *input_view = nil;
    __strong id<MTLDevice> device = nil;
    __strong id<MTLCommandQueue> queue = nil;
    __strong id<CAMetalDrawable> drawable = nil;
    __strong id<MTLTexture> depth_stencil = nil;
    __strong CADisplayLink *frame_timer = nil;
    __strong NKIOSSurfaceTimer *frame_timer_target = nil;
    nk_handle handle = NK_INVALID_HANDLE;
    nk_handle parent = NK_INVALID_HANDLE;
    nk_handle device_handle = NK_INVALID_HANDLE;
    nk_surface_flags flags = 0;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t framebuffer_width = 0;
    int32_t framebuffer_height = 0;
    std::shared_ptr<IOSSurface> shared_surface;
    uint32_t share_dependents = 0;
    nk_surface_frame_callback frame_callback = nullptr;
    void *frame_user_data = nullptr;
    bool frame_prepared = false;
    bool ready = false;
    bool lost_reported = false;
    bool destroying = false;
    std::array<nk_input_action, NK_KEY_LAST + 1> keys{};
    std::array<nk_input_action, NK_POINTER_BUTTON_LAST + 1> pointer_buttons{};
    double pointer_x = 0;
    double pointer_y = 0;
    std::unordered_map<uintptr_t, IOSTouchState> touch_pointers;
    uint32_t next_touch_pointer_id = 1;
    std::string text_input_text;
    nk_text_input_state text_input_state{};
    bool text_input_active = false;
    bool text_composing = false;
    nk_text_position text_composition_start = NK_TEXT_POSITION_NONE;
    nk_text_position text_composition_end = NK_TEXT_POSITION_NONE;
    std::string marked_text;
    NSRange marked_native_range{NSNotFound, 0};
    bool syncing_input_view = false;

    ~IOSSurface() override {
        input_view.surface = NK_INVALID_HANDLE;
        [input_view removeFromSuperview];
        input_view = nil;
        [frame_timer invalidate];
        frame_timer = nil;
        frame_timer_target = nil;
        drawable = nil;
        if (layer)
            [layer removeFromSuperlayer];
        layer = nil;
        host_view = nil;
    }
};

std::unordered_map<nk_handle, std::shared_ptr<IOSHost>> hosts;
std::unordered_map<nk_handle, std::shared_ptr<IOSSurface>> surfaces;

std::shared_ptr<IOSHost> host(nk_handle handle) {
    return std::dynamic_pointer_cast<IOSHost>(
        nk::core::handles().get(handle, nk::core::ResourceType::mobile_host));
}

std::shared_ptr<IOSSurface> surface(nk_handle handle) {
    const auto found = surfaces.find(handle);
    return found == surfaces.end() ? nullptr : found->second;
}

void queue_input_event(nk_event_kind kind, nk_handle source, std::vector<std::byte> data,
                       uint32_t flags = 0) {
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = source;
    event.flags = flags;
    event.data = std::move(data);
    nk::core::push_event(std::move(event));
}

bool decode_utf8(std::string_view text, std::vector<uint32_t> &codepoints) {
    codepoints.clear();
    for (std::size_t index = 0; index < text.size();) {
        const auto first = static_cast<uint8_t>(text[index]);
        uint32_t value = 0;
        std::size_t count = 0;
        if (first < 0x80) {
            value = first;
            count = 1;
        } else if (first >= 0xc2 && first <= 0xdf) {
            value = first & 0x1fu;
            count = 2;
        } else if (first >= 0xe0 && first <= 0xef) {
            value = first & 0x0fu;
            count = 3;
        } else if (first >= 0xf0 && first <= 0xf4) {
            value = first & 0x07u;
            count = 4;
        } else {
            return false;
        }
        if (index + count > text.size())
            return false;
        for (std::size_t part = 1; part < count; ++part) {
            const auto next = static_cast<uint8_t>(text[index + part]);
            if ((next & 0xc0u) != 0x80u)
                return false;
            value = (value << 6) | (next & 0x3fu);
        }
        if ((count == 2 && value < 0x80) || (count == 3 && value < 0x800) ||
            (count == 4 && value < 0x10000) || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff))
            return false;
        codepoints.push_back(value);
        index += count;
    }
    return true;
}

NSString *native_string(const char *value) {
    return value ? [NSString stringWithUTF8String:value] : nil;
}

std::string utf8_string(NSString *value) {
    if (!value)
        return {};
    const char *bytes = value.UTF8String;
    return bytes ? std::string(bytes) : std::string();
}

NSUInteger utf16_offset_for_codepoint(const std::vector<uint32_t> &codepoints,
                                      uint32_t codepoint_index) {
    NSUInteger result = 0;
    const auto count = std::min<std::size_t>(codepoint_index, codepoints.size());
    for (std::size_t index = 0; index < count; ++index)
        result += codepoints[index] > 0xffff ? 2u : 1u;
    return result;
}

uint32_t codepoint_index_for_utf16(const std::vector<uint32_t> &codepoints,
                                   NSUInteger utf16_index) {
    NSUInteger offset = 0;
    uint32_t result = 0;
    for (const auto codepoint : codepoints) {
        const NSUInteger units = codepoint > 0xffff ? 2u : 1u;
        if (offset + units > utf16_index)
            break;
        offset += units;
        ++result;
    }
    return result;
}

NSRange native_range_for_positions(const IOSSurface &resource, nk_text_position start,
                                   nk_text_position end) {
    std::vector<uint32_t> points;
    decode_utf8(resource.text_input_text, points);
    const auto local_start =
        start == NK_TEXT_POSITION_NONE || start < resource.text_input_state.text_start
            ? 0u
            : start - resource.text_input_state.text_start;
    const auto local_end =
        end == NK_TEXT_POSITION_NONE || end < resource.text_input_state.text_start
            ? local_start
            : end - resource.text_input_state.text_start;
    const NSUInteger start_offset = utf16_offset_for_codepoint(points, local_start);
    const NSUInteger end_offset = utf16_offset_for_codepoint(points, local_end);
    return NSMakeRange(start_offset, end_offset >= start_offset ? end_offset - start_offset : 0);
}

bool codepoint_range_for_native_range(const IOSSurface &resource, NSRange range,
                                      nk_text_position &out_start, nk_text_position &out_end) {
    if (range.location == NSNotFound)
        return false;
    std::vector<uint32_t> points;
    if (!decode_utf8(resource.text_input_text, points))
        return false;
    const NSUInteger total_units = utf16_offset_for_codepoint(points, points.size());
    const uint64_t range_end = static_cast<uint64_t>(range.location) + range.length;
    if (range_end > total_units)
        return false;
    const auto local_start = codepoint_index_for_utf16(points, range.location);
    const auto local_end = codepoint_index_for_utf16(points, static_cast<NSUInteger>(range_end));
    out_start = static_cast<nk_text_position>(resource.text_input_state.text_start + local_start);
    out_end = static_cast<nk_text_position>(resource.text_input_state.text_start + local_end);
    return true;
}

void sync_input_view(IOSSurface &resource) {
    if (!resource.input_view)
        return;
    NSString *text = native_string(resource.text_input_text.c_str());
    if (!text)
        return;
    const auto selection = native_range_for_positions(
        resource, resource.text_input_state.selection_start, resource.text_input_state.selection_end);
    resource.syncing_input_view = true;
    resource.input_view.text = text;
    resource.input_view.selectedRange = selection;
    resource.input_view.hidden = (resource.flags & NK_SURFACE_HIDDEN) != 0;
    resource.syncing_input_view = false;
    [resource.input_view reloadInputViews];
}

void update_text_snapshot(IOSSurface &resource, nk_text_position replace_start,
                          nk_text_position replace_end, std::string_view inserted) {
    std::vector<uint32_t> old_codepoints;
    if (!decode_utf8(resource.text_input_text, old_codepoints) ||
        replace_start < resource.text_input_state.text_start || replace_end < replace_start ||
        static_cast<uint64_t>(replace_end) >
            static_cast<uint64_t>(resource.text_input_state.text_start) + old_codepoints.size())
        return;
    std::vector<uint32_t> inserted_codepoints;
    if (!decode_utf8(inserted, inserted_codepoints))
        return;
    const auto first =
        static_cast<std::size_t>(replace_start - resource.text_input_state.text_start);
    const auto last =
        static_cast<std::size_t>(replace_end - resource.text_input_state.text_start);
    const auto byte_offset = [&](std::size_t codepoint_index) {
        std::size_t bytes = 0;
        for (std::size_t index = 0; index < codepoint_index; ++index) {
            const auto value = old_codepoints[index];
            bytes += value < 0x80 ? 1u : (value < 0x800 ? 2u : (value < 0x10000 ? 3u : 4u));
        }
        return bytes;
    };
    std::string updated = resource.text_input_text;
    updated.replace(byte_offset(first), byte_offset(last) - byte_offset(first), inserted.data(),
                    inserted.size());
    const int64_t delta = static_cast<int64_t>(inserted_codepoints.size()) -
                          static_cast<int64_t>(last - first);
    resource.text_input_text = std::move(updated);
    resource.text_input_state.text = resource.text_input_text.c_str();
    resource.text_input_state.document_length = static_cast<nk_text_position>(std::max<int64_t>(
        0, static_cast<int64_t>(resource.text_input_state.document_length) + delta));
}

void emit_text_edit(IOSSurface &resource, nk_text_edit_event payload,
                    const std::string &text = {}) {
    payload.text_offset = text.empty() ? 0u : sizeof(payload);
    payload.text_length = static_cast<uint32_t>(text.size());
    std::vector<std::byte> data(sizeof(payload) + text.size() + (text.empty() ? 0u : 1u));
    std::memcpy(data.data(), &payload, sizeof(payload));
    if (!text.empty())
        std::memcpy(data.data() + sizeof(payload), text.c_str(), text.size() + 1);
    queue_input_event(NK_EVENT_TEXT_EDIT, resource.handle, std::move(data));
}

void apply_text_edit_state(IOSSurface &resource, nk_text_edit_action action,
                           nk_text_position replace_start, nk_text_position replace_end,
                           const std::string &text, nk_text_position selection_start,
                           nk_text_position selection_end, nk_text_position composition_start,
                           nk_text_position composition_end) {
    if (replace_start != NK_TEXT_POSITION_NONE && replace_end != NK_TEXT_POSITION_NONE)
        update_text_snapshot(resource, replace_start, replace_end, text);
    resource.text_input_state.selection_start = selection_start;
    resource.text_input_state.selection_end = selection_end;
    resource.text_input_state.composition_start = composition_start;
    resource.text_input_state.composition_end = composition_end;
    resource.text_composing = composition_start != NK_TEXT_POSITION_NONE;
    resource.text_composition_start = composition_start;
    resource.text_composition_end = composition_end;
    if (resource.text_composing) {
        resource.marked_text = text;
        resource.marked_native_range =
            native_range_for_positions(resource, composition_start, composition_end);
    } else {
        resource.marked_text.clear();
        resource.marked_native_range = NSMakeRange(NSNotFound, 0);
    }
    sync_input_view(resource);
    nk_text_edit_event payload{};
    payload.action = action;
    payload.replace_start = replace_start;
    payload.replace_end = replace_end;
    payload.selection_start = selection_start;
    payload.selection_end = selection_end;
    payload.composition_start = composition_start;
    payload.composition_end = composition_end;
    emit_text_edit(resource, payload, text);
}

nk_text_position text_replacement_start(const IOSSurface &resource) {
    return resource.text_composing ? resource.text_composition_start
                                   : resource.text_input_state.selection_start;
}

nk_text_position text_replacement_end(const IOSSurface &resource) {
    return resource.text_composing ? resource.text_composition_end
                                   : resource.text_input_state.selection_end;
}

void emit_committed_text(IOSSurface &resource, const std::string &text) {
    std::vector<uint32_t> points;
    if (!decode_utf8(text, points))
        return;
    if (!resource.text_input_active) {
        for (const auto point : points) {
            const nk_text_input_event payload{point, 0};
            queue_input_event(NK_EVENT_TEXT_INPUT, resource.handle, bytes_of(payload));
        }
        resource.text_composing = false;
        resource.text_composition_start = NK_TEXT_POSITION_NONE;
        resource.text_composition_end = NK_TEXT_POSITION_NONE;
        resource.text_input_state.composition_start = NK_TEXT_POSITION_NONE;
        resource.text_input_state.composition_end = NK_TEXT_POSITION_NONE;
        resource.marked_text.clear();
        resource.marked_native_range = NSMakeRange(NSNotFound, 0);
        return;
    }
    const auto start = text_replacement_start(resource);
    const auto end = text_replacement_end(resource);
    const auto selection = static_cast<nk_text_position>(start + points.size());
    apply_text_edit_state(resource, NK_TEXT_EDIT_COMMIT, start, end, text, selection, selection,
                          NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
}

void finish_text_composition(IOSSurface &resource) {
    if (!resource.text_composing)
        return;
    auto selection_start = resource.text_input_state.selection_start;
    auto selection_end = resource.text_input_state.selection_end;
    if (selection_start == NK_TEXT_POSITION_NONE)
        selection_start = selection_end = resource.text_composition_end;
    apply_text_edit_state(resource, NK_TEXT_EDIT_FINISH_COMPOSITION, NK_TEXT_POSITION_NONE,
                          NK_TEXT_POSITION_NONE, {}, selection_start, selection_end,
                          NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
}

nk_modifiers modifiers_from_native(UIKeyModifierFlags flags) {
    nk_modifiers result = 0;
    if (flags & UIKeyModifierShift)
        result |= NK_MOD_SHIFT;
    if (flags & UIKeyModifierControl)
        result |= NK_MOD_CONTROL;
    if (flags & UIKeyModifierAlternate)
        result |= NK_MOD_ALT;
    if (flags & UIKeyModifierCommand)
        result |= NK_MOD_SUPER;
    if (flags & UIKeyModifierAlphaShift)
        result |= NK_MOD_CAPS_LOCK;
    return result;
}

nk_key key_from_hid(UIKeyboardHIDUsage code) {
    if (code >= UIKeyboardHIDUsageKeyboardA && code <= UIKeyboardHIDUsageKeyboardZ)
        return static_cast<nk_key>(NK_KEY_A + code - UIKeyboardHIDUsageKeyboardA);
    if (code >= UIKeyboardHIDUsageKeyboard1 && code <= UIKeyboardHIDUsageKeyboard0) {
        constexpr nk_key digits[] = {NK_KEY_1, NK_KEY_2, NK_KEY_3, NK_KEY_4, NK_KEY_5,
                                     NK_KEY_6, NK_KEY_7, NK_KEY_8, NK_KEY_9, NK_KEY_0};
        return digits[code - UIKeyboardHIDUsageKeyboard1];
    }
    if (code >= UIKeyboardHIDUsageKeyboardF1 && code <= UIKeyboardHIDUsageKeyboardF12)
        return static_cast<nk_key>(NK_KEY_F1 + code - UIKeyboardHIDUsageKeyboardF1);
    if (code >= UIKeyboardHIDUsageKeyboardF13 && code <= UIKeyboardHIDUsageKeyboardF24)
        return static_cast<nk_key>(NK_KEY_F13 + code - UIKeyboardHIDUsageKeyboardF13);
    switch (code) {
    case UIKeyboardHIDUsageKeyboardSpacebar:
        return NK_KEY_SPACE;
    case UIKeyboardHIDUsageKeyboardHyphen:
        return NK_KEY_MINUS;
    case UIKeyboardHIDUsageKeyboardEqualSign:
        return NK_KEY_EQUAL;
    case UIKeyboardHIDUsageKeyboardOpenBracket:
        return NK_KEY_LEFT_BRACKET;
    case UIKeyboardHIDUsageKeyboardCloseBracket:
        return NK_KEY_RIGHT_BRACKET;
    case UIKeyboardHIDUsageKeyboardBackslash:
        return NK_KEY_BACKSLASH;
    case UIKeyboardHIDUsageKeyboardSemicolon:
        return NK_KEY_SEMICOLON;
    case UIKeyboardHIDUsageKeyboardQuote:
        return NK_KEY_APOSTROPHE;
    case UIKeyboardHIDUsageKeyboardGraveAccentAndTilde:
        return NK_KEY_GRAVE_ACCENT;
    case UIKeyboardHIDUsageKeyboardComma:
        return NK_KEY_COMMA;
    case UIKeyboardHIDUsageKeyboardPeriod:
        return NK_KEY_PERIOD;
    case UIKeyboardHIDUsageKeyboardSlash:
        return NK_KEY_SLASH;
    case UIKeyboardHIDUsageKeyboardReturnOrEnter:
        return NK_KEY_ENTER;
    case UIKeyboardHIDUsageKeyboardEscape:
        return NK_KEY_ESCAPE;
    case UIKeyboardHIDUsageKeyboardDeleteOrBackspace:
        return NK_KEY_BACKSPACE;
    case UIKeyboardHIDUsageKeyboardTab:
        return NK_KEY_TAB;
    case UIKeyboardHIDUsageKeyboardCapsLock:
        return NK_KEY_CAPS_LOCK;
    case UIKeyboardHIDUsageKeyboardPrintScreen:
        return NK_KEY_PRINT_SCREEN;
    case UIKeyboardHIDUsageKeyboardScrollLock:
        return NK_KEY_SCROLL_LOCK;
    case UIKeyboardHIDUsageKeyboardPause:
        return NK_KEY_PAUSE;
    case UIKeyboardHIDUsageKeyboardInsert:
        return NK_KEY_INSERT;
    case UIKeyboardHIDUsageKeyboardHome:
        return NK_KEY_HOME;
    case UIKeyboardHIDUsageKeyboardPageUp:
        return NK_KEY_PAGE_UP;
    case UIKeyboardHIDUsageKeyboardDeleteForward:
        return NK_KEY_DELETE;
    case UIKeyboardHIDUsageKeyboardEnd:
        return NK_KEY_END;
    case UIKeyboardHIDUsageKeyboardPageDown:
        return NK_KEY_PAGE_DOWN;
    case UIKeyboardHIDUsageKeyboardRightArrow:
        return NK_KEY_RIGHT;
    case UIKeyboardHIDUsageKeyboardLeftArrow:
        return NK_KEY_LEFT;
    case UIKeyboardHIDUsageKeyboardDownArrow:
        return NK_KEY_DOWN;
    case UIKeyboardHIDUsageKeyboardUpArrow:
        return NK_KEY_UP;
    case UIKeyboardHIDUsageKeypadNumLock:
        return NK_KEY_NUM_LOCK;
    case UIKeyboardHIDUsageKeypadSlash:
        return NK_KEY_KP_DIVIDE;
    case UIKeyboardHIDUsageKeypadAsterisk:
        return NK_KEY_KP_MULTIPLY;
    case UIKeyboardHIDUsageKeypadHyphen:
        return NK_KEY_KP_SUBTRACT;
    case UIKeyboardHIDUsageKeypadPlus:
        return NK_KEY_KP_ADD;
    case UIKeyboardHIDUsageKeypadEnter:
        return NK_KEY_KP_ENTER;
    case UIKeyboardHIDUsageKeypad1:
        return NK_KEY_KP_1;
    case UIKeyboardHIDUsageKeypad2:
        return NK_KEY_KP_2;
    case UIKeyboardHIDUsageKeypad3:
        return NK_KEY_KP_3;
    case UIKeyboardHIDUsageKeypad4:
        return NK_KEY_KP_4;
    case UIKeyboardHIDUsageKeypad5:
        return NK_KEY_KP_5;
    case UIKeyboardHIDUsageKeypad6:
        return NK_KEY_KP_6;
    case UIKeyboardHIDUsageKeypad7:
        return NK_KEY_KP_7;
    case UIKeyboardHIDUsageKeypad8:
        return NK_KEY_KP_8;
    case UIKeyboardHIDUsageKeypad9:
        return NK_KEY_KP_9;
    case UIKeyboardHIDUsageKeypad0:
        return NK_KEY_KP_0;
    case UIKeyboardHIDUsageKeypadPeriod:
        return NK_KEY_KP_DECIMAL;
    case UIKeyboardHIDUsageKeypadEqualSign:
        return NK_KEY_KP_EQUAL;
    case UIKeyboardHIDUsageKeyboardLeftControl:
        return NK_KEY_LEFT_CONTROL;
    case UIKeyboardHIDUsageKeyboardLeftShift:
        return NK_KEY_LEFT_SHIFT;
    case UIKeyboardHIDUsageKeyboardLeftAlt:
        return NK_KEY_LEFT_ALT;
    case UIKeyboardHIDUsageKeyboardLeftGUI:
        return NK_KEY_LEFT_SUPER;
    case UIKeyboardHIDUsageKeyboardRightControl:
        return NK_KEY_RIGHT_CONTROL;
    case UIKeyboardHIDUsageKeyboardRightShift:
        return NK_KEY_RIGHT_SHIFT;
    case UIKeyboardHIDUsageKeyboardRightAlt:
        return NK_KEY_RIGHT_ALT;
    case UIKeyboardHIDUsageKeyboardRightGUI:
        return NK_KEY_RIGHT_SUPER;
    default:
        return NK_KEY_UNKNOWN;
    }
}

void emit_key_transition(IOSSurface &resource, UIKey *key, nk_input_action action) {
    if (!key)
        return;
    const auto code = key.keyCode;
    const auto normalized = key_from_hid(code);
    const auto modifiers = modifiers_from_native(key.modifierFlags);
    if (normalized != NK_KEY_UNKNOWN)
        resource.keys[normalized] = action == NK_INPUT_RELEASE ? NK_INPUT_RELEASE : NK_INPUT_PRESS;
    const nk_key_event payload{normalized, static_cast<uint32_t>(code), action, modifiers};
    queue_input_event(NK_EVENT_KEY, resource.handle, bytes_of(payload));
}

void emit_pointer_move(IOSSurface &resource, double x, double y) {
    resource.pointer_x = x;
    resource.pointer_y = y;
    const nk_pointer_move_event payload{x, y};
    queue_input_event(NK_EVENT_POINTER_MOVE, resource.handle, bytes_of(payload));
}

void emit_pointer_button(IOSSurface &resource, nk_pointer_button button, nk_input_action action,
                         nk_modifiers modifiers, double x, double y, uint32_t flags = 0) {
    resource.pointer_buttons[button] = action;
    resource.pointer_x = x;
    resource.pointer_y = y;
    const nk_pointer_button_event payload{button, action, modifiers, 0, x, y};
    queue_input_event(NK_EVENT_POINTER_BUTTON, resource.handle, bytes_of(payload), flags);
}

void emit_touch(IOSSurface &resource, const IOSTouchState &touch, nk_touch_action action,
                nk_modifiers modifiers, uint32_t flags = 0) {
    const nk_touch_event payload{touch.pointer_id,
                                 action,
                                 touch.tool,
                                 modifiers,
                                 touch.x,
                                 touch.y,
                                 touch.pressure,
                                 touch.tilt_x,
                                 touch.tilt_y,
                                 0};
    queue_input_event(NK_EVENT_TOUCH, resource.handle, bytes_of(payload), flags);
}

bool is_indirect_pointer(UITouch *touch) {
    return touch.type == UITouchTypeIndirect || touch.type == UITouchTypeIndirectPointer;
}

IOSTouchState touch_state_for(UITouch *touch, UIView *view, uint32_t pointer_id) {
    const CGPoint point = [touch locationInView:view];
    const CGFloat maximum_force = touch.maximumPossibleForce;
    const CGFloat force = touch.force;
    const float pressure = maximum_force > 0 && force > 0
                               ? static_cast<float>(std::min<CGFloat>(1.0, force / maximum_force))
                               : 1.0f;
    IOSTouchState result;
    result.pointer_id = pointer_id;
    result.tool = (touch.type == UITouchTypePencil || touch.type == UITouchTypeStylus)
                      ? NK_TOUCH_TOOL_STYLUS
                      : NK_TOUCH_TOOL_FINGER;
    result.x = point.x;
    result.y = point.y;
    result.pressure = pressure;
    if (result.tool == NK_TOUCH_TOOL_STYLUS) {
        const CGFloat tilt = std::max<CGFloat>(0, 1.5707963267948966 - touch.altitudeAngle);
        const CGFloat azimuth = [touch azimuthAngleInView:view];
        result.tilt_x = static_cast<float>(std::sin(tilt) * std::cos(azimuth));
        result.tilt_y = static_cast<float>(std::sin(tilt) * std::sin(azimuth));
    }
    return result;
}

void emit_touch_transition(NKIOSInputView *view, NSSet<UITouch *> *touches,
                           nk_touch_action action, UIEvent *event) {
    auto resource = surface(view.surface);
    if (!resource || resource->destroying)
        return;
    const auto modifiers = modifiers_from_native(event.modifierFlags);
    for (UITouch *touch in touches) {
        const auto identity = reinterpret_cast<uintptr_t>((__bridge void *)touch);
        auto found = resource->touch_pointers.find(identity);
        if (action == NK_TOUCH_BEGIN) {
            if (found == resource->touch_pointers.end()) {
                uint32_t pointer_id = resource->next_touch_pointer_id++;
                if (!pointer_id)
                    pointer_id = resource->next_touch_pointer_id++;
                found = resource->touch_pointers
                            .emplace(identity, touch_state_for(touch, view, pointer_id))
                            .first;
            }
        } else if (found == resource->touch_pointers.end()) {
            continue;
        }
        found->second = touch_state_for(touch, view, found->second.pointer_id);
        if (is_indirect_pointer(touch)) {
            if (action == NK_TOUCH_MOVE)
                emit_pointer_move(*resource, found->second.x, found->second.y);
            else if (action == NK_TOUCH_BEGIN)
                emit_pointer_button(*resource, NK_POINTER_BUTTON_LEFT, NK_INPUT_PRESS, modifiers,
                                    found->second.x, found->second.y);
            else if (action == NK_TOUCH_END || action == NK_TOUCH_CANCEL)
                emit_pointer_button(*resource, NK_POINTER_BUTTON_LEFT, NK_INPUT_RELEASE, modifiers,
                                    found->second.x, found->second.y,
                                    action == NK_TOUCH_CANCEL ? 1u : 0u);
        } else {
            emit_touch(*resource, found->second, action, modifiers,
                       action == NK_TOUCH_CANCEL ? 1u : 0u);
        }
        if (action == NK_TOUCH_END || action == NK_TOUCH_CANCEL)
            resource->touch_pointers.erase(found);
    }
}

UITextRange *native_text_range(NKIOSInputView *view, NSRange range) {
    if (range.location == NSNotFound)
        return nil;
    UITextPosition *start = [view positionFromPosition:view.beginningOfDocument
                                                  offset:static_cast<NSInteger>(range.location)];
    UITextPosition *end = [view positionFromPosition:start offset:static_cast<NSInteger>(range.length)];
    return start && end ? [view textRangeFromPosition:start toPosition:end] : nil;
}

NSRange native_range_for_text_range(NKIOSInputView *view, UITextRange *range) {
    if (!range)
        return NSMakeRange(NSNotFound, 0);
    const NSInteger start = [view offsetFromPosition:view.beginningOfDocument toPosition:range.start];
    const NSInteger end = [view offsetFromPosition:view.beginningOfDocument toPosition:range.end];
    if (start < 0 || end < start)
        return NSMakeRange(NSNotFound, 0);
    return NSMakeRange(static_cast<NSUInteger>(start), static_cast<NSUInteger>(end - start));
}

void set_input_traits(IOSSurface &resource) {
    if (!resource.input_view)
        return;
    switch (resource.text_input_state.input_type) {
    case NK_TEXT_INPUT_EMAIL:
        resource.input_view.keyboardType = UIKeyboardTypeEmailAddress;
        break;
    case NK_TEXT_INPUT_URL:
        resource.input_view.keyboardType = UIKeyboardTypeURL;
        break;
    case NK_TEXT_INPUT_NUMBER:
        resource.input_view.keyboardType = UIKeyboardTypeNumbersAndPunctuation;
        break;
    case NK_TEXT_INPUT_PHONE:
        resource.input_view.keyboardType = UIKeyboardTypePhonePad;
        break;
    default:
        resource.input_view.keyboardType = UIKeyboardTypeDefault;
        break;
    }
    resource.input_view.secureTextEntry =
        resource.text_input_state.input_type == NK_TEXT_INPUT_PASSWORD;
    resource.input_view.autocorrectionType =
        (resource.text_input_state.flags & NK_TEXT_INPUT_AUTOCORRECT) ? UITextAutocorrectionTypeYes
                                                                      : UITextAutocorrectionTypeNo;
    resource.input_view.autocapitalizationType =
        (resource.text_input_state.flags & NK_TEXT_INPUT_CAPITALIZE_SENTENCES)
            ? UITextAutocapitalizationTypeSentences
            : UITextAutocapitalizationTypeNone;
    if (resource.text_input_state.flags & NK_TEXT_INPUT_MULTILINE) {
        resource.input_view.returnKeyType = UIReturnKeyDefault;
    } else {
        switch (resource.text_input_state.action) {
        case NK_TEXT_INPUT_ACTION_DONE:
            resource.input_view.returnKeyType = UIReturnKeyDone;
            break;
        case NK_TEXT_INPUT_ACTION_GO:
            resource.input_view.returnKeyType = UIReturnKeyGo;
            break;
        case NK_TEXT_INPUT_ACTION_NEXT:
            resource.input_view.returnKeyType = UIReturnKeyNext;
            break;
        case NK_TEXT_INPUT_ACTION_SEARCH:
            resource.input_view.returnKeyType = UIReturnKeySearch;
            break;
        case NK_TEXT_INPUT_ACTION_SEND:
            resource.input_view.returnKeyType = UIReturnKeySend;
            break;
        case NK_TEXT_INPUT_ACTION_NONE:
            resource.input_view.returnKeyType = UIReturnKeyNone;
            break;
        default:
            resource.input_view.returnKeyType = UIReturnKeyDefault;
            break;
        }
    }
    [resource.input_view reloadInputViews];
}

void reset_surface_input(IOSSurface &resource, uint32_t event_flags) {
    if (resource.text_input_active)
        finish_text_composition(resource);
    else {
        resource.text_composing = false;
        resource.text_composition_start = NK_TEXT_POSITION_NONE;
        resource.text_composition_end = NK_TEXT_POSITION_NONE;
        resource.text_input_state.composition_start = NK_TEXT_POSITION_NONE;
        resource.text_input_state.composition_end = NK_TEXT_POSITION_NONE;
        resource.marked_text.clear();
        resource.marked_native_range = NSMakeRange(NSNotFound, 0);
    }
    for (nk_key key = 1; key <= NK_KEY_LAST; ++key) {
        if (resource.keys[key] != NK_INPUT_PRESS)
            continue;
        resource.keys[key] = NK_INPUT_RELEASE;
        const nk_key_event payload{key, 0, NK_INPUT_RELEASE, 0};
        queue_input_event(NK_EVENT_KEY, resource.handle, bytes_of(payload), event_flags);
    }
    for (nk_pointer_button button = 0; button <= NK_POINTER_BUTTON_LAST; ++button) {
        if (resource.pointer_buttons[button] != NK_INPUT_PRESS)
            continue;
        const nk_pointer_button_event payload{button, NK_INPUT_RELEASE, 0, 0,
                                              resource.pointer_x, resource.pointer_y};
        resource.pointer_buttons[button] = NK_INPUT_RELEASE;
        queue_input_event(NK_EVENT_POINTER_BUTTON, resource.handle, bytes_of(payload), event_flags);
    }
    for (const auto &[identity, touch] : resource.touch_pointers) {
        (void)identity;
        emit_touch(resource, touch, NK_TOUCH_CANCEL, 0, event_flags);
    }
    resource.touch_pointers.clear();
}

void stop_observing(const std::shared_ptr<IOSHost> &resource) {
    if (!resource || !resource->view || !resource->observer)
        return;
    [resource->view removeObserver:resource->observer forKeyPath:@"bounds"];
    [resource->view removeObserver:resource->observer forKeyPath:@"safeAreaInsets"];
    [resource->view removeObserver:resource->observer forKeyPath:@"contentScaleFactor"];
    resource->observer.view = nil;
    resource->observer = nil;
}

void queue_geometry(const std::shared_ptr<IOSHost> &resource) {
    if (!resource || !resource->view)
        return;
    const auto view = resource->view;
    const auto bounds = view.bounds;
    const auto insets = view.safeAreaInsets;
    const auto scale = view.contentScaleFactor > 0 ? view.contentScaleFactor : 1.0f;
    const nk_mobile_host_geometry geometry{
        sizeof(nk_mobile_host_geometry),
        static_cast<int32_t>(std::lround(CGRectGetWidth(bounds))),
        static_cast<int32_t>(std::lround(CGRectGetHeight(bounds))),
        static_cast<float>(scale),
        static_cast<int32_t>(std::lround(insets.left)),
        static_cast<int32_t>(std::lround(insets.top)),
        static_cast<int32_t>(std::lround(insets.right)),
        static_cast<int32_t>(std::lround(insets.bottom)),
        0,
        {0, 0}};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_MOBILE_HOST_GEOMETRY_CHANGED;
    event.source = resource->handle;
    event.data = bytes_of(geometry);
    nk::core::push_event(std::move(event));
    update_host_surfaces(resource);
}

void observe_view(const std::shared_ptr<IOSHost> &resource, nk_handle handle) {
    auto *observer = [NKIOSHostObserver new];
    observer.host = handle;
    observer.view = resource->view;
    resource->observer = observer;
    [resource->view addObserver:observer forKeyPath:@"bounds" options:0 context:nullptr];
    [resource->view addObserver:observer forKeyPath:@"safeAreaInsets" options:0 context:nullptr];
    [resource->view addObserver:observer
                     forKeyPath:@"contentScaleFactor"
                        options:0
                        context:nullptr];
}

uint64_t metal_object_token(id object) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>((__bridge void *)object));
}

void emit_surface_lost(IOSSurface &resource) {
    if (resource.lost_reported || resource.destroying)
        return;
    resource.lost_reported = true;
    resource.frame_prepared = false;
    resource.drawable = nil;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_SURFACE_LOST;
    event.source = resource.handle;
    nk::core::push_event(std::move(event));
}

void emit_surface_resize(IOSSurface &resource) {
    const nk_surface_resize_event payload{resource.width, resource.height,
                                          resource.framebuffer_width, resource.framebuffer_height};
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_SURFACE_RESIZE;
    event.source = resource.handle;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

void sync_surface_drawable_size(IOSSurface &resource) {
    if (!resource.host_view || !resource.layer)
        return;
    const CGFloat scale = resource.host_view.contentScaleFactor > 0
                              ? resource.host_view.contentScaleFactor
                              : 1.0;
    const CGSize size = resource.layer.bounds.size;
    resource.layer.contentsScale = scale;
    resource.layer.drawableSize = CGSizeMake(std::max<CGFloat>(0, size.width * scale),
                                             std::max<CGFloat>(0, size.height * scale));
    const int32_t framebuffer_width = static_cast<int32_t>(resource.layer.drawableSize.width);
    const int32_t framebuffer_height = static_cast<int32_t>(resource.layer.drawableSize.height);
    if (resource.framebuffer_width == framebuffer_width &&
        resource.framebuffer_height == framebuffer_height)
        return;
    const bool changed = resource.framebuffer_width != 0 || resource.framebuffer_height != 0;
    resource.framebuffer_width = framebuffer_width;
    resource.framebuffer_height = framebuffer_height;
    resource.depth_stencil = nil;
    resource.drawable = nil;
    resource.frame_prepared = false;
    if (resource.ready && changed)
        emit_surface_resize(resource);
}

bool set_surface_native_bounds(IOSSurface &resource) {
    if (!resource.layer)
        return false;
    resource.layer.frame = CGRectMake(resource.x, resource.y, resource.width, resource.height);
    if (resource.input_view)
        resource.input_view.frame = resource.layer.frame;
    sync_surface_drawable_size(resource);
    return true;
}

void update_host_surfaces(const std::shared_ptr<IOSHost> &resource) {
    if (!resource)
        return;
    for (const nk_handle handle : resource->surfaces)
        if (auto child = surface(handle)) {
            child->host_view = resource->view;
            sync_surface_drawable_size(*child);
        }
}

bool surface_frame_available(const IOSSurface &resource) {
    auto parent = host(resource.parent);
    return resource.layer && resource.device && resource.queue && parent && parent->view &&
           parent->lifecycle == NK_MOBILE_LIFECYCLE_ACTIVE && !parent->view.hidden &&
           !resource.layer.hidden && resource.framebuffer_width > 0 &&
           resource.framebuffer_height > 0;
}

bool ensure_surface_depth_target(IOSSurface &resource, int32_t width, int32_t height) {
    if (!(resource.flags & (NK_SURFACE_DEPTH | NK_SURFACE_STENCIL))) {
        resource.depth_stencil = nil;
        return true;
    }
    if (resource.depth_stencil && resource.depth_stencil.width == static_cast<NSUInteger>(width) &&
        resource.depth_stencil.height == static_cast<NSUInteger>(height))
        return true;
    MTLTextureDescriptor *descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                           width:static_cast<NSUInteger>(width)
                                                          height:static_cast<NSUInteger>(height)
                                                       mipmapped:NO];
    descriptor.usage = MTLTextureUsageRenderTarget;
    descriptor.storageMode = MTLStorageModePrivate;
    resource.depth_stencil = [resource.device newTextureWithDescriptor:descriptor];
    if (!resource.depth_stencil) {
        if (resource.ready)
            emit_surface_lost(resource);
        nk::core::set_error("could not allocate the Metal depth/stencil target");
        return false;
    }
    return true;
}

void frame_tick(nk_handle handle) noexcept {
    nk::core::callback_boundary([&] {
        auto resource = surface(handle);
        if (!resource || !resource->frame_callback || resource->destroying)
            return;
        if (nk_surface_make_current(handle) != NK_OK)
            return;
        auto active = surface(handle);
        if (!active || !active->frame_callback)
            return;
        active->frame_callback(handle, active->framebuffer_width, active->framebuffer_height,
                               active->frame_user_data);
        if (active->frame_prepared)
            nk_surface_present(handle);
    });
}

} // namespace

@implementation NKIOSHostObserver
- (void)observeValueForKeyPath:(NSString *)keyPath
                      ofObject:(id)object
                        change:(NSDictionary<NSKeyValueChangeKey, id> *)change
                       context:(void *)context {
    (void)keyPath;
    (void)object;
    (void)change;
    (void)context;
    if (self.host == NK_INVALID_HANDLE)
        return;
    queue_geometry(host(self.host));
}
@end

@implementation NKIOSSurfaceTimer
- (void)tick:(CADisplayLink *)link {
    (void)link;
    frame_tick(self.surface);
}
@end

@implementation NKIOSInputView
- (instancetype)initWithFrame:(CGRect)frame {
    self = [super initWithFrame:frame];
    if (self) {
        self.backgroundColor = UIColor.clearColor;
        self.textColor = UIColor.clearColor;
        self.tintColor = UIColor.clearColor;
        self.editable = YES;
        self.selectable = NO;
        self.scrollEnabled = NO;
        self.userInteractionEnabled = YES;
        self.multipleTouchEnabled = YES;
        self.textContainerInset = UIEdgeInsetsZero;
        self.textContainer.lineFragmentPadding = 0;
        auto *hover = [[UIHoverGestureRecognizer alloc] initWithTarget:self
                                                                  action:@selector(handleHover:)];
        hover.cancelsTouchesInView = NO;
        [self addGestureRecognizer:hover];
    }
    return self;
}

- (BOOL)canBecomeFirstResponder {
    return YES;
}

- (void)handleHover:(UIHoverGestureRecognizer *)gesture {
    auto resource = surface(self.surface);
    if (!resource || resource->destroying)
        return;
    const CGPoint point = [gesture locationInView:self];
    switch (gesture.state) {
    case UIGestureRecognizerStateBegan: {
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_POINTER_ENTER;
        event.source = resource->handle;
        event.flags = 1u;
        nk::core::push_event(std::move(event));
        emit_pointer_move(*resource, point.x, point.y);
        break;
    }
    case UIGestureRecognizerStateChanged:
        emit_pointer_move(*resource, point.x, point.y);
        break;
    case UIGestureRecognizerStateEnded:
    case UIGestureRecognizerStateCancelled:
    case UIGestureRecognizerStateFailed: {
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_POINTER_ENTER;
        event.source = resource->handle;
        event.flags = 0;
        nk::core::push_event(std::move(event));
        break;
    }
    default:
        break;
    }
}

- (void)touchesBegan:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    emit_touch_transition(self, touches, NK_TOUCH_BEGIN, event);
}

- (void)touchesMoved:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    emit_touch_transition(self, touches, NK_TOUCH_MOVE, event);
}

- (void)touchesEnded:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    emit_touch_transition(self, touches, NK_TOUCH_END, event);
}

- (void)touchesCancelled:(NSSet<UITouch *> *)touches withEvent:(UIEvent *)event {
    emit_touch_transition(self, touches, NK_TOUCH_CANCEL, event);
}

- (void)pressesBegan:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    auto resource = surface(self.surface);
    if (resource && !resource->destroying)
        for (UIPress *press in presses)
            if (press.key)
                emit_key_transition(*resource, press.key,
                                    press.key.isKeyRepeat ? NK_INPUT_REPEAT : NK_INPUT_PRESS);
    [super pressesBegan:presses withEvent:event];
}

- (void)pressesEnded:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    auto resource = surface(self.surface);
    if (resource && !resource->destroying)
        for (UIPress *press in presses)
            if (press.key)
                emit_key_transition(*resource, press.key, NK_INPUT_RELEASE);
    [super pressesEnded:presses withEvent:event];
}

- (void)pressesCancelled:(NSSet<UIPress *> *)presses withEvent:(UIPressesEvent *)event {
    auto resource = surface(self.surface);
    if (resource && !resource->destroying)
        for (UIPress *press in presses)
            if (press.key)
                emit_key_transition(*resource, press.key, NK_INPUT_RELEASE);
    [super pressesCancelled:presses withEvent:event];
}

- (UITextRange *)markedTextRange {
    auto resource = surface(self.surface);
    if (!resource || !resource->text_composing)
        return nil;
    return native_text_range(
        self, native_range_for_positions(*resource, resource->text_composition_start,
                                          resource->text_composition_end));
}

- (void)setMarkedText:(NSString *)markedText selectedRange:(NSRange)selectedRange {
    auto resource = surface(self.surface);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        const std::string text = utf8_string(markedText ?: @"");
        std::vector<uint32_t> points;
        if (!decode_utf8(text, points))
            return;
        NSRange replacement = resource->text_composing
                                  ? native_range_for_positions(*resource,
                                                               resource->text_composition_start,
                                                               resource->text_composition_end)
                                  : self.selectedRange;
        nk_text_position start = 0;
        nk_text_position end = 0;
        if (!codepoint_range_for_native_range(*resource, replacement, start, end)) {
            start = text_replacement_start(*resource);
            end = text_replacement_end(*resource);
        }
        const NSUInteger text_units = utf16_offset_for_codepoint(points, points.size());
        const NSUInteger selected_start =
            selectedRange.location == NSNotFound
                ? text_units
                : std::min<NSUInteger>(selectedRange.location, text_units);
        const NSUInteger selected_end =
            selectedRange.location == NSNotFound
                ? selected_start
                : std::min<NSUInteger>(selectedRange.location + selectedRange.length, text_units);
        const auto selection_start = static_cast<nk_text_position>(
            start + codepoint_index_for_utf16(points, selected_start));
        const auto selection_end = static_cast<nk_text_position>(
            start + codepoint_index_for_utf16(points, selected_end));
        const auto composition_end = static_cast<nk_text_position>(start + points.size());
        if (resource->text_input_active) {
            apply_text_edit_state(*resource, NK_TEXT_EDIT_COMPOSE, start, end, text,
                                  selection_start, selection_end, start, composition_end);
        } else {
            update_text_snapshot(*resource, start, end, text);
            resource->text_composing = true;
            resource->text_composition_start = start;
            resource->text_composition_end = composition_end;
            resource->text_input_state.composition_start = start;
            resource->text_input_state.composition_end = composition_end;
            resource->text_input_state.selection_start = selection_start;
            resource->text_input_state.selection_end = selection_end;
            resource->marked_text = text;
            resource->marked_native_range = NSMakeRange(replacement.location, text_units);
            sync_input_view(*resource);
        }
    });
}

- (void)unmarkText {
    auto resource = surface(self.surface);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        if (resource->text_input_active)
            finish_text_composition(*resource);
        else {
            resource->text_composing = false;
            resource->text_composition_start = NK_TEXT_POSITION_NONE;
            resource->text_composition_end = NK_TEXT_POSITION_NONE;
            resource->text_input_state.composition_start = NK_TEXT_POSITION_NONE;
            resource->text_input_state.composition_end = NK_TEXT_POSITION_NONE;
            resource->marked_text.clear();
            resource->marked_native_range = NSMakeRange(NSNotFound, 0);
        }
    });
}

- (void)insertText:(NSString *)text {
    auto resource = surface(self.surface);
    if (!resource)
        return;
    nk::core::callback_boundary([&] { emit_committed_text(*resource, utf8_string(text ?: @"")); });
}

- (void)deleteBackward {
    auto resource = surface(self.surface);
    if (!resource || !resource->text_input_active)
        return;
    nk::core::callback_boundary([&] {
        nk_text_position replace_start = std::min(resource->text_input_state.selection_start,
                                                   resource->text_input_state.selection_end);
        nk_text_position replace_end = std::max(resource->text_input_state.selection_start,
                                                 resource->text_input_state.selection_end);
        if (replace_start == replace_end && replace_start > resource->text_input_state.text_start)
            --replace_start;
        if (replace_start == replace_end)
            return;
        apply_text_edit_state(*resource, NK_TEXT_EDIT_DELETE, replace_start, replace_end, {},
                              replace_start, replace_start, NK_TEXT_POSITION_NONE,
                              NK_TEXT_POSITION_NONE);
    });
}

- (void)replaceRange:(UITextRange *)range withText:(NSString *)text {
    auto resource = surface(self.surface);
    if (!resource)
        return;
    nk::core::callback_boundary([&] {
        nk_text_position start = 0;
        nk_text_position end = 0;
        if (!codepoint_range_for_native_range(*resource, native_range_for_text_range(self, range),
                                              start, end))
            return;
        const std::string value = utf8_string(text ?: @"");
        std::vector<uint32_t> points;
        if (!decode_utf8(value, points))
            return;
        const auto selection = static_cast<nk_text_position>(start + points.size());
        if (resource->text_input_active)
            apply_text_edit_state(*resource, NK_TEXT_EDIT_COMMIT, start, end, value, selection,
                                  selection, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
        else
            emit_committed_text(*resource, value);
    });
}

- (void)setSelectedTextRange:(UITextRange *)range {
    [super setSelectedTextRange:range];
    auto resource = surface(self.surface);
    if (!resource || resource->syncing_input_view || !resource->text_input_active || !range)
        return;
    nk::core::callback_boundary([&] {
        nk_text_position start = 0;
        nk_text_position end = 0;
        if (!codepoint_range_for_native_range(*resource, native_range_for_text_range(self, range),
                                              start, end) ||
            (start == resource->text_input_state.selection_start &&
             end == resource->text_input_state.selection_end))
            return;
        apply_text_edit_state(*resource, NK_TEXT_EDIT_SET_SELECTION, NK_TEXT_POSITION_NONE,
                              NK_TEXT_POSITION_NONE, {}, start, end,
                              resource->text_input_state.composition_start,
                              resource->text_input_state.composition_end);
    });
}

- (CGRect)firstRectForRange:(UITextRange *)range {
    (void)range;
    auto resource = surface(self.surface);
    if (!resource)
        return CGRectZero;
    return CGRectMake(resource->text_input_state.cursor_x, resource->text_input_state.cursor_y,
                      std::max(1.f, resource->text_input_state.cursor_width),
                      std::max(1.f, resource->text_input_state.cursor_height));
}

- (CGRect)caretRectForPosition:(UITextPosition *)position {
    (void)position;
    auto resource = surface(self.surface);
    if (!resource)
        return CGRectZero;
    return CGRectMake(resource->text_input_state.cursor_x, resource->text_input_state.cursor_y,
                      std::max(1.f, resource->text_input_state.cursor_width),
                      std::max(1.f, resource->text_input_state.cursor_height));
}
@end

namespace nk::backend {

void pump_events() noexcept {}

void shutdown() noexcept {
    for (auto &[handle, resource] : surfaces) {
        resource->destroying = true;
        resource->frame_prepared = false;
        [resource->frame_timer invalidate];
        resource->frame_timer = nil;
        resource->frame_timer_target = nil;
        resource->drawable = nil;
        resource->depth_stencil = nil;
        if (resource->layer)
            [resource->layer removeFromSuperlayer];
        nk::core::handles().erase(handle, nk::core::ResourceType::surface);
    }
    surfaces.clear();
    for (auto &[handle, resource] : hosts) {
        stop_observing(resource);
        nk::core::handles().erase(handle, nk::core::ResourceType::mobile_host);
    }
    hosts.clear();
    nk::core::handles().clear();
}

nk_result mobile_host_attach(const nk_mobile_host_options &options, nk_handle &out_host) {
    if (options.kind != NK_MOBILE_HOST_UIKIT_VIEW || !options.native_view) {
        nk::core::set_error("iOS host requires a UIView");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto *view = (__bridge UIView *)(void *)options.native_view;
    if (![view isKindOfClass:[UIView class]]) {
        nk::core::set_error("native_view is not a UIView");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = std::make_shared<IOSHost>();
    resource->handle = NK_INVALID_HANDLE;
    resource->view = view;
    const auto handle = nk::core::handles().insert(nk::core::ResourceType::mobile_host, resource);
    if (!handle) {
        nk::core::set_error("could not allocate an iOS mobile host handle");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    try {
        resource->handle = handle;
        hosts.emplace(handle, resource);
        observe_view(resource, handle);
    } catch (...) {
        nk::core::handles().erase(handle, nk::core::ResourceType::mobile_host);
        nk::core::set_error("could not retain the iOS mobile host");
        return NK_ERROR_OUT_OF_MEMORY;
    }
    out_host = handle;
    queue_geometry(resource);
    return NK_OK;
}

nk_result mobile_host_destroy(nk_handle handle) {
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    const auto found = hosts.find(handle);
    if (found == hosts.end()) {
        nk::core::set_error("invalid iOS mobile host handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    const auto child_surfaces = found->second->surfaces;
    for (auto iter = child_surfaces.rbegin(); iter != child_surfaces.rend(); ++iter)
        if (surface(*iter)) {
            const auto result = nk_surface_destroy(*iter);
            if (result != NK_OK)
                return result;
        }
    stop_observing(found->second);
    found->second->view = nil;
    hosts.erase(found);
    nk::core::handles().erase(handle, nk::core::ResourceType::mobile_host);
    return NK_OK;
}

nk_result mobile_host_set_lifecycle(nk_handle handle, nk_mobile_lifecycle_state state) {
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = host(handle);
    if (!resource) {
        nk::core::set_error("invalid iOS mobile host handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (state < NK_MOBILE_LIFECYCLE_ACTIVE || state > NK_MOBILE_LIFECYCLE_BACKGROUND) {
        nk::core::set_error("invalid mobile lifecycle state");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    const bool becoming_unavailable = resource->lifecycle == NK_MOBILE_LIFECYCLE_ACTIVE &&
                                      state != NK_MOBILE_LIFECYCLE_ACTIVE;
    resource->lifecycle = state;
    if (becoming_unavailable)
        for (const nk_handle child_handle : resource->surfaces)
            if (auto child = surface(child_handle)) {
                reset_surface_input(*child);
                emit_surface_lost(*child);
            }
    return NK_OK;
}

nk_result mobile_host_dispatch_event(nk_handle handle, const nk_mobile_host_event &) {
    if (!host(handle)) {
        nk::core::set_error("invalid iOS mobile host handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    nk::core::set_error("iOS host events are not implemented yet");
    return NK_ERROR_UNSUPPORTED;
}

nk_result mobile_host_set_drop_enabled(nk_handle handle, bool) {
    if (!host(handle)) {
        nk::core::set_error("invalid iOS mobile host handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    nk::core::set_error("iOS host drops are not implemented yet");
    return NK_ERROR_UNSUPPORTED;
}

} // namespace nk::backend

extern "C" {

nk_result NK_CALL nk_surface_create(nk_handle parent_handle, const nk_surface_options *options,
                                    nk_handle *out_surface) {
    return nk::core::result_boundary(
        "unexpected error while creating an iOS Metal surface", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            constexpr nk_surface_flags supported_flags = NK_SURFACE_HIDDEN | NK_SURFACE_ALPHA |
                                                         NK_SURFACE_DEPTH | NK_SURFACE_STENCIL |
                                                         NK_SURFACE_DEBUG_CONTEXT;
            if (!options || options->struct_size < sizeof(*options) || !out_surface ||
                options->width <= 0 || options->height <= 0 || options->api != NK_GRAPHICS_METAL ||
                (options->flags & ~supported_flags) != 0) {
                nk::core::set_error("invalid iOS Metal surface options");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            *out_surface = NK_INVALID_HANDLE;
            auto parent = host(parent_handle);
            if (!parent)
                return (nk::core::set_error("invalid or stale iOS mobile host handle"),
                        NK_ERROR_INVALID_HANDLE);
            auto shared = options->share_surface ? surface(options->share_surface) : nullptr;
            if (options->share_surface && !shared) {
                nk::core::set_error("invalid shared graphics surface");
                return NK_ERROR_INVALID_HANDLE;
            }
            if (shared && shared->device_handle == NK_INVALID_HANDLE) {
                nk::core::set_error("shared graphics surface has no Metal device");
                return NK_ERROR_INVALID_REQUEST;
            }
            if (shared && shared->share_dependents == UINT32_MAX) {
                nk::core::set_error("graphics surface has too many dependents");
                return NK_ERROR_INVALID_REQUEST;
            }
            parent->surfaces.reserve(parent->surfaces.size() + 1);
            auto resource = std::make_shared<IOSSurface>();
            resource->parent = parent_handle;
            resource->host_view = parent->view;
            resource->flags = options->flags;
            resource->x = options->x;
            resource->y = options->y;
            resource->width = options->width;
            resource->height = options->height;
            resource->shared_surface = shared;
            if (shared) {
                resource->device = shared->device;
                resource->queue = shared->queue;
                resource->device_handle = shared->device_handle;
            } else {
                resource->device = MTLCreateSystemDefaultDevice();
                if (resource->device)
                    resource->queue = [resource->device newCommandQueue];
            }
            if (!resource->device || !resource->queue) {
                nk::core::set_error("could not create an iOS Metal device and command queue");
                return NK_ERROR_UNSUPPORTED;
            }
            resource->layer = [CAMetalLayer layer];
            resource->layer.device = resource->device;
            resource->layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            resource->layer.framebufferOnly = YES;
            resource->layer.opaque = (options->flags & NK_SURFACE_ALPHA) == 0;
            resource->layer.hidden = (options->flags & NK_SURFACE_HIDDEN) != 0;
            resource->input_view = [[NKIOSInputView alloc] initWithFrame:CGRectZero];
            if (!resource->input_view) {
                nk::core::set_error("could not create the iOS input surface");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            resource->input_view.hidden = (options->flags & NK_SURFACE_HIDDEN) != 0;
            [parent->view.layer addSublayer:resource->layer];
            [parent->view addSubview:resource->input_view];
            if (!set_surface_native_bounds(*resource)) {
                nk::core::set_error("could not attach the iOS Metal layer");
                return NK_ERROR_UNKNOWN;
            }
            if (resource->framebuffer_width > 0 && resource->framebuffer_height > 0 &&
                !ensure_surface_depth_target(*resource, resource->framebuffer_width,
                                              resource->framebuffer_height))
                return NK_ERROR_UNSUPPORTED;
            resource->handle = nk::core::handles().insert(nk::core::ResourceType::surface, resource);
            if (!resource->handle) {
                nk::core::set_error("iOS graphics surface handle registry is full");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            if (!resource->device_handle)
                resource->device_handle = resource->handle;
            resource->input_view.surface = resource->handle;
            try {
                surfaces.emplace(resource->handle, resource);
                parent->surfaces.push_back(resource->handle);
            } catch (...) {
                surfaces.erase(resource->handle);
                nk::core::handles().erase(resource->handle, nk::core::ResourceType::surface);
                nk::core::set_error("could not retain the iOS Metal surface");
                return NK_ERROR_OUT_OF_MEMORY;
            }
            if (shared)
                ++shared->share_dependents;
            resource->ready = true;
            nk::core::QueuedEvent event;
            event.kind = NK_EVENT_SURFACE_READY;
            event.source = resource->handle;
            nk::core::push_event(std::move(event));
            *out_surface = resource->handle;
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_destroy(nk_handle handle) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (resource->share_dependents) {
        nk::core::set_error("graphics surface is still shared by another surface");
        return NK_ERROR_INVALID_REQUEST;
    }
    if (nk_core_graphics_device_has_references(nk_graphics_device{resource->device_handle})) {
        nk::core::set_error("graphics surface still owns retained GPU resources");
        return NK_ERROR_INVALID_REQUEST;
    }
    reset_surface_input(*resource);
    [resource->input_view resignFirstResponder];
    resource->destroying = true;
    resource->frame_prepared = false;
    [resource->frame_timer invalidate];
    resource->frame_timer = nil;
    resource->frame_timer_target = nil;
    resource->drawable = nil;
    resource->depth_stencil = nil;
    if (auto parent = host(resource->parent)) {
        auto &children = parent->surfaces;
        children.erase(std::remove(children.begin(), children.end(), handle), children.end());
    }
    if (resource->shared_surface)
        --resource->shared_surface->share_dependents;
    if (resource->layer)
        [resource->layer removeFromSuperlayer];
    surfaces.erase(handle);
    nk::core::handles().erase(handle, nk::core::ResourceType::surface);
    return NK_OK;
}

nk_result NK_CALL nk_surface_show(nk_handle handle, uint32_t visible) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (visible > 1) {
        nk::core::set_error("surface visibility must be zero or one");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    resource->layer.hidden = visible == 0;
    resource->input_view.hidden = visible == 0;
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_bounds(nk_handle handle, int32_t x, int32_t y, int32_t width,
                                        int32_t height) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (width <= 0 || height <= 0) {
        nk::core::set_error("graphics surface dimensions must be positive");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    resource->x = x;
    resource->y = y;
    resource->width = width;
    resource->height = height;
    return set_surface_native_bounds(*resource) ? NK_OK
                                                 : (nk::core::set_error(
                                                       "could not resize the iOS Metal layer"),
                                                    NK_ERROR_UNKNOWN);
}

nk_result NK_CALL nk_surface_make_current(nk_handle handle) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (resource->frame_prepared && resource->drawable)
        return NK_OK;
    sync_surface_drawable_size(*resource);
    if (!surface_frame_available(*resource)) {
        nk::core::set_error("iOS Metal surface has no drawable frame");
        return NK_ERROR_INVALID_REQUEST;
    }
    resource->drawable = [resource->layer nextDrawable];
    if (!resource->drawable) {
        nk::core::set_error("iOS Metal drawable is temporarily unavailable");
        return NK_ERROR_INVALID_REQUEST;
    }
    const int32_t width = static_cast<int32_t>(resource->drawable.texture.width);
    const int32_t height = static_cast<int32_t>(resource->drawable.texture.height);
    if (!ensure_surface_depth_target(*resource, width, height))
        return NK_ERROR_UNKNOWN;
    resource->framebuffer_width = width;
    resource->framebuffer_height = height;
    resource->frame_prepared = true;
    if (resource->lost_reported) {
        resource->lost_reported = false;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_SURFACE_READY;
        event.source = resource->handle;
        nk::core::push_event(std::move(event));
    }
    return NK_OK;
}

nk_result NK_CALL nk_surface_present(nk_handle handle) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    if (!resource->frame_prepared) {
        nk::core::set_error("iOS Metal surface has no prepared frame");
        return NK_ERROR_INVALID_REQUEST;
    }
    resource->drawable = nil;
    resource->frame_prepared = false;
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_frame_callback(nk_handle handle,
                                                nk_surface_frame_callback callback,
                                                void *user_data) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    [resource->frame_timer invalidate];
    resource->frame_timer = nil;
    resource->frame_timer_target = nil;
    resource->frame_callback = callback;
    resource->frame_user_data = callback ? user_data : nullptr;
    if (!callback)
        return NK_OK;
    auto target = [NKIOSSurfaceTimer new];
    target.surface = handle;
    auto timer = [CADisplayLink displayLinkWithTarget:target selector:@selector(tick:)];
    [timer addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    resource->frame_timer_target = target;
    resource->frame_timer = timer;
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_framebuffer_size(nk_handle handle, int32_t *out_width,
                                                  int32_t *out_height) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!out_width || !out_height) {
        nk::core::set_error("framebuffer size outputs must not be null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    sync_surface_drawable_size(*resource);
    if (surface_frame_available(*resource)) {
        *out_width = resource->framebuffer_width;
        *out_height = resource->framebuffer_height;
    } else {
        *out_width = 0;
        *out_height = 0;
    }
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_frame_target(nk_handle handle,
                                              nk_surface_frame_target *out_target) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!nk::core::surface_frame_target_output_valid(out_target)) {
        nk::core::set_error("frame-target output is missing or too small");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    nk_surface_frame_target target{};
    target.struct_size = out_target->struct_size;
    target.api = NK_GRAPHICS_METAL;
    const bool prepared = resource->frame_prepared;
    target.width = prepared ? resource->framebuffer_width : 0;
    target.height = prepared ? resource->framebuffer_height : 0;
    target.native_target = prepared && resource->drawable
                               ? metal_object_token(resource->drawable.texture)
                               : 0;
    target.device.id = resource->device_handle;
    target.native_device = metal_object_token(resource->device);
    target.native_context = metal_object_token(resource->queue);
    target.native_depth_stencil_target = metal_object_token(resource->depth_stencil);
    target.native_present_target = prepared ? metal_object_token(resource->drawable) : 0;
    nk::core::write_surface_frame_target(out_target, target);
    return NK_OK;
}

nk_result NK_CALL nk_surface_get_proc_address(nk_handle handle, const char *name,
                                              nk_graphics_proc *out_proc) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!name || !*name || !out_proc) {
        nk::core::set_error("invalid graphics procedure query");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    *out_proc = nullptr;
    if (!surface(handle)) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    nk::core::set_error("Metal surfaces do not expose GL procedure addresses");
    return NK_ERROR_UNSUPPORTED;
}

nk_result NK_CALL nk_key_get_state(nk_handle handle, nk_key key, nk_input_action *out_action) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!out_action || key == NK_KEY_UNKNOWN || key > NK_KEY_LAST) {
        nk::core::set_error("invalid key state query");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    *out_action = resource->keys[key];
    return NK_OK;
}

nk_result NK_CALL nk_pointer_button_get_state(nk_handle handle, nk_pointer_button button,
                                              nk_input_action *out_action) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!out_action || button > NK_POINTER_BUTTON_LAST) {
        nk::core::set_error("invalid pointer button state query");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    *out_action = resource->pointer_buttons[button];
    return NK_OK;
}

nk_result NK_CALL nk_pointer_get_position(nk_handle handle, double *out_x, double *out_y) {
    nk::core::clear_error();
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    if (!out_x || !out_y) {
        nk::core::set_error("pointer position outputs must not be null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    auto resource = surface(handle);
    if (!resource) {
        nk::core::set_error("invalid or stale iOS graphics surface handle");
        return NK_ERROR_INVALID_HANDLE;
    }
    *out_x = resource->pointer_x;
    *out_y = resource->pointer_y;
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_text_input_state(nk_handle handle,
                                                  const nk_text_input_state *state) {
    return nk::core::result_boundary(
        "unexpected error while setting iOS text input state", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (!state || state->struct_size < sizeof(*state)) {
                nk::core::set_error("iOS text input state is missing or too small");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            const char *text = state->text ? state->text : "";
            NSString *native_text = native_string(text);
            std::vector<uint32_t> points;
            if (!native_text || !decode_utf8(text, points)) {
                nk::core::set_error("iOS text input state text is not valid UTF-8");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            const uint64_t text_end = static_cast<uint64_t>(state->text_start) + points.size();
            const bool no_composition = state->composition_start == NK_TEXT_POSITION_NONE &&
                                        state->composition_end == NK_TEXT_POSITION_NONE;
            const bool valid_composition = state->composition_start != NK_TEXT_POSITION_NONE &&
                                           state->composition_end != NK_TEXT_POSITION_NONE &&
                                           state->composition_start <= state->composition_end &&
                                           state->composition_start >= state->text_start &&
                                           state->composition_end <= text_end;
            const bool valid_cursor =
                std::isfinite(state->cursor_x) && std::isfinite(state->cursor_y) &&
                std::isfinite(state->cursor_width) && std::isfinite(state->cursor_height) &&
                state->cursor_width >= 0.f && state->cursor_height >= 0.f;
            if ((state->flags & ~(NK_TEXT_INPUT_MULTILINE | NK_TEXT_INPUT_AUTOCORRECT |
                                  NK_TEXT_INPUT_CAPITALIZE_SENTENCES)) ||
                state->text_start > state->document_length || text_end > state->document_length ||
                state->selection_start > state->selection_end ||
                state->selection_start < state->text_start || state->selection_end > text_end ||
                (!no_composition && !valid_composition) ||
                state->input_type > NK_TEXT_INPUT_PASSWORD ||
                state->action > NK_TEXT_INPUT_ACTION_NONE || !valid_cursor) {
                nk::core::set_error("iOS text input ranges or hints are invalid");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            auto resource = surface(handle);
            if (!resource) {
                nk::core::set_error("invalid or stale iOS graphics surface handle");
                return NK_ERROR_INVALID_HANDLE;
            }
            resource->text_input_text = text;
            resource->text_input_state = *state;
            resource->text_input_state.text = resource->text_input_text.c_str();
            resource->text_composition_start = state->composition_start;
            resource->text_composition_end = state->composition_end;
            resource->text_composing = !no_composition;
            resource->marked_native_range =
                resource->text_composing
                    ? native_range_for_positions(*resource, state->composition_start,
                                                 state->composition_end)
                    : NSMakeRange(NSNotFound, 0);
            if (resource->text_composing)
                resource->marked_text = utf8_string([native_text
                    substringWithRange:resource->marked_native_range]);
            else
                resource->marked_text.clear();
            set_input_traits(*resource);
            sync_input_view(*resource);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_set_text_input_active(nk_handle handle, uint32_t active) {
    return nk::core::result_boundary(
        "unexpected error while changing iOS text input", [&]() -> nk_result {
            nk::core::clear_error();
            if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
                return thread;
            if (active > 1) {
                nk::core::set_error("text input active state must be zero or one");
                return NK_ERROR_INVALID_ARGUMENT;
            }
            auto resource = surface(handle);
            if (!resource) {
                nk::core::set_error("invalid or stale iOS graphics surface handle");
                return NK_ERROR_INVALID_HANDLE;
            }
            if (active) {
                resource->text_input_active = true;
                set_input_traits(*resource);
                if (![resource->input_view becomeFirstResponder]) {
                    resource->text_input_active = false;
                    nk::core::set_error("iOS could not activate the text input responder");
                    return NK_ERROR_UNKNOWN;
                }
            } else {
                if (resource->text_input_active)
                    finish_text_composition(*resource);
                resource->text_input_active = false;
                [resource->input_view resignFirstResponder];
                if (resource->text_composing) {
                    resource->text_composing = false;
                    resource->text_composition_start = NK_TEXT_POSITION_NONE;
                    resource->text_composition_end = NK_TEXT_POSITION_NONE;
                    resource->text_input_state.composition_start = NK_TEXT_POSITION_NONE;
                    resource->text_input_state.composition_end = NK_TEXT_POSITION_NONE;
                    resource->marked_text.clear();
                    resource->marked_native_range = NSMakeRange(NSNotFound, 0);
                }
            }
            return NK_OK;
        });
}

nk_capabilities NK_CALL nk_get_capabilities(void) {
    return NK_CAP_MOBILE_HOST | NK_CAP_RESOURCE_IO | NK_CAP_METAL_SURFACE | NK_CAP_INPUT;
}
}
