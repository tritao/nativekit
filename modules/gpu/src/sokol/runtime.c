#define SOKOL_IMPL
#include "nativekit_sokol_runtime.h"

#if defined(NKGPU_TESTING)
#include <stdio.h>
#endif
#include <stdlib.h>
#include <string.h>
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
static uint64_t runtime_device;
static nk_sokol_external_image_slot external_images[NK_SOKOL_EXTERNAL_IMAGE_CAPACITY];

#if defined(_SOKOL_ANY_GL)
enum { NK_SOKOL_READBACK_CAPACITY = 128 };

typedef struct nk_sokol_readback_slot {
    uint32_t generation;
    uint32_t pbo;
    void *fence;
    uint32_t size;
    uint32_t row_pitch;
    uint32_t width;
    uint32_t height;
    int active;
} nk_sokol_readback_slot;

static nk_sokol_readback_slot readbacks[NK_SOKOL_READBACK_CAPACITY];

#ifndef GL_COPY_READ_BUFFER
#define GL_COPY_READ_BUFFER 0x8F36
#endif
#ifndef GL_COPY_WRITE_BUFFER
#define GL_COPY_WRITE_BUFFER 0x8F37
#endif
#ifndef GL_PIXEL_PACK_BUFFER
#define GL_PIXEL_PACK_BUFFER 0x88EB
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER
#define GL_PIXEL_UNPACK_BUFFER 0x88EC
#endif
#ifndef GL_PIXEL_PACK_BUFFER_BINDING
#define GL_PIXEL_PACK_BUFFER_BINDING 0x88ED
#endif
#ifndef GL_PIXEL_UNPACK_BUFFER_BINDING
#define GL_PIXEL_UNPACK_BUFFER_BINDING 0x88EF
#endif
#ifndef GL_READ_FRAMEBUFFER_BINDING
#define GL_READ_FRAMEBUFFER_BINDING 0x8CAA
#endif
#ifndef GL_DRAW_FRAMEBUFFER_BINDING
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_SYNC_FLUSH_COMMANDS_BIT
#define GL_SYNC_FLUSH_COMMANDS_BIT 0x00000001
#endif
#ifndef GL_ALREADY_SIGNALED
#define GL_ALREADY_SIGNALED 0x911A
#endif
#ifndef GL_CONDITION_SATISFIED
#define GL_CONDITION_SATISFIED 0x911C
#endif
#ifndef GL_TIMEOUT_EXPIRED
#define GL_TIMEOUT_EXPIRED 0x911B
#endif
#ifndef GL_WAIT_FAILED
#define GL_WAIT_FAILED 0x911D
#endif
#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_NO_ERROR
#define GL_NO_ERROR 0
#endif

static uint32_t readback_token(uint32_t index, uint32_t generation) {
    return (generation << 16) | (index + 1u);
}

static nk_sokol_readback_slot *readback_slot(uint32_t token) {
    const uint32_t encoded_index = token & 0xFFFFu;
    const uint32_t generation = token >> 16;
    if (!encoded_index || encoded_index > NK_SOKOL_READBACK_CAPACITY || !generation)
        return 0;
    nk_sokol_readback_slot *slot = &readbacks[encoded_index - 1u];
    return slot->active && slot->generation == generation ? slot : 0;
}

static void readback_release(nk_sokol_readback_slot *slot) {
    if (!slot)
        return;
    if (slot->fence)
        glDeleteSync(slot->fence);
    if (slot->pbo)
        glDeleteBuffers(1, &slot->pbo);
    slot->fence = 0;
    slot->pbo = 0;
    slot->size = 0;
    slot->row_pitch = 0;
    slot->width = 0;
    slot->height = 0;
    slot->active = 0;
    slot->generation = (slot->generation % 0xFFFFu) + 1u;
    if (!slot->generation)
        slot->generation = 1;
}

static void clear_gl_errors(void) {
    while (glGetError() != GL_NO_ERROR) {
    }
}

