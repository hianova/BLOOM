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

struct BloomShadowHot {
    ushort b0, b1, b2, b3;
    half sp0, sp1, sp2, sp3;
    half br0, br1, br2, br3;
    uint rgba8;
    ushort clip_id, z_flags;
};

struct BloomGlyphInst {
    ushort d0, d1, d2, d3, u0, u1, u2, u3;
    uint rgba8;
    ushort clip_id, z_flags;
};

struct BloomClipRect { float4 rect; };

struct BloomFrameUniforms {
    float vp_w, vp_h, focal, frustum_half_tan;
    packed_float3 cam_pos; float near_z;
    packed_float3 cam_right; float far_z;
    packed_float3 cam_up; uint total_meshlets;
    packed_float3 cam_fwd; uint prim_count;
};

struct DrawIndexedIndirectArgs {
    uint indexCount;
    atomic_uint instanceCount;
    uint indexStart;
    int baseVertex;
    uint baseInstance;
};

static_assert(sizeof(BloomVtx12) == 12, "MSL BloomVtx12");
static_assert(sizeof(BloomMeshletHeader32) == 32, "MSL BloomMeshletHeader32");
static_assert(sizeof(BloomObjectState48) == 48, "MSL BloomObjectState48");
static_assert(sizeof(BloomAnalyticPrim32) == 32, "MSL BloomAnalyticPrim32");
static_assert(sizeof(BloomQuadHot) == 32, "MSL BloomQuadHot");
static_assert(sizeof(BloomShadowHot) == 32, "MSL BloomShadowHot");
static_assert(sizeof(BloomGlyphInst) == 24, "MSL BloomGlyphInst");
static_assert(sizeof(BloomClipRect) == 16, "MSL BloomClipRect");
static_assert(sizeof(BloomFrameUniforms) == 80, "MSL BloomFrameUniforms");

