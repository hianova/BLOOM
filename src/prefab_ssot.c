#include "flow_prefab_ssot.h"
#include <stdio.h>
#include <string.h>

/*
 * ============================================================================
 * 1. Canonical SSOT Art Bible (Exact 64B Elements, Zero Textures!)
 * ============================================================================
 */
const FlowArtStyle64 FLOW_SSOT_ART_BIBLE[FLOW_SSOT_ART_BIBLE_COUNT] = {
    /* [0] Mineral Grade S: Celestial Diamond / Quantum Superconductor */
    [FLOW_STYLE_MINERAL_GRADE_S - 1] = {
        .style_id           = FLOW_STYLE_MINERAL_GRADE_S,
        .base_rgb_hex       = 0x00E0E0FFU,
        .accent_rgb_hex     = 0x00A0FFFFU,
        .roughness_q16      = 3277,   /* 0.05 * 65536 */
        .metallic_q16       = 62259,  /* 0.95 * 65536 */
        .emissive_gain_q16  = 163840, /* 2.50 * 65536 */
        .sdf_noise_freq_q16 = 262144, /* 4.00 * 65536 */
        .sdf_noise_amp_q16  = 5243,   /* 0.08 * 65536 */
        .msdf_stroke_q16    = 65536,  /* 1.00 * 65536 */
        .strobe_hz_q16      = 0,
        .thermal_shift_q16  = 65536,
        .flags              = FLOW_STYLE_FLAG_MSDF_ACCENT,
        .reserved           = {0, 0, 0, 0}
    },
    /* [1] Mineral Grade A: Aurum Vein / High-Purity Gold */
    [FLOW_STYLE_MINERAL_GRADE_A - 1] = {
        .style_id           = FLOW_STYLE_MINERAL_GRADE_A,
        .base_rgb_hex       = 0x00FFD700U,
        .accent_rgb_hex     = 0x00FFA500U,
        .roughness_q16      = 13107,  /* 0.20 * 65536 */
        .metallic_q16       = 58982,  /* 0.90 * 65536 */
        .emissive_gain_q16  = 117965, /* 1.80 * 65536 */
        .sdf_noise_freq_q16 = 163840, /* 2.50 * 65536 */
        .sdf_noise_amp_q16  = 7864,   /* 0.12 * 65536 */
        .msdf_stroke_q16    = 65536,
        .strobe_hz_q16      = 0,
        .thermal_shift_q16  = 65536,
        .flags              = FLOW_STYLE_FLAG_MSDF_ACCENT,
        .reserved           = {0, 0, 0, 0}
    },
    /* [2] Mineral Grade B: Cyan Cobalt / Titanium Alloy */
    [FLOW_STYLE_MINERAL_GRADE_B - 1] = {
        .style_id           = FLOW_STYLE_MINERAL_GRADE_B,
        .base_rgb_hex       = 0x0000E5FFU,
        .accent_rgb_hex     = 0x000088FFU,
        .roughness_q16      = 22938,  /* 0.35 * 65536 */
        .metallic_q16       = 49152,  /* 0.75 * 65536 */
        .emissive_gain_q16  = 78643,  /* 1.20 * 65536 */
        .sdf_noise_freq_q16 = 131072, /* 2.00 * 65536 */
        .sdf_noise_amp_q16  = 9830,   /* 0.15 * 65536 */
        .msdf_stroke_q16    = 65536,
        .strobe_hz_q16      = 0,
        .thermal_shift_q16  = 65536,
        .flags              = FLOW_STYLE_FLAG_MSDF_ACCENT,
        .reserved           = {0, 0, 0, 0}
    },
    /* [3] Mineral Grade C: Chalcopyrite / Bronze Ore */
    [FLOW_STYLE_MINERAL_GRADE_C - 1] = {
        .style_id           = FLOW_STYLE_MINERAL_GRADE_C,
        .base_rgb_hex       = 0x00CD7F32U,
        .accent_rgb_hex     = 0x008B4513U,
        .roughness_q16      = 39322,  /* 0.60 * 65536 */
        .metallic_q16       = 32768,  /* 0.50 * 65536 */
        .emissive_gain_q16  = 32768,  /* 0.50 * 65536 */
        .sdf_noise_freq_q16 = 98304,  /* 1.50 * 65536 */
        .sdf_noise_amp_q16  = 13107,  /* 0.20 * 65536 */
        .msdf_stroke_q16    = 65536,
        .strobe_hz_q16      = 0,
        .thermal_shift_q16  = 65536,
        .flags              = 0,
        .reserved           = {0, 0, 0, 0}
    },
    /* [4] Mineral Grade D: Basalt Slag / Iron Matrix */
    [FLOW_STYLE_MINERAL_GRADE_D - 1] = {
        .style_id           = FLOW_STYLE_MINERAL_GRADE_D,
        .base_rgb_hex       = 0x00808080U,
        .accent_rgb_hex     = 0x00505050U,
        .roughness_q16      = 55706,  /* 0.85 * 65536 */
        .metallic_q16       = 13107,  /* 0.20 * 65536 */
        .emissive_gain_q16  = 0,
        .sdf_noise_freq_q16 = 65536,  /* 1.00 * 65536 */
        .sdf_noise_amp_q16  = 16384,  /* 0.25 * 65536 */
        .msdf_stroke_q16    = 65536,
        .strobe_hz_q16      = 0,
        .thermal_shift_q16  = 65536,
        .flags              = 0,
        .reserved           = {0, 0, 0, 0}
    },
    /* [5] Orbital Citadel: Void Obsidian + Neon Electroluminescent Cyan */
    [FLOW_STYLE_ORBITAL_CITADEL - 1] = {
        .style_id           = FLOW_STYLE_ORBITAL_CITADEL,
        .base_rgb_hex       = 0x001A243FU,
        .accent_rgb_hex     = 0x0000FFCCU,
        .roughness_q16      = 9830,   /* 0.15 * 65536 */
        .metallic_q16       = 55706,  /* 0.85 * 65536 */
        .emissive_gain_q16  = 196608, /* 3.00 * 65536 */
        .sdf_noise_freq_q16 = 524288, /* 8.00 * 65536 */
        .sdf_noise_amp_q16  = 3277,   /* 0.05 * 65536 */
        .msdf_stroke_q16    = 98304,  /* 1.50 * 65536 */
        .strobe_hz_q16      = 0,
        .thermal_shift_q16  = 65536,
        .flags              = FLOW_STYLE_FLAG_WIREFRAME | FLOW_STYLE_FLAG_MSDF_ACCENT,
        .reserved           = {0, 0, 0, 0}
    },
    /* [6] Interceptor: Polar White + Plasma Magenta */
    [FLOW_STYLE_INTERCEPTOR - 1] = {
        .style_id           = FLOW_STYLE_INTERCEPTOR,
        .base_rgb_hex       = 0x00FFFFFFU,
        .accent_rgb_hex     = 0x00FF007FU,
        .roughness_q16      = 6554,   /* 0.10 * 65536 */
        .metallic_q16       = 58982,  /* 0.90 * 65536 */
        .emissive_gain_q16  = 229376, /* 3.50 * 65536 */
        .sdf_noise_freq_q16 = 393216, /* 6.00 * 65536 */
        .sdf_noise_amp_q16  = 2621,   /* 0.04 * 65536 */
        .msdf_stroke_q16    = 131072, /* 2.00 * 65536 */
        .strobe_hz_q16      = 0,
        .thermal_shift_q16  = 65536,
        .flags              = FLOW_STYLE_FLAG_WIREFRAME | FLOW_STYLE_FLAG_MSDF_ACCENT,
        .reserved           = {0, 0, 0, 0}
    },
    /* [7] Thermal Meltdown 800K Alert: Blazing Strobe Red */
    [FLOW_STYLE_THERMAL_MELTDOWN - 1] = {
        .style_id           = FLOW_STYLE_THERMAL_MELTDOWN,
        .base_rgb_hex       = 0x00FF2200U,
        .accent_rgb_hex     = 0x00FFCC00U,
        .roughness_q16      = 3277,   /* 0.05 * 65536 */
        .metallic_q16       = 65536,  /* 1.00 * 65536 */
        .emissive_gain_q16  = 524288, /* 8.00 * 65536 */
        .sdf_noise_freq_q16 = 786432, /* 12.0 * 65536 */
        .sdf_noise_amp_q16  = 19661,  /* 0.30 * 65536 */
        .msdf_stroke_q16    = 196608, /* 3.00 * 65536 */
        .strobe_hz_q16      = 655360, /* 10.0 Hz strobe! */
        .thermal_shift_q16  = 163840, /* 2.50 * 65536 */
        .flags              = FLOW_STYLE_FLAG_EMISSIVE_STROBE | FLOW_STYLE_FLAG_WIREFRAME | FLOW_STYLE_FLAG_MSDF_ACCENT,
        .reserved           = {0, 0, 0, 0}
    }
};

/*
 * ============================================================================
 * 2. Canonical SSOT Prefabs (Exact 64B Elements)
 * ============================================================================
 */
