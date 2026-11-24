# GPU shader generation

Build the host tool with `cmake -S modules/gpu/tools -B build-shaders`
and `cmake --build build-shaders`. Native GPU builds also provide the
`nkgpu-shaders` target when `NK_BUILD_SHADER_TOOLS` is enabled.

Run `nkgpu-shaders path/to/shaders.json` to regenerate the outputs in a C
consumer's manifest. Use `--check` to verify existing output byte for byte.
The default target set is desktop GLSL, GLSL ES, HLSL 5, macOS Metal and iOS
Metal. A source row names its file and can restrict discovered `@program`
names or supply named `variants` with define sets.

- `header` exports source strings for C++ consumers.
- `tables` writes a C header of program tables used by
  `nkgpu_shader_create_from_table`.
- `reflection` writes the normalized JSON. Set `embed_sources` to include
  shader strings.
- `include_roots` stages additional include directories under a relative
  prefix. The reflection records the actual input dependencies.

Build systems can call `nkgpu-shaders --source FILE --target TARGET --output DIR`.
Separate several targets with a colon. Pass `--variants JSON` for define
sets. The output directory receives `reflection.json` and a newline-delimited
`dependencies` file containing absolute paths.

The driver verifies the SHA-256 of the host's pinned compiler. It checks for
`sokol-shdc` beside the executable, then in the local cache. Use `--shdc`
or `SOKOL_SHDC` to supply a verified executable. `--print-compiler`
prints the resolved compiler path. `--validator PATH` enables GLSL validation
with the pinned glslangValidator. Set `NATIVEKIT_GLSLANG_SHA256` to the
trusted executable digest when building it from the pinned source.

## Reflection schema

A file with `schema: nkgpu-shader-reflection/1` contains `programs`, keyed by
program and then variant name. Each variant has `defines`, `dependencies`
(paths relative to the manifest) and `targets`, keyed by shader target.

Each target has:

- `attributes` with `slot`, `name`, `format` and `semantic`
  (`name` and `index`). An empty semantic name means no HLSL semantic.
- `uniform_blocks` with `stage`, `slot`, `name`, byte `size` and
  `members`. A member has `name`, `type`, `array_count`, byte `offset`
  and, for arrays, byte `array_stride`. `gl_uniforms` describes the actual
  names, types and counts used to upload GLSL uniforms after lowering.
- `images` with `name`, `stage`, `slot`, `type`, `sample_type` and
  `multisampled`. `samplers` have `name`, `stage`, `slot` and `type`.
  `image_sampler_pairs` connect an `image_slot` and `sampler_slot` with
  the combined binding `name`.
- `storage_buffers` with `name`, `stage`, `slot` and `readonly`.
- `stages`, keyed by stage name, with `source` (the generated symbol)
  and `entry_point`. When present, the top-level `sources` map contains
  shader text keyed by that symbol.
