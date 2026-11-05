#include "flow_cine_camera.h"
#include <string.h>

/*
 * ============================================================================
 * BLOOM: Pure C17 Integer Q48.16 Cinematography & Physical Optics Engine
 * ============================================================================
 * Strict Zero Float / Double Guarantee:
 * All easing curves, 2-Jet EMA damping steps, dead-zone hysteresis polyhedra,
 * Circle-of-Confusion DoF calculations, EV auto-exposure, and 6D shake
 * envelopes use 100% deterministic integer arithmetic (int64_t, __int128).
 * ============================================================================
 */

__extension__ typedef __int128 flow_cine_i128;

static inline int64_t flow_cine_q16_mul(int64_t a, int64_t b) {
    flow_cine_i128 p = ((flow_cine_i128)a * (flow_cine_i128)b) >> FLOW_Q16_SHIFT;
    return flow_q16_clamp_i128(p);
}

static inline int64_t flow_cine_q16_div(int64_t a, int64_t b) {
    if (b == 0)
        return 0;
    flow_cine_i128 num = ((flow_cine_i128)a) << FLOW_Q16_SHIFT;
    return flow_q16_clamp_i128(num / (flow_cine_i128)b);
}

static inline int64_t flow_cine_clamp_i64(int64_t v, int64_t lo, int64_t hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static inline int32_t flow_cine_clamp_i32(int64_t v) {
    if (v > (int64_t)INT32_MAX)
        return INT32_MAX;
    if (v < (int64_t)INT32_MIN)
        return INT32_MIN;
    return (int32_t)v;
}

/* Fast pure-integer Q16 triangle wave oscillator on period 65536 -> [-65536, +65536] */
static inline int64_t flow_cine_tri_wave_q16(uint64_t phase_q16) {
    uint64_t p = phase_q16 & 0xFFFFULL;
    int64_t ramp = (int64_t)(p << 2U); /* 0 .. 262140 */
    if (ramp >= (2LL << FLOW_Q16_SHIFT)) {
        ramp = (4LL << FLOW_Q16_SHIFT) - ramp;
    }
    return ramp - FLOW_Q16_ONE;
}

/*
 * ============================================================================
 * 1. Canonical L1-Resident Virtual Camera Table (4 x 64B = 256 Bytes)
 * ============================================================================
 */
const FlowCineRig64 FLOW_CANONICAL_VCAMS[FLOW_VCAM_COUNT] = {
    /* [0] VCAM_ORBIT_TELEPHOTO: 135mm f/1.4 forced-perspective telephoto compression, 2.39:1 */
    {
        .vcam_id            = FLOW_VCAM_ORBIT_TELEPHOTO,
        .flags              = FLOW_CINE_FLAG_LETTERBOX_239 |
                              FLOW_CINE_FLAG_ANAMORPHIC_FLARE |
                              FLOW_CINE_FLAG_DOF_BOKEH |
                              FLOW_CINE_FLAG_AUTO_EXPOSURE |
                              FLOW_CINE_FLAG_CHROMATIC_DISP,
        .focal_length_q16   = 135 * 65536,          /* 135.0 mm telephoto */
        .aperture_fstop_q16 = (14 * 65536) / 10,    /* f/1.4 shallow bokeh */
        .focus_dist_q16     = (48 * 65536) / 10,    /* 4.8 m focus on micro-voxel citadel */
        .follow_damping_q16 = 6554,                 /* 0.10 smooth orbital gimbal damping */
        .look_offset_xy_q16 = { -5243, -1966 },     /* Rule-of-thirds composition */
        .deadzone_enter_q16 = { 6554, 6554 },       /* 0.10 soft-zone enter */
        .deadzone_exit_q16  = { 3277, 3277 },       /* 0.05 dead-zone exit hysteresis */
        .local_offset_q16   = { -14254, 3604, -250675 }, /* (-0.2175m, +0.055m, -3.825m) telephoto standoff */
        .shake_gain_q16     = 8192                  /* 0.125x stabilized orbital mount */
    },
    /* [1] VCAM_CHASE_DYNAMIC: 24mm f/4.0 wide chase camera, rule-of-thirds, FOV breathing */
    {
        .vcam_id            = FLOW_VCAM_CHASE_DYNAMIC,
        .flags              = FLOW_CINE_FLAG_ANAMORPHIC_FLARE |
                              FLOW_CINE_FLAG_MOTION_BLUR_180 |
                              FLOW_CINE_FLAG_AUTO_EXPOSURE |
                              FLOW_CINE_FLAG_CHROMATIC_DISP,
        .focal_length_q16   = 24 * 65536,           /* 24.0 mm dynamic wide chase */
        .aperture_fstop_q16 = 4 * 65536,            /* f/4.0 balanced depth of field */
        .focus_dist_q16     = (46 * 65536) / 10,    /* 4.6 m focus */
        .follow_damping_q16 = 16384,                /* 0.25 responsive 2nd-order chase follow */
        .look_offset_xy_q16 = { 8520, 3932 },       /* Rule-of-thirds chase framing */
        .deadzone_enter_q16 = { 13107, 9830 },      /* 0.20 X, 0.15 Y enter threshold */
        .deadzone_exit_q16  = { 6554, 4915 },       /* 0.10 X, 0.075 Y exit hysteresis */
        .local_offset_q16   = { 15401, 8192, -27853 }, /* (+0.235m, +0.125m, -0.425m) chase anchor */
        .shake_gain_q16     = 32768                 /* 0.50x dynamic chase vibration */
    },
    /* [2] VCAM_FLYBY_DOLLY: 35mm f/2.0 geodesic dolly flyby past fluid & 800K cavity */
    {
        .vcam_id            = FLOW_VCAM_FLYBY_DOLLY,
        .flags              = FLOW_CINE_FLAG_LETTERBOX_239 |
                              FLOW_CINE_FLAG_ANAMORPHIC_FLARE |
                              FLOW_CINE_FLAG_DOF_BOKEH |
                              FLOW_CINE_FLAG_MOTION_BLUR_180 |
                              FLOW_CINE_FLAG_AUTO_EXPOSURE |
                              FLOW_CINE_FLAG_CHROMATIC_DISP,
        .focal_length_q16   = 35 * 65536,           /* 35.0 mm anamorphic director lens */
        .aperture_fstop_q16 = 2 * 65536,            /* f/2.0 cinematic shallow DoF */
        .focus_dist_q16     = (34 * 65536) / 10,    /* 3.4 m close focus on molten cavity */
        .follow_damping_q16 = 12288,                /* 0.1875 geodesic track damping */
        .look_offset_xy_q16 = { 1966, -2621 },      /* Close-up inspection framing */
        .deadzone_enter_q16 = { 9830, 9830 },       /* 0.15 soft-zone enter */
        .deadzone_exit_q16  = { 4915, 4915 },       /* 0.075 dead-zone exit */
        .local_offset_q16   = { -9175, -3539, 108790 }, /* (-0.140m, -0.054m, +1.660m) close dolly push-in */
        .shake_gain_q16     = 65536                 /* 1.00x full 6D flyby shake & blur */
    },
    /* [3] VCAM_COCKPIT_TACTICAL: 18mm f/8.0 first-person cockpit with SDD SVG HUD & G-lag */
    {
        .vcam_id            = FLOW_VCAM_COCKPIT_TACTICAL,
        .flags              = FLOW_CINE_FLAG_COCKPIT_HUD |
                              FLOW_CINE_FLAG_MOTION_BLUR_180 |
                              FLOW_CINE_FLAG_AUTO_EXPOSURE |
                              FLOW_CINE_FLAG_ANAMORPHIC_FLARE,
        .focal_length_q16   = 18 * 65536,           /* 18.0 mm ultra-wide tactical cockpit */
        .aperture_fstop_q16 = 8 * 65536,            /* f/8.0 deep tactical focus */
        .focus_dist_q16     = (45 * 65536) / 10,    /* 4.5 m focus */
        .follow_damping_q16 = 28672,                /* 0.4375 tight G-force pilot head lag */
        .look_offset_xy_q16 = { 0, 0 },             /* Boresight centered reticle */
        .deadzone_enter_q16 = { 3277, 3277 },       /* Tight 0.05 tactical dead-zone */
        .deadzone_exit_q16  = { 1638, 1638 },       /* 0.025 exit hysteresis */
        .local_offset_q16   = { 0, 0, 15565 },      /* (0.0m, 0.0m, +0.2375m) cockpit seat eye point */
        .shake_gain_q16     = 24576                 /* 0.375x cockpit buffet */
    }
};

const FlowCineRig64 *flow_cine_get_vcam(uint32_t vcam_id) {
    if (vcam_id >= (uint32_t)FLOW_VCAM_COUNT)
        return &FLOW_CANONICAL_VCAMS[0];
    return &FLOW_CANONICAL_VCAMS[vcam_id];
}

/*
 * ============================================================================
 * 2. Declarative Pure Q16 Polynomial Easing & 2-Jet Blending
 * ============================================================================
 */
int64_t flow_cine_ease_q16(int64_t t_q16, FlowEaseCurve curve) {
    if (t_q16 <= 0LL)
        return 0LL;
    if (t_q16 >= FLOW_Q16_ONE)
        return FLOW_Q16_ONE;

    flow_cine_i128 t = (flow_cine_i128)t_q16;

    switch (curve) {
        case FLOW_EASE_LINEAR:
            return t_q16;

        case FLOW_EASE_SMOOTHSTEP_CUBIC: {
            /* Hermite: t^2 * (3 - 2*t) evaluated in exact 128-bit before final >> 32 */
            flow_cine_i128 t2 = t * t; /* Q32 */
            flow_cine_i128 factor = (3LL << FLOW_Q16_SHIFT) - (2 * t); /* Q16 */
            return flow_q16_clamp_i128((t2 * factor) >> (2U * FLOW_Q16_SHIFT));
        }

        case FLOW_EASE_SMOOTHERSTEP_5: {
            /* Perlin quintic: t^3 * (6*t^2 - 15*t + 10) evaluated in exact 128-bit before final >> 64 */
            flow_cine_i128 t2 = t * t;       /* Q32 */
            flow_cine_i128 t3 = t2 * t;      /* Q48 */
            flow_cine_i128 poly_q32 = (6 * t2)
                                    - ((15LL << FLOW_Q16_SHIFT) * t)
                                    + (10LL << (2U * FLOW_Q16_SHIFT)); /* Q32 */
            return flow_q16_clamp_i128((t3 * poly_q32) >> (4U * FLOW_Q16_SHIFT));
        }

        case FLOW_EASE_CUBIC_IN_OUT: {
            /* Piecewise symmetric cubic: 4*t^3 for t < 0.5, 1 - 4*(1-t)^3 for t >= 0.5 */
            if (t_q16 < (FLOW_Q16_ONE >> 1)) {
                flow_cine_i128 t3 = t * t * t; /* Q48 */
                return flow_q16_clamp_i128((4 * t3) >> (2U * FLOW_Q16_SHIFT));
            } else {
                flow_cine_i128 u = (flow_cine_i128)FLOW_Q16_ONE - t;
                flow_cine_i128 u3 = u * u * u; /* Q48 */
                return flow_q16_clamp_i128(
                    (flow_cine_i128)FLOW_Q16_ONE - ((4 * u3) >> (2U * FLOW_Q16_SHIFT))
                );
            }
        }

        case FLOW_EASE_SPRING_DAMPED: {
            /* Damped spring curve: 1 - (1 - t)^2 * (1 - 1.5*t) in exact 128-bit */
            flow_cine_i128 u = (flow_cine_i128)FLOW_Q16_ONE - t;
            flow_cine_i128 u2 = u * u; /* Q32 */
            flow_cine_i128 factor = (flow_cine_i128)FLOW_Q16_ONE - ((3 * t) >> 1); /* Q16 */
            return flow_q16_clamp_i128(
                (flow_cine_i128)FLOW_Q16_ONE - ((u2 * factor) >> (2U * FLOW_Q16_SHIFT))
            );
        }

        default:
            return t_q16;
    }
}

FlowJet2 flow_cine_blend_jet2(FlowJet2 a, FlowJet2 b, int64_t t_q16, FlowEaseCurve curve) {
    if (t_q16 <= 0LL)
        return a;
    if (t_q16 >= FLOW_Q16_ONE)
        return b;

    int64_t w_q16 = flow_cine_ease_q16(t_q16, curve);
    FlowJet2 blended;

    if (w_q16 >= 0LL && w_q16 <= FLOW_Q16_ONE) {
        blended = flow_jet2_ema_step(a, b, w_q16);
    } else {
        /* Preserve underdamped spring overshoot (w_q16 > FLOW_Q16_ONE) in exact 128-bit */
        flow_cine_i128 wa = (flow_cine_i128)(FLOW_Q16_ONE - w_q16);
        flow_cine_i128 wb = (flow_cine_i128)w_q16;
        blended.v  = flow_q16_clamp_i128((wa * (flow_cine_i128)a.v  + wb * (flow_cine_i128)b.v)  >> FLOW_Q16_SHIFT);
        blended.d1 = flow_q16_clamp_i128((wa * (flow_cine_i128)a.d1 + wb * (flow_cine_i128)b.d1) >> FLOW_Q16_SHIFT);
        blended.d2 = flow_q16_clamp_i128((wa * (flow_cine_i128)a.d2 + wb * (flow_cine_i128)b.d2) >> FLOW_Q16_SHIFT);
    }

    /* Couple easing 1st slope (dw/dt) into d1 and 2nd curvature (d^2w/dt^2) into d2 */
    const int64_t eps_q16 = 512LL; /* 1/128 in Q16 */
    int64_t t_next = flow_cine_clamp_i64(t_q16 + eps_q16, 0LL, FLOW_Q16_ONE);
    int64_t t_prev = flow_cine_clamp_i64(t_q16 - eps_q16, 0LL, FLOW_Q16_ONE);
    int64_t dt_span = t_next - t_prev;
    if (dt_span > 0LL) {
        int64_t w_next = flow_cine_ease_q16(t_next, curve);
        int64_t w_prev = flow_cine_ease_q16(t_prev, curve);
        int64_t dw_dt_q16 = flow_cine_q16_div(w_next - w_prev, dt_span);
        int64_t span_v = flow_q16_clamp_i128((flow_cine_i128)b.v - (flow_cine_i128)a.v);
        int64_t trans_vel = flow_cine_q16_mul(span_v, dw_dt_q16);
        blended.d1 = flow_q16_clamp_i128((flow_cine_i128)blended.d1 + (flow_cine_i128)trans_vel);

        /* Second central difference: d^2w / dt^2 = (w_next - 2*w_q16 + w_prev) / eps^2 */
        int64_t h_min = ((t_next - t_q16) < (t_q16 - t_prev)) ? (t_next - t_q16) : (t_q16 - t_prev);
        if (h_min > 0LL) {
            int64_t h_sq_q16 = flow_cine_q16_mul(h_min, h_min);
            if (h_sq_q16 > 0LL) {
                int64_t d2w_num = w_next - (2LL * w_q16) + w_prev;
                int64_t d2w_dt2_q16 = flow_cine_q16_div(d2w_num, h_sq_q16);
                int64_t trans_acc = flow_cine_q16_mul(span_v, d2w_dt2_q16);
                blended.d2 = flow_q16_clamp_i128((flow_cine_i128)blended.d2 + (flow_cine_i128)trans_acc);
            }
        }
    }
    return blended;
}

/* Helper: Step a 2-Jet via flow_jet2_ema_step while maintaining true kinematic d1 & d2 */
static inline FlowJet2 flow_cine_step_damped_jet2(FlowJet2 prev,
                                                  FlowJet2 target,
                                                  int64_t alpha_q16,
                                                  int64_t dt_q16) {
    FlowJet2 next = flow_jet2_ema_step(prev, target, alpha_q16);
    if (dt_q16 > 0LL) {
        int64_t damp_vel = flow_cine_q16_div(next.v - prev.v, dt_q16);
        if (target.d1 == 0LL) {
            next.d1 = damp_vel;
        } else {
            next.d1 = flow_q16_clamp_i128(((flow_cine_i128)next.d1 + (flow_cine_i128)damp_vel) >> 1);
        }
        int64_t damp_acc = flow_cine_q16_div(next.d1 - prev.d1, dt_q16);
        if (target.d2 == 0LL) {
            next.d2 = damp_acc;
        } else {
            next.d2 = flow_q16_clamp_i128(((flow_cine_i128)next.d2 + (flow_cine_i128)damp_acc) >> 1);
        }
    }
    return next;
}

/*
 * ============================================================================
 * 3. Composer Dead-Zone / Soft-Zone Hysteresis Polyhedron Configuration
 * ============================================================================
 */
static void flow_cine_sync_composer_bounds(FlowCineBrainState *brain, const FlowCineRig64 *rig) {
    if (!brain || !rig)
        return;

    int64_t bx_enter = (int64_t)rig->deadzone_enter_q16[0];
    int64_t bx_exit  = (int64_t)rig->deadzone_exit_q16[0];
    int64_t by_enter = (int64_t)rig->deadzone_enter_q16[1];
    int64_t by_exit  = (int64_t)rig->deadzone_exit_q16[1];

    /* Plane 0: +X, Plane 1: -X, Plane 2: +Y, Plane 3: -Y */
    FlowComposerPoly_set_hysteresis(&brain->composer_poly, 0U, bx_enter, bx_exit);
    FlowComposerPoly_set_hysteresis(&brain->composer_poly, 1U, bx_enter, bx_exit);
    FlowComposerPoly_set_hysteresis(&brain->composer_poly, 2U, by_enter, by_exit);
    FlowComposerPoly_set_hysteresis(&brain->composer_poly, 3U, by_enter, by_exit);
}

int flow_cine_brain_init(FlowCineBrainState *brain) {
    if (!brain)
        return -1;

    memset(brain, 0, sizeof(*brain));
    memcpy(brain->vcams, FLOW_CANONICAL_VCAMS, sizeof(FLOW_CANONICAL_VCAMS));

    brain->active_vcam = FLOW_VCAM_COCKPIT_TACTICAL;
    brain->from_vcam = FLOW_VCAM_COCKPIT_TACTICAL;
    brain->blend_from_rig = brain->vcams[brain->active_vcam];
    brain->blend_elapsed_q16 = FLOW_Q16_ONE;
    brain->blend_duration_q16 = FLOW_Q16_ONE;
    brain->blend_weight_q16 = FLOW_Q16_ONE;
    brain->blend_curve = FLOW_EASE_SMOOTHERSTEP_5;
    brain->is_blending = false;
    brain->hardware_sieve = FLOW_ASM_SIEVE_ALL_SOUND;

    const FlowCineRig64 *init_rig = &brain->vcams[brain->active_vcam];

    for (size_t k = 0; k < 3U; ++k) {
        brain->cam_offset_jet[k] = flow_jet2_make((int64_t)init_rig->local_offset_q16[k], 0LL, 0LL);
        brain->prev_cam_offset_jet[k] = brain->cam_offset_jet[k];
    }
    for (size_t k = 0; k < 2U; ++k) {
        brain->look_offset_jet[k] = flow_jet2_make((int64_t)init_rig->look_offset_xy_q16[k], 0LL, 0LL);
    }

    brain->focal_jet      = flow_jet2_make((int64_t)init_rig->focal_length_q16, 0LL, 0LL);
    brain->aperture_jet   = flow_jet2_make((int64_t)init_rig->aperture_fstop_q16, 0LL, 0LL);
    brain->focus_dist_jet = flow_jet2_make((int64_t)init_rig->focus_dist_q16, 0LL, 0LL);
    brain->exposure_ev_jet = flow_jet2_make(FLOW_Q16_ONE, 0LL, 0LL);

    /* Initialize 2D Screen-Space Composer Polyhedron normals: +X, -X, +Y, -Y */
    FlowComposerPoly_init(&brain->composer_poly);
    brain->composer_poly.A[0][0] =  1; brain->composer_poly.A[0][1] =  0;
    brain->composer_poly.A[1][0] = -1; brain->composer_poly.A[1][1] =  0;
    brain->composer_poly.A[2][0] =  0; brain->composer_poly.A[2][1] =  1;
    brain->composer_poly.A[3][0] =  0; brain->composer_poly.A[3][1] = -1;
    brain->composer_mask = 0ULL;
    flow_cine_sync_composer_bounds(brain, init_rig);

    /* Populate initial resolved snapshot */
    brain->resolved.active_vcam_id      = init_rig->vcam_id;
    brain->resolved.active_flags        = init_rig->flags;
    brain->resolved.focal_length_q16    = init_rig->focal_length_q16;
    brain->resolved.aperture_fstop_q16  = init_rig->aperture_fstop_q16;
    brain->resolved.focus_dist_q16      = init_rig->focus_dist_q16;
    brain->resolved.exposure_gain_q16   = (int32_t)FLOW_Q16_ONE;
    brain->resolved.cam_offset_q16[0]   = init_rig->local_offset_q16[0];
    brain->resolved.cam_offset_q16[1]   = init_rig->local_offset_q16[1];
    brain->resolved.cam_offset_q16[2]   = init_rig->local_offset_q16[2];
    brain->resolved.look_offset_xy_q16[0] = init_rig->look_offset_xy_q16[0];
    brain->resolved.look_offset_xy_q16[1] = init_rig->look_offset_xy_q16[1];
    brain->resolved.blend_weight_q16    = (int32_t)FLOW_Q16_ONE;
    brain->resolved.composer_mask       = 0U;

    return 0;
}

void flow_cine_trigger_blend(FlowCineBrainState *brain,
                             uint32_t target_vcam_id,
                             int64_t duration_q16,
                             FlowEaseCurve curve) {
    if (!brain || target_vcam_id >= (uint32_t)FLOW_VCAM_COUNT)
        return;

    uint32_t prev_active = brain->active_vcam;
    bool was_blending = brain->is_blending;

    /* Snapshot current live optical & spatial trajectories so mid-blend re-triggers are C1/C2 continuous */
    brain->from_vcam = prev_active;
    brain->blend_from_rig = brain->vcams[prev_active];
    brain->blend_from_rig.focal_length_q16   = flow_cine_clamp_i32(brain->focal_jet.v);
    brain->blend_from_rig.aperture_fstop_q16 = flow_cine_clamp_i32(brain->aperture_jet.v);
    brain->blend_from_rig.focus_dist_q16     = flow_cine_clamp_i32(brain->focus_dist_jet.v);
    for (size_t k = 0; k < 3U; ++k) {
        brain->blend_from_rig.local_offset_q16[k] = flow_cine_clamp_i32(brain->cam_offset_jet[k].v);
    }
    for (size_t k = 0; k < 2U; ++k) {
        brain->blend_from_rig.look_offset_xy_q16[k] = flow_cine_clamp_i32(brain->look_offset_jet[k].v);
    }

    brain->active_vcam = target_vcam_id;
    brain->blend_curve = curve;

    const FlowCineRig64 *target_rig = &brain->vcams[target_vcam_id];
    flow_cine_sync_composer_bounds(brain, target_rig);

    if (duration_q16 <= 0LL || (!was_blending && prev_active == target_vcam_id)) {
        brain->blend_from_rig = *target_rig;
        brain->blend_duration_q16 = FLOW_Q16_ONE;
        brain->blend_elapsed_q16 = FLOW_Q16_ONE;
        brain->blend_weight_q16 = FLOW_Q16_ONE;
        brain->is_blending = false;

        for (size_t k = 0; k < 3U; ++k) {
            brain->cam_offset_jet[k] = flow_jet2_make((int64_t)target_rig->local_offset_q16[k], 0LL, 0LL);
            brain->prev_cam_offset_jet[k] = brain->cam_offset_jet[k];
        }
        for (size_t k = 0; k < 2U; ++k) {
            brain->look_offset_jet[k] = flow_jet2_make((int64_t)target_rig->look_offset_xy_q16[k], 0LL, 0LL);
        }
        brain->focal_jet      = flow_jet2_make((int64_t)target_rig->focal_length_q16, 0LL, 0LL);
        brain->aperture_jet   = flow_jet2_make((int64_t)target_rig->aperture_fstop_q16, 0LL, 0LL);
        brain->focus_dist_jet = flow_jet2_make((int64_t)target_rig->focus_dist_q16, 0LL, 0LL);
    } else {
        brain->blend_duration_q16 = duration_q16;
        brain->blend_elapsed_q16 = 0LL;
        brain->blend_weight_q16 = 0LL;
        brain->is_blending = true;
    }
}

void flow_cine_cycle_vcam(FlowCineBrainState *brain, int64_t duration_q16) {
    if (!brain)
        return;
    uint32_t next_id = (brain->active_vcam + 1U) % (uint32_t)FLOW_VCAM_COUNT;
    flow_cine_trigger_blend(brain, next_id, duration_q16, FLOW_EASE_SMOOTHERSTEP_5);
}

void flow_cine_add_trauma(FlowCineBrainState *brain, int64_t impulse_q16) {
    if (!brain || impulse_q16 <= 0LL)
        return;
    brain->trauma_q16 = flow_cine_clamp_i64(brain->trauma_q16 + impulse_q16, 0LL, FLOW_Q16_ONE);
}

/*
 * ============================================================================
 * 4. Dead-Zone / Soft-Zone Dual-Threshold Hysteresis Composer
 * ============================================================================
 */
uint32_t flow_cine_eval_composer_hysteresis(FlowCineBrainState *brain,
                                            int32_t err_x_q16,
                                            int32_t err_y_q16,
                                            int32_t *out_corr_x_q16,
                                            int32_t *out_corr_y_q16) {
    if (!brain)
        return 0U;

    const int32_t x_vec[2] = { err_x_q16, err_y_q16 };
    FlowQPolyEval ev = FlowComposerPoly_eval_hysteresis(
        &brain->composer_poly, x_vec, brain->composer_mask
    );

    uint64_t target_mask = ev.violated_mask & 0xFULL;
    uint64_t diff_mask = target_mask ^ brain->composer_mask;

    /* Step each plane transition via cubical neighbor homotopy */
    while (diff_mask != 0ULL) {
        uint32_t plane = (uint32_t)__builtin_ctzll(diff_mask);
        diff_mask &= (diff_mask - 1ULL);
        uint64_t next_mask = 0ULL;
        FlowKanStatus kan = flow_cubical_step_neighbor(
            brain->composer_mask, plane, 0ULL, &next_mask
        );
        if (kan == FLOW_KAN_FILLED_HOMOTOPIC) {
            brain->composer_mask = next_mask;
        }
    }

    int64_t bx_exit = atomic_load_explicit(&brain->composer_poly.b_exit[0], memory_order_relaxed);
    int64_t by_exit = atomic_load_explicit(&brain->composer_poly.b_exit[2], memory_order_relaxed);

    int32_t corr_x = 0;
    int32_t corr_y = 0;

    if (brain->composer_mask & 1ULL) {
        /* +X soft-zone active: pull back toward +bx_exit */
        int64_t delta = (int64_t)err_x_q16 - bx_exit;
        corr_x = (delta > 0) ? flow_cine_clamp_i32(delta) : 0;
    } else if (brain->composer_mask & 2ULL) {
        /* -X soft-zone active: pull back toward -bx_exit */
        int64_t delta = (int64_t)err_x_q16 + bx_exit;
        corr_x = (delta < 0) ? flow_cine_clamp_i32(delta) : 0;
    }

    if (brain->composer_mask & 4ULL) {
        /* +Y soft-zone active: pull back toward +by_exit */
        int64_t delta = (int64_t)err_y_q16 - by_exit;
        corr_y = (delta > 0) ? flow_cine_clamp_i32(delta) : 0;
    } else if (brain->composer_mask & 8ULL) {
        /* -Y soft-zone active: pull back toward -by_exit */
        int64_t delta = (int64_t)err_y_q16 + by_exit;
        corr_y = (delta < 0) ? flow_cine_clamp_i32(delta) : 0;
    }

    if (out_corr_x_q16)
        *out_corr_x_q16 = corr_x;
    if (out_corr_y_q16)
        *out_corr_y_q16 = corr_y;

    return (uint32_t)brain->composer_mask;
}

/*
 * ============================================================================
 * 5. Physical Circle-of-Confusion (CoC) Depth of Field Evaluator in Q16
 * ============================================================================
 */
int64_t flow_cine_calc_coc_q16(int64_t depth_q16,
                               int64_t focal_mm_q16,
                               int64_t fstop_q16,
                               int64_t focus_dist_q16) {
    if (depth_q16 <= 0LL || focal_mm_q16 <= 0LL || fstop_q16 <= 0LL || focus_dist_q16 <= 0LL)
        return 0LL;

    int64_t diff = depth_q16 - focus_dist_q16;
    if (diff < 0LL)
        diff = -diff;
    if (diff == 0LL)
        return 0LL;

    /* Normalized focal ratio (focal_mm / 35mm)^2 / fstop */
    int64_t norm_f_q16 = flow_cine_q16_div(focal_mm_q16, 35LL << FLOW_Q16_SHIFT);
    int64_t f_sq_q16 = flow_cine_q16_mul(norm_f_q16, norm_f_q16);
    int64_t lens_factor_q16 = flow_cine_q16_div(f_sq_q16, fstop_q16);

    int64_t denom_q16 = flow_cine_q16_mul(depth_q16, focus_dist_q16);
    if (denom_q16 < (FLOW_Q16_ONE >> 2))
        denom_q16 = (FLOW_Q16_ONE >> 2);

    int64_t blur_ratio_q16 = flow_cine_q16_div(diff, denom_q16);
    return flow_cine_q16_mul(blur_ratio_q16, lens_factor_q16);
}

/*
 * ============================================================================
 * 6. Main Per-Frame Cinematography Brain Step
 * ============================================================================
 */
int flow_cine_brain_step(FlowCineBrainState *brain,
                         const FlowJet2 craft_traj[3],
                         const FlowJet2 craft_rot[3],
                         uint64_t thermal_kelvin,
                         bool collision_hit,
                         int64_t dt_q16) {
    if (!brain || !craft_traj || !craft_rot || dt_q16 <= 0LL)
        return -1;

    /* 1. Advance Virtual Camera transition blend if active */
    const FlowCineRig64 *rig_from = &brain->blend_from_rig;
    const FlowCineRig64 *rig_to   = &brain->vcams[brain->active_vcam];

    if (brain->is_blending) {
        brain->blend_elapsed_q16 = flow_q16_clamp_i128(
            (flow_cine_i128)brain->blend_elapsed_q16 + (flow_cine_i128)dt_q16
        );
        if (brain->blend_elapsed_q16 >= brain->blend_duration_q16) {
            brain->blend_elapsed_q16 = brain->blend_duration_q16;
            brain->blend_weight_q16 = FLOW_Q16_ONE;
            brain->is_blending = false;
        } else {
            int64_t t_norm_q16 = flow_cine_q16_div(
                brain->blend_elapsed_q16, brain->blend_duration_q16
            );
            brain->blend_weight_q16 = flow_cine_ease_q16(t_norm_q16, brain->blend_curve);
        }
    } else {
        brain->blend_weight_q16 = FLOW_Q16_ONE;
    }

    int64_t t_blend_q16 = brain->is_blending
        ? flow_cine_q16_div(brain->blend_elapsed_q16, brain->blend_duration_q16)
        : FLOW_Q16_ONE;

    /* 2. Blend target optical & spatial parameters in FlowJet2 space */
    FlowJet2 target_focal = flow_cine_blend_jet2(
        flow_jet2_make(rig_from->focal_length_q16, 0LL, 0LL),
        flow_jet2_make(rig_to->focal_length_q16, 0LL, 0LL),
        t_blend_q16, brain->blend_curve
    );
    FlowJet2 target_aperture = flow_cine_blend_jet2(
        flow_jet2_make(rig_from->aperture_fstop_q16, 0LL, 0LL),
        flow_jet2_make(rig_to->aperture_fstop_q16, 0LL, 0LL),
        t_blend_q16, brain->blend_curve
    );
    FlowJet2 target_focus = flow_cine_blend_jet2(
        flow_jet2_make(rig_from->focus_dist_q16, 0LL, 0LL),
        flow_jet2_make(rig_to->focus_dist_q16, 0LL, 0LL),
        t_blend_q16, brain->blend_curve
    );

    /* Acceleration FOV breathing on Chase Dynamic camera (widens focal length under boost) */
    int64_t vz_q16 = craft_traj[2].d1;
    int64_t az_q16 = craft_traj[2].d2;
    if (brain->active_vcam == FLOW_VCAM_CHASE_DYNAMIC) {
        int64_t speed_excess = vz_q16 - (500LL << FLOW_Q16_SHIFT);
        if (speed_excess > 0LL) {
            /* Up to -4mm wider focal length under high-speed boost */
            int64_t fov_breath = flow_cine_clamp_i64(speed_excess / 180LL, 0LL, 4LL << FLOW_Q16_SHIFT);
            target_focal.v -= fov_breath;
        }
    }

    /* 3. Evaluate Dead-Zone / Soft-Zone Composer Hysteresis */
    int32_t err_x_q16 = flow_cine_clamp_i32(craft_rot[1].v / 4LL + craft_traj[0].d1 / 64LL);
    int32_t err_y_q16 = flow_cine_clamp_i32(craft_rot[0].v / 4LL + craft_traj[1].d1 / 64LL);
    int32_t corr_x_q16 = 0;
    int32_t corr_y_q16 = 0;
    uint32_t comp_mask = flow_cine_eval_composer_hysteresis(
        brain, err_x_q16, err_y_q16, &corr_x_q16, &corr_y_q16
    );

    /* G-Force Pilot Head Lag in Cockpit Tactical View */
    int64_t g_lag_x_q16 = 0LL;
    int64_t g_lag_y_q16 = 0LL;
    if (brain->active_vcam == FLOW_VCAM_COCKPIT_TACTICAL) {
        g_lag_x_q16 = flow_cine_clamp_i64(-craft_rot[1].d1 / 8LL, -(6554LL), 6554LL);
        g_lag_y_q16 = flow_cine_clamp_i64(-craft_rot[0].d1 / 8LL - az_q16 / 2048LL, -(6554LL), 6554LL);
    }

    /* 4. Step 2-Jet Damping Follow via flow_jet2_ema_step & Taylor Self-Check */
    int64_t alpha_q16 = flow_cine_clamp_i64(rig_to->follow_damping_q16, 1024LL, FLOW_Q16_ONE);

    for (size_t k = 0; k < 3U; ++k) {
        brain->prev_cam_offset_jet[k] = brain->cam_offset_jet[k];

        FlowJet2 from_off = flow_jet2_make(rig_from->local_offset_q16[k], 0LL, 0LL);
        FlowJet2 to_off   = flow_jet2_make(rig_to->local_offset_q16[k], 0LL, 0LL);
        FlowJet2 target_off = flow_cine_blend_jet2(from_off, to_off, t_blend_q16, brain->blend_curve);

        brain->cam_offset_jet[k] = flow_cine_step_damped_jet2(
            brain->cam_offset_jet[k], target_off, alpha_q16, dt_q16
        );

        /* Verify differential 2-Jet continuity via hardware trapezoidal Taylor self-check */
        FlowJet2 taylor_step = brain->prev_cam_offset_jet[k];
        taylor_step.v = flow_jet2_eval_taylor(brain->prev_cam_offset_jet[k], dt_q16);
        taylor_step.d1 = flow_q16_clamp_i128(
            (flow_cine_i128)brain->prev_cam_offset_jet[k].d1 +
            ((((flow_cine_i128)brain->prev_cam_offset_jet[k].d2 + (flow_cine_i128)taylor_step.d2) *
              (flow_cine_i128)dt_q16) >> (FLOW_Q16_SHIFT + 1U))
        );
        brain->hardware_sieve = flow_asm_jet2_self_check_step(
            brain->prev_cam_offset_jet[k], taylor_step,
            dt_q16, 64LL, 64LL, &brain->hardware_sieve
        );
    }

    int64_t target_look_x = (int64_t)rig_to->look_offset_xy_q16[0] + (int64_t)corr_x_q16 + g_lag_x_q16;
    int64_t target_look_y = (int64_t)rig_to->look_offset_xy_q16[1] + (int64_t)corr_y_q16 + g_lag_y_q16;
    FlowJet2 from_lx = flow_jet2_make(rig_from->look_offset_xy_q16[0], 0LL, 0LL);
    FlowJet2 from_ly = flow_jet2_make(rig_from->look_offset_xy_q16[1], 0LL, 0LL);
    FlowJet2 blended_lx = flow_cine_blend_jet2(
        from_lx, flow_jet2_make(target_look_x, 0LL, 0LL), t_blend_q16, brain->blend_curve
    );
    FlowJet2 blended_ly = flow_cine_blend_jet2(
        from_ly, flow_jet2_make(target_look_y, 0LL, 0LL), t_blend_q16, brain->blend_curve
    );

    brain->look_offset_jet[0] = flow_cine_step_damped_jet2(brain->look_offset_jet[0], blended_lx, alpha_q16, dt_q16);
    brain->look_offset_jet[1] = flow_cine_step_damped_jet2(brain->look_offset_jet[1], blended_ly, alpha_q16, dt_q16);

    brain->focal_jet      = flow_cine_step_damped_jet2(brain->focal_jet, target_focal, alpha_q16, dt_q16);
    brain->aperture_jet   = flow_cine_step_damped_jet2(brain->aperture_jet, target_aperture, alpha_q16, dt_q16);
    brain->focus_dist_jet = flow_cine_step_damped_jet2(brain->focus_dist_jet, target_focus, alpha_q16, dt_q16);

    /* 5. EV Auto-Exposure Adaptation via flow_jet2_ema_step */
    int64_t target_ev_gain_q16 = FLOW_Q16_ONE;
    if (thermal_kelvin > 750ULL) {
        /* Stop down exposure slightly when 800K incandescent cavity blazes to preserve highlight detail */
        int64_t kelvin_excess = (int64_t)(thermal_kelvin - 750ULL);
        int64_t reduction = flow_cine_clamp_i64(kelvin_excess * 140LL, 0LL, 14000LL);
        target_ev_gain_q16 = FLOW_Q16_ONE - reduction;
    } else if (brain->active_vcam == FLOW_VCAM_ORBIT_TELEPHOTO) {
        target_ev_gain_q16 = FLOW_Q16_ONE + 3277LL; /* +0.05x luminous boost on telephoto vista */
    }
    brain->exposure_ev_jet = flow_cine_step_damped_jet2(
        brain->exposure_ev_jet,
        flow_jet2_make(target_ev_gain_q16, 0LL, 0LL),
        8192LL, /* Smooth eye adaptation alpha = 0.125 */
        dt_q16
    );

    /* 6. 6D Camera Shake / Trauma Impulse Envelope */
    if (collision_hit) {
        flow_cine_add_trauma(brain, 32768LL); /* +0.50 trauma on worldline collision */
    }
    if (thermal_kelvin >= 800ULL) {
        flow_cine_add_trauma(brain, 8192LL);  /* Thermal meltdown rumble */
    } else if (brain->active_vcam == FLOW_VCAM_FLYBY_DOLLY) {
        /* Subtle continuous flyby turbulence near the molten cavity */
        if (brain->trauma_q16 < 16384LL)
            brain->trauma_q16 = 16384LL;
    }

    /* Quadratic trauma envelope: shake = trauma^2 * shake_gain */
    int64_t trauma_sq_q16 = flow_cine_q16_mul(brain->trauma_q16, brain->trauma_q16);
    int64_t shake_amp_q16 = flow_cine_q16_mul(trauma_sq_q16, (int64_t)rig_to->shake_gain_q16);

    brain->shake_tick++;
    uint64_t tk = brain->shake_tick;
    int64_t wx = flow_cine_tri_wave_q16(tk * 11403ULL + 0x1357ULL);
    int64_t wy = flow_cine_tri_wave_q16(tk * 17909ULL + 0x2468ULL);
    int64_t wz = flow_cine_tri_wave_q16(tk * 23173ULL + 0x9ABCULL);

    /* Max translational shake ~0.04m, rotational shake ~0.015 screen units */
    brain->shake_pos_q16[0] = flow_cine_clamp_i32(flow_cine_q16_mul(wx, shake_amp_q16) >> 5);
    brain->shake_pos_q16[1] = flow_cine_clamp_i32(flow_cine_q16_mul(wy, shake_amp_q16) >> 5);
    brain->shake_pos_q16[2] = flow_cine_clamp_i32(flow_cine_q16_mul(wz, shake_amp_q16) >> 6);
    brain->shake_rot_q16[0] = flow_cine_clamp_i32(flow_cine_q16_mul(wy, shake_amp_q16) >> 6);
    brain->shake_rot_q16[1] = flow_cine_clamp_i32(flow_cine_q16_mul(wx, shake_amp_q16) >> 6);

    /* Decay trauma proportionally to dt_q16 (minimum 1 quantum so substeps never stall) */
    int64_t decay_q16 = flow_cine_q16_mul(dt_q16, 4096LL);
    if (decay_q16 < 1LL)
        decay_q16 = 1LL;
    brain->trauma_q16 = flow_cine_clamp_i64(brain->trauma_q16 - decay_q16, 0LL, FLOW_Q16_ONE);

    /* 7. Populate 64-Byte Resolved Frame Snapshot */
    brain->resolved.active_vcam_id     = brain->active_vcam;
    brain->resolved.active_flags       = (brain->is_blending && brain->blend_weight_q16 < (FLOW_Q16_ONE >> 1))
                                       ? rig_from->flags
                                       : rig_to->flags;
    brain->resolved.focal_length_q16   = flow_cine_clamp_i32(brain->focal_jet.v);
    brain->resolved.aperture_fstop_q16 = flow_cine_clamp_i32(brain->aperture_jet.v);
    brain->resolved.focus_dist_q16     = flow_cine_clamp_i32(brain->focus_dist_jet.v);
    brain->resolved.exposure_gain_q16  = flow_cine_clamp_i32(brain->exposure_ev_jet.v);

    for (size_t k = 0; k < 3U; ++k) {
        brain->resolved.cam_offset_q16[k] = flow_cine_clamp_i32(
            brain->cam_offset_jet[k].v + (int64_t)brain->shake_pos_q16[k]
        );
    }
    brain->resolved.look_offset_xy_q16[0] = flow_cine_clamp_i32(
        brain->look_offset_jet[0].v + (int64_t)brain->shake_rot_q16[0]
    );
    brain->resolved.look_offset_xy_q16[1] = flow_cine_clamp_i32(
        brain->look_offset_jet[1].v + (int64_t)brain->shake_rot_q16[1]
    );

    /* 180-degree shutter velocity vector combining craft velocity & VCam transition/damping velocity */
    brain->resolved.shutter_vel_q16[0] = flow_cine_clamp_i32(
        craft_traj[0].d1 + brain->cam_offset_jet[0].d1 * 8LL + brain->look_offset_jet[0].d1 * 16LL
    );
    brain->resolved.shutter_vel_q16[1] = flow_cine_clamp_i32(
        craft_traj[1].d1 + brain->cam_offset_jet[1].d1 * 8LL + brain->look_offset_jet[1].d1 * 16LL
    );
    brain->resolved.shutter_vel_q16[2] = flow_cine_clamp_i32(
        craft_traj[2].d1 + brain->cam_offset_jet[2].d1 * 8LL
    );
    brain->resolved.blend_weight_q16 = flow_cine_clamp_i32(brain->blend_weight_q16);
    brain->resolved.composer_mask    = comp_mask;

    return 0;
}

/*
 * ============================================================================
 * 8. FlowCinemaShot64: Frame-Accurate Cinema EDL & 48kHz Audio Lockstep Engine
 * ============================================================================
 */
static uint64_t flow_cine_hash_shot64(const FlowCinemaShot64 *s) {
    const uint8_t *bytes = (const uint8_t *)s;
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0U; i < 56U; ++i) {
        h ^= (uint64_t)bytes[i];
        h *= 0x100000001B3ULL;
    }
    return h ^ 0x43494E454D413634ULL; /* "CINEMA64" */
}

