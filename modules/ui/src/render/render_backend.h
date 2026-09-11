#ifndef NATIVEKIT_UI_RENDER_BACKEND_H
#define NATIVEKIT_UI_RENDER_BACKEND_H

#include "nativekit_graphics.h"
#include "display_list/display_list.h"
#include "prepare/nanovg_path.h"
#include "prepare/skribidi_adapter.h"

#include <cstdint>

namespace nkui {

struct SurfaceDescriptor;

struct RenderBackendStats {
    uint32_t passes = 0;
    uint32_t draws = 0;
    uint32_t pipeline_changes = 0;
    uint32_t binding_changes = 0;
    uint32_t image_uploads = 0;
    uint64_t uploaded_bytes = 0;
    uint32_t atlas_full_uploads = 0;
    uint32_t atlas_subregion_uploads = 0;
    uint32_t atlas_reallocations = 0;
    uint64_t atlas_dirty_bytes = 0;
    uint64_t atlas_dirty_capacity_bytes = 0;
    uint64_t atlas_uploaded_bytes = 0;
    uint64_t transient_bytes = 0;
    uint32_t gpu_resources = 0;
};

/** Backend-neutral rendering contract used by the compositor executor. */
class RenderBackend {
  public:
    virtual ~RenderBackend() = default;

    virtual bool initialize() = 0;
    virtual bool valid() const = 0;
    virtual bool begin_window_pass(int width, int height,
                                   const nk_surface_frame_target &target, bool clear) = 0;
    virtual bool begin_target_pass(ResourceId target, int width, int height,
                                   bool load_existing) = 0;
    virtual bool begin_surface_pass(ResourceId target, const SurfaceDescriptor &description,
                                    bool load_existing) = 0;
    virtual bool surface_has_content(ResourceId target) const = 0;
    virtual bool surface_is_current(ResourceId target, uint32_t generation,
                                    const SurfaceDescriptor &description) const = 0;
    virtual void mark_surface_current(ResourceId target, uint32_t generation,
                                      const SurfaceDescriptor &description) = 0;
    virtual bool set_scissor(bool enabled, float x = 0.0f, float y = 0.0f,
                             float width = 0.0f, float height = 0.0f) = 0;
    virtual bool draw_path(const PreparedPathData &path, uint32_t operation_index,
                           float opacity = 1.0f) = 0;
    virtual bool draw_path_transformed(const PreparedPathData &path, uint32_t operation_index,
                                       const float transform[6], float opacity = 1.0f) = 0;
    virtual bool draw_paths(const PreparedPathData &path) = 0;
    virtual bool draw_image(const PreparedTexture &image, float x, float y, float width,
                            float height, const float transform[6], float opacity = 1.0f) = 0;
    virtual bool upload_atlases(SkribidiAdapter &adapter, bool include_clean = false) = 0;
    virtual bool draw_glyphs(const PreparedGlyphs &glyphs, float opacity = 1.0f) = 0;
    virtual bool draw_glyphs_transformed(const PreparedGlyphs &glyphs, const float transform[6],
                                         float origin_x, float origin_y,
                                         float opacity = 1.0f) = 0;
    virtual bool draw_target(ResourceId target, float x, float y, float width, float height,
                             float opacity) = 0;
    virtual bool end_pass() = 0;
    virtual bool commit_frame() = 0;
    virtual bool end_frame() = 0;
    virtual RenderBackendStats stats() const = 0;
    virtual const char *last_error() const = 0;
};

} // namespace nkui

#endif
