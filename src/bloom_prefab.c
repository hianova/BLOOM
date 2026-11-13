#include "bloom_prefab.h"
#include "bloom_scene.h"
#include "bloom_ui.h"
#include "flow.h"

#include <limits.h>
#include <string.h>

/*
 * ============================================================================
 * Pillar 3 ("Prefab SSOT"): Reusable 64B Blueprints, Live Non-Destructive
 * Overrides, Revert-to-SSOT, X-Ray Cutaway, Wave64 LOD, TRC Guard & FLOW v6.0.0
 * ============================================================================
 */

static inline int32_t pf_clamp64_to_32(int64_t v, int32_t lo, int32_t hi) {
    if (v < (int64_t)lo) return lo;
    if (v > (int64_t)hi) return hi;
    return (int32_t)v;
}

static inline int64_t pf_max64(int64_t a, int64_t b) {
    return (a > b) ? a : b;
}

static void pf_stamp_mac(void *block64, uint64_t seed, uint32_t *out_lo, uint32_t *out_hi) {
    uint64_t mac = bloom_hash56_fnv1a(block64, seed);
    *out_lo = (uint32_t)(mac & 0xFFFFFFFFULL);
    *out_hi = (uint32_t)(mac >> 32ULL);
}

uint64_t bloom_prefab_calc_hash(const BloomPrefabDesc64 *prefab) {
    if (!prefab) return 0ULL;
    return bloom_hash56_fnv1a(prefab, 0x550794EFULL);
}

static void pf_make_style(BloomArtStyle64 *s,
                          uint32_t id,
                          uint32_t base_rgb,
                          uint32_t accent_rgb,
                          int32_t rough,
                          int32_t metal,
                          int32_t emissive,
                          int32_t freq,
                          int32_t stroke,
                          int32_t strobe_hz,
                          uint32_t matter_slot) {
    memset(s, 0, sizeof(*s));
    s->style_id = id;
    s->base_rgb24 = base_rgb;
    s->accent_rgb24 = accent_rgb;
    s->roughness_q16 = rough;
    s->metallic_q16 = metal;
    s->emissive_gain_q16 = emissive;
    s->sdf_noise_freq_q16 = freq;
    s->sdf_noise_amp_q16 = 2048;
    s->msdf_stroke_q16 = stroke;
    s->strobe_hz_q16 = strobe_hz;
    s->matter_slot = matter_slot;
    pf_stamp_mac(s, 0x5791E064ULL, &s->mac_lo32, &s->mac_hi32);
}

static void pf_make_prefab(BloomPrefabDesc64 *p,
                           uint32_t id,
                           uint32_t style_id,
                           uint32_t matter_id,
                           uint32_t mask,
                           int32_t bound_r,
                           int32_t p0, int32_t p1, int32_t p2,
                           int32_t p3, int32_t p4, int32_t p5) {
    memset(p, 0, sizeof(*p));
    p->prefab_id = id;
    p->art_style_id = style_id;
    p->matter_id = matter_id;
    p->csg_op_mask = mask;
    p->bounding_radius_q16 = bound_r;
    p->prim_params_q16[0] = p0;
    p->prim_params_q16[1] = p1;
    p->prim_params_q16[2] = p2;
    p->prim_params_q16[3] = p3;
    p->prim_params_q16[4] = p4;
    p->prim_params_q16[5] = p5;
    p->flags = 1U;
    uint64_t h = bloom_prefab_calc_hash(p);
    p->hash_lo32 = (uint32_t)(h & 0xFFFFFFFFULL);
    p->hash_hi32 = (uint32_t)(h >> 32ULL);
}

