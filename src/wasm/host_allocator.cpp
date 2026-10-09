#include "wasm/host_allocator.h"

#include "wasm/host_mspace.h"

namespace nk::wasm {

bool WasmHostAllocator::initialize(void *base, std::size_t size) {
    if (initialized_)
        return valid();
    initialized_ = true;
    if (!base || size == 0)
        return false;
    space_ = nk_host_mspace_create(base, size);
    arena_ = space_ ? size : 0;
    return valid();
}

bool WasmHostAllocator::valid() const {
    return space_ != nullptr;
}

std::size_t WasmHostAllocator::usable_size(const void *pointer) const {
    return valid() && pointer ? nk_host_mspace_usable_size(pointer) : 0;
}

void WasmHostAllocator::track_allocation(void *pointer) {
    if (!pointer)
        return;
    in_use_ += nk_host_mspace_usable_size(pointer);
    if (in_use_ > peak_)
        peak_ = in_use_;
}

void WasmHostAllocator::track_release(void *pointer) {
    if (pointer)
        in_use_ -= nk_host_mspace_usable_size(pointer);
}

void *WasmHostAllocator::allocate(std::size_t size) {
    if (!valid())
        return nullptr;
    auto *pointer = nk_host_mspace_malloc(space_, size);
    track_allocation(pointer);
    return pointer;
}

void WasmHostAllocator::release(void *pointer) {
    if (!pointer || !valid())
        return;
    track_release(pointer);
    nk_host_mspace_free(space_, pointer);
}

void *WasmHostAllocator::reallocate(void *pointer, std::size_t size) {
    if (!valid())
        return nullptr;
    if (!pointer)
        return allocate(size);
    if (size == 0) {
        release(pointer);
        return nullptr;
    }
    const auto previous = nk_host_mspace_usable_size(pointer);
    auto *result = nk_host_mspace_realloc(space_, pointer, size);
    if (result) {
        in_use_ -= previous;
        track_allocation(result);
    }
    return result;
}

void *WasmHostAllocator::allocate_aligned(std::size_t alignment, std::size_t size) {
    if (!valid() || alignment == 0 || (alignment & (alignment - 1)) != 0)
        return nullptr;
    auto *pointer = nk_host_mspace_memalign(space_, alignment, size);
    track_allocation(pointer);
    return pointer;
}

WasmHostAllocator::Statistics WasmHostAllocator::statistics() const {
    Statistics result;
    if (!valid())
        return result;
    const auto free_space = nk_host_mspace_inspect_free(space_);
    result.arena = arena_;
    result.in_use = in_use_;
    result.peak = peak_;
    result.largest_free = free_space.largest;
    result.free_blocks = free_space.blocks;
    return result;
}

} // namespace nk::wasm
