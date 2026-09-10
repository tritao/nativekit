#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"

#include "compositor/compositor.h"
#include "prepare/nanovg_recorder.h"
#include "prepare/skribidi_adapter.h"
#include "render/frame_resources.h"
#include "render/render_plan_executor.h"
#include "render/sokol_backend.h"

#include <GL/gl.h>

#include <chrono>
#include <memory>
#include <thread>

using namespace nkui;

#ifndef NKUI_TEST_FONT_PATH
#error NKUI_TEST_FONT_PATH is required
#endif

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK)
        return 1;
    nk_window_options window_options{};
    window_options.struct_size = sizeof(window_options);
    window_options.width = 320;
    window_options.height = 240;
    window_options.title = "NativeKit UI backend smoke";
    nk_handle window = NK_INVALID_HANDLE;
    if (nk_window_create(&window_options, &window) != NK_OK)
        return 2;
    nk_surface_options surface_options{};
    surface_options.struct_size = sizeof(surface_options);
    surface_options.flags = NK_SURFACE_FORWARD_COMPATIBLE | NK_SURFACE_STENCIL;
    surface_options.api = NK_GRAPHICS_OPENGL;
    surface_options.major_version = 3;
    surface_options.minor_version = 3;
    surface_options.width = window_options.width;
    surface_options.height = window_options.height;
    nk_handle surface = NK_INVALID_HANDLE;
    if (nk_surface_create(window, &surface_options, &surface) != NK_OK)
        return 3;

    int result = 0;
    int width = 0;
    int height = 0;
    int frames = 0;
    bool ready = false;
    auto backend = std::make_unique<SokolBackend>();
    NanoVGRecorder recorder;
    SkribidiAdapter text_adapter;
    PreparedGlyphs title_glyphs;
    PreparedGlyphs layer_glyphs;
    if (!text_adapter.valid() || !text_adapter.add_font(NKUI_TEST_FONT_PATH) ||
        !text_adapter.layout_utf8("NativeKit direct text", 280.0f, 24.0f) ||
        !text_adapter.prepare_glyphs(20.0f, 35.0f, 1.0f, GlyphMode::Alpha, title_glyphs) ||
        !text_adapter.prepare_glyphs(58.0f, 105.0f, 1.0f, GlyphMode::Alpha, layer_glyphs))
        result = 9;
    const ResourceId main_target = make_resource_id(ResourceKind::RenderTarget, 1, 1);
    const ResourceId background = make_resource_id(ResourceKind::Path, 1, 1);
    const ResourceId layer_path = make_resource_id(ResourceKind::Path, 1, 2);
    const ResourceId foreground = make_resource_id(ResourceKind::Path, 1, 3);
    const ResourceId title = make_resource_id(ResourceKind::TextLayout, 1, 1);
    const ResourceId layer_text = make_resource_id(ResourceKind::TextLayout, 1, 2);
    DisplayList display_list;
    display_list.draw_path(background);
    display_list.draw_text_layout(title, 20.0f, 35.0f);
    display_list.begin_layer(0.6f);
    display_list.draw_path(layer_path);
    display_list.draw_text_layout(layer_text, 58.0f, 105.0f);
    display_list.end_layer();
    display_list.draw_path(foreground);
    RenderPlan plan;
    Compositor compositor;
    if (!compositor.compile(display_list, main_target, plan))
        result = 8;
    while (!result && frames < 30) {
        nk_event event{};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK) {
            result = 4;
        } else if (event.kind == NK_EVENT_SURFACE_READY && event.source == surface) {
            if (nk_surface_make_current(surface) != NK_OK ||
                nk_surface_get_framebuffer_size(surface, &width, &height) != NK_OK ||
                !backend->initialize())
                result = 5;
            else
                ready = true;
        } else if (event.kind == NK_EVENT_SURFACE_RESIZE && event.source == surface &&
                   event.data_size >= sizeof(nk_surface_resize_event)) {
            const auto *size = static_cast<const nk_surface_resize_event *>(event.data);
            width = size->framebuffer_width;
            height = size->framebuffer_height;
        }
        nk_event_release(&event);
        if (!ready || result) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        nk_surface_make_current(surface);
        recorder.reset();
        NVGcontext *vg = recorder.context();
        nvgBeginFrame(vg, static_cast<float>(width), static_cast<float>(height), 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        nvgFillColor(vg, nvgRGBA(8, 12, 25, 255));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, 40.0f, 40.0f);
        nvgLineTo(vg, 200.0f, 40.0f);
        nvgLineTo(vg, 120.0f, 85.0f);
        nvgLineTo(vg, 200.0f, 140.0f);
        nvgLineTo(vg, 40.0f, 140.0f);
        nvgClosePath(vg);
        nvgFillColor(vg, nvgRGBA(40, 120, 220, 220));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRect(vg, 35.0f, 35.0f, 170.0f, 110.0f);
        nvgStrokeWidth(vg, 3.0f);
        nvgStrokeColor(vg, nvgRGBA(245, 245, 255, 255));
        nvgStroke(vg);
        nvgEndFrame(vg);
        GLint framebuffer = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
        FrameResources resources;
        if (!resources.bind_path(background, recorder, 0) ||
            !resources.bind_path(layer_path, recorder, 1) ||
            !resources.bind_path(foreground, recorder, 2) ||
            !resources.bind_text(title, title_glyphs) ||
            !resources.bind_text(layer_text, layer_glyphs) ||
            !backend->upload_atlases(text_adapter) ||
            !execute_render_plan(*backend, plan, resources,
                                 {main_target, width, height, static_cast<uint32_t>(framebuffer)}))
            result = 6;
        if (!result && frames == 0) {
            unsigned char filled[4]{};
            unsigned char notch[4]{};
            glReadPixels(60, height - 85, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, filled);
            glReadPixels(170, height - 85, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, notch);
            if (filled[2] <= notch[2] + 30)
                result = 11;
        }
        if (!result && nk_surface_present(surface) != NK_OK)
            result = 6;
        ++frames;
    }
    if (!result && backend->stats().passes != 90)
        result = 7;
    if (!result && (backend->stats().draws != 210 || backend->stats().image_uploads == 0))
        result = 10;
    if (ready)
        nk_surface_make_current(surface);
    backend.reset();
    nk_surface_destroy(surface);
    nk_window_destroy(window);
    nk_shutdown();
    return result;
}
