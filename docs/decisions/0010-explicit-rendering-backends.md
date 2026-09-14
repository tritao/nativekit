# ADR 0010: NativeKit surfaces own presentation across graphics backends

NativeKit needs desktop rendering on Windows and macOS without making its UI,
GPU resource API, or C ABI depend on platform graphics types. The public
`nk_surface` contract therefore describes a NativeKit-owned presentation
target, while `NativeKit::gpu` owns rendering commands, GPU resources, and its
Sokol integration.

## Decision

`nk_surface` remains a child presentation/render target associated with a
NativeKit window. The platform implementation owns the child host view/window,
swapchain or drawable, and any per-surface attachments. NativeKit GPU consumes
the frame target to render into that presentation target. Public handles remain
opaque and platform-neutral.

The graphics API values include OpenGL, OpenGL ES, Vulkan, D3D11, and Metal.
`nk_surface_make_current()` has API-specific behavior: it makes an OpenGL
context current, and prepares/acquires the current frame for explicit APIs.
Callers use it before retrieving a frame target or issuing backend rendering.
`nk_surface_present()` completes the prepared frame and performs any required
presentation step. With Metal, Sokol schedules the drawable when its command
buffer commits and NativeKit releases the drawable from the surface after
submission. A proc-address query is a GL operation and returns
`NK_ERROR_UNSUPPORTED` for APIs without a GL function table, including D3D11
and Metal.

`nk_surface_frame_target` keeps its original 40-byte prefix. Its extensible
tail carries opaque 64-bit tokens for the native device, execution
context/queue, depth/stencil target, and presentation object. Device and
context/queue tokens last for the surface lifetime, the depth/stencil token
lasts until resize, and color/presentation tokens are valid only while the
frame is prepared. NativeKit does not expose or transfer ownership of
Direct3D, Objective-C, or Sokol objects through this ABI. OpenGL continues to
use `native_target` for the current draw framebuffer and leaves the new tokens
zero.

Two surfaces created with `share_surface` use the same backend graphics device
and report the same `nk_graphics_device` identity. Their presentation targets
remain separate. Resize preserves the surface handle and recreates
size-dependent attachments; applications continue with the same GPU renderer.
Temporary lack of a drawable skips rendering. An unrecoverable device failure
emits `NK_EVENT_SURFACE_LOST`; the window survives while the application
recreates the surface and renderer.

## Consequences

- NativeKit UI continues submitting through `nkgpu_*` and contains no
  platform-backend branches.
- D3D11 and Metal implementations live in platform surface code and the
  NativeKit GPU backend adapter.
- `nk_surface_get_proc_address()` remains useful to GL consumers and is
  explicitly unsupported for D3D11 and Metal.
- Frame-target extensions are size-versioned and contain only opaque integer
  tokens, so the C ABI does not depend on platform SDK headers.
- `nk_graphics_image` keeps its backend-neutral identity; backend-specific
  retaining and device validation remain internal to NativeKit core/GPU.

## Validation target

Each backend must pass a surface clear/present smoke test, the GPU triangle,
offscreen render-target image import, and the NativeKit UI renderer smoke test.
Windows and macOS are advertised as supported only after the Haxe Explorer,
IME, resize/DPI, Graphics Lab, offscreen cube, and teardown paths pass on their
respective backend.
