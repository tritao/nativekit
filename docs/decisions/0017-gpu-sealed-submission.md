# ADR 0017: GPU sealed submission batches

## Status

Proposed. Nothing in this document is implemented yet; it is the design note
that should be agreed before the render thread is enabled, so the artifact that
crosses threads is defined before code depends on it.

## Context

ADR 0015 gave surfaces explicit frame transactions and ADR 0016 gave UI an
immutable, reference-counted render plan. Neither is what a render thread needs
to consume, because `NativeKit::ui` submits through `NativeKit::gpu`:

1. UI builds a plan, seals it, then translates it into GPU work while rendering.
2. GPU applies state and draws immediately inside `nkgpu_begin_frame()` and
   `nkgpu_end_frame()`, addressing resources by handle.
3. `nkgpu_submit_commands()` already defines a packed, little-endian command
   stream of opcode and size records — a binary format, not a C++ object.

So the submission that would move to another thread is a *GPU* batch, and it
must be representable without any UI or language-binding type. A UI-level seal
cannot cross that boundary, and a core-level "sealed plan" would have to be a
binary blob core cannot validate.

## Proposal

Add an explicit seam batch to `NativeKit::gpu`:

```
nkgpu_batch_begin(renderer, target, &batch)     build side: records work, retains handles
nkgpu_batch_append_command(batch, bytes, size)  packed records as nkgpu_submit_commands defines
nkgpu_batch_append_draw(batch, ...)             or field-wise equivalents of the immediate API
nkgpu_batch_retain(batch, resource_kind, handle)
nkgpu_batch_seal(batch)                         freezes the batch: no further appends
nkgpu_batch_submit(renderer, batch)             render side: begin/end frame internally
nkgpu_batch_destroy(batch)
```

Invariants:

| Concern | Rule |
| --- | --- |
| Immutability | A sealed batch never changes. Appending after seal is an error, not a silent no-op. |
| Ownership | The batch owns its command bytes and retains every GPU resource handle it references, releasing them on destroy. Handles stay valid even if the caller destroys its own references. |
| No callbacks | A batch contains data and handles only. Sampler/image producers, custom effect callbacks, and anything language-bound must be resolved into handles before sealing. |
| Target binding | The batch carries the render target it was recorded for. Presentation and surface frame acquisition stay with `nk_surface_frame` on the platform executor, so a batch never implies ownership of a surface. |
| Failure | Submit is atomic from the caller's view: a batch that fails validation is rejected before any GPU state changes, and a batch referencing a destroyed resource fails at submit rather than drawing garbage. |

## How the layers compose

```
UI:   build/layout/record → RenderPlan → seal            (semantic freeze, ADR 0016)
GPU:  translate plan → commands + retained handles → seal (submission freeze, this ADR)
      platform executor acquires nk_surface_frame, render executor submits, platform presents
```

The UI seal answers "can this frame's meaning change while it renders". The GPU
batch answers "can this submission run on another thread while the next one is
recorded". A render thread needs the second; the first is what keeps the plan
valid long enough to translate it.

## Open questions

1. Granularity: one batch per frame, or one per pass so a batch maps to the
   existing pass model and can be cached by pass cache key?
2. Ephemeral data: the immediate API currently applies uniform and binding state
   per draw. A batch has to snapshot that state into its byte stream, which
   changes how `nkgpu_apply_*` state is represented but not what it means.
3. Intermediate targets: whether batches may allocate render targets, or whether
   all targets must exist before sealing so a batch never allocates.
4. Thread ownership: confirm that handles and the resource registry can be
   looked up from the render executor, or whether the batch pre-resolves them
   into backend tokens at seal time.
5. Validation of cache keys and content generations at submit, so a batch built
   from stale prepared data fails loudly instead of drawing a stale frame.

## Migration

1. Implement `nkgpu_batch_*` on top of the existing command stream and resource
   registry, with the immediate path unchanged.
2. Translate UI render-plan execution to record one batch and submit it, still on
   the platform executor. Behavior and visuals must not change.
3. Add the deferred submit path and run submission on `NK_EXECUTOR_RENDER`,
   keeping acquisition and presentation on the platform executor.
4. Only then consider the public UI plan handle, since a plan handle only matters
   once UI build and GPU submission can genuinely be decoupled.
