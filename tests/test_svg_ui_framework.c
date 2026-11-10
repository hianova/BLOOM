/*
 * ============================================================================
 * FLOW SEMANTIC UI, PHYSICAL GLASS, LATEX SDF & AUTO-BENTO TEST SUITE (C17)
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
    printf("  [Test 1/13] 64B Univalence (14 Descriptors: SVG-UI, Prefab, Log, LaTeX, Layout, RegBank, TextInput, Font, IME, Window, Dialog, Intent)... ");
    assert(sizeof(FlowSvgUiNode64) == 64U && _Alignof(FlowSvgUiNode64) == 64U);
    assert(sizeof(FlowSvgUiDomState64) == 64U && _Alignof(FlowSvgUiDomState64) == 64U);
    assert(sizeof(FlowPrefabEditState64) == 64U && _Alignof(FlowPrefabEditState64) == 64U);
    assert(sizeof(FlowDebugLogEntry64) == 64U && _Alignof(FlowDebugLogEntry64) == 64U);
    assert(sizeof(FlowPipelineTelemetry64) == 64U && _Alignof(FlowPipelineTelemetry64) == 64U);
    assert(sizeof(FlowLatexBoxNode64) == 64U && _Alignof(FlowLatexBoxNode64) == 64U);
    assert(sizeof(FlowUiLayoutContainer64) == 64U && _Alignof(FlowUiLayoutContainer64) == 64U);
    assert(sizeof(FlowUiRegisterBank64) == 64U && _Alignof(FlowUiRegisterBank64) == 64U);
    assert(sizeof(FlowUiTextInput64) == 64U && _Alignof(FlowUiTextInput64) == 64U);
    assert(sizeof(FlowFontAtlasGlyph64) == 64U && _Alignof(FlowFontAtlasGlyph64) == 64U);
    assert(sizeof(FlowImeComposition64) == 64U && _Alignof(FlowImeComposition64) == 64U);
    assert(sizeof(FlowHostWindowDesc64) == 64U && _Alignof(FlowHostWindowDesc64) == 64U);
    assert(sizeof(FlowNativeDialogDesc64) == 64U && _Alignof(FlowNativeDialogDesc64) == 64U);
    assert(sizeof(FlowUiIntentSpec64) == 64U && _Alignof(FlowUiIntentSpec64) == 64U);
    printf("PASS (14 x 64B)\n");
}

static void test_standard_svg_viewbox_and_64node_arena(void) {
    printf("  [Test 2/13] Standard SVG viewBox Normalization & 64-Node Widget Arena... ");
    assert(flow_svg_ui_norm_viewbox_coord_q16(0, 1920) == 0);
    assert(flow_svg_ui_norm_viewbox_coord_q16(960, 1920) == 32768);
    assert(flow_svg_ui_norm_viewbox_coord_q16(1920, 1920) == 65536);
    assert(flow_svg_ui_norm_viewbox_coord_q16(5000, 1920) == 65536);
    assert(flow_svg_ui_norm_viewbox_coord_q16(100, 0) == 0);

    const char *tmp_svg = "build/test_standard_ui.svg";
    FILE *fp = fopen(tmp_svg, "w");
    assert(fp != NULL);
    fprintf(fp, "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1920 1080\">\n");
    for (uint32_t i = 0U; i < 10U; ++i) {
        fprintf(fp,
                "  <rect data-ui-widget=\"%u\" x=\"%u\" y=\"80\" width=\"160\" height=\"64\" rx=\"12\" fill=\"#20D8FF\" data-action=\"%u\"/>\n",
                300U + i, 40U + i * 180U, i + 1U);
    }
    fprintf(fp, "  <circle data-ui-widget=\"310\" cx=\"960\" cy=\"540\" r=\"54\" fill=\"#FF6820\" data-prim=\"1\" data-action=\"5\"/>\n");
    fprintf(fp, "  <rect data-ui-widget=\"311\" x=\"1600\" y=\"960\" width=\"240\" height=\"80\" rx=\"16\" fill=\"#18F0B8\" data-anchor=\"2\"/>\n");
    fprintf(fp, "</svg>\n");
    fclose(fp);

    FlowSvgUiNode64 nodes[FLOW_SVG_UI_MAX_NODES];
    FlowSvgUiDomState64 dom;
    size_t compiled = flow_svg_ui_compile_file(tmp_svg, nodes, FLOW_SVG_UI_MAX_NODES, &dom);
    assert(compiled == 12U);
    assert(dom.compiled_node_count == 12U);
    assert(flow_svg_ui_extract_fill_rgb24(&nodes[0]) == 0x20D8FFU);
    assert(flow_svg_ui_extract_fill_rgb24(&nodes[10]) == 0xFF6820U);
    assert(flow_svg_ui_extract_fill_rgb24(&nodes[11]) == 0x18F0B8U);
    assert(nodes[0].node_id == 300U && nodes[0].half_w_q16 > 0 && nodes[0].half_h_q16 > 0);
    assert(nodes[10].node_id == 310U && nodes[10].offset_x_q16 == 0 && nodes[10].offset_y_q16 == 0);
    assert(nodes[11].node_id == 311U && nodes[11].offset_x_q16 > 16384 && nodes[11].offset_y_q16 < -16384);

    printf("PASS (Compiled=%zu/64 Standard SVG Widgets, CenterCircle=(%d,%d))\n",
           compiled, nodes[10].offset_x_q16, nodes[10].offset_y_q16);
}

static void test_responsive_anchor_resolution(void) {
    printf("  [Test 3/13] Zero-Reflow Responsive Anchors across 16:9 and 21:9 Viewports... ");
    FlowSvgUiNode64 node;
    memset(&node, 0, sizeof(node));
    node.node_id      = 206U;
    node.anchor_mode  = FLOW_SVG_ANCHOR_TOP_RIGHT;
    node.offset_x_q16 = 26869;
    node.offset_y_q16 = -19660;

    int32_t cx_16_9 = 0, cy_16_9 = 0;
    int32_t cx_21_9 = 0, cy_21_9 = 0;
    flow_svg_ui_resolve_anchor_q16(&node, 3840U, 2160U, &cx_16_9, &cy_16_9);
    flow_svg_ui_resolve_anchor_q16(&node, 3440U, 1440U, &cx_21_9, &cy_21_9);
    assert(cx_21_9 > cx_16_9);
    assert(cy_21_9 == cy_16_9);
    printf("PASS (16:9 cx=%d -> 21:9 cx=%d)\n", cx_16_9, cx_21_9);
}

static void test_wave64_hittest_and_spring_interaction(void) {
    printf("  [Test 4/13] Wave64 O(1) Hit-Testing, 2-Jet Spring & Action Dispatch... ");
    FlowSvgUiNode64 nodes[FLOW_SVG_UI_MAX_NODES];
    FlowSvgUiDomState64 dom;
    size_t n = flow_svg_ui_compile_file("build/test_standard_ui.svg", nodes, FLOW_SVG_UI_MAX_NODES, &dom);
    assert(n == 12U);

    /* 1. Move cursor over empty bottom-left space (-28000, -20000) -> Wave64 culls non-overlapping nodes */
    uint32_t act = flow_svg_ui_step_interaction(nodes, n, &dom, -28000, -20000, false, false);
    assert(act == FLOW_SVG_ACTION_NONE);
    assert(dom.hovered_node_id == 0U);
    assert(dom.wave64_cull_skips > 0U);

    /* 2. Hover & click on center circle (node[10], id=310 at ox=0, oy=0, action=5) */
    act = flow_svg_ui_step_interaction(nodes, n, &dom, 0, 0, true, true);
    assert(dom.hovered_node_id == 310U);
    assert(dom.active_node_id == 310U);
    assert(act == 5U);
    assert(dom.click_dispatch_count == 1U);
    assert(dom.audio_click_patch_id != 0U);
    assert(nodes[10].scale_jet_v_q16 > 65536);

    printf("PASS (Wave64Skips=%u, SpringPop=%d Q16)\n",
           dom.wave64_cull_skips, nodes[10].scale_jet_v_q16);
}

