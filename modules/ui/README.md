# NativeKit UI

NativeKit UI is the retained UI layer above NativeKit core and GPU. NativeKit
core owns windows, surfaces, input, IME, and shared graphics-image handles.
`NativeKit::gpu` owns the public `nkgpu_*` rendering contract and its Sokol
implementation. `NativeKit::ui` owns layout, text preparation, retained display
lists, `RenderPlan`, and `UiRenderer`, which submits UI work through
`nkgpu_*`. The Haxe framework owns `View`, `RenderNode`, state, focus, events,
gestures, semantics, animation, and widgets.

Clay, Skribidi, and NanoVG remain private implementation dependencies. Clay's
custom render command carries the custom node identity and participates in its
normal ordering. Haxe paint callbacks record one retained display list per
custom node; the layout compiler inserts those commands at the matching marker
so custom drawing shares the same transforms, clipping, and sibling order as
boxes, text, and images. The vendored Clay source is unchanged.

The C layout bridge uses a versioned transaction with fixed node records of
`NKUI_LAYOUT_NODE_RECORD_BYTES` bytes and a 16 MiB transaction bound. Node
capacity grows with submitted data rather than imposing a small framework node
limit. Resolved geometry is returned as one dynamically sized snapshot. The
bridge validates record sizes, transaction bounds, text ranges, IDs, and
geometry before replacing the submitted snapshot.

The layout facade keeps Clay types private and uses its external paragraph
layout callback: Skribidi provides shaping, bidirectional text, line breaks,
and glyph atlases; Clay contributes box constraints and line placement.
`LayoutRenderCompiler` turns the resolved snapshot and attached custom paints
into one ordered render plan. `UiRenderer` owns UI-specific drawing vocabulary
such as paths, glyphs, images, and image composition, while NativeKit GPU owns
passes, resources, GPU state, and backend submission.

Configure with `-DNK_BUILD_UI=ON`; this also builds the GPU dependency. The
public CMake targets are `NativeKit::nativekit`, `NativeKit::gpu`, and
`NativeKit::ui`. Sokol headers, configuration, runtime ownership, and resource
handles stay inside `modules/gpu`. The canonical UI shaders are split by family
under `shaders/`; their GLSL, HLSL5, and MSL source variants are checked in as
`src/render/ui_shader_sources.h`, so ordinary builds do not require
`sokol-shdc`. The generator is the standalone [Sokol shader compiler](https://github.com/floooh/sokol-tools/blob/master/docs/sokol-shdc.md).

After editing a shader, regenerate the header with
`SOKOL_SHDC=/path/to/sokol-shdc modules/ui/tools/generate-shaders.sh`. Run
`SOKOL_SHDC=/path/to/sokol-shdc modules/ui/tools/check-shaders.sh` to verify the
generated header is current.

```sh
cmake -S . -B build-ui -GNinja -DNK_BUILD_UI=ON
cmake --build build-ui
ctest --test-dir build-ui --output-on-failure
```

With examples enabled, run the C API showcase with:

```sh
./build-ui/modules/ui/nativekit_ui_c_api
```

It supports `--smoke-test` for a bounded rendering run. The Haxe framework
showcase and framework tests live under `modules/ui/examples/ui_haxeon` and
`modules/ui/tests/haxeon`; generated bindings are checked with
`modules/ui/tools/check-hxi.sh`.

## Web / WASM

The browser backend uses Emscripten and WebGL2 through NativeKit core and GPU.
Set up Emscripten, then build and test with:

```sh
./tools/setup-web.sh
./tools/build-web.sh
./tools/test-web.sh
python3 -m http.server --directory build-web/modules/ui 8080
```

The Haxeon showcase fetches five compact, separately generated TTF assets by
default, so the initial WASM host is not accompanied by a font `.data` payload.
The subsetter collects characters from the showcase sources; set
`NKUI_HAXEON_SUBSET_FONTS=OFF` when the showcase needs the complete source
fonts. The subset build requires the Python `fonttools` package. For an offline
or deterministic bundle, build with
`NKUI_HAXEON_BUNDLE_FONTS=ON`; this puts the selected fonts back into
`nativekit_ui_haxeon.data`.

NativeKit currently accepts TTF/OTF data directly. Configure the web server to
apply Brotli or gzip content encoding to these assets for transfer compression;
serving WOFF2 would require a WOFF2 decoder before calling `FontCollection.addData`.

Supported GNU and Clang native builds enable function/data sections and linker
section garbage collection by default. Set `NKUI_ENABLE_SECTION_GC=OFF` to
disable this size optimization for a toolchain that does not support it.

The browser owns the frame loop through `nk_surface_set_frame_callback()`;
Emscripten types do not appear in NativeKit's public headers.