void bloom_prefab_init_canonical(BloomArtStyle64 out_styles[BLOOM_MAX_STYLES],
                                 BloomPrefabDesc64 out_prefabs[BLOOM_MAX_PREFABS]) {
    if (out_styles) {
        pf_make_style(&out_styles[0], 1U, 0xD8F4FFU, 0x20D8FFU,  3276,     0, 81920,  65536, 2048,      0, BLOOM_MATTER_CRYSTAL_GLASS);
        pf_make_style(&out_styles[1], 2U, 0x22C88AU, 0x70FFCCU, 19660,     0, 65536, 131072, 2560,      0, BLOOM_MATTER_IMPERIAL_JADE);
        pf_make_style(&out_styles[2], 3U, 0x2890FFU, 0x80D8FFU,  1638,     0, 65536, 196608, 3072,      0, BLOOM_MATTER_LIQUID_WATER);
        pf_make_style(&out_styles[3], 4U, 0xB8C4D0U, 0x60B8FFU, 16384, 58982, 65536, 262144, 3584,      0, BLOOM_MATTER_TITANIUM);
        pf_make_style(&out_styles[4], 5U, 0xFFD452U, 0xFFF0A0U,  6553, 65536, 78643, 327680, 4096,      0, BLOOM_MATTER_AURUM_GOLD);
        pf_make_style(&out_styles[5], 6U, 0x1E54E8U, 0x58A8FFU,  2048,     0, 72089, 393216, 4608,      0, BLOOM_MATTER_SAPPHIRE);
        pf_make_style(&out_styles[6], 7U, 0x181C24U, 0x00F0D0U,  8192, 16384, 65536, 458752, 5120,      0, BLOOM_MATTER_OBSIDIAN);
        /* Style 8: Thermal Meltdown Alert (raw strobe 8.0 Hz -> clamped to <= 3.0 Hz by TRC guard!) */
        pf_make_style(&out_styles[7], 8U, 0xFF2838U, 0xFFB020U, 13107, 16384, 196608, 524288, 6144, 524288, BLOOM_MATTER_BLACKBODY);
    }

    if (out_prefabs) {
        memset(out_prefabs, 0, sizeof(BloomPrefabDesc64) * BLOOM_MAX_PREFABS);
        /* 1: Crystal Sphere */
        pf_make_prefab(&out_prefabs[0], 1U, 1U, BLOOM_MATTER_CRYSTAL_GLASS,
                       BLOOM_CSG_MASK_PACK(BLOOM_CSG_PRIM_SPHERE, BLOOM_CSG_PRIM_NONE, BLOOM_CSG_BOOL_UNION, 0, 0),
                       85196, 65536, 0, 0, 0, 0, 0);
        /* 2: Quantum Torus */
        pf_make_prefab(&out_prefabs[1], 2U, 6U, BLOOM_MATTER_SAPPHIRE,
                       BLOOM_CSG_MASK_PACK(BLOOM_CSG_PRIM_TORUS, BLOOM_CSG_PRIM_SPHERE, BLOOM_CSG_BOOL_SMOOTH_UNION, 4, 1),
                       98304, 58982, 16384, 24576, 12288, 0, 0);
        /* 3: Citadel Core (RoundBox + Torus Smooth Union) */
        pf_make_prefab(&out_prefabs[2], 3U, 4U, BLOOM_MATTER_TITANIUM,
                       BLOOM_CSG_MASK_PACK(BLOOM_CSG_PRIM_BOX, BLOOM_CSG_PRIM_TORUS, BLOOM_CSG_BOOL_SMOOTH_UNION, 0, 0),
                       114688, 52428, 52428, 52428, 68812, 13107, 16384);
        /* 4: Octahedral Prism */
        pf_make_prefab(&out_prefabs[3], 4U, 5U, BLOOM_MATTER_AURUM_GOLD,
                       BLOOM_CSG_MASK_PACK(BLOOM_CSG_PRIM_OCTAHEDRON, BLOOM_CSG_PRIM_BOX, BLOOM_CSG_BOOL_INTERSECT, 0, 1),
                       91750, 72089, 55705, 55705, 55705, 0, 0);
        /* 5: Capsule Pillar */
        pf_make_prefab(&out_prefabs[4], 5U, 3U, BLOOM_MATTER_LIQUID_WATER,
                       BLOOM_CSG_MASK_PACK(BLOOM_CSG_PRIM_CAPSULE, BLOOM_CSG_PRIM_TORUS, BLOOM_CSG_BOOL_SMOOTH_UNION, 0, 2),
                       98304, 28000, 52428, 0, 45875, 9830, 12288);
        /* 6: Imperial Jade Vessel (Sphere - Inner Cylinder Hollow so X-Ray reveals inner cavity!) */
        pf_make_prefab(&out_prefabs[5], 6U, 2U, BLOOM_MATTER_IMPERIAL_JADE,
                       BLOOM_CSG_MASK_PACK(BLOOM_CSG_PRIM_SPHERE, BLOOM_CSG_PRIM_SPHERE, BLOOM_CSG_BOOL_SUBTRACT, 0, 0),
                       85196, 65536, 0, 0, 42598, 0, 0);
        /* 7: Aurum Monolith */
        pf_make_prefab(&out_prefabs[6], 7U, 5U, BLOOM_MATTER_AURUM_GOLD,
                       BLOOM_CSG_MASK_PACK(BLOOM_CSG_PRIM_BOX, BLOOM_CSG_PRIM_SPHERE, BLOOM_CSG_BOOL_SUBTRACT, 0, 1),
                       98304, 49152, 65536, 49152, 55705, 0, 0);
        /* 8: Thermal Reactor Core */
        pf_make_prefab(&out_prefabs[7], 8U, 8U, BLOOM_MATTER_BLACKBODY,
                       BLOOM_CSG_MASK_PACK(BLOOM_CSG_PRIM_SPHERE, BLOOM_CSG_PRIM_OCTAHEDRON, BLOOM_CSG_BOOL_SMOOTH_UNION, 0, 0),
                       98304, 49152, 0, 0, 62259, 0, 16384);
    }
}

