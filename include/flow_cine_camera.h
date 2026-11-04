#ifndef FLOW_CINE_CAMERA_H
#define FLOW_CINE_CAMERA_H

/*
 * ============================================================================
 * BLOOM: FlowCineRig64 Cinematography & Physical Camera Optics System
 * ============================================================================
 * Hybrid Architecture (100% Pure C17 Integer Q48.16, Zero Float/Double):
 *   1. Unity Cinemachine Brain & Composer:
 *      - FlowComposerPoly (FLOW_QPOLY_DEFINE_EX) dual-threshold dead-zone /
 *        soft-zone hysteresis (b_enter / b_exit) with cubical neighbor homotopy.
 *      - 2nd-order damping follow via FlowJet2 + flow_jet2_ema_step.
 *      - 6D procedural trauma / camera shake impulse envelope.
 *      - Smooth Virtual Camera (VCam) blending across 4 canonical rigs.
 *   2. Unreal CineCamera Physical Optics:
 *      - Physical focal length (18mm..135mm) & aperture (f/1.4..f/11) driving
 *        forced-perspective telephoto compression and FOV breathing.
 *      - Physical Circle-of-Confusion (CoC) Depth of Field (DoF).
 *      - Anamorphic horizontal blue lens flares on >800K highlights.
 *      - 180-degree shutter velocity motion blur driven by FlowJet2.d1.
 *      - EV auto-exposure adaptation via flow_jet2_ema_step.
 *   3. Godot Declarative Easing:
 *      - Pure Q16 polynomial easing curves (Linear, Hermite Smoothstep,
 *        Perlin Quintic Smootherstep, Cubic In-Out, Critically Damped Spring).
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
 * 1. Canonical Virtual Camera IDs & Easing Curves
 * ----------------------------------------------------------------------------
 */
typedef enum {
    FLOW_VCAM_ORBIT_TELEPHOTO  = 0, /* 135mm f/1.4 telephoto forced-perspective compression, 2.39:1 */
    FLOW_VCAM_CHASE_DYNAMIC    = 1, /* 24mm f/4.0 dynamic chase cam, rule-of-thirds, FOV breathing */
    FLOW_VCAM_FLYBY_DOLLY      = 2, /* 35mm f/2.0 geodesic dolly flyby past fluid & 800K cavity */
    FLOW_VCAM_COCKPIT_TACTICAL = 3, /* 18mm f/8.0 first-person cockpit with SDD SVG HUD & G-lag */
    FLOW_VCAM_COUNT            = 4,

    /* Short aliases matching specification */
    VCAM_ORBIT_TELEPHOTO       = FLOW_VCAM_ORBIT_TELEPHOTO,
    VCAM_CHASE_DYNAMIC         = FLOW_VCAM_CHASE_DYNAMIC,
    VCAM_FLYBY_DOLLY           = FLOW_VCAM_FLYBY_DOLLY,
    VCAM_COCKPIT_TACTICAL      = FLOW_VCAM_COCKPIT_TACTICAL
} FlowVCamId;

typedef enum {
    FLOW_EASE_LINEAR           = 0, /* Linear interpolation t */
    FLOW_EASE_SMOOTHSTEP_CUBIC = 1, /* Hermite cubic 3t^2 - 2t^3 */
    FLOW_EASE_SMOOTHERSTEP_5   = 2, /* Perlin quintic 6t^5 - 15t^4 + 10t^3 */
    FLOW_EASE_CUBIC_IN_OUT     = 3, /* Piecewise symmetric cubic ease-in-out */
    FLOW_EASE_SPRING_DAMPED    = 4  /* Damped spring response with subtle overshoot */
} FlowEaseCurve;

/* Optical & Composer Feature Flags */
#define FLOW_CINE_FLAG_LETTERBOX_239    (1U << 0) /* 2.39:1 Panavision anamorphic letterbox */
#define FLOW_CINE_FLAG_ANAMORPHIC_FLARE (1U << 1) /* Horizontal blue anamorphic streak flare */
#define FLOW_CINE_FLAG_DOF_BOKEH        (1U << 2) /* Circle-of-Confusion Depth of Field */
#define FLOW_CINE_FLAG_MOTION_BLUR_180  (1U << 3) /* 180-deg shutter velocity motion blur */
#define FLOW_CINE_FLAG_AUTO_EXPOSURE    (1U << 4) /* EV auto-exposure adaptation */
#define FLOW_CINE_FLAG_CHROMATIC_DISP   (1U << 5) /* Edge lateral chromatic dispersion */
#define FLOW_CINE_FLAG_COCKPIT_HUD      (1U << 6) /* Full SDD SVG tactical cockpit HUD */

