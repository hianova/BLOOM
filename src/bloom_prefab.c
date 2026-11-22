#include "bloom_prefab.h"
#include "bloom_scene.h"
#include "bloom_ui.h"
#include "flow_sha256.h"
#include "searu.h"

#include <limits.h>
#include <string.h>

/*
 * ============================================================================
 * Pillar 3 ("Prefab SSOT"): Reusable 64B Blueprints, Live Non-Destructive
 * Overrides, Revert-to-SSOT, X-Ray Cutaway, Wave64 LOD, TRC Guard, Swept-CCD
 * & Coupled Thermal Physics + FLOW v9.0.0
 * ============================================================================
 */

BLOOM_STATIC_ASSERT(sizeof(BloomBilevelTerm) == sizeof(searu_bilevel_term),
                    "BloomBilevelTerm must match searu_bilevel_term ABI");

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

/* 6. FLOW v7.0.1 Arena Initialization, @swap, SEARU Heal & Full Dynamical Step */
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

    /* Publish Canonical FLOW v7.0.1 16-Chart Manifold Atlas (Slots 232..247) */
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

    /* Initialize 32 FLOW v9.0.0 State Channels */
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

    {
        static const int32_t def_norm[3] = { BLOOM_Q16_ONE, 0, 0 };
        bloom_section_init64(&arena->map.section_spec, BLOOM_SEC_MODE_OFF, def_norm,
                             0, 16384 /* 0.25 slab */, 11796 /* 0.18 min wall */,
                             9830 /* 0.15 iso pitch */,
                             BLOOM_INSPECT_ISOLINES | BLOOM_INSPECT_WALL_DFM |
                             BLOOM_INSPECT_INTERFERENCE | BLOOM_INSPECT_TRIPLANAR);
        BloomPhysicsSummary64 *ps = &arena->map.phys_summary;
        memset(ps, 0, sizeof(*ps));
        ps->min_clearance_q16 = BLOOM_MAX_Q16;
        ps->earliest_toi_q16 = BLOOM_Q16_ONE;
        ps->peak_kelvin = 300U;
        ps->contact_norm_q16[1] = BLOOM_Q16_ONE;
        pf_stamp_mac(ps, 0xB4150064ULL, &ps->mac_lo32, &ps->mac_hi32);
    }

    bloom_image_seal_arena(arena);
}

static uint64_t pf_abs64(int64_t x) {
    if (x == INT64_MIN) return (uint64_t)INT64_MAX;
    return (x < 0) ? (uint64_t)(-x) : (uint64_t)x;
}

static uint64_t pf_isqrt_u64(uint64_t n) {
    if (n == 0ULL) return 0ULL;
    if (n == UINT64_MAX) return 0xFFFFFFFFULL;
    uint64_t x = n;
    uint64_t y = (x >> 1ULL) + 1ULL;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1ULL;
    }
    return x;
}

static void pf_norm3_q16(int64_t x, int64_t y, int64_t z, int32_t out_n[3]) {
    uint64_t ax = pf_abs64(x), ay = pf_abs64(y), az = pf_abs64(z);
    uint32_t sh = 0U;
    while (((ax | ay | az) >> sh) > 0x3FFFFFFFULL && sh < 48U) ++sh;
    int64_t sx = x / (int64_t)(1ULL << sh);
    int64_t sy = y / (int64_t)(1ULL << sh);
    int64_t sz = z / (int64_t)(1ULL << sh);
    uint64_t len = pf_isqrt_u64((uint64_t)(sx * sx + sy * sy + sz * sz));
    if (len == 0ULL) {
        out_n[0] = 0; out_n[1] = BLOOM_Q16_ONE; out_n[2] = 0;
        return;
    }
    out_n[0] = pf_clamp64_to_32((sx * 65536LL) / (int64_t)len, -BLOOM_Q16_ONE, BLOOM_Q16_ONE);
    out_n[1] = pf_clamp64_to_32((sy * 65536LL) / (int64_t)len, -BLOOM_Q16_ONE, BLOOM_Q16_ONE);
    out_n[2] = pf_clamp64_to_32((sz * 65536LL) / (int64_t)len, -BLOOM_Q16_ONE, BLOOM_Q16_ONE);
}

bool bloom_swept_sphere_ccd_q16(const int32_t pos_a_q16[3],
                                const int32_t vel_a_q16[3],
                                int32_t rad_a_q16,
                                const int32_t pos_b_q16[3],
                                const int32_t vel_b_q16[3],
                                int32_t rad_b_q16,
                                int32_t *out_toi_q16,
                                int32_t out_normal_q16[3]) {
    if (out_toi_q16) *out_toi_q16 = BLOOM_Q16_ONE;
    if (out_normal_q16) {
        out_normal_q16[0] = 0; out_normal_q16[1] = BLOOM_Q16_ONE; out_normal_q16[2] = 0;
    }
    if (!pos_a_q16 || !pos_b_q16) return false;

    int64_t r0x = (int64_t)pos_a_q16[0] - (int64_t)pos_b_q16[0];
    int64_t r0y = (int64_t)pos_a_q16[1] - (int64_t)pos_b_q16[1];
    int64_t r0z = (int64_t)pos_a_q16[2] - (int64_t)pos_b_q16[2];
    int64_t vx = (vel_a_q16 ? (int64_t)vel_a_q16[0] : 0LL) - (vel_b_q16 ? (int64_t)vel_b_q16[0] : 0LL);
    int64_t vy = (vel_a_q16 ? (int64_t)vel_a_q16[1] : 0LL) - (vel_b_q16 ? (int64_t)vel_b_q16[1] : 0LL);
    int64_t vz = (vel_a_q16 ? (int64_t)vel_a_q16[2] : 0LL) - (vel_b_q16 ? (int64_t)vel_b_q16[2] : 0LL);
    int64_t rsum = (int64_t)(rad_a_q16 > 0 ? rad_a_q16 : 0) + (int64_t)(rad_b_q16 > 0 ? rad_b_q16 : 0);

    uint64_t m = pf_abs64(r0x) | pf_abs64(r0y) | pf_abs64(r0z) |
                 pf_abs64(vx)  | pf_abs64(vy)  | pf_abs64(vz)  | pf_abs64(rsum);
    uint32_t sh = 0U;
    while ((m >> sh) > 0x3FFFULL && sh < 48U) ++sh;
    int64_t dv = (int64_t)(1ULL << sh);
    int64_t sx0 = r0x / dv, sy0 = r0y / dv, sz0 = r0z / dv;
    int64_t svx = vx / dv,  svy = vy / dv,  svz = vz / dv;
    int64_t sR  = rsum / dv;

    int64_t c = (sx0 * sx0 + sy0 * sy0 + sz0 * sz0) - (sR * sR);
    if (c <= 0) {
        if (out_toi_q16) *out_toi_q16 = 0;
        if (out_normal_q16) pf_norm3_q16(r0x, r0y, r0z, out_normal_q16);
        return true;
    }
    int64_t a = svx * svx + svy * svy + svz * svz;
    int64_t b = sx0 * svx + sy0 * svy + sz0 * svz;
    if (a <= 0 || b >= 0) return false;

    int64_t b2 = b * b;
    int64_t ac = a * c;
    if (b2 < ac) return false;
    uint64_t sd = pf_isqrt_u64((uint64_t)(b2 - ac));
    int64_t neg_b = (b == INT64_MIN) ? INT64_MAX : -b;
    int64_t num = neg_b - (int64_t)sd;
    if (num < 0) num = 0;
    if (num > a) return false;

    uint32_t tsh = 0U;
    while ((num >> tsh) > INT64_C(0x3FFFFFFFFFF) || (a >> tsh) > INT64_C(0x3FFFFFFFFFF)) ++tsh;
    int64_t aden = a / (int64_t)(1ULL << tsh);
    if (aden <= 0) return false;
    int32_t toi = pf_clamp64_to_32(((num / (int64_t)(1ULL << tsh)) * 65536LL) / aden, 0, BLOOM_Q16_ONE);
    if (out_toi_q16) *out_toi_q16 = toi;
    if (out_normal_q16) {
        int64_t nx = r0x + ((vx * (int64_t)toi) >> 16);
        int64_t ny = r0y + ((vy * (int64_t)toi) >> 16);
        int64_t nz = r0z + ((vz * (int64_t)toi) >> 16);
        pf_norm3_q16(nx, ny, nz, out_normal_q16);
    }
    return true;
}

