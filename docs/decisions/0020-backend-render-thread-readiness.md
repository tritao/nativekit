# ADR 0020: Backend render-thread readiness

## Status

Accepted as an implementation boundary. The desktop explicit backends already
run GPU submission on `RENDER`. GTK and the default Web backend intentionally
keep `PLATFORM = APP = RENDER` until their native surface handoff is complete.

This is a backend limitation at the surface boundary, not a limitation of the
sealed render-plan or GPU-batch contracts. Those contracts are ready to cross a
thread; these two backends still need a native context/framebuffer handoff.

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
destruction with the GTK main loop. The existing `share_surface` context path
is useful groundwork, but it is not this bridge: a shared `GtkGLArea` still
owns a widget callback and does not provide an independent offscreen target.

Until the bridge exists, GTK remains an aliased executor and live
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
3. Create the WebGL context on `RENDER` with
   `proxyContextToMainThread = EMSCRIPTEN_WEBGL_CONTEXT_PROXY_DISALLOW`.
   Offscreen-canvas contexts are pinned to the creating pthread.
4. Keep DOM/input/resize and `requestAnimationFrame` on `PLATFORM`; pass only
   immutable size/context-loss snapshots to `RENDER`.
5. Commit the frame from `RENDER` with explicit swap control. Context loss and
   teardown must be acknowledged by the render pthread before the canvas or
   context is destroyed on the browser thread.

The current `std::thread` executor cannot transfer a canvas because pthread
attributes are required at creation time. Web needs a small Emscripten-specific
thread-start path, plus asynchronous startup/fallback when SharedArrayBuffer or
OffscreenCanvas is unavailable. The normal Web build therefore remains aliased
and keeps its current browser compatibility.

## Implementation order

1. Add a Web threaded-capability build and browser smoke target. It should
   exercise canvas transfer, render-thread context creation, explicit frame
   commit, resize, and context-loss fallback while leaving the default build
   unchanged.
2. Add the GTK offscreen/shared-context bridge and a GTK/Xvfb integration test
   that proves the GTK callback only composites the retained image.
3. Enable the shared sealed-plan path for those physical modes. A live
   `SurfaceProducer` remains an explicit fallback until it publishes a retained
   `nk_graphics_image`.

This order keeps the already-proven D3D11, Metal, and Android handoff stable and
prevents a backend from claiming physical `RENDER` ownership before it can bind
its native target without platform calls.
