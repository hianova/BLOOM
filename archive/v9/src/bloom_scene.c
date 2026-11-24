#include "bloom_scene.h"
#include "bloom_prefab.h"
#include "flow_cholesky_sdd.h"

#include <limits.h>
#include <string.h>

/*
 * ============================================================================
 * Pillar 1 ("Three.js"): Pure C17 Fixed-Point 3D Spatial, BVH, Skinning & Optics
 * Zero FloatOps | Zero Heap Allocations | Bit-Identical Across Architectures
 * ============================================================================
 */

#define SC_MAX_COORD_Q16 INT64_C(0x0FFFFFFFFFFF)

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

/* 1. CSG Primitive, CAD Mesh (.stl/.glb) Ingestion & Cross-Section Plane */
static void sc_vec3_norm_q16(int64_t x, int64_t y, int64_t z, int32_t out_n[3]);

int64_t bloom_section_eval_clip_q16(int64_t raw_d_q16,
                                    int64_t px_q16,
                                    int64_t py_q16,
                                    int64_t pz_q16,
                                    const BloomFrameHeader64 *hdr,
                                    const BloomSectionSpec64 *sec,
                                    bool *out_is_cut) {
    if (out_is_cut) *out_is_cut = false;
    if (sec && sec->section_mode != BLOOM_SEC_MODE_OFF) {
        int64_t cx = sc_clamp64(px_q16, -INT64_C(0x3FFFFFFFFFFF), INT64_C(0x3FFFFFFFFFFF));
        int64_t cy = sc_clamp64(py_q16, -INT64_C(0x3FFFFFFFFFFF), INT64_C(0x3FFFFFFFFFFF));
        int64_t cz = sc_clamp64(pz_q16, -INT64_C(0x3FFFFFFFFFFF), INT64_C(0x3FFFFFFFFFFF));
        int64_t nx = sc_clamp64(sec->plane_norm_q16[0], -BLOOM_Q16_ONE, BLOOM_Q16_ONE);
        int64_t ny = sc_clamp64(sec->plane_norm_q16[1], -BLOOM_Q16_ONE, BLOOM_Q16_ONE);
        int64_t nz = sc_clamp64(sec->plane_norm_q16[2], -BLOOM_Q16_ONE, BLOOM_Q16_ONE);
        if (nx == 0 && ny == 0 && nz == 0) nx = BLOOM_Q16_ONE;
        int64_t ndotp = (nx * cx + ny * cy + nz * cz) >> 16;
        int64_t clip_d = -BLOOM_MAX_Q16;
        if (sec->section_mode == BLOOM_SEC_MODE_SLAB) {
            int64_t slab_h = sc_max64((int64_t)sec->slab_half_q16, 1024LL);
            clip_d = sc_sat_sub64(sc_abs64(sc_sat_sub64(ndotp, (int64_t)sec->plane_dist_q16)), slab_h);
        } else if (sec->section_mode == BLOOM_SEC_MODE_CORNER) {
            int64_t w1 = sc_sat_sub64(ndotp, (int64_t)sec->plane_dist_q16);
            int64_t wedge = sc_max64(w1, cz);
            clip_d = sc_neg64(wedge);
        } else {
            clip_d = sc_sat_sub64((int64_t)sec->plane_dist_q16, ndotp);
        }
        if (clip_d > raw_d_q16) {
            if (out_is_cut) *out_is_cut = true;
            return clip_d;
        }
        return raw_d_q16;
    }
    if (hdr && (hdr->flags & BLOOM_FLAG_XRAY_ENABLED) != 0U) {
        int64_t clipped = bloom_prefab_eval_xray_cutaway_q16(raw_d_q16, px_q16, hdr->xray_plane_q16, true);
        if (clipped > raw_d_q16 && out_is_cut) *out_is_cut = true;
        return clipped;
    }
    return raw_d_q16;
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

bool bloom_tri64_init(BloomTri64 *out_tri,
                      uint32_t tri_id,
                      uint16_t matter_id,
                      uint16_t shell_q12,
                      const int32_t v0_q16[3],
                      const int32_t v1_q16[3],
                      const int32_t v2_q16[3]) {
    if (!out_tri || !v0_q16 || !v1_q16 || !v2_q16) return false;
    memset(out_tri, 0, sizeof(*out_tri));
    out_tri->tri_id = tri_id;
    out_tri->matter_id = matter_id;
    out_tri->shell_q12 = shell_q12;
    for (uint32_t a = 0U; a < 3U; ++a) {
        out_tri->v0_q16[a] = sc_clamp32(v0_q16[a], -64 * BLOOM_Q16_ONE, 64 * BLOOM_Q16_ONE);
        out_tri->v1_q16[a] = sc_clamp32(v1_q16[a], -64 * BLOOM_Q16_ONE, 64 * BLOOM_Q16_ONE);
        out_tri->v2_q16[a] = sc_clamp32(v2_q16[a], -64 * BLOOM_Q16_ONE, 64 * BLOOM_Q16_ONE);
    }
    int64_t e1x = (int64_t)out_tri->v1_q16[0] - (int64_t)out_tri->v0_q16[0];
    int64_t e1y = (int64_t)out_tri->v1_q16[1] - (int64_t)out_tri->v0_q16[1];
    int64_t e1z = (int64_t)out_tri->v1_q16[2] - (int64_t)out_tri->v0_q16[2];
    int64_t e2x = (int64_t)out_tri->v2_q16[0] - (int64_t)out_tri->v0_q16[0];
    int64_t e2y = (int64_t)out_tri->v2_q16[1] - (int64_t)out_tri->v0_q16[1];
    int64_t e2z = (int64_t)out_tri->v2_q16[2] - (int64_t)out_tri->v0_q16[2];

    uint64_t me = (uint64_t)sc_abs64(e1x) | (uint64_t)sc_abs64(e1y) | (uint64_t)sc_abs64(e1z) |
                  (uint64_t)sc_abs64(e2x) | (uint64_t)sc_abs64(e2y) | (uint64_t)sc_abs64(e2z);
    uint32_t sh = 0U;
    while ((me >> sh) > 0x3FFFFFFULL) ++sh;
    e1x /= (int64_t)(1LL << sh); e1y /= (int64_t)(1LL << sh); e1z /= (int64_t)(1LL << sh);
    e2x /= (int64_t)(1LL << sh); e2y /= (int64_t)(1LL << sh); e2z /= (int64_t)(1LL << sh);

    int64_t nx = e1y * e2z - e1z * e2y;
    int64_t ny = e1z * e2x - e1x * e2z;
    int64_t nz = e1x * e2y - e1y * e2x;
    if (nx == 0 && ny == 0 && nz == 0) {
        return false;
    }
    sc_vec3_norm_q16(nx, ny, nz, out_tri->normal_q16);
    sc_stamp_mac(out_tri, 0x74160064ULL, &out_tri->mac_lo32, &out_tri->mac_hi32);
    return true;
}

static uint32_t sc_shift_for_vec3(int64_t x, int64_t y, int64_t z, uint64_t limit) {
    uint64_t m = (uint64_t)sc_abs64(x) | (uint64_t)sc_abs64(y) | (uint64_t)sc_abs64(z);
    uint32_t sh = 0U;
    while ((m >> sh) > limit && sh < 48U) ++sh;
    return sh;
}

static int64_t sc_edge_sign_dot_q16(int64_t ex, int64_t ey, int64_t ez,
                                    int64_t nx, int64_t ny, int64_t nz,
                                    int64_t px, int64_t py, int64_t pz) {
    int64_t cx = ey * nz - ez * ny;
    int64_t cy = ez * nx - ex * nz;
    int64_t cz = ex * ny - ey * nx;
    uint32_t esh = sc_shift_for_vec3(cx, cy, cz, 0x3FFFFULL);
    if (esh > 0U) {
        cx /= (int64_t)(1ULL << esh);
        cy /= (int64_t)(1ULL << esh);
        cz /= (int64_t)(1ULL << esh);
    }
    return cx * px + cy * py + cz * pz;
}

static int64_t sc_edge_dist_q16(int64_t ux_o, int64_t uy_o, int64_t uz_o,
                                int64_t wx_o, int64_t wy_o, int64_t wz_o,
                                int64_t ux_s, int64_t uy_s, int64_t uz_s,
                                int64_t wx_s, int64_t wy_s, int64_t wz_s) {
    int64_t uu = ux_s * ux_s + uy_s * uy_s + uz_s * uz_s;
    int64_t h_q16 = 0;
    if (uu > 0) {
        int64_t uw = ux_s * wx_s + uy_s * wy_s + uz_s * wz_s;
        if (uw <= 0) {
            h_q16 = 0;
        } else if (uw >= uu) {
            h_q16 = BLOOM_Q16_ONE;
        } else {
            uint32_t dsh = 0U;
            while (sc_abs64(uw >> dsh) > INT64_C(0x3FFFFFFFFFF) || (uu >> dsh) > INT64_C(0x3FFFFFFFFFF)) {
                ++dsh;
            }
            int64_t den = sc_max64(uu >> dsh, 1);
            h_q16 = sc_clamp64(((uw >> dsh) * 65536LL) / den, 0, BLOOM_Q16_ONE);
        }
    }
    int64_t dx = wx_o - ((ux_o * h_q16) >> 16);
    int64_t dy = wy_o - ((uy_o * h_q16) >> 16);
    int64_t dz = wz_o - ((uz_o * h_q16) >> 16);
    return sc_len3_q16(dx, dy, dz);
}

int64_t bloom_tri64_eval_ud_q16(const BloomTri64 *tri,
                                int64_t px_q16,
                                int64_t py_q16,
                                int64_t pz_q16) {
    if (!tri) return (int64_t)BLOOM_MAX_Q16;
    int64_t cpx = sc_clamp64(px_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t cpy = sc_clamp64(py_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t cpz = sc_clamp64(pz_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);

    int64_t bax = (int64_t)tri->v1_q16[0] - (int64_t)tri->v0_q16[0];
    int64_t bay = (int64_t)tri->v1_q16[1] - (int64_t)tri->v0_q16[1];
    int64_t baz = (int64_t)tri->v1_q16[2] - (int64_t)tri->v0_q16[2];
    int64_t cbx = (int64_t)tri->v2_q16[0] - (int64_t)tri->v1_q16[0];
    int64_t cby = (int64_t)tri->v2_q16[1] - (int64_t)tri->v1_q16[1];
    int64_t cbz = (int64_t)tri->v2_q16[2] - (int64_t)tri->v1_q16[2];
    int64_t acx = (int64_t)tri->v0_q16[0] - (int64_t)tri->v2_q16[0];
    int64_t acy = (int64_t)tri->v0_q16[1] - (int64_t)tri->v2_q16[1];
    int64_t acz = (int64_t)tri->v0_q16[2] - (int64_t)tri->v2_q16[2];

    int64_t pax = cpx - (int64_t)tri->v0_q16[0];
    int64_t pay = cpy - (int64_t)tri->v0_q16[1];
    int64_t paz = cpz - (int64_t)tri->v0_q16[2];
    int64_t pbx = cpx - (int64_t)tri->v1_q16[0];
    int64_t pby = cpy - (int64_t)tri->v1_q16[1];
    int64_t pbz = cpz - (int64_t)tri->v1_q16[2];
    int64_t pcx = cpx - (int64_t)tri->v2_q16[0];
    int64_t pcy = cpy - (int64_t)tri->v2_q16[1];
    int64_t pcz = cpz - (int64_t)tri->v2_q16[2];

    uint64_t m = (uint64_t)sc_abs64(bax) | (uint64_t)sc_abs64(bay) | (uint64_t)sc_abs64(baz) |
                 (uint64_t)sc_abs64(cbx) | (uint64_t)sc_abs64(cby) | (uint64_t)sc_abs64(cbz) |
                 (uint64_t)sc_abs64(acx) | (uint64_t)sc_abs64(acy) | (uint64_t)sc_abs64(acz) |
                 (uint64_t)sc_abs64(pax) | (uint64_t)sc_abs64(pay) | (uint64_t)sc_abs64(paz) |
                 (uint64_t)sc_abs64(pbx) | (uint64_t)sc_abs64(pby) | (uint64_t)sc_abs64(pbz) |
                 (uint64_t)sc_abs64(pcx) | (uint64_t)sc_abs64(pcy) | (uint64_t)sc_abs64(pcz);
    uint32_t sh = 0U;
    while ((m >> sh) > 0x3FFFFULL && sh < 48U) ++sh;
    int64_t div_s = (int64_t)(1ULL << sh);

    int64_t basx = bax / div_s, basy = bay / div_s, basz = baz / div_s;
    int64_t cbsx = cbx / div_s, cbsy = cby / div_s, cbsz = cbz / div_s;
    int64_t acsx = acx / div_s, acsy = acy / div_s, acsz = acz / div_s;
    int64_t pasx = pax / div_s, pasy = pay / div_s, pasz = paz / div_s;
    int64_t pbsx = pbx / div_s, pbsy = pby / div_s, pbsz = pbz / div_s;
    int64_t pcsx = pcx / div_s, pcsy = pcy / div_s, pcsz = pcz / div_s;

    int64_t norx = basy * acsz - basz * acsy;
    int64_t nory = basz * acsx - basx * acsz;
    int64_t norz = basx * acsy - basy * acsx;
    uint32_t nsh = sc_shift_for_vec3(norx, nory, norz, 0x3FFFFULL);
    if (nsh > 0U) {
        int64_t ndiv = (int64_t)(1ULL << nsh);
        norx /= ndiv;
        nory /= ndiv;
        norz /= ndiv;
    }

    int64_t s0 = sc_edge_sign_dot_q16(basx, basy, basz, norx, nory, norz, pasx, pasy, pasz);
    int64_t s1 = sc_edge_sign_dot_q16(cbsx, cbsy, cbsz, norx, nory, norz, pbsx, pbsy, pbsz);
    int64_t s2 = sc_edge_sign_dot_q16(acsx, acsy, acsz, norx, nory, norz, pcsx, pcsy, pcsz);
    int sgn_sum = ((s0 > 0) - (s0 < 0)) + ((s1 > 0) - (s1 < 0)) + ((s2 > 0) - (s2 < 0));

    if (sgn_sum < 2) {
        int64_t d0 = sc_edge_dist_q16(bax, bay, baz, pax, pay, paz, basx, basy, basz, pasx, pasy, pasz);
        int64_t d1 = sc_edge_dist_q16(cbx, cby, cbz, pbx, pby, pbz, cbsx, cbsy, cbsz, pbsx, pbsy, pbsz);
        int64_t d2 = sc_edge_dist_q16(acx, acy, acz, pcx, pcy, pcz, acsx, acsy, acsz, pcsx, pcsy, pcsz);
        return sc_min64(d0, sc_min64(d1, d2));
    }

    uint64_t nn = (uint64_t)(norx * norx) + (uint64_t)(nory * nory) + (uint64_t)(norz * norz);
    uint64_t nlen = sc_isqrt_u64(nn);
    if (nlen == 0ULL) {
        return sc_edge_dist_q16(bax, bay, baz, pax, pay, paz, basx, basy, basz, pasx, pasy, pasz);
    }
    int64_t d_nor = sc_abs64(norx * pasx + nory * pasy + norz * pasz);
    uint64_t ud_s = (uint64_t)d_nor / nlen;
    if (sh > 0U && ud_s > ((uint64_t)SC_MAX_COORD_Q16 >> sh)) {
        return SC_MAX_COORD_Q16;
    }
    return (int64_t)(ud_s << sh);
}

static void sc_tri_bounds_q16(const BloomTri64 *tri, int32_t out_min[3], int32_t out_max[3]) {
    int64_t pad = 4096LL + ((int64_t)tri->shell_q12 << 4);
    for (uint32_t a = 0U; a < 3U; ++a) {
        int32_t mn = tri->v0_q16[a];
        if (tri->v1_q16[a] < mn) mn = tri->v1_q16[a];
        if (tri->v2_q16[a] < mn) mn = tri->v2_q16[a];
        int32_t mx = tri->v0_q16[a];
        if (tri->v1_q16[a] > mx) mx = tri->v1_q16[a];
        if (tri->v2_q16[a] > mx) mx = tri->v2_q16[a];
        out_min[a] = sc_clamp64_to_i32((int64_t)mn - pad);
        out_max[a] = sc_clamp64_to_i32((int64_t)mx + pad);
    }
}

static uint32_t sc_build_mesh_bvh_rec(const BloomTri64 *tris,
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
        uint32_t ti = use_morton_leaves ? sorted_idx[i] : i;
        int32_t tmin[3], tmax[3];
        sc_tri_bounds_q16(&tris[ti], tmin, tmax);
        for (uint32_t a = 0U; a < 3U; ++a) {
            if (tmin[a] < bmin[a]) bmin[a] = tmin[a];
            if (tmax[a] > bmax[a]) bmax[a] = tmax[a];
        }
    }
    for (uint32_t a = 0U; a < 3U; ++a) {
        nd->aabb_min_q16[a] = bmin[a];
        nd->aabb_max_q16[a] = bmax[a];
    }
    uint32_t rep = use_morton_leaves ? sorted_idx[start] : start;
    int32_t cx = (int32_t)(((int64_t)tris[rep].v0_q16[0] + tris[rep].v1_q16[0] + tris[rep].v2_q16[0]) / 3LL);
    int32_t cy = (int32_t)(((int64_t)tris[rep].v0_q16[1] + tris[rep].v1_q16[1] + tris[rep].v2_q16[1]) / 3LL);
    int32_t cz = (int32_t)(((int64_t)tris[rep].v0_q16[2] + tris[rep].v1_q16[2] + tris[rep].v2_q16[2]) / 3LL);
    uint64_t mkey = use_morton_leaves ? sorted_keys[start] : bloom_bvh_morton3d_q16(cx, cy, cz);
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
        nd->left_or_first = sc_build_mesh_bvh_rec(tris, sorted_idx, sorted_keys, use_morton_leaves,
                                                  start, mid, left_b, out_nodes, inout_count, max_nodes);
        nd->right_or_count = sc_build_mesh_bvh_rec(tris, sorted_idx, sorted_keys, use_morton_leaves,
                                                   mid, end, right_b, out_nodes, inout_count, max_nodes);
    }
    sc_stamp_mac(nd, 0xBF400064ULL, &nd->mac_lo32, &nd->mac_hi32);
    return my_idx;
}

size_t bloom_bvh_build_from_tris64(const BloomTri64 *tris,
                                   size_t tri_count,
                                   BloomBvhNode64 *out_nodes,
                                   size_t max_nodes) {
    if (!tris || tri_count == 0U || !out_nodes || max_nodes == 0U) return 0U;
    uint32_t n = (tri_count > BLOOM_MAX_MESH_TRIS) ? BLOOM_MAX_MESH_TRIS : (uint32_t)tri_count;
    uint32_t cap = (max_nodes > BLOOM_MAX_MESH_BVH_NODES) ? BLOOM_MAX_MESH_BVH_NODES : (uint32_t)max_nodes;
    bool use_morton_leaves = ((2U * n - 1U) <= cap);

    uint32_t idx[BLOOM_MAX_MESH_TRIS];
    uint64_t keys[BLOOM_MAX_MESH_TRIS];
    for (uint32_t i = 0U; i < n; ++i) {
        idx[i] = i;
        int32_t cx = (int32_t)(((int64_t)tris[i].v0_q16[0] + tris[i].v1_q16[0] + tris[i].v2_q16[0]) / 3LL);
        int32_t cy = (int32_t)(((int64_t)tris[i].v0_q16[1] + tris[i].v1_q16[1] + tris[i].v2_q16[1]) / 3LL);
        int32_t cz = (int32_t)(((int64_t)tris[i].v0_q16[2] + tris[i].v1_q16[2] + tris[i].v2_q16[2]) / 3LL);
        keys[i] = bloom_bvh_morton3d_q16(cx, cy, cz);
    }
    if (use_morton_leaves) {
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
    (void)sc_build_mesh_bvh_rec(tris, idx, keys, use_morton_leaves, 0U, n, cap, out_nodes, &built, cap);
    return (size_t)built;
}

static void sc_sort_mesh_tris_morton(BloomTri64 *tris, uint32_t n) {
    uint64_t keys[BLOOM_MAX_MESH_TRIS];
    uint32_t limit = (n > BLOOM_MAX_MESH_TRIS) ? BLOOM_MAX_MESH_TRIS : n;
    for (uint32_t i = 0U; i < limit; ++i) {
        int32_t cx = (int32_t)(((int64_t)tris[i].v0_q16[0] + tris[i].v1_q16[0] + tris[i].v2_q16[0]) / 3LL);
        int32_t cy = (int32_t)(((int64_t)tris[i].v0_q16[1] + tris[i].v1_q16[1] + tris[i].v2_q16[1]) / 3LL);
        int32_t cz = (int32_t)(((int64_t)tris[i].v0_q16[2] + tris[i].v1_q16[2] + tris[i].v2_q16[2]) / 3LL);
        keys[i] = bloom_bvh_morton3d_q16(cx, cy, cz);
    }
    for (uint32_t i = 1U; i < limit; ++i) {
        uint64_t k = keys[i];
        BloomTri64 t = tris[i];
        uint32_t j = i;
        while (j > 0U && keys[j - 1U] > k) {
            keys[j] = keys[j - 1U];
            tris[j] = tris[j - 1U];
            --j;
        }
        keys[j] = k;
        tris[j] = t;
    }
}

static bool sc_ray_pos_x_hits_tri(const BloomTri64 *tri, int64_t px, int64_t ry, int64_t rz) {
    int64_t e1x = (int64_t)tri->v1_q16[0] - (int64_t)tri->v0_q16[0];
    int64_t e1y = (int64_t)tri->v1_q16[1] - (int64_t)tri->v0_q16[1];
    int64_t e1z = (int64_t)tri->v1_q16[2] - (int64_t)tri->v0_q16[2];
    int64_t e2x = (int64_t)tri->v2_q16[0] - (int64_t)tri->v0_q16[0];
    int64_t e2y = (int64_t)tri->v2_q16[1] - (int64_t)tri->v0_q16[1];
    int64_t e2z = (int64_t)tri->v2_q16[2] - (int64_t)tri->v0_q16[2];
    int64_t ty  = ry - (int64_t)tri->v0_q16[1];
    int64_t tz  = rz - (int64_t)tri->v0_q16[2];

    uint64_t m = (uint64_t)sc_abs64(e1x) | (uint64_t)sc_abs64(e1y) | (uint64_t)sc_abs64(e1z) |
                 (uint64_t)sc_abs64(e2x) | (uint64_t)sc_abs64(e2y) | (uint64_t)sc_abs64(e2z) |
                 (uint64_t)sc_abs64(ty)  | (uint64_t)sc_abs64(tz);
    uint32_t sh = 0U;
    while ((m >> sh) > 0x3FFFFULL && sh < 48U) ++sh;
    int64_t d = (int64_t)(1ULL << sh);
    int64_t e1ys = e1y / d, e1zs = e1z / d;
    int64_t e2ys = e2y / d, e2zs = e2z / d;
    int64_t tys = ty / d, tzs = tz / d;

    int64_t det = -(e1ys * e2zs) + (e1zs * e2ys);
    if (det == 0) return false;
    int64_t unum = -(tys * e2zs) + (tzs * e2ys);
    int64_t vnum = (tys * e1zs) - (tzs * e1ys);

    if (det > 0) {
        if (unum < 0 || vnum < 0 || (unum + vnum) > det) return false;
    } else {
        if (unum > 0 || vnum > 0 || (unum + vnum) < det) return false;
    }

    int64_t num_u = unum;
    int64_t num_v = vnum;
    int64_t den = det;
    uint32_t qsh = 0U;
    while ((sc_abs64(num_u >> qsh) > INT64_C(0x3FFFFFFFFFF)) ||
           (sc_abs64(num_v >> qsh) > INT64_C(0x3FFFFFFFFFF)) ||
           (sc_abs64(den >> qsh) > INT64_C(0x3FFFFFFFFFF))) {
        ++qsh;
    }
    int64_t sden = den / (int64_t)(1ULL << qsh);
    if (sden == 0) return false;
    int64_t u_q16 = ((num_u / (int64_t)(1ULL << qsh)) * 65536LL) / sden;
    int64_t v_q16 = ((num_v / (int64_t)(1ULL << qsh)) * 65536LL) / sden;
    int64_t x_hit = (int64_t)tri->v0_q16[0] + (((u_q16 * e1x) + (v_q16 * e2x)) >> 16);
    return x_hit > px;
}

bool bloom_stl_parse_to_mesh64(const uint8_t *stl_bytes,
                               size_t stl_len,
                               uint16_t matter_id,
                               bool normalize_unit_bounds,
                               BloomMeshArena *out_mesh,
                               uint32_t *out_tri_count) {
    if (!stl_bytes || stl_len < 84U || !out_mesh) return false;
    uint32_t raw_cnt = sc_read_u32_le(stl_bytes + 80U);
    if (raw_cnt == 0U || raw_cnt > 200000U) return false;
    uint64_t need_bytes = (uint64_t)raw_cnt * 50ULL + 84ULL;
    if (need_bytes != (uint64_t)stl_len) return false;

    int32_t bmin[3] = { BLOOM_MAX_Q16, BLOOM_MAX_Q16, BLOOM_MAX_Q16 };
    int32_t bmax[3] = { -BLOOM_MAX_Q16, -BLOOM_MAX_Q16, -BLOOM_MAX_Q16 };
    for (uint32_t i = 0U; i < raw_cnt; ++i) {
        const uint8_t *rec = stl_bytes + 84U + (size_t)i * 50U;
        for (uint32_t k = 0U; k < 3U; ++k) {
            uint32_t nbits = sc_read_u32_le(rec + (size_t)k * 4U);
            if ((nbits & 0x7F800000U) == 0x7F800000U) return false;
        }
        for (uint32_t v = 0U; v < 3U; ++v) {
            for (uint32_t a = 0U; a < 3U; ++a) {
                uint32_t bits = sc_read_u32_le(rec + 12U + (size_t)v * 12U + (size_t)a * 4U);
                int32_t qv = 0;
                if (!sc_ieee754_finite_to_q16(bits, &qv)) return false;
                if (qv < bmin[a]) bmin[a] = qv;
                if (qv > bmax[a]) bmax[a] = qv;
            }
        }
    }

    int64_t mid[3];
    int64_t max_span = 1;
    for (uint32_t a = 0U; a < 3U; ++a) {
        mid[a] = ((int64_t)bmin[a] + (int64_t)bmax[a]) / 2LL;
        int64_t sp = (int64_t)bmax[a] - (int64_t)bmin[a];
        if (sp > max_span) max_span = sp;
    }

    memset(out_mesh, 0, sizeof(*out_mesh));
    uint32_t sample_cnt = (raw_cnt > BLOOM_MAX_MESH_TRIS) ? BLOOM_MAX_MESH_TRIS : raw_cnt;
    uint32_t valid_cnt = 0U;
    for (uint32_t k = 0U; k < sample_cnt; ++k) {
        uint32_t src_i = (raw_cnt <= BLOOM_MAX_MESH_TRIS)
            ? k
            : (uint32_t)(((uint64_t)k * (uint64_t)raw_cnt) / (uint64_t)sample_cnt);
        const uint8_t *rec = stl_bytes + 84U + (size_t)src_i * 50U;
        int32_t verts[3][3];
        for (uint32_t v = 0U; v < 3U; ++v) {
            for (uint32_t a = 0U; a < 3U; ++a) {
                uint32_t bits = sc_read_u32_le(rec + 12U + (size_t)v * 12U + (size_t)a * 4U);
                int32_t qv = bloom_ieee754_bits_to_q16(bits);
                if (normalize_unit_bounds) {
                    int64_t centered = (int64_t)qv - mid[a];
                    verts[v][a] = sc_clamp64_to_i32((centered * (3LL * BLOOM_Q16_ONE)) / max_span);
                } else {
                    verts[v][a] = qv;
                }
            }
        }
        if (bloom_tri64_init(&out_mesh->map.tris[valid_cnt], valid_cnt + 1U, matter_id, 0U,
                             verts[0], verts[1], verts[2])) {
            ++valid_cnt;
        }
    }
    if (valid_cnt == 0U) return false;
    sc_sort_mesh_tris_morton(out_mesh->map.tris, valid_cnt);
    (void)bloom_bvh_build_from_tris64(out_mesh->map.tris, valid_cnt,
                                      out_mesh->map.bvh_nodes, BLOOM_MAX_MESH_BVH_NODES);
    if (out_tri_count) *out_tri_count = valid_cnt;
    return true;
}

bool bloom_glb_parse_to_mesh64(const uint8_t *glb_bytes,
                               size_t glb_len,
                               uint16_t matter_id,
                               bool normalize_unit_bounds,
                               BloomMeshArena *out_mesh,
                               uint32_t *out_tri_count) {
    if (!glb_bytes || glb_len < 28U || !out_mesh) return false;
    if (sc_read_u32_le(glb_bytes + 0U) != 0x46546C67U) return false;
    if (sc_read_u32_le(glb_bytes + 4U) != 2U) return false;
    uint64_t total_len = (uint64_t)sc_read_u32_le(glb_bytes + 8U);
    if (total_len > (uint64_t)glb_len || total_len < 28ULL || (total_len & 3ULL) != 0ULL) return false;

    uint64_t c0_len = (uint64_t)sc_read_u32_le(glb_bytes + 12U);
    uint32_t c0_type = sc_read_u32_le(glb_bytes + 16U);
    if (c0_type != 0x4E4F534AU || c0_len == 0ULL || (c0_len & 3ULL) != 0ULL) return false;
    if (20ULL + c0_len + 8ULL > total_len) return false;

    const uint8_t *jbuf = glb_bytes + 20U;
    uint64_t jfirst = 0ULL;
    while (jfirst < c0_len &&
           (jbuf[jfirst] == ' ' || jbuf[jfirst] == '\t' || jbuf[jfirst] == '\r' || jbuf[jfirst] == '\n')) {
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

    uint64_t c1_hdr = 20ULL + c0_len;
    uint64_t c1_len = (uint64_t)sc_read_u32_le(glb_bytes + (size_t)c1_hdr);
    uint32_t c1_type = sc_read_u32_le(glb_bytes + (size_t)c1_hdr + 4U);
    if (c1_type != 0x004E4942U || c1_len < 36ULL || (c1_len & 3ULL) != 0ULL) return false;
    if (c1_hdr + 8ULL + c1_len > total_len) return false;

    const uint8_t *bin = glb_bytes + (size_t)c1_hdr + 8U;
    uint32_t raw_tris = (uint32_t)(c1_len / 36ULL);
    if (raw_tris == 0U) return false;

    int32_t bmin[3] = { BLOOM_MAX_Q16, BLOOM_MAX_Q16, BLOOM_MAX_Q16 };
    int32_t bmax[3] = { -BLOOM_MAX_Q16, -BLOOM_MAX_Q16, -BLOOM_MAX_Q16 };
    uint32_t vtotal = raw_tris * 3U;
    for (uint32_t i = 0U; i < vtotal; ++i) {
        for (uint32_t a = 0U; a < 3U; ++a) {
            uint32_t bits = sc_read_u32_le(bin + (size_t)i * 12U + (size_t)a * 4U);
            int32_t qv = 0;
            if (!sc_ieee754_finite_to_q16(bits, &qv)) return false;
            if (qv < bmin[a]) bmin[a] = qv;
            if (qv > bmax[a]) bmax[a] = qv;
        }
    }

    int64_t mid[3];
    int64_t max_span = 1;
    for (uint32_t a = 0U; a < 3U; ++a) {
        mid[a] = ((int64_t)bmin[a] + (int64_t)bmax[a]) / 2LL;
        int64_t sp = (int64_t)bmax[a] - (int64_t)bmin[a];
        if (sp > max_span) max_span = sp;
    }

    memset(out_mesh, 0, sizeof(*out_mesh));
    uint32_t sample_cnt = (raw_tris > BLOOM_MAX_MESH_TRIS) ? BLOOM_MAX_MESH_TRIS : raw_tris;
    uint32_t valid_cnt = 0U;
    for (uint32_t k = 0U; k < sample_cnt; ++k) {
        uint32_t src_t = (raw_tris <= BLOOM_MAX_MESH_TRIS)
            ? k
            : (uint32_t)(((uint64_t)k * (uint64_t)raw_tris) / (uint64_t)sample_cnt);
        int32_t verts[3][3];
        for (uint32_t v = 0U; v < 3U; ++v) {
            for (uint32_t a = 0U; a < 3U; ++a) {
                uint32_t bits = sc_read_u32_le(bin + (size_t)src_t * 36U + (size_t)v * 12U + (size_t)a * 4U);
                int32_t qv = bloom_ieee754_bits_to_q16(bits);
                if (normalize_unit_bounds) {
                    int64_t centered = (int64_t)qv - mid[a];
                    verts[v][a] = sc_clamp64_to_i32((centered * (3LL * BLOOM_Q16_ONE)) / max_span);
                } else {
                    verts[v][a] = qv;
                }
            }
        }
        if (bloom_tri64_init(&out_mesh->map.tris[valid_cnt], valid_cnt + 1U, matter_id, 0U,
                             verts[0], verts[1], verts[2])) {
            ++valid_cnt;
        }
    }
    if (valid_cnt == 0U) return false;
    sc_sort_mesh_tris_morton(out_mesh->map.tris, valid_cnt);
    (void)bloom_bvh_build_from_tris64(out_mesh->map.tris, valid_cnt,
                                      out_mesh->map.bvh_nodes, BLOOM_MAX_MESH_BVH_NODES);
    if (out_tri_count) *out_tri_count = valid_cnt;
    return true;
}

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

int64_t bloom_mesh_eval_sdf_q16(const BloomMeshArena *mesh,
                                uint32_t tri_count,
                                uint32_t bvh_count,
                                bool solid_parity,
                                int64_t px_q16,
                                int64_t py_q16,
                                int64_t pz_q16,
                                uint32_t *out_visited) {
    if (!mesh || tri_count == 0U || bvh_count == 0U) {
        if (out_visited) *out_visited = 0U;
        return (int64_t)BLOOM_MAX_Q16;
    }
    uint32_t tcnt = (tri_count > BLOOM_MAX_MESH_TRIS) ? BLOOM_MAX_MESH_TRIS : tri_count;
    uint32_t ncnt = (bvh_count > BLOOM_MAX_MESH_BVH_NODES) ? BLOOM_MAX_MESH_BVH_NODES : bvh_count;
    const BloomTri64 *tris = mesh->map.tris;
    const BloomBvhNode64 *nodes = mesh->map.bvh_nodes;

    int64_t cpx = sc_clamp64(px_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t cpy = sc_clamp64(py_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t cpz = sc_clamp64(pz_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);

    uint32_t stack[128];
    uint32_t sp = 0U;
    stack[sp++] = 0U;
    int64_t best_ud = (int64_t)BLOOM_MAX_Q16;
    uint32_t visited = 0U;
    uint16_t hit_shell_q12 = 0U;

    while (sp > 0U && visited <= (ncnt * 2U)) {
        uint32_t nid = stack[--sp];
        if (nid >= ncnt) continue;
        const BloomBvhNode64 *nd = &nodes[nid];
        ++visited;

        if (sc_aabb_lower_bound_q16(nd, cpx, cpy, cpz, false) >= best_ud) {
            continue;
        }
        if (nd->is_leaf != 0U) {
            uint32_t first = nd->left_or_first;
            uint32_t cnt = nd->right_or_count;
            for (uint32_t k = 0U; k < cnt && (first + k) < tcnt; ++k) {
                const BloomTri64 *tr = &tris[first + k];
                int64_t ud = bloom_tri64_eval_ud_q16(tr, cpx, cpy, cpz);
                if (ud < best_ud) {
                    best_ud = ud;
                    hit_shell_q12 = tr->shell_q12;
                }
            }
        } else {
            uint32_t l = nd->left_or_first;
            uint32_t r = nd->right_or_count;
            int64_t dl = (l > nid && l < ncnt) ? sc_aabb_lower_bound_q16(&nodes[l], cpx, cpy, cpz, false) : best_ud;
            int64_t dr = (r > nid && r < ncnt) ? sc_aabb_lower_bound_q16(&nodes[r], cpx, cpy, cpz, false) : best_ud;
            if (dl < dr) {
                if (dr < best_ud && sp < 128U) stack[sp++] = r;
                if (dl < best_ud && sp < 128U) stack[sp++] = l;
            } else {
                if (dl < best_ud && sp < 128U) stack[sp++] = l;
                if (dr < best_ud && sp < 128U) stack[sp++] = r;
            }
        }
    }

    int64_t res = best_ud;
    if (solid_parity &&
        cpx >= (int64_t)nodes[0].aabb_min_q16[0] && cpx <= (int64_t)nodes[0].aabb_max_q16[0] &&
        cpy >= (int64_t)nodes[0].aabb_min_q16[1] && cpy <= (int64_t)nodes[0].aabb_max_q16[1] &&
        cpz >= (int64_t)nodes[0].aabb_min_q16[2] && cpz <= (int64_t)nodes[0].aabb_max_q16[2]) {
        int64_t ry = cpy + 3LL;
        int64_t rz = cpz + 5LL;
        uint32_t parity = 0U;
        uint32_t rstack[128];
        uint32_t rsp = 0U;
        rstack[rsp++] = 0U;
        uint32_t rvis = 0U;
        while (rsp > 0U && rvis <= (ncnt * 2U)) {
            uint32_t nid = rstack[--rsp];
            if (nid >= ncnt) continue;
            const BloomBvhNode64 *nd = &nodes[nid];
            ++rvis;
            if (cpx > (int64_t)nd->aabb_max_q16[0] ||
                ry < (int64_t)nd->aabb_min_q16[1] || ry > (int64_t)nd->aabb_max_q16[1] ||
                rz < (int64_t)nd->aabb_min_q16[2] || rz > (int64_t)nd->aabb_max_q16[2]) {
                continue;
            }
            if (nd->is_leaf != 0U) {
                uint32_t first = nd->left_or_first;
                uint32_t cnt = nd->right_or_count;
                for (uint32_t k = 0U; k < cnt && (first + k) < tcnt; ++k) {
                    if (sc_ray_pos_x_hits_tri(&tris[first + k], cpx, ry, rz)) {
                        parity ^= 1U;
                    }
                }
            } else {
                uint32_t l = nd->left_or_first;
                uint32_t r = nd->right_or_count;
                if (r > nid && r < ncnt && rsp < 128U) rstack[rsp++] = r;
                if (l > nid && l < ncnt && rsp < 128U) rstack[rsp++] = l;
            }
        }
        visited += rvis;
        if (parity != 0U) {
            res = -best_ud;
        }
    }

    if (hit_shell_q12 > 0U) {
        res = sc_sat_sub64(res, (int64_t)hit_shell_q12 << 4);
    }
    if (out_visited) *out_visited = visited;
    return res;
}

/* 3. Volume-Preserving Q16.16 Dual-Quaternion Skeletal Skinning */
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

int64_t bloom_sdf_brick64_eval_world_q16(const BloomSdfBrick64 *brick,
                                         int64_t px_q16,
                                         int64_t py_q16,
                                         int64_t pz_q16) {
    if (!brick || brick->voxel_step_q16 <= 0) return (int64_t)BLOOM_MAX_Q16;
    int64_t cpx = sc_clamp64(px_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t cpy = sc_clamp64(py_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t cpz = sc_clamp64(pz_q16, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);

    int64_t span = 3LL * (int64_t)brick->voxel_step_q16;
    int64_t bx0 = (int64_t)brick->origin_q16[0];
    int64_t by0 = (int64_t)brick->origin_q16[1];
    int64_t bz0 = (int64_t)brick->origin_q16[2];

    int64_t qx = sc_clamp64(cpx, bx0, bx0 + span);
    int64_t qy = sc_clamp64(cpy, by0, by0 + span);
    int64_t qz = sc_clamp64(cpz, bz0, bz0 + span);
    int64_t out_d = sc_len3_q16(cpx - qx, cpy - qy, cpz - qz);
    int64_t samp = bloom_sdf_brick64_sample_q16(brick, qx, qy, qz);
    return samp + out_d;
}

/* 5. Constitutive Physical Matter Optics & 128-Hex Wire Protocol */
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

/* 6. Thin-Lens Physical Orbit Camera & 3D Raycaster */
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

static int64_t sc_eval_inst_sdf_world_q16(const BloomEngineArena *arena,
                                          const BloomPrefabInst64 *inst,
                                          int64_t px,
                                          int64_t py,
                                          int64_t pz) {
    uint32_t pid = inst->prefab_id;
    if (pid == 0U || pid > BLOOM_MAX_PREFABS) return (int64_t)BLOOM_MAX_Q16;
    const BloomPrefabDesc64 *pf = &arena->map.prefabs[pid - 1U];

    int64_t sc = (inst->scale_q16 > 4096)
        ? sc_clamp64((int64_t)inst->scale_q16, 4096, 256LL * BLOOM_Q16_ONE)
        : (int64_t)BLOOM_Q16_ONE;
    int32_t rel[3] = {
        sc_clamp64_to_i32(px - (int64_t)inst->pos_q16[0]),
        sc_clamp64_to_i32(py - (int64_t)inst->pos_q16[1]),
        sc_clamp64_to_i32(pz - (int64_t)inst->pos_q16[2])
    };
    int32_t rot_rel[3];
    bloom_rot_inv_euler_q16(rel, inst->rot_euler_q16, rot_rel);

    int64_t lx = ((int64_t)rot_rel[0] * 65536LL) / sc;
    int64_t ly = ((int64_t)rot_rel[1] * 65536LL) / sc;
    int64_t lz = ((int64_t)rot_rel[2] * 65536LL) / sc;

    int64_t d_local = 0;
    if (pf->csg_count > 0U &&
        (size_t)pf->csg_start_idx + (size_t)pf->csg_count <= BLOOM_MAX_CSG_OPS) {
        uint32_t dummy_mat = 0U;
        d_local = bloom_csg_eval_program_q16(
            &arena->map.csg_ops[pf->csg_start_idx],
            pf->csg_count, lx, ly, lz, &dummy_mat);
    } else {
        d_local = bloom_prefab_eval_sdf_q16(pf, lx, ly, lz, inst->param_override_q16);
    }
    d_local = sc_clamp64(d_local, -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    return (d_local * sc) >> 16;
}

static int64_t sc_eval_scene_core_q16(const BloomEngineArena *arena,
                                      const BloomMeshArena *opt_mesh,
                                      int64_t px,
                                      int64_t py,
                                      int64_t pz,
                                      bool apply_clip,
                                      uint32_t *out_inst_id,
                                      uint32_t *out_prefab_id,
                                      uint32_t *out_matter_id,
                                      uint32_t *inout_bvh_vis) {
    const BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    const BloomSectionSpec64 *sec = &arena->map.section_spec;
    uint32_t inst_n = (hdr->inst_count > BLOOM_MAX_INSTANCES) ? BLOOM_MAX_INSTANCES : hdr->inst_count;

    int64_t min_d = (int64_t)BLOOM_MAX_Q16;
    uint32_t best_inst = 0U;
    uint32_t best_prefab = 0U;
    uint32_t best_matter = 0U;

    for (uint32_t i = 0U; i < inst_n; ++i) {
        const BloomPrefabInst64 *inst = &arena->map.instances[i];
        if ((inst->flags & BLOOM_INST_FLAG_ACTIVE) == 0U) continue;
        uint32_t pid = inst->prefab_id;
        if (pid == 0U || pid > BLOOM_MAX_PREFABS) continue;
        const BloomPrefabDesc64 *pf = &arena->map.prefabs[pid - 1U];

        int64_t sc = (inst->scale_q16 > 4096)
            ? sc_clamp64((int64_t)inst->scale_q16, 4096, 256LL * BLOOM_Q16_ONE)
            : (int64_t)BLOOM_Q16_ONE;
        int64_t br = sc_clamp64((int64_t)pf->bounding_radius_q16, 0, 1024LL * BLOOM_Q16_ONE);
        if (br > 0) {
            int64_t lx = ((px - (int64_t)inst->pos_q16[0]) * 65536LL) / sc;
            int64_t ly = ((py - (int64_t)inst->pos_q16[1]) * 65536LL) / sc;
            int64_t lz = ((pz - (int64_t)inst->pos_q16[2]) * 65536LL) / sc;
            int64_t bound_d = sc_clamp64(sc_len3_q16(lx, ly, lz) - (br + 32768LL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
            if (((bound_d * sc) >> 16) >= min_d) continue;
        }

        int64_t d_world = sc_eval_inst_sdf_world_q16(arena, inst, px, py, pz);
        if (apply_clip) {
            d_world = bloom_section_eval_clip_q16(d_world, px, py, pz, hdr, sec, NULL);
        }
        if (d_world < min_d) {
            min_d = d_world;
            best_inst = (inst->inst_id != 0U) ? inst->inst_id : (i + 1U);
            best_prefab = pid;
            best_matter = (inst->matter_override_id < BLOOM_MAX_MATTERS &&
                           (inst->flags & BLOOM_INST_FLAG_OVERRIDDEN) != 0U)
                ? inst->matter_override_id
                : pf->matter_id;
        }
    }

    if (sec->global_csg_count > 0U &&
        (size_t)sec->global_csg_start + (size_t)sec->global_csg_count <= BLOOM_MAX_CSG_OPS) {
        uint32_t vis = 0U;
        uint32_t csg_mat = arena->map.csg_ops[sec->global_csg_start].matter_id;
        uint32_t bvh_n = (hdr->bvh_count > BLOOM_MAX_BVH_NODES) ? BLOOM_MAX_BVH_NODES : hdr->bvh_count;
        int64_t d_csg = (sec->global_csg_start == 0U && bvh_n > 0U)
            ? bloom_bvh_eval_csg_q16(arena->map.bvh_nodes, bvh_n, arena->map.csg_ops,
                                     sec->global_csg_count, px, py, pz, &vis)
            : bloom_csg_eval_program_q16(&arena->map.csg_ops[sec->global_csg_start],
                                         sec->global_csg_count, px, py, pz, &csg_mat);
        if (inout_bvh_vis) *inout_bvh_vis += vis;
        if (apply_clip) {
            d_csg = bloom_section_eval_clip_q16(d_csg, px, py, pz, hdr, sec, NULL);
        }
        if (d_csg < min_d) {
            min_d = d_csg;
            best_inst = 99U;
            best_prefab = 0U;
            best_matter = csg_mat;
        }
    }

    if (sec->joint_count > 0U) {
        uint32_t jcnt = (sec->joint_count > BLOOM_MAX_JOINTS) ? BLOOM_MAX_JOINTS : sec->joint_count;
        int64_t d_sk = bloom_skeleton_eval_sdf_q16(arena->map.joints, jcnt, px, py, pz);
        if (apply_clip) {
            d_sk = bloom_section_eval_clip_q16(d_sk, px, py, pz, hdr, sec, NULL);
        }
        if (d_sk < min_d) {
            min_d = d_sk;
            best_inst = 100U;
            best_prefab = 0U;
            best_matter = arena->map.joints[0].matter_id;
        }
    }

    if (sec->brick_count > 0U) {
        uint32_t bcnt = (sec->brick_count > BLOOM_MAX_BRICKS) ? BLOOM_MAX_BRICKS : sec->brick_count;
        for (uint32_t b = 0U; b < bcnt; ++b) {
            int64_t d_br = bloom_sdf_brick64_eval_world_q16(&arena->map.bricks[b], px, py, pz);
            if (apply_clip) {
                d_br = bloom_section_eval_clip_q16(d_br, px, py, pz, hdr, sec, NULL);
            }
            if (d_br < min_d) {
                min_d = d_br;
                best_inst = 101U;
                best_prefab = 0U;
                best_matter = arena->map.bricks[b].matter_id;
            }
        }
    }

    if (opt_mesh && sec->mesh_tri_count > 0U && sec->mesh_bvh_count > 0U) {
        uint32_t vis = 0U;
        bool solid = (sec->inspect_flags & BLOOM_INSPECT_MESH_SOLID) != 0U;
        int64_t d_mesh = bloom_mesh_eval_sdf_q16(
            opt_mesh, sec->mesh_tri_count, sec->mesh_bvh_count, solid, px, py, pz, &vis);
        if (inout_bvh_vis) *inout_bvh_vis += vis;
        if (apply_clip) {
            d_mesh = bloom_section_eval_clip_q16(d_mesh, px, py, pz, hdr, sec, NULL);
        }
        if (d_mesh < min_d) {
            min_d = d_mesh;
            best_inst = 102U;
            best_prefab = 0U;
            best_matter = sec->mesh_matter_style & 0xFFFFU;
        }
    }

    if (out_inst_id) *out_inst_id = best_inst;
    if (out_prefab_id) *out_prefab_id = best_prefab;
    if (out_matter_id) *out_matter_id = best_matter;
    return min_d;
}

static int64_t sc_eval_scene_at_q16(const BloomEngineArena *arena,
                                    const BloomMeshArena *opt_mesh,
                                    int64_t px,
                                    int64_t py,
                                    int64_t pz,
                                    uint32_t *out_inst_id,
                                    uint32_t *out_prefab_id,
                                    uint32_t *inout_bvh_vis) {
    return sc_eval_scene_core_q16(arena, opt_mesh, px, py, pz, true,
                                  out_inst_id, out_prefab_id, NULL, inout_bvh_vis);
}

static int64_t sc_hess_proj_q16(const int32_t a[3],
                                const int64_t hmat[3][3],
                                const int32_t b[3],
                                int64_t glen_q16) {
    int64_t hb0 = (hmat[0][0] * (int64_t)b[0] + hmat[0][1] * (int64_t)b[1] + hmat[0][2] * (int64_t)b[2]) >> 16;
    int64_t hb1 = (hmat[1][0] * (int64_t)b[0] + hmat[1][1] * (int64_t)b[1] + hmat[1][2] * (int64_t)b[2]) >> 16;
    int64_t hb2 = (hmat[2][0] * (int64_t)b[0] + hmat[2][1] * (int64_t)b[1] + hmat[2][2] * (int64_t)b[2]) >> 16;
    int64_t num = ((int64_t)a[0] * hb0 + (int64_t)a[1] * hb1 + (int64_t)a[2] * hb2) >> 16;
    num = sc_clamp64(num, -INT64_C(0x3FFFFFFFFFF), INT64_C(0x3FFFFFFFFFF));
    return sc_clamp64((num * 65536LL) / sc_max64(glen_q16, 4096LL),
                      -1024LL * BLOOM_Q16_ONE, 1024LL * BLOOM_Q16_ONE);
}

static void sc_eval_hessian3_q16(const BloomEngineArena *arena,
                                 const BloomMeshArena *opt_mesh,
                                 int64_t px_q16,
                                 int64_t py_q16,
                                 int64_t pz_q16,
                                 int64_t *out_f0,
                                 int64_t *out_glen,
                                 int32_t out_n[3],
                                 uint32_t *out_mid,
                                 int64_t out_hmat[3][3]) {
    int64_t cx = sc_clamp64(px_q16, -SC_MAX_COORD_Q16 + 16384LL, SC_MAX_COORD_Q16 - 16384LL);
    int64_t cy = sc_clamp64(py_q16, -SC_MAX_COORD_Q16 + 16384LL, SC_MAX_COORD_Q16 - 16384LL);
    int64_t cz = sc_clamp64(pz_q16, -SC_MAX_COORD_Q16 + 16384LL, SC_MAX_COORD_Q16 - 16384LL);
    const int64_t h = 4096LL; /* 1/16 unit: 1/(2h)=8, 1/h^2=256, 1/(4h^2)=64 */

    uint32_t mid = 0U;
    int64_t f0   = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx, cy, cz, false, NULL, NULL, &mid, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_px = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx + h, cy, cz, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_mx = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx - h, cy, cz, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_py = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx, cy + h, cz, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_my = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx, cy - h, cz, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_pz = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx, cy, cz + h, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_mz = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx, cy, cz - h, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);

    int64_t f_pp0 = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx + h, cy + h, cz, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_pm0 = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx + h, cy - h, cz, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_mp0 = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx - h, cy + h, cz, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_mm0 = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx - h, cy - h, cz, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);

    int64_t f_0pp = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx, cy + h, cz + h, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_0pm = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx, cy + h, cz - h, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_0mp = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx, cy - h, cz + h, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_0mm = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx, cy - h, cz - h, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);

    int64_t f_p0p = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx + h, cy, cz + h, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_p0m = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx + h, cy, cz - h, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_m0p = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx - h, cy, cz + h, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);
    int64_t f_m0m = sc_clamp64(sc_eval_scene_core_q16(arena, opt_mesh, cx - h, cy, cz - h, false, NULL, NULL, NULL, NULL), -SC_MAX_COORD_Q16, SC_MAX_COORD_Q16);

    int64_t gx = (f_px - f_mx) * 8LL;
    int64_t gy = (f_py - f_my) * 8LL;
    int64_t gz = (f_pz - f_mz) * 8LL;
    *out_f0 = f0;
    *out_glen = sc_max64(sc_len3_q16(gx, gy, gz), 4096LL);
    *out_mid = mid;
    sc_vec3_norm_q16(gx, gy, gz, out_n);

    int64_t d2x = sc_clamp64(f_px - 2LL * f0 + f_mx, -0x0FFFFFFFLL, 0x0FFFFFFFLL);
    int64_t d2y = sc_clamp64(f_py - 2LL * f0 + f_my, -0x0FFFFFFFLL, 0x0FFFFFFFLL);
    int64_t d2z = sc_clamp64(f_pz - 2LL * f0 + f_mz, -0x0FFFFFFFLL, 0x0FFFFFFFLL);
    int64_t dxy = sc_clamp64((f_pp0 - f_pm0) - (f_mp0 - f_mm0), -0x0FFFFFFFLL, 0x0FFFFFFFLL);
    int64_t dyz = sc_clamp64((f_0pp - f_0pm) - (f_0mp - f_0mm), -0x0FFFFFFFLL, 0x0FFFFFFFLL);
    int64_t dxz = sc_clamp64((f_p0p - f_p0m) - (f_m0p - f_m0m), -0x0FFFFFFFLL, 0x0FFFFFFFLL);

    int64_t hxx = sc_clamp64(d2x * 256LL, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
    int64_t hyy = sc_clamp64(d2y * 256LL, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
    int64_t hzz = sc_clamp64(d2z * 256LL, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
    int64_t hxy = sc_clamp64(dxy * 64LL, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
    int64_t hyz = sc_clamp64(dyz * 64LL, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
    int64_t hxz = sc_clamp64(dxz * 64LL, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);

    out_hmat[0][0] = hxx; out_hmat[0][1] = hxy; out_hmat[0][2] = hxz;
    out_hmat[1][0] = hxy; out_hmat[1][1] = hyy; out_hmat[1][2] = hyz;
    out_hmat[2][0] = hxz; out_hmat[2][1] = hyz; out_hmat[2][2] = hzz;
}

void bloom_scene_eval_jet2_q16(const BloomEngineArena *arena,
                               const BloomMeshArena *opt_mesh,
                               int64_t px_q16,
                               int64_t py_q16,
                               int64_t pz_q16,
                               BloomFieldJet64 *out_jet) {
    if (!out_jet) return;
    memset(out_jet, 0, sizeof(*out_jet));
    if (!arena) return;

    int64_t f0 = 0LL;
    int64_t glen = 4096LL;
    int32_t n[3] = { 0, BLOOM_Q16_ONE, 0 };
    uint32_t mid = 0U;
    int64_t hmat[3][3];
    sc_eval_hessian3_q16(arena, opt_mesh, px_q16, py_q16, pz_q16,
                         &f0, &glen, n, &mid, hmat);

    int32_t up_ref[3];
    if (sc_abs64((int64_t)n[1]) < 52000LL) {
        up_ref[0] = 0; up_ref[1] = BLOOM_Q16_ONE; up_ref[2] = 0;
    } else {
        up_ref[0] = BLOOM_Q16_ONE; up_ref[1] = 0; up_ref[2] = 0;
    }
    int32_t t1[3], t2[3];
    sc_vec3_norm_q16(((int64_t)up_ref[1] * n[2] - (int64_t)up_ref[2] * n[1]) >> 16,
                     ((int64_t)up_ref[2] * n[0] - (int64_t)up_ref[0] * n[2]) >> 16,
                     ((int64_t)up_ref[0] * n[1] - (int64_t)up_ref[1] * n[0]) >> 16, t1);
    sc_vec3_norm_q16(((int64_t)n[1] * t1[2] - (int64_t)n[2] * t1[1]) >> 16,
                     ((int64_t)n[2] * t1[0] - (int64_t)n[0] * t1[2]) >> 16,
                     ((int64_t)n[0] * t1[1] - (int64_t)n[1] * t1[0]) >> 16, t2);

    int64_t s11 = sc_hess_proj_q16(t1, hmat, t1, glen);
    int64_t s22 = sc_hess_proj_q16(t2, hmat, t2, glen);
    int64_t s12 = sc_hess_proj_q16(t1, hmat, t2, glen);

    int64_t diff_s = s11 - s22;
    int64_t two_s12 = 2LL * s12;
    int64_t aniso = sc_len2_q16(diff_s, two_s12);

    int64_t bx = BLOOM_Q16_ONE;
    int64_t by = 0;
    if (aniso > 64LL) {
        if (diff_s >= 0) {
            bx = diff_s + aniso;
            by = two_s12;
        } else {
            bx = two_s12;
            by = aniso - diff_s;
        }
    }
    int64_t blen = sc_max64(sc_len2_q16(bx, by), 1LL);
    int64_t ux = sc_clamp64(bx, -0x3FFFFFFFFFFLL, 0x3FFFFFFFFFFLL) * 65536LL / blen;
    int64_t uy = sc_clamp64(by, -0x3FFFFFFFFFFLL, 0x3FFFFFFFFFFLL) * 65536LL / blen;

    int32_t e1[3];
    sc_vec3_norm_q16((ux * t1[0] + uy * t2[0]) >> 16,
                     (ux * t1[1] + uy * t2[1]) >> 16,
                     (ux * t1[2] + uy * t2[2]) >> 16, e1);

    out_jet->sdf_q16 = sc_clamp64_to_i32(f0);
    out_jet->grad_q16[0] = n[0];
    out_jet->grad_q16[1] = n[1];
    out_jet->grad_q16[2] = n[2];
    out_jet->hess_diag_q16[0] = (int32_t)hmat[0][0];
    out_jet->hess_diag_q16[1] = (int32_t)hmat[1][1];
    out_jet->hess_diag_q16[2] = (int32_t)hmat[2][2];
    out_jet->mean_curv_q16 = sc_clamp64_to_i32((s11 + s22) / 2LL);
    out_jet->gauss_curv_q16 = sc_clamp64_to_i32(((s11 * s22) - (s12 * s12)) >> 16);
    out_jet->prin_dir_q16[0] = e1[0];
    out_jet->prin_dir_q16[1] = e1[1];
    out_jet->prin_dir_q16[2] = e1[2];
    out_jet->anisotropy_q16 = sc_clamp64_to_i32(aniso);
    out_jet->matter_id = mid;
    sc_stamp_mac(out_jet, 0x4E720064ULL, &out_jet->mac_lo32, &out_jet->mac_hi32);
}

bool bloom_scene_certify_hessian_psd_q16(const BloomEngineArena *arena,
                                         const BloomMeshArena *opt_mesh,
                                         int64_t px_q16,
                                         int64_t py_q16,
                                         int64_t pz_q16,
                                         int64_t reg_diag_q16,
                                         int64_t *out_mu_psd_q32) {
    if (out_mu_psd_q32) {
        *out_mu_psd_q32 = -1LL;
    }
    if (!arena) return false;

    int64_t f0 = 0LL;
    int64_t glen = 4096LL;
    int32_t n[3] = { 0, BLOOM_Q16_ONE, 0 };
    uint32_t mid = 0U;
    int64_t hmat[3][3];
    sc_eval_hessian3_q16(arena, opt_mesh, px_q16, py_q16, pz_q16,
                         &f0, &glen, n, &mid, hmat);

    const int64_t bound_q16 = 1024LL * BLOOM_Q16_ONE;
    int64_t reg = sc_clamp64(reg_diag_q16, -bound_q16, bound_q16);
    int64_t A[9];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            int64_t hij = sc_clamp64(hmat[i][j], -bound_q16, bound_q16);
            int64_t aij_q16 = sc_clamp64(hij + ((i == j) ? reg : 0LL), -2LL * bound_q16, 2LL * bound_q16);
            A[i * 3 + j] = aij_q16 * 65536LL;
        }
    }

    int64_t L[9];
    memset(L, 0, sizeof(L));
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j <= i; ++j) {
            flow_i128 acc = (flow_i128)A[i * 3 + j];
            for (int k = 0; k < j; ++k) {
                acc -= (flow_i128)L[i * 3 + k] * (flow_i128)L[j * 3 + k];
            }
            if (i == j) {
                if (acc > 0) {
                    uint64_t r = flow_isqrt_round_u128((flow_u128)acc);
                    if (r > 0ULL && ((flow_u128)r * (flow_u128)r) > (flow_u128)acc) {
                        --r;
                    }
                    L[i * 3 + i] = (int64_t)r;
                } else {
                    L[i * 3 + i] = 0LL;
                }
            } else if (L[j * 3 + j] > 0LL) {
                int64_t s64 = (acc > (flow_i128)INT64_MAX) ? INT64_MAX
                            : (acc < (flow_i128)INT64_MIN) ? INT64_MIN
                            : (int64_t)acc;
                L[i * 3 + j] = s64 / L[j * 3 + j];
            }
        }
    }

    int64_t mu_floor = 0LL;
    if (flow_cholesky_sdd_floor_i64(3, 3, A, L, &mu_floor) != 0 ||
        flow_verify_cholesky_sdd_i64(3, 3, A, L, mu_floor) != 0) {
        return false;
    }
    if (out_mu_psd_q32) {
        *out_mu_psd_q32 = mu_floor;
    }
    return mu_floor >= 0LL;
}

bool bloom_scene_raycast_ex64(BloomEngineArena *arena,
                              const BloomMeshArena *opt_mesh,
                              int32_t cursor_norm_x_q16,
                              int32_t cursor_norm_y_q16,
                              BloomRaycastHit64 *out_hit) {
    if (!arena) return false;
    BloomRaycastHit64 hit;
    memset(&hit, 0, sizeof(hit));
    hit.ray_origin_q16[0] = cursor_norm_x_q16;
    hit.ray_origin_q16[1] = cursor_norm_y_q16;

    const BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    const BloomCamera64 *cam = &arena->map.camera;

    int32_t fwd[3], right[3], up_v[3];
    sc_vec3_norm_q16((int64_t)cam->target_q16[0] - cam->eye_q16[0],
                     (int64_t)cam->target_q16[1] - cam->eye_q16[1],
                     (int64_t)cam->target_q16[2] - cam->eye_q16[2], fwd);
    sc_vec3_norm_q16(fwd[2], 0, -fwd[0], right);
    up_v[0] = (int32_t)(((int64_t)fwd[1] * right[2] - (int64_t)fwd[2] * right[1]) >> 16);
    up_v[1] = (int32_t)(((int64_t)fwd[2] * right[0] - (int64_t)fwd[0] * right[2]) >> 16);
    up_v[2] = (int32_t)(((int64_t)fwd[0] * right[1] - (int64_t)fwd[1] * right[0]) >> 16);

    int64_t aspect_q16 = (hdr->height_px > 0U)
        ? sc_clamp64(((int64_t)hdr->width_px << 16) / (int64_t)hdr->height_px, 4096, 16LL * BLOOM_Q16_ONE)
        : BLOOM_Q16_ONE;
    int64_t fov_z_q16 = sc_clamp64(((int64_t)cam->focal_mm_q16) / 28LL, 45875, 16LL * BLOOM_Q16_ONE);

    int64_t cnx = sc_clamp64((int64_t)cursor_norm_x_q16, -4LL * BLOOM_Q16_ONE, 4LL * BLOOM_Q16_ONE);
    int64_t cny = sc_clamp64((int64_t)cursor_norm_y_q16, -4LL * BLOOM_Q16_ONE, 4LL * BLOOM_Q16_ONE);

    int64_t uvx = ((cnx * aspect_q16) >> 17);
    int64_t uvy = -(cny >> 1);

    int32_t rd[3];
    sc_vec3_norm_q16(((int64_t)right[0] * uvx + (int64_t)up_v[0] * uvy + (int64_t)fwd[0] * fov_z_q16) >> 16,
                     ((int64_t)right[1] * uvx + (int64_t)up_v[1] * uvy + (int64_t)fwd[1] * fov_z_q16) >> 16,
                     ((int64_t)right[2] * uvx + (int64_t)up_v[2] * uvy + (int64_t)fwd[2] * fov_z_q16) >> 16,
                     rd);

    int64_t t = 8192;
    const int64_t t_max = 32LL * BLOOM_Q16_ONE;
    uint32_t total_bvh_vis = 0U;

    for (uint32_t step = 0U; step < 64U && t < t_max; ++step) {
        int64_t px = cam->eye_q16[0] + (((int64_t)rd[0] * t) >> 16);
        int64_t py = cam->eye_q16[1] + (((int64_t)rd[1] * t) >> 16);
        int64_t pz = cam->eye_q16[2] + (((int64_t)rd[2] * t) >> 16);

        uint32_t best_inst = 0U;
        uint32_t best_prefab = 0U;
        int64_t min_d = sc_eval_scene_at_q16(arena, opt_mesh, px, py, pz,
                                             &best_inst, &best_prefab, &total_bvh_vis);

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

            int64_t eps = 1024;
            int64_t gx = sc_sat_sub64(sc_eval_scene_at_q16(arena, opt_mesh, px + eps, py, pz, NULL, NULL, NULL),
                                      sc_eval_scene_at_q16(arena, opt_mesh, px - eps, py, pz, NULL, NULL, NULL));
            int64_t gy = sc_sat_sub64(sc_eval_scene_at_q16(arena, opt_mesh, px, py + eps, pz, NULL, NULL, NULL),
                                      sc_eval_scene_at_q16(arena, opt_mesh, px, py - eps, pz, NULL, NULL, NULL));
            int64_t gz = sc_sat_sub64(sc_eval_scene_at_q16(arena, opt_mesh, px, py, pz + eps, NULL, NULL, NULL),
                                      sc_eval_scene_at_q16(arena, opt_mesh, px, py, pz - eps, NULL, NULL, NULL));
            sc_vec3_norm_q16(gx, gy, gz, hit.hit_normal_q16);

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

bool bloom_scene_raycast64(BloomEngineArena *arena,
                           int32_t cursor_norm_x_q16,
                           int32_t cursor_norm_y_q16,
                           BloomRaycastHit64 *out_hit) {
    return bloom_scene_raycast_ex64(arena, NULL, cursor_norm_x_q16, cursor_norm_y_q16, out_hit);
}

void bloom_metric_eval_scale64(int32_t beta_q16,
                               int32_t phi_q16,
                               int32_t flux_q16,
                               const int32_t vel_dir_q16[3],
                               const int32_t center_q16[3],
                               BloomMetricScale64 *out_metric) {
    if (!out_metric) return;
    memset(out_metric, 0, sizeof(*out_metric));

    int64_t b = sc_clamp64((int64_t)beta_q16, -62259LL, 62259LL);
    int64_t phi = sc_clamp64((int64_t)phi_q16, -32768LL, 49152LL);
    int64_t flux = sc_clamp64((int64_t)flux_q16, -262144LL, 262144LL);

    int64_t om_b2 = 65536LL - ((b * b) >> 16);
    if (om_b2 < 1024LL) om_b2 = 1024LL;
    int64_t sqrt_om = sc_max64((int64_t)bloom_isqrt_u64_to_i32((uint64_t)om_b2 << 16ULL), 1LL);
    int32_t gamma_q16 = sc_clamp64_to_i32((65536LL << 16) / sqrt_om);

    uint64_t d_num = ((uint64_t)(65536LL + b)) << 32ULL;
    uint64_t d_den = (uint64_t)(65536LL - b);
    int32_t doppler_q16 = bloom_isqrt_u64_to_i32(d_num / d_den);

    int32_t z_q16 = BLOOM_Q16_ONE;
    if (phi > 0LL) {
        if (phi >= 32768LL) {
            z_q16 = 0;
        } else {
            uint64_t term = (uint64_t)(65536LL - 2LL * phi);
            z_q16 = bloom_isqrt_u64_to_i32(term << 16ULL);
        }
    }

    int32_t vdir[3] = { 0, 0, BLOOM_Q16_ONE };
    if (vel_dir_q16) {
        sc_vec3_norm_q16((int64_t)vel_dir_q16[0], (int64_t)vel_dir_q16[1], (int64_t)vel_dir_q16[2], vdir);
    }

    out_metric->proper_time_q16 = (int32_t)sqrt_om;
    out_metric->lorentz_beta_q16 = (int32_t)b;
    out_metric->lorentz_gamma_q16 = gamma_q16;
    out_metric->doppler_factor_q16 = doppler_q16;
    out_metric->metric_potential_q16 = (int32_t)phi;
    out_metric->grav_redshift_q16 = z_q16;
    out_metric->flux_pressure_q16 = (int32_t)flux;
    out_metric->spatial_scale_q16 = BLOOM_Q16_ONE;
    out_metric->rel_vel_dir_q16[0] = vdir[0];
    out_metric->rel_vel_dir_q16[1] = vdir[1];
    out_metric->rel_vel_dir_q16[2] = vdir[2];
    if (center_q16) {
        out_metric->center_pos_q16[0] = center_q16[0];
        out_metric->center_pos_q16[1] = center_q16[1];
        out_metric->center_pos_q16[2] = center_q16[2];
    }
    sc_stamp_mac(out_metric, 0x3E721C64ULL, &out_metric->mac_lo32, &out_metric->mac_hi32);
}

static inline uint32_t sc_rgb_luma255(const uint8_t *px) {
    return (54U * (uint32_t)px[0] + 183U * (uint32_t)px[1] + 19U * (uint32_t)px[2]) >> 8U;
}

bool bloom_moodboard_extract_rgb24(const uint8_t *rgb,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t stride_bytes,
                                   BloomMoodboard64 *out_mood) {
    if (!rgb || !out_mood || width < 2U || height < 2U || stride_bytes < width * 3U) {
        return false;
    }
    memset(out_mood, 0, sizeof(*out_mood));

    uint32_t cols = (width < 32U) ? width : 32U;
    uint32_t rows = (height < 32U) ? height : 32U;
    uint64_t sum_r = 0ULL, sum_g = 0ULL, sum_b = 0ULL;
    uint64_t sum_luma = 0ULL, sum_edge = 0ULL;
    int64_t sum_warm = 0;
    uint32_t min_y = 255U, max_y = 0U;
    uint32_t spec_cnt = 0U, total_cnt = 0U;
    uint32_t best_chroma = 0U;
    uint32_t accent_r = 128U, accent_g = 128U, accent_b = 128U;
    uint64_t bin_sum[4] = { 0ULL, 0ULL, 0ULL, 0ULL };
    uint32_t bin_cnt[4] = { 0U, 0U, 0U, 0U };

    for (uint32_t r = 0U; r < rows; ++r) {
        uint32_t sy = (r * (height - 1U)) / (rows - 1U);
        uint32_t y0 = (sy > 0U) ? (sy - 1U) : 0U;
        uint32_t y1 = (sy + 1U < height) ? (sy + 1U) : sy;
        for (uint32_t c = 0U; c < cols; ++c) {
            uint32_t sx = (c * (width - 1U)) / (cols - 1U);
            uint32_t x0 = (sx > 0U) ? (sx - 1U) : 0U;
            uint32_t x1 = (sx + 1U < width) ? (sx + 1U) : sx;

            const uint8_t *p = rgb + (size_t)sy * (size_t)stride_bytes + (size_t)sx * 3U;
            uint32_t pr = p[0], pg = p[1], pb = p[2];
            uint32_t lum = sc_rgb_luma255(p);

            sum_r += pr; sum_g += pg; sum_b += pb;
            sum_luma += lum;
            sum_warm += (int64_t)pr - (int64_t)pb;
            if (lum < min_y) min_y = lum;
            if (lum > max_y) max_y = lum;
            if (lum > 220U) spec_cnt++;

            uint32_t mx = (pr > pg) ? ((pr > pb) ? pr : pb) : ((pg > pb) ? pg : pb);
            uint32_t mn = (pr < pg) ? ((pr < pb) ? pr : pb) : ((pg < pb) ? pg : pb);
            uint32_t chroma = mx - mn;
            if (chroma > best_chroma || total_cnt == 0U) {
                best_chroma = chroma;
                accent_r = pr; accent_g = pg; accent_b = pb;
            }

            uint32_t lx0 = sc_rgb_luma255(rgb + (size_t)sy * (size_t)stride_bytes + (size_t)x0 * 3U);
            uint32_t lx1 = sc_rgb_luma255(rgb + (size_t)sy * (size_t)stride_bytes + (size_t)x1 * 3U);
            uint32_t ly0 = sc_rgb_luma255(rgb + (size_t)y0 * (size_t)stride_bytes + (size_t)sx * 3U);
            uint32_t ly1 = sc_rgb_luma255(rgb + (size_t)y1 * (size_t)stride_bytes + (size_t)sx * 3U);
            uint32_t gx_d = (lx1 >= lx0) ? (lx1 - lx0) : (lx0 - lx1);
            uint32_t gy_d = (ly1 >= ly0) ? (ly1 - ly0) : (ly0 - ly1);
            sum_edge += gx_d + gy_d;

            uint32_t bk = (lum >> 6U) & 3U;
            bin_sum[bk] += lum;
            bin_cnt[bk]++;
            total_cnt++;
        }
    }

    uint32_t avg_r = (uint32_t)(sum_r / total_cnt);
    uint32_t avg_g = (uint32_t)(sum_g / total_cnt);
    uint32_t avg_b = (uint32_t)(sum_b / total_cnt);
    uint32_t avg_y = (uint32_t)(sum_luma / total_cnt);

    out_mood->dominant_rgb24 = (avg_r << 16U) | (avg_g << 8U) | avg_b;
    out_mood->accent_rgb24 = (accent_r << 16U) | (accent_g << 8U) | accent_b;
    out_mood->warmth_q16 = (int32_t)((sum_warm * (int64_t)BLOOM_Q16_ONE) / ((int64_t)total_cnt * 255LL));
    out_mood->contrast_q16 = (int32_t)(((uint64_t)(max_y - min_y) * (uint64_t)BLOOM_Q16_ONE) / 255ULL);
    out_mood->edge_density_q16 = sc_clamp64_to_i32((int64_t)((sum_edge * (uint64_t)BLOOM_Q16_ONE) / ((uint64_t)total_cnt * 255ULL)));
    out_mood->specular_hint_q16 = (int32_t)(((uint64_t)spec_cnt * (uint64_t)BLOOM_Q16_ONE) / (uint64_t)total_cnt);

    for (uint32_t k = 0U; k < 4U; ++k) {
        uint32_t fallback_y = k * 64U + 32U;
        uint32_t by = (bin_cnt[k] > 0U) ? (uint32_t)(bin_sum[k] / bin_cnt[k]) : fallback_y;
        out_mood->palette_luma_q16[k] = (int32_t)(((uint64_t)by * (uint64_t)BLOOM_Q16_ONE) / 255ULL);
    }

    uint32_t matter = BLOOM_MATTER_CRYSTAL_GLASS;
    if (avg_y < 55U) {
        matter = BLOOM_MATTER_OBSIDIAN;
    } else if (out_mood->warmth_q16 > 10000 && avg_r > 120U) {
        matter = BLOOM_MATTER_AURUM_GOLD;
    } else if (avg_b > avg_r + 24U && avg_b >= avg_g) {
        matter = BLOOM_MATTER_SAPPHIRE;
    } else if (avg_g > avg_r + 20U && avg_g > avg_b + 12U) {
        matter = BLOOM_MATTER_IMPERIAL_JADE;
    } else if (out_mood->specular_hint_q16 > 8192 || out_mood->contrast_q16 > 36000) {
        matter = BLOOM_MATTER_TITANIUM;
    }
    out_mood->inferred_matter_id = matter;
    out_mood->inferred_prim_mask = (out_mood->edge_density_q16 > 16384)
        ? BLOOM_CSG_MASK_PACK(BLOOM_CSG_PRIM_OCTAHEDRON, BLOOM_CSG_PRIM_BOX, BLOOM_CSG_BOOL_INTERSECT, 0, 1)
        : BLOOM_CSG_MASK_PACK(BLOOM_CSG_PRIM_SPHERE, BLOOM_CSG_PRIM_TORUS, BLOOM_CSG_BOOL_SMOOTH_UNION, 4, 0);

    sc_stamp_mac(out_mood, 0x400DB064ULL, &out_mood->mac_lo32, &out_mood->mac_hi32);
    return true;
}

void bloom_moodboard_compile_to_ssot64(const BloomMoodboard64 *mood,
                                       uint32_t style_id,
                                       uint32_t prefab_id,
                                       BloomArtStyle64 *out_style,
                                       BloomPrefabDesc64 *out_prefab) {
    if (!mood) return;
    uint32_t matter = mood->inferred_matter_id & 7U;
    uint32_t sid = (style_id >= 1U && style_id <= BLOOM_MAX_STYLES) ? style_id : 1U;
    uint32_t pid = (prefab_id >= 1U && prefab_id <= BLOOM_MAX_PREFABS) ? prefab_id : 1U;

    if (out_style) {
        memset(out_style, 0, sizeof(*out_style));
        out_style->style_id = sid;
        out_style->base_rgb24 = mood->dominant_rgb24;
        out_style->accent_rgb24 = mood->accent_rgb24;
        out_style->roughness_q16 = sc_clamp32(8192 + (mood->edge_density_q16 >> 1), 1638, 49152);
        out_style->metallic_q16 = sc_clamp32(mood->specular_hint_q16 * 2, 0, 62000);
        out_style->emissive_gain_q16 = (mood->specular_hint_q16 > 12000) ? 98304 : 65536;
        out_style->sdf_noise_freq_q16 = 131072;
        out_style->sdf_noise_amp_q16 = 2048;
        out_style->msdf_stroke_q16 = 3072;
        out_style->strobe_hz_q16 = 0; /* TRC <= 3.0 Hz strictly safe */
        out_style->matter_slot = matter;
        sc_stamp_mac(out_style, 0x5791E064ULL, &out_style->mac_lo32, &out_style->mac_hi32);
    }

    if (out_prefab) {
        memset(out_prefab, 0, sizeof(*out_prefab));
        out_prefab->prefab_id = pid;
        out_prefab->art_style_id = sid;
        out_prefab->matter_id = matter;
        out_prefab->csg_op_mask = (mood->inferred_prim_mask != 0U)
            ? mood->inferred_prim_mask
            : BLOOM_CSG_MASK_PACK(BLOOM_CSG_PRIM_SPHERE, BLOOM_CSG_PRIM_NONE, BLOOM_CSG_BOOL_UNION, 0, 0);
        out_prefab->bounding_radius_q16 = 98304;
        out_prefab->prim_params_q16[0] = 65536;
        out_prefab->prim_params_q16[1] = 24576;
        out_prefab->prim_params_q16[2] = 49152;
        out_prefab->prim_params_q16[3] = 49152;
        out_prefab->prim_params_q16[4] = 49152;
        out_prefab->prim_params_q16[5] = 16384;
        out_prefab->flags = 1U;
        uint64_t h = bloom_hash56_fnv1a(out_prefab, 0x550794EFULL);
        out_prefab->hash_lo32 = (uint32_t)(h & 0xFFFFFFFFULL);
        out_prefab->hash_hi32 = (uint32_t)(h >> 32ULL);
    }
}
