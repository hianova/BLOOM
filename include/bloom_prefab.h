#ifndef BLOOM_PREFAB_H
#define BLOOM_PREFAB_H

#include "bloom_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Pillar 3 ("Prefab SSOT"): Reusable 64B Blueprints, Live Non-Destructive
 * Overrides, Revert-to-SSOT, X-Ray Cutaway, Wave64 LOD, TRC Guard & .bloom Mmap
 * ============================================================================
 */

/* Projective LOD Layers */
#define BLOOM_LAYER_0_SDF   0U /* Full 3D CSG / BVH SDF Raymarching */
#define BLOOM_LAYER_1_SLICE 1U /* Mid-Distance 2D Optical Slice */
#define BLOOM_LAYER_2_HUD   2U /* Far-Distance Vector Glyph Marker */

/* Harding / Console TRC Photosensitive Flash Limit: <= 3.0 Hz (196,608 Q16) */
#define BLOOM_TRC_MAX_STROBE_HZ_Q16 (3 * BLOOM_Q16_ONE)

/* Thermal Hysteresis Thresholds */
#define BLOOM_THERMAL_ENTER_KELVIN 800U
#define BLOOM_THERMAL_EXIT_KELVIN  760U

/* Canonical Prefab IDs (1..8) */
#define BLOOM_PREFAB_CRYSTAL_SPHERE   1U
#define BLOOM_PREFAB_QUANTUM_TORUS    2U
#define BLOOM_PREFAB_CITADEL_CORE     3U
#define BLOOM_PREFAB_OCTA_PRISM       4U
#define BLOOM_PREFAB_CAPSULE_PILLAR   5U
#define BLOOM_PREFAB_JADE_VESSEL      6U
#define BLOOM_PREFAB_AURUM_MONOLITH   7U
#define BLOOM_PREFAB_THERMAL_REACTOR  8U

/* CSG Op Mask Packing: Prim1(4b) | Prim2(4b) | Op(4b) | Polar(6b) | Grade(4b) */
#define BLOOM_CSG_MASK_PACK(prim1, prim2, op, polar, grade)                    \
    ((((uint32_t)(prim1) & 0xFU) << 0U) |                                      \
     (((uint32_t)(prim2) & 0xFU) << 4U) |                                      \
     (((uint32_t)(op) & 0xFU) << 8U) |                                         \
     (((uint32_t)(polar) & 0x3FU) << 12U) |                                    \
     (((uint32_t)(grade) & 0xFU) << 18U))

/* 1. Canonical SSOT Tables Initialization & Lookup */
void bloom_prefab_init_canonical(BloomArtStyle64 out_styles[BLOOM_MAX_STYLES],
                                 BloomPrefabDesc64 out_prefabs[BLOOM_MAX_PREFABS]);

uint64_t bloom_prefab_calc_hash(const BloomPrefabDesc64 *prefab);

/* 2. Algebraic CSG Composition & SDF Evaluation */
BloomPrefabDesc64 bloom_prefab_join(const BloomPrefabDesc64 *a,
                                    const BloomPrefabDesc64 *b,
                                    uint32_t new_prefab_id,
                                    uint32_t csg_bool_op);

int64_t bloom_prefab_eval_sdf_q16(const BloomPrefabDesc64 *prefab,
                                  int64_t lx_q16,
                                  int64_t ly_q16,
                                  int64_t lz_q16,
                                  int32_t param_delta_q16);

int64_t bloom_prefab_eval_xray_cutaway_q16(int64_t base_sdf_q16,
                                           int64_t world_x_q16,
                                           int32_t xray_plane_q16,
                                           bool xray_enabled);

/* 3. TRC Accessibility Guard (<= 3.0 Hz Flash Clamp + Colorblind Geom Freq) */
BloomArtStyle64 bloom_style_apply_trc_guard(BloomArtStyle64 style,
                                            bool photosensitive_safe,
                                            bool colorblind_geom);

BloomArtStyle64 bloom_style_resolve_thermal(const BloomArtStyle64 styles[BLOOM_MAX_STYLES],
                                            uint32_t style_id,
                                            uint32_t thermal_kelvin,
                                            bool prev_overheated,
                                            bool photosensitive_safe,
                                            bool colorblind_geom,
                                            bool *out_overheated);

/* 4. Scene Instancing, Live Non-Destructive Override & O(1) Revert to SSOT */
uint32_t bloom_prefab_spawn_inst(BloomEngineArena *arena,
                                 uint32_t prefab_id,
                                 const int32_t pos_q16[3],
                                 int32_t scale_q16);

void bloom_prefab_override_inst(BloomPrefabInst64 *inst,
                                uint32_t style_override_id,
                                uint32_t matter_override_id,
                                int32_t param_override_q16);

void bloom_prefab_revert_to_ssot(BloomPrefabInst64 *inst,
                                 const BloomPrefabDesc64 *ssot_prefab);

/* 5. Wave64 Distance LOD Compaction */
uint32_t bloom_prefab_cull_wave64(const BloomPrefabInst64 *instances,
                                  size_t count,
                                  const int32_t eye_q16[3],
                                  int32_t layer0_r_q16,
                                  int32_t layer1_r_q16,
                                  BloomPrefabInst64 *out_compacted,
                                  uint64_t *out_active_mask);

/* 6. FLOW v6.0.0 Arena Init, @swap Safe Chart Switch & Full Frame Step (with 0-FPS Idle) */
void bloom_arena_init_default(BloomEngineArena *arena, uint32_t width_px, uint32_t height_px);

bool bloom_arena_try_swap_chart(BloomEngineArena *arena,
                                uint32_t flow_slot,
                                uint32_t target_chart_idx,
                                uint16_t mode_req);

bool bloom_arena_step_dynamics(BloomEngineArena *arena, uint16_t mode_req);

/* 7. Unified .bloom 64B Container Export / Validate (Zero-Parse Mmap Ready) */
void bloom_image_seal_arena(BloomEngineArena *arena);
bool bloom_image_verify_arena(const BloomEngineArena *arena);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_PREFAB_H */
