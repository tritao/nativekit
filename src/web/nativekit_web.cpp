#include "nativekit_graphics.h"
#include "nativekit_accessibility.h"
#include "nativekit_clipboard.h"
#include "nativekit_dialog.h"
#include "nativekit_gamepad.h"
#include "nativekit_haptics.h"
#include "nativekit_input.h"
#include "nativekit_joystick.h"
#include "nativekit_notification.h"
#include "nativekit_resource.h"
#include "nativekit_system.h"
#include "nativekit_sensor.h"
#include "nativekit_window.h"

#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/frame_request.hpp"
#include "core/graphics_frame_target.hpp"
#include "core/graphics_image_registry.h"
#include "core/gamepad_events.hpp"
#include "core/haptics_internal.hpp"
#include "core/runtime.hpp"
#include "core/sensor_internal.hpp"
#include "core/task.hpp"
#include "core/resource_events.hpp"
#include "web/gamepad.hpp"
#include "web/host.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

struct WebSurfaceResource;
struct WebGLContextResource;

struct WebCursorResource final : nk::core::Resource {
    nk_handle handle = NK_INVALID_HANDLE;
    nk_cursor_shape shape = NK_CURSOR_ARROW;
    std::string css;
};

struct WebAccessibilityTextRange {
    nk_accessibility_text_position start = 0;
    nk_accessibility_text_position end = 0;
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

struct WebAccessibilityNode {
    nk_accessibility_node_id id = NK_ACCESSIBILITY_ROOT;
    nk_accessibility_node_id parent = NK_ACCESSIBILITY_ROOT;
    uint32_t child_index = 0;
    nk_accessibility_role role = NK_ACCESSIBILITY_GROUP;
    nk_accessibility_states states = 0;
    nk_accessibility_actions actions = 0;
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
    std::string label;
    std::string value;
    double numeric_value = 0;
    double numeric_minimum = 0;
    double numeric_maximum = 0;
    nk_accessibility_text_position text_start = 0;
    nk_accessibility_text_position document_length = 0;
    nk_accessibility_text_position selection_start = NK_ACCESSIBILITY_TEXT_POSITION_NONE;
    nk_accessibility_text_position selection_end = NK_ACCESSIBILITY_TEXT_POSITION_NONE;
    uint32_t set_size = 0;
    uint32_t position_in_set = 0;
    uint32_t row_count = 0;
    uint32_t column_count = 0;
    uint32_t row_index = NK_ACCESSIBILITY_INDEX_NONE;
    uint32_t column_index = NK_ACCESSIBILITY_INDEX_NONE;
    uint32_t row_span = 0;
    uint32_t column_span = 0;
    uint32_t hierarchy_level = 0;
    nk_accessibility_orientation orientation = NK_ACCESSIBILITY_ORIENTATION_UNSPECIFIED;
    std::vector<WebAccessibilityTextRange> text_ranges;
};

struct WebGLContextResource {
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;

