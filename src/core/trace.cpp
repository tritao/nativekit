#include "core/trace.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

std::atomic<const nk_trace_sink *> host_sink{nullptr};

/*
 * The text-file fallback, for measuring without a host that records a timeline. Each line is
 * `<kind> <ticks> <a> <b>` and the first line names the clock. On Windows the ticks are
 * QueryPerformanceCounter values, which an input hook in another process and HashLink's GC trace
 * share, so the files merge on one timeline; elsewhere they are steady-clock nanoseconds. Kinds: M
 * pointer move (screen x, y), B pointer button (screen x, y), K key (key code, 1 down / 0 up), W
 * wait begins, w wait ends, F frame begins, f frame ends. Other zones are not written.
 */
std::FILE *open_trace_file() noexcept {
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
    static std::FILE *const file = open_trace_file();
    return file;
}

std::mutex trace_file_mutex;

std::int64_t file_ticks() noexcept {
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

void write_line(char kind, std::int64_t a, std::int64_t b) noexcept {
    const std::int64_t now = file_ticks();
    const std::lock_guard<std::mutex> lock(trace_file_mutex);
    std::fprintf(trace_file(), "%c %lld %lld %lld\n", kind, static_cast<long long>(now),
                 static_cast<long long>(a), static_cast<long long>(b));
}

/* Zone letters, innermost last; 0 marks a zone the file does not record. */
thread_local char open_zones[32];
thread_local int open_zone_count = 0;

char zone_letter(const nk_trace_location *location) noexcept {
    if (std::strcmp(location->name, "nk.wait") == 0)
        return 'W';
    if (std::strcmp(location->name, "nk.frame") == 0)
        return 'F';
    return 0;
}

void NK_CALL file_zone_begin(void *, const nk_trace_location *location) {
    const char letter = zone_letter(location);
    if (open_zone_count < static_cast<int>(sizeof(open_zones)))
        open_zones[open_zone_count] = letter;
    ++open_zone_count;
    if (letter)
        write_line(letter, 0, 0);
}

void NK_CALL file_zone_end(void *) {
    if (open_zone_count == 0)
        return;
    --open_zone_count;
    if (open_zone_count >= static_cast<int>(sizeof(open_zones)))
        return;
    if (const char letter = open_zones[open_zone_count])
        write_line(static_cast<char>(letter - 'A' + 'a'), 0, 0);
}

void NK_CALL file_event(void *, nk_trace_event_kind kind, std::uint64_t, std::int64_t a,
                        std::int64_t b) {
    switch (kind) {
    case NK_TRACE_EVENT_INPUT_MOVE:
        write_line('M', a, b);
        break;
    case NK_TRACE_EVENT_INPUT_BUTTON:
        write_line('B', a, b);
        break;
    case NK_TRACE_EVENT_INPUT_KEY:
        write_line('K', a, b);
        break;
    default:
        break;
    }
}

const nk_trace_sink *file_sink() noexcept {
    static const nk_trace_sink sink{
        sizeof(nk_trace_sink), 0, nullptr, nullptr, file_zone_begin, file_zone_end, file_event};
    static const nk_trace_sink *const active = trace_file() ? &sink : nullptr;
    return active;
}

} // namespace

const nk_trace_sink *active_trace_sink() noexcept {
    const nk_trace_sink *sink = host_sink.load(std::memory_order_acquire);
    if (!sink)
        return file_sink();
    if (sink->enabled && !*sink->enabled)
        return nullptr;
    return sink;
}

void trace_flush() noexcept {
    if (host_sink.load(std::memory_order_relaxed) || !file_sink())
        return;
    const std::lock_guard<std::mutex> lock(trace_file_mutex);
    std::fflush(trace_file());
}

} // namespace nk::core

extern "C" {
void NK_CALL nk_set_trace_sink(const nk_trace_sink *sink) {
    /* A sink from an older header lacks callbacks this build would call; treat it as none. */
    if (sink && (sink->struct_size < sizeof(nk_trace_sink) || !sink->zone_begin ||
                 !sink->zone_end || !sink->event))
        sink = nullptr;
    nk::core::host_sink.store(sink, std::memory_order_release);
}
}
