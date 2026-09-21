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

bool buffer_readback(nkgpu_renderer renderer, nkgpu_buffer buffer, uint32_t offset, uint32_t size,
                     uint8_t *destination, uint32_t destination_size) {
    nkgpu_buffer_readback_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.buffer = buffer;
    desc.offset = offset;
    desc.size = size;
    nkgpu_readback request{};
    if (!expect_result(nkgpu_readback_begin_buffer(renderer, &desc, &request), NKGPU_OK,
                       "nkgpu_readback_begin_buffer"))
        return false;
    nkgpu_readback_info info{};
    info.struct_size = sizeof(info);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!expect_result(nkgpu_readback_query(renderer, request, &info), NKGPU_OK,
                           "nkgpu_readback_query(buffer)")) {
            nkgpu_readback_destroy(renderer, request);
            return false;
        }
        if (info.state != NKGPU_READBACK_PENDING)
            break;
        std::this_thread::yield();
    }
    bool success = info.state == NKGPU_READBACK_READY && info.size == size &&
                   info.row_pitch == size && size <= destination_size;
    uint32_t actual_size = 0;
    if (success && !expect_result(nkgpu_readback_read(renderer, request, destination,
                                                      destination_size, &actual_size),
                                  NKGPU_OK, "nkgpu_readback_read(buffer)"))
        success = false;
    if (success && actual_size != size)
        success = false;
    if (!expect_result(nkgpu_readback_destroy(renderer, request), NKGPU_OK,
                       "nkgpu_readback_destroy(buffer)"))
        success = false;
    return success;
}

uint32_t format_bytes(nkgpu_image_format format) {
    switch (format) {
    case NKGPU_IMAGEFORMAT_R8:
        return 1;
    case NKGPU_IMAGEFORMAT_RG8:
        return 2;
    case NKGPU_IMAGEFORMAT_RGBA8:
    case NKGPU_IMAGEFORMAT_BGRA8:
    case NKGPU_IMAGEFORMAT_RG16F:
    case NKGPU_IMAGEFORMAT_R32F:
    case NKGPU_IMAGEFORMAT_R32_UINT:
    case NKGPU_IMAGEFORMAT_DEPTH24_STENCIL8:
    case NKGPU_IMAGEFORMAT_DEPTH32F:
        return 4;
    case NKGPU_IMAGEFORMAT_R16F:
        return 2;
    case NKGPU_IMAGEFORMAT_RGBA16F:
        return 8;
    case NKGPU_IMAGEFORMAT_RGBA32F:
        return 16;
    case NKGPU_IMAGEFORMAT_DEPTH16:
        return 2;
    default:
        return 0;
    }
}

bool is_depth_format(nkgpu_image_format format) {
    return format == NKGPU_IMAGEFORMAT_DEPTH32F;
}

const char *format_name(nkgpu_image_format format) {
    switch (format) {
    case NKGPU_IMAGEFORMAT_R8:
        return "R8";
    case NKGPU_IMAGEFORMAT_RG8:
        return "RG8";
    case NKGPU_IMAGEFORMAT_RGBA8:
        return "RGBA8";
    case NKGPU_IMAGEFORMAT_BGRA8:
        return "BGRA8";
    case NKGPU_IMAGEFORMAT_R16F:
        return "R16F";
    case NKGPU_IMAGEFORMAT_RG16F:
        return "RG16F";
    case NKGPU_IMAGEFORMAT_RGBA16F:
        return "RGBA16F";
    case NKGPU_IMAGEFORMAT_R32F:
        return "R32F";
    case NKGPU_IMAGEFORMAT_RGBA32F:
        return "RGBA32F";
    case NKGPU_IMAGEFORMAT_R32_UINT:
        return "R32_UINT";
    case NKGPU_IMAGEFORMAT_DEPTH16:
        return "DEPTH16";
    case NKGPU_IMAGEFORMAT_DEPTH24_STENCIL8:
        return "DEPTH24_STENCIL8";
    case NKGPU_IMAGEFORMAT_DEPTH32F:
        return "DEPTH32F";
    default:
        return "unknown";
    }
}