    ~WebGLContextResource() {
        if (context)
            nk::web::destroy_webgl_context(context);
    }
};

template <typename T> std::vector<std::byte> bytes_of(const T &value) {
    const auto *first = reinterpret_cast<const std::byte *>(&value);
    return {first, first + sizeof(value)};
}

struct WebWindowResource final : nk::core::Resource {
    nk_handle handle = NK_INVALID_HANDLE;
    uint64_t generation = 0;
    std::string title;
    std::string selector;
    bool owned_canvas = false;
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
    int32_t min_width = 0;
    int32_t min_height = 0;
    int32_t max_width = 0;
    int32_t max_height = 0;
    int32_t aspect_numerator = 0;
    int32_t aspect_denominator = 0;
    float opacity = 1.0f;
    bool mouse_passthrough = false;
    bool drops_enabled = false;
    bool fullscreen = false;
    nk_orientation last_display_orientation = NK_ORIENTATION_UNKNOWN;
    nk_cursor_mode cursor_mode = NK_CURSOR_MODE_NORMAL;
    std::shared_ptr<WebCursorResource> cursor;
    std::shared_ptr<WebGLContextResource> graphics;
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
    std::shared_ptr<WebGLContextResource> graphics;
    nk_surface_frame_callback frame_callback = nullptr;
    void *frame_user_data = nullptr;
    nk::core::FrameRequestState frame_requests;
    bool text_input_active = false;
    bool text_input_state_set = false;
    nk_text_input_state text_input_state{};
    std::string text_input_text;
    std::unordered_map<nk_accessibility_node_id, WebAccessibilityNode> accessibility_nodes;
    nk_accessibility_node_id accessibility_focus = NK_ACCESSIBILITY_ROOT;

    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context() const noexcept {
        return graphics ? graphics->context : 0;
    }
};

struct WebGamepadResource final : nk::core::Resource {
    int32_t index = -1;
    nk_handle handle = NK_INVALID_HANDLE;
    bool standard = false;
    std::string name;
    std::string guid;
    std::array<float, 6> axes{};
    std::array<uint8_t, 17> buttons{};
    std::array<uint8_t, 1> hats{};
    bool seen = false;
};

struct WebResourceStream final : nk::core::Resource {
    std::mutex mutex;
    std::string uri;
    std::vector<std::byte> data;
    uint64_t position = 0;
    uint32_t flags = 0;
};

struct PendingWebResourceWrite {
    std::string uri;
    std::vector<std::byte> data;
};

std::unordered_set<nk_handle> web_windows;
std::unordered_map<int32_t, std::shared_ptr<WebGamepadResource>> web_gamepads;
struct PendingWebResourceDialog {
    nk_dialog_operation operation = NK_DIALOG_OPEN_RESOURCE;
    nk_handle parent = NK_INVALID_HANDLE;
};

std::unordered_map<nk_request_id, PendingWebResourceDialog> pending_resource_dialogs;
std::unordered_set<nk_request_id> pending_notifications;
std::unordered_set<nk_request_id> pending_device_orientation_requests;
std::unordered_set<nk_request_id> pending_sensor_permission_requests;
std::mutex pending_resource_writes_mutex;
std::vector<PendingWebResourceWrite> pending_resource_writes;
nk_orientation last_device_orientation = NK_ORIENTATION_UNKNOWN;

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

nk_result resource_error(nk_result result, const char *message) {
    nk::core::clear_error();
    nk::core::set_error(message);
    return result;
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

std::shared_ptr<WebWindowResource> first_window() {
    for (const auto handle : web_windows) {
        if (auto window = get_window(handle))
            return window;
    }
    return {};
}

std::shared_ptr<WebSurfaceResource> get_surface(nk_handle handle) {
    return get_resource<WebSurfaceResource>(handle, nk::core::ResourceType::surface,
                                            "invalid web surface handle");
}

std::shared_ptr<WebResourceStream> get_resource_stream(nk_handle handle) {
    return get_resource<WebResourceStream>(handle, nk::core::ResourceType::resource_stream,
                                           "invalid web resource stream handle");
}

constexpr std::string_view web_file_handle_uri_prefix = "nativekit-file-handle://";

bool is_web_file_handle_uri(const char *uri) {
    return uri && std::string_view(uri).compare(0, web_file_handle_uri_prefix.size(),
                                                web_file_handle_uri_prefix) == 0;
}

void flush_web_resource_writes() noexcept {
    std::vector<PendingWebResourceWrite> writes;
    {
        std::lock_guard lock(pending_resource_writes_mutex);
        writes.swap(pending_resource_writes);
    }
    for (const auto &write : writes) {
        if (write.data.size() > std::numeric_limits<uint32_t>::max())
            continue;
        nk::web::write_resource(write.uri.c_str(), write.data.data(),
                                static_cast<uint32_t>(write.data.size()));
    }
}

template <typename T>
nk_result copy_web_array(const T *source, std::size_t count, T *output, uint32_t *inout_count) {
    if (!inout_count)
        return invalid_argument("web array count output is null");
    if (count == 0) {
        *inout_count = 0;
        return NK_OK;
    }
    if (!output || *inout_count < count) {
        *inout_count = static_cast<uint32_t>(count);
        return NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::copy(source, source + count, output);
    *inout_count = static_cast<uint32_t>(count);
    return NK_OK;
}

nk_result copy_web_string(const std::string &value, char *buffer, uint32_t *inout_size) {
    if (!inout_size)
        return invalid_argument("web string size output is null");
    const auto required = static_cast<uint32_t>(value.size() + 1);
    if (!buffer || *inout_size < required) {
        *inout_size = required;
        return NK_ERROR_BUFFER_TOO_SMALL;
    }
    std::memcpy(buffer, value.c_str(), required);
    *inout_size = required;
    return NK_OK;
}

std::string web_gamepad_guid(std::string_view id, int32_t index) {
    const std::string descriptor = std::string(id) + "\n" + std::to_string(index);
    const auto hash = [&](uint64_t seed) {
        uint64_t value = seed;
        for (const auto byte : descriptor) {
            value ^= static_cast<unsigned char>(byte);
            value *= UINT64_C(1099511628211);
        }
        return value;
    };
    char result[33]{};
    std::snprintf(result, sizeof(result), "%016llx%016llx",
                  static_cast<unsigned long long>(hash(UINT64_C(1469598103934665603))),
                  static_cast<unsigned long long>(hash(UINT64_C(1099511628211))));
    return result;
}

bool valid_resource_dialog_options(const nk_file_dialog_options *options) {
    if (!options || options->struct_size < sizeof(*options) ||
        (options->flags &
         ~(NK_DIALOG_ALLOW_MULTIPLE | NK_DIALOG_CONFIRM_OVERWRITE | NK_DIALOG_SHOW_HIDDEN)) ||
        (options->filter_count && !options->filters) || !nk::platform::valid_utf8(options->title) ||
        !nk::platform::valid_utf8(options->initial_path) ||
        !nk::platform::valid_utf8(options->suggested_name))
        return false;
    for (uint32_t index = 0; index < options->filter_count; ++index) {
        const auto &filter = options->filters[index];
        if (!filter.patterns || !*filter.patterns || !nk::platform::valid_utf8(filter.name) ||
            !nk::platform::valid_utf8(filter.patterns))
            return false;
    }
    return true;
}

std::string resource_dialog_accept(const nk_file_dialog_options *options) {
    std::string result;
    for (uint32_t index = 0; index < options->filter_count; ++index) {
        if (!result.empty())
            result += ';';
        result += options->filters[index].patterns;
    }
    return result;
}

bool valid_web_uri(const char *uri) {
    if (!uri || !*uri || !nk::platform::valid_utf8(uri))
        return false;
    const auto scheme_end = std::strchr(uri, ':');
    return scheme_end && scheme_end != uri;
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

bool decode_utf8(const char *value, uint32_t *out_count) {
    if (!value)
        value = "";
    const auto *bytes = reinterpret_cast<const unsigned char *>(value);
    uint64_t count = 0;
    for (std::size_t offset = 0; bytes[offset];) {
        const unsigned char first = bytes[offset];
        uint32_t codepoint = 0;
        std::size_t length = 0;
        if (first <= 0x7fu) {
            codepoint = first;
            length = 1;
        } else if (first >= 0xc2u && first <= 0xdfu) {
            codepoint = first & 0x1fu;
            length = 2;
        } else if (first >= 0xe0u && first <= 0xefu) {
            codepoint = first & 0x0fu;
            length = 3;
        } else if (first >= 0xf0u && first <= 0xf4u) {
            codepoint = first & 0x07u;
            length = 4;
        } else {
            return false;
        }
        for (std::size_t index = 1; index < length; ++index) {
            const unsigned char next = bytes[offset + index];
            if ((next & 0xc0u) != 0x80u)
                return false;
            codepoint = (codepoint << 6) | (next & 0x3fu);
        }
        if ((length == 2 && codepoint < 0x80u) || (length == 3 && codepoint < 0x800u) ||
            (length == 4 && codepoint < 0x10000u) || codepoint > 0x10ffffu ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu))
            return false;
        ++count;
        if (count > std::numeric_limits<uint32_t>::max())
            return false;
        offset += length;
    }
    if (out_count)
        *out_count = static_cast<uint32_t>(count);
    return true;
}

constexpr nk_accessibility_states web_accessibility_states =
    NK_ACCESSIBILITY_FOCUSABLE | NK_ACCESSIBILITY_FOCUSED | NK_ACCESSIBILITY_SELECTED |
    NK_ACCESSIBILITY_CHECKED | NK_ACCESSIBILITY_DISABLED | NK_ACCESSIBILITY_READ_ONLY |
    NK_ACCESSIBILITY_MULTILINE | NK_ACCESSIBILITY_PASSWORD | NK_ACCESSIBILITY_EXPANDED |
    NK_ACCESSIBILITY_MODAL | NK_ACCESSIBILITY_REQUIRED | NK_ACCESSIBILITY_INVALID |
    NK_ACCESSIBILITY_BUSY | NK_ACCESSIBILITY_HAS_POPUP;

constexpr nk_accessibility_actions web_accessibility_actions =
    NK_ACCESSIBILITY_CAN_ACTIVATE | NK_ACCESSIBILITY_CAN_FOCUS | NK_ACCESSIBILITY_CAN_SET_VALUE |
    NK_ACCESSIBILITY_CAN_SET_SELECTION | NK_ACCESSIBILITY_CAN_INCREMENT |
    NK_ACCESSIBILITY_CAN_DECREMENT | NK_ACCESSIBILITY_CAN_SCROLL_FORWARD |
    NK_ACCESSIBILITY_CAN_SCROLL_BACKWARD | NK_ACCESSIBILITY_CAN_MOVE_NEXT |
    NK_ACCESSIBILITY_CAN_MOVE_PREVIOUS | NK_ACCESSIBILITY_CAN_TOGGLE | NK_ACCESSIBILITY_CAN_SELECT |
    NK_ACCESSIBILITY_CAN_DESELECT | NK_ACCESSIBILITY_CAN_EXPAND | NK_ACCESSIBILITY_CAN_COLLAPSE |
    NK_ACCESSIBILITY_CAN_DISMISS | NK_ACCESSIBILITY_CAN_SHOW_CONTEXT_MENU |
    NK_ACCESSIBILITY_CAN_SCROLL_INTO_VIEW;

bool copy_web_accessibility_node(
    const nk_accessibility_node &node,
    const std::unordered_map<nk_accessibility_node_id, WebAccessibilityNode> &nodes,
    WebAccessibilityNode &copy) {
    const auto invalid_node = [](const char *message) {
        nk::core::set_error(message);
        return false;
    };
    nk::core::clear_error();
    uint32_t value_codepoints = 0;
    if (!decode_utf8(node.value, &value_codepoints))
        return invalid_node("Web accessibility node value is not valid UTF-8");
    if (!decode_utf8(node.label, nullptr))
        return invalid_node("Web accessibility node label is not valid UTF-8");
    if (node.struct_size < sizeof(node))
        return invalid_node("Web accessibility node struct is too small");
    if (node.id == NK_ACCESSIBILITY_ROOT)
        return invalid_node("Web accessibility node ID is the reserved root ID");
    if (node.role > NK_ACCESSIBILITY_ALERT)
        return invalid_node("Web accessibility node role is invalid");
    if (node.orientation > NK_ACCESSIBILITY_ORIENTATION_VERTICAL)
        return invalid_node("Web accessibility node orientation is invalid");
    if (node.states & ~web_accessibility_states)
        return invalid_node("Web accessibility node has unknown state flags");
    if (node.actions & ~web_accessibility_actions)
        return invalid_node("Web accessibility node has unknown action flags");
    if (!std::isfinite(node.x) || !std::isfinite(node.y) || !std::isfinite(node.width) ||
        !std::isfinite(node.height) || node.width < 0 || node.height < 0)
        return invalid_node("Web accessibility node geometry is invalid");
    if (!std::isfinite(node.numeric_value) || !std::isfinite(node.numeric_minimum) ||
        !std::isfinite(node.numeric_maximum))
        return invalid_node("Web accessibility node numeric metadata is invalid");
    if (node.role == NK_ACCESSIBILITY_SLIDER &&
        (node.numeric_minimum > node.numeric_maximum || node.numeric_value < node.numeric_minimum ||
         node.numeric_value > node.numeric_maximum))
        return invalid_node("Web accessibility slider value is outside its range");

    const uint64_t text_end = static_cast<uint64_t>(node.text_start) + value_codepoints;
    const bool no_selection = node.selection_start == NK_ACCESSIBILITY_TEXT_POSITION_NONE &&
                              node.selection_end == NK_ACCESSIBILITY_TEXT_POSITION_NONE;
    const bool valid_selection = node.selection_start != NK_ACCESSIBILITY_TEXT_POSITION_NONE &&
                                 node.selection_end != NK_ACCESSIBILITY_TEXT_POSITION_NONE &&
                                 node.selection_start <= node.selection_end &&
                                 node.selection_start >= node.text_start &&
                                 node.selection_end <= text_end;
    if (text_end > node.document_length)
        return invalid_node("Web accessibility node value exceeds its document length");
    if (!no_selection && !valid_selection)
        return invalid_node("Web accessibility node selection is invalid");
    if (node.parent_id != NK_ACCESSIBILITY_ROOT && nodes.find(node.parent_id) == nodes.end())
        return invalid_node("Web accessibility node parent is missing");

    auto ancestor = node.parent_id;
    for (std::size_t depth = 0; ancestor != NK_ACCESSIBILITY_ROOT; ++depth) {
        if (ancestor == node.id || depth > nodes.size())
            return invalid_node("Web accessibility node contains an ancestor cycle");
        const auto parent = nodes.find(ancestor);
        if (parent == nodes.end())
            return invalid_node("Web accessibility node ancestor is missing");
        ancestor = parent->second.parent;
    }

    copy.id = node.id;
    copy.parent = node.parent_id;
    copy.child_index = node.child_index;
    copy.role = node.role;
    copy.states = node.states;
    copy.actions = node.actions;
    copy.x = node.x;
    copy.y = node.y;
    copy.width = node.width;
    copy.height = node.height;
    copy.label = node.label ? node.label : "";
    copy.value = node.value ? node.value : "";
    copy.numeric_value = node.numeric_value;
    copy.numeric_minimum = node.numeric_minimum;
    copy.numeric_maximum = node.numeric_maximum;
    copy.text_start = node.text_start;
    copy.document_length = node.document_length;
    copy.selection_start = node.selection_start;
    copy.selection_end = node.selection_end;
    copy.set_size = node.set_size;
    copy.position_in_set = node.position_in_set;
    copy.row_count = node.row_count;
    copy.column_count = node.column_count;
    copy.row_index = node.row_index;
    copy.column_index = node.column_index;
    copy.row_span = node.row_span;
    copy.column_span = node.column_span;
    copy.hierarchy_level = node.hierarchy_level;
    copy.orientation = node.orientation;
    return true;
}

void remove_web_accessibility_descendants(
    std::unordered_map<nk_accessibility_node_id, WebAccessibilityNode> &nodes,
    nk_accessibility_node_id node) {
    std::vector<nk_accessibility_node_id> pending{node};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        for (const auto &[candidate, value] : nodes)
            if (value.parent == pending[index])
                pending.push_back(candidate);
    }
    for (const auto id : pending)
        nodes.erase(id);
}

struct WebAccessibilityActionSpec {
    nk_accessibility_action action;
    nk_accessibility_actions bit;
    const char *name;
};

constexpr WebAccessibilityActionSpec web_accessibility_action_specs[] = {
    {NK_ACCESSIBILITY_ACTION_ACTIVATE, NK_ACCESSIBILITY_CAN_ACTIVATE, "activate"},
    {NK_ACCESSIBILITY_ACTION_FOCUS, NK_ACCESSIBILITY_CAN_FOCUS, "focus"},
    {NK_ACCESSIBILITY_ACTION_CLEAR_FOCUS, NK_ACCESSIBILITY_CAN_FOCUS, "clear_focus"},
    {NK_ACCESSIBILITY_ACTION_SET_VALUE, NK_ACCESSIBILITY_CAN_SET_VALUE, "set_value"},
    {NK_ACCESSIBILITY_ACTION_SET_SELECTION, NK_ACCESSIBILITY_CAN_SET_SELECTION, "set_selection"},
    {NK_ACCESSIBILITY_ACTION_INCREMENT, NK_ACCESSIBILITY_CAN_INCREMENT, "increment"},
    {NK_ACCESSIBILITY_ACTION_DECREMENT, NK_ACCESSIBILITY_CAN_DECREMENT, "decrement"},
    {NK_ACCESSIBILITY_ACTION_SCROLL_FORWARD, NK_ACCESSIBILITY_CAN_SCROLL_FORWARD, "scroll_forward"},
    {NK_ACCESSIBILITY_ACTION_SCROLL_BACKWARD, NK_ACCESSIBILITY_CAN_SCROLL_BACKWARD,
     "scroll_backward"},
    {NK_ACCESSIBILITY_ACTION_MOVE_NEXT, NK_ACCESSIBILITY_CAN_MOVE_NEXT, "move_next"},
    {NK_ACCESSIBILITY_ACTION_MOVE_PREVIOUS, NK_ACCESSIBILITY_CAN_MOVE_PREVIOUS, "move_previous"},
    {NK_ACCESSIBILITY_ACTION_TOGGLE, NK_ACCESSIBILITY_CAN_TOGGLE, "toggle"},
    {NK_ACCESSIBILITY_ACTION_SELECT, NK_ACCESSIBILITY_CAN_SELECT, "select"},
    {NK_ACCESSIBILITY_ACTION_DESELECT, NK_ACCESSIBILITY_CAN_DESELECT, "deselect"},
    {NK_ACCESSIBILITY_ACTION_EXPAND, NK_ACCESSIBILITY_CAN_EXPAND, "expand"},
    {NK_ACCESSIBILITY_ACTION_COLLAPSE, NK_ACCESSIBILITY_CAN_COLLAPSE, "collapse"},
    {NK_ACCESSIBILITY_ACTION_DISMISS, NK_ACCESSIBILITY_CAN_DISMISS, "dismiss"},
    {NK_ACCESSIBILITY_ACTION_SHOW_CONTEXT_MENU, NK_ACCESSIBILITY_CAN_SHOW_CONTEXT_MENU,
     "show_context_menu"},
    {NK_ACCESSIBILITY_ACTION_SCROLL_INTO_VIEW, NK_ACCESSIBILITY_CAN_SCROLL_INTO_VIEW,
     "scroll_into_view"},
};

nk_accessibility_actions web_accessibility_action_bit(nk_accessibility_action action) {
    for (const auto &spec : web_accessibility_action_specs)
        if (spec.action == action)
            return spec.bit;
    return 0;
}

const char *web_accessibility_role_name(nk_accessibility_role role) {
    switch (role) {
    case NK_ACCESSIBILITY_GROUP:
        return "group";
    case NK_ACCESSIBILITY_BUTTON:
        return "button";
    case NK_ACCESSIBILITY_CHECKBOX:
        return "checkbox";
    case NK_ACCESSIBILITY_RADIO:
        return "radio";
    case NK_ACCESSIBILITY_TEXT:
        return "text";
    case NK_ACCESSIBILITY_TEXT_FIELD:
        return "text_field";
    case NK_ACCESSIBILITY_LINK:
        return "link";
    case NK_ACCESSIBILITY_IMAGE:
        return "image";
    case NK_ACCESSIBILITY_HEADING:
        return "heading";
    case NK_ACCESSIBILITY_LIST:
        return "list";
    case NK_ACCESSIBILITY_LIST_ITEM:
        return "list_item";
    case NK_ACCESSIBILITY_SLIDER:
        return "slider";
    case NK_ACCESSIBILITY_SCROLL_AREA:
        return "scroll_area";
    case NK_ACCESSIBILITY_DIALOG:
        return "dialog";
    case NK_ACCESSIBILITY_MENU:
        return "menu";
    case NK_ACCESSIBILITY_MENU_BAR:
        return "menu_bar";
    case NK_ACCESSIBILITY_MENU_ITEM:
        return "menu_item";
    case NK_ACCESSIBILITY_TAB_LIST:
        return "tab_list";
    case NK_ACCESSIBILITY_TAB:
        return "tab";
    case NK_ACCESSIBILITY_TAB_PANEL:
        return "tab_panel";
    case NK_ACCESSIBILITY_SWITCH:
        return "switch";
    case NK_ACCESSIBILITY_PROGRESS_BAR:
        return "progress_bar";
    case NK_ACCESSIBILITY_COMBO_BOX:
        return "combo_box";
    case NK_ACCESSIBILITY_COLLECTION:
        return "collection";
    case NK_ACCESSIBILITY_COLLECTION_ITEM:
        return "collection_item";
    case NK_ACCESSIBILITY_GRID:
        return "grid";
    case NK_ACCESSIBILITY_ROW:
        return "row";
    case NK_ACCESSIBILITY_CELL:
        return "cell";
    case NK_ACCESSIBILITY_COLUMN_HEADER:
        return "column_header";
    case NK_ACCESSIBILITY_ROW_HEADER:
        return "row_header";
    case NK_ACCESSIBILITY_TREE:
        return "tree";
    case NK_ACCESSIBILITY_TREE_ITEM:
        return "tree_item";
    case NK_ACCESSIBILITY_SEPARATOR:
        return "separator";
    case NK_ACCESSIBILITY_TOOLBAR:
        return "toolbar";
    case NK_ACCESSIBILITY_STATUS:
        return "status";
    case NK_ACCESSIBILITY_ALERT:
        return "alert";
    default:
        return "group";
    }
}

const char *web_accessibility_orientation_name(nk_accessibility_orientation orientation) {
    switch (orientation) {
    case NK_ACCESSIBILITY_ORIENTATION_HORIZONTAL:
        return "horizontal";
    case NK_ACCESSIBILITY_ORIENTATION_VERTICAL:
        return "vertical";
    default:
        return nullptr;
    }
}

void append_json_string(std::string &json, const std::string &value) {
    static constexpr char hex[] = "0123456789abcdef";
    json.push_back('"');
    for (const unsigned char character : value) {
        switch (character) {
        case '"':
            json += "\\\"";
            break;
        case '\\':
            json += "\\\\";
            break;
        case '\b':
            json += "\\b";
            break;
        case '\f':
            json += "\\f";
            break;
        case '\n':
            json += "\\n";
            break;
        case '\r':
            json += "\\r";
            break;
        case '\t':
            json += "\\t";
            break;
        default:
            if (character < 0x20u) {
                json += "\\u00";
                json.push_back(hex[character >> 4]);
                json.push_back(hex[character & 0x0fu]);
            } else {
                json.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    json.push_back('"');
}

void append_json_number(std::string &json, double value) {
    json += std::to_string(value);
}

void append_json_position(std::string &json, nk_accessibility_text_position value) {
    if (value == NK_ACCESSIBILITY_TEXT_POSITION_NONE)
        json += "null";
    else
        json += std::to_string(value);
}

void append_json_node(std::string &json, const WebAccessibilityNode &node,
                      nk_accessibility_node_id focus) {
    const auto add_bool = [&](const char *name, bool value) {
        json += ",\"";
        json += name;
        json += value ? "\":true" : "\":false";
    };
    json += "{\"id\":" + std::to_string(node.id);
    json += ",\"parent\":" + std::to_string(node.parent);
    json += ",\"index\":" + std::to_string(node.child_index);
    json += ",\"role\":";
    append_json_string(json, web_accessibility_role_name(node.role));
    add_bool("focusable", (node.states & NK_ACCESSIBILITY_FOCUSABLE) != 0);
    add_bool("focused", node.id == focus || (node.states & NK_ACCESSIBILITY_FOCUSED) != 0);
    add_bool("selected", (node.states & NK_ACCESSIBILITY_SELECTED) != 0);
    add_bool("checked", (node.states & NK_ACCESSIBILITY_CHECKED) != 0);
    add_bool("disabled", (node.states & NK_ACCESSIBILITY_DISABLED) != 0);
    add_bool("readOnly", (node.states & NK_ACCESSIBILITY_READ_ONLY) != 0);
    add_bool("multiline", (node.states & NK_ACCESSIBILITY_MULTILINE) != 0);
    add_bool("password", (node.states & NK_ACCESSIBILITY_PASSWORD) != 0);
    add_bool("expanded", (node.states & NK_ACCESSIBILITY_EXPANDED) != 0);
    add_bool("modal", (node.states & NK_ACCESSIBILITY_MODAL) != 0);
    add_bool("required", (node.states & NK_ACCESSIBILITY_REQUIRED) != 0);
    add_bool("invalid", (node.states & NK_ACCESSIBILITY_INVALID) != 0);
    add_bool("busy", (node.states & NK_ACCESSIBILITY_BUSY) != 0);
    add_bool("hasPopup", (node.states & NK_ACCESSIBILITY_HAS_POPUP) != 0);
    json += ",\"canFocus\":";
    json += (node.actions & NK_ACCESSIBILITY_CAN_FOCUS) ? "true" : "false";
    json += ",\"x\":";
    append_json_number(json, node.x);
    json += ",\"y\":";
    append_json_number(json, node.y);
    json += ",\"width\":";
    append_json_number(json, node.width);
    json += ",\"height\":";
    append_json_number(json, node.height);
    json += ",\"label\":";
    append_json_string(json, node.label);
    json += ",\"value\":";
    append_json_string(json, node.value);
    json += ",\"numericValue\":";
    append_json_number(json, node.numeric_value);
    json += ",\"numericMinimum\":";
    append_json_number(json, node.numeric_minimum);
    json += ",\"numericMaximum\":";
    append_json_number(json, node.numeric_maximum);
    json += ",\"textStart\":";
    json += std::to_string(node.text_start);
    json += ",\"documentLength\":";
    json += std::to_string(node.document_length);
    json += ",\"selectionStart\":";
    append_json_position(json, node.selection_start);
    json += ",\"selectionEnd\":";
    append_json_position(json, node.selection_end);
    json += ",\"setSize\":" + std::to_string(node.set_size);
    json += ",\"positionInSet\":" + std::to_string(node.position_in_set);
    json += ",\"rowCount\":" + std::to_string(node.row_count);
    json += ",\"columnCount\":" + std::to_string(node.column_count);
    json += ",\"rowIndex\":";
    if (node.row_index == NK_ACCESSIBILITY_INDEX_NONE)
        json += "null";
    else
        json += std::to_string(node.row_index);
    json += ",\"columnIndex\":";
    if (node.column_index == NK_ACCESSIBILITY_INDEX_NONE)
        json += "null";
    else
        json += std::to_string(node.column_index);
    json += ",\"rowSpan\":" + std::to_string(node.row_span);
    json += ",\"columnSpan\":" + std::to_string(node.column_span);
    json += ",\"hierarchyLevel\":" + std::to_string(node.hierarchy_level);
    json += ",\"orientation\":";
    if (const auto *orientation = web_accessibility_orientation_name(node.orientation))
        append_json_string(json, orientation);
    else
        json += "null";
    json += ",\"actions\":[";
    bool first_action = true;
    for (const auto &spec : web_accessibility_action_specs) {
        if (!(node.actions & spec.bit))
            continue;
        if (!first_action)
            json.push_back(',');
        first_action = false;
        append_json_string(json, spec.name);
    }
    json += "]";
    json += ",\"textRanges\":[";
    for (std::size_t index = 0; index < node.text_ranges.size(); ++index) {
        if (index)
            json.push_back(',');
        const auto &range = node.text_ranges[index];
        json += "{\"start\":" + std::to_string(range.start);
        json += ",\"end\":" + std::to_string(range.end);
        json += ",\"x\":";
        append_json_number(json, range.x);
        json += ",\"y\":";
        append_json_number(json, range.y);
        json += ",\"width\":";
        append_json_number(json, range.width);
        json += ",\"height\":";
        append_json_number(json, range.height);
        json.push_back('}');
    }
    json += "]}";
}

void refresh_web_accessibility(WebSurfaceResource &surface);

void refresh_web_accessibility(WebSurfaceResource &surface) {
    std::vector<const WebAccessibilityNode *> nodes;
    nodes.reserve(surface.accessibility_nodes.size());
    for (const auto &[id, node] : surface.accessibility_nodes)
        nodes.push_back(&node);
    std::sort(nodes.begin(), nodes.end(),
              [](const auto *left, const auto *right) { return left->id < right->id; });

    std::string json = "{\"actions\":{";
    for (std::size_t index = 0;
         index < sizeof(web_accessibility_action_specs) / sizeof(web_accessibility_action_specs[0]);
         ++index) {
        if (index)
            json.push_back(',');
        json.push_back('"');
        json += web_accessibility_action_specs[index].name;
        json += "\":";
        json += std::to_string(web_accessibility_action_specs[index].action);
    }
    json += "},\"nodes\":[";
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (index)
            json.push_back(',');
        append_json_node(json, *nodes[index], surface.accessibility_focus);
    }
    json += "]}";
    auto window = get_window(surface.parent);
    const bool visible = surface.visible && window && window->visible;
    if (window)
        nk::web::set_accessibility_tree(window->selector.c_str(), window->handle, surface.handle,
                                        surface.width, surface.height, visible,
                                        surface.accessibility_focus, json.c_str());
}

nk_result emit_web_accessibility_action(
    WebSurfaceResource &surface, nk_accessibility_node_id node, nk_accessibility_action action,
    const std::string &value = {},
    nk_accessibility_text_position selection_start = NK_ACCESSIBILITY_TEXT_POSITION_NONE,
    nk_accessibility_text_position selection_end = NK_ACCESSIBILITY_TEXT_POSITION_NONE,
    nk_accessibility_text_granularity granularity = 0) noexcept {
    return nk::core::callback_boundary_or<nk_result>(NK_ERROR_UNKNOWN, [&]() -> nk_result {
        const auto found = surface.accessibility_nodes.find(node);
        const auto required = web_accessibility_action_bit(action);
        if (found == surface.accessibility_nodes.end() || !required ||
            !(found->second.actions & required))
            return NK_ERROR_UNSUPPORTED;
        if (action == NK_ACCESSIBILITY_ACTION_FOCUS)
            surface.accessibility_focus = node;
        else if (action == NK_ACCESSIBILITY_ACTION_CLEAR_FOCUS &&
                 surface.accessibility_focus == node)
            surface.accessibility_focus = NK_ACCESSIBILITY_ROOT;
        nk_accessibility_action_event payload{};
        payload.node_id = node;
        payload.action = action;
        payload.value_offset = value.empty() ? 0u : sizeof(payload);
        payload.value_length = static_cast<uint32_t>(value.size());
        payload.selection_start = selection_start;
        payload.selection_end = selection_end;
        payload.granularity = granularity;
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_ACCESSIBILITY_ACTION;
        event.source = surface.handle;
        event.data.resize(sizeof(payload) + value.size() + (value.empty() ? 0u : 1u));
        std::memcpy(event.data.data(), &payload, sizeof(payload));
        if (!value.empty())
            std::memcpy(event.data.data() + sizeof(payload), value.c_str(), value.size() + 1);
        const auto result = nk::core::push_event(std::move(event));
        if (result == NK_OK && (action == NK_ACCESSIBILITY_ACTION_FOCUS ||
                                action == NK_ACCESSIBILITY_ACTION_CLEAR_FOCUS))
            refresh_web_accessibility(surface);
        return result;
    });
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
    auto window = get_window(surface.parent);
    if (window)
        nk::web::configure_text_input(window->selector.c_str(), window->handle, config);
}

void update_text_input_state(WebSurfaceResource &surface, nk_text_position replace_start,
                             nk_text_position replace_end, const std::string &text,
                             nk_text_position selection_start, nk_text_position selection_end,
                             nk_text_position composition_start, nk_text_position composition_end) {
    if (!surface.text_input_state_set)
        return;
    auto &state = surface.text_input_state;
    const auto text_end =
        static_cast<uint64_t>(state.text_start) + utf8_codepoints(surface.text_input_text);
    if (replace_start < state.text_start || replace_end < replace_start || replace_end > text_end)
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
    case NK_CURSOR_ARROW:
        return "default";
    case NK_CURSOR_IBEAM:
        return "text";
    case NK_CURSOR_CROSSHAIR:
        return "crosshair";
    case NK_CURSOR_HAND:
        return "pointer";
    case NK_CURSOR_HORIZONTAL_RESIZE:
        return "ew-resize";
    case NK_CURSOR_VERTICAL_RESIZE:
        return "ns-resize";
    case NK_CURSOR_NWSE_RESIZE:
        return "nwse-resize";
    case NK_CURSOR_NESW_RESIZE:
        return "nesw-resize";
    case NK_CURSOR_MOVE:
        return "move";
    case NK_CURSOR_NOT_ALLOWED:
        return "not-allowed";
    default:
        return nullptr;
    }
}

std::string base64(std::string_view value) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    result.reserve((value.size() + 2) / 3 * 4);
    for (std::size_t index = 0; index < value.size(); index += 3) {
        const auto first = static_cast<unsigned char>(value[index]);
        const auto second = index + 1 < value.size() ? static_cast<unsigned char>(value[index + 1])
                                                     : static_cast<unsigned char>(0);
        const auto third = index + 2 < value.size() ? static_cast<unsigned char>(value[index + 2])
                                                    : static_cast<unsigned char>(0);
        result.push_back(alphabet[first >> 2]);
        result.push_back(alphabet[((first & 0x03) << 4) | (second >> 4)]);
        result.push_back(index + 1 < value.size() ? alphabet[((second & 0x0f) << 2) | (third >> 6)]
                                                  : '=');
        result.push_back(index + 2 < value.size() ? alphabet[third & 0x3f] : '=');
    }
    return result;
}

std::string custom_cursor_css(const nk_cursor_image &image) {
    std::string svg;
    svg.reserve(static_cast<std::size_t>(image.width) * image.height * 32);
    svg += "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"";
    svg += std::to_string(image.width);
    svg += "\" height=\"";
    svg += std::to_string(image.height);
    svg += "\" shape-rendering=\"crispEdges\"><g>";
    const auto *pixels = static_cast<const unsigned char *>(image.rgba);
    for (int32_t y = 0; y < image.height; ++y) {
        const auto *row = pixels + static_cast<std::size_t>(y) * image.stride;
        for (int32_t x = 0; x < image.width; ++x) {
            const auto *pixel = row + static_cast<std::size_t>(x) * 4;
            if (pixel[3] == 0)
                continue;
            char color[8]{};
            std::snprintf(color, sizeof(color), "#%02x%02x%02x", pixel[0], pixel[1], pixel[2]);
            svg += "<rect x=\"";
            svg += std::to_string(x);
            svg += "\" y=\"";
            svg += std::to_string(y);
            svg += "\" width=\"1\" height=\"1\" fill=\"";
            svg += color;
            svg += "\"";
            if (pixel[3] != 255) {
                svg += " fill-opacity=\"";
                svg += std::to_string(static_cast<double>(pixel[3]) / 255.0);
                svg += "\"";
            }
            svg += "/>";
        }
    }
    svg += "</g></svg>";
    return "url(data:image/svg+xml;base64," + base64(svg) + ") " + std::to_string(image.hotspot_x) +
           " " + std::to_string(image.hotspot_y) + ", auto";
}

void apply_cursor(WebWindowResource &window) {
    if (window.cursor_mode != NK_CURSOR_MODE_NORMAL) {
        nk::web::set_cursor(window.selector.c_str(), "none");
        return;
    }
    if (window.cursor && !window.cursor->css.empty())
        nk::web::set_cursor(window.selector.c_str(), window.cursor->css.c_str());
    else
        nk::web::set_cursor(window.selector.c_str(),
                            window.cursor ? cursor_name(window.cursor->shape) : "default");
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
    const nk_surface_resize_event payload{surface.width, surface.height, surface.framebuffer_width,
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
        const nk_pointer_button_event payload{button, NK_INPUT_RELEASE, 0,
                                              0,      window.pointer_x, window.pointer_y};
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
        refresh_web_accessibility(*surface);
    }
}

void sync_canvas_size(WebWindowResource &window) {
    nk::web::CanvasSize size{};
    if (nk::web::canvas_size(window.selector.c_str(), &size)) {
        if (size.framebuffer_width != window.framebuffer_width ||
            size.framebuffer_height != window.framebuffer_height)
            nk::web::set_canvas_framebuffer_size(window.selector.c_str(), size);
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
    case 32:
        return NK_KEY_SPACE;
    case 186:
        return NK_KEY_SEMICOLON;
    case 187:
        return location == 3 ? NK_KEY_KP_EQUAL : NK_KEY_EQUAL;
    case 188:
        return NK_KEY_COMMA;
    case 189:
        return location == 3 ? NK_KEY_KP_SUBTRACT : NK_KEY_MINUS;
    case 190:
        return NK_KEY_PERIOD;
    case 191:
        return NK_KEY_SLASH;
    case 192:
        return NK_KEY_GRAVE_ACCENT;
    case 219:
        return NK_KEY_LEFT_BRACKET;
    case 220:
        return NK_KEY_BACKSLASH;
    case 221:
        return NK_KEY_RIGHT_BRACKET;
    case 222:
        return NK_KEY_APOSTROPHE;
    case 8:
        return NK_KEY_BACKSPACE;
    case 9:
        return NK_KEY_TAB;
    case 13:
        return location == 3 ? NK_KEY_KP_ENTER : NK_KEY_ENTER;
    case 16:
        return location == 2 ? NK_KEY_RIGHT_SHIFT : NK_KEY_LEFT_SHIFT;
    case 17:
        return location == 2 ? NK_KEY_RIGHT_CONTROL : NK_KEY_LEFT_CONTROL;
    case 18:
        return location == 2 ? NK_KEY_RIGHT_ALT : NK_KEY_LEFT_ALT;
    case 27:
        return NK_KEY_ESCAPE;
    case 33:
        return NK_KEY_PAGE_UP;
    case 34:
        return NK_KEY_PAGE_DOWN;
    case 35:
        return NK_KEY_END;
    case 36:
        return NK_KEY_HOME;
    case 37:
        return NK_KEY_LEFT;
    case 38:
        return NK_KEY_UP;
    case 40:
        return NK_KEY_DOWN;
    case 44:
        return NK_KEY_PRINT_SCREEN;
    case 45:
        return NK_KEY_INSERT;
    case 46:
        return location == 3 ? NK_KEY_KP_DECIMAL : NK_KEY_DELETE;
    case 91:
        return NK_KEY_LEFT_SUPER;
    case 93:
        return NK_KEY_RIGHT_SUPER;
    case 106:
        return NK_KEY_KP_MULTIPLY;
    case 107:
        return NK_KEY_KP_ADD;
    case 109:
        return NK_KEY_KP_SUBTRACT;
    case 111:
        return NK_KEY_KP_DIVIDE;
    case 144:
        return NK_KEY_NUM_LOCK;
    case 145:
        return NK_KEY_SCROLL_LOCK;
    case 19:
        return NK_KEY_PAUSE;
    default:
        return NK_KEY_UNKNOWN;
    }
}

nk_pointer_button button_from_dom(int32_t button) {
    switch (button) {
    case 0:
        return NK_POINTER_BUTTON_LEFT;
    case 1:
        return NK_POINTER_BUTTON_MIDDLE;
    case 2:
        return NK_POINTER_BUTTON_RIGHT;
    case 3:
        return NK_POINTER_BUTTON_4;
    case 4:
        return NK_POINTER_BUTTON_5;
    default:
        return UINT32_MAX;
    }
}

void on_resize(const nk::web::CanvasSize &size, void *user_data) {
    nk::core::callback_boundary([&] {
        auto *window = static_cast<WebWindowResource *>(user_data);
        if (window && nk::core::is_runtime_generation(window->generation)) {
            nk::web::set_canvas_framebuffer_size(window->selector.c_str(), size);
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
        const nk_input_action action =
            event.type == nk::web::KeyEventType::up ? NK_INPUT_RELEASE
            : (event.repeat || (key <= NK_KEY_LAST && window->keys[key] == NK_INPUT_PRESS))
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
        const nk_input_action action =
            event.type == nk::web::PointerEventType::up ? NK_INPUT_RELEASE : NK_INPUT_PRESS;
        window->buttons[button] = action;
        window->pointer_x = event.x;
        window->pointer_y = event.y;
        const nk_pointer_button_event payload{button, action, event.modifiers, 0, event.x, event.y};
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
        const nk_touch_action action = event.type == nk::web::TouchEventType::begin ? NK_TOUCH_BEGIN
                                       : event.type == nk::web::TouchEventType::end ? NK_TOUCH_END
                                       : event.type == nk::web::TouchEventType::cancel
                                           ? NK_TOUCH_CANCEL
                                           : NK_TOUCH_MOVE;
        const nk_touch_event payload{event.identifier,
                                     action,
                                     NK_TOUCH_TOOL_FINGER,
                                     event.modifiers,
                                     event.x,
                                     event.y,
                                     event.pressure,
                                     0.0f,
                                     0.0f,
                                     0};
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
            const auto relative_start =
                std::min(event.selection_start, text_end - state.text_start);
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

void on_display_orientation(nk_orientation orientation, void *user_data) {
    nk::core::callback_boundary([&] {
        auto *window = static_cast<WebWindowResource *>(user_data);
        if (!window || !nk::core::is_runtime_generation(window->generation) ||
            orientation == NK_ORIENTATION_UNKNOWN ||
            orientation == window->last_display_orientation)
            return;
        window->last_display_orientation = orientation;
        const nk_orientation_event payload{sizeof(payload), orientation, 0, {0, 0}};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_DISPLAY_ORIENTATION_CHANGED;
        event.source = window->handle;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    });
}

void on_device_orientation(nk_orientation orientation, void *) {
    nk::core::callback_boundary([&] {
        if (orientation == NK_ORIENTATION_UNKNOWN || orientation == last_device_orientation)
            return;
        last_device_orientation = orientation;
        const nk_orientation_event payload{sizeof(payload), orientation, 0, {0, 0}};
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_DEVICE_ORIENTATION_CHANGED;
        event.source = NK_INVALID_HANDLE;
        event.data = bytes_of(payload);
        nk::core::push_event(std::move(event));
    });
}

void on_accessibility_action(const nk::web::AccessibilityActionEvent &event, void *user_data) {
    nk::core::callback_boundary([&] {
        auto *window = static_cast<WebWindowResource *>(user_data);
        if (!window || !nk::core::is_runtime_generation(window->generation))
            return;
        auto surface = get_surface(event.surface);
        if (!surface || surface->parent != window->handle)
            return;
        const std::string value = event.value ? event.value : "";
        emit_web_accessibility_action(*surface, event.node, event.action, value,
                                      event.selection_start, event.selection_end,
                                      event.granularity);
    });
}

void on_drop(const nk::web::ResourceDropEvent &event, void *user_data) {
    nk::core::callback_boundary([&] {
        auto *window = static_cast<WebWindowResource *>(user_data);
        if (!window || !window->drops_enabled ||
            !nk::core::is_runtime_generation(window->generation))
            return;
        const std::string uri_list = event.uris ? event.uris : "";
        const auto resources =
            nk::platform::resources_from_uri_list(uri_list, NK_RESOURCE_READABLE);
        const std::string text = event.text ? event.text : "";
        if (resources.empty() && text.empty())
            return;
        nk::core::QueuedEvent queued;
        queued.kind = NK_EVENT_RESOURCE_DROP;
        queued.source = window->handle;
        queued.data_count = static_cast<uint32_t>(resources.size());
        queued.data = nk::platform::resource_drop_payload(event.x, event.y, text, resources);
        nk::core::push_event(std::move(queued));
    });
}

void on_resource_dialog(const nk::web::ResourceDialogEvent &event, void *user_data) {
    nk::core::callback_boundary([&] {
        auto *window = static_cast<WebWindowResource *>(user_data);
        if (!window || !nk::core::is_runtime_generation(window->generation) ||
            pending_resource_dialogs.erase(event.request) == 0)
            return;
        const auto flags = event.kind == NK_DIALOG_OPEN_RESOURCE ? NK_RESOURCE_READABLE
                           : event.kind == NK_DIALOG_SAVE_RESOURCE
                               ? NK_RESOURCE_WRITABLE
                               : NK_RESOURCE_READABLE | NK_RESOURCE_WRITABLE;
        const auto resources = event.result == NK_OK && event.accepted && event.uris
                                   ? nk::platform::resources_from_uri_list(event.uris, flags)
                                   : std::vector<nk::platform::ResourceValue>{};
        nk::core::QueuedEvent queued;
        queued.kind = NK_EVENT_DIALOG_RESOURCES_COMPLETE;
        queued.source = window->handle;
        queued.request_id = event.request;
        queued.flags = event.kind;
        queued.result = event.result;
        queued.data_count = static_cast<uint32_t>(resources.size());
        queued.data = nk::platform::resource_payload(event.accepted, resources);
        nk::core::push_event(std::move(queued));
    });
}

void on_notification(const nk::web::NotificationEvent &event, void *user_data) {
    nk::core::callback_boundary([&] {
        auto *window = static_cast<WebWindowResource *>(user_data);
        if (!window || !nk::core::is_runtime_generation(window->generation) ||
            pending_notifications.find(event.request) == pending_notifications.end())
            return;
        if (event.kind == NK_EVENT_NOTIFICATION_DISMISSED ||
            event.kind == NK_EVENT_NOTIFICATION_FAILED)
            pending_notifications.erase(event.request);
        nk::core::QueuedEvent queued;
        queued.kind = event.kind;
        queued.source = NK_INVALID_HANDLE;
        queued.request_id = event.request;
        queued.result = event.result;
        nk::core::push_event(std::move(queued));
    });
}

void emit_web_joystick(nk_event_kind kind, nk_handle source) {
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = source;
    nk::core::push_event(std::move(event));
}

template <typename Payload>
void emit_web_joystick_input(nk_event_kind kind, nk_handle source, const Payload &payload) {
    nk::core::QueuedEvent event;
    event.kind = kind;
    event.source = source;
    event.data = bytes_of(payload);
    nk::core::push_event(std::move(event));
}

uint8_t web_gamepad_hat(const WebGamepadResource &device) {
    uint8_t result = NK_JOYSTICK_HAT_CENTERED;
    if (device.buttons[13])
        result |= NK_JOYSTICK_HAT_UP;
    if (device.buttons[14])
        result |= NK_JOYSTICK_HAT_RIGHT;
    if (device.buttons[15])
        result |= NK_JOYSTICK_HAT_DOWN;
    if (device.buttons[16])
        result |= NK_JOYSTICK_HAT_LEFT;
    return result;
}

void update_web_gamepad(WebGamepadResource &device, const nk::web::GamepadStateEvent &event) {
    for (std::size_t index = 0; index < device.axes.size(); ++index) {
        if (device.axes[index] == event.axes[index])
            continue;
        device.axes[index] = event.axes[index];
        emit_web_joystick_input(
            NK_EVENT_JOYSTICK_AXIS, device.handle,
            nk_joystick_axis_event{static_cast<uint32_t>(index), device.axes[index]});
    }
    for (std::size_t index = 0; index < device.buttons.size(); ++index) {
        if (device.buttons[index] == event.buttons[index])
            continue;
        device.buttons[index] = event.buttons[index];
        emit_web_joystick_input(
            NK_EVENT_JOYSTICK_BUTTON, device.handle,
            nk_joystick_button_event{static_cast<uint32_t>(index), device.buttons[index]});
    }
    const auto hat = web_gamepad_hat(device);
    if (device.hats[0] != hat) {
        device.hats[0] = hat;
        emit_web_joystick_input(NK_EVENT_JOYSTICK_HAT, device.handle,
                                nk_joystick_hat_event{0, device.hats[0]});
    }
}

void remove_web_gamepad(int32_t index) {
    const auto found = web_gamepads.find(index);
    if (found == web_gamepads.end())
        return;
    const auto handle = found->second->handle;
    (void)nk::web::stop_gamepad_rumble(index);
    emit_web_joystick(NK_EVENT_JOYSTICK_DISCONNECTED, handle);
    nk::core::gamepad_events::disconnect(handle);
    nk::core::handles().erase(handle, nk::core::ResourceType::joystick);
    web_gamepads.erase(found);
}

void on_gamepad(const nk::web::GamepadStateEvent &event, void *) {
    nk::core::callback_boundary([&] {
        if (event.index < 0)
            return;
        const auto found = web_gamepads.find(event.index);
        if (!event.connected) {
            remove_web_gamepad(event.index);
            return;
        }
        std::shared_ptr<WebGamepadResource> device;
        if (found == web_gamepads.end()) {
            device = std::make_shared<WebGamepadResource>();
            device->index = event.index;
            device->standard = event.standard;
            device->name = event.id && *event.id ? event.id : "Web Gamepad";
            device->guid = web_gamepad_guid(device->name, event.index);
            device->handle = nk::core::handles().insert(nk::core::ResourceType::joystick, device);
            if (!device->handle)
                return;
            web_gamepads.emplace(event.index, device);
            update_web_gamepad(*device, event);
            nk::core::gamepad_events::update(device->handle, false);
            emit_web_joystick(NK_EVENT_JOYSTICK_CONNECTED, device->handle);
        } else {
            device = found->second;
            device->standard = event.standard;
            update_web_gamepad(*device, event);
            nk::core::gamepad_events::update(device->handle, true);
        }
        device->seen = true;
    });
}

EM_BOOL frame_loop(double, void *user_data) {
    const auto handle = static_cast<nk_handle>(reinterpret_cast<uintptr_t>(user_data));
    auto surface = get_surface(handle);
    if (!surface || !nk::core::is_runtime_generation(surface->generation) ||
        !surface->frame_callback)
        return EM_FALSE;
    if (surface->context_lost)
        return EM_TRUE;
    if (!surface->frame_requests.should_draw())
        return EM_FALSE;
    auto window = get_window(surface->parent);
    if (!window || !nk::web::make_context_current(surface->context()))
        return EM_FALSE;
    sync_canvas_size(*window);
    surface->frame_requests.begin_frame();
    nk::core::callback_boundary([&] {
        if (surface->frame_callback)
            surface->frame_callback(surface->handle, surface->framebuffer_width,
                                    surface->framebuffer_height, surface->frame_user_data);
    });
    return surface->frame_callback ? EM_TRUE : EM_FALSE;
}

EM_BOOL cooperative_task_frame_loop(double, void *) {
    nk::core::run_cooperative_tasks();
    return nk::core::cooperative_tasks_pending() ? EM_TRUE : EM_FALSE;
}

nk_result arm_surface_frames(const std::shared_ptr<WebSurfaceResource> &surface) {
    if (!surface || !surface->frame_callback)
        return NK_OK;
    const auto frame_user_data = reinterpret_cast<void *>(static_cast<uintptr_t>(surface->handle));
    if (!nk::web::start_frame_loop(frame_loop, frame_user_data)) {
        nk::core::set_error("could not start the browser animation-frame loop");
        return NK_ERROR_UNKNOWN;
    }
    return NK_OK;
}

void disarm_surface_frames(const std::shared_ptr<WebSurfaceResource> &surface) {
    if (!surface)
        return;
    const auto frame_user_data = reinterpret_cast<void *>(static_cast<uintptr_t>(surface->handle));
    nk::web::stop_frame_loop(frame_loop, frame_user_data);
}

void update_canvas_visibility(WebWindowResource &window) {
    const bool surface_visible =
        std::any_of(window.surfaces.begin(), window.surfaces.end(), [](nk_handle handle) {
            const auto surface = get_surface(handle);
            return surface && surface->visible;
        });
    nk::web::set_canvas_visible(window.selector.c_str(), window.visible && surface_visible);
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
    flush_web_resource_writes();
    nk::web::remove_callbacks();
    nk::web_gamepad::shutdown();
    pending_resource_dialogs.clear();
    pending_notifications.clear();
    pending_device_orientation_requests.clear();
    pending_sensor_permission_requests.clear();
    last_device_orientation = NK_ORIENTATION_UNKNOWN;
    web_windows.clear();
}

} // namespace

std::shared_ptr<WebGamepadResource> lookup_web_gamepad(nk_handle handle);

namespace nk::web_gamepad {

void poll() noexcept {
    for (const auto &[index, device] : web_gamepads) {
            (void)index;
            device->seen = false;
        }
        if (!nk::web::poll_gamepads())
            return;
        std::vector<int32_t> disconnected;
        for (const auto &[index, device] : web_gamepads)
            if (!device->seen)
                disconnected.push_back(index);
    for (const auto index : disconnected)
        remove_web_gamepad(index);
}

void shutdown() noexcept {
    std::vector<int32_t> indexes;
    indexes.reserve(web_gamepads.size());
    for (const auto &[index, device] : web_gamepads) {
        (void)device;
        indexes.push_back(index);
    }
    for (const auto index : indexes)
        remove_web_gamepad(index);
    web_gamepads.clear();
}

bool standard_gamepad(nk_handle handle) noexcept {
    const auto device = std::dynamic_pointer_cast<WebGamepadResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::joystick));
    return device && device->standard;
}

nk_result standard_gamepad_state(nk_handle handle, nk_gamepad_state *out_state) noexcept {
    const auto device = std::dynamic_pointer_cast<WebGamepadResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::joystick));
    if (!device)
        return NK_ERROR_INVALID_HANDLE;
    if (!out_state || out_state->struct_size < sizeof(*out_state))
        return NK_ERROR_INVALID_ARGUMENT;
    const auto size = out_state->struct_size;
    *out_state = {};
    out_state->struct_size = size;
    std::copy(device->axes.begin(), device->axes.end(), out_state->axes);
    out_state->buttons[NK_GAMEPAD_BUTTON_A] = device->buttons[0];
    out_state->buttons[NK_GAMEPAD_BUTTON_B] = device->buttons[1];
    out_state->buttons[NK_GAMEPAD_BUTTON_X] = device->buttons[2];
    out_state->buttons[NK_GAMEPAD_BUTTON_Y] = device->buttons[3];
    out_state->buttons[NK_GAMEPAD_BUTTON_LEFT_BUMPER] = device->buttons[4];
    out_state->buttons[NK_GAMEPAD_BUTTON_RIGHT_BUMPER] = device->buttons[5];
    out_state->buttons[NK_GAMEPAD_BUTTON_BACK] = device->buttons[8];
    out_state->buttons[NK_GAMEPAD_BUTTON_START] = device->buttons[9];
    out_state->buttons[NK_GAMEPAD_BUTTON_GUIDE] = device->buttons[10];
    out_state->buttons[NK_GAMEPAD_BUTTON_LEFT_THUMB] = device->buttons[11];
    out_state->buttons[NK_GAMEPAD_BUTTON_RIGHT_THUMB] = device->buttons[12];
    out_state->buttons[NK_GAMEPAD_BUTTON_DPAD_UP] = device->buttons[13];
    out_state->buttons[NK_GAMEPAD_BUTTON_DPAD_RIGHT] = device->buttons[14];
    out_state->buttons[NK_GAMEPAD_BUTTON_DPAD_DOWN] = device->buttons[15];
    out_state->buttons[NK_GAMEPAD_BUTTON_DPAD_LEFT] = device->buttons[16];
    return NK_OK;
}

} // namespace nk::web_gamepad

namespace nk::core::sensor_backend {

nk_result list(std::vector<SensorBackendDescriptor> &out) noexcept {
    if (!nk::web::sensors_supported())
        return NK_ERROR_UNSUPPORTED;
    out = {{NK_SENSOR_ACCELEROMETER, 3, 1000000ULL, 0, 100.0f},
           {NK_SENSOR_GYROSCOPE, 3, 1000000ULL, 0, 100.0f},
           {NK_SENSOR_LINEAR_ACCELERATION, 3, 1000000ULL, 0, 100.0f}};
    return NK_OK;
}

nk_result start(nk_sensor sensor, nk_sensor_type type, const nk_sensor_options &options) noexcept {
    return nk::web::start_sensor(sensor, type, options.sample_interval_ns,
                                 options.maximum_batch_latency_ns)
               ? NK_OK
               : NK_ERROR_UNSUPPORTED;
}

nk_result stop(nk_sensor sensor) noexcept {
    return nk::web::stop_sensor(sensor) ? NK_OK : NK_ERROR_UNSUPPORTED;
}

nk_result request_permission(nk_request_id request) noexcept {
    pending_sensor_permission_requests.insert(request);
    if (nk::web::request_sensor_permission(request))
        return NK_OK;
    pending_sensor_permission_requests.erase(request);
    return NK_ERROR_UNSUPPORTED;
}

void shutdown() noexcept {
    pending_sensor_permission_requests.clear();
    nk::web::stop_all_sensors();
}

} // namespace nk::core::sensor_backend

namespace nk::core::haptics_backend {

nk_result vibrate(const nk_haptic_vibration &options) noexcept {
    return nk::web::vibrate(options.period_ms, options.duration_ms, options.intensity)
               ? NK_OK
               : NK_ERROR_UNSUPPORTED;
}
nk_result stop_vibration() noexcept {
    return nk::web::stop_vibration() ? NK_OK : NK_ERROR_UNSUPPORTED;
}
nk_result gamepad_rumble(nk_joystick handle, const nk_gamepad_rumble_options &options) noexcept {
    const auto device = lookup_web_gamepad(handle);
    if (!device)
        return NK_ERROR_INVALID_HANDLE;
    return nk::web::gamepad_rumble(device->index, options.low_frequency, options.high_frequency,
                                   options.duration_ms)
               ? NK_OK
               : NK_ERROR_UNSUPPORTED;
}
nk_result stop_gamepad_rumble(nk_joystick handle) noexcept {
    const auto device = lookup_web_gamepad(handle);
    if (!device)
        return NK_ERROR_INVALID_HANDLE;
    return nk::web::stop_gamepad_rumble(device->index) ? NK_OK : NK_ERROR_UNSUPPORTED;
}

} // namespace nk::core::haptics_backend

namespace nk::backend {

void schedule_cooperative_tasks() noexcept {
    (void)nk::web::start_frame_loop(cooperative_task_frame_loop, nullptr);
}

void stop_cooperative_tasks() noexcept {
    nk::web::stop_frame_loop(cooperative_task_frame_loop, nullptr);
}

void pump_events() noexcept {
    const auto windows = web_windows;
    for (const auto handle : windows)
        if (auto window = get_window(handle))
            sync_canvas_size(*window);
    flush_web_resource_writes();
    nk::web_gamepad::poll();
}

void shutdown() noexcept {
    nk::core::sensor_backend::shutdown();
    (void)nk::core::haptics_backend::stop_vibration();
    shutdown_web();
}

} // namespace nk::backend

namespace nk::core::system_backend {

nk_result keep_awake_apply(bool enabled) noexcept {
    if (!nk::web::keep_awake_apply(enabled)) {
        nk::core::set_error("browser Screen Wake Lock is unavailable");
        return NK_ERROR_UNSUPPORTED;
    }
    return NK_OK;
}

nk_result get_orientation(nk_system_orientation &out_orientation) noexcept {
    const auto size = out_orientation.struct_size;
    out_orientation = {};
    out_orientation.struct_size = size;
    const bool device_supported = nk::web::device_orientation_supported();
    const bool display_supported = nk::web::display_orientation_supported();
    if (!device_supported && !display_supported) {
        nk::core::set_error("browser orientation APIs are unavailable");
        return NK_ERROR_UNSUPPORTED;
    }
    if (device_supported)
        out_orientation.device = nk::web::device_orientation();
    if (display_supported)
        out_orientation.display = nk::web::display_orientation();
    return NK_OK;
}

nk_result request_device_orientation(nk_request_id request) noexcept {
    pending_device_orientation_requests.insert(request);
    if (nk::web::request_device_orientation(request))
        return NK_OK;
    pending_device_orientation_requests.erase(request);
    nk::core::set_error("browser Device Orientation API is unavailable");
    return NK_ERROR_UNSUPPORTED;
}

nk_result get_string(nk_system_string_kind kind, std::string &out_value) {
    if (kind != NK_SYSTEM_STRING_PLATFORM_VERSION)
        return NK_ERROR_UNSUPPORTED;
    uint32_t size = 0;
    if (nk::web::copy_user_agent(nullptr, &size) != NK_ERROR_BUFFER_TOO_SMALL || !size)
        return NK_ERROR_UNSUPPORTED;
    std::string value(size, '\0');
    auto capacity = size;
    if (nk::web::copy_user_agent(value.data(), &capacity) != NK_OK)
        return NK_ERROR_UNSUPPORTED;
    value.resize(std::strlen(value.c_str()));
    out_value = std::move(value);
    return out_value.empty() ? NK_ERROR_UNSUPPORTED : NK_OK;
}

} // namespace nk::core::system_backend

std::shared_ptr<WebGamepadResource> lookup_web_gamepad(nk_handle handle) {
    return std::dynamic_pointer_cast<WebGamepadResource>(
        nk::core::handles().get(handle, nk::core::ResourceType::joystick));
}

extern "C" {

nk_capabilities NK_CALL nk_get_capabilities(void) {
    auto capabilities = NK_CAP_WINDOW | NK_CAP_INPUT | NK_CAP_OPENGL_ES_SURFACE | NK_CAP_CURSOR |
                        NK_CAP_POINTER_CAPTURE | NK_CAP_CLIPBOARD | NK_CAP_WINDOW_GEOMETRY |
                        NK_CAP_WINDOW_STYLING | NK_CAP_DRAG_DROP | NK_CAP_SHELL |
                        NK_CAP_RESOURCE_SHARING | NK_CAP_RESOURCE_IO | NK_CAP_SYSTEM_INFO |
                        NK_CAP_ACCESSIBILITY | NK_CAP_SURFACE_FRAME_CALLBACK |
                        nk::core::optional_capabilities();
    if (nk::web::appearance_supported())
        capabilities |= NK_CAP_SYSTEM_APPEARANCE;
    if (nk::web::keep_awake_supported())
        capabilities |= NK_CAP_KEEP_AWAKE;
    if (nk::web::display_orientation_supported())
        capabilities |= NK_CAP_DISPLAY_ORIENTATION;
    if (nk::web::device_orientation_supported())
        capabilities |= NK_CAP_DEVICE_ORIENTATION;
    if (nk::web::sensors_supported())
        capabilities |= NK_CAP_SENSORS;
    if (nk::web::haptics_supported())
        capabilities |= NK_CAP_HAPTICS;
    if (nk::web::notification_supported())
        capabilities |= NK_CAP_NOTIFICATION;
    if (nk::web::gamepad_supported()) {
        capabilities |= NK_CAP_JOYSTICK;
        capabilities |= NK_CAP_GAMEPAD_RUMBLE;
    }
    return capabilities;
}

nk_result NK_CALL nk_system_directory(nk_system_directory_kind, char *, uint32_t *) {
    return unsupported("browser filesystem paths are unavailable");
}

EMSCRIPTEN_KEEPALIVE void nk_web_host_device_orientation_permission(uint32_t request,
                                                                    nk_result result) {
    if (pending_device_orientation_requests.erase(static_cast<nk_request_id>(request)) == 0)
        return;
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_DEVICE_ORIENTATION_PERMISSION_COMPLETE;
    event.source = NK_INVALID_HANDLE;
    event.request_id = static_cast<nk_request_id>(request);
    event.result = result;
    nk::core::push_event(std::move(event));
}

EMSCRIPTEN_KEEPALIVE void nk_web_host_sensor_permission(uint32_t request, nk_result result) {
    if (pending_sensor_permission_requests.erase(static_cast<nk_request_id>(request)) == 0)
        return;
    const auto status = result == NK_OK                  ? NK_SENSOR_PERMISSION_GRANTED
                        : result == NK_ERROR_UNSUPPORTED ? NK_SENSOR_PERMISSION_DENIED
                                                         : NK_SENSOR_PERMISSION_UNAVAILABLE;
    nk::core::sensor_permission_complete(static_cast<nk_request_id>(request), result, status);
}

EMSCRIPTEN_KEEPALIVE void nk_web_host_sensor_update(uint32_t sensor, uint32_t type, float x,
                                                    float y, float z, float w, int accuracy) {
    switch (type) {
    case NK_SENSOR_ACCELEROMETER:
    case NK_SENSOR_GYROSCOPE:
    case NK_SENSOR_LINEAR_ACCELERATION:
        break;
    default:
        return;
    }
    const float values[4] = {x, y, z, w};
    const auto status = accuracy <= 0   ? NK_SENSOR_ACCURACY_UNAVAILABLE
                        : accuracy == 1 ? NK_SENSOR_ACCURACY_LOW
                        : accuracy == 2 ? NK_SENSOR_ACCURACY_MEDIUM
                                        : NK_SENSOR_ACCURACY_HIGH;
    nk::core::sensor_publish(static_cast<nk_sensor>(sensor), static_cast<nk_sensor_type>(type),
                             values, status);
}

nk_result NK_CALL nk_system_locale(char *buffer, uint32_t *inout_size) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    nk::core::clear_error();
    const auto result = nk::web::copy_locale(buffer, inout_size);
    if (result == NK_ERROR_INVALID_ARGUMENT)
        nk::core::set_error("browser locale size output is null");
    else if (result == NK_ERROR_BUFFER_TOO_SMALL)
        nk::core::set_error("browser locale output buffer is too small");
    else if (result == NK_ERROR_UNSUPPORTED)
        nk::core::set_error("browser locale is unavailable");
    return result;
}

nk_result NK_CALL nk_system_get_appearance(nk_system_appearance *appearance) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    nk::core::clear_error();
    if (!appearance || appearance->struct_size < sizeof(*appearance))
        return invalid_argument("invalid browser appearance output");
    const auto size = appearance->struct_size;
    *appearance = {};
    appearance->struct_size = size;
    if (!nk::web::get_appearance(appearance))
        return unsupported("browser appearance queries are unavailable");
    return NK_OK;
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

nk_result NK_CALL nk_clipboard_set_resources(const nk_resource *resources,
                                             uint32_t resource_count) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (const auto result = nk::platform::validate_resources(resources, resource_count, false);
        result != NK_OK)
        return result;
    std::string uris;
    for (uint32_t index = 0; index < resource_count; ++index) {
        if (!uris.empty())
            uris += "\r\n";
        uris += resources[index].uri;
    }
    if (!nk::web::set_clipboard_resources(uris.c_str()))
        return unsupported("browser resource clipboard write is unavailable");
    return NK_OK;
}

