#include "nativekit_scene_interaction.h"

#include "scene_internal.hpp"

#include <cassert>
#include <memory>

namespace {

using nkscene::ChangeSet;
using nkscene::Scene;
using nkscene::SceneInteraction;
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

} // namespace

int main() {
    cpp_selection_state();
    c_selection_state();
    return 0;
}
