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

#ifndef GL_MAX_SAMPLES
#define GL_MAX_SAMPLES 0x8D57
#endif

#if defined(__EMSCRIPTEN__) && defined(_SOKOL_ANY_GL)
extern void glGetBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, void *data);
#endif

#if defined(_SOKOL_ANY_GL)
enum { NK_SOKOL_READBACK_CAPACITY = 128 };

typedef struct nk_sokol_readback_slot {
    uint32_t generation;
#if defined(__EMSCRIPTEN__)
    uint8_t *data;
#else
    uint32_t pbo;
    void *fence;
#endif
    uint32_t size;
    uint32_t row_pitch;
    uint32_t width;
    uint32_t height;
    int active;
} nk_sokol_readback_slot;

static nk_sokol_readback_slot readbacks[NK_SOKOL_READBACK_CAPACITY];
enum {
    NK_SOKOL_TIMESTAMP_PENDING = 1,
    NK_SOKOL_TIMESTAMP_READY = 2,
    NK_SOKOL_TIMESTAMP_FAILED = 3
};

#ifndef GL_COPY_READ_BUFFER
#define GL_COPY_READ_BUFFER 0x8F36
#endif
#ifndef GL_COPY_WRITE_BUFFER
#define GL_COPY_WRITE_BUFFER 0x8F37
#endif
#ifndef GL_COPY_READ_BUFFER_BINDING
#define GL_COPY_READ_BUFFER_BINDING 0x8F36
#endif
#ifndef GL_COPY_WRITE_BUFFER_BINDING
#define GL_COPY_WRITE_BUFFER_BINDING 0x8F37
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
#ifndef GL_UNSIGNED_INT_24_8
#define GL_UNSIGNED_INT_24_8 0x84FA
#endif
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif
#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS 0x8B81
#endif
#ifndef GL_LINK_STATUS
#define GL_LINK_STATUS 0x8B82
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_TEXTURE_MIN_FILTER
#define GL_TEXTURE_MIN_FILTER 0x2801
#endif
#ifndef GL_TEXTURE_MAG_FILTER
#define GL_TEXTURE_MAG_FILTER 0x2800
#endif
#ifndef GL_TEXTURE_WRAP_S
#define GL_TEXTURE_WRAP_S 0x2802
#endif
#ifndef GL_TEXTURE_WRAP_T
#define GL_TEXTURE_WRAP_T 0x2803
#endif
#ifndef GL_NEAREST
#define GL_NEAREST 0x2600
#endif
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_CURRENT_PROGRAM
#define GL_CURRENT_PROGRAM 0x8B8D
#endif
#ifndef GL_VERTEX_ARRAY_BINDING
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#endif
#ifndef GL_ACTIVE_TEXTURE
#define GL_ACTIVE_TEXTURE 0x84E0
#endif
#ifndef GL_TEXTURE_BINDING_2D
#define GL_TEXTURE_BINDING_2D 0x8069
#endif
#ifndef GL_SAMPLER_BINDING
#define GL_SAMPLER_BINDING 0x8919
#endif
#ifndef GL_VIEWPORT
#define GL_VIEWPORT 0x0BA2
#endif
#ifndef GL_TEXTURE0
#define GL_TEXTURE0 0x84C0
#endif
#ifndef GL_TRIANGLES
#define GL_TRIANGLES 0x0004
#endif
#ifndef GL_SCISSOR_TEST
#define GL_SCISSOR_TEST 0x0C11
#endif
#ifndef GL_DEPTH_TEST
#define GL_DEPTH_TEST 0x0B71
#endif
#ifndef GL_STENCIL_TEST
#define GL_STENCIL_TEST 0x0B90
#endif
#ifndef GL_BLEND
#define GL_BLEND 0x0BE2
#endif
#ifndef GL_CULL_FACE
#define GL_CULL_FACE 0x0B44
#endif
#ifndef GL_RASTERIZER_DISCARD
#define GL_RASTERIZER_DISCARD 0x8C89
#endif
#ifndef GL_DRAW_FRAMEBUFFER
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#endif

#if defined(__EMSCRIPTEN__)
static GLuint webgl_depth_readback_program;
static GLuint webgl_depth_readback_vao;
static GLuint webgl_depth_readback_sampler;
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
#if defined(__EMSCRIPTEN__)
    free(slot->data);
    slot->data = 0;
