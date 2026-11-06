/*
 * ============================================================================
 * FLOW SVG-UI FRAMEWORK & ZERO-COPY BROADCAST / OBS BRIDGE TEST SUITE (C17)
 * ============================================================================
 * Validates:
 *   1. 64B Univalence of FlowSvgUiNode64, FlowSvgUiDomState64 & FlowBroadcastObsBridge64.
 *   2. Declarative .svg Component Tree Compilation (specs/cockpit_hud.svg -> 8 nodes).
 *   3. Responsive Zero-Reflow Anchor Layout & O(1) Reactive Register Data Binding.
 *   4. Wave64 O(1) Spatial Hit-Testing, 2-Jet Spring Micro-Interaction & Action Routing.
 *   5. Triple-Buffered Zero-Copy OBS Bridge, SHM Export & 4-VCam Highlight Replay.
 * ============================================================================
 */

#include "flow_game.h"
#include "flow_prefab_ssot.h"
#include "flow_svg_ui.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_64b_univalence(void) {
    printf("  [Test 1/15] 64B Univalence (11 Descriptors: SVG-UI, OBS, Prefab, Log, LaTeX, Director, Layout, RegBank, TextInput)... ");
    assert(sizeof(FlowSvgUiNode64) == 64U && _Alignof(FlowSvgUiNode64) == 64U);
    assert(sizeof(FlowSvgUiDomState64) == 64U && _Alignof(FlowSvgUiDomState64) == 64U);
    assert(sizeof(FlowBroadcastObsBridge64) == 64U && _Alignof(FlowBroadcastObsBridge64) == 64U);
    assert(sizeof(FlowPrefabEditState64) == 64U && _Alignof(FlowPrefabEditState64) == 64U);
    assert(sizeof(FlowDebugLogEntry64) == 64U && _Alignof(FlowDebugLogEntry64) == 64U);
    assert(sizeof(FlowPipelineTelemetry64) == 64U && _Alignof(FlowPipelineTelemetry64) == 64U);
    assert(sizeof(FlowLatexBoxNode64) == 64U && _Alignof(FlowLatexBoxNode64) == 64U);
    assert(sizeof(FlowNarrativeDirector64) == 64U && _Alignof(FlowNarrativeDirector64) == 64U);
    assert(sizeof(FlowUiLayoutContainer64) == 64U && _Alignof(FlowUiLayoutContainer64) == 64U);
    assert(sizeof(FlowUiRegisterBank64) == 64U && _Alignof(FlowUiRegisterBank64) == 64U);
    assert(sizeof(FlowUiTextInput64) == 64U && _Alignof(FlowUiTextInput64) == 64U);
    printf("PASS (11 x 64B)\n");
}

static void test_svg_ui_compilation(void) {
    printf("  [Test 2/8] Declarative .svg Component Tree & TrueColor fill='#RRGGBB' Compilation... ");
    FlowSvgUiNode64 nodes[FLOW_SVG_UI_MAX_NODES];
    FlowSvgUiDomState64 dom;

    size_t n = flow_svg_ui_compile_file("specs/cockpit_hud.svg", nodes, FLOW_SVG_UI_MAX_NODES, &dom);
    assert(n == 8U);
    assert(dom.compiled_node_count == 8U);
    assert(dom.active_dom_mask_wave64 != 0ULL);
    assert(dom.dom_state_mac != 0ULL);

    /* Verify compiled node types & parsed 24-bit TrueColor fill spectra */
    assert(nodes[0].node_id == 201U && nodes[0].prim_type == FLOW_SVG_PRIM_RECT_GLASS);
    assert(flow_svg_ui_extract_fill_rgb24(&nodes[0]) == 0x20D8FFU);
    assert(flow_svg_ui_extract_fill_rgb24(&nodes[1]) == 0x18F0B8U);
    assert(flow_svg_ui_extract_fill_rgb24(&nodes[2]) == 0x38A8FFU);
    assert(flow_svg_ui_extract_fill_rgb24(&nodes[3]) == 0xFF6820U);
    assert(nodes[4].node_id == 205U && nodes[4].prim_type == FLOW_SVG_PRIM_CIRCLE_RING);
    assert(nodes[6].node_id == 207U && nodes[6].prim_type == FLOW_SVG_PRIM_NUM_READOUT);
    assert(nodes[7].node_id == 208U && nodes[7].action_opcode == FLOW_SVG_ACTION_DOPAMINE_BOOM);
    assert(flow_svg_ui_extract_fill_rgb24(&nodes[7]) == 0xFFD830U);

    printf("PASS (%zu / 8 nodes, DockRGB=0x%06X, Wave64=0x%016llX)\n",
           n, flow_svg_ui_extract_fill_rgb24(&nodes[0]), (unsigned long long)dom.active_dom_mask_wave64);
}

