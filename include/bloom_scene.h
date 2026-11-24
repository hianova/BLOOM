#ifndef BLOOM_SCENE_H
#define BLOOM_SCENE_H

#include "bloom_bobj.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t BloomHandle;
#define BLOOM_HANDLE_NULL UINT32_C(0)

typedef enum {
    BLOOM_UI_PANEL  = 0,
    BLOOM_UI_BUTTON = 1,
    BLOOM_UI_SLIDER = 2,
    BLOOM_UI_TEXT   = 3,
    BLOOM_UI_VLIST  = 4,
    BLOOM_UI_MODAL  = 5
} BloomUiKind;

enum {
    BLOOM_ACT_NONE    = 0u,
    BLOOM_ACT_CLICK   = 1u << 0,
    BLOOM_ACT_DRAG    = 1u << 1,
    BLOOM_ACT_SCROLL  = 1u << 2,
    BLOOM_ACT_FOCUS   = 1u << 3,
    BLOOM_ACT_SET_DOF = 1u << 4,
    BLOOM_ACT_CUTAWAY = 1u << 5
};

typedef enum {
    BLOOM_FLEX_COL = 0,
    BLOOM_FLEX_ROW = 1
} BloomFlexDir;

typedef enum {
    BLOOM_ANIM_POS_X = 0,
    BLOOM_ANIM_POS_Y = 1,
    BLOOM_ANIM_POS_Z = 2,
    BLOOM_ANIM_ROT_Y = 3,
    BLOOM_ANIM_SCALE = 4,
    BLOOM_ANIM_DOF_0 = 5,
    BLOOM_ANIM_DOF_1 = 6,
    BLOOM_ANIM_DOF_2 = 7,
    BLOOM_ANIM_CLIP_D = 8
} BloomAnimChannel;

typedef enum {
    BLOOM_EASE_LINEAR = 0,
    BLOOM_EASE_CUBIC_IN_OUT = 1,
    BLOOM_EASE_SPRING = 2
} BloomEasing;

typedef enum {
    BLOOM_LOOP_ONCE = 0,
    BLOOM_LOOP_REPEAT = 1,
    BLOOM_LOOP_PING_PONG = 2
} BloomLoopMode;

typedef struct {
    const char *id;
    BloomHandle parent;
    BloomUiKind kind;
    BloomFlexDir flex_dir;
    float x, y;          /* Used only for top-level floating panels (parent == NULL) */
    float w, h;          /* >0 explicit px; <=0 flex stretch along axis */
    float pad;
    float gap;
    uint32_t fill_rgba8;
    uint32_t border_rgba8;
    uint32_t text_rgba8;
    float radius_px;
    float border_px;
    float font_px;
    float shadow_blur_px;
    uint32_t shadow_rgba8;
    const char *label;
    uint64_t bind_obj_key;
    uint8_t bind_dof_slot; /* 0..2 = DOF, 3 = clip_plane.d */
    uint32_t vlist_total_rows;
    float vlist_row_h;
    bool disabled;
    bool modal_trap;
    uint32_t action_mask;
} BloomUiNodeDesc;

typedef struct {
    BloomHandle handle;
    uint64_t    key;
    const char *id;
    const char *label;
    BloomUiKind kind;
    float       x, y, w, h;
    float       clip_x, clip_y, clip_w, clip_h;
    uint16_t    z_order;
    bool        disabled;
    bool        modal_trap;
    bool        focused;
    bool        hovered;
    bool        toggle_on;
    float       slider_t;
    float       scroll_y;
    uint32_t    action_mask;
    uint64_t    bind_obj_key;
    uint8_t     bind_dof_slot;
} BloomUiMeta;

typedef struct {
    BloomHandle        handle;
    uint64_t           key;
    const char        *id;
    BloomObjectState48 world_state;
    float              local_dof[3];
    bool               cutaway;
} BloomObjectMeta;

typedef struct {
    uint32_t            index;
    uint64_t            key;
    BloomAnalyticPrim32 prim;
} BloomAnalyticMeta;

typedef struct {
    uint64_t node_key;
    float x, y, w, h;
    float font_px;
    uint32_t rgba8;
    uint16_t clip_id;
    uint16_t z_order;
    char text[128];
} BloomTextRun;

typedef struct {
    uint64_t target_key;
    BloomAnimChannel channel;
    float t_start;
    float t_end;
    float v_start;
    float v_end;
    BloomEasing easing;
    BloomLoopMode loop_mode;
} BloomAnimTrack;

typedef struct BloomScene BloomScene;

/** Lifecycle */
BloomScene *bloom_scene_create(void);
void bloom_scene_destroy(BloomScene *scene);

/** Camera & Dirty Control (Idle Zero-Submit Discipline) */
void bloom_scene_set_camera(BloomScene *scene, const BloomCamera *cam);
BloomCamera bloom_scene_get_camera(const BloomScene *scene);
bool bloom_scene_is_dirty(const BloomScene *scene);
bool bloom_scene_geom_dirty(const BloomScene *scene);
void bloom_scene_mark_dirty(BloomScene *scene);
void bloom_scene_clear_dirty(BloomScene *scene);
void bloom_scene_clear_geom_dirty(BloomScene *scene);

/** Asset Templates & Generational Object Management */
uint16_t bloom_scene_add_template(BloomScene *scene, const char *name, uint32_t meshlet_cnt,
                                  const BloomMeshletHeader32 *hdrs, const BloomVtx12 *verts,
                                  const uint8_t *local_idx, uint32_t idx_bytes);

BloomHandle bloom_scene_spawn_object(BloomScene *scene, const char *id, uint16_t template_id,
                                     const float pos[3], const float quat[4], const float scale[3],
                                     uint32_t rgba8_mat, const BloomConstraintSpec *cspec);

