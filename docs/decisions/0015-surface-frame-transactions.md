# ADR 0015: Explicit surface frame transactions

> **Historical note:** references to `NativeKit::ui` describe the component now
> maintained by the sibling UIKit project.

## Status

Accepted. Frame acquisition, rendering, and presentation are now three named
steps with a token between them. All of them still run on one thread.

## Decision

The platform callback was the only place a frame could exist: `make_current`,
`get_frame_target`, and `present` described a frame only implicitly, and the
frame's lifetime was whatever the backend happened to track internally. A frame
is now an explicit transaction:

```
nk_surface_acquire_frame(surface, &frame, &target)   PLATFORM   target is immutable for the token
      (draw against target)                          RENDER      affinity declared, same thread today
nk_surface_present_frame(frame)                      PLATFORM   consume the token and present
nk_surface_cancel_frame(frame)                       PLATFORM   consume the token without presenting
```

The token is a serial bound to one surface. `nk_surface_present_frame()` finds
its surface from the token alone, which is what lets a renderer that only
received an `nk_surface_frame_target` finish the frame it was handed. Tokens are
single-use: a token whose frame already ended, or whose surface was destroyed,
is rejected with `NK_ERROR_INVALID_HANDLE`. One frame may be open per surface,
and a second acquire is `NK_ERROR_INVALID_REQUEST` rather than a silent
double-prepare.

The transaction is implemented once in the core (`src/core/surface_frame.cpp`)
on top of the existing `make_current` / `get_frame_target` / `present`
primitives, so every backend participates without backend-specific bookkeeping
and the legacy manual path keeps working unchanged. `nk_surface_frame_target`
gained the owning `frame` token as an appended field; older callers that size
the structure to the original prefix still work, and targets returned outside an
acquired frame report `NK_INVALID_HANDLE`.

## Consequences

- Rendering no longer has to happen inside the platform frame callback: the
  callback, the manual path, and the transaction path all describe the same
  three steps, and only the transaction path names the frame.
- Affinity is declared per step, so moving the draw step to
  `NK_EXECUTOR_RENDER` later is a threading change rather than an API redesign.
- Cancellation exists, so a failed or empty frame releases the surface instead
  of leaving it permanently unavailable.
- `NativeKit::gpu` and `NativeKit::ui` still call the legacy primitives; porting
  them to the transaction API is the follow-up that proves the boundary for
  real renderers.