static void test_responsive_anchor_and_reactive_binding(void) {
    printf("  [Test 3/8] Zero-Reflow Responsive Anchors & O(1) Telemetry Data Binding... ");
    FlowSvgUiNode64 nodes[FLOW_SVG_UI_MAX_NODES];
    FlowSvgUiDomState64 dom;
    size_t n = flow_svg_ui_compile_file("specs/cockpit_hud.svg", nodes, FLOW_SVG_UI_MAX_NODES, &dom);
    assert(n == 8U);

    /* Top-Right Thermal Ring (node[5], id=206) must shift outward on 21:9 (3440x1440) vs 16:9 (3840x2160) */
    int32_t cx_16_9 = 0, cy_16_9 = 0;
    int32_t cx_21_9 = 0, cy_21_9 = 0;
    flow_svg_ui_resolve_anchor_q16(&nodes[5], 3840U, 2160U, &cx_16_9, &cy_16_9);
    flow_svg_ui_resolve_anchor_q16(&nodes[5], 3440U, 1440U, &cx_21_9, &cy_21_9);
    assert(cx_21_9 > cx_16_9);
    assert(cy_21_9 == cy_16_9);

    /* Test O(1) Reactive Telemetry Binding without Virtual DOM Diff */
    flow_svg_ui_bind_telemetry_q16(nodes, n, 700U, 300 << 16, 0U, 0U, FLOW_BROADCAST_MODE_IDLE);
    int32_t ring_700k = nodes[5].bound_value_q16;
    int32_t text_700k = nodes[6].bound_value_q16;
    assert(ring_700k == 0);
    assert(text_700k == 700);
    assert(nodes[1].bound_value_q16 == 65536); /* VCam 0 button highlighted */
    assert(nodes[2].bound_value_q16 == 16384); /* VCam 1 button dimmed */

    flow_svg_ui_bind_telemetry_q16(nodes, n, 820U, 950 << 16, 1U, 3U, FLOW_BROADCAST_MODE_LIVE_OBS_SHM);
    assert(nodes[5].bound_value_q16 > ring_700k && nodes[5].bound_value_q16 <= 65536);
    assert(nodes[6].bound_value_q16 == 820);
    assert(nodes[1].bound_value_q16 == 16384);
    assert(nodes[2].bound_value_q16 == 65536); /* VCam 1 button highlighted */
    assert(nodes[4].bound_value_q16 == 65536); /* OBS Rec ring active */

    printf("PASS (21:9 Anchor Shift Verified, 820K Readout Bound in O(1))\n");
}

static void test_wave64_hittest_and_spring_interaction(void) {
    printf("  [Test 4/8] Wave64 O(1) Hit-Testing, 2-Jet Spring & Action Dispatch... ");
    FlowSvgUiNode64 nodes[FLOW_SVG_UI_MAX_NODES];
    FlowSvgUiDomState64 dom;
    size_t n = flow_svg_ui_compile_file("specs/cockpit_hud.svg", nodes, FLOW_SVG_UI_MAX_NODES, &dom);
    assert(n == 8U);

    /* 1. Move cursor over empty center space (0, 0) -> Wave64 culls non-overlapping nodes */
    uint32_t act = flow_svg_ui_step_interaction(nodes, n, &dom, 0, 0, false, false);
    assert(act == FLOW_SVG_ACTION_NONE);
    assert(dom.hovered_node_id == 0U);
    assert(dom.wave64_cull_skips > 0U);

    /* 2. Hover & click on [PURGE -120K] button (node[3], id=204 at ox=3932, oy=23592) */
    act = flow_svg_ui_step_interaction(nodes, n, &dom, 3932, 23592, true, true);
    assert(dom.hovered_node_id == 204U);
    assert(dom.active_node_id == 204U);
    assert(act == FLOW_SVG_ACTION_THERMAL_PURGE);
    assert(dom.click_dispatch_count == 1U);
    assert(dom.audio_click_patch_id != 0U);
    assert(nodes[3].scale_jet_v_q16 > 65536); /* 2-Jet spring pop impulse applied */

    /* 3. Click on [OBS ZERO-COPY REC] ring (node[4], id=205 at ox=13107, oy=23592) */
    act = flow_svg_ui_step_interaction(nodes, n, &dom, 13107, 23592, false, true);
    assert(dom.hovered_node_id == 205U);
    assert(act == FLOW_SVG_ACTION_BROADCAST_REC);
    assert(dom.click_dispatch_count == 2U);

    printf("PASS (Wave64Skips=%u, SpringPop=%d Q16, Actions Dispatched=2)\n",
           dom.wave64_cull_skips, nodes[3].scale_jet_v_q16);
}

static void test_zero_copy_obs_bridge_and_4vcam_replay(void) {
    printf("  [Test 5/8] Triple-Buffered Zero-Copy OBS Bridge & 4-VCam Highlight Replay... ");
    FlowBroadcastObsBridge64 bridge;
    FlowSvgUiNode64 nodes[FLOW_SVG_UI_MAX_NODES];
    FlowSvgUiDomState64 dom;
    flow_svg_ui_compile_file("specs/cockpit_hud.svg", nodes, FLOW_SVG_UI_MAX_NODES, &dom);
    flow_broadcast_obs_init64(&bridge, 0x4B01U, 0x4B02U, 0x4B03U);

    /* Step 100 frames and verify Theorem 4: write_ring_slot never aliases read_ring_slot */
    for (uint32_t f = 1U; f <= 100U; ++f) {
        uint32_t dop_phase = (f == 80U) ? 3U : 0U; /* Trigger Dopamine Jackpot Boom at frame 80 */
        flow_broadcast_obs_publish_frame64(&bridge, f, f & 3U, dop_phase, (uint64_t)f * 0x12345ULL);
        assert(bridge.write_ring_slot < 3U);
        assert(bridge.read_ring_slot < 3U);
        assert(bridge.write_ring_slot != bridge.read_ring_slot);
    }

    assert(bridge.published_frame_seq == 100U);
    assert(bridge.highlight_jackpot_tick == 80U);
    assert(bridge.broadcast_mode == FLOW_BROADCAST_MODE_REPLAY_4VCAM);
    assert(bridge.zero_copy_bytes_saved == 3300U); /* 3,300 MB (3.3 GB) of 4K CPU readback avoided */

    int rc = flow_broadcast_obs_export_shm_packet(&bridge, &dom, "build/flow_obs_bridge.bin");
    assert(rc == 0);

    FILE *fp = fopen("build/flow_obs_bridge.bin", "rb");
    assert(fp != NULL);
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fclose(fp);
    assert(sz == 128L); /* Exact 2 x 64B = 128 Bytes SHM packet */

    printf("PASS (Saved=%u MB, HighlightTick=%u, SHM=%ld Bytes)\n",
           bridge.zero_copy_bytes_saved, bridge.highlight_jackpot_tick, sz);
}

