#include "bloom_scene.h"

#include <stdio.h>
#include <stdlib.h>

/* ============================================================================
 * Internal Scene Containers
 * ============================================================================ */

typedef struct {
    char name[64];
    uint32_t meshlet_cnt;
    uint32_t vtx_cnt;
    uint32_t idx_bytes;
    uint32_t gpu_vtx_base;
    uint32_t gpu_idx_base;
    BloomMeshletHeader32 *hdrs;
    BloomVtx12 *verts;
    uint8_t *local_idx;
} SceneTemplate;

typedef struct {
    bool alive;
    uint16_t gen;
    uint64_t key;
    char id[64];
    BloomHandle parent;
    float local_pos[3];
    float local_quat[4];
    float local_scale[3];
    float local_dof[3];
    float local_clip[4];
    uint32_t rgba8;
    uint32_t flags;
    BloomConstraintSpec cspec;
    BloomObjectState48 world_state;
    bool synced;
} ObjectSlot;

typedef struct {
    uint64_t key;
    float dof[3];
    float clip_plane[4];
    uint32_t flags;
    bool has_clip;
    float slider_t;
    bool toggle_on;
    float scroll_y;
    char text_buf[128];
    int32_t selected_idx;
    float progress_t;
} StateEntry;

typedef struct {
    bool alive;
    uint16_t gen;
    BloomHandle self;
    uint64_t key;
    BloomUiNodeDesc desc;
    char id_buf[64];
    char label_buf[128];
    char placeholder_buf[64];
    char options_buf[256];
    char text_buf[128];
    char preedit_buf[64];
    float x, y, w, h;
    float clip_x, clip_y, clip_w, clip_h;
    uint16_t clip_id;
    uint16_t z_order;
    float slider_t;
    bool toggle_on;
    float scroll_y;
    float content_h;
    int32_t selected_idx;
    bool dropdown_open;
    float progress_t;
} UiSlot;

struct BloomScene {
    BloomCamera camera;

    /* Asset templates */
    SceneTemplate *templates;
    uint32_t tpl_cnt, tpl_cap;

    /* Generational SlotMap for 3D objects */
    ObjectSlot *objs;
    uint32_t obj_cap;
    uint32_t *free_obj_slots;
    uint32_t free_obj_cnt, free_obj_cap;

    /* Persistent StateStore keyed by fnv1a64(id) across hot-reloads */
    StateEntry *states;
    uint32_t state_cnt, state_cap;

    /* Track B analytics */
    BloomAnalyticPrim32 *analytics;
    uint64_t *analytic_keys;
    uint32_t ana_cnt, ana_cap;

    /* Milestone 2 Track 2B PBR Materials & Lights */
    BloomMaterial32 *materials;
    uint32_t mat_cnt, mat_cap;
    BloomLight32 *lights;
    uint32_t light_cnt, light_cap;

    /* GPU-ready flattened output arrays */
    BloomObjectState48 *gpu_states;
    uint32_t gpu_state_cnt, gpu_state_cap;
    uint32_t *gpu_obj_slots;
    uint32_t gpu_obj_slot_cap;
    BloomMeshletHeader32 *gpu_meshlets;
    uint32_t gpu_meshlet_cnt, gpu_meshlet_cap;

    /* UI tree + pre-partitioned output */
    UiSlot *uis;
    uint32_t ui_cnt, ui_cap;
    BloomHandle captured_ui;
    BloomHandle hovered_ui;
    BloomHandle focused_ui;
    bool pointer_down;
    float last_ptr_x, last_ptr_y;
    BloomTextMeasureFn text_measure_fn;
    void *text_measure_ud;

    BloomQuadHot *opaque_quads;
    uint32_t opq_cnt, opq_cap;
    BloomQuadHot *alpha_quads;
    uint32_t alp_cnt, alp_cap;
    BloomShadowHot *shadows;
    uint32_t sh_cnt, sh_cap;
    BloomClipRect *clips;
    uint32_t clip_cnt, clip_cap;
    BloomTextRun *text_runs;
    uint32_t text_cnt, text_cap;
    BloomSpriteInst *sprites;
    uint32_t sp_cnt, sp_cap;

    /* Visual timeline */
    BloomAnimTrack *tracks;
    uint32_t track_cnt, track_cap;
    bool timeline_playing;
    float timeline_t;

    /* Dirty tracking (Idle Zero-Submit Discipline) */
    bool geom_dirty;
    bool state_dirty;
    bool ui_dirty;
    bool camera_dirty;
};

/* ============================================================================
 * Vector / Handle / Math Helpers
 * ============================================================================ */

static inline BloomHandle make_handle(uint32_t idx, uint16_t gen) {
    return (idx & UINT32_C(0xFFFFF)) | (((uint32_t)(gen & UINT32_C(0xFFF))) << 20u);
}

static inline uint32_t handle_index(BloomHandle h) {
    return h & UINT32_C(0xFFFFF);
}

static inline uint16_t handle_gen(BloomHandle h) {
    return (uint16_t)((h >> 20u) & UINT32_C(0xFFF));
}

static bool grow_array(void **ptr, uint32_t *cap, uint32_t needed, size_t elem_size) {
    if (needed <= *cap) return true;
    uint32_t old_cap = *cap;
    uint32_t ncap = old_cap ? (old_cap * 2u) : 16u;
    while (ncap < needed) ncap *= 2u;
    void *np = realloc(*ptr, (size_t)ncap * elem_size);
    if (!np) return false;
    if (ncap > old_cap) {
        memset((uint8_t *)np + (size_t)old_cap * elem_size, 0, (size_t)(ncap - old_cap) * elem_size);
    }
    *ptr = np;
    *cap = ncap;
    return true;
}

static void quat_mul_f32(const float a[4], const float b[4], float out[4]) {
    float ax = a[0], ay = a[1], az = a[2], aw = a[3];
    float bx = b[0], by = b[1], bz = b[2], bw = b[3];
    out[0] = aw * bx + ax * bw + ay * bz - az * by;
    out[1] = aw * by - ax * bz + ay * bw + az * bx;
    out[2] = aw * bz + ax * by - ay * bx + az * bw;
    out[3] = aw * bw - ax * bx - ay * by - az * bz;
}

static void quat_rotate_f32(const float q[4], const float v[3], float out[3]) {
    float qx = q[0], qy = q[1], qz = q[2], qw = q[3];
    float tx = 2.0f * (qy * v[2] - qz * v[1]);
    float ty = 2.0f * (qz * v[0] - qx * v[2]);
    float tz = 2.0f * (qx * v[1] - qy * v[0]);
    float cx = qy * tz - qz * ty;
    float cy = qz * tx - qx * tz;
    float cz = qx * ty - qy * tx;
    out[0] = v[0] + qw * tx + cx;
    out[1] = v[1] + qw * ty + cy;
    out[2] = v[2] + qw * tz + cz;
}

