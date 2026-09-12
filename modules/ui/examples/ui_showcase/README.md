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

The page shell and panel grid use retained `LayoutNode` values. After each
submission, the Canvas demonstrations query their resolved panel bounds from
`LayoutSession`, so window resizing reflows the cards while vector and image
content remains under the typed graphics API.

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

The WebGL visual regression suite captures the canonical, compact, and wide
layout sizes with software rendering. It also covers deterministic caret clicks,
light theme state, and a fixed non-zero animation frame:

```sh
tools/test-web-visual.sh
tools/test-web-visual.sh --update  # intentionally refresh baselines
```

The Web host uses version 1 of a generated shared linear-memory contract:

```text
[0, 128 MiB)       NativeKit/Emscripten host heap
[128, 256 MiB)     Haxeon guest heap
```

The generated JSON contract is consumed by C++, Haxeon, and the browser. The
NativeKit host uses an explicit bounded allocator rooted at Emscripten's heap
base and refuses allocations outside the host region. The browser compares the
host exports with the guest Wasm contract section before instantiating the
guest. This keeps the two allocators from silently overlapping; a future
dynamic-memory implementation must negotiate a new contract version instead of
enabling independent growth.
