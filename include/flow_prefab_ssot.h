#ifndef FLOW_PREFAB_SSOT_H
#define FLOW_PREFAB_SSOT_H

/*
 * ============================================================================
 * BLOOM: SSOT + FlowPrefabDesc64 + FlowArtStyle64 (Art Bible)
 * ============================================================================
 * The "HTML / CSS / JS" Triad for BLOOM:
 *   - JS   (Causality & Conservation): FLOW v4 + Ricardian v4
 *   - HTML (Structure & Composition):  SSOT + FlowPrefabDesc64 (64B CSG/SDF)
 *   - CSS  (Visual & Optical Rules):   FlowArtStyle64 (64B Art Bible, zero textures!)
 *
 * Strict Mandates:
 *   1. Zero float / double anywhere: 100% pure C17 with int64_t / int32_t Q48.16,
 *      uint64_t, and __int128.
 *   2. 64-Byte Cache-Line Univalence: FlowArtStyle64, FlowPrefabDesc64,
 *      FlowPrefabInstance64 are all exact 64 bytes, 64-byte aligned, and proven
 *      isomorphic via FLOW_STATIC_ASSERT_UNIVALENT.
 *   3. L1 Cache Resident: Canonical SSOT tables fit within <6 KB L1 D-Cache budget.
 * ============================================================================
 */

#include "flow_asm.h"
#include "flow_hott_jet.h"
#include "flow_io.h"
#include "ricardian.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * ----------------------------------------------------------------------------
 * 1. Visual & Optical Rules: FlowArtStyle64 (Art Bible, Exact 64 Bytes)
 * ----------------------------------------------------------------------------
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t style_id;            /* Unique Art Bible Style ID */
    uint32_t base_rgb_hex;         /* 0x00RRGGBB base albedo color */
    uint32_t accent_rgb_hex;       /* 0x00RRGGBB emissive/accent trim color */
    int32_t roughness_q16;         /* Surface roughness [0..65536] in Q16 */
    int32_t metallic_q16;          /* Metalness [0..65536] in Q16 */
    int32_t emissive_gain_q16;     /* Emissive light gain multiplier in Q16 */
    int32_t sdf_noise_freq_q16;    /* Procedural SDF displacement frequency in Q16 */
    int32_t sdf_noise_amp_q16;     /* Procedural SDF displacement amplitude in Q16 */
    int32_t msdf_stroke_q16;       /* MSDF outline / stroke width in Q16 */
    int32_t strobe_hz_q16;         /* Thermal / emergency strobe frequency in Q16 Hz */
    int32_t thermal_shift_q16;     /* Thermal color-temperature Kelvin shift in Q16 */
    uint32_t flags;                /* Optical flags (glow, wireframe, pulse) */
    uint32_t reserved[4];          /* Padding to exact 64 bytes */
} FlowArtStyle64;

typedef struct __attribute__((aligned(64))) {
    int64_t q_word[8];
} FlowArtStyleShadow64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowArtStyle64, FlowArtStyleShadow64);
FLOW_STATIC_ASSERT_UNIVALENT(FlowArtStyle64, RicardianStateV4);
_Static_assert(sizeof(FlowArtStyle64) == 64U, "FlowArtStyle64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowArtStyle64) == 64U, "FlowArtStyle64 must be 64-byte aligned");

/*
 * ----------------------------------------------------------------------------
 * 2. Semantic Structure: FlowPrefabDesc64 (Algebraic CSG/SDF, Exact 64 Bytes)
 * ----------------------------------------------------------------------------
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t prefab_id;            /* Unique Prefab ID */
    uint32_t art_style_id;         /* Default Art Bible Style reference */
    uint16_t ricardian_grade;      /* 0..4 for GRADE_S..GRADE_D */
    uint16_t assembly_index;       /* Ricardian Assembly Theory depth */
    uint32_t csg_op_mask;          /* Prim1(4b) | Prim2(4b) | Op(4b) | Polar(6b) */
    int32_t bounding_radius_q16;   /* Bounding sphere radius in Q16 */
    int32_t prim_params_q16[6];    /* Algebraic parameters in Q16 */
    uint32_t msdf_glyph_id;        /* Fallback MSDF glyph ID for HUD / Layer 2 */
    uint32_t flags;                /* Prefab behavioral flags */
    uint32_t reserved;             /* Padding for 64-bit alignment */
    uint64_t ssot_hash;            /* 64-bit SSOT compile-time / init integrity hash */
} FlowPrefabDesc64;

