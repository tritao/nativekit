#include "nativekit.h"
#include "nativekit_gpu.h"
#include "nativekit_window.h"
#include "testing.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>

#define EXPECT_RESULT(expression, expected)                                                        \
    do {                                                                                           \
        const nkgpu_result actual_result = (expression);                                           \
        if (actual_result != (expected)) {                                                         \
            std::fprintf(stderr, "%s returned %d, expected %d: %s\n", #expression, actual_result,  \
                         (expected), nkgpu_last_error());                                          \
            result = __LINE__;                                                                     \
            goto cleanup;                                                                          \
        }                                                                                          \
    } while (0)

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK)
        return 1;

    int result = 0;
    bool window_created = false;
    bool surface_created = false;
    bool other_window_created = false;
    bool other_surface_created = false;
    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 192;
    window_options.height = 128;
    window_options.title = "NativeKit GPU contract";
    nk_window window = 0;
    nk_surface surface = 0;
    nk_window other_window = 0;
    nk_surface other_surface = 0;
    nkgpu_renderer first{};
    nkgpu_renderer second{};
    nkgpu_renderer foreign_renderer{};
    nkgpu_buffer buffer{};
    nkgpu_buffer_builder unfinished_buffer{};
    nkgpu_shader shader{};
    nkgpu_shader_builder shader_builder{};
    nkgpu_pipeline_builder unfinished_pipeline{};
    nkgpu_pipeline pipeline{};
    nkgpu_render_target target{};
    nkgpu_render_target lost_target{};
    nkgpu_buffer descriptor_buffer{};
    nkgpu_buffer transfer_source{};
    nkgpu_buffer transfer_destination{};
    nkgpu_buffer compute_buffer{};
    nkgpu_image descriptor_color{};
    nkgpu_image descriptor_color_second{};
    nkgpu_image descriptor_depth{};
    nkgpu_image descriptor_mipped{};
    nkgpu_image dynamic_image{};
    nkgpu_image transfer_image{};
    nkgpu_image transfer_image_second{};
    nkgpu_shader compute_shader{};
    nkgpu_shader_builder compute_shader_builder{};
    nkgpu_pipeline compute_pipeline{};
    nkgpu_pipeline_builder compute_pipeline_builder{};
    nkgpu_readback transfer_readback{};
    nk_graphics_image retained_image{};
    nk_graphics_image foreign_image{};
    const uint8_t buffer_data[] = {0, 0, 0, 0};
    bool gles = false;
    const char *vertex_source = nullptr;
    const char *fragment_source = nullptr;

    if (nk_window_create(&window_options, &window) != NK_OK) {
        result = 2;
        goto cleanup;
    }
    window_created = true;
    if (nkgpu_surface_create(window, window_options.width, window_options.height, &surface) !=
        NKGPU_OK) {
        std::fprintf(stderr, "surface creation failed: %s\n", nkgpu_last_error());
        result = 3;
        goto cleanup;
    }
    surface_created = true;

    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool ready = false;
        while (!ready && std::chrono::steady_clock::now() < deadline) {
            nk_event event{};
            event.struct_size = sizeof(event);
            if (nk_poll_event(&event) != NK_OK) {
                result = 4;
                goto cleanup;
            }
            ready = event.kind == NK_EVENT_SURFACE_READY && event.source == surface;
            nk_event_release(&event);
            if (!ready)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!ready) {
            std::fprintf(stderr, "surface did not become ready\n");
            result = 5;
            goto cleanup;
        }
    }

    EXPECT_RESULT(nkgpu_renderer_create(surface, &first), NKGPU_OK);
    EXPECT_RESULT(nkgpu_renderer_create(surface, &second), NKGPU_OK);

