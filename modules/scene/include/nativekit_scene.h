#ifndef NATIVEKIT_SCENE_H
#define NATIVEKIT_SCENE_H

#include <stdint.h>

#include "nativekit.h"

#if defined(_WIN32)
#if defined(NKSCENE_STATIC)
#define NKS_API
#elif defined(NKSCENE_BUILDING_LIBRARY)
#define NKS_API __declspec(dllexport)
#else
#define NKS_API __declspec(dllimport)
#endif
#define NKS_CALL __cdecl
#else
#define NKS_API __attribute__((visibility("default")))
#define NKS_CALL
#endif

/* ------------------------------------------------------------------------- */
/* Binding annotations                                                       */
/* ------------------------------------------------------------------------- */

/* ------------------------------------------------------------------------- */
/* C linkage                                                                 */
/* ------------------------------------------------------------------------- */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Runtime handles and stable identifiers                                    */
/* ------------------------------------------------------------------------- */

/* Runtime handles are generation-checked temporary tokens. They are
 * intentionally separate from stable scene identifiers, which identify scene
 * data and may be persisted by an application. */
typedef uint32_t nkscene_scene NK_HANDLE NK_HANDLE_DESTROY(nkscene_scene_destroy);

typedef uint32_t nkscene_transaction NK_HANDLE NK_HANDLE_DESTROY(nkscene_transaction_cancel);

typedef uint32_t nkscene_snapshot NK_HANDLE NK_HANDLE_DESTROY(nkscene_snapshot_destroy);

typedef uint32_t nkscene_change_set NK_HANDLE NK_HANDLE_DESTROY(nkscene_change_set_destroy);

typedef struct nkscene_occurrence_id {
    uint64_t value;
} nkscene_occurrence_id;

typedef struct nkscene_entity_id {
    uint64_t value;
} nkscene_entity_id;

/*
 * Scene contract:
 *   lengths are meters, angles are radians, and time is seconds;
 *   the world is right-handed with +Z up;
 *   +X is the forward direction where a forward convention is needed;
 *   transforms are column-major 4x4 matrices multiplying column vectors;
 *   translation is stored at matrix[12], matrix[13], and matrix[14].
 *
 * An entity identifies logical/source data. An occurrence identifies one
 * instantiated scene occurrence of that entity. Runtime handles above are
 * neither entity nor occurrence identities.
 */

typedef struct nkscene_geometry_id {
    uint64_t value;
} nkscene_geometry_id;

typedef struct nkscene_material_id {
    uint64_t value;
} nkscene_material_id;

typedef struct nkscene_image_id {
    uint64_t value;
} nkscene_image_id;

typedef struct nkscene_texture_id {
    uint64_t value;
} nkscene_texture_id;

typedef struct nkscene_sampler_id {
    uint64_t value;
} nkscene_sampler_id;

typedef struct nkscene_camera_id {
    uint64_t value;
} nkscene_camera_id;

typedef struct nkscene_light_id {
    uint64_t value;
} nkscene_light_id;

typedef struct nkscene_transform {
    float matrix[16];
} nkscene_transform;

typedef struct nkscene_transform_update {
    nkscene_occurrence_id occurrence;
    nkscene_transform transform;
} nkscene_transform_update;

typedef struct nkscene_bounds {
    float minimum[3];
    float maximum[3];
    uint32_t valid NK_BOOL32;
} nkscene_bounds;

typedef struct nkscene_geometry_vertex {
    float position[3];
} nkscene_geometry_vertex;

typedef enum nkscene_vertex_semantic {
    NKS_VERTEX_SEMANTIC_POSITION = 1,
    NKS_VERTEX_SEMANTIC_NORMAL = 2,
    NKS_VERTEX_SEMANTIC_TANGENT = 3,
    NKS_VERTEX_SEMANTIC_TEXCOORD0 = 4,
    NKS_VERTEX_SEMANTIC_TEXCOORD1 = 5,
    NKS_VERTEX_SEMANTIC_COLOR0 = 6
} nkscene_vertex_semantic;