#else
    if (slot->fence)
        glDeleteSync(slot->fence);
    if (slot->pbo)
        glDeleteBuffers(1, &slot->pbo);
    slot->fence = 0;
    slot->pbo = 0;
#endif
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

#if defined(__EMSCRIPTEN__)
static int webgl_depth_readback_setup(void) {
    if (webgl_depth_readback_program && webgl_depth_readback_vao &&
        webgl_depth_readback_sampler)
        return 1;

    static const char *vertex_source =
        "#version 300 es\n"
        "out vec2 uv;\n"
        "void main() {\n"
        "    vec2 p[3] = vec2[3](vec2(-1.0, -1.0), vec2(3.0, -1.0),\n"
        "                         vec2(-1.0, 3.0));\n"
        "    gl_Position = vec4(p[gl_VertexID], 0.0, 1.0);\n"
        "    uv = p[gl_VertexID] * 0.5 + 0.5;\n"
        "}\n";
    static const char *fragment_source =
        "#version 300 es\n"
        "precision highp float;\n"
        "precision highp int;\n"
        "uniform sampler2D depth_texture;\n"
        "uniform vec2 uv_origin;\n"
        "uniform vec2 uv_scale;\n"
        "uniform float mip_level;\n"
        "in vec2 uv;\n"
        "layout(location = 0) out vec4 color;\n"
        "void main() {\n"
        "    float depth = textureLod(depth_texture, uv_origin + uv * uv_scale, mip_level).r;\n"
        "    uint bits = floatBitsToUint(depth);\n"
        "    color = vec4(float((bits >> 24u) & 255u) / 255.0,\n"
        "                 float((bits >> 16u) & 255u) / 255.0,\n"
        "                 float((bits >> 8u) & 255u) / 255.0,\n"
        "                 float(bits & 255u) / 255.0);\n"
        "}\n";
    GLuint vertex_shader = 0;
    GLuint fragment_shader = 0;
    GLuint program = 0;
    GLint status = 0;
    clear_gl_errors();
    vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    if (!vertex_shader || !fragment_shader)
        goto fail;
    glShaderSource(vertex_shader, 1, &vertex_source, 0);
    glCompileShader(vertex_shader);
    glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &status);
    if (!status)
        goto fail;
    glShaderSource(fragment_shader, 1, &fragment_source, 0);
    glCompileShader(fragment_shader);
    glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &status);
    if (!status)
        goto fail;
    program = glCreateProgram();
    if (!program)
        goto fail;
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status)
        goto fail;
    glGenVertexArrays(1, &webgl_depth_readback_vao);
    glGenSamplers(1, &webgl_depth_readback_sampler);
    if (!webgl_depth_readback_vao || !webgl_depth_readback_sampler)
        goto fail;
    glSamplerParameteri(webgl_depth_readback_sampler, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glSamplerParameteri(webgl_depth_readback_sampler, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glSamplerParameteri(webgl_depth_readback_sampler, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(webgl_depth_readback_sampler, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    webgl_depth_readback_program = program;
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    if (glGetError() == GL_NO_ERROR)
        return 1;
    glDeleteProgram(webgl_depth_readback_program);
    glDeleteVertexArrays(1, &webgl_depth_readback_vao);
    glDeleteSamplers(1, &webgl_depth_readback_sampler);
    webgl_depth_readback_program = 0;
    webgl_depth_readback_vao = 0;
    webgl_depth_readback_sampler = 0;
    clear_gl_errors();
    return 0;

fail:
    if (vertex_shader)
        glDeleteShader(vertex_shader);
    if (fragment_shader)
        glDeleteShader(fragment_shader);
    if (program)
        glDeleteProgram(program);
    if (webgl_depth_readback_vao)
        glDeleteVertexArrays(1, &webgl_depth_readback_vao);
    if (webgl_depth_readback_sampler)
        glDeleteSamplers(1, &webgl_depth_readback_sampler);
    webgl_depth_readback_vao = 0;
    webgl_depth_readback_sampler = 0;
    webgl_depth_readback_program = 0;
    clear_gl_errors();
    return 0;
}

static int webgl_depth_readback(uint32_t source_mip, uint32_t source_texture, GLenum source_target,
                                uint32_t source_width,
                                uint32_t source_height, uint32_t source_x, uint32_t source_y,
                                uint32_t width, uint32_t height, uint8_t *output) {
    if (!output || source_target != GL_TEXTURE_2D || !source_width || !source_height ||
        width > 0x7FFFFFFFu || height > 0x7FFFFFFFu ||
        !webgl_depth_readback_setup())
        return 0;

    const size_t row_size = (size_t)width * 4u;
    const size_t packed_size = row_size * height;
    if (!row_size || row_size / 4u != width || packed_size / row_size != height)
        return 0;
    uint8_t *packed = (uint8_t *)malloc(packed_size);
    if (!packed)
        return 0;

    GLint old_read_framebuffer = 0;
    GLint old_draw_framebuffer = 0;
    GLint old_program = 0;
    GLint old_vao = 0;
    GLint old_active_texture = 0;
    GLint old_texture = 0;
    GLint old_sampler = 0;
    GLint old_viewport[4] = {0, 0, 0, 0};
    GLuint framebuffer = 0;
    GLuint color_texture = 0;
    int success = 0;
    clear_gl_errors();
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_framebuffer);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_framebuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &old_program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &old_vao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &old_active_texture);
    glGetIntegerv(GL_VIEWPORT, old_viewport);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &old_texture);
    glGetIntegerv(GL_SAMPLER_BINDING, &old_sampler);
    glGenTextures(1, &color_texture);
    glBindTexture(GL_TEXTURE_2D, color_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, (GLsizei)width, (GLsizei)height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, 0);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           color_texture, 0);
    const GLenum draw_buffer = GL_COLOR_ATTACHMENT0;
    glDrawBuffers(1, &draw_buffer);
    if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        goto cleanup;

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDisable(GL_RASTERIZER_DISCARD);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(webgl_depth_readback_program);
    glBindVertexArray(webgl_depth_readback_vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(source_target, source_texture);
    glBindSampler(0, webgl_depth_readback_sampler);
    const GLint depth_texture =
        glGetUniformLocation(webgl_depth_readback_program, "depth_texture");
    const GLint uv_origin = glGetUniformLocation(webgl_depth_readback_program, "uv_origin");
    const GLint uv_scale = glGetUniformLocation(webgl_depth_readback_program, "uv_scale");
    const GLint mip_level = glGetUniformLocation(webgl_depth_readback_program, "mip_level");
    const GLfloat origin[2] = {(GLfloat)source_x / (GLfloat)source_width,
                               (GLfloat)(source_height - source_y) / (GLfloat)source_height};
    const GLfloat scale[2] = {(GLfloat)width / (GLfloat)source_width,
                              -(GLfloat)height / (GLfloat)source_height};
    const GLfloat level = (GLfloat)source_mip;
    glUniform1i(depth_texture, 0);
    glUniform2fv(uv_origin, 1, origin);
    glUniform2fv(uv_scale, 1, scale);
    glUniform1fv(mip_level, 1, &level);
    glViewport(0, 0, (GLsizei)width, (GLsizei)height);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, (GLsizei)width, (GLsizei)height, GL_RGBA, GL_UNSIGNED_BYTE, packed);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    if (glGetError() != GL_NO_ERROR)
        goto cleanup;
    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t *source_row = packed + (size_t)row * row_size;
        uint8_t *destination_row = output + (size_t)(height - row - 1u) * row_size;
        for (uint32_t column = 0; column < width; ++column) {
            const uint8_t *pixel = source_row + (size_t)column * 4u;
            const uint32_t bits = ((uint32_t)pixel[0] << 24) | ((uint32_t)pixel[1] << 16) |
                                  ((uint32_t)pixel[2] << 8) | (uint32_t)pixel[3];
            memcpy(destination_row + (size_t)column * 4u, &bits, sizeof(bits));
        }
    }
    success = 1;