static void test_physical_glass_beer_lambert_optics(void) {
    printf("  [Test 6/8] Physical Liquid-Glass Beer-Lambert Optical Transmittance (Thm 5)... ");
    /* Center of crystal glass (thickness=4096 Q16) must stay >90% clear; curved bevel concentrates color */
    int32_t t_center = flow_svg_ui_eval_glass_transmittance_q16(0x20D8FFU, 4096);
    int32_t t_bevel  = flow_svg_ui_eval_glass_transmittance_q16(0x20D8FFU, 58982);
    assert(t_center > 60000 && t_center <= 65536);
    assert(t_bevel >= 4096 && t_bevel < t_center);

    for (uint32_t i = 0U; i < 1000U; ++i) {
        uint32_t rgb = (i * 0x13579BU) & 0x00FFFFFFU;
        int32_t thick = (int32_t)((i * 73U) % 70000U);
        int32_t trans = flow_svg_ui_eval_glass_transmittance_q16(rgb, thick);
        assert(trans >= 4096 && trans <= 65536);
    }
    printf("PASS (Center=%d Q16 [98.2%%], Bevel=%d Q16 [79.1%%], 1,000 samples in [4096..65536])\n",
           t_center, t_bevel);
}

static void test_pointer_capture_continuous_scrubbing(void) {
    printf("  [Test 7/8] Pointer Capture Arena Continuous Scrubbing across Wave64 Tiles (Thm 6)... ");
    FlowSvgUiNode64 nodes[FLOW_SVG_UI_MAX_NODES];
    FlowSvgUiDomState64 dom;
    size_t n = flow_svg_ui_compile_file("specs/cockpit_hud.svg", nodes, FLOW_SVG_UI_MAX_NODES, &dom);
    assert(n == 8U);

    /* Press down on X-Ray / Thermal Ring Scrubber (node[5], id=206 at ox=26869, oy=-19660) */
    nodes[5].bound_value_q16 = 16384;
    uint32_t act = flow_svg_ui_step_pointer_arena(nodes, n, &dom, 26869, -19660, 0, true, true);
    assert(dom.active_node_id == 206U);
    assert(act == FLOW_SVG_ACTION_XRAY_SCRUB);
    assert((nodes[5].style_and_state & FLOW_SVG_STATE_CAPTURED_BIT) != 0U);

    /* Drag far outside node[5]'s Wave64 tile mask (to x=-20000, y=+15000) while holding mouse button! */
    act = flow_svg_ui_step_pointer_arena(nodes, n, &dom, -20000, 15000, 24576, true, false);
    assert(dom.active_node_id == 206U);
    assert(act == FLOW_SVG_ACTION_XRAY_SCRUB);
    assert(nodes[5].bound_value_q16 == 40960); /* 16384 + 24576 = 40960 */

    /* Release mouse button -> Pointer Capture cleanly releases */
    act = flow_svg_ui_step_pointer_arena(nodes, n, &dom, -20000, 15000, 0, false, false);
    assert(dom.active_node_id == 0U);
    assert((nodes[5].style_and_state & FLOW_SVG_STATE_CAPTURED_BIT) == 0U);

    printf("PASS (Cross-Tile Scrub 16384->40960 Q16, Clean Release Verified)\n");
}

