#include "sokol_gfx.h"

int nk_sokol_glcore_runtime_acquire(const sg_desc *desc);
void nk_sokol_glcore_runtime_release(void);
int nk_sokol_gles3_runtime_acquire(const sg_desc *desc);
void nk_sokol_gles3_runtime_release(void);

int main(void) {
    return nk_sokol_glcore_runtime_acquire && nk_sokol_glcore_runtime_release &&
                   nk_sokol_gles3_runtime_acquire && nk_sokol_gles3_runtime_release
               ? 0
               : 1;
}
