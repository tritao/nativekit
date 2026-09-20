#pragma once

#include "ids.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace nkscene {

enum class ChangeDomain : std::uint32_t {
    None = 0,
    Created = 1u << 0,
    Destroyed = 1u << 1,
    Transform = 1u << 2,
    Hierarchy = 1u << 3,
    Geometry = 1u << 4,
    Material = 1u << 5,
    Visibility = 1u << 6,
    Bounds = 1u << 7
};

constexpr ChangeDomain operator|(ChangeDomain lhs, ChangeDomain rhs) noexcept {
    return static_cast<ChangeDomain>(static_cast<std::uint32_t>(lhs) |
                                     static_cast<std::uint32_t>(rhs));
}

constexpr ChangeDomain &operator|=(ChangeDomain &lhs, ChangeDomain rhs) noexcept {
    lhs = lhs | rhs;
    return lhs;
}

constexpr bool has_domain(ChangeDomain value, ChangeDomain domain) noexcept {
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(domain)) != 0;
}

struct SceneChange {
    OccurrenceId occurrence;
    ChangeDomain domains = ChangeDomain::None;
};

struct RevisionCounters {
    std::uint64_t scene = 0;
    std::uint64_t hierarchy = 0;
    std::uint64_t transform = 0;
    std::uint64_t geometry = 0;
    std::uint64_t material = 0;
    std::uint64_t visibility = 0;
    std::uint64_t bounds = 0;
};

struct ChangeStats {
    std::size_t changed_occurrences = 0;
    std::size_t changed_resources = 0;
    std::size_t dirty_world_transforms = 0;
    std::size_t dirty_bounds = 0;
    std::size_t full_rebuilds = 0;
};

struct ChangeSet {
    std::uint64_t scene_revision = 0;
    RevisionCounters revisions;
    ChangeStats stats;
    std::vector<SceneChange> changes;
};

} // namespace nkscene
