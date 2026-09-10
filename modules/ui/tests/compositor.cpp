#include "compositor/compositor.h"

using namespace nkui;

int main() {
    const auto main_target = make_resource_id(ResourceKind::RenderTarget, 1, 1);
    const auto path = make_resource_id(ResourceKind::Path, 1, 1);
    const auto text = make_resource_id(ResourceKind::TextLayout, 1, 1);
    DisplayList list;
    if (!list.draw_path(path) || !list.begin_layer(0.5f) ||
        !list.draw_text_layout(text, 10.0f, 20.0f) || !list.end_layer() || !list.draw_path(path))
        return 1;
    Compositor compositor;
    RenderPlan plan;
    CompositorError error{};
    if (!compositor.compile(list, main_target, plan, &error))
        return 2;
    if (plan.passes.size() != 3 || plan.dependencies.size() != 1)
        return 3;
    const ResourceId first_transient = plan.passes[1].target;
    if (plan.passes[0].target.value != main_target.value ||
        plan.passes[0].commands[0].kind != RenderCommandKind::Path ||
        plan.passes[1].commands[0].kind != RenderCommandKind::GlyphBatch ||
        plan.passes[2].commands[0].kind != RenderCommandKind::CompositeTarget ||
        plan.passes[2].commands[1].kind != RenderCommandKind::Path || !plan.passes[2].load_existing)
        return 4;

    DisplayList direct;
    if (!direct.begin_layer(1.0f) || !direct.draw_path(path) || !direct.end_layer() ||
        !compositor.compile(direct, main_target, plan, &error) || plan.passes.size() != 1 ||
        plan.passes[0].commands.size() != 1)
        return 5;

    DisplayList cycle;
    if (!cycle.draw_render_target(main_target, 0.0f, 0.0f, 10.0f, 10.0f) ||
        compositor.compile(cycle, main_target, plan, &error) || error.command_index != 0)
        return 6;

    if (!compositor.compile(list, main_target, plan, &error) ||
        plan.passes[1].target.value != first_transient.value)
        return 7;

    DisplayList stateful;
    const auto paint = make_resource_id(ResourceKind::Paint, 1, 1);
    const float transform[6] = {2.0f, 0.0f, 0.0f, 3.0f, 5.0f, 7.0f};
    if (!stateful.set_transform(transform) || !stateful.set_paint(paint) ||
        !stateful.clip_rect(1.0f, 2.0f, 10.0f, 20.0f) || !stateful.set_global_alpha(0.5f) ||
        !stateful.push_state() || !stateful.clip_rect(3.0f, 4.0f, 2.0f, 3.0f) ||
        !stateful.set_global_alpha(0.25f) || !stateful.draw_path(path) || !stateful.pop_state() ||
        !stateful.draw_path(path) || !compositor.compile(stateful, main_target, plan, &error))
        return 8;
    const auto &inner = plan.passes[0].commands[0];
    const auto &outer = plan.passes[0].commands[1];
    if (!inner.has_scissor || inner.scissor_x != 11.0f || inner.scissor_y != 19.0f ||
        inner.scissor_width != 4.0f || inner.scissor_height != 9.0f || inner.opacity != 0.25f ||
        !outer.has_scissor || outer.scissor_x != 7.0f || outer.scissor_y != 13.0f ||
        outer.scissor_width != 20.0f || outer.scissor_height != 60.0f || outer.opacity != 0.5f ||
        outer.paint.value != paint.value || outer.transform[0] != 2.0f ||
        outer.transform[3] != 3.0f || outer.transform[4] != 5.0f || outer.transform[5] != 7.0f)
        return 9;
    return 0;
}
