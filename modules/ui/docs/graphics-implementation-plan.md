# NativeKit 2D graphics and compositor implementation plan

## Objective

Build one NativeKit-owned graphics pipeline for immediate Canvas drawing,
retained UI, Skribidi text, NanoVG paths, offscreen layers, and future surface
producers such as 3D and video.

The permanent ownership model is:

```text
Haxe Canvas API / retained UI
            |
            v
NativeKit semantic display list
            |
            v
Native preparation
    NanoVG -> prepared paths
    Skribidi -> glyph batches
            |
            v
NativeKit compositor
    clips, layers, targets, dependencies
            |
            v
NativeKit render plan
            |
            v
NativeKit Sokol backend
```

NativeKit owns public rendering semantics, operation ordering, GPU resources,
render passes, and submission. NanoVG is a private path and image preparation
engine. Skribidi remains the private authority for text shaping, layout,
editing, rasterization, and CPU atlas packing.

## Non-goals

The initial implementation will not:

- expose NanoVG, Skribidi, or Sokol types through the public ABI;
- replace Skribidi shaping, bidi, breaking, editing, or atlas packing;
- implement a general-purpose render graph;
- reorder operations across semantic display-list boundaries;
- support arbitrary path clips, filters, every blend mode, or 3D in the first
  vertical slice;
- bind NanoVG directly to Haxe;
- use NanoVG Fontstash as a public or internal text authority.

## Terminology and ownership

`nk_surface` retains its existing meaning: a platform presentation surface
associated with a NativeKit window. Do not reuse that name for arbitrary
sampleable textures.

Internal graphics terminology:

- **Display list:** ordered semantic Canvas/UI operations; may be retained.
- **Prepared operation:** frame-ready geometry and parameters produced by a
  specialized adapter.
- **Render target:** a window target or offscreen GPU target.
- **Layer:** a semantic isolation scope, not a GPU texture exposed to callers.
- **Compositor:** resolves layers, clips, target dependencies, and passes.
- **Render plan:** concrete, normally frame-scoped passes and draw batches.
- **Backend:** creates GPU resources and executes a render plan.

Ownership rules:

- NativeKit owns display lists, resource IDs, render targets, render plans, and
  all Sokol objects.
- NanoVG owns only its CPU context and transient path-tessellation state.
- Skribidi owns layouts, editing state, glyph rasterization, and CPU atlas
  pixels.
- The Skribidi adapter maps CPU atlas textures to NativeKit image IDs.
- Haxe owns application state and submits validated semantic transactions.
- Frame arenas own transient vertices, indices, uniforms, and prepared records.

## Module layout

Introduce the following private implementation areas incrementally:

```text
modules/ui/src/
    display_list/
        arena.*
        display_list.*
        state_table.*
        validation.*
    prepare/
        nanovg_recorder.*
        skribidi_adapter.*
        glyph_batcher.*
    compositor/
        compositor.*
        render_target_pool.*
        render_plan.*
    render/
        sokol_backend.*
        shaders/
```

Names may change while private, but the four responsibilities must remain
separate even if they initially share a translation unit.

## Core internal model

### Stable resource IDs

Use distinct generational handle types for images, paths, text layouts, render
targets, and any retained display lists. Follow the existing NativeKit-Sokol
kind/generation/slot approach. Raw Sokol IDs, pointers, NanoVG image IDs, and
Skribidi atlas pointers must not appear in semantic commands.

### Semantic display list

Start with a versioned, validated command arena. Each record contains an opcode
and byte size so unknown records can be rejected safely and future versions can
be decoded deliberately.

Initial commands:

```text
SetTransform
SetPaint
SetGlobalAlpha
SetCompositeMode
PushState
PopState
ClipRect
DrawPath
DrawImage
DrawTextLayout
BeginLayer
EndLayer
DrawRenderTarget
```

