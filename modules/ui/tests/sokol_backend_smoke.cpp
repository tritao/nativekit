#include "nativekit.h"
#include "nativekit_graphics.h"
#include "nativekit_window.h"
#ifdef NKUI_TEST_PUBLIC_SOKOL_RUNTIME
#include "nativekit_sokol.h"
#endif

#include "compositor/compositor.h"
#include "prepare/nanovg_recorder.h"
#include "prepare/skribidi_adapter.h"
#include "render/cube_surface_producer.h"
#include "render/frame_resources.h"
#include "render/render_plan_executor.h"
#include "render/sokol_backend.h"

#include "nanovg.h"

#if defined(NK_SOKOL_BACKEND_GLES3)
#include <GLES3/gl3.h>
#define NKUI_TEST_SURFACE_API NK_GRAPHICS_OPENGL_ES
#else
#include <GL/gl.h>
#define NKUI_TEST_SURFACE_API NK_GRAPHICS_OPENGL
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <thread>

using namespace nkui;

#ifndef NKUI_TEST_FONT_PATH
#error NKUI_TEST_FONT_PATH is required
#endif

class TestCubeSurfaceProducer final : public SurfaceProducer {
  public:
    TestCubeSurfaceProducer(uint32_t &generation, bool &unavailable, bool &failed)
        : generation_(generation), unavailable_(unavailable),
          failed_(failed) {}
    bool ready() const override { return true; }
    bool describe(int requested_width, int requested_height,
                  SurfaceDescriptor &description) const override {
        described_width_ = requested_width;
        described_height_ = requested_height;
        return cube_.describe(std::max(1, requested_width / 2),
                              std::max(1, requested_height / 2), description);
    }
    uint32_t generation() const override { return generation_; }
    SurfaceRenderResult render(RenderBackend &backend, ResourceId target,
                               const SurfaceDescriptor &description) override {
        if (failed_)
            return SurfaceRenderResult::Failed;
        if (unavailable_)
            return SurfaceRenderResult::Unavailable;
        return cube_.render(backend, target, description);
    }
    void set_rotation(float radians) { cube_.set_rotation(radians); }
    int described_width() const { return described_width_; }
    int described_height() const { return described_height_; }

  private:
    CubeSurfaceProducer cube_;
    uint32_t &generation_;
    bool &unavailable_;
    bool &failed_;
    mutable int described_width_ = 0;
    mutable int described_height_ = 0;
};
#ifndef NKUI_TEST_COLOR_FONT_PATH
#error NKUI_TEST_COLOR_FONT_PATH is required
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
    surface_options.api = NKUI_TEST_SURFACE_API;
    surface_options.major_version = 3;
#if defined(NK_SOKOL_BACKEND_GLES3)
    surface_options.minor_version = 0;
#else
    surface_options.minor_version = 3;
#endif
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
    uint32_t producer_generation = 1;
    bool producer_unavailable = false;
    bool producer_failed = false;
    TestCubeSurfaceProducer producer(producer_generation, producer_unavailable, producer_failed);
    std::array<uint8_t, 64 * 64 * 4> first_cube_frame{};
    bool captured_cube_frame = false;
    bool prepared_text_update = false;
#ifdef NKUI_TEST_PUBLIC_SOKOL_RUNTIME
    nks_renderer public_renderer{};
