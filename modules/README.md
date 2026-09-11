# NativeKit modules

Optional modules share NativeKit's repository and CI while preserving one-way
dependencies and independently consumable CMake targets.

| Module | CMake option | Target | Purpose |
|---|---|---|---|
| [`sokol`](sokol/) | `NK_BUILD_SOKOL` | `NativeKit::sokol` | Low-level Sokol graphics adapter for Haxeon and C callers |
| [`ui`](ui/) | `NK_BUILD_UI` | `NativeKit::ui` | UI layout/rendering boundary with private Clay/Skribidi adapters |

Both modules are disabled by default. Third-party implementation types must not
cross their public C ABIs.
