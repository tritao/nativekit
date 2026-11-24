#pragma once

/*
 * Internal NativeKit modules are separate shared libraries in desktop builds.
 * Keep the cross-module runtime bridge (errors, handles, events, workers,
 * executors, frame backends) visible without making it part of the public C ABI.
 *
 * Tests that compile core sources into their own executable define
 * NK_INTERNAL_SOURCE_COPY: their copies stay private to the executable, so the
 * library keeps calling its own definitions.
 */
#if defined(NK_INTERNAL_SOURCE_COPY)
#if defined(_WIN32)
#define NK_INTERNAL_API
#else
#define NK_INTERNAL_API __attribute__((visibility("hidden")))
#endif
#elif defined(_WIN32)
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