static void test_physical_glass_beer_lambert_optics(void) {
    printf("  [Test 5/13] Physical Liquid-Glass Beer-Lambert Optical Transmittance... ");
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
    printf("  [Test 6/13] Pointer Capture Arena Continuous Scrubbing across Wave64 Tiles... ");
    FlowSvgUiNode64 nodes[FLOW_SVG_UI_MAX_NODES];
    FlowSvgUiDomState64 dom;
    size_t n = flow_svg_ui_compile_file("build/test_standard_ui.svg", nodes, FLOW_SVG_UI_MAX_NODES, &dom);
    assert(n == 12U);

    /* Press down on center circle scrubber (node[10], id=310 at ox=0, oy=0) */
    nodes[10].bound_value_q16 = 16384;
    uint32_t act = flow_svg_ui_step_pointer_arena(nodes, n, &dom, 0, 0, 0, true, true);
    assert(dom.active_node_id == 310U);
    assert(act == 5U);
    assert((nodes[10].style_and_state & FLOW_SVG_STATE_CAPTURED_BIT) != 0U);

    /* Drag far outside node[10]'s Wave64 tile mask (to x=-25000, y=+18000) while holding mouse button! */
    act = flow_svg_ui_step_pointer_arena(nodes, n, &dom, -25000, 18000, 24576, true, false);
    assert(dom.active_node_id == 310U);
    assert(act == 5U);
    assert(nodes[10].bound_value_q16 == 40960); /* 16384 + 24576 = 40960 */

    /* Release mouse button -> Pointer Capture cleanly releases */
    act = flow_svg_ui_step_pointer_arena(nodes, n, &dom, -25000, 18000, 0, false, false);
    assert(dom.active_node_id == 0U);
    assert((nodes[10].style_and_state & FLOW_SVG_STATE_CAPTURED_BIT) == 0U);

    printf("PASS (Cross-Tile Scrub 16384->40960 Q16, Clean Release Verified)\n");
}

