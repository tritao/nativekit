#include "nativekit.h"
#include "nativekit_gpu.h"
#include "nativekit_window.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

struct TestResources {
    nkgpu_renderer renderer{};
    nkgpu_timestamp first_timestamp{};
    nkgpu_timestamp second_timestamp{};
    nk_surface surface = 0;
    nk_window window = 0;
    bool initialized = false;

    ~TestResources() {
        if (first_timestamp.id)
            nkgpu_timestamp_destroy(renderer, first_timestamp);
        if (second_timestamp.id)
            nkgpu_timestamp_destroy(renderer, second_timestamp);
        if (renderer.id)
            nkgpu_renderer_destroy(renderer);
        if (surface)
            nk_surface_destroy(surface);
        if (window)
            nk_window_destroy(window);
        if (initialized)
            nk_shutdown();
    }
};

bool expect_result(nkgpu_result actual, nkgpu_result expected, const char *expression) {
    if (actual == expected)
        return true;
    std::fprintf(stderr, "%s returned %d, expected %d: %s\n", expression, actual, expected,
                 nkgpu_last_error());
    return false;
}

bool wait_for_surface(nk_surface surface) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        nk_event event{};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK)
            return false;
        const bool ready = event.kind == NK_EVENT_SURFACE_READY && event.source == surface;
        nk_event_release(&event);
        if (ready)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

bool collect_ready(TestResources &resources) {
    const nkgpu_timestamp timestamps[] = {resources.first_timestamp,
                                          resources.second_timestamp};
    nkgpu_timestamp_result results[2]{};
    for (auto &result : results)
        result.struct_size = sizeof(result);

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!expect_result(nkgpu_timestamp_collect(resources.renderer, timestamps, 2, results),
                           NKGPU_OK, "nkgpu_timestamp_collect"))
            return false;
        if (results[0].state == NKGPU_TIMESTAMP_FAILED ||
            results[1].state == NKGPU_TIMESTAMP_FAILED)
            return false;
        if (results[0].state == NKGPU_TIMESTAMP_READY &&
            results[1].state == NKGPU_TIMESTAMP_READY)
            break;
        std::this_thread::yield();
    }

    if (results[0].state != NKGPU_TIMESTAMP_READY ||
        results[1].state != NKGPU_TIMESTAMP_READY) {
        std::fprintf(stderr, "GPU timestamps did not become ready\n");
        return false;
    }

    nkgpu_timestamp_info info{};
    info.struct_size = sizeof(info);
    if (!expect_result(nkgpu_timestamp_query(resources.renderer, resources.first_timestamp, &info),
                       NKGPU_OK, "nkgpu_timestamp_query"))
        return false;
    return info.state == NKGPU_TIMESTAMP_READY;
}

bool draw_frames(TestResources &resources, uint32_t width, uint32_t height, int count) {
    for (int frame = 0; frame < count; ++frame) {
        if (!expect_result(nkgpu_frame_begin(resources.renderer), NKGPU_OK,
                           "nkgpu_frame_begin(timed)") ||
            !expect_result(nkgpu_begin_window_pass(resources.renderer, width, height, 1), NKGPU_OK,
                           "nkgpu_begin_window_pass(timed)") ||
            !expect_result(nkgpu_end_pass(resources.renderer), NKGPU_OK, "nkgpu_end_pass(timed)") ||
            !expect_result(nkgpu_end_frame(resources.renderer), NKGPU_OK, "nkgpu_end_frame(timed)"))
            return false;
    }
    return true;
}

bool read_frame_timing(TestResources &resources, nkgpu_frame_timing &timing) {
    timing = {};
    timing.struct_size = sizeof(timing);
    return expect_result(nkgpu_get_frame_timing(resources.renderer, &timing), NKGPU_OK,
                         "nkgpu_get_frame_timing");
}

