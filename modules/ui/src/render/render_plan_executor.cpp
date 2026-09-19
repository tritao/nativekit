#include "render_plan_executor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace nkui {
namespace {

constexpr uint64_t kRuntimeHashOffset = UINT64_C(1469598103934665603);
constexpr uint64_t kRuntimeHashPrime = UINT64_C(1099511628211);

void hash_runtime_u32(uint64_t &hash, uint32_t value) {
    for (uint32_t shift = 0; shift < 32; shift += 8) {
        hash ^= static_cast<uint8_t>(value >> shift);
        hash *= kRuntimeHashPrime;
    }
}

void hash_runtime_u64(uint64_t &hash, uint64_t value) {
    hash_runtime_u32(hash, static_cast<uint32_t>(value));
    hash_runtime_u32(hash, static_cast<uint32_t>(value >> 32));
}

void hash_runtime_float(uint64_t &hash, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    hash_runtime_u32(hash, bits);
}

void hash_runtime_command_geometry(uint64_t &hash, const RenderCommand &command) {
    for (const float value : {command.x, command.y, command.width, command.height,
                              command.opacity, command.scissor_x, command.scissor_y,
                              command.scissor_width, command.scissor_height, command.stroke_width,
                              command.miter_limit})
        hash_runtime_float(hash, value);
    for (const float value : command.transform)
        hash_runtime_float(hash, value);
    hash_runtime_u32(hash, static_cast<uint32_t>(command.composite));
    hash_runtime_u32(hash, command.has_scissor ? 1u : 0u);
    hash_runtime_u32(hash, command.line_cap);
    hash_runtime_u32(hash, command.line_join);
    hash_runtime_u32(hash, command.custom_payload ? 1u : 0u);
    for (const float value : {command.box_shadow.offset_x, command.box_shadow.offset_y,
                              command.box_shadow.blur_sigma, command.box_shadow.spread})
        hash_runtime_float(hash, value);
    for (const float value : command.box_shadow.radii)
        hash_runtime_float(hash, value);
    for (const float value : command.box_shadow.color)
        hash_runtime_float(hash, value);
}

void hash_runtime_resource(uint64_t &hash, const FrameResources &resources, ResourceId resource) {
    hash_runtime_u32(hash, resource.value);
    hash_runtime_u64(hash, resources.content_generation(resource));
    if (const auto *surface = resources.surface(resource)) {
        hash_runtime_u32(hash, 1u);
        hash_runtime_u32(hash, surface->generation());
    } else if (const auto *image = resources.graphics_image(resource)) {
        hash_runtime_u32(hash, 2u);
        hash_runtime_u32(hash, image->id);
    } else {
        hash_runtime_u32(hash, 0u);
    }
}

void hash_runtime_target(uint64_t &hash, const FrameResources &resources,
                         const std::unordered_map<uint32_t, uint64_t> &target_hashes,
                         ResourceId target) {
    const auto found = target_hashes.find(target.value);
    if (found != target_hashes.end()) {
        hash_runtime_u32(hash, 1u);
        hash_runtime_u64(hash, found->second);
        return;
    }
    hash_runtime_u32(hash, 2u);
    hash_runtime_resource(hash, resources, target);
}

void hash_runtime_command_source(uint64_t &hash, const FrameResources &resources,
                                 const std::unordered_map<uint32_t, uint64_t> &target_hashes,
                                 const RenderCommand &command) {
    hash_runtime_u32(hash, static_cast<uint32_t>(command.kind));
    if (command.kind == RenderCommandKind::CompositeTarget) {
        hash_runtime_target(hash, resources, target_hashes, command.resource);
        return;
    }
    hash_runtime_resource(hash, resources, command.resource);
}

uint64_t runtime_pass_hash(const RenderPass &pass, const FrameResources &resources,
                           const std::unordered_map<uint32_t, uint64_t> &target_hashes,
                           uint64_t execution_serial) {
    uint64_t hash = pass.cache_key ? pass.cache_key : kRuntimeHashOffset;
    hash_runtime_u32(hash, static_cast<uint32_t>(pass.kind));
    if (pass.kind == RenderPassKind::Draw) {
        const auto previous = target_hashes.find(pass.target.value);
        if (previous != target_hashes.end())
            hash_runtime_u64(hash, previous->second);
        if (pass.load_existing)
            hash_runtime_u64(hash, execution_serial);
        for (const auto &command : pass.commands) {
            hash_runtime_u64(hash, command.content_generation);
            hash_runtime_command_geometry(hash, command);
            hash_runtime_command_source(hash, resources, target_hashes, command);
        }
    } else if (pass.kind == RenderPassKind::Effect || pass.kind == RenderPassKind::Mask) {
        hash_runtime_target(hash, resources, target_hashes, pass.input_target);
        if (pass.kind == RenderPassKind::Mask && pass.mask.kind == MaskKind::Image)
            hash_runtime_resource(hash, resources, pass.mask.image);
    } else {
        const auto previous = target_hashes.find(pass.target.value);
        if (previous != target_hashes.end())
            hash_runtime_u64(hash, previous->second);
        if (pass.load_existing)
            hash_runtime_u64(hash, execution_serial);
        for (const auto &command : pass.commands) {
            hash_runtime_u64(hash, command.content_generation);
            hash_runtime_command_geometry(hash, command);
            hash_runtime_command_source(hash, resources, target_hashes, command);
        }
    }
    return hash ? hash : 1;
}

uint64_t frame_effect_cache_key(uint64_t key, const nk_surface_frame_target &window) {
    if (!key)
        return 0;
    hash_runtime_u32(key, static_cast<uint32_t>(window.width));
    hash_runtime_u32(key, static_cast<uint32_t>(window.height));
    return key ? key : 1;
}

bool fail(RenderExecutionError *error, uint32_t pass, uint32_t command, const char *message) {
    if (error)
        *error = {pass, command, message};
    return false;
}

std::pair<int, int> surface_request_size(const RenderPlan &plan, ResourceId surface,
                                         const nk_surface_frame_target &window) {
    float requested_width = 0.0f;
    float requested_height = 0.0f;
    for (const auto &pass : plan.passes) {
        for (const auto &command : pass.commands) {
            if (command.kind != RenderCommandKind::CompositeTarget ||
                command.resource.value != surface.value || command.width <= 0.0f ||
                command.height <= 0.0f)
                continue;
            const auto &m = command.transform;
            const float width = std::abs(m[0]) * command.width + std::abs(m[2]) * command.height;
            const float height = std::abs(m[1]) * command.width + std::abs(m[3]) * command.height;
            if (std::isfinite(width) && std::isfinite(height)) {
                requested_width = std::max(requested_width, width);
                requested_height = std::max(requested_height, height);
            }
        }
    }
    if (!(requested_width > 0.0f) || !(requested_height > 0.0f))
        return {window.width, window.height};
    const auto bounded_extent = [](float value, int maximum) {
        const double rounded = std::ceil(static_cast<double>(value));
        return static_cast<int>(std::clamp(rounded, 1.0, static_cast<double>(maximum)));
    };
    return {bounded_extent(requested_width, window.width),
            bounded_extent(requested_height, window.height)};
}

} // namespace

