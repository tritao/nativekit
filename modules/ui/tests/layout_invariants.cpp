#include "layout/layout_engine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace nkui;

namespace {

struct Rng {
    uint32_t state;

    uint32_t next() {
        state = state * 1664525u + 1013904223u;
        return state;
    }

    float unit() { return static_cast<float>(next() & 0xffffu) / 65535.0f; }
};

bool close(float left, float right) {
    return std::abs(left - right) <= 0.001f;
}

bool finite_rect(const LayoutRect &rect) {
    return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
           std::isfinite(rect.height) && rect.width >= 0.0f && rect.height >= 0.0f;
}

LayoutRect intersect_rect(LayoutRect left, LayoutRect right) {
    const float x = std::max(left.x, right.x);
    const float y = std::max(left.y, right.y);
    return {x, y, std::max(0.0f, std::min(left.x + left.width, right.x + right.width) - x),
            std::max(0.0f, std::min(left.y + left.height, right.y + right.height) - y)};
}

bool contains_rect(const LayoutRect &outer, const LayoutRect &inner) {
    if (inner.width <= 0.0f || inner.height <= 0.0f)
        return true;
    return inner.x + 0.001f >= outer.x && inner.y + 0.001f >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width + 0.001f &&
           inner.y + inner.height <= outer.y + outer.height + 0.001f;
}

bool finite_transform(const LayoutTransform &transform) {
    return std::isfinite(transform.a) && std::isfinite(transform.b) && std::isfinite(transform.c) &&
           std::isfinite(transform.d) && std::isfinite(transform.tx) && std::isfinite(transform.ty);
}

LayoutAxis random_axis(Rng &rng) {
    LayoutAxis axis;
    switch (rng.next() % 4u) {
    case 0:
        axis.sizing = LayoutSizing::Fit;
        break;
    case 1:
        axis.sizing = LayoutSizing::Grow;
        break;
    case 2:
        axis.sizing = LayoutSizing::Fixed;
        axis.value = 8.0f + static_cast<float>(rng.next() % 120u);
        return axis;
    default:
        axis.sizing = LayoutSizing::Percent;
        axis.value = 0.1f + rng.unit() * 0.8f;
        return axis;
    }

    axis.min = rng.next() % 3u == 0 ? rng.unit() * 24.0f : 0.0f;
    axis.max = rng.next() % 2u == 0 ? 0.0f : axis.min + 24.0f + rng.unit() * 160.0f;
    if (axis.sizing == LayoutSizing::Grow)
        axis.grow_weight = 0.25f + rng.unit() * 4.0f;
    return axis;
}

std::vector<LayoutNode> random_tree(Rng &rng) {
    const std::size_t node_count = 2u + rng.next() % 18u;
    std::vector<LayoutNode> nodes;
    nodes.reserve(node_count);

    LayoutNode root;
    root.id = 1;
    root.parent = -1;
    root.style.width = {LayoutSizing::Fixed, 320.0f};
    root.style.height = {LayoutSizing::Fixed, 240.0f};
    root.style.direction =
        (rng.next() & 1u) ? LayoutDirection::LeftToRight : LayoutDirection::TopToBottom;
    root.style.padding_left = static_cast<uint16_t>(rng.next() % 9u);
    root.style.padding_right = static_cast<uint16_t>(rng.next() % 9u);
    root.style.padding_top = static_cast<uint16_t>(rng.next() % 9u);
    root.style.padding_bottom = static_cast<uint16_t>(rng.next() % 9u);
    root.style.child_gap = static_cast<uint16_t>(rng.next() % 7u);
    root.style.row_gap = root.style.padding_top;
    root.style.column_gap = root.style.padding_left;
    root.style.wrap_mode = root.style.direction == LayoutDirection::LeftToRight
                               ? LayoutWrapMode::Wrap
                               : LayoutWrapMode::NoWrap;
    root.style.child_align_x = static_cast<LayoutAlignmentX>(rng.next() % 3u);
    root.style.child_align_y = static_cast<LayoutAlignmentY>(rng.next() % 4u);
    nodes.push_back(root);

    std::vector<std::size_t> containers{0};
    for (std::size_t index = 1; index < node_count; ++index) {
        LayoutNode node;
        node.id = static_cast<uint32_t>(index + 1);
        node.parent = static_cast<int32_t>(containers[rng.next() % containers.size()]);
        node.visual_kind = rng.next() % 5u == 0 ? LayoutVisualKind::Text : LayoutVisualKind::Box;
        node.style.width = random_axis(rng);
        node.style.height = random_axis(rng);
        node.style.direction =
            (rng.next() & 1u) ? LayoutDirection::LeftToRight : LayoutDirection::TopToBottom;
        node.style.padding_left = static_cast<uint16_t>(rng.next() % 5u);
        node.style.padding_right = static_cast<uint16_t>(rng.next() % 5u);
        node.style.padding_top = static_cast<uint16_t>(rng.next() % 5u);
        node.style.padding_bottom = static_cast<uint16_t>(rng.next() % 5u);
        node.style.child_gap = static_cast<uint16_t>(rng.next() % 5u);
        node.style.row_gap = node.style.padding_top;
        node.style.column_gap = node.style.padding_left;
        node.style.wrap_mode = node.style.direction == LayoutDirection::LeftToRight
                                   ? LayoutWrapMode::Wrap
                                   : LayoutWrapMode::NoWrap;
        node.style.child_align_x = static_cast<LayoutAlignmentX>(rng.next() % 3u);
        node.style.child_align_y = static_cast<LayoutAlignmentY>(rng.next() % 4u);
        node.style.clip_horizontal = rng.next() % 4u == 0;
        node.style.clip_vertical = rng.next() % 4u == 0;

        if (rng.next() % 8u == 0) {
            node.style.positioning = LayoutPositioning::Absolute;
            node.style.position_x = rng.unit() * 80.0f;
            node.style.position_y = rng.unit() * 80.0f;
            node.style.z_index = static_cast<int32_t>(rng.next() % 11u) - 5;
            node.style.clip_to_parent = rng.next() & 1u;
        }

        if (node.visual_kind == LayoutVisualKind::Text) {
            node.text = "fuzzed text content for intrinsic measurement";
            node.text_style.font_size = 10.0f + rng.unit() * 14.0f;
            node.paragraph_style.wrap =
                (rng.next() & 1u) ? TextWrapMode::Word : TextWrapMode::WordCharacter;
            node.paragraph_style.line_height =
                rng.next() % 3u == 0 ? 12.0f + rng.unit() * 10.0f : 0.0f;
        } else if (rng.next() % 7u == 0) {
            // Keep the aspect-ratio case meaningful: Clay resolves the
            // missing height from a known width.
            node.style.width = {LayoutSizing::Fixed, 16.0f + rng.unit() * 100.0f};
            node.style.height = {LayoutSizing::Fit, 0.0f};
            node.style.aspect_ratio = 0.5f + rng.unit() * 2.0f;
        }

        nodes.push_back(std::move(node));
        if (nodes.back().visual_kind != LayoutVisualKind::Text)
            containers.push_back(index);
    }

    return nodes;
}

bool check_snapshot(const std::vector<LayoutNode> &nodes, const LayoutSnapshot &snapshot,
                    uint32_t case_index) {
    if (snapshot.items.size() != nodes.size()) {
        std::cerr << "case " << case_index << ": item count mismatch\n";
        return false;
    }
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const LayoutItem *item = snapshot.find(nodes[index].id);
        const float determinant =
            item ? item->transform.a * item->transform.d - item->transform.b * item->transform.c
                 : 0.0f;
        if (!item || !finite_rect(item->bounds) || !finite_rect(item->clip_bounds) ||
            !finite_rect(item->content_bounds) || !finite_rect(item->local_bounds) ||
            !finite_rect(item->world_bounds) || !finite_rect(item->subtree_hit_bounds) ||
            !finite_transform(item->transform) || !finite_transform(item->inverse_transform) ||
            !std::isfinite(determinant) || std::abs(determinant) < 0.000001f ||
            (item->has_baseline && !std::isfinite(item->baseline))) {
            if (item)
                std::cerr << "id " << item->id << " bounds " << item->bounds.x << ","
                          << item->bounds.y << " " << item->bounds.width << "x"
                          << item->bounds.height << " clip " << item->clip_bounds.x << ","
                          << item->clip_bounds.y << " " << item->clip_bounds.width << "x"
                          << item->clip_bounds.height << " content " << item->content_bounds.x
                          << "," << item->content_bounds.y << " " << item->content_bounds.width
                          << "x" << item->content_bounds.height << "\n";
            for (const LayoutItem &debug_item : snapshot.items)
                std::cerr << "  item " << debug_item.id << " " << debug_item.bounds.x << ","
                          << debug_item.bounds.y << " " << debug_item.bounds.width << "x"
                          << debug_item.bounds.height << "\n";
            std::cerr << "case " << case_index << ": non-finite item geometry\n";
            return false;
        }

        const float inverse_determinant = item->inverse_transform.a * item->inverse_transform.d -
                                           item->inverse_transform.b * item->inverse_transform.c;
        const LayoutRect own_hit_bounds = intersect_rect(item->world_bounds, item->clip_bounds);
        if (item->index != index ||
            (item->parent_index == kInvalidLayoutIndex) != (nodes[index].parent < 0) ||
            (item->parent_index != kInvalidLayoutIndex &&
             (item->parent_index >= snapshot.items.size() ||
              snapshot.items[item->parent_index].id != item->parent_id)) ||
            !close(item->local_bounds.x, 0.0f) || !close(item->local_bounds.y, 0.0f) ||
            !close(item->local_bounds.width, item->bounds.width) ||
            !close(item->local_bounds.height, item->bounds.height) ||
            !std::isfinite(inverse_determinant) || std::abs(inverse_determinant) < 0.000001f ||
            item->child_offset > snapshot.child_indices.size() ||
            item->child_count > snapshot.child_indices.size() - item->child_offset ||
            item->hit_self != nodes[index].hit_self ||
            item->hit_children != nodes[index].hit_children ||
            (item->visible && !contains_rect(item->subtree_hit_bounds, own_hit_bounds))) {
            std::cerr << "case " << case_index << ": invalid resolved scene metadata\n";
            return false;
        }
        for (uint32_t child_offset = 0; child_offset < item->child_count; ++child_offset) {
            const uint32_t child_index = snapshot.child_indices[item->child_offset + child_offset];
            if (child_index >= snapshot.items.size() ||
                snapshot.items[child_index].parent_index != item->index) {
                std::cerr << "case " << case_index << ": invalid child range\n";
                return false;
            }
        }

        const LayoutAxis *axes[] = {&nodes[index].style.width, &nodes[index].style.height};
        const float sizes[] = {item->bounds.width, item->bounds.height};
        for (int axis_index = 0; axis_index < 2; ++axis_index) {
            const LayoutAxis &axis = *axes[axis_index];
            if (axis.sizing != LayoutSizing::Fit && axis.sizing != LayoutSizing::Grow)
                continue;
            if (nodes[index].style.aspect_ratio != 0.0f && axis_index == 1)
                continue;
            if (sizes[axis_index] + 0.01f < axis.min ||
                (axis.max != 0.0f && sizes[axis_index] > axis.max + 0.01f)) {
                std::cerr << "case " << case_index << ": sizing constraint violated\n";
                return false;
            }
        }
    }
    for (const LayoutPrimitive &primitive : snapshot.primitives) {
        if (!finite_rect(primitive.bounds) || !finite_transform(primitive.transform)) {
            std::cerr << "case " << case_index << ": non-finite primitive geometry\n";
            return false;
        }
    }
    return true;
}

