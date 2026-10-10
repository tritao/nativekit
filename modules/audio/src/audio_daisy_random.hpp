#pragma once
#include <cstdlib>
#include <stdlib.h>
#include <algorithm>
#include <random>
#include <cstdint>
inline thread_local uint32_t nk_daisy_fallback_random = 1;
inline thread_local uint32_t *nk_daisy_random_state = &nk_daisy_fallback_random;
inline int nk_daisy_rand() noexcept {
    auto &state = *nk_daisy_random_state;
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<int>(state & static_cast<uint32_t>(RAND_MAX));
}
struct DaisyRandomScope {
    uint32_t *previous;
    explicit DaisyRandomScope(uint32_t &state) : previous(nk_daisy_random_state) {
        nk_daisy_random_state = &state;
    }
    ~DaisyRandomScope() { nk_daisy_random_state = previous; }
};
