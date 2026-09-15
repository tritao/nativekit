#include "nativekit.h"
#include "nativekit_input.h"
#include "nativekit_window.h"

#import <Cocoa/Cocoa.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static nk_event wait_for_edit(nk_window window, nk_text_edit_action action) {
    for (int attempt = 0; attempt < 500; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_TEXT_EDIT && event.source == window) {
            nk_text_edit_event edit = {0};
            assert(event.data_size >= sizeof(edit));
            memcpy(&edit, event.data, sizeof(edit));
            if (edit.action == action)
                return event;
        }
        nk_event_release(&event);
        usleep(10000);
    }
    assert(!"timed out waiting for text edit event");
    nk_event unreachable = {0};
    return unreachable;
}

static void verify_edit(const nk_event *event, nk_text_edit_action action,
                        nk_text_position replace_start, nk_text_position replace_end,
                        nk_text_position selection_start, nk_text_position selection_end,
                        nk_text_position composition_start, nk_text_position composition_end,
                        const char *text) {
    nk_text_edit_event edit = {0};
    memcpy(&edit, event->data, sizeof(edit));
    assert(edit.action == action);
    assert(edit.replace_start == replace_start);
    assert(edit.replace_end == replace_end);
    assert(edit.selection_start == selection_start);
    assert(edit.selection_end == selection_end);
    assert(edit.composition_start == composition_start);
    assert(edit.composition_end == composition_end);
    const char *event_text = NULL;
    uint32_t event_length = 0;
    assert(nk_text_edit_event_text(event, &event_text, &event_length) == NK_OK);
    assert(event_length == strlen(text));
    assert(event_length == 0 || memcmp(event_text, text, event_length) == 0);
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    nk_window_options options = {0};
    options.struct_size = sizeof(options);
    options.width = 640;
    options.height = 360;
    options.flags = NK_WINDOW_RESIZABLE;
    options.title = "NativeKit NSTextInputClient test";
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&options, &window) == NK_OK);
    assert(nk_window_show(window, 1) == NK_OK);
    assert(nk_window_activate(window) == NK_OK);

    nk_native_window native = {0};
    native.struct_size = sizeof(native);
    assert(nk_window_get_native(window, &native) == NK_OK);
    assert(native.kind == NK_NATIVE_WINDOW_COCOA);
    assert(native.view != 0);
    id<NSTextInputClient> input_view =
        (__bridge id<NSTextInputClient>)(void *)native.view;

    const char initial_text[] = "A\xF0\x9F\x98\x80\xE6\x97\xA5\xE6\x9C\AC";
    nk_text_input_state state = {0};
    state.struct_size = sizeof(state);
    state.text = initial_text;
    state.document_length = 4;
    state.selection_start = 1;
    state.selection_end = 2;
    state.composition_start = NK_TEXT_POSITION_NONE;
    state.composition_end = NK_TEXT_POSITION_NONE;
    state.cursor_x = 32.0f;
    state.cursor_y = 48.0f;
    state.cursor_width = 1.0f;
    state.cursor_height = 18.0f;
    assert(nk_surface_set_text_input_state(window, &state) == NK_OK);
    assert(nk_surface_set_text_input_active(window, 1) == NK_OK);

    [input_view setMarkedText:@"かな"
                 selectedRange:NSMakeRange(2, 0)
              replacementRange:NSMakeRange(1, 2)];
    nk_event compose = wait_for_edit(window, NK_TEXT_EDIT_COMPOSE);
    verify_edit(&compose, NK_TEXT_EDIT_COMPOSE, 1, 2, 3, 3, 1, 3, "かな");
    nk_event_release(&compose);

    const char composed_text[] = "A\xE3\x81\x8B\xE3\x81\AA\xE6\x97\A5\xE6\x9C\AC";
    state.text = composed_text;
    state.document_length = 5;
    state.selection_start = 3;
    state.selection_end = 3;
    state.composition_start = 1;
    state.composition_end = 3;
    assert(nk_surface_set_text_input_state(window, &state) == NK_OK);
    assert([input_view hasMarkedText]);
    assert(NSEqualRanges([input_view markedRange], NSMakeRange(1, 2)));
    assert(NSEqualRanges([input_view selectedRange], NSMakeRange(3, 0)));

    NSRange actual_range = NSMakeRange(NSNotFound, 0);
    NSRect caret_rect =
        [input_view firstRectForCharacterRange:NSMakeRange(3, 0) actualRange:&actual_range];
    assert(actual_range.location == 3 && actual_range.length == 0);
    assert(caret_rect.size.width > 0.0 && caret_rect.size.height > 0.0);

    [input_view unmarkText];
    nk_event cancel = wait_for_edit(window, NK_TEXT_EDIT_FINISH_COMPOSITION);
    verify_edit(&cancel, NK_TEXT_EDIT_FINISH_COMPOSITION, NK_TEXT_POSITION_NONE,
                NK_TEXT_POSITION_NONE, 3, 3, NK_TEXT_POSITION_NONE, NK_TEXT_POSITION_NONE, "");
    nk_event_release(&cancel);
    assert(![input_view hasMarkedText]);

    state.composition_start = NK_TEXT_POSITION_NONE;
    state.composition_end = NK_TEXT_POSITION_NONE;
    assert(nk_surface_set_text_input_state(window, &state) == NK_OK);
    [input_view insertText:@"終" replacementRange:NSMakeRange(5, 0)];
    nk_event commit = wait_for_edit(window, NK_TEXT_EDIT_COMMIT);
    verify_edit(&commit, NK_TEXT_EDIT_COMMIT, 5, 5, 6, 6, NK_TEXT_POSITION_NONE,
                NK_TEXT_POSITION_NONE, "終");
    nk_event_release(&commit);

    assert(nk_surface_set_text_input_active(window, 0) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
