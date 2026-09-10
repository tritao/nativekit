# NativeKit UI rendering and text integration plan

## Boundaries

The public `nkui_*` ABI describes retained UI nodes, styles, transactions, and
semantic events. Clay, Skribidi, NanoVG, NanoVG-Sokol, and Sokol remain private.
Haxeon owns components, state, reconciliation, and resolved style policy.

Internally, a frame flows through these stages:

```text
validated tree transaction
    -> box layout and text measurement
    -> retained geometry and hit-test data
    -> ordered display list
    -> NanoVG shape batches + Skribidi glyph batches
    -> Sokol pass on a NativeKit surface
```

## Stage 1: isolate and prove NanoVG-Sokol

1. Build the private `nkui_nanovg` target from the NanoVG and NativeKit fork
   submodules, using the same pinned `sokol_gfx.h` as NativeKit Sokol.
2. Keep the fork's explicit blend-state cache key compatible with current
   Sokol rather than packing four blend factors into 16 bits.
3. Preserve the fork's corrected texture lifetime conditionals before enabling
   borrowed image handles.
4. Render rectangles, rounded rectangles, paths, clipping, gradients, and
   images inside an already-open Sokol pass. NanoVG must not own the window,
   surface, event loop, or presentation.
5. Add image-based golden tests and a 30-frame NativeKit smoke test.

NanoVG's Fontstash API is not used by NativeKit UI. Keeping it enabled during
the first bring-up is acceptable, but no UI text behavior may depend on it.

## Stage 2: integrate Skribidi as the text authority

1. Create a private `nkui_skribidi` target with scoped dependency targets for
   HarfBuzz, SheenBidi, libunibreak, and BudouX. Do not consume Skribidi's root
   CMake project because it changes global compiler and install settings.
2. Wrap font collections, rich text, layout cache, editor, and image atlas in
   `src/text/skribidi_adapter.*`. No `skb_*` type crosses `nativekit_ui.h`.
3. Define a UTF-8 paragraph request containing available width, scale, locale,
   font candidates, size, weight, spacing, alignment, and spans. Return stable
   internal layout IDs plus width, height, and baseline metrics.
4. Make text measurement a callback of box layout. Cache by text/style/font
   generation/width/scale so repeated layout passes do not reshape text.
5. Translate Skribidi glyph atlas creation and dirty rectangles into Sokol
   textures and partial uploads. Render its `skb_quad_t` output with dedicated
   alpha, color, and SDF pipelines.
6. Translate caret, grapheme, selection, and line geometry into NativeKit IME
   and accessibility state. Haxeon receives transactional UTF-8 edits, not
   glyph or byte-index manipulation.

## Stage 3: preserve display-list ordering

Start with an ordered display list containing shape, clip, image, and glyph-run
commands. Consecutive shapes are submitted through NanoVG; consecutive glyph
runs use the Skribidi renderer. Switching renderer flushes the current batch
while retaining the same Sokol pass.

This is simpler and more correct than forcing Skribidi atlas data through
NanoVG's image/text APIs. Measure flush count, vertices, atlas uploads, and GPU
passes on representative screens. Only optimize after those numbers exist.

## Stage 4: layout and retained UI

Add Clay behind `src/layout/clay_adapter.*` after text measurement works. A
text node measures through Skribidi under Clay's width constraint. Layout
results populate retained geometry, hit testing, scrolling, focus order, and
accessibility nodes before producing the display list.

The first end-to-end slice is `Box + Text + Button`: nested box layout, shaped
text, pointer activation, keyboard focus, and a NativeKit accessibility node.

## Rewrite and unification policy

Do not rewrite Skribidi's shaping, bidi, grapheme, line-breaking, editing, or
font-selection code. Those are correctness-heavy Unicode mechanisms backed by
specialized libraries. Keep the vendored revision replaceable and contribute
general fixes upstream where practical.

Do not merge Skribidi's CPU canvas with NanoVG. Skribidi's canvas exists to
rasterize glyphs and COLR content into atlas images; NanoVG tessellates general
UI paths for the GPU. Similar drawing vocabulary does not imply interchangeable
jobs.

The likely long-term local fork is the NanoVG rendering half:

- remove or compile out Fontstash once Skribidi coverage is complete;
- maintain the Sokol backend and generated shaders with NativeKit's Sokol pin;
- expose an internal flush boundary suitable for ordered text interleaving;
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