inline float3 rotate_quat(float4 q, float3 v) {
    float3 t = 2.0f * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

inline float3 oct_decode_gpu(char ox, char oy) {
    float2 o = float2(float(ox), float(oy)) * (1.0f / 127.0f);
    float nz = 1.0f - abs(o.x) - abs(o.y);
    if (nz < 0.0f) {
        float2 s = float2(o.x >= 0.0f ? 1.0f : -1.0f, o.y >= 0.0f ? 1.0f : -1.0f);
        o = (1.0f - abs(o.yx)) * s;
    }
    return normalize(float3(o.x, o.y, nz));
}

inline float3 unpack_bloom_pos(BloomVtx12 v) {
    return float3(-1.5f) + float3(float(v.px), float(v.py), float(v.pz)) * (3.0f / 65535.0f);
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

inline half erf7_h(half x) {
    half xa = abs(x);
    half p = 1.0h + xa * (0.278393h + xa * (0.230389h + xa * 0.078108h));
    half p2 = p * p;
    return copysign(1.0h - 1.0h / (p2 * p2), x);
}

kernel void k_cull_meshlets(device const BloomMeshletHeader32 *hdrs [[buffer(0)]],
                            device const BloomObjectState48 *states [[buffer(1)]],
                            constant BloomFrameUniforms &u [[buffer(2)]],
                            device uint *surviving_ids [[buffer(3)]],
                            device DrawIndexedIndirectArgs &ind [[buffer(4)]],
                            uint gid [[thread_position_in_grid]]) {
    bool keep = (gid < u.total_meshlets);
    if (keep) {
        BloomMeshletHeader32 m = hdrs[gid];
        BloomObjectState48 st = states[m.obj_id];
        keep = ((st.flags & 2u) == 0u);
        if (keep) {
            float4 q = float4(float(st.qx), float(st.qy), float(st.qz), float(st.qw));
            float3 sc = float3(float(st.sx), float(st.sy), float(st.sz));
            float3 wc = rotate_quat(q, float3(float(m.bs_x), float(m.bs_y), float(m.bs_z)) * sc) + float3(st.px, st.py, st.pz);
            float3 vc = to_view(wc, u);
            float rad = float(m.bs_r) * max(abs(sc.x), max(abs(sc.y), abs(sc.z)));
            float ly = vc.z * u.frustum_half_tan, lx = ly * (u.vp_w / u.vp_h);
            keep = !(vc.z + rad < u.near_z || vc.z - rad > u.far_z || abs(vc.x) - rad > lx || abs(vc.y) - rad > ly);
            if (keep) {
                float3 d_cam = wc - float3(u.cam_pos); float d_len = length(d_cam);
                float3 axis = rotate_quat(q, float3(float(m.cone_x), float(m.cone_y), float(m.cone_z)) * (1.0f / 127.0f));
                keep = (d_len < 1e-4f) || (dot(d_cam / d_len, axis) < float(m.cone_c) * (1.0f / 127.0f) + rad / d_len);
            }
        }
    }
    uint lane_off = simd_prefix_exclusive_sum(keep ? 1u : 0u);
    uint grp_cnt = simd_sum(keep ? 1u : 0u);
    uint base_slot = 0u;
    if (simd_is_first() && grp_cnt > 0u) base_slot = atomic_fetch_add_explicit(&ind.instanceCount, grp_cnt, memory_order_relaxed);
    base_slot = simd_broadcast_first(base_slot);
    if (keep) surviving_ids[base_slot + lane_off] = gid;
}

struct ObjFragIn {
    float4 pos [[position]];
    half3 nrm;
    half4 col;
};

struct ObjCutOut {
    float4 pos [[position]];
    float clip_d [[clip_distance]] [1];
    half3 nrm;
    half4 col;
};

inline void eval_obj_vertex(uint vid, uint m_id,
                            device const BloomMeshletHeader32 *hdrs,
                            device const BloomVtx12 *verts,
                            device const uchar *local_idx,
                            device const BloomObjectState48 *states,
                            constant BloomFrameUniforms &u,
                            thread float3 &out_lp,
                            thread float4 &out_pos,
                            thread half3 &out_nrm,
                            thread half4 &out_col) {
    BloomMeshletHeader32 m = hdrs[m_id];
    if (vid >= uint(m.tri_cnt) * 3u) {
        out_lp = float3(0.0f);
        out_pos = float4(0.0f, 0.0f, -2.0f, 1.0f);
        out_nrm = half3(0.0h, 1.0h, 0.0h);
        out_col = half4(0.0h);
        return;
    }
    uint lv = uint(local_idx[m.idx_offset + vid]);
    BloomVtx12 v = verts[m.vtx_offset + lv];
    BloomObjectState48 st = states[m.obj_id];
    float3 lp = unpack_bloom_pos(v) * float3(float(st.sx), float(st.sy), float(st.sz));
    uint jmod = uint(v.joint_id % 3u);
    float dof = float(jmod == 0u ? st.dof0 : (jmod == 1u ? st.dof1 : st.dof2));
    lp.y += sin(dof + lp.x) * 0.08f;
    float4 q = float4(float(st.qx), float(st.qy), float(st.qz), float(st.qw));
    float3 wp = rotate_quat(q, lp) + float3(st.px, st.py, st.pz);
    out_lp = lp;
    out_pos = project_persp(to_view(wp, u), u);
    out_nrm = half3(rotate_quat(q, oct_decode_gpu(v.oct_x, v.oct_y)));
    out_col = unpack_unorm4x8_to_half(st.rgba8_mat);
}

vertex ObjFragIn v_meshlet_indirect(uint vid [[vertex_id]], uint iid [[instance_id]],
                                    device const uint *surviving_ids [[buffer(0)]],
                                    device const BloomMeshletHeader32 *hdrs [[buffer(1)]],
                                    device const BloomVtx12 *verts [[buffer(2)]],
                                    device const uchar *local_idx [[buffer(3)]],
                                    device const BloomObjectState48 *states [[buffer(4)]],
                                    constant BloomFrameUniforms &u [[buffer(5)]]) {
    ObjFragIn o;
    float3 lp;
    eval_obj_vertex(vid, surviving_ids[iid], hdrs, verts, local_idx, states, u, lp, o.pos, o.nrm, o.col);
    return o;
}

vertex ObjCutOut v_meshlet_cutaway(uint vid [[vertex_id]], uint iid [[instance_id]],
                                   device const uint *surviving_ids [[buffer(0)]],
                                   device const BloomMeshletHeader32 *hdrs [[buffer(1)]],
                                   device const BloomVtx12 *verts [[buffer(2)]],
                                   device const uchar *local_idx [[buffer(3)]],
                                   device const BloomObjectState48 *states [[buffer(4)]],
                                   constant BloomFrameUniforms &u [[buffer(5)]]) {
    uint m_id = surviving_ids[iid];
    BloomMeshletHeader32 m = hdrs[m_id];
    BloomObjectState48 st = states[m.obj_id];
    ObjCutOut o;
    float3 lp;
    eval_obj_vertex(vid, m_id, hdrs, verts, local_idx, states, u, lp, o.pos, o.nrm, o.col);
    o.clip_d[0] = ((st.flags & 1u) != 0u) ? (dot(lp, float3(float(st.cp0), float(st.cp1), float(st.cp2))) - float(st.cp3)) : 1.0f;
    return o;
}

fragment half4 f_obj_lit(ObjFragIn in [[stage_in]]) {
    half3 n = normalize(in.nrm);
    half ndotl = clamp(dot(n, normalize(half3(0.4h, 0.7h, -0.5h))), 0.18h, 1.0h);
    half rim = pow(clamp(1.0h - abs(n.z), 0.0h, 1.0h), 2.0h) * 0.28h;
    return half4(in.col.rgb * ndotl + half3(0.10h, 0.25h, 0.35h) * rim, 1.0h);
}

struct TightProxyIn { float4 pos [[position]]; float2 ndc; uint idx [[flat]]; };

vertex TightProxyIn v_tight_proxy_analytic(uint vid [[vertex_id]], uint iid [[instance_id]],
                                           device const BloomAnalyticPrim32 *prims [[buffer(0)]],
                                           constant BloomFrameUniforms &u [[buffer(1)]]) {
    BloomAnalyticPrim32 sp = prims[iid];
    float3 vc = to_view(float3(sp.cx, sp.cy, sp.cz), u);
    float aspect = u.vp_w / u.vp_h;
    float r = sp.radius + (sp.prim_kind_flags == 2u ? float(sp.half_len) : 0.0f);
    if (vc.z + r < u.near_z) return TightProxyIn{float4(0.0f, 0.0f, -2.0f, 1.0f), float2(0.0f), iid};
    float z_near = max(vc.z - r, 0.25f);
    float hy = (r * u.focal) / z_near;
    float hx = hy / aspect;
    float2 c_ndc = float2((vc.x * u.focal) / (max(vc.z, 0.25f) * aspect), (vc.y * u.focal) / max(vc.z, 0.25f));
    float2 cr = float2((vid & 1u) ? 1.0f : -1.0f, (vid & 2u) ? 1.0f : -1.0f);
    float nz = clamp(0.20f + (1.0f - (2.0f / max(vc.z - r * 0.45f, 0.25f))) * 0.78f, 0.20f, 0.98f);
    float2 p_ndc = c_ndc + cr * float2(hx, hy);
    return TightProxyIn{float4(p_ndc, nz, 1.0f), p_ndc, iid};
}

fragment half4 f_tight_proxy_analytic(TightProxyIn in [[stage_in]],
                                      device const BloomAnalyticPrim32 *prims [[buffer(0)]],
                                      constant BloomFrameUniforms &u [[buffer(1)]]) {
    BloomAnalyticPrim32 sp = prims[in.idx];
    float2 uv = in.ndc;
    float3 ro = float3(u.cam_pos);
    float3 rd = normalize(float3(u.cam_right) * (uv.x * (u.vp_w / u.vp_h) / u.focal) +
                          float3(u.cam_up) * (uv.y / u.focal) + float3(u.cam_fwd));
    float3 sp_c = float3(sp.cx, sp.cy, sp.cz);
    float3 oc = ro - sp_c;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - sp.radius * sp.radius;
    float h = b * b - c;
    if (h < 0.0f) return half4(0.0h);
    float3 hp = ro + rd * (-b - sqrt(h));
    half3 n = half3(normalize(hp - sp_c));
    half3 col = unpack_unorm4x8_to_half(sp.rgba8).rgb;
    half diff = clamp(dot(n, normalize(half3(0.4h, 0.7h, -0.5h))), 0.22h, 1.0h);
    return half4(col * diff, 1.0h);
}

struct HudOpaqueIn { float4 pos [[position]]; half4 fill [[flat]]; };
struct HudRefetchIn { float4 pos [[position]]; uint idx [[flat]]; };

vertex HudOpaqueIn v_hud_opaque(uint vid [[vertex_id]], uint iid [[instance_id]],
                                device const BloomQuadHot *quads [[buffer(0)]],
                                device const BloomClipRect *clips [[buffer(1)]],
                                constant BloomFrameUniforms &u [[buffer(2)]]) {
    BloomQuadHot q = quads[iid];
    BloomClipRect c = clips[q.clip_id];
    float4 b = float4(float(q.b0), float(q.b1), float(q.b2), float(q.b3)) * 0.25f;
    float2 p0 = max(b.xy, c.rect.xy), p1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    if (p1.x <= p0.x || p1.y <= p0.y) return HudOpaqueIn{float4(0.0f, 0.0f, -2.0f, 1.0f), half4(0)};
    float2 px = mix(p0, p1, float2(float(vid & 1u), float((vid >> 1u) & 1u)));
    float z = 0.14f - clamp(float(q.z_flags & 0xFFFFu) * 1e-5f, 0.0f, 0.12f);
    return HudOpaqueIn{float4(to_ndc_2d(px, u), z, 1.0f), unpack_unorm4x8_to_half(q.rgba8_fill)};
}
fragment half4 f_hud_opaque(HudOpaqueIn in [[stage_in]]) { return in.fill; }

vertex HudRefetchIn v_hud_alpha(uint vid [[vertex_id]], uint iid [[instance_id]],
                                device const BloomQuadHot *quads [[buffer(0)]],
                                device const BloomClipRect *clips [[buffer(1)]],
                                constant BloomFrameUniforms &u [[buffer(2)]]) {
    BloomQuadHot q = quads[iid];
    BloomClipRect c = clips[q.clip_id];
    float4 b = float4(float(q.b0), float(q.b1), float(q.b2), float(q.b3)) * 0.25f;
    float2 p0 = max(b.xy, c.rect.xy), p1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    if (p1.x <= p0.x || p1.y <= p0.y) return HudRefetchIn{float4(0.0f, 0.0f, -2.0f, 1.0f), iid};
    float2 px = mix(p0, p1, float2(float(vid & 1u), float((vid >> 1u) & 1u)));
    float z = 0.14f - clamp(float(q.z_flags & 0xFFFFu) * 1e-5f, 0.0f, 0.12f);
    return HudRefetchIn{float4(to_ndc_2d(px, u), z, 1.0f), iid};
}
fragment half4 f_hud_alpha(HudRefetchIn in [[stage_in]], device const BloomQuadHot *quads [[buffer(0)]]) {
    BloomQuadHot q = quads[in.idx];
    half4 b = half4(half(q.b0), half(q.b1), half(q.b2), half(q.b3)) * 0.25h;
    half2 hs = b.zw * 0.5h, c2p = (half2(in.pos.xy) - b.xy) - hs;
    half4 rr = half4(half(q.r0), half(q.r1), half(q.r2), half(q.r3)) * 0.25h;
    half4 bb = half4(half(q.bw0), half(q.bw1), half(q.bw2), half(q.bw3)) * 0.25h;
    half r = (c2p.x < 0.0h) ? (c2p.y < 0.0h ? rr.x : rr.w) : (c2p.y < 0.0h ? rr.y : rr.z);
    half bw = min(c2p.x < 0.0h ? bb.w : bb.y, c2p.y < 0.0h ? bb.x : bb.z);
    half2 d = abs(c2p) - hs + r;
    half outer = length(max(d, 0.0h)) + min(max(d.x, d.y), 0.0h) - r;
    half4 col = mix(unpack_unorm4x8_to_half(q.rgba8_border), unpack_unorm4x8_to_half(q.rgba8_fill), clamp(0.5h - (outer + bw), 0.0h, 1.0h));
    col.a *= clamp(0.5h - outer, 0.0h, 1.0h);
    return col;
}

vertex HudRefetchIn v_hud_shadow(uint vid [[vertex_id]], uint iid [[instance_id]],
                                 device const BloomShadowHot *shadows [[buffer(0)]],
                                 device const BloomClipRect *clips [[buffer(1)]],
                                 constant BloomFrameUniforms &u [[buffer(2)]]) {
    BloomShadowHot sh = shadows[iid];
    BloomClipRect c = clips[sh.clip_id];
    float4 b = float4(float(sh.b0), float(sh.b1), float(sh.b2), float(sh.b3)) * 0.25f;
    float2 p0 = max(b.xy, c.rect.xy), p1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    if (p1.x <= p0.x || p1.y <= p0.y) return HudRefetchIn{float4(0.0f, 0.0f, -2.0f, 1.0f), iid};
    float2 px = mix(p0, p1, float2(float(vid & 1u), float((vid >> 1u) & 1u)));
    float z = 0.14f - clamp(float(sh.z_flags) * 1e-5f, 0.0f, 0.12f);
    return HudRefetchIn{float4(to_ndc_2d(px, u), z, 1.0f), iid};
}
fragment half4 f_hud_shadow(HudRefetchIn in [[stage_in]], device const BloomShadowHot *shadows [[buffer(0)]]) {
    BloomShadowHot sh = shadows[in.idx];
    half4 b = half4(half(sh.b0), half(sh.b1), half(sh.b2), half(sh.b3)) * 0.25h;
    half2 c2p = half2(in.pos.xy) - (b.xy + b.zw * 0.5h);
    half r1 = sh.br0, ex = sh.br1, sc = sh.br2;
    half2 p0 = abs(c2p) - (half2(sh.sp0, sh.sp1) - r1), p1 = max(p0, 0.0h);
    half d_pos = (p1.x > 0.0h && p1.y > 0.0h) ? pow(pow(p1.x, ex) + pow(p1.y, ex), 1.0h / ex) : (p1.x + p1.y);
    half d = d_pos + min(max(p0.x, p0.y), 0.0h) - r1;
    half4 col = unpack_unorm4x8_to_half(sh.rgba8);
    col.a *= sc * (erf7_h(sh.sp2 * (sh.sp3 + d)) - erf7_h(sh.sp2 * d));
    return col;
}

vertex HudRefetchIn v_hud_glyph(uint vid [[vertex_id]], uint iid [[instance_id]],
                                device const BloomGlyphInst *glyphs [[buffer(0)]],
                                device const BloomClipRect *clips [[buffer(1)]],
                                constant BloomFrameUniforms &u [[buffer(2)]]) {
    BloomGlyphInst g = glyphs[iid];
    BloomClipRect c = clips[g.clip_id];
    float4 b = float4(float(g.d0), float(g.d1), float(g.d2), float(g.d3)) * 0.25f;
    float2 p0 = max(b.xy, c.rect.xy), p1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    if (p1.x <= p0.x || p1.y <= p0.y) return HudRefetchIn{float4(0.0f, 0.0f, -2.0f, 1.0f), iid};
    float2 px = mix(p0, p1, float2(float(vid & 1u), float((vid >> 1u) & 1u)));
    float z = 0.14f - clamp(float(g.z_flags) * 1e-5f, 0.0f, 0.12f);
    return HudRefetchIn{float4(to_ndc_2d(px, u), z, 1.0f), iid};
}
fragment half4 f_hud_glyph(HudRefetchIn in [[stage_in]], device const BloomGlyphInst *glyphs [[buffer(0)]],
                           texture2d<half> atlas [[texture(0)]]) {
    BloomGlyphInst g = glyphs[in.idx];
    float4 b = float4(float(g.d0), float(g.d1), float(g.d2), float(g.d3)) * 0.25f;
    float2 luv = (in.pos.xy - b.xy) / max(b.zw, float2(1e-4f));
    float4 uvr = float4(float(g.u0), float(g.u1), float(g.u2), float(g.u3)) * (1.0f / 65535.0f);
    constexpr sampler smp(mag_filter::linear, min_filter::linear);
    half4 col = unpack_unorm4x8_to_half(g.rgba8);
    col.a *= atlas.sample(smp, uvr.xy + luv * uvr.zw).r;
    return col;
}

kernel void k_tex_checksum(texture2d<half, access::read> tex [[texture(0)]],
                           device atomic_uint *out_acc [[buffer(0)]],
                           uint2 gid [[thread_position_in_grid]]) {
    uint w = tex.get_width(), h = tex.get_height();
    uint x = gid.x * 8u, y = gid.y * 8u, q = 0u;
    if (x < w && y < h) {
        half4 p = tex.read(uint2(x, y));
        q = uint(clamp(p.r * 255.0h, 0.0h, 255.0h)) +
            (uint(clamp(p.g * 255.0h, 0.0h, 255.0h)) << 8u) +
            (uint(clamp(p.b * 255.0h, 0.0h, 255.0h)) << 16u);
    }
    uint s = simd_sum(q);
    if (simd_is_first() && s != 0u) atomic_fetch_add_explicit(out_acc, s, memory_order_relaxed);
}
