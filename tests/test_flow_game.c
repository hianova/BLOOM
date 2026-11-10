/*
 * ============================================================================
 * BLOOM v1.0.0: Deterministic C17 Verification Suite (Zero Float/Double)
 * ============================================================================
 * Verification Gates (13 Gates):
 *   Test  1: Relativistic worldline anti-tunneling (10,000 cases, 0 false negatives)
 *   Test  2: Forced-perspective stage handoff & dual-threshold hysteresis
 *   Test  3: Wave64 topological entropy sparse ballot (>80% tile culling)
 *   Test  4: Ricardian v4 64-byte univalence, Harberger 70/30, AMM, thermal gate
 *   Test  5: Concentric SDF hitbox padding, contact hysteresis & action feel
 *   Test  6: Unified relative scale interface (FlowRelativeScale64)
 *   Test  7: Einstein elevator thought experiment (co-moving & equivalence)
 *   Test  8: FlowTimingWheel64 100,000 concurrent entities zero-jitter
 *   Test  9: Unified keybind chord, VR 6-DoF spatial intent & anti-waggle gate
 *   Test 10: Zero-framework 64B UDP loopback, out-of-order drop & 2-Jet dead reckoning
 *   Test 11: 64B zero-downtime QSBR hot-patch & O(1) time-skip lazy evaluation
 *   Test 12: 64B deterministic save capsule, Hex128 share code & anti-cheat replay
 *   Test 13: 120 FPS (8.33ms) dual-threshold frame-time hysteresis governor
 * ============================================================================
 */

#include "bloom.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Simple deterministic pseudo-random generator for repeatable test cases */
static inline uint64_t test_xorshift64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/*
 * Test 1: Relativistic Worldline Anti-Tunneling Collision
 *
 * Verifies that flow_jet2_solve_root catches thin/small sphere intersections
 * that discrete endpoint sampling misses, achieving 0 false negatives across
 * 10,000 high-speed penetration scenarios.
 */
static void test_worldline_anti_tunneling(void) {
    printf("[Test 1/13] Relativistic Worldline Anti-Tunneling Verification...\n");

    const size_t NUM_CASES = 10000U;
    size_t endpoint_miss_count = 0U;
    size_t jet_caught_count = 0U;
    uint64_t rng = 0x123456789ABCDEFULL;
    int64_t dt_q16 = FLOW_Q16_ONE; /* dt = 1.0 */

    for (size_t i = 0; i < NUM_CASES; ++i) {
        /*
         * Construct a scenario where craft traverses straight through a small sphere:
         * Sphere center at (0, 0, 0), radius R = [10..30] Q16.
         * Craft moves along Z axis with high velocity V_z = [300..800] Q16.
         * Target penetration time t_enter in [0.15, 0.60] dt.
         * Craft starts before sphere: z(0) = -R - V_z * t_enter < -R (outside).
         * Craft ends past sphere: z(dt) = z(0) + V_z * dt > +R (outside).
         */
        int64_t radius_q16 = (int64_t)(10ULL + (test_xorshift64(&rng) % 20ULL)) << FLOW_Q16_SHIFT;
        int64_t vz_q16 = (int64_t)(300ULL + (test_xorshift64(&rng) % 500ULL)) << FLOW_Q16_SHIFT;

        /* Target penetration time t_enter in [15%..60%] of dt */
        int64_t enter_pct = (int64_t)(15ULL + (test_xorshift64(&rng) % 45ULL));
        int64_t target_t_q16 = (dt_q16 * enter_pct) / 100LL;

        __extension__ typedef __int128 i128;
        int64_t travel_before = (int64_t)(((i128)vz_q16 * (i128)target_t_q16) >> FLOW_Q16_SHIFT);
        int64_t start_z = -radius_q16 - travel_before;

        FlowJet2 traj[3];
        traj[0] = flow_jet2_make(0LL, 0LL, 0LL);
        traj[1] = flow_jet2_make(0LL, 0LL, 0LL);
        traj[2] = flow_jet2_make(start_z, vz_q16, 0LL);

        int64_t center_q16[3] = {0LL, 0LL, 0LL};

        /* 1. Discrete endpoint sampling at t=0 and t=dt */
        int64_t end_z = flow_jet2_eval_taylor(traj[2], dt_q16);
        int64_t dist0 = (traj[2].v < 0) ? -traj[2].v : traj[2].v;
        int64_t dist1 = (end_z < 0) ? -end_z : end_z;
        bool endpoint_detected = (dist0 <= radius_q16) || (dist1 <= radius_q16);
        if (!endpoint_detected) {
            endpoint_miss_count++;
        }

        /* 2. Algebraic Worldline Anti-Tunneling via FlowJet2 Root Solver */
        int64_t t_hit_q16 = 0LL;
        bool jet_detected = flow_game_check_worldline_collision(
            traj, center_q16, radius_q16, dt_q16, &t_hit_q16
        );

        if (jet_detected) {
            jet_caught_count++;
            assert(t_hit_q16 > 0 && t_hit_q16 <= dt_q16);

            /* Verify that at t_hit, the distance to center equals sphere radius (within Q16 eps) */
            int64_t z_at_hit = flow_jet2_eval_taylor(traj[2], t_hit_q16);
            int64_t dist_at_hit = (z_at_hit < 0) ? -z_at_hit : z_at_hit;
            int64_t err = dist_at_hit - radius_q16;
            if (err < 0) err = -err;
            assert(err <= (1LL << (FLOW_Q16_SHIFT - 6))); /* < 1/64 pixel error */
        }
    }

    uint64_t miss_pct_x100 = (uint64_t)endpoint_miss_count * 10000ULL / (uint64_t)NUM_CASES;
    printf("  ✓ Tested %zu relativistic penetration cases\n", NUM_CASES);
    printf("  ✓ Discrete endpoint sampling missed : %zu / %zu (%llu.%02llu%% tunneling failure!)\n",
           endpoint_miss_count, NUM_CASES,
           (unsigned long long)(miss_pct_x100 / 100ULL),
           (unsigned long long)(miss_pct_x100 % 100ULL));
    printf("  ✓ FlowJet2 anti-tunneling caught    : %zu / %zu (0 false negatives!)\n",
           jet_caught_count, NUM_CASES);
    assert(jet_caught_count == NUM_CASES);
    assert(endpoint_miss_count > 9000U); /* Discrete sampling fails >90% of thin intersections */

    /* Differential Taylor self-checking verification */
    FlowJet2 j0 = flow_jet2_make(100LL << 16, 50LL << 16, 2LL << 16);
    FlowJet2 j1 = flow_jet2_make(
        flow_jet2_eval_taylor(j0, FLOW_Q16_ONE),
        (50LL + 2LL) << 16,
        2LL << 16
    );
    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
    sieve = flow_asm_jet2_self_check_step(j0, j1, FLOW_Q16_ONE, 64LL, 64LL, &sieve);
    assert((sieve & (FLOW_ASM_SIEVE_JET2_TAYLOR_0 | FLOW_ASM_SIEVE_JET2_TAYLOR_1)) ==
           (FLOW_ASM_SIEVE_JET2_TAYLOR_0 | FLOW_ASM_SIEVE_JET2_TAYLOR_1));
    printf("  ✓ Differential 2-Jet hardware self-check step verified.\n");
}

/*
 * Test 2: Forced-Perspective Stage Handoff & Dual-Threshold Hysteresis
 *
 * Verifies that FlowStagePoly hysteresis (b_enter=10000, b_exit=8000) cleanly
 * switches between Layer 0 and Layer 1 without chattering in the deadband,
 * verified via flow_cubical_step_neighbor and QSBR snapshot publication.
 */
static void test_stage_handoff_hysteresis(void) {
    printf("[Test 2/13] Forced-Perspective Stage Handoff & Hysteresis Verification...\n");

    FlowGameEngineState engine;
    assert(flow_game_init(&engine) == 0);

    /* Initial state: z = 5000 -> Layer 0 */
    int64_t pos[3] = {0LL, 0LL, 5000LL << FLOW_Q16_SHIFT};
    uint32_t layer0 = flow_game_step_stage_handoff(&engine, pos);
    assert(layer0 == 0U);

    /* Move forward into deadband: z = 9000 (between 8000 and 10000) -> stays Layer 0 */
    pos[2] = 9000LL << FLOW_Q16_SHIFT;
    uint32_t layer_deadband_0 = flow_game_step_stage_handoff(&engine, pos);
    assert(layer_deadband_0 == 0U);

    /* Cross enter threshold: z = 10500 (> 10000) -> transitions to Layer 1 */
    pos[2] = 10500LL << FLOW_Q16_SHIFT;
    uint32_t layer1 = flow_game_step_stage_handoff(&engine, pos);
    assert(layer1 == 1U);

    /* Reverse back into deadband: z = 9000 -> stays Layer 1 (NO CHATTER!) */
    pos[2] = 9000LL << FLOW_Q16_SHIFT;
    uint32_t layer_deadband_1 = flow_game_step_stage_handoff(&engine, pos);
    assert(layer_deadband_1 == 1U);

    /* Cross exit threshold: z = 7500 (< 8000) -> transitions back to Layer 0 */
    pos[2] = 7500LL << FLOW_Q16_SHIFT;
    uint32_t layer_exit = flow_game_step_stage_handoff(&engine, pos);
    assert(layer_exit == 0U);

    /* Verify lock-free QSBR reader snapshot load */
    const FlowStagePoly *poly_snap = (const FlowStagePoly *)flow_qsbr_load_ptr(
        engine.qsbr_ctx, &engine.qsbr_reader
    );
    assert(poly_snap != NULL);
    assert(poly_snap == &engine.stage_poly);

    flow_game_destroy(&engine);
    printf("  ✓ FlowStagePoly dual-threshold hysteresis (10000 enter / 8000 exit) verified.\n");
    printf("  ✓ Deadband [8000, 10000] stability confirmed (0 chatter).\n");
    printf("  ✓ Lock-free QSBR stage poly publication verified.\n");
}

/*
 * Test 3: Wave64 Topological Entropy Sparse Ballot
 *
 * Verifies that flow_jet2_nondegenerate_ballot64 and flow_gpu_ballot_compact_wave64
 * achieve >80% tile culling on space scenes and exact index compaction.
 */
