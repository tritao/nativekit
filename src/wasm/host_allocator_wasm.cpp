#include "wasm/host_allocator.h"

#include <emscripten/heap.h>
#include <malloc.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

#ifndef NK_WASM_HOST_HEAP_LIMIT
#error "NK_WASM_HOST_HEAP_LIMIT must be configured for the WebAssembly host allocator"
#endif

extern "C" unsigned char __heap_base;

// Emscripten's Wasm-worker startup reserves main-thread TLS through this
// pointer before C++ constructors run. Start the bounded arena after that TLS
// reservation, rather than overlapping it at __heap_base.
namespace {
std::uintptr_t host_heap_start = 0;
}
extern "C" std::uintptr_t *emscripten_get_sbrk_ptr() {
    if (host_heap_start == 0)
        host_heap_start = reinterpret_cast<std::uintptr_t>(&__heap_base);
    return &host_heap_start;
}

namespace {

// The bounded allocator and its accounting form one transaction. Audio hosts
// share this heap with pthreads and Wasm workers; guard initialization too.
#if defined(__EMSCRIPTEN_SHARED_MEMORY__)
std::atomic_flag allocator_lock = ATOMIC_FLAG_INIT;
struct AllocatorGuard {
    AllocatorGuard() { while (allocator_lock.test_and_set(std::memory_order_acquire)) {} }
    ~AllocatorGuard() { allocator_lock.clear(std::memory_order_release); }
};
#else
struct AllocatorGuard {};
#endif

nk::wasm::WasmHostAllocator allocator;
bool allocator_initialization_attempted = false;
bool allocator_ready = false;

bool ensure_allocator() {
    if (allocator_initialization_attempted)
        return allocator_ready;
    allocator_initialization_attempted = true;

    auto base = *emscripten_get_sbrk_ptr();
#if defined(__EMSCRIPTEN_SHARED_MEMORY__)
    // Emscripten may malloc before the Wasm-worker TLS constructor. Reserve
    // its region up front; that constructor can later initialize TLS safely.
    const auto align_up = [](std::uintptr_t value, std::uintptr_t alignment) {
        return (value + alignment - 1) & ~(alignment - 1);
    };
    const auto tls_alignment = __builtin_wasm_tls_align() > 16 ? __builtin_wasm_tls_align() : 16;
    const auto tls_base = align_up(reinterpret_cast<std::uintptr_t>(&__heap_base), tls_alignment);
    const auto tls_end = tls_base + align_up(__builtin_wasm_tls_size(), alignof(std::max_align_t));
    if (base < tls_end) base = tls_end;
    base = align_up(base, 16);
#endif
    if (base >= NK_WASM_HOST_HEAP_LIMIT || emscripten_get_heap_size() < NK_WASM_HOST_HEAP_LIMIT)
        return false;
    allocator_ready = allocator.initialize(reinterpret_cast<void *>(base), NK_WASM_HOST_HEAP_LIMIT - base);
    return allocator_ready;
}

void *allocate_zeroed(std::size_t count, std::size_t size) {
    if (count != 0 && size > SIZE_MAX / count)
        return nullptr;
    const auto total = count * size;
    if (!ensure_allocator())
        return nullptr;
    auto *pointer = allocator.allocate(total);
    if (pointer)
        std::memset(pointer, 0, total);
    return pointer;
}

} // namespace

extern "C" void *malloc(std::size_t size) {
    [[maybe_unused]] AllocatorGuard guard;
    return ensure_allocator() ? allocator.allocate(size) : nullptr;
}

// Native solvers use the writable capacity to reuse their allocation buffers.
extern "C" std::size_t malloc_usable_size(void *pointer) {
    [[maybe_unused]] AllocatorGuard guard;
    return ensure_allocator() ? allocator.usable_size(pointer) : 0;
}

extern "C" void free(void *pointer) {
    [[maybe_unused]] AllocatorGuard guard;
    if (ensure_allocator())
        allocator.release(pointer);
}

extern "C" void *calloc(std::size_t count, std::size_t size) {
    [[maybe_unused]] AllocatorGuard guard;
    return allocate_zeroed(count, size);
}

extern "C" void *realloc(void *pointer, std::size_t size) {
    [[maybe_unused]] AllocatorGuard guard;
    return ensure_allocator() ? allocator.reallocate(pointer, size) : nullptr;
}

extern "C" void *aligned_alloc(std::size_t alignment, std::size_t size) {
    [[maybe_unused]] AllocatorGuard guard;
    return ensure_allocator() ? allocator.allocate_aligned(alignment, size) : nullptr;
}

