# Vendored sources

- `dlmalloc.c`: Doug Lea's malloc 2.8.6, as shipped in Emscripten 6.0.9
  (`system/lib/dlmalloc.c`, which adds Emscripten tracing hooks and defaults).
  Released to the public domain (CC0). `src/wasm/host_mspace.c` builds it in
  mspace-only mode for the WebAssembly host allocator.
