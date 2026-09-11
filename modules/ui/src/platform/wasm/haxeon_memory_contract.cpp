#include "nativekit_haxeon_memory_contract.h"

#include <cstdint>

extern "C" uint32_t nkui_wasm_host_allocator_status();

extern "C" uint32_t nkui_haxeon_memory_contract_status() {
    return nkui_wasm_host_allocator_status();
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