static void test_wave64_tile_culling(void) {
    printf("[Test 3/13] Wave64 Topological Entropy Sparse Culling Verification...\n");

    FlowGameEngineState engine;
    assert(flow_game_init(&engine) == 0);

    /* Cull viewport tiles */
    uint32_t active_count = flow_game_cull_tiles_wave64(&engine);
    uint32_t total_tiles = FLOW_GAME_TOTAL_TILES; /* 2304 */

    uint32_t culled_tiles = total_tiles - active_count;
    uint32_t cull_rate_x100 = (culled_tiles * 10000U) / total_tiles;
    printf("  ✓ Total Viewport Tiles   : %u (64x36 across 36 Wave64 groups)\n", total_tiles);
    printf("  ✓ Active Tiles Dispatched: %u\n", active_count);
    printf("  ✓ Culled Vacuum Tiles    : %u (%u.%02u%% culling rate)\n",
           culled_tiles, cull_rate_x100 / 100U, cull_rate_x100 % 100U);

    /* Must achieve >80% tile culling on space scenes */
    assert(cull_rate_x100 >= 8000U);
    assert(active_count <= 460U);

    /* Verify 1-cycle POPCNT + CTZ index compaction validity */
    uint32_t counted_from_ballots = 0U;
    for (uint32_t g = 0; g < FLOW_GAME_WAVE64_GROUPS; ++g) {
        uint64_t b = engine.wave64_ballots[g];
        counted_from_ballots += (uint32_t)__builtin_popcountll(b);
    }
    assert(counted_from_ballots == active_count);

    /* Verify each compacted index is strictly monotonically increasing */
    for (uint32_t i = 1; i < active_count; ++i) {
        assert(engine.active_tile_indices[i] > engine.active_tile_indices[i - 1]);
        assert(engine.active_tile_indices[i] < total_tiles);
    }

    flow_game_destroy(&engine);
    printf("  ✓ Wave64 POPCNT+CTZ 1-cycle compaction verified without loss or duplicate.\n");
}

/*
 * Test 4: Ricardian v4 64-Byte State Univalence & Conservation Laws
 *
 * Verifies:
 *   1. 64-byte alignment and univalence between RicardianStateV4 and RicardianShadowStateV4.
 *   2. Harberger 70/30 tax split conservation via flow_asm_qadd_i64 across 1,000 cases.
 *   3. Assembly Index depth monotonicity.
 *   4. AMM reserve floor (reserve B never depletes).
 *   5. Thermal meltdown gate (800K enter / 760K exit) hysteresis.
 */
static void test_ricardian_v4_laws(void) {
    printf("[Test 4/13] Ricardian v4 64-Byte State & Conservation Laws Verification...\n");

    /* 1. 64-Byte Alignment & Univalence */
    assert(sizeof(RicardianStateV4) == 64U);
    assert(sizeof(RicardianShadowStateV4) == 64U);
    assert(_Alignof(RicardianStateV4) == 64U);

    FlowGameEngineState engine;
    assert(flow_game_init(&engine) == 0);

    /* 2. Harberger 70/30 Tax Conservation across 1,000 cases */
    uint64_t rng = 0xDEADBEEFCAFEULL;
    for (size_t i = 0; i < 1000U; ++i) {
        uint64_t valuation = 10000ULL + (test_xorshift64(&rng) % 10000000ULL);
        uint64_t rate_bps = 100ULL + (test_xorshift64(&rng) % 2000ULL);

        uint64_t tax = ricardian_harberger_tax(valuation, rate_bps);
        uint64_t div = ricardian_harberger_div(tax);
        uint64_t burn = ricardian_harberger_burn(tax);

        uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
        FlowResultU64 sum = flow_asm_qadd_i64((int64_t)div, (int64_t)burn, &sieve);
        assert(sum.boundary_tag == 0ULL);
        assert(sum.value == tax);
        assert(div + burn == tax);
    }
    printf("  ✓ Harberger 70/30 conservation verified across 1,000 test cases (0 overflow).\n");

    /* 3. Assembly Index depth monotonicity */
    uint64_t d0 = engine.ricardian.assembly_depth;
    uint64_t d1 = ricardian_join_assembly(d0, 2ULL, 0ULL);
    assert(d1 > d0);
    printf("  ✓ Assembly Index depth monotonicity verified: %llu -> %llu.\n",
           (unsigned long long)d0, (unsigned long long)d1);

    /* 4. AMM reserve floor (constant product never drops to zero) */
    uint64_t r_b = engine.ricardian.reserve_b;
    for (size_t swap = 0; swap < 100U; ++swap) {
        r_b = ricardian_amm_after_swap(engine.ricardian.reserve_a, r_b, 500000ULL, 30ULL);
        assert(r_b >= 1ULL);
    }
    printf("  ✓ AMM reserve floor verified: reserve B >= 1 sustained across 100 swaps.\n");

    /* 5. Thermal Meltdown Hysteresis (800K enter / 760K exit) */
    engine.ricardian.thermal_kelvin = 740ULL;
    engine.thermal_shutdown = 0U;

    /* Heat up to 790K: normal */
    flow_game_step_ricardian(&engine, 200000ULL, 500ULL, 0ULL, 0ULL, 50ULL);
    assert(engine.ricardian.thermal_kelvin == 790ULL);
    assert(engine.thermal_shutdown == 0U);

    /* Heat up to 810K: exceeds 800K -> meltdown shutdown triggers */
    flow_game_step_ricardian(&engine, 200000ULL, 500ULL, 0ULL, 0ULL, 20ULL);
    assert(engine.ricardian.thermal_kelvin == 810ULL);
    assert(engine.thermal_shutdown == 1U);

    /* Cool down to 780K: inside deadband [760, 800] -> stays in shutdown (NO CHATTER!) */
    engine.ricardian.thermal_kelvin = 780ULL;
    flow_game_step_ricardian(&engine, 200000ULL, 500ULL, 0ULL, 0ULL, 0ULL);
    assert(engine.thermal_shutdown == 1U);

    /* Cool down to 750K: drops below 760K -> shutdown clears */
    engine.ricardian.thermal_kelvin = 750ULL;
    flow_game_step_ricardian(&engine, 200000ULL, 500ULL, 0ULL, 0ULL, 0ULL);
    assert(engine.thermal_shutdown == 0U);

    flow_game_destroy(&engine);
    printf("  ✓ 800K Meltdown enter / 760K exit thermal hysteresis verified (0 chatter).\n");
}

/*
 * Test 5: Concentric SDF Hitbox Padding (+-delta), Contact Hysteresis & Action Feel
 *
 * Verifies:
 *   1. Concentric SDF level-set padding: FLOW_HIT_LETHAL, FLOW_HIT_GRAZE, FLOW_HIT_LOCKED, FLOW_HIT_NONE.
 *   2. Visual contact projection p_contact = p - f(p)*grad_f(p) onto f(x) = 0.
 *   3. Contact skin dual-threshold hysteresis [delta_enter, delta_exit] (zero bounce chatter).
 *   4. Hitstop time dilation, ~100ms Input Buffer auto-execution, and 2-Jet impulse kick.
 */
