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
    return 0;
}
