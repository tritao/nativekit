#ifndef NATIVEKIT_UI_LAYOUT_ENGINE_H
#define NATIVEKIT_UI_LAYOUT_ENGINE_H

#include "layout/layout_types.h"

#include <cstddef>
#include <memory>

namespace nkui {

struct LayoutError {
    std::size_t node_index = 0;
    const char *message = nullptr;
};

/**
 * Private NativeKit layout boundary. Clay and its types are deliberately
 * hidden in the implementation; callers submit NativeKit-owned nodes and
 * receive a NativeKit-owned layout snapshot.
 */
class LayoutEngine {
  public:
    struct State;

    explicit LayoutEngine(std::size_t max_nodes = 512);
    ~LayoutEngine();
    LayoutEngine(const LayoutEngine &) = delete;
    LayoutEngine &operator=(const LayoutEngine &) = delete;

    bool valid() const;
    bool add_font(const char *path);
    bool add_system_fallbacks();

    bool layout(const std::vector<LayoutNode> &nodes, float width, float height,
                float pointer_x, float pointer_y, bool pointer_down, float delta_seconds,
                LayoutSnapshot &out, LayoutError *error = nullptr);

  private:
    std::unique_ptr<State> state_;
};

} // namespace nkui

#endif
