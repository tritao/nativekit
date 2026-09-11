#include "render_backend_factory.h"

#include "nativekit_sokol_backend_config.h"
#include "sokol_backend.h"

namespace nkui {

std::unique_ptr<RenderBackend> create_render_backend(nk_graphics_api api) {
#if defined(NK_SOKOL_BACKEND_GLES3)
    constexpr nk_graphics_api supported_api = NK_GRAPHICS_OPENGL_ES;
#else
    constexpr nk_graphics_api supported_api = NK_GRAPHICS_OPENGL;
#endif
    if (api != supported_api)
        return nullptr;
    return std::make_unique<SokolBackend>();
}

} // namespace nkui
