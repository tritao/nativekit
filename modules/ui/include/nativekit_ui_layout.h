#ifndef NATIVEKIT_UI_LAYOUT_H
#define NATIVEKIT_UI_LAYOUT_H

#include "nativekit_ui.h"

#include <stdint.h>

/*
 * This is the NativeKit/Haxe frontend bridge. It is deliberately separate
 * from nativekit_ui.h: this batched, versioned bridge carries render/layout
 * data only. Haxe owns widgets, identity, input, accessibility, and state.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Layout bridge and wire-format versions and fixed sizes. */
enum {
    NKUI_LAYOUT_API_VERSION = 20,
    NKUI_LAYOUT_TRANSACTION_VERSION = 16,
    NKUI_LAYOUT_TRANSACTION_HEADER_BYTES = 16,
    NKUI_LAYOUT_NODE_RECORD_BYTES = 260,
    NKUI_LAYOUT_MAX_TRANSACTION_BYTES = 16 * 1024 * 1024,
    NKUI_LAYOUT_RESOLVED_ITEM_BYTES = 96
};

/** Bit flags stored in each node's clip-flags field. */
typedef uint32_t nkui_layout_clip_flags;
enum NK_FLAGS(nkui_layout_clip_flags) {
    NKUI_LAYOUT_CLIP_HORIZONTAL = 1u << 0,
    NKUI_LAYOUT_CLIP_VERTICAL = 1u << 1
};

/** Node flags stored at NKUI_LAYOUT_NODE_FLAGS_OFFSET. */
typedef uint32_t nkui_layout_node_flags;
enum NK_FLAGS(nkui_layout_node_flags) {
    NKUI_LAYOUT_NODE_VISIBLE = 1u << 0,
    NKUI_LAYOUT_NODE_FLOATING = 1u << 1,
    NKUI_LAYOUT_NODE_CLIP_TO_PARENT = 1u << 2,
    /** Excludes this node's own bounds from geometric hit testing. */
    NKUI_LAYOUT_NODE_HIT_SELF_DISABLED = 1u << 3,
    /** Excludes this node's descendants from geometric hit testing. */
    NKUI_LAYOUT_NODE_HIT_CHILDREN_DISABLED = 1u << 4
};

/** Persistent rendering policy for a retained custom-paint plane. */
typedef uint32_t nkui_layout_cache_policy;
enum NK_ENUM(nkui_layout_cache_policy) {
    NKUI_LAYOUT_CACHE_NONE = 0,
    NKUI_LAYOUT_CACHE_AUTO = 1,
    NKUI_LAYOUT_CACHE_RASTER = 2
};

/** Flags returned with each resolved item. */
typedef uint32_t nkui_layout_resolved_flags;
enum NK_FLAGS(nkui_layout_resolved_flags) {
    NKUI_LAYOUT_RESOLVED_VISIBLE = 1u << 0,
    NKUI_LAYOUT_RESOLVED_HAS_BASELINE = 1u << 1
};

/** Visual content kinds encoded in the transaction. */
typedef uint32_t nkui_layout_visual_kind;
enum NK_ENUM(nkui_layout_visual_kind) {
    NKUI_LAYOUT_VISUAL_BOX = 1,
    NKUI_LAYOUT_VISUAL_TEXT = 2,
    NKUI_LAYOUT_VISUAL_IMAGE = 3,
    NKUI_LAYOUT_VISUAL_CUSTOM = 4
};

/** Sizing modes encoded for each node axis. */
typedef uint32_t nkui_layout_sizing;
enum NK_ENUM(nkui_layout_sizing) {
    NKUI_LAYOUT_SIZING_FIT = 0,
    NKUI_LAYOUT_SIZING_GROW = 1,
    NKUI_LAYOUT_SIZING_FIXED = 2,
    NKUI_LAYOUT_SIZING_PERCENT = 3
};

/** Child flow directions encoded in node styles. */
typedef uint32_t nkui_layout_direction;
enum NK_ENUM(nkui_layout_direction) {
    NKUI_LAYOUT_DIRECTION_LEFT_TO_RIGHT = 0,
    NKUI_LAYOUT_DIRECTION_TOP_TO_BOTTOM = 1
};

