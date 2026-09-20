#include "ui_renderer.h"

#include "frame_resources.h"
#include "ui_shader_sources.h"

#include "adapter_internal.h"
#include "nativekit_gpu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

namespace nkui {

class UiRendererImpl final : public UiRenderer {
  public:
    explicit UiRendererImpl(nk_surface surface);
    UiRendererImpl(nk_surface surface, const nk_surface_frame_target *frame_target);
    ~UiRendererImpl() override;
    bool initialize() override;
    bool valid() const override;
    bool lost() const override;
    bool beginFrame(bool record, const nk_surface_frame_target *frame_target) override;
    bool beginWindowPass(int width, int height, bool clear) override;
    bool beginTargetPass(ResourceId target, int width, int height, bool load_existing) override;
    bool beginEffectPass(ResourceId target, uint64_t cache_key, int width, int height,
                         bool &cache_hit) override;
    bool beginRasterPass(ResourceId target, uint64_t cache_key, int width, int height,
                         bool &cache_hit) override;
    bool beginSurfacePass(ResourceId target, const SurfaceDescriptor &description,
                          bool load_existing) override;
    bool drawSurfaceMesh(const SurfaceMeshView &mesh) override;
    bool surfaceHasContent(ResourceId target) const override;
    bool surfaceIsCurrent(ResourceId target, uint32_t generation,
                          const SurfaceDescriptor &description) const override;
    void markSurfaceCurrent(ResourceId target, uint32_t generation,
                            const SurfaceDescriptor &description) override;
    bool setScissor(bool enabled, float x, float y, float width, float height) override;
    bool drawPath(const PreparedPathData &path, uint32_t operation_index, float opacity) override;
    bool drawPath(const PreparedPathData &path, uint32_t operation_index, const float transform[6],
                  float opacity) override;
    bool drawImage(const PreparedTexture &image, float x, float y, float width, float height,
                   const float transform[6], float opacity) override;
    bool drawBoxShadow(float x, float y, float width, float height, const float transform[6],
                       float opacity, const BoxShadowDescriptor &shadow) override;
    bool uploadAtlases(TextEngine &engine, bool include_clean) override;
    bool drawGlyphs(const PreparedGlyphs &glyphs, float opacity) override;
    bool drawGlyphs(const PreparedGlyphs &glyphs, const float transform[6], float origin_x,
                    float origin_y, float opacity) override;
    bool compositeImage(ResourceId target, float x, float y, float width, float height,
                        const float transform[6], float opacity) override;
    bool compositeImage(nk_graphics_image image, float x, float y, float width, float height,
                        const float transform[6], float opacity) override;
    bool applyEffect(ResourceId source, const EffectDescriptor &effect) override;
    bool applyEffectRegion(ResourceId source, const EffectDescriptor &effect, float x, float y,
                           float width, float height) override;
    bool applyCustomEffect(ResourceId source, const CustomEffectDescriptor &effect) override;
    bool applyCustomEffectRegion(ResourceId source, const CustomEffectDescriptor &effect, float x,
                                 float y, float width, float height) override;
    bool registerCustomEffect(const CustomEffectRegistration &registration) override;
    bool applyMask(ResourceId source, const MaskDescriptor &mask,
                   const PreparedTexture *image) override;
    bool endPass() override;
    bool endFrame() override;
    UiRendererStats stats() const override;
    const char *lastError() const override;

    struct State;

  private:
    State *state_ = nullptr;
};

struct UiRendererImpl::State {
    struct AtlasImage {
        nkgpu_image image{};
        int width = 0;
        int height = 0;
        AtlasTextureFormat format = AtlasTextureFormat::R8Mask;
        uint8_t bytes_per_pixel = 0;
        uint32_t generation = 0;
        std::vector<uint8_t> pixels;
    };

    struct Target {
        nkgpu_image color{};
        nkgpu_image depth{};
        nk_graphics_image image{};
        int width = 0;
        int height = 0;
    };

    struct TargetPoolKey {
        int width = 0;
        int height = 0;

        bool operator==(const TargetPoolKey &other) const {
            return width == other.width && height == other.height;
        }
    };

    struct TargetPoolKeyHash {
        size_t operator()(const TargetPoolKey &key) const {
            const auto width = static_cast<size_t>(static_cast<uint32_t>(key.width));
            const auto height = static_cast<size_t>(static_cast<uint32_t>(key.height));
            return (width * size_t{0x9E3779B1u}) ^ (height + (width << 6) + (width >> 2));
        }
    };

    struct EffectCacheEntry {
        Target target;
        uint64_t last_used_frame = 0;
    };

    struct TargetAlias {
        bool raster = false;
        uint64_t key = 0;
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
        nkgpu_image image{};
        nkgpu_sampler sampler{};
        uint32_t generation = 0;
        PreparedTextureType type = PreparedTextureType::Rgba;
        PreparedImageFlags flags = PreparedImageFlags::None;
    };

    struct CustomEffect {
        uint32_t registration_id = 0;
        uint32_t parameter_components = 0;
        uint32_t pass_count = 1;
        uint32_t sampling_inputs = 1;
        std::array<float, 4> ink_overflow{};
        nkgpu_shader shader{};
        nkgpu_pipeline pipeline{};
    };

