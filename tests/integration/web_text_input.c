#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_input.h"
#include "nativekit_window.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

static void set_text_state(nk_surface surface, const char *text, nk_text_position document_length,
                           nk_text_position selection_start, nk_text_position selection_end,
                           nk_text_position composition_start,
                           nk_text_position composition_end) {
    nk_text_input_state state = {0};
    state.struct_size = sizeof(state);
    state.flags = NK_TEXT_INPUT_MULTILINE;
    state.text = text;
    state.text_start = 10;
    state.document_length = document_length;
    state.selection_start = selection_start;
    state.selection_end = selection_end;
    state.composition_start = composition_start;
    state.composition_end = composition_end;
    state.cursor_width = 1.0f;
    state.cursor_height = 18.0f;
    assert(nk_surface_set_text_input_state(surface, &state) == NK_OK);
}

static void test_failure(void) {
#ifdef __EMSCRIPTEN__
    EM_ASM({ document.documentElement.dataset.nativekitWebTextInputResult = "failed"; });
#endif
    abort();
}

static void expect_edit(nk_surface surface, nk_text_edit_action action,
                        nk_text_position replacement_start, nk_text_position replacement_end,
                        const char *replacement, nk_text_position selection_start,
                        nk_text_position selection_end, nk_text_position composition_start,
                        nk_text_position composition_end) {
    nk_event event = {0};
    event.struct_size = sizeof(event);
    for (;;) {
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_NONE) {
            nk_event_release(&event);
            test_failure();
        }
        if (event.source != surface || event.kind != NK_EVENT_TEXT_EDIT) {
            nk_event_release(&event);
            event.struct_size = sizeof(event);
            continue;
        }
        assert(event.data_size >= sizeof(nk_text_edit_event));
        const nk_text_edit_event *edit = (const nk_text_edit_event *)event.data;
        assert(edit->action == action);
        assert(edit->replace_start == replacement_start);
        assert(edit->replace_end == replacement_end);
        assert(edit->selection_start == selection_start);
        assert(edit->selection_end == selection_end);
        assert(edit->composition_start == composition_start);
        assert(edit->composition_end == composition_end);
        const char *text = NULL;
        uint32_t length = 0;
        assert(nk_text_edit_event_text(&event, &text, &length) == NK_OK);
        assert(length == (uint32_t)strlen(replacement));
        assert(length == 0 || (text && memcmp(text, replacement, length) == 0));
        nk_event_release(&event);
        return;
    }
}

#ifdef __EMSCRIPTEN__
static int dispatch_input_sequence(void) {
    return EM_ASM_INT({
        const input = document.querySelector("[id^='__nativekit_text_input_']");
        if (!input)
            return 0;
        input.setSelectionRange(1, 3);
        input.dispatchEvent(new InputEvent("beforeinput", {
            bubbles: true, cancelable: true, data: "かな", inputType: "insertReplacementText"
        }));
        return 1;
    });
}

static int geometry_anchor_is_published(void) {
    return EM_ASM_INT({
        const input = document.querySelector("[id^='__nativekit_text_input_']");
        const canvas = document.querySelector("canvas");
        if (!input || !canvas)
            return 0;
        const canvasRect = canvas.getBoundingClientRect();
        const left = Number.parseFloat(input.style.left);
        const top = Number.parseFloat(input.style.top);
        return Math.abs(left - (canvasRect.left + 22)) < 1 &&
               Math.abs(top - (canvasRect.top + 33)) < 1 ? 1 : 0;
    });
}

static int dispatch_selection(void) {
    return EM_ASM_INT({
        const input = document.querySelector("[id^='__nativekit_text_input_']");
        if (!input)
            return 0;
        input.setSelectionRange(0, 3);
        input.dispatchEvent(new Event("select", {bubbles: true}));
        return 1;
    });
}

static int dispatch_delete_backward(void) {
    return EM_ASM_INT({
        const input = document.querySelector("[id^='__nativekit_text_input_']");
        if (!input)
            return 0;
        input.setSelectionRange(3, 3);
        input.dispatchEvent(new InputEvent("beforeinput", {
            bubbles: true, cancelable: true, data: null, inputType: "deleteContentBackward"
        }));
        return 1;
    });
}

