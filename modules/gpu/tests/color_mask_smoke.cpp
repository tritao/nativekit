#include "nativekit.h"
#include "nativekit_gpu.h"
#include "nativekit_window.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

// Colour write masks with several render targets: NKGPU_COLORMASK_NONE on one target writes nothing
// to it, and nkgpu_pipeline_color_write_mask applies to every target. (sokol reads a zero mask as
// "all channels"; passing it through once let stencil-only cap steps overwrite a pick pass's extra
// targets.)
namespace {

struct TestResources {
    nkgpu_renderer renderer{};
    nkgpu_image targets[2]{};
    nkgpu_buffer vertices{};
    nkgpu_shader shader{};
    nkgpu_pipeline pipelines[2]{};
    nk_surface surface = 0;
    nk_window window = 0;
    bool initialized = false;

    ~TestResources() {
        for (auto &pipeline : pipelines)
            if (pipeline.id)
                nkgpu_pipeline_destroy(renderer, pipeline);
        if (shader.id)
            nkgpu_shader_destroy(renderer, shader);
        if (vertices.id)
            nkgpu_buffer_destroy(renderer, vertices);
        for (auto &target : targets)
            if (target.id)
                nkgpu_image_destroy(renderer, target);
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

bool read_pixel(TestResources &resources, nkgpu_image image, uint8_t out[4]) {
    nkgpu_image_readback_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.image = image;
    desc.width = 1;
    desc.height = 1;
    nkgpu_readback readback{};
    if (!expect_result(nkgpu_readback_begin_image(resources.renderer, &desc, &readback), NKGPU_OK,
                       "nkgpu_readback_begin_image"))
        return false;
    nkgpu_readback_info info{};
    info.struct_size = sizeof(info);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (nkgpu_readback_query(resources.renderer, readback, &info) != NKGPU_OK ||
            info.state != NKGPU_READBACK_PENDING)
            break;
        std::this_thread::yield();
    }
    uint32_t size = 0;
    const bool success =
        info.state == NKGPU_READBACK_READY && info.size == 4 &&
        nkgpu_readback_read(resources.renderer, readback, out, 4, &size) == NKGPU_OK && size == 4;
    nkgpu_readback_destroy(resources.renderer, readback);
    return success;
}

// Clears both targets to blue, draws red to target 0 and green to target 1, and checks what each
// target holds.
bool draw_and_check(TestResources &resources, nkgpu_pipeline pipeline, const uint8_t expected0[4],
                    const uint8_t expected1[4], const char *label) {
    nkgpu_render_pass_desc pass{};
    pass.struct_size = sizeof(pass);
    pass.color_count = 2;
    for (int index = 0; index < 2; ++index) {
        pass.colors[index].image = resources.targets[index];
        pass.colors[index].action.load_action = NKGPU_LOADACTION_CLEAR;
        pass.colors[index].action.store_action = NKGPU_STOREACTION_STORE;
        pass.colors[index].action.clear_color = {0.0f, 0.0f, 1.0f, 1.0f};
    }
    if (!expect_result(nkgpu_frame_begin(resources.renderer), NKGPU_OK, "nkgpu_frame_begin") ||
        !expect_result(nkgpu_begin_render_pass(resources.renderer, &pass), NKGPU_OK,
                       "nkgpu_begin_render_pass") ||
        !expect_result(nkgpu_apply_pipeline(resources.renderer, pipeline), NKGPU_OK,
                       "nkgpu_apply_pipeline") ||
        !expect_result(nkgpu_apply_vertex_buffer(resources.renderer, 0, resources.vertices, 0),
                       NKGPU_OK, "nkgpu_apply_vertex_buffer") ||
        !expect_result(nkgpu_draw(resources.renderer, 0, 3, 1), NKGPU_OK, "nkgpu_draw") ||
        !expect_result(nkgpu_end_pass(resources.renderer), NKGPU_OK, "nkgpu_end_pass") ||
        !expect_result(nkgpu_end_frame(resources.renderer), NKGPU_OK, "nkgpu_end_frame"))
        return false;
    uint8_t pixel0[4]{}, pixel1[4]{};
    if (!read_pixel(resources, resources.targets[0], pixel0) ||
        !read_pixel(resources, resources.targets[1], pixel1))
        return false;
    const bool success =
        std::memcmp(pixel0, expected0, 4) == 0 && std::memcmp(pixel1, expected1, 4) == 0;
    if (!success)
        std::fprintf(stderr, "%s: target 0 (%u,%u,%u,%u), target 1 (%u,%u,%u,%u)\n", label,
                     pixel0[0], pixel0[1], pixel0[2], pixel0[3], pixel1[0], pixel1[1], pixel1[2],
                     pixel1[3]);
    return success;
}

bool make_pipeline(TestResources &resources, nkgpu_color_write_mask mask0,
                   nkgpu_color_write_mask mask1, bool mask_all_none, nkgpu_pipeline *out) {
    nkgpu_pipeline_builder builder{};
    return expect_result(nkgpu_pipeline_begin(resources.renderer, resources.shader,
                                              2 * sizeof(float), &builder),
                         NKGPU_OK, "nkgpu_pipeline_begin") &&
           expect_result(nkgpu_pipeline_attribute(builder, 0, 0, 0, NKGPU_VERTEXFORMAT_FLOAT2),
                         NKGPU_OK, "nkgpu_pipeline_attribute") &&
           expect_result(
               nkgpu_pipeline_color_target(builder, 0, NKGPU_IMAGEFORMAT_RGBA8, mask0, nullptr),
               NKGPU_OK, "nkgpu_pipeline_color_target(0)") &&
           expect_result(
               nkgpu_pipeline_color_target(builder, 1, NKGPU_IMAGEFORMAT_RGBA8, mask1, nullptr),
               NKGPU_OK, "nkgpu_pipeline_color_target(1)") &&
           (!mask_all_none ||
            expect_result(nkgpu_pipeline_color_write_mask(builder, NKGPU_COLORMASK_NONE), NKGPU_OK,
                          "nkgpu_pipeline_color_write_mask")) &&
           expect_result(nkgpu_pipeline_depth_stencil(builder, 0), NKGPU_OK,
                         "nkgpu_pipeline_depth_stencil") &&
           expect_result(nkgpu_pipeline_end(builder, out), NKGPU_OK, "nkgpu_pipeline_end");
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
    window_options.title = "NativeKit GPU colour mask smoke";
    if (nk_window_create(&window_options, &resources.window) != NK_OK ||
        nkgpu_surface_create(resources.window, window_options.width, window_options.height,
                             &resources.surface) != NKGPU_OK ||
        !wait_for_surface(resources.surface) ||
        !expect_result(nkgpu_renderer_create(resources.surface, &resources.renderer), NKGPU_OK,
                       "nkgpu_renderer_create"))
        return 1;

    const nk_graphics_api api = nkgpu_query_graphics_api(resources.renderer);
    if (api != NK_GRAPHICS_OPENGL && api != NK_GRAPHICS_OPENGL_ES) {
        std::fprintf(stderr, "colour mask smoke covers the GLSL backends; skipping\n");
        return 0;
    }
    const bool gles = api == NK_GRAPHICS_OPENGL_ES;
    const char *vertex =
        gles ? "#version 300 es\nprecision highp float;\nlayout(location=0) in vec2 position;\n"
               "void main(){gl_Position=vec4(position,0.0,1.0);}\n"
             : "#version 330\nlayout(location=0) in vec2 position;\n"
               "void main(){gl_Position=vec4(position,0.0,1.0);}\n";
    const char *fragment =
        gles ? "#version 300 es\nprecision highp float;\n"
               "layout(location=0) out vec4 first;\nlayout(location=1) out vec4 second;\n"
               "void main(){first=vec4(1.0,0.0,0.0,1.0);second=vec4(0.0,1.0,0.0,1.0);}\n"
             : "#version 330\n"
               "layout(location=0) out vec4 first;\nlayout(location=1) out vec4 second;\n"
               "void main(){first=vec4(1.0,0.0,0.0,1.0);second=vec4(0.0,1.0,0.0,1.0);}\n";
    nkgpu_shader_builder shader_builder{};
    if (!expect_result(nkgpu_shader_begin(resources.renderer, NKGPU_SHADERLANGUAGE_GLSL, vertex,
                                          fragment, &shader_builder),
                       NKGPU_OK, "nkgpu_shader_begin") ||
        !expect_result(nkgpu_shader_attribute(shader_builder, 0, "position", nullptr, 0), NKGPU_OK,
                       "nkgpu_shader_attribute") ||
        !expect_result(nkgpu_shader_end(shader_builder, &resources.shader), NKGPU_OK,
                       "nkgpu_shader_end"))
        return 1;

    for (auto &target : resources.targets) {
        nkgpu_image_desc desc{};
        desc.struct_size = sizeof(desc);
        desc.width = 1;
        desc.height = 1;
        desc.format = NKGPU_IMAGEFORMAT_RGBA8;
        desc.usage = NKGPU_IMAGE_SAMPLED | NKGPU_IMAGE_RENDER_TARGET;
        if (!expect_result(nkgpu_image_create_desc(resources.renderer, &desc, &target), NKGPU_OK,
                           "nkgpu_image_create_desc"))
            return 1;
    }
    const float triangle[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    if (!expect_result(nkgpu_buffer_create(resources.renderer,
                                           reinterpret_cast<const uint8_t *>(triangle),
                                           sizeof(triangle), &resources.vertices),
                       NKGPU_OK, "nkgpu_buffer_create"))
        return 1;

    const uint8_t red[4] = {255, 0, 0, 255}, blue[4] = {0, 0, 255, 255};
    if (!make_pipeline(resources, NKGPU_COLORMASK_RGBA, NKGPU_COLORMASK_NONE, false,
                       &resources.pipelines[0]) ||
        !draw_and_check(resources, resources.pipelines[0], red, blue, "per-target NONE"))
        return 1;
    if (!make_pipeline(resources, NKGPU_COLORMASK_RGBA, NKGPU_COLORMASK_RGBA, true,
                       &resources.pipelines[1]) ||
        !draw_and_check(resources, resources.pipelines[1], blue, blue, "pipeline-wide NONE"))
        return 1;
    std::puts("colour mask smoke passed");
    return 0;
}
