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

/* Runtime handles are generation-checked tokens. They are intentionally
 * separate from stable scene identifiers, which identify scene data. */
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

typedef struct nkscene_geometry_id {
    uint64_t value;
} nkscene_geometry_id;

typedef struct nkscene_material_id {
    uint64_t value;
} nkscene_material_id;

typedef struct nkscene_transform {
    float matrix[16];
} nkscene_transform;

typedef struct nkscene_bounds {
    float minimum[3];
    float maximum[3];
    uint32_t valid NK_BOOL32;
} nkscene_bounds;

typedef struct nkscene_geometry_vertex {
    float position[3];
} nkscene_geometry_vertex;

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
} nkscene_geometry_data;

enum { NKS_MATERIAL_OPAQUE = 1u << 0, NKS_MATERIAL_DOUBLE_SIDED = 1u << 1 };

typedef struct nkscene_material_data {
    uint32_t struct_size NK_STRUCT_SIZE;
    float base_color[4];
    float opacity;
    uint32_t flags;
} nkscene_material_data;

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
NKS_API nkscene_result NKS_CALL nkscene_tx_set_geometry(nkscene_transaction transaction,
                                                        nkscene_occurrence_id occurrence,
                                                        nkscene_geometry_id geometry);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_material(nkscene_transaction transaction,
                                                        nkscene_occurrence_id occurrence,
                                                        nkscene_material_id material);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_visibility(nkscene_transaction transaction,
                                                          nkscene_occurrence_id occurrence,
                                                          uint32_t visible);

/* ------------------------------------------------------------------------- */
/* Snapshots and changes                                                     */
/* ------------------------------------------------------------------------- */

NKS_API nkscene_result NKS_CALL
nkscene_scene_snapshot(nkscene_scene scene, nkscene_snapshot *out_snapshot NK_OUT NK_OWNED);
NKS_API void NKS_CALL nkscene_snapshot_destroy(nkscene_snapshot snapshot);
NKS_API nkscene_result NKS_CALL nkscene_snapshot_get_revision(nkscene_snapshot snapshot,
                                                              uint64_t *out_revision NK_OUT);
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

#ifdef __cplusplus
}
#endif

#endif /* NATIVEKIT_SCENE_H */