/** Cross-axis child alignment values shared by the horizontal and vertical axes. */
typedef uint32_t nkui_layout_alignment;
enum NK_ENUM(nkui_layout_alignment) {
    NKUI_LAYOUT_ALIGNMENT_START = 0,
    NKUI_LAYOUT_ALIGNMENT_END = 1,
    NKUI_LAYOUT_ALIGNMENT_CENTER = 2,
    /** Aligns child baselines in horizontal rows; baseline-less children use their bottom edge. */
    NKUI_LAYOUT_ALIGNMENT_BASELINE = 3
};

/** Main-axis free-space distribution policies. */
typedef uint32_t nkui_layout_distribution;
enum NK_ENUM(nkui_layout_distribution) {
    NKUI_LAYOUT_DISTRIBUTION_START = 0,
    NKUI_LAYOUT_DISTRIBUTION_CENTER = 1,
    NKUI_LAYOUT_DISTRIBUTION_END = 2,
    NKUI_LAYOUT_DISTRIBUTION_SPACE_BETWEEN = 3,
    NKUI_LAYOUT_DISTRIBUTION_SPACE_AROUND = 4,
    NKUI_LAYOUT_DISTRIBUTION_SPACE_EVENLY = 5
};

/** Whether children stay on one row/column or flow onto additional lines. */
typedef uint32_t nkui_layout_wrap_mode;
enum NK_ENUM(nkui_layout_wrap_mode) { NKUI_LAYOUT_WRAP_NO_WRAP = 0, NKUI_LAYOUT_WRAP_WRAP = 1 };

/** Optional per-child cross-axis alignment override. */
typedef uint32_t nkui_layout_self_alignment;
enum NK_ENUM(nkui_layout_self_alignment) {
    NKUI_LAYOUT_SELF_ALIGNMENT_INHERIT = 0,
    NKUI_LAYOUT_SELF_ALIGNMENT_START = 1,
    NKUI_LAYOUT_SELF_ALIGNMENT_END = 2,
    NKUI_LAYOUT_SELF_ALIGNMENT_CENTER = 3,
    NKUI_LAYOUT_SELF_ALIGNMENT_BASELINE = 4
};