static void test_hitbox_padding_and_action_feel(void) {
    printf("[Test 5/13] Concentric SDF Hitbox Padding, Contact Hysteresis & Action Feel...\n");

    int64_t center_q16[3] = {0LL, 0LL, 0LL};
    int64_t radius_q16 = 20LL << FLOW_Q16_SHIFT;      /* R = 20m */
    int64_t delta_hurt_q16  = 3LL << FLOW_Q16_SHIFT;  /* Hurtbox shrunk by 3m -> R_hurt = 17m */
    int64_t delta_graze_q16 = 4LL << FLOW_Q16_SHIFT;  /* Graze shell expanded by 4m -> R_graze = 24m */
    int64_t delta_hit_q16   = 10LL << FLOW_Q16_SHIFT; /* Lock-on hitbox expanded by 10m -> R_hit = 30m */
    int64_t dt_q16 = FLOW_Q16_ONE;

    /* 1a. Lethal hit: passes through x = 10m <= R_hurt (17m) */
    FlowJet2 traj_lethal[3] = {
        flow_jet2_make(10LL << FLOW_Q16_SHIFT, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(-(50LL << FLOW_Q16_SHIFT), 100LL << FLOW_Q16_SHIFT, 0LL)
    };
    FlowContactManifold64 m_lethal;
    FlowHitZone z_lethal = flow_game_eval_worldline_padded(
        traj_lethal, center_q16, radius_q16,
        delta_hurt_q16, delta_graze_q16, delta_hit_q16, dt_q16, &m_lethal
    );
    assert(z_lethal == FLOW_HIT_LETHAL);
    assert(m_lethal.hit_zone == FLOW_HIT_LETHAL);

    /* 1b. Near-miss Graze: skims at x = 21m (outside R_hurt=17m, inside R_graze=24m) */
    FlowJet2 traj_graze[3] = {
        flow_jet2_make(21LL << FLOW_Q16_SHIFT, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(-(50LL << FLOW_Q16_SHIFT), 100LL << FLOW_Q16_SHIFT, 0LL)
    };
    FlowContactManifold64 m_graze;
    FlowHitZone z_graze = flow_game_eval_worldline_padded(
        traj_graze, center_q16, radius_q16,
        delta_hurt_q16, delta_graze_q16, delta_hit_q16, dt_q16, &m_graze
    );
    assert(z_graze == FLOW_HIT_GRAZE);
    assert(m_graze.graze_bonus > 0U);

    /* Verify visual contact projection p_contact = p - f(p)*grad_f(p) snaps onto R = 20m */
    __extension__ typedef unsigned __int128 u128;
    int64_t cx = m_graze.contact_pos_q16[0];
    int64_t cy = m_graze.contact_pos_q16[1];
    int64_t cz = m_graze.contact_pos_q16[2];
    u128 r_sq_u128 = (u128)((__int128)cx * cx + (__int128)cy * cy + (__int128)cz * cz);
    int64_t r_snap_q16 = (int64_t)flow_asm_isqrt_u128(r_sq_u128);
    int64_t r_err_q16 = r_snap_q16 - radius_q16;
    if (r_err_q16 < 0) r_err_q16 = -r_err_q16;
    assert(r_err_q16 <= 256LL); /* Snapped directly onto visual surface f(x)=0 within <1/256m */

    /* 1c. Expanded weapon lock-on hitbox: passes at x = 27m (inside R_hit=30m) */
    FlowJet2 traj_lock[3] = {
        flow_jet2_make(27LL << FLOW_Q16_SHIFT, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(-(50LL << FLOW_Q16_SHIFT), 100LL << FLOW_Q16_SHIFT, 0LL)
    };
    FlowContactManifold64 m_lock;
    FlowHitZone z_lock = flow_game_eval_worldline_padded(
        traj_lock, center_q16, radius_q16,
        delta_hurt_q16, delta_graze_q16, delta_hit_q16, dt_q16, &m_lock
    );
    assert(z_lock == FLOW_HIT_LOCKED);

    /* 2. Contact skin dual-threshold hysteresis [delta_enter=4m, delta_exit=24m] */
    FlowGameEngineState engine;
    assert(flow_game_init(&engine) == 0);
    assert(flow_game_step_contact_hysteresis(&engine, 30LL << FLOW_Q16_SHIFT) == 0U); /* Outside enter (>4m) */
    assert(flow_game_step_contact_hysteresis(&engine, 2LL << FLOW_Q16_SHIFT) == 1U);  /* Inside enter (<=4m) -> latch */
    assert(flow_game_step_contact_hysteresis(&engine, 12LL << FLOW_Q16_SHIFT) == 1U); /* Inside deadband [4m, 24m] -> stays latched! */
    assert(flow_game_step_contact_hysteresis(&engine, 30LL << FLOW_Q16_SHIFT) == 0U); /* Exceeds exit (>24m) -> detach */

    /* 3. Hitstop time dilation, ~100ms Input Buffer & 2-Jet Impulse Kick */
    flow_game_trigger_hitstop(&engine, 3U, 4096LL); /* 3 frames hitstop at 1/16x time scale */
    int64_t dv_q16[3] = {12LL << FLOW_Q16_SHIFT, -(8LL << FLOW_Q16_SHIFT), -(25LL << FLOW_Q16_SHIFT)};
    int64_t da_q16[3] = {0LL, 0LL, 0LL};
    flow_game_apply_impulse_jet2(&engine, dv_q16, da_q16);
    assert(engine.craft_traj[0].d1 == (12LL << FLOW_Q16_SHIFT));

    /* Player presses boost throttle (2.0x) and pitch during hitstop -> latched in ~100ms input buffer */
    int64_t boost_throttle = 2LL << FLOW_Q16_SHIFT;
    flow_game_input(&engine, boost_throttle, 32768LL, 0LL, 0LL, false);
    assert(engine.input_buffered == true);
    assert(engine.craft_throttle_q16 != boost_throttle); /* Not yet applied while frozen in hitstop */

    int64_t dt_frame = FLOW_Q16_ONE / 60LL;
    /* Step through 3 frames of hitstop: dt is dilated, input remains buffered until release */
    for (int f = 0; f < 3; ++f) {
        assert(flow_game_step(&engine, dt_frame) == 0);
        assert(engine.last_effective_dt_q16 < dt_frame);
    }
    /* Next step after hitstop expires: buffered input auto-executes on the exact release tick! */
    assert(flow_game_step(&engine, dt_frame) == 0);
    assert(engine.input_buffered == false);
    assert(engine.craft_throttle_q16 == boost_throttle);

    flow_game_destroy(&engine);
    printf("  ✓ Concentric SDF padding (Lethal / Graze / Locked) & surface normal snap verified.\n");
    printf("  ✓ Contact skin dual-threshold hysteresis [delta_enter, delta_exit] verified (0 jitter).\n");
    printf("  ✓ Hitstop time dilation, ~100ms input buffer release & 2-Jet impulse kick verified.\n");
}

/*
 * Test 6: Unified Relative Scale Interface (FlowRelativeScale64)
 *
 * Verifies that ONE single 64-byte descriptor & evaluator continuously spans:
 *   1. Galilean / Lorentz Relativity (Collision <-> Wind, Normal <-> Speed Force):
 *      craft moving at +0.9c in stationary medium == stationary craft in -0.9c wind!
 *   2. Metric Curvature & Optical Refraction (Water Ripple <-> Photon Sphere <-> Black Hole):
 *      weak Phi -> ripple refraction; r = 1.5*r_s -> Einstein ring; r <= r_s -> Event Horizon (grav_redshift = 0).
 *   3. Boundary Energy Flux (External Wind Compression <-> Super Saiyan Plasma Eruption):
 *      signed Pi_rel transitions continuously from inward wind compression (<0) to outward aura shockwave (>>1).
 */
static void test_unified_relative_scale_interface(void) {
    printf("[Test 6/13] Unified Relative Scale Interface (FlowRelativeScale64) Verification...\n");
    assert(sizeof(FlowRelativeScale64) == 64U);
    assert(_Alignof(FlowRelativeScale64) == 64U);

    int64_t c_eff = 1000LL << FLOW_Q16_SHIFT;
    int64_t v_90c = 900LL << FLOW_Q16_SHIFT; /* 0.90c -> Speed Force relativistic regime */

    /* 1. Galilean / Lorentz Duality: Moving Craft vs. Moving Wind */
    FlowJet2 craft_moving[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(-(500LL << FLOW_Q16_SHIFT), v_90c, 0LL)
    };
    FlowJet2 medium_still[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL)
    };
    FlowJet2 craft_still[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(-(500LL << FLOW_Q16_SHIFT), 0LL, 0LL)
    };
    FlowJet2 medium_wind[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, -v_90c, 0LL)
    };

    FlowRelativeScale64 s_move, s_wind;
    flow_game_resolve_relative_scale(
        craft_moving, medium_still, c_eff, 10LL << FLOW_Q16_SHIFT,
        50LL << FLOW_Q16_SHIFT, 50LL << FLOW_Q16_SHIFT,
        1LL << FLOW_Q16_SHIFT, 100LL << FLOW_Q16_SHIFT, &s_move
    );
    flow_game_resolve_relative_scale(
        craft_still, medium_wind, c_eff, 10LL << FLOW_Q16_SHIFT,
        50LL << FLOW_Q16_SHIFT, 50LL << FLOW_Q16_SHIFT,
        1LL << FLOW_Q16_SHIFT, 100LL << FLOW_Q16_SHIFT, &s_wind
    );

    /* Exact relativistic equivalence between moving craft and blowing wind! */
    assert(s_move.lorentz_beta_q16 == s_wind.lorentz_beta_q16);
    assert(s_move.lorentz_gamma_q16 == s_wind.lorentz_gamma_q16);
    assert(s_move.doppler_factor_q16 == s_wind.doppler_factor_q16);
    assert(s_move.lorentz_gamma_q16 > (2LL << FLOW_Q16_SHIFT));  /* gamma(0.9c) ~ 2.294 */
    assert(s_move.doppler_factor_q16 > (4LL << FLOW_Q16_SHIFT)); /* Doppler(0.9c) ~ 4.358 */

    /* 2. Metric Potential Scale: Ripple (Phi ~ 0.01) -> Photon Sphere (r=1.5*r_s) -> Black Hole Horizon (r<=r_s) */
    int64_t rs_bh = 300LL << FLOW_Q16_SHIFT;
    FlowJet2 pos_photon_sphere[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(-(450LL << FLOW_Q16_SHIFT), 0LL, 0LL) /* r = 450 = 1.5 * r_s */
    };
    FlowJet2 pos_horizon[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(-(280LL << FLOW_Q16_SHIFT), 0LL, 0LL) /* r = 280 < r_s (Inside Event Horizon!) */
    };
    FlowRelativeScale64 s_photon, s_horizon;
    flow_game_resolve_relative_scale(
        pos_photon_sphere, medium_still, c_eff, rs_bh,
        50LL << FLOW_Q16_SHIFT, 50LL << FLOW_Q16_SHIFT,
        1LL << FLOW_Q16_SHIFT, 300LL << FLOW_Q16_SHIFT, &s_photon
    );
    flow_game_resolve_relative_scale(
        pos_horizon, medium_still, c_eff, rs_bh,
        50LL << FLOW_Q16_SHIFT, 50LL << FLOW_Q16_SHIFT,
        1LL << FLOW_Q16_SHIFT, 300LL << FLOW_Q16_SHIFT, &s_horizon
    );
    assert(s_photon.metric_potential_q16 > 21000 && s_photon.metric_potential_q16 < 22500); /* Phi = 1/3 */
    assert(s_photon.grav_redshift_q16 > 0);
    assert(s_horizon.metric_potential_q16 >= 32768); /* Phi >= 0.5 inside Event Horizon */
    assert(s_horizon.grav_redshift_q16 == 0);        /* Light cone trapped: redshift -> 0 */
    assert(s_horizon.tidal_stretch_q16 > (2LL << FLOW_Q16_SHIFT)); /* Spaghettification stretch */

    /* 3. Signed Boundary Flux Ratio: Wind Buffeting (<0) vs. Super Saiyan Aura Eruption (>>1) */
    FlowRelativeScale64 s_buffet, s_saiyan;
    flow_game_resolve_relative_scale(
        craft_still, medium_wind, c_eff, 10LL << FLOW_Q16_SHIFT,
        10LL << FLOW_Q16_SHIFT, 200LL << FLOW_Q16_SHIFT, /* External wind pressure dominates */
        1LL << FLOW_Q16_SHIFT, 100LL << FLOW_Q16_SHIFT, &s_buffet
    );
    flow_game_resolve_relative_scale(
        craft_still, medium_still, c_eff, 10LL << FLOW_Q16_SHIFT,
        1200LL << FLOW_Q16_SHIFT, 40LL << FLOW_Q16_SHIFT, /* Internal energy flux erupts! */
        1LL << FLOW_Q16_SHIFT, 100LL << FLOW_Q16_SHIFT, &s_saiyan
    );
    assert(s_buffet.flux_pressure_ratio_q16 < 0);
    assert(s_saiyan.flux_pressure_ratio_q16 > (10LL << FLOW_Q16_SHIFT));
    assert(s_saiyan.wave_phase_freq_q16 > (40LL << FLOW_Q16_SHIFT));

    /*
     * 4. 3D Einstein Velocity Addition & Universal Constancy of Light Speed c:
     *    - Subluminal closure: +0.6c vs -0.6c => u' = (0.6 + 0.6)/(1 + 0.36) c = (15/17) c ~ 0.88235c < c
     *      (Naive Galilean subtraction would give 1.2c > c!)
     *    - Light-cone invariance: A light beam (||u|| = c) observed from stationary (0),
     *      forward (+0.85c), backward (-0.85c), and transverse (+0.85c) frames always
     *      measures exact speed c (lorentz_beta_q16 == FLOW_Q16_ONE).
     */
    int64_t v_06c = 600LL << FLOW_Q16_SHIFT;
    FlowJet2 craft_pos_06c[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(-(500LL << FLOW_Q16_SHIFT), v_06c, 0LL)
    };
    FlowJet2 medium_neg_06c[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, -v_06c, 0LL)
    };
    FlowRelativeScale64 s_subluminal;
    flow_game_resolve_relative_scale(
        craft_pos_06c, medium_neg_06c, c_eff, 0LL,
        50LL << FLOW_Q16_SHIFT, 50LL << FLOW_Q16_SHIFT,
        1LL << FLOW_Q16_SHIFT, 100LL << FLOW_Q16_SHIFT, &s_subluminal
    );
    /* beta = 15/17 in Q16 is 57825; gamma = 17/8 = 2.125 in Q16 is 139264 */
    assert(s_subluminal.lorentz_beta_q16 >= 57820 && s_subluminal.lorentz_beta_q16 <= 57830);
    assert(s_subluminal.lorentz_gamma_q16 >= 139200 && s_subluminal.lorentz_gamma_q16 <= 139330);

    FlowJet2 light_beam_z[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(-(500LL << FLOW_Q16_SHIFT), c_eff, 0LL)
    };
    FlowJet2 obs_fwd_085c[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 850LL << FLOW_Q16_SHIFT, 0LL)
    };
    FlowJet2 obs_bwd_085c[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, -(850LL << FLOW_Q16_SHIFT), 0LL)
    };
    FlowJet2 obs_trans_085c[3] = {
        flow_jet2_make(0LL, 850LL << FLOW_Q16_SHIFT, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL)
    };
    FlowRelativeScale64 s_light_rest, s_light_fwd, s_light_bwd, s_light_trans;
    flow_game_resolve_relative_scale(
        light_beam_z, medium_still, c_eff, 0LL,
        50LL << FLOW_Q16_SHIFT, 50LL << FLOW_Q16_SHIFT,
        1LL << FLOW_Q16_SHIFT, 100LL << FLOW_Q16_SHIFT, &s_light_rest
    );
    flow_game_resolve_relative_scale(
        light_beam_z, obs_fwd_085c, c_eff, 0LL,
        50LL << FLOW_Q16_SHIFT, 50LL << FLOW_Q16_SHIFT,
        1LL << FLOW_Q16_SHIFT, 100LL << FLOW_Q16_SHIFT, &s_light_fwd
    );
    flow_game_resolve_relative_scale(
        light_beam_z, obs_bwd_085c, c_eff, 0LL,
        50LL << FLOW_Q16_SHIFT, 50LL << FLOW_Q16_SHIFT,
        1LL << FLOW_Q16_SHIFT, 100LL << FLOW_Q16_SHIFT, &s_light_bwd
    );
    flow_game_resolve_relative_scale(
        light_beam_z, obs_trans_085c, c_eff, 0LL,
        50LL << FLOW_Q16_SHIFT, 50LL << FLOW_Q16_SHIFT,
        1LL << FLOW_Q16_SHIFT, 100LL << FLOW_Q16_SHIFT, &s_light_trans
    );
    assert(s_light_rest.lorentz_beta_q16 == FLOW_Q16_ONE);
    assert(s_light_fwd.lorentz_beta_q16 == FLOW_Q16_ONE);
    assert(s_light_bwd.lorentz_beta_q16 == FLOW_Q16_ONE);
    assert(s_light_trans.lorentz_beta_q16 == FLOW_Q16_ONE);

    /*
     * 5. Real SI Vacuum Speed of Light (FLOW_C_LIGHT_VACUUM_Q16 = 299,792,458 m/s in Q48.16):
     *    Proves 128-bit overflow-free Lorentz velocity transformation when c^3 ~ 2^132.5 exceeds
     *    signed 128-bit range, preserving exact +c_vac / -c_vac sign and magnitude.
     */
    int64_t c_vac = FLOW_C_LIGHT_VACUUM_Q16;
    int64_t u_vac_pos[3] = { 0LL, 0LL, c_vac };
    int64_t u_vac_neg[3] = { 0LL, 0LL, -c_vac };
    int64_t v_vac_bwd07[3] = { 0LL, 0LL, -(c_vac / 10LL) * 7LL };
    int64_t v_vac_fwd085[3] = { 0LL, 0LL, (c_vac / 20LL) * 17LL };
    int64_t u_vac_out[3] = { 0LL, 0LL, 0LL };

    int64_t vac_norm_1 = flow_game_lorentz_rel_vel_q16(u_vac_pos, v_vac_bwd07, c_vac, u_vac_out);
    assert(vac_norm_1 == c_vac && u_vac_out[2] == c_vac);
    int64_t vac_norm_2 = flow_game_lorentz_rel_vel_q16(u_vac_neg, v_vac_fwd085, c_vac, u_vac_out);
    assert(vac_norm_2 == c_vac && u_vac_out[2] == -c_vac);

    FlowJet2 vac_light_z[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, c_vac, 0LL)
    };
    FlowJet2 vac_obs_bwd[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, v_vac_bwd07[2], 0LL)
    };
    FlowRelativeScale64 s_vac_light;
    flow_game_resolve_relative_scale(
        vac_light_z, vac_obs_bwd, c_vac, 0LL,
        50LL << FLOW_Q16_SHIFT, 50LL << FLOW_Q16_SHIFT,
        1LL << FLOW_Q16_SHIFT, 100LL << FLOW_Q16_SHIFT, &s_vac_light
    );
    assert(s_vac_light.lorentz_beta_q16 == FLOW_Q16_ONE);
    assert(s_vac_light.rel_vel_dir_q16[2] == FLOW_Q16_ONE);

    printf("  ✓ Galilean/Lorentz Duality (Collision <-> Wind, Speed Force gamma=%d.%02d, Doppler=%d.%02dx) verified.\n",
           s_move.lorentz_gamma_q16 >> 16, ((s_move.lorentz_gamma_q16 & 0xFFFF) * 100) >> 16,
           s_move.doppler_factor_q16 >> 16, ((s_move.doppler_factor_q16 & 0xFFFF) * 100) >> 16);
    printf("  ✓ 3D Einstein Velocity Addition (0.6c (+) 0.6c = 15/17 c) & Constancy of Light Speed (c=1000 & SI c=299,792,458 m/s) verified.\n");
    printf("  ✓ Metric Curvature Continuum (Ripple <-> Photon Sphere <-> Black Hole Event Horizon) verified.\n");
    printf("  ✓ Signed Boundary Flux (Wind Compression Pi<0 <-> Super Saiyan Plasma Eruption Pi>>1) verified.\n");
}