static int dispatch_composition(void) {
    return EM_ASM_INT({
        const input = document.querySelector("[id^='__nativekit_text_input_']");
        if (!input)
            return 0;
        input.setSelectionRange(2, 2);
        input.dispatchEvent(new CompositionEvent("compositionstart", {bubbles: true}));
        input.dispatchEvent(new CompositionEvent("compositionupdate", {
            bubbles: true, data: "日"
        }));
        return 1;
    });
}

static int dispatch_composition_commit(void) {
    return EM_ASM_INT({
        const input = document.querySelector("[id^='__nativekit_text_input_']");
        if (!input)
            return 0;
        input.dispatchEvent(new CompositionEvent("compositionend", {
            bubbles: true, data: "日"
        }));
        return 1;
    });
}

static int dispatch_composition_cancel(void) {
    return EM_ASM_INT({
        const input = document.querySelector("[id^='__nativekit_text_input_']");
        if (!input)
            return 0;
        input.setSelectionRange(3, 3);
        input.dispatchEvent(new CompositionEvent("compositionstart", {bubbles: true}));
        input.dispatchEvent(new CompositionEvent("compositionupdate", {
            bubbles: true, data: "x"
        }));
        return 1;
    });
}

static int dispatch_composition_finish(void) {
    return EM_ASM_INT({
        const input = document.querySelector("[id^='__nativekit_text_input_']");
        if (!input)
            return 0;
        input.dispatchEvent(new CompositionEvent("compositionend", {
            bubbles: true, data: ""
        }));
        return 1;
    });
}
#endif

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

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

    set_text_state(surface, "A\xf0\x9f\x98\x80" "B", 13, 11, 12,
                   NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
    assert(nk_surface_set_text_input_active(surface, 1) == NK_OK);

#ifdef __EMSCRIPTEN__
    nk_text_input_rect selection_rect = {sizeof(nk_text_input_rect), 22.0f, 33.0f, 12.0f,
                                         18.0f};
    assert(nk_surface_set_text_input_geometry(
               surface, 11, 12, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE,
               (const uint8_t *)&selection_rect, sizeof(selection_rect), NULL, 0) == NK_OK);
    assert(geometry_anchor_is_published());
    assert(dispatch_input_sequence());
    expect_edit(surface, NK_TEXT_EDIT_COMMIT, 11, 12, "\xe3\x81\x8b\xe3\x81\xaa", 13, 13,
                NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);

    set_text_state(surface, "A\xe3\x81\x8b\xe3\x81\xaa" "B", 14, 10, 10,
                   NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
    assert(dispatch_selection());
    expect_edit(surface, NK_TEXT_EDIT_SET_SELECTION, NK_TEXT_POSITION_NONE,
                NK_TEXT_POSITION_NONE, "", 10, 13, NK_TEXT_POSITION_NONE,
                NK_TEXT_POSITION_NONE);

    set_text_state(surface, "A\xe3\x81\x8b\xe3\x81\xaa" "B", 14, 13, 13,
                   NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
    assert(dispatch_delete_backward());
    expect_edit(surface, NK_TEXT_EDIT_DELETE, 12, 13, "", 12, 12,
                NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);

    set_text_state(surface, "A\xe3\x81\x8b" "B", 13, 12, 12,
                   NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
    assert(dispatch_composition());
    expect_edit(surface, NK_TEXT_EDIT_COMPOSE, 12, 12, "\xe6\x97\xa5", 13, 13, 12, 13);
    assert(dispatch_composition_commit());
    expect_edit(surface, NK_TEXT_EDIT_COMMIT, 12, 13, "\xe6\x97\xa5", 13, 13,
                NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);

    set_text_state(surface, "A\xe3\x81\x8b\xe6\x97\xa5" "B", 14, 13, 13,
                   NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE);
    assert(dispatch_composition_cancel());
    expect_edit(surface, NK_TEXT_EDIT_COMPOSE, 13, 13, "x", 14, 14, 13, 14);
    assert(dispatch_composition_finish());
    expect_edit(surface, NK_TEXT_EDIT_FINISH_COMPOSITION, NK_TEXT_POSITION_NONE,
                NK_TEXT_POSITION_NONE, "", 14, 14, NK_TEXT_POSITION_NONE,
                NK_TEXT_POSITION_NONE);
    EM_ASM({ document.documentElement.dataset.nativekitWebTextInputResult = "passed"; });
#endif

    assert(nk_surface_set_text_input_active(surface, 0) == NK_OK);
    assert(nk_surface_destroy(surface) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
