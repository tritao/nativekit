#include "layout/layout_engine.h"
#include "layout/layout_render_compiler.h"
#include "render/render_plan_executor.h"

#include <array>
#include <iostream>
#include <string>
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

class RecordingBackend final : public RenderBackend {
  public:
    bool initialize() override { return true; }
    bool valid() const override { return true; }

    bool begin_window_pass(int, int, const nk_surface_frame_target &, bool) override {
        ++pass_count;
        return true;
    }
    bool begin_target_pass(ResourceId, int, int, bool) override {
        ++pass_count;
        return true;
    }
    bool begin_surface_pass(ResourceId, const SurfaceDescriptor &, bool) override {
        ++pass_count;
        return true;
    }
    bool surface_has_content(ResourceId) const override { return false; }
    bool surface_is_current(ResourceId, uint32_t, const SurfaceDescriptor &) const override {
        return false;
    }
    void mark_surface_current(ResourceId, uint32_t, const SurfaceDescriptor &) override {}

    bool set_scissor(bool enabled, float x, float y, float width, float height) override {
        last_scissor_enabled = enabled;
        last_scissor = {x, y, width, height};
        return true;
    }
    bool draw_path(const PreparedPathData &, uint32_t, float) override {
        ++path_count;
        return true;
    }
    bool draw_path_transformed(const PreparedPathData &path, uint32_t operation_index,
                               const float[6], float) override {
        if (operation_index >= path.operations().size())
            return false;
        ++path_count;
        return true;
    }
    bool draw_paths(const PreparedPathData &) override {
        ++path_count;
        return true;
    }
    bool draw_image(const PreparedTexture &, float, float, float, float, const float[6],
                    float) override {
        return true;
    }
    bool upload_atlases(SkribidiAdapter &, bool) override { return true; }
    bool draw_glyphs(const PreparedGlyphs &, float) override {
        ++text_count;
        return true;
    }
    bool draw_glyphs_transformed(const PreparedGlyphs &glyphs, const float[6], float, float,
                                 float) override {
        if (glyphs.vertices.empty())
            return false;
        ++text_count;
        return true;
    }
    bool draw_target(ResourceId, float, float, float, float, float) override { return true; }
    bool end_pass() override { return true; }
    bool commit_frame() override {
        ++commit_count;
        return true;
    }
    bool end_frame() override { return true; }
    RenderBackendStats stats() const override { return {}; }
    const char *last_error() const override { return error.c_str(); }

    uint32_t pass_count = 0;
    uint32_t path_count = 0;
    uint32_t text_count = 0;
    uint32_t commit_count = 0;
    bool last_scissor_enabled = false;
    std::array<float, 4> last_scissor{};
    std::string error;
};

} // namespace