typedef enum nkscene_vertex_format {
    NKS_VERTEX_FORMAT_FLOAT32X2 = 1,
    NKS_VERTEX_FORMAT_FLOAT32X3 = 2,
    NKS_VERTEX_FORMAT_FLOAT32X4 = 3,
    NKS_VERTEX_FORMAT_UNORM8X4 = 4,
    NKS_VERTEX_FORMAT_SNORM8X4 = 5
} nkscene_vertex_format;

typedef enum nkscene_primitive_type {
    NKS_PRIMITIVE_TRIANGLES = 1,
    NKS_PRIMITIVE_LINES = 2,
    NKS_PRIMITIVE_POINTS = 3
} nkscene_primitive_type;

typedef struct nkscene_vertex_stream {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkscene_vertex_semantic semantic;
    nkscene_vertex_format format;
    uint32_t stride;
    const void *data NK_BORROWED_ARRAY(count);
    uint32_t count;
} nkscene_vertex_stream;

typedef struct nkscene_subelement_range {
    uint32_t first_primitive;
    uint32_t primitive_count;
    uint32_t subelement;
} nkscene_subelement_range;

typedef struct nkscene_geometry_data {
    uint32_t struct_size NK_STRUCT_SIZE;
    const nkscene_geometry_vertex *vertices NK_BORROWED_ARRAY(vertex_count);
    uint32_t vertex_count;
    /** Packed uint32 triangle indices when index_count is non-zero. */
    const void *indices NK_BORROWED_ARRAY(index_count);
    uint32_t index_count;
    nkscene_bounds bounds;
    const nkscene_subelement_range *subelements NK_BORROWED_ARRAY(subelement_count);
    uint32_t subelement_count;
    nkscene_primitive_type primitive_type;
    const nkscene_vertex_stream *streams NK_BORROWED_ARRAY(stream_count);
    uint32_t stream_count;
} nkscene_geometry_data;

enum { NKS_MATERIAL_OPAQUE = 1u << 0, NKS_MATERIAL_DOUBLE_SIDED = 1u << 1 };

enum { NKS_MATERIAL_ALPHA_OPAQUE = 1, NKS_MATERIAL_ALPHA_MASK = 2, NKS_MATERIAL_ALPHA_BLEND = 3 };

typedef enum nkscene_image_format {
    NKS_IMAGE_FORMAT_R8 = 1,
    NKS_IMAGE_FORMAT_RGBA8 = 2,
    NKS_IMAGE_FORMAT_RGBA16F = 3,
    NKS_IMAGE_FORMAT_R32F = 4
} nkscene_image_format;

typedef enum nkscene_sampler_filter {
    NKS_SAMPLER_FILTER_NEAREST = 1,
    NKS_SAMPLER_FILTER_LINEAR = 2
} nkscene_sampler_filter;

typedef enum nkscene_sampler_wrap {
    NKS_SAMPLER_WRAP_REPEAT = 1,
    NKS_SAMPLER_WRAP_CLAMP_TO_EDGE = 2,
    NKS_SAMPLER_WRAP_MIRRORED_REPEAT = 3
} nkscene_sampler_wrap;

typedef struct nkscene_image_data {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint32_t width;
    uint32_t height;
    nkscene_image_format format;
    uint32_t mip_count;
    const void *data NK_BORROWED_BUFFER(data_size);
    uint32_t data_size;
} nkscene_image_data;

typedef struct nkscene_texture_data {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkscene_image_id image;
} nkscene_texture_data;

typedef struct nkscene_sampler_data {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkscene_sampler_filter min_filter;
    nkscene_sampler_filter mag_filter;
    nkscene_sampler_wrap wrap_u;
    nkscene_sampler_wrap wrap_v;
    nkscene_sampler_wrap wrap_w;
    float max_anisotropy;
} nkscene_sampler_data;

typedef struct nkscene_material_data {
    uint32_t struct_size NK_STRUCT_SIZE;
    float base_color[4];
    float opacity;
    uint32_t flags;
    float metallic;
    float roughness;
    float emissive[3];
    float alpha_cutoff;
    uint32_t alpha_mode;
    nkscene_texture_id base_color_texture;
    nkscene_texture_id metallic_roughness_texture;
    nkscene_texture_id normal_texture;
    nkscene_texture_id emissive_texture;
    nkscene_texture_id occlusion_texture;
    nkscene_sampler_id sampler;
} nkscene_material_data;