    nkgpu_renderer renderer{};
    nkgpu_shader solid_shader{};
    nkgpu_shader path_shader{};
    nkgpu_shader alpha_glyph_shader{};
    nkgpu_shader sdf_glyph_shader{};
    nkgpu_shader color_glyph_shader{};
    nkgpu_shader composite_shader{};
    nkgpu_shader effect_shader{};
    nkgpu_shader blur_shader{};
    nkgpu_shader drop_shadow_shader{};
    nkgpu_shader box_shadow_shader{};
    nkgpu_shader mask_shader{};
    nkgpu_shader surface_mesh_shader{};
    nkgpu_pipeline solid_pipeline{};
    nkgpu_pipeline fill_stencil_pipeline{};
    nkgpu_pipeline fill_stencil_even_odd_pipeline{};
    nkgpu_pipeline fill_cover_pipeline{};
    nkgpu_pipeline path_pipeline{};
    nkgpu_pipeline path_cover_pipeline{};
    nkgpu_pipeline path_fringe_pipeline{};
    nkgpu_pipeline alpha_glyph_pipeline{};
    nkgpu_pipeline sdf_glyph_pipeline{};
    nkgpu_pipeline color_glyph_pipeline{};
    nkgpu_pipeline composite_pipeline{};
    nkgpu_pipeline effect_pipeline{};
    nkgpu_pipeline blur_pipeline{};
    nkgpu_pipeline drop_shadow_pipeline{};
    nkgpu_pipeline box_shadow_pipeline{};
    nkgpu_pipeline mask_pipeline{};
    nkgpu_pipeline surface_mesh_pipeline{};
    nkgpu_sampler sampler{};
    nkgpu_sampler glyph_sampler{};
    nkgpu_sampler surface_sampler{};
    nkgpu_sampler white_sampler{};
    nkgpu_image white_image{};
    nkgpu_buffer solid_vertices{};
    nkgpu_buffer glyph_vertices{};
    nkgpu_buffer composite_vertices{};
    nkgpu_buffer surface_mesh_vertices{};
    nkgpu_buffer indices{};
    std::unordered_map<uint64_t, AtlasImage> atlases;
    std::unordered_map<uint32_t, Target> targets;
    std::unordered_map<TargetPoolKey, std::vector<Target>, TargetPoolKeyHash> transient_target_pool;
    uint64_t transient_target_pool_hits = 0;
    uint64_t transient_target_pool_misses = 0;
    std::unordered_map<uint64_t, EffectCacheEntry> effect_cache;
    std::unordered_map<uint64_t, EffectCacheEntry> raster_cache;
    std::unordered_map<uint32_t, TargetAlias> target_aliases;
    uint64_t effect_cache_hits = 0;
    uint64_t effect_cache_misses = 0;
    uint64_t raster_cache_hits = 0;
    uint64_t raster_cache_misses = 0;
    uint64_t frame_serial = 0;
    std::unordered_map<uint32_t, SurfaceState> surfaces;
    std::unordered_map<const PreparedPathData *, std::unordered_map<PreparedImageToken, PaintImage>>
        paint_images;
    std::unordered_map<uint32_t, PaintImage> images;
    std::unordered_map<uint32_t, CustomEffect> custom_effects;
    std::unordered_map<const TextEngine *, TextEngineStats> text_stats;
    UiRendererStats stats{};
    std::string error;
    nk_surface surface = 0;
    nk_graphics_api graphics_api = 0;
    nk_graphics_device device_identity{};
    int width = 0;
    int height = 0;
    bool initialized = false;
    bool in_frame = false;
    bool in_pass = false;
    /* Sealed-submission recording for the frame in progress. */
    nkgpu_batch batch{};
    bool recording = false;
    std::vector<uint8_t> record_commands;
    nk_surface_frame_target frame_target{};
    bool has_frame_target = false;
};

namespace {

struct TextureVertex {
    float x;
    float y;
    float u;
    float v;
};

struct ColorMatrixUniforms {
    std::array<float, 4> rows[5];
};

static_assert(sizeof(ColorMatrixUniforms) == sizeof(float) * kColorMatrixComponents);

struct BlurUniforms {
    std::array<float, 4> value;
};

static_assert(sizeof(BlurUniforms) == sizeof(float) * 4);

struct DropShadowUniforms {
    std::array<float, 12> value;
};

static_assert(sizeof(DropShadowUniforms) == sizeof(float) * 12);

struct BoxShadowUniforms {
    std::array<float, 4> value[4];
};

static_assert(sizeof(BoxShadowUniforms) == sizeof(float) * 16);

struct MaskUniforms {
    std::array<float, 4> value[3];
};

static_assert(sizeof(MaskUniforms) == sizeof(float) * 12);

struct SolidVertex {
    float x;
    float y;
};

struct SolidMesh {
    std::vector<SolidVertex> vertices;
    std::vector<uint32_t> indices;
};

enum class PathShaderMode : uint8_t {
    Solid = 0,
    LinearGradient = 1,
    Image = 2,
};

constexpr uint32_t kPathShaderVec4Count = 7 + 1 + 2 * kMaxPreparedGradientStops;

struct PathUniforms {
    std::array<float, 4> inner_color;
    std::array<float, 4> outer_color;
    std::array<float, 4> extent_radius_feather;
    std::array<float, 4> inverse_x;
    std::array<float, 4> inverse_y;
    std::array<float, 4> mode;
    std::array<float, 4> coverage;
    std::array<float, 4> gradient_line;
    std::array<float, 4> gradient_colors[kMaxPreparedGradientStops];
    std::array<float, 4> gradient_offsets[kMaxPreparedGradientStops];
};

static_assert(sizeof(PathUniforms) == sizeof(float) * 4 * kPathShaderVec4Count);

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

bool fail(UiRendererImpl::State &state, const char *message) {
    state.error = message;
    return false;
}

bool gpu_result(UiRendererImpl::State &state, nkgpu_result result) {
    if (result == NKGPU_OK)
        return true;
    state.error = nkgpu_last_error();
    if (state.error.empty())
        state.error = "NativeKit GPU operation failed";
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

void copy_atlas_pixels(UiRendererImpl::State::AtlasImage &target, const AtlasUpload &upload,
                       bool full);

bool create_atlas_image(UiRendererImpl::State &state, UiRendererImpl::State::AtlasImage &atlas,
                        const AtlasUpload &upload) {
    const nkgpu_image_format format = upload.format == AtlasTextureFormat::Rgba8Premultiplied
                                          ? NKGPU_IMAGEFORMAT_RGBA8
                                          : NKGPU_IMAGEFORMAT_R8;
    atlas.width = upload.texture_width;
    atlas.height = upload.texture_height;
    atlas.format = upload.format;
    atlas.bytes_per_pixel = upload.bytes_per_pixel;
    atlas.generation = upload.generation;
    atlas.pixels.resize(static_cast<size_t>(upload.texture_width) * upload.texture_height *
                        upload.bytes_per_pixel);
    copy_atlas_pixels(atlas, upload, true);
    if (!gpu_result(state, nkgpu_image_create(state.renderer, upload.texture_width,
                                              upload.texture_height, format, atlas.pixels.data(),
                                              static_cast<uint32_t>(atlas.pixels.size()), 1,
                                              &atlas.image)))
        return false;
    return true;
}

void copy_atlas_pixels(UiRendererImpl::State::AtlasImage &target, const AtlasUpload &upload,
                       bool full) {
    const int32_t x = full ? 0 : upload.x;
    const int32_t y = full ? 0 : upload.y;
    const int32_t width = full ? upload.texture_width : upload.width;
    const int32_t height = full ? upload.texture_height : upload.height;
    for (int32_t row = 0; row < height; ++row) {
        const auto *source = upload.pixels + static_cast<size_t>(y + row) * upload.row_pitch +
                             static_cast<size_t>(x) * upload.bytes_per_pixel;
        auto *destination =
            target.pixels.data() +
            (static_cast<size_t>(y + row) * target.width + x) * target.bytes_per_pixel;
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

void retire_atlas_generations(UiRendererImpl::State &state, AtlasTextureId texture,
                              uint32_t generation) {
    for (auto iterator = state.atlases.begin(); iterator != state.atlases.end();) {
        if (static_cast<uint32_t>(iterator->first >> 32) != texture.value ||
            iterator->second.generation == generation) {
            ++iterator;
            continue;
        }
        nkgpu_image_destroy(state.renderer, iterator->second.image);
        iterator = state.atlases.erase(iterator);
    }
}

/*
 * Command emission. A recorded frame collects packed command records in place
 * of applying them, and nkgpu_batch_append_command() validates and retains
 * every record when the pass closes. Nothing here touches GPU state, which is
 * what lets a frame be recorded while resources are still created eagerly.
 */
void append_u32(std::vector<uint8_t> &bytes, uint32_t value) {
    const uint8_t *raw = reinterpret_cast<const uint8_t *>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof(value));
}

void append_record(std::vector<uint8_t> &bytes, uint32_t opcode,
                   std::initializer_list<uint32_t> payload) {
    append_u32(bytes, opcode);
    append_u32(bytes, static_cast<uint32_t>(8 + payload.size() * sizeof(uint32_t)));
    for (const uint32_t word : payload)
        append_u32(bytes, word);
}

bool emit_pipeline(UiRendererImpl::State &state, nkgpu_pipeline pipeline) {
    if (state.recording) {
        append_record(state.record_commands, NKGPU_COMMAND_APPLY_PIPELINE, {pipeline.id});
        return true;
    }
    return gpu_result(state, nkgpu_apply_pipeline(state.renderer, pipeline));
}

bool emit_vertex_buffer(UiRendererImpl::State &state, uint32_t slot, nkgpu_buffer buffer,
                        uint32_t offset) {
    if (state.recording) {
        append_record(state.record_commands, NKGPU_COMMAND_APPLY_VERTEX_BUFFER,
                      {slot, buffer.id, offset});
        return true;
    }
    return gpu_result(state, nkgpu_apply_vertex_buffer(state.renderer, slot, buffer, offset));
}

bool emit_index_buffer(UiRendererImpl::State &state, nkgpu_buffer buffer, uint32_t offset) {
    if (state.recording) {
        append_record(state.record_commands, NKGPU_COMMAND_APPLY_INDEX_BUFFER, {buffer.id, offset});
        return true;
    }
    return gpu_result(state, nkgpu_apply_index_buffer(state.renderer, buffer, offset));
}

bool emit_image(UiRendererImpl::State &state, uint32_t slot, nkgpu_image image) {
    if (state.recording) {
        append_record(state.record_commands, NKGPU_COMMAND_APPLY_IMAGE, {slot, image.id});
        return true;
    }
    return gpu_result(state, nkgpu_apply_image(state.renderer, slot, image));
}

bool emit_graphics_image(UiRendererImpl::State &state, uint32_t slot, nk_graphics_image image) {
    if (state.recording) {
        append_record(state.record_commands, NKGPU_COMMAND_APPLY_GRAPHICS_IMAGE, {slot, image.id});
        return true;
    }
    return gpu_result(state, nkgpu_apply_graphics_image(state.renderer, slot, image));
}

bool emit_sampler(UiRendererImpl::State &state, uint32_t slot, nkgpu_sampler sampler) {
    if (state.recording) {
        append_record(state.record_commands, NKGPU_COMMAND_APPLY_SAMPLER, {slot, sampler.id});
        return true;
    }
    return gpu_result(state, nkgpu_apply_sampler(state.renderer, slot, sampler));
}

bool emit_uniforms(UiRendererImpl::State &state, uint32_t slot, const uint8_t *data,
                   uint32_t size) {
    if (state.recording) {
        append_u32(state.record_commands, NKGPU_COMMAND_APPLY_UNIFORMS);
        append_u32(state.record_commands, 8 + 8 + size);
        append_u32(state.record_commands, slot);
        append_u32(state.record_commands, size);
        state.record_commands.insert(state.record_commands.end(), data, data + size);
        return true;
    }
    return gpu_result(state, nkgpu_apply_uniform_data(state.renderer, slot, data, size));
}

bool emit_scissor(UiRendererImpl::State &state, uint32_t enabled, int32_t x, int32_t y,
                  int32_t width, int32_t height) {
    if (state.recording) {
        append_record(state.record_commands, NKGPU_COMMAND_APPLY_SCISSOR,
                      {enabled, static_cast<uint32_t>(x), static_cast<uint32_t>(y),
                       static_cast<uint32_t>(width), static_cast<uint32_t>(height)});
        return true;
    }
    return gpu_result(state, nkgpu_apply_scissor(state.renderer, enabled, x, y, width, height));
}

bool emit_draw(UiRendererImpl::State &state, uint32_t base, uint32_t count, uint32_t instances) {
    if (state.recording) {
        append_record(state.record_commands, NKGPU_COMMAND_DRAW, {base, count, instances});
        return true;
    }
    return gpu_result(state, nkgpu_draw(state.renderer, base, count, instances));
}

template <class Vertex>
bool draw_mesh(UiRendererImpl::State &state, nkgpu_pipeline pipeline,
               const std::vector<Vertex> &vertices, const std::vector<uint32_t> &indices,
               const void *fragment_uniforms, size_t fragment_uniform_size, nkgpu_image image = {},
               nkgpu_sampler sampler = {}, nkgpu_buffer vertex_buffer = {},
               nk_graphics_image external_image = {}, const void *vertex_uniforms = nullptr,
               size_t vertex_uniform_size = 0, nkgpu_image second_image = {},
               nkgpu_sampler second_sampler = {}, nk_graphics_image second_external_image = {}) {
    if (vertices.empty() || indices.empty())
        return true;
    if (!state.in_pass || indices.size() > UINT32_MAX || vertices.size() > UINT32_MAX)
        return fail(state, "invalid UI mesh or pass state");
    uint32_t vertex_offset = 0;
    uint32_t index_offset = 0;
    const size_t vertex_bytes = vertices.size() * sizeof(Vertex);
    const size_t index_bytes = indices.size() * sizeof(uint32_t);
    if (vertex_bytes > UINT32_MAX || index_bytes > UINT32_MAX ||
        !gpu_result(state,
                    nkgpu_buffer_append(state.renderer,
                                        vertex_buffer.id ? vertex_buffer : state.solid_vertices,
                                        reinterpret_cast<const uint8_t *>(vertices.data()),
                                        static_cast<uint32_t>(vertex_bytes), &vertex_offset)) ||
        !gpu_result(state,
                    nkgpu_buffer_append(state.renderer, state.indices,
                                        reinterpret_cast<const uint8_t *>(indices.data()),
                                        static_cast<uint32_t>(index_bytes), &index_offset)) ||
        !emit_pipeline(state, pipeline) ||
        !emit_vertex_buffer(state, 0, vertex_buffer.id ? vertex_buffer : state.solid_vertices,
                            vertex_offset) ||
        !emit_index_buffer(state, state.indices, index_offset))
        return false;
    ++state.stats.pipeline_changes;
    if ((image.id && !emit_image(state, 0, image)) ||
        (!image.id && external_image.id && !emit_graphics_image(state, 0, external_image)) ||
        (sampler.id && !emit_sampler(state, 0, sampler)) ||
        (second_image.id && !emit_image(state, 1, second_image)) ||
        (!second_image.id && second_external_image.id &&
         !emit_graphics_image(state, 1, second_external_image)) ||
        (second_sampler.id && !emit_sampler(state, 1, second_sampler)))
        return false;
    ++state.stats.binding_changes;
    const std::array<float, 4> viewport = {static_cast<float>(state.width),
                                           static_cast<float>(state.height), 0.0f, 0.0f};
    const void *vertex_data = vertex_uniforms ? vertex_uniforms : viewport.data();
    const size_t vertex_size = vertex_uniforms ? vertex_uniform_size : sizeof(viewport);
    if (!emit_uniforms(state, 0, reinterpret_cast<const uint8_t *>(vertex_data),
                       static_cast<uint32_t>(vertex_size)))
        return false;
    if (fragment_uniforms &&
        !emit_uniforms(state, 1, static_cast<const uint8_t *>(fragment_uniforms),
                       static_cast<uint32_t>(fragment_uniform_size)))
        return false;
    if (!emit_draw(state, 0, static_cast<uint32_t>(indices.size()), 1))
        return false;
    ++state.stats.draws;
    state.stats.transient_bytes += vertex_bytes + index_bytes;
    return true;
}

void destroy_target(UiRendererImpl::State &state, UiRendererImpl::State::Target &target) {
    if (target.depth.id)
        nkgpu_image_destroy(state.renderer, target.depth);
    if (target.color.id)
        nkgpu_image_destroy(state.renderer, target.color);
    target = {};
}

bool is_transient_target(ResourceId target) {
    return static_cast<uint16_t>(target.value) >= 0x8000u;
}

constexpr size_t kMaxPooledTransientTargets = 4;

size_t pooled_transient_target_count(const UiRendererImpl::State &state) {
    size_t result = 0;
    for (const auto &[key, targets] : state.transient_target_pool) {
        (void)key;
        result += targets.size();
    }
    return result;
}

uint64_t pooled_transient_target_bytes(const UiRendererImpl::State &state) {
    uint64_t result = 0;
    for (const auto &[key, targets] : state.transient_target_pool) {
        const uint64_t bytes =
            static_cast<uint64_t>(key.width) * static_cast<uint64_t>(key.height) * 4u;
        result += bytes * targets.size();
    }
    return result;
}

void recycle_transient_target(UiRendererImpl::State &state, UiRendererImpl::State::Target target) {
    if (!target.color.id)
        return;
    if (pooled_transient_target_count(state) >= kMaxPooledTransientTargets) {
        destroy_target(state, target);
        if (state.stats.gpu_resources)
            --state.stats.gpu_resources;
        return;
    }
    const UiRendererImpl::State::TargetPoolKey key{target.width, target.height};
    state.transient_target_pool[key].push_back(std::move(target));
}

void recycle_transient_targets(UiRendererImpl::State &state) {
    for (auto iterator = state.targets.begin(); iterator != state.targets.end();) {
        if (!is_transient_target(ResourceId{iterator->first})) {
            ++iterator;
            continue;
        }
        recycle_transient_target(state, std::move(iterator->second));
        iterator = state.targets.erase(iterator);
    }
}

bool create_target(UiRendererImpl::State &state, UiRendererImpl::State::Target &target, int width,
                   int height);

bool acquire_transient_target(UiRendererImpl::State &state, UiRendererImpl::State::Target &target,
                              int width, int height) {
    const UiRendererImpl::State::TargetPoolKey key{width, height};
    auto found = state.transient_target_pool.find(key);
    if (found != state.transient_target_pool.end() && !found->second.empty()) {
        target = std::move(found->second.back());
        found->second.pop_back();
        if (found->second.empty())
            state.transient_target_pool.erase(found);
        ++state.transient_target_pool_hits;
        return true;
    }
    ++state.transient_target_pool_misses;
    return create_target(state, target, width, height);
}

bool create_target(UiRendererImpl::State &state, UiRendererImpl::State::Target &target, int width,
                   int height) {
    nkgpu_image_desc color_desc{};
    color_desc.struct_size = sizeof(color_desc);
    color_desc.width = static_cast<uint32_t>(width);
    color_desc.height = static_cast<uint32_t>(height);
    color_desc.format = NKGPU_IMAGEFORMAT_RGBA8;
    color_desc.usage = NKGPU_IMAGE_SAMPLED | NKGPU_IMAGE_RENDER_TARGET;
    if (!gpu_result(state, nkgpu_image_create_desc(state.renderer, &color_desc, &target.color)))
        return false;
    const nkgpu_result image_result =
        nkgpu_image_get_graphics_image(state.renderer, target.color, &target.image);
    if (!gpu_result(state, image_result)) {
        nkgpu_image_destroy(state.renderer, target.color);
        target = {};
        return false;
    }
    nkgpu_image_desc depth_desc{};
    depth_desc.struct_size = sizeof(depth_desc);
    depth_desc.width = static_cast<uint32_t>(width);
    depth_desc.height = static_cast<uint32_t>(height);
    depth_desc.format = NKGPU_IMAGEFORMAT_DEPTH24_STENCIL8;
    depth_desc.usage = NKGPU_IMAGE_DEPTH_STENCIL;
    if (!gpu_result(state, nkgpu_image_create_desc(state.renderer, &depth_desc, &target.depth))) {
        nkgpu_image_destroy(state.renderer, target.color);
        target = {};
        return false;
    }
    target.width = width;
    target.height = height;
    state.stats.gpu_resources += 1;
    return true;
}

constexpr size_t kMaxCachedEffectTargets = 16;
constexpr uint64_t kMaxCachedEffectBytes = 64u * 1024u * 1024u;

uint64_t effect_target_bytes(const UiRendererImpl::State::Target &target) {
    if (target.width <= 0 || target.height <= 0)
        return 0;
    const uint64_t width = static_cast<uint64_t>(target.width);
    const uint64_t height = static_cast<uint64_t>(target.height);
    if (width > std::numeric_limits<uint64_t>::max() / height / 4u)
        return std::numeric_limits<uint64_t>::max();
    return width * height * 4u;
}

uint64_t cached_effect_bytes(const UiRendererImpl::State &state) {
    uint64_t result = 0;
    for (const auto &[key, entry] : state.effect_cache) {
        (void)key;
        const uint64_t bytes = effect_target_bytes(entry.target);
        if (result > std::numeric_limits<uint64_t>::max() - bytes)
            return std::numeric_limits<uint64_t>::max();
        result += bytes;
    }
    return result;
}

uint64_t cached_raster_bytes(const UiRendererImpl::State &state) {
    uint64_t result = 0;
    for (const auto &[key, entry] : state.raster_cache) {
        (void)key;
        const uint64_t bytes = effect_target_bytes(entry.target);
        if (result > std::numeric_limits<uint64_t>::max() - bytes)
            return std::numeric_limits<uint64_t>::max();
        result += bytes;
    }
    return result;
}

void destroy_cached_effect(UiRendererImpl::State &state,
                           UiRendererImpl::State::EffectCacheEntry &entry) {
    const bool alive = entry.target.color.id != 0;
    destroy_target(state, entry.target);
    if (alive && state.stats.gpu_resources)
        --state.stats.gpu_resources;
}

void destroy_cached_raster(UiRendererImpl::State &state,
                           UiRendererImpl::State::EffectCacheEntry &entry) {
    const bool alive = entry.target.color.id != 0;
    destroy_target(state, entry.target);
    if (alive && state.stats.gpu_resources)
        --state.stats.gpu_resources;
}

bool make_effect_cache_room(UiRendererImpl::State &state, int width, int height) {
    if (width <= 0 || height <= 0)
        return false;
    const uint64_t width_value = static_cast<uint64_t>(width);
    const uint64_t height_value = static_cast<uint64_t>(height);
    if (width_value > std::numeric_limits<uint64_t>::max() / height_value / 4u)
        return false;
    const uint64_t bytes = width_value * height_value * 4u;
    if (bytes > kMaxCachedEffectBytes)
        return false;
    while (state.effect_cache.size() >= kMaxCachedEffectTargets) {
        auto victim = state.effect_cache.end();
        for (auto iterator = state.effect_cache.begin(); iterator != state.effect_cache.end();
             ++iterator) {
            if (iterator->second.last_used_frame == state.frame_serial)
                continue;
            if (victim == state.effect_cache.end() ||
                iterator->second.last_used_frame < victim->second.last_used_frame)
                victim = iterator;
        }
        if (victim == state.effect_cache.end())
            return false;
        destroy_cached_effect(state, victim->second);
        state.effect_cache.erase(victim);
    }
    while (cached_effect_bytes(state) > kMaxCachedEffectBytes - bytes) {
        auto victim = state.effect_cache.end();
        for (auto iterator = state.effect_cache.begin(); iterator != state.effect_cache.end();
             ++iterator) {
            if (iterator->second.last_used_frame == state.frame_serial)
                continue;
            if (victim == state.effect_cache.end() ||
                iterator->second.last_used_frame < victim->second.last_used_frame)
                victim = iterator;
        }
        if (victim == state.effect_cache.end())
            return false;
        destroy_cached_effect(state, victim->second);
        state.effect_cache.erase(victim);
    }
    return true;
}

bool make_raster_cache_room(UiRendererImpl::State &state, int width, int height) {
    if (width <= 0 || height <= 0)
        return false;
    const uint64_t width_value = static_cast<uint64_t>(width);
    const uint64_t height_value = static_cast<uint64_t>(height);
    if (width_value > std::numeric_limits<uint64_t>::max() / height_value / 4u)
        return false;
    const uint64_t bytes = width_value * height_value * 4u;
    if (bytes > kMaxCachedEffectBytes)
        return false;
    while (state.raster_cache.size() >= kMaxCachedEffectTargets) {
        auto victim = state.raster_cache.end();
        for (auto iterator = state.raster_cache.begin(); iterator != state.raster_cache.end();
             ++iterator) {
            if (iterator->second.last_used_frame == state.frame_serial)
                continue;
            if (victim == state.raster_cache.end() ||
                iterator->second.last_used_frame < victim->second.last_used_frame)
                victim = iterator;
        }
        if (victim == state.raster_cache.end())
            return false;
        destroy_cached_raster(state, victim->second);
        state.raster_cache.erase(victim);
    }
    while (cached_raster_bytes(state) > kMaxCachedEffectBytes - bytes) {
        auto victim = state.raster_cache.end();
        for (auto iterator = state.raster_cache.begin(); iterator != state.raster_cache.end();
             ++iterator) {
            if (iterator->second.last_used_frame == state.frame_serial)
                continue;
            if (victim == state.raster_cache.end() ||
                iterator->second.last_used_frame < victim->second.last_used_frame)
                victim = iterator;
        }
        if (victim == state.raster_cache.end())
            return false;
        destroy_cached_raster(state, victim->second);
        state.raster_cache.erase(victim);
    }
    return true;
}

UiRendererImpl::State::Target *resolve_target(UiRendererImpl::State &state, ResourceId id) {
    const auto alias = state.target_aliases.find(id.value);
    if (alias != state.target_aliases.end()) {
        if (alias->second.raster) {
            const auto cached = state.raster_cache.find(alias->second.key);
            if (cached != state.raster_cache.end())
                return &cached->second.target;
        } else {
            const auto cached = state.effect_cache.find(alias->second.key);
            if (cached != state.effect_cache.end())
                return &cached->second.target;
        }
        state.target_aliases.erase(alias);
    }
    const auto found = state.targets.find(id.value);
    return found == state.targets.end() ? nullptr : &found->second;
}

bool begin_target_pass(UiRendererImpl::State &state, UiRendererImpl::State::Target &target,
                       bool load_existing) {
    if (!target.color.id)
        return false;
    nkgpu_render_pass_desc render_pass{};
    render_pass.struct_size = sizeof(render_pass);
    render_pass.color_count = 1;
    render_pass.colors[0].image = target.color;
    render_pass.colors[0].action.load_action =
        load_existing ? NKGPU_LOADACTION_LOAD : NKGPU_LOADACTION_CLEAR;
    render_pass.colors[0].action.store_action = NKGPU_STOREACTION_STORE;
    render_pass.colors[0].action.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
    if (target.depth.id) {
        render_pass.depth_stencil = target.depth;
        render_pass.depth_stencil_action.load_action =
            load_existing ? NKGPU_LOADACTION_LOAD : NKGPU_LOADACTION_CLEAR;
        render_pass.depth_stencil_action.store_action = NKGPU_STOREACTION_STORE;
        render_pass.depth_stencil_action.clear_depth = 1.0f;
    }
    if (state.recording) {
        nkgpu_batch_pass pass{};
        pass.struct_size = sizeof(pass);
        pass.kind = NKGPU_BATCH_PASS_RENDER;
        pass.render_pass = &render_pass;
        if (!gpu_result(state, nkgpu_batch_append_pass(state.batch, &pass)))
            return false;
    } else if (!gpu_result(state, nkgpu_begin_render_pass(state.renderer, &render_pass))) {
        return false;
    }
    state.width = target.width;
    state.height = target.height;
    state.in_pass = true;
    ++state.stats.passes;
    return true;
}

const PreparedTexture *find_texture(const PreparedPathData &path, PreparedImageToken token) {
    const auto &textures = path.textures();
    const auto found = std::find_if(textures.begin(), textures.end(), [token](const auto &texture) {
        return texture.token == token;
    });
    return found == textures.end() ? nullptr : &*found;
}

bool upload_texture(UiRendererImpl::State &state, const PreparedTexture &source,
                    UiRendererImpl::State::PaintImage &image) {
    if (!image.image) {
        const uint32_t bytes_per_pixel = source.type == PreparedTextureType::Rgba ? 4 : 1;
        const auto format = source.type == PreparedTextureType::Rgba ? NKGPU_IMAGEFORMAT_RGBA8
                                                                     : NKGPU_IMAGEFORMAT_R8;
        if (source.pixels.size() !=
                static_cast<size_t>(source.width) * source.height * bytes_per_pixel ||
            !gpu_result(state,
                        nkgpu_image_create(
                            state.renderer, static_cast<uint32_t>(source.width),
                            static_cast<uint32_t>(source.height), format, source.pixels.data(),
                            static_cast<uint32_t>(source.pixels.size()), 1, &image.image)))
            return false;
        const auto filter = has_flag(source.flags, PreparedImageFlags::Nearest)
                                ? NKGPU_FILTER_NEAREST
                                : NKGPU_FILTER_LINEAR;
        const auto wrap_u = has_flag(source.flags, PreparedImageFlags::RepeatX)
                                ? NKGPU_WRAP_REPEAT
                                : NKGPU_WRAP_CLAMP_TO_EDGE;
        const auto wrap_v = has_flag(source.flags, PreparedImageFlags::RepeatY)
                                ? NKGPU_WRAP_REPEAT
                                : NKGPU_WRAP_CLAMP_TO_EDGE;
        if (!gpu_result(state, nkgpu_sampler_create(state.renderer, filter, filter, wrap_u, wrap_v,
                                                    &image.sampler))) {
            nkgpu_image_destroy(state.renderer, image.image);
            image.image = {};
            return false;
        }
        image.type = source.type;
        image.flags = source.flags;
        state.stats.gpu_resources += 3;
        image.generation = source.generation;
        ++state.stats.image_uploads;
        state.stats.uploaded_bytes += source.pixels.size();
    }
    if (image.generation != source.generation) {
        const uint32_t bytes_per_pixel = source.type == PreparedTextureType::Rgba ? 4 : 1;
        if (!gpu_result(state,
                        nkgpu_image_update(
                            state.renderer, image.image, 0, 0, static_cast<uint32_t>(source.width),
                            static_cast<uint32_t>(source.height), source.pixels.data(),
                            static_cast<uint32_t>(source.width) * bytes_per_pixel)))
            return false;
        image.generation = source.generation;
        ++state.stats.image_uploads;
        state.stats.uploaded_bytes += source.pixels.size();
    }
    return true;
}

bool resolve_paint_image(UiRendererImpl::State &state, const PreparedPathData &path,
                         PreparedImageToken token, nkgpu_image &image, nkgpu_sampler &sampler,
                         PreparedTextureType &type, PreparedImageFlags &flags) {
    if (!token) {
        image = state.white_image;
        sampler = state.white_sampler;
        type = PreparedTextureType::Rgba;
        flags = PreparedImageFlags::Premultiplied;
        return true;
    }
    const PreparedTexture *source = find_texture(path, token);
    if (!source)
        return fail(state, "prepared path paint texture is missing");
    auto &cached = state.paint_images[&path][token];
    if (!upload_texture(state, *source, cached))
        return false;
    image = cached.image;
    sampler = cached.sampler;
    type = cached.type;
    flags = cached.flags;
    return true;
}

PathUniforms path_uniforms(const PreparedPathOperation &operation, const float transform[6],
                           float opacity, PreparedTextureType texture_type,
                           PreparedImageFlags texture_flags) {
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
    const PathShaderMode mode = operation.paint.image_token ? PathShaderMode::Image
                                : operation.paint.kind == PreparedPaintKind::LinearGradient
                                    ? PathShaderMode::LinearGradient
                                    : PathShaderMode::Solid;
    uniforms.mode = {static_cast<float>(mode),
                     mode == PathShaderMode::LinearGradient
                         ? static_cast<float>(std::min(operation.paint.gradient_stop_count,
                                                       kMaxPreparedGradientStops))
                     : texture_type == PreparedTextureType::Alpha ? 1.0f
                                                                  : 0.0f,
                     has_flag(texture_flags, PreparedImageFlags::FlipY) ? 1.0f : 0.0f,
                     has_flag(texture_flags, PreparedImageFlags::Premultiplied) ? 1.0f : 0.0f};
    if (mode == PathShaderMode::LinearGradient) {
        uniforms.gradient_line = {operation.paint.gradient_start[0],
                                  operation.paint.gradient_start[1],
                                  operation.paint.gradient_end[0], operation.paint.gradient_end[1]};
        const uint32_t stop_count =
            std::min(operation.paint.gradient_stop_count, kMaxPreparedGradientStops);
        for (uint32_t index = 0; index < stop_count; ++index) {
            const auto &stop = operation.paint.gradient_stops[index];
            const float alpha = stop.color.a * opacity;
            uniforms.gradient_colors[index] = {stop.color.r * alpha, stop.color.g * alpha,
                                               stop.color.b * alpha, alpha};
            uniforms.gradient_offsets[index] = {stop.offset, 0.0f, 0.0f, 0.0f};
        }
    }
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
            append_path_range(mesh, path, range.stroke_offset, range.stroke_count, true, transform);
    }
    return mesh;
}

} // namespace

namespace {

enum class UiShaderKind {
    Solid,
    Path,
    AlphaGlyph,
    SdfGlyph,
    ColorGlyph,
    Composite,
    Effect,
    Blur,
    DropShadow,
    BoxShadow,
    Mask,
    SurfaceMesh
};

struct ShaderSources {
    const char *vertex;
    const char *fragment;
    nkgpu_shader_language language;
};

ShaderSources shader_sources(nkgpu_backend backend, UiShaderKind kind) {
    using namespace shader_source;
    const bool es = backend == NKGPU_BACKEND_GLES3;
    const bool d3d11 = backend == NKGPU_BACKEND_D3D11;
    const bool metal = backend == NKGPU_BACKEND_METAL;
    const auto gl = [es](const char *vertex410, const char *fragment410, const char *vertex300,
                         const char *fragment300) {
        return es ? ShaderSources{vertex300, fragment300, NKGPU_SHADERLANGUAGE_GLSL}
                  : ShaderSources{vertex410, fragment410, NKGPU_SHADERLANGUAGE_GLSL};
    };
    switch (kind) {
    case UiShaderKind::Solid:
        if (d3d11)
            return {ui_shader_solid_hlsl5_vertex, ui_shader_solid_hlsl5_fragment,
                    NKGPU_SHADERLANGUAGE_HLSL5};
        if (metal)
            return {ui_shader_solid_metal_macos_vertex, ui_shader_solid_metal_macos_fragment,
                    NKGPU_SHADERLANGUAGE_MSL};
        return gl(ui_shader_solid_glsl410_vertex, ui_shader_solid_glsl410_fragment,
                  ui_shader_solid_glsl300es_vertex, ui_shader_solid_glsl300es_fragment);
    case UiShaderKind::Path:
        if (d3d11)
            return {ui_shader_path_hlsl5_vertex, ui_shader_path_hlsl5_fragment,
                    NKGPU_SHADERLANGUAGE_HLSL5};
        if (metal)
            return {ui_shader_path_metal_macos_vertex, ui_shader_path_metal_macos_fragment,
                    NKGPU_SHADERLANGUAGE_MSL};
        return gl(ui_shader_path_glsl410_vertex, ui_shader_path_glsl410_fragment,
                  ui_shader_path_glsl300es_vertex, ui_shader_path_glsl300es_fragment);
    case UiShaderKind::AlphaGlyph:
        if (d3d11)
            return {ui_shader_text_alpha_hlsl5_vertex, ui_shader_text_alpha_hlsl5_fragment,
                    NKGPU_SHADERLANGUAGE_HLSL5};
        if (metal)
            return {ui_shader_text_alpha_metal_macos_vertex,
                    ui_shader_text_alpha_metal_macos_fragment, NKGPU_SHADERLANGUAGE_MSL};
        return gl(ui_shader_text_alpha_glsl410_vertex, ui_shader_text_alpha_glsl410_fragment,
                  ui_shader_text_alpha_glsl300es_vertex, ui_shader_text_alpha_glsl300es_fragment);
    case UiShaderKind::SdfGlyph:
        if (d3d11)
            return {ui_shader_text_sdf_hlsl5_vertex, ui_shader_text_sdf_hlsl5_fragment,
                    NKGPU_SHADERLANGUAGE_HLSL5};
        if (metal)
            return {ui_shader_text_sdf_metal_macos_vertex, ui_shader_text_sdf_metal_macos_fragment,
                    NKGPU_SHADERLANGUAGE_MSL};
        return gl(ui_shader_text_sdf_glsl410_vertex, ui_shader_text_sdf_glsl410_fragment,
                  ui_shader_text_sdf_glsl300es_vertex, ui_shader_text_sdf_glsl300es_fragment);
    case UiShaderKind::ColorGlyph:
        if (d3d11)
            return {ui_shader_text_color_hlsl5_vertex, ui_shader_text_color_hlsl5_fragment,
                    NKGPU_SHADERLANGUAGE_HLSL5};
        if (metal)
            return {ui_shader_text_color_metal_macos_vertex,
                    ui_shader_text_color_metal_macos_fragment, NKGPU_SHADERLANGUAGE_MSL};
        return gl(ui_shader_text_color_glsl410_vertex, ui_shader_text_color_glsl410_fragment,
                  ui_shader_text_color_glsl300es_vertex, ui_shader_text_color_glsl300es_fragment);
    case UiShaderKind::Composite:
        if (d3d11)
            return {ui_shader_composite_hlsl5_vertex, ui_shader_composite_hlsl5_fragment,
                    NKGPU_SHADERLANGUAGE_HLSL5};
        if (metal)
            return {ui_shader_composite_metal_macos_vertex,
                    ui_shader_composite_metal_macos_fragment, NKGPU_SHADERLANGUAGE_MSL};
        return gl(ui_shader_composite_glsl410_vertex, ui_shader_composite_glsl410_fragment,
                  ui_shader_composite_glsl300es_vertex, ui_shader_composite_glsl300es_fragment);
    case UiShaderKind::Effect:
        if (d3d11)
            return {ui_shader_effect_hlsl5_vertex, ui_shader_effect_hlsl5_fragment,
                    NKGPU_SHADERLANGUAGE_HLSL5};
        if (metal)
            return {ui_shader_effect_metal_macos_vertex, ui_shader_effect_metal_macos_fragment,
                    NKGPU_SHADERLANGUAGE_MSL};
        return gl(ui_shader_effect_glsl410_vertex, ui_shader_effect_glsl410_fragment,
                  ui_shader_effect_glsl300es_vertex, ui_shader_effect_glsl300es_fragment);
    case UiShaderKind::Blur:
        if (d3d11)
            return {ui_shader_blur_hlsl5_vertex, ui_shader_blur_hlsl5_fragment,
                    NKGPU_SHADERLANGUAGE_HLSL5};
        if (metal)
            return {ui_shader_blur_metal_macos_vertex, ui_shader_blur_metal_macos_fragment,
                    NKGPU_SHADERLANGUAGE_MSL};
        return gl(ui_shader_blur_glsl410_vertex, ui_shader_blur_glsl410_fragment,
                  ui_shader_blur_glsl300es_vertex, ui_shader_blur_glsl300es_fragment);
    case UiShaderKind::DropShadow:
        if (d3d11)
            return {ui_shader_drop_shadow_hlsl5_vertex, ui_shader_drop_shadow_hlsl5_fragment,
                    NKGPU_SHADERLANGUAGE_HLSL5};
        if (metal)
            return {ui_shader_drop_shadow_metal_macos_vertex,
                    ui_shader_drop_shadow_metal_macos_fragment, NKGPU_SHADERLANGUAGE_MSL};
        return gl(ui_shader_drop_shadow_glsl410_vertex, ui_shader_drop_shadow_glsl410_fragment,
                  ui_shader_drop_shadow_glsl300es_vertex, ui_shader_drop_shadow_glsl300es_fragment);
    case UiShaderKind::BoxShadow:
        if (d3d11)
            return {ui_shader_box_shadow_hlsl5_vertex, ui_shader_box_shadow_hlsl5_fragment,
                    NKGPU_SHADERLANGUAGE_HLSL5};
        if (metal)
            return {ui_shader_box_shadow_metal_macos_vertex,
                    ui_shader_box_shadow_metal_macos_fragment, NKGPU_SHADERLANGUAGE_MSL};
        return gl(ui_shader_box_shadow_glsl410_vertex, ui_shader_box_shadow_glsl410_fragment,
                  ui_shader_box_shadow_glsl300es_vertex, ui_shader_box_shadow_glsl300es_fragment);
    case UiShaderKind::Mask:
        if (d3d11)
            return {ui_shader_mask_hlsl5_vertex, ui_shader_mask_hlsl5_fragment,
                    NKGPU_SHADERLANGUAGE_HLSL5};
        if (metal)
            return {ui_shader_mask_metal_macos_vertex, ui_shader_mask_metal_macos_fragment,
                    NKGPU_SHADERLANGUAGE_MSL};
        return gl(ui_shader_mask_glsl410_vertex, ui_shader_mask_glsl410_fragment,
                  ui_shader_mask_glsl300es_vertex, ui_shader_mask_glsl300es_fragment);
    case UiShaderKind::SurfaceMesh:
        if (d3d11)
            return {ui_shader_surface_mesh_hlsl5_vertex, ui_shader_surface_mesh_hlsl5_fragment,
                    NKGPU_SHADERLANGUAGE_HLSL5};
        if (metal)
            return {ui_shader_surface_mesh_metal_macos_vertex,
                    ui_shader_surface_mesh_metal_macos_fragment, NKGPU_SHADERLANGUAGE_MSL};
        return gl(ui_shader_surface_mesh_glsl410_vertex, ui_shader_surface_mesh_glsl410_fragment,
                  ui_shader_surface_mesh_glsl300es_vertex,
                  ui_shader_surface_mesh_glsl300es_fragment);
    }
    return {nullptr, nullptr, NKGPU_SHADERLANGUAGE_GLSL};
}

bool add_uniform(UiRendererImpl::State &state, nkgpu_shader_builder builder, uint32_t slot,
                 uint32_t member, const char *name, nkgpu_uniform_type type, uint32_t count = 1) {
    return gpu_result(state, nkgpu_shader_uniform(builder, slot, member, name, type, count));
}

bool create_shader(UiRendererImpl::State &state, UiShaderKind kind, nkgpu_shader &out_shader) {
    const ShaderSources sources = shader_sources(nkgpu_query_backend(state.renderer), kind);
    if (!sources.vertex || !sources.fragment)
        return fail(state, "UI shader source is unavailable for this graphics backend");
    nkgpu_shader_builder builder{};
    if (!gpu_result(state, nkgpu_shader_begin(state.renderer, sources.language, sources.vertex,
                                              sources.fragment, &builder)))
        return false;
    const char *attributes[3]{};
    uint32_t attribute_count = 0;
    switch (kind) {
    case UiShaderKind::Solid:
        attributes[attribute_count++] = "position";
        break;
    case UiShaderKind::Path:
    case UiShaderKind::Composite:
    case UiShaderKind::Effect:
    case UiShaderKind::Blur:
    case UiShaderKind::DropShadow:
    case UiShaderKind::BoxShadow:
    case UiShaderKind::Mask:
        attributes[attribute_count++] = "position";
        attributes[attribute_count++] = "uv0";
        break;
    case UiShaderKind::AlphaGlyph:
    case UiShaderKind::SdfGlyph:
    case UiShaderKind::ColorGlyph:
        attributes[attribute_count++] = "position";
        attributes[attribute_count++] = "uv0";
        attributes[attribute_count++] = "color0";
        break;
    case UiShaderKind::SurfaceMesh:
        attributes[attribute_count++] = "position";
        attributes[attribute_count++] = "color0";
        break;
    }
    for (uint32_t location = 0; location < attribute_count; ++location) {
        if (!gpu_result(state, nkgpu_shader_attribute(builder, location, attributes[location],
                                                      "TEXCOORD", location)))
            return false;
    }
    const char *vertex_block = nullptr;
    uint32_t vertex_size = 16;
    const char *fragment_block = nullptr;
    uint32_t fragment_size = 0;
    bool textured = false;
    switch (kind) {
    case UiShaderKind::Solid:
        vertex_block = "solid_vs_params";
        fragment_block = "solid_fs_params";
        fragment_size = 16;
        break;
    case UiShaderKind::Path:
        vertex_block = "path_vs_params";
        fragment_block = "path_fs_params";
        fragment_size = sizeof(PathUniforms);
        textured = true;
        break;
    case UiShaderKind::AlphaGlyph:
    case UiShaderKind::SdfGlyph:
    case UiShaderKind::ColorGlyph:
        vertex_block = "text_vs_params";
        textured = true;
        break;
    case UiShaderKind::Composite:
        vertex_block = "composite_vs_params";
        fragment_block = "composite_fs_params";
        fragment_size = 16;
        textured = true;
        break;
    case UiShaderKind::Effect:
        vertex_block = "effect_vs_params";
        fragment_block = "effect_fs_params";
        fragment_size = sizeof(ColorMatrixUniforms);
        textured = true;
        break;
    case UiShaderKind::Blur:
        vertex_block = "blur_vs_params";
        fragment_block = "blur_fs_params";
        fragment_size = sizeof(BlurUniforms);
        textured = true;
        break;
    case UiShaderKind::DropShadow:
        vertex_block = "drop_shadow_vs_params";
        fragment_block = "drop_shadow_fs_params";
        fragment_size = sizeof(DropShadowUniforms);
        textured = true;
        break;
    case UiShaderKind::BoxShadow:
        vertex_block = "box_shadow_vs_params";
        fragment_block = "box_shadow_fs_params";
        fragment_size = sizeof(BoxShadowUniforms);
        break;
    case UiShaderKind::Mask:
        vertex_block = "mask_vs_params";
        fragment_block = "mask_fs_params";
        fragment_size = sizeof(MaskUniforms);
        textured = true;
        break;
    case UiShaderKind::SurfaceMesh:
        vertex_block = "surface_mesh_vs_params";
        vertex_size = 64;
        break;
    }
    if (!gpu_result(
            state, nkgpu_shader_uniform_block(builder, 0, NKGPU_SHADERSTAGE_VERTEX, vertex_size)) ||
        !add_uniform(state, builder, 0, 0, vertex_block, NKGPU_UNIFORMTYPE_FLOAT4,
                     kind == UiShaderKind::SurfaceMesh ? 4 : 1))
        return false;
    if (fragment_block &&
        (!gpu_result(state, nkgpu_shader_uniform_block(builder, 1, NKGPU_SHADERSTAGE_FRAGMENT,
                                                       fragment_size)) ||
         !add_uniform(state, builder, 1, 0, fragment_block, NKGPU_UNIFORMTYPE_FLOAT4,
                      kind == UiShaderKind::Path         ? kPathShaderVec4Count
                      : kind == UiShaderKind::Effect     ? 5
                      : kind == UiShaderKind::DropShadow ? 3
                      : kind == UiShaderKind::BoxShadow  ? 4
                      : kind == UiShaderKind::Mask       ? 3
                                                         : 1)))
        return false;
    if (textured && !gpu_result(state, nkgpu_shader_texture(builder, 0, 0,
                                                            NKGPU_SHADERSTAGE_FRAGMENT, "tex_smp")))
        return false;
    if (kind == UiShaderKind::Mask &&
        !gpu_result(state, nkgpu_shader_texture(builder, 1, 1, NKGPU_SHADERSTAGE_FRAGMENT,
                                                "mask_tex_mask_smp")))
        return false;
    return gpu_result(state, nkgpu_shader_end(builder, &out_shader));
}

struct VertexAttribute {
    uint32_t location;
    uint32_t offset;
    nkgpu_vertex_format format;
};

struct PipelineOptions {
    const nkgpu_blend_state *blend = nullptr;
    const nkgpu_stencil_state *stencil = nullptr;
    bool depth_stencil = false;
    bool cull = false;
    nkgpu_cull_mode cull_mode = NKGPU_CULLMODE_NONE;
    nkgpu_face_winding winding = NKGPU_FACEWINDING_CCW;
    bool set_color_mask = false;
    nkgpu_color_write_mask color_mask = NKGPU_COLORMASK_RGBA;
};

bool create_pipeline(UiRendererImpl::State &state, nkgpu_shader shader, uint32_t stride,
                     std::initializer_list<VertexAttribute> attributes,
                     const PipelineOptions &options, nkgpu_pipeline &out_pipeline) {
    nkgpu_pipeline_builder builder{};
    if (!gpu_result(state, nkgpu_pipeline_begin(state.renderer, shader, stride, &builder)))
        return false;
    for (const auto &attribute : attributes) {
        if (!gpu_result(state, nkgpu_pipeline_attribute(builder, attribute.location, 0,
                                                        attribute.offset, attribute.format)))
            return false;
    }
    if (!gpu_result(state, nkgpu_pipeline_index_type(builder, NKGPU_INDEXTYPE_UINT32)) ||
        (options.blend && !gpu_result(state, nkgpu_pipeline_blend(builder, options.blend))) ||
        (options.stencil && !gpu_result(state, nkgpu_pipeline_stencil(builder, options.stencil))) ||
        (options.depth_stencil && !gpu_result(state, nkgpu_pipeline_depth_stencil(builder, 1))) ||
        (options.cull && !gpu_result(state, nkgpu_pipeline_cull_mode(builder, options.cull_mode,
                                                                     options.winding))) ||
        (options.set_color_mask &&
         !gpu_result(state, nkgpu_pipeline_color_write_mask(builder, options.color_mask))))
        return false;
    return gpu_result(state, nkgpu_pipeline_end(builder, &out_pipeline));
}

nkgpu_blend_state premultiplied_blend() {
    return {1,
            NKGPU_BLENDFACTOR_ONE,
            NKGPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            NKGPU_BLENDOP_ADD,
            NKGPU_BLENDFACTOR_ONE,
            NKGPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            NKGPU_BLENDOP_ADD};
}

const char *custom_fragment_source(nkgpu_backend backend,
                                   const CustomEffectRegistration &registration) {
    if (backend == NKGPU_BACKEND_D3D11)
        return registration.hlsl5_fragment;
    if (backend == NKGPU_BACKEND_METAL)
        return registration.metal_macos_fragment;
    return backend == NKGPU_BACKEND_GLES3 ? registration.glsl300es_fragment
                                          : registration.glsl410_fragment;
}

bool create_custom_effect(UiRendererImpl::State &state,
                          const CustomEffectRegistration &registration, nkgpu_shader &out_shader,
                          nkgpu_pipeline &out_pipeline) {
    if (!registration.registration_id || !registration.name || !registration.name[0] ||
        registration.parameter_components > kCustomEffectParameterComponents ||
        registration.pass_count != 1 || registration.sampling_inputs != 1)
        return fail(state, "invalid custom effect registration");
    for (const float value : registration.ink_overflow)
        if (!std::isfinite(value) || value < 0.0f)
            return fail(state, "invalid custom effect ink overflow");
    const nkgpu_backend backend = nkgpu_query_backend(state.renderer);
    const ShaderSources standard = shader_sources(backend, UiShaderKind::Effect);
    const char *fragment = custom_fragment_source(backend, registration);
    if (!standard.vertex || !fragment)
        return fail(state, "custom effect shader source is unavailable for this backend");
    nkgpu_shader_builder builder{};
    if (!gpu_result(state, nkgpu_shader_begin(state.renderer, standard.language, standard.vertex,
                                              fragment, &builder)) ||
        !gpu_result(state, nkgpu_shader_attribute(builder, 0, "position", "TEXCOORD", 0)) ||
        !gpu_result(state, nkgpu_shader_attribute(builder, 1, "uv0", "TEXCOORD", 1)) ||
        !gpu_result(state, nkgpu_shader_uniform_block(builder, 0, NKGPU_SHADERSTAGE_VERTEX, 16)) ||
        !add_uniform(state, builder, 0, 0, "effect_vs_params", NKGPU_UNIFORMTYPE_FLOAT4) ||
        !gpu_result(state, nkgpu_shader_uniform_block(
                               builder, 1, NKGPU_SHADERSTAGE_FRAGMENT,
                               static_cast<uint32_t>(sizeof(float) * kColorMatrixComponents))) ||
        !add_uniform(state, builder, 1, 0, "effect_fs_params", NKGPU_UNIFORMTYPE_FLOAT4, 5) ||
        !gpu_result(state,
                    nkgpu_shader_texture(builder, 0, 0, NKGPU_SHADERSTAGE_FRAGMENT, "tex_smp")) ||
        !gpu_result(state, nkgpu_shader_end(builder, &out_shader)))
        return false;
    const auto blend = premultiplied_blend();
    PipelineOptions options{};
    options.blend = &blend;
    return create_pipeline(
        state, out_shader, sizeof(TextureVertex),
        {{0, 0, NKGPU_VERTEXFORMAT_FLOAT2}, {1, sizeof(float) * 2, NKGPU_VERTEXFORMAT_FLOAT2}},
        options, out_pipeline);
}

nkgpu_stencil_face_state stencil_face(nkgpu_compare_func compare, nkgpu_stencil_op fail_op,
                                      nkgpu_stencil_op depth_fail_op, nkgpu_stencil_op pass_op) {
    return {compare, fail_op, depth_fail_op, pass_op};
}

} // namespace

UiRendererImpl::UiRendererImpl(nk_surface surface) : UiRendererImpl(surface, nullptr) {}

UiRendererImpl::UiRendererImpl(nk_surface surface, const nk_surface_frame_target *frame_target)
    : state_(new State) {
    state_->surface = surface;
    if (!surface) {
        state_->error = "UI renderer requires a NativeKit surface";
        return;
    }
    const nkgpu_result created =
        frame_target
            ? nkgpu_renderer_create_for_frame_target(surface, frame_target, &state_->renderer)
            : nkgpu_renderer_create(surface, &state_->renderer);
    if (!gpu_result(*state_, created))
        return;
    state_->graphics_api = nkgpu_query_graphics_api(state_->renderer);
}

UiRendererImpl::~UiRendererImpl() {
    if (!state_)
        return;
    if (state_->in_frame)
        endFrame();
    if (state_->renderer.id)
        nkgpu_renderer_destroy(state_->renderer);
    delete state_;
}

bool UiRendererImpl::initialize() {
    if (!state_ || !state_->renderer.id)
        return fail(*state_, "NativeKit GPU renderer creation failed");
    if (state_->initialized)
        return fail(*state_, "UI renderer is already initialized");

    if (!create_shader(*state_, UiShaderKind::Solid, state_->solid_shader) ||
        !create_shader(*state_, UiShaderKind::Path, state_->path_shader) ||
        !create_shader(*state_, UiShaderKind::AlphaGlyph, state_->alpha_glyph_shader) ||
        !create_shader(*state_, UiShaderKind::SdfGlyph, state_->sdf_glyph_shader) ||
        !create_shader(*state_, UiShaderKind::ColorGlyph, state_->color_glyph_shader) ||
        !create_shader(*state_, UiShaderKind::Composite, state_->composite_shader) ||
        !create_shader(*state_, UiShaderKind::Effect, state_->effect_shader) ||
        !create_shader(*state_, UiShaderKind::Blur, state_->blur_shader) ||
        !create_shader(*state_, UiShaderKind::DropShadow, state_->drop_shadow_shader) ||
        !create_shader(*state_, UiShaderKind::BoxShadow, state_->box_shadow_shader) ||
        !create_shader(*state_, UiShaderKind::Mask, state_->mask_shader) ||
        !create_shader(*state_, UiShaderKind::SurfaceMesh, state_->surface_mesh_shader))
        return false;

    const auto premultiplied = premultiplied_blend();
    const nkgpu_blend_state glyph_blend{1,
                                        NKGPU_BLENDFACTOR_SRC_ALPHA,
                                        NKGPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                        NKGPU_BLENDOP_ADD,
                                        NKGPU_BLENDFACTOR_ONE,
                                        NKGPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                                        NKGPU_BLENDOP_ADD};
    auto make_stencil = [](nkgpu_stencil_face_state front, nkgpu_stencil_face_state back) {
        nkgpu_stencil_state result{};
        result.enabled = 1;
        result.read_mask = 0xFF;
        result.write_mask = 0xFF;
        result.front = front;
        result.back = back;
        return result;
    };
    const auto keep = stencil_face(NKGPU_COMPAREFUNC_ALWAYS, NKGPU_STENCILOP_KEEP,
                                   NKGPU_STENCILOP_KEEP, NKGPU_STENCILOP_KEEP);
    const auto increment_front = stencil_face(NKGPU_COMPAREFUNC_ALWAYS, NKGPU_STENCILOP_KEEP,
                                              NKGPU_STENCILOP_KEEP, NKGPU_STENCILOP_INCREMENT_WRAP);
    const auto decrement_back = stencil_face(NKGPU_COMPAREFUNC_ALWAYS, NKGPU_STENCILOP_KEEP,
                                             NKGPU_STENCILOP_KEEP, NKGPU_STENCILOP_DECREMENT_WRAP);
    const auto invert = stencil_face(NKGPU_COMPAREFUNC_ALWAYS, NKGPU_STENCILOP_KEEP,
                                     NKGPU_STENCILOP_KEEP, NKGPU_STENCILOP_INVERT);
    const auto zero_cover = stencil_face(NKGPU_COMPAREFUNC_NOT_EQUAL, NKGPU_STENCILOP_ZERO,
                                         NKGPU_STENCILOP_ZERO, NKGPU_STENCILOP_ZERO);
    const auto equal_keep = stencil_face(NKGPU_COMPAREFUNC_EQUAL, NKGPU_STENCILOP_KEEP,
                                         NKGPU_STENCILOP_KEEP, NKGPU_STENCILOP_KEEP);
    const auto fill_stencil = make_stencil(increment_front, decrement_back);
    const auto even_odd_stencil = make_stencil(invert, invert);
    const auto cover_stencil = make_stencil(zero_cover, zero_cover);
    const auto fringe_stencil = make_stencil(equal_keep, equal_keep);

    PipelineOptions color_options{};
    color_options.blend = &premultiplied;
    PipelineOptions stencil_options{};
    stencil_options.stencil = &fill_stencil;
    stencil_options.set_color_mask = true;
    stencil_options.color_mask = NKGPU_COLORMASK_NONE;
    PipelineOptions even_odd_options = stencil_options;
    even_odd_options.stencil = &even_odd_stencil;
    PipelineOptions cover_options{};
    cover_options.blend = &premultiplied;
    cover_options.stencil = &cover_stencil;
    PipelineOptions fringe_options{};
    fringe_options.blend = &premultiplied;
    fringe_options.stencil = &fringe_stencil;
    PipelineOptions glyph_options{};
    glyph_options.blend = &glyph_blend;
    PipelineOptions surface_options{};
    surface_options.depth_stencil = true;
    surface_options.cull = true;
    surface_options.cull_mode = NKGPU_CULLMODE_BACK;
    surface_options.winding = NKGPU_FACEWINDING_CCW;

    if (!create_pipeline(*state_, state_->solid_shader, sizeof(SolidVertex),
                         {{0, offsetof(SolidVertex, x), NKGPU_VERTEXFORMAT_FLOAT2}}, color_options,
                         state_->solid_pipeline) ||
        !create_pipeline(*state_, state_->solid_shader, sizeof(SolidVertex),
                         {{0, offsetof(SolidVertex, x), NKGPU_VERTEXFORMAT_FLOAT2}},
                         stencil_options, state_->fill_stencil_pipeline) ||
        !create_pipeline(*state_, state_->solid_shader, sizeof(SolidVertex),
                         {{0, offsetof(SolidVertex, x), NKGPU_VERTEXFORMAT_FLOAT2}},
                         even_odd_options, state_->fill_stencil_even_odd_pipeline) ||
        !create_pipeline(*state_, state_->solid_shader, sizeof(SolidVertex),
                         {{0, offsetof(SolidVertex, x), NKGPU_VERTEXFORMAT_FLOAT2}}, cover_options,
                         state_->fill_cover_pipeline) ||
        !create_pipeline(*state_, state_->path_shader, sizeof(PathVertex),
                         {{0, offsetof(PathVertex, x), NKGPU_VERTEXFORMAT_FLOAT2},
                          {1, offsetof(PathVertex, u), NKGPU_VERTEXFORMAT_FLOAT2}},
                         color_options, state_->path_pipeline) ||
        !create_pipeline(*state_, state_->path_shader, sizeof(PathVertex),
                         {{0, offsetof(PathVertex, x), NKGPU_VERTEXFORMAT_FLOAT2},
                          {1, offsetof(PathVertex, u), NKGPU_VERTEXFORMAT_FLOAT2}},
                         cover_options, state_->path_cover_pipeline) ||
        !create_pipeline(*state_, state_->path_shader, sizeof(PathVertex),
                         {{0, offsetof(PathVertex, x), NKGPU_VERTEXFORMAT_FLOAT2},
                          {1, offsetof(PathVertex, u), NKGPU_VERTEXFORMAT_FLOAT2}},
                         fringe_options, state_->path_fringe_pipeline) ||
        !create_pipeline(*state_, state_->alpha_glyph_shader, sizeof(GlyphVertex),
                         {{0, offsetof(GlyphVertex, x), NKGPU_VERTEXFORMAT_FLOAT2},
                          {1, offsetof(GlyphVertex, u), NKGPU_VERTEXFORMAT_FLOAT2},
                          {2, offsetof(GlyphVertex, red), NKGPU_VERTEXFORMAT_UBYTE4N}},
                         glyph_options, state_->alpha_glyph_pipeline) ||
        !create_pipeline(*state_, state_->sdf_glyph_shader, sizeof(GlyphVertex),
                         {{0, offsetof(GlyphVertex, x), NKGPU_VERTEXFORMAT_FLOAT2},
                          {1, offsetof(GlyphVertex, u), NKGPU_VERTEXFORMAT_FLOAT2},
                          {2, offsetof(GlyphVertex, red), NKGPU_VERTEXFORMAT_UBYTE4N}},
                         glyph_options, state_->sdf_glyph_pipeline) ||
        !create_pipeline(*state_, state_->color_glyph_shader, sizeof(GlyphVertex),
                         {{0, offsetof(GlyphVertex, x), NKGPU_VERTEXFORMAT_FLOAT2},
                          {1, offsetof(GlyphVertex, u), NKGPU_VERTEXFORMAT_FLOAT2},
                          {2, offsetof(GlyphVertex, red), NKGPU_VERTEXFORMAT_UBYTE4N}},
                         glyph_options, state_->color_glyph_pipeline) ||
        !create_pipeline(*state_, state_->composite_shader, sizeof(TextureVertex),
                         {{0, offsetof(TextureVertex, x), NKGPU_VERTEXFORMAT_FLOAT2},
                          {1, offsetof(TextureVertex, u), NKGPU_VERTEXFORMAT_FLOAT2}},
                         color_options, state_->composite_pipeline) ||
        !create_pipeline(*state_, state_->effect_shader, sizeof(TextureVertex),
                         {{0, offsetof(TextureVertex, x), NKGPU_VERTEXFORMAT_FLOAT2},
                          {1, offsetof(TextureVertex, u), NKGPU_VERTEXFORMAT_FLOAT2}},
                         color_options, state_->effect_pipeline) ||
        !create_pipeline(*state_, state_->blur_shader, sizeof(TextureVertex),
                         {{0, offsetof(TextureVertex, x), NKGPU_VERTEXFORMAT_FLOAT2},
                          {1, offsetof(TextureVertex, u), NKGPU_VERTEXFORMAT_FLOAT2}},
                         color_options, state_->blur_pipeline) ||
        !create_pipeline(*state_, state_->drop_shadow_shader, sizeof(TextureVertex),
                         {{0, offsetof(TextureVertex, x), NKGPU_VERTEXFORMAT_FLOAT2},
                          {1, offsetof(TextureVertex, u), NKGPU_VERTEXFORMAT_FLOAT2}},
                         color_options, state_->drop_shadow_pipeline) ||
        !create_pipeline(*state_, state_->box_shadow_shader, sizeof(TextureVertex),
                         {{0, offsetof(TextureVertex, x), NKGPU_VERTEXFORMAT_FLOAT2},
                          {1, offsetof(TextureVertex, u), NKGPU_VERTEXFORMAT_FLOAT2}},
                         color_options, state_->box_shadow_pipeline) ||
        !create_pipeline(*state_, state_->mask_shader, sizeof(TextureVertex),
                         {{0, offsetof(TextureVertex, x), NKGPU_VERTEXFORMAT_FLOAT2},
                          {1, offsetof(TextureVertex, u), NKGPU_VERTEXFORMAT_FLOAT2}},
                         color_options, state_->mask_pipeline) ||
        !create_pipeline(*state_, state_->surface_mesh_shader, sizeof(SurfaceMeshVertex),
                         {{0, offsetof(SurfaceMeshVertex, x), NKGPU_VERTEXFORMAT_FLOAT3},
                          {1, offsetof(SurfaceMeshVertex, red), NKGPU_VERTEXFORMAT_UBYTE4N}},
                         surface_options, state_->surface_mesh_pipeline))
        return false;

    const auto nearest = NKGPU_FILTER_NEAREST;
    const auto linear = NKGPU_FILTER_LINEAR;
    const auto clamp = NKGPU_WRAP_CLAMP_TO_EDGE;
    if (!gpu_result(*state_, nkgpu_sampler_create(state_->renderer, nearest, nearest, clamp, clamp,
                                                  &state_->sampler)) ||
        !gpu_result(*state_, nkgpu_sampler_create(state_->renderer, linear, linear, clamp, clamp,
                                                  &state_->glyph_sampler)) ||
        !gpu_result(*state_, nkgpu_sampler_create(state_->renderer, linear, linear, clamp, clamp,
                                                  &state_->surface_sampler)) ||
        !gpu_result(*state_, nkgpu_sampler_create(state_->renderer, nearest, nearest, clamp, clamp,
                                                  &state_->white_sampler)))
        return false;
    const std::array<uint8_t, 4> white{255, 255, 255, 255};
    if (!gpu_result(*state_,
                    nkgpu_image_create(state_->renderer, 1, 1, NKGPU_IMAGEFORMAT_RGBA8,
                                       white.data(), white.size(), 0, &state_->white_image)))
        return false;
    const auto create_stream = [this](uint32_t size, nkgpu_buffer_usage usage,
                                      nkgpu_buffer &buffer) {
        return gpu_result(*state_,
                          nkgpu_buffer_create_stream(state_->renderer, size, usage, &buffer));
    };
    if (!create_stream(4 * 1024 * 1024, NKGPU_BUFFER_VERTEX, state_->solid_vertices) ||
        !create_stream(4 * 1024 * 1024, NKGPU_BUFFER_VERTEX, state_->glyph_vertices) ||
        !create_stream(1024 * 1024, NKGPU_BUFFER_VERTEX, state_->composite_vertices) ||
        !create_stream(1024 * 1024, NKGPU_BUFFER_VERTEX, state_->surface_mesh_vertices) ||
        !create_stream(4 * 1024 * 1024, NKGPU_BUFFER_INDEX, state_->indices))
        return false;
    state_->stats.gpu_resources = 38;
    state_->initialized = true;
    return true;
}

bool UiRendererImpl::valid() const {
    return state_ && state_->initialized && !lost();
}

bool UiRendererImpl::lost() const {
    if (!state_ || !state_->renderer.id)
        return false;
    nkgpu_renderer_state renderer_state = NKGPU_RENDERER_READY;
    return nkgpu_renderer_get_state(state_->renderer, &renderer_state) == NKGPU_OK &&
           renderer_state == NKGPU_RENDERER_LOST;
}

bool UiRendererImpl::beginFrame(bool record, const nk_surface_frame_target *frame_target) {
    if (!valid() || state_->in_frame)
        return fail(*state_, "invalid UI frame state");
    recycle_transient_targets(*state_);
    state_->target_aliases.clear();
    ++state_->frame_serial;
    if (!state_->frame_serial)
        ++state_->frame_serial;
    state_->recording = record;
    state_->has_frame_target = frame_target != nullptr;
    if (frame_target)
        state_->frame_target = *frame_target;
    else
        state_->frame_target = {};
    state_->record_commands.clear();
    if (record) {
        if (!gpu_result(*state_, nkgpu_batch_begin(state_->renderer, &state_->batch))) {
            state_->recording = false;
            return false;
        }
        ++state_->stats.recorded_frames;
    } else if (!gpu_result(*state_, frame_target ? nkgpu_frame_begin_with_target(state_->renderer,
                                                                                 frame_target)
                                                 : nkgpu_frame_begin(state_->renderer))) {
        state_->recording = false;
        return false;
    }
    state_->in_frame = true;
    state_->in_pass = false;
    return true;
}

bool UiRendererImpl::beginWindowPass(int width, int height, bool clear) {
    if (!valid() || !state_->in_frame || state_->in_pass || width <= 0 || height <= 0)
        return fail(*state_, "invalid UI window pass");
    if (state_->recording) {
        nkgpu_batch_pass pass{};
        pass.struct_size = sizeof(pass);
        pass.kind = NKGPU_BATCH_PASS_WINDOW;
        pass.clear = clear ? 1 : 0;
        pass.width = static_cast<uint32_t>(width);
        pass.height = static_cast<uint32_t>(height);
        if (!gpu_result(*state_, nkgpu_batch_append_pass(state_->batch, &pass)))
            return false;
    } else if (!gpu_result(*state_,
                           nkgpu_begin_window_pass(state_->renderer, static_cast<uint32_t>(width),
                                                   static_cast<uint32_t>(height), clear ? 1 : 0))) {
        return false;
    }
    state_->width = width;
    state_->height = height;
    state_->in_pass = true;
    ++state_->stats.passes;
    return true;
}

bool UiRendererImpl::beginTargetPass(ResourceId target_id, int width, int height,
                                     bool load_existing) {
    if (!valid() || !state_->in_frame || state_->in_pass ||
        !is_resource_id(target_id, ResourceKind::RenderTarget) || width <= 0 || height <= 0)
        return fail(*state_, "invalid UI render-target pass");
    const bool transient = is_transient_target(target_id);
    auto found = state_->targets.find(target_id.value);
    if (found != state_->targets.end() &&
        (found->second.width != width || found->second.height != height)) {
        if (transient) {
            recycle_transient_target(*state_, std::move(found->second));
            state_->targets.erase(found);
        } else {
            destroy_target(*state_, found->second);
        }
    }
    auto &target = state_->targets[target_id.value];
    if (!target.color.id && !(transient ? acquire_transient_target(*state_, target, width, height)
                                        : create_target(*state_, target, width, height)))
        return false;
    if (target.width != width || target.height != height) {
        if (transient) {
            recycle_transient_target(*state_, std::move(target));
            state_->targets.erase(target_id.value);
            auto &replacement = state_->targets[target_id.value];
            if (!acquire_transient_target(*state_, replacement, width, height))
                return false;
        } else {
            return fail(*state_, "render-target dimensions changed unexpectedly");
        }
    }
    return begin_target_pass(*state_, target, load_existing);
}

bool UiRendererImpl::beginEffectPass(ResourceId target_id, uint64_t cache_key, int width,
                                     int height, bool &cache_hit) {
    cache_hit = false;
    if (!valid() || !state_->in_frame || state_->in_pass ||
        !is_resource_id(target_id, ResourceKind::RenderTarget) || width <= 0 || height <= 0)
        return fail(*state_, "invalid UI effect pass");
    if (!cache_key)
        return beginTargetPass(target_id, width, height, false);

    auto found = state_->effect_cache.find(cache_key);
    if (found != state_->effect_cache.end() &&
        (found->second.target.width != width || found->second.target.height != height ||
         !found->second.target.color.id || !found->second.target.image.id)) {
        destroy_cached_effect(*state_, found->second);
        state_->effect_cache.erase(found);
        found = state_->effect_cache.end();
    }
    if (found != state_->effect_cache.end()) {
        found->second.last_used_frame = state_->frame_serial;
        state_->target_aliases[target_id.value] = {false, cache_key};
        ++state_->effect_cache_hits;
        cache_hit = true;
        return true;
    }
    ++state_->effect_cache_misses;

    if (!make_effect_cache_room(*state_, width, height))
        return beginTargetPass(target_id, width, height, false);

    {
        const auto inserted = state_->effect_cache.try_emplace(cache_key);
        if (!inserted.second)
            return fail(*state_, "effect cache insertion failed");
        auto &entry = inserted.first->second;
        if (!create_target(*state_, entry.target, width, height)) {
            state_->effect_cache.erase(inserted.first);
            return false;
        }
        entry.last_used_frame = state_->frame_serial;
        state_->target_aliases[target_id.value] = {false, cache_key};
        if (!begin_target_pass(*state_, entry.target, false)) {
            destroy_cached_effect(*state_, entry);
            state_->effect_cache.erase(inserted.first);
            state_->target_aliases.erase(target_id.value);
            return false;
        }
    }
    return true;
}

bool UiRendererImpl::beginRasterPass(ResourceId target_id, uint64_t cache_key, int width,
                                     int height, bool &cache_hit) {
    cache_hit = false;
    if (!valid() || !state_->in_frame || state_->in_pass ||
        !is_resource_id(target_id, ResourceKind::RenderTarget) || width <= 0 || height <= 0)
        return fail(*state_, "invalid UI raster pass");
    if (!cache_key)
        return beginTargetPass(target_id, width, height, false);

    auto found = state_->raster_cache.find(cache_key);
    if (found != state_->raster_cache.end() &&
        (found->second.target.width != width || found->second.target.height != height ||
         !found->second.target.color.id || !found->second.target.image.id)) {
        destroy_cached_raster(*state_, found->second);
        state_->raster_cache.erase(found);
        found = state_->raster_cache.end();
    }
    if (found != state_->raster_cache.end()) {
        found->second.last_used_frame = state_->frame_serial;
        state_->target_aliases[target_id.value] = {true, cache_key};
        ++state_->raster_cache_hits;
        cache_hit = true;
        return true;
    }
    ++state_->raster_cache_misses;
    if (!make_raster_cache_room(*state_, width, height))
        return beginTargetPass(target_id, width, height, false);
    const auto inserted = state_->raster_cache.try_emplace(cache_key);
    if (!inserted.second)
        return fail(*state_, "raster cache insertion failed");
    auto &entry = inserted.first->second;
    if (!create_target(*state_, entry.target, width, height)) {
        state_->raster_cache.erase(inserted.first);
        return false;
    }
    entry.last_used_frame = state_->frame_serial;
    state_->target_aliases[target_id.value] = {true, cache_key};
    if (!begin_target_pass(*state_, entry.target, false)) {
        destroy_cached_raster(*state_, entry);
        state_->raster_cache.erase(inserted.first);
        state_->target_aliases.erase(target_id.value);
        return false;
    }
    return true;
}

bool UiRendererImpl::beginSurfacePass(ResourceId target, const SurfaceDescriptor &description,
                                      bool load_existing) {
    /*
     * A live producer renders through callbacks and cannot be recorded, so the
     * executor keeps frames that use one on the inline path.
     */
    if (state_->recording)
        return fail(*state_, "surface producers cannot be recorded");
    if (description.format != SurfacePixelFormat::Rgba8 || description.width <= 0 ||
        description.height <= 0 ||
        (description.alpha != SurfaceAlphaMode::Opaque &&
         description.alpha != SurfaceAlphaMode::Premultiplied) ||
        (description.filter != SurfaceFilter::Nearest &&
         description.filter != SurfaceFilter::Linear) ||
        description.color_space != SurfaceColorSpace::Linear)
        return fail(*state_, "invalid producer surface descriptor");
    return beginTargetPass(target, description.width, description.height, load_existing);
}

bool UiRendererImpl::drawSurfaceMesh(const SurfaceMeshView &mesh) {
    if (!state_->in_pass || mesh.vertices.empty() || mesh.indices.empty())
        return fail(*state_, "invalid surface mesh draw");
    const std::vector<SurfaceMeshVertex> vertices(mesh.vertices.begin(), mesh.vertices.end());
    const std::vector<uint32_t> indices(mesh.indices.begin(), mesh.indices.end());
    return draw_mesh(*state_, state_->surface_mesh_pipeline, vertices, indices, nullptr, 0, {}, {},
                     state_->surface_mesh_vertices, {}, mesh.model_view_projection.data(),
                     sizeof(mesh.model_view_projection));
}

bool UiRendererImpl::surfaceHasContent(ResourceId target) const {
    const auto found = state_->targets.find(target.value);
    return found != state_->targets.end() && found->second.color.id && found->second.image.id;
}

bool UiRendererImpl::surfaceIsCurrent(ResourceId target, uint32_t generation,
                                      const SurfaceDescriptor &description) const {
    const auto found = state_->surfaces.find(target.value);
    if (found == state_->surfaces.end())
        return false;
    const auto &surface = found->second;
    return surface.generation == generation && surface.width == description.width &&
           surface.height == description.height && surface.format == description.format &&
           surface.alpha == description.alpha && surface.filter == description.filter &&
           surface.color_space == description.color_space;
}

void UiRendererImpl::markSurfaceCurrent(ResourceId target, uint32_t generation,
                                        const SurfaceDescriptor &description) {
    state_->surfaces[target.value] = {
        generation,        description.width,  description.height,     description.format,
        description.alpha, description.filter, description.color_space};
}

bool UiRendererImpl::setScissor(bool enabled, float x, float y, float width, float height) {
    if (!state_->in_pass)
        return fail(*state_, "scissor outside UI pass");
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = 0;
    int32_t bottom = 0;
    if (enabled) {
        const double x_coordinate = static_cast<double>(x);
        const double y_coordinate = static_cast<double>(y);
        const double right_coordinate = x_coordinate + static_cast<double>(width);
        const double bottom_coordinate = y_coordinate + static_cast<double>(height);
        const double min_coordinate = static_cast<double>(std::numeric_limits<int32_t>::min());
        const double max_coordinate = static_cast<double>(std::numeric_limits<int32_t>::max());
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
            !std::isfinite(height) || width < 0.0f || height < 0.0f ||
            x_coordinate < min_coordinate || x_coordinate > max_coordinate ||
            y_coordinate < min_coordinate || y_coordinate > max_coordinate ||
            right_coordinate < min_coordinate || right_coordinate > max_coordinate ||
            bottom_coordinate < min_coordinate || bottom_coordinate > max_coordinate)
            return fail(*state_, "invalid UI scissor rectangle");
        left = static_cast<int32_t>(std::floor(x));
        top = static_cast<int32_t>(std::floor(y));
        right = static_cast<int32_t>(std::ceil(right_coordinate));
        bottom = static_cast<int32_t>(std::ceil(bottom_coordinate));
    }
    return emit_scissor(*state_, enabled ? 1 : 0, left, top,
                        enabled ? std::max(0, right - left) : 0,
                        enabled ? std::max(0, bottom - top) : 0);
}

bool UiRendererImpl::drawPath(const PreparedPathData &path, uint32_t operation_index,
                              float opacity) {
    static const float identity[6] = {1, 0, 0, 1, 0, 0};
    return drawPath(path, operation_index, identity, opacity);
}

bool UiRendererImpl::drawPath(const PreparedPathData &path, uint32_t operation_index,
                              const float transform[6], float opacity) {
    if (!state_->in_pass || !transform || operation_index >= path.operations().size() ||
        !std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f)
        return fail(*state_, "invalid path draw");
    for (int index = 0; index < 6; ++index)
        if (!std::isfinite(transform[index]))
            return fail(*state_, "path transform is not finite");

    const auto &operation = path.operations()[operation_index];
    nkgpu_image paint_image{};
    nkgpu_sampler paint_sampler{};
    PreparedTextureType texture_type = PreparedTextureType::Rgba;
    PreparedImageFlags texture_flags = PreparedImageFlags::None;
    if (!resolve_paint_image(*state_, path, operation.paint.image_token, paint_image, paint_sampler,
                             texture_type, texture_flags))
        return false;
    PathUniforms paint = path_uniforms(operation, transform, opacity, texture_type, texture_flags);
    if (operation.kind == PreparedPathKind::Fill &&
        (operation.path_count != 1 || !path.paths()[operation.path_offset].convex)) {
        const std::array<float, 4> stencil_color{};
        for (uint32_t index = 0; index < operation.path_count; ++index) {
            const auto &range = path.paths()[operation.path_offset + index];
            if (range.fill_count < 3)
                continue;
            SolidMesh fan;
            for (uint32_t vertex = 0; vertex < range.fill_count; ++vertex) {
                const auto &source = path.vertices()[range.fill_offset + vertex];
                fan.vertices.push_back(
                    {source.x * transform[0] + source.y * transform[2] + transform[4],
                     source.x * transform[1] + source.y * transform[3] + transform[5]});
            }
            for (uint32_t vertex = 1; vertex + 1 < range.fill_count; ++vertex)
                fan.indices.insert(fan.indices.end(), {0, vertex, vertex + 1});
            const auto stencil_pipeline = operation.fill_rule == PathFillRule::EvenOdd
                                              ? state_->fill_stencil_even_odd_pipeline
                                              : state_->fill_stencil_pipeline;
            if (!draw_mesh(*state_, stencil_pipeline, fan.vertices, fan.indices,
                           stencil_color.data(), sizeof(stencil_color), {}, {},
                           state_->solid_vertices))
                return false;
        }
        const PathMesh fringe = make_paint_mesh(path, operation, transform, true);
        if (!fringe.indices.empty() &&
            !draw_mesh(*state_, state_->path_fringe_pipeline, fringe.vertices, fringe.indices,
                       &paint, sizeof(paint), paint_image, paint_sampler, state_->solid_vertices))
            return false;
        const auto point = [transform](float x, float y) {
            return PathVertex{x * transform[0] + y * transform[2] + transform[4],
                              x * transform[1] + y * transform[3] + transform[5], 0.5f, 1.0f};
        };
        PathMesh cover;
        cover.vertices = {point(operation.bounds[0], operation.bounds[1]),
                          point(operation.bounds[2], operation.bounds[1]),
                          point(operation.bounds[2], operation.bounds[3]),
                          point(operation.bounds[0], operation.bounds[3])};
        cover.indices = {0, 1, 2, 0, 2, 3};
        paint.coverage[0] = 0.0f;
        return draw_mesh(*state_, state_->path_cover_pipeline, cover.vertices, cover.indices,
                         &paint, sizeof(paint), paint_image, paint_sampler, state_->solid_vertices);
    }

    const PathMesh mesh = make_paint_mesh(path, operation, transform);
    if (mesh.indices.empty())
        return fail(*state_, "empty prepared path");
    return draw_mesh(*state_, state_->path_pipeline, mesh.vertices, mesh.indices, &paint,
                     sizeof(paint), paint_image, paint_sampler, state_->solid_vertices);
}

bool UiRendererImpl::drawImage(const PreparedTexture &image, float x, float y, float width,
                               float height, const float transform[6], float opacity) {
    if (!state_->in_pass || !transform || !std::isfinite(opacity) || opacity < 0.0f ||
        opacity > 1.0f || image.width <= 0 || image.height <= 0 ||
        image.type != PreparedTextureType::Rgba || image.pixels.empty())
        return fail(*state_, "invalid image draw");
    auto &gpu_image = state_->images[image.token];
    if (!upload_texture(*state_, image, gpu_image))
        return false;
    const auto point = [transform](float px, float py, float u, float v) {
        return TextureVertex{px * transform[0] + py * transform[2] + transform[4],
                             px * transform[1] + py * transform[3] + transform[5], u, v};
    };
    const std::vector<TextureVertex> vertices = {
        point(x, y, 0.0f, 0.0f), point(x + width, y, 1.0f, 0.0f),
        point(x + width, y + height, 1.0f, 1.0f), point(x, y + height, 0.0f, 1.0f)};
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    const std::array<float, 4> tint = {opacity, opacity, opacity, opacity};
    return draw_mesh(*state_, state_->composite_pipeline, vertices, indices, tint.data(),
                     sizeof(tint), gpu_image.image, gpu_image.sampler, state_->composite_vertices);
}

bool UiRendererImpl::drawBoxShadow(float x, float y, float width, float height,
                                   const float transform[6], float opacity,
                                   const BoxShadowDescriptor &shadow) {
    if (!state_->in_pass || !transform || !std::isfinite(opacity) || opacity < 0.0f ||
        opacity > 1.0f || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
        !std::isfinite(height) || width <= 0.0f || height <= 0.0f)
        return fail(*state_, "invalid box-shadow geometry");
    for (const float value : shadow.radii)
        if (!std::isfinite(value) || value < 0.0f)
            return fail(*state_, "invalid box-shadow radii");
    for (const float value : shadow.color)
        if (!std::isfinite(value) || value < 0.0f || value > 1.0f)
            return fail(*state_, "invalid box-shadow color");
    if (!std::isfinite(shadow.offset_x) || !std::isfinite(shadow.offset_y) ||
        !std::isfinite(shadow.blur_sigma) || shadow.blur_sigma < 0.0f ||
        !std::isfinite(shadow.spread))
        return fail(*state_, "invalid box-shadow parameters");
    for (int index = 0; index < 6; ++index)
        if (!std::isfinite(transform[index]))
            return fail(*state_, "box-shadow transform is not finite");

    const float shape_x = x + shadow.offset_x - shadow.spread;
    const float shape_y = y + shadow.offset_y - shadow.spread;
    const float shape_width = width + 2.0f * shadow.spread;
    const float shape_height = height + 2.0f * shadow.spread;
    if (!std::isfinite(shape_x) || !std::isfinite(shape_y) || !std::isfinite(shape_width) ||
        !std::isfinite(shape_height))
        return fail(*state_, "box-shadow geometry overflow");
    if (shape_width <= 0.0f || shape_height <= 0.0f)
        return true;
    const float blur_extent = shadow.blur_sigma * 3.0f;
    if (!std::isfinite(blur_extent))
        return fail(*state_, "box-shadow blur is too large");
    const float left = shape_x - blur_extent;
    const float top = shape_y - blur_extent;
    const float right = shape_x + shape_width + blur_extent;
    const float bottom = shape_y + shape_height + blur_extent;
    if (!std::isfinite(left) || !std::isfinite(top) || !std::isfinite(right) ||
        !std::isfinite(bottom))
        return fail(*state_, "box-shadow bounds overflow");
    const auto point = [transform](float px, float py) {
        return TextureVertex{px * transform[0] + py * transform[2] + transform[4],
                             px * transform[1] + py * transform[3] + transform[5], px, py};
    };
    const std::vector<TextureVertex> vertices = {point(left, top), point(right, top),
                                                 point(right, bottom), point(left, bottom)};
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    BoxShadowUniforms uniforms{};
    uniforms.value[0] = {x, y, width, height};
    uniforms.value[1] = {shadow.offset_x, shadow.offset_y, shadow.blur_sigma, shadow.spread};
    uniforms.value[2] = shadow.radii;
    uniforms.value[3] = {shadow.color[0], shadow.color[1], shadow.color[2],
                         shadow.color[3] * opacity};
    return draw_mesh(*state_, state_->box_shadow_pipeline, vertices, indices, &uniforms,
                     sizeof(uniforms), {}, {}, state_->composite_vertices);
}

bool UiRendererImpl::uploadAtlases(TextEngine &engine, bool include_clean) {
    for (const auto &upload : engine.atlas_uploads(include_clean)) {
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
        if (new_generation) {
            State::AtlasImage atlas;
            if (!create_atlas_image(*state_, atlas, upload))
                return false;
            found = state_->atlases.emplace(key, std::move(atlas)).first;
            ++state_->stats.gpu_resources;
            ++state_->stats.atlas_rebuilds;
            if (replacing_generation)
                ++state_->stats.atlas_reallocations;
        } else if (found->second.width != upload.texture_width ||
                   found->second.height != upload.texture_height ||
                   found->second.format != upload.format ||
                   found->second.bytes_per_pixel != upload.bytes_per_pixel) {
            return fail(*state_, "atlas generation changed dimensions");
        }
        const uint64_t dirty_bytes = upload.dirty ? static_cast<uint64_t>(upload.width) *
                                                        upload.height * upload.bytes_per_pixel
                                                  : 0;
        state_->stats.atlas_dirty_bytes += dirty_bytes;
        state_->stats.atlas_dirty_upload_bytes += dirty_bytes;
        if (upload.dirty)
            state_->stats.atlas_dirty_capacity_bytes +=
                static_cast<uint64_t>(upload.texture_width) * upload.texture_height *
                upload.bytes_per_pixel;
        // Dynamic image creation uploads its initial pixels through the GPU
        // contract. Later updates refresh the full CPU mirror before upload so
        // rotating backend storage cannot lose clean glyphs.
        if (!new_generation) {
            if (upload.dirty)
                ++state_->stats.atlas_full_upload_fallbacks;
            copy_atlas_pixels(found->second, upload, true);
            const uint32_t row_pitch =
                static_cast<uint32_t>(found->second.width) * found->second.bytes_per_pixel;
            if (!gpu_result(*state_, nkgpu_image_update(state_->renderer, found->second.image, 0, 0,
                                                        static_cast<uint32_t>(found->second.width),
                                                        static_cast<uint32_t>(found->second.height),
                                                        found->second.pixels.data(), row_pitch)))
                return false;
        }
        ++state_->stats.atlas_full_uploads;
        ++state_->stats.glyph_uploads;
        const uint64_t uploaded_bytes = found->second.pixels.size();
        found->second.generation = upload.generation;
        ++state_->stats.image_uploads;
        state_->stats.uploaded_bytes += uploaded_bytes;
        state_->stats.atlas_uploaded_bytes += uploaded_bytes;
        if (upload.dirty && !engine.acknowledge_atlas_upload(upload.texture, upload.dirty_epoch))
            return fail(*state_, "atlas upload acknowledgement failed");
        if (new_generation)
            retire_atlas_generations(*state_, upload.texture, upload.generation);
    }
    const TextEngineStats current = engine.stats();
    auto &previous = state_->text_stats[&engine];
    state_->stats.text_layout_cache_hits +=
        current.text_layout_cache_hits >= previous.text_layout_cache_hits
            ? current.text_layout_cache_hits - previous.text_layout_cache_hits
            : current.text_layout_cache_hits;
    state_->stats.text_layout_cache_misses +=
        current.text_layout_cache_misses >= previous.text_layout_cache_misses
            ? current.text_layout_cache_misses - previous.text_layout_cache_misses
            : current.text_layout_cache_misses;
    state_->stats.glyphs_rasterized += current.glyphs_rasterized >= previous.glyphs_rasterized
                                           ? current.glyphs_rasterized - previous.glyphs_rasterized
                                           : current.glyphs_rasterized;
    previous = current;
    state_->stats.atlas_scale_generation =
        std::max<uint64_t>(state_->stats.atlas_scale_generation, current.scale_generation);
    return true;
}

bool UiRendererImpl::drawGlyphs(const PreparedGlyphs &glyphs, float opacity) {
    static const float identity[6] = {1, 0, 0, 1, 0, 0};
    return drawGlyphs(glyphs, identity, 0.0f, 0.0f, opacity);
}

bool UiRendererImpl::drawGlyphs(const PreparedGlyphs &glyphs, const float transform[6],
                                float origin_x, float origin_y, float opacity) {
    if (!state_->in_pass || !transform || !std::isfinite(opacity) || opacity < 0.0f ||
        opacity > 1.0f || !std::isfinite(glyphs.pixel_scale) || glyphs.pixel_scale <= 0.0f)
        return fail(*state_, "invalid glyph draw");
    const float integral_scale = std::round(glyphs.pixel_scale);
    // Preserve crisp texel alignment at integer scales; fractional device scales
    // need coverage interpolation so glyph edges do not lose partial rows/columns.
    const nkgpu_sampler glyph_sampler = std::abs(glyphs.pixel_scale - integral_scale) < 0.0001f
                                            ? state_->sampler
                                            : state_->glyph_sampler;
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
        const nkgpu_pipeline pipeline =
            batch.mode == GlyphMode::Color ? state_->color_glyph_pipeline
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
        if (!draw_mesh(*state_, pipeline, vertices, indices, nullptr, 0, atlas->second.image,
                       glyph_sampler, state_->glyph_vertices))
            return false;
    }
    return true;
}

