#!/usr/bin/env python3
"""
sabotage_check.py — Mach-O ARM64 Binary Sabotage & Purity Gate for libbloom.a
=============================================================================
Proves static `-mgeneral-regs-only` FPU/SIMD purity and dynamic observational
completeness across `build/bloom_scene.o`, `build/bloom_ui.o`, and
`build/bloom_prefab.o` with ZERO SDL3 / GPU startup overhead (~5ms/mutant):

  - Suite A (12 Static ISA Purity Mutants): Injects forbidden A64 FPU/SIMD/NEON
    opcodes (`fadd`, `fsub`, `fmul`, `scvtf`, `fcvtzs`, `fmov`, `umov`, `ins`,
    `dup`) and ROP-gadget `blr` indirect calls across all 3 core Mach-O objects
    and verifies the `otool -tv` zero-float / zero-SIMD / zero-BLR gate catches
    100% of them.
  - Suite B (Stratified Pure-GPR Datapath & Predicate Mutants across 61 core
    functions including `.stl`/`.glb` mesh parsing, Morton BVH, Jordan parity,
    2-Jet differential geometry, arbitrary 3D cross-sections, swept-CCD
    piecewise rebound, coupled thermal physics, modal physical-modeling DSP,
    concentric SDF gameplay shells, 16KB spacetime time-rewind, relativistic
    metric/Doppler scale, thin-lens CineCamera, and moodboard SSOT compiler):
    mutates A64 machine instructions (`CSEL/CSINC/CSINV/CSNEG` condition flips,
    `MADD<->MSUB` / `SMULH<->UMULH` multiplier flips, `ADD<->SUB` arithmetic
    flips, and `CBZ<->CBNZ` branch flips) across `libbloom.a`, relinking ONLY
    the pure CPU `.o` files in parallel temp dirs and asserting 100.0% kill rate.
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
    r"^(fadd|fsub|fmul|fdiv|fsqrt|fmadd|fmsub|fcmp|fccmp|fcsel|scvtf|ucvtf|fcvtzs|fcvtzu|fmov|umov|smov|ins|dup|mov\.[bhsdq]|blr)$"
)

PROBE_C_SOURCE = r"""
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bloom.h"
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

        /* Non-dyadic small finite floats (-24 <= shift <= -1) to cover rounding-bit carry paths */
        uint8_t glb_round[80];
        memcpy(glb_round, glb, 80U);
        const uint32_t rnd_verts[12] = {
            0xBF000080U, 0xBF000040U, 0xBEAAAAABU,
            0x3F000080U, 0x3F000040U, 0x3EAAAAABU,
            0x00000000U, 0x00000000U, 0x00000000U,
            0x3A83126FU, 0xBA83126FU, 0x3F000080U
        };
        for (size_t k = 0U; k < 12U; ++k) put_u32_le(glb_round + 32U + k * 4U, rnd_verts[k]);
        CHECK(bloom_glb_parse_to_csg64(glb_round, 80U, 6U, 1U, &op, &vcnt) && vcnt == 4U);
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

        static const uint16_t align_modes[3] = { BLOOM_ALIGN_START, BLOOM_ALIGN_END, BLOOM_ALIGN_CENTER };
        static const uint16_t just_modes[3]  = { BLOOM_JUSTIFY_CENTER, BLOOM_JUSTIFY_END, BLOOM_JUSTIFY_START };
        static const uint16_t dir_modes[3]   = { BLOOM_FLEX_COLUMN, BLOOM_FLEX_COLUMN, BLOOM_FLEX_ROW };
        for (uint32_t am = 0U; am < 3U; ++am) {
            memset(fnodes, 0, sizeof(fnodes));
            for (uint32_t i = 0U; i < 4U; ++i) {
                fnodes[i].half_w_q16 = 4096 + (int32_t)(i * 512U);
                fnodes[i].half_h_q16 = 3072 + (int32_t)(i * 256U);
            }
            bloom_flex_init64(&flex, dir_modes[am], just_modes[am], align_modes[am],
                              -16384, 8192, 24576, 32768, 2048, 1536);
            bloom_flex_apply64(&flex, fnodes, 4U, 4096);
            mix_bytes(&flex, sizeof(flex));
            mix_bytes(fnodes, sizeof(fnodes));
        }

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
            "<rect data-node-id=\"1\" data-anchor=\"12\" data-prim=\"4\" data-glass=\" 2\" data-bind=\"0\" data-action=\"1\" data-init-q16=\" 49152\" x=\"  40.5\" y=\"40.25\" width=\"240\" height=\"60\" rx=\"12.5\" fill=\"#20D8FF\" fill-opacity=\"0.75\"/>"
            "<circle data-node-id=\"2\" data-prim=\"2\" data-bind=\"1\" cx=\"900\" cy=\"120\" r=\"42\" fill=\"#FFB020\" stroke-width=\"6\"/>"
            "<rect data-node-id=\"3\" data-prim=\"1\" data-action=\"2\" x=\"-10\" y=\"200\" width=\"120\" height=\"40\" rx=\"8\" fill=\"#3af\"/>"
            "<path data-glyph-id=\"1\" d=\"M 100 100 L 220 180 C 240 190 260 150 300 120 Z\" stroke=\"#FFFFFF\" stroke-width=\"3\"/>"
            "<line data-glyph-id=\"2\" x1=\"-20.5\" y1=\"680\" x2=\"400\" y2=\"680\" stroke=\"#22C88A\" stroke-width=\"2.5\"/>"
            "</svg>";
        size_t sn = bloom_svg_compile_text(svg, fnodes, 4U, glyphs, 16U, &gcnt, &dom);
        CHECK(sn == 3U && gcnt >= 2U);
        mix_bytes(fnodes, 3U * sizeof(BloomUiNode64));
        mix_bytes(glyphs, gcnt * sizeof(BloomGlyph64));

        const char *svg2 =
            "<svg width=\"640\" height=\"360\">"
            "<rect data-node-id=\"11\" data-anchor=\"12\" data-bind=\"10\" data-action=\"13\" data-glass=\"10\" data-init-q16=\"12345\" x=\"32\" y=\"48\" width=\"160\" height=\"64\" rx=\"10\" fill=\"#11AAFF\"/>"
            "<path data-glyph-id=\"3\" d=\"M 10 20 H 120 V 90 L 40 60 Z\" stroke=\"#FF00AA\" stroke-width=\"12\"/>"
            "</svg>";
        size_t gcnt2 = 0U;
        size_t sn2 = bloom_svg_compile_text(svg2, fnodes, 4U, glyphs, 16U, &gcnt2, &dom);
        CHECK(sn2 == 1U && gcnt2 == 1U && fnodes[0].bind_channel == 10U);
        mix_bytes(fnodes, sizeof(BloomUiNode64));
        mix_bytes(glyphs, sizeof(BloomGlyph64));
        mix_bytes(&dom, sizeof(dom));

        BloomLatexBox64 lboxes[16];
        int32_t lw = 0;
        size_t ln = bloom_latex_compile_boxes64("\\\\int \\\\sum \\\\frac{\\\\sqrt{\\\\eta^2 + x_1}}{2\\\\pi}",
                                                -16384, -32768, 4096, 0xFFD848U, lboxes, 16U, &lw);
        CHECK(ln >= 6U);
        mix_u64((uint64_t)(uint32_t)lw);
        for (size_t i = 0U; i < ln; ++i) {
            mix_bytes(&lboxes[i], sizeof(BloomLatexBox64));
            mix_u64((uint64_t)(uint32_t)bloom_latex_eval_box_sdf_q16(&lboxes[i], lboxes[i].x_q16 + 512, lboxes[i].y_q16 - 512));
        }
        BloomLatexBox64 lb_syn;
        memset(&lb_syn, 0, sizeof(lb_syn));
        lb_syn.box_kind = BLOOM_LATEX_SQRT_RAD;
        lb_syn.half_w_q16 = 4096;
        lb_syn.half_h_q16 = 2048;
        lb_syn.stroke_q16 = 128;
        mix_u64((uint64_t)(uint32_t)bloom_latex_eval_box_sdf_q16(&lb_syn, -3072, 1024));
        mix_u64((uint64_t)(uint32_t)bloom_latex_eval_box_sdf_q16(&lb_syn, -1024, 0));
        mix_u64((uint64_t)(uint32_t)bloom_latex_eval_box_sdf_q16(&lb_syn, 2048, -2048));
        lb_syn.box_kind = BLOOM_LATEX_FRAC_BAR;
        mix_u64((uint64_t)(uint32_t)bloom_latex_eval_box_sdf_q16(&lb_syn, 2048, 256));
        lb_syn.half_w_q16 = 64;
        lb_syn.half_h_q16 = 64;
        mix_u64((uint64_t)(uint32_t)bloom_latex_eval_box_sdf_q16(&lb_syn, 128, 64));
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
        {
            uint64_t saved_ch[8];
            uint32_t ch_slot = (uint32_t)((arena.map.flow_states[BLOOM_FLOW_CH_UI_NODE0][5] >> 16U) & BLOOM_FLOW_CHART_MASK);
            memcpy(saved_ch, arena.map.flow_charts[ch_slot], sizeof(saved_ch));
            arena.map.flow_charts[ch_slot][0] = (uint64_t)(int64_t)-0x7FFFFFFFLL;
            arena.map.flow_charts[ch_slot][1] = (uint64_t)0x7FFFFFFFLL;
            arena.map.flow_charts[ch_slot][2] = (uint64_t)0x7FFFFFFFLL;
            arena.map.flow_charts[ch_slot][4] = (0x7FFFFFFFULL << 32U) | 0x00010000ULL;
            arena.map.flow_states[BLOOM_FLOW_CH_UI_NODE0][3] = (uint64_t)(int64_t)-0x5D3A9B71LL;
            (void)bloom_ui_step_pointer_arena(&arena, 0, 0, 0, false, false);
            mix_bytes(arena.map.flow_states[BLOOM_FLOW_CH_UI_NODE0], sizeof(arena.map.flow_states[0]));
            uint64_t next_hi = ((uint64_t)ch_slot ^ 1ULL) & (uint64_t)BLOOM_FLOW_CHART_MASK;
            uint64_t next_lo = ((uint64_t)ch_slot ^ 2ULL) & (uint64_t)BLOOM_FLOW_CHART_MASK;
            memcpy(arena.map.flow_charts[ch_slot], saved_ch, sizeof(saved_ch));
            arena.map.flow_charts[ch_slot][3] =
                ((uint64_t)(uint32_t)(int32_t)(-65536) << 32U) | (uint64_t)(uint32_t)(int32_t)(32768);
            arena.map.flow_charts[ch_slot][5] = (next_lo << 24U) | (next_hi << 16U);
            arena.map.flow_states[BLOOM_FLOW_CH_UI_NODE0][3] = (uint64_t)65536ULL;
            arena.map.flow_states[BLOOM_FLOW_CH_UI_NODE0][5] = ((uint64_t)ch_slot << 16U);
            (void)bloom_ui_step_pointer_arena(&arena, 0, 0, 0, false, false);
            CHECK(((arena.map.flow_states[BLOOM_FLOW_CH_UI_NODE0][5] >> 16U) & BLOOM_FLOW_CHART_MASK) == next_hi);
            mix_bytes(arena.map.flow_states[BLOOM_FLOW_CH_UI_NODE0], sizeof(arena.map.flow_states[0]));
            arena.map.flow_states[BLOOM_FLOW_CH_UI_NODE0][3] = 0ULL;
            arena.map.flow_states[BLOOM_FLOW_CH_UI_NODE0][5] = ((uint64_t)ch_slot << 16U);
            (void)bloom_ui_step_pointer_arena(&arena, 0, 0, 0, false, false);
            mix_bytes(arena.map.flow_states[BLOOM_FLOW_CH_UI_NODE0], sizeof(arena.map.flow_states[0]));
            memcpy(arena.map.flow_charts[ch_slot], saved_ch, sizeof(saved_ch));
            arena.map.flow_states[BLOOM_FLOW_CH_UI_NODE0][3] = (uint64_t)BLOOM_Q16_ONE;
            arena.map.flow_states[BLOOM_FLOW_CH_UI_NODE0][5] = ((uint64_t)ch_slot << 16U);
        }
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

        /* --- FLOW v7.0.1 SEARU Symbiotic Co-Solver & Zero-BLR Inline Solver --- */
        {
            BloomUiNode64 gnodes[16];
            memset(gnodes, 0, sizeof(gnodes));
            uint32_t unsat = 77U;
            CHECK(!bloom_ui_solve_glass_tracks64(NULL, 8U, NULL, 0U, 0ULL, 1ULL, 64U, &unsat) && unsat == 0U);
            CHECK(!bloom_ui_solve_glass_tracks64(gnodes, 0U, NULL, 0U, 0ULL, 1ULL, 64U, &unsat) && unsat == 0U);
            CHECK(bloom_ui_solve_glass_tracks64(gnodes, 8U, NULL, 0U, 0ULL, 1ULL, 64U, &unsat) && unsat == 0U);

            for (size_t i = 0U; i < 8U; ++i) {
                gnodes[i].style_and_state = BLOOM_UI_STATE_GLASS_BIT | BLOOM_GLASS_IRIDESCENT;
            }
            const uint16_t ui_edges[15] = {
                SEARU_EDGE_U16(0, 1), SEARU_EDGE_U16(0, 2), SEARU_EDGE_U16(0, 3),
                SEARU_EDGE_U16(1, 2), SEARU_EDGE_U16(2, 3), SEARU_EDGE_U16(3, 4),
                SEARU_EDGE_U16(4, 5), SEARU_EDGE_U16(5, 6), SEARU_EDGE_U16(6, 7),
                SEARU_EDGE_U16(7, 1), SEARU_EDGE_U16(1, 4), SEARU_EDGE_U16(2, 5),
                SEARU_EDGE_U16(3, 6), SEARU_EDGE_U16(2, 2), SEARU_EDGE_U16(14, 15)
            };
            CHECK(bloom_ui_solve_glass_tracks64(gnodes, 8U, ui_edges, 15U, UINT64_C(0x3),
                                                UINT64_C(0x7000CAFE2026), 1024U, &unsat));
            CHECK(unsat == 0U && (gnodes[0].style_and_state & 0x3U) == BLOOM_GLASS_IRIDESCENT);
            for (size_t i = 0U; i < 8U; ++i) {
                mix_u64(gnodes[i].style_and_state);
            }
            /* Uncolorable K_5 clique with 4 glass modes -> unsat >= 1, returns false */
            const uint16_t k5_edges[10] = {
                SEARU_EDGE_U16(0, 1), SEARU_EDGE_U16(0, 2), SEARU_EDGE_U16(0, 3), SEARU_EDGE_U16(0, 4),
                SEARU_EDGE_U16(1, 2), SEARU_EDGE_U16(1, 3), SEARU_EDGE_U16(1, 4),
                SEARU_EDGE_U16(2, 3), SEARU_EDGE_U16(2, 4), SEARU_EDGE_U16(3, 4)
            };
            CHECK(!bloom_ui_solve_glass_tracks64(gnodes, 20U, k5_edges, 10U, UINT64_C(0xF),
                                                 UINT64_C(0x12345678), 32U, &unsat) && unsat >= 1U);
            mix_u64(unsat);
            /* Odd node_count = 5U exercises scalar remainder tail of vectorized write-back (+ sentinel guard on 5..15) */
            for (size_t i = 5U; i < 16U; ++i) {
                gnodes[i].style_and_state = 0x77777777U;
            }
            CHECK(!bloom_ui_solve_glass_tracks64(gnodes, 5U, k5_edges, 10U, UINT64_C(0x3),
                                                 UINT64_C(0x9ABCDEF0), 32U, &unsat) && unsat >= 1U);
            for (size_t i = 0U; i < 16U; ++i) {
                mix_u64(gnodes[i].style_and_state);
            }

            const BloomBilevelTerm terms[4] = {
                { -20LL * FLOW_Q16_ONE, 0LL,          0LL, UINT64_C(50000) },
                { -6LL * FLOW_Q16_ONE,  FLOW_Q16_ONE, 0LL, UINT64_C(0)     },
                { -6LL * FLOW_Q16_ONE,  0LL,          0LL, UINT64_C(0)     },
                { +8LL * FLOW_Q16_ONE,  0LL,          0LL, UINT64_C(8000)  }
            };
            uint64_t hseed = UINT64_C(0x5EA40700BEEF);
            uint32_t hepoch = 12U;
            CHECK(!bloom_arena_heal_and_swap(NULL, 0U, terms, 4U, 0ULL, 0LL, 0LL, 6U, &hseed, &hepoch, 128U));
            CHECK(!bloom_arena_heal_and_swap(&arena, 99U, terms, 4U, 0ULL, 0LL, 0LL, 6U, &hseed, &hepoch, 128U));
            CHECK(!bloom_arena_heal_and_swap(&arena, BLOOM_FLOW_CH_SCROLL, NULL, 4U, 0ULL, 0LL, 0LL, 6U, &hseed, &hepoch, 128U));
            CHECK(!bloom_arena_heal_and_swap(&arena, BLOOM_FLOW_CH_SCROLL, terms, 0U, 0ULL, 0LL, 0LL, 6U, &hseed, &hepoch, 128U));
            /* Converged channel -> false */
            CHECK(!bloom_arena_heal_and_swap(&arena, BLOOM_FLOW_CH_SCROLL, terms, 4U, UINT64_C(0x1),
                                             0LL, 0LL, 6U, &hseed, &hepoch, 256U));

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
            sst[3] = (uint64_t)(3LL * FLOW_Q16_ONE);
            flow_step_atlas(sst, sst, (const flow_cell *)arena.map.flow_charts, BLOOM_FLOW_CHART_MASK, 0LL, 0U);
            /* Moving (dt != 0) -> false */
            CHECK(!bloom_arena_heal_and_swap(&arena, BLOOM_FLOW_CH_SCROLL, terms, 4U, UINT64_C(0x1),
                                             0LL, 0LL, 6U, &hseed, &hepoch, 256U));
            for (int q = 0; q < 8; ++q) {
                flow_step_atlas(sst, sst, (const flow_cell *)arena.map.flow_charts, BLOOM_FLOW_CHART_MASK, 0LL, 0U);
            }
            /* Trapped at barrier (!CONVERGED && dt == 0): first test >64 clamp (UBSan bounds guard), then 4-var heal */
            BloomBilevelTerm terms70[70];
            memset(terms70, 0, sizeof(terms70));
            CHECK(!bloom_arena_heal_and_swap(&arena, BLOOM_FLOW_CH_SCROLL, terms70, 70U, UINT64_C(0x1),
                                             0LL, 0LL, 4U, &hseed, &hepoch, 8U));
            CHECK(bloom_arena_heal_and_swap(&arena, BLOOM_FLOW_CH_SCROLL, terms, 4U, UINT64_C(0x1),
                                            0LL, 0LL, 8U, &hseed, &hepoch, 512U));
            CHECK(hepoch == 13U && (int64_t)arena.map.flow_charts[6][0] == 0LL);
            mix_bytes(arena.map.flow_charts[6], sizeof(arena.map.flow_charts[6]));
            mix_u64(hseed ^ (uint64_t)hepoch);

            /* Deep cooling + coupled 2-XOR Black-Swan Epiphany reheat trajectory (iters > 1200 -> temp_q12 < init_temp/4) */
            BloomBilevelTerm deep_terms[64];
            for (uint32_t ti = 0U; ti < 64U; ++ti) {
                if (ti == 0U) {
                    deep_terms[ti] = (BloomBilevelTerm){ 0LL, 0LL, 0LL, UINT64_C(0) };
                } else if (ti == 1U) {
                    deep_terms[ti] = (BloomBilevelTerm){ -14LL * FLOW_Q16_ONE, 0LL, 0LL, UINT64_C(40000) };
                } else if ((ti & 1U) == 0U) {
                    deep_terms[ti] = (BloomBilevelTerm){ +30LL * FLOW_Q16_ONE, (int64_t)(ti * 16U), 0LL, UINT64_C(100) + ti * 7U };
                } else {
                    deep_terms[ti] = (BloomBilevelTerm){ -30LL * FLOW_Q16_ONE, -(int64_t)((ti - 1U) * 16U), 0LL, UINT64_C(100) + ti * 7U };
                }
            }
            for (uint32_t k = 1U; k < 16U; ++k) {
                deep_terms[2U * k].db0_q16 = +20LL * FLOW_Q16_ONE;
                deep_terms[2U * k + 1U].db0_q16 = -34LL * FLOW_Q16_ONE;
                deep_terms[2U * k].weight_cost = UINT64_C(200) + k * 50U;
                deep_terms[2U * k + 1U].weight_cost = UINT64_C(200) + k * 50U;
            }
            flow_arena_publish_bound((flow_cell *)arena.map.flow_charts, 6U, BLOOM_FLOW_CHART_MASK, 20U, bchart);
            sst[3] = (uint64_t)(2LL * FLOW_Q16_ONE);
            sst[4] = 0ULL;
            sst[6] = 0ULL;
            uint64_t dseed = UINT64_C(0x1000000000000001) + UINT64_C(5) * UINT64_C(0x9E3779B97F4A7C15);
            uint32_t depoch = 30U;
            CHECK(bloom_arena_heal_and_swap(&arena, BLOOM_FLOW_CH_SCROLL, deep_terms, 64U, UINT64_C(0x1),
                                            0LL, 12345LL, 1U, &dseed, &depoch, 2600U));
            mix_bytes(arena.map.flow_charts[6], sizeof(arena.map.flow_charts[6]));
            mix_u64(dseed ^ (uint64_t)depoch);
        }
    }

    /* --- 5. v7.1.0 Rotations, 3D Sections, STL/GLB Mesh BVH, Brick World, Swept-CCD & Thermal Physics --- */
    {
        /* 5a. Inverse Euler Rotation (zero fast-path, Z-only active path, angle wrap > PI) */
        const int32_t pt_in[3] = { 49152, -32768, 24576 };
        const int32_t rot_z[3] = { 0, 0, 0 };
        const int32_t rot_only_z[3] = { 0, 0, 32768 };
        const int32_t rot_wrap[3] = { 160000, -160000, 180000 };
        const int32_t rot_3[3] = { 34078, -51472, 22937 };
        int32_t pt_id[3] = { 0 }, pt_rz[3] = { 0 }, pt_w[3] = { 0 }, pt_r[3] = { 0 };
        bloom_rot_inv_euler_q16(pt_in, rot_z, pt_id);
        CHECK(pt_id[0] == pt_in[0] && pt_id[1] == pt_in[1] && pt_id[2] == pt_in[2]);
        bloom_rot_inv_euler_q16(pt_in, rot_only_z, pt_rz);
        CHECK(pt_rz[0] != pt_in[0]);
        bloom_rot_inv_euler_q16(pt_in, rot_wrap, pt_w);
        mix_bytes(pt_rz, sizeof(pt_rz));
        mix_bytes(pt_w, sizeof(pt_w));
        bloom_rot_inv_euler_q16(pt_in, rot_3, pt_r);
        mix_bytes(pt_r, sizeof(pt_r));

        /* 5b. Arbitrary 3D Cross-Section Plane / Slab / Corner */
        static BloomEngineArena sec_arena;
        bloom_arena_init_default(&sec_arena, 320U, 180U);
        const int32_t diag_n[3] = { 46341, 46341, 0 };
        const int32_t zero_n[3] = { 0, 0, 0 };
        BloomSectionSpec64 sec;
        memset(&sec, 0, sizeof(sec));
        bloom_section_init64(&sec, BLOOM_SEC_MODE_PLANE, diag_n, 16384, 8192, 11796, 9830, 0xFU);
        mix_bytes(&sec, sizeof(sec));
        bloom_section_init64(&sec, BLOOM_SEC_MODE_PLANE, zero_n, 0, 8192, 11796, 9830, 0xFU);
        CHECK(sec.plane_norm_q16[0] == BLOOM_Q16_ONE);
        bool cut_f = false;
        int64_t dp1 = bloom_section_eval_clip_q16(-8192, -32768, 0, 0, &sec_arena.map.frame_hdr, &sec, &cut_f);
        CHECK(cut_f && dp1 > 0);
        mix_u64((uint64_t)dp1);
        sec.section_mode = BLOOM_SEC_MODE_SLAB;
        int64_t ds_out = bloom_section_eval_clip_q16(-8192, 49152, 0, 0, &sec_arena.map.frame_hdr, &sec, &cut_f);
        int64_t ds_in  = bloom_section_eval_clip_q16(-8192, 4096, 0, 0, &sec_arena.map.frame_hdr, &sec, &cut_f);
        CHECK(ds_out > 0 && ds_in < 0);
        mix_u64((uint64_t)ds_out ^ (uint64_t)ds_in);
        sec.section_mode = BLOOM_SEC_MODE_CORNER;
        int64_t dc1 = bloom_section_eval_clip_q16(-8192, -32768, 0, -32768, &sec_arena.map.frame_hdr, &sec, &cut_f);
        CHECK(cut_f && dc1 > 0);
        mix_u64((uint64_t)dc1);
        sec_arena.map.frame_hdr.flags |= BLOOM_FLAG_XRAY_ENABLED;
        int64_t dx_fb = bloom_section_eval_clip_q16(-8192, -16384, 0, 0, &sec_arena.map.frame_hdr, NULL, &cut_f);
        CHECK(cut_f && dx_fb == 16384);
        mix_u64((uint64_t)dx_fb);

        /* 5c. Triangle UD, Degenerate Reject, STL & Multi-Tri GLB Ingestion & Jordan Parity */
        BloomTri64 tr;
        const int32_t tv0[3] = { 0, 0, 0 }, tv1[3] = { 65536, 0, 0 }, tv2[3] = { 0, 65536, 0 };
        CHECK(!bloom_tri64_init(&tr, 1U, 0U, 0U, tv0, tv0, tv2)); /* zero-area degenerate */
        CHECK(bloom_tri64_init(&tr, 1U, 2U, 256U, tv0, tv1, tv2));
        mix_bytes(&tr, sizeof(tr));
        /* Face prism interior (z=+0.5) & all 3 exterior edge Voronoi regions + huge coordinate shift */
        mix_u64((uint64_t)bloom_tri64_eval_ud_q16(&tr, 16384, 16384, 32768));
        int64_t ud_edge0 = bloom_tri64_eval_ud_q16(&tr, -16384, 16384, 0);
        CHECK(ud_edge0 > 15000);
        mix_u64((uint64_t)ud_edge0);
        mix_u64((uint64_t)bloom_tri64_eval_ud_q16(&tr, 80000, -16384, 0));
        mix_u64((uint64_t)bloom_tri64_eval_ud_q16(&tr, 49152, 49152, 8192));
        mix_u64((uint64_t)bloom_tri64_eval_ud_q16(&tr, 0x1000000LL, -0x1000000LL, 0x800000LL));

        static const int8_t c_tris[12][3][3] = {
            {{-1,-1,-1}, { 1, 1,-1}, { 1,-1,-1}}, {{-1,-1,-1}, {-1, 1,-1}, { 1, 1,-1}},
            {{-1,-1, 1}, { 1,-1, 1}, { 1, 1, 1}}, {{-1,-1, 1}, { 1, 1, 1}, {-1, 1, 1}},
            {{-1,-1,-1}, {-1,-1, 1}, {-1, 1, 1}}, {{-1,-1,-1}, {-1, 1, 1}, {-1, 1,-1}},
            {{ 1,-1,-1}, { 1, 1, 1}, { 1,-1, 1}}, {{ 1,-1,-1}, { 1, 1,-1}, { 1, 1, 1}},
            {{-1,-1,-1}, { 1,-1, 1}, {-1,-1, 1}}, {{-1,-1,-1}, { 1,-1,-1}, { 1,-1, 1}},
            {{-1, 1,-1}, {-1, 1, 1}, { 1, 1, 1}}, {{-1, 1,-1}, { 1, 1, 1}, { 1, 1,-1}}
        };
        uint8_t stl[84 + 12 * 50];
        memset(stl, 0, sizeof(stl));
        put_u32_le(stl + 80U, 12U);
        for (uint32_t t = 0U; t < 12U; ++t) {
            uint8_t *rec = stl + 84U + t * 50U;
            for (uint32_t v = 0U; v < 3U; ++v) {
                for (uint32_t a = 0U; a < 3U; ++a) {
                    put_u32_le(rec + 12U + v * 12U + a * 4U, (c_tris[t][v][a] < 0) ? 0xBF000000U : 0x3F000000U);
                }
            }
        }
        static BloomMeshArena mesh_a;
        uint32_t tcnt = 0U;
        CHECK(bloom_stl_parse_to_mesh64(stl, sizeof(stl), BLOOM_MATTER_TITANIUM, false, &mesh_a, &tcnt) && tcnt == 12U);
        CHECK(!bloom_stl_parse_to_mesh64(stl, sizeof(stl) - 4U, 0U, false, &mesh_a, &tcnt));
        mix_bytes(&mesh_a.map.tris[0], 12U * sizeof(BloomTri64));

        /* Normalized parse + Jordan parity SDF vs brute-force */
        CHECK(bloom_stl_parse_to_mesh64(stl, sizeof(stl), BLOOM_MATTER_TITANIUM, true, &mesh_a, &tcnt) && tcnt == 12U);
        uint32_t mbvh = (uint32_t)bloom_bvh_build_from_tris64(mesh_a.map.tris, tcnt, mesh_a.map.bvh_nodes, BLOOM_MAX_MESH_BVH_NODES);
        CHECK(mbvh > 0U);
        uint32_t mvis = 0U;
        int64_t sd_m_in  = bloom_mesh_eval_sdf_q16(&mesh_a, tcnt, mbvh, true, 0, 0, 0, &mvis);
        int64_t sd_m_out = bloom_mesh_eval_sdf_q16(&mesh_a, tcnt, mbvh, true, 3 * BLOOM_Q16_ONE, 0, 0, &mvis);
        int64_t sd_m_uns = bloom_mesh_eval_sdf_q16(&mesh_a, tcnt, mbvh, false, 0, 0, 0, &mvis);
        int64_t sd_m_sub =
            bloom_mesh_eval_sdf_q16(&mesh_a, 1U, mbvh, false,  98304, 0, 0, &mvis) +
            bloom_mesh_eval_sdf_q16(&mesh_a, 1U, mbvh, false, -98304, 0, 0, &mvis) +
            bloom_mesh_eval_sdf_q16(&mesh_a, 1U, mbvh, false, 0,  98304, 0, &mvis) +
            bloom_mesh_eval_sdf_q16(&mesh_a, 1U, mbvh, false, 0, -98304, 0, &mvis) +
            bloom_mesh_eval_sdf_q16(&mesh_a, 1U, mbvh, false, 0, 0,  98304, &mvis) +
            bloom_mesh_eval_sdf_q16(&mesh_a, 1U, mbvh, false, 0, 0, -98304, &mvis);
        CHECK(sd_m_in < 0 && sd_m_out > 0 && sd_m_uns > 0 && sd_m_sub > 500000);
        mix_u64((uint64_t)sd_m_in ^ (uint64_t)sd_m_out ^ (uint64_t)sd_m_uns ^ (uint64_t)sd_m_sub ^ ((uint64_t)mvis << 32U));

        /* Descending-Morton BVH build + IEEE-754 saturation clamp in STL & GLB */
        static BloomMeshArena mesh_big;
        BloomTri64 rev_tris[4];
        for (uint32_t rt = 0U; rt < 4U; ++rt) {
            int32_t c = (3 - (int32_t)rt * 2) * BLOOM_Q16_ONE;
            const int32_t rv0[3] = { c, c, c }, rv1[3] = { c + 32768, c, c }, rv2[3] = { c, c + 32768, c };
            CHECK(bloom_tri64_init(&rev_tris[rt], rt + 1U, 1U, 0U, rv0, rv1, rv2));
        }
        size_t rbvh = bloom_bvh_build_from_tris64(rev_tris, 4U, mesh_big.map.bvh_nodes, BLOOM_MAX_MESH_BVH_NODES);
        CHECK(rbvh == 7U);
        mix_bytes(mesh_big.map.bvh_nodes, 7U * sizeof(BloomBvhNode64));

        uint8_t stl_big[84 + 50];
        memset(stl_big, 0, sizeof(stl_big));
        put_u32_le(stl_big + 80U, 1U);
        put_u32_le(stl_big + 84U + 28U, 0x3F800000U); /* v1 = (0, 1, 0) */
        put_u32_le(stl_big + 84U + 36U, 0x3F800000U); /* v2.x = 1 */
        put_u32_le(stl_big + 84U + 44U, 0x47800000U); /* v2.z = 65536 */
        uint32_t bcnt = 0U;
        CHECK(bloom_stl_parse_to_mesh64(stl_big, sizeof(stl_big), 1U, false, &mesh_big, &bcnt) && bcnt == 1U);
        CHECK(mesh_big.map.tris[0].v2_q16[2] == 64 * BLOOM_Q16_ONE);
        CHECK(bloom_stl_parse_to_mesh64(stl_big, sizeof(stl_big), 1U, true, &mesh_big, &bcnt) && bcnt == 1U);
        CHECK(mesh_big.map.tris[0].v1_q16[1] < 1024);
        mix_bytes(&mesh_big.map.tris[0], sizeof(BloomTri64));

        uint8_t glb_big[32 + 36];
        memset(glb_big, 0, sizeof(glb_big));
        put_u32_le(glb_big + 0U, 0x46546C67U);
        put_u32_le(glb_big + 4U, 2U);
        put_u32_le(glb_big + 8U, sizeof(glb_big));
        put_u32_le(glb_big + 12U, 4U);
        put_u32_le(glb_big + 16U, 0x4E4F534AU);
        memcpy(glb_big + 20U, "{  }", 4U);
        put_u32_le(glb_big + 24U, 36U);
        put_u32_le(glb_big + 28U, 0x004E4942U);
        put_u32_le(glb_big + 32U + 16U, 0x3F800000U); /* v1 = (0, 1, 0) */
        put_u32_le(glb_big + 32U + 24U, 0x3F800000U); /* v2.x = 1 */
        put_u32_le(glb_big + 32U + 32U, 0x47800000U); /* v2.z = 65536 */
        CHECK(bloom_glb_parse_to_mesh64(glb_big, sizeof(glb_big), 1U, false, &mesh_big, &bcnt) && bcnt == 1U);
        CHECK(mesh_big.map.tris[0].v2_q16[2] == 64 * BLOOM_Q16_ONE);
        CHECK(bloom_glb_parse_to_mesh64(glb_big, sizeof(glb_big), 1U, true, &mesh_big, &bcnt) && bcnt == 1U);
        CHECK(mesh_big.map.tris[0].v1_q16[1] < 1024);
        mix_bytes(&mesh_big.map.tris[0], sizeof(BloomTri64));

        uint8_t glb_round_m[32 + 36];
        memcpy(glb_round_m, glb_big, sizeof(glb_round_m));
        put_u32_le(glb_round_m + 32U + 0U,  0x3F000080U);
        put_u32_le(glb_round_m + 32U + 4U,  0xBF000080U);
        put_u32_le(glb_round_m + 32U + 8U,  0x3F000040U);
        put_u32_le(glb_round_m + 32U + 12U, 0x3F800000U);
        put_u32_le(glb_round_m + 32U + 16U, 0x00000000U);
        put_u32_le(glb_round_m + 32U + 20U, 0x00000000U);
        put_u32_le(glb_round_m + 32U + 24U, 0x00000000U);
        put_u32_le(glb_round_m + 32U + 28U, 0x3F800000U);
        put_u32_le(glb_round_m + 32U + 32U, 0x3F000080U);
        CHECK(bloom_glb_parse_to_mesh64(glb_round_m, sizeof(glb_round_m), 1U, false, &mesh_big, &bcnt) && bcnt == 1U);
        CHECK(mesh_big.map.tris[0].v0_q16[0] == 32769 && mesh_big.map.tris[0].v0_q16[1] == -32769 && mesh_big.map.tris[0].v0_q16[2] == 32768);
        mix_bytes(&mesh_big.map.tris[0], sizeof(BloomTri64));
        CHECK(bloom_glb_parse_to_mesh64(glb_round_m, sizeof(glb_round_m), 1U, true, &mesh_big, &bcnt) && bcnt == 1U);
        mix_bytes(&mesh_big.map.tris[0], sizeof(BloomTri64));

        /* Multi-triangle GLB ingestion */
        uint8_t glb_m[32 + 4 * 36];
        memset(glb_m, 0, sizeof(glb_m));
        put_u32_le(glb_m + 0U, 0x46546C67U);
        put_u32_le(glb_m + 4U, 2U);
        put_u32_le(glb_m + 8U, sizeof(glb_m));
        put_u32_le(glb_m + 12U, 4U);
        put_u32_le(glb_m + 16U, 0x4E4F534AU);
        memcpy(glb_m + 20U, "{  }", 4U);
        put_u32_le(glb_m + 24U, 4U * 36U);
        put_u32_le(glb_m + 28U, 0x004E4942U);
        for (uint32_t t = 0U; t < 4U; ++t) {
            for (uint32_t v = 0U; v < 3U; ++v) {
                for (uint32_t a = 0U; a < 3U; ++a) {
                    put_u32_le(glb_m + 32U + t * 36U + v * 12U + a * 4U, (c_tris[t][v][a] < 0) ? 0xBF000000U : 0x3F000000U);
                }
            }
        }
        uint32_t gcnt_m = 0U;
        CHECK(bloom_glb_parse_to_mesh64(glb_m, sizeof(glb_m), BLOOM_MATTER_SAPPHIRE, true, &mesh_a, &gcnt_m) && gcnt_m == 4U);
        CHECK(!bloom_glb_parse_to_mesh64(glb_m, 20U, 0U, false, &mesh_a, &gcnt_m));
        mix_bytes(&mesh_a.map.tris[0], 4U * sizeof(BloomTri64));

        /* 5d. Brick world eval + extended raycast over Global CSG / Joint / Brick / Mesh */
        int32_t bsamples[64];
        for (uint32_t i = 0U; i < 64U; ++i) bsamples[i] = -24576;
        const int32_t borig[3] = { -32768, -32768, -32768 };
        BloomSdfBrick64 brick_w;
        bloom_sdf_brick64_pack(&brick_w, 1U, 1U, borig, 21845, bsamples);
        int64_t bw_in  = bloom_sdf_brick64_eval_world_q16(&brick_w, 0, 0, 0);
        int64_t bw_out = bloom_sdf_brick64_eval_world_q16(&brick_w, 4 * BLOOM_Q16_ONE, 0, 0);
        CHECK(bw_in < 0 && bw_out > 0);
        mix_u64((uint64_t)bw_in ^ (uint64_t)bw_out);

        static BloomEngineArena ex_arena;
        bloom_arena_init_default(&ex_arena, 320U, 180U);
        ex_arena.map.camera.eye_q16[0] = 0; ex_arena.map.camera.eye_q16[1] = 0; ex_arena.map.camera.eye_q16[2] = -6 * BLOOM_Q16_ONE;
        ex_arena.map.camera.target_q16[0] = 0; ex_arena.map.camera.target_q16[1] = 0; ex_arena.map.camera.target_q16[2] = 0;
        const int32_t cpos[3] = { 0, 0, 0 }, cext[3] = { 49152, 49152, 49152 };
        bloom_csg_op_init64(&ex_arena.map.csg_ops[0], 1U, BLOOM_CSG_PRIM_SPHERE, BLOOM_CSG_BOOL_UNION, 1U, cpos, cext, 0, 0);
        ex_arena.map.section_spec.global_csg_count = 1U;
        BloomRaycastHit64 ex_hit;
        CHECK(bloom_scene_raycast_ex64(&ex_arena, &mesh_a, 0, 0, &ex_hit) && ex_hit.hit_inst_id == 99U);
        mix_bytes(&ex_hit, sizeof(ex_hit));
        ex_arena.map.section_spec.global_csg_count = 0U;
        const int32_t qid[4] = { BLOOM_Q16_ONE, 0, 0, 0 }, jtr[3] = { 0, -16384, 0 };
        bloom_joint_dq64_make(&ex_arena.map.joints[0], 1U, 0U, qid, jtr, 32768, 32768, 0, 1U);
        ex_arena.map.section_spec.joint_count = 1U;
        CHECK(bloom_scene_raycast_ex64(&ex_arena, NULL, 0, 0, &ex_hit) && ex_hit.hit_inst_id == 100U);
        mix_u64(ex_hit.t_hit_q16);
        ex_arena.map.section_spec.joint_count = 0U;
        ex_arena.map.bricks[0] = brick_w;
        ex_arena.map.section_spec.brick_count = 1U;
        CHECK(bloom_scene_raycast_ex64(&ex_arena, NULL, 0, 0, &ex_hit) && ex_hit.hit_inst_id == 101U);
        ex_arena.map.section_spec.brick_count = 0U;
        CHECK(bloom_stl_parse_to_mesh64(stl, sizeof(stl), 1U, true, &mesh_a, &tcnt));
        ex_arena.map.section_spec.mesh_tri_count = (uint16_t)tcnt;
        ex_arena.map.section_spec.mesh_bvh_count = (uint16_t)bloom_bvh_build_from_tris64(mesh_a.map.tris, tcnt, mesh_a.map.bvh_nodes, BLOOM_MAX_MESH_BVH_NODES);
        ex_arena.map.section_spec.inspect_flags |= BLOOM_INSPECT_MESH_SOLID;
        CHECK(bloom_scene_raycast_ex64(&ex_arena, &mesh_a, 0, 0, &ex_hit) && ex_hit.hit_inst_id == 102U);
        mix_bytes(&ex_hit, sizeof(ex_hit));

        /* 5e. Swept-Sphere CCD (overlap, mid-flight hit, nonzero vb, miss) & Coupled Thermal-Mechanical Physics */
        const int32_t c_pa[3] = { -4 * BLOOM_Q16_ONE, 0, 0 }, c_pb[3] = { 0, 0, 0 };
        const int32_t c_va_hit[3] = { 8 * BLOOM_Q16_ONE, 0, 0 }, c_va_away[3] = { -4 * BLOOM_Q16_ONE, 0, 0 };
        const int32_t c_va_miss[3] = { 0, 8 * BLOOM_Q16_ONE, 0 }, c_vb0[3] = { 0, 0, 0 };
        const int32_t c_va_op[3] = { 4 * BLOOM_Q16_ONE, 2 * BLOOM_Q16_ONE, 0 };
        const int32_t c_vb_nz[3] = { -4 * BLOOM_Q16_ONE, 2 * BLOOM_Q16_ONE, 0 };
        int32_t toi = 0, cnrm[3] = { 0 };
        CHECK(bloom_swept_sphere_ccd_q16(c_pb, c_vb0, 32768, c_pb, c_vb0, 32768, &toi, cnrm) && toi == 0);
        CHECK(bloom_swept_sphere_ccd_q16(c_pa, c_va_hit, 32768, c_pb, c_vb0, 32768, &toi, cnrm) && toi > 23500 && toi < 25600);
        mix_u64((uint64_t)(uint32_t)toi ^ (uint64_t)(uint32_t)cnrm[0]);
        CHECK(bloom_swept_sphere_ccd_q16(c_pa, c_va_op, 32768, c_pb, c_vb_nz, 32768, &toi, cnrm) && toi > 23500 && toi < 25600);
        mix_u64((uint64_t)(uint32_t)toi);
        CHECK(!bloom_swept_sphere_ccd_q16(c_pa, c_va_away, 32768, c_pb, c_vb0, 32768, &toi, cnrm));
        CHECK(!bloom_swept_sphere_ccd_q16(c_pa, c_va_miss, 32768, c_pb, c_vb0, 32768, &toi, cnrm));

        static BloomEngineArena phys_arena;
        bloom_arena_init_default(&phys_arena, 320U, 180U);
        const int32_t q_p0[3] = { 0, 0, 0 }, q_p1[3] = { 32768, 0, 0 }, q_p2[3] = { 4 * BLOOM_Q16_ONE, 0, 49152 };
        const int32_t q_p3_guard[3] = { 0, 0, 0 };
        uint32_t pi0 = bloom_prefab_spawn_inst(&phys_arena, BLOOM_PREFAB_CRYSTAL_SPHERE, q_p0, BLOOM_Q16_ONE);
        uint32_t pi1 = bloom_prefab_spawn_inst(&phys_arena, BLOOM_PREFAB_CRYSTAL_SPHERE, q_p1, BLOOM_Q16_ONE);
        uint32_t pi2 = bloom_prefab_spawn_inst(&phys_arena, BLOOM_PREFAB_CRYSTAL_SPHERE, q_p2, BLOOM_Q16_ONE);
        (void)bloom_prefab_spawn_inst(&phys_arena, BLOOM_PREFAB_CRYSTAL_SPHERE, q_p3_guard, BLOOM_Q16_ONE);
        phys_arena.map.frame_hdr.inst_count = 3U; /* keep active collidable sentinel in slot 3 just beyond inst_count */
        phys_arena.map.instances[pi0 - 1U].thermal_kelvin = 950U;
        phys_arena.map.instances[pi1 - 1U].thermal_kelvin = 300U;
        phys_arena.map.instances[pi2 - 1U].rot_euler_q16[1] = 24576;
        int32_t wit[3], pnrm[3];
        int32_t c_ov  = bloom_scene_eval_pair_clearance_q16(&phys_arena, 0U, 1U, wit, pnrm);
        int32_t c_sep = bloom_scene_eval_pair_clearance_q16(&phys_arena, 0U, 2U, wit, pnrm);
        CHECK(c_ov < 0 && c_sep > 0 && wit[2] > 0);
        mix_u64((uint64_t)(uint32_t)c_ov ^ (uint64_t)(uint32_t)c_sep ^ (uint64_t)(uint32_t)wit[2]);
        bloom_physics_step_arena64(&phys_arena, BLOOM_Q16_ONE);
        CHECK(phys_arena.map.phys_summary.interference_pairs == 1U);
        CHECK(phys_arena.map.instances[pi0 - 1U].thermal_kelvin < 950U && phys_arena.map.instances[pi1 - 1U].thermal_kelvin > 300U);
        mix_bytes(&phys_arena.map.phys_summary, sizeof(BloomPhysicsSummary64));
    }

    /* Section 6: 2-Jet Differential Geometry, Sub-Frame Dynamic Rebound & Modal Physical DSP */
    {
        static BloomEngineArena v8a;
        BloomFieldJet64 jet;
        const int32_t orig[3] = { 0, 0, 0 };

        /* 6a. bloom_scene_eval_jet2_q16 guards, empty arena, |n.y| branches, convex/concave/saddle, mesh */
        bloom_scene_eval_jet2_q16(NULL, NULL, 0, 0, 0, NULL);
        bloom_scene_eval_jet2_q16(NULL, NULL, 0, 0, 0, &jet);
        CHECK(jet.mean_curv_q16 == 0 && jet.gauss_curv_q16 == 0);
        mix_bytes(&jet, sizeof(jet));
        bloom_arena_init_default(&v8a, 320U, 180U);
        bloom_scene_eval_jet2_q16(&v8a, NULL, 0, 0, 0, &jet);
        mix_bytes(&jet, sizeof(jet));

        (void)bloom_prefab_spawn_inst(&v8a, BLOOM_PREFAB_CRYSTAL_SPHERE, orig, BLOOM_Q16_ONE);
        bloom_scene_eval_jet2_q16(&v8a, NULL, BLOOM_Q16_ONE, 0, 0, &jet);
        CHECK(jet.mean_curv_q16 > 50000 && jet.gauss_curv_q16 > 40000);
        mix_bytes(&jet, sizeof(jet));
        bloom_scene_eval_jet2_q16(&v8a, NULL, 0, BLOOM_Q16_ONE, 0, &jet); /* |n.y| >= 52000 branch */
        CHECK(jet.grad_q16[1] > 60000 && jet.mean_curv_q16 > 50000);
        mix_bytes(&jet, sizeof(jet));

        bloom_arena_init_default(&v8a, 320U, 180U);
        (void)bloom_prefab_spawn_inst(&v8a, BLOOM_PREFAB_JADE_VESSEL, orig, BLOOM_Q16_ONE);
        bloom_scene_eval_jet2_q16(&v8a, NULL, 42598, 0, 0, &jet); /* concave inner cavity H < 0 */
        CHECK(jet.mean_curv_q16 < -50000);
        mix_bytes(&jet, sizeof(jet));

        bloom_arena_init_default(&v8a, 320U, 180U);
        BloomPrefabDesc64 *tor = &v8a.map.prefabs[8];
        memset(tor, 0, sizeof(*tor));
        tor->prefab_id = 9U;
        tor->art_style_id = 6U;
        tor->matter_id = BLOOM_MATTER_SAPPHIRE;
        tor->csg_op_mask = BLOOM_CSG_MASK_PACK(BLOOM_CSG_PRIM_TORUS, BLOOM_CSG_PRIM_NONE, BLOOM_CSG_BOOL_UNION, 0, 0);
        tor->bounding_radius_q16 = 98304;
        tor->prim_params_q16[0] = 65536;
        tor->prim_params_q16[1] = 16384;
        tor->flags = 1U;
        uint32_t ti = bloom_prefab_spawn_inst(&v8a, 9U, orig, BLOOM_Q16_ONE);
        bloom_scene_eval_jet2_q16(&v8a, NULL, 49152, 0, 0, &jet); /* outer/inner saddle waist */
        CHECK(jet.gauss_curv_q16 < -100000 && jet.anisotropy_q16 > 150000);
        mix_bytes(&jet, sizeof(jet));
        /* Rotate torus 90 deg around X & offset eval so diff_s switches sign */
        v8a.map.instances[ti - 1U].rot_euler_q16[0] = 102944; /* pi/2 */
        bloom_scene_eval_jet2_q16(&v8a, NULL, 0, 49152, 16384, &jet);
        mix_bytes(&jet, sizeof(jet));
        bloom_scene_eval_jet2_q16(&v8a, NULL, 81920, 0, 0, &jet);
        mix_bytes(&jet, sizeof(jet));

        /* Evaluate with optional mesh_a */
        static BloomMeshArena v8m;
        memset(&v8m, 0, sizeof(v8m));
        const int32_t ta[3] = { -65536, -65536, 0 }, tb[3] = { 65536, -65536, 0 }, tc[3] = { 0, 65536, 0 };
        bloom_tri64_init(&v8m.map.tris[0], 0U, BLOOM_MATTER_TITANIUM, 8192, ta, tb, tc);
        v8a.map.section_spec.mesh_tri_count = 1U;
        v8a.map.section_spec.mesh_bvh_count = (uint16_t)bloom_bvh_build_from_tris64(v8m.map.tris, 1U, v8m.map.bvh_nodes, BLOOM_MAX_MESH_BVH_NODES);
        v8a.map.section_spec.inspect_flags |= BLOOM_INSPECT_MESH_SOLID;
        bloom_scene_eval_jet2_q16(&v8a, &v8m, 0, 0, 8192, &jet);
        mix_bytes(&jet, sizeof(jet));
        bloom_scene_eval_jet2_q16(&v8a, &v8m, 32768, 24576, 16384, &jet);
        mix_bytes(&jet, sizeof(jet));

        /* 6b. Velocity codecs & all 3 dynamic Swept-CCD rebound arms (a&b dyn, a dyn only, b dyn only) */
        int32_t vbuf[3] = { 1, 2, 3 };
        bloom_inst_set_vel_q16(NULL, NULL);
        bloom_inst_get_vel_q16(NULL, vbuf);
        CHECK(vbuf[0] == 0 && vbuf[1] == 0 && vbuf[2] == 0);
        bloom_arena_init_default(&v8a, 320U, 180U);
        uint32_t ia = bloom_prefab_spawn_inst(&v8a, BLOOM_PREFAB_CRYSTAL_SPHERE, orig, BLOOM_Q16_ONE);
        const int32_t sat_v[3] = { 1000 * BLOOM_Q16_ONE, -1000 * BLOOM_Q16_ONE, 12288 };
        bloom_inst_set_vel_q16(&v8a.map.instances[ia - 1U], sat_v);
        bloom_inst_get_vel_q16(&v8a.map.instances[ia - 1U], vbuf);
        CHECK(vbuf[0] > 30 * BLOOM_Q16_ONE && vbuf[1] < -30 * BLOOM_Q16_ONE && vbuf[2] == 12288);
        mix_bytes(vbuf, sizeof(vbuf));
        const int32_t odd_neg_v[3] = { -6000, -6000, -6000 }; /* non-4096 negative truncation on all 3 axes */
        bloom_inst_set_vel_q16(&v8a.map.instances[ia - 1U], odd_neg_v);
        bloom_inst_get_vel_q16(&v8a.map.instances[ia - 1U], vbuf);
        CHECK(vbuf[0] == -4096 && vbuf[1] == -4096 && vbuf[2] == -4096);
        mix_bytes(vbuf, sizeof(vbuf));
        bloom_inst_set_vel_q16(&v8a.map.instances[ia - 1U], NULL);
        bloom_inst_get_vel_q16(&v8a.map.instances[ia - 1U], vbuf);
        CHECK(vbuf[0] == 0 && vbuf[1] == 0 && vbuf[2] == 0);
        const int32_t z_v[3] = { 0, 0, 0 };
        bloom_inst_set_vel_q16(&v8a.map.instances[ia - 1U], z_v);
        CHECK(v8a.map.instances[ia - 1U].kin_vel_packed == 0U);

        /* Pair 1: both dynamic; Pair 2: a dynamic vs static wall; Pair 3: static vs b dynamic; Pair 4: 3D oblique */
        const int32_t p_left[3] = { -163840, 0, 0 }, p_right[3] = { 163840, 0, 0 };
        const int32_t v_pos[3] = { 262144, 0, 0 }, v_neg[3] = { -262144, 0, 0 };
        const int32_t p_ob_a[3] = { -163840, -81920, -40960 }, p_ob_b[3] = { 163840, 81920, 40960 };
        const int32_t v_ob_a[3] = {  262144, 131072,  65536 }, v_ob_b[3] = { -262144, -131072, -65536 };
        for (uint32_t mode = 0U; mode < 4U; ++mode) {
            bloom_arena_init_default(&v8a, 320U, 180U);
            uint32_t ba = bloom_prefab_spawn_inst(&v8a, BLOOM_PREFAB_CRYSTAL_SPHERE, (mode == 3U) ? p_ob_a : p_left, BLOOM_Q16_ONE);
            uint32_t bb = bloom_prefab_spawn_inst(&v8a, BLOOM_PREFAB_CRYSTAL_SPHERE, (mode == 3U) ? p_ob_b : p_right, BLOOM_Q16_ONE);
            v8a.map.instances[ba - 1U].thermal_kelvin = 900U;
            if (mode == 0U) {
                bloom_inst_set_vel_q16(&v8a.map.instances[ba - 1U], v_pos);
                bloom_inst_set_vel_q16(&v8a.map.instances[bb - 1U], v_neg);
            } else if (mode == 1U) {
                bloom_inst_set_vel_q16(&v8a.map.instances[ba - 1U], v_pos);
            } else if (mode == 2U) {
                bloom_inst_set_vel_q16(&v8a.map.instances[bb - 1U], v_neg);
            } else {
                bloom_inst_set_vel_q16(&v8a.map.instances[ba - 1U], v_ob_a);
                bloom_inst_set_vel_q16(&v8a.map.instances[bb - 1U], v_ob_b);
            }
            bloom_physics_step_arena64(&v8a, (mode == 1U) ? (2 * BLOOM_Q16_ONE) : BLOOM_Q16_ONE);
            CHECK((v8a.map.phys_summary.phys_flags & BLOOM_PHYS_FLAG_CCD_HIT) != 0U);
            mix_bytes(&v8a.map.phys_summary, sizeof(BloomPhysicsSummary64));
            mix_bytes(&v8a.map.instances[ba - 1U], sizeof(BloomPrefabInst64));
            mix_bytes(&v8a.map.instances[bb - 1U], sizeof(BloomPrefabInst64));
        }

        /* 6c. Modal DSP init, guard rails, all 3 constitutive branches (Dielec/Metal/Amorph), pan, scrub & decay */
        static BloomAudioRig rig;
        static int16_t pcm[2U * 800U];
        bloom_audio_init_rig(NULL, 48000U);
        bloom_audio_init_rig(&rig, 100U);     /* < 8000 clamp -> 48000 */
        CHECK(rig.hdr.sample_rate_hz == 48000U);
        mix_bytes(&rig.hdr, sizeof(BloomAudioHeader64));
        bloom_audio_init_rig(&rig, 500000U);  /* > 192000 clamp -> 192000 */
        CHECK(rig.hdr.sample_rate_hz == 192000U);
        bloom_audio_init_rig(&rig, 48000U);

        bloom_arena_init_default(&v8a, 320U, 180U);
        uint32_t s_glass = bloom_prefab_spawn_inst(&v8a, BLOOM_PREFAB_CRYSTAL_SPHERE, p_left, BLOOM_Q16_ONE);
        uint32_t s_metal = bloom_prefab_spawn_inst(&v8a, BLOOM_PREFAB_CITADEL_CORE, orig, 24576);
        uint32_t s_water = bloom_prefab_spawn_inst(&v8a, BLOOM_PREFAB_CAPSULE_PILLAR, p_right, 163840);
        v8a.map.instances[s_metal - 1U].thermal_kelvin = 1400U;
        bloom_inst_set_vel_q16(&v8a.map.instances[s_glass - 1U], v_pos);
        bloom_inst_set_vel_q16(&v8a.map.instances[s_water - 1U], v_neg);
        bloom_physics_step_arena64(&v8a, BLOOM_Q16_ONE);

        bloom_audio_excite_impact64(NULL, &v8a, 0U, BLOOM_Q16_ONE, 0);
        rig.hdr.sample_rate_hz = 100U; /* force sr guard branch inside excite_impact64 */
        bloom_audio_excite_impact64(&rig, &v8a, s_glass - 1U, -8 * BLOOM_Q16_ONE, -BLOOM_Q16_ONE);
        rig.hdr.sample_rate_hz = 48000U;
        bloom_audio_excite_impact64(&rig, &v8a, s_metal - 1U, 4 * BLOOM_Q16_ONE, 0);
        bloom_audio_excite_impact64(&rig, &v8a, s_water - 1U, 2 * BLOOM_Q16_ONE, BLOOM_Q16_ONE);
        mix_bytes(&rig, sizeof(rig));

        v8a.map.section_spec.plane_dist_q16 = 4096; /* trigger section-plane friction excitation */
        uint32_t hs = bloom_audio_step_frame64(&rig, &v8a, pcm, 800U);
        CHECK(rig.hdr.peak_abs_pcm > 0);
        mix_u64((uint64_t)hs ^ (uint64_t)(uint32_t)rig.hdr.peak_abs_pcm);
        mix_bytes(pcm, sizeof(pcm));

        v8a.map.phys_summary.step_epoch = rig.hdr.last_phys_epoch + 1U;
        v8a.map.phys_summary.phys_flags |= BLOOM_PHYS_FLAG_CCD_HIT;
        v8a.map.phys_summary.earliest_toi_q16 = 32768;
        uint32_t hs_toi = bloom_audio_step_frame64(&rig, &v8a, pcm, 800U);
        CHECK(rig.hdr.last_subframe_sample == 400U);
        mix_u64((uint64_t)hs_toi ^ (uint64_t)rig.hdr.last_subframe_sample);
        mix_bytes(pcm, sizeof(pcm));

        /* Targeted modal energy-gate & skipped-mode branch probe on quiet clean arena (no CCD/scrub override) */
        bloom_arena_init_default(&v8a, 320U, 180U);
        bloom_audio_init_rig(&rig, 48000U);
        rig.voices[0].active = 1U;
        rig.voices[0].pan_l_q14 = 11585;
        rig.voices[0].pan_r_q14 = 11585;
        rig.voices[0].decay_q15[0] = 0U;      /* skipped m=0 -> hits 0x43c0..0x43c8 cbnz m=1! */
        rig.voices[0].u_q24[0] = 65536;
        rig.voices[0].cos_q14[1] = 16000;
        rig.voices[0].sin_q14[1] = 3000;
        rig.voices[0].decay_q15[1] = 32000U;  /* active m=1 after skipped m=0 */
        rig.voices[0].u_q24[1] = 262144;
        rig.voices[1].active = 1U;
        rig.voices[1].pan_l_q14 = 11585;
        rig.voices[1].pan_r_q14 = 11585;
        rig.voices[1].cos_q14[3] = 16384;
        rig.voices[1].sin_q14[3] = 0;
        rig.voices[1].decay_q15[3] = 32767U;
        rig.voices[1].u_q24[3] = 20;
        rig.voices[1].v_q24[3] = 30;          /* positive small: |19| + |29| = 48 < 64 -> must deactivate! */
        (void)bloom_audio_step_frame64(&rig, &v8a, pcm, 1U);
        CHECK(rig.voices[0].active == 1U && rig.voices[1].active == 0U);
        mix_bytes(&rig, sizeof(rig));
        mix_bytes(pcm, sizeof(pcm));
        CHECK(bloom_audio_step_frame64(NULL, &v8a, pcm, 800U) == 0U);
    }

    /* Section 7: Concentric Gameplay Shells, 16KB Rewind, Metric/Doppler, CineCamera & Moodboard SSOT */
    {
        static BloomEngineArena s7_a, s7_b;
        static BloomAudioRig s7_r0, s7_r1;
        static BloomSpacetimeSnap s7_ring[BLOOM_REWIND_RING_CAP];
        static int16_t s7_pcm[1600];
        static uint8_t s7_rgb[36U * 40U * 3U];

        /* 7a. Thin-lens CoC & CineCamera deadzone + trauma decay across epoch parities */
        BloomCamera64 cam;
        bloom_camera_init64(&cam, 0, 16384, 5 * BLOOM_Q16_ONE, 85 * BLOOM_Q16_ONE, 117964);
        CHECK(bloom_camera_eval_coc_q16(NULL, 65536) == 0);
        CHECK(bloom_camera_eval_coc_q16(&cam, 4096) == 0);
        BloomCamera64 bad_cam = cam;
        bad_cam.focus_dist_q16 = 4096;
        CHECK(bloom_camera_eval_coc_q16(&bad_cam, 65536) == 0);
        CHECK(bloom_camera_eval_coc_q16(&cam, cam.focus_dist_q16) == 0);
        mix_u64((uint64_t)(uint32_t)bloom_camera_eval_coc_q16(&cam, 8192));
        mix_u64((uint64_t)(uint32_t)bloom_camera_eval_coc_q16(&cam, cam.focus_dist_q16 - 4096));
        mix_u64((uint64_t)(uint32_t)bloom_camera_eval_coc_q16(&cam, cam.focus_dist_q16 + 4096));
        mix_u64((uint64_t)(uint32_t)bloom_camera_eval_coc_q16(&cam, 20 * BLOOM_Q16_ONE));

        BloomRegBank64 regs;
        memset(&regs, 0, sizeof(regs));
        bloom_camera_step_cine64(NULL, &regs, NULL, 0, 0);
        const int32_t subj_pos[3] = { 3 * BLOOM_Q16_ONE + 7, -2 * BLOOM_Q16_ONE + 5, BLOOM_Q16_ONE + 3 };
        const int32_t subj_neg[3] = { -4 * BLOOM_Q16_ONE - 3, 4 * BLOOM_Q16_ONE + 3, -2 * BLOOM_Q16_ONE - 3 };
        bloom_camera_step_cine64(&cam, &regs, subj_pos, -100, 90000);  /* epoch 1: trauma clamp + amp */
        mix_bytes(&cam, sizeof(cam));
        bloom_camera_step_cine64(&cam, &regs, subj_neg, 16385, -50);   /* epoch 2: diff < -dz, non-mult-of-4 */
        mix_bytes(&cam, sizeof(cam));
        bloom_camera_step_cine64(&cam, &regs, subj_pos, 655360, 0);    /* epoch 3: inside deadzone */
        mix_bytes(&cam, sizeof(cam));
        bloom_camera_step_cine64(&cam, &regs, NULL, 16384, 32768);     /* epoch 4: (ep&3)==0 branch */
        bloom_camera_step_cine64(&cam, NULL, subj_pos, 16384, 128);    /* opt_regs == NULL, low trauma */
        mix_bytes(&cam, sizeof(cam));
        mix_bytes(&regs, sizeof(regs));

        /* 7b. Relativistic Metric Scale & Doppler Audio Excitation */
        BloomMetricScale64 ms;
        bloom_metric_eval_scale64(0, 0, 0, NULL, NULL, NULL);
        const int32_t vdir[3] = { 65536, -32768, 16384 };
        const int32_t mctr[3] = { 1000, -2000, 3000 };
        static const int32_t betas[5] = { 0, 32768, -32768, 70000, -70000 };
        static const int32_t phis[5]  = { -40000, 0, 16384, 32768, 60000 };
        static const int32_t fluxs[3] = { -300000, 0, 300000 };
        for (uint32_t i = 0U; i < 5U; ++i) {
            bloom_metric_eval_scale64(betas[i], phis[i], fluxs[i % 3U],
                                      (i & 1U) ? vdir : NULL, (i & 1U) ? NULL : mctr, &ms);
            mix_bytes(&ms, sizeof(ms));
        }

        bloom_arena_init_default(&s7_a, 320U, 180U);
        const int32_t p_pl[3] = { 0, 0, 0 };
        const int32_t p_hz[3] = { 170393, 0, 0 };
        (void)bloom_prefab_spawn_inst(&s7_a, BLOOM_PREFAB_CRYSTAL_SPHERE, p_pl, BLOOM_Q16_ONE);
        s7_a.map.frame_hdr.flags |= BLOOM_FLAG_METRIC_FIELD;
        bloom_reg_bank_set_q16(&s7_a.map.reg_bank, BLOOM_REG_LORENTZ_BETA, 32768);
        bloom_audio_init_rig(&s7_r0, BLOOM_AUDIO_SAMPLE_RATE);
        bloom_audio_excite_impact64(&s7_r0, &s7_a, 0U, 4 * BLOOM_Q16_ONE, 0);
        bloom_reg_bank_set_q16(&s7_a.map.reg_bank, BLOOM_REG_LORENTZ_BETA, -32768);
        bloom_audio_init_rig(&s7_r1, BLOOM_AUDIO_SAMPLE_RATE);
        bloom_audio_excite_impact64(&s7_r1, &s7_a, 0U, 4 * BLOOM_Q16_ONE, 0);
        CHECK(s7_r0.voices[0].cos_q14[0] < s7_r1.voices[0].cos_q14[0]);
        mix_bytes(&s7_r0, sizeof(s7_r0));
        mix_bytes(&s7_r1, sizeof(s7_r1));

        /* 7c. Moodboard RGB Extractor (all matter regimes + downsample) & SSOT Compiler */
        BloomMoodboard64 mb;
        BloomArtStyle64 st;
        BloomPrefabDesc64 pf;
        CHECK(!bloom_moodboard_extract_rgb24(NULL, 4U, 4U, 12U, &mb));
        CHECK(!bloom_moodboard_extract_rgb24(s7_rgb, 4U, 4U, 12U, NULL));
        CHECK(!bloom_moodboard_extract_rgb24(s7_rgb, 1U, 4U, 12U, &mb));
        CHECK(!bloom_moodboard_extract_rgb24(s7_rgb, 4U, 1U, 12U, &mb));
        CHECK(!bloom_moodboard_extract_rgb24(s7_rgb, 4U, 4U, 11U, &mb));
        bloom_moodboard_compile_to_ssot64(NULL, 1U, 1U, &st, &pf);

        /* Regime 1: 36x40 Obsidian with single vivid chroma pixel */
        for (uint32_t i = 0U; i < 36U * 40U; ++i) {
            s7_rgb[i * 3U + 0U] = 10U; s7_rgb[i * 3U + 1U] = 12U; s7_rgb[i * 3U + 2U] = 14U;
        }
        s7_rgb[0] = 200U; s7_rgb[1] = 10U; s7_rgb[2] = 5U;
        CHECK(bloom_moodboard_extract_rgb24(s7_rgb, 36U, 40U, 36U * 3U, &mb));
        CHECK(mb.inferred_matter_id == BLOOM_MATTER_OBSIDIAN);
        mix_bytes(&mb, sizeof(mb));

        /* Regime 2: 2x2 Sapphire (no edge density) & bin fallback */
        static const uint8_t sap[12] = { 30,80,210, 25,75,215, 32,82,205, 28,78,212 };
        CHECK(bloom_moodboard_extract_rgb24(sap, 2U, 2U, 6U, &mb));
        CHECK(mb.inferred_matter_id == BLOOM_MATTER_SAPPHIRE);
        bloom_moodboard_compile_to_ssot64(&mb, 0U, 9999U, &st, NULL);
        mix_bytes(&st, sizeof(st));

        /* Regime 3: 4x4 Imperial Jade */
        for (uint32_t i = 0U; i < 16U; ++i) {
            s7_rgb[i * 3U + 0U] = 20U; s7_rgb[i * 3U + 1U] = 175U; s7_rgb[i * 3U + 2U] = 45U;
        }
        CHECK(bloom_moodboard_extract_rgb24(s7_rgb, 4U, 4U, 12U, &mb));
        CHECK(mb.inferred_matter_id == BLOOM_MATTER_IMPERIAL_JADE);
        mb.inferred_prim_mask = 0U;
        bloom_moodboard_compile_to_ssot64(&mb, 9999U, 0U, NULL, &pf);
        mix_bytes(&pf, sizeof(pf));

        /* Regime 4: 8x8 Titanium high-contrast split (specular > 12000 & contrast > 36000) */
        for (uint32_t y = 0U; y < 8U; ++y) {
            for (uint32_t x = 0U; x < 8U; ++x) {
                size_t k = ((size_t)y * 8U + (size_t)x) * 3U;
                uint8_t v = (x < 4U) ? 245U : 15U;
                s7_rgb[k + 0U] = v; s7_rgb[k + 1U] = v; s7_rgb[k + 2U] = v;
            }
        }
        CHECK(bloom_moodboard_extract_rgb24(s7_rgb, 8U, 8U, 24U, &mb));
        CHECK(mb.inferred_matter_id == BLOOM_MATTER_TITANIUM);
        bloom_moodboard_compile_to_ssot64(&mb, 2U, 3U, &st, &pf);
        mix_bytes(&st, sizeof(st));
        mix_bytes(&pf, sizeof(pf));

        /* Regime 5: 8x8 Crystal Glass neutral midtone */
        for (uint32_t i = 0U; i < 64U; ++i) {
            s7_rgb[i * 3U + 0U] = 128U; s7_rgb[i * 3U + 1U] = 128U; s7_rgb[i * 3U + 2U] = 128U;
        }
        CHECK(bloom_moodboard_extract_rgb24(s7_rgb, 8U, 8U, 24U, &mb));
        CHECK(mb.inferred_matter_id == BLOOM_MATTER_CRYSTAL_GLASS);
        mix_bytes(&mb, sizeof(mb));

        /* Regime 6: 16x16 Aurum Gold high-edge checkerboard */
        for (uint32_t y = 0U; y < 16U; ++y) {
            for (uint32_t x = 0U; x < 16U; ++x) {
                size_t k = ((size_t)y * 16U + (size_t)x) * 3U;
                bool ck = (((x + y) & 1U) != 0U);
                s7_rgb[k + 0U] = ck ? 245U : 190U;
                s7_rgb[k + 1U] = ck ? 195U : 140U;
                s7_rgb[k + 2U] = 32U;
            }
        }
        CHECK(bloom_moodboard_extract_rgb24(s7_rgb, 16U, 16U, 48U, &mb));
        CHECK(mb.inferred_matter_id == BLOOM_MATTER_AURUM_GOLD);
        bloom_moodboard_compile_to_ssot64(&mb, 4U, 5U, &st, &pf);
        mix_bytes(&mb, sizeof(mb));
        mix_bytes(&st, sizeof(st));
        mix_bytes(&pf, sizeof(pf));

        /* 7d. Concentric Gameplay Shells, Hysteresis, 6-Frame Buffer, Fulcrum & -18dB Hitstop */
        BloomGameplayState64 gp;
        bloom_gameplay_init64(NULL, 0U, 0, 0, 0);
        bloom_gameplay_init64(&gp, 0U, 100, 100, 100);
        CHECK(gp.player_inst_id == 1U && gp.delta_hurt_q16 == 2048 && gp.delta_graze_q16 == 4096);

        bloom_arena_init_default(&s7_a, 320U, 180U);
        CHECK(bloom_gameplay_step64(NULL, &s7_a, NULL, NULL, false) == 0U);
        CHECK(bloom_gameplay_step64(&gp, NULL, NULL, NULL, false) == 0U);
        CHECK(bloom_gameplay_step64(&gp, &s7_a, NULL, NULL, false) == 0U); /* inst_count == 0 */

        bloom_audio_init_rig(&s7_r0, BLOOM_AUDIO_SAMPLE_RATE);
        uint32_t pid = bloom_prefab_spawn_inst(&s7_a, BLOOM_PREFAB_CRYSTAL_SPHERE, p_pl, BLOOM_Q16_ONE);
        uint32_t hid = bloom_prefab_spawn_inst(&s7_a, BLOOM_PREFAB_CRYSTAL_SPHERE, p_hz, BLOOM_Q16_ONE);
        s7_a.map.instances[pid - 1U].flags |= BLOOM_INST_FLAG_COLLIDABLE;
        s7_a.map.instances[hid - 1U].flags |= BLOOM_INST_FLAG_COLLIDABLE;

        /* Inactive player slot 0, inst_count bound (ignore active slot 2 when inst_count==2), & fallback */
        s7_a.map.instances[2] = s7_a.map.instances[hid - 1U];
        s7_a.map.instances[2].inst_id = 3U;
        s7_a.map.instances[2].pos_q16[0] = 65536;
        CHECK(bloom_gameplay_step64(&gp, &s7_a, NULL, NULL, false) == 0U);
        memset(&s7_a.map.instances[2], 0, sizeof(BloomPrefabInst64));

        s7_a.map.instances[pid - 1U].flags &= ~BLOOM_INST_FLAG_ACTIVE;
        CHECK(bloom_gameplay_step64(&gp, &s7_a, NULL, NULL, false) == 0U);
        s7_a.map.instances[pid - 1U].flags |= BLOOM_INST_FLAG_ACTIVE;
        bloom_gameplay_init64(&gp, 99U, 16384, 32768, 65536);
        (void)bloom_gameplay_step64(&gp, &s7_a, NULL, NULL, false);

        /* Re-init with pid & exercise input buffer expiry (6 -> 0 out-of-range) + direct/negative/delayed fulcrum */
        bloom_gameplay_init64(&gp, pid, 16384, 32768, 65536);
        const int32_t imp_x[3] = { 15000, -3000, -7000 };
        const int32_t imp_dir[3] = { 3000, 2500, -1500 };
        const int32_t imp_neg[3] = { -15000, -17000, -19000 };
        s7_a.map.instances[hid - 1U].pos_q16[0] = 176947; /* +2.70 -> inside hit, outside graze */
        uint32_t ev_dir = bloom_gameplay_step64(&gp, &s7_a, NULL, imp_dir, true);
        CHECK((ev_dir & BLOOM_GP_EVENT_FULCRUM) != 0U);
        mix_bytes(s7_a.map.instances, 2U * sizeof(BloomPrefabInst64));
        bloom_inst_set_vel_q16(&s7_a.map.instances[pid - 1U], NULL);
        bloom_inst_set_vel_q16(&s7_a.map.instances[hid - 1U], NULL);
        s7_a.map.instances[pid - 1U].pos_q16[0] = 0;
        s7_a.map.instances[pid - 1U].pos_q16[1] = 0;
        s7_a.map.instances[pid - 1U].pos_q16[2] = 0;
        s7_a.map.instances[hid - 1U].pos_q16[0] = 176947;
        s7_a.map.instances[hid - 1U].pos_q16[1] = 0;
        s7_a.map.instances[hid - 1U].pos_q16[2] = 0;
        CHECK((bloom_gameplay_step64(&gp, &s7_a, NULL, imp_neg, true) & BLOOM_GP_EVENT_FULCRUM) != 0U);
        mix_bytes(s7_a.map.instances, 2U * sizeof(BloomPrefabInst64));
        bloom_inst_set_vel_q16(&s7_a.map.instances[pid - 1U], NULL);
        bloom_inst_set_vel_q16(&s7_a.map.instances[hid - 1U], NULL);
        s7_a.map.instances[pid - 1U].pos_q16[0] = 0;
        s7_a.map.instances[pid - 1U].pos_q16[1] = 0;
        s7_a.map.instances[pid - 1U].pos_q16[2] = 0;

        s7_a.map.instances[hid - 1U].pos_q16[0] = 262144; /* +4.00 out of hit shell */
        (void)bloom_gameplay_step64(&gp, &s7_a, NULL, imp_x, true);
        CHECK(gp.buffered_action == 1U && gp.input_buf_frames == 6U);
        for (uint32_t b = 0U; b < 6U; ++b) {
            (void)bloom_gameplay_step64(&gp, &s7_a, NULL, NULL, false);
        }
        CHECK(gp.buffered_action == 0U && gp.input_buf_frames == 0U);

        /* Delayed buffered fulcrum inside hit shell (outside graze) with fulcrum_imp_q16 == NULL */
        (void)bloom_gameplay_step64(&gp, &s7_a, NULL, imp_x, true);
        s7_a.map.instances[hid - 1U].pos_q16[0] = 176947; /* +2.70 -> clr = +0.70 */
        uint32_t ev_ful = bloom_gameplay_step64(&gp, &s7_a, NULL, NULL, false);
        CHECK((ev_ful & BLOOM_GP_EVENT_FULCRUM) != 0U);
        mix_bytes(s7_a.map.instances, 2U * sizeof(BloomPrefabInst64));

        /* Reset velocities & exercise Graze hysteresis + -18dB Hitstop vs Catharsis */
        bloom_inst_set_vel_q16(&s7_a.map.instances[pid - 1U], NULL);
        bloom_inst_set_vel_q16(&s7_a.map.instances[hid - 1U], NULL);
        s7_a.map.instances[pid - 1U].pos_q16[0] = 0;
        s7_a.map.instances[pid - 1U].pos_q16[1] = 0;
        s7_a.map.instances[pid - 1U].pos_q16[2] = 0;
        s7_a.map.instances[hid - 1U].pos_q16[0] = 157286; /* +2.40 -> clr = +0.40 graze enter */
        s7_a.map.instances[hid - 1U].pos_q16[1] = 0;
        s7_a.map.instances[hid - 1U].pos_q16[2] = 0;
        uint32_t ev_gr = bloom_gameplay_step64(&gp, &s7_a, &s7_r0, NULL, false);
        CHECK((ev_gr & BLOOM_GP_EVENT_GRAZE) != 0U);
        s7_a.map.instances[hid - 1U].pos_q16[0] = 167116; /* +2.55 -> deadband */
        (void)bloom_gameplay_step64(&gp, &s7_a, &s7_r0, NULL, false);
        CHECK(gp.graze_latched == 1U);
        s7_a.map.instances[hid - 1U].pos_q16[0] = 180224; /* +2.75 -> release */
        (void)bloom_gameplay_step64(&gp, &s7_a, &s7_r0, NULL, false);
        CHECK(gp.graze_latched == 0U);

        s7_a.map.instances[pid - 1U].pos_q16[0] = 0;
        s7_a.map.instances[hid - 1U].pos_q16[0] = 85196;  /* +1.30 -> clr = -0.35 < -0.25 hurt */
        uint32_t ev_hs = bloom_gameplay_step64(&gp, &s7_a, &s7_r0, NULL, false);
        CHECK((ev_hs & BLOOM_GP_EVENT_HITSTOP) != 0U);
        (void)bloom_audio_step_frame64(&s7_r0, &s7_a, s7_pcm, 800U);
        int32_t peak_duck = s7_r0.hdr.peak_abs_pcm;
        s7_a.map.instances[hid - 1U].pos_q16[0] = 229376;
        for (uint32_t k = 0U; k < 4U; ++k) {
            (void)bloom_gameplay_step64(&gp, &s7_a, &s7_r0, NULL, false);
        }
        (void)bloom_audio_step_frame64(&s7_r0, &s7_a, s7_pcm, 800U);
        CHECK(s7_r0.hdr.peak_abs_pcm > peak_duck * 2);
        mix_bytes(&gp, sizeof(gp));
        mix_bytes(s7_pcm, sizeof(s7_pcm));

        /* 7e. O(1) 16KB Arena + 576B AudioRig Circular Spacetime Rewind */
        BloomRewindCtrl64 rw;
        bloom_rewind_init64(NULL, 4U);
        bloom_rewind_init64(&rw, 0U);
        CHECK(rw.capacity == 1U);
        bloom_rewind_init64(&rw, 999U);
        CHECK(rw.capacity == BLOOM_REWIND_RING_CAP);
        bloom_rewind_init64(&rw, 4U);
        CHECK(!bloom_rewind_restore(NULL, s7_ring, 0U, &s7_b, &s7_r1));
        CHECK(!bloom_rewind_restore(&rw, s7_ring, 0U, &s7_b, &s7_r1)); /* empty */
        bloom_rewind_push(NULL, s7_ring, &s7_a, &s7_r0);

        for (uint32_t s = 0U; s < 6U; ++s) {
            bloom_rewind_push(&rw, s7_ring, &s7_a, (s == 1U) ? NULL : &s7_r0);
            if (s == 3U) memcpy(&s7_b, &s7_a, sizeof(s7_b));
            bloom_physics_step_arena64(&s7_a, 6554);
        }
        CHECK(!bloom_rewind_restore(&rw, s7_ring, 4U, &s7_b, &s7_r1)); /* >= count */
        CHECK(bloom_rewind_restore(&rw, s7_ring, 2U, &s7_a, NULL));    /* NULL out_audio */
        CHECK(memcmp(&s7_a, &s7_b, sizeof(s7_a)) == 0);
        CHECK(bloom_rewind_restore(&rw, s7_ring, 0U, &s7_a, &s7_r1));
        mix_bytes(&rw, sizeof(rw));
        mix_bytes(&s7_r1, sizeof(s7_r1));
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

    # --- Suite A: Static FPU/SIMD & Indirect-BLR ISA Purity Injections (12 Mutants) ---
    fpu_injections = [
        ("scene", "_bloom_csg_eval_single_q16", "STATIC_FADD_S0", 0x1E202800),
        ("scene", "_bloom_bvh_eval_csg_q16", "STATIC_FMUL_D0", 0x1E600800),
        ("scene", "_bloom_joint_dq64_blend", "STATIC_SCVTF_D0", 0x9E620000),
        ("scene", "_bloom_scene_raycast_ex64", "STATIC_BLR_X8_SCENE", 0xD63F0100),
        ("ui", "_bloom_flex_apply64", "STATIC_FSUB_S0", 0x1E203800),
        ("ui", "_bloom_text_input_insert_char64", "STATIC_FMOV_D0_X0", 0x9E670000),
        ("ui", "_bloom_latex_compile_boxes64", "STATIC_FCVTZS_X0", 0x9E780000),
        ("ui", "_bloom_ui_solve_glass_tracks64", "STATIC_BLR_X8_UI", 0xD63F0100),
        ("prefab", "_bloom_prefab_eval_sdf_q16", "STATIC_UMOV_W0", 0x0E043C00),
        ("prefab", "_bloom_prefab_cull_wave64", "STATIC_INS_V0", 0x4E081C00),
        ("prefab", "_bloom_arena_step_dynamics", "STATIC_DUP_V0", 0x4E080C00),
        ("prefab", "_bloom_arena_heal_and_swap", "STATIC_BLR_X8_PREFAB", 0xD63F0100),
    ]
    for obj_key, fname, mname, bad_op in fpu_injections:
        _, _, insns, funcs = parsed_objs[obj_key]
        lo, hi = funcs[fname]
        anchor = (lo + hi) // 2
        mutants.append(Mutant(mname, "static_isa_purity", obj_key, fname, anchor, insns[anchor], bad_op))

    # --- Suite B: Stratified Pure-GPR Datapath & Predicate Mutants across 61 core functions ---
    target_plan: list[tuple[str, str, int]] = [
        ("scene", "_bloom_smin_q16", 2),
        ("scene", "_bloom_rot_inv_euler_q16", 2),
        ("scene", "_bloom_section_init64", 2),
        ("scene", "_bloom_section_eval_clip_q16", 3),
        ("scene", "_bloom_tri64_init", 2),
        ("scene", "_bloom_tri64_eval_ud_q16", 3),
        ("scene", "_bloom_bvh_build_from_tris64", 3),
        ("scene", "_bloom_mesh_eval_sdf_q16", 4),
        ("scene", "_bloom_stl_parse_to_mesh64", 3),
        ("scene", "_bloom_glb_parse_to_mesh64", 3),
        ("scene", "_bloom_glb_parse_to_csg64", 3),
        ("scene", "_bloom_csg_eval_single_q16", 3),
        ("scene", "_bloom_csg_eval_program_q16", 2),
        ("scene", "_bloom_bvh_build_from_csg64", 3),
        ("scene", "_bloom_bvh_eval_csg_q16", 3),
        ("scene", "_bloom_joint_dq64_make", 2),
        ("scene", "_bloom_joint_dq64_blend", 2),
        ("scene", "_bloom_joint_dq64_transform_q16", 2),
        ("scene", "_bloom_skeleton_eval_sdf_q16", 2),
        ("scene", "_bloom_sdf_brick64_pack", 2),
        ("scene", "_bloom_sdf_brick64_sample_q16", 2),
        ("scene", "_bloom_sdf_brick64_eval_world_q16", 2),
        ("scene", "_bloom_matter_eval_radiance_q16", 2),
        ("scene", "_bloom_matter_parse_hex128", 2),
        ("scene", "_bloom_scene_raycast_ex64", 4),
        ("ui", "_bloom_svg_compile_text", 3),
        ("ui", "_bloom_flex_apply64", 3),
        ("ui", "_bloom_ui_synthesize_bento64", 3),
        ("ui", "_bloom_ui_step_pointer_arena", 3),
        ("ui", "_bloom_ui_solve_glass_tracks64", 3),
        ("ui", "_bloom_latex_compile_boxes64", 3),
        ("ui", "_bloom_latex_eval_box_sdf_q16", 2),
        ("ui", "_bloom_text_input_insert_char64", 3),
        ("ui", "_bloom_text_input_backspace64", 2),
        ("ui", "_bloom_ime_set_preedit64", 2),
        ("ui", "_bloom_ime_commit64", 2),
        ("prefab", "_bloom_prefab_eval_sdf_q16", 3),
        ("prefab", "_bloom_style_resolve_thermal", 2),
        ("prefab", "_bloom_prefab_cull_wave64", 2),
        ("prefab", "_bloom_swept_sphere_ccd_q16", 3),
        ("prefab", "_bloom_scene_eval_pair_clearance_q16", 3),
        ("prefab", "_bloom_physics_step_arena64", 4),
        ("prefab", "_bloom_arena_heal_and_swap", 4),
        ("prefab", "_bloom_arena_step_dynamics", 3),
        ("prefab", "_bloom_image_verify_arena", 2),
        ("scene", "_bloom_scene_eval_jet2_q16", 4),
        ("prefab", "_bloom_inst_set_vel_q16", 2),
        ("prefab", "_bloom_inst_get_vel_q16", 2),
        ("prefab", "_bloom_audio_init_rig", 2),
        ("prefab", "_bloom_audio_excite_impact64", 4),
        ("prefab", "_bloom_audio_step_frame64", 4),
        ("scene", "_bloom_camera_eval_coc_q16", 2),
        ("scene", "_bloom_camera_step_cine64", 3),
        ("scene", "_bloom_metric_eval_scale64", 3),
        ("scene", "_bloom_moodboard_extract_rgb24", 4),
        ("scene", "_bloom_moodboard_compile_to_ssot64", 3),
        ("prefab", "_bloom_gameplay_init64", 2),
        ("prefab", "_bloom_gameplay_step64", 4),
        ("prefab", "_bloom_rewind_init64", 2),
        ("prefab", "_bloom_rewind_push", 2),
        ("prefab", "_bloom_rewind_restore", 3),
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
                f"-I{root / 'build'}",
                "-isystem",
                str(flow_dir / "core"),
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
        print("  BLOOM v9.0.0 — Mach-O ARM64 Binary Sabotage & Purity Audit (Zero-SDL3 Probe)")
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
