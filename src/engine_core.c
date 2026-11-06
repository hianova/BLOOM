#include "flow_game.h"
#include "flow_lean_abi.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sched.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/*
 * ============================================================================
 * BLOOM: Engine Core Implementation (C17, Pure Fixed-Point Q48.16)
 * ============================================================================
 * Strict Zero Float / Double Guarantee:
 * Every variable, accumulator, trajectory, metric, and state vector is
 * purely integer: int64_t (Q48.16), uint64_t, __int128, int32_t, uint32_t.
 * ============================================================================
 */

/* Helper inline arithmetic functions for Q48.16 fixed point */
__extension__ typedef __int128 flow_i128;

static inline int64_t flow_game_q16_mul(int64_t a, int64_t b) {
    flow_i128 p = ((flow_i128)a * (flow_i128)b) >> FLOW_Q16_SHIFT;
    return flow_q16_clamp_i128(p);
}

static inline int64_t flow_game_q16_div(int64_t a, int64_t b) {
    if (b == 0LL)
        return 0LL;
    flow_i128 num = ((flow_i128)a) << FLOW_Q16_SHIFT;
    return flow_q16_clamp_i128(num / (flow_i128)b);
}

static inline int64_t flow_game_q16_sub(int64_t a, int64_t b) {
    return flow_q16_clamp_i128((flow_i128)a - (flow_i128)b);
}

static inline int64_t flow_game_q16_add(int64_t a, int64_t b) {
    return flow_q16_clamp_i128((flow_i128)a + (flow_i128)b);
}
static const int32_t s_local_box_A[6][3] = {
    { 1,  0,  0},  /*  x <= +B */
    {-1,  0,  0},  /* -x <= +B */
    { 0,  1,  0},  /*  y <= +B */
    { 0, -1,  0},  /* -y <= +B */
    { 0,  0,  1},  /*  z <= +B */
    { 0,  0, -1}   /* -z <= +B */
};

/* Sector boundary bounds: 50,000 units */
static const int64_t s_local_box_b[6] = {
    50000LL << FLOW_Q16_SHIFT,
    50000LL << FLOW_Q16_SHIFT,
    50000LL << FLOW_Q16_SHIFT,
    50000LL << FLOW_Q16_SHIFT,
    50000LL << FLOW_Q16_SHIFT,
    50000LL << FLOW_Q16_SHIFT
};

int flow_game_init(FlowGameEngineState *engine) {
    if (!engine)
        return -1;

    memset(engine, 0, sizeof(*engine));
    engine->hardware_sieve = FLOW_ASM_SIEVE_ALL_SOUND;

    /* Initialize craft worldline trajectory (start at z = -20000, moving forward at v_z = +500) */
    engine->craft_traj[0] = flow_jet2_make(0LL, 0LL, 0LL);
    engine->craft_traj[1] = flow_jet2_make(0LL, 0LL, 0LL);
    engine->craft_traj[2] = flow_jet2_make(-(20000LL << FLOW_Q16_SHIFT), 500LL << FLOW_Q16_SHIFT, 0LL);

    engine->prev_craft_traj[0] = engine->craft_traj[0];
    engine->prev_craft_traj[1] = engine->craft_traj[1];
    engine->prev_craft_traj[2] = engine->craft_traj[2];

    /* Craft orientation: pitch, yaw, roll as 2-Jets */
    engine->craft_rot[0] = flow_jet2_make(0LL, 0LL, 0LL);
    engine->craft_rot[1] = flow_jet2_make(0LL, 0LL, 0LL);
    engine->craft_rot[2] = flow_jet2_make(0LL, 0LL, 0LL);
    engine->craft_throttle_q16 = FLOW_Q16_ONE;

    /* Celestial obstacle: planet center at (0, 0, 5000), radius 2000 */
    engine->celestial_pos_q16[0] = 0LL;
    engine->celestial_pos_q16[1] = 0LL;
    engine->celestial_pos_q16[2] = 5000LL << FLOW_Q16_SHIFT;
    engine->celestial_radius_q16 = 2000LL << FLOW_Q16_SHIFT;

    /* Concentric SDF Level-Set Padding (+-delta in Q48.16) */
    engine->delta_hurt_q16  = FLOW_DEFAULT_DELTA_HURT_Q16;
    engine->delta_graze_q16 = FLOW_DEFAULT_DELTA_GRAZE_Q16;
    engine->delta_hit_q16   = FLOW_DEFAULT_DELTA_HIT_Q16;

    /*
     * Contact Skin Dual-Threshold Hysteresis Polyhedron:
     * Plane 0 normal = (-1), testing x[0] = sdf_dist_q16:
     *   dot = -sdf_dist_q16 > -delta_enter  <=>  sdf_dist_q16 < delta_enter (latches contact)
     *   dot = -sdf_dist_q16 > -delta_exit   <=>  sdf_dist_q16 < delta_exit  (holds contact across deadband)
     */
    FlowContactSkinPoly_init(&engine->contact_skin_poly);
    engine->contact_skin_poly.A[0][0] = -1;
    FlowContactSkinPoly_set_hysteresis(
        &engine->contact_skin_poly, 0U,
        -FLOW_DEFAULT_SKIN_ENTER_Q16,
        -FLOW_DEFAULT_SKIN_EXIT_Q16
    );
    engine->contact_skin_mask = 0ULL;
    engine->contact_latched = 0U;
    engine->prev_hit_zone = FLOW_HIT_NONE;
    engine->graze_count = 0ULL;

    /* Time-Dimension Action Feel: Hitstop & ~100ms Input Buffer Window */
    engine->hitstop_frames_remaining = 0U;
    engine->hitstop_scale_q16 = FLOW_Q16_ONE;
    engine->last_effective_dt_q16 = FLOW_Q16_ONE;
    engine->input_buffer_timer_q16 = 0LL;
    engine->input_buffer_frames = 0U;
    engine->input_buffered = false;

    /*
     * Stage handoff polyhedron: FlowStagePoly (DIM=3, SHIFT=16, PLANES=16)
     * Plane 0 normal = (0, 0, 1), testing craft distance along z.
     * Dual-threshold hysteresis:
     *   b_enter = 10000 units (crosses into Layer 1 Celestial Dome)
     *   b_exit  =  8000 units (drops back into Layer 0 Micro SDF)
     */
    FlowStagePoly_init(&engine->stage_poly);
    engine->stage_poly.A[0][0] = 0;
    engine->stage_poly.A[0][1] = 0;
    engine->stage_poly.A[0][2] = 1;

    int64_t b_enter = 10000LL;
    int64_t b_exit  =  8000LL;
    FlowStagePoly_set_hysteresis(&engine->stage_poly, 0U, b_enter, b_exit);
    engine->stage_mask = 0ULL;
    engine->current_layer = 0U;

    /* Initialize Ricardian v4 economy state (64-byte univalent) */
    engine->ricardian.reserve_a = 1000000ULL;
    engine->ricardian.reserve_b = 1000000ULL;
    engine->ricardian.m0_vault = 500000ULL;
    engine->ricardian.iou_supply = 250000ULL;
    engine->ricardian.harberger_pool = 10000ULL;
    engine->ricardian.burned_supply = 5000ULL;
    engine->ricardian.assembly_depth = 4ULL;
    engine->ricardian.thermal_kelvin = 740ULL;
    engine->thermal_shutdown = 0U;
    engine->ricardian_epoch = 1ULL;

    /* Initialize SPSC Packet Ring */
    FlowGamePacketRing_init(&engine->ring);

    /* Initialize Lock-Free QSBR Context */
    engine->qsbr_ctx = flow_reload_create(NULL);
    if (!engine->qsbr_ctx)
        return -2;
    if (flow_reload_reader_register(engine->qsbr_ctx, &engine->qsbr_reader) != 0) {
        flow_reload_destroy(engine->qsbr_ctx);
        engine->qsbr_ctx = NULL;
        return -3;
    }
    if (flow_qsbr_publish_ptr(engine->qsbr_ctx, &engine->stage_poly) != 0) {
        flow_reload_reader_unregister(&engine->qsbr_reader);
        flow_reload_destroy(engine->qsbr_ctx);
        engine->qsbr_ctx = NULL;
        return -4;
    }

    /* Initialize FlowCineRig64 Cinematography Brain & Physical Camera Optics */
    flow_cine_brain_init(&engine->cine_brain);

    /* Initialize 120 FPS (8.33ms) Dual-Threshold Frame-Time Hysteresis Governor */
    flow_fps_governor_init(engine);

    /* Initial Wave64 viewport tile entropy configuration */
    flow_game_cull_tiles_wave64(engine);

    return 0;
}

void flow_game_destroy(FlowGameEngineState *engine) {
    if (!engine)
        return;
    if (engine->qsbr_ctx) {
        flow_reload_reader_unregister(&engine->qsbr_reader);
        flow_reload_destroy(engine->qsbr_ctx);
        engine->qsbr_ctx = NULL;
    }
}

static void flow_game_apply_input_direct(FlowGameEngineState *engine,
                                         int64_t throttle_q16,
                                         int64_t pitch_rate_q16,
                                         int64_t yaw_rate_q16,
                                         int64_t roll_rate_q16,
                                         bool toggle_layer) {
    engine->craft_throttle_q16 = throttle_q16;
    int64_t target_vz = flow_game_q16_mul(throttle_q16, 1200LL << FLOW_Q16_SHIFT);
    int64_t v_max_subluminal = (FLOW_C_LIGHT_ENGINE_Q16 * FLOW_BETA_MAX_Q16) >> FLOW_Q16_SHIFT;
    if (target_vz > v_max_subluminal)
        target_vz = v_max_subluminal;
    if (target_vz < -v_max_subluminal)
        target_vz = -v_max_subluminal;
    engine->craft_traj[2].d2 = flow_game_q16_sub(target_vz, engine->craft_traj[2].d1);

    engine->craft_rot[0].d1 = pitch_rate_q16;
    engine->craft_rot[1].d1 = yaw_rate_q16;
    engine->craft_rot[2].d1 = roll_rate_q16;

    if (toggle_layer) {
        uint64_t next_mask = 0ULL;
        FlowKanStatus kan = flow_cubical_step_neighbor(engine->stage_mask, 0U, 0ULL, &next_mask);
        if (kan == FLOW_KAN_FILLED_HOMOTOPIC) {
            engine->stage_mask = next_mask;
            engine->current_layer = (next_mask & 1ULL) ? 1U : 0U;
            flow_qsbr_publish_ptr(engine->qsbr_ctx, &engine->stage_poly);
        }
    }
}

void flow_game_input(FlowGameEngineState *engine,
                     int64_t throttle_q16,
                     int64_t pitch_rate_q16,
                     int64_t yaw_rate_q16,
                     int64_t roll_rate_q16,
                     bool toggle_layer) {
    if (!engine)
        return;

    /*
     * If engine is in Hitstop time-dilation or 800K Thermal Shutdown, latch the
     * player's action into the ~100ms Input Buffer Window so early inputs are never dropped.
     */
    if (engine->thermal_shutdown || engine->hitstop_frames_remaining > 0U) {
        engine->input_buffered = true;
        engine->input_buffer_timer_q16 = FLOW_INPUT_BUFFER_WINDOW_Q16;
        engine->input_buffer_frames = FLOW_INPUT_BUFFER_FRAMES;
        engine->buffered_throttle_q16 = throttle_q16;
        engine->buffered_pitch_rate_q16 = pitch_rate_q16;
        engine->buffered_yaw_rate_q16 = yaw_rate_q16;
        engine->buffered_roll_rate_q16 = roll_rate_q16;
        engine->buffered_toggle_layer = toggle_layer;

        if (engine->thermal_shutdown) {
            /* Thermal shutdown protects craft engine: disables throttle when overheated (>800K) */
            engine->craft_throttle_q16 = 0LL;
            engine->craft_traj[2].d1 = 100LL << FLOW_Q16_SHIFT; /* Inertial coast */
            engine->craft_traj[2].d2 = 0LL;
            engine->craft_rot[0].d1 = pitch_rate_q16;
            engine->craft_rot[1].d1 = yaw_rate_q16;
            engine->craft_rot[2].d1 = roll_rate_q16;
        }
        return;
    }

    flow_game_apply_input_direct(
        engine, throttle_q16, pitch_rate_q16, yaw_rate_q16, roll_rate_q16, toggle_layer
    );
}

/*
 * Subsystem 1: Relativistic Worldline Anti-Tunneling Collision
 *
 * Formulates the celestial sphere SDF intersection:
 *   ||p(t) - c||^2 - R^2 = 0
 * as a single scalar FlowJet2 quadratic polynomial and solves for the exact
 * smallest positive root t_hit_q16 in (0, dt_q16] via flow_jet2_solve_root.
 * Also performs linear boundary lookahead via flow_qpoly_predict_next_face.
 */
static bool solve_sphere_shell_jet2(FlowJet2 dist_sq,
                                    int64_t r_eff_q16,
                                    int64_t dt_q16,
                                    int64_t *out_t_q16) {
    if (r_eff_q16 <= 0LL)
        r_eff_q16 = 1LL;

    int64_t r_sq = flow_game_q16_mul(r_eff_q16, r_eff_q16);
    FlowJet2 r2_jet = flow_jet2_make(r_sq, 0LL, 0LL);
    FlowJet2 sdf_poly = flow_jet2_sub(dist_sq, r2_jet);

    if (sdf_poly.v <= 0LL) {
        if (out_t_q16)
            *out_t_q16 = 0LL;
        return true;
    }

    int64_t t_root = 0LL;
    if (flow_jet2_solve_root(sdf_poly, dt_q16, &t_root) &&
        t_root > 0LL && t_root <= dt_q16) {
        if (out_t_q16)
            *out_t_q16 = t_root;
        return true;
    }

    /* Parabolic vertex check t_vert = -d1 / d2 in (0, dt_q16] for tangential kiss */
    if (sdf_poly.d2 > 0LL && sdf_poly.d1 < 0LL) {
        flow_i128 tv = (-(flow_i128)sdf_poly.d1 << FLOW_Q16_SHIFT) / (flow_i128)sdf_poly.d2;
        if (tv > 0 && tv <= (flow_i128)dt_q16) {
            int64_t tv_q16 = (int64_t)tv;
            if (flow_jet2_eval_taylor(sdf_poly, tv_q16) <= 0LL) {
                if (out_t_q16)
                    *out_t_q16 = tv_q16;
                return true;
            }
        }
    }

    return false;
}

