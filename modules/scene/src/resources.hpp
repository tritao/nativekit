#pragma once

#include "ids.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>

namespace nkscene {

class GeometryStore {
public:
    GeometryResource &create(GeometryId id) {
        auto [found, inserted] = resources.emplace(id, GeometryResource{id});
        if (!inserted)
            ++found->second.revision;
        ++revision_counter;
        return found->second;
    }

    bool destroy(GeometryId id) noexcept {
        if (resources.erase(id) == 0)
            return false;
        ++revision_counter;
        return true;
    }

    const GeometryResource *find(GeometryId id) const noexcept {
        const auto found = resources.find(id);
        return found == resources.end() ? nullptr : &found->second;
    }

    GeometryResource *find(GeometryId id) noexcept {
        const auto found = resources.find(id);
        return found == resources.end() ? nullptr : &found->second;
    }

    std::size_t size() const noexcept { return resources.size(); }
    std::uint64_t revision() const noexcept { return revision_counter; }

    bool revisions_match(std::span<const GeometryResource> published) const noexcept {
        if (published.size() != resources.size())
            return false;
        for (const auto &resource : published) {
            const auto found = resources.find(resource.id);
            if (found == resources.end() || found->second.revision != resource.revision)
                return false;
        }
        return true;
    }

    template<class Fn>
    void for_each(Fn &&fn) const {
        for (const auto &[id, resource] : resources)
            fn(id, resource);
    }

private:
    std::unordered_map<GeometryId, GeometryResource> resources;
    std::uint64_t revision_counter = 0;
};

class MaterialStore {
public:
    MaterialResource &create(MaterialId id) {
        auto [found, inserted] = resources.emplace(id, MaterialResource{id});
        if (!inserted)
            ++found->second.revision;
        ++revision_counter;
        return found->second;
    }

    bool destroy(MaterialId id) noexcept {
        if (resources.erase(id) == 0)
            return false;
        ++revision_counter;
        return true;
    }

    const MaterialResource *find(MaterialId id) const noexcept {
        const auto found = resources.find(id);
        return found == resources.end() ? nullptr : &found->second;
    }

    MaterialResource *find(MaterialId id) noexcept {
        const auto found = resources.find(id);
        return found == resources.end() ? nullptr : &found->second;
    }

    std::size_t size() const noexcept { return resources.size(); }
    std::uint64_t revision() const noexcept { return revision_counter; }

    bool revisions_match(std::span<const MaterialResource> published) const noexcept {
        if (published.size() != resources.size())
            return false;
        for (const auto &resource : published) {
            const auto found = resources.find(resource.id);
            if (found == resources.end() || found->second.revision != resource.revision)
                return false;
        }
        return true;
    }

    template<class Fn>
    void for_each(Fn &&fn) const {
        for (const auto &[id, resource] : resources)
            fn(id, resource);
    }

private:
    std::unordered_map<MaterialId, MaterialResource> resources;
    std::uint64_t revision_counter = 0;
};

template<class Resource, class Id>
class ResourceStore {
public:
    Resource &create(Id id) {
        auto [found, inserted] = resources.emplace(id, Resource{id});
        if (!inserted)
            ++found->second.revision;
        return found->second;
    }

    bool destroy(Id id) noexcept { return resources.erase(id) != 0; }

    const Resource *find(Id id) const noexcept {
        const auto found = resources.find(id);
        return found == resources.end() ? nullptr : &found->second;
    }

    Resource *find(Id id) noexcept {
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
    std::unordered_map<Id, Resource> resources;
};

using ImageStore = ResourceStore<ImageResource, ImageId>;
using TextureStore = ResourceStore<TextureResource, TextureId>;
using SamplerStore = ResourceStore<SamplerResource, SamplerId>;
using CameraStore = ResourceStore<CameraResource, CameraId>;
using LightStore = ResourceStore<LightResource, LightId>;

} // namespace nkscene
