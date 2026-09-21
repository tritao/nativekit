#include "nativekit.h"
#include "nativekit_gpu.h"
#include "nativekit_window.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

namespace {

struct TestResources {
    nkgpu_renderer renderer{};
    nkgpu_buffer buffer{};
    nkgpu_shader shader{};
    nkgpu_pipeline pipeline{};
    nkgpu_readback readback{};
    nk_surface surface = 0;
    nk_window window = 0;
    bool initialized = false;

    ~TestResources() {
        if (readback.id)
            nkgpu_readback_destroy(renderer, readback);
        if (pipeline.id)
            nkgpu_pipeline_destroy(renderer, pipeline);
        if (shader.id)
            nkgpu_shader_destroy(renderer, shader);
        if (buffer.id)
            nkgpu_buffer_destroy(renderer, buffer);
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

bool readback_range(TestResources &resources, uint32_t offset, uint32_t size,
                    const uint32_t *expected, uint32_t expected_count) {
    nkgpu_buffer_readback_desc desc{};
    desc.struct_size = sizeof(desc);
    desc.buffer = resources.buffer;
    desc.offset = offset;
    desc.size = size;
    if (!expect_result(nkgpu_readback_begin_buffer(resources.renderer, &desc,
                                                   &resources.readback),
                       NKGPU_OK, "nkgpu_readback_begin_buffer(compute)"))
        return false;

    nkgpu_readback_info info{};
    info.struct_size = sizeof(info);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!expect_result(nkgpu_readback_query(resources.renderer, resources.readback, &info),
                           NKGPU_OK, "nkgpu_readback_query(compute)"))
            return false;
        if (info.state != NKGPU_READBACK_PENDING)
            break;
        std::this_thread::yield();
    }

    if (info.state != NKGPU_READBACK_READY || info.size != size || info.row_pitch != size ||
        info.width != size || info.height != 1) {
        std::fprintf(stderr, "compute readback returned an invalid range description\n");
        return false;
    }