/** Byte offsets within each fixed-size node record. */
enum {
    NKUI_LAYOUT_NODE_ID_OFFSET = 0,
    NKUI_LAYOUT_NODE_PARENT_OFFSET = 4,
    NKUI_LAYOUT_NODE_VISUAL_KIND_OFFSET = 8,
    NKUI_LAYOUT_NODE_WIDTH_SIZING_OFFSET = 12,
    NKUI_LAYOUT_NODE_WIDTH_VALUE_OFFSET = 16,
    NKUI_LAYOUT_NODE_HEIGHT_SIZING_OFFSET = 20,
    NKUI_LAYOUT_NODE_HEIGHT_VALUE_OFFSET = 24,
    NKUI_LAYOUT_NODE_DIRECTION_OFFSET = 28,
    NKUI_LAYOUT_NODE_PADDING_LEFT_OFFSET = 32,
    NKUI_LAYOUT_NODE_PADDING_RIGHT_OFFSET = 36,
    NKUI_LAYOUT_NODE_PADDING_TOP_OFFSET = 40,
    NKUI_LAYOUT_NODE_PADDING_BOTTOM_OFFSET = 44,
    NKUI_LAYOUT_NODE_CHILD_GAP_OFFSET = 48,
    NKUI_LAYOUT_NODE_BACKGROUND_OFFSET = 52,
    NKUI_LAYOUT_NODE_RADIUS_TOP_LEFT_OFFSET = 68,
    NKUI_LAYOUT_NODE_RADIUS_TOP_RIGHT_OFFSET = 72,
    NKUI_LAYOUT_NODE_RADIUS_BOTTOM_LEFT_OFFSET = 76,
    NKUI_LAYOUT_NODE_RADIUS_BOTTOM_RIGHT_OFFSET = 80,
    NKUI_LAYOUT_NODE_CLIP_FLAGS_OFFSET = 84,
    NKUI_LAYOUT_NODE_TEXT_OFFSET_OFFSET = 88,
    NKUI_LAYOUT_NODE_TEXT_LENGTH_OFFSET = 92,
    NKUI_LAYOUT_NODE_TEXT_COLOR_OFFSET = 96,
    NKUI_LAYOUT_NODE_FONT_FAMILY_OFFSET = 112,
    NKUI_LAYOUT_NODE_FONT_SIZE_OFFSET = 116,
    NKUI_LAYOUT_NODE_LETTER_SPACING_OFFSET = 120,
    NKUI_LAYOUT_NODE_LINE_HEIGHT_OFFSET = 124,
    NKUI_LAYOUT_NODE_TEXT_WRAP_OFFSET = 128,
    NKUI_LAYOUT_NODE_TEXT_ALIGNMENT_OFFSET = 132,
    NKUI_LAYOUT_NODE_TEXT_DIRECTION_OFFSET = 136,
    NKUI_LAYOUT_NODE_TEXT_FLAGS_OFFSET = 140,
    NKUI_LAYOUT_NODE_TRANSFORM_A_OFFSET = 144,
    NKUI_LAYOUT_NODE_TRANSFORM_B_OFFSET = 148,
    NKUI_LAYOUT_NODE_TRANSFORM_C_OFFSET = 152,
    NKUI_LAYOUT_NODE_TRANSFORM_D_OFFSET = 156,
    NKUI_LAYOUT_NODE_TRANSFORM_TX_OFFSET = 160,
    NKUI_LAYOUT_NODE_TRANSFORM_TY_OFFSET = 164,
    NKUI_LAYOUT_NODE_FLAGS_OFFSET = 168,
    /** Packed child alignment: x in bits 0..7, y in bits 8..15. */
    NKUI_LAYOUT_NODE_CHILD_ALIGNMENT_OFFSET = 172,
    /** Parent-relative x offset for absolute-positioned nodes. */
    NKUI_LAYOUT_NODE_POSITION_X_OFFSET = 176,
    /** Parent-relative y offset for absolute-positioned nodes. */
    NKUI_LAYOUT_NODE_POSITION_Y_OFFSET = 180,
    /** Stacking order for absolute-positioned nodes, encoded as signed int32. */
    NKUI_LAYOUT_NODE_Z_INDEX_OFFSET = 184,
    /** Minimum width for FIT/GROW sizing; zero means no minimum. */
    NKUI_LAYOUT_NODE_WIDTH_MIN_OFFSET = 188,
    /** Maximum width for FIT/GROW sizing; zero means unbounded. */
    NKUI_LAYOUT_NODE_WIDTH_MAX_OFFSET = 192,
    /** Minimum height for FIT/GROW sizing; zero means no minimum. */
    NKUI_LAYOUT_NODE_HEIGHT_MIN_OFFSET = 196,
    /** Maximum height for FIT/GROW sizing; zero means unbounded. */
    NKUI_LAYOUT_NODE_HEIGHT_MAX_OFFSET = 200,
    /** Width divided by height; zero disables aspect-ratio sizing. */
    NKUI_LAYOUT_NODE_ASPECT_RATIO_OFFSET = 204,
    /** Relative share of extra width for GROW sizing; must be positive and finite. */
    NKUI_LAYOUT_NODE_WIDTH_GROW_WEIGHT_OFFSET = 208,
    /** Relative share of extra height for GROW sizing; must be positive and finite. */
    NKUI_LAYOUT_NODE_HEIGHT_GROW_WEIGHT_OFFSET = 212,
    /** Main-axis free-space distribution policy. */
    NKUI_LAYOUT_NODE_CHILD_DISTRIBUTION_OFFSET = 216,
    /** Vertical gap between wrapped rows. */
    NKUI_LAYOUT_NODE_ROW_GAP_OFFSET = 220,
    /** Horizontal gap between wrapped columns. */
    NKUI_LAYOUT_NODE_COLUMN_GAP_OFFSET = 224,
    /** Wrap policy: zero keeps one row/column, one enables wrapping. */
    NKUI_LAYOUT_NODE_WRAP_MODE_OFFSET = 228,
    /** Per-child cross-axis alignment override; zero inherits the parent. */
    NKUI_LAYOUT_NODE_ALIGN_SELF_OFFSET = 232,
    /** Application-defined intrinsic-content version used by measurement caching. */
    NKUI_LAYOUT_NODE_MEASURE_VERSION_OFFSET = 236,
    /** Normalized horizontal transform origin, in the range 0..1. */
    NKUI_LAYOUT_NODE_TRANSFORM_ORIGIN_X_OFFSET = 240,
    /** Normalized vertical transform origin, in the range 0..1. */
    NKUI_LAYOUT_NODE_TRANSFORM_ORIGIN_Y_OFFSET = 244,
    /** Haxe-owned paint/content revision used by native raster caches. */
    NKUI_LAYOUT_NODE_CONTENT_REVISION_OFFSET = 248,
    /** Haxe-owned resolved geometry revision for shared scene metadata. */
    NKUI_LAYOUT_NODE_GEOMETRY_REVISION_OFFSET = 252,
    /** Haxe-owned compositing revision for shared scene metadata. */
    NKUI_LAYOUT_NODE_COMPOSITE_REVISION_OFFSET = 256
};

