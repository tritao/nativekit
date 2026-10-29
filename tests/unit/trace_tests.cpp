#include "nativekit.h"
#include "nativekit_time.h"
#include "nativekit_trace.h"

#include <cassert>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <stdlib.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
struct Recorder {
    std::vector<std::string> calls;
};

Recorder recorder;
volatile int enabled = 1;

void NK_CALL zone_begin(void *user_data, const nk_trace_location *location) {
    assert(user_data == &recorder);
    assert(location && location->name && location->function && location->file &&
           location->line > 0);
    recorder.calls.push_back(std::string("begin ") + location->name);
}

void NK_CALL zone_end(void *user_data) {
    assert(user_data == &recorder);
    recorder.calls.push_back("end");
}

void NK_CALL event(void *user_data, nk_trace_event_kind kind, uint64_t, int64_t, int64_t) {
    assert(user_data == &recorder);
    recorder.calls.push_back("event " + std::to_string(kind));
}

const nk_trace_sink sink{
    sizeof(nk_trace_sink), 0, &recorder, &enabled, zone_begin, zone_end, event};

void wait_briefly() {
    assert(nk_wait_events_timeout(0.002) == NK_OK);
}

bool waited_in_zone() {
    for (std::size_t i = 0; i + 1 < recorder.calls.size(); ++i)
        if (recorder.calls[i] == "begin nk.wait" && recorder.calls[i + 1] == "end")
            return true;
    return false;
}
} // namespace

int main() {
#if defined(_WIN32)
    // A failed assertion exits with a status instead of opening a modal CRT or fault dialog.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    nk_init_options options{};
    options.struct_size = sizeof(options);
    options.api_version = NK_API_VERSION;
    assert(nk_init(&options) == NK_OK);

    // An installed sink sees each wait as a zone.
    nk_set_trace_sink(&sink);
    wait_briefly();
    assert(waited_in_zone());

    // While the host's switch is off, nothing is called.
    recorder.calls.clear();
    enabled = 0;
    wait_briefly();
    assert(recorder.calls.empty());
    enabled = 1;

    // A sink missing callbacks, or from a header older than this build, counts as none.
    nk_trace_sink partial = sink;
    partial.zone_end = nullptr;
    nk_set_trace_sink(&partial);
    wait_briefly();
    assert(recorder.calls.empty());
    nk_trace_sink old = sink;
    old.struct_size = 8;
    nk_set_trace_sink(&old);
    wait_briefly();
    assert(recorder.calls.empty());

    // Removing the sink stops the calls.
    nk_set_trace_sink(&sink);
    nk_set_trace_sink(nullptr);
    wait_briefly();
    assert(recorder.calls.empty());

    nk_shutdown();
    return 0;
}
