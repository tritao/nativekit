#include "nativekit_ui.h"
#include "nativekit_ui_layout.h"

#include "nativekit_graphics.h"
#include "nativekit_gpu.h"
#include "image_decode.h"

#include "core/executor.hpp"
#include "core/frame_backend.hpp"
#include "core/runtime.hpp"

#include "display_list/display_list.h"
#include "compositor/compositor.h"
#include "prepare/image_pixels.h"
#include "layout/layout_engine.h"
#include "layout/layout_render_compiler.h"
#include "prepare/nanovg_path.h"
#include "prepare/skribidi_adapter.h"
#if defined(NKUI_ENABLE_SHOWCASE_PRODUCER)
#include "render/cube_surface_producer.h"
#endif
#include "render/frame_resources.h"
#include "render/render_plan_executor.h"
#include "render/ui_renderer.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

static_assert(sizeof(nkui_command_header) == sizeof(nkui::CommandHeader));
static_assert(sizeof(nkui_text_metrics) == 5 * sizeof(uint32_t));
static_assert(sizeof(nkui_text_position) == 2 * sizeof(uint32_t));
static_assert(sizeof(nkui_text_caret) == 7 * sizeof(uint32_t));
static_assert(sizeof(nkui_text_rect) == 5 * sizeof(uint32_t));
static_assert(sizeof(nkui_text_style) == 4 * sizeof(uint32_t));
static_assert(sizeof(nkui_paragraph_style) == 5 * sizeof(uint32_t));
static_assert(sizeof(nkui_layout_frame_input) == 4 * sizeof(uint32_t));
static_assert(sizeof(nkui_layout_measure_constraints) == 5 * sizeof(uint32_t));
static_assert(sizeof(nkui_layout_measure_result) == 5 * sizeof(uint32_t));
static_assert(sizeof(nkui_path_element) == 7 * sizeof(uint32_t));
static_assert(sizeof(nkui_color) == 4 * sizeof(uint32_t));
static_assert(sizeof(nkui_gradient_stop) == 5 * sizeof(uint32_t));
static_assert(nkui::kMaxPreparedGradientStops == NKUI_GRADIENT_MAX_STOPS);
static_assert(sizeof(nkui_transform_command) == sizeof(nkui::SetTransformCommand));
static_assert(sizeof(nkui_resource_command) == sizeof(nkui::DrawResourceCommand));
static_assert(sizeof(nkui_scalar_command) == sizeof(nkui::SetGlobalAlphaCommand));
static_assert(sizeof(nkui_composite_command) == sizeof(nkui::SetCompositeModeCommand));
static_assert(sizeof(nkui_rect_command) == sizeof(nkui::ClipRectCommand));
static_assert(sizeof(nkui_draw_rect_command) == sizeof(nkui::DrawRectResourceCommand));
static_assert(sizeof(nkui_layer_command) == sizeof(nkui::BeginLayerCommand));
static_assert(sizeof(nkui_layer_v1_unbounded_command) ==
              sizeof(nkui::BeginLayerUnboundedV1Command));
static_assert(sizeof(nkui_layer_v1_command) == sizeof(nkui::BeginLayerV1Command));
static_assert(sizeof(nkui_layer_v1_effect_command) == sizeof(nkui::BeginLayerEffectV1Command));
static_assert(sizeof(nkui_layer_v1_mask_command) == sizeof(nkui::BeginLayerMaskV1Command));
static_assert(sizeof(nkui_layer_v1_backdrop_command) == sizeof(nkui::BeginLayerBackdropV1Command));
static_assert(sizeof(nkui_layer_v1_custom_effect_command) ==
              sizeof(nkui::BeginLayerCustomEffectV1Command));
static_assert(sizeof(nkui_custom_effect_descriptor) == sizeof(nkui::CustomEffectDescriptor));
static_assert(sizeof(nkui_effect_op_command) == sizeof(nkui::EffectOpCommand));
static_assert(sizeof(nkui_stroke_path_command) == sizeof(nkui::StrokePathCommand));
static_assert(sizeof(nkui_layout_item) == NKUI_LAYOUT_RESOLVED_ITEM_BYTES);

namespace {

struct DisplayListSlot {
    std::unique_ptr<nkui::DisplayList> list;
    std::vector<nkui::ResourceId> resources;
    uint32_t custom_refs = 0;
    uint16_t generation = 1;
};

struct FontEntry {
    std::string path;
    nkui::FontFamily family = nkui::FontFamily::Default;
    std::shared_ptr<std::vector<uint8_t>> data;
};

struct ResourceSlot {
    nkui::ResourceKind kind{};
    uint16_t generation = 1;
    bool externally_alive = true;
    uint32_t display_refs = 0;
    std::vector<FontEntry> fonts;
    std::shared_ptr<nkui::SkribidiFontCollection> font_collection;
    bool system_fallbacks = false;
    std::shared_ptr<nkui::SkribidiAdapter> text;
    std::unique_ptr<nkui::SurfaceProducer> surface;
    nk_graphics_image graphics_image{};
    nkui::PreparedGlyphs text_glyphs;
    std::unordered_map<int32_t, nkui::PreparedGlyphs> scaled_text_glyphs;
    float text_width = 0.0f;
    nkui::TextLayoutOptions text_options{};
    std::unique_ptr<nkui::NanoVGPath> path;
    nkui_color color{};
    nkui::PreparedPaintKind paint_kind = nkui::PreparedPaintKind::Solid;
    std::array<float, 2> gradient_start{};
    std::array<float, 2> gradient_end{};
    std::vector<nkui_gradient_stop> gradient_stops;
    uint32_t image_width = 0;
    uint32_t image_height = 0;
    nkui_image_format image_format = NKUI_IMAGE_FORMAT_INVALID;
    nkui_image_filter image_filter = NKUI_IMAGE_FILTER_LINEAR;
    std::vector<uint8_t> pixels;
};

struct PathCacheKey {
    uint32_t path = 0;
    std::array<uint32_t, 6> transform{};
    uint32_t pixel_scale = 0;
    uint32_t kind = 0;
    uint32_t stroke_width = 0;
    uint32_t line_cap = 0;
    uint32_t line_join = 0;
    uint32_t miter_limit = 0;

    bool operator==(const PathCacheKey &other) const {
        return path == other.path && transform == other.transform &&
               pixel_scale == other.pixel_scale && kind == other.kind &&
               stroke_width == other.stroke_width && line_cap == other.line_cap &&
               line_join == other.line_join && miter_limit == other.miter_limit;
    }
};

struct PathCacheKeyHash {
    size_t operator()(const PathCacheKey &key) const {
        size_t hash = key.path * 0x9E3779B1u;
        for (const uint32_t value : key.transform)
            hash = (hash * 0x9E3779B1u) ^ value;
        hash = (hash * 0x9E3779B1u) ^ key.pixel_scale;
        hash = (hash * 0x9E3779B1u) ^ key.kind;
        hash = (hash * 0x9E3779B1u) ^ key.stroke_width;
        hash = (hash * 0x9E3779B1u) ^ key.line_cap;
        hash = (hash * 0x9E3779B1u) ^ key.line_join;
        return (hash * 0x9E3779B1u) ^ key.miter_limit;
    }
};

struct PreparedPathCacheEntry {
    std::shared_ptr<const nkui::PreparedGeometry> geometry;
};

struct CustomEffectRegistrationStorage {
    uint32_t registration_id = 0;
    std::string name;
    std::string glsl410_fragment;
    std::string glsl300es_fragment;
    std::string hlsl5_fragment;
    std::string metal_macos_fragment;
    uint32_t parameter_components = 0;
    uint32_t pass_count = 1;
    uint32_t sampling_inputs = 1;
    std::array<float, 4> ink_overflow{};

    nkui::CustomEffectRegistration native() const {
        return {registration_id,
                name.c_str(),
                glsl410_fragment.empty() ? nullptr : glsl410_fragment.c_str(),
                glsl300es_fragment.empty() ? nullptr : glsl300es_fragment.c_str(),
                hlsl5_fragment.empty() ? nullptr : hlsl5_fragment.c_str(),
                metal_macos_fragment.empty() ? nullptr : metal_macos_fragment.c_str(),
                parameter_components,
                pass_count,
                sampling_inputs,
                ink_overflow};
    }
};

struct RendererSlot {
    std::unique_ptr<nkui::UiRenderer> renderer;
    nk_graphics_api backend_api = 0;
    nk_graphics_device backend_device{};
    nk_surface backend_surface = 0;
    bool active = false;
    nkui::Compositor compositor;
    std::unordered_map<PathCacheKey, PreparedPathCacheEntry, PathCacheKeyHash> paths;
    std::vector<CustomEffectRegistrationStorage> custom_effects;
    std::size_t registered_custom_effects = 0;
    nkui::UiGpuStats retired_gpu{};
    nkui_renderer_stats stats{};
    uint16_t generation = 1;
};

struct LayoutSessionState : std::enable_shared_from_this<LayoutSessionState> {
    std::mutex mutex;
    std::unique_ptr<nkui::LayoutEngine> engine;
    nkui::LayoutRenderCompiler compiler;
    nkui::LayoutRenderFrame frame;
    nkui::LayoutSnapshot snapshot;
    std::unordered_map<uint32_t, nkui_display_list> custom_paints;
    nkui_nullable_layout_measure_callback measure_callback = nullptr;
    void *measure_user_data = nullptr;
    bool fonts_configured = false;
    bool submitted = false;
};

thread_local LayoutSessionState *active_measure_session = nullptr;

struct MeasureCallbackScope {
    LayoutSessionState *previous = nullptr;

    explicit MeasureCallbackScope(LayoutSessionState *session) : previous(active_measure_session) {
        active_measure_session = session;
    }