nk_result NK_CALL nk_clipboard_read_resources(nk_request_id *out_request) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_request)
        return invalid_argument("web resource clipboard request output is null");
    *out_request = NK_INVALID_REQUEST_ID;
    const auto request = nk::core::next_request_id();
    if (!nk::web::read_clipboard_resources(request))
        return unsupported("browser resource clipboard read is unavailable");
    *out_request = request;
    return NK_OK;
}

nk_result NK_CALL nk_share(const nk_share_options *options) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!options || options->struct_size < sizeof(*options) || options->flags != 0 ||
        (!options->text && options->resource_count == 0))
        return invalid_argument("invalid or empty web share options");
    if (const auto result =
            nk::platform::validate_resources(options->resources, options->resource_count, true);
        result != NK_OK)
        return result;
    std::string uris;
    for (uint32_t index = 0; index < options->resource_count; ++index) {
        if (!uris.empty())
            uris += "\r\n";
        uris += options->resources[index].uri;
    }
    if (!nk::web::share(options->title ? options->title : "", options->text ? options->text : "",
                        uris.c_str()))
        return unsupported("browser Web Share API is unavailable or requires a user gesture");
    return NK_OK;
}

nk_result NK_CALL nk_shell_open_url(const char *url) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!valid_web_uri(url))
        return invalid_argument("web shell URL is invalid");
    if (!nk::web::open_url(url))
        return unsupported("browser URL opening is unavailable");
    return NK_OK;
}