/*
 * Test 7: Einstein Elevator Thought Experiment (Co-Moving Invariance & Equivalence Principle)
 *
 * Verifies:
 *   1. Moving Elevator Invariance: A passenger inside a high-speed elevator (||v|| = 850 m/s = 0.85c)
 *      or free-falling elevator in gravity (a_elev = g = -9.8 m/s^2) experiences ZERO
 *      relative velocity, ZERO relative acceleration, ZERO proper G-force, and feels
 *      100% stationary (beta = 0, gamma = 1.0, Doppler = 1.0).
 *   2. Strong Equivalence Principle (Massive Ball & Relativistic Light Beam):
 *      - Frame A: Deep-space elevator cruising at v_y = 0.85c and accelerating upward at a = +9.8 m/s^2 (g = 0).
 *      - Frame B: Stationary elevator on a planet with gravity g = -9.8 m/s^2 (a = 0).
 *      Both a ball released at height h = 2.0m AND a horizontal light beam emitted at height h = 2.0m
 *      inside Frame A and Frame B yield bit-for-bit identical local 2-Jet trajectories, identical
 *      local light speed ||u'|| = c, identical gravitational light-beam deflection, and identical
 *      floor impact time t_hit via flow_jet2_solve_root!
 */
static void test_einstein_elevator_equivalence(void) {
    printf("[Test 7/13] Einstein Elevator Thought Experiment (Co-Moving & Equivalence)...\n");

    int64_t g_val  = -(98LL << FLOW_Q16_SHIFT) / 10LL; /* g = -9.8 m/s^2 in Q16 */
    int64_t v_high = 850LL << FLOW_Q16_SHIFT;          /* ||v|| = 850 m/s = 0.85c high-speed cruise */
    int64_t v_high_y = 510LL << FLOW_Q16_SHIFT;        /* 3-4-5 Pythagorean split: sqrt(510^2 + 680^2) = 850 m/s = 0.85c */
    int64_t v_high_z = 680LL << FLOW_Q16_SHIFT;
    int64_t h_ball = 2LL << FLOW_Q16_SHIFT;            /* Object / light beam at 2.0m above elevator floor */
    int64_t y_elev = 5000LL << FLOW_Q16_SHIFT;         /* Elevator at altitude 5000m */
    int64_t c_eff  = FLOW_C_LIGHT_DEFAULT_Q16;         /* c = 1000 m/s in Q16 */

    /*
     * Experiment 1: Passenger & Ball inside a Free-Falling High-Speed Elevator in Gravity
     * Both elevator and passenger move at subluminal 3D speed ||v|| = sqrt(510^2 + 680^2) = 850 m/s = 0.85c
     * and accelerate at g_val.
     */
    int64_t g_planet[3] = {0LL, g_val, 0LL};
    FlowJet2 elev_freefall[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(y_elev, v_high_y, g_val),
        flow_jet2_make(0LL, v_high_z, 0LL)
    };
    FlowJet2 passenger_freefall[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(y_elev + h_ball, v_high_y, 0LL), /* Free object: intrinsic d2 = 0, gravity supplies g_val */
        flow_jet2_make(0LL, v_high_z, 0LL)
    };

    FlowJet2 local_freefall[3];
    int64_t proper_g_freefall[3];
    flow_game_eval_einstein_elevator(
        elev_freefall, passenger_freefall, g_planet, local_freefall, proper_g_freefall
    );

    /* Inside the moving / free-falling elevator, passenger feels 100% stationary and weightless! */
    assert(local_freefall[0].v == 0LL && local_freefall[0].d1 == 0LL && local_freefall[0].d2 == 0LL);
    assert(local_freefall[1].v == h_ball && local_freefall[1].d1 == 0LL && local_freefall[1].d2 == 0LL);
    assert(local_freefall[2].v == 0LL && local_freefall[2].d1 == 0LL && local_freefall[2].d2 == 0LL);
    assert(proper_g_freefall[0] == 0LL && proper_g_freefall[1] == 0LL && proper_g_freefall[2] == 0LL);

    /* Relative scale between passenger and elevator is 100% rest frame (beta=0, gamma=1.0, Doppler=1.0) */
    FlowRelativeScale64 scale_in_elevator;
    flow_game_resolve_relative_scale(
        passenger_freefall, elev_freefall, c_eff, 0LL,
        50LL << FLOW_Q16_SHIFT, 50LL << FLOW_Q16_SHIFT,
        1LL << FLOW_Q16_SHIFT, 10LL << FLOW_Q16_SHIFT, &scale_in_elevator
    );
    assert(scale_in_elevator.lorentz_beta_q16 == 0);
    assert(scale_in_elevator.lorentz_gamma_q16 == FLOW_Q16_ONE);
    assert(scale_in_elevator.doppler_factor_q16 == FLOW_Q16_ONE);
    assert(scale_in_elevator.proper_time_ratio_q16 == FLOW_Q16_ONE);

    /* Even after t = 10.0 seconds of high-speed free-fall, passenger's position in elevator is unchanged! */
    int64_t y_after_10s = flow_jet2_eval_taylor(local_freefall[1], 10LL << FLOW_Q16_SHIFT);
    assert(y_after_10s == h_ball);

    /*
     * Experiment 2: Einstein's Equivalence Principle (Massive Ball)
     *   Frame A (Deep Space Rocket Elevator): g = 0, elevator cruises at v_y = 0.85c and accelerates upward at a_elev = +9.8 m/s^2.
     *   Frame B (Planetary Stationary Elevator): g = -9.8 m/s^2, elevator is stationary (a_elev = 0).
     */
    int64_t g_zero[3] = {0LL, 0LL, 0LL};
    FlowJet2 elev_rocket_A[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(y_elev, v_high, -g_val), /* Accelerating upward at +9.8 m/s^2 in deep space */
        flow_jet2_make(0LL, 0LL, 0LL)
    };
    FlowJet2 ball_rocket_A[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(y_elev + h_ball, v_high, 0LL), /* Released from passenger's hand at t=0 */
        flow_jet2_make(0LL, 0LL, 0LL)
    };

    FlowJet2 elev_planet_B[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL), /* Stationary on planet surface */
        flow_jet2_make(0LL, 0LL, 0LL)
    };
    FlowJet2 ball_planet_B[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(h_ball, 0LL, 0LL), /* Released at height h_ball in planetary gravity */
        flow_jet2_make(0LL, 0LL, 0LL)
    };

    FlowJet2 local_ball_A[3], local_ball_B[3];
    int64_t proper_g_A[3], proper_g_B[3];
    flow_game_eval_einstein_elevator(elev_rocket_A, ball_rocket_A, g_zero, local_ball_A, proper_g_A);
    flow_game_eval_einstein_elevator(elev_planet_B, ball_planet_B, g_planet, local_ball_B, proper_g_B);

    /* Bit-for-bit identical 2-Jet trajectories and proper G-force! */
    assert(memcmp(local_ball_A, local_ball_B, sizeof(local_ball_A)) == 0);
    assert(memcmp(proper_g_A, proper_g_B, sizeof(proper_g_A)) == 0);
    assert(proper_g_A[1] == -g_val); /* Passenger feels +9.8 m/s^2 floor weight in both! */

    /* Solve exact floor impact time t_hit (y_local(t) == 0) via flow_jet2_solve_root */
    int64_t t_hit_A = 0LL, t_hit_B = 0LL;
    assert(flow_jet2_solve_root(local_ball_A[1], FLOW_Q16_ONE, &t_hit_A));
    assert(flow_jet2_solve_root(local_ball_B[1], FLOW_Q16_ONE, &t_hit_B));
    assert(t_hit_A == t_hit_B); /* Bit-for-bit identical impact time (~0.6389s = 41870 in Q16)! */
    assert(t_hit_A > 41000LL && t_hit_A < 42500LL);

    /*
     * Experiment 3: Constancy of Light Speed c & Horizontal Light-Beam Bending in the Elevator
     *   3a. External world light beams (+c upward, -c downward, +c horizontal) observed from
     *       elev_rocket_A (v_y = 0.85c) all preserve exact local speed ||u'|| == c_eff!
     *       Furthermore, vertical light beams have d2 == 0 (u_local . a_local == 0), maintaining
     *       constant speed c over time t rather than slowing down or going superluminal!
     *   3b. A horizontal laser beam emitted from the left wall at height h = 2.0m inside Frame A
     *       (deep-space rocket moving at v_y = 0.85c and accelerating at +9.8 m/s^2) and Frame B
     *       (stationary elevator in planetary gravity g = -9.8 m/s^2) has world speed c_eff in
     *       both frames, transforms to bit-for-bit identical local 2-Jet trajectories with
     *       local horizontal speed u'_x == c_eff, and bends downward by the exact same amount!
     *   3c. 3D off-axis light beams (dir = {2, 3, 6} and {1, 2, 3}) emitted inside the 3D-moving
     *       elevator (v = {0, 510, 680}) and SI vacuum light beams (c = 299,792,458 m/s) preserve
     *       exact light-cone norm and round-trip velocity recovery.
     */
    FlowJet2 world_light_up[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(y_elev, c_eff, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL)
    };
    FlowJet2 world_light_down[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(y_elev, -c_eff, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL)
    };
    FlowJet2 world_light_horiz[3] = {
        flow_jet2_make(0LL, c_eff, 0LL),
        flow_jet2_make(y_elev, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL)
    };
    FlowJet2 local_light_up[3], local_light_down[3], local_light_horiz[3];
    flow_game_eval_einstein_elevator(elev_rocket_A, world_light_up, g_zero, local_light_up, NULL);
    flow_game_eval_einstein_elevator(elev_rocket_A, world_light_down, g_zero, local_light_down, NULL);
    flow_game_eval_einstein_elevator(elev_rocket_A, world_light_horiz, g_zero, local_light_horiz, NULL);

    /* Upward and downward light beams in the 0.85c elevator propagate at exact +c and -c with d2 == 0! */
    assert(local_light_up[1].d1 == c_eff && local_light_up[1].d2 == 0LL);
    assert(local_light_down[1].d1 == -c_eff && local_light_down[1].d2 == 0LL);
    assert(flow_jet2_eval_taylor(local_light_up[1], FLOW_Q16_ONE) == c_eff);
    assert(flow_jet2_eval_taylor(local_light_down[1], FLOW_Q16_ONE) == -c_eff);

    /* Transverse world light beam undergoes relativistic aberration in elevator frame, preserving ||u'|| == c */
    __extension__ typedef unsigned __int128 u128;
    __extension__ typedef __int128 i128;
    u128 horiz_local_sq = (u128)((i128)local_light_horiz[0].d1 * local_light_horiz[0].d1
                               + (i128)local_light_horiz[1].d1 * local_light_horiz[1].d1
                               + (i128)local_light_horiz[2].d1 * local_light_horiz[2].d1);
    int64_t horiz_local_speed = (int64_t)flow_asm_isqrt_u128(horiz_local_sq);
    assert(horiz_local_speed >= c_eff - 2LL && horiz_local_speed <= c_eff + 2LL);

    /* Emit horizontal light beam (+X) from the left wall of both elevators at height h_ball */
    int64_t dir_plus_x[3] = { FLOW_Q16_ONE, 0LL, 0LL };
    int64_t v_elev_A[3]   = { 0LL, v_high, 0LL };
    int64_t v_elev_B[3]   = { 0LL, 0LL, 0LL };
    int64_t u_light_world_A[3], u_light_world_B[3];

    int64_t speed_world_A = flow_game_lorentz_emit_light_world_q16(v_elev_A, dir_plus_x, c_eff, u_light_world_A);
    int64_t speed_world_B = flow_game_lorentz_emit_light_world_q16(v_elev_B, dir_plus_x, c_eff, u_light_world_B);
    assert(speed_world_A == c_eff);
    assert(speed_world_B == c_eff);

    FlowJet2 beam_rocket_A[3] = {
        flow_jet2_make(0LL, u_light_world_A[0], 0LL),
        flow_jet2_make(y_elev + h_ball, u_light_world_A[1], 0LL),
        flow_jet2_make(0LL, u_light_world_A[2], 0LL)
    };
    FlowJet2 beam_planet_B[3] = {
        flow_jet2_make(0LL, u_light_world_B[0], 0LL),
        flow_jet2_make(h_ball, u_light_world_B[1], 0LL),
        flow_jet2_make(0LL, u_light_world_B[2], 0LL)
    };

    FlowJet2 local_beam_A[3], local_beam_B[3];
    flow_game_eval_einstein_elevator(elev_rocket_A, beam_rocket_A, g_zero, local_beam_A, NULL);
    flow_game_eval_einstein_elevator(elev_planet_B, beam_planet_B, g_planet, local_beam_B, NULL);

    /* Bit-for-bit identical local light-beam 2-Jets in Frame A and Frame B, with local speed u'_x == c_eff! */
    assert(memcmp(local_beam_A, local_beam_B, sizeof(local_beam_A)) == 0);
    assert(local_beam_A[0].d1 == c_eff);
    assert(local_beam_A[1].d1 == 0LL);
    assert(local_beam_A[1].d2 == g_val);

    /* At t = 0.5s (after light travels 500m horizontally), vertical gravitational deflection is identical */
    int64_t t_half_sec = FLOW_Q16_ONE >> 1U;
    int64_t beam_y_A = flow_jet2_eval_taylor(local_beam_A[1], t_half_sec);
    int64_t beam_y_B = flow_jet2_eval_taylor(local_beam_B[1], t_half_sec);
    int64_t beam_x_A = flow_jet2_eval_taylor(local_beam_A[0], t_half_sec);
    assert(beam_y_A == beam_y_B);
    assert(beam_y_A < h_ball); /* Light beam bent downward toward floor! */
    assert(beam_x_A == (500LL << FLOW_Q16_SHIFT));

    /* 3c. 3D off-axis light ray (Pythagorean quadruple {2, 3, 6} and {1, 2, 3}) in 3D-moving elevator */
    int64_t dir_quad[3] = { 2LL, 3LL, 6LL };
    int64_t v_elev_3d[3] = { 0LL, v_high_y, v_high_z };
    int64_t u_world_quad[3] = { 0LL, 0LL, 0LL };
    int64_t speed_quad_world = flow_game_lorentz_emit_light_world_q16(v_elev_3d, dir_quad, c_eff, u_world_quad);
    assert(speed_quad_world == c_eff);

    FlowJet2 beam_3d_world[3] = {
        flow_jet2_make(0LL, u_world_quad[0], 0LL),
        flow_jet2_make(y_elev, u_world_quad[1], 0LL),
        flow_jet2_make(0LL, u_world_quad[2], 0LL)
    };
    FlowRelativeScale64 scale_3d_beam;
    flow_game_resolve_relative_scale(
        beam_3d_world, elev_freefall, c_eff, 0LL,
        50LL << FLOW_Q16_SHIFT, 50LL << FLOW_Q16_SHIFT,
        1LL << FLOW_Q16_SHIFT, 10LL << FLOW_Q16_SHIFT, &scale_3d_beam
    );
    assert(scale_3d_beam.lorentz_beta_q16 == FLOW_Q16_ONE);

    FlowJet2 local_3d_beam[3];
    flow_game_eval_einstein_elevator(elev_freefall, beam_3d_world, g_planet, local_3d_beam, NULL);
    int64_t expected_quad[3] = {
        (2LL * c_eff + 3LL) / 7LL,
        (3LL * c_eff + 3LL) / 7LL,
        (6LL * c_eff + 3LL) / 7LL
    };
    for (size_t k = 0; k < 3U; ++k) {
        int64_t diff = local_3d_beam[k].d1 - expected_quad[k];
        if (diff < 0LL) diff = -diff;
        assert(diff <= 2LL);
    }

    /* 3d. SI Vacuum light speed (c = 299,792,458 m/s) in the Einstein Elevator */
    int64_t c_vac = FLOW_C_LIGHT_VACUUM_Q16;
    FlowJet2 elev_vac_A[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(y_elev, (c_vac / 20LL) * 17LL, -g_val),
        flow_jet2_make(0LL, 0LL, 0LL)
    };
    FlowJet2 light_vac_up[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(y_elev, c_vac, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL)
    };
    FlowJet2 local_vac_up[3];
    flow_game_eval_einstein_elevator(elev_vac_A, light_vac_up, g_zero, local_vac_up, NULL);
    assert(local_vac_up[1].d1 == c_vac && local_vac_up[1].d2 == 0LL);

    printf("  ✓ Moving & Free-Falling Elevator Passenger feels 100%% stationary (dv=0, da=0, beta=0, proper_g=0).\n");
    printf("  ✓ Deep-Space Rocket (+9.8 m/s^2) === Planetary Gravity (-9.8 m/s^2) bit-for-bit identical (t_hit=%lld Q16).\n",
           (long long)t_hit_A);
    printf("  ✓ Einstein Elevator 1D/3D Light-Beam Constancy (c=1000 & SI c=299,792,458 m/s) & Deflection verified.\n");
}

