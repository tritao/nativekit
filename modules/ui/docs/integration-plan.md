# NativeKit UI rendering and text integration plan

## Boundaries

The public `nkui_*` ABI describes render/layout primitives, styled text,
versioned tree transactions, text geometry, and resolved layout snapshots.
NativeUI owns layout, text, and rendering mechanics. It does not own widgets,
input routing, focus, scrolling policy, accessibility semantics, or retained
interaction state. NativeKit provides raw input, IME, clipboard, platform
accessibility, and graphics surfaces; Haxe owns the UI framework, state, and
interaction policy. Clay, Skribidi, NanoVG, and Sokol remain private.

The first private layout slice is implemented in `src/layout/`. It pins Clay
behind a NativeKit-owned `LayoutEngine`, measures text through Skribidi, and
produces NativeKit-owned geometry and rendering primitives. The Haxe-facing
layout bridge uses a versioned, validated batch render-tree transaction; the
resolved geometry for all submitted nodes returns in one snapshot. Clay and
Skribidi remain private implementation details.

An interactive frame flows through these stages:

```text
Haxe view/render tree
    -> validated NativeUI layout transaction
    -> box layout and text measurement
    -> batched resolved geometry and text layouts
    -> Haxe hit testing, event routing, focus, and state updates
    -> NativeUI renders the same resolved frame
    -> NanoVG shape batches + Skribidi glyph batches
    -> Sokol pass on a NativeKit surface
```

## Stage 1: isolate and prove NanoVG path preparation

1. Build the private NanoVG core with path preparation enabled and text disabled.
2. Translate NanoVG values at the NativeKit preparation boundary; no NanoVG
   type crosses NativeKit's persistent prepared representation.
3. Prepare rectangles, rounded rectangles, concave paths, holes, strokes,
   gradients, image paints, and transforms without a frame or renderer.
4. Replay prepared operations through the NativeKit compositor and Sokol backend.
5. Add image-based golden tests and a 30-frame NativeKit smoke test.

NanoVG's Fontstash API is not built by NativeKit UI. Skribidi remains the text
authority, and ordinary images bypass NanoVG entirely.

## Stage 2: integrate Skribidi as the text authority

1. Create a private `nkui_skribidi` target with scoped dependency targets for
   HarfBuzz, SheenBidi, libunibreak, and BudouX. Do not consume Skribidi's root
   CMake project because it changes global compiler and install settings.
2. Wrap font collections, rich text, layout cache, editor, and image atlas in
   `src/text/skribidi_adapter.*`. No `skb_*` type crosses `nativekit_ui.h`.
3. Define a UTF-8 paragraph request containing available width, scale, locale,
   font candidates, size, weight, spacing, alignment, and spans. Return stable
   internal layout IDs plus width, height, and baseline metrics.
4. Make intrinsic measurement and paragraph layout callbacks of box layout.
   Skribidi returns line records and an opaque text-layout ID; Clay only
   consumes the resulting line dimensions and forwards the ID/line index.
   Cache by text/style/font generation/width/scale so repeated layout passes do
   not reshape text.
5. Translate Skribidi glyph atlas creation and dirty rectangles into Sokol
   textures and partial uploads. Render its `skb_quad_t` output with dedicated
   alpha, color, and SDF pipelines.
6. Expose grapheme-safe point-to-caret, caret-rectangle, and selection-rectangle
   queries to Haxe. Haxe owns the editing model; NativeKit delivers committed
   text/IME transactions and receives cursor geometry from the focused field.

## Stage 3: preserve display-list ordering

Start with an ordered display list containing shape, clip, image, and glyph-run
commands. Consecutive shapes are submitted through NanoVG; consecutive glyph
runs use the Skribidi renderer. Switching renderer flushes the current batch
while retaining the same Sokol pass.

This is simpler and more correct than forcing Skribidi atlas data through
NanoVG's image/text APIs. Measure flush count, vertices, atlas uploads, and GPU
passes on representative screens. Only optimize after those numbers exist.

## Stage 4: layout and resolved geometry

The private Clay adapter provides intrinsic measurement and external paragraph
layout through Skribidi. It returns layout geometry only. Haxe consumes a batch
of resolved node bounds, clip/visibility data, transforms, baselines, and
content extents to implement hit testing, scrolling, focus, and accessibility.
NativeUI renders from the exact snapshot that produced those results.

The active NativeUI sequence is generic visual kinds, complete geometry
snapshots, clipping/transforms/content metrics, shared-frame rendering, and
text caret/selection geometry. The Haxe framework then adds a render tree with
stable identity, state storage, input/event propagation, focus, gestures,
scrolling, accessibility projection, themes, animation, and compositional
widgets. NativeKit raw input is delivered to Haxe without passing through the
layout transaction.

## Rewrite and unification policy

Do not rewrite Skribidi's shaping, bidi, grapheme, line-breaking, editing, or
font-selection code. Those are correctness-heavy Unicode mechanisms backed by
specialized libraries. Keep the vendored revision replaceable and contribute
general fixes upstream where practical.

Do not merge Skribidi's CPU canvas with NanoVG. Skribidi's canvas exists to
rasterize glyphs and COLR content into atlas images; NanoVG tessellates general
UI paths for the GPU. Similar drawing vocabulary does not imply interchangeable
jobs.

The long-term local fork is the NanoVG path-preparation half:

- keep Fontstash compiled out of the NativeKit path core;
- maintain allocator-aware preparation and explicit fill/stroke metadata;
- keep GPU resources, generated shaders, and ordered submission in NativeKit's
  Sokol backend;
- optionally replace NanoVG with a smaller display-list tessellator if profiles
  show excessive flushes, allocations, or unsupported effects.

Unify ownership and scheduling, not upstream data structures. NativeKit UI
should own one frame, one display list, one Sokol device, explicit texture
lifetimes, allocator hooks, and instrumentation. Skribidi and NanoVG should
remain replaceable engines behind that boundary.

## Exit criteria before public ABI expansion

- Latin, Arabic/Hebrew bidi, Indic shaping, CJK wrapping, and color emoji tests.
- Caret and selection round trips use grapheme-safe positions.
- Multiple scale factors and surface recreation preserve correct atlases.
- Clips and opacity remain correct when shape and text batches interleave.
- No per-glyph or per-draw Haxeon FFI calls.
- Dependency updates reproduce from recorded revisions and licenses.
