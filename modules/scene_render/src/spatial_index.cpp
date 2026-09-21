#include "nativekit_scene_render.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace nkscene {

namespace {

constexpr std::uint32_t invalid_node = std::numeric_limits<std::uint32_t>::max();
constexpr std::size_t leaf_capacity = 8;

struct Entry {
    OccurrenceId occurrence;
    Bounds bounds;
};

struct Node {
    Bounds bounds;
    std::uint32_t left = invalid_node;
    std::uint32_t right = invalid_node;
    std::uint32_t first = 0;
    std::uint32_t count = 0;

    bool leaf() const noexcept { return left == invalid_node; }
};

Bounds merge_bounds(const Bounds &lhs, const Bounds &rhs) noexcept {
    if (!lhs.valid)
        return rhs;
    if (!rhs.valid)
        return lhs;
    Bounds result;
    result.valid = true;
    for (std::size_t axis = 0; axis < 3; ++axis) {
        result.minimum[axis] = std::min(lhs.minimum[axis], rhs.minimum[axis]);
        result.maximum[axis] = std::max(lhs.maximum[axis], rhs.maximum[axis]);
    }
    return result;
}

float centroid(const Bounds &bounds, std::size_t axis) noexcept {
    return (bounds.minimum[axis] + bounds.maximum[axis]) * 0.5f;
}

std::size_t split_axis(const Bounds &bounds) noexcept {
    std::size_t axis = 0;
    auto extent = bounds.maximum[0] - bounds.minimum[0];
    for (std::size_t candidate = 1; candidate < 3; ++candidate) {
        const auto candidate_extent = bounds.maximum[candidate] - bounds.minimum[candidate];
        if (candidate_extent > extent) {
            axis = candidate;
            extent = candidate_extent;
        }
    }
    return axis;
}

bool overlaps(const Bounds &lhs, const Bounds &rhs) noexcept {
    if (!lhs.valid || !rhs.valid)
        return false;
    for (std::size_t axis = 0; axis < 3; ++axis)
        if (lhs.maximum[axis] < rhs.minimum[axis] || rhs.maximum[axis] < lhs.minimum[axis])
            return false;
    return true;
}

bool normalize_ray(const Ray &ray, Ray &normalized) noexcept {
    const auto length =
        std::sqrt(ray.direction.x * ray.direction.x + ray.direction.y * ray.direction.y +
                  ray.direction.z * ray.direction.z);
    if (!(length > 1.0e-8f))
        return false;
    normalized.origin = ray.origin;
    normalized.direction = {ray.direction.x / length, ray.direction.y / length,
                            ray.direction.z / length};
    return true;
}

bool ray_hits_bounds(const Ray &ray, const Bounds &bounds) noexcept {
    if (!bounds.valid)
        return false;
    float near_distance = 0.0f;
    float far_distance = std::numeric_limits<float>::infinity();
    const std::array<float, 3> origin{ray.origin.x, ray.origin.y, ray.origin.z};
    const std::array<float, 3> direction{ray.direction.x, ray.direction.y, ray.direction.z};
    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (std::abs(direction[axis]) < 1.0e-8f) {
            if (origin[axis] < bounds.minimum[axis] || origin[axis] > bounds.maximum[axis])
                return false;
            continue;
        }
        auto first = (bounds.minimum[axis] - origin[axis]) / direction[axis];
        auto second = (bounds.maximum[axis] - origin[axis]) / direction[axis];
        if (first > second)
            std::swap(first, second);
        near_distance = std::max(near_distance, first);
        far_distance = std::min(far_distance, second);
        if (near_distance > far_distance)
            return false;
    }
    return far_distance >= 0.0f;
}

Vec3 transform_point(const LocalTransform &transform, const GeometryVertex &vertex) noexcept {
    const auto &point = vertex.position;
    return {transform.matrix[0] * point[0] + transform.matrix[4] * point[1] +
                transform.matrix[8] * point[2] + transform.matrix[12],
            transform.matrix[1] * point[0] + transform.matrix[5] * point[1] +
                transform.matrix[9] * point[2] + transform.matrix[13],
            transform.matrix[2] * point[0] + transform.matrix[6] * point[1] +
                transform.matrix[10] * point[2] + transform.matrix[14]};
}