    ~MeasureCallbackScope() { active_measure_session = previous; }
};

void configure_layout_measure_callback(LayoutSessionState &state) {
    const auto callback = state.measure_callback;
    void *const user_data = state.measure_user_data;
    if (!callback) {
        state.engine->set_measure_callback({});
        return;
    }
    state.engine->set_measure_callback([callback, user_data, session = &state](
                                           uint32_t node_id,
                                           const nkui::LayoutMeasureConstraints &constraints) {
        nkui_layout_measure_constraints native_constraints{};
        native_constraints.struct_size = sizeof(native_constraints);
        native_constraints.min_width = constraints.min_width;
        native_constraints.max_width = constraints.max_width;
        native_constraints.min_height = constraints.min_height;
        native_constraints.max_height = constraints.max_height;
        MeasureCallbackScope callback_scope(session);
        const nkui_layout_measure_result measured =
            callback(node_id, native_constraints, user_data);
        return nkui::LayoutMeasureResult{measured.width, measured.height, measured.baseline,
                                         (measured.flags & NKUI_LAYOUT_MEASURE_HAS_BASELINE) != 0};
    });
}

struct LayoutSessionSlot {
    std::shared_ptr<LayoutSessionState> session;
    uint16_t generation = 1;
};

void accumulate_gpu_lifetime(nkui::UiGpuStats &total, const nkui::UiGpuStats &current) {
    total.frames += current.frames;
    total.passes += current.passes;
    total.draw_calls += current.draw_calls;
    total.upload_bytes += current.upload_bytes;
    total.resource_creations += current.resource_creations;
    total.resource_destructions += current.resource_destructions;
    total.surface_recreations += current.surface_recreations;
    total.device_losses += current.device_losses;
    total.failed_allocations += current.failed_allocations;
}

void discard_stale_renderer(RendererSlot &slot, const nk_surface_frame_target &target,
                            nk_surface surface) {
    if (!slot.renderer)
        return;
    const bool was_lost = slot.renderer->lost();
    const bool api_changed = slot.backend_api != target.api;
    const bool device_changed = slot.backend_device.id != target.device.id;
    const bool surface_changed = slot.backend_surface != surface;
    if (!was_lost && !api_changed && !device_changed && !surface_changed)
        return;
    const nkui::UiRendererStats old_stats = slot.renderer->stats();
    const nkui::UiGpuStats &old = old_stats.gpu;
    accumulate_gpu_lifetime(slot.retired_gpu, old);
    slot.retired_gpu.resource_destructions += old.buffers_live + old.images_live +
                                              old.samplers_live + old.shaders_live +
                                              old.pipelines_live + old.render_targets_live;
    if (old.device_losses == 0 && (was_lost || api_changed || device_changed))
        ++slot.retired_gpu.device_losses;
    if (old.surface_recreations == 0 && (api_changed || device_changed || surface_changed))
        ++slot.retired_gpu.surface_recreations;
    slot.stats.glyph_uploads += old_stats.glyph_uploads;
    slot.stats.atlas_rebuilds += old_stats.atlas_rebuilds;
    slot.stats.atlas_partial_updates += old_stats.atlas_partial_updates;
    slot.stats.atlas_dirty_upload_bytes += old_stats.atlas_dirty_upload_bytes;
    slot.stats.atlas_scale_generation =
        std::max<uint64_t>(slot.stats.atlas_scale_generation, old_stats.atlas_scale_generation);
    slot.stats.transient_target_pool_hits += old_stats.transient_target_pool_hits;
    slot.stats.transient_target_pool_misses += old_stats.transient_target_pool_misses;
    slot.stats.effect_cache_hits += old_stats.effect_cache_hits;
    slot.stats.effect_cache_misses += old_stats.effect_cache_misses;
    slot.renderer.reset();
    slot.registered_custom_effects = 0;
}

bool register_custom_effects(RendererSlot &slot) {
    while (slot.registered_custom_effects < slot.custom_effects.size()) {
        const auto &stored = slot.custom_effects[slot.registered_custom_effects];
        const auto registration = stored.native();
        if (!slot.renderer->registerCustomEffect(registration)) {
            slot.renderer.reset();
            slot.registered_custom_effects = 0;
            return false;
        }
        ++slot.registered_custom_effects;
    }
    return true;
}

std::mutex lists_mutex;
std::vector<DisplayListSlot> lists;
std::mutex resources_mutex;
std::vector<ResourceSlot> resources;
std::mutex renderers_mutex;
std::deque<RendererSlot> renderers;
std::mutex layout_sessions_mutex;
std::vector<LayoutSessionSlot> layout_sessions;
std::shared_mutex renderer_execution_mutex;
std::mutex renderer_cpu_mutex;

uint32_t make_handle(uint16_t generation, uint16_t slot) {
    return (static_cast<uint32_t>(generation) << 16) | slot;
}

DisplayListSlot *resolve(nkui_display_list handle) {
    const uint16_t slot = static_cast<uint16_t>(handle.id);
    const uint16_t generation = static_cast<uint16_t>(handle.id >> 16);
    if (!slot || slot > lists.size())
        return nullptr;
    auto &entry = lists[slot - 1];
    return entry.list && entry.generation == generation ? &entry : nullptr;
}

ResourceSlot *resolve(nkui_resource handle, nkui::ResourceKind expected) {
    const nkui::ResourceId id{handle.id};
    if (!nkui::is_resource_id(id, expected))
        return nullptr;
    const uint16_t slot = static_cast<uint16_t>(handle.id);
    const uint16_t generation = static_cast<uint16_t>((handle.id >> 16) & 0x0FFF);
    if (!slot || slot > resources.size())
        return nullptr;
    auto &entry = resources[slot - 1];
    return entry.kind == expected && entry.generation == generation && entry.externally_alive
               ? &entry
               : nullptr;
}

ResourceSlot *resolve_retained(nkui_resource handle, nkui::ResourceKind expected) {
    const nkui::ResourceId id{handle.id};
    if (!nkui::is_resource_id(id, expected))
        return nullptr;
    const uint16_t slot = static_cast<uint16_t>(handle.id);
    const uint16_t generation = static_cast<uint16_t>((handle.id >> 16) & 0x0FFF);
    if (!slot || slot > resources.size())
        return nullptr;
    auto &entry = resources[slot - 1];
    return entry.kind == expected && entry.generation == generation ? &entry : nullptr;
}

RendererSlot *resolve(nkui_renderer handle) {
    const uint16_t slot = static_cast<uint16_t>(handle.id);
    const uint16_t generation = static_cast<uint16_t>(handle.id >> 16);
    if (!slot || slot > renderers.size())
        return nullptr;
    auto &entry = renderers[slot - 1];
    return entry.active && entry.generation == generation ? &entry : nullptr;
}

struct RenderSubmission {
    nkui_renderer renderer{};
    nk_surface surface = NK_INVALID_HANDLE;
    nk_surface_frame frame = NK_INVALID_HANDLE;
    nk_surface_frame_target frame_target{};
    std::shared_ptr<const nkui::SealedRenderPlan> plan;
    std::vector<nkui::SkribidiAdapter *> text_adapters;
    std::vector<std::shared_ptr<nkui::SkribidiAdapter>> text_adapter_owners;
    std::shared_ptr<LayoutSessionState> session_owner;
};

struct RenderCompletion {
    nk_surface_frame frame = NK_INVALID_HANDLE;
    bool success = false;
};

void retain_text_adapter(
    const std::shared_ptr<nkui::SkribidiAdapter> &adapter,
    std::vector<nkui::SkribidiAdapter *> &adapters,
    std::vector<std::shared_ptr<nkui::SkribidiAdapter>> &owners) {
    if (!adapter || std::find(adapters.begin(), adapters.end(), adapter.get()) != adapters.end())
        return;
    adapters.push_back(adapter.get());
    owners.push_back(adapter);
}

struct DeferredRendererDestroy {
    std::unique_ptr<nkui::UiRenderer> renderer;
};

std::mutex render_submission_mutex;
std::unique_ptr<RenderSubmission> pending_render_submission;
bool render_submission_runner_active = false;
std::uint64_t render_submission_generation = 0;

void NK_CALL run_next_render_submission(void *data);
bool enqueue_render_submission(RenderSubmission *submission);
void shutdown_render_scheduler() noexcept;

void ensure_runtime_shutdown_hook() noexcept {
    static const bool registered = [] {
        nk::core::register_runtime_shutdown_hook(&shutdown_render_scheduler);
        return true;
    }();
    (void)registered;
}

void destroy_render_completion(void *data) noexcept {
    auto *completion = static_cast<RenderCompletion *>(data);
    if (completion && completion->frame != NK_INVALID_HANDLE) {
        /* APP owns completion delivery.  If its bounded queue drops this
           task, the cleanup callback is the last chance to close the
           platform-owned frame ticket before shutdown clears the registry. */
        (void)nk_surface_cancel_frame(completion->frame);
        completion->frame = NK_INVALID_HANDLE;
    }
    delete completion;
}

void destroy_deferred_renderer(void *data) noexcept {
    delete static_cast<DeferredRendererDestroy *>(data);
}

void NK_CALL run_deferred_renderer_destroy(void *data) {
    auto *destroy = static_cast<DeferredRendererDestroy *>(data);
    destroy->renderer.reset();
}

void NK_CALL finish_render_submission(void *data) {
    auto *completion = static_cast<RenderCompletion *>(data);
    if (completion->frame != NK_INVALID_HANDLE) {
        if (completion->success)
            nk_surface_present_frame(completion->frame);
        else
            nk_surface_cancel_frame(completion->frame);
        /* The token was consumed even when the backend reports an error. */
        completion->frame = NK_INVALID_HANDLE;
    }
}

void execute_render_submission(RenderSubmission &submission) {
    bool success = false;
    const bool context_backend = submission.frame_target.api == NK_GRAPHICS_OPENGL ||
                                 submission.frame_target.api == NK_GRAPHICS_OPENGL_ES;
    const bool bound = !context_backend || nkgpu_bind_frame_target(&submission.frame_target) == NKGPU_OK;
    if (!bound && context_backend && nk::core::render_executor_physical())
        /* A failed bind may follow a surface-destroy callback while a prior
           frame left the context current on this thread. */
        nk_graphics_unbind_frame_target(&submission.frame_target);
    if (bound) {
        std::shared_lock<std::shared_mutex> renderer_execution_lock(renderer_execution_mutex,
                                                                     std::defer_lock);
        nkui::UiRenderer *renderer_impl = nullptr;
        bool new_backend = false;
        {
            std::lock_guard<std::mutex> lock(renderers_mutex);
            renderer_execution_lock.lock();
            auto *slot = resolve(submission.renderer);
            if (slot) {
                {
                    std::lock_guard<std::mutex> cpu_lock(renderer_cpu_mutex);
                    discard_stale_renderer(*slot, submission.frame_target, submission.surface);
                }
                if (!slot->renderer) {
                    slot->renderer =
                        nkui::create_ui_renderer(submission.surface, &submission.frame_target);
                    slot->backend_api = submission.frame_target.api;
                    slot->backend_device = submission.frame_target.device;
                    slot->backend_surface = submission.surface;
                }
                if (slot->renderer) {
                    new_backend = !slot->renderer->valid();
                    success = !new_backend || slot->renderer->initialize();
                    if (success)
                        success = register_custom_effects(*slot);
                    if (success)
                        renderer_impl = slot->renderer.get();
                }
            }
        }

        if (renderer_impl && success) {
            if (submission.session_owner) {
                std::scoped_lock upload_lock(resources_mutex, submission.session_owner->mutex);
                for (auto *adapter : submission.text_adapters)
                    if (success)
                        success = renderer_impl->uploadAtlases(*adapter, new_backend);
            } else {
                std::lock_guard<std::mutex> upload_lock(resources_mutex);
                for (auto *adapter : submission.text_adapters)
                    if (success)
                        success = renderer_impl->uploadAtlases(*adapter, new_backend);
            }
            if (success)
                success = nkui::execute_render_plan(
                    *renderer_impl, *submission.plan,
                    {nkui::make_resource_id(nkui::ResourceKind::RenderTarget, 1, 1),
                     submission.frame_target});
        }
    }

    /* GL/EGL retains a thread-local context through submit so sealed-plan
       resource destructors can release external images on RENDER. */
    if (bound && nk::core::render_executor_physical() && context_backend) {
        submission.plan.reset();
        nk_graphics_unbind_frame_target(&submission.frame_target);
    }

    auto *completion = new RenderCompletion{submission.frame, success};
    if (nk::core::dispatch_to_executor(NK_EXECUTOR_PLATFORM, &finish_render_submission,
                                       completion, &destroy_render_completion,
                                       sizeof(RenderCompletion)) != NK_OK)
        delete completion;
}

void cancel_render_submission_on_platform(RenderSubmission &submission) {
    if (submission.frame != NK_INVALID_HANDLE)
        nk_surface_cancel_frame(submission.frame);
}

void shutdown_render_scheduler() noexcept {
    std::unique_ptr<RenderSubmission> pending;
    {
        std::lock_guard lock(render_submission_mutex);
        pending = std::move(pending_render_submission);
        render_submission_runner_active = false;
        render_submission_generation = 0;
    }
    if (pending)
        cancel_render_submission_on_platform(*pending);
}

bool enqueue_render_submission(RenderSubmission *raw_submission) {
    std::unique_ptr<RenderSubmission> submission(raw_submission);
    std::unique_ptr<RenderSubmission> stale_generation;
    std::unique_ptr<RenderSubmission> replaced;
    bool start_runner = false;
    const auto generation = nk::core::runtime_generation();
    {
        std::lock_guard lock(render_submission_mutex);
        /* A discarded render task does not run after nk_shutdown(). Drop its
           stale pending plan before accepting work from the next runtime. */
        if (render_submission_generation != generation) {
            stale_generation = std::move(pending_render_submission);
            render_submission_runner_active = false;
            render_submission_generation = generation;
        }
        replaced = std::move(pending_render_submission);
        pending_render_submission = std::move(submission);
        if (!render_submission_runner_active) {
            render_submission_runner_active = true;
            start_runner = true;
        }
    }
    if (stale_generation)
        cancel_render_submission_on_platform(*stale_generation);
    if (replaced)
        cancel_render_submission_on_platform(*replaced);
    if (!start_runner)
        return true;
    if (nk::core::dispatch_to_render(&run_next_render_submission, nullptr, nullptr, 0) == NK_OK)
        return true;

    std::unique_ptr<RenderSubmission> failed;
    {
        std::lock_guard lock(render_submission_mutex);
        failed = std::move(pending_render_submission);
        render_submission_runner_active = false;
    }
    if (failed)
        cancel_render_submission_on_platform(*failed);
    return false;
}

void NK_CALL run_next_render_submission(void *) {
    std::unique_ptr<RenderSubmission> submission;
    {
        std::lock_guard lock(render_submission_mutex);
        submission = std::move(pending_render_submission);
        if (!submission) {
            render_submission_runner_active = false;
            return;
        }
    }
    execute_render_submission(*submission);

    bool schedule_next = false;
    {
        std::lock_guard lock(render_submission_mutex);
        schedule_next = pending_render_submission != nullptr;
        if (!schedule_next)
            render_submission_runner_active = false;
    }
    if (schedule_next &&
        nk::core::dispatch_to_render(&run_next_render_submission, nullptr, nullptr, 0) != NK_OK) {
        std::unique_ptr<RenderSubmission> failed;
        {
            std::lock_guard lock(render_submission_mutex);
            failed = std::move(pending_render_submission);
            render_submission_runner_active = false;
        }
        if (failed)
            /* The callback is already on RENDER, so hand cancellation back to PLATFORM. */
            if (auto *completion = new RenderCompletion{failed->frame, false};
                nk::core::dispatch_to_executor(NK_EXECUTOR_PLATFORM, &finish_render_submission,
                                               completion, &destroy_render_completion,
                                               sizeof(RenderCompletion)) != NK_OK)
                delete completion;
    }
}

LayoutSessionState *resolve(nkui_layout_session handle) {
    const uint16_t slot = static_cast<uint16_t>(handle.id);
    const uint16_t generation = static_cast<uint16_t>(handle.id >> 16);
    if (!slot || slot > layout_sessions.size())
        return nullptr;
    auto &entry = layout_sessions[slot - 1];
    return entry.session && entry.generation == generation ? entry.session.get() : nullptr;
}

bool read_u32(const uint8_t *bytes, size_t size, size_t offset, uint32_t &out) {
    if (!bytes || offset > size || size - offset < sizeof(out))
        return false;
    std::memcpy(&out, bytes + offset, sizeof(out));
    return true;
}

bool read_i32(const uint8_t *bytes, size_t size, size_t offset, int32_t &out) {
    uint32_t value = 0;
    if (!read_u32(bytes, size, offset, value))
        return false;
    std::memcpy(&out, &value, sizeof(out));
    return true;
}

bool read_float(const uint8_t *bytes, size_t size, size_t offset, float &out) {
    uint32_t value = 0;
    if (!read_u32(bytes, size, offset, value))
        return false;
    std::memcpy(&out, &value, sizeof(out));
    return true;
}

bool read_layout_transaction(const uint8_t *bytes, uint32_t byte_count,
                             std::vector<nkui::LayoutNode> &nodes) {
    constexpr size_t header_bytes = NKUI_LAYOUT_TRANSACTION_HEADER_BYTES;
    constexpr size_t record_bytes = NKUI_LAYOUT_NODE_RECORD_BYTES;
    if (!bytes || byte_count < header_bytes || byte_count > NKUI_LAYOUT_MAX_TRANSACTION_BYTES)
        return false;
    const size_t size = byte_count;
    uint32_t version = 0;
    uint32_t node_count = 0;
    uint32_t encoded_record_bytes = 0;
    uint32_t string_offset = 0;
    if (!read_u32(bytes, size, 0, version) || !read_u32(bytes, size, 4, node_count) ||
        !read_u32(bytes, size, 8, encoded_record_bytes) ||
        !read_u32(bytes, size, 12, string_offset) || version != NKUI_LAYOUT_TRANSACTION_VERSION ||
        !node_count || encoded_record_bytes != record_bytes)
        return false;
    if (static_cast<size_t>(node_count) >
        (std::numeric_limits<size_t>::max() - header_bytes) / record_bytes)
        return false;
    const size_t records_end = header_bytes + static_cast<size_t>(node_count) * record_bytes;
    if (string_offset < records_end || string_offset > size)
        return false;

    const auto read_node_u32 = [&](size_t record, size_t field, uint32_t &out) {
        return read_u32(bytes, size, record + field, out);
    };
    const auto read_node_i32 = [&](size_t record, size_t field, int32_t &out) {
        return read_i32(bytes, size, record + field, out);
    };
    const auto read_node_float = [&](size_t record, size_t field, float &out) {
        return read_float(bytes, size, record + field, out);
    };
    const auto read_node_color = [&](size_t record, size_t field, nkui::LayoutColor &out) {
        return read_node_float(record, field, out.red) &&
               read_node_float(record, field + sizeof(float), out.green) &&
               read_node_float(record, field + 2 * sizeof(float), out.blue) &&
               read_node_float(record, field + 3 * sizeof(float), out.alpha);
    };
    {
        nodes.clear();
        nodes.reserve(node_count);
        for (uint32_t index = 0; index < node_count; ++index) {
            const size_t record = header_bytes + static_cast<size_t>(index) * record_bytes;
            nkui::LayoutNode node;
            uint32_t id = 0;
            uint32_t visual_kind = 0;
            uint32_t width_sizing = 0;
            uint32_t height_sizing = 0;
            uint32_t direction = 0;
            uint32_t font_family = 0;
            uint32_t text_wrap = 0;
            uint32_t text_alignment = 0;
            uint32_t text_direction = 0;
            uint32_t clip = 0;
            uint32_t text_flags = 0;
            uint32_t text_offset = 0;
            uint32_t text_length = 0;
            uint32_t node_flags = 0;
            uint32_t child_alignment = 0;
            uint32_t child_distribution = 0;
            int32_t z_index = 0;
            std::array<float, 6> transform{};
            float position_x = 0.0f;
            float position_y = 0.0f;
            float width_min = 0.0f;
            float width_max = 0.0f;
            float height_min = 0.0f;
            float height_max = 0.0f;
            float aspect_ratio = 0.0f;
            float width_grow_weight = 1.0f;
            float height_grow_weight = 1.0f;
            uint32_t wrap_mode = 0;
            uint32_t align_self = 0;
            if (!read_node_u32(record, NKUI_LAYOUT_NODE_ID_OFFSET, id) ||
                !read_node_i32(record, NKUI_LAYOUT_NODE_PARENT_OFFSET, node.parent) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_VISUAL_KIND_OFFSET, visual_kind) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_WIDTH_SIZING_OFFSET, width_sizing) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_WIDTH_VALUE_OFFSET,
                                 node.style.width.value) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_HEIGHT_SIZING_OFFSET, height_sizing) ||
                height_sizing > NKUI_LAYOUT_SIZING_PERCENT ||
                !read_node_float(record, NKUI_LAYOUT_NODE_HEIGHT_VALUE_OFFSET,
                                 node.style.height.value) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_DIRECTION_OFFSET, direction) ||
                direction > NKUI_LAYOUT_DIRECTION_TOP_TO_BOTTOM ||
                !read_node_float(record, NKUI_LAYOUT_NODE_PADDING_LEFT_OFFSET,
                                 node.style.padding_left) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_PADDING_RIGHT_OFFSET,
                                 node.style.padding_right) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_PADDING_TOP_OFFSET,
                                 node.style.padding_top) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_PADDING_BOTTOM_OFFSET,
                                 node.style.padding_bottom) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_CHILD_GAP_OFFSET, node.style.child_gap) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_ROW_GAP_OFFSET, node.style.row_gap) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_COLUMN_GAP_OFFSET,
                                 node.style.column_gap) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_WRAP_MODE_OFFSET, wrap_mode) ||
                !read_node_color(record, NKUI_LAYOUT_NODE_BACKGROUND_OFFSET,
                                 node.style.background) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_RADIUS_TOP_LEFT_OFFSET,
                                 node.style.radius_top_left) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_RADIUS_TOP_RIGHT_OFFSET,
                                 node.style.radius_top_right) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_RADIUS_BOTTOM_LEFT_OFFSET,
                                 node.style.radius_bottom_left) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_RADIUS_BOTTOM_RIGHT_OFFSET,
                                 node.style.radius_bottom_right) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_CLIP_FLAGS_OFFSET, clip) ||
                clip > (NKUI_LAYOUT_CLIP_HORIZONTAL | NKUI_LAYOUT_CLIP_VERTICAL) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_TEXT_OFFSET_OFFSET, text_offset) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_TEXT_LENGTH_OFFSET, text_length) ||
                !read_node_color(record, NKUI_LAYOUT_NODE_TEXT_COLOR_OFFSET, node.text_color) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_FONT_FAMILY_OFFSET, font_family) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_FONT_SIZE_OFFSET,
                                 node.text_style.font_size) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_LETTER_SPACING_OFFSET,
                                 node.text_style.letter_spacing) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_LINE_HEIGHT_OFFSET,
                                 node.paragraph_style.line_height) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_TEXT_WRAP_OFFSET, text_wrap) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_TEXT_ALIGNMENT_OFFSET, text_alignment) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_TEXT_DIRECTION_OFFSET, text_direction) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_TEXT_FLAGS_OFFSET, text_flags) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_TRANSFORM_A_OFFSET, transform[0]) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_TRANSFORM_B_OFFSET, transform[1]) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_TRANSFORM_C_OFFSET, transform[2]) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_TRANSFORM_D_OFFSET, transform[3]) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_TRANSFORM_TX_OFFSET, transform[4]) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_TRANSFORM_TY_OFFSET, transform[5]) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_FLAGS_OFFSET, node_flags) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_CHILD_ALIGNMENT_OFFSET, child_alignment) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_CHILD_DISTRIBUTION_OFFSET,
                               child_distribution) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_ALIGN_SELF_OFFSET, align_self) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_POSITION_X_OFFSET, position_x) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_POSITION_Y_OFFSET, position_y) ||
                !read_node_i32(record, NKUI_LAYOUT_NODE_Z_INDEX_OFFSET, z_index) ||
                !read_node_u32(record, NKUI_LAYOUT_NODE_MEASURE_VERSION_OFFSET,
                               node.measure_version) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_WIDTH_MIN_OFFSET, width_min) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_WIDTH_MAX_OFFSET, width_max) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_HEIGHT_MIN_OFFSET, height_min) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_HEIGHT_MAX_OFFSET, height_max) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_ASPECT_RATIO_OFFSET, aspect_ratio) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_WIDTH_GROW_WEIGHT_OFFSET,
                                 width_grow_weight) ||
                !read_node_float(record, NKUI_LAYOUT_NODE_HEIGHT_GROW_WEIGHT_OFFSET,
                                 height_grow_weight))
                return false;
            const uint32_t child_align_x = child_alignment & 0xffu;
            const uint32_t child_align_y = (child_alignment >> 8u) & 0xffu;
            const bool floating = (node_flags & NKUI_LAYOUT_NODE_FLOATING) != 0;
            const bool clip_to_parent = (node_flags & NKUI_LAYOUT_NODE_CLIP_TO_PARENT) != 0;
            if (visual_kind < NKUI_LAYOUT_VISUAL_BOX || visual_kind > NKUI_LAYOUT_VISUAL_CUSTOM ||
                (child_alignment & 0xffff0000u) != 0 ||
                child_align_x > NKUI_LAYOUT_ALIGNMENT_CENTER ||
                child_align_y > NKUI_LAYOUT_ALIGNMENT_BASELINE ||
                child_distribution > NKUI_LAYOUT_DISTRIBUTION_SPACE_EVENLY ||
                wrap_mode > NKUI_LAYOUT_WRAP_WRAP ||
                align_self > NKUI_LAYOUT_SELF_ALIGNMENT_BASELINE ||
                (node_flags & ~(NKUI_LAYOUT_NODE_VISIBLE | NKUI_LAYOUT_NODE_FLOATING |
                                NKUI_LAYOUT_NODE_CLIP_TO_PARENT)) != 0 ||
                (clip_to_parent && !floating) || z_index < std::numeric_limits<int16_t>::min() ||
                z_index > std::numeric_limits<int16_t>::max() || !std::isfinite(position_x) ||
                !std::isfinite(position_y) || width_sizing > NKUI_LAYOUT_SIZING_PERCENT)
                return false;
            const auto valid_spacing = [](float value) {
                return std::isfinite(value) && value >= 0.0f;
            };
            if (!valid_spacing(node.style.padding_left) ||
                !valid_spacing(node.style.padding_right) ||
                !valid_spacing(node.style.padding_top) ||
                !valid_spacing(node.style.padding_bottom) || !valid_spacing(node.style.child_gap) ||
                !valid_spacing(node.style.row_gap) || !valid_spacing(node.style.column_gap))
                return false;
            const float determinant = transform[0] * transform[3] - transform[1] * transform[2];
            if (std::any_of(transform.begin(), transform.end(),
                            [](float value) { return !std::isfinite(value); }) ||
                !std::isfinite(determinant) || std::abs(determinant) < 0.000001f)
                return false;
            node.id = id;
            node.visual_kind = static_cast<nkui::LayoutVisualKind>(visual_kind);
            node.style.visible = (node_flags & NKUI_LAYOUT_NODE_VISIBLE) != 0;
            node.style.transform = {transform[0], transform[1], transform[2],
                                    transform[3], transform[4], transform[5]};
            node.style.width.sizing = static_cast<nkui::LayoutSizing>(width_sizing);
            node.style.height.sizing = static_cast<nkui::LayoutSizing>(height_sizing);
            node.style.width.min = width_min;
            node.style.width.max = width_max;
            node.style.height.min = height_min;
            node.style.height.max = height_max;
            node.style.width.grow_weight = width_grow_weight;
            node.style.height.grow_weight = height_grow_weight;
            node.style.aspect_ratio = aspect_ratio;
            node.style.direction = static_cast<nkui::LayoutDirection>(direction);
            node.style.child_align_x = static_cast<nkui::LayoutAlignmentX>(child_align_x);
            node.style.child_align_y = static_cast<nkui::LayoutAlignmentY>(child_align_y);
            node.style.child_distribution =
                static_cast<nkui::LayoutDistribution>(child_distribution);
            node.style.wrap_mode = static_cast<nkui::LayoutWrapMode>(wrap_mode);
            node.style.align_self = static_cast<nkui::LayoutSelfAlignment>(align_self);
            node.style.positioning =
                floating ? nkui::LayoutPositioning::Absolute : nkui::LayoutPositioning::Flow;
            node.style.position_x = position_x;
            node.style.position_y = position_y;
            node.style.z_index = z_index;
            node.style.clip_to_parent = clip_to_parent;
            node.text_style.family = static_cast<nkui::FontFamily>(font_family);
            node.paragraph_style.wrap = static_cast<nkui::TextWrapMode>(text_wrap);
            node.paragraph_style.alignment = static_cast<nkui::TextAlignment>(text_alignment);
            node.paragraph_style.direction = static_cast<nkui::TextDirection>(text_direction);
            node.style.clip_horizontal = (clip & NKUI_LAYOUT_CLIP_HORIZONTAL) != 0;
            node.style.clip_vertical = (clip & NKUI_LAYOUT_CLIP_VERTICAL) != 0;
            const auto valid_axis = [](const nkui::LayoutAxis &axis) {
                if (!std::isfinite(axis.value) || axis.value < 0.0f || !std::isfinite(axis.min) ||
                    axis.min < 0.0f || !std::isfinite(axis.max) || axis.max < 0.0f ||
                    !std::isfinite(axis.grow_weight) || axis.grow_weight <= 0.0f)
                    return false;
                if (axis.sizing == nkui::LayoutSizing::Percent)
                    return axis.value <= 1.0f && axis.min == 0.0f && axis.max == 0.0f &&
                           axis.grow_weight == 1.0f;
                if (axis.sizing == nkui::LayoutSizing::Fixed)
                    return axis.min == 0.0f && axis.max == 0.0f && axis.grow_weight == 1.0f;
                if (axis.sizing != nkui::LayoutSizing::Grow && axis.grow_weight != 1.0f)
                    return false;
                return axis.max == 0.0f || axis.max >= axis.min;
            };
            const auto valid_color = [](const nkui::LayoutColor &color) {
                const auto valid = [](float value) {
                    return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
                };
                return valid(color.red) && valid(color.green) && valid(color.blue) &&
                       valid(color.alpha);
            };
            if (!valid_axis(node.style.width) || !valid_axis(node.style.height) ||
                !valid_color(node.style.background) || !valid_color(node.text_color) ||
                !std::isfinite(node.style.radius_top_left) ||
                !std::isfinite(node.style.radius_top_right) ||
                !std::isfinite(node.style.radius_bottom_left) ||
                !std::isfinite(node.style.radius_bottom_right) ||
                node.style.radius_top_left < 0.0f || node.style.radius_top_right < 0.0f ||
                node.style.radius_bottom_left < 0.0f || node.style.radius_bottom_right < 0.0f)
                return false;
            if (!std::isfinite(node.style.aspect_ratio) || node.style.aspect_ratio < 0.0f)
                return false;
            if (font_family > static_cast<uint32_t>(nkui::FontFamily::Emoji) ||
                !std::isfinite(node.text_style.font_size) || node.text_style.font_size <= 0.0f ||
                !std::isfinite(node.text_style.letter_spacing) ||
                !std::isfinite(node.paragraph_style.line_height) ||
                node.paragraph_style.line_height < 0.0f ||
                text_wrap > static_cast<uint32_t>(nkui::TextWrapMode::WordCharacter) ||
                text_alignment > static_cast<uint32_t>(nkui::TextAlignment::End) ||
                text_direction > static_cast<uint32_t>(nkui::TextDirection::Rtl) || text_flags != 0)
                return false;
            if (text_length > size - string_offset || text_offset < string_offset ||
                text_offset - string_offset > size - string_offset - text_length)
                return false;
            node.text.assign(reinterpret_cast<const char *>(bytes + text_offset), text_length);
            nodes.push_back(std::move(node));
        }
    }
    return true;
}

