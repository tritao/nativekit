# ADR 0021: One shader toolchain, with bindings from reflection

## Status

Proposed.

## Context

Shaders for NativeKit GPU are written once in GLSL and cross-compiled by
`sokol-shdc` to GLSL 4.10, GLSL ES 3.00, HLSL 5 and macOS Metal. The output
is checked in as C headers of source strings, so ordinary builds need no
shader tools, and a `--check` mode catches stale output. Nothing is compiled
at runtime.

Two consumers implement this separately, with near-identical Python scripts:
UIKit's UI shaders and SceneKit's scene renderer. ProceduralKit's GPU field
VM and Beartooth's planned renderer would add more. Both scripts run
`sokol-shdc -f bare`, which emits shader source without reflection, so:

- uniform blocks, members, attributes and texture slots are declared a second
  time by hand, as `nkgpu_shader_*` builder calls in C++;
- SceneKit's script keeps a table of uniform blocks that "must match" its GPU
  executor, and rewrites sampler uses with regular expressions;
- nothing declares shader variants, although renderers need them (multi-draw,
  alpha discard, optional per-instance fields);
- only `metal_macos` is generated, although NativeKit runs on iOS, which
  renders through Metal;
- `sokol-shdc` supports `@include`, `@block` and `@include_block`, but neither
  consumer uses them, and there is no shared library of shader snippets.

## Decision

### One tool in `modules/gpu/tools`

NativeKit GPU owns a single generator. Each consumer keeps its shaders and a
manifest next to them; the tool is shared.

- It pins `sokol-shdc` (by `sokol-tools-bin` commit) and `glslangValidator`,
  and validates every OpenGL variant.
- It generates `glsl410`, `glsl300es`, `hlsl5`, `metal_macos` and
  `metal_ios`. `wgsl` is added only if WebGPU becomes a backend.
- Output stays checked in, with a `--check` mode for CI. Ordinary builds
  still need no shader tools.

### The manifest declares programs and variants

A manifest lists each program's stages and its variants as named define
sets, passed to `sokol-shdc --defines`. Every variant is generated and
checked; nothing is compiled at runtime.

### Includes are `sokol-shdc`'s own

Shaders use `@include` and `@block`/`@include_block`. The manifest declares
include roots; if `sokol-shdc` resolves only relative paths, the tool stages
the roots for it. Placeholder substitution and regular-expression rewriting
are retired. NativeKit GPU ships a small include library of backend-neutral
snippets (packing and unpacking, color-space conversion, fullscreen
triangles); domain snippets stay with their consumers.

### Bindings come from reflection

The tool runs `sokol-shdc -f bare_yaml` and reads its reflection: uniform
blocks with their std140 members, attributes, images with their sample types
and samplers with their types. From it the tool generates:

- a C table per program and variant, consumed by a new
  `nkgpu_shader_create_from_table()`, which replaces hand-written builder
  calls;
- a language-neutral reflection file (JSON) that language bindings read. A
  Haxeon generator, owned by Haxeon's packages, turns it into typed uniform
  structs with std140 layout.

The shader source becomes the only declaration of its interface. A shader
change that the C or Haxeon side doesn't match fails at generation or
compile time, not at run time.

### Optional development reload

A development-only path may re-run the tool and recreate a shader through the
same table, for live editing. Shipped builds use only the checked-in output.

## Migration

1. The tool, its manifest format and the include library, with
   `nkgpu_shader_create_from_table()` and its tests.
2. UIKit's UI shaders move to the tool. Their generated sources must be
   byte-identical to today's for the existing slangs.
3. SceneKit's scene shaders move, retiring its uniform-block table and
   rewrites. Hashes of rendered test images must not change.
4. ProceduralKit's field VM and Beartooth's renderer start on the tool.
5. `metal_ios` is enabled for every consumer and checked on an iOS build.

## Rejected alternatives

- **Keeping per-consumer scripts.** Each new renderer copies the script and
  the hand-written bindings again.
- **Hand-written bindings checked by tests.** Tests catch drift late; the
  reflection makes drift impossible.
- **Runtime shader compilation.** It needs a compiler on every platform and
  breaks the rule that shipped builds contain only checked shader output.

## Consequences

- `sokol-shdc` and `glslangValidator` stay developer and CI tools only.
- Integer textures need sample types in NativeKit GPU's binding
  descriptors (ADR 0022); the reflection supplies them.
