# NanoVG path preparation

NativeKit uses NanoVG as a path construction and tessellation library. NanoVG
does not own NativeKit text, render targets, compositor ordering, Sokol
resources, or final draw submission.

## Reusable paths

`NVGpathBuilder` stores path commands independently of `NVGcontext` and frame
state. Build one whenever a path is created, retain it when geometry will be
reused, and call `nvgResetPathBuilder()` when its storage can be reused. The
builder supports lines, cubic and quadratic Béziers, corner arcs, closed
sub-paths, winding, append, and transformed append.

`nvgPathBuilderBounds()` returns conservative bounds that include Bézier
control points. They are suitable for invalidation and broad-phase tests; they
are not an exact extrema calculation.

## Preparing geometry

Call `nvgInitPrepareParams()` before changing individual preparation fields.
The preparation parameters make geometry-affecting state explicit:

- `devicePixelRatio`, tessellation and distance tolerances control flattening;
- `fringeWidth` and `edgeAntiAlias` control antialias geometry;
- `transform` is applied before flattening;
- `strokeWidth`, `lineCap`, `lineJoin`, and `miterLimit` control strokes; and
- `fillRule` selects `NVG_FILL_NON_ZERO` or `NVG_FILL_EVEN_ODD`.

Stroke width is expressed in the transformed coordinate space. A negative
fringe width selects NanoVG's device-ratio-derived default; zero disables the
fringe. `nvgPrepareFill()` and `nvgPrepareStroke()` do not require a frame,
renderer callback, GPU resource, or `NVGcontext`.

Preparation uses a two-call sizing protocol:

```c
NVGprepareParams params;
nvgInitPrepareParams(&params);
NVGpathBuilder* path = nvgCreatePathBuilder();

NVGprepareOutput output = {0};
int result = nvgPrepareFill(path, &params, &output);
if (result != NVG_PREPARE_OUTPUT_TOO_SMALL)
    return 0;

NVGpreparedPath* paths = malloc(sizeof(*paths) * (size_t)output.pathCount);
NVGvertex* vertices = malloc(sizeof(*vertices) * (size_t)output.vertexCount);
output.paths = paths;
output.pathCapacity = output.pathCount;
output.vertices = vertices;
output.vertexCapacity = output.vertexCount;
result = nvgPrepareFill(path, &params, &output);
/* Use paths[i].fillOffset/fillCount and strokeOffset/strokeCount into vertices. */
free(vertices);
free(paths);
nvgDeletePathBuilder(path);
```

The first call also populates counts, bounds, fringe width, stroke width, and
fill rule. `NVG_PREPARE_OUTPUT_TOO_SMALL` is the expected query result;
`NVG_PREPARE_INVALID` indicates invalid input or parameters. The caller owns
the path and vertex buffers and may place them in an arena. NanoVG releases its
temporary preparation storage before returning.

For that temporary storage, set `NVGprepareParams.allocator` to an
`NVGprepareAllocator` containing all three callbacks: `alloc`, `realloc`, and
`free`. The callbacks receive `userPtr`, and every allocation made through the
preparation call is released before it returns. A partial callback set is
invalid. An allocator used by concurrent preparation calls must provide its
own synchronization or independent state.

The preparation API has no mutable global state. Distinct builders may be
constructed and prepared concurrently. A builder must not be modified while
another thread is appending or resetting it; an immutable builder may be
prepared concurrently when the supplied allocator is thread-safe.

## NativeKit integration

NativeKit translates NanoVG output immediately into its private
`PreparedGeometry`, `PreparedPaint`, `PreparedVertex`, and `PreparedBlend`
representations. The geometry cache is keyed by path, transform, and device
scale, not by paint, so recoloring a retained path does not retessellate it.
Paint is attached when a `PreparedPath` operation is created and is consumed by
the NativeKit compositor and Sokol backend.

The stateful NanoVG API remains available to the compatibility recorder and
uses the same NanoVG flattening and expansion implementation. NativeKit's
retained path is direct preparation:

```text
NativeKit Path
    -> NVGpathBuilder
    -> nvgPrepareFill / nvgPrepareStroke
    -> NativeKit PreparedGeometry
    -> compositor / Sokol backend
```

Skribidi remains NativeKit's text authority. Ordinary `DrawImage` operations
also bypass NanoVG; NanoVG image handles are relevant only when an image is a
paint on prepared path geometry.