cleanup:
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)old_read_framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)old_draw_framebuffer);
    glUseProgram((GLuint)old_program);
    glBindVertexArray((GLuint)old_vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, (GLuint)old_texture);
    glBindSampler(0, (GLuint)old_sampler);
    glActiveTexture((GLenum)old_active_texture);
    glViewport(old_viewport[0], old_viewport[1], old_viewport[2], old_viewport[3]);
    if (framebuffer)
        glDeleteFramebuffers(1, &framebuffer);
    if (color_texture)
        glDeleteTextures(1, &color_texture);
    const GLenum error = glGetError();
    free(packed);
    return success && error == GL_NO_ERROR;
}
#endif

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
    case SG_PIXELFORMAT_DEPTH:
        *out_format = GL_DEPTH_COMPONENT;
        *out_type = GL_FLOAT;
        *out_bytes = 4;
        return 1;
    case SG_PIXELFORMAT_DEPTH_STENCIL:
        *out_format = GL_DEPTH_STENCIL;
        *out_type = GL_UNSIGNED_INT_24_8;
        *out_bytes = 4;
        return 1;
    default:
        return 0;
    }
}

static GLenum image_attachment(sg_pixel_format format) {
    return format == SG_PIXELFORMAT_DEPTH
               ? GL_DEPTH_ATTACHMENT
               : (format == SG_PIXELFORMAT_DEPTH_STENCIL ? GL_DEPTH_STENCIL_ATTACHMENT
                                                         : GL_COLOR_ATTACHMENT0);
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
        !image_transfer_info(image, mip_level, layer, &texture, &target, &mip_width, &mip_height,
                             &format, &type, &bytes) ||
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
                                    uint32_t destination_x, uint32_t destination_y, uint32_t width,
                                    uint32_t height) {
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

    const size_t row_size = (size_t)width * source_bytes;
    const size_t temporary_size = row_size * height;
    if (!row_size || row_size / source_bytes != width || temporary_size / row_size != height)
        return 0;
    const sg_image_usage source_usage = sg_query_image_usage(source);
    const sg_image_usage destination_usage = sg_query_image_usage(destination);
    const int source_is_depth = source_usage.depth_stencil_attachment;
    const int destination_is_depth = destination_usage.depth_stencil_attachment;
    if (source_is_depth || destination_is_depth) {
        if (!source_is_depth || !destination_is_depth)
            return 0;

        GLint old_read_framebuffer = 0;
        GLint old_draw_framebuffer = 0;
        GLuint read_framebuffer = 0;
        GLuint draw_framebuffer = 0;
        clear_gl_errors();
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_framebuffer);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &old_draw_framebuffer);
        glGenFramebuffers(1, &read_framebuffer);
        glGenFramebuffers(1, &draw_framebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read_framebuffer);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER,
                               image_attachment(sg_query_image_pixelformat(source)),
                               source_target, source_texture, (GLint)source_mip);
        const GLenum read_status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_framebuffer);
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,
                               image_attachment(sg_query_image_pixelformat(destination)),
                               destination_target, destination_texture,
                               (GLint)destination_mip);
        const GLenum draw_status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
        GLenum error = glGetError();
        if (read_status == GL_FRAMEBUFFER_COMPLETE && draw_status == GL_FRAMEBUFFER_COMPLETE &&
            error == GL_NO_ERROR) {
            const GLint source_y0 = (GLint)source_height - (GLint)source_y - (GLint)height;
            const GLint source_y1 = source_y0 + (GLint)height;
            const GLint destination_y0 =
                (GLint)destination_height - (GLint)destination_y - (GLint)height;
            const GLint destination_y1 = destination_y0 + (GLint)height;
            const GLbitfield mask =
                sg_query_image_pixelformat(source) == SG_PIXELFORMAT_DEPTH_STENCIL
                    ? GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT
                    : GL_DEPTH_BUFFER_BIT;
            glBlitFramebuffer((GLint)source_x, source_y0, (GLint)(source_x + width), source_y1,
                              (GLint)destination_x, destination_y0,
                              (GLint)(destination_x + width), destination_y1, mask, GL_NEAREST);
            error = glGetError();
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)old_read_framebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)old_draw_framebuffer);
        glDeleteFramebuffers(1, &read_framebuffer);
        glDeleteFramebuffers(1, &draw_framebuffer);
        sg_reset_state_cache();
        return read_status == GL_FRAMEBUFFER_COMPLETE && draw_status == GL_FRAMEBUFFER_COMPLETE &&
               error == GL_NO_ERROR;
    }
    const int source_is_attachment =
        source_usage.color_attachment;
    uint8_t *temporary = (uint8_t *)malloc(temporary_size);
    if (!temporary)
        return 0;

    GLint old_read_framebuffer = 0;
    GLint old_pack_buffer = 0;
    GLint old_unpack_buffer = 0;
    GLuint framebuffer = 0;
    clear_gl_errors();
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_framebuffer);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &old_pack_buffer);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &old_unpack_buffer);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,
                           image_attachment(sg_query_image_pixelformat(source)), source_target,
                           source_texture, (GLint)source_mip);
    const GLenum read_status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
    if (read_status == GL_FRAMEBUFFER_COMPLETE) {
        const GLint source_gl_y = (GLint)source_height - (GLint)source_y - (GLint)height;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels((GLint)source_x, source_gl_y, (GLsizei)width, (GLsizei)height, source_format,
                     source_type, temporary);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        if (!source_is_attachment) {
            for (uint32_t row = 0; row < height / 2; ++row) {
                uint8_t *top = temporary + (size_t)row * row_size;
                uint8_t *bottom = temporary + (size_t)(height - row - 1) * row_size;
                for (size_t byte = 0; byte < row_size; ++byte) {
                    const uint8_t value = top[byte];
                    top[byte] = bottom[byte];
                    bottom[byte] = value;
                }
            }
        }
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glBindTexture(destination_target, destination_texture);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        const GLint destination_gl_y =
            source_is_attachment ? (GLint)destination_height - (GLint)destination_y - (GLint)height
                                 : (GLint)destination_y;
        glTexSubImage2D(destination_target, (GLint)destination_mip, (GLint)destination_x,
                        destination_gl_y, (GLsizei)width, (GLsizei)height, destination_format,
                        destination_type, temporary);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glBindTexture(destination_target, 0);
    }
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)old_read_framebuffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, (GLuint)old_pack_buffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)old_unpack_buffer);
    glDeleteFramebuffers(1, &framebuffer);
    const GLenum error = glGetError();
    free(temporary);
    sg_reset_state_cache();
    return read_status == GL_FRAMEBUFFER_COMPLETE && error == GL_NO_ERROR;
}