/*
 * ----------------------------------------------------------------------------
 * 2. FlowCineRig64: 64-Byte Cache-Line Aligned Virtual Camera Descriptor
 * ----------------------------------------------------------------------------
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t vcam_id;               /* 4B: Virtual Camera ID (0..3) */
    uint32_t flags;                 /* 4B: Optical & composer flags (FLOW_CINE_FLAG_*) */
    int32_t  focal_length_q16;      /* 4B: Physical focal length in mm (Q16: 18mm..135mm) */
    int32_t  aperture_fstop_q16;    /* 4B: Physical aperture f-number in Q16 (f/1.4..f/11) */
    int32_t  focus_dist_q16;        /* 4B: Focus plane distance in meters (Q16) */
    int32_t  follow_damping_q16;    /* 4B: 2-Jet EMA damping alpha in Q16 (0..65536) */
    int32_t  look_offset_xy_q16[2]; /* 8B: Rule-of-thirds screen-space composer offset (x, y) in Q16 */
    int32_t  deadzone_enter_q16[2]; /* 8B: Soft-zone trigger threshold b_enter (x, y) in Q16 */
    int32_t  deadzone_exit_q16[2];  /* 8B: Dead-zone recovery hysteresis b_exit (x, y) in Q16 */
    int32_t  local_offset_q16[3];   /* 12B: Camera anchor offset (x, y, z) in Q16 */
    int32_t  shake_gain_q16;        /* 4B: 6D impulse / trauma sensitivity multiplier in Q16 */
} FlowCineRig64;

typedef struct __attribute__((aligned(64))) {
    int64_t q_word[8];
} FlowCineRigShadow64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowCineRig64, FlowCineRigShadow64);
FLOW_STATIC_ASSERT_UNIVALENT(FlowCineRig64, RicardianStateV4);
_Static_assert(sizeof(FlowCineRig64) == 64U, "FlowCineRig64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowCineRig64) == 64U, "FlowCineRig64 must be 64-byte aligned");

/*
 * ----------------------------------------------------------------------------
 * 3. FlowCineResolvedFrame64: 64-Byte Resolved Camera Snapshot for Renderer
 * ----------------------------------------------------------------------------
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t active_vcam_id;        /* 4B: Current primary VCam ID */
    uint32_t active_flags;          /* 4B: Active optical & HUD feature flags */
    int32_t  focal_length_q16;      /* 4B: Effective focal length in mm (Q16) */
    int32_t  aperture_fstop_q16;    /* 4B: Effective aperture f-stop in Q16 */
    int32_t  focus_dist_q16;        /* 4B: Effective focus distance in meters (Q16) */
    int32_t  exposure_gain_q16;     /* 4B: Auto-exposure linear gain in Q16 (65536 = 1.0x) */
    int32_t  cam_offset_q16[3];     /* 12B: Resolved camera offset + 6D shake (x, y, z Q16) */
    int32_t  look_offset_xy_q16[2]; /* 8B: Resolved composer offset + G-force lag (x, y Q16) */
    int32_t  shutter_vel_q16[3];    /* 12B: 180-deg shutter velocity vector from FlowJet2.d1 */
    int32_t  blend_weight_q16;      /* 4B: Current VCam transition eased weight [0..65536] */
    uint32_t composer_mask;         /* 4B: Active soft-zone hysteresis mask (0 = dead-zone) */
} FlowCineResolvedFrame64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowCineResolvedFrame64, RicardianStateV4);
_Static_assert(sizeof(FlowCineResolvedFrame64) == 64U, "FlowCineResolvedFrame64 must be 64 bytes");
_Static_assert(_Alignof(FlowCineResolvedFrame64) == 64U, "FlowCineResolvedFrame64 must be 64-byte aligned");

