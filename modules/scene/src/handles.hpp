#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

namespace nkscene {

struct RuntimeHandle {
    std::uint32_t slot = 0;
    std::uint32_t generation = 0;

    constexpr bool valid() const noexcept { return generation != 0; }
    friend constexpr bool operator==(RuntimeHandle, RuntimeHandle) noexcept = default;
};

constexpr std::uint32_t handle_index_bits = 20;
constexpr std::uint32_t handle_index_mask = (1u << handle_index_bits) - 1u;
constexpr std::uint32_t handle_max_generation = (1u << (32 - handle_index_bits)) - 1u;

inline std::uint32_t pack_handle(RuntimeHandle handle) noexcept {
    if (!handle.valid() || handle.slot >= handle_index_mask ||
        handle.generation > handle_max_generation)
        return 0;
    return (handle.generation << handle_index_bits) | (handle.slot + 1);
}

inline RuntimeHandle unpack_handle(std::uint32_t value) noexcept {
    if (value == 0 || (value & handle_index_mask) == 0)
        return {};
    return {static_cast<std::uint32_t>((value & handle_index_mask) - 1),
            static_cast<std::uint32_t>(value >> handle_index_bits)};
}

template<class T>
class HandleTable {
public:
    RuntimeHandle create(std::shared_ptr<T> value) {
        if (free_slots.empty()) {
            if (slots.size() >= handle_index_mask)
                return {};
            slots.push_back({std::move(value), 1});
            return {static_cast<std::uint32_t>(slots.size() - 1), 1};
        }
        const auto slot = free_slots.back();
        free_slots.pop_back();
        auto &entry = slots[slot];
        ++entry.generation;
        entry.value = std::move(value);
        return {slot, entry.generation};
    }

    std::shared_ptr<T> get(RuntimeHandle handle) const noexcept {
        if (!valid(handle))
            return {};
        return slots[handle.slot].value;
    }

    std::shared_ptr<T> remove(RuntimeHandle handle) noexcept {
        if (!valid(handle))
            return {};
        auto &entry = slots[handle.slot];
        auto result = std::move(entry.value);
        if (entry.generation < handle_max_generation)
            free_slots.push_back(handle.slot);
        return result;
    }

    bool valid(RuntimeHandle handle) const noexcept {
        return handle.valid() && handle.slot < slots.size() &&
            slots[handle.slot].generation == handle.generation &&
            slots[handle.slot].value != nullptr;
    }

    template<class Fn>
    void for_each(Fn &&fn) const {
        for (std::uint32_t index = 0; index < slots.size(); ++index) {
            const auto &entry = slots[index];
            if (entry.value)
                fn(RuntimeHandle{index, entry.generation}, entry.value);
        }
    }

private:
    struct Entry {
        std::shared_ptr<T> value;
        std::uint32_t generation = 1;
    };

    std::vector<Entry> slots;
    std::vector<std::uint32_t> free_slots;
};

} // namespace nkscene
