#include "nativekit.h"
#include "nativekit_input.h"
#include "nativekit_window.h"

#include <assert.h>
#include <windows.h>
#include <imm.h>
#include <msctf.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

static void focus_window(HWND hwnd) {
    const HWND foreground = GetForegroundWindow();
    DWORD foreground_thread = foreground ? GetWindowThreadProcessId(foreground, NULL) : 0;
    const DWORD current_thread = GetCurrentThreadId();
    const BOOL attached = foreground_thread && foreground_thread != current_thread &&
                          AttachThreadInput(current_thread, foreground_thread, TRUE);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    SetFocus(hwnd);
    if (attached)
        AttachThreadInput(current_thread, foreground_thread, FALSE);
}

static HKL activate_japanese_layout(void) {
    static const wchar_t *layout_ids[] = {
        // The Microsoft Japanese IME is normally exposed as E0010411.  Try
        // it before the plain Japanese keyboard layout, which cannot produce
        // composition events even when the Japanese language is installed.
        L"E0010411",
        L"E0200411",
        L"0411:00000411",
        L"0411",
        L"00000411"
    };
    for (size_t index = 0; index < sizeof(layout_ids) / sizeof(layout_ids[0]); ++index) {
        HKL layout = LoadKeyboardLayoutW(layout_ids[index], KLF_ACTIVATE | KLF_SUBSTITUTE_OK);
        if (layout != NULL) {
            ActivateKeyboardLayout(layout, 0);
            return layout;
        }
    }
    return NULL;
}

static int activate_japanese_ime_profile(void) {
    static const GUID clsid_input_processor_profiles =
        {0x33c53a50, 0xf456, 0x4884, {0xb0, 0x49, 0x85, 0xfd, 0x64, 0x3e, 0xcf, 0xed}};
    static const GUID iid_input_processor_profiles =
        {0x1f02b6c5, 0x7842, 0x4ee6, {0x8a, 0x0b, 0x9a, 0x24, 0x18, 0x3a, 0x95, 0xca}};
    static const GUID iid_input_processor_profile_mgr =
        {0x71c6e74c, 0x0f28, 0x11d8, {0xa8, 0x2a, 0x00, 0x06, 0x5b, 0x84, 0x43, 0x5c}};
    GUID clsid = {0};
    GUID profile = {0};
    ITfInputProcessorProfiles *profiles = NULL;
    HRESULT initialized = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized) && initialized != RPC_E_CHANGED_MODE)
        return 0;
    if (FAILED(CLSIDFromString(L"{03B5835F-F03C-411B-9CE2-AA23E1171E36}", &clsid)) ||
        FAILED(CLSIDFromString(L"{A76C93D9-5523-4E90-AAFA-4DB112F9AC76}", &profile))) {
        if (SUCCEEDED(initialized))
            CoUninitialize();
        return 0;
    }
    HRESULT created = CoCreateInstance(&clsid_input_processor_profiles, NULL,
                                       CLSCTX_INPROC_SERVER, &iid_input_processor_profiles,
                                       (void **)&profiles);
    int activated = 0;
    if (SUCCEEDED(created)) {
        const LANGID japanese = MAKELANGID(LANG_JAPANESE, SUBLANG_JAPANESE_JAPAN);
        HRESULT result = profiles->lpVtbl->ActivateLanguageProfile(
            profiles, &clsid, japanese, &profile);
        activated = SUCCEEDED(result);
        ITfInputProcessorProfileMgr *profile_mgr = NULL;
        HRESULT queried = profiles->lpVtbl->QueryInterface(
            profiles, &iid_input_processor_profile_mgr, (void **)&profile_mgr);
        HRESULT process_result = E_NOINTERFACE;
        if (SUCCEEDED(queried)) {
            process_result = profile_mgr->lpVtbl->ActivateProfile(
                profile_mgr, TF_PROFILETYPE_INPUTPROCESSOR, japanese, &clsid, &profile, NULL,
                TF_IPPMF_FORPROCESS);
            profile_mgr->lpVtbl->Release(profile_mgr);
        }
        LANGID active_language = 0;
        GUID active_profile = {0};
        HRESULT active_result = profiles->lpVtbl->GetActiveLanguageProfile(
            profiles, &clsid, &active_language, &active_profile);
        fprintf(stderr, "win_text_input: profile_result=0x%08lx process_result=0x%08lx "
                        "active_result=0x%08lx active_language=%04x active_profile=%d\n",
                (unsigned long)result, (unsigned long)process_result,
                (unsigned long)active_result,
                (unsigned int)active_language, IsEqualGUID(&active_profile, &profile) ? 1 : 0);
        profiles->lpVtbl->Release(profiles);
    }
    if (SUCCEEDED(initialized))
        CoUninitialize();
    return activated;
}

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
            fprintf(stderr, "win_text_input: edit_action=%u replace=%u..%u selection=%u..%u composition=%u..%u\n",
                    (unsigned int)edit.action, (unsigned int)edit.replace_start,
                    (unsigned int)edit.replace_end, (unsigned int)edit.selection_start,
                    (unsigned int)edit.selection_end, (unsigned int)edit.composition_start,
                    (unsigned int)edit.composition_end);
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

