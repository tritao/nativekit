#include "layout/layout_engine.h"
#include "prepare/skribidi_adapter.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace nkui;

namespace {

LayoutNode box(uint32_t id, int32_t parent) {
    LayoutNode node;
    node.id = id;
    node.parent = parent;
    node.kind = LayoutNodeKind::Box;
    node.style.width = {LayoutSizing::Grow, 0.0f};
    node.style.height = {LayoutSizing::Fit, 0.0f};
    return node;
}

LayoutNode text(uint32_t id, int32_t parent, const char *value) {
    LayoutNode node = box(id, parent);
    node.kind = LayoutNodeKind::Text;
    node.text = value;
    node.font_size = 18;
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
                         "keeps box constraints and hit testing"));

    LayoutNode button = box(3, 0);
    button.kind = LayoutNodeKind::Button;
    button.style.width = {LayoutSizing::Fixed, 180.0f};
    button.style.height = {LayoutSizing::Fit, 0.0f};
    button.style.padding_left = button.style.padding_right = 14;
    button.style.padding_top = button.style.padding_bottom = 9;
    button.style.background = {0.2f, 0.55f, 0.9f, 1.0f};
    nodes.push_back(button);
    nodes.push_back(text(4, 2, "Press me"));

    LayoutSnapshot snapshot;
    LayoutError error;
    if (!engine.layout(nodes, 420.0f, 240.0f, 10.0f, 10.0f, false, 1.0f / 60.0f, snapshot,
                       &error)) {
        std::cerr << (error.message ? error.message : "layout failed") << "\n";
        return 4;
    }

    const LayoutItem *button_item = snapshot.find(3);
    const LayoutItem *title_item = snapshot.find(2);
    if (!button_item || !title_item || button_item->bounds.width != 180.0f ||
        button_item->bounds.height <= 0.0f || title_item->bounds.width <= 0.0f)
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
    if (!engine.layout(nodes, 420.0f, 240.0f, 0.0f, 0.0f, false, 1.0f / 60.0f, snapshot,
                       &error) ||
        engine.text_adapter()->layout_build_count() != stable_text_layout_builds)
        return 13;
    button_item = snapshot.find(3);
    if (!button_item)
        return 13;
    const auto hit = snapshot.hit_test(button_item->bounds.x + 1.0f, button_item->bounds.y + 1.0f);
    if (!hit || *hit != 3)
        return 7;

    const float click_x = button_item->bounds.x + button_item->bounds.width / 2.0f;
    const float click_y = button_item->bounds.y + button_item->bounds.height / 2.0f;
    if (!engine.layout(nodes, 420.0f, 240.0f, click_x, click_y, true, 1.0f / 60.0f,
                       snapshot, &error) ||
        !engine.layout(nodes, 420.0f, 240.0f, click_x, click_y, false, 1.0f / 60.0f,
                       snapshot, &error))
        return 8;
    if (snapshot.events.size() != 1 || snapshot.events.front().node_id != 3)
        return 9;

    std::cout << "PASS: Clay layout Box + Text + Button\n";
    return 0;
#endif
}