bool flow_game_check_worldline_collision(const FlowJet2 traj[3],
                                         const int64_t center_q16[3],
                                         int64_t radius_q16,
                                         int64_t dt_q16,
                                         int64_t *out_t_hit_q16) {
    if (!traj || !center_q16 || dt_q16 <= 0)
        return false;

    /* Formulate dp_k(t) = traj[k](t) - center[k] as FlowJet2 polynomials */
    FlowJet2 dp[3];
    for (size_t k = 0; k < 3; ++k) {
        dp[k] = flow_jet2_make(traj[k].v - center_q16[k], traj[k].d1, traj[k].d2);
    }

    /* Compute ||dp(t)||^2 = dp_x^2 + dp_y^2 + dp_z^2 via pure Q16 2-Jet multiplication */
    FlowJet2 dp_x2 = flow_jet2_mul(dp[0], dp[0]);
    FlowJet2 dp_y2 = flow_jet2_mul(dp[1], dp[1]);
    FlowJet2 dp_z2 = flow_jet2_mul(dp[2], dp[2]);
    FlowJet2 dist_sq = flow_jet2_add(flow_jet2_add(dp_x2, dp_y2), dp_z2);

    if (solve_sphere_shell_jet2(dist_sq, radius_q16, dt_q16, out_t_hit_q16)) {
        return true;
    }

    /* Linear boundary lookahead check across local sector boundary planes */
    const int32_t x_pos[3] = {
        (int32_t)(traj[0].v >> FLOW_Q16_SHIFT),
        (int32_t)(traj[1].v >> FLOW_Q16_SHIFT),
        (int32_t)(traj[2].v >> FLOW_Q16_SHIFT)
    };
    const int32_t dx_vel[3] = {
        (int32_t)(traj[0].d1 >> FLOW_Q16_SHIFT),
        (int32_t)(traj[1].d1 >> FLOW_Q16_SHIFT),
        (int32_t)(traj[2].d1 >> FLOW_Q16_SHIFT)
    };
    const int32_t ddx_acc[3] = {
        (int32_t)(traj[0].d2 >> FLOW_Q16_SHIFT),
        (int32_t)(traj[1].d2 >> FLOW_Q16_SHIFT),
        (int32_t)(traj[2].d2 >> FLOW_Q16_SHIFT)
    };

    FlowQPolyForecast fc = flow_qpoly_predict_next_face(
        (const int32_t *)s_local_box_A, s_local_box_b,
        0x3FULL, 3U, x_pos, dx_vel, ddx_acc
    );
    (void)fc; /* Forecast available for navigation advisory */

    return false;
}

/*
 * Concentric SDF Level-Set Hitbox/Hurtbox Worldline Evaluation (+-delta):
 *   - f(x) <= -delta_hurt_q16                -> FLOW_HIT_LETHAL
 *   - -delta_hurt_q16 < f(x) <= +delta_graze -> FLOW_HIT_GRAZE
 *   - +delta_graze < f(x) <= +delta_hit_q16  -> FLOW_HIT_LOCKED
 *   - f(x) > +delta_hit_q16                  -> FLOW_HIT_NONE
 *   Projects contact point onto exact visual surface f(x) = 0:
 *     p_contact = p - f(p) * grad_f(p)
 */
FlowHitZone flow_game_eval_worldline_padded(const FlowJet2 traj[3],
                                            const int64_t center_q16[3],
                                            int64_t radius_q16,
                                            int64_t delta_hurt_q16,
                                            int64_t delta_graze_q16,
                                            int64_t delta_hit_q16,
                                            int64_t dt_q16,
                                            FlowContactManifold64 *out_manifold) {
    if (out_manifold) {
        memset(out_manifold, 0, sizeof(*out_manifold));
    }
    if (!traj || !center_q16 || radius_q16 <= 0LL || dt_q16 <= 0LL) {
        return FLOW_HIT_NONE;
    }

    int64_t dhurt  = (delta_hurt_q16 >= 0LL) ? delta_hurt_q16 : 0LL;
    int64_t dgraze = (delta_graze_q16 >= 0LL) ? delta_graze_q16 : 0LL;
    int64_t dhit   = (delta_hit_q16 >= dgraze) ? delta_hit_q16 : dgraze;

    FlowJet2 dp[3];
    for (size_t k = 0; k < 3U; ++k) {
        dp[k] = flow_jet2_make(traj[k].v - center_q16[k], traj[k].d1, traj[k].d2);
    }

    FlowJet2 dist_sq = flow_jet2_add(
        flow_jet2_add(flow_jet2_mul(dp[0], dp[0]), flow_jet2_mul(dp[1], dp[1])),
        flow_jet2_mul(dp[2], dp[2])
    );

    int64_t r_hurt  = (radius_q16 > dhurt) ? (radius_q16 - dhurt) : 1LL;
    int64_t r_graze = radius_q16 + dgraze;
    int64_t r_hit   = radius_q16 + dhit;

    int64_t t_hit = 0LL;
    FlowHitZone zone = FLOW_HIT_NONE;

    if (solve_sphere_shell_jet2(dist_sq, r_hurt, dt_q16, &t_hit)) {
        zone = FLOW_HIT_LETHAL;
    } else if (solve_sphere_shell_jet2(dist_sq, r_graze, dt_q16, &t_hit)) {
        zone = FLOW_HIT_GRAZE;
    } else if (solve_sphere_shell_jet2(dist_sq, r_hit, dt_q16, &t_hit)) {
        zone = FLOW_HIT_LOCKED;
    }

    /* Evaluate position at t_hit (or t=0 if FLOW_HIT_NONE) for exact SDF & surface snap */
    int64_t pos[3];
    int64_t rel[3];
    for (size_t k = 0; k < 3U; ++k) {
        pos[k] = flow_jet2_eval_taylor(traj[k], t_hit);
        rel[k] = pos[k] - center_q16[k];
    }

    __extension__ typedef unsigned __int128 u128;
    u128 r_sq_u128 = (u128)((flow_i128)rel[0] * (flow_i128)rel[0] +
                            (flow_i128)rel[1] * (flow_i128)rel[1] +
                            (flow_i128)rel[2] * (flow_i128)rel[2]);
    int64_t r_dist_q16 = (int64_t)flow_asm_isqrt_u128(r_sq_u128);
    int64_t sdf_dist_q16 = r_dist_q16 - radius_q16;

    int32_t normal_q16[3] = {0, 0, (int32_t)(-FLOW_Q16_ONE)};
    if (r_dist_q16 > 0LL) {
        for (size_t k = 0; k < 3U; ++k) {
            int64_t nk = (int64_t)(((flow_i128)rel[k] << FLOW_Q16_SHIFT) / (flow_i128)r_dist_q16);
            if (nk > FLOW_Q16_ONE) nk = FLOW_Q16_ONE;
            if (nk < -FLOW_Q16_ONE) nk = -FLOW_Q16_ONE;
            normal_q16[k] = (int32_t)nk;
        }
    }

    /* Exact visual surface projection: p_contact = p - f(p) * grad_f(p) */
    int64_t contact_pos[3];
    for (size_t k = 0; k < 3U; ++k) {
        int64_t shift_k = (int64_t)(((flow_i128)sdf_dist_q16 * (flow_i128)normal_q16[k]) >> FLOW_Q16_SHIFT);
        contact_pos[k] = pos[k] - shift_k;
    }

    if (out_manifold) {
        out_manifold->hit_zone           = (uint32_t)zone;
        out_manifold->contact_latched    = (zone == FLOW_HIT_LETHAL || zone == FLOW_HIT_GRAZE) ? 1U : 0U;
        out_manifold->t_hit_q16          = t_hit;
        out_manifold->sdf_dist_q16       = sdf_dist_q16;
        out_manifold->contact_pos_q16[0] = contact_pos[0];
        out_manifold->contact_pos_q16[1] = contact_pos[1];
        out_manifold->contact_pos_q16[2] = contact_pos[2];
        out_manifold->normal_q16[0]      = normal_q16[0];
        out_manifold->normal_q16[1]      = normal_q16[1];
        out_manifold->normal_q16[2]      = normal_q16[2];
        out_manifold->graze_bonus        = (zone == FLOW_HIT_GRAZE) ? 500U : 0U;
    }

    return zone;
}

uint32_t flow_game_step_contact_hysteresis(FlowGameEngineState *engine,
                                           int64_t sdf_dist_q16) {
    if (!engine)
        return 0U;

    int32_t dist_i32 = (sdf_dist_q16 > (int64_t)INT32_MAX) ? INT32_MAX
                     : ((sdf_dist_q16 < (int64_t)INT32_MIN) ? INT32_MIN : (int32_t)sdf_dist_q16);
    const int32_t x_vec[1] = { dist_i32 };

    FlowQPolyEval ev = FlowContactSkinPoly_eval_hysteresis(
        &engine->contact_skin_poly, x_vec, engine->contact_skin_mask
    );

    uint64_t target_mask = ev.violated_mask & 1ULL;
    if (target_mask != engine->contact_skin_mask) {
        uint64_t next_mask = 0ULL;
        FlowKanStatus kan = flow_cubical_step_neighbor(
            engine->contact_skin_mask, 0U, 0ULL, &next_mask
        );
        if (kan == FLOW_KAN_FILLED_HOMOTOPIC) {
            engine->contact_skin_mask = next_mask;
            engine->contact_latched = (next_mask & 1ULL) ? 1U : 0U;
        }
    }
    engine->last_contact.contact_latched = engine->contact_latched;
    return engine->contact_latched;
}

void flow_game_trigger_hitstop(FlowGameEngineState *engine,
                               uint32_t frames,
                               int64_t scale_q16) {
    if (!engine)
        return;
    if (frames == 0U) {
        engine->hitstop_frames_remaining = 0U;
        engine->hitstop_scale_q16 = FLOW_Q16_ONE;
        return;
    }
    int64_t clamped_scale = scale_q16;
    if (clamped_scale < 1LL)
        clamped_scale = 1LL;
    if (clamped_scale > FLOW_Q16_ONE)
        clamped_scale = FLOW_Q16_ONE;

    engine->hitstop_frames_remaining = frames;
    engine->hitstop_scale_q16 = clamped_scale;
}

void flow_game_apply_impulse_jet2(FlowGameEngineState *engine,
                                  const int64_t dv_q16[3],
                                  const int64_t da_q16[3]) {
    if (!engine)
        return;
    __extension__ typedef unsigned __int128 u128;
    for (size_t k = 0; k < 3U; ++k) {
        if (dv_q16) {
            engine->craft_traj[k].d1 = flow_game_q16_add(engine->craft_traj[k].d1, dv_q16[k]);
        }
        if (da_q16) {
            engine->craft_traj[k].d2 = flow_game_q16_add(engine->craft_traj[k].d2, da_q16[k]);
        }
    }
    int64_t v_max_subluminal = (FLOW_C_LIGHT_ENGINE_Q16 * FLOW_BETA_MAX_Q16) >> FLOW_Q16_SHIFT;
    flow_i128 v_sq = (flow_i128)engine->craft_traj[0].d1 * engine->craft_traj[0].d1
                   + (flow_i128)engine->craft_traj[1].d1 * engine->craft_traj[1].d1
                   + (flow_i128)engine->craft_traj[2].d1 * engine->craft_traj[2].d1;
    int64_t v_norm = (int64_t)flow_asm_isqrt_u128((u128)v_sq);
    if (v_norm > v_max_subluminal && v_norm > 0LL) {
        for (size_t k = 0; k < 3U; ++k) {
            engine->craft_traj[k].d1 = (int64_t)(((flow_i128)engine->craft_traj[k].d1 * v_max_subluminal) / v_norm);
        }
    }
    for (size_t k = 0; k < 3U; ++k) {
        engine->prev_craft_traj[k] = engine->craft_traj[k];
    }
}

/*
 * Subsystem 2: Forced Perspective & Stage Handoff (Layer 0 <-> Layer 1)
 *
 * Evaluates FlowStagePoly hysteresis (b_enter=10000, b_exit=8000) and steps
 * cubical neighbor homotopy via flow_cubical_step_neighbor to prevent chattering.
 */
uint32_t flow_game_step_stage_handoff(FlowGameEngineState *engine,
                                      const int64_t pos_q16[3]) {
    if (!engine || !pos_q16)
        return 0U;

    const int32_t pos_i32[3] = {
        (int32_t)(pos_q16[0] >> FLOW_Q16_SHIFT),
        (int32_t)(pos_q16[1] >> FLOW_Q16_SHIFT),
        (int32_t)(pos_q16[2] >> FLOW_Q16_SHIFT)
    };

    /* Evaluate dual-threshold hysteresis */
    FlowQPolyEval ev = FlowStagePoly_eval_hysteresis(
        &engine->stage_poly, pos_i32, engine->stage_mask
    );

    uint64_t target_mask = ev.violated_mask & 1ULL;
    if (target_mask != engine->stage_mask) {
        uint64_t next_mask = 0ULL;
        FlowKanStatus kan = flow_cubical_step_neighbor(
            engine->stage_mask, 0U, 0ULL, &next_mask
        );
        if (kan == FLOW_KAN_FILLED_HOMOTOPIC) {
            engine->stage_mask = next_mask;
            engine->current_layer = (next_mask & 1ULL) ? 1U : 0U;
            flow_qsbr_publish_ptr(engine->qsbr_ctx, &engine->stage_poly);
        }
    }

    return engine->current_layer;
}

