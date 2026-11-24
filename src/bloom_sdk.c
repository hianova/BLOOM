#include "bloom_sdk.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float clamp_f32(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void cam_basis(const BloomCamera *cam, float r[3], float u[3], float f[3]) {
    float cy = cosf(cam->yaw), sy = sinf(cam->yaw);
    float cp = cosf(cam->pitch), sp = sinf(cam->pitch);
    f[0] = sy * cp;
    f[1] = -sp;
    f[2] = cy * cp;
    r[0] = cy;
    r[1] = 0.0f;
    r[2] = -sy;
    u[0] = f[1] * r[2] - f[2] * r[1];
    u[1] = f[2] * r[0] - f[0] * r[2];
    u[2] = f[0] * r[1] - f[1] * r[0];
}

static void pixel_ray(const BloomCamera *cam, const float r[3], const float u[3], const float f[3],
                      float vp_w, float vp_h, float px, float py, float out_rd[3]) {
    float uvx = ((px + 0.5f) / vp_w) * 2.0f - 1.0f;
    float uvy = 1.0f - ((py + 0.5f) / vp_h) * 2.0f;
    float aspect = vp_w / vp_h;
    float focal = cam->focal > 0.05f ? cam->focal : 1.0f;
    float sx = uvx * aspect / focal;
    float sy = uvy / focal;
    float dx = r[0] * sx + u[0] * sy + f[0];
    float dy = r[1] * sx + u[1] * sy + f[1];
    float dz = r[2] * sx + u[2] * sy + f[2];
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len > 1e-6f) {
        dx /= len;
        dy /= len;
        dz /= len;
    }
    out_rd[0] = dx;
    out_rd[1] = dy;
    out_rd[2] = dz;
}

static uint16_t find_active_modal_z(const BloomScene *scene, uint32_t ui_cnt) {
    uint16_t mz = 0u;
    for (uint32_t s = 1u; s < ui_cnt; ++s) {
        BloomUiMeta um;
        if (bloom_scene_get_ui_meta(scene, s, &um) &&
            (um.kind == BLOOM_UI_MODAL || um.modal_trap) && um.z_order > mz) {
            mz = um.z_order;
        }
    }
    return mz;
}

static void fill_ground_stats(BloomSdkEntity *e, const BloomGroundedGpu32 *st) {
    e->visible_px = st->visible_px;
    if (st->visible_px > 0u) {
        e->screen_bbox[0] = st->min_x;
        e->screen_bbox[1] = st->min_y;
        e->screen_bbox[2] = st->max_x;
        e->screen_bbox[3] = st->max_y;
        e->anchor_x = (float)((st->best_anchor >> 13u) & 0x1FFFu);
        e->anchor_y = (float)(st->best_anchor & 0x1FFFu);
        e->part_id = (uint8_t)(st->part_sample & 0xFFu);
        e->min_depth = (st->min_depth_u32 <= 255u) ? ((float)st->min_depth_u32 / 255.0f) : 1.0f;
    } else {
        e->min_depth = 1.0f;
    }
}

