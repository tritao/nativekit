#include "nativekit_win_accessibility.hpp"

#include "nativekit_accessibility.h"
#include "core/boundary.hpp"
#include "core/error.hpp"
#include "core/runtime.hpp"

// UIAutomationCore's forward declarations need COM headers omitted by WIN32_LEAN_AND_MEAN.
#include <Unknwn.h>
#include <UIAutomation.h>

#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <functional>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace nk::windows {
namespace {

struct TextRange {
    nk_accessibility_text_position start = 0;
    nk_accessibility_text_position end = 0;
    float x = 0;
    float y = 0;
    float width = 0;
    float height = 0;
};

struct SemanticNode {
    nk_accessibility_node_id id = 0;
    nk_accessibility_node_id parent = 0;
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
    std::vector<TextRange> text_ranges;
};

struct AccessibilityHost {
    HWND window = nullptr;
    nk_handle surface = NK_INVALID_HANDLE;
    bool active = true;
    mutable std::shared_mutex mutex;
    std::unordered_map<nk_accessibility_node_id, SemanticNode> nodes;
    nk_accessibility_node_id focus = NK_ACCESSIBILITY_ROOT;
};

std::mutex hosts_mutex;
std::unordered_map<nk_handle, std::shared_ptr<AccessibilityHost>> hosts_by_surface;
std::unordered_map<HWND, nk_handle> surfaces_by_window;

std::shared_ptr<AccessibilityHost> host_for_surface(nk_handle surface) {
    std::lock_guard lock(hosts_mutex);
    const auto found = hosts_by_surface.find(surface);
    return found == hosts_by_surface.end() ? nullptr : found->second;
}

std::shared_ptr<AccessibilityHost> host_for_window(HWND window) {
    std::lock_guard lock(hosts_mutex);
    const auto found = surfaces_by_window.find(window);
    if (found == surfaces_by_window.end())
        return nullptr;
    const auto host = hosts_by_surface.find(found->second);
    return host == hosts_by_surface.end() ? nullptr : host->second;
}

bool has_action(const SemanticNode &node, nk_accessibility_actions action) {
    return (node.actions & action) != 0;
}

nk_result invalid_argument(const char *message) {
    nk::core::set_error(message);
    return NK_ERROR_INVALID_ARGUMENT;
}

bool utf8_to_wide(const std::string &input, std::wstring &output) {
    if (input.empty()) {
        output.clear();
        return true;
    }
    if (input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;
    const int length = static_cast<int>(input.size());
    const int required =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), length, nullptr, 0);
    if (required <= 0)
        return false;
    output.resize(static_cast<std::size_t>(required));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), length,
                               output.data(), required) == required;
}

bool wide_to_utf8(const wchar_t *input, std::string &output) {
    output.clear();
    if (!input)
        return true;
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, -1, nullptr,
                                             0, nullptr, nullptr);
    if (required <= 0)
        return false;
    std::vector<char> buffer(static_cast<std::size_t>(required));
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, input, -1, buffer.data(), required,
                            nullptr, nullptr) != required)
        return false;
    output.assign(buffer.data(), static_cast<std::size_t>(required - 1));
    return true;
}

bool valid_utf8(const char *value) {
    if (!value)
        return true;
    const auto length = std::strlen(value);
    if (length > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        return false;
    return length == 0 || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value,
                                              static_cast<int>(length), nullptr, 0) > 0;
}

uint64_t utf8_codepoint_count(const char *value) {
    uint64_t count = 0;
    if (value) {
        for (const auto *cursor = reinterpret_cast<const unsigned char *>(value); *cursor; ++cursor)
            count += (*cursor & 0xc0u) != 0x80u;
    }
    return count;
}

constexpr nk_accessibility_states all_states =
    NK_ACCESSIBILITY_FOCUSABLE | NK_ACCESSIBILITY_FOCUSED | NK_ACCESSIBILITY_SELECTED |
    NK_ACCESSIBILITY_CHECKED | NK_ACCESSIBILITY_DISABLED | NK_ACCESSIBILITY_READ_ONLY |
    NK_ACCESSIBILITY_MULTILINE | NK_ACCESSIBILITY_PASSWORD | NK_ACCESSIBILITY_EXPANDED |
    NK_ACCESSIBILITY_MODAL | NK_ACCESSIBILITY_REQUIRED | NK_ACCESSIBILITY_INVALID |
    NK_ACCESSIBILITY_BUSY | NK_ACCESSIBILITY_HAS_POPUP;

constexpr nk_accessibility_actions all_actions =
    NK_ACCESSIBILITY_CAN_ACTIVATE | NK_ACCESSIBILITY_CAN_FOCUS | NK_ACCESSIBILITY_CAN_SET_VALUE |
    NK_ACCESSIBILITY_CAN_SET_SELECTION | NK_ACCESSIBILITY_CAN_INCREMENT |
    NK_ACCESSIBILITY_CAN_DECREMENT | NK_ACCESSIBILITY_CAN_SCROLL_FORWARD |
    NK_ACCESSIBILITY_CAN_SCROLL_BACKWARD | NK_ACCESSIBILITY_CAN_MOVE_NEXT |
    NK_ACCESSIBILITY_CAN_MOVE_PREVIOUS | NK_ACCESSIBILITY_CAN_TOGGLE | NK_ACCESSIBILITY_CAN_SELECT |
    NK_ACCESSIBILITY_CAN_DESELECT | NK_ACCESSIBILITY_CAN_EXPAND | NK_ACCESSIBILITY_CAN_COLLAPSE |
    NK_ACCESSIBILITY_CAN_DISMISS | NK_ACCESSIBILITY_CAN_SHOW_CONTEXT_MENU |
    NK_ACCESSIBILITY_CAN_SCROLL_INTO_VIEW;

bool valid_node(const nk_accessibility_node &node, SemanticNode &copy) {
    if (node.struct_size < sizeof(node) || node.id == NK_ACCESSIBILITY_ROOT ||
        node.id > static_cast<uint32_t>(INT_MAX) ||
        node.parent_id > static_cast<uint32_t>(INT_MAX) || node.id == node.parent_id ||
        node.child_index > static_cast<uint32_t>(INT_MAX) || node.role > NK_ACCESSIBILITY_ALERT ||
        node.orientation > NK_ACCESSIBILITY_ORIENTATION_VERTICAL || (node.states & ~all_states) ||
        (node.actions & ~all_actions) || node.text_start > static_cast<uint32_t>(INT_MAX) ||
        node.document_length > static_cast<uint32_t>(INT_MAX) || !std::isfinite(node.x) ||
        !std::isfinite(node.y) || !std::isfinite(node.width) || !std::isfinite(node.height) ||
        node.width < 0 || node.height < 0 || !std::isfinite(node.numeric_value) ||
        !std::isfinite(node.numeric_minimum) || !std::isfinite(node.numeric_maximum) ||
        !valid_utf8(node.label) || !valid_utf8(node.value) ||
        (node.role == NK_ACCESSIBILITY_SLIDER &&
         (node.numeric_minimum > node.numeric_maximum ||
          node.numeric_value < node.numeric_minimum || node.numeric_value > node.numeric_maximum)))
        return false;

    const uint64_t text_end =
        static_cast<uint64_t>(node.text_start) + utf8_codepoint_count(node.value);
    const bool no_selection = node.selection_start == NK_ACCESSIBILITY_TEXT_POSITION_NONE &&
                              node.selection_end == NK_ACCESSIBILITY_TEXT_POSITION_NONE;
    const bool valid_selection = node.selection_start != NK_ACCESSIBILITY_TEXT_POSITION_NONE &&
                                 node.selection_end != NK_ACCESSIBILITY_TEXT_POSITION_NONE &&
                                 node.selection_start <= node.selection_end &&
                                 node.selection_start >= node.text_start &&
                                 node.selection_end <= text_end;
    if (text_end > node.document_length || (!no_selection && !valid_selection))
        return false;

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

void remove_descendants(std::unordered_map<nk_accessibility_node_id, SemanticNode> &nodes,
                        nk_accessibility_node_id node) {
    std::unordered_map<nk_accessibility_node_id, std::vector<nk_accessibility_node_id>> children;
    children.reserve(nodes.size());
    for (const auto &[id, item] : nodes)
        children[item.parent].push_back(id);

    std::vector<nk_accessibility_node_id> pending{node};
    std::unordered_set<nk_accessibility_node_id> removed{node};
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const auto found = children.find(pending[index]);
        if (found == children.end())
            continue;
        for (const auto candidate : found->second)
            if (removed.insert(candidate).second)
                pending.push_back(candidate);
    }
    for (const auto id : removed)
        nodes.erase(id);
}

bool node_has_parent(const std::unordered_map<nk_accessibility_node_id, SemanticNode> &nodes,
                     const SemanticNode &node) {
    if (node.parent == NK_ACCESSIBILITY_ROOT)
        return true;
    auto ancestor = node.parent;
    for (std::size_t depth = 0; depth <= nodes.size(); ++depth) {
        if (ancestor == node.id)
            return false;
        const auto found = nodes.find(ancestor);
        if (found == nodes.end())
            return false;
        ancestor = found->second.parent;
        if (ancestor == NK_ACCESSIBILITY_ROOT)
            return true;
    }
    return false;
}

