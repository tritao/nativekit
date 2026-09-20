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

inline std::uint64_t pack_handle(RuntimeHandle handle) noexcept {
    if (!handle.valid())
        return 0;
    return (static_cast<std::uint64_t>(handle.generation) << 32) |
        static_cast<std::uint64_t>(handle.slot + 1);
}

inline RuntimeHandle unpack_handle(std::uint64_t value) noexcept {
    if (value == 0)
        return {};
    return {static_cast<std::uint32_t>((value & 0xffffffffu) - 1),
            static_cast<std::uint32_t>(value >> 32)};
}

template<class T>
class HandleTable {
public:
    RuntimeHandle create(std::shared_ptr<T> value) {
        if (free_slots.empty()) {
            slots.push_back({std::move(value), 1});
            return {static_cast<std::uint32_t>(slots.size() - 1), 1};
        }
        const auto slot = free_slots.back();
        free_slots.pop_back();
        auto &entry = slots[slot];
        if (++entry.generation == 0)
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