static void populate_ui_entity(BloomSdkEntity *e, const BloomUiMeta *um, uint16_t slot,
                               uint16_t modal_z, float vp_w, float vp_h,
                               const BloomGroundedGpu32 *st) {
    memset(e, 0, sizeof(*e));
    e->domain = BLOOM_GROUND_UI;
    e->slot = slot;
    e->handle = um->handle;
    e->key = um->key;
    snprintf(e->id, sizeof(e->id), "%s", um->id ? um->id : "");
    snprintf(e->label, sizeof(e->label), "%s", um->label ? um->label : "");
    e->ui_kind = um->kind;
    e->disabled = um->disabled;
    e->modal_trap = um->modal_trap;
    e->focused = um->focused;
    e->hovered = um->hovered;
    e->toggle_on = um->toggle_on;
    e->slider_t = um->slider_t;
    e->scroll_y = um->scroll_y;
    e->action_mask = um->action_mask;
    e->layout_rect[0] = um->x;
    e->layout_rect[1] = um->y;
    e->layout_rect[2] = um->w;
    e->layout_rect[3] = um->h;

    if (st) {
        fill_ground_stats(e, st);
    } else {
        e->min_depth = 1.0f;
    }

    float cx0 = fmaxf(fmaxf(um->x, um->clip_x), 0.0f);
    float cy0 = fmaxf(fmaxf(um->y, um->clip_y), 0.0f);
    float cx1 = fminf(fminf(um->x + um->w, um->clip_x + um->clip_w), vp_w);
    float cy1 = fminf(fminf(um->y + um->h, um->clip_y + um->clip_h), vp_h);
    float area = (cx1 > cx0 && cy1 > cy0) ? (cx1 - cx0) * (cy1 - cy0) : 0.0f;

    e->visible_ratio = (area > 0.5f) ? clamp_f32((float)e->visible_px / area, 0.0f, 1.0f) : 0.0f;
    e->occluded = (e->visible_px == 0u);
    bool blocked = (modal_z > 0u && um->z_order < modal_z);
    e->actionable = !um->disabled && !e->occluded && !blocked && (um->action_mask != 0u);
}

static void populate_mesh_entity(BloomSdkEntity *e, const BloomObjectMeta *om, uint16_t slot,
                                 uint16_t modal_z, const BloomGroundedGpu32 *st) {
    memset(e, 0, sizeof(*e));
    e->domain = BLOOM_GROUND_MESH;
    e->slot = slot;
    e->handle = om->handle;
    e->key = om->key;
    snprintf(e->id, sizeof(e->id), "%s", om->id ? om->id : "");
    snprintf(e->label, sizeof(e->label), "%s", om->id ? om->id : "");
    e->action_mask = BLOOM_ACT_SET_DOF | BLOOM_ACT_CUTAWAY;
    memcpy(e->world_pos, om->world_state.pos, sizeof(e->world_pos));
    memcpy(e->local_dof, om->local_dof, sizeof(e->local_dof));
    e->cutaway = om->cutaway;
    if (st) {
        fill_ground_stats(e, st);
    } else {
        e->min_depth = 1.0f;
    }
    e->visible_ratio = (e->visible_px > 0u) ? 1.0f : 0.0f;
    e->occluded = (e->visible_px == 0u);
    e->actionable = !e->occluded && (modal_z == 0u);
}

static void populate_analytic_entity(BloomSdkEntity *e, const BloomAnalyticMeta *am, uint16_t slot,
                                     uint16_t modal_z, const BloomGroundedGpu32 *st) {
    memset(e, 0, sizeof(*e));
    e->domain = BLOOM_GROUND_ANALYTIC;
    e->slot = slot;
    e->key = am->key;
    snprintf(e->id, sizeof(e->id), "analytic_%u", am->index);
    snprintf(e->label, sizeof(e->label), "analytic_%u", am->index);
    e->action_mask = BLOOM_ACT_FOCUS;
    e->world_pos[0] = am->prim.center[0];
    e->world_pos[1] = am->prim.center[1];
    e->world_pos[2] = am->prim.center[2];
    if (st) {
        fill_ground_stats(e, st);
    } else {
        e->min_depth = 1.0f;
    }
    e->visible_ratio = (e->visible_px > 0u) ? 1.0f : 0.0f;
    e->occluded = (e->visible_px == 0u);
    e->actionable = !e->occluded && (modal_z == 0u);
}

