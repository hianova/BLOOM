// ============================================================================
// BLOOM v9.0.0 — Unified Single-Source Cross-Platform Compute Shader (.wgsl)
// ============================================================================
// Single source of truth over the 16,384B (256 x 64B) BloomEngineArena +
// 32,768B (512 x 64B) BloomMeshArena + 3-Pass Radiance Cascades Field Buffer +
// Relativistic Metric Geodesic Raymarching + Concentric Shells + CineCamera.
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
@group(0) @binding(1) var<storage, read> mesh_buf : array<Cell64, 512>;
@group(1) @binding(0) var out_tex : texture_storage_2d<rgba8unorm, write>;
@group(1) @binding(1) var<storage, read_write> rc_buf : array<vec4<f32>, 32768>;

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
// 1. 3D CSG, BVH, DQ-Skeleton, 4-Bit SDF Bricks & Triangle Mesh Kernel
// ----------------------------------------------------------------------------

fn smin_poly(a : f32, b : f32, k : f32) -> f32 {
    if (k <= 1e-4) { return min(a, b); }
    let h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

fn rot_inv_euler(v : vec3<f32>, rot : vec3<f32>) -> vec3<f32> {
    if (abs(rot.x) + abs(rot.y) + abs(rot.z) < 1e-5) {
        return v;
    }
    let sx = sin(rot.x); let cx = cos(rot.x);
    let sy = sin(rot.y); let cy = cos(rot.y);
    let sz = sin(rot.z); let cz = cos(rot.z);

    let x1 = v.x * cy - v.z * sy;
    let z1 = v.x * sy + v.z * cy;
    let y2 = v.y * cx + z1 * sx;
    let z2 = -v.y * sx + z1 * cx;
    let x3 = x1 * cz + y2 * sz;
    let y3 = -x1 * sz + y2 * cz;
    return vec3<f32>(x3, y3, z2);
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

fn eval_csg_single(op : Cell64, p : vec3<f32>) -> f32 {
    let prim_t = op.q0.y;
    if (prim_t == 0u) { return 1e5; }
    let pos = vec3<f32>(q16_to_f32(op.q1.x), q16_to_f32(op.q1.y), q16_to_f32(op.q1.z));
    let ext = vec3<f32>(q16_to_f32(op.q1.w), q16_to_f32(op.q2.x), q16_to_f32(op.q2.y));
    let bev = max(q16_to_f32(op.q2.w), 0.0);
    return sd_prim(prim_t, p - pos, ext, bev);
}

fn eval_csg_program(start_idx : u32, cnt : u32, p : vec3<f32>) -> vec2<f32> {
    var d_acc : f32 = 1e5;
    var mat : u32 = 0u;
    let lim = min(cnt, 32u - min(start_idx, 32u));
    for (var i : u32 = 0u; i < lim; i = i + 1u) {
        let op = arena[72u + start_idx + i];
        let d = eval_csg_single(op, p);
        if (i == 0u) {
            d_acc = d;
            mat = op.q0.w;
            continue;
        }
        let bop = op.q0.z;
        if (bop == 0u) {
            if (d < d_acc) {
                d_acc = d;
                mat = op.q0.w;
            }
        } else if (bop == 1u) {
            d_acc = max(d_acc, -d);
        } else if (bop == 2u) {
            if (d > d_acc) {
                d_acc = d;
                mat = op.q0.w;
            }
        } else if (bop == 3u) {
            let sm = smin_poly(d_acc, d, max(q16_to_f32(op.q2.z), 0.0));
            if (d < d_acc) { mat = op.q0.w; }
            d_acc = sm;
        }
    }
    return vec2<f32>(d_acc, f32(mat));
}

fn dq_transform(j : Cell64, pt : vec3<f32>) -> vec3<f32> {
    let u  = vec3<f32>(q16_to_f32(j.q0.w), q16_to_f32(j.q1.x), q16_to_f32(j.q1.y));
    let qw = q16_to_f32(j.q0.z);
    let dw = q16_to_f32(j.q1.z);
    let dv = vec3<f32>(q16_to_f32(j.q1.w), q16_to_f32(j.q2.x), q16_to_f32(j.q2.y));
    let p_rot = pt + 2.0 * cross(u, cross(u, pt) + qw * pt);
    let trans = 2.0 * (-dw * u + qw * dv - cross(dv, u));
    return p_rot + trans;
}

fn eval_skeleton_sdf(p : vec3<f32>, joint_cnt : u32) -> vec2<f32> {
    let n = min(joint_cnt, 8u);
    var d_acc : f32 = 1e5;
    var best_mat : u32 = 0u;
    for (var i : u32 = 0u; i < n; i = i + 1u) {
        let j = arena[136u + i];
        let blen = clamp(q16_to_f32(j.q2.z), -1024.0, 1024.0);
        let a = dq_transform(j, vec3<f32>(0.0));
        let b = dq_transform(j, vec3<f32>(0.0, blen, 0.0));
        let pa = p - a;
        let ba = b - a;
        let baba = dot(ba, ba);
        let h = select(0.0, clamp(dot(pa, ba) / max(baba, 1e-8), 0.0, 1.0), baba > 1e-8);
        let d_cap = length(pa - ba * h) - q16_to_f32(j.q2.w);
        if (i == 0u) {
            d_acc = d_cap;
            best_mat = j.q3.y;
        } else {
            let sm = smin_poly(d_acc, d_cap, max(q16_to_f32(j.q3.x), 0.0));
            if (d_cap < d_acc) { best_mat = j.q3.y; }
            d_acc = sm;
        }
    }
    return vec2<f32>(d_acc, f32(best_mat));
}

fn brick_voxel(b : Cell64, x : u32, y : u32, z : u32, step_f : f32) -> f32 {
    let idx16 = y * 4u + x;
    let wi = z * 2u + (idx16 >> 3u);
    var word : u32 = 0u;
    switch (wi) {
        case 0u: { word = b.q1.z; }
        case 1u: { word = b.q1.w; }
        case 2u: { word = b.q2.x; }
        case 3u: { word = b.q2.y; }
        case 4u: { word = b.q2.z; }
        case 5u: { word = b.q2.w; }
        case 6u: { word = b.q3.x; }
        default: { word = b.q3.y; }
    }
    let nib = i32((word >> ((idx16 & 7u) * 4u)) & 0xFu) - 8;
    return (f32(nib) * step_f) * 0.25;
}

fn sample_brick_sdf(b : Cell64, p : vec3<f32>) -> f32 {
    let step_f = q16_to_f32(b.q1.y);
    if (step_f <= 0.00390625) { return 1e5; }
    let orig = vec3<f32>(q16_to_f32(b.q0.z), q16_to_f32(b.q0.w), q16_to_f32(b.q1.x));
    let span = 3.0 * step_f;
    let q = clamp(p, orig, orig + vec3<f32>(span));
    let out_d = length(p - q);
    let u = clamp((q - orig) / step_f, vec3<f32>(0.0), vec3<f32>(2.9999));
    let i0 = vec3<u32>(u32(floor(u.x)), u32(floor(u.y)), u32(floor(u.z)));
    let f = u - vec3<f32>(f32(i0.x), f32(i0.y), f32(i0.z));

    let c000 = brick_voxel(b, i0.x,      i0.y,      i0.z,      step_f);
    let c100 = brick_voxel(b, i0.x + 1u, i0.y,      i0.z,      step_f);
    let c010 = brick_voxel(b, i0.x,      i0.y + 1u, i0.z,      step_f);
    let c110 = brick_voxel(b, i0.x + 1u, i0.y + 1u, i0.z,      step_f);
    let c001 = brick_voxel(b, i0.x,      i0.y,      i0.z + 1u, step_f);
    let c101 = brick_voxel(b, i0.x + 1u, i0.y,      i0.z + 1u, step_f);
    let c011 = brick_voxel(b, i0.x,      i0.y + 1u, i0.z + 1u, step_f);
    let c111 = brick_voxel(b, i0.x + 1u, i0.y + 1u, i0.z + 1u, step_f);

    let c00 = mix(c000, c100, f.x);
    let c10 = mix(c010, c110, f.x);
    let c01 = mix(c001, c101, f.x);
    let c11 = mix(c011, c111, f.x);
    let c0  = mix(c00, c10, f.y);
    let c1  = mix(c01, c11, f.y);
    return mix(c0, c1, f.z) + out_d;
}

fn eval_bricks_sdf(p : vec3<f32>, brick_cnt : u32) -> vec2<f32> {
    let n = min(brick_cnt, 8u);
    var best : f32 = 1e5;
    var mat : u32 = 0u;
    for (var i : u32 = 0u; i < n; i = i + 1u) {
        let b = arena[144u + i];
        let d = sample_brick_sdf(b, p);
        if (d < best) {
            best = d;
            mat = b.q0.y;
        }
    }
    return vec2<f32>(best, f32(mat));
}

fn ud_triangle(p : vec3<f32>, a : vec3<f32>, b : vec3<f32>, c : vec3<f32>) -> f32 {
    let ba = b - a; let pa = p - a;
    let cb = c - b; let pb = p - b;
    let ac = a - c; let pc = p - c;
    let nor = cross(ba, ac);

    let sign_sum = sign(dot(cross(ba, nor), pa)) +
                   sign(dot(cross(cb, nor), pb)) +
                   sign(dot(cross(ac, nor), pc));
    if (sign_sum < 2.0) {
        let d0 = ba * clamp(dot(ba, pa) / max(dot(ba, ba), 1e-10), 0.0, 1.0) - pa;
        let d1 = cb * clamp(dot(cb, pb) / max(dot(cb, cb), 1e-10), 0.0, 1.0) - pb;
        let d2 = ac * clamp(dot(ac, pc) / max(dot(ac, ac), 1e-10), 0.0, 1.0) - pc;
        return sqrt(min(dot(d0, d0), min(dot(d1, d1), dot(d2, d2))));
    }
    let ndot = dot(nor, pa);
    return abs(ndot) / max(length(nor), 1e-6);
}

fn aabb_lb(nd : Cell64, p : vec3<f32>) -> f32 {
    let mn = vec3<f32>(q16_to_f32(nd.q1.x), q16_to_f32(nd.q1.y), q16_to_f32(nd.q1.z));
    let mx = vec3<f32>(q16_to_f32(nd.q1.w), q16_to_f32(nd.q2.x), q16_to_f32(nd.q2.y));
    let q = max(mn - p, p - mx);
    let out_d = length(max(q, vec3<f32>(0.0)));
    let in_d = min(max(q.x, max(q.y, q.z)), 0.0);
    return out_d + in_d - 0.002;
}

fn ray_pos_x_tri(tr : Cell64, px : f32, ry : f32, rz : f32) -> bool {
    let v0 = vec3<f32>(q16_to_f32(tr.q0.z), q16_to_f32(tr.q0.w), q16_to_f32(tr.q1.x));
    let v1 = vec3<f32>(q16_to_f32(tr.q1.y), q16_to_f32(tr.q1.z), q16_to_f32(tr.q1.w));
    let v2 = vec3<f32>(q16_to_f32(tr.q2.x), q16_to_f32(tr.q2.y), q16_to_f32(tr.q2.z));
    let e1 = v1 - v0;
    let e2 = v2 - v0;
    let ty = ry - v0.y;
    let tz = rz - v0.z;
    let det = -e1.y * e2.z + e1.z * e2.y;
    if (abs(det) < 1e-9) { return false; }
    let inv_det = 1.0 / det;
    let u = (-ty * e2.z + tz * e2.y) * inv_det;
    let v = ( ty * e1.z - tz * e1.y) * inv_det;
    if (u < 0.0 || v < 0.0 || (u + v) > 1.0) { return false; }
    let x_hit = v0.x + u * e1.x + v * e2.x;
    return x_hit > px;
}

fn eval_mesh_bvh_sdf(p : vec3<f32>, tri_cnt : u32, bvh_cnt : u32, solid_mode : bool) -> f32 {
    if (tri_cnt == 0u || bvh_cnt == 0u) { return 1e5; }
    let tcnt = min(tri_cnt, 256u);
    let ncnt = min(bvh_cnt, 256u);

    var stack : array<u32, 48>;
    var sp : u32 = 0u;
    stack[0] = 0u;
    sp = 1u;
    var best_ud : f32 = 1e5;
    var shell : f32 = 0.0;
    var visited : u32 = 0u;

    for (var it : u32 = 0u; it < 280u; it = it + 1u) {
        if (sp == 0u || visited >= (ncnt * 2u)) { break; }
        sp = sp - 1u;
        let nid = stack[sp];
        if (nid >= ncnt) { continue; }
        visited = visited + 1u;
        let nd = mesh_buf[256u + nid];
        if (aabb_lb(nd, p) >= best_ud) { continue; }
        if (nd.q0.w != 0u) {
            let first = nd.q0.y;
            let cnt = nd.q0.z;
            for (var k : u32 = 0u; k < cnt; k = k + 1u) {
                let ti = first + k;
                if (ti >= tcnt) { break; }
                let tr = mesh_buf[ti];
                let v0 = vec3<f32>(q16_to_f32(tr.q0.z), q16_to_f32(tr.q0.w), q16_to_f32(tr.q1.x));
                let v1 = vec3<f32>(q16_to_f32(tr.q1.y), q16_to_f32(tr.q1.z), q16_to_f32(tr.q1.w));
                let v2 = vec3<f32>(q16_to_f32(tr.q2.x), q16_to_f32(tr.q2.y), q16_to_f32(tr.q2.z));
                let ud = ud_triangle(p, v0, v1, v2);
                if (ud < best_ud) {
                    best_ud = ud;
                    shell = f32((tr.q0.y >> 16u) & 0xFFFFu) * (1.0 / 4096.0);
                }
            }
        } else {
            let l = nd.q0.y;
            let r = nd.q0.z;
            let dl = select(best_ud, aabb_lb(mesh_buf[256u + l], p), l > nid && l < ncnt);
            let dr = select(best_ud, aabb_lb(mesh_buf[256u + r], p), r > nid && r < ncnt);
            if (dl < dr) {
                if (dr < best_ud && sp < 48u) { stack[sp] = r; sp = sp + 1u; }
                if (dl < best_ud && sp < 48u) { stack[sp] = l; sp = sp + 1u; }
            } else {
                if (dl < best_ud && sp < 48u) { stack[sp] = l; sp = sp + 1u; }
                if (dr < best_ud && sp < 48u) { stack[sp] = r; sp = sp + 1u; }
            }
        }
    }

    var res = best_ud;
    let root = mesh_buf[256u];
    let rmn = vec3<f32>(q16_to_f32(root.q1.x), q16_to_f32(root.q1.y), q16_to_f32(root.q1.z));
    let rmx = vec3<f32>(q16_to_f32(root.q1.w), q16_to_f32(root.q2.x), q16_to_f32(root.q2.y));
    if (solid_mode && p.x >= rmn.x && p.x <= rmx.x && p.y >= rmn.y && p.y <= rmx.y && p.z >= rmn.z && p.z <= rmx.z) {
        let ry = p.y + (3.0 / 65536.0);
        let rz = p.z + (5.0 / 65536.0);
        var parity : u32 = 0u;
        var rstack : array<u32, 48>;
        var rsp : u32 = 1u;
        rstack[0] = 0u;
        var rvis : u32 = 0u;
        for (var it : u32 = 0u; it < 280u; it = it + 1u) {
            if (rsp == 0u || rvis >= (ncnt * 2u)) { break; }
            rsp = rsp - 1u;
            let nid = rstack[rsp];
            if (nid >= ncnt) { continue; }
            rvis = rvis + 1u;
            let nd = mesh_buf[256u + nid];
            let mn = vec3<f32>(q16_to_f32(nd.q1.x), q16_to_f32(nd.q1.y), q16_to_f32(nd.q1.z));
            let mx = vec3<f32>(q16_to_f32(nd.q1.w), q16_to_f32(nd.q2.x), q16_to_f32(nd.q2.y));
            if (p.x > mx.x || ry < mn.y || ry > mx.y || rz < mn.z || rz > mx.z) { continue; }
            if (nd.q0.w != 0u) {
                let first = nd.q0.y;
                let cnt = nd.q0.z;
                for (var k : u32 = 0u; k < cnt; k = k + 1u) {
                    let ti = first + k;
                    if (ti >= tcnt) { break; }
                    if (ray_pos_x_tri(mesh_buf[ti], p.x, ry, rz)) {
                        parity = parity ^ 1u;
                    }
                }
            } else {
                let l = nd.q0.y;
                let r = nd.q0.z;
                if (r > nid && r < ncnt && rsp < 48u) { rstack[rsp] = r; rsp = rsp + 1u; }
                if (l > nid && l < ncnt && rsp < 48u) { rstack[rsp] = l; rsp = rsp + 1u; }
            }
        }
        if (parity != 0u) {
            res = -best_ud;
        }
    }
    return res - shell;
}

fn eval_prefab_sdf(pf : Cell64, lp : vec3<f32>, param_delta : f32) -> f32 {
    let csg_cnt = pf.q3.x;
    if (csg_cnt > 0u) {
        let csg_start = pf.q2.w;
        if (csg_start + csg_cnt <= 32u) {
            return eval_csg_program(csg_start, csg_cnt, lp).x;
        }
    }
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

fn get_sec_normal() -> vec3<f32> {
    let sec = arena[254];
    let n = vec3<f32>(q16_to_f32(sec.q1.x), q16_to_f32(sec.q1.y), q16_to_f32(sec.q1.z));
    return safe_norm3(n, vec3<f32>(1.0, 0.0, 0.0));
}

fn eval_section_clip_val(p : vec3<f32>, xray_on : bool, xray_plane : f32) -> f32 {
    let sec = arena[254];
    let smode = sec.q0.x;
    if (smode != 0u) {
        let sn = get_sec_normal();
        let sdist = q16_to_f32(sec.q1.w);
        let ndotp = dot(sn, p);
        if (smode == 2u) {
            let slab_h = max(q16_to_f32(sec.q2.x), 0.015625);
            return abs(ndotp - sdist) - slab_h;
        }
        if (smode == 3u) {
            let wedge = max(ndotp - sdist, p.z);
            return -wedge;
        }
        return sdist - ndotp;
    }
    if (xray_on) {
        return xray_plane - p.x;
    }
    return -1e5;
}

struct SceneHit {
    dist         : f32,
    inst_id      : u32,
    inst_slot    : u32,
    mat_id       : u32,
    style_id     : u32,
    is_cut       : bool,
    raw_d        : f32,
    second_raw_d : f32,
    local_p      : vec3<f32>,
};

fn map_scene_raw_best(p : vec3<f32>) -> f32 {
    let inst_cnt = min(arena[0].q1.z, 32u);
    let sec = arena[254];
    var best_d : f32 = 1e5;

    for (var i : u32 = 0u; i < inst_cnt; i = i + 1u) {
        let inst = arena[40u + i];
        let flags = (inst.q3.x >> 16u) & 0xFFFFu;
        if ((flags & 1u) == 0u) { continue; }
        let pid = inst.q0.y;
        if (pid == 0u || pid > 16u) { continue; }
        let pos = vec3<f32>(q16_to_f32(inst.q1.x), q16_to_f32(inst.q1.y), q16_to_f32(inst.q1.z));
        let rot = vec3<f32>(q16_to_f32(inst.q1.w), q16_to_f32(inst.q2.x), q16_to_f32(inst.q2.y));
        let raw_sc = q16_to_f32(inst.q2.z);
        let sc = select(1.0, clamp(raw_sc, 0.0625, 256.0), raw_sc > 0.0625);
        let p_ovr = q16_to_f32(inst.q3.z);
        let pf = arena[24u + (pid - 1u)];
        let bound_r = q16_to_f32(pf.q1.x) * sc + 0.5;
        let rel = p - pos;
        if (bound_r > 0.51 && length(rel) - bound_r >= best_d) { continue; }
        let lp = rot_inv_euler(rel, rot) / sc;
        let d = eval_prefab_sdf(pf, lp, p_ovr) * sc;
        if (d < best_d) { best_d = d; }
    }

    let gcsg_cnt = (sec.q3.x >> 16u) & 0xFFFFu;
    if (gcsg_cnt > 0u) {
        let gcsg_start = sec.q3.x & 0xFFFFu;
        if (gcsg_start + gcsg_cnt <= 32u) {
            best_d = min(best_d, eval_csg_program(gcsg_start, gcsg_cnt, p).x);
        }
    }
    let jcnt = sec.q0.z & 0xFFFFu;
    if (jcnt > 0u) { best_d = min(best_d, eval_skeleton_sdf(p, jcnt).x); }
    let bcnt = (sec.q0.z >> 16u) & 0xFFFFu;
    if (bcnt > 0u) { best_d = min(best_d, eval_bricks_sdf(p, bcnt).x); }
    let tri_cnt = sec.q0.w & 0xFFFFu;
    let mbvh_cnt = (sec.q0.w >> 16u) & 0xFFFFu;
    if (tri_cnt > 0u && mbvh_cnt > 0u) {
        let solid_m = (sec.q0.y & 16u) != 0u;
        best_d = min(best_d, eval_mesh_bvh_sdf(p, tri_cnt, mbvh_cnt, solid_m));
    }
    return best_d;
}

fn map_scene_dist(p : vec3<f32>, xray_on : bool, xray_plane : f32) -> f32 {
    let raw_d = map_scene_raw_best(p);
    let clip_v = eval_section_clip_val(p, xray_on, xray_plane);
    return max(raw_d, clip_v);
}

fn map_scene_full(p : vec3<f32>, xray_on : bool, xray_plane : f32) -> SceneHit {
    let inst_cnt = min(arena[0].q1.z, 32u);
    let sec = arena[254];
    let clip_v = eval_section_clip_val(p, xray_on, xray_plane);

    var out : SceneHit;
    out.dist = 1e5;
    out.inst_id = 0u;
    out.inst_slot = 0u;
    out.mat_id = 0u;
    out.style_id = 1u;
    out.is_cut = false;
    out.raw_d = 1e5;
    out.second_raw_d = 1e5;
    out.local_p = p;

    var best_raw : f32 = 1e5;
    var sec_raw  : f32 = 1e5;

    for (var i : u32 = 0u; i < inst_cnt; i = i + 1u) {
        let inst = arena[40u + i];
        let flags = (inst.q3.x >> 16u) & 0xFFFFu;
        if ((flags & 1u) == 0u) { continue; }
        let pid = inst.q0.y;
        if (pid == 0u || pid > 16u) { continue; }

        let pos = vec3<f32>(q16_to_f32(inst.q1.x), q16_to_f32(inst.q1.y), q16_to_f32(inst.q1.z));
        let rot = vec3<f32>(q16_to_f32(inst.q1.w), q16_to_f32(inst.q2.x), q16_to_f32(inst.q2.y));
        let raw_sc = q16_to_f32(inst.q2.z);
        let sc = select(1.0, clamp(raw_sc, 0.0625, 256.0), raw_sc > 0.0625);
        let p_ovr = q16_to_f32(inst.q3.z);

        let pf = arena[24u + (pid - 1u)];
        let bound_r = q16_to_f32(pf.q1.x) * sc + 0.5;
        let rel = p - pos;
        if (bound_r > 0.51 && length(rel) - bound_r >= out.dist && sec_raw < 10.0) { continue; }

        let lp = rot_inv_euler(rel, rot) / sc;
        let raw_d = eval_prefab_sdf(pf, lp, p_ovr) * sc;
        if (raw_d < best_raw) {
            sec_raw = best_raw;
            best_raw = raw_d;
        } else if (raw_d < sec_raw) {
            sec_raw = raw_d;
        }

        let cut_face = clip_v > raw_d;
        let d = max(raw_d, clip_v);
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
            out.local_p = lp;
        }
    }

    let gcsg_cnt = (sec.q3.x >> 16u) & 0xFFFFu;
    if (gcsg_cnt > 0u) {
        let gcsg_start = sec.q3.x & 0xFFFFu;
        if (gcsg_start + gcsg_cnt <= 32u) {
            let res = eval_csg_program(gcsg_start, gcsg_cnt, p);
            let raw_d = res.x;
            if (raw_d < best_raw) { sec_raw = best_raw; best_raw = raw_d; }
            else if (raw_d < sec_raw) { sec_raw = raw_d; }
            let d = max(raw_d, clip_v);
            if (d < out.dist) {
                out.dist = d;
                out.inst_id = 99u;
                out.mat_id = u32(res.y) & 7u;
                out.style_id = clamp(out.mat_id + 1u, 1u, 8u);
                out.is_cut = clip_v > raw_d;
                out.raw_d = raw_d;
                out.local_p = p;
            }
        }
    }

    let jcnt = sec.q0.z & 0xFFFFu;
    if (jcnt > 0u) {
        let res = eval_skeleton_sdf(p, jcnt);
        let raw_d = res.x;
        if (raw_d < best_raw) { sec_raw = best_raw; best_raw = raw_d; }
        else if (raw_d < sec_raw) { sec_raw = raw_d; }
        let d = max(raw_d, clip_v);
        if (d < out.dist) {
            out.dist = d;
            out.inst_id = 100u;
            out.mat_id = u32(res.y) & 7u;
            out.style_id = clamp(out.mat_id + 1u, 1u, 8u);
            out.is_cut = clip_v > raw_d;
            out.raw_d = raw_d;
            out.local_p = p;
        }
    }

    let bcnt = (sec.q0.z >> 16u) & 0xFFFFu;
    if (bcnt > 0u) {
        let res = eval_bricks_sdf(p, bcnt);
        let raw_d = res.x;
        if (raw_d < best_raw) { sec_raw = best_raw; best_raw = raw_d; }
        else if (raw_d < sec_raw) { sec_raw = raw_d; }
        let d = max(raw_d, clip_v);
        if (d < out.dist) {
            out.dist = d;
            out.inst_id = 101u;
            out.mat_id = u32(res.y) & 7u;
            out.style_id = clamp(out.mat_id + 1u, 1u, 8u);
            out.is_cut = clip_v > raw_d;
            out.raw_d = raw_d;
            out.local_p = p;
        }
    }

    let tri_cnt = sec.q0.w & 0xFFFFu;
    let mbvh_cnt = (sec.q0.w >> 16u) & 0xFFFFu;
    if (tri_cnt > 0u && mbvh_cnt > 0u) {
        let solid_m = (sec.q0.y & 16u) != 0u;
        let raw_d = eval_mesh_bvh_sdf(p, tri_cnt, mbvh_cnt, solid_m);
        if (raw_d < best_raw) { sec_raw = best_raw; best_raw = raw_d; }
        else if (raw_d < sec_raw) { sec_raw = raw_d; }
        let d = max(raw_d, clip_v);
        if (d < out.dist) {
            out.dist = d;
            out.inst_id = 102u;
            let mms = sec.q3.y;
            out.mat_id = mms & 7u;
            let st_m = (mms >> 16u) & 0xFFu;
            out.style_id = select(clamp(out.mat_id + 1u, 1u, 8u), clamp(st_m, 1u, 8u), st_m >= 1u && st_m <= 8u);
            out.is_cut = clip_v > raw_d;
            out.raw_d = raw_d;
            out.local_p = p;
        }
    }

    out.second_raw_d = sec_raw;
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

fn eval_triplanar_grain(lp : vec3<f32>, ln : vec3<f32>, freq : f32, metallic : f32, wire_w : f32) -> vec3<f32> {
    let w = abs(ln) / max(abs(ln.x) + abs(ln.y) + abs(ln.z), 1e-4);
    let f = clamp(freq, 2.0, 28.0);
    // Anisotropic brushed metal / micro-ceramic directional grain + 3D TPMS gyroid lattice
    let brush_x = sin(lp.y * 42.0 * f + sin(lp.z * 7.0) * 1.5);
    let brush_y = sin(lp.x * 42.0 * f + sin(lp.z * 7.0) * 1.5);
    let brush_z = sin(lp.x * 42.0 * f + sin(lp.y * 7.0) * 1.5);
    let tri_brush = w.x * brush_x + w.y * brush_y + w.z * brush_z;

    let gxy = sin(lp.x * 9.0 + lp.y * 9.0) * sin(lp.x * 9.0 - lp.y * 9.0);
    let gyroid = sin(lp.x * 16.0) * cos(lp.y * 16.0) +
                 sin(lp.y * 16.0) * cos(lp.z * 16.0) +
                 sin(lp.z * 16.0) * cos(lp.x * 16.0);
    let lattice = (1.0 - smoothstep(0.0, 0.22, abs(gyroid))) * wire_w;
    let rough_mod = mix(gxy * 0.04, tri_brush * 0.06, metallic);
    return vec3<f32>(rough_mod, tri_brush, lattice);
}

fn eval_raw_grad(p : vec3<f32>) -> vec3<f32> {
    let e = 0.004;
    let gx = map_scene_raw_best(p + vec3<f32>(e, 0.0, 0.0)) - map_scene_raw_best(p - vec3<f32>(e, 0.0, 0.0));
    let gy = map_scene_raw_best(p + vec3<f32>(0.0, e, 0.0)) - map_scene_raw_best(p - vec3<f32>(0.0, e, 0.0));
    let gz = map_scene_raw_best(p + vec3<f32>(0.0, 0.0, e)) - map_scene_raw_best(p - vec3<f32>(0.0, 0.0, e));
    return safe_norm3(vec3<f32>(gx, gy, gz), vec3<f32>(0.0, 1.0, 0.0));
}

struct DiffJet2 {
    H     : f32,
    K     : f32,
    aniso : f32,
    e1    : vec3<f32>,
};

fn eval_diff_jet2(p : vec3<f32>, n : vec3<f32>) -> DiffJet2 {
    let e = 0.04;
    let inv_e2 = 625.0;
    let inv_4e2 = 156.25;

    let f0   = map_scene_raw_best(p);
    let f_px = map_scene_raw_best(p + vec3<f32>( e, 0.0, 0.0));
    let f_mx = map_scene_raw_best(p + vec3<f32>(-e, 0.0, 0.0));
    let f_py = map_scene_raw_best(p + vec3<f32>(0.0,  e, 0.0));
    let f_my = map_scene_raw_best(p + vec3<f32>(0.0, -e, 0.0));
    let f_pz = map_scene_raw_best(p + vec3<f32>(0.0, 0.0,  e));
    let f_mz = map_scene_raw_best(p + vec3<f32>(0.0, 0.0, -e));

    let f_pp0 = map_scene_raw_best(p + vec3<f32>( e,  e, 0.0));
    let f_pm0 = map_scene_raw_best(p + vec3<f32>( e, -e, 0.0));
    let f_mp0 = map_scene_raw_best(p + vec3<f32>(-e,  e, 0.0));
    let f_mm0 = map_scene_raw_best(p + vec3<f32>(-e, -e, 0.0));

    let f_0pp = map_scene_raw_best(p + vec3<f32>(0.0,  e,  e));
    let f_0pm = map_scene_raw_best(p + vec3<f32>(0.0,  e, -e));
    let f_0mp = map_scene_raw_best(p + vec3<f32>(0.0, -e,  e));
    let f_0mm = map_scene_raw_best(p + vec3<f32>(0.0, -e, -e));

    let f_p0p = map_scene_raw_best(p + vec3<f32>( e, 0.0,  e));
    let f_p0m = map_scene_raw_best(p + vec3<f32>( e, 0.0, -e));
    let f_m0p = map_scene_raw_best(p + vec3<f32>(-e, 0.0,  e));
    let f_m0m = map_scene_raw_best(p + vec3<f32>(-e, 0.0, -e));

    let hxx = clamp((f_px - 2.0 * f0 + f_mx) * inv_e2, -64.0, 64.0);
    let hyy = clamp((f_py - 2.0 * f0 + f_my) * inv_e2, -64.0, 64.0);
    let hzz = clamp((f_pz - 2.0 * f0 + f_mz) * inv_e2, -64.0, 64.0);
    let hxy = clamp(((f_pp0 - f_pm0) - (f_mp0 - f_mm0)) * inv_4e2, -64.0, 64.0);
    let hyz = clamp(((f_0pp - f_0pm) - (f_0mp - f_0mm)) * inv_4e2, -64.0, 64.0);
    let hxz = clamp(((f_p0p - f_p0m) - (f_m0p - f_m0m)) * inv_4e2, -64.0, 64.0);

    let r0 = vec3<f32>(hxx, hxy, hxz);
    let r1 = vec3<f32>(hxy, hyy, hyz);
    let r2 = vec3<f32>(hxz, hyz, hzz);

    let up_ref = select(vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(1.0, 0.0, 0.0), abs(n.y) > 0.80);
    let t1 = safe_norm3(cross(up_ref, n), vec3<f32>(1.0, 0.0, 0.0));
    let t2 = safe_norm3(cross(n, t1), vec3<f32>(0.0, 0.0, 1.0));

    let ht1 = vec3<f32>(dot(r0, t1), dot(r1, t1), dot(r2, t1));
    let ht2 = vec3<f32>(dot(r0, t2), dot(r1, t2), dot(r2, t2));
    let s11 = dot(t1, ht1);
    let s22 = dot(t2, ht2);
    let s12 = dot(t1, ht2);

    let diff_s = s11 - s22;
    let aniso = length(vec2<f32>(diff_s, 2.0 * s12));
    var b = vec2<f32>(1.0, 0.0);
    if (aniso > 1e-3) {
        if (diff_s >= 0.0) {
            b = vec2<f32>(diff_s + aniso, 2.0 * s12);
        } else {
            b = vec2<f32>(2.0 * s12, aniso - diff_s);
        }
    }
    let e1 = safe_norm3(t1 * b.x + t2 * b.y, t1);
    return DiffJet2(0.5 * (s11 + s22), s11 * s22 - s12 * s12, aniso, e1);
}

// ----------------------------------------------------------------------------
// 2b. Zero-Noise 3-Pass 3D Radiance Cascades Global Illumination
//     C2 [0..4095]:     8x4x8 probes x 16 oct dirs  -> [1.15m, 4.80m]
//     C1 [4096..20479]: 16x8x16 probes x 8 oct dirs -> [0.28m, 1.15m] + C2
//     C0 [20480..32767]:16x8x16 probes x 6 lobes    -> [0.04m, 0.28m] + C1
// ----------------------------------------------------------------------------

fn rc_half_ext() -> vec3<f32> {
    return vec3<f32>(4.0, 2.5, 4.0);
}

fn rc_cage_center() -> vec3<f32> {
    let cam = arena[1];
    return vec3<f32>(q16_to_f32(cam.q0.w), q16_to_f32(cam.q1.x), q16_to_f32(cam.q1.y));
}

fn oct_decode(uv : vec2<f32>) -> vec3<f32> {
    var n = vec3<f32>(uv.x, uv.y, 1.0 - abs(uv.x) - abs(uv.y));
    if (n.z < 0.0) {
        let sx = select(-1.0, 1.0, uv.x >= 0.0);
        let sy = select(-1.0, 1.0, uv.y >= 0.0);
        n.x = (1.0 - abs(uv.y)) * sx;
        n.y = (1.0 - abs(uv.x)) * sy;
    }
    return safe_norm3(n, vec3<f32>(0.0, 1.0, 0.0));
}

fn oct_encode(n : vec3<f32>) -> vec2<f32> {
    let l1 = abs(n.x) + abs(n.y) + abs(n.z);
    var p = n.xy / max(l1, 1e-6);
    if (n.z < 0.0) {
        let sx = select(-1.0, 1.0, p.x >= 0.0);
        let sy = select(-1.0, 1.0, p.y >= 0.0);
        p = vec2<f32>((1.0 - abs(p.y)) * sx, (1.0 - abs(p.x)) * sy);
    }
    return clamp(p * 0.5 + vec2<f32>(0.5), vec2<f32>(0.0), vec2<f32>(0.9999));
}

fn rc_probe_pos(px : u32, py : u32, pz : u32, dims : vec3<f32>) -> vec3<f32> {
    let uvw = ((vec3<f32>(f32(px), f32(py), f32(pz)) + 0.5) / dims) * 2.0 - vec3<f32>(1.0);
    return rc_cage_center() + uvw * rc_half_ext();
}

fn eval_rc_hit_radiance(
    p : vec3<f32>,
    xray_on : bool,
    xray_plane : f32,
    time_s : f32
) -> vec4<f32> {
    let hit = map_scene_full(p, xray_on, xray_plane);
    let n = calc_normal(p, xray_on, xray_plane);
    let m_cell = arena[8u + (hit.mat_id & 7u)];
    let s_cell = arena[16u + (clamp(hit.style_id, 1u, 8u) - 1u)];
    let inst_k = arena[40u + (hit.inst_slot & 31u)].q2.w;

    let eta    = clamp(q16_to_f32(m_cell.q0.y), 1.0, 4.0);
    let kappa  = clamp(q16_to_f32(m_cell.q0.z), 0.0, 8.0);
    let kelvin = max(m_cell.q3.x, inst_k);
    let base_tint = unpack_rgb24(s_cell.q0.y);
    let metallic  = clamp(max(q16_to_f32(s_cell.q1.x), clamp((kappa - 0.35) * 0.5, 0.0, 1.0)), 0.0, 1.0);

    let key_dir = vec3<f32>(-0.5145, 0.7503, -0.4150);
    let ndl = clamp(dot(n, key_dir), 0.0, 1.0);
    let sh = calc_soft_shadow(p + n * 0.025, key_dir, 8.0, xray_on, xray_plane);
    var rad = base_tint * (0.04 + 0.52 * ndl * sh) * (1.0 - 0.55 * metallic);

    if (kelvin > 650u) {
        let ex = clamp(f32(kelvin - 650u) * 0.001, 0.0, 2.5);
        let pulse = select(1.0, 0.90 + 0.10 * cos(time_s * 6.0), time_s > 0.0);
        rad = rad + vec3<f32>(ex * 5.2, ex * ex * 2.4, max(0.0, ex - 0.42) * 1.4) * pulse;
    }
    let is_trans = (eta > 1.02) && (kappa < 0.05) && (metallic < 0.35) && (hit.mat_id != 6u) && (kelvin <= 650u);
    let beta = select(0.0, 0.28, is_trans);
    return vec4<f32>(rad, beta);
}

fn trace_rc_interval(
    ro : vec3<f32>,
    rd : vec3<f32>,
    t0 : f32,
    t1 : f32,
    steps : u32,
    xray_on : bool,
    xray_plane : f32,
    time_s : f32
) -> vec4<f32> {
    if (map_scene_dist(ro, xray_on, xray_plane) < -0.02) {
        return vec4<f32>(0.0, 0.0, 0.0, 0.0);
    }
    let min_step = max((t1 - t0) / f32(steps), 0.02);
    var t = t0;
    for (var i : u32 = 0u; i < steps; i = i + 1u) {
        if (t > t1) { break; }
        let p = ro + rd * t;
        let d = map_scene_dist(p, xray_on, xray_plane);
        if (d < 0.02) {
            return eval_rc_hit_radiance(p, xray_on, xray_plane, time_s);
        }
        t = t + max(d, min_step);
    }
    return vec4<f32>(0.0, 0.0, 0.0, 1.0);
}

fn sample_rc2_trilinear(pos : vec3<f32>, dir : vec3<f32>) -> vec4<f32> {
    let dims = vec3<f32>(8.0, 4.0, 8.0);
    let uvw = clamp(((pos - rc_cage_center()) / rc_half_ext()) * 0.5 + 0.5, vec3<f32>(0.0), vec3<f32>(0.9999));
    let gc = clamp(uvw * dims - 0.5, vec3<f32>(0.0), dims - vec3<f32>(1.0001));
    let i0 = vec3<u32>(u32(floor(gc.x)), u32(floor(gc.y)), u32(floor(gc.z)));
    let i1 = min(i0 + vec3<u32>(1u), vec3<u32>(7u, 3u, 7u));
    let f = fract(gc);

    let ouv = oct_encode(dir);
    let du = clamp(ouv.x * 4.0 - 0.5, 0.0, 2.9999);
    let dv = clamp(ouv.y * 4.0 - 0.5, 0.0, 2.9999);
    let ou0 = u32(floor(du)); let ou1 = min(ou0 + 1u, 3u); let fu = fract(du);
    let ov0 = u32(floor(dv)); let ov1 = min(ov0 + 1u, 3u); let fv = fract(dv);

    let d00 = ov0 * 4u + ou0;
    let d10 = ov0 * 4u + ou1;
    let d01 = ov1 * 4u + ou0;
    let d11 = ov1 * 4u + ou1;

    var acc = vec4<f32>(0.0);
    for (var c : u32 = 0u; c < 8u; c = c + 1u) {
        let px = select(i0.x, i1.x, (c & 1u) != 0u);
        let py = select(i0.y, i1.y, (c & 2u) != 0u);
        let pz = select(i0.z, i1.z, (c & 4u) != 0u);
        let wx = select(1.0 - f.x, f.x, (c & 1u) != 0u);
        let wy = select(1.0 - f.y, f.y, (c & 2u) != 0u);
        let wz = select(1.0 - f.z, f.z, (c & 4u) != 0u);
        let base = ((pz * 4u + py) * 8u + px) * 16u;
        let a00 = rc_buf[base + d00];
        let a10 = rc_buf[base + d10];
        let a01 = rc_buf[base + d01];
        let a11 = rc_buf[base + d11];
        let dir_v = mix(mix(a00, a10, fu), mix(a01, a11, fu), fv);
        acc = acc + dir_v * (wx * wy * wz);
    }
    return acc;
}

fn sample_rc1_trilinear(pos : vec3<f32>, dir : vec3<f32>) -> vec4<f32> {
    let dims = vec3<f32>(16.0, 8.0, 16.0);
    let uvw = clamp(((pos - rc_cage_center()) / rc_half_ext()) * 0.5 + 0.5, vec3<f32>(0.0), vec3<f32>(0.9999));
    let gc = clamp(uvw * dims - 0.5, vec3<f32>(0.0), dims - vec3<f32>(1.0001));
    let i0 = vec3<u32>(u32(floor(gc.x)), u32(floor(gc.y)), u32(floor(gc.z)));
    let i1 = min(i0 + vec3<u32>(1u), vec3<u32>(15u, 7u, 15u));
    let f = fract(gc);

    let ouv = oct_encode(dir);
    let du = clamp(ouv.x * 4.0 - 0.5, 0.0, 2.9999);
    let dv = clamp(ouv.y * 2.0 - 0.5, 0.0, 0.9999);
    let ou0 = u32(floor(du)); let ou1 = min(ou0 + 1u, 3u); let fu = fract(du);
    let ov0 = u32(floor(dv)); let ov1 = min(ov0 + 1u, 1u); let fv = fract(dv);

    let d00 = ov0 * 4u + ou0;
    let d10 = ov0 * 4u + ou1;
    let d01 = ov1 * 4u + ou0;
    let d11 = ov1 * 4u + ou1;

    var acc = vec4<f32>(0.0);
    for (var c : u32 = 0u; c < 8u; c = c + 1u) {
        let px = select(i0.x, i1.x, (c & 1u) != 0u);
        let py = select(i0.y, i1.y, (c & 2u) != 0u);
        let pz = select(i0.z, i1.z, (c & 4u) != 0u);
        let wx = select(1.0 - f.x, f.x, (c & 1u) != 0u);
        let wy = select(1.0 - f.y, f.y, (c & 2u) != 0u);
        let wz = select(1.0 - f.z, f.z, (c & 4u) != 0u);
        let base = 4096u + ((pz * 8u + py) * 16u + px) * 8u;
        let a00 = rc_buf[base + d00];
        let a10 = rc_buf[base + d10];
        let a01 = rc_buf[base + d01];
        let a11 = rc_buf[base + d11];
        let dir_v = mix(mix(a00, a10, fu), mix(a01, a11, fu), fv);
        acc = acc + dir_v * (wx * wy * wz);
    }
    return acc;
}

fn sample_rc0_irradiance(p : vec3<f32>, n : vec3<f32>) -> vec3<f32> {
    let q = p + n * 0.045;
    let dims = vec3<f32>(16.0, 8.0, 16.0);
    let uvw = clamp(((q - rc_cage_center()) / rc_half_ext()) * 0.5 + 0.5, vec3<f32>(0.0), vec3<f32>(0.9999));
    let gc = clamp(uvw * dims - 0.5, vec3<f32>(0.0), dims - vec3<f32>(1.0001));
    let i0 = vec3<u32>(u32(floor(gc.x)), u32(floor(gc.y)), u32(floor(gc.z)));
    let i1 = min(i0 + vec3<u32>(1u), vec3<u32>(15u, 7u, 15u));
    let f = fract(gc);

    let n2 = n * n;
    let ix_lobe = select(1u, 0u, n.x >= 0.0);
    let iy_lobe = select(3u, 2u, n.y >= 0.0);
    let iz_lobe = select(5u, 4u, n.z >= 0.0);

    var irr = vec3<f32>(0.0);
    var w_sum : f32 = 0.0;
    for (var c : u32 = 0u; c < 8u; c = c + 1u) {
        let px = select(i0.x, i1.x, (c & 1u) != 0u);
        let py = select(i0.y, i1.y, (c & 2u) != 0u);
        let pz = select(i0.z, i1.z, (c & 4u) != 0u);
        let wx = select(1.0 - f.x, f.x, (c & 1u) != 0u);
        let wy = select(1.0 - f.y, f.y, (c & 2u) != 0u);
        let wz = select(1.0 - f.z, f.z, (c & 4u) != 0u);
        let base = 20480u + ((pz * 8u + py) * 16u + px) * 6u;
        let lx = rc_buf[base + ix_lobe];
        let ly = rc_buf[base + iy_lobe];
        let lz = rc_buf[base + iz_lobe];
        let probe_irr = lx.rgb * n2.x + ly.rgb * n2.y + lz.rgb * n2.z;
        let w = (wx * wy * wz) * mix(0.05, 1.0, lx.a);
        irr = irr + probe_irr * w;
        w_sum = w_sum + w;
    }
    return irr / max(w_sum, 1e-4);
}

@compute @workgroup_size(8, 8, 1)
fn bloom_rc_cascade2(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x == 0xFFFFFFFFu) { textureStore(out_tex, vec2<i32>(0, 0), vec4<f32>(0.0)); }
    let tid = gid.y * 512u + gid.x;
    if (tid >= 4096u) { return; }

    let probe_idx = tid >> 4u;
    let dir_idx   = tid & 15u;
    let px = probe_idx & 7u;
    let py = (probe_idx >> 3u) & 3u;
    let pz = (probe_idx >> 5u) & 7u;
    let pos = rc_probe_pos(px, py, pz, vec3<f32>(8.0, 4.0, 8.0));

    let du = ((f32(dir_idx & 3u) + 0.5) / 4.0) * 2.0 - 1.0;
    let dv = ((f32(dir_idx >> 2u) + 0.5) / 4.0) * 2.0 - 1.0;
    let dir = oct_decode(vec2<f32>(du, dv));

    let hdr_flags = arena[0].q3.z;
    let xray_on   = (hdr_flags & 2u) != 0u;
    let xray_p    = q16_to_f32(arena[0].q3.x);
    let time_s    = f32(arena[0].q1.y) * 0.0166667;

    let res = trace_rc_interval(pos, dir, 1.15, 4.80, 7u, xray_on, xray_p, time_s);
    let sky = eval_sky_dome(dir) * 0.55;
    rc_buf[tid] = vec4<f32>(res.rgb + res.a * sky, res.a);
}

@compute @workgroup_size(8, 8, 1)
fn bloom_rc_cascade1(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x == 0xFFFFFFFFu) { textureStore(out_tex, vec2<i32>(0, 0), vec4<f32>(0.0)); }
    let tid = gid.y * 512u + gid.x;
    if (tid >= 16384u) { return; }

    let probe_idx = tid >> 3u;
    let dir_idx   = tid & 7u;
    let px = probe_idx & 15u;
    let py = (probe_idx >> 4u) & 7u;
    let pz = (probe_idx >> 7u) & 15u;
    let pos = rc_probe_pos(px, py, pz, vec3<f32>(16.0, 8.0, 16.0));

    let du = ((f32(dir_idx & 3u) + 0.5) / 4.0) * 2.0 - 1.0;
    let dv = ((f32(dir_idx >> 2u) + 0.5) / 2.0) * 2.0 - 1.0;
    let dir = oct_decode(vec2<f32>(du, dv));

    let hdr_flags = arena[0].q3.z;
    let xray_on   = (hdr_flags & 2u) != 0u;
    let xray_p    = q16_to_f32(arena[0].q3.x);
    let time_s    = f32(arena[0].q1.y) * 0.0166667;

    let near_v = trace_rc_interval(pos, dir, 0.28, 1.15, 7u, xray_on, xray_p, time_s);
    let far_v  = sample_rc2_trilinear(pos, dir);
    rc_buf[4096u + tid] = vec4<f32>(near_v.rgb + near_v.a * far_v.rgb, near_v.a * far_v.a);
}

@compute @workgroup_size(8, 8, 1)
fn bloom_rc_cascade0(@builtin(global_invocation_id) gid : vec3<u32>) {
    if (gid.x == 0xFFFFFFFFu) { textureStore(out_tex, vec2<i32>(0, 0), vec4<f32>(0.0)); }
    let probe_idx = gid.y * 256u + gid.x;
    if (probe_idx >= 2048u) { return; }

    let px = probe_idx & 15u;
    let py = (probe_idx >> 4u) & 7u;
    let pz = (probe_idx >> 7u) & 15u;
    let pos = rc_probe_pos(px, py, pz, vec3<f32>(16.0, 8.0, 16.0));

    let hdr_flags = arena[0].q3.z;
    let xray_on   = (hdr_flags & 2u) != 0u;
    let xray_p    = q16_to_f32(arena[0].q3.x);
    let time_s    = f32(arena[0].q1.y) * 0.0166667;
    let valid     = select(0.0, 1.0, map_scene_dist(pos, xray_on, xray_p) >= -0.04);

    let axes = array<vec3<f32>, 6>(
        vec3<f32>( 1.0,  0.0,  0.0),
        vec3<f32>(-1.0,  0.0,  0.0),
        vec3<f32>( 0.0,  1.0,  0.0),
        vec3<f32>( 0.0, -1.0,  0.0),
        vec3<f32>( 0.0,  0.0,  1.0),
        vec3<f32>( 0.0,  0.0, -1.0)
    );
    let base = 20480u + probe_idx * 6u;
    for (var k : u32 = 0u; k < 6u; k = k + 1u) {
        let dir = axes[k];
        let near_v = trace_rc_interval(pos, dir, 0.04, 0.28, 5u, xray_on, xray_p, time_s);
        let far_v  = sample_rc1_trilinear(pos, dir);
        rc_buf[base + k] = vec4<f32>(near_v.rgb + near_v.a * far_v.rgb, valid);
    }
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
    studio_gi : bool,
    rc_on : bool,
    diff_field : bool,
    subframe_lock : bool,
    selected_id : u32,
    time_s : f32
) -> vec3<f32> {
    let m_cell = arena[8u + (hit.mat_id & 7u)];
    let s_cell = arena[16u + (clamp(hit.style_id, 1u, 8u) - 1u)];
    let inst_k = arena[40u + (hit.inst_slot & 31u)].q2.w;
    let sec    = arena[254];
    let sec_mode = sec.q0.x;
    let insp_flags = sec.q0.y;

    let eta    = clamp(q16_to_f32(m_cell.q0.y), 1.0, 4.0);
    let kappa  = clamp(q16_to_f32(m_cell.q0.z), 0.0, 8.0);
    let relief = clamp(q16_to_f32(m_cell.q1.x), 0.0, 2.0);
    let rough  = clamp(q16_to_f32(m_cell.q1.y), 0.02, 1.0);
    let mie    = clamp(q16_to_f32(m_cell.q1.z), 0.0, 1.0);
    let mu_a   = vec3<f32>(q16_to_f32(m_cell.q1.w), q16_to_f32(m_cell.q2.x), q16_to_f32(m_cell.q2.y));
    let wire_w = clamp(q16_to_f32(m_cell.q2.z), 0.0, 1.0);
    let d_mm   = clamp(q16_to_f32(m_cell.q2.w) * 0.125, 0.08, 3.0);
    let kelvin = max(m_cell.q3.x, inst_k);

    let base_tint   = unpack_rgb24(s_cell.q0.y);
    let accent_tint = unpack_rgb24(s_cell.q0.z);
    let style_rough = clamp(q16_to_f32(s_cell.q0.w), 0.02, 1.0);
    let metallic    = clamp(max(q16_to_f32(s_cell.q1.x), clamp((kappa - 0.35) * 0.5, 0.0, 1.0)), 0.0, 1.0);

    var n_eff = n;
    var tri_grain = vec3<f32>(0.0);
    var ridge_polish : f32 = 0.0;
    var crevice_patina : f32 = 0.0;
    var field_aniso : f32 = 0.0;
    if ((studio_gi || diff_field || (insp_flags & 8u) != 0u) && !hit.is_cut) {
        let nf = clamp(q16_to_f32(s_cell.q1.z), 1.0, 16.0);
        tri_grain = eval_triplanar_grain(hit.local_p, n, nf, metallic, wire_w);
        var groove_w = tri_grain.y;
        var t_ax = safe_norm3(cross(select(vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(1.0, 0.0, 0.0), abs(n.y) > 0.85), n), vec3<f32>(1.0, 0.0, 0.0));
        if (diff_field) {
            let j2 = eval_diff_jet2(p, n);
            ridge_polish = clamp(j2.H * 0.35, 0.0, 0.65);
            crevice_patina = clamp(-j2.H * 0.45, 0.0, 0.72);
            field_aniso = clamp(j2.aniso * 0.25, 0.0, 1.0);
            let p_curv = dot(hit.local_p, j2.e1);
            let p_orth = dot(hit.local_p, cross(n, j2.e1));
            let curv_flow = sin(p_curv * 44.0 + sin(p_orth * 12.0) * 1.4);
            groove_w = mix(tri_grain.y, curv_flow, 0.68);
            t_ax = j2.e1;
        }
        let bump_amp = relief * (0.085 + 0.045 * field_aniso);
        n_eff = safe_norm3(n + t_ax * (groove_w * bump_amp), n);
    }

    let eff_rough = clamp(mix(rough, style_rough, 0.35) + tri_grain.x - ridge_polish * 0.14 + crevice_patina * 0.12, 0.025, 1.0);

    let key_dir = vec3<f32>(-0.5145, 0.7503, -0.4150);
    let rim_dir = vec3<f32>( 0.7420, 0.3610,  0.5650);

    let ao = calc_ao(p, n_eff, xray_on, xray_plane);
    let sh = calc_soft_shadow(p + n_eff * 0.018, key_dir, 11.0, xray_on, xray_plane);

    let ndv = clamp(dot(n_eff, -rd), 0.0, 1.0);
    let ndl = clamp(dot(n_eff, key_dir), 0.0, 1.0);
    let ndr = clamp(dot(n_eff, rim_dir), 0.0, 1.0);
    let refl_d = reflect(rd, n_eff);

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
    let spec_key = pow(max(clamp(dot(n_eff, h_key), 0.0, 1.0), 1e-6), spec_pow) * mix(1.45, 0.22, eff_rough) * sh;
    let h_rim = safe_norm3(rim_dir - rd, vec3<f32>(0.0, 1.0, 0.0));
    let spec_rim = pow(max(clamp(dot(n_eff, h_rim), 0.0, 1.0), 1e-6), mix(72.0, 6.0, eff_rough)) * 0.55;

    let env_refl = select(eval_studio_env(refl_d, eff_rough),
                        mix(eval_studio_env(refl_d, eff_rough), sample_rc1_trilinear(p + n_eff * 0.06, refl_d).rgb, clamp(1.0 - eff_rough * 0.55, 0.25, 0.85)),
                        rc_on) * (0.45 + 0.55 * ao);
    let rc_irr = select(vec3<f32>(0.0), sample_rc0_irradiance(p, n_eff), rc_on);
    let spec_total = env_refl * fres * (1.0 + ridge_polish * 0.45)
                   + vec3<f32>(1.00, 0.94, 0.86) * spec_key * (vec3<f32>(0.35) + 0.65 * f0_rgb)
                   + vec3<f32>(0.35, 0.82, 1.15) * spec_rim * f0_rgb;

    var col = vec3<f32>(0.0);

    if (eta > 1.02 && metallic < 0.5) {
        // True Snell Secondary Refraction + Internal Optical Thickness + Chromatic Dispersion
        let eta_inv = 1.0 / max(eta, 1.01);
        let refr_raw = refract(rd, n_eff, eta_inv);
        let refr_d = safe_norm3(select(refr_raw, refl_d, dot(refr_raw, refr_raw) < 1e-5), rd);

        let p_enter = p - n_eff * 0.016;
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
        let wrap_key = clamp(dot(n_eff, key_dir) * 0.5 + 0.5, 0.0, 1.0) * (0.42 + 0.58 * sh);
        let jewel_tint = mix(beer, base_tint, 0.52);
        let sss = jewel_tint * (mie * (back_lit * 0.98 + wrap_key * 0.65 + ao * 0.26) + (0.14 + 0.20 * wrap_key) * ao);

        let cavity_glow = select(0.0, 0.18, cavity_crossings >= 1.5) * (1.0 - ndv * 0.45);
        let lattice = sin(p.x * 26.0) * sin(p.y * 26.0) * sin(p.z * 26.0);
        let wire_term = (smoothstep(0.72, 0.96, abs(lattice)) * wire_w * 0.18) + tri_grain.z * 0.22;

        let transmitted = (refr_scene + int_env) * beer * (0.76 + 0.34 * wrap_key)
                        + sss
                        + jewel_tint * rc_irr * (0.45 * (1.0 - metallic))
                        + accent_tint * (cavity_glow + wire_term) * beer;
        col = transmitted * (vec3<f32>(1.0) - fres) + spec_total;
    } else {
        // Brushed Titanium / Aurum Gold / Conductor & Ceramic PBR + Radiance Cascades Indirect Diff/Spec + 2-Jet Patina
        let diff_wrap = clamp(ndl * 0.75 + 0.25, 0.0, 1.0) * (0.30 + 0.70 * sh);
        let diff_albedo = metal_albedo * (1.0 - crevice_patina * 0.42);
        let diff = diff_albedo * (1.0 - metallic * 0.85) * diff_wrap * ao * 0.55;
        let rc_bounce = diff_albedo * rc_irr * (0.85 - metallic * 0.35) * ao;
        let brush_mod = 1.0 + tri_grain.y * 0.18 * metallic;
        let aniso_band = pow(clamp(1.0 - abs(n_eff.y - 0.15), 0.0, 1.0), 4.0) * metallic * 0.16 * brush_mod;
        let rim_metal = diff_albedo * pow(1.0 - ndv, 3.0) * (0.25 + 0.55 * ndr) * metallic * ao;
        col = diff * brush_mod + rc_bounce + spec_total + metal_albedo * aniso_band + rim_metal;
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

    // Pixel-Exact 3D Cross-Section Tomography, Isolines, DFM Wall-Thickness & Boolean Interference (A ^ B)
    let iso_pitch = max(q16_to_f32(sec.q2.z), 0.05);
    let min_wall  = max(q16_to_f32(sec.q2.y), 0.02);
    let has_inter = (hit.raw_d < -0.001) && (hit.second_raw_d < -0.001);
    let inter_seam = ((insp_flags & 4u) != 0u) && !hit.is_cut && (hit.second_raw_d < 0.018);

    if (xray_on || sec_mode != 0u) {
        if (hit.is_cut) {
            let sn = select(vec3<f32>(1.0, 0.0, 0.0), get_sec_normal(), sec_mode != 0u);
            let t1 = safe_norm3(cross(select(vec3<f32>(0.0, 1.0, 0.0), vec3<f32>(1.0, 0.0, 0.0), abs(sn.y) > 0.85), sn), vec3<f32>(0.0, 1.0, 0.0));
            let t2 = cross(sn, t1);
            let uv_cut = vec2<f32>(dot(p, t1), dot(p, t2));
            let hatch = 0.5 + 0.5 * sin((uv_cut.x - uv_cut.y) * 56.0);
            let wall_edge = 1.0 - smoothstep(0.0, 0.028, abs(hit.raw_d));

            var cut_col = mix(vec3<f32>(0.02, 0.48, 0.56), vec3<f32>(0.08, 0.92, 0.84), 0.35 + 0.35 * hatch + 0.30 * ao);

            // SDF Internal Isolines
            if ((insp_flags & 1u) != 0u) {
                let iso_phase = fract(abs(hit.raw_d) / iso_pitch);
                let iso_d = min(iso_phase, 1.0 - iso_phase) * iso_pitch;
                let iso_ring = 1.0 - smoothstep(0.0, 0.014, iso_d);
                cut_col = mix(cut_col, vec3<f32>(0.32, 1.00, 0.98), iso_ring * 0.65);
            }

            // DFM Minimum Wall-Thickness Probe along -grad(raw_d)
            if ((insp_flags & 2u) != 0u) {
                let rg = eval_raw_grad(p);
                let opp_d = map_scene_raw_best(p - rg * min_wall);
                if (opp_d > -0.002) {
                    let dfm_heat = clamp((opp_d + 0.025) / 0.06, 0.0, 1.0);
                    let dfm_col = mix(vec3<f32>(1.00, 0.68, 0.12), vec3<f32>(1.00, 0.18, 0.24), dfm_heat);
                    cut_col = mix(cut_col, dfm_col, 0.72);
                }
            }

            // Multi-Instance Boolean Interference A ^ B
            if ((insp_flags & 4u) != 0u && has_inter) {
                let cross_h = 0.5 + 0.5 * sin((uv_cut.x + uv_cut.y) * 68.0) * sin((uv_cut.x - uv_cut.y) * 68.0);
                let inter_col = mix(vec3<f32>(0.96, 0.14, 0.28), vec3<f32>(1.00, 0.82, 0.16), cross_h);
                cut_col = mix(cut_col, inter_col, 0.86);
            }

            col = mix(col * 0.50, cut_col, 0.68);
            col = col + vec3<f32>(0.18, 1.15, 0.96) * wall_edge * 0.82;
        } else {
            let clip_abs = abs(eval_section_clip_val(p, xray_on, xray_plane));
            let plane_rim = 1.0 - smoothstep(0.0, 0.022, clip_abs);
            col = col + vec3<f32>(0.10, 1.05, 0.90) * plane_rim * 0.88;
        }
    }
    if (inter_seam) {
        let seam_w = 1.0 - smoothstep(0.0, 0.018, max(hit.second_raw_d, 0.0));
        col = mix(col, vec3<f32>(1.00, 0.34, 0.18), seam_w * 0.85);
    }

    // Sub-Frame Huygens Elastic Contact Shockwave Overlay (arena[255] Slot 255)
    if (subframe_lock) {
        let ps = arena[255];
        let pflags = (ps.q0.w >> 16u) & 0xFFFFu;
        if ((pflags & 4u) != 0u) {
            let toi = clamp(q16_to_f32(ps.q1.y), 0.0, 1.0);
            let cpos = vec3<f32>(q16_to_f32(ps.q2.x), q16_to_f32(ps.q2.y), q16_to_f32(ps.q2.z));
            let r_wave = max(1.0 - toi, 0.05) * 0.85;
            let d_ring = abs(length(p - cpos) - r_wave);
            let wave = (1.0 - smoothstep(0.0, 0.045, d_ring)) * exp(-d_ring * 18.0) * (1.0 - 0.5 * toi);
            col = col + vec3<f32>(0.25, 0.95, 1.25) * wave * 0.92;
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
    var rd = safe_norm3(right * uv_cam.x + up_v * uv_cam.y + fwd * fov_z, vec3<f32>(0.0, 0.0, 1.0));
    let px_ang = 1.0 / max(fh * fov_z, 1.0);

    let hdr_flags     = arena[0].q3.z;
    let xray_on       = (hdr_flags & 2u) != 0u;
    let trc_safe      = (hdr_flags & 4u) != 0u;
    let cb_geom       = (hdr_flags & 8u) != 0u;
    let studio_gi     = (hdr_flags & 16u) != 0u;
    let rc_on         = (hdr_flags & 32u) != 0u;
    let diff_field    = (hdr_flags & 64u) != 0u;
    let subframe_lock = (hdr_flags & 128u) != 0u;
    let cine_on       = (hdr_flags & 256u) != 0u;
    let metric_on     = (hdr_flags & 512u) != 0u;
    let shells_on     = (hdr_flags & 1024u) != 0u;
    let xray_plane    = q16_to_f32(arena[0].q3.x);
    let selected_id   = arena[0].q3.y;
    let time_s        = f32(arena[0].q1.y) * 0.0166667;

    let reg = arena[2];
    let metric_phi   = clamp(q16_to_f32(reg.q1.x), -0.45, 0.48);
    let lorentz_beta = clamp(q16_to_f32(reg.q1.y), -0.90, 0.90);
    let hitstop_f    = bitcast<i32>(reg.q1.w);
    let graze_w      = clamp(q16_to_f32(reg.q2.x), 0.04, 0.80);

    var lens_center = tgt;
    let inst_cnt_m = min(arena[0].q1.z, 32u);
    if (selected_id >= 1u && selected_id <= inst_cnt_m) {
        let sel_i = arena[40u + (selected_id - 1u)];
        lens_center = vec3<f32>(q16_to_f32(sel_i.q1.x), q16_to_f32(sel_i.q1.y), q16_to_f32(sel_i.q1.z));
    }

    if (metric_on && abs(lorentz_beta) > 1e-4) {
        let rdx = dot(rd, fwd);
        let denom_b = max(1.0 + lorentz_beta * rdx, 0.08);
        let boost_x = (rdx + lorentz_beta) / denom_b;
        let perp = rd - fwd * rdx;
        let perp_sc = sqrt(max(1.0 - lorentz_beta * lorentz_beta, 0.05)) / denom_b;
        rd = safe_norm3(fwd * boost_x + perp * perp_sc, rd);
    }

    // Base background (exact parity when studio_gi == false for verifyGpuAlignment)
    let sky_col = eval_sky_dome(rd);
    var bg = eval_backdrop_ray(eye, rd, px_ang);

    // 2. Primary 3D SDF / Geodesic Metric Raymarch (Exact CPU Q16.16 parity when !studio_gi && !metric_on)
    var col = bg;
    var hit_found = false;
    var trapped_horizon = false;
    var einstein_glow : f32 = 0.0;
    var t : f32 = 0.125;
    var cur_p  = eye + rd * t;
    var cur_rd = rd;
    let max_steps : u32 = select(64u, 80u, studio_gi || metric_on);
    let hit_eps   : f32 = select(0.0078125, 0.0020, studio_gi);
    let min_step  : f32 = select(0.0078125, 0.0030, studio_gi);
    let max_t     : f32 = select(32.0, 28.0, studio_gi);
    var min_edge_d : f32 = 1e5;
    var edge_p     : vec3<f32> = eye;

    for (var s : u32 = 0u; s < max_steps; s = s + 1u) {
        if (t >= max_t) { break; }
        let p = cur_p;
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
                hit_info, p, n, cur_rd, px_ang,
                xray_on, xray_plane, trc_safe, cb_geom, studio_gi,
                rc_on, diff_field, subframe_lock, selected_id, time_s
            );
            hit_found = true;
            break;
        }
        let step_d = max(d, min_step);
        if (metric_on) {
            let r_vec = cur_p - lens_center;
            let r_len = length(r_vec);
            if (abs(metric_phi) > 1e-4) {
                let rs_ring = 3.0 * abs(metric_phi);
                let ring_prox = exp(-abs(r_len - rs_ring) * 14.0);
                einstein_glow = einstein_glow + ring_prox * 0.032 * abs(metric_phi) * 4.0;
                let deflect = (2.0 * metric_phi * step_d) / max(r_len * r_len * r_len, 0.08);
                cur_rd = safe_norm3(cur_rd - r_vec * deflect, cur_rd);
            }
            if (metric_phi > 0.20 && r_len < 2.0 * metric_phi) {
                trapped_horizon = true;
                col = vec3<f32>(0.002, 0.004, 0.012);
                break;
            }
            cur_p = cur_p + cur_rd * step_d;
            t = t + step_d;
        } else {
            t = t + step_d;
            cur_p = eye + rd * t;
        }
    }
    if (!hit_found && !trapped_horizon && metric_on) {
        col = eval_backdrop_ray(eye, cur_rd, px_ang);
    }

    // Studio Floor GI: Contact AO + Soft Shadows + Radiance Cascades Floor Bounce + Planar Reflection
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
            if (rc_on) {
                let floor_gi = sample_rc0_irradiance(fp, vec3<f32>(0.0, 1.0, 0.0));
                fcol = fcol + floor_gi * 0.22 * ao_f;
            }

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
            let elit = shade_direct_hit(ehit, edge_p, en, cur_rd, trc_safe, time_s);
            col = mix(col, elit, edge_cov * 0.72);
        }
    }

    // Relativistic Photon Ring + Doppler Tint (when metric_on)
    if (metric_on) {
        let dop_shift = vec3<f32>(1.0 - lorentz_beta * 0.25, 1.0, 1.0 + lorentz_beta * 0.38);
        col = col * dop_shift + vec3<f32>(0.42, 0.78, 1.25) * clamp(einstein_glow, 0.0, 1.2);
    }

    // Concentric SDF Graze Contour + Hitstop High-Contrast Punch (when shells_on)
    if (shells_on) {
        if (!hit_found && min_edge_d < graze_w * 0.38) {
            let sh_a = 1.0 - smoothstep(0.0, 0.018, abs(min_edge_d - graze_w * 0.22));
            col = col + vec3<f32>(0.18, 0.94, 1.00) * sh_a * 0.65;
        }
        if (hitstop_f > 0) {
            let vig = clamp(length(norm_xy) - 0.55, 0.0, 0.75) * 0.65;
            col = mix(col, vec3<f32>(0.01, 0.015, 0.025), vig) * 1.18;
        }
    }

    // Thin-Lens CineCamera Optics: CoC Depth-of-Field, Blue Anamorphic Streak & 2.39:1 Letterbox
    if (cine_on) {
        let fstop = clamp(q16_to_f32(cam.q2.z), 1.4, 22.0);
        let focus_d = max(q16_to_f32(cam.q2.w), 0.5);
        let exposure = clamp(q16_to_f32(cam.q3.x), 0.25, 4.0);
        let depth_s = select(18.0, t, hit_found);
        let coc = clamp(abs(depth_s - focus_d) / max(depth_s, 0.5) * (focal_mm / (fstop * 28.0)), 0.0, 0.65);
        col = mix(col, bg * 0.75 + col * 0.25, coc * 0.45) * exposure;

        let inst_cnt_f = min(arena[0].q1.z, 32u);
        for (var fi : u32 = 0u; fi < inst_cnt_f; fi = fi + 1u) {
            let finst = arena[40u + fi];
            let ffl = (finst.q3.x >> 16u) & 0xFFFFu;
            if ((ffl & 1u) == 0u) { continue; }
            if (finst.q2.w <= 800u) { continue; }
            let ipos = vec3<f32>(q16_to_f32(finst.q1.x), q16_to_f32(finst.q1.y), q16_to_f32(finst.q1.z));
            let rel_e = ipos - eye;
            let zc = dot(rel_e, fwd);
            if (zc > 0.25) {
                let em_x = (dot(rel_e, right) * fov_z) / (zc * aspect * 0.5);
                let em_y = -(dot(rel_e, up_v) * fov_z) / (zc * 0.5);
                let sy_k = exp(-abs(norm_xy.y - em_y) * 40.0);
                let sx_k = exp(-abs(norm_xy.x - em_x) * 2.0);
                let f_gain = clamp(f32(finst.q2.w - 800u) * 0.0008, 0.15, 0.85);
                col = col + vec3<f32>(0.10, 0.45, 1.00) * sy_k * sx_k * f_gain;
            }
        }

        let scope_h = clamp(aspect / 2.39, 0.52, 0.88);
        if (abs(norm_xy.y) > scope_h) {
            col = col * 0.04;
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

// ============================================================================
// 5. Warp-Scale FLOW "FIR1" Evaluator & Parallel Witness Hunter (Zero 64-bit Ext)
// ============================================================================

fn w64_mask(w : u32) -> vec2<u32> {
    if (w == 0u) { return vec2<u32>(0u, 0u); }
    if (w < 32u) { return vec2<u32>((1u << w) - 1u, 0u); }
    if (w == 32u) { return vec2<u32>(0xFFFFFFFFu, 0u); }
    if (w < 64u) { return vec2<u32>(0xFFFFFFFFu, (1u << (w - 32u)) - 1u); }
    return vec2<u32>(0xFFFFFFFFu, 0xFFFFFFFFu);
}

fn w64_shl(v : vec2<u32>, sh : u32) -> vec2<u32> {
    if (sh == 0u) { return v; }
    if (sh >= 64u) { return vec2<u32>(0u, 0u); }
    if (sh < 32u) { return vec2<u32>(v.x << sh, (v.y << sh) | (v.x >> (32u - sh))); }
    return vec2<u32>(0u, v.x << (sh - 32u));
}

fn w64_shr(v : vec2<u32>, sh : u32) -> vec2<u32> {
    if (sh == 0u) { return v; }
    if (sh >= 64u) { return vec2<u32>(0u, 0u); }
    if (sh < 32u) { return vec2<u32>((v.x >> sh) | (v.y << (32u - sh)), v.y >> sh); }
    return vec2<u32>(v.y >> (sh - 32u), 0u);
}

fn w64_add(a : vec2<u32>, b : vec2<u32>) -> vec2<u32> {
    let lo = a.x + b.x;
    let cy = select(0u, 1u, lo < a.x);
    return vec2<u32>(lo, a.y + b.y + cy);
}

fn w64_sub(a : vec2<u32>, b : vec2<u32>) -> vec2<u32> {
    let lo = a.x - b.x;
    let br = select(0u, 1u, a.x < b.x);
    return vec2<u32>(lo, a.y - b.y - br);
}

fn w64_mul128_shr(a : vec2<u32>, b : vec2<u32>, sh : u32) -> vec2<u32> {
    if (sh >= 128u) { return vec2<u32>(0u, 0u); }
    let al = array<u32, 4>(a.x & 0xFFFFu, a.x >> 16u, a.y & 0xFFFFu, a.y >> 16u);
    let bl = array<u32, 4>(b.x & 0xFFFFu, b.x >> 16u, b.y & 0xFFFFu, b.y >> 16u);
    var w = array<u32, 8>(0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u);
    for (var i : u32 = 0u; i < 4u; i = i + 1u) {
        for (var j : u32 = 0u; j < 4u; j = j + 1u) {
            let k = i + j;
            let prod = al[i] * bl[j];
            let s0 = w[k] + (prod & 0xFFFFu);
            w[k] = s0 & 0xFFFFu;
            let s1 = w[k + 1u] + (prod >> 16u) + (s0 >> 16u);
            w[k + 1u] = s1 & 0xFFFFu;
            var cy = s1 >> 16u;
            var kk = k + 2u;
            while (cy > 0u && kk < 8u) {
                let sc = w[kk] + cy;
                w[kk] = sc & 0xFFFFu;
                cy = sc >> 16u;
                kk = kk + 1u;
            }
        }
    }
    let lo64 = vec2<u32>(w[0] | (w[1] << 16u), w[2] | (w[3] << 16u));
    let hi64 = vec2<u32>(w[4] | (w[5] << 16u), w[6] | (w[7] << 16u));
    if (sh == 0u) { return lo64; }
    if (sh < 64u) {
        return vec2<u32>(w64_shr(lo64, sh).x | w64_shl(hi64, 64u - sh).x,
                         w64_shr(lo64, sh).y | w64_shl(hi64, 64u - sh).y);
    }
    return w64_shr(hi64, sh - 64u);
}

fn w64_gf8_mul_0xb(a_in : u32, b_in : u32) -> u32 {
    var a = a_in & 7u;
    let b = b_in & 7u;
    var res : u32 = 0u;
    for (var i : u32 = 0u; i < 3u; i = i + 1u) {
        if (((b >> i) & 1u) != 0u) { res = res ^ a; }
        a = a << 1u;
        if ((a & 8u) != 0u) { a = a ^ 0xBu; }
    }
    return res & 7u;
}

fn w64_gf2m_mul(a_in : vec2<u32>, b_in : vec2<u32>, deg : u32, irred : vec2<u32>) -> vec2<u32> {
    if (deg == 0u || deg > 32u) { return vec2<u32>(0u, 0u); }
    if (deg == 6u && irred.x == 0u && irred.y == 0u) {
        let x0 = a_in.x & 7u;
        let x1 = (a_in.x >> 3u) & 7u;
        let y0 = b_in.x & 7u;
        let y1 = (b_in.x >> 3u) & 7u;
        let p0 = w64_gf8_mul_0xb(x0, y0);
        let p1 = w64_gf8_mul_0xb(x1, y1);
        let pm = w64_gf8_mul_0xb(x0 ^ x1, y0 ^ y1);
        return vec2<u32>((p0 ^ p1) | ((pm ^ p0) << 3u), 0u);
    }
    let mask = w64_mask(deg);
    let top  = w64_shl(vec2<u32>(1u, 0u), deg);
    var a    = a_in & mask;
    let b    = b_in & mask;
    var res  = vec2<u32>(0u, 0u);
    for (var i : u32 = 0u; i < 32u; i = i + 1u) {
        if (i >= deg) { break; }
        let bit_i = w64_shr(b, i).x & 1u;
        if (bit_i != 0u) { res = res ^ a; }
        a = w64_shl(a, 1u);
        if ((a.x & top.x) != 0u || (a.y & top.y) != 0u) {
            a = a ^ irred;
        }
    }
    return res & mask;
}

fn w64_gf2m_inv(a_in : vec2<u32>, deg : u32, irred : vec2<u32>) -> vec2<u32> {
    if (deg == 0u || deg > 32u) { return vec2<u32>(0u, 0u); }
    let mask = w64_mask(deg);
    let a    = a_in & mask;
    if (a.x == 0u && a.y == 0u) { return vec2<u32>(0u, 0u); }
    var exp  = w64_sub(mask, vec2<u32>(1u, 0u));
    var res  = vec2<u32>(1u, 0u);
    var base = a;
    for (var step : u32 = 0u; step < 33u; step = step + 1u) {
        if (exp.x == 0u && exp.y == 0u) { break; }
        if ((exp.x & 1u) != 0u) {
            res = w64_gf2m_mul(res, base, deg, irred);
        }
        base = w64_gf2m_mul(base, base, deg, irred);
        exp = w64_shr(exp, 1u);
    }
    return res & mask;
}

fn arena_read_u32(cell_base : u32, idx : u32) -> u32 {
    let cell = arena[cell_base + (idx >> 4u)];
    let qi = (idx >> 2u) & 3u;
    var q = cell.q0;
    if (qi == 1u) { q = cell.q1; }
    else if (qi == 2u) { q = cell.q2; }
    else if (qi == 3u) { q = cell.q3; }
    return vec4_u32_lane(q, idx & 3u);
}

fn arena_read_w64(cell_base : u32, idx : u32) -> vec2<u32> {
    let cell = arena[cell_base + (idx >> 3u)];
    let qi = (idx >> 1u) & 3u;
    var q = cell.q0;
    if (qi == 1u) { q = cell.q1; }
    else if (qi == 2u) { q = cell.q2; }
    else if (qi == 3u) { q = cell.q3; }
    return select(vec2<u32>(q.x, q.y), vec2<u32>(q.z, q.w), (idx & 1u) != 0u);
}

fn fmix32(h_in : u32) -> u32 {
    var h = h_in;
    h = h ^ (h >> 16u);
    h = h * 0x85EBCA6Bu;
    h = h ^ (h >> 13u);
    h = h * 0xC2B2AE35u;
    h = h ^ (h >> 16u);
    return h;
}

@compute @workgroup_size(64, 1, 1)
fn bloom_fir1_hunt(
    @builtin(global_invocation_id) gid : vec3<u32>
) {
    if (gid.x == 0xFFFFFFFFu) { textureStore(out_tex, vec2<i32>(0, 0), vec4<f32>(0.0)); }
    let tid = gid.x;
    let hdr_q0 = arena[0].q0;
    let hdr_q1 = arena[0].q1;
    let hdr_q2 = arena[0].q2;
    let hdr_q3 = arena[0].q3;

    let num_vals     = min(hdr_q0.y, 128u);
    let num_inputs   = min(hdr_q0.z, 32u);
    let num_outputs  = min(hdr_q0.w, 32u);
    let num_ops      = min(hdr_q1.x, 512u);
    let num_lut      = min(hdr_q1.y, 1736u);
    let total_trials = hdr_q1.z;
    let target_mode  = hdr_q1.w;
    let seed_lo      = hdr_q2.x;
    let seed_hi      = hdr_q2.y;
    let target_val   = vec2<u32>(hdr_q2.z, hdr_q2.w);
    let vary_mask    = hdr_q3.x;
    let sweep_mode   = hdr_q3.y;
    let num_threads  = min(hdr_q3.z, 32768u);
    let trial_base   = hdr_q3.w;

    if (hdr_q0.x != 0x46495248u || num_threads == 0u || tid >= num_threads) {
        return;
    }

    var best_local : u32 = 0xFFFFFFFFu;
    var hit_acc    : u32 = 0u;
    var xor_lo     : u32 = 0u;
    var xor_hi     : u32 = 0u;

    var local_tid : u32 = tid;
    while (local_tid < total_trials) {
        let gtid = trial_base + local_tid;
        var env : array<vec2<u32>, 128>;
        for (var i : u32 = 0u; i < num_inputs; i = i + 1u) {
            let w = arena_read_u32(7u, i);
            let wm = w64_mask(w);
            var iv = arena_read_w64(1u, i);
            if (((vary_mask >> min(i, 31u)) & 1u) != 0u) {
                if (sweep_mode == 1u) {
                    let h0 = fmix32(seed_lo ^ gtid ^ (i * 0x9E3779B9u));
                    let h1 = fmix32(seed_hi ^ (gtid * 0x85EBCA6Bu) ^ (i * 0xC2B2AE35u) ^ h0);
                    iv = vec2<u32>(h0, h1);
                } else {
                    if (i == 0u) {
                        iv = w64_add(iv, vec2<u32>(seed_lo + gtid, select(0u, 1u, (seed_lo + gtid) < seed_lo) + seed_hi));
                    } else {
                        iv = w64_add(iv, vec2<u32>(gtid ^ (i * 0x9E3779B9u), 0u));
                    }
                }
            }
            env[i] = iv & wm;
        }

        for (var op_i : u32 = 0u; op_i < num_ops; op_i = op_i + 1u) {
            let op_c   = mesh_buf[op_i];
            let kind   = op_c.q0.x & 0xFFFFu;
            let nins   = (op_c.q0.x >> 16u) & 0xFFFFu;
            let oid    = op_c.q0.y;
            let ins    = array<u32, 6>(op_c.q0.z, op_c.q0.w, op_c.q1.x, op_c.q1.y, op_c.q1.z, op_c.q1.w);
            let lut_off = op_c.q2.x;
            let lut_len = op_c.q2.y;
            let p0     = vec2<u32>(op_c.q2.z, op_c.q2.w);
            let p1     = vec2<u32>(op_c.q3.x, op_c.q3.y);
            let p2_lo  = op_c.q3.z;

            if (oid >= num_vals) { continue; }
            let omask = w64_mask(arena_read_u32(7u, oid));
            let a0 = select(vec2<u32>(0u, 0u), env[min(ins[0], 127u)], nins >= 1u);
            let a1 = select(vec2<u32>(0u, 0u), env[min(ins[1], 127u)], nins >= 2u);
            let a2 = select(vec2<u32>(0u, 0u), env[min(ins[2], 127u)], nins >= 3u);
            var r = vec2<u32>(0u, 0u);

            if (kind == 1u) {
                r = p0 & omask;
            } else if (kind == 2u) {
                r = w64_shr(a0, p0.x) & omask;
            } else if (kind == 3u) {
                var pos : u32 = 0u;
                for (var j : u32 = 0u; j < 6u; j = j + 1u) {
                    if (j >= nins) { break; }
                    let iv_id = min(ins[j], 127u);
                    let iw = arena_read_u32(7u, iv_id);
                    let piece = env[iv_id] & w64_mask(iw);
                    if (pos < 64u) {
                        r = r | w64_shl(piece, pos);
                    }
                    pos = pos + iw;
                }
                r = r & omask;
            } else if (kind == 4u) {
                r = (a0 ^ a1) & omask;
            } else if (kind == 5u) {
                r = (a0 & a1) & omask;
            } else if (kind == 6u) {
                r = (~a0) & omask;
            } else if (kind == 7u) {
                r = vec2<u32>(select(0u, 1u, a0.x != 0u || a0.y != 0u), 0u);
            } else if (kind == 8u) {
                r = w64_add(a0, a1) & omask;
            } else if (kind == 9u) {
                r = w64_sub(a0, a1) & omask;
            } else if (kind == 10u) {
                let s_sum = p0.x + p1.x;
                let sh = select(0u, s_sum - p2_lo, s_sum > p2_lo);
                r = w64_mul128_shr(a0, a1, sh) & omask;
            } else if (kind == 11u) {
                let s_sum = 2u * p0.x;
                let sh = select(0u, s_sum - p2_lo, s_sum > p2_lo);
                r = w64_mul128_shr(a0, a0, sh) & omask;
            } else if (kind == 12u) {
                r = w64_gf2m_mul(a0, a1, p0.x, p1) & omask;
            } else if (kind == 13u) {
                r = w64_gf2m_inv(a0, p0.x, p1) & omask;
            } else if (kind == 14u) {
                let addr = a0.x;
                if (a0.y == 0u && addr < lut_len && (lut_off + addr) < num_lut) {
                    r = arena_read_w64(39u, lut_off + addr) & omask;
                }
            } else if (kind == 15u) {
                r = select(a1, a2, (a0.x & 1u) != 0u) & omask;
            } else if (kind == 16u) {
                let sh = select(64u, a1.x, a1.y == 0u && a1.x < 64u);
                r = w64_shl(a0, sh) & omask;
            } else if (kind == 17u) {
                let sh = select(64u, a1.x, a1.y == 0u && a1.x < 64u);
                r = w64_shr(a0, sh) & omask;
            } else if (kind == 18u) {
                r = vec2<u32>(select(0u, 1u, a0.x == 0u && a0.y == 0u), 0u);
            }
            env[oid] = r;
        }

        var out0 = vec2<u32>(0u, 0u);
        if (num_outputs > 0u) {
            let oid0 = min(arena_read_u32(5u, 0u), 127u);
            out0 = env[oid0];
        }
        xor_lo = xor_lo ^ out0.x;
        xor_hi = xor_hi ^ out0.y;

        var matched = false;
        if (target_mode == 0u) {
            matched = (out0.x == target_val.x) && (out0.y == target_val.y);
        } else if (target_mode == 1u) {
            matched = (out0.x != target_val.x) || (out0.y != target_val.y);
        } else {
            matched = true;
        }
        if (matched) {
            hit_acc = hit_acc + 1u;
            if (local_tid < best_local) {
                best_local = local_tid;
            }
        }
        if (total_trials - local_tid <= num_threads) {
            break;
        }
        local_tid = local_tid + num_threads;
    }

    rc_buf[tid] = vec4<f32>(
        bitcast<f32>(best_local),
        bitcast<f32>(hit_acc),
        bitcast<f32>(xor_lo),
        bitcast<f32>(xor_hi)
    );
}
