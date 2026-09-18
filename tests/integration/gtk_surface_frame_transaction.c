#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#include <assert.h>
#include <stddef.h>
#include <pthread.h>

static void pump_events(int iterations) {
    for (int iteration = 0; iteration < iterations; ++iteration) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        assert(nk_poll_event(&event) == NK_OK);
        nk_event_release(&event);
    }
}

static nk_surface_frame_target empty_target(void) {
    nk_surface_frame_target target = {0};
    target.struct_size = sizeof(target);
    return target;
}

struct frame_probe {
    nk_surface surface;
    nk_result result;
};

static void *acquire_on_worker(void *user_data) {
    struct frame_probe *probe = user_data;
    nk_surface_frame frame = NK_INVALID_HANDLE;
    nk_surface_frame_target target = empty_target();
    probe->result = nk_surface_acquire_frame(probe->surface, &frame, &target);
    return NULL;
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    assert(nk_init(&init) == NK_OK);

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 320;
    window_options.height = 240;
    window_options.title = "NativeKit frame transaction test";
    nk_window window = NK_INVALID_HANDLE;
    assert(nk_window_create(&window_options, &window) == NK_OK);

    nk_surface_options surface_options = {0};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.api = NK_GRAPHICS_OPENGL;
    surface_options.width = 320;
    surface_options.height = 240;
    nk_surface surface = NK_INVALID_HANDLE;
    assert(nk_surface_create(window, &surface_options, &surface) == NK_OK);
    nk_surface second = NK_INVALID_HANDLE;
    surface_options.x = 0;
    surface_options.y = 0;
    assert(nk_surface_create(window, &surface_options, &second) == NK_OK);
    /* Let GTK realize both GL areas before the manual frame path runs. */
    pump_events(20);

    nk_surface_frame_target target = empty_target();
    nk_surface_frame frame = NK_INVALID_HANDLE;
    assert(nk_surface_acquire_frame(surface, &frame, &target) == NK_OK);
    assert(frame != NK_INVALID_HANDLE);
    assert(target.frame == frame);
    assert(target.api == NK_GRAPHICS_OPENGL);
    assert(target.width > 0 && target.height > 0);

    /* A surface carries one open frame at a time. */
    nk_surface_frame_target pending = empty_target();
    nk_surface_frame second_frame = NK_INVALID_HANDLE;
    assert(nk_surface_acquire_frame(surface, &second_frame, &pending) == NK_ERROR_INVALID_REQUEST);
    assert(second_frame == NK_INVALID_HANDLE);

    assert(nk_surface_present_frame(frame) == NK_OK);
    /* Tokens are single use. */
    assert(nk_surface_present_frame(frame) == NK_ERROR_INVALID_HANDLE);
    assert(nk_surface_cancel_frame(frame) == NK_ERROR_INVALID_HANDLE);

    /* Cancelling closes the frame without presenting it. */
    nk_surface_frame_target cancelled_target = empty_target();
    nk_surface_frame cancelled = NK_INVALID_HANDLE;
    assert(nk_surface_acquire_frame(surface, &cancelled, &cancelled_target) == NK_OK);
    assert(nk_surface_cancel_frame(cancelled) == NK_OK);
    assert(nk_surface_cancel_frame(cancelled) == NK_ERROR_INVALID_HANDLE);
    assert(nk_surface_present_frame(cancelled) == NK_ERROR_INVALID_HANDLE);

    nk_surface_frame_target reused_target = empty_target();
    nk_surface_frame reused = NK_INVALID_HANDLE;
    assert(nk_surface_acquire_frame(surface, &reused, &reused_target) == NK_OK);
    assert(reused != NK_INVALID_HANDLE && reused != cancelled);
    assert(nk_surface_present_frame(reused) == NK_OK);

    /* Frames are tracked per surface. */
    nk_surface_frame_target second_target = empty_target();
    nk_surface_frame other_frame = NK_INVALID_HANDLE;
    assert(nk_surface_acquire_frame(second, &other_frame, &second_target) == NK_OK);
    assert(other_frame != reused);
    assert(nk_surface_acquire_frame(surface, &reused, &reused_target) == NK_OK);
    /* Closing the second surface frame leaves the first surface untouched. */
    assert(nk_surface_present_frame(other_frame) == NK_OK);
    assert(nk_surface_present_frame(reused) == NK_OK);

    /* Destroying a surface invalidates the frame it had open. */
    nk_surface_frame orphan = NK_INVALID_HANDLE;
    assert(nk_surface_acquire_frame(second, &orphan, &second_target) == NK_OK);
    assert(nk_surface_destroy(second) == NK_OK);
    assert(nk_surface_present_frame(orphan) == NK_ERROR_INVALID_HANDLE);
    assert(nk_surface_cancel_frame(orphan) == NK_ERROR_INVALID_HANDLE);
    /* The surviving surface still accepts frames afterwards. */
    assert(nk_surface_acquire_frame(surface, &reused, &reused_target) == NK_OK);
    assert(nk_surface_present_frame(reused) == NK_OK);

    /* Argument validation. */
    assert(nk_surface_acquire_frame(surface, NULL, &reused_target) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_surface_acquire_frame(NK_INVALID_HANDLE, &reused, &reused_target) ==
           NK_ERROR_INVALID_ARGUMENT);
    assert(nk_surface_acquire_frame(surface, &reused, NULL) == NK_ERROR_INVALID_ARGUMENT);
    nk_surface_frame_target truncated = empty_target();
    truncated.struct_size = (uint32_t)offsetof(nk_surface_frame_target, frame);
    assert(nk_surface_acquire_frame(surface, &reused, &truncated) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_surface_present_frame(NK_INVALID_HANDLE) == NK_ERROR_INVALID_ARGUMENT);
    assert(nk_surface_cancel_frame(NK_INVALID_HANDLE) == NK_ERROR_INVALID_ARGUMENT);

    /*
     * Acquisition belongs to the platform executor. Rendering the acquired
     * frame may move to the render executor (ADR 0017), so acquiring from a
     * non-platform thread is rejected instead of racing presentation.
     */
    {
        struct frame_probe probe = {surface, NK_OK};
        pthread_t worker;
        assert(pthread_create(&worker, NULL, acquire_on_worker, &probe) == 0);
        assert(pthread_join(worker, NULL) == 0);
        assert(probe.result == NK_ERROR_WRONG_THREAD);
        /* The platform executor still acquires afterwards. */
        assert(nk_surface_acquire_frame(surface, &reused, &reused_target) == NK_OK);
        assert(nk_surface_cancel_frame(reused) == NK_OK);
    }

    /* The legacy manual path keeps working next to the transaction path. */
    assert(nk_surface_make_current(surface) == NK_OK);
    nk_surface_frame_target legacy_target = empty_target();
    assert(nk_surface_get_frame_target(surface, &legacy_target) == NK_OK);
    assert(legacy_target.frame == NK_INVALID_HANDLE);
    assert(nk_surface_present(surface) == NK_OK);

    assert(nk_surface_destroy(surface) == NK_OK);
    assert(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
