#include "compositor/compositor.h"

#include <limits>
#include <string>
#include <vector>

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
    std::vector<uint32_t> pass_order;
    RenderPlanScheduleError schedule_error{};
    if (!schedule_render_plan(plan, pass_order, &schedule_error) || pass_order.size() != 3 ||
        pass_order[0] != 0 || pass_order[1] != 1 || pass_order[2] != 2)
        return 16;
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

    DisplayList nested;
    if (!nested.begin_layer(0.8f) || !nested.draw_path(path) || !nested.begin_layer(0.4f) ||
        !nested.draw_path(path) || !nested.end_layer() || !nested.draw_path(path) ||
        !nested.end_layer() || !nested.draw_path(path) || !compositor.compile(nested, main_target,
                                                                                 plan, &error))
        return 8;
    if (plan.passes.size() != 5 || plan.dependencies.size() != 2 ||
        plan.passes[0].target.value != main_target.value ||
        plan.passes[1].target.value == plan.passes[2].target.value ||
        plan.passes[1].commands.size() != 1 ||
        plan.passes[2].commands.size() != 1 ||
        plan.passes[3].commands.size() != 2 || plan.passes[4].commands.size() != 2 ||
        !plan.passes[3].load_existing || !plan.passes[4].load_existing)
        return 9;

    DisplayList bounded;
    const LayerBounds bounds{10.0f, 20.0f, 80.0f, 40.0f};
    if (!bounded.begin_layer(1.0f, bounds) || !bounded.draw_path(path) ||
        !bounded.end_layer() || !compositor.compile(bounded, main_target, plan, &error) ||
        plan.passes.size() != 3 || plan.dependencies.size() != 1 ||
        plan.passes[1].target_descriptor.logical_width != bounds.width ||
        plan.passes[1].target_descriptor.logical_height != bounds.height ||
        plan.passes[1].target_descriptor.origin_x != bounds.x ||
        plan.passes[1].target_descriptor.origin_y != bounds.y ||
        plan.passes[1].commands.size() != 1 ||
        plan.passes[1].commands[0].transform[4] != -bounds.x ||
        plan.passes[1].commands[0].transform[5] != -bounds.y)
        return 18;
    const auto &bounded_composite = plan.passes[2].commands[0];
    if (bounded_composite.x != bounds.x || bounded_composite.y != bounds.y ||
        bounded_composite.width != bounds.width || bounded_composite.height != bounds.height ||
        bounded_composite.opacity != 1.0f ||
        bounded_composite.composite != CompositeMode::SourceOver)
        return 19;

    DisplayList color_effect;
    EffectDescriptor effect{};
    effect.kind = EffectKind::ColorMatrix;
    effect.color_matrix[0] = 1.0f;
    effect.color_matrix[6] = 1.0f;
    effect.color_matrix[12] = 1.0f;
    effect.color_matrix[18] = 1.0f;
    effect.color_matrix[4] = 0.25f;
    if (!color_effect.begin_layer(1.0f, bounds, effect) || !color_effect.draw_path(path) ||
        !color_effect.end_layer() || !compositor.compile(color_effect, main_target, plan, &error) ||
        plan.passes.size() != 4 || plan.dependencies.size() != 1)
        return 20;
    if (plan.passes[2].kind != RenderPassKind::Effect ||
        plan.passes[2].input_target.value != plan.passes[1].target.value ||
        plan.passes[2].effect.kind != EffectKind::ColorMatrix ||
        plan.passes[2].effect.color_matrix[4] != 0.25f ||
        plan.passes[3].commands.size() != 1 ||
        plan.passes[3].commands[0].resource.value != plan.passes[2].target.value)
        return 21;
    if (!schedule_render_plan(plan, pass_order, &schedule_error) || pass_order.size() != 4 ||
        pass_order[0] != 0 || pass_order[1] != 1 || pass_order[2] != 2 || pass_order[3] != 3)
        return 22;

    DisplayList blur_effect;
    EffectDescriptor blur{};
    blur.kind = EffectKind::Blur;
    blur.color_matrix[0] = 2.0f;
    if (!blur_effect.begin_layer(1.0f, bounds, blur) || !blur_effect.draw_path(path) ||
        !blur_effect.end_layer() || !compositor.compile(blur_effect, main_target, plan, &error) ||
        plan.passes.size() != 5 || plan.dependencies.size() != 1)
        return 23;
    const auto &blur_layer = plan.passes[1];
    if (blur_layer.target_descriptor.logical_width != 92.0f ||
        blur_layer.target_descriptor.logical_height != 52.0f ||
        blur_layer.target_descriptor.origin_x != 4.0f ||
        blur_layer.target_descriptor.origin_y != 14.0f ||
        plan.passes[2].kind != RenderPassKind::Effect ||
        plan.passes[2].input_target.value != blur_layer.target.value ||
        plan.passes[2].effect.kind != EffectKind::Blur ||
        plan.passes[2].effect.color_matrix[0] != 2.0f ||
        plan.passes[2].effect.color_matrix[1] != 0.0f ||
        plan.passes[3].kind != RenderPassKind::Effect ||
        plan.passes[3].input_target.value != plan.passes[2].target.value ||
        plan.passes[3].effect.color_matrix[1] != 1.0f ||
        plan.passes[4].commands.size() != 1 ||
        plan.passes[4].commands[0].resource.value != plan.passes[3].target.value ||
        plan.passes[4].commands[0].x != 4.0f || plan.passes[4].commands[0].y != 14.0f ||
        plan.passes[4].commands[0].width != 92.0f ||
        plan.passes[4].commands[0].height != 52.0f)
        return 24;
    if (!schedule_render_plan(plan, pass_order, &schedule_error) || pass_order.size() != 5 ||
        pass_order[0] != 0 || pass_order[1] != 1 || pass_order[2] != 2 || pass_order[3] != 3 ||
        pass_order[4] != 4)
        return 25;

    DisplayList drop_shadow_effect;
    EffectDescriptor drop_shadow{};
    drop_shadow.kind = EffectKind::DropShadow;
    drop_shadow.color_matrix[0] = 2.0f;
    drop_shadow.color_matrix[2] = 0.0f;
    drop_shadow.color_matrix[3] = 6.0f;
    drop_shadow.color_matrix[7] = 0.35f;
    if (!drop_shadow_effect.begin_layer(1.0f, bounds, drop_shadow) ||
        !drop_shadow_effect.draw_path(path) || !drop_shadow_effect.end_layer() ||
        !compositor.compile(drop_shadow_effect, main_target, plan, &error) ||
        plan.passes.size() != 5 || plan.dependencies.size() != 2)
        return 26;
    const auto &drop_shadow_layer = plan.passes[1];
    if (drop_shadow_layer.target_descriptor.logical_width != 92.0f ||
        drop_shadow_layer.target_descriptor.logical_height != 52.0f ||
        drop_shadow_layer.target_descriptor.origin_x != 4.0f ||
        drop_shadow_layer.target_descriptor.origin_y != 20.0f ||
        plan.passes[2].kind != RenderPassKind::Effect ||
        plan.passes[2].input_target.value != drop_shadow_layer.target.value ||
        plan.passes[2].effect.kind != EffectKind::DropShadow ||
        plan.passes[2].effect.color_matrix[0] != 2.0f ||
        plan.passes[2].effect.color_matrix[1] != 0.0f ||
        plan.passes[2].effect.color_matrix[3] != 6.0f ||
        plan.passes[2].effect.color_matrix[7] != 0.35f ||
        plan.passes[3].kind != RenderPassKind::Effect ||
        plan.passes[3].input_target.value != plan.passes[2].target.value ||
        plan.passes[3].effect.color_matrix[1] != 1.0f ||
        plan.passes[3].effect.color_matrix[3] != 6.0f ||
        plan.passes[4].commands.size() != 2 ||
        plan.passes[4].commands[0].resource.value != plan.passes[3].target.value ||
        plan.passes[4].commands[1].resource.value != drop_shadow_layer.target.value ||
        plan.passes[4].commands[0].x != 4.0f || plan.passes[4].commands[0].y != 20.0f ||
        plan.passes[4].commands[0].width != 92.0f ||
        plan.passes[4].commands[0].height != 52.0f)
        return 27;
    if (!schedule_render_plan(plan, pass_order, &schedule_error) || pass_order.size() != 5 ||
        pass_order[0] != 0 || pass_order[1] != 1 || pass_order[2] != 2 || pass_order[3] != 3 ||
        pass_order[4] != 4)
        return 28;

    DisplayList masked;
    MaskDescriptor rounded_mask{};
    rounded_mask.kind = MaskKind::RoundedRect;
    rounded_mask.values[0] = 8.0f;
    if (!masked.begin_layer(1.0f, bounds, rounded_mask) || !masked.draw_path(path) ||
        !masked.end_layer() || !compositor.compile(masked, main_target, plan, &error) ||
        plan.passes.size() != 4 || plan.dependencies.size() != 1)
        return 29;
    if (plan.passes[2].kind != RenderPassKind::Mask ||
        plan.passes[2].input_target.value != plan.passes[1].target.value ||
        plan.passes[2].mask.kind != MaskKind::RoundedRect ||
        plan.passes[2].mask.values[0] != 8.0f || plan.passes[3].commands.size() != 1 ||
        plan.passes[3].commands[0].resource.value != plan.passes[2].target.value)
        return 30;
    if (!schedule_render_plan(plan, pass_order, &schedule_error) || pass_order.size() != 4 ||
        pass_order[0] != 0 || pass_order[1] != 1 || pass_order[2] != 2 || pass_order[3] != 3)
        return 31;

    DisplayList duplicate_surface;
    const auto external = make_resource_id(ResourceKind::RenderTarget, 1, 12);
    if (!duplicate_surface.draw_render_target(external, 0.0f, 0.0f, 10.0f, 10.0f) ||
        !duplicate_surface.draw_render_target(external, 20.0f, 0.0f, 10.0f, 10.0f) ||
        !compositor.compile(duplicate_surface, main_target, plan, &error) ||
        plan.dependencies.size() != 1)
        return 10;

    DisplayList stateful;
    const auto paint = make_resource_id(ResourceKind::Paint, 1, 1);
    const float transform[6] = {2.0f, 0.0f, 0.0f, 3.0f, 5.0f, 7.0f};
    if (!stateful.set_transform(transform) || !stateful.set_paint(paint) ||
        !stateful.clip_rect(1.0f, 2.0f, 10.0f, 20.0f) || !stateful.set_global_alpha(0.5f) ||
        !stateful.push_state() || !stateful.clip_rect(3.0f, 4.0f, 2.0f, 3.0f) ||
        !stateful.set_global_alpha(0.25f) || !stateful.draw_path(path) || !stateful.pop_state() ||
        !stateful.draw_path(path) || !compositor.compile(stateful, main_target, plan, &error))
        return 11;
    const auto &inner = plan.passes[0].commands[0];
    const auto &outer = plan.passes[0].commands[1];
    if (!inner.has_scissor || inner.scissor_x != 11.0f || inner.scissor_y != 19.0f ||
        inner.scissor_width != 4.0f || inner.scissor_height != 9.0f || inner.opacity != 0.25f ||
        !outer.has_scissor || outer.scissor_x != 7.0f || outer.scissor_y != 13.0f ||
        outer.scissor_width != 20.0f || outer.scissor_height != 60.0f || outer.opacity != 0.5f ||
        outer.paint.value != paint.value || outer.transform[0] != 2.0f ||
        outer.transform[3] != 3.0f || outer.transform[4] != 5.0f || outer.transform[5] != 7.0f)
        return 12;
    DisplayList overflow;
    const float overflowing_transform[6] = {std::numeric_limits<float>::max(), 0.0f,
                                            0.0f, 1.0f, 0.0f, 0.0f};
    if (!overflow.set_transform(overflowing_transform) ||
        !overflow.clip_rect(2.0f, 0.0f, 4.0f, 4.0f) ||
        compositor.compile(overflow, main_target, plan, &error) || error.command_index != 1)
        return 13;

    DisplayList stroked;
    if (!stroked.stroke_path(path, 6.0f, 2, 3, 8.0f) ||
        !compositor.compile(stroked, main_target, plan, &error) || plan.passes.size() != 1 ||
        plan.passes[0].commands.size() != 1)
        return 14;
    const auto &stroke = plan.passes[0].commands[0];
    if (stroke.kind != RenderCommandKind::StrokePath || stroke.stroke_width != 6.0f ||
        stroke.line_cap != 2 || stroke.line_join != 3 || stroke.miter_limit != 8.0f)
        return 15;

    const auto target_a = make_resource_id(ResourceKind::RenderTarget, 1, 21);
    const auto target_b = make_resource_id(ResourceKind::RenderTarget, 1, 22);
    RenderPlan cyclic;
    cyclic.passes = {{target_a, {}, false, {{RenderCommandKind::CompositeTarget, target_b}}},
                     {target_b, {}, false, {{RenderCommandKind::CompositeTarget, target_a}}}};
    cyclic.dependencies = {{target_b, target_a}, {target_a, target_b}};
    if (schedule_render_plan(cyclic, pass_order, &schedule_error) ||
        !schedule_error.message ||
        std::string(schedule_error.message) != "render-plan dependency cycle")
        return 17;
    return 0;
}
