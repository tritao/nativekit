#pragma once

#include "nativekit_scene.hpp"

#include <functional>

namespace nkscene {

template<class Tag>
struct IdHash {
    std::size_t operator()(Id<Tag> id) const noexcept {
        return static_cast<std::size_t>(id.value ^ (id.value >> 32));
    }
};

} // namespace nkscene

namespace std {

template<class Tag>
struct hash<nkscene::Id<Tag>> {
    std::size_t operator()(nkscene::Id<Tag> id) const noexcept {
        return nkscene::IdHash<Tag>{}(id);
    }
};

} // namespace std
