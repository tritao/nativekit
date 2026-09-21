#include "nativekit_scene_render.h"
#include "scene_shader_sources.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nkscene {

namespace {

struct SceneVertex {
    std::array<float, 3> position{};
    std::array<float, 3> normal{0.0f, 0.0f, 1.0f};
    std::array<float, 2> texcoord{};
    std::array<float, 4> color{1.0f, 1.0f, 1.0f, 1.0f};
};

constexpr std::uint32_t vertex_stride = sizeof(SceneVertex);
constexpr std::uint32_t instance_stride = sizeof(float) * 20;

static_assert(sizeof(SceneVertex) == sizeof(float) * 12);

struct InstanceData {
    std::array<float, 16> transform;
    std::array<float, 4> pick_color;
};

static_assert(sizeof(InstanceData) == instance_stride);

struct ClipUniformData {
    std::array<std::array<float, 4>, RenderPlan::max_clip_planes> planes{};
    std::array<float, 4> count{};
};

struct MaterialUniformData {
    std::array<float, 4> base_color{};
    std::array<float, 4> surface_params{};
    std::array<float, 4> emissive{};
};

static_assert(sizeof(MaterialUniformData) == sizeof(float) * 12);

static_assert(sizeof(ClipUniformData) ==
              RenderPlan::max_clip_planes * sizeof(float) * 4 + sizeof(float) * 4);

struct BatchKey {
    GeometryId geometry;
    MaterialId material;

    friend bool operator==(const BatchKey &, const BatchKey &) = default;
};

struct DesiredBatch {
    BatchKey key;
    std::vector<OccurrenceId> instances;
    std::vector<std::uint64_t> transform_revisions;
    std::vector<std::array<float, 16>> transforms;
    std::vector<std::uint32_t> pick_ids;
};

std::array<float, 4> encode_pick_id(std::uint32_t id) {
    return {static_cast<float>(id & 0xffu) / 255.0f, static_cast<float>((id >> 8) & 0xffu) / 255.0f,
            static_cast<float>((id >> 16) & 0xffu) / 255.0f, 1.0f};
}

std::uint32_t decode_pick_id(const std::array<std::uint8_t, 4> &pixel) {
    return static_cast<std::uint32_t>(pixel[0]) | (static_cast<std::uint32_t>(pixel[1]) << 8) |
           (static_cast<std::uint32_t>(pixel[2]) << 16);
}

std::size_t vertex_format_size(VertexFormat format) noexcept {
    switch (format) {
    case VertexFormat::Float32x2:
        return sizeof(float) * 2;
    case VertexFormat::Float32x3:
        return sizeof(float) * 3;
    case VertexFormat::Float32x4:
        return sizeof(float) * 4;
    case VertexFormat::Unorm8x4:
    case VertexFormat::Snorm8x4:
        return 4;
    }
    return 0;
}

bool decode_vertex_stream(const GeometryVertexStream &stream, std::size_t index,
                          std::array<float, 4> &out) noexcept {
    const auto element_size = vertex_format_size(stream.format);
    const auto stride = stream.stride ? stream.stride : element_size;
    if (!element_size || index >= stream.count || stride < element_size ||
        index > (std::numeric_limits<std::size_t>::max() - element_size) / stride)
        return false;
    const auto offset = index * stride;
    if (offset + element_size > stream.data.size())
        return false;
    const auto *source = reinterpret_cast<const std::uint8_t *>(stream.data.data()) + offset;
    switch (stream.format) {
    case VertexFormat::Float32x2: {
        float values[2];
        std::memcpy(values, source, sizeof(values));
        out = {values[0], values[1], 0.0f, 1.0f};
        return true;
    }
    case VertexFormat::Float32x3: {
        float values[3];
        std::memcpy(values, source, sizeof(values));
        out = {values[0], values[1], values[2], 1.0f};
        return true;
    }
    case VertexFormat::Float32x4:
        std::memcpy(out.data(), source, sizeof(float) * 4);
        return true;
    case VertexFormat::Unorm8x4:
        for (std::size_t component = 0; component < 4; ++component)
            out[component] = static_cast<float>(source[component]) / 255.0f;
        return true;
    case VertexFormat::Snorm8x4:
        for (std::size_t component = 0; component < 4; ++component)
            out[component] = std::max(-1.0f, static_cast<float>(static_cast<std::int8_t>(source[component])) /
                                                127.0f);
        return true;
    }
    return false;
}

bool pack_geometry_vertices(const GeometryResource &resource, std::vector<SceneVertex> &out) {
    out.resize(resource.payload.vertices.size());
    for (std::size_t index = 0; index < out.size(); ++index)
        out[index].position = resource.payload.vertices[index].position;
    for (const auto &stream : resource.payload.streams) {
        if (stream.count != out.size())
            return false;
        for (std::size_t index = 0; index < out.size(); ++index) {
            std::array<float, 4> value{};
            if (!decode_vertex_stream(stream, index, value))
                return false;
            switch (stream.semantic) {
            case VertexSemantic::Position:
                out[index].position = {value[0], value[1], value[2]};
                break;
            case VertexSemantic::Normal:
                out[index].normal = {value[0], value[1], value[2]};
                break;
            case VertexSemantic::Texcoord0:
                out[index].texcoord = {value[0], value[1]};
                break;
            case VertexSemantic::Color0:
                out[index].color = value;
                break;
            case VertexSemantic::Tangent:
            case VertexSemantic::Texcoord1:
                break;
            }
        }
    }
    return true;
}

ClipUniformData clip_uniform_data(const RenderPlan &plan) {
    ClipUniformData result;
    const auto planes = plan.clip_planes();
    result.count[0] = static_cast<float>(planes.size());
    for (std::size_t index = 0; index < planes.size(); ++index)
        result.planes[index] = planes[index];
    return result;
}

std::vector<DesiredBatch> desired_batches(const RenderPlan &plan) {
    std::vector<DesiredBatch> result;
    std::unordered_map<OccurrenceId, std::size_t> item_indices;
    item_indices.reserve(plan.items().size());
    for (std::size_t index = 0; index < plan.items().size(); ++index)
        item_indices.emplace(plan.items()[index].occurrence, index);

    for (const auto &batch : plan.batches()) {
        DesiredBatch desired{{batch.geometry, batch.material}};
        const auto transforms = plan.transforms();
        for (const auto occurrence : batch.instances) {
            const auto item_index = item_indices.find(occurrence);
            const auto *item =
                item_index == item_indices.end() ? nullptr : &plan.items()[item_index->second];
            if (!item || has_render_flag(item->flags, RenderFlags::Hidden) ||
                has_render_flag(item->flags, RenderFlags::Culled) ||
                item->transformIndex >= transforms.size())
                continue;
            const auto &transform = transforms[item->transformIndex];
            desired.instances.push_back(occurrence);
            desired.transform_revisions.push_back(transform.revision);
            desired.transforms.push_back(transform.transform.matrix);
            desired.pick_ids.push_back(item->pickId);
        }
        if (!desired.instances.empty())
            result.push_back(std::move(desired));
    }
    return result;
}

} // namespace

struct NativeKitGpuExecutor::State {
    struct GeometryGpu {
        nkgpu_buffer buffer{};
        nkgpu_buffer index_buffer{};
        std::uint64_t revision = 0;
        std::uint32_t byte_size = 0;
        std::uint32_t index_byte_size = 0;
        std::uint32_t vertex_count = 0;
        std::uint32_t index_count = 0;
        bool indexed = false;
    };

    struct MaterialGpu {
        nkgpu_image image{};
        nkgpu_sampler sampler{};
        std::uint64_t material_revision = 0;
        std::uint64_t image_revision = 0;
        std::uint64_t sampler_revision = 0;
        bool owns_image = false;
        bool owns_sampler = false;
    };

    struct BatchGpu {
        BatchKey key;
        nkgpu_buffer buffer{};
        std::vector<OccurrenceId> instances;
        std::vector<std::uint64_t> transform_revisions;
        std::vector<std::uint32_t> pick_ids;
    };

