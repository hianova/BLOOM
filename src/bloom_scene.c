#include "bloom_scene.h"
#include "bloom_prefab.h"

#include <limits.h>
#include <string.h>

/*
 * ============================================================================
 * Pillar 1 ("Three.js"): Pure C17 Fixed-Point 3D Spatial, BVH, Skinning & Optics
 * Zero FloatOps | Zero Heap Allocations | Bit-Identical Across Architectures
 * ============================================================================
 */

static inline int64_t sc_abs64(int64_t x) {
    if (x == INT64_MIN) return INT64_MAX;
    return (x < 0) ? -x : x;
}

static inline int64_t sc_neg64(int64_t x) {
    if (x == INT64_MIN) return INT64_MAX;
    return -x;
}

static inline int64_t sc_max64(int64_t a, int64_t b) {
    return (a > b) ? a : b;
}

static inline int64_t sc_min64(int64_t a, int64_t b) {
    return (a < b) ? a : b;
}

static inline int64_t sc_clamp64(int64_t v, int64_t lo, int64_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int32_t sc_clamp32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int32_t sc_clamp64_to_i32(int64_t v) {
    if (v > (int64_t)BLOOM_MAX_Q16) return BLOOM_MAX_Q16;
    if (v < -(int64_t)BLOOM_MAX_Q16) return -BLOOM_MAX_Q16;
    return (int32_t)v;
}

static inline int64_t sc_sat_sub64(int64_t a, int64_t b) {
    uint64_t ua = (uint64_t)a;
    uint64_t ub = (uint64_t)b;
    uint64_t ur = ua - ub;
    if (((ua ^ ub) & (ua ^ ur)) >> 63ULL) {
        return (a < 0) ? INT64_MIN : INT64_MAX;
    }
    return (int64_t)ur;
}

static inline int32_t sc_mul_q16(int32_t a, int32_t b) {
    int64_t prod = ((int64_t)a * (int64_t)b) >> 16;
    if (prod > (int64_t)BLOOM_MAX_Q16) return BLOOM_MAX_Q16;
    if (prod < -(int64_t)BLOOM_MAX_Q16) return -BLOOM_MAX_Q16;
    return (int32_t)prod;
}

static inline uint32_t sc_read_u32_le(const uint8_t *p) {
    return ((uint32_t)p[0]) |
           ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static void sc_stamp_mac(void *block64, uint64_t seed, uint32_t *out_lo, uint32_t *out_hi) {
    uint64_t mac = bloom_hash56_fnv1a(block64, seed);
    *out_lo = (uint32_t)(mac & 0xFFFFFFFFULL);
    *out_hi = (uint32_t)(mac >> 32ULL);
}

static uint64_t sc_isqrt_u64(uint64_t n) {
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

/* Exact Integer Square Root: floor(sqrt(n)), saturated to BLOOM_MAX_Q16 */
int32_t bloom_isqrt_u64_to_i32(uint64_t n) {
    if (n >= (uint64_t)BLOOM_MAX_Q16 * (uint64_t)BLOOM_MAX_Q16) {
        return BLOOM_MAX_Q16;
    }
    uint64_t x = sc_isqrt_u64(n);
    if (x > (uint64_t)BLOOM_MAX_Q16) return BLOOM_MAX_Q16;
    return (int32_t)x;
}

static int64_t sc_len2_q16(int64_t x, int64_t y) {
    uint64_t ax = (uint64_t)sc_abs64(x);
    uint64_t ay = (uint64_t)sc_abs64(y);
    uint32_t sh = 0U;
    while (((ax | ay) >> sh) > 0x3FFFFFFFULL) {
        ++sh;
    }
    uint64_t sx = ax >> sh;
    uint64_t sy = ay >> sh;
    uint64_t r = sc_isqrt_u64(sx * sx + sy * sy) << sh;
    if (r > (uint64_t)INT64_MAX) return INT64_MAX;
    return (int64_t)r;
}

static int64_t sc_len3_q16(int64_t x, int64_t y, int64_t z) {
    uint64_t ax = (uint64_t)sc_abs64(x);
    uint64_t ay = (uint64_t)sc_abs64(y);
    uint64_t az = (uint64_t)sc_abs64(z);
    uint32_t sh = 0U;
    while (((ax | ay | az) >> sh) > 0x3FFFFFFFULL) {
        ++sh;
    }
    uint64_t sx = ax >> sh;
    uint64_t sy = ay >> sh;
    uint64_t sz = az >> sh;
    uint64_t r = sc_isqrt_u64(sx * sx + sy * sy + sz * sz) << sh;
    if (r > (uint64_t)INT64_MAX) return INT64_MAX;
    return (int64_t)r;
}

/* Bhaskara / Parabolic Q16.16 Sine & Cosine (angle_q16: 65536 = 1 turn = 2*pi, or radians scaled)
   Convention: angle_q16 is radians in Q16.16 (2*pi ~= 411775, pi ~= 205887, pi/2 ~= 102944) */
#define BLOOM_TWO_PI_Q16  411775
#define BLOOM_PI_Q16      205887
#define BLOOM_HALF_PI_Q16 102944

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
    /* Parabolic + 4th-order correction on [0, pi/2]: sin(x) ~= (4*x*(pi - x))/pi^2 * (0.775 + 0.225*p) */
    int64_t num = 4LL * a * ((int64_t)BLOOM_PI_Q16 - a);
    int64_t denom = ((int64_t)BLOOM_PI_Q16 * (int64_t)BLOOM_PI_Q16) >> 16;
    int64_t p = (denom > 0) ? ((num) / denom) : 0; /* in [0, 65536] */
    int64_t corr = ((p * p) >> 16);
    int64_t y = ((50790LL * p + 14746LL * corr) >> 16) * sign;
    return sc_clamp32((int32_t)y, -BLOOM_Q16_ONE, BLOOM_Q16_ONE);
}

int32_t bloom_cos_q16(int32_t angle_q16) {
    int64_t shifted = ((int64_t)angle_q16 + (int64_t)BLOOM_HALF_PI_Q16) % (int64_t)BLOOM_TWO_PI_Q16;
    return bloom_sin_q16((int32_t)shifted);
}

/* Pure Integer IEEE-754 Binary32 -> Q16.16 Converter (0 FPU Instructions) */
static inline bool sc_ieee754_finite_to_q16(uint32_t bits, int32_t *out_q16) {
    uint32_t sign = (bits >> 31U) & 1U;
    uint32_t exp_raw = (bits >> 23U) & 0xFFU;
    uint32_t mant = bits & 0x7FFFFFU;

    if (exp_raw == 0xFFU) return false;
    if (exp_raw == 0U) {
        *out_q16 = 0;
        return true;
    }

    int32_t exp_unbiased = (int32_t)exp_raw - 127;
    uint64_t sig24 = (1ULL << 23U) | (uint64_t)mant;
    /* Value = sig24 * 2^(exp_unbiased - 23); Q16 = sig24 * 2^(exp_unbiased - 7) */
    int32_t shift = exp_unbiased - 7;
    uint64_t q16_mag = 0ULL;
    if (shift >= 31) {
        q16_mag = (uint64_t)BLOOM_MAX_Q16;
    } else if (shift >= 0) {
        q16_mag = sig24 << (uint32_t)shift;
        if (q16_mag > (uint64_t)BLOOM_MAX_Q16) q16_mag = (uint64_t)BLOOM_MAX_Q16;
    } else if (shift > -25) {
        uint32_t rsh = (uint32_t)(-shift);
        q16_mag = (sig24 + (1ULL << (rsh - 1U))) >> rsh;
    } else {
        q16_mag = 0ULL;
    }

    *out_q16 = sign ? -(int32_t)q16_mag : (int32_t)q16_mag;
    return true;
}

int32_t bloom_ieee754_bits_to_q16(uint32_t bits) {
    int32_t q = 0;
    if (sc_ieee754_finite_to_q16(bits, &q)) return q;
    if ((bits & 0x7FFFFFU) != 0U) return 0; /* NaN -> 0 */
    return ((bits >> 31U) & 1U) ? -BLOOM_MAX_Q16 : BLOOM_MAX_Q16;
}

int64_t bloom_smin_q16(int64_t a_q16, int64_t b_q16, int64_t k_q16) {
    int64_t mn = sc_min64(a_q16, b_q16);
    if (k_q16 <= 0) return mn;
    uint64_t uk = (k_q16 > (int64_t)BLOOM_MAX_Q16) ? (uint64_t)BLOOM_MAX_Q16 : (uint64_t)k_q16;
    uint64_t diff = (a_q16 >= b_q16)
        ? ((uint64_t)a_q16 - (uint64_t)b_q16)
        : ((uint64_t)b_q16 - (uint64_t)a_q16);
    if (diff >= uk) return mn;
    uint64_t rem = uk - diff;
    uint64_t num = rem * rem;
    uint64_t den = 4ULL * uk;
    return sc_sat_sub64(mn, (int64_t)(num / den));
}

int64_t bloom_smax_q16(int64_t a_q16, int64_t b_q16, int64_t k_q16) {
    return sc_neg64(bloom_smin_q16(sc_neg64(a_q16), sc_neg64(b_q16), k_q16));
}

/* 1. CSG Primitive & Binary glTF (.glb) Ingestion */
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
    sc_stamp_mac(out_op, 0xC5600064ULL, &out_op->mac_lo32, &out_op->mac_hi32);
}

bool bloom_glb_parse_to_csg64(const uint8_t *glb_bytes,
                              size_t glb_len,
                              uint32_t op_id,
                              uint32_t matter_id,
                              BloomCsgOp64 *out_op,
                              uint32_t *out_vertex_count) {
    if (!glb_bytes || glb_len < 28U || !out_op) return false;
    /* glTF 2.0 Magic: "glTF" (0x46546C67), Version 2 */
    if (sc_read_u32_le(glb_bytes + 0U) != 0x46546C67U) return false;
    if (sc_read_u32_le(glb_bytes + 4U) != 2U) return false;
    uint64_t total_len = (uint64_t)sc_read_u32_le(glb_bytes + 8U);
    if (total_len > (uint64_t)glb_len || total_len < 28ULL || (total_len & 3ULL) != 0ULL) {
        return false;
    }

    /* Chunk 0: JSON (0x4E4F534A), 4-byte aligned, well-formed JSON object */
    uint64_t c0_len = (uint64_t)sc_read_u32_le(glb_bytes + 12U);
    uint32_t c0_type = sc_read_u32_le(glb_bytes + 16U);
    if (c0_type != 0x4E4F534AU || c0_len == 0ULL || (c0_len & 3ULL) != 0ULL) {
        return false;
    }
    if (20ULL + c0_len + 8ULL > total_len) return false;

    const uint8_t *jbuf = glb_bytes + 20U;
    uint64_t jfirst = 0ULL;
    while (jfirst < c0_len &&
           (jbuf[jfirst] == ' ' || jbuf[jfirst] == '\t' ||
            jbuf[jfirst] == '\r' || jbuf[jfirst] == '\n')) {
        ++jfirst;
    }
    if (jfirst >= c0_len || jbuf[jfirst] != '{') return false;
    uint64_t jlast = c0_len;
    while (jlast > jfirst &&
           (jbuf[jlast - 1ULL] == ' ' || jbuf[jlast - 1ULL] == '\t' ||
            jbuf[jlast - 1ULL] == '\r' || jbuf[jlast - 1ULL] == '\n')) {
        --jlast;
    }
    if (jlast <= jfirst + 1ULL || jbuf[jlast - 1ULL] != '}') return false;
    for (uint64_t k = 0ULL; k < c0_len; ++k) {
        if (jbuf[k] == 0U) return false;
    }

    /* Chunk 1: BIN (0x004E4942) containing packed IEEE-754 float32 (x,y,z) vertices */
    uint64_t c1_hdr = 20ULL + c0_len;
    uint64_t c1_len = (uint64_t)sc_read_u32_le(glb_bytes + (size_t)c1_hdr);
    uint32_t c1_type = sc_read_u32_le(glb_bytes + (size_t)c1_hdr + 4U);
    if (c1_type != 0x004E4942U || c1_len < 12ULL || (c1_len & 3ULL) != 0ULL) {
        return false;
    }
    if (c1_hdr + 8ULL + c1_len > total_len) return false;

    const uint8_t *bin = glb_bytes + (size_t)c1_hdr + 8U;
    uint32_t vcount = (uint32_t)(c1_len / 12ULL);
    if (vcount == 0U) return false;
    int32_t bmin[3] = { BLOOM_MAX_Q16, BLOOM_MAX_Q16, BLOOM_MAX_Q16 };
    int32_t bmax[3] = { -BLOOM_MAX_Q16, -BLOOM_MAX_Q16, -BLOOM_MAX_Q16 };

    for (uint32_t i = 0U; i < vcount; ++i) {
        size_t base_off = (size_t)i * 12U;
        for (uint32_t a = 0U; a < 3U; ++a) {
            uint32_t raw_bits = sc_read_u32_le(bin + base_off + (size_t)a * 4U);
            int32_t v = 0;
            if (!sc_ieee754_finite_to_q16(raw_bits, &v)) return false;
            if (v < bmin[a]) bmin[a] = v;
            if (v > bmax[a]) bmax[a] = v;
        }
    }

    int32_t center[3];
    int32_t ext[3];
    for (uint32_t a = 0U; a < 3U; ++a) {
        center[a] = sc_clamp64_to_i32(((int64_t)bmin[a] + (int64_t)bmax[a]) / 2LL);
        int64_t half_e = ((int64_t)bmax[a] - (int64_t)bmin[a]) / 2LL;
        ext[a] = (half_e > 256) ? sc_clamp64_to_i32(half_e) : 256;
    }

    bloom_csg_op_init64(out_op, op_id, BLOOM_CSG_PRIM_BOX, BLOOM_CSG_BOOL_UNION,
                        matter_id, center, ext, 0, 2048);
    if (out_vertex_count) *out_vertex_count = vcount;
    return true;
}

#define SC_MAX_COORD_Q16 INT64_C(0x0FFFFFFFFFFF)

int64_t bloom_csg_eval_single_q16(const BloomCsgOp64 *op,
                                  int64_t px_q16,
                                  int64_t py_q16,
                                  int64_t pz_q16) {
    if (!op || op->prim_type == BLOOM_CSG_PRIM_NONE) return (int64_t)BLOOM_MAX_Q16;
    int64_t cpx = sc_clamp64(px_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t cpy = sc_clamp64(py_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t cpz = sc_clamp64(pz_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t lx = cpx - (int64_t)op->pos_q16[0];
    int64_t ly = cpy - (int64_t)op->pos_q16[1];
    int64_t lz = cpz - (int64_t)op->pos_q16[2];
    int64_t ex = sc_max64((int64_t)op->extents_q16[0], 256);
    int64_t ey = sc_max64((int64_t)op->extents_q16[1], 256);
    int64_t ez = sc_max64((int64_t)op->extents_q16[2], 256);
    int64_t bevel = sc_max64((int64_t)op->bevel_r_q16, 0);

    switch (op->prim_type) {
    case BLOOM_CSG_PRIM_SPHERE:
        return sc_len3_q16(lx, ly, lz) - ex;

    case BLOOM_CSG_PRIM_TORUS: {
        int64_t qx = sc_len2_q16(lx, lz) - ex;
        return sc_len2_q16(qx, ly) - ey;
    }

    case BLOOM_CSG_PRIM_BOX: {
        int64_t qx = sc_abs64(lx) - sc_max64(ex - bevel, 0);
        int64_t qy = sc_abs64(ly) - sc_max64(ey - bevel, 0);
        int64_t qz = sc_abs64(lz) - sc_max64(ez - bevel, 0);
        int64_t out_d = sc_len3_q16(sc_max64(qx, 0), sc_max64(qy, 0), sc_max64(qz, 0));
        int64_t in_d = sc_min64(sc_max64(qx, sc_max64(qy, qz)), 0);
        return out_d + in_d - bevel;
    }

    case BLOOM_CSG_PRIM_OCTAHEDRON:
        /* Exact L1 bound scaled by 1/sqrt(3) ~= 37837 / 65536 */
        return ((sc_abs64(lx) + sc_abs64(ly) + sc_abs64(lz) - ex) * 37837LL) >> 16;

    case BLOOM_CSG_PRIM_CYLINDER: {
        int64_t dr = sc_len2_q16(lx, lz) - ex;
        int64_t dy = sc_abs64(ly) - ey;
        int64_t out_d = sc_len2_q16(sc_max64(dr, 0), sc_max64(dy, 0));
        int64_t in_d = sc_min64(sc_max64(dr, dy), 0);
        return out_d + in_d - bevel;
    }

    case BLOOM_CSG_PRIM_CAPSULE: {
        int64_t cy = ly;
        if (cy > ey) cy = ey;
        if (cy < -ey) cy = -ey;
        return sc_len3_q16(lx, ly - cy, lz) - ex;
    }

    default:
        return (int64_t)BLOOM_MAX_Q16;
    }
}

int64_t bloom_csg_eval_program_q16(const BloomCsgOp64 *ops,
                                   size_t op_count,
                                   int64_t px_q16,
                                   int64_t py_q16,
                                   int64_t pz_q16,
                                   uint32_t *out_matter_id) {
    int64_t d_acc = (int64_t)BLOOM_MAX_Q16;
    uint32_t mat = 0U;
    if (!ops || op_count == 0U) {
        if (out_matter_id) *out_matter_id = 0U;
        return d_acc;
    }

    for (size_t i = 0U; i < op_count; ++i) {
        int64_t d = bloom_csg_eval_single_q16(&ops[i], px_q16, py_q16, pz_q16);
        if (i == 0U) {
            d_acc = d;
            mat = ops[i].matter_id;
            continue;
        }
        switch (ops[i].bool_op) {
        case BLOOM_CSG_BOOL_UNION:
            if (d < d_acc) {
                d_acc = d;
                mat = ops[i].matter_id;
            }
            break;
        case BLOOM_CSG_BOOL_SUBTRACT:
            d_acc = sc_max64(d_acc, sc_neg64(d));
            break;
        case BLOOM_CSG_BOOL_INTERSECT:
            if (d > d_acc) {
                d_acc = d;
                mat = ops[i].matter_id;
            }
            break;
        case BLOOM_CSG_BOOL_SMOOTH_UNION: {
            int64_t sm = bloom_smin_q16(d_acc, d, (int64_t)ops[i].smooth_k_q16);
            if (d < d_acc) mat = ops[i].matter_id;
            d_acc = sm;
            break;
        }
        default:
            break;
        }
    }
    if (out_matter_id) *out_matter_id = mat;
    return d_acc;
}

/* 2. 60-Bit 3D Morton Z-Order & Pointer-Free Linear BVH */
static uint64_t sc_expand_bits_20_to_60(uint32_t v20) {
    uint64_t x = (uint64_t)(v20 & 0xFFFFFU);
    x = (x | (x << 32ULL)) & UINT64_C(0x001F00000000FFFF);
    x = (x | (x << 16ULL)) & UINT64_C(0x001F0000FF0000FF);
    x = (x | (x << 8ULL))  & UINT64_C(0x100F00F00F00F00F);
    x = (x | (x << 4ULL))  & UINT64_C(0x10C30C30C30C30C3);
    x = (x | (x << 2ULL))  & UINT64_C(0x1249249249249249);
    return x;
}

uint64_t bloom_bvh_morton3d_q16(int32_t x_q16, int32_t y_q16, int32_t z_q16) {
    /* Map [-64.0 .. +64.0] Q16 ([-4194304 .. +4194303]) into 20-bit unsigned [0 .. 1048575] */
    int32_t cx = sc_clamp32(x_q16, -4194304, 4194303);
    int32_t cy = sc_clamp32(y_q16, -4194304, 4194303);
    int32_t cz = sc_clamp32(z_q16, -4194304, 4194303);
    uint32_t ux = (uint32_t)((cx + 4194304) >> 3);
    uint32_t uy = (uint32_t)((cy + 4194304) >> 3);
    uint32_t uz = (uint32_t)((cz + 4194304) >> 3);
    return (sc_expand_bits_20_to_60(ux) << 2ULL) |
           (sc_expand_bits_20_to_60(uy) << 1ULL) |
           (sc_expand_bits_20_to_60(uz) << 0ULL);
}

static void sc_csg_bounds_q16(const BloomCsgOp64 *op, int32_t out_min[3], int32_t out_max[3]) {
    int64_t ex = sc_max64((int64_t)op->extents_q16[0], 256);
    int64_t ey = sc_max64((int64_t)op->extents_q16[1], 256);
    int64_t ez = sc_max64((int64_t)op->extents_q16[2], 256);
    int64_t bevel = sc_max64((int64_t)op->bevel_r_q16, 0);
    int64_t sk = sc_max64((int64_t)op->smooth_k_q16, 0);
    int64_t hx = 0;
    int64_t hy = 0;
    int64_t hz = 0;

    switch (op->prim_type) {
    case BLOOM_CSG_PRIM_SPHERE:
        hx = ex; hy = ex; hz = ex;
        break;
    case BLOOM_CSG_PRIM_TORUS:
        hx = ex + ey; hy = ey; hz = ex + ey;
        break;
    case BLOOM_CSG_PRIM_BOX:
        hx = sc_max64(ex, bevel);
        hy = sc_max64(ey, bevel);
        hz = sc_max64(ez, bevel);
        break;
    case BLOOM_CSG_PRIM_OCTAHEDRON:
        hx = ex; hy = ex; hz = ex;
        break;
    case BLOOM_CSG_PRIM_CYLINDER:
        hx = ex + bevel;
        hy = ey + bevel;
        hz = ex + bevel;
        break;
    case BLOOM_CSG_PRIM_CAPSULE:
        hx = ex;
        hy = ey + ex;
        hz = ex;
        break;
    default:
        hx = sc_max64(ex, sc_max64(ey, ez)) + bevel;
        hy = hx;
        hz = hx;
        break;
    }

    int64_t half_ext[3] = {
        hx + sk + 4096LL,
        hy + sk + 4096LL,
        hz + sk + 4096LL
    };
    for (uint32_t a = 0U; a < 3U; ++a) {
        int64_t mn = (int64_t)op->pos_q16[a] - half_ext[a];
        int64_t mx = (int64_t)op->pos_q16[a] + half_ext[a];
        out_min[a] = sc_clamp64_to_i32(mn);
        out_max[a] = sc_clamp64_to_i32(mx);
    }
}

static uint32_t sc_build_bvh_rec(const BloomCsgOp64 *ops,
                                 const uint32_t *sorted_idx,
                                 const uint64_t *sorted_keys,
                                 bool use_morton_leaves,
                                 uint32_t start,
                                 uint32_t end,
                                 uint32_t budget,
                                 BloomBvhNode64 *out_nodes,
                                 uint32_t *inout_count,
                                 uint32_t max_nodes) {
    if (*inout_count >= max_nodes || start >= end || budget == 0U) return 0U;
    uint32_t my_idx = (*inout_count)++;
    BloomBvhNode64 *nd = &out_nodes[my_idx];
    memset(nd, 0, sizeof(*nd));
    nd->node_id = my_idx;

    int32_t bmin[3] = { BLOOM_MAX_Q16, BLOOM_MAX_Q16, BLOOM_MAX_Q16 };
    int32_t bmax[3] = { -BLOOM_MAX_Q16, -BLOOM_MAX_Q16, -BLOOM_MAX_Q16 };
    for (uint32_t i = start; i < end; ++i) {
        uint32_t op_idx = use_morton_leaves ? sorted_idx[i] : i;
        int32_t omin[3], omax[3];
        sc_csg_bounds_q16(&ops[op_idx], omin, omax);
        for (uint32_t a = 0U; a < 3U; ++a) {
            if (omin[a] < bmin[a]) bmin[a] = omin[a];
            if (omax[a] > bmax[a]) bmax[a] = omax[a];
        }
    }
    for (uint32_t a = 0U; a < 3U; ++a) {
        nd->aabb_min_q16[a] = bmin[a];
        nd->aabb_max_q16[a] = bmax[a];
    }
    uint64_t mkey = use_morton_leaves ? sorted_keys[start]
                                      : bloom_bvh_morton3d_q16(ops[start].pos_q16[0],
                                                               ops[start].pos_q16[1],
                                                               ops[start].pos_q16[2]);
    nd->morton_lo32 = (uint32_t)(mkey & 0xFFFFFFFFULL);
    nd->morton_hi32 = (uint32_t)(mkey >> 32ULL);
    nd->stage_mask_lo32 = 0xFFFFFFFFU;
    nd->stage_mask_hi32 = 0xFFFFFFFFU;

    uint32_t span = end - start;
    if (span == 1U || budget < 3U || (*inout_count + 2U) > max_nodes) {
        nd->is_leaf = 1U;
        nd->left_or_first = use_morton_leaves ? sorted_idx[start] : start;
        nd->right_or_count = span;
    } else {
        uint32_t mid = start + (span >> 1U);
        uint32_t rem = budget - 1U;
        uint32_t left_b = rem >> 1U;
        uint32_t right_b = rem - left_b;
        nd->is_leaf = 0U;
        nd->left_or_first = sc_build_bvh_rec(ops, sorted_idx, sorted_keys, use_morton_leaves,
                                             start, mid, left_b, out_nodes, inout_count, max_nodes);
        nd->right_or_count = sc_build_bvh_rec(ops, sorted_idx, sorted_keys, use_morton_leaves,
                                              mid, end, right_b, out_nodes, inout_count, max_nodes);
    }
    sc_stamp_mac(nd, 0xBF400064ULL, &nd->mac_lo32, &nd->mac_hi32);
    return my_idx;
}

size_t bloom_bvh_build_from_csg64(const BloomCsgOp64 *ops,
                                  size_t op_count,
                                  BloomBvhNode64 *out_nodes,
                                  size_t max_nodes) {
    if (!ops || op_count == 0U || !out_nodes || max_nodes == 0U) return 0U;
    uint32_t n = (op_count > BLOOM_MAX_CSG_OPS) ? BLOOM_MAX_CSG_OPS : (uint32_t)op_count;
    uint32_t cap = (max_nodes > 0xFFFFU) ? 0xFFFFU : (uint32_t)max_nodes;
    bool use_morton_leaves = ((2U * n - 1U) <= cap);

    uint32_t idx[BLOOM_MAX_CSG_OPS];
    uint64_t keys[BLOOM_MAX_CSG_OPS];
    for (uint32_t i = 0U; i < n; ++i) {
        idx[i] = i;
        keys[i] = bloom_bvh_morton3d_q16(ops[i].pos_q16[0], ops[i].pos_q16[1], ops[i].pos_q16[2]);
    }
    if (use_morton_leaves) {
        /* Insertion sort on stack (<= 32 ops, 0 heap) when every leaf has span == 1 */
        for (uint32_t i = 1U; i < n; ++i) {
            uint64_t k = keys[i];
            uint32_t id = idx[i];
            uint32_t j = i;
            while (j > 0U && keys[j - 1U] > k) {
                keys[j] = keys[j - 1U];
                idx[j] = idx[j - 1U];
                --j;
            }
            keys[j] = k;
            idx[j] = id;
        }
    }

    uint32_t built = 0U;
    (void)sc_build_bvh_rec(ops, idx, keys, use_morton_leaves, 0U, n, cap,
                           out_nodes, &built, cap);
    return (size_t)built;
}

static int64_t sc_aabb_lower_bound_q16(const BloomBvhNode64 *nd,
                                       int64_t cpx,
                                       int64_t cpy,
                                       int64_t cpz,
                                       bool has_octa) {
    int64_t qx = sc_max64((int64_t)nd->aabb_min_q16[0] - cpx, cpx - (int64_t)nd->aabb_max_q16[0]);
    int64_t qy = sc_max64((int64_t)nd->aabb_min_q16[1] - cpy, cpy - (int64_t)nd->aabb_max_q16[1]);
    int64_t qz = sc_max64((int64_t)nd->aabb_min_q16[2] - cpz, cpz - (int64_t)nd->aabb_max_q16[2]);
    int64_t dx = sc_max64(qx, 0);
    int64_t dy = sc_max64(qy, 0);
    int64_t dz = sc_max64(qz, 0);
    int64_t in_d = sc_min64(sc_max64(qx, sc_max64(qy, qz)), 0);
    int64_t out_d = has_octa
        ? (((dx + dy + dz) * 37837LL) >> 16)
        : sc_len3_q16(dx, dy, dz);
    uint64_t max_d = (uint64_t)sc_max64(dx, sc_max64(dy, dz));
    int64_t eps = 8LL;
    if (max_d > 0x3FFFFFFFULL) {
        uint32_t sh = 0U;
        while ((max_d >> sh) > 0x3FFFFFFFULL) ++sh;
        eps = 8LL + ((int64_t)1LL << (sh + 2U));
    }
    return out_d + in_d - eps;
}

int64_t bloom_bvh_eval_csg_q16(const BloomBvhNode64 *nodes,
                               size_t node_count,
                               const BloomCsgOp64 *ops,
                               size_t op_count,
                               int64_t px_q16,
                               int64_t py_q16,
                               int64_t pz_q16,
                               uint32_t *out_visited_nodes) {
    if (!nodes || node_count == 0U || !ops || op_count == 0U) {
        if (out_visited_nodes) *out_visited_nodes = 0U;
        return (int64_t)BLOOM_MAX_Q16;
    }

    bool pure_union = (op_count <= BLOOM_MAX_CSG_OPS);
    bool has_octa = false;
    for (size_t i = 0U; i < op_count; ++i) {
        if (i > 0U && ops[i].bool_op != BLOOM_CSG_BOOL_UNION) {
            pure_union = false;
            break;
        }
        if (ops[i].prim_type == BLOOM_CSG_PRIM_OCTAHEDRON) {
            has_octa = true;
        }
    }
    /* Guard against int32_t AABB saturation on extreme INT32_MIN/MAX coordinates */
    for (uint32_t a = 0U; a < 3U && pure_union; ++a) {
        if (nodes[0].aabb_min_q16[a] <= -BLOOM_MAX_Q16 ||
            nodes[0].aabb_max_q16[a] >= BLOOM_MAX_Q16) {
            pure_union = false;
        }
    }
    if (!pure_union) {
        if (out_visited_nodes) *out_visited_nodes = 1U;
        return bloom_csg_eval_program_q16(ops, op_count, px_q16, py_q16, pz_q16, NULL);
    }

    int64_t cpx = sc_clamp64(px_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t cpy = sc_clamp64(py_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t cpz = sc_clamp64(pz_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);

    uint32_t stack[64];
    uint32_t sp = 0U;
    stack[sp++] = 0U;
    int64_t best_d = (int64_t)BLOOM_MAX_Q16;
    uint32_t visited = 0U;

    while (sp > 0U && visited <= (uint32_t)(node_count * 2U)) {
        uint32_t nid = stack[--sp];
        if (nid >= node_count) continue;
        const BloomBvhNode64 *nd = &nodes[nid];
        ++visited;

        if (sc_aabb_lower_bound_q16(nd, cpx, cpy, cpz, has_octa) >= best_d) {
            continue;
        }

        if (nd->is_leaf != 0U) {
            uint32_t first = nd->left_or_first;
            uint32_t cnt = nd->right_or_count;
            for (uint32_t k = 0U; k < cnt && ((size_t)first + (size_t)k) < op_count; ++k) {
                int64_t d = bloom_csg_eval_single_q16(&ops[(size_t)first + (size_t)k], px_q16, py_q16, pz_q16);
                if (d < best_d) best_d = d;
            }
        } else {
            uint32_t l = nd->left_or_first;
            uint32_t r = nd->right_or_count;
            int64_t dl = (l > nid && l < node_count) ? sc_aabb_lower_bound_q16(&nodes[l], cpx, cpy, cpz, has_octa) : best_d;
            int64_t dr = (r > nid && r < node_count) ? sc_aabb_lower_bound_q16(&nodes[r], cpx, cpy, cpz, has_octa) : best_d;
            if (dl < dr) {
                if (dr < best_d && sp < 64U) stack[sp++] = r;
                if (dl < best_d && sp < 64U) stack[sp++] = l;
            } else {
                if (dl < best_d && sp < 64U) stack[sp++] = l;
                if (dr < best_d && sp < 64U) stack[sp++] = r;
            }
        }
    }

    if (out_visited_nodes) *out_visited_nodes = visited;
    return best_d;
}

/* 3. Volume-Preserving Q16.16 Dual-Quaternion Skeletal Skinning */
static void sc_quat_normalize_q16(int32_t q[4]) {
    uint64_t max_a = 0ULL;
    for (int i = 0; i < 4; ++i) {
        uint64_t a = (uint64_t)sc_abs64((int64_t)q[i]);
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
    uint64_t len = sc_isqrt_u64(n2);
    if (len == 0ULL) {
        q[0] = BLOOM_Q16_ONE;
        q[1] = q[2] = q[3] = 0;
        return;
    }
    for (int i = 0; i < 4; ++i) {
        int64_t norm_i = (sv[i] * 65536LL) / (int64_t)len;
        q[i] = sc_clamp32((int32_t)norm_i, -BLOOM_Q16_ONE, BLOOM_Q16_ONE);
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
    sc_quat_normalize_q16(out_joint->q_real_q16);

    int64_t tx = trans_q16 ? (int64_t)trans_q16[0] : 0;
    int64_t ty = trans_q16 ? (int64_t)trans_q16[1] : 0;
    int64_t tz = trans_q16 ? (int64_t)trans_q16[2] : 0;
    int64_t qw = out_joint->q_real_q16[0];
    int64_t qx = out_joint->q_real_q16[1];
    int64_t qy = out_joint->q_real_q16[2];
    int64_t qz = out_joint->q_real_q16[3];

    /* q_dual = 0.5 * (0, tx, ty, tz) * q_real */
    out_joint->q_dual_q16[0] = sc_clamp64_to_i32((-(tx * qx + ty * qy + tz * qz)) >> 17);
    out_joint->q_dual_q16[1] = sc_clamp64_to_i32(((tx * qw + ty * qz - tz * qy)) >> 17);
    out_joint->q_dual_q16[2] = sc_clamp64_to_i32(((-tx * qz + ty * qw + tz * qx)) >> 17);
    out_joint->q_dual_q16[3] = sc_clamp64_to_i32(((tx * qy - ty * qx + tz * qw)) >> 17);

    out_joint->bone_len_q16 = bone_len_q16;
    out_joint->bone_radius_q16 = bone_radius_q16;
    out_joint->smooth_k_q16 = smooth_k_q16;
    out_joint->matter_id = matter_id;
    sc_stamp_mac(out_joint, 0xD0640064ULL, &out_joint->mac_lo32, &out_joint->mac_hi32);
}

void bloom_joint_dq64_blend(const BloomJointDq64 *a,
                            const BloomJointDq64 *b,
                            int32_t t_q16,
                            BloomJointDq64 *out_blended) {
    if (!a || !b || !out_blended) return;
    int64_t tc = (int64_t)sc_clamp32(t_q16, 0, BLOOM_Q16_ONE);
    int64_t wa = (int64_t)BLOOM_Q16_ONE - tc;
    int64_t wb = tc;

    /* Shortest-arc antipodal sign check: dot(a.q_real, b.q_real) < 0 -> negate wb */
    uint64_t max_ab = 0ULL;
    for (int i = 0; i < 4; ++i) {
        uint64_t ma = (uint64_t)sc_abs64((int64_t)a->q_real_q16[i]);
        uint64_t mb = (uint64_t)sc_abs64((int64_t)b->q_real_q16[i]);
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
        uint64_t mr = (uint64_t)sc_abs64(r64[i]);
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
    uint64_t inv_len = sc_isqrt_u64(n2) << rsh;
    if (inv_len > 0ULL) {
        for (int i = 0; i < 4; ++i) {
            int64_t nr = (r64[i] * 65536LL) / (int64_t)inv_len;
            int64_t nd = (sc_clamp64(d64[i], -0x7FFFFFFFLL, 0x7FFFFFFFLL) * 65536LL) / (int64_t)inv_len;
            out_blended->q_real_q16[i] = sc_clamp32((int32_t)nr, -BLOOM_Q16_ONE, BLOOM_Q16_ONE);
            out_blended->q_dual_q16[i] = sc_clamp64_to_i32(nd);
        }
    } else {
        out_blended->q_real_q16[0] = BLOOM_Q16_ONE;
        out_blended->q_real_q16[1] = 0;
        out_blended->q_real_q16[2] = 0;
        out_blended->q_real_q16[3] = 0;
        for (int i = 0; i < 4; ++i) {
            out_blended->q_dual_q16[i] = sc_clamp64_to_i32(d64[i]);
        }
    }
    sc_stamp_mac(out_blended, 0xD0640064ULL, &out_blended->mac_lo32, &out_blended->mac_hi32);
}

void bloom_joint_dq64_transform_q16(const BloomJointDq64 *joint,
                                    const int32_t in_pt_q16[3],
                                    int32_t out_pt_q16[3]) {
    if (!joint || !in_pt_q16 || !out_pt_q16) return;
    int64_t qw = sc_clamp64((int64_t)joint->q_real_q16[0], -131072LL, 131072LL);
    int64_t ux = sc_clamp64((int64_t)joint->q_real_q16[1], -131072LL, 131072LL);
    int64_t uy = sc_clamp64((int64_t)joint->q_real_q16[2], -131072LL, 131072LL);
    int64_t uz = sc_clamp64((int64_t)joint->q_real_q16[3], -131072LL, 131072LL);

    int64_t dw = (int64_t)joint->q_dual_q16[0];
    int64_t dx = (int64_t)joint->q_dual_q16[1];
    int64_t dy = (int64_t)joint->q_dual_q16[2];
    int64_t dz = (int64_t)joint->q_dual_q16[3];

    int64_t px = (int64_t)in_pt_q16[0];
    int64_t py = (int64_t)in_pt_q16[1];
    int64_t pz = (int64_t)in_pt_q16[2];

    /* u x p + qw * p */
    int64_t cx = ((uy * pz - uz * py) + qw * px) >> 16;
    int64_t cy = ((uz * px - ux * pz) + qw * py) >> 16;
    int64_t cz = ((ux * py - uy * px) + qw * pz) >> 16;

    /* p_rot = p + 2 * (u x c) */
    int64_t rx = px + (((uy * cz - uz * cy)) >> 15);
    int64_t ry = py + (((uz * cx - ux * cz)) >> 15);
    int64_t rz = pz + (((ux * cy - uy * cx)) >> 15);

    /* t = 2 * (-dw * u + qw * d_vec - d_vec x u) */
    int64_t tx = ((-dw * ux + qw * dx - (dy * uz - dz * uy))) >> 15;
    int64_t ty = ((-dw * uy + qw * dy - (dz * ux - dx * uz))) >> 15;
    int64_t tz = ((-dw * uz + qw * dz - (dx * uy - dy * ux))) >> 15;

    out_pt_q16[0] = sc_clamp64_to_i32(rx + tx);
    out_pt_q16[1] = sc_clamp64_to_i32(ry + ty);
    out_pt_q16[2] = sc_clamp64_to_i32(rz + tz);
}

int64_t bloom_skeleton_eval_sdf_q16(const BloomJointDq64 *joints,
                                    size_t joint_count,
                                    int64_t px_q16,
                                    int64_t py_q16,
                                    int64_t pz_q16) {
    if (!joints || joint_count == 0U) return (int64_t)BLOOM_MAX_Q16;
    int64_t cpx = sc_clamp64(px_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t cpy = sc_clamp64(py_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t cpz = sc_clamp64(pz_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t d_acc = (int64_t)BLOOM_MAX_Q16;

    for (size_t i = 0U; i < joint_count; ++i) {
        int32_t blen = sc_clamp32(joints[i].bone_len_q16, -1024 * BLOOM_Q16_ONE, 1024 * BLOOM_Q16_ONE);
        int32_t z0[3] = { 0, 0, 0 };
        int32_t z1[3] = { 0, blen, 0 };
        int32_t a[3], b[3];
        bloom_joint_dq64_transform_q16(&joints[i], z0, a);
        bloom_joint_dq64_transform_q16(&joints[i], z1, b);

        int64_t pax = cpx - (int64_t)a[0];
        int64_t pay = cpy - (int64_t)a[1];
        int64_t paz = cpz - (int64_t)a[2];
        int64_t bax = (int64_t)b[0] - (int64_t)a[0];
        int64_t bay = (int64_t)b[1] - (int64_t)a[1];
        int64_t baz = (int64_t)b[2] - (int64_t)a[2];

        uint64_t m = (uint64_t)sc_abs64(pax) | (uint64_t)sc_abs64(pay) | (uint64_t)sc_abs64(paz) |
                     (uint64_t)sc_abs64(bax) | (uint64_t)sc_abs64(bay) | (uint64_t)sc_abs64(baz);
        uint32_t sh = 0U;
        while ((m >> sh) > 0x3FFFFFFFULL) {
            ++sh;
        }
        int64_t spx = (sh > 0U) ? (pax / ((int64_t)1LL << sh)) : pax;
        int64_t spy = (sh > 0U) ? (pay / ((int64_t)1LL << sh)) : pay;
        int64_t spz = (sh > 0U) ? (paz / ((int64_t)1LL << sh)) : paz;
        int64_t sbx = (sh > 0U) ? (bax / ((int64_t)1LL << sh)) : bax;
        int64_t sby = (sh > 0U) ? (bay / ((int64_t)1LL << sh)) : bay;
        int64_t sbz = (sh > 0U) ? (baz / ((int64_t)1LL << sh)) : baz;

        int64_t baba = sbx * sbx + sby * sby + sbz * sbz;
        int64_t h_q16 = 0;
        if (baba > 0) {
            int64_t paba = spx * sbx + spy * sby + spz * sbz;
            if (paba <= 0) {
                h_q16 = 0;
            } else if (paba >= baba) {
                h_q16 = BLOOM_Q16_ONE;
            } else if (baba > INT64_C(0x3FFFFFFFFFFF)) {
                h_q16 = ((paba >> 16) * 65536LL) / sc_max64(baba >> 16, 1);
            } else {
                h_q16 = (paba * 65536LL) / baba;
            }
        }
        int64_t dx = pax - ((bax * h_q16) >> 16);
        int64_t dy = pay - ((bay * h_q16) >> 16);
        int64_t dz = paz - ((baz * h_q16) >> 16);
        int64_t d_cap = sc_len3_q16(dx, dy, dz) - (int64_t)joints[i].bone_radius_q16;

        d_acc = (i == 0U) ? d_cap : bloom_smin_q16(d_acc, d_cap, (int64_t)joints[i].smooth_k_q16);
    }
    return d_acc;
}

/* 4. Sparse 4x4x4 4-Bit SDF Voxel Brick */
void bloom_sdf_brick64_pack(BloomSdfBrick64 *out_brick,
                            uint32_t brick_id,
                            uint32_t matter_id,
                            const int32_t origin_q16[3],
                            int32_t voxel_step_q16,
                            const int32_t sdf_samples_q16[64]) {
    if (!out_brick) return;
    memset(out_brick, 0, sizeof(*out_brick));
    out_brick->brick_id = brick_id;
    out_brick->matter_id = matter_id;
    if (origin_q16) {
        out_brick->origin_q16[0] = origin_q16[0];
        out_brick->origin_q16[1] = origin_q16[1];
        out_brick->origin_q16[2] = origin_q16[2];
    }
    int32_t step = (voxel_step_q16 > 256) ? voxel_step_q16 : 256;
    out_brick->voxel_step_q16 = step;

    if (sdf_samples_q16) {
        int64_t quant_unit = sc_max64((int64_t)(step >> 2), 64);
        for (uint32_t z = 0U; z < 4U; ++z) {
            uint64_t plane64 = 0ULL;
            for (uint32_t y = 0U; y < 4U; ++y) {
                for (uint32_t x = 0U; x < 4U; ++x) {
                    uint32_t flat = z * 16U + y * 4U + x;
                    int64_t q64 = ((int64_t)sdf_samples_q16[flat] / quant_unit) + 8LL;
                    int32_t q = (int32_t)sc_clamp64(q64, 0, 15);
                    uint32_t shift = (y * 4U + x) * 4U;
                    plane64 |= ((uint64_t)(uint32_t)q) << shift;
                }
            }
            out_brick->sdf_words[z * 2U + 0U] = (uint32_t)(plane64 & 0xFFFFFFFFULL);
            out_brick->sdf_words[z * 2U + 1U] = (uint32_t)(plane64 >> 32ULL);
        }
    }
    sc_stamp_mac(out_brick, 0xB41C0064ULL, &out_brick->mac_lo32, &out_brick->mac_hi32);
}

static int64_t sc_brick_voxel(const BloomSdfBrick64 *b, uint32_t x, uint32_t y, uint32_t z) {
    uint32_t idx16 = y * 4U + x;
    uint32_t word = b->sdf_words[z * 2U + (idx16 >> 3U)];
    uint32_t shift = (idx16 & 7U) * 4U;
    int32_t nibble = (int32_t)((word >> shift) & 0xFU) - 8;
    return ((int64_t)nibble * (int64_t)b->voxel_step_q16) >> 2;
}

static int64_t sc_brick_coord_u_q16(int64_t p_q16, int32_t orig_q16, uint64_t step) {
    const int64_t max_u = 3LL * BLOOM_Q16_ONE - 1LL;
    if (p_q16 <= (int64_t)orig_q16) return 0;
    uint64_t diff = (uint64_t)p_q16 - (uint64_t)(int64_t)orig_q16;
    if (diff >= 3ULL * step) return max_u;
    uint64_t u = (diff << 16ULL) / step;
    if (u > (uint64_t)max_u) return max_u;
    return (int64_t)u;
}

int64_t bloom_sdf_brick64_sample_q16(const BloomSdfBrick64 *brick,
                                     int64_t px_q16,
                                     int64_t py_q16,
                                     int64_t pz_q16) {
    if (!brick || brick->voxel_step_q16 <= 0) return (int64_t)BLOOM_MAX_Q16;
    uint64_t step = (uint64_t)(uint32_t)brick->voxel_step_q16;
    int64_t ux = sc_brick_coord_u_q16(px_q16, brick->origin_q16[0], step);
    int64_t uy = sc_brick_coord_u_q16(py_q16, brick->origin_q16[1], step);
    int64_t uz = sc_brick_coord_u_q16(pz_q16, brick->origin_q16[2], step);

    uint32_t x0 = (uint32_t)(ux >> 16), y0 = (uint32_t)(uy >> 16), z0 = (uint32_t)(uz >> 16);
    int64_t fx = ux & 0xFFFFLL, fy = uy & 0xFFFFLL, fz = uz & 0xFFFFLL;

    int64_t c000 = sc_brick_voxel(brick, x0,     y0,     z0);
    int64_t c100 = sc_brick_voxel(brick, x0 + 1, y0,     z0);
    int64_t c010 = sc_brick_voxel(brick, x0,     y0 + 1, z0);
    int64_t c110 = sc_brick_voxel(brick, x0 + 1, y0 + 1, z0);
    int64_t c001 = sc_brick_voxel(brick, x0,     y0,     z0 + 1);
    int64_t c101 = sc_brick_voxel(brick, x0 + 1, y0,     z0 + 1);
    int64_t c011 = sc_brick_voxel(brick, x0,     y0 + 1, z0 + 1);
    int64_t c111 = sc_brick_voxel(brick, x0 + 1, y0 + 1, z0 + 1);

    int64_t c00 = c000 + (((c100 - c000) * fx) >> 16);
    int64_t c10 = c010 + (((c110 - c010) * fx) >> 16);
    int64_t c01 = c001 + (((c101 - c001) * fx) >> 16);
    int64_t c11 = c011 + (((c111 - c011) * fx) >> 16);
    int64_t c0  = c00  + (((c10  - c00)  * fy) >> 16);
    int64_t c1  = c01  + (((c11  - c01)  * fy) >> 16);
    return c0 + (((c1 - c0) * fz) >> 16);
}

/* 5. Constitutive Physical Matter Optics & 128-Hex Wire Protocol */
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
    out_matter->ior_eta_q16 = sc_clamp32(ior_eta_q16, BLOOM_Q16_ONE, 4 * BLOOM_Q16_ONE);
    out_matter->extinction_kappa_q16 = sc_clamp32(extinction_kappa_q16, 0, 8 * BLOOM_Q16_ONE);
    out_matter->viscosity_quench_q16 = sc_clamp32(viscosity_quench_q16, 0, BLOOM_Q16_ONE);
    out_matter->surface_relief_q16 = sc_clamp32(surface_relief_q16, 0, 2 * BLOOM_Q16_ONE);
    out_matter->micro_roughness_q16 = sc_clamp32(micro_roughness_q16, 0, BLOOM_Q16_ONE);
    out_matter->mie_scatter_q16 = sc_clamp32(mie_scatter_q16, 0, BLOOM_Q16_ONE);
    if (absorb_rgb_q16) {
        for (int i = 0; i < 3; ++i) {
            out_matter->absorb_rgb_q16[i] = sc_clamp32(absorb_rgb_q16[i], 0, 4 * BLOOM_Q16_ONE);
        }
    }
    out_matter->lattice_wire_q16 = sc_clamp32(lattice_wire_q16, 0, BLOOM_Q16_ONE);
    out_matter->thickness_mm_q16 = sc_clamp32(thickness_mm_q16, 4096, 32 * BLOOM_Q16_ONE);
    out_matter->thermal_kelvin = thermal_kelvin;
    out_matter->density_kg_m3 = density_kg_m3;
    sc_stamp_mac(out_matter, 0xAA77E464ULL, &out_matter->mac_lo32, &out_matter->mac_hi32);
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

    /* 0: Crystal Glass (eta=1.52, kappa=0) */
    bloom_matter_init64(&out_matters[0], 0U,  99615,      0, 65536, 16384,  3276,  1310, abs_glass,     8192, 262144,  295U, 2500U);
    /* 1: Imperial Jade (eta=1.66, high Mie subsurface scatter) */
    bloom_matter_init64(&out_matters[1], 1U, 108790,      0, 65536, 13107, 19660, 42598, abs_jade,      4096, 393216,  295U, 3330U);
    /* 2: Liquid Water (eta=1.333, dynamic relief) */
    bloom_matter_init64(&out_matters[2], 2U,  87359,      0, 16384, 32768,  1638,  3276, abs_water,        0, 327680,  290U, 1000U);
    /* 3: Brushed Titanium (eta=2.16, kappa=2.92) */
    bloom_matter_init64(&out_matters[3], 3U, 141558, 191365, 65536,  9830, 16384,     0, abs_titanium,     0, 131072,  300U, 4506U);
    /* 4: Aurum Gold (eta=1.18, kappa=3.10) */
    bloom_matter_init64(&out_matters[4], 4U,  77332, 203161, 65536,  6553,  6553,     0, abs_gold,         0, 131072,  300U, 19300U);
    /* 5: Sapphire Dielectric (eta=1.77, lattice) */
    bloom_matter_init64(&out_matters[5], 5U, 115999,      0, 65536, 19660,  2048,  6553, abs_sapphire, 26214, 327680,  295U, 3980U);
    /* 6: Obsidian Ceramic (eta=1.50, dense absorption) */
    bloom_matter_init64(&out_matters[6], 6U,  98304,   8192, 65536,  8192,  8192,  2048, abs_obsidian,     0, 393216,  295U, 2600U);
    /* 7: Thermal Blackbody Core (T=1280K Planck emitter) */
    bloom_matter_init64(&out_matters[7], 7U, 111411,  32768, 32768, 26214, 13107, 16384, abs_lava,     16384, 262144, 1280U, 2800U);
}

/* Rational Pade exp(-x) approximation in Q16.16 for x >= 0: 1 / (1 + x + x^2/2) */
static int32_t sc_exp_neg_q16(int64_t x_q16) {
    if (x_q16 <= 0) return BLOOM_Q16_ONE;
    if (x_q16 >= 8LL * BLOOM_Q16_ONE) return 0;
    int64_t x2_half = (x_q16 * x_q16) >> 17;
    int64_t den = BLOOM_Q16_ONE + x_q16 + x2_half;
    return (int32_t)((((int64_t)BLOOM_Q16_ONE) << 16) / den);
}

void bloom_matter_eval_radiance_q16(const BloomMatter64 *m,
                                    const int32_t grad_h_q16[2],
                                    int32_t laplacian_h_q16,
                                    const int32_t bg_rgb_q16[3],
                                    const int32_t light_delta_q16[2],
                                    int32_t out_rgb_q16[3]) {
    if (!m || !bg_rgb_q16 || !out_rgb_q16) return;
    int64_t eta = sc_clamp64((int64_t)m->ior_eta_q16, BLOOM_Q16_ONE, 8LL * BLOOM_Q16_ONE);
    int64_t kappa = sc_clamp64((int64_t)m->extinction_kappa_q16, 0, 16LL * BLOOM_Q16_ONE);
    int64_t relief = sc_clamp64((int64_t)m->surface_relief_q16, 0, 4LL * BLOOM_Q16_ONE);
    int32_t rough = sc_clamp32(m->micro_roughness_q16, 0, BLOOM_Q16_ONE);
    int32_t mie = sc_clamp32(m->mie_scatter_q16, 0, BLOOM_Q16_ONE);

    /* Complex normal-incidence Fresnel F0 = ((eta-1)^2 + kappa^2) / ((eta+1)^2 + kappa^2) */
    int64_t em1 = eta - BLOOM_Q16_ONE;
    int64_t ep1 = eta + BLOOM_Q16_ONE;
    int64_t num = ((em1 * em1) >> 16) + ((kappa * kappa) >> 16);
    int64_t den = sc_max64(((ep1 * ep1) >> 16) + ((kappa * kappa) >> 16), 1);
    int32_t f0_q16 = sc_clamp32((int32_t)sc_clamp64((num * 65536LL) / den, 0, BLOOM_Q16_ONE), 0, BLOOM_Q16_ONE);

    int32_t skin_q16 = sc_mul_q16(BLOOM_Q16_ONE - f0_q16, sc_exp_neg_q16((kappa * 9LL) >> 2));
    int64_t d_norm = sc_clamp64((int64_t)m->thickness_mm_q16 >> 3, 3276, 3LL * BLOOM_Q16_ONE);

    /* Analytic 2-Jet Jacobian Caustics */
    int64_t snell = ((eta - BLOOM_Q16_ONE) * 65536LL) / eta;
    int64_t det_term = (((d_norm * snell) >> 16) * relief) >> 16;
    det_term = (det_term * (int64_t)laplacian_h_q16) >> 16;
    int64_t jac_det = sc_max64(16384, sc_abs64(BLOOM_Q16_ONE - det_term));
    int32_t raw_caustic = (int32_t)((((int64_t)BLOOM_Q16_ONE) << 16) / jac_det);
    int32_t damp = (rough + mie) >> 1;
    int32_t caustic_q16 = raw_caustic + sc_mul_q16(BLOOM_Q16_ONE - raw_caustic, damp);

    int64_t gx = grad_h_q16 ? sc_clamp64((int64_t)grad_h_q16[0], -256LL * BLOOM_Q16_ONE, 256LL * BLOOM_Q16_ONE) : 0;
    int64_t gy = grad_h_q16 ? sc_clamp64((int64_t)grad_h_q16[1], -256LL * BLOOM_Q16_ONE, 256LL * BLOOM_Q16_ONE) : 0;
    int64_t lx = light_delta_q16 ? sc_clamp64((int64_t)light_delta_q16[0], -256LL * BLOOM_Q16_ONE, 256LL * BLOOM_Q16_ONE) : 0;
    int64_t ly = light_delta_q16 ? sc_clamp64((int64_t)light_delta_q16[1], -256LL * BLOOM_Q16_ONE, 256LL * BLOOM_Q16_ONE) : 0;
    int64_t ux = lx + ((gx * relief) >> 18);
    int64_t uy = ly + ((gy * relief) >> 18);
    int64_t u2 = ((ux * ux + uy * uy) >> 16) * 3LL;
    int32_t softbox = 49152 + sc_exp_neg_q16(u2);
    int32_t refl = sc_mul_q16(softbox, f0_q16);

    int32_t planck[3] = { 0, 0, 0 };
    if (m->thermal_kelvin > 650U) {
        uint64_t tk_num = ((uint64_t)(m->thermal_kelvin - 650U) * 65536ULL) / 1000ULL;
        int32_t ex = (tk_num > (uint64_t)(2 * BLOOM_Q16_ONE)) ? (2 * BLOOM_Q16_ONE) : (int32_t)tk_num;
        planck[0] = sc_mul_q16(ex, 91750);
        planck[1] = sc_mul_q16(sc_mul_q16(ex, ex), 62259);
        planck[2] = (ex > 29491) ? sc_mul_q16(ex - 29491, 55705) : 0;
    }

    for (int c = 0; c < 3; ++c) {
        int64_t abs_c = sc_clamp64((int64_t)m->absorb_rgb_q16[c], 0, 8LL * BLOOM_Q16_ONE);
        int64_t opt_depth = (abs_c * d_norm) >> 16;
        int32_t beer_t = sc_exp_neg_q16(opt_depth);
        int32_t sss = sc_mul_q16(beer_t, (int32_t)(((int64_t)mie * 24903LL) >> 16));
        int32_t bg_c = sc_clamp32(bg_rgb_q16[c], 0, 8 * BLOOM_Q16_ONE);
        int64_t body = (int64_t)sc_mul_q16(sc_mul_q16(bg_c, beer_t), caustic_q16) + (int64_t)sss;
        int64_t tot = (int64_t)sc_mul_q16(sc_clamp64_to_i32(body), skin_q16) + (int64_t)refl + (int64_t)planck[c];
        out_rgb_q16[c] = (int32_t)sc_clamp64(tot, 0, 4LL * BLOOM_Q16_ONE);
    }
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

static int sc_hex_val(char c) {
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
        int hi = sc_hex_val(hex128[i * 2U + 0U]);
        int lo = sc_hex_val(hex128[i * 2U + 1U]);
        if (hi < 0 || lo < 0) return false;
        b[i] = (uint8_t)((hi << 4) | lo);
    }
    uint32_t exp_lo = 0U, exp_hi = 0U;
    sc_stamp_mac(&tmp, 0xAA77E464ULL, &exp_lo, &exp_hi);
    if (tmp.mac_lo32 != exp_lo || tmp.mac_hi32 != exp_hi) return false;
    *out_matter = tmp;
    return true;
}

/* 6. Thin-Lens Physical Orbit Camera & 3D Raycaster */
void bloom_camera_sync_from_orbit(BloomCamera64 *cam) {
    if (!cam) return;
    int32_t cy = bloom_cos_q16(cam->orbit_yaw_q16);
    int32_t sy = bloom_sin_q16(cam->orbit_yaw_q16);
    int32_t cp = bloom_cos_q16(cam->orbit_pitch_q16);
    int32_t sp = bloom_sin_q16(cam->orbit_pitch_q16);
    int32_t r = (cam->orbit_dist_q16 > BLOOM_Q16_ONE) ? cam->orbit_dist_q16 : BLOOM_Q16_ONE;

    int32_t r_horiz = sc_mul_q16(r, cp);
    cam->eye_q16[0] = sc_clamp64_to_i32((int64_t)cam->target_q16[0] + (int64_t)sc_mul_q16(r_horiz, sy));
    cam->eye_q16[1] = sc_clamp64_to_i32((int64_t)cam->target_q16[1] + (int64_t)sc_mul_q16(r, sp));
    cam->eye_q16[2] = sc_clamp64_to_i32((int64_t)cam->target_q16[2] - (int64_t)sc_mul_q16(r_horiz, cy));
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
    cam->orbit_pitch_q16 = sc_clamp32(pitch_q16, -85000, 85000);
    cam->orbit_dist_q16 = (dist_q16 > BLOOM_Q16_ONE) ? dist_q16 : (6 * BLOOM_Q16_ONE);
    cam->focal_mm_q16 = sc_clamp32(focal_mm_q16, 18 * BLOOM_Q16_ONE, 135 * BLOOM_Q16_ONE);
    cam->aperture_fstop_q16 = sc_clamp32(fstop_q16, 91750, 16 * BLOOM_Q16_ONE);
    cam->focus_dist_q16 = cam->orbit_dist_q16;
    cam->exposure_q16 = BLOOM_Q16_ONE;
    bloom_camera_sync_from_orbit(cam);
}

int32_t bloom_camera_eval_coc_q16(const BloomCamera64 *cam, int32_t depth_q16) {
    if (!cam || depth_q16 <= 4096 || cam->focus_dist_q16 <= 4096) return 0;
    int64_t diff = sc_abs64((int64_t)depth_q16 - (int64_t)cam->focus_dist_q16);
    if (diff == 0) return 0;
    int64_t focal = sc_clamp64((int64_t)cam->focal_mm_q16, 0, 256LL * BLOOM_Q16_ONE);
    int64_t fstop = sc_clamp64((int64_t)cam->aperture_fstop_q16, 4096, 64LL * BLOOM_Q16_ONE);
    /* Thin-lens Circle of Confusion proportional to |z - z_focus| * f / (N * z) */
    int64_t num = (diff * focal) >> 16;
    int64_t den = sc_max64((fstop * (int64_t)depth_q16) >> 16, 4096);
    int64_t coc = (num * 4096LL) / den;
    return (int32_t)sc_clamp64(coc, 0, BLOOM_Q16_ONE);
}

static void sc_vec3_norm_q16(int64_t x, int64_t y, int64_t z, int32_t out_n[3]) {
    int64_t len = sc_len3_q16(x, y, z);
    if (len <= 0) {
        out_n[0] = 0;
        out_n[1] = 0;
        out_n[2] = BLOOM_Q16_ONE;
        return;
    }
    int64_t cx = sc_min64(sc_max64(x, -0x3FFFFFFFLL), 0x3FFFFFFFLL);
    int64_t cy = sc_min64(sc_max64(y, -0x3FFFFFFFLL), 0x3FFFFFFFLL);
    int64_t cz = sc_min64(sc_max64(z, -0x3FFFFFFFLL), 0x3FFFFFFFLL);
    out_n[0] = (int32_t)((cx * 65536LL) / len);
    out_n[1] = (int32_t)((cy * 65536LL) / len);
    out_n[2] = (int32_t)((cz * 65536LL) / len);
}

bool bloom_scene_raycast64(BloomEngineArena *arena,
                           int32_t cursor_norm_x_q16,
                           int32_t cursor_norm_y_q16,
                           BloomRaycastHit64 *out_hit) {
    if (!arena) return false;
    BloomRaycastHit64 hit;
    memset(&hit, 0, sizeof(hit));
    hit.ray_origin_q16[0] = cursor_norm_x_q16;
    hit.ray_origin_q16[1] = cursor_norm_y_q16;

    const BloomFrameHeader64 *hdr = &arena->cells[BLOOM_SLOT_HEADER].frame_hdr;
    const BloomCamera64 *cam = &arena->cells[BLOOM_SLOT_CAMERA].camera;

    /* Build camera orthonormal basis (fwd, right, up_ortho) in Q16 */
    int32_t fwd[3], right[3], up_v[3];
    sc_vec3_norm_q16((int64_t)cam->target_q16[0] - cam->eye_q16[0],
                     (int64_t)cam->target_q16[1] - cam->eye_q16[1],
                     (int64_t)cam->target_q16[2] - cam->eye_q16[2], fwd);
    /* right = normalize(cross(world_up, fwd)) in left-handed / forward +Z convention:
       cross((0,1,0), fwd) = (fwd.z, 0, -fwd.x) */
    sc_vec3_norm_q16(fwd[2], 0, -fwd[0], right);
    /* up_v = cross(fwd, right) */
    up_v[0] = (int32_t)(((int64_t)fwd[1] * right[2] - (int64_t)fwd[2] * right[1]) >> 16);
    up_v[1] = (int32_t)(((int64_t)fwd[2] * right[0] - (int64_t)fwd[0] * right[2]) >> 16);
    up_v[2] = (int32_t)(((int64_t)fwd[0] * right[1] - (int64_t)fwd[1] * right[0]) >> 16);

    int64_t aspect_q16 = (hdr->height_px > 0U)
        ? sc_clamp64(((int64_t)hdr->width_px << 16) / (int64_t)hdr->height_px, 4096, 16LL * BLOOM_Q16_ONE)
        : BLOOM_Q16_ONE;
    int64_t fov_z_q16 = sc_clamp64(((int64_t)cam->focal_mm_q16) / 28LL, 45875, 16LL * BLOOM_Q16_ONE);

    int64_t cnx = sc_clamp64((int64_t)cursor_norm_x_q16, -4LL * BLOOM_Q16_ONE, 4LL * BLOOM_Q16_ONE);
    int64_t cny = sc_clamp64((int64_t)cursor_norm_y_q16, -4LL * BLOOM_Q16_ONE, 4LL * BLOOM_Q16_ONE);

    /* cursor_norm in [-65536..+65536], screen Y+ down -> world Y+ up (-cursor_norm_y) */
    int64_t uvx = ((cnx * aspect_q16) >> 17);
    int64_t uvy = -(cny >> 1);

    int32_t rd[3];
    sc_vec3_norm_q16(((int64_t)right[0] * uvx + (int64_t)up_v[0] * uvy + (int64_t)fwd[0] * fov_z_q16) >> 16,
                     ((int64_t)right[1] * uvx + (int64_t)up_v[1] * uvy + (int64_t)fwd[1] * fov_z_q16) >> 16,
                     ((int64_t)right[2] * uvx + (int64_t)up_v[2] * uvy + (int64_t)fwd[2] * fov_z_q16) >> 16,
                     rd);

    uint32_t inst_n = (hdr->inst_count > BLOOM_MAX_INSTANCES) ? BLOOM_MAX_INSTANCES : hdr->inst_count;
    bool xray_on = (hdr->flags & BLOOM_FLAG_XRAY_ENABLED) != 0U;
    int64_t t = 8192;
    const int64_t t_max = 32LL * BLOOM_Q16_ONE;
    uint32_t total_bvh_vis = 0U;

    for (uint32_t step = 0U; step < 64U && t < t_max; ++step) {
        int64_t px = cam->eye_q16[0] + (((int64_t)rd[0] * t) >> 16);
        int64_t py = cam->eye_q16[1] + (((int64_t)rd[1] * t) >> 16);
        int64_t pz = cam->eye_q16[2] + (((int64_t)rd[2] * t) >> 16);

        int64_t min_d = (int64_t)BLOOM_MAX_Q16;
        uint32_t best_inst = 0U;
        uint32_t best_inst_slot = 0U;
        uint32_t best_prefab = 0U;

        for (uint32_t i = 0U; i < inst_n; ++i) {
            const BloomPrefabInst64 *inst = &arena->map.instances[i];
            if ((inst->flags & BLOOM_INST_FLAG_ACTIVE) == 0U) continue;
            uint32_t pid = inst->prefab_id;
            if (pid == 0U || pid > BLOOM_MAX_PREFABS) continue;
            const BloomPrefabDesc64 *pf = &arena->map.prefabs[pid - 1U];

            int64_t sc = (inst->scale_q16 > 4096)
                ? sc_clamp64((int64_t)inst->scale_q16, 4096, 256LL * BLOOM_Q16_ONE)
                : (int64_t)BLOOM_Q16_ONE;
            int64_t lx = ((px - (int64_t)inst->pos_q16[0]) * 65536LL) / sc;
            int64_t ly = ((py - (int64_t)inst->pos_q16[1]) * 65536LL) / sc;
            int64_t lz = ((pz - (int64_t)inst->pos_q16[2]) * 65536LL) / sc;

            /* Fast bounding-sphere skip: note xray cutaway is max(d, xray_plane - px),
               which only INCREASES SDF, so bound_d remains a valid lower bound! */
            int64_t br = sc_clamp64((int64_t)pf->bounding_radius_q16, 0, 1024LL * BLOOM_Q16_ONE);
            int64_t bound_d = sc_clamp64(sc_len3_q16(lx, ly, lz) - (br + 32768LL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
            if (((bound_d * sc) >> 16) >= min_d) continue;

            int64_t d_local = sc_clamp64(bloom_prefab_eval_sdf_q16(pf, lx, ly, lz, inst->param_override_q16),
                                         -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
            int64_t d_world = (d_local * sc) >> 16;
            d_world = bloom_prefab_eval_xray_cutaway_q16(d_world, px, hdr->xray_plane_q16, xray_on);
            if (d_world < min_d) {
                min_d = d_world;
                best_inst = (inst->inst_id != 0U) ? inst->inst_id : (i + 1U);
                best_inst_slot = i;
                best_prefab = pid;
            }
        }

        uint32_t bvh_n = (hdr->bvh_count > BLOOM_MAX_BVH_NODES) ? BLOOM_MAX_BVH_NODES : hdr->bvh_count;
        uint32_t csg_n = (hdr->csg_count > BLOOM_MAX_CSG_OPS) ? BLOOM_MAX_CSG_OPS : hdr->csg_count;
        if (bvh_n > 0U && csg_n > 0U) {
            uint32_t vis = 0U;
            int64_t d_bvh = bloom_bvh_eval_csg_q16(
                arena->map.bvh_nodes, bvh_n,
                arena->map.csg_ops, csg_n,
                px, py, pz, &vis);
            total_bvh_vis += vis;
            if (d_bvh < min_d && best_inst == 0U) {
                min_d = d_bvh;
            }
        }

        if (min_d < 512) {
            hit.hit_valid = (best_inst != 0U) ? 1U : 0U;
            hit.hit_inst_id = best_inst;
            hit.hit_prefab_id = best_prefab;
            hit.bvh_nodes_visited = total_bvh_vis;
            hit.t_hit_q16 = (int32_t)t;
            hit.sdf_dist_q16 = sc_clamp64_to_i32(min_d);
            hit.hit_pos_q16[0] = sc_clamp64_to_i32(px);
            hit.hit_pos_q16[1] = sc_clamp64_to_i32(py);
            hit.hit_pos_q16[2] = sc_clamp64_to_i32(pz);

            /* Finite-difference SDF surface normal */
            if (best_prefab >= 1U && best_prefab <= BLOOM_MAX_PREFABS && best_inst_slot < inst_n) {
                const BloomPrefabDesc64 *pf = &arena->map.prefabs[best_prefab - 1U];
                int64_t eps = 1024;
                const BloomPrefabInst64 *inst = &arena->map.instances[best_inst_slot];
                int64_t sc = (inst->scale_q16 > 4096)
                    ? sc_clamp64((int64_t)inst->scale_q16, 4096, 256LL * BLOOM_Q16_ONE)
                    : (int64_t)BLOOM_Q16_ONE;
                int64_t lx = ((px - (int64_t)inst->pos_q16[0]) * 65536LL) / sc;
                int64_t ly = ((py - (int64_t)inst->pos_q16[1]) * 65536LL) / sc;
                int64_t lz = ((pz - (int64_t)inst->pos_q16[2]) * 65536LL) / sc;
                int64_t gx = sc_sat_sub64(bloom_prefab_eval_sdf_q16(pf, lx + eps, ly, lz, inst->param_override_q16),
                                          bloom_prefab_eval_sdf_q16(pf, lx - eps, ly, lz, inst->param_override_q16));
                int64_t gy = sc_sat_sub64(bloom_prefab_eval_sdf_q16(pf, lx, ly + eps, lz, inst->param_override_q16),
                                          bloom_prefab_eval_sdf_q16(pf, lx, ly - eps, lz, inst->param_override_q16));
                int64_t gz = sc_sat_sub64(bloom_prefab_eval_sdf_q16(pf, lx, ly, lz + eps, inst->param_override_q16),
                                          bloom_prefab_eval_sdf_q16(pf, lx, ly, lz - eps, inst->param_override_q16));
                sc_vec3_norm_q16(gx, gy, gz, hit.hit_normal_q16);
            }
            sc_stamp_mac(&hit, 0x4A1CA570ULL, &hit.mac_lo32, &hit.mac_hi32);
            arena->map.ray_hit = hit;
            if (out_hit) *out_hit = hit;
            return (hit.hit_valid != 0U);
        }
        t += sc_max64(min_d, 512);
    }

    hit.bvh_nodes_visited = total_bvh_vis;
    sc_stamp_mac(&hit, 0x4A1CA570ULL, &hit.mac_lo32, &hit.mac_hi32);
    arena->map.ray_hit = hit;
    if (out_hit) *out_hit = hit;
    return false;
}