/** Opaque retained layout session used by a Haxe-owned component tree. */
NK_DECLARE_HANDLE(nkui_layout_session);

/** Resolved geometry for one node in the most recently submitted layout. */
typedef struct nkui_layout_item {
    /** Set to sizeof(nkui_layout_item) or a larger compatible size. */
    uint32_t struct_size NK_STRUCT_SIZE;
    uint32_t node_id;
    nkui_layout_resolved_flags flags;
    /** Layout bounds before the returned world transform is applied. */
    float x;
    float y;
    float width;
    float height;
    /** Effective ancestor clip in viewport coordinates, after transforms. */
    float clip_x;
    float clip_y;
    float clip_width;
    float clip_height;
    /** Union of direct child bounds, relative to this node's content origin. */
    float content_x;
    float content_y;
    float content_width;
    float content_height;
    /** World affine transform using x'=a*x+c*y+tx, y'=b*x+d*y+ty. */
    float transform[6];
    /** First text-line baseline in layout coordinates when HAS_BASELINE is set. */
    float baseline;
    uint32_t reserved[2];
} nkui_layout_item;

/** Per-submission logical viewport and layout timing. */
typedef struct nkui_layout_frame_input {
    /** Set to sizeof(nkui_layout_frame_input) or a larger compatible size. */
    uint32_t struct_size NK_STRUCT_SIZE;
    float width;
    float height;
    float delta_seconds;
} nkui_layout_frame_input;

/** Constraints supplied to an external intrinsic measurer for one custom node. */
typedef struct nkui_layout_measure_constraints {
    /** Always sizeof(nkui_layout_measure_constraints) for this ABI. */
    uint32_t struct_size NK_STRUCT_SIZE;
    float min_width;
    float max_width;
    float min_height;
    float max_height;
} nkui_layout_measure_constraints;

/** Flags returned in nkui_layout_measure_result.flags. */
typedef uint32_t nkui_layout_measure_flags;
enum NK_FLAGS(nkui_layout_measure_flags) { NKUI_LAYOUT_MEASURE_HAS_BASELINE = 1u << 0 };

/** Optional metrics returned by an external intrinsic measurer. */
typedef struct nkui_layout_measure_result {
    /** Set to sizeof(nkui_layout_measure_result) when returned. */
    uint32_t struct_size NK_STRUCT_SIZE;
    float width;
    float height;
    float baseline;
    nkui_layout_measure_flags flags;
} nkui_layout_measure_result;

/** Cumulative intrinsic-measure activity for one layout session. */
typedef struct nkui_layout_measure_stats {
    /** Set to sizeof(nkui_layout_measure_stats) when returned by the API. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Intrinsic-measure requests made by Clay. */
    uint64_t requests;
    /** Requests served by the persistent exact-constraint cache. */
    uint64_t cache_hits;
    /** Requests not found in the persistent cache. */
    uint64_t cache_misses;
    /** Calls made to the external intrinsic-measure callback. */
    uint64_t callback_calls;
    /** Current number of entries in the bounded cache. */
    uint64_t cache_entries;
    /** Maximum number of entries retained by the cache. */
    uint64_t cache_capacity;
} nkui_layout_measure_stats;

