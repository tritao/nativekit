#include "nativekit_accessibility.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#include <windows.h>
#include <uiautomation.h>
#include <uiautomationclient.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <iterator>

namespace {

constexpr CLSID clsid_ui_automation{
    0xff48dba4, 0x60ef, 0x4201, {0xaa, 0x87, 0x54, 0x10, 0x3e, 0xef, 0x59, 0x4e}};

class AutomationSignals final : public IUIAutomationPropertyChangedEventHandler,
                                public IUIAutomationFocusChangedEventHandler,
                                public IUIAutomationStructureChangedEventHandler {
  public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void **object) override {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IUIAutomationPropertyChangedEventHandler))
            *object = static_cast<IUIAutomationPropertyChangedEventHandler *>(this);
        else if (iid == __uuidof(IUIAutomationFocusChangedEventHandler))
            *object = static_cast<IUIAutomationFocusChangedEventHandler *>(this);
        else if (iid == __uuidof(IUIAutomationStructureChangedEventHandler))
            *object = static_cast<IUIAutomationStructureChangedEventHandler *>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (!remaining)
            delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement *,
                                                         PROPERTYID property, VARIANT) override {
        if (property == UIA_NamePropertyId)
            name_changed.store(true, std::memory_order_release);
        if (property == UIA_HasKeyboardFocusPropertyId)
            focus_property_changed.store(true, std::memory_order_release);
        if (property == UIA_ValueValuePropertyId)
            value_changed.store(true, std::memory_order_release);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement *) override {
        focus_changed.store(true, std::memory_order_release);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement *,
                                                          StructureChangeType,
                                                          SAFEARRAY *) override {
        structure_changed.store(true, std::memory_order_release);
        return S_OK;
    }

    std::atomic<bool> name_changed{false};
    std::atomic<bool> focus_property_changed{false};
    std::atomic<bool> value_changed{false};
    std::atomic<bool> focus_changed{false};
    std::atomic<bool> structure_changed{false};

  private:
    ~AutomationSignals() = default;
    std::atomic<ULONG> references_{1};
};

struct SurfaceSearch {
    HWND window = nullptr;
};

BOOL CALLBACK find_surface(HWND window, LPARAM data) {
    auto *search = reinterpret_cast<SurfaceSearch *>(data);
    wchar_t class_name[64]{};
    if (GetClassNameW(window, class_name, static_cast<int>(std::size(class_name))) > 0 &&
        std::wcscmp(class_name, L"NativeKitD3D11Surface") == 0) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

HWND surface_window(nk_window window) {
    nk_native_window native{};
    native.struct_size = sizeof(native);
    assert(nk_window_get_native(window, &native) == NK_OK);
    assert(native.kind == NK_NATIVE_WINDOW_WIN32);
    SurfaceSearch search;
    EnumChildWindows(reinterpret_cast<HWND>(native.window), find_surface,
                     reinterpret_cast<LPARAM>(&search));
    assert(search.window != nullptr);
    return search.window;
}

UINT window_dpi(HWND window) {
    const auto user32 = GetModuleHandleW(L"user32.dll");
    using GetDpiForWindowFn = UINT(WINAPI *)(HWND);
    GetDpiForWindowFn get_dpi = nullptr;
    if (user32) {
        const auto address = GetProcAddress(user32, "GetDpiForWindow");
        static_assert(sizeof(get_dpi) == sizeof(address));
        std::memcpy(&get_dpi, &address, sizeof(get_dpi));
    }
    return get_dpi ? std::max(1u, get_dpi(window)) : 96u;
}

void drain_events() {
    for (;;) {
        nk_event event{};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        const auto kind = event.kind;
        nk_event_release(&event);
        if (kind == NK_EVENT_NONE)
            return;
    }
}

nk_accessibility_action_event wait_for_action(nk_accessibility_node_id node,
                                              nk_accessibility_action action, nk_event *out_event) {
    for (int attempt = 0; attempt < 2000; ++attempt) {
        nk_event event{};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_ACCESSIBILITY_ACTION && event.data &&
            event.data_size >= sizeof(nk_accessibility_action_event)) {
            const auto payload = *static_cast<const nk_accessibility_action_event *>(event.data);
            if (payload.node_id == node && payload.action == action) {
                *out_event = event;
                return payload;
            }
        }
        nk_event_release(&event);
        Sleep(1);
    }
    assert(!"timed out waiting for NativeKit accessibility action");
    return {};
}

IUIAutomationElement *find_named(IUIAutomation *automation, IUIAutomationElement *root,
                                 const wchar_t *name) {
    VARIANT value{};
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(name);
    assert(value.bstrVal);
    IUIAutomationCondition *condition = nullptr;
    assert(SUCCEEDED(automation->CreatePropertyCondition(UIA_NamePropertyId, value, &condition)));
    VariantClear(&value);
    IUIAutomationElement *element = nullptr;
    assert(SUCCEEDED(root->FindFirst(TreeScope_Descendants, condition, &element)));
    condition->Release();
    return element;
}

