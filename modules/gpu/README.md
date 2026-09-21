# NativeKit GPU module

This isolated experiment uses NativeKit for its window, event loop, graphics
context, framebuffer sizing, and presentation. `sokol_gfx.h` owns only
rendering resources and draw submission. It deliberately does not use
`sokol_app.h`.

`NK_SOKOL_BACKEND=glcore`, `gles3`, `d3d11`, or `metal` selects the default
surface API and the single runtime used by a normal build. Defaults are
`glcore` on desktop Linux, `d3d11` on Windows, `metal` on macOS, and `gles3` on
Android. The renderer runtime is selected from the surface's actual API.
Sokol is pinned in `CMakeLists.txt` so changes to its source-level API cannot
silently change the adapter.

On desktop Linux, `-DNK_BUILD_GPU_BACKEND_MATRIX=ON` includes independent
GLCore and GLES3 runtimes in the same binary. `nkgpu_surface_create_for_api()`
then selects a runtime from each surface's actual graphics API, so both kinds
of renderer may coexist. Renderer calls are graphics-thread serialized and
only one pass may be active at a time; resources remain owned by the renderer
that created them. The backend-matrix test alternates frame submission between
both runtime variants. Vulkan still requires its own Sokol runtime and adapter.
Windows D3D11 and macOS Metal are experimental until the Explorer and Graphics
Lab pass their native end-to-end checks.

The public shader API requires an explicit `ShaderLanguage`: GLSL for GL,
HLSL5 for D3D11, and MSL for Metal. The C renderer exposes
`Ready`, `FrameActive`, and `Lost` states. A window frame uses
`nkgpu_begin_frame()` / `nkgpu_end_frame()`; general offscreen work uses
`nkgpu_frame_begin()` / `nkgpu_begin_render_pass()` / `nkgpu_end_pass()` /
`nkgpu_end_frame()`. UI render-plan passes use the same general attachment
path, including when a sealed batch is being recorded. Resource creation and
destruction require no active pass, while binding and drawing require one. A
fatal backend or device failure moves the renderer permanently to `Lost`:
rendering and resource creation return `NKGPU_ERROR_DEVICE_LOST`, and callers
must destroy and recreate that renderer.
Destruction remains safe after loss. Surface resize and DPR changes preserve
the renderer; they update the swapchain/framebuffer dimensions without
discarding UI or text state. GPU statistics are queryable with
`nkgpu_renderer_get_stats()`, and UI renderer statistics expose the GPU and
atlas counters through `nkui_renderer_get_stats()`.

Destroying a renderer releases its remaining GPU resources and unfinished
builders, while Haxe resource `dispose()` methods are idempotent. Handles from
another renderer are rejected.

Build and run from the NativeKit repository root:

```sh
cmake -S . -B build-gpu -GNinja -DNK_BUILD_GPU=ON
cmake --build build-gpu
./build-gpu/modules/gpu/nativekit_gpu_triangle
```

For an explicit GLES3 build on Linux:

```sh
cmake -S . -B build-gpu-gles -GNinja \
  -DNK_BUILD_GPU=ON -DNK_SOKOL_BACKEND=gles3
cmake --build build-gpu-gles
```

For a bounded 30-frame run (suitable for Xvfb):

```sh
xvfb-run -a ./build-gpu/modules/gpu/nativekit_gpu_triangle --smoke-test
```

## Validation matrix

Set `NK_BUILD_TESTS=ON`, then build and run CTest for each configuration:

| Configuration | `NK_BUILD_GPU` | `NK_BUILD_UI` |
| --- | --- | --- |
| Core only | `OFF` | `OFF` |
| GPU only | `ON` | `OFF` |
| UI (GPU dependency enabled automatically) | `OFF` | `ON` |
| GPU + UI | `ON` | `ON` |

Use `NK_BUILD_SHARED=ON` and `OFF` to cover shared and static libraries. On
desktop Linux, use `NK_SOKOL_BACKEND=glcore` or `gles3` for a single runtime;
`NK_BUILD_GPU_BACKEND_MATRIX=ON` builds both runtime variants together. The
GPU runtime test independently verifies both Sokol runtime variants. With UI
enabled, `nativekit_ui_public_renderer_smoke` checks the public UI renderer on
the configured GPU backend.

