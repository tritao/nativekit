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

The virtualization paths can be measured independently with:

```sh
HAXEON_DIR=/path/to/realtime-haxe \
NATIVEKIT_BUILD_DIR=/path/to/build-ui \
modules/ui/tools/benchmark-haxeon-virtual-list.sh
```

This runs 10,000- and 100,000-item fixed, model-backed, and tree collections,
reports the built row/node window, model extent/child calls, and submit time,
and fails if the logical item count increases the materialized viewport beyond
its overscan bound.

`VirtualViewport` contains the fixed-extent range math used by `VirtualList`;
`VirtualGrid` composes it on both axes, so table and grid widgets can reuse the
same windowing primitive without duplicating scroll-boundary behavior.
`TableView` adds fixed-width `TableColumn` metadata, sticky headers, row
selection, and accessible grid/row/cell semantics on top of that body.

Model-backed `ListView` and `TreeView` use an estimated extent for items that
have not entered the materialization window. Models must provide a positive
`estimatedExtent()` and report `extentIsUniform()` when that estimate is exact
for every item; uniform models skip extent callbacks entirely. Model revisions
must still change whenever keys, structure, content, or extents change.

This lazy path is safe for fixed-height models and for variable-height models
whose estimates are acceptable during convergence. Until an unmeasured
variable-height region is visited, `maxScrollY` and `scrollTo` can be
approximate, and measuring rows above the viewport can move the visual anchor.
TreeView also indexes collapsed branches lazily, but its initial setup still
queries root keys and default-expansion state across the root set. Anchor
preservation for corrected extents and a bulk root-metadata path are planned
follow-ups rather than prerequisites for using the current implementation.

## Remaining virtualization and integration work

The current branch intentionally leaves these follow-ups explicit:

1. Add anchor-preserving correction when measured variable-height rows change
   the prefix before the viewport.
2. Add optional exact extent summaries or prefix support so `maxScrollY` and
   `scrollTo` can converge without requiring every row to be visited.
3. Add a bulk/range root-metadata API for TreeView so large root sets do not
   require one-by-one root-key and default-expansion queries at startup.
4. Audit accessibility, rendering, picking, and raster-cache consumers against
   the shared resolved snapshot metadata, removing any parallel geometry rules.
5. Rebase or merge the branch onto current `main`, then rerun the native UI
   suite, Haxe framework smoke, virtualization/hit-test benchmarks, and UI
   visual regressions before publication.

`ListView` and `VirtualGrid` remain separate public controls: ListView owns
one-dimensional model-backed collections, while VirtualGrid owns two-axis
table/grid windows. They should continue sharing internal virtualization
primitives rather than becoming one combined widget. `VirtualList` can remain
as a small fixed-row primitive while callers migrate to a uniform ListView
model where model semantics are needed.

For a static or mostly static Haxe tree, `UiContext.submitCached(build, frame,
cacheKey)` can reuse the previously submitted tree and layout. Reuse is
invalidated by state, interaction, stylesheet/theme, animation, and gesture
revisions; `frame.deltaSeconds` is still advanced by the animation and gesture
systems, but ordinary frame-time jitter does not invalidate an otherwise static
submission. The cache key must identify the caller's build inputs.

## Commands, shortcuts, and edit history

`UiContext.commands` is the application command surface for menus, toolbars,
command palettes, and keyboard shortcuts. Commands are grouped into scopes;
the most recently activated scope wins, so a viewport or modal editor can
override a global action without replacing it:

```haxe
var history = new EditHistory();
context.commands.installHistoryCommands(history);
context.commands.register(new Command("scene.delete", "Delete",
    function() deleteSelection(),
    new Shortcut(UiKey.Delete)));

context.commands.pushScope("viewport");
// ... build the viewport/editor UI ...
context.commands.popScope("viewport");
```

`Command` exposes enabled and checked predicates for command-bound controls.
When application state changes outside a command action, call
`context.commands.refresh()` so cached UI submissions are invalidated.
`EventDispatcher` routes unhandled key-down chords to the active command
registry, while focused widgets can consume a key event first.

Contextual commands use `CommandContext` to receive the active
`EditorDocument`, selected object IDs, viewport ID, typed parameter accessors,
and invocation source. Set it once per active editor context with
`context.setCommandContext(...)`; widgets can read the same value through
`BuildContext.commandContext`.

`EditHistory` supports direct edits, compound transactions, undo/redo, and
continuous-edit coalescing. Give adjacent edits the same coalescing key and
merge their final state in the operation's merge callback to keep a drag or
slider gesture as one undo step. `EditHistory` can then be bound to the
standard Ctrl+Z/Ctrl+Y commands above. For document-owned edits, use
`EditorDocument.apply(...)` or `EditorDocument.begin(...)`; its savepoint
state makes undoing back to the saved history state clear the document's dirty
flag.

