#include "nativekit_time.h"
#include "nativekit_window.h"
#include "shader_table_target.h"

#include <stdio.h>

static int wait_for_surface(nk_surface surface) {
    for (int attempt = 0; attempt < 250; ++attempt) {
        nk_event event = {0};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK)
            return 0;
        const int ready = event.kind == NK_EVENT_SURFACE_READY && event.source == surface;
        nk_event_release(&event);
        if (ready)
            return 1;
        if (nk_wait_events_timeout(0.02) != NK_OK)
            return 0;
    }
    return 0;
}

static int verify_targets(void) {
    const struct {
        nkgpu_backend backend;
        int metal_ios;
        nkgpu_shader_target target;
        nkgpu_shader_target fallback;
        nkgpu_shader_language language;
    } cases[] = {
        {NKGPU_BACKEND_GLCORE, 0, NKGPU_SHADER_TARGET_GLSL430, NKGPU_SHADER_TARGET_GLSL410, NKGPU_SHADERLANGUAGE_GLSL},
        {NKGPU_BACKEND_GLES3, 0, NKGPU_SHADER_TARGET_GLSL310ES, NKGPU_SHADER_TARGET_GLSL300ES, NKGPU_SHADERLANGUAGE_GLSL},
        {NKGPU_BACKEND_D3D11, 0, NKGPU_SHADER_TARGET_HLSL5, NKGPU_SHADER_TARGET_HLSL5, NKGPU_SHADERLANGUAGE_HLSL5},
        {NKGPU_BACKEND_METAL, 0, NKGPU_SHADER_TARGET_METAL_MACOS, NKGPU_SHADER_TARGET_METAL_MACOS, NKGPU_SHADERLANGUAGE_MSL},
        {NKGPU_BACKEND_METAL, 1, NKGPU_SHADER_TARGET_METAL_IOS, NKGPU_SHADER_TARGET_METAL_IOS, NKGPU_SHADERLANGUAGE_MSL},
        {0, 0, 0, 0, 0}
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const shader_table_target selected =
            shader_table_target_for_backend(cases[index].backend, cases[index].metal_ios);
        if (selected.target != cases[index].target || selected.fallback != cases[index].fallback ||
            selected.language != cases[index].language) {
            fprintf(stderr, "Unexpected shader target in case %zu\n", index);
            return 0;
        }
    }
    return 1;
}

static int create(nkgpu_renderer renderer, const nkgpu_shader_table_entry *entries, uint32_t count) {
    nkgpu_shader shader = {0};
    const nkgpu_result result = nkgpu_shader_create_from_table(renderer, entries, count, &shader);
    if (result == NKGPU_OK && nkgpu_shader_destroy(renderer, shader) != NKGPU_OK)
        return -1;
    return result;
}