`nativekit_gpu_contract_smoke` covers shader-language validation, renderer
ownership, renderer cleanup, the lost-state contract, injected allocation and
present failures, and repeated retained `GraphicsImage` use after generic image
and renderer destruction. `nativekit_gpu_batch_submit` covers multi-pass batch
replay, the sealed/immutable contract, retained resources outliving caller
destruction, deferred resource destruction, handle and record validation, and
batch ownership. `nativekit_ui_stress` runs a seeded 120-frame UI
render sequence with changing DPR, surface bounds, and short-lived offscreen
images; its live-resource limits catch unbounded growth. The public UI renderer
smoke also checks fractional/integer scale atlas behavior and text bounds.
`modules/gpu/tools/test-haxeon.sh` compiles and runs the Haxe wrappers, including
wrong-renderer checks, resource disposal, generic render/copy passes, and the
retained `GraphicsImage` lifetime. `modules/gpu/tools/check-hxi.sh` checks
the generated binding contract. The UI C API Showcase smoke test remains the
end-to-end UI rendering check.

For each build directory, run:

```sh
cmake --build build-gpu
ctest --test-dir build-gpu --output-on-failure
```

Generate the curated HXI binding for the adapter:

```sh
modules/gpu/tools/check-hxi.sh
```

Run the end-to-end Haxeon triangle test with:

```sh
modules/gpu/tools/test-haxeon.sh
```

The scene is assembled in Haxe from generic buffers, shaders, pipeline
attributes, bindings, and draw calls. There is no triangle-specific operation
in the native adapter. The stress test constructs an RGBA8 checkerboard in Haxe
and renders 400 independently positioned textured quads per frame. State,
inline uniform data,
and draws are encoded with `nativekit.gpu.CommandBuffer` and sent
through `nkgpu_submit_commands` in one HXI call. Its storage grows automatically
and can be reset and reused without reallocating each frame. The immediate calls
remain available for simple rendering and debugging.

`nkgpu_batch_*` builds the same packed records into a sealed submission: the
batch records an ordered list of window, render, compute, or copy passes, pins
every resource the passes and records reference, and replays them as one frame.
Recording does not touch GPU state, so a batch can be built while another frame
is active, and a sealed batch can be submitted more than once. Retained handles
stay valid after the caller destroys its own references, and deferred backend destruction runs
when the last batch holding them is destroyed. Submission ends the frame with
deferred presentation, leaving presentation to the surface owner, and is the
seam that later moves onto the render executor (ADR 0017).

Renderer, resource, and builder handles are distinct one-word value types in
the public C ABI. Their IDs encode a resource kind, generation, and pool slot;
the raw token stays internal to the adapter. HXI projects each C type to a
distinct nominal Haxe abstract, preventing cross-type calls before runtime.

Measure the HXI call boundary independently of graphics work with:

```sh
modules/gpu/tools/benchmark.sh
```

On the initial x86-64 Linux test machine, one million scalar HXI calls took
317–347 ms across five warm runs (about 317–347 ns per call). A single call
which performed the same million operations in native code took about 0.8 ms.
In the first packed-command benchmark, 10,000 simulated draw records took about
3.33 ms as individual HXI calls and 0.030 ms as one packed submission, including
native command parsing. This measures CPU submission overhead rather than GPU
rendering. In the initial rendered stress run, 400 quads averaged about 2.57 ms
of immediate CPU submission time per frame versus 0.57 ms for command encoding
and batched submission. Both measurements exclude presentation and GPU time.

The GTK backend renders through a `GtkGLArea`. Its framebuffer is not assumed to
be zero: the prototype queries the current draw framebuffer after NativeKit
makes the surface current and passes that value in Sokol's `sg_swapchain`.

The generic NativeKit graphics-image API lets a producer expose a retained
sampled image to consumers such as the UI compositor without exposing
GPU-specific handles. Backend-specific resource creation stays behind the
NativeKit GPU surface.

## GPU feature-envelope direction

