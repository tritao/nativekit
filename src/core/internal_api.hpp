#pragma once

/*
 * Internal NativeKit modules are separate shared libraries in desktop builds.
 * Keep the cross-module executor/frame-backend bridge visible without making
 * it part of the public C ABI.
 */
#if defined(_WIN32)
#if defined(NK_STATIC)
#define NK_INTERNAL_API
#elif defined(NK_BUILDING_LIBRARY)
#define NK_INTERNAL_API __declspec(dllexport)
#else
#define NK_INTERNAL_API __declspec(dllimport)
#endif
#else
#define NK_INTERNAL_API __attribute__((visibility("default")))
#endif