bool bloom_sdk_capture_snapshot(BloomGpu *gpu, BloomScene *scene,
                                uint32_t vp_w, uint32_t vp_h,
                                BloomSdkSnapshot *out_snap) {
    if (!gpu || !scene || !out_snap || vp_w == 0u || vp_h == 0u) return false;
    memset(out_snap, 0, sizeof(*out_snap));

    bloom_scene_sync_transforms(scene);
    bloom_scene_layout_ui(scene, (float)vp_w, (float)vp_h);

    uint32_t obj_slots = bloom_scene_get_object_slot_count(scene);
    uint32_t ana_cnt = bloom_scene_get_analytic_count(scene);
    uint32_t ui_cnt = bloom_scene_get_ui_count(scene);

    uint32_t stride = obj_slots;
    if (ana_cnt + 1u > stride) stride = ana_cnt + 1u;
    if (ui_cnt > stride) stride = ui_cnt;
    stride += 16u;
    uint32_t max_slots = stride * 3u;

    BloomGroundedGpu32 *stats = (BloomGroundedGpu32 *)calloc(max_slots, sizeof(BloomGroundedGpu32));
    if (!stats) return false;

    BloomPickGpuResult pick;
    memset(&pick, 0, sizeof(pick));
    if (!bloom_gpu_render_grounded(gpu, scene, vp_w, vp_h, vp_w / 2u, vp_h / 2u,
                                   stats, max_slots, &pick)) {
        free(stats);
        return false;
    }

    uint32_t max_entities = obj_slots + ana_cnt + ui_cnt;
    BloomSdkEntity *ents = max_entities > 0u
                               ? (BloomSdkEntity *)calloc(max_entities, sizeof(BloomSdkEntity))
                               : NULL;
    if (max_entities > 0u && !ents) {
        free(stats);
        return false;
    }

    uint16_t modal_z = find_active_modal_z(scene, ui_cnt);
    uint32_t cnt = 0u;

    for (uint32_t s = 1u; s < ui_cnt; ++s) {
        BloomUiMeta um;
        if (!bloom_scene_get_ui_meta(scene, s, &um)) continue;
        uint32_t gi = (uint32_t)(BLOOM_GROUND_UI - 1u) * stride + s;
        populate_ui_entity(&ents[cnt++], &um, (uint16_t)s, modal_z,
                           (float)vp_w, (float)vp_h, &stats[gi]);
    }
    for (uint32_t s = 1u; s < obj_slots; ++s) {
        BloomObjectMeta om;
        if (!bloom_scene_get_object_meta(scene, s, &om)) continue;
        uint32_t gi = (uint32_t)(BLOOM_GROUND_MESH - 1u) * stride + s;
        populate_mesh_entity(&ents[cnt++], &om, (uint16_t)s, modal_z, &stats[gi]);
    }
    for (uint32_t i = 0u; i < ana_cnt; ++i) {
        BloomAnalyticMeta am;
        if (!bloom_scene_get_analytic_meta(scene, i, &am)) continue;
        uint32_t slot = i + 1u;
        uint32_t gi = (uint32_t)(BLOOM_GROUND_ANALYTIC - 1u) * stride + slot;
        populate_analytic_entity(&ents[cnt++], &am, (uint16_t)slot, modal_z, &stats[gi]);
    }

    free(stats);

    BloomGpuStats gs = bloom_gpu_get_stats(gpu);
    out_snap->camera = bloom_scene_get_camera(scene);
    out_snap->vp_w = vp_w;
    out_snap->vp_h = vp_h;
    out_snap->ground_ms = gs.last_ground_ms;
    out_snap->entity_cnt = cnt;
    out_snap->entities = ents;
    return true;
}

void bloom_sdk_free_snapshot(BloomSdkSnapshot *snap) {
    if (!snap) return;
    free(snap->entities);
    memset(snap, 0, sizeof(*snap));
}

