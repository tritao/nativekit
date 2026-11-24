# Vendored sources

- `dlmalloc.c`: Doug Lea's malloc 2.8.6, as shipped in Emscripten 6.0.9
  (`system/lib/dlmalloc.c`, which adds Emscripten tracing hooks and defaults).
  Released to the public domain (CC0). `src/wasm/host_mspace.c` builds it in
  mspace-only mode for the WebAssembly host allocator.
- Local change: the `WIN32` configuration block defines `HAVE_MMAP` and
  `HAVE_MORECORE` only when the includer has not, as the other platform blocks
  do. Upstream forces `HAVE_MMAP 1` there, which on Windows let the host
  mspace (built with `HAVE_MMAP 0`) take memory from `VirtualAlloc` past its
  arena.
