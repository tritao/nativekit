#ifndef NATIVEKIT_UI_GRAPHICS_DEVICE_H
#define NATIVEKIT_UI_GRAPHICS_DEVICE_H

#define SOKOL_GLCORE
#include "sokol_gfx.h"

#include <memory>
#include <string>

namespace nkui {

/** Immutable shader, pipeline, and sampler resources shared by UI executors. */
struct GraphicsDeviceResources {
    sg_shader solid_shader{};
    sg_pipeline solid_pipeline{};
    sg_pipeline fill_stencil_pipeline{};
    sg_pipeline fill_stencil_even_odd_pipeline{};
    sg_pipeline fill_cover_pipeline{};
    sg_shader paint_shader{};
    sg_pipeline paint_pipeline{};
    sg_pipeline paint_cover_pipeline{};
    sg_pipeline paint_fringe_pipeline{};
    sg_shader alpha_glyph_shader{};
    sg_pipeline alpha_glyph_pipeline{};
    sg_shader sdf_glyph_shader{};
    sg_pipeline sdf_glyph_pipeline{};
    sg_shader color_glyph_shader{};
    sg_pipeline color_glyph_pipeline{};
    sg_shader composite_shader{};
    sg_pipeline composite_pipeline{};
    sg_sampler sampler{};
    sg_sampler surface_sampler{};
    sg_image white_image{};
    sg_view white_view{};
    sg_sampler white_sampler{};
};

/**
 * Shared owner of the UI Sokol runtime.
 *
 * Sokol's graphics runtime is process-global for the active graphics context,
 * so renderer instances must retain the same device instead of independently
 * calling sg_setup() and sg_shutdown(). The device is acquired after the
 * caller has made the intended NativeKit surface current.
 */
class GraphicsDevice {
  public:
    static std::shared_ptr<GraphicsDevice> acquire(std::string *error = nullptr);

    ~GraphicsDevice();
    GraphicsDevice(const GraphicsDevice &) = delete;
    GraphicsDevice &operator=(const GraphicsDevice &) = delete;

    bool valid() const { return valid_; }
    const GraphicsDeviceResources &resources() const { return resources_; }

  private:
    GraphicsDevice();

    GraphicsDeviceResources resources_{};
    std::string error_;
    bool valid_ = false;
};

} // namespace nkui

#endif
