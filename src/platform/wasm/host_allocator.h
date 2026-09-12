#pragma once

#include <cstddef>
#include <cstdint>

namespace nk::wasm {

/**
 * A bounded first-fit allocator for a caller-owned linear-memory arena.
 *
 * The allocator never grows the arena and never accesses memory outside the
 * range supplied to initialize(). The Emscripten host adapter uses it for a
 * bounded host-memory partition; native tests use an aligned byte buffer.
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

  private:
    struct BlockHeader;
    struct BlockFooter;
    struct AlignedPrefix;

    static constexpr std::uintptr_t kAlignment = 16;
    static constexpr uint32_t kHeaderMagic = 0x4e4b4842; // NKHB
    static constexpr uint32_t kFooterMagic = 0x4e4b4654; // NKFT
    static constexpr uint32_t kAlignedMagic = 0x4e4b414c; // NKAL
    static constexpr uint32_t kAllocated = 1;

    std::uintptr_t align_up(std::uintptr_t value, std::uintptr_t alignment) const;
    std::uintptr_t align_down(std::uintptr_t value, std::uintptr_t alignment) const;
    std::size_t minimum_block_size() const;
    void write_footer(BlockHeader *block);
    bool in_arena(std::uintptr_t address, std::uintptr_t bytes = 1) const;
    bool valid_block(BlockHeader *block) const;
    bool is_free(BlockHeader *block) const;
    BlockHeader *next_block(BlockHeader *block) const;
    BlockHeader *previous_block(BlockHeader *block) const;
    void remove_free(BlockHeader *target);
    void insert_free(BlockHeader *block);
    std::size_t block_size_for(std::size_t payload_size) const;
    void split_allocated(BlockHeader *block, std::size_t requested_size);
    BlockHeader *header_from_payload(void *pointer) const;
    AlignedPrefix *aligned_prefix(void *pointer) const;
    void *allocate_unchecked(std::size_t size);
    void release_unchecked(void *pointer);

    BlockHeader *free_head_ = nullptr;
    std::uintptr_t arena_begin_ = 0;
    std::uintptr_t arena_end_ = 0;
    bool initialized_ = false;
    bool ready_ = false;
};

} // namespace nk::wasm
