# NativeKit modules

Optional modules share NativeKit's repository and CI while preserving one-way
dependencies and independently consumable CMake targets.

| Module | CMake option | Target | Purpose |
|---|---|---|---|
| [`gpu`](gpu/) | `NK_BUILD_GPU` | `NativeKit::gpu` | Low-level GPU API backed by Sokol for Haxeon and C callers |
| [`ui`](ui/) | `NK_BUILD_UI` | `NativeKit::ui` | UI layout/rendering boundary with private Clay/Skribidi adapters |

Both modules are disabled by default. Third-party implementation types must not
cross their public C ABIs.
