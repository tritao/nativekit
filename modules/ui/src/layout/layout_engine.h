#ifndef NATIVEKIT_UI_LAYOUT_ENGINE_H
#define NATIVEKIT_UI_LAYOUT_ENGINE_H

#include "layout/layout_types.h"

#include <cstddef>
#include <memory>

namespace nkui {

class TextEngine;
class FontCollection;

/**
 * Private NativeKit layout boundary. Clay and its types are deliberately
 * hidden in the implementation; callers submit NativeKit-owned nodes and
 * receive a NativeKit-owned layout snapshot.
 */
class LayoutEngine {
  public:
    explicit LayoutEngine(std::size_t initial_capacity = 512);
    explicit LayoutEngine(std::shared_ptr<FontCollection> fonts,
                          std::size_t initial_capacity = 512);
    ~LayoutEngine();
    LayoutEngine(const LayoutEngine &) = delete;
    LayoutEngine &operator=(const LayoutEngine &) = delete;

    bool valid() const;
    TextEngine *text_engine();
    const TextEngine *text_engine() const;
    bool add_font(const char *path, FontFamily family = FontFamily::Default);
    bool add_font_from_data(const char *name, const void *data, std::size_t bytes,
                            FontFamily family = FontFamily::Default);
    bool add_system_fallbacks();

    /** Installs a NativeKit-owned synchronous measurer for external content. */
    void set_measure_callback(LayoutMeasureCallback callback);

    /** Returns cumulative intrinsic-measure activity and current cache occupancy. */
    LayoutMeasureStats measure_stats() const;

    bool layout(const std::vector<LayoutNode> &nodes, float width, float height,
                float delta_seconds, LayoutSnapshot &out, LayoutError *error = nullptr);

    /** Updates transforms and dependent world geometry without rerunning Clay layout. */
    bool update_transforms(const std::vector<LayoutNode> &nodes, LayoutSnapshot &snapshot,
                           LayoutError *error = nullptr);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nkui

#endif