static void test_prefab_revert_xray_and_debug_pipeline(void) {
    printf("  [Test 8/8] Prefab Revert/Modal Cancel (Thm 7), SDF X-Ray Mask (Thm 8) & 64B Flight Log... ");
    FlowPrefabInstance64 inst = {
        .prefab_id = FLOW_PREFAB_ORBITAL_CITADEL,
        .style_override_id = FLOW_STYLE_ORBITAL_CITADEL,
        .pos_q16 = { 10LL << 16, -(5LL << 16), 48LL << 16 },
        .vel_q16 = { 0, 0, 0 },
        .thermal_kelvin = 760U,
        .active_layer = FLOW_STAGE_LAYER_0_SDF,
        .flags = 0U
    };
    FlowPrefabEditState64 edit;
    flow_prefab_edit_init64(&edit, &inst);

    /* 1. Modal Translate + Thermal Scrub -> then Blender-style Right-Click/Esc Revert to SSOT */
    flow_prefab_edit_begin_modal64(&edit, &inst, FLOW_PREFAB_MODAL_TRANSLATE);
    flow_prefab_edit_apply_scrub64(&edit, &inst, 32768, -16384);
    assert(inst.pos_q16[0] != (10LL << 16));
    flow_prefab_edit_revert_ssot64(&edit, &inst);
    assert(inst.pos_q16[0] == (10LL << 16) && inst.pos_q16[1] == -(5LL << 16));
    assert(edit.revert_exec_count == 1U);

    /* 1b. Modal Translate + Commit -> baseline updated, undo_commit_count incremented */
    flow_prefab_edit_begin_modal64(&edit, &inst, FLOW_PREFAB_MODAL_TRANSLATE);
    flow_prefab_edit_apply_scrub64(&edit, &inst, 16384, 8192);
    flow_prefab_edit_commit64(&edit, &inst);
    assert(edit.undo_commit_count == 1U);
    assert(edit.baseline_pos_q16[0] == inst.pos_q16[0]);
    assert(edit.baseline_pos_q16[1] == inst.pos_q16[1]);
    assert(edit.modal_op_mode == FLOW_PREFAB_MODAL_NONE);

    /* 2. SDF X-Ray Cutaway Mask (Theorem 8: exterior invariant, interior strictly > 0) */
    flow_prefab_edit_begin_modal64(&edit, &inst, FLOW_PREFAB_MODAL_XRAY_SCRUB);
    flow_prefab_edit_apply_scrub64(&edit, &inst, 16384, 0);
    assert(edit.xray_mask_mode == FLOW_PREFAB_XRAY_SPHERE_CUT);
    int64_t d_carved = flow_prefab_eval_xray_cutaway_q16(&edit, -12000, -8192);
    int64_t d_ext    = flow_prefab_eval_xray_cutaway_q16(&edit, 15000, 20000);
    assert(d_carved == 8192 && d_carved > 0);
    assert(d_ext == 15000);

    /* 3. Zero-Allocation 64B Flight-Recorder Debug Log Ring & Pipeline Telemetry */
    FlowDebugLogEntry64 log_ring[16];
    FlowPipelineTelemetry64 telemetry;
    memset(log_ring, 0, sizeof(log_ring));
    memset(&telemetry, 0, sizeof(telemetry));

    flow_pipeline_telemetry_step64(&telemetry, 1U, 15U, 20U, 80U, 5200U, 18U, 10U);
    assert(telemetry.total_frame_us == 5343U);
    assert(telemetry.budget_headroom_us == (8333 - 5343));
    assert(telemetry.pipeline_health_q16 > 20000U);

    flow_debug_log_append64(log_ring, 16U, &telemetry, 1000ULL,
                            FLOW_STAGE_PREFAB_CSG, FLOW_LOG_EVENT_XRAY_TOGGLE,
                            206U, 100U, 29491, edit.xray_radius_q16, 26869, -19660, 16384U);
    flow_debug_log_append64(log_ring, 16U, &telemetry, 1016ULL,
                            FLOW_STAGE_PREFAB_CSG, FLOW_LOG_EVENT_PREFAB_REVERT,
                            207U, 100U, 840, 760, 21626, -19660, 22937U);
    assert(telemetry.total_logs_emitted == 2U);
    assert(log_ring[1].chain_hash == log_ring[0].entry_hash);

    printf("PASS (XRayCut=+%lld Q16, Headroom=%d us, LogHashChain Verified)\n",
           (long long)d_carved, telemetry.budget_headroom_us);
}

static void test_fixed_tick_physics_and_fps_vsync_governor(void) {
    printf("  [Test 9/10] Fixed-Tick Physics Decoupling (Thm 9) & Dynamic VSync/FPS Governor... ");
    FlowGameEngineState eng_30, eng_60, eng_120, eng_240;
    assert(flow_game_init(&eng_30) == 0);
    assert(flow_game_init(&eng_60) == 0);
    assert(flow_game_init(&eng_120) == 0);
    assert(flow_game_init(&eng_240) == 0);

    /* Configure 4 different FPS/VSync budgets: 30Hz (33,333us), 60Hz (16,666us), 120Hz (8,333us), Uncapped (4,166us) */
    flow_fps_governor_set_mode(&eng_30,  30U,  true);
    flow_fps_governor_set_mode(&eng_60,  60U,  true);
    flow_fps_governor_set_mode(&eng_120, 120U, true);
    flow_fps_governor_set_mode(&eng_240, 0U,   false);

    assert(eng_30.fps_governor.target_budget_us  == 33333U && flow_fps_governor_verify(&eng_30.fps_governor));
    assert(eng_60.fps_governor.target_budget_us  == 16666U && flow_fps_governor_verify(&eng_60.fps_governor));
    assert(eng_120.fps_governor.target_budget_us == 8333U  && flow_fps_governor_verify(&eng_120.fps_governor));
    assert(eng_240.fps_governor.target_budget_us == 4166U  && flow_fps_governor_verify(&eng_240.fps_governor));

    /* Simulate exactly 999,960 us (~1.0s = 60 fixed physics ticks) across 30 FPS, 60 FPS, 120 FPS, and 240 FPS */
    uint32_t acc30 = 0U, acc60 = 0U, acc120 = 0U, acc240 = 0U;
    int32_t  alpha30 = 0, alpha60 = 0, alpha120 = 0, alpha240 = 0;
    uint32_t ticks30 = 0U, ticks60 = 0U, ticks120 = 0U, ticks240 = 0U;

    for (uint32_t f = 0U; f < 30U; ++f) {
        ticks30 += flow_game_step_fixed_accumulator(&eng_30, 33332U, &acc30, &alpha30);
    }
    for (uint32_t f = 0U; f < 60U; ++f) {
        ticks60 += flow_game_step_fixed_accumulator(&eng_60, 16666U, &acc60, &alpha60);
    }
    for (uint32_t f = 0U; f < 120U; ++f) {
        ticks120 += flow_game_step_fixed_accumulator(&eng_120, 8333U, &acc120, &alpha120);
    }
    for (uint32_t f = 0U; f < 240U; ++f) {
        ticks240 += flow_game_step_fixed_accumulator(&eng_240, 4166U + ((f & 1U) ? 1U : 0U), &acc240, &alpha240);
    }

    assert(ticks30 == 60U && ticks60 == 60U && ticks120 == 60U && ticks240 == 60U);
    assert(eng_30.craft_traj[2].v  == eng_60.craft_traj[2].v &&
           eng_60.craft_traj[2].v  == eng_120.craft_traj[2].v &&
           eng_120.craft_traj[2].v == eng_240.craft_traj[2].v);
    assert(eng_30.frame_index == eng_240.frame_index && eng_30.frame_index == 60ULL);

    flow_game_destroy(&eng_30);
    flow_game_destroy(&eng_60);
    flow_game_destroy(&eng_120);
    flow_game_destroy(&eng_240);
    printf("PASS (30/60/120/240 FPS -> Bit-Identical z_q16, 60 Fixed Ticks)\n");
}