extern "C" int posix_memalign(void **result, std::size_t alignment, std::size_t size) {
    if (!result || alignment < sizeof(void *) || (alignment & (alignment - 1)) != 0)
        return 22;
    *result = aligned_alloc(alignment, size);
    return *result ? 0 : 12;
}

extern "C" void *emscripten_builtin_memalign(std::size_t alignment, std::size_t size) {
    return aligned_alloc(alignment, size);
}

extern "C" void *emscripten_builtin_malloc(std::size_t size) {
    return malloc(size);
}

extern "C" void *emscripten_builtin_calloc(std::size_t count, std::size_t size) {
    return calloc(count, size);
}

extern "C" void *emscripten_builtin_realloc(void *pointer, std::size_t size) {
    return realloc(pointer, size);
}

extern "C" void emscripten_builtin_free(void *pointer) {
    free(pointer);
}

// musl's own allocations (locales, for MuJoCo's XML parser) go through its __libc_* entry points.
extern "C" void *__libc_malloc(std::size_t size) {
    return malloc(size);
}

extern "C" void *__libc_calloc(std::size_t count, std::size_t size) {
    return calloc(count, size);
}

extern "C" void *__libc_realloc(void *pointer, std::size_t size) {
    return realloc(pointer, size);
}

extern "C" void __libc_free(void *pointer) {
    free(pointer);
}

// OCCT's OSD_MemInfo reports heap use through this.
extern "C" struct mallinfo mallinfo() {
    [[maybe_unused]] AllocatorGuard guard;
    struct mallinfo result{};
    if (!ensure_allocator())
        return result;
    const auto stats = allocator.statistics();
    result.arena = static_cast<int>(stats.arena);
    result.uordblks = static_cast<int>(stats.in_use);
    result.fordblks = static_cast<int>(stats.arena - stats.in_use);
    result.ordblks = static_cast<int>(stats.free_blocks);
    return result;
}

// Host heap figures for diagnostics, in bytes: 0 arena, 1 in use, 2 peak in use, 3 largest free
// block, 4 free blocks (a count). Returns 0 for an unknown index.
extern "C" uint32_t nk_wasm_host_allocator_statistic(uint32_t index) {
    [[maybe_unused]] AllocatorGuard guard;
    if (!ensure_allocator())
        return 0;
    const auto stats = allocator.statistics();
    switch (index) {
    case 0:
        return static_cast<uint32_t>(stats.arena);
    case 1:
        return static_cast<uint32_t>(stats.in_use);
    case 2:
        return static_cast<uint32_t>(stats.peak);
    case 3:
        return static_cast<uint32_t>(stats.largest_free);
    case 4:
        return static_cast<uint32_t>(stats.free_blocks);
    default:
        return 0;
    }
}

extern "C" uint32_t nk_wasm_host_allocator_status() {
    [[maybe_unused]] AllocatorGuard guard;
    return ensure_allocator() ? 0u : 1u;
}

void *operator new(std::size_t size) {
    if (auto *pointer = malloc(size))
        return pointer;
    std::abort();
}

void *operator new[](std::size_t size) {
    return ::operator new(size);
}

void *operator new(std::size_t size, const std::nothrow_t &) noexcept {
    return malloc(size);
}

void *operator new[](std::size_t size, const std::nothrow_t &) noexcept {
    return malloc(size);
}

void operator delete(void *pointer) noexcept {
    free(pointer);
}

void operator delete[](void *pointer) noexcept {
    free(pointer);
}

void operator delete(void *pointer, std::size_t) noexcept {
    free(pointer);
}

void operator delete[](void *pointer, std::size_t) noexcept {
    free(pointer);
}

void *operator new(std::size_t size, std::align_val_t alignment) {
    if (auto *pointer = aligned_alloc(static_cast<std::size_t>(alignment), size))
        return pointer;
    std::abort();
}

void *operator new[](std::size_t size, std::align_val_t alignment) {
    return ::operator new(size, alignment);
}

void *operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept {
    return aligned_alloc(static_cast<std::size_t>(alignment), size);
}

void *operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t &) noexcept {
    return aligned_alloc(static_cast<std::size_t>(alignment), size);
}

void operator delete(void *pointer, std::align_val_t) noexcept {
    free(pointer);
}

void operator delete[](void *pointer, std::align_val_t) noexcept {
    free(pointer);
}

void operator delete(void *pointer, std::size_t, std::align_val_t) noexcept {
    free(pointer);
}

void operator delete[](void *pointer, std::size_t, std::align_val_t) noexcept {
    free(pointer);
}
