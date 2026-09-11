# NativeKit UI

NativeKit UI is the optional retained UI engine above NativeKit's platform and
surface APIs. Haxeon will own components, application state, reconciliation,
and resolved styles. This module will own the retained render tree, layout,
text, hit testing, accessibility projection, and batched rendering.

The vendored experimental implementation stack is:

- Clay for box layout;
- Skribidi for shaping, bidirectional editing, line layout, and glyph atlases;
- Sokol for GPU submission;
- an internal display list which keeps those dependencies out of the public ABI.

See the repository-level [`vendor/README.md`](../../vendor/README.md) for pinned revisions and
[`docs/integration-plan.md`](docs/integration-plan.md) for the integration
sequence and the policy on adapting versus rewriting upstream code.
The reusable NanoVG path API is documented in
[`docs/nanovg-path-preparation.md`](docs/nanovg-path-preparation.md).
The Haxe-facing graphics layering and verification strategy is documented in
[`docs/haxe-graphics-api.md`](docs/haxe-graphics-api.md).

All third-party integrations must sit behind private adapters. The public C ABI
will use opaque handles, fixed-width values, versioned structures, and validated
batched transactions. It must not expose Clay, Skribidi, NanoVG, or Sokol types.

The module now has a validated semantic display list, reusable NanoVG fill and
stroke path preparation, direct Skribidi glyph batches, a NativeKit compositor,
and a NativeKit-owned Sokol backend. Public rendering uses opaque NativeKit UI
resources and `nkui_renderer_render`; NanoVG does not own text or GPU
submission.

UI shader sources use `sokol-shdc`; generated headers are written below the
build directory and are never checked into the source tree. Configure the UI
build with `-DNK_SOKOL_SHDC=/path/to/sokol-shdc`, or put `sokol-shdc` on `PATH`.
The current generator languages are selected by `NKUI_SHADER_LANGUAGES` and
default to `glsl410:glsl300es:hlsl5:metal_macos:metal_ios:metal_sim:wgsl:spirv_vk`.
The generated descriptors are therefore ready for the intended Sokol backend
families. Select the compiled Sokol variant with `-DNK_SOKOL_BACKEND=glcore`
or `-DNK_SOKOL_BACKEND=gles3`; the default is `glcore` on desktop Linux and
`gles3` on Android. A build contains one Sokol backend variant at a time, so
this is not yet simultaneous OpenGL/GLES/Vulkan support in one process.

Build it with:

```sh
cmake -S . -B build-ui -GNinja -DNK_BUILD_UI=ON
cmake --build build-ui
ctest --test-dir build-ui --output-on-failure
```

Run the interactive public-API showcase with:

```sh
./build-ui/modules/ui/nativekit_ui_showcase
```

The same executable supports `--smoke-test`, which renders 30 frames and exits.

## Web / WASM

The first browser backend uses Emscripten, WebGL2, and the GLES3 Sokol
backend. Emscripten and the pinned `sokol-shdc` binary are kept under `.tools`
and ignored by Git:

```sh
./tools/setup-web.sh
./tools/build-web.sh
./tools/test-web.sh
python3 -m http.server --directory build-web/modules/ui 8080
```

Open `http://localhost:8080/nativekit_ui_c_api.html` for the interactive C ABI
showcase, or append `?smoke` to run its 30-frame browser smoke test. The
browser owns the frame loop through `nk_surface_set_frame_callback()`; no
Emscripten types appear in NativeKit's public headers.