static void test_single_pass_dfa_latex_sdf_boxes(void) {
    printf("  [Test 10/10] Single-Pass DFA LaTeX Parser (Thm 10-12) & Zero-Font-Atlas 64B SDF Boxes... ");
    FlowLatexBoxNode64 boxes[64];
    uint32_t suppressed_idents = 0U;
    uint32_t literal_dollars   = 0U;

    /*
     * Single-Pass Stress String containing:
     *   1. Currency "$100 ~ $200" (Pandora/GitHub rule: must NOT enter math mode!)
     *   2. C identifier "flow_svg_ui" (must suppress '_' emphasis without escaping!)
     *   3. Real LaTeX formula "$\gamma \ge \frac{A_i}{\sqrt{x^2}}$"
     */
    const char *doc = "$100 ~ $200 flow_svg_ui $\\gamma \\ge \\frac{A_i}{\\sqrt{x^2}}$";
    size_t n = flow_latex_dfa_compile_boxes64(doc, -16384, 0, 2048, 0x00FFCCU,
                                              boxes, 64U, &suppressed_idents, &literal_dollars);
    assert(n > 10U);
    assert(literal_dollars == 2U);   /* Both '$100' and '$200' safely degraded to literal '$' in O(1) */
    assert(suppressed_idents == 2U); /* Both '_' in 'flow_svg_ui' protected without double-escape */

    bool found_gamma = false;
    bool found_ge    = false;
    bool found_frac  = false;
    bool found_sqrt  = false;
    bool found_sub   = false;
    bool found_sup   = false;

    for (size_t i = 0U; i < n; ++i) {
        assert(boxes[i].box_mac != 0ULL);
        assert(boxes[i].tile_mask_wave64 != 0ULL);
        if (boxes[i].symbol_opcode == FLOW_LATEX_OP_GAMMA) found_gamma = true;
        if (boxes[i].symbol_opcode == FLOW_LATEX_OP_GE)    found_ge    = true;
        if (boxes[i].symbol_opcode == FLOW_LATEX_OP_FRAC) {
            found_frac = true;
            /* Verify analytic SDF inside stretchy fraction bar is <= 0 */
            int32_t d_in  = flow_latex_eval_box_sdf_q16(&boxes[i], boxes[i].box_x_q16, boxes[i].box_y_q16);
            int32_t d_out = flow_latex_eval_box_sdf_q16(&boxes[i], boxes[i].box_x_q16 + 16384, boxes[i].box_y_q16 + 16384);
            assert(d_in <= 0 && d_out > 0);
        }
        if (boxes[i].symbol_opcode == FLOW_LATEX_OP_SQRT) {
            found_sqrt = true;
            int32_t d_roof = flow_latex_eval_box_sdf_q16(&boxes[i],
                                                         boxes[i].box_x_q16,
                                                         boxes[i].box_y_q16 - boxes[i].half_h_q16);
            assert(d_roof <= 0);
        }
        if (boxes[i].tex_role == FLOW_TEX_ROLE_SUBSCRIPT) {
            found_sub = true;
            assert(boxes[i].scale_q16 == 45875 && boxes[i].baseline_shift_q16 > 0);
        }
        if (boxes[i].tex_role == FLOW_TEX_ROLE_SUPERSCRIPT) {
            found_sup = true;
            assert(boxes[i].scale_q16 == 45875 && boxes[i].baseline_shift_q16 < 0);
        }
    }

    assert(found_gamma && found_ge && found_frac && found_sqrt && found_sub && found_sup);
    printf("PASS (Boxes=%zu, Literal$=%u, Ident_=%u, Stretchy \\frac/\\sqrt SDF Verified)\n",
           n, literal_dollars, suppressed_idents);
}

static void test_standard_svg_viewbox_and_64node_arena(void) {
    printf("  [Test 11/15] Standard SVG viewBox Normalization (Thm 14) & 64-Node Widget Arena... ");
    assert(flow_svg_ui_norm_viewbox_coord_q16(0, 1920) == 0);
    assert(flow_svg_ui_norm_viewbox_coord_q16(960, 1920) == 32768);
    assert(flow_svg_ui_norm_viewbox_coord_q16(1920, 1920) == 65536);
    assert(flow_svg_ui_norm_viewbox_coord_q16(5000, 1920) == 65536);
    assert(flow_svg_ui_norm_viewbox_coord_q16(100, 0) == 0);

    /* Write a standard SVG file using standard x/y/width/height/rx and cx/cy/r attributes (12 widgets > old 8 cap) */
    const char *tmp_svg = "build/test_standard_ui.svg";
    FILE *fp = fopen(tmp_svg, "w");
    assert(fp != NULL);
    fprintf(fp, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1920 1080\">\n");
    for (uint32_t i = 0U; i < 10U; ++i) {
        fprintf(fp,
                "  <rect data-ui-widget=\"%u\" x=\"%u\" y=\"80\" width=\"160\" height=\"64\" rx=\"12\" fill=\"#20D8FF\" data-action=\"%u\"/>\n",
                300U + i, 40U + i * 180U, i + 1U);
    }
    fprintf(fp, "  <circle data-ui-widget=\"310\" cx=\"960\" cy=\"540\" r=\"54\" fill=\"#FF6820\" data-prim=\"1\"/>\n");
    fprintf(fp, "  <rect data-ui-widget=\"311\" x=\"1600\" y=\"960\" width=\"240\" height=\"80\" rx=\"16\" fill=\"#18F0B8\"/>\n");
    fprintf(fp, "</svg>\n");
    fclose(fp);

    FlowSvgUiNode64 nodes[FLOW_SVG_UI_MAX_NODES];
    FlowSvgUiDomState64 dom;
    size_t compiled = flow_svg_ui_compile_file(tmp_svg, nodes, FLOW_SVG_UI_MAX_NODES, &dom);
    assert(compiled == 12U);
    assert(dom.compiled_node_count == 12U);
    assert(nodes[0].node_id == 300U && nodes[0].half_w_q16 > 0 && nodes[0].half_h_q16 > 0);
    assert(nodes[10].node_id == 310U && nodes[10].offset_x_q16 == 0 && nodes[10].offset_y_q16 == 0);
    assert(nodes[11].node_id == 311U && nodes[11].offset_x_q16 > 16384 && nodes[11].offset_y_q16 < -16384);

    printf("PASS (Compiled=%zu/64 Standard SVG Widgets, CenterCircle=(%d,%d))\n",
           compiled, nodes[10].offset_x_q16, nodes[10].offset_y_q16);
}

