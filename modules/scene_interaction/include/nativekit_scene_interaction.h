#pragma once

/* ------------------------------------------------------------------------- */
/* Dependencies                                                              */
/* ------------------------------------------------------------------------- */

#include "nativekit_scene_render.h"

/* ------------------------------------------------------------------------- */
/* Export visibility                                                         */
/* ------------------------------------------------------------------------- */

#if defined(_WIN32)
#if defined(NK_STATIC)
#define NKSINTERACTION_API
#elif defined(NKSINTERACTION_BUILDING_LIBRARY)
#define NKSINTERACTION_API __declspec(dllexport)
#else
#define NKSINTERACTION_API __declspec(dllimport)
#endif
#else
#define NKSINTERACTION_API __attribute__((visibility("default")))
#endif

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Runtime handles and interaction state                                     */
/* ------------------------------------------------------------------------- */

typedef uint32_t nkscene_interaction NK_HANDLE
    NK_HANDLE_DESTROY(nkscene_interaction_destroy);

enum {
    NKS_INTERACTION_SELECTION_REPLACE = 0,
    NKS_INTERACTION_SELECTION_ADD = 1,
    NKS_INTERACTION_SELECTION_TOGGLE = 2
};

enum {
    NKS_INTERACTION_HOVER_IDLE = 0,
    NKS_INTERACTION_HOVER_PENDING = 1,
    NKS_INTERACTION_HOVER_READY = 2,
    NKS_INTERACTION_HOVER_STALE = 3,
    NKS_INTERACTION_HOVER_FAILED = 4
};

/* ------------------------------------------------------------------------- */
/* Interaction lifecycle                                                     */
/* ------------------------------------------------------------------------- */

NKSINTERACTION_API nkscene_result NKS_CALL nkscene_interaction_create(
    nkscene_interaction *out_interaction NK_OUT NK_OWNED);
NKSINTERACTION_API void NKS_CALL nkscene_interaction_destroy(
    nkscene_interaction interaction);

/* ------------------------------------------------------------------------- */
/* Asynchronous hover                                                        */
/* ------------------------------------------------------------------------- */

NKSINTERACTION_API nkscene_result NKS_CALL nkscene_interaction_request_hover(
    nkscene_interaction interaction, nkscene_render_executor executor,
    nkscene_render_plan plan, nkscene_snapshot snapshot, uint32_t width,
    uint32_t height, uint32_t x, uint32_t y);
NKSINTERACTION_API nkscene_result NKS_CALL nkscene_interaction_poll_hover(
    nkscene_interaction interaction, nkscene_render_executor executor,
    nkscene_render_plan plan, nkscene_snapshot snapshot, uint32_t *out_state NK_OUT,
    nkgpu_result *out_error NK_OUT, nkscene_render_pick_result *out_result NK_OUT);
NKSINTERACTION_API void NKS_CALL nkscene_interaction_cancel_hover(
    nkscene_interaction interaction);

/* ------------------------------------------------------------------------- */
/* Selection state                                                           */
/* ------------------------------------------------------------------------- */

NKSINTERACTION_API nkscene_result NKS_CALL nkscene_interaction_apply_pick(
    nkscene_interaction interaction, const nkscene_render_pick_result *pick,
    uint32_t mode);
NKSINTERACTION_API nkscene_result NKS_CALL nkscene_interaction_select(
    nkscene_interaction interaction, nkscene_occurrence_id occurrence, uint32_t mode);
NKSINTERACTION_API void NKS_CALL nkscene_interaction_clear_selection(
    nkscene_interaction interaction);
NKSINTERACTION_API nkscene_result NKS_CALL nkscene_interaction_synchronize(
    nkscene_interaction interaction, nkscene_snapshot snapshot);
NKSINTERACTION_API nkscene_result NKS_CALL nkscene_interaction_get_hover(
    nkscene_interaction interaction, uint32_t *out_has_hover NK_OUT,
    nkscene_render_pick_result *out_result NK_OUT);
NKSINTERACTION_API nkscene_result NKS_CALL nkscene_interaction_get_selection_count(
    nkscene_interaction interaction, uint64_t *out_count NK_OUT);
NKSINTERACTION_API nkscene_result NKS_CALL nkscene_interaction_get_selected(
    nkscene_interaction interaction, uint64_t index,
    nkscene_occurrence_id *out_occurrence NK_OUT);
NKSINTERACTION_API nkscene_result NKS_CALL nkscene_interaction_is_selected(
    nkscene_interaction interaction, nkscene_occurrence_id occurrence,
    uint32_t *out_selected NK_OUT);

#ifdef __cplusplus
}

/* ------------------------------------------------------------------------- */
/* C++ interaction API                                                       */
/* ------------------------------------------------------------------------- */

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace nkscene {

enum class SelectionMode : std::uint32_t {
    Replace = NKS_INTERACTION_SELECTION_REPLACE,
    Add = NKS_INTERACTION_SELECTION_ADD,
    Toggle = NKS_INTERACTION_SELECTION_TOGGLE
};

enum class InteractionHoverState : std::uint32_t {
    Idle = NKS_INTERACTION_HOVER_IDLE,
    Pending = NKS_INTERACTION_HOVER_PENDING,
    Ready = NKS_INTERACTION_HOVER_READY,
    Stale = NKS_INTERACTION_HOVER_STALE,
    Failed = NKS_INTERACTION_HOVER_FAILED
};

/**
 * Backend-neutral hover and selection state for one scene presentation.
 *
 * The interaction object never mutates a Scene. The caller supplies the
 * current immutable snapshot and render plan when polling asynchronous hover.
 */
class NKSINTERACTION_API SceneInteraction {
public:
    SceneInteraction() = default;
    ~SceneInteraction();
    SceneInteraction(SceneInteraction &&) noexcept;
    SceneInteraction &operator=(SceneInteraction &&) noexcept;
    SceneInteraction(const SceneInteraction &) = delete;
    SceneInteraction &operator=(const SceneInteraction &) = delete;

    nkgpu_result request_hover(NativeKitGpuExecutor &, const RenderPlan &,
                               const SceneSnapshot &, std::uint32_t width,
                               std::uint32_t height, std::uint32_t x, std::uint32_t y);
    InteractionHoverState poll_hover(NativeKitGpuExecutor &, const RenderPlan &,
                                      const SceneSnapshot &, nkgpu_result &out_error);
    void cancel_hover() noexcept;

    void apply_pick(const PickResult &, SelectionMode mode);
    void select(OccurrenceId, SelectionMode mode);
    void clear_selection() noexcept;
    void synchronize(const SceneSnapshot &);

    std::optional<PickResult> hovered() const;
    std::span<const OccurrenceId> selected() const noexcept;
    bool is_selected(OccurrenceId) const noexcept;

private:
    std::shared_ptr<GpuPickRequest> hover_request_;
    std::optional<PickResult> hovered_;
    std::vector<OccurrenceId> selected_;
};

} // namespace nkscene

#endif /* __cplusplus */
