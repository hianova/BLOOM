// ============================================================================
// BLOOM v7.0.1 — Unified Single-Source Cross-Platform Compute Shader (.wgsl)
// ============================================================================
// Single source of truth over the 16,384-byte (256 x 64B) BloomEngineArena.
// Compiled at build time via W3C Naga into both:
//   - build/bloom_core.metal (MSL 2.4 for Apple Metal via SDL3 GPU)
//   - build/bloom_core.spv   (SPIR-V for Vulkan / Lavapipe via SDL3 GPU)
// Zero 64-bit shader integer extensions required: Wave64 masks use u32x2.
// ============================================================================

struct Cell64 {
    q0 : vec4<u32>,
    q1 : vec4<u32>,
    q2 : vec4<u32>,
    q3 : vec4<u32>,
};

@group(0) @binding(0) var<storage, read> arena : array<Cell64, 256>;
@group(1) @binding(0) var out_tex : texture_storage_2d<rgba8unorm, write>;

// ----------------------------------------------------------------------------
// 0. Fixed-Point, Bitfield & Vector Algebra Helpers
// ----------------------------------------------------------------------------

fn q16_to_f32(v : u32) -> f32 {
    return f32(bitcast<i32>(v)) * (1.0 / 65536.0);
}

fn unpack_rgb24(rgb : u32) -> vec3<f32> {
    return vec3<f32>(
        f32((rgb >> 16u) & 0xFFu),
        f32((rgb >> 8u) & 0xFFu),
        f32(rgb & 0xFFu)
    ) * (1.0 / 255.0);
}

fn unpack_q8_pair(w : u32) -> vec2<f32> {
    let lo = bitcast<i32>(w << 16u) >> 16u;
    let hi = bitcast<i32>(w) >> 16u;
    return vec2<f32>(f32(lo), f32(hi)) * (1.0 / 256.0);
}

fn safe_norm3(v : vec3<f32>, fallback : vec3<f32>) -> vec3<f32> {
    let len2 = dot(v, v);
    if (len2 > 1e-10) {
        return v * inverseSqrt(len2);
    }
    return fallback;
}

fn wave64_test_u32x2(lo32 : u32, hi32 : u32, bit_idx : u32) -> bool {
    let w = select(hi32, lo32, bit_idx < 32u);
    return ((w >> (bit_idx & 31u)) & 1u) != 0u;
}

fn vec4_u32_lane(v : vec4<u32>, idx : u32) -> u32 {
    if (idx == 0u) { return v.x; }
    if (idx == 1u) { return v.y; }
    if (idx == 2u) { return v.z; }
    return v.w;
}

fn unpack_byte16(v : vec4<u32>, k : u32) -> u32 {
    if (k >= 16u) { return 0u; }
    let w = vec4_u32_lane(v, k >> 2u);
    return (w >> ((k & 3u) * 8u)) & 0xFFu;
}

fn get_ui_label_vec4(node_idx : u32) -> vec4<u32> {
    let cell = arena[250u + ((node_idx & 15u) >> 2u)];
    let q_idx = node_idx & 3u;
    if (q_idx == 0u) { return cell.q0; }
    if (q_idx == 1u) { return cell.q1; }
    if (q_idx == 2u) { return cell.q2; }
    return cell.q3;
}

fn unpack_text_input_byte(k : u32) -> u32 {
    if (k >= 40u) { return 0u; }
    let c = arena[5];
    let wi = k >> 2u;
    var w : u32 = 0u;
    switch (wi) {
        case 0u: { w = c.q0.x; }
        case 1u: { w = c.q0.y; }
        case 2u: { w = c.q0.z; }
        case 3u: { w = c.q0.w; }
        case 4u: { w = c.q1.x; }
        case 5u: { w = c.q1.y; }
        case 6u: { w = c.q1.z; }
        case 7u: { w = c.q1.w; }
        case 8u: { w = c.q2.x; }
        default: { w = c.q2.y; }
    }
    return (w >> ((k & 3u) * 8u)) & 0xFFu;
}

fn unpack_ime_preedit_byte(k : u32) -> u32 {
    if (k >= 36u) { return 0u; }
    let c = arena[6];
    let wi = (k >> 2u) + 2u;
    var w : u32 = 0u;
    switch (wi) {
        case 2u: { w = c.q0.z; }
        case 3u: { w = c.q0.w; }
        case 4u: { w = c.q1.x; }
        case 5u: { w = c.q1.y; }
        case 6u: { w = c.q1.z; }
        case 7u: { w = c.q1.w; }
        case 8u: { w = c.q2.x; }
        case 9u: { w = c.q2.y; }
        default: { w = c.q2.z; }
    }
    return (w >> ((k & 3u) * 8u)) & 0xFFu;
}

fn aces_tonemap(x : vec3<f32>) -> vec3<f32> {
    let v = max(x, vec3<f32>(0.0));
    return clamp((v * (2.51 * v + 0.03)) / (v * (2.43 * v + 0.59) + 0.14), vec3<f32>(0.0), vec3<f32>(1.0));
}

// ----------------------------------------------------------------------------
// 1. 3D CSG & Prefab SDF Kernel (Exact Parity with bloom_prefab_eval_sdf_q16)
// ----------------------------------------------------------------------------

fn smin_poly(a : f32, b : f32, k : f32) -> f32 {
    let kk = max(k, 1e-4);
    let h = clamp(0.5 + 0.5 * (b - a) / kk, 0.0, 1.0);
    return mix(b, a, h) - kk * h * (1.0 - h);
}

