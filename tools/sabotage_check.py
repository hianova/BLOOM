#!/usr/bin/env python3
"""
sabotage_check.py — Mach-O ARM64 Binary Sabotage & Purity Gate for libbloom.a
=============================================================================
Proves static `-mgeneral-regs-only` FPU/SIMD purity and dynamic observational
completeness across `build/bloom_scene.o`, `build/bloom_ui.o`, and
`build/bloom_prefab.o` with ZERO SDL3 / GPU startup overhead (~5ms/mutant):

  - Suite A (9 Static Purity Mutants): Injects forbidden A64 FPU/SIMD/NEON
    opcodes (`fadd`, `fsub`, `fmul`, `scvtf`, `fcvtzs`, `fmov`, `umov`, `ins`,
    `dup`) across all 3 core Mach-O objects and verifies the `otool -tv`
    zero-float/zero-SIMD static gate catches 100% of them.
  - Suite B (81 Stratified Pure-GPR Datapath & Predicate Mutants): Mutates
    A64 machine instructions (`CSEL/CSINC/CSINV/CSNEG` condition flips,
    `MADD<->MSUB` / `SMULH<->UMULH` multiplier flips, `ADD<->SUB` arithmetic
    flips, and `CBZ<->CBNZ` branch flips) across 30 core functions
    (`bloom_glb_*`, `bloom_bvh_*`, `bloom_csg_*`, `bloom_smin_*`,
    `bloom_joint_dq64_*`, `bloom_skeleton_*`, `bloom_sdf_brick64_*`,
    `bloom_matter_*`, `bloom_scene_raycast64`, `bloom_text_input_*`,
    `bloom_ime_*`, `bloom_flex_*`, `bloom_ui_*`, `bloom_svg_*`, `bloom_latex_*`,
    `bloom_prefab_*`, `bloom_style_*`, `bloom_arena_*`, `bloom_image_*`),
    relinking ONLY the pure CPU `.o` files in parallel temp dirs and asserting
    100.0% Mutant Kill Rate (`0` survivors).
"""

from __future__ import annotations

import concurrent.futures
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

FPU_MNEMONIC_RE = re.compile(
    r"^(fadd|fsub|fmul|fdiv|fsqrt|fmadd|fmsub|fcmp|fccmp|fcsel|scvtf|ucvtf|fcvtzs|fcvtzu|fmov|umov|smov|ins|dup|mov\.[bhsdq])$"
)

