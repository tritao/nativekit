#if defined(_WIN32)
#define NATIVEKIT_TEST_EXPORT __declspec(dllexport)
#else
#define NATIVEKIT_TEST_EXPORT __attribute__((visibility("default")))
#endif

extern "C" NATIVEKIT_TEST_EXPORT int nativekit_test_library_value = 42;