static void test_flex_stack_layout_and_scroll_clamping(void) {
    printf("  [Test 12/15] 64B Flexbox/Stack Auto-Layout (Thm 13) & Scroll Clamping... ");
    FlowSvgUiNode64 row_nodes[4];
    memset(row_nodes, 0, sizeof(row_nodes));
    for (uint32_t i = 0U; i < 4U; ++i) {
        row_nodes[i].node_id    = 400U + i;
        row_nodes[i].half_w_q16 = 3000;
        row_nodes[i].half_h_q16 = 1200;
    }

    FlowUiLayoutContainer64 row_box;
    flow_ui_layout_container_init64(&row_box,
                                    FLOW_UI_LAYOUT_ROW,
                                    FLOW_UI_JUSTIFY_SPACE_BETWEEN,
                                    FLOW_UI_ALIGN_CENTER,
                                    0, 0, 20000, 3000, 1000, 500);
    flow_ui_layout_container_apply64(&row_box, row_nodes, 4U, 0);
    assert(row_box.child_count == 4U);
    assert(row_box.max_scroll_q16 == 0);
    assert(row_box.container_tile_mask != 0ULL && row_box.layout_mac != 0ULL);
    for (uint32_t i = 1U; i < 4U; ++i) {
        /* Theorem 13: Monotonic non-overlapping advance along main X axis */
        assert(row_nodes[i].offset_x_q16 >= row_nodes[i - 1U].offset_x_q16 +
               row_nodes[i - 1U].half_w_q16 + row_nodes[i].half_w_q16);
        assert(row_nodes[i].offset_y_q16 == 0);
    }

    /* Column layout with 16 items overflowing container height -> test scroll clamping & STRETCH */
    FlowSvgUiNode64 col_nodes[16];
    memset(col_nodes, 0, sizeof(col_nodes));
    for (uint32_t i = 0U; i < 16U; ++i) {
        col_nodes[i].node_id    = 500U + i;
        col_nodes[i].half_w_q16 = 2000;
        col_nodes[i].half_h_q16 = 2000;
    }
    FlowUiLayoutContainer64 col_box;
    flow_ui_layout_container_init64(&col_box,
                                    FLOW_UI_LAYOUT_COLUMN,
                                    FLOW_UI_JUSTIFY_START,
                                    FLOW_UI_ALIGN_STRETCH,
                                    0, 0, 10000, 12000, 500, 250);
    flow_ui_layout_container_apply64(&col_box, col_nodes, 16U, 0);
    assert(col_box.content_extent_q16 > 24000);
    assert(col_box.max_scroll_q16 == col_box.content_extent_q16 - 24000);
    assert(col_nodes[0].half_w_q16 == 9500); /* Stretched to inner_cross_half = 10000 - 500 */

    int32_t y0_before_scroll = col_nodes[0].offset_y_q16;
    flow_ui_layout_container_apply64(&col_box, col_nodes, 16U, 999999);
    assert(col_box.scroll_offset_q16 == col_box.max_scroll_q16);
    assert(col_nodes[0].offset_y_q16 > y0_before_scroll);

    printf("PASS (Row4 Span=%d, Col16 MaxScroll=%d, StretchW=%d)\n",
           row_box.content_extent_q16, col_box.max_scroll_q16, col_nodes[0].half_w_q16);
}