namespace {

std::vector<TextureVertex> composite_vertices(float x, float y, float width, float height,
                                              const float transform[6]) {
    const auto point = [transform](float px, float py, float u, float v) {
        return TextureVertex{px * transform[0] + py * transform[2] + transform[4],
                             px * transform[1] + py * transform[3] + transform[5], u, v};
    };
    return {point(x, y, 0.0f, 1.0f), point(x + width, y, 1.0f, 1.0f),
            point(x + width, y + height, 1.0f, 0.0f), point(x, y + height, 0.0f, 0.0f)};
}

bool valid_composite(UiRendererImpl::State &state, const float transform[6], float opacity) {
    if (!state.in_pass || !transform || !std::isfinite(opacity) || opacity < 0.0f || opacity > 1.0f)
        return fail(state, "invalid image composite");
    for (int index = 0; index < 6; ++index)
        if (!std::isfinite(transform[index]))
            return fail(state, "image transform is not finite");
    return true;
}

bool draw_composite(UiRendererImpl::State &state, float x, float y, float width, float height,
                    const float transform[6], float opacity, nkgpu_image image,
                    nk_graphics_image external_image, nkgpu_sampler sampler) {
    const auto vertices = composite_vertices(x, y, width, height, transform);
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    const std::array<float, 4> tint = {opacity, opacity, opacity, opacity};
    return draw_mesh(state, state.composite_pipeline, vertices, indices, tint.data(), sizeof(tint),
                     image, sampler, state.composite_vertices, external_image);
}

} // namespace