int flow_cine_build_default_reel64(FlowCinemaShot64 out_shots[FLOW_CINEMA_REEL_SHOT_COUNT],
                                   uint32_t fps_rate) {
    if (!out_shots)
        return -1;
    if (fps_rate == 0U)
        fps_rate = 24U;

    uint32_t frames_per_shot = fps_rate * 2U; /* 2.0 seconds per shot -> 8.0s 4-Act Reel */
    uint32_t samples_per_frame = FLOW_CINEMA_AUDIO_RATE_HZ / fps_rate;

    memset(out_shots, 0, sizeof(FlowCinemaShot64) * FLOW_CINEMA_REEL_SHOT_COUNT);

    /* Shot 1: Act I - Relativistic Space Citadel & 800K Cavity (135mm Telephoto) */
    out_shots[0].shot_id                 = 1U;
    out_shots[0].stage_mode              = 0U;
    out_shots[0].vcam_id                 = FLOW_VCAM_ORBIT_TELEPHOTO;
    out_shots[0].fps_rate                = fps_rate;
    out_shots[0].start_frame             = 0U;
    out_shots[0].frame_count             = frames_per_shot;
    out_shots[0].audio_samples_per_frame = samples_per_frame;
    out_shots[0].transition_style        = 1U; /* Iris Aperture Wipe */
    out_shots[0].dolly_vel_xyz_q16[0]    = 512;
    out_shots[0].dolly_vel_xyz_q16[1]    = -256;
    out_shots[0].dolly_vel_xyz_q16[2]    = 1024;
    out_shots[0].xray_sweep_q16          = 0;
    out_shots[0].audio_base_freq_hz      = 110U; /* A2 Deep Space Drone */
    out_shots[0].colorspace_flags        = FLOW_CINE_FLAG_LETTERBOX_239 | FLOW_CINE_FLAG_ANAMORPHIC_FLARE;
    out_shots[0].edl_signature_hash      = flow_cine_hash_shot64(&out_shots[0]);

    /* Shot 2: Act II - Autumn Train Window Condensation & 2-Jet Rivulet Refraction (35mm Dolly) */
    out_shots[1].shot_id                 = 2U;
    out_shots[1].stage_mode              = 1U;
    out_shots[1].vcam_id                 = FLOW_VCAM_FLYBY_DOLLY;
    out_shots[1].fps_rate                = fps_rate;
    out_shots[1].start_frame             = frames_per_shot;
    out_shots[1].frame_count             = frames_per_shot;
    out_shots[1].audio_samples_per_frame = samples_per_frame;
    out_shots[1].transition_style        = 2U; /* Horizon Curtain Wipe */
    out_shots[1].dolly_vel_xyz_q16[0]    = 768;
    out_shots[1].dolly_vel_xyz_q16[1]    = 192;
    out_shots[1].dolly_vel_xyz_q16[2]    = 0;
    out_shots[1].xray_sweep_q16          = 0;
    out_shots[1].audio_base_freq_hz      = 165U; /* E3 Warm Train Harmonic */
    out_shots[1].colorspace_flags        = FLOW_CINE_FLAG_DOF_BOKEH;
    out_shots[1].edl_signature_hash      = flow_cine_hash_shot64(&out_shots[1]);

    /* Shot 3: Act III - Infinite 3D Point-Cloud Monoliths & Glitter Wave (24mm Chase) */
    out_shots[2].shot_id                 = 3U;
    out_shots[2].stage_mode              = 2U;
    out_shots[2].vcam_id                 = FLOW_VCAM_CHASE_DYNAMIC;
    out_shots[2].fps_rate                = fps_rate;
    out_shots[2].start_frame             = frames_per_shot * 2U;
    out_shots[2].frame_count             = frames_per_shot;
    out_shots[2].audio_samples_per_frame = samples_per_frame;
    out_shots[2].transition_style        = 3U; /* Diagonal Blade Wipe */
    out_shots[2].dolly_vel_xyz_q16[0]    = -640;
    out_shots[2].dolly_vel_xyz_q16[1]    = -320;
    out_shots[2].dolly_vel_xyz_q16[2]    = 2048;
    out_shots[2].xray_sweep_q16          = 0;
    out_shots[2].audio_base_freq_hz      = 147U; /* D3 Singularity Pulse */
    out_shots[2].colorspace_flags        = FLOW_CINE_FLAG_ANAMORPHIC_FLARE;
    out_shots[2].edl_signature_hash      = flow_cine_hash_shot64(&out_shots[2]);

    /* Shot 4: Act IV - Winter Shore Isometric CSG Cutaway Diorama + Live X-Ray Sweep */
    out_shots[3].shot_id                 = 4U;
    out_shots[3].stage_mode              = 3U;
    out_shots[3].vcam_id                 = FLOW_VCAM_ORBIT_TELEPHOTO;
    out_shots[3].fps_rate                = fps_rate;
    out_shots[3].start_frame             = frames_per_shot * 3U;
    out_shots[3].frame_count             = frames_per_shot;
    out_shots[3].audio_samples_per_frame = samples_per_frame;
    out_shots[3].transition_style        = 0U;
    out_shots[3].dolly_vel_xyz_q16[0]    = 896;
    out_shots[3].dolly_vel_xyz_q16[1]    = -384;
    out_shots[3].dolly_vel_xyz_q16[2]    = 512;
    out_shots[3].xray_sweep_q16          = 22528; /* Sweep cutaway slice across the diorama! */
    out_shots[3].audio_base_freq_hz      = 220U;  /* A3 Nordic Crystalline Chord */
    out_shots[3].colorspace_flags        = 0U;
    out_shots[3].edl_signature_hash      = flow_cine_hash_shot64(&out_shots[3]);

    return 0;
}

