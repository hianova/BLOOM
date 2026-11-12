// bloom_core.wgsl — Cross-Platform WebGPU Compute Library (Vulkan / DX12 / Metal / Web)
// =====================================================================================
// Pure W3C WebGPU Shading Language (WGSL 1.0) twin of `bloom_core.metal`.
// Consumes the exact same `64-byte` (`16 x u32`) `BloomPacket64` descriptors and raw
// `.flow` Manifold Atlas cells (zero C preprocessor directives, pure 32-bit integer
// arithmetic, `0-ULP` bit-exact parity with `Bloom.lean` & `bloom_core.metal`).
//
// Single-BindGroup Frame Pipeline (`@group(0)`):
//   1. `bloom_wave64_tile_cull`      (`@workgroup_size(8, 8, 1)`)
//   2. `bloom_sdf_matter_march`      (`@workgroup_size(8, 8, 1)`)
//   3. `bloom_bento_glass_composite` (`@workgroup_size(8, 8, 1)`)

struct BloomPacket64 {
    u0  : u32, u1  : u32, u2  : u32, u3  : u32,
    u4  : u32, u5  : u32, u6  : u32, u7  : u32,
    u8  : u32, u9  : u32, u10 : u32, u11 : u32,
    u12 : u32, u13 : u32, u14 : u32, u15 : u32,
}

@group(0) @binding(0) var<storage, read>       header       : array<BloomPacket64>;
@group(0) @binding(1) var<storage, read>       prims        : array<BloomPacket64>;
@group(0) @binding(2) var<storage, read>       widgets      : array<BloomPacket64>;
@group(0) @binding(3) var<storage, read>       flow_cells   : array<BloomPacket64>;
@group(0) @binding(4) var<storage, read_write> tile_mask    : array<u32>;
@group(0) @binding(5) var<storage, read_write> scene_pixels : array<u32>;
@group(0) @binding(6) var<storage, read_write> final_pixels : array<u32>;

// ============================================================================
// 1. Hardware FP32 Reusable SDF & Constitutive Matter Optics Helpers
// ============================================================================

fn bloom_poly_smin_f32(a: f32, b: f32, k: f32) -> f32 {
    if (k <= 0.0) { return min(a, b); }
    let h: f32 = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}

fn bloom_gladstone_dale_f32(k_gd: f32, rho: f32) -> f32 {
    return 1.0 + k_gd * max(rho, 0.0);
}

fn bloom_schlick_fresnel_f32(cos_theta: f32, f0: f32) -> f32 {
    let u: f32 = clamp(1.0 - cos_theta, 0.0, 1.0);
    let u2: f32 = u * u;
    return f0 + (1.0 - f0) * (u2 * u2 * u);
}

fn bloom_beer_lambert_f32(radiance: vec3<f32>, sigma_a: vec3<f32>, dist: f32) -> vec3<f32> {
    return radiance * exp(-max(sigma_a, vec3<f32>(0.0)) * max(dist, 0.0));
}

fn bloom_aces_tonemap_f32(x: vec3<f32>) -> vec3<f32> {
    let a: f32 = 2.51;
    let b: f32 = 0.03;
    let c: f32 = 2.43;
    let d: f32 = 0.59;
    let e: f32 = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), vec3<f32>(0.0), vec3<f32>(1.0));
}

// ============================================================================
// 2. Deterministic 32-Bit Fixed-Point Primitives (`0-ULP` CPU/Metal/WGSL)
// ============================================================================

fn u32_to_i32(u: u32) -> i32 {
    return bitcast<i32>(u);
}

fn abs_i32_to_u32(x: i32) -> u32 {
    let ux: u32 = bitcast<u32>(x);
    return select(ux, 0u - ux, x < 0);
}

fn sdiv_i32(num: i32, den: u32) -> i32 {
    if (den == 0u) { return 0; }
    let mag: u32 = abs_i32_to_u32(num) / den;
    return select(i32(mag), -i32(mag), num < 0);
}

