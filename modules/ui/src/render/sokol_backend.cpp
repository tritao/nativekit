#include "sokol_backend.h"

#define SOKOL_GLCORE
#include "sokol_gfx.h"

#include <algorithm>
#include <array>
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
    sg_buffer solid_vertices{};
    sg_buffer glyph_vertices{};
    sg_buffer composite_vertices{};
    sg_buffer indices{};
    std::unordered_map<uint32_t, AtlasImage> atlases;
    std::unordered_map<uint32_t, Target> targets;
    std::unordered_map<const NanoVGRecorder *, std::unordered_map<int, PaintImage>> paint_images;
    sg_image white_image{};
    sg_view white_view{};
    sg_sampler white_sampler{};
    SokolBackendStats stats{};
    std::string error;
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

struct PaintUniforms {
    std::array<float, 4> inner;
    std::array<float, 4> outer;
    std::array<float, 4> extent_radius_feather;
    std::array<float, 4> inverse_x;
    std::array<float, 4> inverse_y;
    std::array<float, 4> params;
    std::array<float, 4> coverage;
};

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
    sg_shader_desc desc{};
    desc.vertex_func.source = "#version 330\n"
                              "uniform vec2 viewport; layout(location=0) in vec2 position;"
                              "void main(){vec2 p=vec2(position.x/viewport.x*2.0-1.0,"
                              "1.0-position.y/viewport.y*2.0);gl_Position=vec4(p,0,1);}";
    desc.fragment_func.source =
        "#version 330\n"
        "uniform vec4 color; out vec4 frag_color; void main(){frag_color=color;}";
    desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    desc.uniform_blocks[0].size = 8;
    desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    desc.uniform_blocks[0].glsl_uniforms[0].array_count = 1;
    desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "viewport";
    desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.uniform_blocks[1].size = 16;
    desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    desc.uniform_blocks[1].glsl_uniforms[0].array_count = 1;
    desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "color";
    return sg_make_shader(&desc);
}

sg_shader make_paint_shader() {
    sg_shader_desc desc{};
    desc.vertex_func.source =
        "#version 330\n"
        "uniform vec2 viewport; layout(location=0) in vec2 position;"
        "layout(location=1) in vec2 uv0; out vec2 fpos; out vec2 ftcoord;"
        "void main(){fpos=position;ftcoord=uv0;vec2 p=vec2(position.x/viewport.x*2.0-1.0,"
        "1.0-position.y/viewport.y*2.0);gl_Position=vec4(p,0,1);}";
    desc.fragment_func.source =
        "#version 330\n"
        "uniform sampler2D tex; uniform vec4 innerColor; uniform vec4 outerColor;"
        "uniform vec4 extentRadiusFeather; uniform vec4 inverseX; uniform vec4 inverseY;"
        "uniform vec4 params; uniform vec4 coverage; in vec2 fpos; in vec2 ftcoord;"
        "out vec4 frag_color;"
        "float sdroundrect(vec2 p,vec2 ext,float rad){vec2 ext2=ext-vec2(rad);"
        "vec2 d=abs(p)-ext2;return min(max(d.x,d.y),0.0)+length(max(d,0.0))-rad;}"
        "void main(){vec2 pt=vec2(dot(vec3(fpos,1),inverseX.xyz),"
        "dot(vec3(fpos,1),inverseY.xyz));vec4 c;if(params.x>1.5){"
        "vec2 uv=pt/extentRadiusFeather.xy;if(params.z>0.5)uv.y=1.0-uv.y;"
        "c=texture(tex,uv);if(params.y>0.5)c=vec4(1,1,1,c.r);c*=innerColor;"
        "if(params.w<0.5)c.rgb*=c.a;}else{float d=sdroundrect(pt,extentRadiusFeather.xy,"
        "extentRadiusFeather.z);float t=clamp((d+extentRadiusFeather.w*0.5)/"
        "max(extentRadiusFeather.w,0.0001),0.0,1.0);c=mix(innerColor,outerColor,t);}"
        "if(coverage.x>0.5){float a=min(1.0,(1.0-abs(ftcoord.x*2.0-1.0))*coverage.y)"
        "*min(1.0,ftcoord.y);if(a<coverage.z)discard;c*=a;}frag_color=c;}";
    desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    desc.uniform_blocks[0].size = 8;
    desc.uniform_blocks[0].glsl_uniforms[0] = {SG_UNIFORMTYPE_FLOAT2, 1, "viewport"};
    desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.uniform_blocks[1].size = sizeof(PaintUniforms);
    const char *names[] = {"innerColor", "outerColor", "extentRadiusFeather",
                           "inverseX",   "inverseY",   "params",
                           "coverage"};
    for (int index = 0; index < 7; ++index)
        desc.uniform_blocks[1].glsl_uniforms[index] = {SG_UNIFORMTYPE_FLOAT4, 1, names[index]};
    desc.views[0].texture = {SG_SHADERSTAGE_FRAGMENT, SG_IMAGETYPE_2D, SG_IMAGESAMPLETYPE_FLOAT,
                             false};
    desc.samplers[0] = {SG_SHADERSTAGE_FRAGMENT, SG_SAMPLERTYPE_FILTERING};
    desc.texture_sampler_pairs[0] = {SG_SHADERSTAGE_FRAGMENT, 0, 0, "tex"};
    return sg_make_shader(&desc);
}

