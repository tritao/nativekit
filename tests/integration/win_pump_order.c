/* Posts key messages and a frame request to the window's own queue (no synthesized hardware
 * input) and checks what nk_poll_event delivers: characters stay right behind the key that
 * produced them, and a requested frame is held back for unpolled events at most once. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            fprintf(stderr, "win_pump_order: line %d: %s\n", __LINE__, #condition); \
            exit(1);                                                              \
        }                                                                         \
    } while (0)

static int frame_count = 0;

static void NK_CALL on_frame(nk_surface surface, int32_t width, int32_t height, void *user_data) {
    (void)surface;
    (void)width;
    (void)height;
    (void)user_data;
    ++frame_count;
}

static void poll_once(nk_event_kind *kinds, int *count, int capacity) {
    nk_event event = {0};
    event.struct_size = sizeof(event);
    CHECK(nk_poll_event(&event) == NK_OK);
    if ((event.kind == NK_EVENT_KEY || event.kind == NK_EVENT_TEXT_INPUT) && *count < capacity)
        kinds[(*count)++] = event.kind;
    nk_event_release(&event);
}

static void post_key(HWND hwnd, WPARAM vk, WPARAM scan) {
    PostMessageW(hwnd, WM_KEYDOWN, vk, (LPARAM)(1 | (scan << 16)));
}

int main(void) {
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    CHECK(nk_init(&init) == NK_OK);

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 320;
    window_options.height = 240;
    window_options.title = "NativeKit pump order test";
    fprintf(stderr, "init ok\n");
    nk_window window = NK_INVALID_HANDLE;
    CHECK(nk_window_create(&window_options, &window) == NK_OK);

    nk_surface_options surface_options = {0};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.api = NK_GRAPHICS_D3D11;
    surface_options.width = 160;
    surface_options.height = 120;
    fprintf(stderr, "window ok\n");
    nk_surface surface = NK_INVALID_HANDLE;
    CHECK(nk_surface_create(window, &surface_options, &surface) == NK_OK);
    CHECK(nk_surface_set_frame_callback(surface, on_frame, NULL) == NK_OK);
    CHECK(nk_surface_set_frame_mode(surface, NK_SURFACE_FRAME_ON_DEMAND) == NK_OK);
    CHECK(nk_window_activate(window) == NK_OK);

    nk_native_window native = {0};
    native.struct_size = sizeof(native);
    CHECK(nk_window_get_native(window, &native) == NK_OK);
    HWND hwnd = (HWND)native.window;
    CHECK(hwnd != NULL);

    nk_event_kind kinds[16];
    int count = 0;
    // Wait for the surface to produce a first frame so later frames are plain requests.
    for (int attempt = 0; attempt < 500 && frame_count == 0; ++attempt) {
        CHECK(nk_surface_request_frame(surface) == NK_OK);
        poll_once(kinds, &count, 16);
        Sleep(5);
    }
    CHECK(frame_count > 0);
    for (int i = 0; i < 20; ++i)
        poll_once(kinds, &count, 16);

    // 'A' then Backspace: the character of 'A' must arrive before Backspace.
    count = 0;
    post_key(hwnd, 'A', 0x1e);
    post_key(hwnd, VK_BACK, 0x0e);
    for (int i = 0; i < 50 && count < 3; ++i) {
        poll_once(kinds, &count, 16);
        Sleep(1);
    }
    fprintf(stderr, "win_pump_order: kinds=%d [%u %u %u]\n", count, count > 0 ? kinds[0] : 0,
            count > 1 ? kinds[1] : 0, count > 2 ? kinds[2] : 0);
    CHECK(count >= 3);
    CHECK(kinds[0] == NK_EVENT_KEY);
    CHECK(kinds[1] == NK_EVENT_TEXT_INPUT);
    CHECK(kinds[2] == NK_EVENT_KEY);

    // Events keep arriving, yet a requested frame renders on the second pump at the latest.
    for (int i = 0; i < 20; ++i)
        poll_once(kinds, &count, 16);
    const int before = frame_count;
    CHECK(nk_surface_request_frame(surface) == NK_OK);
    for (int poll = 0; poll < 3; ++poll) {
        for (int i = 0; i < 3; ++i)
            post_key(hwnd, VK_F13, 0x64);
        count = 0;
        poll_once(kinds, &count, 16);
    }
    CHECK(frame_count > before);

    CHECK(nk_surface_set_frame_callback(surface, NULL, NULL) == NK_OK);
    CHECK(nk_window_destroy(window) == NK_OK);
    nk_shutdown();
    return 0;
}