fn q16_mul_u32(a: u32, b: u32) -> u32 {
    let a_hi: u32 = a >> 16u;
    let a_lo: u32 = a & 0xFFFFu;
    let b_hi: u32 = b >> 16u;
    let b_lo: u32 = b & 0xFFFFu;
    return ((a_hi * b_hi) << 16u) + a_hi * b_lo + a_lo * b_hi + ((a_lo * b_lo) >> 16u);
}

fn q16_mul_i32(a: i32, b: i32) -> i32 {
    let mag: u32 = q16_mul_u32(abs_i32_to_u32(a), abs_i32_to_u32(b));
    let neg: bool = (a < 0) != (b < 0);
    return select(i32(mag), -i32(mag), neg);
}

fn isqrt_u32(n: u32) -> u32 {
    var lo: u32 = 0u;
    var hi: u32 = 65535u;
    for (var i: u32 = 0u; i < 16u; i = i + 1u) {
        let mid: u32 = (lo + hi + 1u) >> 1u;
        if (mid * mid <= n) {
            lo = mid;
        } else {
            hi = mid - 1u;
        }
    }
    return lo;
}

fn length3_q16(x: i32, y: i32, z: i32) -> u32 {
    let ax: u32 = min(abs_i32_to_u32(x) >> 6u, 24000u);
    let ay: u32 = min(abs_i32_to_u32(y) >> 6u, 24000u);
    let az: u32 = min(abs_i32_to_u32(z) >> 6u, 24000u);
    return isqrt_u32(ax * ax + ay * ay + az * az) << 6u;
}

fn pack_rgba8(r: u32, g: u32, b: u32, a: u32) -> u32 {
    return min(r, 255u) | (min(g, 255u) << 8u) | (min(b, 255u) << 16u) | (min(a, 255u) << 24u);
}

fn eval_sky_rgb(py: u32, height: u32, bg_top: u32, bg_bot: u32) -> vec3<u32> {
    let denom: u32 = select(1u, height - 1u, height > 1u);
    let t: u32 = min((py * 1000u) / denom, 1000u);
    let inv_t: u32 = 1000u - t;
    let tr: u32 = (bg_top >> 16u) & 0xFFu;
    let tg: u32 = (bg_top >> 8u)  & 0xFFu;
    let tb: u32 = bg_top & 0xFFu;
    let br: u32 = (bg_bot >> 16u) & 0xFFu;
    let bg: u32 = (bg_bot >> 8u)  & 0xFFu;
    let bb: u32 = bg_bot & 0xFFu;
    return vec3<u32>(
        (tr * inv_t + br * t) / 1000u,
        (tg * inv_t + bg * t) / 1000u,
        (tb * inv_t + bb * t) / 1000u
    );
}

struct SceneSample {
    dist    : i32,
    ior_q16 : u32,
    sigma_a : u32,
    f0      : u32,
    flags   : u32,
    r       : u32,
    g       : u32,
    b       : u32,
}

fn eval_prim_sdf_q16(p: BloomPacket64, px: i32, py: i32, pz: i32) -> i32 {
    let lx: i32 = px - u32_to_i32(p.u4);
    let ly: i32 = py - u32_to_i32(p.u5);
    let lz: i32 = pz - u32_to_i32(p.u6);
    let r0: i32 = i32(p.u7);
    let r1: i32 = i32(p.u8);
    let r2: i32 = i32(p.u9);
    if (p.u0 == 0u) {
        return i32(length3_q16(lx, ly, lz)) - r0;
    } else if (p.u0 == 1u) {
        let qx: i32 = i32(abs_i32_to_u32(lx)) - r0;
        let qy: i32 = i32(abs_i32_to_u32(ly)) - r1;
        let qz: i32 = i32(abs_i32_to_u32(lz)) - r2;
        let ox: i32 = max(qx, 0);
        let oy: i32 = max(qy, 0);
        let oz: i32 = max(qz, 0);
        let out_d: i32 = i32(length3_q16(ox, oy, oz));
        let in_d: i32  = min(max(qx, max(qy, qz)), 0);
        return out_d + in_d;
    } else {
        let cy: i32 = clamp(ly, -r1, r1);
        return i32(length3_q16(lx, ly - cy, lz)) - r0;
    }
}