    nkgpu_renderer renderer{};
    nkgpu_shader shader{};
    nkgpu_pipeline pipeline{};
    nkgpu_pipeline indexed_pipeline{};
    nkgpu_shader pick_shader{};
    nkgpu_pipeline pick_pipeline{};
    nkgpu_pipeline pick_indexed_pipeline{};
    nkgpu_image pick_color{};
    nkgpu_image pick_subelement{};
    nkgpu_image pick_depth_value{};
    nkgpu_image pick_depth{};
    nkgpu_image default_image{};
    nkgpu_sampler default_sampler{};
    std::uint32_t pick_width = 0;
    std::uint32_t pick_height = 0;
    std::unordered_map<GeometryId, GeometryGpu> geometry_resources;
    std::unordered_map<MaterialId, std::uint64_t> material_revisions;
    std::unordered_map<MaterialId, MaterialGpu> material_resources;
    std::vector<BatchGpu> batches;
    std::vector<GpuCommand> commands;
    nkgpu_result last_result = NKGPU_OK;

    ~State() { release_gpu(); }

    void release_gpu() noexcept {
        if (!renderer.id)
            return;
        for (auto &[id, resource] : geometry_resources) {
            (void)id;
            if (resource.buffer.id)
                (void)nkgpu_buffer_destroy(renderer, resource.buffer);
            if (resource.index_buffer.id)
                (void)nkgpu_buffer_destroy(renderer, resource.index_buffer);
        }
        for (auto &[id, resource] : material_resources) {
            (void)id;
            if (resource.owns_image && resource.image.id)
                (void)nkgpu_image_destroy(renderer, resource.image);
            if (resource.owns_sampler && resource.sampler.id)
                (void)nkgpu_sampler_destroy(renderer, resource.sampler);
        }
        for (auto &batch : batches) {
            if (batch.buffer.id)
                (void)nkgpu_buffer_destroy(renderer, batch.buffer);
        }
        if (pick_color.id)
            (void)nkgpu_image_destroy(renderer, pick_color);
        if (pick_subelement.id)
            (void)nkgpu_image_destroy(renderer, pick_subelement);
        if (pick_depth_value.id)
            (void)nkgpu_image_destroy(renderer, pick_depth_value);
        if (pick_depth.id)
            (void)nkgpu_image_destroy(renderer, pick_depth);
        if (default_image.id)
            (void)nkgpu_image_destroy(renderer, default_image);
        if (default_sampler.id)
            (void)nkgpu_sampler_destroy(renderer, default_sampler);
        if (pipeline.id)
            (void)nkgpu_pipeline_destroy(renderer, pipeline);
        if (indexed_pipeline.id)
            (void)nkgpu_pipeline_destroy(renderer, indexed_pipeline);
        if (shader.id)
            (void)nkgpu_shader_destroy(renderer, shader);
        if (pick_pipeline.id)
            (void)nkgpu_pipeline_destroy(renderer, pick_pipeline);
        if (pick_indexed_pipeline.id)
            (void)nkgpu_pipeline_destroy(renderer, pick_indexed_pipeline);
        if (pick_shader.id)
            (void)nkgpu_shader_destroy(renderer, pick_shader);
        geometry_resources.clear();
        material_revisions.clear();
        material_resources.clear();
        batches.clear();
        pipeline = {};
        indexed_pipeline = {};
        shader = {};
        pick_pipeline = {};
        pick_indexed_pipeline = {};
        pick_shader = {};
        pick_color = {};
        pick_subelement = {};
        pick_depth_value = {};
        pick_depth = {};
        default_image = {};
        default_sampler = {};
        pick_width = 0;
        pick_height = 0;
    }
};

struct GpuPickRequest::State {
    nkgpu_renderer renderer{};
    nkgpu_readback color_readback{};
    nkgpu_readback subelement_readback{};
    nkgpu_readback depth_readback{};
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint64_t snapshot_revision = 0;
    std::uint64_t plan_source_revision = 0;
    std::uint64_t plan_view_signature = 0;
    std::uint32_t state = NKS_RENDER_PICK_PENDING;
    nkgpu_result error = NKGPU_OK;
    PickResult result;

    ~State() {
        if (!renderer.id)
            return;
        if (color_readback.id)
            (void)nkgpu_readback_destroy(renderer, color_readback);
        if (subelement_readback.id)
            (void)nkgpu_readback_destroy(renderer, subelement_readback);
        if (depth_readback.id)
            (void)nkgpu_readback_destroy(renderer, depth_readback);
    }
};

GpuPickRequest::~GpuPickRequest() = default;
GpuPickRequest::GpuPickRequest(GpuPickRequest &&) noexcept = default;
GpuPickRequest &GpuPickRequest::operator=(GpuPickRequest &&) noexcept = default;

namespace {

bool valid_geometry_payload(const GeometryResource &resource) {
    const auto vertex_count = resource.payload.vertices.size();
    const auto index_count = resource.payload.indices.size();
    return vertex_count <= std::numeric_limits<std::uint32_t>::max() &&
           index_count <= std::numeric_limits<std::uint32_t>::max() &&
           vertex_count <= std::numeric_limits<std::uint32_t>::max() / sizeof(SceneVertex) &&
           index_count % 3 == 0 &&
           (resource.payload.indices.empty()
                ? vertex_count % 3 == 0
                : !std::any_of(resource.payload.indices.begin(), resource.payload.indices.end(),
                               [vertex_count](std::uint32_t index) {
                                   return static_cast<std::size_t>(index) >= vertex_count;
                               }));
}

template <class StateT> void destroy_batch_buffers(StateT &state) noexcept {
    if (!state.renderer.id)
        return;
    for (auto &batch : state.batches) {
        if (batch.buffer.id)
            (void)nkgpu_buffer_destroy(state.renderer, batch.buffer);
    }
    state.batches.clear();
}

template <class StateT>
bool set_failure(StateT &state, GpuExecutionStats &stats, nkgpu_result result) {
    stats.result = result;
    state.last_result = result;
    return false;
}

template <class StateT>
bool ensure_pipeline(StateT &state, GpuExecutionStats &stats, bool indexed) {
    auto &pipeline = indexed ? state.indexed_pipeline : state.pipeline;
    if (pipeline.id)
        return true;

    const auto sources =
        render_internal::scene_shader_sources(nkgpu_query_backend(state.renderer), false);
    if (!sources.vertex || !sources.fragment)
        return set_failure(state, stats, NKGPU_ERROR_UNSUPPORTED);

    nkgpu_result result = NKGPU_OK;
    if (!state.shader.id) {
        nkgpu_shader_builder shader_builder{};
        result = nkgpu_shader_begin(state.renderer, sources.language, sources.vertex,
                                    sources.fragment, &shader_builder);
        if (result != NKGPU_OK)
            return set_failure(state, stats, result);
        const auto attribute = [&](std::uint32_t location, const char *name, const char *semantic) {
            return nkgpu_shader_attribute(shader_builder, location, name, semantic, location);
        };
        if ((result = attribute(0, "position", "POSITION")) != NKGPU_OK ||
            (result = attribute(1, "transform0", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(2, "transform1", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(3, "transform2", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(4, "transform3", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(5, "normal", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(6, "texcoord0", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(7, "color0", "TEXCOORD")) != NKGPU_OK ||
            (result = nkgpu_shader_uniform_block(shader_builder, 1, NKGPU_SHADERSTAGE_VERTEX,
                                                 sizeof(float) * 16)) != NKGPU_OK ||
            (result = nkgpu_shader_uniform(shader_builder, 1, 0, "view_projection",
                                           NKGPU_UNIFORMTYPE_MAT4, 1)) != NKGPU_OK ||
            (result = nkgpu_shader_uniform_block(shader_builder, 0, NKGPU_SHADERSTAGE_FRAGMENT,
                                                 sizeof(float) * 12)) != NKGPU_OK ||
            (result = nkgpu_shader_uniform(shader_builder, 0, 0, "base_color",
                                           NKGPU_UNIFORMTYPE_FLOAT4, 0)) != NKGPU_OK ||
            (result = nkgpu_shader_uniform(shader_builder, 0, 1, "material_params",
                                           NKGPU_UNIFORMTYPE_FLOAT4, 0)) != NKGPU_OK ||
            (result = nkgpu_shader_uniform(shader_builder, 0, 2, "emissive",
                                           NKGPU_UNIFORMTYPE_FLOAT4, 0)) != NKGPU_OK ||
            (result = nkgpu_shader_texture(shader_builder, 0, 0, NKGPU_SHADERSTAGE_FRAGMENT,
                                           "base_color_texture")) != NKGPU_OK ||
            (result = nkgpu_shader_uniform_block(shader_builder, 2, NKGPU_SHADERSTAGE_FRAGMENT,
                                                 sizeof(ClipUniformData))) != NKGPU_OK ||
            (result =
                 nkgpu_shader_uniform(shader_builder, 2, 0, "clip_planes", NKGPU_UNIFORMTYPE_FLOAT4,
                                      RenderPlan::max_clip_planes)) != NKGPU_OK ||
            (result = nkgpu_shader_uniform(shader_builder, 2, 1, "clip_plane_count",
                                           NKGPU_UNIFORMTYPE_FLOAT4, 0)) != NKGPU_OK ||
            (result = nkgpu_shader_end(shader_builder, &state.shader)) != NKGPU_OK)
            return set_failure(state, stats, result);
    }

    nkgpu_pipeline_builder pipeline_builder{};
    if ((result = nkgpu_pipeline_begin(state.renderer, state.shader, vertex_stride,
                                       &pipeline_builder)) != NKGPU_OK)
        return set_failure(state, stats, result);
    if ((result = nkgpu_pipeline_attribute(pipeline_builder, 0, 0, 0, NKGPU_VERTEXFORMAT_FLOAT3)) !=
            NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 1, 1, 0, NKGPU_VERTEXFORMAT_FLOAT4)) !=
            NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 2, 1, sizeof(float) * 4,
                                           NKGPU_VERTEXFORMAT_FLOAT4)) != NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 3, 1, sizeof(float) * 8,
                                           NKGPU_VERTEXFORMAT_FLOAT4)) != NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 4, 1, sizeof(float) * 12,
                                           NKGPU_VERTEXFORMAT_FLOAT4)) != NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 5, 0, sizeof(float) * 3,
                                           NKGPU_VERTEXFORMAT_FLOAT3)) != NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 6, 0, sizeof(float) * 6,
                                           NKGPU_VERTEXFORMAT_FLOAT2)) != NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 7, 0, sizeof(float) * 8,
                                           NKGPU_VERTEXFORMAT_FLOAT4)) != NKGPU_OK ||
        (result = nkgpu_pipeline_vertex_buffer(pipeline_builder, 0, vertex_stride,
                                               NKGPU_VERTEXSTEP_PER_VERTEX, 1)) != NKGPU_OK ||
        (result = nkgpu_pipeline_vertex_buffer(pipeline_builder, 1, instance_stride,
                                               NKGPU_VERTEXSTEP_PER_INSTANCE, 1)) != NKGPU_OK ||
        (result = nkgpu_pipeline_primitive_type(pipeline_builder, NKGPU_PRIMITIVETYPE_TRIANGLES)) !=
            NKGPU_OK ||
        (indexed && (result = nkgpu_pipeline_index_type(pipeline_builder,
                                                        NKGPU_INDEXTYPE_UINT32)) != NKGPU_OK) ||
        (result = nkgpu_pipeline_depth_stencil(pipeline_builder, 1)) != NKGPU_OK ||
        (result = nkgpu_pipeline_end(pipeline_builder, &pipeline)) != NKGPU_OK)
        return set_failure(state, stats, result);
    return true;
}