bool UiRendererImpl::applyEffect(ResourceId source, const EffectDescriptor &effect) {
    return applyEffectRegion(source, effect, 0.0f, 0.0f, 0.0f, 0.0f);
}

bool UiRendererImpl::applyEffectRegion(ResourceId source, const EffectDescriptor &effect, float x,
                                       float y, float region_width, float region_height) {
    if (!state_->in_pass ||
        (effect.kind != EffectKind::ColorMatrix && effect.kind != EffectKind::Blur &&
         effect.kind != EffectKind::DropShadow))
        return fail(*state_, "unsupported UI effect");
    for (const float value : effect.color_matrix)
        if (!std::isfinite(value))
            return fail(*state_, "UI effect parameters are not finite");
    if ((effect.kind == EffectKind::Blur || effect.kind == EffectKind::DropShadow) &&
        (effect.color_matrix[0] < 0.0f ||
         (effect.color_matrix[1] != 0.0f && effect.color_matrix[1] != 1.0f)))
        return fail(*state_, "invalid blur effect parameters");
    if (effect.kind == EffectKind::DropShadow)
        for (size_t index = 4; index < 8; ++index)
            if (effect.color_matrix[index] < 0.0f || effect.color_matrix[index] > 1.0f)
                return fail(*state_, "invalid drop-shadow color");
    const auto *found = resolve_target(*state_, source);
    if (!found || !found->image.id)
        return fail(*state_, "effect input target was not rendered");
    const bool has_region = region_width > 0.0f || region_height > 0.0f;
    if (has_region && (region_width <= 0.0f || region_height <= 0.0f || x < 0.0f || y < 0.0f ||
                       x + region_width > static_cast<float>(found->width) + 0.01f ||
                       y + region_height > static_cast<float>(found->height) + 0.01f))
        return fail(*state_, "backdrop source rectangle is outside its target");
    if (!setScissor(false, 0.0f, 0.0f, 0.0f, 0.0f))
        return false;

    const float width = static_cast<float>(state_->width);
    const float height = static_cast<float>(state_->height);
    const float source_width = static_cast<float>(found->width);
    const float source_height = static_cast<float>(found->height);
    const float u0 = has_region ? x / source_width : 0.0f;
    const float u1 = has_region ? (x + region_width) / source_width : 1.0f;
    const float v1 = has_region ? 1.0f - y / source_height : 1.0f;
    const float v0 = has_region ? 1.0f - (y + region_height) / source_height : 0.0f;
    const std::vector<TextureVertex> vertices = {
        {0.0f, 0.0f, u0, v1},
        {width, 0.0f, u1, v1},
        {width, height, u1, v0},
        {0.0f, height, u0, v0},
    };
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    if (effect.kind == EffectKind::Blur) {
        const BlurUniforms uniforms{{effect.color_matrix[0], effect.color_matrix[1],
                                     1.0f / source_width, 1.0f / source_height}};
        return draw_mesh(*state_, state_->blur_pipeline, vertices, indices, &uniforms,
                         sizeof(uniforms), {}, state_->surface_sampler, state_->composite_vertices,
                         found->image);
    }
    if (effect.kind == EffectKind::DropShadow) {
        const DropShadowUniforms uniforms{{effect.color_matrix[0], effect.color_matrix[1],
                                           effect.color_matrix[2], effect.color_matrix[3],
                                           effect.color_matrix[4], effect.color_matrix[5],
                                           effect.color_matrix[6], effect.color_matrix[7],
                                           1.0f / source_width, 1.0f / source_height, 0.0f, 0.0f}};
        return draw_mesh(*state_, state_->drop_shadow_pipeline, vertices, indices, &uniforms,
                         sizeof(uniforms), {}, state_->surface_sampler, state_->composite_vertices,
                         found->image);
    }
    ColorMatrixUniforms uniforms{};
    for (uint32_t row = 0; row < 4; ++row) {
        for (uint32_t column = 0; column < 4; ++column)
            uniforms.rows[row][column] = effect.color_matrix[row * 5 + column];
        uniforms.rows[4][row] = effect.color_matrix[row * 5 + 4];
    }
    return draw_mesh(*state_, state_->effect_pipeline, vertices, indices, &uniforms,
                     sizeof(uniforms), {}, state_->sampler, state_->composite_vertices,
                     found->image);
}