`PropertyDescriptor` and `PropertyEditor` provide the first inspector layer for
bool, integer, float, text, and enum values. Descriptors own model read/write
hooks, range and option validation, units, defaults, and read-only policy.
Mixed values are preserved for multi-selection; edits apply to the active
`EditorDocument`, so reset, slider, and text commits participate in undo/redo.

`PropertyInspector` composes those editors into a scrollable, sectioned
inspector. Pass descriptors directly to group them by descriptor category, or
pass explicit `PropertyInspectorSection` values when the application needs a
stable editor-specific order and section labels. Sections can be collapsed and
their state persists on the inspector instance; `onSectionExpanded` can be
used to persist that preference with the workspace. `PropertyInspector` also
forwards `applyValue(...)` to the section editor, so programmatic edits use the
same validation and undo path as visible controls.

Application types can extend that inspector without changing NativeKit's core
property enum. Register a `PropertyEditorExtension` in a
`PropertyEditorRegistry`, then use `PropertyType.Custom("sim.vec3")` and
`PropertyValue.Custom("sim.vec3", value)` in a descriptor. The extension owns
payload equality, formatting, parsing, validation, and its inspector `View`;
its `apply` callback routes edits through the same document history as built-in
controls. This is the intended boundary for simulation/CAD values such as
vectors, transforms, entity references, assets, colors, and curves.

`DockWorkspaceModel` stores a versioned, view-independent layout tree of
panels, tab groups, and ratio-based splits. `DockWorkspace` renders that tree
through `SplitView` and `Tabs`, building only the active tab page. The model
supports activation, close/open, docking mutations, split-ratio updates,
snapshots, JSON persistence, and `dock.close`/`dock.reset` command
registration. `DockWorkspaceInteraction` adds pointer-driven tab dragging and
resolved-geometry drop-zone targeting while keeping storage application-owned
through `DockWorkspacePersistence`. Persist its `DockWorkspaceSnapshot`
alongside project or user preferences; panel content and document state remain
owned by the application.

## Frame building and sealed plans

One frame has three stages: layout and recording build a display list, the
compiler turns it into a `RenderPlan` plus the prepared resources it references,
and the renderer executes that plan. A compiled plan is already a value, but its
resource set borrows prepared paths, glyph batches, images, and graphics-image
handles from the frame that produced them.

`SealedRenderPlan::seal()` crosses that boundary. A sealed plan owns the plan and
every resource it references, so no layout frame, session, or display list has to
stay alive, and a live `SurfaceProducer` - a callback by nature - is rejected
rather than captured. Sealed plans are reference counted and execute through the
same `execute_render_plan()` entry point, which is what will let one thread render
frame N while another builds frame N+1.

Sealing takes an `OwnedFrameResources`: the type shares prepared data as immutable
objects and retains graphics images, and it cannot hold borrowed bindings or a live
`SurfaceProducer`, so the callback case is a compile error rather than a rule.
Prepared text publishes those immutable objects itself through
`TextEngine::published_glyphs()`, which returns the same snapshot for the same
layout generation, geometry, scale, and mode, so sealing cost follows the number of
bindings rather than the size of the glyph buffers.

## Custom native window chrome

Desktop Haxe UI trees can provide their own borderless-window hit testing. Attach
the NativeKit window to the context once, then wrap any view in
`WindowChrome`:

```haxe
context.attachPlatformWindow(window.nativeHandle());

var titleBar = new WindowChrome("title-bar", WindowDecorationRegionKind.Drag,
    new Row("title-bar-content", [
        new KeyedView("close", new WindowChrome("close-client",
            WindowDecorationRegionKind.Client, closeButton))
    ]));
```

The fourth argument optionally overrides the cursor for that decoration region
with a standard `CursorShape`. When omitted, drag regions retain the normal
arrow cursor while hovering; resize regions use the corresponding resize
cursor. The platform owns any operation cursor after a drag or resize begins,
and supplies the same defaults for regions that are outside the Haxe hit-test
layer:

```haxe
var customZone = new WindowChrome("custom-zone", WindowDecorationRegionKind.Client,
    zoneContent, CursorShape.Hand);
```

This is useful for custom drag, resize, or client regions without creating a
window-global cursor override. Standard cursor images remain platform-owned;
custom cursor resources can still be applied through the normal window cursor
API when a whole-window cursor is needed.

After each layout submission, `UiContext` projects the visible, clipped node
bounds into `nk_window_set_decoration_regions`. Child declarations are emitted
after their parents, so a `Client` node can carve an interactive hole out of a
larger `Drag` or resize region. The projection is cleared when the window is
detached or the context is disposed. On platforms without
`NK_CAP_WINDOW_CUSTOM_DECORATIONS`, the annotations are harmless no-ops.

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
