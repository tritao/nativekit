#include "nativekit_scene_render.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nkscene {

namespace {

constexpr std::uint32_t vertex_stride = sizeof(float) * 3;
constexpr std::uint32_t instance_stride = sizeof(float) * 16;

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
};

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
          "out vec4 fragment_color;\n"
          "void main(){ fragment_color=vec4(1.0); }\n"
        : "#version 330\n"
          "out vec4 fragment_color;\n"
          "void main(){ fragment_color=vec4(1.0); }\n";
}

} // namespace

struct NativeKitGpuExecutor::State {
    struct GeometryGpu {
        nkgpu_buffer buffer{};
        std::uint64_t revision = 0;
        std::uint32_t byte_size = 0;
        std::uint32_t vertex_count = 0;
    };

    struct BatchGpu {
        BatchKey key;
        nkgpu_buffer buffer{};
        std::vector<OccurrenceId> instances;
        std::vector<std::uint64_t> transform_revisions;
    };

    nkgpu_renderer renderer{};
    nkgpu_shader shader{};
    nkgpu_pipeline pipeline{};
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
        }
        for (auto &batch : batches) {
            if (batch.buffer.id)
                (void)nkgpu_buffer_destroy(renderer, batch.buffer);
        }
        if (pipeline.id)
            (void)nkgpu_pipeline_destroy(renderer, pipeline);
        if (shader.id)
            (void)nkgpu_shader_destroy(renderer, shader);
        geometry_resources.clear();
        material_revisions.clear();
        batches.clear();
        pipeline = {};
        shader = {};
    }
};

namespace {

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
bool ensure_pipeline(StateT &state, GpuExecutionStats &stats) {
    if (state.pipeline.id)
        return true;

    const auto graphics_api = nkgpu_query_graphics_api(state.renderer);
    const bool gles = graphics_api == NK_GRAPHICS_OPENGL_ES;
    if (graphics_api != NK_GRAPHICS_OPENGL && !gles)
        return set_failure(state, stats, NKGPU_ERROR_UNSUPPORTED);

    nkgpu_shader_builder shader_builder{};
    auto result = nkgpu_shader_begin(state.renderer, NKGPU_SHADERLANGUAGE_GLSL,
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
        (result = attribute(4, "transform3", "TEXCOORD")) != NKGPU_OK)
        return set_failure(state, stats, result);
    if ((result = nkgpu_shader_end(shader_builder, &state.shader)) != NKGPU_OK)
        return set_failure(state, stats, result);

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
        (result = nkgpu_pipeline_depth_stencil(pipeline_builder, 1)) != NKGPU_OK ||
        (result = nkgpu_pipeline_end(pipeline_builder, &state.pipeline)) != NKGPU_OK)
        return set_failure(state, stats, result);
    return true;
}

template<class StateT>
bool create_instance_buffer(StateT &state, const DesiredBatch &desired,
                            typename StateT::BatchGpu &target, GpuExecutionStats &stats) {
    const auto byte_size = desired.transforms.size() * sizeof(std::array<float, 16>);
    if (byte_size > std::numeric_limits<std::uint32_t>::max())
        return set_failure(state, stats, NKGPU_ERROR_INVALID_ARGUMENT);
    nkgpu_buffer_desc descriptor{};
    descriptor.struct_size = sizeof(descriptor);
    descriptor.size = static_cast<std::uint32_t>(byte_size);
    descriptor.usage = NKGPU_BUFFER_VERTEX;
    descriptor.data = reinterpret_cast<const std::uint8_t *>(desired.transforms.data());
    descriptor.data_size = descriptor.size;
    descriptor.dynamic_update = 1;
    const auto result = nkgpu_buffer_create_desc(state.renderer, &descriptor, &target.buffer);
    if (result != NKGPU_OK)
        return set_failure(state, stats, result);
    target.key = desired.key;
    target.instances = desired.instances;
    target.transform_revisions = desired.transform_revisions;
    ++stats.instance_buffers_created;
    return true;
}

