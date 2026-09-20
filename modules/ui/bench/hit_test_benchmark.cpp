#include "nativekit_ui_layout.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr std::size_t kRecordBytes = NKUI_LAYOUT_NODE_RECORD_BYTES;
constexpr std::size_t kHeaderBytes = NKUI_LAYOUT_TRANSACTION_HEADER_BYTES;
constexpr uint32_t kViewportWidth = 1280;
constexpr uint32_t kViewportHeight = 720;

struct NodeSpec {
    int32_t parent = -1;
    float width = 0.0f;
    float height = 0.0f;
    float position_x = 0.0f;
    float position_y = 0.0f;
    int32_t z_index = 0;
    uint32_t flags = NKUI_LAYOUT_NODE_VISIBLE;
    uint32_t clip_flags = 0;
    uint32_t direction = NKUI_LAYOUT_DIRECTION_TOP_TO_BOTTOM;
};

struct Point {
    float x;
    float y;
};

struct Scenario {
    const char *name;
    std::vector<NodeSpec> nodes;
    std::vector<Point> points;
    uint32_t iterations;
};

void write_u32(std::vector<uint8_t> &bytes, std::size_t offset, uint32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void write_i32(std::vector<uint8_t> &bytes, std::size_t offset, int32_t value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void write_float(std::vector<uint8_t> &bytes, std::size_t offset, float value) {
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::vector<uint8_t> encode(const std::vector<NodeSpec> &nodes) {
    const std::size_t bytes = kHeaderBytes + nodes.size() * kRecordBytes;
    std::vector<uint8_t> result(bytes, 0);
    write_u32(result, 0, NKUI_LAYOUT_TRANSACTION_VERSION);
    write_u32(result, 4, static_cast<uint32_t>(nodes.size()));
    write_u32(result, 8, NKUI_LAYOUT_NODE_RECORD_BYTES);
    write_u32(result, 12, static_cast<uint32_t>(bytes));
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const auto &node = nodes[index];
        const std::size_t offset = kHeaderBytes + index * kRecordBytes;
        write_u32(result, offset + NKUI_LAYOUT_NODE_ID_OFFSET, static_cast<uint32_t>(index + 1));
        write_i32(result, offset + NKUI_LAYOUT_NODE_PARENT_OFFSET, node.parent);
        write_u32(result, offset + NKUI_LAYOUT_NODE_VISUAL_KIND_OFFSET, NKUI_LAYOUT_VISUAL_BOX);
        write_u32(result, offset + NKUI_LAYOUT_NODE_WIDTH_SIZING_OFFSET, NKUI_LAYOUT_SIZING_FIXED);
        write_float(result, offset + NKUI_LAYOUT_NODE_WIDTH_VALUE_OFFSET, node.width);
        write_u32(result, offset + NKUI_LAYOUT_NODE_HEIGHT_SIZING_OFFSET,
                  NKUI_LAYOUT_SIZING_FIXED);
        write_float(result, offset + NKUI_LAYOUT_NODE_HEIGHT_VALUE_OFFSET, node.height);
        write_u32(result, offset + NKUI_LAYOUT_NODE_DIRECTION_OFFSET, node.direction);
        write_u32(result, offset + NKUI_LAYOUT_NODE_CLIP_FLAGS_OFFSET, node.clip_flags);
        write_u32(result, offset + NKUI_LAYOUT_NODE_TEXT_OFFSET_OFFSET, static_cast<uint32_t>(bytes));
        write_float(result, offset + NKUI_LAYOUT_NODE_FONT_SIZE_OFFSET, 16.0f);
        write_float(result, offset + NKUI_LAYOUT_NODE_TRANSFORM_A_OFFSET, 1.0f);
        write_float(result, offset + NKUI_LAYOUT_NODE_TRANSFORM_D_OFFSET, 1.0f);
        write_u32(result, offset + NKUI_LAYOUT_NODE_FLAGS_OFFSET, node.flags);
        write_float(result, offset + NKUI_LAYOUT_NODE_POSITION_X_OFFSET, node.position_x);
        write_float(result, offset + NKUI_LAYOUT_NODE_POSITION_Y_OFFSET, node.position_y);
        write_i32(result, offset + NKUI_LAYOUT_NODE_Z_INDEX_OFFSET, node.z_index);
        write_float(result, offset + NKUI_LAYOUT_NODE_WIDTH_GROW_WEIGHT_OFFSET, 1.0f);
        write_float(result, offset + NKUI_LAYOUT_NODE_HEIGHT_GROW_WEIGHT_OFFSET, 1.0f);
    }
    return result;
}

std::vector<Point> sweep_points(std::size_t count) {
    std::vector<Point> points;
    points.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const float t = count <= 1 ? 0.0f : static_cast<float>(index) / (count - 1);
        points.push_back({t * (kViewportWidth - 1.0f), t * (kViewportHeight - 1.0f)});
    }
    return points;
}

Scenario ordinary(const char *name, std::size_t leaf_count, uint32_t iterations) {
    constexpr std::size_t kLeavesPerRow = 100;
    const std::size_t row_count = (leaf_count + kLeavesPerRow - 1) / kLeavesPerRow;
    std::vector<NodeSpec> nodes;
    nodes.reserve(1 + row_count + leaf_count);
    nodes.push_back({-1, static_cast<float>(kViewportWidth), static_cast<float>(kViewportHeight)});
    for (std::size_t row = 0; row < row_count; ++row)
        nodes.push_back({0, static_cast<float>(kViewportWidth), 72.0f, 0.0f, 0.0f, 0,
                         NKUI_LAYOUT_NODE_VISIBLE, 0, NKUI_LAYOUT_DIRECTION_LEFT_TO_RIGHT});
    for (std::size_t index = 0; index < leaf_count; ++index) {
        const std::size_t row = index / kLeavesPerRow;
        nodes.push_back({static_cast<int32_t>(1 + row), 12.0f, 72.0f});
    }
    return {name, std::move(nodes), sweep_points(257), iterations};
}

Scenario nested_clipping(uint32_t depth, uint32_t iterations) {
    std::vector<NodeSpec> nodes;
    nodes.reserve(depth);
    nodes.push_back({-1, static_cast<float>(kViewportWidth), static_cast<float>(kViewportHeight),
                     0.0f, 0.0f, 0, NKUI_LAYOUT_NODE_VISIBLE, 0});
    for (uint32_t index = 1; index < depth; ++index)
        nodes.push_back({static_cast<int32_t>(index - 1), 1200.0f, 680.0f, 0.0f, 0.0f, 0,
                         NKUI_LAYOUT_NODE_VISIBLE,
                         NKUI_LAYOUT_CLIP_HORIZONTAL | NKUI_LAYOUT_CLIP_VERTICAL});
    return {"deeply nested clipping", std::move(nodes), {{640.0f, 360.0f}, {2.0f, 2.0f}},
            iterations};
}

Scenario heavy_overlap(std::size_t count, uint32_t iterations) {
    std::vector<NodeSpec> nodes;
    nodes.reserve(1 + count);
    nodes.push_back({-1, static_cast<float>(kViewportWidth), static_cast<float>(kViewportHeight)});
    for (std::size_t index = 0; index < count; ++index)
        nodes.push_back({0, static_cast<float>(kViewportWidth), static_cast<float>(kViewportHeight),
                         0.0f, 0.0f, static_cast<int32_t>(index),
                         NKUI_LAYOUT_NODE_VISIBLE | NKUI_LAYOUT_NODE_FLOATING});
    return {"heavy overlap", std::move(nodes), {{640.0f, 360.0f}, {100.0f, 100.0f}}, iterations};
}

Scenario absolute_z_index(std::size_t count, uint32_t iterations) {
    constexpr std::size_t kColumns = 40;
    constexpr float kCellWidth = 32.0f;
    constexpr float kCellHeight = 32.0f;
    std::vector<NodeSpec> nodes;
    nodes.reserve(1 + count);
    nodes.push_back({-1, static_cast<float>(kViewportWidth), static_cast<float>(kViewportHeight)});
    for (std::size_t index = 0; index < count; ++index) {
        const std::size_t column = index % kColumns;
        const std::size_t row = index / kColumns;
        nodes.push_back({0, kCellWidth, kCellHeight, static_cast<float>(column) * kCellWidth,
                         static_cast<float>(row) * kCellHeight,
                         static_cast<int32_t>((index * 37) % 101),
                         NKUI_LAYOUT_NODE_VISIBLE | NKUI_LAYOUT_NODE_FLOATING});
    }
    return {"absolute and z-index", std::move(nodes), sweep_points(257), iterations};
}

struct Counters {
    uint64_t queries = 0;
    uint64_t nodes_visited = 0;
    uint64_t subtrees_rejected = 0;
    uint64_t precise_hit_tests = 0;
    uint64_t max_nodes_visited = 0;
    uint64_t nanoseconds = 0;
};

Counters read_stats(nkui_layout_session session) {
    nkui_layout_hit_test_stats stats{};
    assert(nkui_layout_session_get_hit_test_stats(session, &stats) == NKUI_OK);
    return {stats.hit_test_count, stats.nodes_visited, stats.subtrees_rejected,
            stats.precise_hit_tests, stats.max_nodes_visited, stats.hit_test_time_nanoseconds};
}

Counters subtract(const Counters &after, const Counters &before) {
    return {after.queries - before.queries,
            after.nodes_visited - before.nodes_visited,
            after.subtrees_rejected - before.subtrees_rejected,
            after.precise_hit_tests - before.precise_hit_tests,
            after.max_nodes_visited,
            after.nanoseconds - before.nanoseconds};
}

void run(const Scenario &scenario) {
    nkui_layout_session session{};
    assert(nkui_layout_session_create(&session) == NKUI_OK);
    const auto transaction = encode(scenario.nodes);
    nkui_layout_frame_input frame{sizeof(frame), static_cast<float>(kViewportWidth),
                                  static_cast<float>(kViewportHeight), 0.0f};
    const auto submit_status =
        nkui_layout_session_submit(session, transaction.data(), transaction.size(), &frame);
    if (submit_status != NKUI_OK) {
        std::fprintf(stderr, "%s submit failed: %d (%zu nodes, %zu bytes)\n", scenario.name,
                     submit_status, scenario.nodes.size(), transaction.size());
        assert(false);
    }

    std::vector<uint8_t> path(4096, 0);
    const auto query = [&](const Point point) {
        uint32_t count = 0;
        const auto result = nkui_layout_session_hit_test_into(
            session, point.x, point.y, path.data(), static_cast<uint32_t>(path.size()), &count);
        assert(result == NKUI_OK);
    };
    for (std::size_t index = 0; index < scenario.points.size(); ++index)
        query(scenario.points[index]);
    const Counters before = read_stats(session);
    for (uint32_t iteration = 0; iteration < scenario.iterations; ++iteration)
        for (const auto point : scenario.points)
            query(point);
    const Counters counters = subtract(read_stats(session), before);
    const double queries = static_cast<double>(counters.queries);
    const double average_nodes = counters.nodes_visited / queries;
    const double rejection_ratio = counters.nodes_visited == 0
                                       ? 0.0
                                       : 100.0 * counters.subtrees_rejected /
                                             counters.nodes_visited;
    const double precise = counters.precise_hit_tests / queries;
    const double microseconds = counters.nanoseconds / queries / 1000.0;
    std::printf("%-24s %6zu %8llu %10.2f %9.2f%% %10.2f %10.3f %8llu\n", scenario.name,
                scenario.nodes.size(), static_cast<unsigned long long>(counters.queries),
                average_nodes, rejection_ratio, precise, microseconds,
                static_cast<unsigned long long>(counters.max_nodes_visited));
    assert(nkui_layout_session_destroy(session) == NKUI_OK);
}

} // namespace

int main() {
    std::printf("Native hit-test benchmark (caller-owned into buffer)\n");
    std::printf("%-24s %6s %8s %10s %10s %10s %10s %8s\n", "scenario", "nodes", "queries",
                "avg nodes", "reject", "precise", "us/query", "max");
    run(ordinary("ordinary UI (1k)", 1000, 16));
    run(ordinary("ordinary UI (10k)", 10000, 8));
    run(nested_clipping(64, 32));
    run(heavy_overlap(1000, 16));
    run(absolute_z_index(2000, 16));
    run(ordinary("pointer sweep (10k)", 10000, 32));
    return 0;
}
