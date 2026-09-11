#include "platform/wasm/host_allocator.h"

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
    nkui::WasmHostAllocator allocator;
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

bool realloc_preserves_data() {
    alignas(64) std::array<std::byte, 16 * 1024> storage{};
    nkui::WasmHostAllocator allocator;
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
    nkui::WasmHostAllocator allocator;
    if (!allocator.initialize(storage.data(), storage.size()))
        return false;
    auto *pointer = allocator.allocate_aligned(256, 300);
    if (!check(pointer != nullptr && aligned(pointer, 256), "aligned allocation failed"))
        return false;
    allocator.release(pointer);
    return true;
}

bool exhaustion_is_bounded() {
    alignas(64) std::array<std::byte, 4096> storage{};
    nkui::WasmHostAllocator allocator;
    if (!allocator.initialize(storage.data(), storage.size()))
        return false;
    auto *large = allocator.allocate(3900);
    if (!check(large != nullptr, "bounded arena could not use available space"))
        return false;
    if (!check(allocator.allocate(200) == nullptr, "allocator crossed the arena limit"))
        return false;
    allocator.release(large);
    return check(allocator.allocate(3900) != nullptr, "freed bounded space was not reusable");
}

} // namespace

int main() {
    return basic_allocations() && realloc_preserves_data() && aligned_allocations() && exhaustion_is_bounded() ? 0 : 1;
}