int main() {
#ifndef NKUI_TEST_FONT_PATH
    std::cerr << "NKUI_TEST_FONT_PATH is required\n";
    return 2;
#else
    LayoutEngine engine;
    if (!engine.valid() || !engine.add_font(NKUI_TEST_FONT_PATH))
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
    button.kind = LayoutNodeKind::Button;
    button.style.width = {LayoutSizing::Fixed, 160.0f};
    button.style.height = {LayoutSizing::Fit, 0.0f};
    button.style.padding_left = button.style.padding_right = 12;
    button.style.padding_top = button.style.padding_bottom = 8;
    button.style.background = {0.2f, 0.5f, 0.9f, 1.0f};
    button.style.radius_top_left = button.style.radius_top_right = 8.0f;
    button.style.radius_bottom_left = button.style.radius_bottom_right = 8.0f;
    nodes.push_back(button);

    LayoutNode label = box(3, 1);
    label.kind = LayoutNodeKind::Text;
    label.text = "Compile me";
    label.font_size = 18;
    label.text_color = {0.1f, 0.15f, 0.25f, 1.0f};
    nodes.push_back(label);

    LayoutSnapshot snapshot;
    LayoutError layout_error;
    if (!engine.layout(nodes, 320.0f, 200.0f, 0.0f, 0.0f, false, 1.0f / 60.0f, snapshot,
                       &layout_error))
        return 4;

    LayoutRenderCompiler compiler;
    if (!compiler.add_font(NKUI_TEST_FONT_PATH))
        return 5;
    const ResourceId main_target = make_resource_id(ResourceKind::RenderTarget, 1, 1);
    LayoutRenderFrame frame;
    LayoutRenderCompileError compile_error;
    if (!compiler.compile(snapshot, main_target, 1.5f, frame, &compile_error)) {
        std::cerr << (compile_error.message ? compile_error.message : "compile failed") << "\n";
        return 6;
    }
    if (frame.plan().passes.size() != 1 || frame.plan().passes.front().commands.size() < 3 ||
        !frame.text_adapter())
        return 7;

    uint32_t path_commands = 0;
    uint32_t text_commands = 0;
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
            if (text->vertices.front().red != 26 || text->vertices.front().green != 38 ||
                text->vertices.front().blue != 64)
                return 10;
            ++text_commands;
        }
    }
    if (path_commands < 2 || text_commands != 1)
        return 11;

    RecordingBackend backend;
    nk_surface_frame_target frame_target{};
    frame_target.struct_size = sizeof(frame_target);
    frame_target.api = NK_GRAPHICS_OPENGL;
    frame_target.width = 480;
    frame_target.height = 300;
    RenderExecutionError execution_error;
    if (!execute_render_plan(backend, frame.plan(), frame.resources(),
                             {main_target, frame_target}, &execution_error) ||
        backend.pass_count != 1 || backend.path_count != path_commands ||
        backend.text_count != text_commands || backend.commit_count != 1)
        return 12;

    const auto *text_adapter = frame.text_adapter();
    const LayoutItem *button_item = snapshot.find(2);
    if (!button_item)
        return 13;
    const float click_x = button_item->bounds.x + button_item->bounds.width * 0.5f;
    const float click_y = button_item->bounds.y + button_item->bounds.height * 0.5f;
    if (!engine.layout(nodes, 320.0f, 200.0f, click_x, click_y, true, 1.0f / 60.0f, snapshot,
                       &layout_error) ||
        !engine.layout(nodes, 320.0f, 200.0f, click_x, click_y, false, 1.0f / 60.0f, snapshot,
                       &layout_error) ||
        snapshot.events.size() != 1 || snapshot.events.front().node_id != 2)
        return 14;
    if (!compiler.compile(snapshot, main_target, 1.5f, frame, &compile_error) ||
        frame.text_adapter() != text_adapter)
        return 15;

    LayoutSnapshot clipped;
    LayoutPrimitive clip_begin;
    clip_begin.kind = LayoutPrimitiveKind::ClipBegin;
    clip_begin.bounds = {10.0f, 20.0f, 100.0f, 80.0f};
    LayoutPrimitive rectangle;
    rectangle.kind = LayoutPrimitiveKind::Rectangle;
    rectangle.bounds = {0.0f, 0.0f, 200.0f, 200.0f};
    rectangle.color = {1.0f, 0.0f, 0.0f, 1.0f};
    LayoutPrimitive clip_end;
    clip_end.kind = LayoutPrimitiveKind::ClipEnd;
    clipped.primitives = {clip_begin, rectangle, clip_end};
    if (!compiler.compile(clipped, main_target, 1.5f, frame, &compile_error) ||
        frame.plan().passes.front().commands.size() != 1)
        return 16;
    const auto &clipped_command = frame.plan().passes.front().commands.front();
    if (!clipped_command.has_scissor || clipped_command.scissor_x != 15.0f ||
        clipped_command.scissor_y != 30.0f || clipped_command.scissor_width != 150.0f ||
        clipped_command.scissor_height != 120.0f)
        return 17;

    std::cout << "PASS: layout snapshot compiles through NativeKit render plan\n";
    return 0;
#endif
}
