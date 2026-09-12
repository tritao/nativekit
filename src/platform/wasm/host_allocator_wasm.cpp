#include "platform/wasm/host_allocator.h"

#include <emscripten/heap.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

#ifndef NK_WASM_HOST_HEAP_LIMIT
#error "NK_WASM_HOST_HEAP_LIMIT must be configured for the WebAssembly host allocator"
#endif

extern "C" unsigned char __heap_base;

namespace {

nk::wasm::WasmHostAllocator allocator;
bool allocator_initialization_attempted = false;
bool allocator_ready = false;

bool ensure_allocator() {
    if (allocator_initialization_attempted)
        return allocator_ready;
    allocator_initialization_attempted = true;

    const auto base = reinterpret_cast<std::uintptr_t>(&__heap_base);
    if (base >= NK_WASM_HOST_HEAP_LIMIT || emscripten_get_heap_size() < NK_WASM_HOST_HEAP_LIMIT)
        return false;
    allocator_ready = allocator.initialize(&__heap_base, NK_WASM_HOST_HEAP_LIMIT - base);
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
    return ensure_allocator() ? allocator.allocate(size) : nullptr;
}

extern "C" void free(void *pointer) {
    if (ensure_allocator())
        allocator.release(pointer);
}

extern "C" void *calloc(std::size_t count, std::size_t size) {
    return allocate_zeroed(count, size);
}

extern "C" void *realloc(void *pointer, std::size_t size) {
    return ensure_allocator() ? allocator.reallocate(pointer, size) : nullptr;
}

extern "C" void *aligned_alloc(std::size_t alignment, std::size_t size) {
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

extern "C" uint32_t nk_wasm_host_allocator_status() {
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

void *operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t &) noexcept {
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
