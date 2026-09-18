#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#include <assert.h>
#include <unistd.h>

typedef struct frame_state {
    int count;
    int request_next;
    nk_surface surface;
} frame_state;

static void NK_CALL on_frame(nk_surface surface, int32_t width, int32_t height, void *user_data) {
    frame_state *state = user_data;
    assert(surface != NK_INVALID_HANDLE && width > 0 && height > 0);
    ++state->count;
    if (state->request_next)
        assert(nk_surface_request_frame(surface) == NK_OK);
}

static void pump_frames(frame_state *state, int iterations) {
    for (int iteration = 0; iteration < iterations; ++iteration) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        nk_event_release(&event);
        usleep(2000);
    }
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
    window_options.title = "NativeKit request-frame test";
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);

    nk_surface_options surface_options = {0};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.api = NK_GRAPHICS_OPENGL;
    surface_options.width = 320;
    surface_options.height = 240;
    nk_surface surface = NK_INVALID_HANDLE;
    assert(nk_surface_create(window, &surface_options, &surface) == NK_OK);

    frame_state state = {0};
    state.surface = surface;
    assert(nk_surface_set_frame_callback(surface, on_frame, &state) == NK_OK);
    /* Continuous scheduling stays the default for game-loop callers. */
    pump_until_frames(&state, 1);
    pump_until_frames(&state, 3);

    assert(nk_surface_set_frame_mode(surface, NK_SURFACE_FRAME_ON_DEMAND) == NK_OK);
    /* Let the in-flight continuous frame settle, then verify the surface is idle. */
    pump_frames(&state, 40);
    const int idle_count = state.count;
    pump_frames(&state, 80);
    assert(state.count == idle_count);

    /* One request produces exactly one frame. */
    assert(nk_surface_request_frame(surface) == NK_OK);
    pump_until_frames(&state, idle_count + 1);
    const int requested_count = state.count;
    pump_frames(&state, 80);
    assert(state.count == requested_count);

    /* Requests made before the frame coalesce. */
    assert(nk_surface_request_frame(surface) == NK_OK);
    assert(nk_surface_request_frame(surface) == NK_OK);
    assert(nk_surface_request_frame(surface) == NK_OK);
    pump_until_frames(&state, requested_count + 1);
    const int coalesced_count = state.count;
    pump_frames(&state, 80);
    assert(state.count == coalesced_count);

    /* A frame that requests its successor keeps an animation running. */
    state.request_next = 1;
    assert(nk_surface_request_frame(surface) == NK_OK);
    pump_until_frames(&state, coalesced_count + 3);
    state.request_next = 0;
    pump_frames(&state, 80);
    const int settled_count = state.count;
    pump_frames(&state, 80);
    assert(state.count == settled_count);

    /* Continuous scheduling resumes when the application asks for it. */
    assert(nk_surface_set_frame_mode(surface, NK_SURFACE_FRAME_CONTINUOUS) == NK_OK);
    pump_until_frames(&state, settled_count + 2);

    assert(nk_surface_request_frame(surface) == NK_OK);
    assert(nk_surface_set_frame_mode(surface, (nk_surface_frame_mode)7) ==
           NK_ERROR_INVALID_ARGUMENT);
    assert(nk_surface_request_frame(NK_INVALID_HANDLE) == NK_ERROR_INVALID_HANDLE);
    assert(nk_surface_set_frame_mode(NK_INVALID_HANDLE, NK_SURFACE_FRAME_ON_DEMAND) ==
           NK_ERROR_INVALID_HANDLE);

    assert(nk_surface_set_frame_callback(surface, NULL, NULL) == NK_OK);
    assert(nk_surface_destroy(surface) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