bool offscreen_format(nkgpu_renderer renderer, const nkgpu_features &features,
                      nkgpu_image_format format) {
    nkgpu_image_format_support support{};
    support.struct_size = sizeof(support);
    if (!expect_result(nkgpu_query_image_format_support(renderer, format, &support), NKGPU_OK,
                       "nkgpu_query_image_format_support(offscreen)"))
        return false;
    const bool depth = is_depth_format(format);
    const bool can_render = depth ? support.depth_stencil != 0 : support.render_target != 0;
    const bool can_sample = support.sampled != 0;
    if (!can_render && !can_sample)
        return true;

    const uint32_t width = 2;
    const uint32_t height = 2;
    const uint32_t bytes = format_bytes(format);
    uint8_t initial_data[128]{};
    for (uint32_t index = 0; index < width * height * bytes; ++index)
        initial_data[index] = static_cast<uint8_t>(0x20u + index);
    uint8_t expected_bytes[128]{};
    if (can_render && depth) {
        const uint32_t depth_bits = 0x3f800000u;
        for (uint32_t offset = 0; offset < width * height * bytes; offset += sizeof(depth_bits))
            std::memcpy(expected_bytes + offset, &depth_bits, sizeof(depth_bits));
    } else if (!can_render) {
        std::memcpy(expected_bytes, initial_data, width * height * bytes);
    }

    nkgpu_image_usage usage = 0;
    if (can_sample)
        usage |= NKGPU_IMAGE_SAMPLED;
    if (can_render)
        usage |= depth ? NKGPU_IMAGE_DEPTH_STENCIL : NKGPU_IMAGE_RENDER_TARGET;
    nkgpu_image_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.width = width;
    desc.height = height;
    desc.format = format;
    desc.usage = usage;
    if (!can_render) {
        desc.data = initial_data;
        desc.data_size = width * height * bytes;
    }

    nkgpu_image source{};
    nkgpu_image destination{};
    if (!expect_result(nkgpu_image_create_desc(renderer, &desc, &source), NKGPU_OK,
                       "nkgpu_image_create_desc(offscreen source)"))
        return false;

    bool success = true;
    if (can_render) {
        nkgpu_render_pass_desc pass{};
        pass.struct_size = sizeof(pass);
        if (depth) {
            pass.depth_stencil = source;
            pass.depth_stencil_action.load_action = NKGPU_LOADACTION_CLEAR;
            pass.depth_stencil_action.store_action = NKGPU_STOREACTION_STORE;
            pass.depth_stencil_action.clear_depth = 1.0f;
        } else {
            pass.color_count = 1;
            pass.colors[0].image = source;
            pass.colors[0].action.load_action =
                format == NKGPU_IMAGEFORMAT_R32_UINT ? NKGPU_LOADACTION_DISCARD
                                                      : NKGPU_LOADACTION_CLEAR;
            pass.colors[0].action.store_action = NKGPU_STOREACTION_STORE;
            pass.colors[0].action.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
        }
        success = expect_result(nkgpu_frame_begin(renderer), NKGPU_OK,
                                "nkgpu_frame_begin(offscreen)") &&
                   expect_result(nkgpu_begin_render_pass(renderer, &pass), NKGPU_OK,
                                  "nkgpu_begin_render_pass(offscreen)") &&
                   expect_result(nkgpu_end_pass(renderer), NKGPU_OK,
                                  "nkgpu_end_pass(offscreen)") &&
                   expect_result(nkgpu_end_frame(renderer), NKGPU_OK,
                                  "nkgpu_end_frame(offscreen)");
    }

    auto check_readback = [&](nkgpu_image image, const char *stage) {
        uint8_t readback_bytes[128]{};
        if (readback(renderer, image, 0, 0, width, height, readback_bytes,
                     width * height * bytes) &&
            std::memcmp(readback_bytes, expected_bytes, width * height * bytes) == 0)
            return true;
        uint32_t actual_bits = 0;
        uint32_t expected_bits = 0;
        std::memcpy(&actual_bits, readback_bytes, sizeof(actual_bits));
        std::memcpy(&expected_bits, expected_bytes, sizeof(expected_bits));
        std::fprintf(stderr, "offscreen format %s %s readback mismatch: %08x != %08x\n",
                     format_name(format), stage, actual_bits, expected_bits);
        return false;
    };
    if (success && can_render && can_sample && features.image_readback && support.readback)
        success = check_readback(source, "source");
    if (success && can_render && depth && features.image_readback && support.readback) {
        uint32_t depth_pixel = 0;
        const uint32_t expected_depth_bits = 0x3f800000u;
        if (!readback(renderer, source, 1, 1, 1, 1, reinterpret_cast<uint8_t *>(&depth_pixel),
                      sizeof(depth_pixel)) ||
            depth_pixel != expected_depth_bits) {
            std::fprintf(stderr,
                         "offscreen format %s depth rectangle readback mismatch: %08x != %08x\n",
                         format_name(format), depth_pixel, expected_depth_bits);
            success = false;
        }
    }
    if (success && can_render && depth && support.readback && features.image_to_buffer &&
        features.buffer_readback) {
        nkgpu_buffer depth_buffer{};
        nkgpu_buffer_desc depth_buffer_desc{};
        depth_buffer_desc.struct_size = sizeof(depth_buffer_desc);
        depth_buffer_desc.size = bytes;
        depth_buffer_desc.usage = NKGPU_BUFFER_TRANSFER;
        if (!expect_result(nkgpu_buffer_create_desc(renderer, &depth_buffer_desc, &depth_buffer),
                           NKGPU_OK, "nkgpu_buffer_create_desc(depth readback)")) {
            success = false;
        } else {
            nkgpu_buffer_image_copy_desc depth_copy{};
            depth_copy.struct_size = sizeof(depth_copy);
            depth_copy.buffer = depth_buffer;
            depth_copy.image = source;
            depth_copy.x = 1;
            depth_copy.y = 1;
            depth_copy.width = 1;
            depth_copy.height = 1;
            const nkgpu_result depth_copy_result = nkgpu_image_to_buffer(renderer, &depth_copy);
            if (depth_copy_result == NKGPU_OK) {
                uint32_t depth_pixel = 0;
                if (!buffer_readback(renderer, depth_buffer, 0, bytes,
                                     reinterpret_cast<uint8_t *>(&depth_pixel), bytes) ||
                    depth_pixel != 0x3f800000u) {
                    std::fprintf(stderr, "offscreen format %s depth buffer rectangle mismatch\n",
                                 format_name(format));
                    success = false;
                }
            } else if (depth_copy_result != NKGPU_ERROR_UNSUPPORTED) {
                std::fprintf(stderr, "offscreen format %s depth image-to-buffer returned %d: %s\n",
                             format_name(format), depth_copy_result, nkgpu_last_error());
                success = false;
            }
            expect_result(nkgpu_buffer_destroy(renderer, depth_buffer), NKGPU_OK,
                          "nkgpu_buffer_destroy(depth readback)");
        }
    }

    if (success && features.image_copy && support.copy) {
        nkgpu_image_desc destination_desc = desc;
        if (can_render) {
            destination_desc.data = nullptr;
            destination_desc.data_size = 0;
        } else {
            destination_desc.data = initial_data;
            destination_desc.data_size = width * height * bytes;
        }
        if (!expect_result(nkgpu_image_create_desc(renderer, &destination_desc, &destination),
                           NKGPU_OK, "nkgpu_image_create_desc(offscreen destination)"))
            success = false;
        nkgpu_image_copy_desc copy{};
        copy.struct_size = sizeof(copy);
        copy.source = source;
        copy.destination = destination;
        copy.width = width;
        copy.height = height;
        if (success && !expect_result(nkgpu_image_copy(renderer, &copy), NKGPU_OK,
                                       "nkgpu_image_copy(offscreen)"))
            success = false;
        if (success && format == NKGPU_IMAGEFORMAT_DEPTH32F &&
            nkgpu_query_backend(renderer) == NKGPU_BACKEND_D3D11) {
            nkgpu_image_copy_desc depth_region_copy = copy;
            depth_region_copy.width = 1;
            depth_region_copy.height = 1;
            if (!expect_result(nkgpu_image_copy(renderer, &depth_region_copy),
                               NKGPU_ERROR_UNSUPPORTED,
                               "nkgpu_image_copy(depth subregion, D3D11)"))
                success = false;
        }
    }

    if (success && can_sample && features.image_readback && support.readback) {
        if (!check_readback(destination.id ? destination : source, "destination"))
            success = false;
    }
    if (destination.id)
        expect_result(nkgpu_image_destroy(renderer, destination), NKGPU_OK,
                      "nkgpu_image_destroy(offscreen destination)");
    expect_result(nkgpu_image_destroy(renderer, source), NKGPU_OK,
                  "nkgpu_image_destroy(offscreen source)");
    if (!success)
        std::fprintf(stderr, "offscreen format %s failed\n", format_name(format));
    return success;
}

