# NativeKit UI Explorer

`ui_showcase` is the flagship interactive example for NativeKit's Haxe UI
framework. A searchable component catalog leads into seven pages: Overview,
Controls, Text & Input, Layout, Scrolling & Data, Navigation & Overlays, and
Graphics Lab. The app includes a global light/dark switch and a collapsible
inspector that shows resolved render-tree bounds, focus, semantics, and the
current accessibility audit.

The Haxe widgets own composition, state, focus, event handling, and semantics.
Each frame follows the `UiContext.submit → native layout → render` path on both
desktop and WebAssembly. Text & Input includes multilingual editing and sends
selection, composition, and caret state to NativeKit's platform IME bridge.
Scrolling & Data demonstrates a fixed-row `VirtualList` with 10,000 items and
reports the visible row range.

The Graphics Lab page opens the existing typed-graphics scene. That scene keeps
its vector paths, Bézier curves, stroke caps and joins, image clipping and
opacity, multilingual shaping and caret hit testing, retained display lists,
path-cache diagnostics, and depth-tested offscreen cube. It remains the
deterministic graphics workload used by existing visual regressions.

Build and run the desktop explorer with:

```sh
modules/ui/tools/showcase.sh
```

The two smoke modes exercise the UI Explorer and the focused Graphics Lab,
respectively. `--static-frame` renders one canonical Graphics Lab frame, and
`--stats` prints its retained-list and path-cache counters:

```sh
modules/ui/tools/showcase.sh --ui-smoke-test
modules/ui/tools/showcase.sh --smoke-test
modules/ui/tools/showcase.sh --static-frame --stats
```

The WebGL host starts in the UI Explorer. The visual suite captures both the
Graphics Lab and deterministic UI Explorer states: overview, dark/light and
focused controls, text editing, layout, a scrolled virtual list, and dialog,
popup, and menu overlays. Graphics cases continue to cover canonical, compact,
and wide layouts, caret hit testing, light theme state, and a fixed animation
frame:

```sh
tools/test-web-visual.sh
tools/test-web-visual.sh --ui-only
tools/test-web-visual.sh --ui-only --case ui-controls
tools/test-web-visual.sh --update  # intentionally refresh baselines
```

`--ui-only` is a faster component-focused loop. UI baselines live beside the
Graphics Lab references in `modules/ui/tests/golden/`; mismatches produce
`*-actual.png` and `*-diff.png` artifacts under `build-web/visual-diffs/`.

The browser bundle packages explicit IBM Plex Latin, Arabic, Hebrew, and
Japanese fonts plus Noto Emoji from the Skribidi test assets. Caret cases assert
code-point offset, affinity, and direction for Latin, Arabic, Hebrew, CJK, and
emoji runs, independent of fonts installed on the CI host.

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