/* Explicit timestamps nest inside each other and inside a timed frame. */
bool check_nested_timestamps(TestResources &resources, uint32_t width, uint32_t height) {
    if (!expect_result(nkgpu_frame_begin(resources.renderer), NKGPU_OK, "nkgpu_frame_begin(nested)") ||
        !expect_result(nkgpu_begin_window_pass(resources.renderer, width, height, 1), NKGPU_OK,
                       "nkgpu_begin_window_pass(nested)") ||
        !expect_result(nkgpu_timestamp_begin(resources.renderer, &resources.first_timestamp),
                       NKGPU_OK, "nkgpu_timestamp_begin(outer)") ||
        !expect_result(nkgpu_timestamp_begin(resources.renderer, &resources.second_timestamp),
                       NKGPU_OK, "nkgpu_timestamp_begin(inner)") ||
        !expect_result(nkgpu_timestamp_end(resources.renderer, resources.second_timestamp),
                       NKGPU_OK, "nkgpu_timestamp_end(inner)") ||
        !expect_result(nkgpu_timestamp_end(resources.renderer, resources.first_timestamp),
                       NKGPU_OK, "nkgpu_timestamp_end(outer)") ||
        !expect_result(nkgpu_end_pass(resources.renderer), NKGPU_OK, "nkgpu_end_pass(nested)") ||
        !expect_result(nkgpu_end_frame(resources.renderer), NKGPU_OK, "nkgpu_end_frame(nested)") ||
        !collect_ready(resources))
        return false;
    nkgpu_timestamp_info outer{}, inner{};
    outer.struct_size = sizeof(outer);
    inner.struct_size = sizeof(inner);
    if (!expect_result(nkgpu_timestamp_query(resources.renderer, resources.first_timestamp, &outer),
                       NKGPU_OK, "nkgpu_timestamp_query(outer)") ||
        !expect_result(nkgpu_timestamp_query(resources.renderer, resources.second_timestamp, &inner),
                       NKGPU_OK, "nkgpu_timestamp_query(inner)"))
        return false;
    if (inner.nanoseconds > outer.nanoseconds) {
        std::fprintf(stderr, "inner timestamp outlasted the outer one\n");
        return false;
    }
    nkgpu_timestamp_destroy(resources.renderer, resources.first_timestamp);
    nkgpu_timestamp_destroy(resources.renderer, resources.second_timestamp);
    resources.first_timestamp = {};
    resources.second_timestamp = {};
    return true;
}

/* Timed frames report results a few frames late; once timing is off, results stop after the
   frames already in flight. */
bool check_frame_timing(TestResources &resources, uint32_t width, uint32_t height) {
    nkgpu_frame_timing timing{};
    if (!read_frame_timing(resources, timing) || timing.timed_frames != 0) {
        std::fprintf(stderr, "frames were timed before timing was enabled\n");
        return false;
    }
    if (!expect_result(nkgpu_set_frame_timing(resources.renderer, 2),
                       NKGPU_ERROR_INVALID_ARGUMENT, "nkgpu_set_frame_timing(2)") ||
        !expect_result(nkgpu_set_frame_timing(resources.renderer, 1), NKGPU_OK,
                       "nkgpu_set_frame_timing(1)"))
        return false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (timing.timed_frames < 3 && std::chrono::steady_clock::now() < deadline) {
        if (!draw_frames(resources, width, height, 1) || !read_frame_timing(resources, timing))
            return false;
    }
    if (!check_nested_timestamps(resources, width, height) || !read_frame_timing(resources, timing))
        return false;
    if (timing.timed_frames < 3) {
        std::fprintf(stderr, "only %llu frames were timed\n",
                     static_cast<unsigned long long>(timing.timed_frames));
        return false;
    }
    if (!expect_result(nkgpu_set_frame_timing(resources.renderer, 0), NKGPU_OK,
                       "nkgpu_set_frame_timing(0)"))
        return false;
    /* The first untimed frames take what was still in flight; later ones add nothing. */
    nkgpu_frame_timing drained{}, after{};
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (!draw_frames(resources, width, height, 2) || !read_frame_timing(resources, drained) ||
        !draw_frames(resources, width, height, 8) || !read_frame_timing(resources, after))
        return false;
    if (after.timed_frames != drained.timed_frames) {
        std::fprintf(stderr, "frames were still timed after timing was disabled\n");
        return false;
    }
    std::printf("frame timing: %llu frames, last %llu ns\n",
                static_cast<unsigned long long>(after.timed_frames),
                static_cast<unsigned long long>(after.last_frame_nanoseconds));
    return true;
}

} // namespace