sg_shader make_glyph_shader(GlyphMode mode) {
    sg_shader_desc desc{};
    desc.vertex_func.source =
        "#version 330\n"
        "uniform vec2 viewport; layout(location=0) in vec2 position;"
        "layout(location=1) in vec2 uv0; layout(location=2) in vec4 color0;"
        "out vec2 uv; out vec4 color; void main(){uv=uv0;color=color0;"
        "vec2 p=vec2(position.x/viewport.x*2.0-1.0,1.0-position.y/viewport.y*2.0);"
        "gl_Position=vec4(p,0,1);}";
    if (mode == GlyphMode::Color)
        desc.fragment_func.source =
            "#version 330\n"
            "uniform sampler2D tex; in vec2 uv; in vec4 color; out vec4 frag_color;"
            "void main(){frag_color=texture(tex,uv)*color;}";
    else if (mode == GlyphMode::Sdf)
        desc.fragment_func.source =
            "#version 330\n"
            "uniform sampler2D tex; in vec2 uv; in vec4 color; out vec4 frag_color;"
            "void main(){float d=texture(tex,uv).r;float w=max(fwidth(d),0.001);"
            "float a=smoothstep(0.5-w,0.5+w,d);frag_color=vec4(color.rgb,color.a*a);}";
    else
        desc.fragment_func.source =
            "#version 330\n"
            "uniform sampler2D tex; in vec2 uv; in vec4 color; out vec4 frag_color;"
            "void main(){float a=texture(tex,uv).r;frag_color=vec4(color.rgb,color.a*a);}";
    desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    desc.uniform_blocks[0].size = 8;
    desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    desc.uniform_blocks[0].glsl_uniforms[0].array_count = 1;
    desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "viewport";
    desc.views[0].texture = {SG_SHADERSTAGE_FRAGMENT, SG_IMAGETYPE_2D,
                             SG_IMAGESAMPLETYPE_UNFILTERABLE_FLOAT, false};
    desc.samplers[0] = {SG_SHADERSTAGE_FRAGMENT, SG_SAMPLERTYPE_NONFILTERING};
    desc.texture_sampler_pairs[0] = {SG_SHADERSTAGE_FRAGMENT, 0, 0, "tex"};
    return sg_make_shader(&desc);
}

