#!/usr/bin/env python3
"""
sabotage_check.py — FLOW v9.0.0 Shared C17 Source-Level Sabotage & Purity Gate
==============================================================================
Uses `FLOW/sdk/tools/flow_sabotage_runner.py` (`FlowMutant`, `run_sabotage_suite`)
to execute parallel single-fault C17 source-level mutation campaigns across all
three Pure-GPR `libbloom.a` modules (`src/bloom_scene.c`, `src/bloom_ui.c`,
`src/bloom_prefab.c`) with zero SDL3 / GPU startup overhead, plus static
FPU/SIMD/BLR ISA purity verification.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Dict, List

ROOT = Path(__file__).resolve().parent.parent
FLOW_DIR = Path(os.environ.get("FLOW_DIR", str(ROOT.parent / "FLOW"))).resolve()
FLOW_TOOLS_DIR = FLOW_DIR / "sdk" / "tools"
if str(FLOW_TOOLS_DIR) not in sys.path:
    sys.path.insert(0, str(FLOW_TOOLS_DIR))

from flow_sabotage_runner import FlowMutant, resolve_llvm_cc, run_sabotage_suite  # noqa: E402

FORBIDDEN_INSN_RE = re.compile(
    r"^(fadd|fsub|fmul|fdiv|fsqrt|fmadd|fmsub|fcmp|fccmp|fcsel|"
    r"scvtf|ucvtf|fcvtzs|fcvtzu|fmov|umov|smov|ins|dup|mov\.[bhsdq]|blr)$"
)

PROBE_C_SOURCE = r"""
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bloom.h"
#include "flow_cholesky_sdd.h"
#include "flow_sha256.h"
#include "searu.h"

static uint64_t g_acc = UINT64_C(0xCBF29CE484222325);

static inline void mix_u64(uint64_t v) {
    g_acc ^= v;
    g_acc *= UINT64_C(0x100000001B3);
    g_acc ^= (g_acc >> 32U);
}

static inline void mix_bytes(const void *ptr, size_t len) {
    const uint8_t *p = (const uint8_t *)ptr;
    for (size_t i = 0U; i < len; ++i) {
        g_acc ^= (uint64_t)p[i];
        g_acc *= UINT64_C(0x100000001B3);
    }
}

#define CHECK(cond)                                                               \
    do {                                                                          \
        if (!(cond)) {                                                            \
            fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #cond);    \
            return 2;                                                             \
        }                                                                         \
    } while (0)

static void put_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
    p[2] = (uint8_t)((v >> 16U) & 0xFFU);
    p[3] = (uint8_t)((v >> 24U) & 0xFFU);
}

static bool utf8_valid(const char *buf, size_t cap) {
    size_t n = 0U;
    while (n <= cap && buf[n] != '\0') {
        ++n;
    }
    if (n > cap) {
        return false;
    }
    size_t i = 0U;
    while (i < n) {
        uint8_t b0 = (uint8_t)buf[i];
        if (b0 < 0x80U) {
            ++i;
        } else if (b0 >= 0xC2U && b0 <= 0xDFU) {
            if (i + 2U > n || ((uint8_t)buf[i + 1U] & 0xC0U) != 0x80U) {
                return false;
            }
            i += 2U;
        } else if (b0 >= 0xE0U && b0 <= 0xEFU) {
            if (i + 3U > n) {
                return false;
            }
            uint8_t b1 = (uint8_t)buf[i + 1U];
            uint8_t b2 = (uint8_t)buf[i + 2U];
            if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U) {
                return false;
            }
            if ((b0 == 0xE0U && b1 < 0xA0U) || (b0 == 0xEDU && b1 >= 0xA0U)) {
                return false;
            }
            i += 3U;
        } else if (b0 >= 0xF0U && b0 <= 0xF4U) {
            if (i + 4U > n) {
                return false;
            }
            uint8_t b1 = (uint8_t)buf[i + 1U];
            uint8_t b2 = (uint8_t)buf[i + 2U];
            uint8_t b3 = (uint8_t)buf[i + 3U];
            if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U || (b3 & 0xC0U) != 0x80U) {
                return false;
            }
            if ((b0 == 0xF0U && b1 < 0x90U) || (b0 == 0xF4U && b1 >= 0x90U)) {
                return false;
            }
            i += 4U;
        } else {
            return false;
        }
    }
    return true;
}

