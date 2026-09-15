# NativeKit modules

Optional modules share NativeKit's repository and CI while preserving one-way
dependencies and independently consumable CMake targets.

| Module | CMake option | Target | Purpose |
|---|---|---|---|
| [`gpu`](gpu/) | `NK_BUILD_GPU` | `NativeKit::gpu` | Low-level GPU API backed by Sokol for Haxeon and C callers |
| [`ui`](ui/) | `NK_BUILD_UI` | `NativeKit::ui` | UI layout/rendering boundary with private Clay/Skribidi adapters |
| [`audio`](audio/) | `NK_BUILD_AUDIO` | `NativeKit::audio` | Sound playback and mixing backed by miniaudio |

All modules are disabled by default. Third-party implementation types must not
cross their public C ABIs.

## Module rule

`modules/` contains independently consumable libraries layered on top of
NativeKit. A component belongs here when it has its own CMake target and public
API and can primarily consume NativeKit through that public interface.

Capabilities that share NativeKit runtime state, handles, event delivery,
lifecycle, or platform backend machinery belong to the main NativeKit library.
GPU and UI are modules; HTTP networking, clipboard, and accessibility are
NativeKit capabilities.