static int image_format_io(sg_pixel_format format, GLenum *out_format, GLenum *out_type,
                           uint32_t *out_bytes) {
    if (!out_format || !out_type || !out_bytes)
        return 0;
    switch (format) {
    case SG_PIXELFORMAT_R8:
        *out_format = GL_RED;
        *out_type = GL_UNSIGNED_BYTE;
        *out_bytes = 1;
        return 1;
    case SG_PIXELFORMAT_RG8:
        *out_format = GL_RG;
        *out_type = GL_UNSIGNED_BYTE;
        *out_bytes = 2;
        return 1;
    case SG_PIXELFORMAT_RGBA8:
        *out_format = GL_RGBA;
        *out_type = GL_UNSIGNED_BYTE;
        *out_bytes = 4;
        return 1;
    case SG_PIXELFORMAT_BGRA8:
        *out_format = GL_BGRA;
        *out_type = GL_UNSIGNED_BYTE;
        *out_bytes = 4;
        return 1;
    case SG_PIXELFORMAT_R16F:
        *out_format = GL_RED;
        *out_type = GL_HALF_FLOAT;
        *out_bytes = 2;
        return 1;
    case SG_PIXELFORMAT_RG16F:
        *out_format = GL_RG;
        *out_type = GL_HALF_FLOAT;
        *out_bytes = 4;
        return 1;
    case SG_PIXELFORMAT_RGBA16F:
        *out_format = GL_RGBA;
        *out_type = GL_HALF_FLOAT;
        *out_bytes = 8;
        return 1;
    case SG_PIXELFORMAT_R32F:
        *out_format = GL_RED;
        *out_type = GL_FLOAT;
        *out_bytes = 4;
        return 1;
    case SG_PIXELFORMAT_RGBA32F:
        *out_format = GL_RGBA;
        *out_type = GL_FLOAT;
        *out_bytes = 16;
        return 1;
    case SG_PIXELFORMAT_R32UI:
        *out_format = GL_RED_INTEGER;
        *out_type = GL_UNSIGNED_INT;
        *out_bytes = 4;
        return 1;
    default:
        return 0;
    }
}

static int image_transfer_info(sg_image image, uint32_t mip_level, uint32_t layer,
                               uint32_t *out_texture, GLenum *out_target, uint32_t *out_width,
                               uint32_t *out_height, GLenum *out_format, GLenum *out_type,
                               uint32_t *out_bytes) {
    if (!image.id || sg_query_image_state(image) != SG_RESOURCESTATE_VALID ||
        sg_query_image_type(image) != SG_IMAGETYPE_2D || layer != 0 ||
        mip_level >= (uint32_t)sg_query_image_num_mipmaps(image) ||
        sg_query_image_sample_count(image) != 1)
        return 0;
    const sg_gl_image_info info = sg_gl_query_image_info(image);
    if (info.active_slot < 0 || info.active_slot >= SG_NUM_INFLIGHT_FRAMES ||
        !info.tex[info.active_slot] || info.tex_target != GL_TEXTURE_2D)
        return 0;
    const int base_width = sg_query_image_width(image);
    const int base_height = sg_query_image_height(image);
    if (base_width <= 0 || base_height <= 0)
        return 0;
    uint32_t width = (uint32_t)base_width;
    uint32_t height = (uint32_t)base_height;
    for (uint32_t mip = 0; mip < mip_level; ++mip) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
    }
    GLenum format = 0;
    GLenum type = 0;
    uint32_t bytes = 0;
    if (!image_format_io(sg_query_image_pixelformat(image), &format, &type, &bytes))
        return 0;
    *out_texture = info.tex[info.active_slot];
    *out_target = (GLenum)info.tex_target;
    *out_width = width;
    *out_height = height;
    *out_format = format;
    *out_type = type;
    *out_bytes = bytes;
    return 1;
}

static int buffer_transfer_info(sg_buffer buffer, uint32_t *out_buffer) {
    if (!buffer.id || sg_query_buffer_state(buffer) != SG_RESOURCESTATE_VALID || !out_buffer)
        return 0;
    const sg_gl_buffer_info info = sg_gl_query_buffer_info(buffer);
    if (info.active_slot < 0 || info.active_slot >= SG_NUM_INFLIGHT_FRAMES ||
        !info.buf[info.active_slot])
        return 0;
    *out_buffer = info.buf[info.active_slot];
    return 1;
}

