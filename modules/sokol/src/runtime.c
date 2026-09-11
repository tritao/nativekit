#define SOKOL_IMPL
#include "nativekit_sokol_runtime.h"

#include <stdint.h>

static uint32_t runtime_references;
static int runtime_color_format;
static int runtime_depth_format;
static int runtime_sample_count;

static int runtime_config_matches(const sg_desc *desc) {
    return runtime_color_format == desc->environment.defaults.color_format &&
           runtime_depth_format == desc->environment.defaults.depth_format &&
           runtime_sample_count == desc->environment.defaults.sample_count;
}

int nk_sokol_runtime_acquire(const sg_desc *desc) {
    if (!desc || runtime_references == UINT32_MAX)
        return 0;
    if (runtime_references && !runtime_config_matches(desc))
        return 0;
    if (!runtime_references) {
        if (sg_isvalid())
            return 0;
        runtime_color_format = desc->environment.defaults.color_format;
        runtime_depth_format = desc->environment.defaults.depth_format;
        runtime_sample_count = desc->environment.defaults.sample_count;
        sg_setup(desc);
        if (!sg_isvalid()) {
            runtime_color_format = 0;
            runtime_depth_format = 0;
            runtime_sample_count = 0;
            return 0;
        }
    }
    ++runtime_references;
    return 1;
}

void nk_sokol_runtime_release(void) {
    if (!runtime_references)
        return;
    --runtime_references;
    if (!runtime_references) {
        sg_shutdown();
        runtime_color_format = 0;
        runtime_depth_format = 0;
        runtime_sample_count = 0;
    }
}
