#ifndef FLOW_GAME_H
#define FLOW_GAME_H

/*
 * ============================================================================
 * BLOOM v1.0.0: Clean-Slate Native C17 Simulation Engine
 * ============================================================================
 * Architectural Mandates:
 *   1. Zero Godot / Zero Scene-Tree / Zero Floating-Point Simulation:
 *      100% pure integer simulation (int64_t Q48.16, uint64_t, __int128).
 *      No float or double types in the C simulation layer.
 *   2. Relativistic Worldline Anti-Tunneling:
 *      3D FlowJet2 trajectories, exact analytic root via flow_jet2_solve_root,
 *      linear boundary lookahead via flow_qpoly_predict_next_face,
 *      differential self-checking via flow_asm_jet2_self_check_step.
 *   3. Forced Perspective & Stage Handoff (Layer 0 <-> Layer 1):
 *      Universal external constraint polyhedron FlowStagePoly (FLOW_QPOLY_DEFINE_EX),
 *      dual-threshold hysteresis (b_enter / b_exit), flow_cubical_step_neighbor
 *      topological verification, and lock-free QSBR epoch publication.
 *   4. Topological Entropy Sparse Wave64 Culling:
 *      Viewport partitioned into 36 Wave64 groups (64x36 tiles = 2304 tiles).
 *      Nondegenerate 2-Jet variation ballot via flow_jet2_nondegenerate_ballot64,
 *      1-cycle POPCNT+CTZ compaction via flow_gpu_ballot_compact_wave64.
 *   5. Ricardian v4 Economy Kernel:
 *      64-byte univalent RicardianStateV4 state, Harberger 70/30 tax conservation,
 *      Assembly Index depth monotonicity, AMM reserve floor, 800K/760K thermal
 *      meltdown hysteresis.
 * ============================================================================
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "flow_asm.h"
#include "flow_hott_jet.h"
#include "flow_io.h"
#include "ricardian.h"
#include "flow_cine_camera.h"
#include "flow_prefab_ssot.h"

#ifndef BLOOM_VERSION_MAJOR
#define BLOOM_VERSION_MAJOR 1U
#endif
#ifndef BLOOM_VERSION_MINOR
#define BLOOM_VERSION_MINOR 0U
#endif
#ifndef BLOOM_VERSION_PATCH
#define BLOOM_VERSION_PATCH 0U
#endif
#ifndef BLOOM_VERSION_STRING
#define BLOOM_VERSION_STRING "1.0.0"
#endif

/* Viewport Tile Partitioning for 4K (3840x2160) */
#define FLOW_GAME_VIEWPORT_W       3840U
#define FLOW_GAME_VIEWPORT_H       2160U
#define FLOW_GAME_TILE_SIZE        60U
#define FLOW_GAME_TILES_X          64U  /* Exactly 1 Wave64 lane per column */
#define FLOW_GAME_TILES_Y          36U  /* 36 Wave64 groups */
#define FLOW_GAME_TOTAL_TILES      (FLOW_GAME_TILES_X * FLOW_GAME_TILES_Y) /* 2304 tiles */
#define FLOW_GAME_WAVE64_GROUPS    FLOW_GAME_TILES_Y

/* Concentric SDF Level-Set Padding & Action Feel Defaults (Pure Q48.16) */
#define FLOW_DEFAULT_DELTA_HURT_Q16      (16LL << FLOW_Q16_SHIFT)
#define FLOW_DEFAULT_DELTA_GRAZE_Q16     (48LL << FLOW_Q16_SHIFT)
#define FLOW_DEFAULT_DELTA_HIT_Q16       (128LL << FLOW_Q16_SHIFT)
#define FLOW_DEFAULT_SKIN_ENTER_Q16      (4LL << FLOW_Q16_SHIFT)
#define FLOW_DEFAULT_SKIN_EXIT_Q16       (24LL << FLOW_Q16_SHIFT)
#define FLOW_HITSTOP_DEFAULT_SCALE_Q16   (FLOW_Q16_ONE >> 4U) /* 1/16th speed time dilation */
#define FLOW_HITSTOP_LETHAL_FRAMES       4U
#define FLOW_HITSTOP_GRAZE_FRAMES        2U
#define FLOW_INPUT_BUFFER_WINDOW_Q16     6554LL               /* ~100 ms window in Q16 */
#define FLOW_INPUT_BUFFER_FRAMES         6U                   /* 6 frames at 60 Hz (~100 ms) */
#define FLOW_C_LIGHT_DEFAULT_Q16         (1000LL << FLOW_Q16_SHIFT)      /* Canonical elevator/test light speed c */
#define FLOW_C_LIGHT_ENGINE_Q16          (2400LL << FLOW_Q16_SHIFT)      /* Engine worldline light speed c_eff */
#define FLOW_C_LIGHT_VACUUM_Q16          (299792458LL << FLOW_Q16_SHIFT) /* Exact SI vacuum speed of light c in Q48.16 */
#define FLOW_BETA_MAX_Q16                65470LL                         /* 0.999c subluminal ceiling in Q16 */

/* Forced-Perspective Stage Handoff Polyhedron (DIM=3, SHIFT=16, PLANES=16) */
FLOW_QPOLY_DEFINE_EX(FlowStagePoly, 3, 16, 16)

