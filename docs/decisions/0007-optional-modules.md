# ADR 0007: Optional Sokol and UI modules

NativeKit's platform ABI remains independent of rendering and UI-framework
policy. Experimental higher layers live in the same repository for atomic
changes and shared CI, but build as optional targets.

`NativeKit::sokol` is the low-level, Haxeon-callable graphics adapter.
`NativeKit::ui` is the retained UI engine. UI implementation code may use Sokol
directly or through a future private reusable integration target; it must not
issue fine-grained draw work through the exported Haxe-facing Sokol ABI.

The dependency direction is Haxeon UI to NativeKit UI to NativeKit. NativeKit
never depends on either optional module. Clay, Skribidi, NanoVG, and Sokol are
private implementation dependencies and their types do not cross public ABIs.