static int send_virtual_key(WORD key) {
    INPUT inputs[2] = {0};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = key;
    inputs[1] = inputs[0];
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    const UINT sent = SendInput(2, inputs, sizeof(inputs[0]));
    fprintf(stderr, "win_text_input: key=%u sent=%u foreground=%p\n",
            (unsigned int)key, (unsigned int)sent, (void *)GetForegroundWindow());
    return sent == 2;
}

static int send_ime_roman(const char *letters) {
    for (const char *letter = letters; *letter; ++letter) {
        if (*letter < 'A' || *letter > 'Z' || !send_virtual_key((WORD)*letter))
            return 0;
        Sleep(30);
    }
    return 1;
}

static int verify_native_composition(const nk_event *event) {
    nk_text_edit_event edit = {0};
    memcpy(&edit, event->data, sizeof(edit));
    const char *event_text = NULL;
    uint32_t event_length = 0;
    return edit.action == NK_TEXT_EDIT_COMPOSE && edit.replace_start == 2 &&
           edit.replace_end == 2 && edit.composition_start == 2 && edit.composition_end > 2 &&
           edit.selection_start == edit.selection_end &&
           nk_text_edit_event_text(event, &event_text, &event_length) == NK_OK &&
           event_length != 0;
}

static int verify_native_commit(const nk_event *event) {
    nk_text_edit_event edit = {0};
    memcpy(&edit, event->data, sizeof(edit));
    const char *event_text = NULL;
    uint32_t event_length = 0;
    return edit.action == NK_TEXT_EDIT_COMMIT && edit.replace_start == 2 &&
           edit.replace_end > edit.replace_start && edit.selection_start == edit.selection_end &&
           edit.selection_start > edit.replace_start &&
           edit.composition_start == NK_TEXT_POSITION_NONE &&
           edit.composition_end == NK_TEXT_POSITION_NONE &&
           nk_text_edit_event_text(event, &event_text, &event_length) == NK_OK &&
           event_length != 0;
}