bool msaa_resolve(nkgpu_renderer renderer, const nkgpu_features &features) {
    nkgpu_image_format_support support{};
    support.struct_size = sizeof(support);
    if (!expect_result(nkgpu_query_image_format_support(renderer, NKGPU_IMAGEFORMAT_RGBA8,
                                                        &support),
                       NKGPU_OK, "nkgpu_query_image_format_support(MSAA)"))
        return false;
    if (!features.image_readback || features.max_samples < 2 || !support.multisample)
        return true;

    const uint32_t sample_count = features.max_samples >= 4 ? 4u : 2u;
    nkgpu_image_desc multisample_desc{};
    multisample_desc.struct_size = sizeof(multisample_desc);
    multisample_desc.width = 4;
    multisample_desc.height = 4;
    multisample_desc.format = NKGPU_IMAGEFORMAT_RGBA8;
    multisample_desc.usage = NKGPU_IMAGE_RENDER_TARGET;
    multisample_desc.sample_count = sample_count;
    nkgpu_image multisample_image{};
    nkgpu_image resolve_image{};
    if (!expect_result(nkgpu_image_create_desc(renderer, &multisample_desc, &multisample_image),
                       NKGPU_OK, "nkgpu_image_create_desc(MSAA)"))
        return false;

    nkgpu_image_desc resolve_desc = multisample_desc;
    resolve_desc.sample_count = 1;
    resolve_desc.usage = NKGPU_IMAGE_SAMPLED | NKGPU_IMAGE_RENDER_TARGET;
    bool success = expect_result(nkgpu_image_create_desc(renderer, &resolve_desc, &resolve_image),
                                 NKGPU_OK, "nkgpu_image_create_desc(MSAA resolve)");
    if (success) {
        nkgpu_render_pass_desc pass{};
        pass.struct_size = sizeof(pass);
        pass.color_count = 1;
        pass.colors[0].image = multisample_image;
        pass.colors[0].resolve_image = resolve_image;
        pass.colors[0].action.load_action = NKGPU_LOADACTION_CLEAR;
        pass.colors[0].action.store_action = NKGPU_STOREACTION_STORE;
        pass.colors[0].action.clear_color = {0.2f, 0.4f, 0.6f, 1.0f};
        success = expect_result(nkgpu_frame_begin(renderer), NKGPU_OK,
                                "nkgpu_frame_begin(MSAA)") &&
                   expect_result(nkgpu_begin_render_pass(renderer, &pass), NKGPU_OK,
                                  "nkgpu_begin_render_pass(MSAA)") &&
                   expect_result(nkgpu_end_pass(renderer), NKGPU_OK, "nkgpu_end_pass(MSAA)") &&
                   expect_result(nkgpu_end_frame(renderer), NKGPU_OK,
                                  "nkgpu_end_frame(MSAA)");
    }
    if (success) {
        uint8_t pixel[4]{};
        const uint8_t expected[] = {51, 102, 153, 255};
        success = readback(renderer, resolve_image, 0, 0, 1, 1, pixel, sizeof(pixel)) &&
                  std::memcmp(pixel, expected, sizeof(expected)) == 0;
        if (!success)
            std::fprintf(stderr, "MSAA resolve readback mismatch: %u,%u,%u,%u\n", pixel[0],
                         pixel[1], pixel[2], pixel[3]);
    }
    if (resolve_image.id)
        expect_result(nkgpu_image_destroy(renderer, resolve_image), NKGPU_OK,
                      "nkgpu_image_destroy(MSAA resolve)");
    if (multisample_image.id)
        expect_result(nkgpu_image_destroy(renderer, multisample_image), NKGPU_OK,
                      "nkgpu_image_destroy(MSAA)");
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
    const nk_graphics_api requested_api =
#if defined(NKGPU_TRANSFER_SMOKE_GLES3)
        NK_GRAPHICS_OPENGL_ES;
#else
        nkgpu_default_graphics_api();
#endif
    if (nk_window_create(&window_options, &resources.window) != NK_OK ||
        nkgpu_surface_create_for_api(resources.window, requested_api, window_options.width,
                                     window_options.height, &resources.surface) != NKGPU_OK ||
        !wait_for_surface(resources.surface) ||
        !expect_result(nkgpu_renderer_create(resources.surface, &resources.renderer), NKGPU_OK,
                       "nkgpu_renderer_create"))
        return 1;

    nkgpu_features features{};
    features.struct_size = sizeof(features);
    if (!expect_result(nkgpu_query_features(resources.renderer, &features), NKGPU_OK,
                       "nkgpu_query_features") ||
        !features.buffer_copy || !features.image_copy || !features.image_readback ||
        !features.buffer_readback || !features.buffer_to_image || !features.image_to_buffer) {
        std::fprintf(stderr, "the selected GPU backend does not expose transfer operations\n");
        return 1;
    }

    const nkgpu_backend backend = nkgpu_query_backend(resources.renderer);
    if (!msaa_resolve(resources.renderer, features))
        return 1;
    if (features.timestamps) {
        nkgpu_timestamp scene_timestamp{};
        nkgpu_timestamp depth_timestamp{};
        nkgpu_timestamp legacy_timestamp{};
        if (!expect_result(nkgpu_frame_begin(resources.renderer), NKGPU_OK,
                           "nkgpu_frame_begin(timestamp)"))
            return 1;
        if (!expect_result(nkgpu_begin_window_pass(resources.renderer, window_options.width,
                                                   window_options.height, 0),
                           NKGPU_OK, "nkgpu_begin_window_pass(timestamp)"))
            return 1;
        nkgpu_timestamp_desc scene_desc{};
        scene_desc.struct_size = sizeof(scene_desc);
        scene_desc.label = "scene";
        if (!expect_result(nkgpu_timestamp_begin_desc(resources.renderer, &scene_desc,
                                                      &scene_timestamp),
                           NKGPU_OK, "nkgpu_timestamp_begin_desc(scene)"))
            return 1;
        if (!expect_result(nkgpu_timestamp_end(resources.renderer, scene_timestamp), NKGPU_OK,
                           "nkgpu_timestamp_end(scene)"))
            return 1;
        nkgpu_timestamp_desc depth_desc{};
        depth_desc.struct_size = sizeof(depth_desc);
        depth_desc.label = "depth";
        if (!expect_result(nkgpu_timestamp_begin_desc(resources.renderer, &depth_desc,
                                                      &depth_timestamp),
                           NKGPU_OK, "nkgpu_timestamp_begin_desc(depth)"))
            return 1;
        if (!expect_result(nkgpu_timestamp_end(resources.renderer, depth_timestamp), NKGPU_OK,
                           "nkgpu_timestamp_end(depth)"))
            return 1;
        if (!expect_result(nkgpu_timestamp_begin(resources.renderer, &legacy_timestamp),
                           NKGPU_OK, "nkgpu_timestamp_begin(legacy)"))
            return 1;
        if (!expect_result(nkgpu_timestamp_end(resources.renderer, legacy_timestamp), NKGPU_OK,
                           "nkgpu_timestamp_end(legacy)"))
            return 1;
        if (!expect_result(nkgpu_timestamp_end(resources.renderer, scene_timestamp),
                           NKGPU_ERROR_WRONG_STATE, "nkgpu_timestamp_end(repeated)"))
            return 1;
        if (!expect_result(nkgpu_end_pass(resources.renderer), NKGPU_OK,
                           "nkgpu_end_pass(timestamp)"))
            return 1;
        if (!expect_result(nkgpu_end_frame(resources.renderer), NKGPU_OK,
                           "nkgpu_end_frame(timestamp)"))
            return 1;

        const char *scene_label =
            nkgpu_timestamp_get_label(resources.renderer, scene_timestamp);
        const char *depth_label =
            nkgpu_timestamp_get_label(resources.renderer, depth_timestamp);
        const char *legacy_label =
            nkgpu_timestamp_get_label(resources.renderer, legacy_timestamp);
        if (!scene_label || std::strcmp(scene_label, "scene") != 0 || !depth_label ||
            std::strcmp(depth_label, "depth") != 0 || !legacy_label || legacy_label[0] != '\0') {
            std::fprintf(stderr, "timestamp labels were not retained\n");
            return 1;
        }

        const nkgpu_timestamp timestamps[] = {scene_timestamp, depth_timestamp, legacy_timestamp};
        nkgpu_timestamp_result timestamp_results[3]{};
        for (auto &timestamp_result : timestamp_results)
            timestamp_result.struct_size = sizeof(timestamp_result);
        nkgpu_timestamp_info timestamp_info{};
        timestamp_info.struct_size = sizeof(timestamp_info);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            if (!expect_result(nkgpu_timestamp_collect(resources.renderer, timestamps, 3,
                                                       timestamp_results),
                               NKGPU_OK, "nkgpu_timestamp_collect"))
                return 1;
            if (timestamp_results[0].state != NKGPU_TIMESTAMP_PENDING &&
                timestamp_results[1].state != NKGPU_TIMESTAMP_PENDING &&
                timestamp_results[2].state != NKGPU_TIMESTAMP_PENDING)
                break;
            std::this_thread::yield();
        }
        if (timestamp_results[0].state != NKGPU_TIMESTAMP_READY ||
            timestamp_results[1].state != NKGPU_TIMESTAMP_READY ||
            timestamp_results[2].state != NKGPU_TIMESTAMP_READY) {
            std::fprintf(stderr, "GPU timestamp collection did not become ready\n");
            return 1;
        }
        if (!expect_result(nkgpu_timestamp_query(resources.renderer, scene_timestamp,
                                                 &timestamp_info),
                           NKGPU_OK, "nkgpu_timestamp_query"))
            return 1;
        if (timestamp_info.state != NKGPU_TIMESTAMP_READY) {
            std::fprintf(stderr, "GPU timestamp did not become ready\n");
            return 1;
        }
        if (!expect_result(nkgpu_timestamp_destroy(resources.renderer, scene_timestamp), NKGPU_OK,
                           "nkgpu_timestamp_destroy(scene)"))
            return 1;
        if (!expect_result(nkgpu_timestamp_destroy(resources.renderer, depth_timestamp), NKGPU_OK,
                           "nkgpu_timestamp_destroy(depth)"))
            return 1;
        if (!expect_result(nkgpu_timestamp_destroy(resources.renderer, legacy_timestamp), NKGPU_OK,
                           "nkgpu_timestamp_destroy(legacy)"))
            return 1;
        nkgpu_timestamp_info stale_timestamp_info{};
        stale_timestamp_info.struct_size = sizeof(stale_timestamp_info);
        if (!expect_result(nkgpu_timestamp_query(resources.renderer, scene_timestamp,
                                                 &stale_timestamp_info),
                           NKGPU_ERROR_INVALID_HANDLE, "nkgpu_timestamp_query(stale)"))
            return 1;
        nkgpu_timestamp outside_timestamp{};
        if (!expect_result(nkgpu_timestamp_begin(resources.renderer, &outside_timestamp),
                           NKGPU_ERROR_WRONG_STATE, "nkgpu_timestamp_begin(outside pass)"))
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
    uint8_t buffer_readback_bytes[sizeof(pixels)]{};
    if (!buffer_readback(resources.renderer, resources.source_buffer, 8, sizeof(pixels),
                         buffer_readback_bytes, sizeof(buffer_readback_bytes)) ||
        std::memcmp(buffer_readback_bytes, pixels, sizeof(pixels)) != 0) {
        std::fprintf(stderr, "buffer readback did not match source bytes\n");
        return 1;
    }
    nkgpu_buffer_readback_desc invalid_buffer_readback{};
    invalid_buffer_readback.struct_size = sizeof(invalid_buffer_readback);
    invalid_buffer_readback.buffer = resources.source_buffer;
    invalid_buffer_readback.offset = sizeof(source_data);
    invalid_buffer_readback.size = 1;
    nkgpu_readback invalid_buffer_readback_handle{};
    if (!expect_result(nkgpu_readback_begin_buffer(resources.renderer, &invalid_buffer_readback,
                                                   &invalid_buffer_readback_handle),
                       NKGPU_ERROR_INVALID_ARGUMENT,
                       "nkgpu_readback_begin_buffer(out of range)"))
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
    uint32_t picked_column[2]{};
    if (!readback(resources.renderer, resources.round_trip_image, 0, 0, 1, 2,
                  reinterpret_cast<uint8_t *>(picked_column), sizeof(picked_column)) ||
        picked_column[0] != pixels[0] || picked_column[1] != pixels[2]) {
        std::fprintf(stderr, "R32_UINT rectangle readback did not match\n");
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

    const nkgpu_image_format offscreen_formats[] = {
        NKGPU_IMAGEFORMAT_R8,
        NKGPU_IMAGEFORMAT_RG8,
        NKGPU_IMAGEFORMAT_RGBA8,
        NKGPU_IMAGEFORMAT_BGRA8,
        NKGPU_IMAGEFORMAT_R16F,
        NKGPU_IMAGEFORMAT_RG16F,
        NKGPU_IMAGEFORMAT_RGBA16F,
        NKGPU_IMAGEFORMAT_R32F,
        NKGPU_IMAGEFORMAT_RGBA32F,
        NKGPU_IMAGEFORMAT_R32_UINT,
        NKGPU_IMAGEFORMAT_DEPTH32F,
    };
    for (const nkgpu_image_format format : offscreen_formats) {
        if (!offscreen_format(resources.renderer, features, format))
            return 1;
    }

    return 0;
}