std::vector<SemanticNode> children_of(const AccessibilityHost &host,
                                      nk_accessibility_node_id parent) {
    std::vector<SemanticNode> children;
    for (const auto &[id, node] : host.nodes) {
        (void)id;
        if (node.parent == parent)
            children.push_back(node);
    }
    std::sort(children.begin(), children.end(),
              [](const SemanticNode &left, const SemanticNode &right) {
                  if (left.child_index != right.child_index)
                      return left.child_index < right.child_index;
                  return left.id < right.id;
              });
    return children;
}

bool focusable(const SemanticNode &node) {
    return (node.states & NK_ACCESSIBILITY_FOCUSABLE) ||
           has_action(node, NK_ACCESSIBILITY_CAN_FOCUS);
}

bool enabled(const SemanticNode &node) {
    return (node.states & NK_ACCESSIBILITY_DISABLED) == 0;
}

bool selected(const SemanticNode &node) {
    return (node.states & NK_ACCESSIBILITY_SELECTED) != 0;
}

bool focused(const AccessibilityHost &host, const SemanticNode &node) {
    return host.focus == node.id || (node.states & NK_ACCESSIBILITY_FOCUSED) != 0;
}

int control_type(nk_accessibility_role role) {
    switch (role) {
    case NK_ACCESSIBILITY_GROUP:
    case NK_ACCESSIBILITY_COLLECTION:
    case NK_ACCESSIBILITY_TAB_PANEL:
        return UIA_GroupControlTypeId;
    case NK_ACCESSIBILITY_BUTTON:
        return UIA_ButtonControlTypeId;
    case NK_ACCESSIBILITY_CHECKBOX:
    case NK_ACCESSIBILITY_SWITCH:
        return UIA_CheckBoxControlTypeId;
    case NK_ACCESSIBILITY_RADIO:
        return UIA_RadioButtonControlTypeId;
    case NK_ACCESSIBILITY_TEXT:
    case NK_ACCESSIBILITY_HEADING:
    case NK_ACCESSIBILITY_STATUS:
    case NK_ACCESSIBILITY_ALERT:
        return UIA_TextControlTypeId;
    case NK_ACCESSIBILITY_TEXT_FIELD:
        return UIA_EditControlTypeId;
    case NK_ACCESSIBILITY_LINK:
        return UIA_HyperlinkControlTypeId;
    case NK_ACCESSIBILITY_IMAGE:
        return UIA_ImageControlTypeId;
    case NK_ACCESSIBILITY_LIST:
        return UIA_ListControlTypeId;
    case NK_ACCESSIBILITY_LIST_ITEM:
    case NK_ACCESSIBILITY_COLLECTION_ITEM:
        return UIA_ListItemControlTypeId;
    case NK_ACCESSIBILITY_SLIDER:
        return UIA_SliderControlTypeId;
    case NK_ACCESSIBILITY_SCROLL_AREA:
        return UIA_PaneControlTypeId;
    case NK_ACCESSIBILITY_DIALOG:
        return UIA_WindowControlTypeId;
    case NK_ACCESSIBILITY_MENU:
        return UIA_MenuControlTypeId;
    case NK_ACCESSIBILITY_MENU_BAR:
        return UIA_MenuBarControlTypeId;
    case NK_ACCESSIBILITY_MENU_ITEM:
        return UIA_MenuItemControlTypeId;
    case NK_ACCESSIBILITY_TAB_LIST:
        return UIA_TabControlTypeId;
    case NK_ACCESSIBILITY_TAB:
        return UIA_TabItemControlTypeId;
    case NK_ACCESSIBILITY_PROGRESS_BAR:
        return UIA_ProgressBarControlTypeId;
    case NK_ACCESSIBILITY_COMBO_BOX:
        return UIA_ComboBoxControlTypeId;
    case NK_ACCESSIBILITY_GRID:
        return UIA_DataGridControlTypeId;
    case NK_ACCESSIBILITY_ROW:
    case NK_ACCESSIBILITY_CELL:
        return UIA_DataItemControlTypeId;
    case NK_ACCESSIBILITY_COLUMN_HEADER:
    case NK_ACCESSIBILITY_ROW_HEADER:
        return UIA_HeaderItemControlTypeId;
    case NK_ACCESSIBILITY_TREE:
        return UIA_TreeControlTypeId;
    case NK_ACCESSIBILITY_TREE_ITEM:
        return UIA_TreeItemControlTypeId;
    case NK_ACCESSIBILITY_SEPARATOR:
        return UIA_SeparatorControlTypeId;
    case NK_ACCESSIBILITY_TOOLBAR:
        return UIA_ToolBarControlTypeId;
    default:
        return UIA_CustomControlTypeId;
    }
}

bool selection_container_role(nk_accessibility_role role) {
    return role == NK_ACCESSIBILITY_LIST || role == NK_ACCESSIBILITY_COLLECTION ||
           role == NK_ACCESSIBILITY_TAB_LIST || role == NK_ACCESSIBILITY_GRID ||
           role == NK_ACCESSIBILITY_TREE;
}

bool supports_selection_item(const SemanticNode &node) {
    return (node.role == NK_ACCESSIBILITY_RADIO || node.role == NK_ACCESSIBILITY_TAB ||
            node.role == NK_ACCESSIBILITY_LIST_ITEM ||
            node.role == NK_ACCESSIBILITY_COLLECTION_ITEM ||
            node.role == NK_ACCESSIBILITY_TREE_ITEM || node.role == NK_ACCESSIBILITY_CELL) &&
           (has_action(node, NK_ACCESSIBILITY_CAN_SELECT) ||
            has_action(node, NK_ACCESSIBILITY_CAN_DESELECT));
}

bool supports_selection_item(
    const SemanticNode &node,
    const std::unordered_map<nk_accessibility_node_id, SemanticNode> &nodes) {
    if (!supports_selection_item(node))
        return false;
    const auto parent = nodes.find(node.parent);
    return parent != nodes.end() && selection_container_role(parent->second.role);
}

template <typename Function> Function resolve_function(HMODULE module, const char *name) noexcept {
    Function function = nullptr;
    if (!module)
        return function;
    const auto address = GetProcAddress(module, name);
    static_assert(sizeof(function) == sizeof(address));
    std::memcpy(&function, &address, sizeof(function));
    return function;
}