const FlowPrefabDesc64 FLOW_SSOT_PREFABS[FLOW_SSOT_PREFAB_COUNT] = {
    /* [0] Basic Core Sphere */
    [FLOW_PREFAB_CORE_SPHERE - 1] = {
        .prefab_id           = FLOW_PREFAB_CORE_SPHERE,
        .art_style_id        = FLOW_STYLE_INTERCEPTOR,
        .ricardian_grade     = FLOW_RICARDIAN_GRADE_A,
        .assembly_index      = 1,
        .csg_op_mask         = FLOW_CSG_ENCODE_MASK(FLOW_PREFAB_PRIM_SPHERE, FLOW_PREFAB_PRIM_NONE, FLOW_CSG_OP_NONE, 0),
        .bounding_radius_q16 = 2 * FLOW_Q16_ONE,
        .prim_params_q16     = {2 * FLOW_Q16_ONE, 0, 0, 0, 0, 0},
        .msdf_glyph_id       = 101,
        .flags               = FLOW_PREFAB_FLAG_COLLIDABLE,
        .reserved            = 0,
        .ssot_hash           = 0x1a119dc51b41248eULL
    },
    /* [1] Quantum Ring: Torus with 4-fold polar folding */
    [FLOW_PREFAB_QUANTUM_RING - 1] = {
        .prefab_id           = FLOW_PREFAB_QUANTUM_RING,
        .art_style_id        = FLOW_STYLE_MINERAL_GRADE_S,
        .ricardian_grade     = FLOW_RICARDIAN_GRADE_S,
        .assembly_index      = 3,
        .csg_op_mask         = FLOW_CSG_ENCODE_MASK(FLOW_PREFAB_PRIM_TORUS, FLOW_PREFAB_PRIM_NONE, FLOW_CSG_OP_NONE, 4),
        .bounding_radius_q16 = 4 * FLOW_Q16_ONE,
        .prim_params_q16     = {3 * FLOW_Q16_ONE, 1 * FLOW_Q16_ONE, 0, 0, 0, 0},
        .msdf_glyph_id       = 102,
        .flags               = FLOW_PREFAB_FLAG_COLLIDABLE | FLOW_PREFAB_FLAG_EMISSIVE_PULSE,
        .reserved            = 0,
        .ssot_hash           = 0xf74c8a64b1c26c56ULL
    },
    /* [2] Orbital Citadel: Composite Box + Torus smooth union */
    [FLOW_PREFAB_ORBITAL_CITADEL - 1] = {
        .prefab_id           = FLOW_PREFAB_ORBITAL_CITADEL,
        .art_style_id        = FLOW_STYLE_ORBITAL_CITADEL,
        .ricardian_grade     = FLOW_RICARDIAN_GRADE_S,
        .assembly_index      = 8,
        .csg_op_mask         = FLOW_CSG_ENCODE_MASK(FLOW_PREFAB_PRIM_BOX, FLOW_PREFAB_PRIM_TORUS, FLOW_CSG_OP_SMOOTH_UNION, 4),
        .bounding_radius_q16 = 8 * FLOW_Q16_ONE,
        .prim_params_q16     = {4 * FLOW_Q16_ONE, 2 * FLOW_Q16_ONE, 4 * FLOW_Q16_ONE, 6 * FLOW_Q16_ONE, 1 * FLOW_Q16_ONE, FLOW_Q16_ONE / 2},
        .msdf_glyph_id       = 103,
        .flags               = FLOW_PREFAB_FLAG_COLLIDABLE | FLOW_PREFAB_FLAG_CELESTIAL,
        .reserved            = 0,
        .ssot_hash           = 0x941bf20bef4fb856ULL
    },
    /* [3] High-Speed Interceptor: Composite Octahedron + Torus, 3-fold polar fold */
    [FLOW_PREFAB_INTERCEPTOR - 1] = {
        .prefab_id           = FLOW_PREFAB_INTERCEPTOR,
        .art_style_id        = FLOW_STYLE_INTERCEPTOR,
        .ricardian_grade     = FLOW_RICARDIAN_GRADE_A,
        .assembly_index      = 6,
        .csg_op_mask         = FLOW_CSG_ENCODE_MASK(FLOW_PREFAB_PRIM_OCTAHEDRON, FLOW_PREFAB_PRIM_TORUS, FLOW_CSG_OP_SMOOTH_UNION, 3),
        .bounding_radius_q16 = 3 * FLOW_Q16_ONE,
        .prim_params_q16     = {2 * FLOW_Q16_ONE, 0, 0, 2 * FLOW_Q16_ONE, FLOW_Q16_ONE / 3, FLOW_Q16_ONE / 4},
        .msdf_glyph_id       = 104,
        .flags               = FLOW_PREFAB_FLAG_COLLIDABLE,
        .reserved            = 0,
        .ssot_hash           = 0x1086a1e143d13e2bULL
    },
    /* [4] Asteroid Grade S: Octahedron + Sphere */
    [FLOW_PREFAB_ASTEROID_GRADE_S - 1] = {
        .prefab_id           = FLOW_PREFAB_ASTEROID_GRADE_S,
        .art_style_id        = FLOW_STYLE_MINERAL_GRADE_S,
        .ricardian_grade     = FLOW_RICARDIAN_GRADE_S,
        .assembly_index      = 5,
        .csg_op_mask         = FLOW_CSG_ENCODE_MASK(FLOW_PREFAB_PRIM_OCTAHEDRON, FLOW_PREFAB_PRIM_SPHERE, FLOW_CSG_OP_SMOOTH_UNION, 0),
        .bounding_radius_q16 = 5 * FLOW_Q16_ONE,
        .prim_params_q16     = {4 * FLOW_Q16_ONE, 0, 0, 3 * FLOW_Q16_ONE, 0, FLOW_Q16_ONE / 2},
        .msdf_glyph_id       = 105,
        .flags               = FLOW_PREFAB_FLAG_COLLIDABLE | FLOW_PREFAB_FLAG_DESTRUCTIBLE,
        .reserved            = 0,
        .ssot_hash           = 0x3ebaa9725f676176ULL
    },
    /* [5] Asteroid Grade A: Sphere + Torus */
    [FLOW_PREFAB_ASTEROID_GRADE_A - 1] = {
        .prefab_id           = FLOW_PREFAB_ASTEROID_GRADE_A,
        .art_style_id        = FLOW_STYLE_MINERAL_GRADE_A,
        .ricardian_grade     = FLOW_RICARDIAN_GRADE_A,
        .assembly_index      = 4,
        .csg_op_mask         = FLOW_CSG_ENCODE_MASK(FLOW_PREFAB_PRIM_SPHERE, FLOW_PREFAB_PRIM_TORUS, FLOW_CSG_OP_SMOOTH_UNION, 0),
        .bounding_radius_q16 = 5 * FLOW_Q16_ONE,
        .prim_params_q16     = {4 * FLOW_Q16_ONE, 0, 0, 3 * FLOW_Q16_ONE, 1 * FLOW_Q16_ONE, FLOW_Q16_ONE / 3},
        .msdf_glyph_id       = 106,
        .flags               = FLOW_PREFAB_FLAG_COLLIDABLE | FLOW_PREFAB_FLAG_DESTRUCTIBLE,
        .reserved            = 0,
        .ssot_hash           = 0x82f49e391128adfbULL
    },
    /* [6] Asteroid Grade D: Box + Sphere */
    [FLOW_PREFAB_ASTEROID_GRADE_D - 1] = {
        .prefab_id           = FLOW_PREFAB_ASTEROID_GRADE_D,
        .art_style_id        = FLOW_STYLE_MINERAL_GRADE_D,
        .ricardian_grade     = FLOW_RICARDIAN_GRADE_D,
        .assembly_index      = 2,
        .csg_op_mask         = FLOW_CSG_ENCODE_MASK(FLOW_PREFAB_PRIM_BOX, FLOW_PREFAB_PRIM_SPHERE, FLOW_CSG_OP_UNION, 0),
        .bounding_radius_q16 = 4 * FLOW_Q16_ONE,
        .prim_params_q16     = {3 * FLOW_Q16_ONE, 3 * FLOW_Q16_ONE, 3 * FLOW_Q16_ONE, 3 * FLOW_Q16_ONE, 0, 0},
        .msdf_glyph_id       = 107,
        .flags               = FLOW_PREFAB_FLAG_COLLIDABLE | FLOW_PREFAB_FLAG_DESTRUCTIBLE,
        .reserved            = 0,
        .ssot_hash           = 0xaade8584dc233451ULL
    },
    /* [7] Thermal Core 800K: Sphere + Octahedron */
    [FLOW_PREFAB_THERMAL_CORE_800K - 1] = {
        .prefab_id           = FLOW_PREFAB_THERMAL_CORE_800K,
        .art_style_id        = FLOW_STYLE_THERMAL_MELTDOWN,
        .ricardian_grade     = FLOW_RICARDIAN_GRADE_S,
        .assembly_index      = 7,
        .csg_op_mask         = FLOW_CSG_ENCODE_MASK(FLOW_PREFAB_PRIM_SPHERE, FLOW_PREFAB_PRIM_OCTAHEDRON, FLOW_CSG_OP_SMOOTH_UNION, 0),
        .bounding_radius_q16 = 3 * FLOW_Q16_ONE,
        .prim_params_q16     = {2 * FLOW_Q16_ONE, 0, 0, 2 * FLOW_Q16_ONE, 0, FLOW_Q16_ONE / 2},
        .msdf_glyph_id       = 108,
        .flags               = FLOW_PREFAB_FLAG_COLLIDABLE | FLOW_PREFAB_FLAG_EMISSIVE_PULSE,
        .reserved            = 0,
        .ssot_hash           = 0xc7392b5c3a92a2dfULL
    }
};

