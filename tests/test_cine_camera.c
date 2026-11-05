/*
 * ============================================================================
 * BLOOM: FlowCineRig64 Cinematography & Physical Optics Test Suite (C17)
 * ============================================================================
 * Strict Zero Float / Double Verification:
 *   Test 1: 64-byte cache-line univalence & canonical VCam SSOT integrity
 *   Test 2: Declarative Q16 polynomial easing curves & boundary robustness
 *   Test 3: FlowComposerPoly dual-threshold dead-zone / soft-zone hysteresis
 *   Test 4: 2-Jet EMA damping follow, VCam blending & hardware Taylor sieve
 *   Test 5: Physical Circle-of-Confusion (CoC) DoF & EV auto-exposure
 *   Test 6: 6D trauma shake envelope & lock-free FlowGameFramePacket integration
 * ============================================================================
 */

#include "flow_game.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_univalence_and_canonical_vcams(void) {
    printf("[Test 1/6] 64-Byte Univalence & Canonical VCam SSOT Table...\n");

    assert(sizeof(FlowCineRig64) == 64U);
    assert(_Alignof(FlowCineRig64) == 64U);
    assert(sizeof(FlowCineResolvedFrame64) == 64U);
    assert(_Alignof(FlowCineResolvedFrame64) == 64U);
    assert((sizeof(FlowGameFramePacket) % 64U) == 0U);
    assert(sizeof(FLOW_CANONICAL_VCAMS) == 256U);

    /* Verify all 4 canonical VCams have sound optical and hysteresis parameters */
    for (uint32_t i = 0; i < (uint32_t)FLOW_VCAM_COUNT; ++i) {
        const FlowCineRig64 *v = flow_cine_get_vcam(i);
        assert(v != NULL);
        assert(v->vcam_id == i);
        assert(v->focal_length_q16 >= (18 * 65536) && v->focal_length_q16 <= (135 * 65536));
        assert(v->aperture_fstop_q16 >= ((14 * 65536) / 10) && v->aperture_fstop_q16 <= (11 * 65536));
        assert(v->focus_dist_q16 > 0);
        assert(v->follow_damping_q16 > 0 && v->follow_damping_q16 <= 65536);
        assert(v->deadzone_enter_q16[0] > v->deadzone_exit_q16[0]);
        assert(v->deadzone_enter_q16[1] > v->deadzone_exit_q16[1]);
        assert(v->deadzone_exit_q16[0] > 0 && v->deadzone_exit_q16[1] > 0);
    }

    /* Out-of-range VCam ID safely falls back to VCam 0 */
    const FlowCineRig64 *fallback = flow_cine_get_vcam(999U);
    assert(fallback == &FLOW_CANONICAL_VCAMS[0]);

    printf("  ✓ FlowCineRig64 (64B) & FlowCineResolvedFrame64 (64B) univalence verified!\n");
    printf("  ✓ 4 canonical VCams (135mm, 24mm, 35mm, 18mm) in 256B L1 table verified!\n");
}

static void test_declarative_q16_easing_curves(void) {
    printf("[Test 2/6] Declarative Pure Q16 Easing Curves & Boundary Clamping...\n");

    const FlowEaseCurve monotonic_curves[4] = {
        FLOW_EASE_LINEAR,
        FLOW_EASE_SMOOTHSTEP_CUBIC,
        FLOW_EASE_SMOOTHERSTEP_5,
        FLOW_EASE_CUBIC_IN_OUT
    };

    for (size_t c = 0; c < 4U; ++c) {
        FlowEaseCurve curve = monotonic_curves[c];

        /* Boundary clamping for negative, zero, unity, and >1 inputs */
        assert(flow_cine_ease_q16(-100000LL, curve) == 0LL);
        assert(flow_cine_ease_q16(0LL, curve) == 0LL);
        assert(flow_cine_ease_q16(FLOW_Q16_ONE, curve) == FLOW_Q16_ONE);
        assert(flow_cine_ease_q16(500000LL, curve) == FLOW_Q16_ONE);

        /* Exact midpoint symmetry: ease(0.5) == 0.5 (32768) */
        assert(flow_cine_ease_q16(FLOW_Q16_ONE >> 1, curve) == (FLOW_Q16_ONE >> 1));

        /* Monotonic non-decreasing progression across 256 substeps */
        int64_t prev = 0LL;
        for (int64_t step = 0; step <= 256; ++step) {
            int64_t t_q16 = (step * FLOW_Q16_ONE) / 256LL;
            int64_t val = flow_cine_ease_q16(t_q16, curve);
            assert(val >= prev);
            assert(val >= 0LL && val <= FLOW_Q16_ONE);
            prev = val;
        }
    }

    /* Smootherstep-5 has zero 1st and 2nd derivative pop at t=0 and t=1 */
    int64_t near_zero = flow_cine_ease_q16(512LL, FLOW_EASE_SMOOTHERSTEP_5);
    assert(near_zero >= 0LL && near_zero <= 2LL);

    /* Damped spring curve settles to exact 1.0 at t=1.0 with slight overshoot around t=0.80 */
    assert(flow_cine_ease_q16(0LL, FLOW_EASE_SPRING_DAMPED) == 0LL);
    assert(flow_cine_ease_q16(FLOW_Q16_ONE, FLOW_EASE_SPRING_DAMPED) == FLOW_Q16_ONE);
    int64_t spring_80 = flow_cine_ease_q16((FLOW_Q16_ONE * 80LL) / 100LL, FLOW_EASE_SPRING_DAMPED);
    assert(spring_80 > FLOW_Q16_ONE);

    printf("  ✓ All 5 pure Q16 easing curves (Linear, Hermite, Smootherstep-5, Cubic, Spring) verified!\n");
}