template <class StateT>
bool ensure_pick_pipeline(StateT &state, GpuExecutionStats &stats, bool indexed) {
    auto &pipeline = indexed ? state.pick_indexed_pipeline : state.pick_pipeline;
    if (pipeline.id)
        return true;

    const auto sources =
        render_internal::scene_shader_sources(nkgpu_query_backend(state.renderer), true);
    if (!sources.vertex || !sources.fragment)
        return set_failure(state, stats, NKGPU_ERROR_UNSUPPORTED);

    nkgpu_result result = NKGPU_OK;
    if (!state.pick_shader.id) {
        nkgpu_shader_builder shader_builder{};
        result = nkgpu_shader_begin(state.renderer, sources.language, sources.vertex,
                                    sources.fragment, &shader_builder);
        if (result != NKGPU_OK)
            return set_failure(state, stats, result);
        const auto attribute = [&](std::uint32_t location, const char *name, const char *semantic) {
            return nkgpu_shader_attribute(shader_builder, location, name, semantic, location);
        };
        if ((result = attribute(0, "position", "POSITION")) != NKGPU_OK ||
            (result = attribute(1, "transform0", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(2, "transform1", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(3, "transform2", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(4, "transform3", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(5, "pick_color", "TEXCOORD")) != NKGPU_OK ||
            (result = nkgpu_shader_uniform_block(shader_builder, 1, NKGPU_SHADERSTAGE_VERTEX,
                                                 sizeof(float) * 16)) != NKGPU_OK ||
            (result = nkgpu_shader_uniform(shader_builder, 1, 0, "view_projection",
                                           NKGPU_UNIFORMTYPE_MAT4, 1)) != NKGPU_OK ||
            (result = nkgpu_shader_uniform_block(shader_builder, 2, NKGPU_SHADERSTAGE_FRAGMENT,
                                                 sizeof(ClipUniformData))) != NKGPU_OK ||
            (result =
                 nkgpu_shader_uniform(shader_builder, 2, 0, "clip_planes", NKGPU_UNIFORMTYPE_FLOAT4,
                                      RenderPlan::max_clip_planes)) != NKGPU_OK ||
            (result = nkgpu_shader_uniform(shader_builder, 2, 1, "clip_plane_count",
                                           NKGPU_UNIFORMTYPE_FLOAT4, 0)) != NKGPU_OK ||
            (result = nkgpu_shader_end(shader_builder, &state.pick_shader)) != NKGPU_OK)
            return set_failure(state, stats, result);
    }

    nkgpu_pipeline_builder pipeline_builder{};
    if ((result = nkgpu_pipeline_begin(state.renderer, state.pick_shader, vertex_stride,
                                       &pipeline_builder)) != NKGPU_OK)
        return set_failure(state, stats, result);
    if ((result = nkgpu_pipeline_attribute(pipeline_builder, 0, 0, 0, NKGPU_VERTEXFORMAT_FLOAT3)) !=
            NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 1, 1, 0, NKGPU_VERTEXFORMAT_FLOAT4)) !=
            NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 2, 1, sizeof(float) * 4,
                                           NKGPU_VERTEXFORMAT_FLOAT4)) != NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 3, 1, sizeof(float) * 8,
                                           NKGPU_VERTEXFORMAT_FLOAT4)) != NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 4, 1, sizeof(float) * 12,
                                           NKGPU_VERTEXFORMAT_FLOAT4)) != NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 5, 1, sizeof(float) * 16,
                                           NKGPU_VERTEXFORMAT_FLOAT4)) != NKGPU_OK ||
        (result = nkgpu_pipeline_vertex_buffer(pipeline_builder, 0, vertex_stride,
                                               NKGPU_VERTEXSTEP_PER_VERTEX, 1)) != NKGPU_OK ||
        (result = nkgpu_pipeline_vertex_buffer(pipeline_builder, 1, instance_stride,
                                               NKGPU_VERTEXSTEP_PER_INSTANCE, 1)) != NKGPU_OK ||
        (result = nkgpu_pipeline_primitive_type(pipeline_builder, NKGPU_PRIMITIVETYPE_TRIANGLES)) !=
            NKGPU_OK ||
        (indexed && (result = nkgpu_pipeline_index_type(pipeline_builder,
                                                        NKGPU_INDEXTYPE_UINT32)) != NKGPU_OK) ||
        (result = nkgpu_pipeline_depth_stencil(pipeline_builder, 1)) != NKGPU_OK ||
        (result = nkgpu_pipeline_color_target(pipeline_builder, 0, NKGPU_IMAGEFORMAT_RGBA8,
                                              NKGPU_COLORMASK_RGBA, nullptr)) != NKGPU_OK ||
        (result = nkgpu_pipeline_color_target(pipeline_builder, 1, NKGPU_IMAGEFORMAT_RGBA8,
                                              NKGPU_COLORMASK_RGBA, nullptr)) != NKGPU_OK ||
        (result = nkgpu_pipeline_color_target(pipeline_builder, 2, NKGPU_IMAGEFORMAT_R32F,
                                              NKGPU_COLORMASK_R, nullptr)) != NKGPU_OK ||
        (result = nkgpu_pipeline_end(pipeline_builder, &pipeline)) != NKGPU_OK)
        return set_failure(state, stats, result);
    return true;
}

