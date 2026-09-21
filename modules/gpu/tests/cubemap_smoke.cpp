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
    nkgpu_image cube{};
    nkgpu_image cube_array{};
    nkgpu_image target{};
    nkgpu_buffer vertices{};
    nkgpu_sampler sampler{};
    nkgpu_shader cube_shader{};
    nkgpu_shader cube_array_shader{};
    nkgpu_pipeline cube_pipeline{};
    nkgpu_pipeline cube_array_pipeline{};
    nk_surface surface = 0;
    nk_window window = 0;
    bool initialized = false;

    ~TestResources() {
        if (cube_array_pipeline.id)
            nkgpu_pipeline_destroy(renderer, cube_array_pipeline);
        if (cube_pipeline.id)
            nkgpu_pipeline_destroy(renderer, cube_pipeline);
        if (cube_array_shader.id)
            nkgpu_shader_destroy(renderer, cube_array_shader);
        if (cube_shader.id)
            nkgpu_shader_destroy(renderer, cube_shader);
        if (sampler.id)
            nkgpu_sampler_destroy(renderer, sampler);
        if (vertices.id)
            nkgpu_buffer_destroy(renderer, vertices);
        if (target.id)
            nkgpu_image_destroy(renderer, target);
        if (cube_array.id)
            nkgpu_image_destroy(renderer, cube_array);
        if (cube.id)
            nkgpu_image_destroy(renderer, cube);
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

struct ShaderSources {
    nkgpu_shader_language language;
    const char *vertex;
    const char *cube_fragment;
    const char *cube_array_fragment;
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

bool select_sources(nk_graphics_api api, ShaderSources &out) {
    switch (api) {
    case NK_GRAPHICS_OPENGL:
        out = {NKGPU_SHADERLANGUAGE_GLSL,
               "#version 330\n"
               "layout(location=0) in vec2 position;\n"
               "void main(){gl_Position=vec4(position,0.0,1.0);}\n",
               "#version 330\n"
               "uniform samplerCube tex;\n"
               "out vec4 frag_color;\n"
               "void main(){frag_color=texture(tex,vec3(1.0,0.0,0.0));}\n",
               "#version 330\n"
               "uniform sampler2DArray tex;\n"
               "out vec4 frag_color;\n"
               "void main(){frag_color=texture(tex,vec3(0.5,0.5,6.0));}\n"};
        return true;
    case NK_GRAPHICS_OPENGL_ES:
        out = {NKGPU_SHADERLANGUAGE_GLSL,
               "#version 300 es\n"
               "precision highp float;\n"
               "layout(location=0) in vec2 position;\n"
               "void main(){gl_Position=vec4(position,0.0,1.0);}\n",
               "#version 300 es\n"
               "precision highp float;\n"
               "uniform samplerCube tex;\n"
               "out vec4 frag_color;\n"
               "void main(){frag_color=texture(tex,vec3(1.0,0.0,0.0));}\n",
               "#version 300 es\n"
               "precision highp float;\n"
               "uniform sampler2DArray tex;\n"
               "out vec4 frag_color;\n"
               "void main(){frag_color=texture(tex,vec3(0.5,0.5,6.0));}\n"};
        return true;
    case NK_GRAPHICS_D3D11:
        out = {NKGPU_SHADERLANGUAGE_HLSL5,
               "struct VSIn { float2 position : POSITION; };\n"
               "struct VSOut { float4 position : SV_Position; };\n"
               "VSOut main(VSIn input){ VSOut output; output.position=float4(input.position,0,1);"
               " return output; }\n",
               "struct VSOut { float4 position : SV_Position; };\n"
               "TextureCube tex : register(t0);\n"
               "SamplerState smp : register(s0);\n"
               "float4 main(VSOut input) : SV_Target { return tex.Sample(smp,float3(1,0,0)); }\n",
               "struct VSOut { float4 position : SV_Position; };\n"
               "Texture2DArray tex : register(t0);\n"
               "SamplerState smp : register(s0);\n"
               "float4 main(VSOut input) : SV_Target { return tex.Sample(smp,float3(0.5,0.5,6)); }\n"};
        return true;
    case NK_GRAPHICS_METAL:
        out = {NKGPU_SHADERLANGUAGE_MSL,
               "#include <metal_stdlib>\n"
               "using namespace metal;\n"
               "struct VertexIn { float2 position [[attribute(0)]]; };\n"
               "struct VertexOut { float4 position [[position]]; };\n"
               "vertex VertexOut main0(VertexIn input) {\n"
               "    VertexOut output; output.position=float4(input.position,0,1); return output;\n"
               "}\n",
               "#include <metal_stdlib>\n"
               "using namespace metal;\n"
               "struct VertexOut { float4 position [[position]]; };\n"
               "fragment float4 main0(VertexOut input [[stage_in]],\n"
               "                       texturecube<float> tex [[texture(0)]],\n"
               "                       sampler smp [[sampler(0)]]) {\n"
               "    return tex.sample(smp,float3(1,0,0));\n"
               "}\n",
               "#include <metal_stdlib>\n"
               "using namespace metal;\n"
               "struct VertexOut { float4 position [[position]]; };\n"
               "fragment float4 main0(VertexOut input [[stage_in]],\n"
               "                       texture2d_array<float> tex [[texture(0)]],\n"
               "                       sampler smp [[sampler(0)]]) {\n"
               "    return tex.sample(smp,float3(0.5,0.5,6));\n"
               "}\n"};
        return true;
    default:
        return false;
    }
}

bool create_pipeline(TestResources &resources, const ShaderSources &sources, const char *fragment,
                     nkgpu_image_type image_type, nkgpu_shader *out_shader,
                     nkgpu_pipeline *out_pipeline) {
    nkgpu_shader_builder shader_builder{};
    if (!expect_result(nkgpu_shader_begin(resources.renderer, sources.language, sources.vertex,
                                          fragment, &shader_builder),
                       NKGPU_OK, "nkgpu_shader_begin(cube)"))
        return false;

    const char *glsl_name = sources.language == NKGPU_SHADERLANGUAGE_GLSL ? "position" : nullptr;
    const char *hlsl_semantic =
        sources.language == NKGPU_SHADERLANGUAGE_HLSL5 ? "POSITION" : nullptr;
    if (!expect_result(nkgpu_shader_attribute(shader_builder, 0, glsl_name, hlsl_semantic, 0),
                       NKGPU_OK, "nkgpu_shader_attribute(cube)"))
        return false;

    nkgpu_shader_binding_desc image_binding{};
    image_binding.struct_size = sizeof(image_binding);
    image_binding.kind = NKGPU_SHADERBINDING_SAMPLED_IMAGE;
    image_binding.stage = NKGPU_SHADERSTAGE_FRAGMENT;
    image_binding.slot = 0;
    image_binding.secondary_slot = 0;
    image_binding.name = "tex";
    image_binding.image_type = image_type;
    if (!expect_result(nkgpu_shader_binding(shader_builder, &image_binding), NKGPU_OK,
                       "nkgpu_shader_binding(cube image)"))
        return false;

    image_binding.kind = NKGPU_SHADERBINDING_SAMPLER;
    if (!expect_result(nkgpu_shader_binding(shader_builder, &image_binding), NKGPU_OK,
                       "nkgpu_shader_binding(cube sampler)"))
        return false;
    if (!expect_result(nkgpu_shader_end(shader_builder, out_shader), NKGPU_OK,
                       "nkgpu_shader_end(cube)"))
        return false;

    nkgpu_pipeline_builder pipeline_builder{};
    if (!expect_result(nkgpu_pipeline_begin(resources.renderer, *out_shader, 2 * sizeof(float),
                                            &pipeline_builder),
                       NKGPU_OK, "nkgpu_pipeline_begin(cube)") ||
        !expect_result(nkgpu_pipeline_attribute(pipeline_builder, 0, 0, 0,
                                                NKGPU_VERTEXFORMAT_FLOAT2),
                       NKGPU_OK, "nkgpu_pipeline_attribute(cube)") ||
        !expect_result(nkgpu_pipeline_color_target(pipeline_builder, 0,
                                                   NKGPU_IMAGEFORMAT_RGBA8,
                                                   NKGPU_COLORMASK_RGBA, nullptr),
                       NKGPU_OK, "nkgpu_pipeline_color_target(cube)") ||
        !expect_result(nkgpu_pipeline_depth_stencil(pipeline_builder, 0), NKGPU_OK,
                       "nkgpu_pipeline_depth_stencil(cube)") ||
        !expect_result(nkgpu_pipeline_end(pipeline_builder, out_pipeline), NKGPU_OK,
                       "nkgpu_pipeline_end(cube)"))
        return false;
    return true;
}

bool readback_pixel(TestResources &resources, const uint8_t expected[4]) {
    nkgpu_image_readback_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.image = resources.target;
    desc.width = 1;
    desc.height = 1;
    nkgpu_readback readback{};
    if (!expect_result(nkgpu_readback_begin_image(resources.renderer, &desc, &readback), NKGPU_OK,
                       "nkgpu_readback_begin_image(cube)"))
        return false;

    nkgpu_readback_info info{};
    info.struct_size = sizeof(info);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!expect_result(nkgpu_readback_query(resources.renderer, readback, &info), NKGPU_OK,
                           "nkgpu_readback_query(cube)"))
            break;
        if (info.state != NKGPU_READBACK_PENDING)
            break;
        std::this_thread::yield();
    }
    uint8_t pixel[4]{};
    uint32_t size = 0;
    const bool success = info.state == NKGPU_READBACK_READY && info.size == sizeof(pixel) &&
                         nkgpu_readback_read(resources.renderer, readback, pixel, sizeof(pixel),
                                             &size) == NKGPU_OK &&
                         size == sizeof(pixel) && std::memcmp(pixel, expected, sizeof(pixel)) == 0;
    if (!expect_result(nkgpu_readback_destroy(resources.renderer, readback), NKGPU_OK,
                       "nkgpu_readback_destroy(cube)"))
        return false;
    if (!success)
        std::fprintf(stderr, "cubemap sample was (%u,%u,%u,%u)\n", pixel[0], pixel[1], pixel[2],
                     pixel[3]);
    return success;
}

