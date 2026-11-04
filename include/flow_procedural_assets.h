#ifndef FLOW_PROCEDURAL_ASSETS_H
#define FLOW_PROCEDURAL_ASSETS_H

#include "flow_asm.h"
#include "flow_hott_jet.h"
#include "flow_io.h"
#include "ricardian.h"

/* ============================================================================
 * BLOOM: Zero-Token 64-Byte Procedural Asset & SDD SVG Pipeline
 * ============================================================================
 * Replaces all token-heavy / bandwidth-heavy external game assets (.gltf,
 * .ttf, .png, .anim, .wav) with 4 cache-line aligned (64-byte) pure Q16
 * integer descriptors + SDD SVG compile-time parser:
 *   1. FlowGenomeRecipe64 : 16 Atomic Parts x Folding Grammar -> 8x8 3D Atlas
 *   2. FlowGlyphDesc64    : 2D Analytic SDF Vector HUD + 1.58-Bit Ternary Icon
 *   3. FlowKineticRig64   : Keyframe-less 2-Jet Mechanism & Hysteresis Rig
 *   4. FlowAudioPatch64   : Zero-WAV 2-Jet FM & Shepard Chirp Synth
 * ============================================================================ */

#define FLOW_ATOMIC_PART_COUNT 16U
#define FLOW_GLYPH_MAX_STROKES 5U

/* Folding & Greeble Bitmask for FlowGenomeRecipe64 */
#define FLOW_GENE_FOLD_MIRROR_X  (1U << 0)
#define FLOW_GENE_FOLD_MIRROR_Y  (1U << 1)
#define FLOW_GENE_FOLD_POLAR_4   (1U << 2)
#define FLOW_GENE_FOLD_POLAR_8   (1U << 3)
#define FLOW_GENE_GREEBLE_PANEL  (1U << 4)
#define FLOW_GENE_SMOOTH_UNION   (1U << 5)

/* 1. 64-Byte Combinatorial 3D Shape Genome (0-Token Mass 3D Generator) */
typedef struct __attribute__((aligned(64))) {
    uint64_t genome_seed;        /* 8B: Deterministic evolution seed */
    uint32_t recipe_id;          /* 4B: Registry / Atlas slot ID */
    uint32_t assembly_index;     /* 4B: Ricardian Assembly Theory depth */
    uint32_t ricardian_grade;    /* 4B: 0..4 (GRADE_S .. GRADE_D) */
    uint32_t fold_flags;         /* 4B: Mirror / Polar / Greeble bitmask */
    uint8_t  part_ids[4];        /* 4B: 4 atomic part indices (0..15) */
    uint32_t shared_sub_count;   /* 4B: Reused substructure count */
    int32_t  radii_q16[4];       /* 16B: Primary & secondary radii in Q16 */
    int32_t  elongation_q16[2];  /* 8B: Axial & radial elongation in Q16 */
    uint32_t rgb_accent_hex;     /* 4B: Accent color from Art Bible */
    uint32_t checksum;           /* 4B: Deterministic integrity hash */
} FlowGenomeRecipe64;

/* 2. 64-Byte 2D Analytic SDF Vector Glyph & 1.58-Bit Ternary Symbol */
typedef struct {
    int16_t x0_q8;
    int16_t y0_q8;
    int16_t x1_q8;
    int16_t y1_q8;
} FlowStrokeSeg8;

typedef struct __attribute__((aligned(64))) {
    uint32_t glyph_id;                          /* 4B: ASCII / HUD symbol ID */
    uint32_t stroke_count;                      /* 4B: Number of active 2D segments (<= 5) */
    int32_t  stroke_radius_q16;                 /* 4B: Line thickness / arc radius in Q16 */
    uint32_t style_id;                          /* 4B: Bound Art Bible style class */
    FlowStrokeSeg8 segs[FLOW_GLYPH_MAX_STROKES];/* 40B: 5 analytic segments in Q8.8 [-1, 1] */
    uint64_t ternary_pos_mask;                  /* 8B: Positive bitplane for 1-cycle POPCNT match */
} FlowGlyphDesc64;

/* 3. 64-Byte Keyframe-less 2-Jet Procedural Mechanism & VFX Worldline */
typedef struct __attribute__((aligned(64))) {
    FlowJet2 angle_jet;             /* 24B: Angular position (v), vel (d1), accel (d2) in Q16 */
    int64_t  deploy_enter_dist_q16; /* 8B: Distance threshold to deploy mechanism */
    int64_t  deploy_exit_dist_q16;  /* 8B: Hysteresis distance threshold to retract */
    int64_t  target_omega_q16;      /* 8B: Active angular velocity target */
    uint32_t rig_id;                /* 4B: Mechanism ID */
    uint32_t deployed_state;        /* 4B: 0 = Retracted, 1 = Deployed (hysteresis latched) */
    uint64_t sieve_status;          /* 8B: Hardware 2-Jet Taylor self-check sieve */
} FlowKineticRig64;

/* 4. 64-Byte Zero-WAV Pure Q16 2-Jet FM & Chirp Acoustic Synthesizer */
typedef struct __attribute__((aligned(64))) {
    FlowJet2 carrier_jet;        /* 24B: Phase (v), base freq (d1), chirp rate (d2) in Q16 */
    int64_t  mod_freq_q16;       /* 8B: FM modulator frequency in Q16 */
    int64_t  fm_index_q16;       /* 8B: FM modulation depth in Q16 */
    int32_t  attack_step_q16;    /* 4B: Envelope attack ramp per sample */
    int32_t  decay_step_q16;     /* 4B: Envelope decay ramp per sample */
    uint32_t patch_id;           /* 4B: Sound patch ID */
    uint32_t waveform_flags;     /* 4B: Sine/Triangle/Pulse/Klaxon flags */
    uint64_t checksum;           /* 8B: Patch integrity tag */
} FlowAudioPatch64;

