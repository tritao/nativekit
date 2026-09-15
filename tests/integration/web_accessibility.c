#include "nativekit.h"
#include "nativekit_accessibility.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#include <assert.h>
#include <stdint.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

static nk_accessibility_node make_node(uint32_t id, uint32_t parent, uint32_t role,
                                       uint32_t actions, const char *label, const char *value) {
    nk_accessibility_node node = {0};
    node.struct_size = sizeof(node);
    node.id = id;
    node.parent_id = parent;
    node.role = role;
    node.states = NK_ACCESSIBILITY_FOCUSABLE;
    node.actions = actions;
    node.width = 160;
    node.height = 32;
    node.label = label;
    node.value = value;
    node.document_length = value ? 5 : 0;
    node.selection_start = value ? 1 : NK_ACCESSIBILITY_TEXT_POSITION_NONE;
    node.selection_end = value ? 3 : NK_ACCESSIBILITY_TEXT_POSITION_NONE;
    return node;
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    assert((nk_get_capabilities() & NK_CAP_ACCESSIBILITY) != 0);

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 320;
    window_options.height = 240;
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);

    nk_surface_options surface_options = {0};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.api = NK_GRAPHICS_OPENGL_ES;
    surface_options.width = 320;
    surface_options.height = 240;
    nk_surface surface = NK_INVALID_HANDLE;
    assert(nk_surface_create(window, &surface_options, &surface) == NK_OK);

    nk_accessibility_node root = make_node(1, NK_ACCESSIBILITY_ROOT, NK_ACCESSIBILITY_GROUP,
                                           NK_ACCESSIBILITY_CAN_FOCUS, "Document", NULL);
    nk_accessibility_node button =
        make_node(2, 1, NK_ACCESSIBILITY_BUTTON,
                  NK_ACCESSIBILITY_CAN_ACTIVATE | NK_ACCESSIBILITY_CAN_FOCUS, "Continue", NULL);
    nk_accessibility_node field =
        make_node(3, 1, NK_ACCESSIBILITY_TEXT_FIELD,
                  NK_ACCESSIBILITY_CAN_FOCUS | NK_ACCESSIBILITY_CAN_SET_VALUE |
                      NK_ACCESSIBILITY_CAN_SET_SELECTION,
                  "Name", "Hello");
    assert(nk_surface_accessibility_set_node(surface, &root) == NK_OK);
    assert(nk_surface_accessibility_set_node(surface, &button) == NK_OK);
    assert(nk_surface_accessibility_set_node(surface, &field) == NK_OK);
    assert(nk_surface_accessibility_set_focus(surface, 2) == NK_OK);
    nk_accessibility_text_range range = {0, 5, 0, 0, 80, 24};
    assert(nk_surface_accessibility_set_text_ranges(surface, 3, &range, 1) == NK_OK);

#ifdef __EMSCRIPTEN__
    EM_ASM({
        const node = document.querySelector("[data-nativekit-accessibility-node='2']");
        if (!node) {
            document.documentElement.dataset.nativekitAccessibilityResult = "missing";
        } else {
            node.dispatchEvent(new MouseEvent("click", {bubbles : true}));
        }
    });
#endif

    nk_event event = {0};
    event.struct_size = sizeof(event);
    int action_seen = 0;
    while (nk_poll_event(&event) == NK_OK && event.kind != NK_EVENT_NONE) {
        if (event.kind == NK_EVENT_ACCESSIBILITY_ACTION) {
            const nk_accessibility_action_event *action =
                (const nk_accessibility_action_event *)event.data;
            assert(action && action->node_id == 2);
            assert(action->action == NK_ACCESSIBILITY_ACTION_ACTIVATE);
            action_seen = 1;
        }
        nk_event_release(&event);
        event.struct_size = sizeof(event);
    }
    nk_event_release(&event);
    assert(action_seen);

#ifdef __EMSCRIPTEN__
    EM_ASM({ document.documentElement.dataset.nativekitAccessibilityResult = "passed"; });
#endif

    assert(nk_surface_accessibility_clear(surface) == NK_OK);
    assert(nk_surface_destroy(surface) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