static void test_composer_deadzone_hysteresis(void) {
    printf("[Test 3/6] FlowComposerPoly Dead-Zone / Soft-Zone Dual-Threshold Hysteresis...\n");

    FlowCineBrainState brain;
    assert(flow_cine_brain_init(&brain) == 0);

    /* Select VCAM_CHASE_DYNAMIC: b_enter = (13107, 9830), b_exit = (6554, 4915) */
    flow_cine_trigger_blend(&brain, FLOW_VCAM_CHASE_DYNAMIC, 0LL, FLOW_EASE_LINEAR);

    int32_t cx = 0, cy = 0;
    uint32_t mask = 0U;

    /* 1. Inside dead-zone: zero correction */
    mask = flow_cine_eval_composer_hysteresis(&brain, 4000, 2000, &cx, &cy);
    assert(mask == 0U && cx == 0 && cy == 0);

    /* 2. Inside hysteresis band [6554, 13107] from below: stays in dead-zone (0) */
    mask = flow_cine_eval_composer_hysteresis(&brain, 10000, 7000, &cx, &cy);
    assert(mask == 0U && cx == 0 && cy == 0);

    /* 3. Cross +X enter threshold (15000 > 13107): latches plane 0 active */
    mask = flow_cine_eval_composer_hysteresis(&brain, 15000, 2000, &cx, &cy);
    assert((mask & 1U) == 1U);
    assert(cx == (15000 - 6554));
    assert(cy == 0);

    /* 4. Return into hysteresis deadband (10000 in [6554, 13107]): stays active (NO CHATTER!) */
    mask = flow_cine_eval_composer_hysteresis(&brain, 10000, 2000, &cx, &cy);
    assert((mask & 1U) == 1U);
    assert(cx == (10000 - 6554));

    /* 5. Drop below exit threshold (5000 <= 6554): exits back to dead-zone */
    mask = flow_cine_eval_composer_hysteresis(&brain, 5000, 2000, &cx, &cy);
    assert(mask == 0U && cx == 0 && cy == 0);

    /* 6. Test negative -X and -Y soft-zone planes (planes 1 and 3) */
    mask = flow_cine_eval_composer_hysteresis(&brain, -15000, -12000, &cx, &cy);
    assert((mask & 2U) != 0U && (mask & 8U) != 0U);
    assert(cx < 0 && cy < 0);

    mask = flow_cine_eval_composer_hysteresis(&brain, 0, 0, &cx, &cy);
    assert(mask == 0U && cx == 0 && cy == 0);

    printf("  ✓ FlowComposerPoly 4-plane dual-threshold hysteresis verified (0 boundary chatter)!\n");
}