/*
 * Subsystem 3: Topological Entropy Sparse Wave64 Culling
 *
 * Partitions viewport into 36 Wave64 groups (64x36 tiles = 2304 tiles).
 * Evaluates temporal/spatial 2-Jet variation per tile via flow_jet2_nondegenerate_ballot64,
 * compacts active tiles in 1 cycle via flow_gpu_ballot_compact_wave64 (POPCNT+CTZ),
 * achieving >80% tile culling on space scenes.
 */
uint32_t flow_game_cull_tiles_wave64(FlowGameEngineState *engine) {
    if (!engine)
        return 0U;

    /*
     * Calculate projected celestial center in tile coordinates:
     * Screen: 64 tiles wide (0..63), 36 tiles high (0..35).
     * Center tile = (32, 18).
     */
    int64_t craft_z = engine->craft_traj[2].v;
    int64_t planet_z = engine->celestial_pos_q16[2];
    int64_t rel_z = planet_z - craft_z;
    if (rel_z < (100LL << FLOW_Q16_SHIFT))
        rel_z = 100LL << FLOW_Q16_SHIFT;

    /* Apparent radius in tile coordinates (Q16) */
    int64_t app_rad_tiles = (engine->celestial_radius_q16 * 32LL) / (rel_z >> FLOW_Q16_SHIFT);
    int32_t rad_tiles = (int32_t)(app_rad_tiles >> FLOW_Q16_SHIFT);
    if (rad_tiles < 2)
        rad_tiles = 2;
    if (rad_tiles > 18)
        rad_tiles = 18;

    const int32_t center_tx = 32;
    const int32_t center_ty = 18;

    engine->active_tile_count = 0U;

    /* Thresholds for nondegenerate 2-Jet variations */
    const int64_t eps_d1 = 1LL << (FLOW_Q16_SHIFT - 4); /* ~0.0625 */
    const int64_t eps_d2 = 1LL << (FLOW_Q16_SHIFT - 4);

    for (uint32_t g = 0U; g < FLOW_GAME_WAVE64_GROUPS; ++g) {
        int32_t ty = (int32_t)g;
        int32_t dy = ty - center_ty;

        for (uint32_t lane = 0U; lane < FLOW_WAVE64_SIZE; ++lane) {
            int32_t tx = (int32_t)lane;
            int32_t dx = tx - center_tx;
            int32_t dist_sq = dx * dx + dy * dy;

            /*
             * Active regions:
             *   1. Planet & rings silhouette: dist_sq <= (rad_tiles + 2)^2
             *   2. Center HUD Reticle: tx in [28..35] and ty in [15..20]
             *   3. Bottom HUD instrument bar: ty >= 33
             * Vacuum tiles: all derivatives degenerate to 0.
             */
            bool is_planet = (dist_sq <= (rad_tiles + 2) * (rad_tiles + 2));
            bool is_reticle = (tx >= 28 && tx <= 35 && ty >= 15 && ty <= 20);
            bool is_hud_bar = (ty >= 33);

            if (is_planet || is_reticle || is_hud_bar) {
                /* Active non-degenerate tile: non-zero 2-Jet variation */
                engine->tile_jets[g][lane] = flow_jet2_make(
                    FLOW_Q16_ONE,
                    FLOW_Q16_ONE + ((int64_t)dx << 12),
                    FLOW_Q16_ONE + ((int64_t)dy << 12)
                );
            } else {
                /* Static vacuum background tile: zero variation */
                engine->tile_jets[g][lane] = flow_jet2_make(0LL, 0LL, 0LL);
            }
        }

        /* Wave64 ballot evaluation */
        uint64_t ballot = flow_jet2_nondegenerate_ballot64(
            engine->tile_jets[g], FLOW_WAVE64_SIZE, eps_d1, eps_d2
        );
        engine->wave64_ballots[g] = ballot;

        /* Compact active tiles in 1 cycle using hardware POPCNT + CTZ */
        uint32_t group_base = g * FLOW_WAVE64_SIZE;
        uint32_t compacted = flow_gpu_ballot_compact_wave64(
            ballot, group_base, &engine->active_tile_indices[engine->active_tile_count]
        );
        engine->active_tile_count += compacted;
    }

    return engine->active_tile_count;
}

/*
 * Subsystem 4: Ricardian v4 Economy Step
 *
 * Steps Harberger 70/30 tax conservation, Assembly Index depth monotonicity,
 * AMM constant-product swap with floor, and 800K/760K thermal hysteresis.
 */
void flow_game_step_ricardian(FlowGameEngineState *engine,
                              uint64_t valuation,
                              uint64_t tax_rate_bps,
                              uint64_t swap_amt,
                              uint64_t fee_bps,
                              uint64_t temp_delta_kelvin) {
    if (!engine)
        return;

    /* 1. Harberger 70/30 split tax conservation */
    uint64_t tax = ricardian_harberger_tax(valuation, tax_rate_bps);
    uint64_t div = ricardian_harberger_div(tax);
    uint64_t burn = ricardian_harberger_burn(tax);

    FlowResultU64 sum_res = flow_asm_qadd_i64(
        (int64_t)div, (int64_t)burn, &engine->hardware_sieve
    );
    if (sum_res.boundary_tag == 0ULL && sum_res.value == tax) {
        engine->ricardian.harberger_pool += div;
        engine->ricardian.burned_supply += burn;
    }

    /* 2. Assembly depth monotonicity & compression */
    engine->ricardian.assembly_depth = ricardian_join_assembly(
        engine->ricardian.assembly_depth, 1ULL, 0ULL
    );

    /* 3. Constant-product AMM swap with reserve floor */
    if (swap_amt > 0ULL) {
        uint64_t next_b = ricardian_amm_after_swap(
            engine->ricardian.reserve_a, engine->ricardian.reserve_b,
            swap_amt, fee_bps
        );
        engine->ricardian.reserve_a += swap_amt;
        engine->ricardian.reserve_b = next_b;
    }

    /* 4. Thermal Meltdown Hysteresis (800K enter / 760K exit) */
    engine->ricardian.thermal_kelvin += temp_delta_kelvin;
    if (engine->thermal_shutdown == 0U) {
        if (engine->ricardian.thermal_kelvin >= 800ULL) {
            engine->thermal_shutdown = 1U;
        }
    } else {
        if (engine->ricardian.thermal_kelvin <= 760ULL) {
            engine->thermal_shutdown = 0U;
        }
    }

    engine->ricardian_epoch++;
}

/*
 * Main Engine Simulation Step (Deterministic C17 Q48.16)
 */
int flow_game_step(FlowGameEngineState *engine, int64_t dt_q16) {
    if (!engine || dt_q16 <= 0)
        return -1;

    engine->frame_index++;

    /* 0. Time-Dimension Action Feel: Hitstop Time Dilation & ~100ms Input Buffer */
    bool in_hitstop = (engine->hitstop_frames_remaining > 0U);
    int64_t dt_sim_q16 = dt_q16;
    if (in_hitstop) {
        dt_sim_q16 = flow_game_q16_mul(dt_q16, engine->hitstop_scale_q16);
        if (dt_sim_q16 < 1LL)
            dt_sim_q16 = 1LL;
        engine->hitstop_frames_remaining--;
        if (engine->hitstop_frames_remaining == 0U) {
            engine->hitstop_scale_q16 = FLOW_Q16_ONE;
        }
    }
    engine->last_effective_dt_q16 = dt_sim_q16;

    if (engine->input_buffered) {
        if (!in_hitstop && engine->thermal_shutdown == 0U) {
            flow_game_apply_input_direct(
                engine,
                engine->buffered_throttle_q16,
                engine->buffered_pitch_rate_q16,
                engine->buffered_yaw_rate_q16,
                engine->buffered_roll_rate_q16,
                engine->buffered_toggle_layer
            );
            engine->input_buffered = false;
            engine->input_buffer_timer_q16 = 0LL;
            engine->input_buffer_frames = 0U;
        } else {
            if (dt_q16 <= (FLOW_Q16_ONE / 10LL)) {
                engine->input_buffer_timer_q16 -= dt_q16;
                if (engine->input_buffer_timer_q16 <= 0LL) {
                    engine->input_buffered = false;
                    engine->input_buffer_timer_q16 = 0LL;
                    engine->input_buffer_frames = 0U;
                }
            } else {
                if (engine->input_buffer_frames > 0U)
                    engine->input_buffer_frames--;
                engine->input_buffer_timer_q16 =
                    ((int64_t)engine->input_buffer_frames * FLOW_INPUT_BUFFER_WINDOW_Q16) /
                    (int64_t)FLOW_INPUT_BUFFER_FRAMES;
                if (engine->input_buffer_frames == 0U) {
                    engine->input_buffered = false;
                    engine->input_buffer_timer_q16 = 0LL;
                }
            }
        }
    }

    /* Save previous trajectory for differential self-check */
    for (size_t k = 0; k < 3; ++k) {
        engine->prev_craft_traj[k] = engine->craft_traj[k];
    }

    /* Integrate craft worldline trajectory using 2-Jet Taylor evaluation with dt_sim_q16 */
    for (size_t k = 0; k < 3; ++k) {
        int64_t new_pos = flow_jet2_eval_taylor(engine->craft_traj[k], dt_sim_q16);
        int64_t new_vel = flow_game_q16_add(
            engine->craft_traj[k].d1,
            flow_game_q16_mul(engine->craft_traj[k].d2, dt_sim_q16)
        );
        engine->craft_traj[k].v = new_pos;
        engine->craft_traj[k].d1 = new_vel;
    }

    /* Integrate craft orientation */
    for (size_t k = 0; k < 3; ++k) {
        int64_t new_rot = flow_jet2_eval_taylor(engine->craft_rot[k], dt_sim_q16);
        engine->craft_rot[k].v = new_rot;
    }

    /* Verify step integrity via hardware self-check */
    for (size_t k = 0; k < 3; ++k) {
        engine->hardware_sieve = flow_asm_jet2_self_check_step(
            engine->prev_craft_traj[k], engine->craft_traj[k],
            dt_sim_q16, 64LL, 64LL, &engine->hardware_sieve
        );
    }

    /* Subsystem 1: Worldline Anti-Tunneling Collision & Concentric SDF Padding */
    int64_t collision_t = 0LL;
    bool hit = flow_game_check_worldline_collision(
        engine->craft_traj, engine->celestial_pos_q16,
        engine->celestial_radius_q16, dt_sim_q16, &collision_t
    );

    FlowHitZone zone = flow_game_eval_worldline_padded(
        engine->prev_craft_traj,
        engine->celestial_pos_q16,
        engine->celestial_radius_q16,
        engine->delta_hurt_q16,
        engine->delta_graze_q16,
        engine->delta_hit_q16,
        dt_sim_q16,
        &engine->last_contact
    );
    if (zone == FLOW_HIT_NONE) {
        zone = flow_game_eval_worldline_padded(
            engine->craft_traj,
            engine->celestial_pos_q16,
            engine->celestial_radius_q16,
            engine->delta_hurt_q16,
            engine->delta_graze_q16,
            engine->delta_hit_q16,
            dt_sim_q16,
            &engine->last_contact
        );
    }
    if (zone == FLOW_HIT_LETHAL) {
        hit = true;
        collision_t = engine->last_contact.t_hit_q16;
    }

    /* Contact Skin Dual-Threshold Hysteresis [delta_enter, delta_exit] */
    uint32_t skin_latched = flow_game_step_contact_hysteresis(
        engine, engine->last_contact.sdf_dist_q16
    );

    /* Edge-triggered Hitstop & Graze Spark Deflection / Bonus */
    if (zone == FLOW_HIT_LETHAL && engine->prev_hit_zone != FLOW_HIT_LETHAL) {
        flow_game_trigger_hitstop(
            engine, FLOW_HITSTOP_LETHAL_FRAMES, FLOW_HITSTOP_DEFAULT_SCALE_Q16
        );
    } else if (zone == FLOW_HIT_GRAZE && engine->prev_hit_zone < FLOW_HIT_GRAZE) {
        engine->graze_count++;
        engine->ricardian.m0_vault += (uint64_t)engine->last_contact.graze_bonus;
        flow_game_trigger_hitstop(
            engine, FLOW_HITSTOP_GRAZE_FRAMES, FLOW_Q16_ONE >> 3U
        );
        flow_cine_add_trauma(&engine->cine_brain, 16384LL);
        int64_t spark_dv[3] = {
            (int64_t)engine->last_contact.normal_q16[0] * 8LL,
            (int64_t)engine->last_contact.normal_q16[1] * 8LL,
            0LL
        };
        flow_game_apply_impulse_jet2(engine, spark_dv, NULL);
    }
    engine->prev_hit_zone = (uint32_t)zone;

    /* Subsystem 2: Forced Perspective & Stage Handoff */
    int64_t pos_q16[3] = {
        engine->craft_traj[0].v,
        engine->craft_traj[1].v,
        engine->craft_traj[2].v
    };
    uint32_t active_layer = flow_game_step_stage_handoff(engine, pos_q16);

    /* Subsystem 3: Wave64 Viewport Tile Entropy Culling */
    uint32_t active_tiles = flow_game_cull_tiles_wave64(engine);

    /* Subsystem 4: Ricardian v4 Economy Step */
    uint64_t temp_delta = engine->thermal_shutdown ? 0ULL : (engine->craft_throttle_q16 > 0 ? 1ULL : 0ULL);
    flow_game_step_ricardian(engine, 200000ULL, 500ULL, 1000ULL, 30ULL, temp_delta);

    /* If thermal shutdown just cleared within the ~100ms Input Buffer Window, fire buffered input */
    if (engine->input_buffered && !in_hitstop && engine->thermal_shutdown == 0U &&
        engine->hitstop_frames_remaining == 0U) {
        flow_game_apply_input_direct(
            engine,
            engine->buffered_throttle_q16,
            engine->buffered_pitch_rate_q16,
            engine->buffered_yaw_rate_q16,
            engine->buffered_roll_rate_q16,
            engine->buffered_toggle_layer
        );
        engine->input_buffered = false;
        engine->input_buffer_timer_q16 = 0LL;
        engine->input_buffer_frames = 0U;
    }

    /*
     * Subsystem 5: FlowCineRig64 Cinematography & Physical Camera Optics Step
     * Note: Receives full unscaled dt_q16 so camera shake vibrates at full speed during Hitstop!
     */
    flow_cine_brain_step(
        &engine->cine_brain,
        engine->craft_traj,
        engine->craft_rot,
        engine->ricardian.thermal_kelvin,
        hit,
        dt_q16
    );
    engine->hardware_sieve &= engine->cine_brain.hardware_sieve;

    /*
     * Subsystem 6: Unified Relativistic & Medium Scale Interface (FlowRelativeScale64)
     * Computes the 4 universal dimensionless relative ratios between Observer (J_craft)
     * and Medium/Spacetime (J_env) without any hard-coded effect classes.
     */
    FlowJet2 medium_traj[3] = {
        flow_jet2_make(engine->celestial_pos_q16[0], 0LL, 0LL),
        flow_jet2_make(engine->celestial_pos_q16[1], 0LL, 0LL),
        flow_jet2_make(engine->celestial_pos_q16[2], 0LL, 0LL)
    };
    int64_t c_eff_q16 = FLOW_C_LIGHT_ENGINE_Q16; /* Effective relativistic speed of light / wave speed */
    int64_t rs_q16 = engine->celestial_radius_q16 >> 2U; /* Effective gravitational / refractive radius */
    int64_t self_flux_q16 = ((int64_t)engine->ricardian.thermal_kelvin - 700LL) << FLOW_Q16_SHIFT;
    int64_t env_flux_q16  = 40LL << FLOW_Q16_SHIFT;
    flow_game_resolve_relative_scale(
        engine->craft_traj,
        medium_traj,
        c_eff_q16,
        rs_q16,
        self_flux_q16,
        env_flux_q16,
        10LL << FLOW_Q16_SHIFT,
        engine->celestial_radius_q16,
        &engine->relative_scale
    );
    if (in_hitstop && dt_q16 > 0LL) {
        engine->relative_scale.proper_time_ratio_q16 = (int32_t)flow_game_q16_div(dt_sim_q16, dt_q16);
    }

    /* Advance QSBR reader epoch checkpoint */
    flow_qsbr_checkpoint(&engine->qsbr_reader);

    /* Publish 64-byte aligned FlowGameFramePacket to Lock-Free SPSC Ring */
    FlowGameFramePacket pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.frame_index = engine->frame_index;
    pkt.timestamp_ns = engine->frame_index * 16666666ULL; /* ~60 Hz simulation */
    pkt.craft_x_q16 = engine->craft_traj[0].v;
    pkt.craft_y_q16 = engine->craft_traj[1].v;
    pkt.craft_z_q16 = engine->craft_traj[2].v;
    pkt.craft_vx_q16 = engine->craft_traj[0].d1;
    pkt.craft_vy_q16 = engine->craft_traj[1].d1;
    pkt.craft_vz_q16 = engine->craft_traj[2].d1;
    pkt.craft_pitch_q16 = engine->craft_rot[0].v;
    pkt.craft_yaw_q16 = engine->craft_rot[1].v;
    pkt.craft_roll_q16 = engine->craft_rot[2].v;
    pkt.stage_layer = active_layer;
    pkt.active_tiles_count = active_tiles;
    pkt.thermal_shutdown = engine->thermal_shutdown;
    pkt.collision_hit = hit ? 1ULL : 0ULL;
    pkt.collision_t_q16 = collision_t;
    pkt.hardware_sieve = engine->hardware_sieve;
    pkt.hit_zone = (uint64_t)zone;
    pkt.hitstop_active = in_hitstop ? 1ULL : 0ULL;
    pkt.effective_dt_q16 = dt_sim_q16;
    pkt.contact_x_q16 = engine->last_contact.contact_pos_q16[0];
    pkt.contact_y_q16 = engine->last_contact.contact_pos_q16[1];
    pkt.contact_z_q16 = engine->last_contact.contact_pos_q16[2];
    pkt.contact_latched = ((uint64_t)skin_latched & 1ULL) | (engine->graze_count << 1U);
    pkt.ricardian = engine->ricardian;
    pkt.cine_camera = engine->cine_brain.resolved;
    pkt.relative_scale = engine->relative_scale;
    pkt.fps_governor = engine->fps_governor;

    /* Push packet into ring (drop oldest if full) */
    if (!FlowGamePacketRing_push(&engine->ring, pkt)) {
        FlowGameFramePacket discard;
        FlowGamePacketRing_pop(&engine->ring, &discard);
        FlowGamePacketRing_push(&engine->ring, pkt);
    }

    return 0;
}