static int64_t pf_eval_inst_sdf_q16(const BloomEngineArena *arena,
                                    const BloomPrefabInst64 *inst,
                                    int64_t px, int64_t py, int64_t pz) {
    uint32_t pid = inst->prefab_id;
    if (pid == 0U || pid > BLOOM_MAX_PREFABS) return (int64_t)BLOOM_MAX_Q16;
    const BloomPrefabDesc64 *pf = &arena->map.prefabs[pid - 1U];
    int64_t sc = (inst->scale_q16 > 4096) ? (int64_t)inst->scale_q16 : (int64_t)BLOOM_Q16_ONE;
    int32_t rel[3] = {
        pf_clamp64_to_32(px - (int64_t)inst->pos_q16[0], -BLOOM_MAX_Q16, BLOOM_MAX_Q16),
        pf_clamp64_to_32(py - (int64_t)inst->pos_q16[1], -BLOOM_MAX_Q16, BLOOM_MAX_Q16),
        pf_clamp64_to_32(pz - (int64_t)inst->pos_q16[2], -BLOOM_MAX_Q16, BLOOM_MAX_Q16)
    };
    int32_t rot_rel[3];
    bloom_rot_inv_euler_q16(rel, inst->rot_euler_q16, rot_rel);
    int64_t lx = ((int64_t)rot_rel[0] * 65536LL) / sc;
    int64_t ly = ((int64_t)rot_rel[1] * 65536LL) / sc;
    int64_t lz = ((int64_t)rot_rel[2] * 65536LL) / sc;
    int64_t dl = (pf->csg_count > 0U &&
                  (size_t)pf->csg_start_idx + (size_t)pf->csg_count <= BLOOM_MAX_CSG_OPS)
        ? bloom_csg_eval_program_q16(&arena->map.csg_ops[pf->csg_start_idx], pf->csg_count, lx, ly, lz, NULL)
        : bloom_prefab_eval_sdf_q16(pf, lx, ly, lz, inst->param_override_q16);
    return (dl * sc) >> 16;
}

static int64_t pf_pair_score_at(const BloomEngineArena *arena,
                                const BloomPrefabInst64 *ia,
                                const BloomPrefabInst64 *ib,
                                int64_t px, int64_t py, int64_t pz) {
    int64_t da = pf_eval_inst_sdf_q16(arena, ia, px, py, pz);
    int64_t db = pf_eval_inst_sdf_q16(arena, ib, px, py, pz);
    if (da >= 0 && db >= 0) return da + db;
    return pf_max64(da, db);
}