bool bloom_sdk_pick_at(BloomGpu *gpu, BloomScene *scene,
                       uint32_t vp_w, uint32_t vp_h,
                       uint32_t px, uint32_t py,
                       BloomSdkEntity *out_entity) {
    if (!gpu || !scene || !out_entity || vp_w == 0u || vp_h == 0u) return false;
    memset(out_entity, 0, sizeof(*out_entity));

    bloom_scene_sync_transforms(scene);
    bloom_scene_layout_ui(scene, (float)vp_w, (float)vp_h);

    uint32_t obj_slots = bloom_scene_get_object_slot_count(scene);
    uint32_t ana_cnt = bloom_scene_get_analytic_count(scene);
    uint32_t ui_cnt = bloom_scene_get_ui_count(scene);
    uint32_t stride = obj_slots;
    if (ana_cnt + 1u > stride) stride = ana_cnt + 1u;
    if (ui_cnt > stride) stride = ui_cnt;
    stride += 16u;
    uint32_t max_slots = stride * 3u;

    BloomPickGpuResult pk;
    memset(&pk, 0, sizeof(pk));
    if (!bloom_gpu_render_grounded(gpu, scene, vp_w, vp_h, px, py, NULL, max_slots, &pk)) {
        return false;
    }

    uint32_t dom = bloom_ground_domain(pk.ground_id);
    uint32_t slot = bloom_ground_slot(pk.ground_id);
    uint32_t part = bloom_ground_part(pk.ground_id);
    uint32_t qd = bloom_ground_depth_q8(pk.ground_id);
    if (dom == BLOOM_GROUND_EMPTY || slot == 0u) return false;

    uint16_t modal_z = find_active_modal_z(scene, ui_cnt);
    BloomGroundedGpu32 synth;
    memset(&synth, 0, sizeof(synth));
    synth.visible_px = 1u;
    synth.min_x = px;
    synth.min_y = py;
    synth.max_x = px;
    synth.max_y = py;
    synth.best_anchor = (60u << 26u) | ((px & 0x1FFFu) << 13u) | (py & 0x1FFFu);
    synth.part_sample = part;
    synth.min_depth_u32 = qd;

    BloomCamera cam = bloom_scene_get_camera(scene);
    float cr[3], cu[3], cf[3], rd[3];
    cam_basis(&cam, cr, cu, cf);
    pixel_ray(&cam, cr, cu, cf, (float)vp_w, (float)vp_h, (float)px, (float)py, rd);

    if (dom == BLOOM_GROUND_UI) {
        BloomUiMeta um;
        if (!bloom_scene_get_ui_meta(scene, slot, &um)) return false;
        populate_ui_entity(out_entity, &um, (uint16_t)slot, modal_z, (float)vp_w, (float)vp_h, &synth);
        return true;
    }
    if (dom == BLOOM_GROUND_MESH) {
        BloomObjectMeta om;
        if (!bloom_scene_get_object_meta(scene, slot, &om)) return false;
        populate_mesh_entity(out_entity, &om, (uint16_t)slot, modal_z, &synth);
        float nz = (float)qd / 255.0f;
        float denom = 0.98f - nz;
        float fwd_cos = rd[0] * cf[0] + rd[1] * cf[1] + rd[2] * cf[2];
        if (denom > 1e-4f && fwd_cos > 1e-4f) {
            float z_view = clamp_f32(1.56f / denom, cam.near_z, cam.far_z);
            float t = z_view / fwd_cos;
            out_entity->world_pos[0] = cam.pos[0] + rd[0] * t;
            out_entity->world_pos[1] = cam.pos[1] + rd[1] * t;
            out_entity->world_pos[2] = cam.pos[2] + rd[2] * t;
        }
        return true;
    }
    if (dom == BLOOM_GROUND_ANALYTIC) {
        BloomAnalyticMeta am;
        if (!bloom_scene_get_analytic_meta(scene, slot - 1u, &am)) return false;
        populate_analytic_entity(out_entity, &am, (uint16_t)slot, modal_z, &synth);
        float oc[3] = {
            cam.pos[0] - am.prim.center[0],
            cam.pos[1] - am.prim.center[1],
            cam.pos[2] - am.prim.center[2]
        };
        float b = oc[0] * rd[0] + oc[1] * rd[1] + oc[2] * rd[2];
        float c = oc[0] * oc[0] + oc[1] * oc[1] + oc[2] * oc[2] - am.prim.radius * am.prim.radius;
        float h = b * b - c;
        if (h >= 0.0f) {
            float t = -b - sqrtf(h);
            if (t > 0.0f) {
                out_entity->world_pos[0] = cam.pos[0] + rd[0] * t;
                out_entity->world_pos[1] = cam.pos[1] + rd[1] * t;
                out_entity->world_pos[2] = cam.pos[2] + rd[2] * t;
            }
        }
        return true;
    }
    return false;
}

