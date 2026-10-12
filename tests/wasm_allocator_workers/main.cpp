#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>
#include <cstdint>
#include <malloc.h>

extern "C" uint32_t nk_wasm_host_allocator_status();
extern "C" uint32_t nk_wasm_host_allocator_statistic(uint32_t);
static std::vector<unsigned char> early_allocation(8192, 0x5a);
static thread_local unsigned char tls_pattern[512];
static void require(bool condition) { if (!condition) std::abort(); }
static void churn(unsigned char marker) {
    std::memset(tls_pattern, marker, sizeof(tls_pattern));
    for (int n = 0; n < 2000; ++n) {
        auto *bytes = static_cast<unsigned char *>(std::calloc(1, 1024 + n % 1024));
        require(bytes && malloc_usable_size(bytes) >= 1024u + n % 1024);
        for (int i = 0; i < 1024; ++i) require(bytes[i] == 0);
        std::memset(bytes, marker, 1024);
        bytes = static_cast<unsigned char *>(std::realloc(bytes, 4096));
        require(bytes);
        for (int i = 0; i < 1024; ++i) require(bytes[i] == marker);
        std::free(bytes);
        void *aligned = nullptr;
        require(posix_memalign(&aligned, 64, 1024) == 0);
        require(reinterpret_cast<uintptr_t>(aligned) % 64 == 0);
        std::free(aligned);
        for (auto value : tls_pattern) require(value == marker);
    }
}
int main() {
    require(nk_wasm_host_allocator_status() == 0);
    for (auto value : early_allocation) require(value == 0x5a);
    std::thread workers[4];
    for (int i = 0; i < 4; ++i) workers[i] = std::thread(churn, i + 1);
    for (auto &worker : workers) worker.join();
    for (auto value : early_allocation) require(value == 0x5a);
    require(nk_wasm_host_allocator_statistic(0) <= 16777216);
    require(nk_wasm_host_allocator_statistic(1) < nk_wasm_host_allocator_statistic(0));
    std::puts("PASS: bounded heap startup, TLS, concurrent allocation and accounting");
}