void check_action_event(nk_handle surface, nk_accessibility_node_id id,
                        nk_accessibility_action action) {
    nk_event event{};
    const auto payload = wait_for_action(id, action, &event);
    assert(payload.node_id == id);
    assert(payload.action == action);
    assert(event.source == surface);
    nk_event_release(&event);
}

template <typename Predicate> bool wait_for(Predicate predicate) {
    for (int attempt = 0; attempt < 2000; ++attempt) {
        if (predicate())
            return true;
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        Sleep(1);
    }
    return predicate();
}

} // namespace

int main() {
    const HRESULT com_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    assert(SUCCEEDED(com_result));

    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    assert((nk_get_capabilities() & NK_CAP_ACCESSIBILITY) != 0);

    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 520;
    window_options.height = 360;
    window_options.flags = NK_WINDOW_RESIZABLE;
    window_options.title = "NativeKit Windows UIA integration";
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);
    assert(nk_window_show(window, 1) == NK_OK);

    nk_surface_options surface_options{};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.flags = NK_SURFACE_DEPTH | NK_SURFACE_STENCIL;
    surface_options.api = NK_GRAPHICS_D3D11;
    surface_options.width = 520;
    surface_options.height = 360;
    nk_surface surface = NK_INVALID_HANDLE;
    assert(nk_surface_create(window, &surface_options, &surface) == NK_OK);
    const HWND child = surface_window(window);

    nk_accessibility_node nodes[6]{};
    for (auto &node : nodes) {
        node.struct_size = sizeof(node);
        node.selection_start = NK_ACCESSIBILITY_TEXT_POSITION_NONE;
        node.selection_end = NK_ACCESSIBILITY_TEXT_POSITION_NONE;
        node.row_index = NK_ACCESSIBILITY_INDEX_NONE;
        node.column_index = NK_ACCESSIBILITY_INDEX_NONE;
    }
    nodes[0].id = 1;
    nodes[0].parent_id = NK_ACCESSIBILITY_ROOT;
    nodes[0].role = NK_ACCESSIBILITY_GROUP;
    nodes[0].label = "Explorer controls";
    nodes[0].width = 520;
    nodes[0].height = 360;
    nodes[1].id = 2;
    nodes[1].parent_id = 1;
    nodes[1].child_index = 0;
    nodes[1].role = NK_ACCESSIBILITY_BUTTON;
    nodes[1].states = NK_ACCESSIBILITY_FOCUSABLE;
    nodes[1].actions = NK_ACCESSIBILITY_CAN_ACTIVATE | NK_ACCESSIBILITY_CAN_FOCUS;
    nodes[1].label = "Open Overview";
    nodes[1].x = 12;
    nodes[1].y = 16;
    nodes[1].width = 180;
    nodes[1].height = 36;
    nodes[2].id = 3;
    nodes[2].parent_id = 1;
    nodes[2].child_index = 1;
    nodes[2].role = NK_ACCESSIBILITY_SLIDER;
    nodes[2].states = NK_ACCESSIBILITY_FOCUSABLE;
    nodes[2].actions = NK_ACCESSIBILITY_CAN_SET_VALUE | NK_ACCESSIBILITY_CAN_INCREMENT |
                       NK_ACCESSIBILITY_CAN_DECREMENT | NK_ACCESSIBILITY_CAN_FOCUS;
    nodes[2].label = "Zoom";
    nodes[2].value = "1";
    nodes[2].document_length = 1;
    nodes[2].numeric_value = 1;
    nodes[2].numeric_minimum = 0;
    nodes[2].numeric_maximum = 10;
    nodes[2].x = 12;
    nodes[2].y = 64;
    nodes[2].width = 240;
    nodes[2].height = 32;
    nodes[3].id = 4;
    nodes[3].parent_id = 1;
    nodes[3].child_index = 2;
    nodes[3].role = NK_ACCESSIBILITY_CHECKBOX;
    nodes[3].states = NK_ACCESSIBILITY_FOCUSABLE;
    nodes[3].actions = NK_ACCESSIBILITY_CAN_TOGGLE | NK_ACCESSIBILITY_CAN_FOCUS;
    nodes[3].label = "Show labels";
    nodes[3].x = 12;
    nodes[3].y = 112;
    nodes[3].width = 180;
    nodes[3].height = 32;
    nodes[4].id = 5;
    nodes[4].parent_id = 1;
    nodes[4].child_index = 3;
    nodes[4].role = NK_ACCESSIBILITY_TEXT_FIELD;
    nodes[4].states = NK_ACCESSIBILITY_FOCUSABLE;
    nodes[4].actions = NK_ACCESSIBILITY_CAN_SET_VALUE | NK_ACCESSIBILITY_CAN_FOCUS;
    nodes[4].label = "Filter items";
    nodes[4].value = "all";
    nodes[4].document_length = 3;
    nodes[4].x = 12;
    nodes[4].y = 160;
    nodes[4].width = 200;
    nodes[4].height = 32;
    nodes[5].id = 6;
    nodes[5].parent_id = 4;
    nodes[5].role = NK_ACCESSIBILITY_TEXT;
    nodes[5].label = "Show labels setting detail";
    nodes[5].x = 208;
    nodes[5].y = 112;
    nodes[5].width = 180;
    nodes[5].height = 32;
    nk_accessibility_update update{};
    update.struct_size = sizeof(update);
    update.flags = NK_ACCESSIBILITY_UPDATE_FOCUS;
    update.nodes = nodes;
    update.node_count = static_cast<uint32_t>(std::size(nodes));
    update.focus = 2;
    assert(nk_surface_accessibility_update(surface, &update) == NK_OK);

    IUIAutomation *automation = nullptr;
    assert(SUCCEEDED(CoCreateInstance(clsid_ui_automation, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&automation))));
    IUIAutomationElement *root = nullptr;
    assert(SUCCEEDED(automation->ElementFromHandle(child, &root)));
    assert(root);
    int root_type = 0;
    assert(SUCCEEDED(root->get_CurrentControlType(&root_type)));
    assert(root_type == UIA_PaneControlTypeId);
    auto *signals = new AutomationSignals();
    PROPERTYID properties[] = {UIA_NamePropertyId, UIA_HasKeyboardFocusPropertyId,
                               UIA_ValueValuePropertyId};
    SAFEARRAY *property_array =
        SafeArrayCreateVector(VT_I4, 0, static_cast<ULONG>(std::size(properties)));
    assert(property_array);
    for (LONG index = 0; index < static_cast<LONG>(std::size(properties)); ++index) {
        LONG property = properties[index];
        assert(SUCCEEDED(SafeArrayPutElement(property_array, &index, &property)));
    }
    assert(SUCCEEDED(automation->AddPropertyChangedEventHandler(root, TreeScope_SubTree, nullptr,
                                                                signals, property_array)));
    SafeArrayDestroy(property_array);
    assert(SUCCEEDED(automation->AddFocusChangedEventHandler(nullptr, signals)));
    assert(SUCCEEDED(
        automation->AddStructureChangedEventHandler(root, TreeScope_SubTree, nullptr, signals)));

    IUIAutomationElement *button = find_named(automation, root, L"Open Overview");
    assert(button);
    int button_type = 0;
    assert(SUCCEEDED(button->get_CurrentControlType(&button_type)));
    assert(button_type == UIA_ButtonControlTypeId);
    RECT button_bounds{};
    assert(SUCCEEDED(button->get_CurrentBoundingRectangle(&button_bounds)));
    const int expected_button_width = MulDiv(180, static_cast<int>(window_dpi(child)), 96);
    assert(std::abs((button_bounds.right - button_bounds.left) - expected_button_width) <= 2);
    IUIAutomationInvokePattern *invoke = nullptr;
    assert(SUCCEEDED(button->GetCurrentPatternAs(UIA_InvokePatternId, IID_PPV_ARGS(&invoke))));
    drain_events();
    assert(SUCCEEDED(invoke->Invoke()));
    check_action_event(surface, 2, NK_ACCESSIBILITY_ACTION_ACTIVATE);
    drain_events();
    assert(SUCCEEDED(button->SetFocus()));
    check_action_event(surface, 2, NK_ACCESSIBILITY_ACTION_FOCUS);
    invoke->Release();

    IUIAutomationRangeValuePattern *range = nullptr;
    IUIAutomationElement *slider = find_named(automation, root, L"Zoom");
    assert(slider);
    assert(SUCCEEDED(slider->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_PPV_ARGS(&range))));
    drain_events();
    assert(SUCCEEDED(range->SetValue(4.25)));
    nk_event value_event{};
    const auto value_payload = wait_for_action(3, NK_ACCESSIBILITY_ACTION_SET_VALUE, &value_event);
    assert(value_event.source == surface);
    assert(value_payload.node_id == 3);
    const char *value = nullptr;
    uint32_t value_length = 0;
    assert(nk_accessibility_action_event_value(&value_event, &value, &value_length) == NK_OK);
    assert(value && value_length == 4 && std::memcmp(value, "4.25", 4) == 0);
    nk_event_release(&value_event);
    range->Release();

    IUIAutomationValuePattern *text_value = nullptr;
    IUIAutomationElement *text_field = find_named(automation, root, L"Filter items");
    assert(text_field);
    assert(
        SUCCEEDED(text_field->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&text_value))));
    drain_events();
    BSTR replacement_text = SysAllocString(L"Part");
    assert(replacement_text);
    assert(SUCCEEDED(text_value->SetValue(replacement_text)));
    SysFreeString(replacement_text);
    nk_event text_event{};
    const auto text_payload = wait_for_action(5, NK_ACCESSIBILITY_ACTION_SET_VALUE, &text_event);
    assert(text_event.source == surface);
    assert(nk_accessibility_action_event_value(&text_event, &value, &value_length) == NK_OK);
    assert(value && value_length == 4 && std::memcmp(value, "Part", 4) == 0);
    assert(text_payload.node_id == 5);
    nk_event_release(&text_event);
    nodes[4].value = "Part";
    nodes[4].document_length = 4;
    assert(nk_surface_accessibility_update(surface, &update) == NK_OK);
    assert(wait_for([&] { return signals->value_changed.load(std::memory_order_acquire); }));
    BSTR current_text = nullptr;
    assert(SUCCEEDED(text_value->get_CurrentValue(&current_text)));
    assert(current_text && std::wcscmp(current_text, L"Part") == 0);
    SysFreeString(current_text);
    text_value->Release();

    IUIAutomationTogglePattern *toggle = nullptr;
    IUIAutomationElement *checkbox = find_named(automation, root, L"Show labels");
    assert(checkbox);
    assert(SUCCEEDED(checkbox->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&toggle))));
    drain_events();
    assert(SUCCEEDED(toggle->Toggle()));
    check_action_event(surface, 4, NK_ACCESSIBILITY_ACTION_TOGGLE);
    toggle->Release();

    nodes[1].label = "Open Components";
    update.focus = 3;
    assert(nk_surface_accessibility_update(surface, &update) == NK_OK);
    assert(wait_for([&] {
        return signals->name_changed.load(std::memory_order_acquire) &&
               signals->value_changed.load(std::memory_order_acquire) &&
               signals->focus_property_changed.load(std::memory_order_acquire) &&
               signals->focus_changed.load(std::memory_order_acquire);
    }));
    BOOL button_focused = TRUE;
    BOOL slider_focused = FALSE;
    assert(SUCCEEDED(button->get_CurrentHasKeyboardFocus(&button_focused)));
    assert(SUCCEEDED(slider->get_CurrentHasKeyboardFocus(&slider_focused)));
    assert(button_focused == FALSE);
    assert(slider_focused == TRUE);
    BSTR current_name = nullptr;
    assert(SUCCEEDED(button->get_CurrentName(&current_name)));
    assert(current_name && std::wcscmp(current_name, L"Open Components") == 0);
    SysFreeString(current_name);
    assert(nk_surface_accessibility_remove_node(surface, 4) == NK_OK);
    assert(wait_for([&] { return signals->structure_changed.load(std::memory_order_acquire); }));
    IUIAutomationElement *removed_checkbox = find_named(automation, root, L"Show labels");
    assert(removed_checkbox == nullptr);
    IUIAutomationElement *removed_descendant =
        find_named(automation, root, L"Show labels setting detail");
    assert(removed_descendant == nullptr);

    const UINT dpi = window_dpi(child);
    assert(nk_surface_set_bounds(surface, 8, 10, 360, 240) == NK_OK);
    for (int iteration = 0; iteration < 30; ++iteration) {
        const int width = 360 + (iteration * 17) % 120;
        const int height = 240 + (iteration * 13) % 90;
        assert(nk_surface_set_bounds(surface, 8, 10, width, height) == NK_OK);
        assert(nk_surface_make_current(surface) == NK_OK);
        nk_surface_frame_target target{};
        target.struct_size = sizeof(target);
        assert(nk_surface_get_frame_target(surface, &target) == NK_OK);
        assert(target.width > 0 && target.height > 0);
        assert(nk_surface_present(surface) == NK_OK);
    }
    assert(window_dpi(child) == dpi);

    text_field->Release();
    checkbox->Release();
    slider->Release();
    button->Release();
    assert(SUCCEEDED(automation->RemovePropertyChangedEventHandler(root, signals)));
    assert(SUCCEEDED(automation->RemoveFocusChangedEventHandler(signals)));
    assert(SUCCEEDED(automation->RemoveStructureChangedEventHandler(root, signals)));
    signals->Release();
    root->Release();
    automation->Release();
    assert(nk_surface_accessibility_clear(surface) == NK_OK);
    assert(nk_surface_destroy(surface) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    CoUninitialize();
    return 0;
}