bool same_geometry(const LayoutSnapshot &left, const LayoutSnapshot &right) {
    if (left.items.size() != right.items.size() ||
        left.child_indices != right.child_indices || left.primitives.size() != right.primitives.size())
        return false;
    for (std::size_t index = 0; index < left.items.size(); ++index) {
        const LayoutItem &a = left.items[index];
        const LayoutItem &b = right.items[index];
        const float a_values[] = {a.bounds.x,
                                  a.bounds.y,
                                  a.bounds.width,
                                  a.bounds.height,
                                  a.clip_bounds.x,
                                  a.clip_bounds.y,
                                  a.clip_bounds.width,
                                  a.clip_bounds.height,
                                  a.content_bounds.x,
                                  a.content_bounds.y,
                                  a.content_bounds.width,
                                  a.content_bounds.height,
                                  a.local_bounds.x,
                                  a.local_bounds.y,
                                  a.local_bounds.width,
                                  a.local_bounds.height,
                                  a.world_bounds.x,
                                  a.world_bounds.y,
                                  a.world_bounds.width,
                                  a.world_bounds.height,
                                  a.subtree_hit_bounds.x,
                                  a.subtree_hit_bounds.y,
                                  a.subtree_hit_bounds.width,
                                  a.subtree_hit_bounds.height,
                                  a.inverse_transform.a,
                                  a.inverse_transform.b,
                                  a.inverse_transform.c,
                                  a.inverse_transform.d,
                                  a.inverse_transform.tx,
                                  a.inverse_transform.ty,
                                  a.transform.a,
                                  a.transform.b,
                                  a.transform.c,
                                  a.transform.d,
                                  a.transform.tx,
                                  a.transform.ty,
                                  a.baseline};
        const float b_values[] = {b.bounds.x,
                                  b.bounds.y,
                                  b.bounds.width,
                                  b.bounds.height,
                                  b.clip_bounds.x,
                                  b.clip_bounds.y,
                                  b.clip_bounds.width,
                                  b.clip_bounds.height,
                                  b.content_bounds.x,
                                  b.content_bounds.y,
                                  b.content_bounds.width,
                                  b.content_bounds.height,
                                  b.local_bounds.x,
                                  b.local_bounds.y,
                                  b.local_bounds.width,
                                  b.local_bounds.height,
                                  b.world_bounds.x,
                                  b.world_bounds.y,
                                  b.world_bounds.width,
                                  b.world_bounds.height,
                                  b.subtree_hit_bounds.x,
                                  b.subtree_hit_bounds.y,
                                  b.subtree_hit_bounds.width,
                                  b.subtree_hit_bounds.height,
                                  b.inverse_transform.a,
                                  b.inverse_transform.b,
                                  b.inverse_transform.c,
                                  b.inverse_transform.d,
                                  b.inverse_transform.tx,
                                  b.inverse_transform.ty,
                                  b.transform.a,
                                  b.transform.b,
                                  b.transform.c,
                                  b.transform.d,
                                  b.transform.tx,
                                  b.transform.ty,
                                  b.baseline};
        for (std::size_t value = 0; value < sizeof(a_values) / sizeof(a_values[0]); ++value)
            if (!close(a_values[value], b_values[value]))
                return false;
        if (a.id != b.id || a.visual_kind != b.visual_kind || a.visible != b.visible ||
            a.has_baseline != b.has_baseline || a.index != b.index ||
            a.parent_index != b.parent_index || a.child_offset != b.child_offset ||
            a.child_count != b.child_count || a.paint_order != b.paint_order ||
            a.hit_child_offset != b.hit_child_offset || a.hit_child_count != b.hit_child_count ||
            a.subtree_paint_order != b.subtree_paint_order ||
            a.z_index != b.z_index || a.positioned_absolute != b.positioned_absolute ||
            a.hit_self != b.hit_self || a.hit_children != b.hit_children)
            return false;
    }
    if (left.hit_child_indices != right.hit_child_indices)
        return false;
    for (std::size_t index = 0; index < left.primitives.size(); ++index) {
        const LayoutPrimitive &a = left.primitives[index];
        const LayoutPrimitive &b = right.primitives[index];
        if (a.kind != b.kind || a.node_id != b.node_id || a.visible != b.visible ||
            !close(a.bounds.x, b.bounds.x) || !close(a.bounds.y, b.bounds.y) ||
            !close(a.bounds.width, b.bounds.width) || !close(a.bounds.height, b.bounds.height))
            return false;
    }
    return true;
}

