#include "render_backend_factory.h"

#include "nativekit_sokol_api.h"
#include "sokol_backend.h"

namespace nkui {

std::unique_ptr<RenderBackend> create_render_backend(nk_graphics_api api) {
#if defined(NKUI_SOKOL_RUNTIME_MATRIX)
    const nk_sokol_api *sokol_api = nullptr;
    switch (api) {
    case NK_GRAPHICS_OPENGL:
        sokol_api = nk_sokol_glcore_get_api();
        break;
    case NK_GRAPHICS_OPENGL_ES:
        sokol_api = nk_sokol_gles3_get_api();
        break;
    default:
        break;
    }
    if (!sokol_api)
        return nullptr;
#else
    #if defined(NK_SOKOL_BACKEND_GLES3)
        if (api != NK_GRAPHICS_OPENGL_ES)
            return nullptr;
    #else
        if (api != NK_GRAPHICS_OPENGL)
            return nullptr;
    #endif
    const nk_sokol_api *sokol_api = nk_sokol_get_api();
#endif
    return sokol_api ? std::make_unique<SokolBackend>(sokol_api) : nullptr;
}

} // namespace nkui
