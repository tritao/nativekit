#include "nativekit.h"
#include "nativekit_scene_interaction.h"
#include "nativekit_window.h"

#include "scene_internal.hpp"

#include <cassert>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>

namespace {

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
    options.width = 64;
    options.height = 64;
    options.title = "NativeKit scene interaction GPU test";
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
        auto scene = std::make_shared<nkscene::Scene>();
        const auto geometry = scene->reserve_geometry_id();
        auto &geometry_resource = scene->geometry_store().create(geometry);
        geometry_resource.payload.vertices = {
            {{{-0.6f, -0.6f, 0.0f}}},
            {{{0.6f, -0.6f, 0.0f}}},
            {{{0.0f, 0.6f, 0.0f}}}};
        geometry_resource.subelements.ranges.push_back({0, 1, 42});
        const auto material = scene->reserve_material_id();
        scene->material_store().create(material);
        const auto occurrence = scene->reserve_occurrence_id();
        nkscene::Transaction create(scene);
        create.add_create(occurrence);
        nkscene::ChangeSet changes;
        assert(scene->commit(create, changes) == NKS_OK);
        create.close();
        nkscene::Transaction configure(scene);
        configure.add_geometry(occurrence, geometry);
        configure.add_material(occurrence, material);
        assert(scene->commit(configure, changes) == NKS_OK);
        configure.close();

        const auto snapshot = scene->snapshot();
        const auto plan = nkscene::compile(snapshot, {});
        nkscene::NativeKitGpuExecutor executor(renderer);
        nkscene::SceneInteraction interaction;
        assert(interaction.request_hover(executor, plan, snapshot, 64, 64, 32, 32) ==
               NKGPU_OK);
        nkgpu_result error = NKGPU_OK;
        auto state = nkscene::InteractionHoverState::Pending;
        for (int attempt = 0; attempt < 100 &&
             state == nkscene::InteractionHoverState::Pending; ++attempt) {
            state = interaction.poll_hover(executor, plan, snapshot, error);
            if (state == nkscene::InteractionHoverState::Pending)
                std::this_thread::yield();
        }
        assert(state == nkscene::InteractionHoverState::Ready);
        assert(error == NKGPU_OK);
        assert(interaction.hovered().has_value());
        assert(interaction.hovered()->occurrence == occurrence);
        assert(interaction.hovered()->source == nkscene::EntityId{});

        nkscene::PickResult picked = *interaction.hovered();
        interaction.apply_pick(picked, nkscene::SelectionMode::Replace);
        assert(interaction.selected().size() == 1);
        assert(interaction.is_selected(occurrence));

        nkscene::SceneView changed_view;
        changed_view.include_invisible = true;
        const auto changed_plan = nkscene::compile(snapshot, changed_view);
        assert(interaction.request_hover(executor, plan, snapshot, 64, 64, 32, 32) ==
               NKGPU_OK);
        state = interaction.poll_hover(executor, changed_plan, snapshot, error);
        assert(state == nkscene::InteractionHoverState::Stale);
        assert(interaction.hovered().has_value());
        assert(interaction.is_selected(occurrence));
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
