#include "sokol_backend.h"

#include "frame_resources.h"
#include "graphics_device.h"

#define SOKOL_GLCORE
#include "sokol_gfx.h"

#include "nkui_composite.glsl.h"
#include "nkui_path.glsl.h"
#include "nkui_solid.glsl.h"
#include "nkui_text.glsl.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

namespace nkui {

struct SokolBackend::State {
    struct AtlasImage {
        sg_image image{};
        sg_view view{};
        int width = 0;
        int height = 0;
        AtlasTextureFormat format = AtlasTextureFormat::R8Mask;
        uint8_t bytes_per_pixel = 0;
        uint32_t generation = 0;
        std::vector<uint8_t> pixels;
    };

    struct Target {
        sg_image color{};
        sg_image depth{};
        sg_view texture{};
        sg_view color_attachment{};
        sg_view depth_attachment{};
        int width = 0;
        int height = 0;
    };

    struct SurfaceState {
        uint32_t generation = 0;
        int width = 0;
        int height = 0;
        SurfacePixelFormat format = SurfacePixelFormat::Rgba8;
        SurfaceAlphaMode alpha = SurfaceAlphaMode::Premultiplied;
        SurfaceFilter filter = SurfaceFilter::Nearest;
        SurfaceColorSpace color_space = SurfaceColorSpace::Linear;
    };

    struct PaintImage {
        sg_image image{};
        sg_view view{};
        sg_sampler sampler{};
        uint32_t generation = 0;
        int type = 0;
        int flags = 0;
    };