uint32_t requested_cases() {
    const char *value = std::getenv("NKUI_LAYOUT_FUZZ_CASES");
    if (!value || !*value)
        return 2000;
    char *end = nullptr;
    const unsigned long parsed = std::strtoul(value, &end, 10);
    if (end == value || *end != '\0' || parsed == 0 || parsed > 100000)
        return 2000;
    return static_cast<uint32_t>(parsed);
}

bool resolved_scene_metadata_case(LayoutEngine &engine) {
    std::vector<LayoutNode> nodes(4);
    nodes[0].id = 100;
    nodes[0].parent = -1;
    nodes[0].style.width = {LayoutSizing::Fixed, 320.0f};
    nodes[0].style.height = {LayoutSizing::Fixed, 240.0f};
    nodes[0].hit_self = false;

    nodes[1].id = 101;
    nodes[1].parent = 0;
    nodes[1].style.width = {LayoutSizing::Fixed, 120.0f};
    nodes[1].style.height = {LayoutSizing::Fixed, 60.0f};
    nodes[1].style.background.alpha = 1.0f;

    nodes[2].id = 102;
    nodes[2].parent = 0;
    nodes[2].style.width = {LayoutSizing::Fixed, 120.0f};
    nodes[2].style.height = {LayoutSizing::Fixed, 60.0f};
    nodes[2].style.positioning = LayoutPositioning::Absolute;
    nodes[2].style.position_x = 10.0f;
    nodes[2].style.position_y = 10.0f;
    nodes[2].style.z_index = -1;
    nodes[2].style.transform.tx = 24.0f;
    nodes[2].style.transform.ty = 18.0f;
    nodes[2].style.background.alpha = 1.0f;

    nodes[3].id = 103;
    nodes[3].parent = 0;
    nodes[3].style.width = {LayoutSizing::Fixed, 120.0f};
    nodes[3].style.height = {LayoutSizing::Fixed, 60.0f};
    nodes[3].style.positioning = LayoutPositioning::Absolute;
    nodes[3].style.position_x = 10.0f;
    nodes[3].style.position_y = 10.0f;
    nodes[3].style.z_index = 0;
    nodes[3].hit_self = false;
    nodes[3].hit_children = false;

    LayoutSnapshot snapshot;
    LayoutError error;
    if (!engine.layout(nodes, 320.0f, 240.0f, 1.0f / 60.0f, snapshot, &error))
        return false;
    const LayoutItem *root = snapshot.find(100);
    const LayoutItem *flow = snapshot.find(101);
    const LayoutItem *negative = snapshot.find(102);
    const LayoutItem *disabled = snapshot.find(103);
    if (!root || !flow || !negative || !disabled ||
        !(negative->paint_order < flow->paint_order && flow->paint_order < disabled->paint_order) ||
        disabled->subtree_hit_bounds.width != 0.0f ||
        disabled->subtree_hit_bounds.height != 0.0f ||
        !contains_rect(root->subtree_hit_bounds, flow->world_bounds) ||
        !contains_rect(root->subtree_hit_bounds, negative->world_bounds) ||
        negative->world_bounds.x <= negative->bounds.x ||
        negative->world_bounds.y <= negative->bounds.y)
        return false;
    return true;
}

} // namespace

