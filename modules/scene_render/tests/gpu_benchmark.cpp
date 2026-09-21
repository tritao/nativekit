#include "nativekit.h"
#include "nativekit_scene_render.h"
#include "nativekit_window.h"

#include "scene_internal.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using nkscene::ChangeSet;
using nkscene::RenderPlan;
using nkscene::Scene;
using nkscene::Transaction;

bool wait_for_surface(nk_surface surface) {
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (Clock::now() < deadline) {
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

nkscene::LocalTransform translated(float x) {
    nkscene::LocalTransform transform;
    transform.matrix[12] = x;
    return transform;
}

void print_stats(const char *name, const nkscene::GpuExecutionStats &stats,
                 const nkscene::RenderUpdate &update, double milliseconds) {
    std::printf(
        "%-10s %8.3f ms  plan(rebuild=%d instances=%zu) "
        "gpu(geometry=%zu/%zu materials=%zu/%zu buffers=%zu records=%zu draws=%zu)\n",
        name, milliseconds, update.plan_rebuilt, update.patched_instances,
        stats.geometry_resources_created, stats.geometry_resources_updated,
        stats.material_resources_created, stats.material_resources_updated,
        stats.instance_buffers_created, stats.instance_records_updated, stats.draw_calls);
}

} // namespace

int main() {
    constexpr std::size_t occurrence_count = 50000;
    constexpr std::size_t group_count = 50;
    constexpr std::size_t leaf_count = occurrence_count - group_count;
    constexpr std::uint32_t width = 128;
    constexpr std::uint32_t height = 96;

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
    options.width = width;
    options.height = height;
    options.title = "NativeKit scene render GPU benchmark";
    if (nk_window_create(&options, &window) != NK_OK) {
        result = 2;
        goto cleanup;
    }
    window_created = true;
    if (nkgpu_surface_create(window, width, height, &surface) != NKGPU_OK) {
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
            nkscene::GeometryVertex{{-0.05f, -0.05f, 0.0f}},
            nkscene::GeometryVertex{{0.05f, -0.05f, 0.0f}},
            nkscene::GeometryVertex{{0.0f, 0.05f, 0.0f}}};
        geometry_resource.edit_payload().indices = {0, 1, 2};

        std::vector<nkscene::MaterialId> materials;
        materials.reserve(4);
        for (int index = 0; index < 4; ++index) {
            const auto material = scene->reserve_material_id();
            materials.push_back(material);
            scene->material_store().create(material);
        }

        std::vector<nkscene::OccurrenceId> groups;
        std::vector<nkscene::OccurrenceId> leaves;
        groups.reserve(group_count);
        leaves.reserve(leaf_count);
        Transaction create(scene);
        for (std::size_t index = 0; index < group_count; ++index) {
            const auto id = scene->reserve_occurrence_id();
            groups.push_back(id);
            create.add_create(id);
        }
        for (std::size_t index = 0; index < leaf_count; ++index) {
            const auto id = scene->reserve_occurrence_id();
            leaves.push_back(id);
            create.add_create(id);
        }
        ChangeSet changes;
        assert(scene->commit(create, changes) == NKS_OK);
        create.close();

        Transaction configure(scene);
        for (std::size_t index = 0; index < leaf_count; ++index) {
            configure.add_parent(leaves[index], groups[index % groups.size()]);
            configure.add_geometry(leaves[index], geometry);
            configure.add_material(leaves[index], materials[index % materials.size()]);
        }
        assert(scene->commit(configure, changes) == NKS_OK);
        configure.close();

        nkscene::SceneView view;
        auto plan = nkscene::compile(scene->snapshot(), view);
        assert(plan.items().size() == leaf_count);
        assert(plan.batches().size() == materials.size());

        nkscene::NativeKitGpuExecutor executor(renderer);
        const auto initial_start = Clock::now();
        auto stats = executor.execute(plan, scene->snapshot());
        const auto initial_time = std::chrono::duration<double, std::milli>(
            Clock::now() - initial_start);
        assert(stats.result == NKGPU_OK);
        assert(stats.geometry_resources_created == 1);
        assert(stats.geometry_resources_updated == 0);
        assert(stats.material_resources_created == materials.size());
        assert(stats.material_resources_updated == 0);
        assert(stats.instance_buffers_created == materials.size());
        assert(stats.instance_records_updated == 0);
        assert(stats.commands == leaf_count);
        assert(stats.draw_calls == materials.size());
        print_stats("initial", stats, {}, initial_time.count());

        Transaction move_one(scene);
        move_one.add_transform(leaves[0], translated(1.0f));
        assert(scene->commit(move_one, changes) == NKS_OK);
        move_one.close();
        const auto move_one_start = Clock::now();
        auto update = nkscene::update(plan, scene->snapshot(), changes, view);
        stats = executor.execute(plan, scene->snapshot());
        const auto move_one_time = std::chrono::duration<double, std::milli>(
            Clock::now() - move_one_start);
        assert(!update.plan_rebuilt);
        assert(!update.geometry_rebuilt);
        assert(update.patched_instances == 1);
        assert(update.updated_geometry_resources == 0);
        assert(update.updated_material_resources == 0);
        assert(stats.result == NKGPU_OK);
        assert(stats.geometry_resources_created == 0);
        assert(stats.geometry_resources_updated == 0);
        assert(stats.material_resources_created == 0);
        assert(stats.material_resources_updated == 0);
        assert(stats.instance_buffers_created == 0);
        assert(stats.instance_records_updated == 1);
        assert(stats.commands == leaf_count);
        assert(stats.draw_calls == materials.size());
        print_stats("move one", stats, update, move_one_time.count());

        Transaction move_hundred(scene);
        for (std::size_t index = 0; index < 100; ++index)
            move_hundred.add_transform(leaves[index], translated(0.5f + index * 0.001f));
        assert(scene->commit(move_hundred, changes) == NKS_OK);
        move_hundred.close();
        const auto move_hundred_start = Clock::now();
        update = nkscene::update(plan, scene->snapshot(), changes, view);
        stats = executor.execute(plan, scene->snapshot());
        const auto move_hundred_time = std::chrono::duration<double, std::milli>(
            Clock::now() - move_hundred_start);
        assert(!update.plan_rebuilt);
        assert(!update.geometry_rebuilt);
        assert(update.patched_instances == 100);
        assert(update.updated_geometry_resources == 0);
        assert(update.updated_material_resources == 0);
        assert(stats.result == NKGPU_OK);
        assert(stats.geometry_resources_created == 0);
        assert(stats.geometry_resources_updated == 0);
        assert(stats.material_resources_created == 0);
        assert(stats.material_resources_updated == 0);
        assert(stats.instance_buffers_created == 0);
        assert(stats.instance_records_updated == 100);
        assert(stats.commands == leaf_count);
        assert(stats.draw_calls == materials.size());
        print_stats("move 100", stats, update, move_hundred_time.count());

        auto &updated_material = scene->material_store().create(materials[0]);
        updated_material.edit_state().base_color = {0.2f, 0.8f, 0.4f, 1.0f};
        scene->publish();
        const auto material_start = Clock::now();
        stats = executor.execute(plan, scene->snapshot());
        const auto material_time = std::chrono::duration<double, std::milli>(
            Clock::now() - material_start);
        assert(stats.result == NKGPU_OK);
        assert(stats.geometry_resources_created == 0);
        assert(stats.geometry_resources_updated == 0);
        assert(stats.material_resources_created == 0);
        assert(stats.material_resources_updated == 1);
        assert(stats.instance_buffers_created == 0);
        assert(stats.instance_records_updated == 0);
        assert(stats.commands == leaf_count);
        assert(stats.draw_calls == materials.size());
        print_stats("material", stats, {}, material_time.count());

        auto &updated_geometry = scene->geometry_store().create(geometry);
        updated_geometry.edit_payload().vertices[0].position[0] = -0.04f;
        scene->publish();
        const auto geometry_start = Clock::now();
        stats = executor.execute(plan, scene->snapshot());
        const auto geometry_time = std::chrono::duration<double, std::milli>(
            Clock::now() - geometry_start);
        assert(stats.result == NKGPU_OK);
        assert(stats.geometry_resources_created == 0);
        assert(stats.geometry_resources_updated == 1);
        assert(stats.material_resources_created == 0);
        assert(stats.material_resources_updated == 0);
        assert(stats.instance_buffers_created == 0);
        assert(stats.instance_records_updated == 0);
        assert(stats.commands == leaf_count);
        assert(stats.draw_calls == materials.size());
        print_stats("geometry", stats, {}, geometry_time.count());
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