    uint32_t values[16]{};
    uint32_t actual_size = 0;
    if (!expect_result(nkgpu_readback_read(resources.renderer, resources.readback,
                                           reinterpret_cast<uint8_t *>(values), sizeof(values),
                                           &actual_size),
                       NKGPU_OK, "nkgpu_readback_read(compute)"))
        return false;
    if (actual_size != size || expected_count != size / sizeof(uint32_t))
        return false;
    for (uint32_t index = 0; index < expected_count; ++index) {
        if (values[index] != expected[index]) {
            std::fprintf(stderr, "compute readback value %u was %u, expected %u\n", index,
                         values[index], expected[index]);
            return false;
        }
    }
    if (!expect_result(nkgpu_readback_destroy(resources.renderer, resources.readback), NKGPU_OK,
                       "nkgpu_readback_destroy(compute)"))
        return false;
    resources.readback = {};
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
    window_options.title = "NativeKit GPU compute readback smoke";
    const nk_graphics_api requested_api =
#if defined(NKGPU_COMPUTE_READBACK_SMOKE_GLES3)
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
                       "nkgpu_query_features"))
        return 1;
    if (!features.compute || !features.storage_buffer || !features.buffer_readback) {
        std::fprintf(stderr, "compute storage-buffer readback is unavailable; skipping\n");
        return 0;
    }

    const nk_graphics_api graphics_api = nkgpu_query_graphics_api(resources.renderer);
    nkgpu_shader_language language = NKGPU_SHADERLANGUAGE_GLSL;
    const char *source = nullptr;
    switch (graphics_api) {
    case NK_GRAPHICS_OPENGL:
        source = "#version 430\n"
                 "layout(local_size_x=4, local_size_y=1, local_size_z=1) in;\n"
                 "layout(std430, binding=0) buffer Data { uint values[]; };\n"
                 "void main(){ uint i=gl_GlobalInvocationID.x; values[i]=values[i]*3u+1u; }\n";
        break;
    case NK_GRAPHICS_OPENGL_ES:
        source = "#version 310 es\n"
                 "layout(local_size_x=4, local_size_y=1, local_size_z=1) in;\n"
                 "layout(std430, binding=0) buffer Data { uint values[]; };\n"
                 "void main(){ uint i=gl_GlobalInvocationID.x; values[i]=values[i]*3u+1u; }\n";
        break;
    case NK_GRAPHICS_D3D11:
        language = NKGPU_SHADERLANGUAGE_HLSL5;
        source = "RWStructuredBuffer<uint> values : register(u0);\n"
                 "[numthreads(4,1,1)] void main(uint3 id : SV_DispatchThreadID) {\n"
                 "    values[id.x] = values[id.x] * 3 + 1;\n"
                 "}\n";
        break;
    case NK_GRAPHICS_METAL:
        language = NKGPU_SHADERLANGUAGE_MSL;
        source = "#include <metal_stdlib>\n"
                 "using namespace metal;\n"
                 "kernel void main0(device uint* values [[buffer(0)]],\n"
                 "                   uint3 id [[thread_position_in_grid]]) {\n"
                 "    values[id.x] = values[id.x] * 3u + 1u;\n"
                 "}\n";
        break;
    default:
        std::fprintf(stderr, "unsupported compute readback shader backend; skipping\n");
        return 0;
    }

    nkgpu_shader_builder shader_builder{};
    if (!expect_result(nkgpu_shader_begin_compute(resources.renderer, language, source,
                                                  &shader_builder),
                       NKGPU_OK, "nkgpu_shader_begin_compute"))
        return 1;
    nkgpu_shader_binding_desc binding{};
    binding.struct_size = sizeof(binding);
    binding.kind = NKGPU_SHADERBINDING_STORAGE_BUFFER;
    binding.stage = NKGPU_SHADERSTAGE_COMPUTE;
    binding.slot = 0;
    if (!expect_result(nkgpu_shader_binding(shader_builder, &binding), NKGPU_OK,
                       "nkgpu_shader_binding(storage)"))
        return 1;
    if (!expect_result(nkgpu_shader_end(shader_builder, &resources.shader), NKGPU_OK,
                       "nkgpu_shader_end"))
        return 1;

    nkgpu_pipeline_builder pipeline_builder{};
    if (!expect_result(nkgpu_pipeline_begin_compute(resources.renderer, resources.shader,
                                                    &pipeline_builder),
                       NKGPU_OK, "nkgpu_pipeline_begin_compute") ||
        !expect_result(nkgpu_pipeline_end(pipeline_builder, &resources.pipeline), NKGPU_OK,
                       "nkgpu_pipeline_end"))
        return 1;

    const uint32_t initial[] = {9, 4, 7, 1, 11, 13, 17, 19};
    nkgpu_buffer_desc buffer_desc{};
    buffer_desc.struct_size = sizeof(buffer_desc);
    buffer_desc.size = sizeof(initial);
    buffer_desc.usage = NKGPU_BUFFER_STORAGE;
    buffer_desc.data = reinterpret_cast<const uint8_t *>(initial);
    buffer_desc.data_size = sizeof(initial);
    if (!expect_result(nkgpu_buffer_create_desc(resources.renderer, &buffer_desc, &resources.buffer),
                       NKGPU_OK, "nkgpu_buffer_create_desc(storage)"))
        return 1;

    if (!expect_result(nkgpu_frame_begin(resources.renderer), NKGPU_OK,
                       "nkgpu_frame_begin(compute)") ||
        !expect_result(nkgpu_begin_compute_pass(resources.renderer), NKGPU_OK,
                        "nkgpu_begin_compute_pass") ||
        !expect_result(nkgpu_apply_pipeline(resources.renderer, resources.pipeline), NKGPU_OK,
                       "nkgpu_apply_pipeline(compute)") ||
        !expect_result(nkgpu_apply_storage_buffer(resources.renderer, 0, resources.buffer),
                       NKGPU_OK, "nkgpu_apply_storage_buffer") ||
        !expect_result(nkgpu_dispatch(resources.renderer, 2, 1, 1), NKGPU_OK,
                       "nkgpu_dispatch") ||
        !expect_result(nkgpu_end_pass(resources.renderer), NKGPU_OK,
                       "nkgpu_end_pass(compute)") ||
        !expect_result(nkgpu_begin_window_pass(resources.renderer, window_options.width,
                                               window_options.height, 0),
                       NKGPU_OK, "nkgpu_begin_window_pass") ||
        !expect_result(nkgpu_end_frame(resources.renderer), NKGPU_OK,
                        "nkgpu_end_frame(compute)"))
        return 1;

    const uint32_t expected[] = {initial[2] * 3 + 1, initial[3] * 3 + 1,
                                 initial[4] * 3 + 1, initial[5] * 3 + 1};
    if (!readback_range(resources, 2 * sizeof(uint32_t), sizeof(expected), expected,
                        sizeof(expected) / sizeof(expected[0])))
        return 1;

    nkgpu_buffer_readback_desc invalid{};
    invalid.struct_size = sizeof(invalid);
    invalid.buffer = resources.buffer;
    invalid.offset = sizeof(initial) - sizeof(uint32_t);
    invalid.size = sizeof(expected);
    nkgpu_readback invalid_readback{};
    if (!expect_result(nkgpu_readback_begin_buffer(resources.renderer, &invalid, &invalid_readback),
                       NKGPU_ERROR_INVALID_ARGUMENT, "nkgpu_readback_begin_buffer(invalid)"))
        return 1;

    return 0;
}