static inline int32_t flow_game_clamp_i32(int64_t v) {
    if (v > (int64_t)INT32_MAX) return INT32_MAX;
    if (v < (int64_t)INT32_MIN) return INT32_MIN;
    return (int32_t)v;
}

static inline flow_i128 flow_div_round_i128(flow_i128 num, flow_i128 den) {
    if (den == 0)
        return 0;
    if (den < 0) {
        num = -num;
        den = -den;
    }
    if (num >= 0)
        return (num + (den >> 1)) / den;
    return -((-num + (den >> 1)) / den);
}

int64_t flow_game_lorentz_rel_vel_q16(const int64_t u_obj_q16[3],
                                      const int64_t v_obs_q16[3],
                                      int64_t c_eff_q16,
                                      int64_t out_u_local_q16[3]) {
    if (!u_obj_q16 || !v_obs_q16) {
        if (out_u_local_q16) {
            out_u_local_q16[0] = 0LL;
            out_u_local_q16[1] = 0LL;
            out_u_local_q16[2] = 0LL;
        }
        return 0LL;
    }

    __extension__ typedef unsigned __int128 u128;

    int64_t c_safe = (c_eff_q16 > 1LL) ? c_eff_q16 : FLOW_C_LIGHT_DEFAULT_Q16;

    /* Exact co-moving fast path: u_obj == v_obs => u_local == 0 */
    if (u_obj_q16[0] == v_obs_q16[0] &&
        u_obj_q16[1] == v_obs_q16[1] &&
        u_obj_q16[2] == v_obs_q16[2]) {
        if (out_u_local_q16) {
            out_u_local_q16[0] = 0LL;
            out_u_local_q16[1] = 0LL;
            out_u_local_q16[2] = 0LL;
        }
        return 0LL;
    }

    flow_i128 u_sq_i128 = (flow_i128)u_obj_q16[0] * u_obj_q16[0]
                        + (flow_i128)u_obj_q16[1] * u_obj_q16[1]
                        + (flow_i128)u_obj_q16[2] * u_obj_q16[2];
    int64_t u_norm_q16 = (int64_t)flow_asm_isqrt_u128((u128)u_sq_i128);

    flow_i128 v_sq_i128 = (flow_i128)v_obs_q16[0] * v_obs_q16[0]
                        + (flow_i128)v_obs_q16[1] * v_obs_q16[1]
                        + (flow_i128)v_obs_q16[2] * v_obs_q16[2];
    int64_t v_norm_q16 = (int64_t)flow_asm_isqrt_u128((u128)v_sq_i128);

    /* Detect light-cone trajectory (within 4 Q16 LSB of c_safe for 3D lattice vectors) */
    int64_t light_tol = 4LL;
    bool u_is_light = (u_norm_q16 >= c_safe - light_tol) && (u_norm_q16 <= c_safe + light_tol);

    int64_t u_safe[3] = { u_obj_q16[0], u_obj_q16[1], u_obj_q16[2] };
    if ((!u_is_light && u_norm_q16 > c_safe) || (u_is_light && u_norm_q16 > c_safe)) {
        for (size_t k = 0; k < 3U; ++k) {
            u_safe[k] = (int64_t)flow_div_round_i128((flow_i128)u_obj_q16[k] * c_safe, u_norm_q16);
        }
        u_is_light = true;
    }

    /* Stationary observer fast path: v_obs == 0 => u_local == u_obj */
    if (v_norm_q16 == 0LL) {
        if (u_is_light && u_norm_q16 > 0LL && u_norm_q16 != c_safe) {
            for (size_t k = 0; k < 3U; ++k) {
                u_safe[k] = (int64_t)flow_div_round_i128((flow_i128)u_safe[k] * c_safe, u_norm_q16);
            }
        }
        if (out_u_local_q16) {
            out_u_local_q16[0] = u_safe[0];
            out_u_local_q16[1] = u_safe[1];
            out_u_local_q16[2] = u_safe[2];
        }
        return u_is_light ? c_safe : u_norm_q16;
    }

    /* Enforce subluminal observer frame ||v_obs|| <= 0.999 * c_safe */
    int64_t v_safe[3] = { v_obs_q16[0], v_obs_q16[1], v_obs_q16[2] };
    int64_t v_max_q16 = (int64_t)(((flow_i128)c_safe * FLOW_BETA_MAX_Q16) >> FLOW_Q16_SHIFT);
    if (v_max_q16 < 1LL)
        v_max_q16 = 1LL;
    if (v_norm_q16 > v_max_q16) {
        for (size_t k = 0; k < 3U; ++k) {
            v_safe[k] = (int64_t)flow_div_round_i128((flow_i128)v_obs_q16[k] * v_max_q16, v_norm_q16);
        }
        v_sq_i128 = (flow_i128)v_safe[0] * v_safe[0]
                  + (flow_i128)v_safe[1] * v_safe[1]
                  + (flow_i128)v_safe[2] * v_safe[2];
    }

    /*
     * 3D Einstein-Lorentz Velocity Transformation:
     *   u_par   = ((u . v) / ||v||^2) * v
     *   u_perp  = u - u_par
     *   u_local = ((u_par - v) * c^2 + u_perp * c * sqrt(c^2 - ||v||^2)) / (c^2 - u . v)
     *
     * To prevent signed 128-bit overflow when c_safe is large (e.g. SI vacuum light
     * speed FLOW_C_LIGHT_VACUUM_Q16 = 299792458 << 16 ~ 2^44.16, where c^3 ~ 2^132.5),
     * we scale O(c^2) weights down into <= 60-bit precision before multiplying by O(c).
     */
    flow_i128 c_sq = (flow_i128)c_safe * c_safe;
    flow_i128 u_dot_v = (flow_i128)u_safe[0] * v_safe[0]
                      + (flow_i128)u_safe[1] * v_safe[1]
                      + (flow_i128)u_safe[2] * v_safe[2];
    flow_i128 den = c_sq - u_dot_v;
    if (den < 1)
        den = 1;

    flow_i128 c2_minus_v2 = c_sq - v_sq_i128;
    if (c2_minus_v2 < 0)
        c2_minus_v2 = 0;
    int64_t sqrt_c2_v2 = (int64_t)flow_asm_isqrt_u128((u128)c2_minus_v2);

    uint32_t sq_shift = 0U;
    while ((c_sq >> sq_shift) > ((flow_i128)1 << 60)) {
        sq_shift += 2U;
    }
    flow_i128 c_sq_s = c_sq >> sq_shift;
    flow_i128 c_s_prod = ((flow_i128)c_safe * (flow_i128)sqrt_c2_v2) >> sq_shift;
    flow_i128 den_s = den >> sq_shift;
    if (den_s < 1)
        den_s = 1;

    uint32_t v_shift = 0U;
    while ((v_sq_i128 >> v_shift) > ((flow_i128)1 << 60)) {
        v_shift += 2U;
    }
    flow_i128 u_dot_v_vs = u_dot_v >> v_shift;
    flow_i128 v_sq_vs = v_sq_i128 >> v_shift;

    size_t v_nonzero_axes = 0U;
    for (size_t k = 0; k < 3U; ++k) {
        if (v_safe[k] != 0LL)
            v_nonzero_axes++;
    }

    int64_t u_local[3] = { 0LL, 0LL, 0LL };
    for (size_t k = 0; k < 3U; ++k) {
        int64_t u_par_k = 0LL;
        if (v_nonzero_axes == 1U) {
            u_par_k = (v_safe[k] != 0LL) ? u_safe[k] : 0LL;
        } else if (v_sq_vs > 0) {
            u_par_k = (int64_t)flow_div_round_i128(u_dot_v_vs * (flow_i128)v_safe[k], v_sq_vs);
        }
        int64_t u_perp_k = u_safe[k] - u_par_k;
        flow_i128 num = (flow_i128)(u_par_k - v_safe[k]) * c_sq_s
                      + (flow_i128)u_perp_k * c_s_prod;
        u_local[k] = (int64_t)flow_div_round_i128(num, den_s);
    }

    flow_i128 local_sq_i128 = (flow_i128)u_local[0] * u_local[0]
                            + (flow_i128)u_local[1] * u_local[1]
                            + (flow_i128)u_local[2] * u_local[2];
    int64_t local_norm_q16 = (int64_t)flow_asm_isqrt_u128((u128)local_sq_i128);

    if (u_is_light) {
        /*
         * Constancy of the Speed of Light c:
         * Every light beam (||u|| == c) has exact speed c in every subluminal frame.
         * Normalize single-axis or multi-axis light vectors onto the exact light sphere ||u'|| == c_safe.
         */
        size_t nonzero_axes = 0U;
        size_t active_axis = 0U;
        for (size_t k = 0; k < 3U; ++k) {
            if (u_local[k] != 0LL) {
                nonzero_axes++;
                active_axis = k;
            }
        }
        if (nonzero_axes == 1U) {
            u_local[active_axis] = (u_local[active_axis] > 0LL) ? c_safe : -c_safe;
        } else if (nonzero_axes > 1U && local_norm_q16 > 0LL && local_norm_q16 != c_safe) {
            for (size_t k = 0; k < 3U; ++k) {
                u_local[k] = (int64_t)flow_div_round_i128((flow_i128)u_local[k] * c_safe, local_norm_q16);
            }
        }
        local_norm_q16 = c_safe;
    } else if (local_norm_q16 >= c_safe) {
        /* Subluminal closure: two subluminal velocities never compose to >= c */
        for (size_t k = 0; k < 3U; ++k) {
            u_local[k] = (int64_t)flow_div_round_i128((flow_i128)u_local[k] * v_max_q16, local_norm_q16);
        }
        local_norm_q16 = v_max_q16;
    }

    if (out_u_local_q16) {
        out_u_local_q16[0] = u_local[0];
        out_u_local_q16[1] = u_local[1];
        out_u_local_q16[2] = u_local[2];
    }
    return local_norm_q16;
}