fn sd_prim(prim_type : u32, p : vec3<f32>, ext : vec3<f32>, bevel : f32) -> f32 {
    let e = max(ext, vec3<f32>(0.00390625));
    if (prim_type == 1u) {
        return length(p) - e.x;
    } else if (prim_type == 2u) {
        let q = vec2<f32>(length(p.xz) - e.x, p.y);
        return length(q) - e.y;
    } else if (prim_type == 3u) {
        let q = abs(p) - max(e - vec3<f32>(bevel), vec3<f32>(0.0));
        return length(max(q, vec3<f32>(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0) - bevel;
    } else if (prim_type == 4u) {
        let ap = abs(p);
        return (ap.x + ap.y + ap.z - e.x) * 0.57735027;
    } else if (prim_type == 5u) {
        let d = vec2<f32>(length(p.xz) - e.x, abs(p.y) - e.y);
        return min(max(d.x, d.y), 0.0) + length(max(d, vec2<f32>(0.0))) - bevel;
    } else if (prim_type == 6u) {
        var q = p;
        q.y = q.y - clamp(q.y, -e.y, e.y);
        return length(q) - e.x;
    }
    return 1e5;
}

fn eval_prefab_sdf(pf : Cell64, lp : vec3<f32>, param_delta : f32) -> f32 {
    let mask = pf.q0.w;
    let prim1 = mask & 0xFu;
    let prim2 = (mask >> 4u) & 0xFu;
    let bop   = (mask >> 8u) & 0xFu;

    let p0 = clamp(q16_to_f32(pf.q1.y) + param_delta, 0.0625, 8.0);
    let p1 = q16_to_f32(pf.q1.z);
    let p2 = q16_to_f32(pf.q1.w);
    let e1 = vec3<f32>(p0, clamp(select(p0, p1, p1 > 0.0), 0.0625, 8.0), clamp(select(p0, p2, p2 > 0.0), 0.0625, 8.0));
    let d1 = sd_prim(prim1, lp, e1, 0.03125);
    if (prim2 == 0u) {
        return d1;
    }

    let p3 = clamp(q16_to_f32(pf.q2.x), 0.0625, 8.0);
    let p4 = q16_to_f32(pf.q2.y);
    let p5 = max(q16_to_f32(pf.q2.z), 0.125);
    let e2 = vec3<f32>(p3, clamp(select(p3, p4, p4 > 0.0), 0.0625, 8.0), p3);
    let d2 = sd_prim(prim2, lp, e2, 0.015625);

    if (bop == 1u) { return max(d1, -d2); }
    if (bop == 2u) { return max(d1, d2); }
    if (bop == 3u) { return smin_poly(d1, d2, p5); }
    return min(d1, d2);
}

struct SceneHit {
    dist      : f32,
    inst_id   : u32,
    inst_slot : u32,
    mat_id    : u32,
    style_id  : u32,
    is_cut    : bool,
    raw_d     : f32,
};

fn map_scene_dist(p : vec3<f32>, xray_on : bool, xray_plane : f32) -> f32 {
    let inst_cnt = min(arena[0].q1.z, 32u);
    var best_d : f32 = 1e5;
    for (var i : u32 = 0u; i < inst_cnt; i = i + 1u) {
        let inst = arena[40u + i];
        let flags = (inst.q3.x >> 16u) & 0xFFFFu;
        if ((flags & 1u) == 0u) { continue; }
        let pid = inst.q0.y;
        if (pid == 0u || pid > 16u) { continue; }

        let pos = vec3<f32>(q16_to_f32(inst.q1.x), q16_to_f32(inst.q1.y), q16_to_f32(inst.q1.z));
        let raw_sc = q16_to_f32(inst.q2.z);
        let sc = select(1.0, clamp(raw_sc, 0.0625, 256.0), raw_sc > 0.0625);
        let p_ovr = q16_to_f32(inst.q3.z);

        let pf = arena[24u + (pid - 1u)];
        let bound_r = q16_to_f32(pf.q1.x) * sc + 0.5;
        let rel = p - pos;
        if (length(rel) - bound_r >= best_d) { continue; }

        var d = eval_prefab_sdf(pf, rel / sc, p_ovr) * sc;
        if (xray_on) {
            d = max(d, xray_plane - p.x);
        }
        if (d < best_d) {
            best_d = d;
        }
    }
    return best_d;
}

fn map_scene_full(p : vec3<f32>, xray_on : bool, xray_plane : f32) -> SceneHit {
    let inst_cnt = min(arena[0].q1.z, 32u);
    var out : SceneHit;
    out.dist = 1e5;
    out.inst_id = 0u;
    out.inst_slot = 0u;
    out.mat_id = 0u;
    out.style_id = 1u;
    out.is_cut = false;
    out.raw_d = 1e5;

    for (var i : u32 = 0u; i < inst_cnt; i = i + 1u) {
        let inst = arena[40u + i];
        let flags = (inst.q3.x >> 16u) & 0xFFFFu;
        if ((flags & 1u) == 0u) { continue; }
        let pid = inst.q0.y;
        if (pid == 0u || pid > 16u) { continue; }

        let pos = vec3<f32>(q16_to_f32(inst.q1.x), q16_to_f32(inst.q1.y), q16_to_f32(inst.q1.z));
        let raw_sc = q16_to_f32(inst.q2.z);
        let sc = select(1.0, clamp(raw_sc, 0.0625, 256.0), raw_sc > 0.0625);
        let p_ovr = q16_to_f32(inst.q3.z);

        let pf = arena[24u + (pid - 1u)];
        let bound_r = q16_to_f32(pf.q1.x) * sc + 0.5;
        let rel = p - pos;
        if (length(rel) - bound_r >= out.dist) { continue; }

        let raw_d = eval_prefab_sdf(pf, rel / sc, p_ovr) * sc;
        var d = raw_d;
        var cut_face = false;
        if (xray_on) {
            let clip_d = xray_plane - p.x;
            if (clip_d > d) {
                d = clip_d;
                cut_face = true;
            }
        }
        if (d < out.dist) {
            out.dist = d;
            out.inst_id = select(i + 1u, inst.q0.x, inst.q0.x != 0u);
            out.inst_slot = i;
            let is_ovr = (flags & 4u) != 0u;
            let mat_ovr = inst.q0.w;
            out.mat_id = select(pf.q0.z & 7u, mat_ovr & 7u, is_ovr && mat_ovr < 8u);
            let sty_ovr = inst.q0.z;
            let pf_sty = clamp(pf.q0.y, 1u, 8u);
            out.style_id = select(pf_sty, sty_ovr, is_ovr && sty_ovr >= 1u && sty_ovr <= 8u);
            out.is_cut = cut_face;
            out.raw_d = raw_d;
        }
    }
    return out;
}

fn calc_normal(p : vec3<f32>, xray_on : bool, xray_plane : f32) -> vec3<f32> {
    let e = 0.0022;
    let gx = map_scene_dist(p + vec3<f32>(e, 0.0, 0.0), xray_on, xray_plane) -
             map_scene_dist(p - vec3<f32>(e, 0.0, 0.0), xray_on, xray_plane);
    let gy = map_scene_dist(p + vec3<f32>(0.0, e, 0.0), xray_on, xray_plane) -
             map_scene_dist(p - vec3<f32>(0.0, e, 0.0), xray_on, xray_plane);
    let gz = map_scene_dist(p + vec3<f32>(0.0, 0.0, e), xray_on, xray_plane) -
             map_scene_dist(p - vec3<f32>(0.0, 0.0, e), xray_on, xray_plane);
    return safe_norm3(vec3<f32>(gx, gy, gz), vec3<f32>(0.0, 1.0, 0.0));
}

fn calc_ao(p : vec3<f32>, n : vec3<f32>, xray_on : bool, xray_plane : f32) -> f32 {
    var occ : f32 = 0.0;
    var w : f32 = 1.0;
    for (var i : u32 = 0u; i < 5u; i = i + 1u) {
        let h = 0.025 + 0.055 * f32(i);
        let d = map_scene_dist(p + n * h, xray_on, xray_plane);
        occ = occ + max(h - d, 0.0) * w;
        w = w * 0.68;
    }
    return clamp(1.0 - 2.6 * occ, 0.10, 1.0);
}

fn calc_soft_shadow(ro : vec3<f32>, rd : vec3<f32>, k_pen : f32, xray_on : bool, xray_plane : f32) -> f32 {
    var res : f32 = 1.0;
    var t : f32 = 0.03;
    for (var i : u32 = 0u; i < 14u; i = i + 1u) {
        if (t > 6.5) { break; }
        let h = map_scene_dist(ro + rd * t, xray_on, xray_plane);
        if (h < 0.0015) {
            return 0.04;
        }
        res = min(res, k_pen * h / t);
        t = t + clamp(h, 0.025, 0.45);
    }
    return clamp(res, 0.04, 1.0);
}

// ----------------------------------------------------------------------------
// 2. Studio HDR Environment, Architectural Floor & Physical Matter Optics
// ----------------------------------------------------------------------------

fn eval_sky_dome(rd : vec3<f32>) -> vec3<f32> {
    let sky_t = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    var sky = mix(vec3<f32>(0.040, 0.056, 0.096), vec3<f32>(0.010, 0.016, 0.034), sky_t);
    let center_glow = pow(clamp(dot(rd, vec3<f32>(0.0, 0.08, 0.9968)), 0.0, 1.0), 5.0);
    sky = sky + vec3<f32>(0.014, 0.026, 0.048) * center_glow;
    return sky;
}

fn eval_studio_env(dir : vec3<f32>, roughness : f32) -> vec3<f32> {
    let r = clamp(roughness, 0.02, 1.0);
    let key_dir  = vec3<f32>(-0.5145, 0.7503, -0.4150);
    let rim_dir  = vec3<f32>( 0.7420, 0.3610,  0.5650);
    let fill_dir = vec3<f32>( 0.1450, 0.9550, -0.2580);

    let key_exp  = mix(42.0, 3.5, r);
    let rim_exp  = mix(28.0, 3.0, r);
    let fill_exp = mix(14.0, 2.0, r);

    let key_lobe  = pow(clamp(dot(dir, key_dir),  0.0, 1.0), key_exp)  * mix(3.2, 0.85, r);
    let rim_lobe  = pow(clamp(dot(dir, rim_dir),  0.0, 1.0), rim_exp)  * mix(2.2, 0.70, r);
    let fill_lobe = pow(clamp(dot(dir, fill_dir), 0.0, 1.0), fill_exp) * mix(0.95, 0.45, r);

    let horiz_band = exp(-abs(dir.y - 0.05) * mix(16.0, 5.0, r)) * (0.30 + 0.20 * clamp(1.0 - r, 0.0, 1.0));
    let studio_bounce = vec3<f32>(0.11, 0.15, 0.22) * clamp(0.65 - 0.45 * dir.y, 0.15, 1.0);
    let dome = eval_sky_dome(dir);

    return dome
         + studio_bounce
         + vec3<f32>(1.00, 0.93, 0.84) * key_lobe
         + vec3<f32>(0.32, 0.78, 1.15) * rim_lobe
         + vec3<f32>(0.58, 0.72, 0.95) * fill_lobe
         + vec3<f32>(0.42, 0.56, 0.74) * horiz_band;
}

fn eval_floor_grid(gp : vec3<f32>, tf : f32, abs_rdy : f32, px_ang : f32) -> vec3<f32> {
    let fw_w = max((tf * px_ang) / max(abs_rdy, 0.06), 0.002);

    // Minor architectural grid (0.5m cells)
    let uv_min = gp.xz * 2.0;
    let g_min = abs(fract(uv_min - vec2<f32>(0.5)) - vec2<f32>(0.5)) / max(fw_w * 2.0, 1e-4);
    let line_min = (1.0 - smoothstep(0.0, 1.35, min(g_min.x, g_min.y))) * clamp(1.0 - fw_w * 3.2, 0.0, 1.0);

    // Major architectural grid (2.0m cells)
    let uv_maj = gp.xz * 0.5;
    let g_maj = abs(fract(uv_maj - vec2<f32>(0.5)) - vec2<f32>(0.5)) / max(fw_w * 0.5, 1e-4);
    let line_maj = (1.0 - smoothstep(0.0, 1.45, min(g_maj.x, g_maj.y))) * clamp(1.0 - fw_w * 0.9, 0.0, 1.0);

    let dist_fade = exp(-tf * 0.105);
    var fcol = vec3<f32>(0.018, 0.026, 0.044);
    fcol = mix(fcol, vec3<f32>(0.055, 0.195, 0.295), line_min * dist_fade * 0.34);
    fcol = mix(fcol, vec3<f32>(0.105, 0.380, 0.540), line_maj * dist_fade * 0.62);
    return fcol;
}

fn eval_backdrop_ray(ro : vec3<f32>, rd : vec3<f32>, px_ang : f32) -> vec3<f32> {
    let sky = eval_sky_dome(rd);
    if (rd.y < -0.004) {
        let tf = (-1.25 - ro.y) / rd.y;
        if (tf > 0.0 && tf < 28.0) {
            let gp = ro + rd * tf;
            let fcol = eval_floor_grid(gp, tf, abs(rd.y), px_ang);
            return mix(sky, fcol, exp(-tf * 0.065));
        }
    }
    return sky;
}

fn shade_direct_hit(
    hit : SceneHit,
    p : vec3<f32>,
    n : vec3<f32>,
    rd : vec3<f32>,
    trc_safe : bool,
    time_s : f32
) -> vec3<f32> {
    let m_cell = arena[8u + (hit.mat_id & 7u)];
    let s_cell = arena[16u + (clamp(hit.style_id, 1u, 8u) - 1u)];
    let inst_k = arena[40u + (hit.inst_slot & 31u)].q2.w;

    let eta   = clamp(q16_to_f32(m_cell.q0.y), 1.0, 4.0);
    let kappa = clamp(q16_to_f32(m_cell.q0.z), 0.0, 8.0);
    let rough = clamp(q16_to_f32(m_cell.q1.y), 0.02, 1.0);
    let mie   = clamp(q16_to_f32(m_cell.q1.z), 0.0, 1.0);
    let mu_a  = vec3<f32>(q16_to_f32(m_cell.q1.w), q16_to_f32(m_cell.q2.x), q16_to_f32(m_cell.q2.y));
    let d_mm  = clamp(q16_to_f32(m_cell.q2.w) * 0.125, 0.08, 3.0);
    let kelvin = max(m_cell.q3.x, inst_k);

    let base_tint = unpack_rgb24(s_cell.q0.y);
    let metallic = clamp(max(q16_to_f32(s_cell.q1.x), clamp((kappa - 0.35) * 0.5, 0.0, 1.0)), 0.0, 1.0);

    let key_dir = vec3<f32>(-0.5145, 0.7503, -0.4150);
    let ndv = clamp(dot(n, -rd), 0.0, 1.0);
    let ndl = clamp(dot(n, key_dir), 0.0, 1.0);
    let refl_d = reflect(rd, n);

    let f0_scalar = ((eta - 1.0) * (eta - 1.0) + kappa * kappa) /
                    max((eta + 1.0) * (eta + 1.0) + kappa * kappa, 1e-4);
    let f0_rgb = mix(vec3<f32>(f0_scalar), mix(vec3<f32>(0.88, 0.92, 0.98), base_tint, 0.82), metallic);
    let fres = f0_rgb + (vec3<f32>(1.0) - f0_rgb) * pow(max(1.0 - ndv, 1e-5), 5.0);

    let env_spec = eval_studio_env(refl_d, rough);
    let beer = exp(-mu_a * d_mm * 0.85);
    let body = beer * mix(vec3<f32>(0.55, 0.65, 0.78), base_tint, 0.65) * (0.35 + 0.65 * ndl + mie * 0.45);
    var col = body * (vec3<f32>(1.0) - fres) * (1.0 - metallic * 0.92) + env_spec * fres;

    if (kelvin > 650u) {
        let ex = clamp(f32(kelvin - 650u) * 0.001, 0.0, 2.2);
        var strobe_hz = clamp(q16_to_f32(s_cell.q2.y), 0.0, 12.0);
        if (trc_safe) { strobe_hz = min(strobe_hz, 3.0); }
        let pulse = select(1.0, 0.86 + 0.14 * cos(time_s * strobe_hz * 6.2831853), strobe_hz > 0.05 && time_s > 0.0);
        col = col + vec3<f32>(ex * 1.45, ex * ex * 0.88, max(0.0, ex - 0.42) * 0.78) * pulse;
    }
    if (hit.is_cut) {
        col = mix(col, vec3<f32>(0.04, 0.96, 0.84), 0.55);
    }
    return col;
}

fn shade_full_hit(
    hit : SceneHit,
    p : vec3<f32>,
    n : vec3<f32>,
    rd : vec3<f32>,
    px_ang : f32,
    xray_on : bool,
    xray_plane : f32,
    trc_safe : bool,
    cb_geom : bool,
    selected_id : u32,
    time_s : f32
) -> vec3<f32> {
    let m_cell = arena[8u + (hit.mat_id & 7u)];
    let s_cell = arena[16u + (clamp(hit.style_id, 1u, 8u) - 1u)];
    let inst_k = arena[40u + (hit.inst_slot & 31u)].q2.w;

    let eta    = clamp(q16_to_f32(m_cell.q0.y), 1.0, 4.0);
    let kappa  = clamp(q16_to_f32(m_cell.q0.z), 0.0, 8.0);
    let rough  = clamp(q16_to_f32(m_cell.q1.y), 0.02, 1.0);
    let mie    = clamp(q16_to_f32(m_cell.q1.z), 0.0, 1.0);
    let mu_a   = vec3<f32>(q16_to_f32(m_cell.q1.w), q16_to_f32(m_cell.q2.x), q16_to_f32(m_cell.q2.y));
    let wire_w = clamp(q16_to_f32(m_cell.q2.z), 0.0, 1.0);
    let d_mm   = clamp(q16_to_f32(m_cell.q2.w) * 0.125, 0.08, 3.0);
    let kelvin = max(m_cell.q3.x, inst_k);

    let base_tint   = unpack_rgb24(s_cell.q0.y);
    let accent_tint = unpack_rgb24(s_cell.q0.z);
    let style_rough = clamp(q16_to_f32(s_cell.q0.w), 0.02, 1.0);
    let eff_rough   = clamp(mix(rough, style_rough, 0.35), 0.025, 1.0);
    let metallic    = clamp(max(q16_to_f32(s_cell.q1.x), clamp((kappa - 0.35) * 0.5, 0.0, 1.0)), 0.0, 1.0);

    let key_dir = vec3<f32>(-0.5145, 0.7503, -0.4150);
    let rim_dir = vec3<f32>( 0.7420, 0.3610,  0.5650);

    let ao = calc_ao(p, n, xray_on, xray_plane);
    let sh = calc_soft_shadow(p + n * 0.018, key_dir, 11.0, xray_on, xray_plane);

    let ndv = clamp(dot(n, -rd), 0.0, 1.0);
    let ndl = clamp(dot(n, key_dir), 0.0, 1.0);
    let ndr = clamp(dot(n, rim_dir), 0.0, 1.0);
    let refl_d = reflect(rd, n);

    let f0_dielec = ((eta - 1.0) * (eta - 1.0) + kappa * kappa) /
                    max((eta + 1.0) * (eta + 1.0) + kappa * kappa, 1e-4);
    let metal_albedo = mix(
        mix(vec3<f32>(0.84, 0.89, 0.95), vec3<f32>(1.00, 0.82, 0.38), clamp(mu_a.z * 0.9, 0.0, 1.0)),
        base_tint,
        0.58
    );
    let f0_rgb = mix(vec3<f32>(clamp(f0_dielec, 0.035, 0.24)), metal_albedo, metallic);
    let fres = f0_rgb + (vec3<f32>(1.0) - f0_rgb) * pow(max(1.0 - ndv, 1e-5), 5.0);

    // Specular GGX-like highlight + 3-Light HDR Studio Environment
    let h_key = safe_norm3(key_dir - rd, vec3<f32>(0.0, 1.0, 0.0));
    let spec_pow = mix(140.0, 7.0, eff_rough);
    let spec_key = pow(max(clamp(dot(n, h_key), 0.0, 1.0), 1e-6), spec_pow) * mix(1.45, 0.22, eff_rough) * sh;
    let h_rim = safe_norm3(rim_dir - rd, vec3<f32>(0.0, 1.0, 0.0));
    let spec_rim = pow(max(clamp(dot(n, h_rim), 0.0, 1.0), 1e-6), mix(72.0, 6.0, eff_rough)) * 0.55;

    let env_refl = eval_studio_env(refl_d, eff_rough) * (0.45 + 0.55 * ao);
    let spec_total = env_refl * fres
                   + vec3<f32>(1.00, 0.94, 0.86) * spec_key * (vec3<f32>(0.35) + 0.65 * f0_rgb)
                   + vec3<f32>(0.35, 0.82, 1.15) * spec_rim * f0_rgb;

    var col = vec3<f32>(0.0);

    if (eta > 1.02 && metallic < 0.5) {
        // True Snell Secondary Refraction + Internal Optical Thickness + Chromatic Dispersion
        let eta_inv = 1.0 / max(eta, 1.01);
        let refr_raw = refract(rd, n, eta_inv);
        let refr_d = safe_norm3(select(refr_raw, refl_d, dot(refr_raw, refr_raw) < 1e-5), rd);

        let p_enter = p - n * 0.016;
        var solid_dist : f32 = 0.0;
        var t_in : f32 = 0.0;
        var p_exit : vec3<f32> = p_enter;
        var prev_solid : bool = true;
        var cavity_crossings : f32 = 0.0;

        for (var k : u32 = 0u; k < 16u; k = k + 1u) {
            let samp_p = p_enter + refr_d * t_in;
            let sd = map_scene_dist(samp_p, xray_on, xray_plane);
            let step_s = clamp(abs(sd), 0.042, 0.22);
            if (sd < 0.0025) {
                solid_dist = solid_dist + step_s;
                p_exit = samp_p;
                prev_solid = true;
            } else {
                if (prev_solid && k > 0u) {
                    cavity_crossings = cavity_crossings + 1.0;
                }
                prev_solid = false;
            }
            t_in = t_in + step_s;
            if (t_in > 3.0) { break; }
        }

        let n_exit = calc_normal(p_exit, xray_on, xray_plane);
        let exit_raw = refract(refr_d, -n_exit, eta);
        let exit_d = safe_norm3(select(exit_raw, reflect(refr_d, -n_exit), dot(exit_raw, exit_raw) < 1e-5), refr_d);

        let disp = (eta - 1.0) * 0.035;
        let exit_r = safe_norm3(exit_d - n_exit * disp, exit_d);
        let exit_b = safe_norm3(exit_d + n_exit * disp, exit_d);
        let ro_out = p_exit + exit_d * 0.03;

        let refr_rough = clamp(eff_rough + 0.10, 0.04, 1.0);
        let bg_r = eval_backdrop_ray(ro_out, exit_r, px_ang).r + eval_studio_env(exit_r, refr_rough).r * 0.34;
        let bg_g = eval_backdrop_ray(ro_out, exit_d, px_ang).g + eval_studio_env(exit_d, refr_rough).g * 0.34;
        let bg_b = eval_backdrop_ray(ro_out, exit_b, px_ang).b + eval_studio_env(exit_b, refr_rough).b * 0.34;
        let caustic = 1.0 + 0.48 * clamp((eta - 1.0) * (1.0 - eff_rough), 0.0, 1.2);
        let refr_scene = vec3<f32>(bg_r, bg_g, bg_b) * caustic;

        // Internal back-wall Fresnel bounce + Beer-Lambert volume attenuation + Jadeite SSS
        let tir_d = reflect(refr_d, -n_exit);
        let int_env = eval_studio_env(tir_d, clamp(eff_rough + 0.08, 0.04, 1.0)) * 0.22;

        let opt_path = d_mm * 0.40 + solid_dist * 1.65;
        let beer = exp(-mu_a * opt_path) * exp(-kappa * 1.6);

        let back_lit = pow(clamp(dot(rd, -key_dir), 0.0, 1.0), 3.0);
        let wrap_key = clamp(dot(n, key_dir) * 0.5 + 0.5, 0.0, 1.0) * (0.42 + 0.58 * sh);
        let jewel_tint = mix(beer, base_tint, 0.52);
        let sss = jewel_tint * (mie * (back_lit * 0.98 + wrap_key * 0.65 + ao * 0.26) + (0.14 + 0.20 * wrap_key) * ao);

        let cavity_glow = select(0.0, 0.18, cavity_crossings >= 1.5) * (1.0 - ndv * 0.45);
        let lattice = sin(p.x * 26.0) * sin(p.y * 26.0) * sin(p.z * 26.0);
        let wire_term = smoothstep(0.72, 0.96, abs(lattice)) * wire_w * 0.18;

        let transmitted = (refr_scene + int_env) * beer * (0.76 + 0.34 * wrap_key)
                        + sss
                        + accent_tint * (cavity_glow + wire_term) * beer;
        col = transmitted * (vec3<f32>(1.0) - fres) + spec_total;
    } else {
        // Brushed Titanium / Aurum Gold / Conductor & Ceramic PBR
        let diff_wrap = clamp(ndl * 0.75 + 0.25, 0.0, 1.0) * (0.30 + 0.70 * sh);
        let diff = metal_albedo * (1.0 - metallic * 0.85) * diff_wrap * ao * 0.55;
        let aniso_band = pow(clamp(1.0 - abs(n.y - 0.15), 0.0, 1.0), 4.0) * metallic * 0.16;
        let rim_metal = metal_albedo * pow(1.0 - ndv, 3.0) * (0.25 + 0.55 * ndr) * metallic * ao;
        col = diff + spec_total + metal_albedo * aniso_band + rim_metal;
    }

    // Colorblind Geometric Surface Contour Accessibility Mode
    if (cb_geom) {
        let freq = f32(clamp(hit.style_id, 1u, 8u)) * 10.0;
        let bands = smoothstep(0.65, 0.92, abs(sin((p.x + p.y + p.z) * freq)));
        col = mix(col, accent_tint * 1.15, bands * 0.28);
    }

    // Planck Thermal Incandescence (with <= 3.0 Hz TRC Photosensitive Clamp)
    if (kelvin > 650u) {
        let ex = clamp(f32(kelvin - 650u) * 0.001, 0.0, 2.2);
        var strobe_hz = clamp(q16_to_f32(s_cell.q2.y), 0.0, 12.0);
        if (trc_safe) { strobe_hz = min(strobe_hz, 3.0); }
        let pulse = select(1.0, 0.86 + 0.14 * cos(time_s * strobe_hz * 6.2831853), strobe_hz > 0.05 && time_s > 0.0);
        let core_heat = (0.65 + 0.55 * (1.0 - ao) + 0.35 * ndv) * pulse;
        let planck = vec3<f32>(ex * 1.42, ex * ex * 0.86, max(0.0, ex - 0.42) * 0.76);
        col = col * mix(1.0, 0.45, clamp(ex * 0.5, 0.0, 0.6)) + planck * core_heat;
    }

    // X-Ray Cutaway Cross-Section & Laser Perimeter Rim
    if (xray_on) {
        if (hit.is_cut) {
            let hatch = 0.5 + 0.5 * sin((p.y - p.z) * 52.0);
            let wall_edge = 1.0 - smoothstep(0.0, 0.032, abs(hit.raw_d));
            let cut_col = mix(vec3<f32>(0.02, 0.58, 0.62), vec3<f32>(0.08, 0.98, 0.86), 0.35 + 0.35 * hatch + 0.30 * ao);
            col = mix(col * 0.55, cut_col, 0.62);
            col = col + vec3<f32>(0.18, 1.15, 0.96) * wall_edge * 0.85;
        } else {
            let plane_rim = 1.0 - smoothstep(0.0, 0.020, abs(p.x - xray_plane));
            col = col + vec3<f32>(0.10, 1.05, 0.90) * plane_rim * 0.90;
        }
    }

    // Selected 3D Instance Crisp Cyan-Gold Fresnel Rim Highlight
    if (selected_id != 0u && hit.inst_id == selected_id) {
        let sel_rim = pow(max(1.0 - ndv, 1e-5), 2.6);
        let sel_col = mix(vec3<f32>(0.12, 0.94, 1.00), vec3<f32>(1.00, 0.84, 0.32), clamp(n.y * 0.5 + 0.5, 0.0, 1.0));
        col = col + sel_col * sel_rim * 0.92;
    }

    return col;
}

// ----------------------------------------------------------------------------
// 3. Vector SDF Stroke Font, CJK Ideograph Marks & Analytic SDF LaTeX Glyphs
// ----------------------------------------------------------------------------

fn sd_seg2d(p : vec2<f32>, a : vec2<f32>, b : vec2<f32>) -> f32 {
    let pa = p - a;
    let ba = b - a;
    let h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

fn eval_7seg(p : vec2<f32>, d : u32) -> f32 {
    let masks = array<u32, 10>(0x3Fu, 0x06u, 0x5Bu, 0x4Fu, 0x66u, 0x6Du, 0x7Du, 0x07u, 0x7Fu, 0x6Fu);
    let m = masks[d % 10u];
    var res : f32 = 1e5;
    if ((m & 1u) != 0u)  { res = min(res, sd_seg2d(p, vec2<f32>(-0.32, -0.65), vec2<f32>( 0.32, -0.65))); }
    if ((m & 2u) != 0u)  { res = min(res, sd_seg2d(p, vec2<f32>( 0.35, -0.60), vec2<f32>( 0.35, -0.04))); }
    if ((m & 4u) != 0u)  { res = min(res, sd_seg2d(p, vec2<f32>( 0.35,  0.04), vec2<f32>( 0.35,  0.60))); }
    if ((m & 8u) != 0u)  { res = min(res, sd_seg2d(p, vec2<f32>(-0.32,  0.65), vec2<f32>( 0.32,  0.65))); }
    if ((m & 16u) != 0u) { res = min(res, sd_seg2d(p, vec2<f32>(-0.35,  0.04), vec2<f32>(-0.35,  0.60))); }
    if ((m & 32u) != 0u) { res = min(res, sd_seg2d(p, vec2<f32>(-0.35, -0.60), vec2<f32>(-0.35, -0.04))); }
    if ((m & 64u) != 0u) { res = min(res, sd_seg2d(p, vec2<f32>(-0.30,  0.00), vec2<f32>( 0.30,  0.00))); }
    return res;
}

const SEG_TOP     : u32 = 1u << 0u;
const SEG_MID     : u32 = 1u << 1u;
const SEG_BOT     : u32 = 1u << 2u;
const SEG_MIDR    : u32 = 1u << 3u;
const SEG_TOPI    : u32 = 1u << 4u;
const SEG_MIDI    : u32 = 1u << 5u;
const SEG_BOTI    : u32 = 1u << 6u;
const SEG_LU      : u32 = 1u << 7u;
const SEG_LL      : u32 = 1u << 8u;
const SEG_RU      : u32 = 1u << 9u;
const SEG_RL      : u32 = 1u << 10u;
const SEG_CU      : u32 = 1u << 11u;
const SEG_CL      : u32 = 1u << 12u;
const SEG_BOWL_U  : u32 = 1u << 13u;
const SEG_BOWL_L  : u32 = 1u << 14u;
const SEG_D_ARCH  : u32 = 1u << 15u;
const SEG_AL      : u32 = 1u << 16u;
const SEG_AR      : u32 = 1u << 17u;
const SEG_VL      : u32 = 1u << 18u;
const SEG_VR      : u32 = 1u << 19u;
const SEG_TLM     : u32 = 1u << 20u;
const SEG_TRM     : u32 = 1u << 21u;
const SEG_DIAG_DR : u32 = 1u << 22u;
const SEG_DIAG_DL : u32 = 1u << 23u;
const SEG_K_UP    : u32 = 1u << 24u;
const SEG_R_LEG   : u32 = 1u << 25u;
const SEG_Q_TAIL  : u32 = 1u << 26u;
const SEG_W_FULL  : u32 = 1u << 27u;
const SEG_ABAR    : u32 = 1u << 28u;
const SEG_O_LOOP  : u32 = 1u << 29u;
const SEG_S_SPINE : u32 = 1u << 30u;
const SEG_C_ARCH  : u32 = 1u << 31u;

fn eval_mask_segments(p : vec2<f32>, m : u32) -> f32 {
    var d : f32 = 1e5;
    if ((m & SEG_TOP) != 0u)     { d = min(d, sd_seg2d(p, vec2<f32>(-0.32, -0.46), vec2<f32>( 0.32, -0.46))); }
    if ((m & SEG_MID) != 0u)     { d = min(d, sd_seg2d(p, vec2<f32>(-0.32,  0.00), vec2<f32>( 0.32,  0.00))); }
    if ((m & SEG_BOT) != 0u)     { d = min(d, sd_seg2d(p, vec2<f32>(-0.32,  0.46), vec2<f32>( 0.32,  0.46))); }
    if ((m & SEG_MIDR) != 0u)    { d = min(d, sd_seg2d(p, vec2<f32>( 0.02,  0.00), vec2<f32>( 0.32,  0.00))); }
    if ((m & SEG_TOPI) != 0u)    { d = min(d, sd_seg2d(p, vec2<f32>(-0.32, -0.46), vec2<f32>( 0.16, -0.46))); }
    if ((m & SEG_MIDI) != 0u)    { d = min(d, sd_seg2d(p, vec2<f32>(-0.32,  0.00), vec2<f32>( 0.16,  0.00))); }
    if ((m & SEG_BOTI) != 0u)    { d = min(d, sd_seg2d(p, vec2<f32>(-0.32,  0.46), vec2<f32>( 0.16,  0.46))); }
    if ((m & SEG_LU) != 0u)      { d = min(d, sd_seg2d(p, vec2<f32>(-0.32, -0.46), vec2<f32>(-0.32,  0.00))); }
    if ((m & SEG_LL) != 0u)      { d = min(d, sd_seg2d(p, vec2<f32>(-0.32,  0.00), vec2<f32>(-0.32,  0.46))); }
    if ((m & SEG_RU) != 0u)      { d = min(d, sd_seg2d(p, vec2<f32>( 0.32, -0.46), vec2<f32>( 0.32,  0.00))); }
    if ((m & SEG_RL) != 0u)      { d = min(d, sd_seg2d(p, vec2<f32>( 0.32,  0.00), vec2<f32>( 0.32,  0.46))); }
    if ((m & SEG_CU) != 0u)      { d = min(d, sd_seg2d(p, vec2<f32>( 0.00, -0.46), vec2<f32>( 0.00,  0.00))); }
    if ((m & SEG_CL) != 0u)      { d = min(d, sd_seg2d(p, vec2<f32>( 0.00,  0.00), vec2<f32>( 0.00,  0.46))); }
    if ((m & SEG_BOWL_U) != 0u) {
        d = min(d, sd_seg2d(p, vec2<f32>(0.16, -0.46), vec2<f32>(0.33, -0.33)));
        d = min(d, sd_seg2d(p, vec2<f32>(0.33, -0.33), vec2<f32>(0.33, -0.13)));
        d = min(d, sd_seg2d(p, vec2<f32>(0.33, -0.13), vec2<f32>(0.16,  0.00)));
    }
    if ((m & SEG_BOWL_L) != 0u) {
        d = min(d, sd_seg2d(p, vec2<f32>(0.16,  0.00), vec2<f32>(0.34,  0.13)));
        d = min(d, sd_seg2d(p, vec2<f32>(0.34,  0.13), vec2<f32>(0.34,  0.33)));
        d = min(d, sd_seg2d(p, vec2<f32>(0.34,  0.33), vec2<f32>(0.16,  0.46)));
    }
    if ((m & SEG_D_ARCH) != 0u) {
        d = min(d, sd_seg2d(p, vec2<f32>(0.16, -0.46), vec2<f32>(0.34, -0.28)));
        d = min(d, sd_seg2d(p, vec2<f32>(0.34, -0.28), vec2<f32>(0.34,  0.28)));
        d = min(d, sd_seg2d(p, vec2<f32>(0.34,  0.28), vec2<f32>(0.16,  0.46)));
    }
    if ((m & SEG_AL) != 0u)      { d = min(d, sd_seg2d(p, vec2<f32>( 0.00, -0.46), vec2<f32>(-0.34,  0.46))); }
    if ((m & SEG_AR) != 0u)      { d = min(d, sd_seg2d(p, vec2<f32>( 0.00, -0.46), vec2<f32>( 0.34,  0.46))); }
    if ((m & SEG_VL) != 0u)      { d = min(d, sd_seg2d(p, vec2<f32>(-0.34, -0.46), vec2<f32>( 0.00,  0.46))); }
    if ((m & SEG_VR) != 0u)      { d = min(d, sd_seg2d(p, vec2<f32>( 0.34, -0.46), vec2<f32>( 0.00,  0.46))); }
    if ((m & SEG_TLM) != 0u)     { d = min(d, sd_seg2d(p, vec2<f32>(-0.32, -0.46), vec2<f32>( 0.00,  0.04))); }
    if ((m & SEG_TRM) != 0u)     { d = min(d, sd_seg2d(p, vec2<f32>( 0.32, -0.46), vec2<f32>( 0.00,  0.04))); }
    if ((m & SEG_DIAG_DR) != 0u) { d = min(d, sd_seg2d(p, vec2<f32>(-0.32, -0.46), vec2<f32>( 0.32,  0.46))); }
    if ((m & SEG_DIAG_DL) != 0u) { d = min(d, sd_seg2d(p, vec2<f32>( 0.32, -0.46), vec2<f32>(-0.32,  0.46))); }
    if ((m & SEG_K_UP) != 0u)    { d = min(d, sd_seg2d(p, vec2<f32>(-0.32,  0.06), vec2<f32>( 0.32, -0.46))); }
    if ((m & SEG_R_LEG) != 0u)   { d = min(d, sd_seg2d(p, vec2<f32>( 0.06,  0.00), vec2<f32>( 0.34,  0.46))); }
    if ((m & SEG_Q_TAIL) != 0u)  { d = min(d, sd_seg2d(p, vec2<f32>( 0.05,  0.18), vec2<f32>( 0.36,  0.49))); }
    if ((m & SEG_W_FULL) != 0u) {
        d = min(d, sd_seg2d(p, vec2<f32>(-0.36, -0.46), vec2<f32>(-0.18,  0.46)));
        d = min(d, sd_seg2d(p, vec2<f32>(-0.18,  0.46), vec2<f32>( 0.00, -0.04)));
        d = min(d, sd_seg2d(p, vec2<f32>( 0.00, -0.04), vec2<f32>( 0.18,  0.46)));
        d = min(d, sd_seg2d(p, vec2<f32>( 0.18,  0.46), vec2<f32>( 0.36, -0.46)));
    }
    if ((m & SEG_ABAR) != 0u)    { d = min(d, sd_seg2d(p, vec2<f32>(-0.21,  0.14), vec2<f32>( 0.21,  0.14))); }
    if ((m & SEG_O_LOOP) != 0u) {
        d = min(d, sd_seg2d(p, vec2<f32>(-0.16, -0.46), vec2<f32>( 0.16, -0.46)));
        d = min(d, sd_seg2d(p, vec2<f32>( 0.16, -0.46), vec2<f32>( 0.33, -0.28)));
        d = min(d, sd_seg2d(p, vec2<f32>( 0.33, -0.28), vec2<f32>( 0.33,  0.28)));
        d = min(d, sd_seg2d(p, vec2<f32>( 0.33,  0.28), vec2<f32>( 0.16,  0.46)));
        d = min(d, sd_seg2d(p, vec2<f32>( 0.16,  0.46), vec2<f32>(-0.16,  0.46)));
        d = min(d, sd_seg2d(p, vec2<f32>(-0.16,  0.46), vec2<f32>(-0.33,  0.28)));
        d = min(d, sd_seg2d(p, vec2<f32>(-0.33,  0.28), vec2<f32>(-0.33, -0.28)));
        d = min(d, sd_seg2d(p, vec2<f32>(-0.33, -0.28), vec2<f32>(-0.16, -0.46)));
    }
    if ((m & SEG_S_SPINE) != 0u) {
        d = min(d, sd_seg2d(p, vec2<f32>( 0.30, -0.32), vec2<f32>( 0.16, -0.46)));
        d = min(d, sd_seg2d(p, vec2<f32>( 0.16, -0.46), vec2<f32>(-0.18, -0.46)));
        d = min(d, sd_seg2d(p, vec2<f32>(-0.18, -0.46), vec2<f32>(-0.32, -0.30)));
        d = min(d, sd_seg2d(p, vec2<f32>(-0.32, -0.30), vec2<f32>(-0.32, -0.13)));
        d = min(d, sd_seg2d(p, vec2<f32>(-0.32, -0.13), vec2<f32>( 0.32,  0.13)));
        d = min(d, sd_seg2d(p, vec2<f32>( 0.32,  0.13), vec2<f32>( 0.32,  0.30)));
        d = min(d, sd_seg2d(p, vec2<f32>( 0.32,  0.30), vec2<f32>( 0.16,  0.46)));
        d = min(d, sd_seg2d(p, vec2<f32>( 0.16,  0.46), vec2<f32>(-0.18,  0.46)));
        d = min(d, sd_seg2d(p, vec2<f32>(-0.18,  0.46), vec2<f32>(-0.32,  0.32)));
    }
    if ((m & SEG_C_ARCH) != 0u) {
        d = min(d, sd_seg2d(p, vec2<f32>( 0.32, -0.30), vec2<f32>( 0.16, -0.46)));
        d = min(d, sd_seg2d(p, vec2<f32>( 0.16, -0.46), vec2<f32>(-0.16, -0.46)));
        d = min(d, sd_seg2d(p, vec2<f32>(-0.16, -0.46), vec2<f32>(-0.33, -0.28)));
        d = min(d, sd_seg2d(p, vec2<f32>(-0.33, -0.28), vec2<f32>(-0.33,  0.28)));
        d = min(d, sd_seg2d(p, vec2<f32>(-0.33,  0.28), vec2<f32>(-0.16,  0.46)));
        d = min(d, sd_seg2d(p, vec2<f32>(-0.16,  0.46), vec2<f32>( 0.16,  0.46)));
        d = min(d, sd_seg2d(p, vec2<f32>( 0.16,  0.46), vec2<f32>( 0.32,  0.30)));
    }
    return d;
}

fn eval_ascii_glyph_sdf(uv_local : vec2<f32>, raw_ch : u32) -> f32 {
    var ch = raw_ch;
    if (ch >= 97u && ch <= 122u) {
        ch = ch - 32u;
    }
    if (ch <= 32u) {
        return 1e5;
    }

    // Uppercase A..Z
    if (ch >= 65u && ch <= 90u) {
        var mask : u32 = 0u;
        var extra : f32 = 1e5;
        switch (ch) {
            case 65u: { mask = SEG_AL | SEG_AR | SEG_ABAR; } // A
            case 66u: { mask = SEG_LU | SEG_LL | SEG_TOPI | SEG_MIDI | SEG_BOTI | SEG_BOWL_U | SEG_BOWL_L; } // B
            case 67u: { mask = SEG_C_ARCH; } // C
            case 68u: { mask = SEG_LU | SEG_LL | SEG_TOPI | SEG_BOTI | SEG_D_ARCH; } // D
            case 69u: { mask = SEG_LU | SEG_LL | SEG_TOP | SEG_MIDI | SEG_BOT; } // E
            case 70u: { mask = SEG_LU | SEG_LL | SEG_TOP | SEG_MIDI; } // F
            case 71u: { // G
                mask = SEG_C_ARCH | SEG_MIDR;
                extra = sd_seg2d(uv_local, vec2<f32>(0.32, 0.00), vec2<f32>(0.32, 0.30));
            }
            case 72u: { mask = SEG_LU | SEG_LL | SEG_RU | SEG_RL | SEG_MID; } // H
            case 73u: { // I
                mask = SEG_CU | SEG_CL;
                extra = min(
                    sd_seg2d(uv_local, vec2<f32>(-0.20, -0.46), vec2<f32>(0.20, -0.46)),
                    sd_seg2d(uv_local, vec2<f32>(-0.20,  0.46), vec2<f32>(0.20,  0.46))
                );
            }
            case 74u: { // J
                extra = min(
                    min(sd_seg2d(uv_local, vec2<f32>(0.04, -0.46), vec2<f32>(0.32, -0.46)),
                        sd_seg2d(uv_local, vec2<f32>(0.30, -0.46), vec2<f32>(0.30,  0.28))),
                    min(sd_seg2d(uv_local, vec2<f32>(0.30,  0.28), vec2<f32>(0.14,  0.46)),
                        min(sd_seg2d(uv_local, vec2<f32>(0.14,  0.46), vec2<f32>(-0.14, 0.46)),
                            sd_seg2d(uv_local, vec2<f32>(-0.14, 0.46), vec2<f32>(-0.30, 0.28))))
                );
            }
            case 75u: { // K
                mask = SEG_LU | SEG_LL | SEG_K_UP;
                extra = sd_seg2d(uv_local, vec2<f32>(-0.12, -0.10), vec2<f32>(0.34, 0.46));
            }
            case 76u: { mask = SEG_LU | SEG_LL | SEG_BOT; } // L
            case 77u: { mask = SEG_LU | SEG_LL | SEG_RU | SEG_RL | SEG_TLM | SEG_TRM; } // M
            case 78u: { mask = SEG_LU | SEG_LL | SEG_RU | SEG_RL | SEG_DIAG_DR; } // N
            case 79u: { mask = SEG_O_LOOP; } // O
            case 80u: { mask = SEG_LU | SEG_LL | SEG_TOPI | SEG_MIDI | SEG_BOWL_U; } // P
            case 81u: { mask = SEG_O_LOOP | SEG_Q_TAIL; } // Q
            case 82u: { mask = SEG_LU | SEG_LL | SEG_TOPI | SEG_MIDI | SEG_BOWL_U | SEG_R_LEG; } // R
            case 83u: { mask = SEG_S_SPINE; } // S
            case 84u: { mask = SEG_TOP | SEG_CU | SEG_CL; } // T
            case 85u: { // U
                extra = min(
                    min(sd_seg2d(uv_local, vec2<f32>(-0.32, -0.46), vec2<f32>(-0.32,  0.28)),
                        sd_seg2d(uv_local, vec2<f32>( 0.32, -0.46), vec2<f32>( 0.32,  0.28))),
                    min(sd_seg2d(uv_local, vec2<f32>(-0.32,  0.28), vec2<f32>(-0.16,  0.46)),
                        min(sd_seg2d(uv_local, vec2<f32>(-0.16,  0.46), vec2<f32>( 0.16,  0.46)),
                            sd_seg2d(uv_local, vec2<f32>( 0.16,  0.46), vec2<f32>( 0.32,  0.28))))
                );
            }
            case 86u: { mask = SEG_VL | SEG_VR; } // V
            case 87u: { mask = SEG_W_FULL; } // W
            case 88u: { mask = SEG_DIAG_DR | SEG_DIAG_DL; } // X
            case 89u: { mask = SEG_TLM | SEG_TRM | SEG_CL; } // Y
            default:  { mask = SEG_TOP | SEG_DIAG_DL | SEG_BOT; } // Z
        }
        return min(eval_mask_segments(uv_local, mask), extra);
    }

    // Digits 0..9
    if (ch >= 48u && ch <= 57u) {
        switch (ch) {
            case 48u: { // 0
                return min(
                    eval_mask_segments(uv_local, SEG_O_LOOP),
                    sd_seg2d(uv_local, vec2<f32>(-0.18, 0.24), vec2<f32>(0.18, -0.24))
                );
            }
            case 49u: { // 1
                return min(
                    sd_seg2d(uv_local, vec2<f32>(-0.20, -0.28), vec2<f32>(0.04, -0.46)),
                    min(sd_seg2d(uv_local, vec2<f32>( 0.04, -0.46), vec2<f32>(0.04,  0.46)),
                        sd_seg2d(uv_local, vec2<f32>(-0.22,  0.46), vec2<f32>(0.24,  0.46)))
                );
            }
            case 50u: { // 2
                var d = sd_seg2d(uv_local, vec2<f32>(-0.30, -0.28), vec2<f32>(-0.16, -0.46));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.16, -0.46), vec2<f32>( 0.16, -0.46)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.16, -0.46), vec2<f32>( 0.32, -0.30)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.32, -0.30), vec2<f32>( 0.32, -0.10)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.32, -0.10), vec2<f32>(-0.32,  0.46)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.32,  0.46), vec2<f32>( 0.34,  0.46)));
                return d;
            }
            case 51u: { // 3
                var d = eval_mask_segments(uv_local, SEG_BOWL_U | SEG_BOWL_L);
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.28, -0.46), vec2<f32>(0.16, -0.46)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.10,  0.00), vec2<f32>(0.16,  0.00)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.28,  0.46), vec2<f32>(0.16,  0.46)));
                return d;
            }
            case 52u: { // 4
                var d = sd_seg2d(uv_local, vec2<f32>( 0.16,  0.46), vec2<f32>( 0.16, -0.46));
                d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.16, -0.46), vec2<f32>(-0.34,  0.16)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.34,  0.16), vec2<f32>( 0.34,  0.16)));
                return d;
            }
            case 53u: { // 5
                var d = eval_mask_segments(uv_local, SEG_BOWL_L);
                d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.30, -0.46), vec2<f32>(-0.30, -0.46)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.30, -0.46), vec2<f32>(-0.30,  0.00)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.30,  0.00), vec2<f32>( 0.16,  0.00)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.30,  0.34), vec2<f32>(-0.16,  0.46)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.16,  0.46), vec2<f32>( 0.16,  0.46)));
                return d;
            }
            case 54u: { // 6
                var d = eval_mask_segments(uv_local, SEG_BOWL_L);
                d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.24, -0.46), vec2<f32>(-0.14, -0.46)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.14, -0.46), vec2<f32>(-0.32, -0.22)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.32, -0.22), vec2<f32>(-0.32,  0.28)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.32,  0.28), vec2<f32>(-0.16,  0.46)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.16,  0.46), vec2<f32>( 0.16,  0.46)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.32,  0.00), vec2<f32>( 0.16,  0.00)));
                return d;
            }
            case 55u: { // 7
                return min(
                    sd_seg2d(uv_local, vec2<f32>(-0.32, -0.46), vec2<f32>( 0.32, -0.46)),
                    sd_seg2d(uv_local, vec2<f32>( 0.32, -0.46), vec2<f32>(-0.10,  0.46))
                );
            }
            case 56u: { // 8
                var d = eval_mask_segments(uv_local, SEG_BOWL_U | SEG_BOWL_L);
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.16, -0.46), vec2<f32>( 0.16, -0.46)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.16,  0.00), vec2<f32>( 0.16,  0.00)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.16,  0.46), vec2<f32>( 0.16,  0.46)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.16, -0.46), vec2<f32>(-0.33, -0.33)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.33, -0.33), vec2<f32>(-0.33, -0.13)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.33, -0.13), vec2<f32>(-0.16,  0.00)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.16,  0.00), vec2<f32>(-0.34,  0.13)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.34,  0.13), vec2<f32>(-0.34,  0.33)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.34,  0.33), vec2<f32>(-0.16,  0.46)));
                return d;
            }
            default: { // 9
                var d = eval_mask_segments(uv_local, SEG_BOWL_U);
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.16, -0.46), vec2<f32>( 0.16, -0.46)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.16,  0.00), vec2<f32>( 0.32,  0.00)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.16, -0.46), vec2<f32>(-0.33, -0.33)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.33, -0.33), vec2<f32>(-0.33, -0.13)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.33, -0.13), vec2<f32>(-0.16,  0.00)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.33, -0.22), vec2<f32>( 0.33,  0.24)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.33,  0.24), vec2<f32>( 0.14,  0.46)));
                d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.14,  0.46), vec2<f32>(-0.20,  0.46)));
                return d;
            }
        }
    }

    // Punctuation & Operators
    switch (ch) {
        case 45u: { return sd_seg2d(uv_local, vec2<f32>(-0.26, 0.00), vec2<f32>(0.26, 0.00)); } // -
        case 43u: { // +
            return min(
                sd_seg2d(uv_local, vec2<f32>(-0.28,  0.00), vec2<f32>(0.28, 0.00)),
                sd_seg2d(uv_local, vec2<f32>( 0.00, -0.28), vec2<f32>(0.00, 0.28))
            );
        }
        case 47u: { return sd_seg2d(uv_local, vec2<f32>( 0.28, -0.48), vec2<f32>(-0.28, 0.48)); } // /
        case 92u: { return sd_seg2d(uv_local, vec2<f32>(-0.28, -0.48), vec2<f32>( 0.28, 0.48)); } // \
        case 58u: { // :
            return min(
                max(length(uv_local - vec2<f32>(0.0, -0.20)) - 0.035, 0.0),
                max(length(uv_local - vec2<f32>(0.0,  0.22)) - 0.035, 0.0)
            );
        }
        case 46u: { return max(length(uv_local - vec2<f32>(0.0, 0.40)) - 0.04, 0.0); } // .
        case 95u: { return sd_seg2d(uv_local, vec2<f32>(-0.34, 0.48), vec2<f32>(0.34, 0.48)); } // _
        case 61u: { // =
            return min(
                sd_seg2d(uv_local, vec2<f32>(-0.28, -0.14), vec2<f32>(0.28, -0.14)),
                sd_seg2d(uv_local, vec2<f32>(-0.28,  0.14), vec2<f32>(0.28,  0.14))
            );
        }
        case 40u: { // (
            return min(
                sd_seg2d(uv_local, vec2<f32>( 0.14, -0.48), vec2<f32>(-0.12, -0.20)),
                min(sd_seg2d(uv_local, vec2<f32>(-0.12, -0.20), vec2<f32>(-0.12,  0.20)),
                    sd_seg2d(uv_local, vec2<f32>(-0.12,  0.20), vec2<f32>( 0.14,  0.48)))
            );
        }
        case 41u: { // )
            return min(
                sd_seg2d(uv_local, vec2<f32>(-0.14, -0.48), vec2<f32>( 0.12, -0.20)),
                min(sd_seg2d(uv_local, vec2<f32>( 0.12, -0.20), vec2<f32>( 0.12,  0.20)),
                    sd_seg2d(uv_local, vec2<f32>( 0.12,  0.20), vec2<f32>(-0.14,  0.48)))
            );
        }
        case 60u: { // <
            return min(
                sd_seg2d(uv_local, vec2<f32>(0.22, -0.32), vec2<f32>(-0.22, 0.00)),
                sd_seg2d(uv_local, vec2<f32>(-0.22, 0.00), vec2<f32>(0.22,  0.32))
            );
        }
        case 62u: { // >
            return min(
                sd_seg2d(uv_local, vec2<f32>(-0.22, -0.32), vec2<f32>(0.22, 0.00)),
                sd_seg2d(uv_local, vec2<f32>( 0.22,  0.00), vec2<f32>(-0.22, 0.32))
            );
        }
        case 63u: { // ?
            var d = sd_seg2d(uv_local, vec2<f32>(-0.26, -0.30), vec2<f32>(-0.12, -0.46));
            d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.12, -0.46), vec2<f32>(0.16, -0.46)));
            d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.16, -0.46), vec2<f32>(0.30, -0.30)));
            d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.30, -0.30), vec2<f32>(0.00,  0.04)));
            d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.00,  0.04), vec2<f32>(0.00,  0.20)));
            d = min(d, max(length(uv_local - vec2<f32>(0.00, 0.42)) - 0.035, 0.0));
            return d;
        }
        case 42u: { // *
            let d1 = sd_seg2d(uv_local, vec2<f32>( 0.00, -0.28), vec2<f32>(0.00, 0.28));
            let d2 = sd_seg2d(uv_local, vec2<f32>(-0.25, -0.15), vec2<f32>(0.25, 0.15));
            let d3 = sd_seg2d(uv_local, vec2<f32>(-0.25,  0.15), vec2<f32>(0.25, -0.15));
            return min(d1, min(d2, d3));
        }
        case 64u: { // @
            var d = eval_mask_segments(uv_local, SEG_C_ARCH);
            d = min(d, sd_seg2d(uv_local, vec2<f32>(0.32, -0.30), vec2<f32>(0.32,  0.12)));
            d = min(d, sd_seg2d(uv_local, vec2<f32>(0.32,  0.12), vec2<f32>(0.14,  0.18)));
            d = min(d, abs(length(uv_local - vec2<f32>(0.02, 0.02)) - 0.16));
            return d;
        }
        case 33u: { // !
            return min(
                sd_seg2d(uv_local, vec2<f32>(0.00, -0.46), vec2<f32>(0.00, 0.18)),
                max(length(uv_local - vec2<f32>(0.00, 0.42)) - 0.035, 0.0)
            );
        }
        default: {
            return min(
                sd_seg2d(uv_local, vec2<f32>(-0.24, -0.24), vec2<f32>(0.24,  0.24)),
                sd_seg2d(uv_local, vec2<f32>( 0.24, -0.24), vec2<f32>(-0.24, 0.24))
            );
        }
    }
}