static uint32_t nk_sokol_buffer_to_image(sg_buffer source, uint32_t source_offset,
                                         uint32_t row_pitch, sg_image destination,
                                         uint32_t mip_level, uint32_t layer, uint32_t x, uint32_t y,
                                         uint32_t width, uint32_t height) {
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
        height > image_height - y || width > UINT32_MAX / bytes || row_pitch < width * bytes ||
        row_pitch % bytes != 0)
        return 0;
    const size_t tight_row = (size_t)width * bytes;
    const size_t source_size = (size_t)row_pitch * height;
    const size_t temporary_size = tight_row * height;
    if (source_size / row_pitch != height || temporary_size / tight_row != height)
        return 0;
    uint8_t *temporary = (uint8_t *)malloc(temporary_size);
    if (!temporary)
        return 0;
    clear_gl_errors();
    GLint old_unpack_buffer = 0;
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &old_unpack_buffer);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, source_buffer);
#if defined(__EMSCRIPTEN__)
    uint8_t *buffer_data = (uint8_t *)malloc(source_size);
    if (!buffer_data) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)old_unpack_buffer);
        free(temporary);
        sg_reset_state_cache();
        return 0;
    }
    glGetBufferSubData(GL_PIXEL_UNPACK_BUFFER, (GLintptr)source_offset, (GLsizeiptr)source_size,
                       buffer_data);
    const uint8_t *mapped = buffer_data;
