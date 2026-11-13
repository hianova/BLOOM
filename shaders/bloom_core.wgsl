// ============================================================================
// BLOOM v6.0.0 — Unified Single-Source Cross-Platform Compute Shader (.wgsl)
// ============================================================================
// Single source of truth over the 16,384-byte (256 x 64B) BloomEngineArena.
// Compiled at build time via W3C Naga into both:
//   - build/bloom_core.metal (MSL 2.4 for Apple Metal via SDL3 GPU)
//   - build/bloom_core.spv   (SPIR-V for Vulkan / Lavapipe via SDL3 GPU)
// Zero shader-int64 extensions required: Wave64 masks use u32x2 (lo32, hi32).
// ============================================================================

struct Cell64 {
    q0 : vec4<u32>,
    q1 : vec4<u32>,
    q2 : vec4<u32>,
    q3 : vec4<u32>,
};

@group(0) @binding(0) var<storage, read> arena : array<Cell64, 256>;
@group(1) @binding(0) var out_tex : texture_storage_2d<rgba8unorm, write>;

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

fn map_scene(p : vec3<f32>, xray_on : bool, xray_plane : f32) -> vec4<f32> {
    /* Returns (sdf_dist, inst_id, matter_id, cut_face_flag) */
    let inst_cnt = min(arena[0].q1.z, 32u);
    var best_d : f32 = 1e5;
    var best_inst : f32 = 0.0;
    var best_mat : f32 = 0.0;
    var best_cut : f32 = 0.0;

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
        var cut_flag : f32 = 0.0;
        if (xray_on) {
            let clip_d = xray_plane - p.x;
            if (clip_d > d) {
                d = clip_d;
                cut_flag = 1.0;
            }
        }
        if (d < best_d) {
            best_d = d;
            best_inst = f32(inst.q0.x);
            let mat_ovr = inst.q0.w;
            best_mat = f32(select(pf.q0.z & 7u, mat_ovr, mat_ovr > 0u && mat_ovr < 8u));
            best_cut = cut_flag;
        }
    }
    return vec4<f32>(best_d, best_inst, best_mat, best_cut);
}

fn eval_matter_shade(m : Cell64, n : vec3<f32>, rd : vec3<f32>, ldir : vec3<f32>, bg : vec3<f32>, is_cut : bool) -> vec3<f32> {
    let eta   = clamp(q16_to_f32(m.q0.y), 1.0, 4.0);
    let kappa = clamp(q16_to_f32(m.q0.z), 0.0, 8.0);
    let rough = clamp(q16_to_f32(m.q1.y), 0.02, 1.0);
    let mie   = clamp(q16_to_f32(m.q1.z), 0.0, 1.0);
    let mu_a  = vec3<f32>(q16_to_f32(m.q1.w), q16_to_f32(m.q2.x), q16_to_f32(m.q2.y));
    let d_mm  = clamp(q16_to_f32(m.q2.w) * 0.125, 0.08, 3.0);
    let kelvin = m.q3.x;

    let cos_t = clamp(dot(n, -rd), 0.0, 1.0);
    let f0 = ((eta - 1.0) * (eta - 1.0) + kappa * kappa) /
             max((eta + 1.0) * (eta + 1.0) + kappa * kappa, 1e-4);
    let fres = f0 + (1.0 - f0) * pow(max(1.0 - cos_t, 1e-6), 5.0);
    let skin_t = (1.0 - fres) * exp(-kappa * 1.8);

    let beer = exp(-mu_a * d_mm);
    let ndl = clamp(dot(n, ldir), 0.0, 1.0);
    let h = safe_norm3(ldir - rd, vec3<f32>(0.0, 1.0, 0.0));
    let spec = pow(max(clamp(dot(n, h), 0.0, 1.0), 1e-6), mix(96.0, 8.0, rough));

    let refr_bg = bg * beer * (0.65 + 0.55 * ndl) + beer * (mie * 0.42);
    let metal_tint = mix(vec3<f32>(0.92, 0.95, 1.0), vec3<f32>(1.0, 0.84, 0.42), clamp(mu_a.z * 0.8, 0.0, 1.0));
    var col = refr_bg * skin_t + metal_tint * (fres * (0.45 + 0.55 * ndl) + spec * (0.45 + 0.55 * f0));

    if (kelvin > 650u) {
        let ex = clamp(f32(kelvin - 650u) * 0.001, 0.0, 2.0);
        col = col + vec3<f32>(ex * 1.35, ex * ex * 0.85, max(0.0, ex - 0.45) * 0.75);
    }
    if (is_cut) {
        col = mix(col, vec3<f32>(0.0, 0.95, 0.82), 0.55);
    }
    return col;
}

