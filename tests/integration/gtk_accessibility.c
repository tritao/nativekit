#include "nativekit.h"
#include "nativekit_accessibility.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#include <assert.h>

static nk_accessibility_node make_node(uint32_t id, uint32_t parent, uint32_t role,
                                       const char *label, const char *value) {
    nk_accessibility_node node = {0};
    node.struct_size = sizeof(node);
    node.id = id;
    node.parent_id = parent;
    node.role = role;
    node.states = NK_ACCESSIBILITY_FOCUSABLE;
    node.actions = NK_ACCESSIBILITY_CAN_ACTIVATE | NK_ACCESSIBILITY_CAN_FOCUS |
                   NK_ACCESSIBILITY_CAN_SET_SELECTION | NK_ACCESSIBILITY_CAN_SET_VALUE;
    node.width = 120;
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

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.flags = NK_WINDOW_HIDDEN;
    window_options.width = 320;
    window_options.height = 240;
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);

    nk_surface_options surface_options = {0};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.flags = NK_SURFACE_HIDDEN;
    surface_options.api = NK_GRAPHICS_OPENGL;
    surface_options.width = 320;
    surface_options.height = 240;
    nk_surface surface = NK_INVALID_HANDLE;
    assert(nk_surface_create(window, &surface_options, &surface) == NK_OK);

    nk_accessibility_node root =
        make_node(1, NK_ACCESSIBILITY_ROOT, NK_ACCESSIBILITY_GROUP, "Document", NULL);
    assert(nk_surface_accessibility_set_node(surface, &root) == NK_OK);
    nk_accessibility_node field =
        make_node(2, 1, NK_ACCESSIBILITY_TEXT_FIELD, "Name", "Hello");
    assert(nk_surface_accessibility_set_node(surface, &field) == NK_OK);
    assert(nk_surface_accessibility_set_focus(surface, 2) == NK_OK);

    nk_accessibility_text_range range = {0, 5, 0, 0, 80, 24};
    assert(nk_surface_accessibility_set_text_ranges(surface, 2, &range, 1) == NK_OK);

    nk_accessibility_update update = {0};
    update.struct_size = sizeof(update);
    update.flags = NK_ACCESSIBILITY_UPDATE_FOCUS;
    update.nodes = &field;
    update.node_count = 1;
    update.focus = 2;
    assert(nk_surface_accessibility_update(surface, &update) == NK_OK);
    assert(nk_surface_accessibility_remove_node(surface, 1) == NK_OK);
    assert(nk_surface_accessibility_clear(surface) == NK_OK);

    assert(nk_surface_destroy(surface) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
