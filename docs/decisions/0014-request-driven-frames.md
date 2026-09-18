# ADR 0014: Request-driven frame scheduling for surfaces

## Status

Accepted. Continuous rendering remains the default; request-driven frames are an
opt-in mode on `nk_surface`, verified end to end on GTK and implemented for every
graphics backend.

## Decision

A surface frame callback is scheduled in one of two modes:

| Mode | Behavior |
| --- | --- |
| `NK_SURFACE_FRAME_CONTINUOUS` | The backend invokes the callback every vsync while one is installed. This is the default and the contract game loops already rely on. |
| `NK_SURFACE_FRAME_ON_DEMAND` | The backend invokes the callback only while a frame is pending, and stops scheduling work while the surface is idle. |

`nk_surface_request_frame()` records a pending frame. Requests coalesce: any
number of calls before the next frame produces exactly one callback. The
platform decides when that frame happens (frame clock, display link, or browser
animation frame), and a callback that requests its successor keeps an animation
running without re-arming anything by hand. The call requires platform-executor
affinity, so native work on other threads uses `nk_dispatch_to_app()` first.

Coalescing lives in one shared state object (`nk::core::FrameRequestState`) that
every backend embeds in its surface resource. `begin_frame()` consumes exactly
the requests that scheduled the frame being started, which is why a request
recorded from inside a callback schedules the next frame instead of being
swallowed by the current one.

Backends reach idle by disarming their own source: GTK removes its tick
callback, Web unregisters from the browser animation-frame loop, Windows kills
its surface timer, and iOS and macOS pause their display link or repeating
timer. Android keeps its platform frame source armed and skips the callback
instead, because the Java surface owns that loop; the callback gating is
identical from the application's point of view.

## Consequences

- UI code can request frames from animation and state changes instead of
  rendering permanently, with no change for existing continuous callers.
- `nk_surface_present()` on an idle on-demand surface composites the existing
  frame without invoking the application callback.
- Frame scheduling is now an explicit contract that the render-thread work can
  build on: the platform executor decides when a frame happens, and the draw
  step stays inside one callback.
- A future damage-tracking pass can replace "the application requests a frame"
  with "the UI marks regions dirty" without changing this API.
