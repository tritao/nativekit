#ifndef NATIVEKIT_UI_RENDERER_H
#define NATIVEKIT_UI_RENDERER_H

#include "nativekit_graphics.h"
#include "display_list/display_list.h"
#include "prepare/nanovg_path.h"
#include "prepare/skribidi_adapter.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>

namespace nkui {

struct SurfaceDescriptor;

struct UiRendererStats {
    uint32_t passes = 0;
    uint32_t draws = 0;
    uint32_t pipeline_changes = 0;
    uint32_t binding_changes = 0;
    uint32_t image_uploads = 0;
    uint64_t uploaded_bytes = 0;
    uint32_t atlas_full_uploads = 0;
    uint32_t atlas_subregion_uploads = 0;
    uint32_t atlas_full_upload_fallbacks = 0;
    uint32_t atlas_reallocations = 0;
    uint64_t atlas_dirty_bytes = 0;
    uint64_t atlas_dirty_capacity_bytes = 0;
    uint64_t atlas_subregion_bytes = 0;
    uint64_t atlas_uploaded_bytes = 0;
    uint64_t transient_bytes = 0;
    uint32_t gpu_resources = 0;
};

struct SurfaceMeshVertex {
    float x;
    float y;
    float z;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
};

struct SurfaceMeshView {
    std::span<const SurfaceMeshVertex> vertices;
    std::span<const uint32_t> indices;
    std::array<float, 16> model_view_projection{};
};

class UiRenderer {
  public:
    virtual ~UiRenderer() = default;

    virtual bool initialize() = 0;
    virtual bool valid() const = 0;
    virtual bool beginFrame() = 0;
    virtual bool beginWindowPass(int width, int height, bool clear) = 0;
    virtual bool beginTargetPass(ResourceId target, int width, int height,
                                 bool load_existing) = 0;
    virtual bool beginSurfacePass(ResourceId target, const SurfaceDescriptor &description,
                                  bool load_existing) = 0;
    virtual bool drawSurfaceMesh(const SurfaceMeshView &mesh) = 0;
    virtual bool surfaceHasContent(ResourceId target) const = 0;
    virtual bool surfaceIsCurrent(ResourceId target, uint32_t generation,
                                  const SurfaceDescriptor &description) const = 0;
    virtual void markSurfaceCurrent(ResourceId target, uint32_t generation,
                                    const SurfaceDescriptor &description) = 0;
    virtual bool setScissor(bool enabled, float x = 0.0f, float y = 0.0f,
                            float width = 0.0f, float height = 0.0f) = 0;
    virtual bool drawPath(const PreparedPathData &path, uint32_t operation_index,
                          float opacity = 1.0f) = 0;
    virtual bool drawPath(const PreparedPathData &path, uint32_t operation_index,
                          const float transform[6], float opacity = 1.0f) = 0;
    virtual bool drawImage(const PreparedTexture &image, float x, float y, float width,
                           float height, const float transform[6], float opacity = 1.0f) = 0;
    virtual bool uploadAtlases(SkribidiAdapter &adapter, bool include_clean = false) = 0;
    virtual bool drawGlyphs(const PreparedGlyphs &glyphs, float opacity = 1.0f) = 0;
    virtual bool drawGlyphs(const PreparedGlyphs &glyphs, const float transform[6],
                            float origin_x, float origin_y, float opacity = 1.0f) = 0;
    virtual bool compositeImage(ResourceId target, float x, float y, float width, float height,
                                const float transform[6], float opacity) = 0;
    virtual bool compositeImage(nk_graphics_image image, float x, float y, float width,
                                float height, const float transform[6], float opacity) = 0;
    virtual bool endPass() = 0;
    virtual bool endFrame() = 0;
    virtual UiRendererStats stats() const = 0;
    virtual const char *lastError() const = 0;
};

std::unique_ptr<UiRenderer> create_ui_renderer(nk_surface surface);

} // namespace nkui

#endif
