#ifndef NATIVEKIT_UI_GRAPHICS_DEVICE_H
#define NATIVEKIT_UI_GRAPHICS_DEVICE_H

#include "nativekit_sokol_backend_config.h"
#include "sokol_gfx.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nkui {

/** Generation-checked identity for a dynamic GPU buffer owned by a device. */
struct GpuBufferHandle {
    uint32_t value = 0;
    explicit operator bool() const { return value != 0; }
};

/** Generation-checked identity for a device-owned GPU image. */
struct GpuImageHandle {
    uint32_t value = 0;
    explicit operator bool() const { return value != 0; }
};

/** Generation-checked identity for a device-owned GPU view. */
struct GpuViewHandle {
    uint32_t value = 0;
    explicit operator bool() const { return value != 0; }
};

/** Generation-checked identity for a device-owned GPU sampler. */
struct GpuSamplerHandle {
    uint32_t value = 0;
    explicit operator bool() const { return value != 0; }
};

/** Device-owned registry for dynamic Sokol resources. */
class GpuResourceRegistry {
  public:
    GpuResourceRegistry() = default;
    ~GpuResourceRegistry();

    GpuResourceRegistry(const GpuResourceRegistry &) = delete;
    GpuResourceRegistry &operator=(const GpuResourceRegistry &) = delete;

    GpuBufferHandle create_buffer(const sg_buffer_desc &description);
    sg_buffer resolve(GpuBufferHandle handle) const;
    void destroy(GpuBufferHandle handle);
    GpuImageHandle create_image(const sg_image_desc &description);
    sg_image resolve(GpuImageHandle handle) const;
    void destroy(GpuImageHandle handle);
    GpuViewHandle create_view(const sg_view_desc &description);
    sg_view resolve(GpuViewHandle handle) const;
    void destroy(GpuViewHandle handle);
    GpuSamplerHandle create_sampler(const sg_sampler_desc &description);
    sg_sampler resolve(GpuSamplerHandle handle) const;
    void destroy(GpuSamplerHandle handle);
    void clear();

  private:
    template <class T> struct Slot {
        uint16_t generation = 1;
        bool active = false;
        T value{};
    };

    std::vector<Slot<sg_buffer>> buffers_;
    std::vector<Slot<sg_image>> images_;
    std::vector<Slot<sg_view>> views_;
    std::vector<Slot<sg_sampler>> samplers_;
};

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
    GpuResourceRegistry &gpu_resources() { return gpu_resources_; }
    const GpuResourceRegistry &gpu_resources() const { return gpu_resources_; }

  private:
    GraphicsDevice();

    GraphicsDeviceResources resources_{};
    GpuResourceRegistry gpu_resources_;
    std::string error_;
    bool runtime_acquired_ = false;
    bool valid_ = false;
};

} // namespace nkui

#endif
