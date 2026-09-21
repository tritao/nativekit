#pragma once

#include "occurrence_store.hpp"

#include <cstdint>
#include <limits>
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
        const auto was_empty = !entry.value;
        entry.value = std::move(value);
        if (was_empty) {
            dense_positions_[handle.slot] = static_cast<std::uint32_t>(dense_slots_.size());
            dense_slots_.push_back(handle.slot);
            ++size_;
        }
        return *entry.value;
    }

    bool erase(OccurrenceHandle handle) noexcept {
        auto *entry = slot(handle);
        if (!entry || !entry->value)
            return false;
        remove_dense_slot(handle.slot);
        entry->value.reset();
        --size_;
        return true;
    }

    void clear() noexcept {
        slots.clear();
        dense_slots_.clear();
        dense_positions_.clear();
        size_ = 0;
    }

    std::size_t size() const noexcept { return size_; }

    template<class Fn>
    void for_each(Fn &&fn) const {
        for (const auto index : dense_slots_) {
            const auto &entry = slots[index];
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
        if (handle.slot >= slots.size()) {
            const auto new_size = static_cast<std::size_t>(handle.slot) + 1;
            slots.resize(new_size);
            dense_positions_.resize(new_size, invalid_dense_position);
        }
        auto &entry = slots[handle.slot];
        if (entry.generation != handle.generation) {
            if (entry.value) {
                remove_dense_slot(handle.slot);
                --size_;
            }
            entry.generation = handle.generation;
            entry.value.reset();
        }
        return entry;
    }

    void remove_dense_slot(std::uint32_t slot) noexcept {
        const auto position = dense_positions_[slot];
        if (position == invalid_dense_position)
            return;
        const auto last = dense_slots_.back();
        dense_slots_[position] = last;
        dense_positions_[last] = position;
        dense_slots_.pop_back();
        dense_positions_[slot] = invalid_dense_position;
    }

    static constexpr std::uint32_t invalid_dense_position =
        std::numeric_limits<std::uint32_t>::max();
    std::vector<Slot> slots;
    std::vector<std::uint32_t> dense_slots_;
    std::vector<std::uint32_t> dense_positions_;
    std::size_t size_ = 0;
};

} // namespace nkscene