The first executable milestone only needs rectangular clips, source-over
compositing, paths, images, text, and opacity layers. Reserve command/version
space for the remaining operations without pretending they already work.

Commands should reference compact immutable state IDs where that avoids
repeating transforms, paints, and clip data. Validation must check record
bounds, nesting, handle kinds, finite numeric values, and balanced state,
clip, and layer scopes before preparation begins.

### Prepared operations

Preparation converts semantic commands into ordered, backend-neutral records:

```text
PreparedPathFill
PreparedPathStroke
PreparedImage
PreparedGlyphBatch
PreparedClipRect
PreparedCompositeTarget
```

Prepared records reference frame-arena ranges for vertices, indices, and
uniform data. They must not contain pointers that can escape the arena lifetime.
NanoVG non-convex fills and stencil strokes remain atomic prepared operations;
the compositor must never insert unrelated work inside them.

### Render plan

The initial render plan is a linear list of passes, not a general DAG:

```c
typedef struct nkui_render_pass {
    nkui_render_target_id target;
    nkui_render_target_id depth_stencil;
    nkui_prepared_range commands;
} nkui_render_pass;

typedef struct nkui_render_plan {
    nkui_render_pass *passes;
    uint32_t pass_count;
} nkui_render_plan;
```

The compositor records target dependencies while walking the display list.
Plans must reject self-dependencies and cycles. A later implementation may use
a DAG internally, but the public API must not expose pass scheduling mechanics.

## Phase 0: lock down the architecture

1. Record an ADR covering the ownership model, terminology, and decision not to
   couple NanoVG directly to Skribidi.
2. Document which existing files are evaluation code and which are intended to
   become permanent adapters.
3. Add dependency revision and license checks to CI.
4. Keep the public NativeKit renderer smoke test as the reference for the
   replacement path and text pipeline.

Exit criteria:

- The ownership rules above are reviewed and accepted.
- `nk_surface`, render-target, image, and layer terminology is unambiguous.
- No proposed public ABI exposes third-party types.

## Phase 1: display-list arena and validation

1. Implement a growable native arena with checked alignment and overflow.
2. Define private command headers, opcodes, resource IDs, and state records.
3. Implement command validation as a distinct pass with useful command index
   and byte-offset errors.
4. Implement push/pop state and rectangular clip semantics.
5. Add a native builder API used by tests before defining the Haxe projection.
6. Add reset/reuse behavior and allocation/growth counters.

Tests:

- truncated, oversized, unknown, and mis-nested command streams;
- stale and cross-kind resource handles;
- NaN and infinite transform/paint values;
- deterministic encoding and decoding;
- arena growth, reset, reuse, and overflow rejection;
- preservation of exact semantic order.

Exit criteria:

- A display list can represent paths, text-layout references, images, clips,
  and one nested opacity layer without invoking a graphics backend.
- Invalid input cannot partially mutate retained state.

## Phase 2: NanoVG recording backend

1. Keep upstream NanoVG path construction and tessellation.
2. Replace renderer callbacks with a NativeKit recording adapter.
3. Have `renderFill`, `renderStroke`, and `renderTriangles` copy prepared
   geometry, paint uniforms, scissor state, image references, and blend state
   into the frame arena.
4. Do not call `sg_setup*`, create GPU resources, begin/end passes, or commit
   from the recording backend.
5. Preserve each fill/stroke's internal stencil sequence as one atomic prepared
   operation.
6. Add counters for paths, vertices, indices, prepared operations, allocations,
   and NanoVG flushes.

Prefer implementing this against NanoVG's existing renderer callback boundary
without changing NanoVG core. Fork NanoVG core only if the callback data is
insufficient to preserve operation boundaries or required state.

Tests:

- rectangles, rounded rectangles, concave paths, holes, strokes, gradients,
  image paints, transforms, and rectangular scissors;
- prepared geometry compared with the NativeKit Sokol output;
- golden images on the initial OpenGL backend;
- proof that the recorder contains no Sokol calls or handles.

