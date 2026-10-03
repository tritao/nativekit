# WebAssembly host memory

`NativeKit::wasm_host_allocator` is the process-wide allocator adapter for an
Emscripten host module that shares linear memory with another Wasm runtime. It
belongs to the platform layer, so modules such as NativeKit core, UI, and
Sokol all use the same host allocator rather than supplying their own.

Link it once from the final executable that owns the shared memory:

```cmake
target_link_libraries(my_wasm_host PRIVATE NativeKit::wasm_host_allocator)
```

The target provides `malloc`, `free`, `calloc`, `realloc`, aligned allocation,
and C++ `new`/`delete`, and propagates Emscripten's `-sMALLOC=none` setting. Its
arena starts at `__heap_base` and ends at `NK_WASM_HOST_HEAP_LIMIT`, an exclusive
byte offset configurable at CMake time (128 MiB by default). The host module's
initial linear memory must extend at least to that limit. The adapter checks
this before using the arena and never grows it.

Inside the arena it runs dlmalloc 2.8.6 (`vendor/dlmalloc`) as a single mspace
created over the arena (`create_mspace_with_base`), with no `sbrk` or `mmap`, so
it cannot grow past the limit. Its size-binned free lists keep large blocks
available under allocate-and-free churn, where the earlier single first-fit list
splintered the free space until large allocations failed with most of the arena
free. The adapter also provides `mallinfo` and
`nk_wasm_host_allocator_statistic(index)` for diagnostics: 0 arena, 1 bytes in
use, 2 peak bytes in use, 3 largest free block, 4 free blocks. Index 3 and 4
walk the whole heap, so read them occasionally, not per frame.

The allocator does not define the guest partition or guest runtime policy.
Those remain the responsibility of the application-level memory contract. For
example, the Haxeon Showcase places its managed guest heap at the host limit and
uses a separate Haxeon allocator there. Host and guest objects must be released
by the allocator that created them; the shared `WebAssembly.Memory` does not
make their allocation domains interchangeable.

Browser hosts that register guest callbacks must configure
`NK_WEB_EXPORTED_RUNTIME_METHODS=ccall,addFunction,removeFunction` before adding
NativeKit. The default is `ccall`. NativeKit's transitive Emscripten runtime
export option must include the consumer's methods because a later option replaces
an earlier export list rather than merging it.