HMODULE load_uia_module() noexcept {
    if (const auto module =
            LoadLibraryExW(L"uiautomationcore.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
        return module;

    // LOAD_LIBRARY_SEARCH_SYSTEM32 was added after the oldest Windows versions that
    // provide UI Automation. Fall back to an explicit system directory path there.
    wchar_t path[MAX_PATH + 64]{};
    const UINT length = GetSystemDirectoryW(path, MAX_PATH);
    constexpr wchar_t suffix[] = L"\\uiautomationcore.dll";
    if (!length || length >= MAX_PATH || length + std::size(suffix) > std::size(path))
        return nullptr;
    std::memcpy(path + length, suffix, sizeof(suffix));
    return LoadLibraryW(path);
}

struct UiaApi {
    using ClientsAreListeningFn = BOOL(WINAPI *)();
    HMODULE module = load_uia_module();
    ClientsAreListeningFn clients_are_listening =
        resolve_function<ClientsAreListeningFn>(module, "UiaClientsAreListening");
    decltype(&UiaHostProviderFromHwnd) host_provider_from_hwnd =
        resolve_function<decltype(&UiaHostProviderFromHwnd)>(module, "UiaHostProviderFromHwnd");
    decltype(&UiaRaiseAutomationPropertyChangedEvent) raise_property_changed =
        resolve_function<decltype(&UiaRaiseAutomationPropertyChangedEvent)>(
            module, "UiaRaiseAutomationPropertyChangedEvent");
    decltype(&UiaRaiseAutomationEvent) raise_automation_event =
        resolve_function<decltype(&UiaRaiseAutomationEvent)>(module, "UiaRaiseAutomationEvent");
    decltype(&UiaRaiseStructureChangedEvent) raise_structure_changed =
        resolve_function<decltype(&UiaRaiseStructureChangedEvent)>(module,
                                                                   "UiaRaiseStructureChangedEvent");
    decltype(&UiaReturnRawElementProvider) return_raw_element_provider =
        resolve_function<decltype(&UiaReturnRawElementProvider)>(module,
                                                                 "UiaReturnRawElementProvider");
};

const UiaApi &uia_api() {
    static const UiaApi api;
    return api;
}

bool uia_clients_are_listening() noexcept {
    const auto query = uia_api().clients_are_listening;
    // Older UI Automation versions may omit this optimization; raising events is safe.
    return !query || query();
}

bool make_bstr(const std::string &value, BSTR *output) noexcept {
    std::wstring wide;
    if (!utf8_to_wide(value, wide) || wide.size() > std::numeric_limits<UINT>::max())
        return false;
    if (wide.empty()) {
        *output = SysAllocString(L"");
        return *output != nullptr;
    }
    *output = SysAllocStringLen(wide.data(), static_cast<UINT>(wide.size()));
    return *output != nullptr;
}

void variant_bstr(VARIANT &value, const std::string &text) {
    value.vt = VT_BSTR;
    if (!make_bstr(text, &value.bstrVal))
        value.vt = VT_EMPTY;
}

void variant_bool(VARIANT &value, bool state) {
    value.vt = VT_BOOL;
    value.boolVal = state ? VARIANT_TRUE : VARIANT_FALSE;
}

void variant_i4(VARIANT &value, LONG state) {
    value.vt = VT_I4;
    value.lVal = state;
}

void variant_r8(VARIANT &value, double state) {
    value.vt = VT_R8;
    value.dblVal = state;
}

HRESULT queue_action(const std::shared_ptr<AccessibilityHost> &host, const SemanticNode &node,
                     nk_accessibility_action action, const std::string &value = {}) noexcept {
    {
        if (!host)
            return UIA_E_ELEMENTNOTAVAILABLE;
        {
            std::shared_lock lock(host->mutex);
            if (!host->active || host->nodes.find(node.id) == host->nodes.end())
                return UIA_E_ELEMENTNOTAVAILABLE;
        }
        nk_accessibility_action_event payload{};
        payload.node_id = node.id;
        payload.action = action;
        payload.selection_start = NK_ACCESSIBILITY_TEXT_POSITION_NONE;
        payload.selection_end = NK_ACCESSIBILITY_TEXT_POSITION_NONE;
        if (!value.empty()) {
            if (value.size() > std::numeric_limits<uint32_t>::max())
                return E_INVALIDARG;
            payload.value_offset = sizeof(payload);
            payload.value_length = static_cast<uint32_t>(value.size());
        }
        nk::core::QueuedEvent event;
        event.kind = NK_EVENT_ACCESSIBILITY_ACTION;
        event.source = host->surface;
        event.data.resize(sizeof(payload) + value.size() + (value.empty() ? 0u : 1u));
        std::memcpy(event.data.data(), &payload, sizeof(payload));
        if (!value.empty())
            std::memcpy(event.data.data() + sizeof(payload), value.c_str(), value.size() + 1);
        const nk_result result = nk::core::push_event(std::move(event));
        if (result == NK_OK)
            return S_OK;
        if (result == NK_ERROR_QUEUE_FULL)
            return HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_QUOTA);
        return result == NK_ERROR_OUT_OF_MEMORY ? E_OUTOFMEMORY : UIA_E_ELEMENTNOTAVAILABLE;
    }
}

class AccessibilityProvider final : public IRawElementProviderSimple,
                                    public IRawElementProviderFragment,
                                    public IRawElementProviderFragmentRoot,
                                    public IInvokeProvider,
                                    public IValueProvider,
                                    public IRangeValueProvider,
                                    public IToggleProvider,
                                    public ISelectionProvider,
                                    public ISelectionItemProvider,
                                    public IExpandCollapseProvider,
                                    public IScrollItemProvider {
  public:
    AccessibilityProvider(std::shared_ptr<AccessibilityHost> host, nk_accessibility_node_id node)
        : host_(std::move(host)), node_(node) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override;
    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (!remaining)
            delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions *value) override;
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown **value) override;
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT *value) override;
    HRESULT STDMETHODCALLTYPE
    get_HostRawElementProvider(IRawElementProviderSimple **value) override;
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
                                       IRawElementProviderFragment **value) override;
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY **value) override;
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect *value) override;
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY **value) override;
    HRESULT STDMETHODCALLTYPE SetFocus() override;
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot **value) override;
    HRESULT STDMETHODCALLTYPE
    ElementProviderFromPoint(double x, double y, IRawElementProviderFragment **value) override;
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment **value) override;

    HRESULT STDMETHODCALLTYPE Invoke() override;
    HRESULT STDMETHODCALLTYPE get_Value(BSTR *value) override;
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL *value) override;
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override;
    HRESULT STDMETHODCALLTYPE SetValue(double value) override;
    HRESULT STDMETHODCALLTYPE get_Value(double *value) override;
    HRESULT STDMETHODCALLTYPE get_Maximum(double *value) override;
    HRESULT STDMETHODCALLTYPE get_Minimum(double *value) override;
    HRESULT STDMETHODCALLTYPE get_LargeChange(double *value) override;
    HRESULT STDMETHODCALLTYPE get_SmallChange(double *value) override;
    HRESULT STDMETHODCALLTYPE Toggle() override;
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState *value) override;
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL *value) override;
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL *value) override;
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY **value) override;
    HRESULT STDMETHODCALLTYPE Select() override;
    HRESULT STDMETHODCALLTYPE AddToSelection() override;
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override;
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL *value) override;
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple **value) override;
    HRESULT STDMETHODCALLTYPE Expand() override;
    HRESULT STDMETHODCALLTYPE Collapse() override;
    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState *value) override;
    HRESULT STDMETHODCALLTYPE ScrollIntoView() override;

  private:
    ~AccessibilityProvider() = default;
    HRESULT snapshot(SemanticNode &node) const noexcept;
    HRESULT make_provider(nk_accessibility_node_id node, IRawElementProviderFragment **value) const;
    HRESULT queue(nk_accessibility_action action, nk_accessibility_actions required,
                  const std::string &text = {}) const;
    HWND window() const;
    bool is_root() const { return node_ == NK_ACCESSIBILITY_ROOT; }

    std::atomic<ULONG> references_{1};
    std::shared_ptr<AccessibilityHost> host_;
    nk_accessibility_node_id node_;
};

HRESULT AccessibilityProvider::QueryInterface(REFIID iid, void **object) {
    if (!object)
        return E_POINTER;
    *object = nullptr;
    if (iid == __uuidof(IUnknown)) {
        *object = static_cast<IRawElementProviderSimple *>(this);
    } else if (iid == __uuidof(IRawElementProviderSimple)) {
        *object = static_cast<IRawElementProviderSimple *>(this);
    } else if (iid == __uuidof(IRawElementProviderFragment)) {
        *object = static_cast<IRawElementProviderFragment *>(this);
    } else if (iid == __uuidof(IRawElementProviderFragmentRoot)) {
        if (!is_root())
            return E_NOINTERFACE;
        *object = static_cast<IRawElementProviderFragmentRoot *>(this);
    } else if (iid == __uuidof(IInvokeProvider)) {
        SemanticNode node;
        if (FAILED(snapshot(node)) || !has_action(node, NK_ACCESSIBILITY_CAN_ACTIVATE))
            return E_NOINTERFACE;
        *object = static_cast<IInvokeProvider *>(this);
    } else if (iid == __uuidof(IValueProvider)) {
        SemanticNode node;
        if (FAILED(snapshot(node)) || node.role != NK_ACCESSIBILITY_TEXT_FIELD)
            return E_NOINTERFACE;
        *object = static_cast<IValueProvider *>(this);
    } else if (iid == __uuidof(IRangeValueProvider)) {
        SemanticNode node;
        if (FAILED(snapshot(node)) || node.role != NK_ACCESSIBILITY_SLIDER)
            return E_NOINTERFACE;
        *object = static_cast<IRangeValueProvider *>(this);
    } else if (iid == __uuidof(IToggleProvider)) {
        SemanticNode node;
        if (FAILED(snapshot(node)) ||
            (node.role != NK_ACCESSIBILITY_CHECKBOX && node.role != NK_ACCESSIBILITY_SWITCH) ||
            !has_action(node, NK_ACCESSIBILITY_CAN_TOGGLE))
            return E_NOINTERFACE;
        *object = static_cast<IToggleProvider *>(this);
    } else if (iid == __uuidof(ISelectionProvider)) {
        SemanticNode node;
        if (FAILED(snapshot(node)) || !selection_container_role(node.role))
            return E_NOINTERFACE;
        *object = static_cast<ISelectionProvider *>(this);
    } else if (iid == __uuidof(ISelectionItemProvider)) {
        SemanticNode node;
        if (FAILED(snapshot(node)))
            return E_NOINTERFACE;
        std::shared_lock lock(host_->mutex);
        if (!host_->active || !supports_selection_item(node, host_->nodes))
            return E_NOINTERFACE;
        *object = static_cast<ISelectionItemProvider *>(this);
    } else if (iid == __uuidof(IExpandCollapseProvider)) {
        SemanticNode node;
        if (FAILED(snapshot(node)) || (!has_action(node, NK_ACCESSIBILITY_CAN_EXPAND) &&
                                       !has_action(node, NK_ACCESSIBILITY_CAN_COLLAPSE)))
            return E_NOINTERFACE;
        *object = static_cast<IExpandCollapseProvider *>(this);
    } else if (iid == __uuidof(IScrollItemProvider)) {
        SemanticNode node;
        if (FAILED(snapshot(node)) || !has_action(node, NK_ACCESSIBILITY_CAN_SCROLL_INTO_VIEW))
            return E_NOINTERFACE;
        *object = static_cast<IScrollItemProvider *>(this);
    } else {
        return E_NOINTERFACE;
    }
    AddRef();
    return S_OK;
}

