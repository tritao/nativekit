#pragma once

#include <cstddef>
#include <cstdint>

namespace nk::wasm {

/**
 * A bounded allocator for a caller-owned linear-memory arena: dlmalloc in mspace-only mode
 * (host_mspace.c).
 *
 * The allocator never grows the arena and never accesses memory outside the range supplied to
 * initialize(). The Emscripten host adapter uses it for a bounded host-memory partition; native
 * tests use an aligned byte buffer. Allocations are 16-byte aligned.
 */
class WasmHostAllocator {
  public:
    WasmHostAllocator() = default;
    WasmHostAllocator(const WasmHostAllocator &) = delete;
    WasmHostAllocator &operator=(const WasmHostAllocator &) = delete;

    bool initialize(void *base, std::size_t size);
    bool valid() const;

    void *allocate(std::size_t size);
    void release(void *pointer);
    void *reallocate(void *pointer, std::size_t size);
    void *allocate_aligned(std::size_t alignment, std::size_t size);

    /** Bytes: the arena, the usable bytes allocated now and at most, and the free space. */
    struct Statistics {
        std::size_t arena = 0;
        std::size_t in_use = 0;
        std::size_t peak = 0;
        std::size_t largest_free = 0;
        std::size_t free_blocks = 0;
    };
    /** Walks the whole heap for the free-space figures; for diagnostics, not every frame. */
    Statistics statistics() const;

  private:
    void track_allocation(void *pointer);
    void track_release(void *pointer);

    void *space_ = nullptr;
    std::size_t arena_ = 0;
    std::size_t in_use_ = 0;
    std::size_t peak_ = 0;
    bool initialized_ = false;
};

} // namespace nk::wasm
