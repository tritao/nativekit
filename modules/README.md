# NativeKit modules

Optional modules share NativeKit's repository and CI while preserving one-way
dependencies and independently consumable CMake targets.

| Module | CMake option | Target | Purpose |
|---|---|---|---|
| [`gpu`](gpu/) | `NK_BUILD_GPU` | `NativeKit::gpu` | Low-level GPU API backed by Sokol for Haxeon and C callers |
The GPU module is disabled by default. UIKit and SceneKit now live as sibling
projects in the Materia repository and consume NativeKit through its public
core and GPU targets.

## Module rule

`modules/` contains independently consumable libraries layered on top of
NativeKit. A component belongs here when it has its own CMake target and public
API and can primarily consume NativeKit through that public interface.

Capabilities that share NativeKit runtime state, handles, event delivery,
lifecycle, or platform backend machinery belong to the main NativeKit library.
GPU is a module; HTTP networking, clipboard, and accessibility are NativeKit
capabilities.