static float clamp_f32(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint16_t f32_to_q14_2(float px) {
    long q = lround((double)clamp_f32(px, 0.0f, 16383.75f) * 4.0);
    return (uint16_t)(q < 0 ? 0 : (q > 65535 ? 65535 : q));
}

static uint8_t f32_to_q6_2(float px) {
    long q = lround((double)clamp_f32(px, 0.0f, 63.75f) * 4.0);
    return (uint8_t)(q < 0 ? 0 : (q > 255 ? 255 : q));
}

static StateEntry *state_store_get_or_create(BloomScene *s, uint64_t key, bool *created) {
    for (uint32_t i = 0u; i < s->state_cnt; ++i) {
        if (s->states[i].key == key) {
            if (created) *created = false;
            return &s->states[i];
        }
    }
    if (!grow_array((void **)&s->states, &s->state_cap, s->state_cnt + 1u, sizeof(StateEntry))) {
        return NULL;
    }
    StateEntry *e = &s->states[s->state_cnt++];
    memset(e, 0, sizeof(*e));
    e->key = key;
    e->slider_t = 0.5f;
    if (created) *created = true;
    return e;
}

static const StateEntry *state_store_find(const BloomScene *s, uint64_t key) {
    for (uint32_t i = 0u; i < s->state_cnt; ++i) {
        if (s->states[i].key == key) return &s->states[i];
    }
    return NULL;
}

/* ============================================================================
 * Lifecycle & Camera
 * ============================================================================ */

BloomScene *bloom_scene_create(void) {
    BloomScene *s = (BloomScene *)calloc(1u, sizeof(BloomScene));
    if (!s) return NULL;
    /* Slot 0 reserved as BLOOM_HANDLE_NULL sentinel */
    grow_array((void **)&s->objs, &s->obj_cap, 32u, sizeof(ObjectSlot));
    memset(&s->objs[0], 0, sizeof(ObjectSlot));
    s->objs[0].alive = false;
    s->objs[0].gen = 0u;
    /* Mark slots 1..obj_cap-1 unused: allocate sequentially on demand */
    grow_array((void **)&s->uis, &s->ui_cap, 32u, sizeof(UiSlot));
    s->ui_cnt = 1u; /* Index 0 reserved for NULL */

    /* Material 0 reserved as default legacy material */
    grow_array((void **)&s->materials, &s->mat_cap, 8u, sizeof(BloomMaterial32));
    if (s->materials) {
        s->materials[0].base_rgba8 = 0xFFFFFFFFu;
        s->materials[0].roughness_f16 = bloom_f32_to_f16(0.5f);
        s->materials[0].ao_f16 = bloom_f32_to_f16(1.0f);
        s->materials[0].normal_scale_f16 = bloom_f32_to_f16(1.0f);
        s->mat_cnt = 1u;
    }

    s->camera.pos[0] = 0.0f;
    s->camera.pos[1] = 0.0f;
    s->camera.pos[2] = -2.0f;
    s->camera.yaw = 0.0f;
    s->camera.pitch = 0.0f;
    s->camera.focal = 1.8f;
    s->camera.near_z = 0.5f;
    s->camera.far_z = 80.0f;
    s->geom_dirty = true;
    s->state_dirty = true;
    s->ui_dirty = true;
    s->camera_dirty = true;
    return s;
}

void bloom_scene_destroy(BloomScene *s) {
    if (!s) return;
    for (uint32_t i = 0u; i < s->tpl_cnt; ++i) {
        free(s->templates[i].hdrs);
        free(s->templates[i].verts);
        free(s->templates[i].local_idx);
    }
    free(s->templates);
    free(s->objs);
    free(s->free_obj_slots);
    free(s->states);
    free(s->analytics);
    free(s->analytic_keys);
    free(s->materials);
    free(s->lights);
    free(s->gpu_states);
    free(s->gpu_obj_slots);
    free(s->gpu_meshlets);
    free(s->uis);
    free(s->opaque_quads);
    free(s->alpha_quads);
    free(s->shadows);
    free(s->clips);
    free(s->text_runs);
    free(s->sprites);
    free(s->tracks);
    free(s);
}

void bloom_scene_set_camera(BloomScene *s, const BloomCamera *cam) {
    if (!s || !cam) return;
    s->camera = *cam;
    s->camera_dirty = true;
}

BloomCamera bloom_scene_get_camera(const BloomScene *s) {
    BloomCamera zero = {{0}, 0, 0, 1.8f, 0.5f, 80.0f};
    return s ? s->camera : zero;
}

bool bloom_scene_is_dirty(const BloomScene *s) {
    if (!s) return false;
    return s->geom_dirty || s->state_dirty || s->ui_dirty || s->camera_dirty ||
           (s->timeline_playing && s->track_cnt > 0u);
}

bool bloom_scene_geom_dirty(const BloomScene *s) {
    return s ? s->geom_dirty : false;
}

void bloom_scene_mark_dirty(BloomScene *s) {
    if (s) { s->state_dirty = true; s->ui_dirty = true; }
}

void bloom_scene_clear_dirty(BloomScene *s) {
    if (s) {
        s->state_dirty = false;
        s->ui_dirty = false;
        s->camera_dirty = false;
    }
}

void bloom_scene_clear_geom_dirty(BloomScene *s) {
    if (s) s->geom_dirty = false;
}

/* ============================================================================
 * Asset Templates & Generational Object Management
 * ============================================================================ */

static void scene_clear_templates(BloomScene *s) {
    for (uint32_t i = 0u; i < s->tpl_cnt; ++i) {
        free(s->templates[i].hdrs);
        free(s->templates[i].verts);
        free(s->templates[i].local_idx);
    }
    s->tpl_cnt = 0u;
    s->geom_dirty = true;
}

uint16_t bloom_scene_add_template(BloomScene *s, const char *name, uint32_t meshlet_cnt,
                                  const BloomMeshletHeader32 *hdrs, const BloomVtx12 *verts,
                                  const uint8_t *local_idx, uint32_t idx_bytes) {
    if (!s || meshlet_cnt == 0u || !hdrs || !verts || !local_idx) return UINT16_MAX;
    if (!grow_array((void **)&s->templates, &s->tpl_cap, s->tpl_cnt + 1u, sizeof(SceneTemplate))) {
        return UINT16_MAX;
    }
    uint32_t vbase = 0u, ibase = 0u;
    if (s->tpl_cnt > 0u) {
        const SceneTemplate *prev = &s->templates[s->tpl_cnt - 1u];
        vbase = prev->gpu_vtx_base + prev->vtx_cnt;
        ibase = prev->gpu_idx_base + prev->idx_bytes;
    }
    uint16_t id = (uint16_t)s->tpl_cnt;
    SceneTemplate *t = &s->templates[s->tpl_cnt++];
    memset(t, 0, sizeof(*t));
    if (name) snprintf(t->name, sizeof(t->name), "%s", name);
    uint32_t max_vtx = 0u;
    for (uint32_t m = 0u; m < meshlet_cnt; ++m) {
        uint32_t end = hdrs[m].vtx_offset + hdrs[m].vtx_cnt;
        if (end > max_vtx) max_vtx = end;
    }
    t->meshlet_cnt = meshlet_cnt;
    t->vtx_cnt = max_vtx > 0u ? max_vtx : 1u;
    t->idx_bytes = idx_bytes;
    t->gpu_vtx_base = vbase;
    t->gpu_idx_base = ibase;
    t->hdrs = (BloomMeshletHeader32 *)malloc((size_t)meshlet_cnt * sizeof(BloomMeshletHeader32));
    t->verts = (BloomVtx12 *)malloc((size_t)t->vtx_cnt * sizeof(BloomVtx12));
    t->local_idx = (uint8_t *)malloc(idx_bytes ? idx_bytes : 1u);
    if (!t->hdrs || !t->verts || !t->local_idx) {
        free(t->hdrs); free(t->verts); free(t->local_idx);
        s->tpl_cnt--;
        return UINT16_MAX;
    }
    memcpy(t->hdrs, hdrs, (size_t)meshlet_cnt * sizeof(BloomMeshletHeader32));
    memcpy(t->verts, verts, (size_t)t->vtx_cnt * sizeof(BloomVtx12));
    memcpy(t->local_idx, local_idx, idx_bytes);
    s->geom_dirty = true;
    return id;
}

static uint32_t alloc_obj_slot(BloomScene *s) {
    if (s->free_obj_cnt > 0u) {
        return s->free_obj_slots[--s->free_obj_cnt];
    }
    for (uint32_t i = 1u; i < s->obj_cap; ++i) {
        if (!s->objs[i].alive && s->objs[i].gen == 0u) return i;
    }
    uint32_t old_cap = s->obj_cap;
    if (!grow_array((void **)&s->objs, &s->obj_cap, old_cap + 32u, sizeof(ObjectSlot))) return 0u;
    memset(s->objs + old_cap, 0, (size_t)(s->obj_cap - old_cap) * sizeof(ObjectSlot));
    return old_cap;
}

BloomHandle bloom_scene_spawn_object(BloomScene *s, const char *id, uint16_t template_id,
                                     const float pos[3], const float quat[4], const float scale[3],
                                     uint32_t rgba8_mat, const BloomConstraintSpec *cspec) {
    if (!s) return BLOOM_HANDLE_NULL;
    uint32_t idx = alloc_obj_slot(s);
    if (idx == 0u) return BLOOM_HANDLE_NULL;
    ObjectSlot *o = &s->objs[idx];
    uint16_t next_gen = (uint16_t)((o->gen + 1u) & 0xFFFu);
    if (next_gen == 0u) next_gen = 1u;
    memset(o, 0, sizeof(*o));
    o->alive = true;
    o->gen = next_gen;
    if (id) snprintf(o->id, sizeof(o->id), "%s", id);
    else snprintf(o->id, sizeof(o->id), "obj_%u_%u", idx, next_gen);
    o->key = bloom_fnv1a64(o->id);
    for (int k = 0; k < 3; ++k) {
        o->local_pos[k] = pos ? pos[k] : 0.0f;
        o->local_scale[k] = scale ? scale[k] : 1.0f;
    }
    if (quat) {
        for (int k = 0; k < 4; ++k) o->local_quat[k] = quat[k];
    } else {
        o->local_quat[0] = o->local_quat[1] = o->local_quat[2] = 0.0f;
        o->local_quat[3] = 1.0f;
    }
    o->rgba8 = rgba8_mat ? rgba8_mat : BLOOM_RGBA(0x40, 0xD8, 0xF0, 0xFF);
    o->local_clip[0] = 0.7071f;
    o->local_clip[2] = 0.7071f;
    if (cspec) {
        o->cspec = *cspec;
        o->cspec.template_id = template_id;
    } else {
        for (int k = 0; k < 3; ++k) {
            o->cspec.dof_min_f16[k] = bloom_f32_to_f16(-1.5f);
            o->cspec.dof_max_f16[k] = bloom_f32_to_f16(1.5f);
        }
        o->cspec.template_id = template_id;
        o->cspec.joint_mask = 0x07u;
    }
    s->state_dirty = true;
    return make_handle(idx, next_gen);
}

static ObjectSlot *get_obj_mut(BloomScene *s, BloomHandle h) {
    if (!s || h == BLOOM_HANDLE_NULL) return NULL;
    uint32_t idx = handle_index(h);
    if (idx == 0u || idx >= s->obj_cap) return NULL;
    ObjectSlot *o = &s->objs[idx];
    if (!o->alive || o->gen != handle_gen(h)) return NULL;
    return o;
}

static const ObjectSlot *get_obj_const(const BloomScene *s, BloomHandle h) {
    if (!s || h == BLOOM_HANDLE_NULL) return NULL;
    uint32_t idx = handle_index(h);
    if (idx == 0u || idx >= s->obj_cap) return NULL;
    const ObjectSlot *o = &s->objs[idx];
    if (!o->alive || o->gen != handle_gen(h)) return NULL;
    return o;
}

bool bloom_scene_is_handle_valid(const BloomScene *s, BloomHandle h) {
    return get_obj_const(s, h) != NULL;
}

bool bloom_scene_destroy_object(BloomScene *s, BloomHandle h) {
    ObjectSlot *o = get_obj_mut(s, h);
    if (!o) return false;
    uint32_t idx = handle_index(h);
    o->alive = false;
    for (uint32_t i = 1u; i < s->obj_cap; ++i) {
        if (s->objs[i].alive && s->objs[i].parent == h) s->objs[i].parent = BLOOM_HANDLE_NULL;
    }
    if (grow_array((void **)&s->free_obj_slots, &s->free_obj_cap, s->free_obj_cnt + 1u, sizeof(uint32_t))) {
        s->free_obj_slots[s->free_obj_cnt++] = idx;
    }
    s->state_dirty = true;
    return true;
}

BloomHandle bloom_scene_find_object_by_key(const BloomScene *s, uint64_t key) {
    if (!s) return BLOOM_HANDLE_NULL;
    for (uint32_t i = 1u; i < s->obj_cap; ++i) {
        if (s->objs[i].alive && s->objs[i].key == key) return make_handle(i, s->objs[i].gen);
    }
    return BLOOM_HANDLE_NULL;
}

bool bloom_scene_set_parent(BloomScene *s, BloomHandle child, BloomHandle parent) {
    ObjectSlot *c = get_obj_mut(s, child);
    if (!c) return false;
    if (parent != BLOOM_HANDLE_NULL && !get_obj_const(s, parent)) return false;
    c->parent = parent;
    s->state_dirty = true;
    return true;
}

bool bloom_scene_set_object_trs(BloomScene *s, BloomHandle h,
                                const float pos[3], const float quat[4], const float scale[3]) {
    ObjectSlot *o = get_obj_mut(s, h);
    if (!o) return false;
    if (pos) memcpy(o->local_pos, pos, 12u);
    if (quat) memcpy(o->local_quat, quat, 16u);
    if (scale) memcpy(o->local_scale, scale, 12u);
    s->state_dirty = true;
    return true;
}

bool bloom_scene_set_object_dof(BloomScene *s, BloomHandle h, uint8_t slot, float val) {
    ObjectSlot *o = get_obj_mut(s, h);
    if (!o || slot >= 3u) return false;
    float dmin = bloom_f16_to_f32(o->cspec.dof_min_f16[slot]);
    float dmax = bloom_f16_to_f32(o->cspec.dof_max_f16[slot]);
    float cval = clamp_f32(val, dmin, dmax);
    if (fabsf(o->local_dof[slot] - cval) > 1e-6f) {
        o->local_dof[slot] = cval;
        StateEntry *se = state_store_get_or_create(s, o->key, NULL);
        if (se) se->dof[slot] = cval;
        s->state_dirty = true;
    }
    return true;
}

bool bloom_scene_set_object_cutaway(BloomScene *s, BloomHandle h, bool enabled, const float plane_xyzd[4]) {
    ObjectSlot *o = get_obj_mut(s, h);
    if (!o) return false;
    if (enabled) o->flags |= BLOOM_OBJ_FLAG_CUTAWAY;
    else o->flags &= ~BLOOM_OBJ_FLAG_CUTAWAY;
    if (plane_xyzd) memcpy(o->local_clip, plane_xyzd, 16u);
    StateEntry *se = state_store_get_or_create(s, o->key, NULL);
    if (se) {
        se->has_clip = true;
        se->flags = o->flags;
        memcpy(se->clip_plane, o->local_clip, 16u);
    }
    s->state_dirty = true;
    return true;
}

bool bloom_scene_get_object_state(const BloomScene *s, BloomHandle h, BloomObjectState48 *out) {
    const ObjectSlot *o = get_obj_const(s, h);
    if (!o || !out) return false;
    *out = o->world_state;
    return true;
}

/* ============================================================================
 * Analytics, Hot-Reload State Preservation & Layer 0 Projection Bridge
 * ============================================================================ */

uint32_t bloom_scene_spawn_analytic(BloomScene *s, const char *id, const BloomAnalyticPrim32 *prim) {
    if (!s || !prim) return UINT32_MAX;
    if (!grow_array((void **)&s->analytics, &s->ana_cap, s->ana_cnt + 1u, sizeof(BloomAnalyticPrim32))) {
        return UINT32_MAX;
    }
    /* Keep analytic_keys capacity lockstepped with ana_cap */
    uint64_t *nk = (uint64_t *)realloc(s->analytic_keys, (size_t)s->ana_cap * sizeof(uint64_t));
    if (!nk) return UINT32_MAX;
    s->analytic_keys = nk;
    uint32_t idx = s->ana_cnt++;
    s->analytics[idx] = *prim;
    s->analytic_keys[idx] = bloom_fnv1a64(id ? id : "");
    s->state_dirty = true;
    return idx;
}

bool bloom_scene_update_analytic(BloomScene *s, uint32_t idx, const BloomAnalyticPrim32 *prim) {
    if (!s || !prim || idx >= s->ana_cnt) return false;
    s->analytics[idx] = *prim;
    s->state_dirty = true;
    return true;
}

bool bloom_scene_load_bobj(BloomScene *s, const BloomBobjDoc *doc, bool preserve_live_state) {
    if (!s || !doc) return false;
    /* Clear active object slots cleanly while optionally preserving StateStore */
    for (uint32_t i = 1u; i < s->obj_cap; ++i) s->objs[i].alive = false;
    s->free_obj_cnt = 0u;
    for (uint32_t i = 1u; i < s->obj_cap; ++i) {
        if (s->objs[i].gen > 0u) {
            if (grow_array((void **)&s->free_obj_slots, &s->free_obj_cap, s->free_obj_cnt + 1u, sizeof(uint32_t))) {
                s->free_obj_slots[s->free_obj_cnt++] = i;
            }
        }
    }
    if (doc->template_cnt > 0u && doc->templates[0].meshlet_cnt > 0u && doc->templates[0].verts) {
        scene_clear_templates(s);
        for (uint32_t t = 0u; t < doc->template_cnt; ++t) {
            const BloomBobjTemplate *bt = &doc->templates[t];
            if (bt->meshlet_cnt > 0u && bt->hdrs && bt->verts && bt->local_idx) {
                bloom_scene_add_template(s, bt->name, bt->meshlet_cnt, bt->hdrs, bt->verts,
                                         bt->local_idx, bt->idx_bytes);
            }
        }
    }
    for (uint32_t i = 0u; i < doc->object_cnt; ++i) {
        const BloomBobjObject *bo = &doc->objects[i];
        float q[4], sc[3];
        for (int k = 0; k < 4; ++k) q[k] = bloom_f16_to_f32(bo->state.quat_f16[k]);
        for (int k = 0; k < 3; ++k) sc[k] = bloom_f16_to_f32(bo->state.scale_f16[k]);
        BloomHandle h = bloom_scene_spawn_object(s, bo->id, bo->cspec.template_id,
                                                 bo->state.pos, q, sc, bo->state.rgba8_mat, &bo->cspec);
        ObjectSlot *os = get_obj_mut(s, h);
        if (!os) continue;
        for (int k = 0; k < 3; ++k) os->local_dof[k] = bloom_f16_to_f32(bo->state.dof_f16[k]);
        for (int k = 0; k < 4; ++k) os->local_clip[k] = bloom_f16_to_f32(bo->state.clip_plane_f16[k]);
        os->flags = bo->state.flags;
        if (preserve_live_state) {
            const StateEntry *se = state_store_find(s, os->key);
            if (se) {
                for (int k = 0; k < 3; ++k) {
                    float dmin = bloom_f16_to_f32(os->cspec.dof_min_f16[k]);
                    float dmax = bloom_f16_to_f32(os->cspec.dof_max_f16[k]);
                    os->local_dof[k] = clamp_f32(se->dof[k], dmin, dmax);
                }
                if (se->has_clip) {
                    memcpy(os->local_clip, se->clip_plane, 16u);
                    os->flags = se->flags;
                }
            }
        }
        StateEntry *init_se = state_store_get_or_create(s, os->key, NULL);
        if (init_se) {
            memcpy(init_se->dof, os->local_dof, 12u);
            memcpy(init_se->clip_plane, os->local_clip, 16u);
            init_se->flags = os->flags;
            init_se->has_clip = true;
        }
    }
    s->ana_cnt = 0u;
    for (uint32_t i = 0u; i < doc->analytic_cnt; ++i) {
        bloom_scene_spawn_analytic(s, doc->analytics[i].id, &doc->analytics[i].prim);
    }
    s->state_dirty = true;
    s->ui_dirty = true;
    return true;
}

void bloom_scene_apply_projections(BloomScene *s, const BloomStateProjection *projs, uint32_t count) {
    if (!s || !projs) return;
    bool any_changed = false;
    for (uint32_t p = 0u; p < count; ++p) {
        const BloomStateProjection *pr = &projs[p];
        BloomHandle h = bloom_scene_find_object_by_key(s, pr->node_key);
        ObjectSlot *o = get_obj_mut(s, h);
        if (!o) continue;
        if (pr->field_mask & BLOOM_PROJ_POS) {
            memcpy(o->local_pos, pr->pos, 12u);
            any_changed = true;
        }
        if (pr->field_mask & BLOOM_PROJ_QUAT) {
            memcpy(o->local_quat, pr->quat, 16u);
            any_changed = true;
        }
        if (pr->field_mask & BLOOM_PROJ_SCALE) {
            memcpy(o->local_scale, pr->scale, 12u);
            any_changed = true;
        }
        if (pr->field_mask & BLOOM_PROJ_DOF) {
            for (int k = 0; k < 3; ++k) {
                float dmin = bloom_f16_to_f32(o->cspec.dof_min_f16[k]);
                float dmax = bloom_f16_to_f32(o->cspec.dof_max_f16[k]);
                o->local_dof[k] = clamp_f32(pr->dof[k], dmin, dmax);
            }
            any_changed = true;
        }
        if (pr->field_mask & BLOOM_PROJ_COLOR) {
            o->rgba8 = pr->rgba8;
            any_changed = true;
        }
        if (pr->field_mask & BLOOM_PROJ_CLIP) {
            memcpy(o->local_clip, pr->clip_plane, 16u);
            any_changed = true;
        }
        if (pr->field_mask & BLOOM_PROJ_FLAGS) {
            uint32_t hi_mat = (pr->flags & UINT32_C(0xFFFF0000))
                                  ? (pr->flags & UINT32_C(0xFFFF0000))
                                  : (o->flags & UINT32_C(0xFFFF0000));
            uint32_t lo_flg = pr->flags & UINT32_C(0x0000FFFF);
            uint32_t mid = hi_mat >> 16u;
            if (mid < s->mat_cnt && (s->materials[mid].flags & BLOOM_MAT_FLAG_PBR)) {
                lo_flg |= BLOOM_OBJ_FLAG_PBR;
            }
            o->flags = hi_mat | lo_flg;
            any_changed = true;
        }
    }
    if (any_changed) s->state_dirty = true;
}

/* ============================================================================
 * Hierarchical Transform Propagation & GPU Buffer Staging
 * ============================================================================ */

static void sync_object_recursive(BloomScene *s, uint32_t idx, uint32_t depth,
                                  float out_p[3], float out_q[4], float out_sc[3]) {
    ObjectSlot *o = &s->objs[idx];
    if (o->synced) {
        memcpy(out_p, o->world_state.pos, 12u);
        for (int k = 0; k < 4; ++k) out_q[k] = bloom_f16_to_f32(o->world_state.quat_f16[k]);
        for (int k = 0; k < 3; ++k) out_sc[k] = bloom_f16_to_f32(o->world_state.scale_f16[k]);
        return;
    }
    const ObjectSlot *parent = (depth < 32u) ? get_obj_const(s, o->parent) : NULL;
    if (!parent) {
        memcpy(out_p, o->local_pos, 12u);
        memcpy(out_q, o->local_quat, 16u);
        memcpy(out_sc, o->local_scale, 12u);
    } else {
        float pp[3], pq[4], ps[3];
        sync_object_recursive(s, handle_index(o->parent), depth + 1u, pp, pq, ps);
        float scaled_local[3] = {
            o->local_pos[0] * ps[0],
            o->local_pos[1] * ps[1],
            o->local_pos[2] * ps[2]
        };
        float rot_p[3];
        quat_rotate_f32(pq, scaled_local, rot_p);
        for (int k = 0; k < 3; ++k) {
            out_p[k] = pp[k] + rot_p[k];
            out_sc[k] = ps[k] * o->local_scale[k];
        }
        quat_mul_f32(pq, o->local_quat, out_q);
    }
    memcpy(o->world_state.pos, out_p, 12u);
    for (int k = 0; k < 4; ++k) o->world_state.quat_f16[k] = bloom_f32_to_f16(out_q[k]);
    for (int k = 0; k < 3; ++k) o->world_state.scale_f16[k] = bloom_f32_to_f16(out_sc[k]);
    for (int k = 0; k < 3; ++k) o->world_state.dof_f16[k] = bloom_f32_to_f16(o->local_dof[k]);
    o->world_state.rgba8_mat = o->rgba8;
    for (int k = 0; k < 4; ++k) o->world_state.clip_plane_f16[k] = bloom_f32_to_f16(o->local_clip[k]);
    o->world_state.flags = o->flags;
    o->synced = true;
}

typedef struct {
    uint32_t g_idx;
    uint32_t obj_idx;
    uint8_t  tier;
    float    view_z;
} MeshSortKey;

static int cmp_mesh_sort_key(const void *pa, const void *pb) {
    const MeshSortKey *a = (const MeshSortKey *)pa;
    const MeshSortKey *b = (const MeshSortKey *)pb;
    if (a->tier != b->tier) return (a->tier < b->tier) ? -1 : 1;
    if (a->view_z < b->view_z) return -1;
    if (a->view_z > b->view_z) return 1;
    return (a->g_idx < b->g_idx) ? -1 : ((a->g_idx > b->g_idx) ? 1 : 0);
}

void bloom_scene_sync_transforms(BloomScene *s) {
    if (!s) return;
    for (uint32_t i = 1u; i < s->obj_cap; ++i) s->objs[i].synced = false;
    s->gpu_state_cnt = 0u;
    s->gpu_meshlet_cnt = 0u;
    for (uint32_t i = 1u; i < s->obj_cap; ++i) {
        if (!s->objs[i].alive) continue;
        float wp[3], wq[4], ws[3];
        sync_object_recursive(s, i, 0u, wp, wq, ws);
        if (s->objs[i].flags & BLOOM_OBJ_FLAG_HIDDEN) continue;
        if (!grow_array((void **)&s->gpu_states, &s->gpu_state_cap, s->gpu_state_cnt + 1u, sizeof(BloomObjectState48))) {
            return;
        }
        if (!grow_array((void **)&s->gpu_obj_slots, &s->gpu_obj_slot_cap, s->gpu_state_cnt + 1u, sizeof(uint32_t))) {
            return;
        }
        uint32_t g_idx = s->gpu_state_cnt++;
        s->gpu_states[g_idx] = s->objs[i].world_state;
        s->gpu_obj_slots[g_idx] = i;
    }
    if (s->gpu_state_cnt == 0u) return;

    MeshSortKey *order = (MeshSortKey *)malloc((size_t)s->gpu_state_cnt * sizeof(MeshSortKey));
    if (order) {
        float cy = cosf(s->camera.yaw), sy = sinf(s->camera.yaw);
        float cp = cosf(s->camera.pitch), sp = sinf(s->camera.pitch);
        float fx = sy * cp, fy = -sp, fz = cy * cp;
        for (uint32_t g_idx = 0u; g_idx < s->gpu_state_cnt; ++g_idx) {
            const BloomObjectState48 *st = &s->gpu_states[g_idx];
            float dx = st->pos[0] - s->camera.pos[0];
            float dy = st->pos[1] - s->camera.pos[1];
            float dz = st->pos[2] - s->camera.pos[2];
            float vz = dx * fx + dy * fy + dz * fz;
            float sx = fabsf(bloom_f16_to_f32(st->scale_f16[0]));
            float sy_s = fabsf(bloom_f16_to_f32(st->scale_f16[1]));
            float sz = fabsf(bloom_f16_to_f32(st->scale_f16[2]));
            float max_sc = sx > sy_s ? sx : sy_s;
            if (sz > max_sc) max_sc = sz;
            order[g_idx].g_idx = g_idx;
            order[g_idx].obj_idx = s->gpu_obj_slots[g_idx];
            order[g_idx].tier = (max_sc >= 8.0f) ? 1u : 0u;
            order[g_idx].view_z = vz;
        }
        qsort(order, s->gpu_state_cnt, sizeof(MeshSortKey), cmp_mesh_sort_key);
    }

    for (uint32_t k = 0u; k < s->gpu_state_cnt; ++k) {
        uint32_t g_idx = order ? order[k].g_idx : k;
        uint32_t obj_idx = order ? order[k].obj_idx : s->gpu_obj_slots[k];
        uint16_t tid = s->objs[obj_idx].cspec.template_id;
        if (tid < s->tpl_cnt) {
            const SceneTemplate *t = &s->templates[tid];
            if (!grow_array((void **)&s->gpu_meshlets, &s->gpu_meshlet_cap,
                            s->gpu_meshlet_cnt + t->meshlet_cnt, sizeof(BloomMeshletHeader32))) {
                free(order);
                return;
            }
            for (uint32_t m = 0u; m < t->meshlet_cnt; ++m) {
                BloomMeshletHeader32 h = t->hdrs[m];
                h.vtx_offset += t->gpu_vtx_base;
                h.idx_offset += t->gpu_idx_base;
                h.obj_id = (uint16_t)g_idx;
                h.flags |= (uint16_t)(s->objs[obj_idx].flags & BLOOM_OBJ_FLAG_CUTAWAY);
                s->gpu_meshlets[s->gpu_meshlet_cnt++] = h;
            }
        }
    }
    free(order);
}

uint32_t bloom_scene_get_template_vtx_count(const BloomScene *s) {
    if (!s || s->tpl_cnt == 0u) return 0u;
    const SceneTemplate *last = &s->templates[s->tpl_cnt - 1u];
    return last->gpu_vtx_base + last->vtx_cnt;
}

uint32_t bloom_scene_get_template_idx_bytes(const BloomScene *s) {
    if (!s || s->tpl_cnt == 0u) return 0u;
    const SceneTemplate *last = &s->templates[s->tpl_cnt - 1u];
    return last->gpu_idx_base + last->idx_bytes;
}

uint32_t bloom_scene_copy_template_geom(const BloomScene *s, BloomVtx12 *out_vtx, uint8_t *out_idx) {
    if (!s) return 0u;
    for (uint32_t i = 0u; i < s->tpl_cnt; ++i) {
        const SceneTemplate *t = &s->templates[i];
        if (out_vtx) memcpy(out_vtx + t->gpu_vtx_base, t->verts, (size_t)t->vtx_cnt * sizeof(BloomVtx12));
        if (out_idx) memcpy(out_idx + t->gpu_idx_base, t->local_idx, t->idx_bytes);
    }
    return bloom_scene_get_template_vtx_count(s);
}

uint32_t bloom_scene_get_gpu_states(const BloomScene *s, const BloomObjectState48 **out_ptr) {
    if (!s) return 0u;
    if (out_ptr) *out_ptr = s->gpu_states;
    return s->gpu_state_cnt;
}

uint32_t bloom_scene_get_gpu_meshlets(const BloomScene *s, const BloomMeshletHeader32 **out_ptr) {
    if (!s) return 0u;
    if (out_ptr) *out_ptr = s->gpu_meshlets;
    return s->gpu_meshlet_cnt;
}

uint32_t bloom_scene_get_analytics(const BloomScene *s, const BloomAnalyticPrim32 **out_ptr) {
    if (!s) return 0u;
    if (out_ptr) *out_ptr = s->analytics;
    return s->ana_cnt;
}

/* ============================================================================
 * Tri-Layer 2D UI, Flexbox Layout, VirtualList & Interactive Hit-Test
 * ============================================================================ */

void bloom_scene_set_text_measurer(BloomScene *s, BloomTextMeasureFn fn, void *userdata) {
    if (!s) return;
    s->text_measure_fn = fn;
    s->text_measure_ud = userdata;
    s->ui_dirty = true;
}

BloomHandle bloom_scene_add_ui(BloomScene *s, const BloomUiNodeDesc *desc) {
    if (!s || !desc) return BLOOM_HANDLE_NULL;
    if (!grow_array((void **)&s->uis, &s->ui_cap, s->ui_cnt + 1u, sizeof(UiSlot))) return BLOOM_HANDLE_NULL;
    uint32_t idx = s->ui_cnt++;
    UiSlot *u = &s->uis[idx];
    memset(u, 0, sizeof(*u));
    u->alive = true;
    u->gen = 1u;
    u->self = make_handle(idx, u->gen);
    u->desc = *desc;
    if (desc->id) snprintf(u->id_buf, sizeof(u->id_buf), "%s", desc->id);
    else snprintf(u->id_buf, sizeof(u->id_buf), "ui_%u", idx);
    if (desc->label) snprintf(u->label_buf, sizeof(u->label_buf), "%s", desc->label);
    if (desc->placeholder) snprintf(u->placeholder_buf, sizeof(u->placeholder_buf), "%s", desc->placeholder);
    if (desc->options_pipe) snprintf(u->options_buf, sizeof(u->options_buf), "%s", desc->options_pipe);
    u->desc.id = u->id_buf;
    u->desc.label = u->label_buf;
    u->desc.placeholder = u->placeholder_buf;
    u->desc.options_pipe = u->options_buf;
    if (u->desc.kind == BLOOM_UI_MODAL) u->desc.modal_trap = true;
    if (u->desc.action_mask == 0u && !u->desc.disabled) {
        if (desc->kind == BLOOM_UI_BUTTON || desc->kind == BLOOM_UI_CHECKBOX ||
            desc->kind == BLOOM_UI_SWITCH || desc->kind == BLOOM_UI_SELECT ||
            desc->kind == BLOOM_UI_TABS || desc->kind == BLOOM_UI_TEXT_INPUT) {
            u->desc.action_mask = BLOOM_ACT_CLICK | BLOOM_ACT_FOCUS;
        } else if (desc->kind == BLOOM_UI_SLIDER) {
            u->desc.action_mask = BLOOM_ACT_CLICK | BLOOM_ACT_DRAG | BLOOM_ACT_FOCUS;
        } else if (desc->kind == BLOOM_UI_VLIST || desc->kind == BLOOM_UI_SCROLLVIEW) {
            u->desc.action_mask = BLOOM_ACT_SCROLL | BLOOM_ACT_FOCUS;
        } else if (desc->kind == BLOOM_UI_MODAL || desc->kind == BLOOM_UI_PANEL) {
            u->desc.action_mask = BLOOM_ACT_FOCUS;
        }
    }
    u->key = bloom_fnv1a64(u->id_buf);
    u->slider_t = 0.5f;
    u->progress_t = clamp_f32(desc->progress_t, 0.0f, 1.0f);

    const StateEntry *se = state_store_find(s, u->key);
    if (se) {
        u->slider_t = se->slider_t;
        u->toggle_on = se->toggle_on;
        u->scroll_y = se->scroll_y;
        u->selected_idx = se->selected_idx;
        if (se->text_buf[0]) snprintf(u->text_buf, sizeof(u->text_buf), "%s", se->text_buf);
    } else if (desc->bind_obj_key != 0u && desc->kind == BLOOM_UI_SLIDER) {
        BloomHandle bh = bloom_scene_find_object_by_key(s, desc->bind_obj_key);
        const ObjectSlot *bo = get_obj_const(s, bh);
        if (bo && desc->bind_dof_slot < 3u) {
            float dmin = bloom_f16_to_f32(bo->cspec.dof_min_f16[desc->bind_dof_slot]);
            float dmax = bloom_f16_to_f32(bo->cspec.dof_max_f16[desc->bind_dof_slot]);
            if (dmax > dmin) {
                u->slider_t = clamp_f32((bo->local_dof[desc->bind_dof_slot] - dmin) / (dmax - dmin), 0.0f, 1.0f);
            }
        }
    }
    s->ui_dirty = true;
    return u->self;
}

static void destroy_ui_recursive(BloomScene *s, BloomHandle target) {
    for (uint32_t i = 1u; i < s->ui_cnt; ++i) {
        if (s->uis[i].alive && s->uis[i].desc.parent == target) {
            destroy_ui_recursive(s, s->uis[i].self);
        }
    }
    uint32_t idx = handle_index(target);
    if (idx > 0u && idx < s->ui_cnt && s->uis[idx].alive && s->uis[idx].gen == handle_gen(target)) {
        s->uis[idx].alive = false;
        if (s->captured_ui == target) s->captured_ui = BLOOM_HANDLE_NULL;
        if (s->hovered_ui == target) s->hovered_ui = BLOOM_HANDLE_NULL;
        if (s->focused_ui == target) s->focused_ui = BLOOM_HANDLE_NULL;
    }
}

bool bloom_scene_destroy_ui(BloomScene *s, BloomHandle ui) {
    if (!s || ui == BLOOM_HANDLE_NULL) return false;
    uint32_t idx = handle_index(ui);
    if (idx == 0u || idx >= s->ui_cnt || !s->uis[idx].alive || s->uis[idx].gen != handle_gen(ui)) {
        return false;
    }
    destroy_ui_recursive(s, ui);
    s->ui_dirty = true;
    return true;
}

void bloom_scene_clear_ui(BloomScene *s) {
    if (!s) return;
    s->ui_cnt = 1u;
    s->captured_ui = BLOOM_HANDLE_NULL;
    s->hovered_ui = BLOOM_HANDLE_NULL;
    s->focused_ui = BLOOM_HANDLE_NULL;
    s->ui_dirty = true;
}

static UiSlot *get_ui_mut(BloomScene *s, BloomHandle h) {
    if (!s || h == BLOOM_HANDLE_NULL) return NULL;
    uint32_t idx = handle_index(h);
    if (idx == 0u || idx >= s->ui_cnt) return NULL;
    if (!s->uis[idx].alive || s->uis[idx].gen != handle_gen(h)) return NULL;
    return &s->uis[idx];
}

bool bloom_scene_set_ui_label(BloomScene *s, BloomHandle ui, const char *label) {
    UiSlot *u = get_ui_mut(s, ui);
    if (!u || !label) return false;
    snprintf(u->label_buf, sizeof(u->label_buf), "%s", label);
    s->ui_dirty = true;
    return true;
}

bool bloom_scene_set_ui_scroll(BloomScene *s, BloomHandle ui, float scroll_y) {
    UiSlot *u = get_ui_mut(s, ui);
    if (!u) return false;
    float max_sc = 0.0f;
    if (u->desc.kind == BLOOM_UI_SCROLLVIEW) {
        max_sc = u->content_h - u->h;
        if (max_sc < 0.0f) max_sc = 0.0f;
    } else if (u->desc.vlist_total_rows > 0u && u->desc.vlist_row_h > 0.0f) {
        max_sc = (float)u->desc.vlist_total_rows * u->desc.vlist_row_h - u->h;
        if (max_sc < 0.0f) max_sc = 0.0f;
    }
    u->scroll_y = clamp_f32(scroll_y, 0.0f, max_sc);
    StateEntry *se = state_store_get_or_create(s, u->key, NULL);
    if (se) se->scroll_y = u->scroll_y;
    s->ui_dirty = true;
    return true;
}

float bloom_scene_get_ui_slider(const BloomScene *s, BloomHandle ui) {
    if (!s || ui == BLOOM_HANDLE_NULL) return 0.0f;
    uint32_t idx = handle_index(ui);
    if (idx == 0u || idx >= s->ui_cnt) return 0.0f;
    return s->uis[idx].slider_t;
}

bool bloom_scene_get_ui_toggle(const BloomScene *s, BloomHandle ui) {
    if (!s || ui == BLOOM_HANDLE_NULL) return false;
    uint32_t idx = handle_index(ui);
    if (idx == 0u || idx >= s->ui_cnt) return false;
    return s->uis[idx].toggle_on;
}

const char *bloom_scene_get_ui_text(const BloomScene *s, BloomHandle ui) {
    if (!s || ui == BLOOM_HANDLE_NULL) return "";
    uint32_t idx = handle_index(ui);
    if (idx == 0u || idx >= s->ui_cnt || !s->uis[idx].alive) return "";
    return s->uis[idx].text_buf;
}

bool bloom_scene_set_ui_text(BloomScene *s, BloomHandle ui, const char *utf8) {
    UiSlot *u = get_ui_mut(s, ui);
    if (!u) return false;
    snprintf(u->text_buf, sizeof(u->text_buf), "%s", utf8 ? utf8 : "");
    StateEntry *se = state_store_get_or_create(s, u->key, NULL);
    if (se) snprintf(se->text_buf, sizeof(se->text_buf), "%s", u->text_buf);
    s->ui_dirty = true;
    return true;
}

const char *bloom_scene_get_ui_preedit(const BloomScene *s, BloomHandle ui) {
    if (!s || ui == BLOOM_HANDLE_NULL) return "";
    uint32_t idx = handle_index(ui);
    if (idx == 0u || idx >= s->ui_cnt || !s->uis[idx].alive) return "";
    return s->uis[idx].preedit_buf;
}

int32_t bloom_scene_get_ui_selected(const BloomScene *s, BloomHandle ui) {
    if (!s || ui == BLOOM_HANDLE_NULL) return -1;
    uint32_t idx = handle_index(ui);
    if (idx == 0u || idx >= s->ui_cnt || !s->uis[idx].alive) return -1;
    return s->uis[idx].selected_idx;
}

bool bloom_scene_set_ui_selected(BloomScene *s, BloomHandle ui, int32_t sel) {
    UiSlot *u = get_ui_mut(s, ui);
    if (!u) return false;
    u->selected_idx = sel;
    StateEntry *se = state_store_get_or_create(s, u->key, NULL);
    if (se) se->selected_idx = sel;
    s->ui_dirty = true;
    return true;
}

bool bloom_scene_set_ui_progress(BloomScene *s, BloomHandle ui, float t) {
    UiSlot *u = get_ui_mut(s, ui);
    if (!u) return false;
    u->progress_t = clamp_f32(t, 0.0f, 1.0f);
    s->ui_dirty = true;
    return true;
}

static void emit_quad(BloomScene *s, float x, float y, float w, float h,
                      uint32_t fill, uint32_t border, float r_px, float bw_px,
                      uint16_t clip_id, uint16_t z_order,
                      uint16_t ui_slot, uint8_t part_id, uint32_t qflags) {
    if (w <= 0.5f || h <= 0.5f) return;
    BloomQuadHot q;
    q.bounds_q14_2[0] = f32_to_q14_2(x);
    q.bounds_q14_2[1] = f32_to_q14_2(y);
    q.bounds_q14_2[2] = f32_to_q14_2(w);
    q.bounds_q14_2[3] = f32_to_q14_2(h);
    q.rgba8_fill = fill;
    q.rgba8_border = border;
    uint8_t rq = f32_to_q6_2(r_px);
    uint8_t bq = f32_to_q6_2(bw_px);
    for (int k = 0; k < 4; ++k) { q.radii_q6_2[k] = rq; q.borders_q6_2[k] = bq; }
    q.clip_id = clip_id;
    q.style_id = ui_slot;
    q.z_flags = ((uint32_t)z_order & 0xFFFFu) | (((uint32_t)part_id & 0xFFu) << 16u) | qflags;

    bool is_opaque = ((fill >> 24u) == 0xFFu) && rq == 0u && bq == 0u && ((qflags & BLOOM_QUAD_FLAG_GROUND_ONLY) == 0u);
    if (is_opaque) {
        if (grow_array((void **)&s->opaque_quads, &s->opq_cap, s->opq_cnt + 1u, sizeof(BloomQuadHot))) {
            s->opaque_quads[s->opq_cnt++] = q;
        }
    } else if (((fill >> 24u) > 0u) || (bq > 0u && (border >> 24u) > 0u) || ((qflags & BLOOM_QUAD_FLAG_GROUND_ONLY) != 0u)) {
        if (grow_array((void **)&s->alpha_quads, &s->alp_cap, s->alp_cnt + 1u, sizeof(BloomQuadHot))) {
            s->alpha_quads[s->alp_cnt++] = q;
        }
    }
}

static void sort_quads_by_z(BloomQuadHot *q, uint32_t cnt) {
    for (uint32_t i = 1u; i < cnt; ++i) {
        BloomQuadHot key = q[i];
        uint32_t kz = key.z_flags & 0xFFFFu;
        uint32_t j = i;
        while (j > 0u && (q[j - 1u].z_flags & 0xFFFFu) > kz) {
            q[j] = q[j - 1u];
            --j;
        }
        q[j] = key;
    }
}

static void emit_shadow(BloomScene *s, float x, float y, float w, float h,
                        float r_px, float blur_px, uint32_t rgba8,
                        uint16_t clip_id, uint16_t z_order) {
    if (blur_px <= 0.25f || (rgba8 >> 24u) == 0u) return;
    if (!grow_array((void **)&s->shadows, &s->sh_cap, s->sh_cnt + 1u, sizeof(BloomShadowHot))) return;
    float pad = blur_px * 1.5f;
    BloomShadowHot sh;
    sh.bounds_q14_2[0] = f32_to_q14_2(x - pad);
    sh.bounds_q14_2[1] = f32_to_q14_2(y - pad);
    sh.bounds_q14_2[2] = f32_to_q14_2(w + 2.0f * pad);
    sh.bounds_q14_2[3] = f32_to_q14_2(h + 2.0f * pad);
    sh.shape_params[0] = bloom_f32_to_f16(w * 0.5f);
    sh.shape_params[1] = bloom_f32_to_f16(h * 0.5f);
    sh.shape_params[2] = bloom_f32_to_f16(0.707f / blur_px);
    sh.shape_params[3] = bloom_f32_to_f16(blur_px * 2.0f);
    sh.blur_r1_exp_sc[0] = bloom_f32_to_f16(r_px);
    sh.blur_r1_exp_sc[1] = bloom_f32_to_f16(2.0f);
    sh.blur_r1_exp_sc[2] = bloom_f32_to_f16(0.55f);
    sh.blur_r1_exp_sc[3] = 0u;
    sh.rgba8 = rgba8;
    sh.clip_id = clip_id;
    sh.z_flags = z_order;
    s->shadows[s->sh_cnt++] = sh;
}

static void emit_sprite(BloomScene *s, float x, float y, float w, float h,
                        const uint16_t uv[4], uint32_t tint, float rad_px,
                        uint8_t spr_flags, uint16_t tex_id, uint16_t clip_id,
                        uint16_t z_order, uint16_t ui_slot) {
    if (w <= 0.5f || h <= 0.5f) return;
    if (!grow_array((void **)&s->sprites, &s->sp_cap, s->sp_cnt + 1u, sizeof(BloomSpriteInst))) return;
    BloomSpriteInst sp;
    memset(&sp, 0, sizeof(sp));
    sp.dst_q14_2[0] = f32_to_q14_2(x);
    sp.dst_q14_2[1] = f32_to_q14_2(y);
    sp.dst_q14_2[2] = f32_to_q14_2(w);
    sp.dst_q14_2[3] = f32_to_q14_2(h);
    if (uv) memcpy(sp.uv_unorm16, uv, 8u);
    sp.tint_rgba8 = tint ? tint : 0xFFFFFFFFu;
    sp.tex_id = tex_id;
    sp.clip_id = clip_id;
    sp.z_flags = ((uint32_t)z_order & 0xFFFFu) |
                 ((uint32_t)f32_to_q6_2(rad_px) << 16u) |
                 ((uint32_t)spr_flags << 24u);
    sp.ui_slot = ui_slot;
    s->sprites[s->sp_cnt++] = sp;
}

static uint32_t split_options(const char *pipe, const char **ptrs, uint32_t *lens, uint32_t max_n) {
    if (!pipe || !pipe[0]) return 0u;
    uint32_t n = 0u;
    const char *p = pipe;
    while (*p && n < max_n) {
        const char *bar = p;
        while (*bar && *bar != '|') ++bar;
        ptrs[n] = p;
        lens[n] = (uint32_t)(bar - p);
        ++n;
        p = (*bar == '|') ? (bar + 1) : bar;
    }
    return n;
}

static void emit_text(BloomScene *s, uint64_t key, float x, float y, float w, float h,
                      float font_px, uint32_t rgba8, uint16_t clip_id, uint16_t z_order,
                      bool wrap, const char *txt) {
    if (!txt || !txt[0] || font_px <= 1.0f || (rgba8 >> 24u) == 0u) return;
    if (!grow_array((void **)&s->text_runs, &s->text_cap, s->text_cnt + 1u, sizeof(BloomTextRun))) return;
    BloomTextRun *tr = &s->text_runs[s->text_cnt++];
    tr->node_key = key;
    tr->x = x; tr->y = y; tr->w = w; tr->h = h;
    tr->font_px = font_px;
    tr->rgba8 = rgba8;
    tr->clip_id = clip_id;
    tr->z_order = z_order;
    tr->wrap = wrap;
    snprintf(tr->text, sizeof(tr->text), "%s", txt);
}

static float clamp_range_f32(float v, float mn, float mx) {
    if (mn > 0.0f && v < mn) v = mn;
    if (mx > 0.0f && v > mx) v = mx;
    return v;
}

static void layout_ui_recursive(BloomScene *s, uint32_t ui_idx, uint16_t clip_id, uint16_t z_base) {
    UiSlot *u = &s->uis[ui_idx];
    const BloomClipRect *cl = &s->clips[clip_id];
    u->clip_id = clip_id;
    u->clip_x = cl->x; u->clip_y = cl->y; u->clip_w = cl->w; u->clip_h = cl->h;
    u->z_order = z_base;

    emit_shadow(s, u->x, u->y, u->w, u->h, u->desc.radius_px, u->desc.shadow_blur_px,
                u->desc.shadow_rgba8, clip_id, z_base);

    uint32_t fill = u->desc.fill_rgba8;
    if (u->desc.kind == BLOOM_UI_BUTTON && u->toggle_on) fill = BLOOM_RGBA(0x2E, 0x6A, 0xA4, 0xFF);
    uint32_t qf = (u->desc.kind == BLOOM_UI_TEXT && (fill >> 24u) == 0u && (u->desc.border_rgba8 >> 24u) == 0u)
                      ? BLOOM_QUAD_FLAG_GROUND_ONLY : 0u;
    uint32_t bdr = u->desc.border_rgba8;
    float bw = u->desc.border_px;
    if (u->self == s->focused_ui && !u->desc.disabled && (bw <= 0.1f || u->desc.kind == BLOOM_UI_TEXT_INPUT)) {
        bdr = BLOOM_RGBA(0x50, 0xE0, 0xFF, 0xFF);
        bw = bw > 1.5f ? bw : 1.5f;
    }
    if (u->desc.kind == BLOOM_UI_SPRITE) {
        if ((fill >> 24u) > 0u || (bw > 0.1f && (bdr >> 24u) > 0u)) {
            emit_quad(s, u->x, u->y, u->w, u->h, fill, bdr,
                      u->desc.radius_px, bw, clip_id, (uint16_t)(z_base + 1u), (uint16_t)ui_idx, 0u, 0u);
        }
        emit_sprite(s, u->x, u->y, u->w, u->h, u->desc.sprite_uv_unorm16, u->desc.sprite_tint_rgba8,
                    u->desc.radius_px, u->desc.sprite_nearest ? 1u : 0u, 0u,
                    clip_id, (uint16_t)(z_base + 2u), (uint16_t)ui_idx);
    } else if (!(u->desc.kind == BLOOM_UI_PANEL && (fill >> 24u) == 0u && (bw <= 0.1f || (bdr >> 24u) == 0u))) {
        emit_quad(s, u->x, u->y, u->w, u->h, fill, bdr,
                  u->desc.radius_px, bw, clip_id, (uint16_t)(z_base + 1u), (uint16_t)ui_idx, 0u, qf);
    }

    float fpx = u->desc.font_px > 0.0f ? u->desc.font_px : 15.0f;
    uint32_t tcol = u->desc.text_rgba8 ? u->desc.text_rgba8 : BLOOM_RGBA(0xEA, 0xF2, 0xFC, 0xFF);

    if (u->desc.kind == BLOOM_UI_SLIDER) {
        float track_h = 8.0f, track_y = u->y + u->h * 0.55f, track_x = u->x + 12.0f, track_w = u->w - 24.0f;
        if (track_w > 4.0f) {
            emit_quad(s, track_x, track_y, track_w, track_h, BLOOM_RGBA(0x1A, 0x22, 0x30, 0xFF), 0u, 4.0f, 0.0f,
                      clip_id, (uint16_t)(z_base + 2u), (uint16_t)ui_idx, 1u, 0u);
            emit_quad(s, track_x, track_y, track_w * u->slider_t, track_h, BLOOM_RGBA(0x38, 0xD0, 0xFF, 0xFF), 0u, 4.0f, 0.0f,
                      clip_id, (uint16_t)(z_base + 3u), (uint16_t)ui_idx, 2u, 0u);
            float kx = track_x + track_w * u->slider_t - 7.0f;
            emit_quad(s, kx, track_y - 4.0f, 14.0f, 16.0f, BLOOM_RGBA(0xF4, 0xF8, 0xFF, 0xFF), BLOOM_RGBA(0x0A, 0x14, 0x20, 0xFF), 7.0f, 1.5f,
                      clip_id, (uint16_t)(z_base + 4u), (uint16_t)ui_idx, 3u, 0u);
        }
    } else if (u->desc.kind == BLOOM_UI_CHECKBOX) {
        float bs = 20.0f, bx = u->x + 8.0f, by = u->y + (u->h - bs) * 0.5f;
        uint32_t box_col = u->toggle_on ? BLOOM_RGBA(0x38, 0xB6, 0xFF, 0xFF) : BLOOM_RGBA(0x18, 0x21, 0x2F, 0xFF);
        emit_quad(s, bx, by, bs, bs, box_col, BLOOM_RGBA(0x4E, 0x6A, 0x8A, 0xFF), 4.0f, 1.5f,
                  clip_id, (uint16_t)(z_base + 2u), (uint16_t)ui_idx, 1u, 0u);
        if (u->toggle_on) {
            emit_quad(s, bx + 5.0f, by + 5.0f, bs - 10.0f, bs - 10.0f, BLOOM_RGBA(0xFA, 0xFC, 0xFF, 0xFF), 0u, 2.0f, 0.0f,
                      clip_id, (uint16_t)(z_base + 3u), (uint16_t)ui_idx, 2u, 0u);
        }
        if (u->label_buf[0]) {
            emit_text(s, u->key, bx + bs + 10.0f, u->y + (u->h - fpx * 1.2f) * 0.5f,
                      u->w - (bs + 24.0f), u->h, fpx, tcol, clip_id, (uint16_t)(z_base + 5u), false, u->label_buf);
        }
    } else if (u->desc.kind == BLOOM_UI_SWITCH) {
        float sw_w = 44.0f, sw_h = 24.0f, sx = u->x + 8.0f, sy = u->y + (u->h - sw_h) * 0.5f;
        uint32_t trk = u->toggle_on ? BLOOM_RGBA(0x2E, 0xC8, 0x7A, 0xFF) : BLOOM_RGBA(0x2B, 0x35, 0x45, 0xFF);
        emit_quad(s, sx, sy, sw_w, sw_h, trk, 0u, 12.0f, 0.0f,
                  clip_id, (uint16_t)(z_base + 2u), (uint16_t)ui_idx, 1u, 0u);
        float kx = u->toggle_on ? (sx + sw_w - 21.0f) : (sx + 3.0f);
        emit_quad(s, kx, sy + 3.0f, 18.0f, 18.0f, BLOOM_RGBA(0xFF, 0xFF, 0xFF, 0xFF), 0u, 9.0f, 0.0f,
                  clip_id, (uint16_t)(z_base + 3u), (uint16_t)ui_idx, 2u, 0u);
        if (u->label_buf[0]) {
            emit_text(s, u->key, sx + sw_w + 10.0f, u->y + (u->h - fpx * 1.2f) * 0.5f,
                      u->w - (sw_w + 24.0f), u->h, fpx, tcol, clip_id, (uint16_t)(z_base + 5u), false, u->label_buf);
        }
    } else if (u->desc.kind == BLOOM_UI_PROGRESS) {
        float pt = clamp_f32(u->progress_t, 0.0f, 1.0f);
        emit_quad(s, u->x, u->y, u->w * pt, u->h, BLOOM_RGBA(0x38, 0xD0, 0xFF, 0xFF), 0u,
                  u->desc.radius_px, 0.0f, clip_id, (uint16_t)(z_base + 2u), (uint16_t)ui_idx, 1u, 0u);
    } else if (u->desc.kind == BLOOM_UI_TEXT_INPUT) {
        float pad = u->desc.pad > 0.0f ? u->desc.pad : 10.0f;
        char disp[196];
        bool is_empty = (!u->text_buf[0] && !u->preedit_buf[0]);
        if (u->preedit_buf[0]) {
            snprintf(disp, sizeof(disp), "%s[%s]", u->text_buf, u->preedit_buf);
        } else if (u->text_buf[0]) {
            snprintf(disp, sizeof(disp), "%s%s", u->text_buf, (u->self == s->focused_ui) ? "|" : "");
        } else {
            snprintf(disp, sizeof(disp), "%s", u->placeholder_buf[0] ? u->placeholder_buf : "");
        }
        uint32_t col = is_empty ? BLOOM_RGBA(0x76, 0x86, 0x9C, 0xFF) : tcol;
        if (disp[0]) {
            emit_text(s, u->key, u->x + pad, u->y + (u->h - fpx * 1.2f) * 0.5f,
                      u->w - 2.0f * pad, u->h, fpx, col, clip_id, (uint16_t)(z_base + 5u), false, disp);
        }
    } else if (u->desc.kind == BLOOM_UI_TABS) {
        const char *ptrs[16]; uint32_t lens[16];
        uint32_t n = split_options(u->options_buf, ptrs, lens, 16u);
        if (n > 0u) {
            float tw = u->w / (float)n;
            for (uint32_t k = 0u; k < n; ++k) {
                bool sel = ((int32_t)k == u->selected_idx);
                uint32_t tc = sel ? BLOOM_RGBA(0x28, 0x4E, 0x7A, 0xFF) : 0u;
                float tx = u->x + (float)k * tw;
                if (sel) {
                    emit_quad(s, tx + 2.0f, u->y + 2.0f, tw - 4.0f, u->h - 4.0f, tc, 0u, 6.0f, 0.0f,
                              clip_id, (uint16_t)(z_base + 2u), (uint16_t)ui_idx, (uint8_t)(k + 1u), 0u);
                    emit_quad(s, tx + 8.0f, u->y + u->h - 3.0f, tw - 16.0f, 3.0f, BLOOM_RGBA(0x38, 0xD0, 0xFF, 0xFF), 0u,
                              1.5f, 0.0f, clip_id, (uint16_t)(z_base + 3u), (uint16_t)ui_idx, (uint8_t)(k + 1u), 0u);
                }
                char seg[64];
                uint32_t clen = lens[k] < 63u ? lens[k] : 63u;
                memcpy(seg, ptrs[k], clen);
                seg[clen] = '\0';
                emit_text(s, u->key ^ k, tx + 10.0f, u->y + (u->h - fpx * 1.2f) * 0.5f, tw - 20.0f, u->h,
                          fpx, tcol, clip_id, (uint16_t)(z_base + 5u), false, seg);
            }
        }
    } else if (u->desc.kind == BLOOM_UI_SELECT) {
        const char *ptrs[16]; uint32_t lens[16];
        uint32_t n = split_options(u->options_buf, ptrs, lens, 16u);
        char cur_lbl[80];
        if (n > 0u) {
            uint32_t si = (u->selected_idx >= 0 && (uint32_t)u->selected_idx < n) ? (uint32_t)u->selected_idx : 0u;
            uint32_t clen = lens[si] < 64u ? lens[si] : 64u;
            memcpy(cur_lbl, ptrs[si], clen);
            snprintf(cur_lbl + clen, sizeof(cur_lbl) - clen, "  v");
        } else {
            snprintf(cur_lbl, sizeof(cur_lbl), "%s v", u->label_buf);
        }
        emit_text(s, u->key, u->x + 12.0f, u->y + (u->h - fpx * 1.2f) * 0.5f, u->w - 24.0f, u->h,
                  fpx, tcol, clip_id, (uint16_t)(z_base + 5u), false, cur_lbl);
        if (u->dropdown_open && n > 0u) {
            float opt_h = u->h > 20.0f ? u->h : 34.0f;
            float base_y = u->y + u->h + 4.0f;
            emit_quad(s, u->x, base_y, u->w, opt_h * (float)n, BLOOM_RGBA(0x1B, 0x26, 0x38, 0xFF),
                      BLOOM_RGBA(0x40, 0x88, 0xC8, 0xFF), 6.0f, 1.0f, 0u, (uint16_t)(z_base + 200u), (uint16_t)ui_idx, 100u, 0u);
            for (uint32_t k = 0u; k < n; ++k) {
                float oy = base_y + (float)k * opt_h;
                if ((int32_t)k == u->selected_idx) {
                    emit_quad(s, u->x + 2.0f, oy + 2.0f, u->w - 4.0f, opt_h - 4.0f, BLOOM_RGBA(0x2A, 0x56, 0x86, 0xFF),
                              0u, 4.0f, 0.0f, 0u, (uint16_t)(z_base + 201u), (uint16_t)ui_idx, (uint8_t)(101u + k), 0u);
                }
                char seg[64];
                uint32_t clen = lens[k] < 63u ? lens[k] : 63u;
                memcpy(seg, ptrs[k], clen);
                seg[clen] = '\0';
                emit_text(s, u->key ^ (k + 0x100u), u->x + 12.0f, oy + (opt_h - fpx * 1.2f) * 0.5f,
                          u->w - 24.0f, opt_h, fpx, tcol, 0u, (uint16_t)(z_base + 205u), false, seg);
            }
        }
    }

    if (u->desc.kind == BLOOM_UI_VLIST && u->desc.vlist_total_rows > 0u && u->desc.vlist_row_h > 4.0f) {
        float rh = u->desc.vlist_row_h;
        uint32_t first_row = (uint32_t)(u->scroll_y / rh);
        float offset_y = -(u->scroll_y - (float)first_row * rh);
        uint32_t vis = (uint32_t)(u->h / rh) + 2u;
        if (grow_array((void **)&s->clips, &s->clip_cap, s->clip_cnt + 1u, sizeof(BloomClipRect))) {
            uint16_t vclip = (uint16_t)s->clip_cnt++;
            s->clips[vclip] = (BloomClipRect){u->x + 2.0f, u->y + 2.0f, u->w - 4.0f, u->h - 4.0f};
            for (uint32_t r = 0u; r < vis && first_row + r < u->desc.vlist_total_rows; ++r) {
                uint32_t row_id = first_row + r;
                float ry = u->y + offset_y + (float)r * rh + 2.0f;
                uint32_t rcol = (row_id & 1u) ? BLOOM_RGBA(0x1A, 0x22, 0x2E, 0xDD) : BLOOM_RGBA(0x21, 0x2B, 0x3A, 0xDD);
                emit_quad(s, u->x + 6.0f, ry, u->w - 12.0f, rh - 4.0f, rcol, 0u, 4.0f, 0.0f,
                          vclip, (uint16_t)(z_base + 2u), (uint16_t)ui_idx, (uint8_t)((row_id + 1u) & 0xFFu), 0u);
                char row_lbl[64];
                snprintf(row_lbl, sizeof(row_lbl), "#%04u  node_state_row", row_id);
                emit_text(s, u->key ^ row_id, u->x + 14.0f, ry + 5.0f, u->w - 28.0f, rh,
                          u->desc.font_px > 0.0f ? u->desc.font_px : 14.0f,
                          u->desc.text_rgba8 ? u->desc.text_rgba8 : BLOOM_RGBA(0xD0, 0xDC, 0xEC, 0xFF),
                          vclip, (uint16_t)(z_base + 3u), false, row_lbl);
            }
        }
    } else if (u->label_buf[0] &&
               u->desc.kind != BLOOM_UI_CHECKBOX && u->desc.kind != BLOOM_UI_SWITCH &&
               u->desc.kind != BLOOM_UI_TEXT_INPUT && u->desc.kind != BLOOM_UI_TABS &&
               u->desc.kind != BLOOM_UI_SELECT && u->desc.kind != BLOOM_UI_SPRITE) {
        bool exact_abs_text = (u->desc.kind == BLOOM_UI_TEXT && u->desc.pos_absolute);
        float pad = exact_abs_text ? u->desc.pad : (u->desc.pad > 0.0f ? u->desc.pad : 10.0f);
        float ly = exact_abs_text
                       ? (u->y + pad)
                       : ((u->desc.kind == BLOOM_UI_SLIDER)
                              ? (u->y + 6.0f)
                              : ((u->desc.kind == BLOOM_UI_BUTTON || u->desc.kind == BLOOM_UI_TOAST)
                                     ? (u->y + (u->h - fpx * 1.2f) * 0.5f)
                                     : (u->y + pad * 0.6f)));
        emit_text(s, u->key, u->x + pad, ly, u->w - 2.0f * pad, u->h,
                  fpx, tcol, clip_id, (uint16_t)(z_base + 5u), u->desc.text_wrap, u->label_buf);
    }

    uint32_t flow_stack[128], abs_stack[64];
    uint32_t flow_cnt = 0u, abs_cnt = 0u;
    for (uint32_t i = 1u; i < s->ui_cnt; ++i) {
        if (s->uis[i].alive && s->uis[i].desc.parent == u->self) {
            if (s->uis[i].desc.pos_absolute) ++abs_cnt;
            else ++flow_cnt;
        }
    }
    if (flow_cnt == 0u && abs_cnt == 0u) return;

    uint32_t *flow_heap = flow_cnt > 128u ? (uint32_t *)malloc((size_t)flow_cnt * sizeof(uint32_t)) : NULL;
    uint32_t *abs_heap = abs_cnt > 64u ? (uint32_t *)malloc((size_t)abs_cnt * sizeof(uint32_t)) : NULL;
    uint32_t *flow_idxs = flow_heap ? flow_heap : flow_stack;
    uint32_t *abs_idxs = abs_heap ? abs_heap : abs_stack;
    uint32_t flow_cap = flow_heap ? flow_cnt : 128u;
    uint32_t abs_cap = abs_heap ? abs_cnt : 64u;
    uint32_t fi = 0u, ai = 0u;
    for (uint32_t i = 1u; i < s->ui_cnt; ++i) {
        if (s->uis[i].alive && s->uis[i].desc.parent == u->self) {
            if (s->uis[i].desc.pos_absolute) {
                if (ai < abs_cap) abs_idxs[ai++] = i;
            } else if (fi < flow_cap) {
                flow_idxs[fi++] = i;
            }
        }
    }
    flow_cnt = fi;
    abs_cnt = ai;

    uint16_t child_clip = clip_id;
    if (grow_array((void **)&s->clips, &s->clip_cap, s->clip_cnt + 1u, sizeof(BloomClipRect))) {
        child_clip = (uint16_t)s->clip_cnt++;
        s->clips[child_clip].x = u->x > cl->x ? u->x : cl->x;
        s->clips[child_clip].y = u->y > cl->y ? u->y : cl->y;
        float rx = (u->x + u->w) < (cl->x + cl->w) ? (u->x + u->w) : (cl->x + cl->w);
        float by = (u->y + u->h) < (cl->y + cl->h) ? (u->y + u->h) : (cl->y + cl->h);
        s->clips[child_clip].w = rx > s->clips[child_clip].x ? (rx - s->clips[child_clip].x) : 0.0f;
        s->clips[child_clip].h = by > s->clips[child_clip].y ? (by - s->clips[child_clip].y) : 0.0f;
    }

    float pad = u->desc.pad;
    float hdr_h = ((u->desc.kind == BLOOM_UI_PANEL || u->desc.kind == BLOOM_UI_MODAL) && u->label_buf[0])
                      ? (fpx + 12.0f) : 0.0f;
    float gap = u->desc.gap;
    float inner_w = u->w - 2.0f * pad; if (inner_w < 0.0f) inner_w = 0.0f;
    float inner_h = u->h - 2.0f * pad - hdr_h; if (inner_h < 0.0f) inner_h = 0.0f;
    bool row = (u->desc.flex_dir == BLOOM_FLEX_ROW);

    for (uint32_t a = 0u; a < abs_cnt; ++a) {
        UiSlot *cu = &s->uis[abs_idxs[a]];
        cu->x = u->x + cu->desc.x;
        cu->y = u->y + cu->desc.y;
        float aw = cu->desc.pct_w > 0.0f ? (cu->desc.pct_w * inner_w) : (cu->desc.w > 0.0f ? cu->desc.w : inner_w);
        float ah = cu->desc.pct_h > 0.0f ? (cu->desc.pct_h * inner_h) : (cu->desc.h > 0.0f ? cu->desc.h : inner_h);
        cu->w = clamp_range_f32(aw, cu->desc.min_w, cu->desc.max_w);
        cu->h = clamp_range_f32(ah, cu->desc.min_h, cu->desc.max_h);
        uint32_t cz = (uint32_t)z_base + 8u + a * 8u;
        if (cz > 11000u) cz = 11000u;
        layout_ui_recursive(s, abs_idxs[a], child_clip, (uint16_t)cz);
    }
    if (flow_cnt == 0u) {
        free(flow_heap);
        free(abs_heap);
        return;
    }

    float bw_stack[128], bh_stack[128], w_stack[128];
    float *metrics_heap = flow_cnt > 128u ? (float *)malloc((size_t)flow_cnt * 3u * sizeof(float)) : NULL;
    float *base_w = metrics_heap ? metrics_heap : bw_stack;
    float *base_h = metrics_heap ? (metrics_heap + flow_cnt) : bh_stack;
    float *weights = metrics_heap ? (metrics_heap + flow_cnt * 2u) : w_stack;
    for (uint32_t c = 0u; c < flow_cnt; ++c) {
        const UiSlot *cu = &s->uis[flow_idxs[c]];
        float cw = cu->desc.pct_w > 0.0f ? (cu->desc.pct_w * inner_w) : cu->desc.w;
        float ch = cu->desc.pct_h > 0.0f ? (cu->desc.pct_h * inner_h) : cu->desc.h;
        if ((cw <= 0.0f || ch <= 0.0f) && s->text_measure_fn && cu->label_buf[0] &&
            (cu->desc.kind == BLOOM_UI_TEXT || (cu->desc.kind == BLOOM_UI_BUTTON && cw <= 0.0f && ch <= 0.0f))) {
            float mw = 0.0f, mh = 0.0f;
            float cfpx = cu->desc.font_px > 0.0f ? cu->desc.font_px : 15.0f;
            s->text_measure_fn(s->text_measure_ud, cu->label_buf, cfpx, cw > 0.0f ? cw : inner_w, &mw, &mh);
            if (cw <= 0.0f) cw = mw + 20.0f;
            if (ch <= 0.0f) ch = mh + 12.0f;
        }
        base_w[c] = cw > 0.0f ? clamp_range_f32(cw, cu->desc.min_w, cu->desc.max_w) : 0.0f;
        base_h[c] = ch > 0.0f ? clamp_range_f32(ch, cu->desc.min_h, cu->desc.max_h) : 0.0f;
        weights[c] = (cu->desc.flex_grow > 0.0f) ? cu->desc.flex_grow : ((row ? base_w[c] : base_h[c]) <= 0.0f ? 1.0f : 0.0f);
    }

    float max_cap = row ? inner_w : inner_h;
    float cross_cursor = row ? (u->y + pad + hdr_h) : (u->x + pad);
    if (u->desc.kind == BLOOM_UI_SCROLLVIEW && !row) cross_cursor = u->x + pad;
    float scroll_off = (u->desc.kind == BLOOM_UI_SCROLLVIEW) ? u->scroll_y : 0.0f;
    float content_end = u->y + pad + hdr_h;

    uint32_t ls = 0u;
    while (ls < flow_cnt) {
        uint32_t le = ls + 1u;
        float line_fixed = row ? base_w[ls] : base_h[ls];
        while (u->desc.flex_wrap && le < flow_cnt) {
            float next_p = row ? base_w[le] : base_h[le];
            if (line_fixed + gap + next_p > max_cap + 0.5f) break;
            line_fixed += gap + next_p;
            ++le;
        }
        if (!u->desc.flex_wrap) le = flow_cnt;

        float fixed_sum = gap * (float)(le - ls > 1u ? le - ls - 1u : 0u);
        float w_sum = 0.0f, cross_max = 0.0f;
        for (uint32_t c = ls; c < le; ++c) {
            float pr = row ? base_w[c] : base_h[c];
            float cr = row ? base_h[c] : base_w[c];
            if (weights[c] > 0.0f && pr <= 0.0f) w_sum += weights[c];
            else if (weights[c] > 0.0f) { fixed_sum += pr; w_sum += weights[c]; }
            else fixed_sum += pr;
            if (cr > cross_max) cross_max = cr;
        }
        float line_cross = (!u->desc.flex_wrap || cross_max <= 0.0f) ? (row ? inner_h : inner_w) : cross_max;
        float rem = max_cap - fixed_sum;
        float free_rem = (w_sum > 0.0f) ? 0.0f : (rem > 0.0f ? rem : 0.0f);

        float cur = row ? (u->x + pad) : (u->y + pad + hdr_h);
        float eff_gap = gap;
        uint32_t lcnt = le - ls;
        if (free_rem > 0.0f) {
            if (u->desc.justify == BLOOM_JUSTIFY_CENTER) cur += free_rem * 0.5f;
            else if (u->desc.justify == BLOOM_JUSTIFY_END) cur += free_rem;
            else if (u->desc.justify == BLOOM_JUSTIFY_SPACE_BETWEEN && lcnt > 1u) eff_gap += free_rem / (float)(lcnt - 1u);
            else if (u->desc.justify == BLOOM_JUSTIFY_SPACE_AROUND && lcnt > 0u) {
                float sp = free_rem / (float)lcnt;
                cur += sp * 0.5f;
                eff_gap += sp;
            }
        }

        for (uint32_t c = ls; c < le; ++c) {
            UiSlot *cu = &s->uis[flow_idxs[c]];
            float add = (w_sum > 0.0f && rem > 0.0f && weights[c] > 0.0f) ? (rem * (weights[c] / w_sum)) : 0.0f;
            float cw = row ? clamp_range_f32(base_w[c] + add, cu->desc.min_w, cu->desc.max_w)
                           : (base_w[c] > 0.0f ? base_w[c] : (u->desc.align == BLOOM_ALIGN_STRETCH ? line_cross : line_cross));
            float ch = row ? (base_h[c] > 0.0f ? base_h[c] : (u->desc.align == BLOOM_ALIGN_STRETCH ? line_cross : line_cross))
                           : clamp_range_f32(base_h[c] + add, cu->desc.min_h, cu->desc.max_h);
            float cx_off = 0.0f;
            if (u->desc.align == BLOOM_ALIGN_CENTER) cx_off = (line_cross - (row ? ch : cw)) * 0.5f;
            else if (u->desc.align == BLOOM_ALIGN_END) cx_off = line_cross - (row ? ch : cw);

            cu->w = cw > 0.0f ? cw : 0.0f;
            cu->h = ch > 0.0f ? ch : 0.0f;
            if (row) {
                cu->x = cur;
                cu->y = cross_cursor + cx_off - scroll_off;
                cur += cu->w + eff_gap;
            } else {
                cu->x = cross_cursor + cx_off;
                cu->y = cur - scroll_off;
                cur += cu->h + eff_gap;
            }
            float unshifted_b = (row ? cross_cursor + cx_off : (cur - eff_gap)) + cu->h;
            if (unshifted_b > content_end) content_end = unshifted_b;
            layout_ui_recursive(s, flow_idxs[c], child_clip, (uint16_t)(z_base + 10u + c * 10u));
        }
        cross_cursor += line_cross + gap;
        ls = le;
    }

    u->content_h = content_end - (u->y + pad + hdr_h) + 2.0f * pad + hdr_h;
    if (u->desc.kind == BLOOM_UI_SCROLLVIEW && u->content_h > u->h + 2.0f) {
        float ratio = u->h / u->content_h;
        float th = clamp_f32(u->h * ratio, 24.0f, u->h - 8.0f);
        float max_sc = u->content_h - u->h;
        float ty = u->y + 4.0f + (max_sc > 1.0f ? (u->scroll_y / max_sc) * (u->h - 8.0f - th) : 0.0f);
        emit_quad(s, u->x + u->w - 8.0f, ty, 5.0f, th, BLOOM_RGBA(0x5A, 0x80, 0xA8, 0xCC), 0u,
                  2.5f, 0.0f, child_clip, (uint16_t)(z_base + 40u), (uint16_t)ui_idx, 99u, 0u);
    }
    free(metrics_heap);
    free(flow_heap);
    free(abs_heap);
}

void bloom_scene_layout_ui(BloomScene *s, float vp_w, float vp_h) {
    if (!s) return;
    s->opq_cnt = 0u;
    s->alp_cnt = 0u;
    s->sh_cnt = 0u;
    s->clip_cnt = 0u;
    s->text_cnt = 0u;
    s->sp_cnt = 0u;

    if (!grow_array((void **)&s->clips, &s->clip_cap, 16u, sizeof(BloomClipRect))) return;
    s->clips[0] = (BloomClipRect){0.0f, 0.0f, vp_w, vp_h};
    s->clip_cnt = 1u;

    uint16_t z_acc = 10u;
    for (uint32_t i = 1u; i < s->ui_cnt; ++i) {
        if (!s->uis[i].alive || s->uis[i].desc.parent != BLOOM_HANDLE_NULL) continue;
        UiSlot *root = &s->uis[i];
        root->x = root->desc.x;
        root->y = root->desc.y;
        root->w = root->desc.w > 0.0f ? root->desc.w : (vp_w - root->x);
        root->h = root->desc.h > 0.0f ? root->desc.h : (vp_h - root->y);
        layout_ui_recursive(s, i, 0u, z_acc);
        z_acc += 500u;
    }
    sort_quads_by_z(s->opaque_quads, s->opq_cnt);
    sort_quads_by_z(s->alpha_quads, s->alp_cnt);
}

static bool point_in_ui(const UiSlot *u, float x, float y) {
    if (x < u->clip_x || x > u->clip_x + u->clip_w) return false;
    if (y < u->clip_y || y > u->clip_y + u->clip_h) return false;
    return x >= u->x && x <= u->x + u->w && y >= u->y && y <= u->y + u->h;
}

static void apply_ui_binding(BloomScene *s, UiSlot *u) {
    StateEntry *se = state_store_get_or_create(s, u->key, NULL);
    if (se) {
        se->slider_t = u->slider_t;
        se->toggle_on = u->toggle_on;
        se->selected_idx = u->selected_idx;
    }
    if (u->desc.bind_obj_key == 0u) return;
    BloomHandle oh = bloom_scene_find_object_by_key(s, u->desc.bind_obj_key);
    ObjectSlot *bo = get_obj_mut(s, oh);
    if (!bo) return;

    if (u->desc.kind == BLOOM_UI_SLIDER) {
        if (u->desc.bind_dof_slot < 3u) {
            float dmin = bloom_f16_to_f32(bo->cspec.dof_min_f16[u->desc.bind_dof_slot]);
            float dmax = bloom_f16_to_f32(bo->cspec.dof_max_f16[u->desc.bind_dof_slot]);
            bloom_scene_set_object_dof(s, oh, u->desc.bind_dof_slot, dmin + u->slider_t * (dmax - dmin));
        } else if (u->desc.bind_dof_slot == 3u) {
            float p[4];
            memcpy(p, bo->local_clip, 16u);
            p[3] = -1.0f + u->slider_t * 2.0f;
            bloom_scene_set_object_cutaway(s, oh, (bo->flags & BLOOM_OBJ_FLAG_CUTAWAY) != 0u, p);
        }
    } else if (u->desc.kind == BLOOM_UI_BUTTON || u->desc.kind == BLOOM_UI_CHECKBOX || u->desc.kind == BLOOM_UI_SWITCH) {
        bloom_scene_set_object_cutaway(s, oh, u->toggle_on, bo->local_clip);
    }
}

static uint16_t active_modal_min_z(const BloomScene *s) {
    uint16_t mz = 0u;
    for (uint32_t i = 1u; i < s->ui_cnt; ++i) {
        const UiSlot *u = &s->uis[i];
        if (u->alive && (u->desc.kind == BLOOM_UI_MODAL || u->desc.modal_trap) && u->z_order > mz) {
            mz = u->z_order;
        }
    }
    return mz;
}

bool bloom_scene_set_ui_focused(BloomScene *s, BloomHandle ui) {
    if (!s) return false;
    if (ui == BLOOM_HANDLE_NULL) { s->focused_ui = BLOOM_HANDLE_NULL; s->ui_dirty = true; return true; }
    const UiSlot *u = get_ui_mut(s, ui);
    if (!u || u->desc.disabled || u->z_order < active_modal_min_z(s)) return false;
    s->focused_ui = u->self;
    s->ui_dirty = true;
    return true;
}

BloomHandle bloom_scene_get_focused_ui(const BloomScene *s) {
    return s ? s->focused_ui : BLOOM_HANDLE_NULL;
}

bool bloom_scene_handle_input(BloomScene *s, const BloomInputEvent *ev) {
    if (!s || !ev) return false;
    bool consumed = false;
    uint16_t modal_z = active_modal_min_z(s);

    if (ev->type == BLOOM_EVENT_POINTER_DOWN) {
        s->pointer_down = true;
        s->last_ptr_x = ev->x;
        s->last_ptr_y = ev->y;

        /* Check open Select dropdown overlay first */
        for (uint32_t i = 1u; i < s->ui_cnt; ++i) {
            UiSlot *u = &s->uis[i];
            if (!u->alive || u->desc.disabled || u->z_order < modal_z ||
                u->desc.kind != BLOOM_UI_SELECT || !u->dropdown_open) continue;
            const char *ptrs[16]; uint32_t lens[16];
            uint32_t n = split_options(u->options_buf, ptrs, lens, 16u);
            float opt_h = u->h > 20.0f ? u->h : 34.0f;
            float base_y = u->y + u->h + 4.0f;
            if (n > 0u && ev->x >= u->x && ev->x <= u->x + u->w &&
                ev->y >= base_y && ev->y < base_y + opt_h * (float)n) {
                int32_t k = (int32_t)((ev->y - base_y) / opt_h);
                if (k >= 0 && (uint32_t)k < n) u->selected_idx = k;
                u->dropdown_open = false;
                apply_ui_binding(s, u);
                s->ui_dirty = true;
                return true;
            }
        }

        uint32_t best_i = 0u;
        uint16_t best_z = 0u;
        for (uint32_t i = 1u; i < s->ui_cnt; ++i) {
            const UiSlot *u = &s->uis[i];
            if (!u->alive || u->desc.disabled || u->z_order < modal_z) continue;
            BloomUiKind k = u->desc.kind;
            bool clickable = (k == BLOOM_UI_BUTTON || k == BLOOM_UI_SLIDER || k == BLOOM_UI_MODAL ||
                              k == BLOOM_UI_CHECKBOX || k == BLOOM_UI_SWITCH || k == BLOOM_UI_SELECT ||
                              k == BLOOM_UI_TABS || k == BLOOM_UI_TEXT_INPUT);
            if (clickable && point_in_ui(u, ev->x, ev->y) && u->z_order >= best_z) {
                best_z = u->z_order;
                best_i = i;
            }
        }
        for (uint32_t i = 1u; i < s->ui_cnt; ++i) {
            if (s->uis[i].alive && s->uis[i].desc.kind == BLOOM_UI_SELECT && i != best_i) {
                if (s->uis[i].dropdown_open) { s->uis[i].dropdown_open = false; s->ui_dirty = true; }
            }
        }
        if (best_i > 0u) {
            UiSlot *u = &s->uis[best_i];
            s->focused_ui = u->self;
            if (u->desc.kind == BLOOM_UI_BUTTON || u->desc.kind == BLOOM_UI_CHECKBOX || u->desc.kind == BLOOM_UI_SWITCH) {
                s->captured_ui = u->self;
                u->toggle_on = !u->toggle_on;
                apply_ui_binding(s, u);
            } else if (u->desc.kind == BLOOM_UI_SLIDER) {
                s->captured_ui = u->self;
                float tx = u->x + 12.0f, tw = u->w - 24.0f;
                u->slider_t = tw > 1.0f ? clamp_f32((ev->x - tx) / tw, 0.0f, 1.0f) : 0.5f;
                apply_ui_binding(s, u);
            } else if (u->desc.kind == BLOOM_UI_SELECT) {
                u->dropdown_open = !u->dropdown_open;
            } else if (u->desc.kind == BLOOM_UI_TABS) {
                const char *ptrs[16]; uint32_t lens[16];
                uint32_t n = split_options(u->options_buf, ptrs, lens, 16u);
                if (n > 0u && u->w > 1.0f) {
                    int32_t k = (int32_t)(((ev->x - u->x) / u->w) * (float)n);
                    if (k < 0) k = 0;
                    if ((uint32_t)k >= n) k = (int32_t)(n - 1u);
                    u->selected_idx = k;
                    apply_ui_binding(s, u);
                }
            }
            s->ui_dirty = true;
            consumed = true;
        }
    } else if (ev->type == BLOOM_EVENT_POINTER_MOVE) {
        if (s->pointer_down && s->captured_ui != BLOOM_HANDLE_NULL) {
            UiSlot *u = get_ui_mut(s, s->captured_ui);
            if (u && !u->desc.disabled && u->desc.kind == BLOOM_UI_SLIDER) {
                float tx = u->x + 12.0f, tw = u->w - 24.0f;
                u->slider_t = tw > 1.0f ? clamp_f32((ev->x - tx) / tw, 0.0f, 1.0f) : 0.5f;
                apply_ui_binding(s, u);
                s->ui_dirty = true;
                consumed = true;
            }
        } else if (s->pointer_down && s->captured_ui == BLOOM_HANDLE_NULL && modal_z == 0u) {
            s->camera.yaw += (ev->x - s->last_ptr_x) * 0.005f;
            s->camera.pitch = clamp_f32(s->camera.pitch + (ev->y - s->last_ptr_y) * 0.004f, -1.2f, 1.2f);
            s->camera_dirty = true;
        } else if (!s->pointer_down) {
            uint32_t hi = 0u; uint16_t hz = 0u;
            for (uint32_t i = 1u; i < s->ui_cnt; ++i) {
                const UiSlot *u = &s->uis[i];
                if (!u->alive || u->desc.disabled || u->z_order < modal_z) continue;
                if (point_in_ui(u, ev->x, ev->y) && u->z_order >= hz) { hz = u->z_order; hi = i; }
            }
            s->hovered_ui = hi ? s->uis[hi].self : BLOOM_HANDLE_NULL;
        }
        s->last_ptr_x = ev->x;
        s->last_ptr_y = ev->y;
    } else if (ev->type == BLOOM_EVENT_POINTER_UP) {
        s->pointer_down = false;
        s->captured_ui = BLOOM_HANDLE_NULL;
    } else if (ev->type == BLOOM_EVENT_SCROLL) {
        bool hit_scroll = false;
        for (uint32_t i = s->ui_cnt; i > 1u; --i) {
            UiSlot *u = &s->uis[i - 1u];
            if (u->alive && !u->desc.disabled && u->z_order >= modal_z &&
                (u->desc.kind == BLOOM_UI_VLIST || u->desc.kind == BLOOM_UI_SCROLLVIEW) &&
                point_in_ui(u, ev->x, ev->y)) {
                bloom_scene_set_ui_scroll(s, u->self, u->scroll_y - ev->dy * 16.0f);
                hit_scroll = true;
                consumed = true;
                break;
            }
        }
        if (!hit_scroll && modal_z == 0u) {
            s->camera.pos[2] += ev->dy * 0.25f;
            s->camera_dirty = true;
        }
    } else if (ev->type == BLOOM_EVENT_IME_PREEDIT) {
        UiSlot *u = get_ui_mut(s, s->focused_ui);
        if (u && !u->desc.disabled && u->desc.kind == BLOOM_UI_TEXT_INPUT && u->z_order >= modal_z) {
            snprintf(u->preedit_buf, sizeof(u->preedit_buf), "%s", ev->text);
            s->ui_dirty = true;
            consumed = true;
        }
    } else if (ev->type == BLOOM_EVENT_TEXT_INPUT) {
        UiSlot *u = get_ui_mut(s, s->focused_ui);
        if (u && !u->desc.disabled && u->desc.kind == BLOOM_UI_TEXT_INPUT && u->z_order >= modal_z) {
            u->preedit_buf[0] = '\0';
            size_t cur_len = strlen(u->text_buf);
            if (cur_len + 1u < sizeof(u->text_buf)) {
                snprintf(u->text_buf + cur_len, sizeof(u->text_buf) - cur_len, "%s", ev->text);
            }
            StateEntry *se = state_store_get_or_create(s, u->key, NULL);
            if (se) snprintf(se->text_buf, sizeof(se->text_buf), "%s", u->text_buf);
            s->ui_dirty = true;
            consumed = true;
        }
    } else if (ev->type == BLOOM_EVENT_KEY_DOWN) {
        UiSlot *fu = get_ui_mut(s, s->focused_ui);
        if (fu && !fu->desc.disabled && fu->desc.kind == BLOOM_UI_TEXT_INPUT && fu->z_order >= modal_z &&
            (ev->key_code == 51u || ev->key_code == 8u)) {
            if (fu->preedit_buf[0]) {
                fu->preedit_buf[0] = '\0';
            } else {
                size_t len = strlen(fu->text_buf);
                while (len > 0u && (((uint8_t)fu->text_buf[len - 1u] & 0xC0u) == 0x80u)) --len;
                if (len > 0u) --len;
                fu->text_buf[len] = '\0';
                StateEntry *se = state_store_get_or_create(s, fu->key, NULL);
                if (se) snprintf(se->text_buf, sizeof(se->text_buf), "%s", fu->text_buf);
            }
            s->ui_dirty = true;
            consumed = true;
        } else if ((ev->key_code == 48u || ev->key_code == 9u) && s->ui_cnt > 1u) {
            uint32_t cur = (s->focused_ui != BLOOM_HANDLE_NULL) ? handle_index(s->focused_ui) : 0u;
            for (uint32_t step = 1u; step < s->ui_cnt; ++step) {
                uint32_t idx = ((cur + step - 1u) % (s->ui_cnt - 1u)) + 1u;
                const UiSlot *u = &s->uis[idx];
                if (!u->alive || u->desc.disabled || u->z_order < modal_z) continue;
                if ((u->desc.action_mask & (BLOOM_ACT_CLICK | BLOOM_ACT_DRAG | BLOOM_ACT_SCROLL)) != 0u) {
                    s->focused_ui = u->self;
                    s->ui_dirty = true;
                    consumed = true;
                    break;
                }
            }
        }
    } else if (ev->type == BLOOM_EVENT_RESIZE) {
        s->ui_dirty = true;
        s->camera_dirty = true;
    }
    return consumed;
}

uint32_t bloom_scene_get_opaque_quads(const BloomScene *s, const BloomQuadHot **out_ptr) {
    if (!s) return 0u;
    if (out_ptr) *out_ptr = s->opaque_quads;
    return s->opq_cnt;
}

uint32_t bloom_scene_get_alpha_quads(const BloomScene *s, const BloomQuadHot **out_ptr) {
    if (!s) return 0u;
    if (out_ptr) *out_ptr = s->alpha_quads;
    return s->alp_cnt;
}

uint32_t bloom_scene_get_shadows(const BloomScene *s, const BloomShadowHot **out_ptr) {
    if (!s) return 0u;
    if (out_ptr) *out_ptr = s->shadows;
    return s->sh_cnt;
}

uint32_t bloom_scene_get_clips(const BloomScene *s, const BloomClipRect **out_ptr) {
    if (!s) return 0u;
    if (out_ptr) *out_ptr = s->clips;
    return s->clip_cnt;
}

uint32_t bloom_scene_get_text_runs(const BloomScene *s, const BloomTextRun **out_ptr) {
    if (!s) return 0u;
    if (out_ptr) *out_ptr = s->text_runs;
    return s->text_cnt;
}

uint32_t bloom_scene_get_sprites(const BloomScene *s, const BloomSpriteInst **out_ptr) {
    if (!s) return 0u;
    if (out_ptr) *out_ptr = s->sprites;
    return s->sp_cnt;
}

uint16_t bloom_scene_add_material(BloomScene *s, const BloomMaterial32 *mat) {
    if (!s || !mat) return 0u;
    if (!grow_array((void **)&s->materials, &s->mat_cap, s->mat_cnt + 1u, sizeof(BloomMaterial32))) {
        return 0u;
    }
    uint16_t id = (uint16_t)s->mat_cnt++;
    s->materials[id] = *mat;
    s->state_dirty = true;
    return id;
}

bool bloom_scene_set_object_material(BloomScene *s, BloomHandle obj, uint16_t mat_id) {
    ObjectSlot *o = get_obj_mut(s, obj);
    if (!o || mat_id >= s->mat_cnt) return false;
    o->flags = (o->flags & 0xFFFFu) | ((uint32_t)mat_id << 16u);
    if (s->materials[mat_id].flags & BLOOM_MAT_FLAG_PBR) o->flags |= BLOOM_OBJ_FLAG_PBR;
    else o->flags &= ~BLOOM_OBJ_FLAG_PBR;
    s->state_dirty = true;
    return true;
}

uint32_t bloom_scene_add_light(BloomScene *s, const BloomLight32 *light) {
    if (!s || !light) return UINT32_MAX;
    if (!grow_array((void **)&s->lights, &s->light_cap, s->light_cnt + 1u, sizeof(BloomLight32))) {
        return UINT32_MAX;
    }
    uint32_t idx = s->light_cnt++;
    s->lights[idx] = *light;
    s->state_dirty = true;
    return idx;
}

void bloom_scene_clear_lights(BloomScene *s) {
    if (!s) return;
    s->light_cnt = 0u;
    s->state_dirty = true;
}

uint32_t bloom_scene_get_materials(const BloomScene *s, const BloomMaterial32 **out_ptr) {
    if (!s) return 0u;
    if (out_ptr) *out_ptr = s->materials;
    return s->mat_cnt;
}

uint32_t bloom_scene_get_lights(const BloomScene *s, const BloomLight32 **out_ptr) {
    if (!s) return 0u;
    if (out_ptr) *out_ptr = s->lights;
    return s->light_cnt;
}

bool bloom_scene_has_pbr_objects(const BloomScene *s) {
    if (!s) return false;
    for (uint32_t i = 0u; i < s->gpu_state_cnt; ++i) {
        if ((s->gpu_states[i].flags & BLOOM_OBJ_FLAG_PBR) != 0u) return true;
    }
    for (uint32_t i = 1u; i < s->obj_cap; ++i) {
        if (s->objs[i].alive && ((s->objs[i].flags & BLOOM_OBJ_FLAG_PBR) != 0u)) return true;
    }
    return false;
}

/* ============================================================================
 * Visual Presentation Timeline
 * ============================================================================ */

void bloom_timeline_clear(BloomScene *s) {
    if (!s) return;
    s->track_cnt = 0u;
    s->timeline_t = 0.0f;
}

uint32_t bloom_timeline_add_track(BloomScene *s, const BloomAnimTrack *track) {
    if (!s || !track) return UINT32_MAX;
    if (!grow_array((void **)&s->tracks, &s->track_cap, s->track_cnt + 1u, sizeof(BloomAnimTrack))) {
        return UINT32_MAX;
    }
    uint32_t idx = s->track_cnt++;
    s->tracks[idx] = *track;
    if (s->timeline_playing) s->state_dirty = true;
    return idx;
}

void bloom_timeline_set_playing(BloomScene *s, bool playing) {
    if (!s) return;
    s->timeline_playing = playing;
}

bool bloom_timeline_is_playing(const BloomScene *s) {
    return s ? s->timeline_playing : false;
}

static float eval_easing(BloomEasing e, float t) {
    t = clamp_f32(t, 0.0f, 1.0f);
    if (e == BLOOM_EASE_CUBIC_IN_OUT) {
        return t < 0.5f ? (4.0f * t * t * t) : (1.0f - powf(-2.0f * t + 2.0f, 3.0f) * 0.5f);
    }
    if (e == BLOOM_EASE_SPRING) {
        float w = 12.0f;
        return 1.0f - expf(-6.0f * t) * cosf(w * t);
    }
    return t;
}

void bloom_timeline_scrub(BloomScene *s, float t_sec) {
    if (!s) return;
    s->timeline_t = t_sec < 0.0f ? 0.0f : t_sec;
    for (uint32_t i = 0u; i < s->track_cnt; ++i) {
        const BloomAnimTrack *tr = &s->tracks[i];
        float dur = tr->t_end - tr->t_start;
        if (dur <= 1e-6f) continue;
        float rel = s->timeline_t - tr->t_start;
        float u = 0.0f;
        if (rel <= 0.0f) u = 0.0f;
        else if (tr->loop_mode == BLOOM_LOOP_ONCE) u = rel >= dur ? 1.0f : (rel / dur);
        else if (tr->loop_mode == BLOOM_LOOP_REPEAT) u = fmodf(rel, dur) / dur;
        else {
            float cyc = fmodf(rel, 2.0f * dur) / dur;
            u = cyc <= 1.0f ? cyc : (2.0f - cyc);
        }
        float val = tr->v_start + (tr->v_end - tr->v_start) * eval_easing(tr->easing, u);

        BloomHandle h = bloom_scene_find_object_by_key(s, tr->target_key);
        ObjectSlot *o = get_obj_mut(s, h);
        if (!o) continue;
        switch (tr->channel) {
            case BLOOM_ANIM_POS_X: o->local_pos[0] = val; break;
            case BLOOM_ANIM_POS_Y: o->local_pos[1] = val; break;
            case BLOOM_ANIM_POS_Z: o->local_pos[2] = val; break;
            case BLOOM_ANIM_ROT_Y:
                o->local_quat[0] = 0.0f;
                o->local_quat[1] = sinf(val * 0.5f);
                o->local_quat[2] = 0.0f;
                o->local_quat[3] = cosf(val * 0.5f);
                break;
            case BLOOM_ANIM_SCALE:
                o->local_scale[0] = o->local_scale[1] = o->local_scale[2] = val;
                break;
            case BLOOM_ANIM_DOF_0: bloom_scene_set_object_dof(s, h, 0u, val); break;
            case BLOOM_ANIM_DOF_1: bloom_scene_set_object_dof(s, h, 1u, val); break;
            case BLOOM_ANIM_DOF_2: bloom_scene_set_object_dof(s, h, 2u, val); break;
            case BLOOM_ANIM_CLIP_D: {
                float p[4];
                memcpy(p, o->local_clip, 16u);
                p[3] = val;
                bloom_scene_set_object_cutaway(s, h, (o->flags & BLOOM_OBJ_FLAG_CUTAWAY) != 0u, p);
                break;
            }
        }
    }
    if (s->track_cnt > 0u) s->state_dirty = true;
}

bool bloom_timeline_step(BloomScene *s, float dt_sec) {
    if (!s || !s->timeline_playing || s->track_cnt == 0u || dt_sec <= 0.0f) return false;
    float next_t = s->timeline_t + dt_sec;
    bool all_once_done = true;
    for (uint32_t i = 0u; i < s->track_cnt; ++i) {
        if (s->tracks[i].loop_mode != BLOOM_LOOP_ONCE || s->timeline_t < s->tracks[i].t_end) {
            all_once_done = false;
            break;
        }
    }
    if (all_once_done) {
        s->timeline_playing = false;
        return false;
    }
    bloom_timeline_scrub(s, next_t);
    return true;
}

/* ============================================================================
 * Unified Embodied Inspection Getters
 * ============================================================================ */

uint32_t bloom_scene_get_object_slot_count(const BloomScene *s) {
    return s ? (s->gpu_state_cnt + 1u) : 0u;
}

bool bloom_scene_get_object_meta(const BloomScene *s, uint32_t slot, BloomObjectMeta *out) {
    if (!s || !out || slot == 0u || slot > s->gpu_state_cnt) return false;
    uint32_t obj_idx = s->gpu_obj_slots[slot - 1u];
    if (obj_idx == 0u || obj_idx >= s->obj_cap || !s->objs[obj_idx].alive) return false;
    const ObjectSlot *o = &s->objs[obj_idx];
    out->handle = make_handle(obj_idx, o->gen);
    out->key = o->key;
    out->id = o->id;
    out->world_state = o->world_state;
    memcpy(out->local_dof, o->local_dof, 12u);
    out->cutaway = (o->flags & BLOOM_OBJ_FLAG_CUTAWAY) != 0u;
    return true;
}

uint32_t bloom_scene_get_analytic_count(const BloomScene *s) {
    return s ? s->ana_cnt : 0u;
}

bool bloom_scene_get_analytic_meta(const BloomScene *s, uint32_t idx, BloomAnalyticMeta *out) {
    if (!s || !out || idx >= s->ana_cnt) return false;
    out->index = idx;
    out->key = s->analytic_keys ? s->analytic_keys[idx] : 0u;
    out->prim = s->analytics[idx];
    return true;
}

uint32_t bloom_scene_get_ui_count(const BloomScene *s) {
    return s ? s->ui_cnt : 0u;
}

bool bloom_scene_get_ui_meta(const BloomScene *s, uint32_t slot, BloomUiMeta *out) {
    if (!s || !out || slot == 0u || slot >= s->ui_cnt || !s->uis[slot].alive) return false;
    const UiSlot *u = &s->uis[slot];
    out->handle = u->self;
    out->key = u->key;
    out->id = u->id_buf;
    out->label = u->label_buf;
    out->kind = u->desc.kind;
    out->x = u->x; out->y = u->y; out->w = u->w; out->h = u->h;
    out->clip_x = u->clip_x; out->clip_y = u->clip_y; out->clip_w = u->clip_w; out->clip_h = u->clip_h;
    out->z_order = u->z_order;
    out->disabled = u->desc.disabled;
    out->modal_trap = u->desc.modal_trap;
    out->focused = (u->self == s->focused_ui);
    out->hovered = (u->self == s->hovered_ui);
    out->toggle_on = u->toggle_on;
    out->slider_t = u->slider_t;
    out->scroll_y = u->scroll_y;
    out->action_mask = u->desc.action_mask;
    out->bind_obj_key = u->desc.bind_obj_key;
    out->bind_dof_slot = u->desc.bind_dof_slot;
    out->selected_idx = u->selected_idx;
    out->text_value = u->text_buf;
    out->preedit_text = u->preedit_buf;
    return true;
}

bool bloom_scene_get_template_mesh(const BloomScene *s, uint16_t template_id,
                                   uint32_t *out_mcnt, const BloomMeshletHeader32 **out_hdrs,
                                   const BloomVtx12 **out_verts, const uint8_t **out_lidx,
                                   uint32_t *out_idx_bytes) {
    if (!s || template_id >= s->tpl_cnt) return false;
    const SceneTemplate *t = &s->templates[template_id];
    if (out_mcnt) *out_mcnt = t->meshlet_cnt;
    if (out_hdrs) *out_hdrs = t->hdrs;
    if (out_verts) *out_verts = t->verts;
    if (out_lidx) *out_lidx = t->local_idx;
    if (out_idx_bytes) *out_idx_bytes = t->idx_bytes;
    return true;
}
