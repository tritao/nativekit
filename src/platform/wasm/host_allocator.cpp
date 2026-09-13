#include "platform/wasm/host_allocator.h"

#include <cstdlib>
#include <cstring>
#include <limits>

namespace nk::wasm {

struct alignas(16) WasmHostAllocator::BlockHeader {
    uint32_t magic;
    uint32_t flags;
    std::size_t size;
    BlockHeader *next_free;
};

struct WasmHostAllocator::BlockFooter {
    uint32_t magic;
    std::size_t size;
};

struct WasmHostAllocator::AlignedPrefix {
    uint32_t magic;
    std::size_t size;
    void *raw;
};

std::uintptr_t WasmHostAllocator::align_up(std::uintptr_t value, std::uintptr_t alignment) const {
    return (value + alignment - 1) & ~(alignment - 1);
}

std::uintptr_t WasmHostAllocator::align_down(std::uintptr_t value, std::uintptr_t alignment) const {
    return value & ~(alignment - 1);
}

std::size_t WasmHostAllocator::minimum_block_size() const {
    return static_cast<std::size_t>(
        align_up(sizeof(BlockHeader) + sizeof(BlockFooter) + 1, kAlignment));
}

void WasmHostAllocator::write_footer(BlockHeader *block) {
    auto *footer = reinterpret_cast<BlockFooter *>(reinterpret_cast<uint8_t *>(block) +
                                                   block->size - sizeof(BlockFooter));
    footer->magic = kFooterMagic;
    footer->size = block->size;
}

bool WasmHostAllocator::in_arena(std::uintptr_t address, std::uintptr_t bytes) const {
    return address >= arena_begin_ && address <= arena_end_ && bytes <= arena_end_ - address;
}

bool WasmHostAllocator::valid_block(BlockHeader *block) const {
    if (!block || !in_arena(reinterpret_cast<std::uintptr_t>(block), sizeof(BlockHeader)))
        return false;
    if (block->magic != kHeaderMagic || block->size < minimum_block_size() ||
        block->size % kAlignment != 0 ||
        !in_arena(reinterpret_cast<std::uintptr_t>(block), block->size))
        return false;
    auto *footer = reinterpret_cast<BlockFooter *>(reinterpret_cast<uint8_t *>(block) +
                                                   block->size - sizeof(BlockFooter));
    return footer->magic == kFooterMagic && footer->size == block->size;
}

bool WasmHostAllocator::is_free(BlockHeader *block) const {
    return valid_block(block) && (block->flags & kAllocated) == 0;
}

WasmHostAllocator::BlockHeader *WasmHostAllocator::next_block(BlockHeader *block) const {
    const auto address = reinterpret_cast<std::uintptr_t>(block) + block->size;
    if (address >= arena_end_)
        return nullptr;
    auto *next = reinterpret_cast<BlockHeader *>(address);
    return valid_block(next) ? next : nullptr;
}

WasmHostAllocator::BlockHeader *WasmHostAllocator::previous_block(BlockHeader *block) const {
    const auto address = reinterpret_cast<std::uintptr_t>(block);
    if (address < arena_begin_ + sizeof(BlockFooter))
        return nullptr;
    auto *footer = reinterpret_cast<BlockFooter *>(address - sizeof(BlockFooter));
    if (footer->magic != kFooterMagic || footer->size < minimum_block_size() ||
        footer->size > address - arena_begin_)
        return nullptr;
    auto *previous = reinterpret_cast<BlockHeader *>(address - footer->size);
    return valid_block(previous) &&
                   reinterpret_cast<std::uintptr_t>(previous) + previous->size == address
               ? previous
               : nullptr;
}

void WasmHostAllocator::remove_free(BlockHeader *target) {
    BlockHeader *previous = nullptr;
    for (auto *current = free_head_; current; current = current->next_free) {
        if (current != target) {
            previous = current;
            continue;
        }
        if (previous)
            previous->next_free = current->next_free;
        else
            free_head_ = current->next_free;
        current->next_free = nullptr;
        return;
    }
    std::abort();
}

void WasmHostAllocator::insert_free(BlockHeader *block) {
    block->flags = 0;
    block->next_free = free_head_;
    write_footer(block);
    free_head_ = block;
}

bool WasmHostAllocator::initialize(void *base, std::size_t size) {
    if (initialized_)
        return ready_;
    initialized_ = true;
    const auto base_address = reinterpret_cast<std::uintptr_t>(base);
    if (!base || size > std::numeric_limits<std::uintptr_t>::max() - base_address)
        return false;
    arena_begin_ = align_up(base_address, kAlignment);
    arena_end_ = align_down(base_address + size, kAlignment);
    if (arena_begin_ >= arena_end_ || arena_end_ - arena_begin_ < minimum_block_size())
        return false;
    free_head_ = reinterpret_cast<BlockHeader *>(arena_begin_);
    free_head_->magic = kHeaderMagic;
    free_head_->flags = 0;
    free_head_->size = arena_end_ - arena_begin_;
    free_head_->next_free = nullptr;
    write_footer(free_head_);
    ready_ = true;
    return true;
}

bool WasmHostAllocator::valid() const {
    return ready_ && arena_begin_ < arena_end_ &&
           (free_head_ == nullptr || valid_block(free_head_));
}

std::size_t WasmHostAllocator::block_size_for(std::size_t payload_size) const {
    const auto overhead = sizeof(BlockHeader) + sizeof(BlockFooter);
    const auto maximum = std::numeric_limits<std::size_t>::max();
    if (payload_size > maximum - overhead - (kAlignment - 1))
        return 0;
    return static_cast<std::size_t>(align_up(payload_size + overhead, kAlignment));
}

void WasmHostAllocator::split_allocated(BlockHeader *block, std::size_t requested_size) {
    const auto remainder_size = block->size - requested_size;
    if (remainder_size < minimum_block_size()) {
        write_footer(block);
        return;
    }
    block->size = requested_size;
    write_footer(block);
    auto *remainder =
        reinterpret_cast<BlockHeader *>(reinterpret_cast<uint8_t *>(block) + requested_size);
    remainder->magic = kHeaderMagic;
    remainder->flags = 0;
    remainder->size = remainder_size;
    remainder->next_free = nullptr;
    insert_free(remainder);
}

void *WasmHostAllocator::allocate_unchecked(std::size_t size) {
    const auto requested_size = block_size_for(size == 0 ? 1 : size);
    if (requested_size == 0)
        return nullptr;
    for (auto *block = free_head_; block; block = block->next_free) {
        if (block->size < requested_size)
            continue;
        remove_free(block);
        block->flags = kAllocated;
        block->next_free = nullptr;
        split_allocated(block, requested_size);
        return reinterpret_cast<uint8_t *>(block) + sizeof(BlockHeader);
    }
    return nullptr;
}

void *WasmHostAllocator::allocate(std::size_t size) {
    return valid() ? allocate_unchecked(size) : nullptr;
}

WasmHostAllocator::BlockHeader *WasmHostAllocator::header_from_payload(void *pointer) const {
    if (!pointer)
        return nullptr;
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    if (address < arena_begin_ + sizeof(BlockHeader))
        return nullptr;
    auto *block = reinterpret_cast<BlockHeader *>(address - sizeof(BlockHeader));
    return valid_block(block) && (block->flags & kAllocated) != 0 ? block : nullptr;
}

WasmHostAllocator::AlignedPrefix *WasmHostAllocator::aligned_prefix(void *pointer) const {
    if (!pointer ||
        reinterpret_cast<std::uintptr_t>(pointer) < arena_begin_ + sizeof(AlignedPrefix))
        return nullptr;
    auto *prefix = reinterpret_cast<AlignedPrefix *>(reinterpret_cast<uint8_t *>(pointer) -
                                                     sizeof(AlignedPrefix));
    if (prefix->magic != kAlignedMagic || !header_from_payload(prefix->raw))
        return nullptr;
    return prefix;
}

void WasmHostAllocator::release_unchecked(void *pointer) {
    if (!pointer)
        return;
    if (auto *prefix = aligned_prefix(pointer)) {
        prefix->magic = 0;
        release_unchecked(prefix->raw);
        return;
    }
    auto *block = header_from_payload(pointer);
    if (!block)
        std::abort();
    block->flags = 0;
    block->next_free = nullptr;
    if (auto *next = next_block(block); is_free(next)) {
        remove_free(next);
        block->size += next->size;
        write_footer(block);
    }
    if (auto *previous = previous_block(block); is_free(previous)) {
        remove_free(previous);
        previous->size += block->size;
        write_footer(previous);
        block = previous;
    }
    insert_free(block);
}

void WasmHostAllocator::release(void *pointer) {
    if (pointer && valid())
        release_unchecked(pointer);
}

void *WasmHostAllocator::allocate_aligned(std::size_t alignment, std::size_t size) {
    if (alignment <= kAlignment)
        return allocate(size);
    if ((alignment & (alignment - 1)) != 0 ||
        size > std::numeric_limits<std::size_t>::max() - alignment - sizeof(AlignedPrefix))
        return nullptr;
    auto *raw = static_cast<uint8_t *>(allocate(size + alignment - 1 + sizeof(AlignedPrefix)));
    if (!raw)
        return nullptr;
    const auto aligned =
        align_up(reinterpret_cast<std::uintptr_t>(raw) + sizeof(AlignedPrefix), alignment);
    auto *prefix = reinterpret_cast<AlignedPrefix *>(aligned - sizeof(AlignedPrefix));
    prefix->magic = kAlignedMagic;
    prefix->size = size;
    prefix->raw = raw;
    return reinterpret_cast<void *>(aligned);
}

void *WasmHostAllocator::reallocate(void *pointer, std::size_t size) {
    if (!pointer)
        return allocate(size);
    if (size == 0) {
        release(pointer);
        return nullptr;
    }
    if (auto *prefix = aligned_prefix(pointer)) {
        auto *replacement = allocate(size);
        if (!replacement)
            return nullptr;
        std::memcpy(replacement, pointer, prefix->size < size ? prefix->size : size);
        release(pointer);
        return replacement;
    }
    auto *block = header_from_payload(pointer);
    if (!block)
        std::abort();
    const auto requested_size = block_size_for(size);
    if (requested_size == 0)
        return nullptr;
    const auto old_payload = block->size - sizeof(BlockHeader) - sizeof(BlockFooter);
    if (requested_size <= block->size)
        return pointer;
    if (auto *next = next_block(block);
        is_free(next) && block->size + next->size >= requested_size) {
        remove_free(next);
        block->size += next->size;
        split_allocated(block, requested_size);
        return pointer;
    }
    auto *replacement = allocate(size);
    if (!replacement)
        return nullptr;
    std::memcpy(replacement, pointer, old_payload < size ? old_payload : size);
    release(pointer);
    return replacement;
}

} // namespace nk::wasm