Vec3 subtract(Vec3 lhs, Vec3 rhs) noexcept {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 cross(Vec3 lhs, Vec3 rhs) noexcept {
    return {lhs.y * rhs.z - lhs.z * rhs.y, lhs.z * rhs.x - lhs.x * rhs.z,
            lhs.x * rhs.y - lhs.y * rhs.x};
}

float dot(Vec3 lhs, Vec3 rhs) noexcept {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vec3 scale_add(Vec3 origin, Vec3 direction, float distance) noexcept {
    return {origin.x + direction.x * distance, origin.y + direction.y * distance,
            origin.z + direction.z * distance};
}

bool ray_hits_triangle(const Ray &ray, Vec3 first, Vec3 second, Vec3 third,
                       float &distance) noexcept {
    constexpr float epsilon = 1.0e-7f;
    const auto edge_one = subtract(second, first);
    const auto edge_two = subtract(third, first);
    const auto perpendicular = cross(ray.direction, edge_two);
    const auto determinant = dot(edge_one, perpendicular);
    if (std::abs(determinant) < epsilon)
        return false;
    const auto inverse = 1.0f / determinant;
    const auto offset = subtract(ray.origin, first);
    const auto barycentric_u = dot(offset, perpendicular) * inverse;
    if (barycentric_u < 0.0f || barycentric_u > 1.0f)
        return false;
    const auto direction = cross(offset, edge_one);
    const auto barycentric_v = dot(ray.direction, direction) * inverse;
    if (barycentric_v < 0.0f || barycentric_u + barycentric_v > 1.0f)
        return false;
    const auto candidate = dot(edge_two, direction) * inverse;
    if (candidate < 0.0f)
        return false;
    distance = candidate;
    return true;
}

std::uint32_t vertex_index(const GeometryPayload &payload, std::size_t index) noexcept {
    return payload.indexed() ? payload.indices[index] : static_cast<std::uint32_t>(index);
}

template <class Visitor>
void visit_bounds(const std::vector<Node> &nodes, const std::vector<Entry> &entries,
                  std::uint32_t node_index, const Bounds &query, Visitor &&visitor) {
    if (node_index == invalid_node || !overlaps(nodes[node_index].bounds, query))
        return;
    const auto &node = nodes[node_index];
    if (node.leaf()) {
        for (std::uint32_t index = 0; index < node.count; ++index)
            if (overlaps(entries[node.first + index].bounds, query))
                visitor(entries[node.first + index]);
        return;
    }
    visit_bounds(nodes, entries, node.left, query, visitor);
    visit_bounds(nodes, entries, node.right, query, visitor);
}

template <class Visitor>
void visit_ray(const std::vector<Node> &nodes, const std::vector<Entry> &entries,
               std::uint32_t node_index, const Ray &ray, Visitor &&visitor) {
    if (node_index == invalid_node || !ray_hits_bounds(ray, nodes[node_index].bounds))
        return;
    const auto &node = nodes[node_index];
    if (node.leaf()) {
        for (std::uint32_t index = 0; index < node.count; ++index)
            if (ray_hits_bounds(ray, entries[node.first + index].bounds))
                visitor(entries[node.first + index]);
        return;
    }
    visit_ray(nodes, entries, node.left, ray, visitor);
    visit_ray(nodes, entries, node.right, ray, visitor);
}

} // namespace

struct SceneSpatialIndex::State {
    explicit State(const SceneSnapshot &value) : snapshot(value), revision(value.revision()) {}

    SceneSnapshot snapshot;
    std::uint64_t revision = 0;
    std::vector<Entry> entries;
    std::vector<Node> nodes;
    mutable std::vector<OccurrenceId> results;
};

SceneSpatialIndex::SceneSpatialIndex(const SceneSnapshot &snapshot)
    : state_(std::make_unique<State>(snapshot)) {
    state_->entries.reserve(snapshot.occurrences().size());
    for (const auto &occurrence : snapshot.occurrences())
        if (occurrence.bounds.valid)
            state_->entries.push_back({occurrence.occurrence, occurrence.bounds});
    const auto build_node = [&](auto &&self, std::size_t first, std::size_t last) -> std::uint32_t {
        const auto node_index = static_cast<std::uint32_t>(state_->nodes.size());
        state_->nodes.emplace_back();
        Bounds node_bounds;
        for (std::size_t index = first; index < last; ++index)
            node_bounds = merge_bounds(node_bounds, state_->entries[index].bounds);
        state_->nodes[node_index].bounds = node_bounds;
        const auto count = last - first;
        if (count <= leaf_capacity) {
            state_->nodes[node_index].first = static_cast<std::uint32_t>(first);
            state_->nodes[node_index].count = static_cast<std::uint32_t>(count);
            return node_index;
        }

        const auto axis = split_axis(node_bounds);
        const auto middle = first + count / 2;
        std::nth_element(state_->entries.begin() + static_cast<std::ptrdiff_t>(first),
                         state_->entries.begin() + static_cast<std::ptrdiff_t>(middle),
                         state_->entries.begin() + static_cast<std::ptrdiff_t>(last),
                         [axis](const Entry &lhs, const Entry &rhs) {
                             return centroid(lhs.bounds, axis) < centroid(rhs.bounds, axis);
                         });
        state_->nodes[node_index].left = self(self, first, middle);
        state_->nodes[node_index].right = self(self, middle, last);
        return node_index;
    };
    if (!state_->entries.empty())
        build_node(build_node, 0, state_->entries.size());
}

SceneSpatialIndex::~SceneSpatialIndex() = default;
SceneSpatialIndex::SceneSpatialIndex(SceneSpatialIndex &&) noexcept = default;
SceneSpatialIndex &SceneSpatialIndex::operator=(SceneSpatialIndex &&) noexcept = default;

std::uint64_t SceneSpatialIndex::source_revision() const noexcept {
    return state_ ? state_->revision : 0;
}

std::span<const OccurrenceId> SceneSpatialIndex::query_bounds(const Bounds &bounds) const {
    if (!state_)
        return {};
    state_->results.clear();
    if (!bounds.valid || state_->nodes.empty())
        return {};
    visit_bounds(state_->nodes, state_->entries, 0, bounds,
                 [this](const Entry &entry) { state_->results.push_back(entry.occurrence); });
    std::sort(state_->results.begin(), state_->results.end(),
              [](OccurrenceId lhs, OccurrenceId rhs) { return lhs.value < rhs.value; });
    return state_->results;
}

std::span<const OccurrenceId> SceneSpatialIndex::query_ray(const Ray &ray) const {
    if (!state_)
        return {};
    state_->results.clear();
    if (state_->nodes.empty())
        return {};
    Ray normalized;
    if (!normalize_ray(ray, normalized))
        return {};
    visit_ray(state_->nodes, state_->entries, 0, normalized,
              [this](const Entry &entry) { state_->results.push_back(entry.occurrence); });
    std::sort(state_->results.begin(), state_->results.end(),
              [](OccurrenceId lhs, OccurrenceId rhs) { return lhs.value < rhs.value; });
    return state_->results;
}

std::size_t SceneSpatialIndex::query_result_count() const noexcept {
    return state_ ? state_->results.size() : 0;
}

OccurrenceId SceneSpatialIndex::query_result(std::size_t index) const noexcept {
    if (!state_ || index >= state_->results.size())
        return invalid_occurrence;
    return state_->results[index];
}

PickResult SceneSpatialIndex::pick_ray(const Ray &ray) const {
    PickResult result;
    Ray normalized;
    if (!normalize_ray(ray, normalized))
        return result;
    const auto candidates = query_ray(ray);
    auto best_distance = std::numeric_limits<float>::infinity();
    for (const auto occurrence_id : candidates) {
        const auto *occurrence = state_->snapshot.find(occurrence_id);
        if (!occurrence || !occurrence->visible || !occurrence->geometry.valid())
            continue;
        const auto *geometry = state_->snapshot.find_geometry(occurrence->geometry);
        if (!geometry)
            continue;
        const auto primitive_count = geometry->payload->element_count() / 3;
        for (std::size_t primitive = 0; primitive < primitive_count; ++primitive) {
            const auto first_index =
                static_cast<std::size_t>(vertex_index(*geometry->payload, primitive * 3));
            const auto second_index =
                static_cast<std::size_t>(vertex_index(*geometry->payload, primitive * 3 + 1));
            const auto third_index =
                static_cast<std::size_t>(vertex_index(*geometry->payload, primitive * 3 + 2));
            if (first_index >= geometry->payload->vertices.size() ||
                second_index >= geometry->payload->vertices.size() ||
                third_index >= geometry->payload->vertices.size())
                continue;
            float distance = 0.0f;
            if (!ray_hits_triangle(normalized,
                                   transform_point(occurrence->world_transform.transform,
                                                   geometry->payload->vertices[first_index]),
                                   transform_point(occurrence->world_transform.transform,
                                                   geometry->payload->vertices[second_index]),
                                   transform_point(occurrence->world_transform.transform,
                                                   geometry->payload->vertices[third_index]),
                                   distance) ||
                distance >= best_distance)
                continue;
            best_distance = distance;
            result.occurrence = occurrence->occurrence;
            result.source = occurrence->source;
            result.subelement = {geometry->subelements->id_for_primitive(primitive)};
            result.worldPosition = scale_add(normalized.origin, normalized.direction, distance);
            result.depth = distance;
        }
    }
    return result;
}

} // namespace nkscene
