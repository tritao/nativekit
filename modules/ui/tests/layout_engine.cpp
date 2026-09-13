#include "layout/layout_engine.h"
#include "prepare/skribidi_adapter.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace nkui;

namespace {

LayoutNode box(uint32_t id, int32_t parent) {
    LayoutNode node;
    node.id = id;
    node.parent = parent;
    node.visual_kind = LayoutVisualKind::Box;
    node.style.width = {LayoutSizing::Grow, 0.0f};
    node.style.height = {LayoutSizing::Fit, 0.0f};
    return node;
}

LayoutNode text(uint32_t id, int32_t parent, const char *value) {
    LayoutNode node = box(id, parent);
    node.visual_kind = LayoutVisualKind::Text;
    node.text = value;
    node.text_style.font_size = 18.0f;
    node.text_color = {0.1f, 0.1f, 0.1f, 1.0f};
    return node;
}

} // namespace

int main(int argc, char **argv) {
#ifndef NKUI_TEST_FONT_PATH
    (void)argc;
    (void)argv;
    std::cerr << "NKUI_TEST_FONT_PATH is required\n";
    return 2;
#else
    LayoutEngine engine;
    if (!engine.valid() || !engine.add_font(NKUI_TEST_FONT_PATH))
        return 3;

    std::vector<LayoutNode> nodes;
    LayoutNode root = box(1, -1);
    root.style.width = {LayoutSizing::Fixed, 420.0f};
    root.style.height = {LayoutSizing::Fixed, 240.0f};
    root.style.padding_left = root.style.padding_right = 20;
    root.style.padding_top = root.style.padding_bottom = 16;
    root.style.child_gap = 12;
    root.style.background = {0.92f, 0.94f, 0.98f, 1.0f};
    nodes.push_back(root);

    nodes.push_back(text(2, 0,
                         "NativeKit layout delegates paragraph wrapping to Skribidi while Clay "
                         "keeps box constraints and geometry"));

    LayoutNode panel = box(3, 0);
    panel.style.width = {LayoutSizing::Fixed, 180.0f};
    panel.style.height = {LayoutSizing::Fit, 0.0f};
    panel.style.padding_left = panel.style.padding_right = 14;
    panel.style.padding_top = panel.style.padding_bottom = 9;
    panel.style.background = {0.2f, 0.55f, 0.9f, 1.0f};
    nodes.push_back(panel);
    nodes.push_back(text(4, 2, "Press me"));

    LayoutSnapshot snapshot;
    LayoutError error;
    if (!engine.layout(nodes, 420.0f, 240.0f, 1.0f / 60.0f, snapshot,
                       &error)) {
        std::cerr << (error.message ? error.message : "layout failed") << "\n";
        return 4;
    }

    const LayoutItem *panel_item = snapshot.find(3);
    const LayoutItem *title_item = snapshot.find(2);
    if (!panel_item || !title_item || panel_item->bounds.width != 180.0f ||
        panel_item->bounds.height <= 0.0f || title_item->bounds.width <= 0.0f)
        return 5;
    if (snapshot.primitives.size() < 3)
        return 6;
    const auto text_layout = std::find_if(
        snapshot.text_layouts.begin(), snapshot.text_layouts.end(),
        [](const LayoutTextLayout &layout) { return layout.node_id == 2; });
    if (text_layout == snapshot.text_layouts.end() || text_layout->id == 0 ||
        text_layout->lines.size() < 2)
        return 10;
    uint32_t title_line_count = 0;
    for (const auto &primitive : snapshot.primitives) {
        if (primitive.kind != LayoutPrimitiveKind::Text || primitive.node_id != 2)
            continue;
        if (primitive.text_layout_id != text_layout->id ||
            primitive.text_line_index != title_line_count)
            return 11;
        ++title_line_count;
    }
    if (title_line_count != text_layout->lines.size())
        return 12;
    const uint32_t stable_text_layout_builds = engine.text_adapter()->layout_build_count();
    if (!engine.layout(nodes, 420.0f, 240.0f, 1.0f / 60.0f, snapshot,
                       &error) ||
        engine.text_adapter()->layout_build_count() != stable_text_layout_builds)
        return 13;
    panel_item = snapshot.find(3);
    if (!panel_item || snapshot.items.size() != nodes.size())
        return 13;

    LayoutNode geometry_root = box(300, -1);
    geometry_root.style.width = {LayoutSizing::Fixed, 100.0f};
    geometry_root.style.height = {LayoutSizing::Fixed, 80.0f};
    geometry_root.style.clip_horizontal = true;
    geometry_root.style.clip_vertical = true;
    LayoutNode geometry_child = box(301, 0);
    geometry_child.style.width = {LayoutSizing::Fixed, 40.0f};
    geometry_child.style.height = {LayoutSizing::Fixed, 30.0f};
    geometry_child.style.transform.tx = 10.0f;
    geometry_child.style.transform.ty = 15.0f;
    LayoutNode hidden_child = box(302, 1);
    hidden_child.style.width = {LayoutSizing::Fixed, 8.0f};
    hidden_child.style.height = {LayoutSizing::Fixed, 8.0f};
    hidden_child.style.background = {1.0f, 0.0f, 0.0f, 1.0f};
    hidden_child.style.visible = false;
    std::vector<LayoutNode> geometry_nodes{geometry_root, geometry_child, hidden_child};
    if (!engine.layout(geometry_nodes, 100.0f, 80.0f, 1.0f / 60.0f, snapshot, &error))
        return 19;
    const LayoutItem *geometry_root_item = snapshot.find(300);
    const LayoutItem *geometry_child_item = snapshot.find(301);
    const LayoutItem *hidden_child_item = snapshot.find(302);
    if (!geometry_root_item || !geometry_child_item || !hidden_child_item ||
        geometry_root_item->content_bounds.width != 40.0f ||
        geometry_root_item->content_bounds.height != 30.0f ||
        geometry_child_item->transform.tx != 10.0f || geometry_child_item->transform.ty != 15.0f ||
        geometry_child_item->clip_bounds.width != 100.0f ||
        geometry_child_item->clip_bounds.height != 80.0f || hidden_child_item->visible)
        return 20;
    const auto hidden_primitive = std::find_if(
        snapshot.primitives.begin(), snapshot.primitives.end(),
        [](const LayoutPrimitive &primitive) { return primitive.node_id == 302; });
    if (hidden_primitive == snapshot.primitives.end() || hidden_primitive->visible)
        return 21;

    LayoutNode stack_root = box(400, -1);
    stack_root.style.width = {LayoutSizing::Fixed, 100.0f};
    stack_root.style.height = {LayoutSizing::Fixed, 80.0f};
    LayoutNode lower_layer = box(401, 0);
    lower_layer.style.positioning = LayoutPositioning::Absolute;
    lower_layer.style.position_x = 10.0f;
    lower_layer.style.position_y = 12.0f;
    lower_layer.style.width = {LayoutSizing::Fixed, 40.0f};
    lower_layer.style.height = {LayoutSizing::Fixed, 30.0f};
    lower_layer.style.z_index = 1;
    lower_layer.style.background = {1.0f, 0.0f, 0.0f, 1.0f};
    LayoutNode upper_layer = lower_layer;
    upper_layer.id = 402;
    upper_layer.style.position_x = 18.0f;
    upper_layer.style.position_y = 16.0f;
    upper_layer.style.z_index = 5;
    upper_layer.style.background = {0.0f, 0.0f, 1.0f, 1.0f};
    std::vector<LayoutNode> stack_nodes{stack_root, lower_layer, upper_layer};
    if (!engine.layout(stack_nodes, 100.0f, 80.0f, 1.0f / 60.0f, snapshot, &error))
        return 23;
    const auto lower_draw = std::find_if(
        snapshot.primitives.begin(), snapshot.primitives.end(),
        [](const LayoutPrimitive &primitive) {
            return primitive.node_id == 401 && primitive.kind == LayoutPrimitiveKind::Rectangle;
        });
    const auto upper_draw = std::find_if(
        snapshot.primitives.begin(), snapshot.primitives.end(),
        [](const LayoutPrimitive &primitive) {
            return primitive.node_id == 402 && primitive.kind == LayoutPrimitiveKind::Rectangle;
        });
    const auto *lower_item = snapshot.find(401);
    const auto *upper_item = snapshot.find(402);
    if (lower_draw == snapshot.primitives.end() || upper_draw == snapshot.primitives.end() ||
        lower_draw >= upper_draw || !lower_item || !upper_item ||
        lower_item->bounds.x != 10.0f || lower_item->bounds.y != 12.0f ||
        upper_item->bounds.x != 18.0f || upper_item->bounds.y != 16.0f)
        return 24;

    LayoutNode clip_root = box(410, -1);
    clip_root.style.width = {LayoutSizing::Fixed, 100.0f};
    clip_root.style.height = {LayoutSizing::Fixed, 80.0f};
    LayoutNode clip_parent = box(411, 0);
    clip_parent.style.width = {LayoutSizing::Fixed, 40.0f};
    clip_parent.style.height = {LayoutSizing::Fixed, 30.0f};
    LayoutNode clipped_layer = box(412, 1);
    clipped_layer.style.positioning = LayoutPositioning::Absolute;
    clipped_layer.style.position_x = 30.0f;
    clipped_layer.style.position_y = 20.0f;
    clipped_layer.style.width = {LayoutSizing::Fixed, 40.0f};
    clipped_layer.style.height = {LayoutSizing::Fixed, 30.0f};
    clipped_layer.style.clip_to_parent = true;
    std::vector<LayoutNode> clip_nodes{clip_root, clip_parent, clipped_layer};
    if (!engine.layout(clip_nodes, 100.0f, 80.0f, 1.0f / 60.0f, snapshot, &error))
        return 25;
    const auto *clipped_item = snapshot.find(412);
    if (!clipped_item || clipped_item->clip_bounds.x != 0.0f ||
        clipped_item->clip_bounds.y != 0.0f || clipped_item->clip_bounds.width != 40.0f ||
        clipped_item->clip_bounds.height != 30.0f)
        return 26;
    clip_nodes[2].style.clip_to_parent = false;
    if (!engine.layout(clip_nodes, 100.0f, 80.0f, 1.0f / 60.0f, snapshot, &error))
        return 27;
    clipped_item = snapshot.find(412);
    if (!clipped_item || clipped_item->clip_bounds.width != 100.0f ||
        clipped_item->clip_bounds.height != 80.0f)
        return 28;

    geometry_nodes[1].style.clip_horizontal = true;
    geometry_nodes[1].style.transform.b = 1.0f;
    geometry_nodes[1].style.transform.c = -1.0f;
    if (engine.layout(geometry_nodes, 100.0f, 80.0f, 1.0f / 60.0f, snapshot, &error) ||
        !error.message || std::string(error.message) != "rotated or skewed clipping is not supported")
        return 22;

    constexpr std::size_t text_layout_cache_limit = 128;
    LayoutNode crowded_root = box(200, -1);
    crowded_root.style.width = {LayoutSizing::Fixed, 420.0f};
    crowded_root.style.height = {LayoutSizing::Fixed, 240.0f};
    std::vector<LayoutNode> crowded_nodes{crowded_root};
    for (std::size_t index = 0; index < text_layout_cache_limit + 2; ++index)
        crowded_nodes.push_back(
            text(static_cast<uint32_t>(201 + index), 0,
                 ("visible paragraph " + std::to_string(index)).c_str()));
    if (!engine.layout(crowded_nodes, 420.0f, 240.0f, 1.0f / 60.0f,
                       snapshot, &error) ||
        snapshot.text_layouts.size() != text_layout_cache_limit + 2)
        return 14;
    for (const auto &layout : snapshot.text_layouts)
        if (!engine.text_adapter()->has_layout(layout.id))
            return 15;

    std::vector<TextLayoutId> churned_layouts;
    churned_layouts.reserve(text_layout_cache_limit + 16);
    LayoutNode cache_root = box(100, -1);
    cache_root.style.width = {LayoutSizing::Fixed, 420.0f};
    cache_root.style.height = {LayoutSizing::Fixed, 240.0f};
    LayoutNode cache_text = text(101, 0, "cache paragraph 0");
    std::vector<LayoutNode> cache_nodes{cache_root, cache_text};
    for (std::size_t index = 0; index < text_layout_cache_limit + 16; ++index) {
        cache_nodes[1].text = "cache paragraph " + std::to_string(index);
        const float width = 300.0f + static_cast<float>(index);
        cache_nodes[0].style.width = {LayoutSizing::Fixed, width};
        if (!engine.layout(cache_nodes, 600.0f, 240.0f, 1.0f / 60.0f,
                           snapshot, &error) ||
            snapshot.text_layouts.size() != 1)
            return 16;
        churned_layouts.push_back(snapshot.text_layouts.front().id);
        if (!engine.text_adapter()->has_layout(churned_layouts.back()))
            return 17;
    }
    for (std::size_t index = 0; index < churned_layouts.size(); ++index) {
        const bool should_be_retained = index >= 16;
        if (engine.text_adapter()->has_layout(churned_layouts[index]) != should_be_retained)
            return 18;
    }

    std::cout << "PASS: Clay layout boxes, text, transforms, and geometry\n";
    return 0;
#endif
}