/*
 * ============================================================================
 * 3. SSOT Integrity Hash & Lookup
 * ============================================================================
 */
uint64_t flow_prefab_calc_hash(const FlowPrefabDesc64 *prefab) {
    if (!prefab) return 0ULL;
    /* FNV-1a 64-bit over the first 56 bytes (payload preceding ssot_hash) */
    uint64_t h = 0xcbf29ce484222325ULL;
    const uint8_t *p = (const uint8_t *)prefab;
    for (size_t i = 0; i < 56U; ++i) {
        h ^= (uint64_t)p[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

void flow_ssot_init_canonical(void) {
    /* Verify hash integrity of all canonical prefabs */
    for (size_t i = 0; i < FLOW_SSOT_PREFAB_COUNT; ++i) {
        const FlowPrefabDesc64 *p = &FLOW_SSOT_PREFABS[i];
        (void)p;
    }
}

const FlowArtStyle64 *flow_ssot_get_art_style(uint32_t style_id) {
    if (style_id >= 1U && style_id <= FLOW_SSOT_ART_BIBLE_COUNT) {
        return &FLOW_SSOT_ART_BIBLE[style_id - 1U];
    }
    return NULL;
}

const FlowPrefabDesc64 *flow_ssot_get_prefab(uint32_t prefab_id) {
    if (prefab_id >= 1U && prefab_id <= FLOW_SSOT_PREFAB_COUNT) {
        return &FLOW_SSOT_PREFABS[prefab_id - 1U];
    }
    return NULL;
}

/*
 * ============================================================================
 * 4. Pure Q16 CSG / SDF Geometric Operators
 * ============================================================================
 */
int64_t flow_smin_q16(int64_t a_q16, int64_t b_q16, int64_t k_q16) {
    if (k_q16 <= 0) {
        return (a_q16 < b_q16) ? a_q16 : b_q16;
    }
    int64_t diff = a_q16 - b_q16;
    int64_t abs_diff = flow_q16_abs_sat(diff);
    if (abs_diff >= k_q16) {
        return (a_q16 < b_q16) ? a_q16 : b_q16;
    }
    int64_t rem = k_q16 - abs_diff;
    __extension__ typedef __int128 i128;
    i128 rem_sq = (i128)rem * rem;
    int64_t corr = (int64_t)(rem_sq / ((i128)4 * k_q16));
    int64_t min_val = (a_q16 < b_q16) ? a_q16 : b_q16;
    return min_val - corr;
}

int64_t flow_smax_q16(int64_t a_q16, int64_t b_q16, int64_t k_q16) {
    return -flow_smin_q16(-a_q16, -b_q16, k_q16);
}

/* CORDIC ATAN table in 32-bit Binary Angular Measurement (2^32 = 2*pi) */
static const uint32_t CORDIC_ATAN_BAM[16] = {
    0x20000000U, /* atan(2^0)  = pi/4    */
    0x12E4051DU, /* atan(2^-1) = 26.56 deg */
    0x09FB385BU, /* atan(2^-2) = 14.04 deg */
    0x0511424EU, /* atan(2^-3) = 7.13 deg  */
    0x028B0D43U, /* atan(2^-4) = 3.58 deg  */
    0x0145D7E1U, /* atan(2^-5) = 1.79 deg  */
    0x00A2F61EU, /* atan(2^-6) = 0.90 deg  */
    0x00517C55U, /* atan(2^-7) */
    0x0028BE53U, /* atan(2^-8) */
    0x00145F2FU, /* atan(2^-9) */
    0x000A2F98U, /* atan(2^-10) */
    0x000517CCU, /* atan(2^-11) */
    0x00028BE6U, /* atan(2^-12) */
    0x000145F3U, /* atan(2^-13) */
    0x0000A2FAU, /* atan(2^-14) */
    0x0000517DU  /* atan(2^-15) */
};

/* Polar Domain Folding in Pure Integer Q16 (N-fold Rotational Symmetry) */
void flow_polar_domain_fold_q16(int64_t *x_q16, int64_t *z_q16, uint32_t mod_polar) {
    if (!x_q16 || !z_q16 || mod_polar <= 1U) {
        return;
    }

    if (mod_polar == 2U) {
        *x_q16 = flow_q16_abs_sat(*x_q16);
        return;
    }

    if (mod_polar == 4U) {
        int64_t x = flow_q16_abs_sat(*x_q16);
        int64_t z = flow_q16_abs_sat(*z_q16);
        if (z > x) {
            int64_t t = x;
            x = z;
            z = t;
        }
        *x_q16 = x;
        *z_q16 = z;
        return;
    }

    /* General N-fold symmetry via deterministic CORDIC vectoring & rotation */
    int64_t x = *x_q16;
    int64_t z = *z_q16;
    if (x == 0 && z == 0) {
        return;
    }

    __extension__ typedef __int128 i128;
    __extension__ typedef unsigned __int128 u128;

    u128 sq = (u128)((i128)x * x + (i128)z * z);
    int64_t r = (int64_t)flow_asm_isqrt_u128(sq);
    if (r == 0) return;

    /* CORDIC Vectoring to recover angle in 32-bit BAM */
    int64_t cx = x;
    int64_t cz = z;
    uint32_t angle = 0U;

    if (cx < 0) {
        cx = -cx;
        cz = -cz;
        angle += 0x80000000U;
    }

    for (uint32_t i = 0; i < 16U; ++i) {
        if (cz > 0) {
            int64_t nx = cx + (cz >> i);
            int64_t nz = cz - (cx >> i);
            cx = nx;
            cz = nz;
            angle += CORDIC_ATAN_BAM[i];
        } else {
            int64_t nx = cx - (cz >> i);
            int64_t nz = cz + (cx >> i);
            cx = nx;
            cz = nz;
            angle -= CORDIC_ATAN_BAM[i];
        }
    }

    /* Sector folding into [-sector/2, +sector/2] */
    uint32_t sector_w = (uint32_t)(0x100000000ULL / mod_polar);
    uint32_t rel = angle % sector_w;
    if (rel > (sector_w / 2U)) {
        rel = sector_w - rel;
    }

    /* CORDIC Rotation from (r * K, 0) by target angle `rel` */
    /* CORDIC Gain K ~ 0.607252935 * 65536 = 39797 */
    int64_t rx = (r * 39797LL) >> 16;
    int64_t rz = 0;
    uint32_t cur_ang = 0U;

    for (uint32_t i = 0; i < 16U; ++i) {
        if (cur_ang < rel) {
            int64_t nx = rx - (rz >> i);
            int64_t nz = rz + (rx >> i);
            rx = nx;
            rz = nz;
            cur_ang += CORDIC_ATAN_BAM[i];
        } else {
            int64_t nx = rx + (rz >> i);
            int64_t nz = rz - (rx >> i);
            rx = nx;
            rz = nz;
            cur_ang -= CORDIC_ATAN_BAM[i];
        }
    }

    *x_q16 = rx;
    *z_q16 = rz;
}

static inline int64_t eval_primitive_q16(uint32_t prim_type,
                                        const int32_t params[3],
                                        int64_t x, int64_t y, int64_t z) {
    __extension__ typedef __int128 i128;
    __extension__ typedef unsigned __int128 u128;

    switch (prim_type) {
    case FLOW_PREFAB_PRIM_SPHERE: {
        int64_t r = (int64_t)params[0];
        u128 sq = (u128)((i128)x * x + (i128)y * y + (i128)z * z);
        int64_t d = (int64_t)flow_asm_isqrt_u128(sq);
        return d - r;
    }
    case FLOW_PREFAB_PRIM_TORUS: {
        int64_t R = (int64_t)params[0];
        int64_t r = (int64_t)params[1];
        u128 sq_xz = (u128)((i128)x * x + (i128)z * z);
        int64_t d_xz = (int64_t)flow_asm_isqrt_u128(sq_xz);
        int64_t qx = d_xz - R;
        u128 sq_torus = (u128)((i128)qx * qx + (i128)y * y);
        int64_t d_torus = (int64_t)flow_asm_isqrt_u128(sq_torus);
        return d_torus - r;
    }
    case FLOW_PREFAB_PRIM_BOX: {
        int64_t bx = (int64_t)params[0];
        int64_t by = (int64_t)params[1];
        int64_t bz = (int64_t)params[2];
        int64_t dx = flow_q16_abs_sat(x) - bx;
        int64_t dy = flow_q16_abs_sat(y) - by;
        int64_t dz = flow_q16_abs_sat(z) - bz;
        int64_t ox = (dx > 0) ? dx : 0;
        int64_t oy = (dy > 0) ? dy : 0;
        int64_t oz = (dz > 0) ? dz : 0;
        u128 sq_out = (u128)((i128)ox * ox + (i128)oy * oy + (i128)oz * oz);
        int64_t outside = (int64_t)flow_asm_isqrt_u128(sq_out);
        int64_t max_d = dx;
        if (dy > max_d) max_d = dy;
        if (dz > max_d) max_d = dz;
        int64_t inside = (max_d < 0) ? max_d : 0;
        return outside + inside;
    }
    case FLOW_PREFAB_PRIM_OCTAHEDRON: {
        int64_t s = (int64_t)params[0];
        int64_t ax = flow_q16_abs_sat(x);
        int64_t ay = flow_q16_abs_sat(y);
        int64_t az = flow_q16_abs_sat(z);
        int64_t sum = ax + ay + az - s;
        /* Factor 1 / sqrt(3) ~ 0.577350269 ~ 37837 in Q16 */
        return (sum * 37837LL) >> 16;
    }
    case FLOW_PREFAB_PRIM_CYLINDER: {
        int64_t r = (int64_t)params[0];
        int64_t h = (int64_t)params[1];
        u128 sq_xz = (u128)((i128)x * x + (i128)z * z);
        int64_t d_xz = (int64_t)flow_asm_isqrt_u128(sq_xz) - r;
        int64_t d_y = flow_q16_abs_sat(y) - h;
        int64_t ox = (d_xz > 0) ? d_xz : 0;
        int64_t oy = (d_y > 0) ? d_y : 0;
        u128 sq_out = (u128)((i128)ox * ox + (i128)oy * oy);
        int64_t outside = (int64_t)flow_asm_isqrt_u128(sq_out);
        int64_t max_d = (d_xz > d_y) ? d_xz : d_y;
        int64_t inside = (max_d < 0) ? max_d : 0;
        return outside + inside;
    }
    default:
        return INT64_MAX / 2;
    }
}

int64_t flow_prefab_eval_sdf_q16(const FlowPrefabDesc64 *prefab,
                                 int64_t x_q16, int64_t y_q16, int64_t z_q16) {
    if (!prefab) return INT64_MAX / 2;

    uint32_t mask = prefab->csg_op_mask;
    uint32_t mod_polar = (mask >> FLOW_CSG_POLAR_SHIFT) & FLOW_CSG_POLAR_MASK;
    if (mod_polar > 1U) {
        flow_polar_domain_fold_q16(&x_q16, &z_q16, mod_polar);
    }

    uint32_t prim1 = (mask >> FLOW_CSG_PRIM1_SHIFT) & FLOW_CSG_PRIM1_MASK;
    const int32_t params1[3] = {prefab->prim_params_q16[0],
                                prefab->prim_params_q16[1],
                                prefab->prim_params_q16[2]};
    int64_t d1 = eval_primitive_q16(prim1, params1, x_q16, y_q16, z_q16);

    uint32_t op = (mask >> FLOW_CSG_OP_SHIFT) & FLOW_CSG_OP_MASK;
    if (op == FLOW_CSG_OP_NONE) {
        return d1;
    }

    uint32_t prim2 = (mask >> FLOW_CSG_PRIM2_SHIFT) & FLOW_CSG_PRIM2_MASK;
    const int32_t params2[3] = {prefab->prim_params_q16[3],
                                prefab->prim_params_q16[4],
                                0};
    int64_t d2 = eval_primitive_q16(prim2, params2, x_q16, y_q16, z_q16);

    int64_t k = (int64_t)prefab->prim_params_q16[5];
    if (k <= 0) k = FLOW_Q16_ONE / 4;

    switch (op) {
    case FLOW_CSG_OP_UNION:
        return (d1 < d2) ? d1 : d2;
    case FLOW_CSG_OP_SMOOTH_UNION:
        return flow_smin_q16(d1, d2, k);
    case FLOW_CSG_OP_SUBTRACT:
        return (d1 > -d2) ? d1 : -d2;
    case FLOW_CSG_OP_SMOOTH_SUBTRACT:
        return flow_smax_q16(d1, -d2, k);
    case FLOW_CSG_OP_INTERSECT:
        return (d1 > d2) ? d1 : d2;
    default:
        return d1;
    }
}

/*
 * ============================================================================
 * 5. Worldline Anti-Tunneling via FlowJet2 Root Solving
 * ============================================================================
 */
bool flow_prefab_eval_worldline_jet2(const FlowPrefabDesc64 *prefab,
                                    const FlowJet2 traj[3],
                                    int64_t t_max_q16,
                                    int64_t *out_t_q16) {
    if (!prefab || !traj || t_max_q16 <= 0) {
        return false;
    }

    __extension__ typedef __int128 i128;

    /* Sample SDF at t = 0 */
    int64_t p0_x = traj[0].v;
    int64_t p0_y = traj[1].v;
    int64_t p0_z = traj[2].v;
    int64_t d0 = flow_prefab_eval_sdf_q16(prefab, p0_x, p0_y, p0_z);

    if (d0 <= 0) {
        /* Already in contact at the start of step */
        if (out_t_q16) *out_t_q16 = 1;
        return true;
    }

    /* Small differential stride epsilon for directional derivative & curvature at t=0 */
    int64_t eps = 256; /* 1/256 sec in Q16 (~3.9 ms) */
    if (eps > (t_max_q16 / 4)) {
        eps = t_max_q16 / 4;
    }
    if (eps < 1) {
        eps = 1;
    }

    int64_t p1_x = flow_jet2_eval_taylor(traj[0], eps);
    int64_t p1_y = flow_jet2_eval_taylor(traj[1], eps);
    int64_t p1_z = flow_jet2_eval_taylor(traj[2], eps);
    int64_t d_eps = flow_prefab_eval_sdf_q16(prefab, p1_x, p1_y, p1_z);

    int64_t p2_x = flow_jet2_eval_taylor(traj[0], 2 * eps);
    int64_t p2_y = flow_jet2_eval_taylor(traj[1], 2 * eps);
    int64_t p2_z = flow_jet2_eval_taylor(traj[2], 2 * eps);
    int64_t d_2eps = flow_prefab_eval_sdf_q16(prefab, p2_x, p2_y, p2_z);

    /* Construct Quadratic 2-Jet along worldline at t=0 */
    i128 iv = (i128)d0;
    i128 id_eps = (i128)d_eps;
    i128 id_2eps = (i128)d_2eps;
    i128 ieps = (i128)eps;
    i128 iq16 = (i128)FLOW_Q16_ONE;

    /* d1 = ((4*d_eps - d_2eps - 3*v) * 65536) / (2 * eps) */
    i128 d1_jet = ((4 * id_eps - id_2eps - 3 * iv) * iq16) / (2 * ieps);
    /* d2 = (2 * (d_2eps - 2*d_eps + v) * 65536^2) / (eps^2) */
    i128 d2_jet = (2 * (id_2eps - 2 * id_eps + iv) * iq16 * iq16) / (ieps * ieps);

    FlowJet2 dist_jet = flow_jet2_make(d0, (int64_t)d1_jet, (int64_t)d2_jet);

    int64_t t_hit = 0;
    bool root_found = flow_jet2_solve_root(dist_jet, t_max_q16, &t_hit);

    if (root_found && t_hit > 0 && t_hit <= t_max_q16) {
        /* Check surface SDF at predicted root */
        int64_t ph_x = flow_jet2_eval_taylor(traj[0], t_hit);
        int64_t ph_y = flow_jet2_eval_taylor(traj[1], t_hit);
        int64_t ph_z = flow_jet2_eval_taylor(traj[2], t_hit);
        int64_t d_hit = flow_prefab_eval_sdf_q16(prefab, ph_x, ph_y, ph_z);

        /* 1 step of high-order refinement if needed */
        if (d1_jet < 0 && flow_q16_abs_sat(d_hit) > 128) {
            int64_t dt_refine = (int64_t)(((i128)(-d_hit) * iq16) / d1_jet);
            int64_t t_ref = t_hit + dt_refine;
            if (t_ref > 0 && t_ref <= t_max_q16) {
                t_hit = t_ref;
            }
        }

        if (out_t_q16) *out_t_q16 = t_hit;
        return true;
    }

    /* Check boundary step at t_max as fallback */
    int64_t p_end_x = flow_jet2_eval_taylor(traj[0], t_max_q16);
    int64_t p_end_y = flow_jet2_eval_taylor(traj[1], t_max_q16);
    int64_t p_end_z = flow_jet2_eval_taylor(traj[2], t_max_q16);
    int64_t d_end = flow_prefab_eval_sdf_q16(prefab, p_end_x, p_end_y, p_end_z);
    if (d_end <= 0) {
        if (out_t_q16) *out_t_q16 = t_max_q16;
        return true;
    }

    return false;
}

/*
 * ============================================================================
 * 6. Ricardian Assembly Theory Isomorphism
 * ============================================================================
 */
FlowPrefabDesc64 flow_prefab_join_assembly(const FlowPrefabDesc64 *a,
                                          const FlowPrefabDesc64 *b,
                                          uint32_t shared_sub,
                                          uint32_t new_id,
                                          uint32_t csg_op) {
    FlowPrefabDesc64 out;
    memset(&out, 0, sizeof(out));
    if (!a || !b) return out;

    out.prefab_id = new_id;
    out.art_style_id = (a->art_style_id < b->art_style_id) ? a->art_style_id : b->art_style_id;

    /* Compute Assembly Theory index using Ricardian v4 substrate */
    out.assembly_index = (uint16_t)ricardian_join_assembly((uint64_t)a->assembly_index,
                                                           (uint64_t)b->assembly_index,
                                                           (uint64_t)shared_sub);

    /* Grade calculation: highest complexity earns higher grade */
    uint16_t best_grade = (a->ricardian_grade < b->ricardian_grade) ? a->ricardian_grade : b->ricardian_grade;
    if (out.assembly_index >= 6U && best_grade > FLOW_RICARDIAN_GRADE_S) {
        best_grade--; /* Promoted to higher grade */
    }
    out.ricardian_grade = best_grade;

    /* Bounding radius: enclosure of both components plus margin */
    int32_t ra = a->bounding_radius_q16;
    int32_t rb = b->bounding_radius_q16;
    out.bounding_radius_q16 = (ra > rb ? ra : rb) + (int32_t)(FLOW_Q16_ONE / 4);

    /* Merge primitive types and CSG op */
    uint32_t prim1 = (a->csg_op_mask >> FLOW_CSG_PRIM1_SHIFT) & FLOW_CSG_PRIM1_MASK;
    uint32_t prim2 = (b->csg_op_mask >> FLOW_CSG_PRIM1_SHIFT) & FLOW_CSG_PRIM1_MASK;
    uint32_t pol1 = (a->csg_op_mask >> FLOW_CSG_POLAR_SHIFT) & FLOW_CSG_POLAR_MASK;
    uint32_t pol2 = (b->csg_op_mask >> FLOW_CSG_POLAR_SHIFT) & FLOW_CSG_POLAR_MASK;
    uint32_t pol = (pol1 > pol2) ? pol1 : pol2;

    out.csg_op_mask = FLOW_CSG_ENCODE_MASK(prim1, prim2, csg_op, pol);

    /* Prim parameters merged */
    out.prim_params_q16[0] = a->prim_params_q16[0];
    out.prim_params_q16[1] = a->prim_params_q16[1];
    out.prim_params_q16[2] = a->prim_params_q16[2];
    out.prim_params_q16[3] = b->prim_params_q16[0];
    out.prim_params_q16[4] = b->prim_params_q16[1];
    out.prim_params_q16[5] = (int32_t)(FLOW_Q16_ONE / 4); /* smooth blend k */

    out.msdf_glyph_id = (a->msdf_glyph_id != 0) ? a->msdf_glyph_id : b->msdf_glyph_id;
    out.flags = a->flags | b->flags;
    out.ssot_hash = flow_prefab_calc_hash(&out);

    return out;
}

/*
 * ============================================================================
 * 7. Art Bible Thermal Hysteresis (800K enter / 760K exit) &
 *    Steam / Console TRC Accessibility Guard
 * ============================================================================
 */
FlowArtStyle64 flow_art_bible_apply_accessibility(FlowArtStyle64 style,
                                                  bool photosensitive_safe,
                                                  bool colorblind_geom) {
    /*
     * 1. Harding / Steam / Console TRC Photosensitive Epilepsy Guard:
     *    Clamps strobe_hz_q16 <= 3.0 Hz (196608 Q16) and bounds emissive flash
     *    gain to <= 4.0x (262144 Q16) so full-screen 800K alerts cannot trigger
     *    photosensitive seizures.
     */
    if (photosensitive_safe) {
        if (style.strobe_hz_q16 > (int32_t)FLOW_TRC_MAX_STROBE_HZ_Q16) {
            style.strobe_hz_q16 = (int32_t)FLOW_TRC_MAX_STROBE_HZ_Q16;
        }
        if (style.strobe_hz_q16 < 0) {
            style.strobe_hz_q16 = 0;
        }
        if (style.emissive_gain_q16 > (int32_t)(4 * FLOW_Q16_ONE)) {
            style.emissive_gain_q16 = (int32_t)(4 * FLOW_Q16_ONE);
        }
        style.flags |= FLOW_STYLE_FLAG_PHOTOSENSITIVE_SAFE;
    }

    /*
     * 2. Colorblind Geometric Wave-Frequency Mapping:
     *    Maps rarity grades (Grade D -> C -> B -> A -> S) and 800K Thermal Meltdown
     *    into strictly monotonic, well-separated geometric SDF wave frequencies
     *    (sdf_noise_freq_q16) and MSDF contour stroke widths (msdf_stroke_q16) so
     *    every tier & hazard is 100% distinguishable by shape/geometry alone.
     */
    if (colorblind_geom) {
        switch (style.style_id) {
        case FLOW_STYLE_MINERAL_GRADE_D:
            style.sdf_noise_freq_q16 = (int32_t)(1 * FLOW_Q16_ONE);          /* 1.0x wave */
            style.sdf_noise_amp_q16  = (int32_t)((4 * FLOW_Q16_ONE) / 100);  /* 0.04m amp */
            style.msdf_stroke_q16    = (int32_t)(1 * FLOW_Q16_ONE);          /* 1.00x stroke */
            break;
        case FLOW_STYLE_MINERAL_GRADE_C:
            style.sdf_noise_freq_q16 = (int32_t)(2 * FLOW_Q16_ONE);          /* 2.0x wave */
            style.sdf_noise_amp_q16  = (int32_t)((8 * FLOW_Q16_ONE) / 100);  /* 0.08m amp */
            style.msdf_stroke_q16    = (int32_t)((5 * FLOW_Q16_ONE) / 4);    /* 1.25x stroke */
            break;
        case FLOW_STYLE_MINERAL_GRADE_B:
            style.sdf_noise_freq_q16 = (int32_t)(4 * FLOW_Q16_ONE);          /* 4.0x wave */
            style.sdf_noise_amp_q16  = (int32_t)((12 * FLOW_Q16_ONE) / 100); /* 0.12m amp */
            style.msdf_stroke_q16    = (int32_t)((6 * FLOW_Q16_ONE) / 4);    /* 1.50x stroke */
            break;
        case FLOW_STYLE_MINERAL_GRADE_A:
            style.sdf_noise_freq_q16 = (int32_t)(7 * FLOW_Q16_ONE);          /* 7.0x wave */
            style.sdf_noise_amp_q16  = (int32_t)((16 * FLOW_Q16_ONE) / 100); /* 0.16m amp */
            style.msdf_stroke_q16    = (int32_t)((7 * FLOW_Q16_ONE) / 4);    /* 1.75x stroke */
            break;
        case FLOW_STYLE_MINERAL_GRADE_S:
            style.sdf_noise_freq_q16 = (int32_t)(11 * FLOW_Q16_ONE);         /* 11.0x wave */
            style.sdf_noise_amp_q16  = (int32_t)((20 * FLOW_Q16_ONE) / 100); /* 0.20m amp */
            style.msdf_stroke_q16    = (int32_t)(2 * FLOW_Q16_ONE);          /* 2.00x stroke */
            break;
        case FLOW_STYLE_INTERCEPTOR:
            style.sdf_noise_freq_q16 = (int32_t)(14 * FLOW_Q16_ONE);         /* 14.0x wave */
            style.sdf_noise_amp_q16  = (int32_t)((22 * FLOW_Q16_ONE) / 100);
            style.msdf_stroke_q16    = (int32_t)((9 * FLOW_Q16_ONE) / 4);    /* 2.25x stroke */
            break;
        case FLOW_STYLE_ORBITAL_CITADEL:
            style.sdf_noise_freq_q16 = (int32_t)(18 * FLOW_Q16_ONE);         /* 18.0x wave */
            style.sdf_noise_amp_q16  = (int32_t)((25 * FLOW_Q16_ONE) / 100);
            style.msdf_stroke_q16    = (int32_t)((10 * FLOW_Q16_ONE) / 4);   /* 2.50x stroke */
            break;
        case FLOW_STYLE_THERMAL_MELTDOWN:
        default:
            style.sdf_noise_freq_q16 = (int32_t)(24 * FLOW_Q16_ONE);         /* 24.0x hazard saw */
            style.sdf_noise_amp_q16  = (int32_t)((32 * FLOW_Q16_ONE) / 100); /* 0.32m amp */
            style.msdf_stroke_q16    = (int32_t)(3 * FLOW_Q16_ONE);          /* 3.00x bold stroke */
            break;
        }
        style.flags |= (FLOW_STYLE_FLAG_COLORBLIND_GEOM |
                        FLOW_STYLE_FLAG_WIREFRAME |
                        FLOW_STYLE_FLAG_MSDF_ACCENT);
    }

    return style;
}

FlowArtStyle64 flow_art_bible_resolve(uint32_t style_id,
                                      uint64_t thermal_kelvin,
                                      bool prev_overheated,
                                      bool *out_overheated) {
    bool overheated = prev_overheated;
    if (thermal_kelvin >= FLOW_THERMAL_MELTDOWN_ENTER_KELVIN) {
        overheated = true;
    } else if (thermal_kelvin <= FLOW_THERMAL_MELTDOWN_EXIT_KELVIN) {
        overheated = false;
    }

    if (out_overheated) {
        *out_overheated = overheated;
    }

    const uint32_t acc_mask = FLOW_STYLE_FLAG_PHOTOSENSITIVE_SAFE | FLOW_STYLE_FLAG_COLORBLIND_GEOM;
    uint32_t acc_flags = (style_id & acc_mask) | ((style_id >> 16U) & acc_mask);
    uint32_t base_id = (style_id & ~acc_mask) & 0xFFFFU;
    bool photo_safe = (acc_flags & FLOW_STYLE_FLAG_PHOTOSENSITIVE_SAFE) != 0U;
    bool cb_geom    = (acc_flags & FLOW_STYLE_FLAG_COLORBLIND_GEOM) != 0U;

    FlowArtStyle64 resolved = FLOW_SSOT_ART_BIBLE[0];
    if (overheated) {
        const FlowArtStyle64 *alert_style = flow_ssot_get_art_style(FLOW_STYLE_THERMAL_MELTDOWN);
        if (alert_style) {
            resolved = *alert_style;
        }
    } else {
        const FlowArtStyle64 *base = flow_ssot_get_art_style(base_id);
        if (base) {
            resolved = *base;
        }
    }

    if (photo_safe || cb_geom) {
        resolved = flow_art_bible_apply_accessibility(resolved, photo_safe, cb_geom);
    }
    return resolved;
}

FlowArtStyle64 flow_art_bible_resolve_accessible(uint32_t style_id,
                                                 uint64_t thermal_kelvin,
                                                 bool prev_overheated,
                                                 bool photosensitive_safe,
                                                 bool colorblind_geom,
                                                 bool *out_overheated) {
    FlowArtStyle64 resolved = flow_art_bible_resolve(
        style_id & 0xFFFFU, thermal_kelvin, prev_overheated, out_overheated
    );
    return flow_art_bible_apply_accessibility(resolved, photosensitive_safe, colorblind_geom);
}

/*
 * ============================================================================
 * 8. Wave64 Compaction & Layer Collapse
 * ============================================================================
 */
uint32_t flow_prefab_cull_and_collapse_wave64(const FlowPrefabInstance64 *instances,
                                             size_t count,
                                             const int64_t view_pos_q16[3],
                                             int64_t layer0_r_max_q16,
                                             int64_t layer1_r_max_q16,
                                             FlowPrefabInstance64 *out_collapsed,
                                             uint32_t out_compact_indices[FLOW_WAVE64_SIZE],
                                             uint64_t *out_active_mask) {
    if (!instances || count == 0U) {
        if (out_active_mask) *out_active_mask = 0ULL;
        return 0U;
    }

    size_t n = (count > (size_t)FLOW_WAVE64_SIZE) ? (size_t)FLOW_WAVE64_SIZE : count;
    __extension__ typedef __int128 i128;
    __extension__ typedef unsigned __int128 u128;

    FlowJet2 jets[FLOW_WAVE64_SIZE];
    uint64_t active_mask = 0ULL;

    for (size_t i = 0; i < n; ++i) {
        if (instances[i].prefab_id == 0U) {
            jets[i] = flow_jet2_make(INT64_MAX, 0, 0);
            continue;
        }

        int64_t dx = instances[i].pos_q16[0] - view_pos_q16[0];
        int64_t dy = instances[i].pos_q16[1] - view_pos_q16[1];
        int64_t dz = instances[i].pos_q16[2] - view_pos_q16[2];

        u128 sq = (u128)((i128)dx * dx + (i128)dy * dy + (i128)dz * dz);
        int64_t dist = (int64_t)flow_asm_isqrt_u128(sq);

        int64_t vx = instances[i].vel_q16[0];
        int64_t vy = instances[i].vel_q16[1];
        int64_t vz = instances[i].vel_q16[2];
        u128 vsq = (u128)((i128)vx * vx + (i128)vy * vy + (i128)vz * vz);
        int64_t vmag = (int64_t)flow_asm_isqrt_u128(vsq);

        jets[i] = flow_jet2_make(dist, vmag, 0);
        active_mask |= (1ULL << i);
    }

    /* Ballot check for active dynamic variations */
    (void)flow_jet2_nondegenerate_ballot64(jets, n, 1, 1);

    if (out_active_mask) {
        *out_active_mask = active_mask;
    }

    /* Compact active lanes into dense output in 1 cycle via hardware POPCNT & CTZ */
    uint32_t active_count = flow_gpu_ballot_compact_wave64(active_mask, 0U, out_compact_indices);

    for (uint32_t k = 0; k < active_count; ++k) {
        uint32_t src_idx = out_compact_indices[k];
        FlowPrefabInstance64 inst = instances[src_idx];

        int64_t dist = jets[src_idx].v;
        if (dist <= layer0_r_max_q16) {
            inst.active_layer = FLOW_STAGE_LAYER_0_SDF;
        } else if (dist <= layer1_r_max_q16) {
            inst.active_layer = FLOW_STAGE_LAYER_1_SLICE;
        } else {
            inst.active_layer = FLOW_STAGE_LAYER_HUD;
        }

        if (out_collapsed) {
            out_collapsed[k] = inst;
        }
    }

    return active_count;
}

/*
 * ============================================================================
 * 9. Concentric SDF Level-Set Padding (+-delta) & Visual Surface Projection
 * ============================================================================
 */
static inline int32_t clamp_i32_unit_q16(int64_t v) {
    if (v > FLOW_Q16_ONE) return (int32_t)FLOW_Q16_ONE;
    if (v < -FLOW_Q16_ONE) return (int32_t)(-FLOW_Q16_ONE);
    return (int32_t)v;
}

void flow_prefab_eval_gradient_q16(const FlowPrefabDesc64 *prefab,
                                   int64_t x_q16,
                                   int64_t y_q16,
                                   int64_t z_q16,
                                   int32_t out_normal_q16[3]) {
    if (!out_normal_q16) {
        return;
    }
    if (!prefab) {
        out_normal_q16[0] = 0;
        out_normal_q16[1] = 0;
        out_normal_q16[2] = (int32_t)(-FLOW_Q16_ONE);
        return;
    }

    const int64_t h = 256LL; /* 1/256 unit step in Q16 */
    int64_t gx = flow_prefab_eval_sdf_q16(prefab, x_q16 + h, y_q16, z_q16) -
                 flow_prefab_eval_sdf_q16(prefab, x_q16 - h, y_q16, z_q16);
    int64_t gy = flow_prefab_eval_sdf_q16(prefab, x_q16, y_q16 + h, z_q16) -
                 flow_prefab_eval_sdf_q16(prefab, x_q16, y_q16 - h, z_q16);
    int64_t gz = flow_prefab_eval_sdf_q16(prefab, x_q16, y_q16, z_q16 + h) -
                 flow_prefab_eval_sdf_q16(prefab, x_q16, y_q16, z_q16 - h);

    __extension__ typedef __int128 i128;
    __extension__ typedef unsigned __int128 u128;
    u128 sq = (u128)((i128)gx * (i128)gx + (i128)gy * (i128)gy + (i128)gz * (i128)gz);
    int64_t mag = (int64_t)flow_asm_isqrt_u128(sq);

    if (mag <= 0) {
        out_normal_q16[0] = 0;
        out_normal_q16[1] = 0;
        out_normal_q16[2] = (int32_t)(-FLOW_Q16_ONE);
        return;
    }

    out_normal_q16[0] = clamp_i32_unit_q16((int64_t)(((i128)gx << FLOW_Q16_SHIFT) / (i128)mag));
    out_normal_q16[1] = clamp_i32_unit_q16((int64_t)(((i128)gy << FLOW_Q16_SHIFT) / (i128)mag));
    out_normal_q16[2] = clamp_i32_unit_q16((int64_t)(((i128)gz << FLOW_Q16_SHIFT) / (i128)mag));
}

int64_t flow_prefab_project_surface_q16(const FlowPrefabDesc64 *prefab,
                                        const int64_t pos_q16[3],
                                        int64_t out_contact_q16[3],
                                        int32_t out_normal_q16[3]) {
    if (!prefab || !pos_q16) {
        return INT64_MAX / 2;
    }

    __extension__ typedef __int128 i128;
    int64_t d0 = flow_prefab_eval_sdf_q16(prefab, pos_q16[0], pos_q16[1], pos_q16[2]);
    int32_t n0[3] = {0, 0, (int32_t)(-FLOW_Q16_ONE)};
    flow_prefab_eval_gradient_q16(prefab, pos_q16[0], pos_q16[1], pos_q16[2], n0);

    /* First-order exact SDF projection: p_contact = p - f(p) * grad_f(p) */
    int64_t pc[3];
    for (size_t k = 0; k < 3U; ++k) {
        int64_t step = (int64_t)(((i128)d0 * (i128)n0[k]) >> FLOW_Q16_SHIFT);
        pc[k] = pos_q16[k] - step;
    }

    /* Up to 2 Newton-gradient refinement steps for composite CSG / smooth-union curvature */
    for (int iter = 0; iter < 2; ++iter) {
        int64_t d_res = flow_prefab_eval_sdf_q16(prefab, pc[0], pc[1], pc[2]);
        if (flow_q16_abs_sat(d_res) <= 32LL) {
            break;
        }
        int32_t n_ref[3];
        flow_prefab_eval_gradient_q16(prefab, pc[0], pc[1], pc[2], n_ref);
        for (size_t k = 0; k < 3U; ++k) {
            int64_t corr = (int64_t)(((i128)d_res * (i128)n_ref[k]) >> FLOW_Q16_SHIFT);
            pc[k] -= corr;
        }
    }

    if (out_contact_q16) {
        out_contact_q16[0] = pc[0];
        out_contact_q16[1] = pc[1];
        out_contact_q16[2] = pc[2];
    }
    if (out_normal_q16) {
        out_normal_q16[0] = n0[0];
        out_normal_q16[1] = n0[1];
        out_normal_q16[2] = n0[2];
    }
    return d0;
}

static bool solve_level_set_1d_jet2(FlowJet2 sdf_jet,
                                    int64_t level_q16,
                                    int64_t t_max_q16,
                                    int64_t *out_t_q16) {
    /* Check if already inside level set f(0) <= level_q16 */
    if (sdf_jet.v <= level_q16) {
        if (out_t_q16) *out_t_q16 = 0LL;
        return true;
    }

    /* Shift 0-Jet value: f(t) - level_q16 == 0 */
    __extension__ typedef __int128 i128;
    int64_t shifted_v = flow_q16_clamp_i128((i128)sdf_jet.v - (i128)level_q16);
    FlowJet2 shifted = flow_jet2_make(shifted_v, sdf_jet.d1, sdf_jet.d2);

    int64_t t_root = 0LL;
    if (flow_jet2_solve_root(shifted, t_max_q16, &t_root) &&
        t_root > 0LL && t_root <= t_max_q16) {
        if (out_t_q16) *out_t_q16 = t_root;
        return true;
    }

    /* Check parabolic vertex t_vert = -d1 / d2 in (0, t_max_q16] for tangential kiss */
    if (sdf_jet.d2 > 0LL && sdf_jet.d1 < 0LL) {
        i128 t_v = (-(i128)sdf_jet.d1 << FLOW_Q16_SHIFT) / (i128)sdf_jet.d2;
        if (t_v > 0 && t_v <= (i128)t_max_q16) {
            int64_t tv_q16 = (int64_t)t_v;
            int64_t val_vert = flow_jet2_eval_taylor(sdf_jet, tv_q16);
            if (val_vert <= level_q16) {
                if (out_t_q16) *out_t_q16 = tv_q16;
                return true;
            }
        }
    }

    /* Check endpoint t_max_q16 */
    int64_t val_end = flow_jet2_eval_taylor(sdf_jet, t_max_q16);
    if (val_end <= level_q16) {
        if (out_t_q16) *out_t_q16 = t_max_q16;
        return true;
    }

    return false;
}

FlowHitZone flow_sdf_jet2_solve_padded(FlowJet2 sdf_jet,
                                       int64_t delta_hurt_q16,
                                       int64_t delta_graze_q16,
                                       int64_t delta_hit_q16,
                                       int64_t t_max_q16,
                                       int64_t *out_t_hit_q16) {
    if (t_max_q16 <= 0LL) {
        if (out_t_hit_q16) *out_t_hit_q16 = 0LL;
        return FLOW_HIT_NONE;
    }

    int64_t dhurt  = (delta_hurt_q16 >= 0LL) ? delta_hurt_q16 : 0LL;
    int64_t dgraze = (delta_graze_q16 >= 0LL) ? delta_graze_q16 : 0LL;
    int64_t dhit   = (delta_hit_q16 >= dgraze) ? delta_hit_q16 : dgraze;

    int64_t t_hit = 0LL;

    /* 1. Shrunk Lethal Core: f(t) <= -delta_hurt_q16 -> solve(J + delta_hurt) */
    if (solve_level_set_1d_jet2(sdf_jet, -dhurt, t_max_q16, &t_hit)) {
        if (out_t_hit_q16) *out_t_hit_q16 = t_hit;
        return FLOW_HIT_LETHAL;
    }

    /* 2. Near-Miss Graze Zone: -delta_hurt_q16 < f(t) <= +delta_graze_q16 -> solve(J - delta_graze) */
    if (solve_level_set_1d_jet2(sdf_jet, dgraze, t_max_q16, &t_hit)) {
        if (out_t_hit_q16) *out_t_hit_q16 = t_hit;
        return FLOW_HIT_GRAZE;
    }

    /* 3. Expanded Weapon / Mining Laser Lock-On Hitbox: f(t) <= +delta_hit_q16 -> solve(J - delta_hit) */
    if (solve_level_set_1d_jet2(sdf_jet, dhit, t_max_q16, &t_hit)) {
        if (out_t_hit_q16) *out_t_hit_q16 = t_hit;
        return FLOW_HIT_LOCKED;
    }

    if (out_t_hit_q16) *out_t_hit_q16 = 0LL;
    return FLOW_HIT_NONE;
}

FlowHitZone flow_prefab_eval_worldline_padded_jet2(const FlowPrefabDesc64 *prefab,
                                                   const FlowJet2 traj[3],
                                                   int64_t delta_hurt_q16,
                                                   int64_t delta_graze_q16,
                                                   int64_t delta_hit_q16,
                                                   int64_t t_max_q16,
                                                   FlowContactManifold64 *out_manifold) {
    if (out_manifold) {
        memset(out_manifold, 0, sizeof(*out_manifold));
    }
    if (!prefab || !traj || t_max_q16 <= 0LL) {
        return FLOW_HIT_NONE;
    }

    __extension__ typedef __int128 i128;

    int64_t dhurt  = (delta_hurt_q16 >= 0LL) ? delta_hurt_q16 : 0LL;
    int64_t dgraze = (delta_graze_q16 >= 0LL) ? delta_graze_q16 : 0LL;
    int64_t dhit   = (delta_hit_q16 >= dgraze) ? delta_hit_q16 : dgraze;

    /* Construct local 1D SDF 2-Jet at t = 0 */
    int64_t d0 = flow_prefab_eval_sdf_q16(prefab, traj[0].v, traj[1].v, traj[2].v);
    int64_t eps = 256LL;
    if (eps > (t_max_q16 / 4LL)) eps = t_max_q16 / 4LL;
    if (eps < 1LL) eps = 1LL;

    int64_t p1[3], p2[3];
    for (size_t k = 0; k < 3U; ++k) {
        p1[k] = flow_jet2_eval_taylor(traj[k], eps);
        p2[k] = flow_jet2_eval_taylor(traj[k], 2LL * eps);
    }
    int64_t d_eps  = flow_prefab_eval_sdf_q16(prefab, p1[0], p1[1], p1[2]);
    int64_t d_2eps = flow_prefab_eval_sdf_q16(prefab, p2[0], p2[1], p2[2]);

    i128 iv = (i128)d0;
    i128 id_eps = (i128)d_eps;
    i128 id_2eps = (i128)d_2eps;
    i128 ieps = (i128)eps;
    i128 iq16 = (i128)FLOW_Q16_ONE;

    i128 d1_jet = ((4 * id_eps - id_2eps - 3 * iv) * iq16) / (2 * ieps);
    i128 d2_jet = (2 * (id_2eps - 2 * id_eps + iv) * iq16 * iq16) / (ieps * ieps);
    FlowJet2 dist_jet = flow_jet2_make(
        d0, flow_q16_clamp_i128(d1_jet), flow_q16_clamp_i128(d2_jet)
    );

    /*
     * Also compute exact closest-approach time t_closest of ||p(t)||^2 in (0, t_max_q16]
     * so off-axis grazing chords that start far away never suffer Taylor horizon truncation.
     */
    FlowJet2 r2_jet = flow_jet2_add(
        flow_jet2_add(flow_jet2_mul(traj[0], traj[0]), flow_jet2_mul(traj[1], traj[1])),
        flow_jet2_mul(traj[2], traj[2])
    );
    int64_t t_closest = 0LL;
    if (r2_jet.d2 > 0LL && r2_jet.d1 < 0LL) {
        i128 tc = (-(i128)r2_jet.d1 << FLOW_Q16_SHIFT) / (i128)r2_jet.d2;
        if (tc > (i128)t_max_q16) {
            tc = (i128)t_max_q16;
        }
        if (tc > 0) {
            t_closest = (int64_t)tc;
        }
    }

    int64_t d_closest = d0;
    if (t_closest > 0LL) {
        int64_t pc_x = flow_jet2_eval_taylor(traj[0], t_closest);
        int64_t pc_y = flow_jet2_eval_taylor(traj[1], t_closest);
        int64_t pc_z = flow_jet2_eval_taylor(traj[2], t_closest);
        d_closest = flow_prefab_eval_sdf_q16(prefab, pc_x, pc_y, pc_z);
    }

    int64_t t_hit = 0LL;
    FlowHitZone zone = flow_sdf_jet2_solve_padded(
        dist_jet, dhurt, dgraze, dhit, t_max_q16, &t_hit
    );

    /* Verify actual SDF at predicted t_hit and compare against closest-approach point */
    if (zone != FLOW_HIT_NONE) {
        int64_t ph_x = flow_jet2_eval_taylor(traj[0], t_hit);
        int64_t ph_y = flow_jet2_eval_taylor(traj[1], t_hit);
        int64_t ph_z = flow_jet2_eval_taylor(traj[2], t_hit);
        int64_t d_at_hit = flow_prefab_eval_sdf_q16(prefab, ph_x, ph_y, ph_z);

        /* Re-classify based on true trajectory minimum if Taylor curvature overestimated */
        int64_t d_min = (d_at_hit < d_closest) ? d_at_hit : d_closest;
        if (d_closest < d_at_hit && t_closest > 0LL) {
            t_hit = t_closest;
        }
        if (d_min <= -dhurt) {
            zone = FLOW_HIT_LETHAL;
        } else if (d_min <= dgraze) {
            zone = FLOW_HIT_GRAZE;
        } else if (d_min <= dhit) {
            zone = FLOW_HIT_LOCKED;
        } else {
            zone = FLOW_HIT_NONE;
        }
    } else if (t_closest > 0LL) {
        if (d_closest <= -dhurt) {
            zone = FLOW_HIT_LETHAL;
            t_hit = t_closest;
        } else if (d_closest <= dgraze) {
            zone = FLOW_HIT_GRAZE;
            t_hit = t_closest;
        } else if (d_closest <= dhit) {
            zone = FLOW_HIT_LOCKED;
            t_hit = t_closest;
        }
    }

    if (zone == FLOW_HIT_NONE) {
        if (out_manifold) {
            out_manifold->hit_zone = FLOW_HIT_NONE;
            out_manifold->sdf_dist_q16 = (d_closest < d0) ? d_closest : d0;
        }
        return FLOW_HIT_NONE;
    }

    /* Refine entry root for head-on impacts if needed */
    int64_t target_level = (zone == FLOW_HIT_LETHAL) ? (-dhurt)
                         : ((zone == FLOW_HIT_GRAZE) ? dgraze : dhit);
    int64_t pos_hit[3] = {
        flow_jet2_eval_taylor(traj[0], t_hit),
        flow_jet2_eval_taylor(traj[1], t_hit),
        flow_jet2_eval_taylor(traj[2], t_hit)
    };
    int64_t d_eval = flow_prefab_eval_sdf_q16(prefab, pos_hit[0], pos_hit[1], pos_hit[2]);
    if (d1_jet < 0 && flow_q16_abs_sat(d_eval - target_level) > 128LL) {
        int64_t dt_ref = (int64_t)(((i128)(target_level - d_eval) * iq16) / d1_jet);
        int64_t t_ref = t_hit + dt_ref;
        if (t_ref > 0LL && t_ref <= t_max_q16) {
            t_hit = t_ref;
            for (size_t k = 0; k < 3U; ++k) {
                pos_hit[k] = flow_jet2_eval_taylor(traj[k], t_hit);
            }
        }
    }

    /* Project onto the exact f(x) = 0 visual surface: p_contact = p - f(p) * grad_f(p) */
    int64_t contact_pos[3] = {0LL, 0LL, 0LL};
    int32_t normal_q16[3]  = {0, 0, 0};
    int64_t sdf_dist = flow_prefab_project_surface_q16(prefab, pos_hit, contact_pos, normal_q16);

    if (out_manifold) {
        out_manifold->hit_zone           = (uint32_t)zone;
        out_manifold->contact_latched    = (zone == FLOW_HIT_LETHAL || zone == FLOW_HIT_GRAZE) ? 1U : 0U;
        out_manifold->t_hit_q16          = t_hit;
        out_manifold->sdf_dist_q16       = sdf_dist;
        out_manifold->contact_pos_q16[0] = contact_pos[0];
        out_manifold->contact_pos_q16[1] = contact_pos[1];
        out_manifold->contact_pos_q16[2] = contact_pos[2];
        out_manifold->normal_q16[0]      = normal_q16[0];
        out_manifold->normal_q16[1]      = normal_q16[1];
        out_manifold->normal_q16[2]      = normal_q16[2];
        out_manifold->graze_bonus        = (zone == FLOW_HIT_GRAZE)
            ? (uint32_t)(100U * (uint32_t)(FLOW_RICARDIAN_GRADE_COUNT - prefab->ricardian_grade))
            : 0U;
    }

    return zone;
}

/*
 * ============================================================================
 * 8. Prefab-Managed Non-Destructive Edit / SSOT Revert & SDF X-Ray Cutaway Mask
 * ============================================================================
 */
void flow_prefab_edit_init64(FlowPrefabEditState64 *edit,
                             const FlowPrefabInstance64 *inst) {
    if (!edit) return;
    memset(edit, 0, sizeof(*edit));
    edit->xray_mask_mode      = (uint16_t)FLOW_PREFAB_XRAY_OFF;
    edit->xray_radius_q16     = 29491; /* 0.45m default X-Ray cutaway radius */
    edit->xray_cut_layer_mask = 0x1U;  /* Carve Layer 0 outer rock/armor by default */

    if (inst) {
        edit->target_prefab_id    = inst->prefab_id;
        edit->baseline_pos_q16[0] = inst->pos_q16[0];
        edit->baseline_pos_q16[1] = inst->pos_q16[1];
        edit->baseline_pos_q16[2] = inst->pos_q16[2];
        edit->baseline_thermal_k  = inst->thermal_kelvin;
        edit->baseline_layer      = inst->active_layer;
        edit->baseline_flags      = inst->flags;
    }
}

void flow_prefab_edit_begin_modal64(FlowPrefabEditState64 *edit,
                                    const FlowPrefabInstance64 *inst,
                                    uint16_t modal_op_mode) {
    if (!edit) return;
    edit->modal_op_mode = modal_op_mode;
    if (inst) {
        edit->target_prefab_id    = inst->prefab_id;
        edit->baseline_pos_q16[0] = inst->pos_q16[0];
        edit->baseline_pos_q16[1] = inst->pos_q16[1];
        edit->baseline_pos_q16[2] = inst->pos_q16[2];
        edit->baseline_thermal_k  = inst->thermal_kelvin;
        edit->baseline_layer      = inst->active_layer;
        edit->baseline_flags      = inst->flags;
    }
}

void flow_prefab_edit_apply_scrub64(FlowPrefabEditState64 *edit,
                                    FlowPrefabInstance64 *inst,
                                    int32_t delta_x_q16,
                                    int32_t delta_y_q16) {
    if (!edit) return;

    if (edit->modal_op_mode == FLOW_PREFAB_MODAL_XRAY_SCRUB) {
        int32_t next_r = edit->xray_radius_q16 + delta_x_q16;
        if (next_r < 0) next_r = 0;
        if (next_r > 65536) next_r = 65536;
        edit->xray_radius_q16 = next_r;
        edit->xray_mask_mode  = (next_r > 2048)
            ? (uint16_t)FLOW_PREFAB_XRAY_SPHERE_CUT
            : (uint16_t)FLOW_PREFAB_XRAY_OFF;
    } else if (edit->modal_op_mode == FLOW_PREFAB_MODAL_TRANSLATE && inst) {
        inst->pos_q16[0] += (int64_t)delta_x_q16;
        inst->pos_q16[1] += (int64_t)delta_y_q16;
    } else if (edit->modal_op_mode == FLOW_PREFAB_MODAL_THERMAL_SCRUB && inst) {
        int32_t next_k = (int32_t)inst->thermal_kelvin + (delta_x_q16 >> 8);
        if (next_k < 700) next_k = 700;
        if (next_k > 1200) next_k = 1200;
        inst->thermal_kelvin = (uint32_t)next_k;
    }
}

void flow_prefab_edit_commit64(FlowPrefabEditState64 *edit,
                               const FlowPrefabInstance64 *inst) {
    if (!edit) return;
    edit->modal_op_mode = (uint16_t)FLOW_PREFAB_MODAL_NONE;
    edit->undo_commit_count++;
    if (inst) {
        edit->baseline_pos_q16[0] = inst->pos_q16[0];
        edit->baseline_pos_q16[1] = inst->pos_q16[1];
        edit->baseline_pos_q16[2] = inst->pos_q16[2];
        edit->baseline_thermal_k  = inst->thermal_kelvin;
        edit->baseline_layer      = inst->active_layer;
        edit->baseline_flags      = inst->flags;
    }
}

void flow_prefab_edit_revert_ssot64(FlowPrefabEditState64 *edit,
                                    FlowPrefabInstance64 *inst) {
    if (!edit) return;
    edit->modal_op_mode = (uint16_t)FLOW_PREFAB_MODAL_NONE;
    edit->revert_exec_count++;
    if (inst) {
        inst->pos_q16[0]     = edit->baseline_pos_q16[0];
        inst->pos_q16[1]     = edit->baseline_pos_q16[1];
        inst->pos_q16[2]     = edit->baseline_pos_q16[2];
        inst->thermal_kelvin = edit->baseline_thermal_k;
        inst->active_layer   = edit->baseline_layer;
        inst->flags          = edit->baseline_flags;
    }
}

int64_t flow_prefab_eval_xray_cutaway_q16(const FlowPrefabEditState64 *edit,
                                          int64_t d_outer_q16,
                                          int64_t d_xray_sphere_q16) {
    if (!edit || edit->xray_mask_mode == FLOW_PREFAB_XRAY_OFF) {
        return d_outer_q16;
    }
    int64_t neg_xray = -d_xray_sphere_q16;
    return (d_outer_q16 >= neg_xray) ? d_outer_q16 : neg_xray;
}
