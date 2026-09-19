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
    if (!engine.layout(nodes, 420.0f, 240.0f, 1.0f / 60.0f, snapshot, &error)) {
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
    const auto text_layout =
        std::find_if(snapshot.text_layouts.begin(), snapshot.text_layouts.end(),
                     [](const LayoutTextLayout &layout) { return layout.node_id == 2; });
    if (text_layout == snapshot.text_layouts.end() || text_layout->id == 0 ||
        text_layout->lines.size() < 2 || !text_layout->has_baseline ||
        !std::isfinite(text_layout->first_line_baseline))
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
    if (!engine.layout(nodes, 420.0f, 240.0f, 1.0f / 60.0f, snapshot, &error) ||
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

    geometry_nodes[1].style.transform = {0.0f, 1.0f, -1.0f, 0.0f, 0.0f, 0.0f};
    if (!engine.layout(geometry_nodes, 100.0f, 80.0f, 1.0f / 60.0f, snapshot, &error))
        return 23;
    geometry_child_item = snapshot.find(301);
    if (!geometry_child_item || std::abs(geometry_child_item->transform.tx - 35.0f) > 0.0001f ||
        std::abs(geometry_child_item->transform.ty + 5.0f) > 0.0001f)
        return 24;

    const auto hidden_primitive =
        std::find_if(snapshot.primitives.begin(), snapshot.primitives.end(),
                     [](const LayoutPrimitive &primitive) { return primitive.node_id == 302; });
    if (hidden_primitive == snapshot.primitives.end() || hidden_primitive->visible)
        return 21;

    LayoutNode scroll_root = box(320, -1);
    scroll_root.style.width = {LayoutSizing::Fixed, 100.0f};
    scroll_root.style.height = {LayoutSizing::Fixed, 80.0f};
    scroll_root.style.clip_vertical = true;
    LayoutNode scroll_content = box(321, 0);
    scroll_content.style.width = {LayoutSizing::Grow, 0.0f};
    scroll_content.style.height = {LayoutSizing::Fixed, 13280.0f};
    scroll_content.style.transform.ty = -13248.0f;
    LayoutNode scroll_spacer = box(322, 1);
    scroll_spacer.style.width = {LayoutSizing::Grow, 0.0f};
    scroll_spacer.style.height = {LayoutSizing::Fixed, 13248.0f};
    LayoutNode scroll_row = box(323, 1);
    scroll_row.style.width = {LayoutSizing::Grow, 0.0f};
    scroll_row.style.height = {LayoutSizing::Fixed, 32.0f};
    scroll_row.style.background = {0.2f, 0.55f, 0.9f, 1.0f};
    std::vector<LayoutNode> scroll_nodes{scroll_root, scroll_content, scroll_spacer, scroll_row};
    if (!engine.layout(scroll_nodes, 100.0f, 80.0f, 1.0f / 60.0f, snapshot, &error))
        return 49;
    const auto scrolled_row_primitive = std::find_if(
        snapshot.primitives.begin(), snapshot.primitives.end(),
        [](const LayoutPrimitive &primitive) {
            return primitive.node_id == 323 && primitive.kind == LayoutPrimitiveKind::Rectangle;
        });
    if (scrolled_row_primitive == snapshot.primitives.end() ||
        scrolled_row_primitive->transform.ty != -13248.0f)
        return 50;

    LayoutNode collapsed_root = box(310, -1);
    collapsed_root.style.width = {LayoutSizing::Fixed, 100.0f};
    collapsed_root.style.height = {LayoutSizing::Fixed, 80.0f};
    LayoutNode collapsed_pane = box(311, 0);
    collapsed_pane.style.width = {LayoutSizing::Percent, 0.0f};
    collapsed_pane.style.height = {LayoutSizing::Grow, 0.0f};
    collapsed_pane.style.visible = false;
    LayoutNode collapsed_text = text(312, 1, "Collapsed pane text");
    std::vector<LayoutNode> collapsed_nodes{collapsed_root, collapsed_pane, collapsed_text};
    if (!engine.layout(collapsed_nodes, 100.0f, 80.0f, 1.0f / 60.0f, snapshot, &error))
        return 47;
    const auto *collapsed_pane_item = snapshot.find(311);
    const auto *collapsed_text_item = snapshot.find(312);
    if (!collapsed_pane_item || !collapsed_text_item || collapsed_pane_item->visible ||
        collapsed_text_item->visible || collapsed_pane_item->bounds.width != 0.0f ||
        collapsed_text_item->bounds.width != 0.0f)
        return 48;

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
        lower_draw >= upper_draw || !lower_item || !upper_item || lower_item->bounds.x != 10.0f ||
        lower_item->bounds.y != 12.0f || upper_item->bounds.x != 18.0f ||
        upper_item->bounds.y != 16.0f)
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
        !error.message ||
        std::string(error.message) != "rotated or skewed clipping is not supported")
        return 22;

    constexpr std::size_t text_layout_cache_limit = 128;
    LayoutNode crowded_root = box(200, -1);
    crowded_root.style.width = {LayoutSizing::Fixed, 420.0f};
    crowded_root.style.height = {LayoutSizing::Fixed, 240.0f};
    std::vector<LayoutNode> crowded_nodes{crowded_root};
    for (std::size_t index = 0; index < text_layout_cache_limit + 2; ++index)
        crowded_nodes.push_back(text(static_cast<uint32_t>(201 + index), 0,
                                     ("visible paragraph " + std::to_string(index)).c_str()));
    if (!engine.layout(crowded_nodes, 420.0f, 240.0f, 1.0f / 60.0f, snapshot, &error) ||
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
        if (!engine.layout(cache_nodes, 600.0f, 240.0f, 1.0f / 60.0f, snapshot, &error) ||
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

    LayoutNode constraint_root = box(500, -1);
    constraint_root.style.width = {LayoutSizing::Fixed, 220.0f};
    constraint_root.style.height = {LayoutSizing::Fixed, 100.0f};
    LayoutNode bounded = box(501, 0);
    bounded.style.width = {LayoutSizing::Fit, 0.0f};
    bounded.style.width.min = 48.0f;
    bounded.style.width.max = 90.0f;
    bounded.style.height = {LayoutSizing::Fixed, 20.0f};
    LayoutNode aspect = box(502, 0);
    aspect.style.width = {LayoutSizing::Fixed, 80.0f};
    aspect.style.height = {LayoutSizing::Fit, 0.0f};
    aspect.style.aspect_ratio = 2.0f;
    std::vector<LayoutNode> constraint_nodes{constraint_root, bounded, aspect};
    if (!engine.layout(constraint_nodes, 220.0f, 100.0f, 1.0f / 60.0f, snapshot, &error))
        return 29;
    const auto *bounded_item = snapshot.find(501);
    const auto *aspect_item = snapshot.find(502);
    if (!bounded_item || bounded_item->bounds.width < 48.0f || bounded_item->bounds.width > 90.0f ||
        !aspect_item || std::abs(aspect_item->bounds.width - 80.0f) > 0.01f ||
        std::abs(aspect_item->bounds.height - 40.0f) > 0.01f)
        return 30;

    LayoutNode weighted_root = box(600, -1);
    weighted_root.style.width = {LayoutSizing::Fixed, 500.0f};
    weighted_root.style.height = {LayoutSizing::Fixed, 80.0f};
    weighted_root.style.direction = LayoutDirection::LeftToRight;
    LayoutNode weighted_first = box(601, 0);
    weighted_first.style.width = {LayoutSizing::Grow, 0.0f, 0.0f, 0.0f, 1.0f};
    weighted_first.style.height = {LayoutSizing::Fixed, 20.0f};
    LayoutNode weighted_second = box(602, 0);
    weighted_second.style.width = {LayoutSizing::Grow, 0.0f, 0.0f, 0.0f, 4.0f};
    weighted_second.style.height = {LayoutSizing::Fixed, 20.0f};
    std::vector<LayoutNode> weighted_nodes{weighted_root, weighted_first, weighted_second};
    if (!engine.layout(weighted_nodes, 500.0f, 80.0f, 1.0f / 60.0f, snapshot, &error))
        return 31;
    const auto *weighted_first_item = snapshot.find(601);
    const auto *weighted_second_item = snapshot.find(602);
    if (!weighted_first_item || !weighted_second_item ||
        std::abs(weighted_first_item->bounds.width - 100.0f) > 0.01f ||
        std::abs(weighted_second_item->bounds.width - 400.0f) > 0.01f)
        return 32;

    LayoutNode capped_root = box(610, -1);
    capped_root.style.width = {LayoutSizing::Fixed, 400.0f};
    capped_root.style.height = {LayoutSizing::Fixed, 80.0f};
    capped_root.style.direction = LayoutDirection::LeftToRight;
    LayoutNode capped_first = box(611, 0);
    capped_first.style.width = {LayoutSizing::Grow, 0.0f, 0.0f, 100.0f, 1.0f};
    capped_first.style.height = {LayoutSizing::Fixed, 20.0f};
    LayoutNode capped_second = box(612, 0);
    capped_second.style.width = {LayoutSizing::Grow, 0.0f, 0.0f, 0.0f, 3.0f};
    capped_second.style.height = {LayoutSizing::Fixed, 20.0f};
    std::vector<LayoutNode> capped_nodes{capped_root, capped_first, capped_second};
    if (!engine.layout(capped_nodes, 400.0f, 80.0f, 1.0f / 60.0f, snapshot, &error))
        return 33;
    const auto *capped_first_item = snapshot.find(611);
    const auto *capped_second_item = snapshot.find(612);
    if (!capped_first_item || !capped_second_item ||
        std::abs(capped_first_item->bounds.width - 100.0f) > 0.01f ||
        std::abs(capped_second_item->bounds.width - 300.0f) > 0.01f)
        return 34;

    const float expected_distribution_positions[][3] = {
        {0.0f, 30.0f, 60.0f},     // start
        {60.0f, 90.0f, 120.0f},   // center
        {120.0f, 150.0f, 180.0f}, // end
        {0.0f, 90.0f, 180.0f},    // space-between
        {20.0f, 90.0f, 160.0f},   // space-around
        {30.0f, 90.0f, 150.0f},   // space-evenly
    };
    for (int distribution = 0; distribution <= 5; ++distribution) {
        LayoutNode distribution_root = box(700, -1);
        distribution_root.style.width = {LayoutSizing::Fixed, 200.0f};
        distribution_root.style.height = {LayoutSizing::Fixed, 40.0f};
        distribution_root.style.direction = LayoutDirection::LeftToRight;
        distribution_root.style.child_gap = 10;
        distribution_root.style.child_distribution = static_cast<LayoutDistribution>(distribution);
        std::vector<LayoutNode> distribution_nodes{distribution_root};
        for (int child = 0; child < 3; ++child) {
            LayoutNode item = box(701 + child, 0);
            item.style.width = {LayoutSizing::Fixed, 20.0f};
            item.style.height = {LayoutSizing::Fixed, 20.0f};
            distribution_nodes.push_back(item);
        }
        if (!engine.layout(distribution_nodes, 200.0f, 40.0f, 1.0f / 60.0f, snapshot, &error))
            return 35;
        for (int child = 0; child < 3; ++child) {
            const auto *item = snapshot.find(701 + child);
            if (!item ||
                std::abs(item->bounds.x - expected_distribution_positions[distribution][child]) >
                    0.01f ||
                std::abs(item->bounds.y) > 0.01f) {
                std::cerr << "distribution " << distribution << " child " << child;
                if (item)
                    std::cerr << " got " << item->bounds.x << "," << item->bounds.y;
                std::cerr << " expected " << expected_distribution_positions[distribution][child]
                          << "\n";
                return 36;
            }
        }
    }

    LayoutNode vertical_distribution_root = box(710, -1);
    vertical_distribution_root.style.width = {LayoutSizing::Fixed, 40.0f};
    vertical_distribution_root.style.height = {LayoutSizing::Fixed, 200.0f};
    vertical_distribution_root.style.child_gap = 10;
    vertical_distribution_root.style.child_distribution = LayoutDistribution::SpaceEvenly;
    std::vector<LayoutNode> vertical_distribution_nodes{vertical_distribution_root};
    for (int child = 0; child < 3; ++child) {
        LayoutNode item = box(711 + child, 0);
        item.style.width = {LayoutSizing::Fixed, 20.0f};
        item.style.height = {LayoutSizing::Fixed, 20.0f};
        vertical_distribution_nodes.push_back(item);
    }
    if (!engine.layout(vertical_distribution_nodes, 40.0f, 200.0f, 1.0f / 60.0f, snapshot, &error))
        return 37;
    for (int child = 0; child < 3; ++child) {
        const auto *item = snapshot.find(711 + child);
        if (!item || std::abs(item->bounds.x) > 0.01f ||
            std::abs(item->bounds.y - (30.0f + child * 60.0f)) > 0.01f)
            return 38;
    }

    LayoutNode baseline_root = box(720, -1);
    baseline_root.style.width = {LayoutSizing::Fixed, 320.0f};
    baseline_root.style.height = {LayoutSizing::Fixed, 80.0f};
    baseline_root.style.direction = LayoutDirection::LeftToRight;
    baseline_root.style.child_align_y = LayoutAlignmentY::Baseline;
    LayoutNode baseline_text = text(721, 0, "Settings");
    baseline_text.style.width = {LayoutSizing::Fit, 0.0f};
    baseline_text.style.height = {LayoutSizing::Fit, 0.0f};
    LayoutNode baseline_control = box(722, 0);
    baseline_control.style.width = {LayoutSizing::Fixed, 32.0f};
    baseline_control.style.height = {LayoutSizing::Fixed, 32.0f};
    std::vector<LayoutNode> baseline_nodes{baseline_root, baseline_text, baseline_control};
    if (!engine.layout(baseline_nodes, 320.0f, 80.0f, 1.0f / 60.0f, snapshot, &error))
        return 39;
    const auto *baseline_text_item = snapshot.find(721);
    const auto *baseline_control_item = snapshot.find(722);
    if (!baseline_text_item || !baseline_control_item || !baseline_text_item->has_baseline ||
        baseline_control_item->has_baseline ||
        std::abs(baseline_text_item->baseline -
                 (baseline_control_item->bounds.y + baseline_control_item->bounds.height)) > 0.01f)
        return 40;

    LayoutNode wrapped_row_root = box(730, -1);
    wrapped_row_root.style.width = {LayoutSizing::Fixed, 100.0f};
    wrapped_row_root.style.height = {LayoutSizing::Fit, 0.0f};
    wrapped_row_root.style.direction = LayoutDirection::LeftToRight;
    wrapped_row_root.style.wrap_mode = LayoutWrapMode::Wrap;
    wrapped_row_root.style.row_gap = 8.5f;
    wrapped_row_root.style.column_gap = 5.25f;
    LayoutNode wrapped_row_first = box(731, 0);
    wrapped_row_first.style.width = {LayoutSizing::Fixed, 60.0f};
    wrapped_row_first.style.height = {LayoutSizing::Fixed, 20.0f};
    LayoutNode wrapped_row_second = box(732, 0);
    wrapped_row_second.style.width = {LayoutSizing::Fixed, 30.0f};
    wrapped_row_second.style.height = {LayoutSizing::Fixed, 10.0f};
    LayoutNode wrapped_row_third = box(733, 0);
    wrapped_row_third.style.width = {LayoutSizing::Fixed, 50.0f};
    wrapped_row_third.style.height = {LayoutSizing::Fixed, 12.0f};
    std::vector<LayoutNode> wrapped_row_nodes{wrapped_row_root, wrapped_row_first,
                                              wrapped_row_second, wrapped_row_third};
    if (!engine.layout(wrapped_row_nodes, 100.0f, 80.0f, 1.0f / 60.0f, snapshot, &error))
        return 41;
    const auto *wrapped_row_root_item = snapshot.find(730);
    const auto *wrapped_row_first_item = snapshot.find(731);
    const auto *wrapped_row_second_item = snapshot.find(732);
    const auto *wrapped_row_third_item = snapshot.find(733);
    if (!wrapped_row_root_item || !wrapped_row_first_item || !wrapped_row_second_item ||
        !wrapped_row_third_item || std::abs(wrapped_row_root_item->bounds.height - 40.5f) > 0.01f ||
        std::abs(wrapped_row_first_item->bounds.x) > 0.01f ||
        std::abs(wrapped_row_second_item->bounds.x - 65.25f) > 0.01f ||
        std::abs(wrapped_row_third_item->bounds.x) > 0.01f ||
        std::abs(wrapped_row_third_item->bounds.y - 28.5f) > 0.01f)
        return 42;

    LayoutNode wrapped_grow_root = box(735, -1);
    wrapped_grow_root.style.width = {LayoutSizing::Fixed, 120.0f};
    wrapped_grow_root.style.height = {LayoutSizing::Fit, 0.0f};
    wrapped_grow_root.style.direction = LayoutDirection::LeftToRight;
    wrapped_grow_root.style.wrap_mode = LayoutWrapMode::Wrap;
    wrapped_grow_root.style.row_gap = 5.5f;
    wrapped_grow_root.style.column_gap = 5.25f;
    LayoutNode wrapped_grow_fixed = box(736, 0);
    wrapped_grow_fixed.style.width = {LayoutSizing::Fixed, 70.0f};
    wrapped_grow_fixed.style.height = {LayoutSizing::Fixed, 10.0f};
    LayoutNode wrapped_grow_sibling = box(737, 0);
    wrapped_grow_sibling.style.width = {LayoutSizing::Fixed, 30.0f};
    wrapped_grow_sibling.style.height = {LayoutSizing::Fixed, 10.0f};
    LayoutNode wrapped_grow_child = box(738, 0);
    wrapped_grow_child.style.width = {LayoutSizing::Grow, 0.0f, 30.0f, 0.0f, 1.0f};
    wrapped_grow_child.style.height = {LayoutSizing::Fixed, 10.0f};
    std::vector<LayoutNode> wrapped_grow_nodes{wrapped_grow_root, wrapped_grow_fixed,
                                               wrapped_grow_sibling, wrapped_grow_child};
    if (!engine.layout(wrapped_grow_nodes, 120.0f, 80.0f, 1.0f / 60.0f, snapshot, &error))
        return 45;
    const auto *wrapped_grow_root_item = snapshot.find(735);
    const auto *wrapped_grow_child_item = snapshot.find(738);
    if (!wrapped_grow_root_item || !wrapped_grow_child_item ||
        std::abs(wrapped_grow_root_item->bounds.height - 25.5f) > 0.01f ||
        std::abs(wrapped_grow_child_item->bounds.x) > 0.01f ||
        std::abs(wrapped_grow_child_item->bounds.y - 15.5f) > 0.01f ||
        std::abs(wrapped_grow_child_item->bounds.width - 120.0f) > 0.01f) {
        return 46;
    }

    LayoutNode wrapped_column_root = box(740, -1);
    wrapped_column_root.style.width = {LayoutSizing::Fit, 0.0f};
    wrapped_column_root.style.height = {LayoutSizing::Fixed, 60.0f};
    wrapped_column_root.style.direction = LayoutDirection::TopToBottom;
    wrapped_column_root.style.wrap_mode = LayoutWrapMode::Wrap;
    wrapped_column_root.style.row_gap = 4.5f;
    wrapped_column_root.style.column_gap = 7.25f;
    LayoutNode wrapped_column_first = box(741, 0);
    wrapped_column_first.style.width = {LayoutSizing::Fixed, 10.0f};
    wrapped_column_first.style.height = {LayoutSizing::Fixed, 35.0f};
    LayoutNode wrapped_column_second = box(742, 0);
    wrapped_column_second.style.width = {LayoutSizing::Fixed, 20.0f};
    wrapped_column_second.style.height = {LayoutSizing::Fixed, 20.0f};
    LayoutNode wrapped_column_third = box(743, 0);
    wrapped_column_third.style.width = {LayoutSizing::Fixed, 30.0f};
    wrapped_column_third.style.height = {LayoutSizing::Fixed, 30.0f};
    std::vector<LayoutNode> wrapped_column_nodes{wrapped_column_root, wrapped_column_first,
                                                 wrapped_column_second, wrapped_column_third};
    if (!engine.layout(wrapped_column_nodes, 80.0f, 60.0f, 1.0f / 60.0f, snapshot, &error))
        return 43;
    const auto *wrapped_column_root_item = snapshot.find(740);
    const auto *wrapped_column_first_item = snapshot.find(741);
    const auto *wrapped_column_second_item = snapshot.find(742);
    const auto *wrapped_column_third_item = snapshot.find(743);
    if (!wrapped_column_root_item || !wrapped_column_first_item || !wrapped_column_second_item ||
        !wrapped_column_third_item ||
        std::abs(wrapped_column_root_item->bounds.width - 57.25f) > 0.01f ||
        std::abs(wrapped_column_second_item->bounds.y - 39.5f) > 0.01f ||
        std::abs(wrapped_column_third_item->bounds.x - 27.25f) > 0.01f ||
        std::abs(wrapped_column_third_item->bounds.y) > 0.01f)
        return 44;

    LayoutNode align_self_row_root = box(745, -1);
    align_self_row_root.style.width = {LayoutSizing::Fixed, 100.0f};
    align_self_row_root.style.height = {LayoutSizing::Fixed, 40.0f};
    align_self_row_root.style.direction = LayoutDirection::LeftToRight;
    align_self_row_root.style.child_align_y = LayoutAlignmentY::Start;
    LayoutNode align_self_row_child = box(746, 0);
    align_self_row_child.style.width = {LayoutSizing::Fixed, 20.0f};
    align_self_row_child.style.height = {LayoutSizing::Fixed, 10.0f};
    align_self_row_child.style.align_self = LayoutSelfAlignment::End;
    std::vector<LayoutNode> align_self_row_nodes{align_self_row_root, align_self_row_child};
    if (!engine.layout(align_self_row_nodes, 100.0f, 40.0f, 1.0f / 60.0f, snapshot, &error))
        return 47;
    const auto *align_self_row_item = snapshot.find(746);
    if (!align_self_row_item || std::abs(align_self_row_item->bounds.y - 30.0f) > 0.01f)
        return 48;

    LayoutNode align_self_column_root = box(747, -1);
    align_self_column_root.style.width = {LayoutSizing::Fixed, 40.0f};
    align_self_column_root.style.height = {LayoutSizing::Fixed, 100.0f};
    align_self_column_root.style.direction = LayoutDirection::TopToBottom;
    align_self_column_root.style.child_align_x = LayoutAlignmentX::Start;
    LayoutNode align_self_column_child = box(748, 0);
    align_self_column_child.style.width = {LayoutSizing::Fixed, 10.0f};
    align_self_column_child.style.height = {LayoutSizing::Fixed, 20.0f};
    align_self_column_child.style.align_self = LayoutSelfAlignment::Center;
    std::vector<LayoutNode> align_self_column_nodes{align_self_column_root,
                                                    align_self_column_child};
    if (!engine.layout(align_self_column_nodes, 40.0f, 100.0f, 1.0f / 60.0f, snapshot, &error))
        return 49;
    const auto *align_self_column_item = snapshot.find(748);
    if (!align_self_column_item || std::abs(align_self_column_item->bounds.x - 15.0f) > 0.01f)
        return 50;
    std::cout << "PASS: Clay layout boxes, text, transforms, and geometry\n";
    return 0;
#endif
}
