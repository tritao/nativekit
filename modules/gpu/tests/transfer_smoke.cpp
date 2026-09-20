#include "nativekit.h"
#include "nativekit_gpu.h"
#include "nativekit_window.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

struct TestResources {
    nkgpu_renderer renderer{};
    nkgpu_buffer source_buffer{};
    nkgpu_buffer copied_buffer{};
    nkgpu_buffer round_trip_buffer{};
    nkgpu_image source_image{};
    nkgpu_image copied_image{};
    nkgpu_image round_trip_image{};
    nkgpu_image array_image{};
    nkgpu_readback readback{};
    nk_surface surface = 0;
    nk_window window = 0;
    bool initialized = false;

    ~TestResources() {
        if (readback.id)
            nkgpu_readback_destroy(renderer, readback);
        if (array_image.id)
            nkgpu_image_destroy(renderer, array_image);
        if (round_trip_image.id)
            nkgpu_image_destroy(renderer, round_trip_image);
        if (copied_image.id)
            nkgpu_image_destroy(renderer, copied_image);
        if (source_image.id)
            nkgpu_image_destroy(renderer, source_image);
        if (round_trip_buffer.id)
            nkgpu_buffer_destroy(renderer, round_trip_buffer);
        if (copied_buffer.id)
            nkgpu_buffer_destroy(renderer, copied_buffer);
        if (source_buffer.id)
            nkgpu_buffer_destroy(renderer, source_buffer);
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

bool readback(nkgpu_renderer renderer, nkgpu_image image, uint32_t x, uint32_t y, uint32_t width,
              uint32_t height, uint8_t *destination, uint32_t destination_size) {
    nkgpu_image_readback_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.image = image;
    desc.x = x;
    desc.y = y;
    desc.width = width;
    desc.height = height;

    nkgpu_readback request{};
    if (!expect_result(nkgpu_readback_begin_image(renderer, &desc, &request), NKGPU_OK,
                       "nkgpu_readback_begin_image"))
        return false;

    nkgpu_readback_info info{};
    info.struct_size = sizeof(info);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!expect_result(nkgpu_readback_query(renderer, request, &info), NKGPU_OK,
                           "nkgpu_readback_query")) {
            nkgpu_readback_destroy(renderer, request);
            return false;
        }
        if (info.state != NKGPU_READBACK_PENDING)
            break;
        std::this_thread::yield();
    }