bool sample(TestResources &resources, nkgpu_pipeline pipeline, nkgpu_image image,
            const uint8_t expected[4]) {
    nkgpu_render_pass_desc pass{};
    pass.struct_size = sizeof(pass);
    pass.color_count = 1;
    pass.colors[0].image = resources.target;
    pass.colors[0].action.load_action = NKGPU_LOADACTION_CLEAR;
    pass.colors[0].action.store_action = NKGPU_STOREACTION_STORE;
    pass.colors[0].action.clear_color = {0.0f, 0.0f, 0.0f, 1.0f};
    return expect_result(nkgpu_frame_begin(resources.renderer), NKGPU_OK,
                         "nkgpu_frame_begin(cube)") &&
           expect_result(nkgpu_begin_render_pass(resources.renderer, &pass), NKGPU_OK,
                         "nkgpu_begin_render_pass(cube)") &&
           expect_result(nkgpu_apply_pipeline(resources.renderer, pipeline), NKGPU_OK,
                         "nkgpu_apply_pipeline(cube)") &&
           expect_result(nkgpu_apply_vertex_buffer(resources.renderer, 0, resources.vertices, 0),
                         NKGPU_OK, "nkgpu_apply_vertex_buffer(cube)") &&
           expect_result(nkgpu_apply_image(resources.renderer, 0, image), NKGPU_OK,
                         "nkgpu_apply_image(cube)") &&
           expect_result(nkgpu_apply_sampler(resources.renderer, 0, resources.sampler), NKGPU_OK,
                         "nkgpu_apply_sampler(cube)") &&
           expect_result(nkgpu_draw(resources.renderer, 0, 3, 1), NKGPU_OK,
                         "nkgpu_draw(cube)") &&
           expect_result(nkgpu_end_pass(resources.renderer), NKGPU_OK,
                         "nkgpu_end_pass(cube)") &&
           expect_result(nkgpu_end_frame(resources.renderer), NKGPU_OK,
                         "nkgpu_end_frame(cube)") &&
           readback_pixel(resources, expected);
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
    window_options.title = "NativeKit GPU cubemap smoke";
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
    nkgpu_image_format_support support{};
    support.struct_size = sizeof(support);
    if (!expect_result(nkgpu_query_image_format_support(resources.renderer,
                                                        NKGPU_IMAGEFORMAT_RGBA8, &support),
                       NKGPU_OK, "nkgpu_query_image_format_support(cube)"))
        return 1;
    if (!features.image_readback || !support.sampled || !support.render_target) {
        std::fprintf(stderr, "cubemap sampling prerequisites are unavailable; skipping\n");
        return 0;
    }

    ShaderSources sources{};
    if (!select_sources(nkgpu_query_graphics_api(resources.renderer), sources)) {
        std::fprintf(stderr, "cubemap shader backend is unavailable; skipping\n");
        return 0;
    }

    const uint8_t cube_pixels[] = {
        255, 0,   0,   255, /* +X */
        0,   255, 0,   255, /* -X */
        0,   0,   255, 255, /* +Y */
        255, 255, 0, 255, /* -Y */
        255, 0,   255, 255, /* +Z */
        0,   255, 255, 255, /* -Z */
    };
    const uint8_t cube_array_pixels[] = {
        16,  17,  18,  255, 32,  33,  34,  255, 48,  49,  50,  255,
        64,  65,  66,  255, 80,  81,  82,  255, 96,  97,  98,  255,
        160, 161, 162, 255, 112, 113, 114, 255, 128, 129, 130, 255,
        144, 145, 146, 255, 176, 177, 178, 255, 192, 193, 194, 255,
    };
    nkgpu_image_desc cube_desc{};
    cube_desc.struct_size = sizeof(cube_desc);
    cube_desc.width = 1;
    cube_desc.height = 1;
    cube_desc.format = NKGPU_IMAGEFORMAT_RGBA8;
    cube_desc.usage = NKGPU_IMAGE_SAMPLED;
    cube_desc.layer_count = 6;
    cube_desc.data = cube_pixels;
    cube_desc.data_size = sizeof(cube_pixels);
    cube_desc.type = NKGPU_IMAGETYPE_CUBE;
    if (!expect_result(nkgpu_image_create_desc(resources.renderer, &cube_desc, &resources.cube),
                       NKGPU_OK, "nkgpu_image_create_desc(cube)"))
        return 1;

    nkgpu_image_desc cube_array_desc = cube_desc;
    cube_array_desc.layer_count = 12;
    cube_array_desc.data = cube_array_pixels;
    cube_array_desc.data_size = sizeof(cube_array_pixels);
    cube_array_desc.type = NKGPU_IMAGETYPE_CUBE_ARRAY;
    if (!expect_result(nkgpu_image_create_desc(resources.renderer, &cube_array_desc,
                                               &resources.cube_array),
                       NKGPU_OK, "nkgpu_image_create_desc(cube array)"))
        return 1;

    nkgpu_image_desc target_desc{};
    target_desc.struct_size = sizeof(target_desc);
    target_desc.width = 1;
    target_desc.height = 1;
    target_desc.format = NKGPU_IMAGEFORMAT_RGBA8;
    target_desc.usage = NKGPU_IMAGE_SAMPLED | NKGPU_IMAGE_RENDER_TARGET;
    if (!expect_result(nkgpu_image_create_desc(resources.renderer, &target_desc, &resources.target),
                       NKGPU_OK, "nkgpu_image_create_desc(target)"))
        return 1;

    const float vertices[] = {-1.0f, -1.0f, 3.0f, -1.0f, -1.0f, 3.0f};
    if (!expect_result(nkgpu_buffer_create(resources.renderer,
                                           reinterpret_cast<const uint8_t *>(vertices),
                                           sizeof(vertices), &resources.vertices),
                       NKGPU_OK, "nkgpu_buffer_create(vertices)") ||
        !expect_result(nkgpu_sampler_create(resources.renderer, NKGPU_FILTER_NEAREST,
                                            NKGPU_FILTER_NEAREST, NKGPU_WRAP_CLAMP_TO_EDGE,
                                            NKGPU_WRAP_CLAMP_TO_EDGE, &resources.sampler),
                       NKGPU_OK, "nkgpu_sampler_create(cube)"))
        return 1;

    if (!create_pipeline(resources, sources, sources.cube_fragment, NKGPU_IMAGETYPE_CUBE,
                         &resources.cube_shader, &resources.cube_pipeline) ||
        !create_pipeline(resources, sources, sources.cube_array_fragment,
                         NKGPU_IMAGETYPE_CUBE_ARRAY, &resources.cube_array_shader,
                         &resources.cube_array_pipeline))
        return 1;

    const uint8_t cube_expected[] = {255, 0, 0, 255};
    const uint8_t cube_array_expected[] = {160, 161, 162, 255};
    if (!sample(resources, resources.cube_pipeline, resources.cube, cube_expected) ||
        !sample(resources, resources.cube_array_pipeline, resources.cube_array,
                cube_array_expected))
        return 1;
    return 0;
}
