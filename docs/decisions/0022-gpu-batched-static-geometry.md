# ADR 0022: GPU features for batched static geometry

## Status

Proposed.

## Context

Renderers built on NativeKit GPU will draw large static worlds the way
efficient chunk renderers do: each chunk baked off-thread into one vertex
buffer and one index buffer, with many draw ranges, and per-instance data in
one table indexed from the shader. Each frame then picks a precomputed command
list per visible chunk, so CPU cost grows with chunks, not objects.

Sokol already provides most of this; NativeKit GPU does not yet expose all of
it, and neither has multi-draw:

| Need | Sokol | NativeKit GPU |
| --- | --- | --- |
| Instancing, per-instance vertex buffers, integer vertex formats, 32-bit indices | yes | yes |
| Base vertex and base instance per draw | `sg_draw_ex`, with feature flags | no; `nkgpu_draw` has neither |
| Integer image formats (`R16UI`, `RG16UI`, `RGBA16UI`, `R16SI`, `R32SI`, ...) | yes | only `R32_UINT` |
| Image sample types (float, uint, sint) and non-filtering samplers | yes | no field in the binding descriptor |
| Updating one layer or mip of an array image | yes | `nkgpu_image_update` has no layer or mip |
| Many draws in one call | no | no |
| `gl_DrawID` | no | no |

## Decision

### Expose what Sokol already has

- `nkgpu_draw_ex()` with base vertex and base instance, a matching command in
  the command stream, and `draw_base_vertex` and `draw_base_instance` in
  `nkgpu_features`.
- The integer image formats, with their capability reported through
  `nkgpu_image_format_support`.
- A sample type in `nkgpu_shader_binding_desc` for sampled images, and
  non-filtering samplers. ADR 0021's reflection fills them in.
- Layer and mip parameters for image region updates, on the region-update
  support already in NativeKit's Sokol fork.

New commands are additive records in the command stream (ADR 0017).

### Extend NativeKit's Sokol fork with multi-draw

`sg_draw_multi(const sg_draw_range* ranges, int count)` issues many indexed,
instanced draws from one call. Each range has a base element, element count,
instance count, base vertex and base instance. Validation runs once per call.

| Backend | Implementation |
| --- | --- |
| GL 4.3+ | `glMultiDrawElementsIndirect` over a small indirect buffer |
| GL 4.2 | a loop of `glDrawElementsInstancedBaseVertexBaseInstance` |
| WebGL2 | `WEBGL_multi_draw_instanced_base_vertex_base_instance` when present, else a loop |
| GLES 3 | a loop; base vertex from 3.2, no base instance |
| D3D11 | a loop of `DrawIndexedInstanced` with a start instance |
| Metal | a loop of `drawIndexedPrimitives` with base vertex and base instance |

Feature flags report `multi_draw` (one driver call) separately from base
vertex and base instance. Where only a loop exists, the call still saves
crossing from the host language for every range. NativeKit GPU exposes it as
`nkgpu_draw_multi()` and a command-stream record.

### Instance data without `gl_DrawID`

`gl_DrawID` does not translate to GLSL ES 3.00 through `sokol-shdc`, and D3D11's
`SV_InstanceID` ignores the start instance. The supported pattern is an
instance-counter stream: a shared per-instance vertex buffer holding
0, 1, 2, ..., bound with a step of one instance. With base instance, its value
is the global index into the instance table on every backend, and each draw's
instances are contiguous in that table. Where base instance is unavailable
(GLES 3, WebGL2 without the extension), renderers set the draw's instance start
as a uniform per draw. Instance tables live in an integer texture read with
`texelFetch`, which works on GLES 3 and WebGL2; storage buffers are an option
where `nkgpu_features.storage_buffer` is set.

### Later

`sg_draw_multi_indirect` from a GPU buffer, for compute-driven culling on GL
4.3, Metal and D3D11. Not part of the first step.

## Tests

- The exposed features in the existing GLCore and GLES3 backend matrix, then
  D3D11 and Metal: each draw variant renders pixel-identical output to the
  equivalent sequence of single draws.
- `sg_draw_multi` with and without base instance, with empty ranges and with
  a count of zero.
- A benchmark: 10,000 ranges per frame through `nkgpu_draw_multi()` against
  10,000 `nkgpu_draw_ex()` calls from Haxeon, recording CPU time per frame on
  each backend.

## Consequences

- NativeKit's Sokol fork carries one more extension; it is kept as a separate
  commit so it can be offered upstream.
- Renderers that use these features check the feature flags and keep the
  per-draw-uniform fallback for GLES 3 and plain WebGL2.