#else
    const uint8_t *mapped = (const uint8_t *)glMapBufferRange(
        GL_PIXEL_UNPACK_BUFFER, (GLintptr)source_offset, (GLsizeiptr)source_size, GL_MAP_READ_BIT);
    if (!mapped) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)old_unpack_buffer);
        free(temporary);
        sg_reset_state_cache();
        return 0;
    }
#endif
    for (uint32_t row = 0; row < height; ++row) {
        memcpy(temporary + (size_t)row * tight_row, mapped + (size_t)(height - row - 1) * row_pitch,
               tight_row);
    }
#if defined(__EMSCRIPTEN__)
    free(buffer_data);
#else
    const GLboolean unmapped = glUnmapBuffer(GL_PIXEL_UNPACK_BUFFER);
    if (!unmapped || glGetError() != GL_NO_ERROR) {
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)old_unpack_buffer);
        free(temporary);
        sg_reset_state_cache();
        return 0;
    }
#endif
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    glBindTexture(target, texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexSubImage2D(target, (GLint)mip_level, (GLint)x, (GLint)y, (GLsizei)width, (GLsizei)height,
                    format, type, temporary);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glBindTexture(target, 0);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, (GLuint)old_unpack_buffer);
    const GLenum error = glGetError();
    free(temporary);
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
        height > image_height - y || width > UINT32_MAX / bytes || row_pitch < width * bytes ||
        row_pitch % bytes != 0)
        return 0;
