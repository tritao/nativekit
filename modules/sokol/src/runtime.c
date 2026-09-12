#define SOKOL_IMPL
#include "nativekit_sokol_runtime.h"

#include <stdint.h>

enum { NK_SOKOL_EXTERNAL_IMAGE_CAPACITY = 4096 };

typedef struct nk_sokol_external_image_slot {
    sg_image image;
    sg_view view;
    uint16_t generation;
    int32_t width;
    int32_t height;
    int active;
} nk_sokol_external_image_slot;

static uint32_t runtime_references;
static int runtime_color_format;
static int runtime_depth_format;
static int runtime_sample_count;
static uint32_t runtime_device;
static nk_sokol_external_image_slot external_images[NK_SOKOL_EXTERNAL_IMAGE_CAPACITY];

static uint32_t external_image_token(uint32_t index, uint16_t generation) {
    return ((uint32_t)generation << 16) | (index + 1u);
}

static nk_sokol_external_image_slot *external_image_slot(uint32_t token) {
    const uint32_t encoded_index = token & 0xFFFFu;
    const uint16_t generation = (uint16_t)(token >> 16);
    if (!encoded_index || encoded_index > NK_SOKOL_EXTERNAL_IMAGE_CAPACITY || !generation)
        return 0;
    nk_sokol_external_image_slot *slot = &external_images[encoded_index - 1u];
    return slot->active && slot->generation == generation ? slot : 0;
}

static int runtime_config_matches(const sg_desc *desc) {
    return runtime_color_format == desc->environment.defaults.color_format &&
           runtime_depth_format == desc->environment.defaults.depth_format &&
           runtime_sample_count == desc->environment.defaults.sample_count;
}

int nk_sokol_runtime_acquire(const sg_desc *desc, nk_graphics_device device) {
    if (!desc || !device.id || runtime_references == UINT32_MAX)
        return 0;
    if (runtime_references &&
        (!runtime_config_matches(desc) || runtime_device != device.id))
        return 0;
    if (!runtime_references) {
        if (sg_isvalid())
            return 0;
        runtime_color_format = desc->environment.defaults.color_format;
        runtime_depth_format = desc->environment.defaults.depth_format;
        runtime_sample_count = desc->environment.defaults.sample_count;
        runtime_device = device.id;
        sg_setup(desc);
        if (!sg_isvalid()) {
            runtime_color_format = 0;
            runtime_depth_format = 0;
            runtime_sample_count = 0;
            runtime_device = 0;
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
        runtime_device = 0;
    }
}

uint32_t nk_sokol_external_image_create(sg_image image, sg_view view, int32_t width,
                                        int32_t height) {
    if (!runtime_references || !sg_isvalid() || !image.id || !view.id || width <= 0 ||
        height <= 0 || runtime_references == UINT32_MAX)
        return 0;
    for (uint32_t index = 0; index < NK_SOKOL_EXTERNAL_IMAGE_CAPACITY; ++index) {
        nk_sokol_external_image_slot *slot = &external_images[index];
        if (slot->active)
            continue;
        if (!slot->generation)
            slot->generation = 1;
        slot->image = image;
        slot->view = view;
        slot->width = width;
        slot->height = height;
        slot->active = 1;
        /* Keep the Sokol runtime alive until the last exported image reference drops. */
        ++runtime_references;
        return external_image_token(index, slot->generation);
    }
    return 0;
}

void nk_sokol_external_image_release(uint32_t token) {
    nk_sokol_external_image_slot *slot = external_image_slot(token);
    if (!slot)
        return;
    sg_destroy_view(slot->view);
    sg_destroy_image(slot->image);
    slot->image.id = 0;
    slot->view.id = 0;
    slot->width = 0;
    slot->height = 0;
    slot->active = 0;
    slot->generation = (uint16_t)(slot->generation + 1u);
    if (!slot->generation)
        slot->generation = 1;
    nk_sokol_runtime_release();
}

int nk_sokol_external_image_resolve(uint32_t token, sg_view *out_view, int32_t *out_width,
                                    int32_t *out_height) {
    nk_sokol_external_image_slot *slot = external_image_slot(token);
    if (!slot || !out_view || !out_width || !out_height ||
        sg_query_view_state(slot->view) != SG_RESOURCESTATE_VALID)
        return 0;
    *out_view = slot->view;
    *out_width = slot->width;
    *out_height = slot->height;
    return 1;
}
