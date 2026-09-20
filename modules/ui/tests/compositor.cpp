#include "compositor/compositor.h"

#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

using namespace nkui;

template <class T> void append_record(std::vector<uint8_t> &bytes, const T &value) {
    const auto offset = bytes.size();
    bytes.resize(offset + sizeof(value));
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

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

    BeginLayerEffectV1Command legacy_layer{};
    legacy_layer.base.header = {CommandOpcode::BeginLayer, 1, sizeof(legacy_layer)};
    legacy_layer.base.opacity = 1.0f;
    legacy_layer.base.mode = CompositeMode::SourceOver;
    legacy_layer.base.x = 10.0f;
    legacy_layer.base.y = 10.0f;
    legacy_layer.base.width = 40.0f;
    legacy_layer.base.height = 30.0f;
    legacy_layer.base.flags = LayerIsolated | LayerHasBounds;
    legacy_layer.effect.kind = EffectKind::Blur;
    legacy_layer.effect.color_matrix[0] = 2.0f;
    DrawResourceCommand legacy_draw{{CommandOpcode::DrawPath, 1, sizeof(legacy_draw)}, path};
    ScopeCommand legacy_end{{CommandOpcode::EndLayer, 1, sizeof(legacy_end)}};
    std::vector<uint8_t> legacy_bytes;
    append_record(legacy_bytes, legacy_layer);
    append_record(legacy_bytes, legacy_draw);
    append_record(legacy_bytes, legacy_end);
    DisplayList legacy;
    if (!legacy.assign_validated(legacy_bytes.data(), legacy_bytes.size()) ||
        !compositor.compile(legacy, main_target, plan, &error) || plan.passes.size() != 5 ||
        plan.passes[2].kind != RenderPassKind::Effect ||
        plan.passes[2].effect.kind != EffectKind::Blur)
        return 48;

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
        !nested.end_layer() || !nested.draw_path(path) ||
        !compositor.compile(nested, main_target, plan, &error))
        return 8;
    if (plan.passes.size() != 5 || plan.dependencies.size() != 2 ||
        plan.passes[0].target.value != main_target.value ||
        plan.passes[1].target.value == plan.passes[2].target.value ||
        plan.passes[1].commands.size() != 1 || plan.passes[2].commands.size() != 1 ||
        plan.passes[3].commands.size() != 2 || plan.passes[4].commands.size() != 2 ||
        !plan.passes[3].load_existing || !plan.passes[4].load_existing)
        return 9;

    DisplayList bounded;
    const LayerBounds bounds{10.0f, 20.0f, 80.0f, 40.0f};
    if (!bounded.begin_layer(1.0f, bounds) || !bounded.draw_path(path) || !bounded.end_layer() ||
        !compositor.compile(bounded, main_target, plan, &error) || plan.passes.size() != 3 ||
        plan.dependencies.size() != 1 ||
        plan.passes[1].target_descriptor.logical_width != bounds.width ||
        plan.passes[1].target_descriptor.logical_height != bounds.height ||
        plan.passes[1].target_descriptor.origin_x != bounds.x ||
        plan.passes[1].target_descriptor.origin_y != bounds.y ||
        plan.passes[1].commands.size() != 1 ||
        plan.passes[1].commands[0].transform[4] != -bounds.x ||
        plan.passes[1].commands[0].transform[5] != -bounds.y)
        return 18;
    for (const auto &pass : plan.passes)
        if (!pass.cache_key)
            return 49;
    const auto &bounded_composite = plan.passes[2].commands[0];
    if (bounded_composite.x != bounds.x || bounded_composite.y != bounds.y ||
        bounded_composite.width != bounds.width || bounded_composite.height != bounds.height ||
        bounded_composite.opacity != 1.0f ||
        bounded_composite.composite != CompositeMode::SourceOver)
        return 19;

    const auto embedded_target = make_resource_id(ResourceKind::RenderTarget, 1, 31);
    const auto embedded_remapped_target = make_resource_id(ResourceKind::RenderTarget, 1, 32);
    RenderPlan embedded_source;
    embedded_source.passes.push_back({main_target, {}, false, {}});
    RenderTargetDescriptor embedded_descriptor;
    embedded_descriptor.logical_width = 80.0f;
    embedded_descriptor.logical_height = 40.0f;
    embedded_descriptor.origin_x = 10.0f;
    embedded_descriptor.origin_y = 20.0f;
    embedded_source.passes.push_back({embedded_target, embedded_descriptor, false, {}});
    embedded_source.passes.back().commands.push_back({RenderCommandKind::Path, path});
    embedded_source.passes.front().commands.push_back(
        {RenderCommandKind::CompositeTarget, embedded_target, 10.0f, 20.0f, 80.0f, 40.0f});
    embedded_source.dependencies.push_back({embedded_target, main_target});
    RenderPlan embedded_destination;
    embedded_destination.passes.push_back({main_target, {}, false, {}});
    const std::unordered_map<uint32_t, ResourceId> embedded_remap{
        {embedded_target.value, embedded_remapped_target}};
    RenderPlanEmbedOptions embed_options;
    embed_options.source_main_target = main_target;
    embed_options.destination_main_target = main_target;
    embed_options.placement = {0.0f, 1.0f, -1.0f, 0.0f, 100.0f, 50.0f};
    embed_options.target_remap = &embedded_remap;
    RenderPlanEmbedError embed_error;
    if (!append_embedded_render_plan(embedded_source, embed_options, embedded_destination,
                                     &embed_error) ||
        embedded_destination.passes.size() != 2 ||
        embedded_destination.passes[1].target.value != embedded_remapped_target.value ||
        embedded_destination.passes[1].target_descriptor.origin_x != 40.0f ||
        embedded_destination.passes[1].target_descriptor.origin_y != 60.0f ||
        embedded_destination.passes[1].target_descriptor.logical_width != 80.0f ||
        embedded_destination.passes[1].target_descriptor.logical_height != 40.0f ||
        embedded_destination.passes[1].target_descriptor.width != 80 ||
        embedded_destination.passes[1].target_descriptor.height != 40 ||
        embedded_destination.passes[1].commands.front().transform !=
            std::array<float, 6>{1.0f, 0.0f, 0.0f, 1.0f, -30.0f, -40.0f} ||
        embedded_destination.passes.front().commands.front().transform !=
            std::array<float, 6>{0.0f, 1.0f, -1.0f, 0.0f, 100.0f, 50.0f} ||
        embedded_destination.passes.front().commands.front().resource.value !=
            embedded_remapped_target.value)
        return 40;

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
        plan.passes[2].effect.color_matrix[4] != 0.25f || plan.passes[3].commands.size() != 1 ||
        plan.passes[3].commands[0].resource.value != plan.passes[2].target.value)
        return 21;
    const uint64_t color_effect_cache_key = plan.passes[2].cache_key;
    if (!color_effect_cache_key)
        return 37;
    if (!compositor.compile(color_effect, main_target, plan, &error) ||
        plan.passes[2].cache_key != color_effect_cache_key)
        return 38;
    color_effect.reset();
    effect.color_matrix[4] = 0.5f;
    if (!color_effect.begin_layer(1.0f, bounds, effect) || !color_effect.draw_path(path) ||
        !color_effect.end_layer() || !compositor.compile(color_effect, main_target, plan, &error) ||
        plan.passes[2].cache_key == color_effect_cache_key)
        return 39;
    if (!schedule_render_plan(plan, pass_order, &schedule_error) || pass_order.size() != 4 ||
        pass_order[0] != 0 || pass_order[1] != 1 || pass_order[2] != 2 || pass_order[3] != 3)
        return 22;

    DisplayList custom_effect;
    CustomEffectDescriptor custom{};
    custom.registration_id = 42;
    custom.parameter_count = 2;
    custom.pass_count = 1;
    custom.sampling_inputs = 1;
    custom.ink_overflow = {3.0f, 5.0f, 7.0f, 9.0f};
    custom.parameters[0] = 0.25f;
    custom.parameters[1] = 0.75f;
    if (!custom_effect.begin_layer(1.0f, bounds, custom) || !custom_effect.draw_path(path) ||
        !custom_effect.end_layer() ||
        !compositor.compile(custom_effect, main_target, plan, &error) || plan.passes.size() != 4 ||
        plan.dependencies.size() != 1)
        return 35;
    if (plan.passes[1].target_descriptor.logical_width != 90.0f ||
        plan.passes[1].target_descriptor.logical_height != 54.0f ||
        plan.passes[1].target_descriptor.origin_x != 7.0f ||
        plan.passes[1].target_descriptor.origin_y != 15.0f ||
        plan.passes[2].kind != RenderPassKind::Effect ||
        plan.passes[2].effect.kind != EffectKind::Custom ||
        plan.passes[2].custom_effect.registration_id != 42 ||
        plan.passes[2].custom_effect.parameter_count != 2 ||
        plan.passes[2].custom_effect.parameters[1] != 0.75f ||
        plan.passes[3].commands.size() != 1 || plan.passes[3].commands[0].x != 7.0f ||
        plan.passes[3].commands[0].y != 15.0f || plan.passes[3].commands[0].width != 90.0f ||
        plan.passes[3].commands[0].height != 54.0f ||
        !schedule_render_plan(plan, pass_order, &schedule_error) || pass_order.size() != 4)
        return 36;

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
        plan.passes[3].effect.color_matrix[1] != 1.0f || plan.passes[4].commands.size() != 1 ||
        plan.passes[4].commands[0].resource.value != plan.passes[3].target.value ||
        plan.passes[4].commands[0].x != 4.0f || plan.passes[4].commands[0].y != 14.0f ||
        plan.passes[4].commands[0].width != 92.0f || plan.passes[4].commands[0].height != 52.0f)
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
    if (!drop_shadow_effect.begin_layer(0.5f, bounds, drop_shadow) ||
        !drop_shadow_effect.draw_path(path) || !drop_shadow_effect.end_layer() ||
        !compositor.compile(drop_shadow_effect, main_target, plan, &error) ||
        plan.passes.size() != 6 || plan.dependencies.size() != 3)
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
        plan.passes[4].target.value == plan.passes[3].target.value ||
        plan.passes[4].commands.size() != 2 ||
        plan.passes[4].commands[0].resource.value != plan.passes[3].target.value ||
        plan.passes[4].commands[1].resource.value != drop_shadow_layer.target.value ||
        plan.passes[4].commands[0].opacity != 1.0f || plan.passes[4].commands[1].opacity != 1.0f ||
        plan.passes[4].commands[0].x != 0.0f || plan.passes[4].commands[0].y != 0.0f ||
        plan.passes[4].commands[0].width != 92.0f || plan.passes[4].commands[0].height != 52.0f ||
        plan.passes[5].commands.size() != 1 ||
        plan.passes[5].commands[0].resource.value != plan.passes[4].target.value ||
        plan.passes[5].commands[0].opacity != 0.5f)
        return 27;
    if (!schedule_render_plan(plan, pass_order, &schedule_error) || pass_order.size() != 6 ||
        pass_order[0] != 0 || pass_order[1] != 1 || pass_order[2] != 2 || pass_order[3] != 3 ||
        pass_order[4] != 4 || pass_order[5] != 5)
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

    DisplayList box_shadow;
    const float shadow_radii[] = {5.0f, 7.0f, 9.0f, 11.0f};
    const float shadow_color[] = {0.1f, 0.2f, 0.3f, 0.5f};
    if (!box_shadow.draw_box_shadow(12.0f, 14.0f, 80.0f, 40.0f, 2.0f, 3.0f, 8.0f, 1.0f,
                                    shadow_radii, shadow_color) ||
        !compositor.compile(box_shadow, main_target, plan, &error) || plan.passes.size() != 1 ||
        plan.isolated_layers != 0 || plan.bounded_layers != 0 ||
        plan.passes[0].commands.size() != 1 ||
        plan.passes[0].commands[0].kind != RenderCommandKind::BoxShadow)
        return 50;
    const auto &shadow_command = plan.passes[0].commands[0];
    if (shadow_command.box_shadow.offset_x != 2.0f || shadow_command.box_shadow.offset_y != 3.0f ||
        shadow_command.box_shadow.blur_sigma != 8.0f || shadow_command.box_shadow.spread != 1.0f ||
        shadow_command.box_shadow.radii[2] != 9.0f || shadow_command.box_shadow.color[3] != 0.5f ||
        !shadow_command.opacity)
        return 51;

    DisplayList backdrop;
    EffectDescriptor backdrop_effect{};
    backdrop_effect.kind = EffectKind::ColorMatrix;
    backdrop_effect.color_matrix[0] = 1.0f;
    backdrop_effect.color_matrix[6] = 1.0f;
    backdrop_effect.color_matrix[12] = 1.0f;
    backdrop_effect.color_matrix[18] = 1.0f;
    backdrop_effect.color_matrix[4] = 0.5f;
    if (!backdrop.begin_layer(1.0f, bounds, EffectDescriptor{}, MaskDescriptor{},
                              backdrop_effect) ||
        !backdrop.draw_path(path) || !backdrop.end_layer() || !backdrop.has_backdrop_effects() ||
        !compositor.compile(backdrop, main_target, plan, &error) || plan.passes.size() != 5 ||
        plan.dependencies.size() != 3)
        return 32;
    if (plan.passes[1].kind != RenderPassKind::Effect ||
        plan.passes[1].input_target.value != main_target.value || !plan.passes[1].has_input_rect ||
        plan.passes[1].input_rect[0] != bounds.x || plan.passes[1].input_rect[1] != bounds.y ||
        plan.passes[1].input_rect[2] != bounds.width ||
        plan.passes[1].input_rect[3] != bounds.height ||
        plan.passes[2].target.value == main_target.value || plan.passes[2].commands.size() != 1 ||
        plan.passes[3].commands.size() != 2 ||
        plan.passes[3].commands[0].resource.value != plan.passes[1].target.value ||
        plan.passes[3].commands[1].resource.value != plan.passes[2].target.value ||
        plan.passes[3].commands[0].x != 0.0f || plan.passes[3].commands[0].y != 0.0f ||
        plan.passes[3].commands[0].width != bounds.width ||
        plan.passes[3].commands[0].height != bounds.height || plan.passes[4].commands.size() != 1 ||
        plan.passes[4].commands[0].resource.value != plan.passes[3].target.value)
        return 33;
    if (!schedule_render_plan(plan, pass_order, &schedule_error) || pass_order.size() != 5 ||
        pass_order[0] != 0 || pass_order[1] != 1 || pass_order[2] != 2 || pass_order[3] != 3 ||
        pass_order[4] != 4)
        return 34;

    DisplayList drop_shadow_mask;
    if (!drop_shadow_mask.begin_layer(0.75f, bounds, drop_shadow, rounded_mask) ||
        !drop_shadow_mask.draw_path(path) || !drop_shadow_mask.end_layer() ||
        !compositor.compile(drop_shadow_mask, main_target, plan, &error) ||
        plan.passes.size() != 7 || plan.dependencies.size() != 3)
        return 41;
    if (plan.passes[4].commands.size() != 2 || plan.passes[4].commands[0].opacity != 1.0f ||
        plan.passes[4].commands[1].opacity != 1.0f || plan.passes[5].kind != RenderPassKind::Mask ||
        plan.passes[5].input_target.value != plan.passes[4].target.value ||
        plan.passes[5].mask.kind != MaskKind::RoundedRect || plan.passes[6].commands.size() != 1 ||
        plan.passes[6].commands[0].resource.value != plan.passes[5].target.value ||
        plan.passes[6].commands[0].opacity != 0.75f)
        return 42;

    DisplayList backdrop_mask;
    EffectDescriptor backdrop_blur{};
    backdrop_blur.kind = EffectKind::Blur;
    backdrop_blur.color_matrix[0] = 3.0f;
    if (!backdrop_mask.begin_layer(1.0f, bounds, EffectDescriptor{}, rounded_mask, backdrop_blur) ||
        !backdrop_mask.draw_path(path) || !backdrop_mask.end_layer() ||
        !compositor.compile(backdrop_mask, main_target, plan, &error) || plan.passes.size() != 7 ||
        plan.dependencies.size() != 3)
        return 43;
    if (plan.passes[1].kind != RenderPassKind::Effect ||
        plan.passes[2].kind != RenderPassKind::Effect || plan.passes[3].commands.size() != 1 ||
        plan.passes[4].commands.size() != 2 ||
        plan.passes[4].commands[0].resource.value != plan.passes[2].target.value ||
        plan.passes[4].commands[1].resource.value != plan.passes[3].target.value ||
        plan.passes[5].kind != RenderPassKind::Mask ||
        plan.passes[5].input_target.value != plan.passes[4].target.value ||
        plan.passes[5].mask.kind != MaskKind::RoundedRect || plan.passes[6].commands.size() != 1 ||
        plan.passes[6].commands[0].resource.value != plan.passes[5].target.value)
        return 44;

    DisplayList backdrop_margin;
    const LayerBounds backdrop_bounds{20.0f, 20.0f, 88.0f, 34.0f};
    EffectDescriptor backdrop_margin_blur{};
    backdrop_margin_blur.kind = EffectKind::Blur;
    backdrop_margin_blur.color_matrix[0] = 2.0f;
    if (!backdrop_margin.begin_layer(1.0f, backdrop_bounds, EffectDescriptor{}, MaskDescriptor{},
                                     backdrop_margin_blur) ||
        !backdrop_margin.draw_path(path) || !backdrop_margin.end_layer() ||
        !compositor.compile(backdrop_margin, main_target, plan, &error) ||
        plan.passes.size() != 6 || !plan.passes[1].has_input_rect ||
        plan.passes[1].input_rect != std::array<float, 4>{14.0f, 14.0f, 100.0f, 46.0f} ||
        plan.passes[1].target_descriptor.origin_x != 14.0f ||
        plan.passes[1].target_descriptor.origin_y != 14.0f ||
        plan.passes[1].target_descriptor.logical_width != 100.0f ||
        plan.passes[1].target_descriptor.logical_height != 46.0f)
        return 47;

    EffectOpCommand matrix_before{};
    matrix_before.kind = EffectKind::ColorMatrix;
    matrix_before.color_matrix[0] = 1.0f;
    matrix_before.color_matrix[6] = 1.0f;
    matrix_before.color_matrix[12] = 1.0f;
    matrix_before.color_matrix[18] = 1.0f;
    matrix_before.color_matrix[4] = 0.25f;
    EffectOpCommand chained_blur{};
    chained_blur.kind = EffectKind::Blur;
    chained_blur.color_matrix[0] = 2.0f;
    EffectOpCommand matrix_after = matrix_before;
    matrix_after.color_matrix[4] = 0.75f;
    DisplayList chained_effects;
    if (!chained_effects.begin_layer(0.6f, bounds, {matrix_before, chained_blur, matrix_after},
                                     rounded_mask) ||
        !chained_effects.draw_path(path) || !chained_effects.end_layer() ||
        !compositor.compile(chained_effects, main_target, plan, &error) ||
        plan.passes.size() != 8 || plan.dependencies.size() != 1)
        return 45;
    if (plan.passes[2].kind != RenderPassKind::Effect ||
        plan.passes[2].effect.kind != EffectKind::ColorMatrix ||
        plan.passes[3].effect.kind != EffectKind::Blur ||
        plan.passes[4].effect.kind != EffectKind::Blur ||
        plan.passes[5].effect.kind != EffectKind::ColorMatrix ||
        plan.passes[6].kind != RenderPassKind::Mask || plan.passes[7].commands.size() != 1 ||
        plan.passes[7].commands[0].resource.value != plan.passes[6].target.value ||
        plan.passes[7].commands[0].opacity != 0.6f)
        return 46;

    CustomEffectDescriptor mixed_custom{};
    mixed_custom.registration_id = 77;
    mixed_custom.parameter_count = 1;
    mixed_custom.pass_count = 1;
    mixed_custom.sampling_inputs = 1;
    mixed_custom.parameters[0] = 0.5f;
    EffectOpCommand mixed_custom_operation{};
    mixed_custom_operation.kind = EffectKind::Custom;
    mixed_custom_operation.custom = mixed_custom;
    const std::vector<EffectOpCommand> mixed_operations{
        chained_blur, matrix_before, mixed_custom_operation, chained_blur, matrix_after};
    DisplayList mixed_effects;
    if (!mixed_effects.begin_layer(0.8f, bounds, mixed_operations) ||
        !mixed_effects.draw_path(path) || !mixed_effects.end_layer() ||
        !compositor.compile(mixed_effects, main_target, plan, &error) || plan.passes.size() != 10 ||
        plan.dependencies.size() != 1)
        return 52;
    const EffectKind mixed_kinds[] = {
        EffectKind::Blur, EffectKind::Blur, EffectKind::ColorMatrix, EffectKind::Custom,
        EffectKind::Blur, EffectKind::Blur, EffectKind::ColorMatrix};
    for (size_t index = 0; index < sizeof(mixed_kinds) / sizeof(mixed_kinds[0]); ++index) {
        const auto &pass = plan.passes[2 + index];
        if (pass.kind != RenderPassKind::Effect || pass.effect.kind != mixed_kinds[index])
            return 53;
    }
    if (plan.passes[5].custom_effect.registration_id != mixed_custom.registration_id ||
        plan.passes[5].custom_effect.parameter_count != mixed_custom.parameter_count ||
        plan.passes[5].custom_effect.parameters[0] != mixed_custom.parameters[0] ||
        plan.passes[9].kind != RenderPassKind::Draw || plan.passes[9].commands.size() != 1 ||
        plan.passes[9].commands[0].resource.value != plan.passes[8].target.value)
        return 54;

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
    const float overflowing_transform[6] = {
        std::numeric_limits<float>::max(), 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
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
    if (schedule_render_plan(cyclic, pass_order, &schedule_error) || !schedule_error.message ||
        std::string(schedule_error.message) != "render-plan dependency cycle")
        return 17;
    return 0;
}
