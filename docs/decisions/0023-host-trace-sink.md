# ADR 0023: Trace points go to a host-installed sink

## Status

Accepted.

## Context

Measuring input latency needs timestamps from inside NativeKit: when the UI
thread takes each pointer or key event off the platform queue, how long it waits
for events, and how long it spends drawing frames. The first version wrote these
to a text file named by `NK_LATENCY_TRACE`, with its own clock per platform. That
works, but every host that already records a timeline (Haxeon's profiler, Tracy,
ETW, Perfetto) would have to merge a second file recorded on a second clock, and
NativeKit would own a file format and a clock contract.

NativeKit must stay independent of any one host. The only link to Haxeon today
is the annotation macros in `nativekit.h`, which expand to nothing outside Clang
binding import.

## Decision

NativeKit defines the interface and the host implements it.

- `nativekit_trace.h` adds `nk_set_trace_sink()` and a table of C callbacks:
  `zone_begin`, `zone_end` and `event`. Zones nest per thread. Events are
  instants with a kind, the OS timestamp of the input, and two integers.
- Each trace point names a static `nk_trace_location`, so a sink can key by
  address and describe a location once, as Tracy's source locations do.
- The sink timestamps each call with its own clock. NativeKit keeps none. The OS
  timestamp of an input is passed as an argument, on the clock of
  `nk_time_now_ns()`, because only NativeKit can read it at the moment of the
  event.
- The sink may carry a pointer to a host-owned switch. While it reads zero, a
  trace point costs one load and calls nothing, so a host can leave the sink
  installed and record only during a session.
- Building with `NK_TRACE=OFF` removes the trace points.
- The `NK_LATENCY_TRACE` text file stays as the fallback sink used when no host
  sink is installed, with its old line format, so existing measurement tooling
  keeps working until it reads captures instead.
- The header is not in the generated bindings. A sink is a table of function
  pointers, and the host that installs one is native code.

Haxeon's runtime HDLL provides the sink. It resolves `nk_set_trace_sink` in the
library the FFI already loaded, maps zones to profiler spans and inputs to a
profiler event, and points the switch at `hl_prof_on`. NativeKit never names or
searches for the profiler's library.

The age of an input is computed on NativeKit's clock, in the sink's event
callback, rather than by the profiler converting the timestamp. On Linux the
profiler's clock (`CLOCK_MONOTONIC_RAW`) is not the clock input is stamped on, so
only a difference taken on NativeKit's clock is right.

## Consequences

- Any host can record NativeKit's trace points without NativeKit changing.
- Zone names are a small ABI between NativeKit and its hosts: `nk.wait` and
  `nk.frame`. The text-file fallback matches on them, which is one reason to
  remove the fallback once its consumers read captures.
- Each backend must add its own input trace points, with the platform's timestamp
  converted to the clock of `nk_time_now_ns()`. Windows and GTK have them. The
  others are marked `TODO(trace)`.
- Frames are traced through `nk::core::run_surface_frame`, which every backend can
  adopt, so a frame means the same thing everywhere: one zone per drawn frame,
  however the draw was triggered.
- OS timestamps are coarse (1 ms on GTK, the system tick on Windows). They find
  delays of tens of milliseconds but cannot time single frames. An external
  recorder, such as an input hook, is needed for exact arrival times.