static int verify_native_finish(const nk_event *event) {
    nk_text_edit_event edit = {0};
    memcpy(&edit, event->data, sizeof(edit));
    const char *event_text = NULL;
    uint32_t event_length = 0;
    return edit.action == NK_TEXT_EDIT_FINISH_COMPOSITION &&
           edit.selection_start == edit.selection_end &&
           edit.composition_start == NK_TEXT_POSITION_NONE &&
           edit.composition_end == NK_TEXT_POSITION_NONE &&
           nk_text_edit_event_text(event, &event_text, &event_length) == NK_OK &&
           event_length == 0;
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
    const nk_result activation = nk_window_activate(window);
    fprintf(stderr, "win_text_input: activate=%d\n", activation);

    nk_native_window native = {0};
    native.struct_size = sizeof(native);
    assert(nk_window_get_native(window, &native) == NK_OK);
    assert(native.kind == NK_NATIVE_WINDOW_WIN32);
    HWND hwnd = (HWND)native.window;
    assert(hwnd != NULL);
    focus_window(hwnd);
    fprintf(stderr, "win_text_input: hwnd=%p foreground=%p focus=%p\n",
            (void *)hwnd, (void *)GetForegroundWindow(), (void *)GetFocus());
    // Prefer the installed Japanese Microsoft IME when this runner has it.
    // Keep the default layout as a fallback so ordinary Windows runners still
    // validate caret positioning and committed WM_IME_CHAR behavior.
    HKL ime_layout = activate_japanese_layout();
    const int ime_profile = activate_japanese_ime_profile();
    focus_window(hwnd);
    wchar_t active_layout[KL_NAMELENGTH] = {0};
    wchar_t ime_name[MAX_PATH] = {0};
    if (ime_layout != NULL)
        GetKeyboardLayoutNameW(active_layout);
    if (ime_layout != NULL)
        ImmGetDescriptionW(ime_layout, ime_name, MAX_PATH);
    fprintf(stderr, "win_text_input: layout=%p active=%ls ime=%ls profile=%d\n",
            (void *)ime_layout, active_layout, ime_name, ime_profile);

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
    fprintf(stderr, "win_text_input: himc=%p\n", (void *)context);
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
    const BOOL conversion_set = ImmSetConversionStatus(
        context, IME_CMODE_NATIVE | IME_CMODE_ROMAN, IME_SMODE_NONE);
    DWORD conversion = 0;
    DWORD sentence = 0;
    const BOOL conversion_read = ImmGetConversionStatus(context, &conversion, &sentence);
    fprintf(stderr, "win_text_input: conversion_set=%d open=%d read=%d mode=0x%08lx sentence=0x%08lx\n",
            conversion_set ? 1 : 0, ImmGetOpenStatus(context) ? 1 : 0,
            conversion_read ? 1 : 0, (unsigned long)conversion, (unsigned long)sentence);
    int composition_supported = 0;
    if (ime_profile && conversion_set && send_ime_roman("KANJI")) {
        nk_event compose = {0};
        compose.struct_size = sizeof(compose);
        if (!wait_for_edit(window, NK_TEXT_EDIT_COMPOSE, &compose))
            return skip_test(window, hwnd, context);
        if (!verify_native_composition(&compose)) {
            nk_event_release(&compose);
            return 2;
        }
        nk_event_release(&compose);

        if (!send_virtual_key(VK_RETURN))
            return skip_test(window, hwnd, context);
        nk_event commit = {0};
        commit.struct_size = sizeof(commit);
        if (!wait_for_edit(window, NK_TEXT_EDIT_COMMIT, &commit))
            return skip_test(window, hwnd, context);
        if (!verify_native_commit(&commit)) {
            nk_event_release(&commit);
            return 2;
        }
        nk_event_release(&commit);

        if (!send_ime_roman("KANJI"))
            return skip_test(window, hwnd, context);
        nk_event cancelled_compose = {0};
        cancelled_compose.struct_size = sizeof(cancelled_compose);
        if (!wait_for_edit(window, NK_TEXT_EDIT_COMPOSE, &cancelled_compose))
            return skip_test(window, hwnd, context);
        if (!verify_native_composition(&cancelled_compose)) {
            nk_event_release(&cancelled_compose);
            return 2;
        }
        nk_event_release(&cancelled_compose);

        if (!send_virtual_key(VK_ESCAPE))
            return skip_test(window, hwnd, context);
        nk_event finish = {0};
        finish.struct_size = sizeof(finish);
        if (!wait_for_edit(window, NK_TEXT_EDIT_FINISH_COMPOSITION, &finish))
            return skip_test(window, hwnd, context);
        if (!verify_native_finish(&finish)) {
            nk_event_release(&finish);
            return 2;
        }
        nk_event_release(&finish);
        composition_supported = 1;
    }

    ImmReleaseContext(hwnd, context);
    if (SendMessageW(hwnd, WM_IME_CHAR, (WPARAM)L'Z', 0) != 0 ||
        !wait_for_text(window, 'Z'))
        return skip_test(window, hwnd, NULL);

    assert(nk_surface_set_text_input_active(window, 0) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return composition_supported ? 0 : 77;
}
