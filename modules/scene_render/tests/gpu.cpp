#include "nativekit.h"
#include "nativekit_scene_render.h"
#include "nativekit_window.h"

#include "scene_internal.hpp"

#include <array>
#include <cassert>
#include <chrono>
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
        geometry_resource.payload.vertices = {
            {{{-0.6f, -0.6f, 0.0f}}},
            {{{0.6f, -0.6f, 0.0f}}},
            {{{0.0f, 0.6f, 0.0f}}}};
        geometry_resource.payload.indices = {0, 1, 2};
        geometry_resource.subelements.ranges.push_back({0, 1, 42});
        const auto material = scene->reserve_material_id();
        auto &material_resource = scene->material_store().create(material);
        material_resource.base_color = {0.2f, 0.7f, 1.0f, 1.0f};

        Transaction create(scene);
        const auto occurrence = scene->reserve_occurrence_id();
        const auto second_occurrence = scene->reserve_occurrence_id();
        create.add_create(occurrence);
        create.add_create(second_occurrence);
        ChangeSet changes;
        assert(scene->commit(create, changes) == NKS_OK);
        create.close();
        Transaction configure(scene);
        configure.add_geometry(occurrence, geometry);
        configure.add_material(occurrence, material);
        configure.add_geometry(second_occurrence, geometry);
        configure.add_material(second_occurrence, material);
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
        assert(picked.source == nkscene::EntityId{});
        assert(picked.subelement.value == 42);
        assert(executor.pick_pixel(plan, scene->snapshot(), options.width, options.height,
                                  112, options.height / 2, &picked) == NKGPU_OK);
        assert(picked.occurrence == second_occurrence);
        assert(picked.subelement.value == 42);
        nkscene::PickResult miss;
        assert(executor.pick_pixel(plan, scene->snapshot(), options.width, options.height, 0, 0,
                                  &miss) == NKGPU_OK);
        assert(!miss.occurrence.valid());

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

        auto &updated_material = scene->material_store().create(material);
        updated_material.base_color = {1.0f, 0.3f, 0.2f, 1.0f};
        stats = executor.execute(plan, scene->snapshot());
        assert(stats.result == NKGPU_OK);
        assert(stats.material_resources_updated == 1);
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