int32_t bloom_scene_eval_pair_clearance_q16(const BloomEngineArena *arena,
                                            uint32_t slot_a,
                                            uint32_t slot_b,
                                            int32_t out_witness_q16[3],
                                            int32_t out_norm_q16[3]) {
    if (!arena || slot_a >= BLOOM_MAX_INSTANCES || slot_b >= BLOOM_MAX_INSTANCES || slot_a == slot_b) {
        return BLOOM_MAX_Q16;
    }
    const BloomPrefabInst64 *ia = &arena->map.instances[slot_a];
    const BloomPrefabInst64 *ib = &arena->map.instances[slot_b];
    if ((ia->flags & BLOOM_INST_FLAG_ACTIVE) == 0U || (ib->flags & BLOOM_INST_FLAG_ACTIVE) == 0U) {
        return BLOOM_MAX_Q16;
    }

    int64_t wx = ia->pos_q16[0], wy = ia->pos_q16[1], wz = ia->pos_q16[2];
    int64_t best_s = pf_pair_score_at(arena, ia, ib, wx, wy, wz);
    for (int32_t k = 1; k <= 8; ++k) {
        int64_t cx = (int64_t)ia->pos_q16[0] + ((((int64_t)ib->pos_q16[0] - (int64_t)ia->pos_q16[0]) * k) >> 3);
        int64_t cy = (int64_t)ia->pos_q16[1] + ((((int64_t)ib->pos_q16[1] - (int64_t)ia->pos_q16[1]) * k) >> 3);
        int64_t cz = (int64_t)ia->pos_q16[2] + ((((int64_t)ib->pos_q16[2] - (int64_t)ia->pos_q16[2]) * k) >> 3);
        int64_t s = pf_pair_score_at(arena, ia, ib, cx, cy, cz);
        if (s < best_s) {
            best_s = s;
            wx = cx; wy = cy; wz = cz;
        }
    }

    static const int64_t steps[3] = { 16384LL, 4096LL, 1024LL };
    for (uint32_t r = 0U; r < 3U; ++r) {
        int64_t h = steps[r];
        for (int axis = 0; axis < 3; ++axis) {
            for (int dir = -1; dir <= 1; dir += 2) {
                int64_t nx = wx + ((axis == 0) ? dir * h : 0);
                int64_t ny = wy + ((axis == 1) ? dir * h : 0);
                int64_t nz = wz + ((axis == 2) ? dir * h : 0);
                int64_t s = pf_pair_score_at(arena, ia, ib, nx, ny, nz);
                if (s < best_s) {
                    best_s = s;
                    wx = nx; wy = ny; wz = nz;
                }
            }
        }
    }

    if (out_witness_q16) {
        out_witness_q16[0] = pf_clamp64_to_32(wx, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
        out_witness_q16[1] = pf_clamp64_to_32(wy, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
        out_witness_q16[2] = pf_clamp64_to_32(wz, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
    }
    if (out_norm_q16) {
        int64_t eps = 1024LL;
        int64_t gx = pf_eval_inst_sdf_q16(arena, ia, wx + eps, wy, wz) - pf_eval_inst_sdf_q16(arena, ia, wx - eps, wy, wz);
        int64_t gy = pf_eval_inst_sdf_q16(arena, ia, wx, wy + eps, wz) - pf_eval_inst_sdf_q16(arena, ia, wx, wy - eps, wz);
        int64_t gz = pf_eval_inst_sdf_q16(arena, ia, wx, wy, wz + eps) - pf_eval_inst_sdf_q16(arena, ia, wx, wy, wz - eps);
        pf_norm3_q16(gx, gy, gz, out_norm_q16);
    }
    return pf_clamp64_to_32(best_s, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
}

static int32_t pf_inst_matter_id(const BloomEngineArena *arena, const BloomPrefabInst64 *inst) {
    uint32_t pid = inst->prefab_id;
    uint32_t mid = 0U;
    if (pid >= 1U && pid <= BLOOM_MAX_PREFABS) {
        mid = arena->map.prefabs[pid - 1U].matter_id;
    }
    if ((inst->flags & BLOOM_INST_FLAG_OVERRIDDEN) != 0U && inst->matter_override_id < BLOOM_MAX_MATTERS) {
        mid = inst->matter_override_id;
    }
    return (int32_t)(mid & 7U);
}

static int32_t pf_inst_ccd_radius_q16(const BloomEngineArena *arena, const BloomPrefabInst64 *inst) {
    uint32_t pid = inst->prefab_id;
    if (pid == 0U || pid > BLOOM_MAX_PREFABS) return BLOOM_Q16_ONE;
    const BloomPrefabDesc64 *pf = &arena->map.prefabs[pid - 1U];
    uint32_t p1 = pf->csg_op_mask & 0xFU;
    uint32_t p2 = (pf->csg_op_mask >> 4U) & 0xFU;
    int64_t r_base = (p1 == BLOOM_CSG_PRIM_SPHERE && p2 == 0U)
        ? (int64_t)pf->prim_params_q16[0]
        : (int64_t)pf->bounding_radius_q16;
    if (r_base <= 0) r_base = BLOOM_Q16_ONE;
    int64_t sc = (inst->scale_q16 > 4096)
        ? (int64_t)pf_clamp64_to_32(inst->scale_q16, 4096, 64 * BLOOM_Q16_ONE)
        : (int64_t)BLOOM_Q16_ONE;
    return pf_clamp64_to_32((r_base * sc) >> 16, 4096, 64 * BLOOM_Q16_ONE);
}

void bloom_inst_set_vel_q16(BloomPrefabInst64 *inst, const int32_t vel_q16[3]) {
    if (!inst) return;
    if (!vel_q16) {
        inst->kin_vel_packed = 0U;
        return;
    }
    uint32_t packed = 0U;
    bool nonzero = false;
    for (uint32_t a = 0U; a < 3U; ++a) {
        int32_t q4 = pf_clamp64_to_32((int64_t)vel_q16[a] / 4096LL, -511, 511);
        if (q4 != 0) nonzero = true;
        uint32_t u10 = ((uint32_t)(q4 + 1024)) & 0x3FFU;
        packed |= (u10 << (a * 10U));
    }
    if (nonzero) packed |= BLOOM_KIN_DYNAMIC;
    inst->kin_vel_packed = packed;
}

void bloom_inst_get_vel_q16(const BloomPrefabInst64 *inst, int32_t out_vel_q16[3]) {
    if (!out_vel_q16) return;
    if (!inst || (inst->kin_vel_packed & BLOOM_KIN_DYNAMIC) == 0U) {
        out_vel_q16[0] = 0; out_vel_q16[1] = 0; out_vel_q16[2] = 0;
        return;
    }
    uint32_t p = inst->kin_vel_packed;
    for (uint32_t a = 0U; a < 3U; ++a) {
        uint32_t raw = (p >> (a * 10U)) & 0x3FFU;
        int32_t s10 = ((raw & 0x200U) != 0U) ? ((int32_t)raw - 1024) : (int32_t)raw;
        out_vel_q16[a] = (int32_t)((int64_t)s10 * 4096LL);
    }
}

static void pf_conduct_pair_heat(BloomPrefabInst64 *ia_mut,
                                 BloomPrefabInst64 *ib,
                                 int32_t dt_q16,
                                 uint32_t *io_peak_k) {
    if (dt_q16 <= 0) return;
    int32_t ta = (int32_t)ia_mut->thermal_kelvin;
    int32_t tb = (int32_t)ib->thermal_kelvin;
    int32_t diff = ta - tb;
    if (diff >= 4 || diff <= -4) {
        int32_t rate = pf_clamp64_to_32(dt_q16, 1024, BLOOM_Q16_ONE);
        int32_t dT = (int32_t)(((int64_t)diff * (int64_t)rate) >> 18);
        if (dT == 0) dT = (diff > 0) ? 1 : -1;
        ia_mut->thermal_kelvin = (uint32_t)pf_clamp64_to_32((int64_t)ta - dT, 200, 4000);
        ib->thermal_kelvin = (uint32_t)pf_clamp64_to_32((int64_t)tb + dT, 200, 4000);
    }
    if (ia_mut->thermal_kelvin > *io_peak_k) *io_peak_k = ia_mut->thermal_kelvin;
    if (ib->thermal_kelvin > *io_peak_k) *io_peak_k = ib->thermal_kelvin;
}

void bloom_physics_step_arena64(BloomEngineArena *arena, int32_t dt_q16) {
    if (!arena) return;
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    BloomPhysicsSummary64 *ps = &arena->map.phys_summary;
    uint32_t inst_n = (hdr->inst_count > BLOOM_MAX_INSTANCES) ? BLOOM_MAX_INSTANCES : hdr->inst_count;

    int32_t step_dt = (dt_q16 > 0) ? pf_clamp64_to_32(dt_q16, 1, 4 * BLOOM_Q16_ONE) : 0;
    uint32_t epoch = ps->step_epoch + ((step_dt > 0) ? 1U : 0U);
    int32_t tol = (arena->map.section_spec.clearance_tol_q16 > 0)
        ? arena->map.section_spec.clearance_tol_q16
        : 13107;

    uint16_t inter_cnt = 0U;
    uint16_t warn_cnt = 0U;
    uint16_t ccd_cnt = 0U;
    uint16_t best_a = 0U, best_b = 0U;
    int32_t min_clr = BLOOM_MAX_Q16;
    int32_t max_pen = 0;
    int32_t min_toi = BLOOM_Q16_ONE;
    int32_t contact_pos[3] = { 0, 0, 0 };
    int32_t contact_norm[3] = { 0, BLOOM_Q16_ONE, 0 };
    uint32_t peak_k = 300U;
    uint32_t integrated_mask = 0U;

    for (uint32_t i = 0U; i < inst_n; ++i) {
        BloomPrefabInst64 *ia_mut = &arena->map.instances[i];
        if ((ia_mut->flags & BLOOM_INST_FLAG_ACTIVE) == 0U) continue;
        if (ia_mut->thermal_kelvin > peak_k) peak_k = ia_mut->thermal_kelvin;
        if ((ia_mut->flags & BLOOM_INST_FLAG_COLLIDABLE) == 0U) continue;

        for (uint32_t j = i + 1U; j < inst_n; ++j) {
            BloomPrefabInst64 *ib = &arena->map.instances[j];
            if ((ib->flags & (BLOOM_INST_FLAG_ACTIVE | BLOOM_INST_FLAG_COLLIDABLE)) !=
                (BLOOM_INST_FLAG_ACTIVE | BLOOM_INST_FLAG_COLLIDABLE)) {
                continue;
            }
            bool a_dyn = (ia_mut->kin_vel_packed & BLOOM_KIN_DYNAMIC) != 0U;
            bool b_dyn = (ib->kin_vel_packed & BLOOM_KIN_DYNAMIC) != 0U;

            if (step_dt > 0 && (a_dyn || b_dyn)) {
                int32_t va[3], vb[3], da[3], db[3], nrm[3];
                bloom_inst_get_vel_q16(ia_mut, va);
                bloom_inst_get_vel_q16(ib, vb);
                for (uint32_t k = 0U; k < 3U; ++k) {
                    da[k] = (int32_t)(((int64_t)va[k] * (int64_t)step_dt) >> 16);
                    db[k] = (int32_t)(((int64_t)vb[k] * (int64_t)step_dt) >> 16);
                }
                int32_t ra = pf_inst_ccd_radius_q16(arena, ia_mut);
                int32_t rb = pf_inst_ccd_radius_q16(arena, ib);
                int32_t toi = BLOOM_Q16_ONE;
                if (bloom_swept_sphere_ccd_q16(ia_mut->pos_q16, da, ra, ib->pos_q16, db, rb, &toi, nrm)) {
                    int64_t rvx = (int64_t)va[0] - (int64_t)vb[0];
                    int64_t rvy = (int64_t)va[1] - (int64_t)vb[1];
                    int64_t rvz = (int64_t)va[2] - (int64_t)vb[2];
                    int64_t vn_q16 = (rvx * nrm[0] + rvy * nrm[1] + rvz * nrm[2]) >> 16;
                    if (vn_q16 < 0) {
                        int32_t ma = pf_inst_matter_id(arena, ia_mut);
                        int32_t mb = pf_inst_matter_id(arena, ib);
                        int64_t qavg = ((int64_t)arena->map.matters[ma].viscosity_quench_q16 +
                                        (int64_t)arena->map.matters[mb].viscosity_quench_q16) / 2LL;
                        qavg = (qavg < 0) ? 0 : ((qavg > BLOOM_Q16_ONE) ? BLOOM_Q16_ONE : qavg);
                        int64_t rest = 16384LL + ((qavg * 42598LL) >> 16);
                        int64_t one_plus_e = BLOOM_Q16_ONE + rest;
                        int32_t va_post[3] = { va[0], va[1], va[2] };
                        int32_t vb_post[3] = { vb[0], vb[1], vb[2] };

                        if (a_dyn && b_dyn) {
                            int64_t hj = -((vn_q16 * one_plus_e) >> 17);
                            for (uint32_t k = 0U; k < 3U; ++k) {
                                int32_t imp = (int32_t)((hj * nrm[k]) >> 16);
                                va_post[k] = pf_clamp64_to_32((int64_t)va[k] + imp, -31 * BLOOM_Q16_ONE, 31 * BLOOM_Q16_ONE);
                                vb_post[k] = pf_clamp64_to_32((int64_t)vb[k] - imp, -31 * BLOOM_Q16_ONE, 31 * BLOOM_Q16_ONE);
                            }
                        } else if (a_dyn) {
                            int64_t fj = -((vn_q16 * one_plus_e) >> 16);
                            for (uint32_t k = 0U; k < 3U; ++k) {
                                va_post[k] = pf_clamp64_to_32((int64_t)va[k] + ((fj * nrm[k]) >> 16), -31 * BLOOM_Q16_ONE, 31 * BLOOM_Q16_ONE);
                            }
                        } else {
                            int64_t fj = -((vn_q16 * one_plus_e) >> 16);
                            for (uint32_t k = 0U; k < 3U; ++k) {
                                vb_post[k] = pf_clamp64_to_32((int64_t)vb[k] - ((fj * nrm[k]) >> 16), -31 * BLOOM_Q16_ONE, 31 * BLOOM_Q16_ONE);
                            }
                        }

                        int64_t rem = (int64_t)(BLOOM_Q16_ONE - toi);
                        int32_t pa_hit[3], pb_hit[3];
                        for (uint32_t k = 0U; k < 3U; ++k) {
                            pa_hit[k] = ia_mut->pos_q16[k] + (int32_t)(((int64_t)da[k] * toi) >> 16);
                            pb_hit[k] = ib->pos_q16[k] + (int32_t)(((int64_t)db[k] * toi) >> 16);
                            if (a_dyn) {
                                int64_t da_post = ((int64_t)va_post[k] * step_dt) >> 16;
                                ia_mut->pos_q16[k] = pf_clamp64_to_32((int64_t)pa_hit[k] + ((da_post * rem) >> 16), -64 * BLOOM_Q16_ONE, 64 * BLOOM_Q16_ONE);
                            }
                            if (b_dyn) {
                                int64_t db_post = ((int64_t)vb_post[k] * step_dt) >> 16;
                                ib->pos_q16[k] = pf_clamp64_to_32((int64_t)pb_hit[k] + ((db_post * rem) >> 16), -64 * BLOOM_Q16_ONE, 64 * BLOOM_Q16_ONE);
                            }
                        }
                        if (a_dyn) bloom_inst_set_vel_q16(ia_mut, va_post);
                        if (b_dyn) bloom_inst_set_vel_q16(ib, vb_post);
                        integrated_mask |= (1U << i) | (1U << j);
                        ++ccd_cnt;
                        pf_conduct_pair_heat(ia_mut, ib, step_dt, &peak_k);
                        if (toi <= min_toi) {
                            min_toi = toi;
                            if (min_clr > 0) min_clr = 0;
                            best_a = (uint16_t)(ia_mut->inst_id != 0U ? ia_mut->inst_id : (i + 1U));
                            best_b = (uint16_t)(ib->inst_id != 0U ? ib->inst_id : (j + 1U));
                            for (uint32_t k = 0U; k < 3U; ++k) {
                                contact_pos[k] = pb_hit[k] + (int32_t)(((int64_t)nrm[k] * rb) >> 16);
                                contact_norm[k] = nrm[k];
                            }
                        }
                        continue;
                    }
                }
            }

            int32_t wit[3] = { 0, 0, 0 }, nrm[3] = { 0, BLOOM_Q16_ONE, 0 };
            int32_t clr = bloom_scene_eval_pair_clearance_q16(arena, i, j, wit, nrm);
            if (clr < 0) {
                ++inter_cnt;
                int32_t pen = (clr == INT32_MIN) ? INT32_MAX : -clr;
                if (pen > max_pen) max_pen = pen;
            } else if (clr < tol) {
                ++warn_cnt;
            }
            if (clr <= 0) {
                ++ccd_cnt;
                min_toi = 0;
                pf_conduct_pair_heat(ia_mut, ib, step_dt, &peak_k);
            }
            if (clr < min_clr) {
                min_clr = clr;
                best_a = (uint16_t)(ia_mut->inst_id != 0U ? ia_mut->inst_id : (i + 1U));
                best_b = (uint16_t)(ib->inst_id != 0U ? ib->inst_id : (j + 1U));
                contact_pos[0] = wit[0]; contact_pos[1] = wit[1]; contact_pos[2] = wit[2];
                contact_norm[0] = nrm[0]; contact_norm[1] = nrm[1]; contact_norm[2] = nrm[2];
            }
        }
    }

    if (step_dt > 0) {
        for (uint32_t i = 0U; i < inst_n; ++i) {
            if ((integrated_mask & (1U << i)) != 0U) continue;
            BloomPrefabInst64 *inst = &arena->map.instances[i];
            if ((inst->flags & BLOOM_INST_FLAG_ACTIVE) == 0U) continue;
            if ((inst->kin_vel_packed & BLOOM_KIN_DYNAMIC) == 0U) continue;
            int32_t vel[3];
            bloom_inst_get_vel_q16(inst, vel);
            for (uint32_t k = 0U; k < 3U; ++k) {
                int64_t dp = ((int64_t)vel[k] * (int64_t)step_dt) >> 16;
                inst->pos_q16[k] = pf_clamp64_to_32((int64_t)inst->pos_q16[k] + dp, -64 * BLOOM_Q16_ONE, 64 * BLOOM_Q16_ONE);
            }
        }
    }

    uint16_t pflags = (inst_n > 0U) ? BLOOM_PHYS_FLAG_ACTIVE : 0U;
    if (inter_cnt > 0U) pflags |= BLOOM_PHYS_FLAG_INTERFERENCE;
    if (ccd_cnt > 0U) pflags |= BLOOM_PHYS_FLAG_CCD_HIT;
    if (peak_k >= BLOOM_THERMAL_ENTER_KELVIN) pflags |= BLOOM_PHYS_FLAG_THERMAL_HOT;

    ps->step_epoch = epoch;
    ps->interference_pairs = inter_cnt;
    ps->clearance_warn_pairs = warn_cnt;
    ps->ccd_hit_count = ccd_cnt;
    ps->pair_inst_a = best_a;
    ps->pair_inst_b = best_b;
    ps->phys_flags = pflags;
    ps->min_clearance_q16 = min_clr;
    ps->earliest_toi_q16 = min_toi;
    ps->peak_kelvin = peak_k;
    ps->max_penetration_q16 = max_pen;
    ps->contact_pos_q16[0] = contact_pos[0];
    ps->contact_pos_q16[1] = contact_pos[1];
    ps->contact_pos_q16[2] = contact_pos[2];
    ps->contact_norm_q16[0] = contact_norm[0];
    ps->contact_norm_q16[1] = contact_norm[1];
    ps->contact_norm_q16[2] = contact_norm[2];
    pf_stamp_mac(ps, 0xB4150064ULL, &ps->mac_lo32, &ps->mac_hi32);
}

void bloom_audio_init_rig(BloomAudioRig *rig, uint32_t sample_rate_hz) {
    if (!rig) return;
    memset(rig, 0, sizeof(*rig));
    uint32_t sr = sample_rate_hz;
    if (sr < 8000U) sr = BLOOM_AUDIO_SAMPLE_RATE;
    if (sr > 192000U) sr = 192000U;
    rig->hdr.sample_rate_hz = sr;
    rig->hdr.rng_state = 0x8000A51DU;
    rig->hdr.last_subframe_sample = 0xFFFFFFFFU;
    pf_stamp_mac(&rig->hdr, 0xA0D10064ULL, &rig->hdr.mac_lo32, &rig->hdr.mac_hi32);
}

void bloom_audio_excite_impact64(BloomAudioRig *rig,
                                 const BloomEngineArena *arena,
                                 uint32_t inst_slot,
                                 int32_t impact_speed_q16,
                                 int32_t pan_q16) {
    if (!rig || !arena) return;
    uint32_t sr = rig->hdr.sample_rate_hz;
    if (sr < 8000U || sr > 192000U) sr = BLOOM_AUDIO_SAMPLE_RATE;

    uint32_t mid = 0U;
    int32_t scale_q16 = BLOOM_Q16_ONE;
    uint32_t kelvin = 300U;
    if (inst_slot < BLOOM_MAX_INSTANCES) {
        const BloomPrefabInst64 *inst = &arena->map.instances[inst_slot];
        if ((inst->flags & BLOOM_INST_FLAG_ACTIVE) != 0U) {
            mid = (uint32_t)pf_inst_matter_id(arena, inst);
            if (inst->scale_q16 > 4096) scale_q16 = inst->scale_q16;
            if (inst->thermal_kelvin >= 200U && inst->thermal_kelvin <= 4000U) {
                kelvin = inst->thermal_kelvin;
            }
        }
    }
    const BloomMatter64 *mat = &arena->map.matters[mid & 7U];

    int64_t eta = pf_max64((int64_t)mat->ior_eta_q16, BLOOM_Q16_ONE);
    uint64_t dens = (mat->density_kg_m3 > 100U) ? (uint64_t)mat->density_kg_m3 : 2500ULL;
    int64_t sqrt_rho = (int64_t)pf_isqrt_u64(dens);
    if (sqrt_rho < 16) sqrt_rho = 16;

    /* Constitutive f0 from refractive/elastic stiffness, mass density, scale & thermal softening */
    int64_t base_hz = ((eta >> 6) * 52LL) / sqrt_rho;
    int64_t clamped_sc = (scale_q16 < 24576) ? 24576LL : ((scale_q16 > 163840) ? 163840LL : (int64_t)scale_q16);
    int64_t scale_f = (65536LL * 65536LL) / clamped_sc;
    int64_t thick_clamped = (mat->thickness_mm_q16 < 0) ? 0LL : ((mat->thickness_mm_q16 > 524288) ? 524288LL : (int64_t)mat->thickness_mm_q16);
    int64_t thick_f = 52428LL + (thick_clamped / 16LL);
    int32_t hot_excess = (kelvin > 300U) ? (int32_t)(kelvin - 300U) : 0;
    int64_t therm_f = 65536LL - (int64_t)hot_excess * 14LL;
    if (therm_f < 32768LL) therm_f = 32768LL;

    int64_t f0_hz = (((base_hz * scale_f) >> 16) * thick_f) >> 16;
    f0_hz = (f0_hz * therm_f) >> 16;
    int64_t f0_max = 3200LL;
    if ((arena->map.frame_hdr.flags & BLOOM_FLAG_METRIC_FIELD) != 0U) {
        int32_t beta = pf_clamp64_to_32((int64_t)arena->map.reg_bank.channels_q16[BLOOM_REG_LORENTZ_BETA], -62259, 62259);
        if (beta != 0) {
            uint64_t d_num = ((uint64_t)(65536LL + (int64_t)beta)) << 32ULL;
            uint64_t d_den = (uint64_t)(65536LL - (int64_t)beta);
            int64_t dop_q16 = (int64_t)pf_isqrt_u64(d_num / d_den);
            f0_hz = (f0_hz * dop_q16) >> 16;
            if (dop_q16 > BLOOM_Q16_ONE) f0_max = 6400LL;
        }
    }
    if (f0_hz < 160LL) f0_hz = 160LL;
    if (f0_hz > f0_max) f0_hz = f0_max;

    static const int32_t ratio_dielec[BLOOM_AUDIO_MODES]  = { 1024, 2822, 5534, 8120 };
    static const int32_t ratio_metal[BLOOM_AUDIO_MODES]   = { 1024, 2048, 3983, 6267 };
    static const int32_t ratio_amorph[BLOOM_AUDIO_MODES]  = { 1024, 1515, 2160, 3020 };
    const int32_t *ratios = ratio_amorph;
    if (mid == BLOOM_MATTER_CRYSTAL_GLASS || mid == BLOOM_MATTER_IMPERIAL_JADE || mid == BLOOM_MATTER_SAPPHIRE) {
        ratios = ratio_dielec;
    } else if (mid == BLOOM_MATTER_TITANIUM || mid == BLOOM_MATTER_AURUM_GOLD) {
        ratios = ratio_metal;
    }

    int64_t quench = (mat->viscosity_quench_q16 < 0) ? 0LL : ((mat->viscosity_quench_q16 > BLOOM_Q16_ONE) ? (int64_t)BLOOM_Q16_ONE : (int64_t)mat->viscosity_quench_q16);
    int64_t loss = (int64_t)BLOOM_Q16_ONE - quench + (int64_t)((mat->micro_roughness_q16 > 0) ? (mat->micro_roughness_q16 >> 1) : 0);

    uint32_t v_idx = rig->hdr.voice_cursor % BLOOM_AUDIO_MAX_VOICES;
    rig->hdr.voice_cursor = (v_idx + 1U) % BLOOM_AUDIO_MAX_VOICES;
    BloomAudioVoice64 *vc = &rig->voices[v_idx];
    memset(vc, 0, sizeof(*vc));
    vc->active = 1U;
    vc->matter_id = (uint16_t)mid;

    int32_t pan_c = pf_clamp64_to_32(pan_q16, -BLOOM_Q16_ONE, BLOOM_Q16_ONE);
    vc->pan_l_q14 = (int16_t)pf_clamp64_to_32((11585LL * (int64_t)(BLOOM_Q16_ONE - pan_c)) >> 16, 1024, 16384);
    vc->pan_r_q14 = (int16_t)pf_clamp64_to_32((11585LL * (int64_t)(BLOOM_Q16_ONE + pan_c)) >> 16, 1024, 16384);

    int32_t abs_spd = (impact_speed_q16 < 0) ? ((impact_speed_q16 == INT32_MIN) ? BLOOM_MAX_Q16 : -impact_speed_q16) : impact_speed_q16;
    int32_t amp_q24 = pf_clamp64_to_32(((int64_t)abs_spd * 128LL), 131072, 16777216);

    for (uint32_t m = 0U; m < BLOOM_AUDIO_MODES; ++m) {
        int64_t fm = (f0_hz * (int64_t)ratios[m]) >> 10;
        int64_t fmax = (int64_t)(sr / 3U);
        if (fm > fmax) fm = fmax;
        if (fm < 60LL) fm = 60LL;
        int32_t omega_q16 = (int32_t)((fm * 411775LL) / (int64_t)sr);
        vc->cos_q14[m] = (int16_t)(bloom_cos_q16(omega_q16) >> 2);
        vc->sin_q14[m] = (int16_t)(bloom_sin_q16(omega_q16) >> 2);

        int64_t ddrop = (loss >> 7) + (int64_t)m * 6LL + 4LL;
        int32_t dec = pf_clamp64_to_32(32766LL - ddrop, 30500, 32764);
        vc->decay_q15[m] = (uint16_t)dec;
        vc->u_q24[m] = amp_q24 / (int32_t)(m + 1U);
        vc->v_q24[m] = 0;
    }
    rig->hdr.total_impacts++;
}

uint32_t bloom_audio_step_frame64(BloomAudioRig *rig,
                                  const BloomEngineArena *arena,
                                  int16_t *out_stereo_pcm,
                                  uint32_t sample_frames) {
    if (!rig || !arena || !out_stereo_pcm || sample_frames == 0U) return 0U;
    const BloomPhysicsSummary64 *ps = &arena->map.phys_summary;

    uint32_t hit_sample = sample_frames;
    if (ps->step_epoch != rig->hdr.last_phys_epoch) {
        rig->hdr.last_phys_epoch = ps->step_epoch;
        if ((ps->phys_flags & BLOOM_PHYS_FLAG_CCD_HIT) != 0U) {
            int32_t toi_c = pf_clamp64_to_32(ps->earliest_toi_q16, 0, BLOOM_Q16_ONE - 1);
            hit_sample = (uint32_t)(((uint64_t)(uint32_t)toi_c * (uint64_t)sample_frames) >> 16ULL);
            if (hit_sample >= sample_frames) hit_sample = sample_frames - 1U;
            rig->hdr.last_subframe_sample = hit_sample;
        }
    }

    int32_t sec_dist = arena->map.section_spec.plane_dist_q16;
    int32_t sec_delta = (sec_dist >= rig->hdr.prev_sec_dist_q16)
        ? (sec_dist - rig->hdr.prev_sec_dist_q16)
        : (rig->hdr.prev_sec_dist_q16 - sec_dist);
    rig->hdr.prev_sec_dist_q16 = sec_dist;
    if (sec_delta > 64) {
        BloomAudioVoice64 *fvc = &rig->voices[BLOOM_AUDIO_MAX_VOICES - 1U];
        fvc->active = 1U;
        fvc->matter_id = BLOOM_MATTER_TITANIUM;
        fvc->pan_l_q14 = 11585;
        fvc->pan_r_q14 = 11585;
        int32_t omega = (int32_t)((1240LL * 411775LL) / (int64_t)(rig->hdr.sample_rate_hz >= 8000U ? rig->hdr.sample_rate_hz : BLOOM_AUDIO_SAMPLE_RATE));
        fvc->cos_q14[0] = (int16_t)(bloom_cos_q16(omega) >> 2);
        fvc->sin_q14[0] = (int16_t)(bloom_sin_q16(omega) >> 2);
        fvc->decay_q15[0] = 32400U;
        uint32_t xr = rig->hdr.rng_state;
        xr ^= xr << 13U;
        xr ^= xr >> 17U;
        xr ^= xr << 5U;
        rig->hdr.rng_state = (xr != 0U) ? xr : 0x8000A51DU;
        int32_t famp = pf_clamp64_to_32((int64_t)sec_delta * 96LL, 32768, 2097152);
        fvc->u_q24[0] += ((xr & 1U) != 0U) ? famp : -famp;
    }

    int32_t peak = 0;
    for (uint32_t s = 0U; s < sample_frames; ++s) {
        if (s == hit_sample) {
            uint32_t ia = (ps->pair_inst_a > 0U) ? (uint32_t)(ps->pair_inst_a - 1U) : 0U;
            uint32_t ib = (ps->pair_inst_b > 0U) ? (uint32_t)(ps->pair_inst_b - 1U) : 1U;
            int32_t pan = pf_clamp64_to_32((int64_t)ps->contact_pos_q16[0] >> 2, -BLOOM_Q16_ONE, BLOOM_Q16_ONE);
            int32_t imp_spd = BLOOM_Q16_ONE * 2;
            bloom_audio_excite_impact64(rig, arena, ia, imp_spd, pan);
            if (ib != ia) {
                bloom_audio_excite_impact64(rig, arena, ib, (imp_spd * 3) / 4, -pan);
            }
        }

        int64_t acc_l = 0;
        int64_t acc_r = 0;
        for (uint32_t v = 0U; v < BLOOM_AUDIO_MAX_VOICES; ++v) {
            BloomAudioVoice64 *vc = &rig->voices[v];
            if (vc->active == 0U) continue;
            int64_t vsig = 0;
            int64_t energy = 0;
            for (uint32_t m = 0U; m < BLOOM_AUDIO_MODES; ++m) {
                if (vc->decay_q15[m] == 0U) continue;
                int64_t u = (int64_t)vc->u_q24[m];
                int64_t vv = (int64_t)vc->v_q24[m];
                int64_t un = (u * (int64_t)vc->cos_q14[m] - vv * (int64_t)vc->sin_q14[m]) >> 14;
                int64_t vn = (u * (int64_t)vc->sin_q14[m] + vv * (int64_t)vc->cos_q14[m]) >> 14;
                vc->u_q24[m] = (int32_t)((un * (int64_t)vc->decay_q15[m]) >> 15);
                vc->v_q24[m] = (int32_t)((vn * (int64_t)vc->decay_q15[m]) >> 15);
                vsig += vc->u_q24[m];
                energy += (int64_t)pf_abs64(vc->u_q24[m]) + (int64_t)pf_abs64(vc->v_q24[m]);
            }
            if (energy < 64LL) {
                vc->active = 0U;
                continue;
            }
            int64_t pcm_s = vsig >> 10; /* Q24 -> Q14 scale */
            acc_l += (pcm_s * (int64_t)vc->pan_l_q14) >> 14;
            acc_r += (pcm_s * (int64_t)vc->pan_r_q14) >> 14;
        }

        if ((arena->map.frame_hdr.flags & BLOOM_FLAG_GAMEPLAY_SHELLS) != 0U &&
            arena->map.reg_bank.channels_q16[BLOOM_REG_HITSTOP_FRAMES] > 1) {
            acc_l = (acc_l * 8231LL) / 65536LL;
            acc_r = (acc_r * 8231LL) / 65536LL;
        }

        int32_t sl = pf_clamp64_to_32(acc_l, -32767, 32767);
        int32_t sr = pf_clamp64_to_32(acc_r, -32767, 32767);
        int32_t al = (sl < 0) ? -sl : sl;
        int32_t ar = (sr < 0) ? -sr : sr;
        if (al > peak) peak = al;
        if (ar > peak) peak = ar;
        out_stereo_pcm[2U * s + 0U] = (int16_t)sl;
        out_stereo_pcm[2U * s + 1U] = (int16_t)sr;
    }

    rig->hdr.peak_abs_pcm = peak;
    pf_stamp_mac(&rig->hdr, 0xA0D10064ULL, &rig->hdr.mac_lo32, &rig->hdr.mac_hi32);
    return hit_sample;
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

bool bloom_arena_heal_and_swap(BloomEngineArena *arena,
                               uint32_t flow_slot,
                               const BloomBilevelTerm *terms,
                               uint32_t term_count,
                               uint64_t kan_bit_pin,
                               int64_t u_ext_q16,
                               int64_t target_x_q16,
                               uint32_t jet_steps,
                               uint64_t *io_seed,
                               uint32_t *io_epoch,
                               uint32_t max_iters) {
    if (!arena || flow_slot >= BLOOM_MAX_FLOW_STATES || !terms || term_count == 0U) {
        return false;
    }
    const uint64_t *st = arena->cells[BLOOM_SLOT_FLOW_STATE_BASE + flow_slot].u64;
    if (((st[6] & (uint64_t)FLOW_STATUS_CONVERGED) != UINT64_C(0)) || (st[4] != UINT64_C(0))) {
        return false;
    }
    uint32_t n = (term_count > SEARU_BILEVEL_MAX_VARS) ? SEARU_BILEVEL_MAX_VARS : term_count;
    searu_bilevel_term s_terms[SEARU_BILEVEL_MAX_VARS];
    for (uint32_t i = 0U; i < n; ++i) {
        s_terms[i].db0_q16 = terms[i].db0_q16;
        s_terms[i].db1_q16 = terms[i].db1_q16;
        s_terms[i].db2_q16 = terms[i].db2_q16;
        s_terms[i].weight_cost = terms[i].weight_cost;
    }
    flow_cell *charts = (flow_cell *)arena->cells[BLOOM_SLOT_FLOW_CHART_BASE].u64;
    return searu_heal_and_swap(charts, BLOOM_FLOW_CHART_MASK, st, s_terms, n,
                               kan_bit_pin, u_ext_q16, target_x_q16, jet_steps,
                               io_seed, io_epoch, max_iters, NULL);
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
    bloom_physics_step_arena64(arena, 0);

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
    for (uint32_t c = 0U; c < BLOOM_ARENA_CELLS; ++c) {
        if (c == BLOOM_SLOT_IMAGE_HDR) continue;
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

void bloom_arena_sha256_hex(const BloomEngineArena *arena, char out_hex65[65]) {
    if (!out_hex65) return;
    if (!arena) {
        out_hex65[0] = '\0';
        return;
    }
    flow_sha256_bytes_hex(arena, sizeof(*arena), out_hex65);
}

void bloom_mesh_sha256_hex(const BloomMeshArena *mesh, char out_hex65[65]) {
    if (!out_hex65) return;
    if (!mesh) {
        out_hex65[0] = '\0';
        return;
    }
    flow_sha256_bytes_hex(mesh, sizeof(*mesh), out_hex65);
}

static void pf_set_ir_input(flow_ir_prog_t *p, uint32_t idx, uint16_t width, const char *name) {
    p->inputs[idx] = idx;
    p->vals[idx].width = width;
    p->vals[idx].is_classical = 0U;
    for (size_t i = 0U; i < 24U; ++i) {
        p->vals[idx].name[i] = '\0';
    }
    for (size_t i = 0U; i < 23U && name[i] != '\0'; ++i) {
        p->vals[idx].name[i] = name[i];
    }
}

bool bloom_fir_certify_arena_contracts(flow_ir_builder_ws_t *ws,
                                       uint32_t *out_mult_bits,
                                       uint32_t *out_mult_depth,
                                       size_t *out_wire_len) {
    if (!ws) return false;
    flow_ir_builder_init(ws, "bloom_arena_qtt");
    flow_ir_prog_t *p = &ws->ir_prog;
    p->num_inputs = 4U;
    p->num_vals = 4U;
    pf_set_ir_input(p, 0U, 16U, "slot");
    pf_set_ir_input(p, 1U, 16U, "mode_cur");
    pf_set_ir_input(p, 2U, 16U, "mode_req");
    pf_set_ir_input(p, 3U, 1U, "kan_pin");

    uint32_t c_slot_mask = flow_ir_emit_const(p, 16U, UINT64_C(0x00FF), "c_slot_mask");
    uint32_t slot_safe   = flow_ir_emit_op2(p, FLOW_IR_OP_AND_VEC, 16U, 0U, c_slot_mask, "slot_safe");
    uint32_t c_hi_u8     = flow_ir_emit_const(p, 16U, UINT64_C(0xFF00), "c_hi_u8");
    uint32_t slot_hi     = flow_ir_emit_op2(p, FLOW_IR_OP_AND_VEC, 16U, slot_safe, c_hi_u8, "slot_hi");
    uint32_t slot_ok     = flow_ir_emit_op1(p, FLOW_IR_OP_EQ_ZERO, 1U, slot_hi, 0ULL, "slot_ok");

    uint32_t c_one     = flow_ir_emit_const(p, 16U, UINT64_C(1), "c_one");
    uint32_t c_hi_mode = flow_ir_emit_const(p, 16U, UINT64_C(0xFFFE), "c_hi_mode");
    uint32_t cur_b0    = flow_ir_emit_op2(p, FLOW_IR_OP_AND_VEC, 16U, 1U, c_one, "cur_b0");
    uint32_t req_b0    = flow_ir_emit_op2(p, FLOW_IR_OP_AND_VEC, 16U, 2U, c_one, "req_b0");
    uint32_t req_hi    = flow_ir_emit_op2(p, FLOW_IR_OP_AND_VEC, 16U, 2U, c_hi_mode, "req_hi");
    uint32_t pin_b0    = flow_ir_emit_op1(p, FLOW_IR_OP_SLICE, 1U, 3U, 0ULL, "pin_b0");
    uint32_t next_b0   = flow_ir_emit_sel(p, 16U, pin_b0, req_b0, cur_b0, "next_b0");
    uint32_t next_mode = flow_ir_emit_op2(p, FLOW_IR_OP_XOR_VEC, 16U, req_hi, next_b0, "next_mode");

    if (slot_ok == UINT32_MAX || next_mode == UINT32_MAX) {
        return false;
    }
    p->outputs[0] = slot_safe;
    p->outputs[1] = next_mode;
    p->outputs[2] = slot_ok;
    p->num_outputs = 3U;

    if (!flow_ir_builder_seal(ws)) {
        return false;
    }

    ws->ir_eval_ok = ws->ir_linear_ok && ws->ir_unitary_ok;
    if (out_mult_bits) *out_mult_bits = ws->mult_bits;
    if (out_mult_depth) *out_mult_depth = ws->mult_depth;
    if (out_wire_len) *out_wire_len = ws->wire_len;
    return ws->ir_eval_ok;
}

void bloom_gameplay_init64(BloomGameplayState64 *gp,
                           uint32_t player_inst_id,
                           int32_t hurt_q16,
                           int32_t graze_q16,
                           int32_t hit_q16) {
    if (!gp) return;
    memset(gp, 0, sizeof(*gp));
    gp->player_inst_id = (player_inst_id > 0U) ? player_inst_id : 1U;
    gp->delta_hurt_q16 = (hurt_q16 >= 2048) ? hurt_q16 : 2048;
    gp->delta_graze_q16 = (graze_q16 >= 4096) ? graze_q16 : 4096;
    gp->delta_hit_q16 = (hit_q16 >= gp->delta_graze_q16) ? hit_q16 : gp->delta_graze_q16;
    gp->hyst_enter_q16 = gp->delta_graze_q16;
    gp->hyst_exit_q16 = pf_clamp64_to_32((int64_t)gp->delta_graze_q16 + 8192LL, 4096, BLOOM_MAX_Q16);
    gp->proper_time_q16 = BLOOM_Q16_ONE;
    gp->audio_duck_q16 = BLOOM_Q16_ONE;
    pf_stamp_mac(gp, 0x6A3E9100ULL, &gp->mac_lo32, &gp->mac_hi32);
}

uint32_t bloom_gameplay_step64(BloomGameplayState64 *gp,
                               BloomEngineArena *arena,
                               BloomAudioRig *opt_audio,
                               const int32_t fulcrum_imp_q16[3],
                               bool trigger_action) {
    if (!gp || !arena) return 0U;

    uint32_t inst_n = (arena->map.frame_hdr.inst_count > BLOOM_MAX_INSTANCES)
        ? BLOOM_MAX_INSTANCES : arena->map.frame_hdr.inst_count;
    if (inst_n == 0U) return 0U;

    uint32_t player_slot = UINT32_MAX;
    for (uint32_t i = 0U; i < inst_n; ++i) {
        if ((arena->map.instances[i].flags & BLOOM_INST_FLAG_ACTIVE) != 0U &&
            arena->map.instances[i].inst_id == gp->player_inst_id) {
            player_slot = i;
            break;
        }
    }
    if (player_slot == UINT32_MAX) {
        if ((arena->map.instances[0].flags & BLOOM_INST_FLAG_ACTIVE) == 0U) return 0U;
        player_slot = 0U;
    }

    if (trigger_action) {
        gp->input_buf_frames = 6U;
        gp->buffered_action = 1U;
        if (fulcrum_imp_q16) {
            gp->buffered_dir_q16[0] = fulcrum_imp_q16[0];
            gp->buffered_dir_q16[1] = fulcrum_imp_q16[2];
        }
    } else if (gp->input_buf_frames > 0U) {
        gp->input_buf_frames--;
        if (gp->input_buf_frames == 0U) {
            gp->buffered_action = 0U;
        }
    }

    int32_t min_clr = BLOOM_MAX_Q16;
    uint32_t nearest_slot = UINT32_MAX;
    for (uint32_t j = 0U; j < inst_n; ++j) {
        if (j == player_slot) continue;
        const BloomPrefabInst64 *oj = &arena->map.instances[j];
        if ((oj->flags & (BLOOM_INST_FLAG_ACTIVE | BLOOM_INST_FLAG_COLLIDABLE)) !=
            (BLOOM_INST_FLAG_ACTIVE | BLOOM_INST_FLAG_COLLIDABLE)) {
            continue;
        }
        int32_t wit[3], nrm[3];
        int32_t clr = bloom_scene_eval_pair_clearance_q16(arena, player_slot, j, wit, nrm);
        if (clr < min_clr) {
            min_clr = clr;
            nearest_slot = j;
        }
    }

    uint32_t events = 0U;

    /* Lever / fulcrum reciprocal impulse inside concentric hit shell */
    if (gp->buffered_action != 0U && nearest_slot != UINT32_MAX && min_clr <= gp->delta_hit_q16) {
        int32_t imp[3] = {
            fulcrum_imp_q16 ? fulcrum_imp_q16[0] : gp->buffered_dir_q16[0],
            fulcrum_imp_q16 ? fulcrum_imp_q16[1] : 0,
            fulcrum_imp_q16 ? fulcrum_imp_q16[2] : gp->buffered_dir_q16[1]
        };
        int32_t vp[3], vj[3];
        bloom_inst_get_vel_q16(&arena->map.instances[player_slot], vp);
        bloom_inst_get_vel_q16(&arena->map.instances[nearest_slot], vj);
        for (uint32_t a = 0U; a < 3U; ++a) {
            vp[a] = pf_clamp64_to_32((int64_t)vp[a] + (int64_t)imp[a], -31 * BLOOM_Q16_ONE, 31 * BLOOM_Q16_ONE);
            vj[a] = pf_clamp64_to_32((int64_t)vj[a] - (int64_t)imp[a], -31 * BLOOM_Q16_ONE, 31 * BLOOM_Q16_ONE);
        }
        bloom_inst_set_vel_q16(&arena->map.instances[player_slot], vp);
        bloom_inst_set_vel_q16(&arena->map.instances[nearest_slot], vj);
        gp->input_buf_frames = 0U;
        gp->buffered_action = 0U;
        events |= BLOOM_GP_EVENT_FULCRUM;
    }

    /* Concentric SDF shell probes + dual-threshold graze hysteresis + -18dB Hitstop contrast */
    if (nearest_slot != UINT32_MAX && min_clr < -gp->delta_hurt_q16) {
        gp->lethal_hits++;
        gp->hitstop_frames = 4U;
        gp->proper_time_q16 = 4096; /* 1/16x stasis dilation */
        gp->audio_duck_q16 = 8231;  /* -18 dB */
        events |= BLOOM_GP_EVENT_HITSTOP;
        arena->map.reg_bank.channels_q16[BLOOM_REG_CINE_TRAUMA] = 49152;
    } else {
        if (nearest_slot != UINT32_MAX) {
            if (min_clr <= gp->hyst_enter_q16) {
                if (gp->graze_latched == 0U) {
                    gp->graze_latched = 1U;
                    gp->graze_score++;
                    events |= BLOOM_GP_EVENT_GRAZE;
                    if (opt_audio) {
                        bloom_audio_excite_impact64(opt_audio, arena, player_slot, 2 * BLOOM_Q16_ONE, 0);
                    }
                }
            } else if (min_clr >= gp->hyst_exit_q16) {
                gp->graze_latched = 0U;
            }
        }

        if (gp->hitstop_frames > 1U) {
            gp->hitstop_frames--;
            gp->proper_time_q16 = 4096;
            gp->audio_duck_q16 = 8231;
        } else if (gp->hitstop_frames == 1U) {
            gp->hitstop_frames = 0U;
            gp->proper_time_q16 = BLOOM_Q16_ONE;
            gp->audio_duck_q16 = BLOOM_Q16_ONE;
            if (opt_audio) {
                bloom_audio_excite_impact64(opt_audio, arena, player_slot, 8 * BLOOM_Q16_ONE, 0);
            }
        } else {
            gp->proper_time_q16 = BLOOM_Q16_ONE;
            gp->audio_duck_q16 = BLOOM_Q16_ONE;
        }
    }

    arena->map.frame_hdr.flags |= BLOOM_FLAG_GAMEPLAY_SHELLS;
    arena->map.reg_bank.channels_q16[BLOOM_REG_HITSTOP_FRAMES] = (int32_t)gp->hitstop_frames;
    arena->map.reg_bank.channels_q16[BLOOM_REG_GRAZE_SHELL] = gp->delta_graze_q16;
    arena->map.reg_bank.channels_q16[BLOOM_REG_GRAZE_COUNT] = (int32_t)(gp->graze_score & 0x7FFFFFFFU);
    bloom_physics_step_arena64(arena, gp->proper_time_q16);
    pf_stamp_mac(gp, 0x6A3E9100ULL, &gp->mac_lo32, &gp->mac_hi32);
    return events;
}

void bloom_rewind_init64(BloomRewindCtrl64 *ctrl, uint32_t capacity) {
    if (!ctrl) return;
    memset(ctrl, 0, sizeof(*ctrl));
    uint32_t cap = (capacity == 0U) ? 1U : ((capacity > BLOOM_REWIND_RING_CAP) ? BLOOM_REWIND_RING_CAP : capacity);
    ctrl->capacity = cap;
    pf_stamp_mac(ctrl, 0x4E51D064ULL, &ctrl->mac_lo32, &ctrl->mac_hi32);
}

void bloom_rewind_push(BloomRewindCtrl64 *ctrl,
                       BloomSpacetimeSnap *ring_buf,
                       const BloomEngineArena *arena,
                       const BloomAudioRig *audio) {
    if (!ctrl || !ring_buf || !arena) return;
    uint32_t cap = (ctrl->capacity >= 1U && ctrl->capacity <= BLOOM_REWIND_RING_CAP)
        ? ctrl->capacity : BLOOM_REWIND_RING_CAP;
    uint32_t idx = ctrl->head % cap;
    memcpy(&ring_buf[idx].arena, arena, sizeof(BloomEngineArena));
    if (audio) {
        memcpy(&ring_buf[idx].audio, audio, sizeof(BloomAudioRig));
    } else {
        memset(&ring_buf[idx].audio, 0, sizeof(BloomAudioRig));
    }
    ctrl->head = (idx + 1U) % cap;
    if (ctrl->count < cap) {
        ctrl->count++;
    }
    ctrl->cursor = 0U;
    pf_stamp_mac(ctrl, 0x4E51D064ULL, &ctrl->mac_lo32, &ctrl->mac_hi32);
}

bool bloom_rewind_restore(BloomRewindCtrl64 *ctrl,
                          const BloomSpacetimeSnap *ring_buf,
                          uint32_t steps_back,
                          BloomEngineArena *out_arena,
                          BloomAudioRig *out_audio) {
    if (!ctrl || !ring_buf || !out_arena || ctrl->count == 0U || steps_back >= ctrl->count) {
        return false;
    }
    uint32_t cap = (ctrl->capacity >= 1U && ctrl->capacity <= BLOOM_REWIND_RING_CAP)
        ? ctrl->capacity : BLOOM_REWIND_RING_CAP;
    uint32_t slot = (ctrl->head + cap - 1U - (steps_back % cap)) % cap;
    memcpy(out_arena, &ring_buf[slot].arena, sizeof(BloomEngineArena));
    if (out_audio) {
        memcpy(out_audio, &ring_buf[slot].audio, sizeof(BloomAudioRig));
    }
    ctrl->head = (slot + 1U) % cap;
    ctrl->count -= steps_back;
    ctrl->cursor = steps_back;
    ctrl->branch_epoch++;
    ctrl->total_rewinds++;
    pf_stamp_mac(ctrl, 0x4E51D064ULL, &ctrl->mac_lo32, &ctrl->mac_hi32);
    return true;
}
