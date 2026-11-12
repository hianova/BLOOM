#include <metal_stdlib>
using namespace metal;

/*
  bloom_core.metal — Apple Silicon Zero-Copy Unified Memory Compute Library
  =========================================================================
  Reusable headless Metal Shading Language (MSL) compute kernels operating
  directly on canonical `64-byte` (`16 x uint`) descriptors & raw `.flow`
  Manifold Atlas buffers (`MTLStorageModeShared` zero-copy Unified Memory).

  Portability & Safety Guarantees:
    • Zero C preprocessor conditional macro shims or external header files.
    • Pure 32-bit integer arithmetic (`8x8` Wave64 tile masks use `uint2` pairs).
    • `0-ULP` bit-exact integer/fixed-point parity with `Bloom.lean` & `bloom_core.wgsl`,
      plus idiomatic hardware `float` (`f32`) SDF/optics helpers for 120Hz pipelines.
*/

struct BloomPacket64 {
    uint u0;  uint u1;  uint u2;  uint u3;
    uint u4;  uint u5;  uint u6;  uint u7;
    uint u8;  uint u9;  uint u10; uint u11;
    uint u12; uint u13; uint u14; uint u15;
};

static_assert(sizeof(BloomPacket64) == 64, "BloomPacket64 ABI must be strictly 64 bytes");

// ============================================================================
// 1. Hardware FP32 Reusable SDF & Constitutive Matter Optics Helpers
// ============================================================================

inline float bloom_poly_smin_f32(float a, float b, float k) {
    if (k <= 0.0f) { return min(a, b); }
    float h = max(k - abs(a - b), 0.0f) / k;
    return min(a, b) - h * h * k * 0.25f;
}

inline float bloom_gladstone_dale_f32(float k_gd, float rho) {
    return 1.0f + k_gd * max(rho, 0.0f);
}

inline float bloom_schlick_fresnel_f32(float cos_theta, float f0) {
    float u = clamp(1.0f - cos_theta, 0.0f, 1.0f);
    float u2 = u * u;
    return f0 + (1.0f - f0) * (u2 * u2 * u);
}

inline float3 bloom_beer_lambert_f32(float3 radiance, float3 sigma_a, float dist) {
    return radiance * exp(-max(sigma_a, float3(0.0f)) * max(dist, 0.0f));
}

