# ADR 0008: NativeKit GPU owns backend rendering; UI owns render planning

> **Superseded in ownership only:** the UI responsibilities described here now
> belong to the sibling UIKit project; the GPU boundary remains unchanged.

NativeKit needs one ordered rendering model for Canvas drawing, retained UI,
NanoVG paths, Skribidi text, offscreen layers, and producers such as 3D views.
The GPU/UI boundary keeps backend work generic while allowing UI to preserve
meaningful ordering and preparation.

NativeKit core owns platform windows and surfaces, input and IME, and the
graphics-image bridge used to share images between producers. `NativeKit::gpu`
owns the public `nkgpu_*` resource, pass, command, and submission contract, as
well as backend implementations such as Sokol. GPU does not know about widgets,
paths, glyph meaning, focus, or accessibility.

`NativeKit::ui` owns the semantic display list, compositor, `RenderPlan`,
layout and text preparation, and a small `UiRenderer` that expresses UI work in
terms of paths, glyphs, images, and image composition. `UiRenderer` performs
GPU work exclusively through `nkgpu_*`. It does not own a second GPU resource
registry, backend runtime, pass scheduler, or Sokol implementation.

Haxe owns `View`, `RenderNode`, application state, events, focus, gestures,
semantics, animation, and widgets. A custom Haxe painter records display-list
commands for its `RenderNode`. Clay's custom command supplies the ordered node
marker; NativeKit merges those commands into the same `RenderPlan` as ordinary
box, text, and image content. The vendored Clay source remains unchanged.

NanoVG prepares paths and Skribidi provides shaping, bidirectional text,
layout, editing, rasterization, and CPU glyph atlases. Their types remain
private to NativeKit UI and never cross its public C ABI.

The layout bridge has dynamically growing node capacity bounded by validated
transaction size rather than a small framework node constant. Its fixed record
size and transaction version are defined by `nativekit_ui_layout.h`; tests and
documentation verify those definitions.

`nk_surface` remains a platform presentation surface. UI render targets and
public GPU resources use their own handles and are never interchangeable with
surface handles. Another GPU backend belongs in NativeKit GPU; UI continues to
use the same public `nkgpu_*` contract.
