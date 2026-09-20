#pragma once

#include "ids.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace nkscene {

struct Bounds {
    std::array<float, 3> minimum{0.0f, 0.0f, 0.0f};
    std::array<float, 3> maximum{0.0f, 0.0f, 0.0f};
    bool valid = false;
};

struct GeometryPayload {
    std::vector<std::byte> bytes;
};

struct SubelementTable {
    std::vector<std::uint32_t> offsets;
};

struct GeometryResource {
    GeometryId id;
    std::uint64_t revision = 1;
    Bounds bounds;
    GeometryPayload payload;
    SubelementTable subelements;
};

struct MaterialResource {
    MaterialId id;
    std::uint64_t revision = 1;
};

class GeometryStore {
public:
    const GeometryResource *find(GeometryId id) const noexcept {
        const auto found = resources.find(id);
        return found == resources.end() ? nullptr : &found->second;
    }

    GeometryResource *find(GeometryId id) noexcept {
        const auto found = resources.find(id);
        return found == resources.end() ? nullptr : &found->second;
    }

private:
    std::unordered_map<GeometryId, GeometryResource> resources;
};

class MaterialStore {
public:
    const MaterialResource *find(MaterialId id) const noexcept {
        const auto found = resources.find(id);
        return found == resources.end() ? nullptr : &found->second;
    }

    MaterialResource *find(MaterialId id) noexcept {
        const auto found = resources.find(id);
        return found == resources.end() ? nullptr : &found->second;
    }

private:
    std::unordered_map<MaterialId, MaterialResource> resources;
};

} // namespace nkscene