bool execute_render_plan(UiRenderer &renderer, const RenderPlan &plan,
                         const FrameResources &resources, const WindowTarget &window,
                         RenderExecutionError *error) {
    if (!renderer.valid() || !is_resource_id(window.id, ResourceKind::RenderTarget) ||
        window.frame_target.struct_size < sizeof(window.frame_target) ||
        window.frame_target.width <= 0 || window.frame_target.height <= 0)
        return fail(error, 0, 0, "invalid render-plan execution input");
    std::vector<uint32_t> pass_order;
    RenderPlanScheduleError schedule_error{};
    if (!schedule_render_plan(plan, pass_order, &schedule_error))
        return fail(error, schedule_error.pass_index, 0, schedule_error.message);
    /*
     * A live surface producer renders through callbacks, which a sealed
     * submission batch cannot carry, so those frames are drawn inline.
     */
    bool record = true;
    for (const auto &dependency : plan.dependencies) {
        if (resources.surface(dependency.producer)) {
            record = false;
            break;
        }
    }
    if (!renderer.beginFrame(record, &window.frame_target))
        return fail(error, 0, 0, renderer.lastError());
    struct FrameGuard {
        UiRenderer &renderer;
        bool complete = false;
        ~FrameGuard() {
            if (!complete)
                renderer.endFrame();
        }
    } frame_guard{renderer};
    std::unordered_set<uint32_t> internal_targets;
    for (const auto &pass : plan.passes)
        internal_targets.insert(pass.target.value);
    std::unordered_set<uint32_t> rendered_producers;
    for (const auto &dependency : plan.dependencies) {
        if (internal_targets.count(dependency.producer.value) ||
            rendered_producers.count(dependency.producer.value))
            continue;
        if (resources.graphics_image(dependency.producer)) {
            rendered_producers.insert(dependency.producer.value);
            continue;
        }
        SurfaceProducer *producer = resources.surface(dependency.producer);
        if (!producer)
            return fail(error, 0, 0, "surface producer is unavailable");
        if (!producer->ready()) {
            if (!renderer.surfaceHasContent(dependency.producer))
                return fail(error, 0, 0, "surface producer is unavailable");
            rendered_producers.insert(dependency.producer.value);
            continue;
        }
        SurfaceDescriptor description{};
        const auto requested = surface_request_size(plan, dependency.producer, window.frame_target);
        if (!producer->describe(requested.first, requested.second, description) ||
            description.width <= 0 || description.height <= 0)
            return fail(error, 0, 0, "surface producer description is invalid");
        if (description.format != SurfacePixelFormat::Rgba8 ||
            (description.alpha != SurfaceAlphaMode::Opaque &&
             description.alpha != SurfaceAlphaMode::Premultiplied) ||
            (description.filter != SurfaceFilter::Nearest &&
             description.filter != SurfaceFilter::Linear) ||
            description.color_space != SurfaceColorSpace::Linear)
            return fail(error, 0, 0, "surface producer descriptor is unsupported");
        const uint32_t generation = producer->generation();
        if (renderer.surfaceIsCurrent(dependency.producer, generation, description)) {
            rendered_producers.insert(dependency.producer.value);
            continue;
        }
        const SurfaceRenderResult render_result =
            producer->render(renderer, dependency.producer, description);
        if (render_result == SurfaceRenderResult::Unavailable) {
            if (!renderer.surfaceHasContent(dependency.producer))
                return fail(error, 0, 0, "surface producer has no fallback content");
            rendered_producers.insert(dependency.producer.value);
            continue;
        }
        if (render_result != SurfaceRenderResult::Rendered)
            return fail(error, 0, 0, "surface producer render failed");
        renderer.markSurfaceCurrent(dependency.producer, generation, description);
        rendered_producers.insert(dependency.producer.value);
    }
    static std::atomic<uint64_t> next_execution_serial{1};
    const uint64_t execution_serial = next_execution_serial.fetch_add(1, std::memory_order_relaxed);
    std::unordered_map<uint32_t, uint64_t> target_runtime_hashes;
    for (uint32_t scheduled_index = 0; scheduled_index < pass_order.size(); ++scheduled_index) {
        const uint32_t pass_index = pass_order[scheduled_index];
        const auto &pass = plan.passes[pass_index];
        int pass_width = pass.target_descriptor.width;
        int pass_height = pass.target_descriptor.height;
        if (pass_width <= 0)
            pass_width = pass.target_descriptor.logical_width > 0.0f
                             ? static_cast<int>(std::ceil(pass.target_descriptor.logical_width))
                             : window.frame_target.width;
        if (pass_height <= 0)
            pass_height = pass.target_descriptor.logical_height > 0.0f
                              ? static_cast<int>(std::ceil(pass.target_descriptor.logical_height))
                              : window.frame_target.height;
        const bool window_pass = pass.target.value == window.id.value;
        const uint64_t runtime_hash =
            runtime_pass_hash(pass, resources, target_runtime_hashes, execution_serial);
        target_runtime_hashes[pass.target.value] = runtime_hash;
        bool effect_cache_hit = false;
        const bool began =
            pass.kind == RenderPassKind::Effect
                ? renderer.beginEffectPass(
                      pass.target, frame_effect_cache_key(runtime_hash, window.frame_target),
                      pass_width, pass_height, effect_cache_hit)
                : pass.kind == RenderPassKind::Raster
                       ? renderer.beginRasterPass(
                             pass.target, frame_effect_cache_key(runtime_hash, window.frame_target),
                             pass_width, pass_height, effect_cache_hit)
                : (window_pass
                       ? renderer.beginWindowPass(pass_width, pass_height, !pass.load_existing)
                       : renderer.beginTargetPass(pass.target, pass_width, pass_height,
                                                  pass.load_existing));
        if (!began)
            return fail(error, pass_index, 0, renderer.lastError());
        if (pass.kind == RenderPassKind::Effect) {
            if (effect_cache_hit)
                continue;
            const bool applied =
                pass.effect.kind == EffectKind::Custom
                    ? (pass.has_input_rect
                           ? renderer.applyCustomEffectRegion(
                                 pass.input_target, pass.custom_effect, pass.input_rect[0],
                                 pass.input_rect[1], pass.input_rect[2], pass.input_rect[3])
                           : renderer.applyCustomEffect(pass.input_target, pass.custom_effect))
                    : (pass.has_input_rect
                           ? renderer.applyEffectRegion(pass.input_target, pass.effect,
                                                        pass.input_rect[0], pass.input_rect[1],
                                                        pass.input_rect[2], pass.input_rect[3])
                           : renderer.applyEffect(pass.input_target, pass.effect));
            if (!applied) {
                renderer.endPass();
                return fail(error, pass_index, 0, renderer.lastError());
            }
            if (!renderer.endPass())
                return fail(error, pass_index, 0, renderer.lastError());
            continue;
        }
        if (pass.kind == RenderPassKind::Raster && effect_cache_hit)
            continue;
        if (pass.kind == RenderPassKind::Mask) {
            const PreparedTexture *image = nullptr;
            if (pass.mask.kind == MaskKind::Image) {
                const auto *bound = resources.image(pass.mask.image);
                if (!bound || !bound->image) {
                    renderer.endPass();
                    return fail(error, pass_index, 0, "mask image is unavailable");
                }
                image = bound->image;
            }
            if (!renderer.applyMask(pass.input_target, pass.mask, image)) {
                renderer.endPass();
                return fail(error, pass_index, 0, renderer.lastError());
            }
            if (!renderer.endPass())
                return fail(error, pass_index, 0, renderer.lastError());
            continue;
        }
        const auto fail_command = [&](uint32_t command, const char *message) {
            renderer.endPass();
            return fail(error, pass_index, command, message);
        };
        for (uint32_t command_index = 0; command_index < pass.commands.size(); ++command_index) {
            const auto &command = pass.commands[command_index];
            if (!renderer.setScissor(command.has_scissor, command.scissor_x, command.scissor_y,
                                     command.scissor_width, command.scissor_height))
                return fail(error, pass_index, command_index, renderer.lastError());
            bool rendered = false;
            switch (command.kind) {
            case RenderCommandKind::Path:
            case RenderCommandKind::StrokePath: {
                const auto *path = resources.path(command.resource);
                rendered = path && renderer.drawPath(*path->path, path->operation_index,
                                                     command.transform.data(), command.opacity);
                break;
            }
            case RenderCommandKind::GlyphBatch: {
                const auto *text = resources.text(command.resource);
                rendered = text && renderer.drawGlyphs(*text, command.transform.data(), command.x,
                                                       command.y, command.opacity);
                break;
            }
            case RenderCommandKind::CompositeTarget:
                if (const auto *image = resources.graphics_image(command.resource))
                    rendered = renderer.compositeImage(*image, command.x, command.y, command.width,
                                                       command.height, command.transform.data(),
                                                       command.opacity);
                else
                    rendered = renderer.compositeImage(command.resource, command.x, command.y,
                                                       command.width, command.height,
                                                       command.transform.data(), command.opacity);
                break;
            case RenderCommandKind::Image: {
                const auto *image = resources.image(command.resource);
                rendered = image && renderer.drawImage(*image->image, command.x, command.y,
                                                       command.width, command.height,
                                                       command.transform.data(), command.opacity);
            } break;
            case RenderCommandKind::BoxShadow:
                rendered = renderer.drawBoxShadow(command.x, command.y, command.width,
                                                  command.height, command.transform.data(),
                                                  command.opacity, command.box_shadow);
                break;
            }
            if (!rendered)
                return fail_command(command_index, renderer.lastError());
        }
        if (!renderer.endPass())
            return fail(error, pass_index, static_cast<uint32_t>(pass.commands.size()),
                        renderer.lastError());
    }
    if (!renderer.endFrame())
        return fail(error, static_cast<uint32_t>(plan.passes.size()), 0, renderer.lastError());
    frame_guard.complete = true;
    if (error)
        *error = {};
    return true;
}

bool execute_render_plan(UiRenderer &renderer, const SealedRenderPlan &sealed,
                         const WindowTarget &window, RenderExecutionError *error) {
    return execute_render_plan(renderer, sealed.plan(), sealed.resources(), window, error);
}

} // namespace nkui