nk_result NK_CALL nk_shell_open_resource(const nk_resource *resource) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!resource || resource->struct_size < sizeof(*resource) || !valid_web_uri(resource->uri) ||
        !nk::platform::valid_utf8(resource->mime_type) ||
        !nk::platform::valid_utf8(resource->display_name))
        return invalid_argument("web resource shell arguments are invalid");
    if (!nk::web::open_url(resource->uri))
        return unsupported("browser resource opening is unavailable");
    return NK_OK;
}

nk_result start_web_resource_dialog(nk_dialog_operation operation, nk_handle parent,
                                    const nk_file_dialog_options *options,
                                    nk_request_id *out_request) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!out_request || !valid_resource_dialog_options(options))
        return invalid_argument("invalid web resource dialog options");
    auto window = parent == NK_INVALID_HANDLE ? first_window() : get_window(parent);
    if (!window)
        return invalid_handle("invalid web dialog parent");
    *out_request = NK_INVALID_REQUEST_ID;
    const auto request = nk::core::next_request_id();
    pending_resource_dialogs.emplace(request, PendingWebResourceDialog{operation, window->handle});
    const bool multiple =
        operation == NK_DIALOG_OPEN_RESOURCE && (options->flags & NK_DIALOG_ALLOW_MULTIPLE) != 0;
    const auto accept = resource_dialog_accept(options);
    if (!nk::web::pick_resources(window->selector.c_str(), window->handle, request, operation,
                                 multiple, options->title ? options->title : "", accept.c_str(),
                                 options->suggested_name ? options->suggested_name : "")) {
        pending_resource_dialogs.erase(request);
        return unsupported("browser resource picker is unavailable");
    }
    *out_request = request;
    return NK_OK;
}

