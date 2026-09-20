#include "layout/layout_engine.h"
#include "layout/layout_render_compiler.h"
#include "render/render_plan_executor.h"
#include "render/sealed_render_plan.h"
#include "render/ui_renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_map>
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
    bool beginFrame(bool record, const nk_surface_frame_target *) override {
        ++frame_count;
        recorded_frame = record;
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
    bool beginEffectPass(ResourceId, uint64_t cache_key, int width, int height,
                         bool &cache_hit) override {
        cache_hit = cached_pass(effect_cache_keys, effect_cache_dimensions, cache_key, width,
                                height);
        if (cache_hit)
            ++effect_cache_hits;
        ++pass_count;
        return true;
    }
    bool beginRasterPass(ResourceId, uint64_t cache_key, int width, int height,
                         bool &cache_hit) override {
        cache_hit = cached_pass(raster_cache_keys, raster_cache_dimensions, cache_key, width,
                                height);
        if (cache_hit)
            ++raster_cache_hits;
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
        ++image_count;
        return true;
    }
    bool drawBoxShadow(float, float, float, float, const float[6], float,
                       const BoxShadowDescriptor &) override {
        ++box_shadow_count;
        return true;
    }
    bool uploadAtlases(TextEngine &, bool) override { return true; }
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

  private:
    static bool cached_pass(std::unordered_set<uint64_t> &keys,
                            std::unordered_map<uint64_t, std::array<int, 2>> &dimensions,
                            uint64_t cache_key, int width, int height) {
        if (!cache_key)
            return false;
        const auto found = dimensions.find(cache_key);
        const bool hit = found != dimensions.end() && found->second == std::array<int, 2>{width, height};
        keys.insert(cache_key);
        dimensions[cache_key] = {width, height};
        return hit;
    }

  public:
    uint32_t frame_count = 0;
    bool recorded_frame = false;
    uint32_t pass_count = 0;
    uint32_t path_count = 0;
    uint32_t text_count = 0;
    uint32_t effect_count = 0;
    uint32_t mask_count = 0;
    uint32_t surface_mesh_count = 0;
    uint32_t box_shadow_count = 0;
    uint32_t image_count = 0;
    uint32_t commit_count = 0;
    uint32_t effect_cache_hits = 0;
    uint32_t raster_cache_hits = 0;
    std::unordered_set<uint64_t> effect_cache_keys;
    std::unordered_set<uint64_t> raster_cache_keys;
    std::unordered_map<uint64_t, std::array<int, 2>> effect_cache_dimensions;
    std::unordered_map<uint64_t, std::array<int, 2>> raster_cache_dimensions;
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
    bool recordable() const override { return true; }
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
    auto shared_fonts = std::make_shared<FontCollection>();
    if (!shared_fonts->valid() || !shared_fonts->add_font(NKUI_TEST_FONT_PATH) ||
        shared_fonts->font_load_count() != 1)
        return 3;
    TextEngine direct_layout(shared_fonts);
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
    const uint32_t layout_builds = engine.text_engine()->layout_build_count();
    if (!compiler.compile(snapshot, main_target, 1.5f, frame, &compile_error, false,
                          engine.text_engine())) {
        std::cerr << (compile_error.message ? compile_error.message : "compile failed") << "\n";
        return 6;
    }
    if (engine.text_engine()->layout_build_count() != layout_builds ||
        frame.text_engine() != engine.text_engine() || shared_fonts->font_load_count() != 1)
        return 6;
    if (frame.plan().passes.size() != 1 || frame.plan().passes.front().commands.size() < 3 ||
        !frame.text_engine())
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
            engine.text_engine()->selection_rects({0, 0}, {rtl_lengths[rtl_index], 0});
        if (selection.empty() ||
            std::abs(selection.front().x - rtl_layout->lines.front().bounds.x) > 0.01f)
            return 14;

        LayoutRenderFrame rtl_frame;
        if (!compiler.compile(rtl_snapshot, main_target, 1.0f, rtl_frame, &compile_error, false,
                              engine.text_engine()))
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
    const uint32_t layout_builds_after_rtl = engine.text_engine()->layout_build_count();

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
    custom_marker.content_revision = 1;
    custom_marker.bounds = button_item->bounds;
    custom_marker.transform = button_item->transform;
    ordered_snapshot.primitives.insert(label_primitive, custom_marker);
    RenderPlan custom_plan;
    custom_plan.passes.push_back({main_target, {}, false, {}});
    const ResourceId custom_path = make_resource_id(ResourceKind::Path, 1, 444);
    RenderCommand custom_command{RenderCommandKind::Path, custom_path};
    custom_command.transform = {2.0f, 0.0f, 0.0f, 2.0f, 10.0f, 20.0f};
    custom_command.has_scissor = true;
    // Custom display-list commands and scissors are node-local; embedding
    // applies the resolved node placement.
    custom_command.scissor_x = 26.0f;
    custom_command.scissor_y = 18.0f;
    custom_command.scissor_width = 3.0f;
    custom_command.scissor_height = 4.0f;
    custom_plan.passes.front().commands.push_back(custom_command);
    LayoutRenderCompiler::CustomPaintPlans custom_paints{{2, &custom_plan}};
    LayoutRenderFrame ordered_frame;
    if (!compiler.compile(ordered_snapshot, main_target, 1.5f, ordered_frame, &compile_error, false,
                          engine.text_engine(), &custom_paints))
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
        custom_position->transform != std::array<float, 6>{3.0f, 0.0f, 0.0f, 3.0f, 54.0f, 57.0f} ||
        custom_position->scissor_x != 78.0f || custom_position->scissor_y != 54.0f ||
        custom_position->scissor_width != 4.5f || custom_position->scissor_height != 6.0f)
        return 22;

    // Framework-owned layer metadata wraps retained custom pixels in a
    // separate target. The draw plan remains free of the outer layer, so a
    // composite-only revision can reuse its content target.
    DisplayList composite_metadata;
    EffectDescriptor composite_blur;
    composite_blur.kind = EffectKind::Blur;
    composite_blur.color_matrix[0] = 2.0f;
    MaskDescriptor composite_mask;
    composite_mask.kind = MaskKind::RoundedRect;
    composite_mask.values[0] = 3.0f;
    const LayerBounds composite_bounds{0.0f, 0.0f, button_item->bounds.width,
                                      button_item->bounds.height};
    if (!composite_metadata.begin_layer(0.5f, composite_bounds, composite_blur, composite_mask) ||
        !composite_metadata.end_layer())
        return 220;
    LayoutRenderCompiler::CustomPaintComposites composite_paints{{2, &composite_metadata}};
    LayoutRenderFrame split_frame;
    if (!compiler.compile(ordered_snapshot, main_target, 1.5f, split_frame, &compile_error, false,
                          engine.text_engine(), &custom_paints, nullptr, &composite_paints)) {
        std::cerr << "split compile failed: "
                  << (compile_error.message ? compile_error.message : "unknown") << "\n";
        return 221;
    }
    const auto content_pass = std::find_if(
        split_frame.plan().passes.begin(), split_frame.plan().passes.end(),
        [custom_path, main_target](const RenderPass &pass) {
            return pass.target.value != main_target.value &&
                   std::any_of(pass.commands.begin(), pass.commands.end(),
                               [custom_path](const RenderCommand &command) {
                                   return command.kind == RenderCommandKind::Path &&
                                          command.resource.value == custom_path.value &&
                                          command.custom_payload;
                               });
        });
    if (content_pass == split_frame.plan().passes.end() ||
        split_frame.plan().isolated_layers == 0 || split_frame.plan().dependencies.size() < 2 ||
        std::none_of(split_frame.plan().passes.begin(), split_frame.plan().passes.end(),
                     [](const RenderPass &pass) { return pass.kind == RenderPassKind::Effect; }) ||
        std::none_of(split_frame.plan().passes.begin(), split_frame.plan().passes.end(),
                     [](const RenderPass &pass) { return pass.kind == RenderPassKind::Mask; }))
        return 222;
    const auto wrapped_content = std::find_if(
        split_frame.plan().passes.begin(), split_frame.plan().passes.end(),
        [content_target = content_pass->target](const RenderPass &pass) {
            return std::any_of(pass.commands.begin(), pass.commands.end(),
                               [content_target](const RenderCommand &command) {
                                   return command.kind == RenderCommandKind::CompositeTarget &&
                                          command.resource.value == content_target.value &&
                                          command.custom_payload;
                               });
        });
    const auto final_composite = std::find_if(
        split_frame.plan().passes.front().commands.begin(),
        split_frame.plan().passes.front().commands.end(), [](const RenderCommand &command) {
            return command.kind == RenderCommandKind::CompositeTarget && command.custom_payload;
        });
    if (wrapped_content == split_frame.plan().passes.end() ||
        final_composite == split_frame.plan().passes.front().commands.end())
        return 223;
    LayoutRenderCompiler::RasterPaintNodes split_raster_paints{2};
    LayoutRenderFrame split_raster_frame;
    if (!compiler.compile(ordered_snapshot, main_target, 1.5f, split_raster_frame, &compile_error,
                          false, engine.text_engine(), &custom_paints, &split_raster_paints,
                          &composite_paints))
        return 224;
    if (std::none_of(split_raster_frame.plan().passes.begin(),
                     split_raster_frame.plan().passes.end(), [](const RenderPass &pass) {
                         return pass.kind == RenderPassKind::Raster &&
                                std::any_of(pass.commands.begin(), pass.commands.end(),
                                            [](const RenderCommand &command) {
                                                return command.custom_payload;
                                            });
                     }))
        return 225;

    // A style decoration may target an ordinary box without changing its
    // native layout visual kind. Its retained paint joins immediately after
    // that box's own primitive instead of being silently dropped.
    LayoutRenderCompiler::CustomPaintPlans decorated_paints{{2, &custom_plan}};
    LayoutRenderFrame decorated_frame;
    if (!compiler.compile(snapshot, main_target, 1.5f, decorated_frame, &compile_error, false,
                          engine.text_engine(), &decorated_paints))
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

    LayoutRenderCompiler::RasterPaintNodes raster_paints{2};
    LayoutRenderFrame raster_frame;
    if (!compiler.compile(ordered_snapshot, main_target, 1.5f, raster_frame, &compile_error, false,
                          engine.text_engine(), &custom_paints, &raster_paints) ||
        raster_frame.plan().passes.size() != 3 ||
        raster_frame.plan().passes[1].kind != RenderPassKind::Raster ||
        raster_frame.plan().passes[1].commands.size() < 2 ||
        std::none_of(raster_frame.plan().passes[1].commands.begin(),
                     raster_frame.plan().passes[1].commands.end(),
                     [](const RenderCommand &command) { return command.custom_payload; }) ||
        raster_frame.plan().passes[2].commands.empty() ||
        raster_frame.plan().passes[2].commands.front().kind != RenderCommandKind::CompositeTarget ||
        raster_frame.plan().dependencies.size() != 1)
        return 25;

    // A cache policy on an ordinary layout node captures its native rectangle
    // and text descendants in local coordinates, so translating the subtree
    // changes only the main-pass composite transform.
    LayoutRenderCompiler::RasterPaintNodes subtree_paints{1};
    LayoutRenderFrame subtree_frame;
    if (!compiler.compile(snapshot, main_target, 1.5f, subtree_frame, &compile_error, false,
                          engine.text_engine(), nullptr, &subtree_paints) ||
        subtree_frame.plan().passes.size() != 3 ||
        subtree_frame.plan().passes[1].kind != RenderPassKind::Raster ||
        subtree_frame.plan().passes[1].commands.size() < 2 ||
        subtree_frame.plan().passes[1].target_descriptor.logical_width <= 0.0f ||
        subtree_frame.plan().passes[2].commands.size() != 1 ||
        subtree_frame.plan().passes[2].commands.front().kind != RenderCommandKind::CompositeTarget)
        return 26;

    // Nested raster policies collapse to the outermost cache boundary. The
    // child must not create a second cache target or invalidate independently.
    LayoutRenderCompiler::RasterPaintNodes nested_raster_paints{1, 2};
    LayoutRenderFrame nested_policy_frame;
    if (!compiler.compile(snapshot, main_target, 1.5f, nested_policy_frame, &compile_error, false,
                          engine.text_engine(), nullptr, &nested_raster_paints) ||
        std::count_if(nested_policy_frame.plan().passes.begin(),
                      nested_policy_frame.plan().passes.end(), [](const RenderPass &pass) {
                          return pass.kind == RenderPassKind::Raster;
                      }) != 1)
        return 126;

    std::vector<LayoutNode> moved_nodes = nodes;
    moved_nodes[0].style.transform.tx = 48.0f;
    moved_nodes[0].style.transform.ty = 24.0f;
    LayoutSnapshot moved_snapshot;
    if (!engine.layout(moved_nodes, 320.0f, 200.0f, 1.0f / 60.0f, moved_snapshot,
                       &layout_error))
        return 27;
    LayoutRenderFrame moved_subtree_frame;
    if (!compiler.compile(moved_snapshot, main_target, 1.5f, moved_subtree_frame, &compile_error,
                          false, engine.text_engine(), nullptr, &subtree_paints))
        return 27;

    // Custom paint descendants are embedded into the parent's raster target.
    // Their local display list remains unchanged while native placement moves
    // it with the layout snapshot.
    LayoutSnapshot moved_ordered_snapshot = moved_snapshot;
    const auto moved_label_primitive = std::find_if(
        moved_ordered_snapshot.primitives.begin(), moved_ordered_snapshot.primitives.end(),
        [](const LayoutPrimitive &primitive) { return primitive.kind == LayoutPrimitiveKind::Text; });
    const LayoutItem *moved_button_item = moved_ordered_snapshot.find(2);
    if (moved_label_primitive == moved_ordered_snapshot.primitives.end() || !moved_button_item)
        return 28;
    LayoutPrimitive moved_custom_marker = custom_marker;
    moved_custom_marker.bounds = moved_button_item->bounds;
    moved_custom_marker.transform = moved_button_item->transform;
    moved_ordered_snapshot.primitives.insert(moved_label_primitive, moved_custom_marker);
    RenderPlan moved_custom_plan = custom_plan;
    LayoutRenderCompiler::CustomPaintPlans moved_custom_paints{{2, &moved_custom_plan}};
    LayoutRenderFrame mixed_frame;
    LayoutRenderFrame moved_mixed_frame;
    if (!compiler.compile(ordered_snapshot, main_target, 1.5f, mixed_frame, &compile_error, false,
                          engine.text_engine(), &custom_paints, &raster_paints) ||
        !compiler.compile(moved_ordered_snapshot, main_target, 1.5f, moved_mixed_frame,
                          &compile_error, false, engine.text_engine(), &moved_custom_paints,
                          &raster_paints) ||
        mixed_frame.plan().passes.size() != 3 || moved_mixed_frame.plan().passes.size() != 3)
        return 28;
    const auto mixed_custom = std::find_if(
        mixed_frame.plan().passes[1].commands.begin(), mixed_frame.plan().passes[1].commands.end(),
        [](const RenderCommand &command) { return command.custom_payload; });
    const auto moved_mixed_custom = std::find_if(
        moved_mixed_frame.plan().passes[1].commands.begin(),
        moved_mixed_frame.plan().passes[1].commands.end(),
        [](const RenderCommand &command) { return command.custom_payload; });
    if (mixed_custom == mixed_frame.plan().passes[1].commands.end() ||
        moved_mixed_custom == moved_mixed_frame.plan().passes[1].commands.end())
        return 28;
    for (std::size_t component = 0; component < mixed_custom->transform.size(); ++component)
        if (std::abs(mixed_custom->transform[component] -
                     moved_mixed_custom->transform[component]) > 0.001f)
            return 29;
    if (std::abs(mixed_custom->scissor_x - moved_mixed_custom->scissor_x) > 0.001f ||
        std::abs(mixed_custom->scissor_y - moved_mixed_custom->scissor_y) > 0.001f ||
        std::abs(mixed_custom->scissor_width - moved_mixed_custom->scissor_width) > 0.001f ||
        std::abs(mixed_custom->scissor_height - moved_mixed_custom->scissor_height) > 0.001f)
        return 29;

    NanoVGPath mixed_path;
    mixed_path.move_to(0.0f, 0.0f);
    mixed_path.line_to(12.0f, 0.0f);
    mixed_path.line_to(12.0f, 8.0f);
    mixed_path.line_to(0.0f, 8.0f);
    mixed_path.close();
    PreparedGeometry mixed_geometry;
    PathPreparationParams mixed_path_params;
    PreparedPaint mixed_paint{};
    mixed_paint.transform[0] = mixed_paint.transform[3] = 1.0f;
    mixed_paint.feather = 1.0f;
    mixed_paint.inner_color = {1.0f, 0.0f, 0.0f, 1.0f};
    mixed_paint.outer_color = mixed_paint.inner_color;
    PreparedPath mixed_prepared;
    if (!mixed_path.valid() || !prepare_fill(mixed_path, mixed_path_params, mixed_geometry) ||
        !mixed_prepared.set(PreparedPathKind::Fill, mixed_geometry, mixed_paint))
        return 30;

    RecordingRenderer subtree_backend;
    if (!mixed_frame.resources().bind_path(custom_path, mixed_prepared, 0, 1) ||
        !moved_mixed_frame.resources().bind_path(custom_path, mixed_prepared, 0, 1))
        return 30;
    nk_surface_frame_target subtree_frame_target{};
    subtree_frame_target.struct_size = sizeof(subtree_frame_target);
    subtree_frame_target.width = 480;
    subtree_frame_target.height = 300;
    RenderExecutionError subtree_execution_error;
    if (!execute_render_plan(subtree_backend, subtree_frame.plan(), subtree_frame.resources(),
                             {main_target, subtree_frame_target}, &subtree_execution_error) ||
        !execute_render_plan(subtree_backend, moved_subtree_frame.plan(),
                             moved_subtree_frame.resources(),
                             {main_target, subtree_frame_target}, &subtree_execution_error) ||
        subtree_backend.raster_cache_hits != 1)
        return 127;

    // Explicit content revisions participate in raster identity, while a
    // composite-only revision leaves the cached subtree pixels reusable.
    std::vector<LayoutNode> content_changed_nodes = nodes;
    content_changed_nodes[0].content_revision = 7;
    LayoutSnapshot content_changed_snapshot;
    if (!engine.layout(content_changed_nodes, 320.0f, 200.0f, 1.0f / 60.0f,
                       content_changed_snapshot, &layout_error))
        return 128;
    LayoutRenderFrame content_changed_subtree_frame;
    if (!compiler.compile(content_changed_snapshot, main_target, 1.5f,
                          content_changed_subtree_frame, &compile_error, false,
                          engine.text_engine(), nullptr, &subtree_paints))
        return 128;
    RecordingRenderer revision_backend;
    if (!execute_render_plan(revision_backend, subtree_frame.plan(), subtree_frame.resources(),
                             {main_target, subtree_frame_target}, &subtree_execution_error) ||
        !execute_render_plan(revision_backend, moved_subtree_frame.plan(),
                             moved_subtree_frame.resources(),
                             {main_target, subtree_frame_target}, &subtree_execution_error) ||
        !execute_render_plan(revision_backend, content_changed_subtree_frame.plan(),
                             content_changed_subtree_frame.resources(),
                             {main_target, subtree_frame_target}, &subtree_execution_error) ||
        revision_backend.raster_cache_hits != 1)
        return 128;

    // Custom display-list plans use the owning layout item's retained content
    // revision even when the display-list command stream itself is unchanged.
    LayoutSnapshot custom_content_snapshot = ordered_snapshot;
    const auto custom_content_item = std::find_if(
        custom_content_snapshot.items.begin(), custom_content_snapshot.items.end(),
        [](const LayoutItem &item) { return item.id == 2; });
    if (custom_content_item == custom_content_snapshot.items.end())
        return 130;
    custom_content_item->content_revision = 2;
    LayoutRenderFrame custom_content_frame;
    if (!compiler.compile(custom_content_snapshot, main_target, 1.5f, custom_content_frame,
                          &compile_error, false, engine.text_engine(), &custom_paints,
                          &raster_paints) ||
        !custom_content_frame.resources().bind_path(custom_path, mixed_prepared, 0, 1))
        return 130;
    RecordingRenderer custom_revision_backend;
    if (!mixed_frame.resources().bind_path(custom_path, mixed_prepared, 0, 1) ||
        !execute_render_plan(custom_revision_backend, mixed_frame.plan(), mixed_frame.resources(),
                             {main_target, subtree_frame_target}, &subtree_execution_error) ||
        !execute_render_plan(custom_revision_backend, custom_content_frame.plan(),
                             custom_content_frame.resources(),
                             {main_target, subtree_frame_target}, &subtree_execution_error) ||
        custom_revision_backend.raster_cache_hits != 0)
        return 130;

    std::vector<LayoutNode> composite_changed_nodes = nodes;
    composite_changed_nodes[0].composite_revision = 9;
    LayoutSnapshot composite_changed_snapshot;
    if (!engine.layout(composite_changed_nodes, 320.0f, 200.0f, 1.0f / 60.0f,
                       composite_changed_snapshot, &layout_error))
        return 129;
    LayoutRenderFrame composite_changed_subtree_frame;
    if (!compiler.compile(composite_changed_snapshot, main_target, 1.5f,
                          composite_changed_subtree_frame, &compile_error, false,
                          engine.text_engine(), nullptr, &subtree_paints) ||
        !execute_render_plan(revision_backend, composite_changed_subtree_frame.plan(),
                             composite_changed_subtree_frame.resources(),
                             {main_target, subtree_frame_target}, &subtree_execution_error) ||
        revision_backend.raster_cache_hits != 2)
        return 129;

    // Raster entries are keyed by content, but the renderer must also reject
    // a cached surface when its physical dimensions change (resize/DPR).
    auto &subtree_raster_pass = subtree_frame.plan().passes[1];
    subtree_raster_pass.target_descriptor.width =
        std::max(1, static_cast<int>(std::ceil(subtree_raster_pass.target_descriptor.logical_width *
                                               2.0f)));
    subtree_raster_pass.target_descriptor.height =
        std::max(1, static_cast<int>(std::ceil(subtree_raster_pass.target_descriptor.logical_height *
                                               2.0f)));
    if (!execute_render_plan(subtree_backend, subtree_frame.plan(), subtree_frame.resources(),
                             {main_target, subtree_frame_target}, &subtree_execution_error) ||
        subtree_backend.raster_cache_hits != 1 ||
        !execute_render_plan(subtree_backend, subtree_frame.plan(), subtree_frame.resources(),
                             {main_target, subtree_frame_target}, &subtree_execution_error) ||
        subtree_backend.raster_cache_hits != 2)
        return 127;
    RecordingRenderer mixed_backend;
    if (!execute_render_plan(mixed_backend, mixed_frame.plan(), mixed_frame.resources(),
                             {main_target, subtree_frame_target}, &subtree_execution_error) ||
        !execute_render_plan(mixed_backend, moved_mixed_frame.plan(), moved_mixed_frame.resources(),
                             {main_target, subtree_frame_target}, &subtree_execution_error) ||
        mixed_backend.raster_cache_hits != 1)
        return 30;
    RenderPlan changed_custom_plan = moved_custom_plan;
    changed_custom_plan.passes.front().commands.front().content_generation = 2;
    LayoutRenderCompiler::CustomPaintPlans changed_custom_paints{{2, &changed_custom_plan}};
    LayoutRenderFrame changed_mixed_frame;
    if (!compiler.compile(moved_ordered_snapshot, main_target, 1.5f, changed_mixed_frame,
                          &compile_error, false, engine.text_engine(), &changed_custom_paints,
                          &raster_paints) ||
        !changed_mixed_frame.resources().bind_path(custom_path, mixed_prepared, 0, 1) ||
        !execute_render_plan(mixed_backend, changed_mixed_frame.plan(),
                             changed_mixed_frame.resources(),
                             {main_target, subtree_frame_target}, &subtree_execution_error) ||
        mixed_backend.raster_cache_hits != 1)
        return 31;

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
    nk_surface_frame_target nested_frame_target{};
    nested_frame_target.struct_size = sizeof(nested_frame_target);
    nested_frame_target.width = 480;
    nested_frame_target.height = 300;
    RenderExecutionError nested_execution_error;
    LayoutRenderFrame nested_mixed_frame;
    if (!compiler.compile(ordered_snapshot, main_target, 1.5f, nested_mixed_frame, &compile_error,
                          false, engine.text_engine(), &bounded_paints, &raster_paints) ||
        nested_mixed_frame.plan().passes.size() != 4 ||
        nested_mixed_frame.plan().passes[1].kind != RenderPassKind::Raster ||
        nested_mixed_frame.plan().passes[2].kind != RenderPassKind::Draw ||
        nested_mixed_frame.plan().dependencies.size() != 2)
        return 32;
    RenderPlan moved_bounded_custom_plan = bounded_custom_plan;
    LayoutRenderCompiler::CustomPaintPlans moved_bounded_paints{{2,
                                                                 &moved_bounded_custom_plan}};
    LayoutRenderFrame moved_nested_mixed_frame;
    if (!compiler.compile(moved_ordered_snapshot, main_target, 1.5f, moved_nested_mixed_frame,
                          &compile_error, false, engine.text_engine(), &moved_bounded_paints,
                          &raster_paints) ||
        moved_nested_mixed_frame.plan().passes.size() != 4)
        return 32;
    if (!nested_mixed_frame.resources().bind_path(custom_path, mixed_prepared, 0, 1) ||
        !moved_nested_mixed_frame.resources().bind_path(custom_path, mixed_prepared, 0, 1))
        return 32;
    RecordingRenderer nested_mixed_backend;
    if (!execute_render_plan(nested_mixed_backend, nested_mixed_frame.plan(),
                             nested_mixed_frame.resources(),
                             {main_target, nested_frame_target}, &nested_execution_error) ||
        !execute_render_plan(nested_mixed_backend, moved_nested_mixed_frame.plan(),
                             moved_nested_mixed_frame.resources(),
                             {main_target, nested_frame_target}, &nested_execution_error) ||
        nested_mixed_backend.raster_cache_hits != 1)
        return 32;

    // An embedded cached subtree may itself contain an effect and a mask.
    // Their intermediate targets stay local to the cache, and changes to
    // either descriptor must invalidate the enclosing raster entry.
    const ResourceId embedded_effect_input = make_resource_id(ResourceKind::RenderTarget, 1, 480);
    const ResourceId embedded_effect_output = make_resource_id(ResourceKind::RenderTarget, 1, 481);
    const ResourceId embedded_mask_output = make_resource_id(ResourceKind::RenderTarget, 1, 482);
    RenderPlan effect_mask_plan;
    effect_mask_plan.passes.push_back({main_target, {}, false, {}});
    RenderCommand effect_mask_composite{RenderCommandKind::CompositeTarget,
                                        embedded_mask_output, 0.0f, 0.0f, 20.0f, 10.0f};
    effect_mask_plan.passes.front().commands.push_back(effect_mask_composite);
    RenderPass embedded_input_pass;
    embedded_input_pass.target = embedded_effect_input;
    embedded_input_pass.target_descriptor.logical_width = 20.0f;
    embedded_input_pass.target_descriptor.logical_height = 10.0f;
    embedded_input_pass.commands.push_back({RenderCommandKind::Path, custom_path});
    effect_mask_plan.passes.push_back(std::move(embedded_input_pass));
    RenderPass embedded_effect_pass;
    embedded_effect_pass.target = embedded_effect_output;
    embedded_effect_pass.target_descriptor.logical_width = 20.0f;
    embedded_effect_pass.target_descriptor.logical_height = 10.0f;
    embedded_effect_pass.kind = RenderPassKind::Effect;
    embedded_effect_pass.input_target = embedded_effect_input;
    embedded_effect_pass.effect.kind = EffectKind::ColorMatrix;
    embedded_effect_pass.effect.color_matrix[0] = 1.0f;
    embedded_effect_pass.effect.color_matrix[6] = 1.0f;
    embedded_effect_pass.effect.color_matrix[12] = 1.0f;
    embedded_effect_pass.effect.color_matrix[18] = 1.0f;
    effect_mask_plan.passes.push_back(std::move(embedded_effect_pass));
    RenderPass embedded_mask_pass;
    embedded_mask_pass.target = embedded_mask_output;
    embedded_mask_pass.target_descriptor.logical_width = 20.0f;
    embedded_mask_pass.target_descriptor.logical_height = 10.0f;
    embedded_mask_pass.kind = RenderPassKind::Mask;
    embedded_mask_pass.input_target = embedded_effect_output;
    embedded_mask_pass.mask.kind = MaskKind::RoundedRect;
    embedded_mask_pass.mask.values[0] = 3.0f;
    effect_mask_plan.passes.push_back(std::move(embedded_mask_pass));
    effect_mask_plan.dependencies.push_back({embedded_mask_output, main_target});
    LayoutRenderCompiler::CustomPaintPlans effect_mask_paints{{2, &effect_mask_plan}};
    LayoutRenderFrame effect_mask_frame;
    if (!compiler.compile(ordered_snapshot, main_target, 1.5f, effect_mask_frame, &compile_error,
                          false, engine.text_engine(), &effect_mask_paints, &raster_paints) ||
        effect_mask_frame.plan().passes.size() != 6 ||
        effect_mask_frame.plan().passes[2].kind != RenderPassKind::Draw ||
        effect_mask_frame.plan().passes[3].kind != RenderPassKind::Effect ||
        effect_mask_frame.plan().passes[4].kind != RenderPassKind::Mask)
        return 132;
    if (!effect_mask_frame.resources().bind_path(custom_path, mixed_prepared, 0, 1))
        return 133;
    RecordingRenderer effect_mask_backend;
    const bool effect_mask_first = execute_render_plan(
        effect_mask_backend, effect_mask_frame.plan(), effect_mask_frame.resources(),
        {main_target, nested_frame_target}, &nested_execution_error);
    const bool effect_mask_second = effect_mask_first && execute_render_plan(
                                                       effect_mask_backend,
                                                       effect_mask_frame.plan(),
                                                       effect_mask_frame.resources(),
                                                       {main_target, nested_frame_target},
                                                       &nested_execution_error);
    if (!effect_mask_first || !effect_mask_second || effect_mask_backend.effect_count != 1 ||
        effect_mask_backend.mask_count != 2 || effect_mask_backend.raster_cache_hits != 1)
        return 134;
    RenderPlan changed_effect_mask_plan = effect_mask_plan;
    changed_effect_mask_plan.passes[2].effect.color_matrix[0] = 0.75f;
    LayoutRenderCompiler::CustomPaintPlans changed_effect_mask_paints{{2,
                                                                         &changed_effect_mask_plan}};
    LayoutRenderFrame changed_effect_mask_frame;
    if (!compiler.compile(ordered_snapshot, main_target, 1.5f, changed_effect_mask_frame,
                          &compile_error, false, engine.text_engine(),
                          &changed_effect_mask_paints, &raster_paints) ||
        !changed_effect_mask_frame.resources().bind_path(custom_path, mixed_prepared, 0, 1) ||
        !execute_render_plan(effect_mask_backend, changed_effect_mask_frame.plan(),
                             changed_effect_mask_frame.resources(),
                             {main_target, nested_frame_target}, &nested_execution_error) ||
        effect_mask_backend.effect_count != 2 || effect_mask_backend.mask_count != 3 ||
        effect_mask_backend.raster_cache_hits != 1)
        return 135;

    LayoutRenderFrame bounded_frame;
    if (!compiler.compile(ordered_snapshot, main_target, 1.5f, bounded_frame, &compile_error, false,
                          engine.text_engine(), &bounded_paints) ||
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
        bounded_pass.target_descriptor.origin_x != 28.0f ||
        bounded_pass.target_descriptor.origin_y != 21.0f ||
        bounded_pass.commands.front().transform !=
            std::array<float, 6>{1.5f, 0.0f, 0.0f, 1.5f, -9.0f, 18.0f} ||
        bounded_pass.commands.front().scissor_x != -33.0f ||
        bounded_pass.commands.front().scissor_y != -19.5f ||
        bounded_pass.commands.front().scissor_width != 9.0f ||
        bounded_pass.commands.front().scissor_height != 10.5f ||
        bounded_composite == bounded_frame.plan().passes.front().commands.end() ||
        bounded_composite->transform != std::array<float, 6>{1.5f, 0.0f, 0.0f, 1.5f, 39.0f, 27.0f})
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
                          false, engine.text_engine(), &scaled_effect_paints) ||
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
    /* Plans without a live producer are recorded into a sealed batch. */
    if (!backend.recorded_frame)
        return 65;

    /* Live, non-recordable producers cannot cross the render boundary. Their
       replacement is a retained graphics-image binding; recordable producers
       remain valid because they encode entirely through the supplied renderer. */
    {
        struct TestProducer final : SurfaceProducer {
            bool ready() const override { return true; }
            bool describe(int, int, SurfaceDescriptor &description) const override {
                description.width = 4;
                description.height = 4;
                description.format = SurfacePixelFormat::Rgba8;
                description.alpha = SurfaceAlphaMode::Premultiplied;
                description.filter = SurfaceFilter::Nearest;
                description.color_space = SurfaceColorSpace::Linear;
                return true;
            }
            uint32_t generation() const override { return 1; }
            SurfaceRenderResult render(UiRenderer &, ResourceId,
                                       const SurfaceDescriptor &) override {
                return SurfaceRenderResult::Rendered;
            }
        } producer;
        FrameResources producer_resources;
        RenderPlan producer_plan;
        producer_plan.passes.push_back({main_target, {}, false, {}});
        const ResourceId producer_target = make_resource_id(ResourceKind::RenderTarget, 1, 448);
        producer_plan.dependencies.push_back({producer_target, main_target});
        if (!producer_resources.bind_surface(producer_target, producer))
            return 66;
        RecordingRenderer producer_backend;
        if (execute_render_plan(producer_backend, producer_plan, producer_resources,
                                {main_target, frame_target}, &execution_error) ||
            producer_backend.recorded_frame || !execution_error.message ||
            std::strcmp(
                execution_error.message,
                "surface producer must publish a retained graphics image or be recordable") != 0)
            return 67;
    }

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

    RenderPlan raster_cache_plan;
    const ResourceId raster_cache_target = make_resource_id(ResourceKind::RenderTarget, 1, 470);
    RenderPass raster_cache_pass;
    raster_cache_pass.target = raster_cache_target;
    raster_cache_pass.kind = RenderPassKind::Raster;
    RenderCommand raster_image_command;
    raster_image_command.kind = RenderCommandKind::Image;
    raster_image_command.resource = cache_source_image;
    raster_image_command.width = raster_image_command.height = 10.0f;
    raster_cache_pass.commands.push_back(raster_image_command);
    raster_cache_plan.passes.push_back(std::move(raster_cache_pass));
    RenderPass raster_cache_main;
    raster_cache_main.target = cache_main;
    raster_cache_main.commands.push_back(
        {RenderCommandKind::CompositeTarget, raster_cache_target, 0.0f, 0.0f, 10.0f, 10.0f});
    raster_cache_plan.passes.push_back(std::move(raster_cache_main));
    raster_cache_plan.dependencies.push_back({raster_cache_target, cache_main});
    RecordingRenderer raster_cache_backend;
    auto execute_raster_cache_plan = [&] {
        return execute_render_plan(raster_cache_backend, raster_cache_plan, cache_resources,
                                   {cache_main, frame_target}, &execution_error);
    };
    if (!execute_raster_cache_plan() || !execute_raster_cache_plan() ||
        raster_cache_backend.raster_cache_hits != 1 || raster_cache_backend.image_count != 1)
        return 34;
    if (!cache_resources.bind_image(cache_source_image, cache_source_texture, 12) ||
        !execute_raster_cache_plan() || raster_cache_backend.raster_cache_hits != 1 ||
        raster_cache_backend.image_count != 2)
        return 35;

    LayoutSnapshot recolored = snapshot;
    for (auto &primitive : recolored.primitives) {
        if (primitive.kind == LayoutPrimitiveKind::Text)
            primitive.color = {0.9f, 0.2f, 0.1f, 1.0f};
    }
    if (!compiler.compile(recolored, main_target, 2.0f, frame, &compile_error, false,
                          engine.text_engine()) ||
        engine.text_engine()->layout_build_count() != layout_builds_after_rtl)
        return 13;

    const auto *text_engine = frame.text_engine();
    if (button_item->transform.tx != 10.0f || button_item->transform.ty != 6.0f)
        return 13;
    if (!compiler.compile(snapshot, main_target, 1.5f, frame, &compile_error, false,
                          engine.text_engine()) ||
        frame.text_engine() != text_engine)
        return 15;
    if (!compiler.compile(snapshot, main_target, 1.5f, frame, &compile_error, true,
                          engine.text_engine()) ||
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
    OwnedFrameResources sealed_resources;
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

    /* Sealing must reject an unowned external render-target dependency rather
       than allowing a live surface producer to cross the RENDER boundary. */
    RenderPlan unowned_source;
    RenderPass unowned_pass;
    unowned_pass.target = main_target;
    unowned_source.passes.push_back(unowned_pass);
    const ResourceId unowned_target = make_resource_id(ResourceKind::RenderTarget, 1, 901);
    unowned_source.dependencies.push_back({unowned_target, main_target});
    RenderPlanSealError unowned_error;
    if (SealedRenderPlan::seal(std::move(unowned_source), OwnedFrameResources{}, &unowned_error) ||
        !unowned_error.message)
        return 65;

    /*
     * An owned set shares prepared data instead of copying it, so a second
     * sealed plan over the same texture does not duplicate pixels and both
     * stay valid after the caller drops its handle.
     */
    auto shared_texture = std::make_shared<PreparedTexture>();
    shared_texture->width = shared_texture->height = 1;
    shared_texture->pixels = {9, 8, 7, 6};
    OwnedFrameResources shared_one;
    OwnedFrameResources shared_two;
    if (!shared_one.bind_image(sealed_image, shared_texture) ||
        !shared_two.bind_image(sealed_image, shared_texture))
        return 25;
    shared_texture.reset();
    RenderPlanSealError shared_error;
    auto first_sealed = SealedRenderPlan::seal(RenderPlan{}, std::move(shared_one), &shared_error);
    auto second_sealed = SealedRenderPlan::seal(RenderPlan{}, std::move(shared_two), &shared_error);
    if (!first_sealed || !second_sealed || shared_error.message)
        return 26;
    const auto *shared_ref = first_sealed->resources().image(sealed_image);
    if (!shared_ref || !shared_ref->image || shared_ref->image->pixels[2] != 7)
        return 27;

    /*
     * The layout-session render path seals exactly this: the compiler's owned
     * bindings. Prove they survive the builder frame and its text adapter.
     */
    std::shared_ptr<const SealedRenderPlan> session_sealed;
    {
        LayoutRenderFrame builder_frame;
        LayoutSnapshot seal_snapshot;
        rectangle.visible = true;
        seal_snapshot.primitives.push_back(rectangle);
        if (!compiler.compile(seal_snapshot, main_target, 1.5f, builder_frame, &compile_error))
            return 61;
        if (!builder_frame.sealable())
            return 64;
        RenderPlanSealError session_seal_error;
        session_sealed = SealedRenderPlan::seal(
            RenderPlan(builder_frame.plan()), OwnedFrameResources(builder_frame.owned_resources()),
            &session_seal_error);
        if (!session_sealed || session_seal_error.message)
            return 62;
    }
    RecordingRenderer session_backend;
    RenderExecutionError session_execution_error;
    if (!execute_render_plan(session_backend, *session_sealed, {main_target, frame_target},
                             &session_execution_error) ||
        session_backend.pass_count == 0 || session_backend.path_count == 0)
        return 63;

    std::cout << "PASS: layout snapshot compiles through NativeKit render plan\n";
    return 0;
#endif
}
