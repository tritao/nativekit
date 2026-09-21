# ADR 0007: Optional GPU and UI modules

> **Superseded in part:** UIKit was extracted to the sibling `uikit` project.
> NativeKit retains its core and optional GPU module.

NativeKit's platform ABI remains independent of rendering and UI-framework
policy. Experimental higher layers live in the same repository for atomic
changes and shared CI, but build as optional targets.

`NativeKit::gpu` is the low-level, Haxeon-callable graphics adapter and owns
the Sokol implementation. `NativeKit::ui` is the retained UI engine and depends
on `NativeKit::gpu` for all GPU work. UI retains layout, text shaping, render
preparation, and the render plan; it does not own a graphics runtime or issue
backend-specific calls.

Haxeon UI depends on NativeKit UI, and Haxeon GPU depends on NativeKit GPU;
both optional modules depend on NativeKit core. NativeKit core never depends on
either optional module. Building NativeKit UI enables the GPU dependency.
Clay, Skribidi, NanoVG, and Sokol remain private
implementation dependencies whose types do not cross public ABIs.

The Haxeon interfaces follow the same direction: `NativeKitGpu` and
`NativeKitUI` each declare an explicit HXI dependency on `NativeKit`. Haxeon
projects shared ABI declarations from the core interface once, while each
optional interface contributes only its own functions and types.