inline float3 bloom_aces_tonemap_f32(float3 x) {
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

// ============================================================================
// 2. Deterministic 32-Bit Fixed-Point Primitives (`0-ULP` CPU/Metal/WGSL)
// ============================================================================

inline int u32_to_i32(uint u) {
    return as_type<int>(u);
}

inline uint abs_i32_to_u32(int x) {
    uint ux = as_type<uint>(x);
    return (x < 0) ? (0u - ux) : ux;
}

inline int sdiv_i32(int num, uint den) {
    if (den == 0u) { return 0; }
    uint mag = abs_i32_to_u32(num) / den;
    return (num < 0) ? -int(mag) : int(mag);
}

inline uint q16_mul_u32(uint a, uint b) {
    uint a_hi = a >> 16u;
    uint a_lo = a & 0xFFFFu;
    uint b_hi = b >> 16u;
    uint b_lo = b & 0xFFFFu;
    return ((a_hi * b_hi) << 16u) + a_hi * b_lo + a_lo * b_hi + ((a_lo * b_lo) >> 16u);
}

inline int q16_mul_i32(int a, int b) {
    uint mag = q16_mul_u32(abs_i32_to_u32(a), abs_i32_to_u32(b));
    bool neg = (a < 0) != (b < 0);
    return neg ? -int(mag) : int(mag);
}

inline uint isqrt_u32(uint n) {
    uint lo = 0u;
    uint hi = 65535u;
    for (uint i = 0u; i < 16u; ++i) {
        uint mid = (lo + hi + 1u) >> 1u;
        if (mid * mid <= n) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    return lo;
}

inline uint length3_q16(int x, int y, int z) {
    uint ax = min(abs_i32_to_u32(x) >> 6u, 24000u);
    uint ay = min(abs_i32_to_u32(y) >> 6u, 24000u);
    uint az = min(abs_i32_to_u32(z) >> 6u, 24000u);
    return isqrt_u32(ax * ax + ay * ay + az * az) << 6u;
}

inline uint pack_rgba8(uint r, uint g, uint b, uint a) {
    return min(r, 255u) | (min(g, 255u) << 8u) | (min(b, 255u) << 16u) | (min(a, 255u) << 24u);
}

inline uint3 eval_sky_rgb(uint py, uint height, uint bg_top, uint bg_bot) {
    uint denom = (height > 1u) ? (height - 1u) : 1u;
    uint t = min((py * 1000u) / denom, 1000u);
    uint inv_t = 1000u - t;
    uint tr = (bg_top >> 16u) & 0xFFu;
    uint tg = (bg_top >> 8u)  & 0xFFu;
    uint tb = bg_top & 0xFFu;
    uint br = (bg_bot >> 16u) & 0xFFu;
    uint bg = (bg_bot >> 8u)  & 0xFFu;
    uint bb = bg_bot & 0xFFu;
    return uint3(
        (tr * inv_t + br * t) / 1000u,
        (tg * inv_t + bg * t) / 1000u,
        (tb * inv_t + bb * t) / 1000u
    );
}

struct SceneSample {
    int  dist;
    uint ior_q16;
    uint sigma_a;
    uint f0;
    uint flags;
    uint r;
    uint g;
    uint b;
};

inline int eval_prim_sdf_q16(BloomPacket64 p, int px, int py, int pz) {
    int lx = px - u32_to_i32(p.u4);
    int ly = py - u32_to_i32(p.u5);
    int lz = pz - u32_to_i32(p.u6);
    int r0 = int(p.u7);
    int r1 = int(p.u8);
    int r2 = int(p.u9);
    if (p.u0 == 0u) {
        return int(length3_q16(lx, ly, lz)) - r0;
    } else if (p.u0 == 1u) {
        int qx = int(abs_i32_to_u32(lx)) - r0;
        int qy = int(abs_i32_to_u32(ly)) - r1;
        int qz = int(abs_i32_to_u32(lz)) - r2;
        int ox = max(qx, 0);
        int oy = max(qy, 0);
        int oz = max(qz, 0);
        int out_d = int(length3_q16(ox, oy, oz));
        int in_d  = min(max(qx, max(qy, qz)), 0);
        return out_d + in_d;
    } else {
        int cy = clamp(ly, -r1, r1);
        return int(length3_q16(lx, ly - cy, lz)) - r0;
    }
}

inline SceneSample eval_scene(device const BloomPacket64* prims, uint prim_count, int px, int py, int pz) {
    if (prim_count == 0u) {
        return SceneSample{655360, 65536u, 0u, 40u, 0u, 200u, 200u, 200u};
    }
    BloomPacket64 p0 = prims[0];
    SceneSample acc = SceneSample{
        eval_prim_sdf_q16(p0, px, py, pz),
        p0.u10, p0.u11, p0.u12, p0.u3, p0.u13, p0.u14, p0.u15
    };
    for (uint idx = 1u; idx < prim_count; ++idx) {
        BloomPacket64 p = prims[idx];
        int  d_raw = eval_prim_sdf_q16(p, px, py, pz);
        uint op    = p.u1;
        uint k_u   = p.u2;
        int  k_i   = int(k_u);
        if (op == 0u) {
            if (k_u == 0u) {
                if (d_raw < acc.dist) {
                    acc = SceneSample{d_raw, p.u10, p.u11, p.u12, p.u3, p.u13, p.u14, p.u15};
                }
            } else {
                int  diff = int(abs_i32_to_u32(acc.dist - d_raw));
                uint h_u  = (k_i > diff) ? uint(k_i - diff) : 0u;
                uint h4   = h_u >> 4u;
                uint k8   = max(k_u >> 8u, 1u);
                int  corr = int((h4 * h4) / (4u * k8));
                int  d_new = min(acc.dist, d_raw) - corr;
                uint blend = 0u;
                if (d_raw + k_i <= acc.dist) {
                    blend = 1000u;
                } else if (acc.dist + k_i <= d_raw) {
                    blend = 0u;
                } else {
                    blend = (uint(k_i + acc.dist - d_raw) * 500u) / max(k_u, 1u);
                }
                uint inv_b = 1000u - blend;
                acc.dist    = d_new;
                acc.ior_q16 = (acc.ior_q16 * inv_b + p.u10 * blend) / 1000u;
                acc.sigma_a = (acc.sigma_a * inv_b + p.u11 * blend) / 1000u;
                acc.f0      = (acc.f0      * inv_b + p.u12 * blend) / 1000u;
                acc.flags   = (blend >= 500u) ? p.u3 : acc.flags;
                acc.r       = (acc.r       * inv_b + p.u13 * blend) / 1000u;
                acc.g       = (acc.g       * inv_b + p.u14 * blend) / 1000u;
                acc.b       = (acc.b       * inv_b + p.u15 * blend) / 1000u;
            }
        } else if (op == 1u) {
            int neg_d = -d_raw;
            if (neg_d > acc.dist) {
                acc.dist = neg_d;
            }
        } else {
            if (d_raw > acc.dist) {
                acc = SceneSample{d_raw, p.u10, p.u11, p.u12, p.u3, p.u13, p.u14, p.u15};
            }
        }
    }
    return acc;
}

inline uint digit_7seg_mask(uint d) {
    switch (d) {
        case 0u: return 0x3Fu;
        case 1u: return 0x06u;
        case 2u: return 0x5Bu;
        case 3u: return 0x4Fu;
        case 4u: return 0x66u;
        case 5u: return 0x6Du;
        case 6u: return 0x7Du;
        case 7u: return 0x07u;
        case 8u: return 0x7Fu;
        case 9u: return 0x6Fu;
        default: return 0x00u;
    }
}

inline bool eval_7seg_digit_pixel(uint d, uint lx, uint ly) {
    if (lx >= 6u || ly >= 9u) { return false; }
    uint m = digit_7seg_mask(d);
    bool seg_a = ((m & 0x01u) != 0u) && (ly == 0u) && (lx >= 1u) && (lx <= 4u);
    bool seg_b = ((m & 0x02u) != 0u) && (lx == 5u) && (ly >= 1u) && (ly <= 3u);
    bool seg_c = ((m & 0x04u) != 0u) && (lx == 5u) && (ly >= 5u) && (ly <= 7u);
    bool seg_d = ((m & 0x08u) != 0u) && (ly == 8u) && (lx >= 1u) && (lx <= 4u);
    bool seg_e = ((m & 0x10u) != 0u) && (lx == 0u) && (ly >= 5u) && (ly <= 7u);
    bool seg_f = ((m & 0x20u) != 0u) && (lx == 0u) && (ly >= 1u) && (ly <= 3u);
    bool seg_g = ((m & 0x40u) != 0u) && (ly == 4u) && (lx >= 1u) && (lx <= 4u);
    return seg_a || seg_b || seg_c || seg_d || seg_e || seg_f || seg_g;
}

inline bool eval_readout_pixel(uint tenths_total, int rx, int ry) {
    if (rx < 0 || ry < 0 || rx >= 23 || ry >= 9) { return false; }
    uint ux = uint(rx);
    uint uy = uint(ry);
    uint d1 = (tenths_total / 100u) % 10u;
    uint d0 = (tenths_total / 10u)  % 10u;
    uint f0 = tenths_total % 10u;
    if (ux < 6u) { return eval_7seg_digit_pixel(d1, ux, uy); }
    if (ux >= 7u && ux < 13u) { return eval_7seg_digit_pixel(d0, ux - 7u, uy); }
    if (ux == 14u && (uy == 7u || uy == 8u)) { return true; }
    if (ux >= 16u && ux < 22u) { return eval_7seg_digit_pixel(f0, ux - 16u, uy); }
    return false;
}

// ============================================================================
// 3. Three Zero-Copy `64-Byte` Compute Kernels
// ============================================================================

kernel void bloom_wave64_tile_cull(
    device const BloomPacket64* header    [[buffer(0)]],
    device const BloomPacket64* widgets   [[buffer(1)]],
    device uint*                tile_mask [[buffer(2)]],
    uint2                       tid       [[thread_position_in_grid]])
{
    BloomPacket64 hdr = header[0];
    uint width     = hdr.u0;
    uint height    = hdr.u1;
    uint tile_cols = hdr.u2;
    uint tile_rows = hdr.u3;
    uint w_count   = hdr.u5;
    if (tid.x >= tile_cols || tid.y >= tile_rows) { return; }

    uint lo32 = 0u;
    uint hi32 = 0u;
    for (uint ly = 0u; ly < 8u; ++ly) {
        for (uint lx = 0u; lx < 8u; ++lx) {
            uint px = tid.x * 8u + lx;
            uint py = tid.y * 8u + ly;
            if (px < width && py < height) {
                bool hit = false;
                for (uint wi = 0u; wi < w_count; ++wi) {
                    BloomPacket64 w = widgets[wi];
                    if (px >= w.u0 && px < w.u2 && py >= w.u1 && py < w.u3) {
                        hit = true;
                    }
                }
                if (hit) {
                    uint bit = ly * 8u + lx;
                    if (bit < 32u) {
                        lo32 |= (1u << bit);
                    } else {
                        hi32 |= (1u << (bit - 32u));
                    }
                }
            }
        }
    }
    uint base = (tid.y * tile_cols + tid.x) * 2u;
    tile_mask[base]      = lo32;
    tile_mask[base + 1u] = hi32;
}

kernel void bloom_sdf_matter_march(
    device const BloomPacket64* header       [[buffer(0)]],
    device const BloomPacket64* prims        [[buffer(1)]],
    device uint*                scene_pixels [[buffer(2)]],
    uint2                       gid          [[thread_position_in_grid]])
{
    BloomPacket64 hdr = header[0];
    uint width      = hdr.u0;
    uint height     = hdr.u1;
    if (gid.x >= width || gid.y >= height) { return; }

    uint h_u32       = max(height, 1u);
    uint prim_count  = hdr.u4;
    int  cam_z       = u32_to_i32(hdr.u6);
    int  focal_len   = u32_to_i32(hdr.u7);
    int  focus_dist  = u32_to_i32(hdr.u8);
    uint aperture    = hdr.u9;
    uint strobe_gain = min(hdr.u10, 1000u);
    uint bg_top      = hdr.u11;
    uint bg_bot      = hdr.u12;
    int  sun_x       = u32_to_i32(hdr.u13);
    int  sun_y       = u32_to_i32(hdr.u14);
    int  sun_z       = u32_to_i32(hdr.u15);

    int sx_num = (2 * int(gid.x) + 1 - int(width))  * 65536;
    int sy_num = (int(height) - (2 * int(gid.y) + 1)) * 65536;
    int uvx = sdiv_i32(sx_num, h_u32);
    int uvy = sdiv_i32(sy_num, h_u32);
    uint r_len10 = max(length3_q16(uvx, uvy, focal_len) >> 6u, 1u);
    int rd_x = sdiv_i32(uvx * 1024, r_len10);
    int rd_y = sdiv_i32(uvy * 1024, r_len10);
    int rd_z = sdiv_i32(focal_len * 1024, r_len10);

    uint3 sky = eval_sky_rgb(gid.y, h_u32, bg_top, bg_bot);
    int  t_q16 = 16384;
    bool hit   = false;
    SceneSample hit_s = SceneSample{655360, 65536u, 0u, 40u, 0u, sky.x, sky.y, sky.z};

    for (uint step = 0u; step < 24u; ++step) {
        if (!hit && t_q16 <= 655360) {
            int pos_x = q16_mul_i32(t_q16, rd_x);
            int pos_y = q16_mul_i32(t_q16, rd_y);
            int pos_z = cam_z + q16_mul_i32(t_q16, rd_z);
            SceneSample s = eval_scene(prims, prim_count, pos_x, pos_y, pos_z);
            if (s.dist <= 512) {
                hit   = true;
                hit_s = s;
            } else {
                t_q16 += s.dist;
            }
        }
    }

    uint3 raw_rgb = sky;
    if (hit) {
        int pos_x = q16_mul_i32(t_q16, rd_x);
        int pos_y = q16_mul_i32(t_q16, rd_y);
        int pos_z = cam_z + q16_mul_i32(t_q16, rd_z);
        int eps   = 1024;
        int gx = eval_scene(prims, prim_count, pos_x + eps, pos_y, pos_z).dist -
                 eval_scene(prims, prim_count, pos_x - eps, pos_y, pos_z).dist;
        int gy = eval_scene(prims, prim_count, pos_x, pos_y + eps, pos_z).dist -
                 eval_scene(prims, prim_count, pos_x, pos_y - eps, pos_z).dist;
        int gz = eval_scene(prims, prim_count, pos_x, pos_y, pos_z + eps).dist -
                 eval_scene(prims, prim_count, pos_x, pos_y, pos_z - eps).dist;
        uint n_len10 = max(length3_q16(gx * 8, gy * 8, gz * 8) >> 6u, 1u);
        int nx = sdiv_i32(gx * 8192, n_len10);
        int ny = sdiv_i32(gy * 8192, n_len10);
        int nz = sdiv_i32(gz * 8192, n_len10);

        int  ndotl_raw = max(q16_mul_i32(nx, sun_x) + q16_mul_i32(ny, sun_y) + q16_mul_i32(nz, sun_z), 0);
        uint diff_perm = min((uint(ndotl_raw) * 1000u) >> 16u, 1000u);
        uint lit_perm  = 180u + (diff_perm * 820u) / 1000u;

        int  ndotv_raw = clamp(q16_mul_i32(nx, -rd_x) + q16_mul_i32(ny, -rd_y) + q16_mul_i32(nz, -rd_z), 0, 65536);
        uint omc       = 1000u - min((uint(ndotv_raw) * 1000u) >> 16u, 1000u);
        uint p2        = (omc * omc) / 1000u;
        uint p4        = (p2 * p2) / 1000u;
        uint p5        = (p4 * omc) / 1000u;
        uint fresnel   = min(hit_s.f0 + ((1000u - hit_s.f0) * p5) / 1000u, 1000u);

        uint3 surf;
        if ((hit_s.flags & 1u) != 0u) {
            int in_x = pos_x - sdiv_i32(nx, 4u);
            int in_y = pos_y - sdiv_i32(ny, 4u);
            int in_z = pos_z - sdiv_i32(nz, 4u);
            int d_in = max(-eval_scene(prims, prim_count, in_x, in_y, in_z).dist, 0);
            uint path_perm  = 250u + min((uint(d_in) * 1000u) >> 16u, 750u);
            uint atten      = min((hit_s.sigma_a * path_perm) / 1000u, 850u);
            uint trans_perm = 1000u - atten;
            int  ior_shift  = sdiv_i32(int(hit_s.ior_q16) - 65536, 4096u);
            int  refr_py    = clamp(int(gid.y) + sdiv_i32(ny * ior_shift, 8192u), 0, int(height) - 1);
            uint3 r_sky     = eval_sky_rgb(uint(refr_py), h_u32, bg_top, bg_bot);
            uint body_r = (min((r_sky.x * hit_s.r) / 160u, 255u) * trans_perm) / 1000u;
            uint body_g = (min((r_sky.y * hit_s.g) / 160u, 255u) * trans_perm) / 1000u;
            uint body_b = (min((r_sky.z * hit_s.b) / 160u, 255u) * trans_perm) / 1000u;
            uint inv_f  = 1000u - fresnel;
            surf = uint3(
                (body_r * inv_f + sky.x * fresnel) / 1000u,
                (body_g * inv_f + sky.y * fresnel) / 1000u,
                (body_b * inv_f + sky.z * fresnel) / 1000u
            );
        } else {
            uint base_r = (hit_s.r * lit_perm) / 1000u;
            uint base_g = (hit_s.g * lit_perm) / 1000u;
            uint base_b = (hit_s.b * lit_perm) / 1000u;
            uint inv_f  = 1000u - fresnel;
            surf = uint3(
                (base_r * inv_f + sky.x * fresnel) / 1000u,
                (base_g * inv_f + sky.y * fresnel) / 1000u,
                (base_b * inv_f + sky.z * fresnel) / 1000u
            );
        }

        uint depth_diff = abs_i32_to_u32(pos_z - focus_dist);
        uint coc_perm   = min((q16_mul_u32(depth_diff, aperture) * 1000u) >> 16u, 650u);
        uint inv_coc    = 1000u - coc_perm;
        raw_rgb = uint3(
            (surf.x * inv_coc + sky.x * coc_perm) / 1000u,
            (surf.y * inv_coc + sky.y * coc_perm) / 1000u,
            (surf.z * inv_coc + sky.z * coc_perm) / 1000u
        );
    }

    uint gain_perm = 700u + (strobe_gain * 300u) / 1000u;
    uint fin_r = (raw_rgb.x * gain_perm) / 1000u;
    uint fin_g = (raw_rgb.y * gain_perm) / 1000u;
    uint fin_b = (raw_rgb.z * gain_perm) / 1000u;
    scene_pixels[gid.y * width + gid.x] = pack_rgba8(fin_r, fin_g, fin_b, 255u);
}

kernel void bloom_bento_glass_composite(
    device const BloomPacket64* header       [[buffer(0)]],
    device const BloomPacket64* widgets      [[buffer(1)]],
    device const BloomPacket64* flow_cells   [[buffer(2)]],
    device const uint*          tile_mask    [[buffer(3)]],
    device const uint*          scene_pixels [[buffer(4)]],
    device uint*                final_pixels [[buffer(5)]],
    uint2                       gid          [[thread_position_in_grid]])
{
    BloomPacket64 hdr = header[0];
    uint width     = hdr.u0;
    uint height    = hdr.u1;
    uint tile_cols = hdr.u2;
    uint w_count   = hdr.u5;
    if (gid.x >= width || gid.y >= height) { return; }

    uint idx     = gid.y * width + gid.x;
    uint bg_rgba = scene_pixels[idx];
    uint tx      = gid.x >> 3u;
    uint ty      = gid.y >> 3u;
    uint bit     = (gid.y & 7u) * 8u + (gid.x & 7u);
    uint m_base  = (ty * tile_cols + tx) * 2u;
    uint m_word  = (bit < 32u) ? tile_mask[m_base] : tile_mask[m_base + 1u];
    uint b_shift = (bit < 32u) ? bit : (bit - 32u);
    if (((m_word >> b_shift) & 1u) == 0u) {
        final_pixels[idx] = bg_rgba;
        return;
    }

    bool found = false;
    BloomPacket64 w;
    for (uint wi = 0u; wi < w_count; ++wi) {
        if (!found) {
            BloomPacket64 cand = widgets[wi];
            if (gid.x >= cand.u0 && gid.x < cand.u2 && gid.y >= cand.u1 && gid.y < cand.u3) {
                w = cand;
                found = true;
            }
        }
    }
    if (!found) {
        final_pixels[idx] = bg_rgba;
        return;
    }

    int x0_i = int(w.u0);
    int y0_i = int(w.u1);
    int x1_i = int(w.u2);
    int y1_i = int(w.u3);
    int cx2  = x0_i + x1_i;
    int cy2  = y0_i + y1_i;
    int px2  = 2 * int(gid.x) + 1;
    int py2  = 2 * int(gid.y) + 1;
    int hx2  = x1_i - x0_i;
    int hy2  = y1_i - y0_i;
    int r2   = 2 * int(w.u8);
    int qx2  = int(abs_i32_to_u32(px2 - cx2)) - (hx2 - r2);
    int qy2  = int(abs_i32_to_u32(py2 - cy2)) - (hy2 - r2);
    uint ox2 = uint(max(qx2, 0));
    uint oy2 = uint(max(qy2, 0));
    int out2 = int(isqrt_u32(ox2 * ox2 + oy2 * oy2));
    int in2  = min(max(qx2, qy2), 0);
    int sdf2 = out2 + in2 - r2;
    if (sdf2 > 0) {
        final_pixels[idx] = bg_rgba;
        return;
    }

    // Zero-copy inspection of raw 64-byte `.flow` State + Chart cells in GPU Unified Memory!
    uint base_cell = w.u4;
    BloomPacket64 st = flow_cells[base_cell];
    bool seal_ok   = (st.u14 | st.u15) != 0u;
    uint sieve     = st.u12;
    bool gate_ok   = seal_ok && ((sieve & 1u) != 0u);
    int  x_q16     = u32_to_i32(st.u6);
    uint slot      = (st.u10 >> 16u) & 0xFFFFu;
    uint safe_slot = slot & w.u11;
    BloomPacket64 ch = flow_cells[base_cell + 1u + safe_slot];
    int  b_enter   = u32_to_i32(ch.u6);
    int  b_exit    = u32_to_i32(ch.u7);
    int  span      = max(b_enter - b_exit, 65536);
    int  warn_at   = b_enter - sdiv_i32(span, 5u);

    uint glass_mode = 0u;
    if (!gate_ok || slot >= w.u5) {
        glass_mode = 2u;
    } else if (slot > 0u || (b_enter < 2000000000 && x_q16 >= warn_at)) {
        glass_mode = 1u;
    }

    uint3 tint = (glass_mode == 2u) ? uint3(235u, 58u, 72u)
               : (glass_mode == 1u) ? uint3(245u, 178u, 42u)
                                    : uint3(44u, 212u, 148u);

    int ref_off = int(w.u10);
    uint sx_l = uint(clamp(int(gid.x) - ref_off, 0, int(width)  - 1));
    uint sx_r = uint(clamp(int(gid.x) + ref_off, 0, int(width)  - 1));
    uint sy_u = uint(clamp(int(gid.y) - ref_off, 0, int(height) - 1));
    uint sy_d = uint(clamp(int(gid.y) + ref_off, 0, int(height) - 1));
    uint p_l = scene_pixels[gid.y * width + sx_l];
    uint p_r = scene_pixels[gid.y * width + sx_r];
    uint p_u = scene_pixels[sy_u  * width + gid.x];
    uint p_d = scene_pixels[sy_d  * width + gid.x];
    uint blur_r = ((p_l & 0xFFu) + (p_r & 0xFFu) + (p_u & 0xFFu) + (p_d & 0xFFu)) / 4u;
    uint blur_g = (((p_l >> 8u) & 0xFFu) + ((p_r >> 8u) & 0xFFu) + ((p_u >> 8u) & 0xFFu) + ((p_d >> 8u) & 0xFFu)) / 4u;
    uint blur_b = (((p_l >> 16u) & 0xFFu) + ((p_r >> 16u) & 0xFFu) + ((p_u >> 16u) & 0xFFu) + ((p_d >> 16u) & 0xFFu)) / 4u;

    uint pix_r = (blur_r * 520u + 18u * 300u + tint.x * 180u) / 1000u;
    uint pix_g = (blur_g * 520u + 24u * 300u + tint.y * 180u) / 1000u;
    uint pix_b = (blur_b * 520u + 34u * 300u + tint.z * 180u) / 1000u;

    int  min_vis   = u32_to_i32(w.u6);
    int  max_vis   = max(u32_to_i32(w.u7), min_vis + 65536);
    int  clamped_x = clamp(x_q16, min_vis, max_vis);
    uint fill_perm = uint(sdiv_i32((clamped_x - min_vis) * 1000, uint(max_vis - min_vis)));
    int  bar_x0    = x0_i + 5;
    int  bar_x1    = max(x1_i - 5, bar_x0 + 1);
    int  bar_y0    = y1_i - 7;
    int  bar_y1    = y1_i - 4;
    if (int(gid.x) >= bar_x0 && int(gid.x) < bar_x1 &&
        int(gid.y) >= bar_y0 && int(gid.y) < bar_y1) {
        uint bar_w = uint(bar_x1 - bar_x0);
        uint rel_x = uint(int(gid.x) - bar_x0);
        if (rel_x * 1000u < fill_perm * bar_w) {
            pix_r = tint.x;
            pix_g = tint.y;
            pix_b = tint.z;
        } else {
            pix_r /= 2u;
            pix_g /= 2u;
            pix_b /= 2u;
        }
    }

    uint tenths   = (abs_i32_to_u32(x_q16) * 10u + 32768u) >> 16u;
    int  glyph_x0 = sdiv_i32(x0_i + x1_i, 2u) - 11;
    int  glyph_y0 = max(y0_i + 3, sdiv_i32(y0_i + bar_y0 - 9, 2u));
    if (eval_readout_pixel(tenths, int(gid.x) - glyph_x0, int(gid.y) - glyph_y0)) {
        pix_r = 242u;
        pix_g = 248u;
        pix_b = 255u;
    }

    int border2 = 2 * int(w.u9);
    if (sdf2 >= -border2) {
        pix_r = (pix_r * 300u + tint.x * 700u) / 1000u;
        pix_g = (pix_g * 300u + tint.y * 700u) / 1000u;
        pix_b = (pix_b * 300u + tint.z * 700u) / 1000u;
    }

    final_pixels[idx] = pack_rgba8(pix_r, pix_g, pix_b, 255u);
}
