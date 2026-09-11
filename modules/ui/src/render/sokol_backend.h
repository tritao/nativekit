#ifndef NATIVEKIT_UI_SOKOL_BACKEND_H
#define NATIVEKIT_UI_SOKOL_BACKEND_H

#include "render_backend.h"
#include "nativekit_sokol_api.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace nkui {

struct SurfaceDescriptor;

struct SolidVertex {
    float x;
    float y;
};

struct SolidMesh {
    std::vector<SolidVertex> vertices;
    std::vector<uint32_t> indices;
};

bool triangulate_prepared_path(const PreparedPathData &path,
                               const PreparedPathOperation &operation, SolidMesh &mesh);

using SokolBackendStats = RenderBackendStats;

/** Creates the renderer implementation compatible with a surface API. */
std::unique_ptr<RenderBackend> create_render_backend(nk_graphics_api api);

class SokolBackend final : public RenderBackend {
  public:
    explicit SokolBackend(const nk_sokol_api *api);
    ~SokolBackend() override;
    SokolBackend(const SokolBackend &) = delete;
    SokolBackend &operator=(const SokolBackend &) = delete;

    bool initialize() override;
    bool valid() const override;
    bool begin_window_pass(int width, int height, const nk_surface_frame_target &target,
                           bool clear) override;
    bool begin_target_pass(ResourceId target, int width, int height,
                           bool load_existing) override;
    bool begin_surface_pass(ResourceId target, const SurfaceDescriptor &description,
                            bool load_existing) override;
    bool surface_has_content(ResourceId target) const override;
    bool surface_is_current(ResourceId target, uint32_t generation,
                            const SurfaceDescriptor &description) const override;
    void mark_surface_current(ResourceId target, uint32_t generation,
                              const SurfaceDescriptor &description) override;
    bool set_scissor(bool enabled, float x = 0.0f, float y = 0.0f, float width = 0.0f,
                     float height = 0.0f) override;
    bool draw_path(const PreparedPathData &path, uint32_t operation_index,
                   float opacity = 1.0f) override;
    bool draw_path_transformed(const PreparedPathData &path, uint32_t operation_index,
                               const float transform[6], float opacity = 1.0f) override;
    bool draw_paths(const PreparedPathData &path) override;
    bool draw_image(const PreparedTexture &image, float x, float y, float width, float height,
                    const float transform[6], float opacity = 1.0f) override;
    bool upload_atlases(SkribidiAdapter &adapter, bool include_clean = false) override;
    bool draw_glyphs(const PreparedGlyphs &glyphs, float opacity = 1.0f) override;
    bool draw_glyphs_transformed(const PreparedGlyphs &glyphs, const float transform[6],
                                 float origin_x, float origin_y,
                                 float opacity = 1.0f) override;
    bool draw_target(ResourceId target, float x, float y, float width, float height,
                     float opacity) override;
    bool end_pass() override;
    bool commit_frame() override;
    bool end_frame() override;
    RenderBackendStats stats() const override;
    const char *last_error() const override;

    struct State;

  private:
    State *state_ = nullptr;
};

} // namespace nkui

#endif