static void test_prefab_revert_xray_and_debug_pipeline(void) {
    printf("  [Test 7/13] Prefab Revert/Modal Cancel, SDF X-Ray Mask & 64B Flight Log... ");
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

    /* 1. Modal Translate + Thermal Scrub -> then Right-Click/Esc Revert to SSOT */
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

    /* 2. SDF X-Ray Cutaway Mask (exterior invariant, interior strictly > 0) */
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
    assert(flow_debug_log_verify_chain64(log_ring, 2U));
    log_ring[1].next_value_q16 ^= 1;
    assert(!flow_debug_log_verify_chain64(log_ring, 2U));

    printf("PASS (XRayCut=+%lld Q16, Headroom=%d us, LogHashChain Verified)\n",
           (long long)d_carved, telemetry.budget_headroom_us);
}

static void test_fixed_tick_physics_and_fps_vsync_governor(void) {
    printf("  [Test 8/13] Fixed-Tick Physics Decoupling & Dynamic VSync/FPS Governor... ");
    FlowGameEngineState eng_30, eng_60, eng_120, eng_240;
    assert(flow_game_init(&eng_30) == 0);
    assert(flow_game_init(&eng_60) == 0);
    assert(flow_game_init(&eng_120) == 0);
    assert(flow_game_init(&eng_240) == 0);

    flow_fps_governor_set_mode(&eng_30,  30U,  true);
    flow_fps_governor_set_mode(&eng_60,  60U,  true);
    flow_fps_governor_set_mode(&eng_120, 120U, true);
    flow_fps_governor_set_mode(&eng_240, 0U,   false);

    assert(eng_30.fps_governor.target_budget_us  == 33333U && flow_fps_governor_verify(&eng_30.fps_governor));
    assert(eng_60.fps_governor.target_budget_us  == 16666U && flow_fps_governor_verify(&eng_60.fps_governor));
    assert(eng_120.fps_governor.target_budget_us == 8333U  && flow_fps_governor_verify(&eng_120.fps_governor));
    assert(eng_240.fps_governor.target_budget_us == 4166U  && flow_fps_governor_verify(&eng_240.fps_governor));

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
    printf("  [Test 9/13] Single-Pass DFA LaTeX Parser & Zero-Font-Atlas 64B SDF Boxes... ");
    FlowLatexBoxNode64 boxes[64];
    uint32_t suppressed_idents = 0U;
    uint32_t literal_dollars   = 0U;

    const char *doc = "$100 ~ $200 flow_svg_ui $\\gamma \\ge \\frac{A_i}{\\sqrt{x^2}}$";
    size_t n = flow_latex_dfa_compile_boxes64(doc, -16384, 0, 2048, 0x00FFCCU,
                                              boxes, 64U, &suppressed_idents, &literal_dollars);
    assert(n > 10U);
    assert(literal_dollars == 2U);
    assert(suppressed_idents == 2U);

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

static void test_flex_stack_layout_and_scroll_clamping(void) {
    printf("  [Test 10/13] 64B Flexbox/Stack Auto-Layout & Scroll Clamping... ");
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
        assert(row_nodes[i].offset_x_q16 >= row_nodes[i - 1U].offset_x_q16 +
               row_nodes[i - 1U].half_w_q16 + row_nodes[i].half_w_q16);
        assert(row_nodes[i].offset_y_q16 == 0);
    }

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
    assert(col_nodes[0].half_w_q16 == 9500);

    int32_t y0_before_scroll = col_nodes[0].offset_y_q16;
    flow_ui_layout_container_apply64(&col_box, col_nodes, 16U, 999999);
    assert(col_box.scroll_offset_q16 == col_box.max_scroll_q16);
    assert(col_nodes[0].offset_y_q16 > y0_before_scroll);

    printf("PASS (Row4 Span=%d, Col16 MaxScroll=%d, StretchW=%d)\n",
           row_box.content_extent_q16, col_box.max_scroll_q16, col_nodes[0].half_w_q16);
}