int64_t flow_game_lorentz_emit_light_world_q16(const int64_t v_source_q16[3],
                                               const int64_t dir_local_q16[3],
                                               int64_t c_eff_q16,
                                               int64_t out_u_world_q16[3]) {
    int64_t c_safe = (c_eff_q16 > 1LL) ? c_eff_q16 : FLOW_C_LIGHT_DEFAULT_Q16;
    if (!v_source_q16 || !dir_local_q16) {
        if (out_u_world_q16) {
            out_u_world_q16[0] = c_safe;
            out_u_world_q16[1] = 0LL;
            out_u_world_q16[2] = 0LL;
        }
        return c_safe;
    }

    __extension__ typedef unsigned __int128 u128;

    flow_i128 dir_sq = (flow_i128)dir_local_q16[0] * dir_local_q16[0]
                     + (flow_i128)dir_local_q16[1] * dir_local_q16[1]
                     + (flow_i128)dir_local_q16[2] * dir_local_q16[2];

    int64_t u_local_light[3] = { c_safe, 0LL, 0LL };
    if (dir_sq > 0) {
        /* Scale small direction vectors before isqrt to preserve full 64-bit angular precision */
        uint32_t norm_shift = (dir_sq < ((flow_i128)1 << 60)) ? 30U : 0U;
        int64_t dir_norm_scaled = (int64_t)flow_asm_isqrt_u128((u128)dir_sq << (2U * norm_shift));
        if (dir_norm_scaled > 0LL) {
            for (size_t k = 0; k < 3U; ++k) {
                flow_i128 scaled_num = ((flow_i128)dir_local_q16[k] * (flow_i128)c_safe) << norm_shift;
                u_local_light[k] = (int64_t)flow_div_round_i128(scaled_num, (flow_i128)dir_norm_scaled);
            }
        }
    }

    /* Inverse Lorentz velocity boost from source rest frame to world frame (v_obs = -v_source) */
    int64_t neg_v_source[3] = {
        -v_source_q16[0],
        -v_source_q16[1],
        -v_source_q16[2]
    };
    return flow_game_lorentz_rel_vel_q16(u_local_light, neg_v_source, c_safe, out_u_world_q16);
}

void flow_game_resolve_relative_scale(const FlowJet2 self_traj[3],
                                      const FlowJet2 medium_traj[3],
                                      int64_t c_eff_q16,
                                      int64_t schwarzschild_rs_q16,
                                      int64_t self_energy_flux_q16,
                                      int64_t env_pressure_flux_q16,
                                      int64_t self_length_q16,
                                      int64_t field_length_q16,
                                      FlowRelativeScale64 *out_scale) {
    if (!out_scale)
        return;
    memset(out_scale, 0, sizeof(*out_scale));
    if (!self_traj || !medium_traj)
        return;

    __extension__ typedef unsigned __int128 u128;

    int64_t c_safe = (c_eff_q16 > 1LL) ? c_eff_q16 : FLOW_C_LIGHT_DEFAULT_Q16;

    /* 1. Relative 2-Jet with 3D Einstein-Lorentz Velocity Addition */
    int64_t dx[3], dv[3], da[3];
    int64_t u_self[3], v_med[3];
    for (size_t k = 0; k < 3U; ++k) {
        dx[k]     = self_traj[k].v  - medium_traj[k].v;
        u_self[k] = self_traj[k].d1;
        v_med[k]  = medium_traj[k].d1;
        da[k]     = self_traj[k].d2 - medium_traj[k].d2;
    }

    int64_t v_rel_q16 = flow_game_lorentz_rel_vel_q16(u_self, v_med, c_safe, dv);

    u128 r_sq_u128 = (u128)((flow_i128)dx[0] * dx[0] + (flow_i128)dx[1] * dx[1] + (flow_i128)dx[2] * dx[2]);
    int64_t r_dist_q16 = (int64_t)flow_asm_isqrt_u128(r_sq_u128);

    u128 a_sq_u128 = (u128)((flow_i128)da[0] * da[0] + (flow_i128)da[1] * da[1] + (flow_i128)da[2] * da[2]);
    int64_t a_rel_q16 = (int64_t)flow_asm_isqrt_u128(a_sq_u128);

    /* Unit relative velocity direction u = dv / ||dv|| */
    if (v_rel_q16 > 0LL) {
        for (size_t k = 0; k < 3U; ++k) {
            out_scale->rel_vel_dir_q16[k] = flow_game_clamp_i32(flow_game_q16_div(dv[k], v_rel_q16));
        }
    } else {
        out_scale->rel_vel_dir_q16[2] = (int32_t)FLOW_Q16_ONE;
    }

    /* Unit metric gradient grad(Phi) pointing toward field center (-dx / ||dx||) */
    if (r_dist_q16 > 0LL) {
        for (size_t k = 0; k < 3U; ++k) {
            out_scale->metric_grad_q16[k] = flow_game_clamp_i32(flow_game_q16_div(-dx[k], r_dist_q16));
        }
    } else {
        out_scale->metric_grad_q16[2] = (int32_t)(-FLOW_Q16_ONE);
    }

    /* 2. Lorentz Scale: beta = ||dv|| / c_eff, gamma = (1 - beta^2)^(-1/2), Doppler sqrt((1+b)/(1-b)) */
    int64_t beta_q16 = (int64_t)flow_div_round_i128(((flow_i128)v_rel_q16) << FLOW_Q16_SHIFT, (flow_i128)c_safe);
    if (beta_q16 < 0LL) beta_q16 = 0LL;
    if (beta_q16 > FLOW_Q16_ONE) beta_q16 = FLOW_Q16_ONE; /* Light cone: beta == 1.0 exactly */

    /* Regularize beta at 0.999c when computing gamma and Doppler to prevent division-by-zero singularity */
    int64_t beta_reg_q16 = (beta_q16 > FLOW_BETA_MAX_Q16) ? FLOW_BETA_MAX_Q16 : beta_q16;
    int64_t beta_sq_q16 = flow_game_q16_mul(beta_reg_q16, beta_reg_q16);
    int64_t one_minus_b2 = FLOW_Q16_ONE - beta_sq_q16;
    if (one_minus_b2 < 64LL) one_minus_b2 = 64LL;

    /* sqrt(one_minus_b2) in Q16: isqrt(one_minus_b2 << 16) */
    int64_t inv_gamma_q16 = (int64_t)flow_asm_isqrt_u128((u128)one_minus_b2 << FLOW_Q16_SHIFT);
    if (inv_gamma_q16 < 1LL) inv_gamma_q16 = 1LL;
    int64_t gamma_q16 = flow_game_q16_div(FLOW_Q16_ONE, inv_gamma_q16);

    int64_t doppler_sq_q16 = flow_game_q16_div(FLOW_Q16_ONE + beta_reg_q16, FLOW_Q16_ONE - beta_reg_q16);
    int64_t doppler_q16 = (int64_t)flow_asm_isqrt_u128((u128)((doppler_sq_q16 > 0LL) ? doppler_sq_q16 : 0LL) << FLOW_Q16_SHIFT);

    /* 3. Metric Curvature / Refractive Scale: Phi = r_s / (2*r), grav_redshift = sqrt(max(0, 1 - 2*Phi)) */
    int64_t r_safe = (r_dist_q16 > 1LL) ? r_dist_q16 : 1LL;
    int64_t rs_pos = (schwarzschild_rs_q16 > 0LL) ? schwarzschild_rs_q16 : 0LL;
    int64_t two_phi_q16 = flow_game_q16_div(rs_pos, r_safe);
    int64_t phi_q16 = two_phi_q16 >> 1U;

    int64_t g00_q16 = FLOW_Q16_ONE - two_phi_q16;
    int64_t grav_redshift_q16 = 0LL;
    if (g00_q16 > 0LL) {
        grav_redshift_q16 = (int64_t)flow_asm_isqrt_u128((u128)g00_q16 << FLOW_Q16_SHIFT);
    }

    /* Proper time ratio: tau_rel = gamma * sqrt(max(g00, eps)) */
    int64_t tau_rel_q16 = flow_game_q16_mul(gamma_q16, (grav_redshift_q16 > 1024LL) ? grav_redshift_q16 : 1024LL);

    /* 4. Signed Boundary Flux Ratio: Pi_rel = (P_self - P_env) / P_0 */
    int64_t p_dyn_env = env_pressure_flux_q16 + (v_rel_q16 >> 4U);
    int64_t p_base = (p_dyn_env > FLOW_Q16_ONE) ? p_dyn_env : FLOW_Q16_ONE;
    int64_t flux_ratio_q16 = flow_game_q16_div(self_energy_flux_q16 - p_dyn_env, p_base);

    /* 5. Spatial Renormalization Scale: Sigma_rel = L_self / L_field */
    int64_t l_field_safe = (field_length_q16 > 1LL) ? field_length_q16 : FLOW_Q16_ONE;
    int64_t spatial_scale_q16 = flow_game_q16_div(self_length_q16, l_field_safe);

    /* 6. Boundary Wave Phase Frequency & Tidal Stretch Anisotropy */
    int64_t abs_flux = (flux_ratio_q16 < 0LL) ? -flux_ratio_q16 : flux_ratio_q16;
    int64_t wave_freq_q16 = FLOW_Q16_ONE + (v_rel_q16 >> 5U) + (a_rel_q16 >> 6U) + abs_flux * 4LL;
    int64_t tidal_stretch_q16 = FLOW_Q16_ONE + two_phi_q16 + (gamma_q16 - FLOW_Q16_ONE);

    out_scale->proper_time_ratio_q16   = flow_game_clamp_i32(tau_rel_q16);
    out_scale->lorentz_beta_q16        = flow_game_clamp_i32(beta_q16);
    out_scale->lorentz_gamma_q16       = flow_game_clamp_i32(gamma_q16);
    out_scale->doppler_factor_q16      = flow_game_clamp_i32(doppler_q16);
    out_scale->metric_potential_q16    = flow_game_clamp_i32(phi_q16);
    out_scale->grav_redshift_q16       = flow_game_clamp_i32(grav_redshift_q16);
    out_scale->flux_pressure_ratio_q16 = flow_game_clamp_i32(flux_ratio_q16);
    out_scale->spatial_scale_q16       = flow_game_clamp_i32(spatial_scale_q16);
    out_scale->wave_phase_freq_q16     = flow_game_clamp_i32(wave_freq_q16);
    out_scale->tidal_stretch_q16       = flow_game_clamp_i32(tidal_stretch_q16);
}

void flow_game_eval_einstein_elevator(const FlowJet2 elevator_world_traj[3],
                                      const FlowJet2 obj_world_traj[3],
                                      const int64_t gravity_accel_q16[3],
                                      FlowJet2 out_local_traj[3],
                                      int64_t out_proper_g_q16[3]) {
    if (!elevator_world_traj || !obj_world_traj)
        return;

    __extension__ typedef unsigned __int128 u128;

    int64_t u_obj[3] = {
        obj_world_traj[0].d1,
        obj_world_traj[1].d1,
        obj_world_traj[2].d1
    };
    int64_t v_elev[3] = {
        elevator_world_traj[0].d1,
        elevator_world_traj[1].d1,
        elevator_world_traj[2].d1
    };

    /* Select canonical speed of light scale matching the trajectory regime */
    flow_i128 u_sq = (flow_i128)u_obj[0] * u_obj[0]
                   + (flow_i128)u_obj[1] * u_obj[1]
                   + (flow_i128)u_obj[2] * u_obj[2];
    flow_i128 v_sq = (flow_i128)v_elev[0] * v_elev[0]
                   + (flow_i128)v_elev[1] * v_elev[1]
                   + (flow_i128)v_elev[2] * v_elev[2];
    int64_t u_norm = (int64_t)flow_asm_isqrt_u128((u128)u_sq);
    int64_t v_norm = (int64_t)flow_asm_isqrt_u128((u128)v_sq);
    int64_t max_norm = (u_norm > v_norm) ? u_norm : v_norm;

    int64_t c_eff = FLOW_C_LIGHT_DEFAULT_Q16;
    if (max_norm > FLOW_C_LIGHT_ENGINE_Q16 + 4LL) {
        c_eff = FLOW_C_LIGHT_VACUUM_Q16;
    } else if (max_norm > FLOW_C_LIGHT_DEFAULT_Q16 + 4LL) {
        c_eff = FLOW_C_LIGHT_ENGINE_Q16;
    }

    int64_t u_local[3] = { 0LL, 0LL, 0LL };
    int64_t local_speed = flow_game_lorentz_rel_vel_q16(u_obj, v_elev, c_eff, u_local);
    bool is_light_ray = (local_speed >= c_eff - 4LL) && (u_norm >= c_eff - 4LL);

    int64_t a_equiv[3] = { 0LL, 0LL, 0LL };
    for (size_t k = 0; k < 3U; ++k) {
        int64_t g_k = gravity_accel_q16 ? gravity_accel_q16[k] : 0LL;
        int64_t obj_total_d2 = flow_game_q16_add(obj_world_traj[k].d2, g_k);
        a_equiv[k] = flow_game_q16_sub(obj_total_d2, elevator_world_traj[k].d2);

        /* Proper G-force felt by the elevator passenger: g_proper = a_elev - g_field */
        if (out_proper_g_q16) {
            out_proper_g_q16[k] = flow_game_q16_sub(elevator_world_traj[k].d2, g_k);
        }
    }

    /*
     * On the null light cone (||u_local|| == c_eff), light speed c cannot increase or
     * decrease along its propagation direction (d/dt ||u_local||^2 = 2 u_local . a_local = 0);
     * longitudinal acceleration produces gravitational frequency shift while only transverse
     * acceleration a_perp = a_equiv - ((a_equiv . u_local) / c^2) * u_local deflects the ray.
     */
    int64_t a_local[3] = { a_equiv[0], a_equiv[1], a_equiv[2] };
    if (is_light_ray && c_eff > 0LL) {
        uint32_t l_shift = 0U;
        while ((c_eff >> l_shift) > (1LL << 30)) {
            l_shift++;
        }
        int64_t c_s = c_eff >> l_shift;
        int64_t u_s[3] = {
            u_local[0] >> l_shift,
            u_local[1] >> l_shift,
            u_local[2] >> l_shift
        };
        flow_i128 c_s_sq = (flow_i128)c_s * c_s;
        if (c_s_sq > 0) {
            flow_i128 a_dot_u_s = (flow_i128)a_equiv[0] * u_s[0]
                                + (flow_i128)a_equiv[1] * u_s[1]
                                + (flow_i128)a_equiv[2] * u_s[2];
            for (size_t k = 0; k < 3U; ++k) {
                int64_t a_par_k = (int64_t)flow_div_round_i128(a_dot_u_s * (flow_i128)u_s[k], c_s_sq);
                a_local[k] = a_equiv[k] - a_par_k;
            }
        }
    }

    if (out_local_traj) {
        for (size_t k = 0; k < 3U; ++k) {
            out_local_traj[k] = flow_jet2_make(
                flow_game_q16_sub(obj_world_traj[k].v, elevator_world_traj[k].v),
                u_local[k],
                a_local[k]
            );
        }
    }
}