template <class StateT>
bool ensure_pick_targets(StateT &state, std::uint32_t width, std::uint32_t height,
                         GpuExecutionStats &stats) {
    if (state.pick_color.id && state.pick_subelement.id && state.pick_depth_value.id &&
        state.pick_width == width && state.pick_height == height)
        return true;
    if (state.pick_color.id)
        (void)nkgpu_image_destroy(state.renderer, state.pick_color);
    if (state.pick_subelement.id)
        (void)nkgpu_image_destroy(state.renderer, state.pick_subelement);
    if (state.pick_depth_value.id)
        (void)nkgpu_image_destroy(state.renderer, state.pick_depth_value);
    if (state.pick_depth.id)
        (void)nkgpu_image_destroy(state.renderer, state.pick_depth);
    state.pick_color = {};
    state.pick_subelement = {};
    state.pick_depth_value = {};
    state.pick_depth = {};

    nkgpu_image_desc color{};
    color.struct_size = sizeof(color);
    color.width = width;
    color.height = height;
    color.format = NKGPU_IMAGEFORMAT_RGBA8;
    color.usage = NKGPU_IMAGE_RENDER_TARGET;
    color.mip_count = 1;
    color.sample_count = 1;
    color.layer_count = 1;
    auto result = nkgpu_image_create_desc(state.renderer, &color, &state.pick_color);
    if (result != NKGPU_OK)
        return set_failure(state, stats, result);

    result = nkgpu_image_create_desc(state.renderer, &color, &state.pick_subelement);
    if (result != NKGPU_OK) {
        (void)nkgpu_image_destroy(state.renderer, state.pick_color);
        state.pick_color = {};
        return set_failure(state, stats, result);
    }

    nkgpu_image_desc depth_value = color;
    depth_value.format = NKGPU_IMAGEFORMAT_R32F;
    result = nkgpu_image_create_desc(state.renderer, &depth_value, &state.pick_depth_value);
    if (result != NKGPU_OK) {
        (void)nkgpu_image_destroy(state.renderer, state.pick_color);
        (void)nkgpu_image_destroy(state.renderer, state.pick_subelement);
        state.pick_color = {};
        state.pick_subelement = {};
        return set_failure(state, stats, result);
    }

    nkgpu_image_desc depth{};
    depth.struct_size = sizeof(depth);
    depth.width = width;
    depth.height = height;
    depth.format = NKGPU_IMAGEFORMAT_DEPTH24_STENCIL8;
    depth.usage = NKGPU_IMAGE_DEPTH_STENCIL;
    depth.mip_count = 1;
    depth.sample_count = 1;
    depth.layer_count = 1;
    result = nkgpu_image_create_desc(state.renderer, &depth, &state.pick_depth);
    if (result != NKGPU_OK) {
        (void)nkgpu_image_destroy(state.renderer, state.pick_color);
        (void)nkgpu_image_destroy(state.renderer, state.pick_subelement);
        (void)nkgpu_image_destroy(state.renderer, state.pick_depth_value);
        state.pick_color = {};
        state.pick_subelement = {};
        state.pick_depth_value = {};
        return set_failure(state, stats, result);
    }
    state.pick_width = width;
    state.pick_height = height;
    return true;
}

template <class StateT>
bool create_instance_buffer(StateT &state, const DesiredBatch &desired,
                            typename StateT::BatchGpu &target, GpuExecutionStats &stats) {
    std::vector<InstanceData> data;
    data.reserve(desired.transforms.size());
    for (std::size_t index = 0; index < desired.transforms.size(); ++index)
        data.push_back({desired.transforms[index], encode_pick_id(desired.pick_ids[index])});
    const auto byte_size = data.size() * sizeof(InstanceData);
    if (byte_size > std::numeric_limits<std::uint32_t>::max())
        return set_failure(state, stats, NKGPU_ERROR_INVALID_ARGUMENT);
    nkgpu_buffer_desc descriptor{};
    descriptor.struct_size = sizeof(descriptor);
    descriptor.size = static_cast<std::uint32_t>(byte_size);
    descriptor.usage = NKGPU_BUFFER_VERTEX;
    descriptor.data = reinterpret_cast<const std::uint8_t *>(data.data());
    descriptor.data_size = descriptor.size;
    descriptor.dynamic_update = 1;
    const auto result = nkgpu_buffer_create_desc(state.renderer, &descriptor, &target.buffer);
    if (result != NKGPU_OK)
        return set_failure(state, stats, result);
    target.key = desired.key;
    target.instances = desired.instances;
    target.transform_revisions = desired.transform_revisions;
    target.pick_ids = desired.pick_ids;
    ++stats.instance_buffers_created;
    return true;
}

template <class StateT>
bool ensure_geometry(StateT &state, const GeometryResource &resource, GpuExecutionStats &stats) {
    const auto vertex_count = resource.payload.vertices.size();
    const auto index_count = resource.payload.indices.size();
    if (!valid_geometry_payload(resource))
        return set_failure(state, stats, NKGPU_ERROR_INVALID_ARGUMENT);
    std::vector<SceneVertex> packed_vertices;
    if (!pack_geometry_vertices(resource, packed_vertices))
        return set_failure(state, stats, NKGPU_ERROR_INVALID_ARGUMENT);

    auto [found, inserted] = state.geometry_resources.try_emplace(resource.id);
    auto &cached = found->second;
    const auto byte_size = packed_vertices.size() * sizeof(SceneVertex);
    const auto index_byte_size = index_count * sizeof(std::uint32_t);
    const bool revision_changed = inserted || cached.revision != resource.revision;
    if (!revision_changed)
        return true;

    if (cached.buffer.id) {
        if (cached.byte_size == byte_size && byte_size != 0) {
            const auto result = nkgpu_buffer_update(
                state.renderer, cached.buffer, 0,
                reinterpret_cast<const std::uint8_t *>(packed_vertices.data()),
                static_cast<std::uint32_t>(byte_size));
            if (result != NKGPU_OK)
                return set_failure(state, stats, result);
        } else {
            (void)nkgpu_buffer_destroy(state.renderer, cached.buffer);
            cached.buffer = {};
            cached.byte_size = 0;
            cached.vertex_count = 0;
        }
    }
    if (byte_size != 0 && !cached.buffer.id) {
        nkgpu_buffer_desc descriptor{};
        descriptor.struct_size = sizeof(descriptor);
        descriptor.size = static_cast<std::uint32_t>(byte_size);
        descriptor.usage = NKGPU_BUFFER_VERTEX;
        descriptor.data = reinterpret_cast<const std::uint8_t *>(packed_vertices.data());
        descriptor.data_size = descriptor.size;
        descriptor.dynamic_update = 1;
        const auto result = nkgpu_buffer_create_desc(state.renderer, &descriptor, &cached.buffer);
        if (result != NKGPU_OK)
            return set_failure(state, stats, result);
    }

    if (cached.index_buffer.id) {
        if (cached.index_byte_size == index_byte_size && index_byte_size != 0) {
            const auto result = nkgpu_buffer_update(
                state.renderer, cached.index_buffer, 0,
                reinterpret_cast<const std::uint8_t *>(resource.payload.indices.data()),
                static_cast<std::uint32_t>(index_byte_size));
            if (result != NKGPU_OK)
                return set_failure(state, stats, result);
        } else {
            (void)nkgpu_buffer_destroy(state.renderer, cached.index_buffer);
            cached.index_buffer = {};
            cached.index_byte_size = 0;
            cached.index_count = 0;
        }
    }
    if (index_byte_size != 0 && !cached.index_buffer.id) {
        nkgpu_buffer_desc descriptor{};
        descriptor.struct_size = sizeof(descriptor);
        descriptor.size = static_cast<std::uint32_t>(index_byte_size);
        descriptor.usage = NKGPU_BUFFER_INDEX;
        descriptor.data = reinterpret_cast<const std::uint8_t *>(resource.payload.indices.data());
        descriptor.data_size = descriptor.size;
        descriptor.dynamic_update = 1;
        const auto result =
            nkgpu_buffer_create_desc(state.renderer, &descriptor, &cached.index_buffer);
        if (result != NKGPU_OK)
            return set_failure(state, stats, result);
    }
    cached.revision = resource.revision;
    cached.byte_size = static_cast<std::uint32_t>(byte_size);
    cached.index_byte_size = static_cast<std::uint32_t>(index_byte_size);
    cached.vertex_count = static_cast<std::uint32_t>(vertex_count);
    cached.index_count = static_cast<std::uint32_t>(index_count);
    cached.indexed = !resource.payload.indices.empty();
    if (inserted)
        ++stats.geometry_resources_created;
    else if (revision_changed)
        ++stats.geometry_resources_updated;
    return true;
}