/** Cumulative geometric hit-test activity for one layout session. */
typedef struct nkui_layout_hit_test_stats {
    /** Set to sizeof(nkui_layout_hit_test_stats) when returned by the API. */
    uint32_t struct_size NK_STRUCT_SIZE;
    /** Valid native hit-test queries performed by the session. */
    uint64_t hit_test_count;
    /** Resolved scene nodes inspected across all queries. */
    uint64_t nodes_visited;
    /** Scene branches rejected by visibility, bounds, or clipping. */
    uint64_t subtrees_rejected;
    /** Precise inverse-transform self tests performed. */
    uint64_t precise_hit_tests;
    /** Maximum resolved scene nodes inspected by one query. */
    uint64_t max_nodes_visited;
    /** Monotonic elapsed time spent in native hit testing, in nanoseconds. */
    uint64_t hit_test_time_nanoseconds;
} nkui_layout_hit_test_stats;

/** Synchronous intrinsic measurement callback for custom-visual nodes.
 *
 * The callback may use unrelated UI registries, but must not re-enter layout
 * session operations. Such re-entry returns NKUI_ERROR_INVALID_ARGUMENT.
 */
typedef nkui_layout_measure_result(NK_CALL *nkui_layout_measure_callback)(
    uint32_t node_id, nkui_layout_measure_constraints constraints, void *NK_NULLABLE user_data);
typedef nkui_layout_measure_callback NK_NULLABLE nkui_nullable_layout_measure_callback;

/** Creates a private layout session. */
NKUI_API nkui_result NK_CALL nkui_layout_session_create(nkui_layout_session *out_session NKUI_OUT);

/** Destroys a layout session and releases its retained native frame state. */
NKUI_API nkui_result NK_CALL nkui_layout_session_destroy(nkui_layout_session session);

/** Copies the font collection configuration into the layout session. */
NKUI_API nkui_result NK_CALL nkui_layout_session_set_font_collection(nkui_layout_session session,
                                                                     nkui_resource fonts);

/**
 * Installs or removes the synchronous intrinsic measurer for custom-visual
 * nodes. The callback is invoked during submit on the submitting thread. Its
 * user data is borrowed and the callback must be detached before its Haxe/C
 * callback handle is closed.
 */
NKUI_API nkui_result NK_CALL nkui_layout_session_set_measure_callback(
    nkui_layout_session session, nkui_nullable_layout_measure_callback callback NK_RETAINED,
    void *NK_NULLABLE user_data);

/** Returns cumulative intrinsic-measure counters and current cache occupancy. */
NKUI_API nkui_result NK_CALL nkui_layout_session_get_measure_stats(
    nkui_layout_session session, nkui_layout_measure_stats *out_stats NKUI_OUT);

/** Returns cumulative geometric hit-test counters for the session. */
NKUI_API nkui_result NK_CALL nkui_layout_session_get_hit_test_stats(
    nkui_layout_session session, nkui_layout_hit_test_stats *out_stats NKUI_OUT);

/** Clears all custom-paint display lists attached to the session. */
NKUI_API nkui_result NK_CALL nkui_layout_session_clear_custom_paints(nkui_layout_session session);

/**
 * Associates a retained display list with a custom-visual node from the most
 * recently submitted tree. The session retains the list until it is replaced,
 * cleared, or the session is destroyed.
 */
NKUI_API nkui_result NK_CALL nkui_layout_session_set_custom_paint(nkui_layout_session session,
                                                                  uint32_t node_id,
                                                                  nkui_display_list display_list);

/** Selects whether a custom-paint plane is rendered as vector content or cached as a raster. */
NKUI_API nkui_result NK_CALL nkui_layout_session_set_custom_paint_cache_policy(
    nkui_layout_session session, uint32_t node_id, nkui_layout_cache_policy policy);