nkui_result allocate_resource(nkui::ResourceKind kind, nkui_resource *out, ResourceSlot **out_slot);
void release_resource_slot(ResourceSlot &slot);

bool text_options_from_api(const nkui_text_style *text_style,
                           const nkui_paragraph_style *paragraph_style,
                           nkui::TextLayoutOptions &out) {
    if (!text_style || !paragraph_style || text_style->struct_size < sizeof(*text_style) ||
        paragraph_style->struct_size < sizeof(*paragraph_style))
        return false;
    if (text_style->family < NKUI_FONT_FAMILY_DEFAULT ||
        text_style->family > NKUI_FONT_FAMILY_EMOJI ||
        paragraph_style->wrap < NKUI_TEXT_WRAP_NONE ||
        paragraph_style->wrap > NKUI_TEXT_WRAP_WORD_CHARACTER ||
        paragraph_style->alignment < NKUI_TEXT_ALIGN_START ||
        paragraph_style->alignment > NKUI_TEXT_ALIGN_END ||
        paragraph_style->direction < NKUI_TEXT_DIRECTION_AUTO ||
        paragraph_style->direction > NKUI_TEXT_DIRECTION_RTL ||
        !std::isfinite(text_style->font_size) || text_style->font_size <= 0.0f ||
        !std::isfinite(text_style->letter_spacing) ||
        !std::isfinite(paragraph_style->line_height) || paragraph_style->line_height < 0.0f)
        return false;
    out.family = static_cast<nkui::FontFamily>(text_style->family);
    out.font_size = text_style->font_size;
    out.letter_spacing = text_style->letter_spacing;
    out.line_height = paragraph_style->line_height;
    out.wrap = static_cast<nkui::TextWrapMode>(paragraph_style->wrap);
    out.alignment = static_cast<nkui::TextAlignment>(paragraph_style->alignment);
    out.direction = static_cast<nkui::TextDirection>(paragraph_style->direction);
    return true;
}

nkui_result ensure_mutable_font_collection(ResourceSlot &slot) {
    if (!slot.font_collection)
        return NKUI_ERROR_INVALID_HANDLE;
    if (slot.font_collection.use_count() == 1)
        return NKUI_OK;
    {
        auto replacement = std::make_shared<nkui::SkribidiFontCollection>();
        if (!replacement->valid())
            return NKUI_ERROR_OUT_OF_MEMORY;
        for (const auto &font : slot.fonts) {
            const bool added = font.data ? replacement->add_font_from_shared_data(
                                               font.path.c_str(), font.data, font.family)
                                         : replacement->add_font(font.path.c_str(), font.family);
            if (!added)
                return NKUI_ERROR_INVALID_ARGUMENT;
        }
        if (slot.system_fallbacks && !replacement->add_system_fallbacks())
            return NKUI_ERROR_INVALID_ARGUMENT;
        slot.font_collection = std::move(replacement);
    }
    return NKUI_OK;
}

nkui_result create_text_layout_locked(nkui_resource fonts, const char *text, float width,
                                      const nkui::TextLayoutOptions &options,
                                      nkui_resource *out_layout) {
    auto *font_slot = resolve(fonts, nkui::ResourceKind::FontCollection);
    if (!font_slot || (font_slot->fonts.empty() && !font_slot->system_fallbacks))
        return NKUI_ERROR_INVALID_HANDLE;
    if (!font_slot->font_collection || !font_slot->font_collection->valid())
        return NKUI_ERROR_INVALID_HANDLE;
    const auto shared_fonts = font_slot->font_collection;
    ResourceSlot *layout_slot = nullptr;
    const nkui_result allocated =
        allocate_resource(nkui::ResourceKind::TextLayout, out_layout, &layout_slot);
    if (allocated != NKUI_OK)
        return allocated;
    layout_slot->text = std::make_shared<nkui::SkribidiAdapter>(shared_fonts);
    bool valid = layout_slot->text->valid() &&
                 layout_slot->text->set_atlas_namespace(static_cast<uint16_t>(out_layout->id));
    valid = valid && layout_slot->text->layout_utf8(text, width, options);
    valid = valid && layout_slot->text->prepare_glyphs(0.0f, 0.0f, 1.0f, nkui::GlyphMode::Alpha,
                                                       layout_slot->text_glyphs);
    if (!valid) {
        release_resource_slot(*layout_slot);
        out_layout->id = 0;
        return NKUI_ERROR_INVALID_ARGUMENT;
    }
    layout_slot->text_width = width;
    layout_slot->text_options = options;
    return NKUI_OK;
}

bool append_path(nkui::NanoVGPath &path, const std::vector<nkui_path_element> &elements) {
    path.reset();
    for (const auto &element : elements) {
        const float *v = element.values;
        switch (element.verb) {
        case NKUI_PATH_MOVE_TO:
            path.move_to(v[0], v[1]);
            break;
        case NKUI_PATH_LINE_TO:
            path.line_to(v[0], v[1]);
            break;
        case NKUI_PATH_BEZIER_TO:
            path.bezier_to(v[0], v[1], v[2], v[3], v[4], v[5]);
            break;
        case NKUI_PATH_QUADRATIC_TO:
            path.quad_to(v[0], v[1], v[2], v[3]);
            break;
        case NKUI_PATH_ARC_TO:
            path.arc_to(v[0], v[1], v[2], v[3], v[4]);
            break;
        case NKUI_PATH_CLOSE:
            path.close();
            break;
        default:
            break;
        }
    }
    return !path.empty();
}

uint32_t float_bits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

void add_effect_cache_pixel_scale(nkui::RenderPlan &plan, float pixel_scale) {
    const uint32_t scale = float_bits(pixel_scale);
    for (auto &pass : plan.passes) {
        if (pass.kind != nkui::RenderPassKind::Effect || !pass.cache_key)
            continue;
        pass.cache_key ^= static_cast<uint64_t>(scale) + UINT64_C(0x9E3779B97F4A7C15) +
                          (pass.cache_key << 6) + (pass.cache_key >> 2);
        if (!pass.cache_key)
            pass.cache_key = 1;
    }
}

PathCacheKey path_cache_key(nkui_resource path, const std::array<float, 6> &transform,
                            float pixel_scale, const nkui::RenderCommand &command) {
    PathCacheKey key{};
    key.path = path.id;
    key.pixel_scale = float_bits(pixel_scale);
    key.kind = static_cast<uint32_t>(command.kind);
    key.stroke_width = float_bits(command.stroke_width);
    key.line_cap = command.line_cap;
    key.line_join = command.line_join;
    key.miter_limit = float_bits(command.miter_limit);
    for (size_t index = 0; index < transform.size(); ++index)
        key.transform[index] = float_bits(transform[index]);
    return key;
}

float average_scale(const std::array<float, 6> &transform) {
    const float x_scale = std::sqrt(transform[0] * transform[0] + transform[2] * transform[2]);
    const float y_scale = std::sqrt(transform[1] * transform[1] + transform[3] * transform[3]);
    return (x_scale + y_scale) * 0.5f;
}

uint64_t geometry_memory_bytes(const nkui::PreparedGeometry &geometry) {
    return static_cast<uint64_t>(geometry.paths.capacity()) * sizeof(nkui::PreparedPathRange) +
           static_cast<uint64_t>(geometry.vertices.capacity()) * sizeof(nkui::PreparedVertex);
}

void clear_path_cache(RendererSlot &renderer) {
    renderer.paths.clear();
    renderer.stats.path_geometry_bytes_retained = 0;
}

bool uniform_scale(const std::array<float, 6> &matrix, float &scale) {
    constexpr float epsilon = 0.0001f;
    if (std::abs(matrix[1]) > epsilon || std::abs(matrix[2]) > epsilon || matrix[0] <= 0.0f ||
        matrix[3] <= 0.0f || std::abs(matrix[0] - matrix[3]) > epsilon)
        return false;
    scale = matrix[0];
    return true;
}

std::array<float, 6> device_transform(const std::array<float, 6> &transform, float pixel_scale) {
    std::array<float, 6> result = transform;
    for (float &value : result)
        value *= pixel_scale;
    return result;
}

// Reserve the first transient slot for the offscreen frame root. Compositor and
// layout compilation start their per-frame allocations after it when this root
// is active, so backdrop frames do not grow the target pool by one permanent ID.
constexpr uint16_t kBackdropRootTargetSlot = 0x8000;

nkui::ResourceId backdrop_root_target() {
    return nkui::make_resource_id(nkui::ResourceKind::RenderTarget, 1, kBackdropRootTargetSlot);
}

void append_backdrop_window_composite(nkui::RenderPlan &plan, nkui::ResourceId root_target,
                                      nkui::ResourceId window_target, bool load_existing) {
    nkui::RenderPass window_pass;
    window_pass.target = window_target;
    window_pass.load_existing = load_existing;
    window_pass.commands.push_back({nkui::RenderCommandKind::CompositeTarget, root_target});
    plan.passes.push_back(std::move(window_pass));
    plan.dependencies.push_back({root_target, window_target});
}

void accumulate_render_plan_stats(nkui_renderer_stats &stats, const nkui::RenderPlan &plan) {
    stats.isolated_layers += plan.isolated_layers;
    stats.bounded_layers += plan.bounded_layers;
    for (const auto &pass : plan.passes) {
        if (pass.kind == nkui::RenderPassKind::Effect) {
            ++stats.effect_passes;
            if (pass.backdrop)
                ++stats.backdrop_passes;
        } else if (pass.kind == nkui::RenderPassKind::Mask) {
            ++stats.mask_passes;
        }
    }
}

std::array<float, 6> tessellation_transform(const std::array<float, 6> &transform) {
    return {transform[0], transform[1], transform[2], transform[3], 0.0f, 0.0f};
}

std::array<float, 6> placement_transform(const std::array<float, 6> &transform) {
    return {1.0f, 0.0f, 0.0f, 1.0f, transform[4], transform[5]};
}

nkui::PreparedPaint paint_color(ResourceSlot *paint, const std::array<float, 6> &tessellation);

PreparedPathCacheEntry *prepare_cached_path(RendererSlot &renderer, nkui_resource path_handle,
                                            const ResourceSlot &path,
                                            const std::array<float, 6> &transform,
                                            float pixel_scale, const nkui::RenderCommand &command) {
    const PathCacheKey key = path_cache_key(path_handle, transform, pixel_scale, command);
    if (const auto found = renderer.paths.find(key); found != renderer.paths.end()) {
        ++renderer.stats.path_cache_hits;
        return &found->second;
    }
    ++renderer.stats.path_cache_misses;
    constexpr size_t max_cached_paths = 256;
    if (renderer.paths.size() >= max_cached_paths)
        clear_path_cache(renderer);
    if (!path.path || !path.path->valid())
        return nullptr;
    nkui::PathPreparationParams params;
    params.device_pixel_ratio = pixel_scale;
    params.transform = transform;
    const bool stroke = command.kind == nkui::RenderCommandKind::StrokePath;
    if (stroke) {
        params.stroke_width = command.stroke_width * average_scale(transform);
        params.line_cap = static_cast<nkui::PathLineCap>(command.line_cap);
        params.line_join = static_cast<nkui::PathLineJoin>(command.line_join);
        params.miter_limit = command.miter_limit;
    }
    nkui::PreparedGeometry geometry;
    ++renderer.stats.path_preparations;
    const auto start = std::chrono::steady_clock::now();
    const bool prepared = stroke ? nkui::prepare_stroke(*path.path, params, geometry)
                                 : nkui::prepare_fill(*path.path, params, geometry);
    renderer.stats.path_tessellation_nanoseconds +=
        static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now() - start)
                                  .count());
    if (!prepared)
        return nullptr;
    const uint64_t geometry_bytes = geometry_memory_bytes(geometry);
    renderer.stats.path_vertices_generated += geometry.vertices.size();
    renderer.stats.path_geometry_bytes_allocated += geometry_bytes;
    {
        auto cached_geometry = std::make_shared<const nkui::PreparedGeometry>(std::move(geometry));
        auto [found, inserted] = renderer.paths.emplace(key, PreparedPathCacheEntry{});
        if (!inserted)
            return &found->second;
        found->second.geometry = std::move(cached_geometry);
        renderer.stats.path_geometry_bytes_retained += geometry_bytes;
        return &found->second;
    }
}

nkui::PreparedPaint paint_color(ResourceSlot *paint, const std::array<float, 6> &tessellation) {
    nkui::PreparedPaint result{};
    result.transform[0] = result.transform[3] = 1.0f;
    result.feather = 1.0f;
    const nkui_color color = paint ? paint->color : nkui_color{0.0f, 0.0f, 0.0f, 1.0f};
    result.inner_color = {color.red, color.green, color.blue, color.alpha};
    result.outer_color = result.inner_color;
    if (!paint || paint->paint_kind != nkui::PreparedPaintKind::LinearGradient)
        return result;

    result.kind = nkui::PreparedPaintKind::LinearGradient;
    const auto transform_point = [&tessellation](const std::array<float, 2> &point) {
        return std::array<float, 2>{point[0] * tessellation[0] + point[1] * tessellation[2],
                                    point[0] * tessellation[1] + point[1] * tessellation[3]};
    };
    const auto start = transform_point(paint->gradient_start);
    const auto end = transform_point(paint->gradient_end);
    result.gradient_start[0] = start[0];
    result.gradient_start[1] = start[1];
    result.gradient_end[0] = end[0];
    result.gradient_end[1] = end[1];
    result.gradient_stop_count = static_cast<uint32_t>(paint->gradient_stops.size());
    for (uint32_t index = 0; index < result.gradient_stop_count; ++index) {
        const auto &stop = paint->gradient_stops[index];
        result.gradient_stops[index].offset = stop.offset;
        result.gradient_stops[index].color = {stop.color.red, stop.color.green, stop.color.blue,
                                              stop.color.alpha};
    }
    result.inner_color = result.gradient_stops[0].color;
    result.outer_color = result.gradient_stops[result.gradient_stop_count - 1].color;
    return result;
}

