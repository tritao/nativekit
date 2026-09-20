#ifndef NATIVEKIT_UI_LAYOUT_RENDER_COMPILER_H
#define NATIVEKIT_UI_LAYOUT_RENDER_COMPILER_H

#include "compositor/render_plan.h"
#include "layout/layout_types.h"
#include "render/frame_resources.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nkui {

class TextEngine;
class FontCollection;

struct LayoutRenderCompileError {
    std::size_t primitive_index = 0;
    const char *message = nullptr;
};

/**
 * Owns the prepared resources referenced by one layout render plan.
 *
 * The frame must remain alive until its plan has finished executing because
 * FrameResources deliberately stores non-owning references to prepared data.
 * The text engine is retained across compilations so atlas state can be
 * reused by a UI session.
 */
class LayoutRenderFrame {
  public:
    LayoutRenderFrame() = default;
    ~LayoutRenderFrame() = default;
    LayoutRenderFrame(const LayoutRenderFrame &) = delete;
    LayoutRenderFrame &operator=(const LayoutRenderFrame &) = delete;

    const RenderPlan &plan() const { return plan_; }
    RenderPlan &plan() { return plan_; }
    const FrameResources &resources() const { return resources_; }
    FrameResources &resources() { return resources_; }
    /**
     * Immutable bindings of the same resources, filled alongside the borrowed
     * set so the frame can be sealed. Stay empty when sealable() is false.
     */
    const OwnedFrameResources &owned_resources() const { return owned_resources_; }
    OwnedFrameResources &owned_resources() { return owned_resources_; }
    /** False when the frame references something that cannot be sealed. */
    bool sealable() const { return sealable_; }
    void set_sealable(bool value) { sealable_ = value; }
    // A shared source is owned by the layout engine and must outlive this
    // frame and any backend atlas uploads derived from it.
    TextEngine *text_engine() {
        return text_engine_source_ ? text_engine_source_ : text_engine_.get();
    }
    const TextEngine *text_engine() const {
        return text_engine_source_ ? text_engine_source_ : text_engine_.get();
    }

  private:
    friend class LayoutRenderCompiler;

    void reset();

    RenderPlan plan_;
    FrameResources resources_;
    OwnedFrameResources owned_resources_;
    bool sealable_ = true;
    std::unique_ptr<TextEngine> text_engine_;
    TextEngine *text_engine_source_ = nullptr;
    std::vector<std::shared_ptr<PreparedPath>> paths_;
    std::vector<std::unique_ptr<PreparedGlyphs>> glyphs_;
};

/** Compiles NativeKit-owned layout output into the backend-neutral render plan. */
class LayoutRenderCompiler {
  public:
    using CustomPaintPlans = std::unordered_map<uint32_t, const RenderPlan *>;
    using CustomPaintComposites = std::unordered_map<uint32_t, const DisplayList *>;
    using RasterPaintNodes = std::unordered_set<uint32_t>;

    LayoutRenderCompiler();
    void set_font_collection(std::shared_ptr<FontCollection> fonts);
    bool add_font(const char *path, FontFamily family = FontFamily::Default);
    bool add_font_from_data(const char *name, const void *data, std::size_t bytes,
                            FontFamily family = FontFamily::Default);
    bool add_system_fallbacks();

    bool compile(const LayoutSnapshot &snapshot, ResourceId main_target, float pixel_scale,
                 LayoutRenderFrame &out, LayoutRenderCompileError *error = nullptr,
                 bool load_existing = false, TextEngine *text_engine_source = nullptr,
                 const CustomPaintPlans *custom_paints = nullptr,
                 const RasterPaintNodes *raster_paint_nodes = nullptr,
                 const CustomPaintComposites *custom_composites = nullptr) const;

  private:
    std::shared_ptr<FontCollection> fonts_;
};

} // namespace nkui

#endif
