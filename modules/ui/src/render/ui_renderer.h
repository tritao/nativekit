#ifndef NATIVEKIT_UI_RENDERER_H
#define NATIVEKIT_UI_RENDERER_H

#include "nativekit_graphics.h"
#include "display_list/display_list.h"
#include "prepare/nanovg_path.h"
#include "prepare/text_engine.h"

#include <array>
#include <cstdint>
#include <memory>

namespace nkui {

/** Native-owned shader sources and execution contract for one custom effect. */
struct CustomEffectRegistration {
    uint32_t registration_id = 0;
    const char *name = nullptr;
    const char *glsl410_fragment = nullptr;
    const char *glsl300es_fragment = nullptr;
    const char *hlsl5_fragment = nullptr;
    const char *metal_macos_fragment = nullptr;
    uint32_t parameter_components = 0;
    uint32_t pass_count = 1;
    uint32_t sampling_inputs = 1;
    std::array<float, 4> ink_overflow{};
};

struct UiGpuStats {
    uint64_t frames = 0;
    uint64_t passes = 0;
    uint64_t draw_calls = 0;
    uint64_t buffers_live = 0;
    uint64_t images_live = 0;
    uint64_t samplers_live = 0;
    uint64_t shaders_live = 0;
    uint64_t pipelines_live = 0;
    uint64_t render_targets_live = 0;
    uint64_t buffer_bytes = 0;
    uint64_t image_bytes = 0;
    uint64_t render_target_bytes = 0;
    uint64_t upload_bytes = 0;
    uint64_t resource_creations = 0;
    uint64_t resource_destructions = 0;
    uint64_t surface_recreations = 0;
    uint64_t device_losses = 0;
    uint64_t failed_allocations = 0;
};

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
    uint64_t render_plan_commands = 0;
    uint64_t display_list_count = 0;
    uint64_t display_list_bytes = 0;
    /** Frames recorded into a sealed submission batch instead of drawn inline. */
    uint64_t recorded_frames = 0;
    uint64_t atlas_pages = 0;
    uint64_t atlas_bytes = 0;
    uint64_t glyph_uploads = 0;
    uint64_t glyphs_rasterized = 0;
    uint64_t atlas_rebuilds = 0;
    uint64_t atlas_partial_updates = 0;
    uint64_t atlas_dirty_upload_bytes = 0;
    uint64_t atlas_scale_generation = 0;
    uint64_t text_layout_cache_hits = 0;
    uint64_t text_layout_cache_misses = 0;
    uint64_t custom_paint_nodes = 0;
    uint64_t custom_paint_bytes = 0;
    uint64_t transient_target_pool_hits = 0;
    uint64_t transient_target_pool_misses = 0;
    uint64_t transient_target_pool_count = 0;
    uint64_t transient_target_pool_bytes = 0;
    uint64_t effect_cache_hits = 0;
    uint64_t effect_cache_misses = 0;
    uint64_t effect_cache_entries = 0;
    uint64_t effect_cache_bytes = 0;
    uint64_t raster_cache_hits = 0;
    uint64_t raster_cache_misses = 0;
    uint64_t raster_cache_entries = 0;
    uint64_t raster_cache_bytes = 0;
    UiGpuStats gpu{};
};

class UiRenderer {
  public:
    virtual ~UiRenderer() = default;

    virtual bool initialize() = 0;
    virtual bool valid() const = 0;
    virtual bool lost() const = 0;
    /**
     * Starts a frame. Frames are recorded into a sealed submission batch and
     * replayed by endFrame(). External surfaces are retained graphics images.
     */
    virtual bool beginFrame(bool record, const nk_surface_frame_target *frame_target = nullptr) = 0;
    virtual bool beginWindowPass(int width, int height, bool clear) = 0;
    virtual bool beginTargetPass(ResourceId target, int width, int height, bool load_existing) = 0;
    /** Begin an effect output pass, reusing a persistent cached result when its key matches. */
    virtual bool beginEffectPass(ResourceId target, uint64_t cache_key, int width, int height,
                                 bool &cache_hit) = 0;
    /** Begin a cached raster pass containing ordinary draw commands. */
    virtual bool beginRasterPass(ResourceId target, uint64_t cache_key, int width, int height,
                                 bool &cache_hit) = 0;
    virtual bool setScissor(bool enabled, float x = 0.0f, float y = 0.0f, float width = 0.0f,
                            float height = 0.0f) = 0;
    virtual bool drawPath(const PreparedPathData &path, uint32_t operation_index,
                          float opacity = 1.0f) = 0;
    virtual bool drawPath(const PreparedPathData &path, uint32_t operation_index,
                          const float transform[6], float opacity = 1.0f) = 0;
    virtual bool drawImage(const PreparedTexture &image, float x, float y, float width,
                           float height, const float transform[6], float opacity = 1.0f) = 0;
    /** Paints a geometry-aware rounded-rectangle shadow without filtering a subtree. */
    virtual bool drawBoxShadow(float x, float y, float width, float height,
                               const float transform[6], float opacity,
                               const BoxShadowDescriptor &shadow) = 0;
    virtual bool uploadAtlases(TextEngine &engine, bool include_clean = false) = 0;
    virtual bool drawGlyphs(const PreparedGlyphs &glyphs, float opacity = 1.0f) = 0;
    virtual bool drawGlyphs(const PreparedGlyphs &glyphs, const float transform[6], float origin_x,
                            float origin_y, float opacity = 1.0f) = 0;
    virtual bool compositeImage(ResourceId target, float x, float y, float width, float height,
                                const float transform[6], float opacity) = 0;
    virtual bool compositeImage(nk_graphics_image image, float x, float y, float width,
                                float height, const float transform[6], float opacity) = 0;
    /** Applies one backend-neutral effect to the currently active target. */
    virtual bool applyEffect(ResourceId source, const EffectDescriptor &effect) = 0;
    /** Applies an effect while sampling a bounded source rectangle, used by backdrop capture. */
    virtual bool applyEffectRegion(ResourceId source, const EffectDescriptor &effect, float x,
                                   float y, float width, float height) {
        (void)x;
        (void)y;
        (void)width;
        (void)height;
        return applyEffect(source, effect);
    }
    /** Applies a registered custom effect; shader and pipeline ownership stays native. */
    virtual bool applyCustomEffect(ResourceId source, const CustomEffectDescriptor &effect) {
        (void)source;
        (void)effect;
        return false;
    }
    /** Region variant used by bounded capture passes. */
    virtual bool applyCustomEffectRegion(ResourceId source, const CustomEffectDescriptor &effect,
                                         float x, float y, float width, float height) {
        (void)x;
        (void)y;
        (void)width;
        (void)height;
        return applyCustomEffect(source, effect);
    }
    /** Registers a backend-specific shader implementation for a custom effect. */
    virtual bool registerCustomEffect(const CustomEffectRegistration &registration) {
        (void)registration;
        return false;
    }
    /** Applies a separate source-alpha mask to the currently active target. */
    virtual bool applyMask(ResourceId source, const MaskDescriptor &mask,
                           const PreparedTexture *image) = 0;
    virtual bool endPass() = 0;
    virtual bool endFrame() = 0;
    virtual UiRendererStats stats() const = 0;
    virtual const char *lastError() const = 0;
};

std::unique_ptr<UiRenderer> create_ui_renderer(nk_surface surface);
std::unique_ptr<UiRenderer> create_ui_renderer(nk_surface surface,
                                               const nk_surface_frame_target *frame_target);

} // namespace nkui

#endif
