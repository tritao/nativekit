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

    float unit() {
        return static_cast<float>(next() & 0xffffu) / 65535.0f;
    }
};

bool close(float left, float right) {
    return std::abs(left - right) <= 0.001f;
}

bool finite_rect(const LayoutRect &rect) {
    return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
           std::isfinite(rect.height) && rect.width >= 0.0f && rect.height >= 0.0f;
}

bool finite_transform(const LayoutTransform &transform) {
    return std::isfinite(transform.a) && std::isfinite(transform.b) &&
           std::isfinite(transform.c) && std::isfinite(transform.d) &&
           std::isfinite(transform.tx) && std::isfinite(transform.ty);
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
    root.style.direction = (rng.next() & 1u) ? LayoutDirection::LeftToRight
                                             : LayoutDirection::TopToBottom;
    root.style.padding_left = static_cast<uint16_t>(rng.next() % 9u);
    root.style.padding_right = static_cast<uint16_t>(rng.next() % 9u);
    root.style.padding_top = static_cast<uint16_t>(rng.next() % 9u);
    root.style.padding_bottom = static_cast<uint16_t>(rng.next() % 9u);
    root.style.child_gap = static_cast<uint16_t>(rng.next() % 7u);
    root.style.child_align_x = static_cast<uint8_t>(rng.next() % 3u);
    root.style.child_align_y = static_cast<uint8_t>(rng.next() % 3u);
    nodes.push_back(root);

    std::vector<std::size_t> containers{0};
    for (std::size_t index = 1; index < node_count; ++index) {
        LayoutNode node;
        node.id = static_cast<uint32_t>(index + 1);
        node.parent = static_cast<int32_t>(containers[rng.next() % containers.size()]);
        node.visual_kind = rng.next() % 5u == 0 ? LayoutVisualKind::Text : LayoutVisualKind::Box;
        node.style.width = random_axis(rng);
        node.style.height = random_axis(rng);
        node.style.direction = (rng.next() & 1u) ? LayoutDirection::LeftToRight
                                                 : LayoutDirection::TopToBottom;
        node.style.padding_left = static_cast<uint16_t>(rng.next() % 5u);
        node.style.padding_right = static_cast<uint16_t>(rng.next() % 5u);
        node.style.padding_top = static_cast<uint16_t>(rng.next() % 5u);
        node.style.padding_bottom = static_cast<uint16_t>(rng.next() % 5u);
        node.style.child_gap = static_cast<uint16_t>(rng.next() % 5u);
        node.style.child_align_x = static_cast<uint8_t>(rng.next() % 3u);
        node.style.child_align_y = static_cast<uint8_t>(rng.next() % 3u);
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
            node.paragraph_style.wrap = (rng.next() & 1u) ? TextWrapMode::Word
                                                          : TextWrapMode::WordCharacter;
            node.paragraph_style.line_height = rng.next() % 3u == 0 ? 12.0f + rng.unit() * 10.0f
                                                                      : 0.0f;
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
        const float determinant = item ? item->transform.a * item->transform.d -
                                             item->transform.b * item->transform.c
                                       : 0.0f;
        if (!item || !finite_rect(item->bounds) || !finite_rect(item->clip_bounds) ||
            !finite_rect(item->content_bounds) || !finite_transform(item->transform) ||
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
    if (left.items.size() != right.items.size() || left.primitives.size() != right.primitives.size())
        return false;
    for (std::size_t index = 0; index < left.items.size(); ++index) {
        const LayoutItem &a = left.items[index];
        const LayoutItem &b = right.items[index];
        const float a_values[] = {
            a.bounds.x,       a.bounds.y,       a.bounds.width,       a.bounds.height,
            a.clip_bounds.x,  a.clip_bounds.y,  a.clip_bounds.width,  a.clip_bounds.height,
            a.content_bounds.x, a.content_bounds.y, a.content_bounds.width,
            a.content_bounds.height, a.transform.a, a.transform.b, a.transform.c,
            a.transform.d, a.transform.tx, a.transform.ty, a.baseline};
        const float b_values[] = {
            b.bounds.x,       b.bounds.y,       b.bounds.width,       b.bounds.height,
            b.clip_bounds.x,  b.clip_bounds.y,  b.clip_bounds.width,  b.clip_bounds.height,
            b.content_bounds.x, b.content_bounds.y, b.content_bounds.width,
            b.content_bounds.height, b.transform.a, b.transform.b, b.transform.c,
            b.transform.d, b.transform.tx, b.transform.ty, b.baseline};
        for (std::size_t value = 0; value < sizeof(a_values) / sizeof(a_values[0]); ++value)
            if (!close(a_values[value], b_values[value]))
                return false;
        if (a.id != b.id || a.visual_kind != b.visual_kind || a.visible != b.visible ||
            a.has_baseline != b.has_baseline)
            return false;
    }
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
            std::cerr << "case " << case_index << " failed: "
                      << (error.message ? error.message : "unknown layout error") << "\n";
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

    std::cout << "PASS: " << cases << " randomized Clay layout invariant cases\n";
    return 0;
#endif
}
