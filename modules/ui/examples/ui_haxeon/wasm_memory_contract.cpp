#include "nativekit_haxeon_memory_contract.h"

#include <cerrno>
#include <cstdint>

extern "C" void *__real__sbrk64(int64_t increment);

namespace {

uintptr_t current_break() {
    void *value = __real__sbrk64(0);
    if (value == reinterpret_cast<void *>(-1))
        return UINTPTR_MAX;
    return reinterpret_cast<uintptr_t>(value);
}

bool exceeds_host_region(uintptr_t current, int64_t increment) {
    if (current == UINTPTR_MAX || current > NKUI_HAXEON_HOST_HEAP_LIMIT)
        return true;
    if (increment == 0)
        return false;
    if (increment < 0) {
        const auto requested = static_cast<uint64_t>(-(increment + 1)) + 1;
        return requested > current - NKUI_HAXEON_HOST_HEAP_BASE;
    }
    const auto requested = static_cast<uint64_t>(increment);
    return requested > NKUI_HAXEON_HOST_HEAP_LIMIT - current;
}

} // namespace

extern "C" void *__wrap__sbrk64(int64_t increment) {
    if (exceeds_host_region(current_break(), increment)) {
        errno = ENOMEM;
        return reinterpret_cast<void *>(-1);
    }
    return __real__sbrk64(increment);
}

extern "C" uint32_t nkui_haxeon_memory_contract_status() {
    const auto value = current_break();
    return value >= NKUI_HAXEON_HOST_HEAP_BASE && value <= NKUI_HAXEON_HOST_HEAP_LIMIT ? 0u : 1u;
}

extern "C" uint32_t nkui_haxeon_memory_contract_version() {
    return NKUI_HAXEON_MEMORY_CONTRACT_VERSION;
}

extern "C" uint32_t nkui_haxeon_memory_contract_page_size() {
    return NKUI_HAXEON_WASM_PAGE_SIZE;
}

extern "C" uint32_t nkui_haxeon_memory_contract_host_base() {
    return NKUI_HAXEON_HOST_HEAP_BASE;
}

extern "C" uint32_t nkui_haxeon_memory_contract_host_limit() {
    return NKUI_HAXEON_HOST_HEAP_LIMIT;
}

extern "C" uint32_t nkui_haxeon_memory_contract_base() {
    return NKUI_HAXEON_GUEST_MEMORY_BASE;
}

extern "C" uint32_t nkui_haxeon_memory_contract_guest_base() {
    return NKUI_HAXEON_GUEST_MEMORY_BASE;
}

extern "C" uint32_t nkui_haxeon_memory_contract_guest_limit() {
    return NKUI_HAXEON_GUEST_MEMORY_LIMIT;
}

extern "C" uint32_t nkui_haxeon_memory_contract_memory_size() {
    return NKUI_HAXEON_MEMORY_LIMIT;
}

extern "C" uint32_t nkui_haxeon_memory_contract_limit() {
    return NKUI_HAXEON_MEMORY_LIMIT;
}
