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

#if defined(__clang__)
#define NKS_OUT __attribute__((annotate("hxi:out")))
#define NKS_HANDLE_ANNOTATION __attribute__((annotate("hxi:handle")))
#define NKS_HANDLE_DESTROY(symbol) __attribute__((annotate("hxi:handle_destroy")))
#else
#define NKS_OUT
#define NKS_HANDLE_ANNOTATION
#define NKS_HANDLE_DESTROY(symbol)
#endif

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
typedef struct nkscene_scene {
    uint64_t value;
} nkscene_scene NKS_HANDLE_ANNOTATION NKS_HANDLE_DESTROY(nkscene_scene_destroy);

typedef struct nkscene_transaction {
    uint64_t value;
} nkscene_transaction NKS_HANDLE_ANNOTATION
    NKS_HANDLE_DESTROY(nkscene_transaction_cancel);

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

NKS_API nkscene_result NKS_CALL nkscene_scene_create(
    nkscene_scene *out_scene NKS_OUT);
NKS_API void NKS_CALL nkscene_scene_destroy(nkscene_scene scene);

/* ------------------------------------------------------------------------- */
/* Transactions                                                              */
/* ------------------------------------------------------------------------- */

NKS_API nkscene_result NKS_CALL nkscene_transaction_begin(
    nkscene_scene scene, nkscene_transaction *out_transaction NKS_OUT);
NKS_API void NKS_CALL nkscene_transaction_cancel(nkscene_transaction transaction);
NKS_API nkscene_result NKS_CALL nkscene_transaction_commit(
    nkscene_transaction transaction);

NKS_API nkscene_result NKS_CALL nkscene_tx_create_occurrence(
    nkscene_transaction transaction, nkscene_occurrence_id *out_occurrence);
NKS_API nkscene_result NKS_CALL nkscene_tx_destroy_occurrence(
    nkscene_transaction transaction, nkscene_occurrence_id occurrence);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_parent(
    nkscene_transaction transaction, nkscene_occurrence_id occurrence,
    nkscene_occurrence_id parent);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_transform(
    nkscene_transaction transaction, nkscene_occurrence_id occurrence,
    const nkscene_transform *transform);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_geometry(
    nkscene_transaction transaction, nkscene_occurrence_id occurrence,
    nkscene_geometry_id geometry);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_material(
    nkscene_transaction transaction, nkscene_occurrence_id occurrence,
    nkscene_material_id material);
NKS_API nkscene_result NKS_CALL nkscene_tx_set_visibility(
    nkscene_transaction transaction, nkscene_occurrence_id occurrence,
    uint32_t visible);

#ifdef __cplusplus
}
#endif

#endif /* NATIVEKIT_SCENE_H */