nkui_result allocate_resource(nkui::ResourceKind kind, nkui_resource *out,
                              ResourceSlot **out_slot) {
    if (!out)
        return NKUI_ERROR_INVALID_ARGUMENT;
    out->id = 0;
    const uint32_t slot_limit = kind == nkui::ResourceKind::RenderTarget ? 0x7FFFu : UINT16_MAX;
    const uint32_t first_slot = kind == nkui::ResourceKind::RenderTarget ? 1u : 0u;
    const uint32_t reusable_slots = std::min<uint32_t>(resources.size(), slot_limit);
    for (uint32_t index = first_slot; index < reusable_slots; ++index) {
        auto &entry = resources[index];
        if (entry.kind == nkui::ResourceKind{}) {
            entry.kind = kind;
            entry.externally_alive = true;
            entry.display_refs = 0;
            out->id =
                nkui::make_resource_id(kind, entry.generation, static_cast<uint16_t>(index + 1))
                    .value;
            *out_slot = &entry;
            return NKUI_OK;
        }
    }
    if (kind == nkui::ResourceKind::RenderTarget && resources.empty()) {
        resources.emplace_back(); // Slot one is reserved for the window render target.
    }
    if (resources.size() >= slot_limit)
        return NKUI_ERROR_OUT_OF_MEMORY;
    resources.emplace_back();
    resources.back().kind = kind;
    resources.back().externally_alive = true;
    resources.back().display_refs = 0;
    out->id = nkui::make_resource_id(kind, 1, static_cast<uint16_t>(resources.size())).value;
    *out_slot = &resources.back();
    return NKUI_OK;
}

void release_resource_slot(ResourceSlot &slot) {
    if (slot.graphics_image.id) {
        nk_graphics_image_release(slot.graphics_image);
        slot.graphics_image = {};
    }
    slot.surface.reset();
    slot.text.reset();
    slot.text_glyphs = {};
    slot.scaled_text_glyphs.clear();
    slot.text_width = 0.0f;
    slot.text_options = {};
    slot.fonts.clear();
    slot.font_collection.reset();
    slot.system_fallbacks = false;
    slot.path.reset();
    slot.paint_kind = nkui::PreparedPaintKind::Solid;
    slot.gradient_start = {};
    slot.gradient_end = {};
    slot.gradient_stops.clear();
    slot.pixels.clear();
    slot.color = {};
    slot.image_width = 0;
    slot.image_height = 0;
    slot.image_format = NKUI_IMAGE_FORMAT_INVALID;
    slot.image_filter = NKUI_IMAGE_FILTER_LINEAR;
    slot.externally_alive = false;
    slot.display_refs = 0;
    slot.kind = {};
    slot.generation = static_cast<uint16_t>((slot.generation % 0x0FFF) + 1);
}

bool retain_display_resource(nkui::ResourceId id) {
    const auto kind = static_cast<nkui::ResourceKind>(id.value >> 28);
    auto *slot = resolve(nkui_resource{id.value}, kind);
    if (!slot)
        return false;
    ++slot->display_refs;
    return true;
}

void release_display_resource(nkui::ResourceId id) {
    const auto kind = static_cast<nkui::ResourceKind>(id.value >> 28);
    auto *slot = resolve_retained(nkui_resource{id.value}, kind);
    if (!slot || !slot->display_refs)
        return;
    --slot->display_refs;
    if (!slot->externally_alive && !slot->display_refs)
        release_resource_slot(*slot);
}

void release_display_resources(DisplayListSlot &list) {
    for (const auto id : list.resources)
        release_display_resource(id);
    list.resources.clear();
}

void release_custom_paints(LayoutSessionState &session) {
    for (const auto &[node_id, handle] : session.custom_paints) {
        (void)node_id;
        auto *list = resolve(handle);
        if (list && list->custom_refs)
            --list->custom_refs;
    }
    session.custom_paints.clear();
}

bool collect_display_resources(const uint8_t *data, size_t size,
                               std::vector<nkui::ResourceId> &out) {
    size_t offset = 0;
    while (offset < size) {
        nkui::CommandHeader header{};
        std::memcpy(&header, data + offset, sizeof(header));
        const auto append = [&](nkui::ResourceId id) {
            const auto found = std::find_if(out.begin(), out.end(), [id](nkui::ResourceId value) {
                return value.value == id.value;
            });
            if (found == out.end())
                out.push_back(id);
        };
        switch (header.opcode) {
        case nkui::CommandOpcode::SetPaint:
            append(reinterpret_cast<const nkui::SetPaintCommand *>(data + offset)->paint);
            break;
        case nkui::CommandOpcode::DrawPath:
            append(reinterpret_cast<const nkui::DrawResourceCommand *>(data + offset)->resource);
            break;
        case nkui::CommandOpcode::StrokePath:
            append(reinterpret_cast<const nkui::StrokePathCommand *>(data + offset)->path);
            break;
        case nkui::CommandOpcode::DrawImage:
        case nkui::CommandOpcode::DrawTextLayout:
        case nkui::CommandOpcode::DrawRenderTarget:
            append(
                reinterpret_cast<const nkui::DrawRectResourceCommand *>(data + offset)->resource);
            break;
        default:
            break;
        }
        offset += header.size;
    }
    return true;
}

} // namespace

extern "C" uint32_t nkui_api_version(void) {
    return NKUI_API_VERSION;
}

#if defined(NKUI_ENABLE_SHOWCASE_PRODUCER)
extern "C" NKUI_API nkui_result nkui_showcase_cube_create(nkui_resource *out_surface) {
    if (!out_surface)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    ResourceSlot *slot = nullptr;
    const nkui_result allocated =
        allocate_resource(nkui::ResourceKind::RenderTarget, out_surface, &slot);
    if (allocated != NKUI_OK)
        return allocated;
    slot->surface = std::make_unique<nkui::CubeSurfaceProducer>();
    return NKUI_OK;
}

extern "C" NKUI_API nkui_result nkui_showcase_cube_set_rotation(nkui_resource surface,
                                                                float radians) {
    if (!std::isfinite(radians))
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(surface, nkui::ResourceKind::RenderTarget);
    auto *cube = slot ? dynamic_cast<nkui::CubeSurfaceProducer *>(slot->surface.get()) : nullptr;
    if (!cube)
        return NKUI_ERROR_INVALID_HANDLE;
    cube->set_rotation(radians);
    return NKUI_OK;
}
#endif

extern "C" nkui_result nkui_display_list_create(nkui_display_list *out_list) {
    if (!out_list)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(lists_mutex);
    {
        for (uint32_t index = 0; index < lists.size(); ++index) {
            auto &slot = lists[index];
            if (!slot.list) {
                slot.list = std::make_unique<nkui::DisplayList>();
                out_list->id = make_handle(slot.generation, static_cast<uint16_t>(index + 1));
                return NKUI_OK;
            }
        }
        if (lists.size() >= UINT16_MAX)
            return NKUI_ERROR_OUT_OF_MEMORY;
        lists.push_back({std::make_unique<nkui::DisplayList>(), {}, 0, 1});
        out_list->id = make_handle(1, static_cast<uint16_t>(lists.size()));
        return NKUI_OK;
    }
}

extern "C" nkui_result nkui_display_list_destroy(nkui_display_list list) {
    std::scoped_lock lock(lists_mutex, resources_mutex);
    auto *slot = resolve(list);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    if (slot->custom_refs)
        return NKUI_ERROR_INVALID_ARGUMENT;
    release_display_resources(*slot);
    slot->list.reset();
    if (++slot->generation == 0)
        slot->generation = 1;
    return NKUI_OK;
}

extern "C" nkui_result nkui_display_list_reset(nkui_display_list list) {
    std::scoped_lock lock(lists_mutex, resources_mutex);
    auto *slot = resolve(list);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    release_display_resources(*slot);
    slot->list->reset();
    return NKUI_OK;
}

extern "C" nkui_result nkui_display_list_submit(nkui_display_list list, const uint8_t *commands,
                                                uint32_t command_bytes) {
    if (!nkui::validate_display_list(commands, command_bytes))
        return NKUI_ERROR_INVALID_TRANSACTION;
    std::scoped_lock lock(lists_mutex, resources_mutex);
    auto *slot = resolve(list);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    std::vector<nkui::ResourceId> retained;
    collect_display_resources(commands, command_bytes, retained);
    size_t retained_count = 0;
    for (const auto id : retained) {
        if (retain_display_resource(id)) {
            ++retained_count;
            continue;
        }
        for (size_t index = 0; index < retained_count; ++index)
            release_display_resource(retained[index]);
        return NKUI_ERROR_INVALID_HANDLE;
    }
    if (!slot->list->assign_validated(commands, command_bytes)) {
        for (const auto id : retained)
            release_display_resource(id);
        return NKUI_ERROR_OUT_OF_MEMORY;
    }
    release_display_resources(*slot);
    slot->resources = std::move(retained);
    return NKUI_OK;
}

extern "C" nkui_result nkui_display_list_get_info(nkui_display_list list,
                                                  nkui_transaction_info *out_info) {
    if (!out_info)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(lists_mutex);
    auto *slot = resolve(list);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    *out_info = {sizeof(*out_info), NKUI_API_VERSION, static_cast<uint32_t>(slot->list->size()),
                 slot->list->command_count()};
    return NKUI_OK;
}

extern "C" nkui_result nkui_font_collection_create(nkui_resource *out_fonts) {
    std::lock_guard<std::mutex> lock(resources_mutex);
    ResourceSlot *slot = nullptr;
    const auto result = allocate_resource(nkui::ResourceKind::FontCollection, out_fonts, &slot);
    if (result != NKUI_OK)
        return result;
    slot->font_collection = std::make_shared<nkui::SkribidiFontCollection>();
    if (!slot->font_collection->valid()) {
        release_resource_slot(*slot);
        out_fonts->id = 0;
        return NKUI_ERROR_OUT_OF_MEMORY;
    }
    return NKUI_OK;
}

extern "C" nkui_result nkui_font_collection_add(nkui_resource fonts, const char *path,
                                                nkui_font_family family) {
    if (!path || !*path || (family != NKUI_FONT_FAMILY_DEFAULT && family != NKUI_FONT_FAMILY_EMOJI))
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(fonts, nkui::ResourceKind::FontCollection);
    if (!slot || !slot->font_collection)
        return NKUI_ERROR_INVALID_HANDLE;
    const auto mutable_result = ensure_mutable_font_collection(*slot);
    if (mutable_result != NKUI_OK)
        return mutable_result;
    {
        slot->fonts.push_back({path, family == NKUI_FONT_FAMILY_EMOJI ? nkui::FontFamily::Emoji
                                                                      : nkui::FontFamily::Default});
        const auto &entry = slot->fonts.back();
        if (!slot->font_collection->add_font(entry.path.c_str(), entry.family)) {
            slot->fonts.pop_back();
            return NKUI_ERROR_INVALID_ARGUMENT;
        }
    }
    return NKUI_OK;
}

extern "C" nkui_result nkui_font_collection_add_data(nkui_resource fonts, const char *name,
                                                     const uint8_t *font_data, uint32_t font_bytes,
                                                     nkui_font_family family) {
    if (!name || !*name || !font_data || !font_bytes ||
        (family != NKUI_FONT_FAMILY_DEFAULT && family != NKUI_FONT_FAMILY_EMOJI))
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(fonts, nkui::ResourceKind::FontCollection);
    if (!slot || !slot->font_collection)
        return NKUI_ERROR_INVALID_HANDLE;
    const auto mutable_result = ensure_mutable_font_collection(*slot);
    if (mutable_result != NKUI_OK)
        return mutable_result;
    {
        auto data = std::make_shared<std::vector<uint8_t>>(font_data, font_data + font_bytes);
        slot->fonts.push_back(
            {name,
             family == NKUI_FONT_FAMILY_EMOJI ? nkui::FontFamily::Emoji : nkui::FontFamily::Default,
             std::move(data)});
        const auto &entry = slot->fonts.back();
        if (!slot->font_collection->add_font_from_shared_data(entry.path.c_str(), entry.data,
                                                              entry.family)) {
            slot->fonts.pop_back();
            return NKUI_ERROR_INVALID_ARGUMENT;
        }
    }
    return NKUI_OK;
}

extern "C" nkui_result nkui_font_collection_add_system_fallbacks(nkui_resource fonts) {
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(fonts, nkui::ResourceKind::FontCollection);
    if (!slot || !slot->font_collection)
        return NKUI_ERROR_INVALID_HANDLE;
    const auto mutable_result = ensure_mutable_font_collection(*slot);
    if (mutable_result != NKUI_OK)
        return mutable_result;
    if (!slot->font_collection->add_system_fallbacks())
        return NKUI_ERROR_INVALID_ARGUMENT;
    slot->system_fallbacks = true;
    return NKUI_OK;
}

extern "C" nkui_result nkui_layout_session_create(nkui_layout_session *out_session) {
    if (!out_session)
        return NKUI_ERROR_INVALID_ARGUMENT;
    out_session->id = 0;
    std::lock_guard<std::mutex> lock(layout_sessions_mutex);
    {
        for (uint32_t index = 0; index < layout_sessions.size(); ++index) {
            auto &slot = layout_sessions[index];
            if (slot.session)
                continue;
            slot.session = std::make_shared<LayoutSessionState>();
            slot.session->engine = std::make_unique<nkui::LayoutEngine>();
            if (!slot.session->engine->valid()) {
                slot.session.reset();
                return NKUI_ERROR_RENDERING;
            }
            out_session->id = make_handle(slot.generation, static_cast<uint16_t>(index + 1));
            return NKUI_OK;
        }
        if (layout_sessions.size() >= UINT16_MAX)
            return NKUI_ERROR_OUT_OF_MEMORY;
        LayoutSessionSlot slot;
        slot.session = std::make_shared<LayoutSessionState>();
        slot.session->engine = std::make_unique<nkui::LayoutEngine>();
        if (!slot.session->engine->valid())
            return NKUI_ERROR_RENDERING;
        layout_sessions.push_back(std::move(slot));
        out_session->id = make_handle(1, static_cast<uint16_t>(layout_sessions.size()));
        return NKUI_OK;
    }
}

extern "C" nkui_result nkui_layout_session_destroy(nkui_layout_session session) {
    if (active_measure_session)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::scoped_lock lock(layout_sessions_mutex, lists_mutex);
    const uint16_t slot_index = static_cast<uint16_t>(session.id);
    auto *state = resolve(session);
    if (!state || !slot_index)
        return NKUI_ERROR_INVALID_HANDLE;
    auto &slot = layout_sessions[slot_index - 1];
    release_custom_paints(*state);
    slot.session.reset();
    slot.generation = static_cast<uint16_t>(slot.generation + 1);
    if (!slot.generation)
        slot.generation = 1;
    return NKUI_OK;
}

extern "C" nkui_result nkui_layout_session_set_font_collection(nkui_layout_session session,
                                                               nkui_resource fonts) {
    if (active_measure_session)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::scoped_lock lock(layout_sessions_mutex, resources_mutex);
    auto *state = resolve(session);
    auto *font_slot = resolve(fonts, nkui::ResourceKind::FontCollection);
    if (!state || !font_slot || state->fonts_configured)
        return NKUI_ERROR_INVALID_HANDLE;
    {
        if (!font_slot->font_collection || !font_slot->font_collection->valid())
            return NKUI_ERROR_INVALID_HANDLE;
        const auto shared_fonts = font_slot->font_collection;
        state->engine = std::make_unique<nkui::LayoutEngine>(shared_fonts);
        if (!state->engine->valid())
            return NKUI_ERROR_OUT_OF_MEMORY;
        state->compiler.set_font_collection(shared_fonts);
        configure_layout_measure_callback(*state);
        state->fonts_configured = true;
    }
    return NKUI_OK;
}

extern "C" nkui_result nkui_layout_session_set_measure_callback(
    nkui_layout_session session, nkui_nullable_layout_measure_callback callback, void *user_data) {
    if (active_measure_session)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(layout_sessions_mutex);
    auto *state = resolve(session);
    if (!state)
        return NKUI_ERROR_INVALID_HANDLE;
    {
        state->measure_callback = callback;
        state->measure_user_data = callback ? user_data : nullptr;
        configure_layout_measure_callback(*state);
    }
    return NKUI_OK;
}

extern "C" nkui_result nkui_layout_session_get_measure_stats(nkui_layout_session session,
                                                             nkui_layout_measure_stats *out_stats) {
    if (!out_stats)
        return NKUI_ERROR_INVALID_ARGUMENT;
    if (active_measure_session)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(layout_sessions_mutex);
    auto *state = resolve(session);
    if (!state || !state->engine)
        return NKUI_ERROR_INVALID_HANDLE;
    const auto stats = state->engine->measure_stats();
    *out_stats = {};
    out_stats->struct_size = sizeof(*out_stats);
    out_stats->requests = stats.requests;
    out_stats->cache_hits = stats.cache_hits;
    out_stats->cache_misses = stats.cache_misses;
    out_stats->callback_calls = stats.callback_calls;
    out_stats->cache_entries = stats.cache_entries;
    out_stats->cache_capacity = stats.cache_capacity;
    return NKUI_OK;
}

extern "C" nkui_result nkui_layout_session_clear_custom_paints(nkui_layout_session session) {
    if (active_measure_session)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::scoped_lock lock(layout_sessions_mutex, lists_mutex);
    auto *state = resolve(session);
    if (!state)
        return NKUI_ERROR_INVALID_HANDLE;
    release_custom_paints(*state);
    return NKUI_OK;
}

extern "C" nkui_result nkui_layout_session_set_custom_paint(nkui_layout_session session,
                                                            uint32_t node_id,
                                                            nkui_display_list display_list) {
    if (active_measure_session)
        return NKUI_ERROR_INVALID_ARGUMENT;
    if (!node_id || !display_list.id)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::scoped_lock lock(layout_sessions_mutex, lists_mutex);
    auto *state = resolve(session);
    auto *list = resolve(display_list);
    if (!state || !list)
        return NKUI_ERROR_INVALID_HANDLE;
    const auto *item = state->submitted ? state->snapshot.find(node_id) : nullptr;
    if (!item || item->visual_kind != nkui::LayoutVisualKind::Custom)
        return NKUI_ERROR_INVALID_ARGUMENT;
    const auto existing = state->custom_paints.find(node_id);
    if (existing != state->custom_paints.end() && existing->second.id == display_list.id)
        return NKUI_OK;
    const bool replacing = existing != state->custom_paints.end();
    const nkui_display_list previous_handle = replacing ? existing->second : nkui_display_list{};
    if (list->custom_refs == std::numeric_limits<uint32_t>::max())
        return NKUI_ERROR_OUT_OF_MEMORY;
    if (!replacing)
        state->custom_paints.emplace(node_id, display_list);
    else
        state->custom_paints.find(node_id)->second = display_list;
    ++list->custom_refs;
    if (replacing) {
        auto *previous = resolve(previous_handle);
        if (previous && previous->custom_refs)
            --previous->custom_refs;
    }
    return NKUI_OK;
}

