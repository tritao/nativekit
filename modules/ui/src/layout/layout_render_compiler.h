#ifndef NATIVEKIT_UI_LAYOUT_RENDER_COMPILER_H
#define NATIVEKIT_UI_LAYOUT_RENDER_COMPILER_H

#include "compositor/render_plan.h"
#include "layout/layout_types.h"
#include "render/frame_resources.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nkui {

struct LayoutRenderCompileError {
    std::size_t primitive_index = 0;
    const char *message = nullptr;
};

/**
 * Owns the prepared resources referenced by one layout render plan.
 *
 * The frame must remain alive until its plan has finished executing because
 * FrameResources deliberately stores non-owning references to prepared data.
 * The text adapter is retained across compilations so atlas state can be
 * reused by a UI session.
 */
class LayoutRenderFrame {
  public:
    LayoutRenderFrame() = default;
    ~LayoutRenderFrame() = default;
    LayoutRenderFrame(const LayoutRenderFrame &) = delete;
    LayoutRenderFrame &operator=(const LayoutRenderFrame &) = delete;

    const RenderPlan &plan() const { return plan_; }
    const FrameResources &resources() const { return resources_; }
    FrameResources &resources() { return resources_; }
    SkribidiAdapter *text_adapter() { return text_.get(); }
    const SkribidiAdapter *text_adapter() const { return text_.get(); }

  private:
    friend class LayoutRenderCompiler;

    void reset();

    RenderPlan plan_;
    FrameResources resources_;
    std::unique_ptr<SkribidiAdapter> text_;
    std::vector<std::unique_ptr<PreparedPath>> paths_;
    std::vector<std::unique_ptr<PreparedGlyphs>> glyphs_;
    std::size_t configured_font_count_ = 0;
    bool configured_system_fallbacks_ = false;
    TextLayoutId active_text_layout_id_ = 0;
};

/** Compiles NativeKit-owned layout output into the backend-neutral render plan. */
class LayoutRenderCompiler {
  public:
    bool add_font(const char *path, FontFamily family = FontFamily::Default);
    bool add_font_from_data(const char *name, const void *data, std::size_t bytes,
                            FontFamily family = FontFamily::Default);
    bool add_system_fallbacks();

    bool compile(const LayoutSnapshot &snapshot, ResourceId main_target, float pixel_scale,
                 LayoutRenderFrame &out, LayoutRenderCompileError *error = nullptr,
                 bool load_existing = false) const;

  private:
    struct FontEntry {
        std::string name;
        FontFamily family = FontFamily::Default;
        std::shared_ptr<std::vector<uint8_t>> data;
    };

    std::vector<FontEntry> fonts_;
    bool system_fallbacks_ = false;
};

} // namespace nkui

#endif