fn eval_scene(prim_count: u32, px: i32, py: i32, pz: i32) -> SceneSample {
    if (prim_count == 0u) {
        return SceneSample(655360, 65536u, 0u, 40u, 0u, 200u, 200u, 200u);
    }
    let p0: BloomPacket64 = prims[0u];
    var acc: SceneSample = SceneSample(
        eval_prim_sdf_q16(p0, px, py, pz),
        p0.u10, p0.u11, p0.u12, p0.u3, p0.u13, p0.u14, p0.u15
    );
    for (var idx: u32 = 1u; idx < prim_count; idx = idx + 1u) {
        let p: BloomPacket64 = prims[idx];
        let d_raw: i32 = eval_prim_sdf_q16(p, px, py, pz);
        let op: u32    = p.u1;
        let k_u: u32   = p.u2;
        let k_i: i32   = i32(k_u);
        if (op == 0u) {
            if (k_u == 0u) {
                if (d_raw < acc.dist) {
                    acc = SceneSample(d_raw, p.u10, p.u11, p.u12, p.u3, p.u13, p.u14, p.u15);
                }
            } else {
                let diff: i32 = i32(abs_i32_to_u32(acc.dist - d_raw));
                let h_u: u32  = select(0u, u32(k_i - diff), k_i > diff);
                let h4: u32   = h_u >> 4u;
                let k8: u32   = max(k_u >> 8u, 1u);
                let corr: i32 = i32((h4 * h4) / (4u * k8));
                let d_new: i32 = min(acc.dist, d_raw) - corr;
                var blend: u32 = 0u;
                if (d_raw + k_i <= acc.dist) {
                    blend = 1000u;
                } else if (acc.dist + k_i <= d_raw) {
                    blend = 0u;
                } else {
                    blend = (u32(k_i + acc.dist - d_raw) * 500u) / max(k_u, 1u);
                }
                let inv_b: u32 = 1000u - blend;
                acc.dist    = d_new;
                acc.ior_q16 = (acc.ior_q16 * inv_b + p.u10 * blend) / 1000u;
                acc.sigma_a = (acc.sigma_a * inv_b + p.u11 * blend) / 1000u;
                acc.f0      = (acc.f0      * inv_b + p.u12 * blend) / 1000u;
                acc.flags   = select(acc.flags, p.u3, blend >= 500u);
                acc.r       = (acc.r       * inv_b + p.u13 * blend) / 1000u;
                acc.g       = (acc.g       * inv_b + p.u14 * blend) / 1000u;
                acc.b       = (acc.b       * inv_b + p.u15 * blend) / 1000u;
            }
        } else if (op == 1u) {
            let neg_d: i32 = -d_raw;
            if (neg_d > acc.dist) {
                acc.dist = neg_d;
            }
        } else {
            if (d_raw > acc.dist) {
                acc = SceneSample(d_raw, p.u10, p.u11, p.u12, p.u3, p.u13, p.u14, p.u15);
            }
        }
    }
    return acc;
}

fn digit_7seg_mask(d: u32) -> u32 {
    switch (d) {
        case 0u: { return 0x3Fu; }
        case 1u: { return 0x06u; }
        case 2u: { return 0x5Bu; }
        case 3u: { return 0x4Fu; }
        case 4u: { return 0x66u; }
        case 5u: { return 0x6Du; }
        case 6u: { return 0x7Du; }
        case 7u: { return 0x07u; }
        case 8u: { return 0x7Fu; }
        case 9u: { return 0x6Fu; }
        default: { return 0x00u; }
    }
}