static int image_region_valid(sg_image image, uint32_t mip_level, uint32_t layer, uint32_t x,
                              uint32_t y, uint32_t width, uint32_t height, uint32_t *out_bytes,
                              uint32_t *out_mip_width, uint32_t *out_mip_height) {
    uint32_t texture = 0;
    GLenum target = 0;
    GLenum format = 0;
    GLenum type = 0;
    uint32_t mip_width = 0;
    uint32_t mip_height = 0;
    uint32_t bytes = 0;
    if (!width || !height ||
        !image_transfer_info(image, mip_level, layer, &texture, &target, &mip_width,
                             &mip_height, &format, &type, &bytes) ||
        x > mip_width || y > mip_height || width > mip_width - x || height > mip_height - y)
        return 0;
    if (out_bytes)
        *out_bytes = bytes;
    if (out_mip_width)
        *out_mip_width = mip_width;
    if (out_mip_height)
        *out_mip_height = mip_height;
    return 1;
}

static uint32_t nk_sokol_buffer_copy(sg_buffer source, uint32_t source_offset,
                                     sg_buffer destination, uint32_t destination_offset,
                                     uint32_t size) {
    uint32_t source_buffer = 0;
    uint32_t destination_buffer = 0;
    if (!size || !buffer_transfer_info(source, &source_buffer) ||
        !buffer_transfer_info(destination, &destination_buffer))
        return 0;
    clear_gl_errors();
    glBindBuffer(GL_COPY_READ_BUFFER, source_buffer);
    glBindBuffer(GL_COPY_WRITE_BUFFER, destination_buffer);
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, (GLintptr)source_offset,
                        (GLintptr)destination_offset, (GLsizeiptr)size);
    glBindBuffer(GL_COPY_READ_BUFFER, 0);
    glBindBuffer(GL_COPY_WRITE_BUFFER, 0);
    const GLenum error = glGetError();
    sg_reset_state_cache();
    return error == GL_NO_ERROR;
}

static uint32_t nk_sokol_image_copy(sg_image source, uint32_t source_mip, uint32_t source_layer,
                                    uint32_t source_x, uint32_t source_y, sg_image destination,
                                    uint32_t destination_mip, uint32_t destination_layer,
                                    uint32_t destination_x, uint32_t destination_y,
                                    uint32_t width, uint32_t height) {
    uint32_t source_texture = 0;
    uint32_t destination_texture = 0;
    GLenum source_target = 0;
    GLenum destination_target = 0;
    uint32_t source_width = 0;
    uint32_t source_height = 0;
    uint32_t destination_width = 0;
    uint32_t destination_height = 0;
    GLenum source_format = 0;
    GLenum source_type = 0;
    uint32_t source_bytes = 0;
    GLenum destination_format = 0;
    GLenum destination_type = 0;
    uint32_t destination_bytes = 0;
    if (!image_transfer_info(source, source_mip, source_layer, &source_texture, &source_target,
                             &source_width, &source_height, &source_format, &source_type,
                             &source_bytes) ||
        !image_transfer_info(destination, destination_mip, destination_layer, &destination_texture,
                             &destination_target, &destination_width, &destination_height,
                             &destination_format, &destination_type, &destination_bytes) ||
        source_format != destination_format || source_type != destination_type ||
        source_bytes != destination_bytes || !width || !height || source_x > source_width ||
        source_y > source_height || destination_x > destination_width ||
        destination_y > destination_height || width > source_width - source_x ||
        height > source_height - source_y || width > destination_width - destination_x ||
        height > destination_height - destination_y)
        return 0;

    GLint old_read_framebuffer = 0;
    GLint old_draw_framebuffer = 0;
    GLuint framebuffers[2] = {0, 0};
    clear_gl_errors();
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_framebuffer);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_framebuffer);
    glGenFramebuffers(2, framebuffers);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffers[0]);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, source_target, source_texture,
                           (GLint)source_mip);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffers[1]);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, destination_target,
                           destination_texture, (GLint)destination_mip);
    const GLenum read_status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    const GLenum draw_status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (read_status == GL_FRAMEBUFFER_COMPLETE && draw_status == GL_FRAMEBUFFER_COMPLETE) {
        const GLint source_gl_y = (GLint)source_height - (GLint)source_y - (GLint)height;
        const GLint destination_gl_y = (GLint)destination_height - (GLint)destination_y - (GLint)height;
        glBlitFramebuffer((GLint)source_x, source_gl_y, (GLint)(source_x + width),
                          source_gl_y + (GLint)height, (GLint)destination_x, destination_gl_y,
                          (GLint)(destination_x + width), destination_gl_y + (GLint)height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)old_read_framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)old_draw_framebuffer);
    glDeleteFramebuffers(2, framebuffers);
    const GLenum error = glGetError();
    sg_reset_state_cache();
    return read_status == GL_FRAMEBUFFER_COMPLETE && draw_status == GL_FRAMEBUFFER_COMPLETE &&
           error == GL_NO_ERROR;
}

