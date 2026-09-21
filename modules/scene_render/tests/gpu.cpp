#include "nativekit.h"
#include "nativekit_scene_render.h"
#include "nativekit_window.h"

#include "scene_internal.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>

namespace {

using nkscene::ChangeSet;
using nkscene::Scene;
using nkscene::Transaction;

bool wait_for_surface(nk_surface surface) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        nk_event event{};
        event.struct_size = sizeof(event);
        if (nk_poll_event(&event) != NK_OK)
            return false;
        const bool ready = event.kind == NK_EVENT_SURFACE_READY && event.source == surface;
        nk_event_release(&event);
        if (ready)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

} // namespace

int main() {
    nk_init_options init{};
    init.struct_size = sizeof(init);
    init.api_version = NK_API_VERSION;
    if (nk_init(&init) != NK_OK)
        return 1;

    nk_window window{};
    nk_surface surface{};
    nkgpu_renderer renderer{};
    int result = 0;
    bool window_created = false;
    bool surface_created = false;

    nk_window_options options{};
    options.struct_size = sizeof(options);
    options.width = 128;
    options.height = 96;
    options.title = "NativeKit scene render GPU test";
    if (nk_window_create(&options, &window) != NK_OK) {
        result = 2;
        goto cleanup;
    }
    window_created = true;
    if (nkgpu_surface_create(window, options.width, options.height, &surface) != NKGPU_OK) {
        result = 2;
        goto cleanup;
    }
    surface_created = true;
    if (!wait_for_surface(surface) || nkgpu_renderer_create(surface, &renderer) != NKGPU_OK) {
        result = 3;
        goto cleanup;
    }

    {
        auto scene = std::make_shared<Scene>();
        const auto geometry = scene->reserve_geometry_id();
        auto &geometry_resource = scene->geometry_store().create(geometry);
        geometry_resource.edit_payload().vertices = {
            {{{-0.6f, -0.6f, 0.0f}}},
            {{{0.6f, -0.6f, 0.0f}}},
            {{{0.0f, 0.6f, 0.0f}}}};
        const std::array<float, 9> normals = {
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f,
            0.0f, 0.0f, 1.0f};
        nkscene::GeometryVertexStream normal_stream;
        normal_stream.semantic = nkscene::VertexSemantic::Normal;
        normal_stream.format = nkscene::VertexFormat::Float32x3;
        normal_stream.stride = sizeof(float) * 3;
        normal_stream.count = 3;
        normal_stream.data.resize(sizeof(normals));
        std::memcpy(normal_stream.data.data(), normals.data(), sizeof(normals));
        const std::array<float, 6> texcoords = {
            0.0f, 0.0f,
            1.0f, 0.0f,
            0.5f, 1.0f};
        nkscene::GeometryVertexStream texcoord_stream;
        texcoord_stream.semantic = nkscene::VertexSemantic::Texcoord0;
        texcoord_stream.format = nkscene::VertexFormat::Float32x2;
        texcoord_stream.stride = sizeof(float) * 2;
        texcoord_stream.count = 3;
        texcoord_stream.data.resize(sizeof(texcoords));
        std::memcpy(texcoord_stream.data.data(), texcoords.data(), sizeof(texcoords));
        geometry_resource.edit_payload().streams = {normal_stream, texcoord_stream};
        geometry_resource.edit_payload().indices = {0, 1, 2};
        geometry_resource.edit_subelements().ranges.push_back({0, 1, 42});
        const auto image = scene->reserve_image_id();
        auto &image_resource = scene->image_store().create(image);
        image_resource.width = 1;
        image_resource.height = 1;
        image_resource.format = nkscene::ImageFormat::RGBA8;
        image_resource.data = {std::byte{255}, std::byte{128}, std::byte{64}, std::byte{255}};
        const auto texture = scene->reserve_texture_id();
        auto &texture_resource = scene->texture_store().create(texture);
        texture_resource.image = image;
        const auto sampler = scene->reserve_sampler_id();
        auto &sampler_resource = scene->sampler_store().create(sampler);
        sampler_resource.min_filter = nkscene::SamplerFilter::Nearest;
        sampler_resource.mag_filter = nkscene::SamplerFilter::Nearest;
        sampler_resource.wrap_u = nkscene::SamplerWrap::ClampToEdge;
        sampler_resource.wrap_v = nkscene::SamplerWrap::ClampToEdge;
        const auto light = scene->reserve_light_id();
        auto &light_resource = scene->light_store().create(light);
        light_resource.type = nkscene::LightType::Directional;
        light_resource.intensity = 1.25f;
        const auto material = scene->reserve_material_id();
        auto &material_resource = scene->material_store().create(material);
        material_resource.edit_state().base_color = {0.2f, 0.7f, 1.0f, 1.0f};
        material_resource.edit_state().base_color_texture = texture;
        material_resource.edit_state().sampler = sampler;

        Transaction create(scene);
        const auto occurrence = scene->reserve_occurrence_id();
        const auto second_occurrence = scene->reserve_occurrence_id();
        const auto light_occurrence = scene->reserve_occurrence_id();
        create.add_create(occurrence);
        create.add_create(second_occurrence);
        create.add_create(light_occurrence);
        ChangeSet changes;
        assert(scene->commit(create, changes) == NKS_OK);
        create.close();
        Transaction configure(scene);
        configure.add_geometry(occurrence, geometry);
        configure.add_material(occurrence, material);
        configure.add_geometry(second_occurrence, geometry);
        configure.add_material(second_occurrence, material);
        configure.add_source_entity(occurrence, nkscene::EntityId{42});
        configure.add_source_entity(second_occurrence, nkscene::EntityId{84});
        configure.add_light(light_occurrence, light);
        nkscene::LocalTransform first_transform;
        first_transform.matrix[12] = -0.8f;
        configure.add_transform(occurrence, first_transform);
        nkscene::LocalTransform second_transform;
        second_transform.matrix[12] = 0.8f;
        configure.add_transform(second_occurrence, second_transform);
        assert(scene->commit(configure, changes) == NKS_OK);
        configure.close();

        nkscene::SceneView view;
        auto plan = nkscene::compile(scene->snapshot(), view);
        nkscene::NativeKitGpuExecutor executor(renderer);
        auto stats = executor.execute(plan, scene->snapshot());
        assert(stats.result == NKGPU_OK);
        assert(stats.geometry_resources_created == 1);
        assert(stats.instance_buffers_created == 1);
        assert(stats.draw_calls == 1);

        nkscene::PickResult picked;
        assert(executor.pick_pixel(plan, scene->snapshot(), options.width, options.height,
                                  16, options.height / 2, &picked) == NKGPU_OK);
        assert(picked.occurrence == occurrence);
        assert(picked.source == nkscene::EntityId{42});
        assert(picked.subelement.value == 42);
        assert(executor.pick_pixel(plan, scene->snapshot(), options.width, options.height,
                                  112, options.height / 2, &picked) == NKGPU_OK);
        assert(picked.occurrence == second_occurrence);
        assert(picked.source == nkscene::EntityId{84});
        assert(picked.subelement.value == 42);
        nkscene::PickResult miss;
        assert(executor.pick_pixel(plan, scene->snapshot(), options.width, options.height, 0, 0,
                                  &miss) == NKGPU_OK);
        assert(!miss.occurrence.valid());

        std::shared_ptr<nkscene::GpuPickRequest> stale_request;
        assert(executor.begin_pick_pixel(plan, scene->snapshot(), options.width, options.height,
                                         16, options.height / 2, stale_request) == NKGPU_OK);
        nkscene::SceneView changed_view;
        changed_view.include_invisible = true;
        const auto changed_plan = nkscene::compile(scene->snapshot(), changed_view);
        nkscene::PickResult stale_result;
        nkgpu_result stale_error = NKGPU_OK;
        const auto stale_state = executor.poll_pick_pixel(
            *stale_request, changed_plan, scene->snapshot(), &stale_result, &stale_error);
        assert(stale_state == NKS_RENDER_PICK_STALE);
        assert(stale_error == NKGPU_OK);

        std::shared_ptr<nkscene::GpuPickRequest> async_request;
        assert(executor.begin_pick_pixel(plan, scene->snapshot(), options.width, options.height,
                                         16, options.height / 2, async_request) == NKGPU_OK);
        nkscene::PickResult async_picked;
        nkgpu_result async_error = NKGPU_OK;
        std::uint32_t async_state = NKS_RENDER_PICK_PENDING;
        for (int attempt = 0; attempt < 100 && async_state == NKS_RENDER_PICK_PENDING;
             ++attempt) {
            async_state = executor.poll_pick_pixel(
                *async_request, plan, scene->snapshot(), &async_picked, &async_error);
            if (async_state == NKS_RENDER_PICK_PENDING)
                std::this_thread::yield();
        }
        assert(async_state == NKS_RENDER_PICK_READY);
        assert(async_error == NKGPU_OK);
        assert(async_picked.occurrence == occurrence);
        assert(async_picked.source == nkscene::EntityId{42});
        assert(async_picked.subelement.value == 42);
        assert(std::abs(async_picked.worldPosition.x + 0.7421875f) < 0.05f);
        assert(std::abs(async_picked.worldPosition.z) < 0.001f);
        assert(std::abs(async_picked.depth - 0.5f) < 0.01f);

        nkscene::LocalTransform transform;
        transform.matrix[12] = 0.25f;
        Transaction move(scene);
        move.add_transform(occurrence, transform);
        assert(scene->commit(move, changes) == NKS_OK);
        move.close();
        const auto moved_snapshot = scene->snapshot();
        const auto update = nkscene::update(plan, moved_snapshot, changes, view);
        assert(!update.plan_rebuilt);
        stats = executor.execute(plan, scene->snapshot());
        assert(stats.result == NKGPU_OK);
        assert(stats.geometry_resources_created == 0);
        assert(stats.geometry_resources_updated == 0);
        assert(stats.instance_records_updated == 1);
        assert(stats.draw_calls == 1);

        Transaction change_source(scene);
        change_source.add_source_entity(second_occurrence, nkscene::EntityId{142});
        assert(scene->commit(change_source, changes) == NKS_OK);
        change_source.close();
        const auto source_snapshot = scene->snapshot();
        const auto source_update = nkscene::update(plan, source_snapshot, changes, view);
        assert(!source_update.plan_rebuilt);
        assert(source_update.patched_instances == 0);
        stats = executor.execute(plan, source_snapshot);
        assert(stats.result == NKGPU_OK);
        assert(stats.geometry_resources_created == 0);
        assert(stats.geometry_resources_updated == 0);
        assert(stats.instance_records_updated == 0);
        assert(executor.pick_pixel(plan, source_snapshot, options.width, options.height,
                                  112, options.height / 2, &picked) == NKGPU_OK);
        assert(picked.occurrence == second_occurrence);
        assert(picked.source == nkscene::EntityId{142});

        auto &updated_material = scene->material_store().create(material);
        updated_material.edit_state().base_color = {1.0f, 0.3f, 0.2f, 1.0f};
        scene->publish();
        stats = executor.execute(plan, scene->snapshot());
        assert(stats.result == NKGPU_OK);
        assert(stats.material_resources_updated == 1);

        auto &non_indexed_geometry = scene->geometry_store().create(geometry);
        non_indexed_geometry.edit_payload().indices.clear();
        scene->publish();
        stats = executor.execute(plan, scene->snapshot());
        assert(stats.result == NKGPU_OK);
        assert(stats.geometry_resources_created == 0);
        assert(stats.geometry_resources_updated == 1);
        assert(stats.draw_calls == 1);

        auto &indexed_geometry = scene->geometry_store().create(geometry);
        indexed_geometry.edit_payload().indices = {0, 1, 2};
        scene->publish();
        stats = executor.execute(plan, scene->snapshot());
        assert(stats.result == NKGPU_OK);
        assert(stats.geometry_resources_created == 0);
        assert(stats.geometry_resources_updated == 1);
        assert(stats.draw_calls == 1);

        const auto vertices = indexed_geometry.payload->vertices;
        const auto unused_geometry = scene->reserve_geometry_id();
        auto &unused_resource = scene->geometry_store().create(unused_geometry);
        unused_resource.edit_payload().vertices = vertices;
        unused_resource.edit_payload().indices = {0, 1, 2};
        scene->publish();
        stats = executor.execute(plan, scene->snapshot());
        assert(stats.result == NKGPU_OK);
        assert(stats.geometry_resources_created == 1);
        assert(stats.geometry_resources_updated == 0);
        assert(stats.draw_calls == 1);

        assert(scene->geometry_store().destroy(unused_geometry));
        scene->publish();
        stats = executor.execute(plan, scene->snapshot());
        assert(stats.result == NKGPU_OK);
        assert(stats.geometry_resources_created == 0);
        assert(stats.geometry_resources_updated == 0);
        assert(stats.draw_calls == 1);

        auto &recreated_geometry = scene->geometry_store().create(unused_geometry);
        recreated_geometry.edit_payload().vertices = vertices;
        recreated_geometry.edit_payload().indices.clear();
        scene->publish();
        stats = executor.execute(plan, scene->snapshot());
        assert(stats.result == NKGPU_OK);
        assert(stats.geometry_resources_created == 1);
        assert(stats.geometry_resources_updated == 0);
        assert(stats.draw_calls == 1);
    }

    {
        auto scene = std::make_shared<Scene>();
        const auto geometry = scene->reserve_geometry_id();
        auto &geometry_resource = scene->geometry_store().create(geometry);
        geometry_resource.edit_payload().vertices = {
            {{{-0.6f, -0.6f, 0.0f}}},
            {{{0.6f, -0.6f, 0.0f}}},
            {{{0.0f, 0.6f, 0.0f}}}};
        geometry_resource.edit_payload().indices = {0, 1, 2};
        geometry_resource.edit_subelements().ranges.push_back({0, 1, 7});
        const auto material = scene->reserve_material_id();
        auto &material_resource = scene->material_store().create(material);
        material_resource.edit_state().base_color = {0.8f, 0.8f, 0.8f, 1.0f};

        Transaction create(scene);
        const auto occurrence = scene->reserve_occurrence_id();
        create.add_create(occurrence);
        ChangeSet changes;
        assert(scene->commit(create, changes) == NKS_OK);
        create.close();
        Transaction configure(scene);
        configure.add_geometry(occurrence, geometry);
        configure.add_material(occurrence, material);
        assert(scene->commit(configure, changes) == NKS_OK);
        configure.close();

        nkscene::SceneView view;
        view.clip_planes.push_back({{{1.0f, 0.0f, 0.0f}}, 0.0f, true});
        auto plan = nkscene::compile(scene->snapshot(), view);
        assert(plan.clip_planes().size() == 1);
        assert(plan.visible_items() == 1);
        assert(plan.culled_items() == 0);

        nkscene::NativeKitGpuExecutor executor(renderer);
        const auto stats = executor.execute(plan, scene->snapshot());
        assert(stats.result == NKGPU_OK);
        assert(stats.draw_calls == 1);

        nkscene::PickResult clipped;
        assert(executor.pick_pixel(plan, scene->snapshot(), options.width, options.height,
                                  48, options.height / 2, &clipped) == NKGPU_OK);
        assert(!clipped.occurrence.valid());

        nkscene::PickResult visible;
        assert(executor.pick_pixel(plan, scene->snapshot(), options.width, options.height,
                                  80, options.height / 2, &visible) == NKGPU_OK);
        assert(visible.occurrence == occurrence);
        assert(visible.subelement.value == 7);
    }

cleanup:
    if (renderer.id)
        nkgpu_renderer_destroy(renderer);
    if (surface_created)
        nkgpu_surface_destroy(surface);
    if (window_created)
        nk_window_destroy(window);
    nk_shutdown();
    return result;
}