bool UiRendererImpl::applyCustomEffect(ResourceId source, const CustomEffectDescriptor &effect) {
    return applyCustomEffectRegion(source, effect, 0.0f, 0.0f, 0.0f, 0.0f);
}

bool UiRendererImpl::applyCustomEffectRegion(ResourceId source,
                                             const CustomEffectDescriptor &effect, float x, float y,
                                             float region_width, float region_height) {
    if (!state_->in_pass || !effect.registration_id ||
        effect.parameter_count > kCustomEffectParameterComponents || effect.pass_count != 1 ||
        effect.sampling_inputs != 1)
        return fail(*state_, "invalid custom UI effect");
    for (const float value : effect.ink_overflow)
        if (!std::isfinite(value) || value < 0.0f)
            return fail(*state_, "custom UI effect overflow is invalid");
    for (const float value : effect.parameters)
        if (!std::isfinite(value))
            return fail(*state_, "custom UI effect parameters are not finite");
    const auto registered = state_->custom_effects.find(effect.registration_id);
    if (registered == state_->custom_effects.end())
        return fail(*state_, "custom UI effect registration is unavailable");
    const auto &implementation = registered->second;
    if (effect.parameter_count != implementation.parameter_components ||
        effect.pass_count != implementation.pass_count ||
        effect.sampling_inputs != implementation.sampling_inputs)
        return fail(*state_, "custom UI effect descriptor does not match its registration");
    for (size_t index = 0; index < implementation.ink_overflow.size(); ++index)
        if (effect.ink_overflow[index] != implementation.ink_overflow[index])
            return fail(*state_, "custom UI effect overflow does not match its registration");
    const auto *found = resolve_target(*state_, source);
    if (!found || !found->image.id)
        return fail(*state_, "custom effect input target was not rendered");
    const bool has_region = region_width > 0.0f || region_height > 0.0f;
    if (has_region && (region_width <= 0.0f || region_height <= 0.0f || x < 0.0f || y < 0.0f ||
                       x + region_width > static_cast<float>(found->width) + 0.01f ||
                       y + region_height > static_cast<float>(found->height) + 0.01f))
        return fail(*state_, "custom effect source rectangle is outside its target");
    if (!setScissor(false, 0.0f, 0.0f, 0.0f, 0.0f))
        return false;
    const float width = static_cast<float>(state_->width);
    const float height = static_cast<float>(state_->height);
    const float source_width = static_cast<float>(found->width);
    const float source_height = static_cast<float>(found->height);
    const float u0 = has_region ? x / source_width : 0.0f;
    const float u1 = has_region ? (x + region_width) / source_width : 1.0f;
    const float v1 = has_region ? 1.0f - y / source_height : 1.0f;
    const float v0 = has_region ? 1.0f - (y + region_height) / source_height : 0.0f;
    const std::vector<TextureVertex> vertices = {{0.0f, 0.0f, u0, v1},
                                                 {width, 0.0f, u1, v1},
                                                 {width, height, u1, v0},
                                                 {0.0f, height, u0, v0}};
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    return draw_mesh(*state_, implementation.pipeline, vertices, indices, effect.parameters.data(),
                     sizeof(effect.parameters), {}, state_->surface_sampler,
                     state_->composite_vertices, found->image);
}