nkgpu_image_format gpu_image_format(ImageFormat format) noexcept {
    switch (format) {
    case ImageFormat::R8:
        return NKGPU_IMAGEFORMAT_R8;
    case ImageFormat::RGBA8:
        return NKGPU_IMAGEFORMAT_RGBA8;
    case ImageFormat::RGBA16F:
        return NKGPU_IMAGEFORMAT_RGBA16F;
    case ImageFormat::R32F:
        return NKGPU_IMAGEFORMAT_R32F;
    }
    return 0;
}

nkgpu_filter gpu_filter(SamplerFilter filter) noexcept {
    return filter == SamplerFilter::Nearest ? NKGPU_FILTER_NEAREST : NKGPU_FILTER_LINEAR;
}

nkgpu_wrap gpu_wrap(SamplerWrap wrap) noexcept {
    return wrap == SamplerWrap::ClampToEdge ? NKGPU_WRAP_CLAMP_TO_EDGE : NKGPU_WRAP_REPEAT;
}

template <class StateT>
bool ensure_default_material_resources(StateT &state, GpuExecutionStats &stats) {
    if (!state.default_image.id) {
        constexpr std::uint8_t white[] = {255, 255, 255, 255};
        nkgpu_image_desc image{};
        image.struct_size = sizeof(image);
        image.width = 1;
        image.height = 1;
        image.format = NKGPU_IMAGEFORMAT_RGBA8;
        image.usage = NKGPU_IMAGE_SAMPLED;
        image.mip_count = 1;
        image.sample_count = 1;
        image.layer_count = 1;
        image.data = white;
        image.data_size = sizeof(white);
        image.type = NKGPU_IMAGETYPE_2D;
        auto result = nkgpu_image_create_desc(state.renderer, &image, &state.default_image);
        if (result != NKGPU_OK)
            return set_failure(state, stats, result);
    }
    if (!state.default_sampler.id) {
        const auto result = nkgpu_sampler_create(
            state.renderer, NKGPU_FILTER_LINEAR, NKGPU_FILTER_LINEAR, NKGPU_WRAP_REPEAT,
            NKGPU_WRAP_REPEAT, &state.default_sampler);
        if (result != NKGPU_OK)
            return set_failure(state, stats, result);
    }
    return true;
}

template <class StateT>
bool ensure_material(StateT &state, const SceneSnapshot &snapshot, const MaterialResource &resource,
                     GpuExecutionStats &stats) {
    if (!ensure_default_material_resources(state, stats))
        return false;

    const auto *image = static_cast<const ImageResource *>(nullptr);
    const auto *sampler = static_cast<const SamplerResource *>(nullptr);
    if (resource.base_color_texture.valid()) {
        if (const auto *texture = snapshot.find_texture(resource.base_color_texture))
            image = snapshot.find_image(texture->image);
    }
    if (resource.sampler.valid())
        sampler = snapshot.find_sampler(resource.sampler);
    const auto image_revision = image ? image->revision : 0;
    const auto sampler_revision = sampler ? sampler->revision : 0;
    auto [found, inserted] = state.material_resources.try_emplace(resource.id);
    auto &cached = found->second;
    if (!inserted && cached.material_revision == resource.revision &&
        cached.image_revision == image_revision && cached.sampler_revision == sampler_revision)
        return true;

    if (cached.owns_image && cached.image.id)
        (void)nkgpu_image_destroy(state.renderer, cached.image);
    if (cached.owns_sampler && cached.sampler.id)
        (void)nkgpu_sampler_destroy(state.renderer, cached.sampler);
    cached = {};
    cached.material_revision = resource.revision;
    cached.image_revision = image_revision;
    cached.sampler_revision = sampler_revision;

    if (image && gpu_image_format(image->format) != 0) {
        nkgpu_image_desc descriptor{};
        descriptor.struct_size = sizeof(descriptor);
        descriptor.width = image->width;
        descriptor.height = image->height;
        descriptor.format = gpu_image_format(image->format);
        descriptor.usage = NKGPU_IMAGE_SAMPLED;
        descriptor.mip_count = image->mip_count ? image->mip_count : 1;
        descriptor.sample_count = 1;
        descriptor.layer_count = 1;
        descriptor.data = reinterpret_cast<const std::uint8_t *>(image->data.data());
        descriptor.data_size = static_cast<std::uint32_t>(image->data.size());
        descriptor.type = NKGPU_IMAGETYPE_2D;
        const auto result = nkgpu_image_create_desc(state.renderer, &descriptor, &cached.image);
        if (result != NKGPU_OK)
            return set_failure(state, stats, result);
        cached.owns_image = true;
    } else {
        cached.image = state.default_image;
    }

    if (sampler) {
        const auto result = nkgpu_sampler_create(
            state.renderer, gpu_filter(sampler->min_filter), gpu_filter(sampler->mag_filter),
            gpu_wrap(sampler->wrap_u), gpu_wrap(sampler->wrap_v), &cached.sampler);
        if (result != NKGPU_OK)
            return set_failure(state, stats, result);
        cached.owns_sampler = true;
    } else {
        cached.sampler = state.default_sampler;
    }
    return true;
}

template <class StateT>
bool synchronize_batches(StateT &state, const RenderPlan &plan, GpuExecutionStats &stats) {
    const auto desired = desired_batches(plan);
    bool same_layout = desired.size() == state.batches.size();
    if (same_layout) {
        for (std::size_t index = 0; index < desired.size(); ++index) {
            const auto &want = desired[index];
            const auto &have = state.batches[index];
            if (!(want.key == have.key) || want.instances != have.instances ||
                want.pick_ids != have.pick_ids) {
                same_layout = false;
                break;
            }
        }
    }
    if (!same_layout)
        destroy_batch_buffers(state);

    if (!same_layout) {
        state.batches.reserve(desired.size());
        for (const auto &want : desired) {
            typename StateT::BatchGpu target;
            if (!create_instance_buffer(state, want, target, stats))
                return false;
            state.batches.push_back(std::move(target));
        }
        return true;
    }

    for (std::size_t batch_index = 0; batch_index < desired.size(); ++batch_index) {
        const auto &want = desired[batch_index];
        auto &have = state.batches[batch_index];
        for (std::size_t instance = 0; instance < want.transform_revisions.size(); ++instance) {
            if (want.transform_revisions[instance] == have.transform_revisions[instance])
                continue;
            const InstanceData data{want.transforms[instance],
                                    encode_pick_id(want.pick_ids[instance])};
            const auto result = nkgpu_buffer_update(
                state.renderer, have.buffer, static_cast<std::uint32_t>(instance * instance_stride),
                reinterpret_cast<const std::uint8_t *>(&data), instance_stride);
            if (result != NKGPU_OK)
                return set_failure(state, stats, result);
            have.transform_revisions[instance] = want.transform_revisions[instance];
            ++stats.instance_records_updated;
        }
    }
    return true;
}

template <class StateT>
bool prepare_resources(StateT &state, const RenderPlan &plan, const SceneSnapshot &snapshot,
                       GpuExecutionStats &stats) {
    for (const auto &resource : snapshot.geometries()) {
        if (!valid_geometry_payload(resource))
            return set_failure(state, stats, NKGPU_ERROR_INVALID_ARGUMENT);
        const auto found = state.geometry_resources.find(resource.id);
        if (!state.renderer.id) {
            if (found == state.geometry_resources.end()) {
                typename StateT::GeometryGpu cached;
                cached.revision = resource.revision;
                state.geometry_resources.emplace(resource.id, cached);
                ++stats.geometry_resources_created;
            } else if (found->second.revision != resource.revision) {
                found->second.revision = resource.revision;
                ++stats.geometry_resources_updated;
            }
        } else if (!ensure_geometry(state, resource, stats)) {
            return false;
        }
    }
    for (const auto &resource : snapshot.materials()) {
        const auto found = state.material_revisions.find(resource.id);
        if (found == state.material_revisions.end()) {
            state.material_revisions.emplace(resource.id, resource.revision);
            ++stats.material_resources_created;
        } else if (found->second != resource.revision) {
            found->second = resource.revision;
            ++stats.material_resources_updated;
        }
        if (state.renderer.id && !ensure_material(state, snapshot, resource, stats))
            return false;
    }

    for (auto found = state.geometry_resources.begin(); found != state.geometry_resources.end();) {
        if (snapshot.find_geometry(found->first)) {
            ++found;
            continue;
        }
        if (state.renderer.id && found->second.buffer.id)
            (void)nkgpu_buffer_destroy(state.renderer, found->second.buffer);
        if (state.renderer.id && found->second.index_buffer.id)
            (void)nkgpu_buffer_destroy(state.renderer, found->second.index_buffer);
        found = state.geometry_resources.erase(found);
    }
    for (auto found = state.material_revisions.begin(); found != state.material_revisions.end();) {
        if (snapshot.find_material(found->first)) {
            ++found;
            continue;
        }
        found = state.material_revisions.erase(found);
    }
    for (auto found = state.material_resources.begin(); found != state.material_resources.end();) {
        if (snapshot.find_material(found->first)) {
            ++found;
            continue;
        }
        if (state.renderer.id && found->second.owns_image && found->second.image.id)
            (void)nkgpu_image_destroy(state.renderer, found->second.image);
        if (state.renderer.id && found->second.owns_sampler && found->second.sampler.id)
            (void)nkgpu_sampler_destroy(state.renderer, found->second.sampler);
        found = state.material_resources.erase(found);
    }

    if (state.renderer.id && !synchronize_batches(state, plan, stats))
        return false;
    return true;
}