fn eval_7seg_digit_pixel(d: u32, lx: u32, ly: u32) -> bool {
    if (lx >= 6u || ly >= 9u) { return false; }
    let m: u32 = digit_7seg_mask(d);
    let seg_a: bool = ((m & 0x01u) != 0u) && (ly == 0u) && (lx >= 1u) && (lx <= 4u);
    let seg_b: bool = ((m & 0x02u) != 0u) && (lx == 5u) && (ly >= 1u) && (ly <= 3u);
    let seg_c: bool = ((m & 0x04u) != 0u) && (lx == 5u) && (ly >= 5u) && (ly <= 7u);
    let seg_d: bool = ((m & 0x08u) != 0u) && (ly == 8u) && (lx >= 1u) && (lx <= 4u);
    let seg_e: bool = ((m & 0x10u) != 0u) && (lx == 0u) && (ly >= 5u) && (ly <= 7u);
    let seg_f: bool = ((m & 0x20u) != 0u) && (lx == 0u) && (ly >= 1u) && (ly <= 3u);
    let seg_g: bool = ((m & 0x40u) != 0u) && (ly == 4u) && (lx >= 1u) && (lx <= 4u);
    return seg_a || seg_b || seg_c || seg_d || seg_e || seg_f || seg_g;
}

fn eval_readout_pixel(tenths_total: u32, rx: i32, ry: i32) -> bool {
    if (rx < 0 || ry < 0 || rx >= 23 || ry >= 9) { return false; }
    let ux: u32 = u32(rx);
    let uy: u32 = u32(ry);
    let d1: u32 = (tenths_total / 100u) % 10u;
    let d0: u32 = (tenths_total / 10u)  % 10u;
    let f0: u32 = tenths_total % 10u;
    if (ux < 6u) { return eval_7seg_digit_pixel(d1, ux, uy); }
    if (ux >= 7u && ux < 13u) { return eval_7seg_digit_pixel(d0, ux - 7u, uy); }
    if (ux == 14u && (uy == 7u || uy == 8u)) { return true; }
    if (ux >= 16u && ux < 22u) { return eval_7seg_digit_pixel(f0, ux - 16u, uy); }
    return false;
}

// ============================================================================
// 3. Three Zero-Copy `64-Byte` Compute Kernels
// ============================================================================

@compute @workgroup_size(8, 8, 1)
fn bloom_wave64_tile_cull(@builtin(global_invocation_id) tid: vec3<u32>) {
    let hdr: BloomPacket64 = header[0u];
    let width: u32     = hdr.u0;
    let height: u32    = hdr.u1;
    let tile_cols: u32 = hdr.u2;
    let tile_rows: u32 = hdr.u3;
    let w_count: u32   = hdr.u5;
    if (tid.x >= tile_cols || tid.y >= tile_rows) { return; }

    var lo32: u32 = 0u;
    var hi32: u32 = 0u;
    for (var ly: u32 = 0u; ly < 8u; ly = ly + 1u) {
        for (var lx: u32 = 0u; lx < 8u; lx = lx + 1u) {
            let px: u32 = tid.x * 8u + lx;
            let py: u32 = tid.y * 8u + ly;
            if (px < width && py < height) {
                var hit: bool = false;
                for (var wi: u32 = 0u; wi < w_count; wi = wi + 1u) {
                    let w: BloomPacket64 = widgets[wi];
                    if (px >= w.u0 && px < w.u2 && py >= w.u1 && py < w.u3) {
                        hit = true;
                    }
                }
                if (hit) {
                    let bit: u32 = ly * 8u + lx;
                    if (bit < 32u) {
                        lo32 = lo32 | (1u << bit);
                    } else {
                        hi32 = hi32 | (1u << (bit - 32u));
                    }
                }
            }
        }
    }
    let base: u32 = (tid.y * tile_cols + tid.x) * 2u;
    tile_mask[base]      = lo32;
    tile_mask[base + 1u] = hi32;
}

