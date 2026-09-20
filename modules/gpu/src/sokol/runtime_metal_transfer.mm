#include "nativekit_sokol_runtime.h"

#import <Metal/Metal.h>

#if !defined(SOKOL_METAL)
#error "runtime_metal_transfer.mm requires the Sokol Metal backend"
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>

#if __has_feature(objc_arc)
#define NK_MTL_RETAIN(obj) ((void)0)
#define NK_MTL_RELEASE(obj) ((obj) = nil)
#else
#define NK_MTL_RETAIN(obj) [obj retain]
#define NK_MTL_RELEASE(obj)                                                                        \
    do {                                                                                           \
        [obj release];                                                                             \
        (obj) = nil;                                                                               \
    } while (0)
#endif

namespace {

constexpr uint32_t kReadbackCapacity = 128;
constexpr uint32_t kReadbackPending = 1;
constexpr uint32_t kReadbackReady = 2;
constexpr uint32_t kReadbackFailed = 3;

struct ImageInfo {
    id<MTLTexture> texture = nil;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bytes = 0;
};

struct ReadbackSlot {
    uint32_t generation = 0;
    id<MTLBuffer> buffer = nil;
    id<MTLCommandBuffer> command = nil;
    uint32_t size = 0;
    uint32_t row_pitch = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    bool active = false;
};

ReadbackSlot readbacks[kReadbackCapacity];
bool transfer_pass_active = false;
id<MTLCommandBuffer> transfer_command = nil;
id<MTLBlitCommandEncoder> transfer_blit = nil;

bool format_bytes(sg_pixel_format format, uint32_t &out_bytes) {
    switch (format) {
    case SG_PIXELFORMAT_R8:
        out_bytes = 1;
        return true;
    case SG_PIXELFORMAT_RG8:
        out_bytes = 2;
        return true;
    case SG_PIXELFORMAT_RGBA8:
    case SG_PIXELFORMAT_BGRA8:
    case SG_PIXELFORMAT_R32F:
    case SG_PIXELFORMAT_R32UI:
        out_bytes = 4;
        return true;
    case SG_PIXELFORMAT_R16F:
        out_bytes = 2;
        return true;
    case SG_PIXELFORMAT_RG16F:
        out_bytes = 4;
        return true;
    case SG_PIXELFORMAT_RGBA16F:
        out_bytes = 8;
        return true;
    case SG_PIXELFORMAT_RGBA32F:
        out_bytes = 16;
        return true;
    default:
        return false;
    }
}

id<MTLDevice> device() {
    return (__bridge id<MTLDevice>)sg_mtl_device();
}

id<MTLCommandQueue> command_queue() {
    return (__bridge id<MTLCommandQueue>)sg_mtl_command_queue();
}

bool image_info(sg_image image, uint32_t mip_level, uint32_t layer, uint32_t x, uint32_t y,
                uint32_t width, uint32_t height, ImageInfo &out) {
    const sg_image_type type = sg_query_image_type(image);
    if (!image.id || sg_query_image_state(image) != SG_RESOURCESTATE_VALID ||
        (type != SG_IMAGETYPE_2D && type != SG_IMAGETYPE_ARRAY) ||
        mip_level >= static_cast<uint32_t>(sg_query_image_num_mipmaps(image)) ||
        layer >= static_cast<uint32_t>(sg_query_image_num_slices(image)) ||
        sg_query_image_sample_count(image) != 1 || !width || !height)
        return false;
    const sg_mtl_image_info native = sg_mtl_query_image_info(image);
    if (native.active_slot < 0 || native.active_slot >= SG_NUM_INFLIGHT_FRAMES ||
        !native.tex[native.active_slot] ||
        !format_bytes(sg_query_image_pixelformat(image), out.bytes))
        return false;
    out.texture = (__bridge id<MTLTexture>)native.tex[native.active_slot];
    out.width = std::max(1u, static_cast<uint32_t>(sg_query_image_width(image)) >> mip_level);
    out.height = std::max(1u, static_cast<uint32_t>(sg_query_image_height(image)) >> mip_level);
    return x <= out.width && y <= out.height && width <= out.width - x && height <= out.height - y;
}

id<MTLBuffer> buffer_object(sg_buffer buffer, uint32_t &out_size) {
    if (!buffer.id || sg_query_buffer_state(buffer) != SG_RESOURCESTATE_VALID ||
        sg_query_buffer_size(buffer) > UINT32_MAX)
        return nil;
    const sg_mtl_buffer_info native = sg_mtl_query_buffer_info(buffer);
    if (native.active_slot < 0 || native.active_slot >= SG_NUM_INFLIGHT_FRAMES ||
        !native.buf[native.active_slot])
        return nil;
    out_size = static_cast<uint32_t>(sg_query_buffer_size(buffer));
    return (__bridge id<MTLBuffer>)native.buf[native.active_slot];
}

bool ensure_blit(bool &temporary) {
    if (transfer_blit) {
        temporary = false;
        return true;
    }
    id<MTLCommandQueue> queue = command_queue();
    if (!queue)
        return false;
    /* Sokol may have an already-ended render/compute encoder queued for this
       frame. Commit it before opening a separate blit command buffer so the
       queue order is explicit. */
    if (!transfer_pass_active)
        sg_commit();
    transfer_command = [queue commandBuffer];
    if (!transfer_command)
        return false;
    [transfer_command enqueue];
    transfer_blit = [transfer_command blitCommandEncoder];
    if (!transfer_blit) {
        transfer_command = nil;
        return false;
    }
    NK_MTL_RETAIN(transfer_command);
    NK_MTL_RETAIN(transfer_blit);
    temporary = true;
    return true;
}

bool finish_temporary(bool temporary, bool wait) {
    if (!temporary)
        return true;
    [transfer_blit endEncoding];
    NK_MTL_RELEASE(transfer_blit);
    [transfer_command commit];
    if (wait)
        [transfer_command waitUntilCompleted];
    const bool success = transfer_command.status != MTLCommandBufferStatusError;
    NK_MTL_RELEASE(transfer_command);
    return success;
}

uint32_t readback_token(uint32_t index, uint32_t generation) {
    return (generation << 16) | (index + 1u);
}

ReadbackSlot *readback_slot(uint32_t token) {
    const uint32_t encoded_index = token & 0xFFFFu;
    const uint32_t generation = token >> 16;
    if (!encoded_index || encoded_index > kReadbackCapacity || !generation)
        return nullptr;
    ReadbackSlot &slot = readbacks[encoded_index - 1u];
    return slot.active && slot.generation == generation ? &slot : nullptr;
}

void release_readback(ReadbackSlot &slot) {
    NK_MTL_RELEASE(slot.buffer);
    NK_MTL_RELEASE(slot.command);
    slot.size = 0;
    slot.row_pitch = 0;
    slot.width = 0;
    slot.height = 0;
    slot.active = false;
    slot.generation = (slot.generation % 0xFFFFu) + 1u;
    if (!slot.generation)
        slot.generation = 1;
}

uint32_t metal_buffer_copy(sg_buffer source, uint32_t source_offset, sg_buffer destination,
                           uint32_t destination_offset, uint32_t size) {
    uint32_t source_size = 0;
    uint32_t destination_size = 0;
    id<MTLBuffer> source_buffer = buffer_object(source, source_size);
    id<MTLBuffer> destination_buffer = buffer_object(destination, destination_size);
    if (!source_buffer || !destination_buffer || !size || source_offset > source_size ||
        size > source_size - source_offset || destination_offset > destination_size ||
        size > destination_size - destination_offset)
        return 0;
    bool temporary = false;
    if (!ensure_blit(temporary))
        return 0;
    [transfer_blit copyFromBuffer:source_buffer
                     sourceOffset:source_offset
                         toBuffer:destination_buffer
                destinationOffset:destination_offset
                             size:size];
    return finish_temporary(temporary, true) ? 1u : 0u;
}

uint32_t metal_image_copy(sg_image source, uint32_t source_mip, uint32_t source_layer,
                          uint32_t source_x, uint32_t source_y, sg_image destination,
                          uint32_t destination_mip, uint32_t destination_layer,
                          uint32_t destination_x, uint32_t destination_y, uint32_t width,
                          uint32_t height) {
    ImageInfo source_info;
    ImageInfo destination_info;
    if (!image_info(source, source_mip, source_layer, source_x, source_y, width, height,
                    source_info) ||
        !image_info(destination, destination_mip, destination_layer, destination_x, destination_y,
                    width, height, destination_info) ||
        source_info.bytes != destination_info.bytes)
        return 0;
    bool temporary = false;
    if (!ensure_blit(temporary))
        return 0;
    [transfer_blit copyFromTexture:source_info.texture
                       sourceSlice:source_layer
                       sourceLevel:source_mip
                      sourceOrigin:MTLOriginMake(source_x, source_y, 0)
                        sourceSize:MTLSizeMake(width, height, 1)
                         toTexture:destination_info.texture
                  destinationSlice:destination_layer
                  destinationLevel:destination_mip
                 destinationOrigin:MTLOriginMake(destination_x, destination_y, 0)];
    return finish_temporary(temporary, true) ? 1u : 0u;
}

uint32_t metal_buffer_to_image(sg_buffer source, uint32_t source_offset, uint32_t row_pitch,
                               sg_image destination, uint32_t mip_level, uint32_t layer, uint32_t x,
                               uint32_t y, uint32_t width, uint32_t height) {
    ImageInfo destination_info;
    uint32_t source_size = 0;
    id<MTLBuffer> source_buffer = buffer_object(source, source_size);
    if (!source_buffer ||
        !image_info(destination, mip_level, layer, x, y, width, height, destination_info) ||
        width > UINT32_MAX / destination_info.bytes || row_pitch < width * destination_info.bytes ||
        row_pitch % destination_info.bytes != 0)
        return 0;
    const uint64_t transfer_size = static_cast<uint64_t>(row_pitch) * height;
    if (!transfer_size || transfer_size > UINT32_MAX || source_offset > source_size ||
        transfer_size > source_size - source_offset)
        return 0;
    id<MTLBuffer> staging = [device() newBufferWithLength:transfer_size
                                                  options:MTLResourceStorageModeShared];
    if (!staging)
        return 0;
    bool temporary = false;
    if (!ensure_blit(temporary)) {
        NK_MTL_RELEASE(staging);
        return 0;
    }
    [transfer_blit copyFromBuffer:source_buffer
                     sourceOffset:source_offset
                         toBuffer:staging
                destinationOffset:0
                             size:transfer_size];
    [transfer_blit copyFromBuffer:staging
                     sourceOffset:0
                sourceBytesPerRow:row_pitch
              sourceBytesPerImage:0
                       sourceSize:MTLSizeMake(width, height, 1)
                        toTexture:destination_info.texture
                 destinationSlice:layer
                 destinationLevel:mip_level
                destinationOrigin:MTLOriginMake(x, y, 0)];
    const bool success = finish_temporary(temporary, true);
    NK_MTL_RELEASE(staging);
    return success ? 1u : 0u;
}

uint32_t metal_image_to_buffer(sg_image source, uint32_t mip_level, uint32_t layer, uint32_t x,
                               uint32_t y, uint32_t width, uint32_t height, sg_buffer destination,
                               uint32_t destination_offset, uint32_t row_pitch) {
    ImageInfo source_info;
    uint32_t destination_size = 0;
    id<MTLBuffer> destination_buffer = buffer_object(destination, destination_size);
    if (!destination_buffer ||
        !image_info(source, mip_level, layer, x, y, width, height, source_info) ||
        width > UINT32_MAX / source_info.bytes || row_pitch < width * source_info.bytes ||
        row_pitch % source_info.bytes != 0)
        return 0;
    const uint64_t transfer_size = static_cast<uint64_t>(row_pitch) * height;
    if (!transfer_size || transfer_size > UINT32_MAX || destination_offset > destination_size ||
        transfer_size > destination_size - destination_offset)
        return 0;
    id<MTLBuffer> staging = [device() newBufferWithLength:transfer_size
                                                  options:MTLResourceStorageModeShared];
    if (!staging)
        return 0;
    bool temporary = false;
    if (!ensure_blit(temporary)) {
        NK_MTL_RELEASE(staging);
        return 0;
    }
    [transfer_blit copyFromTexture:source_info.texture
                       sourceSlice:layer
                       sourceLevel:mip_level
                      sourceOrigin:MTLOriginMake(x, y, 0)
                        sourceSize:MTLSizeMake(width, height, 1)
                          toBuffer:staging
                 destinationOffset:0
            destinationBytesPerRow:row_pitch
          destinationBytesPerImage:0];
    [transfer_blit copyFromBuffer:staging
                     sourceOffset:0
                         toBuffer:destination_buffer
                destinationOffset:destination_offset
                             size:transfer_size];
    const bool success = finish_temporary(temporary, true);
    NK_MTL_RELEASE(staging);
    return success ? 1u : 0u;
}

uint32_t metal_readback_begin(sg_image source, uint32_t mip_level, uint32_t layer, uint32_t x,
                              uint32_t y, uint32_t width, uint32_t height) {
    ImageInfo source_info;
    if (!image_info(source, mip_level, layer, x, y, width, height, source_info) ||
        width > UINT32_MAX / source_info.bytes || height > UINT32_MAX / (width * source_info.bytes))
        return 0;
    uint32_t index = kReadbackCapacity;
    for (uint32_t i = 0; i < kReadbackCapacity; ++i) {
        if (!readbacks[i].active) {
            index = i;
            break;
        }
    }
    if (index == kReadbackCapacity)
        return 0;
    const uint32_t row_pitch = width * source_info.bytes;
    const uint32_t size = row_pitch * height;
    id<MTLBuffer> staging = [device() newBufferWithLength:size
                                                  options:MTLResourceStorageModeShared];
    if (!staging)
        return 0;
    bool temporary = false;
    if (!ensure_blit(temporary)) {
        NK_MTL_RELEASE(staging);
        return 0;
    }
    [transfer_blit copyFromTexture:source_info.texture
                       sourceSlice:layer
                       sourceLevel:mip_level
                      sourceOrigin:MTLOriginMake(x, y, 0)
                        sourceSize:MTLSizeMake(width, height, 1)
                          toBuffer:staging
                 destinationOffset:0
            destinationBytesPerRow:row_pitch
          destinationBytesPerImage:0];
    ReadbackSlot &slot = readbacks[index];
    if (!slot.generation)
        slot.generation = 1;
    slot.buffer = staging;
    NK_MTL_RETAIN(slot.buffer);
    slot.command = transfer_command;
    NK_MTL_RETAIN(slot.command);
    slot.size = size;
    slot.row_pitch = row_pitch;
    slot.width = width;
    slot.height = height;
    slot.active = true;
    if (temporary && !finish_temporary(true, false)) {
        release_readback(slot);
        NK_MTL_RELEASE(staging);
        return 0;
    }
    NK_MTL_RELEASE(staging);
    return readback_token(index, slot.generation);
}

uint32_t metal_readback_status(uint32_t token) {
    ReadbackSlot *slot = readback_slot(token);
    if (!slot || !slot->command)
        return kReadbackFailed;
    switch (slot->command.status) {
    case MTLCommandBufferStatusCompleted:
        return kReadbackReady;
    case MTLCommandBufferStatusError:
        return kReadbackFailed;
    default:
        return kReadbackPending;
    }
}

uint32_t metal_readback_size(uint32_t token) {
    ReadbackSlot *slot = readback_slot(token);
    return slot ? slot->size : 0;
}

uint32_t metal_readback_row_pitch(uint32_t token) {
    ReadbackSlot *slot = readback_slot(token);
    return slot ? slot->row_pitch : 0;
}

int metal_readback_read(uint32_t token, void *destination, uint32_t size) {
    ReadbackSlot *slot = readback_slot(token);
    if (!slot || !destination || size < slot->size ||
        metal_readback_status(token) != kReadbackReady)
        return 0;
    std::memcpy(destination, [slot->buffer contents], slot -> size);
    return 1;
}

void metal_readback_destroy(uint32_t token) {
    if (ReadbackSlot *slot = readback_slot(token))
        release_readback(*slot);
}

int metal_begin_pass() {
    if (transfer_pass_active || transfer_command)
        return 0;
    /* Finish the preceding Sokol command buffer before this pass owns the
       queue with its private blit encoder. */
    sg_commit();
    transfer_pass_active = true;
    return 1;
}

int metal_end_pass() {
    if (!transfer_pass_active)
        return 1;
    if (transfer_blit) {
        [transfer_blit endEncoding];
        NK_MTL_RELEASE(transfer_blit);
        [transfer_command commit];
        NK_MTL_RELEASE(transfer_command);
    }
    transfer_pass_active = false;
    return true;
}

const nk_sokol_transfer_api transfer_api = {
    metal_buffer_copy,    metal_image_copy,       metal_buffer_to_image, metal_image_to_buffer,
    metal_readback_begin, metal_readback_status,  metal_readback_size,   metal_readback_row_pitch,
    metal_readback_read,  metal_readback_destroy, metal_begin_pass,      metal_end_pass,
};

} // namespace

extern "C" const nk_sokol_transfer_api *nk_sokol_metal_transfer_get_api(void) {
    return &transfer_api;
}

extern "C" int nk_sokol_metal_query_max_samples(void) {
    id<MTLDevice> native_device = device();
    if (!native_device)
        return 1;
    int max_samples = 1;
    for (NSUInteger samples = 2; samples <= 32; samples *= 2) {
        if (![native_device supportsTextureSampleCount:samples])
            break;
        max_samples = static_cast<int>(samples);
    }
    return max_samples;
}

extern "C" void nk_sokol_metal_transfer_shutdown(void) {
    if (transfer_blit) {
        [transfer_blit endEncoding];
        NK_MTL_RELEASE(transfer_blit);
    }
    if (transfer_command) {
        [transfer_command commit];
        [transfer_command waitUntilCompleted];
        NK_MTL_RELEASE(transfer_command);
    }
    transfer_pass_active = false;
    for (auto &slot : readbacks) {
        if (slot.active) {
            if (slot.command)
                [slot.command waitUntilCompleted];
            release_readback(slot);
        }
    }
}
