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
        nkgpu_shader_language language;
    } cases[] = {
        {NKGPU_BACKEND_GLCORE, 0, NKGPU_SHADER_TARGET_GLSL410, NKGPU_SHADERLANGUAGE_GLSL},
        {NKGPU_BACKEND_GLES3, 0, NKGPU_SHADER_TARGET_GLSL300ES, NKGPU_SHADERLANGUAGE_GLSL},
        {NKGPU_BACKEND_D3D11, 0, NKGPU_SHADER_TARGET_HLSL5, NKGPU_SHADERLANGUAGE_HLSL5},
        {NKGPU_BACKEND_METAL, 0, NKGPU_SHADER_TARGET_METAL_MACOS, NKGPU_SHADERLANGUAGE_MSL},
        {NKGPU_BACKEND_METAL, 1, NKGPU_SHADER_TARGET_METAL_IOS, NKGPU_SHADERLANGUAGE_MSL},
        {0, 0, 0, 0}
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); ++index) {
        const shader_table_target selected =
            shader_table_target_for_backend(cases[index].backend, cases[index].metal_ios);
        if (selected.target != cases[index].target || selected.language != cases[index].language) {
            fprintf(stderr, "Unexpected shader target in case %zu\n", index);
            return 0;
        }
    }
    return 1;
}

static int verify_table(nkgpu_renderer renderer) {
    const int gles = nkgpu_query_backend(renderer) == NKGPU_BACKEND_GLES3;
    const nkgpu_shader_target live = gles ? NKGPU_SHADER_TARGET_GLSL300ES : NKGPU_SHADER_TARGET_GLSL410;
    nkgpu_shader_table_entry entries[] = {
        {.target = NKGPU_SHADER_TARGET_GLSL410},
        {.target = NKGPU_SHADER_TARGET_GLSL300ES},
        {.target = NKGPU_SHADER_TARGET_HLSL5},
        {.target = NKGPU_SHADER_TARGET_METAL_MACOS},
        {.target = NKGPU_SHADER_TARGET_METAL_IOS},
    };
    nkgpu_shader_table_entry *selected = &entries[live - 1];
    selected->vertex = gles ? "#version 300 es\nvoid main(){gl_Position=vec4(0.0);}"
                           : "#version 410\nvoid main(){gl_Position=vec4(0.0);}";
    selected->fragment = gles
        ? "#version 300 es\nprecision mediump float;out vec4 c;void main(){c=vec4(1.0);}"
        : "#version 410\nout vec4 c;void main(){c=vec4(1.0);}";
    nkgpu_shader shader = {0};
    if (nkgpu_shader_create_from_table(renderer, entries, 5, &shader) != NKGPU_OK ||
        nkgpu_shader_destroy(renderer, shader) != NKGPU_OK ||
        nkgpu_shader_create_from_table(renderer, entries + 2, 3, &shader) != NKGPU_ERROR_UNSUPPORTED)
        return 0;
    selected->attribute_count = 1;
    if (nkgpu_shader_create_from_table(renderer, entries, 5, &shader) != NKGPU_ERROR_INVALID_ARGUMENT)
        return 0;
    selected->attribute_count = 0;
    entries[4].target = live;
    if (nkgpu_shader_create_from_table(renderer, entries, 5, &shader) != NKGPU_ERROR_INVALID_ARGUMENT)
        return 0;
    entries[4].target = 0;
    return nkgpu_shader_create_from_table(renderer, entries, 5, &shader) == NKGPU_ERROR_INVALID_ARGUMENT;
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
        puts("PASS: five shader targets, live shader creation, and malformed tables");
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