    bool success = info.state == NKGPU_READBACK_READY && info.size <= destination_size;
    uint32_t actual_size = 0;
    if (success && !expect_result(nkgpu_readback_read(renderer, request, destination,
                                                      destination_size, &actual_size),
                                  NKGPU_OK, "nkgpu_readback_read"))
        success = false;
    if (success && actual_size != info.size)
        success = false;
    if (!success) {
        std::fprintf(stderr, "readback did not become ready or returned an invalid size\n");
    }
    if (!expect_result(nkgpu_readback_destroy(renderer, request), NKGPU_OK,
                       "nkgpu_readback_destroy"))
        success = false;
    return success;
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
    window_options.title = "NativeKit GPU transfer smoke";
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
                       "nkgpu_query_features") ||
        !features.buffer_copy || !features.image_copy || !features.image_readback) {
        std::fprintf(stderr, "the selected GPU backend does not expose transfer operations\n");
        return 1;
    }

    const uint32_t pixels[] = {1u, 2u, 3u, 4u};
    uint8_t source_data[32]{};
    const uint8_t initial_data[] = {0xaa, 0xbb, 0xcc, 0xdd};
    std::memcpy(source_data, initial_data, sizeof(initial_data));
    nkgpu_buffer_desc source_desc{};
    source_desc.struct_size = sizeof(source_desc);
    source_desc.size = sizeof(source_data);
    source_desc.usage = NKGPU_BUFFER_TRANSFER;
    source_desc.data = source_data;
    source_desc.data_size = sizeof(source_data);
    source_desc.dynamic_update = 1;
    if (!expect_result(
            nkgpu_buffer_create_desc(resources.renderer, &source_desc, &resources.source_buffer),
            NKGPU_OK, "nkgpu_buffer_create_desc(source)"))
        return 1;
    if (!expect_result(nkgpu_buffer_update(resources.renderer, resources.source_buffer, 8,
                                           reinterpret_cast<const uint8_t *>(pixels),
                                           sizeof(pixels)),
                       NKGPU_OK, "nkgpu_buffer_update"))
        return 1;

    nkgpu_buffer_desc copied_desc{};
    copied_desc.struct_size = sizeof(copied_desc);
    copied_desc.size = sizeof(source_data);
    copied_desc.usage = NKGPU_BUFFER_TRANSFER;
    if (!expect_result(
            nkgpu_buffer_create_desc(resources.renderer, &copied_desc, &resources.copied_buffer),
            NKGPU_OK, "nkgpu_buffer_create_desc(copy destination)"))
        return 1;

    nkgpu_buffer_copy_desc buffer_copy{};
    buffer_copy.struct_size = sizeof(buffer_copy);
    buffer_copy.source = resources.source_buffer;
    buffer_copy.source_offset = 8;
    buffer_copy.destination = resources.copied_buffer;
    buffer_copy.destination_offset = 4;
    buffer_copy.size = sizeof(pixels);
    if (!expect_result(nkgpu_buffer_copy(resources.renderer, &buffer_copy), NKGPU_OK,
                       "nkgpu_buffer_copy"))
        return 1;
    nkgpu_buffer_copy_desc invalid_buffer_copy = buffer_copy;
    invalid_buffer_copy.size = sizeof(source_data);
    if (!expect_result(nkgpu_buffer_copy(resources.renderer, &invalid_buffer_copy),
                       NKGPU_ERROR_INVALID_ARGUMENT, "nkgpu_buffer_copy(out of range)"))
        return 1;

    nkgpu_image_desc image_desc{};
    image_desc.struct_size = sizeof(image_desc);
    image_desc.width = 2;
    image_desc.height = 2;
    image_desc.format = NKGPU_IMAGEFORMAT_R32_UINT;
    image_desc.usage = NKGPU_IMAGE_SAMPLED | NKGPU_IMAGE_RENDER_TARGET;
    if (!expect_result(
            nkgpu_image_create_desc(resources.renderer, &image_desc, &resources.source_image),
            NKGPU_OK, "nkgpu_image_create_desc(source)"))
        return 1;
    if (!expect_result(
            nkgpu_image_create_desc(resources.renderer, &image_desc, &resources.copied_image),
            NKGPU_OK, "nkgpu_image_create_desc(copy destination)"))
        return 1;

    nkgpu_buffer_image_copy_desc buffer_to_image{};
    buffer_to_image.struct_size = sizeof(buffer_to_image);
    buffer_to_image.buffer = resources.copied_buffer;
    buffer_to_image.buffer_offset = 4;
    buffer_to_image.image = resources.source_image;
    buffer_to_image.width = 2;
    buffer_to_image.height = 2;
    if (!expect_result(nkgpu_buffer_to_image(resources.renderer, &buffer_to_image), NKGPU_OK,
                       "nkgpu_buffer_to_image"))
        return 1;

    uint8_t source_readback_bytes[sizeof(pixels)]{};
    if (!readback(resources.renderer, resources.source_image, 0, 0, 2, 2, source_readback_bytes,
                  sizeof(source_readback_bytes)))
        return 1;
    if (std::memcmp(source_readback_bytes, pixels, sizeof(pixels)) != 0) {
        const auto *actual_pixels = reinterpret_cast<const uint32_t *>(source_readback_bytes);
        std::fprintf(stderr, "source image after buffer upload: %u, %u, %u, %u\n", actual_pixels[0],
                     actual_pixels[1], actual_pixels[2], actual_pixels[3]);
        return 1;
    }

    nkgpu_image_copy_desc image_copy{};
    image_copy.struct_size = sizeof(image_copy);
    image_copy.source = resources.source_image;
    image_copy.destination = resources.copied_image;
    image_copy.width = 2;
    image_copy.height = 2;
    if (!expect_result(nkgpu_image_copy(resources.renderer, &image_copy), NKGPU_OK,
                       "nkgpu_image_copy"))
        return 1;
    nkgpu_image_copy_desc invalid_image_copy = image_copy;
    invalid_image_copy.width = 3;
    if (!expect_result(nkgpu_image_copy(resources.renderer, &invalid_image_copy),
                       NKGPU_ERROR_INVALID_ARGUMENT, "nkgpu_image_copy(out of range)"))
        return 1;

    uint8_t copied_readback_bytes[sizeof(pixels)]{};
    if (!readback(resources.renderer, resources.copied_image, 0, 0, 2, 2, copied_readback_bytes,
                  sizeof(copied_readback_bytes)))
        return 1;
    if (std::memcmp(copied_readback_bytes, pixels, sizeof(pixels)) != 0) {
        const auto *actual_pixels = reinterpret_cast<const uint32_t *>(copied_readback_bytes);
        std::fprintf(stderr, "image after image copy: %u, %u, %u, %u\n", actual_pixels[0],
                     actual_pixels[1], actual_pixels[2], actual_pixels[3]);
        return 1;
    }

    nkgpu_buffer_desc round_trip_desc{};
    round_trip_desc.struct_size = sizeof(round_trip_desc);
    round_trip_desc.size = sizeof(pixels);
    round_trip_desc.usage = NKGPU_BUFFER_TRANSFER;
    if (!expect_result(nkgpu_buffer_create_desc(resources.renderer, &round_trip_desc,
                                                &resources.round_trip_buffer),
                       NKGPU_OK, "nkgpu_buffer_create_desc(round trip)"))
        return 1;

    nkgpu_buffer_image_copy_desc image_to_buffer = buffer_to_image;
    image_to_buffer.buffer = resources.round_trip_buffer;
    image_to_buffer.buffer_offset = 0;
    image_to_buffer.image = resources.copied_image;
    if (!expect_result(nkgpu_image_to_buffer(resources.renderer, &image_to_buffer), NKGPU_OK,
                       "nkgpu_image_to_buffer"))
        return 1;
    nkgpu_buffer_image_copy_desc invalid_image_to_buffer = image_to_buffer;
    invalid_image_to_buffer.x = 2;
    if (!expect_result(nkgpu_image_to_buffer(resources.renderer, &invalid_image_to_buffer),
                       NKGPU_ERROR_INVALID_ARGUMENT, "nkgpu_image_to_buffer(out of range)"))
        return 1;

    if (!expect_result(
            nkgpu_image_create_desc(resources.renderer, &image_desc, &resources.round_trip_image),
            NKGPU_OK, "nkgpu_image_create_desc(round trip)"))
        return 1;
    buffer_to_image.buffer = resources.round_trip_buffer;
    buffer_to_image.buffer_offset = 0;
    buffer_to_image.image = resources.round_trip_image;
    if (!expect_result(nkgpu_buffer_to_image(resources.renderer, &buffer_to_image), NKGPU_OK,
                       "nkgpu_buffer_to_image(round trip)"))
        return 1;
    nkgpu_buffer_image_copy_desc invalid_buffer_to_image = buffer_to_image;
    invalid_buffer_to_image.row_pitch = sizeof(uint32_t);
    if (!expect_result(nkgpu_buffer_to_image(resources.renderer, &invalid_buffer_to_image),
                       NKGPU_ERROR_INVALID_ARGUMENT, "nkgpu_buffer_to_image(row pitch)"))
        return 1;
    invalid_buffer_to_image = buffer_to_image;
    invalid_buffer_to_image.buffer_offset = sizeof(pixels);
    if (!expect_result(nkgpu_buffer_to_image(resources.renderer, &invalid_buffer_to_image),
                       NKGPU_ERROR_INVALID_ARGUMENT, "nkgpu_buffer_to_image(buffer range)"))
        return 1;

    uint8_t round_trip_readback_bytes[sizeof(pixels)]{};
    if (!readback(resources.renderer, resources.round_trip_image, 0, 0, 2, 2,
                  round_trip_readback_bytes, sizeof(round_trip_readback_bytes)))
        return 1;
    if (std::memcmp(round_trip_readback_bytes, pixels, sizeof(pixels)) != 0) {
        const auto *actual_pixels = reinterpret_cast<const uint32_t *>(round_trip_readback_bytes);
        std::fprintf(stderr, "image after image-to-buffer round trip: %u, %u, %u, %u\n",
                     actual_pixels[0], actual_pixels[1], actual_pixels[2], actual_pixels[3]);
        return 1;
    }

    uint8_t readback_bytes[sizeof(pixels)]{};
    if (!readback(resources.renderer, resources.round_trip_image, 0, 0, 2, 2, readback_bytes,
                  sizeof(readback_bytes)) ||
        std::memcmp(readback_bytes, pixels, sizeof(pixels)) != 0) {
        const auto *actual_pixels = reinterpret_cast<const uint32_t *>(readback_bytes);
        std::fprintf(
            stderr,
            "full R32_UINT readback did not match: %u, %u, %u, %u (expected %u, %u, %u, %u)\n",
            actual_pixels[0], actual_pixels[1], actual_pixels[2], actual_pixels[3], pixels[0],
            pixels[1], pixels[2], pixels[3]);
        return 1;
    }

    uint32_t picked_pixel = 0;
    if (!readback(resources.renderer, resources.round_trip_image, 1, 1, 1, 1,
                  reinterpret_cast<uint8_t *>(&picked_pixel), sizeof(picked_pixel)) ||
        picked_pixel != pixels[3]) {
        std::fprintf(stderr, "single-pixel R32_UINT readback did not match\n");
        return 1;
    }
    nkgpu_image_readback_desc invalid_readback{};
    invalid_readback.struct_size = sizeof(invalid_readback);
    invalid_readback.image = resources.round_trip_image;
    invalid_readback.width = 3;
    invalid_readback.height = 1;
    nkgpu_readback invalid_readback_handle{};
    if (!expect_result(nkgpu_readback_begin_image(resources.renderer, &invalid_readback,
                                                  &invalid_readback_handle),
                       NKGPU_ERROR_INVALID_ARGUMENT, "nkgpu_readback_begin_image(out of range)"))
        return 1;

    /* OpenGL's current transfer path is intentionally 2D-only; native D3D11
       and Metal paths must also preserve array-layer addressing. */
    const nkgpu_backend backend = nkgpu_query_backend(resources.renderer);
    if (backend == NKGPU_BACKEND_D3D11 || backend == NKGPU_BACKEND_METAL) {
        const uint32_t array_pixels[] = {11u, 12u, 21u, 22u, 31u, 32u, 41u, 42u};
        nkgpu_image_desc array_desc = image_desc;
        array_desc.layer_count = 2;
        array_desc.usage = NKGPU_IMAGE_SAMPLED;
        array_desc.data = reinterpret_cast<const uint8_t *>(array_pixels);
        array_desc.data_size = sizeof(array_pixels);
        if (!expect_result(
                nkgpu_image_create_desc(resources.renderer, &array_desc, &resources.array_image),
                NKGPU_OK, "nkgpu_image_create_desc(array)"))
            return 1;
        uint32_t array_pixel = 0;
        if (!readback(resources.renderer, resources.array_image, 1, 1, 1, 1,
                      reinterpret_cast<uint8_t *>(&array_pixel), sizeof(array_pixel)) ||
            array_pixel != array_pixels[3]) {
            std::fprintf(stderr, "array layer 0 readback did not match\n");
            return 1;
        }
        nkgpu_image_readback_desc layer_desc{};
        layer_desc.struct_size = sizeof(layer_desc);
        layer_desc.image = resources.array_image;
        layer_desc.layer = 1;
        layer_desc.x = 1;
        layer_desc.y = 1;
        layer_desc.width = 1;
        layer_desc.height = 1;
        nkgpu_readback layer_readback{};
        if (!expect_result(
                nkgpu_readback_begin_image(resources.renderer, &layer_desc, &layer_readback),
                NKGPU_OK, "nkgpu_readback_begin_image(array layer)"))
            return 1;
        resources.readback = layer_readback;
        nkgpu_readback_info layer_info{};
        layer_info.struct_size = sizeof(layer_info);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            if (!expect_result(
                    nkgpu_readback_query(resources.renderer, layer_readback, &layer_info), NKGPU_OK,
                    "nkgpu_readback_query(array layer)"))
                return 1;
            if (layer_info.state != NKGPU_READBACK_PENDING)
                break;
            std::this_thread::yield();
        }
        uint32_t layer_pixel = 0;
        uint32_t layer_size = 0;
        if (!expect_result(nkgpu_readback_read(resources.renderer, layer_readback,
                                               reinterpret_cast<uint8_t *>(&layer_pixel),
                                               sizeof(layer_pixel), &layer_size),
                           NKGPU_OK, "nkgpu_readback_read(array layer)"))
            return 1;
        if (layer_size != sizeof(layer_pixel) || layer_pixel != array_pixels[7]) {
            std::fprintf(stderr, "array layer 1 readback did not match\n");
            return 1;
        }
        if (!expect_result(nkgpu_readback_destroy(resources.renderer, layer_readback), NKGPU_OK,
                           "nkgpu_readback_destroy(array layer)"))
            return 1;
        resources.readback = {};
    }

    return 0;
}