    sg_shader solid_shader{};
    sg_pipeline solid_pipeline{};
    sg_pipeline fill_stencil_pipeline{};
    sg_pipeline fill_stencil_even_odd_pipeline{};
    sg_pipeline fill_cover_pipeline{};
    sg_shader paint_shader{};
    sg_pipeline paint_pipeline{};
    sg_pipeline paint_cover_pipeline{};
    sg_pipeline paint_fringe_pipeline{};
    sg_shader alpha_glyph_shader{};
    sg_pipeline alpha_glyph_pipeline{};
    sg_shader sdf_glyph_shader{};
    sg_pipeline sdf_glyph_pipeline{};
    sg_shader color_glyph_shader{};
    sg_pipeline color_glyph_pipeline{};
    sg_shader composite_shader{};
    sg_pipeline composite_pipeline{};
    sg_sampler sampler{};
    sg_sampler surface_sampler{};
    sg_buffer solid_vertices{};
    sg_buffer glyph_vertices{};
    sg_buffer composite_vertices{};
    sg_buffer indices{};
    std::unordered_map<uint64_t, AtlasImage> atlases;
    std::unordered_map<uint32_t, Target> targets;
    std::unordered_map<uint32_t, SurfaceState> surfaces;
    std::unordered_map<const PreparedPathData *,
                       std::unordered_map<PreparedImageToken, PaintImage>> paint_images;
    std::unordered_map<uint32_t, PaintImage> images;
    sg_image white_image{};
    sg_view white_view{};
    sg_sampler white_sampler{};
    SokolBackendStats stats{};
    std::string error;
    std::shared_ptr<GraphicsDevice> device;
    int width = 0;
    int height = 0;
    bool initialized = false;
    bool in_pass = false;
};

namespace {

struct TextureVertex {
    float x;
    float y;
    float u;
    float v;
};

enum class PathShaderMode : uint8_t {
    Solid = 0,
    Image = 2,
};

struct PathUniforms {
    std::array<float, 4> inner_color;
    std::array<float, 4> outer_color;
    std::array<float, 4> extent_radius_feather;
    std::array<float, 4> inverse_x;
    std::array<float, 4> inverse_y;
    std::array<float, 4> mode;
    std::array<float, 4> coverage;
};

static_assert(sizeof(PathUniforms) == sizeof(float) * 4 * 7);

struct PathVertex {
    float x;
    float y;
    float u;
    float v;
};

struct PathMesh {
    std::vector<PathVertex> vertices;
    std::vector<uint32_t> indices;
};

bool fail(SokolBackend::State &state, const char *message) {
    state.error = message;
    return false;
}

void transform_identity(float *transform) {
    transform[0] = 1.0f;
    transform[1] = 0.0f;
    transform[2] = 0.0f;
    transform[3] = 1.0f;
    transform[4] = 0.0f;
    transform[5] = 0.0f;
}

void transform_multiply(float *transform, const float *other) {
    const float a = transform[0] * other[0] + transform[1] * other[2];
    const float c = transform[2] * other[0] + transform[3] * other[2];
    const float tx = transform[4] * other[0] + transform[5] * other[2] + other[4];
    const float b = transform[0] * other[1] + transform[1] * other[3];
    const float d = transform[2] * other[1] + transform[3] * other[3];
    const float ty = transform[4] * other[1] + transform[5] * other[3] + other[5];
    transform[0] = a;
    transform[1] = b;
    transform[2] = c;
    transform[3] = d;
    transform[4] = tx;
    transform[5] = ty;
}

void transform_inverse(float *inverse, const float *transform) {
    const double determinant = static_cast<double>(transform[0]) * transform[3] -
                               static_cast<double>(transform[2]) * transform[1];
    if (determinant > -1e-6 && determinant < 1e-6) {
        transform_identity(inverse);
        return;
    }
    const double reciprocal = 1.0 / determinant;
    inverse[0] = static_cast<float>(transform[3] * reciprocal);
    inverse[2] = static_cast<float>(-transform[2] * reciprocal);
    inverse[4] = static_cast<float>((static_cast<double>(transform[2]) * transform[5] -
                                     static_cast<double>(transform[3]) * transform[4]) *
                                    reciprocal);
    inverse[1] = static_cast<float>(-transform[1] * reciprocal);
    inverse[3] = static_cast<float>(transform[0] * reciprocal);
    inverse[5] = static_cast<float>((static_cast<double>(transform[1]) * transform[4] -
                                     static_cast<double>(transform[0]) * transform[5]) *
                                    reciprocal);
}

uint64_t atlas_key(AtlasTextureId texture, uint32_t generation) {
    return (static_cast<uint64_t>(texture.value) << 32) | generation;
}

bool create_atlas_image(SokolBackend::State::AtlasImage &atlas, const AtlasUpload &upload) {
    sg_image_desc desc{};
    desc.width = upload.texture_width;
    desc.height = upload.texture_height;
    desc.pixel_format = upload.format == AtlasTextureFormat::Rgba8Premultiplied
                            ? SG_PIXELFORMAT_RGBA8
                            : SG_PIXELFORMAT_R8;
    desc.usage.dynamic_update = true;
    atlas.image = sg_make_image(&desc);
    sg_view_desc view_desc{};
    view_desc.texture.image = atlas.image;
    atlas.view = sg_make_view(&view_desc);
    if (sg_query_image_state(atlas.image) != SG_RESOURCESTATE_VALID ||
        sg_query_view_state(atlas.view) != SG_RESOURCESTATE_VALID)
        return false;
    atlas.width = upload.texture_width;
    atlas.height = upload.texture_height;
    atlas.format = upload.format;
    atlas.bytes_per_pixel = upload.bytes_per_pixel;
    atlas.generation = upload.generation;
    atlas.pixels.assign(static_cast<size_t>(upload.texture_width) * upload.texture_height *
                            upload.bytes_per_pixel,
                        0);
    return true;
}

void copy_atlas_pixels(SokolBackend::State::AtlasImage &target, const AtlasUpload &upload,
                       bool full) {
    const int32_t x = full ? 0 : upload.x;
    const int32_t y = full ? 0 : upload.y;
    const int32_t width = full ? upload.texture_width : upload.width;
    const int32_t height = full ? upload.texture_height : upload.height;
    for (int32_t row = 0; row < height; ++row) {
        const auto *source = upload.pixels + static_cast<size_t>(y + row) * upload.row_pitch +
                             static_cast<size_t>(x) * upload.bytes_per_pixel;
        auto *destination = target.pixels.data() +
                            (static_cast<size_t>(y + row) * target.width + x) *
                                target.bytes_per_pixel;
        std::memcpy(destination, source, static_cast<size_t>(width) * upload.bytes_per_pixel);
    }
}

sg_shader make_solid_shader() {
    const sg_shader_desc *desc = nkui_solid_solid_shader_desc(sg_query_backend());
    return desc ? sg_make_shader(desc) : sg_shader{};
}

sg_shader make_path_shader() {
    const sg_shader_desc *desc = nkui_path_path_shader_desc(sg_query_backend());
    return desc ? sg_make_shader(desc) : sg_shader{};
}

sg_shader make_glyph_shader(GlyphMode mode) {
    const sg_shader_desc *desc = nullptr;
    switch (mode) {
    case GlyphMode::Alpha:
        desc = nkui_text_alpha_shader_desc(sg_query_backend());
        break;
    case GlyphMode::Sdf:
        desc = nkui_text_sdf_shader_desc(sg_query_backend());
        break;
    case GlyphMode::Color:
        desc = nkui_text_color_shader_desc(sg_query_backend());
        break;
    }
    return desc ? sg_make_shader(desc) : sg_shader{};
}

sg_shader make_composite_shader() {
    const sg_shader_desc *desc = nkui_composite_composite_shader_desc(sg_query_backend());
    return desc ? sg_make_shader(desc) : sg_shader{};
}

sg_pipeline make_solid_pipeline(sg_shader shader) {
    sg_pipeline_desc desc{};
    desc.shader = shader;
    desc.layout.buffers[0].stride = sizeof(SolidVertex);
    desc.layout.attrs[0] = {0, 0, SG_VERTEXFORMAT_FLOAT2};
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    return sg_make_pipeline(&desc);
}

sg_pipeline make_fill_stencil_pipeline(sg_shader shader, bool even_odd = false) {
    sg_pipeline_desc desc{};
    desc.shader = shader;
    desc.layout.buffers[0].stride = sizeof(SolidVertex);
    desc.layout.attrs[0] = {0, 0, SG_VERTEXFORMAT_FLOAT2};
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.colors[0].write_mask = SG_COLORMASK_NONE;
    desc.cull_mode = SG_CULLMODE_NONE;
    desc.stencil.enabled = true;
    desc.stencil.front.compare = SG_COMPAREFUNC_ALWAYS;
    desc.stencil.front.pass_op = even_odd ? SG_STENCILOP_INVERT : SG_STENCILOP_INCR_WRAP;
    desc.stencil.back.compare = SG_COMPAREFUNC_ALWAYS;
    desc.stencil.back.pass_op = even_odd ? SG_STENCILOP_INVERT : SG_STENCILOP_DECR_WRAP;
    desc.stencil.read_mask = 0xFF;
    desc.stencil.write_mask = 0xFF;
    return sg_make_pipeline(&desc);
}

sg_pipeline make_fill_cover_pipeline(sg_shader shader) {
    sg_pipeline_desc desc{};
    desc.shader = shader;
    desc.layout.buffers[0].stride = sizeof(SolidVertex);
    desc.layout.attrs[0] = {0, 0, SG_VERTEXFORMAT_FLOAT2};
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.stencil.enabled = true;
    desc.stencil.front.compare = SG_COMPAREFUNC_NOT_EQUAL;
    desc.stencil.front.fail_op = SG_STENCILOP_ZERO;
    desc.stencil.front.depth_fail_op = SG_STENCILOP_ZERO;
    desc.stencil.front.pass_op = SG_STENCILOP_ZERO;
    desc.stencil.back = desc.stencil.front;
    desc.stencil.read_mask = 0xFF;
    desc.stencil.write_mask = 0xFF;
    return sg_make_pipeline(&desc);
}

sg_pipeline make_paint_pipeline(sg_shader shader, bool stencil_cover, bool stencil_fringe = false) {
    sg_pipeline_desc desc{};
    desc.shader = shader;
    desc.layout.buffers[0].stride = sizeof(PathVertex);
    desc.layout.attrs[0] = {0, offsetof(PathVertex, x), SG_VERTEXFORMAT_FLOAT2};
    desc.layout.attrs[1] = {0, offsetof(PathVertex, u), SG_VERTEXFORMAT_FLOAT2};
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    if (stencil_cover) {
        desc.stencil.enabled = true;
        desc.stencil.front.compare = SG_COMPAREFUNC_NOT_EQUAL;
        desc.stencil.front.fail_op = SG_STENCILOP_ZERO;
        desc.stencil.front.depth_fail_op = SG_STENCILOP_ZERO;
        desc.stencil.front.pass_op = SG_STENCILOP_ZERO;
        desc.stencil.back = desc.stencil.front;
        desc.stencil.read_mask = 0xFF;
        desc.stencil.write_mask = 0xFF;
    } else if (stencil_fringe) {
        desc.stencil.enabled = true;
        desc.stencil.front.compare = SG_COMPAREFUNC_EQUAL;
        desc.stencil.back.compare = SG_COMPAREFUNC_EQUAL;
        desc.stencil.read_mask = 0xFF;
        desc.stencil.write_mask = 0xFF;
    }
    return sg_make_pipeline(&desc);
}

sg_pipeline make_glyph_pipeline(sg_shader shader) {
    sg_pipeline_desc desc{};
    desc.shader = shader;
    desc.layout.buffers[0].stride = sizeof(GlyphVertex);
    desc.layout.attrs[0] = {0, offsetof(GlyphVertex, x), SG_VERTEXFORMAT_FLOAT2};
    desc.layout.attrs[1] = {0, offsetof(GlyphVertex, u), SG_VERTEXFORMAT_FLOAT2};
    desc.layout.attrs[2] = {0, offsetof(GlyphVertex, red), SG_VERTEXFORMAT_UBYTE4N};
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    return sg_make_pipeline(&desc);
}

sg_pipeline make_composite_pipeline(sg_shader shader) {
    sg_pipeline_desc desc{};
    desc.shader = shader;
    desc.layout.buffers[0].stride = sizeof(TextureVertex);
    desc.layout.attrs[0] = {0, offsetof(TextureVertex, x), SG_VERTEXFORMAT_FLOAT2};
    desc.layout.attrs[1] = {0, offsetof(TextureVertex, u), SG_VERTEXFORMAT_FLOAT2};
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.colors[0].blend.enabled = true;
    desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
    desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    return sg_make_pipeline(&desc);
}

template <class Vertex>
bool draw_mesh(SokolBackend::State &state, sg_pipeline pipeline,
               const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices,
               const void *fragment_uniforms, size_t fragment_uniform_size, sg_view view = {},
               sg_sampler sampler = {}, sg_buffer vertex_buffer = {}) {
    if (vertices.empty() || indices.empty())
        return true;
    const sg_range vertex_data{vertices.data(), vertices.size() * sizeof(Vertex)};
    const sg_range index_data{indices.data(), indices.size() * sizeof(uint32_t)};
    const int vertex_offset = sg_append_buffer(vertex_buffer, &vertex_data);
    const int index_offset = sg_append_buffer(state.indices, &index_data);
    if (sg_query_buffer_overflow(vertex_buffer) || sg_query_buffer_overflow(state.indices))
        return fail(state, "UI streaming buffer overflow");
    sg_apply_pipeline(pipeline);
    ++state.stats.pipeline_changes;
    sg_bindings bindings{};
    bindings.vertex_buffers[0] = vertex_buffer;
    bindings.vertex_buffer_offsets[0] = vertex_offset;
    bindings.index_buffer = state.indices;
    bindings.index_buffer_offset = index_offset;
    bindings.views[0] = view;
    bindings.samplers[0] = sampler;
    sg_apply_bindings(&bindings);
    ++state.stats.binding_changes;
    // All NativeKit shader families use generated std140 uniform blocks. A
    // vec2 therefore has a 16-byte block footprint even though only the first
    // two values are consumed by the vertex shader.
    const std::array<float, 4> viewport = {
        static_cast<float>(state.width), static_cast<float>(state.height), 0.0f, 0.0f};
    const sg_range viewport_range{viewport.data(), sizeof(viewport)};
    sg_apply_uniforms(0, &viewport_range);
    if (fragment_uniforms) {
        const sg_range fragment_range{fragment_uniforms, fragment_uniform_size};
        sg_apply_uniforms(1, &fragment_range);
    }
    sg_draw(0, static_cast<int>(indices.size()), 1);
    ++state.stats.draws;
    state.stats.transient_bytes += vertex_data.size + index_data.size;
    return true;
}

sg_buffer make_stream_buffer(size_t size, bool index) {
    sg_buffer_desc desc{};
    desc.size = size;
    desc.usage.vertex_buffer = !index;
    desc.usage.index_buffer = index;
    desc.usage.immutable = false;
    desc.usage.dynamic_update = true;
    return sg_make_buffer(&desc);
}

void destroy_target(SokolBackend::State::Target &target) {
    sg_destroy_view(target.depth_attachment);
    sg_destroy_view(target.color_attachment);
    sg_destroy_view(target.texture);
    sg_destroy_image(target.depth);
    sg_destroy_image(target.color);
    target = {};
}

bool create_target(SokolBackend::State &state, SokolBackend::State::Target &target, int width,
                   int height) {
    sg_image_desc color_desc{};
    color_desc.width = width;
    color_desc.height = height;
    color_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    color_desc.usage.color_attachment = true;
    target.color = sg_make_image(&color_desc);
    sg_image_desc depth_desc{};
    depth_desc.width = width;
    depth_desc.height = height;
    depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    depth_desc.usage.depth_stencil_attachment = true;
    target.depth = sg_make_image(&depth_desc);
    sg_view_desc texture_desc{};
    texture_desc.texture.image = target.color;
    target.texture = sg_make_view(&texture_desc);
    sg_view_desc color_view_desc{};
    color_view_desc.color_attachment.image = target.color;
    target.color_attachment = sg_make_view(&color_view_desc);
    sg_view_desc depth_view_desc{};
    depth_view_desc.depth_stencil_attachment.image = target.depth;
    target.depth_attachment = sg_make_view(&depth_view_desc);
    target.width = width;
    target.height = height;
    const bool valid = sg_query_image_state(target.color) == SG_RESOURCESTATE_VALID &&
                       sg_query_image_state(target.depth) == SG_RESOURCESTATE_VALID &&
                       sg_query_view_state(target.texture) == SG_RESOURCESTATE_VALID &&
                       sg_query_view_state(target.color_attachment) == SG_RESOURCESTATE_VALID &&
                       sg_query_view_state(target.depth_attachment) == SG_RESOURCESTATE_VALID;
    if (!valid) {
        destroy_target(target);
        return fail(state, "offscreen target creation failed");
    }
    state.stats.gpu_resources += 5;
    return true;
}

const PreparedTexture *find_texture(const PreparedPathData &path, PreparedImageToken token) {
    const auto &textures = path.textures();
    const auto found = std::find_if(textures.begin(), textures.end(),
                                    [token](const auto &texture) { return texture.token == token; });
    return found == textures.end() ? nullptr : &*found;
}

bool upload_texture(SokolBackend::State &state, const PreparedTexture &source,
                    SokolBackend::State::PaintImage &image) {
    if (!image.image.id) {
        sg_image_desc image_desc{};
        image_desc.width = source.width;
        image_desc.height = source.height;
        image_desc.pixel_format =
            source.type == PreparedTextureRgba ? SG_PIXELFORMAT_RGBA8 : SG_PIXELFORMAT_R8;
        image_desc.usage.dynamic_update = true;
        image.image = sg_make_image(&image_desc);
        sg_view_desc view_desc{};
        view_desc.texture.image = image.image;
        image.view = sg_make_view(&view_desc);
        sg_sampler_desc sampler_desc{};
        sampler_desc.min_filter =
            (source.flags & PreparedImageNearest) ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;
        sampler_desc.mag_filter = sampler_desc.min_filter;
        sampler_desc.wrap_u =
            (source.flags & PreparedImageRepeatX) ? SG_WRAP_REPEAT : SG_WRAP_CLAMP_TO_EDGE;
        sampler_desc.wrap_v =
            (source.flags & PreparedImageRepeatY) ? SG_WRAP_REPEAT : SG_WRAP_CLAMP_TO_EDGE;
        image.sampler = sg_make_sampler(&sampler_desc);
        if (sg_query_image_state(image.image) != SG_RESOURCESTATE_VALID ||
            sg_query_view_state(image.view) != SG_RESOURCESTATE_VALID ||
            sg_query_sampler_state(image.sampler) != SG_RESOURCESTATE_VALID)
            return fail(state, "NanoVG paint texture creation failed");
        image.type = source.type;
        image.flags = source.flags;
        state.stats.gpu_resources += 3;
    }
    if (image.generation != source.generation) {
        const sg_image_data data = {.mip_levels = {{source.pixels.data(), source.pixels.size()}}};
        sg_update_image(image.image, &data);
        image.generation = source.generation;
        ++state.stats.image_uploads;
        state.stats.uploaded_bytes += source.pixels.size();
    }
    return true;
}

bool resolve_paint_image(SokolBackend::State &state, const PreparedPathData &path,
                         PreparedImageToken token,
                         sg_view &view, sg_sampler &sampler, int &type, int &flags) {
    if (!token) {
        view = state.white_view;
        sampler = state.white_sampler;
        type = PreparedTextureRgba;
        flags = PreparedImagePremultiplied;
        return true;
    }
    const PreparedTexture *source = find_texture(path, token);
    if (!source)
        return fail(state, "prepared path paint texture is missing");
    auto &image = state.paint_images[&path][token];
    if (!upload_texture(state, *source, image))
        return false;
    view = image.view;
    sampler = image.sampler;
    type = image.type;
    flags = image.flags;
    return true;
}

PathUniforms path_uniforms(const PreparedPathOperation &operation, const float transform[6],
                           float opacity, int texture_type, int texture_flags) {
    PathUniforms uniforms{};
    const float inner_alpha = operation.paint.inner_color.a * opacity;
    const float outer_alpha = operation.paint.outer_color.a * opacity;
    uniforms.inner_color = {operation.paint.inner_color.r * inner_alpha,
                            operation.paint.inner_color.g * inner_alpha,
                            operation.paint.inner_color.b * inner_alpha, inner_alpha};
    uniforms.outer_color = {operation.paint.outer_color.r * outer_alpha,
                            operation.paint.outer_color.g * outer_alpha,
                            operation.paint.outer_color.b * outer_alpha, outer_alpha};
    uniforms.extent_radius_feather = {operation.paint.extent[0], operation.paint.extent[1],
                                      operation.paint.radius, operation.paint.feather};
    float paint_transform[6];
    std::memcpy(paint_transform, operation.paint.transform, sizeof(paint_transform));
    transform_multiply(paint_transform, transform);
    float inverse[6];
    transform_inverse(inverse, paint_transform);
    uniforms.inverse_x = {inverse[0], inverse[2], inverse[4], 0.0f};
    uniforms.inverse_y = {inverse[1], inverse[3], inverse[5], 0.0f};
    uniforms.mode = {operation.paint.image_token ? static_cast<float>(PathShaderMode::Image)
                                                 : static_cast<float>(PathShaderMode::Solid),
                     texture_type == PreparedTextureAlpha ? 1.0f : 0.0f,
                     (texture_flags & PreparedImageFlipY) ? 1.0f : 0.0f,
                     (texture_flags & PreparedImagePremultiplied) ? 1.0f : 0.0f};
    const float width =
        operation.kind == PreparedPathKind::Stroke ? operation.stroke_width : operation.fringe;
    uniforms.coverage = {operation.kind == PreparedPathKind::Triangles ? 0.0f : 1.0f,
                         operation.fringe > 0.0f
                             ? (width * 0.5f + operation.fringe * 0.5f) / operation.fringe
                             : 1.0f,
                         -1.0f, 0.0f};
    return uniforms;
}

void append_path_range(PathMesh &mesh, const PreparedPathData &path, uint32_t offset,
                       uint32_t count, bool strip, const float transform[6]) {
    if (count < 3)
        return;
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (uint32_t index = 0; index < count; ++index) {
        const auto &source = path.vertices()[offset + index];
        mesh.vertices.push_back({source.x * transform[0] + source.y * transform[2] + transform[4],
                                 source.x * transform[1] + source.y * transform[3] + transform[5],
                                 source.u, source.v});
    }
    if (!strip) {
        for (uint32_t index = 1; index + 1 < count; ++index)
            mesh.indices.insert(mesh.indices.end(), {base, base + index, base + index + 1});
    } else {
        for (uint32_t index = 0; index + 2 < count; ++index) {
            if (index & 1)
                mesh.indices.insert(mesh.indices.end(),
                                    {base + index + 1, base + index, base + index + 2});
            else
                mesh.indices.insert(mesh.indices.end(),
                                    {base + index, base + index + 1, base + index + 2});
        }
    }
}

PathMesh make_paint_mesh(const PreparedPathData &path, const PreparedPathOperation &operation,
                         const float transform[6], bool fringe_only = false) {
    PathMesh mesh;
    if (operation.kind == PreparedPathKind::Triangles) {
        const uint32_t base = 0;
        for (uint32_t index = 0; index < operation.vertex_count; ++index) {
            const auto &source = path.vertices()[operation.vertex_offset + index];
            mesh.vertices.push_back(
                {source.x * transform[0] + source.y * transform[2] + transform[4],
                 source.x * transform[1] + source.y * transform[3] + transform[5], source.u,
                 source.v});
            mesh.indices.push_back(base + index);
        }
        return mesh;
    }
    for (uint32_t index = 0; index < operation.path_count; ++index) {
        const auto &range = path.paths()[operation.path_offset + index];
        if (operation.kind == PreparedPathKind::Fill && !fringe_only)
            append_path_range(mesh, path, range.fill_offset, range.fill_count, false, transform);
        if (operation.kind == PreparedPathKind::Stroke || fringe_only ||
            operation.kind == PreparedPathKind::Fill)
            append_path_range(mesh, path, range.stroke_offset, range.stroke_count, true,
                              transform);
    }
    return mesh;
}

} // namespace

bool triangulate_prepared_path(const PreparedPathData &path,
                               const PreparedPathOperation &operation, SolidMesh &mesh) {
    mesh = {};
    if (operation.kind != PreparedPathKind::Fill && operation.kind != PreparedPathKind::Stroke)
        return false;
    for (uint32_t index = 0; index < operation.path_count; ++index) {
        const auto &range = path.paths()[operation.path_offset + index];
        const uint32_t source_offset =
            operation.kind == PreparedPathKind::Fill ? range.fill_offset : range.stroke_offset;
        const uint32_t source_count =
            operation.kind == PreparedPathKind::Fill ? range.fill_count : range.stroke_count;
        if (source_count < 3 || (operation.kind == PreparedPathKind::Fill && !range.convex))
            return false;
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (uint32_t vertex = 0; vertex < source_count; ++vertex) {
            const auto &source = path.vertices()[source_offset + vertex];
            mesh.vertices.push_back({source.x, source.y});
        }
        if (operation.kind == PreparedPathKind::Fill) {
            for (uint32_t vertex = 1; vertex + 1 < source_count; ++vertex) {
                mesh.indices.push_back(base);
                mesh.indices.push_back(base + vertex);
                mesh.indices.push_back(base + vertex + 1);
            }
        } else {
            for (uint32_t vertex = 0; vertex + 2 < source_count; ++vertex) {
                if (vertex & 1) {
                    mesh.indices.push_back(base + vertex + 1);
                    mesh.indices.push_back(base + vertex);
                } else {
                    mesh.indices.push_back(base + vertex);
                    mesh.indices.push_back(base + vertex + 1);
                }
                mesh.indices.push_back(base + vertex + 2);
            }
        }
    }
    return !mesh.indices.empty();
}

SokolBackend::SokolBackend() : state_(new State) {}

SokolBackend::~SokolBackend() {
    if (state_->device) {
        for (auto &[owner, images] : state_->paint_images) {
            (void)owner;
            for (auto &[id, image] : images) {
                (void)id;
                sg_destroy_sampler(image.sampler);
                sg_destroy_view(image.view);
                sg_destroy_image(image.image);
            }
        }
        for (auto &[id, image] : state_->images) {
            (void)id;
            sg_destroy_sampler(image.sampler);
            sg_destroy_view(image.view);
            sg_destroy_image(image.image);
        }
        for (auto &[id, target] : state_->targets) {
            (void)id;
            destroy_target(target);
        }
        for (const auto &[id, atlas] : state_->atlases) {
            (void)id;
            sg_destroy_view(atlas.view);
            sg_destroy_image(atlas.image);
        }
        sg_destroy_sampler(state_->sampler);
        sg_destroy_sampler(state_->surface_sampler);
        sg_destroy_sampler(state_->white_sampler);
        sg_destroy_view(state_->white_view);
        sg_destroy_image(state_->white_image);
        sg_destroy_buffer(state_->indices);
        sg_destroy_buffer(state_->composite_vertices);
        sg_destroy_buffer(state_->glyph_vertices);
        sg_destroy_buffer(state_->solid_vertices);
        sg_destroy_pipeline(state_->color_glyph_pipeline);
        sg_destroy_shader(state_->color_glyph_shader);
        sg_destroy_pipeline(state_->sdf_glyph_pipeline);
        sg_destroy_shader(state_->sdf_glyph_shader);
        sg_destroy_pipeline(state_->alpha_glyph_pipeline);
        sg_destroy_shader(state_->alpha_glyph_shader);
        sg_destroy_pipeline(state_->composite_pipeline);
        sg_destroy_shader(state_->composite_shader);
        sg_destroy_pipeline(state_->solid_pipeline);
        sg_destroy_pipeline(state_->fill_cover_pipeline);
        sg_destroy_pipeline(state_->fill_stencil_pipeline);
        sg_destroy_pipeline(state_->fill_stencil_even_odd_pipeline);
        sg_destroy_pipeline(state_->paint_cover_pipeline);
        sg_destroy_pipeline(state_->paint_fringe_pipeline);
        sg_destroy_pipeline(state_->paint_pipeline);
        sg_destroy_shader(state_->paint_shader);
        sg_destroy_shader(state_->solid_shader);
        state_->device.reset();
    }
    delete state_;
}

bool SokolBackend::initialize() {
    if (state_->initialized)
        return fail(*state_, "Sokol backend is already initialized");
    std::string device_error;
    state_->device = GraphicsDevice::acquire(&device_error);
    if (!state_->device)
        return fail(*state_, device_error.c_str());
    state_->solid_shader = make_solid_shader();
    state_->paint_shader = make_path_shader();
    state_->alpha_glyph_shader = make_glyph_shader(GlyphMode::Alpha);
    state_->sdf_glyph_shader = make_glyph_shader(GlyphMode::Sdf);
    state_->color_glyph_shader = make_glyph_shader(GlyphMode::Color);
    state_->composite_shader = make_composite_shader();
    state_->solid_pipeline = make_solid_pipeline(state_->solid_shader);
    state_->fill_stencil_pipeline = make_fill_stencil_pipeline(state_->solid_shader);
    state_->fill_stencil_even_odd_pipeline = make_fill_stencil_pipeline(state_->solid_shader, true);
    state_->fill_cover_pipeline = make_fill_cover_pipeline(state_->solid_shader);
    state_->paint_pipeline = make_paint_pipeline(state_->paint_shader, false);
    state_->paint_cover_pipeline = make_paint_pipeline(state_->paint_shader, true);
    state_->paint_fringe_pipeline = make_paint_pipeline(state_->paint_shader, false, true);
    state_->alpha_glyph_pipeline = make_glyph_pipeline(state_->alpha_glyph_shader);
    state_->sdf_glyph_pipeline = make_glyph_pipeline(state_->sdf_glyph_shader);
    state_->color_glyph_pipeline = make_glyph_pipeline(state_->color_glyph_shader);
    state_->composite_pipeline = make_composite_pipeline(state_->composite_shader);
    sg_sampler_desc sampler_desc{};
    sampler_desc.min_filter = SG_FILTER_NEAREST;
    sampler_desc.mag_filter = SG_FILTER_NEAREST;
    sampler_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sampler_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    state_->sampler = sg_make_sampler(&sampler_desc);
    sampler_desc.min_filter = SG_FILTER_LINEAR;
    sampler_desc.mag_filter = SG_FILTER_LINEAR;
    state_->surface_sampler = sg_make_sampler(&sampler_desc);
    const uint32_t white_pixel = UINT32_MAX;
    sg_image_desc white_desc{};
    white_desc.width = 1;
    white_desc.height = 1;
    white_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    white_desc.data.mip_levels[0] = {&white_pixel, sizeof(white_pixel)};
    state_->white_image = sg_make_image(&white_desc);
    sg_view_desc white_view_desc{};
    white_view_desc.texture.image = state_->white_image;
    state_->white_view = sg_make_view(&white_view_desc);
    sg_sampler_desc white_sampler_desc{};
    white_sampler_desc.min_filter = SG_FILTER_NEAREST;
    white_sampler_desc.mag_filter = SG_FILTER_NEAREST;
    white_sampler_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    white_sampler_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    state_->white_sampler = sg_make_sampler(&white_sampler_desc);
    state_->solid_vertices = make_stream_buffer(4 * 1024 * 1024, false);
    state_->glyph_vertices = make_stream_buffer(4 * 1024 * 1024, false);
    state_->composite_vertices = make_stream_buffer(1024 * 1024, false);
    state_->indices = make_stream_buffer(4 * 1024 * 1024, true);
    state_->initialized =
        sg_query_pipeline_state(state_->solid_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->fill_stencil_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->fill_stencil_even_odd_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->fill_cover_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->paint_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->paint_cover_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->paint_fringe_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->alpha_glyph_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->sdf_glyph_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->color_glyph_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->composite_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_sampler_state(state_->sampler) == SG_RESOURCESTATE_VALID &&
        sg_query_sampler_state(state_->surface_sampler) == SG_RESOURCESTATE_VALID &&
        sg_query_image_state(state_->white_image) == SG_RESOURCESTATE_VALID &&
        sg_query_view_state(state_->white_view) == SG_RESOURCESTATE_VALID &&
        sg_query_sampler_state(state_->white_sampler) == SG_RESOURCESTATE_VALID &&
        sg_query_buffer_state(state_->solid_vertices) == SG_RESOURCESTATE_VALID &&
        sg_query_buffer_state(state_->glyph_vertices) == SG_RESOURCESTATE_VALID &&
        sg_query_buffer_state(state_->composite_vertices) == SG_RESOURCESTATE_VALID &&
        sg_query_buffer_state(state_->indices) == SG_RESOURCESTATE_VALID;
    if (state_->initialized)
        state_->stats.gpu_resources = 28;
    return state_->initialized || fail(*state_, "Sokol UI resource creation failed");
}

bool SokolBackend::valid() const {
    return state_->initialized;
}

bool SokolBackend::begin_window_pass(int width, int height, uint32_t framebuffer, bool clear) {
    if (!valid() || state_->in_pass || width <= 0 || height <= 0)
        return fail(*state_, "invalid window pass");
    state_->width = width;
    state_->height = height;
    sg_pass pass{};
    pass.action.colors[0].load_action = clear ? SG_LOADACTION_CLEAR : SG_LOADACTION_LOAD;
    pass.action.colors[0].clear_value = {0.025f, 0.035f, 0.07f, 1.0f};
    pass.action.depth = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, 1.0f};
    pass.action.stencil = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, 0};
    pass.swapchain.width = width;
    pass.swapchain.height = height;
    pass.swapchain.sample_count = 1;
    pass.swapchain.color_format = SG_PIXELFORMAT_RGBA8;
    pass.swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    pass.swapchain.gl.framebuffer = framebuffer;
    sg_begin_pass(&pass);
    state_->in_pass = true;
    ++state_->stats.passes;
    return true;
}