BloomPrefabDesc64 bloom_prefab_join(const BloomPrefabDesc64 *a,
                                    const BloomPrefabDesc64 *b,
                                    uint32_t new_prefab_id,
                                    uint32_t csg_bool_op) {
    BloomPrefabDesc64 out;
    memset(&out, 0, sizeof(out));
    if (!a || !b) return out;
    uint32_t p1 = a->csg_op_mask & 0xFU;
    uint32_t p2 = b->csg_op_mask & 0xFU;
    out.prefab_id = new_prefab_id;
    out.art_style_id = a->art_style_id;
    out.matter_id = a->matter_id;
    out.csg_op_mask = BLOOM_CSG_MASK_PACK(p1, p2, csg_bool_op & 0xFU, 0U, 0U);
    out.bounding_radius_q16 = (a->bounding_radius_q16 > b->bounding_radius_q16)
        ? a->bounding_radius_q16 : b->bounding_radius_q16;
    out.prim_params_q16[0] = a->prim_params_q16[0];
    out.prim_params_q16[1] = a->prim_params_q16[1];
    out.prim_params_q16[2] = a->prim_params_q16[2];
    out.prim_params_q16[3] = b->prim_params_q16[0];
    out.prim_params_q16[4] = b->prim_params_q16[1];
    out.prim_params_q16[5] = 16384;
    out.flags = a->flags | b->flags;
    uint64_t h = bloom_prefab_calc_hash(&out);
    out.hash_lo32 = (uint32_t)(h & 0xFFFFFFFFULL);
    out.hash_hi32 = (uint32_t)(h >> 32ULL);
    return out;
}

int64_t bloom_prefab_eval_sdf_q16(const BloomPrefabDesc64 *prefab,
                                  int64_t lx_q16,
                                  int64_t ly_q16,
                                  int64_t lz_q16,
                                  int32_t param_delta_q16) {
    if (!prefab) return (int64_t)BLOOM_MAX_Q16;
    uint32_t prim1 = (prefab->csg_op_mask >> 0U) & 0xFU;
    uint32_t prim2 = (prefab->csg_op_mask >> 4U) & 0xFU;
    uint32_t bool_op = (prefab->csg_op_mask >> 8U) & 0xFU;

    BloomCsgOp64 op1;
    int32_t zpos[3] = { 0, 0, 0 };
    int32_t p0_eff = pf_clamp64_to_32((int64_t)prefab->prim_params_q16[0] + (int64_t)param_delta_q16, 4096, 8 * BLOOM_Q16_ONE);
    int32_t ext1[3] = {
        p0_eff,
        pf_clamp64_to_32(prefab->prim_params_q16[1] > 0 ? prefab->prim_params_q16[1] : p0_eff, 4096, 8 * BLOOM_Q16_ONE),
        pf_clamp64_to_32(prefab->prim_params_q16[2] > 0 ? prefab->prim_params_q16[2] : p0_eff, 4096, 8 * BLOOM_Q16_ONE)
    };
    bloom_csg_op_init64(&op1, 1U, prim1, BLOOM_CSG_BOOL_UNION, prefab->matter_id, zpos, ext1, 0, 2048);
    int64_t d1 = bloom_csg_eval_single_q16(&op1, lx_q16, ly_q16, lz_q16);
    if (prim2 == BLOOM_CSG_PRIM_NONE) return d1;

    BloomCsgOp64 op2;
    int32_t ext2[3] = {
        pf_clamp64_to_32(prefab->prim_params_q16[3], 4096, 8 * BLOOM_Q16_ONE),
        pf_clamp64_to_32(prefab->prim_params_q16[4] > 0 ? prefab->prim_params_q16[4] : prefab->prim_params_q16[3], 4096, 8 * BLOOM_Q16_ONE),
        pf_clamp64_to_32(prefab->prim_params_q16[3], 4096, 8 * BLOOM_Q16_ONE)
    };
    bloom_csg_op_init64(&op2, 2U, prim2, bool_op, prefab->matter_id, zpos, ext2,
                        prefab->prim_params_q16[5], 1024);
    int64_t d2 = bloom_csg_eval_single_q16(&op2, lx_q16, ly_q16, lz_q16);

    switch (bool_op) {
    case BLOOM_CSG_BOOL_SUBTRACT:
        return pf_max64(d1, (d2 == INT64_MIN) ? INT64_MAX : -d2);
    case BLOOM_CSG_BOOL_INTERSECT:
        return pf_max64(d1, d2);
    case BLOOM_CSG_BOOL_SMOOTH_UNION:
        return bloom_smin_q16(d1, d2, pf_max64(prefab->prim_params_q16[5], 8192));
    default:
        return (d1 < d2) ? d1 : d2;
    }
}

int64_t bloom_prefab_eval_xray_cutaway_q16(int64_t base_sdf_q16,
                                           int64_t world_x_q16,
                                           int32_t xray_plane_q16,
                                           bool xray_enabled) {
    if (!xray_enabled) return base_sdf_q16;
    /* Half-space cutaway: clip region where world_x < xray_plane (overflow-free across INT64_MIN..MAX) */
    int64_t wx = world_x_q16;
    if (wx < -INT64_C(0x3FFFFFFFFFFF)) wx = -INT64_C(0x3FFFFFFFFFFF);
    if (wx >  INT64_C(0x3FFFFFFFFFFF)) wx =  INT64_C(0x3FFFFFFFFFFF);
    int64_t clip_d = (int64_t)xray_plane_q16 - wx;
    return pf_max64(base_sdf_q16, clip_d);
}

