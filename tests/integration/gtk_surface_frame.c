#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#include <assert.h>
#include <unistd.h>

typedef struct frame_state {
    int count;
    int32_t width;
    int32_t height;
} frame_state;

static void NK_CALL on_frame(nk_handle surface, int32_t width, int32_t height, void *user_data) {
    frame_state *state = user_data;
    assert(surface != NK_INVALID_HANDLE && width > 0 && height > 0);
    ++state->count;
    state->width = width;
    state->height = height;
}

static void pump_until_frames(frame_state *state, int expected) {
    for (int attempt = 0; attempt < 500 && state->count < expected; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        nk_event_release(&event);
        usleep(1000);
    }
    assert(state->count >= expected);
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);
    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.flags = NK_WINDOW_RESIZABLE;
    window_options.width = 320;
    window_options.height = 240;
    window_options.title = "NativeKit frame callback test";
    nk_handle window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);
    nk_surface_options surface_options = {0};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.api = NK_GRAPHICS_OPENGL;
    surface_options.width = 320;
    surface_options.height = 240;
    nk_handle surface = NK_INVALID_HANDLE;
    assert(nk_surface_create(window, &surface_options, &surface) == NK_OK);
    frame_state state = {0};
    assert(nk_surface_set_frame_callback(surface, on_frame, &state) == NK_OK);
    assert(nk_surface_present(surface) == NK_OK);
    pump_until_frames(&state, 1);
    const int first_count = state.count;
    assert(nk_window_set_bounds(window, 0, 0, 480, 320) == NK_OK);
    assert(nk_surface_set_bounds(surface, 0, 0, 480, 320) == NK_OK);
    assert(nk_surface_present(surface) == NK_OK);
    pump_until_frames(&state, first_count + 1);
    assert(state.width > 0 && state.height > 0);
    assert(nk_surface_set_frame_callback(surface, NULL, NULL) == NK_OK);
    assert(nk_surface_destroy(surface) == NK_OK);
    assert(nk_surface_set_frame_callback(surface, on_frame, &state) == NK_ERROR_INVALID_HANDLE);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