HRESULT AccessibilityProvider::snapshot(SemanticNode &node) const noexcept {
    {
        if (!host_)
            return UIA_E_ELEMENTNOTAVAILABLE;
        std::shared_lock lock(host_->mutex);
        if (!host_->active || !host_->window)
            return UIA_E_ELEMENTNOTAVAILABLE;
        if (is_root())
            return S_OK;
        const auto found = host_->nodes.find(node_);
        if (found == host_->nodes.end())
            return UIA_E_ELEMENTNOTAVAILABLE;
        node = found->second;
        return S_OK;
    }
}

HWND AccessibilityProvider::window() const {
    if (!host_)
        return nullptr;
    std::shared_lock lock(host_->mutex);
    return host_->active ? host_->window : nullptr;
}

HRESULT AccessibilityProvider::make_provider(nk_accessibility_node_id node,
                                             IRawElementProviderFragment **value) const {
    if (!value)
        return E_POINTER;
    *value = nullptr;
    auto *provider = new (std::nothrow) AccessibilityProvider(host_, node);
    if (!provider)
        return E_OUTOFMEMORY;
    *value = static_cast<IRawElementProviderFragment *>(provider);
    return S_OK;
}

HRESULT AccessibilityProvider::queue(nk_accessibility_action action,
                                     nk_accessibility_actions required,
                                     const std::string &text) const {
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    if (is_root() || !has_action(node, required) || !enabled(node))
        return UIA_E_NOTSUPPORTED;
    return queue_action(host_, node, action, text);
}

HRESULT AccessibilityProvider::get_ProviderOptions(ProviderOptions *value) {
    if (!value)
        return E_POINTER;
    *value = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
                                          ProviderOptions_ProviderOwnsSetFocus);
    return S_OK;
}

HRESULT AccessibilityProvider::GetPatternProvider(PATTERNID pattern, IUnknown **value) {
    if (!value)
        return E_POINTER;
    *value = nullptr;
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result) || is_root())
        return result;
    IID iid = IID_IUnknown;
    if (pattern == UIA_InvokePatternId && has_action(node, NK_ACCESSIBILITY_CAN_ACTIVATE))
        iid = __uuidof(IInvokeProvider);
    else if (pattern == UIA_ValuePatternId && node.role == NK_ACCESSIBILITY_TEXT_FIELD)
        iid = __uuidof(IValueProvider);
    else if (pattern == UIA_RangeValuePatternId && node.role == NK_ACCESSIBILITY_SLIDER)
        iid = __uuidof(IRangeValueProvider);
    else if (pattern == UIA_TogglePatternId &&
             (node.role == NK_ACCESSIBILITY_CHECKBOX || node.role == NK_ACCESSIBILITY_SWITCH) &&
             has_action(node, NK_ACCESSIBILITY_CAN_TOGGLE))
        iid = __uuidof(IToggleProvider);
    else if (pattern == UIA_SelectionPatternId && selection_container_role(node.role))
        iid = __uuidof(ISelectionProvider);
    else if (pattern == UIA_SelectionItemPatternId) {
        std::shared_lock lock(host_->mutex);
        if (!supports_selection_item(node, host_->nodes))
            return S_OK;
        iid = __uuidof(ISelectionItemProvider);
    } else if (pattern == UIA_ExpandCollapsePatternId &&
               (has_action(node, NK_ACCESSIBILITY_CAN_EXPAND) ||
                has_action(node, NK_ACCESSIBILITY_CAN_COLLAPSE)))
        iid = __uuidof(IExpandCollapseProvider);
    else if (pattern == UIA_ScrollItemPatternId &&
             has_action(node, NK_ACCESSIBILITY_CAN_SCROLL_INTO_VIEW))
        iid = __uuidof(IScrollItemProvider);
    else
        return S_OK;
    return QueryInterface(iid, reinterpret_cast<void **>(value));
}

HRESULT AccessibilityProvider::GetPropertyValue(PROPERTYID property, VARIANT *value) {
    if (!value)
        return E_POINTER;
    VariantInit(value);
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    if (is_root()) {
        switch (property) {
        case UIA_NamePropertyId:
            variant_bstr(*value, "NativeKit surface");
            break;
        case UIA_ControlTypePropertyId:
            variant_i4(*value, UIA_PaneControlTypeId);
            break;
        case UIA_AutomationIdPropertyId:
            variant_bstr(*value, "NativeKit.Surface");
            break;
        case UIA_IsEnabledPropertyId:
        case UIA_IsControlElementPropertyId:
        case UIA_IsContentElementPropertyId:
            variant_bool(*value, true);
            break;
        case UIA_IsKeyboardFocusablePropertyId:
        case UIA_HasKeyboardFocusPropertyId:
        case UIA_IsPasswordPropertyId:
            variant_bool(*value, false);
            break;
        default:
            break;
        }
        return S_OK;
    }

    std::shared_lock lock(host_->mutex);
    if (!host_->active || host_->nodes.find(node_) == host_->nodes.end())
        return UIA_E_ELEMENTNOTAVAILABLE;
    switch (property) {
    case UIA_NamePropertyId:
        variant_bstr(*value, node.label);
        break;
    case UIA_AutomationIdPropertyId: {
        wchar_t id_text[16]{};
        const int length = std::swprintf(id_text, 16, L"%u", node.id);
        if (length < 0)
            return E_FAIL;
        value->vt = VT_BSTR;
        value->bstrVal = SysAllocStringLen(id_text, static_cast<UINT>(length));
        if (!value->bstrVal)
            return E_OUTOFMEMORY;
    } break;
    case UIA_ControlTypePropertyId:
        variant_i4(*value, control_type(node.role));
        break;
    case UIA_IsEnabledPropertyId:
        variant_bool(*value, enabled(node));
        break;
    case UIA_IsControlElementPropertyId:
    case UIA_IsContentElementPropertyId:
        variant_bool(*value, true);
        break;
    case UIA_IsKeyboardFocusablePropertyId:
        variant_bool(*value, focusable(node));
        break;
    case UIA_HasKeyboardFocusPropertyId:
        variant_bool(*value, focused(*host_, node));
        break;
    case UIA_IsPasswordPropertyId:
        variant_bool(*value, (node.states & NK_ACCESSIBILITY_PASSWORD) != 0);
        break;
    case UIA_IsOffscreenPropertyId:
        variant_bool(*value, node.width <= 0 || node.height <= 0);
        break;
    case UIA_ValueValuePropertyId:
        if (node.role == NK_ACCESSIBILITY_TEXT_FIELD)
            variant_bstr(*value, node.value);
        break;
    case UIA_ValueIsReadOnlyPropertyId:
        if (node.role == NK_ACCESSIBILITY_TEXT_FIELD)
            variant_bool(*value, (node.states & NK_ACCESSIBILITY_READ_ONLY) != 0 ||
                                     !has_action(node, NK_ACCESSIBILITY_CAN_SET_VALUE));
        break;
    case UIA_RangeValueValuePropertyId:
        if (node.role == NK_ACCESSIBILITY_SLIDER)
            variant_r8(*value, node.numeric_value);
        break;
    case UIA_RangeValueMinimumPropertyId:
        if (node.role == NK_ACCESSIBILITY_SLIDER)
            variant_r8(*value, node.numeric_minimum);
        break;
    case UIA_RangeValueMaximumPropertyId:
        if (node.role == NK_ACCESSIBILITY_SLIDER)
            variant_r8(*value, node.numeric_maximum);
        break;
    case UIA_RangeValueSmallChangePropertyId:
    case UIA_RangeValueLargeChangePropertyId:
        if (node.role == NK_ACCESSIBILITY_SLIDER)
            variant_r8(*value, 1.0);
        break;
    case UIA_RangeValueIsReadOnlyPropertyId:
        if (node.role == NK_ACCESSIBILITY_SLIDER)
            variant_bool(*value, !has_action(node, NK_ACCESSIBILITY_CAN_SET_VALUE));
        break;
    case UIA_ToggleToggleStatePropertyId:
        if (node.role == NK_ACCESSIBILITY_CHECKBOX || node.role == NK_ACCESSIBILITY_SWITCH)
            variant_i4(*value,
                       (node.states & NK_ACCESSIBILITY_CHECKED) ? ToggleState_On : ToggleState_Off);
        break;
    case UIA_SelectionItemIsSelectedPropertyId:
        if (supports_selection_item(node, host_->nodes))
            variant_bool(*value, selected(node));
        break;
    case UIA_ExpandCollapseExpandCollapseStatePropertyId:
        if (has_action(node, NK_ACCESSIBILITY_CAN_EXPAND) ||
            has_action(node, NK_ACCESSIBILITY_CAN_COLLAPSE))
            variant_i4(*value, (node.states & NK_ACCESSIBILITY_EXPANDED)
                                   ? ExpandCollapseState_Expanded
                                   : ExpandCollapseState_Collapsed);
        break;
    case UIA_LevelPropertyId:
        if (node.hierarchy_level)
            variant_i4(*value, static_cast<LONG>(node.hierarchy_level));
        break;
    case UIA_PositionInSetPropertyId:
        if (node.position_in_set)
            variant_i4(*value, static_cast<LONG>(node.position_in_set));
        break;
    case UIA_SizeOfSetPropertyId:
        if (node.set_size)
            variant_i4(*value, static_cast<LONG>(node.set_size));
        break;
    case UIA_OrientationPropertyId:
        if (node.orientation == NK_ACCESSIBILITY_ORIENTATION_HORIZONTAL)
            variant_i4(*value, OrientationType_Horizontal);
        else if (node.orientation == NK_ACCESSIBILITY_ORIENTATION_VERTICAL)
            variant_i4(*value, OrientationType_Vertical);
        break;
    default:
        break;
    }
    return S_OK;
}

