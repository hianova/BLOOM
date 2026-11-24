#include <metal_stdlib>
using namespace metal;

struct BloomVtx12 {
    ushort px, py, pz;
    char oct_x, oct_y;
    uchar u, v, joint_id, mat_id;
};

struct BloomMeshletHeader32 {
    uint vtx_offset, idx_offset;
    ushort vtx_cnt, tri_cnt, obj_id, flags;
    half bs_x, bs_y, bs_z, bs_r;
    char cone_x, cone_y, cone_z, cone_c;
    uint aabb_scale_tag;
};

struct BloomObjectState48 {
    float px, py, pz;
    half qx, qy, qz, qw;
    half sx, sy, sz, dof0, dof1, dof2;
    uint rgba8_mat;
    half cp0, cp1, cp2, cp3;
    uint flags;
};

struct BloomAnalyticPrim32 {
    float cx, cy, cz, radius;
    half ax, ay, az, half_len;
    uint rgba8, prim_kind_flags;
};

struct BloomQuadHot {
    ushort b0, b1, b2, b3;
    uint rgba8_fill, rgba8_border;
    uchar r0, r1, r2, r3;
    uchar bw0, bw1, bw2, bw3;
    ushort clip_id, style_id;
    uint z_flags;
};

struct BloomClipRect { float4 rect; };

struct BloomFrameUniforms {
    float vp_w, vp_h, focal, frustum_half_tan;
    packed_float3 cam_pos; float near_z;
    packed_float3 cam_right; float far_z;
    packed_float3 cam_up; uint total_meshlets;
    packed_float3 cam_fwd; uint prim_count;
};

struct BloomGroundParams {
    uint vp_w, vp_h, pick_x, pick_y;
    uint max_slots, half_shift;
    float pos_scale;
    uint pad0;
};

struct BloomGroundedAtomic {
    atomic_uint visible_px, min_x, min_y, max_x, max_y, best_anchor, part_sample, min_depth_u32;
};

struct BloomPickGpuResult {
    uint ground_id, depth_bits, px, py;
};

static_assert(sizeof(BloomGroundParams) == 32, "MSL BloomGroundParams");
static_assert(sizeof(BloomGroundedAtomic) == 32, "MSL BloomGroundedAtomic");
static_assert(sizeof(BloomPickGpuResult) == 16, "MSL BloomPickGpuResult");