#if defined(__EMSCRIPTEN__)
    if (sg_query_image_pixelformat(source) == SG_PIXELFORMAT_DEPTH) {
        const size_t temporary_size = (size_t)width * height * 4u;
        if (!temporary_size || temporary_size / 4u / height != width)
            return 0;
        uint8_t *temporary = (uint8_t *)malloc(temporary_size);
        if (!temporary)
            return 0;
        int converted = webgl_depth_readback(
            mip_level, texture, target, image_width, image_height, x, y, width, height, temporary);
        if (converted) {
            GLint old_copy_write_buffer = 0;
            clear_gl_errors();
            glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &old_copy_write_buffer);
            glBindBuffer(GL_COPY_WRITE_BUFFER, destination_buffer);
            const size_t tight_row = (size_t)width * 4u;
            for (uint32_t row = 0; row < height; ++row) {
                glBufferSubData(GL_COPY_WRITE_BUFFER,
                                (GLintptr)(destination_offset + row * row_pitch),
                                (GLsizeiptr)tight_row, temporary + (size_t)row * tight_row);
            }
            glBindBuffer(GL_COPY_WRITE_BUFFER, (GLuint)old_copy_write_buffer);
            converted = glGetError() == GL_NO_ERROR;
        }
        free(temporary);
        sg_reset_state_cache();
        return converted;
    }
#endif
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
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,
                           image_attachment(sg_query_image_pixelformat(source)), target, texture,
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
                             &image_height, &format, &type, &bytes) ||
        !width || !height || x > image_width || y > image_height || width > image_width - x ||
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
#if defined(__EMSCRIPTEN__)
    slot->data = (uint8_t *)malloc(size);
    if (!slot->data)
        return 0;
    if (sg_query_image_pixelformat(source) == SG_PIXELFORMAT_DEPTH) {
        if (!webgl_depth_readback(mip_level, texture, target, image_width, image_height, x, y,
                                  width, height, slot->data)) {
            readback_release(slot);
            sg_reset_state_cache();
            return 0;
        }
    } else {
        clear_gl_errors();
        GLint old_read_framebuffer = 0;
        GLuint framebuffer = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &old_read_framebuffer);
        glGenFramebuffers(1, &framebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_READ_FRAMEBUFFER,
                               image_attachment(sg_query_image_pixelformat(source)), target,
                               texture, (GLint)mip_level);
        const GLenum status = glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
        if (status == GL_FRAMEBUFFER_COMPLETE) {
            const GLint gl_y = (GLint)image_height - (GLint)y - (GLint)height;
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels((GLint)x, gl_y, (GLsizei)width, (GLsizei)height, format, type,
                         slot->data);
            glPixelStorei(GL_PACK_ALIGNMENT, 4);
            for (uint32_t row = 0; row < height / 2; ++row) {
                uint8_t *top = slot->data + (size_t)row * row_pitch;
                uint8_t *bottom = slot->data + (size_t)(height - row - 1) * row_pitch;
                for (uint32_t byte = 0; byte < row_pitch; ++byte) {
                    const uint8_t value = top[byte];
                    top[byte] = bottom[byte];
                    bottom[byte] = value;
                }
            }
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)old_read_framebuffer);
        glDeleteFramebuffers(1, &framebuffer);
        if (status != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR) {
            readback_release(slot);
            sg_reset_state_cache();
            return 0;
        }
    }