HRESULT AccessibilityProvider::get_HostRawElementProvider(IRawElementProviderSimple **value) {
    if (!value)
        return E_POINTER;
    *value = nullptr;
    if (!is_root())
        return S_OK;
    const HWND hwnd = window();
    const auto function = uia_api().host_provider_from_hwnd;
    return hwnd && function ? function(hwnd, value) : UIA_E_ELEMENTNOTAVAILABLE;
}

HRESULT AccessibilityProvider::Navigate(NavigateDirection direction,
                                        IRawElementProviderFragment **value) {
    if (!value)
        return E_POINTER;
    *value = nullptr;
    SemanticNode node;
    HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    {
        std::shared_lock lock(host_->mutex);
        if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild) {
            auto children = children_of(*host_, node_);
            if (children.empty())
                return S_OK;
            const auto target = direction == NavigateDirection_FirstChild ? children.front().id
                                                                          : children.back().id;
            lock.unlock();
            return make_provider(target, value);
        }
        if (direction == NavigateDirection_Parent) {
            if (is_root())
                return S_OK;
            const auto parent = host_->nodes.find(node_);
            if (parent == host_->nodes.end())
                return UIA_E_ELEMENTNOTAVAILABLE;
            const auto parent_id = parent->second.parent;
            lock.unlock();
            return make_provider(parent_id, value);
        }
        if (direction != NavigateDirection_NextSibling &&
            direction != NavigateDirection_PreviousSibling)
            return E_INVALIDARG;
        if (is_root())
            return S_OK;
        const auto found = host_->nodes.find(node_);
        if (found == host_->nodes.end())
            return UIA_E_ELEMENTNOTAVAILABLE;
        auto siblings = children_of(*host_, found->second.parent);
        auto current = std::find_if(siblings.begin(), siblings.end(),
                                    [&](const auto &item) { return item.id == node_; });
        if (current == siblings.end())
            return UIA_E_ELEMENTNOTAVAILABLE;
        if (direction == NavigateDirection_NextSibling) {
            if (++current == siblings.end())
                return S_OK;
        } else {
            if (current == siblings.begin())
                return S_OK;
            --current;
        }
        const auto target = current->id;
        lock.unlock();
        return make_provider(target, value);
    }
}

HRESULT AccessibilityProvider::GetRuntimeId(SAFEARRAY **value) {
    if (!value)
        return E_POINTER;
    *value = nullptr;
    if (is_root())
        return S_OK;
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    LONG runtime_id[] = {UiaAppendRuntimeId, static_cast<LONG>(node.id)};
    SAFEARRAY *array = SafeArrayCreateVector(VT_I4, 0, 2);
    if (!array)
        return E_OUTOFMEMORY;
    for (LONG index = 0; index < 2; ++index) {
        if (FAILED(SafeArrayPutElement(array, &index, &runtime_id[index]))) {
            SafeArrayDestroy(array);
            return E_OUTOFMEMORY;
        }
    }
    *value = array;
    return S_OK;
}

HRESULT AccessibilityProvider::get_BoundingRectangle(UiaRect *value) {
    if (!value)
        return E_POINTER;
    const HWND hwnd = window();
    if (!hwnd)
        return UIA_E_ELEMENTNOTAVAILABLE;
    POINT origin{0, 0};
    if (!ClientToScreen(hwnd, &origin))
        return HRESULT_FROM_WIN32(GetLastError());
    UINT dpi = 96;
    if (const auto user32 = GetModuleHandleW(L"user32.dll")) {
        using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
        const auto get_dpi = resolve_function<GetDpiForWindowFn>(user32, "GetDpiForWindow");
        if (get_dpi)
            dpi = std::max(1u, get_dpi(hwnd));
    }
    const double scale = static_cast<double>(dpi) / 96.0;
    if (is_root()) {
        RECT rect{};
        if (!GetClientRect(hwnd, &rect))
            return HRESULT_FROM_WIN32(GetLastError());
        value->left = origin.x;
        value->top = origin.y;
        value->width = rect.right - rect.left;
        value->height = rect.bottom - rect.top;
        return S_OK;
    }
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    value->left = origin.x + node.x * scale;
    value->top = origin.y + node.y * scale;
    value->width = node.width * scale;
    value->height = node.height * scale;
    return S_OK;
}

HRESULT AccessibilityProvider::GetEmbeddedFragmentRoots(SAFEARRAY **value) {
    if (!value)
        return E_POINTER;
    *value = nullptr;
    SemanticNode node;
    return snapshot(node);
}

HRESULT AccessibilityProvider::SetFocus() {
    return queue(NK_ACCESSIBILITY_ACTION_FOCUS, NK_ACCESSIBILITY_CAN_FOCUS);
}

HRESULT AccessibilityProvider::get_FragmentRoot(IRawElementProviderFragmentRoot **value) {
    if (!value)
        return E_POINTER;
    *value = nullptr;
    auto *root = new (std::nothrow) AccessibilityProvider(host_, NK_ACCESSIBILITY_ROOT);
    if (!root)
        return E_OUTOFMEMORY;
    *value = static_cast<IRawElementProviderFragmentRoot *>(root);
    return S_OK;
}

HRESULT AccessibilityProvider::ElementProviderFromPoint(double x, double y,
                                                        IRawElementProviderFragment **value) {
    if (!value)
        return E_POINTER;
    *value = nullptr;
    if (!is_root())
        return E_FAIL;
    UiaRect bounds{};
    const HRESULT result = get_BoundingRectangle(&bounds);
    if (FAILED(result))
        return result;
    if (x < bounds.left || y < bounds.top || x >= bounds.left + bounds.width ||
        y >= bounds.top + bounds.height)
        return S_OK;
    POINT screen{static_cast<LONG>(x), static_cast<LONG>(y)};
    const HWND hwnd = window();
    if (!hwnd || !ScreenToClient(hwnd, &screen))
        return UIA_E_ELEMENTNOTAVAILABLE;
    UINT dpi = 96;
    if (const auto user32 = GetModuleHandleW(L"user32.dll")) {
        using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
        const auto get_dpi = resolve_function<GetDpiForWindowFn>(user32, "GetDpiForWindow");
        if (get_dpi)
            dpi = std::max(1u, get_dpi(hwnd));
    }
    const double scale = static_cast<double>(dpi) / 96.0;
    const double local_x = screen.x / scale;
    const double local_y = screen.y / scale;
    {
        std::shared_lock lock(host_->mutex);
        std::function<std::optional<nk_accessibility_node_id>(nk_accessibility_node_id)> hit;
        hit = [&](nk_accessibility_node_id parent) -> std::optional<nk_accessibility_node_id> {
            auto children = children_of(*host_, parent);
            for (auto child = children.rbegin(); child != children.rend(); ++child) {
                if (local_x < child->x || local_y < child->y ||
                    local_x >= child->x + child->width || local_y >= child->y + child->height)
                    continue;
                if (const auto nested = hit(child->id))
                    return nested;
                return child->id;
            }
            return std::nullopt;
        };
        const auto target = hit(NK_ACCESSIBILITY_ROOT);
        lock.unlock();
        return make_provider(target.value_or(NK_ACCESSIBILITY_ROOT), value);
    }
}

HRESULT AccessibilityProvider::GetFocus(IRawElementProviderFragment **value) {
    if (!value)
        return E_POINTER;
    *value = nullptr;
    if (!is_root())
        return E_FAIL;
    SemanticNode focused_node;
    nk_accessibility_node_id focus = NK_ACCESSIBILITY_ROOT;
    {
        std::shared_lock lock(host_->mutex);
        if (!host_->active)
            return UIA_E_ELEMENTNOTAVAILABLE;
        focus = host_->focus;
        if (!focus) {
            for (const auto &[id, node] : host_->nodes) {
                if (node.states & NK_ACCESSIBILITY_FOCUSED) {
                    focus = id;
                    break;
                }
            }
        }
        if (focus && host_->nodes.find(focus) == host_->nodes.end())
            focus = NK_ACCESSIBILITY_ROOT;
    }
    return focus ? make_provider(focus, value) : S_OK;
}

HRESULT AccessibilityProvider::Invoke() {
    return queue(NK_ACCESSIBILITY_ACTION_ACTIVATE, NK_ACCESSIBILITY_CAN_ACTIVATE);
}

HRESULT AccessibilityProvider::get_Value(BSTR *value) {
    if (!value)
        return E_POINTER;
    *value = nullptr;
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    return make_bstr(node.value, value) ? S_OK : E_OUTOFMEMORY;
}

