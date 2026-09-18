#include "nativekit.h"
#include "nativekit_gpu.h"
#include "nativekit_window.h"
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
    nkgpu_batch batch{};
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
        EXPECT_RESULT(nkgpu_batch_append_command(batch, commands.data(),
                                                 static_cast<uint32_t>(commands.size())),
                      NKGPU_OK);

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
        append_apply_pipeline(commands, pipeline);
        append_apply_vertex_buffer(commands, 0, buffer, 0);
        append_draw(commands, 0, 3, 1);
        EXPECT_RESULT(nkgpu_batch_append_command(batch, commands.data(),
                                                 static_cast<uint32_t>(commands.size())),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_batch_seal(batch), NKGPU_OK);
        /* Destroy the caller's references; the batch retains both. */
        EXPECT_RESULT(nkgpu_pipeline_destroy(renderer, pipeline), NKGPU_OK);
        pipeline = {};
        EXPECT_RESULT(nkgpu_buffer_destroy(renderer, buffer), NKGPU_OK);
        buffer = {};
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
        const float vertices[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
        EXPECT_RESULT(nkgpu_buffer_create(renderer, reinterpret_cast<const uint8_t *>(vertices),
                                          sizeof(vertices), &buffer),
                      NKGPU_OK);
        EXPECT_RESULT(nkgpu_buffer_destroy(renderer, buffer), NKGPU_OK);
        buffer = {};
    }

    /* An offscreen pass records and replays like a window pass. */
    EXPECT_RESULT(nkgpu_render_target_create(renderer, 16, 16, 0, &target), NKGPU_OK);
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

    EXPECT_RESULT(nkgpu_batch_destroy(batch), NKGPU_OK);
    batch = {};
    EXPECT_RESULT(nkgpu_render_target_destroy(renderer, target), NKGPU_OK);
    target = {};
    EXPECT_RESULT(nk_graphics_image_release(retained_image), NK_OK);
    retained_image = {};

cleanup:
    if (batch.id)
        nkgpu_batch_destroy(batch);
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
    if (target.id && renderer.id)
        nkgpu_render_target_destroy(renderer, target);
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