bool flow_game_poll_frame_packet(FlowGameEngineState *engine,
                                 FlowGameFramePacket *out_packet) {
    if (!engine || !out_packet)
        return false;
    return (FlowGamePacketRing_pop(&engine->ring, out_packet) == 1);
}

/*
 * ============================================================================
 * Subsystem 5: O(1) Phase-Staggered Wave64 Timing Wheel Implementation
 * ============================================================================
 * Solves the "Don't Starve" 100,000-entity timer & polling jitter problem.
 * 100% pure integer C17 (zero float/double).
 * ============================================================================
 */

void flow_timing_wheel_init(FlowTimingWheel64 *wheel) {
    if (!wheel) {
        return;
    }
    memset(wheel, 0, sizeof(*wheel));
}

uint32_t flow_timing_wheel_schedule_staggered(FlowTimingWheel64 *wheel,
                                              uint32_t slot_buckets[64],
                                              uint64_t entity_id,
                                              uint32_t base_period_ticks) {
    if (!wheel || !slot_buckets) {
        return 0U;
    }

    /* Golden-ratio phase hash staggering across 64 slots */
    uint32_t phase = (uint32_t)((entity_id * 0x9E3779B97F4A7C15ULL) >> 58U);
    uint32_t slot = (wheel->current_tick + base_period_ticks + phase) & 63U;

    slot_buckets[slot]++;
    wheel->active_slot_mask |= (1ULL << slot);
    wheel->total_scheduled++;

    if (slot < 18U) {
        wheel->slot_counts[slot] = (uint16_t)(slot_buckets[slot] > 0xFFFFU ? 0xFFFFU : slot_buckets[slot]);
    }

    return slot;
}

uint32_t flow_timing_wheel_step_o1(FlowTimingWheel64 *wheel,
                                   uint32_t slot_buckets[64]) {
    if (!wheel || !slot_buckets) {
        return 0U;
    }

    /* Step current_tick cursor */
    wheel->current_tick++;
    uint32_t slot = wheel->current_tick & 63U;
    uint32_t fired = 0U;

    /* O(1) 1-cycle bit-test dispatch without polling the 100,000 entities */
    if ((wheel->active_slot_mask >> slot) & 1ULL) {
        fired = slot_buckets[slot];
        slot_buckets[slot] = 0U;
        wheel->active_slot_mask &= ~(1ULL << slot);

        if (slot < 18U) {
            wheel->slot_counts[slot] = 0U;
        }

        if (wheel->total_scheduled >= fired) {
            wheel->total_scheduled -= fired;
        } else {
            wheel->total_scheduled = 0U;
        }
        wheel->total_fired += fired;
    }

    /* Track peak and minimum items fired in any active tick */
    if (fired > 0U) {
        if (fired > wheel->max_fired_per_tick) {
            wheel->max_fired_per_tick = fired;
        }
        if (wheel->min_fired_per_tick == 0U || fired < wheel->min_fired_per_tick) {
            wheel->min_fired_per_tick = fired;
        }
    }

    return fired;
}

/*
 * ============================================================================
 * Subsystem 6: Unified Keybind + VR 6-DoF Spatial Intent & External C ABI
 * ============================================================================
 */
bool flow_input_resolve_keybind_chord(FlowInputIntent64 *intent,
                                      uint64_t required_switch_mask,
                                      uint64_t forbidden_switch_mask,
                                      uint64_t semantic_action_bit) {
    if (!intent) {
        return false;
    }

    uint64_t raw = intent->raw_device_chord_mask;
    /* 1-cycle bitwise ternary check: all required switches ON, all forbidden switches OFF */
    bool matched = ((raw & required_switch_mask) == required_switch_mask) &&
                   ((raw & forbidden_switch_mask) == 0ULL);
    if (matched) {
        intent->semantic_action_mask |= semantic_action_bit;
    }
    return matched;
}

FlowInputIntent64 flow_input_lift_keymouse_to_intent(uint64_t raw_key_mask,
                                                     int32_t cursor_x_q16,
                                                     int32_t cursor_y_q16,
                                                     int32_t reach_z_q16,
                                                     int32_t virtual_swing_vel_q16) {
    FlowInputIntent64 intent;
    memset(&intent, 0, sizeof(intent));

    intent.raw_device_chord_mask = raw_key_mask;
    intent.device_class          = FLOW_DEVICE_KEYMOUSE;
    intent.stereo_ipd_q16        = 0; /* 2D flat screen */
    intent.hand_pos_q16[0]       = cursor_x_q16;
    intent.hand_pos_q16[1]       = cursor_y_q16;
    intent.hand_pos_q16[2]       = reach_z_q16;

    /* Lift 0-Jet attack key press into a synthesized 2-Jet forward strike velocity */
    intent.hand_vel_q16[0]       = 0;
    intent.hand_vel_q16[1]       = 0;
    intent.hand_vel_q16[2]       = virtual_swing_vel_q16;
    intent.hand_acc_q16[2]       = virtual_swing_vel_q16 >> 1;

    return intent;
}

FlowHitZone flow_input_eval_spatial_interaction(FlowInputIntent64 *intent,
                                                const FlowPrefabDesc64 *target_prefab,
                                                int64_t min_lethal_speed_q16,
                                                int64_t dt_q16,
                                                FlowContactManifold64 *out_manifold) {
    if (!intent || !target_prefab || dt_q16 <= 0LL) {
        return FLOW_HIT_NONE;
    }

    /* Construct 3D 2-Jet Worldline from unified effector state (works for both VR & Keymouse!) */
    FlowJet2 hand_traj[3] = {
        flow_jet2_make(intent->hand_pos_q16[0], intent->hand_vel_q16[0], intent->hand_acc_q16[0]),
        flow_jet2_make(intent->hand_pos_q16[1], intent->hand_vel_q16[1], intent->hand_acc_q16[1]),
        flow_jet2_make(intent->hand_pos_q16[2], intent->hand_vel_q16[2], intent->hand_acc_q16[2])
    };

    FlowContactManifold64 local_manifold;
    FlowHitZone raw_zone = flow_prefab_eval_worldline_padded_jet2(
        target_prefab,
        hand_traj,
        FLOW_Q16_ONE / 8, /* delta_hurt: 0.125m core penetration */
        FLOW_Q16_ONE / 4, /* delta_graze: 0.25m tactile surface touch */
        FLOW_Q16_ONE / 2, /* delta_hit: 0.50m magnetic hover/lock-on */
        dt_q16,
        &local_manifold
    );

    /* Compute 1-Jet effector speed ||v_hand|| in Q16 for VR Anti-Waggle Momentum Gate */
    __extension__ typedef __int128 i128;
    __extension__ typedef unsigned __int128 u128;
    i128 vx = (i128)intent->hand_vel_q16[0];
    i128 vy = (i128)intent->hand_vel_q16[1];
    i128 vz = (i128)intent->hand_vel_q16[2];
    int64_t speed_q16 = (int64_t)flow_asm_isqrt_u128((u128)(vx * vx + vy * vy + vz * vz));

    /*
     * Anti-Waggle Gate:
     * If a VR controller penetrates the lethal core without sufficient physical swing speed
     * (speed_q16 < min_lethal_speed_q16), downgrade FLOW_HIT_LETHAL to a tactile surface touch
     * (FLOW_HIT_GRAZE) so wrist-waggling cannot cheese DPS!
     */
    if (raw_zone == FLOW_HIT_LETHAL && speed_q16 < min_lethal_speed_q16) {
        raw_zone = FLOW_HIT_GRAZE;
        local_manifold.hit_zone = (uint32_t)FLOW_HIT_GRAZE;
    }

    /* Synthesize continuous tactile / haptic output frequency proportional to interaction zone & speed */
    if (raw_zone == FLOW_HIT_LETHAL) {
        intent->haptic_freq_out_q16 = (uint32_t)(220LL * FLOW_Q16_ONE + (speed_q16 >> 2));
    } else if (raw_zone == FLOW_HIT_GRAZE) {
        intent->haptic_freq_out_q16 = (uint32_t)(110LL * FLOW_Q16_ONE);
    } else if (raw_zone == FLOW_HIT_LOCKED) {
        intent->haptic_freq_out_q16 = (uint32_t)(55LL * FLOW_Q16_ONE);
    } else {
        intent->haptic_freq_out_q16 = 0U;
    }

    if (out_manifold) {
        *out_manifold = local_manifold;
    }
    return raw_zone;
}

/*
 * ============================================================================
 * Subsystem 7: Zero-Framework Headless Server & 64-Byte UDP 2-Jet Protocol
 * ============================================================================
 */
static uint64_t flow_udp_calc_mac(const FlowUdpSnapshot64 *snap) {
    if (!snap) return 0ULL;
    const uint8_t *bytes = (const uint8_t *)snap;
    /* Hash first 56 bytes (excluding 8-byte state_mac at offset 56) */
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < 56U; ++i) {
        h ^= (uint64_t)bytes[i];
        h *= 0x100000001B3ULL;
    }
    h ^= (h >> 30U);
    h *= 0xBF58476D1CE4E5B9ULL;
    h ^= (h >> 27U);
    return h;
}

FlowUdpSnapshot64 flow_udp_pack_snapshot64(const FlowGameEngineState *engine,
                                           uint64_t sequence_id,
                                           uint32_t active_action_mask) {
    FlowUdpSnapshot64 snap;
    memset(&snap, 0, sizeof(snap));
    if (!engine) {
        return snap;
    }

    snap.traj_z               = engine->craft_traj[2];
    snap.lateral_xy_q16[0]    = flow_game_clamp_i32(engine->craft_traj[0].v);
    snap.lateral_xy_q16[1]    = flow_game_clamp_i32(engine->craft_traj[1].v);
    snap.lateral_vel_xy_q16[0]= flow_game_clamp_i32(engine->craft_traj[0].d1);
    snap.lateral_vel_xy_q16[1]= flow_game_clamp_i32(engine->craft_traj[1].d1);
    snap.sequence_id          = sequence_id;
    snap.server_tick          = (uint32_t)engine->frame_index;
    snap.active_action_mask   = active_action_mask;
    snap.state_mac            = flow_udp_calc_mac(&snap);
    return snap;
}

bool flow_udp_verify_snapshot64(const FlowUdpSnapshot64 *snap) {
    if (!snap) return false;
    return (snap->state_mac != 0ULL) && (snap->state_mac == flow_udp_calc_mac(snap));
}

int64_t flow_udp_dead_reckon_z_q16(const FlowUdpSnapshot64 *last_valid_snap,
                                   int64_t elapsed_since_snap_q16) {
    if (!last_valid_snap) return 0LL;
    if (elapsed_since_snap_q16 <= 0LL) return last_valid_snap->traj_z.v;
    /* Exact 2-Jet Taylor expansion z(t0 + dt) = z0 + vz0*dt + 0.5*az0*dt^2 */
    return flow_jet2_eval_taylor(last_valid_snap->traj_z, elapsed_since_snap_q16);
}

