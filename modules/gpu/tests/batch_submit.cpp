#include "nativekit.h"
#include "nativekit_gpu.h"
#include "nativekit_window.h"
#include "adapter_internal.h"
#include "core/frame_backend.hpp"
#include "testing.h"

#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#include <GLES3/gl3.h>
#elif defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#include <OpenGL/gl3.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <chrono>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

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

namespace {

void append_u32(std::vector<uint8_t> &bytes, uint32_t value) {
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof(value));
}

void append_record(std::vector<uint8_t> &bytes, uint32_t opcode,
                   const std::vector<uint8_t> &payload) {
    append_u32(bytes, opcode);
    append_u32(bytes, static_cast<uint32_t>(8 + payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
}

void append_apply_pipeline(std::vector<uint8_t> &bytes, nkgpu_pipeline pipeline) {
    std::vector<uint8_t> payload;
    append_u32(payload, pipeline.id);
    append_record(bytes, NKGPU_COMMAND_APPLY_PIPELINE, payload);
}

void append_apply_vertex_buffer(std::vector<uint8_t> &bytes, uint32_t slot, nkgpu_buffer buffer,
                                uint32_t offset) {
    std::vector<uint8_t> payload;
    append_u32(payload, slot);
    append_u32(payload, buffer.id);
    append_u32(payload, offset);
    append_record(bytes, NKGPU_COMMAND_APPLY_VERTEX_BUFFER, payload);
}

void append_draw(std::vector<uint8_t> &bytes, uint32_t base, uint32_t count, uint32_t instances) {
    std::vector<uint8_t> payload;
    append_u32(payload, base);
    append_u32(payload, count);
    append_u32(payload, instances);
    append_record(bytes, NKGPU_COMMAND_DRAW, payload);
}

void append_copy_buffer(std::vector<uint8_t> &bytes, nkgpu_buffer source, uint32_t source_offset,
                        nkgpu_buffer destination, uint32_t destination_offset, uint32_t size) {
    std::vector<uint8_t> payload;
    append_u32(payload, source.id);
    append_u32(payload, source_offset);
    append_u32(payload, destination.id);
    append_u32(payload, destination_offset);
    append_u32(payload, size);
    append_record(bytes, NKGPU_COMMAND_COPY_BUFFER, payload);
}

void append_apply_scissor(std::vector<uint8_t> &bytes, uint32_t enabled, int32_t x, int32_t y,
                          int32_t width, int32_t height) {
    std::vector<uint8_t> payload;
    append_u32(payload, enabled);
    append_u32(payload, static_cast<uint32_t>(x));
    append_u32(payload, static_cast<uint32_t>(y));
    append_u32(payload, static_cast<uint32_t>(width));
    append_u32(payload, static_cast<uint32_t>(height));
    append_record(bytes, NKGPU_COMMAND_APPLY_SCISSOR, payload);
}

void append_apply_sampler(std::vector<uint8_t> &bytes, uint32_t slot, nkgpu_sampler sampler) {
    std::vector<uint8_t> payload;
    append_u32(payload, slot);
    append_u32(payload, sampler.id);
    append_record(bytes, NKGPU_COMMAND_APPLY_SAMPLER, payload);
}

void append_apply_graphics_image(std::vector<uint8_t> &bytes, uint32_t slot,
                                 nk_graphics_image image) {
    std::vector<uint8_t> payload;
    append_u32(payload, slot);
    append_u32(payload, image.id);
    append_record(bytes, NKGPU_COMMAND_APPLY_GRAPHICS_IMAGE, payload);
}

} // namespace

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK)
        return 1;

    int result = 0;
    bool window_created = false;
    bool surface_created = false;
    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 128;
    window_options.height = 96;
    window_options.title = "NativeKit GPU batch submission";
    nk_window window = 0;
    nk_surface surface = 0;
    nkgpu_renderer renderer{};
    nkgpu_shader shader{};
    nkgpu_pipeline pipeline{};
    nkgpu_buffer buffer{};
    nkgpu_render_target target{};
    nkgpu_image general_color{};
    nkgpu_image general_depth{};
    nkgpu_batch batch{};
    nkgpu_batch general_batch{};
    nkgpu_shader textured_shader{};
    nkgpu_pipeline textured_pipeline{};
    nkgpu_buffer textured_buffer{};
    nkgpu_sampler sampler{};
    nk_graphics_image retained_image{};

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

    EXPECT_RESULT(nkgpu_renderer_create(surface, &renderer), NKGPU_OK);

    {
        const bool gles = nkgpu_query_graphics_api(renderer) == NK_GRAPHICS_OPENGL_ES;
        /*
         * A single triangle large enough to cover the framebuffer, so any
         * sampled pixel proves the batch's draw reached the target.
         */
        const char *vertex_source = gles ? "#version 300 es\n"
                                           "layout(location=0) in vec2 position;\n"
                                           "void main(){gl_Position=vec4(position,0.0,1.0);}\n"
                                         : "#version 330\n"
                                           "layout(location=0) in vec2 position;\n"
                                           "void main(){gl_Position=vec4(position,0.0,1.0);}\n";
        const char *fragment_source = gles ? "#version 300 es\n"
                                             "precision mediump float;\n"
                                             "out vec4 frag_color;\n"
                                             "void main(){frag_color=vec4(1.0,0.0,0.0,1.0);}\n"
                                           : "#version 330\n"
                                             "out vec4 frag_color;\n"
                                             "void main(){frag_color=vec4(1.0,0.0,0.0,1.0);}\n";
        EXPECT_RESULT(nkgpu_shader_create(renderer, NKGPU_SHADERLANGUAGE_GLSL, vertex_source,
                                          fragment_source, &shader),
                      NKGPU_OK);
        nkgpu_pipeline_builder builder{};
        EXPECT_RESULT(nkgpu_pipeline_begin(renderer, shader, 2 * sizeof(float), &builder),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_pipeline_attribute(builder, 0, 0, 0, NKGPU_VERTEXFORMAT_FLOAT2),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_pipeline_end(builder, &pipeline), NKGPU_OK);
        const float vertices[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
        EXPECT_RESULT(nkgpu_buffer_create(renderer, reinterpret_cast<const uint8_t *>(vertices),
                                          sizeof(vertices), &buffer),
                      NKGPU_OK);
    }

    /* A pass, then its commands, then another pass. */
    EXPECT_RESULT(nkgpu_batch_seal(batch), NKGPU_ERROR_INVALID_HANDLE);
    EXPECT_RESULT(nkgpu_batch_begin(renderer, &batch), NKGPU_OK);
    EXPECT_RESULT(nkgpu_batch_append_command(batch, nullptr, 0), NKGPU_ERROR_INVALID_ARGUMENT);
    {
        std::vector<uint8_t> commands;
        append_apply_pipeline(commands, pipeline);
        append_apply_vertex_buffer(commands, 0, buffer, 0);
        append_draw(commands, 0, 3, 1);
        /* Commands need a pass first. */
        EXPECT_RESULT(nkgpu_batch_append_command(batch, commands.data(),
                                                 static_cast<uint32_t>(commands.size())),
                      NKGPU_ERROR_WRONG_STATE);
    }
    {
        nkgpu_batch_pass pass{};
        pass.struct_size = sizeof(pass);
        pass.kind = NKGPU_BATCH_PASS_WINDOW;
        pass.clear = 1;
        pass.width = window_options.width;
        pass.height = window_options.height;
        EXPECT_RESULT(nkgpu_batch_append_pass(batch, &pass), NKGPU_OK);

        std::vector<uint8_t> commands;
        append_apply_pipeline(commands, pipeline);
        append_apply_vertex_buffer(commands, 0, buffer, 0);
        append_apply_scissor(commands, 1, 0, 0, window_options.width, window_options.height);
        append_draw(commands, 0, 3, 1);
        nkgpu_command_stream_desc stream{};
        stream.struct_size = sizeof(stream);
        stream.version = NKGPU_COMMAND_STREAM_VERSION_1;
        stream.commands = commands.data();
        stream.size = static_cast<uint32_t>(commands.size());
        EXPECT_RESULT(nkgpu_batch_append_command_stream(batch, &stream), NKGPU_OK);
        stream.version = 99;
        EXPECT_RESULT(nkgpu_batch_append_command_stream(batch, &stream),
                      NKGPU_ERROR_INVALID_ARGUMENT);

        /* A second pass with a partial scissor covers pass ordering. */
        nkgpu_batch_pass overlay{};
        overlay.struct_size = sizeof(overlay);
        overlay.kind = NKGPU_BATCH_PASS_WINDOW;
        overlay.clear = 0;
        overlay.width = window_options.width;
        overlay.height = window_options.height;
        EXPECT_RESULT(nkgpu_batch_append_pass(batch, &overlay), NKGPU_OK);
        std::vector<uint8_t> overlay_commands;
        append_apply_pipeline(overlay_commands, pipeline);
        append_apply_vertex_buffer(overlay_commands, 0, buffer, 0);
        append_apply_scissor(overlay_commands, 1, 0, 0, 8, 8);
        append_apply_scissor(overlay_commands, 0, 0, 0, 0, 0);
        append_draw(overlay_commands, 0, 3, 1);
        EXPECT_RESULT(nkgpu_batch_append_command(batch, overlay_commands.data(),
                                                 static_cast<uint32_t>(overlay_commands.size())),
                      NKGPU_OK);

        nkgpu_batch_pass copy_pass{};
        copy_pass.struct_size = sizeof(copy_pass);
        copy_pass.kind = NKGPU_BATCH_PASS_COPY;
        EXPECT_RESULT(nkgpu_batch_append_pass(batch, &copy_pass), NKGPU_OK);
        std::vector<uint8_t> copy_commands;
        append_copy_buffer(copy_commands, buffer, 0, buffer, 12, 4);
        EXPECT_RESULT(nkgpu_batch_append_command(batch, copy_commands.data(),
                                                 static_cast<uint32_t>(copy_commands.size())),
                      NKGPU_OK);
    }
    /* Sealing is idempotent and freezes the batch. */
    EXPECT_RESULT(nkgpu_batch_seal(batch), NKGPU_OK);
    EXPECT_RESULT(nkgpu_batch_seal(batch), NKGPU_OK);
    {
        std::vector<uint8_t> commands;
        append_draw(commands, 0, 3, 1);
        EXPECT_RESULT(nkgpu_batch_append_command(batch, commands.data(),
                                                 static_cast<uint32_t>(commands.size())),
                      NKGPU_ERROR_WRONG_STATE);
        nkgpu_batch_pass pass{};
        pass.struct_size = sizeof(pass);
        pass.kind = NKGPU_BATCH_PASS_WINDOW;
        pass.width = window_options.width;
        pass.height = window_options.height;
        EXPECT_RESULT(nkgpu_batch_append_pass(batch, &pass), NKGPU_ERROR_WRONG_STATE);
    }

    EXPECT_RESULT(nkgpu_batch_submit(renderer, batch), NKGPU_OK);
    {
        uint8_t pixel[4]{};
        glReadPixels(window_options.width / 2, window_options.height / 2, 1, 1, GL_RGBA,
                     GL_UNSIGNED_BYTE, pixel);
        if (pixel[0] < 200 || pixel[1] > 40 || pixel[2] > 40 || pixel[3] != 255) {
            std::fprintf(stderr, "batch pixel was (%u,%u,%u,%u)\n", pixel[0], pixel[1], pixel[2],
                         pixel[3]);
            result = __LINE__;
            goto cleanup;
        }
    }
    /* The platform can hand an acquired immutable target to render submission. */
    {
        nk_surface_frame frame = NK_INVALID_HANDLE;
        nk_surface_frame_target frame_target{};
        frame_target.struct_size = sizeof(frame_target);
        EXPECT_RESULT(nk_surface_acquire_frame(surface, &frame, &frame_target), NK_OK);
        if (frame_target.frame != frame) {
            std::fprintf(stderr, "acquired frame target did not carry its frame token\n");
            result = __LINE__;
            goto cleanup;
        }
        nk::core::FrameTicket ticket{};
        if (!nk::core::lookup_frame_ticket(frame, &ticket) || !ticket.backend.bind ||
            !ticket.backend.submit || !ticket.backend.finish || !ticket.backend.cancel) {
            std::fprintf(stderr, "acquired frame ticket did not carry backend operations\n");
            result = __LINE__;
            goto cleanup;
        }
        /* Once the platform hands the immutable target to RENDER, the whole
           submission must use that snapshot without querying the surface. */
        nkgpu_test_forbid_surface_target_queries();
        EXPECT_RESULT(nkgpu_batch_submit(renderer, batch, &frame_target), NKGPU_OK);
        nkgpu_test_allow_surface_target_queries();
        EXPECT_RESULT(nk_surface_present_frame(frame), NK_OK);
    }
    /* The inline multi-pass path uses the same immutable target contract. */
    {
        nk_surface_frame frame = NK_INVALID_HANDLE;
        nk_surface_frame_target frame_target{};
        frame_target.struct_size = sizeof(frame_target);
        EXPECT_RESULT(nk_surface_acquire_frame(surface, &frame, &frame_target), NK_OK);
        nkgpu_test_forbid_surface_target_queries();
        EXPECT_RESULT(nkgpu_frame_begin_with_target(renderer, &frame_target), NKGPU_OK);
        EXPECT_RESULT(
            nkgpu_begin_window_pass(renderer, window_options.width, window_options.height, 1),
            NKGPU_OK);
        EXPECT_RESULT(nkgpu_end_frame_deferred_present(renderer), NKGPU_OK);
        nkgpu_test_allow_surface_target_queries();
        EXPECT_RESULT(nk_surface_present_frame(frame), NK_OK);
    }
    /* A submitted ticket can also be cancelled exactly once; physical
       backends use this path to release a drawable without presenting it. */
    {
        nk_surface_frame frame = NK_INVALID_HANDLE;
        nk_surface_frame_target frame_target{};
        frame_target.struct_size = sizeof(frame_target);
        EXPECT_RESULT(nk_surface_acquire_frame(surface, &frame, &frame_target), NK_OK);
        nkgpu_test_forbid_surface_target_queries();
        EXPECT_RESULT(nkgpu_batch_submit(renderer, batch, &frame_target), NKGPU_OK);
        nkgpu_test_allow_surface_target_queries();
        EXPECT_RESULT(nk_surface_cancel_frame(frame), NK_OK);
        EXPECT_RESULT(nk_surface_cancel_frame(frame), NK_ERROR_INVALID_HANDLE);
        EXPECT_RESULT(nk_surface_present_frame(frame), NK_ERROR_INVALID_HANDLE);
    }
    /* A sealed batch can be replayed. */
    EXPECT_RESULT(nkgpu_batch_submit(renderer, batch), NKGPU_OK);
    /* A window frame left by a batch must be closed before other work. */
    EXPECT_RESULT(nkgpu_begin_frame(renderer), NKGPU_OK);
    EXPECT_RESULT(nkgpu_end_frame(renderer), NKGPU_OK);

    /* Recording touches no GPU state, so a batch can be built mid-frame. */
    {
        nkgpu_batch deferred{};
        EXPECT_RESULT(nkgpu_begin_frame(renderer), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_begin(renderer, &deferred), NKGPU_OK);
        nkgpu_batch_pass pass{};
        pass.struct_size = sizeof(pass);
        pass.kind = NKGPU_BATCH_PASS_WINDOW;
        pass.clear = 1;
        pass.width = window_options.width;
        pass.height = window_options.height;
        EXPECT_RESULT(nkgpu_batch_append_pass(deferred, &pass), NKGPU_OK);
        std::vector<uint8_t> commands;
        append_apply_pipeline(commands, pipeline);
        append_apply_vertex_buffer(commands, 0, buffer, 0);
        append_draw(commands, 0, 3, 1);
        EXPECT_RESULT(nkgpu_batch_append_command(deferred, commands.data(),
                                                 static_cast<uint32_t>(commands.size())),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_seal(deferred), NKGPU_OK);
        EXPECT_RESULT(nkgpu_end_frame(renderer), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_submit(renderer, deferred), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_destroy(deferred), NKGPU_OK);
    }

    /*
     * Retention: the batch keeps the pipeline and buffer alive after the
     * caller destroys its own handles, and releases them with the batch.
     */
    EXPECT_RESULT(nkgpu_batch_destroy(batch), NKGPU_OK);
    batch = {};
    EXPECT_RESULT(nkgpu_batch_destroy(batch), NKGPU_ERROR_INVALID_HANDLE);
    {
        nkgpu_pipeline retained_pipeline{};
        nkgpu_buffer retained_buffer{};
        nkgpu_pipeline_builder retained_builder{};
        EXPECT_RESULT(nkgpu_pipeline_begin(renderer, shader, 2 * sizeof(float), &retained_builder),
                      NKGPU_OK);
        EXPECT_RESULT(
            nkgpu_pipeline_attribute(retained_builder, 0, 0, 0, NKGPU_VERTEXFORMAT_FLOAT2),
            NKGPU_OK);
        EXPECT_RESULT(nkgpu_pipeline_end(retained_builder, &retained_pipeline), NKGPU_OK);
        const float vertices[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
        EXPECT_RESULT(nkgpu_buffer_create(renderer, reinterpret_cast<const uint8_t *>(vertices),
                                          sizeof(vertices), &retained_buffer),
                      NKGPU_OK);

        nkgpu_renderer_stats before{};
        EXPECT_RESULT(nkgpu_renderer_get_stats(renderer, &before), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_begin(renderer, &batch), NKGPU_OK);
        nkgpu_batch_pass pass{};
        pass.struct_size = sizeof(pass);
        pass.kind = NKGPU_BATCH_PASS_WINDOW;
        pass.clear = 1;
        pass.width = window_options.width;
        pass.height = window_options.height;
        EXPECT_RESULT(nkgpu_batch_append_pass(batch, &pass), NKGPU_OK);
        std::vector<uint8_t> commands;
        append_apply_pipeline(commands, retained_pipeline);
        append_apply_vertex_buffer(commands, 0, retained_buffer, 0);
        append_draw(commands, 0, 3, 1);
        EXPECT_RESULT(nkgpu_batch_append_command(batch, commands.data(),
                                                 static_cast<uint32_t>(commands.size())),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_seal(batch), NKGPU_OK);
        /* Destroy the caller's references; the batch retains both. */
        EXPECT_RESULT(nkgpu_pipeline_destroy(renderer, retained_pipeline), NKGPU_OK);
        retained_pipeline = {};
        EXPECT_RESULT(nkgpu_buffer_destroy(renderer, retained_buffer), NKGPU_OK);
        retained_buffer = {};
        EXPECT_RESULT(nkgpu_batch_submit(renderer, batch), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_destroy(batch), NKGPU_OK);
        batch = {};
        nkgpu_renderer_stats after{};
        EXPECT_RESULT(nkgpu_renderer_get_stats(renderer, &after), NKGPU_OK);
        if (after.resource_destructions < before.resource_destructions + 2) {
            std::fprintf(stderr, "deferred resource destruction was not recorded\n");
            result = __LINE__;
            goto cleanup;
        }
        /* Slots released by the batch are reusable. */
        EXPECT_RESULT(nkgpu_buffer_create(renderer, reinterpret_cast<const uint8_t *>(vertices),
                                          sizeof(vertices), &retained_buffer),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_buffer_destroy(renderer, retained_buffer), NKGPU_OK);
        retained_buffer = {};
    }

    /* An offscreen pass records and replays like a window pass. */
    EXPECT_RESULT(nkgpu_render_target_create(renderer, 16, 16, 1, &target), NKGPU_OK);
    EXPECT_RESULT(nkgpu_render_target_get_image(renderer, target, &retained_image), NKGPU_OK);
    EXPECT_RESULT(nk_graphics_image_retain(retained_image), NK_OK);
    EXPECT_RESULT(nkgpu_batch_begin(renderer, &batch), NKGPU_OK);
    {
        nkgpu_batch_pass pass{};
        pass.struct_size = sizeof(pass);
        pass.kind = NKGPU_BATCH_PASS_TARGET;
        pass.target = target;
        pass.clear = 1;
        EXPECT_RESULT(nkgpu_batch_append_pass(batch, &pass), NKGPU_OK);
        /* A window pass never carries a target. */
        nkgpu_batch_pass bad{};
        bad.struct_size = sizeof(bad);
        bad.kind = NKGPU_BATCH_PASS_TARGET;
        bad.target = nkgpu_render_target{0};
        EXPECT_RESULT(nkgpu_batch_append_pass(batch, &bad), NKGPU_ERROR_INVALID_HANDLE);
    }
    EXPECT_RESULT(nkgpu_batch_seal(batch), NKGPU_OK);
    EXPECT_RESULT(nkgpu_batch_submit(renderer, batch), NKGPU_OK);

    /* General attachment-based render passes are reusable batch passes too. */
    {
        nkgpu_image_desc color_desc{};
        color_desc.struct_size = sizeof(color_desc);
        color_desc.width = 16;
        color_desc.height = 16;
        color_desc.format = NKGPU_IMAGEFORMAT_RGBA8;
        color_desc.usage = NKGPU_IMAGE_RENDER_TARGET;
        EXPECT_RESULT(nkgpu_image_create_desc(renderer, &color_desc, &general_color), NKGPU_OK);

        nkgpu_image_desc depth_desc{};
        depth_desc.struct_size = sizeof(depth_desc);
        depth_desc.width = 16;
        depth_desc.height = 16;
        depth_desc.format = NKGPU_IMAGEFORMAT_DEPTH24_STENCIL8;
        depth_desc.usage = NKGPU_IMAGE_DEPTH_STENCIL;
        EXPECT_RESULT(nkgpu_image_create_desc(renderer, &depth_desc, &general_depth), NKGPU_OK);

        nkgpu_batch_pass pass{};
        pass.struct_size = sizeof(pass);
        pass.kind = NKGPU_BATCH_PASS_RENDER;
        nkgpu_render_pass_desc render_pass{};
        render_pass.struct_size = sizeof(render_pass);
        render_pass.color_count = 1;
        render_pass.colors[0].image = general_color;
        render_pass.colors[0].action.load_action = NKGPU_LOADACTION_CLEAR;
        render_pass.colors[0].action.store_action = NKGPU_STOREACTION_STORE;
        render_pass.colors[0].action.clear_color = {0.2f, 0.3f, 0.4f, 1.0f};
        render_pass.depth_stencil = general_depth;
        render_pass.depth_stencil_action.load_action = NKGPU_LOADACTION_CLEAR;
        render_pass.depth_stencil_action.store_action = NKGPU_STOREACTION_STORE;
        render_pass.depth_stencil_action.clear_depth = 1.0f;
        pass.render_pass = &render_pass;
        EXPECT_RESULT(nkgpu_batch_begin(renderer, &general_batch), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_append_pass(general_batch, &pass), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_seal(general_batch), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_submit(renderer, general_batch), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_destroy(general_batch), NKGPU_OK);
        general_batch = {};

        nkgpu_batch helper_batch{};
        EXPECT_RESULT(nkgpu_batch_begin(renderer, &helper_batch), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_append_render_pass(helper_batch, &render_pass), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_seal(helper_batch), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_submit(renderer, helper_batch), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_destroy(helper_batch), NKGPU_OK);
        EXPECT_RESULT(nkgpu_image_destroy(renderer, general_depth), NKGPU_OK);
        general_depth = {};
        EXPECT_RESULT(nkgpu_image_destroy(renderer, general_color), NKGPU_OK);
        general_color = {};
    }

    /*
     * Records may reference an external graphics image. The batch retains it
     * through the core handle, so the texture outlives both the target it came
     * from and the caller's own reference.
     */
    {
        const bool gles = nkgpu_query_graphics_api(renderer) == NK_GRAPHICS_OPENGL_ES;
        const char *vertex_source =
            gles ? "#version 300 es\n"
                   "layout(location=0) in vec2 position;\n"
                   "layout(location=1) in vec2 uv;\n"
                   "out vec2 v_uv;\n"
                   "void main(){v_uv=uv;gl_Position=vec4(position,0.0,1.0);}\n"
                 : "#version 330\n"
                   "layout(location=0) in vec2 position;\n"
                   "layout(location=1) in vec2 uv;\n"
                   "out vec2 v_uv;\n"
                   "void main(){v_uv=uv;gl_Position=vec4(position,0.0,1.0);}\n";
        const char *fragment_source = gles ? "#version 300 es\n"
                                             "precision mediump float;\n"
                                             "uniform sampler2D tex;\n"
                                             "in vec2 v_uv;\n"
                                             "out vec4 frag_color;\n"
                                             "void main(){frag_color=texture(tex,v_uv);}\n"
                                           : "#version 330\n"
                                             "uniform sampler2D tex;\n"
                                             "in vec2 v_uv;\n"
                                             "out vec4 frag_color;\n"
                                             "void main(){frag_color=texture(tex,v_uv);}\n";
        nkgpu_shader_builder shader_builder{};
        EXPECT_RESULT(nkgpu_shader_begin(renderer, NKGPU_SHADERLANGUAGE_GLSL, vertex_source,
                                         fragment_source, &shader_builder),
                      NKGPU_OK);
        nkgpu_shader_binding_desc sampled_binding{};
        sampled_binding.struct_size = sizeof(sampled_binding);
        sampled_binding.kind = NKGPU_SHADERBINDING_SAMPLED_IMAGE;
        sampled_binding.stage = NKGPU_SHADERSTAGE_FRAGMENT;
        sampled_binding.slot = 0;
        sampled_binding.secondary_slot = 0;
        sampled_binding.name = "tex";
        EXPECT_RESULT(nkgpu_shader_binding(shader_builder, &sampled_binding), NKGPU_OK);
        nkgpu_shader_binding_desc sampler_binding = sampled_binding;
        sampler_binding.kind = NKGPU_SHADERBINDING_SAMPLER;
        EXPECT_RESULT(nkgpu_shader_binding(shader_builder, &sampler_binding), NKGPU_OK);
        EXPECT_RESULT(nkgpu_shader_end(shader_builder, &textured_shader), NKGPU_OK);
        nkgpu_pipeline_builder pipeline_builder{};
        EXPECT_RESULT(
            nkgpu_pipeline_begin(renderer, textured_shader, 4 * sizeof(float), &pipeline_builder),
            NKGPU_OK);
        EXPECT_RESULT(
            nkgpu_pipeline_attribute(pipeline_builder, 0, 0, 0, NKGPU_VERTEXFORMAT_FLOAT2),
            NKGPU_OK);
        EXPECT_RESULT(nkgpu_pipeline_attribute(pipeline_builder, 1, 0, 2 * sizeof(float),
                                               NKGPU_VERTEXFORMAT_FLOAT2),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_pipeline_end(pipeline_builder, &textured_pipeline), NKGPU_OK);
        const float quad[] = {-1.0f, -1.0f, 0.0f,  0.0f, 3.0f, -1.0f,
                              2.0f,  0.0f,  -1.0f, 3.0f, 0.0f, 2.0f};
        EXPECT_RESULT(nkgpu_buffer_create(renderer, reinterpret_cast<const uint8_t *>(quad),
                                          sizeof(quad), &textured_buffer),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_sampler_create(renderer, NKGPU_FILTER_NEAREST, NKGPU_FILTER_NEAREST,
                                           NKGPU_WRAP_CLAMP_TO_EDGE, NKGPU_WRAP_CLAMP_TO_EDGE,
                                           &sampler),
                      NKGPU_OK);

        nk_graphics_image composite_image{};
        nkgpu_batch image_batch{};
        nkgpu_render_target composite_target{};
        EXPECT_RESULT(nkgpu_render_target_create(renderer, 16, 16, 1, &composite_target), NKGPU_OK);
        /* Fill the target with the solid color pipeline. */
        EXPECT_RESULT(nkgpu_batch_begin(renderer, &image_batch), NKGPU_OK);
        {
            nkgpu_batch_pass pass{};
            pass.struct_size = sizeof(pass);
            pass.kind = NKGPU_BATCH_PASS_TARGET;
            pass.target = composite_target;
            pass.clear = 1;
            EXPECT_RESULT(nkgpu_batch_append_pass(image_batch, &pass), NKGPU_OK);
            std::vector<uint8_t> commands;
            append_apply_pipeline(commands, pipeline);
            append_apply_vertex_buffer(commands, 0, buffer, 0);
            append_draw(commands, 0, 3, 1);
            EXPECT_RESULT(nkgpu_batch_append_command(image_batch, commands.data(),
                                                     static_cast<uint32_t>(commands.size())),
                          NKGPU_OK);
        }
        EXPECT_RESULT(nkgpu_batch_seal(image_batch), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_submit(renderer, image_batch), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_destroy(image_batch), NKGPU_OK);
        EXPECT_RESULT(nkgpu_render_target_get_image(renderer, composite_target, &composite_image),
                      NKGPU_OK);
        EXPECT_RESULT(nk_graphics_image_retain(composite_image), NK_OK);
        /* The target goes away; the retained image keeps the texture alive. */
        EXPECT_RESULT(nkgpu_render_target_destroy(renderer, composite_target), NKGPU_OK);
        composite_target = {};

        EXPECT_RESULT(nkgpu_batch_begin(renderer, &image_batch), NKGPU_OK);
        {
            nkgpu_batch_pass pass{};
            pass.struct_size = sizeof(pass);
            pass.kind = NKGPU_BATCH_PASS_WINDOW;
            pass.clear = 1;
            pass.width = window_options.width;
            pass.height = window_options.height;
            EXPECT_RESULT(nkgpu_batch_append_pass(image_batch, &pass), NKGPU_OK);
            std::vector<uint8_t> commands;
            append_apply_pipeline(commands, textured_pipeline);
            append_apply_vertex_buffer(commands, 0, textured_buffer, 0);
            append_apply_graphics_image(commands, 0, composite_image);
            append_apply_sampler(commands, 0, sampler);
            append_draw(commands, 0, 3, 1);
            EXPECT_RESULT(nkgpu_batch_append_command(image_batch, commands.data(),
                                                     static_cast<uint32_t>(commands.size())),
                          NKGPU_OK);
        }
        EXPECT_RESULT(nkgpu_batch_seal(image_batch), NKGPU_OK);
        /* Drop the caller's reference: only the batch keeps the image alive. */
        EXPECT_RESULT(nk_graphics_image_release(composite_image), NK_OK);
        EXPECT_RESULT(nkgpu_batch_submit(renderer, image_batch), NKGPU_OK);
        {
            uint8_t pixel[4]{};
            glReadPixels(window_options.width / 2, window_options.height / 2, 1, 1, GL_RGBA,
                         GL_UNSIGNED_BYTE, pixel);
            if (pixel[0] < 200 || pixel[1] > 40 || pixel[2] > 40 || pixel[3] != 255) {
                std::fprintf(stderr, "composited pixel was (%u,%u,%u,%u)\n", pixel[0], pixel[1],
                             pixel[2], pixel[3]);
                result = __LINE__;
                goto cleanup;
            }
        }
        EXPECT_RESULT(nkgpu_batch_destroy(image_batch), NKGPU_OK);
        /* The batch held the last reference, so the image handle is now dead. */
        {
            nk_graphics_image_info info{};
            info.struct_size = sizeof(info);
            EXPECT_RESULT(nk_graphics_image_get_info(composite_image, &info),
                          NK_ERROR_INVALID_HANDLE);
        }
        EXPECT_RESULT(nkgpu_buffer_destroy(renderer, textured_buffer), NKGPU_OK);
        textured_buffer = {};
        EXPECT_RESULT(nkgpu_sampler_destroy(renderer, sampler), NKGPU_OK);
        sampler = {};
        EXPECT_RESULT(nkgpu_pipeline_destroy(renderer, textured_pipeline), NKGPU_OK);
        textured_pipeline = {};
        EXPECT_RESULT(nkgpu_shader_destroy(renderer, textured_shader), NKGPU_OK);
        textured_shader = {};
    }

    /* A batch belongs to the renderer that recorded it. */
    {
        nkgpu_renderer foreign{};
        nkgpu_buffer foreign_buffer{};
        const float vertices[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
        EXPECT_RESULT(nkgpu_renderer_create(surface, &foreign), NKGPU_OK);
        EXPECT_RESULT(nkgpu_buffer_create(foreign, reinterpret_cast<const uint8_t *>(vertices),
                                          sizeof(vertices), &foreign_buffer),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_submit(foreign, batch), NKGPU_ERROR_INVALID_HANDLE);
        /* Recording another renderer's resources is rejected, not deferred. */
        {
            nkgpu_batch foreign_batch{};
            EXPECT_RESULT(nkgpu_batch_begin(renderer, &foreign_batch), NKGPU_OK);
            nkgpu_batch_pass pass{};
            pass.struct_size = sizeof(pass);
            pass.kind = NKGPU_BATCH_PASS_WINDOW;
            pass.clear = 1;
            pass.width = window_options.width;
            pass.height = window_options.height;
            EXPECT_RESULT(nkgpu_batch_append_pass(foreign_batch, &pass), NKGPU_OK);
            std::vector<uint8_t> commands;
            append_apply_vertex_buffer(commands, 0, foreign_buffer, 0);
            EXPECT_RESULT(nkgpu_batch_append_command(foreign_batch, commands.data(),
                                                     static_cast<uint32_t>(commands.size())),
                          NKGPU_ERROR_INVALID_ARGUMENT);
            EXPECT_RESULT(nkgpu_batch_destroy(foreign_batch), NKGPU_OK);
        }
        EXPECT_RESULT(nkgpu_buffer_destroy(foreign, foreign_buffer), NKGPU_OK);
        EXPECT_RESULT(nkgpu_renderer_destroy(foreign), NKGPU_OK);
    }

    /*
     * Records are validated, including handle resolution, before they are
     * appended: a stale handle is rejected at append time.
     */
    {
        nkgpu_batch stale_batch{};
        EXPECT_RESULT(nkgpu_batch_begin(renderer, &stale_batch), NKGPU_OK);
        nkgpu_batch_pass pass{};
        pass.struct_size = sizeof(pass);
        pass.kind = NKGPU_BATCH_PASS_WINDOW;
        pass.clear = 1;
        pass.width = window_options.width;
        pass.height = window_options.height;
        EXPECT_RESULT(nkgpu_batch_append_pass(stale_batch, &pass), NKGPU_OK);
        std::vector<uint8_t> commands;
        append_apply_pipeline(commands, nkgpu_pipeline{0x0BAD0001u});
        EXPECT_RESULT(nkgpu_batch_append_command(stale_batch, commands.data(),
                                                 static_cast<uint32_t>(commands.size())),
                      NKGPU_ERROR_INVALID_ARGUMENT);
        std::vector<uint8_t> truncated;
        append_u32(truncated, NKGPU_COMMAND_DRAW);
        append_u32(truncated, 8);
        EXPECT_RESULT(nkgpu_batch_append_command(stale_batch, truncated.data(),
                                                 static_cast<uint32_t>(truncated.size())),
                      NKGPU_ERROR_INVALID_ARGUMENT);
        /* A rejected append leaves the batch unchanged and empty. */
        EXPECT_RESULT(nkgpu_batch_seal(stale_batch), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_destroy(stale_batch), NKGPU_OK);
    }

    /* An unsealed batch cannot be submitted; a pass-less batch cannot be sealed. */
    {
        nkgpu_batch empty_batch{};
        EXPECT_RESULT(nkgpu_batch_begin(renderer, &empty_batch), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_seal(empty_batch), NKGPU_ERROR_WRONG_STATE);
        EXPECT_RESULT(nkgpu_batch_submit(renderer, empty_batch), NKGPU_ERROR_WRONG_STATE);
        EXPECT_RESULT(nkgpu_batch_destroy(empty_batch), NKGPU_OK);
    }

    /*
     * Submission is render-executor work. While the platform, application, and
     * render executors share the init thread the main thread satisfies all of
     * them, so the contract bites on any other thread.
     */
    {
        nkgpu_batch worker_batch{};
        EXPECT_RESULT(nkgpu_batch_begin(renderer, &worker_batch), NKGPU_OK);
        nkgpu_batch_pass pass{};
        pass.struct_size = sizeof(pass);
        pass.kind = NKGPU_BATCH_PASS_WINDOW;
        pass.clear = 1;
        pass.width = window_options.width;
        pass.height = window_options.height;
        EXPECT_RESULT(nkgpu_batch_append_pass(worker_batch, &pass), NKGPU_OK);
        std::vector<uint8_t> commands;
        append_apply_pipeline(commands, pipeline);
        append_apply_vertex_buffer(commands, 0, buffer, 0);
        append_draw(commands, 0, 3, 1);
        EXPECT_RESULT(nkgpu_batch_append_command(worker_batch, commands.data(),
                                                 static_cast<uint32_t>(commands.size())),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_seal(worker_batch), NKGPU_OK);
        std::atomic<nkgpu_result> worker_result{NKGPU_OK};
        std::thread worker([&] { worker_result = nkgpu_batch_submit(renderer, worker_batch); });
        worker.join();
        if (worker_result.load() != NKGPU_ERROR_WRONG_THREAD) {
            std::fprintf(stderr, "worker-thread batch submit returned %d\n",
                         static_cast<int>(worker_result.load()));
            result = __LINE__;
            goto cleanup;
        }
        /* The same batch still submits on the render executor. */
        EXPECT_RESULT(nkgpu_batch_submit(renderer, worker_batch), NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_destroy(worker_batch), NKGPU_OK);
    }

    EXPECT_RESULT(nkgpu_batch_destroy(batch), NKGPU_OK);
    batch = {};
    EXPECT_RESULT(nkgpu_render_target_destroy(renderer, target), NKGPU_OK);
    target = {};
    EXPECT_RESULT(nk_graphics_image_release(retained_image), NK_OK);
    retained_image = {};

cleanup:
    nkgpu_test_allow_surface_target_queries();
    if (batch.id)
        nkgpu_batch_destroy(batch);
    if (general_batch.id)
        nkgpu_batch_destroy(general_batch);
    if (retained_image.id)
        nk_graphics_image_release(retained_image);
    if (renderer.id) {
        nkgpu_end_pass(renderer);
        nkgpu_end_frame(renderer);
    }
    if (pipeline.id && renderer.id)
        nkgpu_pipeline_destroy(renderer, pipeline);
    if (buffer.id && renderer.id)
        nkgpu_buffer_destroy(renderer, buffer);
    if (shader.id && renderer.id)
        nkgpu_shader_destroy(renderer, shader);
    if (textured_pipeline.id && renderer.id)
        nkgpu_pipeline_destroy(renderer, textured_pipeline);
    if (textured_buffer.id && renderer.id)
        nkgpu_buffer_destroy(renderer, textured_buffer);
    if (textured_shader.id && renderer.id)
        nkgpu_shader_destroy(renderer, textured_shader);
    if (sampler.id && renderer.id)
        nkgpu_sampler_destroy(renderer, sampler);
    if (target.id && renderer.id)
        nkgpu_render_target_destroy(renderer, target);
    if (general_depth.id && renderer.id)
        nkgpu_image_destroy(renderer, general_depth);
    if (general_color.id && renderer.id)
        nkgpu_image_destroy(renderer, general_color);
    if (renderer.id)
        nkgpu_renderer_destroy(renderer);
    if (surface_created)
        nk_surface_destroy(surface);
    if (window_created)
        nk_window_destroy(window);
    nk_shutdown();
    return result;
}

#undef EXPECT_RESULT