static uint32_t nk_sokol_buffer_to_image(sg_buffer source, uint32_t source_offset,
                                         uint32_t row_pitch, sg_image destination,
                                         uint32_t mip_level, uint32_t layer, uint32_t x,
                                         uint32_t y, uint32_t width, uint32_t height) {
    uint32_t source_buffer = 0;
    uint32_t texture = 0;
    GLenum target = 0;
    uint32_t image_width = 0;
    uint32_t image_height = 0;
    GLenum format = 0;
    GLenum type = 0;
    uint32_t bytes = 0;
    if (!buffer_transfer_info(source, &source_buffer) ||
        !image_transfer_info(destination, mip_level, layer, &texture, &target, &image_width,
                             &image_height, &format, &type, &bytes) ||
        !width || !height || x > image_width || y > image_height || width > image_width - x ||
        height > image_height - y || width > UINT32_MAX / bytes ||
        row_pitch < width * bytes || row_pitch % bytes != 0)
        return 0;
    clear_gl_errors();
    GLint old_unpack_buffer = 0;
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &old_unpack_buffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, source_buffer);
    glBindTexture(target, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, (GLint)(row_pitch / bytes));
    glTexSubImage2D(target, (GLint)mip_level, (GLint)x, (GLint)y, (GLsizei)width, (GLsizei)height,
                    format, type, (const void *)(uintptr_t)source_offset);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(target, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)old_unpack_buffer);
    const GLenum error = glGetError();
    sg_reset_state_cache();
    return error == GL_NO_ERROR;
}

static uint32_t nk_sokol_image_to_buffer(sg_image source, uint32_t mip_level, uint32_t layer,
                                         uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                                         sg_buffer destination, uint32_t destination_offset,
                                         uint32_t row_pitch) {
    uint32_t texture = 0;
    GLenum target = 0;
    uint32_t image_width = 0;
    uint32_t image_height = 0;
    GLenum format = 0;
    GLenum type = 0;
    uint32_t bytes = 0;
    uint32_t destination_buffer = 0;
    if (!image_transfer_info(source, mip_level, layer, &texture, &target, &image_width,
                             &image_height, &format, &type, &bytes) ||
        !buffer_transfer_info(destination, &destination_buffer) || !width || !height ||
        x > image_width || y > image_height || width > image_width - x ||
        height > image_height - y || width > UINT32_MAX / bytes ||
        row_pitch < width * bytes || row_pitch % bytes != 0)
        return 0;
    const size_t temporary_size = (size_t)width * height * bytes;
    uint8_t *temporary = (uint8_t *)malloc(temporary_size);
    if (!temporary)
        return 0;
    GLint old_read_framebuffer = 0;
    GLuint framebuffer = 0;
    clear_gl_errors();
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_framebuffer);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, texture,
                           (GLint)mip_level);
    const GLenum status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE) {
        const GLint gl_y = (GLint)image_height - (GLint)y - (GLint)height;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels((GLint)x, gl_y, (GLsizei)width, (GLsizei)height, format, type, temporary);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)old_read_framebuffer);
    glDeleteFramebuffers(1, &framebuffer);
    GLenum error = glGetError();
    if (status == GL_FRAMEBUFFER_COMPLETE && error == GL_NO_ERROR) {
        GLint old_copy_write_buffer = 0;
        glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &old_copy_write_buffer);
        glBindBuffer(GL_COPY_WRITE_BUFFER, destination_buffer);
        const size_t tight_row = (size_t)width * bytes;
        for (uint32_t row = 0; row < height; ++row) {
            const uint8_t *row_data = temporary + (size_t)(height - row - 1) * tight_row;
            glBufferSubData(GL_COPY_WRITE_BUFFER, (GLintptr)(destination_offset + row * row_pitch),
                            (GLsizeiptr)tight_row, row_data);
        }
        glBindBuffer(GL_COPY_WRITE_BUFFER, (GLuint)old_copy_write_buffer);
        error = glGetError();
    }
    free(temporary);
    sg_reset_state_cache();
    return status == GL_FRAMEBUFFER_COMPLETE && error == GL_NO_ERROR;
}