#endif
    auto backend = std::make_unique<SokolBackend>(nk_sokol_get_api());
    std::unique_ptr<SokolBackend> shared_backend;
    NanoVGRecorder recorder;
    const unsigned char image_pixel[4] = {40, 120, 220, 220};
    const int paint_image =
        nvgCreateImageRGBA(recorder.context(), 1, 1, NVG_IMAGE_NEAREST, image_pixel);
    if (!paint_image)
        result = 12;
    SkribidiAdapter text_adapter;
    PreparedGlyphs title_glyphs;
    PreparedGlyphs layer_glyphs;
    PreparedGlyphs color_glyphs;
    if (!text_adapter.valid() || !text_adapter.add_font(NKUI_TEST_FONT_PATH) ||
        !text_adapter.add_font(NKUI_TEST_COLOR_FONT_PATH, FontFamily::Emoji) ||
        !text_adapter.layout_utf8("NativeKit direct text", 280.0f, 24.0f) ||
        !text_adapter.prepare_glyphs(0.0f, 0.0f, 1.0f, GlyphMode::Alpha, title_glyphs) ||
        !text_adapter.prepare_glyphs(0.0f, 0.0f, 1.0f, GlyphMode::Sdf, layer_glyphs) ||
        !text_adapter.layout_utf8("😀", 80.0f, 32.0f) ||
        !text_adapter.prepare_glyphs(0.0f, 0.0f, 1.0f, GlyphMode::Color, color_glyphs))
        result = 9;
    const ResourceId main_target = make_resource_id(ResourceKind::RenderTarget, 1, 1);
    const ResourceId external_target = make_resource_id(ResourceKind::RenderTarget, 1, 2);
    const ResourceId background = make_resource_id(ResourceKind::Path, 1, 1);
    const ResourceId layer_path = make_resource_id(ResourceKind::Path, 1, 2);
    const ResourceId foreground = make_resource_id(ResourceKind::Path, 1, 3);
    const ResourceId title = make_resource_id(ResourceKind::TextLayout, 1, 1);
    const ResourceId layer_text = make_resource_id(ResourceKind::TextLayout, 1, 2);
    const ResourceId color_text = make_resource_id(ResourceKind::TextLayout, 1, 3);
    DisplayList display_list;
    display_list.draw_path(background);
    display_list.draw_text_layout(title, 20.0f, 35.0f);
    display_list.draw_text_layout(color_text, 250.0f, 55.0f);
    const float surface_transform[6] = {1.05f, 0.0f, 0.0f, 1.05f, 2.0f, 1.0f};
    const float identity_transform[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    display_list.set_transform(surface_transform);
    display_list.draw_render_target(external_target, 205.0f, 145.0f, 90.0f, 70.0f);
    display_list.set_transform(identity_transform);
    display_list.begin_layer(0.6f);
    display_list.draw_path(layer_path);
    display_list.draw_text_layout(layer_text, 58.0f, 105.0f);
    display_list.end_layer();
    display_list.draw_path(foreground);
    RenderPlan plan;
    Compositor compositor;
    if (!compositor.compile(display_list, main_target, plan))
        result = 8;
    nk_surface_frame_target last_frame_target{};
    while (!result && frames < 30) {
        nk_event event{};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK) {
            result = 4;
        } else if (event.kind == NK_EVENT_SURFACE_READY && event.source == surface) {
            if (nk_surface_make_current(surface) != NK_OK ||
                nk_surface_get_framebuffer_size(surface, &width, &height) != NK_OK)
                result = 5;
            else {
#ifdef NKUI_TEST_PUBLIC_SOKOL_RUNTIME
                // Exercise the reverse ownership order: the public Sokol
                // adapter acquires the NativeKit-wide runtime first, then
                // UI backends retain the same compatible lease.
                if (nks_renderer_create(surface, &public_renderer) != NKS_OK)
                    result = 19;
#endif
                if (!result && !backend->initialize())
                    result = 5;
                if (!result) {
                    // Multiple UI renderers retain one process-local Sokol
                    // device. This exercises shared setup and release without
                    // changing the single-renderer draw sequence below.
                    shared_backend = std::make_unique<SokolBackend>(nk_sokol_get_api());
                    if (!shared_backend->initialize())
                        result = 17;
                    else
                        ready = true;
                }
            }
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
        nk_surface_frame_target frame_target{};
        frame_target.struct_size = sizeof(frame_target);
        if (nk_surface_get_frame_target(surface, &frame_target) != NK_OK ||
            frame_target.api != NKUI_TEST_SURFACE_API || frame_target.width <= 0 ||
            frame_target.height <= 0)
            result = 16;
        else
            last_frame_target = frame_target;
        recorder.reset();
        NVGcontext *vg = recorder.context();
        nvgBeginFrame(vg, static_cast<float>(width), static_cast<float>(height), 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height));
        nvgFillPaint(vg, nvgLinearGradient(vg, 0.0f, 0.0f, static_cast<float>(width), 0.0f,
                                           nvgRGBA(45, 12, 25, 255), nvgRGBA(8, 55, 25, 255)));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgMoveTo(vg, 40.0f, 40.0f);
        nvgLineTo(vg, 200.0f, 40.0f);
        nvgLineTo(vg, 120.0f, 85.0f);
        nvgLineTo(vg, 200.0f, 140.0f);
        nvgLineTo(vg, 40.0f, 140.0f);
        nvgClosePath(vg);
        nvgFillPaint(vg, nvgImagePattern(vg, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, paint_image, 1.0f));
        nvgFill(vg);
        nvgBeginPath(vg);
        nvgRect(vg, 35.0f, 35.0f, 170.0f, 110.0f);
        nvgStrokeWidth(vg, 3.0f);
        nvgStrokeColor(vg, nvgRGBA(245, 245, 255, 255));
        nvgStroke(vg);
        nvgEndFrame(vg);
        FrameResources resources;
        if (!producer_unavailable) {
            ++producer_generation;
            producer.set_rotation(0.65f + static_cast<float>(frames) * 0.18f);
        }
        if (!prepared_text_update && frames == 1) {
            PreparedGlyphs updated_title;
            if (!text_adapter.layout_utf8("NativeKit direct text: retained atlas update 123", 280.0f,
                                          24.0f) ||
                !text_adapter.prepare_glyphs(0.0f, 0.0f, 1.0f, GlyphMode::Alpha,
                                             updated_title))
                result = 15;
            else {
                title_glyphs = std::move(updated_title);
                prepared_text_update = true;
            }
        }
        if (!resources.bind_path(background, recorder.data(), 0) ||
            !resources.bind_path(layer_path, recorder.data(), 1) ||
            !resources.bind_path(foreground, recorder.data(), 2) ||
            !resources.bind_text(title, title_glyphs) ||
            !resources.bind_text(layer_text, layer_glyphs) ||
            !resources.bind_text(color_text, color_glyphs) ||
            !resources.bind_surface(external_target, producer) ||
            !backend->upload_atlases(text_adapter) ||
            !execute_render_plan(*backend, plan, resources,
                                 {main_target, frame_target}))
            result = 6;
        if (!result && frames == 0 &&
            (producer.described_width() != 95 || producer.described_height() != 74))
            result = 23;
        if (!result && frames == 0) {
            unsigned char filled[4]{};
            unsigned char notch[4]{};
            unsigned char gradient_left[4]{};
            unsigned char gradient_right[4]{};
            unsigned char cube_pixel[4]{};
            glReadPixels(60, height - 85, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, filled);
            glReadPixels(170, height - 85, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, notch);
            glReadPixels(10, height - 200, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, gradient_left);
            glReadPixels(width - 10, height - 200, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, gradient_right);
            glReadPixels(264, height - 190, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, cube_pixel);
            glReadPixels(217, 14, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE,
                         first_cube_frame.data());
            captured_cube_frame = true;
            bool cyan_face = false;
            bool orange_face = false;
            bool purple_face = false;
            for (size_t index = 0; index < first_cube_frame.size(); index += 4) {
                const int red = first_cube_frame[index];
                const int green = first_cube_frame[index + 1];
                const int blue = first_cube_frame[index + 2];
                cyan_face |= blue > red + 80 && green > red + 80;
                orange_face |= red > green + 60 && green > blue + 30;
                purple_face |= blue > red + 60 && red > green + 20;
            }
            if (filled[2] <= notch[2] + 30)
                result = 11;
            if (!result && (gradient_left[0] <= gradient_right[0] + 15 ||
                            gradient_right[1] <= gradient_left[1] + 15))
                result = 13;
            if (!result && std::max({cube_pixel[0], cube_pixel[1], cube_pixel[2]}) < 140)
                result = 21;
            if (!result && !(cyan_face && orange_face && purple_face))
                result = 24;
        }
        if (!result && frames == 3) {
            std::array<uint8_t, 64 * 64 * 4> cube_frame{};
            glReadPixels(217, 14, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, cube_frame.data());
            bool changed = false;
            for (size_t index = 0; index < cube_frame.size(); ++index) {
                const int difference = static_cast<int>(cube_frame[index]) -
                                       static_cast<int>(first_cube_frame[index]);
                if (difference > 20 || difference < -20) {
                    changed = true;
                    break;
                }
            }
            if (!captured_cube_frame || !changed)
                result = 22;
        }
        if (!result && nk_surface_present(surface) != NK_OK)
            result = 6;
        ++frames;
        if (frames == 1) {
            ++producer_generation;
            producer_unavailable = true;
        } else if (frames == 3) {
            producer_unavailable = false;
        }
    }
    if (!result) {
        ++producer_generation;
        producer_failed = true;
        TestCubeSurfaceProducer failed_producer(producer_generation, producer_unavailable,
                                                producer_failed);
        FrameResources failure_resources;
        RenderExecutionError failure_error{};
        if (!failure_resources.bind_surface(external_target, failed_producer) ||
            execute_render_plan(*backend, plan, failure_resources,
                                {main_target, last_frame_target}, &failure_error))
            result = 14;
    }
    if (!result && backend->stats().passes != 118)
        result = 7;
    if (!result && (backend->stats().draws != 328 || backend->stats().image_uploads < 2))
        result = 10;
    if (!result) {
        const auto atlas_stats = backend->stats();
        if (!atlas_stats.atlas_full_uploads || !atlas_stats.atlas_dirty_bytes ||
            atlas_stats.atlas_subregion_uploads || atlas_stats.atlas_subregion_bytes ||
            atlas_stats.atlas_full_upload_fallbacks || !atlas_stats.atlas_uploaded_bytes ||
            atlas_stats.atlas_uploaded_bytes < atlas_stats.atlas_dirty_capacity_bytes) {
            result = 16;
        }
    }
    if (ready)
        nk_surface_make_current(surface);
    backend.reset();
    if (shared_backend && !shared_backend->valid())
        result = 18;
    shared_backend.reset();
#ifdef NKUI_TEST_PUBLIC_SOKOL_RUNTIME
    if (public_renderer.id && nks_renderer_destroy(public_renderer) != NKS_OK)
        result = 20;
#endif
    nk_surface_destroy(surface);
    nk_window_destroy(window);
    nk_shutdown();
    return result;
}