/*
 * Test 8: FlowTimingWheel64 100,000 Concurrent Entities Zero-Jitter Verification
 *
 * Verifies:
 *   1. "Don't Starve" 100,000-entity thundering-herd elimination:
 *      Simultaneously register 100,000 entities on the exact same frame (tick = 0)
 *      with the exact same period (64 ticks).
 *   2. Step the wheel for 64 ticks using flow_timing_wheel_step_o1.
 *   3. All 100,000 events fire across the 64 ticks (total_fired == 100000U).
 *   4. Wave64 O(1) bit-test slot dispatch: 1-cycle per slot without polling
 *      the 100,000 entities.
 *   5. Deterministic golden-ratio phase staggering: every single tick fires
 *      almost the exact same number of entities (100000 / 64 = 1562 or 1563,
 *      so max_fired_per_tick - min_fired_per_tick <= 1 — literally ZERO thundering-herd spike!).
 */
static void test_timing_wheel_100k_zero_jitter(void) {
    printf("[Test 8/13] FlowTimingWheel64 100,000 Concurrent Entities Zero-Jitter Verification...\n");

    FlowTimingWheel64 wheel;
    uint32_t slot_buckets[64];
    memset(slot_buckets, 0, sizeof(slot_buckets));

    flow_timing_wheel_init(&wheel);
    assert(wheel.active_slot_mask == 0ULL);
    assert(wheel.current_tick == 0U);
    assert(wheel.total_scheduled == 0U);
    assert(wheel.total_fired == 0U);

    /*
     * Generate 100,000 distinct entities on the exact same frame (tick = 0) with period = 64 ticks.
     * The modular inverse of 0x9E3779B97F4A7C15ULL is 0xF1DE83E19937733DULL (prod == 1 mod 2^64).
     * This distributes 100,000 entities with perfect uniform balance across all 64 slots:
     * exactly 32 slots receive 1563 entities, and 32 slots receive 1562 entities (total 100,000).
     */
    const uint64_t inv_phi = 0xF1DE83E19937733DULL;
    for (uint32_t s = 0U; s < 64U; ++s) {
        uint32_t target_count = (s < 32U) ? 1563U : 1562U;
        uint64_t slot_base = (uint64_t)s << 58U;
        uint64_t slot_step = (1ULL << 58U) / 1564ULL;
        for (uint32_t k = 0U; k < target_count; ++k) {
            uint64_t hash_target = slot_base + (uint64_t)(k + 1U) * slot_step;
            uint64_t entity_id = hash_target * inv_phi;
            uint32_t assigned_slot = flow_timing_wheel_schedule_staggered(&wheel, slot_buckets, entity_id, 64U);
            assert(assigned_slot == s);
        }
    }

    assert(wheel.total_scheduled == 100000U);
    assert(wheel.active_slot_mask == 0xFFFFFFFFFFFFFFFFULL);

    /* Verify slot_counts compact summary for first 18 sub-buckets */
    for (size_t i = 0U; i < 18U; ++i) {
        assert(wheel.slot_counts[i] == (uint16_t)slot_buckets[i]);
        assert(wheel.slot_counts[i] == 1563U);
    }

    /* Step the wheel for 64 ticks using flow_timing_wheel_step_o1 */
    for (uint32_t t = 0U; t < 64U; ++t) {
        uint32_t fired = flow_timing_wheel_step_o1(&wheel, slot_buckets);
        assert(fired == 1562U || fired == 1563U);
    }

    /* Verify all 100,000 events fired across 64 ticks */
    assert(wheel.total_fired == 100000U);
    assert(wheel.total_scheduled == 0U);
    assert(wheel.active_slot_mask == 0ULL);
    assert(wheel.min_fired_per_tick == 1562U);
    assert(wheel.max_fired_per_tick == 1563U);
    assert(wheel.max_fired_per_tick - wheel.min_fired_per_tick <= 1U);

    /* Also verify sequential IDs (0..99999): thundering herd spike reduced from 100,000 to <= 1564 */
    FlowTimingWheel64 seq_wheel;
    uint32_t seq_buckets[64];
    memset(seq_buckets, 0, sizeof(seq_buckets));
    flow_timing_wheel_init(&seq_wheel);

    for (uint64_t i = 0ULL; i < 100000ULL; ++i) {
        flow_timing_wheel_schedule_staggered(&seq_wheel, seq_buckets, i, 64U);
    }
    assert(seq_wheel.total_scheduled == 100000U);
    assert(seq_wheel.active_slot_mask == 0xFFFFFFFFFFFFFFFFULL);

    for (uint32_t t = 0U; t < 64U; ++t) {
        uint32_t fired = flow_timing_wheel_step_o1(&seq_wheel, seq_buckets);
        assert(fired >= 1560U && fired <= 1564U);
    }
    assert(seq_wheel.total_fired == 100000U);
    assert(seq_wheel.total_scheduled == 0U);
    assert(seq_wheel.active_slot_mask == 0ULL);
    assert(seq_wheel.max_fired_per_tick - seq_wheel.min_fired_per_tick <= 4U);

    printf("  ✓ 100,000 concurrent entities registered at tick 0 with period 64 (0 thundering herd).\n");
    printf("  ✓ Wave64 O(1) bit test dispatched all 100,000 events across 64 ticks (total_fired=%u).\n",
           wheel.total_fired);
    printf("  ✓ Golden-ratio phase dispersion: min=%u, max=%u (spread=%u <= 1, perfect 1562/1563 load).\n",
           wheel.min_fired_per_tick, wheel.max_fired_per_tick,
           wheel.max_fired_per_tick - wheel.min_fired_per_tick);
}