nk_result NK_CALL nk_dialog_open_resource(nk_handle parent, const nk_file_dialog_options *options,
                                          nk_request_id *out_request) {
    return start_web_resource_dialog(NK_DIALOG_OPEN_RESOURCE, parent, options, out_request);
}

nk_result NK_CALL nk_dialog_save_resource(nk_handle parent, const nk_file_dialog_options *options,
                                          nk_request_id *out_request) {
    return start_web_resource_dialog(NK_DIALOG_SAVE_RESOURCE, parent, options, out_request);
}

nk_result NK_CALL nk_dialog_select_resource_directory(nk_handle parent,
                                                      const nk_file_dialog_options *options,
                                                      nk_request_id *out_request) {
    return start_web_resource_dialog(NK_DIALOG_SELECT_RESOURCE_DIRECTORY, parent, options,
                                     out_request);
}

nk_result NK_CALL nk_dialog_cancel(nk_request_id request) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    const auto found = pending_resource_dialogs.find(request);
    if (!request || found == pending_resource_dialogs.end()) {
        nk::core::set_error("invalid or completed web resource dialog request");
        return NK_ERROR_INVALID_REQUEST;
    }
    const auto dialog = found->second;
    pending_resource_dialogs.erase(found);
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_DIALOG_RESOURCES_COMPLETE;
    event.source = dialog.parent;
    event.flags = dialog.operation;
    event.request_id = request;
    event.data = nk::platform::resource_payload(false, {});
    return nk::core::push_event(std::move(event));
}

