# ADR 0020: Backend render-thread readiness

## Status

Accepted as an implementation boundary. The desktop explicit backends already
run GPU submission on `RENDER`. GTK and the default Web backend keep
`PLATFORM = APP = RENDER`; their physical render executors are opt-in because
they require native/browser capability checks.

This is a backend limitation at the surface boundary, not a limitation of the
sealed render-plan or GPU-batch contracts. Those contracts are ready to cross a
thread. The opt-in GTK and Web bridges below now provide the native
context/framebuffer handoff; default builds remain conservative for hosts that
cannot guarantee the required context and browser capabilities.

## GTK

NativeKit renders GTK surfaces through `GtkGLArea`. GTK makes the area's
`GdkGLContext` current before the `render` signal and integrates the area's
framebuffer after the signal returns. The current backend also discovers the
draw framebuffer through that current context, and `nk_surface_present()` queues
the GTK render callback. Moving the callback itself to `RENDER` would therefore
violate GTK's widget and context ownership rules.

The supported split is an offscreen/shared-context bridge:

```text
GTK main thread              RENDER
GtkGLArea lifecycle          shared GL context
resize/visibility           offscreen FBO + texture
render signal                UI/GPU batch into offscreen target
tiny texture composite  <--- completion
```

The bridge must keep the GTK callback as a compositor-only operation. It must
not call `nk_surface_*` from `RENDER`, and it must synchronize context/resource
destruction with the GTK main loop. GTK3 does not expose a public setter for
attaching a newly-created `GdkGLContext` to an existing share group, so the
threaded path automatically groups compatible surfaces on the same window
under the first surface's context. Each surface still owns an independent
offscreen target. Surfaces on different windows, or with incompatible context
options, remain independent and use the existing fallback behavior.

`NK_GTK_THREADED_RENDER=ON` enables the first bridge implementation. NativeKit
keeps the `GtkGLArea` on the GTK thread, creates a color-only offscreen FBO and
texture on `RENDER`, and lets the GTK render signal blit the completed texture
into the widget's framebuffer. The frame callback is scheduled from the GTK
tick callback rather than the GL render signal, so RENDER can bind the retained
context without racing GTK's compositor. The first bridge is deliberately
desktop-OpenGL/color-only; depth/stencil and GLES surfaces remain on the
default build until equivalent offscreen bindings are added.

Until that option is enabled, GTK remains an aliased executor and live
`SurfaceProducer` callbacks are allowed on the normal inline path.

## Web

The default Web backend creates a WebGL2 context on the browser thread and
uses the browser compositor for the default framebuffer. A context created on
the browser thread cannot be made current by a worker without using Emscripten's
proxy mode, which adds the latency we are explicitly trying to avoid.

The real split therefore needs an opt-in pthread build:

1. Build with `-pthread` and `-sOFFSCREENCANVAS_SUPPORT=1` (and
   `-sOFFSCREEN_FRAMEBUFFER=1` for the compatibility fallback).
2. Transfer the canvas to the render pthread with
   `emscripten_pthread_attr_settransferredcanvases()` before any WebGL context
   is created.
3. Create the WebGL context on `RENDER` with explicit swap control. NativeKit
   requests the no-proxy path and permits Emscripten's compatibility fallback
   when the browser cannot bind the context directly. Offscreen-canvas contexts
   are pinned to the creating pthread.
4. Keep DOM/input/resize and `requestAnimationFrame` on `PLATFORM`; pass only
   immutable size/context-loss snapshots to `RENDER`.
5. Commit the frame from `RENDER` with explicit swap control. Context loss and
   teardown must be acknowledged by the render pthread before the canvas or
   context is destroyed on the browser thread.

The Web bridge is enabled with `NK_WEB_THREADED_RENDER=ON`. It starts the
executor as an Emscripten pthread, transfers the pre-existing `#canvas` (or the
configured selector) before context creation, creates the WebGL context on
RENDER, and commits explicit-swap frames there. The default Web build remains
aliased and keeps its current browser compatibility; hosts must provide a
pthread-capable, cross-origin-isolated page for the opt-in mode.

## Implementation order

1. Add a Web threaded-capability browser smoke target. It should exercise
   canvas transfer, render-thread context creation, explicit frame commit,
   resize, and context-loss fallback while leaving the default build unchanged.
2. Keep the GTK offscreen bridge covered by the GTK/Xvfb target and lifecycle
   tests; add a visual compositor assertion when the UI test harness can run
   against an accelerated X server.
3. Enable the shared sealed-plan path for those physical modes. A live
   `SurfaceProducer` remains an explicit fallback until it publishes a retained
   `nk_graphics_image`.

This order keeps the already-proven D3D11, Metal, and Android handoff stable and
prevents a backend from claiming physical `RENDER` ownership before it can bind
its native target without platform calls.
