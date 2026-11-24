#include "bloom_scene.h"
#include "bloom_prefab.h"

#include <limits.h>
#include <string.h>

/*
 * ============================================================================
 * BLOOM Thin 64-Byte Wire-Schema & Q16 Fixed-Point Data Builders
 * Zero CPU SDF/BVH/Mesh Evaluators | Pure-GPR C17 | Zero Heap | Zero Float
 * ============================================================================
 */

#define BLOOM_TWO_PI_Q16  411775
#define BLOOM_PI_Q16      205887
#define BLOOM_HALF_PI_Q16 102944

static inline int64_t wire_abs64(int64_t x) {
    if (x == INT64_MIN) return INT64_MAX;
    return (x < 0) ? -x : x;
}

static inline int64_t wire_max64(int64_t a, int64_t b) {
    return (a > b) ? a : b;
}

static inline int64_t wire_min64(int64_t a, int64_t b) {
    return (a < b) ? a : b;
}

static inline int64_t wire_clamp64(int64_t v, int64_t lo, int64_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int32_t wire_clamp32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int32_t wire_clamp64_to_i32(int64_t v) {
    if (v > (int64_t)BLOOM_MAX_Q16) return BLOOM_MAX_Q16;
    if (v < -(int64_t)BLOOM_MAX_Q16) return -BLOOM_MAX_Q16;
    return (int32_t)v;
}

static inline int32_t wire_mul_q16(int32_t a, int32_t b) {
    int64_t prod = ((int64_t)a * (int64_t)b) >> 16;
    if (prod > (int64_t)BLOOM_MAX_Q16) return BLOOM_MAX_Q16;
    if (prod < -(int64_t)BLOOM_MAX_Q16) return -BLOOM_MAX_Q16;
    return (int32_t)prod;
}

static void wire_stamp_mac(void *block64, uint64_t seed, uint32_t *out_lo, uint32_t *out_hi) {
    uint64_t mac = bloom_hash56_fnv1a(block64, seed);
    *out_lo = (uint32_t)(mac & 0xFFFFFFFFULL);
    *out_hi = (uint32_t)(mac >> 32ULL);
}

static uint64_t wire_isqrt_u64(uint64_t n) {
    if (n == 0ULL) return 0ULL;
    if (n == UINT64_MAX) return 0xFFFFFFFFULL;
    uint64_t x = n;
    uint64_t y = (x >> 1ULL) + 1ULL;
    while (y < x) {
        x = y;
        y = (x + n / x) >> 1ULL;
    }
    return x;
}

int32_t bloom_isqrt_u64_to_i32(uint64_t n) {
    if (n >= (uint64_t)BLOOM_MAX_Q16 * (uint64_t)BLOOM_MAX_Q16) {
        return BLOOM_MAX_Q16;
    }
    uint64_t x = wire_isqrt_u64(n);
    if (x > (uint64_t)BLOOM_MAX_Q16) return BLOOM_MAX_Q16;
    return (int32_t)x;
}

static int64_t wire_len3_q16(int64_t x, int64_t y, int64_t z) {
    uint64_t ax = (uint64_t)wire_abs64(x);
    uint64_t ay = (uint64_t)wire_abs64(y);
    uint64_t az = (uint64_t)wire_abs64(z);
    uint32_t sh = 0U;
    while (((ax | ay | az) >> sh) > 0x3FFFFFFFULL) {
        ++sh;
    }
    uint64_t sx = ax >> sh;
    uint64_t sy = ay >> sh;
    uint64_t sz = az >> sh;
    uint64_t r = wire_isqrt_u64(sx * sx + sy * sy + sz * sz) << sh;
    if (r > (uint64_t)INT64_MAX) return INT64_MAX;
    return (int64_t)r;
}

static void wire_vec3_norm_q16(int64_t x, int64_t y, int64_t z, int32_t out_n[3]) {
    int64_t len = wire_len3_q16(x, y, z);
    if (len <= 0) {
        out_n[0] = 0;
        out_n[1] = 0;
        out_n[2] = BLOOM_Q16_ONE;
        return;
    }
    int64_t cx = wire_min64(wire_max64(x, -0x3FFFFFFFLL), 0x3FFFFFFFLL);
    int64_t cy = wire_min64(wire_max64(y, -0x3FFFFFFFLL), 0x3FFFFFFFLL);
    int64_t cz = wire_min64(wire_max64(z, -0x3FFFFFFFLL), 0x3FFFFFFFLL);
    out_n[0] = wire_clamp64_to_i32((cx << 16) / len);
    out_n[1] = wire_clamp64_to_i32((cy << 16) / len);
    out_n[2] = wire_clamp64_to_i32((cz << 16) / len);
}

int32_t bloom_sin_q16(int32_t angle_q16) {
    int64_t a = (int64_t)angle_q16 % (int64_t)BLOOM_TWO_PI_Q16;
    if (a > BLOOM_PI_Q16) a -= BLOOM_TWO_PI_Q16;
    if (a < -BLOOM_PI_Q16) a += BLOOM_TWO_PI_Q16;

    int64_t sign = 1;
    if (a < 0) {
        sign = -1;
        a = -a;
    }
    if (a > BLOOM_HALF_PI_Q16) {
        a = BLOOM_PI_Q16 - a;
    }
    int64_t num = 4LL * a * ((int64_t)BLOOM_PI_Q16 - a);
    int64_t denom = ((int64_t)BLOOM_PI_Q16 * (int64_t)BLOOM_PI_Q16) >> 16;
    int64_t p = (denom > 0) ? (num / denom) : 0;
    int64_t corr = ((p * p) >> 16);
    int64_t y = ((50790LL * p + 14746LL * corr) >> 16) * sign;
    return wire_clamp32((int32_t)y, -BLOOM_Q16_ONE, BLOOM_Q16_ONE);
}

int32_t bloom_cos_q16(int32_t angle_q16) {
    int64_t shifted = ((int64_t)angle_q16 + (int64_t)BLOOM_HALF_PI_Q16) % (int64_t)BLOOM_TWO_PI_Q16;
    return bloom_sin_q16((int32_t)shifted);
}

void bloom_rot_inv_euler_q16(const int32_t in_pt_q16[3],
                             const int32_t rot_euler_q16[3],
                             int32_t out_pt_q16[3]) {
    if (!in_pt_q16 || !out_pt_q16) return;
    if (!rot_euler_q16 || (rot_euler_q16[0] == 0 && rot_euler_q16[1] == 0 && rot_euler_q16[2] == 0)) {
        out_pt_q16[0] = in_pt_q16[0];
        out_pt_q16[1] = in_pt_q16[1];
        out_pt_q16[2] = in_pt_q16[2];
        return;
    }
    int64_t x = wire_clamp64((int64_t)in_pt_q16[0], -0x1FFFFFFFLL, 0x1FFFFFFFLL);
    int64_t y = wire_clamp64((int64_t)in_pt_q16[1], -0x1FFFFFFFLL, 0x1FFFFFFFLL);
    int64_t z = wire_clamp64((int64_t)in_pt_q16[2], -0x1FFFFFFFLL, 0x1FFFFFFFLL);

    int64_t sx = bloom_sin_q16(rot_euler_q16[0]), cx = bloom_cos_q16(rot_euler_q16[0]);
    int64_t sy = bloom_sin_q16(rot_euler_q16[1]), cy = bloom_cos_q16(rot_euler_q16[1]);
    int64_t sz = bloom_sin_q16(rot_euler_q16[2]), cz = bloom_cos_q16(rot_euler_q16[2]);

    int64_t x1 = (x * cy - z * sy) >> 16;
    int64_t z1 = (x * sy + z * cy) >> 16;

    int64_t y2 = (y * cx + z1 * sx) >> 16;
    int64_t z2 = (-y * sx + z1 * cx) >> 16;

    int64_t x3 = (x1 * cz + y2 * sz) >> 16;
    int64_t y3 = (-x1 * sz + y2 * cz) >> 16;

    out_pt_q16[0] = wire_clamp64_to_i32(x3);
    out_pt_q16[1] = wire_clamp64_to_i32(y3);
    out_pt_q16[2] = wire_clamp64_to_i32(z2);
}

void bloom_section_init64(BloomSectionSpec64 *sec,
                          uint32_t mode,
                          const int32_t norm_q16[3],
                          int32_t dist_q16,
                          int32_t slab_half_q16,
                          int32_t min_wall_q16,
                          int32_t iso_pitch_q16,
                          uint32_t inspect_flags) {
    if (!sec) return;
    uint16_t jc = sec->joint_count;
    uint16_t bc = sec->brick_count;
    uint16_t mt = sec->mesh_tri_count;
    uint16_t mb = sec->mesh_bvh_count;
    uint16_t gs = sec->global_csg_start;
    uint16_t gc = sec->global_csg_count;
    uint32_t mms = sec->mesh_matter_style;
    int32_t tol = (sec->clearance_tol_q16 > 0) ? sec->clearance_tol_q16 : 13107;

    memset(sec, 0, sizeof(*sec));
    sec->section_mode = (mode <= BLOOM_SEC_MODE_CORNER) ? mode : BLOOM_SEC_MODE_PLANE;
    sec->inspect_flags = inspect_flags;
    sec->joint_count = jc;
    sec->brick_count = bc;
    sec->mesh_tri_count = mt;
    sec->mesh_bvh_count = mb;
    sec->global_csg_start = gs;
    sec->global_csg_count = gc;
    sec->mesh_matter_style = mms;
    if (norm_q16 && (norm_q16[0] != 0 || norm_q16[1] != 0 || norm_q16[2] != 0)) {
        wire_vec3_norm_q16(norm_q16[0], norm_q16[1], norm_q16[2], sec->plane_norm_q16);
    } else {
        sec->plane_norm_q16[0] = BLOOM_Q16_ONE;
        sec->plane_norm_q16[1] = 0;
        sec->plane_norm_q16[2] = 0;
    }
    sec->plane_dist_q16 = wire_clamp32(dist_q16, -64 * BLOOM_Q16_ONE, 64 * BLOOM_Q16_ONE);
    sec->slab_half_q16 = wire_clamp32(slab_half_q16, 1024, 32 * BLOOM_Q16_ONE);
    sec->min_wall_q16 = wire_clamp32(min_wall_q16, 1024, 8 * BLOOM_Q16_ONE);
    sec->iso_pitch_q16 = wire_clamp32(iso_pitch_q16, 1024, 8 * BLOOM_Q16_ONE);
    sec->clearance_tol_q16 = tol;
    wire_stamp_mac(sec, 0x5EC70064ULL, &sec->mac_lo32, &sec->mac_hi32);
}

void bloom_csg_op_init64(BloomCsgOp64 *out_op,
                         uint32_t op_id,
                         uint32_t prim_type,
                         uint32_t bool_op,
                         uint32_t matter_id,
                         const int32_t pos_q16[3],
                         const int32_t extents_q16[3],
                         int32_t smooth_k_q16,
                         int32_t bevel_r_q16) {
    if (!out_op) return;
    memset(out_op, 0, sizeof(*out_op));
    out_op->op_id = op_id;
    out_op->prim_type = prim_type;
    out_op->bool_op = bool_op;
    out_op->matter_id = matter_id;
    if (pos_q16) {
        out_op->pos_q16[0] = pos_q16[0];
        out_op->pos_q16[1] = pos_q16[1];
        out_op->pos_q16[2] = pos_q16[2];
    }
    if (extents_q16) {
        out_op->extents_q16[0] = extents_q16[0];
        out_op->extents_q16[1] = extents_q16[1];
        out_op->extents_q16[2] = extents_q16[2];
    }
    out_op->smooth_k_q16 = (smooth_k_q16 > 0) ? smooth_k_q16 : 0;
    out_op->bevel_r_q16 = (bevel_r_q16 > 0) ? bevel_r_q16 : 0;
    out_op->stage_mask_lo32 = 0xFFFFFFFFU;
    out_op->stage_mask_hi32 = 0xFFFFFFFFU;
    wire_stamp_mac(out_op, 0xC5600064ULL, &out_op->mac_lo32, &out_op->mac_hi32);
}

static void wire_quat_normalize_q16(int32_t q[4]) {
    uint64_t max_a = 0ULL;
    for (int i = 0; i < 4; ++i) {
        uint64_t a = (uint64_t)wire_abs64((int64_t)q[i]);
        if (a > max_a) max_a = a;
    }
    if (max_a == 0ULL) {
        q[0] = BLOOM_Q16_ONE;
        q[1] = q[2] = q[3] = 0;
        return;
    }
    uint32_t sh = 0U;
    while ((max_a >> sh) > 0x1FFFFFFFULL) {
        ++sh;
    }
    int64_t sv[4];
    uint64_t n2 = 0ULL;
    for (int i = 0; i < 4; ++i) {
        int64_t v = (int64_t)q[i];
        sv[i] = (sh > 0U) ? (v / ((int64_t)1LL << sh)) : v;
        n2 += (uint64_t)(sv[i] * sv[i]);
    }
    uint64_t len = wire_isqrt_u64(n2);
    if (len == 0ULL) {
        q[0] = BLOOM_Q16_ONE;
        q[1] = q[2] = q[3] = 0;
        return;
    }
    for (int i = 0; i < 4; ++i) {
        int64_t norm_i = (sv[i] * 65536LL) / (int64_t)len;
        q[i] = wire_clamp32((int32_t)norm_i, -BLOOM_Q16_ONE, BLOOM_Q16_ONE);
    }
}

void bloom_joint_dq64_make(BloomJointDq64 *out_joint,
                           uint32_t joint_id,
                           uint32_t parent_id,
                           const int32_t q_rot_q16[4],
                           const int32_t trans_q16[3],
                           int32_t bone_len_q16,
                           int32_t bone_radius_q16,
                           int32_t smooth_k_q16,
                           uint32_t matter_id) {
    if (!out_joint) return;
    memset(out_joint, 0, sizeof(*out_joint));
    out_joint->joint_id = joint_id;
    out_joint->parent_id = parent_id;
    if (q_rot_q16) {
        for (int i = 0; i < 4; ++i) out_joint->q_real_q16[i] = q_rot_q16[i];
    } else {
        out_joint->q_real_q16[0] = BLOOM_Q16_ONE;
    }
    wire_quat_normalize_q16(out_joint->q_real_q16);

    int64_t tx = trans_q16 ? (int64_t)trans_q16[0] : 0;
    int64_t ty = trans_q16 ? (int64_t)trans_q16[1] : 0;
    int64_t tz = trans_q16 ? (int64_t)trans_q16[2] : 0;
    int64_t qw = out_joint->q_real_q16[0];
    int64_t qx = out_joint->q_real_q16[1];
    int64_t qy = out_joint->q_real_q16[2];
    int64_t qz = out_joint->q_real_q16[3];

    out_joint->q_dual_q16[0] = wire_clamp64_to_i32((-(tx * qx + ty * qy + tz * qz)) >> 17);
    out_joint->q_dual_q16[1] = wire_clamp64_to_i32(((tx * qw + ty * qz - tz * qy)) >> 17);
    out_joint->q_dual_q16[2] = wire_clamp64_to_i32(((-tx * qz + ty * qw + tz * qx)) >> 17);
    out_joint->q_dual_q16[3] = wire_clamp64_to_i32(((tx * qy - ty * qx + tz * qw)) >> 17);

    out_joint->bone_len_q16 = bone_len_q16;
    out_joint->bone_radius_q16 = bone_radius_q16;
    out_joint->smooth_k_q16 = smooth_k_q16;
    out_joint->matter_id = matter_id;
    wire_stamp_mac(out_joint, 0xD0640064ULL, &out_joint->mac_lo32, &out_joint->mac_hi32);
}

void bloom_joint_dq64_blend(const BloomJointDq64 *a,
                            const BloomJointDq64 *b,
                            int32_t t_q16,
                            BloomJointDq64 *out_blended) {
    if (!a || !b || !out_blended) return;
    int64_t tc = (int64_t)wire_clamp32(t_q16, 0, BLOOM_Q16_ONE);
    int64_t wa = (int64_t)BLOOM_Q16_ONE - tc;
    int64_t wb = tc;

    uint64_t max_ab = 0ULL;
    for (int i = 0; i < 4; ++i) {
        uint64_t ma = (uint64_t)wire_abs64((int64_t)a->q_real_q16[i]);
        uint64_t mb = (uint64_t)wire_abs64((int64_t)b->q_real_q16[i]);
        if (ma > max_ab) max_ab = ma;
        if (mb > max_ab) max_ab = mb;
    }
    uint32_t dsh = 0U;
    while ((max_ab >> dsh) > 0x1FFFFFFFULL) {
        ++dsh;
    }
    int64_t dot_r = 0;
    for (int i = 0; i < 4; ++i) {
        int64_t ar = (dsh > 0U) ? ((int64_t)a->q_real_q16[i] / ((int64_t)1LL << dsh)) : (int64_t)a->q_real_q16[i];
        int64_t br = (dsh > 0U) ? ((int64_t)b->q_real_q16[i] / ((int64_t)1LL << dsh)) : (int64_t)b->q_real_q16[i];
        dot_r += ar * br;
    }
    if (dot_r < 0) wb = -wb;

    *out_blended = *a;
    int64_t r64[4];
    int64_t d64[4];
    uint64_t max_r = 0ULL;
    for (int i = 0; i < 4; ++i) {
        r64[i] = (wa * (int64_t)a->q_real_q16[i] + wb * (int64_t)b->q_real_q16[i]) >> 16;
        d64[i] = (wa * (int64_t)a->q_dual_q16[i] + wb * (int64_t)b->q_dual_q16[i]) >> 16;
        uint64_t mr = (uint64_t)wire_abs64(r64[i]);
        if (mr > max_r) max_r = mr;
    }
    uint32_t rsh = 0U;
    while ((max_r >> rsh) > 0x1FFFFFFFULL) {
        ++rsh;
    }
    uint64_t n2 = 0ULL;
    for (int i = 0; i < 4; ++i) {
        int64_t sr = (rsh > 0U) ? (r64[i] / ((int64_t)1LL << rsh)) : r64[i];
        n2 += (uint64_t)(sr * sr);
    }
    uint64_t inv_len = wire_isqrt_u64(n2) << rsh;
    if (inv_len > 0ULL) {
        for (int i = 0; i < 4; ++i) {
            int64_t nr = (r64[i] * 65536LL) / (int64_t)inv_len;
            int64_t nd = (wire_clamp64(d64[i], -0x7FFFFFFFLL, 0x7FFFFFFFLL) * 65536LL) / (int64_t)inv_len;
            out_blended->q_real_q16[i] = wire_clamp32((int32_t)nr, -BLOOM_Q16_ONE, BLOOM_Q16_ONE);
            out_blended->q_dual_q16[i] = wire_clamp64_to_i32(nd);
        }
    } else {
        out_blended->q_real_q16[0] = BLOOM_Q16_ONE;
        out_blended->q_real_q16[1] = 0;
        out_blended->q_real_q16[2] = 0;
        out_blended->q_real_q16[3] = 0;
        for (int i = 0; i < 4; ++i) {
            out_blended->q_dual_q16[i] = wire_clamp64_to_i32(d64[i]);
        }
    }
    wire_stamp_mac(out_blended, 0xD0640064ULL, &out_blended->mac_lo32, &out_blended->mac_hi32);
}

void bloom_matter_init64(BloomMatter64 *out_matter,
                         uint32_t matter_id,
                         int32_t ior_eta_q16,
                         int32_t extinction_kappa_q16,
                         int32_t viscosity_quench_q16,
                         int32_t surface_relief_q16,
                         int32_t micro_roughness_q16,
                         int32_t mie_scatter_q16,
                         const int32_t absorb_rgb_q16[3],
                         int32_t lattice_wire_q16,
                         int32_t thickness_mm_q16,
                         uint32_t thermal_kelvin,
                         uint32_t density_kg_m3) {
    if (!out_matter) return;
    memset(out_matter, 0, sizeof(*out_matter));
    out_matter->matter_id = matter_id;
    out_matter->ior_eta_q16 = wire_clamp32(ior_eta_q16, BLOOM_Q16_ONE, 4 * BLOOM_Q16_ONE);
    out_matter->extinction_kappa_q16 = wire_clamp32(extinction_kappa_q16, 0, 8 * BLOOM_Q16_ONE);
    out_matter->viscosity_quench_q16 = wire_clamp32(viscosity_quench_q16, 0, BLOOM_Q16_ONE);
    out_matter->surface_relief_q16 = wire_clamp32(surface_relief_q16, 0, 2 * BLOOM_Q16_ONE);
    out_matter->micro_roughness_q16 = wire_clamp32(micro_roughness_q16, 0, BLOOM_Q16_ONE);
    out_matter->mie_scatter_q16 = wire_clamp32(mie_scatter_q16, 0, BLOOM_Q16_ONE);
    if (absorb_rgb_q16) {
        for (int i = 0; i < 3; ++i) {
            out_matter->absorb_rgb_q16[i] = wire_clamp32(absorb_rgb_q16[i], 0, 4 * BLOOM_Q16_ONE);
        }
    }
    out_matter->lattice_wire_q16 = wire_clamp32(lattice_wire_q16, 0, BLOOM_Q16_ONE);
    out_matter->thickness_mm_q16 = wire_clamp32(thickness_mm_q16, 4096, 32 * BLOOM_Q16_ONE);
    out_matter->thermal_kelvin = thermal_kelvin;
    out_matter->density_kg_m3 = density_kg_m3;
    wire_stamp_mac(out_matter, 0xAA77E464ULL, &out_matter->mac_lo32, &out_matter->mac_hi32);
}

void bloom_matter_init_canonical(BloomMatter64 out_matters[BLOOM_MAX_MATTERS]) {
    if (!out_matters) return;
    const int32_t abs_glass[3]    = {  3276,  1638,   983 };
    const int32_t abs_jade[3]     = { 45875,  8192, 29491 };
    const int32_t abs_water[3]    = { 19660,  6553,  1966 };
    const int32_t abs_titanium[3] = { 16384, 16384, 14745 };
    const int32_t abs_gold[3]     = {  1310,  9830, 52428 };
    const int32_t abs_sapphire[3] = { 49152, 22937,  1638 };
    const int32_t abs_obsidian[3] = { 58982, 58982, 52428 };
    const int32_t abs_lava[3]     = {  6553, 39321, 65536 };

    bloom_matter_init64(&out_matters[0], 0U,  99615,      0, 65536, 16384,  3276,  1310, abs_glass,     8192, 262144,  295U, 2500U);
    bloom_matter_init64(&out_matters[1], 1U, 108790,      0, 65536, 13107, 19660, 42598, abs_jade,      4096, 393216,  295U, 3330U);
    bloom_matter_init64(&out_matters[2], 2U,  87359,      0, 16384, 32768,  1638,  3276, abs_water,        0, 327680,  290U, 1000U);
    bloom_matter_init64(&out_matters[3], 3U, 141558, 191365, 65536,  9830, 16384,     0, abs_titanium,     0, 131072,  300U, 4506U);
    bloom_matter_init64(&out_matters[4], 4U,  77332, 203161, 65536,  6553,  6553,     0, abs_gold,         0, 131072,  300U, 19300U);
    bloom_matter_init64(&out_matters[5], 5U, 115999,      0, 65536, 19660,  2048,  6553, abs_sapphire, 26214, 327680,  295U, 3980U);
    bloom_matter_init64(&out_matters[6], 6U,  98304,   8192, 65536,  8192,  8192,  2048, abs_obsidian,     0, 393216,  295U, 2600U);
    bloom_matter_init64(&out_matters[7], 7U, 111411,  32768, 32768, 26214, 13107, 16384, abs_lava,     16384, 262144, 1280U, 2800U);
}

void bloom_matter_format_hex128(const BloomMatter64 *matter, char out_hex129[129]) {
    static const char hex_lut[17] = "0123456789abcdef";
    if (!matter || !out_hex129) return;
    const uint8_t *b = (const uint8_t *)matter;
    for (size_t i = 0U; i < 64U; ++i) {
        out_hex129[i * 2U + 0U] = hex_lut[(b[i] >> 4U) & 0xFU];
        out_hex129[i * 2U + 1U] = hex_lut[b[i] & 0xFU];
    }
    out_hex129[128] = '\0';
}

static int wire_hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

bool bloom_matter_parse_hex128(const char *hex128, BloomMatter64 *out_matter) {
    if (!hex128 || !out_matter) return false;
    BloomMatter64 tmp;
    uint8_t *b = (uint8_t *)&tmp;
    for (size_t i = 0U; i < 64U; ++i) {
        int hi = wire_hex_val(hex128[i * 2U + 0U]);
        int lo = wire_hex_val(hex128[i * 2U + 1U]);
        if (hi < 0 || lo < 0) return false;
        b[i] = (uint8_t)((hi << 4) | lo);
    }
    uint32_t exp_lo = 0U, exp_hi = 0U;
    wire_stamp_mac(&tmp, 0xAA77E464ULL, &exp_lo, &exp_hi);
    if (tmp.mac_lo32 != exp_lo || tmp.mac_hi32 != exp_hi) return false;
    *out_matter = tmp;
    return true;
}

void bloom_camera_sync_from_orbit(BloomCamera64 *cam) {
    if (!cam) return;
    int32_t cy = bloom_cos_q16(cam->orbit_yaw_q16);
    int32_t sy = bloom_sin_q16(cam->orbit_yaw_q16);
    int32_t cp = bloom_cos_q16(cam->orbit_pitch_q16);
    int32_t sp = bloom_sin_q16(cam->orbit_pitch_q16);
    int32_t r = (cam->orbit_dist_q16 > BLOOM_Q16_ONE) ? cam->orbit_dist_q16 : BLOOM_Q16_ONE;

    int32_t r_horiz = wire_mul_q16(r, cp);
    cam->eye_q16[0] = wire_clamp64_to_i32((int64_t)cam->target_q16[0] + (int64_t)wire_mul_q16(r_horiz, sy));
    cam->eye_q16[1] = wire_clamp64_to_i32((int64_t)cam->target_q16[1] + (int64_t)wire_mul_q16(r, sp));
    cam->eye_q16[2] = wire_clamp64_to_i32((int64_t)cam->target_q16[2] - (int64_t)wire_mul_q16(r_horiz, cy));
}

void bloom_camera_init64(BloomCamera64 *cam,
                         int32_t yaw_q16,
                         int32_t pitch_q16,
                         int32_t dist_q16,
                         int32_t focal_mm_q16,
                         int32_t fstop_q16) {
    if (!cam) return;
    memset(cam, 0, sizeof(*cam));
    cam->up_q16[1] = BLOOM_Q16_ONE;
    cam->orbit_yaw_q16 = yaw_q16;
    cam->orbit_pitch_q16 = wire_clamp32(pitch_q16, -85000, 85000);
    cam->orbit_dist_q16 = (dist_q16 > BLOOM_Q16_ONE) ? dist_q16 : (6 * BLOOM_Q16_ONE);
    cam->focal_mm_q16 = wire_clamp32(focal_mm_q16, 18 * BLOOM_Q16_ONE, 135 * BLOOM_Q16_ONE);
    cam->aperture_fstop_q16 = wire_clamp32(fstop_q16, 91750, 16 * BLOOM_Q16_ONE);
    cam->focus_dist_q16 = cam->orbit_dist_q16;
    cam->exposure_q16 = BLOOM_Q16_ONE;
    bloom_camera_sync_from_orbit(cam);
}

void bloom_camera_step_cine64(BloomCamera64 *cam,
                              BloomRegBank64 *opt_regs,
                              const int32_t subject_pos_q16[3],
                              int32_t deadzone_q16,
                              int32_t trauma_impulse_q16) {
    if (!cam) return;
    int64_t dz = wire_max64((int64_t)deadzone_q16, 0LL);

    if (subject_pos_q16) {
        for (uint32_t a = 0U; a < 3U; ++a) {
            int64_t diff = (int64_t)subject_pos_q16[a] - (int64_t)cam->target_q16[a];
            if (diff > dz) {
                cam->target_q16[a] = wire_clamp64_to_i32((int64_t)cam->target_q16[a] + ((diff - dz) / 4LL));
            } else if (diff < -dz) {
                cam->target_q16[a] = wire_clamp64_to_i32((int64_t)cam->target_q16[a] + ((diff + dz) / 4LL));
            }
        }
    }

    int64_t trauma = (trauma_impulse_q16 > 0) ? wire_clamp64((int64_t)trauma_impulse_q16, 0LL, BLOOM_Q16_ONE) : 0LL;
    uint32_t ep = 0U;
    if (opt_regs) {
        int64_t prev_tr = wire_clamp64((int64_t)opt_regs->channels_q16[BLOOM_REG_CINE_TRAUMA], 0LL, BLOOM_Q16_ONE);
        int64_t decayed = (prev_tr * 7LL) >> 3;
        trauma = wire_clamp64(decayed + trauma, 0LL, BLOOM_Q16_ONE);
        opt_regs->channels_q16[BLOOM_REG_CINE_TRAUMA] = (int32_t)trauma;
        opt_regs->dirty_mask |= (1U << BLOOM_REG_CINE_TRAUMA);
        opt_regs->epoch++;
        ep = opt_regs->epoch;
        wire_stamp_mac(opt_regs, 0x2E9B0064ULL, &opt_regs->mac_lo32, &opt_regs->mac_hi32);
    }

    bloom_camera_sync_from_orbit(cam);

    if (trauma > 256LL) {
        int64_t amp = (trauma * trauma) >> 20;
        int64_t sx = ((ep & 1U) == 0U) ? amp : -amp;
        int64_t sy = ((ep & 2U) == 0U) ? (amp / 2LL) : -(amp / 2LL);
        cam->eye_q16[0] = wire_clamp64_to_i32((int64_t)cam->eye_q16[0] + sx);
        cam->eye_q16[1] = wire_clamp64_to_i32((int64_t)cam->eye_q16[1] + sy);
    }

    int64_t d_tgt = wire_len3_q16((int64_t)cam->target_q16[0] - (int64_t)cam->eye_q16[0],
                                  (int64_t)cam->target_q16[1] - (int64_t)cam->eye_q16[1],
                                  (int64_t)cam->target_q16[2] - (int64_t)cam->eye_q16[2]);
    cam->focus_dist_q16 = wire_clamp64_to_i32(wire_max64(d_tgt, (int64_t)BLOOM_Q16_ONE));
}