static bool parse_u64_hex(const char *s, uint64_t *out) {
    if (!s || s[0] == '\0' || !out) {
        return false;
    }
    uint64_t acc = 0ULL;
    size_t digits = 0U;
    for (size_t i = 0U; s[i] != '\0'; ++i) {
        char c = s[i];
        uint64_t nib = 0ULL;
        if (c >= '0' && c <= '9') {
            nib = (uint64_t)(uint8_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            nib = 10ULL + (uint64_t)(uint8_t)(c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            nib = 10ULL + (uint64_t)(uint8_t)(c - 'A');
        } else {
            return false;
        }
        acc = (acc << 4U) | nib;
        ++digits;
    }
    *out = acc;
    return digits > 0U && digits <= 16U;
}

static int run_probe(uint64_t *out_digest) {
    g_acc = UINT64_C(0xCBF29CE484222325);

    /* 1. Scene Algebra, GLB/STL Mesh Ingestion, Morton BVH, Jordan Parity & 2-Jet Cholesky+SDD */
    CHECK(FLOW_VERSION_MAJOR >= 9);
    mix_u64((uint64_t)(uint32_t)bloom_isqrt_u64_to_i32(0ULL));
    mix_u64((uint64_t)(uint32_t)bloom_isqrt_u64_to_i32(UINT64_MAX));
    mix_u64((uint64_t)(uint32_t)bloom_sin_q16(51472));
    mix_u64((uint64_t)(uint32_t)bloom_cos_q16(-51472));
    mix_u64((uint64_t)bloom_smin_q16(12000, 18000, 16384));
    mix_u64((uint64_t)bloom_smax_q16(-12000, -18000, 16384));
    CHECK(bloom_ieee754_bits_to_q16(0xBF800000U) == -BLOOM_Q16_ONE);
    CHECK(bloom_ieee754_bits_to_q16(0x3E800000U) == 16384);
    CHECK(bloom_ieee754_bits_to_q16(0x7FC00001U) == 0);

    {
        BloomCsgOp64 op;
        uint32_t vcnt = 0U;
        uint8_t glb[80];
        memset(glb, 0, sizeof(glb));
        put_u32_le(glb + 0U, 0x46546C67U);
        put_u32_le(glb + 4U, 2U);
        put_u32_le(glb + 8U, 80U);
        put_u32_le(glb + 12U, 4U);
        put_u32_le(glb + 16U, 0x4E4F534AU);
        memcpy(glb + 20U, "{  }", 4U);
        put_u32_le(glb + 24U, 48U);
        put_u32_le(glb + 28U, 0x004E4942U);
        const uint32_t verts[12] = {
            0xBF800000U, 0xC0000000U, 0xBF000000U,
            0x40400000U, 0x40000000U, 0x3FC00000U,
            0x00000000U, 0x00000000U, 0x00000000U,
            0x3F800000U, 0x3F800000U, 0x3F800000U
        };
        for (size_t k = 0U; k < 12U; ++k) {
            put_u32_le(glb + 32U + k * 4U, verts[k]);
        }
        CHECK(bloom_glb_parse_to_csg64(glb, 80U, 5U, 2U, &op, &vcnt) && vcnt == 4U);
        mix_bytes(&op, sizeof(op));
    }

    {
        static const int8_t c_tris[12][3][3] = {
            {{-1, -1, -1}, { 1,  1, -1}, { 1, -1, -1}}, {{-1, -1, -1}, {-1,  1, -1}, { 1,  1, -1}},
            {{-1, -1,  1}, { 1, -1,  1}, { 1,  1,  1}}, {{-1, -1,  1}, { 1,  1,  1}, {-1,  1,  1}},
            {{-1, -1, -1}, {-1, -1,  1}, {-1,  1,  1}}, {{-1, -1, -1}, {-1,  1,  1}, {-1,  1, -1}},
            {{ 1, -1, -1}, { 1,  1,  1}, { 1, -1,  1}}, {{ 1, -1, -1}, { 1,  1, -1}, { 1,  1,  1}},
            {{-1, -1, -1}, { 1, -1,  1}, {-1, -1,  1}}, {{-1, -1, -1}, { 1, -1, -1}, { 1, -1,  1}},
            {{-1,  1, -1}, {-1,  1,  1}, { 1,  1,  1}}, {{-1,  1, -1}, { 1,  1,  1}, { 1,  1, -1}}
        };
        uint8_t stl[84U + 12U * 50U];
        memset(stl, 0, sizeof(stl));
        put_u32_le(stl + 80U, 12U);
        for (uint32_t t = 0U; t < 12U; ++t) {
            uint8_t *rec = stl + 84U + (size_t)t * 50U;
            for (uint32_t v = 0U; v < 3U; ++v) {
                for (uint32_t a = 0U; a < 3U; ++a) {
                    put_u32_le(rec + 12U + (size_t)v * 12U + (size_t)a * 4U,
                               (c_tris[t][v][a] < 0) ? 0xBF000000U : 0x3F000000U);
                }
            }
        }
        static BloomMeshArena mesh_a;
        uint32_t tcnt = 0U;
        CHECK(!bloom_stl_parse_to_mesh64(stl, sizeof(stl) - 4U, BLOOM_MATTER_TITANIUM, false, &mesh_a, &tcnt));
        CHECK(bloom_stl_parse_to_mesh64(stl, sizeof(stl), BLOOM_MATTER_TITANIUM, true, &mesh_a, &tcnt) &&
              tcnt == 12U);
        uint32_t mbvh = (uint32_t)bloom_bvh_build_from_tris64(
            mesh_a.map.tris, tcnt, mesh_a.map.bvh_nodes, BLOOM_MAX_MESH_BVH_NODES);
        CHECK(mbvh > 0U);
        mix_bytes(mesh_a.map.bvh_nodes, (size_t)mbvh * sizeof(BloomBvhNode64));

        BloomTri64 rev_tris[12];
        static BloomBvhNode64 rev_bvh[BLOOM_MAX_MESH_BVH_NODES];
        for (uint32_t t = 0U; t < tcnt; ++t) {
            rev_tris[t] = mesh_a.map.tris[tcnt - 1U - t];
        }
        uint32_t mbvh_rev = (uint32_t)bloom_bvh_build_from_tris64(
            rev_tris, tcnt, rev_bvh, BLOOM_MAX_MESH_BVH_NODES);
        CHECK(mbvh_rev == mbvh);
        mix_bytes(rev_bvh, (size_t)mbvh_rev * sizeof(BloomBvhNode64));
        uint32_t vis = 0U;
        int64_t sd_in = bloom_mesh_eval_sdf_q16(&mesh_a, tcnt, mbvh, true, 0LL, 0LL, 0LL, &vis);
        int64_t sd_out = bloom_mesh_eval_sdf_q16(&mesh_a, tcnt, mbvh, true, 3LL * BLOOM_Q16_ONE, 0LL, 0LL, &vis);
        CHECK(sd_in < 0LL && sd_out > 0LL);
        mix_u64((uint64_t)sd_in ^ (uint64_t)sd_out ^ (uint64_t)vis);

        char mesh_sha[65];
        bloom_mesh_sha256_hex(&mesh_a, mesh_sha);
        CHECK(strlen(mesh_sha) == 64U);
        mix_bytes(mesh_sha, 64U);
    }

    /* 2. UI Flexbox, Pointer Capture, Glass Solver, LaTeX & UTF-8/IME */
    {
        BloomTextInput64 inp;
        BloomIme64 ime;
        bloom_text_input_init64(&inp, 1U, BLOOM_ACTION_FOCUS_INPUT);
        bloom_ime_init64(&ime, 1U);
        const char *init = "hi\xF0\x9F\x9A\x80";
        for (size_t i = 0U; init[i] != '\0'; ++i) {
            CHECK(bloom_text_input_insert_char64(&inp, init[i]));
        }
        CHECK(inp.byte_len == 6U && utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP));
        CHECK(!bloom_text_input_insert_char64(&inp, (char)0xFFU));
        CHECK(bloom_text_input_insert_char64(&inp, (char)0xE0U) &&
              !bloom_text_input_insert_char64(&inp, (char)0x80U));
        CHECK(bloom_text_input_backspace64(&inp) && inp.byte_len == 2U &&
              utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP));
        CHECK(bloom_ime_set_preedit64(&ime, "台灣\xF0\x9F\x94\xA5", 1U));
        CHECK(bloom_ime_commit64(&ime, &inp) && ime.ime_active == 0U &&
              utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP));
        mix_bytes(&inp, sizeof(inp));
        mix_bytes(&ime, sizeof(ime));

        BloomFlexContainer64 flex;
        BloomUiNode64 fnodes[4];
        memset(fnodes, 0, sizeof(fnodes));
        for (uint32_t i = 0U; i < 4U; ++i) {
            fnodes[i].half_w_q16 = 8192 + (int32_t)(i * 1024U);
            fnodes[i].half_h_q16 = 6144 + (int32_t)(i * 512U);
        }
        bloom_flex_init64(&flex, BLOOM_FLEX_COLUMN, BLOOM_JUSTIFY_SPACE_BETWEEN, BLOOM_ALIGN_STRETCH,
                          -16384, 8192, 24576, 32768, 2048, 1536);
        bloom_flex_apply64(&flex, fnodes, 4U, 4096);
        mix_bytes(&flex, sizeof(flex));
        mix_bytes(fnodes, sizeof(fnodes));

        BloomLatexBox64 lboxes[16];
        int32_t lw = 0;
        CHECK(bloom_latex_compile_boxes64("x", 0, 0, 4096, 0xFFFFFFU, lboxes, 0U, &lw) == 0U);
        size_t ln = bloom_latex_compile_boxes64("\\int \\sum \\frac{\\sqrt{\\eta^2 + x_1}}{2\\pi}",
                                                -16384, -32768, 4096, 0xFFD848U, lboxes, 16U, &lw);
        CHECK(ln >= 6U && lw > 0);
        mix_u64((uint64_t)(uint32_t)lw);
        for (size_t i = 0U; i < ln; ++i) {
            mix_u64((uint64_t)(uint32_t)bloom_latex_eval_box_sdf_q16(
                &lboxes[i], lboxes[i].x_q16 + 512, lboxes[i].y_q16 - 512));
            mix_u64((uint64_t)(uint32_t)bloom_latex_eval_box_sdf_q16(
                &lboxes[i], lboxes[i].x_q16, lboxes[i].y_q16));
        }

        BloomUiNode64 gnodes[8];
        memset(gnodes, 0, sizeof(gnodes));
        for (size_t i = 0U; i < 8U; ++i) {
            gnodes[i].style_and_state = BLOOM_UI_STATE_GLASS_BIT | BLOOM_GLASS_IRIDESCENT;
        }
        const uint16_t ui_edges[14] = {
            SEARU_EDGE_U16(0, 1), SEARU_EDGE_U16(0, 2), SEARU_EDGE_U16(0, 3),
            SEARU_EDGE_U16(1, 2), SEARU_EDGE_U16(2, 3), SEARU_EDGE_U16(3, 4),
            SEARU_EDGE_U16(4, 5), SEARU_EDGE_U16(5, 6), SEARU_EDGE_U16(6, 7),
            SEARU_EDGE_U16(7, 1), SEARU_EDGE_U16(1, 4), SEARU_EDGE_U16(2, 5),
            SEARU_EDGE_U16(3, 6), SEARU_EDGE_U16(4, 7)
        };
        uint32_t unsat = 99U;
        CHECK(bloom_ui_solve_glass_tracks64(gnodes, 8U, ui_edges, 14U, UINT64_C(0x3),
                                            UINT64_C(0xB100BEEF), 2048U, &unsat) &&
              unsat == 0U && (gnodes[0].style_and_state & 0x3U) == BLOOM_GLASS_IRIDESCENT);
        const uint16_t k5_edges[10] = {
            SEARU_EDGE_U16(0, 1), SEARU_EDGE_U16(0, 2), SEARU_EDGE_U16(0, 3), SEARU_EDGE_U16(0, 4),
            SEARU_EDGE_U16(1, 2), SEARU_EDGE_U16(1, 3), SEARU_EDGE_U16(1, 4),
            SEARU_EDGE_U16(2, 3), SEARU_EDGE_U16(2, 4), SEARU_EDGE_U16(3, 4)
        };
        CHECK(!bloom_ui_solve_glass_tracks64(gnodes, 5U, k5_edges, 10U, UINT64_C(0xF),
                                             UINT64_C(0x12345678), 32U, &unsat) &&
              unsat >= 1U);
        mix_bytes(gnodes, sizeof(gnodes));
        mix_u64((uint64_t)(uint32_t)bloom_ui_eval_glass_transmittance_q16(0x20D8FFU, 16384));

        /* 2b. Generic Agent-Native UI SDK (Snapshot, Query, Guarded Act, Modal Trap, Roving Focus, JSON) */
        static BloomEngineArena ui_arena;
        bloom_arena_init_default(&ui_arena, 640U, 360U);
        BloomUiNode64 *unds = ui_arena.map.ui_nodes;
        memset(unds, 0, sizeof(ui_arena.map.ui_nodes));
        for (uint32_t i = 0U; i < 4U; ++i) {
            unds[i].node_id = i + 1U;
            unds[i].prim_type = BLOOM_UI_PRIM_RECT_GLASS;
            unds[i].bind_channel = i;
            unds[i].half_w_q16 = 8000;
            unds[i].half_h_q16 = 3000;
            unds[i].scale_jet_v_q16 = BLOOM_Q16_ONE;
            unds[i].style_and_state = BLOOM_UI_STATE_GLASS_BIT | (0x20D8FFU << 8U);
            unds[i].action_opcode = (BLOOM_ACTION_CUSTOM_BASE + i) | BLOOM_UI_CAP_FOCUSABLE;
            ui_arena.map.dom.dom_mask_lo32 |= 1U;
        }
        unds[0].action_opcode |= BLOOM_UI_CAP_TOGGLE;
        unds[1].action_opcode |= BLOOM_UI_CAP_DISABLED;
        unds[2].action_opcode |= BLOOM_UI_CAP_AGENT_GUARD;
        ui_arena.map.frame_hdr.ui_count = 4U;
        bloom_ui_set_node_label(&ui_arena, 0U, "ACT0");
        bloom_ui_set_node_label(&ui_arena, 2U, "GRD2");

        BloomUiActRequest ureq;
        BloomUiActResult ures;
        memset(&ureq, 0, sizeof(ureq));
        ureq.source = BLOOM_UI_SRC_AGENT;
        ureq.act_kind = BLOOM_UI_ACT_ROVE_NEXT;
        CHECK(bloom_ui_agent_act(&ui_arena, &ureq, &ures) == BLOOM_UI_STATUS_OK && ures.focused_node_id == 1U);
        CHECK(bloom_ui_agent_act(&ui_arena, &ureq, &ures) == BLOOM_UI_STATUS_OK && ures.focused_node_id == 3U);

        ureq.node_id = 2U;
        ureq.act_kind = BLOOM_UI_ACT_CLICK;
        CHECK(bloom_ui_agent_act(&ui_arena, &ureq, &ures) == BLOOM_UI_STATUS_DISABLED);
        ureq.node_id = 3U;
        CHECK(bloom_ui_agent_act(&ui_arena, &ureq, &ures) == BLOOM_UI_STATUS_VETOED_GUARD);
        ureq.allow_guarded = true;
        CHECK(bloom_ui_agent_act(&ui_arena, &ureq, &ures) == BLOOM_UI_STATUS_OK);

        bloom_ui_set_node_caps(&unds[3], BLOOM_UI_CAP_MODAL, 0U);
        ureq.node_id = 1U;
        ureq.act_kind = BLOOM_UI_ACT_CLICK;
        CHECK(bloom_ui_agent_act(&ui_arena, &ureq, &ures) == BLOOM_UI_STATUS_MODAL_TRAP);
        bloom_ui_set_node_caps(&unds[3], 0U, BLOOM_UI_CAP_MODAL);

        BloomUiSnapshot usnap;
        static char ujson[2048];
        CHECK(bloom_ui_snapshot_arena(&ui_arena, &usnap));
        size_t ujn = bloom_ui_snapshot_to_json(&usnap, ujson, sizeof(ujson));
        CHECK(ujn > 20U && ujson[0] == '{');
        mix_bytes(ujson, ujn);
    }

    /* 3. Arena Init, Prefabs, Swept-CCD, Modal DSP, Gameplay Shells, Rewind & FLOW v9.0.0 Gates */
    {
        static BloomEngineArena arena;
        bloom_arena_init_default(&arena, 320U, 180U);
        CHECK(bloom_image_verify_arena(&arena));
        arena.map.reg_bank.epoch ^= 1U;
        CHECK(!bloom_image_verify_arena(&arena));
        bloom_image_seal_arena(&arena);
        CHECK(bloom_image_verify_arena(&arena));

        char sha0[65], sha1[65];
        bloom_arena_sha256_hex(&arena, sha0);
        CHECK(strlen(sha0) == 64U);
        mix_bytes(sha0, 64U);

        const int32_t p_orig[3] = { 0, 0, 0 };
        uint32_t sp_id = bloom_prefab_spawn_inst(&arena, BLOOM_PREFAB_CRYSTAL_SPHERE, p_orig, BLOOM_Q16_ONE);
        CHECK(sp_id == 1U);
        bloom_arena_sha256_hex(&arena, sha1);
        CHECK(memcmp(sha0, sha1, 64U) != 0);

        BloomFieldJet64 jet;
        bloom_scene_eval_jet2_q16(&arena, NULL, 65536LL, 0LL, 0LL, &jet);
        mix_bytes(&jet, sizeof(jet));

        int64_t mu_pos = -1LL;
        int64_t mu_neg = 0LL;
        CHECK(bloom_scene_certify_hessian_psd_q16(&arena, NULL, 0LL, 0LL, 0LL, 16LL * BLOOM_Q16_ONE, &mu_pos) &&
              mu_pos >= 0LL);
        CHECK(!bloom_scene_certify_hessian_psd_q16(&arena, NULL, 0LL, 0LL, 0LL, -512LL * BLOOM_Q16_ONE, &mu_neg) &&
              mu_neg < 0LL);
        mix_u64((uint64_t)mu_pos ^ (uint64_t)mu_neg);

        static flow_ir_builder_ws_t fir_ws;
        uint32_t mbits = 0U;
        uint32_t mdepth = 0U;
        size_t wlen = 0U;
        CHECK(bloom_fir_certify_arena_contracts(&fir_ws, &mbits, &mdepth, &wlen) &&
              fir_ws.ir_linear_ok && fir_ws.ir_unitary_ok && wlen > 64U);
        static const uint64_t fir_vecs[3][4] = {
            { UINT64_C(0x1234), UINT64_C(0x0003), UINT64_C(0xABCD), UINT64_C(1) },
            { UINT64_C(0x1234), UINT64_C(0x0003), UINT64_C(0xABCC), UINT64_C(1) },
            { UINT64_C(0x00F9), UINT64_C(0x0003), UINT64_C(0xABCC), UINT64_C(0) }
        };
        for (size_t vi = 0U; vi < 3U; ++vi) {
            uint64_t outv[3] = { 0ULL, 0ULL, 0ULL };
            uint64_t exp_slot = fir_vecs[vi][0] & UINT64_C(0xFF);
            uint64_t exp_b0 = (fir_vecs[vi][3] != 0ULL) ? (fir_vecs[vi][1] & UINT64_C(1))
                                                        : (fir_vecs[vi][2] & UINT64_C(1));
            uint64_t exp_mode = (fir_vecs[vi][2] & UINT64_C(0xFFFE)) | exp_b0;
            CHECK(flow_ir_eval_u64(&fir_ws.ir_reloaded, fir_vecs[vi], outv) &&
                  outv[0] == exp_slot && outv[1] == exp_mode && outv[2] == UINT64_C(1));
            mix_u64(outv[0] ^ outv[1] ^ outv[2]);
        }

        BloomPrefabDesc64 joined = bloom_prefab_join(
            &arena.map.prefabs[0], &arena.map.prefabs[1], 9U, BLOOM_CSG_BOOL_SMOOTH_UNION);
        mix_bytes(&joined, sizeof(joined));

        bool hot = false;
        BloomArtStyle64 s_hot = bloom_style_resolve_thermal(
            arena.map.styles, 2U, 850U, false, true, true, &hot);
        CHECK(hot && s_hot.strobe_hz_q16 <= BLOOM_TRC_MAX_STROBE_HZ_Q16);
        BloomArtStyle64 s_hyst = bloom_style_resolve_thermal(
            arena.map.styles, 2U, 770U, hot, false, false, &hot);
        CHECK(hot);
        mix_bytes(&s_hyst, sizeof(s_hyst));

        uint32_t fslot = arena.map.instances[0].flow_slot & 31U;
        CHECK(bloom_arena_try_swap_chart(&arena, fslot, BLOOM_CHART_PREFAB_ALERT, 0x0003U));
        CHECK((arena.map.flow_states[fslot][5] & 0xFFFFULL) == 0x0003ULL);
        CHECK(bloom_arena_try_swap_chart(&arena, fslot, BLOOM_CHART_PREFAB_NORMAL, 0x0004U));
        CHECK((arena.map.flow_states[fslot][5] & 0xFFFFULL) == 0x0005ULL);

        uint64_t bchart[8] = {
            (uint64_t)(12LL * FLOW_Q16_ONE),
            0ULL,
            (uint64_t)(2LL * FLOW_Q16_ONE),
            ((uint64_t)(uint32_t)(-FLOW_MAX_Q16) << 32U) | (uint64_t)(uint32_t)FLOW_MAX_Q16,
            ((uint64_t)(uint32_t)(16 * FLOW_Q16_ONE) << 32U) | (uint64_t)(uint32_t)FLOW_Q16_ONE,
            (6ULL << 24U) | (6ULL << 16U),
            1ULL << 32U,
            0ULL
        };
        flow_arena_publish_bound((flow_cell *)arena.map.flow_charts, 6U, BLOOM_FLOW_CHART_MASK, 2U, bchart);
        CHECK(bloom_arena_try_swap_chart(&arena, BLOOM_FLOW_CH_SCROLL, 6U, 0U));
        uint64_t *sst = arena.map.flow_states[BLOOM_FLOW_CH_SCROLL];
        sst[3] = 0ULL;
        sst[4] = 0ULL;
        sst[6] = 0ULL;
        const BloomBilevelTerm terms[4] = {
            { -20LL * FLOW_Q16_ONE, 0LL,          0LL, UINT64_C(50000) },
            { -6LL * FLOW_Q16_ONE,  FLOW_Q16_ONE, 0LL, UINT64_C(0)     },
            { -6LL * FLOW_Q16_ONE,  0LL,          0LL, UINT64_C(0)     },
            { +8LL * FLOW_Q16_ONE,  0LL,          0LL, UINT64_C(8000)  }
        };
        uint64_t hseed = UINT64_C(0x5EA40700BEEF);
        uint32_t hepoch = 12U;
        CHECK(bloom_arena_heal_and_swap(&arena, BLOOM_FLOW_CH_SCROLL, terms, 4U, UINT64_C(0x1),
                                        0LL, 0LL, 8U, &hseed, &hepoch, 512U));
        CHECK((int64_t)arena.map.flow_charts[6][0] == 0LL);

        /* UI Pointer Capture + 2-Jet Step */
        arena.map.frame_hdr.ui_count = 1U;
        arena.map.ui_nodes[0].node_id = 1U;
        arena.map.ui_nodes[0].prim_type = BLOOM_UI_PRIM_SLIDER_BAR;
        arena.map.ui_nodes[0].half_w_q16 = 32768;
        arena.map.ui_nodes[0].half_h_q16 = 16384;
        arena.map.ui_nodes[0].action_opcode = BLOOM_ACTION_SCRUB_XRAY;
        arena.map.ui_nodes[0].tile_mask_lo32 = 0xFFFFFFFFU;
        arena.map.ui_nodes[0].tile_mask_hi32 = 0xFFFFFFFFU;
        arena.map.dom.dom_mask_lo32 = 0xFFFFFFFFU;
        arena.map.dom.dom_mask_hi32 = 0xFFFFFFFFU;
        (void)bloom_ui_step_pointer_arena(&arena, 0, 0, 8192, true, true);
        CHECK(arena.map.dom.captured_node_id == 1U);
        (void)bloom_arena_step_dynamics(&arena, 0x0001U);

        /* Swept-CCD, Modal Audio, Gameplay Shells, Rewind, Metric & Moodboard */
        const int32_t p_left[3] = { -163840, 0, 0 };
        const int32_t p_right[3] = { 163840, 0, 0 };
        const int32_t v_pos[3] = { 262144, 0, 0 };
        const int32_t v_neg[3] = { -262144, 0, 0 };
        int32_t toi = 0;
        int32_t nrm[3] = { 0, 0, 0 };
        CHECK(bloom_swept_sphere_ccd_q16(p_left, v_pos, BLOOM_Q16_ONE,
                                         p_right, v_neg, BLOOM_Q16_ONE, &toi, nrm));
        mix_u64((uint64_t)(uint32_t)toi ^ (uint64_t)(uint32_t)nrm[0]);

        static BloomAudioRig rig;
        static int16_t pcm[2U * 800U];
        bloom_audio_init_rig(&rig, BLOOM_AUDIO_SAMPLE_RATE);
        bloom_audio_excite_impact64(&rig, &arena, 0U, 4 * BLOOM_Q16_ONE, 0);
        (void)bloom_audio_step_frame64(&rig, &arena, pcm, 800U);
        CHECK(rig.hdr.peak_abs_pcm > 0);
        mix_u64((uint64_t)(uint32_t)rig.hdr.peak_abs_pcm);

        static BloomEngineArena gp_arena;
        static BloomSpacetimeSnap ring[BLOOM_REWIND_RING_CAP];
        bloom_arena_init_default(&gp_arena, 320U, 180U);
        const int32_t p_pl[3] = { 0, 0, 0 };
        const int32_t p_hz[3] = { 176947, 0, 0 };
        uint32_t pid = bloom_prefab_spawn_inst(&gp_arena, BLOOM_PREFAB_CRYSTAL_SPHERE, p_pl, BLOOM_Q16_ONE);
        uint32_t hid = bloom_prefab_spawn_inst(&gp_arena, BLOOM_PREFAB_CRYSTAL_SPHERE, p_hz, BLOOM_Q16_ONE);
        gp_arena.map.instances[pid - 1U].flags |= BLOOM_INST_FLAG_COLLIDABLE;
        gp_arena.map.instances[hid - 1U].flags |= BLOOM_INST_FLAG_COLLIDABLE;

        BloomGameplayState64 gp;
        BloomRewindCtrl64 rw;
        bloom_gameplay_init64(&gp, pid, 16384, 32768, 65536);
        bloom_rewind_init64(&rw, BLOOM_REWIND_RING_CAP);
        bloom_rewind_push(&rw, ring, &gp_arena, &rig);
        const int32_t imp_dir[3] = { 3000, 2500, -1500 };
        uint32_t ev = bloom_gameplay_step64(&gp, &gp_arena, &rig, imp_dir, true);
        CHECK((ev & BLOOM_GP_EVENT_FULCRUM) != 0U);
        bloom_rewind_push(&rw, ring, &gp_arena, &rig);
        CHECK(bloom_rewind_restore(&rw, ring, 1U, &gp_arena, &rig) && rw.branch_epoch == 1U);
        mix_u64(ev ^ (uint64_t)gp.graze_score);

        BloomMetricScale64 ms;
        bloom_metric_eval_scale64(32768, 16384, 65536, NULL, NULL, &ms);
        CHECK(ms.lorentz_gamma_q16 > BLOOM_Q16_ONE && ms.doppler_factor_q16 > 110000);
        mix_bytes(&ms, sizeof(ms));

        static uint8_t rgb[16U * 16U * 3U];
        for (uint32_t y = 0U; y < 16U; ++y) {
            for (uint32_t x = 0U; x < 16U; ++x) {
                size_t k = ((size_t)y * 16U + (size_t)x) * 3U;
                bool ck = (((x + y) & 1U) != 0U);
                rgb[k + 0U] = ck ? 245U : 190U;
                rgb[k + 1U] = ck ? 195U : 140U;
                rgb[k + 2U] = 32U;
            }
        }
        BloomMoodboard64 mb;
        BloomArtStyle64 mb_st;
        BloomPrefabDesc64 mb_pf;
        CHECK(bloom_moodboard_extract_rgb24(rgb, 16U, 16U, 48U, &mb) &&
              mb.inferred_matter_id == BLOOM_MATTER_AURUM_GOLD);
        bloom_moodboard_compile_to_ssot64(&mb, 4U, 5U, &mb_st, &mb_pf);
        mix_bytes(&mb_pf, sizeof(mb_pf));
    }

    *out_digest = g_acc;
    return 0;
}

