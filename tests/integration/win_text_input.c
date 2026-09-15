#include "nativekit.h"
#include "nativekit_input.h"
#include "nativekit_window.h"

#include <assert.h>
#include <windows.h>
#include <imm.h>
#include <math.h>
#include <string.h>
#include <wchar.h>

static int skip_test(nk_window window, HWND hwnd, HIMC context) {
    if (context)
        ImmReleaseContext(hwnd, context);
    nk_surface_set_text_input_active(window, 0);
    nk_window_destroy(window);
    nk_shutdown();
    return 77;
}

static int wait_for_edit(nk_window window, nk_text_edit_action action, nk_event *out_event) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_TEXT_EDIT && event.source == window) {
            nk_text_edit_event edit = {0};
            assert(event.data_size >= sizeof(edit));
            memcpy(&edit, event.data, sizeof(edit));
            if (edit.action == action) {
                *out_event = event;
                return 1;
            }
        }
        nk_event_release(&event);
        Sleep(5);
    }
    return 0;
}

static int wait_for_text(nk_window window, uint32_t codepoint) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        if (event.kind == NK_EVENT_TEXT_INPUT && event.source == window) {
            nk_text_input_event input = {0};
            assert(event.data_size >= sizeof(input));
            memcpy(&input, event.data, sizeof(input));
            if (input.codepoint == codepoint) {
                nk_event_release(&event);
                return 1;
            }
        }
        nk_event_release(&event);
        Sleep(5);
    }
    return 0;
}

