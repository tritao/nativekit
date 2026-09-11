#include "render_backend_factory.h"

#include "sokol_backend.h"

namespace nkui {

std::unique_ptr<RenderBackend> create_render_backend(nk_graphics_api api) {
    // The current Sokol runtime is compiled for desktop OpenGL. Keeping the
    // selection here makes unsupported surface APIs explicit and gives future
    // GLES/Vulkan implementations one stable insertion point.
    if (api != NK_GRAPHICS_OPENGL)
        return nullptr;
    return std::make_unique<SokolBackend>();
}

} // namespace nkui