fn eval_cjk_glyph_sdf(uv_local : vec2<f32>, code_hash : u32) -> f32 {
    // Crisp architectural CJK ideograph cell in [-0.36..+0.36] x [-0.44..+0.44]
    var d = sd_seg2d(uv_local, vec2<f32>(-0.34, -0.38), vec2<f32>( 0.34, -0.38));
    d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.26, -0.14), vec2<f32>( 0.26, -0.14)));
    d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.26,  0.14), vec2<f32>( 0.26,  0.14)));
    d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.26, -0.14), vec2<f32>(-0.26,  0.18)));
    d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.26, -0.14), vec2<f32>( 0.26,  0.18)));
    d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.00, -0.44), vec2<f32>( 0.00,  0.44)));
    if ((code_hash & 1u) != 0u) {
        d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.04, 0.14), vec2<f32>(-0.34, 0.42)));
        d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.04, 0.14), vec2<f32>( 0.34, 0.42)));
    } else {
        d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.34, 0.40), vec2<f32>( 0.34, 0.40)));
    }
    if ((code_hash & 2u) != 0u) {
        d = min(d, sd_seg2d(uv_local, vec2<f32>(-0.22, -0.44), vec2<f32>(-0.10, -0.26)));
        d = min(d, sd_seg2d(uv_local, vec2<f32>( 0.22, -0.44), vec2<f32>( 0.10, -0.26)));
    }
    return d;
}