template <class StateT>
nkgpu_result begin_pick_readback(StateT &state, nkgpu_image image, std::uint32_t x, std::uint32_t y,
                                 nkgpu_readback &out_readback) {
    nkgpu_image_readback_desc readback_desc{};
    readback_desc.struct_size = sizeof(readback_desc);
    readback_desc.image = image;
    readback_desc.width = 1;
    readback_desc.height = 1;
    readback_desc.x = x;
    readback_desc.y = y;
    return nkgpu_readback_begin_image(state.renderer, &readback_desc, &out_readback);
}

bool invert_matrix(const std::array<float, 16> &matrix, std::array<float, 16> &inverse) noexcept {
    float augmented[4][8]{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column)
            augmented[row][column] = matrix[column * 4 + row];
        augmented[row][4 + row] = 1.0f;
    }
    for (std::size_t column = 0; column < 4; ++column) {
        auto pivot = column;
        for (std::size_t row = column + 1; row < 4; ++row)
            if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column]))
                pivot = row;
        if (std::abs(augmented[pivot][column]) < 1.0e-8f)
            return false;
        if (pivot != column)
            for (std::size_t index = 0; index < 8; ++index)
                std::swap(augmented[pivot][index], augmented[column][index]);
        const auto divisor = augmented[column][column];
        for (auto &value : augmented[column])
            value /= divisor;
        for (std::size_t row = 0; row < 4; ++row) {
            if (row == column)
                continue;
            const auto factor = augmented[row][column];
            for (std::size_t index = 0; index < 8; ++index)
                augmented[row][index] -= factor * augmented[column][index];
        }
    }
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            inverse[column * 4 + row] = augmented[row][4 + column];
    return true;
}

bool unproject_pick_pixel(const RenderPlan &plan, std::uint32_t x, std::uint32_t y,
                          std::uint32_t width, std::uint32_t height, float depth,
                          Vec3 &world_position) noexcept {
    if (!width || !height || !std::isfinite(depth))
        return false;
    std::array<float, 16> inverse{};
    if (!invert_matrix(plan.view_projection(), inverse))
        return false;
    const std::array<float, 4> clip{
        (static_cast<float>(x) + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f,
        1.0f - (static_cast<float>(y) + 0.5f) / static_cast<float>(height) * 2.0f,
        depth * 2.0f - 1.0f, 1.0f};
    std::array<float, 4> world{};
    for (std::size_t row = 0; row < 4; ++row)
        for (std::size_t column = 0; column < 4; ++column)
            world[row] += inverse[column * 4 + row] * clip[column];
    if (std::abs(world[3]) < 1.0e-8f)
        return false;
    world_position = {world[0] / world[3], world[1] / world[3], world[2] / world[3]};
    return true;
}

void resolve_pick_result(const RenderPlan &plan, const SceneSnapshot &snapshot,
                         std::uint32_t pick_id, std::uint32_t subelement_id, std::uint32_t x,
                         std::uint32_t y, std::uint32_t width, std::uint32_t height, float depth,
                         PickResult &out_result) {
    out_result = {};
    if (pick_id == 0 || pick_id > plan.items().size())
        return;
    out_result = nkscene::pick(plan, snapshot, pick_id - 1, {}, 0.0f);
    out_result.depth = depth;
    (void)unproject_pick_pixel(plan, x, y, width, height, depth, out_result.worldPosition);
    if (subelement_id == 0)
        return;
    const auto &item = plan.items()[pick_id - 1];
    if (const auto *geometry = snapshot.find_geometry(item.geometry)) {
        const auto element_count = geometry->payload.element_count();
        const auto primitive = static_cast<std::size_t>(subelement_id - 1);
        if (primitive < element_count / 3)
            out_result.subelement = {geometry->subelements.id_for_primitive(primitive)};
    }
}

} // namespace

NativeKitGpuExecutor::NativeKitGpuExecutor() : state_(std::make_unique<State>()) {}

NativeKitGpuExecutor::NativeKitGpuExecutor(nkgpu_renderer renderer)
    : state_(std::make_unique<State>()) {
    state_->renderer = renderer;
}

NativeKitGpuExecutor::~NativeKitGpuExecutor() = default;

NativeKitGpuExecutor::NativeKitGpuExecutor(NativeKitGpuExecutor &&) noexcept = default;

NativeKitGpuExecutor &NativeKitGpuExecutor::operator=(NativeKitGpuExecutor &&) noexcept = default;

void NativeKitGpuExecutor::set_renderer(nkgpu_renderer renderer) noexcept {
    if (state_->renderer.id == renderer.id)
        return;
    state_->release_gpu();
    state_->renderer = renderer;
    state_->last_result = NKGPU_OK;
}

nkgpu_renderer NativeKitGpuExecutor::renderer() const noexcept {
    return state_->renderer;
}

nkgpu_result NativeKitGpuExecutor::last_result() const noexcept {
    return state_->last_result;
}

GpuExecutionStats NativeKitGpuExecutor::execute(const RenderPlan &plan,
                                                const SceneSnapshot &snapshot) {
    GpuExecutionStats stats;
    state_->last_result = NKGPU_OK;
    state_->commands.clear();
    state_->commands.reserve(plan.items().size());
    for (const auto &item : plan.items()) {
        if (has_render_flag(item.flags, RenderFlags::Hidden) ||
            has_render_flag(item.flags, RenderFlags::Culled))
            continue;
        state_->commands.push_back(
            {item.occurrence, item.geometry, item.material, item.transformIndex});
    }
    stats.commands = state_->commands.size();

    if (!prepare_resources(*state_, plan, snapshot, stats))
        return stats;

    if (!state_->renderer.id) {
        stats.draw_calls = stats.commands;
        return stats;
    }
    for (const auto &batch : state_->batches) {
        const auto geometry = state_->geometry_resources.find(batch.key.geometry);
        if (geometry == state_->geometry_resources.end() || !geometry->second.buffer.id)
            continue;
        const auto element_count =
            geometry->second.indexed ? geometry->second.index_count : geometry->second.vertex_count;
        if (element_count && !ensure_pipeline(*state_, stats, geometry->second.indexed))
            return stats;
    }

    auto result = nkgpu_begin_frame(state_->renderer);
    if (result != NKGPU_OK) {
        set_failure(*state_, stats, result);
        return stats;
    }
    const auto clip_data = clip_uniform_data(plan);
    for (const auto &batch : state_->batches) {
        const auto geometry = state_->geometry_resources.find(batch.key.geometry);
        if (geometry == state_->geometry_resources.end() || !geometry->second.buffer.id)
            continue;
        const auto element_count =
            geometry->second.indexed ? geometry->second.index_count : geometry->second.vertex_count;
        if (!element_count)
            continue;
        const auto &pipeline =
            geometry->second.indexed ? state_->indexed_pipeline : state_->pipeline;
        const auto *material = snapshot.find_material(batch.key.material);
        MaterialUniformData material_data;
        if (material) {
            material_data.base_color = material->base_color;
            material_data.base_color[3] *= material->opacity;
            material_data.surface_params = {material->metallic, material->roughness,
                                            material->alpha_cutoff,
                                            static_cast<float>(static_cast<std::uint32_t>(
                                                material->alpha_mode))};
            material_data.emissive = {material->emissive[0], material->emissive[1],
                                      material->emissive[2], 1.0f};
        }
        const auto material_gpu = state_->material_resources.find(batch.key.material);
        if (material_gpu == state_->material_resources.end()) {
            set_failure(*state_, stats, NKGPU_ERROR_INVALID_HANDLE);
            (void)nkgpu_end_frame(state_->renderer);
            return stats;
        }
        if ((result = nkgpu_apply_pipeline(state_->renderer, pipeline)) != NKGPU_OK ||
            (result = nkgpu_apply_uniform_data(
                 state_->renderer, 1,
                 reinterpret_cast<const std::uint8_t *>(plan.view_projection().data()),
                 sizeof(float) * 16)) != NKGPU_OK ||
            (result = nkgpu_apply_vertex_buffer(state_->renderer, 0, geometry->second.buffer, 0)) !=
                NKGPU_OK ||
            (geometry->second.indexed &&
             (result = nkgpu_apply_index_buffer(state_->renderer, geometry->second.index_buffer,
                                                0)) != NKGPU_OK) ||
            (result = nkgpu_apply_vertex_buffer(state_->renderer, 1, batch.buffer, 0)) !=
                NKGPU_OK ||
            (result = nkgpu_apply_image(state_->renderer, 0, material_gpu->second.image)) !=
                NKGPU_OK ||
            (result = nkgpu_apply_sampler(state_->renderer, 0, material_gpu->second.sampler)) !=
                NKGPU_OK ||
            (result = nkgpu_apply_uniform_data(
                 state_->renderer, 0,
                 reinterpret_cast<const std::uint8_t *>(&material_data), sizeof(material_data))) !=
                NKGPU_OK ||
            (result = nkgpu_apply_uniform_data(state_->renderer, 2,
                                               reinterpret_cast<const std::uint8_t *>(&clip_data),
                                               sizeof(clip_data))) != NKGPU_OK ||
            (result = nkgpu_draw(state_->renderer, 0, element_count,
                                 static_cast<std::uint32_t>(batch.instances.size()))) != NKGPU_OK) {
            set_failure(*state_, stats, result);
            (void)nkgpu_end_frame(state_->renderer);
            return stats;
        }
        ++stats.draw_calls;
    }
    result = nkgpu_end_frame(state_->renderer);
    if (result != NKGPU_OK)
        set_failure(*state_, stats, result);
    return stats;
}

