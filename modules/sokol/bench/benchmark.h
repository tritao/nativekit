#ifndef NATIVEKIT_SOKOL_BENCHMARK_H
#define NATIVEKIT_SOKOL_BENCHMARK_H

#include <stdint.h>

#if defined(_WIN32)
#define NKS_BENCH_API __declspec(dllexport)
#else
#define NKS_BENCH_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

NKS_BENCH_API uint32_t nks_benchmark_call(uint32_t value);
NKS_BENCH_API uint32_t nks_benchmark_batch(uint32_t iterations, uint32_t value);
NKS_BENCH_API uint32_t nks_benchmark_commands(const uint8_t *commands, uint32_t size,
                                              uint32_t value);

#ifdef __cplusplus
}
#endif

#endif
