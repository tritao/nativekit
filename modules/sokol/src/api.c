#include "nativekit_sokol_api.h"

#include "nativekit_sokol_runtime.h"

static nk_sokol_api api;

const nk_sokol_api *nk_sokol_get_api(void) {
    if (!api.gfx) {
        api.gfx = sg_query_api();
        api.runtime_acquire = nk_sokol_runtime_acquire;
        api.runtime_release = nk_sokol_runtime_release;
        api.external_image_create = nk_sokol_external_image_create;
        api.external_image_release = nk_sokol_external_image_release;
        api.external_image_resolve = nk_sokol_external_image_resolve;
    }
    return &api;
}
