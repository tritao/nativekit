#include "wasm/host_allocator.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

bool check(bool condition, const char *message) {
    if (!condition)
        std::fprintf(stderr, "wasm_host_allocator: %s\n", message);
    return condition;
}

bool aligned(const void *pointer, std::size_t alignment) {
    return reinterpret_cast<std::uintptr_t>(pointer) % alignment == 0;
}

bool basic_allocations() {
    alignas(64) std::array<std::byte, 16 * 1024> storage{};
    nk::wasm::WasmHostAllocator allocator;
    if (!check(allocator.initialize(storage.data(), storage.size()), "initialize failed"))
        return false;
    auto *first = static_cast<std::uint8_t *>(allocator.allocate(31));
    auto *second = static_cast<std::uint8_t *>(allocator.allocate(257));
    if (!check(first && second, "basic allocation failed") ||
        !check(aligned(first, 16) && aligned(second, 16), "allocation alignment failed"))
        return false;
    std::memset(first, 0xa5, 31);
    std::memset(second, 0x5a, 257);
    allocator.release(first);
    allocator.release(second);
    return check(allocator.allocate(512) != nullptr, "coalescing did not recover freed blocks");
}

bool usable_allocation_capacity() {
    alignas(64) std::array<std::byte, 16 * 1024> storage{};
    nk::wasm::WasmHostAllocator allocator;
    if (!check(allocator.usable_size(nullptr) == 0, "uninitialized capacity")) return false;
    if (!allocator.initialize(storage.data(), storage.size())) return false;
    if (!check(allocator.usable_size(nullptr) == 0, "null capacity")) return false;
    auto *pointer = allocator.allocate(31);
    if (!check(pointer && allocator.usable_size(pointer) >= 31, "allocation capacity")) return false;
    std::memset(pointer, 0xa5, allocator.usable_size(pointer));
    pointer = allocator.reallocate(pointer, 257);
    if (!check(pointer && allocator.usable_size(pointer) >= 257, "reallocation capacity")) return false;
    std::memset(pointer, 0x5a, allocator.usable_size(pointer));
    auto *other = allocator.allocate_aligned(256, 300);
    if (!check(other && allocator.usable_size(other) >= 300, "aligned allocation capacity")) return false;
    std::memset(other, 0x33, allocator.usable_size(other));
    allocator.release(other);
    allocator.release(pointer);
    return check(allocator.statistics().in_use == 0, "capacity query changed accounting");
}

bool realloc_preserves_data() {
    alignas(64) std::array<std::byte, 16 * 1024> storage{};
    nk::wasm::WasmHostAllocator allocator;
    if (!allocator.initialize(storage.data(), storage.size()))
        return false;
    auto *pointer = static_cast<std::uint8_t *>(allocator.allocate(64));
    if (!check(pointer != nullptr, "realloc source allocation failed"))
        return false;
    for (std::size_t index = 0; index < 64; ++index)
        pointer[index] = static_cast<std::uint8_t>(index);
    auto *grown = static_cast<std::uint8_t *>(allocator.reallocate(pointer, 1024));
    if (!check(grown != nullptr, "realloc growth failed"))
        return false;
    for (std::size_t index = 0; index < 64; ++index)
        if (grown[index] != static_cast<std::uint8_t>(index))
            return check(false, "realloc did not preserve data");
    allocator.release(grown);
    return true;
}

bool aligned_allocations() {
    alignas(64) std::array<std::byte, 16 * 1024> storage{};
    nk::wasm::WasmHostAllocator allocator;
    if (!allocator.initialize(storage.data(), storage.size()))
        return false;
    auto *pointer = allocator.allocate_aligned(256, 300);
    if (!check(pointer != nullptr && aligned(pointer, 256), "aligned allocation failed"))
        return false;
    allocator.release(pointer);
    return true;
}

bool exhaustion_is_bounded() {
    // The heap's own state (about half a kilobyte) lives in the arena too.
    alignas(64) std::array<std::byte, 8192> storage{};
    nk::wasm::WasmHostAllocator allocator;
    if (!allocator.initialize(storage.data(), storage.size()))
        return false;
    auto *large = allocator.allocate(7000);
    if (!check(large != nullptr, "bounded arena could not use available space"))
        return false;
    if (!check(allocator.allocate(2000) == nullptr, "allocator crossed the arena limit"))
        return false;
    allocator.release(large);
    return check(allocator.allocate(7000) != nullptr, "freed bounded space was not reusable");
}

bool churn_keeps_large_blocks() {
    // Small and large blocks allocated and freed in turns, as a renderer does each frame: the free
    // space must not splinter, so the large block keeps fitting and everything coalesces at the
    // end.
    alignas(64) std::array<std::byte, 256 * 1024> storage{};
    nk::wasm::WasmHostAllocator allocator;
    if (!allocator.initialize(storage.data(), storage.size()))
        return false;
    for (int round = 0; round < 200; ++round) {
        void *small[16];
        for (auto &pointer : small)
            pointer = allocator.allocate(48 + round % 7 * 16);
        auto *large = allocator.allocate(96 * 1024);
        if (!check(large != nullptr, "a large block failed after churn"))
            return false;
        for (int index = 0; index < 16; index += 2)
            allocator.release(small[index]);
        allocator.release(large);
        for (int index = 1; index < 16; index += 2)
            allocator.release(small[index]);
    }
    const auto stats = allocator.statistics();
    return check(stats.in_use == 0, "statistics did not return to zero") &&
           check(stats.largest_free > 200 * 1024, "free space stayed fragmented");
}

} // namespace

int main() {
    return basic_allocations() && usable_allocation_capacity() && realloc_preserves_data() && aligned_allocations() &&
                   exhaustion_is_bounded() && churn_keeps_large_blocks()
               ? 0
               : 1;
}