static void test_generic_register_bank_and_text_input64(void) {
    printf("  [Test 11/13] 64B Generic Register Bank & Inline Text Input Widget... ");
    FlowUiRegisterBank64 bank;
    flow_ui_register_bank_init64(&bank);
    assert(bank.bank_mac != 0ULL);

    flow_ui_register_bank_set_q16(&bank, 1U, 49152);
    flow_ui_register_bank_set_q16(&bank, 7U, 80000);
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

    FlowUiTextInput64 input;
    flow_ui_text_input_init64(&input, 601U, 42U, "BLOOM");
    assert(input.byte_len == 5U && input.cursor_byte_pos == 5U);
    assert(strcmp(input.utf8_buf, "BLOOM") == 0);

    flow_ui_text_input_move_cursor64(&input, -5);
    assert(input.cursor_byte_pos == 0U);
    assert(flow_ui_text_input_insert_char64(&input, 'v', &nodes[0]));
    assert(flow_ui_text_input_insert_char64(&input, '1', &nodes[0]));
    assert(flow_ui_text_input_insert_char64(&input, ':', &nodes[0]));
    assert(strcmp(input.utf8_buf, "v1:BLOOM") == 0);
    assert(input.byte_len == 8U && input.cursor_byte_pos == 3U);

    assert(flow_ui_text_input_backspace64(&input, &nodes[0]));
    assert(strcmp(input.utf8_buf, "v1BLOOM") == 0);
    assert(input.byte_len == 7U && input.cursor_byte_pos == 2U);

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

static void test_font_atlas_ime_dpi_and_native_dialog(void) {
    printf("  [Test 12/13] 64B System Font Glyph, CJK IME Bridge, High-DPI Window & Native Dialog... ");

    FlowFontAtlasGlyph64 g_ascii, g_cjk;
    flow_font_glyph_rasterize64(0x0041U, &g_ascii); /* 'A' */
    flow_font_glyph_rasterize64(0x6D41U, &g_cjk);   /* '流' (U+6D41) */
    assert(g_ascii.glyph_mac != 0ULL && g_cjk.glyph_mac != 0ULL);
    assert(g_cjk.advance_x_q16 == 65536 && g_cjk.advance_x_q16 > g_ascii.advance_x_q16);

    FlowUiTextInput64 input;
    FlowSvgUiNode64 node;
    memset(&node, 0, sizeof(node));
    flow_ui_text_input_init64(&input, 701U, 1U, "FLOW:");
    FlowImeComposition64 ime;
    flow_ime_composition_init64(&ime, 701U);
    assert(flow_ime_composition_set_preedit64(&ime, "\xE6\xB5\x81\xE9\x87\x8F", 2U)); /* "流量" */
    assert(ime.ime_active == 1U && ime.preedit_len == 6U && ime.candidate_index == 2U);
    assert(flow_ime_composition_commit_to_input64(&ime, &input, &node));
    assert(ime.ime_active == 0U && ime.preedit_len == 0U && ime.committed_count == 1U);
    assert(input.byte_len == 11U && strcmp(input.utf8_buf, "FLOW:\xE6\xB5\x81\xE9\x87\x8F") == 0);

    FlowHostWindowDesc64 win;
    flow_host_window_init64(&win, FLOW_HOST_BACKEND_METAL_COCOA, 1920U, 1080U, 65536, 120U);
    assert(win.fb_width_px == 1920U && win.fb_height_px == 1080U);
    flow_host_window_apply_dpi64(&win, 131072);
    assert(win.fb_width_px == 3840U && win.fb_height_px == 2160U && win.window_mac != 0ULL);

    FlowNativeDialogDesc64 dlg;
    assert(flow_native_dialog_prepare64(&dlg, FLOW_DIALOG_OPEN_FILE, 0x03U, "assets/spaceship.glb"));
    assert(dlg.status_code == 2U && strcmp(dlg.path_or_clip_utf8, "assets/spaceship.glb") == 0 && dlg.dialog_mac != 0ULL);

    printf("PASS (CJK Advance=%d, IME=\"%s\", Retina=%ux%u, Dialog=%s)\n",
           g_cjk.advance_x_q16, input.utf8_buf, win.fb_width_px, win.fb_height_px, dlg.path_or_clip_utf8);
}

static void test_constraint_projected_bento_ui(void) {
    printf("  [Test 13/13] Constraint-Projected 64B Semantic UI & Auto-Bento Synthesizer (0 XML/Coord)... ");

    FlowLeanClause64 clause;
    memset(&clause, 0, sizeof(clause));
    clause.param_a = 57344; /* b_enter_q16 */
    clause.param_b = 40960; /* b_exit_q16 */

    FlowUiRegisterBank64 bank;
    flow_ui_register_bank_init64(&bank);
    flow_ui_register_bank_set_q16(&bank, 0U, 32768); /* <= b_exit: Safe Cyan-Ice (0x0020D8FF) */
    flow_ui_register_bank_set_q16(&bank, 1U, 49152); /* In (b_exit, b_enter): Hysteresis Warn Amber (0x00FFB020) */
    flow_ui_register_bank_set_q16(&bank, 2U, 61440); /* >= b_enter: Trip Alert Crimson (0x00FF2848) */
    flow_ui_register_bank_set_q16(&bank, 3U, 16384);

    FlowUiIntentSpec64 intents[4];
    FlowSvgUiNode64 nodes[4];
    FlowSvgUiDomState64 dom;

    size_t count = flow_ui_project_from_abi64(&clause, &bank, 1920U, 1080U,
                                              intents, nodes, 4U, &dom);
    assert(count == 4U && dom.compiled_node_count == 4U);
    assert(dom.active_dom_mask_wave64 != 0ULL && dom.dom_state_mac != 0ULL);

    assert(intents[0].optical_state == FLOW_UI_OPTICAL_SAFE && intents[0].derived_rgb24 == 0x0020D8FFU);
    assert(intents[1].optical_state == FLOW_UI_OPTICAL_HYST_WARN && intents[1].derived_rgb24 == 0x00FFB020U);
    assert(intents[2].optical_state == FLOW_UI_OPTICAL_TRIP_ALERT && intents[2].derived_rgb24 == 0x00FF2848U);
    assert(flow_svg_ui_extract_fill_rgb24(&nodes[0]) == 0x0020D8FFU);
    assert(flow_svg_ui_extract_fill_rgb24(&nodes[1]) == 0x00FFB020U);
    assert(flow_svg_ui_extract_fill_rgb24(&nodes[2]) == 0x00FF2848U);

    assert(nodes[0].offset_x_q16 < 0 && nodes[1].offset_x_q16 > 0);
    assert(nodes[0].offset_y_q16 < nodes[2].offset_y_q16);
    for (size_t i = 0U; i < 4U; ++i) {
        assert(intents[i].intent_mac != 0ULL);
        assert(nodes[i].half_w_q16 > 0 && nodes[i].half_h_q16 > 0);
        assert(nodes[i].tile_mask_wave64 != 0ULL);
    }

    printf("PASS (Projected=4 Nodes, Colors=[0x%06X,0x%06X,0x%06X], Wave64=0x%016llX)\n",
           intents[0].derived_rgb24, intents[1].derived_rgb24, intents[2].derived_rgb24,
           (unsigned long long)dom.active_dom_mask_wave64);
}

int main(void) {
    printf("================================================================================\n");
    printf("  FLOW Semantic UI, Physical Glass, LaTeX SDF, Flex Layout & Auto-Bento Suite\n");
    printf("================================================================================\n");
    test_64b_univalence();
    test_standard_svg_viewbox_and_64node_arena();
    test_responsive_anchor_resolution();
    test_wave64_hittest_and_spring_interaction();
    test_physical_glass_beer_lambert_optics();
    test_pointer_capture_continuous_scrubbing();
    test_prefab_revert_xray_and_debug_pipeline();
    test_fixed_tick_physics_and_fps_vsync_governor();
    test_single_pass_dfa_latex_sdf_boxes();
    test_flex_stack_layout_and_scroll_clamping();
    test_generic_register_bank_and_text_input64();
    test_font_atlas_ime_dpi_and_native_dialog();
    test_constraint_projected_bento_ui();
    printf("================================================================================\n");
    printf("  ALL 13 FLOW SEMANTIC UI, GLASS, LATEX SDF, FLEX LAYOUT & BENTO TESTS PASSED\n");
    printf("================================================================================\n");
    return 0;
}