nk_result NK_CALL nk_notification_show(const nk_notification_options *options,
                                       nk_request_id *out_request) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!options || options->struct_size < sizeof(*options) || !out_request || !options->title ||
        !*options->title || (options->flags & ~NK_NOTIFICATION_SILENT) || options->reserved != 0 ||
        !nk::platform::valid_utf8(options->title) || !nk::platform::valid_utf8(options->body) ||
        !nk::platform::valid_utf8(options->icon))
        return invalid_argument("invalid web notification options");
    *out_request = NK_INVALID_REQUEST_ID;
    const auto request = nk::core::next_request_id();
    pending_notifications.insert(request);
    if (!nk::web::show_notification(request, options->title, options->body ? options->body : "",
                                    options->icon ? options->icon : "",
                                    (options->flags & NK_NOTIFICATION_SILENT) != 0)) {
        pending_notifications.erase(request);
        return unsupported("browser notifications are unavailable");
    }
    *out_request = request;
    return NK_OK;
}

nk_result NK_CALL nk_notification_close(nk_request_id request) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!request || pending_notifications.find(request) == pending_notifications.end()) {
        nk::core::set_error("invalid or completed web notification request");
        return NK_ERROR_INVALID_REQUEST;
    }
    if (!nk::web::close_notification(request)) {
        nk::core::set_error("web notification is no longer active");
        return NK_ERROR_INVALID_REQUEST;
    }
    pending_notifications.erase(request);
    nk::core::QueuedEvent event;
    event.kind = NK_EVENT_NOTIFICATION_DISMISSED;
    event.request_id = request;
    return nk::core::push_event(std::move(event));
}

nk_result NK_CALL nk_joystick_list(nk_handle *output, uint32_t *inout_count) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    nk::web_gamepad::poll();
    std::vector<nk_handle> handles;
    handles.reserve(web_gamepads.size());
    for (const auto &[index, device] : web_gamepads) {
        (void)index;
        handles.push_back(device->handle);
    }
    std::sort(handles.begin(), handles.end());
    return copy_web_array(handles.data(), handles.size(), output, inout_count);
}

nk_result NK_CALL nk_joystick_get_name(nk_handle handle, char *buffer, uint32_t *inout_size) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    const auto device = lookup_web_gamepad(handle);
    if (!device)
        return invalid_handle("invalid web joystick handle");
    return copy_web_string(device->name, buffer, inout_size);
}

nk_result NK_CALL nk_joystick_get_guid(nk_handle handle, char *buffer, uint32_t *inout_size) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    const auto device = lookup_web_gamepad(handle);
    if (!device)
        return invalid_handle("invalid web joystick handle");
    return copy_web_string(device->guid, buffer, inout_size);
}

nk_result NK_CALL nk_joystick_get_axes(nk_handle handle, float *axes, uint32_t *inout_count) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    const auto device = lookup_web_gamepad(handle);
    if (!device)
        return invalid_handle("invalid web joystick handle");
    return copy_web_array(device->axes.data(), device->axes.size(), axes, inout_count);
}

nk_result NK_CALL nk_joystick_get_buttons(nk_handle handle, uint8_t *buttons,
                                          uint32_t *inout_count) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    const auto device = lookup_web_gamepad(handle);
    if (!device)
        return invalid_handle("invalid web joystick handle");
    return copy_web_array(device->buttons.data(), device->buttons.size(), buttons, inout_count);
}

nk_result NK_CALL nk_joystick_get_hats(nk_handle handle, uint8_t *hats, uint32_t *inout_count) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    const auto device = lookup_web_gamepad(handle);
    if (!device)
        return invalid_handle("invalid web joystick handle");
    return copy_web_array(device->hats.data(), device->hats.size(), hats, inout_count);
}

nk_result NK_CALL nk_joystick_get_diagnostics(char *buffer, uint32_t *inout_size) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    return copy_web_string({}, buffer, inout_size);
}

nk_result NK_CALL nk_window_set_drop_enabled(nk_handle handle, nk_bool enabled) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    window->drops_enabled = enabled != 0;
    return NK_OK;
}

nk_result NK_CALL nk_resource_load_async(const nk_resource *resource, nk_request_id *out_request) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!resource || resource->struct_size < sizeof(nk_resource) || !resource->uri ||
        !*resource->uri || !out_request)
        return invalid_argument("web resource load arguments are invalid");
    *out_request = NK_INVALID_REQUEST_ID;
    const auto request = nk::core::next_request_id();
    if (!nk::web::fetch_resource(resource->uri, request))
        return unsupported("browser resource fetch is unavailable");
    *out_request = request;
    return NK_OK;
}

nk_result NK_CALL nk_resource_set_persisted_access(const nk_resource *resource,
                                                   uint32_t access_flags, uint32_t *out_flags) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!resource || resource->struct_size < sizeof(nk_resource) || !resource->uri ||
        !*resource->uri || !out_flags ||
        (access_flags & ~(NK_RESOURCE_READABLE | NK_RESOURCE_WRITABLE)) != 0)
        return invalid_argument("web persisted resource access arguments are invalid");
    *out_flags = 0;
    return unsupported("browser resource permissions are runtime-scoped");
}

nk_result NK_CALL nk_resource_get_persisted_access(const nk_resource *resource,
                                                   uint32_t *out_flags) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!resource || resource->struct_size < sizeof(nk_resource) || !resource->uri ||
        !*resource->uri || !out_flags)
        return invalid_argument("web persisted resource access arguments are invalid");
    *out_flags = 0;
    return unsupported("browser resource permissions are runtime-scoped");
}

nk_result NK_CALL nk_resource_open(const nk_resource *resource, uint32_t flags,
                                   nk_handle *out_stream) {
    return nk::core::result_boundary(
        "unexpected error while opening web resource", [&]() -> nk_result {
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            if (!resource || resource->struct_size < sizeof(nk_resource) || !resource->uri ||
                !*resource->uri || !out_stream ||
                (flags & (NK_RESOURCE_OPEN_READ | NK_RESOURCE_OPEN_WRITE)) == 0 ||
                (flags & ~(NK_RESOURCE_OPEN_READ | NK_RESOURCE_OPEN_WRITE |
                           NK_RESOURCE_OPEN_CREATE | NK_RESOURCE_OPEN_TRUNCATE)) != 0 ||
                ((flags & (NK_RESOURCE_OPEN_CREATE | NK_RESOURCE_OPEN_TRUNCATE)) != 0 &&
                 (flags & NK_RESOURCE_OPEN_WRITE) == 0))
                return invalid_argument("web resource open arguments are invalid");
            *out_stream = NK_INVALID_HANDLE;
            if ((flags & NK_RESOURCE_OPEN_READ) != 0)
                return unsupported("browser resource reads use nk_resource_load_async");
            if (!is_web_file_handle_uri(resource->uri))
                return unsupported("web resource streams require a retained file handle");
            if (!nk::web::has_resource_handle(resource->uri))
                return unsupported("browser file handle is no longer available");

            auto resource_stream = std::make_shared<WebResourceStream>();
            resource_stream->uri = resource->uri;
            resource_stream->flags = NK_RESOURCE_STREAM_WRITABLE | NK_RESOURCE_STREAM_SEEKABLE |
                                     NK_RESOURCE_STREAM_SIZE_KNOWN;
            const auto handle = nk::core::handles().insert(nk::core::ResourceType::resource_stream,
                                                           resource_stream);
            if (handle == NK_INVALID_HANDLE)
                return resource_error(NK_ERROR_OUT_OF_MEMORY,
                                      "could not allocate web resource stream handle");
            *out_stream = handle;
            return NK_OK;
        });
}

nk_result NK_CALL nk_resource_stream_info_get(nk_handle handle, nk_resource_stream_info *out_info) {
    if (!out_info || out_info->struct_size < sizeof(nk_resource_stream_info))
        return resource_error(NK_ERROR_INVALID_ARGUMENT, "web resource stream info is invalid");
    auto resource = get_resource_stream(handle);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    const auto struct_size = out_info->struct_size;
    std::lock_guard lock(resource->mutex);
    *out_info = {};
    out_info->struct_size = struct_size;
    out_info->flags = resource->flags;
    out_info->size = resource->data.size();
    return NK_OK;
}