/*
 * 5. 64-Byte Hearthstone-Grade SDD SVG Animation & VFX Choreography Clip
 *    Authored declaratively inside specs/cockpit_hud.svg via <animate data-clip-id=.../>
 *    4-Stage Juicy Choreography: Anticipation -> Delivery -> Impact Climax -> Spring Settle
 */
typedef enum {
    FLOW_ANIM_PHASE_ANTICIPATION = 0, /* Hover lift / crouch pullback + aura charge */
    FLOW_ANIM_PHASE_DELIVERY     = 1, /* High-speed arc + area-preserving squash & stretch */
    FLOW_ANIM_PHASE_IMPACT       = 2, /* Climax slam + hitstop + shockwave ripple + trauma */
    FLOW_ANIM_PHASE_SETTLE       = 3, /* Underdamped spring overshoot back to 1.0x rest */
    FLOW_ANIM_PHASE_DONE         = 4
} FlowAnimPhase;

typedef struct __attribute__((aligned(64))) {
    uint32_t clip_id;                /* 4B: Bound Glyph / Prefab ID */
    uint32_t style_id;               /* 4B: Art Bible Rarity / Grade Style ID */
    uint16_t dur_anticipation_ms;    /* 2B: Phase 0 duration (ms) */
    uint16_t dur_delivery_ms;        /* 2B: Phase 1 duration (ms) */
    uint16_t dur_impact_ms;          /* 2B: Phase 2 duration (ms) */
    uint16_t dur_settle_ms;          /* 2B: Phase 3 duration (ms) */
    int32_t  scale_anticipation_q16; /* 4B: Crouch / hover scale in Q16 (e.g. 0.85x = 55705) */
    int32_t  scale_impact_q16;       /* 4B: Climax slam scale in Q16 (e.g. 1.45x = 95027) */
    int32_t  squash_gain_q16;        /* 4B: Velocity squash-and-stretch factor in Q16 */
    int32_t  arc_height_q16;         /* 4B: Parabolic delivery lift height in Q16 */
    int32_t  peak_flux_q16;          /* 4B: Impact Pi_rel aura/corona eruption in Q16 */
    int32_t  peak_ripple_phi_q16;    /* 4B: Impact Phi_rel refractive shockwave in Q16 */
    int32_t  trauma_shake_q16;       /* 4B: 6D camera shake impulse at impact in Q16 */
    uint16_t hitstop_frames;         /* 2B: Hitstop freeze frames triggered at impact */
    uint16_t audio_patch_id;         /* 2B: Coupled 2-Jet FM sound patch ID */
    int32_t  settle_overshoot_q16;   /* 4B: Damped spring overshoot amplitude in Q16 */
    uint64_t checksum;               /* 8B: SDD compile-time integrity tag */
} FlowAnimClip64;

typedef struct __attribute__((aligned(64))) {
    uint32_t clip_id;                /* 4B: Active clip ID */
    uint32_t phase;                  /* 4B: FlowAnimPhase (0..4) */
    int32_t  phase_progress_q16;     /* 4B: Normalized progress within current phase [0..65536] */
    int32_t  scale_parallel_q16;     /* 4B: Scale along velocity vector (Squash & Stretch) */
    int32_t  scale_perp_q16;         /* 4B: Scale perpendicular to velocity (area-preserving!) */
    int32_t  offset_arc_y_q16;       /* 4B: Parabolic trajectory lift offset in Q16 */
    int32_t  flux_corona_q16;        /* 4B: Instantaneous Pi_rel aura intensity in Q16 */
    int32_t  ripple_phi_q16;         /* 4B: Instantaneous Phi_rel shockwave refraction in Q16 */
    int32_t  camera_trauma_q16;      /* 4B: Instantaneous camera trauma impulse in Q16 */
    uint32_t hitstop_frames_trigger; /* 4B: Non-zero on impact entry to trigger hitstop */
    uint32_t audio_patch_trigger;    /* 4B: Non-zero on impact entry to trigger synth */
    int32_t  reserved[5];            /* 20B: Pad to exact 64 bytes */
} FlowAnimSample64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowGenomeRecipe64, RicardianStateV4);
FLOW_STATIC_ASSERT_UNIVALENT(FlowGlyphDesc64, RicardianStateV4);
FLOW_STATIC_ASSERT_UNIVALENT(FlowKineticRig64, RicardianStateV4);
FLOW_STATIC_ASSERT_UNIVALENT(FlowAudioPatch64, RicardianStateV4);
FLOW_STATIC_ASSERT_UNIVALENT(FlowAnimClip64, RicardianStateV4);
FLOW_STATIC_ASSERT_UNIVALENT(FlowAnimSample64, RicardianStateV4);
_Static_assert(sizeof(FlowGenomeRecipe64) == 64U && _Alignof(FlowGenomeRecipe64) == 64U, "FlowGenomeRecipe64 must be 64B");
_Static_assert(sizeof(FlowGlyphDesc64) == 64U && _Alignof(FlowGlyphDesc64) == 64U, "FlowGlyphDesc64 must be 64B");
_Static_assert(sizeof(FlowKineticRig64) == 64U && _Alignof(FlowKineticRig64) == 64U, "FlowKineticRig64 must be 64B");
_Static_assert(sizeof(FlowAudioPatch64) == 64U && _Alignof(FlowAudioPatch64) == 64U, "FlowAudioPatch64 must be 64B");
_Static_assert(sizeof(FlowAnimClip64) == 64U && _Alignof(FlowAnimClip64) == 64U, "FlowAnimClip64 must be 64B");
_Static_assert(sizeof(FlowAnimSample64) == 64U && _Alignof(FlowAnimSample64) == 64U, "FlowAnimSample64 must be 64B");

