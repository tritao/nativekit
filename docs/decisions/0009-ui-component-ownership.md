# ADR 0009: Haxe owns UI components, NativeKit owns UI mechanics

NativeKit UI is a language-neutral layout, rendering, input, text, and
accessibility substrate. Haxeon owns reusable components, application state,
reconciliation, themes, and event handlers.

The public UI boundary is a versioned, batched semantic tree transaction. It
must not expose Clay, Skribidi, NanoVG, Sokol, or platform-widget types, and it
must not require one FFI call per node or property.

The ownership model is:

- Haxeon owns `Button`, `Checkbox`, `Slider`, `ScrollView`, `TextField`, and
  other semantic components.
- NativeKit UI owns layout execution, display-list compilation, hit-test
  geometry, focus mechanics, text measurement, IME integration, and the
  accessibility projection.
- Clay is a private, frame-scoped layout implementation. Its hierarchy and
  render-command types do not cross the NativeKit boundary.
- Skribidi remains the private authority for shaping, wrapping, grapheme-safe
  editing, caret geometry, and glyph preparation.
- Native platform controls remain explicit escape hatches for services such as
  WebViews, dialogs, menus, and other behavior that genuinely requires the OS.

NativeKit retains transient geometry and correctness-heavy editor state, but it
must not become the authority for application state, component lifecycle, or
widget policy. Haxeon components may be replaced by another frontend later if
it can produce the same semantic transaction model.