HRESULT AccessibilityProvider::get_IsReadOnly(BOOL *value) {
    if (!value)
        return E_POINTER;
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    *value = (node.states & NK_ACCESSIBILITY_READ_ONLY) ||
                     !has_action(node, NK_ACCESSIBILITY_CAN_SET_VALUE)
                 ? TRUE
                 : FALSE;
    return S_OK;
}

HRESULT AccessibilityProvider::SetValue(LPCWSTR value) {
    if (!value)
        return E_INVALIDARG;
    std::string text;
    if (!wide_to_utf8(value, text))
        return E_INVALIDARG;
    return queue(NK_ACCESSIBILITY_ACTION_SET_VALUE, NK_ACCESSIBILITY_CAN_SET_VALUE, text);
}

HRESULT AccessibilityProvider::SetValue(double value) {
    if (!std::isfinite(value))
        return E_INVALIDARG;
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    if (node.role != NK_ACCESSIBILITY_SLIDER || value < node.numeric_minimum ||
        value > node.numeric_maximum)
        return E_INVALIDARG;
    char buffer[64]{};
    const int length = std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    if (length < 0 || static_cast<std::size_t>(length) >= sizeof(buffer))
        return E_INVALIDARG;
    return queue(NK_ACCESSIBILITY_ACTION_SET_VALUE, NK_ACCESSIBILITY_CAN_SET_VALUE,
                 std::string(buffer, static_cast<std::size_t>(length)));
}

HRESULT AccessibilityProvider::get_Value(double *value) {
    if (!value)
        return E_POINTER;
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    if (node.role != NK_ACCESSIBILITY_SLIDER)
        return UIA_E_NOTSUPPORTED;
    *value = node.numeric_value;
    return S_OK;
}

HRESULT AccessibilityProvider::get_Maximum(double *value) {
    if (!value)
        return E_POINTER;
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    if (node.role != NK_ACCESSIBILITY_SLIDER)
        return UIA_E_NOTSUPPORTED;
    *value = node.numeric_maximum;
    return S_OK;
}

HRESULT AccessibilityProvider::get_Minimum(double *value) {
    if (!value)
        return E_POINTER;
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    if (node.role != NK_ACCESSIBILITY_SLIDER)
        return UIA_E_NOTSUPPORTED;
    *value = node.numeric_minimum;
    return S_OK;
}

HRESULT AccessibilityProvider::get_LargeChange(double *value) {
    if (!value)
        return E_POINTER;
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    if (node.role != NK_ACCESSIBILITY_SLIDER)
        return UIA_E_NOTSUPPORTED;
    *value = 1.0;
    return S_OK;
}

HRESULT AccessibilityProvider::get_SmallChange(double *value) {
    return get_LargeChange(value);
}

HRESULT AccessibilityProvider::Toggle() {
    return queue(NK_ACCESSIBILITY_ACTION_TOGGLE, NK_ACCESSIBILITY_CAN_TOGGLE);
}

HRESULT AccessibilityProvider::get_ToggleState(ToggleState *value) {
    if (!value)
        return E_POINTER;
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    if (node.role != NK_ACCESSIBILITY_CHECKBOX && node.role != NK_ACCESSIBILITY_SWITCH)
        return UIA_E_NOTSUPPORTED;
    *value = (node.states & NK_ACCESSIBILITY_CHECKED) ? ToggleState_On : ToggleState_Off;
    return S_OK;
}

HRESULT AccessibilityProvider::get_CanSelectMultiple(BOOL *value) {
    if (!value)
        return E_POINTER;
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    if (!selection_container_role(node.role))
        return UIA_E_NOTSUPPORTED;
    *value = FALSE;
    return S_OK;
}

HRESULT AccessibilityProvider::get_IsSelectionRequired(BOOL *value) {
    if (!value)
        return E_POINTER;
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    if (!selection_container_role(node.role))
        return UIA_E_NOTSUPPORTED;
    *value = FALSE;
    return S_OK;
}

HRESULT AccessibilityProvider::GetSelection(SAFEARRAY **value) {
    if (!value)
        return E_POINTER;
    *value = nullptr;
    SemanticNode node;
    HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    if (!selection_container_role(node.role))
        return UIA_E_NOTSUPPORTED;
    {
        std::shared_lock lock(host_->mutex);
        if (!host_->active || host_->nodes.find(node_) == host_->nodes.end())
            return UIA_E_ELEMENTNOTAVAILABLE;
        std::vector<nk_accessibility_node_id> ids;
        for (const auto &child : children_of(*host_, node_))
            if (selected(child) && supports_selection_item(child, host_->nodes))
                ids.push_back(child.id);
        SAFEARRAY *array = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(ids.size()));
        if (!array)
            return E_OUTOFMEMORY;
        for (LONG index = 0; index < static_cast<LONG>(ids.size()); ++index) {
            auto *provider = new (std::nothrow) AccessibilityProvider(host_, ids[index]);
            if (!provider) {
                SafeArrayDestroy(array);
                return E_OUTOFMEMORY;
            }
            IUnknown *unknown = static_cast<ISelectionItemProvider *>(provider);
            const HRESULT put_result = SafeArrayPutElement(array, &index, unknown);
            unknown->Release();
            if (FAILED(put_result)) {
                SafeArrayDestroy(array);
                return put_result;
            }
        }
        *value = array;
        return S_OK;
    }
}

HRESULT AccessibilityProvider::Select() {
    return queue(NK_ACCESSIBILITY_ACTION_SELECT, NK_ACCESSIBILITY_CAN_SELECT);
}

HRESULT AccessibilityProvider::AddToSelection() {
    return Select();
}

HRESULT AccessibilityProvider::RemoveFromSelection() {
    return queue(NK_ACCESSIBILITY_ACTION_DESELECT, NK_ACCESSIBILITY_CAN_DESELECT);
}

HRESULT AccessibilityProvider::get_IsSelected(BOOL *value) {
    if (!value)
        return E_POINTER;
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    std::shared_lock lock(host_->mutex);
    if (!host_->active || host_->nodes.find(node_) == host_->nodes.end())
        return UIA_E_ELEMENTNOTAVAILABLE;
    if (!supports_selection_item(node, host_->nodes))
        return UIA_E_NOTSUPPORTED;
    *value = selected(node) ? TRUE : FALSE;
    return S_OK;
}

HRESULT AccessibilityProvider::get_SelectionContainer(IRawElementProviderSimple **value) {
    if (!value)
        return E_POINTER;
    *value = nullptr;
    SemanticNode node;
    HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    nk_accessibility_node_id parent = NK_ACCESSIBILITY_ROOT;
    {
        std::shared_lock lock(host_->mutex);
        const auto found = host_->nodes.find(node_);
        if (found == host_->nodes.end())
            return UIA_E_ELEMENTNOTAVAILABLE;
        if (!supports_selection_item(node, host_->nodes))
            return UIA_E_NOTSUPPORTED;
        parent = found->second.parent;
    }
    auto *provider = new (std::nothrow) AccessibilityProvider(host_, parent);
    if (!provider)
        return E_OUTOFMEMORY;
    *value = static_cast<IRawElementProviderSimple *>(provider);
    return S_OK;
}

HRESULT AccessibilityProvider::Expand() {
    return queue(NK_ACCESSIBILITY_ACTION_EXPAND, NK_ACCESSIBILITY_CAN_EXPAND);
}

HRESULT AccessibilityProvider::Collapse() {
    return queue(NK_ACCESSIBILITY_ACTION_COLLAPSE, NK_ACCESSIBILITY_CAN_COLLAPSE);
}

HRESULT AccessibilityProvider::get_ExpandCollapseState(ExpandCollapseState *value) {
    if (!value)
        return E_POINTER;
    SemanticNode node;
    const HRESULT result = snapshot(node);
    if (FAILED(result))
        return result;
    if (!has_action(node, NK_ACCESSIBILITY_CAN_EXPAND) &&
        !has_action(node, NK_ACCESSIBILITY_CAN_COLLAPSE))
        return UIA_E_NOTSUPPORTED;
    *value = (node.states & NK_ACCESSIBILITY_EXPANDED) ? ExpandCollapseState_Expanded
                                                       : ExpandCollapseState_Collapsed;
    return S_OK;
}

HRESULT AccessibilityProvider::ScrollIntoView() {
    return queue(NK_ACCESSIBILITY_ACTION_SCROLL_INTO_VIEW, NK_ACCESSIBILITY_CAN_SCROLL_INTO_VIEW);
}

void raise_property(AccessibilityProvider *provider, PROPERTYID property, const VARIANT &old_value,
                    const VARIANT &new_value) {
    if (const auto function = uia_api().raise_property_changed)
        function(provider, property, old_value, new_value);
}

void raise_text_property(const std::shared_ptr<AccessibilityHost> &host,
                         nk_accessibility_node_id id, PROPERTYID property,
                         const std::string &old_text, const std::string &new_text) {
    auto *provider = new (std::nothrow) AccessibilityProvider(host, id);
    if (!provider)
        return;
    VARIANT old_value;
    VARIANT new_value;
    VariantInit(&old_value);
    VariantInit(&new_value);
    variant_bstr(old_value, old_text);
    variant_bstr(new_value, new_text);
    if (old_value.vt == VT_BSTR && new_value.vt == VT_BSTR)
        raise_property(provider, property, old_value, new_value);
    VariantClear(&old_value);
    VariantClear(&new_value);
    provider->Release();
}