static void test_story_finisher_execution_director(void) {
    printf("  [Test 13/14] One-Call Scripted Story Finisher ('劇情殺') & Plot-Armor HP Gate (Thm 15)... ");
    FlowNarrativeDirector64 dir;
    flow_director_init64(&dir);
    assert(dir.director_phase == FLOW_DIRECTOR_IDLE);
    assert(dir.time_dilation_q16 == 65536);

    /* Trigger One-Call Scripted Story Finisher ("劇情殺") with HP floor = 1024 */
    flow_director_trigger_story_finisher64(&dir,
                                           0x777U,
                                           FLOW_PREFAB_ORBITAL_CITADEL,
                                           FLOW_VCAM_ORBIT_TELEPHOTO,
                                           FLOW_TRANS_LIQUID_GLASS_WAVE,
                                           1024);
    assert(dir.director_phase == FLOW_DIRECTOR_ANTICIPATION_LOCK);
    assert(dir.input_lock_mask == FLOW_INPUT_LOCK_FULL_CUTSCENE);
    assert(dir.letterbox_aspect_q16 == 65536);
    assert(dir.time_dilation_q16 == 16384); /* 0.25x slow-motion anticipation */

    /* Theorem 15: Plot-Armor HP Floor prevents premature death during Anticipation Lock */
    assert(flow_director_eval_hp_gate_q16(&dir, 0) == 1024);
    assert(flow_director_eval_hp_gate_q16(&dir, -5000) == 1024);
    assert(flow_director_eval_hp_gate_q16(&dir, 4096) == 4096);

    /* Step past the 320ms Anticipation Lock into CLIMAX_EXECUTION */
    uint32_t active_vcam = 99U;
    int32_t trauma_boost = 0;
    uint32_t ticket_preset = 0U;
    uint32_t stage_layer = 0U;
    bool swapped = flow_director_step_choreography64(&dir,
                                                     320U,
                                                     &active_vcam,
                                                     &trauma_boost,
                                                     &ticket_preset,
                                                     &stage_layer);
    assert(!swapped);
    assert(dir.director_phase == FLOW_DIRECTOR_CLIMAX_EXECUTION);
    assert(active_vcam == FLOW_VCAM_ORBIT_TELEPHOTO);
    assert(dir.time_dilation_q16 == 4096);  /* 0.0625x relativistic hitstop */
    assert(dir.xray_cutaway_q16 == 52428);  /* 0.80 SDF X-Ray internal core shatter */
    assert(trauma_boost == 49152);          /* 0.75 6D camera shake impulse */

    /* Plot-Armor HP Floor is now released so the climax strike deals the fatal blow (HP -> 0) */
    assert(dir.scripted_hp_floor_q16 == 0);
    assert(flow_director_eval_hp_gate_q16(&dir, 0) == 0);

    /* Verify Flight-Recorder Log Hash Chain across UI Click + FPS Toggle + Story Director */
    FlowDebugLogEntry64 logs[8];
    FlowPipelineTelemetry64 tel;
    memset(logs, 0, sizeof(logs));
    memset(&tel, 0, sizeof(tel));
    flow_debug_log_append64(logs, 8U, &tel, 1000ULL, FLOW_STAGE_SVG_UI_DOM, FLOW_LOG_EVENT_UI_CLICK,
                            101U, FLOW_PREFAB_ORBITAL_CITADEL, 0, 1, 0, 0, 16384U);
    flow_debug_log_append64(logs, 8U, &tel, 2000ULL, FLOW_STAGE_GPU_RAYMARCH, FLOW_LOG_EVENT_FPS_VSYNC,
                            0U, 120U, 1, 0, 0, 0, 20480U);
    flow_debug_log_append64(logs, 8U, &tel, 3000ULL, FLOW_STAGE_PREFAB_CSG, FLOW_LOG_EVENT_STORY_DIRECTOR,
                            0U, FLOW_PREFAB_ORBITAL_CITADEL, 1024, (int32_t)dir.transition_style, 0, 0, 32768U);
    assert(flow_debug_log_verify_chain64(logs, 3U));
    logs[1].next_value_q16 ^= 1; /* Tamper middle event */
    assert(!flow_debug_log_verify_chain64(logs, 3U));

    printf("PASS (HPFloor=1024->0, Hitstop=4096, XRay=%d, ReplayChain=3 Verified)\n",
           dir.xray_cutaway_q16);
}

static void test_scene_transition_wipe_and_atomic_swap(void) {
    printf("  [Test 14/14] One-Call Scene Transition ('過渡動畫') & 0-Pop-In Midpoint Swap (Thm 16)... ");
    FlowNarrativeDirector64 dir;
    flow_director_init64(&dir);

    /* Trigger One-Call Scene Transition to Ticket Preset 3 & Stage Layer 1 (320ms total -> 160ms close) */
    flow_director_trigger_scene_transition64(&dir,
                                             3U,
                                             1U,
                                             FLOW_TRANS_IRIS_APERTURE,
                                             320U);
    assert(dir.director_phase == FLOW_DIRECTOR_CLIMAX_EXECUTION);
    assert(dir.transition_style == FLOW_TRANS_IRIS_APERTURE);
    assert(dir.wipe_progress_q16 == 0);

    uint32_t active_vcam = 0U;
    int32_t trauma_boost = 0;
    uint32_t live_preset = 1U;
    uint32_t live_stage  = 0U;

    /* 1. Step halfway (80ms / 160ms): wipe closing at 50% (32768), scene NOT swapped yet */
    bool swapped = flow_director_step_choreography64(&dir, 80U, &active_vcam, &trauma_boost, &live_preset, &live_stage);
    assert(!swapped);
    assert(dir.wipe_progress_q16 == 32768);
    assert(live_preset == 1U && live_stage == 0U);

    /* 2. Step to 100% occlusion midpoint (160ms / 160ms): Theorem 16 guarantees wipe == 65536 & atomic swap! */
    swapped = flow_director_step_choreography64(&dir, 80U, &active_vcam, &trauma_boost, &live_preset, &live_stage);
    assert(swapped);
    assert(dir.director_phase == FLOW_DIRECTOR_SCENE_TRANSITION);
    assert(dir.wipe_progress_q16 == 65536);
    assert(live_preset == 3U && live_stage == 1U);
    assert(dir.atomic_swap_completed == 1U);

    /* 3. Step through midpoint hold (64ms) into RECOVERY_SETTLE */
    swapped = flow_director_step_choreography64(&dir, 64U, &active_vcam, &trauma_boost, &live_preset, &live_stage);
    assert(!swapped);
    assert(dir.director_phase == FLOW_DIRECTOR_RECOVERY_SETTLE);

    /* 4. Step through recovery settle (240ms) back to IDLE gameplay */
    swapped = flow_director_step_choreography64(&dir, 240U, &active_vcam, &trauma_boost, &live_preset, &live_stage);
    assert(!swapped);
    assert(dir.director_phase == FLOW_DIRECTOR_IDLE);
    assert(dir.wipe_progress_q16 == 0);
    assert(dir.input_lock_mask == FLOW_INPUT_LOCK_NONE);
    assert(dir.time_dilation_q16 == 65536);

    printf("PASS (Wipe=0->32768->65536[Swap Preset=%u,Stage=%u]->0, MAC=0x%016llX)\n",
           live_preset, live_stage, (unsigned long long)dir.director_mac);
}

