#include "render_plan_executor.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>
#include <utility>

namespace nkui {
namespace {

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
    if (!renderer.beginFrame())
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
        if (!(window_pass ? renderer.beginWindowPass(pass_width, pass_height, !pass.load_existing)
                          : renderer.beginTargetPass(pass.target, pass_width, pass_height,
                                                     pass.load_existing)))
            return fail(error, pass_index, 0, renderer.lastError());
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

} // namespace nkui