static void flow_cine_write_2digits(char *dst, uint32_t val) {
    uint32_t v = val % 100U;
    dst[0] = (char)('0' + (v / 10U));
    dst[1] = (char)('0' + (v % 10U));
}

int flow_cine_format_smpte_timecode(uint32_t frame_idx,
                                    uint32_t fps_rate,
                                    char out_tc[12]) {
    if (!out_tc)
        return -1;
    if (fps_rate == 0U)
        fps_rate = 24U;

    uint32_t ff = frame_idx % fps_rate;
    uint32_t total_sec = frame_idx / fps_rate;
    uint32_t ss = total_sec % 60U;
    uint32_t total_min = total_sec / 60U;
    uint32_t mm = total_min % 60U;
    uint32_t hh = (total_min / 60U) % 24U;

    flow_cine_write_2digits(&out_tc[0], hh);
    out_tc[2] = ':';
    flow_cine_write_2digits(&out_tc[3], mm);
    out_tc[5] = ':';
    flow_cine_write_2digits(&out_tc[6], ss);
    out_tc[8] = ':';
    flow_cine_write_2digits(&out_tc[9], ff);
    out_tc[11] = '\0';
    return 0;
}

int flow_cine_synth_lockstep_audio48k(const FlowCinemaShot64 *shot,
                                      uint32_t shot_local_frame,
                                      int16_t *out_pcm_stereo,
                                      uint32_t sample_frames) {
    if (!shot || !out_pcm_stereo)
        return -1;

    uint32_t base_hz = (shot->audio_base_freq_hz > 40U) ? shot->audio_base_freq_hz : 110U;
    uint32_t fifth_hz = (base_hz * 3U) / 2U;
    uint32_t octave_hz = base_hz * 2U;

    uint64_t global_sample_start = (uint64_t)(shot->start_frame + shot_local_frame)
                                 * (uint64_t)shot->audio_samples_per_frame;
    /* Smooth fade-in on first 4 frames and fade-out on last 4 frames of each shot */
    int64_t env_q16 = FLOW_Q16_ONE;
    if (shot_local_frame < 4U) {
        env_q16 = (int64_t)(shot_local_frame + 1U) * (FLOW_Q16_ONE / 4LL);
    } else if (shot->frame_count > 4U && shot_local_frame >= shot->frame_count - 4U) {
        uint32_t rem = shot->frame_count - shot_local_frame;
        env_q16 = (int64_t)rem * (FLOW_Q16_ONE / 4LL);
    }

    for (uint32_t i = 0U; i < sample_frames; ++i) {
        uint64_t s_idx = global_sample_start + (uint64_t)i;
        uint64_t ph1 = (s_idx * (uint64_t)base_hz   * 65536ULL) / (uint64_t)FLOW_CINEMA_AUDIO_RATE_HZ;
        uint64_t ph2 = (s_idx * (uint64_t)fifth_hz  * 65536ULL) / (uint64_t)FLOW_CINEMA_AUDIO_RATE_HZ;
        uint64_t ph3 = (s_idx * (uint64_t)octave_hz * 65536ULL) / (uint64_t)FLOW_CINEMA_AUDIO_RATE_HZ;

        int64_t w1 = flow_cine_tri_wave_q16(ph1);
        int64_t w2 = flow_cine_tri_wave_q16(ph2 + 16384ULL);
        int64_t w3 = flow_cine_tri_wave_q16(ph3);

        int64_t mix_l = (w1 * 3LL + w2 * 2LL + w3) / 6LL;
        int64_t mix_r = (w1 * 3LL - w2 + w3 * 2LL) / 6LL;

        int64_t pcm_l = (mix_l * env_q16 * 7200LL) >> (2U * FLOW_Q16_SHIFT);
        int64_t pcm_r = (mix_r * env_q16 * 7200LL) >> (2U * FLOW_Q16_SHIFT);

        out_pcm_stereo[i * 2U + 0U] = (int16_t)flow_cine_clamp_i64(pcm_l, -32000LL, 32000LL);
        out_pcm_stereo[i * 2U + 1U] = (int16_t)flow_cine_clamp_i64(pcm_r, -32000LL, 32000LL);
    }
    return 0;
}