#else
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
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,
                           image_attachment(sg_query_image_pixelformat(source)), target, texture,
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
#endif
    slot->size = (uint32_t)size;
    slot->row_pitch = row_pitch;
    slot->width = width;
    slot->height = height;
    slot->active = 1;
    sg_reset_state_cache();
    return readback_token(index, slot->generation);
}

static uint32_t nk_sokol_readback_begin_buffer(sg_buffer source, uint32_t offset, uint32_t size) {
    uint32_t source_buffer = 0;
    if (!size || !buffer_transfer_info(source, &source_buffer) ||
        offset > sg_query_buffer_size(source) || size > sg_query_buffer_size(source) - offset)
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
    nk_sokol_readback_slot *slot = &readbacks[index];
    if (!slot->generation)
        slot->generation = 1;
#if defined(__EMSCRIPTEN__)
    slot->data = (uint8_t *)malloc(size);
    if (!slot->data)
        return 0;
    clear_gl_errors();
    GLint old_read_buffer = 0;
    glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &old_read_buffer);
    glBindBuffer(GL_COPY_READ_BUFFER, source_buffer);
    glGetBufferSubData(GL_COPY_READ_BUFFER, (GLintptr)offset, (GLsizeiptr)size, slot->data);
    glBindBuffer(GL_COPY_READ_BUFFER, (GLuint)old_read_buffer);
    if (glGetError() != GL_NO_ERROR) {
        readback_release(slot);
        sg_reset_state_cache();
        return 0;
    }
#else
    clear_gl_errors();
    GLint old_read_buffer = 0;
    GLint old_write_buffer = 0;
    glGetIntegerv(GL_COPY_READ_BUFFER_BINDING, &old_read_buffer);
    glGetIntegerv(GL_COPY_WRITE_BUFFER_BINDING, &old_write_buffer);
    glGenBuffers(1, &slot->pbo);
    glBindBuffer(GL_COPY_WRITE_BUFFER, slot->pbo);
    glBufferData(GL_COPY_WRITE_BUFFER, (GLsizeiptr)size, 0, GL_STREAM_READ);
    glBindBuffer(GL_COPY_READ_BUFFER, source_buffer);
    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER, (GLintptr)offset, 0,
                        (GLsizeiptr)size);
    slot->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    glFlush();
    glBindBuffer(GL_COPY_READ_BUFFER, (GLuint)old_read_buffer);
    glBindBuffer(GL_COPY_WRITE_BUFFER, (GLuint)old_write_buffer);
    if (!slot->fence || glGetError() != GL_NO_ERROR) {
        readback_release(slot);
        sg_reset_state_cache();
        return 0;
    }
#endif
    slot->size = size;
    slot->row_pitch = size;
    slot->width = size;
    slot->height = 1;
    slot->active = 1;
    sg_reset_state_cache();
    return readback_token(index, slot->generation);
}

static uint32_t nk_sokol_readback_status(uint32_t token) {
    nk_sokol_readback_slot *slot = readback_slot(token);
#if defined(__EMSCRIPTEN__)
    return slot && slot->data ? 2 : 3;
#else
    if (!slot || !slot->fence)
        return 3;
    const GLenum status = glClientWaitSync(slot->fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
    if (status == GL_ALREADY_SIGNALED || status == GL_CONDITION_SATISFIED)
        return 2;
    if (status == GL_TIMEOUT_EXPIRED)
        return 1;
    return 3;
#endif
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
#if defined(__EMSCRIPTEN__)
    memcpy(destination, slot->data, slot->size);
    return 1;
#else
    GLint old_pack_buffer = 0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &old_pack_buffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
    const uint8_t *mapped = (const uint8_t *)glMapBufferRange(
        GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)slot->size, GL_MAP_READ_BIT);
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
#endif
}

static void nk_sokol_readback_destroy(uint32_t token) {
    nk_sokol_readback_slot *slot = readback_slot(token);
    if (slot)
        readback_release(slot);
}