/*
 * ----------------------------------------------------------------------------
 * 4. Screen-Space Composer Polyhedron & Cinemachine Brain State
 * ----------------------------------------------------------------------------
 */
/* 2D Screen-Space Composer Polyhedron (DIM=2, SHIFT=16, PLANES=4: +X, -X, +Y, -Y) */
FLOW_QPOLY_DEFINE_EX(FlowComposerPoly, 2, 16, 4)

typedef struct {
    /* L1-Resident Canonical Virtual Camera Table (4 x 64B = 256B) */
    FlowCineRig64 vcams[FLOW_VCAM_COUNT];
    FlowCineRig64 blend_from_rig; /* Live camera snapshot captured at blend trigger */

    /* Active & Blending Virtual Camera State */
    uint32_t active_vcam;
    uint32_t from_vcam;
    int64_t  blend_elapsed_q16;
    int64_t  blend_duration_q16;
    int64_t  blend_weight_q16;
    FlowEaseCurve blend_curve;
    bool     is_blending;

    /* 2-Jet Damped Worldline Follow & Optical Trajectories */
    FlowJet2 cam_offset_jet[3];
    FlowJet2 prev_cam_offset_jet[3];
    FlowJet2 look_offset_jet[2];
    FlowJet2 focal_jet;
    FlowJet2 aperture_jet;
    FlowJet2 focus_dist_jet;
    FlowJet2 exposure_ev_jet;

    /* Dead-Zone / Soft-Zone Dual-Threshold Hysteresis Composer */
    FlowComposerPoly composer_poly;
    uint64_t composer_mask;

    /* 6D Trauma / Camera Shake Impulse Envelope */
    int64_t  trauma_q16;
    uint64_t shake_tick;
    int32_t  shake_pos_q16[3];
    int32_t  shake_rot_q16[2];

    /* Hardware Self-Check Sieve & Latest Resolved Frame */
    uint64_t hardware_sieve;
    FlowCineResolvedFrame64 resolved;
} FlowCineBrainState;

/*
 * ----------------------------------------------------------------------------
 * 5. Public C17 Cinematography & Optics API (Zero Float/Double)
 * ----------------------------------------------------------------------------
 */
extern const FlowCineRig64 FLOW_CANONICAL_VCAMS[FLOW_VCAM_COUNT];

int flow_cine_brain_init(FlowCineBrainState *brain);
const FlowCineRig64 *flow_cine_get_vcam(uint32_t vcam_id);

/* Declarative Q16 Easing & 2-Jet Blending */
int64_t flow_cine_ease_q16(int64_t t_q16, FlowEaseCurve curve);
FlowJet2 flow_cine_blend_jet2(FlowJet2 a, FlowJet2 b, int64_t t_q16, FlowEaseCurve curve);

/* VCam Transition & Trauma Impulse Control */
void flow_cine_trigger_blend(FlowCineBrainState *brain,
                             uint32_t target_vcam_id,
                             int64_t duration_q16,
                             FlowEaseCurve curve);
void flow_cine_cycle_vcam(FlowCineBrainState *brain, int64_t duration_q16);
void flow_cine_add_trauma(FlowCineBrainState *brain, int64_t impulse_q16);

/* Dead-Zone / Soft-Zone Dual-Threshold Hysteresis Composer Evaluation */
uint32_t flow_cine_eval_composer_hysteresis(FlowCineBrainState *brain,
                                            int32_t err_x_q16,
                                            int32_t err_y_q16,
                                            int32_t *out_corr_x_q16,
                                            int32_t *out_corr_y_q16);

/* Physical Circle-of-Confusion (CoC) Depth of Field Evaluator in Q16 */
int64_t flow_cine_calc_coc_q16(int64_t depth_q16,
                               int64_t focal_mm_q16,
                               int64_t fstop_q16,
                               int64_t focus_dist_q16);

/* Main Per-Frame Cinematography Brain Step */
int flow_cine_brain_step(FlowCineBrainState *brain,
                         const FlowJet2 craft_traj[3],
                         const FlowJet2 craft_rot[3],
                         uint64_t thermal_kelvin,
                         bool collision_hit,
                         int64_t dt_q16);

#endif /* FLOW_CINE_CAMERA_H */