inline float3 rotate_quat(float4 q, float3 v) {
    float3 t = 2.0f * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

inline float3 to_view(float3 wp, constant BloomFrameUniforms &u) {
    float3 d = wp - float3(u.cam_pos);
    return float3(dot(d, float3(u.cam_right)), dot(d, float3(u.cam_up)), dot(d, float3(u.cam_fwd)));
}

inline float4 project_persp(float3 vp, constant BloomFrameUniforms &u) {
    float z = max(vp.z, 0.25f);
    float aspect = u.vp_w / u.vp_h;
    float nx = (vp.x * u.focal) / (z * aspect);
    float ny = (vp.y * u.focal) / z;
    float nz = clamp(0.20f + (1.0f - (2.0f / z)) * 0.78f, 0.20f, 0.98f);
    return float4(nx, ny, nz, 1.0f);
}

inline float2 to_ndc_2d(float2 px, constant BloomFrameUniforms &u) {
    return float2((px.x / u.vp_w) * 2.0f - 1.0f, 1.0f - (px.y / u.vp_h) * 2.0f);
}

struct MeshIdVertOut {
    float4 pos [[position]];
    float clip_d [[clip_distance]] [1];
    uint gid [[flat]];
};

struct MeshIdFragIn {
    float4 pos [[position]];
    uint gid [[flat]];
};

vertex MeshIdVertOut v_meshlet_id(uint vid [[vertex_id]], uint iid [[instance_id]],
                                  device const uint *surviving_ids [[buffer(0)]],
                                  device const BloomMeshletHeader32 *hdrs [[buffer(1)]],
                                  device const BloomVtx12 *verts [[buffer(2)]],
                                  device const uchar *local_idx [[buffer(3)]],
                                  device const BloomObjectState48 *states [[buffer(4)]],
                                  constant BloomFrameUniforms &u [[buffer(5)]]) {
    uint m_id = surviving_ids[iid];
    BloomMeshletHeader32 m = hdrs[m_id];
    MeshIdVertOut o;
    if (vid >= uint(m.tri_cnt) * 3u) {
        o.pos = float4(0.0f, 0.0f, -2.0f, 1.0f);
        o.clip_d[0] = -1.0f;
        o.gid = 0u;
        return o;
    }
    BloomVtx12 v = verts[m.vtx_offset + uint(local_idx[m.idx_offset + vid])];
    BloomObjectState48 st = states[m.obj_id];
    float3 lp = (float3(-1.5f) + float3(float(v.px), float(v.py), float(v.pz)) * (3.0f / 65535.0f)) *
                float3(float(st.sx), float(st.sy), float(st.sz));
    uint jmod = uint(v.joint_id % 3u);
    float dof = float(jmod == 0u ? st.dof0 : (jmod == 1u ? st.dof1 : st.dof2));
    lp.y += sin(dof + lp.x) * 0.08f;
    float4 q = float4(float(st.qx), float(st.qy), float(st.qz), float(st.qw));
    float3 wp = rotate_quat(q, lp) + float3(st.px, st.py, st.pz);
    o.pos = project_persp(to_view(wp, u), u);
    o.clip_d[0] = ((st.flags & 1u) != 0u) ? (dot(lp, float3(float(st.cp0), float(st.cp1), float(st.cp2))) - float(st.cp3)) : 1.0f;
    o.gid = (1u << 30u) | (((uint(m.obj_id) + 1u) & 0x3FFFu) << 16u) | ((uint(v.joint_id) & 0xFFu) << 8u);
    return o;
}

fragment uint f_meshlet_id(MeshIdFragIn in [[stage_in]]) {
    uint qd = uint(clamp(in.pos.z, 0.0f, 1.0f) * 255.0f + 0.5f);
    return in.gid | qd;
}

struct ProxyIdIn {
    float4 pos [[position]];
    uint idx [[flat]];
};

vertex ProxyIdIn v_proxy_id(uint vid [[vertex_id]], uint iid [[instance_id]],
                            device const BloomAnalyticPrim32 *prims [[buffer(0)]],
                            constant BloomFrameUniforms &u [[buffer(1)]]) {
    BloomAnalyticPrim32 sp = prims[iid];
    float3 vc = to_view(float3(sp.cx, sp.cy, sp.cz), u);
    float aspect = u.vp_w / u.vp_h;
    float r = sp.radius + (sp.prim_kind_flags == 2u ? float(sp.half_len) : 0.0f);
    if (vc.z + r < u.near_z) return ProxyIdIn{float4(0.0f, 0.0f, -2.0f, 1.0f), iid};
    float hy = (r * u.focal) / max(vc.z - r, 0.25f);
    float hx = hy / aspect;
    float2 c_ndc = float2((vc.x * u.focal) / (max(vc.z, 0.25f) * aspect), (vc.y * u.focal) / max(vc.z, 0.25f));
    float2 cr = float2((vid & 1u) ? 1.0f : -1.0f, (vid & 2u) ? 1.0f : -1.0f);
    float nz = clamp(0.20f + (1.0f - (2.0f / max(vc.z - r * 0.45f, 0.25f))) * 0.78f, 0.20f, 0.98f);
    return ProxyIdIn{float4(c_ndc + cr * float2(hx, hy), nz, 1.0f), iid};
}

fragment uint f_proxy_id(ProxyIdIn in [[stage_in]],
                         device const BloomAnalyticPrim32 *prims [[buffer(0)]],
                         constant BloomFrameUniforms &fu [[buffer(1)]]) {
    BloomAnalyticPrim32 sp = prims[in.idx];
    float2 uv = (in.pos.xy / float2(fu.vp_w, fu.vp_h)) * 2.0f - 1.0f;
    uv.y = -uv.y;
    float3 ro = float3(fu.cam_pos);
    float3 rd = normalize(float3(fu.cam_right) * (uv.x * (fu.vp_w / fu.vp_h) / fu.focal) +
                          float3(fu.cam_up) * (uv.y / fu.focal) + float3(fu.cam_fwd));
    float3 sp_c = float3(sp.cx, sp.cy, sp.cz);
    float3 oc = ro - sp_c;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - sp.radius * sp.radius;
    if (b * b - c < 0.0f) discard_fragment();
    uint qd = uint(clamp(in.pos.z, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (2u << 30u) | (((in.idx + 1u) & 0x3FFFu) << 16u) | qd;
}

struct HudIdIn {
    float4 pos [[position]];
    uint idx [[flat]];
};

vertex HudIdIn v_hud_id(uint vid [[vertex_id]], uint iid [[instance_id]],
                        device const BloomQuadHot *quads [[buffer(0)]],
                        device const BloomClipRect *clips [[buffer(1)]],
                        constant BloomFrameUniforms &u [[buffer(2)]]) {
    BloomQuadHot q = quads[iid];
    BloomClipRect c = clips[q.clip_id];
    float4 b = float4(float(q.b0), float(q.b1), float(q.b2), float(q.b3)) * 0.25f;
    float2 p0 = max(b.xy, c.rect.xy), p1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    if (p1.x <= p0.x || p1.y <= p0.y || q.style_id == 0u) return HudIdIn{float4(0.0f, 0.0f, -2.0f, 1.0f), iid};
    float2 px = mix(p0, p1, float2(float(vid & 1u), float((vid >> 1u) & 1u)));
    float z = 0.14f - clamp(float(q.z_flags & 0xFFFFu) * 1e-5f, 0.0f, 0.12f);
    return HudIdIn{float4(to_ndc_2d(px, u), z, 1.0f), iid};
}

fragment uint f_hud_id(HudIdIn in [[stage_in]],
                       device const BloomQuadHot *quads [[buffer(0)]],
                       constant BloomGroundParams &gp [[buffer(1)]]) {
    BloomQuadHot q = quads[in.idx];
    half4 b = half4(half(q.b0), half(q.b1), half(q.b2), half(q.b3)) * 0.25h;
    half2 hs = b.zw * 0.5h, c2p = (half2(in.pos.xy * gp.pos_scale) - b.xy) - hs;
    half4 rr = half4(half(q.r0), half(q.r1), half(q.r2), half(q.r3)) * 0.25h;
    half r = (c2p.x < 0.0h) ? (c2p.y < 0.0h ? rr.x : rr.w) : (c2p.y < 0.0h ? rr.y : rr.z);
    half2 d = abs(c2p) - hs + r;
    half outer = length(max(d, 0.0h)) + min(max(d.x, d.y), 0.0h) - r;
    if (outer > 0.45h) discard_fragment();
    bool vis = ((q.rgba8_fill >> 24u) > 0u) || ((q.rgba8_border >> 24u) > 0u) || ((q.z_flags & (1u << 28u)) != 0u);
    if (!vis) discard_fragment();
    uint part = (q.z_flags >> 16u) & 0xFFu;
    uint qd = uint(clamp(in.pos.z, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (3u << 30u) | ((uint(q.style_id) & 0x3FFFu) << 16u) | (part << 8u) | qd;
}

inline void emit_ground_stat(uint id_val, uint px_cnt, uint mn_x, uint mn_y, uint mx_x, uint mx_y,
                             uint anc, uint part, uint min_d, uint slot_base, uint max_slots,
                             device BloomGroundedAtomic *stats) {
    uint dom = (id_val >> 30u) & 3u;
    uint sl = (id_val >> 16u) & 0x3FFFu;
    uint gi = (dom - 1u) * slot_base + sl;
    if (dom == 0u || sl == 0u || gi >= max_slots) return;
    atomic_fetch_add_explicit(&stats[gi].visible_px, px_cnt, memory_order_relaxed);
    atomic_fetch_min_explicit(&stats[gi].min_x, mn_x, memory_order_relaxed);
    atomic_fetch_min_explicit(&stats[gi].min_y, mn_y, memory_order_relaxed);
    atomic_fetch_max_explicit(&stats[gi].max_x, mx_x, memory_order_relaxed);
    atomic_fetch_max_explicit(&stats[gi].max_y, mx_y, memory_order_relaxed);
    if (anc != 0u) atomic_fetch_max_explicit(&stats[gi].best_anchor, anc, memory_order_relaxed);
    atomic_store_explicit(&stats[gi].part_sample, part, memory_order_relaxed);
    atomic_fetch_min_explicit(&stats[gi].min_depth_u32, min_d, memory_order_relaxed);
}

kernel void k_compute_ground_reduce(texture2d<uint, access::read> id_tex [[texture(0)]],
                                    device BloomGroundedAtomic *stats [[buffer(0)]],
                                    device BloomPickGpuResult *pick_out [[buffer(1)]],
                                    constant BloomGroundParams &gp [[buffer(2)]],
                                    uint2 tid [[thread_position_in_threadgroup]],
                                    uint2 gid [[thread_position_in_grid]]) {
    uint w = id_tex.get_width(), h = id_tex.get_height();
    bool in_vp = (gid.x < w && gid.y < h);
    uint val = in_vp ? id_tex.read(gid).r : 0u;
    uint step_px = 1u << gp.half_shift;
    uint px = (gid.x << gp.half_shift) + (gp.half_shift ? 1u : 0u);
    uint py = (gid.y << gp.half_shift) + (gp.half_shift ? 1u : 0u);
    uint area = 1u << (gp.half_shift * 2u);
    if (in_vp && (gid.x << gp.half_shift) <= gp.pick_x && gp.pick_x < (gid.x << gp.half_shift) + step_px &&
        (gid.y << gp.half_shift) <= gp.pick_y && gp.pick_y < (gid.y << gp.half_shift) + step_px) {
        pick_out->ground_id = val;
        pick_out->depth_bits = val & 0xFFu;
        pick_out->px = gp.pick_x;
        pick_out->py = gp.pick_y;
    }
    uint id_key = val & 0xFFFF0000u;
    if (simd_all(id_key == 0u)) return;
    uint first_key = simd_broadcast_first(id_key);
    if (simd_all(in_vp) && first_key != 0u && simd_all(id_key == first_key)) {
        uint mn_x = simd_min(px), mn_y = simd_min(py), mx_x = simd_max(px), mx_y = simd_max(py);
        uint min_d = simd_min(val & 0xFFu);
        bool interior = (tid.x >= 2u && tid.x + 2u < 16u && tid.y >= 2u && tid.y + 2u < 16u);
        uint anc = ((interior ? 60u : 28u) << 26u) | ((px & 0x1FFFu) << 13u) | (py & 0x1FFFu);
        uint best_a = simd_max(anc);
        if (simd_is_first()) {
            emit_ground_stat(first_key, 32u * area, mn_x, mn_y, mx_x, mx_y,
                             best_a, (val >> 8u) & 0xFFu, min_d, gp.max_slots / 3u, gp.max_slots, stats);
        }
        return;
    }
    if (id_key == 0u) return;
    uint m = max(1u, 6u >> gp.half_shift);
    uint x0 = uint(max(int(gid.x) - int(m), 0)), x1 = uint(min(int(gid.x) + int(m), int(w) - 1));
    uint y0 = uint(max(int(gid.y) - int(m), 0)), y1 = uint(min(int(gid.y) + int(m), int(h) - 1));
    uint sc = 1u;
    sc += ((id_tex.read(uint2(x0, gid.y)).r & 0xFFFF0000u) == id_key) ? 14u : 0u;
    sc += ((id_tex.read(uint2(x1, gid.y)).r & 0xFFFF0000u) == id_key) ? 14u : 0u;
    sc += ((id_tex.read(uint2(gid.x, y0)).r & 0xFFFF0000u) == id_key) ? 14u : 0u;
    sc += ((id_tex.read(uint2(gid.x, y1)).r & 0xFFFF0000u) == id_key) ? 14u : 0u;
    uint anc = (min(sc, 63u) << 26u) | ((px & 0x1FFFu) << 13u) | (py & 0x1FFFu);
    emit_ground_stat(id_key, area, px, py, px, py, anc, (val >> 8u) & 0xFFu, val & 0xFFu,
                     gp.max_slots / 3u, gp.max_slots, stats);
}