bool SokolBackend::begin_target_pass(ResourceId target_id, int width, int height,
                                     bool load_existing) {
    if (!valid() || state_->in_pass || !is_resource_id(target_id, ResourceKind::RenderTarget) ||
        width <= 0 || height <= 0)
        return fail(*state_, "invalid offscreen pass");
    auto &target = state_->targets[target_id.value];
    if (target.width != width || target.height != height) {
        if (target.color.id) {
            destroy_target(target);
            state_->stats.gpu_resources -= 5;
        }
        if (!create_target(*state_, target, width, height))
            return false;
    }
    state_->width = width;
    state_->height = height;
    sg_pass pass{};
    pass.action.colors[0].load_action = load_existing ? SG_LOADACTION_LOAD : SG_LOADACTION_CLEAR;
    pass.action.colors[0].clear_value = {0.0f, 0.0f, 0.0f, 0.0f};
    pass.action.depth = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, 1.0f};
    pass.action.stencil = {SG_LOADACTION_CLEAR, SG_STOREACTION_STORE, 0};
    pass.attachments.colors[0] = target.color_attachment;
    pass.attachments.depth_stencil = target.depth_attachment;
    sg_begin_pass(&pass);
    state_->in_pass = true;
    ++state_->stats.passes;
    return true;
}