/* Contact Skin Dual-Threshold Hysteresis Polyhedron (DIM=1, SHIFT=16, PLANES=2) */
FLOW_QPOLY_DEFINE_EX(FlowContactSkinPoly, 1, 16, 2)

/*
 * ============================================================================
 * FlowRelativeScale64: 64-Byte Unified Relativistic & Medium Scale Interface
 * ============================================================================
 * Eliminates hard-coded effect classes (BlackHole, SpeedForce, SuperSaiyan,
 * Wind, WaterRipple). All physical & optical phenomena emerge continuously
 * from 4 dimensionless relative ratios between Observer (J_self) and Field (J_env):
 *   1. Time / Lorentz Scale (proper_time_ratio_q16, lorentz_beta_q16, gamma, doppler):
 *      - tau_rel << 1 : Hitstop / Gravitational stasis
 *      - beta -> 1, tau_rel >> 1 : "Speed Force" (world freezes, Lorentz aberration
 *        tunnel, Doppler blueshift, high-frequency shear lightning)
 *   2. Metric Curvature / Refractive Potential (metric_potential_q16 = r_s/(2r) = delta_n):
 *      - Phi ~ 1e-3 (oscillating) : Surface water ripples / heat mirage refraction
 *      - Phi ~ 0.333 (r = 1.5 r_s): Photon Sphere & Einstein Ring lensing
 *      - Phi >= 0.50 (r <= r_s)   : Black Hole Event Horizon (grav_redshift -> 0,
 *        tidal_stretch spaghettification)
 *   3. Signed Boundary Flux Ratio (flux_pressure_ratio_q16 = (P_self - P_env)/P_0):
 *      - Pi_rel < 0  : External wind buffeting / bow-shock compression
 *      - Pi_rel >> +1: "Super Saiyan" outward plasma corona eruption, radial
 *        repulsion of surrounding fluid/debris, updraft phase turbulence
 *   4. Spatial Renormalization Scale (spatial_scale_q16 = L_self / L_field):
 *      - Continuous micro-voxel to celestial dome scale invariance.
 * ============================================================================
 */
typedef struct __attribute__((aligned(64))) {
    int32_t proper_time_ratio_q16;   /* 4B: d(tau_self)/dt_world in Q16 (65536 = 1.0x) */
    int32_t lorentz_beta_q16;        /* 4B: ||v_self - v_env|| / c_eff in [0, 65535] */
    int32_t lorentz_gamma_q16;       /* 4B: (1 - beta^2)^(-1/2) in Q16 (>= 65536) */
    int32_t doppler_factor_q16;      /* 4B: Forward relativistic Doppler shift sqrt((1+b)/(1-b)) */
    int32_t metric_potential_q16;    /* 4B: Dimensionless Phi = r_s/(2r) = delta_n in Q16 */
    int32_t grav_redshift_q16;       /* 4B: sqrt(max(0, 1 - 2*Phi)) in Q16 (0 inside horizon) */
    int32_t flux_pressure_ratio_q16; /* 4B: Signed (P_self - P_env)/P_0 (<0 wind, >0 aura) */
    int32_t spatial_scale_q16;       /* 4B: L_self / L_field continuous scale factor in Q16 */
    int32_t rel_vel_dir_q16[3];      /* 12B: Unit relative velocity direction (ux, uy, uz) Q16 */
    int32_t metric_grad_q16[3];      /* 12B: Normalized metric/medium gradient grad(Phi) Q16 */
    int32_t wave_phase_freq_q16;     /* 4B: Boundary-layer wave frequency omega from ||dJ|| */
    int32_t tidal_stretch_q16;       /* 4B: Radial/transverse anisotropy 1 + 2*Phi + (gamma-1) */
} FlowRelativeScale64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowRelativeScale64, RicardianStateV4);
_Static_assert(sizeof(FlowRelativeScale64) == 64U, "FlowRelativeScale64 must be 64 bytes");
_Static_assert(_Alignof(FlowRelativeScale64) == 64U, "FlowRelativeScale64 must be 64-byte aligned");

/*
 * ============================================================================
 * FlowTimingWheel64: 64-Byte O(1) Phase-Staggered Wave64 Timing Wheel
 * ============================================================================
 * Solves the "Don't Starve" 100,000-entity timer & polling jitter problem:
 *   - active_slot_mask: 64-bit Wave64 bitmask where bit k is 1 iff slot k has
 *     pending events/buckets. Allows O(1) 1-cycle bit-test dispatch without
 *     polling entities.
 *   - Golden-ratio phase hash staggering deterministically spreads synchronous
 *     timer registrations across all 64 slots, eliminating thundering-herd
 *     tick execution spikes.
 * ============================================================================
 */
typedef struct __attribute__((aligned(64))) {
    uint64_t active_slot_mask;   /* 8B: 64-bit Wave64 bitmask (bit k = 1 iff slot k has pending items) */
    uint32_t current_tick;      /* 4B: current wheel tick cursor */
    uint32_t total_scheduled;  /* 4B: total active scheduled items across the 64 slots */
    uint32_t total_fired;      /* 4B: cumulative fired count */
    uint32_t max_fired_per_tick; /* 4B: peak items fired in any single tick */
    uint32_t min_fired_per_tick; /* 4B: min items fired in any active tick */
    uint16_t slot_counts[18];   /* 36B: compact counter / head summary for first 18 sub-buckets (total 64B) */
} FlowTimingWheel64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowTimingWheel64, RicardianStateV4);
_Static_assert(sizeof(FlowTimingWheel64) == 64U, "FlowTimingWheel64 must be 64 bytes");
_Static_assert(_Alignof(FlowTimingWheel64) == 64U, "FlowTimingWheel64 must be 64-byte aligned");

