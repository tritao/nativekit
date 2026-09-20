# ADR 0016: Sealed immutable render plans

## Status

Accepted. The UI module can now seal a compiled render plan into a
reference-counted, immutable object that owns everything it renders with. The
production API paths still execute unsealed plans; adopting sealing there is the
next step and is called out below.

## Decision

A compiled `RenderPlan` was already a value: the compositor copies commands into
the destination plan, including when it embeds custom-paint plans, so nothing in
the plan itself points back at a display list or a layout frame. What was *not*
self-contained was the resource set beside it: `FrameResources` stored raw
pointers to prepared paths, images, and glyphs owned by the frame that built
them, plus borrowed `nk_graphics_image` handles and live `SurfaceProducer`
references.

Sealing therefore formalizes two things:

| Concern | Contract |
| --- | --- |
| Plan | `SealedRenderPlan` owns the `RenderPlan` by value; no builder, session, or display list has to stay alive. |
| Resources | The plan is sealed with an `OwnedFrameResources`, whose bindings share prepared data as immutable objects and retain graphics images until the set is destroyed. |
| Callbacks | An owned set cannot hold a live `SurfaceProducer`: the borrowed bind and the producer bind are deleted there, so a producer (a callback by nature) has no immutable form. |

The type split is the contract rather than a runtime check: `FrameResources` is
the borrowed execution set that is only valid while its builder lives, and
`OwnedFrameResources` is the set that can be sealed, enforced by deleted
overloads instead of a validation pass that a caller could forget or bypass.
Sealed plans are reference counted, and
`execute_render_plan(renderer, sealed, window)` is the renderer entry point, so
frame N can render while frame N+1 is built.

The path to a sealed plan is deliberately data-oriented rather than
widget-oriented: a plan holds prepared geometry, glyph batches, images, target
descriptors, and pass dependencies. No Haxe object, closure, or paint callback
survives compilation, which is what makes sealing possible without a language
runtime in the sealed plan.

## Consequences

- The frame boundary is now explicit: build and record, seal, then render an
  immutable plan that cannot observe later mutations.
- Text and image data referenced by a sealed plan are owned copies or shared
  immutable objects, so later preparation passes cannot change what a sealed
  frame draws.
- Sealing cost follows the bindings, not the bytes. Prepared text publishes an
  immutable snapshot through `TextEngine::published_glyphs()`, which shares
  one object per layout generation, geometry, scale, and mode and stays valid
  after later preparation passes. Images and paths follow the same rule as their
  producers move to shared immutable data; the API paths still bind borrowed
  resources until they adopt sealing.
- Live surface producers are excluded by construction. A future shared-buffer or
  image-handle path should give producers a way to publish a retained
  `nk_graphics_image` snapshot, which *can* be sealed.
- `nkui_renderer_render_frame()` and the layout-session render path keep
  executing unsealed plans until they bind owned resources; that adoption, plus
  the public C API that exposes sealing to Haxe, remains the next step. Doing it
  requires deciding how a plan handle binds to an acquired surface frame from
  ADR 0015.

## Open questions

The display-list render path seals today. The layout-session path cannot seal
until one more thing is settled: only `LayoutRenderCompiler` tints glyphs, by
multiplying the tint into vertex colors after preparation
(`tint_glyphs`), and it does so per primitive. A published glyph snapshot is
shared and immutable, so it must not be tinted in place, and publishing an
untinted snapshot would render uncolored text.

Two options:

1. Make the tint part of the snapshot identity, so
   `published_glyphs(..., tint)` builds and shares one tinted snapshot per
   layout generation, scale, mode, and color. Colors per layout are few, so the
   cache still dedupes across frames, and the renderer stays unchanged.
2. Move tint out of vertex data entirely: carry it as a glyph-batch command
   property that the renderer applies. Cleaner long term, but it changes the
   batch contract and the renderer path.