int main() {
#ifndef NKUI_TEST_FONT_PATH
    std::cerr << "NKUI_TEST_FONT_PATH is required\n";
    return 2;
#else
    LayoutEngine engine;
    if (!engine.valid() || !engine.add_font(NKUI_TEST_FONT_PATH))
        return 3;

    Rng rng{0x4e4b5549u};
    const uint32_t cases = requested_cases();
    for (uint32_t case_index = 0; case_index < cases; ++case_index) {
        const std::vector<LayoutNode> nodes = random_tree(rng);
        LayoutSnapshot first;
        LayoutError error;
        if (!engine.layout(nodes, 320.0f, 240.0f, 1.0f / 60.0f, first, &error)) {
            std::cerr << "case " << case_index
                      << " failed: " << (error.message ? error.message : "unknown layout error")
                      << "\n";
            return 4;
        }
        if (!check_snapshot(nodes, first, case_index))
            return 5;

        LayoutSnapshot second;
        if (!engine.layout(nodes, 320.0f, 240.0f, 1.0f / 60.0f, second, &error) ||
            !same_geometry(first, second)) {
            std::cerr << "case " << case_index << ": layout is not deterministic\n";
            return 6;
        }
    }

    if (!resolved_scene_metadata_case(engine)) {
        std::cerr << "resolved scene metadata case failed\n";
        return 7;
    }

    std::cout << "PASS: " << cases << " randomized Clay layout invariant cases\n";
    return 0;
#endif
}
