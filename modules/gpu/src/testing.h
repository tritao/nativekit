#ifndef NATIVEKIT_GPU_TESTING_H
#define NATIVEKIT_GPU_TESTING_H

#include "nativekit_gpu.h"

#if defined(NKGPU_TESTING)
#if defined(_WIN32)
#if defined(NK_STATIC)
#define NKGPU_TEST_API
#elif defined(NKGPU_BUILDING_LIBRARY)
#define NKGPU_TEST_API __declspec(dllexport)
#else
#define NKGPU_TEST_API __declspec(dllimport)
#endif
#else
#define NKGPU_TEST_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

NKGPU_TEST_API void nkgpu_test_fail_next_image_creation(void);
NKGPU_TEST_API void nkgpu_test_fail_next_buffer_creation(void);
NKGPU_TEST_API void nkgpu_test_fail_next_present(void);
NKGPU_TEST_API int32_t nkgpu_test_generation_exhaustion(void);
NKGPU_TEST_API void nkgpu_test_forbid_surface_target_queries(void);
NKGPU_TEST_API void nkgpu_test_allow_surface_target_queries(void);
NKGPU_TEST_API nkgpu_result nkgpu_test_lose_after_frames(nkgpu_renderer renderer, uint32_t frames);
NKGPU_TEST_API void nkgpu_test_lose_all_after_frames(uint32_t frames);
NKGPU_TEST_API void nkgpu_test_invalidate_all(void);
NKGPU_TEST_API nkgpu_result nkgpu_test_invalidate_surface(nkgpu_renderer renderer);

#ifdef __cplusplus
}
#endif
#endif

#endif
