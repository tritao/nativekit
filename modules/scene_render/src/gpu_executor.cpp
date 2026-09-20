#include "nativekit_scene_render.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nkscene {

namespace {

constexpr std::uint32_t vertex_stride = sizeof(GeometryVertex);
constexpr std::uint32_t instance_stride = sizeof(float) * 20;

static_assert(sizeof(GeometryVertex) == sizeof(float) * 3);

struct InstanceData {
    std::array<float, 16> transform;
    std::array<float, 4> pick_color;
};

static_assert(sizeof(InstanceData) == instance_stride);

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
    return {static_cast<float>(id & 0xffu) / 255.0f,
            static_cast<float>((id >> 8) & 0xffu) / 255.0f,
            static_cast<float>((id >> 16) & 0xffu) / 255.0f, 1.0f};
}

std::uint32_t decode_pick_id(const std::array<std::uint8_t, 4> &pixel) {
    return static_cast<std::uint32_t>(pixel[0]) |
        (static_cast<std::uint32_t>(pixel[1]) << 8) |
        (static_cast<std::uint32_t>(pixel[2]) << 16);
}

const RenderItem *find_item(const RenderPlan &plan, OccurrenceId occurrence) {
    const auto items = plan.items();
    const auto found = std::find_if(items.begin(), items.end(), [&](const RenderItem &item) {
        return item.occurrence == occurrence;
    });
    return found == items.end() ? nullptr : &*found;
}

