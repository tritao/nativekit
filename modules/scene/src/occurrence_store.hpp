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

    bool destroy(OccurrenceHandle handle) noexcept {
        if (!handle.valid() || handle.slot >= slots.size())
            return false;
        auto &entry = slots[handle.slot];
        if (!entry.live || entry.generation != handle.generation)
            return false;
        entry.live = false;
        by_id.erase(entry.id);
        free_slots.push_back(handle.slot);
        return true;
    }

    bool destroy(OccurrenceId id) noexcept { return destroy(resolve(id)); }

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
    std::size_t slot_count() const noexcept { return slots.size(); }

    template<class Fn>
    void for_each(Fn &&fn) const {
        for (std::uint32_t slot = 0; slot < slots.size(); ++slot) {
            const auto &entry = slots[slot];
            if (entry.live)
                fn(entry.id, OccurrenceHandle{slot, entry.generation});
        }
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