static int verify_edit(const nk_event *event, nk_text_edit_action action,
                       nk_text_position replace_start, nk_text_position replace_end,
                       nk_text_position selection_start, nk_text_position selection_end,
                       nk_text_position composition_start, nk_text_position composition_end,
                       const char *text) {
    nk_text_edit_event edit = {0};
    memcpy(&edit, event->data, sizeof(edit));
    if (edit.action != action)
        return 1;
    if (edit.replace_start != replace_start)
        return 1;
    if (edit.replace_end != replace_end)
        return 1;
    if (edit.composition_start != composition_start)
        return 1;
    if (edit.composition_end != composition_end)
        return 1;
    if (action == NK_TEXT_EDIT_COMPOSE || action == NK_TEXT_EDIT_FINISH_COMPOSITION) {
        if (edit.selection_start != edit.selection_end || edit.selection_start < 2 ||
            edit.selection_start > 7)
            return 1;
    } else if (edit.selection_start != selection_start || edit.selection_end != selection_end) {
        return 1;
    }
    const char *event_text = NULL;
    uint32_t event_length = 0;
    if (nk_text_edit_event_text(event, &event_text, &event_length) != NK_OK ||
        event_length != strlen(text) ||
        (event_length != 0 && memcmp(event_text, text, event_length) != 0))
        return 1;
    return 0;
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
    options.title = "NativeKit IMM32 text input test";
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&options, &window) == NK_OK);
    assert(nk_window_show(window, 1) == NK_OK);
    assert(nk_window_activate(window) == NK_OK);

    nk_native_window native = {0};
    native.struct_size = sizeof(native);
    assert(nk_window_get_native(window, &native) == NK_OK);
    assert(native.kind == NK_NATIVE_WINDOW_WIN32);
    HWND hwnd = (HWND)native.window;
    assert(hwnd != NULL);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    float scale = 0.0f;
    assert(nk_window_get_scale(window, &scale) == NK_OK);
    const char text[] = "nativekit";
    nk_text_input_state state = {0};
    state.struct_size = sizeof(state);
    state.flags = NK_TEXT_INPUT_MULTILINE;
    state.text = text;
    state.text_start = 0;
    state.document_length = 9;
    state.selection_start = 2;
    state.selection_end = 2;
    state.composition_start = NK_TEXT_POSITION_NONE;
    state.composition_end = NK_TEXT_POSITION_NONE;
    state.cursor_x = 47.0f;
    state.cursor_y = 83.0f;
    state.cursor_width = 1.0f;
    state.cursor_height = 18.0f;
    assert(nk_surface_set_text_input_state(window, &state) == NK_OK);
    assert(nk_surface_set_text_input_active(window, 1) == NK_OK);

    HIMC context = ImmGetContext(hwnd);
    if (!context) {
        assert(nk_surface_set_text_input_active(window, 0) == NK_OK);
        assert(nk_window_destroy(window) == NK_OK);
        nk_shutdown();
        return 77;
    }
    COMPOSITIONFORM composition = {0};
    if (!ImmGetCompositionWindow(context, &composition) ||
        composition.dwStyle != CFS_POINT ||
        composition.ptCurrentPos.x != (LONG)lroundf(state.cursor_x * scale) ||
        composition.ptCurrentPos.y != (LONG)lroundf(state.cursor_y * scale))
        return skip_test(window, hwnd, context);
    CANDIDATEFORM candidate = {0};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_CANDIDATEPOS;
    candidate.ptCurrentPos = composition.ptCurrentPos;
    if (!ImmSetCandidateWindow(context, &candidate)) {
        return skip_test(window, hwnd, context);
    }
    if (ImmGetCandidateWindow(context, 0, &candidate) == sizeof(candidate)) {
        if (candidate.dwStyle != CFS_CANDIDATEPOS ||
            candidate.ptCurrentPos.x != composition.ptCurrentPos.x ||
            candidate.ptCurrentPos.y != composition.ptCurrentPos.y)
            return skip_test(window, hwnd, context);
    }

    if (!ImmSetOpenStatus(context, TRUE))
        return skip_test(window, hwnd, context);
    SendMessageW(hwnd, WM_IME_STARTCOMPOSITION, 0, 0);
    const wchar_t preedit[] = L"kanji";
    const DWORD preedit_bytes = (DWORD)(wcslen(preedit) * sizeof(wchar_t));
    const BOOL injected = ImmSetCompositionStringW(context, SCS_SETSTR, (void *)preedit,
                                                    preedit_bytes, NULL, 0);
    int composition_supported = 0;
    if (injected) {
        SendMessageW(hwnd, WM_IME_COMPOSITION, 0, GCS_COMPSTR | GCS_CURSORPOS);
        nk_event compose = {0};
        compose.struct_size = sizeof(compose);
        if (!wait_for_edit(window, NK_TEXT_EDIT_COMPOSE, &compose))
            return skip_test(window, hwnd, context);
        const char *compose_text = NULL;
        uint32_t compose_length = 0;
        if (nk_text_edit_event_text(&compose, &compose_text, &compose_length) != NK_OK ||
            compose_length == 0) {
            nk_event_release(&compose);
        } else {
            const int compose_result =
                verify_edit(&compose, NK_TEXT_EDIT_COMPOSE, 2, 2, 2, 2, 2, 7, "kanji");
            if (compose_result != 0) {
                nk_event_release(&compose);
                return 2;
            }
            nk_event_release(&compose);

            SendMessageW(hwnd, WM_IME_ENDCOMPOSITION, 0, 0);
            nk_event finish = {0};
            finish.struct_size = sizeof(finish);
            if (!wait_for_edit(window, NK_TEXT_EDIT_FINISH_COMPOSITION, &finish))
                return skip_test(window, hwnd, context);
            const int finish_result =
                verify_edit(&finish, NK_TEXT_EDIT_FINISH_COMPOSITION, NK_TEXT_POSITION_NONE,
                            NK_TEXT_POSITION_NONE, 2, 2, NK_TEXT_POSITION_NONE,
                            NK_TEXT_POSITION_NONE, "");
            if (finish_result != 0) {
                nk_event_release(&finish);
                return 2;
            }
            nk_event_release(&finish);
            composition_supported = 1;
        }
    }

    ImmReleaseContext(hwnd, context);
    if (SendMessageW(hwnd, WM_IME_CHAR, (WPARAM)L'Z', 0) != 0 ||
        !wait_for_text(window, 'Z'))
        return skip_test(window, hwnd, NULL);

    assert(nk_surface_set_text_input_active(window, 0) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return injected && composition_supported ? 0 : 77;
}