/*
 * ============================================================================
 * 120 FPS (8.33 ms) Dual-Threshold Frame-Time Hysteresis Governor
 * ============================================================================
 * Uses FlowStagePoly_eval_hysteresis [b_enter = 7800 us, b_exit = 6000 us]
 * and flow_cubical_step_neighbor topological verification to dynamically bound
 * SDF raymarch step budget (64 vs 32) and Layer 0 detail radius (100m vs 60m)
 * under the 8333 us (120 FPS) ceiling with zero oscillation chatter in the
 * (6000, 7800] us deadband.
 * ============================================================================
 */
#define FLOW_FPS30_TARGET_BUDGET_US       33333U              /* 33.333ms = 30 FPS ceiling */
#define FLOW_FPS60_TARGET_BUDGET_US       16666U              /* 16.666ms = 60 FPS ceiling */
#define FLOW_FPS120_TARGET_BUDGET_US      8333U               /* 8.333ms  = 120 FPS ceiling */
#define FLOW_FPS_UNCAPPED_BUDGET_US       4166U               /* 4.166ms  = 240+ FPS uncapped pacing */
#define FLOW_FIXED_PHYSICS_DT_US          16666U              /* Authoritative 60Hz fixed physics tick (us) */
#define FLOW_FPS120_THROTTLE_ENTER_US     7800U               /* Enter quality-throttle if frame > 7.8ms */
#define FLOW_FPS120_THROTTLE_EXIT_US      6000U               /* Exit quality-throttle only when <= 6.0ms */
#define FLOW_FPS120_RAYMARCH_STEPS_ULTRA  64U                 /* Full SDF raymarch step budget */
#define FLOW_FPS120_RAYMARCH_STEPS_FAST   32U                 /* Throttled SDF raymarch step budget */
#define FLOW_FPS120_LAYER0_R_ULTRA_Q16    (100 * FLOW_Q16_ONE)
#define FLOW_FPS120_LAYER0_R_FAST_Q16     (60 * FLOW_Q16_ONE)