nk_result NK_CALL nk_resource_read(nk_handle handle, void *buffer, uint64_t size,
                                   uint64_t *out_read) {
    if ((!buffer && size) || !out_read)
        return resource_error(NK_ERROR_INVALID_ARGUMENT, "web resource read arguments are invalid");
    auto resource = get_resource_stream(handle);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    std::lock_guard lock(resource->mutex);
    if (!(resource->flags & NK_RESOURCE_STREAM_READABLE))
        return resource_error(NK_ERROR_UNSUPPORTED, "web resource stream is not readable");
    *out_read = 0;
    if (resource->position >= resource->data.size())
        return NK_OK;
    const auto available = static_cast<uint64_t>(resource->data.size()) - resource->position;
    const auto count = std::min(size, available);
    if (count != 0)
        std::memcpy(buffer, resource->data.data() + static_cast<std::size_t>(resource->position),
                    static_cast<std::size_t>(count));
    resource->position += count;
    *out_read = count;
    return NK_OK;
}

nk_result NK_CALL nk_resource_write(nk_handle handle, const void *buffer, uint64_t size,
                                    uint64_t *out_written) {
    return nk::core::result_boundary(
        "unexpected error while writing web resource", [&]() -> nk_result {
            if ((!buffer && size) || !out_written)
                return resource_error(NK_ERROR_INVALID_ARGUMENT,
                                      "web resource write arguments are invalid");
            auto resource = get_resource_stream(handle);
            if (!resource)
                return NK_ERROR_INVALID_HANDLE;
            std::lock_guard lock(resource->mutex);
            if (!(resource->flags & NK_RESOURCE_STREAM_WRITABLE))
                return resource_error(NK_ERROR_UNSUPPORTED, "web resource stream is not writable");
            if (size > std::numeric_limits<uint64_t>::max() - resource->position)
                return resource_error(NK_ERROR_INVALID_ARGUMENT, "web resource write is too large");
            const auto end = resource->position + size;
            if (end > std::numeric_limits<std::size_t>::max())
                return resource_error(NK_ERROR_INVALID_ARGUMENT, "web resource write is too large");
            if (end > resource->data.size())
                resource->data.resize(static_cast<std::size_t>(end));
            if (size != 0)
                std::memcpy(resource->data.data() + static_cast<std::size_t>(resource->position),
                            buffer, static_cast<std::size_t>(size));
            resource->position = end;
            *out_written = size;
            return NK_OK;
        });
}

nk_result NK_CALL nk_resource_seek(nk_handle handle, int64_t offset, nk_seek_origin origin,
                                   uint64_t *out_position) {
    if (!out_position || origin > NK_SEEK_END)
        return resource_error(NK_ERROR_INVALID_ARGUMENT, "web resource seek arguments are invalid");
    auto resource = get_resource_stream(handle);
    if (!resource)
        return NK_ERROR_INVALID_HANDLE;
    std::lock_guard lock(resource->mutex);
    if (!(resource->flags & NK_RESOURCE_STREAM_SEEKABLE))
        return resource_error(NK_ERROR_UNSUPPORTED, "web resource stream is not seekable");
    const auto base = origin == NK_SEEK_START     ? uint64_t{0}
                      : origin == NK_SEEK_CURRENT ? resource->position
                                                  : static_cast<uint64_t>(resource->data.size());
    uint64_t position = 0;
    if (offset >= 0) {
        const auto distance = static_cast<uint64_t>(offset);
        if (distance > std::numeric_limits<uint64_t>::max() - base)
            return resource_error(NK_ERROR_UNKNOWN, "web resource seek failed");
        position = base + distance;
    } else {
        const auto distance = static_cast<uint64_t>(-(offset + 1)) + 1;
        if (distance > base)
            return resource_error(NK_ERROR_UNKNOWN, "web resource seek failed");
        position = base - distance;
    }
    if (position > std::numeric_limits<std::size_t>::max())
        return resource_error(NK_ERROR_UNKNOWN, "web resource seek failed");
    resource->position = position;
    *out_position = position;
    return NK_OK;
}

nk_result NK_CALL nk_resource_close(nk_handle handle) {
    return nk::core::result_boundary(
        "unexpected error while closing web resource", [&]() -> nk_result {
            auto resource = get_resource_stream(handle);
            if (!resource)
                return NK_ERROR_INVALID_HANDLE;
            PendingWebResourceWrite write;
            {
                std::lock_guard lock(resource->mutex);
                if (resource->flags & NK_RESOURCE_STREAM_WRITABLE) {
                    write.uri = resource->uri;
                    write.data = resource->data;
                }
            }
            if (write.uri.size() != 0) {
                std::lock_guard lock(pending_resource_writes_mutex);
                pending_resource_writes.push_back(std::move(write));
            }
            if (!nk::core::handles().erase(handle, nk::core::ResourceType::resource_stream))
                return resource_error(NK_ERROR_INVALID_HANDLE,
                                      "invalid web resource stream handle");
            return NK_OK;
        });
}

