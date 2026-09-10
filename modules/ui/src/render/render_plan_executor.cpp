#include "render_plan_executor.h"

namespace nkui {
namespace {

bool fail(RenderExecutionError *error, uint32_t pass, uint32_t command, const char *message) {
    if (error)
        *error = {pass, command, message};
    return false;
}

} // namespace

bool execute_render_plan(SokolBackend &backend, const RenderPlan &plan,
                         const FrameResources &resources, const WindowTarget &window,
                         RenderExecutionError *error) {
    if (!backend.valid() || !is_resource_id(window.id, ResourceKind::RenderTarget) ||
        window.width <= 0 || window.height <= 0)
        return fail(error, 0, 0, "invalid render-plan execution input");
    for (uint32_t pass_index = 0; pass_index < plan.passes.size(); ++pass_index) {
        const auto &pass = plan.passes[pass_index];
        const bool window_pass = pass.target.value == window.id.value;
        if (!(window_pass ? backend.begin_window_pass(window.width, window.height,
                                                      window.framebuffer, !pass.load_existing)
                          : backend.begin_target_pass(pass.target, window.width, window.height,
                                                      pass.load_existing)))
            return fail(error, pass_index, 0, backend.last_error());
        const auto fail_command = [&](uint32_t command, const char *message) {
            backend.end_pass();
            return fail(error, pass_index, command, message);
        };
        for (uint32_t command_index = 0; command_index < pass.commands.size(); ++command_index) {
            const auto &command = pass.commands[command_index];
            if (!backend.set_scissor(command.has_scissor, command.scissor_x, command.scissor_y,
                                     command.scissor_width, command.scissor_height))
                return fail(error, pass_index, command_index, backend.last_error());
            bool rendered = false;
            switch (command.kind) {
            case RenderCommandKind::Path: {
                const auto *path = resources.path(command.resource);
                rendered = path && backend.draw_path(*path->recorder, path->operation_index,
                                                     command.opacity);
                break;
            }
            case RenderCommandKind::GlyphBatch: {
                const auto *text = resources.text(command.resource);
                rendered = text && backend.draw_glyphs(*text, command.opacity);
                break;
            }
            case RenderCommandKind::CompositeTarget:
                rendered = backend.draw_target(command.resource, command.x, command.y,
                                               command.width, command.height, command.opacity);
                break;
            case RenderCommandKind::Image:
                return fail_command(command_index, "image resource is not prepared");
            }
            if (!rendered)
                return fail_command(command_index, backend.last_error());
        }
        if (!backend.end_pass())
            return fail(error, pass_index, static_cast<uint32_t>(pass.commands.size()),
                        backend.last_error());
    }
    if (!backend.commit_frame())
        return fail(error, static_cast<uint32_t>(plan.passes.size()), 0, backend.last_error());
    if (error)
        *error = {};
    return true;
}

} // namespace nkui
