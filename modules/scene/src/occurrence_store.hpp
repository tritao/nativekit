#pragma once

#include "ids.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace nkscene {

struct OccurrenceHandle {
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;

    constexpr bool valid() const noexcept { return generation != 0; }
    friend constexpr bool operator==(OccurrenceHandle, OccurrenceHandle) noexcept = default;
};

class OccurrenceStore {
public:
    OccurrenceId reserve_id() noexcept { return {next_id++}; }

    OccurrenceHandle create(OccurrenceId id) {
        if (!id.valid() || by_id.contains(id))
            return {};
        std::uint32_t slot = 0;
        if (free_slots.empty()) {
            slot = static_cast<std::uint32_t>(slots.size());
            slots.push_back({id, 1, true});
        } else {
            slot = free_slots.back();
            free_slots.pop_back();
            auto &entry = slots[slot];
            if (++entry.generation == 0)
                ++entry.generation;
            entry.id = id;
            entry.live = true;
        }
        const OccurrenceHandle handle{slot, slots[slot].generation};
        by_id.emplace(id, handle);
        return handle;
    }

    bool destroy(OccurrenceId id) noexcept {
        const auto found = by_id.find(id);
        if (found == by_id.end())
            return false;
        const auto slot = found->second.slot;
        auto &entry = slots[slot];
        entry.live = false;
        by_id.erase(found);
        free_slots.push_back(slot);
        return true;
    }

    OccurrenceHandle resolve(OccurrenceId id) const noexcept {
        const auto found = by_id.find(id);
        return found == by_id.end() ? OccurrenceHandle{} : found->second;
    }

    OccurrenceId id(OccurrenceHandle handle) const noexcept {
        if (!handle.valid() || handle.slot >= slots.size())
            return invalid_occurrence;
        const auto &entry = slots[handle.slot];
        return entry.live && entry.generation == handle.generation ? entry.id
                                                                     : invalid_occurrence;
    }

    bool contains(OccurrenceId id) const noexcept { return by_id.contains(id); }
    std::size_t size() const noexcept { return by_id.size(); }

    template<class Fn>
    void for_each(Fn &&fn) const {
        for (const auto &[id, handle] : by_id)
            fn(id, handle);
    }

private:
    struct Slot {
        OccurrenceId id;
        std::uint32_t generation = 1;
        bool live = false;
    };

    std::uint64_t next_id = 1;
    std::vector<Slot> slots;
    std::vector<std::uint32_t> free_slots;
    std::unordered_map<OccurrenceId, OccurrenceHandle> by_id;
};

} // namespace nkscene
