#ifndef NATIVEKIT_UI_LAYOUT_ENGINE_H
#define NATIVEKIT_UI_LAYOUT_ENGINE_H

#include "layout/layout_types.h"

#include <cstddef>
#include <memory>

namespace nkui {

class SkribidiAdapter;
class SkribidiFontCollection;

/**
 * Private NativeKit layout boundary. Clay and its types are deliberately
 * hidden in the implementation; callers submit NativeKit-owned nodes and
 * receive a NativeKit-owned layout snapshot.
 */
class LayoutEngine {
  public:
    explicit LayoutEngine(std::size_t max_nodes = 512);
    explicit LayoutEngine(std::shared_ptr<SkribidiFontCollection> fonts,
                          std::size_t max_nodes = 512);
    ~LayoutEngine();
    LayoutEngine(const LayoutEngine &) = delete;
    LayoutEngine &operator=(const LayoutEngine &) = delete;

    bool valid() const;
    SkribidiAdapter *text_adapter();
    const SkribidiAdapter *text_adapter() const;
    bool add_font(const char *path, FontFamily family = FontFamily::Default);
    bool add_font_from_data(const char *name, const void *data, std::size_t bytes,
                            FontFamily family = FontFamily::Default);
    bool add_system_fallbacks();

    bool layout(const std::vector<LayoutNode> &nodes, float width, float height,
                float pointer_x, float pointer_y, bool pointer_down, float delta_seconds,
                LayoutSnapshot &out, LayoutError *error = nullptr);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nkui

#endif