extern "C" nkui_result nkui_layout_session_submit(nkui_layout_session session,
                                                  const uint8_t *transaction,
                                                  uint32_t transaction_bytes,
                                                  const nkui_layout_frame_input *frame) {
    if (!frame || frame->struct_size < sizeof(*frame))
        return NKUI_ERROR_INVALID_ARGUMENT;
    if (active_measure_session)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::shared_ptr<LayoutSessionState> pinned_state;
    {
        std::lock_guard<std::mutex> lock(layout_sessions_mutex);
        auto *resolved = resolve(session);
        if (!resolved)
            return NKUI_ERROR_INVALID_HANDLE;
        const auto slot = static_cast<uint16_t>(session.id);
        pinned_state = layout_sessions[slot - 1].session;
    }
    auto *state = pinned_state.get();
    std::unique_lock<std::mutex> session_lock(state->mutex);
    std::vector<nkui::LayoutNode> nodes;
    if (!read_layout_transaction(transaction, transaction_bytes, nodes))
        return NKUI_ERROR_INVALID_TRANSACTION;
    const bool has_text = std::any_of(nodes.begin(), nodes.end(), [](const nkui::LayoutNode &node) {
        return node.visual_kind == nkui::LayoutVisualKind::Text && !node.text.empty();
    });
    if (has_text && !state->fonts_configured)
        return NKUI_ERROR_INVALID_ARGUMENT;
    nkui::LayoutSnapshot snapshot;
    nkui::LayoutError error{};
    if (!state->engine->layout(nodes, frame->width, frame->height, frame->delta_seconds, snapshot,
                               &error))
        return NKUI_ERROR_INVALID_TRANSACTION;
    state->snapshot = std::move(snapshot);
    state->submitted = true;
    std::lock_guard<std::mutex> lists_lock(lists_mutex);
    for (auto it = state->custom_paints.begin(); it != state->custom_paints.end();) {
        const auto *item = state->snapshot.find(it->first);
        if (item && item->visual_kind == nkui::LayoutVisualKind::Custom) {
            ++it;
            continue;
        }
        auto *list = resolve(it->second);
        if (list && list->custom_refs)
            --list->custom_refs;
        it = state->custom_paints.erase(it);
    }
    return NKUI_OK;
}