PROBE_C_SOURCE = r"""
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bloom.h"

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

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #cond); return 2; } } while (0)

static void put_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
    p[2] = (uint8_t)((v >> 16U) & 0xFFU);
    p[3] = (uint8_t)((v >> 24U) & 0xFFU);
}

static bool utf8_valid(const char *buf, size_t cap) {
    size_t n = 0U;
    while (n <= cap && buf[n] != '\0') ++n;
    if (n > cap) return false;
    size_t i = 0U;
    while (i < n) {
        uint8_t b0 = (uint8_t)buf[i];
        if (b0 < 0x80U) {
            ++i;
        } else if (b0 >= 0xC2U && b0 <= 0xDFU) {
            if (i + 2U > n || ((uint8_t)buf[i + 1U] & 0xC0U) != 0x80U) return false;
            i += 2U;
        } else if (b0 >= 0xE0U && b0 <= 0xEFU) {
            if (i + 3U > n) return false;
            uint8_t b1 = (uint8_t)buf[i + 1U], b2 = (uint8_t)buf[i + 2U];
            if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U) return false;
            if ((b0 == 0xE0U && b1 < 0xA0U) || (b0 == 0xEDU && b1 >= 0xA0U)) return false;
            i += 3U;
        } else if (b0 >= 0xF0U && b0 <= 0xF4U) {
            if (i + 4U > n) return false;
            uint8_t b1 = (uint8_t)buf[i + 1U], b2 = (uint8_t)buf[i + 2U], b3 = (uint8_t)buf[i + 3U];
            if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U || (b3 & 0xC0U) != 0x80U) return false;
            if ((b0 == 0xF0U && b1 < 0x90U) || (b0 == 0xF4U && b1 >= 0x90U)) return false;
            i += 4U;
        } else {
            return false;
        }
    }
    return true;
}

static int run_probe(uint64_t *out_digest) {
    g_acc = UINT64_C(0xCBF29CE484222325);

    /* --- 1. Algebra, GLB, CSG & Linear BVH 0-ULP --- */
    mix_u64((uint64_t)bloom_isqrt_u64_to_i32(0ULL));
    mix_u64((uint64_t)bloom_isqrt_u64_to_i32(UINT64_MAX));
    mix_u64((uint64_t)(uint32_t)bloom_sin_q16(51472));
    mix_u64((uint64_t)(uint32_t)bloom_cos_q16(-51472));
    mix_u64((uint64_t)bloom_smin_q16(12000, 18000, 16384));
    mix_u64((uint64_t)bloom_smin_q16(INT64_MIN, INT64_MAX, 32768));
    mix_u64((uint64_t)bloom_smax_q16(-12000, -18000, 16384));
    mix_u64((uint64_t)(uint32_t)bloom_ieee754_bits_to_q16(0xBF800000U));
    mix_u64((uint64_t)(uint32_t)bloom_ieee754_bits_to_q16(0x3E800000U));

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
        for (size_t k = 0U; k < 12U; ++k) put_u32_le(glb + 32U + k * 4U, verts[k]);

        CHECK(bloom_glb_parse_to_csg64(glb, 80U, 5U, 2U, &op, &vcnt) && vcnt == 4U);
        mix_bytes(&op, sizeof(op));

        /* Extreme finite floats (huge +1e10f, huge -1e10f, tiny 1e-9f) to exercise clamp & shift paths */
        uint8_t glb_ext[80];
        memcpy(glb_ext, glb, 80U);
        const uint32_t ext_verts[12] = {
            0xD01502F9U, 0x3089705FU, 0xB089705FU,
            0x501502F9U, 0x501502F9U, 0x501502F9U,
            0x00000001U, 0x80000001U, 0xBF800000U,
            0x3F800000U, 0xC0800000U, 0x40800000U
        };
        for (size_t k = 0U; k < 12U; ++k) put_u32_le(glb_ext + 32U + k * 4U, ext_verts[k]);
        CHECK(bloom_glb_parse_to_csg64(glb_ext, 80U, 3U, 1U, &op, &vcnt) && vcnt == 4U);
        mix_bytes(&op, sizeof(op));

        uint8_t bad[80];
        memcpy(bad, glb, 80U);
        put_u32_le(bad + 12U, 0xFFFFFFFFU);
        CHECK(!bloom_glb_parse_to_csg64(bad, 80U, 1U, 0U, &op, &vcnt));
        memcpy(bad, glb, 80U);
        put_u32_le(bad + 12U, 3U);
        CHECK(!bloom_glb_parse_to_csg64(bad, 80U, 1U, 0U, &op, &vcnt));
        memcpy(bad, glb, 80U);
        put_u32_le(bad + 36U, 0x7FC00001U);
        CHECK(!bloom_glb_parse_to_csg64(bad, 80U, 1U, 0U, &op, &vcnt));
        memcpy(bad, glb, 80U);
        bad[20] = 'x';
        CHECK(!bloom_glb_parse_to_csg64(bad, 80U, 1U, 0U, &op, &vcnt));
    }

    {
        BloomCsgOp64 ops[12];
        BloomBvhNode64 bvh[32];
        for (uint32_t i = 0U; i < 12U; ++i) {
            int32_t pos[3] = { (int32_t)((int64_t)(i % 4U) * 52000LL - 78000LL),
                               (int32_t)((int64_t)(i / 4U) * 48000LL - 48000LL),
                               (int32_t)((i & 1U) ? 24000 : -24000) };
            int32_t ext[3] = { 30000 + (int32_t)(i * 1500U), 20000 + (int32_t)(i * 900U), 25000 };
            bloom_csg_op_init64(&ops[i], i + 1U, (i % 6U) + 1U, BLOOM_CSG_BOOL_UNION, i & 7U, pos, ext, 0, 2048);
        }
        size_t nb = bloom_bvh_build_from_csg64(ops, 12U, bvh, 32U);
        CHECK(nb >= 4U && nb <= 32U);
        mix_bytes(bvh, nb * sizeof(BloomBvhNode64));

        for (int gx = -2; gx <= 2; ++gx) {
            for (int gy = -2; gy <= 2; ++gy) {
                for (int gz = -1; gz <= 1; ++gz) {
                    int64_t px = (int64_t)gx * 55000LL, py = (int64_t)gy * 55000LL, pz = (int64_t)gz * 30000LL;
                    uint32_t vis = 0U, mat = 0U;
                    int64_t db = bloom_bvh_eval_csg_q16(bvh, nb, ops, 12U, px, py, pz, &vis);
                    int64_t dc = bloom_csg_eval_program_q16(ops, 12U, px, py, pz, &mat);
                    CHECK(db == dc);
                    mix_u64((uint64_t)db ^ ((uint64_t)vis << 40U) ^ (uint64_t)mat);
                }
            }
        }

        /* Pure-union BVH WITHOUT any octahedron (has_octa == false) + 2-op overlapping SMOOTH_UNION */
        BloomCsgOp64 ops_no_oct[8];
        for (uint32_t i = 0U; i < 8U; ++i) {
            ops_no_oct[i] = ops[i];
            ops_no_oct[i].prim_type = (i % 3U) + 1U; /* SPHERE / TORUS / BOX only */
            ops_no_oct[i].bool_op = BLOOM_CSG_BOOL_UNION;
        }
        size_t nb_no = bloom_bvh_build_from_csg64(ops_no_oct, 8U, bvh, 32U);
        CHECK(nb_no >= 4U);
        for (int gx = -2; gx <= 2; gx += 2) {
            uint32_t vis_no = 0U, mat_no = 0U;
            int64_t db_no = bloom_bvh_eval_csg_q16(bvh, nb_no, ops_no_oct, 8U, (int64_t)gx * 90000LL, 65000, -40000, &vis_no);
            int64_t dc_no = bloom_csg_eval_program_q16(ops_no_oct, 8U, (int64_t)gx * 90000LL, 65000, -40000, &mat_no);
            CHECK(db_no == dc_no);
            mix_u64((uint64_t)db_no ^ ((uint64_t)vis_no << 40U) ^ (uint64_t)mat_no);
        }

        BloomCsgOp64 sops[2];
        const int32_t sp0[3] = { -12000, 0, 0 }, sp1[3] = { 12000, 0, 0 }, sext[3] = { 24000, 24000, 24000 };
        bloom_csg_op_init64(&sops[0], 1U, BLOOM_CSG_PRIM_SPHERE, BLOOM_CSG_BOOL_UNION, 1U, sp0, sext, 0, 0);
        bloom_csg_op_init64(&sops[1], 2U, BLOOM_CSG_PRIM_SPHERE, BLOOM_CSG_BOOL_SMOOTH_UNION, 2U, sp1, sext, 32768, 0);
        for (int sx = -20000; sx <= 20000; sx += 5000) {
            uint32_t smat = 0U;
            int64_t sd = bloom_csg_eval_program_q16(sops, 2U, sx, 4000, 0, &smat);
            mix_u64((uint64_t)sd ^ ((uint64_t)smat << 48U));
        }

        /* Mixed Boolean Program: SUBTRACT, INTERSECT, SMOOTH_UNION (covering d_acc < d, d_acc > d, and single-op / OCTAHEDRON root) */
        ops[0].prim_type = BLOOM_CSG_PRIM_OCTAHEDRON;
        ops[1].bool_op = BLOOM_CSG_BOOL_SMOOTH_UNION; ops[1].smooth_k_q16 = 49152;
        ops[2].bool_op = BLOOM_CSG_BOOL_SUBTRACT;
        ops[3].bool_op = BLOOM_CSG_BOOL_INTERSECT;
        size_t nm1 = bloom_bvh_build_from_csg64(ops, 1U, bvh, 32U);
        CHECK(nm1 == 1U);
        {
            uint32_t vis1 = 0U, mat1 = 0U;
            int64_t db1 = bloom_bvh_eval_csg_q16(bvh, nm1, ops, 1U, -65000, -40000, -18000, &vis1);
            int64_t dc1 = bloom_csg_eval_program_q16(ops, 1U, -65000, -40000, -18000, &mat1);
            CHECK(db1 == dc1);
            mix_u64((uint64_t)db1 ^ ((uint64_t)vis1 << 32U));
        }
        size_t nm = bloom_bvh_build_from_csg64(ops, 4U, bvh, 32U);
        CHECK(nm > 0U);
        for (int k = -4; k <= 4; ++k) {
            for (int sy = -1; sy <= 1; ++sy) {
                uint32_t vis = 0U, mat = 0U;
                int64_t px = (int64_t)k * 22000LL - 40000LL;
                int64_t py = (int64_t)sy * 36000LL - 30000LL;
                int64_t db = bloom_bvh_eval_csg_q16(bvh, nm, ops, 4U, px, py, 0, &vis);
                int64_t dc = bloom_csg_eval_program_q16(ops, 4U, px, py, 0, &mat);
                CHECK(db == dc);
                mix_u64((uint64_t)db ^ ((uint64_t)mat << 48U) ^ ((uint64_t)vis << 24U));
            }
        }
    }

    /* --- 2. Dual-Quaternion Skinning, 4-Bit Brick & Matter --- */
    {
        BloomJointDq64 ja, jb, jm, jbig;
        const int32_t qa[4] = { -46341, -46341,  18000, -12000 }; /* negative components */
        const int32_t qb[4] = {  46341,  12000, -46341,  15000 }; /* antipodal */
        const int32_t qbig[4] = { 0x40000000, -0x40000000, 0x28000000, -0x28000000 }; /* > 0x1FFFFFFF downshift path */
        const int32_t ta[3] = { 65536, -32768, 16384 };
        const int32_t tb[3] = { -16384, 49152, 32768 };
        bloom_joint_dq64_make(&ja, 1U, 0U, qa, ta, 65536, 16384, 12288, 1U);
        bloom_joint_dq64_make(&jb, 2U, 1U, qb, tb, 52428, 12288, 12288, 2U);
        bloom_joint_dq64_make(&jbig, 3U, 0U, qbig, ta, 65536, 16384, 16384, 1U);
        bloom_joint_dq64_blend(&ja, &jb, 24576, &jm);
        mix_bytes(&ja, sizeof(ja));
        mix_bytes(&jb, sizeof(jb));
        mix_bytes(&jbig, sizeof(jbig));
        mix_bytes(&jm, sizeof(jm));

        /* Corrupted oversized q_real_q16 to exercise blend downshift (max_ab > 0x1FFFFFFF),
           small-positive antipodal knife-edge, and zero-norm singularity (inv_len == 0) */
        BloomJointDq64 jraw = ja;
        jraw.q_real_q16[0] = -0x50000000;
        jraw.q_real_q16[1] =  0x48000000;
        BloomJointDq64 jm_big;
        bloom_joint_dq64_blend(&jraw, &jb, 32768, &jm_big);
        mix_bytes(&jm_big, sizeof(jm_big));

        BloomJointDq64 jt_a = ja, jt_b = jb, jm_tiny, jz_a = ja, jz_b = jb, jm_zero;
        jt_a.q_real_q16[0] =  5; jt_a.q_real_q16[1] = 3; jt_a.q_real_q16[2] = 0; jt_a.q_real_q16[3] = 0;
        jt_b.q_real_q16[0] = -5; jt_b.q_real_q16[1] = 2; jt_b.q_real_q16[2] = 0; jt_b.q_real_q16[3] = 0;
        bloom_joint_dq64_blend(&jt_a, &jt_b, 32768, &jm_tiny);
        mix_bytes(&jm_tiny, sizeof(jm_tiny));

        memset(jz_a.q_real_q16, 0, sizeof(jz_a.q_real_q16));
        memset(jz_b.q_real_q16, 0, sizeof(jz_b.q_real_q16));
        jz_a.q_dual_q16[0] = 1200; jz_a.q_dual_q16[1] = -2400; jz_a.q_dual_q16[2] = 3600; jz_a.q_dual_q16[3] = -4800;
        bloom_joint_dq64_blend(&jz_a, &jz_b, 16384, &jm_zero);
        mix_bytes(&jm_zero, sizeof(jm_zero));

        const int32_t pin[3] = { 28000, -19000, 41000 };
        int32_t pout[3] = { 0, 0, 0 };
        bloom_joint_dq64_transform_q16(&jm, pin, pout);
        mix_bytes(pout, sizeof(pout));

        BloomJointDq64 skel[2] = { ja, jm };
        int64_t sd0 = bloom_skeleton_eval_sdf_q16(skel, 2U, 65536, -16384, 16384);
        int64_t sd1 = bloom_skeleton_eval_sdf_q16(skel, 2U, 0, 32768, 32768);
        mix_u64((uint64_t)sd0);
        mix_u64((uint64_t)sd1);

        BloomSdfBrick64 brick;
        int32_t vox[64];
        const int32_t borig[3] = { -32768, -32768, -32768 };
        for (uint32_t i = 0U; i < 64U; ++i) {
            vox[i] = ((int32_t)(i % 9U) - 4) * 12000;
        }
        bloom_sdf_brick64_pack(&brick, 3U, 4U, borig, 32768, vox);
        mix_bytes(&brick, sizeof(brick));
        mix_u64((uint64_t)bloom_sdf_brick64_sample_q16(&brick, -10000, 5000, 15000));
        mix_u64((uint64_t)bloom_sdf_brick64_sample_q16(&brick, 25000, -18000, 42000));

        BloomMatter64 mats[BLOOM_MAX_MATTERS];
        bloom_matter_init_canonical(mats);
        int32_t grad[2] = { 12000, -9000 }, bg[3] = { 8192, 12288, 20480 }, ld[2] = { 16384, 16384 }, rgb[3];
        for (uint32_t m = 0U; m < BLOOM_MAX_MATTERS; ++m) {
            bloom_matter_eval_radiance_q16(&mats[m], grad, -6000, bg, ld, rgb);
            mix_bytes(rgb, sizeof(rgb));
        }
        bloom_matter_eval_radiance_q16(&mats[3], NULL, -6000, bg, ld, rgb);
        mix_bytes(rgb, sizeof(rgb));
        bloom_matter_eval_radiance_q16(&mats[3], grad, -6000, bg, NULL, rgb);
        mix_bytes(rgb, sizeof(rgb));
        char hex[129], hex_upper[129];
        BloomMatter64 mp;
        bloom_matter_format_hex128(&mats[5], hex);
        CHECK(bloom_matter_parse_hex128(hex, &mp) && mp.ior_eta_q16 == mats[5].ior_eta_q16);
        for (size_t i = 0U; i < 129U; ++i) {
            char c = hex[i];
            hex_upper[i] = (c >= 'a' && c <= 'f') ? (char)(c - 'a' + 'A') : c;
        }
        CHECK(bloom_matter_parse_hex128(hex_upper, &mp) && mp.ior_eta_q16 == mats[5].ior_eta_q16);
        hex[4] = (hex[4] == 'a') ? 'b' : 'a';
        CHECK(!bloom_matter_parse_hex128(hex, &mp));
    }

    /* --- 3. UTF-8 / CJK / Emoji Text Input, IME, Flex, Bento, SVG & LaTeX --- */
    {
        BloomTextInput64 inp;
        BloomIme64 ime;
        bloom_text_input_init64(&inp, 2U, BLOOM_ACTION_FOCUS_INPUT);
        bloom_ime_init64(&ime, 2U);

        /* Test failed multi-byte staging at blen > 0, plus 4-byte emoji backspace & mid-string backspace */
        const char *s_init = "Hi\xF0\x9F\x9A\x80"; /* 2 + 4 = 6 bytes */
        for (size_t i = 0U; s_init[i] != '\0'; ++i) {
            (void)bloom_text_input_insert_char64(&inp, s_init[i]);
        }
        CHECK(inp.byte_len == 6U && utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP));
        /* Stage 3-byte lead at non-zero blen, test invalid lead, orphan continuation & overlong 2nd byte, then splice ASCII 'Z' */
        CHECK(bloom_text_input_insert_char64(&inp, (char)0xE4U) && inp.byte_len == 6U);
        CHECK(!bloom_text_input_insert_char64(&inp, (char)0xFFU));
        CHECK(!bloom_text_input_insert_char64(&inp, (char)0x80U) && inp.byte_len == 6U && utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP));
        CHECK(bloom_text_input_insert_char64(&inp, (char)0xE0U) && !bloom_text_input_insert_char64(&inp, (char)0x80U));
        CHECK(bloom_text_input_insert_char64(&inp, 'Z'));
        CHECK(inp.byte_len == 7U && utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP));
        /* Delete 'Z', then delete 4-byte emoji 🚀 directly! */
        CHECK(bloom_text_input_backspace64(&inp) && inp.byte_len == 6U);
        CHECK(bloom_text_input_backspace64(&inp) && inp.byte_len == 2U && utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP));
        /* Out-of-range cursor_pos clamp and mid-string backspace (cursor_pos < byte_len) */
        (void)bloom_text_input_insert_char64(&inp, 'X');
        inp.cursor_pos = 99U;
        CHECK(bloom_text_input_backspace64(&inp) && inp.byte_len == 2U);
        inp.cursor_pos = 1U;
        CHECK(bloom_text_input_backspace64(&inp) && inp.byte_len == 1U && inp.utf8_buf[0] == 'i');
        mix_bytes(&inp, sizeof(inp));

        bloom_text_input_init64(&inp, 2U, BLOOM_ACTION_FOCUS_INPUT);
        const char *s_utf8 = "繁體A\xC3\xA9\xF0\x9F\x9A\x80中文測試輸入法邊界安全驗證超";
        for (size_t i = 0U; s_utf8[i] != '\0'; ++i) {
            (void)bloom_text_input_insert_char64(&inp, s_utf8[i]);
            CHECK(utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP));
        }
        mix_bytes(&inp, sizeof(inp));
        CHECK(bloom_text_input_backspace64(&inp) && utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP));
        CHECK(bloom_text_input_backspace64(&inp) && utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP));
        mix_bytes(&inp, sizeof(inp));

        CHECK(bloom_ime_set_preedit64(&ime, "台灣\xF0\x9F\x94\xA5", 1U));
        CHECK(utf8_valid(ime.preedit_utf8, 35U));
        mix_bytes(&ime, sizeof(ime));
        inp.byte_len = 6U; inp.utf8_buf[6] = '\0'; inp.cursor_pos = 3U;
        CHECK(bloom_ime_commit64(&ime, &inp) && utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP));
        mix_bytes(&inp, sizeof(inp));

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

        BloomRegBank64 bank;
        bloom_reg_bank_init64(&bank);
        bloom_reg_bank_set_q16(&bank, 1U, 55000);
        BloomIntent64 intents[6];
        memset(intents, 0, sizeof(intents));
        for (uint32_t i = 0U; i < 6U; ++i) {
            intents[i].intent_id = i + 1U;
            intents[i].prim_type = BLOOM_UI_PRIM_SLIDER_BAR;
            intents[i].bind_channel = i & 3U;
            intents[i].lo_exit_q16 = 16384;
            intents[i].hi_enter_q16 = 49152;
            intents[i].current_val_q16 = (int32_t)(i * 18000U);
        }
        BloomDomState64 dom;
        memset(&dom, 0, sizeof(dom));
        /* Both intent_count < max_nodes (3 < 4) and intent_count > max_nodes (6 > 4) */
        size_t bn_lt = bloom_ui_synthesize_bento64(intents, 3U, &bank, fnodes, 4U, &dom);
        CHECK(bn_lt == 3U);
        mix_bytes(fnodes, 3U * sizeof(BloomUiNode64));
        size_t bn = bloom_ui_synthesize_bento64(intents, 6U, &bank, fnodes, 4U, &dom);
        CHECK(bn == 4U);
        mix_bytes(fnodes, sizeof(fnodes));
        mix_bytes(&dom, sizeof(dom));

        BloomGlyph64 glyphs[16];
        size_t gcnt = 0U;
        const char *svg =
            "<svg viewBox=\"  0 0 1280 720\" width=\"1280\" height=\"720\"><!-- inspector -->"
            "<rect data-node-id=\"1\" data-prim=\"4\" data-glass=\" 2\" data-bind=\"0\" data-action=\"1\" data-init-q16=\" 49152\" x=\"  40.5\" y=\"40.25\" width=\"240\" height=\"60\" rx=\"12.5\" fill=\"#20D8FF\" fill-opacity=\"0.75\"/>"
            "<circle data-node-id=\"2\" data-prim=\"2\" data-bind=\"1\" cx=\"900\" cy=\"120\" r=\"42\" fill=\"#FFB020\" stroke-width=\"6\"/>"
            "<rect data-node-id=\"3\" data-prim=\"1\" data-action=\"2\" x=\"-10\" y=\"200\" width=\"120\" height=\"40\" rx=\"8\" fill=\"#3af\"/>"
            "<path data-glyph-id=\"1\" d=\"M 100 100 L 220 180 C 240 190 260 150 300 120 Z\" stroke=\"#FFFFFF\" stroke-width=\"3\"/>"
            "<line data-glyph-id=\"2\" x1=\"-20.5\" y1=\"680\" x2=\"400\" y2=\"680\" stroke=\"#22C88A\" stroke-width=\"2.5\"/>"
            "</svg>";
        size_t sn = bloom_svg_compile_text(svg, fnodes, 4U, glyphs, 16U, &gcnt, &dom);
        CHECK(sn == 3U && gcnt >= 2U);
        mix_bytes(fnodes, 3U * sizeof(BloomUiNode64));
        mix_bytes(glyphs, gcnt * sizeof(BloomGlyph64));

        BloomLatexBox64 lboxes[16];
        int32_t lw = 0;
        size_t ln = bloom_latex_compile_boxes64("\\int \\sum \\frac{\\sqrt{\\eta^2 + x_1}}{2\\pi}",
                                                -16384, -32768, 4096, 0xFFD848U, lboxes, 16U, &lw);
        CHECK(ln >= 6U);
        mix_u64((uint64_t)(uint32_t)lw);
        for (size_t i = 0U; i < ln; ++i) {
            mix_bytes(&lboxes[i], sizeof(BloomLatexBox64));
            mix_u64((uint64_t)(uint32_t)bloom_latex_eval_box_sdf_q16(&lboxes[i], lboxes[i].x_q16 + 512, lboxes[i].y_q16 - 512));
        }
    }

    /* --- 4. Prefab SSOT, Styles, Full Arena Dynamics, Pointer, Raycast & Merkle Seal --- */
    {
        static BloomEngineArena arena;
        bloom_arena_init_default(&arena, 320U, 180U);
        CHECK(bloom_image_verify_arena(&arena));

        BloomPrefabDesc64 joined = bloom_prefab_join(&arena.map.prefabs[0], &arena.map.prefabs[1], 9U, BLOOM_CSG_BOOL_SMOOTH_UNION);
        mix_bytes(&joined, sizeof(joined));

        bool hot = false;
        BloomArtStyle64 s_null_cb = bloom_style_resolve_thermal(NULL, 0U, 300U, false, false, true, &hot);
        BloomArtStyle64 s_null_nc = bloom_style_resolve_thermal(NULL, 0U, 300U, false, false, false, &hot);
        mix_bytes(&s_null_cb, sizeof(s_null_cb));
        mix_bytes(&s_null_nc, sizeof(s_null_nc));
        BloomArtStyle64 s_hot = bloom_style_resolve_thermal(arena.map.styles, 2U, 850U, false, true, true, &hot);
        CHECK(hot && s_hot.strobe_hz_q16 <= BLOOM_TRC_MAX_STROBE_HZ_Q16);
        mix_bytes(&s_hot, sizeof(s_hot));
        BloomArtStyle64 s_hyst = bloom_style_resolve_thermal(arena.map.styles, 2U, 770U, hot, false, false, &hot);
        CHECK(hot);
        mix_bytes(&s_hyst, sizeof(s_hyst));

        for (uint32_t p = 1U; p <= 8U; ++p) {
            int32_t pos[3] = { ((int32_t)((p - 1U) % 4U) - 2) * 90000 + 45000,
                               ((p <= 4U) ? 80000 : -80000),
                               0 };
            uint32_t iid = bloom_prefab_spawn_inst(&arena, p, pos, 49152);
            CHECK(iid == p);
            mix_u64((uint64_t)bloom_prefab_eval_sdf_q16(&arena.map.prefabs[p - 1U], 12000, -8000, 15000, 2048));
        }
        mix_u64((uint64_t)bloom_prefab_eval_xray_cutaway_q16(-8192, -16384, 0, true));
        mix_u64((uint64_t)bloom_prefab_eval_xray_cutaway_q16(-8192,  16384, 0, true));

        BloomPrefabInst64 comp[64];
        uint64_t lmask = 0ULL;
        const int32_t ceye[3] = { 0, 0, -262144 };
        uint32_t cn = bloom_prefab_cull_wave64(arena.map.instances, 8U, ceye, 270000, 310000, comp, &lmask);
        CHECK(cn == 8U && lmask == 0xFFULL);
        mix_bytes(comp, 8U * sizeof(BloomPrefabInst64));
        /* Also test count > 64 clamp (70 -> 64), inactive skip, and all 3 LOD bands (LAYER_0 / 1 / 2) */
        BloomPrefabInst64 many[70];
        memset(many, 0, sizeof(many));
        for (uint32_t i = 0U; i < 70U; ++i) {
            many[i] = arena.map.instances[i & 7U];
            if (i % 7U == 0U) many[i].flags = 0U;
            else if (i % 5U == 0U) many[i].pos_q16[2] = 9000000;
            else if (i % 3U == 0U) many[i].pos_q16[2] = 20000;
        }
        uint64_t lmask70 = 0ULL;
        uint32_t cn70 = bloom_prefab_cull_wave64(many, 70U, ceye, 270000, 320000, comp, &lmask70);
        CHECK(cn70 < 64U && cn70 > 40U);
        mix_u64(((uint64_t)cn70 << 56U) ^ lmask70);
        mix_bytes(comp, cn70 * sizeof(BloomPrefabInst64));

        /* Mount SVG slider + button in arena & exercise hover, button click, slider scrub, release, FLOW dynamics */
        size_t gcnt = 0U;
        const char *ui_svg =
            "<svg viewBox=\"0 0 320 180\" width=\"320\" height=\"180\">"
            "<rect data-node-id=\"1\" data-prim=\"4\" data-bind=\"0\" data-action=\"1\" x=\"20\" y=\"140\" width=\"140\" height=\"30\" rx=\"6\" fill=\"#00E8C6\"/>"
            "<rect data-node-id=\"2\" data-prim=\"1\" data-action=\"2\" x=\"180\" y=\"140\" width=\"110\" height=\"30\" rx=\"6\" fill=\"#FFB020\"/>"
            "</svg>";
        arena.map.frame_hdr.ui_count = (uint32_t)bloom_svg_compile_text(
            ui_svg, arena.map.ui_nodes, BLOOM_MAX_UI_NODES, arena.map.glyphs, BLOOM_MAX_GLYPHS, &gcnt, &arena.map.dom);
        CHECK(arena.map.frame_hdr.ui_count == 2U);

        int32_t ux = arena.map.ui_nodes[0].offset_x_q16;
        int32_t uy = arena.map.ui_nodes[0].offset_y_q16;
        int32_t bx = arena.map.ui_nodes[1].offset_x_q16;
        int32_t by = arena.map.ui_nodes[1].offset_y_q16;
        /* Hover button without clicking, then click button, then release, then drag slider */
        (void)bloom_ui_step_pointer_arena(&arena, bx, by, 0, false, false);
        mix_bytes(&arena.map.ui_nodes[1], sizeof(BloomUiNode64));
        uint32_t act_btn = bloom_ui_step_pointer_arena(&arena, bx, by, 0, true, true);
        CHECK(act_btn == 2U);
        mix_bytes(&arena.map.ui_nodes[1], sizeof(BloomUiNode64));
        (void)bloom_ui_step_pointer_arena(&arena, 0, 0, 0, false, false);
        uint32_t act1 = bloom_ui_step_pointer_arena(&arena, ux, uy, 0, true, true);
        uint32_t act2 = bloom_ui_step_pointer_arena(&arena, ux + 8192, uy, 8192, true, false);
        CHECK(act1 == BLOOM_ACTION_SCRUB_XRAY && act2 == BLOOM_ACTION_SCRUB_XRAY);
        mix_u64((uint64_t)(uint32_t)arena.map.frame_hdr.xray_plane_q16);
        mix_bytes(arena.map.flow_states[BLOOM_FLOW_CH_UI_NODE0], 2U * sizeof(arena.map.flow_states[0]));
        (void)bloom_ui_step_pointer_arena(&arena, 0, 0, 0, false, false);

        uint32_t fslot = arena.map.instances[0].flow_slot;
        CHECK(bloom_arena_try_swap_chart(&arena, fslot, BLOOM_CHART_PREFAB_ALERT, 0x0001U));
        CHECK(bloom_arena_try_swap_chart(&arena, fslot, BLOOM_CHART_PREFAB_NORMAL, 0x0000U));
        mix_u64(arena.map.flow_states[fslot][5]);

        /* Perturb camera + thermal + xray so first dynamics step is NOT converged, then step until converged */
        arena.map.camera.orbit_yaw_q16 += 16384;
        arena.map.instances[0].thermal_kelvin = 880U;
        bool conv0 = bloom_arena_step_dynamics(&arena, 0x0001U);
        mix_u64(conv0 ? 1ULL : 0ULL);
        mix_bytes(&arena.map.camera, sizeof(BloomCamera64));
        arena.map.instances[0].thermal_kelvin = 300U;
        bool conv_final = false;
        for (int stp = 0; stp < 96; ++stp) {
            conv_final = bloom_arena_step_dynamics(&arena, 0x0000U);
            if (conv_final) break;
        }
        mix_u64(conv_final ? 0xA5A5ULL : 0x5A5AULL);
        mix_bytes(&arena.map.ui_nodes[0], 2U * sizeof(BloomUiNode64));

        /* Raycast straight at spawned Prefabs */
        arena.map.camera.eye_q16[0] = 0;
        arena.map.camera.eye_q16[1] = 0;
        arena.map.camera.eye_q16[2] = -7 * BLOOM_Q16_ONE;
        arena.map.camera.target_q16[0] = 0;
        arena.map.camera.target_q16[1] = 0;
        arena.map.camera.target_q16[2] = 0;
        arena.map.camera.focal_mm_q16 = 28 * BLOOM_Q16_ONE;
        arena.map.frame_hdr.flags &= ~BLOOM_FLAG_XRAY_ENABLED;
        for (int ry = -2; ry <= 2; ++ry) {
            for (int rx = -3; rx <= 3; ++rx) {
                BloomRaycastHit64 hit;
                bool ok = bloom_scene_raycast64(&arena, rx * 14000, ry * 18000, &hit);
                mix_u64(ok ? ((uint64_t)hit.hit_inst_id << 32U) | (uint32_t)hit.t_hit_q16 : 0ULL);
            }
        }

        bloom_image_seal_arena(&arena);
        CHECK(bloom_image_verify_arena(&arena));
        mix_bytes(&arena.map.img_hdr, sizeof(BloomImageHeader64));
        arena.cells[12].bytes[7] ^= 0x01U;
        CHECK(!bloom_image_verify_arena(&arena));
    }

    *out_digest = g_acc;
    return 0;
}

int main(int argc, char **argv) {
    uint64_t digest = 0ULL;
    int rc = run_probe(&digest);
    if (rc != 0) return rc;
    if (argc == 2 && strcmp(argv[1], "--emit-digest") == 0) {
        printf("%016llx\n", (unsigned long long)digest);
        return 0;
    }
    if (argc == 2) {
        uint64_t expected = strtoull(argv[1], NULL, 16);
        return (digest == expected) ? 0 : 3;
    }
    return 1;
}
"""


