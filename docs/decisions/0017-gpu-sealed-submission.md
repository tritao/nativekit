# ADR 0017: GPU sealed submission batches

> **Historical note:** references to `NativeKit::ui` describe the component now
> maintained by the sibling UIKit project.

## Status

Implemented through migration step 3. `nkgpu_batch_*` records, seals, retains,
and replays one frame; UI render-plan execution records frames into a batch
instead of drawing them inline; and submission declares render-executor
affinity while acquisition and presentation keep platform-executor affinity.

The render thread itself is still not enabled: the platform, application, and
render executors remain aliases of the `nk_init()` thread, so submission runs on
that thread today. The affinity contract is enforced now, which makes moving
submission a change of dispatch rather than a redesign.

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
nkgpu_batch_begin(renderer, &batch)             build side: records work, retains handles
nkgpu_batch_append_pass(batch, &pass)           window or offscreen target pass, in order
nkgpu_batch_append_command(batch, bytes, size)  packed records as nkgpu_submit_commands defines
nkgpu_batch_seal(batch)                         freezes the batch: no further appends
nkgpu_batch_submit(renderer, batch, target)      render side: binds, replays, ends the frame
nkgpu_batch_destroy(batch)
```

Retention is implicit rather than a separate `nkgpu_batch_retain` call: every
handle a recorded pass or command record references is resolved and pinned when
it is appended, so a caller cannot describe work it does not own and cannot
forget to retain it. Two records were added to the packed stream so a batch can
express everything the immediate apply API can apart from pass boundaries:
`NKGPU_COMMAND_APPLY_SCISSOR`, and `NKGPU_COMMAND_APPLY_GRAPHICS_IMAGE`, whose
handle is retained through the core image registry rather than the adapter's
resource pools.

Invariants:

| Concern | Rule |
| --- | --- |
| Immutability | A sealed batch never changes. Appending after seal is an error, not a silent no-op. |
| Ownership | The batch owns its command bytes and retains every GPU resource handle it references, releasing them on destroy. Handles stay valid even if the caller destroys its own references. |
| No callbacks | A batch contains data and handles only. Sampler/image producers, custom effect callbacks, and anything language-bound must be resolved into handles before sealing. |
| Target binding | The platform executor acquires an immutable `nk_surface_frame_target`; the render side binds that target and submits the batch. Presentation and surface frame acquisition stay with `nk_surface_frame` on the platform executor, so a batch never implies ownership of a surface. |
| Failure | Submit is atomic from the caller's view: a batch that fails validation is rejected before any GPU state changes, and a batch referencing a destroyed resource fails at submit rather than drawing garbage. |

## How the layers compose

```
UI:   build/layout/record → RenderPlan → seal            (semantic freeze, ADR 0016)
GPU:  translate plan → commands + retained handles → seal (submission freeze, this ADR)
      platform executor acquires nk_surface_frame and target, render executor binds and submits, platform presents or cancels
```

The UI seal answers "can this frame's meaning change while it renders". The GPU
batch answers "can this submission run on another thread while the next one is
recorded". A render thread needs the second; the first is what keeps the plan
valid long enough to translate it.

## Decisions taken in step 1

The open questions are answered as follows, and each answer is observable in the
landed API rather than left to a comment.

1. **Granularity: one batch per frame.** A batch records an ordered list of
   passes, because a frame's passes share frame state and the frame — not the
   pass — is the unit that would move to the render executor. Submission opens
   one frame, replays every pass, and ends it.
2. **Ephemeral state is snapshotted.** Uniform bytes live in the command stream,
   and pipeline, buffer, view, and sampler state are explicit apply records,
   exactly as the packed stream already describes them. Nothing implicit is
   carried over from the call site that recorded the work.
3. **Batches never allocate targets.** A pass target must already exist when the
   pass is appended, and is retained like any other resource. A batch contains
   data and handles only.
4. **Handles stay registry-addressable.** A retained slot keeps its generation
   and value, so replay resolves handles through the same registry the immediate
   path uses instead of pre-resolving them into backend tokens. A pinned slot is
   never reused, and its backend objects are destroyed when the last batch that
   holds it releases them.
5. **Validation happens at append time.** `nkgpu_batch_append_pass` and
   `nkgpu_batch_append_command` reject malformed records, foreign handles, and
   stale handles before anything is recorded. Submission re-validates ownership,
   seal state, and target availability before touching GPU state, so a rejected
   batch cannot change it.

External graphics images are the one retained handle that is not in the
adapter's pools: they belong to the core image registry, so a batch retains them
with `nk_graphics_image_retain()` and the same runtime/device check the
immediate path applies runs while the record is appended.

## Consequences of step 1

`nkgpu_batch_submit()` ends its frame with deferred presentation, so a batch is
the render-side commit rather than a whole frame transaction; presentation stays
with the surface owner. Retained resources outlive the caller's handles but not
their renderer: destroying a renderer releases its batches and any deferred
destruction they were holding.

## Migration

1. ~~Implement `nkgpu_batch_*` on top of the existing command stream and resource
   registry, with the immediate path unchanged.~~ Done; covered by
   `nativekit_gpu_batch_submit`.
2. ~~Translate UI render-plan execution to record one batch and submit it, still
   on the platform executor. Behavior and visuals must not change.~~ Done. The
   plan executor records every frame that has no live surface producer; frames
   that composite one stay inline because a producer renders through callbacks
   a batch cannot carry. Stream-buffer appends moved out of the pass
   requirement, because a recorded frame fills its buffers before any pass
   opens, and Sokol rewinds the append cursor per frame either way.
3. ~~Add the deferred submit path and run submission on `NK_EXECUTOR_RENDER`,
   keeping acquisition and presentation on the platform executor.~~ Done.
   `nkgpu_batch_submit()` requires render-executor affinity and the surface
   frame transaction requires platform-executor affinity, both enforced and
   covered by tests; presentation still happens where the surface owner
   presents.
4. Only then consider the public UI plan handle, since a plan handle only matters
   once UI build and GPU submission can genuinely be decoupled.