nk_result NK_CALL nk_window_create(const nk_window_options *options, nk_handle *out_window) {
    return nk::core::result_boundary(
        "unexpected error while creating web window", [&]() -> nk_result {
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            if (!options || options->struct_size < sizeof(nk_window_options) || !out_window ||
                options->width <= 0 || options->height <= 0)
                return invalid_argument("invalid web window options");
            if (options->owner != NK_INVALID_HANDLE || (options->flags & NK_WINDOW_MODAL))
                return unsupported("owned and modal web windows are not supported");
            auto window = std::make_shared<WebWindowResource>();
            window->generation = nk::core::runtime_generation();
            window->title = options->title ? options->title : "";
            window->visible = (options->flags & NK_WINDOW_HIDDEN) == 0;
            window->resizable = (options->flags & NK_WINDOW_RESIZABLE) != 0;
            window->last_display_orientation = nk::web::display_orientation();
            const auto handle = nk::core::handles().insert(nk::core::ResourceType::window, window);
            if (handle == NK_INVALID_HANDLE)
                return NK_ERROR_OUT_OF_MEMORY;
            window->handle = handle;
            window->owned_canvas = !web_windows.empty();
            window->selector = window->owned_canvas
                                   ? "#nativekit-window-" + std::to_string(window->handle)
                                   : nk::web::canvas_selector();
            if (!nk::web::create_canvas(window->selector.c_str(), window->owned_canvas,
                                        options->width, options->height) ||
                !nk::web::set_canvas_size(window->selector.c_str(), options->width,
                                          options->height)) {
                nk::web::destroy_canvas(window->selector.c_str(), window->owned_canvas);
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
            callbacks.device_orientation = on_device_orientation;
            callbacks.display_orientation = on_display_orientation;
            callbacks.drop = on_drop;
            callbacks.resource_dialog = on_resource_dialog;
            callbacks.notification = on_notification;
            callbacks.gamepad = on_gamepad;
            callbacks.accessibility_action = on_accessibility_action;
            if (!nk::web::install_callbacks(window->selector.c_str(), window->handle, callbacks,
                                            window.get())) {
                nk::web::destroy_canvas(window->selector.c_str(), window->owned_canvas);
                nk::core::handles().erase(handle, nk::core::ResourceType::window);
                return NK_ERROR_UNKNOWN;
            }
            web_windows.insert(handle);
            nk::web::set_canvas_size_limits(window->selector.c_str(), 0, 0, 0, 0);
            nk::web::set_canvas_aspect_ratio(window->selector.c_str(), 0, 0);
            nk::web::set_canvas_resizable(window->selector.c_str(), window->resizable);
            nk::web::set_canvas_opacity(window->selector.c_str(), window->opacity);
            nk::web::set_canvas_mouse_passthrough(window->selector.c_str(),
                                                  window->mouse_passthrough);
            nk::web::set_canvas_visible(window->selector.c_str(), window->visible);
            if (!window->title.empty())
                nk::web::set_title(window->title.c_str());
            nk::web::CanvasSize size{};
            nk::web::canvas_size(window->selector.c_str(), &size);
            apply_canvas_size(*window, size);
            queue_window_state(*window);
            *out_window = handle;
            return NK_OK;
        });
}

nk_result NK_CALL nk_window_destroy(nk_handle handle) {
    return nk::core::result_boundary(
        "unexpected error while destroying web window", [&]() -> nk_result {
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            auto window = get_window(handle);
            if (!window)
                return invalid_handle("invalid web window handle");
            const auto surfaces = window->surfaces;
            for (const auto surface : surfaces)
                nk_surface_destroy(surface);
            nk::web::remove_callbacks(window->selector.c_str(), window->handle);
            nk::web::destroy_canvas(window->selector.c_str(), window->owned_canvas);
            web_windows.erase(handle);
            if (web_windows.empty())
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
    if (window->surfaces.empty())
        nk::web::set_canvas_visible(window->selector.c_str(), window->visible);
    else
        update_canvas_visibility(*window);
    for (const auto surface_handle : window->surfaces) {
        if (auto surface = get_surface(surface_handle))
            nk::web::set_accessibility_visible(surface->handle,
                                               window->visible && surface->visible);
    }
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
    if (!nk::web::set_canvas_size(window->selector.c_str(), width, height))
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
    const bool success = enabled ? nk::web::request_fullscreen(window->selector.c_str())
                                 : nk::web::exit_fullscreen();
    if (!success)
        return unsupported("fullscreen requires a browser gesture and page permission");
    window->fullscreen = enabled != 0;
    queue_window_state(*window);
    return NK_OK;
}

nk_result NK_CALL nk_window_request_attention(nk_handle) {
    return unsupported("browser canvases have no window-manager attention state");
}

nk_result NK_CALL nk_window_set_size_limits(nk_handle handle, const nk_window_size_limits *limits) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!limits || limits->struct_size < sizeof(*limits) || limits->min_width < 0 ||
        limits->min_height < 0 || limits->max_width < 0 || limits->max_height < 0 ||
        (limits->max_width && limits->max_width < limits->min_width) ||
        (limits->max_height && limits->max_height < limits->min_height))
        return invalid_argument("invalid web window size limits");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    window->min_width = limits->min_width;
    window->min_height = limits->min_height;
    window->max_width = limits->max_width;
    window->max_height = limits->max_height;
    nk::web::set_canvas_size_limits(window->selector.c_str(), window->min_width, window->min_height,
                                    window->max_width, window->max_height);
    return NK_OK;
}

nk_result NK_CALL nk_window_get_native(nk_handle, nk_native_window *) {
    return unsupported("browser windows have no NativeKit native-window descriptor");
}

nk_result NK_CALL nk_window_wrap_native(const nk_native_window *, nk_handle *) {
    return unsupported("browser windows cannot wrap native-window descriptors");
}

nk_result NK_CALL nk_surface_create(nk_handle window_handle, const nk_surface_options *options,
                                    nk_handle *out_surface) {
    return nk::core::result_boundary(
        "unexpected error while creating web surface", [&]() -> nk_result {
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            if (!options || options->struct_size < sizeof(nk_surface_options) || !out_surface ||
                options->width <= 0 || options->height <= 0)
                return invalid_argument("invalid web surface options");
            if (options->api != NK_GRAPHICS_OPENGL && options->api != NK_GRAPHICS_OPENGL_ES)
                return unsupported("the first web backend supports WebGL2 only");
            auto window = get_window(window_handle);
            if (!window)
                return invalid_handle("invalid web surface parent window");
            if (!nk::web::set_canvas_size(window->selector.c_str(), options->width,
                                          options->height))
                return NK_ERROR_UNKNOWN;
            nk::web::WebGLContextOptions context_options{};
            context_options.alpha = (options->flags & NK_SURFACE_ALPHA) != 0;
            context_options.depth = (options->flags & NK_SURFACE_DEPTH) != 0;
            context_options.stencil = (options->flags & NK_SURFACE_STENCIL) != 0;
            std::shared_ptr<WebGLContextResource> graphics = window->graphics;
            if (options->share_surface != NK_INVALID_HANDLE) {
                auto shared_surface = get_surface(options->share_surface);
                if (!shared_surface)
                    return invalid_handle("invalid shared web surface handle");
                if (shared_surface->parent != window_handle || !shared_surface->graphics)
                    return invalid_argument("shared web surface belongs to another window");
                graphics = shared_surface->graphics;
            }
            if (!graphics) {
                EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
                if (!nk::web::create_webgl_context(window->selector.c_str(), context_options,
                                                   &context)) {
                    nk::core::set_error(
                        "could not create a WebGL2 context for the NativeKit canvas");
                    return NK_ERROR_UNKNOWN;
                }
                graphics = std::make_shared<WebGLContextResource>();
                graphics->context = context;
                window->graphics = graphics;
            }
            auto surface = std::make_shared<WebSurfaceResource>();
            surface->parent = window_handle;
            surface->generation = nk::core::runtime_generation();
            surface->graphics = std::move(graphics);
            const auto handle =
                nk::core::handles().insert(nk::core::ResourceType::surface, surface);
            if (handle == NK_INVALID_HANDLE) {
                return NK_ERROR_OUT_OF_MEMORY;
            }
            surface->handle = handle;
            window->surfaces.push_back(handle);
            sync_canvas_size(*window);
            surface->width = window->width;
            surface->height = window->height;
            surface->framebuffer_width = window->framebuffer_width;
            surface->framebuffer_height = window->framebuffer_height;
            surface->visible = (options->flags & NK_SURFACE_HIDDEN) == 0;
            update_canvas_visibility(*window);
            refresh_web_accessibility(*surface);
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
    if (nk_core_graphics_device_has_references(nk_graphics_device{handle}))
        return NK_ERROR_INVALID_REQUEST;
    if (surface->text_input_active) {
        if (auto window = get_window(surface->parent)) {
            if (window->text_input_surface == surface->handle) {
                window->text_input_surface = NK_INVALID_HANDLE;
                surface->text_input_active = false;
                configure_text_input(*surface);
            }
        }
    }
    const auto frame_user_data = reinterpret_cast<void *>(static_cast<uintptr_t>(surface->handle));
    if (surface->frame_callback)
        nk::web::stop_frame_loop(frame_loop, frame_user_data);
    surface->frame_callback = nullptr;
    surface->frame_user_data = nullptr;
    nk::web::clear_accessibility_tree(surface->handle);
    auto window = get_window(surface->parent);
    remove_surface_from_window(*surface);
    if (window)
        update_canvas_visibility(*window);
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
    if (window) {
        update_canvas_visibility(*window);
        nk::web::set_accessibility_visible(surface->handle, surface->visible && window->visible);
    }
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
    if (!nk::web::set_canvas_size(window->selector.c_str(), width, height))
        return NK_ERROR_UNKNOWN;
    sync_canvas_size(*window);
    return NK_OK;
}

nk_result NK_CALL nk_surface_accessibility_set_node(nk_handle handle,
                                                    const nk_accessibility_node *node) {
    return nk::core::result_boundary(
        "unexpected error while setting a Web accessibility node", [&]() -> nk_result {
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            auto surface = get_surface(handle);
            if (!surface)
                return invalid_handle("invalid web accessibility surface handle");
            if (!node)
                return invalid_argument("Web accessibility node is missing");
            WebAccessibilityNode copy;
            if (!copy_web_accessibility_node(*node, surface->accessibility_nodes, copy))
                return NK_ERROR_INVALID_ARGUMENT;
            if (const auto old = surface->accessibility_nodes.find(node->id);
                old != surface->accessibility_nodes.end())
                copy.text_ranges = old->second.text_ranges;
            surface->accessibility_nodes[node->id] = std::move(copy);
            refresh_web_accessibility(*surface);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_remove_node(nk_handle handle,
                                                       nk_accessibility_node_id node) {
    return nk::core::result_boundary(
        "unexpected error while removing a Web accessibility node", [&]() -> nk_result {
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            auto surface = get_surface(handle);
            if (!surface)
                return invalid_handle("invalid web accessibility surface handle");
            if (!node ||
                surface->accessibility_nodes.find(node) == surface->accessibility_nodes.end())
                return invalid_argument("invalid or unknown Web accessibility node");
            remove_web_accessibility_descendants(surface->accessibility_nodes, node);
            if (surface->accessibility_nodes.find(surface->accessibility_focus) ==
                surface->accessibility_nodes.end())
                surface->accessibility_focus = NK_ACCESSIBILITY_ROOT;
            refresh_web_accessibility(*surface);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_clear(nk_handle handle) {
    return nk::core::result_boundary(
        "unexpected error while clearing Web accessibility nodes", [&]() -> nk_result {
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            auto surface = get_surface(handle);
            if (!surface)
                return invalid_handle("invalid web accessibility surface handle");
            surface->accessibility_nodes.clear();
            surface->accessibility_focus = NK_ACCESSIBILITY_ROOT;
            refresh_web_accessibility(*surface);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_set_focus(nk_handle handle,
                                                     nk_accessibility_node_id node) {
    return nk::core::result_boundary(
        "unexpected error while focusing a Web accessibility node", [&]() -> nk_result {
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            auto surface = get_surface(handle);
            if (!surface)
                return invalid_handle("invalid web accessibility surface handle");
            if (node != NK_ACCESSIBILITY_ROOT &&
                surface->accessibility_nodes.find(node) == surface->accessibility_nodes.end())
                return invalid_argument("cannot focus an unknown Web accessibility node");
            surface->accessibility_focus = node;
            refresh_web_accessibility(*surface);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_update(nk_handle handle,
                                                  const nk_accessibility_update *update) {
    return nk::core::result_boundary(
        "unexpected error while updating Web accessibility nodes", [&]() -> nk_result {
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            auto surface = get_surface(handle);
            if (!surface)
                return invalid_handle("invalid web accessibility surface handle");
            if (!update || update->struct_size < sizeof(*update) ||
                (update->flags & ~NK_ACCESSIBILITY_UPDATE_FOCUS) ||
                (update->node_count && !update->nodes) ||
                (update->removed_node_count && !update->removed_nodes))
                return invalid_argument("invalid Web accessibility update");
            auto nodes = surface->accessibility_nodes;
            for (uint32_t index = 0; index < update->removed_node_count; ++index) {
                const auto removed = update->removed_nodes[index];
                if (!removed || nodes.find(removed) == nodes.end())
                    return invalid_argument("Web accessibility update removes an unknown node");
                remove_web_accessibility_descendants(nodes, removed);
            }
            for (uint32_t index = 0; index < update->node_count; ++index) {
                const auto &node = update->nodes[index];
                WebAccessibilityNode copy;
                if (!copy_web_accessibility_node(node, nodes, copy))
                    return NK_ERROR_INVALID_ARGUMENT;
                if (const auto old = nodes.find(node.id); old != nodes.end())
                    copy.text_ranges = old->second.text_ranges;
                nodes[node.id] = std::move(copy);
            }
            if ((update->flags & NK_ACCESSIBILITY_UPDATE_FOCUS) &&
                update->focus != NK_ACCESSIBILITY_ROOT && nodes.find(update->focus) == nodes.end())
                return invalid_argument("Web accessibility update focuses an unknown node");
            surface->accessibility_nodes = std::move(nodes);
            if (update->flags & NK_ACCESSIBILITY_UPDATE_FOCUS)
                surface->accessibility_focus = update->focus;
            else if (surface->accessibility_focus != NK_ACCESSIBILITY_ROOT &&
                     surface->accessibility_nodes.find(surface->accessibility_focus) ==
                         surface->accessibility_nodes.end())
                surface->accessibility_focus = NK_ACCESSIBILITY_ROOT;
            refresh_web_accessibility(*surface);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_accessibility_set_text_ranges(
    nk_handle handle, nk_accessibility_node_id node, const nk_accessibility_text_range *ranges,
    uint32_t range_count) {
    return nk::core::result_boundary(
        "unexpected error while setting Web accessibility text ranges", [&]() -> nk_result {
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            auto surface = get_surface(handle);
            if (!surface)
                return invalid_handle("invalid web accessibility surface handle");
            const auto found = surface->accessibility_nodes.find(node);
            if (!node || found == surface->accessibility_nodes.end() || (range_count && !ranges))
                return invalid_argument("invalid Web accessibility text ranges");
            std::vector<WebAccessibilityTextRange> copy;
            copy.reserve(range_count);
            nk_accessibility_text_position previous = 0;
            for (uint32_t index = 0; index < range_count; ++index) {
                const auto &range = ranges[index];
                if (range.start >= range.end || range.start < previous ||
                    range.end > found->second.document_length || !std::isfinite(range.x) ||
                    !std::isfinite(range.y) || !std::isfinite(range.width) ||
                    !std::isfinite(range.height) || range.width < 0 || range.height < 0)
                    return invalid_argument("invalid or unordered Web accessibility text ranges");
                copy.push_back(
                    {range.start, range.end, range.x, range.y, range.width, range.height});
                previous = range.end;
            }
            found->second.text_ranges = std::move(copy);
            refresh_web_accessibility(*surface);
            return NK_OK;
        });
}

nk_result NK_CALL nk_surface_make_current(nk_handle handle) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    auto surface = get_surface(handle);
    if (!surface)
        return invalid_handle("invalid web surface handle");
    return nk::web::make_context_current(surface->context()) ? NK_OK : NK_ERROR_UNKNOWN;
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
    surface->frame_user_data = callback ? user_data : nullptr;
    if (!callback) {
        disarm_surface_frames(surface);
        return NK_OK;
    }
    if (!surface->frame_requests.continuous() && !surface->frame_requests.pending())
        return NK_OK;
    const auto result = arm_surface_frames(surface);
    if (result != NK_OK) {
        surface->frame_callback = nullptr;
        surface->frame_user_data = nullptr;
        return result;
    }
    return NK_OK;
}

nk_result NK_CALL nk_surface_set_frame_mode(nk_handle handle, nk_surface_frame_mode mode) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (mode != NK_SURFACE_FRAME_CONTINUOUS && mode != NK_SURFACE_FRAME_ON_DEMAND)
        return invalid_argument("unknown graphics surface frame mode");
    auto surface = get_surface(handle);
    if (!surface)
        return invalid_handle("invalid web surface handle");
    surface->frame_requests.set_continuous(mode == NK_SURFACE_FRAME_CONTINUOUS);
    if (surface->frame_requests.continuous() || surface->frame_requests.pending())
        return arm_surface_frames(surface);
    disarm_surface_frames(surface);
    return NK_OK;
}

nk_result NK_CALL nk_surface_request_frame(nk_handle handle) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    auto surface = get_surface(handle);
    if (!surface)
        return invalid_handle("invalid web surface handle");
    surface->frame_requests.request();
    if (surface->frame_requests.continuous())
        return NK_OK;
    return arm_surface_frames(surface);
}

nk_result NK_CALL nk_surface_set_text_input_state(nk_handle handle,
                                                  const nk_text_input_state *state) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!state || state->struct_size < sizeof(nk_text_input_state))
        return invalid_argument("text input state is missing or too small");
    const std::string text = state->text ? state->text : "";
    uint32_t codepoints = utf8_codepoints(text);
    const uint64_t text_end = static_cast<uint64_t>(state->text_start) + codepoints;
    const bool no_composition = state->composition_start == NK_TEXT_POSITION_NONE &&
                                state->composition_end == NK_TEXT_POSITION_NONE;
    const bool valid_composition = state->composition_start != NK_TEXT_POSITION_NONE &&
                                   state->composition_end != NK_TEXT_POSITION_NONE &&
                                   state->composition_start <= state->composition_end &&
                                   state->composition_start >= state->text_start &&
                                   state->composition_end <= text_end;
    const bool valid_cursor = std::isfinite(state->cursor_x) && std::isfinite(state->cursor_y) &&
                              std::isfinite(state->cursor_width) &&
                              std::isfinite(state->cursor_height) && state->cursor_width >= 0.0f &&
                              state->cursor_height >= 0.0f;
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
    if (!nk::core::surface_frame_target_output_valid(out_target))
        return invalid_argument("invalid web surface frame target output");
    auto surface = get_surface(handle);
    if (!surface)
        return invalid_handle("invalid web surface handle");
    nk_surface_frame_target target{};
    target.struct_size = out_target->struct_size;
    target.api = NK_GRAPHICS_OPENGL_ES;
    target.width = surface->framebuffer_width;
    target.height = surface->framebuffer_height;
    target.device.id = surface->handle;
    nk::core::write_surface_frame_target(out_target, target);
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
nk_result NK_CALL nk_cursor_create_custom(const nk_cursor_image *image, nk_handle *out_cursor) {
    return nk::core::result_boundary(
        "unexpected error while creating custom Web cursor", [&]() -> nk_result {
            if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
                return result;
            if (!image || image->struct_size < sizeof(*image) || !out_cursor || !image->rgba ||
                image->width <= 0 || image->height <= 0 || image->width > INT32_MAX / 4 ||
                image->stride < image->width * 4 || image->hotspot_x < 0 || image->hotspot_y < 0 ||
                image->hotspot_x >= image->width || image->hotspot_y >= image->height)
                return invalid_argument("invalid custom Web cursor image");
            *out_cursor = NK_INVALID_HANDLE;
            auto cursor = std::make_shared<WebCursorResource>();
            cursor->css = custom_cursor_css(*image);
            cursor->handle = nk::core::handles().insert(nk::core::ResourceType::cursor, cursor);
            if (cursor->handle == NK_INVALID_HANDLE)
                return resource_error(NK_ERROR_OUT_OF_MEMORY, "Web cursor handle registry is full");
            *out_cursor = cursor->handle;
            return NK_OK;
        });
}
nk_result NK_CALL nk_cursor_destroy(nk_handle handle) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!get_resource<WebCursorResource>(handle, nk::core::ResourceType::cursor,
                                         "invalid web cursor handle"))
        return invalid_handle("invalid web cursor handle");
    return nk::core::handles().erase(handle, nk::core::ResourceType::cursor)
               ? NK_OK
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
    if (!nk::web::request_pointer_lock(window->selector.c_str()))
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

nk_result NK_CALL nk_window_set_aspect_ratio(nk_handle handle, int32_t numerator,
                                             int32_t denominator) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if ((numerator == 0) != (denominator == 0) || numerator < 0 || denominator < 0)
        return invalid_argument("invalid web window aspect ratio");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    window->aspect_numerator = numerator;
    window->aspect_denominator = denominator;
    nk::web::set_canvas_aspect_ratio(window->selector.c_str(), numerator, denominator);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_resizable(nk_handle handle, nk_bool enabled) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    window->resizable = enabled != 0;
    nk::web::set_canvas_resizable(window->selector.c_str(), window->resizable);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_decorated(nk_handle, nk_bool) {
    return unsupported("browser canvas decorations are controlled by the page");
}
nk_result NK_CALL nk_window_set_decoration_regions(nk_handle, const nk_window_decoration_region *,
                                                   uint32_t) {
    return unsupported("browser canvas decorations are controlled by the page");
}
nk_result NK_CALL nk_window_set_floating(nk_handle, nk_bool) {
    return unsupported("browser canvases have no window-manager stacking state");
}
nk_result NK_CALL nk_window_set_opacity(nk_handle handle, float opacity) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    if (!(opacity >= 0.0f && opacity <= 1.0f))
        return invalid_argument("web window opacity must be between zero and one");
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    window->opacity = opacity;
    nk::web::set_canvas_opacity(window->selector.c_str(), window->opacity);
    return NK_OK;
}

nk_result NK_CALL nk_window_set_mouse_passthrough(nk_handle handle, nk_bool enabled) {
    if (const auto result = nk::core::require_ui_thread(); result != NK_OK)
        return result;
    auto window = get_window(handle);
    if (!window)
        return invalid_handle("invalid web window handle");
    window->mouse_passthrough = enabled != 0;
    if (window->mouse_passthrough)
        window->hovered = false;
    nk::web::set_canvas_mouse_passthrough(window->selector.c_str(), window->mouse_passthrough);
    return NK_OK;
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