/* 3. TRC Accessibility Guard (<= 3.0 Hz Flash Clamp + Colorblind Geom Freq) */
BloomArtStyle64 bloom_style_apply_trc_guard(BloomArtStyle64 style,
                                            bool photosensitive_safe,
                                            bool colorblind_geom) {
    if (photosensitive_safe) {
        if (style.strobe_hz_q16 > BLOOM_TRC_MAX_STROBE_HZ_Q16) {
            style.strobe_hz_q16 = BLOOM_TRC_MAX_STROBE_HZ_Q16;
        }
        if (style.strobe_hz_q16 < 0) style.strobe_hz_q16 = 0;
        if (style.emissive_gain_q16 > 2 * BLOOM_Q16_ONE) {
            style.emissive_gain_q16 = 2 * BLOOM_Q16_ONE;
        }
    }
    if (colorblind_geom) {
        uint32_t idx = (style.style_id > 0U && style.style_id <= 8U) ? style.style_id : 1U;
        style.sdf_noise_freq_q16 = (int32_t)(idx * 65536U);
        style.msdf_stroke_q16 = (int32_t)(1536U + idx * 512U);
    }
    pf_stamp_mac(&style, 0x5791E064ULL, &style.mac_lo32, &style.mac_hi32);
    return style;
}

BloomArtStyle64 bloom_style_resolve_thermal(const BloomArtStyle64 styles[BLOOM_MAX_STYLES],
                                            uint32_t style_id,
                                            uint32_t thermal_kelvin,
                                            bool prev_overheated,
                                            bool photosensitive_safe,
                                            bool colorblind_geom,
                                            bool *out_overheated) {
    bool hot = prev_overheated
        ? (thermal_kelvin > BLOOM_THERMAL_EXIT_KELVIN)
        : (thermal_kelvin >= BLOOM_THERMAL_ENTER_KELVIN);
    if (out_overheated) *out_overheated = hot;

    BloomArtStyle64 resolved;
    if (!styles) {
        memset(&resolved, 0, sizeof(resolved));
        resolved.style_id = 1U;
        return bloom_style_apply_trc_guard(resolved, photosensitive_safe, colorblind_geom);
    }
    uint32_t pick = hot ? 8U : ((style_id >= 1U && style_id <= BLOOM_MAX_STYLES) ? style_id : 1U);
    resolved = styles[pick - 1U];
    return bloom_style_apply_trc_guard(resolved, photosensitive_safe, colorblind_geom);
}

/* 4. Scene Instancing, Live Non-Destructive Override & O(1) Revert to SSOT */
uint32_t bloom_prefab_spawn_inst(BloomEngineArena *arena,
                                 uint32_t prefab_id,
                                 const int32_t pos_q16[3],
                                 int32_t scale_q16) {
    if (!arena || prefab_id == 0U || prefab_id > BLOOM_MAX_PREFABS) return 0U;
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    if (hdr->inst_count >= BLOOM_MAX_INSTANCES) return 0U;

    uint32_t slot = hdr->inst_count++;
    BloomPrefabInst64 *inst = &arena->map.instances[slot];
    memset(inst, 0, sizeof(*inst));
    inst->inst_id = slot + 1U;
    inst->prefab_id = prefab_id;
    if (pos_q16) {
        inst->pos_q16[0] = pos_q16[0];
        inst->pos_q16[1] = pos_q16[1];
        inst->pos_q16[2] = pos_q16[2];
    }
    inst->scale_q16 = (scale_q16 > 4096) ? scale_q16 : BLOOM_Q16_ONE;
    inst->thermal_kelvin = 300U;
    inst->active_layer = BLOOM_LAYER_0_SDF;
    inst->flags = BLOOM_INST_FLAG_ACTIVE | BLOOM_INST_FLAG_COLLIDABLE;
    inst->flow_slot = BLOOM_FLOW_CH_PREFAB0 + (slot % 10U);
    return inst->inst_id;
}

void bloom_prefab_override_inst(BloomPrefabInst64 *inst,
                                uint32_t style_override_id,
                                uint32_t matter_override_id,
                                int32_t param_override_q16) {
    if (!inst) return;
    inst->style_override_id = style_override_id;
    inst->matter_override_id = matter_override_id;
    inst->param_override_q16 = param_override_q16;
    inst->flags |= BLOOM_INST_FLAG_OVERRIDDEN;
}

void bloom_prefab_revert_to_ssot(BloomPrefabInst64 *inst,
                                 const BloomPrefabDesc64 *ssot_prefab) {
    if (!inst) return;
    (void)ssot_prefab;
    inst->style_override_id = 0U;
    inst->matter_override_id = 0U;
    inst->param_override_q16 = 0;
    inst->thermal_kelvin = 300U;
    inst->flags &= (uint16_t)~BLOOM_INST_FLAG_OVERRIDDEN;
}

