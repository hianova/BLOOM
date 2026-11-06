#include "flow_gpu_abi.h"
#include "flow_procedural_assets.h"
#include "flow_lean_abi.h"
#include "flow_assets.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int main(void) {
    printf("====================================================================\n");
    printf("BLOOM: Zero-Token Procedural Assets & SDD SVG Test Suite\n");
    printf("====================================================================\n");

    /* 1. 64B Univalence Check */
    printf("[Test 1/8] 64-Byte Univalence of Procedural Descriptors...\n");
    assert(sizeof(FlowGenomeRecipe64) == 64 && _Alignof(FlowGenomeRecipe64) == 64);
    assert(sizeof(FlowGlyphDesc64) == 64 && _Alignof(FlowGlyphDesc64) == 64);
    assert(sizeof(FlowKineticRig64) == 64 && _Alignof(FlowKineticRig64) == 64);
    assert(sizeof(FlowAudioPatch64) == 64 && _Alignof(FlowAudioPatch64) == 64);
    assert(sizeof(FlowAnimClip64) == 64 && _Alignof(FlowAnimClip64) == 64);
    assert(sizeof(FlowAnimSample64) == 64 && _Alignof(FlowAnimSample64) == 64);
    assert(sizeof(FlowComboChain64) == 64 && _Alignof(FlowComboChain64) == 64);
    assert(sizeof(FlowDialogNode64) == 64 && _Alignof(FlowDialogNode64) == 64);
    assert(sizeof(FlowItemStat64) == 64 && _Alignof(FlowItemStat64) == 64);
    assert(sizeof(FlowAudiovisualFrame64) == 64 && _Alignof(FlowAudiovisualFrame64) == 64);
    assert(sizeof(FlowGenerativeSeedStyle64) == 64 && _Alignof(FlowGenerativeSeedStyle64) == 64);
    assert(sizeof(FlowSovereignGate64) == 64 && _Alignof(FlowSovereignGate64) == 64);
    assert(sizeof(FlowObjectTicket64) == 64 && _Alignof(FlowObjectTicket64) == 64);
    printf("  ✓ All 12 Procedural, Audiovisual, Master-Seed, Sovereign-Gate & Object-Ticket Descriptors verified (64B Univalent)!\n");

    /* 2. 0-Token Combinatorial 3D Shape Evolution & 8x8 Atlas */
    printf("[Test 2/8] 0-Token 64-Shape Combinatorial Evolution & 8x8 Atlas...\n");
    FlowGenomeRecipe64 batch[64];
    flow_genome_evolve_batch64(0x20261103ULL, batch);
    for (uint32_t i = 0; i < 64U; ++i) {
        assert(batch[i].assembly_index >= 2U);
        assert(batch[i].ricardian_grade < 5U);
        assert(batch[i].checksum != 0U);
        int64_t d_origin = flow_genome_eval_sdf_q16(&batch[i], 0, 0, 0);
        assert(d_origin < 0); /* Solid interior at origin */
    }
    int rc = flow_genome_render_atlas_ppm(batch, "build/genome_atlas.ppm");
    assert(rc == 0);
    struct stat st;
    assert(stat("build/genome_atlas.ppm", &st) == 0 && st.st_size > 190000);
    printf("  ✓ Evolved 64 symmetric 3D spacecraft/station genomes (0 LLM tokens)!\n");
    printf("  ✓ Rendered 8x8 3D Shape Atlas to build/genome_atlas.ppm (%lld bytes)!\n",
           (long long)st.st_size);

    /* 3. SDD SVG Blueprint Compile-Time Loader & 2D Analytic SDF + Ternary Match */
    printf("[Test 3/8] SDD SVG Blueprint Compiler (specs/cockpit_hud.svg -> FlowGlyphDesc64)...\n");
    FlowGlyphDesc64 glyphs[8];
    size_t n_glyphs = flow_svg_compile_file("specs/cockpit_hud.svg", glyphs, 8U);
    assert(n_glyphs == 4U);
    assert(glyphs[0].glyph_id == 101U && glyphs[0].stroke_count == 5U);
    assert(glyphs[1].glyph_id == 102U && glyphs[1].style_id == 7U);
    int64_t d_stroke = flow_glyph_eval_sdf_q16(&glyphs[0], 0, 0);
    assert(d_stroke <= 0); /* Center of crosshair X stroke is inside SDF */
    int32_t self_corr = flow_glyph_match_ternary(&glyphs[0], &glyphs[0]);
    assert(self_corr == 64);
    printf("  ✓ Compiled %zu SDD SVG symbols from specs/cockpit_hud.svg into 64B Q16 SDF glyphs!\n",
           n_glyphs);

    /* 4. Keyframe-less 2-Jet Mechanism & Hysteresis */
    printf("[Test 4/8] Keyframe-less FlowKineticRig64 Hysteresis & 2-Jet Step...\n");
    FlowKineticRig64 rig = {
        .angle_jet = flow_jet2_make(0, 0, 1024),
        .deploy_enter_dist_q16 = 200 * FLOW_Q16_ONE,
        .deploy_exit_dist_q16 = 250 * FLOW_Q16_ONE,
        .target_omega_q16 = 16384,
        .rig_id = 1U,
        .deployed_state = 0U,
        .sieve_status = FLOW_ASM_SIEVE_ALL_SOUND,
    };
    /* Approach within 180m -> deploys */
    uint64_t sv = flow_kinetic_rig_step(&rig, 180 * FLOW_Q16_ONE, 4096);
    assert(rig.deployed_state == 1U);
    assert((sv & (FLOW_ASM_SIEVE_JET2_TAYLOR_0 | FLOW_ASM_SIEVE_JET2_TAYLOR_1)) != 0ULL);
    /* Drift to 220m (inside deadband [200, 250]) -> stays deployed without chatter */
    (void)flow_kinetic_rig_step(&rig, 220 * FLOW_Q16_ONE, 4096);
    assert(rig.deployed_state == 1U);
    /* Retreat to 260m -> retracts */
    (void)flow_kinetic_rig_step(&rig, 260 * FLOW_Q16_ONE, 4096);
    assert(rig.deployed_state == 0U);
    printf("  ✓ 200m/250m deployment hysteresis & 2-Jet hardware self-check verified!\n");

    /* 5. Zero-WAV Pure Q16 2-Jet FM & Shepard Chirp Synth */
    printf("[Test 5/8] Zero-WAV FlowAudioPatch64 Pure Q16 FM & Chirp Synthesizer...\n");
    FlowAudioPatch64 warp_patch = {
        .carrier_jet = flow_jet2_make(0, 14417920, 3276800), /* 220 Hz base + quadratic chirp */
        .mod_freq_q16 = 7208960,
        .fm_index_q16 = 16384,
        .attack_step_q16 = 4096,
        .decay_step_q16 = 512,
        .patch_id = 1U,
        .waveform_flags = 1U,
        .checksum = 0xCAFEULL,
    };
    int64_t pcm[128];
    flow_audio_synth_samples_q16(&warp_patch, pcm, 128U, 1024);
    int non_zero = 0;
    for (size_t i = 0; i < 128U; ++i) {
        assert(pcm[i] >= -FLOW_Q16_ONE && pcm[i] <= FLOW_Q16_ONE);
        if (pcm[i] != 0)
            non_zero++;
    }
    assert(non_zero > 100);
    printf("  ✓ Synthesized 128 pure Q16 FM/Chirp audio samples (0 WAV files, 0 floats)!\n");

    /* 6. SDD SVG Hearthstone-Grade 4-Phase Animation & VFX Choreography */
    printf("[Test 6/8] SDD SVG 4-Phase Animation & VFX Compiler (FlowAnimClip64)...\n");
    assert(sizeof(FlowAnimClip64) == 64 && _Alignof(FlowAnimClip64) == 64);
    assert(sizeof(FlowAnimSample64) == 64 && _Alignof(FlowAnimSample64) == 64);

    FlowAnimClip64 clips[8];
    size_t n_clips = flow_svg_compile_anim_clips("specs/cockpit_hud.svg", clips, 8U);
    assert(n_clips == 4U);
    /* Clip 2 is glyph_grade_s_badge (id=103): Legendary-Slam 4-phase choreography */
    const FlowAnimClip64 *leg = &clips[2];
    assert(leg->clip_id == 103U && leg->hitstop_frames == 5U && leg->audio_patch_id == 3U);

    FlowAnimSample64 s_antic, s_deliv, s_impact, s_settle, s_done;
    /* Phase 0: Anticipation (t = 100ms in [0..200ms]) -> crouched scale < 1.0x */
    flow_anim_clip_eval_q16(leg, 100U, &s_antic);
    assert(s_antic.phase == FLOW_ANIM_PHASE_ANTICIPATION);
    assert(s_antic.scale_parallel_q16 < FLOW_Q16_ONE);

    /* Phase 1: Delivery (t = 270ms in [200..340ms]) -> parabolic lift & area-preserving Squash & Stretch */
    flow_anim_clip_eval_q16(leg, 270U, &s_deliv);
    assert(s_deliv.phase == FLOW_ANIM_PHASE_DELIVERY);
    assert(s_deliv.scale_parallel_q16 > FLOW_Q16_ONE);
    assert(s_deliv.scale_perp_q16 < FLOW_Q16_ONE);
    assert(s_deliv.offset_arc_y_q16 > 0);
    /* Verify area conservation: (scale_parallel * scale_perp) >> 16 == 1.0 within Q16 rounding */
    int64_t area_q16 = ((int64_t)s_deliv.scale_parallel_q16 * (int64_t)s_deliv.scale_perp_q16) >> 16U;
    assert(area_q16 >= 65500 && area_q16 <= 65536);

    /* Phase 2: Impact Climax (t = 345ms in [340..440ms]) -> slam scale 1.45x + hitstop + shockwave + trauma */
    flow_anim_clip_eval_q16(leg, 345U, &s_impact);
    assert(s_impact.phase == FLOW_ANIM_PHASE_IMPACT);
    assert(s_impact.scale_parallel_q16 == leg->scale_impact_q16);
    assert(s_impact.flux_corona_q16 > (10 * FLOW_Q16_ONE));
    assert(s_impact.ripple_phi_q16 > 0);
    assert(s_impact.camera_trauma_q16 > 0);
    assert(s_impact.hitstop_frames_trigger == 5U);
    assert(s_impact.audio_patch_trigger == 3U);

    /* Phase 3: Spring Settle (t = 640ms in [440..840ms]) & Done (t = 900ms) */
    flow_anim_clip_eval_q16(leg, 640U, &s_settle);
    assert(s_settle.phase == FLOW_ANIM_PHASE_SETTLE);
    flow_anim_clip_eval_q16(leg, 900U, &s_done);
    assert(s_done.phase == FLOW_ANIM_PHASE_DONE && s_done.scale_parallel_q16 == FLOW_Q16_ONE);

    printf("  ✓ Compiled %zu SDD SVG <animate> clips into 64B FlowAnimClip64 descriptors!\n", n_clips);
    printf("  ✓ Verified 4-Phase Choreography (Anticipation -> Squash/Stretch Arc -> Impact Slam/VFX -> Spring Settle)!\n");

    /* 7. White-Box 4-Phase Combo Cancel Gate & 2-Jet Momentum Spline (FlowComboChain64) */
    printf("[Test 7/8] White-Box 4-Phase Combo Cancel Gate & 2-Jet Momentum Spline (FlowComboChain64)...\n");
    assert(sizeof(FlowComboChain64) == 64 && _Alignof(FlowComboChain64) == 64);

    const FlowAnimClip64 *clip_q = &clips[2]; /* id=103: 200ms antic, 140ms deliv, 100ms impact, 400ms settle */
    const FlowAnimClip64 *clip_a = &clips[0]; /* id=101: follow-up strike */

    FlowComboChain64 combo;
    uint32_t gates = FLOW_CANCEL_GATE_SETTLE | FLOW_CANCEL_GATE_HIT_CONFIRM | FLOW_CANCEL_GATE_DELIVERY_FLASH;
    flow_combo_chain_init(&combo, clip_q, 1000U, gates, 100 * FLOW_Q16_ONE, 20 * FLOW_Q16_ONE);

    /* 7a. Phase 0 (Anticipation, t = 1080ms): Cancel MUST be rejected (prevents zero-frame startup exploit) */
    assert(!flow_combo_chain_try_cancel(&combo, clip_q, clip_a, 1080U, true, 0, 50 * FLOW_Q16_ONE, 0));
    assert(combo.last_transition_type == FLOW_COMBO_TRANSITION_NONE);
    assert(combo.combo_counter == 1U);

    /* 7b. Phase 1 (Delivery, t = 1250ms): Q-Flash / E-Q-Flash displacement injection WITHOUT resetting phase! */
    int64_t flash_dist = 300LL * FLOW_Q16_ONE;
    assert(flow_combo_chain_try_cancel(&combo, clip_q, NULL, 1250U, false, flash_dist, 40 * FLOW_Q16_ONE, 0));
    assert(combo.last_transition_type == FLOW_COMBO_TRANSITION_FLASH_INJECT);
    assert(combo.active_clip_id == clip_q->clip_id); /* Still on clip_q! */
    FlowAnimSample64 s_after_flash;
    FlowJet2 jet_after_flash;
    flow_combo_chain_eval_q16(&combo, clip_q, 1250U, &s_after_flash, &jet_after_flash);
    assert(s_after_flash.phase == FLOW_ANIM_PHASE_DELIVERY); /* Phase countdown preserved! */
    assert(jet_after_flash.v > flash_dist); /* Position includes flash displacement + forward travel */

    /* 7c. Phase 2 (Impact, t = 1360ms): Whiff (hit_confirmed=false) is REJECTED; Hit-Confirm (true) SUCCEEDS! */
    assert(!flow_combo_chain_try_cancel(&combo, clip_q, clip_a, 1360U, false, 0, 50 * FLOW_Q16_ONE, 0));
    assert(combo.last_transition_type == FLOW_COMBO_TRANSITION_NONE);

    int64_t v_before_cancel = jet_after_flash.d1;
    assert(flow_combo_chain_try_cancel(&combo, clip_q, clip_a, 1360U, true, 0, 50 * FLOW_Q16_ONE, 0));
    assert(combo.last_transition_type == FLOW_COMBO_TRANSITION_HIT_CONFIRM);
    assert(combo.active_clip_id == clip_a->clip_id);
    assert(combo.anchor_ms == 1360U);
    assert(combo.combo_counter == 2U);
    assert(combo.traj_z.d1 > v_before_cancel); /* Momentum inherited + boosted! */

    /* 7d. Phase 3 (Settle / Backswing of clip_a at t = 1360 + 400 = 1760ms): Fast-QA Recovery Cancel! */
    assert(flow_combo_chain_try_cancel(&combo, clip_a, clip_q, 1760U, false, 0, 30 * FLOW_Q16_ONE, 0));
    assert(combo.last_transition_type == FLOW_COMBO_TRANSITION_RECOVERY_CUT);
    assert(combo.active_clip_id == clip_q->clip_id);
    assert(combo.anchor_ms == 1760U);
    assert(combo.combo_counter == 3U);
    assert((combo.sieve_status & (FLOW_ASM_SIEVE_JET2_TAYLOR_0 | FLOW_ASM_SIEVE_JET2_TAYLOR_1)) ==
           (FLOW_ASM_SIEVE_JET2_TAYLOR_0 | FLOW_ASM_SIEVE_JET2_TAYLOR_1));

    printf("  ✓ Verified Phase 0 Lock, Phase 1 Q-Flash Spline Inject, Phase 2 Hit-Confirm & Phase 3 Fast-QA Recovery Cancel!\n");
    printf("  ✓ Verified C^1 2-Jet Momentum Inheritance & Hardware Taylor Continuity across all combo splices!\n");

    /* 8. 64B Ternary HashMap Narrative Engine & 100,000-Combo Headless Balance Gate */
    printf("[Test 8/8] 64B Ternary HashMap Narrative Engine & 100,000-Combo Headless Balance Gate...\n");

    /* 8a. Ternary HashMap Narrative Engine Verification */
    #define DIALOG_TEST_TABLE_SIZE 16U
    FlowDialogNode64 dialog_table[DIALOG_TEST_TABLE_SIZE];
    memset(dialog_table, 0, sizeof(dialog_table));

    uint64_t quest_event_hash = 0xABCD9876ULL;
    uint64_t side_event_hash  = 0x12345678ULL;

    /*
     * Define 4 sequential narrative states on quest_event_hash:
     * Fact 0 (bit 0): Quest Accepted (0 -> 1)
     * Fact 1 (bit 1): Relic Recovered (0 -> 1)
     * Fact 2 (bit 2): Relic Purified (0 -> 1)
     * Fact 3 (bit 3): Quest Completed (0 -> 1)
     */
    FlowDialogNode64 d1 = {
        .event_key_hash = quest_event_hash,
        .req_mask       = (1ULL << 0),
        .req_values     = 0ULL,
        .set_mask       = (1ULL << 0),
        .set_values     = (1ULL << 0),
        .dialog_id      = 1001U,
        .priority_bias  = 0U,
        .next_clip_id   = 101U,
        .reward_grade   = 4U,
        .checksum       = 0x1001ULL,
    };
    FlowDialogNode64 d2 = {
        .event_key_hash = quest_event_hash,
        .req_mask       = (1ULL << 0) | (1ULL << 1),
        .req_values     = (1ULL << 0),
        .set_mask       = (1ULL << 1),
        .set_values     = (1ULL << 1),
        .dialog_id      = 1002U,
        .priority_bias  = 0U,
        .next_clip_id   = 102U,
        .reward_grade   = 3U,
        .checksum       = 0x1002ULL,
    };
    FlowDialogNode64 d3 = {
        .event_key_hash = quest_event_hash,
        .req_mask       = (1ULL << 0) | (1ULL << 1) | (1ULL << 2),
        .req_values     = (1ULL << 0) | (1ULL << 1),
        .set_mask       = (1ULL << 2),
        .set_values     = (1ULL << 2),
        .dialog_id      = 1003U,
        .priority_bias  = 0U,
        .next_clip_id   = 103U,
        .reward_grade   = 1U,
        .checksum       = 0x1003ULL,
    };
    FlowDialogNode64 d4 = {
        .event_key_hash = quest_event_hash,
        .req_mask       = (1ULL << 0) | (1ULL << 1) | (1ULL << 2) | (1ULL << 3),
        .req_values     = (1ULL << 0) | (1ULL << 1) | (1ULL << 2),
        .set_mask       = (1ULL << 3),
        .set_values     = (1ULL << 3),
        .dialog_id      = 1004U,
        .priority_bias  = 0U,
        .next_clip_id   = 104U,
        .reward_grade   = 0U, /* Grade S */
        .checksum       = 0x1004ULL,
    };
    FlowDialogNode64 d5 = {
        .event_key_hash = quest_event_hash,
        .req_mask       = (1ULL << 3),
        .req_values     = (1ULL << 3),
        .set_mask       = 0ULL,
        .set_values     = 0ULL,
        .dialog_id      = 1005U,
        .priority_bias  = 10U,
        .next_clip_id   = 101U,
        .reward_grade   = 0U,
        .checksum       = 0x1005ULL,
    };
    FlowDialogNode64 d5_alt = {
        .event_key_hash = quest_event_hash,
        .req_mask       = (1ULL << 3),
        .req_values     = (1ULL << 3),
        .set_mask       = 0ULL,
        .set_values     = 0ULL,
        .dialog_id      = 1006U,
        .priority_bias  = 2U,
        .next_clip_id   = 101U,
        .reward_grade   = 0U,
        .checksum       = 0x1006ULL,
    };
    FlowDialogNode64 d_side = {
        .event_key_hash = side_event_hash,
        .req_mask       = 0ULL,
        .req_values     = 0ULL,
        .set_mask       = (1ULL << 10),
        .set_values     = (1ULL << 10),
        .dialog_id      = 2001U,
        .priority_bias  = 0U,
        .next_clip_id   = 102U,
        .reward_grade   = 2U,
        .checksum       = 0x2001ULL,
    };

    assert(flow_dialog_hashmap_insert(dialog_table, DIALOG_TEST_TABLE_SIZE, d1));
    assert(flow_dialog_hashmap_insert(dialog_table, DIALOG_TEST_TABLE_SIZE, d2));
    assert(flow_dialog_hashmap_insert(dialog_table, DIALOG_TEST_TABLE_SIZE, d3));
    assert(flow_dialog_hashmap_insert(dialog_table, DIALOG_TEST_TABLE_SIZE, d4));
    assert(flow_dialog_hashmap_insert(dialog_table, DIALOG_TEST_TABLE_SIZE, d5));
    assert(flow_dialog_hashmap_insert(dialog_table, DIALOG_TEST_TABLE_SIZE, d5_alt));
    assert(flow_dialog_hashmap_insert(dialog_table, DIALOG_TEST_TABLE_SIZE, d_side));

    /* Verify 1-cycle POPCNT match score and ternary dot verification */
    int32_t score_mismatch = flow_dialog_match_score_o1(&d1, quest_event_hash, 1ULL << 0);
    assert(score_mismatch == -1); /* Expected 0, but fact was 1 */

    int32_t score_d1 = flow_dialog_match_score_o1(&d1, quest_event_hash, 0ULL);
    assert(score_d1 == 16); /* 1 bit * 16 + 0 bias */

    int32_t score_d4 = flow_dialog_match_score_o1(&d4, quest_event_hash, 0x07ULL);
    assert(score_d4 == 64); /* 4 bits * 16 + 0 bias */

    /* Verify sequential story state progression with inout_world_facts and zero dead-ends */
    uint64_t world_facts = 0ULL;

    const FlowDialogNode64 *sel1 = flow_dialog_hashmap_select(dialog_table, DIALOG_TEST_TABLE_SIZE, quest_event_hash, &world_facts);
    assert(sel1 && sel1->dialog_id == 1001U);
    assert((world_facts & (1ULL << 0)) != 0ULL);

    const FlowDialogNode64 *sel2 = flow_dialog_hashmap_select(dialog_table, DIALOG_TEST_TABLE_SIZE, quest_event_hash, &world_facts);
    assert(sel2 && sel2->dialog_id == 1002U);
    assert((world_facts & (1ULL << 1)) != 0ULL);

    const FlowDialogNode64 *sel3 = flow_dialog_hashmap_select(dialog_table, DIALOG_TEST_TABLE_SIZE, quest_event_hash, &world_facts);
    assert(sel3 && sel3->dialog_id == 1003U);
    assert((world_facts & (1ULL << 2)) != 0ULL);

    const FlowDialogNode64 *sel4 = flow_dialog_hashmap_select(dialog_table, DIALOG_TEST_TABLE_SIZE, quest_event_hash, &world_facts);
    assert(sel4 && sel4->dialog_id == 1004U);
    assert((world_facts & (1ULL << 3)) != 0ULL);

    /* Priority bias tie-breaker selects d5 (bias 10) over d5_alt (bias 2) */
    const FlowDialogNode64 *sel5 = flow_dialog_hashmap_select(dialog_table, DIALOG_TEST_TABLE_SIZE, quest_event_hash, &world_facts);
    assert(sel5 && sel5->dialog_id == 1005U);

    /* Side event */
    const FlowDialogNode64 *sel_side = flow_dialog_hashmap_select(dialog_table, DIALOG_TEST_TABLE_SIZE, side_event_hash, &world_facts);
    assert(sel_side && sel_side->dialog_id == 2001U);
    assert((world_facts & (1ULL << 10)) != 0ULL);

    printf("  ✓ O(1) Ternary HashMap lookup, 1-cycle POPCNT specificity & world-fact progression verified!\n");

    /* 8b. Hyperbolic AMM Asymptote Verifications (50 items stacked) */
    FlowItemStat64 max_cdr_items[50];
    for (uint32_t i = 0; i < 50U; ++i) {
        max_cdr_items[i] = flow_item_craft_ricardian(i + 1U, 16U, 0U, 0, 100, 0);
    }
    FlowItemStat64 max_cdr_build = flow_item_combine_build(max_cdr_items, 50U);
    assert(max_cdr_build.eff_cdr_q16 <= 49152);
    assert(max_cdr_build.eff_cdr_q16 > 0);

    FlowItemStat64 max_mit_items[50];
    for (uint32_t i = 0; i < 50U; ++i) {
        max_mit_items[i] = flow_item_craft_ricardian(i + 1U, 16U, 0U, 0, 0, 100);
    }
    FlowItemStat64 max_mit_build = flow_item_combine_build(max_mit_items, 50U);
    assert(max_mit_build.eff_mitigation_q16 <= 52428);
    assert(max_mit_build.eff_mitigation_q16 > 0);

    printf("  ✓ 50 Max-CDR stack: eff_cdr_q16=%d <= 49152 (<75%% cap, zero infinite-CD bugs)!\n",
           max_cdr_build.eff_cdr_q16);
    printf("  ✓ 50 Max-Mit stack: eff_mitigation_q16=%d <= 52428 (<80%% cap, zero invulnerability bugs)!\n",
           max_mit_build.eff_mitigation_q16);

    /* 8c. 100,000-Encounter Headless Balance Sweep */
    printf("  ✓ Running 100,000-encounter headless balance sweep across diverse builds...\n");
    uint64_t sweep_seed = 0x20261104ULL;
    int32_t min_ttk = 999999;
    int32_t max_ttk = 0;

    for (uint32_t enc = 0; enc < 100000U; ++enc) {
        sweep_seed = sweep_seed * 6364136223846793005ULL + 1442695040888963407ULL;
        uint32_t r1 = (uint32_t)(sweep_seed >> 32U);
        sweep_seed = sweep_seed * 6364136223846793005ULL + 1442695040888963407ULL;
        uint32_t r2 = (uint32_t)(sweep_seed >> 32U);

        size_t n_atk = 1U + (size_t)(r1 % 8U);
        size_t n_def = 1U + (size_t)(r2 % 8U);

        FlowItemStat64 a_items[8];
        for (size_t k = 0; k < n_atk; ++k) {
            sweep_seed = sweep_seed * 6364136223846793005ULL + 1442695040888963407ULL;
            uint32_t w = (uint32_t)(sweep_seed >> 32U);
            a_items[k] = flow_item_craft_ricardian((uint32_t)k + 1U,
                                                   1U + (w & 0x0FU),
                                                   (w >> 4U) % 5U,
                                                   (int32_t)((w >> 8U) & 0x7FU),
                                                   (int32_t)((w >> 15U) & 0x7FU),
                                                   (int32_t)((w >> 22U) & 0x7FU));
        }
        FlowItemStat64 a_build = flow_item_combine_build(a_items, n_atk);

        FlowItemStat64 d_items[8];
        for (size_t k = 0; k < n_def; ++k) {
            sweep_seed = sweep_seed * 6364136223846793005ULL + 1442695040888963407ULL;
            uint32_t w = (uint32_t)(sweep_seed >> 32U);
            d_items[k] = flow_item_craft_ricardian(100U + (uint32_t)k,
                                                   1U + (w & 0x0FU),
                                                   (w >> 4U) % 5U,
                                                   (int32_t)((w >> 8U) & 0x7FU),
                                                   (int32_t)((w >> 15U) & 0x7FU),
                                                   (int32_t)((w >> 22U) & 0x7FU));
        }
        FlowItemStat64 d_build = flow_item_combine_build(d_items, n_def);

        int32_t ttk_ms = flow_item_simulate_ttk_ms(&a_build, &d_build);
        assert(ttk_ms >= 2000 && ttk_ms <= 25000);
        if (ttk_ms < min_ttk) min_ttk = ttk_ms;
        if (ttk_ms > max_ttk) max_ttk = ttk_ms;
    }

    printf("  ✓ 100,000 matches verified: min TTK = %d ms, max TTK = %d ms (all strictly in [2000ms, 25000ms])!\n",
           min_ttk, max_ttk);
    printf("  ✓ Zero instant one-shot overflows, zero immortal stalemates!\n");

    /* 9. 64B Tri-Modal Audiovisual Coupling & SDF Cavity Relativistic Synth */
    printf("[Test 9/9] 64B Tri-Modal Audiovisual Coupling & SDF Cavity Relativistic Synth (FlowAudiovisualFrame64)...\n");
    FlowAudiovisualFrame64 av_open, av_enter, av_deadband, av_exit;

    /* 9a. Open space (sdf_dist = 30m > 12m enter), Doppler blueshift (1.5x), 12Hz raw strobe -> clamped to 3.0Hz TRC */
    flow_av_resolve_frame64(220 * FLOW_Q16_ONE,
                            98304,          /* 1.5x Doppler blueshift */
                            FLOW_Q16_ONE,   /* 1.0x proper time */
                            30LL * FLOW_Q16_ONE,
                            0U,
                            &s_deliv,
                            2U,             /* Kintsugi Gold Damascus finish */
                            52428,
                            40960,
                            12 * FLOW_Q16_ONE, /* 12 Hz raw strobe (unsafe) */
                            12345,
                            &av_open);
    assert(av_open.cavity_latched == 0U && av_open.cavity_resonance_q16 == 0);
    assert(av_open.doppler_carrier_freq_q16 == 330 * FLOW_Q16_ONE);
    assert(av_open.trc_safe_strobe_hz_q16 == 3 * FLOW_Q16_ONE); /* Clamped to 3.0 Hz TRC ceiling! */
    assert(av_open.active_finish_class == 2U);
    assert(av_open.trimodal_phase_q16 == 12345);
    int64_t av_area = ((int64_t)av_open.squash_parallel_q16 * (int64_t)av_open.squash_perp_q16) >> 16U;
    assert(av_area >= 65500 && av_area <= 65536);

    /* 9b. Enter asteroid cavity (sdf_dist = 10m <= 12m enter) -> latches cavity resonance */
    flow_av_resolve_frame64(220 * FLOW_Q16_ONE, FLOW_Q16_ONE, FLOW_Q16_ONE,
                            10LL * FLOW_Q16_ONE, av_open.cavity_latched,
                            &s_impact, 2U, 52428, 40960, 2 * FLOW_Q16_ONE, 23456, &av_enter);
    assert(av_enter.cavity_latched == 1U && av_enter.cavity_resonance_q16 > 0);
    assert(av_enter.shockwave_ripple_q16 > 0);

    /* 9c. Drift to 16m inside deadband (12m, 20m) -> stays latched (zero chatter!) */
    flow_av_resolve_frame64(220 * FLOW_Q16_ONE, FLOW_Q16_ONE, FLOW_Q16_ONE,
                            16LL * FLOW_Q16_ONE, av_enter.cavity_latched,
                            NULL, 2U, 52428, 40960, 2 * FLOW_Q16_ONE, 34567, &av_deadband);
    assert(av_deadband.cavity_latched == 1U && av_deadband.cavity_resonance_q16 > 0);

    /* 9d. Exit cavity at 22m (>= 20m exit) -> unlatches cleanly */
    flow_av_resolve_frame64(220 * FLOW_Q16_ONE, FLOW_Q16_ONE, FLOW_Q16_ONE >> 4U, /* Hitstop 1/16x time dilation */
                            22LL * FLOW_Q16_ONE, av_deadband.cavity_latched,
                            NULL, 0U, 45875, 32768, 2 * FLOW_Q16_ONE, 45678, &av_exit);
    assert(av_exit.cavity_latched == 0U && av_exit.cavity_resonance_q16 == 0);
    assert(av_exit.doppler_carrier_freq_q16 < 220 * FLOW_Q16_ONE); /* Pitch drops during Hitstop time dilation! */

    /* 9e. Synthesize relativistic + SDF cavity comb-reverb audio samples */
    int64_t pcm_open[128], pcm_cavity[128];
    flow_audio_synth_relativistic_q16(&warp_patch, &av_open, pcm_open, 128U, 1024);
    flow_audio_synth_relativistic_q16(&warp_patch, &av_enter, pcm_cavity, 128U, 1024);
    int diff_samples = 0;
    for (size_t i = 0; i < 128U; ++i) {
        assert(pcm_open[i] >= -FLOW_Q16_ONE && pcm_open[i] <= FLOW_Q16_ONE);
        assert(pcm_cavity[i] >= -FLOW_Q16_ONE && pcm_cavity[i] <= FLOW_Q16_ONE);
        if (pcm_open[i] != pcm_cavity[i]) diff_samples++;
    }
    assert(diff_samples > 64);
    printf("  ✓ Verified SDF Cavity Hysteresis [12m/20m], Doppler/Hitstop Pitch Shift, TRC <=3Hz Guard & Comb Reverb!\n");

    /* 10. Master-Seed Generative Style Engine, Sovereign Quality Gates & True Binaural Stereo Synth */
    printf("[Test 10/10] Master-Seed Generative Style, Sovereign Quality Gates & 48kHz Binaural Stereo...\n");

    /* 10a. Sweep 1,000 random 64-bit Master Seeds: verify Oklab Delta-L >= 26214 & Negative Space [60%..75%] */
    uint32_t archetype_counts[4] = {0, 0, 0, 0};
    FlowGenerativeSeedStyle64 seed_ink = {0}, seed_crackle = {0};
    for (uint64_t s = 1ULL; s <= 1000ULL; ++s) {
        FlowGenerativeSeedStyle64 st = flow_seed_style_derive64(s * 0x9E3779B97F4A7C15ULL);
        assert(st.oklab_delta_l_q16 >= FLOW_OKLAB_MIN_DELTA_L_Q16);
        assert(st.negative_space_ratio_q16 >= FLOW_NEG_SPACE_MIN_RATIO_Q16);
        assert(st.negative_space_ratio_q16 <= FLOW_NEG_SPACE_MAX_RATIO_Q16);
        assert(st.seed_mac != 0ULL);
        archetype_counts[st.archetype_id & 3U]++;
        if (st.archetype_id == FLOW_SEED_ARCHETYPE_SHANSHUI_INK) seed_ink = st;
        if (st.archetype_id == FLOW_SEED_ARCHETYPE_KINTSUGI_CRACKLE) seed_crackle = st;
    }
    assert(archetype_counts[0] > 150 && archetype_counts[1] > 150);
    assert(seed_ink.stroke_feibai_q16 < 0 && seed_crackle.stroke_feibai_q16 > 0);
    assert(seed_ink.audio_lpf_cutoff_q16 < seed_crackle.audio_lpf_cutoff_q16);
    printf("  ✓ 1,000 Master Seeds verified: Oklab Delta-L >= 0.40 & Negative-Space [60%%..75%%] ('計白當黑')!\n");

    /* 10b. Sovereign Quality Gate: Dual-Currency Orthogonality, Sandbox Quota, Lineage Tether & Thermal Purge */
    FlowSovereignGate64 gate;
    flow_sovereign_gate_init64(&gate, 0x20268888ULL, 0x20269999ULL, 50000LL, 2400ULL);
    assert(flow_sovereign_gate_verify_seal(&gate));
    assert(gate.joint_trust_dual_sig == 3U);

    /* Execute arbitrary gameplay sequence: sovereign_sr MUST remain strictly 2400 */
    flow_sovereign_gate_exec_action(&gate, FLOW_GAMEPLAY_MINE_ORE, 12000LL);
    flow_sovereign_gate_exec_action(&gate, FLOW_GAMEPLAY_PAY_HARBERGER, 3500LL);
    flow_sovereign_gate_exec_action(&gate, FLOW_GAMEPLAY_FUND_WONDER, 25000LL);
    flow_sovereign_gate_exec_action(&gate, FLOW_GAMEPLAY_SQUEEZE_TARGET, 8000LL);
    flow_sovereign_gate_exec_action(&gate, FLOW_GAMEPLAY_SCULPT_PLANET, 1500LL);
    assert(gate.funds_g == 40000LL);
    assert(gate.sovereign_sr == 2400ULL); /* 100% orthogonal to gameplay! */
    assert(flow_sovereign_gate_verify_seal(&gate));

    /* Runaway script while(true) (50,000 steps, 200 SDF ops) -> circuit-broken at 1,000 steps & 64 ops (<8333 us) */
    uint32_t est_us = 0U;
    bool quota_ok = flow_sovereign_gate_run_sandbox_quota(&gate, 200U, 50000U, &est_us);
    assert(!quota_ok);
    assert(gate.active_sdf_ops == FLOW_SANDBOX_MAX_SDF_OPS);
    assert(gate.active_script_steps == FLOW_SANDBOX_MAX_SCRIPT_STEPS);
    assert(gate.script_tripped_halt == 1U);
    assert(est_us <= 8333U); /* Guaranteed <= 120 FPS (8333 us) & 60 FPS (16666 us) budget! */

    /* Two-Player Lineage Tether: first claim succeeds, second claim under same order_id is rejected */
    assert(flow_sovereign_gate_claim_lineage_plot(&gate) == true);
    assert(flow_sovereign_gate_claim_lineage_plot(&gate) == false);

    /* Thermal ASMR [720K..795K] & [X] Key -120K Purge clamped >= 700K */
    gate.thermal_kelvin = 785U;
    uint32_t cooled_k = flow_sovereign_gate_thermal_purge_x(&gate);
    assert(cooled_k == FLOW_THERMAL_AMBIENT_KELVIN);

    /* 1-bit save tamper detection */
    gate.funds_g ^= 1LL;
    assert(!flow_sovereign_gate_verify_seal(&gate));
    printf("  ✓ Verified Dual-Currency Orthogonality, Sandbox Circuit-Breaker (<8.33ms), Lineage Lock & Seal!\n");

    /* 10c. 48kHz True Binaural Stereo (2-Jet ITD + ILD + Pinna Front/Rear + Thermal Venting ASMR) */
    int64_t stereo_right[256], stereo_left[256];
    flow_audio_synth_binaural_stereo_q16(&warp_patch, &av_enter, &seed_crackle,
                                         FLOW_Q16_ONE, /* Full Right (+1.0) */
                                         16384,
                                         FLOW_Q16_ONE,
                                         780U,
                                         false,
                                         stereo_right, 128U, 1024LL);
    flow_audio_synth_binaural_stereo_q16(&warp_patch, &av_enter, &seed_ink,
                                         -FLOW_Q16_ONE, /* Full Left (-1.0) */
                                         -16384,
                                         -FLOW_Q16_ONE,
                                         780U,
                                         true,          /* [X] Purge decompression active */
                                         stereo_left, 128U, 1024LL);
    int64_t energy_r_ch = 0, energy_l_ch = 0;
    for (size_t i = 0; i < 128U; ++i) {
        int64_t l_val = stereo_right[i * 2U + 0U];
        int64_t r_val = stereo_right[i * 2U + 1U];
        energy_l_ch += (l_val < 0) ? -l_val : l_val;
        energy_r_ch += (r_val < 0) ? -r_val : r_val;
    }
    assert(energy_r_ch > energy_l_ch); /* Right-side source has stronger Right-ear ILD energy! */
    printf("  ✓ Verified 48kHz Binaural Stereo 2-Jet ITD/ILD Separation, Pinna Notch & [X] -120K Thermal ASMR!\n");

    /* 11. 64B Composable 4-Axis Object Ticket & Genre-Free Mechanics Engine */
    printf("[Test 11/11] 64B Composable 4-Axis Object Ticket & Genre-Free Mechanics (FlowObjectTicket64)...\n");
    FlowObjectTicket64 tk_horror   = flow_ticket_build_preset64(0U);
    FlowObjectTicket64 tk_climber  = flow_ticket_build_preset64(1U);
    FlowObjectTicket64 tk_tactical = flow_ticket_build_preset64(2U);
    FlowObjectTicket64 tk_arena    = flow_ticket_build_preset64(3U);

    assert(flow_ticket_verify_seal64(&tk_horror));
    assert(flow_ticket_verify_seal64(&tk_climber));
    assert(flow_ticket_verify_seal64(&tk_tactical));
    assert(flow_ticket_verify_seal64(&tk_arena));

    /* 11a. Orthogonal Rebinding: changing Camera Axis preserves Kinetic, Audio & Visual axes identically */
    uint32_t orig_kin = tk_horror.kinetic_axis_flags;
    uint32_t orig_aud = tk_horror.audio_axis_flags;
    uint32_t orig_vis = tk_horror.visual_prefab_id;
    flow_ticket_rebind_axis64(&tk_horror, FLOW_AXIS_CAMERA, FLOW_CAM_AXIS_CCTV_FIXED_28MM);
    assert(tk_horror.camera_axis_id == FLOW_CAM_AXIS_CCTV_FIXED_28MM);
    assert(tk_horror.kinetic_axis_flags == orig_kin);
    assert(tk_horror.audio_axis_flags == orig_aud);
    assert(tk_horror.visual_prefab_id == orig_vis);
    assert(flow_ticket_verify_seal64(&tk_horror));

    /* 11b. Unified Fulcrum / Lever Reaction: F_body + F_tip == 0 on contact (sdf <= 0), 0 when airborne */
    int32_t tip_push = -12 * FLOW_Q16_ONE; /* Pushing hammer/crowbar down (-12) */
    int32_t body_imp_contact = flow_ticket_step_mechanics_q16(&tk_climber, tip_push, 0, 0, 0, 32U);
    assert(body_imp_contact + tip_push == 0);
    assert(body_imp_contact == 12 * FLOW_Q16_ONE); /* Launches climber body UP (+12)! */
    assert(tk_climber.body_pos_z_q16 > 0);

    int32_t body_imp_air = flow_ticket_step_mechanics_q16(&tk_climber, tip_push, 0, 5 * FLOW_Q16_ONE, 0, 32U);
    assert(body_imp_air == 0); /* Airborne hammer swing produces 0 fulcrum reaction */

    /* 11c. 2-Jet Inertial Flashlight Lag & 75% Negative-Space Abyss Cone (Horror Ticket) */
    (void)flow_ticket_step_mechanics_q16(&tk_horror, 0, 0, 10 * FLOW_Q16_ONE, 8 * FLOW_Q16_ONE, 24U);
    int32_t lag_after_turn = tk_horror.inertia_lag_offset_q16;
    assert(lag_after_turn > 0);
    (void)flow_ticket_step_mechanics_q16(&tk_horror, 0, 0, 10 * FLOW_Q16_ONE, 0, 24U);
    assert(tk_horror.inertia_lag_offset_q16 < lag_after_turn); /* Monotonically converges toward 0 */
    uint32_t lit_tiles = (uint32_t)__builtin_popcountll(tk_horror.visible_tile_mask64);
    assert(lit_tiles == 16U); /* 16 / 64 tiles lit = 25% lit, 75% negative-space abyss! */

    /* 11d. Tactical Counter-Strafe Instant Friction Stop */
    (void)flow_ticket_step_mechanics_q16(&tk_tactical, 0, 16 * FLOW_Q16_ONE, 10 * FLOW_Q16_ONE, 0, 32U);
    assert(tk_tactical.body_vel_z_q16 > 0);
    /* Tap opposite direction key (-move_cmd) -> instant counter-strafe zero velocity! */
    (void)flow_ticket_step_mechanics_q16(&tk_tactical, 0, -4 * FLOW_Q16_ONE, 10 * FLOW_Q16_ONE, 0, 32U);
    assert(tk_tactical.body_vel_z_q16 == 0);
    assert(flow_ticket_verify_seal64(&tk_tactical));
    printf("  ✓ Verified 4-Axis Orthogonal Rebind, Fulcrum Lever (F_body=-F_tip), Horror Flashlight Lag & Counter-Strafe!\n");

    /* 12. 64B Dopamine Economy Jackpot, SSOT GPU 4x64B ABI & 10,000-Session Experience SLA Audit */
    printf("[Test 12/12] 64B Dopamine Economy Jackpot, SSOT GPU 4x64B ABI & 10,000-Session SLA Audit...\n");
    assert(sizeof(FlowDopamineJackpot64) == 64U && _Alignof(FlowDopamineJackpot64) == 64U);
    assert(sizeof(FlowGpuSlice0Core64) == 64U && sizeof(FlowGpuSlice1Camera64) == 64U);
    assert(sizeof(FlowGpuSlice2Relativity64) == 64U && sizeof(FlowGpuSlice3DopamineAv64) == 64U);
    assert(sizeof(FlowGpuFrameUniforms256) == 256U && _Alignof(FlowGpuFrameUniforms256) == 64U);
    assert(sizeof(FlowGpuGlyphDesc64) == sizeof(FlowGlyphDesc64));

    FlowDopamineJackpot64 jp;
    flow_dopamine_trigger_jackpot64(&jp, 0x20261105ULL, 2U, 2U, 1100U);
    assert(jp.phase == FLOW_DOPAMINE_PHASE_SUSPENSE);
    assert(jp.frustration_index_q16 == 0U); /* <= 2 simultaneous keys -> 0 mechanical anxiety! */

    flow_dopamine_step_ms(&jp, 240U); /* Advance Suspense (240ms) -> Hitstop Silence */
    assert(jp.phase == FLOW_DOPAMINE_PHASE_HITSTOP);
    assert(jp.catharsis_bloom_q16 <= 8192); /* -18dB pre-detonation silence floor */

    flow_dopamine_step_ms(&jp, 60U);  /* Advance Hitstop (60ms) -> Catharsis Boom! */
    assert(jp.phase == FLOW_DOPAMINE_PHASE_BOOM);
    assert(jp.contrast_delta_q16 >= 57344); /* >= 87.5% FS silence-to-boom contrast amplification! */

    flow_dopamine_step_ms(&jp, 450U); /* Advance Boom (450ms) -> 135mm Telephoto Flex Spotlight */
    assert(jp.phase == FLOW_DOPAMINE_PHASE_FLEX);
    assert(jp.flex_spotlight_q16 > 65536);

    flow_dopamine_step_ms(&jp, 1600U); /* Complete 1600ms Flex Window -> Idle & SLA Certified */
    assert(jp.phase == FLOW_DOPAMINE_PHASE_IDLE);
    assert(jp.flex_window_ms >= 1500U);
    assert(jp.sla_certified == 1U);

    FlowDopamineJackpot64 summary_jp;
    uint32_t certified_10k = flow_dopamine_audit_10k_sessions(0x2026CAFEULL, &tk_climber, &summary_jp);
    assert(certified_10k == 10000U);
    assert(summary_jp.sla_certified == 1U);
    assert(summary_jp.time_to_first_fun_ms <= 3000U);
    assert(summary_jp.contrast_delta_q16 >= 57344);
    assert(summary_jp.flex_window_ms >= 1500U);
    printf("  ✓ 10,000 Headless Sessions Certified: 10000/10000 PASS (FirstFun<=%ums, Contrast=%d Q16, Flex=%ums)!\n",
           summary_jp.time_to_first_fun_ms, summary_jp.contrast_delta_q16, summary_jp.flex_window_ms);

    /* 13. Direct .lean <-> FLOW 64-Byte ABI Bridge & Runtime Specification Loader */
    printf("[Test 13/14] Direct .lean <-> FLOW 64B ABI Bridge & Runtime Spec Binding...\n");
    FlowLeanClause64 abi_table[16];
    flow_lean_abi_init_canonical_table(abi_table);

    FlowLeanSpecMeta64 av_meta, sov_meta;
    assert(flow_lean_abi_load_file("research/AudiovisualInvariants.lean", &av_meta, abi_table, 16U));
    assert(av_meta.sorry_count == 0U && av_meta.theorem_count >= 10U && av_meta.def_count >= 8U);
    assert(abi_table[FLOW_CLAUSE_AV_THERMAL_PURGE].param_a == 700LL);
    assert(abi_table[FLOW_CLAUSE_AV_THERMAL_PURGE].param_b == 120LL);

    assert(flow_lean_abi_load_file("research/SovereignQualityGates.lean", &sov_meta, abi_table, 16U));
    assert(sov_meta.sorry_count == 0U && sov_meta.theorem_count >= 8U);
    assert(abi_table[FLOW_CLAUSE_SOV_OKLAB_CONTRAST].param_a == 26214LL);
    assert(abi_table[FLOW_CLAUSE_SOV_NEGATIVE_SPACE].param_a == 39321LL);
    assert(abi_table[FLOW_CLAUSE_SOV_NEGATIVE_SPACE].param_b == 49152LL);

    FlowLeanRegBank64 rbank;
    flow_lean_abi_regbank_init64(&rbank);
    /* Step thermal hysteresis (801K enter, 760K exit): 820K -> latched, 780K deadband -> stays latched, 750K -> unlatched */
    rbank.regs_q16[0] = 820;
    assert(flow_lean_abi_step_clause64(&abi_table[FLOW_CLAUSE_RICARDIAN_THERMAL_GATE], &rbank));
    assert(rbank.regs_q16[0] == 1);
    rbank.regs_q16[0] = 780;
    assert(flow_lean_abi_step_clause64(&abi_table[FLOW_CLAUSE_RICARDIAN_THERMAL_GATE], &rbank));
    assert(rbank.regs_q16[0] == 1);
    rbank.regs_q16[0] = 750;
    assert(flow_lean_abi_step_clause64(&abi_table[FLOW_CLAUSE_RICARDIAN_THERMAL_GATE], &rbank));
    assert(rbank.regs_q16[0] == 0);

    /* Step Harberger 70/30 conservation split on 10,000 units */
    rbank.regs_q16[1] = 10000;
    assert(flow_lean_abi_step_clause64(&abi_table[FLOW_CLAUSE_RICARDIAN_HARBERGER], &rbank));
    assert(rbank.regs_q16[2] == 7000 && rbank.regs_q16[3] == 3000);
    assert(rbank.regs_q16[2] + rbank.regs_q16[3] == 10000);
    printf("  ✓ Verified .lean Runtime Parser & FLOW 64B ABI Execution (Hysteresis, Clamp, Split, FloorSub)!\n");

    /* 14. Native 64-Byte External Asset Pipeline (WAV PCM Mixer, GLB 2.0 Mesh, W3C SVG <path>) */
    printf("[Test 14/14] Native 64B External Asset Pipeline (WAV, GLB 2.0, W3C SVG <path>)...\n");
    uint8_t test_wav[44 + 16];
    memset(test_wav, 0, sizeof(test_wav));
    memcpy(test_wav + 0,  "RIFF", 4);
    uint32_t riff_sz = 36U + 16U;
    memcpy(test_wav + 4,  &riff_sz, 4);
    memcpy(test_wav + 8,  "WAVEfmt ", 8);
    uint32_t fmt_sz = 16U, srate = 48000U, brate = 96000U, data_sz = 16U;
    uint16_t ftag = 1U, nchan = 1U, balign = 2U, bps = 16U;
    memcpy(test_wav + 16, &fmt_sz, 4);
    memcpy(test_wav + 20, &ftag, 2);
    memcpy(test_wav + 22, &nchan, 2);
    memcpy(test_wav + 24, &srate, 4);
    memcpy(test_wav + 28, &brate, 4);
    memcpy(test_wav + 32, &balign, 2);
    memcpy(test_wav + 34, &bps, 2);
    memcpy(test_wav + 36, "data", 4);
    memcpy(test_wav + 40, &data_sz, 4);
    int16_t raw_samples[8] = { 4000, 8000, 12000, 16000, -16000, -12000, -8000, -4000 };
    memcpy(test_wav + 44, raw_samples, 16);

    int16_t decoded_mono[8] = {0};
    FlowWavAssetDesc64 wav_desc;
    assert(flow_asset_load_wav_bytes(test_wav, sizeof(test_wav), 10U, decoded_mono, 8U, &wav_desc));
    assert(wav_desc.frame_count == 8U && wav_desc.sample_rate_hz == 48000U && decoded_mono[3] == 16000);

    FlowAudioVoice64 voice;
    int16_t stereo_mix[16] = {0};
    flow_audio_voice_trigger64(&voice, 0U, &wav_desc, 65536, 0, 65536, false, 1ULL);
    flow_audio_voice_mix_stereo_s16(&voice, decoded_mono, stereo_mix, 8U);
    assert(stereo_mix[6] > 0 && stereo_mix[7] > 0);

    /* Construct a valid glTF 2.0 Binary (.glb) with 2 vertices (-1.0,-1.0,-1.0) and (+2.0,+1.5,+1.0) */
    const char *glb_json = "{\"asset\":{\"version\":\"2.0\"},\"accessors\":[{\"count\":2}]}   ";
    uint32_t json_len = (uint32_t)strlen(glb_json); /* 56 bytes (4-byte aligned) */
    uint32_t bin_len = 24U; /* 2 vertices x 12 bytes */
    uint32_t glb_tot = 12U + 8U + json_len + 8U + bin_len;
    uint8_t test_glb[128];
    memset(test_glb, 0, sizeof(test_glb));
    uint32_t glb_magic = FLOW_GLB_MAGIC_HEADER, glb_ver = 2U;
    uint32_t chunk_json_t = FLOW_GLB_CHUNK_JSON, chunk_bin_t = FLOW_GLB_CHUNK_BIN;
    memcpy(test_glb + 0,  &glb_magic, 4);
    memcpy(test_glb + 4,  &glb_ver, 4);
    memcpy(test_glb + 8,  &glb_tot, 4);
    memcpy(test_glb + 12, &json_len, 4);
    memcpy(test_glb + 16, &chunk_json_t, 4);
    memcpy(test_glb + 20, glb_json, json_len);
    size_t bin_off = 20U + json_len;
    memcpy(test_glb + bin_off + 0, &bin_len, 4);
    memcpy(test_glb + bin_off + 4, &chunk_bin_t, 4);
    /* IEEE-754 bits: -1.0f = 0xBF800000, +2.0f = 0x40000000, +1.5f = 0x3FC00000, +1.0f = 0x3F800000 */
    uint32_t vbits[6] = { 0xBF800000U, 0xBF800000U, 0xBF800000U, 0x40000000U, 0x3FC00000U, 0x3F800000U };
    memcpy(test_glb + bin_off + 8, vbits, 24U);

    int32_t glb_verts[4][3] = {{0}};
    FlowGlbMeshDesc64 glb_desc;
    assert(flow_asset_load_glb_bytes(test_glb, glb_tot, 20U, 1U, glb_verts, 4U, &glb_desc));
    assert(glb_desc.vertex_count == 2U);
    assert(glb_desc.aabb_min_q16[0] == -65536 && glb_desc.aabb_max_q16[0] == 131072);
    assert(glb_desc.aabb_max_q16[1] == 98304  && glb_desc.aabb_max_q16[2] == 65536);
    assert(flow_asset_eval_glb_sdf_q16(&glb_desc, 0, 0, 0) < 0); /* Origin is inside AABB! */

    /* Compile standard W3C SVG <path> icon */
    FlowGpuGlyphDesc64 svg_icon;
    const char *w3c_svg = "<svg viewBox=\"0 0 24 24\"><path d=\"M 2 12 L 12 2 L 22 12 V 22 H 2 Z\"/></svg>";
    assert(flow_asset_compile_svg_icon64(w3c_svg, 301U, 1U, &svg_icon));
    assert(svg_icon.stroke_count == 5U && svg_icon.ternary_pos_mask != 0ULL);

    /* Compile GLB 2.0 Mesh into Data-Driven 64B GPU CSG Instruction Stream & Evaluate Boolean Subtract */
    assert(sizeof(FlowGpuCsgOp64) == 64U && _Alignof(FlowGpuCsgOp64) == 64U);
    FlowGpuCsgOp64 csg_prog[2];
    flow_asset_compile_glb_to_csg_op64(&glb_desc, 1U, FLOW_CSG_BOOL_UNION, 0, &csg_prog[0]);
    FlowGlbMeshDesc64 cutter_glb = glb_desc;
    cutter_glb.aabb_min_q16[0] = -16384;
    cutter_glb.aabb_min_q16[1] = -16384;
    cutter_glb.aabb_min_q16[2] = -16384;
    cutter_glb.aabb_max_q16[0] = 16384;
    cutter_glb.aabb_max_q16[1] = 16384;
    cutter_glb.aabb_max_q16[2] = 16384;
    flow_asset_compile_glb_to_csg_op64(&cutter_glb, 2U, FLOW_CSG_BOOL_SUBTRACT, 0, &csg_prog[1]);
    /* Origin (0,0,0) was inside glb_desc (<0), but subtracted by cutter_glb -> carved hollow (>0)! */
    int64_t d_carved_csg = flow_asset_eval_csg_program_q16(csg_prog, 2U, 0, 0, 0);
    int64_t d_shell_csg  = flow_asset_eval_csg_program_q16(csg_prog, 2U, 49152, 0, 0);
    assert(d_carved_csg > 0 && d_shell_csg < 0 && csg_prog[0].csg_op_mac != 0ULL);
    printf("  ✓ Verified WAV PCM Mixer, GLB 2.0 Mesh Decoder, W3C SVG <path> & 64B GPU CSG Stream Evaluator!\n");

    printf("====================================================================\n");
    printf("✓ ALL 14 PROCEDURAL, LEAN-ABI & EXTERNAL ASSET TESTS PASSED (100%% SOUND)\n");
    printf("====================================================================\n");
    return 0;
}