fn sd_seg2d(p : vec2<f32>, a : vec2<f32>, b : vec2<f32>) -> f32 {
    let pa = p - a;
    let ba = b - a;
    let h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6), 0.0, 1.0);
    return length(pa - ba * h);
}

fn eval_7seg(p : vec2<f32>, d : u32) -> f32 {
    /* 7-segment display in [-0.45..0.45] x [-0.75..0.75] */
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

@compute @workgroup_size(8, 8, 1)
fn bloom_render_frame(@builtin(global_invocation_id) gid : vec3<u32>) {
    let dims = textureDimensions(out_tex);
    if (gid.x >= dims.x || gid.y >= dims.y) { return; }

    let fw = f32(dims.x);
    let fh = f32(dims.y);
    let px = vec2<f32>(f32(gid.x) + 0.5, f32(gid.y) + 0.5);
    /* Normalized screen space [-1..+1] matching CPU Q16 [-65536..+65536] */
    let norm_xy = (px / vec2<f32>(fw, fh)) * 2.0 - vec2<f32>(1.0);

    /* 1. Camera & Studio Background */
    let cam = arena[1];
    let eye = vec3<f32>(q16_to_f32(cam.q0.x), q16_to_f32(cam.q0.y), q16_to_f32(cam.q0.z));
    let tgt = vec3<f32>(q16_to_f32(cam.q0.w), q16_to_f32(cam.q1.x), q16_to_f32(cam.q1.y));
    let focal_mm = clamp(q16_to_f32(cam.q2.y), 18.0, 135.0);
    let fov_z = max(focal_mm / 28.0, 0.7);

    let fwd = safe_norm3(tgt - eye, vec3<f32>(0.0, 0.0, 1.0));
    let right = safe_norm3(vec3<f32>(fwd.z, 0.0, -fwd.x), vec3<f32>(1.0, 0.0, 0.0));
    let up_v = cross(fwd, right);

    let aspect = fw / max(fh, 1.0);
    let uv_cam = vec2<f32>(norm_xy.x * aspect * 0.5, -norm_xy.y * 0.5);
    let rd = safe_norm3(right * uv_cam.x + up_v * uv_cam.y + fwd * fov_z, vec3<f32>(0.0, 0.0, 1.0));

    /* Gradient sky + analytic ground grid */
    let sky_t = clamp(rd.y * 0.5 + 0.5, 0.0, 1.0);
    var bg = mix(vec3<f32>(0.045, 0.062, 0.105), vec3<f32>(0.012, 0.020, 0.042), sky_t);
    if (rd.y < -0.01) {
        let tg = (-1.35 - eye.y) / rd.y;
        if (tg > 0.0 && tg < 28.0) {
            let gp = eye + rd * tg;
            let grid = abs(fract(gp.xz * 0.75) - vec2<f32>(0.5));
            let gline = 1.0 - smoothstep(0.012, 0.035, min(grid.x, grid.y));
            let fade = exp(-tg * 0.11);
            bg = mix(bg, vec3<f32>(0.08, 0.32, 0.46), gline * fade * 0.55);
        }
    }

    /* 2. 3D SDF Scene Raymarch */
    let hdr_flags = arena[0].q3.z;
    let xray_on = (hdr_flags & 2u) != 0u;
    let xray_plane = q16_to_f32(arena[0].q3.x);
    let selected_id = arena[0].q3.y;

    var col = bg;
    var t : f32 = 0.12;
    let ldir = safe_norm3(vec3<f32>(0.58, 0.72, -0.38), vec3<f32>(0.0, 1.0, 0.0));
    for (var s : u32 = 0u; s < 56u; s = s + 1u) {
        if (t >= 24.0) { break; }
        let p = eye + rd * t;
        let res = map_scene(p, xray_on, xray_plane);
        if (res.x < 0.0025) {
            let eps = 0.0025;
            let n = safe_norm3(vec3<f32>(
                map_scene(p + vec3<f32>(eps, 0.0, 0.0), xray_on, xray_plane).x - map_scene(p - vec3<f32>(eps, 0.0, 0.0), xray_on, xray_plane).x,
                map_scene(p + vec3<f32>(0.0, eps, 0.0), xray_on, xray_plane).x - map_scene(p - vec3<f32>(0.0, eps, 0.0), xray_on, xray_plane).x,
                map_scene(p + vec3<f32>(0.0, 0.0, eps), xray_on, xray_plane).x - map_scene(p - vec3<f32>(0.0, 0.0, eps), xray_on, xray_plane).x
            ), vec3<f32>(0.0, 1.0, 0.0));
            let mat_idx = u32(res.z) & 7u;
            col = eval_matter_shade(arena[8u + mat_idx], n, rd, ldir, bg, res.w > 0.5);
            if (selected_id != 0u && u32(res.y) == selected_id) {
                let rim = pow(max(1.0 - clamp(dot(n, -rd), 0.0, 1.0), 1e-6), 2.5);
                col = col + vec3<f32>(0.15, 0.92, 1.0) * rim * 0.85;
            }
            break;
        }
        t = t + max(res.x, 0.004);
    }

    /* 3. Wave64 (u32x2) Culled 64-Byte SVG-UI + Liquid Glass + FLOW v6.0.0 2-Jet + Gauges */
    let ui_cnt = min(arena[0].q2.y, 16u);
    let latex_cnt = min(arena[0].q2.w, 16u);
    let wc = u32(clamp((norm_xy.x + 1.0) * 4.0, 0.0, 7.0));
    let wr = u32(clamp((norm_xy.y + 1.0) * 4.0, 0.0, 7.0));
    let bit_idx = wr * 8u + wc;

    let dom_lo = arena[3].q3.x;
    let dom_hi = arena[3].q3.y;
    let time_s = f32(arena[0].q1.y) * 0.0166;

    if (wave64_test_u32x2(dom_lo, dom_hi, bit_idx)) {
        for (var i : u32 = 0u; i < ui_cnt; i = i + 1u) {
            let nd = arena[152u + i];
            if (!wave64_test_u32x2(nd.q3.z, nd.q3.w, bit_idx)) { continue; }

            let prim = nd.q0.y;
            let center = vec2<f32>(q16_to_f32(nd.q1.x), q16_to_f32(nd.q1.y));
            let scale_v = clamp(q16_to_f32(nd.q2.y), 0.75, 1.35);
            let hsz = vec2<f32>(q16_to_f32(nd.q1.z), q16_to_f32(nd.q1.w)) * scale_v;
            let lp = norm_xy - center;

            if (abs(lp.x) > hsz.x + 0.02 || abs(lp.y) > hsz.y + 0.02) { continue; }

            let style_st = nd.q3.x;
            let hovered = (style_st & 0x10u) != 0u;
            let fill_c = unpack_rgb24((style_st >> 8u) & 0xFFFFFFu);
            let bound_v = clamp(q16_to_f32(nd.q2.w), 0.0, 1.0);
            let jet_d1 = clamp(abs(q16_to_f32(nd.q2.z)) * 4.0, 0.0, 1.2);

            if (prim == 2u) {
                let r = length(lp);
                let thick = max((hsz.x - hsz.y) * 0.5, 0.004);
                let d_ring = abs(r - (hsz.x + hsz.y) * 0.5) - thick;
                if (d_ring < 0.003) {
                    let ang = fract(atan2(lp.y, lp.x) * (1.0 / 6.2831853) + 0.25);
                    let rc = select(fill_c * 0.22, fill_c, ang <= bound_v);
                    let a = 1.0 - smoothstep(-0.001, 0.003, d_ring);
                    col = mix(col, rc, a * 0.92);
                }
            } else {
                let rx = q16_to_f32(nd.q2.x);
                let qd = abs(lp) - max(hsz - vec2<f32>(rx), vec2<f32>(0.001));
                let sdf = min(max(qd.x, qd.y), 0.0) + length(max(qd, vec2<f32>(0.0))) - rx;
                if (sdf < 0.003) {
                    let bevel = 1.0 - clamp(-sdf * 140.0, 0.0, 1.0);
                    let ripple = (0.5 + 0.5 * sin(length(lp) * 95.0 - time_s * 9.0)) * jet_d1 * 0.35;
                    let trans = clamp(vec3<f32>(1.0) - (vec3<f32>(1.0) - fill_c) * (0.20 + 0.55 * bevel), vec3<f32>(0.16), vec3<f32>(1.0));
                    var glass = col * trans + fill_c * (0.14 + 0.22 * bevel + ripple + select(0.0, 0.12, hovered));
                    if (prim == 4u && sdf < -0.002) {
                        let prog = (lp.x + hsz.x) / max(2.0 * hsz.x, 1e-4);
                        if (prog <= bound_v) {
                            glass = glass + fill_c * 0.38;
                        }
                    }
                    if (prim == 6u && sdf < -0.002) {
                        let val = u32(clamp(bitcast<i32>(nd.q2.w), 0, 999));
                        let dp = vec2<f32>(lp.x / max(hsz.x, 1e-4), lp.y / max(hsz.y, 1e-4));
                        let sd7 = min(eval_7seg(vec2<f32>(dp.x * 1.65 + 0.95, dp.y * 1.15), (val / 100u) % 10u),
                                  min(eval_7seg(vec2<f32>(dp.x * 1.65,        dp.y * 1.15), (val / 10u)  % 10u),
                                      eval_7seg(vec2<f32>(dp.x * 1.65 - 0.95, dp.y * 1.15), val % 10u)));
                        if (sd7 < 0.11) {
                            glass = mix(glass, fill_c * 1.35, 1.0 - smoothstep(0.03, 0.11, sd7));
                        }
                    }
                    col = mix(col, glass, select(0.0, select(0.76, 0.86, hovered), sdf < 0.0));
                    let rim = 1.0 - smoothstep(0.0, 0.003, abs(sdf));
                    col = mix(col, fill_c * select(1.05, 1.4, hovered), rim * 0.85);
                }
            }
        }
    }

    /* 4. Vector <path>/<line> SDF Icon Glyphs (Slots 168..183) */
    let glyph_cnt = min(arena[0].q2.z, 16u);
    for (var g : u32 = 0u; g < glyph_cnt; g = g + 1u) {
        let gl = arena[168u + g];
        let scnt = min(gl.q0.y, 5u);
        let gw = clamp(q16_to_f32(gl.q0.z) * 0.12, 0.0018, 0.012);
        let gc = unpack_rgb24(gl.q0.w);
        let words = array<u32, 10>(
            gl.q1.x, gl.q1.y, gl.q1.z, gl.q1.w,
            gl.q2.x, gl.q2.y, gl.q2.z, gl.q2.w,
            gl.q3.x, gl.q3.y
        );
        var dg : f32 = 1e5;
        for (var s : u32 = 0u; s < scnt; s = s + 1u) {
            let a = unpack_q8_pair(words[s * 2u + 0u]);
            let b = unpack_q8_pair(words[s * 2u + 1u]);
            dg = min(dg, sd_seg2d(norm_xy, a, b) - gw);
        }
        if (dg < 0.003) {
            col = mix(col, gc, (1.0 - smoothstep(-0.0005, 0.003, dg)) * 0.55);
        }
    }

    /* 5. Zero-Font-Atlas Analytic SDF LaTeX Formula Overlay (Slots 184..199) */
    for (var i : u32 = 0u; i < latex_cnt; i = i + 1u) {
        let lb = arena[184u + i];
        if (!wave64_test_u32x2(lb.q3.x, lb.q3.y, bit_idx)) { continue; }
        let bc = vec2<f32>(q16_to_f32(lb.q1.x), q16_to_f32(lb.q1.y));
        let bsz = vec2<f32>(q16_to_f32(lb.q1.z), q16_to_f32(lb.q1.w));
        let stroke = max(q16_to_f32(lb.q2.x), 0.002);
        let lp = norm_xy - bc;
        if (abs(lp.x) > bsz.x + 0.015 || abs(lp.y) > bsz.y + 0.015) { continue; }

        let kind = lb.q0.y;
        var dl : f32 = 1e5;
        if (kind == 2u) {
            dl = sd_seg2d(lp, vec2<f32>(-bsz.x, 0.0), vec2<f32>(bsz.x, 0.0)) - stroke;
        } else if (kind == 3u) {
            let d1 = sd_seg2d(lp, vec2<f32>(-bsz.x, 0.0), vec2<f32>(-bsz.x * 0.5, bsz.y));
            let d2 = sd_seg2d(lp, vec2<f32>(-bsz.x * 0.5, bsz.y), vec2<f32>(0.0, -bsz.y));
            let d3 = sd_seg2d(lp, vec2<f32>(0.0, -bsz.y), vec2<f32>(bsz.x, -bsz.y));
            dl = min(d1, min(d2, d3)) - stroke;
        } else {
            let q = abs(lp) - bsz;
            dl = abs(length(max(q, vec2<f32>(0.0))) + min(max(q.x, q.y), 0.0)) - stroke;
        }
        if (dl < 0.0025) {
            let tc = unpack_rgb24(lb.q0.w);
            col = mix(col, tc, 1.0 - smoothstep(-0.0005, 0.0025, dl));
        }
    }

    /* ACES-lite Tonemap + sRGB Gamma */
    col = clamp((col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14), vec3<f32>(0.0), vec3<f32>(1.0));
    col = pow(max(col, vec3<f32>(1e-6)), vec3<f32>(1.0 / 2.2));
    textureStore(out_tex, vec2<i32>(gid.xy), vec4<f32>(col, 1.0));
}