static void test_unified_keybind_vr_and_external_api(void) {
    printf("[Test 9/13] Unified Keybind Chord, VR 6-DoF Spatial Intent & Anti-Waggle Gate...\n");

    assert(sizeof(FlowInputIntent64) == 64U);
    assert(_Alignof(FlowInputIntent64) == 64U);

    flow_ssot_init_canonical();
    const FlowPrefabDesc64 *target_sphere = flow_ssot_get_prefab(FLOW_PREFAB_CORE_SPHERE); /* radius = 2.0m */
    assert(target_sphere != NULL);

    /* 1. 1-Cycle Ternary Keybind Chord Resolution (Shift+Q vs unmodified Q) */
    const uint64_t KEY_Q     = (1ULL << 0);
    const uint64_t KEY_SHIFT = (1ULL << 1);

    /* Press Q + Shift simultaneously */
    FlowInputIntent64 km_intent = flow_input_lift_keymouse_to_intent(
        KEY_Q | KEY_SHIFT,
        0, 0,
        (int32_t)(-3LL * FLOW_Q16_ONE),  /* z = -3.0m facing target at origin */
        (int32_t)(12LL * FLOW_Q16_ONE)   /* virtual strike speed = 12 m/s */
    );
    /* Unmodified Q rule (requires Q, forbids Shift) must NOT fire */
    assert(flow_input_resolve_keybind_chord(&km_intent, KEY_Q, KEY_SHIFT, FLOW_ACTION_PRIMARY_STRIKE) == false);
    /* Shift+Q Ultimate Chord rule (requires Q|Shift, forbids 0) MUST fire */
    assert(flow_input_resolve_keybind_chord(&km_intent, KEY_Q | KEY_SHIFT, 0ULL, FLOW_ACTION_ULTIMATE_CHORD) == true);
    assert((km_intent.semantic_action_mask & FLOW_ACTION_ULTIMATE_CHORD) != 0ULL);

    /* 2. Keymouse Lifted to 2-Jet hits the target identically to a VR swing */
    const int64_t min_lethal_speed_q16 = 5LL * FLOW_Q16_ONE; /* 5.0 m/s threshold */
    const int64_t dt_q16 = FLOW_Q16_ONE / 4;                 /* 0.25s step */
    FlowContactManifold64 km_manifold;
    FlowHitZone km_zone = flow_input_eval_spatial_interaction(
        &km_intent, target_sphere, min_lethal_speed_q16, dt_q16, &km_manifold
    );
    assert(km_zone == FLOW_HIT_LETHAL);
    assert(km_intent.haptic_freq_out_q16 > (uint32_t)(220U * FLOW_Q16_ONE));

    /* 3. VR 6-DoF Controller: Fast Physical Swing (15 m/s) -> LETHAL Strike */
    FlowInputIntent64 vr_swing;
    memset(&vr_swing, 0, sizeof(vr_swing));
    vr_swing.device_class       = FLOW_DEVICE_VR_6DOF;
    vr_swing.stereo_ipd_q16     = 4194; /* 64mm binocular IPD in Q16 */
    vr_swing.hand_pos_q16[2]    = (int32_t)(-3LL * FLOW_Q16_ONE);
    vr_swing.hand_vel_q16[2]    = (int32_t)(15LL * FLOW_Q16_ONE);
    FlowContactManifold64 vr_manifold;
    FlowHitZone vr_zone = flow_input_eval_spatial_interaction(
        &vr_swing, target_sphere, min_lethal_speed_q16, dt_q16, &vr_manifold
    );
    assert(vr_zone == FLOW_HIT_LETHAL);

    /* 4. VR Anti-Waggle Exploit Prevention:
     *    Controller resting inside the target at low speed (0.5 m/s < 5.0 m/s threshold)
     *    is downgraded from LETHAL to tactile GRAZE touch! */
    FlowInputIntent64 vr_waggle = vr_swing;
    vr_waggle.hand_pos_q16[2] = (int32_t)(-1LL * FLOW_Q16_ONE); /* inside r=2.0m sphere */
    vr_waggle.hand_vel_q16[2] = (int32_t)(FLOW_Q16_ONE / 2);    /* 0.5 m/s wrist waggle */
    FlowHitZone waggle_zone = flow_input_eval_spatial_interaction(
        &vr_waggle, target_sphere, min_lethal_speed_q16, dt_q16, &vr_manifold
    );
    assert(waggle_zone == FLOW_HIT_GRAZE);
    assert(vr_waggle.haptic_freq_out_q16 == (uint32_t)(110U * FLOW_Q16_ONE));

    printf("  ✓ 1-cycle ternary chord (Shift+Q vs Q), Keymouse-to-2-Jet lifting, VR 6-DoF & Anti-Waggle gate verified!\n");
}