extern "C" nkui_result nkui_layout_session_get_resolved_items(nkui_layout_session session,
                                                              uint8_t *out_buffer,
                                                              uint32_t *inout_bytes) {
    if (!inout_bytes)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(layout_sessions_mutex);
    auto *state = resolve(session);
    if (!state)
        return NKUI_ERROR_INVALID_HANDLE;
    if (!state->submitted)
        return NKUI_ERROR_INVALID_ARGUMENT;

    const size_t required_bytes = state->snapshot.items.size() * sizeof(nkui_layout_item);
    if (required_bytes > UINT32_MAX)
        return NKUI_ERROR_OUT_OF_MEMORY;
    const uint32_t required = static_cast<uint32_t>(required_bytes);
    if (!out_buffer) {
        *inout_bytes = required;
        return NKUI_OK;
    }
    if (*inout_bytes < required) {
        *inout_bytes = required;
        return NKUI_ERROR_INVALID_ARGUMENT;
    }
    for (size_t index = 0; index < state->snapshot.items.size(); ++index) {
        const auto &resolved = state->snapshot.items[index];
        nkui_layout_item item{};
        item.struct_size = sizeof(item);
        item.node_id = resolved.id;
        item.flags = (resolved.visible ? NKUI_LAYOUT_RESOLVED_VISIBLE : 0u) |
                     (resolved.has_baseline ? NKUI_LAYOUT_RESOLVED_HAS_BASELINE : 0u);
        item.x = resolved.bounds.x;
        item.y = resolved.bounds.y;
        item.width = resolved.bounds.width;
        item.height = resolved.bounds.height;
        item.clip_x = resolved.clip_bounds.x;
        item.clip_y = resolved.clip_bounds.y;
        item.clip_width = resolved.clip_bounds.width;
        item.clip_height = resolved.clip_bounds.height;
        item.content_x = resolved.content_bounds.x;
        item.content_y = resolved.content_bounds.y;
        item.content_width = resolved.content_bounds.width;
        item.content_height = resolved.content_bounds.height;
        item.transform[0] = resolved.transform.a;
        item.transform[1] = resolved.transform.b;
        item.transform[2] = resolved.transform.c;
        item.transform[3] = resolved.transform.d;
        item.transform[4] = resolved.transform.tx;
        item.transform[5] = resolved.transform.ty;
        item.baseline = resolved.baseline;
        std::memcpy(out_buffer + index * sizeof(item), &item, sizeof(item));
    }
    *inout_bytes = required;
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_create(nkui_resource fonts, const char *text, float width,
                                               float font_size, nkui_resource *out_layout) {
    if (!out_layout || !std::isfinite(width) || !std::isfinite(font_size) || width <= 0.0f ||
        font_size <= 0.0f)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    nkui::TextLayoutOptions options;
    options.font_size = font_size;
    return create_text_layout_locked(fonts, text ? text : "", width, options, out_layout);
}

extern "C" nkui_result nkui_text_layout_create_styled(nkui_resource fonts, const char *text,
                                                      float width,
                                                      const nkui_text_style *text_style,
                                                      const nkui_paragraph_style *paragraph_style,
                                                      nkui_resource *out_layout) {
    if (!out_layout || !std::isfinite(width) || width <= 0.0f)
        return NKUI_ERROR_INVALID_ARGUMENT;
    nkui::TextLayoutOptions options;
    if (!text_options_from_api(text_style, paragraph_style, options))
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    return create_text_layout_locked(fonts, text ? text : "", width, options, out_layout);
}

extern "C" nkui_result nkui_text_layout_update(nkui_resource layout, const char *text, float width,
                                               const nkui_text_style *text_style,
                                               const nkui_paragraph_style *paragraph_style) {
    if (!std::isfinite(width) || width <= 0.0f)
        return NKUI_ERROR_INVALID_ARGUMENT;
    nkui::TextLayoutOptions options;
    if (!text_options_from_api(text_style, paragraph_style, options))
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot || !slot->text)
        return NKUI_ERROR_INVALID_HANDLE;
    nkui::TextLayoutResult shaped;
    if (!slot->text->layout_utf8(text ? text : "", width, options, &shaped))
        return NKUI_ERROR_INVALID_ARGUMENT;
    nkui::PreparedGlyphs updated;
    if (!slot->text->prepare_glyphs(0.0f, 0.0f, 1.0f, nkui::GlyphMode::Alpha, updated))
        return NKUI_ERROR_RENDERING;
    slot->text_width = width;
    slot->text_options = options;
    slot->text_glyphs = std::move(updated);
    slot->scaled_text_glyphs.clear();
    slot->text->prune_layout_cache({shaped.id}, 1);
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_set_text(nkui_resource layout, const char *text) {
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot || !slot->text || slot->text_width <= 0.0f)
        return NKUI_ERROR_INVALID_HANDLE;
    nkui::TextLayoutResult shaped;
    if (!slot->text->layout_utf8(text ? text : "", slot->text_width, slot->text_options, &shaped))
        return NKUI_ERROR_INVALID_ARGUMENT;
    nkui::PreparedGlyphs updated;
    if (!slot->text->prepare_glyphs(0.0f, 0.0f, 1.0f, nkui::GlyphMode::Alpha, updated))
        return NKUI_ERROR_RENDERING;
    slot->text_glyphs = std::move(updated);
    slot->scaled_text_glyphs.clear();
    slot->text->prune_layout_cache({shaped.id}, 1);
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_measure(nkui_resource layout,
                                                nkui_text_metrics *out_metrics) {
    if (!out_metrics)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    const auto bounds = slot->text->bounds();
    *out_metrics = {sizeof(*out_metrics), bounds.x, bounds.y, bounds.width, bounds.height};
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_hit_test(nkui_resource layout, float x, float y,
                                                 nkui_text_position *out_position) {
    if (!out_position || !std::isfinite(x) || !std::isfinite(y))
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    const auto position = slot->text->hit_test(x, y);
    *out_position = {position.offset, position.affinity};
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_position_offset(nkui_resource layout,
                                                        nkui_text_position position,
                                                        int32_t *out_offset) {
    if (!out_offset || position.offset < 0 || position.affinity > 4u)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot || !slot->text)
        return NKUI_ERROR_INVALID_HANDLE;
    *out_offset = slot->text->offset_from_position(
        {position.offset, static_cast<uint8_t>(position.affinity)});
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_caret(nkui_resource layout, nkui_text_position position,
                                              nkui_text_caret *out_caret) {
    if (!out_caret || position.offset < 0 || position.affinity > 4)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    const auto caret =
        slot->text->caret({position.offset, static_cast<uint8_t>(position.affinity)});
    *out_caret = {sizeof(*out_caret), caret.x,     caret.y,        caret.ascender,
                  caret.descender,    caret.slope, caret.direction};
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_get_selection_rects(nkui_resource layout,
                                                            nkui_text_position start,
                                                            nkui_text_position end,
                                                            uint8_t *out_buffer,
                                                            uint32_t *inout_bytes) {
    if (!inout_bytes || start.offset < 0 || end.offset < 0 || start.affinity > 4 ||
        end.affinity > 4)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot || !slot->text)
        return NKUI_ERROR_INVALID_HANDLE;
    std::vector<nkui::TextRect> rectangles;
    rectangles = slot->text->selection_rects({start.offset, static_cast<uint8_t>(start.affinity)},
                                             {end.offset, static_cast<uint8_t>(end.affinity)});
    if (rectangles.size() > std::numeric_limits<uint32_t>::max() / sizeof(nkui_text_rect))
        return NKUI_ERROR_OUT_OF_MEMORY;
    const uint32_t required = static_cast<uint32_t>(rectangles.size() * sizeof(nkui_text_rect));
    if (!out_buffer) {
        *inout_bytes = required;
        return NKUI_OK;
    }
    if (*inout_bytes < required) {
        *inout_bytes = required;
        return NKUI_ERROR_INVALID_ARGUMENT;
    }
    for (std::size_t index = 0; index < rectangles.size(); ++index) {
        const auto &rect = rectangles[index];
        if (!std::isfinite(rect.x) || !std::isfinite(rect.y) || !std::isfinite(rect.width) ||
            !std::isfinite(rect.height))
            return NKUI_ERROR_RENDERING;
        const nkui_text_rect result{sizeof(nkui_text_rect), rect.x, rect.y, rect.width,
                                    rect.height};
        std::memcpy(out_buffer + index * sizeof(result), &result, sizeof(result));
    }
    *inout_bytes = required;
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_next_grapheme(nkui_resource layout, int32_t offset,
                                                      int32_t *out_offset) {
    if (!out_offset || offset < 0)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot || !slot->text)
        return NKUI_ERROR_INVALID_HANDLE;
    *out_offset = slot->text->next_grapheme(offset);
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_previous_grapheme(nkui_resource layout, int32_t offset,
                                                          int32_t *out_offset) {
    if (!out_offset || offset < 0)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot || !slot->text)
        return NKUI_ERROR_INVALID_HANDLE;
    *out_offset = slot->text->previous_grapheme(offset);
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_align_grapheme(nkui_resource layout, int32_t offset,
                                                       int32_t *out_offset) {
    if (!out_offset || offset < 0)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot || !slot->text)
        return NKUI_ERROR_INVALID_HANDLE;
    *out_offset = slot->text->align_grapheme(offset);
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_word_range_at(nkui_resource layout, int32_t offset,
                                                      int32_t *out_start, int32_t *out_end) {
    if (!out_start || !out_end || offset < 0)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot || !slot->text)
        return NKUI_ERROR_INVALID_HANDLE;
    const auto range = slot->text->word_range_at(offset);
    *out_start = range.start;
    *out_end = range.end;
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_line_range_at(nkui_resource layout, int32_t offset,
                                                      int32_t *out_start, int32_t *out_end) {
    if (!out_start || !out_end || offset < 0)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot || !slot->text)
        return NKUI_ERROR_INVALID_HANDLE;
    const auto range = slot->text->line_range_at(offset);
    *out_start = range.start;
    *out_end = range.end;
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_move_word(nkui_resource layout, int32_t offset,
                                                  int32_t direction,
                                                  nkui_text_navigation_behavior behavior,
                                                  int32_t *out_offset) {
    if (!out_offset || offset < 0 || (direction != -1 && direction != 1) ||
        behavior > NKUI_TEXT_NAVIGATION_BEHAVIOR_MACOS)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot || !slot->text)
        return NKUI_ERROR_INVALID_HANDLE;
    *out_offset =
        slot->text->move_word(offset, direction, behavior == NKUI_TEXT_NAVIGATION_BEHAVIOR_MACOS);
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_move_paragraph(nkui_resource layout, int32_t offset,
                                                       int32_t direction,
                                                       nkui_text_navigation_behavior behavior,
                                                       int32_t *out_offset) {
    if (!out_offset || offset < 0 || (direction != -1 && direction != 1) ||
        behavior > NKUI_TEXT_NAVIGATION_BEHAVIOR_MACOS)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot || !slot->text)
        return NKUI_ERROR_INVALID_HANDLE;
    *out_offset = slot->text->move_paragraph(offset, direction,
                                             behavior == NKUI_TEXT_NAVIGATION_BEHAVIOR_MACOS);
    return NKUI_OK;
}

extern "C" nkui_result nkui_text_layout_word_range(nkui_resource layout,
                                                   nkui_text_position position, int32_t *out_start,
                                                   int32_t *out_end) {
    if (!out_start || !out_end || position.offset < 0 || position.affinity > 4u)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    auto *slot = resolve(layout, nkui::ResourceKind::TextLayout);
    if (!slot || !slot->text)
        return NKUI_ERROR_INVALID_HANDLE;
    const nkui::TextPosition input{position.offset, static_cast<uint8_t>(position.affinity)};
    *out_start = slot->text->offset_from_position(slot->text->word_start(input));
    *out_end = slot->text->offset_from_position(slot->text->word_end(input));
    return NKUI_OK;
}

extern "C" nkui_result nkui_resource_destroy(nkui_resource resource) {
    std::lock_guard<std::mutex> lock(resources_mutex);
    const uint16_t slot_index = static_cast<uint16_t>(resource.id);
    const uint16_t generation = static_cast<uint16_t>((resource.id >> 16) & 0x0FFF);
    if (!slot_index || slot_index > resources.size())
        return NKUI_ERROR_INVALID_HANDLE;
    auto &slot = resources[slot_index - 1];
    if (slot.kind == nkui::ResourceKind{} || slot.generation != generation ||
        !slot.externally_alive)
        return NKUI_ERROR_INVALID_HANDLE;
    slot.externally_alive = false;
    if (!slot.display_refs)
        release_resource_slot(slot);
    return NKUI_OK;
}

extern "C" nkui_result nkui_path_create(const nkui_path_element *elements, uint32_t count,
                                        nkui_resource *out_path) {
    if (!elements || !count || !out_path)
        return NKUI_ERROR_INVALID_ARGUMENT;
    bool has_geometry = false;
    for (uint32_t index = 0; index < count; ++index) {
        const auto &element = elements[index];
        uint32_t value_count = 0;
        switch (element.verb) {
        case NKUI_PATH_MOVE_TO:
        case NKUI_PATH_LINE_TO:
            value_count = 2;
            has_geometry = true;
            break;
        case NKUI_PATH_BEZIER_TO:
            value_count = 6;
            has_geometry = true;
            break;
        case NKUI_PATH_QUADRATIC_TO:
            value_count = 4;
            has_geometry = true;
            break;
        case NKUI_PATH_ARC_TO:
            value_count = 5;
            has_geometry = true;
            break;
        case NKUI_PATH_CLOSE:
            break;
        default:
            return NKUI_ERROR_INVALID_ARGUMENT;
        }
        for (uint32_t value = 0; value < value_count; ++value)
            if (!std::isfinite(element.values[value]))
                return NKUI_ERROR_INVALID_ARGUMENT;
    }
    if (!has_geometry)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    ResourceSlot *slot = nullptr;
    const auto result = allocate_resource(nkui::ResourceKind::Path, out_path, &slot);
    if (result != NKUI_OK)
        return result;
    {
        auto path = std::make_unique<nkui::NanoVGPath>();
        if (!path->valid() ||
            !append_path(*path, std::vector<nkui_path_element>(elements, elements + count))) {
            release_resource_slot(*slot);
            out_path->id = 0;
            return NKUI_ERROR_OUT_OF_MEMORY;
        }
        slot->path = std::move(path);
    }
    return NKUI_OK;
}

extern "C" nkui_result nkui_paint_create_solid(nkui_color color, nkui_resource *out_paint) {
    if (!out_paint || !std::isfinite(color.red) || !std::isfinite(color.green) ||
        !std::isfinite(color.blue) || !std::isfinite(color.alpha))
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    ResourceSlot *slot = nullptr;
    const auto result = allocate_resource(nkui::ResourceKind::Paint, out_paint, &slot);
    if (result == NKUI_OK)
        slot->color = color;
    return result;
}

extern "C" nkui_result nkui_paint_create_linear_gradient(float start_x, float start_y, float end_x,
                                                         float end_y,
                                                         const nkui_gradient_stop *stops,
                                                         uint32_t stop_count,
                                                         nkui_resource *out_paint) {
    if (!out_paint || !stops || stop_count < 2 || stop_count > NKUI_GRADIENT_MAX_STOPS ||
        !std::isfinite(start_x) || !std::isfinite(start_y) || !std::isfinite(end_x) ||
        !std::isfinite(end_y))
        return NKUI_ERROR_INVALID_ARGUMENT;
    const float delta_x = end_x - start_x;
    const float delta_y = end_y - start_y;
    if (!std::isfinite(delta_x) || !std::isfinite(delta_y) || (delta_x == 0.0f && delta_y == 0.0f))
        return NKUI_ERROR_INVALID_ARGUMENT;
    float previous_offset = -1.0f;
    for (uint32_t index = 0; index < stop_count; ++index) {
        const auto &stop = stops[index];
        if (!std::isfinite(stop.offset) || stop.offset < 0.0f || stop.offset > 1.0f ||
            stop.offset <= previous_offset || !std::isfinite(stop.color.red) ||
            !std::isfinite(stop.color.green) || !std::isfinite(stop.color.blue) ||
            !std::isfinite(stop.color.alpha) || stop.color.red < 0.0f || stop.color.red > 1.0f ||
            stop.color.green < 0.0f || stop.color.green > 1.0f || stop.color.blue < 0.0f ||
            stop.color.blue > 1.0f || stop.color.alpha < 0.0f || stop.color.alpha > 1.0f)
            return NKUI_ERROR_INVALID_ARGUMENT;
        previous_offset = stop.offset;
    }

    std::lock_guard<std::mutex> lock(resources_mutex);
    ResourceSlot *slot = nullptr;
    const auto result = allocate_resource(nkui::ResourceKind::Paint, out_paint, &slot);
    if (result != NKUI_OK)
        return result;
    {
        slot->paint_kind = nkui::PreparedPaintKind::LinearGradient;
        slot->gradient_start = {start_x, start_y};
        slot->gradient_end = {end_x, end_y};
        slot->gradient_stops.assign(stops, stops + stop_count);
    }
    return NKUI_OK;
}

extern "C" nkui_result nkui_image_create(uint32_t width, uint32_t height, nkui_image_format format,
                                         const uint8_t *pixels, uint32_t pixel_bytes,
                                         nkui_resource *out_image) {
    return nkui_image_create_filtered(width, height, format, pixels, pixel_bytes,
                                      NKUI_IMAGE_FILTER_LINEAR, out_image);
}

extern "C" nkui_result nkui_image_create_filtered(uint32_t width, uint32_t height,
                                                  nkui_image_format format, const uint8_t *pixels,
                                                  uint32_t pixel_bytes, nkui_image_filter filter,
                                                  nkui_resource *out_image) {
    const uint32_t bytes_per_pixel = format == NKUI_IMAGE_R8      ? 1
                                     : format == NKUI_IMAGE_RGBA8 ? 4
                                                                  : 0;
    const uint64_t required = static_cast<uint64_t>(width) * height * bytes_per_pixel;
    if (!out_image || !pixels || !width || !height || !bytes_per_pixel || required != pixel_bytes ||
        required > UINT32_MAX ||
        (filter != NKUI_IMAGE_FILTER_LINEAR && filter != NKUI_IMAGE_FILTER_NEAREST))
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(resources_mutex);
    ResourceSlot *slot = nullptr;
    const auto result = allocate_resource(nkui::ResourceKind::Image, out_image, &slot);
    if (result != NKUI_OK)
        return result;
    slot->pixels.assign(pixels, pixels + pixel_bytes);
    slot->image_width = width;
    slot->image_height = height;
    slot->image_format = format;
    slot->image_filter = filter;
    return NKUI_OK;
}

extern "C" nkui_result nkui_image_load_file(const char *path, nkui_image_filter filter,
                                            uint32_t *out_width, uint32_t *out_height,
                                            nkui_resource *out_image) {
    if (!path || !*path || !out_width || !out_height || !out_image ||
        (filter != NKUI_IMAGE_FILTER_LINEAR && filter != NKUI_IMAGE_FILTER_NEAREST))
        return NKUI_ERROR_INVALID_ARGUMENT;
    *out_width = 0;
    *out_height = 0;
    out_image->id = 0;
    nkui::DecodedImage decoded;
    if (!nkui::default_image_decoder().decode_file(path, decoded))
        return NKUI_ERROR_INVALID_ARGUMENT;
    if (decoded.rgba8.size() > UINT32_MAX)
        return NKUI_ERROR_OUT_OF_MEMORY;
    const nkui_result result = nkui_image_create_filtered(
        decoded.width, decoded.height, NKUI_IMAGE_RGBA8, decoded.rgba8.data(),
        static_cast<uint32_t>(decoded.rgba8.size()), filter, out_image);
    if (result == NKUI_OK) {
        *out_width = decoded.width;
        *out_height = decoded.height;
    }
    return result;
}

extern "C" nkui_result nkui_graphics_surface_create(nk_graphics_image image,
                                                    nkui_resource *out_surface) {
    if (!out_surface || !image.id)
        return NKUI_ERROR_INVALID_ARGUMENT;
    out_surface->id = 0;
    nk_graphics_image_info info{};
    info.struct_size = sizeof(info);
    if (nk_graphics_image_get_info(image, &info) != NK_OK || !info.device.id || info.width <= 0 ||
        info.height <= 0)
        return NKUI_ERROR_INVALID_HANDLE;
    if (nk_graphics_image_retain(image) != NK_OK)
        return NKUI_ERROR_INVALID_HANDLE;
    std::lock_guard<std::mutex> lock(resources_mutex);
    ResourceSlot *slot = nullptr;
    const auto result = allocate_resource(nkui::ResourceKind::RenderTarget, out_surface, &slot);
    if (result != NKUI_OK) {
        nk_graphics_image_release(image);
        return result;
    }
    slot->graphics_image = image;
    return NKUI_OK;
}

extern "C" nkui_result nkui_renderer_create(nkui_renderer *out_renderer) {
    if (!out_renderer)
        return NKUI_ERROR_INVALID_ARGUMENT;
    ensure_runtime_shutdown_hook();
    out_renderer->id = 0;
    std::lock_guard<std::mutex> lock(renderers_mutex);
    {
        for (uint32_t index = 0; index < renderers.size(); ++index) {
            auto &slot = renderers[index];
            if (!slot.active) {
                slot.renderer.reset();
                slot.backend_api = 0;
                slot.backend_device = {};
                slot.backend_surface = 0;
                slot.custom_effects.clear();
                slot.registered_custom_effects = 0;
                slot.active = true;
                slot.retired_gpu = {};
                slot.stats = {};
                out_renderer->id = make_handle(slot.generation, static_cast<uint16_t>(index + 1));
                return NKUI_OK;
            }
        }
        if (renderers.size() >= UINT16_MAX)
            return NKUI_ERROR_OUT_OF_MEMORY;
        renderers.emplace_back();
        auto &slot = renderers.back();
        slot.active = true;
        slot.custom_effects.clear();
        slot.registered_custom_effects = 0;
        slot.retired_gpu = {};
        slot.stats = {};
        out_renderer->id = make_handle(1, static_cast<uint16_t>(renderers.size()));
        return NKUI_OK;
    }
}

extern "C" nkui_result nkui_renderer_destroy(nkui_renderer renderer) {
    std::lock_guard<std::mutex> lock(renderers_mutex);
    auto *slot = resolve(renderer);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    std::unique_lock<std::shared_mutex> execution_lock;
    if (nk::core::render_executor_physical())
        execution_lock = std::unique_lock<std::shared_mutex>(renderer_execution_mutex);
    if (nk::core::render_executor_physical() && slot->renderer) {
        auto *destroy = new DeferredRendererDestroy{std::move(slot->renderer)};
        if (nk::core::dispatch_to_render(&run_deferred_renderer_destroy, destroy,
                                         &destroy_deferred_renderer,
                                         sizeof(DeferredRendererDestroy)) != NK_OK) {
            /* Keep destruction on the owning thread if the runtime is already
               shutting down and cannot accept another render task. */
            destroy->renderer.reset();
            delete destroy;
        }
    } else {
        slot->renderer.reset();
    }
    slot->backend_api = 0;
    slot->backend_device = {};
    slot->backend_surface = 0;
    slot->active = false;
    slot->custom_effects.clear();
    slot->registered_custom_effects = 0;
    clear_path_cache(*slot);
    slot->retired_gpu = {};
    slot->stats = {};
    slot->generation = static_cast<uint16_t>(slot->generation + 1);
    if (!slot->generation)
        slot->generation = 1;
    return NKUI_OK;
}

extern "C" nkui_result
nkui_renderer_register_custom_effect(nkui_renderer renderer,
                                     const nkui_custom_effect_registration *registration) {
    if (!registration || registration->struct_size < sizeof(*registration) ||
        !registration->registration_id || !registration->name || !registration->name[0] ||
        registration->parameter_components > NKUI_CUSTOM_EFFECT_PARAMETER_COMPONENTS ||
        registration->pass_count != 1 || registration->sampling_inputs != 1 ||
        (!registration->glsl410_fragment && !registration->glsl300es_fragment &&
         !registration->hlsl5_fragment && !registration->metal_macos_fragment))
        return NKUI_ERROR_INVALID_ARGUMENT;
    for (const float value : registration->ink_overflow)
        if (!std::isfinite(value) || value < 0.0f)
            return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(renderers_mutex);
    auto *slot = resolve(renderer);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    for (const auto &existing : slot->custom_effects)
        if (existing.registration_id == registration->registration_id)
            return NKUI_ERROR_INVALID_ARGUMENT;
    {
        CustomEffectRegistrationStorage stored{};
        stored.registration_id = registration->registration_id;
        stored.name = registration->name;
        if (registration->glsl410_fragment)
            stored.glsl410_fragment = registration->glsl410_fragment;
        if (registration->glsl300es_fragment)
            stored.glsl300es_fragment = registration->glsl300es_fragment;
        if (registration->hlsl5_fragment)
            stored.hlsl5_fragment = registration->hlsl5_fragment;
        if (registration->metal_macos_fragment)
            stored.metal_macos_fragment = registration->metal_macos_fragment;
        stored.parameter_components = registration->parameter_components;
        stored.pass_count = registration->pass_count;
        stored.sampling_inputs = registration->sampling_inputs;
        std::copy(std::begin(registration->ink_overflow), std::end(registration->ink_overflow),
                  stored.ink_overflow.begin());
        slot->custom_effects.push_back(std::move(stored));
        if (!nk::core::render_executor_physical() && slot->renderer &&
            !slot->renderer->registerCustomEffect(slot->custom_effects.back().native())) {
            slot->custom_effects.pop_back();
            return NKUI_ERROR_RENDERING;
        }
        if (!nk::core::render_executor_physical() && slot->renderer)
            ++slot->registered_custom_effects;
    }
    return NKUI_OK;
}

extern "C" nkui_result nkui_renderer_get_stats(nkui_renderer renderer,
                                               nkui_renderer_stats *out_stats) {
    if (!out_stats)
        return NKUI_ERROR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(renderers_mutex);
    std::unique_lock<std::shared_mutex> execution_lock;
    if (nk::core::render_executor_physical())
        execution_lock = std::unique_lock<std::shared_mutex>(renderer_execution_mutex);
    std::unique_lock<std::mutex> cpu_lock(renderer_cpu_mutex);
    auto *slot = resolve(renderer);
    if (!slot)
        return NKUI_ERROR_INVALID_HANDLE;
    *out_stats = slot->stats;
    out_stats->struct_size = sizeof(*out_stats);
    const nkui::UiGpuStats &retired = slot->retired_gpu;
    out_stats->gpu_frames = retired.frames;
    out_stats->gpu_passes = retired.passes;
    out_stats->gpu_draw_calls = retired.draw_calls;
    out_stats->upload_bytes = retired.upload_bytes;
    out_stats->resource_creations = retired.resource_creations;
    out_stats->resource_destructions = retired.resource_destructions;
    out_stats->surface_recreations = retired.surface_recreations;
    out_stats->device_losses = retired.device_losses;
    out_stats->failed_allocations = retired.failed_allocations;
    if (slot->renderer) {
        const nkui::UiRendererStats ui_stats = slot->renderer->stats();
        const nkui::UiGpuStats &gpu = ui_stats.gpu;
        out_stats->gpu_frames += gpu.frames;
        out_stats->gpu_passes += gpu.passes;
        out_stats->gpu_draw_calls += gpu.draw_calls;
        out_stats->buffers_live = gpu.buffers_live;
        out_stats->images_live = gpu.images_live;
        out_stats->samplers_live = gpu.samplers_live;
        out_stats->shaders_live = gpu.shaders_live;
        out_stats->pipelines_live = gpu.pipelines_live;
        out_stats->render_targets_live = gpu.render_targets_live;
        out_stats->buffer_bytes = gpu.buffer_bytes;
        out_stats->image_bytes = gpu.image_bytes;
        out_stats->render_target_bytes = gpu.render_target_bytes;
        out_stats->upload_bytes += gpu.upload_bytes;
        out_stats->resource_creations += gpu.resource_creations;
        out_stats->resource_destructions += gpu.resource_destructions;
        out_stats->surface_recreations += gpu.surface_recreations;
        out_stats->device_losses += gpu.device_losses;
        out_stats->failed_allocations += gpu.failed_allocations;
        out_stats->atlas_pages = ui_stats.atlas_pages;
        out_stats->atlas_bytes = ui_stats.atlas_bytes;
        out_stats->glyph_uploads = slot->stats.glyph_uploads + ui_stats.glyph_uploads;
        out_stats->glyphs_rasterized = ui_stats.glyphs_rasterized;
        out_stats->atlas_rebuilds = slot->stats.atlas_rebuilds + ui_stats.atlas_rebuilds;
        out_stats->atlas_partial_updates =
            slot->stats.atlas_partial_updates + ui_stats.atlas_partial_updates;
        out_stats->atlas_dirty_upload_bytes =
            slot->stats.atlas_dirty_upload_bytes + ui_stats.atlas_dirty_upload_bytes;
        out_stats->atlas_scale_generation =
            std::max<uint64_t>(slot->stats.atlas_scale_generation, ui_stats.atlas_scale_generation);
        out_stats->transient_target_pool_hits =
            slot->stats.transient_target_pool_hits + ui_stats.transient_target_pool_hits;
        out_stats->transient_target_pool_misses =
            slot->stats.transient_target_pool_misses + ui_stats.transient_target_pool_misses;
        out_stats->transient_target_pool_count = ui_stats.transient_target_pool_count;
        out_stats->transient_target_pool_bytes = ui_stats.transient_target_pool_bytes;
        out_stats->effect_cache_hits = slot->stats.effect_cache_hits + ui_stats.effect_cache_hits;
        out_stats->effect_cache_misses =
            slot->stats.effect_cache_misses + ui_stats.effect_cache_misses;
        out_stats->effect_cache_entries = ui_stats.effect_cache_entries;
        out_stats->effect_cache_bytes = ui_stats.effect_cache_bytes;
        out_stats->text_layout_cache_hits = ui_stats.text_layout_cache_hits;
        out_stats->text_layout_cache_misses = ui_stats.text_layout_cache_misses;
    }
    return NKUI_OK;
}

static nkui_result renderer_render_frame_impl(nkui_renderer renderer, nkui_display_list list,
                                              nk_surface surface, const nkui_frame_info *frame_info,
                                              bool load_existing) {
    if (!frame_info || frame_info->struct_size < sizeof(*frame_info) ||
        !std::isfinite(frame_info->logical_width) || !std::isfinite(frame_info->logical_height) ||
        !std::isfinite(frame_info->pixel_scale) || frame_info->logical_width <= 0.0f ||
        frame_info->logical_height <= 0.0f || frame_info->framebuffer_width <= 0 ||
        frame_info->framebuffer_height <= 0 || frame_info->pixel_scale <= 0.0f)
        return NKUI_ERROR_INVALID_ARGUMENT;
    const int32_t width = frame_info->framebuffer_width;
    const int32_t height = frame_info->framebuffer_height;
    const bool threaded = nk::core::render_executor_physical();
    if (!surface || nk_surface_set_frame_mode(surface, NK_SURFACE_FRAME_ON_DEMAND) != NK_OK)
        return NKUI_ERROR_INVALID_ARGUMENT;
    nk_surface_frame frame = NK_INVALID_HANDLE;
    struct AcquiredFrameGuard {
        nk_surface_frame frame = NK_INVALID_HANDLE;
        bool handed_off = false;
        ~AcquiredFrameGuard() {
            if (frame != NK_INVALID_HANDLE && !handed_off)
                nk_surface_cancel_frame(frame);
        }
    } frame_guard;
    nk_surface_frame_target frame_target{};
    frame_target.struct_size = sizeof(frame_target);
    if (threaded) {
        if (nk_surface_acquire_frame(surface, &frame, &frame_target) != NK_OK)
            return NKUI_ERROR_RENDERING;
        frame_guard.frame = frame;
    } else {
        if (nk_surface_make_current(surface) != NK_OK ||
            nk_surface_get_frame_target(surface, &frame_target) != NK_OK)
            return NKUI_ERROR_RENDERING;
    }
    std::unique_lock<std::mutex> renderer_lock(renderers_mutex, std::defer_lock);
    std::unique_lock<std::mutex> lists_lock(lists_mutex, std::defer_lock);
    std::unique_lock<std::mutex> resources_lock(resources_mutex, std::defer_lock);
    std::lock(renderer_lock, lists_lock, resources_lock);
    std::unique_lock<std::mutex> cpu_lock(renderer_cpu_mutex);
    auto *renderer_slot = resolve(renderer);
    auto *list_slot = resolve(list);
    if (!renderer_slot || !list_slot)
        return NKUI_ERROR_INVALID_HANDLE;
    if (!threaded)
        discard_stale_renderer(*renderer_slot, frame_target, surface);
    if (!threaded && !renderer_slot->renderer) {
        auto ui_renderer = nkui::create_ui_renderer(surface);
        if (!ui_renderer)
            return NKUI_ERROR_RENDERING;
        renderer_slot->renderer = std::move(ui_renderer);
        renderer_slot->backend_api = frame_target.api;
        renderer_slot->backend_device = frame_target.device;
        renderer_slot->backend_surface = surface;
    }
    const nkui::ResourceId main_target =
        nkui::make_resource_id(nkui::ResourceKind::RenderTarget, 1, 1);
    const bool has_backdrop = list_slot->list->has_backdrop_effects();
    const nkui::ResourceId compile_target = has_backdrop ? backdrop_root_target() : main_target;
    nkui::RenderPlan plan;
    if (!renderer_slot->compositor.compile(*list_slot->list, compile_target, plan))
        return NKUI_ERROR_INVALID_TRANSACTION;
    if (has_backdrop)
        append_backdrop_window_composite(plan, compile_target, main_target, load_existing);
    accumulate_render_plan_stats(renderer_slot->stats, plan);
    // Compositor geometry is expressed in logical pixels. Resolve bounded
    // transient targets to physical dimensions only at the frame boundary,
    // where the device pixel ratio is known.
    for (auto &pass : plan.passes) {
        if (pass.target_descriptor.logical_width > 0.0f) {
            const double width =
                static_cast<double>(pass.target_descriptor.logical_width) * frame_info->pixel_scale;
            const double height = static_cast<double>(pass.target_descriptor.logical_height) *
                                  frame_info->pixel_scale;
            if (!std::isfinite(width) || !std::isfinite(height) || width > INT32_MAX ||
                height > INT32_MAX)
                return NKUI_ERROR_INVALID_ARGUMENT;
            pass.target_descriptor.width = std::max(1, static_cast<int>(std::ceil(width)));
            pass.target_descriptor.height = std::max(1, static_cast<int>(std::ceil(height)));
        }
        if (!nkui::scale_render_plan_parameters(pass, frame_info->pixel_scale))
            return NKUI_ERROR_INVALID_ARGUMENT;
    }
    add_effect_cache_pixel_scale(plan, frame_info->pixel_scale);
    ++renderer_slot->stats.display_list_count;
    renderer_slot->stats.display_list_bytes += list_slot->list->size();
    for (const auto &pass : plan.passes)
        renderer_slot->stats.render_plan_commands += pass.commands.size();
    if (load_existing && !plan.passes.empty())
        plan.passes.front().load_existing = true;

    nkui::FrameResources frame_resources;
    nkui::OwnedFrameResources owned_resources;
    bool sealable = true;
    std::vector<std::shared_ptr<nkui::PreparedPath>> prepared_paths;
    std::vector<std::shared_ptr<nkui::PreparedTexture>> prepared_images;
    std::vector<nkui::SkribidiAdapter *> text_adapters;
    std::vector<std::shared_ptr<nkui::SkribidiAdapter>> text_adapter_owners;
    std::vector<std::pair<nkui::SkribidiAdapter *, nkui::PreparedGlyphs *>> prepared_texts;
    struct OwnedTextBind {
        nkui::ResourceId id{};
        nkui::SkribidiAdapter *adapter = nullptr;
        float pixel_scale = 1.0f;
        nkui::GlyphMode mode = nkui::GlyphMode::Alpha;
        uint64_t content_generation = 0;
    };
    std::vector<OwnedTextBind> owned_text_binds;
    uint16_t prepared_slot = 1;
    bool valid = true;

    // Mask images are pass metadata rather than draw commands, so prepare them
    // before the ordinary command-resource walk below.
    for (const auto &pass : plan.passes) {
        if (pass.kind != nkui::RenderPassKind::Mask || pass.mask.kind != nkui::MaskKind::Image)
            continue;
        auto *image =
            resolve_retained(nkui_resource{pass.mask.image.value}, nkui::ResourceKind::Image);
        if (!image) {
            valid = false;
            break;
        }
        auto prepared = std::make_shared<nkui::PreparedTexture>();
        if (!prepared) {
            valid = false;
            break;
        }
        prepared->token = pass.mask.image.value;
        prepared->type = nkui::PreparedTextureType::Rgba;
        prepared->width = static_cast<int>(image->image_width);
        prepared->height = static_cast<int>(image->image_height);
        prepared->generation = 1;
        prepared->dirty = true;
        if (image->image_format == NKUI_IMAGE_R8) {
            prepared->pixels.resize(image->pixels.size() * 4);
            for (size_t index = 0; index < image->pixels.size(); ++index) {
                prepared->pixels[index * 4 + 0] = 255;
                prepared->pixels[index * 4 + 1] = 255;
                prepared->pixels[index * 4 + 2] = 255;
                prepared->pixels[index * 4 + 3] = image->pixels[index];
            }
        } else {
            prepared->pixels = image->pixels;
        }
        auto *prepared_image = prepared.get();
        prepared_images.push_back(std::move(prepared));
        if (!frame_resources.bind_image(pass.mask.image, *prepared_image,
                                        static_cast<uint64_t>(pass.mask.image.value))) {
            valid = false;
            break;
        }
        if (!owned_resources.bind_image(pass.mask.image, prepared_images.back(),
                                        static_cast<uint64_t>(pass.mask.image.value)))
            sealable = false;
    }

    for (auto &pass : plan.passes) {
        for (auto &command : pass.commands) {
            command.scissor_x *= frame_info->pixel_scale;
            command.scissor_y *= frame_info->pixel_scale;
            command.scissor_width *= frame_info->pixel_scale;
            command.scissor_height *= frame_info->pixel_scale;
            if (command.kind == nkui::RenderCommandKind::Path ||
                command.kind == nkui::RenderCommandKind::StrokePath) {
                auto *path = resolve_retained(nkui_resource{command.resource.value},
                                              nkui::ResourceKind::Path);
                auto *paint = command.paint.value
                                  ? resolve_retained(nkui_resource{command.paint.value},
                                                     nkui::ResourceKind::Paint)
                                  : nullptr;
                if (!path || (command.paint.value && !paint) || !prepared_slot) {
                    valid = false;
                    break;
                }
                const auto transform = device_transform(command.transform, frame_info->pixel_scale);
                const auto tessellation = tessellation_transform(transform);
                const nkui_resource path_handle{command.resource.value};
                const auto cached =
                    prepare_cached_path(*renderer_slot, path_handle, *path, tessellation,
                                        frame_info->pixel_scale, command);
                if (!cached) {
                    valid = false;
                    break;
                }
                auto prepared = std::make_shared<nkui::PreparedPath>();
                const auto kind = command.kind == nkui::RenderCommandKind::StrokePath
                                      ? nkui::PreparedPathKind::Stroke
                                      : nkui::PreparedPathKind::Fill;
                if (!prepared ||
                    !prepared->set_view(kind, cached->geometry, paint_color(paint, tessellation))) {
                    valid = false;
                    break;
                }
                auto *prepared_path = prepared.get();
                prepared_paths.push_back(std::move(prepared));
                const nkui::ResourceId prepared_id =
                    nkui::make_resource_id(nkui::ResourceKind::Path, 0x0FFE, prepared_slot++);
                command.resource = prepared_id;
                command.transform = placement_transform(transform);
                valid = frame_resources.bind_path(prepared_id, *prepared_path, 0,
                                                  static_cast<uint64_t>(path_handle.id));
                if (valid && !owned_resources.bind_path(prepared_id, prepared_paths.back(), 0,
                                                        static_cast<uint64_t>(path_handle.id)))
                    sealable = false;
            } else if (command.kind == nkui::RenderCommandKind::Image) {
                const uint32_t source_resource = command.resource.value;
                auto *image = resolve_retained(nkui_resource{command.resource.value},
                                               nkui::ResourceKind::Image);
                if (!image || !prepared_slot) {
                    valid = false;
                    break;
                }
                auto prepared = std::make_shared<nkui::PreparedTexture>();
                if (!prepared) {
                    valid = false;
                    break;
                }
                prepared->token = command.resource.value;
                prepared->type = nkui::PreparedTextureType::Rgba;
                prepared->width = static_cast<int>(image->image_width);
                prepared->height = static_cast<int>(image->image_height);
                prepared->generation = 1;
                prepared->dirty = true;
                prepared->flags |= nkui::PreparedImageFlags::Premultiplied;
                if (image->image_filter == NKUI_IMAGE_FILTER_NEAREST)
                    prepared->flags |= nkui::PreparedImageFlags::Nearest;
                prepared->pixels = image->image_format == NKUI_IMAGE_R8
                                       ? nkui::prepare_alpha8_pixels(image->pixels)
                                       : nkui::prepare_rgba8_pixels(image->pixels);
                auto *prepared_image = prepared.get();
                prepared_images.push_back(std::move(prepared));
                const nkui::ResourceId prepared_id =
                    nkui::make_resource_id(nkui::ResourceKind::Image, 0x0FFE, prepared_slot++);
                command.resource = prepared_id;
                command.transform = device_transform(command.transform, frame_info->pixel_scale);
                valid = frame_resources.bind_image(prepared_id, *prepared_image,
                                                   static_cast<uint64_t>(source_resource));
                if (valid && !owned_resources.bind_image(prepared_id, prepared_images.back(),
                                                         static_cast<uint64_t>(source_resource)))
                    sealable = false;
            } else if (command.kind == nkui::RenderCommandKind::BoxShadow) {
                command.transform = device_transform(command.transform, frame_info->pixel_scale);
            } else if (command.kind == nkui::RenderCommandKind::GlyphBatch) {
                const uint32_t source_resource = command.resource.value;
                auto *layout = resolve_retained(nkui_resource{command.resource.value},
                                                nkui::ResourceKind::TextLayout);
                if (!layout || !layout->text) {
                    valid = false;
                    break;
                }
                float requested_scale = 1.0f;
                const auto transform = device_transform(command.transform, frame_info->pixel_scale);
                if (!uniform_scale(transform, requested_scale))
                    requested_scale = 1.0f;
                // Coarse 1/8-scale steps can rasterize a fractional-DPR glyph
                // atlas a full texel larger than its destination, shifting thin
                // strokes under nearest sampling at browser zoom levels.
                constexpr int32_t raster_scale_precision = 1024;
                const int32_t raster_scale_key = std::max(
                    1, static_cast<int32_t>(std::round(requested_scale * raster_scale_precision)));
                const float raster_scale =
                    static_cast<float>(raster_scale_key) / raster_scale_precision;
                nkui::PreparedGlyphs *glyphs = nullptr;
                if (raster_scale_key == raster_scale_precision) {
                    glyphs = &layout->text_glyphs;
                    if (!layout->text->prepared_glyphs_current(*glyphs))
                        valid = layout->text->prepare_glyphs(0.0f, 0.0f, raster_scale,
                                                             nkui::GlyphMode::Alpha, *glyphs);
                } else {
                    auto found = layout->scaled_text_glyphs.find(raster_scale_key);
                    if (found == layout->scaled_text_glyphs.end()) {
                        nkui::PreparedGlyphs prepared;
                        valid = layout->text->prepare_glyphs(0.0f, 0.0f, raster_scale,
                                                             nkui::GlyphMode::Alpha, prepared);
                        if (!valid)
                            break;
                        found = layout->scaled_text_glyphs
                                    .emplace(raster_scale_key, std::move(prepared))
                                    .first;
                    }
                    glyphs = &found->second;
                    if (valid && !layout->text->prepared_glyphs_current(*glyphs))
                        valid = layout->text->prepare_glyphs(0.0f, 0.0f, raster_scale,
                                                             nkui::GlyphMode::Alpha, *glyphs);
                }
                if (!valid)
                    break;
                if (valid)
                    prepared_texts.push_back({layout->text.get(), glyphs});
                const nkui::ResourceId prepared_id =
                    nkui::make_resource_id(nkui::ResourceKind::TextLayout, 0x0FFE, prepared_slot++);
                valid = frame_resources.bind_text(prepared_id, *glyphs,
                                                  (static_cast<uint64_t>(source_resource) << 32) ^
                                                      layout->text->layout_generation() ^
                                                      layout->text->font_collection_generation());
                if (valid)
                    owned_text_binds.push_back({prepared_id, layout->text.get(), raster_scale,
                                                nkui::GlyphMode::Alpha,
                                                (static_cast<uint64_t>(source_resource) << 32) ^
                                                    layout->text->layout_generation() ^
                                                    layout->text->font_collection_generation()});
                command.resource = prepared_id;
                // Skribidi's pixel scale changes atlas raster density while
                // preserving layout geometry. Keep the draw origin in layout
                // coordinates and apply the complete device transform here;
                // baking raster_scale into x/y or replacing the transform with
                // a correction scales positions but leaves glyph geometry small.
                command.transform = transform;
                retain_text_adapter(layout->text, text_adapters, text_adapter_owners);
            } else if (command.kind == nkui::RenderCommandKind::CompositeTarget &&
                       command.resource.value < (UINT32_C(4) << 28)) {
                valid = false;
                break;
            } else if (command.kind == nkui::RenderCommandKind::CompositeTarget) {
                const uint16_t target_slot = static_cast<uint16_t>(command.resource.value);
                if (target_slot < 0x8000u && command.resource.value != compile_target.value) {
                    auto *surface_slot = resolve_retained(nkui_resource{command.resource.value},
                                                          nkui::ResourceKind::RenderTarget);
                    if (!surface_slot) {
                        valid = false;
                        break;
                    }
                    if (surface_slot->graphics_image.id) {
                        valid = frame_resources.bind_graphics_image(
                            command.resource, surface_slot->graphics_image,
                            static_cast<uint64_t>(surface_slot->graphics_image.id));
                        if (valid && !owned_resources.bind_graphics_image(
                                         command.resource, surface_slot->graphics_image,
                                         static_cast<uint64_t>(surface_slot->graphics_image.id)))
                            sealable = false;
                    } else if (surface_slot->surface) {
                        const nk_graphics_image published =
                            surface_slot->surface->retained_image();
                        if (published.id) {
                            const auto generation = static_cast<uint64_t>(
                                surface_slot->surface->generation());
                            valid = frame_resources.bind_graphics_image(command.resource, published,
                                                                        generation);
                            if (valid && !owned_resources.bind_graphics_image(
                                             command.resource, published, generation))
                                sealable = false;
                        } else {
                            valid = frame_resources.bind_surface(
                                command.resource, *surface_slot->surface,
                                static_cast<uint64_t>(surface_slot->surface->generation()));
                            /* A live result producer is a callback and cannot be sealed. */
                            sealable = false;
                        }
                    } else {
                        valid = false;
                    }
                    if (!valid)
                        break;
                }
                if (command.width > 0.0f && command.height > 0.0f) {
                    command.transform =
                        device_transform(command.transform, frame_info->pixel_scale);
                } else {
                    command.x *= frame_info->pixel_scale;
                    command.y *= frame_info->pixel_scale;
                    command.width *= frame_info->pixel_scale;
                    command.height *= frame_info->pixel_scale;
                }
            }
        }
        if (!valid)
            break;
    }
    if (!valid)
        return NKUI_ERROR_INVALID_HANDLE;
    for (size_t pass = 0; pass < prepared_texts.size() && valid; ++pass) {
        auto &[adapter, glyphs] = prepared_texts[pass];
        if (!adapter->prepared_glyphs_current(*glyphs))
            valid = adapter->prepare_glyphs(glyphs->origin_x, glyphs->origin_y, glyphs->pixel_scale,
                                            glyphs->mode, *glyphs);
    }
    if (!valid)
        return NKUI_ERROR_RENDERING;
    /*
     * Text is published after the final preparation pass so the owned set can
     * share an immutable snapshot instead of copying glyph buffers.
     */
    for (const auto &bind : owned_text_binds) {
        if (!sealable)
            break;
        auto snapshot = bind.adapter->published_glyphs(bind.adapter->active_layout_id(), 0.0f, 0.0f,
                                                       bind.pixel_scale, bind.mode);
        if (!snapshot ||
            !owned_resources.bind_text(bind.id, std::move(snapshot), bind.content_generation))
            sealable = false;
    }
    if (!sealable && threaded)
        return NKUI_ERROR_RENDERING;
    if (!threaded) {
        const bool new_backend = !renderer_slot->renderer->valid();
        if (new_backend && !renderer_slot->renderer->initialize())
            return NKUI_ERROR_RENDERING;
        if (new_backend && !register_custom_effects(*renderer_slot))
            return NKUI_ERROR_RENDERING;
        for (auto *adapter : text_adapters)
            if (!renderer_slot->renderer->uploadAtlases(*adapter, new_backend))
                return NKUI_ERROR_RENDERING;
    }
    if (sealable) {
        nkui::RenderPlanSealError seal_error;
        auto sealed =
            nkui::SealedRenderPlan::seal(std::move(plan), std::move(owned_resources), &seal_error);
        if (!sealed)
            return NKUI_ERROR_OUT_OF_MEMORY;
        if (threaded) {
            auto *submission = new RenderSubmission{renderer,
                                                    surface,
                                                    frame,
                                                    frame_target,
                                                    std::move(sealed),
                                                    std::move(text_adapters),
                                                    std::move(text_adapter_owners),
                                                    {}};
            renderer_lock.unlock();
            lists_lock.unlock();
            resources_lock.unlock();
            cpu_lock.unlock();
            if (!enqueue_render_submission(submission)) {
                return NKUI_ERROR_RENDERING;
            }
            frame_guard.handed_off = true;
            return NKUI_OK;
        }
        const bool sealed_executed = nkui::execute_render_plan(*renderer_slot->renderer, *sealed,
                                                               {main_target, frame_target});
        return sealed_executed ? NKUI_OK : NKUI_ERROR_RENDERING;
    }
    /* Frames that composite a live surface producer keep the borrowed path. */
    const bool executed = nkui::execute_render_plan(*renderer_slot->renderer, plan, frame_resources,
                                                    {main_target, frame_target});
    return executed ? NKUI_OK : NKUI_ERROR_RENDERING;
}

extern "C" nkui_result nkui_renderer_render_frame(nkui_renderer renderer, nkui_display_list list,
                                                  nk_surface surface,
                                                  const nkui_frame_info *frame_info) {
    return renderer_render_frame_impl(renderer, list, surface, frame_info, false);
}

extern "C" nkui_result nkui_renderer_render_frame_overlay(nkui_renderer renderer,
                                                          nkui_display_list list,
                                                          nk_surface surface,
                                                          const nkui_frame_info *frame_info) {
    return renderer_render_frame_impl(renderer, list, surface, frame_info, true);
}

extern "C" nkui_result nkui_layout_session_render_frame(nkui_renderer renderer,
                                                        nkui_layout_session session,
                                                        nk_surface surface,
                                                        const nkui_frame_info *frame_info,
                                                        nk_bool load_existing) {
    if (!frame_info || frame_info->struct_size < sizeof(*frame_info) ||
        !std::isfinite(frame_info->logical_width) || !std::isfinite(frame_info->logical_height) ||
        !std::isfinite(frame_info->pixel_scale) || frame_info->logical_width <= 0.0f ||
        frame_info->logical_height <= 0.0f || frame_info->framebuffer_width <= 0 ||
        frame_info->framebuffer_height <= 0 || frame_info->pixel_scale <= 0.0f)
        return NKUI_ERROR_INVALID_ARGUMENT;
    const bool threaded = nk::core::render_executor_physical();
    if (!surface || nk_surface_set_frame_mode(surface, NK_SURFACE_FRAME_ON_DEMAND) != NK_OK)
        return NKUI_ERROR_INVALID_ARGUMENT;
    nk_surface_frame frame = NK_INVALID_HANDLE;
    struct AcquiredFrameGuard {
        nk_surface_frame frame = NK_INVALID_HANDLE;
        bool handed_off = false;
        ~AcquiredFrameGuard() {
            if (frame != NK_INVALID_HANDLE && !handed_off)
                nk_surface_cancel_frame(frame);
        }
    } frame_guard;
    nk_surface_frame_target frame_target{};
    frame_target.struct_size = sizeof(frame_target);
    if (threaded) {
        if (nk_surface_acquire_frame(surface, &frame, &frame_target) != NK_OK)
            return NKUI_ERROR_RENDERING;
        frame_guard.frame = frame;
    } else {
        if (nk_surface_make_current(surface) != NK_OK ||
            nk_surface_get_frame_target(surface, &frame_target) != NK_OK)
            return NKUI_ERROR_RENDERING;
    }
    std::unique_lock<std::mutex> renderer_lock(renderers_mutex, std::defer_lock);
    std::unique_lock<std::mutex> lists_lock(lists_mutex, std::defer_lock);
    std::unique_lock<std::mutex> resources_lock(resources_mutex, std::defer_lock);
    std::unique_lock<std::mutex> sessions_lock(layout_sessions_mutex, std::defer_lock);
    std::lock(renderer_lock, lists_lock, resources_lock, sessions_lock);
    std::unique_lock<std::mutex> cpu_lock(renderer_cpu_mutex);
    auto *renderer_slot = resolve(renderer);
    auto *session_state = resolve(session);
    if (!renderer_slot || !session_state || !session_state->submitted)
        return NKUI_ERROR_INVALID_HANDLE;
    if (!threaded)
        discard_stale_renderer(*renderer_slot, frame_target, surface);
    if (!threaded && !renderer_slot->renderer) {
        auto ui_renderer = nkui::create_ui_renderer(surface);
        if (!ui_renderer)
            return NKUI_ERROR_RENDERING;
        renderer_slot->renderer = std::move(ui_renderer);
        renderer_slot->backend_api = frame_target.api;
        renderer_slot->backend_device = frame_target.device;
        renderer_slot->backend_surface = surface;
    }
    const nkui::ResourceId main_target =
        nkui::make_resource_id(nkui::ResourceKind::RenderTarget, 1, 1);
    bool has_backdrop = false;
    for (const auto &[node_id, list_handle] : session_state->custom_paints) {
        (void)node_id;
        auto *list_slot = resolve(list_handle);
        if (list_slot && list_slot->list)
            has_backdrop = has_backdrop || list_slot->list->has_backdrop_effects();
    }
    const nkui::ResourceId compile_target = has_backdrop ? backdrop_root_target() : main_target;
    std::vector<std::pair<uint32_t, nkui::RenderPlan>> custom_plan_storage;
    nkui::LayoutRenderCompiler::CustomPaintPlans custom_plans;
    {
        custom_plan_storage.reserve(session_state->custom_paints.size());
        for (const auto &[node_id, list_handle] : session_state->custom_paints) {
            const auto *item = session_state->snapshot.find(node_id);
            auto *list_slot = resolve(list_handle);
            if (!item || !list_slot)
                return NKUI_ERROR_INVALID_HANDLE;
            nkui::RenderPlan custom_plan;
            nkui::Compositor custom_compositor;
            nkui::CompositorError compositor_error{};
            if (!custom_compositor.compile(*list_slot->list, compile_target, custom_plan,
                                           &compositor_error))
                return NKUI_ERROR_INVALID_TRANSACTION;
            custom_plan_storage.emplace_back(node_id, std::move(custom_plan));
        }
        custom_plans.reserve(custom_plan_storage.size());
        for (const auto &[node_id, custom_plan] : custom_plan_storage)
            custom_plans.emplace(node_id, &custom_plan);
    }
    nkui::LayoutRenderCompileError compile_error{};
    if (!session_state->compiler.compile(session_state->snapshot, compile_target,
                                         frame_info->pixel_scale, session_state->frame,
                                         &compile_error, load_existing != 0,
                                         session_state->engine->text_adapter(), &custom_plans))
        return NKUI_ERROR_INVALID_TRANSACTION;

    if (has_backdrop)
        append_backdrop_window_composite(session_state->frame.plan(), compile_target, main_target,
                                         load_existing != 0);

    auto &plan = session_state->frame.plan();
    add_effect_cache_pixel_scale(plan, frame_info->pixel_scale);
    accumulate_render_plan_stats(renderer_slot->stats, plan);
    for (const auto &[node_id, custom_plan] : custom_plan_storage) {
        (void)custom_plan;
        ++renderer_slot->stats.custom_paint_nodes;
        const auto found = session_state->custom_paints.find(node_id);
        if (found != session_state->custom_paints.end())
            if (auto *list_slot = resolve(found->second))
                renderer_slot->stats.custom_paint_bytes += list_slot->list->size();
    }
    for (const auto &pass : plan.passes)
        renderer_slot->stats.render_plan_commands += pass.commands.size();
    auto &frame_resources = session_state->frame.resources();
    auto &owned_resources = session_state->frame.owned_resources();
    bool sealable = session_state->frame.sealable();
    std::vector<std::shared_ptr<nkui::PreparedPath>> custom_paths;
    std::vector<std::shared_ptr<nkui::PreparedTexture>> custom_images;
    std::vector<nkui::SkribidiAdapter *> text_adapters;
    std::vector<std::shared_ptr<nkui::SkribidiAdapter>> text_adapter_owners;
    std::vector<std::pair<nkui::SkribidiAdapter *, nkui::PreparedGlyphs *>> prepared_texts;
    uint32_t prepared_slot = 1;
    bool valid = true;
    for (auto &pass : plan.passes) {
        if (pass.kind == nkui::RenderPassKind::Mask && pass.mask.kind == nkui::MaskKind::Image) {
            auto *image =
                resolve_retained(nkui_resource{pass.mask.image.value}, nkui::ResourceKind::Image);
            if (!image) {
                valid = false;
                break;
            }
            auto prepared = std::make_shared<nkui::PreparedTexture>();
            if (!prepared) {
                valid = false;
                break;
            }
            prepared->token = pass.mask.image.value;
            prepared->type = nkui::PreparedTextureType::Rgba;
            prepared->width = static_cast<int>(image->image_width);
            prepared->height = static_cast<int>(image->image_height);
            prepared->generation = 1;
            prepared->dirty = true;
            if (image->image_format == NKUI_IMAGE_R8) {
                prepared->pixels.resize(image->pixels.size() * 4);
                for (size_t index = 0; index < image->pixels.size(); ++index) {
                    prepared->pixels[index * 4 + 0] = 255;
                    prepared->pixels[index * 4 + 1] = 255;
                    prepared->pixels[index * 4 + 2] = 255;
                    prepared->pixels[index * 4 + 3] = image->pixels[index];
                }
            } else {
                prepared->pixels = image->pixels;
            }
            auto *prepared_image = prepared.get();
            custom_images.push_back(std::move(prepared));
            if (!frame_resources.bind_image(pass.mask.image, *prepared_image,
                                            static_cast<uint64_t>(pass.mask.image.value))) {
                valid = false;
                break;
            }
            if (!owned_resources.bind_image(pass.mask.image, custom_images.back(),
                                            static_cast<uint64_t>(pass.mask.image.value)))
                sealable = false;
        }
        for (auto &command : pass.commands) {
            if (!command.custom_payload)
                continue;
            if (command.kind == nkui::RenderCommandKind::Path ||
                command.kind == nkui::RenderCommandKind::StrokePath) {
                auto *path = resolve_retained(nkui_resource{command.resource.value},
                                              nkui::ResourceKind::Path);
                auto *paint = command.paint.value
                                  ? resolve_retained(nkui_resource{command.paint.value},
                                                     nkui::ResourceKind::Paint)
                                  : nullptr;
                if (!path || (command.paint.value && !paint) ||
                    prepared_slot > std::numeric_limits<uint16_t>::max()) {
                    valid = false;
                    break;
                }
                const auto transform = command.transform;
                const auto tessellation = tessellation_transform(transform);
                const nkui_resource path_handle{command.resource.value};
                const auto cached =
                    prepare_cached_path(*renderer_slot, path_handle, *path, tessellation,
                                        frame_info->pixel_scale, command);
                if (!cached) {
                    valid = false;
                    break;
                }
                auto prepared = std::make_shared<nkui::PreparedPath>();
                const auto kind = command.kind == nkui::RenderCommandKind::StrokePath
                                      ? nkui::PreparedPathKind::Stroke
                                      : nkui::PreparedPathKind::Fill;
                if (!prepared ||
                    !prepared->set_view(kind, cached->geometry, paint_color(paint, tessellation))) {
                    valid = false;
                    break;
                }
                auto *prepared_path = prepared.get();
                custom_paths.push_back(std::move(prepared));
                const auto prepared_id = nkui::make_resource_id(
                    nkui::ResourceKind::Path, 0x0FFD, static_cast<uint16_t>(prepared_slot++));
                command.resource = prepared_id;
                command.transform = placement_transform(transform);
                valid = frame_resources.bind_path(prepared_id, *prepared_path, 0,
                                                  static_cast<uint64_t>(path_handle.id));
                if (valid && !owned_resources.bind_path(prepared_id, custom_paths.back(), 0,
                                                        static_cast<uint64_t>(path_handle.id)))
                    sealable = false;
            } else if (command.kind == nkui::RenderCommandKind::Image) {
                const uint32_t source_resource = command.resource.value;
                auto *image = resolve_retained(nkui_resource{command.resource.value},
                                               nkui::ResourceKind::Image);
                if (!image || prepared_slot > std::numeric_limits<uint16_t>::max()) {
                    valid = false;
                    break;
                }
                auto prepared = std::make_shared<nkui::PreparedTexture>();
                if (!prepared) {
                    valid = false;
                    break;
                }
                prepared->token = command.resource.value;
                prepared->type = nkui::PreparedTextureType::Rgba;
                prepared->width = static_cast<int>(image->image_width);
                prepared->height = static_cast<int>(image->image_height);
                prepared->generation = 1;
                prepared->dirty = true;
                prepared->flags |= nkui::PreparedImageFlags::Premultiplied;
                if (image->image_filter == NKUI_IMAGE_FILTER_NEAREST)
                    prepared->flags |= nkui::PreparedImageFlags::Nearest;
                prepared->pixels = image->image_format == NKUI_IMAGE_R8
                                       ? nkui::prepare_alpha8_pixels(image->pixels)
                                       : nkui::prepare_rgba8_pixels(image->pixels);
                auto *prepared_image = prepared.get();
                custom_images.push_back(std::move(prepared));
                const auto prepared_id = nkui::make_resource_id(
                    nkui::ResourceKind::Image, 0x0FFD, static_cast<uint16_t>(prepared_slot++));
                command.resource = prepared_id;
                valid = frame_resources.bind_image(prepared_id, *prepared_image,
                                                   static_cast<uint64_t>(source_resource));
                if (valid && !owned_resources.bind_image(prepared_id, custom_images.back(),
                                                         static_cast<uint64_t>(source_resource)))
                    sealable = false;
            } else if (command.kind == nkui::RenderCommandKind::GlyphBatch) {
                const uint32_t source_resource = command.resource.value;
                auto *layout = resolve_retained(nkui_resource{command.resource.value},
                                                nkui::ResourceKind::TextLayout);
                if (!layout || !layout->text ||
                    prepared_slot > std::numeric_limits<uint16_t>::max()) {
                    valid = false;
                    break;
                }
                float requested_scale = 1.0f;
                if (!uniform_scale(command.transform, requested_scale))
                    requested_scale = 1.0f;
                constexpr int32_t raster_scale_precision = 1024;
                const int32_t raster_scale_key = std::max(
                    1, static_cast<int32_t>(std::round(requested_scale * raster_scale_precision)));
                const float raster_scale =
                    static_cast<float>(raster_scale_key) / raster_scale_precision;
                nkui::PreparedGlyphs *glyphs = nullptr;
                if (raster_scale_key == raster_scale_precision) {
                    glyphs = &layout->text_glyphs;
                    if (!layout->text->prepared_glyphs_current(*glyphs))
                        valid = layout->text->prepare_glyphs(0.0f, 0.0f, raster_scale,
                                                             nkui::GlyphMode::Alpha, *glyphs);
                } else {
                    auto found = layout->scaled_text_glyphs.find(raster_scale_key);
                    if (found == layout->scaled_text_glyphs.end()) {
                        nkui::PreparedGlyphs prepared;
                        valid = layout->text->prepare_glyphs(0.0f, 0.0f, raster_scale,
                                                             nkui::GlyphMode::Alpha, prepared);
                        if (!valid)
                            break;
                        found = layout->scaled_text_glyphs
                                    .emplace(raster_scale_key, std::move(prepared))
                                    .first;
                    }
                    glyphs = &found->second;
                    if (valid && !layout->text->prepared_glyphs_current(*glyphs))
                        valid = layout->text->prepare_glyphs(0.0f, 0.0f, raster_scale,
                                                             nkui::GlyphMode::Alpha, *glyphs);
                }
                if (!valid)
                    break;
                prepared_texts.push_back({layout->text.get(), glyphs});
                const auto prepared_id = nkui::make_resource_id(
                    nkui::ResourceKind::TextLayout, 0x0FFD, static_cast<uint16_t>(prepared_slot++));
                valid = frame_resources.bind_text(prepared_id, *glyphs,
                                                  (static_cast<uint64_t>(source_resource) << 32) ^
                                                      layout->text->layout_generation() ^
                                                      layout->text->font_collection_generation());
                if (valid) {
                    auto snapshot =
                        layout->text->published_glyphs(layout->text->active_layout_id(), 0.0f, 0.0f,
                                                       raster_scale, nkui::GlyphMode::Alpha);
                    if (!snapshot ||
                        !owned_resources.bind_text(prepared_id, std::move(snapshot),
                                                   (static_cast<uint64_t>(source_resource) << 32) ^
                                                       layout->text->layout_generation() ^
                                                       layout->text->font_collection_generation()))
                        sealable = false;
                }
                command.resource = prepared_id;
                retain_text_adapter(layout->text, text_adapters, text_adapter_owners);
            } else if (command.kind == nkui::RenderCommandKind::CompositeTarget) {
                if (!nkui::is_resource_id(command.resource, nkui::ResourceKind::RenderTarget)) {
                    valid = false;
                    break;
                }
                const uint16_t target_slot = static_cast<uint16_t>(command.resource.value);
                if (target_slot < 0x8000u && command.resource.value != compile_target.value) {
                    auto *surface_slot = resolve_retained(nkui_resource{command.resource.value},
                                                          nkui::ResourceKind::RenderTarget);
                    if (!surface_slot) {
                        valid = false;
                        break;
                    }
                    if (surface_slot->graphics_image.id) {
                        valid = frame_resources.bind_graphics_image(command.resource,
                                                                    surface_slot->graphics_image);
                        if (valid && !owned_resources.bind_graphics_image(
                                         command.resource, surface_slot->graphics_image))
                            sealable = false;
                    } else if (surface_slot->surface) {
                        const nk_graphics_image published =
                            surface_slot->surface->retained_image();
                        if (published.id) {
                            const auto generation = static_cast<uint64_t>(
                                surface_slot->surface->generation());
                            valid = frame_resources.bind_graphics_image(command.resource, published,
                                                                        generation);
                            if (valid && !owned_resources.bind_graphics_image(
                                             command.resource, published, generation))
                                sealable = false;
                        } else {
                            valid = frame_resources.bind_surface(command.resource,
                                                                 *surface_slot->surface);
                            /* A live result producer is a callback and cannot be sealed. */
                            sealable = false;
                        }
                    }
                }
            }
            if (!valid)
                break;
        }
        if (!valid)
            break;
    }
    if (!valid)
        return NKUI_ERROR_INVALID_HANDLE;
    for (auto &[adapter, glyphs] : prepared_texts)
        if (!adapter->prepared_glyphs_current(*glyphs) &&
            !adapter->prepare_glyphs(glyphs->origin_x, glyphs->origin_y, glyphs->pixel_scale,
                                     glyphs->mode, *glyphs))
            return NKUI_ERROR_RENDERING;
    auto *session_text_adapter = session_state->frame.text_adapter();
    if (!sealable && threaded)
        return NKUI_ERROR_RENDERING;
    if (!threaded) {
        const bool new_backend = !renderer_slot->renderer->valid();
        if (new_backend && !renderer_slot->renderer->initialize())
            return NKUI_ERROR_RENDERING;
        if (new_backend && !register_custom_effects(*renderer_slot))
            return NKUI_ERROR_RENDERING;
        if (session_text_adapter)
            if (!renderer_slot->renderer->uploadAtlases(*session_text_adapter, new_backend))
                return NKUI_ERROR_RENDERING;
        for (auto *adapter : text_adapters)
            if (!renderer_slot->renderer->uploadAtlases(*adapter, new_backend))
                return NKUI_ERROR_RENDERING;
    }
    if (sealable) {
        nkui::RenderPlanSealError seal_error;
        auto sealed = nkui::SealedRenderPlan::seal(std::move(session_state->frame.plan()),
                                                   std::move(owned_resources), &seal_error);
        if (!sealed)
            return NKUI_ERROR_OUT_OF_MEMORY;
        if (threaded) {
            if (session_text_adapter)
                text_adapters.push_back(session_text_adapter);
            auto *submission = new RenderSubmission{renderer,
                                                    surface,
                                                    frame,
                                                    frame_target,
                                                    std::move(sealed),
                                                    std::move(text_adapters),
                                                    std::move(text_adapter_owners),
                                                    session_state->shared_from_this()};
            renderer_lock.unlock();
            lists_lock.unlock();
            resources_lock.unlock();
            sessions_lock.unlock();
            cpu_lock.unlock();
            if (!enqueue_render_submission(submission))
                return NKUI_ERROR_RENDERING;
            frame_guard.handed_off = true;
            return NKUI_OK;
        }
        const bool sealed_executed = nkui::execute_render_plan(*renderer_slot->renderer, *sealed,
                                                               {main_target, frame_target});
        return sealed_executed ? NKUI_OK : NKUI_ERROR_RENDERING;
    }
    /* Frames that composite a live surface producer keep the borrowed path. */
    const bool executed = nkui::execute_render_plan(*renderer_slot->renderer, plan, frame_resources,
                                                    {main_target, frame_target});
    return executed ? NKUI_OK : NKUI_ERROR_RENDERING;
}

extern "C" nkui_result nkui_renderer_render(nkui_renderer renderer, nkui_display_list list,
                                            nk_surface surface) {
    int32_t width = 0;
    int32_t height = 0;
    if (!surface || nk_surface_make_current(surface) != NK_OK ||
        nk_surface_get_framebuffer_size(surface, &width, &height) != NK_OK)
        return NKUI_ERROR_INVALID_ARGUMENT;
    const nkui_frame_info frame_info{sizeof(nkui_frame_info),
                                     static_cast<float>(width),
                                     static_cast<float>(height),
                                     width,
                                     height,
                                     1.0f};
    return nkui_renderer_render_frame(renderer, list, surface, &frame_info);
}