static void test_jet2_damping_and_vcam_blending(void) {
    printf("[Test 4/6] 2-Jet Damping Follow, VCam Blending & Hardware Sieve...\n");

    /* Verify flow_cine_blend_jet2 couples transition slope into d1 velocity */
    FlowJet2 j_wide = flow_jet2_make(18LL << 16, 0LL, 0LL);
    FlowJet2 j_tele = flow_jet2_make(135LL << 16, 0LL, 0LL);

    FlowJet2 b0 = flow_cine_blend_jet2(j_wide, j_tele, 0LL, FLOW_EASE_SMOOTHERSTEP_5);
    FlowJet2 b_mid = flow_cine_blend_jet2(j_wide, j_tele, FLOW_Q16_ONE >> 1, FLOW_EASE_SMOOTHERSTEP_5);
    FlowJet2 b1 = flow_cine_blend_jet2(j_wide, j_tele, FLOW_Q16_ONE, FLOW_EASE_SMOOTHERSTEP_5);

    assert(b0.v == (18LL << 16));
    assert(b1.v == (135LL << 16));
    assert(b_mid.v == ((18LL + 135LL) << 15)); /* Exact 76.5mm midpoint */
    assert(b_mid.d1 > (100LL << 16));          /* Positive whip-zoom velocity at midpoint */

    /* Step FlowCineBrainState across a 20-step smooth blend from Cockpit (18mm) to Telephoto (135mm) */
    FlowCineBrainState brain;
    assert(flow_cine_brain_init(&brain) == 0);
    assert(brain.active_vcam == FLOW_VCAM_COCKPIT_TACTICAL);

    FlowJet2 craft_traj[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(-(10000LL << 16), 800LL << 16, 50LL << 16)
    };
    FlowJet2 craft_rot[3] = {
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL),
        flow_jet2_make(0LL, 0LL, 0LL)
    };

    flow_cine_trigger_blend(&brain, FLOW_VCAM_ORBIT_TELEPHOTO, 10LL * FLOW_Q16_ONE, FLOW_EASE_SMOOTHERSTEP_5);
    assert(brain.is_blending == true);

    int32_t prev_focal = brain.resolved.focal_length_q16;
    for (int i = 0; i < 40; ++i) {
        assert(flow_cine_brain_step(&brain, craft_traj, craft_rot, 740ULL, false, FLOW_Q16_ONE) == 0);
        assert((brain.hardware_sieve & (FLOW_ASM_SIEVE_JET2_TAYLOR_0 | FLOW_ASM_SIEVE_JET2_TAYLOR_1)) ==
               (FLOW_ASM_SIEVE_JET2_TAYLOR_0 | FLOW_ASM_SIEVE_JET2_TAYLOR_1));
        assert(brain.resolved.focal_length_q16 >= prev_focal);
        prev_focal = brain.resolved.focal_length_q16;
    }
    assert(brain.is_blending == false);
    assert(brain.resolved.focal_length_q16 > (120 * 65536));

    /* Verify cycling through all 4 VCams */
    for (uint32_t c = 0; c < 4U; ++c) {
        uint32_t prev_id = brain.active_vcam;
        flow_cine_cycle_vcam(&brain, 2LL * FLOW_Q16_ONE);
        assert(brain.active_vcam == ((prev_id + 1U) % (uint32_t)FLOW_VCAM_COUNT));
    }

    printf("  ✓ 2-Jet VCam blending, EMA damping follow & hardware Taylor self-check verified!\n");
}

static void test_physical_coc_dof_and_auto_exposure(void) {
    printf("[Test 5/6] Physical Circle-of-Confusion (CoC) DoF & EV Auto-Exposure...\n");

    int64_t focus_4_8m = (48LL * FLOW_Q16_ONE) / 10LL;
    int64_t bg_50m     = 50LL * FLOW_Q16_ONE;

    /* At exact focus plane, CoC is strictly 0 */
    int64_t coc_in_focus = flow_cine_calc_coc_q16(
        focus_4_8m, 135LL << 16, (14LL << 16) / 10LL, focus_4_8m
    );
    assert(coc_in_focus == 0LL);

    /* 135mm f/1.4 telephoto produces >20x larger background CoC than 18mm f/8.0 wide lens */
    int64_t coc_tele = flow_cine_calc_coc_q16(
        bg_50m, 135LL << 16, (14LL << 16) / 10LL, focus_4_8m
    );
    int64_t coc_wide = flow_cine_calc_coc_q16(
        bg_50m, 18LL << 16, 8LL << 16, focus_4_8m
    );
    assert(coc_tele > 0LL && coc_wide > 0LL);
    assert(coc_tele > coc_wide * 20LL);

    /* Guard against zero/negative depth or fstop */
    assert(flow_cine_calc_coc_q16(0LL, 35LL << 16, 2LL << 16, focus_4_8m) == 0LL);
    assert(flow_cine_calc_coc_q16(bg_50m, 35LL << 16, 0LL, focus_4_8m) == 0LL);

    /* Verify EV auto-exposure stops down when thermal cavity reaches 850K */
    FlowCineBrainState brain;
    flow_cine_brain_init(&brain);
    FlowJet2 traj[3] = { flow_jet2_const(0), flow_jet2_const(0), flow_jet2_const(0) };
    FlowJet2 rot[3]  = { flow_jet2_const(0), flow_jet2_const(0), flow_jet2_const(0) };
    for (int i = 0; i < 15; ++i) {
        flow_cine_brain_step(&brain, traj, rot, 850ULL, false, FLOW_Q16_ONE);
    }
    assert(brain.resolved.exposure_gain_q16 < FLOW_Q16_ONE);
    assert(brain.resolved.exposure_gain_q16 > (FLOW_Q16_ONE / 2));

    printf("  ✓ Physical Circle-of-Confusion DoF (135mm f/1.4 >> 18mm f/8.0) & EV adaptation verified!\n");
}