static void test_headless_udp_64b_and_jet2_dead_reckoning(void) {
    printf("[Test 10/13] Zero-Framework 64B UDP Loopback, Out-of-Order Drop & 2-Jet Dead Reckoning...\n");

    assert(sizeof(FlowUdpSnapshot64) == 64U);
    assert(_Alignof(FlowUdpSnapshot64) == 64U);

    /* 1. Run 600 authoritative headless ticks (10 seconds at 60Hz, zero GPU) */
    FlowUdpSnapshot64 headless_snap;
    assert(flow_game_run_headless_ticks(600U, &headless_snap) == 0);
    assert(headless_snap.server_tick == 600U);
    assert(flow_udp_verify_snapshot64(&headless_snap) == true);

    /* 2. Open non-blocking POSIX AF_INET SOCK_DGRAM (UDP) loopback pair */
    int server_fd = -1, client_fd = -1;
    uint16_t client_port = 0U;
    assert(flow_udp_open_loopback_pair(&server_fd, &client_fd, &client_port) == 0);

    FlowGameEngineState srv_engine;
    flow_game_init(&srv_engine);
    /* Accelerating worldline: z(0) = 100m, vz(0) = 50 m/s, az(0) = 20 m/s^2 */
    srv_engine.craft_traj[2] = flow_jet2_make(
        100LL * FLOW_Q16_ONE,
        50LL * FLOW_Q16_ONE,
        20LL * FLOW_Q16_ONE
    );

    /* Send UDP packet #10, then #12 (newer), then #11 (out-of-order late arrival!) */
    FlowUdpSnapshot64 pkt10 = flow_udp_pack_snapshot64(&srv_engine, 10ULL, FLOW_ACTION_THRUST_BOOST);
    FlowUdpSnapshot64 pkt12 = flow_udp_pack_snapshot64(&srv_engine, 12ULL, FLOW_ACTION_THRUST_BOOST);
    FlowUdpSnapshot64 pkt11 = flow_udp_pack_snapshot64(&srv_engine, 11ULL, FLOW_ACTION_THRUST_BOOST);

    /* Also send a tampered packet #99 with corrupted MAC */
    FlowUdpSnapshot64 pkt_forged = flow_udp_pack_snapshot64(&srv_engine, 99ULL, FLOW_ACTION_THRUST_BOOST);
    pkt_forged.state_mac ^= 0x1ULL;

    assert(flow_udp_send_snapshot64(server_fd, client_port, &pkt10) == 0);
    assert(flow_udp_send_snapshot64(server_fd, client_port, &pkt12) == 0);
    assert(flow_udp_send_snapshot64(server_fd, client_port, &pkt11) == 0);
    assert(flow_udp_send_snapshot64(server_fd, client_port, &pkt_forged) == 0);

    uint64_t last_seq = 0ULL;
    FlowUdpSnapshot64 latest;
    assert(flow_udp_recv_latest_snapshot64(client_fd, &last_seq, &latest) == true);
    /* Must lock onto #12, silently dropping late #11 and forged #99! */
    assert(last_seq == 12ULL);
    assert(latest.sequence_id == 12ULL);

    /* 3. Simulate 3 dropped UDP packets (dt = 0.5s = 32768 Q16):
     *    Exact 2-Jet Taylor Dead-Reckoning:
     *    z(0.5) = 100 + 50*(0.5) + 0.5*20*(0.5)^2 = 100 + 25 + 2.5 = 127.5 m!
     *    (Whereas naive linear 1-Jet extrapolation would only give 125.0 m -> 2.5m error!) */
    int64_t dt_half_sec_q16 = FLOW_Q16_ONE / 2;
    int64_t predicted_z_q16 = flow_udp_dead_reckon_z_q16(&latest, dt_half_sec_q16);
    int64_t expected_z_q16  = 127LL * FLOW_Q16_ONE + (FLOW_Q16_ONE / 2); /* 127.5m */
    assert(predicted_z_q16 == expected_z_q16);

    close(server_fd);
    close(client_fd);
    flow_game_destroy(&srv_engine);

    printf("  ✓ 64B non-blocking UDP loopback (seq=12, dropped late #11 & forged #99) & exact 2-Jet Dead Reckoning (127.5m) verified!\n");
}

static void test_qsbr_hot_patch_and_lazy_sector_o1(void) {
    printf("[Test 11/13] 64B Zero-Downtime QSBR Hot-Patch & O(1) Time-Skip Lazy Evaluation...\n");

    assert(sizeof(FlowLazySector64) == 64U);
    assert(_Alignof(FlowLazySector64) == 64U);

    FlowGameEngineState engine;
    flow_game_init(&engine);

    /* 1. Publish Season v1 64-Byte Sector Patch at t = 0ms:
     *    z(0) = 100m, vz(0) = 10 m/s, az(0) = 2 m/s^2, Prefab=CORE_SPHERE, Style=GRADE_B */
    FlowLazySector64 patch_v1 = flow_sector_make_patch64(
        777ULL,
        1U,
        0U,
        flow_jet2_make(100LL * FLOW_Q16_ONE, 10LL * FLOW_Q16_ONE, 2LL * FLOW_Q16_ONE),
        FLOW_PREFAB_CORE_SPHERE,
        FLOW_STYLE_MINERAL_GRADE_B
    );
    assert(flow_sector_qsbr_publish_patch(&engine, &patch_v1) == 0);

    /* 2. Player leaves sector for 100 seconds (100,000 ms -> 0 CPU ticks spent while away!).
     *    When evaluated at t = 100,000 ms:
     *    Exact 2-Jet O(1) position: z(100) = 100 + 10*(100) + 0.5*2*(100)^2 = 100 + 1000 + 10000 = 11,100 m! */
    FlowLazySector64 eval_far;
    /* Player is at z = 0m -> distance is 11,100m (> 1000m) => Lazy Layer collapses to HUD (2) */
    assert(flow_sector_qsbr_eval_lazy_o1(&engine, 100000U, 0LL, &eval_far) == true);
    assert(eval_far.patch_version == 1U);
    assert(eval_far.current_z_q16 == (int32_t)(11100LL * FLOW_Q16_ONE));
    assert(eval_far.resolved_lazy_layer == FLOW_STAGE_LAYER_HUD);

    /* Player warps to z = 10,600m (distance = 500m in [100m, 1000m]) => Lazy Layer = 2D Slice (1) */
    FlowLazySector64 eval_mid;
    assert(flow_sector_qsbr_eval_lazy_o1(&engine, 100000U, 10600LL * FLOW_Q16_ONE, &eval_mid) == true);
    assert(eval_mid.resolved_lazy_layer == FLOW_STAGE_LAYER_1_SLICE);

    /* Player closes in to z = 11,050m (distance = 50m <= 100m) => JIT expands full 3D SDF Layer 0 (0)! */
    FlowLazySector64 eval_close;
    assert(flow_sector_qsbr_eval_lazy_o1(&engine, 100000U, 11050LL * FLOW_Q16_ONE, &eval_close) == true);
    assert(eval_close.resolved_lazy_layer == FLOW_STAGE_LAYER_0_SDF);

    /* 3. Zero-Downtime Mid-Flight 64B Live Hot-Patch (v1 -> v2 Season Boss Upgrade):
     *    Upgrades Prefab to ASTEROID_GRADE_S & Style to GRADE_S at t = 100,000ms without restarting! */
    FlowLazySector64 patch_v2 = flow_sector_make_patch64(
        777ULL,
        2U,
        100000U,
        flow_jet2_make(11100LL * FLOW_Q16_ONE, 210LL * FLOW_Q16_ONE, 0LL),
        FLOW_PREFAB_ASTEROID_GRADE_S,
        FLOW_STYLE_MINERAL_GRADE_S
    );
    assert(flow_sector_qsbr_publish_patch(&engine, &patch_v2) == 0);

    FlowLazySector64 eval_v2;
    assert(flow_sector_qsbr_eval_lazy_o1(&engine, 101000U, 11300LL * FLOW_Q16_ONE, &eval_v2) == true);
    assert(eval_v2.patch_version == 2U);
    assert(eval_v2.prefab_id == FLOW_PREFAB_ASTEROID_GRADE_S);
    assert(eval_v2.art_style_id == FLOW_STYLE_MINERAL_GRADE_S);
    assert(eval_v2.current_z_q16 == (int32_t)(11310LL * FLOW_Q16_ONE)); /* 11100 + 210 * 1s */
    assert(eval_v2.resolved_lazy_layer == FLOW_STAGE_LAYER_0_SDF);

    flow_game_destroy(&engine);
    printf("  ✓ O(1) 100s time-skip (z=11,100m, 0 loop ticks), 3-tier Lazy Collapse (HUD->Slice->3D) & 64B QSBR Hot-Patch (v1->v2) verified!\n");
}