bool UiRendererImpl::registerCustomEffect(const CustomEffectRegistration &registration) {
    if (!state_->initialized)
        return fail(*state_, "custom effects require an initialized UI renderer");
    if (!registration.registration_id || !registration.name ||
        state_->custom_effects.find(registration.registration_id) != state_->custom_effects.end())
        return fail(*state_, "custom effect registration ID is invalid or already registered");
    State::CustomEffect implementation{};
    implementation.registration_id = registration.registration_id;
    implementation.parameter_components = registration.parameter_components;
    implementation.pass_count = registration.pass_count;
    implementation.sampling_inputs = registration.sampling_inputs;
    implementation.ink_overflow = registration.ink_overflow;
    if (!create_custom_effect(*state_, registration, implementation.shader,
                              implementation.pipeline))
        return false;
    state_->custom_effects.emplace(registration.registration_id, implementation);
    return true;
}

bool UiRendererImpl::applyMask(ResourceId source, const MaskDescriptor &mask,
                               const PreparedTexture *image) {
    if (!state_->in_pass || mask.kind < MaskKind::Rectangle || mask.kind > MaskKind::Image)
        return fail(*state_, "unsupported UI mask");
    for (const float value : mask.values)
        if (!std::isfinite(value))
            return fail(*state_, "UI mask parameters are not finite");
    if ((mask.kind == MaskKind::RoundedRect || mask.kind == MaskKind::Circle) &&
        mask.values[0] < 0.0f)
        return fail(*state_, "invalid UI mask radius");
    if (mask.kind == MaskKind::LinearGradient && (mask.values[4] < 0.0f || mask.values[4] > 1.0f ||
                                                  mask.values[5] < 0.0f || mask.values[5] > 1.0f))
        return fail(*state_, "invalid UI mask gradient");
    if ((mask.kind == MaskKind::Image) != (image != nullptr))
        return fail(*state_, "UI mask image input is invalid");
    const auto *found = resolve_target(*state_, source);
    if (!found || !found->image.id)
        return fail(*state_, "mask input target was not rendered");
    if (!setScissor(false, 0.0f, 0.0f, 0.0f, 0.0f))
        return false;

    const float width = static_cast<float>(state_->width);
    const float height = static_cast<float>(state_->height);
    const std::vector<TextureVertex> vertices = {
        {0.0f, 0.0f, 0.0f, 1.0f},
        {width, 0.0f, 1.0f, 1.0f},
        {width, height, 1.0f, 0.0f},
        {0.0f, height, 0.0f, 0.0f},
    };
    const std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    MaskUniforms uniforms{};
    uniforms.value[0] = {static_cast<float>(static_cast<uint32_t>(mask.kind)), mask.values[0],
                         mask.values[0], mask.values[1]};
    uniforms.value[1] = {mask.values[2], mask.values[3], mask.values[4], mask.values[5]};
    uniforms.value[2] = {width, height, 1.0f / width, 1.0f / height};

    nkgpu_image mask_gpu_image = state_->white_image;
    nkgpu_sampler mask_gpu_sampler = state_->white_sampler;
    if (image) {
        auto &prepared = state_->images[image->token];
        if (!upload_texture(*state_, *image, prepared))
            return false;
        mask_gpu_image = prepared.image;
        mask_gpu_sampler = prepared.sampler;
    }
    return draw_mesh(*state_, state_->mask_pipeline, vertices, indices, &uniforms, sizeof(uniforms),
                     {}, state_->surface_sampler, state_->composite_vertices, found->image, nullptr,
                     0, mask_gpu_image, mask_gpu_sampler);
}