@compute @workgroup_size(8, 8, 1)
fn bloom_sdf_matter_march(@builtin(global_invocation_id) gid: vec3<u32>) {
    let hdr: BloomPacket64 = header[0u];
    let width: u32  = hdr.u0;
    let height: u32 = hdr.u1;
    if (gid.x >= width || gid.y >= height) { return; }

    let h_u32: u32       = max(height, 1u);
    let prim_count: u32  = hdr.u4;
    let cam_z: i32       = u32_to_i32(hdr.u6);
    let focal_len: i32   = u32_to_i32(hdr.u7);
    let focus_dist: i32  = u32_to_i32(hdr.u8);
    let aperture: u32    = hdr.u9;
    let strobe_gain: u32 = min(hdr.u10, 1000u);
    let bg_top: u32      = hdr.u11;
    let bg_bot: u32      = hdr.u12;
    let sun_x: i32       = u32_to_i32(hdr.u13);
    let sun_y: i32       = u32_to_i32(hdr.u14);
    let sun_z: i32       = u32_to_i32(hdr.u15);

    let sx_num: i32 = (2 * i32(gid.x) + 1 - i32(width))  * 65536;
    let sy_num: i32 = (i32(height) - (2 * i32(gid.y) + 1)) * 65536;
    let uvx: i32 = sdiv_i32(sx_num, h_u32);
    let uvy: i32 = sdiv_i32(sy_num, h_u32);
    let r_len10: u32 = max(length3_q16(uvx, uvy, focal_len) >> 6u, 1u);
    let rd_x: i32 = sdiv_i32(uvx * 1024, r_len10);
    let rd_y: i32 = sdiv_i32(uvy * 1024, r_len10);
    let rd_z: i32 = sdiv_i32(focal_len * 1024, r_len10);

    let sky: vec3<u32> = eval_sky_rgb(gid.y, h_u32, bg_top, bg_bot);
    var t_q16: i32 = 16384;
    var hit: bool  = false;
    var hit_s: SceneSample = SceneSample(655360, 65536u, 0u, 40u, 0u, sky.x, sky.y, sky.z);

    for (var step: u32 = 0u; step < 24u; step = step + 1u) {
        if (!hit && t_q16 <= 655360) {
            let pos_x: i32 = q16_mul_i32(t_q16, rd_x);
            let pos_y: i32 = q16_mul_i32(t_q16, rd_y);
            let pos_z: i32 = cam_z + q16_mul_i32(t_q16, rd_z);
            let s: SceneSample = eval_scene(prim_count, pos_x, pos_y, pos_z);
            if (s.dist <= 512) {
                hit   = true;
                hit_s = s;
            } else {
                t_q16 = t_q16 + s.dist;
            }
        }
    }

    var raw_rgb: vec3<u32> = sky;
    if (hit) {
        let pos_x: i32 = q16_mul_i32(t_q16, rd_x);
        let pos_y: i32 = q16_mul_i32(t_q16, rd_y);
        let pos_z: i32 = cam_z + q16_mul_i32(t_q16, rd_z);
        let eps: i32   = 1024;
        let gx: i32 = eval_scene(prim_count, pos_x + eps, pos_y, pos_z).dist -
                      eval_scene(prim_count, pos_x - eps, pos_y, pos_z).dist;
        let gy: i32 = eval_scene(prim_count, pos_x, pos_y + eps, pos_z).dist -
                      eval_scene(prim_count, pos_x, pos_y - eps, pos_z).dist;
        let gz: i32 = eval_scene(prim_count, pos_x, pos_y, pos_z + eps).dist -
                      eval_scene(prim_count, pos_x, pos_y, pos_z - eps).dist;
        let n_len10: u32 = max(length3_q16(gx * 8, gy * 8, gz * 8) >> 6u, 1u);
        let nx: i32 = sdiv_i32(gx * 8192, n_len10);
        let ny: i32 = sdiv_i32(gy * 8192, n_len10);
        let nz: i32 = sdiv_i32(gz * 8192, n_len10);

        let ndotl_raw: i32 = max(q16_mul_i32(nx, sun_x) + q16_mul_i32(ny, sun_y) + q16_mul_i32(nz, sun_z), 0);
        let diff_perm: u32 = min((u32(ndotl_raw) * 1000u) >> 16u, 1000u);
        let lit_perm: u32  = 180u + (diff_perm * 820u) / 1000u;

        let ndotv_raw: i32 = clamp(q16_mul_i32(nx, -rd_x) + q16_mul_i32(ny, -rd_y) + q16_mul_i32(nz, -rd_z), 0, 65536);
        let omc: u32       = 1000u - min((u32(ndotv_raw) * 1000u) >> 16u, 1000u);
        let p2: u32        = (omc * omc) / 1000u;
        let p4: u32        = (p2 * p2) / 1000u;
        let p5: u32        = (p4 * omc) / 1000u;
        let fresnel: u32   = min(hit_s.f0 + ((1000u - hit_s.f0) * p5) / 1000u, 1000u);

        var surf: vec3<u32> = vec3<u32>(0u);
        if ((hit_s.flags & 1u) != 0u) {
            let in_x: i32 = pos_x - sdiv_i32(nx, 4u);
            let in_y: i32 = pos_y - sdiv_i32(ny, 4u);
            let in_z: i32 = pos_z - sdiv_i32(nz, 4u);
            let d_in: i32 = max(-eval_scene(prim_count, in_x, in_y, in_z).dist, 0);
            let path_perm: u32  = 250u + min((u32(d_in) * 1000u) >> 16u, 750u);
            let atten: u32      = min((hit_s.sigma_a * path_perm) / 1000u, 850u);
            let trans_perm: u32 = 1000u - atten;
            let ior_shift: i32  = sdiv_i32(i32(hit_s.ior_q16) - 65536, 4096u);
            let refr_py: i32    = clamp(i32(gid.y) + sdiv_i32(ny * ior_shift, 8192u), 0, i32(height) - 1);
            let r_sky: vec3<u32> = eval_sky_rgb(u32(refr_py), h_u32, bg_top, bg_bot);
            let body_r: u32 = (min((r_sky.x * hit_s.r) / 160u, 255u) * trans_perm) / 1000u;
            let body_g: u32 = (min((r_sky.y * hit_s.g) / 160u, 255u) * trans_perm) / 1000u;
            let body_b: u32 = (min((r_sky.z * hit_s.b) / 160u, 255u) * trans_perm) / 1000u;
            let inv_f: u32  = 1000u - fresnel;
            surf = vec3<u32>(
                (body_r * inv_f + sky.x * fresnel) / 1000u,
                (body_g * inv_f + sky.y * fresnel) / 1000u,
                (body_b * inv_f + sky.z * fresnel) / 1000u
            );
        } else {
            let base_r: u32 = (hit_s.r * lit_perm) / 1000u;
            let base_g: u32 = (hit_s.g * lit_perm) / 1000u;
            let base_b: u32 = (hit_s.b * lit_perm) / 1000u;
            let inv_f: u32  = 1000u - fresnel;
            surf = vec3<u32>(
                (base_r * inv_f + sky.x * fresnel) / 1000u,
                (base_g * inv_f + sky.y * fresnel) / 1000u,
                (base_b * inv_f + sky.z * fresnel) / 1000u
            );
        }

        let depth_diff: u32 = abs_i32_to_u32(pos_z - focus_dist);
        let coc_perm: u32   = min((q16_mul_u32(depth_diff, aperture) * 1000u) >> 16u, 650u);
        let inv_coc: u32    = 1000u - coc_perm;
        raw_rgb = vec3<u32>(
            (surf.x * inv_coc + sky.x * coc_perm) / 1000u,
            (surf.y * inv_coc + sky.y * coc_perm) / 1000u,
            (surf.z * inv_coc + sky.z * coc_perm) / 1000u
        );
    }

    let gain_perm: u32 = 700u + (strobe_gain * 300u) / 1000u;
    let fin_r: u32 = (raw_rgb.x * gain_perm) / 1000u;
    let fin_g: u32 = (raw_rgb.y * gain_perm) / 1000u;
    let fin_b: u32 = (raw_rgb.z * gain_perm) / 1000u;
    scene_pixels[gid.y * width + gid.x] = pack_rgba8(fin_r, fin_g, fin_b, 255u);
}