/** Selects whether a render-node subtree is rendered normally or cached as a raster. */
NKUI_API nkui_result NK_CALL nkui_layout_session_set_cache_policy(nkui_layout_session session,
                                                                  uint32_t node_id,
                                                                  nkui_layout_cache_policy policy);

/**
 * Submits one flat, Haxe-owned render/layout tree transaction.
 *
 * The transaction is little-endian and consists of a fixed header, node
 * records of NKUI_LAYOUT_NODE_RECORD_BYTES bytes, and a UTF-8 string table.
 * Node text offsets are absolute byte offsets from the beginning of the
 * transaction. The native side copies all values before returning, so the
 * input buffer may be reused. The transaction is bounded by
 * NKUI_LAYOUT_MAX_TRANSACTION_BYTES; node capacity grows with submitted data.
 * Each node transform is applied about its normalized resolved-box origin;
 * clipped nodes must have an axis-aligned cumulative transform. The default
 * origin is the center of the resolved box.
 */
NKUI_API nkui_result NK_CALL nkui_layout_session_submit(
    nkui_layout_session session, const uint8_t *transaction NKUI_IN_ARRAY(transaction_bytes),
    uint32_t transaction_bytes, const nkui_layout_frame_input *frame);

/** Copies the resolved geometry for every node in submission order.
 *
 * The buffer contains `nkui_layout_item` records with `struct_size` set.
 * Bounds remain in pre-transform layout coordinates; `transform` maps them to
 * viewport coordinates. `clip_*` is the effective inherited clip, and
 * `content_*` describes the union of direct children relative to the node's
 * padded content origin. Pass NULL to query the required byte count, then
 * pass a buffer of that size.
 */
NKUI_API nkui_result NK_CALL nkui_layout_session_get_resolved_items(
    nkui_layout_session session, uint8_t *out_buffer NK_OUT_BUFFER(inout_bytes),
    uint32_t *inout_bytes NK_INOUT);

/** Returns the root-to-target geometric hit path for a viewport point.
 *
 * The returned path contains stable node IDs in root-to-target order. Pass
 * NULL to query the required byte count, then provide a buffer containing at
 * least that many bytes. The path buffer is an array of uint32_t values even
 * though its capacity is reported in bytes. A point with no eligible target
 * returns an empty path. Geometric questions only are considered: visibility,
 * clipping, transforms, hit policies, and native paint order.
 */
NKUI_API nkui_result NK_CALL nkui_layout_session_hit_test(
    nkui_layout_session session, float x, float y, uint8_t *out_path NK_OUT_BUFFER(inout_bytes),
    uint32_t *inout_bytes NK_INOUT);

/** Executes a geometric hit test into caller-owned reusable storage.
 *
 * `path` is a mutable byte buffer containing uint32_t node IDs in
 * root-to-target order. `path_capacity_bytes` is its capacity in bytes and
 * must be a multiple of sizeof(uint32_t). `out_count` receives the number of
 * node IDs written. If the buffer is too small, no IDs are written,
 * `out_count` receives the required number of IDs, and NKUI_ERROR_INVALID_ARGUMENT
 * is returned so the caller can grow and retry without a size-query allocation.
 * A zero-capacity buffer is valid when `path` is NULL.
 */
NKUI_API nkui_result NK_CALL nkui_layout_session_hit_test_into(
    nkui_layout_session session, float x, float y,
    uint8_t *path NKUI_IN_ARRAY(path_capacity_bytes), uint32_t path_capacity_bytes,
    uint32_t *out_count NKUI_OUT);

/** Executes the submitted layout through the existing NativeKit renderer.
 *
 * The retained UI surface uses on-demand scheduling; request another frame
 * with nk_surface_request_frame() after state, input, or animation changes.
 * When load_existing is non-zero, the layout is composited over the current
 * surface contents instead of clearing the frame first.
 */
NKUI_API nkui_result NK_CALL nkui_layout_session_render_frame(nkui_renderer renderer,
                                                              nkui_layout_session session,
                                                              nk_surface surface,
                                                              const nkui_frame_info *frame_info,
                                                              nk_bool load_existing);

#ifdef __cplusplus
}
#endif

#endif