int main(int argc, char **argv) {
    uint64_t digest = 0ULL;
    int rc = run_probe(&digest);
    if (rc != 0) {
        return rc;
    }
    if (argc >= 2 && strcmp(argv[1], "--emit-digest") == 0) {
        printf("%016llx\n", (unsigned long long)digest);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "selftest") == 0) {
        uint64_t expected = 0ULL;
        if (!parse_u64_hex(argv[2], &expected)) {
            return 1;
        }
        return (digest == expected) ? 0 : 3;
    }
    return 1;
}
"""


def check_static_purity(obj_path: Path) -> bool:
    if sys.platform == "darwin":
        out = subprocess.check_output(["otool", "-tv", str(obj_path)], text=True)
    else:
        out = subprocess.check_output(["objdump", "-d", str(obj_path)], text=True)
    for line in out.splitlines():
        cols = line.strip().split()
        if len(cols) >= 2 and FORBIDDEN_INSN_RE.match(cols[1]):
            return False
    return True


def compile_clean_objects(cc: str, tmp_dir: Path) -> Dict[str, Path]:
    srcs = {
        "wire": ROOT / "src" / "bloom_wire.c",
        "scene": ROOT / "src" / "bloom_scene.c",
        "ui": ROOT / "src" / "bloom_ui.c",
        "prefab": ROOT / "src" / "bloom_prefab.c",
    }
    objs: Dict[str, Path] = {}
    for key, src in srcs.items():
        obj = tmp_dir / f"clean_{key}.o"
        cmd = [
            cc,
            "-std=c17",
            "-O2",
            "-ffreestanding",
            "-nostdlib",
            "-fno-builtin",
            "-fno-stack-protector",
            "-fno-jump-tables",
            "-mgeneral-regs-only",
            "-Wall",
            "-Wextra",
            "-Wpedantic",
            "-Wconversion",
            "-Wundef",
            "-Wmissing-prototypes",
            "-Werror",
            f"-I{ROOT / 'include'}",
            f"-I{ROOT / 'build'}",
            "-isystem",
            str(FLOW_DIR / "core"),
            "-isystem",
            str(FLOW_DIR / "sdk"),
            "-c",
            str(src),
            "-o",
            str(obj),
        ]
        subprocess.run(cmd, check=True)
        if not check_static_purity(obj):
            raise RuntimeError(f"Static FPU/SIMD/BLR purity check failed for {src}")
        objs[key] = obj
    return objs


def build_scene_mutants() -> List[FlowMutant]:
    return [
        FlowMutant(
            "S01_smin_drop_quad",
            "scene_algebra",
            "    return sc_sat_sub64(mn, (int64_t)(num / den));",
            "    return sc_sat_sub64(mn, (int64_t)((num / den) & 0ULL));",
        ),
        FlowMutant(
            "S02_ieee754_nan_nonzero",
            "scene_algebra",
            "    if ((bits & 0x7FFFFFU) != 0U) return 0; /* NaN -> 0 */",
            "    if ((bits & 0x7FFFFFU) != 0U) return BLOOM_Q16_ONE; /* NaN corrupt */",
        ),
        FlowMutant(
            "S03_smax_flip_sign",
            "scene_algebra",
            "    return sc_neg64(bloom_smin_q16(sc_neg64(a_q16), sc_neg64(b_q16), k_q16));",
            "    return bloom_smin_q16(sc_neg64(a_q16), sc_neg64(b_q16), k_q16);",
        ),
        FlowMutant(
            "S04_mesh_jordan_flip",
            "scene_mesh_bvh",
            "        if (parity != 0U) {\n"
            "            res = -best_ud;\n"
            "        }",
            "        if (parity == 0U) {\n"
            "            res = -best_ud;\n"
            "        }",
        ),
        FlowMutant(
            "S05_bvh_morton_drop",
            "scene_mesh_bvh",
            "    bool use_morton_leaves = ((2U * n - 1U) <= cap);\n"
            "\n"
            "    uint32_t idx[BLOOM_MAX_MESH_TRIS];",
            "    bool use_morton_leaves = (((2U * n - 1U) <= cap) && false);\n"
            "\n"
            "    uint32_t idx[BLOOM_MAX_MESH_TRIS];",
        ),
        FlowMutant(
            "S06_stl_short_accept",
            "scene_mesh_bvh",
            "    uint64_t need_bytes = (uint64_t)raw_cnt * 50ULL + 84ULL;\n"
            "    if (need_bytes != (uint64_t)stl_len) return false;",
            "    uint64_t need_bytes = (uint64_t)raw_cnt * 50ULL + 84ULL;\n"
            "    if (need_bytes == 0ULL && stl_len == 0U) return false;",
        ),
        FlowMutant(
            "S07_hess_cholesky_floor_bypass",
            "scene_cholesky_sdd",
            "    return mu_floor >= 0LL;",
            "    return mu_floor <= 0LL;",
        ),
        FlowMutant(
            "S08_hess_reg_diag_drop",
            "scene_cholesky_sdd",
            "            int64_t aij_q16 = sc_clamp64(hij + ((i == j) ? reg : 0LL), -2LL * bound_q16, 2LL * bound_q16);",
            "            int64_t aij_q16 = sc_clamp64(hij + ((i == j) ? (reg & 0LL) : 0LL), -2LL * bound_q16, 2LL * bound_q16);",
        ),
        FlowMutant(
            "S09_jet2_mean_curv_zero",
            "scene_cholesky_sdd",
            "    out_jet->mean_curv_q16 = sc_clamp64_to_i32((s11 + s22) / 2LL);",
            "    out_jet->mean_curv_q16 = 0;",
        ),
        FlowMutant(
            "S10_metric_gamma_one",
            "scene_metric_mood",
            "    int32_t gamma_q16 = sc_clamp64_to_i32((65536LL << 16) / sqrt_om);",
            "    int32_t gamma_q16 = BLOOM_Q16_ONE;",
        ),
        FlowMutant(
            "S11_metric_doppler_invert",
            "scene_metric_mood",
            "    uint64_t d_num = ((uint64_t)(65536LL + b)) << 32ULL;\n"
            "    uint64_t d_den = (uint64_t)(65536LL - b);",
            "    uint64_t d_num = ((uint64_t)(65536LL - b)) << 32ULL;\n"
            "    uint64_t d_den = (uint64_t)(65536LL + b);",
        ),
        FlowMutant(
            "S12_mood_matter_force_obsidian",
            "scene_metric_mood",
            "        matter = BLOOM_MATTER_AURUM_GOLD;",
            "        matter = BLOOM_MATTER_OBSIDIAN;",
        ),
    ]


def build_ui_mutants() -> List[FlowMutant]:
    return [
        FlowMutant(
            "U01_flex_gap_drop",
            "ui_flex_layout",
            "    int64_t gap = (flex->gap_q16 > 0) ? (int64_t)flex->gap_q16 : 0LL;",
            "    int64_t gap = (flex->gap_q16 > 0) ? 0LL : 0LL;",
        ),
        FlowMutant(
            "U02_flex_stretch_skip",
            "ui_flex_layout",
            "        if (flex->align_items == BLOOM_ALIGN_STRETCH) {\n"
            "            cross_half = inner_cross / 2LL;",
            "        if (false && flex->align_items == BLOOM_ALIGN_STRETCH) {\n"
            "            cross_half = inner_cross / 2LL;",
        ),
        FlowMutant(
            "U03_pointer_capture_drop",
            "ui_flex_layout",
            "                    dom->captured_node_id = nd->node_id;\n"
            "                    nd->style_and_state |= BLOOM_UI_STATE_CAPTURED_BIT;",
            "                    dom->captured_node_id = 0U;\n"
            "                    nd->style_and_state |= BLOOM_UI_STATE_CAPTURED_BIT;",
        ),
        FlowMutant(
            "U04_glass_pin_drop",
            "ui_glass_searu",
            "    const uint64_t pin_mask[SEARU_CANVAS_WORDS] = {kan_bit_pin, 0ULL, 0ULL, 0ULL};",
            "    const uint64_t pin_mask[SEARU_CANVAS_WORDS] = {kan_bit_pin & 0ULL, 0ULL, 0ULL, 0ULL};",
        ),
        FlowMutant(
            "U05_glass_unsat_always_zero",
            "ui_glass_searu",
            "    return res.best_energy == UINT64_C(0);",
            "    return (res.best_energy == UINT64_C(0)) || (res.best_energy != UINT64_C(0));",
        ),
        FlowMutant(
            "U06_glass_transmittance_const",
            "ui_glass_searu",
            "    return ui_clamp32(BLOOM_Q16_ONE - (int32_t)atten, 4096, BLOOM_Q16_ONE);",
            "    return ui_clamp32(BLOOM_Q16_ONE - ((int32_t)atten & 0), 4096, BLOOM_Q16_ONE);",
        ),
        FlowMutant(
            "U07_latex_sqrt_drop_rad",
            "ui_latex_sdf",
            "        int32_t mn = (d1 < d2) ? d1 : d2;\n"
            "        if (d3 < mn) mn = d3;",
            "        int32_t mn = (d1 > d2) ? d1 : d2;\n"
            "        if (d3 > mn) mn = d3;",
        ),
        FlowMutant(
            "U08_latex_width_zero",
            "ui_latex_sdf",
            "    if (out_total_width_q16) *out_total_width_q16 = ui_clamp64_to_32((int64_t)end_x - (int64_t)ox, 0, BLOOM_MAX_Q16);",
            "    if (out_total_width_q16) *out_total_width_q16 = ui_clamp64_to_32(((int64_t)end_x - (int64_t)ox) & 0LL, 0, BLOOM_MAX_Q16);",
        ),
        FlowMutant(
            "U09_latex_box_sdf_sign",
            "ui_latex_sdf",
            "        int64_t signed_d = (int64_t)out_d + (int64_t)in_d;\n"
            "        if (signed_d < 0LL) signed_d = -signed_d;",
            "        int64_t signed_d = (int64_t)out_d + (int64_t)in_d;\n"
            "        if (signed_d > 0LL) signed_d = -signed_d;",
        ),
        FlowMutant(
            "U10_utf8_reject_invalid_bypass",
            "ui_utf8_ime",
            "    if ((b1 & 0xC0U) != 0x80U) return false;\n"
            "    if (b0 == 0xE0U) return b1 >= 0xA0U;",
            "    if ((b1 & 0xC0U) != 0x80U) return false;\n"
            "    if (b0 == 0xE0U) return b1 >= 0x80U;",
        ),
        FlowMutant(
            "U11_utf8_backspace_single_byte",
            "ui_utf8_ime",
            "    uint8_t cut = (cp_len > 0U && cp_len <= (size_t)max_cut) ? (uint8_t)cp_len : 1U;",
            "    uint8_t cut = (cp_len > 0U && cp_len <= (size_t)max_cut) ? (uint8_t)1U : 1U;",
        ),
        FlowMutant(
            "U12_ime_commit_skip_clear",
            "ui_utf8_ime",
            "    ime->ime_active = 0U;\n"
            "    ime->committed_count++;",
            "    ime->ime_active = 1U;\n"
            "    ime->committed_count++;",
        ),
        FlowMutant(
            "U13_agent_guard_bypass",
            "ui_agent_sdk",
            "        } else if ((nd->action_opcode & BLOOM_UI_CAP_AGENT_GUARD) != 0U &&\n"
            "                   !req->allow_guarded &&\n"
            "                   req->guard_verdict != BLOOM_UI_GUARD_ALLOW) {\n"
            "            allowed = false;",
            "        } else if ((nd->action_opcode & BLOOM_UI_CAP_AGENT_GUARD) != 0U &&\n"
            "                   false &&\n"
            "                   req->guard_verdict != BLOOM_UI_GUARD_ALLOW) {\n"
            "            allowed = false;",
        ),
        FlowMutant(
            "U14_modal_trap_bypass",
            "ui_agent_sdk",
            "    if (modal_active && (nd->action_opcode & BLOOM_UI_CAP_MODAL) == 0U) {\n"
            "        return ui_finish_act_status(arena, dom, out_res, BLOOM_UI_STATUS_MODAL_TRAP, target_nid,",
            "    if (false && modal_active && (nd->action_opcode & BLOOM_UI_CAP_MODAL) == 0U) {\n"
            "        return ui_finish_act_status(arena, dom, out_res, BLOOM_UI_STATUS_MODAL_TRAP, target_nid,",
        ),
        FlowMutant(
            "U15_disabled_act_bypass",
            "ui_agent_sdk",
            "    if ((nd->action_opcode & BLOOM_UI_CAP_DISABLED) != 0U) {\n"
            "        return ui_finish_act_status(arena, dom, out_res, BLOOM_UI_STATUS_DISABLED, target_nid,",
            "    if (false && (nd->action_opcode & BLOOM_UI_CAP_DISABLED) != 0U) {\n"
            "        return ui_finish_act_status(arena, dom, out_res, BLOOM_UI_STATUS_DISABLED, target_nid,",
        ),
    ]


def build_prefab_mutants() -> List[FlowMutant]:
    return [
        FlowMutant(
            "P01_sha256_arena_corrupt",
            "prefab_flow_v9_sdk",
            "    flow_sha256_bytes_hex(arena, sizeof(*arena), out_hex65);",
            "    flow_sha256_bytes_hex(arena, 64U, out_hex65);",
        ),
        FlowMutant(
            "P02_sha256_mesh_corrupt",
            "prefab_flow_v9_sdk",
            "    flow_sha256_bytes_hex(mesh, sizeof(*mesh), out_hex65);",
            "    flow_sha256_bytes_hex(mesh, 64U, out_hex65);",
        ),
        FlowMutant(
            "P03_fir1_kan_pin_inverted",
            "prefab_flow_v9_sdk",
            "    uint32_t next_b0   = flow_ir_emit_sel(p, 16U, pin_b0, req_b0, cur_b0, \"next_b0\");",
            "    uint32_t next_b0   = flow_ir_emit_sel(p, 16U, pin_b0, cur_b0, req_b0, \"next_b0\");",
        ),
        FlowMutant(
            "P04_trc_strobe_clamp_drop",
            "prefab_ssot_trc",
            "        if (style.strobe_hz_q16 > BLOOM_TRC_MAX_STROBE_HZ_Q16) {\n"
            "            style.strobe_hz_q16 = BLOOM_TRC_MAX_STROBE_HZ_Q16;\n"
            "        }",
            "        if (false && style.strobe_hz_q16 > BLOOM_TRC_MAX_STROBE_HZ_Q16) {\n"
            "            style.strobe_hz_q16 = BLOOM_TRC_MAX_STROBE_HZ_Q16;\n"
            "        }",
        ),
        FlowMutant(
            "P05_thermal_hyst_drop",
            "prefab_ssot_trc",
            "    bool hot = prev_overheated\n"
            "        ? (thermal_kelvin > BLOOM_THERMAL_EXIT_KELVIN)\n"
            "        : (thermal_kelvin >= BLOOM_THERMAL_ENTER_KELVIN);",
            "    bool hot = prev_overheated\n"
            "        ? (thermal_kelvin >= BLOOM_THERMAL_ENTER_KELVIN)\n"
            "        : (thermal_kelvin >= BLOOM_THERMAL_ENTER_KELVIN);",
        ),
        FlowMutant(
            "P06_kanlock_swap_bypass",
            "prefab_ssot_trc",
            "    uint16_t next_mode = (uint16_t)((mode_req & (uint16_t)(~kan_lock)) | (cur_mode & kan_lock));",
            "    uint16_t next_mode = (uint16_t)((mode_req & (uint16_t)(~kan_lock | kan_lock)) | (cur_mode & 0U));",
        ),
        FlowMutant(
            "P07_swept_ccd_discriminant_flip",
            "prefab_ccd_dsp",
            "    int64_t num = neg_b - (int64_t)sd;",
            "    int64_t num = neg_b + (int64_t)sd;",
        ),
        FlowMutant(
            "P08_modal_audio_mute",
            "prefab_ccd_dsp",
            "    rig->hdr.peak_abs_pcm = peak;",
            "    rig->hdr.peak_abs_pcm = (peak & 0);",
        ),
        FlowMutant(
            "P09_searu_heal_skip",
            "prefab_ccd_dsp",
            "    return searu_heal_and_swap(charts, BLOOM_FLOW_CHART_MASK, st, s_terms, n,\n"
            "                               kan_bit_pin, u_ext_q16, target_x_q16, jet_steps,\n"
            "                               io_seed, io_epoch, max_iters, NULL);",
            "    return false && searu_heal_and_swap(charts, BLOOM_FLOW_CHART_MASK, st, s_terms, n,\n"
            "                               kan_bit_pin, u_ext_q16, target_x_q16, jet_steps,\n"
            "                               io_seed, io_epoch, max_iters, NULL);",
        ),
        FlowMutant(
            "P10_gameplay_fulcrum_drop",
            "prefab_game_rewind",
            "        events |= BLOOM_GP_EVENT_FULCRUM;",
            "        events &= ~BLOOM_GP_EVENT_FULCRUM;",
        ),
        FlowMutant(
            "P11_rewind_epoch_drop",
            "prefab_game_rewind",
            "    ctrl->branch_epoch++;",
            "    ctrl->cursor = steps_back;",
        ),
        FlowMutant(
            "P12_merkle_verify_bypass",
            "prefab_game_rewind",
            "    return (im->merkle_lo32 == (uint32_t)(mk & 0xFFFFFFFFULL)) &&\n"
            "           (im->merkle_hi32 == (uint32_t)(mk >> 32ULL));",
            "    return (im->merkle_lo32 != (uint32_t)(mk & 0xFFFFFFFFULL)) &&\n"
            "           (im->merkle_hi32 != (uint32_t)(mk >> 32ULL));",
        ),
    ]


def main() -> int:
    schema_hdr = ROOT / "build" / "bloom_schema_abi.h"
    if not schema_hdr.is_file():
        print(f"ERROR: missing {schema_hdr}; run `make all` first.", file=sys.stderr)
        return 1

    cc = resolve_llvm_cc()
    with tempfile.TemporaryDirectory(prefix="bloom_sab_probe_") as td:
        tmp_dir = Path(td)
        probe_c = tmp_dir / "bloom_sabotage_probe.c"
        probe_c.write_text(PROBE_C_SOURCE, encoding="utf-8")

        clean_objs = compile_clean_objects(cc, tmp_dir)

        base_exe = tmp_dir / "golden_probe.bin"
        subprocess.run(
            [
                cc,
                "-std=c17",
                "-O2",
                "-Wall",
                "-Wextra",
                "-Wpedantic",
                "-Wconversion",
                "-Wundef",
                "-Wmissing-prototypes",
                "-Werror",
                f"-I{ROOT / 'include'}",
                f"-I{ROOT / 'build'}",
                "-isystem",
                str(FLOW_DIR / "core"),
                "-isystem",
                str(FLOW_DIR / "sdk"),
                str(probe_c),
                str(clean_objs["wire"]),
                str(clean_objs["scene"]),
                str(clean_objs["ui"]),
                str(clean_objs["prefab"]),
                "-o",
                str(base_exe),
            ],
            check=True,
        )
        golden_digest = subprocess.check_output(
            [str(base_exe), "--emit-digest"], text=True
        ).strip()

        print("================================================================================")
        print("  BLOOM v9.0.0 — FLOW v9.0.0 Shared C17 Sabotage Gate (flow_sabotage_runner.py)")
        print("================================================================================")
        print(f"  golden_baseline_digest : 0x{golden_digest}")

        suites = [
            ("bloom_scene.c", ROOT / "src" / "bloom_scene.c", [str(clean_objs["wire"]), str(clean_objs["ui"]), str(clean_objs["prefab"])], build_scene_mutants()),
            ("bloom_ui.c", ROOT / "src" / "bloom_ui.c", [str(clean_objs["wire"]), str(clean_objs["scene"]), str(clean_objs["prefab"])], build_ui_mutants()),
            ("bloom_prefab.c", ROOT / "src" / "bloom_prefab.c", [str(clean_objs["wire"]), str(clean_objs["scene"]), str(clean_objs["ui"])], build_prefab_mutants()),
        ]

        overall_rc = 0
        for label, ring0_path, extra_objs, mutants in suites:
            print(f"\n--- Module: {label} ({len(mutants)} mutants) ---")
            rc = run_sabotage_suite(
                ring0_path=ring0_path,
                cli_path=probe_c,
                schema_hdr=schema_hdr,
                mutants=mutants,
                repo_root=ROOT,
                extra_cli_flags=extra_objs,
                selftest_args=("selftest", golden_digest),
                prefix=f"bloom_{label.replace('.', '_')}_",
            )
            if rc != 0:
                overall_rc = 1

    return overall_rc


if __name__ == "__main__":
    raise SystemExit(main())