std::uint32_t NativeKitGpuExecutor::poll_pick_pixel(GpuPickRequest &request,
                                                    const RenderPlan &current_plan,
                                                    const SceneSnapshot &current_snapshot,
                                                    PickResult *out_result,
                                                    nkgpu_result *out_error) {
    if (!out_result || !out_error || !request.state_) {
        if (out_error)
            *out_error = NKGPU_ERROR_INVALID_ARGUMENT;
        return NKS_RENDER_PICK_FAILED;
    }
    *out_result = {};
    *out_error = NKGPU_OK;
    auto &request_state = *request.state_;
    if (request_state.state != NKS_RENDER_PICK_PENDING) {
        *out_result = request_state.result;
        *out_error = request_state.error;
        return request_state.state;
    }
    if (!state_->renderer.id || request_state.renderer.id != state_->renderer.id) {
        request_state.state = NKS_RENDER_PICK_FAILED;
        request_state.error = NKGPU_ERROR_INVALID_HANDLE;
        *out_error = request_state.error;
        return request_state.state;
    }
    if (request_state.snapshot_revision != current_snapshot.revision() ||
        request_state.plan_source_revision != current_plan.source_revision() ||
        request_state.plan_view_signature != current_plan.view_signature()) {
        request_state.state = NKS_RENDER_PICK_STALE;
        if (request_state.color_readback.id) {
            (void)nkgpu_readback_destroy(request_state.renderer, request_state.color_readback);
            request_state.color_readback = {};
        }
        if (request_state.subelement_readback.id) {
            (void)nkgpu_readback_destroy(request_state.renderer, request_state.subelement_readback);
            request_state.subelement_readback = {};
        }
        if (request_state.depth_readback.id) {
            (void)nkgpu_readback_destroy(request_state.renderer, request_state.depth_readback);
            request_state.depth_readback = {};
        }
        return request_state.state;
    }

    nkgpu_readback_info color_info{};
    color_info.struct_size = sizeof(color_info);
    auto result =
        nkgpu_readback_query(request_state.renderer, request_state.color_readback, &color_info);
    if (result != NKGPU_OK) {
        request_state.state = NKS_RENDER_PICK_FAILED;
        request_state.error = result;
        *out_error = result;
        return request_state.state;
    }
    nkgpu_readback_info subelement_info{};
    subelement_info.struct_size = sizeof(subelement_info);
    result = nkgpu_readback_query(request_state.renderer, request_state.subelement_readback,
                                  &subelement_info);
    if (result != NKGPU_OK) {
        request_state.state = NKS_RENDER_PICK_FAILED;
        request_state.error = result;
        *out_error = result;
        return request_state.state;
    }
    nkgpu_readback_info depth_info{};
    depth_info.struct_size = sizeof(depth_info);
    result =
        nkgpu_readback_query(request_state.renderer, request_state.depth_readback, &depth_info);
    if (result != NKGPU_OK) {
        request_state.state = NKS_RENDER_PICK_FAILED;
        request_state.error = result;
        *out_error = result;
        return request_state.state;
    }
    if (color_info.state == NKGPU_READBACK_PENDING ||
        subelement_info.state == NKGPU_READBACK_PENDING ||
        depth_info.state == NKGPU_READBACK_PENDING)
        return NKS_RENDER_PICK_PENDING;
    if (color_info.state != NKGPU_READBACK_READY || subelement_info.state != NKGPU_READBACK_READY ||
        depth_info.state != NKGPU_READBACK_READY) {
        request_state.state = NKS_RENDER_PICK_FAILED;
        request_state.error = NKGPU_ERROR_UNKNOWN;
        *out_error = request_state.error;
        return request_state.state;
    }

    std::array<std::uint8_t, 4> pixel{};
    std::uint32_t read_size = 0;
    result = nkgpu_readback_read(request_state.renderer, request_state.color_readback, pixel.data(),
                                 pixel.size(), &read_size);
    if (result != NKGPU_OK || read_size < pixel.size()) {
        request_state.state = NKS_RENDER_PICK_FAILED;
        request_state.error = result != NKGPU_OK ? result : NKGPU_ERROR_UNKNOWN;
        *out_error = request_state.error;
        return request_state.state;
    }
    std::array<std::uint8_t, 4> subelement_pixel{};
    read_size = 0;
    result = nkgpu_readback_read(request_state.renderer, request_state.subelement_readback,
                                 subelement_pixel.data(), subelement_pixel.size(), &read_size);
    if (result != NKGPU_OK || read_size < subelement_pixel.size()) {
        request_state.state = NKS_RENDER_PICK_FAILED;
        request_state.error = result != NKGPU_OK ? result : NKGPU_ERROR_UNKNOWN;
        *out_error = request_state.error;
        return request_state.state;
    }
    float depth = 1.0f;
    read_size = 0;
    result =
        nkgpu_readback_read(request_state.renderer, request_state.depth_readback,
                            reinterpret_cast<std::uint8_t *>(&depth), sizeof(depth), &read_size);
    if (result != NKGPU_OK || read_size < sizeof(depth)) {
        request_state.state = NKS_RENDER_PICK_FAILED;
        request_state.error = result != NKGPU_OK ? result : NKGPU_ERROR_UNKNOWN;
        *out_error = request_state.error;
        return request_state.state;
    }
    (void)nkgpu_readback_destroy(request_state.renderer, request_state.color_readback);
    (void)nkgpu_readback_destroy(request_state.renderer, request_state.subelement_readback);
    (void)nkgpu_readback_destroy(request_state.renderer, request_state.depth_readback);
    request_state.color_readback = {};
    request_state.subelement_readback = {};
    request_state.depth_readback = {};
    resolve_pick_result(current_plan, current_snapshot, decode_pick_id(pixel),
                        decode_pick_id(subelement_pixel), request_state.x, request_state.y,
                        request_state.width, request_state.height, depth, request_state.result);
    request_state.state = NKS_RENDER_PICK_READY;
    *out_result = request_state.result;
    return request_state.state;
}

