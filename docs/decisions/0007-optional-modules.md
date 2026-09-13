# ADR 0007: Optional GPU and UI modules

NativeKit's platform ABI remains independent of rendering and UI-framework
policy. Experimental higher layers live in the same repository for atomic
changes and shared CI, but build as optional targets.

`NativeKit::gpu` is the low-level, Haxeon-callable graphics adapter.
`NativeKit::ui` is the retained UI engine. UI and GPU have no dependency on
each other's public modules; either may use the same private Sokol runtime. UI
keeps its own rendering implementation and does not issue fine-grained work
through the Haxe-facing GPU ABI.

Haxeon UI depends on NativeKit UI, and Haxeon GPU depends on NativeKit GPU;
both optional modules depend on NativeKit core. NativeKit core never depends on
either optional module. Clay, Skribidi, NanoVG, and Sokol remain private
implementation dependencies whose types do not cross public ABIs.

The Haxeon interfaces follow the same direction: `NativeKitGpu` and
`NativeKitUI` each declare an explicit HXI dependency on `NativeKit`. Haxeon
projects shared ABI declarations from the core interface once, while each
optional interface contributes only its own functions and types.
