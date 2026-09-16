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
boxes, text, and images. Clay remains pinned as a private dependency; this
branch carries only small layout-kernel fixes intended to be upstreamable.

The C layout bridge uses a versioned transaction with fixed node records of
`NKUI_LAYOUT_NODE_RECORD_BYTES` bytes and a 16 MiB transaction bound. Node
capacity grows with submitted data rather than imposing a small framework node
limit. Resolved geometry is returned as one dynamically sized snapshot. The
bridge validates record sizes, transaction bounds, text ranges, IDs, and
geometry before replacing the submitted snapshot. FIT/GROW axes carry Clay's
existing minimum and maximum constraints, and nodes can opt into Clay's
existing aspect-ratio sizing with `aspectRatio`; NativeKit does not reimplement
either rule.

Custom nodes can provide intrinsic dimensions and an optional baseline through
the synchronous measurement callback. NativeKit caches successful results by
node ID, the node's application-defined measurement version, and the exact
four-axis constraints; changing the version invalidates only that node's
measurements. Haxe callers can use `LayoutNode.custom` with
`LayoutMeasuredContent` for canvas, native-view, and editor surfaces, or
`LayoutImageContent` for immutable image dimensions. For content that also
paints, `LayoutRenderableContent` pairs a provider with a retained Canvas
display list and repaints only when its version or resolved geometry changes.
The painter receives local-node geometry while the helper applies the resolved
clip and transform. `measureStats()` exposes cumulative cache hit/miss counts
for profiling.

The Haxe style resolver caches pure computed-style resolutions and returns
detached forks to callers, so mutating a resolved style cannot modify the
shared cache entry. `ComputedStyle.toLayoutStyle()` preserves the complete
FIT/GROW axis policy, including `min`, `max`, and `growWeight`.

GROW axes also accept a positive `growWeight`. Equal weights preserve the
normal equal-share behavior; for example, `LayoutAxis.grow(0.0, 0.0, 4.0)`
receives four times the unconstrained space of a sibling with weight `1.0`.
Clay redistributes space when a weighted child reaches its maximum.
Main-axis free space is controlled with `LayoutStyle.childDistribution`, which
supports start, center, end, space-between, space-around, and space-evenly.
`childAlignX` and `childAlignY` now describe cross-axis alignment; distribution
owns the axis selected by `direction`.
`childAlignX` uses `LayoutAlignmentX` and `childAlignY` uses `LayoutAlignmentY`;
`LayoutAlignmentY.Baseline` is available for `childAlignY` in horizontal rows;
NativeKit aligns text baselines and explicitly bottom-aligns children without
baseline metrics.
Set `LayoutStyle.wrapMode` to `LayoutWrapMode.Wrap` to flow children onto
additional rows or columns. `rowGap` and `columnGap` keep the two axes
unambiguous; wrapping distributes main-axis free space independently per line.
Set `LayoutStyle.alignSelf` to override the parent cross-axis alignment for an
individual child; `LayoutSelfAlignment.Baseline` is meaningful in horizontal
rows.

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

The `nativekit_ui_layout_invariants` test runs 2,000 deterministic randomized
trees by default. Increase coverage for sanitizer jobs with, for example,
`NKUI_LAYOUT_FUZZ_CASES=100000 build-ui/modules/ui/nativekit_ui_layout_invariants_test`.

With examples enabled, run the C API showcase with:

```sh
./build-ui/modules/ui/nativekit_ui_c_api
```

It supports `--smoke-test` for a bounded rendering run. The Haxe framework
showcase and framework tests live under `modules/ui/examples/ui_haxeon` and
`modules/ui/tests/haxeon`; generated bindings are checked with
`modules/ui/tools/check-hxi.sh`.

For a static or mostly static Haxe tree, `UiContext.submitCached(build, frame,
cacheKey)` can reuse the previously submitted tree and layout. Reuse is
invalidated by state, interaction, stylesheet/theme, animation, and gesture
revisions; `frame.deltaSeconds` is still advanced by the animation and gesture
systems, but ordinary frame-time jitter does not invalidate an otherwise static
submission. The cache key must identify the caller's build inputs.

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
Native shared builds also use HarfBuzz's `HB_MINI` profile by default, removing
legacy and AAT shaping from the private copy while retaining OpenType shaping.
Set `NKUI_ENABLE_HARFBUZZ_MINI=OFF` when those font formats are required.
Release builds also compile the private HarfBuzz copy with size-focused
optimization by default; set `NKUI_ENABLE_HARFBUZZ_SIZE_OPTIMIZATION=OFF` when
shaping throughput is preferred over binary size.
NativeKit does not expose Clay's internal debug view, so it is excluded by
default as well. Set `NKUI_ENABLE_CLAY_DEBUG=ON` when developing against that
private Clay API. NativeKit also does not currently expose Skribidi language
attributes, so its embedded BudouX word-break models are excluded by default;
set `NKUI_ENABLE_BUDOUX=ON` when integrating language-specific attributes
through the private Skribidi path.

The browser owns the frame loop through `nk_surface_set_frame_callback()`;
Emscripten types do not appear in NativeKit's public headers.
