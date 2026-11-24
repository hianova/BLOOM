#ifndef BLOOM_SDK_H
#define BLOOM_SDK_H

#include "bloom_gpu.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BLOOM_SDK_ACT_CLICK       = 0,
    BLOOM_SDK_ACT_SET_VALUE   = 1,
    BLOOM_SDK_ACT_SCROLL      = 2,
    BLOOM_SDK_ACT_FOCUS       = 3,
    BLOOM_SDK_ACT_SET_DOF     = 4,
    BLOOM_SDK_ACT_SET_CUTAWAY = 5
} BloomSdkAction;

typedef struct {
    /* Unified Pixel-Render Identity */
    uint8_t     domain;       /* BLOOM_GROUND_MESH | BLOOM_GROUND_ANALYTIC | BLOOM_GROUND_UI */
    uint16_t    slot;
    uint8_t     part_id;      /* UI sub-part/vlist row, or mesh joint_id */
    BloomHandle handle;
    uint64_t    key;
    char        id[64];
    char        label[128];

    /* UI Metadata (zeroed for 3D entities) */
    BloomUiKind ui_kind;
    bool        disabled;
    bool        modal_trap;
    bool        focused;
    bool        hovered;
    bool        toggle_on;
    float       slider_t;
    float       scroll_y;
    uint32_t    action_mask;
    float       layout_rect[4]; /* x, y, w, h in logical viewport px */

    /* Pixel-Exact GPU Grounding Observation */
    uint32_t    visible_px;
    float       visible_ratio;
    bool        occluded;
    bool        actionable;
    uint32_t    screen_bbox[4]; /* min_x, min_y, max_x, max_y */
    float       anchor_x;       /* Optimal interior unoccluded pixel X */
    float       anchor_y;       /* Optimal interior unoccluded pixel Y */
    float       min_depth;      /* Normalized [0..1] quantized GPU depth */

    /* 3D World Pose / Ray Hit */
    float       world_pos[3];
    float       local_dof[3];
    bool        cutaway;
} BloomSdkEntity;

typedef struct {
    BloomCamera     camera;
    uint32_t        vp_w;
    uint32_t        vp_h;
    double          ground_ms;
    uint32_t        entity_cnt;
    BloomSdkEntity *entities;
} BloomSdkSnapshot;

/** Capture a unified occlusion-aware snapshot covering all UI, Mesh, and Analytic entities. */
bool bloom_sdk_capture_snapshot(BloomGpu *gpu, BloomScene *scene,
                                uint32_t vp_w, uint32_t vp_h,
                                BloomSdkSnapshot *out_snap);

void bloom_sdk_free_snapshot(BloomSdkSnapshot *snap);

/** Pixel-exact pick at (px, py) across 2D UI, 3D Meshlets, and 3D Analytics. */
bool bloom_sdk_pick_at(BloomGpu *gpu, BloomScene *scene,
                       uint32_t vp_w, uint32_t vp_h,
                       uint32_t px, uint32_t py,
                       BloomSdkEntity *out_entity);

/** Find entity by exact id string, falling back to bloom_fnv1a64(id) key match. */
const BloomSdkEntity *bloom_sdk_find_by_id(const BloomSdkSnapshot *snap, const char *id);

/** Filter entities by domain bitmask (1u << domain), UI kind (-1 = any), and actionable flag. */
uint32_t bloom_sdk_query(const BloomSdkSnapshot *snap,
                         uint32_t domain_mask, int ui_kind_or_neg1,
                         bool only_actionable,
                         uint32_t *out_idxs, uint32_t max_out);

/**
 * Dispatch an embodied action strictly using verified GPU anchor coordinates.
 * Rejects occluded, disabled, or modal-blocked entities cleanly (returns false).
 */
bool bloom_sdk_dispatch_action(BloomScene *scene, const BloomSdkSnapshot *snap,
                               const char *id, BloomSdkAction action, float value);

/** Export compact deterministic JSON snapshot (returns bytes needed, excluding NUL). */
size_t bloom_sdk_snapshot_to_json(const BloomSdkSnapshot *snap, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_SDK_H */