bool UiRendererImpl::compositeImage(ResourceId target_id, float x, float y, float width,
                                    float height, const float transform[6], float opacity) {
    if (!valid_composite(*state_, transform, opacity))
        return false;
    const auto *found = resolve_target(*state_, target_id);
    if (!found || !found->image.id)
        return fail(*state_, "target was not rendered");
    if (width <= 0.0f)
        width = static_cast<float>(found->width);
    if (height <= 0.0f)
        height = static_cast<float>(found->height);
    nkgpu_sampler sampler = state_->sampler;
    const auto surface = state_->surfaces.find(target_id.value);
    if (surface != state_->surfaces.end()) {
        if (surface->second.filter == SurfaceFilter::Linear)
            sampler = state_->surface_sampler;
        else if (surface->second.filter != SurfaceFilter::Nearest)
            return fail(*state_, "surface filter is unsupported");
    }
    return draw_composite(*state_, x, y, width, height, transform, opacity, {}, found->image,
                          sampler);
}

bool UiRendererImpl::compositeImage(nk_graphics_image image, float x, float y, float width,
                                    float height, const float transform[6], float opacity) {
    if (!valid_composite(*state_, transform, opacity) || !image.id)
        return fail(*state_, "invalid graphics-image composite");
    return draw_composite(*state_, x, y, width, height, transform, opacity, {}, image,
                          state_->surface_sampler);
}