std::vector<DesiredBatch> desired_batches(const RenderPlan &plan) {
    std::vector<DesiredBatch> result;
    for (const auto &batch : plan.batches()) {
        DesiredBatch desired{{batch.geometry, batch.material}};
        const auto transforms = plan.transforms();
        for (const auto occurrence : batch.instances) {
            const auto *item = find_item(plan, occurrence);
            if (!item || has_render_flag(item->flags, RenderFlags::Hidden) ||
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

const char *vertex_shader_source(bool gles) {
    return gles
        ? "#version 300 es\n"
          "precision highp float;\n"
          "layout(location=0) in vec3 position;\n"
          "layout(location=1) in vec4 transform0;\n"
          "layout(location=2) in vec4 transform1;\n"
          "layout(location=3) in vec4 transform2;\n"
          "layout(location=4) in vec4 transform3;\n"
          "void main(){ mat4 transform=mat4(transform0,transform1,transform2,transform3);"
          "gl_Position=transform*vec4(position,1.0); }\n"
        : "#version 330\n"
          "layout(location=0) in vec3 position;\n"
          "layout(location=1) in vec4 transform0;\n"
          "layout(location=2) in vec4 transform1;\n"
          "layout(location=3) in vec4 transform2;\n"
          "layout(location=4) in vec4 transform3;\n"
          "void main(){ mat4 transform=mat4(transform0,transform1,transform2,transform3);"
          "gl_Position=transform*vec4(position,1.0); }\n";
}

const char *fragment_shader_source(bool gles) {
    return gles
        ? "#version 300 es\n"
          "precision mediump float;\n"
          "uniform vec4 material_color;\n"
          "out vec4 fragment_color;\n"
          "void main(){ fragment_color=material_color; }\n"
        : "#version 330\n"
          "uniform vec4 material_color;\n"
          "out vec4 fragment_color;\n"
          "void main(){ fragment_color=material_color; }\n";
}

const char *pick_vertex_shader_source(bool gles) {
    return gles
        ? "#version 300 es\n"
          "precision highp float;\n"
          "layout(location=0) in vec3 position;\n"
          "layout(location=1) in vec4 transform0;\n"
          "layout(location=2) in vec4 transform1;\n"
          "layout(location=3) in vec4 transform2;\n"
          "layout(location=4) in vec4 transform3;\n"
          "layout(location=5) in vec4 pick_color;\n"
          "out vec4 vertex_pick_color;\n"
          "void main(){ mat4 transform=mat4(transform0,transform1,transform2,transform3);"
          "vertex_pick_color=pick_color; gl_Position=transform*vec4(position,1.0); }\n"
        : "#version 330\n"
          "layout(location=0) in vec3 position;\n"
          "layout(location=1) in vec4 transform0;\n"
          "layout(location=2) in vec4 transform1;\n"
          "layout(location=3) in vec4 transform2;\n"
          "layout(location=4) in vec4 transform3;\n"
          "layout(location=5) in vec4 pick_color;\n"
          "out vec4 vertex_pick_color;\n"
          "void main(){ mat4 transform=mat4(transform0,transform1,transform2,transform3);"
          "vertex_pick_color=pick_color; gl_Position=transform*vec4(position,1.0); }\n";
}

const char *pick_fragment_shader_source(bool gles) {
    return gles
        ? "#version 300 es\n"
          "precision mediump float;\n"
          "in vec4 vertex_pick_color;\n"
          "out vec4 fragment_color;\n"
          "layout(location=1) out vec4 fragment_subelement;\n"
          "void main(){ uint id=uint(gl_PrimitiveID)+1u;"
          "fragment_color=vertex_pick_color; fragment_subelement=vec4(float(id & 0xffu)/255.0,"
          "float((id >> 8u) & 0xffu)/255.0,float((id >> 16u) & 0xffu)/255.0,1.0); }\n"
        : "#version 330\n"
          "in vec4 vertex_pick_color;\n"
          "out vec4 fragment_color;\n"
          "layout(location=1) out vec4 fragment_subelement;\n"
          "void main(){ uint id=uint(gl_PrimitiveID)+1u;"
          "fragment_color=vertex_pick_color; fragment_subelement=vec4(float(id & 0xffu)/255.0,"
          "float((id >> 8u) & 0xffu)/255.0,float((id >> 16u) & 0xffu)/255.0,1.0); }\n";
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
    nkgpu_image pick_depth{};
    std::uint32_t pick_width = 0;
    std::uint32_t pick_height = 0;
    std::unordered_map<GeometryId, GeometryGpu> geometry_resources;
    std::unordered_map<MaterialId, std::uint64_t> material_revisions;
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
        for (auto &batch : batches) {
            if (batch.buffer.id)
                (void)nkgpu_buffer_destroy(renderer, batch.buffer);
        }
        if (pick_color.id)
            (void)nkgpu_image_destroy(renderer, pick_color);
        if (pick_subelement.id)
            (void)nkgpu_image_destroy(renderer, pick_subelement);
        if (pick_depth.id)
            (void)nkgpu_image_destroy(renderer, pick_depth);
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
        batches.clear();
        pipeline = {};
        indexed_pipeline = {};
        shader = {};
        pick_pipeline = {};
        pick_indexed_pipeline = {};
        pick_shader = {};
        pick_color = {};
        pick_subelement = {};
        pick_depth = {};
        pick_width = 0;
        pick_height = 0;
    }
};

namespace {

bool valid_geometry_payload(const GeometryResource &resource) {
    const auto vertex_count = resource.payload.vertices.size();
    const auto index_count = resource.payload.indices.size();
    return vertex_count <= std::numeric_limits<std::uint32_t>::max() &&
        index_count <= std::numeric_limits<std::uint32_t>::max() &&
        vertex_count <= std::numeric_limits<std::uint32_t>::max() / sizeof(GeometryVertex) &&
        index_count % 3 == 0 &&
        (resource.payload.indices.empty()
             ? vertex_count % 3 == 0
             : !std::any_of(resource.payload.indices.begin(), resource.payload.indices.end(),
                            [vertex_count](std::uint32_t index) {
                                return static_cast<std::size_t>(index) >= vertex_count;
                            }));
}

template<class StateT>
void destroy_batch_buffers(StateT &state) noexcept {
    if (!state.renderer.id)
        return;
    for (auto &batch : state.batches) {
        if (batch.buffer.id)
            (void)nkgpu_buffer_destroy(state.renderer, batch.buffer);
    }
    state.batches.clear();
}

template<class StateT>
bool set_failure(StateT &state, GpuExecutionStats &stats, nkgpu_result result) {
    stats.result = result;
    state.last_result = result;
    return false;
}

template<class StateT>
bool ensure_pipeline(StateT &state, GpuExecutionStats &stats, bool indexed) {
    auto &pipeline = indexed ? state.indexed_pipeline : state.pipeline;
    if (pipeline.id)
        return true;

    const auto graphics_api = nkgpu_query_graphics_api(state.renderer);
    const bool gles = graphics_api == NK_GRAPHICS_OPENGL_ES;
    if (graphics_api != NK_GRAPHICS_OPENGL && !gles)
        return set_failure(state, stats, NKGPU_ERROR_UNSUPPORTED);

    nkgpu_result result = NKGPU_OK;
    if (!state.shader.id) {
        nkgpu_shader_builder shader_builder{};
        result = nkgpu_shader_begin(state.renderer, NKGPU_SHADERLANGUAGE_GLSL,
                                    vertex_shader_source(gles), fragment_shader_source(gles),
                                    &shader_builder);
        if (result != NKGPU_OK)
            return set_failure(state, stats, result);
        const auto attribute = [&](std::uint32_t location, const char *name,
                                   const char *semantic) {
            return nkgpu_shader_attribute(shader_builder, location, name, semantic, location);
        };
        if ((result = attribute(0, "position", "POSITION")) != NKGPU_OK ||
            (result = attribute(1, "transform0", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(2, "transform1", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(3, "transform2", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(4, "transform3", "TEXCOORD")) != NKGPU_OK ||
            (result = nkgpu_shader_uniform_block(shader_builder, 0,
                                                 NKGPU_SHADERSTAGE_FRAGMENT,
                                                 sizeof(float) * 4)) != NKGPU_OK ||
            (result = nkgpu_shader_uniform(shader_builder, 0, 0, "material_color",
                                           NKGPU_UNIFORMTYPE_FLOAT4, 0)) != NKGPU_OK ||
            (result = nkgpu_shader_end(shader_builder, &state.shader)) != NKGPU_OK)
            return set_failure(state, stats, result);
    }

    nkgpu_pipeline_builder pipeline_builder{};
    if ((result = nkgpu_pipeline_begin(state.renderer, state.shader, vertex_stride,
                                       &pipeline_builder)) != NKGPU_OK)
        return set_failure(state, stats, result);
    if ((result = nkgpu_pipeline_attribute(pipeline_builder, 0, 0, 0,
                                           NKGPU_VERTEXFORMAT_FLOAT3)) != NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 1, 1, 0,
                                           NKGPU_VERTEXFORMAT_FLOAT4)) != NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 2, 1, sizeof(float) * 4,
                                           NKGPU_VERTEXFORMAT_FLOAT4)) != NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 3, 1, sizeof(float) * 8,
                                           NKGPU_VERTEXFORMAT_FLOAT4)) != NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 4, 1, sizeof(float) * 12,
                                           NKGPU_VERTEXFORMAT_FLOAT4)) != NKGPU_OK ||
        (result = nkgpu_pipeline_vertex_buffer(pipeline_builder, 0, vertex_stride,
                                                NKGPU_VERTEXSTEP_PER_VERTEX, 1)) != NKGPU_OK ||
        (result = nkgpu_pipeline_vertex_buffer(pipeline_builder, 1, instance_stride,
                                                NKGPU_VERTEXSTEP_PER_INSTANCE, 1)) != NKGPU_OK ||
        (result = nkgpu_pipeline_primitive_type(pipeline_builder,
                                                NKGPU_PRIMITIVETYPE_TRIANGLES)) != NKGPU_OK ||
        (indexed && (result = nkgpu_pipeline_index_type(pipeline_builder,
                                                         NKGPU_INDEXTYPE_UINT32)) != NKGPU_OK) ||
        (result = nkgpu_pipeline_depth_stencil(pipeline_builder, 1)) != NKGPU_OK ||
        (result = nkgpu_pipeline_end(pipeline_builder, &pipeline)) != NKGPU_OK)
        return set_failure(state, stats, result);
    return true;
}