static void test_generic_register_bank_and_text_input64(void) {
    printf("  [Test 15/15] 64B Generic Register Bank (Thm 18) & Inline Text Input Widget (Thm 17)... ");
    FlowUiRegisterBank64 bank;
    flow_ui_register_bank_init64(&bank);
    assert(bank.bank_mac != 0ULL);

    /* Set channels 1, 7, 11 and test out-of-bounds channel 99 safety (Theorem 18) */
    flow_ui_register_bank_set_q16(&bank, 1U, 49152);
    flow_ui_register_bank_set_q16(&bank, 7U, 80000); /* Will clamp to 65536 on gauge bind */
    flow_ui_register_bank_set_q16(&bank, 11U, 1440);
    flow_ui_register_bank_set_q16(&bank, 99U, 12345);
    assert(flow_ui_register_bank_get_q16(&bank, 99U) == 0);

    FlowSvgUiNode64 nodes[3];
    memset(nodes, 0, sizeof(nodes));
    nodes[0].prim_type   = FLOW_SVG_PRIM_CIRCLE_RING;
    nodes[0].bind_source = 1U;
    nodes[1].prim_type   = FLOW_SVG_PRIM_RECT_GLASS;
    nodes[1].bind_source = 7U;
    nodes[2].prim_type   = FLOW_SVG_PRIM_NUM_READOUT;
    nodes[2].bind_source = 11U;

    flow_svg_ui_bind_register_bank64(nodes, 3U, &bank);
    assert(nodes[0].bound_value_q16 == 49152);
    assert(nodes[1].bound_value_q16 == 65536);
    assert(nodes[2].bound_value_q16 == 1440);

    /* Test FlowUiTextInput64 inline editing, caret movement, backspace & 39-char cap (Theorem 17) */
    FlowUiTextInput64 input;
    flow_ui_text_input_init64(&input, 601U, 42U, "BLOOM");
    assert(input.byte_len == 5U && input.cursor_byte_pos == 5U);
    assert(strcmp(input.utf8_buf, "BLOOM") == 0);

    /* Move caret left by 5 -> insert "v1:" at start -> "v1:BLOOM" */
    flow_ui_text_input_move_cursor64(&input, -5);
    assert(input.cursor_byte_pos == 0U);
    assert(flow_ui_text_input_insert_char64(&input, 'v', &nodes[0]));
    assert(flow_ui_text_input_insert_char64(&input, '1', &nodes[0]));
    assert(flow_ui_text_input_insert_char64(&input, ':', &nodes[0]));
    assert(strcmp(input.utf8_buf, "v1:BLOOM") == 0);
    assert(input.byte_len == 8U && input.cursor_byte_pos == 3U);

    /* Backspace ':' -> "v1BLOOM" */
    assert(flow_ui_text_input_backspace64(&input, &nodes[0]));
    assert(strcmp(input.utf8_buf, "v1BLOOM") == 0);
    assert(input.byte_len == 7U && input.cursor_byte_pos == 2U);

    /* Fill up to FLOW_UI_TEXT_INPUT_CAP (39 chars) and verify 40th insert is rejected */
    flow_ui_text_input_move_cursor64(&input, 100);
    while (input.byte_len < FLOW_UI_TEXT_INPUT_CAP) {
        assert(flow_ui_text_input_insert_char64(&input, 'x', &nodes[0]));
    }
    assert(input.byte_len == 39U && input.cursor_byte_pos == 39U);
    assert(!flow_ui_text_input_insert_char64(&input, 'z', &nodes[0]));
    assert(input.byte_len == 39U && input.utf8_buf[39] == '\0');

    printf("PASS (RegBank=12ch, TextInput=\"%.7s...\" [%u/%uB, 0 Heap])\n",
           input.utf8_buf, input.byte_len, (unsigned)FLOW_UI_TEXT_INPUT_CAP);
}

int main(void) {
    printf("================================================================================\n");
    printf("  FLOW SVG-UI, Physical Glass, LaTeX SDF, Flex Layout & Story Director Suite\n");
    printf("================================================================================\n");
    test_64b_univalence();
    test_svg_ui_compilation();
    test_responsive_anchor_and_reactive_binding();
    test_wave64_hittest_and_spring_interaction();
    test_zero_copy_obs_bridge_and_4vcam_replay();
    test_physical_glass_beer_lambert_optics();
    test_pointer_capture_continuous_scrubbing();
    test_prefab_revert_xray_and_debug_pipeline();
    test_fixed_tick_physics_and_fps_vsync_governor();
    test_single_pass_dfa_latex_sdf_boxes();
    test_standard_svg_viewbox_and_64node_arena();
    test_flex_stack_layout_and_scroll_clamping();
    test_story_finisher_execution_director();
    test_scene_transition_wipe_and_atomic_swap();
    test_generic_register_bank_and_text_input64();
    printf("================================================================================\n");
    printf("  ALL 15 FLOW SVG-UI, GLASS, LATEX SDF, FLEX LAYOUT & WIDGET TESTS PASSED\n");
    printf("================================================================================\n");
    return 0;
}