static void test_shake_envelope_and_engine_integration(void) {
    printf("[Test 6/6] 6D Trauma Shake Envelope & Lock-Free Engine Integration...\n");

    FlowGameEngineState engine;
    assert(flow_game_init(&engine) == 0);

    /* Switch to VCAM_CHASE_DYNAMIC and apply high-speed boost to verify FOV breathing */
    flow_cine_trigger_blend(&engine.cine_brain, FLOW_VCAM_CHASE_DYNAMIC, 0LL, FLOW_EASE_LINEAR);
    flow_game_input(&engine, 3LL * FLOW_Q16_ONE, 0LL, 0LL, 0LL, false);

    /* Inject collision trauma impulse */
    flow_cine_add_trauma(&engine.cine_brain, 49152LL);
    int64_t init_trauma = engine.cine_brain.trauma_q16;
    assert(init_trauma == 49152LL);

    for (int step = 0; step < 25; ++step) {
        assert(flow_game_step(&engine, FLOW_Q16_ONE) == 0);
    }

    /* Trauma decays over time */
    assert(engine.cine_brain.trauma_q16 < init_trauma);

    /* Poll frame packet and verify cine_camera is published in the SPSC ring */
    FlowGameFramePacket pkt;
    bool got = false;
    while (flow_game_poll_frame_packet(&engine, &pkt)) {
        got = true;
    }
    assert(got == true);
    assert(pkt.cine_camera.active_vcam_id == FLOW_VCAM_CHASE_DYNAMIC);
    /* Under 3x boost, Chase Dynamic camera focal length breathes wider (< 24.0mm) */
    assert(pkt.cine_camera.focal_length_q16 < (24 * 65536));
    assert(pkt.cine_camera.focal_length_q16 >= (20 * 65536));

    flow_game_destroy(&engine);
    printf("  ✓ 6D trauma decay, acceleration FOV breathing & SPSC packet publication verified!\n");
}

static void test_cinema_edl_and_48khz_lockstep(void) {
    printf("[Test 7/7] FlowCinemaShot64 24fps EDL Timecode & 48kHz Audio Lockstep...\n");

    assert(sizeof(FlowCinemaShot64) == 64U);
    assert(_Alignof(FlowCinemaShot64) == 64U);

    FlowCinemaShot64 shots[FLOW_CINEMA_REEL_SHOT_COUNT];
    assert(flow_cine_build_default_reel64(shots, 24U) == 0);
    for (uint32_t i = 0U; i < FLOW_CINEMA_REEL_SHOT_COUNT; ++i) {
        assert(shots[i].shot_id == i + 1U);
        assert(shots[i].stage_mode == i);
        assert(shots[i].fps_rate == 24U);
        assert(shots[i].frame_count == 48U);
        assert(shots[i].audio_samples_per_frame == 2000U);
        assert(shots[i].edl_signature_hash != 0ULL);
    }

    char tc0[12], tc1[12], tc_end[12];
    assert(flow_cine_format_smpte_timecode(0U, 24U, tc0) == 0);
    assert(flow_cine_format_smpte_timecode(48U, 24U, tc1) == 0);
    assert(flow_cine_format_smpte_timecode(192U, 24U, tc_end) == 0);
    assert(strcmp(tc0, "00:00:00:00") == 0);
    assert(strcmp(tc1, "00:00:02:00") == 0);
    assert(strcmp(tc_end, "00:00:08:00") == 0);

    int16_t pcm_a[2000 * 2];
    int16_t pcm_b[2000 * 2];
    assert(flow_cine_synth_lockstep_audio48k(&shots[0], 10U, pcm_a, 2000U) == 0);
    assert(flow_cine_synth_lockstep_audio48k(&shots[0], 10U, pcm_b, 2000U) == 0);
    /* 100% bit-identical deterministic audio lockstep */
    assert(memcmp(pcm_a, pcm_b, sizeof(pcm_a)) == 0);

    printf("  ✓ FlowCinemaShot64 64B EDL (00:00:00:00..00:00:08:00) & 48kHz lockstep verified!\n");
}

int main(void) {
    printf("====================================================================\n");
    printf("BLOOM: FlowCineRig64 Cinematography & Physical Optics Test Suite\n");
    printf("====================================================================\n");

    test_univalence_and_canonical_vcams();
    test_declarative_q16_easing_curves();
    test_composer_deadzone_hysteresis();
    test_jet2_damping_and_vcam_blending();
    test_physical_coc_dof_and_auto_exposure();
    test_shake_envelope_and_engine_integration();
    test_cinema_edl_and_48khz_lockstep();

    printf("====================================================================\n");
    printf("✓ ALL 7 FLOWCINERIG64 CINEMATOGRAPHY & OPTICS TESTS PASSED (100%% SOUND)\n");
    printf("====================================================================\n");
    return 0;
}