template<class StateT>
bool ensure_pick_pipeline(StateT &state, GpuExecutionStats &stats, bool indexed) {
    auto &pipeline = indexed ? state.pick_indexed_pipeline : state.pick_pipeline;
    if (pipeline.id)
        return true;

    const auto graphics_api = nkgpu_query_graphics_api(state.renderer);
    const bool gles = graphics_api == NK_GRAPHICS_OPENGL_ES;
    if (graphics_api != NK_GRAPHICS_OPENGL && !gles)
        return set_failure(state, stats, NKGPU_ERROR_UNSUPPORTED);

    nkgpu_result result = NKGPU_OK;
    if (!state.pick_shader.id) {
        nkgpu_shader_builder shader_builder{};
        result = nkgpu_shader_begin(state.renderer, NKGPU_SHADERLANGUAGE_GLSL,
                                    pick_vertex_shader_source(gles),
                                    pick_fragment_shader_source(gles), &shader_builder);
        if (result != NKGPU_OK)
            return set_failure(state, stats, result);
        const auto attribute = [&](std::uint32_t location, const char *name,
                                   const char *semantic) {
            return nkgpu_shader_attribute(shader_builder, location, name, semantic, location);
        };
        if ((result = attribute(0, "position", "POSITION")) != NKGPU_OK ||
            (result = attribute(1, "transform0", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(2, "transform1", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(3, "transform2", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(4, "transform3", "TEXCOORD")) != NKGPU_OK ||
            (result = attribute(5, "pick_color", "TEXCOORD")) != NKGPU_OK ||
            (result = nkgpu_shader_end(shader_builder, &state.pick_shader)) != NKGPU_OK)
            return set_failure(state, stats, result);
    }

    nkgpu_pipeline_builder pipeline_builder{};
    if ((result = nkgpu_pipeline_begin(state.renderer, state.pick_shader, vertex_stride,
                                       &pipeline_builder)) != NKGPU_OK)
        return set_failure(state, stats, result);
    if ((result = nkgpu_pipeline_attribute(pipeline_builder, 0, 0, 0,
                                           NKGPU_VERTEXFORMAT_FLOAT3)) != NKGPU_OK ||
        (result = nkgpu_pipeline_attribute(pipeline_builder, 1, 1, 0,
                                           NKGPU_VERTEXFORMAT_FLOAT4)) != NKGPU_OK ||
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
        (result = nkgpu_pipeline_primitive_type(pipeline_builder,
                                                NKGPU_PRIMITIVETYPE_TRIANGLES)) != NKGPU_OK ||
        (indexed && (result = nkgpu_pipeline_index_type(pipeline_builder,
                                                         NKGPU_INDEXTYPE_UINT32)) != NKGPU_OK) ||
        (result = nkgpu_pipeline_depth_stencil(pipeline_builder, 1)) != NKGPU_OK ||
        (result = nkgpu_pipeline_color_target(pipeline_builder, 0, NKGPU_IMAGEFORMAT_RGBA8,
                                              NKGPU_COLORMASK_RGBA, nullptr)) != NKGPU_OK ||
        (result = nkgpu_pipeline_color_target(pipeline_builder, 1, NKGPU_IMAGEFORMAT_RGBA8,
                                              NKGPU_COLORMASK_RGBA, nullptr)) != NKGPU_OK ||
        (result = nkgpu_pipeline_end(pipeline_builder, &pipeline)) != NKGPU_OK)
        return set_failure(state, stats, result);
    return true;
}

template<class StateT>
bool ensure_pick_targets(StateT &state, std::uint32_t width, std::uint32_t height,
                         GpuExecutionStats &stats) {
    if (state.pick_color.id && state.pick_subelement.id && state.pick_width == width &&
        state.pick_height == height)
        return true;
    if (state.pick_color.id)
        (void)nkgpu_image_destroy(state.renderer, state.pick_color);
    if (state.pick_subelement.id)
        (void)nkgpu_image_destroy(state.renderer, state.pick_subelement);
    if (state.pick_depth.id)
        (void)nkgpu_image_destroy(state.renderer, state.pick_depth);
    state.pick_color = {};
    state.pick_subelement = {};
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
        state.pick_color = {};
        state.pick_subelement = {};
        return set_failure(state, stats, result);
    }
    state.pick_width = width;
    state.pick_height = height;
    return true;
}

template<class StateT>
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

template<class StateT>
bool ensure_geometry(StateT &state, const GeometryResource &resource, GpuExecutionStats &stats) {
    const auto vertex_count = resource.payload.vertices.size();
    const auto index_count = resource.payload.indices.size();
    if (!valid_geometry_payload(resource))
        return set_failure(state, stats, NKGPU_ERROR_INVALID_ARGUMENT);

    auto [found, inserted] = state.geometry_resources.try_emplace(resource.id);
    auto &cached = found->second;
    const auto byte_size = vertex_count * sizeof(GeometryVertex);
    const auto index_byte_size = index_count * sizeof(std::uint32_t);
    const bool revision_changed = inserted || cached.revision != resource.revision;
    if (!revision_changed)
        return true;

    if (cached.buffer.id) {
        if (cached.byte_size == byte_size && byte_size != 0) {
            const auto result = nkgpu_buffer_update(
                state.renderer, cached.buffer, 0,
                reinterpret_cast<const std::uint8_t *>(resource.payload.vertices.data()),
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
        descriptor.data = reinterpret_cast<const std::uint8_t *>(resource.payload.vertices.data());
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
        const auto result = nkgpu_buffer_create_desc(state.renderer, &descriptor,
                                                      &cached.index_buffer);
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

template<class StateT>
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
                state.renderer, have.buffer,
                static_cast<std::uint32_t>(instance * instance_stride),
                reinterpret_cast<const std::uint8_t *>(&data),
                instance_stride);
            if (result != NKGPU_OK)
                return set_failure(state, stats, result);
            have.transform_revisions[instance] = want.transform_revisions[instance];
            ++stats.instance_records_updated;
        }
    }
    return true;
}

template<class StateT>
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
    }

    for (auto found = state.geometry_resources.begin();
         found != state.geometry_resources.end();) {
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
    for (auto found = state.material_revisions.begin();
         found != state.material_revisions.end();) {
        if (snapshot.find_material(found->first)) {
            ++found;
            continue;
        }
        found = state.material_revisions.erase(found);
    }

    if (state.renderer.id && !synchronize_batches(state, plan, stats))
        return false;
    return true;
}

template<class StateT>
nkgpu_result read_pick_pixel(StateT &state, nkgpu_image image, std::uint32_t x,
                             std::uint32_t y, std::array<std::uint8_t, 4> &pixel) {
    nkgpu_image_readback_desc readback_desc{};
    readback_desc.struct_size = sizeof(readback_desc);
    readback_desc.image = image;
    readback_desc.width = 1;
    readback_desc.height = 1;
    readback_desc.x = x;
    readback_desc.y = y;
    nkgpu_readback readback{};
    auto result = nkgpu_readback_begin_image(state.renderer, &readback_desc, &readback);
    if (result != NKGPU_OK)
        return result;

    nkgpu_readback_info info{};
    info.struct_size = sizeof(info);
    for (int attempt = 0; attempt < 100; ++attempt) {
        result = nkgpu_readback_query(state.renderer, readback, &info);
        if (result != NKGPU_OK) {
            (void)nkgpu_readback_destroy(state.renderer, readback);
            return result;
        }
        if (info.state != NKGPU_READBACK_PENDING)
            break;
        std::this_thread::yield();
    }
    if (info.state != NKGPU_READBACK_READY || info.size < pixel.size()) {
        (void)nkgpu_readback_destroy(state.renderer, readback);
        return info.state == NKGPU_READBACK_FAILED ? NKGPU_ERROR_UNKNOWN
                                                   : NKGPU_ERROR_WRONG_STATE;
    }
    std::uint32_t read_size = 0;
    result = nkgpu_readback_read(state.renderer, readback, pixel.data(), pixel.size(), &read_size);
    (void)nkgpu_readback_destroy(state.renderer, readback);
    return result != NKGPU_OK ? result
        : read_size < pixel.size() ? NKGPU_ERROR_UNKNOWN : NKGPU_OK;
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

nkgpu_renderer NativeKitGpuExecutor::renderer() const noexcept { return state_->renderer; }

nkgpu_result NativeKitGpuExecutor::last_result() const noexcept { return state_->last_result; }

GpuExecutionStats NativeKitGpuExecutor::execute(const RenderPlan &plan,
                                                 const SceneSnapshot &snapshot) {
    GpuExecutionStats stats;
    state_->last_result = NKGPU_OK;
    state_->commands.clear();
    state_->commands.reserve(plan.items().size());
    for (const auto &item : plan.items()) {
        if (has_render_flag(item.flags, RenderFlags::Hidden))
            continue;
        state_->commands.push_back({item.occurrence, item.geometry, item.material,
                                    item.transformIndex});
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
        const auto element_count = geometry->second.indexed ? geometry->second.index_count
                                                            : geometry->second.vertex_count;
        if (element_count && !ensure_pipeline(*state_, stats, geometry->second.indexed))
            return stats;
    }

    auto result = nkgpu_begin_frame(state_->renderer);
    if (result != NKGPU_OK) {
        set_failure(*state_, stats, result);
        return stats;
    }
    for (const auto &batch : state_->batches) {
        const auto geometry = state_->geometry_resources.find(batch.key.geometry);
        if (geometry == state_->geometry_resources.end() || !geometry->second.buffer.id)
            continue;
        const auto element_count = geometry->second.indexed ? geometry->second.index_count
                                                            : geometry->second.vertex_count;
        if (!element_count)
            continue;
        const auto &pipeline = geometry->second.indexed ? state_->indexed_pipeline
                                                        : state_->pipeline;
        const auto *material = snapshot.find_material(batch.key.material);
        std::array<float, 4> material_color{1.0f, 1.0f, 1.0f, 1.0f};
        if (material) {
            material_color = material->base_color;
            material_color[3] *= material->opacity;
        }
        if ((result = nkgpu_apply_pipeline(state_->renderer, pipeline)) != NKGPU_OK ||
            (result = nkgpu_apply_vertex_buffer(state_->renderer, 0, geometry->second.buffer, 0)) !=
                NKGPU_OK ||
            (geometry->second.indexed &&
             (result = nkgpu_apply_index_buffer(state_->renderer,
                                                geometry->second.index_buffer, 0)) != NKGPU_OK) ||
            (result = nkgpu_apply_vertex_buffer(state_->renderer, 1, batch.buffer, 0)) != NKGPU_OK ||
            (result = nkgpu_apply_uniform_data(
                 state_->renderer, 0,
                 reinterpret_cast<const std::uint8_t *>(material_color.data()),
                 sizeof(material_color))) != NKGPU_OK ||
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

nkgpu_result NativeKitGpuExecutor::pick_pixel(const RenderPlan &plan,
                                              const SceneSnapshot &snapshot,
                                              std::uint32_t width, std::uint32_t height,
                                              std::uint32_t x, std::uint32_t y,
                                              PickResult *out_result) {
    if (!out_result || !width || !height || x >= width || y >= height) {
        state_->last_result = NKGPU_ERROR_INVALID_ARGUMENT;
        return state_->last_result;
    }
    *out_result = {};
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
        const auto element_count = geometry->second.indexed ? geometry->second.index_count
                                                            : geometry->second.vertex_count;
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
    pass.color_count = 2;
    pass.colors[0].image = state_->pick_color;
    pass.colors[0].action.load_action = NKGPU_LOADACTION_CLEAR;
    pass.colors[0].action.store_action = NKGPU_STOREACTION_STORE;
    pass.colors[0].action.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
    pass.colors[1].image = state_->pick_subelement;
    pass.colors[1].action.load_action = NKGPU_LOADACTION_CLEAR;
    pass.colors[1].action.store_action = NKGPU_STOREACTION_STORE;
    pass.colors[1].action.clear_color = {0.0f, 0.0f, 0.0f, 0.0f};
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
        const auto element_count = geometry->second.indexed ? geometry->second.index_count
                                                            : geometry->second.vertex_count;
        if (!element_count)
            continue;
        const auto &pipeline = geometry->second.indexed ? state_->pick_indexed_pipeline
                                                        : state_->pick_pipeline;
        if ((result = nkgpu_apply_pipeline(state_->renderer, pipeline)) != NKGPU_OK ||
            (result = nkgpu_apply_vertex_buffer(state_->renderer, 0, geometry->second.buffer, 0)) !=
                NKGPU_OK ||
            (geometry->second.indexed &&
             (result = nkgpu_apply_index_buffer(state_->renderer,
                                                geometry->second.index_buffer, 0)) != NKGPU_OK) ||
            (result = nkgpu_apply_vertex_buffer(state_->renderer, 1, batch.buffer, 0)) != NKGPU_OK ||
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

    std::array<std::uint8_t, 4> pixel{};
    if ((result = read_pick_pixel(*state_, state_->pick_color, x, y, pixel)) != NKGPU_OK) {
        state_->last_result = result;
        return result;
    }

    std::array<std::uint8_t, 4> subelement_pixel{};
    if ((result = read_pick_pixel(*state_, state_->pick_subelement, x, y,
                                  subelement_pixel)) != NKGPU_OK) {
        state_->last_result = result;
        return state_->last_result;
    }

    const auto pick_id = decode_pick_id(pixel);
    const auto subelement_id = decode_pick_id(subelement_pixel);
    if (pick_id != 0 && pick_id <= plan.items().size()) {
        *out_result = nkscene::pick(plan, snapshot, pick_id - 1, {}, 0.0f);
        if (subelement_id != 0) {
            const auto &item = plan.items()[pick_id - 1];
            if (const auto *geometry = snapshot.find_geometry(item.geometry)) {
                const auto element_count = geometry->payload.element_count();
                const auto primitive = static_cast<std::size_t>(subelement_id - 1);
                if (primitive < element_count / 3)
                    out_result->subelement = {
                        geometry->subelements.id_for_primitive(primitive)};
            }
        }
    }
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