const BloomSdkEntity *bloom_sdk_find_by_id(const BloomSdkSnapshot *snap, const char *id) {
    if (!snap || !snap->entities || !id) return NULL;
    for (uint32_t i = 0u; i < snap->entity_cnt; ++i) {
        if (strcmp(snap->entities[i].id, id) == 0) return &snap->entities[i];
    }
    uint64_t k = bloom_fnv1a64(id);
    for (uint32_t i = 0u; i < snap->entity_cnt; ++i) {
        if (snap->entities[i].key == k) return &snap->entities[i];
    }
    return NULL;
}

uint32_t bloom_sdk_query(const BloomSdkSnapshot *snap,
                         uint32_t domain_mask, int ui_kind_or_neg1,
                         bool only_actionable,
                         uint32_t *out_idxs, uint32_t max_out) {
    if (!snap || !snap->entities) return 0u;
    uint32_t matched = 0u;
    for (uint32_t i = 0u; i < snap->entity_cnt; ++i) {
        const BloomSdkEntity *e = &snap->entities[i];
        if (domain_mask != 0u && ((domain_mask & (1u << e->domain)) == 0u)) continue;
        if (ui_kind_or_neg1 >= 0 && (e->domain != BLOOM_GROUND_UI || (int)e->ui_kind != ui_kind_or_neg1)) continue;
        if (only_actionable && !e->actionable) continue;
        if (out_idxs && matched < max_out) out_idxs[matched] = i;
        matched++;
    }
    return matched;
}

