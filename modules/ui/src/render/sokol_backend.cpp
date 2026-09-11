#include "sokol_backend.h"

#include "frame_resources.h"
#include "graphics_device.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <string>
#include <unordered_map>

namespace nkui {

struct SokolBackend::State {
    struct AtlasImage {
        GpuImageHandle image{};
        GpuViewHandle view{};
        int width = 0;
        int height = 0;
        AtlasTextureFormat format = AtlasTextureFormat::R8Mask;
        uint8_t bytes_per_pixel = 0;
        uint32_t generation = 0;
        std::vector<uint8_t> pixels;
    };

    struct Target {
        GpuImageHandle color{};
        GpuImageHandle depth{};
        GpuViewHandle texture{};
        GpuViewHandle color_attachment{};
        GpuViewHandle depth_attachment{};
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
        GpuImageHandle image{};
        GpuViewHandle view{};
        GpuSamplerHandle sampler{};
        uint32_t generation = 0;
        int type = 0;
        int flags = 0;
    };

    GpuBufferHandle solid_vertices{};
    GpuBufferHandle glyph_vertices{};
    GpuBufferHandle composite_vertices{};
    GpuBufferHandle indices{};
    std::unordered_map<uint64_t, AtlasImage> atlases;
    std::unordered_map<uint32_t, Target> targets;
    std::unordered_map<uint32_t, SurfaceState> surfaces;
    std::unordered_map<const PreparedPathData *,
                       std::unordered_map<PreparedImageToken, PaintImage>> paint_images;
    std::unordered_map<uint32_t, PaintImage> images;
    SokolBackendStats stats{};
    std::string error;
    const nk_sokol_api *api = nullptr;
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

bool create_atlas_image(GpuResourceRegistry &gpu, SokolBackend::State::AtlasImage &atlas,
                        const AtlasUpload &upload) {
    sg_image_desc desc{};
    desc.width = upload.texture_width;
    desc.height = upload.texture_height;
    desc.pixel_format = upload.format == AtlasTextureFormat::Rgba8Premultiplied
                            ? SG_PIXELFORMAT_RGBA8
                            : SG_PIXELFORMAT_R8;
    desc.usage.dynamic_update = true;
    atlas.image = gpu.create_image(desc);
    sg_view_desc view_desc{};
    view_desc.texture.image = gpu.resolve(atlas.image);
    atlas.view = gpu.create_view(view_desc);
    if (!atlas.image || !atlas.view) {
        gpu.destroy(atlas.view);
        gpu.destroy(atlas.image);
        return false;
    }
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

bool valid_atlas_upload(const AtlasUpload &upload) {
    if (!upload.pixels || upload.texture_width <= 0 || upload.texture_height <= 0 ||
        (upload.bytes_per_pixel != 1 && upload.bytes_per_pixel != 4) || upload.row_pitch <= 0 ||
        upload.row_pitch < upload.texture_width * upload.bytes_per_pixel || upload.x < 0 ||
        upload.y < 0 || upload.width <= 0 || upload.height <= 0 ||
        upload.x > upload.texture_width - upload.width ||
        upload.y > upload.texture_height - upload.height)
        return false;
    return true;
}

void retire_atlas_generations(SokolBackend::State &state, AtlasTextureId texture,
                              uint32_t generation) {
    auto &gpu = state.device->gpu_resources();
    for (auto iterator = state.atlases.begin(); iterator != state.atlases.end();) {
        if (static_cast<uint32_t>(iterator->first >> 32) != texture.value ||
            iterator->second.generation == generation) {
            ++iterator;
            continue;
        }
        gpu.destroy(iterator->second.view);
        gpu.destroy(iterator->second.image);
        iterator = state.atlases.erase(iterator);
    }
}

template <class Vertex>
bool draw_mesh(SokolBackend::State &state, sg_pipeline pipeline,
               const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices,
               const void *fragment_uniforms, size_t fragment_uniform_size, sg_view view = {},
               sg_sampler sampler = {}, GpuBufferHandle vertex_buffer_handle = {}) {
    if (vertices.empty() || indices.empty())
        return true;
    const sg_buffer vertex_buffer =
        state.device->gpu_resources().resolve(vertex_buffer_handle);
    const sg_buffer index_buffer = state.device->gpu_resources().resolve(state.indices);
    if (!vertex_buffer.id || !index_buffer.id)
        return fail(state, "UI streaming buffer is unavailable");
    const sg_range vertex_data{vertices.data(), vertices.size() * sizeof(Vertex)};
    const sg_range index_data{indices.data(), indices.size() * sizeof(uint32_t)};
    const int vertex_offset = state.api->gfx->append_buffer(vertex_buffer, &vertex_data);
    const int index_offset = state.api->gfx->append_buffer(index_buffer, &index_data);
    if (state.api->gfx->query_buffer_overflow(vertex_buffer) ||
        state.api->gfx->query_buffer_overflow(index_buffer))
        return fail(state, "UI streaming buffer overflow");
    state.api->gfx->apply_pipeline(pipeline);
    ++state.stats.pipeline_changes;
    sg_bindings bindings{};
    bindings.vertex_buffers[0] = vertex_buffer;
    bindings.vertex_buffer_offsets[0] = vertex_offset;
    bindings.index_buffer = index_buffer;
    bindings.index_buffer_offset = index_offset;
    bindings.views[0] = view;
    bindings.samplers[0] = sampler;
    state.api->gfx->apply_bindings(&bindings);
    ++state.stats.binding_changes;
    // All NativeKit shader families use generated std140 uniform blocks. A
    // vec2 therefore has a 16-byte block footprint even though only the first
    // two values are consumed by the vertex shader.
    const std::array<float, 4> viewport = {
        static_cast<float>(state.width), static_cast<float>(state.height), 0.0f, 0.0f};
    const sg_range viewport_range{viewport.data(), sizeof(viewport)};
    state.api->gfx->apply_uniforms(0, &viewport_range);
    if (fragment_uniforms) {
        const sg_range fragment_range{fragment_uniforms, fragment_uniform_size};
        state.api->gfx->apply_uniforms(1, &fragment_range);
    }
    state.api->gfx->draw(0, static_cast<int>(indices.size()), 1);
    ++state.stats.draws;
    state.stats.transient_bytes += vertex_data.size + index_data.size;
    return true;
}

void destroy_target(SokolBackend::State &state, SokolBackend::State::Target &target) {
    auto &gpu = state.device->gpu_resources();
    gpu.destroy(target.depth_attachment);
    gpu.destroy(target.color_attachment);
    gpu.destroy(target.texture);
    gpu.destroy(target.depth);
    gpu.destroy(target.color);
    target = {};
}

bool create_target(SokolBackend::State &state, SokolBackend::State::Target &target, int width,
                   int height) {
    sg_image_desc color_desc{};
    color_desc.width = width;
    color_desc.height = height;
    color_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    color_desc.usage.color_attachment = true;
    auto &gpu = state.device->gpu_resources();
    target.color = gpu.create_image(color_desc);
    sg_image_desc depth_desc{};
    depth_desc.width = width;
    depth_desc.height = height;
    depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    depth_desc.usage.depth_stencil_attachment = true;
    target.depth = gpu.create_image(depth_desc);
    sg_view_desc texture_desc{};
    texture_desc.texture.image = gpu.resolve(target.color);
    target.texture = gpu.create_view(texture_desc);
    sg_view_desc color_view_desc{};
    color_view_desc.color_attachment.image = gpu.resolve(target.color);
    target.color_attachment = gpu.create_view(color_view_desc);
    sg_view_desc depth_view_desc{};
    depth_view_desc.depth_stencil_attachment.image = gpu.resolve(target.depth);
    target.depth_attachment = gpu.create_view(depth_view_desc);
    target.width = width;
    target.height = height;
    const bool valid = target.color && target.depth && target.texture &&
                       target.color_attachment && target.depth_attachment;
    if (!valid) {
        destroy_target(state, target);
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
    auto &gpu = state.device->gpu_resources();
    if (!image.image) {
        sg_image_desc image_desc{};
        image_desc.width = source.width;
        image_desc.height = source.height;
        image_desc.pixel_format =
            source.type == PreparedTextureRgba ? SG_PIXELFORMAT_RGBA8 : SG_PIXELFORMAT_R8;
        image_desc.usage.dynamic_update = true;
        image.image = gpu.create_image(image_desc);
        sg_view_desc view_desc{};
        view_desc.texture.image = gpu.resolve(image.image);
        image.view = gpu.create_view(view_desc);
        sg_sampler_desc sampler_desc{};
        sampler_desc.min_filter =
            (source.flags & PreparedImageNearest) ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;
        sampler_desc.mag_filter = sampler_desc.min_filter;
        sampler_desc.wrap_u =
            (source.flags & PreparedImageRepeatX) ? SG_WRAP_REPEAT : SG_WRAP_CLAMP_TO_EDGE;
        sampler_desc.wrap_v =
            (source.flags & PreparedImageRepeatY) ? SG_WRAP_REPEAT : SG_WRAP_CLAMP_TO_EDGE;
        image.sampler = gpu.create_sampler(sampler_desc);
        if (!image.image || !image.view || !image.sampler) {
            gpu.destroy(image.sampler);
            gpu.destroy(image.view);
            gpu.destroy(image.image);
            return fail(state, "prepared path texture creation failed");
        }
        image.type = source.type;
        image.flags = source.flags;
        state.stats.gpu_resources += 3;
    }
    if (image.generation != source.generation) {
        const sg_image_data data = {.mip_levels = {{source.pixels.data(), source.pixels.size()}}};
        state.api->gfx->update_image(gpu.resolve(image.image), &data);
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
        view = state.device->resources().white_view;
        sampler = state.device->resources().white_sampler;
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
    view = state.device->gpu_resources().resolve(image.view);
    sampler = state.device->gpu_resources().resolve(image.sampler);
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

SokolBackend::SokolBackend(const nk_sokol_api *api) : state_(new State) {
    state_->api = api;
}

SokolBackend::~SokolBackend() {
    if (state_->device) {
        auto &gpu = state_->device->gpu_resources();
        for (auto &[owner, images] : state_->paint_images) {
            (void)owner;
            for (auto &[id, image] : images) {
                (void)id;
                gpu.destroy(image.sampler);
                gpu.destroy(image.view);
                gpu.destroy(image.image);
            }
        }
        for (auto &[id, image] : state_->images) {
            (void)id;
            gpu.destroy(image.sampler);
            gpu.destroy(image.view);
            gpu.destroy(image.image);
        }
        for (auto &[id, target] : state_->targets) {
            (void)id;
            destroy_target(*state_, target);
        }
        for (const auto &[id, atlas] : state_->atlases) {
            (void)id;
            gpu.destroy(atlas.view);
            gpu.destroy(atlas.image);
        }
        gpu.destroy(state_->indices);
        gpu.destroy(state_->composite_vertices);
        gpu.destroy(state_->glyph_vertices);
        gpu.destroy(state_->solid_vertices);
        state_->device.reset();
    }
    delete state_;
}

bool SokolBackend::initialize() {
    if (state_->initialized)
        return fail(*state_, "Sokol backend is already initialized");
    std::string device_error;
    state_->device = GraphicsDevice::acquire(state_->api, &device_error);
    if (!state_->device)
        return fail(*state_, device_error.c_str());
    auto &gpu = state_->device->gpu_resources();
    auto make_stream_buffer = [&gpu](size_t size, bool index) {
        sg_buffer_desc description{};
        description.size = size;
        description.usage.vertex_buffer = !index;
        description.usage.index_buffer = index;
        description.usage.immutable = false;
        description.usage.dynamic_update = true;
        return gpu.create_buffer(description);
    };
    state_->solid_vertices = make_stream_buffer(4 * 1024 * 1024, false);
    state_->glyph_vertices = make_stream_buffer(4 * 1024 * 1024, false);
    state_->composite_vertices = make_stream_buffer(1024 * 1024, false);
    state_->indices = make_stream_buffer(4 * 1024 * 1024, true);
    state_->initialized =
        gpu.resolve(state_->solid_vertices).id && gpu.resolve(state_->glyph_vertices).id &&
        gpu.resolve(state_->composite_vertices).id && gpu.resolve(state_->indices).id;
    if (state_->initialized)
        state_->stats.gpu_resources = 28;
    return state_->initialized || fail(*state_, "Sokol UI resource creation failed");
}

bool SokolBackend::valid() const {
    return state_->initialized;
}

bool SokolBackend::begin_window_pass(int width, int height,
                                     const nk_surface_frame_target &target, bool clear) {
    if (!valid() || state_->in_pass || width <= 0 || height <= 0)
        return fail(*state_, "invalid window pass");
    const nk_graphics_api supported_api =
        state_->api->gfx->query_backend() == SG_BACKEND_GLES3 ? NK_GRAPHICS_OPENGL_ES
                                                               : NK_GRAPHICS_OPENGL;
    if (target.struct_size < sizeof(target) || target.api != supported_api)
        return fail(*state_, "unsupported window target");
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
    pass.swapchain.gl.framebuffer = static_cast<uint32_t>(target.native_target);
    state_->api->gfx->begin_pass(&pass);
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
        if (target.color) {
            destroy_target(*state_, target);
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
    pass.attachments.colors[0] = state_->device->gpu_resources().resolve(target.color_attachment);
    pass.attachments.depth_stencil =
        state_->device->gpu_resources().resolve(target.depth_attachment);
    state_->api->gfx->begin_pass(&pass);
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
        state_->api->gfx->apply_scissor_rect(0, 0, state_->width, state_->height, true);
        return true;
    }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
        !std::isfinite(height) || width < 0.0f || height < 0.0f)
        return fail(*state_, "invalid scissor rectangle");
    const int left = std::max(0, static_cast<int>(x));
    const int top = std::max(0, static_cast<int>(y));
    const int right = std::min(state_->width, static_cast<int>(x + width + 0.999f));
    const int bottom = std::min(state_->height, static_cast<int>(y + height + 0.999f));
    state_->api->gfx->apply_scissor_rect(left, top, std::max(0, right - left),
                                         std::max(0, bottom - top), true);
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
                    ? state_->device->resources().fill_stencil_even_odd_pipeline
                    : state_->device->resources().fill_stencil_pipeline;
            if (!draw_mesh(*state_, stencil_pipeline, fan.vertices, fan.indices,
                           stencil_color.data(), sizeof(stencil_color), {}, {},
                           state_->solid_vertices))
                return false;
        }
        const PathMesh fringe = make_paint_mesh(path, operation, transform, true);
        if (!fringe.indices.empty() &&
            !draw_mesh(*state_, state_->device->resources().paint_fringe_pipeline, fringe.vertices,
                       fringe.indices,
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
        return draw_mesh(*state_, state_->device->resources().paint_cover_pipeline, cover.vertices,
                         cover.indices,
                         &paint, sizeof(paint), paint_view, paint_sampler, state_->solid_vertices);
    }
    const PathMesh mesh = make_paint_mesh(path, operation, transform);
    if (mesh.indices.empty())
        return fail(*state_, "empty prepared path");
    return draw_mesh(*state_, state_->device->resources().paint_pipeline, mesh.vertices,
                     mesh.indices, &paint,
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
        return draw_mesh(*state_, state_->device->resources().composite_pipeline, vertices, indices,
                         tint.data(), sizeof(tint),
                         state_->device->gpu_resources().resolve(gpu_image.view),
                         state_->device->gpu_resources().resolve(gpu_image.sampler),
                         state_->composite_vertices);
}

bool SokolBackend::upload_atlases(SkribidiAdapter &adapter, bool include_clean) {
    for (const auto &upload : adapter.atlas_uploads(include_clean)) {
        if (!valid_atlas_upload(upload))
            return fail(*state_, "invalid atlas upload region");
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
        const bool new_generation = found == state_->atlases.end();
        if (found == state_->atlases.end()) {
            State::AtlasImage atlas;
            if (!create_atlas_image(state_->device->gpu_resources(), atlas, upload))
                return fail(*state_, "atlas image creation failed");
            found = state_->atlases.emplace(key, std::move(atlas)).first;
            state_->stats.gpu_resources += 2;
            if (replacing_generation)
                ++state_->stats.atlas_reallocations;
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
        // Atlas images use Sokol's dynamic-update storage. Each update rotates
        // to another in-flight image slot; a subregion update would therefore
        // discard all glyphs outside the dirty rectangle in that slot. The
        // retained CPU mirror is the source of truth, so upload the complete
        // mirror whenever the atlas changes.
        copy_atlas_pixels(found->second, upload, true);
        const sg_image_data data = {
            .mip_levels = {{found->second.pixels.data(), found->second.pixels.size()}}};
        state_->api->gfx->update_image(
            state_->device->gpu_resources().resolve(found->second.image), &data);
        ++state_->stats.atlas_full_uploads;
        const uint64_t uploaded_bytes = found->second.pixels.size();
        found->second.generation = upload.generation;
        ++state_->stats.image_uploads;
        state_->stats.uploaded_bytes += uploaded_bytes;
        state_->stats.atlas_uploaded_bytes += uploaded_bytes;
        if (upload.dirty && !adapter.acknowledge_atlas_upload(upload.texture, upload.dirty_epoch))
            return fail(*state_, "atlas upload acknowledgement failed");
        if (new_generation)
            retire_atlas_generations(*state_, upload.texture, upload.generation);
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
        const sg_pipeline pipeline =
            batch.mode == GlyphMode::Color ? state_->device->resources().color_glyph_pipeline
            : batch.mode == GlyphMode::Sdf ? state_->device->resources().sdf_glyph_pipeline
                                           : state_->device->resources().alpha_glyph_pipeline;
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
        if (!draw_mesh(*state_, pipeline, vertices, indices, nullptr, 0,
                       state_->device->gpu_resources().resolve(atlas->second.view),
                       state_->device->resources().sampler, state_->glyph_vertices))
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
    sg_sampler sampler = state_->device->resources().sampler;
    const auto surface = state_->surfaces.find(target_id.value);
    if (surface != state_->surfaces.end()) {
        if (surface->second.filter == SurfaceFilter::Linear)
            sampler = state_->device->resources().surface_sampler;
        else if (surface->second.filter != SurfaceFilter::Nearest)
            return fail(*state_, "surface filter is unsupported");
    }
    return draw_mesh(*state_, state_->device->resources().composite_pipeline, vertices, indices,
                     tint.data(), sizeof(tint),
                     state_->device->gpu_resources().resolve(found->second.texture), sampler,
                     state_->composite_vertices);
}

bool SokolBackend::end_pass() {
    if (!state_->in_pass)
        return fail(*state_, "no pass to end");
    state_->api->gfx->end_pass();
    state_->in_pass = false;
    return true;
}

bool SokolBackend::commit_frame() {
    if (!valid() || state_->in_pass)
        return fail(*state_, "cannot commit inside pass");
    state_->api->gfx->commit();
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