Exit criteria:

- NanoVG can prepare all shape operations required by the vertical slice.
- NativeKit can inspect and replay the resulting prepared operations.

## Phase 3: direct Skribidi glyph batches

1. Add a private Skribidi adapter for font collections, layout caches, layouts,
   editor state, rasterizer, and image atlas.
2. Define semantic text-layout requests containing UTF-8 text, available width,
   locale, scale, font candidates, size, weight, spacing, alignment, and spans.
3. Cache layouts by text/style/font generation/width/scale.
4. Translate Skribidi quads directly into NativeKit glyph vertices. Do not turn
   glyphs into NanoVG rectangles or image patterns.
5. Produce distinct alpha-mask, SDF, and color glyph batches.
6. Merge only consecutive compatible glyph runs. Never cross an intervening
   semantic operation.
7. Preserve grapheme-safe caret, hit-test, selection, and line geometry for UI,
   IME, and accessibility consumers.

Tests:

- Latin and font fallback;
- Arabic and Hebrew bidi;
- Indic shaping;
- CJK line and word wrapping;
- combining marks and emoji sequences;
- color emoji;
- caret and selection round trips at grapheme boundaries;
- scaling and layout-cache invalidation.

Exit criteria:

- Text renders through direct glyph batches with no NanoVG text or Fontstash
  dependency.
- No per-glyph Haxe/native calls occur.

## Phase 4: atlas and image ownership

1. Let Skribidi continue to own CPU packing and pixels.
2. Let NativeKit exclusively own corresponding GPU images and views.
3. Store NativeKit image IDs in atlas texture user data through the private
   adapter.
4. Use R8 images for alpha-mask and SDF atlases and RGBA images for color
   atlases.
5. Track texture generation, format, dimensions, dirty bounds, and upload
   acknowledgement.
6. Remove per-update full RGBA conversion and temporary full-atlas allocation.
7. Use queryable texture generations in the adapter; add explicit lifecycle
   callbacks only if future replacement/repack behavior needs them.
8. Keep a correct whole-atlas fallback isolated in the uploader until partial
   region upload is supported by the active backend.

Potential upstream Skribidi improvements:

- richer batch-oriented render iteration when profiling justifies it;
- multiple dirty rectangles when one bounding region is measurably wasteful;
- explicit destroy/repack callbacks if queryable texture generations prove
  insufficient for a future atlas replacement strategy.

The initial boundary work is now implemented in the vendored Skribidi API:

- `skb_layout_iterate_render_glyphs()` exposes value-based visual glyph data
  without exposing line, run, glyph, or cluster storage;
- `skb_layout_prepare_glyphs()` makes atlas population and rasterization an
  explicit step before NativeKit quad emission;
- atlas textures report semantic R8-mask, R8-SDF, or premultiplied RGBA8
  formats, allocation generations, row pitch, and dirty origins;
- dirty uploads use non-destructive epoch snapshots and exact acknowledgement;
- font collections and layouts expose generations for retained cache identity.

NativeKit keys retained GPU atlas resources by texture ID and generation, while
the private adapter revalidates cached glyph batches after atlas growth. The
active backend still uses a whole-image Sokol update as a correctness fallback;
subregion upload and richer lifecycle callbacks remain deliberately deferred.

These changes should be proposed upstream when generally useful. Do not make
NativeKit GPU handles part of Skribidi.

Potential Sokol work:

- investigate and, if appropriate, upstream an image-subregion update primitive
  with origin, extent, source data, and row pitch;
- otherwise implement backend-specific partial upload privately behind the
  NativeKit image uploader.

Exit criteria:

- Failed uploads do not discard dirty state.
- Alpha/SDF updates do not expand to RGBA.
- Full-atlas fallback and partial-update paths produce identical images.

## Phase 5: NativeKit-owned Sokol backend

1. Move all `sg_setup` and `sg_shutdown` ownership into the NativeKit graphics
   runtime.
