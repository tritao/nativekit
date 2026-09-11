# NativeKit Graphics Lab

The flagship UI example is a Haxeon program using the typed NativeKit graphics
API. It keeps `nkui_resource`, command headers, and byte offsets out of the
application code.

The scene demonstrates:

- reusable vector paths, Bézier curves, concave geometry, and stroke caps/joins;
- solid paints, RGBA image upload, scaling, clipping, and opacity layers;
- mixed Latin, Arabic, Hebrew, Japanese, and emoji text;
- pointer-driven text hit testing with a visible caret;
- the same retained star path rendered repeatedly with different translations;
- display-list and path-cache diagnostics; and
- a render-target panel reserved for the first offscreen/3D producer.

Build the Haxeon artifact and run it with:

```sh
modules/ui/tools/showcase.sh
```

Useful deterministic modes are:

```sh
modules/ui/tools/showcase.sh --static-frame --stats
modules/ui/tools/showcase.sh --smoke-test
```

`--static-frame` renders one canonical frame. `--smoke-test` renders 30 frames
and exits, while `--stats` prints the final retained-list and path-cache
counters.

The Web host uses one fixed shared linear-memory contract:

```text
[0, 128 MiB)       NativeKit/Emscripten host heap
[128, 256 MiB)     Haxeon guest heap
```

The native `sbrk` boundary rejects host allocations that would enter the
guest region. The browser shell validates the same boundary and fixed memory
size before instantiating the guest. This keeps the two allocators from
silently overlapping; a future dynamic-memory implementation must negotiate
this contract instead of enabling independent growth.
