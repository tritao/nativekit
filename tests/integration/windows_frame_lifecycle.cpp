#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_input.h"
#include "nativekit_window.h"
#include <windows.h>
#include <cassert>

struct State {
    int callbacks = 0;
    nk_surface_frame held = NK_INVALID_HANDLE;
};

static void NK_CALL on_frame(nk_surface surface, int32_t, int32_t, void *data) {
    auto &state = *static_cast<State *>(data);
    ++state.callbacks;
    nk_surface_frame_target target{};
    target.struct_size = sizeof(target);
    nk_surface_frame frame = NK_INVALID_HANDLE;
    assert(nk_surface_acquire_frame(surface, &frame, &target) == NK_OK);
    if (state.callbacks == 1) {
        state.held = frame;
        assert(nk_surface_request_frame(surface) == NK_OK);
    } else {
        assert(state.callbacks == 2);
        assert(nk_surface_cancel_frame(frame) == NK_OK);
    }
}

static void pump_for(DWORD milliseconds) {
    const auto end = GetTickCount64() + milliseconds;
    do {
        nk_event event{};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        nk_event_release(&event);
        Sleep(1);
    } while (GetTickCount64() < end);
}

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    nk_window_options options{};
    options.struct_size = sizeof(options);
    options.width = 160;
    options.height = 120;
    options.title = "NativeKit frame lifecycle regression";
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&options, &window) == NK_OK);
    nk_surface_options graphics{};
    graphics.struct_size = sizeof(graphics);
    graphics.api = NK_GRAPHICS_D3D11;
    graphics.width = 160;
    graphics.height = 120;
    nk_surface surface = NK_INVALID_HANDLE;
    assert(nk_surface_create(window, &graphics, &surface) == NK_OK);
    // UIKit activates text editing through the graphics surface, not its parent.
    assert(nk_surface_set_text_input_active(surface, 1) == NK_OK);
    nk_text_input_state text{};
    text.struct_size = sizeof(text);
    text.text = "";
    text.composition_start = NK_TEXT_POSITION_NONE;
    text.composition_end = NK_TEXT_POSITION_NONE;
    assert(nk_surface_set_text_input_state(surface, &text) == NK_OK);
    assert(nk_surface_set_text_input_geometry(surface, 0, 0, NK_TEXT_POSITION_NONE,
                                            NK_TEXT_POSITION_NONE, nullptr, 0, nullptr, 0) == NK_OK);
    assert(nk_surface_set_text_input_active(surface, 0) == NK_OK);
    assert(nk_surface_set_text_input_active(window, 1) == NK_OK);
    assert(nk_surface_set_text_input_active(window, 0) == NK_OK);
    State state;
    assert(nk_surface_set_frame_mode(surface, NK_SURFACE_FRAME_ON_DEMAND) == NK_OK);
    assert(nk_surface_set_frame_callback(surface, on_frame, &state) == NK_OK);
    assert(nk_surface_request_frame(surface) == NK_OK);
    nk_event first_event{};
    first_event.struct_size = sizeof(first_event);
    assert(nk_poll_event(&first_event) == NK_OK);
    nk_event_release(&first_event);
    // An on-demand request must dispatch without waiting for the 16 ms timer.
    assert(state.callbacks == 1);
    pump_for(150);
    // A pending successor must not reenter the callback or present the held frame.
    assert(state.callbacks == 1 && state.held != NK_INVALID_HANDLE);
    assert(nk_surface_cancel_frame(state.held) == NK_OK);
    pump_for(150);
    assert(state.callbacks == 2);
    pump_for(50);
    assert(state.callbacks == 2);
    assert(nk_surface_set_frame_callback(surface, nullptr, nullptr) == NK_OK);
    assert(nk_surface_destroy(surface) == NK_OK);
    assert(nk_surface_set_text_input_active(surface, 1) == NK_ERROR_INVALID_HANDLE);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
}