/* 5. Wave64 Distance LOD Compaction */
uint32_t bloom_prefab_cull_wave64(const BloomPrefabInst64 *instances,
                                  size_t count,
                                  const int32_t eye_q16[3],
                                  int32_t layer0_r_q16,
                                  int32_t layer1_r_q16,
                                  BloomPrefabInst64 *out_compacted,
                                  uint64_t *out_active_mask) {
    if (!instances || !eye_q16) {
        if (out_active_mask) *out_active_mask = 0ULL;
        return 0U;
    }
    size_t n = (count > 64U) ? 64U : count;
    uint64_t mask = 0ULL;
    uint32_t out_n = 0U;

    for (size_t i = 0U; i < n; ++i) {
        if ((instances[i].flags & BLOOM_INST_FLAG_ACTIVE) == 0U) continue;
        int64_t dx = (int64_t)instances[i].pos_q16[0] - (int64_t)eye_q16[0];
        int64_t dy = (int64_t)instances[i].pos_q16[1] - (int64_t)eye_q16[1];
        int64_t dz = (int64_t)instances[i].pos_q16[2] - (int64_t)eye_q16[2];
        uint64_t ax = (uint64_t)(dx < 0 ? -dx : dx);
        uint64_t ay = (uint64_t)(dy < 0 ? -dy : dy);
        uint64_t az = (uint64_t)(dz < 0 ? -dz : dz);
        int32_t dist = ((ax | ay | az) >= (uint64_t)BLOOM_MAX_Q16)
            ? BLOOM_MAX_Q16
            : bloom_isqrt_u64_to_i32(ax * ax + ay * ay + az * az);

        BloomPrefabInst64 copy = instances[i];
        if (dist <= layer0_r_q16) {
            copy.active_layer = BLOOM_LAYER_0_SDF;
        } else if (dist <= layer1_r_q16) {
            copy.active_layer = BLOOM_LAYER_1_SLICE;
        } else {
            copy.active_layer = BLOOM_LAYER_2_HUD;
        }
        mask |= (1ULL << i);
        if (out_compacted) out_compacted[out_n] = copy;
        ++out_n;
    }
    if (out_active_mask) *out_active_mask = mask;
    return out_n;
}

/* 6. FLOW v6.0.0 Arena Initialization, @swap & Full Dynamical Step */
static void pf_pack_flow_chart(uint64_t out[8],
                               int64_t b0,
                               int64_t b1,
                               int64_t b2,
                               int32_t b_exit,
                               int32_t b_enter,
                               int32_t dt_max,
                               int32_t u_gain,
                               uint8_t next_lo,
                               uint8_t next_hi,
                               uint16_t kan_lock,
                               uint32_t epoch) {
    out[0] = (uint64_t)b0;
    out[1] = (uint64_t)b1;
    out[2] = (uint64_t)b2;
    out[3] = ((uint64_t)(uint32_t)b_exit << 32U) | (uint64_t)(uint32_t)b_enter;
    out[4] = ((uint64_t)(uint32_t)dt_max << 32U) | (uint64_t)(uint32_t)u_gain;
    out[5] = ((uint64_t)(next_lo & 0xFU) << 24U) | ((uint64_t)(next_hi & 0xFU) << 16U) | (uint64_t)kan_lock;
    out[6] = (uint64_t)epoch << 32U;
    out[7] = 0ULL;
}

static void pf_pack_flow_state(uint64_t out[8], int32_t x0_q16, uint16_t init_chart, uint16_t init_mode) {
    memset(out, 0, 64U);
    out[3] = (uint64_t)(int64_t)x0_q16;
    out[5] = ((uint64_t)(init_chart & 0xFU) << 16U) | (uint64_t)init_mode;
    out[6] = FLOW_STATUS_CONVERGED;
}