#if defined(NK_GPU_TEST_BACKEND_MATRIX)
    {
        nk_window_options other_options = window_options;
        other_options.title = "NativeKit GPU foreign-device test";
        if (nk_window_create(&other_options, &other_window) != NK_OK) {
            result = 6;
            goto cleanup;
        }
        other_window_created = true;
        const nk_graphics_api foreign_api = nkgpu_query_graphics_api(first) == NK_GRAPHICS_OPENGL
                                                ? NK_GRAPHICS_OPENGL_ES
                                                : NK_GRAPHICS_OPENGL;
        if (nkgpu_surface_create_for_api(other_window, foreign_api, other_options.width,
                                         other_options.height, &other_surface) != NKGPU_OK) {
            std::fprintf(stderr, "foreign surface creation failed: %s\n", nkgpu_last_error());
            result = 7;
            goto cleanup;
        }
        other_surface_created = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        bool ready = false;
        while (!ready && std::chrono::steady_clock::now() < deadline) {
            nk_event event{};
            event.struct_size = sizeof(event);
            if (nk_poll_event(&event) != NK_OK) {
                result = 8;
                goto cleanup;
            }
            ready = event.kind == NK_EVENT_SURFACE_READY && event.source == other_surface;
            nk_event_release(&event);
            if (!ready)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if (!ready) {
            std::fprintf(stderr, "foreign surface did not become ready\n");
            result = 9;
            goto cleanup;
        }
        EXPECT_RESULT(nkgpu_renderer_create(other_surface, &foreign_renderer), NKGPU_OK);
        nkgpu_render_target foreign_target{};
        EXPECT_RESULT(nkgpu_render_target_create(foreign_renderer, 8, 8, 0, &foreign_target),
                      NKGPU_OK);
        EXPECT_RESULT(
            nkgpu_render_target_get_image(foreign_renderer, foreign_target, &foreign_image),
            NKGPU_OK);
        EXPECT_RESULT(nk_graphics_image_retain(foreign_image), NK_OK);
        EXPECT_RESULT(nkgpu_render_target_destroy(foreign_renderer, foreign_target), NKGPU_OK);
        EXPECT_RESULT(nkgpu_frame_begin(first), NKGPU_OK);
        EXPECT_RESULT(
            nkgpu_begin_window_pass(first, window_options.width, window_options.height, 1),
            NKGPU_OK);
        EXPECT_RESULT(nkgpu_apply_graphics_image(first, 0, foreign_image),
                      NKGPU_ERROR_INVALID_HANDLE);
        EXPECT_RESULT(nkgpu_end_frame(first), NKGPU_OK);
        EXPECT_RESULT(nk_graphics_image_release(foreign_image), NK_OK);
        foreign_image = {};
        EXPECT_RESULT(nkgpu_renderer_destroy(foreign_renderer), NKGPU_OK);
        foreign_renderer = {};
    }
#endif

    EXPECT_RESULT(nkgpu_end_frame(first), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_draw(first, 0, 3, 1), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_begin_frame(first), NKGPU_OK);
    EXPECT_RESULT(nkgpu_begin_frame(second), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_end_render_target(first), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_renderer_destroy(first), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_render_target_create(first, 16, 16, 0, &target), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_buffer_create(first, buffer_data, sizeof(buffer_data), &buffer),
                  NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_end_frame(first), NKGPU_OK);
    {
        nkgpu_renderer_state state = NKGPU_RENDERER_LOST;
        EXPECT_RESULT(nkgpu_renderer_get_state(first, &state), NKGPU_OK);
        if (state != NKGPU_RENDERER_READY) {
            result = __LINE__;
            goto cleanup;
        }
    }

    EXPECT_RESULT(nkgpu_render_target_create(first, 16, 16, 0, &target), NKGPU_OK);
    EXPECT_RESULT(nkgpu_begin_render_target(second, target, 1), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_begin_render_target(first, target, 2), NKGPU_ERROR_INVALID_ARGUMENT);
    EXPECT_RESULT(nkgpu_begin_render_target(first, target, 1), NKGPU_OK);
    {
        nkgpu_renderer_state state = NKGPU_RENDERER_READY;
        EXPECT_RESULT(nkgpu_renderer_get_state(first, &state), NKGPU_OK);
        if (state != NKGPU_RENDERER_RENDER_TARGET_ACTIVE) {
            result = __LINE__;
            goto cleanup;
        }
    }
    EXPECT_RESULT(nkgpu_begin_frame(first), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_end_frame(first), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_render_target_destroy(first, target), NKGPU_ERROR_WRONG_STATE);
    EXPECT_RESULT(nkgpu_end_render_target(first), NKGPU_OK);
    EXPECT_RESULT(nkgpu_render_target_destroy(first, target), NKGPU_OK);

    {
        nkgpu_features features{};
        nkgpu_limits limits{};
        features.struct_size = sizeof(features);
        limits.struct_size = sizeof(limits);
        EXPECT_RESULT(nkgpu_query_features(first, &features), NKGPU_OK);
        EXPECT_RESULT(nkgpu_query_limits(first, &limits), NKGPU_OK);
        nkgpu_native_context native_context{};
        native_context.struct_size = sizeof(native_context);
        EXPECT_RESULT(nkgpu_get_native_context(first, &native_context), NKGPU_OK);
        if (!native_context.backend) {
            result = __LINE__;
            goto cleanup;
        }
        nkgpu_command_stream_desc unsupported_stream{};
        unsupported_stream.struct_size = sizeof(unsupported_stream);
        unsupported_stream.version = 99;
        EXPECT_RESULT(nkgpu_submit_command_stream(first, &unsupported_stream),
                      NKGPU_ERROR_INVALID_ARGUMENT);
        if (!features.mrt_count || !features.max_samples || !features.instancing ||
            !features.buffer_copy ||
            !features.image_copy || !features.image_readback || !limits.max_texture_size ||
            !limits.max_color_attachments) {
            result = __LINE__;
            goto cleanup;
        }

        const uint8_t transfer_pixels[] = {
            1, 0, 0, 0, 2, 0, 0, 0, 3, 0, 0, 0, 4, 0, 0, 0,
        };
        nkgpu_buffer_desc transfer_buffer_desc{};
        transfer_buffer_desc.struct_size = sizeof(transfer_buffer_desc);
        transfer_buffer_desc.size = sizeof(transfer_pixels);
        transfer_buffer_desc.usage = NKGPU_BUFFER_TRANSFER;
        transfer_buffer_desc.data = transfer_pixels;
        transfer_buffer_desc.data_size = sizeof(transfer_pixels);
        EXPECT_RESULT(nkgpu_buffer_create_desc(first, &transfer_buffer_desc, &transfer_source),
                      NKGPU_OK);
        transfer_buffer_desc.data = nullptr;
        transfer_buffer_desc.data_size = 0;
        EXPECT_RESULT(nkgpu_buffer_create_desc(first, &transfer_buffer_desc, &transfer_destination),
                      NKGPU_OK);
        nkgpu_buffer_copy_desc buffer_copy{};
        buffer_copy.struct_size = sizeof(buffer_copy);
        buffer_copy.source = transfer_source;
        buffer_copy.destination = transfer_destination;
        buffer_copy.size = sizeof(transfer_pixels);
        EXPECT_RESULT(nkgpu_buffer_copy(first, &buffer_copy), NKGPU_OK);

        nkgpu_image_desc transfer_image_desc{};
        transfer_image_desc.struct_size = sizeof(transfer_image_desc);
        transfer_image_desc.width = 2;
        transfer_image_desc.height = 2;
        transfer_image_desc.format = NKGPU_IMAGEFORMAT_R32_UINT;
        transfer_image_desc.usage = NKGPU_IMAGE_SAMPLED | NKGPU_IMAGE_RENDER_TARGET;
        EXPECT_RESULT(nkgpu_image_create_desc(first, &transfer_image_desc, &transfer_image),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_image_create_desc(first, &transfer_image_desc, &transfer_image_second),
                      NKGPU_OK);
        nkgpu_buffer_image_copy_desc buffer_to_image{};
        buffer_to_image.struct_size = sizeof(buffer_to_image);
        buffer_to_image.buffer = transfer_destination;
        buffer_to_image.image = transfer_image;
        buffer_to_image.width = 2;
        buffer_to_image.height = 2;
        EXPECT_RESULT(nkgpu_buffer_to_image(first, &buffer_to_image), NKGPU_OK);

        nkgpu_image_copy_desc image_copy{};
        image_copy.struct_size = sizeof(image_copy);
        image_copy.source = transfer_image;
        image_copy.destination = transfer_image_second;
        image_copy.width = 2;
        image_copy.height = 2;
        EXPECT_RESULT(nkgpu_image_copy(first, &image_copy), NKGPU_OK);

        nkgpu_buffer_image_copy_desc image_to_buffer = buffer_to_image;
        image_to_buffer.image = transfer_image_second;
        EXPECT_RESULT(nkgpu_image_to_buffer(first, &image_to_buffer), NKGPU_OK);
        buffer_to_image.image = transfer_image;
        EXPECT_RESULT(nkgpu_buffer_to_image(first, &buffer_to_image), NKGPU_OK);

        nkgpu_image_readback_desc readback_desc{};
        readback_desc.struct_size = sizeof(readback_desc);
        readback_desc.image = transfer_image;
        readback_desc.width = 2;
        readback_desc.height = 2;
        EXPECT_RESULT(nkgpu_readback_begin_image(first, &readback_desc, &transfer_readback),
                      NKGPU_OK);
        nkgpu_readback_info readback_info{};
        readback_info.struct_size = sizeof(readback_info);
        for (int attempt = 0; attempt < 100; ++attempt) {
            EXPECT_RESULT(nkgpu_readback_query(first, transfer_readback, &readback_info), NKGPU_OK);
            if (readback_info.state != NKGPU_READBACK_PENDING)
                break;
            std::this_thread::yield();
        }
        if (readback_info.state != NKGPU_READBACK_READY ||
            readback_info.size != sizeof(transfer_pixels)) {
            result = __LINE__;
            goto cleanup;
        }
        uint8_t readback_pixels[sizeof(transfer_pixels)]{};
        uint32_t readback_size = 0;
        EXPECT_RESULT(nkgpu_readback_read(first, transfer_readback, readback_pixels,
                                          sizeof(readback_pixels), &readback_size),
                      NKGPU_OK);
        if (readback_size != sizeof(readback_pixels) ||
            memcmp(readback_pixels, transfer_pixels, sizeof(transfer_pixels)) != 0) {
            result = __LINE__;
            goto cleanup;
        }
        EXPECT_RESULT(nkgpu_readback_destroy(first, transfer_readback), NKGPU_OK);
        transfer_readback = {};
        EXPECT_RESULT(nkgpu_image_destroy(first, transfer_image_second), NKGPU_OK);
        transfer_image_second = {};
        EXPECT_RESULT(nkgpu_image_destroy(first, transfer_image), NKGPU_OK);
        transfer_image = {};
        EXPECT_RESULT(nkgpu_buffer_destroy(first, transfer_destination), NKGPU_OK);
        transfer_destination = {};
        EXPECT_RESULT(nkgpu_buffer_destroy(first, transfer_source), NKGPU_OK);
        transfer_source = {};

        const uint8_t initial[] = {1, 2, 3, 4, 5, 6, 7, 8};
        nkgpu_buffer_desc buffer_desc{};
        buffer_desc.struct_size = sizeof(buffer_desc);
        buffer_desc.size = sizeof(initial);
        buffer_desc.usage = NKGPU_BUFFER_VERTEX;
        buffer_desc.data = initial;
        buffer_desc.data_size = sizeof(initial);
        buffer_desc.dynamic_update = 1;
        EXPECT_RESULT(nkgpu_buffer_create_desc(first, &buffer_desc, &descriptor_buffer), NKGPU_OK);
        const uint8_t replacement[] = {9, 10, 11, 12};
        EXPECT_RESULT(
            nkgpu_buffer_update(first, descriptor_buffer, 2, replacement, sizeof(replacement)),
            NKGPU_OK);
        EXPECT_RESULT(nkgpu_buffer_destroy(first, descriptor_buffer), NKGPU_OK);
        descriptor_buffer = {};

        uint8_t mip_pixels[80]{};
        for (uint32_t index = 0; index < sizeof(mip_pixels); ++index)
            mip_pixels[index] = static_cast<uint8_t>(index);
        nkgpu_image_desc mipped_desc{};
        mipped_desc.struct_size = sizeof(mipped_desc);
        mipped_desc.width = 4;
        mipped_desc.height = 4;
        mipped_desc.format = NKGPU_IMAGEFORMAT_RGBA8;
        mipped_desc.usage = NKGPU_IMAGE_SAMPLED;
        mipped_desc.mip_count = 2;
        mipped_desc.data = mip_pixels;
        mipped_desc.data_size = sizeof(mip_pixels);
        EXPECT_RESULT(nkgpu_image_create_desc(first, &mipped_desc, &descriptor_mipped), NKGPU_OK);

        const uint8_t dynamic_pixels[] = {0, 0, 0, 255};
        nkgpu_image_desc dynamic_desc{};
        dynamic_desc.struct_size = sizeof(dynamic_desc);
        dynamic_desc.width = 1;
        dynamic_desc.height = 1;
        dynamic_desc.format = NKGPU_IMAGEFORMAT_RGBA8;
        dynamic_desc.usage = NKGPU_IMAGE_SAMPLED;
        dynamic_desc.data = dynamic_pixels;
        dynamic_desc.data_size = sizeof(dynamic_pixels);
        dynamic_desc.dynamic_update = 1;
        EXPECT_RESULT(nkgpu_image_create_desc(first, &dynamic_desc, &dynamic_image), NKGPU_OK);
        const uint8_t updated_pixel[] = {255, 0, 0, 255};
        EXPECT_RESULT(nkgpu_image_update(first, dynamic_image, 0, 0, 1, 1, updated_pixel, 4),
                      NKGPU_OK);

        nkgpu_image_desc color_desc{};
        color_desc.struct_size = sizeof(color_desc);
        color_desc.width = 16;
        color_desc.height = 16;
        color_desc.format = NKGPU_IMAGEFORMAT_RGBA8;
        color_desc.usage = NKGPU_IMAGE_SAMPLED | NKGPU_IMAGE_RENDER_TARGET;
        EXPECT_RESULT(nkgpu_image_create_desc(first, &color_desc, &descriptor_color), NKGPU_OK);
        EXPECT_RESULT(nkgpu_image_create_desc(first, &color_desc, &descriptor_color_second),
                      NKGPU_OK);

        nkgpu_image_desc depth_desc{};
        depth_desc.struct_size = sizeof(depth_desc);
        depth_desc.width = 16;
        depth_desc.height = 16;
        depth_desc.format = NKGPU_IMAGEFORMAT_DEPTH24_STENCIL8;
        depth_desc.usage = NKGPU_IMAGE_DEPTH_STENCIL;
        EXPECT_RESULT(nkgpu_image_create_desc(first, &depth_desc, &descriptor_depth), NKGPU_OK);

        nkgpu_render_pass_desc pass_desc{};
        pass_desc.struct_size = sizeof(pass_desc);
        pass_desc.color_count = 2;
        pass_desc.colors[0].image = descriptor_color;
        pass_desc.colors[1].image = descriptor_color_second;
        for (uint32_t index = 0; index < pass_desc.color_count; ++index) {
            pass_desc.colors[index].action.load_action = NKGPU_LOADACTION_CLEAR;
            pass_desc.colors[index].action.store_action = NKGPU_STOREACTION_STORE;
            pass_desc.colors[index].action.clear_color = {0.1f, 0.2f, 0.3f, 1.0f};
        }
        pass_desc.depth_stencil = descriptor_depth;
        pass_desc.depth_stencil_action.load_action = NKGPU_LOADACTION_CLEAR;
        pass_desc.depth_stencil_action.store_action = NKGPU_STOREACTION_STORE;
        pass_desc.depth_stencil_action.clear_depth = 1.0f;
        EXPECT_RESULT(nkgpu_frame_begin(first), NKGPU_OK);
        EXPECT_RESULT(nkgpu_begin_render_pass(first, &pass_desc), NKGPU_OK);
        EXPECT_RESULT(nkgpu_apply_viewport(first, 0, 0, 16, 16), NKGPU_OK);
        EXPECT_RESULT(nkgpu_end_pass(first), NKGPU_OK);
        EXPECT_RESULT(nkgpu_end_frame(first), NKGPU_OK);

        if (features.compute) {
            const char *compute_source =
                nkgpu_query_graphics_api(first) == NK_GRAPHICS_OPENGL_ES
                    ? "#version 310 es\n"
                      "layout(local_size_x=1, local_size_y=1, local_size_z=1) in;\n"
                      "layout(std430, binding=0) buffer Data { uint value[]; };\n"
                      "void main(){ value[0] = value[0] + 1u; }\n"
                    : "#version 430\n"
                      "layout(local_size_x=1, local_size_y=1, local_size_z=1) in;\n"
                      "layout(std430, binding=0) buffer Data { uint value[]; };\n"
                      "void main(){ value[0] = value[0] + 1u; }\n";
            EXPECT_RESULT(nkgpu_shader_begin_compute(first, NKGPU_SHADERLANGUAGE_GLSL,
                                                     compute_source, &compute_shader_builder),
                          NKGPU_OK);
            nkgpu_shader_binding_desc storage_binding{};
            storage_binding.struct_size = sizeof(storage_binding);
            storage_binding.kind = NKGPU_SHADERBINDING_STORAGE_BUFFER;
            storage_binding.stage = NKGPU_SHADERSTAGE_COMPUTE;
            storage_binding.slot = 0;
            EXPECT_RESULT(nkgpu_shader_binding(compute_shader_builder, &storage_binding),
                          NKGPU_OK);
            EXPECT_RESULT(nkgpu_shader_end(compute_shader_builder, &compute_shader), NKGPU_OK);
            compute_shader_builder = {};

            EXPECT_RESULT(
                nkgpu_pipeline_begin_compute(first, compute_shader, &compute_pipeline_builder),
                NKGPU_OK);
            EXPECT_RESULT(nkgpu_pipeline_end(compute_pipeline_builder, &compute_pipeline),
                          NKGPU_OK);
            compute_pipeline_builder = {};

            const uint32_t compute_value = 7;
            nkgpu_buffer_desc compute_desc{};
            compute_desc.struct_size = sizeof(compute_desc);
            compute_desc.size = sizeof(compute_value);
            compute_desc.usage = NKGPU_BUFFER_STORAGE;
            compute_desc.data = reinterpret_cast<const uint8_t *>(&compute_value);
            compute_desc.data_size = sizeof(compute_value);
            EXPECT_RESULT(nkgpu_buffer_create_desc(first, &compute_desc, &compute_buffer),
                          NKGPU_OK);

            EXPECT_RESULT(nkgpu_frame_begin(first), NKGPU_OK);
            EXPECT_RESULT(nkgpu_begin_compute_pass(first), NKGPU_OK);
            EXPECT_RESULT(nkgpu_apply_pipeline(first, compute_pipeline), NKGPU_OK);
            EXPECT_RESULT(nkgpu_apply_storage_buffer(first, 0, compute_buffer), NKGPU_OK);
            EXPECT_RESULT(nkgpu_dispatch(first, 1, 1, 1), NKGPU_OK);
            EXPECT_RESULT(nkgpu_end_pass(first), NKGPU_OK);
            EXPECT_RESULT(
                nkgpu_begin_window_pass(first, window_options.width, window_options.height, 0),
                NKGPU_OK);
            EXPECT_RESULT(nkgpu_end_frame(first), NKGPU_OK);

            EXPECT_RESULT(nkgpu_buffer_destroy(first, compute_buffer), NKGPU_OK);
            EXPECT_RESULT(nkgpu_pipeline_destroy(first, compute_pipeline), NKGPU_OK);
            EXPECT_RESULT(nkgpu_shader_destroy(first, compute_shader), NKGPU_OK);
            compute_buffer = {};
            compute_pipeline = {};
            compute_shader = {};
        }
        EXPECT_RESULT(nkgpu_image_destroy(first, descriptor_depth), NKGPU_OK);
        EXPECT_RESULT(nkgpu_image_destroy(first, descriptor_color_second), NKGPU_OK);
        EXPECT_RESULT(nkgpu_image_destroy(first, descriptor_color), NKGPU_OK);
        EXPECT_RESULT(nkgpu_image_destroy(first, dynamic_image), NKGPU_OK);
        EXPECT_RESULT(nkgpu_image_destroy(first, descriptor_mipped), NKGPU_OK);
        descriptor_depth = {};
        descriptor_color_second = {};
        descriptor_color = {};
        dynamic_image = {};
        descriptor_mipped = {};
    }

    for (int iteration = 0; iteration < 1000; ++iteration) {
        nk_graphics_image borrowed{};
        nk_graphics_image_info info{};
        info.struct_size = sizeof(info);
        EXPECT_RESULT(nkgpu_render_target_create(first, 8, 8, 1, &target), NKGPU_OK);
        EXPECT_RESULT(nkgpu_render_target_get_image(first, target, &borrowed), NKGPU_OK);
        EXPECT_RESULT(nk_graphics_image_retain(borrowed), NKGPU_OK);
        retained_image = borrowed;
        EXPECT_RESULT(nkgpu_render_target_destroy(first, target), NKGPU_OK);
        EXPECT_RESULT(nk_graphics_image_get_info(retained_image, &info), NKGPU_OK);
        if (info.width != 8 || info.height != 8 || !info.device.id) {
            result = __LINE__;
            goto cleanup;
        }
        EXPECT_RESULT(nk_graphics_image_release(retained_image), NKGPU_OK);
        retained_image = {};
    }

    {
        nkgpu_renderer_stats stats{};
        EXPECT_RESULT(nkgpu_renderer_get_stats(first, &stats), NKGPU_OK);
        if (stats.struct_size != sizeof(stats) || stats.render_targets_live != 0 ||
            stats.resource_creations < 1000 || stats.resource_destructions < 1000 ||
            stats.render_target_bytes != 0) {
            result = __LINE__;
            goto cleanup;
        }
    }

    {
        const uint8_t pixel[] = {255, 255, 255, 255};
        nkgpu_image failed_image{};
        nkgpu_buffer failed_buffer{};
        nkgpu_test_fail_next_image_creation();
        EXPECT_RESULT(nkgpu_image_create(first, 1, 1, NKGPU_IMAGEFORMAT_RGBA8, pixel, sizeof(pixel),
                                         0, &failed_image),
                      NKGPU_ERROR_OUT_OF_MEMORY);
        nkgpu_test_fail_next_buffer_creation();
        EXPECT_RESULT(nkgpu_buffer_create(first, pixel, sizeof(pixel), &failed_buffer),
                      NKGPU_ERROR_OUT_OF_MEMORY);
        nkgpu_renderer_stats stats{};
        EXPECT_RESULT(nkgpu_renderer_get_stats(first, &stats), NKGPU_OK);
        if (stats.failed_allocations < 2) {
            result = __LINE__;
            goto cleanup;
        }
    }

    EXPECT_RESULT(nkgpu_surface_resize(surface, 240, 160), NKGPU_OK);
    {
        nkgpu_renderer_state state = NKGPU_RENDERER_LOST;
        EXPECT_RESULT(nkgpu_renderer_get_state(first, &state), NKGPU_OK);
        if (state != NKGPU_RENDERER_READY) {
            result = __LINE__;
            goto cleanup;
        }
    }

    EXPECT_RESULT(nkgpu_buffer_create(first, buffer_data, sizeof(buffer_data), &buffer), NKGPU_OK);
    EXPECT_RESULT(nkgpu_buffer_destroy(second, buffer), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_shader_create(first, 0, "void main(){}", "void main(){}", &shader),
                  NKGPU_ERROR_INVALID_ARGUMENT);
    EXPECT_RESULT(nkgpu_shader_begin(first, 0, "void main(){}", "void main(){}", &shader_builder),
                  NKGPU_ERROR_INVALID_ARGUMENT);

    gles = nkgpu_query_graphics_api(first) == NK_GRAPHICS_OPENGL_ES;
    vertex_source = gles ? "#version 300 es\nvoid main(){gl_Position=vec4(0.0);}\n"
                         : "#version 330\nvoid main(){gl_Position=vec4(0.0);}\n";
    fragment_source =
        gles ? "#version 300 es\nprecision mediump float; out vec4 c; void main(){c=vec4(1.0);}\n"
             : "#version 330\nout vec4 c; void main(){c=vec4(1.0);}\n";
    EXPECT_RESULT(nkgpu_shader_create(first, NKGPU_SHADERLANGUAGE_GLSL, vertex_source,
                                      fragment_source, &shader),
                  NKGPU_OK);
    EXPECT_RESULT(nkgpu_shader_begin(first, NKGPU_SHADERLANGUAGE_GLSL, vertex_source,
                                     fragment_source, &shader_builder),
                  NKGPU_OK);
    EXPECT_RESULT(nkgpu_shader_end(shader_builder, &shader), NKGPU_OK);
    {
        nkgpu_pipeline_builder expanded_pipeline{};
        nkgpu_depth_state depth_state{};
        depth_state.enabled = 1;
        depth_state.compare = NKGPU_COMPAREFUNC_LESS_EQUAL;
        depth_state.write_enabled = 1;
        nkgpu_blend_state blend_state{};
        blend_state.enabled = 1;
        blend_state.src_rgb = NKGPU_BLENDFACTOR_SRC_COLOR;
        blend_state.dst_rgb = NKGPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
        blend_state.op_rgb = NKGPU_BLENDOP_ADD;
        blend_state.src_alpha = NKGPU_BLENDFACTOR_ONE;
        blend_state.dst_alpha = NKGPU_BLENDFACTOR_ZERO;
        blend_state.op_alpha = NKGPU_BLENDOP_ADD;
        nkgpu_stencil_state stencil_state{};
        stencil_state.enabled = 1;
        stencil_state.read_mask = 0xff;
        stencil_state.write_mask = 0xff;
        stencil_state.reference = 1;
        stencil_state.front = {NKGPU_COMPAREFUNC_ALWAYS, NKGPU_STENCILOP_KEEP, NKGPU_STENCILOP_KEEP,
                               NKGPU_STENCILOP_KEEP};
        stencil_state.back = stencil_state.front;
        EXPECT_RESULT(nkgpu_pipeline_begin(first, shader, 4, &expanded_pipeline), NKGPU_OK);
        EXPECT_RESULT(
            nkgpu_pipeline_vertex_buffer(expanded_pipeline, 0, 4, NKGPU_VERTEXSTEP_PER_INSTANCE, 1),
            NKGPU_OK);
        EXPECT_RESULT(
            nkgpu_pipeline_attribute(expanded_pipeline, 0, 0, 0, NKGPU_VERTEXFORMAT_FLOAT),
            NKGPU_OK);
        EXPECT_RESULT(nkgpu_pipeline_primitive_type(expanded_pipeline, NKGPU_PRIMITIVETYPE_POINTS),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_pipeline_depth(expanded_pipeline, &depth_state), NKGPU_OK);
        EXPECT_RESULT(nkgpu_pipeline_blend(expanded_pipeline, &blend_state), NKGPU_OK);
        EXPECT_RESULT(nkgpu_pipeline_stencil(expanded_pipeline, &stencil_state), NKGPU_OK);
        EXPECT_RESULT(nkgpu_pipeline_cull_mode(expanded_pipeline, NKGPU_CULLMODE_FRONT,
                                               NKGPU_FACEWINDING_CCW),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_pipeline_color_target(expanded_pipeline, 0, NKGPU_IMAGEFORMAT_RGBA8,
                                                  NKGPU_COLORMASK_RGBA, &blend_state),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_pipeline_multisample(expanded_pipeline, 1, 0), NKGPU_OK);
        EXPECT_RESULT(nkgpu_pipeline_end(expanded_pipeline, &pipeline), NKGPU_OK);
        EXPECT_RESULT(nkgpu_pipeline_destroy(first, pipeline), NKGPU_OK);
        pipeline = {};
    }
    EXPECT_RESULT(nkgpu_pipeline_begin(second, shader, 4, &unfinished_pipeline),
                  NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_pipeline_begin(first, shader, 4, &unfinished_pipeline), NKGPU_OK);
    EXPECT_RESULT(nkgpu_buffer_begin(first, 16, &unfinished_buffer), NKGPU_OK);

    EXPECT_RESULT(nkgpu_render_target_create(second, 8, 8, 0, &lost_target), NKGPU_OK);
    EXPECT_RESULT(nkgpu_test_lose_after_frames(second, 1), NKGPU_OK);
    EXPECT_RESULT(nkgpu_begin_frame(second), NKGPU_OK);
    EXPECT_RESULT(nkgpu_end_frame(second), NKGPU_OK);
    {
        nkgpu_renderer_state state = NKGPU_RENDERER_READY;
        EXPECT_RESULT(nkgpu_renderer_get_state(second, &state), NKGPU_OK);
        if (state != NKGPU_RENDERER_LOST) {
            result = __LINE__;
            goto cleanup;
        }
    }
    EXPECT_RESULT(nkgpu_begin_frame(second), NKGPU_ERROR_DEVICE_LOST);
    EXPECT_RESULT(nkgpu_buffer_create_stream(second, 16, NKGPU_BUFFER_VERTEX, &buffer),
                  NKGPU_ERROR_DEVICE_LOST);
    {
        const uint8_t pixel[] = {255, 255, 255, 255};
        nkgpu_image lost_image{};
        nkgpu_render_target new_target{};
        EXPECT_RESULT(nkgpu_image_create(second, 1, 1, NKGPU_IMAGEFORMAT_RGBA8, pixel,
                                         sizeof(pixel), 0, &lost_image),
                      NKGPU_ERROR_DEVICE_LOST);
        EXPECT_RESULT(nkgpu_render_target_create(second, 8, 8, 0, &new_target),
                      NKGPU_ERROR_DEVICE_LOST);
    }
    EXPECT_RESULT(nkgpu_draw(second, 0, 3, 1), NKGPU_ERROR_DEVICE_LOST);
    EXPECT_RESULT(nkgpu_render_target_destroy(second, lost_target), NKGPU_OK);
    lost_target = {};
    {
        nkgpu_renderer_stats stats{};
        EXPECT_RESULT(nkgpu_renderer_get_stats(second, &stats), NKGPU_OK);
        if (stats.device_losses != 1 || stats.render_targets_live != 0) {
            result = __LINE__;
            goto cleanup;
        }
    }
    EXPECT_RESULT(nkgpu_renderer_destroy(second), NKGPU_OK);
    second = {};

    {
        nkgpu_renderer third{};
        EXPECT_RESULT(nkgpu_renderer_create(surface, &third), NKGPU_OK);
        EXPECT_RESULT(nkgpu_test_invalidate_surface(third), NKGPU_OK);
        EXPECT_RESULT(nkgpu_begin_frame(third), NKGPU_ERROR_DEVICE_LOST);
        EXPECT_RESULT(nkgpu_renderer_destroy(third), NKGPU_OK);
    }

    EXPECT_RESULT(nkgpu_render_target_create(first, 16, 16, 0, &target), NKGPU_OK);
    EXPECT_RESULT(nkgpu_render_target_get_image(first, target, &retained_image), NKGPU_OK);
    EXPECT_RESULT(nk_graphics_image_retain(retained_image), NK_OK);
    EXPECT_RESULT(nkgpu_render_target_destroy(first, target), NKGPU_OK);
    nkgpu_test_fail_next_present();
    EXPECT_RESULT(nkgpu_begin_frame(first), NKGPU_OK);
    EXPECT_RESULT(nkgpu_end_frame(first), NKGPU_ERROR_DEVICE_LOST);
    EXPECT_RESULT(nkgpu_renderer_destroy(first), NKGPU_OK);
    first = {};
    EXPECT_RESULT(nkgpu_buffer_destroy(first, buffer), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_buffer_end(unfinished_buffer, &buffer), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_pipeline_end(unfinished_pipeline, &pipeline), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_render_target_destroy(first, target), NKGPU_ERROR_INVALID_HANDLE);
    {
        nk_graphics_image_info info{};
        info.struct_size = sizeof(info);
        EXPECT_RESULT(nk_graphics_image_get_info(retained_image, &info), NK_OK);
        const nk_graphics_image stale_image = retained_image;
        nkgpu_renderer recreated{};
        EXPECT_RESULT(nkgpu_renderer_create(surface, &recreated), NKGPU_OK);
        EXPECT_RESULT(nkgpu_begin_frame(recreated), NKGPU_OK);
        EXPECT_RESULT(nkgpu_apply_graphics_image(recreated, 0, retained_image), NKGPU_OK);
        EXPECT_RESULT(nkgpu_end_frame(recreated), NKGPU_OK);
        EXPECT_RESULT(nkgpu_renderer_destroy(recreated), NKGPU_OK);
        EXPECT_RESULT(nk_graphics_image_release(retained_image), NK_OK);
        retained_image = {};
        EXPECT_RESULT(nk_graphics_image_get_info(stale_image, &info), NK_ERROR_INVALID_HANDLE);
    }

cleanup:
    if (transfer_readback.id)
        nkgpu_readback_destroy(first, transfer_readback);
    if (descriptor_depth.id)
        nkgpu_image_destroy(first, descriptor_depth);
    if (descriptor_color_second.id)
        nkgpu_image_destroy(first, descriptor_color_second);
    if (descriptor_color.id)
        nkgpu_image_destroy(first, descriptor_color);
    if (dynamic_image.id)
        nkgpu_image_destroy(first, dynamic_image);
    if (descriptor_mipped.id)
        nkgpu_image_destroy(first, descriptor_mipped);
    if (transfer_image_second.id)
        nkgpu_image_destroy(first, transfer_image_second);
    if (transfer_image.id)
        nkgpu_image_destroy(first, transfer_image);
    if (descriptor_buffer.id)
        nkgpu_buffer_destroy(first, descriptor_buffer);
    if (transfer_destination.id)
        nkgpu_buffer_destroy(first, transfer_destination);
    if (transfer_source.id)
        nkgpu_buffer_destroy(first, transfer_source);
    if (compute_buffer.id)
        nkgpu_buffer_destroy(first, compute_buffer);
    if (compute_pipeline.id)
        nkgpu_pipeline_destroy(first, compute_pipeline);
    if (compute_shader.id)
        nkgpu_shader_destroy(first, compute_shader);
    if (retained_image.id)
        nk_graphics_image_release(retained_image);
    if (foreign_image.id)
        nk_graphics_image_release(foreign_image);
    if (first.id) {
        nkgpu_end_render_target(first);
        nkgpu_end_frame(first);
        nkgpu_renderer_destroy(first);
    }
    if (second.id)
        nkgpu_renderer_destroy(second);
    if (foreign_renderer.id)
        nkgpu_renderer_destroy(foreign_renderer);
    if (other_surface_created)
        nk_surface_destroy(other_surface);
    if (other_window_created)
        nk_window_destroy(other_window);
    if (surface_created)
        nk_surface_destroy(surface);
    if (window_created)
        nk_window_destroy(window);
    nk_shutdown();
    return result;
}

#undef EXPECT_RESULT