typedef enum nkscene_camera_projection {
    NKS_CAMERA_PERSPECTIVE = 1,
    NKS_CAMERA_ORTHOGRAPHIC = 2
} nkscene_camera_projection;

typedef struct nkscene_camera_data {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkscene_camera_projection projection;
    float fov_y;
    float orthographic_height;
    float near_plane;
    float far_plane;
    float aspect_ratio;
} nkscene_camera_data;

typedef enum nkscene_light_type {
    NKS_LIGHT_DIRECTIONAL = 1,
    NKS_LIGHT_POINT = 2,
    NKS_LIGHT_SPOT = 3
} nkscene_light_type;

typedef struct nkscene_light_data {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkscene_light_type type;
    float color[3];
    float intensity;
    float range;
    float inner_cone_angle;
    float outer_cone_angle;
} nkscene_light_data;

/** Read-only occurrence state captured by a scene snapshot. */
typedef struct nkscene_snapshot_occurrence {
    uint32_t struct_size NK_STRUCT_SIZE;
    nkscene_occurrence_id occurrence;
    nkscene_entity_id source;
    nkscene_occurrence_id parent;
    nkscene_transform local_transform;
    nkscene_transform world_transform;
    uint64_t world_transform_revision;
    nkscene_geometry_id geometry;
    nkscene_material_id material;
    uint32_t visible NK_BOOL32;
    nkscene_bounds bounds;
    nkscene_camera_id camera;
    nkscene_light_id light;
} nkscene_snapshot_occurrence;

enum { NKS_SCENE_SNAPSHOT_OCCURRENCE_PAGE_CAPACITY = 64u };

/** Fixed-size page used to transfer snapshot occurrences across the C ABI. */
typedef struct nkscene_snapshot_occurrence_page {
    uint32_t struct_size NK_STRUCT_SIZE;
    uint64_t start_index;
    uint32_t count;
    nkscene_snapshot_occurrence occurrences[NKS_SCENE_SNAPSHOT_OCCURRENCE_PAGE_CAPACITY];
} nkscene_snapshot_occurrence_page;

/* ------------------------------------------------------------------------- */
/* Result codes                                                              */
/* ------------------------------------------------------------------------- */

typedef int32_t nkscene_result;

enum {
    NKS_OK = 0,
    NKS_ERROR_INVALID_ARGUMENT = -1,
    NKS_ERROR_INVALID_HANDLE = -2,
    NKS_ERROR_STALE_ID = -3,
    NKS_ERROR_INVALID_STATE = -4,
    NKS_ERROR_HIERARCHY_CYCLE = -5,
    NKS_ERROR_OUT_OF_MEMORY = -6
};

#define NKS_INVALID_SCENE ((nkscene_scene){0})
#define NKS_INVALID_TRANSACTION ((nkscene_transaction){0})
#define NKS_INVALID_OCCURRENCE ((nkscene_occurrence_id){0})
#define NKS_INVALID_ENTITY ((nkscene_entity_id){0})
#define NKS_INVALID_GEOMETRY ((nkscene_geometry_id){0})
#define NKS_INVALID_MATERIAL ((nkscene_material_id){0})
#define NKS_INVALID_IMAGE ((nkscene_image_id){0})
#define NKS_INVALID_TEXTURE ((nkscene_texture_id){0})
#define NKS_INVALID_SAMPLER ((nkscene_sampler_id){0})
#define NKS_INVALID_CAMERA ((nkscene_camera_id){0})
#define NKS_INVALID_LIGHT ((nkscene_light_id){0})

/* ------------------------------------------------------------------------- */
/* Scene lifecycle                                                           */
/* ------------------------------------------------------------------------- */

NKS_API nkscene_result NKS_CALL nkscene_scene_create(nkscene_scene *out_scene NK_OUT NK_OWNED);
NKS_API void NKS_CALL nkscene_scene_destroy(nkscene_scene scene);

/* ------------------------------------------------------------------------- */
/* Transactions                                                              */
/* ------------------------------------------------------------------------- */

NKS_API nkscene_result NKS_CALL nkscene_transaction_begin(
    nkscene_scene scene, nkscene_transaction *out_transaction NK_OUT NK_OWNED);