/*
 * 6. 64-Byte White-Box Combo Cancel Gate & 2-Jet Momentum Spline (FlowComboChain64)
 *    Replaces black-box "root privilege" state overrides with:
 *      - Explicit 4-Phase Cancel Permission Mask (Settle / Hit-Confirm / Delivery-Flash)
 *      - Piecewise C^1 2-Jet Phase Re-Anchoring (inherits x(t_c) & v(t_c) at t_cancel)
 *      - Local Proper-Time Fiber (proper_time_ratio_q16) without breaking global causality
 */
#define FLOW_CANCEL_GATE_SETTLE         (1U << 0) /* Fast-QA: truncate Phase 3 backswing */
#define FLOW_CANCEL_GATE_HIT_CONFIRM    (1U << 1) /* Roman Cancel: branch in Phase 2 iff hit_confirmed */
#define FLOW_CANCEL_GATE_DELIVERY_FLASH (1U << 2) /* Q-Flash: inject 2-Jet displacement in Phase 1 */

typedef enum {
    FLOW_COMBO_TRANSITION_NONE           = 0,
    FLOW_COMBO_TRANSITION_RECOVERY_CUT   = 1, /* Phase 3 backswing truncated */
    FLOW_COMBO_TRANSITION_HIT_CONFIRM    = 2, /* Phase 2 hit-confirm branch */
    FLOW_COMBO_TRANSITION_FLASH_INJECT   = 3, /* Phase 1 trajectory displacement overlay */
    FLOW_COMBO_TRANSITION_NATURAL_CHAIN  = 4  /* Chained after Phase 4 completion */
} FlowComboTransitionType;

typedef struct __attribute__((aligned(64))) {
    FlowJet2 traj_z;                 /* 24B: Anchor 2-Jet worldline (z0, v0, a0) at anchor_ms */
    uint32_t active_clip_id;         /* 4B: Active FlowAnimClip64 ID */
    uint32_t anchor_ms;              /* 4B: Global timestamp (ms) of last spline re-anchor */
    int32_t  proper_time_ratio_q16;  /* 4B: Local proper-time multiplier (65536 = 1.0x) */
    uint32_t cancel_gate_mask;       /* 4B: Allowed white-box cancel gates */
    uint32_t combo_counter;          /* 4B: Consecutive chained actions */
    int32_t  momentum_inherit_q16;   /* 4B: Velocity carryover ratio in Q16 (65536 = 100%) */
    uint64_t sieve_status;           /* 8B: Hardware 2-Jet Taylor continuity sieve */
    uint64_t last_transition_type;   /* 8B: FlowComboTransitionType of last cancel */
} FlowComboChain64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowComboChain64, RicardianStateV4);
_Static_assert(sizeof(FlowComboChain64) == 64U, "FlowComboChain64 must be 64 bytes");
_Static_assert(_Alignof(FlowComboChain64) == 64U, "FlowComboChain64 must be 64-byte aligned");

/*
 * 7. 64-Byte Ternary HashMap Narrative Engine (FlowDialogNode64)
 *    O(1) open-addressing lookup + 1-cycle POPCNT contextual matcher:
 *      - 8B event_key_hash: HashMap bucket key (NPC/quest event)
 *      - 8B req_mask: bit k = 1 if world fact k is checked, 0 if Don't Care
 *      - 8B req_values: bit k = 1 if required True, 0 if False
 *      - 8B set_mask: world facts modified on branch execution
 *      - 8B set_values: new values for modified world facts
 *      - 4B dialog_id: unique branch ID
 *      - 4B priority_bias: designer tie-breaker
 *      - 4B next_clip_id: triggered FlowAnimClip64 ID
 *      - 4B reward_grade: Ricardian reward grade 0..4
 *      - 8B checksum: integrity hash
 *    Total: 5*8 + 4*4 + 8 = 64 bytes (exact 64B univalent cache line!)
 */
typedef struct __attribute__((aligned(64))) {
    uint64_t event_key_hash; /* 8B: O(1) HashMap event bucket key */
    uint64_t req_mask;       /* 8B: bit k = 1 if checked, 0 if Don't Care */
    uint64_t req_values;     /* 8B: bit k = 1 if required True, 0 if False */
    uint64_t set_mask;       /* 8B: world facts modified when firing */
    uint64_t set_values;     /* 8B: new values for modified facts */
    uint32_t dialog_id;      /* 4B: unique story branch ID */
    uint32_t priority_bias;  /* 4B: tie-breaker added to POPCNT score */
    uint32_t next_clip_id;   /* 4B: triggered FlowAnimClip64 choreography ID */
    uint32_t reward_grade;   /* 4B: Ricardian reward grade 0..4 */
    uint64_t checksum;       /* 8B: integrity hash */
} FlowDialogNode64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowDialogNode64, RicardianStateV4);
_Static_assert(sizeof(FlowDialogNode64) == 64U, "FlowDialogNode64 must be 64 bytes");
_Static_assert(_Alignof(FlowDialogNode64) == 64U, "FlowDialogNode64 must be 64-byte aligned");

