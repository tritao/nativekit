# NativeKit Scene contracts

NativeKit scene data uses one fixed world contract across the C ABI, C++ API,
and rendering integrations:

- lengths are meters;
- angles are radians;
- time values are seconds;
- the world is right-handed;
- +Z is up;
- +X is forward where a forward direction is required;
- transforms are column-major 4x4 matrices multiplying column vectors;
- translation is stored in matrix elements 12, 13, and 14.

`nkscene_entity_id` identifies logical or source data. It is the identity that
can be shared by multiple scene instances and external simulation objects.
`nkscene_occurrence_id` identifies one instantiated occurrence in the scene
hierarchy. A source entity may therefore have many occurrences, each with its
own parent, transform, visibility, geometry, and material assignment.

Runtime handles such as `nkscene_scene`, `nkscene_transaction`, and
`nkscene_snapshot` are generation-checked temporary tokens. They are not
persistent scene identities and must not be used in simulation or asset data.

Snapshots preserve the entity/occurrence distinction. A renderer or
selection system can map several visual occurrences back to one source entity
without collapsing their independent transforms or presentation state.

Transactions remain the scene mutation boundary. Simulation systems should
place all updates for one step in one transaction and use
`nkscene_tx_set_transforms()` for bulk transform updates.

Geometry uses vertex streams. A geometry must provide a position stream, while
normal, tangent, UV, and color streams are optional and independently packed.
Scene images, textures, and samplers are asset descriptions owned by the scene;
their GPU realizations belong to `scene_render`.

Materials use a compact metallic/roughness model. Cameras and lights are
generic scene resources attached to occurrences through their own references;
sensor-specific concepts remain above the scene layer.