NKS_API void NKS_CALL nkscene_transaction_cancel(nkscene_transaction transaction);
NKS_API nkscene_result NKS_CALL nkscene_transaction_commit(nkscene_transaction transaction);
NKS_API nkscene_result NKS_CALL nkscene_transaction_commit_with_changes(
    nkscene_transaction transaction, nkscene_change_set *out_changes NK_OUT NK_OWNED);

NKS_API nkscene_result NKS_CALL nkscene_tx_create_occurrence(nkscene_transaction transaction,
                                                             nkscene_occurrence_id *out_occurrence);
NKS_API nkscene_result NKS_CALL nkscene_tx_destroy_occurrence(nkscene_transaction transaction,
                                                              nkscene_occurrence_id occurrence);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_parent(nkscene_transaction transaction,
                                                      nkscene_occurrence_id occurrence,
                                                      nkscene_occurrence_id parent);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_transform(nkscene_transaction transaction,
                                                         nkscene_occurrence_id occurrence,
                                                         const nkscene_transform *transform);
// clang-format off
NKS_API nkscene_result NKS_CALL nkscene_tx_set_transforms(
    nkscene_transaction transaction,
    const nkscene_transform_update *updates NK_IN_ARRAY(update_count),
    uint32_t update_count);
// clang-format on
NKS_API nkscene_result NKS_CALL nkscene_tx_set_geometry(nkscene_transaction transaction,
                                                        nkscene_occurrence_id occurrence,
                                                        nkscene_geometry_id geometry);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_material(nkscene_transaction transaction,
                                                        nkscene_occurrence_id occurrence,
                                                        nkscene_material_id material);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_camera(nkscene_transaction transaction,
                                                      nkscene_occurrence_id occurrence,
                                                      nkscene_camera_id camera);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_light(nkscene_transaction transaction,
                                                     nkscene_occurrence_id occurrence,
                                                     nkscene_light_id light);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_visibility(nkscene_transaction transaction,
                                                          nkscene_occurrence_id occurrence,
                                                          uint32_t visible);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_source_entity(nkscene_transaction transaction,
                                                             nkscene_occurrence_id occurrence,
                                                             nkscene_entity_id source);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_name(nkscene_transaction transaction,
                                                    nkscene_occurrence_id occurrence,
                                                    const char *name NK_UTF8);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_entity_name(nkscene_transaction transaction,
                                                           nkscene_entity_id entity,
                                                           const char *name NK_UTF8);

/* ------------------------------------------------------------------------- */
/* Snapshots and changes                                                     */
/* ------------------------------------------------------------------------- */

NKS_API nkscene_result NKS_CALL
nkscene_scene_snapshot(nkscene_scene scene, nkscene_snapshot *out_snapshot NK_OUT NK_OWNED);
NKS_API void NKS_CALL nkscene_snapshot_destroy(nkscene_snapshot snapshot);
NKS_API nkscene_result NKS_CALL nkscene_snapshot_get_revision(nkscene_snapshot snapshot,
                                                              uint64_t *out_revision NK_OUT);
NKS_API nkscene_result NKS_CALL nkscene_snapshot_get_occurrence_count(nkscene_snapshot snapshot,
                                                                      uint64_t *out_count NK_OUT);
NKS_API nkscene_result NKS_CALL
nkscene_snapshot_get_occurrence(nkscene_snapshot snapshot, uint64_t index,
                                nkscene_snapshot_occurrence *out_occurrence NK_INOUT);
NKS_API nkscene_result NKS_CALL
nkscene_snapshot_get_occurrence_page(nkscene_snapshot snapshot, uint64_t start_index,
                                     nkscene_snapshot_occurrence_page *out_page NK_INOUT);
NKS_API nkscene_result NKS_CALL nkscene_snapshot_get_source_occurrence_count(
    nkscene_snapshot snapshot, nkscene_entity_id source, uint64_t *out_count NK_OUT);
NKS_API nkscene_result NKS_CALL nkscene_snapshot_get_source_occurrence(
    nkscene_snapshot snapshot, nkscene_entity_id source, uint64_t index,
    nkscene_occurrence_id *out_occurrence NK_OUT);