template<class StateT>
bool ensure_geometry(StateT &state, const GeometryResource &resource, GpuExecutionStats &stats) {
    auto [found, inserted] = state.geometry_resources.try_emplace(resource.id);
    auto &cached = found->second;
    const auto byte_size = resource.payload.bytes.size();
    if (byte_size > std::numeric_limits<std::uint32_t>::max() || byte_size % vertex_stride != 0)
        return set_failure(state, stats, NKGPU_ERROR_INVALID_ARGUMENT);
    const auto vertex_count = static_cast<std::uint32_t>(byte_size / vertex_stride);
    if (!inserted && cached.revision == resource.revision)
        return true;

    if (cached.buffer.id) {
        if (cached.byte_size == byte_size && byte_size != 0) {
            const auto result = nkgpu_buffer_update(
                state.renderer, cached.buffer, 0,
                reinterpret_cast<const std::uint8_t *>(resource.payload.bytes.data()),
                static_cast<std::uint32_t>(byte_size));
            if (result != NKGPU_OK)
                return set_failure(state, stats, result);
            ++stats.geometry_resources_updated;
        } else {
            (void)nkgpu_buffer_destroy(state.renderer, cached.buffer);
            cached.buffer = {};
            cached.byte_size = 0;
            cached.vertex_count = 0;
            if (byte_size != 0)
                ++stats.geometry_resources_updated;
        }
    }
    if (byte_size != 0 && !cached.buffer.id) {
        nkgpu_buffer_desc descriptor{};
        descriptor.struct_size = sizeof(descriptor);
        descriptor.size = static_cast<std::uint32_t>(byte_size);
        descriptor.usage = NKGPU_BUFFER_VERTEX;
        descriptor.data = reinterpret_cast<const std::uint8_t *>(resource.payload.bytes.data());
        descriptor.data_size = descriptor.size;
        descriptor.dynamic_update = 1;
        const auto result = nkgpu_buffer_create_desc(state.renderer, &descriptor, &cached.buffer);
        if (result != NKGPU_OK)
            return set_failure(state, stats, result);
        ++stats.geometry_resources_created;
    }
    cached.revision = resource.revision;
    cached.byte_size = static_cast<std::uint32_t>(byte_size);
    cached.vertex_count = vertex_count;
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
            if (!(want.key == have.key) || want.instances != have.instances) {
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
            const auto result = nkgpu_buffer_update(
                state.renderer, have.buffer,
                static_cast<std::uint32_t>(instance * instance_stride),
                reinterpret_cast<const std::uint8_t *>(want.transforms[instance].data()),
                instance_stride);
            if (result != NKGPU_OK)
                return set_failure(state, stats, result);
            have.transform_revisions[instance] = want.transform_revisions[instance];
            ++stats.instance_records_updated;
        }
    }
    return true;
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

    for (const auto &resource : snapshot.geometries()) {
        const auto found = state_->geometry_resources.find(resource.id);
        if (!state_->renderer.id) {
            if (found == state_->geometry_resources.end()) {
                state_->geometry_resources.emplace(resource.id,
                                                   State::GeometryGpu{{}, resource.revision});
                ++stats.geometry_resources_created;
            } else if (found->second.revision != resource.revision) {
                found->second.revision = resource.revision;
                ++stats.geometry_resources_updated;
            }
        } else if (!ensure_geometry(*state_, resource, stats)) {
            return stats;
        }
    }
    for (const auto &resource : snapshot.materials()) {
        const auto found = state_->material_revisions.find(resource.id);
        if (found == state_->material_revisions.end()) {
            state_->material_revisions.emplace(resource.id, resource.revision);
            ++stats.material_resources_created;
        } else if (found->second != resource.revision) {
            found->second = resource.revision;
            ++stats.material_resources_updated;
        }
    }

    for (auto found = state_->geometry_resources.begin();
         found != state_->geometry_resources.end();) {
        if (snapshot.find_geometry(found->first)) {
            ++found;
            continue;
        }
        if (state_->renderer.id && found->second.buffer.id)
            (void)nkgpu_buffer_destroy(state_->renderer, found->second.buffer);
        found = state_->geometry_resources.erase(found);
    }
    for (auto found = state_->material_revisions.begin();
         found != state_->material_revisions.end();) {
        if (snapshot.find_material(found->first)) {
            ++found;
            continue;
        }
        found = state_->material_revisions.erase(found);
    }

    if (!state_->renderer.id) {
        stats.draw_calls = stats.commands;
        return stats;
    }
    if (!ensure_pipeline(*state_, stats) || !synchronize_batches(*state_, plan, stats))
        return stats;

    auto result = nkgpu_begin_frame(state_->renderer);
    if (result != NKGPU_OK) {
        set_failure(*state_, stats, result);
        return stats;
    }
    for (const auto &batch : state_->batches) {
        const auto geometry = state_->geometry_resources.find(batch.key.geometry);
        if (geometry == state_->geometry_resources.end() || !geometry->second.buffer.id ||
            !geometry->second.vertex_count)
            continue;
        if ((result = nkgpu_apply_pipeline(state_->renderer, state_->pipeline)) != NKGPU_OK ||
            (result = nkgpu_apply_vertex_buffer(state_->renderer, 0, geometry->second.buffer, 0)) !=
                NKGPU_OK ||
            (result = nkgpu_apply_vertex_buffer(state_->renderer, 1, batch.buffer, 0)) != NKGPU_OK ||
            (result = nkgpu_draw(state_->renderer, 0, geometry->second.vertex_count,
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

std::span<const GpuCommand> NativeKitGpuExecutor::commands() const noexcept {
    return state_->commands;
}

} // namespace nkscene

namespace nkscene::render_internal {

/* GPU submission is isolated from scene hierarchy and resource ownership. */

} // namespace nkscene::render_internal
