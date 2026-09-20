#include "nativekit_scene_interaction.h"

#include "scene_internal.hpp"

#include <algorithm>
#include <cassert>
#include <memory>
#include <vector>

namespace {

using nkscene::ChangeSet;
using nkscene::Scene;
using nkscene::SceneInteraction;
using nkscene::SceneView;
using nkscene::SelectionMode;
using nkscene::Transaction;

void cpp_selection_state() {
    SceneInteraction interaction;
    const nkscene::OccurrenceId first{1};
    const nkscene::OccurrenceId second{2};

    interaction.select(first, SelectionMode::Replace);
    interaction.select(second, SelectionMode::Add);
    assert(interaction.selected().size() == 2);
    assert(interaction.is_selected(first));
    assert(interaction.is_selected(second));

    interaction.select(first, SelectionMode::Toggle);
    assert(interaction.selected().size() == 1);
    assert(!interaction.is_selected(first));
    assert(interaction.is_selected(second));

    nkscene::PickResult pick;
    pick.occurrence = first;
    pick.source = nkscene::EntityId{42};
    pick.subelement = {7};
    interaction.apply_pick(pick, SelectionMode::Replace);
    assert(interaction.hovered().has_value());
    assert(interaction.hovered()->source == nkscene::EntityId{42});
    assert(interaction.selected().size() == 1);
    assert(interaction.selected().front() == first);

    const auto scene = std::make_shared<Scene>();
    const auto live = scene->reserve_occurrence_id();
    const auto removed = scene->reserve_occurrence_id();
    Transaction create(scene);
    create.add_create(live);
    create.add_create(removed);
    ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();

    interaction.select(removed, SelectionMode::Add);
    interaction.synchronize(scene->snapshot());
    assert(interaction.is_selected(removed));

    Transaction destroy(scene);
    destroy.add_destroy(removed);
    assert(scene->commit(destroy, changes) == NKS_OK);
    destroy.close();
    interaction.synchronize(scene->snapshot());
    assert(!interaction.is_selected(removed));
    assert(interaction.is_selected(first));
}

void c_selection_state() {
    nkscene_interaction interaction = 0;
    assert(nkscene_interaction_create(&interaction) == NKS_OK);

    const nkscene_occurrence_id first = {11};
    const nkscene_occurrence_id second = {22};
    assert(nkscene_interaction_select(
               interaction, first, NKS_INTERACTION_SELECTION_REPLACE) == NKS_OK);
    assert(nkscene_interaction_select(
               interaction, second, NKS_INTERACTION_SELECTION_ADD) == NKS_OK);
    uint64_t count = 0;
    assert(nkscene_interaction_get_selection_count(interaction, &count) == NKS_OK);
    assert(count == 2);

    assert(nkscene_interaction_select(
               interaction, first, NKS_INTERACTION_SELECTION_TOGGLE) == NKS_OK);
    assert(nkscene_interaction_get_selection_count(interaction, &count) == NKS_OK);
    assert(count == 1);
    nkscene_occurrence_id selected = {};
    assert(nkscene_interaction_get_selected(interaction, 0, &selected) == NKS_OK);
    assert(selected.value == second.value);

    nkscene_render_pick_result pick = {};
    pick.occurrence = first;
    pick.source.value = 42;
    assert(nkscene_interaction_apply_pick(
               interaction, &pick, NKS_INTERACTION_SELECTION_REPLACE) == NKS_OK);
    uint32_t has_hover = 0;
    nkscene_render_pick_result hovered = {};
    assert(nkscene_interaction_get_hover(interaction, &has_hover, &hovered) == NKS_OK);
    assert(has_hover == 1);
    assert(hovered.occurrence.value == first.value);

    nkscene_scene scene = 0;
    assert(nkscene_scene_create(&scene) == NKS_OK);
    nkscene_transaction transaction = 0;
    assert(nkscene_transaction_begin(scene, &transaction) == NKS_OK);
    nkscene_occurrence_id live = {};
    nkscene_occurrence_id removed = {};
    assert(nkscene_tx_create_occurrence(transaction, &live) == NKS_OK);
    assert(nkscene_tx_create_occurrence(transaction, &removed) == NKS_OK);
    assert(nkscene_transaction_commit(transaction) == NKS_OK);

    assert(nkscene_interaction_select(
               interaction, live, NKS_INTERACTION_SELECTION_ADD) == NKS_OK);
    assert(nkscene_interaction_select(
               interaction, removed, NKS_INTERACTION_SELECTION_ADD) == NKS_OK);
    nkscene_snapshot snapshot = 0;
    assert(nkscene_scene_snapshot(scene, &snapshot) == NKS_OK);
    assert(nkscene_interaction_synchronize(interaction, snapshot) == NKS_OK);
    nkscene_snapshot_destroy(snapshot);

    assert(nkscene_transaction_begin(scene, &transaction) == NKS_OK);
    assert(nkscene_tx_destroy_occurrence(transaction, removed) == NKS_OK);
    assert(nkscene_transaction_commit(transaction) == NKS_OK);
    assert(nkscene_scene_snapshot(scene, &snapshot) == NKS_OK);
    assert(nkscene_interaction_synchronize(interaction, snapshot) == NKS_OK);
    assert(nkscene_interaction_is_selected(interaction, removed, &has_hover) == NKS_OK);
    assert(has_hover == 0);
    nkscene_snapshot_destroy(snapshot);
    nkscene_scene_destroy(scene);

    nkscene_interaction_clear_selection(interaction);
    assert(nkscene_interaction_get_selection_count(interaction, &count) == NKS_OK);
    assert(count == 0);
    nkscene_interaction_destroy(interaction);
    assert(nkscene_interaction_get_selection_count(interaction, &count) ==
           NKS_ERROR_INVALID_HANDLE);
}

void selection_presentation_50k() {
    constexpr std::size_t count = 50000;
    auto scene = std::make_shared<Scene>();
    const auto geometry = scene->reserve_geometry_id();
    scene->geometry_store().create(geometry);
    const auto base_material = scene->reserve_material_id();
    scene->material_store().create(base_material);
    const auto highlight_material = scene->reserve_material_id();
    scene->material_store().create(highlight_material);
    const auto hover_material = scene->reserve_material_id();
    scene->material_store().create(hover_material);

    std::vector<nkscene::OccurrenceId> occurrences;
    occurrences.reserve(count);
    Transaction create(scene);
    for (std::size_t index = 0; index < count; ++index) {
        const auto occurrence = scene->reserve_occurrence_id();
        occurrences.push_back(occurrence);
        create.add_create(occurrence);
    }
    ChangeSet changes;
    assert(scene->commit(create, changes) == NKS_OK);
    create.close();

    Transaction configure(scene);
    for (const auto occurrence : occurrences) {
        configure.add_geometry(occurrence, geometry);
        configure.add_material(occurrence, base_material);
    }
    assert(scene->commit(configure, changes) == NKS_OK);
    configure.close();

    const auto snapshot = scene->snapshot();
    SceneView view;
    auto plan = nkscene::compile(snapshot, view);
    const auto compile_count = plan.compile_count();

    SceneInteraction interaction;
    interaction.select(occurrences[12345], SelectionMode::Replace);
    auto selected_view = view;
    for (const auto occurrence : interaction.selected())
        selected_view.set_selection_material_override(occurrence, highlight_material);
    selected_view.set_selection_material_override(occurrences[12345], hover_material);
    selected_view.set_selection_material_override(occurrences[12345], highlight_material);
    assert(selected_view.selection_material_overrides.size() == 1);
    const auto update = nkscene::update(plan, snapshot, changes, selected_view);

    assert(!update.plan_rebuilt);
    assert(plan.compile_count() == compile_count);
    assert(update.patched_instances == 0);
    assert(update.patched_visibility == 0);
    assert(update.patched_materials == 1);
    assert(update.updated_geometry_resources == 0);
    assert(update.updated_material_resources == 0);
    assert(update.rebuilt_batches != 0);

    const auto selected_item = std::find_if(
        plan.items().begin(), plan.items().end(), [&](const auto &item) {
            return item.occurrence == occurrences[12345];
        });
    assert(selected_item != plan.items().end());
    assert(selected_item->material == highlight_material);

    auto hovered_view = selected_view;
    hovered_view.set_hover_material_override(occurrences[12345], hover_material);
    hovered_view.set_hover_material_override(occurrences[12345], hover_material);
    assert(hovered_view.hover_material_overrides.size() == 1);
    const auto hover_update = nkscene::update(plan, snapshot, changes, hovered_view);
    assert(!hover_update.plan_rebuilt);
    assert(hover_update.patched_instances == 0);
    assert(hover_update.patched_visibility == 0);
    assert(hover_update.patched_materials == 1);
    assert(selected_item->material == hover_material);

    const auto selection_update = nkscene::update(plan, snapshot, changes, selected_view);
    assert(!selection_update.plan_rebuilt);
    assert(selection_update.patched_instances == 0);
    assert(selection_update.patched_visibility == 0);
    assert(selection_update.patched_materials == 1);
    assert(selected_item->material == highlight_material);
}

} // namespace

int main() {
    cpp_selection_state();
    c_selection_state();
    selection_presentation_50k();
    return 0;
}
