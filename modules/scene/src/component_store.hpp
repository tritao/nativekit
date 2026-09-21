#pragma once

#include "occurrence_store.hpp"

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace nkscene {

template<class T>
class ComponentStore {
public:
    bool contains(OccurrenceHandle handle) const noexcept {
        return find(handle) != nullptr;
    }

    T *find(OccurrenceHandle handle) noexcept {
        auto *entry = slot(handle);
        return entry && entry->value ? &*entry->value : nullptr;
    }

    const T *find(OccurrenceHandle handle) const noexcept {
        const auto *entry = slot(handle);
        return entry && entry->value ? &*entry->value : nullptr;
    }

    T &insert_or_assign(OccurrenceHandle handle, T value) {
        auto &entry = ensure_slot(handle);
        if (!entry.value)
            ++size_;
        entry.generation = handle.generation;
        entry.value = std::move(value);
        return *entry.value;
    }

    bool erase(OccurrenceHandle handle) noexcept {
        auto *entry = slot(handle);
        if (!entry || !entry->value)
            return false;
        entry->value.reset();
        --size_;
        return true;
    }

    void clear() noexcept {
        slots.clear();
        size_ = 0;
    }

    std::size_t size() const noexcept { return size_; }

    template<class Fn>
    void for_each(Fn &&fn) const {
        for (std::uint32_t index = 0; index < slots.size(); ++index) {
            const auto &entry = slots[index];
            if (entry.value)
                fn(OccurrenceHandle{index, entry.generation}, *entry.value);
        }
    }

private:
    struct Slot {
        std::uint32_t generation = 0;
        std::optional<T> value;
    };

    Slot *slot(OccurrenceHandle handle) noexcept {
        if (!handle.valid() || handle.slot >= slots.size())
            return nullptr;
        auto &entry = slots[handle.slot];
        return entry.generation == handle.generation ? &entry : nullptr;
    }

    const Slot *slot(OccurrenceHandle handle) const noexcept {
        if (!handle.valid() || handle.slot >= slots.size())
            return nullptr;
        const auto &entry = slots[handle.slot];
        return entry.generation == handle.generation ? &entry : nullptr;
    }

    Slot &ensure_slot(OccurrenceHandle handle) {
        if (!handle.valid())
            throw std::invalid_argument("cannot store a component for an invalid occurrence handle");
        if (handle.slot >= slots.size())
            slots.resize(static_cast<std::size_t>(handle.slot) + 1);
        auto &entry = slots[handle.slot];
        if (entry.generation != handle.generation) {
            if (entry.value)
                --size_;
            entry.generation = handle.generation;
            entry.value.reset();
        }
        return entry;
    }

    std::vector<Slot> slots;
    std::size_t size_ = 0;
};

} // namespace nkscene
