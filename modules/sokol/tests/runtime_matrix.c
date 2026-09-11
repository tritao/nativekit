#include "nativekit_sokol_api.h"
#include "sokol_gfx.h"

int nk_sokol_glcore_runtime_acquire(const sg_desc *desc);
void nk_sokol_glcore_runtime_release(void);
int nk_sokol_gles3_runtime_acquire(const sg_desc *desc);
void nk_sokol_gles3_runtime_release(void);

int main(void) {
    if (!nk_sokol_glcore_runtime_acquire || !nk_sokol_glcore_runtime_release ||
        !nk_sokol_gles3_runtime_acquire || !nk_sokol_gles3_runtime_release)
        return 1;
    const nk_sokol_api *glcore = nk_sokol_glcore_get_api();
    const nk_sokol_api *gles3 = nk_sokol_gles3_get_api();
    if (!glcore || !gles3 || glcore == gles3 || !glcore->gfx || !gles3->gfx ||
        !glcore->runtime_acquire || !gles3->runtime_acquire ||
        glcore->runtime_acquire == gles3->runtime_acquire ||
        glcore->gfx == gles3->gfx || glcore->gfx->make_image == gles3->gfx->make_image)
        return 2;
    return 0;
}