/** Returns a name borrowed from the snapshot and valid until it is destroyed. */
NKS_API nkscene_result NKS_CALL nkscene_snapshot_get_name(nkscene_snapshot snapshot,
                                                          nkscene_occurrence_id occurrence,
                                                          const char **out_name);
/** Returns a source-entity name borrowed from the snapshot and valid until it is destroyed. */
NKS_API nkscene_result NKS_CALL nkscene_snapshot_get_entity_name(nkscene_snapshot snapshot,
                                                                 nkscene_entity_id entity,
                                                                 const char **out_name);
NKS_API void NKS_CALL nkscene_change_set_destroy(nkscene_change_set changes);
NKS_API nkscene_result NKS_CALL nkscene_change_set_get_revision(nkscene_change_set changes,
                                                                uint64_t *out_revision NK_OUT);

/* ------------------------------------------------------------------------- */
/* Resource identifiers                                                      */
/* ------------------------------------------------------------------------- */

NKS_API nkscene_result NKS_CALL nkscene_geometry_create(nkscene_scene scene,
                                                        nkscene_geometry_id *out_geometry NK_OUT);
NKS_API void NKS_CALL nkscene_geometry_destroy(nkscene_scene scene, nkscene_geometry_id geometry);
NKS_API nkscene_result NKS_CALL nkscene_material_create(nkscene_scene scene,
                                                        nkscene_material_id *out_material NK_OUT);
NKS_API void NKS_CALL nkscene_material_destroy(nkscene_scene scene, nkscene_material_id material);
NKS_API nkscene_result NKS_CALL nkscene_geometry_set_data(nkscene_scene scene,
                                                          nkscene_geometry_id geometry,
                                                          const nkscene_geometry_data *data);
NKS_API nkscene_result NKS_CALL nkscene_material_set_data(nkscene_scene scene,
                                                          nkscene_material_id material,
                                                          const nkscene_material_data *data);
NKS_API nkscene_result NKS_CALL nkscene_image_create(nkscene_scene scene,
                                                     nkscene_image_id *out_image NK_OUT);
NKS_API void NKS_CALL nkscene_image_destroy(nkscene_scene scene, nkscene_image_id image);
NKS_API nkscene_result NKS_CALL nkscene_image_set_data(nkscene_scene scene, nkscene_image_id image,
                                                       const nkscene_image_data *data);
NKS_API nkscene_result NKS_CALL nkscene_texture_create(nkscene_scene scene,
                                                       nkscene_texture_id *out_texture NK_OUT);
NKS_API void NKS_CALL nkscene_texture_destroy(nkscene_scene scene, nkscene_texture_id texture);
NKS_API nkscene_result NKS_CALL nkscene_texture_set_data(nkscene_scene scene,
                                                         nkscene_texture_id texture,
                                                         const nkscene_texture_data *data);
NKS_API nkscene_result NKS_CALL nkscene_sampler_create(nkscene_scene scene,
                                                       nkscene_sampler_id *out_sampler NK_OUT);
NKS_API void NKS_CALL nkscene_sampler_destroy(nkscene_scene scene, nkscene_sampler_id sampler);
NKS_API nkscene_result NKS_CALL nkscene_sampler_set_data(nkscene_scene scene,
                                                         nkscene_sampler_id sampler,
                                                         const nkscene_sampler_data *data);
NKS_API nkscene_result NKS_CALL nkscene_camera_create(nkscene_scene scene,
                                                      nkscene_camera_id *out_camera NK_OUT);
NKS_API void NKS_CALL nkscene_camera_destroy(nkscene_scene scene, nkscene_camera_id camera);
NKS_API nkscene_result NKS_CALL nkscene_camera_set_data(nkscene_scene scene,
                                                        nkscene_camera_id camera,
                                                        const nkscene_camera_data *data);
NKS_API nkscene_result NKS_CALL nkscene_light_create(nkscene_scene scene,
                                                     nkscene_light_id *out_light NK_OUT);
NKS_API void NKS_CALL nkscene_light_destroy(nkscene_scene scene, nkscene_light_id light);
NKS_API nkscene_result NKS_CALL nkscene_light_set_data(nkscene_scene scene, nkscene_light_id light,
                                                       const nkscene_light_data *data);

#ifdef __cplusplus
}
#endif

#endif /* NATIVEKIT_SCENE_H */