bool SokolBackend::begin_surface_pass(ResourceId target_id, const SurfaceDescriptor &description,
                                       bool load_existing) {
    if (description.format != SurfacePixelFormat::Rgba8 || description.width <= 0 ||
        description.height <= 0 ||
        (description.alpha != SurfaceAlphaMode::Opaque &&
         description.alpha != SurfaceAlphaMode::Premultiplied) ||
        (description.filter != SurfaceFilter::Nearest &&
         description.filter != SurfaceFilter::Linear) ||
        description.color_space != SurfaceColorSpace::Linear)
        return fail(*state_, "invalid surface descriptor");
    return begin_target_pass(target_id, description.width, description.height, load_existing);
}

bool SokolBackend::surface_has_content(ResourceId target_id) const {
    if (!valid() || !is_resource_id(target_id, ResourceKind::RenderTarget))
        return false;
    return state_->surfaces.find(target_id.value) != state_->surfaces.end() &&
           state_->targets.find(target_id.value) != state_->targets.end();
}

bool SokolBackend::surface_is_current(ResourceId target_id, uint32_t generation,
                                      const SurfaceDescriptor &description) const {
    if (!valid() || !is_resource_id(target_id, ResourceKind::RenderTarget) || generation == 0 ||
        description.width <= 0 || description.height <= 0)
        return false;
    const auto found = state_->surfaces.find(target_id.value);
    return found != state_->surfaces.end() && found->second.generation == generation &&
           found->second.width == description.width && found->second.height == description.height &&
           found->second.format == description.format && found->second.alpha == description.alpha &&
           found->second.filter == description.filter &&
           found->second.color_space == description.color_space &&
           state_->targets.find(target_id.value) != state_->targets.end();
}