bool UiRendererImpl::endPass() {
    if (!state_->in_pass)
        return fail(*state_, "no UI pass to end");
    if (state_->recording) {
        /* The recorded pass is validated and retained when it is closed. */
        if (!state_->record_commands.empty()) {
            const bool appended = gpu_result(
                *state_,
                nkgpu_batch_append_command(state_->batch, state_->record_commands.data(),
                                           static_cast<uint32_t>(state_->record_commands.size())));
            state_->record_commands.clear();
            if (!appended)
                return false;
        }
    } else if (!gpu_result(*state_, nkgpu_end_pass(state_->renderer))) {
        return false;
    }
    state_->in_pass = false;
    return true;
}

bool UiRendererImpl::endFrame() {
    if (!state_ || !state_->in_frame)
        return true;
    if (state_->in_pass && !endPass())
        return false;
    if (state_->recording) {
        const bool sealed = gpu_result(*state_, nkgpu_batch_seal(state_->batch));
        const bool submitted =
            sealed &&
            gpu_result(*state_, nkgpu_batch_submit(state_->renderer, state_->batch,
                                                   state_->has_frame_target ? &state_->frame_target
                                                                            : nullptr));
        /* The batch is always released, including after a failed submission. */
        nkgpu_batch_destroy(state_->batch);
        state_->batch = {};
        state_->record_commands.clear();
        state_->recording = false;
        state_->has_frame_target = false;
        state_->in_frame = false;
        return sealed && submitted;
    }
    if (!gpu_result(*state_, nkgpu_end_frame_deferred_present(state_->renderer)))
        return false;
    state_->in_frame = false;
    state_->has_frame_target = false;
    return true;
}

UiRendererStats UiRendererImpl::stats() const {
    if (!state_)
        return {};
    UiRendererStats stats = state_->stats;
    stats.atlas_pages = state_->atlases.size();
    stats.atlas_bytes = 0;
    for (const auto &[key, atlas] : state_->atlases) {
        (void)key;
        stats.atlas_bytes += atlas.pixels.size();
    }
    stats.transient_target_pool_hits = state_->transient_target_pool_hits;
    stats.transient_target_pool_misses = state_->transient_target_pool_misses;
    stats.transient_target_pool_count = pooled_transient_target_count(*state_);
    stats.transient_target_pool_bytes = pooled_transient_target_bytes(*state_);
    stats.effect_cache_hits = state_->effect_cache_hits;
    stats.effect_cache_misses = state_->effect_cache_misses;
    stats.effect_cache_entries = state_->effect_cache.size();
    stats.effect_cache_bytes = cached_effect_bytes(*state_);
    stats.raster_cache_hits = state_->raster_cache_hits;
    stats.raster_cache_misses = state_->raster_cache_misses;
    stats.raster_cache_entries = state_->raster_cache.size();
    stats.raster_cache_bytes = cached_raster_bytes(*state_);
    if (state_->renderer.id) {
        nkgpu_renderer_stats gpu{};
        if (nkgpu_renderer_get_stats(state_->renderer, &gpu) == NKGPU_OK) {
            stats.gpu = {gpu.frames,
                         gpu.passes,
                         gpu.draw_calls,
                         gpu.buffers_live,
                         gpu.images_live,
                         gpu.samplers_live,
                         gpu.shaders_live,
                         gpu.pipelines_live,
                         gpu.render_targets_live,
                         gpu.buffer_bytes,
                         gpu.image_bytes,
                         gpu.render_target_bytes,
                         gpu.upload_bytes,
                         gpu.resource_creations,
                         gpu.resource_destructions,
                         gpu.surface_recreations,
                         gpu.device_losses,
                         gpu.failed_allocations};
        }
    }
    return stats;
}

const char *UiRendererImpl::lastError() const {
    return state_ ? state_->error.c_str() : "UI renderer is unavailable";
}

std::unique_ptr<UiRenderer> create_ui_renderer(nk_surface surface) {
    if (!surface)
        return nullptr;
    return std::make_unique<UiRendererImpl>(surface);
}

std::unique_ptr<UiRenderer> create_ui_renderer(nk_surface surface,
                                               const nk_surface_frame_target *frame_target) {
    if (!surface)
        return nullptr;
    return std::make_unique<UiRendererImpl>(surface, frame_target);
}

} // namespace nkui