int flow_udp_open_loopback_pair(int *out_server_fd, int *out_client_fd, uint16_t *out_client_port) {
    if (!out_server_fd || !out_client_fd || !out_client_port) return -1;

    int s_fd = socket(AF_INET, SOCK_DGRAM, 0);
    int c_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_fd < 0 || c_fd < 0) {
        if (s_fd >= 0) close(s_fd);
        if (c_fd >= 0) close(c_fd);
        return -1;
    }

    /* Set non-blocking mode (O_NONBLOCK) for zero Head-of-Line blocking */
    (void)fcntl(s_fd, F_SETFL, fcntl(s_fd, F_GETFL, 0) | O_NONBLOCK);
    (void)fcntl(c_fd, F_SETFL, fcntl(c_fd, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in c_addr;
    memset(&c_addr, 0, sizeof(c_addr));
    c_addr.sin_family      = AF_INET;
    c_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    c_addr.sin_port        = htons(0); /* Ephemeral loopback port */

    if (bind(c_fd, (struct sockaddr *)&c_addr, sizeof(c_addr)) < 0) {
        close(s_fd);
        close(c_fd);
        return -1;
    }

    socklen_t len = sizeof(c_addr);
    if (getsockname(c_fd, (struct sockaddr *)&c_addr, &len) < 0) {
        close(s_fd);
        close(c_fd);
        return -1;
    }

    *out_server_fd   = s_fd;
    *out_client_fd   = c_fd;
    *out_client_port = ntohs(c_addr.sin_port);
    return 0;
}

int flow_udp_send_snapshot64(int server_fd, uint16_t client_port, const FlowUdpSnapshot64 *snap) {
    if (server_fd < 0 || !snap) return -1;

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family      = AF_INET;
    dst.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    dst.sin_port        = htons(client_port);

    ssize_t sent = sendto(server_fd, snap, sizeof(FlowUdpSnapshot64), 0,
                          (const struct sockaddr *)&dst, sizeof(dst));
    return (sent == (ssize_t)sizeof(FlowUdpSnapshot64)) ? 0 : -1;
}

bool flow_udp_recv_latest_snapshot64(int client_fd,
                                     uint64_t *inout_last_seq,
                                     FlowUdpSnapshot64 *out_latest_snap) {
    if (client_fd < 0 || !inout_last_seq || !out_latest_snap) return false;

    bool got_newer = false;
    FlowUdpSnapshot64 buf;

    /* Drain non-blocking UDP socket queue (with brief kernel loopback yield if netisr is in-flight) */
    for (int attempt = 0; attempt < 64; ++attempt) {
        for (;;) {
            ssize_t n = recvfrom(client_fd, &buf, sizeof(buf), 0, NULL, NULL);
            if (n != (ssize_t)sizeof(FlowUdpSnapshot64)) {
                break;
            }
            if (!flow_udp_verify_snapshot64(&buf)) {
                continue; /* Drop corrupted / forged datagram */
            }
            if (buf.sequence_id > *inout_last_seq) {
                *inout_last_seq  = buf.sequence_id;
                *out_latest_snap = buf;
                got_newer        = true;
            }
            /* Out-of-order or duplicate packets (sequence_id <= *inout_last_seq) are silently dropped! */
        }
        if (got_newer) {
            break;
        }
        sched_yield();
    }
    return got_newer;
}

int flow_game_run_headless_ticks(uint32_t num_ticks, FlowUdpSnapshot64 *out_final_snap) {
    FlowGameEngineState engine;
    flow_game_init(&engine);

    const int64_t dt_60hz_q16 = 1092LL; /* 1/60s in Q16 */
    for (uint32_t t = 0U; t < num_ticks; ++t) {
        (void)flow_game_step(&engine, dt_60hz_q16);
    }

    FlowUdpSnapshot64 snap = flow_udp_pack_snapshot64(&engine, (uint64_t)num_ticks, FLOW_ACTION_THRUST_BOOST);
    if (out_final_snap) {
        *out_final_snap = snap;
    }
    flow_game_destroy(&engine);
    return 0;
}

/*
 * ============================================================================
 * Subsystem 8: 64-Byte Zero-Downtime QSBR Hot-Patch & O(1) Lazy Sector
 *              Evaluator (FlowLazySector64)
 * ============================================================================
 */
static uint64_t flow_sector_calc_mac(const FlowLazySector64 *sec) {
    if (!sec) return 0ULL;
    const uint8_t *bytes = (const uint8_t *)sec;
    uint64_t h = 0x9E3779B97F4A7C15ULL;
    for (size_t i = 0; i < 56U; ++i) {
        h ^= (uint64_t)bytes[i];
        h *= 0x100000001B3ULL;
    }
    return h ^ (h >> 31U);
}

FlowLazySector64 flow_sector_make_patch64(uint64_t sector_id,
                                          uint32_t patch_version,
                                          uint32_t anchor_epoch_ms,
                                          FlowJet2 anchor_traj_z,
                                          uint32_t prefab_id,
                                          uint32_t art_style_id) {
    FlowLazySector64 sec;
    memset(&sec, 0, sizeof(sec));
    sec.anchor_traj_z       = anchor_traj_z;
    sec.sector_id           = sector_id;
    sec.patch_version       = patch_version;
    sec.anchor_epoch_ms     = anchor_epoch_ms;
    sec.prefab_id           = prefab_id;
    sec.art_style_id        = art_style_id;
    sec.resolved_lazy_layer = FLOW_STAGE_LAYER_HUD;
    sec.current_z_q16       = flow_game_clamp_i32(anchor_traj_z.v);
    sec.patch_mac           = flow_sector_calc_mac(&sec);
    return sec;
}

int flow_sector_qsbr_publish_patch(FlowGameEngineState *engine,
                                   const FlowLazySector64 *patch_bank) {
    if (!engine || !engine->qsbr_ctx || !patch_bank) {
        return -1;
    }
    if (patch_bank->patch_mac == 0ULL || patch_bank->patch_mac != flow_sector_calc_mac(patch_bank)) {
        return -1; /* Reject corrupted 64B patch */
    }
    /* Atomic lock-free QSBR pointer swap (0ms downtime, 0 mutexes) */
    return flow_qsbr_publish_ptr(engine->qsbr_ctx, (const void *)patch_bank);
}

bool flow_sector_qsbr_eval_lazy_o1(FlowGameEngineState *engine,
                                   uint32_t now_ms,
                                   int64_t player_z_q16,
                                   FlowLazySector64 *out_evaluated) {
    if (!engine || !engine->qsbr_ctx || !out_evaluated) {
        return false;
    }

    const FlowLazySector64 *active_patch = (const FlowLazySector64 *)flow_qsbr_load_ptr(
        engine->qsbr_ctx, &engine->qsbr_reader
    );
    if (!active_patch) {
        return false;
    }

    FlowLazySector64 eval = *active_patch;
    /* Advance QSBR quiescent state checkpoint after lock-free read */
    flow_qsbr_checkpoint(&engine->qsbr_reader);

    if (eval.patch_mac != flow_sector_calc_mac(&eval)) {
        return false;
    }

    /*
     * 1. O(1) Analytical Time-Skip Lazy Evaluation:
     *    Compute elapsed time dt_q16 = ((now_ms - anchor_epoch_ms) * 65536) / 1000
     *    and evaluate closed-form 2-Jet polynomial z(t) = z0 + v0*dt + 0.5*a0*dt^2
     *    in 1 step (zero tick loop even if player was away for 1,000 seconds!).
     */
    uint32_t elapsed_ms = (now_ms >= eval.anchor_epoch_ms) ? (now_ms - eval.anchor_epoch_ms) : 0U;
    int64_t dt_q16 = ((int64_t)elapsed_ms << FLOW_Q16_SHIFT) / 1000LL;
    int64_t z_now_q16 = flow_jet2_eval_taylor(eval.anchor_traj_z, dt_q16);
    eval.current_z_q16 = flow_game_clamp_i32(z_now_q16);

    /*
     * 2. Compute-Tier Lazy Dimensional Collapse:
     *    - dist > 1000m           => FLOW_STAGE_LAYER_HUD     (2D MSDF tactical icon only, 0% 3D cost)
     *    - dist <= 1000m          => FLOW_STAGE_LAYER_1_SLICE (2D analytic slice, 10% cost)
     *    - dist <= l0_radius_q16  => FLOW_STAGE_LAYER_0_SDF   (JIT full 3D CSG/SDF & luxury shader)
     *      where l0_radius_q16 is dynamically governed by FlowFrameGovernor64 (100m Ultra / 60m Fast).
     */
    int64_t dist_q16 = flow_q16_abs_sat(z_now_q16 - player_z_q16);
    int64_t l0_radius_q16 = (engine->fps_governor.layer0_radius_q16 > 0)
        ? (int64_t)engine->fps_governor.layer0_radius_q16
        : (100LL * FLOW_Q16_ONE);
    if (dist_q16 <= l0_radius_q16) {
        eval.resolved_lazy_layer = FLOW_STAGE_LAYER_0_SDF;
    } else if (dist_q16 <= (1000LL * FLOW_Q16_ONE)) {
        eval.resolved_lazy_layer = FLOW_STAGE_LAYER_1_SLICE;
    } else {
        eval.resolved_lazy_layer = FLOW_STAGE_LAYER_HUD;
    }

    eval.patch_mac = flow_sector_calc_mac(&eval);
    *out_evaluated = eval;
    return true;
}

/*
 * ============================================================================
 * Subsystem 9: 64-Byte Deterministic Savegame Capsule, Hex128 Clipboard Share
 *              Code & Driverless Anti-Cheat Replay Verifier (FlowSaveCapsule64)
 * ============================================================================
 */
static inline uint64_t flow_save_splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31U);
}

static uint64_t flow_save_calc_mac(const FlowSaveCapsule64 *capsule) {
    if (!capsule) {
        return 0ULL;
    }
    const uint8_t *bytes = (const uint8_t *)capsule;
    uint64_t h = 0x5A7ECAFEF1092026ULL;
    /* Hash first 56 bytes (excluding 8-byte state_mac at offset 56) */
    for (size_t i = 0U; i < 56U; ++i) {
        h ^= (uint64_t)bytes[i];
        h *= 0x100000001B3ULL;
    }
    h = flow_save_splitmix64(h);
    return (h == 0ULL) ? 1ULL : h;
}

static uint64_t flow_save_world_facts_bitmask(const FlowGameEngineState *engine) {
    if (!engine) {
        return 0ULL;
    }
    uint64_t mask = 0ULL;
    mask |= ((uint64_t)(engine->current_layer & 1U)) << 0U;
    mask |= ((uint64_t)(engine->thermal_shutdown ? 1U : 0U)) << 1U;
    mask |= ((uint64_t)(engine->contact_latched ? 1U : 0U)) << 2U;
    mask |= ((uint64_t)(engine->hardware_sieve != 0ULL ? 1U : 0U)) << 3U;
    mask |= (engine->graze_count & 0xFFFFULL) << 4U;
    mask |= (engine->ricardian.thermal_kelvin & 0xFFFFULL) << 20U;
    mask |= (engine->ricardian_epoch & 0x0FFFFFFFULL) << 36U;
    return mask;
}

uint64_t flow_save_init_hash_chain(uint64_t initial_seed) {
    uint64_t h = flow_save_splitmix64(initial_seed ^ 0xF1095A7ECAFEBABEULL);
    return (h == 0ULL) ? 1ULL : h;
}

uint64_t flow_save_chain_input(uint64_t prev_hash_chain,
                               uint32_t tick_index,
                               const FlowReplayInputCmd *cmd) {
    uint64_t h = flow_save_splitmix64(prev_hash_chain ^ ((uint64_t)tick_index * 0x9E3779B97F4A7C15ULL));
    if (!cmd) {
        return (h == 0ULL) ? 1ULL : h;
    }
    h = flow_save_splitmix64(h ^ (uint64_t)cmd->throttle_q16);
    h = flow_save_splitmix64(h ^ (uint64_t)cmd->pitch_rate_q16);
    h = flow_save_splitmix64(h ^ (uint64_t)cmd->yaw_rate_q16);
    h = flow_save_splitmix64(h ^ (uint64_t)cmd->roll_rate_q16);
    h = flow_save_splitmix64(h ^ (((uint64_t)cmd->toggle_layer << 32U) | (uint64_t)cmd->trigger_ricardian));
    h = flow_save_splitmix64(h ^ cmd->ricardian_Cost);
    h = flow_save_splitmix64(h ^ cmd->ricardian_Temp);
    return (h == 0ULL) ? 1ULL : h;
}

FlowSaveCapsule64 flow_save_pack_capsule64(const FlowGameEngineState *engine,
                                           uint64_t initial_seed,
                                           uint64_t input_hash_chain) {
    FlowSaveCapsule64 cap;
    memset(&cap, 0, sizeof(cap));
    if (!engine) {
        return cap;
    }
    cap.initial_seed        = initial_seed;
    cap.tick_count          = engine->frame_index;
    cap.world_facts_bitmask = flow_save_world_facts_bitmask(engine);
    cap.ricardian_vault     = engine->ricardian.m0_vault;
    cap.final_craft_z_q16   = engine->craft_traj[2].v;
    cap.final_craft_vz_q16  = engine->craft_traj[2].d1;
    cap.input_hash_chain    = input_hash_chain;
    cap.state_mac           = flow_save_calc_mac(&cap);
    return cap;
}

bool flow_save_verify_capsule64(const FlowSaveCapsule64 *capsule) {
    if (!capsule || capsule->state_mac == 0ULL) {
        return false;
    }
    return capsule->state_mac == flow_save_calc_mac(capsule);
}

bool flow_save_restore_engine(FlowGameEngineState *engine,
                              const FlowSaveCapsule64 *capsule) {
    if (!engine || !flow_save_verify_capsule64(capsule)) {
        return false;
    }
    engine->frame_index              = capsule->tick_count;
    engine->current_layer            = (uint32_t)(capsule->world_facts_bitmask & 1ULL);
    engine->stage_mask               = capsule->world_facts_bitmask & 1ULL;
    engine->thermal_shutdown         = (uint32_t)((capsule->world_facts_bitmask >> 1U) & 1ULL);
    engine->contact_latched          = (uint32_t)((capsule->world_facts_bitmask >> 2U) & 1ULL);
    engine->contact_skin_mask        = (uint64_t)engine->contact_latched;
    engine->last_contact.contact_latched = engine->contact_latched;
    engine->hardware_sieve           = ((capsule->world_facts_bitmask >> 3U) & 1ULL)
                                           ? FLOW_ASM_SIEVE_ALL_SOUND
                                           : 0ULL;
    engine->graze_count              = (capsule->world_facts_bitmask >> 4U) & 0xFFFFULL;
    engine->ricardian.thermal_kelvin = (capsule->world_facts_bitmask >> 20U) & 0xFFFFULL;
    engine->ricardian_epoch          = (capsule->world_facts_bitmask >> 36U) & 0x0FFFFFFFULL;
    engine->ricardian.m0_vault       = capsule->ricardian_vault;
    engine->craft_traj[2].v          = capsule->final_craft_z_q16;
    engine->craft_traj[2].d1         = capsule->final_craft_vz_q16;
    engine->prev_craft_traj[2]       = engine->craft_traj[2];
    if (engine->qsbr_ctx) {
        flow_qsbr_publish_ptr(engine->qsbr_ctx, &engine->stage_poly);
    }
    return true;
}