typedef struct __attribute__((aligned(64))) {
    int64_t q_word[8];
} FlowPrefabDescShadow64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowPrefabDesc64, FlowPrefabDescShadow64);
FLOW_STATIC_ASSERT_UNIVALENT(FlowPrefabDesc64, RicardianStateV4);
_Static_assert(sizeof(FlowPrefabDesc64) == 64U, "FlowPrefabDesc64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowPrefabDesc64) == 64U, "FlowPrefabDesc64 must be 64-byte aligned");

/*
 * ----------------------------------------------------------------------------
 * 3. Runtime Instance: FlowPrefabInstance64 (Compact State, Exact 64 Bytes)
 * ----------------------------------------------------------------------------
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t prefab_id;            /* Referenced prefab ID */
    uint32_t style_override_id;    /* 0 = use prefab default, else style override */
    int64_t pos_q16[3];            /* 3D position in Q48.16 (x, y, z) */
    int64_t vel_q16[3];            /* 3D velocity in Q48.16 (vx, vy, vz) */
    uint32_t thermal_kelvin;       /* Operational temperature in Kelvin */
    uint16_t active_layer;         /* 0 = Layer 0 SDF, 1 = Layer 1 Dome, 2 = HUD */
    uint16_t flags;                /* Instance state flags */
} FlowPrefabInstance64;

