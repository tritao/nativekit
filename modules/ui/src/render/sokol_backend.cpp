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

    sg_shader solid_shader{};
    sg_pipeline solid_pipeline{};
    sg_shader glyph_shader{};
    sg_pipeline glyph_pipeline{};
    sg_shader composite_shader{};
    sg_pipeline composite_pipeline{};
    sg_sampler sampler{};
    sg_buffer solid_vertices{};
    sg_buffer glyph_vertices{};
    sg_buffer composite_vertices{};
    sg_buffer indices{};
    std::unordered_map<uint32_t, AtlasImage> atlases;
    std::unordered_map<uint32_t, Target> targets;
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

bool fail(SokolBackend::State &state, const char *message) {
    state.error = message;
    return false;
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

sg_shader make_glyph_shader() {
    sg_shader_desc desc{};
    desc.vertex_func.source =
        "#version 330\n"
        "uniform vec2 viewport; layout(location=0) in vec2 position;"
        "layout(location=1) in vec2 uv0; layout(location=2) in vec4 color0;"
        "out vec2 uv; out vec4 color; void main(){uv=uv0;color=color0;"
        "vec2 p=vec2(position.x/viewport.x*2.0-1.0,1.0-position.y/viewport.y*2.0);"
        "gl_Position=vec4(p,0,1);}";
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
        sg_destroy_buffer(state_->indices);
        sg_destroy_buffer(state_->composite_vertices);
        sg_destroy_buffer(state_->glyph_vertices);
        sg_destroy_buffer(state_->solid_vertices);
        sg_destroy_pipeline(state_->glyph_pipeline);
        sg_destroy_shader(state_->glyph_shader);
        sg_destroy_pipeline(state_->composite_pipeline);
        sg_destroy_shader(state_->composite_shader);
        sg_destroy_pipeline(state_->solid_pipeline);
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
    state_->glyph_shader = make_glyph_shader();
    state_->composite_shader = make_composite_shader();
    state_->solid_pipeline = make_solid_pipeline(state_->solid_shader);
    state_->glyph_pipeline = make_glyph_pipeline(state_->glyph_shader);
    state_->composite_pipeline = make_composite_pipeline(state_->composite_shader);
    sg_sampler_desc sampler_desc{};
    sampler_desc.min_filter = SG_FILTER_NEAREST;
    sampler_desc.mag_filter = SG_FILTER_NEAREST;
    sampler_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sampler_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    state_->sampler = sg_make_sampler(&sampler_desc);
    state_->solid_vertices = make_stream_buffer(4 * 1024 * 1024, false);
    state_->glyph_vertices = make_stream_buffer(4 * 1024 * 1024, false);
    state_->composite_vertices = make_stream_buffer(1024 * 1024, false);
    state_->indices = make_stream_buffer(4 * 1024 * 1024, true);
    state_->initialized =
        sg_query_pipeline_state(state_->solid_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->glyph_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_pipeline_state(state_->composite_pipeline) == SG_RESOURCESTATE_VALID &&
        sg_query_sampler_state(state_->sampler) == SG_RESOURCESTATE_VALID &&
        sg_query_buffer_state(state_->solid_vertices) == SG_RESOURCESTATE_VALID &&
        sg_query_buffer_state(state_->glyph_vertices) == SG_RESOURCESTATE_VALID &&
        sg_query_buffer_state(state_->composite_vertices) == SG_RESOURCESTATE_VALID &&
        sg_query_buffer_state(state_->indices) == SG_RESOURCESTATE_VALID;
    if (state_->initialized)
        state_->stats.gpu_resources = 13;
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
    if (!state_->in_pass || operation_index >= recorder.operations().size() || opacity < 0.0f ||
        opacity > 1.0f)
        return fail(*state_, "invalid path draw");
    const auto &operation = recorder.operations()[operation_index];
    SolidMesh mesh;
    if (!triangulate_prepared_path(recorder, operation, mesh))
        return fail(*state_, "unsupported non-convex or triangle path");
    const float alpha = operation.paint.innerColor.a * opacity;
    const std::array<float, 4> color = {operation.paint.innerColor.r * alpha,
                                        operation.paint.innerColor.g * alpha,
                                        operation.paint.innerColor.b * alpha, alpha};
    return draw_mesh(*state_, state_->solid_pipeline, mesh.vertices, mesh.indices, color.data(),
                     sizeof(color), {}, {}, state_->solid_vertices);
}

bool SokolBackend::draw_paths(const NanoVGRecorder &recorder) {
    if (!state_->in_pass)
        return fail(*state_, "path draw outside pass");
    for (uint32_t index = 0; index < recorder.operations().size(); ++index)
        if (!draw_path(recorder, index))
            return false;
    return true;
}

bool SokolBackend::upload_atlases(SkribidiAdapter &adapter) {
    for (const auto &upload : adapter.pending_atlas_uploads()) {
        auto found = state_->atlases.find(upload.texture.value);
        if (found == state_->atlases.end()) {
            sg_image_desc desc{};
            desc.width = upload.texture_width;
            desc.height = upload.texture_height;
            desc.pixel_format =
                upload.bytes_per_pixel == 1 ? SG_PIXELFORMAT_R8 : SG_PIXELFORMAT_RGBA8;
            desc.usage.dynamic_update = true;
            desc.data.mip_levels[0] = {upload.pixels, static_cast<size_t>(upload.row_pitch) *
                                                          upload.texture_height};
            const sg_image image = sg_make_image(&desc);
            sg_view_desc view_desc{};
            view_desc.texture.image = image;
            const sg_view view = sg_make_view(&view_desc);
            if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID ||
                sg_query_view_state(view) != SG_RESOURCESTATE_VALID)
                return fail(*state_, "atlas image creation failed");
            found = state_->atlases
                        .emplace(upload.texture.value,
                                 State::AtlasImage{image, view, upload.texture_width,
                                                   upload.texture_height, upload.bytes_per_pixel})
                        .first;
            state_->stats.gpu_resources += 2;
        } else {
            const sg_image_data data = {
                .mip_levels = {{upload.pixels,
                                static_cast<size_t>(upload.row_pitch) * upload.texture_height}}};
            sg_update_image(found->second.image, &data);
        }
        ++state_->stats.image_uploads;
        state_->stats.uploaded_bytes +=
            static_cast<uint64_t>(upload.row_pitch) * upload.texture_height;
        if (!adapter.acknowledge_atlas_upload(upload.texture))
            return fail(*state_, "atlas upload acknowledgement failed");
    }
    return true;
}

bool SokolBackend::draw_glyphs(const PreparedGlyphs &glyphs, float opacity) {
    if (!state_->in_pass || opacity < 0.0f || opacity > 1.0f)
        return fail(*state_, "invalid glyph draw");
    for (const auto &batch : glyphs.batches) {
        if (batch.mode != GlyphMode::Alpha)
            return fail(*state_, "glyph mode is not implemented");
        const auto atlas = state_->atlases.find(batch.atlas.value);
        if (atlas == state_->atlases.end())
            return fail(*state_, "glyph atlas was not uploaded");
        std::vector<GlyphVertex> vertices(glyphs.vertices.begin() + batch.first_vertex,
                                          glyphs.vertices.begin() + batch.first_vertex +
                                              batch.vertex_count);
        for (auto &vertex : vertices)
            vertex.alpha = static_cast<uint8_t>(vertex.alpha * opacity);
        std::vector<uint32_t> indices;
        indices.reserve(batch.index_count);
        for (uint32_t index = 0; index < batch.index_count; ++index)
            indices.push_back(glyphs.indices[batch.first_index + index] - batch.first_vertex);
        if (!draw_mesh(*state_, state_->glyph_pipeline, vertices, indices, nullptr, 0,
                       atlas->second.view, state_->sampler, state_->glyph_vertices))
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
