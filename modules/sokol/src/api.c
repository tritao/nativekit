#include "nativekit_sokol_api.h"

#include "nativekit_sokol_runtime.h"

static nk_sokol_api api;

const nk_sokol_api *nk_sokol_get_api(void) {
    if (!api.gfx) {
        api.gfx = sg_query_api();
        api.runtime_acquire = nk_sokol_runtime_acquire;
        api.runtime_release = nk_sokol_runtime_release;
    }
    return &api;
}