/*
 * 8. 64-Byte Ricardian Self-Balancing Item & Combat Stat (FlowItemStat64)
 *    Ricardian Assembly-budget conservation + AMM hyperbolic diminishing returns:
 *      - 4B item_id: unique item / affix ID
 *      - 4B assembly_index: Assembly Theory depth determining total budget
 *      - 4B ricardian_grade: 0..4 (GRADE_S .. GRADE_D)
 *      - 4B raw_attack_flux_q16: raw offensive flux
 *      - 4B raw_haste_cdr_q16: raw cooldown reduction contribution
 *      - 4B raw_shield_mit_q16: raw damage mitigation contribution
 *      - 4B thermal_cost_kelvin: Joule heat generated per activation
 *      - 4B eff_attack_q16: resolved attack after budget conservation
 *      - 4B eff_cdr_q16: resolved CDR in [0, 49152] (<75% cap via AMM)
 *      - 4B eff_mitigation_q16: resolved mitigation in [0, 52428] (<80% cap via AMM)
 *      - 4B flags: item ability & affix flags
 *      - 4B synergy_mask: elemental / affix synergy bitmask
 *      - 8B harberger_upkeep: per-epoch Harberger tax upkeep
 *      - 8B ssot_hash: SSOT deterministic integrity hash
 *    Total: 12*4 + 2*8 = 64 bytes (exact 64B univalent cache line!)
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t item_id;             /* 4B: item / affix ID */
    uint32_t assembly_index;      /* 4B: Ricardian Assembly Theory depth */
    uint32_t ricardian_grade;     /* 4B: 0..4 for GRADE_S..GRADE_D */
    int32_t  raw_attack_flux_q16; /* 4B: raw offensive flux contribution */
    int32_t  raw_haste_cdr_q16;   /* 4B: raw cooldown/haste contribution */
    int32_t  raw_shield_mit_q16;  /* 4B: raw damage mitigation contribution */
    int32_t  thermal_cost_kelvin; /* 4B: Joule heat generated per activation */
    int32_t  eff_attack_q16;      /* 4B: resolved effective attack */
    int32_t  eff_cdr_q16;         /* 4B: resolved effective CDR in [0, 49152] */
    int32_t  eff_mitigation_q16;  /* 4B: resolved effective mitigation in [0, 52428] */
    uint32_t flags;               /* 4B: affix / special property flags */
    uint32_t synergy_mask;        /* 4B: synergy bitmask */
    uint64_t harberger_upkeep;    /* 8B: per-epoch Harberger tax upkeep */
    uint64_t ssot_hash;           /* 8B: integrity hash */
} FlowItemStat64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowItemStat64, RicardianStateV4);
_Static_assert(sizeof(FlowItemStat64) == 64U, "FlowItemStat64 must be 64 bytes");
_Static_assert(_Alignof(FlowItemStat64) == 64U, "FlowItemStat64 must be 64-byte aligned");

/*
 * 9. 64-Byte Tri-Modal Audiovisual Coupling & SDF Cavity Resonance Descriptor
 *    (FlowAudiovisualFrame64)
 *    Unifies Relativistic Doppler & Proper-Time Acoustics, SDF Cavity Helmholtz
 *    Resonance (with dual-threshold hysteresis [12m enter, 20m exit]), Live
 *    Haute-Couture Ship Finishes, 4-Phase Squash & Stretch Deformation, TRC <= 3Hz
 *    Photosensitive Guard, and Zero-Drift Tri-Modal (Optical-Acoustic-Haptic) Phase Lock.
 */
#define FLOW_CAVITY_ENTER_DIST_Q16 (12LL * FLOW_Q16_ONE)
#define FLOW_CAVITY_EXIT_DIST_Q16  (20LL * FLOW_Q16_ONE)

typedef struct __attribute__((aligned(64))) {
    int32_t  doppler_carrier_freq_q16; /* 4B: Relativistic Doppler + proper-time shifted freq (Q16 Hz) */
    int32_t  cavity_resonance_q16;     /* 4B: SDF cavity Helmholtz resonance gain [0..65536] */
    int32_t  cavity_reverb_decay_q16;  /* 4B: Early-reflection comb decay from local SDF enclosure */
    uint32_t cavity_latched;           /* 4B: Dual-threshold cavity hysteresis state (0=open, 1=cavity) */
    int32_t  squash_parallel_q16;      /* 4B: Live 4-phase choreography parallel scale (Q16) */
    int32_t  squash_perp_q16;          /* 4B: Area-preserving perpendicular scale (Q16) */
    int32_t  shockwave_ripple_q16;     /* 4B: Impact / graze refractive shockwave amplitude (Q16) */
    uint32_t active_finish_class;      /* 4B: Active FlowLuxuryFinishClass (0..3) */
    int32_t  finish_iridescence_q16;   /* 4B: Thin-film / thermal quench iridescence intensity (Q16) */
    int32_t  finish_gold_vein_q16;     /* 4B: Kintsugi / Blue-Gem vein coverage ratio (Q16) */
    int32_t  trc_safe_strobe_hz_q16;   /* 4B: TRC-clamped (<= 3.0 Hz) strobe frequency (Q16) */
    int32_t  trimodal_phase_q16;       /* 4B: Shared 2-Jet phase locking Camera, Audio & Haptics */
    int32_t  haptic_resonance_hz_q16;  /* 4B: Phase-locked tactile/haptic output frequency (Q16) */
    uint32_t active_audio_patch_id;    /* 4B: Active coupled 2-Jet FM sound patch ID */
    uint64_t checksum;                 /* 8B: Deterministic SSOT integrity hash */
} FlowAudiovisualFrame64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowAudiovisualFrame64, RicardianStateV4);
_Static_assert(sizeof(FlowAudiovisualFrame64) == 64U, "FlowAudiovisualFrame64 must be 64 bytes");
_Static_assert(_Alignof(FlowAudiovisualFrame64) == 64U, "FlowAudiovisualFrame64 must be 64-byte aligned");

/*
 * 10. 64-Byte Master-Seed Generative Style Engine Descriptor (FlowGenerativeSeedStyle64)
 *     Implements the 5-Component Generative Style Pipeline (todo.md):
 *       1. Oklab Perceptual Gamut & Readability Solver (Delta-L >= 0.40 = 26214 Q16)
 *       2. Negative-Space ("計白當黑") Dominance Ratio [60%..75% = 39321..49152 Q16]
 *       3. Edge Contour Synthesizer (Feibai dry-brush ink vs Gold-Stone razor chisel)
 *       4. Surface PDE / Voronoi Ice-Crackle ("熔金開片 / 冰裂瓷紋") vs Shanshui宣紙
 *       5. Coupled Acoustic Resonator LPF Cutoff & Harmonic Brightness
 */
#define FLOW_OKLAB_MIN_DELTA_L_Q16     26214 /* 0.40 minimum perceptual contrast */
#define FLOW_NEG_SPACE_MIN_RATIO_Q16   39321 /* 60% minimum negative space ("計白當黑") */
#define FLOW_NEG_SPACE_MAX_RATIO_Q16   49152 /* 75% maximum negative space */

