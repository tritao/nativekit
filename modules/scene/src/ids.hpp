#pragma once

#include <cstdint>

namespace nkscene {

template<class Tag>
struct Id {
    std::uint64_t value = 0;

    constexpr bool valid() const noexcept { return value != 0; }
    friend constexpr bool operator==(Id, Id) noexcept = default;
    friend constexpr bool operator!=(Id lhs, Id rhs) noexcept { return !(lhs == rhs); }
};

struct OccurrenceTag;
struct EntityTag;
struct GeometryTag;
struct MaterialTag;

using OccurrenceId = Id<OccurrenceTag>;
using EntityId = Id<EntityTag>;
using GeometryId = Id<GeometryTag>;
using MaterialId = Id<MaterialTag>;

template<class Tag>
struct IdHash {
    std::size_t operator()(Id<Tag> id) const noexcept {
        return static_cast<std::size_t>(id.value ^ (id.value >> 32));
    }
};

constexpr OccurrenceId invalid_occurrence{};

} // namespace nkscene

namespace std {

template<class Tag>
struct hash<nkscene::Id<Tag>> {
    std::size_t operator()(nkscene::Id<Tag> id) const noexcept {
        return nkscene::IdHash<Tag>{}(id);
    }
};

} // namespace std