static uint32_t nk_sokol_readback_begin(sg_image source, uint32_t mip_level, uint32_t layer,
                                        uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
    uint32_t texture = 0;
    GLenum target = 0;
    uint32_t image_width = 0;
    uint32_t image_height = 0;
    GLenum format = 0;
    GLenum type = 0;
    uint32_t bytes = 0;
    if (!image_transfer_info(source, mip_level, layer, &texture, &target, &image_width,
                             &image_height, &format, &type, &bytes) || !width || !height ||
        x > image_width || y > image_height || width > image_width - x ||
        height > image_height - y)
        return 0;
    uint32_t index = NK_SOKOL_READBACK_CAPACITY;
    for (uint32_t i = 0; i < NK_SOKOL_READBACK_CAPACITY; ++i) {
        if (!readbacks[i].active) {
            index = i;
            break;
        }
    }
    if (index == NK_SOKOL_READBACK_CAPACITY)
        return 0;
    if (width > UINT32_MAX / bytes)
        return 0;
    const uint32_t row_pitch = width * bytes;
    if (!row_pitch || height > UINT32_MAX / row_pitch)
        return 0;
    const uint32_t size = row_pitch * height;
    nk_sokol_readback_slot *slot = &readbacks[index];
    if (!slot->generation)
        slot->generation = 1;
    clear_gl_errors();
    GLint old_read_framebuffer = 0;
    GLint old_pack_buffer = 0;
    GLuint framebuffer = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_framebuffer);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &old_pack_buffer);
    glGenBuffers(1, &slot->pbo);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)size, 0, GL_STREAM_READ);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, target, texture,
                           (GLint)mip_level);
    const GLenum status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    if (status == GL_FRAMEBUFFER_COMPLETE) {
        const GLint gl_y = (GLint)image_height - (GLint)y - (GLint)height;
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels((GLint)x, gl_y, (GLsizei)width, (GLsizei)height, format, type, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        slot->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)old_read_framebuffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)old_pack_buffer);
    glDeleteFramebuffers(1, &framebuffer);
    if (status != GL_FRAMEBUFFER_COMPLETE || !slot->fence || glGetError() != GL_NO_ERROR) {
        readback_release(slot);
        sg_reset_state_cache();
        return 0;
    }
    slot->size = (uint32_t)size;
    slot->row_pitch = row_pitch;
    slot->width = width;
    slot->height = height;
    slot->active = 1;
    sg_reset_state_cache();
    return readback_token(index, slot->generation);
}

static uint32_t nk_sokol_readback_status(uint32_t token) {
    nk_sokol_readback_slot *slot = readback_slot(token);
    if (!slot || !slot->fence)
        return 3;
    const GLenum status = glClientWaitSync(slot->fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
    if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED)
        return 2;
    if (status == GL_TIMEOUT_EXPIRED)
        return 1;
    return 3;
}

static uint32_t nk_sokol_readback_size(uint32_t token) {
    nk_sokol_readback_slot *slot = readback_slot(token);
    return slot ? slot->size : 0;
}

static uint32_t nk_sokol_readback_row_pitch(uint32_t token) {
    nk_sokol_readback_slot *slot = readback_slot(token);
    return slot ? slot->row_pitch : 0;
}

