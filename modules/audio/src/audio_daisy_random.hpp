#pragma once
#include <cstdint>
#include "Utility/random.h"
inline int nk_daisy_random(void *context) noexcept {
    auto &state = *static_cast<uint32_t *>(context);
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<int>(state & static_cast<uint32_t>(RAND_MAX));
}
struct DaisyRandomScope {
    daisysp::ScopedRandomSource scope;
    explicit DaisyRandomScope(uint32_t &state) : scope(nk_daisy_random, &state) {}
};
