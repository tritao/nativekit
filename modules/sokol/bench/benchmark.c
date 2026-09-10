#include "benchmark.h"

static uint32_t step(uint32_t value) {
    return value * UINT32_C(1664525) + UINT32_C(1013904223);
}

uint32_t nks_benchmark_call(uint32_t value) {
    return step(value);
}

uint32_t nks_benchmark_batch(uint32_t iterations, uint32_t value) {
    for (uint32_t index = 0; index < iterations; ++index)
        value = step(value);
    return value;
}

static uint32_t read_u32(const uint8_t *data) {
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

uint32_t nks_benchmark_commands(const uint8_t *commands, uint32_t size, uint32_t value) {
    if (!commands || size % 20 != 0)
        return 0;
    for (uint32_t offset = 0; offset < size; offset += 20) {
        if (read_u32(commands + offset) != 7 || read_u32(commands + offset + 4) != 20)
            return 0;
        value = step(value ^ read_u32(commands + offset + 8) ^ read_u32(commands + offset + 12) ^
                     read_u32(commands + offset + 16));
    }
    return value;
}