fn eval_latex_symbol_sdf(uv : vec2<f32>, cp : u32) -> f32 {
    switch (cp) {
        case 101u: { // 'e' -> Greek \eta
            var d = sd_seg2d(uv, vec2<f32>(-0.36, -0.18), vec2<f32>(-0.22, -0.32));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.22, -0.32), vec2<f32>(-0.22,  0.34)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.22, -0.14), vec2<f32>(-0.04, -0.32)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.04, -0.32), vec2<f32>( 0.18, -0.32)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.18, -0.32), vec2<f32>( 0.26, -0.16)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.26, -0.16), vec2<f32>( 0.22,  0.58)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.22,  0.58), vec2<f32>( 0.12,  0.66)));
            return d;
        }
        case 107u: { // 'k' -> Greek \kappa
            var d = sd_seg2d(uv, vec2<f32>(-0.22, -0.34), vec2<f32>(-0.28,  0.36));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.24,  0.04), vec2<f32>( 0.14, -0.32)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.14, -0.32), vec2<f32>( 0.28, -0.28)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.08, -0.06), vec2<f32>( 0.20,  0.34)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.20,  0.34), vec2<f32>( 0.30,  0.30)));
            return d;
        }
        case 112u: { // 'p' -> Greek \pi
            var d = sd_seg2d(uv, vec2<f32>(-0.36, -0.20), vec2<f32>(-0.24, -0.30));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.24, -0.30), vec2<f32>( 0.34, -0.30)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.14, -0.30), vec2<f32>(-0.22,  0.36)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.14, -0.30), vec2<f32>( 0.12,  0.26)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.12,  0.26), vec2<f32>( 0.22,  0.36)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.22,  0.36), vec2<f32>( 0.30,  0.28)));
            return d;
        }
        case 79u: { // 'O' -> Greek \Omega
            var d = sd_seg2d(uv, vec2<f32>(-0.14,  0.24), vec2<f32>(-0.32,  0.02));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.32,  0.02), vec2<f32>(-0.32, -0.24)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.32, -0.24), vec2<f32>(-0.16, -0.44)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.16, -0.44), vec2<f32>( 0.16, -0.44)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.16, -0.44), vec2<f32>( 0.32, -0.24)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.32, -0.24), vec2<f32>( 0.32,  0.02)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.32,  0.02), vec2<f32>( 0.14,  0.24)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.14,  0.24), vec2<f32>(-0.12,  0.42)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.36,  0.42), vec2<f32>(-0.12,  0.42)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.14,  0.24), vec2<f32>( 0.12,  0.42)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.12,  0.42), vec2<f32>( 0.36,  0.42)));
            return d;
        }
        case 97u: { // 'a' -> Greek \alpha
            var d = sd_seg2d(uv, vec2<f32>( 0.32, -0.28), vec2<f32>( 0.08,  0.28));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.08,  0.28), vec2<f32>(-0.16,  0.34)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.16,  0.34), vec2<f32>(-0.32,  0.16)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.32,  0.16), vec2<f32>(-0.32, -0.16)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.32, -0.16), vec2<f32>(-0.16, -0.34)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.16, -0.34), vec2<f32>( 0.08, -0.24)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.08, -0.24), vec2<f32>( 0.28,  0.32)));
            return d;
        }
        case 98u: { // 'b' -> Greek \beta
            var d = sd_seg2d(uv, vec2<f32>(-0.24,  0.60), vec2<f32>(-0.20, -0.34));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.20, -0.34), vec2<f32>(-0.04, -0.46)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.04, -0.46), vec2<f32>( 0.18, -0.46)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.18, -0.46), vec2<f32>( 0.28, -0.30)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.28, -0.30), vec2<f32>( 0.02, -0.06)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.02, -0.06), vec2<f32>( 0.30,  0.08)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.30,  0.08), vec2<f32>( 0.28,  0.30)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.28,  0.30), vec2<f32>( 0.10,  0.42)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.10,  0.42), vec2<f32>(-0.20,  0.36)));
            return d;
        }
        case 103u: { // 'g' -> Greek \gamma
            var d = sd_seg2d(uv, vec2<f32>(-0.30, -0.32), vec2<f32>(-0.02,  0.18));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.30, -0.32), vec2<f32>(-0.02,  0.18)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.02,  0.18), vec2<f32>(-0.04,  0.58)));
            return d;
        }
        case 100u: { // 'd' -> lowercase math d
            var d = sd_seg2d(uv, vec2<f32>( 0.12, -0.48), vec2<f32>( 0.22, -0.48));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.22, -0.48), vec2<f32>( 0.22,  0.42)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.22,  0.42), vec2<f32>( 0.32,  0.42)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.22, -0.10), vec2<f32>( 0.04, -0.22)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.04, -0.22), vec2<f32>(-0.18, -0.20)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.18, -0.20), vec2<f32>(-0.30, -0.02)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.30, -0.02), vec2<f32>(-0.30,  0.24)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.30,  0.24), vec2<f32>(-0.16,  0.42)));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.16,  0.42), vec2<f32>( 0.06,  0.42)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.06,  0.42), vec2<f32>( 0.22,  0.28)));
            return d;
        }
        case 105u: { // 'i'
            return min(
                sd_seg2d(uv, vec2<f32>(0.0, -0.12), vec2<f32>(0.0, 0.42)),
                max(length(uv - vec2<f32>(0.0, -0.36)) - 0.04, 0.0)
            );
        }
        case 120u: { // 'x' -> math italic x
            var d = sd_seg2d(uv, vec2<f32>(-0.28, -0.26), vec2<f32>(-0.16, -0.30));
            d = min(d, sd_seg2d(uv, vec2<f32>(-0.16, -0.30), vec2<f32>( 0.16,  0.30)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.16,  0.30), vec2<f32>( 0.28,  0.26)));
            d = min(d, sd_seg2d(uv, vec2<f32>( 0.26, -0.28), vec2<f32>(-0.26,  0.28)));
            return d;
        }
        default: {
            return eval_ascii_glyph_sdf(uv, cp);
        }
    }
}