void flow_save_encode_hex128(const FlowSaveCapsule64 *capsule, char out_hex[129]) {
    flow_capsule64_encode_hex128(capsule, out_hex);
}

bool flow_save_decode_hex128(const char hex[129], FlowSaveCapsule64 *out_capsule) {
    if (!hex || !out_capsule) {
        return false;
    }
    if (hex[128] != '\0') {
        memset(out_capsule, 0, sizeof(*out_capsule));
        return false;
    }
    FlowSaveCapsule64 candidate;
    memset(&candidate, 0, sizeof(candidate));
    if (!flow_capsule64_decode_hex128(hex, &candidate)) {
        memset(out_capsule, 0, sizeof(*out_capsule));
        return false;
    }
    if (!flow_save_verify_capsule64(&candidate)) {
        memset(out_capsule, 0, sizeof(*out_capsule));
        return false;
    }
    *out_capsule = candidate;
    return true;
}

static void flow_save_apply_seed(FlowGameEngineState *engine, uint64_t initial_seed) {
    if (!engine || initial_seed == 0ULL) {
        return;
    }
    uint64_t s0 = flow_save_splitmix64(initial_seed);
    int64_t z_offset_q16 = (int64_t)(s0 & 0xFFULL) << FLOW_Q16_SHIFT;
    engine->celestial_pos_q16[2] += z_offset_q16;
    engine->ricardian.m0_vault   += (s0 >> 8U) & 0x3FFULL;
}

bool flow_save_run_recorded_session(uint64_t initial_seed,
                                    const FlowReplayInputCmd *inputs,
                                    uint32_t num_ticks,
                                    FlowSaveCapsule64 *out_capsule) {
    if (!out_capsule || (num_ticks > 0U && !inputs)) {
        return false;
    }
    FlowGameEngineState sim;
    if (flow_game_init(&sim) != 0) {
        return false;
    }
    flow_save_apply_seed(&sim, initial_seed);

    uint64_t hash_chain = flow_save_init_hash_chain(initial_seed);
    const int64_t dt_q16 = 1092LL; /* Deterministic ~60 Hz fixed tick */

    for (uint32_t t = 0U; t < num_ticks; ++t) {
        const FlowReplayInputCmd *cmd = &inputs[t];
        hash_chain = flow_save_chain_input(hash_chain, t, cmd);

        flow_game_input(
            &sim,
            cmd->throttle_q16,
            cmd->pitch_rate_q16,
            cmd->yaw_rate_q16,
            cmd->roll_rate_q16,
            cmd->toggle_layer != 0U
        );
        if (cmd->trigger_ricardian != 0U) {
            flow_game_step_ricardian(
                &sim,
                200000ULL,
                500ULL,
                cmd->ricardian_Cost,
                30ULL,
                cmd->ricardian_Temp
            );
        }
        if (flow_game_step(&sim, dt_q16) != 0) {
            flow_game_destroy(&sim);
            return false;
        }
    }

    *out_capsule = flow_save_pack_capsule64(&sim, initial_seed, hash_chain);
    flow_game_destroy(&sim);
    return true;
}

bool flow_save_audit_replay_anticheat(const FlowSaveCapsule64 *claimed_capsule,
                                      const FlowReplayInputCmd *recorded_inputs,
                                      uint32_t num_ticks) {
    if (!flow_save_verify_capsule64(claimed_capsule)) {
        return false;
    }
    if (claimed_capsule->tick_count != (uint64_t)num_ticks) {
        return false;
    }
    if (num_ticks > 0U && !recorded_inputs) {
        return false;
    }

    FlowSaveCapsule64 replayed;
    memset(&replayed, 0, sizeof(replayed));
    if (!flow_save_run_recorded_session(
            claimed_capsule->initial_seed,
            recorded_inputs,
            num_ticks,
            &replayed)) {
        return false;
    }

    /*
     * Driverless Anti-Cheat Verification:
     * Every deterministic output (input_hash_chain, ricardian_vault,
     * final_craft_z_q16, final_craft_vz_q16, world_facts_bitmask, state_mac)
     * must match the authoritative replay down to the last bit.
     * Any Cheat Engine memory edit (even with a forged MAC) is caught!
     */
    return (replayed.initial_seed        == claimed_capsule->initial_seed) &&
           (replayed.tick_count          == claimed_capsule->tick_count) &&
           (replayed.world_facts_bitmask == claimed_capsule->world_facts_bitmask) &&
           (replayed.ricardian_vault     == claimed_capsule->ricardian_vault) &&
           (replayed.final_craft_z_q16   == claimed_capsule->final_craft_z_q16) &&
           (replayed.final_craft_vz_q16  == claimed_capsule->final_craft_vz_q16) &&
           (replayed.input_hash_chain    == claimed_capsule->input_hash_chain) &&
           (replayed.state_mac           == claimed_capsule->state_mac);
}

/*
 * ============================================================================
 * Subsystem 10: 120 FPS (8.33 ms) Dual-Threshold Frame-Time Hysteresis Governor
 *               (FlowFrameGovernor64)
 * ============================================================================
 */
static uint64_t flow_fps_governor_calc_mac(const FlowFrameGovernor64 *gov) {
    if (!gov) {
        return 0ULL;
    }
    const uint8_t *bytes = (const uint8_t *)gov;
    uint64_t h = 0x120F958333CAFE01ULL;
    /* Hash first 56 bytes (excluding 8-byte governor_mac at offset 56) */
    for (size_t i = 0U; i < 56U; ++i) {
        h ^= (uint64_t)bytes[i];
        h *= 0x100000001B3ULL;
    }
    h = flow_save_splitmix64(h);
    return (h == 0ULL) ? 1ULL : h;
}

void flow_fps_governor_init(FlowGameEngineState *engine) {
    if (!engine) {
        return;
    }
    FlowStagePoly_init(&engine->fps_governor_poly);
    engine->fps_governor_poly.A[0][0] = 1;
    engine->fps_governor_poly.A[0][1] = 0;
    engine->fps_governor_poly.A[0][2] = 0;
    FlowStagePoly_set_hysteresis(
        &engine->fps_governor_poly,
        0U,
        (int64_t)FLOW_FPS120_THROTTLE_ENTER_US, /* 7800 us */
        (int64_t)FLOW_FPS120_THROTTLE_EXIT_US   /* 6000 us */
    );

    memset(&engine->fps_governor, 0, sizeof(engine->fps_governor));
    engine->fps_governor.frame_index        = 0ULL;
    engine->fps_governor.violated_mask      = 0ULL;
    engine->fps_governor.measured_frame_us  = 4000U;
    engine->fps_governor.ema_frame_us       = 4000U;
    engine->fps_governor.target_budget_us   = FLOW_FPS120_TARGET_BUDGET_US;
    engine->fps_governor.throttle_enter_us  = FLOW_FPS120_THROTTLE_ENTER_US;
    engine->fps_governor.throttle_exit_us   = FLOW_FPS120_THROTTLE_EXIT_US;
    engine->fps_governor.quality_throttled  = 0U;
    engine->fps_governor.raymarch_max_steps = FLOW_FPS120_RAYMARCH_STEPS_ULTRA;
    engine->fps_governor.layer0_radius_q16  = FLOW_FPS120_LAYER0_R_ULTRA_Q16;
    engine->fps_governor.transition_count   = 0U;
    engine->fps_governor.cubical_verified   = 1U;
    engine->fps_governor.governor_mac       = flow_fps_governor_calc_mac(&engine->fps_governor);
}

void flow_fps_governor_set_mode(FlowGameEngineState *engine,
                                uint32_t target_fps,
                                bool vsync_enabled) {
    if (!engine) {
        return;
    }
    uint32_t budget_us = FLOW_FPS120_TARGET_BUDGET_US;
    if (target_fps == 30U) {
        budget_us = FLOW_FPS30_TARGET_BUDGET_US;   /* 33,333 us */
    } else if (target_fps == 60U) {
        budget_us = FLOW_FPS60_TARGET_BUDGET_US;   /* 16,666 us */
    } else if (target_fps == 120U) {
        budget_us = FLOW_FPS120_TARGET_BUDGET_US;  /*  8,333 us */
    } else {
        budget_us = FLOW_FPS_UNCAPPED_BUDGET_US;   /*  4,166 us (240+ FPS Uncapped) */
    }

    uint32_t enter_us = (budget_us * 93U) / 100U;
    uint32_t exit_us  = (budget_us * 72U) / 100U;

    FlowStagePoly_set_hysteresis(
        &engine->fps_governor_poly,
        0U,
        (int64_t)enter_us,
        (int64_t)exit_us
    );

    engine->fps_governor.target_budget_us  = budget_us;
    engine->fps_governor.throttle_enter_us = enter_us;
    engine->fps_governor.throttle_exit_us  = exit_us;
    engine->fps_governor.cubical_verified  = vsync_enabled ? 1U : 2U;
    engine->fps_governor.governor_mac      = flow_fps_governor_calc_mac(&engine->fps_governor);
}

uint32_t flow_game_step_fixed_accumulator(FlowGameEngineState *engine,
                                          uint32_t elapsed_wall_us,
                                          uint32_t *inout_accum_us,
                                          int32_t *out_render_alpha_q16) {
    if (!engine || !inout_accum_us) {
        if (out_render_alpha_q16) {
            *out_render_alpha_q16 = 0;
        }
        return 0U;
    }

    /* Clamp single-frame wall delta to 8 fixed ticks to prevent spiral-of-death */
    uint32_t max_accum = FLOW_FIXED_PHYSICS_DT_US * 8U;
    uint64_t next_accum = (uint64_t)(*inout_accum_us) + (uint64_t)elapsed_wall_us;
    if (next_accum > (uint64_t)max_accum) {
        next_accum = (uint64_t)max_accum;
    }

    uint32_t accum = (uint32_t)next_accum;
    uint32_t ticks_executed = 0U;

    /* Fixed-Tick Authoritative Physics Loop (decoupled from variable render FPS) */
    while (accum >= FLOW_FIXED_PHYSICS_DT_US) {
        (void)flow_game_step(engine, FLOW_Q16_ONE);
        accum -= FLOW_FIXED_PHYSICS_DT_US;
        ticks_executed++;
    }

    *inout_accum_us = accum;
    if (out_render_alpha_q16) {
        *out_render_alpha_q16 = (int32_t)(((uint64_t)accum << 16U) / (uint64_t)FLOW_FIXED_PHYSICS_DT_US);
    }
    return ticks_executed;
}

FlowFrameGovernor64 flow_game_step_fps_governor(FlowGameEngineState *engine,
                                                uint32_t measured_frame_us) {
    FlowFrameGovernor64 fallback;
    memset(&fallback, 0, sizeof(fallback));
    if (!engine) {
        return fallback;
    }

    int32_t frame_us_i32 = (measured_frame_us > (uint32_t)INT32_MAX)
        ? INT32_MAX
        : (int32_t)measured_frame_us;
    int32_t x[3] = { frame_us_i32, 0, 0 };

    uint64_t prev_mask = engine->fps_governor.violated_mask & 1ULL;
    FlowQPolyEval ev = FlowStagePoly_eval_hysteresis(
        &engine->fps_governor_poly,
        x,
        prev_mask
    );

    uint64_t target_mask = ev.violated_mask & 1ULL;
    if (target_mask != prev_mask) {
        uint64_t next_mask = 0ULL;
        FlowKanStatus kan = flow_cubical_step_neighbor(
            prev_mask, 0U, 0ULL, &next_mask
        );
        if (kan == FLOW_KAN_FILLED_HOMOTOPIC) {
            engine->fps_governor.violated_mask     = next_mask & 1ULL;
            engine->fps_governor.quality_throttled = (next_mask & 1ULL) ? 1U : 0U;
            engine->fps_governor.transition_count++;
            engine->fps_governor.cubical_verified  = 1U;
        }
    }

    engine->fps_governor.frame_index++;
    engine->fps_governor.measured_frame_us = measured_frame_us;
    engine->fps_governor.ema_frame_us = (uint32_t)(
        ((uint64_t)engine->fps_governor.ema_frame_us * 3ULL + (uint64_t)measured_frame_us) >> 2U
    );

    if (engine->fps_governor.quality_throttled != 0U) {
        engine->fps_governor.raymarch_max_steps = FLOW_FPS120_RAYMARCH_STEPS_FAST;
        engine->fps_governor.layer0_radius_q16  = FLOW_FPS120_LAYER0_R_FAST_Q16;
    } else {
        engine->fps_governor.raymarch_max_steps = FLOW_FPS120_RAYMARCH_STEPS_ULTRA;
        engine->fps_governor.layer0_radius_q16  = FLOW_FPS120_LAYER0_R_ULTRA_Q16;
    }

    engine->fps_governor.governor_mac = flow_fps_governor_calc_mac(&engine->fps_governor);
    return engine->fps_governor;
}

bool flow_fps_governor_verify(const FlowFrameGovernor64 *gov) {
    if (!gov || gov->governor_mac == 0ULL) {
        return false;
    }
    return gov->governor_mac == flow_fps_governor_calc_mac(gov);
}