typedef enum {
    FLOW_SEED_ARCHETYPE_SHANSHUI_INK     = 0, /* 種子 A：寒江獨釣 / 極簡水墨 (宣紙 + 朱砂印 + 悶鼓撕裂) */
    FLOW_SEED_ARCHETYPE_KINTSUGI_CRACKLE = 1, /* 種子 B：熔金開片 / 碎形陶瓷 (赭石燒青 + 冰裂金光 + 清脆金石) */
    FLOW_SEED_ARCHETYPE_BRONZE_PATINA    = 2, /* 種子 C：金石青銅 / 綠鏽銘文 */
    FLOW_SEED_ARCHETYPE_ASTRO_ARTDECO    = 3  /* 種子 D：高定占星 / 燙金幾何 */
} FlowSeedStyleArchetype;

typedef struct __attribute__((aligned(64))) {
    uint64_t master_seed;               /* 8B: 64-bit Daily Rental / Season Master Seed */
    uint32_t archetype_id;              /* 4B: FlowSeedStyleArchetype (0..3) */
    uint32_t bg_rgb_hex;                /* 4B: Oklab-solved background color (0x00RRGGBB) */
    uint32_t fg_lit_rgb_hex;            /* 4B: Oklab-solved foreground lit color (0x00RRGGBB) */
    uint32_t accent_rgb_hex;            /* 4B: Cinnabar seal / 24K molten gold accent (0x00RRGGBB) */
    int32_t  oklab_delta_l_q16;         /* 4B: Enforced Oklab lightness contrast (>= 26214) */
    int32_t  negative_space_ratio_q16;  /* 4B: Enforced negative space ratio in [39321, 49152] */
    int32_t  stroke_feibai_q16;         /* 4B: <0 Feibai ink feathering, >0 razor gold-stone line */
    int32_t  pde_crackle_freq_q16;      /* 4B: Reaction-Diffusion / Voronoi ice-crackle frequency */
    int32_t  audio_lpf_cutoff_q16;      /* 4B: Coupled audio resonator low-pass filter cutoff */
    int32_t  audio_harmonic_bright_q16; /* 4B: Coupled ceramic/metallic chime brightness in Q16 */
    uint32_t ui_diegetic_mode;          /* 4B: 0 = Quant Terminal (極客終端), 1 = Astro Art-Deco (占星策展) */
    uint32_t reserved_pad;              /* 4B: Alignment pad */
    uint64_t seed_mac;                  /* 8B: Deterministic SSOT MAC signature */
} FlowGenerativeSeedStyle64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowGenerativeSeedStyle64, RicardianStateV4);
_Static_assert(sizeof(FlowGenerativeSeedStyle64) == 64U, "FlowGenerativeSeedStyle64 must be 64 bytes");
_Static_assert(_Alignof(FlowGenerativeSeedStyle64) == 64U, "FlowGenerativeSeedStyle64 must be 64-byte aligned");

/*
 * 11. 64-Byte Sovereign Quality Gate, Dual-Currency Orthogonality & Sandbox Quota
 *     Descriptor (FlowSovereignGate64)
 *     Enforces todo.md Release Candidate & Anti-Refund Quality Invariants:
 *       - Dual-Currency Orthogonality: Gameplay actions mutate `funds_g` only;
 *         `sovereign_sr` is 100% invariant across all gameplay/script transitions.
 *       - Sandbox Instruction Quota Circuit-Breaker: Halts `while(true)` scripts at
 *         `FLOW_SANDBOX_MAX_SCRIPT_STEPS` (1000 steps) in <1 us without main-thread stall.
 *       - Two-Player Lineage Tether: Shared `order_id` can claim `(0,0)` tax-free plot at most once.
 *       - Thermal ASMR [720K..795K] Resonance & [X] Key -120K High-Pressure Venting.
 */
#define FLOW_SANDBOX_MAX_SDF_OPS       64U
#define FLOW_SANDBOX_MAX_SCRIPT_STEPS  1000U
#define FLOW_THERMAL_AMBIENT_KELVIN    700U
#define FLOW_THERMAL_RESONANCE_MIN_K   720U
#define FLOW_THERMAL_RESONANCE_MAX_K   795U
#define FLOW_THERMAL_PURGE_DELTA_K     120U

typedef enum {
    FLOW_GAMEPLAY_MINE_ORE       = 0,
    FLOW_GAMEPLAY_PAY_HARBERGER  = 1,
    FLOW_GAMEPLAY_FUND_WONDER    = 2,
    FLOW_GAMEPLAY_SQUEEZE_TARGET = 3,
    FLOW_GAMEPLAY_SCULPT_PLANET  = 4
} FlowGameplayActionKind;

typedef struct __attribute__((aligned(64))) {
    uint64_t order_id;             /* 8B: Shared 2-player bundle Lineage Tether order ID */
    uint64_t partner_account_id;   /* 8B: Co-pilot / Joint-Trust partner account ID */
    int64_t  funds_g;              /* 8B: In-game galactic currency (G) */
    uint64_t sovereign_sr;         /* 8B: Orthogonal fiat Sovereign Reserve points (sr) */
    uint32_t active_sdf_ops;       /* 4B: Active parametric SDF carving ops (<= 64) */
    uint32_t active_script_steps;  /* 4B: Executed script steps in current tick (<= 1000) */
    uint32_t script_tripped_halt;  /* 4B: 1 if sandbox quota circuit-breaker tripped */
    uint32_t lineage_plot_claimed; /* 4B: 1 if (0,0) tax-free plot claimed under order_id */
    uint32_t joint_trust_dual_sig; /* 4B: Bit 0=Primary, Bit 1=Partner (3 = Joint Trust active) */
    uint32_t thermal_kelvin;       /* 4B: Live machine temperature ([720..795K] resonance, [X] -120K) */
    uint64_t state_mac;            /* 8B: Cryptographic seal (1-bit tamper -> Unranked Sandbox) */
} FlowSovereignGate64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowSovereignGate64, RicardianStateV4);
_Static_assert(sizeof(FlowSovereignGate64) == 64U, "FlowSovereignGate64 must be 64 bytes");
_Static_assert(_Alignof(FlowSovereignGate64) == 64U, "FlowSovereignGate64 must be 64-byte aligned");

