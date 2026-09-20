#pragma once

#include "ids.hpp"

#include <cstddef>
#include <unordered_map>

namespace nkscene {

class GeometryStore {
public:
    GeometryResource &create(GeometryId id) {
        auto [found, inserted] = resources.emplace(id, GeometryResource{id});
        if (!inserted)
            ++found->second.revision;
        return found->second;
    }

    bool destroy(GeometryId id) noexcept { return resources.erase(id) != 0; }

    const GeometryResource *find(GeometryId id) const noexcept {
        const auto found = resources.find(id);
        return found == resources.end() ? nullptr : &found->second;
    }

    GeometryResource *find(GeometryId id) noexcept {
        const auto found = resources.find(id);
        return found == resources.end() ? nullptr : &found->second;
    }

    std::size_t size() const noexcept { return resources.size(); }

    template<class Fn>
    void for_each(Fn &&fn) const {
        for (const auto &[id, resource] : resources)
            fn(id, resource);
    }

private:
    std::unordered_map<GeometryId, GeometryResource> resources;
};

class MaterialStore {
public:
    MaterialResource &create(MaterialId id) {
        auto [found, inserted] = resources.emplace(id, MaterialResource{id});
        if (!inserted)
            ++found->second.revision;
        return found->second;
    }

    bool destroy(MaterialId id) noexcept { return resources.erase(id) != 0; }

    const MaterialResource *find(MaterialId id) const noexcept {
        const auto found = resources.find(id);
        return found == resources.end() ? nullptr : &found->second;
    }

    MaterialResource *find(MaterialId id) noexcept {
        const auto found = resources.find(id);
        return found == resources.end() ? nullptr : &found->second;
    }

    std::size_t size() const noexcept { return resources.size(); }

    template<class Fn>
    void for_each(Fn &&fn) const {
        for (const auto &[id, resource] : resources)
            fn(id, resource);
    }

private:
    std::unordered_map<MaterialId, MaterialResource> resources;
};

} // namespace nkscene