int main() {
    TestResources resources;

    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK)
        return 1;
    resources.initialized = true;

    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 64;
    window_options.height = 64;
    window_options.title = "NativeKit GPU timestamp smoke";
    if (nk_window_create(&window_options, &resources.window) != NK_OK ||
        nkgpu_surface_create(resources.window, window_options.width, window_options.height,
                             &resources.surface) != NKGPU_OK ||
        !wait_for_surface(resources.surface) ||
        !expect_result(nkgpu_renderer_create(resources.surface, &resources.renderer), NKGPU_OK,
                       "nkgpu_renderer_create"))
        return 1;

    nkgpu_features features{};
    features.struct_size = sizeof(features);
    if (!expect_result(nkgpu_query_features(resources.renderer, &features), NKGPU_OK,
                       "nkgpu_query_features"))
        return 1;
    if (!features.timestamps) {
        std::fprintf(stderr, "GPU timestamps are unavailable; skipping\n");
        return 0;
    }

    if (!expect_result(nkgpu_frame_begin(resources.renderer), NKGPU_OK,
                       "nkgpu_frame_begin(timestamp)") ||
        !expect_result(nkgpu_begin_window_pass(resources.renderer, window_options.width,
                                               window_options.height, 1),
                       NKGPU_OK, "nkgpu_begin_window_pass(timestamp)"))
        return 1;

    nkgpu_timestamp_desc first_desc{};
    first_desc.struct_size = sizeof(first_desc);
    first_desc.label = "scene";
    if (!expect_result(nkgpu_timestamp_begin_desc(resources.renderer, &first_desc,
                                                  &resources.first_timestamp),
                       NKGPU_OK, "nkgpu_timestamp_begin_desc") ||
        !expect_result(nkgpu_timestamp_end(resources.renderer, resources.first_timestamp),
                       NKGPU_OK, "nkgpu_timestamp_end(first)"))
        return 1;

    nkgpu_timestamp_desc second_desc{};
    second_desc.struct_size = sizeof(second_desc);
    second_desc.label = "ui";
    if (!expect_result(nkgpu_timestamp_begin_desc(resources.renderer, &second_desc,
                                                  &resources.second_timestamp),
                       NKGPU_OK, "nkgpu_timestamp_begin_desc(second)") ||
        !expect_result(nkgpu_timestamp_end(resources.renderer, resources.second_timestamp),
                       NKGPU_OK, "nkgpu_timestamp_end(second)"))
        return 1;
    if (!expect_result(nkgpu_timestamp_end(resources.renderer, resources.first_timestamp),
                       NKGPU_ERROR_WRONG_STATE, "nkgpu_timestamp_end(repeated)"))
        return 1;
    if (!expect_result(nkgpu_end_pass(resources.renderer), NKGPU_OK,
                       "nkgpu_end_pass(timestamp)") ||
        !expect_result(nkgpu_end_frame(resources.renderer), NKGPU_OK,
                        "nkgpu_end_frame(timestamp)"))
        return 1;

    const char *first_label =
        nkgpu_timestamp_get_label(resources.renderer, resources.first_timestamp);
    const char *second_label =
        nkgpu_timestamp_get_label(resources.renderer, resources.second_timestamp);
    if (!first_label || std::strcmp(first_label, "scene") != 0 || !second_label ||
        std::strcmp(second_label, "ui") != 0) {
        std::fprintf(stderr, "timestamp labels were not retained\n");
        return 1;
    }
    if (!collect_ready(resources))
        return 1;

    if (!expect_result(nkgpu_timestamp_destroy(resources.renderer, resources.first_timestamp),
                       NKGPU_OK, "nkgpu_timestamp_destroy(first)"))
        return 1;
    resources.first_timestamp = {};
    if (!expect_result(nkgpu_timestamp_destroy(resources.renderer, resources.second_timestamp),
                       NKGPU_OK, "nkgpu_timestamp_destroy(second)"))
        return 1;
    resources.second_timestamp = {};

    nkgpu_timestamp_info stale_info{};
    stale_info.struct_size = sizeof(stale_info);
    if (!expect_result(nkgpu_timestamp_query(resources.renderer, resources.first_timestamp,
                                             &stale_info),
                       NKGPU_ERROR_INVALID_HANDLE, "nkgpu_timestamp_query(stale)"))
        return 1;
    return check_frame_timing(resources, window_options.width, window_options.height) ? 0 : 1;
}