2. Centralize render-target, image, sampler, buffer, shader, and pipeline
   creation/destruction.
3. Add pipelines for NanoVG fills/strokes and alpha, SDF, and color glyphs.
4. Execute prepared operations inside caller-selected passes.
5. Reuse the NativeKit-Sokol generational-handle and command-batching work where
   it fits, but do not expose low-level Sokol commands as Canvas semantics.
6. Add backend state caching without reordering semantic operations.
7. Record statistics for passes, draws, pipeline changes, binding changes,
   uploads, transient bytes, and GPU-resource counts.

The NativeKit backend is the only runtime backend. NanoVG is used only for
path construction and tessellation, and its renderer callback adapter remains
limited to compatibility tests while direct preparation is migrated in.

Exit criteria:

- NativeKit is the only subsystem issuing `sg_*` calls.
- Shapes and text render in one NativeKit-owned pass when isolation is not
  needed.
- Resource destruction is deterministic across surface recreation and shutdown.

## Phase 6: minimal compositor and opacity layers

Implement a linear compositor walk:

1. Direct ordinary operations to the current target.
2. Resolve rectangular clips to scissors.
3. On `BeginLayer`, decide whether isolation can be elided.
4. If isolation is required, allocate a transient target and redirect children.
5. On `EndLayer`, restore the parent and emit `PreparedCompositeTarget` with
   group opacity and source-over blending.
6. Pool transient targets by size, format, sample count, and usage.
7. Track target dependencies and reject cycles.

Group opacity must be tested separately from multiplying each child alpha. A
layer containing overlapping translucent children is the canonical regression
case.

Initial limitations:

- source-over composition only;
- one color format and sample count;
- rectangular clips;
- no filters;
- bounded layer nesting;
- no target aliasing within a frame.

Exit criteria:

- The vertical slice renders background, text, an isolated translucent group,
  more text, and foreground decoration in exact order.
- The compositor can elide an opacity-1 layer when isolation has no semantic
  effect.

## Phase 7: Haxe Canvas and retained-UI transaction API

Expose a backend-neutral, batched API. Do not expose NanoVG methods directly.

Representative Canvas concepts:

```text
save / restore
transform
fill / stroke
drawImage
clipRect
beginLayer / endLayer
fillText
drawTextLayout
measureText
```

`fillText` creates or reuses a short-lived Skribidi layout internally.
Retained/editable text uses explicit text-layout handles so shaping and
measurement are not repeated unnecessarily.

1. Define versioned C transaction structures and validate them before mutation.
2. Bind coarse transactions through HXI.
3. Provide a growable, resettable Haxe command encoder similar to the proven
   Sokol command-buffer prototype.
4. Keep Haxe calls semantic; resource preparation and draw expansion happen
   natively.
5. Let retained UI and immediate Canvas produce the same semantic display-list
   format.

Exit criteria:

- No per-path, per-glyph, or per-draw HXI calls are required.
- Immediate Canvas and retained UI render through the same preparation,
  compositor, and backend pipeline.

## Phase 8: advanced clips and compositing

Add features only after the minimal compositor is measured and stable:

1. Rounded-rectangle and arbitrary path clips.
2. Choose stencil or mask targets per clip without exposing the choice publicly.
3. Ensure NanoVG internal stencil use cannot conflict with Canvas clipping.
4. Add destination-dependent blend modes through isolation where necessary.
5. Add filters and nested layers.
6. Add color-space and high-DPI rules for offscreen targets.

Tests must cover clips and layers interleaved with both paths and glyphs.

## Phase 9: external and 3D render-target producers

Treat 3D, video, cached subtrees, and future external textures as render-target
producers. UI consumes them through semantic `DrawRenderTarget` operations.

1. Define private producer readiness, size, format, generation, and dependency
   contracts.
