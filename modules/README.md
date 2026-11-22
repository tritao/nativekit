# NativeKit modules

Optional modules share NativeKit's repository and CI while preserving one-way
dependencies and independently consumable CMake targets.

| Module | CMake option | Target | Purpose |
|---|---|---|---|
| [`gpu`](gpu/) | `NK_BUILD_GPU` | `NativeKit::gpu` | Low-level GPU C API backed by Sokol |
| [`audio`](audio/) | `NK_BUILD_AUDIO` | `NativeKit::audio` | miniaudio playback, mixer graph, and DSP |
| [`credentials`](credentials/) | `NK_BUILD_CREDENTIALS` | `NativeKit::credentials` | OS-protected storage for small binary credentials |
| [`filesystem`](filesystem/) | `NK_BUILD_FILESYSTEM` | `NativeKit::filesystem` | Root-scoped filesystem access |

All modules are disabled by default. UIKit and SceneKit now live as sibling
projects in the Materia repository and consume NativeKit through its public
core and GPU targets.

## Packaging

Each module is its own library, and enabling one never changes the core
`nativekit` library, so packages can share one installed copy of it. Installed,
each module is a component of the `NativeKit` CMake package with its own export
file:

```cmake
find_package(NativeKit 0.1 REQUIRED COMPONENTS gpu audio)
target_link_libraries(app PRIVATE NativeKit::nativekit NativeKit::gpu NativeKit::audio)
```

A missing component fails a `REQUIRED` search with a message naming it; without
`COMPONENTS`, every module installed in the prefix is loaded. Modules built in
separate configurations can be installed into one prefix.

## Module rule

`modules/` contains independently consumable libraries layered on top of
NativeKit. A component belongs here when it has its own CMake target and public
API and can primarily consume NativeKit through that public interface.

Capabilities that share NativeKit runtime state, handles, event delivery,
lifecycle, or platform backend machinery belong to the main NativeKit library.
GPU is a module; HTTP networking, clipboard, and accessibility are NativeKit
capabilities.
