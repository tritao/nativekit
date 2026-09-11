#include "nativekit_haxeon_memory_contract.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

extern "C" unsigned char __heap_base;

namespace {

constexpr uint32_t kHeaderMagic = 0x4e4b4842; // NKHB
constexpr uint32_t kFooterMagic = 0x4e4b4654; // NKFT
constexpr uint32_t kAlignedMagic = 0x4e4b414c; // NKAL
constexpr uint32_t kAllocated = 1;
constexpr uintptr_t kAlignment = 16;

struct BlockHeader {
    uint32_t magic;
    uint32_t size;
    BlockHeader *next_free;
    uint32_t flags;
};

struct BlockFooter {
    uint32_t magic;
    uint32_t size;
};

struct AlignedPrefix {
    uint32_t magic;
    uint32_t size;
    void *raw;
};

static_assert(sizeof(BlockHeader) % kAlignment == 0);
static_assert(sizeof(BlockFooter) % sizeof(uint32_t) == 0);

BlockHeader *free_head = nullptr;
uintptr_t arena_begin = 0;
uintptr_t arena_end = 0;
bool initialized = false;

uintptr_t align_up(uintptr_t value, uintptr_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

uintptr_t align_down(uintptr_t value, uintptr_t alignment) {
    return value & ~(alignment - 1);
}

uint32_t minimum_block_size() {
    return static_cast<uint32_t>(align_up(sizeof(BlockHeader) + sizeof(BlockFooter) + 1, kAlignment));
}

void write_footer(BlockHeader *block) {
    auto *footer = reinterpret_cast<BlockFooter *>(reinterpret_cast<uint8_t *>(block) + block->size -
                                                   sizeof(BlockFooter));
    footer->magic = kFooterMagic;
    footer->size = block->size;
}

bool in_arena(uintptr_t address, uintptr_t bytes = 1) {
    return address >= arena_begin && address <= arena_end && bytes <= arena_end - address;
}

bool valid_block(BlockHeader *block) {
    if (!block || !in_arena(reinterpret_cast<uintptr_t>(block), sizeof(BlockHeader)))
        return false;
    if (block->magic != kHeaderMagic || block->size < minimum_block_size() ||
        block->size % kAlignment != 0 || !in_arena(reinterpret_cast<uintptr_t>(block), block->size))
        return false;
    auto *footer = reinterpret_cast<BlockFooter *>(reinterpret_cast<uint8_t *>(block) + block->size -
                                                   sizeof(BlockFooter));
    return footer->magic == kFooterMagic && footer->size == block->size;
}

bool is_free(BlockHeader *block) {
    return valid_block(block) && (block->flags & kAllocated) == 0;
}

BlockHeader *next_block(BlockHeader *block) {
    auto address = reinterpret_cast<uintptr_t>(block) + block->size;
    if (address >= arena_end)
        return nullptr;
    auto *next = reinterpret_cast<BlockHeader *>(address);
    return valid_block(next) ? next : nullptr;
}

BlockHeader *previous_block(BlockHeader *block) {
    auto address = reinterpret_cast<uintptr_t>(block);
    if (address < arena_begin + sizeof(BlockFooter))
        return nullptr;
    auto *footer = reinterpret_cast<BlockFooter *>(address - sizeof(BlockFooter));
    if (footer->magic != kFooterMagic || footer->size < minimum_block_size() ||
        footer->size > address - arena_begin)
        return nullptr;
    auto *previous = reinterpret_cast<BlockHeader *>(address - footer->size);
    return valid_block(previous) && reinterpret_cast<uintptr_t>(previous) + previous->size == address
               ? previous
               : nullptr;
}

void remove_free(BlockHeader *target) {
    BlockHeader *previous = nullptr;
    for (auto *current = free_head; current; current = current->next_free) {
        if (current != target) {
            previous = current;
            continue;
        }
        if (previous)
            previous->next_free = current->next_free;
        else
            free_head = current->next_free;
        current->next_free = nullptr;
        return;
    }
    std::abort();
}

void insert_free(BlockHeader *block) {
    block->flags = 0;
    block->next_free = free_head;
    write_footer(block);
    free_head = block;
}

bool initialize() {
    if (initialized)
        return free_head != nullptr || arena_begin < arena_end;
    initialized = true;
    arena_begin = align_up(reinterpret_cast<uintptr_t>(&__heap_base), kAlignment);
    arena_end = align_down(NKUI_HAXEON_HOST_HEAP_LIMIT, kAlignment);
    if (arena_begin >= arena_end || arena_end - arena_begin < minimum_block_size())
        return false;
    free_head = reinterpret_cast<BlockHeader *>(arena_begin);
    free_head->magic = kHeaderMagic;
    free_head->size = static_cast<uint32_t>(arena_end - arena_begin);
    free_head->flags = 0;
    free_head->next_free = nullptr;
    write_footer(free_head);
    return true;
}

uint32_t block_size_for(std::size_t payload_size) {
    if (payload_size > UINT32_MAX - sizeof(BlockHeader) - sizeof(BlockFooter))
        return 0;
    const auto total = payload_size + sizeof(BlockHeader) + sizeof(BlockFooter);
    if (total > UINT32_MAX)
        return 0;
    const auto aligned = align_up(total, kAlignment);
    return aligned > UINT32_MAX ? 0 : static_cast<uint32_t>(aligned);
}

void split_allocated(BlockHeader *block, uint32_t requested_size) {
    const auto remainder_size = block->size - requested_size;
    if (remainder_size < minimum_block_size()) {
        write_footer(block);
        return;
    }
    block->size = requested_size;
    write_footer(block);
    auto *remainder = reinterpret_cast<BlockHeader *>(reinterpret_cast<uint8_t *>(block) + requested_size);
    remainder->magic = kHeaderMagic;
    remainder->size = remainder_size;
    remainder->flags = 0;
    remainder->next_free = nullptr;
    insert_free(remainder);
}

void *allocate(std::size_t size) {
    if (!initialize())
        return nullptr;
    const auto requested_size = block_size_for(size == 0 ? 1 : size);
    if (requested_size == 0)
        return nullptr;
    for (auto *block = free_head; block; block = block->next_free) {
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

BlockHeader *header_from_payload(void *pointer) {
    if (!pointer)
        return nullptr;
    auto address = reinterpret_cast<uintptr_t>(pointer);
    if (address < arena_begin + sizeof(BlockHeader))
        return nullptr;
    auto *block = reinterpret_cast<BlockHeader *>(address - sizeof(BlockHeader));
    return valid_block(block) && (block->flags & kAllocated) != 0 ? block : nullptr;
}

void release(void *pointer);

AlignedPrefix *aligned_prefix(void *pointer) {
    if (!pointer || reinterpret_cast<uintptr_t>(pointer) < arena_begin + sizeof(AlignedPrefix))
        return nullptr;
    auto *prefix = reinterpret_cast<AlignedPrefix *>(reinterpret_cast<uint8_t *>(pointer) - sizeof(AlignedPrefix));
    if (prefix->magic != kAlignedMagic || !header_from_payload(prefix->raw))
        return nullptr;
    return prefix;
}

void release(void *pointer) {
    if (!pointer)
        return;
    if (auto *prefix = aligned_prefix(pointer)) {
        prefix->magic = 0;
        release(prefix->raw);
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

void *allocate_aligned(std::size_t alignment, std::size_t size) {
    if (alignment <= kAlignment)
        return allocate(size);
    if ((alignment & (alignment - 1)) != 0 || size > SIZE_MAX - alignment - sizeof(AlignedPrefix))
        return nullptr;
    auto *raw = static_cast<uint8_t *>(allocate(size + alignment - 1 + sizeof(AlignedPrefix)));
    if (!raw)
        return nullptr;
    auto aligned = align_up(reinterpret_cast<uintptr_t>(raw) + sizeof(AlignedPrefix), alignment);
    auto *prefix = reinterpret_cast<AlignedPrefix *>(aligned - sizeof(AlignedPrefix));
    prefix->magic = kAlignedMagic;
    prefix->size = size > UINT32_MAX ? 0 : static_cast<uint32_t>(size);
    prefix->raw = raw;
    return reinterpret_cast<void *>(aligned);
}

} // namespace

extern "C" void *emscripten_builtin_memalign(std::size_t alignment, std::size_t size) {
    return allocate_aligned(alignment, size);
}

extern "C" void *emscripten_builtin_malloc(std::size_t size) {
    return allocate(size);
}

extern "C" void *emscripten_builtin_calloc(std::size_t count, std::size_t size) {
    if (count != 0 && size > SIZE_MAX / count)
        return nullptr;
    auto total = count * size;
    auto *pointer = allocate(total);
    if (pointer)
        std::memset(pointer, 0, total);
    return pointer;
}

extern "C" void *emscripten_builtin_realloc(void *pointer, std::size_t size) {
    return realloc(pointer, size);
}

extern "C" void emscripten_builtin_free(void *pointer) {
    release(pointer);
}

extern "C" void *malloc(std::size_t size) {
    return allocate(size);
}

extern "C" void free(void *pointer) {
    release(pointer);
}

extern "C" void *calloc(std::size_t count, std::size_t size) {
    if (count != 0 && size > SIZE_MAX / count)
        return nullptr;
    auto total = count * size;
    auto *pointer = allocate(total);
    if (pointer)
        std::memset(pointer, 0, total);
    return pointer;
}

extern "C" void *realloc(void *pointer, std::size_t size) {
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
    if (auto *next = next_block(block); is_free(next) && block->size + next->size >= requested_size) {
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

extern "C" void *aligned_alloc(std::size_t alignment, std::size_t size) {
    if (alignment == 0 || size % alignment != 0)
        return nullptr;
    return allocate_aligned(alignment, size);
}

extern "C" int posix_memalign(void **result, std::size_t alignment, std::size_t size) {
    if (!result || alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0)
        return 22;
    *result = allocate_aligned(alignment, size);
    return *result ? 0 : 12;
}

extern "C" uint32_t nkui_haxeon_host_allocator_status() {
    return initialize() ? 0u : 1u;
}

void *operator new(std::size_t size) {
    if (auto *pointer = allocate(size))
        return pointer;
    std::abort();
}

void *operator new[](std::size_t size) {
    return ::operator new(size);
}

void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
    return allocate(size);
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept {
    return allocate(size);
}

void operator delete(void *pointer) noexcept {
    release(pointer);
}

void operator delete[](void *pointer) noexcept {
    release(pointer);
}

void operator delete(void *pointer, std::size_t) noexcept {
    release(pointer);
}

void operator delete[](void *pointer, std::size_t) noexcept {
    release(pointer);
}

void *operator new(std::size_t size, std::align_val_t alignment) {
    if (auto *pointer = allocate_aligned(static_cast<std::size_t>(alignment), size))
        return pointer;
    std::abort();
}

void *operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}

void *operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept {
    return allocate_aligned(static_cast<std::size_t>(alignment), size);
}

void *operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept {
    return allocate_aligned(static_cast<std::size_t>(alignment), size);
}

void operator delete(void *pointer, std::align_val_t) noexcept {
    release(pointer);
}

void operator delete[](void *pointer, std::align_val_t) noexcept {
    release(pointer);
}

void operator delete(void *pointer, std::size_t, std::align_val_t) noexcept {
    release(pointer);
}

void operator delete[](void *pointer, std::size_t, std::align_val_t) noexcept {
    release(pointer);
}