void bloom_arena_init_default(BloomEngineArena *arena, uint32_t width_px, uint32_t height_px) {
    if (!arena) return;
    memset(arena, 0, sizeof(*arena));

    BloomFrameHeader64 *hdr = &arena->cells[BLOOM_SLOT_HEADER].frame_hdr;
    hdr->magic = BLOOM_MAGIC_U32;
    hdr->version = BLOOM_VERSION_U32;
    hdr->width_px = (width_px > 0U) ? width_px : 1280U;
    hdr->height_px = (height_px > 0U) ? height_px : 720U;
    hdr->dpi_scale_q16 = BLOOM_Q16_ONE;
    hdr->flags = BLOOM_FLAG_TRC_SAFE;
    hdr->cap_mask = BLOOM_ARENA_CAP_MASK;

    bloom_camera_init64(&arena->cells[BLOOM_SLOT_CAMERA].camera,
                        22000, 18000, 5 * BLOOM_Q16_ONE,
                        35 * BLOOM_Q16_ONE, 183500 /* f/2.8 */);
    bloom_reg_bank_init64(&arena->cells[BLOOM_SLOT_REGBANK].reg_bank);
    bloom_text_input_init64(&arena->cells[BLOOM_SLOT_TEXT_INPUT].text_input, 1U, BLOOM_ACTION_FOCUS_INPUT);
    bloom_ime_init64(&arena->cells[BLOOM_SLOT_IME].ime, 1U);
    bloom_window_init64(&arena->cells[BLOOM_SLOT_WINDOW].window, 1U, hdr->width_px, hdr->height_px,
                        BLOOM_Q16_ONE, 60U);

    bloom_matter_init_canonical(arena->map.matters);
    bloom_prefab_init_canonical(arena->map.styles, arena->map.prefabs);

    /* Publish Canonical FLOW v6.0.0 16-Chart Manifold Atlas (Slots 232..247) */
    flow_cell *charts = (flow_cell *)arena->map.flow_charts;
    uint64_t tmp[8];
    /* Chart 0: UI Crisp Tactile Micro-Spring (horizon 2560 Q16 ~= 0.039/step, zero overshoot) */
    pf_pack_flow_chart(tmp, 0, FLOW_Q16_ONE, 0, -FLOW_MAX_Q16, FLOW_MAX_Q16,
                       2560, FLOW_Q16_ONE, 0U, 0U, 0U, 1U);
    flow_arena_publish_bound(charts, BLOOM_CHART_UI_SPRING, BLOOM_FLOW_CHART_MASK, 1U, tmp);

    /* Chart 1: UI Fast Pointer-Capture Scrubbing */
    pf_pack_flow_chart(tmp, 0, FLOW_Q16_ONE, 0, -FLOW_MAX_Q16, FLOW_MAX_Q16,
                       16384, FLOW_Q16_ONE, 1U, 1U, 0U, 1U);
    flow_arena_publish_bound(charts, BLOOM_CHART_UI_SCRUB, BLOOM_FLOW_CHART_MASK, 1U, tmp);

    /* Chart 2: Camera Orbit & Thin-Lens Focus Rig */
    pf_pack_flow_chart(tmp, 0, FLOW_Q16_ONE, 0, -FLOW_MAX_Q16, FLOW_MAX_Q16,
                       12288, FLOW_Q16_ONE, 2U, 2U, 0U, 1U);
    flow_arena_publish_bound(charts, BLOOM_CHART_CAMERA_RIG, BLOOM_FLOW_CHART_MASK, 1U, tmp);

    /* Chart 3: Smooth X-Ray Cutaway Plane Sweep */
    pf_pack_flow_chart(tmp, 0, FLOW_Q16_ONE, 0, -FLOW_MAX_Q16, FLOW_MAX_Q16,
                       24576, FLOW_Q16_ONE, 3U, 3U, 0U, 1U);
    flow_arena_publish_bound(charts, BLOOM_CHART_XRAY_SWEEP, BLOOM_FLOW_CHART_MASK, 1U, tmp);

    /* Chart 4 <-> Chart 5: Prefab 800K Enter / 760K Exit Schmitt Thermal Hysteresis + KanLock */
    pf_pack_flow_chart(tmp, 0, FLOW_Q16_ONE, 0, -FLOW_MAX_Q16, 800 * FLOW_Q16_ONE,
                       120 * FLOW_Q16_ONE, FLOW_Q16_ONE, 4U, 5U, 0U, 1U);
    flow_arena_publish_bound(charts, BLOOM_CHART_PREFAB_NORMAL, BLOOM_FLOW_CHART_MASK, 1U, tmp);

    pf_pack_flow_chart(tmp, 0, FLOW_Q16_ONE, 0, 760 * FLOW_Q16_ONE, FLOW_MAX_Q16,
                       120 * FLOW_Q16_ONE, FLOW_Q16_ONE, 4U, 5U, 0x0001U, 1U);
    flow_arena_publish_bound(charts, BLOOM_CHART_PREFAB_ALERT, BLOOM_FLOW_CHART_MASK, 1U, tmp);

    for (uint32_t c = 6U; c < BLOOM_MAX_FLOW_CHARTS; ++c) {
        pf_pack_flow_chart(tmp, 0, FLOW_Q16_ONE, 0, -FLOW_MAX_Q16, FLOW_MAX_Q16,
                           8192, FLOW_Q16_ONE, (uint8_t)c, (uint8_t)c, 0U, 1U);
        flow_arena_publish_bound(charts, c, BLOOM_FLOW_CHART_MASK, 1U, tmp);
    }

    /* Initialize 32 FLOW v6.0.0 State Channels */
    const BloomCamera64 *cam = &arena->cells[BLOOM_SLOT_CAMERA].camera;
    pf_pack_flow_state(arena->cells[BLOOM_SLOT_FLOW_STATE_BASE + BLOOM_FLOW_CH_CAM_YAW].u64,
                       cam->orbit_yaw_q16, BLOOM_CHART_CAMERA_RIG, 0U);
    pf_pack_flow_state(arena->cells[BLOOM_SLOT_FLOW_STATE_BASE + BLOOM_FLOW_CH_CAM_PITCH].u64,
                       cam->orbit_pitch_q16, BLOOM_CHART_CAMERA_RIG, 0U);
    pf_pack_flow_state(arena->cells[BLOOM_SLOT_FLOW_STATE_BASE + BLOOM_FLOW_CH_CAM_DIST].u64,
                       cam->orbit_dist_q16, BLOOM_CHART_CAMERA_RIG, 0U);
    pf_pack_flow_state(arena->cells[BLOOM_SLOT_FLOW_STATE_BASE + BLOOM_FLOW_CH_CAM_FOCUS].u64,
                       cam->focus_dist_q16, BLOOM_CHART_CAMERA_RIG, 0U);

    for (uint32_t i = 0U; i < BLOOM_MAX_UI_NODES; ++i) {
        pf_pack_flow_state(arena->cells[BLOOM_SLOT_FLOW_STATE_BASE + BLOOM_FLOW_CH_UI_NODE0 + i].u64,
                           BLOOM_Q16_ONE, BLOOM_CHART_UI_SPRING, 0U);
    }
    pf_pack_flow_state(arena->cells[BLOOM_SLOT_FLOW_STATE_BASE + BLOOM_FLOW_CH_XRAY_PLANE].u64,
                       0, BLOOM_CHART_XRAY_SWEEP, 0U);
    pf_pack_flow_state(arena->cells[BLOOM_SLOT_FLOW_STATE_BASE + BLOOM_FLOW_CH_SCROLL].u64,
                       0, BLOOM_CHART_UI_SPRING, 0U);

    for (uint32_t i = 0U; i < 10U; ++i) {
        pf_pack_flow_state(arena->cells[BLOOM_SLOT_FLOW_STATE_BASE + BLOOM_FLOW_CH_PREFAB0 + i].u64,
                           300 * BLOOM_Q16_ONE, BLOOM_CHART_PREFAB_NORMAL, 0U);
    }

    bloom_image_seal_arena(arena);
}