@compute @workgroup_size(8, 8, 1)
fn bloom_bento_glass_composite(@builtin(global_invocation_id) gid: vec3<u32>) {
    let hdr: BloomPacket64 = header[0u];
    let width: u32     = hdr.u0;
    let height: u32    = hdr.u1;
    let tile_cols: u32 = hdr.u2;
    let w_count: u32   = hdr.u5;
    if (gid.x >= width || gid.y >= height) { return; }

    let idx: u32     = gid.y * width + gid.x;
    let bg_rgba: u32 = scene_pixels[idx];
    let tx: u32      = gid.x >> 3u;
    let ty: u32      = gid.y >> 3u;
    let bit: u32     = (gid.y & 7u) * 8u + (gid.x & 7u);
    let m_base: u32  = (ty * tile_cols + tx) * 2u;
    let m_word: u32  = select(tile_mask[m_base + 1u], tile_mask[m_base], bit < 32u);
    let b_shift: u32 = select(bit - 32u, bit, bit < 32u);
    if (((m_word >> b_shift) & 1u) == 0u) {
        final_pixels[idx] = bg_rgba;
        return;
    }

    var found: bool = false;
    var w: BloomPacket64 = widgets[0u];
    for (var wi: u32 = 0u; wi < w_count; wi = wi + 1u) {
        if (!found) {
            let cand: BloomPacket64 = widgets[wi];
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

    let x0_i: i32 = i32(w.u0);
    let y0_i: i32 = i32(w.u1);
    let x1_i: i32 = i32(w.u2);
    let y1_i: i32 = i32(w.u3);
    let cx2: i32  = x0_i + x1_i;
    let cy2: i32  = y0_i + y1_i;
    let px2: i32  = 2 * i32(gid.x) + 1;
    let py2: i32  = 2 * i32(gid.y) + 1;
    let hx2: i32  = x1_i - x0_i;
    let hy2: i32  = y1_i - y0_i;
    let r2: i32   = 2 * i32(w.u8);
    let qx2: i32  = i32(abs_i32_to_u32(px2 - cx2)) - (hx2 - r2);
    let qy2: i32  = i32(abs_i32_to_u32(py2 - cy2)) - (hy2 - r2);
    let ox2: u32  = u32(max(qx2, 0));
    let oy2: u32  = u32(max(qy2, 0));
    let out2: i32 = i32(isqrt_u32(ox2 * ox2 + oy2 * oy2));
    let in2: i32  = min(max(qx2, qy2), 0);
    let sdf2: i32 = out2 + in2 - r2;
    if (sdf2 > 0) {
        final_pixels[idx] = bg_rgba;
        return;
    }

    // Zero-copy inspection of raw 64-byte `.flow` State + Chart cells!
    let base_cell: u32    = w.u4;
    let st: BloomPacket64 = flow_cells[base_cell];
    let seal_ok: bool     = (st.u14 | st.u15) != 0u;
    let sieve: u32        = st.u12;
    let gate_ok: bool     = seal_ok && ((sieve & 1u) != 0u);
    let x_q16: i32        = u32_to_i32(st.u6);
    let slot: u32         = (st.u10 >> 16u) & 0xFFFFu;
    let safe_slot: u32    = slot & w.u11;
    let ch: BloomPacket64 = flow_cells[base_cell + 1u + safe_slot];
    let b_enter: i32      = u32_to_i32(ch.u6);
    let b_exit: i32       = u32_to_i32(ch.u7);
    let span: i32         = max(b_enter - b_exit, 65536);
    let warn_at: i32      = b_enter - sdiv_i32(span, 5u);

    var glass_mode: u32 = 0u;
    if (!gate_ok || slot >= w.u5) {
        glass_mode = 2u;
    } else if (slot > 0u || (b_enter < 2000000000 && x_q16 >= warn_at)) {
        glass_mode = 1u;
    }

    var tint: vec3<u32> = vec3<u32>(44u, 212u, 148u);
    if (glass_mode == 2u) {
        tint = vec3<u32>(235u, 58u, 72u);
    } else if (glass_mode == 1u) {
        tint = vec3<u32>(245u, 178u, 42u);
    }

    let ref_off: i32 = i32(w.u10);
    let sx_l: u32 = u32(clamp(i32(gid.x) - ref_off, 0, i32(width)  - 1));
    let sx_r: u32 = u32(clamp(i32(gid.x) + ref_off, 0, i32(width)  - 1));
    let sy_u: u32 = u32(clamp(i32(gid.y) - ref_off, 0, i32(height) - 1));
    let sy_d: u32 = u32(clamp(i32(gid.y) + ref_off, 0, i32(height) - 1));
    let p_l: u32 = scene_pixels[gid.y * width + sx_l];
    let p_r: u32 = scene_pixels[gid.y * width + sx_r];
    let p_u: u32 = scene_pixels[sy_u  * width + gid.x];
    let p_d: u32 = scene_pixels[sy_d  * width + gid.x];
    let blur_r: u32 = ((p_l & 0xFFu) + (p_r & 0xFFu) + (p_u & 0xFFu) + (p_d & 0xFFu)) / 4u;
    let blur_g: u32 = (((p_l >> 8u) & 0xFFu) + ((p_r >> 8u) & 0xFFu) + ((p_u >> 8u) & 0xFFu) + ((p_d >> 8u) & 0xFFu)) / 4u;
    let blur_b: u32 = (((p_l >> 16u) & 0xFFu) + ((p_r >> 16u) & 0xFFu) + ((p_u >> 16u) & 0xFFu) + ((p_d >> 16u) & 0xFFu)) / 4u;

    var pix_r: u32 = (blur_r * 520u + 18u * 300u + tint.x * 180u) / 1000u;
    var pix_g: u32 = (blur_g * 520u + 24u * 300u + tint.y * 180u) / 1000u;
    var pix_b: u32 = (blur_b * 520u + 34u * 300u + tint.z * 180u) / 1000u;

    let min_vis: i32   = u32_to_i32(w.u6);
    let max_vis: i32   = max(u32_to_i32(w.u7), min_vis + 65536);
    let clamped_x: i32 = clamp(x_q16, min_vis, max_vis);
    let fill_perm: u32 = u32(sdiv_i32((clamped_x - min_vis) * 1000, u32(max_vis - min_vis)));
    let bar_x0: i32    = x0_i + 5;
    let bar_x1: i32    = max(x1_i - 5, bar_x0 + 1);
    let bar_y0: i32    = y1_i - 7;
    let bar_y1: i32    = y1_i - 4;
    if (i32(gid.x) >= bar_x0 && i32(gid.x) < bar_x1 &&
        i32(gid.y) >= bar_y0 && i32(gid.y) < bar_y1) {
        let bar_w: u32 = u32(bar_x1 - bar_x0);
        let rel_x: u32 = u32(i32(gid.x) - bar_x0);
        if (rel_x * 1000u < fill_perm * bar_w) {
            pix_r = tint.x;
            pix_g = tint.y;
            pix_b = tint.z;
        } else {
            pix_r = pix_r / 2u;
            pix_g = pix_g / 2u;
            pix_b = pix_b / 2u;
        }
    }

    let tenths: u32   = (abs_i32_to_u32(x_q16) * 10u + 32768u) >> 16u;
    let glyph_x0: i32 = sdiv_i32(x0_i + x1_i, 2u) - 11;
    let glyph_y0: i32 = max(y0_i + 3, sdiv_i32(y0_i + bar_y0 - 9, 2u));
    if (eval_readout_pixel(tenths, i32(gid.x) - glyph_x0, i32(gid.y) - glyph_y0)) {
        pix_r = 242u;
        pix_g = 248u;
        pix_b = 255u;
    }

    let border2: i32 = 2 * i32(w.u9);
    if (sdf2 >= -border2) {
        pix_r = (pix_r * 300u + tint.x * 700u) / 1000u;
        pix_g = (pix_g * 300u + tint.y * 700u) / 1000u;
        pix_b = (pix_b * 300u + tint.z * 700u) / 1000u;
    }

    final_pixels[idx] = pack_rgba8(pix_r, pix_g, pix_b, 255u);
}