static uint32_t nk_sokol_timestamp_begin(void) {
#if !defined(NK_SOKOL_BACKEND_GLCORE)
    return 0;
#else
    GLuint query = 0;
    clear_gl_errors();
    glGenQueries(1, &query);
    if (!query)
        return 0;
    glBeginQuery(GL_TIME_ELAPSED, query);
    if (glGetError() != GL_NO_ERROR) {
        glDeleteQueries(1, &query);
        return 0;
    }
    return query;
#endif
}

static int nk_sokol_timestamp_end(uint32_t timestamp) {
#if !defined(NK_SOKOL_BACKEND_GLCORE)
    (void)timestamp;
    return 0;
#else
    if (!timestamp)
        return 0;
    clear_gl_errors();
    glEndQuery(GL_TIME_ELAPSED);
    glFlush();
    return glGetError() == GL_NO_ERROR;
#endif
}

static uint32_t nk_sokol_timestamp_status(uint32_t timestamp) {
#if !defined(NK_SOKOL_BACKEND_GLCORE)
    (void)timestamp;
    return NK_SOKOL_TIMESTAMP_FAILED;
#else
    if (!timestamp)
        return NK_SOKOL_TIMESTAMP_FAILED;
    GLuint available = 0;
    clear_gl_errors();
    glGetQueryObjectuiv(timestamp, GL_QUERY_RESULT_AVAILABLE, &available);
    if (glGetError() != GL_NO_ERROR)
        return NK_SOKOL_TIMESTAMP_FAILED;
    return available ? NK_SOKOL_TIMESTAMP_READY : NK_SOKOL_TIMESTAMP_PENDING;
#endif
}

static uint64_t nk_sokol_timestamp_elapsed_ns(uint32_t timestamp) {
#if !defined(NK_SOKOL_BACKEND_GLCORE)
    (void)timestamp;
    return 0;
#else
    if (!timestamp)
        return 0;
    GLuint64 elapsed = 0;
    glGetQueryObjectui64v(timestamp, GL_QUERY_RESULT, &elapsed);
    return (uint64_t)elapsed;
#endif
}

static void nk_sokol_timestamp_destroy(uint32_t timestamp) {
#if !defined(NK_SOKOL_BACKEND_GLCORE)
    (void)timestamp;
#else
    if (timestamp) {
        GLuint query = timestamp;
        glDeleteQueries(1, &query);
    }
#endif
}

static int nk_sokol_timestamp_supported(void) {
#if defined(NK_SOKOL_BACKEND_GLCORE)
    return 1;
#else
    return 0;
#endif
}

static const nk_sokol_transfer_api transfer_api = {
    nk_sokol_buffer_copy,
    nk_sokol_image_copy,
    nk_sokol_buffer_to_image,
    nk_sokol_image_to_buffer,
    nk_sokol_readback_begin,
    nk_sokol_readback_begin_buffer,
    nk_sokol_readback_status,
    nk_sokol_readback_size,
    nk_sokol_readback_row_pitch,
    nk_sokol_readback_read,
    nk_sokol_readback_destroy,
    0,
    0,
#if defined(NK_SOKOL_BACKEND_GLCORE)
    nk_sokol_timestamp_begin,
    nk_sokol_timestamp_end,
    nk_sokol_timestamp_status,
    nk_sokol_timestamp_elapsed_ns,
    nk_sokol_timestamp_destroy,
    nk_sokol_timestamp_supported,
#else
    0,
    0,
    0,
    0,
    0,
    0,
#endif
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
#if defined(__EMSCRIPTEN__)
        if (webgl_depth_readback_program)
            glDeleteProgram(webgl_depth_readback_program);
        if (webgl_depth_readback_vao)
            glDeleteVertexArrays(1, &webgl_depth_readback_vao);
        if (webgl_depth_readback_sampler)
            glDeleteSamplers(1, &webgl_depth_readback_sampler);
        webgl_depth_readback_program = 0;
        webgl_depth_readback_vao = 0;
        webgl_depth_readback_sampler = 0;
#endif
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

int nk_sokol_query_max_samples(void) {
#if defined(SOKOL_D3D11)
    return nk_sokol_d3d11_query_max_samples();
#elif defined(SOKOL_METAL)
    return nk_sokol_metal_query_max_samples();
#elif defined(_SOKOL_ANY_GL)
    GLint samples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &samples);
    return samples > 0 ? samples : 1;
#else
    return 1;
#endif
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