nkgpu_result NativeKitGpuExecutor::pick_pixel(const RenderPlan &plan, const SceneSnapshot &snapshot,
                                              std::uint32_t width, std::uint32_t height,
                                              std::uint32_t x, std::uint32_t y,
                                              PickResult *out_result) {
    if (!out_result) {
        state_->last_result = NKGPU_ERROR_INVALID_ARGUMENT;
        return state_->last_result;
    }
    *out_result = {};
    std::shared_ptr<GpuPickRequest> request;
    auto result = begin_pick_pixel(plan, snapshot, width, height, x, y, request);
    if (result != NKGPU_OK)
        return result;
    for (int attempt = 0; attempt < 100; ++attempt) {
        nkgpu_result error = NKGPU_OK;
        const auto state = poll_pick_pixel(*request, plan, snapshot, out_result, &error);
        if (state == NKS_RENDER_PICK_READY)
            return NKGPU_OK;
        if (state == NKS_RENDER_PICK_FAILED || state == NKS_RENDER_PICK_STALE)
            return state == NKS_RENDER_PICK_FAILED ? error : NKGPU_ERROR_WRONG_STATE;
        std::this_thread::yield();
    }
    state_->last_result = NKGPU_ERROR_WRONG_STATE;
    return state_->last_result;
}

nkgpu_result NativeKitGpuExecutor::begin_pick_pixel(const RenderPlan &plan,
                                                    const SceneSnapshot &snapshot,
                                                    std::uint32_t width, std::uint32_t height,
                                                    std::uint32_t x, std::uint32_t y,
                                                    std::shared_ptr<GpuPickRequest> &out_request) {
    if (!width || !height || x >= width || y >= height) {
        state_->last_result = NKGPU_ERROR_INVALID_ARGUMENT;
        return state_->last_result;
    }
    out_request.reset();
    if (!state_->renderer.id) {
        state_->last_result = NKGPU_ERROR_INVALID_HANDLE;
        return state_->last_result;
    }

    GpuExecutionStats stats;
    if (!prepare_resources(*state_, plan, snapshot, stats) ||
        !ensure_pick_targets(*state_, width, height, stats))
        return state_->last_result;
    for (const auto &batch : state_->batches) {
        const auto geometry = state_->geometry_resources.find(batch.key.geometry);
        if (geometry == state_->geometry_resources.end() || !geometry->second.buffer.id)
            continue;
        const auto element_count =
            geometry->second.indexed ? geometry->second.index_count : geometry->second.vertex_count;
        if (element_count && !ensure_pick_pipeline(*state_, stats, geometry->second.indexed))
            return state_->last_result;
    }

    auto result = nkgpu_frame_begin(state_->renderer);
    if (result != NKGPU_OK) {
        state_->last_result = result;
        return result;
    }

    nkgpu_render_pass_desc pass{};
    pass.struct_size = sizeof(pass);
    pass.color_count = 3;
    pass.colors[0].image = state_->pick_color;
    pass.colors[0].action.load_action = NKGPU_LOADACTION_CLEAR;
    pass.colors[0].action.store_action = NKGPU_STOREACTION_STORE;
    pass.colors[0].action.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
    pass.colors[1].image = state_->pick_subelement;
    pass.colors[1].action.load_action = NKGPU_LOADACTION_CLEAR;
    pass.colors[1].action.store_action = NKGPU_STOREACTION_STORE;
    pass.colors[1].action.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
    pass.colors[2].image = state_->pick_depth_value;
    pass.colors[2].action.load_action = NKGPU_LOADACTION_CLEAR;
    pass.colors[2].action.store_action = NKGPU_STOREACTION_STORE;
    pass.colors[2].action.clear_color = {1.0f, 0.0f, 0.0f, 0.0f};
    pass.depth_stencil = state_->pick_depth;
    pass.depth_stencil_action.load_action = NKGPU_LOADACTION_CLEAR;
    pass.depth_stencil_action.store_action = NKGPU_STOREACTION_STORE;
    pass.depth_stencil_action.clear_depth = 1.0f;
    result = nkgpu_begin_render_pass(state_->renderer, &pass);
    if (result != NKGPU_OK) {
        (void)nkgpu_end_frame(state_->renderer);
        state_->last_result = result;
        return result;
    }
    const auto clip_data = clip_uniform_data(plan);
    if ((result = nkgpu_apply_viewport(state_->renderer, 0, 0, static_cast<std::int32_t>(width),
                                       static_cast<std::int32_t>(height))) != NKGPU_OK) {
        (void)nkgpu_end_pass(state_->renderer);
        (void)nkgpu_end_frame(state_->renderer);
        state_->last_result = result;
        return result;
    }

    for (const auto &batch : state_->batches) {
        const auto geometry = state_->geometry_resources.find(batch.key.geometry);
        if (geometry == state_->geometry_resources.end() || !geometry->second.buffer.id)
            continue;
        const auto element_count =
            geometry->second.indexed ? geometry->second.index_count : geometry->second.vertex_count;
        if (!element_count)
            continue;
        const auto &pipeline =
            geometry->second.indexed ? state_->pick_indexed_pipeline : state_->pick_pipeline;
        if ((result = nkgpu_apply_pipeline(state_->renderer, pipeline)) != NKGPU_OK ||
            (result = nkgpu_apply_uniform_data(
                 state_->renderer, 1,
                 reinterpret_cast<const std::uint8_t *>(plan.view_projection().data()),
                 sizeof(float) * 16)) != NKGPU_OK ||
            (result = nkgpu_apply_vertex_buffer(state_->renderer, 0, geometry->second.buffer, 0)) !=
                NKGPU_OK ||
            (geometry->second.indexed &&
             (result = nkgpu_apply_index_buffer(state_->renderer, geometry->second.index_buffer,
                                                0)) != NKGPU_OK) ||
            (result = nkgpu_apply_vertex_buffer(state_->renderer, 1, batch.buffer, 0)) !=
                NKGPU_OK ||
            (result = nkgpu_apply_uniform_data(state_->renderer, 2,
                                               reinterpret_cast<const std::uint8_t *>(&clip_data),
                                               sizeof(clip_data))) != NKGPU_OK ||
            (result = nkgpu_draw(state_->renderer, 0, element_count,
                                 static_cast<std::uint32_t>(batch.instances.size()))) != NKGPU_OK) {
            (void)nkgpu_end_pass(state_->renderer);
            (void)nkgpu_end_frame(state_->renderer);
            state_->last_result = result;
            return result;
        }
    }
    if ((result = nkgpu_end_pass(state_->renderer)) != NKGPU_OK) {
        (void)nkgpu_end_frame(state_->renderer);
        state_->last_result = result;
        return result;
    }
    if ((result = nkgpu_end_frame(state_->renderer)) != NKGPU_OK) {
        state_->last_result = result;
        return result;
    }

    auto request = std::make_shared<GpuPickRequest>();
    request->state_ = std::make_unique<GpuPickRequest::State>();
    request->state_->renderer = state_->renderer;
    request->state_->width = width;
    request->state_->height = height;
    request->state_->x = x;
    request->state_->y = y;
    request->state_->snapshot_revision = snapshot.revision();
    request->state_->plan_source_revision = plan.source_revision();
    request->state_->plan_view_signature = plan.view_signature();
    if ((result = begin_pick_readback(*state_, state_->pick_color, x, y,
                                      request->state_->color_readback)) != NKGPU_OK) {
        state_->last_result = result;
        return result;
    }
    if ((result = begin_pick_readback(*state_, state_->pick_subelement, x, y,
                                      request->state_->subelement_readback)) != NKGPU_OK) {
        (void)nkgpu_readback_destroy(state_->renderer, request->state_->color_readback);
        request->state_->color_readback = {};
        state_->last_result = result;
        return result;
    }
    if ((result = begin_pick_readback(*state_, state_->pick_depth_value, x, y,
                                      request->state_->depth_readback)) != NKGPU_OK) {
        (void)nkgpu_readback_destroy(state_->renderer, request->state_->color_readback);
        (void)nkgpu_readback_destroy(state_->renderer, request->state_->subelement_readback);
        request->state_->color_readback = {};
        request->state_->subelement_readback = {};
        state_->last_result = result;
        return result;
    }
    out_request = std::move(request);
    state_->last_result = NKGPU_OK;
    return NKGPU_OK;
}

std::span<const GpuCommand> NativeKitGpuExecutor::commands() const noexcept {
    return state_->commands;
}

} // namespace nkscene

namespace nkscene::render_internal {

/* GPU submission is isolated from scene hierarchy and resource ownership. */

} // namespace nkscene::render_internal
