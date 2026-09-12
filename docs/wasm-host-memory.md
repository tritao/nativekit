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

The allocator does not define the guest partition or guest runtime policy.
Those remain the responsibility of the application-level memory contract. For
example, the Haxeon Showcase places its managed guest heap at the host limit and
uses a separate Haxeon allocator there. Host and guest objects must be released
by the allocator that created them; the shared `WebAssembly.Memory` does not
make their allocation domains interchangeable.