bool bloom_arena_try_swap_chart(BloomEngineArena *arena,
                                uint32_t flow_slot,
                                uint32_t target_chart_idx,
                                uint16_t mode_req) {
    if (!arena || flow_slot >= BLOOM_MAX_FLOW_STATES || target_chart_idx >= BLOOM_MAX_FLOW_CHARTS) {
        return false;
    }
    uint64_t *st = arena->cells[BLOOM_SLOT_FLOW_STATE_BASE + flow_slot].u64;
    const flow_cell *charts = (const flow_cell *)arena->cells[BLOOM_SLOT_FLOW_CHART_BASE].u64;
    uint64_t cur_chart = (st[5] >> 16U) & BLOOM_FLOW_CHART_MASK;
    uint16_t kan_lock = (uint16_t)(charts[cur_chart][5] & 0xFFFFULL);
    uint16_t cur_mode = (uint16_t)(st[5] & 0xFFFFULL);
    uint16_t next_mode = (uint16_t)((mode_req & (uint16_t)(~kan_lock)) | (cur_mode & kan_lock));
    st[5] = ((uint64_t)(target_chart_idx & BLOOM_FLOW_CHART_MASK) << 16U) | (uint64_t)next_mode;
    return true;
}

bool bloom_arena_step_dynamics(BloomEngineArena *arena, uint16_t mode_req) {
    if (!arena) return true;
    BloomFrameHeader64 *hdr = &arena->cells[BLOOM_SLOT_HEADER].frame_hdr;
    BloomCamera64 *cam = &arena->cells[BLOOM_SLOT_CAMERA].camera;
    const flow_cell *charts = (const flow_cell *)arena->cells[BLOOM_SLOT_FLOW_CHART_BASE].u64;

    bool all_converged = true;

    /* 1. Step Camera Rig Channels (0..3) toward target orbit state */
    int64_t cam_targets[4] = {
        cam->orbit_yaw_q16,
        cam->orbit_pitch_q16,
        cam->orbit_dist_q16,
        cam->focus_dist_q16
    };
    int32_t cam_resolved[4];
    for (uint32_t c = 0U; c < 4U; ++c) {
        uint64_t *st = arena->cells[BLOOM_SLOT_FLOW_STATE_BASE + c].u64;
        flow_step_atlas(st, st, charts, BLOOM_FLOW_CHART_MASK, cam_targets[c], mode_req);
        if ((st[6] & FLOW_STATUS_CONVERGED) == 0ULL || st[4] != 0ULL) {
            all_converged = false;
        }
        cam_resolved[c] = pf_clamp64_to_32((int64_t)st[3], -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
    }

    /* Update live camera eye position from smoothed 2-Jet coordinates */
    BloomCamera64 smooth_cam = *cam;
    smooth_cam.orbit_yaw_q16 = cam_resolved[0];
    smooth_cam.orbit_pitch_q16 = cam_resolved[1];
    smooth_cam.orbit_dist_q16 = cam_resolved[2];
    bloom_camera_sync_from_orbit(&smooth_cam);
    cam->eye_q16[0] = smooth_cam.eye_q16[0];
    cam->eye_q16[1] = smooth_cam.eye_q16[1];
    cam->eye_q16[2] = smooth_cam.eye_q16[2];

    /* 2. Step X-Ray Cutaway Plane Channel */
    {
        uint64_t *st = arena->cells[BLOOM_SLOT_FLOW_STATE_BASE + BLOOM_FLOW_CH_XRAY_PLANE].u64;
        flow_step_atlas(st, st, charts, BLOOM_FLOW_CHART_MASK, hdr->xray_plane_q16, mode_req);
        if ((st[6] & FLOW_STATUS_CONVERGED) == 0ULL || st[4] != 0ULL) {
            all_converged = false;
        }
    }

    /* 3. Check UI Spring Channels (4..4+ui_count) */
    uint32_t ui_n = (hdr->ui_count > BLOOM_MAX_UI_NODES) ? BLOOM_MAX_UI_NODES : hdr->ui_count;
    BloomUiNode64 *nodes = arena->map.ui_nodes;
    for (uint32_t i = 0U; i < ui_n; ++i) {
        uint64_t *st = arena->map.flow_states[BLOOM_FLOW_CH_UI_NODE0 + i];
        int64_t tgt = BLOOM_Q16_ONE;
        if ((nodes[i].style_and_state & BLOOM_UI_STATE_ACTIVE_BIT) != 0U) tgt = 60293;
        else if ((nodes[i].style_and_state & BLOOM_UI_STATE_HOVER_BIT) != 0U) tgt = 72089;
        flow_step_atlas(st, st, charts, BLOOM_FLOW_CHART_MASK, tgt, mode_req);
        nodes[i].scale_jet_v_q16 = pf_clamp64_to_32((int64_t)st[3], 49152, 85196);
        nodes[i].scale_jet_d1_q16 = (int32_t)(st[4] & 0x7FFFFFFFULL);
        if ((st[6] & FLOW_STATUS_CONVERGED) == 0ULL || st[4] != 0ULL) {
            all_converged = false;
        }
    }

    /* 4. Step Prefab Thermal / Dynamical Channels (22..31) */
    uint32_t inst_n = (hdr->inst_count > BLOOM_MAX_INSTANCES) ? BLOOM_MAX_INSTANCES : hdr->inst_count;
    for (uint32_t i = 0U; i < inst_n; ++i) {
        BloomPrefabInst64 *inst = &arena->map.instances[i];
        if ((inst->flags & BLOOM_INST_FLAG_ACTIVE) == 0U) continue;
        uint32_t fslot = inst->flow_slot & 31U;
        uint64_t *st = arena->map.flow_states[fslot];
        int64_t target_k_q16 = (int64_t)inst->thermal_kelvin * FLOW_Q16_ONE;
        flow_step_atlas(st, st, charts, BLOOM_FLOW_CHART_MASK, target_k_q16, mode_req);
        if ((st[6] & FLOW_STATUS_CONVERGED) == 0ULL || st[4] != 0ULL) {
            all_converged = false;
        }
    }

    bloom_ui_bind_reg_bank64(nodes, ui_n, &arena->map.reg_bank);

    if (all_converged) {
        hdr->flags |= BLOOM_FLAG_IDLE_SLEEP;
    } else {
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
        hdr->frame_seq++;
    }
    return all_converged;
}

/* 7. Unified .bloom 64B Container Export / Validate */
static uint64_t pf_merkle_arena(const BloomEngineArena *arena) {
    uint64_t h = UINT64_C(0xCBF29CE484222325);
    for (uint32_t c = 0U; c < BLOOM_SLOT_IMAGE_HDR; ++c) {
        const uint8_t *p = arena->cells[c].bytes;
        for (uint32_t b = 0U; b < 64U; ++b) {
            h ^= (uint64_t)p[b];
            h *= UINT64_C(0x100000001B3);
        }
    }
    return h;
}

void bloom_image_seal_arena(BloomEngineArena *arena) {
    if (!arena) return;
    const BloomFrameHeader64 *hdr = &arena->cells[BLOOM_SLOT_HEADER].frame_hdr;
    BloomImageHeader64 *im = &arena->cells[BLOOM_SLOT_IMAGE_HDR].img_hdr;
    memset(im, 0, sizeof(*im));
    im->magic_lo32 = BLOOM_IMAGE_MAGIC_LO;
    im->magic_hi32 = BLOOM_IMAGE_MAGIC_HI;
    im->block_cap_mask = BLOOM_ARENA_CAP_MASK;
    im->version = BLOOM_VERSION_U32;
    im->prefab_off = BLOOM_SLOT_PREFAB_BASE;
    im->prefab_count = BLOOM_MAX_PREFABS;
    im->csg_off = BLOOM_SLOT_CSG_BASE;
    im->csg_count = hdr->csg_count;
    im->ui_off = BLOOM_SLOT_UI_BASE;
    im->ui_count = hdr->ui_count;
    im->flow_chart_off = BLOOM_SLOT_FLOW_CHART_BASE;
    im->flow_chart_count = BLOOM_MAX_FLOW_CHARTS;
    im->schema_lo32 = 0xB100A006U;
    im->schema_hi32 = 0x00000256U;
    uint64_t mk = pf_merkle_arena(arena);
    im->merkle_lo32 = (uint32_t)(mk & 0xFFFFFFFFULL);
    im->merkle_hi32 = (uint32_t)(mk >> 32ULL);
}

bool bloom_image_verify_arena(const BloomEngineArena *arena) {
    if (!arena) return false;
    const BloomImageHeader64 *im = &arena->cells[BLOOM_SLOT_IMAGE_HDR].img_hdr;
    if (im->magic_lo32 != BLOOM_IMAGE_MAGIC_LO || im->magic_hi32 != BLOOM_IMAGE_MAGIC_HI) {
        return false;
    }
    if (im->block_cap_mask != BLOOM_ARENA_CAP_MASK || im->version != BLOOM_VERSION_U32) {
        return false;
    }
    uint64_t mk = pf_merkle_arena(arena);
    return (im->merkle_lo32 == (uint32_t)(mk & 0xFFFFFFFFULL)) &&
           (im->merkle_hi32 == (uint32_t)(mk >> 32ULL));
}