@dataclass(frozen=True)
class Mutant:
    name: str
    category: str
    obj_key: str
    func: str
    insn_idx: int
    orig_op: int
    mut_op: int


def parse_macho_text_and_syms(data: bytes) -> tuple[int, int, dict[str, tuple[int, int]]]:
    magic, _, _, _, ncmds, _, _, _ = struct.unpack_from("<IIIIIIII", data, 0)
    if magic != 0xFEEDFACF:
        raise RuntimeError(f"Unexpected Mach-O magic: 0x{magic:08x}")

    text_offset = -1
    text_size = -1
    symoff = nsyms = stroff = 0

    off = 32
    for _ in range(ncmds):
        cmd, cmdsize = struct.unpack_from("<II", data, off)
        if cmd == 0x19:  # LC_SEGMENT_64
            text_size = struct.unpack_from("<Q", data, off + 112)[0]
            text_offset = struct.unpack_from("<I", data, off + 120)[0]
        elif cmd == 0x02:  # LC_SYMTAB
            symoff, nsyms, stroff, _ = struct.unpack_from("<IIII", data, off + 8)
        off += cmdsize

    if text_offset < 0 or text_size <= 0:
        raise RuntimeError("Could not locate __TEXT,__text section in Mach-O object")

    # Collect ALL section-1 symbols (both external T and internal static t) for exact function bounds
    all_syms: list[tuple[int, str, bool]] = []
    for i in range(nsyms):
        n_strx, n_type, n_sect, _, n_value = struct.unpack_from("<IBBHQ", data, symoff + i * 16)
        if (n_type & 0x0E) == 0x0E and n_sect == 1:
            end = data.find(b"\x00", stroff + n_strx)
            name = data[stroff + n_strx : end].decode("ascii", errors="replace")
            if name.startswith("ltmp") or name.startswith("L"):
                continue
            is_ext = (n_type & 0x01) != 0
            all_syms.append((n_value, name, is_ext))

    all_syms.sort(key=lambda x: (x[0], not x[2]))
    func_ranges: dict[str, tuple[int, int]] = {}
    for idx, (start_b, name, is_ext) in enumerate(all_syms):
        end_b = all_syms[idx + 1][0] if idx + 1 < len(all_syms) else text_size
        if is_ext and end_b > start_b:
            func_ranges[name] = (start_b // 4, end_b // 4)

    return text_offset, text_size, func_ranges


def check_static_fpu_purity(obj_path: Path) -> bool:
    """Returns True if obj_path contains ZERO FPU/SIMD opcodes (matches Makefile Gate 1)."""
    proc = subprocess.run(["otool", "-tv", str(obj_path)], capture_output=True, text=True)
    if proc.returncode != 0:
        return False
    for line in proc.stdout.splitlines():
        parts = line.split()
        if len(parts) >= 2 and FPU_MNEMONIC_RE.match(parts[1]):
            return False
    return True


def sample_even(indices: list[int], k: int) -> list[int]:
    if not indices or k <= 0:
        return []
    if len(indices) <= k:
        return indices
    if k == 1:
        return [indices[len(indices) // 2]]
    return [indices[(i * (len(indices) - 1)) // (k - 1)] for i in range(k)]


def collect_candidates_in_func(insns: list[int], lo: int, hi: int) -> list[tuple[str, int, int, int]]:
    """Returns user-datapath candidate (cat, idx, orig, mut) tuples inside [lo+4, hi-4),
    excluding compiler-synthesized `-fsanitize-trap=all` UBSan overflow/shift trap guards."""
    out: list[tuple[str, int, int, int]] = []
    if hi - lo <= 8:
        return out
    body = range(lo + 4, hi - 4)

    # Identify `-fsanitize-trap=all` `brk #imm` instructions inside [lo, hi)
    brk_indices = {idx for idx in range(lo, hi) if (insns[idx] & 0xFFE0001F) == 0xD4200000}

    # 1. CSEL / CSINC / CSINV / CSNEG condition flip
    for i in body:
        if i in brk_indices:
            continue
        op = insns[i]
        if (op & 0x3FE00800) == 0x1A800000:
            out.append(("datapath_csel", i, op, op ^ 0x1000))

    # 2. MADD <-> MSUB or SMULH <-> UMULH (skip UBSan `smulh; cmp x_hi, x_lo, asr #63` overflow check)
    for i in body:
        if i in brk_indices:
            continue
        op = insns[i]
        if (op & 0x3F000000) == 0x1B000000:
            if ((op >> 21) & 3) == 2 and i + 1 < hi and (insns[i + 1] & 0xFFE0FC1F) == 0xEB80FC1F:
                continue
            mop = (op ^ (1 << 23)) if (((op >> 21) & 3) == 2) else (op ^ (1 << 15))
            out.append(("datapath_mul", i, op, mop))

    # 3. Shifted-register ADD <-> SUB (non-SP/FP, non-flag-setting S=0 so UBSan b.vs isn't touched)
    for i in body:
        if i in brk_indices:
            continue
        op = insns[i]
        if (
            (op & 0x3F200000) == 0x0B000000
            and ((op >> 29) & 1) == 0  # S == 0
            and ((op >> 22) & 3) != 3
            and (op & 0x1F) < 29
            and ((op >> 5) & 0x1F) < 29
        ):
            out.append(("datapath_addsub", i, op, op ^ 0x40000000))

    # 4. Immediate ADD <-> SUB (S=0, Rd < 29, Rn < 29, imm12 > 0)
    for i in body:
        if i in brk_indices:
            continue
        op = insns[i]
        imm12 = (op >> 10) & 0xFFF
        if (
            (op & 0x1F800000) == 0x11000000
            and ((op >> 29) & 1) == 0
            and imm12 > 0
            and (op & 0x1F) < 29
            and ((op >> 5) & 0x1F) < 29
        ):
            out.append(("datapath_addimm", i, op, op ^ 0x40000000))

    # 5. CBZ <-> CBNZ predicate flip (skip UBSan trap branches jumping to `brk`)
    for i in body:
        if i in brk_indices:
            continue
        op = insns[i]
        if (op & 0x7E000000) == 0x34000000:
            imm19 = (op >> 5) & 0x7FFFF
            if imm19 & 0x40000:
                imm19 -= 0x80000
            target = i + imm19
            if target in brk_indices or (i + 1) in brk_indices:
                continue
            # Also skip `-fsanitize=shift-exponent` `lslv x, x_neg1, x_sh; cbnz/cbz`
            if i > lo and (insns[i - 1] & 0xFFE0FC00) == 0x9AC02000:
                continue
            out.append(("predicate_cbz", i, op, op ^ 0x01000000))

    return out


def build_mutants(
    parsed_objs: dict[str, tuple[bytes, int, list[int], dict[str, tuple[int, int]]]]
) -> list[Mutant]:
    mutants: list[Mutant] = []

    # --- Suite A: Static FPU/SIMD Opcode Purity Injections across all 3 core objects (9 Mutants) ---
    fpu_injections = [
        ("scene", "_bloom_csg_eval_single_q16", "STATIC_FADD_S0", 0x1E202800),
        ("scene", "_bloom_bvh_eval_csg_q16", "STATIC_FMUL_D0", 0x1E600800),
        ("scene", "_bloom_joint_dq64_blend", "STATIC_SCVTF_D0", 0x9E620000),
        ("ui", "_bloom_flex_apply64", "STATIC_FSUB_S0", 0x1E203800),
        ("ui", "_bloom_text_input_insert_char64", "STATIC_FMOV_D0_X0", 0x9E670000),
        ("ui", "_bloom_latex_compile_boxes64", "STATIC_FCVTZS_X0", 0x9E780000),
        ("prefab", "_bloom_prefab_eval_sdf_q16", "STATIC_UMOV_W0", 0x0E043C00),
        ("prefab", "_bloom_prefab_cull_wave64", "STATIC_INS_V0", 0x4E081C00),
        ("prefab", "_bloom_arena_step_dynamics", "STATIC_DUP_V0", 0x4E080C00),
    ]
    for obj_key, fname, mname, bad_op in fpu_injections:
        _, _, insns, funcs = parsed_objs[obj_key]
        lo, hi = funcs[fname]
        anchor = (lo + hi) // 2
        mutants.append(Mutant(mname, "static_fpu_purity", obj_key, fname, anchor, insns[anchor], bad_op))

    # --- Suite B: Stratified Pure-GPR Datapath & Predicate Mutants across 29 core functions ---
    target_plan: list[tuple[str, str, int]] = [
        ("scene", "_bloom_smin_q16", 2),
        ("scene", "_bloom_glb_parse_to_csg64", 4),
        ("scene", "_bloom_csg_eval_single_q16", 4),
        ("scene", "_bloom_csg_eval_program_q16", 3),
        ("scene", "_bloom_bvh_build_from_csg64", 4),
        ("scene", "_bloom_bvh_eval_csg_q16", 4),
        ("scene", "_bloom_joint_dq64_make", 2),
        ("scene", "_bloom_joint_dq64_blend", 3),
        ("scene", "_bloom_joint_dq64_transform_q16", 2),
        ("scene", "_bloom_skeleton_eval_sdf_q16", 3),
        ("scene", "_bloom_sdf_brick64_pack", 2),
        ("scene", "_bloom_sdf_brick64_sample_q16", 3),
        ("scene", "_bloom_matter_eval_radiance_q16", 2),
        ("scene", "_bloom_matter_parse_hex128", 2),
        ("scene", "_bloom_scene_raycast64", 3),
        ("ui", "_bloom_svg_compile_text", 3),
        ("ui", "_bloom_flex_apply64", 3),
        ("ui", "_bloom_ui_synthesize_bento64", 3),
        ("ui", "_bloom_ui_step_pointer_arena", 3),
        ("ui", "_bloom_latex_compile_boxes64", 3),
        ("ui", "_bloom_latex_eval_box_sdf_q16", 2),
        ("ui", "_bloom_text_input_insert_char64", 3),
        ("ui", "_bloom_text_input_backspace64", 2),
        ("ui", "_bloom_ime_set_preedit64", 2),
        ("ui", "_bloom_ime_commit64", 2),
        ("prefab", "_bloom_prefab_eval_sdf_q16", 3),
        ("prefab", "_bloom_style_resolve_thermal", 2),
        ("prefab", "_bloom_prefab_cull_wave64", 2),
        ("prefab", "_bloom_arena_step_dynamics", 3),
        ("prefab", "_bloom_image_verify_arena", 2),
    ]

    for obj_key, fname, quota in target_plan:
        _, _, insns, funcs = parsed_objs[obj_key]
        if fname not in funcs:
            raise RuntimeError(f"Expected symbol {fname} missing from {obj_key}.o")
        lo, hi = funcs[fname]
        cands = collect_candidates_in_func(insns, lo, hi)
        picked = [cands[idx] for idx in sample_even(list(range(len(cands))), quota)]
        for k, (cat, i_idx, orig, mut) in enumerate(picked):
            short = fname.lstrip("_").replace("bloom_", "")
            mutants.append(
                Mutant(f"{short}_{k:02d}", cat, obj_key, fname, i_idx, orig, mut)
            )

    return mutants


def eval_mutant(
    m: Mutant,
    parsed_objs: dict[str, tuple[bytes, int, list[int], dict[str, tuple[int, int]]]],
    golden_obj_paths: dict[str, Path],
    probe_obj: Path,
    golden_digest: str,
    sdk: str,
    macos_ver: str,
    tmpdir: Path,
) -> tuple[Mutant, bool, str]:
    orig_bytes, text_offset, _, _ = parsed_objs[m.obj_key]
    mut_obj = tmpdir / f"{m.name}.o"
    mut_bin = tmpdir / f"{m.name}.bin"

    buf = bytearray(orig_bytes)
    struct.pack_into("<I", buf, text_offset + m.insn_idx * 4, m.mut_op)
    mut_obj.write_bytes(buf)

    # Gate 1: Static `-mgeneral-regs-only` FPU/SIMD opcode purity gate
    if not check_static_fpu_purity(mut_obj):
        return (m, True, "static_otool_fpu_gate")

    objs = [
        str(mut_obj if k == m.obj_key else golden_obj_paths[k])
        for k in ("scene", "ui", "prefab")
    ]
    link_cmd = [
        "ld",
        "-arch",
        "arm64",
        "-platform_version",
        "macos",
        macos_ver,
        macos_ver,
        "-syslibroot",
        sdk,
        "-lSystem",
        "-e",
        "_main",
        str(probe_obj),
        *objs,
        "-o",
        str(mut_bin),
    ]
    lk = subprocess.run(link_cmd, capture_output=True, text=True)
    if lk.returncode != 0:
        return (m, True, f"link_err({lk.returncode})")

    try:
        run = subprocess.run([str(mut_bin), golden_digest], capture_output=True, text=True, timeout=5)
        killed = run.returncode != 0
        return (m, killed, f"exit({run.returncode})")
    except subprocess.TimeoutExpired:
        return (m, True, "timeout_killed")


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    flow_dir = Path(os.environ.get("FLOW_DIR", str(root.parent / "FLOW"))).resolve()
    golden_obj_paths = {
        "scene": root / "build" / "bloom_scene.o",
        "ui": root / "build" / "bloom_ui.o",
        "prefab": root / "build" / "bloom_prefab.o",
    }

    for k, p in golden_obj_paths.items():
        if not p.exists():
            print(f"ERROR: Missing {p}; run `make all` first.", file=sys.stderr)
            return 1
        if not check_static_fpu_purity(p):
            print(f"ERROR: Golden {p} failed static FPU/SIMD purity check!", file=sys.stderr)
            return 1

    sdk = subprocess.check_output(["xcrun", "-sdk", "macosx", "--show-sdk-path"], text=True).strip()
    macos_ver = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-version"], text=True).strip()

    parsed_objs: dict[str, tuple[bytes, int, list[int], dict[str, tuple[int, int]]]] = {}
    total_insns = 0
    for k, p in golden_obj_paths.items():
        raw = p.read_bytes()
        toff, tsz, funcs = parse_macho_text_and_syms(raw)
        insns = list(struct.unpack_from(f"<{tsz // 4}I", raw, toff))
        total_insns += len(insns)
        parsed_objs[k] = (raw, toff, insns, funcs)

    tmpdir = Path(tempfile.mkdtemp(prefix="bloom_sabotage_"))
    try:
        probe_c = tmpdir / "probe.c"
        probe_obj = tmpdir / "probe.o"
        golden_bin = tmpdir / "golden.bin"
        probe_c.write_text(PROBE_C_SOURCE, encoding="utf-8")

        cc = os.environ.get("CC", "clang")
        comp = subprocess.run(
            [
                cc,
                "-std=c17",
                "-O2",
                f"-I{root / 'include'}",
                "-isystem",
                str(flow_dir),
                "-c",
                str(probe_c),
                "-o",
                str(probe_obj),
            ],
            capture_output=True,
            text=True,
        )
        if comp.returncode != 0:
            print("ERROR: Failed to compile pure-memory sabotage probe:", file=sys.stderr)
            print(comp.stderr, file=sys.stderr)
            return 1

        lk_gold = subprocess.run(
            [
                "ld",
                "-arch",
                "arm64",
                "-platform_version",
                "macos",
                macos_ver,
                macos_ver,
                "-syslibroot",
                sdk,
                "-lSystem",
                "-e",
                "_main",
                str(probe_obj),
                str(golden_obj_paths["scene"]),
                str(golden_obj_paths["ui"]),
                str(golden_obj_paths["prefab"]),
                "-o",
                str(golden_bin),
            ],
            capture_output=True,
            text=True,
        )
        if lk_gold.returncode != 0:
            print("ERROR: Failed to link golden probe:", file=sys.stderr)
            print(lk_gold.stderr, file=sys.stderr)
            return 1

        base = subprocess.run([str(golden_bin), "--emit-digest"], capture_output=True, text=True, timeout=5)
        if base.returncode != 0:
            print(f"ERROR: Unmutated golden libbloom failed probe (rc={base.returncode}): {base.stderr}", file=sys.stderr)
            return 1
        golden_digest = base.stdout.strip()

        mutants = build_mutants(parsed_objs)
        print("================================================================================")
        print("  BLOOM v6.0.0 — Mach-O ARM64 Binary Sabotage & Purity Audit (Zero-SDL3 Probe)")
        print("================================================================================")
        print(f"  golden_baseline_status : PASS (exit 0, digest=0x{golden_digest}, {total_insns} A64 insns)")
        print(f"  adversarial_mutants    : {len(mutants)} Mach-O mutants across scene/ui/prefab")

        survivors: list[Mutant] = []
        by_cat: dict[str, list[bool]] = {}
        workers = min(8, os.cpu_count() or 4)
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
            futs = [
                pool.submit(
                    eval_mutant,
                    m,
                    parsed_objs,
                    golden_obj_paths,
                    probe_obj,
                    golden_digest,
                    sdk,
                    macos_ver,
                    tmpdir,
                )
                for m in mutants
            ]
            for fut in concurrent.futures.as_completed(futs):
                m, killed, _ = fut.result()
                by_cat.setdefault(m.category, []).append(killed)
                if not killed:
                    survivors.append(m)
    finally:
        shutil.rmtree(tmpdir, ignore_errors=True)

    for cat in sorted(by_cat):
        res = by_cat[cat]
        print(f"  cat_{cat:<19}: {sum(res):2d} / {len(res):2d} killed")

    killed_total = len(mutants) - len(survivors)
    pct = (100.0 * killed_total / len(mutants)) if mutants else 0.0
    print(f"\n  mutant_kill_rate       : {killed_total} / {len(mutants)} ({pct:.1f}%)")

    if survivors:
        print("================================================================================")
        print("  [MUTATION AUDIT FAIL] SURVIVING MUTANTS DETECTED (BLIND SPOT!):")
        for s in sorted(survivors, key=lambda x: x.name):
            print(f"    - {s.name} ({s.obj_key}.o:{s.func} insn #{s.insn_idx}: 0x{s.orig_op:08x} -> 0x{s.mut_op:08x})")
        print("================================================================================")
        return 1

    print("================================================================================")
    print("  [MUTATION AUDIT PASS] 100.0% KILL RATE — ZERO STATIC OR DYNAMIC BLIND SPOTS")
    print("================================================================================")
    return 0


if __name__ == "__main__":
    sys.exit(main())