void SokolBackend::mark_surface_current(ResourceId target_id, uint32_t generation,
                                         const SurfaceDescriptor &description) {
    if (!is_resource_id(target_id, ResourceKind::RenderTarget) || !generation ||
        description.width <= 0 || description.height <= 0)
        return;
    state_->surfaces[target_id.value] = {generation, description.width, description.height,
                                         description.format, description.alpha, description.filter,
                                         description.color_space};
}

bool SokolBackend::set_scissor(bool enabled, float x, float y, float width, float height) {
    if (!state_->in_pass)
        return fail(*state_, "scissor outside pass");
    if (!enabled) {
        sg_apply_scissor_rect(0, 0, state_->width, state_->height, true);
        return true;
    }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
        !std::isfinite(height) || width < 0.0f || height < 0.0f)
        return fail(*state_, "invalid scissor rectangle");
    const int left = std::max(0, static_cast<int>(x));
    const int top = std::max(0, static_cast<int>(y));
    const int right = std::min(state_->width, static_cast<int>(x + width + 0.999f));
    const int bottom = std::min(state_->height, static_cast<int>(y + height + 0.999f));
    sg_apply_scissor_rect(left, top, std::max(0, right - left), std::max(0, bottom - top), true);
    return true;
}

bool SokolBackend::draw_path(const PreparedPathData &path, uint32_t operation_index,
                             float opacity) {
    static const float identity[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    return draw_path_transformed(path, operation_index, identity, opacity);
}

bool SokolBackend::draw_path_transformed(const PreparedPathData &path, uint32_t operation_index,
                                         const float transform[6], float opacity) {
    if (!state_->in_pass || operation_index >= path.operations().size() || opacity < 0.0f ||
        opacity > 1.0f || !transform)
        return fail(*state_, "invalid path draw");
    const auto &operation = path.operations()[operation_index];
    sg_view paint_view{};
    sg_sampler paint_sampler{};
    int texture_type = 0;
    int texture_flags = 0;
    if (!resolve_paint_image(*state_, path, operation.paint.image_token, paint_view, paint_sampler,
                             texture_type, texture_flags))
        return false;
    PathUniforms paint =
        path_uniforms(operation, transform, opacity, texture_type, texture_flags);
    if (operation.kind == PreparedPathKind::Fill &&
        (operation.path_count != 1 || !path.paths()[operation.path_offset].convex)) {
        const std::array<float, 4> stencil_color{};
        for (uint32_t index = 0; index < operation.path_count; ++index) {
            const auto &range = path.paths()[operation.path_offset + index];
            if (range.fill_count < 3)
                continue;
            SolidMesh fan;
            const uint32_t base = static_cast<uint32_t>(fan.vertices.size());
            for (uint32_t vertex = 0; vertex < range.fill_count; ++vertex) {
                const auto &source = path.vertices()[range.fill_offset + vertex];
                fan.vertices.push_back(
                    {source.x * transform[0] + source.y * transform[2] + transform[4],
                     source.x * transform[1] + source.y * transform[3] + transform[5]});
            }
            for (uint32_t vertex = 1; vertex + 1 < range.fill_count; ++vertex)
                fan.indices.insert(fan.indices.end(), {base, base + vertex, base + vertex + 1});
            const sg_pipeline stencil_pipeline =
                operation.fill_rule == PathFillRule::EvenOdd
                    ? state_->fill_stencil_even_odd_pipeline
                    : state_->fill_stencil_pipeline;
            if (!draw_mesh(*state_, stencil_pipeline, fan.vertices, fan.indices,
                           stencil_color.data(), sizeof(stencil_color), {}, {},
                           state_->solid_vertices))
                return false;
        }
        const PathMesh fringe = make_paint_mesh(path, operation, transform, true);
        if (!fringe.indices.empty() &&
            !draw_mesh(*state_, state_->paint_fringe_pipeline, fringe.vertices, fringe.indices,
                       &paint, sizeof(paint), paint_view, paint_sampler, state_->solid_vertices))
            return false;
        PathMesh cover;
        const auto point = [transform](float x, float y) {
            return PathVertex{x * transform[0] + y * transform[2] + transform[4],
                              x * transform[1] + y * transform[3] + transform[5], 0.5f, 1.0f};
        };
        cover.vertices = {point(operation.bounds[0], operation.bounds[1]),
                          point(operation.bounds[2], operation.bounds[1]),
                          point(operation.bounds[2], operation.bounds[3]),
                          point(operation.bounds[0], operation.bounds[3])};
        cover.indices = {0, 1, 2, 0, 2, 3};
        paint.coverage[0] = 0.0f;
        return draw_mesh(*state_, state_->paint_cover_pipeline, cover.vertices, cover.indices,
                         &paint, sizeof(paint), paint_view, paint_sampler, state_->solid_vertices);
    }
    const PathMesh mesh = make_paint_mesh(path, operation, transform);
    if (mesh.indices.empty())
        return fail(*state_, "empty prepared path");
    return draw_mesh(*state_, state_->paint_pipeline, mesh.vertices, mesh.indices, &paint,
                     sizeof(paint), paint_view, paint_sampler, state_->solid_vertices);
}

bool SokolBackend::draw_paths(const PreparedPathData &path) {
    if (!state_->in_pass)
        return fail(*state_, "path draw outside pass");
    for (uint32_t index = 0; index < path.operations().size(); ++index)
        if (!draw_path(path, index))
            return false;
    return true;
}

bool SokolBackend::draw_image(const PreparedTexture &image, float x, float y, float width,
                              float height, const float transform[6], float opacity) {
    if (!state_->in_pass || !transform || opacity < 0.0f || opacity > 1.0f || image.width <= 0 ||
        image.height <= 0 || image.type != PreparedTextureRgba || image.pixels.empty())
        return fail(*state_, "invalid image draw");
    auto &gpu_image = state_->images[image.token];
    if (!upload_texture(*state_, image, gpu_image))
        return false;
    const auto point = [transform](float px, float py, float u, float v) {
        return TextureVertex{px * transform[0] + py * transform[2] + transform[4],
                              px * transform[1] + py * transform[3] + transform[5], u, v};
    };
    const std::vector<TextureVertex> vertices = {
        point(x, y, 0.0f, 1.0f), point(x + width, y, 1.0f, 1.0f),
        point(x + width, y + height, 1.0f, 0.0f), point(x, y + height, 0.0f, 0.0f)};
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    const std::array<float, 4> tint = {opacity, opacity, opacity, opacity};
    return draw_mesh(*state_, state_->composite_pipeline, vertices, indices, tint.data(),
                     sizeof(tint), gpu_image.view, gpu_image.sampler, state_->composite_vertices);
}

bool SokolBackend::upload_atlases(SkribidiAdapter &adapter, bool include_clean) {
    for (const auto &upload : adapter.atlas_uploads(include_clean)) {
        const uint64_t key = atlas_key(upload.texture, upload.generation);
        auto found = state_->atlases.find(key);
        bool replacing_generation = false;
        if (found == state_->atlases.end()) {
            for (const auto &[existing_key, existing_atlas] : state_->atlases) {
                (void)existing_atlas;
                if (static_cast<uint32_t>(existing_key >> 32) == upload.texture.value) {
                    replacing_generation = true;
                    break;
                }
            }
        }
        const bool dirty_covers_image =
            upload.x == 0 && upload.y == 0 && upload.width == upload.texture_width &&
            upload.height == upload.texture_height;
        bool full_upload = !upload.dirty || dirty_covers_image;
        if (found == state_->atlases.end()) {
            State::AtlasImage atlas;
            if (!create_atlas_image(atlas, upload))
                return fail(*state_, "atlas image creation failed");
            found = state_->atlases.emplace(key, std::move(atlas)).first;
            state_->stats.gpu_resources += 2;
            if (replacing_generation)
                ++state_->stats.atlas_reallocations;
            full_upload = true;
        } else if (found->second.width != upload.texture_width ||
                   found->second.height != upload.texture_height ||
                   found->second.format != upload.format ||
                   found->second.bytes_per_pixel != upload.bytes_per_pixel) {
            return fail(*state_, "atlas generation changed dimensions");
        }
        const uint64_t dirty_bytes = upload.dirty
                                         ? static_cast<uint64_t>(upload.width) * upload.height *
                                               upload.bytes_per_pixel
                                         : 0;
        state_->stats.atlas_dirty_bytes += dirty_bytes;
        if (upload.dirty)
            state_->stats.atlas_dirty_capacity_bytes +=
                static_cast<uint64_t>(upload.texture_width) * upload.texture_height *
                upload.bytes_per_pixel;
        copy_atlas_pixels(found->second, upload, full_upload);
        uint64_t uploaded_bytes = 0;
        if (full_upload) {
            const sg_image_data data = {
                .mip_levels = {{found->second.pixels.data(), found->second.pixels.size()}}};
            sg_update_image(found->second.image, &data);
            ++state_->stats.atlas_full_uploads;
            uploaded_bytes = found->second.pixels.size();
        } else {
            sg_write_image_desc data{};
            data.src.data = {upload.pixels,
                             static_cast<size_t>(upload.row_pitch) * upload.texture_height};
            data.src.offset = static_cast<size_t>(upload.y) * upload.row_pitch +
                              static_cast<size_t>(upload.x) * upload.bytes_per_pixel;
            data.src.bytes_per_row = upload.row_pitch;
            data.src.bytes_per_slice = upload.row_pitch * upload.height;
            data.dst.image = found->second.image;
            data.dst.mip_level = 0;
            data.dst.x = upload.x;
            data.dst.y = upload.y;
            data.size.width = upload.width;
            data.size.height = upload.height;
            data.size.num_slices = 1;
            if (!sg_update_image_region(&data))
                return fail(*state_, "atlas subregion upload failed");
            ++state_->stats.atlas_subregion_uploads;
            uploaded_bytes = dirty_bytes;
        }
        found->second.generation = upload.generation;
        ++state_->stats.image_uploads;
        state_->stats.uploaded_bytes += uploaded_bytes;
        state_->stats.atlas_uploaded_bytes += uploaded_bytes;
        if (upload.dirty && !adapter.acknowledge_atlas_upload(upload.texture, upload.dirty_epoch))
            return fail(*state_, "atlas upload acknowledgement failed");
    }
    return true;
}

bool SokolBackend::draw_glyphs(const PreparedGlyphs &glyphs, float opacity) {
    static const float identity[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    return draw_glyphs_transformed(glyphs, identity, 0.0f, 0.0f, opacity);
}

bool SokolBackend::draw_glyphs_transformed(const PreparedGlyphs &glyphs, const float transform[6],
                                           float origin_x, float origin_y, float opacity) {
    if (!state_->in_pass || !transform || opacity < 0.0f || opacity > 1.0f)
        return fail(*state_, "invalid glyph draw");
    for (const auto &batch : glyphs.batches) {
        const auto atlas = state_->atlases.find(atlas_key(batch.atlas, batch.atlas_generation));
        if (atlas == state_->atlases.end())
            return fail(*state_, "glyph atlas was not uploaded");
        if (batch.atlas_generation != atlas->second.generation)
            return fail(*state_, "glyph atlas generation is stale");
        const bool color_format = atlas->second.format == AtlasTextureFormat::Rgba8Premultiplied;
        if ((batch.mode == GlyphMode::Color) != color_format ||
            (batch.mode == GlyphMode::Sdf) != (atlas->second.format == AtlasTextureFormat::R8Sdf))
            return fail(*state_, "glyph mode does not match atlas format");
        const sg_pipeline pipeline = batch.mode == GlyphMode::Color ? state_->color_glyph_pipeline
                                     : batch.mode == GlyphMode::Sdf ? state_->sdf_glyph_pipeline
                                                                    : state_->alpha_glyph_pipeline;
        std::vector<GlyphVertex> vertices(glyphs.vertices.begin() + batch.first_vertex,
                                          glyphs.vertices.begin() + batch.first_vertex +
                                              batch.vertex_count);
        for (auto &vertex : vertices) {
            const float x = vertex.x + origin_x;
            const float y = vertex.y + origin_y;
            vertex.x = x * transform[0] + y * transform[2] + transform[4];
            vertex.y = x * transform[1] + y * transform[3] + transform[5];
            vertex.alpha = static_cast<uint8_t>(vertex.alpha * opacity);
        }
        std::vector<uint32_t> indices;
        indices.reserve(batch.index_count);
        for (uint32_t index = 0; index < batch.index_count; ++index)
            indices.push_back(glyphs.indices[batch.first_index + index] - batch.first_vertex);
        if (!draw_mesh(*state_, pipeline, vertices, indices, nullptr, 0, atlas->second.view,
                       state_->sampler, state_->glyph_vertices))
            return false;
    }
    return true;
}

bool SokolBackend::draw_target(ResourceId target_id, float x, float y, float width, float height,
                               float opacity) {
    if (!state_->in_pass || opacity < 0.0f || opacity > 1.0f)
        return fail(*state_, "invalid target composite");
    const auto found = state_->targets.find(target_id.value);
    if (found == state_->targets.end())
        return fail(*state_, "target was not rendered");
    if (width <= 0.0f)
        width = static_cast<float>(found->second.width);
    if (height <= 0.0f)
        height = static_cast<float>(found->second.height);
    const std::vector<TextureVertex> vertices = {
        {x, y, 0.0f, 1.0f},
        {x + width, y, 1.0f, 1.0f},
        {x + width, y + height, 1.0f, 0.0f},
        {x, y + height, 0.0f, 0.0f},
    };
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    const std::array<float, 4> tint = {opacity, opacity, opacity, opacity};
    sg_sampler sampler = state_->sampler;
    const auto surface = state_->surfaces.find(target_id.value);
    if (surface != state_->surfaces.end()) {
        if (surface->second.filter == SurfaceFilter::Linear)
            sampler = state_->surface_sampler;
        else if (surface->second.filter != SurfaceFilter::Nearest)
            return fail(*state_, "surface filter is unsupported");
    }
    return draw_mesh(*state_, state_->composite_pipeline, vertices, indices, tint.data(),
                     sizeof(tint), found->second.texture, sampler,
                     state_->composite_vertices);
}

bool SokolBackend::end_pass() {
    if (!state_->in_pass)
        return fail(*state_, "no pass to end");
    sg_end_pass();
    state_->in_pass = false;
    return true;
}

bool SokolBackend::commit_frame() {
    if (!valid() || state_->in_pass)
        return fail(*state_, "cannot commit inside pass");
    sg_commit();
    return true;
}

bool SokolBackend::end_frame() {
    return end_pass() && commit_frame();
}

SokolBackendStats SokolBackend::stats() const {
    return state_->stats;
}

const char *SokolBackend::last_error() const {
    return state_->error.c_str();
}

} // namespace nkui