fn eval_latex_box_iso(px_iso : vec2<f32>, bsz : vec2<f32>, aspect : f32, kind : u32, cp : u32) -> f32 {
    let sx = bsz.x * aspect;
    let sy = bsz.y;

    if (kind == 2u) {
        // \frac horizontal bar
        return sd_seg2d(px_iso, vec2<f32>(-sx, 0.0), vec2<f32>(sx, 0.0));
    }
    if (kind == 3u) {
        // \sqrt radical check in left gutter + vinculum overbar across top
        let gw = min(sy * 0.64 * aspect, sx * 0.55);
        let x_lead = -sx;
        let x_knee = -sx + 0.18 * gw;
        let x_dip  = -sx + 0.46 * gw;
        let x_top  = -sx + 0.96 * gw;
        let y_top  = -sy * 0.96;
        var d = sd_seg2d(px_iso, vec2<f32>(x_lead, 0.08 * sy), vec2<f32>(x_knee, -0.04 * sy));
        d = min(d, sd_seg2d(px_iso, vec2<f32>(x_knee, -0.04 * sy), vec2<f32>(x_dip, 0.84 * sy)));
        d = min(d, sd_seg2d(px_iso, vec2<f32>(x_dip, 0.84 * sy), vec2<f32>(x_top, y_top)));
        d = min(d, sd_seg2d(px_iso, vec2<f32>(x_top, y_top), vec2<f32>(sx * 0.98, y_top)));
        d = min(d, sd_seg2d(px_iso, vec2<f32>(sx * 0.98, y_top), vec2<f32>(sx * 0.98, -sy * 0.76)));
        return d;
    }
    if (kind == 4u) {
        // \int graceful tall integral curve with top & bottom serif curls
        var d = sd_seg2d(px_iso, vec2<f32>( 0.34 * sy, -0.78 * sy), vec2<f32>( 0.20 * sy, -0.92 * sy));
        d = min(d, sd_seg2d(px_iso, vec2<f32>( 0.20 * sy, -0.92 * sy), vec2<f32>( 0.07 * sy, -0.80 * sy)));
        d = min(d, sd_seg2d(px_iso, vec2<f32>( 0.07 * sy, -0.80 * sy), vec2<f32>(-0.07 * sy,  0.80 * sy)));
        d = min(d, sd_seg2d(px_iso, vec2<f32>(-0.07 * sy,  0.80 * sy), vec2<f32>(-0.20 * sy,  0.92 * sy)));
        d = min(d, sd_seg2d(px_iso, vec2<f32>(-0.20 * sy,  0.92 * sy), vec2<f32>(-0.34 * sy,  0.78 * sy)));
        return d;
    }
    if (kind == 5u) {
        // \sum capital sigma
        let w = min(sx * 0.90, sy * 0.76);
        let h = sy * 0.86;
        var d = sd_seg2d(px_iso, vec2<f32>( w, -h * 0.74), vec2<f32>( w, -h));
        d = min(d, sd_seg2d(px_iso, vec2<f32>( w, -h), vec2<f32>(-w, -h)));
        d = min(d, sd_seg2d(px_iso, vec2<f32>(-w, -h), vec2<f32>( w * 0.12, 0.0)));
        d = min(d, sd_seg2d(px_iso, vec2<f32>( w * 0.12, 0.0), vec2<f32>(-w,  h)));
        d = min(d, sd_seg2d(px_iso, vec2<f32>(-w,  h), vec2<f32>( w,  h)));
        d = min(d, sd_seg2d(px_iso, vec2<f32>( w,  h), vec2<f32>( w,  h * 0.74)));
        return d;
    }

    // kind == 1u (SYM_GLYPH) or 6u (SUP_SUB): strictly proportional to sy so Wave64 never clips
    let gh = max(sy * select(1.52, 1.82, kind == 6u), 1e-4);
    let uv = px_iso / gh;
    return eval_latex_symbol_sdf(uv, cp) * gh;
}