2. Schedule producer passes before consumers.
3. Define synchronization and fallback behavior when a producer is unavailable.
4. Add sampling rules for size, filtering, alpha, and color space.
5. Keep scene, camera, decoding, and external-platform semantics outside the
   compositor.

Do not expose the internal render plan unless advanced users demonstrate a real
need for explicit graph control.

## Migration from the current example

Migrate in reversible steps:

1. Preserve the NativeKit public-renderer and showcase smoke tests as visual
   references.
2. Land the display-list arena and validation with no rendering changes.
3. Land the NanoVG recording backend and replay its prepared paths through the
   NativeKit Sokol backend.
4. Continue rendering text through the existing path temporarily.
5. Add direct alpha glyph batches and compare output.
6. Keep the public renderer on direct glyph batches.
7. Add R8 atlas ownership and the whole-atlas upload fallback.
8. Add the isolated opacity-layer vertical slice.
9. Remove glyph-as-NanoVG-image-pattern code.
10. Keep direct NanoVG-Sokol execution retired; use NativeKit's backend for
    golden and stress tests.

Each step should remain buildable and independently testable. Do not combine the
display-list format, NanoVG recorder, glyph renderer, compositor, and public Haxe
API into one review.

## Validation strategy

### Structural tests

- command validation and nesting;
- resource generation and ownership;
- deterministic preparation;
- target dependency and cycle detection;
- arena lifetime and reset behavior;
- failed upload acknowledgement behavior.

### Golden-image tests

- NanoVG path corpus;
- multilingual text corpus;
- alpha, SDF, and color glyph modes;
- shape/text/shape ordering;
- overlapping group opacity;
- clipping across path and glyph operations;
- scale factors and resized targets.

### Stress and performance tests

- large path counts;
- long multilingual documents;
- atlas growth and eviction;
- repeated retained frames with no semantic changes;
- deeply interleaved paths and glyph runs;
- nested layers within configured limits;
- surface recreation and repeated initialization/shutdown.

Track at least:

```text
semantic commands
prepared operations
vertices and indices
glyphs and glyph batches
NanoVG flushes
render passes
draws
pipeline and binding changes
atlas bytes uploaded
transient target count and bytes
arena allocations and growth
prepare/compositor/submit CPU time
```

## Fork and upstream policy

Keep NativeKit-specific work in NativeKit:

- semantic display list and Haxe protocol;
- compositor and render-plan compiler;
- NanoVG recording adapter;
- Sokol backend and shaders;
- glyph batching and GPU pipelines;
- NativeKit resource IDs and instrumentation.

Prefer upstream contributions when generally applicable:

- Sokol image-subregion updates;
- Skribidi dirty query/acknowledgement;
- Skribidi atlas texture generations (with lifecycle notifications only if
  generation queries prove insufficient);
- narrowly scoped NanoVG renderer-callback improvements needed by any recording
  backend.

Avoid carrying a deep NanoVG core fork unless measurements or missing callback
data make it necessary. Never merge Skribidi types into NanoVG core.

## First vertical slice

The first end-to-end target is deliberately bounded:

```text
window render target
    background prepared by NanoVG
    direct Skribidi alpha glyph batch
    isolated layer at opacity 0.6
        NanoVG-prepared path
        direct Skribidi alpha glyph batch
    composited layer
    foreground NanoVG path
```

It uses source-over blending, rectangular clips, one offscreen color target,
whole-atlas upload fallback, and the OpenGL Sokol backend.

Completion requires:

- exact shape/text/layer ordering;
- visibly correct group opacity for overlapping children;
- no glyph conversion through NanoVG image patterns;
- no Sokol calls outside the NativeKit backend/runtime;
- one batched Haxe transaction per display-list update;
- golden-image and 30-frame smoke tests;
- recorded CPU timing and render statistics;
- clean surface recreation and resource teardown.

After this slice is stable, prioritize partial atlas uploads, richer text modes,
advanced clips, and additional backends before introducing 3D dependencies.