The portable C API is being expanded in milestones rather than mirroring
`sokol_gfx.h` directly. The first envelope keeps the existing handle and
builder model while adding descriptor-backed buffers and images, explicit
image usages and formats, general render passes with four color attachments,
depth/stencil actions, optional resolve images, richer pipeline state,
instanced vertex-buffer layouts, viewport commands, capability/limit queries,
compute shader metadata, compute pipelines and passes, storage-buffer/image
views, dispatch commands, compute-capable sealed batches, versioned command
stream envelopes, and an opaque native device/context escape hatch.

NativeKit now layers a private `nk_sokol_transfer_api` beside Sokol's regular
dispatch table. GLCore and GLES3 expose real buffer/image transfers plus
fence-backed asynchronous image and buffer readback, including tightly packed
`R32_UINT` rectangles suitable for CAD picking and arbitrary storage-buffer
ranges suitable for sensor results. D3D11 uses staging resources and event
queries, while Metal uses blit encoders and shared readback buffers. The
portable surface does not expose backend fences or native resource structs. The
WebGL build uses WebGL2 staging and synchronous readback, so it exposes the
same transfer/readback operations but does not promise native asynchronous
completion semantics. Per-format transfer capabilities are reported by
`nkgpu_query_image_format_support()`: use its `copy` and `readback` fields in
addition to the resource-usage fields. WebGL2 supports depth textures for
rendering, sampling, and framebuffer copies, but its `readPixels()` contract
does not include `DEPTH_COMPONENT` or `DEPTH_STENCIL`; depth image readback is
therefore reported unavailable on Web. GLCore, D3D11, and Metal expose opaque
timestamp queries when the runtime provides them; unsupported backends report
that capability as unavailable.

## Current GPU API

Build render work from generic `nkgpu_image_desc` resources and
`nkgpu_render_pass_desc` attachment descriptions. A frame may contain window,
render, compute, and copy passes; sealed batches can retain and replay the same
command records. `nkgpu_batch_append_render_pass()` is the binding-friendly
form for general attachment passes. Surface frame acquisition and presentation
remain separate from render submission: acquire an immutable frame target on the
platform executor, submit the sealed batch on the render executor, then present
or cancel the frame. Use `nkgpu_image_get_graphics_image()` when a sampled image
crosses into the UI compositor or another NativeKit module. The returned
graphics-image handle is borrowed from the image and must be retained before
outliving it.

The Haxe binding exposes the same flow as `Surface.acquireFrame()`,
`Batch.submit(frame)`, and `SurfaceFrame.present()` or `cancel()`. Generic
render passes use `Batch.renderPass()` without requiring callers to construct
pointer-bearing native batch records.

Use `nkgpu_image_desc.type` for 2D, array, cube, and cube-array image shapes,
and retain the image shape in shader binding metadata with
`nkgpu_shader_texture_type()`. Cube-array resources use six-layer groups in
the portable descriptor and are sampled as array textures, preserving the
array-layer data layout without requiring a backend-specific cube-array view.

Query `nkgpu_features` and `nkgpu_limits` before optional compute, storage,
transfer, readback, or timestamp work. The feature envelope reports buffer and
image transfer directions separately, so callers can gate
`nkgpu_buffer_to_image()` and `nkgpu_image_to_buffer()` without identifying the
selected backend. `nkgpu_readback_begin_buffer()` is the portable path for
asynchronous storage-buffer results, and
`nkgpu_timestamp_begin_desc()` / `nkgpu_timestamp_end()` provide named,
backend-hidden GPU timing scopes where timer queries or counter samples are
available. Use `nkgpu_timestamp_collect()` to poll several completed scopes
with one NativeKit call and `nkgpu_timestamp_get_label()` to retrieve their
copied labels. Unsupported operations return
`NKGPU_ERROR_UNSUPPORTED`, so callers do not need to identify the selected
backend. D3D11 depth image copies remain whole-subresource operations because
that backend does not permit depth-stencil source rectangles; D3D11 depth
subregion image copies report `NKGPU_ERROR_UNSUPPORTED`, while full-subresource
copies remain available.