// ----------------------------------------------------------------------------
// 4. Inline UI Labels (Slots 250..253) & UTF-8 / CJK IME TextInput (Slots 5..6)
// ----------------------------------------------------------------------------

fn resolve_ui_label(node_idx : u32, prim : u32, hsz_x : f32, action_op : u32, studio_gi : bool) -> vec4<u32> {
    let raw = get_ui_label_vec4(node_idx);
    if (unpack_byte16(raw, 0u) != 0u) {
        return raw;
    }
    if (!studio_gi) {
        return vec4<u32>(0u);
    }
    if (prim == 1u && hsz_x > 0.5) {
        // "BLOOM STUDIO"
        return vec4<u32>(0x4F4F4C42u, 0x5453204Du, 0x4F494455u, 0u);
    }
    switch (action_op) {
        case 1u: { return vec4<u32>(0x41522D58u, 0x00000059u, 0u, 0u); } // "X-RAY"
        case 2u: { return vec4<u32>(0x45564552u, 0x00005452u, 0u, 0u); } // "REVERT"
        case 3u: { return vec4<u32>(0x5454414Du, 0x00005245u, 0u, 0u); } // "MATTER"
        case 4u: { return vec4<u32>(0x50415753u, 0u, 0u, 0u); }          // "SWAP"
        case 5u: { return vec4<u32>(0x00435254u, 0u, 0u, 0u); }          // "TRC"
        default: { return vec4<u32>(0u); }
    }
}

fn label_length16(lbl : vec4<u32>) -> u32 {
    for (var k : u32 = 0u; k < 16u; k = k + 1u) {
        if (unpack_byte16(lbl, k) == 0u) {
            return k;
        }
    }
    return 16u;
}

fn eval_label_sdf_at(
    norm_xy : vec2<f32>,
    text_x0 : f32,
    text_cy : f32,
    gh : f32,
    aspect : f32,
    lbl : vec4<u32>,
    len : u32
) -> f32 {
    if (len == 0u || abs(norm_xy.y - text_cy) > gh * 0.72) {
        return 1e5;
    }
    let pitch_x = (gh * 0.96) / aspect;
    let rel_x = norm_xy.x - text_x0;
    if (rel_x < -pitch_x * 0.25) {
        return 1e5;
    }
    let col_i = i32(floor(rel_x / pitch_x));
    if (col_i < 0 || u32(col_i) >= len) {
        return 1e5;
    }
    let ch = unpack_byte16(lbl, u32(col_i));
    if (ch <= 32u) {
        return 1e5;
    }
    let cx = text_x0 + (f32(col_i) + 0.5) * pitch_x;
    let uv = vec2<f32>((norm_xy.x - cx) * aspect / gh, (norm_xy.y - text_cy) / gh);
    return eval_ascii_glyph_sdf(uv, ch);
}

fn shade_text_input_widget(
    base_col : vec3<f32>,
    norm_xy : vec2<f32>,
    center : vec2<f32>,
    hsz : vec2<f32>,
    sdf : f32,
    fill_c : vec3<f32>,
    hovered : bool,
    aspect : f32,
    fh : f32,
    node_idx : u32
) -> vec3<f32> {
    let ti = arena[5];
    let byte_len = min(ti.q2.z & 0xFFu, 39u);
    let cursor_b = min((ti.q2.z >> 8u) & 0xFFu, byte_len);
    let focused  = ((ti.q2.z >> 24u) & 0xFFu) != 0u;

    let ime = arena[6];
    let pre_len_raw = min(ime.q0.y & 0xFFu, 35u);
    let ime_active  = (((ime.q0.y >> 24u) & 0xFFu) != 0u) && (pre_len_raw > 0u);
    let pre_len     = select(0u, pre_len_raw, ime_active);

    // Smoked recessed dark obsidian glass well
    let inner_sh = clamp((norm_xy.y - (center.y - hsz.y)) / max(hsz.y * 0.45, 1e-4), 0.0, 1.0);
    var well = base_col * 0.20 + vec3<f32>(0.012, 0.020, 0.036) * (0.65 + 0.35 * inner_sh);
    if (hovered) {
        well = well + fill_c * 0.05;
    }

    let gh = clamp(hsz.y * 0.54, 0.012, 0.024);
    let pitch_x = (gh * 0.96) / aspect;
    let pad_x = max(0.024, pitch_x * 0.55);
    let prompt_cx = center.x - hsz.x + pad_x + 0.5 * pitch_x;
    let text_x0   = prompt_cx + 0.95 * pitch_x;
    let max_cols  = u32(clamp(floor(((center.x + hsz.x - pad_x) - text_x0) / max(pitch_x, 1e-4)), 1.0, 48.0));

    let px_uv = 2.0 / max(fh * gh, 1.0);
    let stroke_uv = max(0.088, px_uv * 0.65);

    // '>' Command Prompt glyph
    if (abs(norm_xy.x - prompt_cx) < pitch_x * 0.65 && abs(norm_xy.y - center.y) < gh * 0.72) {
        let p_uv = vec2<f32>((norm_xy.x - prompt_cx) * aspect / gh, (norm_xy.y - center.y) / gh);
        let d_pr = eval_ascii_glyph_sdf(p_uv, 62u);
        let a_pr = 1.0 - smoothstep(stroke_uv - px_uv * 0.65, stroke_uv + px_uv * 0.65, d_pr);
        let pr_col = select(vec3<f32>(0.25, 0.68, 0.85), vec3<f32>(0.20, 0.96, 1.00), focused);
        well = mix(well, pr_col, a_pr * 0.92);
    }

    // Scan UTF-8 prefix [0..cursor_b) + IME preedit [0..pre_len) + UTF-8 suffix [cursor_b..byte_len)
    let rel_x = norm_xy.x - text_x0;
    let target_col = i32(floor(rel_x / pitch_x));

    var cur_col : u32 = 0u;
    var caret_col : u32 = 0u;
    var pre_col_start : u32 = 0u;
    var pre_col_end : u32 = 0u;
    var hit_code : u32 = 0u;
    var hit_is_cjk : bool = false;
    var hit_is_preedit : bool = false;

    // 1) Prefix bytes [0 .. cursor_b)
    var bi : u32 = 0u;
    for (var step : u32 = 0u; step < 40u; step = step + 1u) {
        if (bi >= cursor_b || cur_col >= max_cols) { break; }
        let b0 = unpack_text_input_byte(bi);
        if (b0 == 0u) { break; }
        var adv : u32 = 1u;
        var is_cjk = false;
        var code = b0;
        if (b0 >= 0xC0u) {
            is_cjk = true;
            let b1 = select(0u, unpack_text_input_byte(bi + 1u), bi + 1u < byte_len);
            code = ((b0 & 0x1Fu) << 6u) ^ (b1 & 0x3Fu);
            adv = select(2u, select(3u, 4u, b0 >= 0xF0u), b0 >= 0xE0u);
        } else if ((b0 & 0xC0u) == 0x80u) {
            bi = bi + 1u;
            continue;
        }
        if (i32(cur_col) == target_col) {
            hit_code = code;
            hit_is_cjk = is_cjk;
            hit_is_preedit = false;
        }
        cur_col = cur_col + 1u;
        bi = bi + adv;
    }

    pre_col_start = cur_col;

    // 2) Active IME Preedit bytes [0 .. pre_len)
    if (pre_len > 0u) {
        var pi : u32 = 0u;
        for (var step : u32 = 0u; step < 36u; step = step + 1u) {
            if (pi >= pre_len || cur_col >= max_cols) { break; }
            let b0 = unpack_ime_preedit_byte(pi);
            if (b0 == 0u) { break; }
            var adv : u32 = 1u;
            var is_cjk = false;
            var code = b0;
            if (b0 >= 0xC0u) {
                is_cjk = true;
                let b1 = select(0u, unpack_ime_preedit_byte(pi + 1u), pi + 1u < pre_len);
                code = ((b0 & 0x1Fu) << 6u) ^ (b1 & 0x3Fu);
                adv = select(2u, select(3u, 4u, b0 >= 0xF0u), b0 >= 0xE0u);
            } else if ((b0 & 0xC0u) == 0x80u) {
                pi = pi + 1u;
                continue;
            }
            if (i32(cur_col) == target_col) {
                hit_code = code;
                hit_is_cjk = is_cjk;
                hit_is_preedit = true;
            }
            cur_col = cur_col + 1u;
            pi = pi + adv;
        }
    }

    pre_col_end = cur_col;
    caret_col = cur_col;

    // 3) Suffix bytes [cursor_b .. byte_len)
    bi = cursor_b;
    for (var step : u32 = 0u; step < 40u; step = step + 1u) {
        if (bi >= byte_len || cur_col >= max_cols) { break; }
        let b0 = unpack_text_input_byte(bi);
        if (b0 == 0u) { break; }
        var adv : u32 = 1u;
        var is_cjk = false;
        var code = b0;
        if (b0 >= 0xC0u) {
            is_cjk = true;
            let b1 = select(0u, unpack_text_input_byte(bi + 1u), bi + 1u < byte_len);
            code = ((b0 & 0x1Fu) << 6u) ^ (b1 & 0x3Fu);
            adv = select(2u, select(3u, 4u, b0 >= 0xF0u), b0 >= 0xE0u);
        } else if ((b0 & 0xC0u) == 0x80u) {
            bi = bi + 1u;
            continue;
        }
        if (i32(cur_col) == target_col) {
            hit_code = code;
            hit_is_cjk = is_cjk;
            hit_is_preedit = false;
        }
        cur_col = cur_col + 1u;
        bi = bi + adv;
    }

    // Render either active UTF-8/IME glyph OR dimmed placeholder label when empty
    if (byte_len == 0u && pre_len == 0u) {
        var ph = get_ui_label_vec4(node_idx);
        if (unpack_byte16(ph, 0u) == 0u) {
            ph = vec4<u32>(0x45505954u, 0x444D4320u, 0u, 0u); // "TYPE CMD"
        }
        let ph_len = label_length16(ph);
        let d_ph = eval_label_sdf_at(norm_xy, text_x0 + pitch_x * 0.22, center.y, gh * 0.88, aspect, ph, ph_len);
        if (d_ph < stroke_uv + px_uv) {
            let a_ph = 1.0 - smoothstep(stroke_uv - px_uv * 0.65, stroke_uv + px_uv * 0.65, d_ph);
            well = mix(well, vec3<f32>(0.36, 0.56, 0.72), a_ph * 0.58);
        }
    } else if (hit_code != 0u && target_col >= 0 && abs(norm_xy.y - center.y) < gh * 0.72) {
        let cx = text_x0 + (f32(target_col) + 0.5) * pitch_x;
        let uv = vec2<f32>((norm_xy.x - cx) * aspect / gh, (norm_xy.y - center.y) / gh);
        let dg = select(eval_ascii_glyph_sdf(uv, hit_code), eval_cjk_glyph_sdf(uv, hit_code), hit_is_cjk);
        if (dg < stroke_uv + px_uv) {
            let ag = 1.0 - smoothstep(stroke_uv - px_uv * 0.65, stroke_uv + px_uv * 0.65, dg);
            let gcol = select(vec3<f32>(0.92, 0.98, 1.00), vec3<f32>(0.32, 0.98, 0.92), hit_is_preedit);
            well = mix(well, gcol, ag * 0.96);
        }
    }

    // Glowing Cyan IME Preedit Composition Underline
    if (pre_col_end > pre_col_start) {
        let ux0 = text_x0 + f32(pre_col_start) * pitch_x;
        let ux1 = text_x0 + f32(pre_col_end) * pitch_x;
        let uy  = center.y + gh * 0.64;
        let d_ul = sd_seg2d(
            vec2<f32>(norm_xy.x * aspect, norm_xy.y),
            vec2<f32>(ux0 * aspect, uy),
            vec2<f32>(ux1 * aspect, uy)
        );
        let ul_w = max(2.2 / fh, 0.0024);
        if (d_ul < ul_w * 2.5) {
            let a_ul = 1.0 - smoothstep(ul_w * 0.4, ul_w * 1.8, d_ul);
            well = mix(well, vec3<f32>(0.15, 0.98, 0.92), a_ul * 0.95);
        }
    }

    // Crisp Vertical Caret Bar when focused
    if (focused && caret_col <= max_cols) {
        let caret_x = text_x0 + f32(caret_col) * pitch_x + 0.04 * pitch_x;
        let d_cx = abs(norm_xy.x - caret_x) * aspect;
        let d_cy = abs(norm_xy.y - center.y);
        let cw = max(1.8 / fh, 0.0020);
        if (d_cx < cw * 2.2 && d_cy < gh * 0.68) {
            let pulse = 0.80 + 0.20 * cos(f32(arena[0].q1.y) * 0.12);
            let a_c = (1.0 - smoothstep(cw * 0.45, cw * 1.65, d_cx)) * pulse;
            well = mix(well, vec3<f32>(0.22, 0.98, 1.00), a_c);
        }
    }

    var out_col = mix(base_col, well, select(0.0, 0.92, sdf < 0.0));
    let rim_w = max(2.2 / fh, 0.0028);
    let rim = 1.0 - smoothstep(0.0, rim_w, abs(sdf));
    let border_c = select(fill_c * 0.75, mix(vec3<f32>(0.18, 0.95, 1.00), vec3<f32>(1.00, 0.80, 0.28), 0.25), focused || hovered);
    out_col = mix(out_col, border_c, rim * 0.92);
    return out_col;
}

