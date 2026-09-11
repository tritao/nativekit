#define SOKOL_GLCORE
#include "sokol_gfx.h"

#include "ui_toolchain_probe.glsl.h"

namespace nkui {

bool shader_toolchain_probe() {
    const sg_shader_desc *desc =
        ui_toolchain_probe_ui_toolchain_probe_shader_desc(sg_query_backend());
    return desc != nullptr && desc->vertex_func.source != nullptr &&
           desc->fragment_func.source != nullptr;
}

} // namespace nkui