/*
 * ----------------------------------------------------------------------------
 * 11. 64-Byte Composable 4-Axis Object Ticket (FlowObjectTicket64)
 *     Genre-free orthogonal composition of:
 *       Axis 1 (Camera Rig) x Axis 2 (Input/Kinetic Coupling) x
 *       Axis 3 (Binaural/Cavity Audio) x Axis 4 (Visual/Physics Prefab & Visibility)
 * ----------------------------------------------------------------------------
 */
#define FLOW_TICKET_STATUS_BACKLOG      0U
#define FLOW_TICKET_STATUS_IN_DEV       1U
#define FLOW_TICKET_STATUS_QA_AUDIT     2U
#define FLOW_TICKET_STATUS_SEALED_READY 3U

/* Axis 1: Camera Perspective Archetypes */
#define FLOW_CAM_AXIS_HORROR_FP_21MM    1U
#define FLOW_CAM_AXIS_CLIMBER_SIDE_35MM 2U
#define FLOW_CAM_AXIS_TACTICAL_EYE_18MM 3U
#define FLOW_CAM_AXIS_ARENA_TOPDOWN_135 4U
#define FLOW_CAM_AXIS_CCTV_FIXED_28MM   5U

/* Axis 2: Input & Kinematic Coupling Flags */
#define FLOW_KINETIC_DIRECT_IMPULSE     (1U << 0)
#define FLOW_KINETIC_FULCRUM_LEVER      (1U << 1) /* F_body = -F_tip (Hammer / Crowbar / Recoil) */
#define FLOW_KINETIC_INERTIA_LAG        (1U << 2) /* 2-Jet trailing flashlight / heavy weapon lag */
#define FLOW_KINETIC_COUNTER_STRAFE     (1U << 3) /* Instant friction stop on opposite key */
#define FLOW_KINETIC_FLOWFIELD_STEP     (1U << 4) /* Waypoint / Flow-Field steering */

/* Axis 3: Spatial Acoustic & Psychoacoustic Flags */
#define FLOW_AUDIO_BINAURAL_3D          (1U << 0) /* 48kHz ITD + Head-Shadow ILD + Pinna Notch */
#define FLOW_AUDIO_CAVITY_OCCLUSION     (1U << 1) /* SDF room/wall low-pass & comb reverb */
#define FLOW_AUDIO_CONTACT_RESONANCE    (1U << 2) /* Impact velocity -> material chime/thud */
#define FLOW_AUDIO_REAR_WHISPER_SUB     (1U << 3) /* Horror rear-neck whisper & sub-bass pulse */

/* Axis 4: Unified Wave64 Visibility Cone / Lighting Modes */
#define FLOW_VIS_MODE_FULL_LIT          0U
#define FLOW_VIS_MODE_HORROR_FLASHLIGHT 1U /* Inertial cone + 75% negative-space abyss */
#define FLOW_VIS_MODE_TACTICAL_SIGHT    2U /* Directed tactical sightline cone */
#define FLOW_VIS_MODE_ARENA_FOG_OF_WAR  3U /* Top-down radial + cone fog-of-war bitmask */

/* Orthogonal Axis Selector for flow_ticket_rebind_axis64 */
#define FLOW_AXIS_CAMERA                0U
#define FLOW_AXIS_KINETIC               1U
#define FLOW_AXIS_AUDIO                 2U
#define FLOW_AXIS_VISUAL                3U

typedef struct __attribute__((aligned(64))) {
    uint64_t ticket_id;              /* 8B: Studio work-order / object ticket ID */
    uint32_t ticket_status;          /* 4B: BACKLOG(0), IN_DEV(1), QA_AUDIT(2), SEALED_READY(3) */
    uint32_t camera_axis_id;         /* 4B: Axis 1 — Camera Rig ID (21mm FP, 35mm Side, 18mm Eye, 135mm Top) */
    uint32_t kinetic_axis_flags;     /* 4B: Axis 2 — Input/Kinematic Coupling bitmask */
    uint32_t audio_axis_flags;       /* 4B: Axis 3 — Binaural/Cavity/Whisper Acoustic bitmask */
    uint32_t visual_prefab_id;       /* 4B: Axis 4 — SDF Visual/Physics Prefab ID */
    uint32_t visibility_mode;        /* 4B: Unified Wave64 Visibility Cone mode (0..3) */
    int32_t  body_pos_z_q16;         /* 4B: Resolved primary elevation / forward coord in Q16 */
    int32_t  body_vel_z_q16;         /* 4B: Resolved primary velocity in Q16 */
    int32_t  lever_tip_dist_q16;     /* 4B: Signed SDF distance of lever/hammer/crowbar tip in Q16 */
    int32_t  inertia_lag_offset_q16; /* 4B: 2-Jet trailing lag offset (flashlight/weapon sway) in Q16 */
    uint64_t visible_tile_mask64;    /* 8B: Active Wave64 visibility cone / fog-of-war bitmask */
    uint64_t ticket_seal_mac;        /* 8B: Cryptographic SLA & 4-axis composition MAC */
} FlowObjectTicket64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowObjectTicket64, RicardianStateV4);
_Static_assert(sizeof(FlowObjectTicket64) == 64U, "FlowObjectTicket64 must be 64 bytes");
_Static_assert(_Alignof(FlowObjectTicket64) == 64U, "FlowObjectTicket64 must be 64-byte aligned");