static int verify_table(nkgpu_renderer renderer) {
    const int gles = nkgpu_query_backend(renderer) == NKGPU_BACKEND_GLES3;
    const nkgpu_shader_target basic = gles ? NKGPU_SHADER_TARGET_GLSL300ES : NKGPU_SHADER_TARGET_GLSL410;
    const nkgpu_shader_target modern = gles ? NKGPU_SHADER_TARGET_GLSL310ES : NKGPU_SHADER_TARGET_GLSL430;
    const char *header = gles ? "#version 300 es\n" : "#version 410\n";
    const char *modern_header = gles ? "#version 310 es\n" : "#version 430\n";
    char vertex[128], fragment[160], modern_vertex[128], modern_fragment[160], broken[64];
    snprintf(vertex, sizeof(vertex), "%svoid main(){gl_Position=vec4(0.0);}", header);
    snprintf(fragment, sizeof(fragment), "%s%sout vec4 c;void main(){c=vec4(1.0);}", header,
             gles ? "precision mediump float;" : "");
    snprintf(modern_vertex, sizeof(modern_vertex), "%svoid main(){gl_Position=vec4(0.0);}", modern_header);
    snprintf(modern_fragment, sizeof(modern_fragment), "%s%sout vec4 c;void main(){c=vec4(1.0);}", modern_header,
             gles ? "precision mediump float;" : "");
    snprintf(broken, sizeof(broken), "%snot a shader", header);

    /* The GL backends fall back to the GLSL 4.1 or ES 3.0 entry. */
    nkgpu_shader_table_entry entries[] = {
        {.target = NKGPU_SHADER_TARGET_GLSL410},
        {.target = NKGPU_SHADER_TARGET_GLSL300ES},
        {.target = NKGPU_SHADER_TARGET_HLSL5},
        {.target = NKGPU_SHADER_TARGET_METAL_MACOS},
        {.target = NKGPU_SHADER_TARGET_METAL_IOS},
        {.target = NKGPU_SHADER_TARGET_GLSL430},
        {.target = NKGPU_SHADER_TARGET_GLSL310ES},
    };
    nkgpu_shader_table_entry *fallback = &entries[basic - 1], *preferred = &entries[modern - 1];
    fallback->vertex = vertex;
    fallback->fragment = fragment;
    if (create(renderer, entries, 5) != NKGPU_OK || create(renderer, entries + 2, 3) != NKGPU_ERROR_UNSUPPORTED)
        return 0;
    /* With both present, the GLSL 4.3 or ES 3.1 entry wins; the broken fallback is never compiled. */
    fallback->vertex = broken;
    preferred->vertex = modern_vertex;
    preferred->fragment = modern_fragment;
    if (create(renderer, entries, 7) != NKGPU_OK)
        return 0;
    fallback->vertex = vertex;
    /* An entry needs vertex and fragment sources or one compute source. */
    preferred->compute = modern_vertex;
    if (create(renderer, entries, 7) != NKGPU_ERROR_INVALID_ARGUMENT)
        return 0;
    preferred->compute = NULL;
    preferred->fragment = NULL;
    if (create(renderer, entries, 7) != NKGPU_ERROR_INVALID_ARGUMENT)
        return 0;
    preferred->fragment = modern_fragment;
    nkgpu_features features = {0};
    features.struct_size = sizeof(features);
    if (nkgpu_query_features(renderer, &features) != NKGPU_OK)
        return 0;
    if (features.compute && features.storage_buffer) {
        char compute[256];
        snprintf(compute, sizeof(compute),
                 "%slayout(local_size_x=64) in;layout(std430, binding=0) buffer data { uint values[]; };"
                 "void main(){values[gl_GlobalInvocationID.x]=1u;}", modern_header);
        const nkgpu_shader_table_storage_buffer storage = {0, NKGPU_SHADERSTAGE_COMPUTE, 0, "data"};
        nkgpu_shader_table_entry compute_entry = {.target = modern, .compute = compute,
            .threads_x = 64, .threads_y = 1, .threads_z = 1, .storage_buffers = &storage, .storage_buffer_count = 1};
        if (create(renderer, &compute_entry, 1) != NKGPU_OK)
            return 0;
        compute_entry.storage_buffer_count = 1;
        compute_entry.storage_buffers = NULL;
        if (create(renderer, &compute_entry, 1) != NKGPU_ERROR_INVALID_ARGUMENT)
            return 0;
    }
    fallback->attribute_count = 1;
    if (create(renderer, entries, 5) != NKGPU_ERROR_INVALID_ARGUMENT)
        return 0;
    fallback->attribute_count = 0;
    entries[4].target = basic;
    if (create(renderer, entries, 5) != NKGPU_ERROR_INVALID_ARGUMENT)
        return 0;
    entries[4].target = 0;
    return create(renderer, entries, 5) == NKGPU_ERROR_INVALID_ARGUMENT;
}

int main(void) {
    if (!verify_targets())
        return 1;
    nk_init_options init = {0};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK)
        return 1;

    nk_window_options window_options = {0};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 64;
    window_options.height = 64;
    window_options.title = "NativeKit shader table";
    nk_window window = NK_INVALID_HANDLE;
    nk_surface surface = NK_INVALID_HANDLE;
    nkgpu_renderer renderer = {0};
    const int passed = nk_window_create(&window_options, &window) == NK_OK &&
        nkgpu_surface_create(window, 64, 64, &surface) == NKGPU_OK &&
        wait_for_surface(surface) &&
        nkgpu_renderer_create(surface, &renderer) == NKGPU_OK &&
        verify_table(renderer);
    if (passed)
        puts("PASS: shader targets and GL fallbacks, live graphics and compute shaders, and malformed tables");
    else
        fprintf(stderr, "Shader table test failed: %s (%s)\n", nk_last_error(), nkgpu_last_error());
    if (renderer.id)
        nkgpu_renderer_destroy(renderer);
    if (surface != NK_INVALID_HANDLE)
        nk_surface_destroy(surface);
    if (window != NK_INVALID_HANDLE)
        nk_window_destroy(window);
    nk_shutdown();
    return passed ? 0 : 1;
}
