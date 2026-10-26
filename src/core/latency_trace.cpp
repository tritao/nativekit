#include "core/latency_trace.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace nk::core {
namespace {

std::FILE *open_trace() noexcept {
#if defined(_WIN32)
    char path[1024];
    const DWORD length = GetEnvironmentVariableA("NK_LATENCY_TRACE", path, sizeof(path));
    if (length == 0 || length >= sizeof(path))
        return nullptr;
    std::FILE *file = nullptr;
    if (fopen_s(&file, path, "w") != 0)
        return nullptr;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    std::fprintf(file, "# nativekit latency trace clock=qpc frequency=%lld\n",
                 static_cast<long long>(frequency.QuadPart));
#else
    const char *path = std::getenv("NK_LATENCY_TRACE");
    if (!path || !*path)
        return nullptr;
    std::FILE *file = std::fopen(path, "w");
    if (!file)
        return nullptr;
    std::fprintf(file, "# nativekit latency trace clock=steady frequency=1000000000\n");
#endif
    std::setvbuf(file, nullptr, _IOFBF, 1 << 16);
    return file;
}

std::FILE *trace_file() noexcept {
    static std::FILE *const file = open_trace();
    return file;
}

std::mutex trace_mutex;

std::int64_t ticks() noexcept {
#if defined(_WIN32)
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return now.QuadPart;
#else
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
#endif
}

} // namespace

bool latency_trace_enabled() noexcept { return trace_file() != nullptr; }

void latency_trace(char kind, std::int64_t a, std::int64_t b) noexcept {
    std::FILE *file = trace_file();
    if (!file)
        return;
    const std::int64_t now = ticks();
    const std::lock_guard<std::mutex> lock(trace_mutex);
    std::fprintf(file, "%c %lld %lld %lld\n", kind, static_cast<long long>(now), static_cast<long long>(a),
                 static_cast<long long>(b));
}

void latency_trace_flush() noexcept {
    std::FILE *file = trace_file();
    if (!file)
        return;
    const std::lock_guard<std::mutex> lock(trace_mutex);
    std::fflush(file);
}

} // namespace nk::core