bool bloom_scene_destroy_object(BloomScene *scene, BloomHandle handle);
bool bloom_scene_is_handle_valid(const BloomScene *scene, BloomHandle handle);
BloomHandle bloom_scene_find_object_by_key(const BloomScene *scene, uint64_t key);
bool bloom_scene_set_parent(BloomScene *scene, BloomHandle child, BloomHandle parent);

bool bloom_scene_set_object_trs(BloomScene *scene, BloomHandle handle,
                                const float pos[3], const float quat[4], const float scale[3]);
bool bloom_scene_set_object_dof(BloomScene *scene, BloomHandle handle, uint8_t slot, float val);
bool bloom_scene_set_object_cutaway(BloomScene *scene, BloomHandle handle, bool enabled,
                                    const float plane_xyzd[4]);
bool bloom_scene_get_object_state(const BloomScene *scene, BloomHandle handle, BloomObjectState48 *out);

/** Track B Analytic Primitives */
uint32_t bloom_scene_spawn_analytic(BloomScene *scene, const char *id, const BloomAnalyticPrim32 *prim);
bool bloom_scene_update_analytic(BloomScene *scene, uint32_t idx, const BloomAnalyticPrim32 *prim);

/** Load / Hot-Reload 3C Binary SSOT preserving StateStore by fnv1a64(id) */
bool bloom_scene_load_bobj(BloomScene *scene, const BloomBobjDoc *doc, bool preserve_live_state);

/** Layer 0 External Constraint State Projection (from CPU FLOW / PHY / external sims) */
void bloom_scene_apply_projections(BloomScene *scene, const BloomStateProjection *projs, uint32_t count);

/** World Transform Propagation + Contiguous GPU Staging Arrays */
void bloom_scene_sync_transforms(BloomScene *scene);
uint32_t bloom_scene_get_template_vtx_count(const BloomScene *scene);
uint32_t bloom_scene_get_template_idx_bytes(const BloomScene *scene);
uint32_t bloom_scene_copy_template_geom(const BloomScene *scene, BloomVtx12 *out_vtx, uint8_t *out_idx);
uint32_t bloom_scene_get_gpu_states(const BloomScene *scene, const BloomObjectState48 **out_ptr);
uint32_t bloom_scene_get_gpu_meshlets(const BloomScene *scene, const BloomMeshletHeader32 **out_ptr);
uint32_t bloom_scene_get_analytics(const BloomScene *scene, const BloomAnalyticPrim32 **out_ptr);

/** Tri-Layer 2D UI, Flexbox, VirtualList & Hit-Test */
BloomHandle bloom_scene_add_ui(BloomScene *scene, const BloomUiNodeDesc *desc);
bool bloom_scene_destroy_ui(BloomScene *scene, BloomHandle ui);
void bloom_scene_clear_ui(BloomScene *scene);
bool bloom_scene_set_ui_label(BloomScene *scene, BloomHandle ui, const char *label);
bool bloom_scene_set_ui_scroll(BloomScene *scene, BloomHandle ui, float scroll_y);
bool bloom_scene_set_ui_focused(BloomScene *scene, BloomHandle ui);
BloomHandle bloom_scene_get_focused_ui(const BloomScene *scene);
float bloom_scene_get_ui_slider(const BloomScene *scene, BloomHandle ui);
bool bloom_scene_get_ui_toggle(const BloomScene *scene, BloomHandle ui);

/** Unified Embodied Inspection Getters (for SDK & Vision Modules) */
uint32_t bloom_scene_get_object_slot_count(const BloomScene *scene);
bool bloom_scene_get_object_meta(const BloomScene *scene, uint32_t slot, BloomObjectMeta *out);
uint32_t bloom_scene_get_analytic_count(const BloomScene *scene);
bool bloom_scene_get_analytic_meta(const BloomScene *scene, uint32_t idx, BloomAnalyticMeta *out);
uint32_t bloom_scene_get_ui_count(const BloomScene *scene);
bool bloom_scene_get_ui_meta(const BloomScene *scene, uint32_t slot, BloomUiMeta *out);
bool bloom_scene_get_template_mesh(const BloomScene *scene, uint16_t template_id,
                                   uint32_t *out_mcnt, const BloomMeshletHeader32 **out_hdrs,
                                   const BloomVtx12 **out_verts, const uint8_t **out_lidx,
                                   uint32_t *out_idx_bytes);

void bloom_scene_layout_ui(BloomScene *scene, float vp_w, float vp_h);
bool bloom_scene_handle_input(BloomScene *scene, const BloomInputEvent *ev);

uint32_t bloom_scene_get_opaque_quads(const BloomScene *scene, const BloomQuadHot **out_ptr);
uint32_t bloom_scene_get_alpha_quads(const BloomScene *scene, const BloomQuadHot **out_ptr);
uint32_t bloom_scene_get_shadows(const BloomScene *scene, const BloomShadowHot **out_ptr);
uint32_t bloom_scene_get_clips(const BloomScene *scene, const BloomClipRect **out_ptr);
uint32_t bloom_scene_get_text_runs(const BloomScene *scene, const BloomTextRun **out_ptr);

/** Visual Presentation Timeline */
void bloom_timeline_clear(BloomScene *scene);
uint32_t bloom_timeline_add_track(BloomScene *scene, const BloomAnimTrack *track);
void bloom_timeline_set_playing(BloomScene *scene, bool playing);
bool bloom_timeline_is_playing(const BloomScene *scene);
void bloom_timeline_scrub(BloomScene *scene, float t_sec);
bool bloom_timeline_step(BloomScene *scene, float dt_sec);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_SCENE_H */