void raise_bool_property(const std::shared_ptr<AccessibilityHost> &host,
                         nk_accessibility_node_id id, PROPERTYID property, bool old_state,
                         bool new_state) {
    auto *provider = new (std::nothrow) AccessibilityProvider(host, id);
    if (!provider)
        return;
    VARIANT old_value;
    VARIANT new_value;
    VariantInit(&old_value);
    VariantInit(&new_value);
    variant_bool(old_value, old_state);
    variant_bool(new_value, new_state);
    raise_property(provider, property, old_value, new_value);
    provider->Release();
}

void raise_i4_property(const std::shared_ptr<AccessibilityHost> &host, nk_accessibility_node_id id,
                       PROPERTYID property, LONG old_state, LONG new_state) {
    auto *provider = new (std::nothrow) AccessibilityProvider(host, id);
    if (!provider)
        return;
    VARIANT old_value;
    VARIANT new_value;
    VariantInit(&old_value);
    VariantInit(&new_value);
    variant_i4(old_value, old_state);
    variant_i4(new_value, new_state);
    raise_property(provider, property, old_value, new_value);
    provider->Release();
}

void raise_r8_property(const std::shared_ptr<AccessibilityHost> &host, nk_accessibility_node_id id,
                       PROPERTYID property, double old_state, double new_state) {
    auto *provider = new (std::nothrow) AccessibilityProvider(host, id);
    if (!provider)
        return;
    VARIANT old_value;
    VARIANT new_value;
    VariantInit(&old_value);
    VariantInit(&new_value);
    variant_r8(old_value, old_state);
    variant_r8(new_value, new_state);
    raise_property(provider, property, old_value, new_value);
    provider->Release();
}

void notify_update(const std::shared_ptr<AccessibilityHost> &host,
                   const std::unordered_map<nk_accessibility_node_id, SemanticNode> &before,
                   const std::unordered_map<nk_accessibility_node_id, SemanticNode> &after,
                   nk_accessibility_node_id old_focus,
                   nk_accessibility_node_id new_focus) noexcept {
    if (!uia_clients_are_listening())
            return;
        bool structure_changed = before.size() != after.size();
        for (const auto &[id, old_node] : before) {
            const auto found = after.find(id);
            if (found == after.end()) {
                structure_changed = true;
                continue;
            }
            const auto &next = found->second;
            if (old_node.parent != next.parent || old_node.child_index != next.child_index)
                structure_changed = true;
            if (old_node.label != next.label)
                raise_text_property(host, id, UIA_NamePropertyId, old_node.label, next.label);
            if (old_node.value != next.value && next.role == NK_ACCESSIBILITY_TEXT_FIELD)
                raise_text_property(host, id, UIA_ValueValuePropertyId, old_node.value, next.value);
            if (next.role == NK_ACCESSIBILITY_TEXT_FIELD) {
                const bool old_read_only = (old_node.states & NK_ACCESSIBILITY_READ_ONLY) != 0 ||
                                           !has_action(old_node, NK_ACCESSIBILITY_CAN_SET_VALUE);
                const bool new_read_only = (next.states & NK_ACCESSIBILITY_READ_ONLY) != 0 ||
                                           !has_action(next, NK_ACCESSIBILITY_CAN_SET_VALUE);
                if (old_read_only != new_read_only)
                    raise_bool_property(host, id, UIA_ValueIsReadOnlyPropertyId, old_read_only,
                                        new_read_only);
            }
            if (old_node.role != next.role)
                raise_i4_property(host, id, UIA_ControlTypePropertyId, control_type(old_node.role),
                                  control_type(next.role));
            if (enabled(old_node) != enabled(next))
                raise_bool_property(host, id, UIA_IsEnabledPropertyId, enabled(old_node),
                                    enabled(next));
            if (focusable(old_node) != focusable(next))
                raise_bool_property(host, id, UIA_IsKeyboardFocusablePropertyId,
                                    focusable(old_node), focusable(next));
            if (next.role == NK_ACCESSIBILITY_SLIDER &&
                old_node.numeric_value != next.numeric_value)
                raise_r8_property(host, id, UIA_RangeValueValuePropertyId, old_node.numeric_value,
                                  next.numeric_value);
            if (next.role == NK_ACCESSIBILITY_SLIDER) {
                if (old_node.numeric_minimum != next.numeric_minimum)
                    raise_r8_property(host, id, UIA_RangeValueMinimumPropertyId,
                                      old_node.numeric_minimum, next.numeric_minimum);
                if (old_node.numeric_maximum != next.numeric_maximum)
                    raise_r8_property(host, id, UIA_RangeValueMaximumPropertyId,
                                      old_node.numeric_maximum, next.numeric_maximum);
                const bool old_read_only = !has_action(old_node, NK_ACCESSIBILITY_CAN_SET_VALUE);
                const bool new_read_only = !has_action(next, NK_ACCESSIBILITY_CAN_SET_VALUE);
                if (old_read_only != new_read_only)
                    raise_bool_property(host, id, UIA_RangeValueIsReadOnlyPropertyId, old_read_only,
                                        new_read_only);
            }
            if ((next.role == NK_ACCESSIBILITY_CHECKBOX || next.role == NK_ACCESSIBILITY_SWITCH) &&
                ((old_node.states ^ next.states) & NK_ACCESSIBILITY_CHECKED))
                raise_i4_property(
                    host, id, UIA_ToggleToggleStatePropertyId,
                    (old_node.states & NK_ACCESSIBILITY_CHECKED) ? ToggleState_On : ToggleState_Off,
                    (next.states & NK_ACCESSIBILITY_CHECKED) ? ToggleState_On : ToggleState_Off);
            if (supports_selection_item(next, after) && selected(old_node) != selected(next))
                raise_bool_property(host, id, UIA_SelectionItemIsSelectedPropertyId,
                                    selected(old_node), selected(next));
            if (((old_node.states ^ next.states) & NK_ACCESSIBILITY_EXPANDED) &&
                (has_action(next, NK_ACCESSIBILITY_CAN_EXPAND) ||
                 has_action(next, NK_ACCESSIBILITY_CAN_COLLAPSE)))
                raise_i4_property(
                    host, id, UIA_ExpandCollapseExpandCollapseStatePropertyId,
                    (old_node.states & NK_ACCESSIBILITY_EXPANDED) ? ExpandCollapseState_Expanded
                                                                  : ExpandCollapseState_Collapsed,
                    (next.states & NK_ACCESSIBILITY_EXPANDED) ? ExpandCollapseState_Expanded
                                                              : ExpandCollapseState_Collapsed);
        }
        for (const auto &[id, node] : after) {
            (void)node;
            if (before.find(id) == before.end())
                structure_changed = true;
        }
        if (old_focus != new_focus) {
            if (old_focus && after.find(old_focus) != after.end())
                raise_bool_property(host, old_focus, UIA_HasKeyboardFocusPropertyId, true, false);
            if (new_focus && after.find(new_focus) != after.end())
                raise_bool_property(host, new_focus, UIA_HasKeyboardFocusPropertyId, false, true);
            auto *provider = new (std::nothrow)
                AccessibilityProvider(host, new_focus ? new_focus : NK_ACCESSIBILITY_ROOT);
            if (provider) {
                if (const auto function = uia_api().raise_automation_event)
                    function(provider, UIA_AutomationFocusChangedEventId);
                provider->Release();
            }
        }
        if (structure_changed) {
            auto *provider = new (std::nothrow) AccessibilityProvider(host, NK_ACCESSIBILITY_ROOT);
            if (provider) {
                if (const auto function = uia_api().raise_structure_changed)
                    function(provider, StructureChangeType_ChildrenInvalidated, nullptr, 0);
                provider->Release();
            }
        }
}

