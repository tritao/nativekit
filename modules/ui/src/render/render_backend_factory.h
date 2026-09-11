#ifndef NATIVEKIT_UI_RENDER_BACKEND_FACTORY_H
#define NATIVEKIT_UI_RENDER_BACKEND_FACTORY_H

#include "render_backend.h"

#include <memory>

namespace nkui {

/** Creates the renderer implementation compatible with a surface API. */
std::unique_ptr<RenderBackend> create_render_backend(nk_graphics_api api);

} // namespace nkui

#endif
