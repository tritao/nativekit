# Haxeon Showcase Wasm performance

## Runtime ownership

The browser page owns startup and scheduling. It creates the NativeKit Emscripten
host, obtains its shared `WebAssembly.Memory`, validates the host/guest partition,
loads the guest module, and calls the exported Showcase entry points. Browser input
becomes NativeKit events; the Showcase frame consumes events, updates retained UI
state, submits layout, encodes a display list, renders it, and presents the surface.

The memory contract assigns `[host_base, host_limit)` to NativeKit/Emscripten and
`[guest_base, guest_limit)` to Haxeon. NativeKit's Wasm host allocator is bounded
by the host limit. Haxeon owns allocation and garbage collection in the guest
partition. The shared memory's byte length is capacity, not live usage; allocator
high-water and allocation counters are available only in a benchmark-instrumented
guest built with `NKUI_HAXEON_MEMORY_STATS=ON`.

`ShowcaseWeb` creates the `FontCollection` and adds the preloaded font assets.
`Showcase` takes ownership of that collection and injects it into the retained
layout session and text layouts. Disposal releases the layout session, renderer,
display list, and dependent UI resources before disposing the collection.

| Layer | Owns |
| --- | --- |
| Browser shell | WebGL canvas, event loop, module loading, input forwarding, benchmark collection |
| Haxeon guest | Showcase state, Wasm heap and GC, frame orchestration, display-list encoding |
| NativeKit UI | Layout, text shaping, hit testing, display-list interpretation, rendering resources |
| NativeKit platform | Window/surface lifecycle and the bounded native host allocator |

## Benchmark modes

Build the normal optimized browser artifacts with `./tools/build-web.sh`. Then run
the synchronous core benchmark with:

```sh
NATIVEKIT_WEB_BENCHMARK_MODE=core ./tools/benchmark-web-haxeon.sh
```

Core mode calls the exported frame function directly with fixed virtual time. It
does not wait for `requestAnimationFrame`; its per-frame duration measures the
guest call, NativeKit API work, WebGL command submission, and presentation call.
It is suitable for repeatable call-cost comparisons, while browser mode retains
the real `requestAnimationFrame` intervals and dropped-frame estimate:

```sh
./tools/benchmark-web-haxeon.sh
```

Choose `NATIVEKIT_WEB_BENCHMARK_SCENARIO` from `full`, `static`, `text-heavy`, or
`editing`. The editing workload updates multilingual text and moves the caret on
each frame. Set `NATIVEKIT_WEB_BENCHMARK_WARMUP` and
`NATIVEKIT_WEB_BENCHMARK_FRAMES` to control sample counts. Both modes write JSON
with frame percentiles, startup costs, artifact sizes, memory partition capacity,
browser details, CPU model, CPU affinity, and host load average.

The text-heavy baseline uses eight repetitions of the multilingual sample. The
previous eight-repeat trap was a stale nullable field in a GC-reused Wasm heap
block: newly grown memory was zeroed, but the free-list reuse path was not.
Haxeon's allocator now zero-fills recycled blocks, and its focused
`scripts/test-wasm-gc-reuse.sh` regression proves an old reference does not leak
into a fresh object's default-null field, and that recycled array elements and
`Bytes` storage are zeroed. The Showcase passed 4×, 8×, 16×, and 32× text-repeat
smoke runs; 16× and 32× were one-frame stress checks, not timing baselines.

## Local release-build sample

On a 13th Gen Intel Core i5-13600K with headless Chrome 136/software WebGL, a
120-frame core run (20 warmup frames) measured median/p95 frame times of 38.9 /
45.6 ms for full, 39.0 / 47.5 ms for static, 41.5 / 54.6 ms for text-heavy at
8×, and 46.5 / 54.2 ms for editing. The browser-paced full run measured 33.6 /
42.9 ms median/p95 and estimated 253 dropped frames. These are diagnostic local
samples, not a CI performance gate: the host's load average was around 5.8 on
20 logical CPUs, and software WebGL does not represent hardware-GPU
presentation. JSON reports are retained under ignored `out/web-benchmarks/`
for this workspace.

The separate stats-enabled 8× text-heavy sample recorded 16,928 guest
allocations / 1.44 MB allocated, with a 1.94 MB heap high-water mark. Imported
NativeKit API time was median 2.3 ms / p95 3.1 ms per frame. This instrumented
run is useful for attribution, not as a release-timing comparison.

For allocator counters and time spent inside imported NativeKit APIs, build the
separate instrumented artifact and run a short profile sample:

```sh
./tools/build-web-haxeon-benchmark.sh
NATIVEKIT_WEB_ARTIFACT_DIR=build-web-haxeon-profile/modules/ui \
NATIVEKIT_WEB_BENCHMARK_MODE=core \
NATIVEKIT_WEB_BENCHMARK_PROFILE=1 \
NATIVEKIT_WEB_BENCHMARK_SCENARIO=editing \
NATIVEKIT_WEB_BENCHMARK_FRAMES=120 \
./tools/benchmark-web-haxeon.sh
```

The profile separates layout submission, text-layout/shaping calls, rendering,
display-list operations, platform calls, and the residual time in the guest call
and JavaScript wrapper. These imported-call measurements are inclusive API-call
wall times, not a sampling profiler. Allocator counters add work to allocations,
so use the normal build for timing comparisons and the instrumented build for
allocation and attribution data.

To measure the self-hosted Haxeon-to-Wasm compile itself and capture a HashLink
sampling profile, use:

```sh
./tools/profile-haxeon-showcase.sh --runs 3
```

It records repeated compile time and peak RSS, checks that each run emits the
same Wasm artifact, then stores the compiler profile and its report under `out/`.
Set `NATIVEKIT_HAXEON_COMPILER_MODULE` when profiling a locally rebuilt compiler.

## Interpreting results

Keep synchronous call cost and browser frame pacing as separate series. Record
repeated release-build runs on an idle, fixed machine before setting a regression
gate; headless software WebGL is useful for repeatability but does not predict a
hardware GPU's presentation behavior. Compare medians and p95 values across runs,
and keep the JSON environment fields with every result. CI currently runs the
same benchmark workloads and uploads their reports; thresholds should be set
from repeated CI-runner data rather than from a developer machine's baseline.