nk_result apply_update(nk_handle handle, const nk_accessibility_update *update) {
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
        return thread;
    const auto host = host_for_surface(handle);
    if (!host)
        return NK_ERROR_INVALID_HANDLE;
    if (!update || update->struct_size < sizeof(*update) ||
        (update->flags & ~NK_ACCESSIBILITY_UPDATE_FOCUS) ||
        (update->node_count && !update->nodes) ||
        (update->removed_node_count && !update->removed_nodes) ||
        update->node_count > static_cast<uint32_t>(INT_MAX / 16) ||
        update->removed_node_count > static_cast<uint32_t>(INT_MAX) ||
        update->focus > static_cast<uint32_t>(INT_MAX))
        return invalid_argument("invalid Windows accessibility update");

    {
        std::unordered_map<nk_accessibility_node_id, SemanticNode> before;
        nk_accessibility_node_id focus_before = NK_ACCESSIBILITY_ROOT;
        {
            std::shared_lock lock(host->mutex);
            if (!host->active)
                return NK_ERROR_INVALID_HANDLE;
            before = host->nodes;
            focus_before = host->focus;
        }
        auto after = before;
        for (uint32_t index = 0; index < update->removed_node_count; ++index) {
            const auto id = update->removed_nodes[index];
            if (!id || before.find(id) == before.end())
                return invalid_argument("Windows accessibility update removes an unknown node");
            if (after.find(id) != after.end())
                remove_descendants(after, id);
        }
        for (uint32_t index = 0; index < update->node_count; ++index) {
            SemanticNode node;
            if (!valid_node(update->nodes[index], node))
                return invalid_argument("invalid node in Windows accessibility update");
            const auto old = after.find(node.id);
            if (old != after.end() && old->second.value == node.value &&
                old->second.document_length == node.document_length)
                node.text_ranges = old->second.text_ranges;
            after[node.id] = std::move(node);
            const auto inserted = after.find(update->nodes[index].id);
            if (inserted == after.end() || !node_has_parent(after, inserted->second))
                return invalid_argument("Windows accessibility update has an invalid parent chain");
        }

        nk_accessibility_node_id focus_after = focus_before;
        if (update->flags & NK_ACCESSIBILITY_UPDATE_FOCUS) {
            focus_after = update->focus;
            if (focus_after && after.find(focus_after) == after.end())
                return invalid_argument("Windows accessibility update focuses an unknown node");
        } else if (focus_after && after.find(focus_after) == after.end()) {
            focus_after = NK_ACCESSIBILITY_ROOT;
        }
        for (auto &[id, node] : after) {
            if (id == focus_after)
                node.states |= NK_ACCESSIBILITY_FOCUSED;
            else
                node.states &= ~NK_ACCESSIBILITY_FOCUSED;
        }
        {
            std::unique_lock lock(host->mutex);
            if (!host->active)
                return NK_ERROR_INVALID_HANDLE;
            host->nodes = std::move(after);
            host->focus = focus_after;
        }
        // Use the committed map for notification payloads without holding the tree lock.
        std::unordered_map<nk_accessibility_node_id, SemanticNode> committed;
        {
            std::shared_lock lock(host->mutex);
            committed = host->nodes;
        }
        notify_update(host, before, committed, focus_before, focus_after);
        return NK_OK;
    }
}

} // namespace

nk_result accessibility_attach(HWND window, nk_handle surface) noexcept {
    if (!window || surface == NK_INVALID_HANDLE)
        return NK_ERROR_INVALID_ARGUMENT;
    auto host = std::make_shared<AccessibilityHost>();
        host->window = window;
        host->surface = surface;
        std::lock_guard lock(hosts_mutex);
        if (hosts_by_surface.find(surface) != hosts_by_surface.end() ||
            surfaces_by_window.find(window) != surfaces_by_window.end())
            return NK_ERROR_INVALID_REQUEST;
        hosts_by_surface.emplace(surface, host);
    surfaces_by_window.emplace(window, surface);
    return NK_OK;
}

void accessibility_detach(HWND window) noexcept {
    std::shared_ptr<AccessibilityHost> host;
    {
        std::lock_guard lock(hosts_mutex);
        const auto found = surfaces_by_window.find(window);
        if (found == surfaces_by_window.end())
            return;
        const auto surface = found->second;
        surfaces_by_window.erase(found);
        const auto item = hosts_by_surface.find(surface);
        if (item != hosts_by_surface.end()) {
            host = item->second;
            hosts_by_surface.erase(item);
        }
    }
    if (host) {
        std::unique_lock lock(host->mutex);
        host->active = false;
        host->window = nullptr;
        host->nodes.clear();
        host->focus = NK_ACCESSIBILITY_ROOT;
    }
}

bool accessibility_handle_getobject(HWND window, WPARAM wparam, LPARAM lparam,
                                    LRESULT *out_result) noexcept {
    if (!out_result || lparam != UiaRootObjectId)
        return false;
    const auto host = host_for_window(window);
    if (!host)
        return false;
    auto *provider = new (std::nothrow) AccessibilityProvider(host, NK_ACCESSIBILITY_ROOT);
    if (!provider) {
        *out_result = 0;
        return true;
    }
    if (const auto function = uia_api().return_raw_element_provider)
        *out_result = function(window, wparam, lparam, provider);
    else
        *out_result = 0;
    provider->Release();
    return true;
}

nk_result accessibility_update_tree(nk_handle surface,
                                    const nk_accessibility_update *update) noexcept {
    return apply_update(surface, update);
}

nk_result accessibility_clear_tree(nk_handle surface) noexcept {
        if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
            return thread;
        const auto host = host_for_surface(surface);
        if (!host)
            return NK_ERROR_INVALID_HANDLE;
        std::vector<nk_accessibility_node_id> ids;
        {
            std::shared_lock lock(host->mutex);
            ids.reserve(host->nodes.size());
            for (const auto &[id, node] : host->nodes) {
                if (node.parent == NK_ACCESSIBILITY_ROOT)
                    ids.push_back(id);
            }
        }
        nk_accessibility_update update{};
        update.struct_size = sizeof(update);
        update.flags = NK_ACCESSIBILITY_UPDATE_FOCUS;
        update.removed_nodes = ids.data();
        update.removed_node_count = static_cast<uint32_t>(ids.size());
        update.focus = NK_ACCESSIBILITY_ROOT;
    return apply_update(surface, &update);
}

nk_result accessibility_set_text_ranges(nk_handle surface, nk_accessibility_node_id node,
                                        const nk_accessibility_text_range *ranges,
                                        uint32_t range_count) noexcept {
    if (const auto thread = nk::core::require_ui_thread(); thread != NK_OK)
            return thread;
        const auto host = host_for_surface(surface);
        if (!host)
            return NK_ERROR_INVALID_HANDLE;
        if (!node || (range_count && !ranges) || range_count > static_cast<uint32_t>(INT_MAX / 4))
            return invalid_argument("invalid Windows accessibility text ranges");
        std::unique_lock lock(host->mutex);
        const auto found = host->nodes.find(node);
        if (!host->active || found == host->nodes.end())
            return NK_ERROR_INVALID_HANDLE;
        std::vector<TextRange> normalized;
        normalized.reserve(range_count);
        nk_accessibility_text_position previous = 0;
        for (uint32_t index = 0; index < range_count; ++index) {
            const auto &range = ranges[index];
            if (range.start >= range.end || range.start < previous ||
                range.end > found->second.document_length || !std::isfinite(range.x) ||
                !std::isfinite(range.y) || !std::isfinite(range.width) ||
                !std::isfinite(range.height) || range.width < 0 || range.height < 0)
                return invalid_argument(
                    "Windows accessibility text ranges are invalid or unordered");
            normalized.push_back(
                {range.start, range.end, range.x, range.y, range.width, range.height});
            previous = range.end;
        }
        found->second.text_ranges = std::move(normalized);
    return NK_OK;
}

} // namespace nk::windows

extern "C" {

nk_result NK_CALL nk_surface_accessibility_set_node(nk_handle handle,
                                                    const nk_accessibility_node *node) {
    if (!node) {
        nk::core::set_error("accessibility node is null");
        return NK_ERROR_INVALID_ARGUMENT;
    }
    nk_accessibility_update update{};
    update.struct_size = sizeof(update);
    update.nodes = node;
    update.node_count = 1;
    return nk::core::result_boundary("set Windows surface accessibility node", [&] {
        return nk::windows::accessibility_update_tree(handle, &update);
    });
}

nk_result NK_CALL nk_surface_accessibility_remove_node(nk_handle handle,
                                                       nk_accessibility_node_id node) {
    nk_accessibility_update update{};
    update.struct_size = sizeof(update);
    update.removed_nodes = &node;
    update.removed_node_count = 1;
    return nk::core::result_boundary("remove Windows surface accessibility node", [&] {
        return nk::windows::accessibility_update_tree(handle, &update);
    });
}

nk_result NK_CALL nk_surface_accessibility_clear(nk_handle handle) {
    return nk::core::result_boundary("clear Windows surface accessibility tree",
                                     [&] { return nk::windows::accessibility_clear_tree(handle); });
}

nk_result NK_CALL nk_surface_accessibility_set_focus(nk_handle handle,
                                                     nk_accessibility_node_id node) {
    nk_accessibility_update update{};
    update.struct_size = sizeof(update);
    update.flags = NK_ACCESSIBILITY_UPDATE_FOCUS;
    update.focus = node;
    return nk::core::result_boundary("set Windows surface accessibility focus", [&] {
        return nk::windows::accessibility_update_tree(handle, &update);
    });
}

nk_result NK_CALL nk_surface_accessibility_update(nk_handle handle,
                                                  const nk_accessibility_update *update) {
    return nk::core::result_boundary("update Windows surface accessibility tree", [&] {
        return nk::windows::accessibility_update_tree(handle, update);
    });
}

nk_result NK_CALL nk_surface_accessibility_set_text_ranges(
    nk_handle handle, nk_accessibility_node_id node, const nk_accessibility_text_range *ranges,
    uint32_t range_count) {
    return nk::core::result_boundary("set Windows accessibility text ranges", [&]() -> nk_result {
        return nk::windows::accessibility_set_text_ranges(handle, node, ranges, range_count);
    });
}

} // extern "C"