typedef struct __attribute__((aligned(64))) {
    int64_t q_word[8];
} FlowPrefabInstanceShadow64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowPrefabInstance64, FlowPrefabInstanceShadow64);
FLOW_STATIC_ASSERT_UNIVALENT(FlowPrefabInstance64, RicardianStateV4);
_Static_assert(sizeof(FlowPrefabInstance64) == 64U, "FlowPrefabInstance64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowPrefabInstance64) == 64U, "FlowPrefabInstance64 must be 64-byte aligned");

/*
 * ----------------------------------------------------------------------------
 * 4. Constants & Enumerations
 * ----------------------------------------------------------------------------
 */

/* Ricardian Grade: 0..4 for GRADE_S..GRADE_D */
typedef enum {
    FLOW_RICARDIAN_GRADE_S = 0,
    FLOW_RICARDIAN_GRADE_A = 1,
    FLOW_RICARDIAN_GRADE_B = 2,
    FLOW_RICARDIAN_GRADE_C = 3,
    FLOW_RICARDIAN_GRADE_D = 4,
    FLOW_RICARDIAN_GRADE_COUNT = 5
} FlowRicardianGrade;

/* CSG Primitive Types (4 bits: 0..15) */
typedef enum {
    FLOW_PREFAB_PRIM_NONE        = 0,
    FLOW_PREFAB_PRIM_SPHERE      = 1,
    FLOW_PREFAB_PRIM_TORUS       = 2,
    FLOW_PREFAB_PRIM_BOX         = 3,
    FLOW_PREFAB_PRIM_OCTAHEDRON  = 4,
    FLOW_PREFAB_PRIM_CYLINDER    = 5,
    FLOW_PREFAB_PRIM_CAPSULE     = 6
} FlowPrefabPrimType;

/* CSG Operations (4 bits: 0..15) */
typedef enum {
    FLOW_CSG_OP_NONE             = 0,
    FLOW_CSG_OP_UNION            = 1,
    FLOW_CSG_OP_SMOOTH_UNION     = 2,
    FLOW_CSG_OP_SUBTRACT         = 3,
    FLOW_CSG_OP_SMOOTH_SUBTRACT  = 4,
    FLOW_CSG_OP_INTERSECT        = 5
} FlowCsgOp;

/* CSG Op Mask Bitfield Positions */
#define FLOW_CSG_PRIM1_SHIFT    0U
#define FLOW_CSG_PRIM1_MASK     0x0000000FU
#define FLOW_CSG_PRIM2_SHIFT    4U
#define FLOW_CSG_PRIM2_MASK     0x0000000FU
#define FLOW_CSG_OP_SHIFT       8U
#define FLOW_CSG_OP_MASK        0x0000000FU
#define FLOW_CSG_POLAR_SHIFT    12U
#define FLOW_CSG_POLAR_MASK     0x0000003FU

#define FLOW_CSG_ENCODE_MASK(prim1, prim2, op, mod_polar) \
    ((((uint32_t)(prim1) & FLOW_CSG_PRIM1_MASK) << FLOW_CSG_PRIM1_SHIFT) | \
     (((uint32_t)(prim2) & FLOW_CSG_PRIM2_MASK) << FLOW_CSG_PRIM2_SHIFT) | \
     (((uint32_t)(op)    & FLOW_CSG_OP_MASK)    << FLOW_CSG_OP_SHIFT)    | \
     (((uint32_t)(mod_polar) & FLOW_CSG_POLAR_MASK) << FLOW_CSG_POLAR_SHIFT))

/* Projective Layers */
typedef enum {
    FLOW_STAGE_LAYER_0_SDF   = 0, /* Full 3D CSG SDF Evaluation (Near) */
    FLOW_STAGE_LAYER_1_SLICE = 1, /* Forced-Perspective 2D Slice / Light Trace (Mid) */
    FLOW_STAGE_LAYER_HUD     = 2  /* MSDF Glyph / Tactical Radar Overlay (Far) */
} FlowStageLayer;

/* Thermal Meltdown Dual Thresholds (Hysteresis 800K enter / 760K exit) */
#define FLOW_THERMAL_MELTDOWN_ENTER_KELVIN 800ULL
#define FLOW_THERMAL_MELTDOWN_EXIT_KELVIN  760ULL

/* Canonical Style IDs (Art Bible: at least 8 canonical styles) */
enum {
    FLOW_STYLE_MINERAL_GRADE_S   = 1, /* Celestial Diamond / Quantum Superconductor */
    FLOW_STYLE_MINERAL_GRADE_A   = 2, /* Aurum Vein / High-Purity Gold */
    FLOW_STYLE_MINERAL_GRADE_B   = 3, /* Cyan Cobalt / Titanium Alloy */
    FLOW_STYLE_MINERAL_GRADE_C   = 4, /* Chalcopyrite / Bronze Ore */
    FLOW_STYLE_MINERAL_GRADE_D   = 5, /* Basalt Slag / Iron Matrix */
    FLOW_STYLE_ORBITAL_CITADEL   = 6, /* Void Obsidian + Neon Cyan Electroluminescent */
    FLOW_STYLE_INTERCEPTOR       = 7, /* High-Albedo Polar White + Plasma Magenta */
    FLOW_STYLE_THERMAL_MELTDOWN  = 8  /* 800K Meltdown Alert (Blazing Strobe Red) */
};

#define FLOW_SSOT_ART_BIBLE_COUNT 8U

/* Canonical Prefab IDs (at least 8 canonical prefabs) */
enum {
    FLOW_PREFAB_CORE_SPHERE          = 1, /* Basic Sphere */
    FLOW_PREFAB_QUANTUM_RING         = 2, /* Torus with 4-fold polar symmetry */
    FLOW_PREFAB_ORBITAL_CITADEL      = 3, /* Composite: Box + Torus smooth union */
    FLOW_PREFAB_INTERCEPTOR          = 4, /* Composite: Octahedron + Torus, 3-fold symmetry */
    FLOW_PREFAB_ASTEROID_GRADE_S     = 5, /* Mineral S: Octahedron + Sphere */
    FLOW_PREFAB_ASTEROID_GRADE_A     = 6, /* Mineral A: Sphere + Torus */
    FLOW_PREFAB_ASTEROID_GRADE_D     = 7, /* Mineral D: Box + Sphere */
    FLOW_PREFAB_THERMAL_CORE_800K    = 8  /* Thermal Core: Sphere + Octahedron */
};

#define FLOW_SSOT_PREFAB_COUNT 8U

/* Flags */
#define FLOW_PREFAB_FLAG_COLLIDABLE      (1U << 0)
#define FLOW_PREFAB_FLAG_DESTRUCTIBLE    (1U << 1)
#define FLOW_PREFAB_FLAG_CELESTIAL       (1U << 2)
#define FLOW_PREFAB_FLAG_EMISSIVE_PULSE  (1U << 3)

#define FLOW_STYLE_FLAG_WIREFRAME           (1U << 0)
#define FLOW_STYLE_FLAG_EMISSIVE_STROBE     (1U << 1)
#define FLOW_STYLE_FLAG_MSDF_ACCENT         (1U << 2)
#define FLOW_STYLE_FLAG_PHOTOSENSITIVE_SAFE (1U << 3) /* Clamps strobe_hz_q16 <= 3.0 Hz (196608 Q16) */
#define FLOW_STYLE_FLAG_COLORBLIND_GEOM     (1U << 4) /* Maps rarity/hazard to distinct sdf_noise_freq_q16 */

/* Harding / Steam / Console TRC Photosensitive Epilepsy Flash Limit: 3.0 Hz (3 * 65536 = 196608) */
#define FLOW_TRC_MAX_STROBE_HZ_Q16          (3 * FLOW_Q16_ONE)

/*
 * ----------------------------------------------------------------------------
 * 5. SSOT Canonical Tables & API Functions
 * ----------------------------------------------------------------------------
 */

/* Canonical SSOT Tables stored in read-only / resident memory */
extern const FlowArtStyle64 FLOW_SSOT_ART_BIBLE[FLOW_SSOT_ART_BIBLE_COUNT];
extern const FlowPrefabDesc64 FLOW_SSOT_PREFABS[FLOW_SSOT_PREFAB_COUNT];

/* SSOT Hash & Lookup API */
uint64_t flow_prefab_calc_hash(const FlowPrefabDesc64 *prefab);
void flow_ssot_init_canonical(void);
const FlowArtStyle64 *flow_ssot_get_art_style(uint32_t style_id);
const FlowPrefabDesc64 *flow_ssot_get_prefab(uint32_t prefab_id);

/* Pure Q16 CSG / SDF Geometric Evaluators */
int64_t flow_smin_q16(int64_t a_q16, int64_t b_q16, int64_t k_q16);
int64_t flow_smax_q16(int64_t a_q16, int64_t b_q16, int64_t k_q16);
void flow_polar_domain_fold_q16(int64_t *x_q16, int64_t *z_q16, uint32_t mod_polar);
int64_t flow_prefab_eval_sdf_q16(const FlowPrefabDesc64 *prefab, int64_t x_q16, int64_t y_q16, int64_t z_q16);

/* Worldline Anti-Tunneling via FlowJet2 Root Solving */
bool flow_prefab_eval_worldline_jet2(const FlowPrefabDesc64 *prefab,
                                    const FlowJet2 traj[3],
                                    int64_t t_max_q16,
                                    int64_t *out_t_q16);

/* Ricardian Assembly Theory Isomorphism */
FlowPrefabDesc64 flow_prefab_join_assembly(const FlowPrefabDesc64 *a,
                                          const FlowPrefabDesc64 *b,
                                          uint32_t shared_sub,
                                          uint32_t new_id,
                                          uint32_t csg_op);

/* Art Bible Thermal Hysteresis (800K enter / 760K exit) */
FlowArtStyle64 flow_art_bible_resolve(uint32_t style_id,
                                      uint64_t thermal_kelvin,
                                      bool prev_overheated,
                                      bool *out_overheated);

/*
 * Steam / Console TRC Accessibility Guard (Photosensitive Epilepsy & Colorblind Geometry Mapping):
 *   1. Photosensitive Epilepsy Guard: Clamps strobe_hz_q16 <= 3.0 Hz (196608 Q16) and caps
 *      emissive flash gain so full-screen 800K thermal alerts never exceed 3 flashes/sec.
 *   2. Colorblind Geometry Mapping: Maps rarity grades (Grade S..D) and hazard alerts into
 *      strictly monotonic, well-separated geometric SDF wave frequencies (sdf_noise_freq_q16)
 *      and MSDF stroke widths (msdf_stroke_q16) + high-contrast Daltonized luminance separation.
 */
FlowArtStyle64 flow_art_bible_apply_accessibility(FlowArtStyle64 style,
                                                  bool photosensitive_safe,
                                                  bool colorblind_geom);

FlowArtStyle64 flow_art_bible_resolve_accessible(uint32_t style_id,
                                                 uint64_t thermal_kelvin,
                                                 bool prev_overheated,
                                                 bool photosensitive_safe,
                                                 bool colorblind_geom,
                                                 bool *out_overheated);

/* Wave64 Compaction & Layer Collapse */
uint32_t flow_prefab_cull_and_collapse_wave64(const FlowPrefabInstance64 *instances,
                                             size_t count,
                                             const int64_t view_pos_q16[3],
                                             int64_t layer0_r_max_q16,
                                             int64_t layer1_r_max_q16,
                                             FlowPrefabInstance64 *out_collapsed,
                                             uint32_t out_compact_indices[FLOW_WAVE64_SIZE],
                                             uint64_t *out_active_mask);

/*
 * ----------------------------------------------------------------------------
 * 6. Concentric SDF Level-Set Padding (+-delta) & 64B Contact Manifold
 * ----------------------------------------------------------------------------
 *   - f(x) <= -delta_hurt_q16                -> FLOW_HIT_LETHAL (shrunk lethal core)
 *   - -delta_hurt_q16 < f(x) <= +delta_graze -> FLOW_HIT_GRAZE  (near-miss spark/bonus)
 *   - +delta_graze < f(x) <= +delta_hit_q16  -> FLOW_HIT_LOCKED (expanded weapon/laser lock)
 *   - f(x) > +delta_hit_q16                  -> FLOW_HIT_NONE   (vacuum)
 *   Visual contact projection: p_contact = p - f(p) * grad_f(p) onto f(x) = 0.
 */
typedef enum {
    FLOW_HIT_NONE   = 0,
    FLOW_HIT_LOCKED = 1,
    FLOW_HIT_GRAZE  = 2,
    FLOW_HIT_LETHAL = 3
} FlowHitZone;

typedef struct __attribute__((aligned(64))) {
    uint32_t hit_zone;           /* 4B: FlowHitZone (0=NONE, 1=LOCKED, 2=GRAZE, 3=LETHAL) */
    uint32_t contact_latched;    /* 4B: 1 if contact skin hysteresis is latched, 0 if detached */
    int64_t  t_hit_q16;          /* 8B: Exact worldline root t_hit in [0, dt_q16] (Q48.16) */
    int64_t  sdf_dist_q16;       /* 8B: Signed distance f(p(t_hit)) on visual SDF (Q48.16) */
    int64_t  contact_pos_q16[3]; /* 24B: Exact surface snap p - f(p)*grad_f(p) on f(x)=0 */
    int32_t  normal_q16[3];      /* 12B: Unit SDF gradient grad_f(p) in Q16 */
    uint32_t graze_bonus;        /* 4B: Shield / energy / M0 bonus awarded on graze */
} FlowContactManifold64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowContactManifold64, RicardianStateV4);
_Static_assert(sizeof(FlowContactManifold64) == 64U, "FlowContactManifold64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowContactManifold64) == 64U, "FlowContactManifold64 must be 64-byte aligned");

/* Pure Q16 SDF Unit Gradient & Visual Surface Projection: p_contact = p - f(p)*grad_f(p) */
void flow_prefab_eval_gradient_q16(const FlowPrefabDesc64 *prefab,
                                   int64_t x_q16,
                                   int64_t y_q16,
                                   int64_t z_q16,
                                   int32_t out_normal_q16[3]);

int64_t flow_prefab_project_surface_q16(const FlowPrefabDesc64 *prefab,
                                        const int64_t pos_q16[3],
                                        int64_t out_contact_q16[3],
                                        int32_t out_normal_q16[3]);

/* Concentric Level-Set Worldline Root Solver (J + delta_hurt, J - delta_graze, J - delta_hit) */
FlowHitZone flow_prefab_eval_worldline_padded_jet2(const FlowPrefabDesc64 *prefab,
                                                   const FlowJet2 traj[3],
                                                   int64_t delta_hurt_q16,
                                                   int64_t delta_graze_q16,
                                                   int64_t delta_hit_q16,
                                                   int64_t t_max_q16,
                                                   FlowContactManifold64 *out_manifold);

/* Direct 1D SDF 2-Jet Concentric Level-Set Classifier & Root Solver */
FlowHitZone flow_sdf_jet2_solve_padded(FlowJet2 sdf_jet,
                                       int64_t delta_hurt_q16,
                                       int64_t delta_graze_q16,
                                       int64_t delta_hit_q16,
                                       int64_t t_max_q16,
                                       int64_t *out_t_hit_q16);

/*
 * ============================================================================
 * 7. High-Level Commercial Strategy: Human-Aesthetic Silhouette Lofting
 *    (FlowFashionSkin64) & Zero-Backend Steam Marketplace (FlowMarketToken64)
 * ============================================================================
 *   - Replaces Blender 3D modeling with Orthogonal 2D Silhouette Lofting:
 *       f_3D(x, y, z) = smax(f_top_profile(x, z), f_side_profile(y, z))
 *     governed by Golden Ratio (1 : 0.618 = 40503 Q16) industrial proportions.
 *   - Replaces Photoshop textures with CS2-style Haute-Couture Optical Shaders:
 *     Doppler Sapphire, Case-Hardened Blue Gem, Kintsugi Gold Damascus,
 *     and Imperial Subsurface Jade, parameterized by pattern_seed & wear_q16.
 *   - Zero-Friction Steam Inventory & Community Market Integration:
 *     64-byte descriptors serialize bijectively into 128-char Hex metadata
 *     blobs with 64-bit SSOT anti-tamper signatures, Ricardian 10-to-1
 *     Trade-Up deflation contracts, and exact integer royalty splits.
 * ============================================================================
 */
typedef enum {
    FLOW_FINISH_DOPPLER_SAPPHIRE   = 0, /* Thin-film phase-shifted sapphire/violet pearl */
    FLOW_FINISH_CASE_HARDENED_BLUE = 1, /* 800K-1800K thermal quench blue-gem / gold patina */
    FLOW_FINISH_KINTSUGI_DAMASCUS  = 2, /* Folded obsidian Damascus steel + 24K molten gold veins */
    FLOW_FINISH_CELESTIAL_JADE     = 3  /* Translucent Beer-Lambert subsurface imperial jade */
} FlowLuxuryFinishClass;

typedef struct __attribute__((aligned(64))) {
    uint64_t pattern_seed;            /* 8B: Collector pattern seed (determines Blue-Gem / Vein layout) */
    uint32_t skin_id;                 /* 4B: Season / IP catalog ID */
    uint32_t finish_class;            /* 4B: FlowLuxuryFinishClass (0..3) */
    uint16_t ricardian_grade;         /* 2B: 0..4 (GRADE_S .. GRADE_D) */
    uint16_t assembly_index;          /* 2B: Ricardian Assembly Theory depth */
    int32_t  wear_q16;                /* 4B: Wear float in Q16 (0..4587 = Factory New) */
    int32_t  length_z_q16;            /* 4B: Primary longitudinal chord length in Q16 */
    int32_t  wingspan_x_q16;          /* 4B: Top-silhouette span (Golden ratio ~0.618 * length_z) */
    int32_t  profile_height_y_q16;    /* 4B: Side-silhouette thickness in Q16 */
    int32_t  sweep_taper_q16;         /* 4B: Aerodynamic leading-edge sweep slope in Q16 */
    int32_t  bevel_g2_q16;            /* 4B: G2 continuity smoothing radius in Q16 */
    int32_t  iridescence_q16;         /* 4B: Thin-film / quench color-shift intensity in Q16 */
    int32_t  gold_vein_q16;           /* 4B: Kintsugi / blue-gem coverage ratio in Q16 */
    int32_t  aesthetic_harmony_q16;   /* 4B: Golden-ratio + silhouette G2 harmony score [0..65536] */
    uint64_t ssot_signature;          /* 8B: Cryptographic / SSOT anti-forgery MAC signature */
} FlowFashionSkin64;

typedef struct __attribute__((aligned(64))) {
    uint64_t steam_item_instance_id;  /* 8B: Steam Inventory Service 64-bit item ID */
    uint64_t creator_account_id;      /* 8B: Web SaaS Workshop Creator ID for royalty split */
    uint64_t gross_price_cents;       /* 8B: Marketplace transaction price in cents */
    uint64_t valve_steam_fee_cents;   /* 8B: 5% Steam Community Market platform fee */
    uint64_t studio_treasury_cents;   /* 8B: 7% Studio fee (70% of 10% Game Fee) */
    uint64_t creator_royalty_cents;   /* 8B: 3% Web SaaS Creator royalty (30% of 10% Game Fee) */
    uint64_t seller_net_payout_cents; /* 8B: 85% Net payout to seller wallet */
    uint64_t trade_up_burn_count;     /* 8B: Cumulative lower-tier skins burned via Trade-Up */
} FlowMarketToken64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowFashionSkin64, RicardianStateV4);
FLOW_STATIC_ASSERT_UNIVALENT(FlowMarketToken64, RicardianStateV4);
_Static_assert(sizeof(FlowFashionSkin64) == 64U, "FlowFashionSkin64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowFashionSkin64) == 64U, "FlowFashionSkin64 must be 64-byte aligned");
_Static_assert(sizeof(FlowMarketToken64) == 64U, "FlowMarketToken64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowMarketToken64) == 64U, "FlowMarketToken64 must be 64-byte aligned");

/* 1. PM Zero-Blender 2D-to-3D Silhouette Lofting & Aesthetic Harmony Evaluator */
FlowFashionSkin64 flow_fashion_design_from_silhouette(uint32_t skin_id,
                                                      uint32_t finish_class,
                                                      uint16_t grade,
                                                      uint64_t pattern_seed,
                                                      int32_t wear_q16,
                                                      int32_t length_z_q16,
                                                      int32_t sweep_taper_q16);

int64_t flow_fashion_eval_lofted_sdf_q16(const FlowFashionSkin64 *skin,
                                         int64_t x_q16,
                                         int64_t y_q16,
                                         int64_t z_q16);

int flow_fashion_render_showroom_ppm(const FlowFashionSkin64 skins[4], const char *ppm_path);

/* 2. Zero-Backend Steam Inventory Hex128 Codec, Royalty Split & 10-to-1 Trade-Up Contract */
void flow_market_encode_hex128(const FlowFashionSkin64 *skin, char out_hex[129]);
bool flow_market_decode_hex128(const char hex[129], FlowFashionSkin64 *out_skin);

int flow_market_settle_transaction(uint64_t steam_item_id,
                                   uint64_t creator_id,
                                   uint64_t gross_price_cents,
                                   FlowMarketToken64 *out_settlement);

int flow_market_trade_up_contract(const FlowFashionSkin64 inputs[10],
                                  uint32_t new_skin_id,
                                  FlowFashionSkin64 *out_upgraded_skin,
                                  uint64_t *inout_burned_supply);

/*
 * ============================================================================
 * 8. Prefab-Managed Non-Destructive Edit / SSOT Revert & SDF X-Ray Cutaway Mask
 * ============================================================================
 * Enables PS / Blender / Godot / Flutter immediate viewport manipulation:
 *   - Modal operators (Grab/Translate, X-Ray Radius Scrub, Thermal Scrub)
 *     with O(1) Right-Click/Esc modal cancel and Godot-style Revert to SSOT.
 *   - Real-time SDF X-Ray Boolean Difference `max(d_outer, -d_xray_sphere)`
 *     managed directly by the Prefab layer.
 * ============================================================================
 */
#define FLOW_PREFAB_MODAL_NONE          0U
#define FLOW_PREFAB_MODAL_TRANSLATE     1U
#define FLOW_PREFAB_MODAL_XRAY_SCRUB    2U
#define FLOW_PREFAB_MODAL_THERMAL_SCRUB 3U

#define FLOW_PREFAB_XRAY_OFF            0U
#define FLOW_PREFAB_XRAY_SPHERE_CUT     1U
#define FLOW_PREFAB_XRAY_PLANE_CUT      2U

typedef struct __attribute__((aligned(64))) {
    uint32_t target_prefab_id;       /*  4B: Bound canonical FlowPrefabDesc64 ID */
    uint16_t modal_op_mode;          /*  2B: FLOW_PREFAB_MODAL_* active operator */
    uint16_t xray_mask_mode;         /*  2B: FLOW_PREFAB_XRAY_* cutaway mode */
    int32_t  xray_radius_q16;        /*  4B: X-Ray cutaway aperture radius in Q16 [0..65536] */
    int32_t  xray_cursor_x_q16;      /*  4B: Cursor X for X-Ray lens center in Q16 */
    int32_t  xray_cursor_y_q16;      /*  4B: Cursor Y for X-Ray lens center in Q16 */
    uint32_t xray_cut_layer_mask;    /*  4B: Bitmask of active_layers carved by X-Ray mask */
    int64_t  baseline_pos_q16[3];    /* 24B: Canonical / Modal-start 3D position snapshot */
    uint32_t baseline_thermal_k;     /*  4B: Canonical / Modal-start Kelvin snapshot */
    uint16_t baseline_layer;         /*  2B: Canonical active_layer snapshot */
    uint16_t baseline_flags;         /*  2B: Canonical instance flags snapshot */
    uint32_t undo_commit_count;      /*  4B: Total committed non-destructive edits */
    uint32_t revert_exec_count;      /*  4B: Total SSOT reverts / modal cancels executed */
} FlowPrefabEditState64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowPrefabEditState64, RicardianStateV4);
_Static_assert(sizeof(FlowPrefabEditState64) == 64U, "FlowPrefabEditState64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowPrefabEditState64) == 64U, "FlowPrefabEditState64 must be 64-byte aligned");

void flow_prefab_edit_init64(FlowPrefabEditState64 *edit,
                             const FlowPrefabInstance64 *inst);

void flow_prefab_edit_begin_modal64(FlowPrefabEditState64 *edit,
                                    const FlowPrefabInstance64 *inst,
                                    uint16_t modal_op_mode);

void flow_prefab_edit_apply_scrub64(FlowPrefabEditState64 *edit,
                                    FlowPrefabInstance64 *inst,
                                    int32_t delta_x_q16,
                                    int32_t delta_y_q16);

void flow_prefab_edit_commit64(FlowPrefabEditState64 *edit,
                               const FlowPrefabInstance64 *inst);

void flow_prefab_edit_revert_ssot64(FlowPrefabEditState64 *edit,
                                    FlowPrefabInstance64 *inst);

int64_t flow_prefab_eval_xray_cutaway_q16(const FlowPrefabEditState64 *edit,
                                          int64_t d_outer_q16,
                                          int64_t d_xray_sphere_q16);

/*
 * ============================================================================
 * 9. Fashion & Game Moodboard-to-Prefab Notebook (FlowMoodboardPrefab64)
 * ============================================================================
 * Bridges fashion-industry Moodboard / Lookbook collage workflows directly into
 * zero-modeling 3D Prefab & Optical Shader stages:
 *   - Stage Mode 0 (FLOW_MOOD_STAGE_SPACE_DEFAULT): Classic Relativistic Citadel
 *   - Stage Mode 1 (FLOW_MOOD_STAGE_WINDOW_OPTICS): Autumn Train Window Condensation,
 *     Gravity Rivulet Wiping State & 2-Jet Snell's Law Refraction
 *   - Stage Mode 2 (FLOW_MOOD_STAGE_PARTICLE_CORRIDOR): Infinite 3D Hash-Lattice
 *     Point-Cloud Monolith Corridors + Horizon Singularity + Glittering Wave Path
 *   - Stage Mode 3 (FLOW_MOOD_STAGE_ISOMETRIC_DIORAMA): "Winter Shore" Isometric
 *     CSG Cutaway Diorama Block (Snow Terrain + Geological Strata Cross-Section +
 *     Beer-Lambert Translucent Sea Volume + 2400K Warm Cabin Blackbody GI)
 * ============================================================================
 */
#define FLOW_MOOD_STAGE_SPACE_DEFAULT     0U
#define FLOW_MOOD_STAGE_WINDOW_OPTICS     1U
#define FLOW_MOOD_STAGE_PARTICLE_CORRIDOR 2U
#define FLOW_MOOD_STAGE_ISOMETRIC_DIORAMA 3U
#define FLOW_MOOD_STAGE_COUNT             4U
#define FLOW_MOOD_STAGE_AUTO_DETECT       0xFFFFFFFFU

typedef struct __attribute__((aligned(64))) {
    uint32_t mood_id;              /*  4B: Lookbook / Moodboard entry ID */
    uint32_t stage_mode;           /*  4B: FLOW_MOOD_STAGE_* (0..3) */
    uint32_t sky_rgb24;            /*  4B: Upper horizon / sky swatch (0x00RRGGBB) */
    uint32_t ground_rgb24;         /*  4B: Lower ground / foliage / sea swatch (0x00RRGGBB) */
    uint32_t accent_rgb24;         /*  4B: Emissive / singularity / cabin glow (0x00RRGGBB) */
    uint32_t backdrop_rgb24;       /*  4B: Studio tabletop / interior frame swatch (0x00RRGGBB) */
    int32_t  fog_condense_q16;     /*  4B: Window Mie condensation & roughness [0..65536] */
    int32_t  rivulet_wipe_q16;     /*  4B: Gravity water droplet trail wiping & refraction [0..65536] */
    int32_t  particle_density_q16; /*  4B: 3D hash-lattice monolith & wave glitter density [0..65536] */
    int32_t  singularity_gain_q16; /*  4B: Horizon anamorphic singularity core intensity [0..131072] */
    int32_t  diorama_cut_q16;      /*  4B: Isometric cutaway slab aperture / slice ratio [0..65536] */
    int32_t  water_level_q16;      /*  4B: Beer-Lambert translucent sea volume level [-32768..32768] */
    int32_t  snow_cover_q16;       /*  4B: Normal-projected procedural snow accumulation [0..65536] */
    uint32_t cabin_warmth_kelvin;  /*  4B: Interior blackbody emissive temperature (e.g. 2400K) */
    uint64_t mood_signature;       /*  8B: 64-bit SSOT integrity hash for 128-Hex sharing */
} FlowMoodboardPrefab64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowMoodboardPrefab64, RicardianStateV4);
_Static_assert(sizeof(FlowMoodboardPrefab64) == 64U, "FlowMoodboardPrefab64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowMoodboardPrefab64) == 64U, "FlowMoodboardPrefab64 must be 64-byte aligned");

void flow_moodboard_init_preset64(FlowMoodboardPrefab64 *mb, uint32_t stage_mode);

int flow_moodboard_extract_from_rgb_buffer64(const uint8_t *rgb_bytes,
                                             uint32_t width,
                                             uint32_t height,
                                             uint32_t row_stride_bytes,
                                             uint32_t preferred_stage_mode,
                                             FlowMoodboardPrefab64 *out_mb);

void flow_moodboard_to_prefab_and_style64(const FlowMoodboardPrefab64 *mb,
                                          FlowPrefabDesc64 *out_prefab,
                                          FlowArtStyle64 *out_style);

void flow_moodboard_encode_hex128(const FlowMoodboardPrefab64 *mb, char out_hex[129]);
bool flow_moodboard_decode_hex128(const char hex[129], FlowMoodboardPrefab64 *out_mb);

#endif /* FLOW_PREFAB_SSOT_H */