/*
 * ----------------------------------------------------------------------------
 * 12. 64-Byte Dopamine Economy Jackpot & Player Experience Auditor (FlowDopamineJackpot64)
 *     Models the 4-Phase Slot-Machine Execution Payoff & Headless Experience SLA:
 *       Phase 0: IDLE
 *       Phase 1: SUSPENSE_BUILDUP (Edge vignette + rising FM pitch tension)
 *       Phase 2: HITSTOP_SILENCE  (-18dB acoustic drop + 50ms freeze frame)
 *       Phase 3: CATHARSIS_BOOM   (800K->1800K cavity overdrive + sub-bass drop)
 *       Phase 4: TELEPHOTO_FLEX   (135mm f/1.4 Haute-Couture C-Position skin spotlight)
 * ----------------------------------------------------------------------------
 */
#define FLOW_DOPAMINE_PHASE_IDLE      0U
#define FLOW_DOPAMINE_PHASE_SUSPENSE  1U
#define FLOW_DOPAMINE_PHASE_HITSTOP   2U
#define FLOW_DOPAMINE_PHASE_BOOM      3U
#define FLOW_DOPAMINE_PHASE_FLEX      4U

typedef struct __attribute__((aligned(64))) {
    uint64_t session_id;             /* 8B: Telemetry session / replay seed ID */
    uint32_t phase;                  /* 4B: Active jackpot phase (0..4) */
    uint32_t phase_remaining_ms;     /* 4B: Remaining duration in current phase (ms) */
    uint32_t required_keys_apm;      /* 4B: Required simultaneous keys (<= 2 -> 0 anxiety) */
    uint32_t active_finish_tier;     /* 4B: Cosmetic skin tier (0..3, 0 combat modifier) */
    int32_t  suspense_vignette_q16;  /* 4B: Peripheral vignette narrowing [0, 55705] */
    int32_t  catharsis_bloom_q16;    /* 4B: Execution shockwave & bloom gain [0, 98304] */
    int32_t  flex_spotlight_q16;     /* 4B: 135mm f/1.4 C-position key-light gain [0, 98304] */
    int32_t  contrast_delta_q16;     /* 4B: Measured Boom-minus-Silence contrast (>= 57344) */
    uint32_t time_to_first_fun_ms;   /* 4B: Time from spawn to first dopamine hook (<= 3000ms) */
    uint32_t frustration_index_q16;  /* 4B: Mechanical anxiety / stall index (<= 16384) */
    uint32_t flex_window_ms;         /* 4B: Cumulative 135mm cosmetic showcase time (>= 1500ms) */
    uint32_t sla_certified;          /* 4B: 1 if all CEO Dopamine & 120FPS SLAs pass */
    uint64_t jackpot_mac;            /* 8B: Deterministic telemetry seal MAC */
} FlowDopamineJackpot64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowDopamineJackpot64, RicardianStateV4);
_Static_assert(sizeof(FlowDopamineJackpot64) == 64U, "FlowDopamineJackpot64 must be 64 bytes");
_Static_assert(_Alignof(FlowDopamineJackpot64) == 64U, "FlowDopamineJackpot64 must be 64-byte aligned");

/* API: 1. 0-Token Combinatorial 3D Shape Generator & 8x8 Atlas */
FlowGenomeRecipe64 flow_genome_evolve_one(uint64_t seed, uint32_t slot_id);
void flow_genome_evolve_batch64(uint64_t base_seed, FlowGenomeRecipe64 out_batch[64]);
int64_t flow_genome_eval_sdf_q16(const FlowGenomeRecipe64 *r, int64_t x_q16, int64_t y_q16, int64_t z_q16);
int flow_genome_render_atlas_ppm(const FlowGenomeRecipe64 batch[64], const char *ppm_path);

/* API: 2. 2D Analytic SDF Vector HUD & SDD SVG Compile-Time Loader */
int64_t flow_glyph_eval_sdf_q16(const FlowGlyphDesc64 *g, int32_t u_q16, int32_t v_q16);
int32_t flow_glyph_match_ternary(const FlowGlyphDesc64 *a, const FlowGlyphDesc64 *b);
size_t flow_svg_compile_file(const char *svg_path, FlowGlyphDesc64 *out_glyphs, size_t max_glyphs);
size_t flow_svg_compile_anim_clips(const char *svg_path, FlowAnimClip64 *out_clips, size_t max_clips);
void flow_anim_clip_eval_q16(const FlowAnimClip64 *clip, uint32_t elapsed_ms, FlowAnimSample64 *out_sample);

/* API: 3. Keyframe-less 2-Jet Procedural Mechanism */
uint64_t flow_kinetic_rig_step(FlowKineticRig64 *rig, int64_t dist_q16, int64_t dt_q16);

/* API: 4. Zero-WAV Pure Q16 2-Jet FM Acoustic Synthesizer */
void flow_audio_synth_samples_q16(const FlowAudioPatch64 *patch, int64_t *out_pcm_q16,
                                  size_t sample_count, int64_t dt_sample_q16);

/* API: 5. White-Box 4-Phase Combo Cancel Gate & 2-Jet Momentum Spline */
void flow_combo_chain_init(FlowComboChain64 *chain,
                           const FlowAnimClip64 *initial_clip,
                           uint32_t now_ms,
                           uint32_t cancel_gate_mask,
                           int64_t initial_vz_q16,
                           int64_t initial_az_q16);

void flow_combo_chain_eval_q16(const FlowComboChain64 *chain,
                               const FlowAnimClip64 *active_clip,
                               uint32_t now_ms,
                               FlowAnimSample64 *out_anim_sample,
                               FlowJet2 *out_instant_jet_z);

bool flow_combo_chain_try_cancel(FlowComboChain64 *chain,
                                 const FlowAnimClip64 *current_clip,
                                 const FlowAnimClip64 *next_clip,
                                 uint32_t now_ms,
                                 bool hit_confirmed,
                                 int64_t flash_dz_q16,
                                 int64_t next_dv_q16,
                                 int64_t next_da_q16);

