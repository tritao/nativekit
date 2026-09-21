#pragma once

#include "ids.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nkscene {

class GeometryStore {
public:
    GeometryResource &create(GeometryId id) {
        auto found = resources.find(id);
        if (found != resources.end()) {
            ++found->second.revision;
            found->second.mutation_state = mutation_state;
            mutation_state->mark(id);
            return found->second;
        }
        GeometryResource resource{id};
        resource.mutation_state = mutation_state;
        auto [inserted, unused] = resources.emplace(id, std::move(resource));
        mutation_state->mark(id);
        return inserted->second;
    }

    bool destroy(GeometryId id) noexcept {
        if (resources.erase(id) == 0)
            return false;
        mutation_state->mark(id);
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
    std::uint64_t revision() const noexcept { return mutation_state->current_revision(); }

    void changes_since(std::uint64_t previous_revision, std::vector<GeometryId> &out) const {
        mutation_state->changes_since(previous_revision, out);
    }

    void discard_mutations_through(std::uint64_t revision) const {
        mutation_state->discard_through(revision);
    }

    template<class Fn>
    void for_each(Fn &&fn) const {
        for (const auto &[id, resource] : resources)
            fn(id, resource);
    }

private:
    std::unordered_map<GeometryId, GeometryResource> resources;
    std::shared_ptr<ResourceMutationState<GeometryId>> mutation_state =
        std::make_shared<ResourceMutationState<GeometryId>>();
};

class MaterialStore {
public:
    MaterialResource &create(MaterialId id) {
        auto found = resources.find(id);
        if (found != resources.end()) {
            ++found->second.revision;
            found->second.mutation_state = mutation_state;
            mutation_state->mark(id);
            return found->second;
        }
        MaterialResource resource{id};
        resource.mutation_state = mutation_state;
        auto [inserted, unused] = resources.emplace(id, std::move(resource));
        mutation_state->mark(id);
        return inserted->second;
    }

    bool destroy(MaterialId id) noexcept {
        if (resources.erase(id) == 0)
            return false;
        mutation_state->mark(id);
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
    std::uint64_t revision() const noexcept { return mutation_state->current_revision(); }

    void changes_since(std::uint64_t previous_revision, std::vector<MaterialId> &out) const {
        mutation_state->changes_since(previous_revision, out);
    }

    void discard_mutations_through(std::uint64_t revision) const {
        mutation_state->discard_through(revision);
    }

    template<class Fn>
    void for_each(Fn &&fn) const {
        for (const auto &[id, resource] : resources)
            fn(id, resource);
    }

private:
    std::unordered_map<MaterialId, MaterialResource> resources;
    std::shared_ptr<ResourceMutationState<MaterialId>> mutation_state =
        std::make_shared<ResourceMutationState<MaterialId>>();
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