sg_shader make_composite_shader() {
    sg_shader_desc desc{};
    desc.vertex_func.source =
        "#version 330\n"
        "uniform vec2 viewport; layout(location=0) in vec2 position;"
        "layout(location=1) in vec2 uv0; out vec2 uv; void main(){uv=uv0;"
        "vec2 p=vec2(position.x/viewport.x*2.0-1.0,1.0-position.y/viewport.y*2.0);"
        "gl_Position=vec4(p,0,1);}";
    desc.fragment_func.source =
        "#version 330\n"
        "uniform sampler2D tex; uniform vec4 tint; in vec2 uv; out vec4 frag_color;"
        "void main(){frag_color=texture(tex,uv)*tint;}";
    desc.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    desc.uniform_blocks[0].size = 8;
    desc.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT2;
    desc.uniform_blocks[0].glsl_uniforms[0].array_count = 1;
    desc.uniform_blocks[0].glsl_uniforms[0].glsl_name = "viewport";
    desc.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    desc.uniform_blocks[1].size = 16;
    desc.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    desc.uniform_blocks[1].glsl_uniforms[0].array_count = 1;
    desc.uniform_blocks[1].glsl_uniforms[0].glsl_name = "tint";
    desc.views[0].texture = {SG_SHADERSTAGE_FRAGMENT, SG_IMAGETYPE_2D, SG_IMAGESAMPLETYPE_FLOAT,
                             false};
    desc.samplers[0] = {SG_SHADERSTAGE_FRAGMENT, SG_SAMPLERTYPE_NONFILTERING};
    desc.texture_sampler_pairs[0] = {SG_SHADERSTAGE_FRAGMENT, 0, 0, "tex"};
    return sg_make_shader(&desc);
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

sg_pipeline make_fill_stencil_pipeline(sg_shader shader) {
    sg_pipeline_desc desc{};
    desc.shader = shader;
    desc.layout.buffers[0].stride = sizeof(SolidVertex);
    desc.layout.attrs[0] = {0, 0, SG_VERTEXFORMAT_FLOAT2};
    desc.index_type = SG_INDEXTYPE_UINT32;
    desc.colors[0].write_mask = SG_COLORMASK_NONE;
    desc.cull_mode = SG_CULLMODE_NONE;
    desc.stencil.enabled = true;
    desc.stencil.front.compare = SG_COMPAREFUNC_ALWAYS;
    desc.stencil.front.pass_op = SG_STENCILOP_INCR_WRAP;
    desc.stencil.back.compare = SG_COMPAREFUNC_ALWAYS;
    desc.stencil.back.pass_op = SG_STENCILOP_DECR_WRAP;
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
    const std::array<float, 2> viewport = {static_cast<float>(state.width),
                                           static_cast<float>(state.height)};
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

const PreparedTexture *find_texture(const NanoVGRecorder &recorder, int id) {
    const auto &textures = recorder.textures();
    const auto found = std::find_if(textures.begin(), textures.end(),
                                    [id](const auto &texture) { return texture.id == id; });
    return found == textures.end() ? nullptr : &*found;
}

bool resolve_paint_image(SokolBackend::State &state, const NanoVGRecorder &recorder, int id,
                         sg_view &view, sg_sampler &sampler, int &type, int &flags) {
    if (!id) {
        view = state.white_view;
        sampler = state.white_sampler;
        type = NVG_TEXTURE_RGBA;
        flags = NVG_IMAGE_PREMULTIPLIED;
        return true;
    }
    const PreparedTexture *source = find_texture(recorder, id);
    if (!source)
        return fail(state, "NanoVG paint texture is missing");
    auto &image = state.paint_images[&recorder][id];
    if (!image.image.id) {
        sg_image_desc image_desc{};
        image_desc.width = source->width;
        image_desc.height = source->height;
        image_desc.pixel_format =
            source->type == NVG_TEXTURE_RGBA ? SG_PIXELFORMAT_RGBA8 : SG_PIXELFORMAT_R8;
        image_desc.usage.dynamic_update = true;
        image.image = sg_make_image(&image_desc);
        sg_view_desc view_desc{};
        view_desc.texture.image = image.image;
        image.view = sg_make_view(&view_desc);
        sg_sampler_desc sampler_desc{};
        sampler_desc.min_filter =
            (source->flags & NVG_IMAGE_NEAREST) ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;
        sampler_desc.mag_filter = sampler_desc.min_filter;
        sampler_desc.wrap_u =
            (source->flags & NVG_IMAGE_REPEATX) ? SG_WRAP_REPEAT : SG_WRAP_CLAMP_TO_EDGE;
        sampler_desc.wrap_v =
            (source->flags & NVG_IMAGE_REPEATY) ? SG_WRAP_REPEAT : SG_WRAP_CLAMP_TO_EDGE;
        image.sampler = sg_make_sampler(&sampler_desc);
        if (sg_query_image_state(image.image) != SG_RESOURCESTATE_VALID ||
            sg_query_view_state(image.view) != SG_RESOURCESTATE_VALID ||
            sg_query_sampler_state(image.sampler) != SG_RESOURCESTATE_VALID)
            return fail(state, "NanoVG paint texture creation failed");
        image.type = source->type;
        image.flags = source->flags;
        state.stats.gpu_resources += 3;
    }
    if (image.generation != source->generation) {
        const sg_image_data data = {.mip_levels = {{source->pixels.data(), source->pixels.size()}}};
        sg_update_image(image.image, &data);
        image.generation = source->generation;
        ++state.stats.image_uploads;
        state.stats.uploaded_bytes += source->pixels.size();
    }
    view = image.view;
    sampler = image.sampler;
    type = image.type;
    flags = image.flags;
    return true;
}

PaintUniforms paint_uniforms(const PreparedPathOperation &operation, const float transform[6],
                             float opacity, int texture_type, int texture_flags) {
    PaintUniforms uniforms{};
    const float inner_alpha = operation.paint.innerColor.a * opacity;
    const float outer_alpha = operation.paint.outerColor.a * opacity;
    uniforms.inner = {operation.paint.innerColor.r * inner_alpha,
                      operation.paint.innerColor.g * inner_alpha,
                      operation.paint.innerColor.b * inner_alpha, inner_alpha};
    uniforms.outer = {operation.paint.outerColor.r * outer_alpha,
                      operation.paint.outerColor.g * outer_alpha,
                      operation.paint.outerColor.b * outer_alpha, outer_alpha};
    uniforms.extent_radius_feather = {operation.paint.extent[0], operation.paint.extent[1],
                                      operation.paint.radius, operation.paint.feather};
    float paint_transform[6];
    std::memcpy(paint_transform, operation.paint.xform, sizeof(paint_transform));
    nvgTransformMultiply(paint_transform, transform);
    float inverse[6];
    nvgTransformInverse(inverse, paint_transform);
    uniforms.inverse_x = {inverse[0], inverse[2], inverse[4], 0.0f};
    uniforms.inverse_y = {inverse[1], inverse[3], inverse[5], 0.0f};
    uniforms.params = {operation.paint.image ? 2.0f : 0.0f,
                       texture_type == NVG_TEXTURE_ALPHA ? 1.0f : 0.0f,
                       (texture_flags & NVG_IMAGE_FLIPY) ? 1.0f : 0.0f,
                       (texture_flags & NVG_IMAGE_PREMULTIPLIED) ? 1.0f : 0.0f};
    const float width =
        operation.kind == PreparedPathKind::Stroke ? operation.stroke_width : operation.fringe;
    uniforms.coverage = {operation.kind == PreparedPathKind::Triangles ? 0.0f : 1.0f,
                         operation.fringe > 0.0f
                             ? (width * 0.5f + operation.fringe * 0.5f) / operation.fringe
                             : 1.0f,
                         -1.0f, 0.0f};
    return uniforms;
}

void append_path_range(PathMesh &mesh, const NanoVGRecorder &recorder, uint32_t offset,
                       uint32_t count, bool strip, const float transform[6]) {
    if (count < 3)
        return;
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (uint32_t index = 0; index < count; ++index) {
        const auto &source = recorder.vertices()[offset + index];
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

PathMesh make_paint_mesh(const NanoVGRecorder &recorder, const PreparedPathOperation &operation,
                         const float transform[6], bool fringe_only = false) {
    PathMesh mesh;
    if (operation.kind == PreparedPathKind::Triangles) {
        const uint32_t base = 0;
        for (uint32_t index = 0; index < operation.vertex_count; ++index) {
            const auto &source = recorder.vertices()[operation.vertex_offset + index];
            mesh.vertices.push_back(
                {source.x * transform[0] + source.y * transform[2] + transform[4],
                 source.x * transform[1] + source.y * transform[3] + transform[5], source.u,
                 source.v});
            mesh.indices.push_back(base + index);
        }
        return mesh;
    }
    for (uint32_t index = 0; index < operation.path_count; ++index) {
        const auto &path = recorder.paths()[operation.path_offset + index];
        if (operation.kind == PreparedPathKind::Fill && !fringe_only)
            append_path_range(mesh, recorder, path.fill_offset, path.fill_count, false, transform);
        if (operation.kind == PreparedPathKind::Stroke || fringe_only ||
            operation.kind == PreparedPathKind::Fill)
            append_path_range(mesh, recorder, path.stroke_offset, path.stroke_count, true,
                              transform);
    }
    return mesh;
}

} // namespace

bool triangulate_prepared_path(const NanoVGRecorder &recorder,
                               const PreparedPathOperation &operation, SolidMesh &mesh) {
    mesh = {};
    if (operation.kind != PreparedPathKind::Fill && operation.kind != PreparedPathKind::Stroke)
        return false;
    for (uint32_t index = 0; index < operation.path_count; ++index) {
        const auto &path = recorder.paths()[operation.path_offset + index];
        const uint32_t source_offset =
            operation.kind == PreparedPathKind::Fill ? path.fill_offset : path.stroke_offset;
        const uint32_t source_count =
            operation.kind == PreparedPathKind::Fill ? path.fill_count : path.stroke_count;
        if (source_count < 3 || (operation.kind == PreparedPathKind::Fill && !path.convex))
            return false;
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (uint32_t vertex = 0; vertex < source_count; ++vertex) {
            const auto &source = recorder.vertices()[source_offset + vertex];
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
    if (state_->initialized) {
        for (auto &[owner, images] : state_->paint_images) {
            (void)owner;
            for (auto &[id, image] : images) {
                (void)id;
                sg_destroy_sampler(image.sampler);
                sg_destroy_view(image.view);
                sg_destroy_image(image.image);
            }
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
        sg_destroy_pipeline(state_->paint_cover_pipeline);
        sg_destroy_pipeline(state_->paint_fringe_pipeline);
        sg_destroy_pipeline(state_->paint_pipeline);
        sg_destroy_shader(state_->paint_shader);
        sg_destroy_shader(state_->solid_shader);
        sg_shutdown();
    }
    delete state_;
}

bool SokolBackend::initialize() {
    if (state_->initialized || sg_isvalid())
        return fail(*state_, "Sokol runtime is already owned");
    sg_desc desc{};
    desc.environment.defaults = {SG_PIXELFORMAT_RGBA8, SG_PIXELFORMAT_DEPTH_STENCIL, 1};
    sg_setup(&desc);
    if (!sg_isvalid())
        return fail(*state_, "sg_setup failed");
    state_->solid_shader = make_solid_shader();
    state_->paint_shader = make_paint_shader();
    state_->alpha_glyph_shader = make_glyph_shader(GlyphMode::Alpha);
    state_->sdf_glyph_shader = make_glyph_shader(GlyphMode::Sdf);
    state_->color_glyph_shader = make_glyph_shader(GlyphMode::Color);
    state_->composite_shader = make_composite_shader();
    state_->solid_pipeline = make_solid_pipeline(state_->solid_shader);
    state_->fill_stencil_pipeline = make_fill_stencil_pipeline(state_->solid_shader);
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
        sg_query_pipeline_state(state_->fill_cover_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->paint_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->paint_cover_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->paint_fringe_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->alpha_glyph_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->sdf_glyph_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->color_glyph_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->composite_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_sampler_state(state_->sampler) == SG_RESOURCESTATE_VALID &&
        sg_query_image_state(state_->white_image) == SG_RESOURCESTATE_VALID &&
        sg_query_view_state(state_->white_view) == SG_RESOURCESTATE_VALID &&
        sg_query_sampler_state(state_->white_sampler) == SG_RESOURCESTATE_VALID &&
        sg_query_buffer_state(state_->solid_vertices) == SG_RESOURCESTATE_VALID &&
        sg_query_buffer_state(state_->glyph_vertices) == SG_RESOURCESTATE_VALID &&
        sg_query_buffer_state(state_->composite_vertices) == SG_RESOURCESTATE_VALID &&
        sg_query_buffer_state(state_->indices) == SG_RESOURCESTATE_VALID;
    if (state_->initialized)
        state_->stats.gpu_resources = 26;
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

bool SokolBackend::set_scissor(bool enabled, float x, float y, float width, float height) {
    if (!state_->in_pass)
        return fail(*state_, "scissor outside pass");
    if (!enabled) {
        sg_apply_scissor_rect(0, 0, state_->width, state_->height, true);
        return true;
    }
    const int left = std::max(0, static_cast<int>(x));
    const int top = std::max(0, static_cast<int>(y));
    const int right = std::min(state_->width, static_cast<int>(x + width + 0.999f));
    const int bottom = std::min(state_->height, static_cast<int>(y + height + 0.999f));
    sg_apply_scissor_rect(left, top, std::max(0, right - left), std::max(0, bottom - top), true);
    return true;
}

bool SokolBackend::draw_path(const NanoVGRecorder &recorder, uint32_t operation_index,
                             float opacity) {
    static const float identity[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    return draw_path_transformed(recorder, operation_index, identity, opacity);
}

bool SokolBackend::draw_path_transformed(const NanoVGRecorder &recorder, uint32_t operation_index,
                                         const float transform[6], float opacity) {
    if (!state_->in_pass || operation_index >= recorder.operations().size() || opacity < 0.0f ||
        opacity > 1.0f || !transform)
        return fail(*state_, "invalid path draw");
    const auto &operation = recorder.operations()[operation_index];
    sg_view paint_view{};
    sg_sampler paint_sampler{};
    int texture_type = 0;
    int texture_flags = 0;
    if (!resolve_paint_image(*state_, recorder, operation.paint.image, paint_view, paint_sampler,
                             texture_type, texture_flags))
        return false;
    PaintUniforms paint =
        paint_uniforms(operation, transform, opacity, texture_type, texture_flags);
    if (operation.kind == PreparedPathKind::Fill &&
        (operation.path_count != 1 || !recorder.paths()[operation.path_offset].convex)) {
        const std::array<float, 4> stencil_color{};
        for (uint32_t index = 0; index < operation.path_count; ++index) {
            const auto &path = recorder.paths()[operation.path_offset + index];
            if (path.fill_count < 3)
                continue;
            SolidMesh fan;
            const uint32_t base = static_cast<uint32_t>(fan.vertices.size());
            for (uint32_t vertex = 0; vertex < path.fill_count; ++vertex) {
                const auto &source = recorder.vertices()[path.fill_offset + vertex];
                fan.vertices.push_back(
                    {source.x * transform[0] + source.y * transform[2] + transform[4],
                     source.x * transform[1] + source.y * transform[3] + transform[5]});
            }
            for (uint32_t vertex = 1; vertex + 1 < path.fill_count; ++vertex)
                fan.indices.insert(fan.indices.end(), {base, base + vertex, base + vertex + 1});
            if (!draw_mesh(*state_, state_->fill_stencil_pipeline, fan.vertices, fan.indices,
                           stencil_color.data(), sizeof(stencil_color), {}, {},
                           state_->solid_vertices))
                return false;
        }
        const PathMesh fringe = make_paint_mesh(recorder, operation, transform, true);
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
    const PathMesh mesh = make_paint_mesh(recorder, operation, transform);
    if (mesh.indices.empty())
        return fail(*state_, "empty prepared path");
    return draw_mesh(*state_, state_->paint_pipeline, mesh.vertices, mesh.indices, &paint,
                     sizeof(paint), paint_view, paint_sampler, state_->solid_vertices);
}

bool SokolBackend::draw_paths(const NanoVGRecorder &recorder) {
    if (!state_->in_pass)
        return fail(*state_, "path draw outside pass");
    for (uint32_t index = 0; index < recorder.operations().size(); ++index)
        if (!draw_path(recorder, index))
            return false;
    return true;
}

bool SokolBackend::upload_atlases(SkribidiAdapter &adapter, bool include_clean) {
    for (const auto &upload : adapter.atlas_uploads(include_clean)) {
        auto found = state_->atlases.find(upload.texture.value);
        bool created = false;
        if (found == state_->atlases.end()) {
            sg_image_desc desc{};
            desc.width = upload.texture_width;
            desc.height = upload.texture_height;
            desc.pixel_format =
                upload.bytes_per_pixel == 1 ? SG_PIXELFORMAT_R8 : SG_PIXELFORMAT_RGBA8;
            desc.usage.dynamic_update = true;
            const sg_image image = sg_make_image(&desc);
            sg_view_desc view_desc{};
            view_desc.texture.image = image;
            const sg_view view = sg_make_view(&view_desc);
            if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID ||
                sg_query_view_state(view) != SG_RESOURCESTATE_VALID)
                return fail(*state_, "atlas image creation failed");
            State::AtlasImage atlas{image, view, upload.texture_width, upload.texture_height,
                                    upload.bytes_per_pixel, upload.generation, {}};
            atlas.pixels.resize(static_cast<size_t>(upload.texture_width) * upload.texture_height *
                                upload.bytes_per_pixel);
            found = state_->atlases.emplace(upload.texture.value, std::move(atlas)).first;
            created = true;
            state_->stats.gpu_resources += 2;
        }
        copy_atlas_pixels(found->second, upload, created || !upload.dirty);
        // Sokol currently exposes whole-image updates only; keep this fallback correct while
        // retaining the dirty rectangle in the CPU mirror for a future subregion primitive.
        const sg_image_data data = {
            .mip_levels = {{found->second.pixels.data(), found->second.pixels.size()}}};
        sg_update_image(found->second.image, &data);
        found->second.generation = upload.generation;
        ++state_->stats.image_uploads;
        state_->stats.uploaded_bytes += found->second.pixels.size();
        if (upload.dirty && !adapter.acknowledge_atlas_upload(upload.texture))
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
        const auto atlas = state_->atlases.find(batch.atlas.value);
        if (atlas == state_->atlases.end())
            return fail(*state_, "glyph atlas was not uploaded");
        if ((batch.mode == GlyphMode::Color) != (atlas->second.bytes_per_pixel == 4))
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
    return draw_mesh(*state_, state_->composite_pipeline, vertices, indices, tint.data(),
                     sizeof(tint), found->second.texture, state_->sampler,
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
