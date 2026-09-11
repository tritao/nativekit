#include "layout/layout_engine.h"

#include "layout/layout_backend.h"

namespace nkui {

LayoutEngine::LayoutEngine(std::size_t max_nodes)
    : backend_(make_clay_layout_backend(max_nodes)) {}

LayoutEngine::~LayoutEngine() = default;

bool LayoutEngine::valid() const {
    return backend_ && backend_->valid();
}

bool LayoutEngine::add_font(const char *path, FontFamily family) {
    return backend_ && backend_->add_font(path, family);
}

bool LayoutEngine::add_font_from_data(const char *name, const void *data, std::size_t bytes,
                                      FontFamily family) {
    return backend_ && backend_->add_font_from_data(name, data, bytes, family);
}

bool LayoutEngine::add_system_fallbacks() {
    return backend_ && backend_->add_system_fallbacks();
}

bool LayoutEngine::layout(const std::vector<LayoutNode> &nodes, float width, float height,
                          float pointer_x, float pointer_y, bool pointer_down,
                          float delta_seconds, LayoutSnapshot &out, LayoutError *error) {
    if (!backend_) {
        if (error)
            error->message = "layout backend is unavailable";
        return false;
    }
    return backend_->layout(nodes, width, height, pointer_x, pointer_y, pointer_down,
                            delta_seconds, out, error);
}

} // namespace nkui
