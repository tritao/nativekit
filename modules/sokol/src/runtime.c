#define SOKOL_IMPL
#include "nativekit_sokol_runtime.h"

#include <stdint.h>

static uint32_t runtime_references;

int nk_sokol_runtime_acquire(const sg_desc *desc) {
    if (!desc || runtime_references == UINT32_MAX)
        return 0;
    if (!runtime_references) {
        if (sg_isvalid())
            return 0;
        sg_setup(desc);
        if (!sg_isvalid())
            return 0;
    }
    ++runtime_references;
    return 1;
}

void nk_sokol_runtime_release(void) {
    if (!runtime_references)
        return;
    --runtime_references;
    if (!runtime_references)
        sg_shutdown();
}
