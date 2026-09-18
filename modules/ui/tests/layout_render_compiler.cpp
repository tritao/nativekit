#include "layout/layout_engine.h"
#include "layout/layout_render_compiler.h"
#include "render/render_plan_executor.h"
#include "render/sealed_render_plan.h"
#include "render/ui_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

using namespace nkui;

namespace {

LayoutNode box(uint32_t id, int32_t parent) {
    LayoutNode node;
    node.id = id;
    node.parent = parent;
    node.style.width = {LayoutSizing::Grow, 0.0f};
    node.style.height = {LayoutSizing::Fit, 0.0f};
    return node;
}

class RecordingRenderer final : public UiRenderer {
  public:
    bool initialize() override { return true; }
    bool valid() const override { return true; }
    bool lost() const override { return false; }
    bool beginFrame() override {
        ++frame_count;
        return true;
    }
    bool beginWindowPass(int, int, bool) override {
        ++pass_count;
        return true;
    }
    bool beginTargetPass(ResourceId, int, int, bool) override {
        ++pass_count;
        return true;
    }
    bool beginEffectPass(ResourceId, uint64_t cache_key, int, int, bool &cache_hit) override {
        cache_hit = cache_key != 0 && !effect_cache_keys.insert(cache_key).second;
        if (cache_hit)
            ++effect_cache_hits;
        ++pass_count;
        return true;
    }
    bool beginSurfacePass(ResourceId, const SurfaceDescriptor &, bool) override {
        ++pass_count;
        return true;
    }
    bool drawSurfaceMesh(const SurfaceMeshView &) override {
        ++surface_mesh_count;
        return true;
    }
    bool surfaceHasContent(ResourceId) const override { return false; }
    bool surfaceIsCurrent(ResourceId, uint32_t, const SurfaceDescriptor &) const override {
        return false;
    }
    void markSurfaceCurrent(ResourceId, uint32_t, const SurfaceDescriptor &) override {}

    bool setScissor(bool enabled, float x, float y, float width, float height) override {
        last_scissor_enabled = enabled;
        last_scissor = {x, y, width, height};
        return true;
    }
    bool drawPath(const PreparedPathData &, uint32_t, float) override {
        ++path_count;
        return true;
    }
    bool drawPath(const PreparedPathData &path, uint32_t operation_index, const float[6],
                  float) override {
        if (operation_index >= path.operations().size())
            return false;
        ++path_count;
        return true;
    }
    bool drawImage(const PreparedTexture &, float, float, float, float, const float[6],
                   float) override {
        return true;
    }
    bool drawBoxShadow(float, float, float, float, const float[6], float,
                       const BoxShadowDescriptor &) override {
        ++box_shadow_count;
        return true;
    }
    bool uploadAtlases(SkribidiAdapter &, bool) override { return true; }
    bool drawGlyphs(const PreparedGlyphs &, float) override {
        ++text_count;
        return true;
    }
    bool drawGlyphs(const PreparedGlyphs &glyphs, const float[6], float, float, float) override {
        if (glyphs.vertices.empty())
            return false;
        ++text_count;
        return true;
    }
    bool compositeImage(ResourceId, float, float, float, float, const float[6], float) override {
        return true;
    }
    bool compositeImage(nk_graphics_image, float, float, float, float, const float[6],
                        float) override {
        return true;
    }
    bool applyEffect(ResourceId, const EffectDescriptor &) override {
        ++effect_count;
        return true;
    }
    bool applyMask(ResourceId, const MaskDescriptor &, const PreparedTexture *) override {
        ++mask_count;
        return true;
    }
    bool endPass() override { return true; }
    bool endFrame() override {
        ++commit_count;
        return true;
    }
    UiRendererStats stats() const override { return {}; }
    const char *lastError() const override { return error.c_str(); }

    uint32_t frame_count = 0;
    uint32_t pass_count = 0;
    uint32_t path_count = 0;
    uint32_t text_count = 0;
    uint32_t effect_count = 0;
    uint32_t mask_count = 0;
    uint32_t surface_mesh_count = 0;
    uint32_t box_shadow_count = 0;
    uint32_t commit_count = 0;
    uint32_t effect_cache_hits = 0;
    std::unordered_set<uint64_t> effect_cache_keys;
    bool last_scissor_enabled = false;
    std::array<float, 4> last_scissor{};
    std::string error;
};

class MutableSurfaceProducer final : public SurfaceProducer {
  public:
    bool ready() const override { return true; }
    bool describe(int requested_width, int requested_height,
                  SurfaceDescriptor &description) const override {
        description = {requested_width, requested_height, SurfacePixelFormat::Rgba8,
                       SurfaceAlphaMode::Premultiplied};
        return true;
    }
    uint32_t generation() const override { return generation_value; }
    SurfaceRenderResult render(UiRenderer &, ResourceId, const SurfaceDescriptor &) override {
        return SurfaceRenderResult::Rendered;
    }

    uint32_t generation_value = 1;
};

} // namespace