typedef struct __attribute__((aligned(64))) {
    uint64_t frame_index;             /* 8B:  Monotonic frame index */
    uint64_t violated_mask;           /* 8B:  FlowStagePoly_eval_hysteresis bitmask (bit 0 = throttled) */
    uint32_t measured_frame_us;       /* 4B:  Latest measured frame duration (us) */
    uint32_t ema_frame_us;            /* 4B:  Integer EMA of frame duration (us) */
    uint32_t target_budget_us;        /* 4B:  8333 us (120 FPS) */
    uint32_t throttle_enter_us;       /* 4B:  7800 us upper hysteresis trigger */
    uint32_t throttle_exit_us;        /* 4B:  6000 us lower hysteresis recovery */
    uint32_t quality_throttled;       /* 4B:  1 = quality throttle active, 0 = Ultra */
    uint32_t raymarch_max_steps;      /* 4B:  64 (Ultra) or 32 (Fast) */
    int32_t  layer0_radius_q16;       /* 4B:  100m (Ultra) or 60m (Fast) */
    uint32_t transition_count;        /* 4B:  Count of quality state transitions (proves zero chatter) */
    uint32_t cubical_verified;        /* 4B:  1 if flow_cubical_step_neighbor verified transition */
    uint64_t governor_mac;            /* 8B:  64-bit integrity MAC */
} FlowFrameGovernor64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowFrameGovernor64, RicardianStateV4);
_Static_assert(sizeof(FlowFrameGovernor64) == 64U, "FlowFrameGovernor64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowFrameGovernor64) == 64U, "FlowFrameGovernor64 must be 64-byte aligned");

/*
 * FlowGameFramePacket:
 * Cache-line aligned (64-byte multiple) lock-free snapshot published to renderer.
 * 100% pure integer: zero float/double.
 */
typedef struct __attribute__((aligned(64))) {
    uint64_t frame_index;
    uint64_t timestamp_ns;
    int64_t craft_x_q16;
    int64_t craft_y_q16;
    int64_t craft_z_q16;
    int64_t craft_vx_q16;
    int64_t craft_vy_q16;
    int64_t craft_vz_q16;
    int64_t craft_pitch_q16;
    int64_t craft_yaw_q16;
    int64_t craft_roll_q16;
    uint64_t stage_layer;          /* 0 = Layer 0 Local SDF, 1 = Layer 1 Celestial Dome */
    uint64_t active_tiles_count;   /* Compacted active tiles in viewport */
    uint64_t thermal_shutdown;     /* 1 if engine overheated (>800K), 0 if normal */
    uint64_t collision_hit;        /* 1 if worldline collision occurred */
    int64_t collision_t_q16;       /* Exact root t_hit in Q16 */
    uint64_t hardware_sieve;       /* FLOW v4 hardware check sieve */
    uint64_t hit_zone;             /* FlowHitZone: 0=NONE, 1=LOCKED, 2=GRAZE, 3=LETHAL */
    uint64_t hitstop_active;       /* 1 if hitstop time dilation is active, 0 if normal */
    int64_t effective_dt_q16;      /* Time-dilated dt_q16 applied to physics step */
    int64_t contact_x_q16;         /* Visual surface snap contact X on f(x)=0 (Q48.16) */
    int64_t contact_y_q16;         /* Visual surface snap contact Y on f(x)=0 (Q48.16) */
    int64_t contact_z_q16;         /* Visual surface snap contact Z on f(x)=0 (Q48.16) */
    uint64_t contact_latched;      /* Contact skin hysteresis state (bit 0) + graze count */
    RicardianStateV4 ricardian;    /* 64 bytes aligned */
    FlowCineResolvedFrame64 cine_camera;     /* 64 bytes aligned resolved VCam optics */
    FlowRelativeScale64 relative_scale;      /* 64 bytes aligned unified relative scale tensor */
    FlowFrameGovernor64 fps_governor;        /* 64 bytes aligned 120 FPS hysteresis governor */
} FlowGameFramePacket;

_Static_assert((sizeof(FlowGameFramePacket) % 64U) == 0U,
               "FlowGameFramePacket must be 64-byte aligned and multiple of 64");

/* Lock-free SPSC Ring for publishing frame packets */
FLOW_SPSC_DEFINE(FlowGamePacketRing, FlowGameFramePacket, 64)

/*
 * FlowGameEngineState:
 * Complete state of the clean-slate FLOW simulation engine.
 */
typedef struct {
    /* 3D Craft Worldline Trajectory: x(t), y(t), z(t) as 2-Jets in Q48.16 */
    FlowJet2 craft_traj[3];
    FlowJet2 prev_craft_traj[3];

    /* Craft Orientation: pitch, yaw, roll as 2-Jets in Q48.16 */
    FlowJet2 craft_rot[3];
    int64_t craft_throttle_q16;

    /* Primary Celestial Obstacle (Analytic Sphere Center & Radius in Q48.16) */
    int64_t celestial_pos_q16[3];
    int64_t celestial_radius_q16;

    /* Concentric SDF Level-Set Padding (+-delta in Q48.16) */
    int64_t delta_hurt_q16;        /* Shrunk lethal hurtbox offset (-delta_hurt) */
    int64_t delta_graze_q16;       /* Expanded near-miss graze shell (+delta_graze) */
    int64_t delta_hit_q16;         /* Expanded weapon/mining lock-on shell (+delta_hit) */

    /* Contact Skin Dual-Threshold Hysteresis [delta_enter, delta_exit] */
    FlowContactSkinPoly contact_skin_poly;
    uint64_t contact_skin_mask;
    uint32_t contact_latched;
    uint32_t prev_hit_zone;
    FlowContactManifold64 last_contact;
    uint64_t graze_count;

    /* Time-Dimension Action Feel: Hitstop & ~100ms Input Buffer Window */
    uint32_t hitstop_frames_remaining;
    int64_t  hitstop_scale_q16;
    int64_t  last_effective_dt_q16;
    int64_t  input_buffer_timer_q16;
    uint32_t input_buffer_frames;
    bool     input_buffered;
    int64_t  buffered_throttle_q16;
    int64_t  buffered_pitch_rate_q16;
    int64_t  buffered_yaw_rate_q16;
    int64_t  buffered_roll_rate_q16;
    bool     buffered_toggle_layer;

    /* Stage Handoff Polyhedron & Hysteresis */
    FlowStagePoly stage_poly;
    uint64_t stage_mask;
    uint32_t current_layer;        /* 0 = Layer 0 Micro SDF, 1 = Layer 1 Celestial Dome */

    /* Wave64 Topological Tile Entropy */
    FlowJet2 tile_jets[FLOW_GAME_WAVE64_GROUPS][FLOW_WAVE64_SIZE];
    uint64_t wave64_ballots[FLOW_GAME_WAVE64_GROUPS];
    uint32_t active_tile_indices[FLOW_GAME_TOTAL_TILES];
    uint32_t active_tile_count;

    /* Ricardian v4 Economic State */
    RicardianStateV4 ricardian;
    uint64_t ricardian_epoch;
    uint32_t thermal_shutdown;

    /* FlowCineRig64 Cinematography Brain & Physical Camera Optics */
    FlowCineBrainState cine_brain;

    /* Unified Relativistic & Medium Scale Interface (64-byte univalent) */
    FlowRelativeScale64 relative_scale;

    /* 120 FPS (8.33ms) Dual-Threshold Frame-Time Hysteresis Governor */
    FlowStagePoly fps_governor_poly;
    FlowFrameGovernor64 fps_governor;

    /* Frame Clock & Verification */
    uint64_t frame_index;
    uint64_t hardware_sieve;

    /* Lock-Free SPSC Ring & QSBR Epoch Context */
    FlowGamePacketRing ring;
    FlowReloadContext *qsbr_ctx;
    FlowReloadReader qsbr_reader;
} FlowGameEngineState;

/*
 * Engine Public API (Zero float/double)
 */
int flow_game_init(FlowGameEngineState *engine);
void flow_game_destroy(FlowGameEngineState *engine);

/* Unified Relativistic & Medium Scale Interface Evaluator (Zero Hardcoded Classes) */
void flow_game_resolve_relative_scale(const FlowJet2 self_traj[3],
                                      const FlowJet2 medium_traj[3],
                                      int64_t c_eff_q16,
                                      int64_t schwarzschild_rs_q16,
                                      int64_t self_energy_flux_q16,
                                      int64_t env_pressure_flux_q16,
                                      int64_t self_length_q16,
                                      int64_t field_length_q16,
                                      FlowRelativeScale64 *out_scale);

/*
 * 3D Einstein-Lorentz Velocity Transformation & Light-Cone Invariance:
 *   - flow_game_lorentz_rel_vel_q16: Transforms world velocity u_world_q16[3]
 *     into the co-moving Lorentz rest frame of v_frame_q16[3] under universal
 *     constant speed of light c_eff_q16, returning ||u_local|| in Q16.
 *     Guarantees:
 *       (1) Constancy of c: ||u_world|| == c  ==>  ||u_local|| == c for all ||v_frame|| < c.
 *       (2) Subluminal closure: ||u_world|| < c && ||v_frame|| < c  ==>  ||u_local|| < c.
 *       (3) Co-moving invariance: u_world == v_frame  ==>  u_local == 0.
 *   - flow_game_lorentz_emit_light_world_q16: Computes the world-frame velocity
 *     out_u_world_q16[3] of a light beam emitted in local direction local_dir_q16[3]
 *     by a source moving at v_frame_q16[3], guaranteeing ||out_u_world|| == c_eff_q16.
 */
int64_t flow_game_lorentz_rel_vel_q16(const int64_t u_world_q16[3],
                                      const int64_t v_frame_q16[3],
                                      int64_t c_eff_q16,
                                      int64_t out_u_local_q16[3]);

int64_t flow_game_lorentz_emit_light_world_q16(const int64_t v_frame_q16[3],
                                               const int64_t local_dir_q16[3],
                                               int64_t c_eff_q16,
                                               int64_t out_u_world_q16[3]);

/*
 * Einstein Elevator Equivalence & Co-Moving Lorentz Frame Transform:
 * Transforms any world-frame 2-Jet trajectory (obj_world_traj) under ambient
 * gravity field (gravity_accel_q16) into the local co-moving reference frame
 * of an elevator (elevator_world_traj) preserving constancy of light speed c:
 *   - out_local_traj[3]  : What the elevator passenger observes (Lorentz boost + Equivalence)
 *   - out_proper_g_q16[3]: Proper G-force felt by the passenger (a_elev - g_field)
 */
void flow_game_eval_einstein_elevator(const FlowJet2 elevator_world_traj[3],
                                      const FlowJet2 obj_world_traj[3],
                                      const int64_t gravity_accel_q16[3],
                                      FlowJet2 out_local_traj[3],
                                      int64_t out_proper_g_q16[3]);

void flow_game_input(FlowGameEngineState *engine,
                     int64_t throttle_q16,
                     int64_t pitch_rate_q16,
                     int64_t yaw_rate_q16,
                     int64_t roll_rate_q16,
                     bool toggle_layer);

int flow_game_step(FlowGameEngineState *engine, int64_t dt_q16);

/* Subsystem 1: Relativistic Worldline Anti-Tunneling & Concentric SDF Padding */
bool flow_game_check_worldline_collision(const FlowJet2 traj[3],
                                         const int64_t center_q16[3],
                                         int64_t radius_q16,
                                         int64_t dt_q16,
                                         int64_t *out_t_hit_q16);

FlowHitZone flow_game_eval_worldline_padded(const FlowJet2 traj[3],
                                            const int64_t center_q16[3],
                                            int64_t radius_q16,
                                            int64_t delta_hurt_q16,
                                            int64_t delta_graze_q16,
                                            int64_t delta_hit_q16,
                                            int64_t dt_q16,
                                            FlowContactManifold64 *out_manifold);

/* Contact Skin Dual-Threshold Hysteresis [delta_enter, delta_exit] */
uint32_t flow_game_step_contact_hysteresis(FlowGameEngineState *engine,
                                           int64_t sdf_dist_q16);

/* Time-Dimension Action Feel: Hitstop & 2-Jet Impulse Kick */
void flow_game_trigger_hitstop(FlowGameEngineState *engine,
                               uint32_t frames,
                               int64_t scale_q16);

void flow_game_apply_impulse_jet2(FlowGameEngineState *engine,
                                  const int64_t dv_q16[3],
                                  const int64_t da_q16[3]);

/* Subsystem 2: Forced Perspective & Stage Handoff */
uint32_t flow_game_step_stage_handoff(FlowGameEngineState *engine,
                                      const int64_t pos_q16[3]);

/* Subsystem 3: Topological Entropy Sparse Wave64 Culling */
uint32_t flow_game_cull_tiles_wave64(FlowGameEngineState *engine);

/* Subsystem 4: Ricardian v4 Economy Step */
void flow_game_step_ricardian(FlowGameEngineState *engine,
                              uint64_t valuation,
                              uint64_t tax_rate_bps,
                              uint64_t swap_amt,
                              uint64_t fee_bps,
                              uint64_t temp_delta_kelvin);

/* Lock-free Packet Polling */
bool flow_game_poll_frame_packet(FlowGameEngineState *engine,
                                 FlowGameFramePacket *out_packet);

/* Subsystem 5: O(1) Phase-Staggered Wave64 Timing Wheel (Zero Float/Double) */
void flow_timing_wheel_init(FlowTimingWheel64 *wheel);
uint32_t flow_timing_wheel_schedule_staggered(FlowTimingWheel64 *wheel,
                                              uint32_t slot_buckets[64],
                                              uint64_t entity_id,
                                              uint32_t base_period_ticks);
uint32_t flow_timing_wheel_step_o1(FlowTimingWheel64 *wheel,
                                   uint32_t slot_buckets[64]);

/*
 * ============================================================================
 * Subsystem 6: Unified Keybind + VR 6-DoF Spatial Intent & External C ABI
 *              (FlowInputIntent64)
 * ============================================================================
 * Unifies Keyboard/Mouse (0-Jet/1-Jet), Gamepad/HOTAS (1-Jet), OpenXR/VisionOS
 * VR 6-DoF Controllers (2-Jet), and External App/AI Plugin APIs into a single
 * 64-byte cache-line-aligned intent packet:
 *   - 1-Cycle Ternary Chord Keybind Resolution (req_mask / forbid_mask)
 *   - 0-Jet/1-Jet Keymouse-to-2-Jet Trajectory Lifting (Device Compatibility)
 *   - Analytic SDF VR Spatial Interaction + Anti-Waggle Kinetic Momentum Gate
 * ============================================================================
 */
typedef enum {
    FLOW_DEVICE_KEYMOUSE     = 0, /* 0-Jet keys + 1-Jet mouse delta lifted to 2-Jet */
    FLOW_DEVICE_GAMEPAD      = 1, /* Dual analog sticks + triggers lifted to 2-Jet */
    FLOW_DEVICE_VR_6DOF      = 2, /* Native 6-DoF HMD + Hand Worldline 2-Jet (OpenXR) */
    FLOW_DEVICE_EXTERNAL_API = 3  /* Zero-copy C ABI / IPC / AI Agent intent injection */
} FlowInputDeviceClass;

#define FLOW_ACTION_PRIMARY_STRIKE   (1ULL << 0)
#define FLOW_ACTION_SECONDARY_GUARD  (1ULL << 1)
#define FLOW_ACTION_THRUST_BOOST     (1ULL << 2)
#define FLOW_ACTION_VR_GRAB_PINCH    (1ULL << 3)
#define FLOW_ACTION_ULTIMATE_CHORD   (1ULL << 4)

typedef struct __attribute__((aligned(64))) {
    uint64_t raw_device_chord_mask; /* 8B: Physical switches / scancodes / VR pinch bits */
    uint64_t semantic_action_mask;  /* 8B: Resolved game actions via 1-cycle chord map */
    int32_t  hand_pos_q16[3];       /* 12B: 6-DoF effector / cursor position (x,y,z) in Q16 */
    int32_t  hand_vel_q16[3];       /* 12B: 1-Jet effector velocity (vx,vy,vz) in Q16 */
    int32_t  hand_acc_q16[3];       /* 12B: 2-Jet effector acceleration (ax,ay,az) in Q16 */
    int32_t  stereo_ipd_q16;        /* 4B: Binocular IPD in Q16 (0=2D, 4194=64mm VR stereo) */
    uint32_t device_class;          /* 4B: FlowInputDeviceClass (0..3) */
    uint32_t haptic_freq_out_q16;   /* 4B: Output tactile/haptic resonance returned to caller */
} FlowInputIntent64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowInputIntent64, RicardianStateV4);
_Static_assert(sizeof(FlowInputIntent64) == 64U, "FlowInputIntent64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowInputIntent64) == 64U, "FlowInputIntent64 must be 64-byte aligned");

/* 1-Cycle Ternary Keybind / Gesture Chord Resolver */
bool flow_input_resolve_keybind_chord(FlowInputIntent64 *intent,
                                      uint64_t required_switch_mask,
                                      uint64_t forbidden_switch_mask,
                                      uint64_t semantic_action_bit);

/* Lift 0-Jet/1-Jet Keyboard & Mouse input into the same 2-Jet 6-DoF space as VR */
FlowInputIntent64 flow_input_lift_keymouse_to_intent(uint64_t raw_key_mask,
                                                     int32_t cursor_x_q16,
                                                     int32_t cursor_y_q16,
                                                     int32_t reach_z_q16,
                                                     int32_t virtual_swing_vel_q16);

/* Evaluate VR / Spatial / Keymouse 2-Jet effector against an analytic SDF target
 * with kinetic momentum gating (prevents VR wrist-waggle exploits) */
FlowHitZone flow_input_eval_spatial_interaction(FlowInputIntent64 *intent,
                                                const FlowPrefabDesc64 *target_prefab,
                                                int64_t min_lethal_speed_q16,
                                                int64_t dt_q16,
                                                FlowContactManifold64 *out_manifold);

/*
 * ============================================================================
 * Subsystem 7: Zero-Framework Headless Server & 64-Byte UDP 2-Jet Protocol
 *              (FlowUdpSnapshot64)
 * ============================================================================
 * Replaces HTTP/TCP/JSON backend frameworks with raw non-blocking UDP 64-byte
 * datagrams (1/20th of MTU, zero IP fragmentation, zero Head-of-Line blocking):
 *   - 24B FlowJet2 traj_z (z0, vz0, az0) enables exact quadratic Taylor
 *     Dead-Reckoning across dropped UDP packets!
 *   - Monotonic sequence_id silently discards stale/out-of-order datagrams.
 *   - 64-bit state_mac prevents packet tampering.
 * ============================================================================
 */
typedef struct __attribute__((aligned(64))) {
    FlowJet2 traj_z;                /* 24B: Authoritative 2-Jet (z0, vz0, az0) at server_tick */
    int32_t  lateral_xy_q16[2];     /* 8B:  Lateral position (x0, y0) in Q16 */
    int32_t  lateral_vel_xy_q16[2]; /* 8B:  Lateral velocity (vx0, vy0) in Q16 */
    uint64_t sequence_id;           /* 8B:  Monotonic UDP sequence number (drops stale packets) */
    uint32_t server_tick;           /* 4B:  Authoritative simulation tick */
    uint32_t active_action_mask;    /* 4B:  Resolved semantic action bitmask */
    uint64_t state_mac;             /* 8B:  64-bit anti-tamper MAC signature */
} FlowUdpSnapshot64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowUdpSnapshot64, RicardianStateV4);
_Static_assert(sizeof(FlowUdpSnapshot64) == 64U, "FlowUdpSnapshot64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowUdpSnapshot64) == 64U, "FlowUdpSnapshot64 must be 64-byte aligned");

FlowUdpSnapshot64 flow_udp_pack_snapshot64(const FlowGameEngineState *engine,
                                           uint64_t sequence_id,
                                           uint32_t active_action_mask);

bool flow_udp_verify_snapshot64(const FlowUdpSnapshot64 *snap);

int64_t flow_udp_dead_reckon_z_q16(const FlowUdpSnapshot64 *last_valid_snap,
                                   int64_t elapsed_since_snap_q16);

/* Non-blocking POSIX SOCK_DGRAM (UDP) 64-byte loopback & transceiver */
int flow_udp_open_loopback_pair(int *out_server_fd, int *out_client_fd, uint16_t *out_server_port);
int flow_udp_send_snapshot64(int server_fd, uint16_t client_port, const FlowUdpSnapshot64 *snap);
bool flow_udp_recv_latest_snapshot64(int client_fd,
                                     uint64_t *inout_last_seq,
                                     FlowUdpSnapshot64 *out_latest_snap);

/* Zero-GPU Headless Authoritative Simulation Runner */
int flow_game_run_headless_ticks(uint32_t num_ticks, FlowUdpSnapshot64 *out_final_snap);

/*
 * ============================================================================
 * Subsystem 8: 64-Byte Zero-Downtime QSBR Hot-Patch & O(1) Lazy Sector
 *              Evaluator (FlowLazySector64)
 * ============================================================================
 *   1. 64B Micro-Delta Hot-Patch: Replaces gigabyte .pak patches with a single
 *      64-byte sector/prefab descriptor hot-swapped mid-flight via lock-free
 *      QSBR (flow_qsbr_publish_ptr / flow_qsbr_load_ptr) with 0ms stall.
 *   2. O(1) Time-Skip Lazy Evaluation: Offscreen sectors consume 0 CPU ticks;
 *      when visited after arbitrary elapsed time dt (e.g. 1,000s), their exact
 *      state z(t) = z0 + v0*dt + 0.5*a0*dt^2 and LOD Collapse Layer (HUD ->
 *      2D Slice -> 3D SDF) are evaluated in O(1) without step loops.
 * ============================================================================
 */
typedef struct __attribute__((aligned(64))) {
    FlowJet2 anchor_traj_z;       /* 24B: Closed-form 2-Jet trajectory (z0, v0, a0) at anchor_epoch_ms */
    uint64_t sector_id;           /* 8B:  World Sector / Encounter ID */
    uint32_t patch_version;       /* 4B:  Live hot-patch generation (v1 -> v2 -> ...) */
    uint32_t anchor_epoch_ms;     /* 4B:  Timestamp (ms) when sector was anchored */
    uint32_t prefab_id;           /* 4B:  Active 64B Prefab ID */
    uint32_t art_style_id;        /* 4B:  Active 64B Art Style ID */
    uint32_t resolved_lazy_layer; /* 4B:  0=Layer0 3D SDF, 1=Layer1 2D Slice, 2=HUD MSDF */
    int32_t  current_z_q16;       /* 4B:  Lazily evaluated instantaneous z(now_ms) in Q16 */
    uint64_t patch_mac;           /* 8B:  64-bit integrity MAC of the 64B patch */
} FlowLazySector64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowLazySector64, RicardianStateV4);
_Static_assert(sizeof(FlowLazySector64) == 64U, "FlowLazySector64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowLazySector64) == 64U, "FlowLazySector64 must be 64-byte aligned");

FlowLazySector64 flow_sector_make_patch64(uint64_t sector_id,
                                          uint32_t patch_version,
                                          uint32_t anchor_epoch_ms,
                                          FlowJet2 anchor_traj_z,
                                          uint32_t prefab_id,
                                          uint32_t art_style_id);

int flow_sector_qsbr_publish_patch(FlowGameEngineState *engine,
                                   const FlowLazySector64 *patch_bank);

bool flow_sector_qsbr_eval_lazy_o1(FlowGameEngineState *engine,
                                   uint32_t now_ms,
                                   int64_t player_z_q16,
                                   FlowLazySector64 *out_evaluated);

/*
 * ============================================================================
 * Subsystem 9: 64-Byte Deterministic Savegame Capsule, Hex128 Clipboard Share
 *              Code & Driverless Anti-Cheat Replay Verifier (FlowSaveCapsule64)
 * ============================================================================
 *   1. 64B Deterministic Save Capsule: Compresses the entire player save state
 *      into a single 64-byte cache-line univalent descriptor (Initial_Seed,
 *      Tick_Count, World_Facts_Bitmask, Ricardian_Vault, Final_Craft_Z/Vz,
 *      Input_Hash_Chain, State_MAC).
 *   2. Hex128 Clipboard Share Code: Encodes/decodes the 64-byte capsule into a
 *      128-character uppercase hexadecimal string (+ NUL = 129B) for instant
 *      Discord / Steam Community / Social Media clipboard sharing.
 *   3. Driverless Anti-Cheat Replay Audit: Because the entire engine is 100%
 *      Q48.16 integer deterministic (zero float/double), any leaderboard claim
 *      or save capsule is verified in milliseconds by replaying the recorded
 *      input stream from Initial_Seed and auditing (input_hash_chain,
 *      ricardian_vault, final_craft_z_q16, final_craft_vz_q16, world_facts)
 *      without invasive Ring-0 kernel drivers.
 * ============================================================================
 */
typedef struct __attribute__((aligned(64))) {
    uint64_t initial_seed;         /* 8B:  Deterministic PRNG / world generation seed */
    uint64_t tick_count;           /* 8B:  Authoritative simulation ticks elapsed */
    uint64_t world_facts_bitmask;  /* 8B:  World Facts Bitmask (current_layer, thermal_shutdown, etc.) */
    uint64_t ricardian_vault;      /* 8B:  Ricardian m0_vault economic balance */
    int64_t  final_craft_z_q16;    /* 8B:  Authoritative final craft Z position (Q48.16) */
    int64_t  final_craft_vz_q16;   /* 8B:  Authoritative final craft Z velocity (Q48.16) */
    uint64_t input_hash_chain;     /* 8B:  64-bit hash-chain of every tick's input command */
    uint64_t state_mac;            /* 8B:  64-bit cryptographic MAC of [0..55] */
} FlowSaveCapsule64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowSaveCapsule64, RicardianStateV4);
_Static_assert(sizeof(FlowSaveCapsule64) == 64U, "FlowSaveCapsule64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowSaveCapsule64) == 64U, "FlowSaveCapsule64 must be 64-byte aligned");

typedef struct {
    int64_t  throttle_q16;
    int64_t  pitch_rate_q16;
    int64_t  yaw_rate_q16;
    int64_t  roll_rate_q16;
    uint32_t toggle_layer;
    uint32_t trigger_ricardian;
    uint64_t ricardian_Cost;
    uint64_t ricardian_Temp;
} FlowReplayInputCmd;

uint64_t flow_save_init_hash_chain(uint64_t initial_seed);

uint64_t flow_save_chain_input(uint64_t prev_hash_chain,
                               uint32_t tick_index,
                               const FlowReplayInputCmd *cmd);

FlowSaveCapsule64 flow_save_pack_capsule64(const FlowGameEngineState *engine,
                                           uint64_t initial_seed,
                                           uint64_t input_hash_chain);

bool flow_save_verify_capsule64(const FlowSaveCapsule64 *capsule);

bool flow_save_restore_engine(FlowGameEngineState *engine,
                              const FlowSaveCapsule64 *capsule);

void flow_save_encode_hex128(const FlowSaveCapsule64 *capsule, char out_hex[129]);

bool flow_save_decode_hex128(const char hex[129], FlowSaveCapsule64 *out_capsule);

bool flow_save_run_recorded_session(uint64_t initial_seed,
                                    const FlowReplayInputCmd *inputs,
                                    uint32_t num_ticks,
                                    FlowSaveCapsule64 *out_capsule);

bool flow_save_audit_replay_anticheat(const FlowSaveCapsule64 *claimed_capsule,
                                      const FlowReplayInputCmd *recorded_inputs,
                                      uint32_t num_ticks);

/*
 * ============================================================================
 * Subsystem 10: 120 FPS (8.33 ms) Dual-Threshold Frame-Time Hysteresis Governor
 *               Public API
 * ============================================================================
 */
void flow_fps_governor_init(FlowGameEngineState *engine);

void flow_fps_governor_set_mode(FlowGameEngineState *engine,
                                uint32_t target_fps,
                                bool vsync_enabled);

uint32_t flow_game_step_fixed_accumulator(FlowGameEngineState *engine,
                                          uint32_t elapsed_wall_us,
                                          uint32_t *inout_accum_us,
                                          int32_t *out_render_alpha_q16);

FlowFrameGovernor64 flow_game_step_fps_governor(FlowGameEngineState *engine,
                                                uint32_t measured_frame_us);

bool flow_fps_governor_verify(const FlowFrameGovernor64 *gov);

#endif /* FLOW_GAME_H */

