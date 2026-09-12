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

class SkribidiAdapter;
class SkribidiFontCollection;

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
    // A shared source is owned by the layout engine and must outlive this
    // frame and any backend atlas uploads derived from it.
    SkribidiAdapter *text_adapter() { return text_source_ ? text_source_ : text_.get(); }
    const SkribidiAdapter *text_adapter() const {
        return text_source_ ? text_source_ : text_.get();
    }

  private:
    friend class LayoutRenderCompiler;

    void reset();

    RenderPlan plan_;
    FrameResources resources_;
    std::unique_ptr<SkribidiAdapter> text_;
    SkribidiAdapter *text_source_ = nullptr;
    std::vector<std::unique_ptr<PreparedPath>> paths_;
    std::vector<std::unique_ptr<PreparedGlyphs>> glyphs_;
};

/** Compiles NativeKit-owned layout output into the backend-neutral render plan. */
class LayoutRenderCompiler {
  public:
    LayoutRenderCompiler();
    void set_font_collection(std::shared_ptr<SkribidiFontCollection> fonts);
    bool add_font(const char *path, FontFamily family = FontFamily::Default);
    bool add_font_from_data(const char *name, const void *data, std::size_t bytes,
                            FontFamily family = FontFamily::Default);
    bool add_system_fallbacks();

    bool compile(const LayoutSnapshot &snapshot, ResourceId main_target, float pixel_scale,
                 LayoutRenderFrame &out, LayoutRenderCompileError *error = nullptr,
                 bool load_existing = false, SkribidiAdapter *text_source = nullptr) const;

  private:
    std::shared_ptr<SkribidiFontCollection> fonts_;
};

} // namespace nkui

#endif