// ----------------------------------------------------------------------------
// 5. Main Cross-Platform Compute Entrypoint
// ----------------------------------------------------------------------------

@compute @workgroup_size(8, 8, 1)
fn bloom_render_frame(@builtin(global_invocation_id) gid : vec3<u32>) {
    let dims = textureDimensions(out_tex);
    if (gid.x >= dims.x || gid.y >= dims.y) { return; }

    let fw = max(f32(dims.x), 1.0);
    let fh = max(f32(dims.y), 1.0);
    let px = vec2<f32>(f32(gid.x) + 0.5, f32(gid.y) + 0.5);
    // Normalized screen space [-1..+1] matching CPU Q16 [-65536..+65536]
    let norm_xy = (px / vec2<f32>(fw, fh)) * 2.0 - vec2<f32>(1.0);

    // 1. Physical Orbit Camera Setup (Exact parity with bloom_scene_raycast64)
    let cam = arena[1];
    let eye = vec3<f32>(q16_to_f32(cam.q0.x), q16_to_f32(cam.q0.y), q16_to_f32(cam.q0.z));
    let tgt = vec3<f32>(q16_to_f32(cam.q0.w), q16_to_f32(cam.q1.x), q16_to_f32(cam.q1.y));
    let focal_mm = clamp(q16_to_f32(cam.q2.y), 18.0, 135.0);
    let fov_z = max(focal_mm / 28.0, 0.7);

    let fwd = safe_norm3(tgt - eye, vec3<f32>(0.0, 0.0, 1.0));
    let right = safe_norm3(vec3<f32>(fwd.z, 0.0, -fwd.x), vec3<f32>(1.0, 0.0, 0.0));
    let up_v = cross(fwd, right);

    let aspect = fw / fh;
    let uv_cam = vec2<f32>(norm_xy.x * aspect * 0.5, -norm_xy.y * 0.5);
    let rd = safe_norm3(right * uv_cam.x + up_v * uv_cam.y + fwd * fov_z, vec3<f32>(0.0, 0.0, 1.0));
    let px_ang = 1.0 / max(fh * fov_z, 1.0);

    let hdr_flags   = arena[0].q3.z;
    let xray_on     = (hdr_flags & 2u) != 0u;
    let trc_safe    = (hdr_flags & 4u) != 0u;
    let cb_geom     = (hdr_flags & 8u) != 0u;
    let studio_gi   = (hdr_flags & 16u) != 0u;
    let xray_plane  = q16_to_f32(arena[0].q3.x);
    let selected_id = arena[0].q3.y;
    let time_s      = f32(arena[0].q1.y) * 0.0166667;

    // Base background (exact parity when studio_gi == false for verifyGpuAlignment)
    let sky_col = eval_sky_dome(rd);
    var bg = eval_backdrop_ray(eye, rd, px_ang);

    // 2. Primary 3D SDF Raymarch (Exact CPU Q16.16 parity when !studio_gi; 80-step AA in Studio)
    var col = bg;
    var hit_found = false;
    var t : f32 = 0.125;
    let max_steps : u32 = select(64u, 80u, studio_gi);
    let hit_eps   : f32 = select(0.0078125, 0.0020, studio_gi);
    let min_step  : f32 = select(0.0078125, 0.0030, studio_gi);
    let max_t     : f32 = select(32.0, 28.0, studio_gi);
    var min_edge_d : f32 = 1e5;
    var edge_p     : vec3<f32> = eye;

    for (var s : u32 = 0u; s < max_steps; s = s + 1u) {
        if (t >= max_t) { break; }
        let p = eye + rd * t;
        let d = map_scene_dist(p, xray_on, xray_plane);
        let cone_d = d / max(t, 0.25);
        if (cone_d < min_edge_d) {
            min_edge_d = cone_d;
            edge_p = p;
        }
        if (d < hit_eps) {
            let hit_info = map_scene_full(p, xray_on, xray_plane);
            let n = calc_normal(p, xray_on, xray_plane);
            col = shade_full_hit(
                hit_info, p, n, rd, px_ang,
                xray_on, xray_plane, trc_safe, cb_geom, selected_id, time_s
            );
            hit_found = true;
            break;
        }
        t = t + max(d, min_step);
    }

    // Studio Floor GI: Contact AO + Soft Shadows + Glossy Planar Reflection + Selection Ring
    // Strictly gated by studio_gi so verifyGpuAlignment (studio_gi == false) keeps exact bg outside silhouettes!
    if (!hit_found && studio_gi && rd.y < -0.001) {
        let tf = (-1.25 - eye.y) / rd.y;
        if (tf > 0.0 && tf < 28.0) {
            let fp = eye + rd * tf;
            var fcol = eval_floor_grid(fp, tf, abs(rd.y), px_ang);

            // Key-light ground soft shadow + 2-tap ambient proximity occlusion
            let key_dir = vec3<f32>(-0.5145, 0.7503, -0.4150);
            let sh_f = calc_soft_shadow(fp + vec3<f32>(0.0, 0.018, 0.0), key_dir, 8.5, xray_on, xray_plane);
            let prox1 = clamp(map_scene_dist(fp + vec3<f32>(0.0, 0.12, 0.0), xray_on, xray_plane) / 0.16, 0.0, 1.0);
            let prox2 = clamp(map_scene_dist(fp + vec3<f32>(0.0, 0.30, 0.0), xray_on, xray_plane) / 0.38, 0.0, 1.0);
            let ao_f = clamp(min(prox1, prox2), 0.16, 1.0);
            fcol = fcol * (0.36 + 0.64 * sh_f) * (0.38 + 0.62 * ao_f);

            // 24-Step Glossy Planar Reflection Ray
            let refl_rd = reflect(rd, vec3<f32>(0.0, 1.0, 0.0));
            let refl_ro = fp + vec3<f32>(0.0, 0.016, 0.0);
            var tr : f32 = 0.05;
            var refl_col = eval_sky_dome(refl_rd) * 0.85;
            for (var rs : u32 = 0u; rs < 24u; rs = rs + 1u) {
                if (tr > 11.0) { break; }
                let rp = refl_ro + refl_rd * tr;
                let rd_s = map_scene_dist(rp, xray_on, xray_plane);
                if (rd_s < 0.006) {
                    let rhit = map_scene_full(rp, xray_on, xray_plane);
                    let rn = calc_normal(rp, xray_on, xray_plane);
                    let lit = shade_direct_hit(rhit, rp, rn, refl_rd, trc_safe, time_s);
                    refl_col = mix(refl_col, lit, exp(-tr * 0.16));
                    break;
                }
                tr = tr + max(rd_s, 0.035);
            }
            let fres_f = clamp(0.16 + 0.20 * pow(1.0 - clamp(abs(rd.y), 0.0, 1.0), 4.0), 0.16, 0.32);
            fcol = mix(fcol, refl_col, fres_f * exp(-tf * 0.06));

            // 3D Selected Instance Floor Orbital Target Ring
            let inst_cnt = min(arena[0].q1.z, 32u);
            if (selected_id >= 1u && selected_id <= inst_cnt) {
                let s_inst = arena[40u + (selected_id - 1u)];
                let s_flags = (s_inst.q3.x >> 16u) & 0xFFFFu;
                if ((s_flags & 1u) != 0u) {
                    let s_xz = vec2<f32>(q16_to_f32(s_inst.q1.x), q16_to_f32(s_inst.q1.z));
                    let s_raw_sc = q16_to_f32(s_inst.q2.z);
                    let s_sc = select(1.0, clamp(s_raw_sc, 0.35, 2.5), s_raw_sc > 0.0625);
                    let rel_xz = fp.xz - s_xz;
                    let r_sel = length(rel_xz);
                    let r_inner = 1.04 * s_sc;
                    let r_outer = 1.22 * s_sc;
                    let ring_aa = max((tf * px_ang) / max(abs(rd.y), 0.08), 0.010);

                    let d_in = abs(r_sel - r_inner);
                    let a_in = 1.0 - smoothstep(ring_aa * 0.4, ring_aa * 1.8, d_in);

                    let ang = atan2(rel_xz.y, rel_xz.x);
                    let dash = smoothstep(-0.15, 0.25, cos(ang * 8.0));
                    let d_out = abs(r_sel - r_outer);
                    let a_out = (1.0 - smoothstep(ring_aa * 0.4, ring_aa * 1.6, d_out)) * dash;

                    let halo = exp(-abs(r_sel - r_inner) * 5.5) * 0.18;
                    fcol = fcol
                         + vec3<f32>(0.16, 0.95, 1.00) * (a_in * 0.88 + halo)
                         + vec3<f32>(1.00, 0.82, 0.30) * (a_out * 0.65);
                }
            }

            col = mix(sky_col, fcol, exp(-tf * 0.065));
        }
    }

    // Subpixel Cone-Traced Silhouette Edge Antialiasing (Studio GI mode only)
    if (!hit_found && studio_gi && min_edge_d < px_ang * 1.35) {
        let edge_cov = 1.0 - smoothstep(0.0, px_ang * 1.35, min_edge_d);
        let ehit = map_scene_full(edge_p, xray_on, xray_plane);
        if (ehit.inst_id != 0u) {
            let en = calc_normal(edge_p, xray_on, xray_plane);
            let elit = shade_direct_hit(ehit, edge_p, en, rd, trc_safe, time_s);
            col = mix(col, elit, edge_cov * 0.72);
        }
    }

    // 3. Wave64 (u32x2) Culled 64-Byte SVG-UI + Liquid Glass + Inline Labels + TextInput/IME
    let ui_cnt = min(arena[0].q2.y, 16u);
    let latex_cnt = min(arena[0].q2.w, 16u);
    let wc = u32(clamp((norm_xy.x + 1.0) * 4.0, 0.0, 7.0));
    let wr = u32(clamp((norm_xy.y + 1.0) * 4.0, 0.0, 7.0));
    let bit_idx = wr * 8u + wc;

    let dom_lo = arena[3].q3.x;
    let dom_hi = arena[3].q3.y;

    var has_topleft_logo = false;
    if (arena[0].q2.z > 0u && arena[168u].q0.y > 0u) {
        let logo_p = unpack_q8_pair(arena[168u].q1.x);
        has_topleft_logo = (logo_p.x < -0.80) && (logo_p.y < -0.75);
    }

    if (wave64_test_u32x2(dom_lo, dom_hi, bit_idx)) {
        for (var i : u32 = 0u; i < ui_cnt; i = i + 1u) {
            let nd = arena[152u + i];
            if (!wave64_test_u32x2(nd.q3.z, nd.q3.w, bit_idx)) { continue; }

            let nid = nd.q0.x;
            let prim = nd.q0.y;
            let center = vec2<f32>(q16_to_f32(nd.q1.x), q16_to_f32(nd.q1.y));
            let scale_v = clamp(q16_to_f32(nd.q2.y), 0.75, 1.35);
            let hsz = vec2<f32>(q16_to_f32(nd.q1.z), q16_to_f32(nd.q1.w)) * scale_v;
            let lp = norm_xy - center;

            if (abs(lp.x) > hsz.x + 0.02 || abs(lp.y) > hsz.y + 0.02) { continue; }

            let style_st = nd.q3.x;
            let action_op = nd.q3.y & 0xFFFFu;
            let hovered = (style_st & 0x10u) != 0u;
            let fill_c = unpack_rgb24((style_st >> 8u) & 0xFFFFFFu);
            let bound_v = clamp(q16_to_f32(nd.q2.w), 0.0, 1.0);
            let jet_d1 = clamp(abs(q16_to_f32(nd.q2.z)) * 4.0, 0.0, 1.2);

            let is_text_input = (prim == 7u) || (action_op == 6u);

            if (prim == 2u) {
                // Telemetry Ring Gauge (aspect-corrected circular arc)
                let lp_iso = vec2<f32>(lp.x * aspect, lp.y);
                let r_outer = hsz.x * aspect;
                let r_inner = hsz.y * aspect;
                let r_mid = 0.5 * (r_outer + r_inner);
                let thick = max(0.5 * (r_outer - r_inner), 0.0045);
                let r = length(lp_iso);
                let d_ring = abs(r - r_mid) - thick;
                let aa = 2.0 / fh;
                if (r < r_inner) {
                    col = mix(col, vec3<f32>(0.015, 0.024, 0.042), 0.55);
                }
                if (d_ring < aa * 1.5) {
                    let ang = fract(atan2(lp_iso.y, lp_iso.x) * (1.0 / 6.2831853) + 0.25);
                    let rc = select(fill_c * 0.20, fill_c * 1.18, ang <= bound_v);
                    let a = 1.0 - smoothstep(-aa * 0.5, aa * 1.2, d_ring);
                    col = mix(col, rc, a * 0.94);
                }
            } else {
                let rx = q16_to_f32(nd.q2.x);
                let qd = abs(lp) - max(hsz - vec2<f32>(rx), vec2<f32>(0.001));
                let sdf = min(max(qd.x, qd.y), 0.0) + length(max(qd, vec2<f32>(0.0))) - rx;

                if (sdf < 0.0035) {
                    if (is_text_input) {
                        col = shade_text_input_widget(col, norm_xy, center, hsz, sdf, fill_c, hovered, aspect, fh, i);
                    } else if (prim == 5u) {
                        // Smoked Crystal Backing Card for Analytic SDF LaTeX Formula
                        let bevel = 1.0 - clamp(-sdf * 130.0, 0.0, 1.0);
                        let card = col * 0.30 + vec3<f32>(0.016, 0.022, 0.038) * 0.70 + fill_c * (0.07 + 0.14 * bevel);
                        col = mix(col, card, select(0.0, 0.88, sdf < 0.0));
                        let rim = 1.0 - smoothstep(0.0, 0.003, abs(sdf));
                        col = mix(col, fill_c * 1.05, rim * 0.82);
                    } else {
                        let bevel = 1.0 - clamp(-sdf * 140.0, 0.0, 1.0);
                        let top_sheen = clamp((center.y - norm_xy.y) / max(hsz.y, 1e-4), 0.0, 1.0) * 0.08;
                        let ripple = (0.5 + 0.5 * sin(length(lp) * 95.0 - time_s * 9.0)) * jet_d1 * 0.32;
                        let trans = clamp(vec3<f32>(1.0) - (vec3<f32>(1.0) - fill_c) * (0.22 + 0.52 * bevel), vec3<f32>(0.16), vec3<f32>(1.0));
                        var glass = col * trans * 0.50 + fill_c * (0.27 + 0.24 * bevel + top_sheen + ripple + select(0.0, 0.12, hovered));

                        // Slider Bar Fill + Thumb Indicator
                        if (prim == 4u && sdf < -0.0015) {
                            let prog = (lp.x + hsz.x) / max(2.0 * hsz.x, 1e-4);
                            if (prog <= bound_v) {
                                glass = mix(glass, fill_c * 0.62 + vec3<f32>(0.04, 0.12, 0.14), 0.68);
                            }
                            let thumb_x = mix(-hsz.x + 0.012, hsz.x - 0.012, bound_v);
                            let d_thumb = abs(lp.x - thumb_x) * aspect;
                            if (d_thumb < 0.008 && abs(lp.y) < hsz.y * 0.72) {
                                let a_th = 1.0 - smoothstep(0.003, 0.008, d_thumb);
                                glass = mix(glass, vec3<f32>(0.92, 1.00, 1.00), a_th * 0.85);
                            }
                        }

                        // 7-Segment Numeric Readout
                        if (prim == 6u && sdf < -0.0015) {
                            glass = glass * 0.36 + vec3<f32>(0.01, 0.02, 0.03);
                            let val = u32(clamp(bitcast<i32>(nd.q2.w), 0, 999));
                            let dp = vec2<f32>(lp.x / max(hsz.x, 1e-4), lp.y / max(hsz.y, 1e-4));
                            let sd7 = min(eval_7seg(vec2<f32>(dp.x * 1.65 + 0.95, dp.y * 1.15), (val / 100u) % 10u),
                                      min(eval_7seg(vec2<f32>(dp.x * 1.65,        dp.y * 1.15), (val / 10u)  % 10u),
                                          eval_7seg(vec2<f32>(dp.x * 1.65 - 0.95, dp.y * 1.15), val % 10u)));
                            if (sd7 < 0.115) {
                                let a7 = 1.0 - smoothstep(0.045, 0.115, sd7);
                                glass = mix(glass, fill_c * 1.45 + vec3<f32>(0.22), a7);
                            }
                        }

                        // Inline Vector UI Node Labels (Slots 250..253 + Studio Action Labels)
                        if ((prim == 1u || prim == 4u) && sdf < -0.0015) {
                            let lbl = resolve_ui_label(i, prim, hsz.x, action_op, studio_gi);
                            let llen = label_length16(lbl);
                            if (llen > 0u) {
                                let gh = clamp(hsz.y * 0.44, 0.011, 0.021);
                                let pitch_x = (gh * 0.96) / aspect;
                                var tx0 = center.x - 0.5 * f32(llen) * pitch_x;
                                if (prim == 1u && hsz.x > 0.5) {
                                    let left_pad = select(0.035, 0.092, has_topleft_logo);
                                    tx0 = center.x - hsz.x + left_pad;
                                } else if (prim == 4u) {
                                    tx0 = center.x - hsz.x + 0.030;
                                }
                                let dl = eval_label_sdf_at(norm_xy, tx0, center.y, gh, aspect, lbl, llen);
                                let px_uv = 2.0 / max(fh * gh, 1.0);
                                let st_uv = max(0.092, px_uv * 0.68);
                                if (dl < st_uv + px_uv * 2.4) {
                                    let shadow_a = 1.0 - smoothstep(st_uv, st_uv + px_uv * 2.2, dl);
                                    glass = mix(glass, vec3<f32>(0.01, 0.02, 0.04), shadow_a * 0.62);
                                    let stroke_a = 1.0 - smoothstep(st_uv - px_uv * 0.65, st_uv + px_uv * 0.65, dl);
                                    let txt_col = mix(vec3<f32>(0.94, 0.98, 1.00), fill_c + vec3<f32>(0.55), 0.22);
                                    glass = mix(glass, clamp(txt_col, vec3<f32>(0.0), vec3<f32>(1.15)), stroke_a * 0.96);
                                }
                            }
                        }

                        col = mix(col, glass, select(0.0, select(0.85, 0.91, hovered), sdf < 0.0));
                        let rim = 1.0 - smoothstep(0.0, 0.003, abs(sdf));
                        col = mix(col, fill_c * select(1.08, 1.42, hovered), rim * 0.86);
                    }
                }
            }
        }
    }

    // 4. Vector <path>/<line> SDF Icon Glyphs (Slots 168..183)
    let glyph_cnt = min(arena[0].q2.z, 16u);
    for (var g : u32 = 0u; g < glyph_cnt; g = g + 1u) {
        let gl = arena[168u + g];
        let scnt = min(gl.q0.y, 5u);
        let gw = clamp(q16_to_f32(gl.q0.z) * 0.09, 0.0018, 0.009);
        let gc = unpack_rgb24(gl.q0.w);
        let words = array<u32, 10>(
            gl.q1.x, gl.q1.y, gl.q1.z, gl.q1.w,
            gl.q2.x, gl.q2.y, gl.q2.z, gl.q2.w,
            gl.q3.x, gl.q3.y
        );
        let p_iso = vec2<f32>(norm_xy.x * aspect, norm_xy.y);
        var dg : f32 = 1e5;
        for (var s : u32 = 0u; s < scnt; s = s + 1u) {
            let a_raw = unpack_q8_pair(words[s * 2u + 0u]);
            let b_raw = unpack_q8_pair(words[s * 2u + 1u]);
            let a_iso = vec2<f32>(a_raw.x * aspect, a_raw.y);
            let b_iso = vec2<f32>(b_raw.x * aspect, b_raw.y);
            dg = min(dg, sd_seg2d(p_iso, a_iso, b_iso) - gw);
        }
        let aa_g = 2.0 / fh;
        if (dg < aa_g * 1.4) {
            col = mix(col, gc * 1.12, (1.0 - smoothstep(-aa_g * 0.5, aa_g * 1.2, dg)) * 0.88);
        }
    }

    // 5. Zero-Font-Atlas Analytic SDF LaTeX Formula Overlay (Slots 184..199)
    for (var i : u32 = 0u; i < latex_cnt; i = i + 1u) {
        let lbox = arena[184u + i];
        if (!wave64_test_u32x2(lbox.q3.x, lbox.q3.y, bit_idx)) { continue; }
        let bc = vec2<f32>(q16_to_f32(lbox.q1.x), q16_to_f32(lbox.q1.y));
        let bsz = vec2<f32>(q16_to_f32(lbox.q1.z), q16_to_f32(lbox.q1.w));
        let lp = norm_xy - bc;
        if (abs(lp.x) > bsz.x + 0.022 || abs(lp.y) > bsz.y * 1.45 + 0.022) { continue; }

        let kind = lbox.q0.y;
        let cp   = lbox.q0.z;
        let raw_stroke = q16_to_f32(lbox.q2.x);
        let aa_l = 2.0 / fh;
        let max_sw = select(bsz.y * 0.24, max(bsz.y * 0.30, aa_l * 0.85), kind == 2u || kind == 6u);
        let sw = clamp(raw_stroke * 0.48, max(aa_l * 0.62, 0.0016), max(max_sw, 0.0022));

        let px_iso = vec2<f32>(lp.x * aspect, lp.y);
        let d_raw = eval_latex_box_iso(px_iso, bsz, aspect, kind, cp);
        let dl = d_raw - sw;
        if (dl < aa_l * 1.6) {
            let tc = unpack_rgb24(lbox.q0.w);
            let alpha_l = 1.0 - smoothstep(-aa_l * 0.55, aa_l * 1.25, dl);
            col = mix(col, tc * 1.15 + vec3<f32>(0.08, 0.06, 0.02), alpha_l * 0.96);
        }
    }

    // ACES Filmic Tonemap + sRGB Display Transfer
    col = aces_tonemap(col);
    col = pow(max(col, vec3<f32>(1e-6)), vec3<f32>(1.0 / 2.2));
    textureStore(out_tex, vec2<i32>(gid.xy), vec4<f32>(col, 1.0));
}
