#ifndef NATIVEKIT_UI_LAYOUT_BACKEND_H
#define NATIVEKIT_UI_LAYOUT_BACKEND_H

#include "layout/layout_types.h"

#include <cstddef>
#include <memory>

namespace nkui {

/**
 * NativeKit's replaceable box-layout contract. Implementations may use Clay
 * or another solver, but callers only exchange NativeKit-owned values.
 */
class LayoutBackend {
  public:
    virtual ~LayoutBackend() = default;

    virtual bool valid() const = 0;
    virtual bool add_font(const char *path, FontFamily family) = 0;
    virtual bool add_font_from_data(const char *name, const void *data, std::size_t bytes,
                                    FontFamily family) = 0;
    virtual bool add_system_fallbacks() = 0;
    virtual bool layout(const std::vector<LayoutNode> &nodes, float width, float height,
                        float pointer_x, float pointer_y, bool pointer_down,
                        float delta_seconds, LayoutSnapshot &out, LayoutError *error) = 0;
};

std::unique_ptr<LayoutBackend> make_clay_layout_backend(std::size_t max_nodes);

} // namespace nkui

#endif