/* API: 6. 64B Ternary HashMap Narrative Engine */
int32_t flow_dialog_match_score_o1(const FlowDialogNode64 *node, uint64_t event_key_hash, uint64_t world_facts);
const FlowDialogNode64 *flow_dialog_hashmap_select(const FlowDialogNode64 *table, size_t table_pow2_size, uint64_t event_key_hash, uint64_t *inout_world_facts);
bool flow_dialog_hashmap_insert(FlowDialogNode64 *table, size_t table_pow2_size, FlowDialogNode64 node);

/* API: 7. 64B Ricardian Self-Balancing Item & 100,000-Combo Balance Verifier */
FlowItemStat64 flow_item_craft_ricardian(uint32_t item_id, uint32_t assembly_index, uint32_t grade, int32_t atk_weight, int32_t cdr_weight, int32_t mit_weight);
FlowItemStat64 flow_item_combine_build(const FlowItemStat64 *items, size_t count);
int32_t flow_item_simulate_ttk_ms(const FlowItemStat64 *attacker_build, const FlowItemStat64 *defender_build);

/* API: 8. 64B Tri-Modal Audiovisual Coupling & SDF Cavity Relativistic Synth */
void flow_av_resolve_frame64(int32_t base_carrier_hz_q16,
                             int32_t doppler_factor_q16,
                             int32_t proper_time_ratio_q16,
                             int64_t sdf_dist_q16,
                             uint32_t prev_cavity_latched,
                             const FlowAnimSample64 *anim_sample,
                             uint32_t finish_class,
                             int32_t finish_iridescence_q16,
                             int32_t finish_gold_vein_q16,
                             int32_t raw_strobe_hz_q16,
                             int32_t shared_jet_phase_q16,
                             FlowAudiovisualFrame64 *out_av);

void flow_audio_synth_relativistic_q16(const FlowAudioPatch64 *patch,
                                       const FlowAudiovisualFrame64 *av,
                                       int64_t *out_pcm_q16,
                                       size_t sample_count,
                                       int64_t dt_sample_q16);

/* API: 9. 64B Master-Seed Generative Style Solver (Oklab + Negative Space + Shanshui/Kintsugi) */
FlowGenerativeSeedStyle64 flow_seed_style_derive64(uint64_t master_seed);

/* API: 10. 64B Sovereign Quality Gate, Dual-Currency Orthogonality & Sandbox Quota */
void flow_sovereign_gate_init64(FlowSovereignGate64 *gate,
                                uint64_t order_id,
                                uint64_t partner_id,
                                int64_t initial_funds_g,
                                uint64_t verified_sovereign_sr);

void flow_sovereign_gate_exec_action(FlowSovereignGate64 *gate,
                                     FlowGameplayActionKind kind,
                                     int64_t amount_g);

bool flow_sovereign_gate_run_sandbox_quota(FlowSovereignGate64 *gate,
                                           uint32_t requested_sdf_ops,
                                           uint32_t requested_script_steps,
                                           uint32_t *out_estimated_frame_us);

bool flow_sovereign_gate_claim_lineage_plot(FlowSovereignGate64 *gate);

uint32_t flow_sovereign_gate_thermal_purge_x(FlowSovereignGate64 *gate);

bool flow_sovereign_gate_verify_seal(const FlowSovereignGate64 *gate);

/* API: 11. 48kHz True Binaural Stereo Synthesizer (2-Jet ITD + ILD + Pinna Notch + Thermal ASMR) */
void flow_audio_synth_binaural_stereo_q16(const FlowAudioPatch64 *patch,
                                          const FlowAudiovisualFrame64 *av,
                                          const FlowGenerativeSeedStyle64 *seed_style,
                                          int32_t azimuth_x_q16,
                                          int32_t elevation_y_q16,
                                          int32_t front_back_z_q16,
                                          uint32_t thermal_kelvin,
                                          bool purge_vent_active,
                                          int64_t *out_stereo_lr_q16,
                                          size_t frame_count,
                                          int64_t dt_sample_q16);

/* API: 12. 64B Composable 4-Axis Object Ticket & Genre-Free Mechanics Engine */
FlowObjectTicket64 flow_ticket_compose64(uint64_t ticket_id,
                                         uint32_t camera_axis_id,
                                         uint32_t kinetic_axis_flags,
                                         uint32_t audio_axis_flags,
                                         uint32_t visual_prefab_id,
                                         uint32_t visibility_mode,
                                         uint32_t sdf_op_cost,
                                         uint32_t script_step_cost);

void flow_ticket_rebind_axis64(FlowObjectTicket64 *ticket,
                               uint32_t axis_selector,
                               uint32_t new_axis_value);

int32_t flow_ticket_step_mechanics_q16(FlowObjectTicket64 *ticket,
                                       int32_t tip_force_q16,
                                       int32_t move_cmd_q16,
                                       int32_t surface_sdf_q16,
                                       int32_t look_delta_q16,
                                       uint32_t aim_tile_idx);

FlowObjectTicket64 flow_ticket_build_preset64(uint32_t preset_index);

bool flow_ticket_verify_seal64(const FlowObjectTicket64 *ticket);

/* API: 13. 64B Dopamine Economy Jackpot & 10,000-Session Player Experience Auditor */
void flow_dopamine_trigger_jackpot64(FlowDopamineJackpot64 *jp,
                                     uint64_t session_id,
                                     uint32_t required_keys_apm,
                                     uint32_t finish_tier,
                                     uint32_t first_hook_ms);

void flow_dopamine_step_ms(FlowDopamineJackpot64 *jp, uint32_t dt_ms);

uint32_t flow_dopamine_audit_10k_sessions(uint64_t base_seed,
                                          const FlowObjectTicket64 *ticket,
                                          FlowDopamineJackpot64 *out_summary);

#endif /* FLOW_PROCEDURAL_ASSETS_H */
