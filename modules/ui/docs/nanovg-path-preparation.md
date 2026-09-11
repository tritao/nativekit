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

The NativeKit C++ adapter uses the same concepts with value-oriented names:
`NanoVGPath` is the reusable command builder, `PathPreparationParams` is the
complete geometry input, and `PreparedGeometry` is caller-owned output. The
builder is valid after construction unless NanoVG cannot allocate its private
storage; command methods are intentionally `void` to match NanoVG's existing
immediate path API, so callers should check `valid()` before using one.

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

The NativeKit adapter exposes the equivalent calls as:

```cpp
PathPreparationParams params;
params.fill_rule = PathFillRule::EvenOdd;
PreparedGeometry geometry;
if (!prepare_fill(path, params, geometry))
    return false;
```

`prepare_fill()` and `prepare_stroke()` clear the output before preparing. On
success, the output ranges and vertices are ready for immediate attachment to
one `PreparedPath` operation. Reusing the same `PreparedGeometry` preserves
vector capacity; callers that need an arena can reserve or replace those
containers at the NativeKit boundary.

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
`PreparedGeometry`, `PreparedPaint`, and `PreparedVertex` representations.
NanoVG writes directly into the `std::vector` storage owned by the NativeKit
preparation result; there is no temporary `NVGpreparedPath`/`NVGvertex` array
or conversion copy between tessellation and the retained geometry.

NativeKit's retained-path policy is device-space preparation with deferred
placement:

- the linear part of the device transform (scale, skew, and rotation) is
  passed to NanoVG and is part of the geometry-cache key;
- translation is removed before preparation and is applied by the renderer;
- device pixel scale is part of the key and affects flattening tolerance,
  fringe width, and stroke expansion; and
- therefore moving a retained path reuses geometry, while a scale, skew,
  rotation, or pixel-scale change prepares a new device-quality result.

The standalone preparation API leaves this choice to its caller. Callers
wanting transform-independent retained geometry can prepare in local space
and apply a transform during rendering, accepting that very large scale
changes may reduce curve or stroke quality. NativeKit chooses device-space
geometry so that its cache preserves antialias and tessellation quality.

The geometry cache is keyed by path, the tessellation-affecting transform,
device scale, and stroke style when applicable, not by paint, so recoloring a
retained path does not retessellate it. Paint is attached when a
`PreparedPath` operation is created and is consumed by the NativeKit
compositor and Sokol backend. Canvas clipping and compositing remain on the
render-plan command; they are not retained in prepared path data.

The public NativeKit display list uses `NKUI_COMMAND_DRAW_PATH` for fills and
`NKUI_COMMAND_STROKE_PATH` for strokes. A stroke command carries its logical
width, line cap, line join, and miter limit; the compositor applies the current
transform and paint before NativeKit requests `nvgPrepareStroke()`.

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
also bypass NanoVG. Image-pattern paints, where used by the compatibility
boundary, carry a NativeKit-local opaque image token into the backend rather
than retaining a NanoVG image handle in path data.