static int nk_sokol_readback_read(uint32_t token, void *destination, uint32_t size) {
    nk_sokol_readback_slot *slot = readback_slot(token);
    if (!slot || !destination || size < slot->size || nk_sokol_readback_status(token) != 2)
        return 0;
    GLint old_pack_buffer = 0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &old_pack_buffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
    const uint8_t *mapped = (const uint8_t *)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0,
                                                               (GLsizeiptr)slot->size,
                                                               GL_MAP_READ_BIT);
    if (!mapped) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)old_pack_buffer);
        return 0;
    }
    for (uint32_t row = 0; row < slot->height; ++row) {
        memcpy((uint8_t *)destination + (size_t)row * slot->row_pitch,
               mapped + (size_t)(slot->height - row - 1) * slot->row_pitch, slot->row_pitch);
    }
    const GLboolean unmapped = glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)old_pack_buffer);
    sg_reset_state_cache();
    return unmapped != 0 && glGetError() == GL_NO_ERROR;
}

static void nk_sokol_readback_destroy(uint32_t token) {
    nk_sokol_readback_slot *slot = readback_slot(token);
    if (slot)
        readback_release(slot);
}

static const nk_sokol_transfer_api transfer_api = {
    nk_sokol_buffer_copy,
    nk_sokol_image_copy,
    nk_sokol_buffer_to_image,
    nk_sokol_image_to_buffer,
    nk_sokol_readback_begin,
    nk_sokol_readback_status,
    nk_sokol_readback_size,
    nk_sokol_readback_row_pitch,
    nk_sokol_readback_read,
    nk_sokol_readback_destroy,
    0,
    0,
};
#elif defined(SOKOL_D3D11) || defined(SOKOL_METAL)
/* Native transfer callbacks live in the backend-specific translation unit. */
#else
static const nk_sokol_transfer_api transfer_api = {};
#endif

#if defined(NKGPU_TESTING)
static void nk_sokol_test_log(const char *tag, uint32_t level, uint32_t item_id,
                              const char *message, uint32_t line, const char *filename,
                              void *user_data) {
    (void)user_data;
    if (level > 3)
        return;
    fprintf(stderr, "%s: %s (item %u, %s:%u)\n", tag ? tag : "sokol", message ? message : "",
            item_id, filename ? filename : "unknown", line);
}
#endif

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

int nk_sokol_runtime_is_compatible(const sg_desc *desc, nk_graphics_device device,
                                   uint64_t native_device) {
    if (!desc || !device.id)
        return 0;
    if (!runtime_references)
        return 1;
    const uint64_t device_key = native_device ? native_device : device.id;
    return runtime_config_matches(desc) && runtime_device == device_key;
}

int nk_sokol_runtime_acquire(const sg_desc *desc, nk_graphics_device device,
                             uint64_t native_device) {
    if (!desc || !device.id || runtime_references == UINT32_MAX)
        return 0;
    if (!nk_sokol_runtime_is_compatible(desc, device, native_device))
        return 0;
    const uint64_t device_key = native_device ? native_device : device.id;
    if (!runtime_references) {
        if (sg_isvalid())
            return 0;
        runtime_color_format = desc->environment.defaults.color_format;
        runtime_depth_format = desc->environment.defaults.depth_format;
        runtime_sample_count = desc->environment.defaults.sample_count;
        runtime_device = device_key;
        sg_desc runtime_desc = *desc;
#if defined(NKGPU_TESTING)
        runtime_desc.logger.func = nk_sokol_test_log;
#endif
        sg_setup(&runtime_desc);
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
#if defined(_SOKOL_ANY_GL)
        for (uint32_t i = 0; i < NK_SOKOL_READBACK_CAPACITY; ++i)
            if (readbacks[i].active)
                readback_release(&readbacks[i]);
#elif defined(SOKOL_D3D11)
        nk_sokol_d3d11_transfer_shutdown();
#elif defined(SOKOL_METAL)
        nk_sokol_metal_transfer_shutdown();
#endif
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

const nk_sokol_transfer_api *nk_sokol_transfer_get_api(void) {
#if defined(SOKOL_D3D11)
    return nk_sokol_d3d11_transfer_get_api();
#elif defined(SOKOL_METAL)
    return nk_sokol_metal_transfer_get_api();
#else
    return &transfer_api;
#endif
}