int main() {
#ifndef NKUI_TEST_FONT_PATH
    std::cerr << "NKUI_TEST_FONT_PATH is required\n";
    return 2;
#else
    auto shared_fonts = std::make_shared<SkribidiFontCollection>();
    if (!shared_fonts->valid() || !shared_fonts->add_font(NKUI_TEST_FONT_PATH) ||
        shared_fonts->font_load_count() != 1)
        return 3;
    SkribidiAdapter direct_layout(shared_fonts);
    if (!direct_layout.layout_utf8("shared direct layout", 240.0f, 16.0f) ||
        shared_fonts->font_load_count() != 1)
        return 3;
    LayoutEngine engine(shared_fonts);
    if (!engine.valid())
        return 3;

    std::vector<LayoutNode> nodes;
    LayoutNode root = box(1, -1);
    root.style.width = {LayoutSizing::Fixed, 320.0f};
    root.style.height = {LayoutSizing::Fixed, 200.0f};
    root.style.padding_left = root.style.padding_right = 16;
    root.style.padding_top = root.style.padding_bottom = 12;
    root.style.child_gap = 8;
    root.style.background = {0.9f, 0.92f, 0.98f, 1.0f};
    root.style.radius_top_left = root.style.radius_top_right = 12.0f;
    root.style.radius_bottom_left = root.style.radius_bottom_right = 12.0f;
    nodes.push_back(root);

    LayoutNode button = box(2, 0);
    button.style.width = {LayoutSizing::Fixed, 160.0f};
    button.style.height = {LayoutSizing::Fit, 0.0f};
    button.style.padding_left = button.style.padding_right = 12;
    button.style.padding_top = button.style.padding_bottom = 8;
    button.style.transform.tx = 10.0f;
    button.style.transform.ty = 6.0f;
    button.style.clip_horizontal = true;
    button.style.clip_vertical = true;
    button.style.background = {0.2f, 0.5f, 0.9f, 1.0f};
    button.style.radius_top_left = button.style.radius_top_right = 8.0f;
    button.style.radius_bottom_left = button.style.radius_bottom_right = 8.0f;
    nodes.push_back(button);

    LayoutNode label = box(3, 1);
    label.visual_kind = LayoutVisualKind::Text;
    label.text = "Compile me";
    label.text_style.font_size = 18.0f;
    label.text_color = {0.1f, 0.15f, 0.25f, 1.0f};
    nodes.push_back(label);

    LayoutSnapshot snapshot;
    LayoutError layout_error;
    if (!engine.layout(nodes, 320.0f, 200.0f, 1.0f / 60.0f, snapshot, &layout_error))
        return 4;

    LayoutRenderCompiler compiler;
    compiler.set_font_collection(shared_fonts);
    const ResourceId main_target = make_resource_id(ResourceKind::RenderTarget, 1, 1);
    LayoutRenderFrame frame;
    LayoutRenderCompileError compile_error;
    const uint32_t layout_builds = engine.text_adapter()->layout_build_count();
    if (!compiler.compile(snapshot, main_target, 1.5f, frame, &compile_error, false,
                          engine.text_adapter())) {
        std::cerr << (compile_error.message ? compile_error.message : "compile failed") << "\n";
        return 6;
    }
    if (engine.text_adapter()->layout_build_count() != layout_builds ||
        frame.text_adapter() != engine.text_adapter() || shared_fonts->font_load_count() != 1)
        return 6;
    if (frame.plan().passes.size() != 1 || frame.plan().passes.front().commands.size() < 3 ||
        !frame.text_adapter())
        return 7;

    uint32_t path_commands = 0;
    uint32_t text_commands = 0;
    const LayoutItem *button_item = snapshot.find(2);
    if (!button_item)
        return 8;
    for (const auto &command : frame.plan().passes.front().commands) {
        if (command.kind == RenderCommandKind::Path) {
            const auto *path = frame.resources().path(command.resource);
            if (!path || path->operation_index >= path->path->operations().size())
                return 8;
            ++path_commands;
        } else if (command.kind == RenderCommandKind::GlyphBatch) {
            const auto *text = frame.resources().text(command.resource);
            if (!text || text->vertices.empty() || command.transform[0] != 1.5f)
                return 9;
            const float clip_x = (button_item->bounds.x + 10.0f) * 1.5f;
            const float clip_y = (button_item->bounds.y + 6.0f) * 1.5f;
            if (!command.has_scissor || command.scissor_x != clip_x ||
                command.scissor_y != clip_y ||
                command.scissor_width != button_item->bounds.width * 1.5f ||
                command.scissor_height != button_item->bounds.height * 1.5f)
                return 9;
            if (text->vertices.front().red != 26 || text->vertices.front().green != 38 ||
                text->vertices.front().blue != 64)
                return 10;
            ++text_commands;
        }
    }
    const uint32_t expected_text_commands = static_cast<uint32_t>(std::count_if(
        snapshot.primitives.begin(), snapshot.primitives.end(),
        [](const LayoutPrimitive &primitive) {
            return primitive.kind == LayoutPrimitiveKind::Text && !primitive.text.empty();
        }));
    if (path_commands < 2 || text_commands != expected_text_commands)
        return 11;

    // RTL lines keep a non-zero horizontal line origin in Skribidi. Verify
    // that the compiled glyphs retain it, so they remain aligned with the
    // selection rectangles painted in the text item's coordinate space.
    const char *rtl_values[] = {"مرحبا بالعالم", "שלום עולם"};
    const int32_t rtl_lengths[] = {13, 9};
    for (std::size_t rtl_index = 0; rtl_index < std::size(rtl_values); ++rtl_index) {
        const char *value = rtl_values[rtl_index];
        std::vector<LayoutNode> rtl_nodes;
        LayoutNode rtl_root = box(10, -1);
        rtl_root.style.width = {LayoutSizing::Fixed, 320.0f};
        rtl_root.style.height = {LayoutSizing::Fixed, 80.0f};
        rtl_root.style.padding_left = rtl_root.style.padding_right = 12;
        rtl_root.style.padding_top = rtl_root.style.padding_bottom = 10;
        rtl_nodes.push_back(rtl_root);

        LayoutNode rtl_text = box(11, 0);
        rtl_text.visual_kind = LayoutVisualKind::Text;
        rtl_text.text = value;
        rtl_text.text_style.font_size = 18.0f;
        rtl_text.paragraph_style.wrap = TextWrapMode::None;
        rtl_nodes.push_back(rtl_text);

        LayoutSnapshot rtl_snapshot;
        if (!engine.layout(rtl_nodes, 320.0f, 80.0f, 1.0f / 60.0f, rtl_snapshot, &layout_error))
            return 14;
        const auto rtl_primitive = std::find_if(
            rtl_snapshot.primitives.begin(), rtl_snapshot.primitives.end(),
            [](const LayoutPrimitive &primitive) {
                return primitive.kind == LayoutPrimitiveKind::Text && primitive.node_id == 11;
            });
        const auto rtl_layout =
            std::find_if(rtl_snapshot.text_layouts.begin(), rtl_snapshot.text_layouts.end(),
                         [](const LayoutTextLayout &layout) { return layout.node_id == 11; });
        const LayoutItem *rtl_item = rtl_snapshot.find(11);
        if (rtl_primitive == rtl_snapshot.primitives.end() ||
            rtl_layout == rtl_snapshot.text_layouts.end() || rtl_layout->lines.size() != 1 ||
            !rtl_item || std::abs(rtl_layout->width - rtl_item->bounds.width) > 0.01f ||
            rtl_layout->lines.front().bounds.x <= 1.0f)
            return 14;
        const auto selection =
            engine.text_adapter()->selection_rects({0, 0}, {rtl_lengths[rtl_index], 0});
        if (selection.empty() ||
            std::abs(selection.front().x - rtl_layout->lines.front().bounds.x) > 0.01f)
            return 14;

        LayoutRenderFrame rtl_frame;
        if (!compiler.compile(rtl_snapshot, main_target, 1.0f, rtl_frame, &compile_error, false,
                              engine.text_adapter()))
            return 14;
        const auto rtl_command = std::find_if(
            rtl_frame.plan().passes.front().commands.begin(),
            rtl_frame.plan().passes.front().commands.end(), [](const RenderCommand &command) {
                return command.kind == RenderCommandKind::GlyphBatch;
            });
        if (rtl_command == rtl_frame.plan().passes.front().commands.end())
            return 14;
        const PreparedGlyphs *rtl_glyphs = rtl_frame.resources().text(rtl_command->resource);
        if (!rtl_glyphs || rtl_glyphs->vertices.empty())
            return 14;

        float min_x = rtl_glyphs->vertices.front().x;
        float max_x = min_x;
        for (const auto &vertex : rtl_glyphs->vertices) {
            min_x = std::min(min_x, vertex.x);
            max_x = std::max(max_x, vertex.x);
        }
        const LayoutRect &line_bounds = rtl_layout->lines.front().bounds;
        const float world_min_x = min_x + rtl_command->x;
        const float world_max_x = max_x + rtl_command->x;
        const float expected_min_x = rtl_item->bounds.x + line_bounds.x;
        const float expected_max_x = expected_min_x + line_bounds.width;
        if (world_min_x < expected_min_x - 2.0f || world_max_x > expected_max_x + 2.0f)
            return 14;
    }
    const uint32_t layout_builds_after_rtl = engine.text_adapter()->layout_build_count();

    // Custom display-list commands join the ordered layout stream at the
    // node marker, before that node's descendants.
    LayoutSnapshot ordered_snapshot = snapshot;
    const auto label_primitive =
        std::find_if(ordered_snapshot.primitives.begin(), ordered_snapshot.primitives.end(),
                     [](const LayoutPrimitive &primitive) {
                         return primitive.kind == LayoutPrimitiveKind::Text;
                     });
    if (label_primitive == ordered_snapshot.primitives.end())
        return 20;
    LayoutPrimitive custom_marker;
    custom_marker.kind = LayoutPrimitiveKind::Custom;
    custom_marker.node_id = 2;
    custom_marker.bounds = button_item->bounds;
    custom_marker.transform = button_item->transform;
    ordered_snapshot.primitives.insert(label_primitive, custom_marker);
    RenderPlan custom_plan;
    custom_plan.passes.push_back({main_target, {}, false, {}});
    const ResourceId custom_path = make_resource_id(ResourceKind::Path, 1, 444);
    RenderCommand custom_command{RenderCommandKind::Path, custom_path};
    custom_command.transform = {2.0f, 0.0f, 0.0f, 2.0f, 10.0f, 20.0f};
    custom_command.has_scissor = true;
    // Custom display-list scissors are already in resolved viewport space.
    custom_command.scissor_x = 26.0f;
    custom_command.scissor_y = 18.0f;
    custom_command.scissor_width = 3.0f;
    custom_command.scissor_height = 4.0f;
    custom_plan.passes.front().commands.push_back(custom_command);
    LayoutRenderCompiler::CustomPaintPlans custom_paints{{2, &custom_plan}};
    LayoutRenderFrame ordered_frame;
    if (!compiler.compile(ordered_snapshot, main_target, 1.5f, ordered_frame, &compile_error, false,
                          engine.text_adapter(), &custom_paints))
        return 21;
    const auto &ordered_commands = ordered_frame.plan().passes.front().commands;
    const auto custom_position =
        std::find_if(ordered_commands.begin(), ordered_commands.end(),
                     [](const RenderCommand &command) { return command.custom_payload; });
    if (custom_position == ordered_commands.end() || custom_position == ordered_commands.begin() ||
        custom_position + 1 == ordered_commands.end() ||
        (custom_position - 1)->kind != RenderCommandKind::Path ||
        custom_position->resource.value != custom_path.value ||
        (custom_position + 1)->kind != RenderCommandKind::GlyphBatch ||
        custom_position->transform != std::array<float, 6>{3.0f, 0.0f, 0.0f, 3.0f, 15.0f, 30.0f} ||
        custom_position->scissor_x != 39.0f || custom_position->scissor_y != 27.0f ||
        custom_position->scissor_width != 4.5f || custom_position->scissor_height != 6.0f)
        return 22;

    // A style decoration may target an ordinary box without changing its
    // native layout visual kind. Its retained paint joins immediately after
    // that box's own primitive instead of being silently dropped.
    LayoutRenderCompiler::CustomPaintPlans decorated_paints{{2, &custom_plan}};
    LayoutRenderFrame decorated_frame;
    if (!compiler.compile(snapshot, main_target, 1.5f, decorated_frame, &compile_error, false,
                          engine.text_adapter(), &decorated_paints))
        return 23;
    const auto &decorated_commands = decorated_frame.plan().passes.front().commands;
    const auto decorated_position =
        std::find_if(decorated_commands.begin(), decorated_commands.end(),
                     [](const RenderCommand &command) { return command.custom_payload; });
    if (decorated_position == decorated_commands.end() ||
        decorated_position == decorated_commands.begin() ||
        (decorated_position - 1)->kind != RenderCommandKind::Path ||
        decorated_position->resource.value != custom_path.value)
        return 24;

    RenderPlan bounded_custom_plan;
    bounded_custom_plan.passes.push_back({main_target, {}, false, {}});
    const ResourceId bounded_target = make_resource_id(ResourceKind::RenderTarget, 1, 445);
    RenderTargetDescriptor bounded_descriptor;
    bounded_descriptor.logical_width = 20.0f;
    bounded_descriptor.logical_height = 10.0f;
    bounded_descriptor.origin_x = 2.0f;
    bounded_descriptor.origin_y = 3.0f;
    bounded_custom_plan.passes.push_back({bounded_target, bounded_descriptor, false, {}});
    bounded_custom_plan.passes.front().commands.push_back(
        {RenderCommandKind::CompositeTarget, bounded_target, 2.0f, 3.0f, 20.0f, 10.0f});
    RenderCommand bounded_command{RenderCommandKind::Path, custom_path};
    bounded_command.transform = {1.0f, 0.0f, 0.0f, 1.0f, 20.0f, 30.0f};
    bounded_command.has_scissor = true;
    bounded_command.scissor_x = 4.0f;
    bounded_command.scissor_y = 5.0f;
    bounded_command.scissor_width = 6.0f;
    bounded_command.scissor_height = 7.0f;
    bounded_custom_plan.passes.back().commands.push_back(bounded_command);
    bounded_custom_plan.dependencies.push_back({bounded_target, main_target});
    LayoutRenderCompiler::CustomPaintPlans bounded_paints{{2, &bounded_custom_plan}};
    LayoutRenderFrame bounded_frame;
    if (!compiler.compile(ordered_snapshot, main_target, 1.5f, bounded_frame, &compile_error, false,
                          engine.text_adapter(), &bounded_paints) ||
        bounded_frame.plan().passes.size() != 2)
        return 23;
    const auto &bounded_pass = bounded_frame.plan().passes[1];
    const auto bounded_composite = std::find_if(
        bounded_frame.plan().passes.front().commands.begin(),
        bounded_frame.plan().passes.front().commands.end(), [](const RenderCommand &command) {
            return command.kind == RenderCommandKind::CompositeTarget && command.custom_payload;
        });
    if (bounded_pass.target_descriptor.logical_width != 20.0f ||
        bounded_pass.target_descriptor.logical_height != 10.0f ||
        bounded_pass.target_descriptor.width != 30 || bounded_pass.target_descriptor.height != 15 ||
        bounded_pass.target_descriptor.origin_x != 12.0f ||
        bounded_pass.target_descriptor.origin_y != 9.0f ||
        bounded_pass.commands.front().transform !=
            std::array<float, 6>{1.5f, 0.0f, 0.0f, 1.5f, 15.0f, 36.0f} ||
        bounded_pass.commands.front().scissor_x != -9.0f ||
        bounded_pass.commands.front().scissor_y != -1.5f ||
        bounded_pass.commands.front().scissor_width != 9.0f ||
        bounded_pass.commands.front().scissor_height != 10.5f ||
        bounded_composite == bounded_frame.plan().passes.front().commands.end() ||
        bounded_composite->transform != std::array<float, 6>{1.5f, 0.0f, 0.0f, 1.5f, 15.0f, 9.0f})
        return 24;

    const ResourceId scaled_effect_input = make_resource_id(ResourceKind::RenderTarget, 1, 448);
    const ResourceId scaled_effect_output = make_resource_id(ResourceKind::RenderTarget, 1, 449);
    RenderPlan scaled_effect_plan;
    scaled_effect_plan.passes.push_back({main_target, {}, false, {}});
    RenderPass scaled_effect_input_pass;
    scaled_effect_input_pass.target = scaled_effect_input;
    scaled_effect_input_pass.target_descriptor.logical_width = 20.0f;
    scaled_effect_input_pass.target_descriptor.logical_height = 10.0f;
    scaled_effect_input_pass.commands.push_back({RenderCommandKind::Path, custom_path});
    scaled_effect_plan.passes.push_back(std::move(scaled_effect_input_pass));
    RenderPass scaled_effect_pass;
    scaled_effect_pass.target = scaled_effect_output;
    scaled_effect_pass.target_descriptor.logical_width = 20.0f;
    scaled_effect_pass.target_descriptor.logical_height = 10.0f;
    scaled_effect_pass.kind = RenderPassKind::Effect;
    scaled_effect_pass.input_target = scaled_effect_input;
    scaled_effect_pass.effect.kind = EffectKind::DropShadow;
    scaled_effect_pass.effect.color_matrix[0] = 4.0f;
    scaled_effect_pass.effect.color_matrix[2] = 2.0f;
    scaled_effect_pass.effect.color_matrix[3] = -3.0f;
    scaled_effect_plan.passes.push_back(std::move(scaled_effect_pass));
    scaled_effect_plan.dependencies.push_back({scaled_effect_output, main_target});
    LayoutRenderCompiler::CustomPaintPlans scaled_effect_paints{{2, &scaled_effect_plan}};
    LayoutRenderFrame scaled_effect_frame;
    if (!compiler.compile(ordered_snapshot, main_target, 1.5f, scaled_effect_frame, &compile_error,
                          false, engine.text_adapter(), &scaled_effect_paints) ||
        scaled_effect_frame.plan().passes.size() != 3)
        return 26;
    const auto &scaled_effect = scaled_effect_frame.plan().passes[2];
    if (scaled_effect.target_descriptor.width != 30 ||
        scaled_effect.target_descriptor.height != 15 ||
        scaled_effect.effect.color_matrix[0] != 6.0f ||
        scaled_effect.effect.color_matrix[2] != 3.0f ||
        scaled_effect.effect.color_matrix[3] != -4.5f)
        return 27;

    RecordingRenderer backend;
    nk_surface_frame_target frame_target{};
    frame_target.struct_size = sizeof(frame_target);
    frame_target.api = NK_GRAPHICS_OPENGL;
    frame_target.width = 480;
    frame_target.height = 300;
    RenderExecutionError execution_error;
    if (!execute_render_plan(backend, frame.plan(), frame.resources(), {main_target, frame_target},
                             &execution_error) ||
        backend.pass_count != 1 || backend.path_count != path_commands ||
        backend.text_count != text_commands || backend.commit_count != 1)
        return 12;

    const ResourceId effect_input = make_resource_id(ResourceKind::RenderTarget, 1, 446);
    const ResourceId effect_output = make_resource_id(ResourceKind::RenderTarget, 1, 447);
    RenderPlan effect_plan;
    effect_plan.passes.push_back({main_target, {}, false, {}});
    effect_plan.passes.push_back({effect_input, {}, false, {}});
    RenderPass effect_pass;
    effect_pass.target = effect_output;
    effect_pass.kind = RenderPassKind::Effect;
    effect_pass.input_target = effect_input;
    effect_pass.effect.kind = EffectKind::ColorMatrix;
    effect_pass.effect.color_matrix[0] = 1.0f;
    effect_pass.effect.color_matrix[6] = 1.0f;
    effect_pass.effect.color_matrix[12] = 1.0f;
    effect_pass.effect.color_matrix[18] = 1.0f;
    effect_plan.passes.push_back(effect_pass);
    if (!execute_render_plan(backend, effect_plan, frame.resources(), {main_target, frame_target},
                             &execution_error) ||
        backend.effect_count != 1 || backend.commit_count != 2)
        return 25;

    const ResourceId mask_input = make_resource_id(ResourceKind::RenderTarget, 1, 450);
    const ResourceId mask_output = make_resource_id(ResourceKind::RenderTarget, 1, 451);
    RenderPlan mask_plan;
    mask_plan.passes.push_back({main_target, {}, false, {}});
    mask_plan.passes.push_back({mask_input, {}, false, {}});
    RenderPass mask_pass;
    mask_pass.target = mask_output;
    mask_pass.kind = RenderPassKind::Mask;
    mask_pass.input_target = mask_input;
    mask_pass.mask.kind = MaskKind::LinearGradient;
    mask_pass.mask.values[0] = 0.0f;
    mask_pass.mask.values[2] = 1.0f;
    mask_pass.mask.values[4] = 0.0f;
    mask_pass.mask.values[5] = 1.0f;
    mask_plan.passes.push_back(mask_pass);
    if (!execute_render_plan(backend, mask_plan, frame.resources(), {main_target, frame_target},
                             &execution_error) ||
        backend.mask_count != 1 || backend.commit_count != 3)
        return 28;

    RenderPlan box_shadow_plan;
    box_shadow_plan.passes.push_back({main_target, {}, false, {}});
    RenderCommand box_shadow_command;
    box_shadow_command.kind = RenderCommandKind::BoxShadow;
    box_shadow_command.x = 12.0f;
    box_shadow_command.y = 14.0f;
    box_shadow_command.width = 80.0f;
    box_shadow_command.height = 40.0f;
    box_shadow_command.box_shadow.blur_sigma = 8.0f;
    box_shadow_command.box_shadow.color = {0.1f, 0.2f, 0.3f, 0.5f};
    box_shadow_plan.passes.front().commands.push_back(box_shadow_command);
    if (!execute_render_plan(backend, box_shadow_plan, frame.resources(),
                             {main_target, frame_target}, &execution_error) ||
        backend.box_shadow_count != 1 || backend.commit_count != 4)
        return 29;

    // Effect cache identity follows only the targets that feed an effect. An
    // unrelated external surface in the final composition must not evict the
    // static blur, while source and mask generations must invalidate the
    // dependent effect chain.
    const ResourceId cache_main = make_resource_id(ResourceKind::RenderTarget, 1, 460);
    const ResourceId cache_source = make_resource_id(ResourceKind::RenderTarget, 1, 461);
    const ResourceId cache_blur = make_resource_id(ResourceKind::RenderTarget, 1, 462);
    const ResourceId cache_mask = make_resource_id(ResourceKind::RenderTarget, 1, 463);
    const ResourceId cache_output = make_resource_id(ResourceKind::RenderTarget, 1, 464);
    const ResourceId cache_surface = make_resource_id(ResourceKind::RenderTarget, 1, 465);
    const ResourceId cache_source_image = make_resource_id(ResourceKind::Image, 1, 466);
    const ResourceId cache_mask_image = make_resource_id(ResourceKind::Image, 1, 467);
    PreparedTexture cache_source_texture;
    cache_source_texture.width = cache_source_texture.height = 1;
    cache_source_texture.pixels = {255, 255, 255, 255};
    PreparedTexture cache_mask_texture = cache_source_texture;
    MutableSurfaceProducer cache_surface_producer;
    FrameResources cache_resources;
    if (!cache_resources.bind_image(cache_source_image, cache_source_texture, 10) ||
        !cache_resources.bind_image(cache_mask_image, cache_mask_texture, 20) ||
        !cache_resources.bind_surface(cache_surface, cache_surface_producer))
        return 29;

    RenderPlan cache_plan;
    RenderPass cache_source_pass;
    cache_source_pass.target = cache_source;
    RenderCommand cache_image_command;
    cache_image_command.kind = RenderCommandKind::Image;
    cache_image_command.resource = cache_source_image;
    cache_image_command.width = cache_image_command.height = 10.0f;
    cache_source_pass.commands.push_back(cache_image_command);
    cache_plan.passes.push_back(std::move(cache_source_pass));

    RenderPass cache_blur_pass;
    cache_blur_pass.target = cache_blur;
    cache_blur_pass.kind = RenderPassKind::Effect;
    cache_blur_pass.input_target = cache_source;
    cache_blur_pass.effect.kind = EffectKind::ColorMatrix;
    cache_blur_pass.effect.color_matrix[0] = 1.0f;
    cache_blur_pass.effect.color_matrix[6] = 1.0f;
    cache_blur_pass.effect.color_matrix[12] = 1.0f;
    cache_blur_pass.effect.color_matrix[18] = 1.0f;
    cache_blur_pass.cache_key = 0x1001;
    cache_plan.passes.push_back(std::move(cache_blur_pass));

    RenderPass cache_mask_pass;
    cache_mask_pass.target = cache_mask;
    cache_mask_pass.kind = RenderPassKind::Mask;
    cache_mask_pass.input_target = cache_blur;
    cache_mask_pass.mask.kind = MaskKind::Image;
    cache_mask_pass.mask.image = cache_mask_image;
    cache_mask_pass.cache_key = 0x1002;
    cache_plan.passes.push_back(std::move(cache_mask_pass));

    RenderPass cache_output_pass;
    cache_output_pass.target = cache_output;
    cache_output_pass.kind = RenderPassKind::Effect;
    cache_output_pass.input_target = cache_mask;
    cache_output_pass.effect.kind = EffectKind::ColorMatrix;
    cache_output_pass.effect.color_matrix[0] = 1.0f;
    cache_output_pass.effect.color_matrix[6] = 1.0f;
    cache_output_pass.effect.color_matrix[12] = 1.0f;
    cache_output_pass.effect.color_matrix[18] = 1.0f;
    cache_output_pass.cache_key = 0x1003;
    cache_plan.passes.push_back(std::move(cache_output_pass));

    RenderPass cache_main_pass;
    cache_main_pass.target = cache_main;
    RenderCommand cache_surface_command;
    cache_surface_command.kind = RenderCommandKind::CompositeTarget;
    cache_surface_command.resource = cache_surface;
    cache_surface_command.width = cache_surface_command.height = 10.0f;
    cache_main_pass.commands.push_back(cache_surface_command);
    RenderCommand cache_output_command;
    cache_output_command.kind = RenderCommandKind::CompositeTarget;
    cache_output_command.resource = cache_output;
    cache_output_command.width = cache_output_command.height = 10.0f;
    cache_main_pass.commands.push_back(cache_output_command);
    cache_plan.passes.push_back(std::move(cache_main_pass));
    cache_plan.dependencies.push_back({cache_output, cache_main});
    cache_plan.dependencies.push_back({cache_surface, cache_main});

    RecordingRenderer cache_backend;
    auto execute_cache_plan = [&] {
        return execute_render_plan(cache_backend, cache_plan, cache_resources,
                                   {cache_main, frame_target}, &execution_error);
    };
    if (!execute_cache_plan() || cache_backend.effect_cache_keys.size() != 2 ||
        cache_backend.effect_cache_hits != 0 || cache_backend.effect_count != 2)
        return 30;
    cache_surface_producer.generation_value = 2;
    if (!execute_cache_plan() || cache_backend.effect_cache_keys.size() != 2 ||
        cache_backend.effect_cache_hits != 2 || cache_backend.effect_count != 2)
        return 31;
    if (!cache_resources.bind_image(cache_source_image, cache_source_texture, 11) ||
        !execute_cache_plan() || cache_backend.effect_cache_keys.size() != 4 ||
        cache_backend.effect_cache_hits != 2 || cache_backend.effect_count != 4)
        return 32;
    if (!cache_resources.bind_image(cache_mask_image, cache_mask_texture, 21) ||
        !execute_cache_plan() || cache_backend.effect_cache_keys.size() != 5 ||
        cache_backend.effect_cache_hits != 3 || cache_backend.effect_count != 5)
        return 33;

    LayoutSnapshot recolored = snapshot;
    for (auto &primitive : recolored.primitives) {
        if (primitive.kind == LayoutPrimitiveKind::Text)
            primitive.color = {0.9f, 0.2f, 0.1f, 1.0f};
    }
    if (!compiler.compile(recolored, main_target, 2.0f, frame, &compile_error, false,
                          engine.text_adapter()) ||
        engine.text_adapter()->layout_build_count() != layout_builds_after_rtl)
        return 13;

    const auto *text_adapter = frame.text_adapter();
    if (button_item->transform.tx != 10.0f || button_item->transform.ty != 6.0f)
        return 13;
    if (!compiler.compile(snapshot, main_target, 1.5f, frame, &compile_error, false,
                          engine.text_adapter()) ||
        frame.text_adapter() != text_adapter)
        return 15;
    if (!compiler.compile(snapshot, main_target, 1.5f, frame, &compile_error, true,
                          engine.text_adapter()) ||
        frame.plan().passes.size() != 1 || !frame.plan().passes.front().load_existing)
        return 16;

    LayoutSnapshot clipped;
    LayoutPrimitive clip_begin;
    clip_begin.kind = LayoutPrimitiveKind::ClipBegin;
    clip_begin.bounds = {10.0f, 20.0f, 100.0f, 80.0f};
    clip_begin.transform.tx = 5.0f;
    clip_begin.transform.ty = 6.0f;
    LayoutPrimitive rectangle;
    rectangle.kind = LayoutPrimitiveKind::Rectangle;
    rectangle.bounds = {0.0f, 0.0f, 200.0f, 200.0f};
    rectangle.transform.tx = 20.0f;
    rectangle.transform.ty = 30.0f;
    rectangle.color = {1.0f, 0.0f, 0.0f, 1.0f};
    LayoutPrimitive clip_end;
    clip_end.kind = LayoutPrimitiveKind::ClipEnd;
    clipped.primitives = {clip_begin, rectangle, clip_end};
    if (!compiler.compile(clipped, main_target, 1.5f, frame, &compile_error) ||
        frame.plan().passes.front().commands.size() != 1)
        return 16;
    const auto &clipped_command = frame.plan().passes.front().commands.front();
    if (!clipped_command.has_scissor || clipped_command.scissor_x != 22.5f ||
        clipped_command.scissor_y != 39.0f || clipped_command.scissor_width != 150.0f ||
        clipped_command.scissor_height != 120.0f)
        return 17;
    const auto *transformed_path = frame.resources().path(clipped_command.resource);
    if (!transformed_path ||
        transformed_path->path->operations()[transformed_path->operation_index].bounds[0] !=
            30.0f ||
        transformed_path->path->operations()[transformed_path->operation_index].bounds[1] != 45.0f)
        return 18;

    LayoutSnapshot hidden_snapshot;
    rectangle.visible = false;
    hidden_snapshot.primitives.push_back(rectangle);
    if (!compiler.compile(hidden_snapshot, main_target, 1.5f, frame, &compile_error) ||
        !frame.plan().passes.front().commands.empty())
        return 19;

    /*
     * Sealing owns the plan and every resource it references, so a sealed plan
     * keeps rendering after its builder is gone.
     */
    const ResourceId sealed_image = make_resource_id(ResourceKind::Image, 1, 900);
    auto sealed_texture = std::make_shared<PreparedTexture>();
    sealed_texture->width = 2;
    sealed_texture->height = 2;
    sealed_texture->pixels.assign(16, 0x7f);
    FrameResources sealed_resources;
    if (!sealed_resources.bind_image(sealed_image, sealed_texture))
        return 20;
    RenderPlan sealed_source;
    RenderPass sealed_pass;
    sealed_pass.target = main_target;
    RenderCommand sealed_command;
    sealed_command.kind = RenderCommandKind::Image;
    sealed_command.resource = sealed_image;
    sealed_command.width = 2.0f;
    sealed_command.height = 2.0f;
    sealed_pass.commands.push_back(sealed_command);
    sealed_source.passes.push_back(sealed_pass);

    RenderPlanSealError seal_error;
    std::shared_ptr<const SealedRenderPlan> sealed =
        SealedRenderPlan::seal(std::move(sealed_source), std::move(sealed_resources), &seal_error);
    if (!sealed || seal_error.message)
        return 21;
    /* Drop both builder-side references: the sealed plan owns them now. */
    sealed_texture.reset();
    sealed_source = RenderPlan{};
    RecordingRenderer sealed_backend;
    RenderExecutionError sealed_execution_error;
    if (!execute_render_plan(sealed_backend, *sealed, {main_target, frame_target},
                             &sealed_execution_error) ||
        sealed_backend.pass_count != 1 || sealed_backend.commit_count != 1)
        return 22;
    const auto *sealed_ref = sealed->resources().image(sealed_image);
    if (!sealed_ref || !sealed_ref->image || sealed_ref->image->pixels.size() != 16)
        return 23;
    /* Reference counting keeps the plan alive past the original handle. */
    std::shared_ptr<const SealedRenderPlan> kept = sealed;
    sealed.reset();
    if (!kept || kept->pass_count() != 1 || kept->resources().image(sealed_image) == nullptr)
        return 24;
    kept.reset();

    /* Borrowed resources cannot be sealed. */
    PreparedTexture borrowed_texture;
    borrowed_texture.width = borrowed_texture.height = 1;
    borrowed_texture.pixels = {1, 2, 3, 4};
    FrameResources borrowed_resources;
    RenderPlanSealError borrowed_error;
    if (!borrowed_resources.bind_image(sealed_image, borrowed_texture))
        return 25;
    if (SealedRenderPlan::seal(RenderPlan{}, std::move(borrowed_resources), &borrowed_error) ||
        !borrowed_error.message)
        return 26;

    /* A live result producer is a callback and cannot be sealed either. */
    MutableSurfaceProducer sealed_producer;
    const ResourceId sealed_producer_target = make_resource_id(ResourceKind::RenderTarget, 1, 901);
    FrameResources producer_resources;
    RenderPlanSealError producer_error;
    if (!producer_resources.bind_surface(sealed_producer_target, sealed_producer))
        return 27;
    if (SealedRenderPlan::seal(RenderPlan{}, std::move(producer_resources), &producer_error) ||
        !producer_error.message)
        return 28;

    std::cout << "PASS: layout snapshot compiles through NativeKit render plan\n";
    return 0;
#endif
}