static void test_save_capsule_hex128_and_anticheat_replay(void) {
    printf("[Test 12/13] 64B Deterministic Save Capsule, Hex128 Share Code & Driverless Anti-Cheat Replay...\n");

    assert(sizeof(FlowSaveCapsule64) == 64U);
    assert(_Alignof(FlowSaveCapsule64) == 64U);

    /* 1. Record a 240-tick deterministic gameplay session from seed 0x2026F109ULL */
    FlowReplayInputCmd recorded_inputs[240];
    memset(recorded_inputs, 0, sizeof(recorded_inputs));
    for (uint32_t t = 0U; t < 240U; ++t) {
        recorded_inputs[t].throttle_q16   = (t < 120U) ? (2LL * FLOW_Q16_ONE) : FLOW_Q16_ONE;
        recorded_inputs[t].pitch_rate_q16 = (t % 30U == 0U) ? (FLOW_Q16_ONE / 8LL) : 0LL;
        recorded_inputs[t].yaw_rate_q16   = (t % 45U == 0U) ? -(FLOW_Q16_ONE / 16LL) : 0LL;
        recorded_inputs[t].roll_rate_q16  = 0LL;
        recorded_inputs[t].toggle_layer   = (t == 60U) ? 1U : 0U;
        if (t == 100U) {
            recorded_inputs[t].trigger_ricardian = 1U;
            recorded_inputs[t].ricardian_Cost    = 4000ULL;
            recorded_inputs[t].ricardian_Temp    = 10ULL;
        }
    }

    const uint64_t seed = 0x2026F109CAFEBABEULL;
    FlowSaveCapsule64 capsule;
    assert(flow_save_run_recorded_session(seed, recorded_inputs, 240U, &capsule) == true);
    assert(flow_save_verify_capsule64(&capsule) == true);
    assert(capsule.initial_seed == seed);
    assert(capsule.tick_count == 240ULL);
    assert(capsule.final_craft_z_q16 > -(20000LL << FLOW_Q16_SHIFT));
    assert(capsule.final_craft_vz_q16 > 0LL);

    /* 2. Hex128 Clipboard Share Code Bijective Encode/Decode */
    char share_hex[129];
    flow_save_encode_hex128(&capsule, share_hex);
    assert(strlen(share_hex) == 128U);

    FlowSaveCapsule64 decoded;
    assert(flow_save_decode_hex128(share_hex, &decoded) == true);
    assert(memcmp(&decoded, &capsule, sizeof(FlowSaveCapsule64)) == 0);

    /* Tamper 1 hex character in clipboard share code -> rejected by MAC! */
    char tampered_hex[129];
    memcpy(tampered_hex, share_hex, sizeof(share_hex));
    tampered_hex[24] = (tampered_hex[24] == 'F') ? '0' : 'F';
    FlowSaveCapsule64 rejected_cap;
    assert(flow_save_decode_hex128(tampered_hex, &rejected_cap) == false);

    /* Non-hex character in clipboard share code -> rejected! */
    memcpy(tampered_hex, share_hex, sizeof(share_hex));
    tampered_hex[10] = 'Z';
    assert(flow_save_decode_hex128(tampered_hex, &rejected_cap) == false);

    /* 3. Engine State Restoration from Valid Capsule vs Forged Capsule */
    FlowGameEngineState restored_engine;
    assert(flow_game_init(&restored_engine) == 0);
    assert(flow_save_restore_engine(&restored_engine, &decoded) == true);
    assert(restored_engine.frame_index == 240ULL);
    assert(restored_engine.hardware_sieve == FLOW_ASM_SIEVE_ALL_SOUND);
    assert(restored_engine.ricardian.m0_vault == capsule.ricardian_vault);
    assert(restored_engine.craft_traj[2].v == capsule.final_craft_z_q16);
    assert(restored_engine.craft_traj[2].d1 == capsule.final_craft_vz_q16);
    assert(restored_engine.prev_craft_traj[2].v == capsule.final_craft_z_q16);
    assert(restored_engine.prev_craft_traj[2].d1 == capsule.final_craft_vz_q16);

    /* Verify exact bijective roundtrip: re-packing restored_engine equals capsule bit-for-bit */
    FlowSaveCapsule64 repacked_cap = flow_save_pack_capsule64(
        &restored_engine, seed, capsule.input_hash_chain
    );
    assert(memcmp(&repacked_cap, &capsule, sizeof(FlowSaveCapsule64)) == 0);

    /* Verify stepping restored_engine preserves hardware_sieve == FLOW_ASM_SIEVE_ALL_SOUND */
    FlowGameEngineState step_engine;
    assert(flow_game_init(&step_engine) == 0);
    assert(flow_save_restore_engine(&step_engine, &decoded) == true);
    assert(flow_game_step(&step_engine, 1092LL) == 0);
    assert(step_engine.hardware_sieve == FLOW_ASM_SIEVE_ALL_SOUND);
    flow_game_destroy(&step_engine);

    FlowSaveCapsule64 forged_mac_cap = capsule;
    forged_mac_cap.state_mac ^= 1ULL;
    assert(flow_save_restore_engine(&restored_engine, &forged_mac_cap) == false);

    /* 4. Driverless Anti-Cheat Replay Verification */
    assert(flow_save_audit_replay_anticheat(&capsule, recorded_inputs, 240U) == true);

    /* Simulate Cheat Engine memory tamper: attacker inflates m0_vault by +999,999 AND re-packs a valid MAC! */
    restored_engine.ricardian.m0_vault += 999999ULL;
    FlowSaveCapsule64 cheat_engine_capsule = flow_save_pack_capsule64(
        &restored_engine, seed, capsule.input_hash_chain
    );
    assert(flow_save_verify_capsule64(&cheat_engine_capsule) == true); /* MAC itself was forged by attacker */
    /* Driverless deterministic replay catches the memory tamper with 100% certainty! */
    assert(flow_save_audit_replay_anticheat(&cheat_engine_capsule, recorded_inputs, 240U) == false);

    /* Simulate tampered input stream (1 tick altered) -> caught by input_hash_chain & state mismatch! */
    FlowReplayInputCmd tampered_inputs[240];
    memcpy(tampered_inputs, recorded_inputs, sizeof(recorded_inputs));
    tampered_inputs[50].throttle_q16 += FLOW_Q16_ONE;
    assert(flow_save_audit_replay_anticheat(&capsule, tampered_inputs, 240U) == false);

    /* Boundary case: 0-tick session */
    FlowSaveCapsule64 zero_tick_cap;
    assert(flow_save_run_recorded_session(seed, NULL, 0U, &zero_tick_cap) == true);
    assert(flow_save_audit_replay_anticheat(&zero_tick_cap, NULL, 0U) == true);
    assert(flow_save_audit_replay_anticheat(&capsule, recorded_inputs, 239U) == false);

    flow_game_destroy(&restored_engine);
    printf("  ✓ 64B Save Capsule, Hex128 Share Code & Driverless Anti-Cheat Replay (caught Cheat Engine +999,999 vault tamper) verified!\n");
}

static void test_fps120_hysteresis_governor(void) {
    printf("[Test 13/13] 120 FPS (8.33ms) Dual-Threshold Frame-Time Hysteresis Governor...\n");

    assert(sizeof(FlowFrameGovernor64) == 64U);
    assert(_Alignof(FlowFrameGovernor64) == 64U);

    FlowGameEngineState engine;
    assert(flow_game_init(&engine) == 0);
    assert(flow_fps_governor_verify(&engine.fps_governor) == true);
    assert(engine.fps_governor.quality_throttled == 0U);
    assert(engine.fps_governor.raymarch_max_steps == FLOW_FPS120_RAYMARCH_STEPS_ULTRA);
    assert(engine.fps_governor.layer0_radius_q16 == FLOW_FPS120_LAYER0_R_ULTRA_Q16);

    /* 1. Nominal frames below 7800 us (e.g. 5200 us, 7800 us exact boundary) stay in Ultra mode (64 steps, 100m) */
    FlowFrameGovernor64 g0 = flow_game_step_fps_governor(&engine, 5200U);
    assert(flow_fps_governor_verify(&g0) == true);
    assert(g0.quality_throttled == 0U);
    assert(g0.raymarch_max_steps == 64U);
    assert(g0.layer0_radius_q16 == (int32_t)(100 * FLOW_Q16_ONE));
    assert(g0.transition_count == 0U);

    FlowFrameGovernor64 g_bound = flow_game_step_fps_governor(&engine, 7800U);
    assert(g_bound.quality_throttled == 0U);
    assert(g_bound.transition_count == 0U);

    /* 2. Frame spikes above 7800 us (e.g. 8100 us, approaching 8333 us 120 FPS ceiling) -> enters Quality-Throttle! */
    FlowFrameGovernor64 g_spike = flow_game_step_fps_governor(&engine, 8100U);
    assert(flow_fps_governor_verify(&g_spike) == true);
    assert(g_spike.quality_throttled == 1U);
    assert(g_spike.raymarch_max_steps == 32U);
    assert(g_spike.layer0_radius_q16 == (int32_t)(60 * FLOW_Q16_ONE));
    assert(g_spike.transition_count == 1U);
    assert(g_spike.cubical_verified == 1U);

    /* 3. 100 frames oscillating wildly inside the (6000, 7800] us deadband -> ZERO oscillation chatter! */
    for (uint32_t i = 0U; i < 100U; ++i) {
        uint32_t noisy_us = (i & 1U) ? 6001U : 7800U;
        FlowFrameGovernor64 g_hold = flow_game_step_fps_governor(&engine, noisy_us);
        assert(g_hold.quality_throttled == 1U);
        assert(g_hold.raymarch_max_steps == 32U);
        assert(g_hold.transition_count == 1U);
    }

    /* 4. Frame time drops to <= 6000 us recovery threshold -> cleanly exits back to Ultra mode! */
    FlowFrameGovernor64 g_recover = flow_game_step_fps_governor(&engine, 6000U);
    assert(flow_fps_governor_verify(&g_recover) == true);
    assert(g_recover.quality_throttled == 0U);
    assert(g_recover.raymarch_max_steps == 64U);
    assert(g_recover.layer0_radius_q16 == (int32_t)(100 * FLOW_Q16_ONE));
    assert(g_recover.transition_count == 2U);

    /* 5. Boundary values: 0 us and UINT32_MAX */
    FlowFrameGovernor64 g_zero = flow_game_step_fps_governor(&engine, 0U);
    assert(g_zero.quality_throttled == 0U);
    FlowFrameGovernor64 g_max = flow_game_step_fps_governor(&engine, UINT32_MAX);
    assert(g_max.quality_throttled == 1U);

    flow_game_destroy(&engine);
    printf("  ✓ 120 FPS (8333us) Hysteresis Governor [enter=7800us, exit=6000us, 0 chatter over 100 deadband frames] verified!\n");
}

int main(void) {
    printf("================================================================================\n");
    printf("  BLOOM: Deterministic C17 Verification Suite (Zero Float/Double)\n");
    printf("================================================================================\n");

    test_worldline_anti_tunneling();
    test_stage_handoff_hysteresis();
    test_wave64_tile_culling();
    test_ricardian_v4_laws();
    test_hitbox_padding_and_action_feel();
    test_unified_relative_scale_interface();
    test_einstein_elevator_equivalence();
    test_timing_wheel_100k_zero_jitter();
    test_unified_keybind_vr_and_external_api();
    test_headless_udp_64b_and_jet2_dead_reckoning();
    test_qsbr_hot_patch_and_lazy_sector_o1();
    test_save_capsule_hex128_and_anticheat_replay();
    test_fps120_hysteresis_governor();

    printf("================================================================================\n");
    printf("  ✓ ALL 13 BLOOM DETERMINISTIC C17 VERIFICATION GATES PASSED!\n");
    printf("================================================================================\n");
    return 0;
}

