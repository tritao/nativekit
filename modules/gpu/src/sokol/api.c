#include "nativekit_sokol_api.h"

#include "nativekit_sokol_runtime.h"

static nk_sokol_api api;

const nk_sokol_api *nk_sokol_get_api(void) {
    if (!api.gfx) {
        api.gfx = sg_query_api();
        api.query_features = sg_query_features;
        api.query_limits = sg_query_limits;
        api.update_buffer = sg_update_buffer;
        api.apply_viewport = sg_apply_viewport;
        api.runtime_acquire = nk_sokol_runtime_acquire;
        api.runtime_is_compatible = nk_sokol_runtime_is_compatible;
        api.runtime_release = nk_sokol_runtime_release;
        api.external_image_create = nk_sokol_external_image_create;
        api.external_image_release = nk_sokol_external_image_release;
        api.external_image_resolve = nk_sokol_external_image_resolve;
    }
    return &api;
}