bool bloom_sdk_dispatch_action(BloomScene *scene, const BloomSdkSnapshot *snap,
                               const char *id, BloomSdkAction action, float value) {
    if (!scene || !snap || !id) return false;
    const BloomSdkEntity *e = bloom_sdk_find_by_id(snap, id);
    if (!e || e->disabled || !e->actionable) return false;

    if (action == BLOOM_SDK_ACT_CLICK) {
        if (e->domain != BLOOM_GROUND_UI || (e->action_mask & BLOOM_ACT_CLICK) == 0u) return false;
        BloomInputEvent down = {BLOOM_EVENT_POINTER_DOWN, e->anchor_x, e->anchor_y, 0.0f, 0.0f, 0u, 0u, 0u, 0u};
        BloomInputEvent up   = {BLOOM_EVENT_POINTER_UP,   e->anchor_x, e->anchor_y, 0.0f, 0.0f, 0u, 0u, 0u, 0u};
        bool hit = bloom_scene_handle_input(scene, &down);
        bloom_scene_handle_input(scene, &up);
        return hit;
    }
    if (action == BLOOM_SDK_ACT_SET_VALUE) {
        if (e->domain != BLOOM_GROUND_UI || e->ui_kind != BLOOM_UI_SLIDER ||
            (e->action_mask & BLOOM_ACT_DRAG) == 0u) {
            return false;
        }
        float t = clamp_f32(value, 0.0f, 1.0f);
        float tx = e->layout_rect[0] + 12.0f;
        float tw = e->layout_rect[2] - 24.0f;
        float target_x = tw > 1.0f ? (tx + t * tw) : e->anchor_x;
        BloomInputEvent down = {BLOOM_EVENT_POINTER_DOWN, e->anchor_x, e->anchor_y, 0.0f, 0.0f, 0u, 0u, 0u, 0u};
        BloomInputEvent move = {BLOOM_EVENT_POINTER_MOVE, target_x, e->anchor_y, 0.0f, 0.0f, 0u, 0u, 0u, 0u};
        BloomInputEvent up   = {BLOOM_EVENT_POINTER_UP,   target_x, e->anchor_y, 0.0f, 0.0f, 0u, 0u, 0u, 0u};
        if (!bloom_scene_handle_input(scene, &down)) return false;
        bloom_scene_handle_input(scene, &move);
        bloom_scene_handle_input(scene, &up);
        return true;
    }
    if (action == BLOOM_SDK_ACT_SCROLL) {
        if (e->domain != BLOOM_GROUND_UI || e->ui_kind != BLOOM_UI_VLIST ||
            (e->action_mask & BLOOM_ACT_SCROLL) == 0u) {
            return false;
        }
        return bloom_scene_set_ui_scroll(scene, e->handle, value);
    }
    if (action == BLOOM_SDK_ACT_FOCUS) {
        if (e->domain != BLOOM_GROUND_UI) return false;
        return bloom_scene_set_ui_focused(scene, e->handle);
    }
    if (action == BLOOM_SDK_ACT_SET_DOF) {
        if (e->domain != BLOOM_GROUND_MESH) return false;
        return bloom_scene_set_object_dof(scene, e->handle, 0u, value);
    }
    if (action == BLOOM_SDK_ACT_SET_CUTAWAY) {
        if (e->domain != BLOOM_GROUND_MESH) return false;
        const float default_plane[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        return bloom_scene_set_object_cutaway(scene, e->handle, value > 0.5f, default_plane);
    }
    return false;
}

typedef struct {
    char  *buf;
    size_t cap;
    size_t len;
} JsonWriter;

static void jw_fmt(JsonWriter *jw, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char *dst = (jw->buf && jw->len < jw->cap) ? (jw->buf + jw->len) : NULL;
    size_t rem = (jw->buf && jw->len < jw->cap) ? (jw->cap - jw->len) : 0u;
    int n = vsnprintf(dst, rem, fmt, ap);
    va_end(ap);
    if (n > 0) jw->len += (size_t)n;
}

static void jw_str(JsonWriter *jw, const char *s) {
    jw_fmt(jw, "\"");
    if (s) {
        for (const char *p = s; *p; ++p) {
            unsigned char c = (unsigned char)*p;
            if (c == '"' || c == '\\') jw_fmt(jw, "\\%c", (char)c);
            else if (c == '\n') jw_fmt(jw, "\\n");
            else if (c == '\r') jw_fmt(jw, "\\r");
            else if (c == '\t') jw_fmt(jw, "\\t");
            else if (c >= 0x20u) jw_fmt(jw, "%c", (char)c);
        }
    }
    jw_fmt(jw, "\"");
}

static const char *domain_name(uint8_t dom) {
    if (dom == BLOOM_GROUND_MESH) return "mesh";
    if (dom == BLOOM_GROUND_ANALYTIC) return "analytic";
    if (dom == BLOOM_GROUND_UI) return "ui";
    return "empty";
}

size_t bloom_sdk_snapshot_to_json(const BloomSdkSnapshot *snap, char *buf, size_t cap) {
    if (!snap) return 0u;
    JsonWriter jw = {buf, cap, 0u};
    jw_fmt(&jw, "{\"vp_w\":%u,\"vp_h\":%u,\"ground_ms\":%.3f,\"entity_cnt\":%u,\"entities\":[",
           snap->vp_w, snap->vp_h, snap->ground_ms, snap->entity_cnt);
    for (uint32_t i = 0u; i < snap->entity_cnt; ++i) {
        const BloomSdkEntity *e = &snap->entities[i];
        jw_fmt(&jw, "%s{\"id\":", i == 0u ? "" : ",");
        jw_str(&jw, e->id);
        jw_fmt(&jw, ",\"domain\":\"%s\",\"slot\":%u,\"part\":%u,"
                    "\"visible_px\":%u,\"visible_ratio\":%.4f,"
                    "\"occluded\":%s,\"actionable\":%s,"
                    "\"anchor\":[%.1f,%.1f],"
                    "\"bbox\":[%u,%u,%u,%u],"
                    "\"world_pos\":[%.4f,%.4f,%.4f]}",
               domain_name(e->domain), e->slot, e->part_id,
               e->visible_px, e->visible_ratio,
               e->occluded ? "true" : "false",
               e->actionable ? "true" : "false",
               e->anchor_x, e->anchor_y,
               e->screen_bbox[0], e->screen_bbox[1], e->screen_bbox[2], e->screen_bbox[3],
               e->world_pos[0], e->world_pos[1], e->world_pos[2]);
    }
    jw_fmt(&jw, "]}");
    if (buf && cap > 0u && jw.len >= cap) buf[cap - 1u] = '\0';
    return jw.len;
}
