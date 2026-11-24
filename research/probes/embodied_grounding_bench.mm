// embodied_grounding_bench.mm — Empirical Apple M1 Metal Benchmark:
// 1. 4K (3840x2160) Pixel-Exact Grounding Bake-Off:
//    baseline_rgb_only vs opt1_mrt_private_compute vs opt2_mrt_memoryless_tile vs opt3_halfres_id_pass
// 2. Occlusion-Aware Unified UI + 3D Affordance Grounding Verification (Modal / VList / Partial)
// 3. Analysis-by-Synthesis Visual Localization & Multi-Template Recognition (bloom-vision-align)
// Standalone C++17 / Objective-C++ / Metal 3 (zero FLOW, zero SDL3, zero external deps).

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <time.h>
#include <vector>

namespace {

static constexpr uint32_t kClusterVtx = 64u;
static constexpr uint32_t kClusterTri = 84u;
static constexpr float kBoxLo = -1.5f;
static constexpr float kBoxSpan = 3.0f;

static inline uint32_t pack_rgba(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
    return (a << 24u) | (b << 16u) | (g << 8u) | r;
}
static inline uint16_t f32_to_f16(float v) {
    _Float16 h = static_cast<_Float16>(v);
    uint16_t b = 0; std::memcpy(&b, &h, 2); return b;
}
static inline uint16_t pack_q14_2(float px) {
    float v = std::max(0.0f, std::min(65535.0f, px * 4.0f));
    return static_cast<uint16_t>(v + 0.5f);
}
static inline uint8_t pack_q6_2(float px) {
    float v = std::max(0.0f, std::min(255.0f, px * 4.0f));
    return static_cast<uint8_t>(v + 0.5f);
}
static inline uint16_t pack_pos_u16(float x) {
    float t = std::max(0.0f, std::min(1.0f, (x - kBoxLo) / kBoxSpan));
    return static_cast<uint16_t>(t * 65535.0f + 0.5f);
}
static inline void oct_encode(float nx, float ny, float nz, int8_t out[2]) {
    float inv = 1.0f / std::max(std::fabs(nx) + std::fabs(ny) + std::fabs(nz), 1e-8f);
    float ox = nx * inv, oy = ny * inv;
    if (nz < 0.0f) {
        float tx = (1.0f - std::fabs(oy)) * (ox >= 0.0f ? 1.0f : -1.0f);
        float ty = (1.0f - std::fabs(ox)) * (oy >= 0.0f ? 1.0f : -1.0f);
        ox = tx; oy = ty;
    }
    auto q = [](float v) -> int8_t {
        float s = std::max(-1.0f, std::min(1.0f, v)) * 127.0f;
        return static_cast<int8_t>(s >= 0.0f ? (s + 0.5f) : (s - 0.5f));
    };
    out[0] = q(ox); out[1] = q(oy);
}

static inline uint32_t ground_slot(uint32_t g) { return (g >> 16u) & 0x3FFFu; }

struct alignas(4) Vtx12 {
    uint16_t px, py, pz;
    int8_t oct_x, oct_y;
    uint8_t u, v, joint_id, mat_id;
};
static_assert(sizeof(Vtx12) == 12, "Vtx12");

struct alignas(4) MeshletHdr32 {
    uint32_t vtx_offset, idx_offset;
    uint16_t vtx_cnt, tri_cnt, obj_id, flags;
    uint16_t bs_f16[4];
    int8_t cone_x, cone_y, cone_z, cone_c;
    uint32_t aabb_scale_tag;
};
static_assert(sizeof(MeshletHdr32) == 32, "MeshletHdr32");

struct alignas(4) ObjState48 {
    float px, py, pz;
    uint16_t quat_f16[4];
    uint16_t scale_f16[3];
    uint16_t dof_f16[3];
    uint32_t rgba8_mat;
    uint16_t clip_f16[4];
    uint32_t flags;
};
static_assert(sizeof(ObjState48) == 48, "ObjState48");

struct alignas(4) Analytic32 {
    float cx, cy, cz, radius;
    uint16_t axis_f16[4];
    uint32_t rgba8, prim_kind_flags;
};
static_assert(sizeof(Analytic32) == 32, "Analytic32");

struct alignas(4) QuadHot32 {
    uint16_t b0, b1, b2, b3;
    uint32_t rgba8_fill, rgba8_border;
    uint8_t r[4], bw[4];
    uint16_t clip_id, style_id;
    uint32_t z_flags;
};
static_assert(sizeof(QuadHot32) == 32, "QuadHot32");

struct alignas(4) ClipRect16 { float x, y, w, h; };
static_assert(sizeof(ClipRect16) == 16, "ClipRect16");

struct alignas(4) FrameUniforms {
    float vp_w, vp_h, focal, frustum_half_tan;
    float cam_pos[3]; float near_z;
    float cam_right[3]; float far_z;
    float cam_up[3]; uint32_t total_meshlets;
    float cam_fwd[3]; uint32_t prim_count;
};
static_assert(sizeof(FrameUniforms) == 80, "FrameUniforms");

struct alignas(4) GroundParams {
    uint32_t vp_w, vp_h, pick_x, pick_y, max_slots, half_shift, pad0, pad1;
};
static_assert(sizeof(GroundParams) == 32, "GroundParams");

struct alignas(4) GroundStat32 {
    uint32_t visible_px, min_x, min_y, max_x, max_y, best_anchor, part_sample, min_depth_u32;
};
static_assert(sizeof(GroundStat32) == 32, "GroundStat32");

struct VisionHyp64 {
    float px, py, pz;
    float yaw, pitch, roll, scale, dof0;
    float score_out, iou_out, lum_err, edge_err;
    uint32_t tpl_id, pad0, pad1, pad2;
};
static_assert(sizeof(VisionHyp64) == 64, "VisionHyp64");

struct alignas(4) VisionParams {
    uint32_t img_w, img_h, first_m, hyps_base;
    float focal; uint32_t tile_base;
    uint32_t n_hyps, full_vp;
};
static_assert(sizeof(VisionParams) == 32, "VisionParams");

static const char *kProbeMsl = R"(
#include <metal_stdlib>
using namespace metal;

struct Vtx12 { ushort px, py, pz; char oct_x, oct_y; uchar u, v, joint_id, mat_id; };
struct MeshletHdr32 {
    uint vtx_offset, idx_offset; ushort vtx_cnt, tri_cnt, obj_id, flags;
    half bs_x, bs_y, bs_z, bs_r; char cone_x, cone_y, cone_z, cone_c; uint tag;
};
struct ObjState48 {
    float px, py, pz; half qx, qy, qz, qw; half sx, sy, sz, dof0, dof1, dof2;
    uint rgba8_mat; half cp0, cp1, cp2, cp3; uint flags;
};
struct Analytic32 { float cx, cy, cz, radius; half ax, ay, az, half_len; uint rgba8, prim_kind_flags; };
struct QuadHot32 {
    ushort b0, b1, b2, b3; uint rgba8_fill, rgba8_border;
    uchar r0, r1, r2, r3, bw0, bw1, bw2, bw3; ushort clip_id, style_id; uint z_flags;
};
struct ClipRect16 { float4 rect; };
struct FrameUniforms {
    float vp_w, vp_h, focal, frustum_half_tan;
    packed_float3 cam_pos; float near_z;
    packed_float3 cam_right; float far_z;
    packed_float3 cam_up; uint total_meshlets;
    packed_float3 cam_fwd; uint prim_count;
};
struct DrawIndirect { uint indexCount; atomic_uint instanceCount; uint indexStart; int baseVertex; uint baseInstance; };
struct GroundParams { uint vp_w, vp_h, pick_x, pick_y, max_slots, half_shift, pad0, pad1; };
struct GroundStatAtomic {
    atomic_uint visible_px, min_x, min_y, max_x, max_y, best_anchor, part_sample, min_depth_u32;
};

inline float3 rotate_quat(float4 q, float3 v) {
    float3 t = 2.0f * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}
inline float3 oct_dec(char ox, char oy) {
    float2 o = float2(float(ox), float(oy)) * (1.0f / 127.0f);
    float nz = 1.0f - abs(o.x) - abs(o.y);
    if (nz < 0.0f) {
        float2 s = float2(o.x >= 0.0f ? 1.0f : -1.0f, o.y >= 0.0f ? 1.0f : -1.0f);
        o = (1.0f - abs(o.yx)) * s;
    }
    return normalize(float3(o.x, o.y, nz));
}
inline float3 to_view(float3 wp, constant FrameUniforms &u) {
    float3 d = wp - float3(u.cam_pos);
    return float3(dot(d, float3(u.cam_right)), dot(d, float3(u.cam_up)), dot(d, float3(u.cam_fwd)));
}
inline float4 project_persp(float3 vp, constant FrameUniforms &u) {
    float z = max(vp.z, 0.25f);
    float aspect = u.vp_w / u.vp_h;
    return float4((vp.x * u.focal) / (z * aspect), (vp.y * u.focal) / z,
                  clamp(0.20f + (1.0f - (2.0f / z)) * 0.78f, 0.20f, 0.98f), 1.0f);
}
inline float2 to_ndc_2d(float2 px, constant FrameUniforms &u) {
    return float2((px.x / u.vp_w) * 2.0f - 1.0f, 1.0f - (px.y / u.vp_h) * 2.0f);
}

kernel void k_cull(device const MeshletHdr32 *hdrs [[buffer(0)]],
                   device const ObjState48 *states [[buffer(1)]],
                   constant FrameUniforms &u [[buffer(2)]],
                   device uint *surv [[buffer(3)]],
                   device DrawIndirect &ind [[buffer(4)]],
                   uint gid [[thread_position_in_grid]]) {
    if (gid >= u.total_meshlets) return;
    MeshletHdr32 m = hdrs[gid];
    ObjState48 st = states[m.obj_id];
    if ((st.flags & 2u) != 0u) return;
    float4 q = float4(float(st.qx), float(st.qy), float(st.qz), float(st.qw));
    float3 sc = float3(float(st.sx), float(st.sy), float(st.sz));
    float3 wc = rotate_quat(q, float3(float(m.bs_x), float(m.bs_y), float(m.bs_z)) * sc) + float3(st.px, st.py, st.pz);
    float3 vc = to_view(wc, u);
    float rad = float(m.bs_r) * max(sc.x, max(sc.y, sc.z));
    if (vc.z + rad < u.near_z || vc.z - rad > u.far_z) return;
    float ly = vc.z * u.frustum_half_tan;
    float lx = ly * (u.vp_w / u.vp_h);
    if (abs(vc.x) - rad > lx || abs(vc.y) - rad > ly) return;
    uint slot = atomic_fetch_add_explicit(&ind.instanceCount, 1u, memory_order_relaxed);
    surv[slot] = gid;
}

struct ObjIn { float4 pos [[position]]; half3 nrm; half4 col; uint gid [[flat]]; };
struct MrtOut { half4 col [[color(0)]]; uint gid [[color(1)]]; };

vertex ObjIn v_mesh(uint vid [[vertex_id]], uint iid [[instance_id]],
                    device const uint *surv [[buffer(0)]],
                    device const MeshletHdr32 *hdrs [[buffer(1)]],
                    device const Vtx12 *verts [[buffer(2)]],
                    device const uchar *lidx [[buffer(3)]],
                    device const ObjState48 *states [[buffer(4)]],
                    constant FrameUniforms &u [[buffer(5)]]) {
    uint mid = surv[iid];
    MeshletHdr32 m = hdrs[mid];
    ObjIn o;
    if (vid >= uint(m.tri_cnt) * 3u) {
        o.pos = float4(0.0f, 0.0f, -2.0f, 1.0f); o.nrm = half3(0,1,0); o.col = half4(0); o.gid = 0u; return o;
    }
    Vtx12 v = verts[m.vtx_offset + uint(lidx[m.idx_offset + vid])];
    ObjState48 st = states[m.obj_id];
    float3 lp = (float3(-1.5f) + float3(float(v.px), float(v.py), float(v.pz)) * (3.0f / 65535.0f)) *
                float3(float(st.sx), float(st.sy), float(st.sz));
    lp.y += sin(float(st.dof0) + lp.x) * 0.08f;
    float4 q = float4(float(st.qx), float(st.qy), float(st.qz), float(st.qw));
    float3 wp = rotate_quat(q, lp) + float3(st.px, st.py, st.pz);
    o.pos = project_persp(to_view(wp, u), u);
    o.nrm = half3(rotate_quat(q, oct_dec(v.oct_x, v.oct_y)));
    o.col = unpack_unorm4x8_to_half(st.rgba8_mat);
    o.gid = (1u << 30u) | ((uint(m.obj_id) & 0x3FFFu) << 16u) | ((uint(v.joint_id) & 0xFFu) << 8u);
    return o;
}
fragment half4 f_mesh_rgb(ObjIn in [[stage_in]]) {
    half nd = clamp(dot(normalize(in.nrm), normalize(half3(0.4h, 0.7h, -0.5h))), 0.18h, 1.0h);
    return half4(in.col.rgb * nd, 1.0h);
}
fragment MrtOut f_mesh_mrt(ObjIn in [[stage_in]]) {
    half nd = clamp(dot(normalize(in.nrm), normalize(half3(0.4h, 0.7h, -0.5h))), 0.18h, 1.0h);
    uint qd = uint(clamp(in.pos.z, 0.0f, 1.0f) * 255.0f + 0.5f);
    return MrtOut{half4(in.col.rgb * nd, 1.0h), in.gid | qd};
}
fragment uint f_mesh_id_only(ObjIn in [[stage_in]]) {
    uint qd = uint(clamp(in.pos.z, 0.0f, 1.0f) * 255.0f + 0.5f);
    return in.gid | qd;
}

struct ProxyIn { float4 pos [[position]]; uint idx [[flat]]; };
vertex ProxyIn v_proxy(uint vid [[vertex_id]], uint iid [[instance_id]],
                       device const Analytic32 *prims [[buffer(0)]], constant FrameUniforms &u [[buffer(1)]]) {
    Analytic32 sp = prims[iid];
    float3 vc = to_view(float3(sp.cx, sp.cy, sp.cz), u);
    float aspect = u.vp_w / u.vp_h;
    float r = sp.radius;
    if (vc.z + r < u.near_z) return ProxyIn{float4(0, 0, -2, 1), iid};
    float hy = (r * u.focal) / max(vc.z - r, 0.25f), hx = hy / aspect;
    float2 c = float2((vc.x * u.focal) / (max(vc.z, 0.25f) * aspect), (vc.y * u.focal) / max(vc.z, 0.25f));
    float2 cr = float2((vid & 1u) ? 1.0f : -1.0f, (vid & 2u) ? 1.0f : -1.0f);
    float nz = clamp(0.20f + (1.0f - (2.0f / max(vc.z - r * 0.45f, 0.25f))) * 0.78f, 0.20f, 0.98f);
    return ProxyIn{float4(c + cr * float2(hx, hy), nz, 1.0f), iid};
}
fragment half4 f_proxy_rgb(ProxyIn in [[stage_in]], device const Analytic32 *prims [[buffer(0)]], constant FrameUniforms &u [[buffer(1)]]) {
    Analytic32 sp = prims[in.idx];
    float2 uv = (in.pos.xy / float2(u.vp_w, u.vp_h)) * 2.0f - 1.0f; uv.y = -uv.y;
    float3 ro = float3(u.cam_pos);
    float3 rd = normalize(float3(u.cam_right) * (uv.x * (u.vp_w / u.vp_h) / u.focal) + float3(u.cam_up) * (uv.y / u.focal) + float3(u.cam_fwd));
    float3 sc = float3(sp.cx, sp.cy, sp.cz), oc = ro - sc;
    float b = dot(oc, rd), c = dot(oc, oc) - sp.radius * sp.radius, h = b * b - c;
    if (h < 0.0f) return half4(0.0h);
    half3 n = half3(normalize((ro + rd * (-b - sqrt(h))) - sc));
    return half4(unpack_unorm4x8_to_half(sp.rgba8).rgb * clamp(dot(n, normalize(half3(0.4h, 0.7h, -0.5h))), 0.22h, 1.0h), 1.0h);
}
fragment MrtOut f_proxy_mrt(ProxyIn in [[stage_in]], device const Analytic32 *prims [[buffer(0)]], constant FrameUniforms &u [[buffer(1)]]) {
    Analytic32 sp = prims[in.idx];
    float2 uv = (in.pos.xy / float2(u.vp_w, u.vp_h)) * 2.0f - 1.0f; uv.y = -uv.y;
    float3 ro = float3(u.cam_pos);
    float3 rd = normalize(float3(u.cam_right) * (uv.x * (u.vp_w / u.vp_h) / u.focal) + float3(u.cam_up) * (uv.y / u.focal) + float3(u.cam_fwd));
    float3 sc = float3(sp.cx, sp.cy, sp.cz), oc = ro - sc;
    float b = dot(oc, rd), c = dot(oc, oc) - sp.radius * sp.radius, h = b * b - c;
    if (h < 0.0f) discard_fragment();
    half3 n = half3(normalize((ro + rd * (-b - sqrt(h))) - sc));
    uint slot = (sp.prim_kind_flags >> 16u) & 0x3FFFu;
    uint qd = uint(clamp(in.pos.z, 0.0f, 1.0f) * 255.0f + 0.5f);
    return MrtOut{half4(unpack_unorm4x8_to_half(sp.rgba8).rgb * clamp(dot(n, normalize(half3(0.4h, 0.7h, -0.5h))), 0.22h, 1.0h), 1.0h),
                  (2u << 30u) | (slot << 16u) | qd};
}
fragment uint f_proxy_id_only(ProxyIn in [[stage_in]], device const Analytic32 *prims [[buffer(0)]], constant FrameUniforms &u [[buffer(1)]]) {
    Analytic32 sp = prims[in.idx];
    float2 uv = (in.pos.xy / float2(u.vp_w, u.vp_h)) * 2.0f - 1.0f; uv.y = -uv.y;
    float3 ro = float3(u.cam_pos);
    float3 rd = normalize(float3(u.cam_right) * (uv.x * (u.vp_w / u.vp_h) / u.focal) + float3(u.cam_up) * (uv.y / u.focal) + float3(u.cam_fwd));
    float3 sc = float3(sp.cx, sp.cy, sp.cz), oc = ro - sc;
    float b = dot(oc, rd), c = dot(oc, oc) - sp.radius * sp.radius;
    if (b * b - c < 0.0f) discard_fragment();
    uint slot = (sp.prim_kind_flags >> 16u) & 0x3FFFu;
    uint qd = uint(clamp(in.pos.z, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (2u << 30u) | (slot << 16u) | qd;
}

struct HudIn { float4 pos [[position]]; uint idx [[flat]]; };
vertex HudIn v_hud(uint vid [[vertex_id]], uint iid [[instance_id]],
                   device const QuadHot32 *quads [[buffer(0)]],
                   device const ClipRect16 *clips [[buffer(1)]],
                   constant FrameUniforms &u [[buffer(2)]]) {
    QuadHot32 q = quads[iid];
    ClipRect16 c = clips[q.clip_id];
    float4 b = float4(float(q.b0), float(q.b1), float(q.b2), float(q.b3)) * 0.25f;
    float2 p0 = max(b.xy, c.rect.xy), p1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    if (p1.x <= p0.x || p1.y <= p0.y) return HudIn{float4(0, 0, -2, 1), iid};
    float2 px = mix(p0, p1, float2(float(vid & 1u), float((vid >> 1u) & 1u)));
    float z = 0.14f - clamp(float(q.z_flags & 0xFFFFu) * 1e-5f, 0.0f, 0.12f);
    return HudIn{float4(to_ndc_2d(px, u), z, 1.0f), iid};
}
inline half hud_sdf_alpha(QuadHot32 q, float2 pos_px, thread half4 &out_col) {
    half4 b = half4(half(q.b0), half(q.b1), half(q.b2), half(q.b3)) * 0.25h;
    half2 hs = b.zw * 0.5h, c2p = (half2(pos_px) - b.xy) - hs;
    half4 rr = half4(half(q.r0), half(q.r1), half(q.r2), half(q.r3)) * 0.25h;
    half4 bb = half4(half(q.bw0), half(q.bw1), half(q.bw2), half(q.bw3)) * 0.25h;
    half r = (c2p.x < 0.0h) ? (c2p.y < 0.0h ? rr.x : rr.w) : (c2p.y < 0.0h ? rr.y : rr.z);
    half bw = min(c2p.x < 0.0h ? bb.w : bb.y, c2p.y < 0.0h ? bb.x : bb.z);
    half2 d = abs(c2p) - hs + r;
    half outer = length(max(d, 0.0h)) + min(max(d.x, d.y), 0.0h) - r;
    out_col = mix(unpack_unorm4x8_to_half(q.rgba8_border), unpack_unorm4x8_to_half(q.rgba8_fill), clamp(0.5h - (outer + bw), 0.0h, 1.0h));
    half cov = clamp(0.5h - outer, 0.0h, 1.0h);
    out_col.a *= cov;
    return cov;
}
fragment half4 f_hud_rgb(HudIn in [[stage_in]], device const QuadHot32 *quads [[buffer(0)]]) {
    half4 col; hud_sdf_alpha(quads[in.idx], in.pos.xy, col); return col;
}
fragment MrtOut f_hud_mrt(HudIn in [[stage_in]], device const QuadHot32 *quads [[buffer(0)]]) {
    QuadHot32 q = quads[in.idx];
    half4 col; half cov = hud_sdf_alpha(q, in.pos.xy, col);
    if (cov < 0.05h || (col.a < 0.02h && ((q.z_flags & (1u << 28u)) == 0u))) discard_fragment();
    uint part = (q.z_flags >> 16u) & 0xFFu;
    uint qd = uint(clamp(in.pos.z, 0.0f, 1.0f) * 255.0f + 0.5f);
    uint gid = (q.style_id > 0u) ? ((3u << 30u) | (uint(q.style_id & 0x3FFFu) << 16u) | (part << 8u) | qd) : 0u;
    return MrtOut{col, gid};
}
fragment uint f_hud_id_only(HudIn in [[stage_in]], device const QuadHot32 *quads [[buffer(0)]],
                              constant float &pos_scale [[buffer(1)]]) {
    QuadHot32 q = quads[in.idx];
    half4 col; half cov = hud_sdf_alpha(q, in.pos.xy * pos_scale, col);
    if (cov < 0.05h || (col.a < 0.02h && ((q.z_flags & (1u << 28u)) == 0u))) discard_fragment();
    uint part = (q.z_flags >> 16u) & 0xFFu;
    uint qd = uint(clamp(in.pos.z, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (q.style_id > 0u) ? ((3u << 30u) | (uint(q.style_id & 0x3FFFu) << 16u) | (part << 8u) | qd) : 0u;
}

inline void emit_ground_stat(uint id_val, uint px_cnt, uint mn_x, uint mn_y, uint mx_x, uint mx_y,
                             uint anc, uint part, uint min_d, uint slot_base, uint max_slots,
                             device GroundStatAtomic *stats) {
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

struct TilePx {
    half4 col [[color(0)]];
    uint  val [[color(1)]];
};

kernel void k_tile_ground_reduce(imageblock<TilePx, imageblock_layout_implicit> img,
                                 device GroundStatAtomic *stats [[buffer(0)]],
                                 device uint *pick_out [[buffer(1)]],
                                 constant GroundParams &gp [[buffer(2)]],
                                 ushort2 tid [[thread_position_in_threadgroup]],
                                 ushort2 tsz [[threads_per_threadgroup]],
                                 uint2 gid [[thread_position_in_grid]]) {
    bool in_vp = (gid.x < gp.vp_w && gid.y < gp.vp_h);
    uint val = in_vp ? img.read(tid).val : 0u;
    if (in_vp && gid.x == gp.pick_x && gid.y == gp.pick_y) {
        pick_out[0] = val;
    }
    uint id_key = val & 0xFFFF0000u;
    if (simd_all(id_key == 0u)) return;

    uint first_key = simd_broadcast_first(id_key);
    bool full_vp_simd = simd_all(in_vp);
    if (full_vp_simd && first_key != 0u && simd_all(id_key == first_key)) {
        uint mn_x = simd_min(gid.x), mx_x = simd_max(gid.x);
        uint mn_y = simd_min(gid.y), mx_y = simd_max(gid.y);
        uint d = val & 0xFFu;
        uint min_d = simd_min(d);
        uint part = (val >> 8u) & 0xFFu;
        bool interior = (tid.x >= 4u && tid.x + 4u < tsz.x && tid.y >= 2u && tid.y + 2u < tsz.y);
        uint sc = interior ? 62u : 30u;
        uint anc = (sc << 26u) | ((gid.x & 0x1FFFu) << 13u) | (gid.y & 0x1FFFu);
        if (simd_is_first()) {
            emit_ground_stat(first_key, 32u, mn_x, mn_y, mx_x, mx_y, anc, part, min_d,
                             gp.max_slots / 3u, gp.max_slots, stats);
        }
        return;
    }

    if (id_key == 0u) return;
    uint x0 = uint(max(int(tid.x) - 6, 0)), x1 = uint(min(int(tid.x) + 6, int(tsz.x) - 1));
    uint y0 = uint(max(int(tid.y) - 6, 0)), y1 = uint(min(int(tid.y) + 6, int(tsz.y) - 1));
    uint score = 1u;
    score += ((img.read(ushort2(x0, tid.y)).val & 0xFFFF0000u) == id_key) ? 14u : 0u;
    score += ((img.read(ushort2(x1, tid.y)).val & 0xFFFF0000u) == id_key) ? 14u : 0u;
    score += ((img.read(ushort2(tid.x, y0)).val & 0xFFFF0000u) == id_key) ? 14u : 0u;
    score += ((img.read(ushort2(tid.x, y1)).val & 0xFFFF0000u) == id_key) ? 14u : 0u;
    if (tid.x >= 2u && tid.x + 2u < tsz.x && tid.y >= 2u && tid.y + 2u < tsz.y) score += 6u;
    uint anc = (min(score, 63u) << 26u) | ((gid.x & 0x1FFFu) << 13u) | (gid.y & 0x1FFFu);
    emit_ground_stat(id_key, 1u, gid.x, gid.y, gid.x, gid.y, anc, (val >> 8u) & 0xFFu,
                     val & 0xFFu, gp.max_slots / 3u, gp.max_slots, stats);
}

kernel void k_compute_ground_reduce(texture2d<uint, access::read> id_tex [[texture(0)]],
                                    device GroundStatAtomic *stats [[buffer(0)]],
                                    device uint *pick_out [[buffer(1)]],
                                    constant GroundParams &gp [[buffer(2)]],
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
        pick_out[0] = val;
    }
    uint id_key = val & 0xFFFF0000u;
    if (simd_all(id_key == 0u)) return;
    uint first_key = simd_broadcast_first(id_key);
    if (simd_all(in_vp) && first_key != 0u && simd_all(id_key == first_key)) {
        bool interior = (tid.x >= 2u && tid.x + 2u < 16u && tid.y >= 2u && tid.y + 2u < 16u);
        uint anc = ((interior ? 60u : 28u) << 26u) | ((px & 0x1FFFu) << 13u) | (py & 0x1FFFu);
        if (simd_is_first()) {
            emit_ground_stat(first_key, 32u * area, simd_min(px), simd_min(py), simd_max(px), simd_max(py),
                             anc, (val >> 8u) & 0xFFu, val & 0xFFu, gp.max_slots / 3u, gp.max_slots, stats);
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

/* ======== Experiment 3: Track V2 Hardware TBDR 16x8 Tiled Render-and-Compare ======== */
struct ObsCell64 { short sx, sy; float fg; float lum; };
struct ObsSummaryAtomic { atomic_uint sum_x, sum_y, fg_cnt, pad; };
struct VisionHyp {
    float px, py, pz, yaw, pitch, roll, scale, dof0;
    float score_out, iou_out, lum_err, edge_err;
    uint tpl_id, pad0, pad1, pad2;
};
struct VisionParams { uint img_w, img_h, first_m, hyps_base; float focal; uint tile_base; uint n_hyps, full_vp; };

inline float sample_avg4(texture2d<half, access::read> cam_tex, uint w, uint h, int bx, int by) {
    float acc = 0.0f;
    for (int dy = 0; dy < 4; ++dy) for (int dx = 0; dx < 4; ++dx) {
        uint2 p = uint2(clamp(bx * 4 + dx, 0, int(w) - 1), clamp(by * 4 + dy, 0, int(h) - 1));
        half4 c = cam_tex.read(p);
        acc += float(0.299h * c.r + 0.587h * c.g + 0.114h * c.b);
    }
    return acc * (1.0f / 16.0f);
}

kernel void k_vision_prep_64(texture2d<half, access::read> cam_tex [[texture(0)]],
                             device ObsCell64 *obs64 [[buffer(0)]],
                             device ObsSummaryAtomic *summary [[buffer(1)]],
                             constant VisionParams &vp [[buffer(2)]],
                             uint2 tid [[thread_position_in_threadgroup]],
                             uint2 gid [[thread_position_in_grid]]) {
    threadgroup float tg_lum[10][10];
    float c = sample_avg4(cam_tex, vp.img_w, vp.img_h, int(gid.x), int(gid.y));
    tg_lum[tid.y + 1u][tid.x + 1u] = c;
    if (tid.x == 0u) tg_lum[tid.y + 1u][0u] = sample_avg4(cam_tex, vp.img_w, vp.img_h, int(gid.x) - 1, int(gid.y));
    if (tid.x == 7u) tg_lum[tid.y + 1u][9u] = sample_avg4(cam_tex, vp.img_w, vp.img_h, int(gid.x) + 1, int(gid.y));
    if (tid.y == 0u) tg_lum[0u][tid.x + 1u] = sample_avg4(cam_tex, vp.img_w, vp.img_h, int(gid.x), int(gid.y) - 1);
    if (tid.y == 7u) tg_lum[9u][tid.x + 1u] = sample_avg4(cam_tex, vp.img_w, vp.img_h, int(gid.x), int(gid.y) + 1);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    if (gid.x >= 64u || gid.y >= 64u) return;
    float fg = clamp((c - 0.14f) * 5.0f, 0.0f, 1.0f);
    float l_norm = clamp(c / 0.773f, 0.0f, 1.0f);
    float gx = -tg_lum[tid.y + 1u][tid.x] + tg_lum[tid.y + 1u][tid.x + 2u];
    float gy = -tg_lum[tid.y][tid.x + 1u] + tg_lum[tid.y + 2u][tid.x + 1u];
    bool is_edge = (sqrt(gx * gx + gy * gy) > 0.14f) && (gid.x > 6u || gid.y > 10u);
    uint idx = gid.y * 64u + gid.x;
    obs64[idx] = ObsCell64{is_edge ? short(gid.x) : short(-1), is_edge ? short(gid.y) : short(-1), fg, l_norm};
    if (fg > 0.45f && (gid.x > 12u || gid.y > 12u)) {
        atomic_fetch_add_explicit(&summary->sum_x, gid.x, memory_order_relaxed);
        atomic_fetch_add_explicit(&summary->sum_y, gid.y, memory_order_relaxed);
        atomic_fetch_add_explicit(&summary->fg_cnt, 1u, memory_order_relaxed);
    }
}

kernel void k_vision_jfa64(device const ObsCell64 *src [[buffer(0)]],
                           device ObsCell64 *dst [[buffer(1)]],
                           constant int &step [[buffer(2)]],
                           uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= 64u || gid.y >= 64u) return;
    ObsCell64 best = src[gid.y * 64u + gid.x];
    float best_d2 = (best.sx >= 0) ? float((int(gid.x)-int(best.sx))*(int(gid.x)-int(best.sx)) +
                                           (int(gid.y)-int(best.sy))*(int(gid.y)-int(best.sy))) : 1e9f;
    for (int dy = -1; dy <= 1; ++dy) {
        int ny = int(gid.y) + dy * step;
        if (ny < 0 || ny >= 64) continue;
        for (int dx = -1; dx <= 1; ++dx) {
            int nx = int(gid.x) + dx * step;
            if (nx < 0 || nx >= 64) continue;
            ObsCell64 c = src[uint(ny) * 64u + uint(nx)];
            if (c.sx < 0) continue;
            float rx = float(int(gid.x) - int(c.sx)), ry = float(int(gid.y) - int(c.sy));
            float d2 = rx * rx + ry * ry;
            if (d2 < best_d2) { best_d2 = d2; best.sx = c.sx; best.sy = c.sy; }
        }
    }
    dst[gid.y * 64u + gid.x] = best;
}

inline float3 mul_euler(float yaw, float pitch, float roll, float3 v) {
    float cy = cos(yaw), sy = sin(yaw), cp = cos(pitch), sp = sin(pitch), cr = cos(roll), sr = sin(roll);
    return float3(
        (cy*cr + sy*sp*sr) * v.x + (cr*sy*sp - cy*sr) * v.y + (cp*sy) * v.z,
        (cp*sr) * v.x            + (cp*cr) * v.y            + (-sp) * v.z,
        (cy*sp*sr - cr*sy) * v.x + (cy*cr*sp + sy*sr) * v.y + (cy*cp) * v.z
    );
}

struct VisionAtlasOut {
    float4 pos [[position]];
    float clip_d [[clip_distance]] [4];
    half3 nrm;
};
struct VisionAtlasFragIn {
    float4 pos [[position]];
    half3 nrm;
};

vertex VisionAtlasOut v_vision_atlas(uint vid [[vertex_id]], uint iid [[instance_id]],
                                     device const VisionHyp *hyps [[buffer(0)]],
                                     device const MeshletHdr32 *hdrs [[buffer(1)]],
                                     device const Vtx12 *verts [[buffer(2)]],
                                     device const uchar *lidx [[buffer(3)]],
                                     constant VisionParams &vp [[buffer(4)]]) {
    uint local_m = vid / 252u;
    uint lv_i = vid % 252u;
    MeshletHdr32 m = hdrs[vp.first_m + local_m];
    VisionAtlasOut o;
    if (lv_i >= uint(m.tri_cnt) * 3u) {
        o.pos = float4(0, 0, -2, 1); o.nrm = half3(0,1,0);
        o.clip_d[0] = -1; o.clip_d[1] = -1; o.clip_d[2] = -1; o.clip_d[3] = -1;
        return o;
    }
    VisionHyp h = hyps[vp.hyps_base + iid];
    Vtx12 v = verts[m.vtx_offset + uint(lidx[m.idx_offset + lv_i])];
    float3 lp = (float3(-1.5f) + float3(float(v.px), float(v.py), float(v.pz)) * (3.0f / 65535.0f)) * h.scale;
    if (v.joint_id != 0u) lp.y += sin(h.dof0 + lp.x) * 0.08f;
    float3 wp = mul_euler(h.yaw, h.pitch, h.roll, lp) + float3(h.px, h.py, h.pz);
    float3 wn = normalize(mul_euler(h.yaw, h.pitch, h.roll, oct_dec(v.oct_x, v.oct_y)));
    float z = max(wp.z, 0.25f);
    float2 loc = float2((wp.x * vp.focal) / z, (wp.y * vp.focal) / z);
    o.clip_d[0] = 1.0f + loc.x; o.clip_d[1] = 1.0f - loc.x;
    o.clip_d[2] = 1.0f + loc.y; o.clip_d[3] = 1.0f - loc.y;
    float pz = clamp(0.10f + (1.0f - (1.0f / z)) * 0.85f, 0.05f, 0.98f);
    if (vp.full_vp != 0u) {
        o.pos = float4(loc.x, loc.y, pz, 1.0f);
    } else {
        uint tile_i = vp.tile_base + iid;
        uint tx = tile_i & 15u, ty = tile_i >> 4u;
        float gx = -1.0f + (2.0f / 16.0f) * (float(tx) + 0.5f + 0.5f * loc.x);
        float gy =  1.0f - (2.0f / 8.0f)  * (float(ty) + 0.5f - 0.5f * loc.y);
        o.pos = float4(gx, gy, pz, 1.0f);
    }
    o.nrm = half3(wn);
    return o;
}

fragment half4 f_vision_atlas(VisionAtlasFragIn in [[stage_in]]) {
    half ndl = clamp(dot(normalize(in.nrm), normalize(half3(0.35h, 0.60h, -0.55h))) + 0.25h, 0.35h, 1.0h);
    return half4(1.0h, ndl, ndl, 1.0h);
}

fragment half4 f_vision_gt(VisionAtlasFragIn in [[stage_in]]) {
    half ndl = clamp(dot(normalize(in.nrm), normalize(half3(0.35h, 0.60h, -0.55h))) + 0.25h, 0.35h, 1.0h);
    return half4(half3(0.55h, 0.85h, 0.96h) * ndl, 1.0h);
}

kernel void k_vision_score_tiles(texture2d<half, access::read> atlas [[texture(0)]],
                                 device const ObsCell64 *obs64 [[buffer(0)]],
                                 device VisionHyp *hyps [[buffer(1)]],
                                 constant VisionParams &vp [[buffer(2)]],
                                 uint tid [[thread_index_in_threadgroup]],
                                 uint tgid [[threadgroup_position_in_grid]]) {
    if (tgid >= vp.n_hyps) return;
    threadgroup float tg_inter[256], tg_union[256], tg_lum[256], tg_dt[256], tg_ecnt[256];
    uint tile_i = vp.tile_base + tgid;
    uint tx = tile_i & 15u, ty = tile_i >> 4u;
    uint bx = tx * 64u, by = ty * 64u;
    float s_inter = 0.0f, s_union = 0.0f, s_lum = 0.0f, s_dt = 0.0f, s_ecnt = 0.0f;
    for (uint p = tid; p < 4096u; p += 256u) {
        uint lx = p & 63u, ly = p >> 6u;
        half2 rv = atlas.read(uint2(bx + lx, by + ly)).rg;
        float rm = float(rv.r), rl = float(rv.g);
        ObsCell64 o = obs64[p];
        s_inter += min(rm, o.fg);
        s_union += max(rm, o.fg);
        if (rm > 0.5f && o.fg > 0.5f) s_lum += abs(rl - o.lum);
        if (rm > 0.5f) {
            bool bnd = (lx == 0u || lx == 63u || ly == 0u || ly == 63u) ||
                       (atlas.read(uint2(bx + lx - 1u, by + ly)).r < 0.5h) ||
                       (atlas.read(uint2(bx + lx + 1u, by + ly)).r < 0.5h) ||
                       (atlas.read(uint2(bx + lx, by + ly - 1u)).r < 0.5h) ||
                       (atlas.read(uint2(bx + lx, by + ly + 1u)).r < 0.5h);
            if (bnd) {
                float d = (o.sx >= 0) ? sqrt(float((int(lx)-int(o.sx))*(int(lx)-int(o.sx)) +
                                                   (int(ly)-int(o.sy))*(int(ly)-int(o.sy)))) : 16.0f;
                s_dt += min(d, 16.0f);
                s_ecnt += 1.0f;
            }
        }
    }
    tg_inter[tid] = s_inter; tg_union[tid] = s_union; tg_lum[tid] = s_lum; tg_dt[tid] = s_dt; tg_ecnt[tid] = s_ecnt;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    for (uint off = 128u; off > 0u; off >>= 1u) {
        if (tid < off) {
            tg_inter[tid] += tg_inter[tid + off]; tg_union[tid] += tg_union[tid + off];
            tg_lum[tid] += tg_lum[tid + off]; tg_dt[tid] += tg_dt[tid + off]; tg_ecnt[tid] += tg_ecnt[tid + off];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    if (tid == 0u) {
        float iou = (tg_union[0] > 4.0f) ? (tg_inter[0] / tg_union[0]) : 0.0f;
        float lum_m = (tg_inter[0] > 4.0f) ? (tg_lum[0] / tg_inter[0]) : 1.0f;
        float dt_m = (tg_ecnt[0] > 4.0f) ? (tg_dt[0] / tg_ecnt[0]) : 16.0f;
        float loss = (1.0f - iou) * 24.0f + dt_m * 1.5f + lum_m * 8.0f;
        uint hi = vp.hyps_base + tgid;
        hyps[hi].iou_out = iou;
        hyps[hi].lum_err = lum_m;
        hyps[hi].edge_err = dt_m;
        hyps[hi].score_out = loss;
    }
}
)";

static void build_torus_mesh(std::vector<MeshletHdr32> &hdrs, std::vector<Vtx12> &verts, std::vector<uint8_t> &lidx,
                             uint32_t obj_id, uint32_t base_vtx, float r_maj = 0.85f, float r_min = 0.35f) {
    uint32_t rings = 2u, segs = 4u, mcnt = rings * segs;
    if (lidx.empty()) {
        lidx.resize(kClusterTri * 3u);
        uint32_t it = 0u;
        for (uint32_t iy = 0u; iy < 6u; ++iy) {
            for (uint32_t ix = 0u; ix < 7u; ++ix) {
                uint8_t i0 = uint8_t(iy * 8u + ix), i1 = i0 + 1u, i2 = i0 + 8u, i3 = i2 + 1u;
                lidx[it*3+0]=i0; lidx[it*3+1]=i2; lidx[it*3+2]=i1; ++it;
                lidx[it*3+0]=i1; lidx[it*3+1]=i2; lidx[it*3+2]=i3; ++it;
            }
        }
    }
    for (uint32_t m = 0u; m < mcnt; ++m) {
        float u0 = float(m % segs) / float(segs) * 6.2831853f;
        float u1 = float((m % segs) + 1u) / float(segs) * 6.2831853f;
        float v0 = float(m / segs) / float(rings) * 6.2831853f;
        float v1 = float((m / segs) + 1u) / float(rings) * 6.2831853f;
        float cx = 0, cy = 0, cz = 0;
        size_t vs = verts.size(); verts.resize(vs + kClusterVtx);
        for (uint32_t iy = 0u; iy < 7u; ++iy) {
            float phi = v0 + (float(iy) / 6.0f) * (v1 - v0);
            for (uint32_t ix = 0u; ix < 8u; ++ix) {
                float th = u0 + (float(ix) / 7.0f) * (u1 - u0);
                float px = (r_maj + r_min * cosf(phi)) * cosf(th);
                float py = r_min * sinf(phi);
                float pz = (r_maj + r_min * cosf(phi)) * sinf(th);
                float nx = cosf(phi) * cosf(th), ny = sinf(phi), nz = cosf(phi) * sinf(th);
                Vtx12 &v = verts[vs + iy * 8u + ix];
                v.px = pack_pos_u16(px); v.py = pack_pos_u16(py); v.pz = pack_pos_u16(pz);
                int8_t o[2]; oct_encode(nx, ny, nz, o); v.oct_x = o[0]; v.oct_y = o[1];
                v.joint_id = uint8_t(m & 3u);
                cx += px; cy += py; cz += pz;
            }
        }
        MeshletHdr32 h{};
        h.vtx_offset = base_vtx + uint32_t(vs); h.vtx_cnt = 56u; h.tri_cnt = kClusterTri; h.obj_id = uint16_t(obj_id);
        h.bs_f16[0] = f32_to_f16(cx / 56.0f); h.bs_f16[1] = f32_to_f16(cy / 56.0f);
        h.bs_f16[2] = f32_to_f16(cz / 56.0f); h.bs_f16[3] = f32_to_f16(1.35f);
        h.cone_z = 127; h.cone_c = 127;
        hdrs.push_back(h);
    }
}

static void build_box_mesh(std::vector<MeshletHdr32> &hdrs, std::vector<Vtx12> &verts, std::vector<uint8_t> &lidx,
                           float hx, float hy, float hz) {
    uint32_t vbase = uint32_t(verts.size()), ibase = uint32_t(lidx.size());
    const float fverts[6][4][3] = {
        {{ hx,-hy,-hz},{ hx, hy,-hz},{ hx, hy, hz},{ hx,-hy, hz}},
        {{-hx,-hy, hz},{-hx, hy, hz},{-hx, hy,-hz},{-hx,-hy,-hz}},
        {{-hx, hy,-hz},{-hx, hy, hz},{ hx, hy, hz},{ hx, hy,-hz}},
        {{-hx,-hy, hz},{-hx,-hy,-hz},{ hx,-hy,-hz},{ hx,-hy, hz}},
        {{-hx,-hy, hz},{ hx,-hy, hz},{ hx, hy, hz},{-hx, hy, hz}},
        {{ hx,-hy,-hz},{-hx,-hy,-hz},{-hx, hy,-hz},{ hx, hy,-hz}}
    };
    const float fnrms[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (int f = 0; f < 6; ++f) {
        uint8_t base_i = uint8_t(f * 4);
        int8_t on[2]; oct_encode(fnrms[f][0], fnrms[f][1], fnrms[f][2], on);
        for (int c = 0; c < 4; ++c) {
            Vtx12 v{};
            v.px = pack_pos_u16(fverts[f][c][0]); v.py = pack_pos_u16(fverts[f][c][1]); v.pz = pack_pos_u16(fverts[f][c][2]);
            v.oct_x = on[0]; v.oct_y = on[1];
            verts.push_back(v);
        }
        lidx.push_back(base_i+0); lidx.push_back(base_i+1); lidx.push_back(base_i+2);
        lidx.push_back(base_i+0); lidx.push_back(base_i+2); lidx.push_back(base_i+3);
    }
    MeshletHdr32 h{};
    h.vtx_offset = vbase; h.idx_offset = ibase; h.vtx_cnt = 24u; h.tri_cnt = 12u;
    h.bs_f16[3] = f32_to_f16(1.35f); h.cone_z = 127; h.cone_c = 127;
    hdrs.push_back(h);
}

static void build_octa_mesh(std::vector<MeshletHdr32> &hdrs, std::vector<Vtx12> &verts, std::vector<uint8_t> &lidx,
                            float rx, float ry, float rz) {
    uint32_t vbase = uint32_t(verts.size()), ibase = uint32_t(lidx.size());
    const float pv[6][3] = {{rx,0,0},{-rx,0,0},{0,ry,0},{0,-ry,0},{0,0,rz},{0,0,-rz}};
    const int tris[8][3] = {{0,2,4},{4,2,1},{1,2,5},{5,2,0},{4,3,0},{1,3,4},{5,3,1},{0,3,5}};
    for (int f = 0; f < 8; ++f) {
        const float *a = pv[tris[f][0]], *b = pv[tris[f][1]], *c = pv[tris[f][2]];
        float ux=b[0]-a[0], uy=b[1]-a[1], uz=b[2]-a[2], vx=c[0]-a[0], vy=c[1]-a[1], vz=c[2]-a[2];
        float nx = uy*vz - uz*vy, ny = uz*vx - ux*vz, nz = ux*vy - uy*vx;
        if (nx*(a[0]+b[0]+c[0]) + ny*(a[1]+b[1]+c[1]) + nz*(a[2]+b[2]+c[2]) < 0) { nx=-nx; ny=-ny; nz=-nz; }
        float il = 1.0f / std::max(sqrtf(nx*nx+ny*ny+nz*nz), 1e-6f);
        int8_t on[2]; oct_encode(nx*il, ny*il, nz*il, on);
        const float *pts[3] = {a, b, c};
        for (int k = 0; k < 3; ++k) {
            Vtx12 v{}; v.px = pack_pos_u16(pts[k][0]); v.py = pack_pos_u16(pts[k][1]); v.pz = pack_pos_u16(pts[k][2]);
            v.oct_x = on[0]; v.oct_y = on[1];
            verts.push_back(v);
            lidx.push_back(uint8_t(f * 3 + k));
        }
    }
    MeshletHdr32 h{};
    h.vtx_offset = vbase; h.idx_offset = ibase; h.vtx_cnt = 24u; h.tri_cnt = 8u;
    h.bs_f16[3] = f32_to_f16(1.35f); h.cone_z = 127; h.cone_c = 127;
    hdrs.push_back(h);
}

static void add_quad(std::vector<QuadHot32> &q, float x, float y, float w, float h, uint32_t fill,
                     uint32_t border, float rad, float bw, uint16_t clip, uint16_t style_id, uint32_t z, uint32_t flags = 0u) {
    QuadHot32 o{};
    o.b0 = pack_q14_2(x); o.b1 = pack_q14_2(y); o.b2 = pack_q14_2(w); o.b3 = pack_q14_2(h);
    o.rgba8_fill = fill; o.rgba8_border = border;
    o.r[0]=o.r[1]=o.r[2]=o.r[3]=pack_q6_2(rad);
    o.bw[0]=o.bw[1]=o.bw[2]=o.bw[3]=pack_q6_2(bw);
    o.clip_id = clip; o.style_id = style_id;
    o.z_flags = (z & 0xFFFFu) | flags;
    q.push_back(o);
}

static double median_of(std::vector<double> v) {
    std::sort(v.begin(), v.end()); return v[v.size() / 2];
}
static double p95_of(std::vector<double> v) {
    std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, (v.size() * 95) / 100)];
}

} // namespace

int main(int argc, char **argv) {
    const char *json_out = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) json_out = argv[++i];
    }

    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        id<MTLCommandQueue> q = [dev newCommandQueue];
        NSError *err = nil;
        MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
        opts.languageVersion = MTLLanguageVersion3_0;
        id<MTLLibrary> lib = [dev newLibraryWithSource:[NSString stringWithUTF8String:kProbeMsl] options:opts error:&err];
        if (!lib || err) { fprintf(stderr, "MSL compile failed: %s\n", err.localizedDescription.UTF8String); return 1; }

        auto make_c = [&](const char *fn) {
            id<MTLComputePipelineState> p = [dev newComputePipelineStateWithFunction:[lib newFunctionWithName:[NSString stringWithUTF8String:fn]] error:&err];
            if (!p) { fprintf(stderr, "CPSO %s err: %s\n", fn, err.localizedDescription.UTF8String); exit(1); }
            return p;
        };
        auto make_r = [&](const char *vf, const char *ff, bool mrt, bool id_only, bool blend) {
            MTLRenderPipelineDescriptor *d = [[MTLRenderPipelineDescriptor alloc] init];
            d.vertexFunction = [lib newFunctionWithName:[NSString stringWithUTF8String:vf]];
            d.fragmentFunction = [lib newFunctionWithName:[NSString stringWithUTF8String:ff]];
            if (id_only) {
                d.colorAttachments[0].pixelFormat = MTLPixelFormatR32Uint;
            } else {
                d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
                if (blend) {
                    d.colorAttachments[0].blendingEnabled = YES;
                    d.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
                    d.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
                    d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
                    d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                    d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
                    d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                }
                if (mrt) {
                    d.colorAttachments[1].pixelFormat = MTLPixelFormatR32Uint;
                    d.colorAttachments[1].blendingEnabled = NO;
                }
            }
            d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            id<MTLRenderPipelineState> p = [dev newRenderPipelineStateWithDescriptor:d error:&err];
            if (!p) { fprintf(stderr, "RPSO %s/%s err: %s\n", vf, ff, err.localizedDescription.UTF8String); exit(1); }
            return p;
        };

        MTLTileRenderPipelineDescriptor *td = [[MTLTileRenderPipelineDescriptor alloc] init];
        td.tileFunction = [lib newFunctionWithName:@"k_tile_ground_reduce"];
        td.rasterSampleCount = 1;
        td.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        td.colorAttachments[1].pixelFormat = MTLPixelFormatR32Uint;
        td.threadgroupSizeMatchesTileSize = YES;
        id<MTLRenderPipelineState> p_tile = [dev newRenderPipelineStateWithTileDescriptor:td options:0 reflection:nil error:&err];
        if (!p_tile) { fprintf(stderr, "Tile PSO err: %s\n", err.localizedDescription.UTF8String); return 1; }

        id<MTLComputePipelineState> p_cull = make_c("k_cull");
        id<MTLComputePipelineState> p_cmp_red = make_c("k_compute_ground_reduce");
        id<MTLComputePipelineState> p_vis_prep = make_c("k_vision_prep_64");
        id<MTLComputePipelineState> p_vis_jfa = make_c("k_vision_jfa64");
        id<MTLComputePipelineState> p_vis_score = make_c("k_vision_score_tiles");

        auto make_vis_r = [&](const char *ff, MTLPixelFormat fmt) {
            MTLRenderPipelineDescriptor *d = [[MTLRenderPipelineDescriptor alloc] init];
            d.vertexFunction = [lib newFunctionWithName:@"v_vision_atlas"];
            d.fragmentFunction = [lib newFunctionWithName:[NSString stringWithUTF8String:ff]];
            d.colorAttachments[0].pixelFormat = fmt;
            d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
            id<MTLRenderPipelineState> p = [dev newRenderPipelineStateWithDescriptor:d error:&err];
            if (!p) { fprintf(stderr, "VisRPSO err: %s\n", err.localizedDescription.UTF8String); exit(1); }
            return p;
        };
        id<MTLRenderPipelineState> p_vis_atlas = make_vis_r("f_vision_atlas", MTLPixelFormatRG8Unorm);
        id<MTLRenderPipelineState> p_vis_gt = make_vis_r("f_vision_gt", MTLPixelFormatRGBA8Unorm);

        id<MTLRenderPipelineState> p_mesh_rgb = make_r("v_mesh", "f_mesh_rgb", false, false, false);
        id<MTLRenderPipelineState> p_mesh_mrt = make_r("v_mesh", "f_mesh_mrt", true, false, false);
        id<MTLRenderPipelineState> p_mesh_id  = make_r("v_mesh", "f_mesh_id_only", false, true, false);
        id<MTLRenderPipelineState> p_prx_rgb  = make_r("v_proxy", "f_proxy_rgb", false, false, false);
        id<MTLRenderPipelineState> p_prx_mrt  = make_r("v_proxy", "f_proxy_mrt", true, false, false);
        id<MTLRenderPipelineState> p_prx_id   = make_r("v_proxy", "f_proxy_id_only", false, true, false);
        id<MTLRenderPipelineState> p_hud_rgb  = make_r("v_hud", "f_hud_rgb", false, false, true);
        id<MTLRenderPipelineState> p_hud_mrt  = make_r("v_hud", "f_hud_mrt", true, false, true);
        id<MTLRenderPipelineState> p_hud_id   = make_r("v_hud", "f_hud_id_only", false, true, false);

        printf("=== Experiment 1: 4K (3840x2160) Grounding Bake-Off (Apple %s) ===\n", dev.name.UTF8String);
        printf("  Occupancy: cull=%lu tile=%lu compute_reduce=%lu prep=%lu jfa=%lu score=%lu\n",
               (unsigned long)p_cull.maxTotalThreadsPerThreadgroup,
               (unsigned long)p_tile.maxTotalThreadsPerThreadgroup,
               (unsigned long)p_cmp_red.maxTotalThreadsPerThreadgroup,
               (unsigned long)p_vis_prep.maxTotalThreadsPerThreadgroup,
               (unsigned long)p_vis_jfa.maxTotalThreadsPerThreadgroup,
               (unsigned long)p_vis_score.maxTotalThreadsPerThreadgroup);

        /* Build 4K benchmark scene: 256 3D objects, 32 analytics, UI panel + hidden + partially occluded slider */
        std::vector<MeshletHdr32> hdrs; std::vector<Vtx12> verts; std::vector<uint8_t> lidx;
        build_torus_mesh(hdrs, verts, lidx, 0u, 0u); // template 8 meshlets
        std::vector<MeshletHdr32> all_hdrs; std::vector<ObjState48> states(257u);
        for (uint32_t i = 1u; i <= 256u; ++i) {
            ObjState48 st{};
            st.px = (float((i - 1u) % 16u) - 7.5f) * 1.15f;
            st.py = (float((i - 1u) / 16u) - 7.5f) * 0.65f;
            st.pz = 11.0f + float(i & 7u) * 0.8f;
            st.quat_f16[3] = f32_to_f16(1.0f);
            st.scale_f16[0] = st.scale_f16[1] = st.scale_f16[2] = f32_to_f16(0.42f);
            st.rgba8_mat = pack_rgba(0x40, 0xD8, 0xF0, 0xFF);
            states[i] = st;
            for (auto h : hdrs) { h.obj_id = uint16_t(i); all_hdrs.push_back(h); }
        }
        std::vector<Analytic32> prims(32u);
        for (uint32_t i = 0u; i < 32u; ++i) {
            prims[i] = {(float(i % 8u) - 3.5f) * 1.8f, (float(i / 8u) - 1.5f) * 1.5f, 9.5f, 0.32f,
                        {0, f32_to_f16(1.0f), 0, 0}, pack_rgba(0xFF, 0x88, 0x38, 0xFF),
                        1u | (((i + 1u) & 0x3FFFu) << 16u)};
        }

        /* UI layout: 1=sidebar(40,40,480,920), 2=btn_ok, 3=btn_hidden(covered by modal), 4=sl_partial(center covered by modal), 5=modal */
        std::vector<ClipRect16> clips = {{0, 0, 3840, 2160}, {40, 40, 480, 920}};
        std::vector<QuadHot32> quads;
        add_quad(quads, 40, 40, 480, 920, pack_rgba(0x12,0x19,0x24,0xE8), pack_rgba(0x2D,0x44,0x5E,0xFF), 14, 2, 0, 1, 10);
        add_quad(quads, 60, 80, 440, 64, pack_rgba(0x22,0x34,0x4C,0xFF), pack_rgba(0x38,0xD0,0xFF,0xFF), 8, 1, 1, 2, 20);
        add_quad(quads, 60, 160, 440, 64, pack_rgba(0x22,0x34,0x4C,0xFF), pack_rgba(0x38,0xD0,0xFF,0xFF), 8, 1, 1, 3, 25); // hidden by modal!
        add_quad(quads, 60, 240, 440, 64, pack_rgba(0x1A,0x25,0x35,0xFF), pack_rgba(0x38,0xD0,0xFF,0xFF), 8, 1, 1, 4, 30); // sl_partial! center=(280,272)
        // 5=Modal covering x:[160..600], y:[140..288] -> 100% covers btn_hidden(60..500,160..224)? Wait: let modal cover x:[55..510], y:[150..278] -> covers 100% of btn_hidden (y=160..224) and covers top+center of sl_partial (y=240..278, center y=272 covered, leaving y=279..304 visible!)
        add_quad(quads, 55, 150, 455, 128, pack_rgba(0x1E,0x24,0x34,0xF5), pack_rgba(0xFF,0x50,0x90,0xFF), 12, 2, 0, 5, 100);
        for (int i = 0; i < 40; ++i) { // VList rows: row i >= 30 clipped below panel
            add_quad(quads, 60, 320 + float(i) * 28.0f, 440, 24, pack_rgba(0x18,0x20,0x2E,0xEE), 0, 4, 0, 1, uint16_t(6 + i), uint32_t(40 + i));
        }

        const uint32_t W = 3840, H = 2160;
        const uint32_t kMaxSlotPerDom = 512, kMaxSlots = kMaxSlotPerDom * 3u;
        id<MTLBuffer> b_vtx = [dev newBufferWithBytes:verts.data() length:verts.size()*sizeof(Vtx12) options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_lidx = [dev newBufferWithBytes:lidx.data() length:lidx.size() options:MTLResourceStorageModeShared];
        std::vector<uint16_t> id16(kClusterTri*3u); for (uint16_t i=0; i<kClusterTri*3u; ++i) id16[i]=i;
        id<MTLBuffer> b_id16 = [dev newBufferWithBytes:id16.data() length:id16.size()*2 options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_hdrs = [dev newBufferWithBytes:all_hdrs.data() length:all_hdrs.size()*sizeof(MeshletHdr32) options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_st = [dev newBufferWithBytes:states.data() length:states.size()*sizeof(ObjState48) options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_pr = [dev newBufferWithBytes:prims.data() length:prims.size()*sizeof(Analytic32) options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_qd = [dev newBufferWithBytes:quads.data() length:quads.size()*sizeof(QuadHot32) options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_cl = [dev newBufferWithBytes:clips.data() length:clips.size()*sizeof(ClipRect16) options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_surv = [dev newBufferWithLength:all_hdrs.size()*4 options:MTLResourceStorageModePrivate];
        id<MTLBuffer> b_ind = [dev newBufferWithLength:32 options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_gstats = [dev newBufferWithLength:kMaxSlots*sizeof(GroundStat32) options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_pick = [dev newBufferWithLength:16 options:MTLResourceStorageModeShared];

        MTLTextureDescriptor *cd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:W height:H mipmapped:NO];
        cd.usage = MTLTextureUsageRenderTarget; cd.storageMode = MTLStorageModePrivate; cd.allowGPUOptimizedContents = YES;
        id<MTLTexture> t_rgb = [dev newTextureWithDescriptor:cd];

        MTLTextureDescriptor *dd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:W height:H mipmapped:NO];
        dd.usage = MTLTextureUsageRenderTarget; dd.storageMode = MTLStorageModeMemoryless; dd.allowGPUOptimizedContents = YES;
        id<MTLTexture> t_depth = [dev newTextureWithDescriptor:dd];

        MTLTextureDescriptor *id_mem = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Uint width:W height:H mipmapped:NO];
        id_mem.usage = MTLTextureUsageRenderTarget; id_mem.storageMode = MTLStorageModeMemoryless; id_mem.allowGPUOptimizedContents = YES;
        id<MTLTexture> t_id_mem = [dev newTextureWithDescriptor:id_mem];

        MTLTextureDescriptor *id_priv = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Uint width:W height:H mipmapped:NO];
        id_priv.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; id_priv.storageMode = MTLStorageModePrivate;
        id<MTLTexture> t_id_priv = [dev newTextureWithDescriptor:id_priv];

        MTLTextureDescriptor *id_half = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Uint width:W/2 height:H/2 mipmapped:NO];
        id_half.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; id_half.storageMode = MTLStorageModePrivate;
        id<MTLTexture> t_id_half = [dev newTextureWithDescriptor:id_half];
        MTLTextureDescriptor *dh_half = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:W/2 height:H/2 mipmapped:NO];
        dh_half.usage = MTLTextureUsageRenderTarget; dh_half.storageMode = MTLStorageModeMemoryless;
        id<MTLTexture> t_depth_half = [dev newTextureWithDescriptor:dh_half];

        MTLDepthStencilDescriptor *dsd3 = [[MTLDepthStencilDescriptor alloc] init]; dsd3.depthCompareFunction = MTLCompareFunctionLess; dsd3.depthWriteEnabled = YES;
        id<MTLDepthStencilState> ds_3d = [dev newDepthStencilStateWithDescriptor:dsd3];
        MTLDepthStencilDescriptor *dsui = [[MTLDepthStencilDescriptor alloc] init]; dsui.depthCompareFunction = MTLCompareFunctionLessEqual; dsui.depthWriteEnabled = YES;
        id<MTLDepthStencilState> ds_ui = [dev newDepthStencilStateWithDescriptor:dsui];

        FrameUniforms u{float(W), float(H), 1.85f, 1.0f/1.85f, {0,0,-1.5f}, 0.5f, {1,0,0}, 80.0f, {0,1,0}, uint32_t(all_hdrs.size()), {0,0,1}, uint32_t(prims.size())};
        GroundParams gp{W, H, 280u, 272u, kMaxSlots, 0u, 0u, 0u};

        auto reset_ground_bufs = [&]() {
            auto *gs = static_cast<GroundStat32 *>(b_gstats.contents);
            for (uint32_t i = 0; i < kMaxSlots; ++i) {
                gs[i] = {0u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0u, 0u, 0u, 0u, 0xFFFFFFFFu};
            }
            std::memset(b_pick.contents, 0, 16);
        };

        auto run_mode = [&](int mode, uint32_t px, uint32_t py) -> std::vector<double> {
            gp.pick_x = px; gp.pick_y = py;
            std::vector<double> ms;
            for (int it = 0; it < 24; ++it) {
                auto *ia = static_cast<uint32_t *>(b_ind.contents);
                ia[0] = kClusterTri * 3u; ia[1]=0; ia[2]=0; ia[3]=0; ia[4]=0;
                if (mode > 0) reset_ground_bufs();
                gp.half_shift = (mode == 3) ? 1u : 0u;
                id<MTLCommandBuffer> cb = [q commandBuffer];
                id<MTLComputeCommandEncoder> ce = [cb computeCommandEncoder];
                [ce setComputePipelineState:p_cull];
                [ce setBuffer:b_hdrs offset:0 atIndex:0]; [ce setBuffer:b_st offset:0 atIndex:1];
                [ce setBytes:&u length:sizeof(u) atIndex:2]; [ce setBuffer:b_surv offset:0 atIndex:3];
                [ce setBuffer:b_ind offset:0 atIndex:4];
                [ce dispatchThreadgroups:MTLSizeMake((all_hdrs.size()+63)/64, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
                [ce endEncoding];

                MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
                rpd.colorAttachments[0].texture = t_rgb; rpd.colorAttachments[0].loadAction = MTLLoadActionClear;
                rpd.colorAttachments[0].storeAction = MTLStoreActionDontCare;
                rpd.depthAttachment.texture = t_depth; rpd.depthAttachment.loadAction = MTLLoadActionClear; rpd.depthAttachment.storeAction = MTLStoreActionDontCare; rpd.depthAttachment.clearDepth = 1.0;
                if (mode == 1 || mode == 2) {
                    rpd.colorAttachments[1].texture = (mode == 2) ? t_id_mem : t_id_priv;
                    rpd.colorAttachments[1].loadAction = MTLLoadActionClear;
                    rpd.colorAttachments[1].storeAction = (mode == 2) ? MTLStoreActionDontCare : MTLStoreActionStore;
                }
                id<MTLRenderCommandEncoder> re = [cb renderCommandEncoderWithDescriptor:rpd];
                [re setDepthStencilState:ds_3d]; [re setCullMode:MTLCullModeBack];
                [re setRenderPipelineState:(mode==1||mode==2)?p_mesh_mrt:p_mesh_rgb];
                [re setVertexBuffer:b_surv offset:0 atIndex:0]; [re setVertexBuffer:b_hdrs offset:0 atIndex:1];
                [re setVertexBuffer:b_vtx offset:0 atIndex:2]; [re setVertexBuffer:b_lidx offset:0 atIndex:3];
                [re setVertexBuffer:b_st offset:0 atIndex:4]; [re setVertexBytes:&u length:sizeof(u) atIndex:5];
                [re drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexType:MTLIndexTypeUInt16 indexBuffer:b_id16 indexBufferOffset:0 indirectBuffer:b_ind indirectBufferOffset:0];

                [re setCullMode:MTLCullModeNone];
                [re setRenderPipelineState:(mode==1||mode==2)?p_prx_mrt:p_prx_rgb];
                [re setVertexBuffer:b_pr offset:0 atIndex:0]; [re setVertexBytes:&u length:sizeof(u) atIndex:1];
                [re setFragmentBuffer:b_pr offset:0 atIndex:0]; [re setFragmentBytes:&u length:sizeof(u) atIndex:1];
                [re drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:prims.size()];

                [re setDepthStencilState:ds_ui];
                [re setRenderPipelineState:(mode==1||mode==2)?p_hud_mrt:p_hud_rgb];
                [re setVertexBuffer:b_qd offset:0 atIndex:0]; [re setVertexBuffer:b_cl offset:0 atIndex:1]; [re setVertexBytes:&u length:sizeof(u) atIndex:2];
                [re setFragmentBuffer:b_qd offset:0 atIndex:0];
                [re drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:quads.size()];

                if (mode == 2) {
                    [re setRenderPipelineState:p_tile];
                    [re setTileBuffer:b_gstats offset:0 atIndex:0];
                    [re setTileBuffer:b_pick offset:0 atIndex:1];
                    [re setTileBytes:&gp length:sizeof(gp) atIndex:2];
                    [re dispatchThreadsPerTile:MTLSizeMake(re.tileWidth, re.tileHeight, 1)];
                }
                [re endEncoding];

                if (mode == 3) {
                    FrameUniforms hfu = u; hfu.vp_w = float(W / 2u); hfu.vp_h = float(H / 2u);
                    float pos_scale = 2.0f;
                    MTLRenderPassDescriptor *hrpd = [MTLRenderPassDescriptor renderPassDescriptor];
                    hrpd.colorAttachments[0].texture = t_id_half; hrpd.colorAttachments[0].loadAction = MTLLoadActionClear; hrpd.colorAttachments[0].storeAction = MTLStoreActionStore;
                    hrpd.depthAttachment.texture = t_depth_half; hrpd.depthAttachment.loadAction = MTLLoadActionClear; hrpd.depthAttachment.storeAction = MTLStoreActionDontCare; hrpd.depthAttachment.clearDepth = 1.0;
                    id<MTLRenderCommandEncoder> hre = [cb renderCommandEncoderWithDescriptor:hrpd];
                    [hre setDepthStencilState:ds_3d]; [hre setCullMode:MTLCullModeBack]; [hre setRenderPipelineState:p_mesh_id];
                    [hre setVertexBuffer:b_surv offset:0 atIndex:0]; [hre setVertexBuffer:b_hdrs offset:0 atIndex:1];
                    [hre setVertexBuffer:b_vtx offset:0 atIndex:2]; [hre setVertexBuffer:b_lidx offset:0 atIndex:3];
                    [hre setVertexBuffer:b_st offset:0 atIndex:4]; [hre setVertexBytes:&u length:sizeof(u) atIndex:5];
                    [hre drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexType:MTLIndexTypeUInt16 indexBuffer:b_id16 indexBufferOffset:0 indirectBuffer:b_ind indirectBufferOffset:0];
                    [hre setCullMode:MTLCullModeNone]; [hre setRenderPipelineState:p_prx_id];
                    [hre setVertexBuffer:b_pr offset:0 atIndex:0]; [hre setVertexBytes:&u length:sizeof(u) atIndex:1];
                    [hre setFragmentBuffer:b_pr offset:0 atIndex:0]; [hre setFragmentBytes:&hfu length:sizeof(hfu) atIndex:1];
                    [hre drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:prims.size()];
                    [hre setDepthStencilState:ds_ui]; [hre setRenderPipelineState:p_hud_id];
                    [hre setVertexBuffer:b_qd offset:0 atIndex:0]; [hre setVertexBuffer:b_cl offset:0 atIndex:1]; [hre setVertexBytes:&u length:sizeof(u) atIndex:2];
                    [hre setFragmentBuffer:b_qd offset:0 atIndex:0]; [hre setFragmentBytes:&pos_scale length:sizeof(pos_scale) atIndex:1];
                    [hre drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:quads.size()];
                    [hre endEncoding];
                }
                if (mode == 1 || mode == 3) {
                    id<MTLTexture> src = (mode == 1) ? t_id_priv : t_id_half;
                    id<MTLComputeCommandEncoder> rce = [cb computeCommandEncoder];
                    [rce setComputePipelineState:p_cmp_red];
                    [rce setTexture:src atIndex:0];
                    [rce setBuffer:b_gstats offset:0 atIndex:0]; [rce setBuffer:b_pick offset:0 atIndex:1];
                    [rce setBytes:&gp length:sizeof(gp) atIndex:2];
                    [rce dispatchThreadgroups:MTLSizeMake((src.width+15)/16, (src.height+15)/16, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
                    [rce endEncoding];
                }
                [cb commit]; [cb waitUntilCompleted];
                if (it >= 4) ms.push_back((cb.GPUEndTime - cb.GPUStartTime) * 1000.0);
            }
            return ms;
        };

        auto t_base = run_mode(0, 0, 0);
        auto t_opt1 = run_mode(1, 280, 272);
        std::vector<GroundStat32> gs_opt1(kMaxSlots);
        std::memcpy(gs_opt1.data(), b_gstats.contents, kMaxSlots * sizeof(GroundStat32));
        auto t_opt2 = run_mode(2, 280, 272);
        std::vector<GroundStat32> gs_opt2(kMaxSlots);
        std::memcpy(gs_opt2.data(), b_gstats.contents, kMaxSlots * sizeof(GroundStat32));
        uint32_t center_pick_opt2 = static_cast<uint32_t *>(b_pick.contents)[0];
        auto t_opt3 = run_mode(3, 280, 272);
        std::vector<GroundStat32> gs_opt3(kMaxSlots);
        std::memcpy(gs_opt3.data(), b_gstats.contents, kMaxSlots * sizeof(GroundStat32));
        uint32_t center_pick_opt3 = static_cast<uint32_t *>(b_pick.contents)[0];

        double m_base = median_of(t_base), m_opt1 = median_of(t_opt1), m_opt2 = median_of(t_opt2), m_opt3 = median_of(t_opt3);
        printf("  baseline_rgb_only        : med=%.3f ms (p95=%.3f ms, %.1f fps) | id_vram=0.0 MB\n", m_base, p95_of(t_base), 1000.0/m_base);
        printf("  opt1_mrt_private_compute : med=%.3f ms (p95=%.3f ms, delta=+%.3f ms) | id_vram=%.2f MB\n",
               m_opt1, p95_of(t_opt1), m_opt1 - m_base, (double)(W*H*4)/(1024.0*1024.0));
        printf("  opt2_mrt_memoryless_tile : med=%.3f ms (p95=%.3f ms, delta=+%.3f ms) | id_vram=0.0 MB!\n",
               m_opt2, p95_of(t_opt2), m_opt2 - m_base);
        printf("  opt3_halfres_id_pass     : med=%.3f ms (p95=%.3f ms, delta=+%.3f ms) | id_vram=%.2f MB\n",
               m_opt3, p95_of(t_opt3), m_opt3 - m_base, (double)((W/2)*(H/2)*4)/(1024.0*1024.0));

        /* Experiment 2: Occlusion-Aware Grounding Parity & Anchor Pick Verification */
        uint32_t ui_base = 2u * kMaxSlotPerDom;
        auto chk_parity = [&](uint32_t gi) { return gs_opt1[gi].visible_px == gs_opt2[gi].visible_px; };
        bool parity_ok = chk_parity(ui_base+2) && chk_parity(ui_base+3) && chk_parity(ui_base+4) && chk_parity(ui_base+5);
        uint32_t anc_raw = gs_opt2[ui_base+4].best_anchor;
        uint32_t anc_x = (anc_raw >> 13u) & 0x1FFFu, anc_y = anc_raw & 0x1FFFu;
        run_mode(2, anc_x, anc_y);
        uint32_t anc_pick = static_cast<uint32_t *>(b_pick.contents)[0];

        uint32_t opt3_anc_raw = gs_opt3[ui_base+4].best_anchor;
        uint32_t o3ax = (opt3_anc_raw >> 13u) & 0x1FFFu, o3ay = opt3_anc_raw & 0x1FFFu;
        run_mode(3, o3ax, o3ay);
        uint32_t opt3_anc_pick = static_cast<uint32_t *>(b_pick.contents)[0];

        uint32_t mesh1_px = gs_opt2[1].visible_px, prx1_px = gs_opt2[kMaxSlotPerDom + 1].visible_px;
        printf("\n=== Experiment 2: Occlusion-Aware UI + 3D Grounding Verification ===\n");
        printf("  opt1 vs opt2 exact pixel parity: %s\n", parity_ok ? "PASS" : "FAIL");
        printf("  btn_ok (ui=2)      : opt2_px=%u | opt3_px=%u\n", gs_opt2[ui_base+2].visible_px, gs_opt3[ui_base+2].visible_px);
        printf("  btn_hidden (ui=3)  : opt2_px=%u | opt3_px=%u (expected 0 under Modal)\n", gs_opt2[ui_base+3].visible_px, gs_opt3[ui_base+3].visible_px);
        printf("  vlist_clipped (35) : opt2_px=%u | opt3_px=%u (expected 0 clipped below Panel)\n", gs_opt2[ui_base+35].visible_px, gs_opt3[ui_base+35].visible_px);
        printf("  opt2 sl_partial    : px=%u center(280,272)->slot=%u | anchor(%u,%u)->slot=%u\n",
               gs_opt2[ui_base+4].visible_px, ground_slot(center_pick_opt2), anc_x, anc_y, ground_slot(anc_pick));
        printf("  opt3 sl_partial    : px=%u center(280,272)->slot=%u | anchor(%u,%u)->slot=%u\n",
               gs_opt3[ui_base+4].visible_px, ground_slot(center_pick_opt3), o3ax, o3ay, ground_slot(opt3_anc_pick));
        printf("  3D reactor_1 px=%u (opt3=%u) | analytic_1 px=%u (opt3=%u)\n",
               mesh1_px, gs_opt3[1].visible_px, prx1_px, gs_opt3[kMaxSlotPerDom+1].visible_px);
        bool exp2_pass = parity_ok && gs_opt2[ui_base+3].visible_px == 0 && gs_opt2[ui_base+35].visible_px == 0 &&
                         gs_opt3[ui_base+3].visible_px == 0 && gs_opt3[ui_base+35].visible_px == 0 &&
                         ground_slot(center_pick_opt2) == 5 && ground_slot(anc_pick) == 4 &&
                         ground_slot(center_pick_opt3) == 5 && ground_slot(opt3_anc_pick) == 4 &&
                         mesh1_px > 0 && prx1_px > 0 && gs_opt3[1].visible_px > 0 && gs_opt3[kMaxSlotPerDom+1].visible_px > 0;
        printf("  Experiment 2 Result: %s\n", exp2_pass ? "PASS" : "FAIL");

        /* Experiment 3: Track V2 Hardware TBDR 16x8 Tiled Render-and-Compare across 3 BloomVtx12 Mesh Templates */
        std::vector<MeshletHdr32> vis_hdrs; std::vector<Vtx12> vis_vtx; std::vector<uint8_t> vis_lidx;
        build_torus_mesh(vis_hdrs, vis_vtx, vis_lidx, 0u, 0u, 0.88f, 0.34f); // tpl 0: first_m=0, mcnt=8
        uint32_t tpl_first_m[3] = {0u, uint32_t(vis_hdrs.size()), 0u};
        uint32_t tpl_vtx_cnt[3] = {8u * kClusterTri * 3u, 36u, 24u};
        build_box_mesh(vis_hdrs, vis_vtx, vis_lidx, 0.92f, 0.45f, 0.65f); // tpl 1: first_m=8, 36 verts
        tpl_first_m[2] = uint32_t(vis_hdrs.size());
        build_octa_mesh(vis_hdrs, vis_vtx, vis_lidx, 1.05f, 1.15f, 0.90f); // tpl 2: first_m=9, 24 verts

        id<MTLBuffer> b_vhdr = [dev newBufferWithBytes:vis_hdrs.data() length:vis_hdrs.size()*sizeof(MeshletHdr32) options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_vvtx = [dev newBufferWithBytes:vis_vtx.data() length:vis_vtx.size()*sizeof(Vtx12) options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_vlid = [dev newBufferWithBytes:vis_lidx.data() length:vis_lidx.size() options:MTLResourceStorageModeShared];

        const uint32_t VW = 256, VH = 256, AW = 1024, AH = 512;
        MTLTextureDescriptor *camd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:VW height:VH mipmapped:NO];
        camd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; camd.storageMode = MTLStorageModeShared;
        id<MTLTexture> t_cam = [dev newTextureWithDescriptor:camd];
        MTLTextureDescriptor *camdd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:VW height:VH mipmapped:NO];
        camdd.usage = MTLTextureUsageRenderTarget; camdd.storageMode = MTLStorageModeMemoryless;
        id<MTLTexture> t_cam_d = [dev newTextureWithDescriptor:camdd];

        MTLTextureDescriptor *atd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRG8Unorm width:AW height:AH mipmapped:NO];
        atd.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; atd.storageMode = MTLStorageModePrivate; atd.allowGPUOptimizedContents = YES;
        id<MTLTexture> t_atlas = [dev newTextureWithDescriptor:atd];
        MTLTextureDescriptor *atdd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float width:AW height:AH mipmapped:NO];
        atdd.usage = MTLTextureUsageRenderTarget; atdd.storageMode = MTLStorageModeMemoryless;
        id<MTLTexture> t_atlas_d = [dev newTextureWithDescriptor:atdd];

        id<MTLBuffer> b_obs64a = [dev newBufferWithLength:4096u * 16u options:MTLResourceStorageModePrivate];
        id<MTLBuffer> b_obs64b = [dev newBufferWithLength:4096u * 16u options:MTLResourceStorageModePrivate];
        id<MTLBuffer> b_sum = [dev newBufferWithLength:16u options:MTLResourceStorageModeShared];
        id<MTLBuffer> b_hyps = [dev newBufferWithLength:512u * sizeof(VisionHyp64) options:MTLResourceStorageModeShared];

        struct GT { uint32_t tpl; float px, py, pz, yaw, pitch, roll, dof0; const char *name; };
        GT gts[3] = {
            {0u,  0.22f, -0.15f, 4.2f,  0.42f, 0.35f, -0.18f, 0.85f, "torus"},
            {1u, -0.18f,  0.14f, 4.4f, -0.35f, 0.45f,  0.25f, 0.00f, "bevel_box"},
            {2u,  0.10f, -0.10f, 4.1f,  0.55f,-0.25f,  0.15f, 0.00f, "octa_prism"}
        };

        printf("\n=== Experiment 3: Track V2 Hardware TBDR Render-and-Compare (16x8 1024x512 Atlas) ===\n");
        int correct = 0; std::vector<double> vis_ms; double sum_terr = 0, sum_rerr = 0, sum_derr = 0, sum_iou = 0;
        for (int g = 0; g < 3; ++g) {
            /* Step 0: Render GT observation with real Metal Meshlets + inject camera noise sigma=6 + distractor */
            auto *hy = static_cast<VisionHyp64 *>(b_hyps.contents);
            hy[0] = {gts[g].px, gts[g].py, gts[g].pz, gts[g].yaw, gts[g].pitch, gts[g].roll, 1.0f, gts[g].dof0,
                     0, 0, 0, 0, gts[g].tpl, 0, 0, 0};
            {
                VisionParams vgt{VW, VH, tpl_first_m[gts[g].tpl], 0u, 2.0f, 0u, 1u, 1u};
                id<MTLCommandBuffer> cbg = [q commandBuffer];
                MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
                rp.colorAttachments[0].texture = t_cam; rp.colorAttachments[0].loadAction = MTLLoadActionClear;
                rp.colorAttachments[0].storeAction = MTLStoreActionStore;
                rp.colorAttachments[0].clearColor = MTLClearColorMake(22.0/255.0, 22.0/255.0, 22.0/255.0, 1.0);
                rp.depthAttachment.texture = t_cam_d; rp.depthAttachment.loadAction = MTLLoadActionClear;
                rp.depthAttachment.storeAction = MTLStoreActionDontCare; rp.depthAttachment.clearDepth = 1.0;
                id<MTLRenderCommandEncoder> re = [cbg renderCommandEncoderWithDescriptor:rp];
                [re setDepthStencilState:ds_3d]; [re setCullMode:MTLCullModeNone];
                [re setRenderPipelineState:p_vis_gt];
                [re setVertexBuffer:b_hyps offset:0 atIndex:0]; [re setVertexBuffer:b_vhdr offset:0 atIndex:1];
                [re setVertexBuffer:b_vvtx offset:0 atIndex:2]; [re setVertexBuffer:b_vlid offset:0 atIndex:3];
                [re setVertexBytes:&vgt length:sizeof(vgt) atIndex:4];
                [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:tpl_vtx_cnt[gts[g].tpl] instanceCount:1];
                [re endEncoding]; [cbg commit]; [cbg waitUntilCompleted];
                std::vector<uint8_t> buf(VW * VH * 4u);
                [t_cam getBytes:buf.data() bytesPerRow:VW * 4u fromRegion:MTLRegionMake2D(0, 0, VW, VH) mipmapLevel:0];
                std::mt19937 nrng(100u + uint32_t(g)); std::normal_distribution<float> gn(0.0f, 6.0f);
                for (size_t i = 0; i < VW * VH; ++i) for (int c = 0; c < 3; ++c)
                    buf[i*4+c] = uint8_t(std::max(0.0f, std::min(255.0f, buf[i*4+c] + gn(nrng))));
                for (int x = 10; x < 42; ++x) { size_t idx = 24u*VW + size_t(x); buf[idx*4]=buf[idx*4+1]=buf[idx*4+2]=190u; }
                [t_cam replaceRegion:MTLRegionMake2D(0, 0, VW, VH) mipmapLevel:0 withBytes:buf.data() bytesPerRow:VW * 4u];
            }

            uint64_t t_gpu_ns = 0;
            /* Round 0: GPU prep 64x64 + 5-step JFA */
            std::memset(b_sum.contents, 0, 16u);
            VisionParams vp_prep{VW, VH, 0u, 0u, 2.0f, 0u, 0u, 0u};
            id<MTLCommandBuffer> cb0 = [q commandBuffer];
            id<MTLComputeCommandEncoder> ce0 = [cb0 computeCommandEncoder];
            [ce0 setComputePipelineState:p_vis_prep];
            [ce0 setTexture:t_cam atIndex:0]; [ce0 setBuffer:b_obs64a offset:0 atIndex:0];
            [ce0 setBuffer:b_sum offset:0 atIndex:1]; [ce0 setBytes:&vp_prep length:sizeof(vp_prep) atIndex:2];
            [ce0 dispatchThreadgroups:MTLSizeMake(8, 8, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
            id<MTLBuffer> jfa_src = b_obs64a, jfa_dst = b_obs64b;
            const int jsteps[5] = {16, 8, 4, 2, 1};
            [ce0 setComputePipelineState:p_vis_jfa];
            for (int js : jsteps) {
                [ce0 setBuffer:jfa_src offset:0 atIndex:0]; [ce0 setBuffer:jfa_dst offset:0 atIndex:1];
                [ce0 setBytes:&js length:4 atIndex:2];
                [ce0 dispatchThreadgroups:MTLSizeMake(8, 8, 1) threadsPerThreadgroup:MTLSizeMake(8, 8, 1)];
                std::swap(jfa_src, jfa_dst);
            }
            [ce0 endEncoding]; [cb0 commit]; [cb0 waitUntilCompleted];
            t_gpu_ns += uint64_t((cb0.GPUEndTime - cb0.GPUStartTime) * 1e9);

            const uint32_t *sum_u = static_cast<const uint32_t *>(b_sum.contents);
            float cx_ndc = (sum_u[2] > 4u) ? ((float(sum_u[0]) / float(sum_u[2]) + 0.5f) / 64.0f * 2.0f - 1.0f) : 0.0f;
            float cy_ndc = (sum_u[2] > 4u) ? (1.0f - (float(sum_u[1]) / float(sum_u[2]) + 0.5f) / 64.0f * 2.0f) : 0.0f;

            /* Notice on a symmetric torus ring, azimuthal yaw has pi/2 near-symmetry except subtle dof0 wave,
               so stratify (yaw, pitch) across kPerTplC=128 tiles cleanly */
            std::mt19937 rng(42u + uint32_t(g));
            const uint32_t kPerTplC = 128u, kCTot = 3u * kPerTplC;
            for (uint32_t h = 0; h < kCTot; ++h) {
                uint32_t tid = h / kPerTplC, lh = h % kPerTplC;
                float syaw = -0.80f + (float(lh % 8u) / 7.0f) * 1.60f;
                float spitch = -0.60f + (float((lh / 8u) % 4u) / 3.0f) * 1.20f;
                std::uniform_real_distribution<float> dz(3.85f, 4.65f), dj(-0.06f, 0.06f), dja(-0.14f, 0.14f), dr(-0.35f, 0.35f), dd(0.0f, 1.5f);
                float z_e = dz(rng);
                hy[h] = {(cx_ndc * z_e)/2.0f + dj(rng), (cy_ndc * z_e)/2.0f + dj(rng), z_e,
                         syaw + dja(rng), spitch + dja(rng), dr(rng),
                         1.0f, tid == 0 ? dd(rng) : 0.0f, 0, 0, 0, 0, tid, 0, 0, 0};
            }
            id<MTLCommandBuffer> cb1 = [q commandBuffer];
            for (uint32_t t = 0u; t < 3u; ++t) {
                VisionParams vpa{VW, VH, tpl_first_m[t], t * kPerTplC, 2.0f, 0u, kPerTplC, 0u};
                MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
                rp.colorAttachments[0].texture = t_atlas; rp.colorAttachments[0].loadAction = MTLLoadActionClear;
                rp.colorAttachments[0].storeAction = MTLStoreActionStore;
                rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
                rp.depthAttachment.texture = t_atlas_d; rp.depthAttachment.loadAction = MTLLoadActionClear;
                rp.depthAttachment.storeAction = MTLStoreActionDontCare; rp.depthAttachment.clearDepth = 1.0;
                id<MTLRenderCommandEncoder> re = [cb1 renderCommandEncoderWithDescriptor:rp];
                [re setDepthStencilState:ds_3d]; [re setCullMode:MTLCullModeNone]; [re setRenderPipelineState:p_vis_atlas];
                [re setVertexBuffer:b_hyps offset:0 atIndex:0]; [re setVertexBuffer:b_vhdr offset:0 atIndex:1];
                [re setVertexBuffer:b_vvtx offset:0 atIndex:2]; [re setVertexBuffer:b_vlid offset:0 atIndex:3];
                [re setVertexBytes:&vpa length:sizeof(vpa) atIndex:4];
                [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:tpl_vtx_cnt[t] instanceCount:kPerTplC];
                [re endEncoding];
                id<MTLComputeCommandEncoder> ce = [cb1 computeCommandEncoder];
                [ce setComputePipelineState:p_vis_score]; [ce setTexture:t_atlas atIndex:0];
                [ce setBuffer:jfa_src offset:0 atIndex:0]; [ce setBuffer:b_hyps offset:0 atIndex:1];
                [ce setBytes:&vpa length:sizeof(vpa) atIndex:2];
                [ce dispatchThreadgroups:MTLSizeMake(kPerTplC, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
                [ce endEncoding];
            }
            [cb1 commit]; [cb1 waitUntilCompleted];
            t_gpu_ns += uint64_t((cb1.GPUEndTime - cb1.GPUStartTime) * 1e9);

            /* Rounds 2..4: CEM Refinement keeping top-4 elites per template (42/tpl -> 126 <= 128 tiles per 1 Render+Score pass!) */
            std::vector<VisionHyp64> bank(hy, hy + kCTot);
            for (int r = 0; r < 3; ++r) {
                std::vector<VisionHyp64> elites;
                for (uint32_t t = 0u; t < 3u; ++t) {
                    std::vector<VisionHyp64> sub;
                    for (const auto &c : bank) if (c.tpl_id == t) sub.push_back(c);
                    std::sort(sub.begin(), sub.end(), [](const VisionHyp64 &a, const VisionHyp64 &b){ return a.score_out < b.score_out; });
                    for (size_t k = 0; k < std::min<size_t>(4u, sub.size()); ++k) elites.push_back(sub[k]);
                }
                float spread = (r == 0) ? 0.35f : ((r == 1) ? 0.12f : 0.04f);
                const uint32_t kPerT = 42u, kRefTot = 3u * kPerT;
                for (uint32_t i = 0; i < kRefTot; ++i) {
                    uint32_t t = i / kPerT;
                    VisionHyp64 ch = elites[t * 4u + (i % 4u)];
                    if ((i % kPerT) >= 4u) { // always keep top-4 pure unperturbed!
                        std::normal_distribution<float> nd(0.0f, 1.0f);
                        ch.px += nd(rng) * 0.15f * spread; ch.py += nd(rng) * 0.15f * spread; ch.pz += nd(rng) * 0.30f * spread;
                        ch.yaw += nd(rng) * 0.50f * spread; ch.pitch += nd(rng) * 0.50f * spread; ch.roll += nd(rng) * 0.40f * spread;
                        if (ch.tpl_id == 0u) ch.dof0 = std::max(0.0f, std::min(1.5f, ch.dof0 + nd(rng) * 0.50f * spread));
                    }
                    hy[i] = ch;
                }
                id<MTLCommandBuffer> cbr = [q commandBuffer];
                MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
                rp.colorAttachments[0].texture = t_atlas; rp.colorAttachments[0].loadAction = MTLLoadActionClear;
                rp.colorAttachments[0].storeAction = MTLStoreActionStore;
                rp.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
                rp.depthAttachment.texture = t_atlas_d; rp.depthAttachment.loadAction = MTLLoadActionClear;
                rp.depthAttachment.storeAction = MTLStoreActionDontCare; rp.depthAttachment.clearDepth = 1.0;
                id<MTLRenderCommandEncoder> re = [cbr renderCommandEncoderWithDescriptor:rp];
                [re setDepthStencilState:ds_3d]; [re setCullMode:MTLCullModeNone]; [re setRenderPipelineState:p_vis_atlas];
                [re setVertexBuffer:b_hyps offset:0 atIndex:0]; [re setVertexBuffer:b_vhdr offset:0 atIndex:1];
                [re setVertexBuffer:b_vvtx offset:0 atIndex:2]; [re setVertexBuffer:b_vlid offset:0 atIndex:3];
                for (uint32_t t = 0u; t < 3u; ++t) {
                    VisionParams vpa{VW, VH, tpl_first_m[t], t * kPerT, 2.0f, t * kPerT, kPerT, 0u};
                    [re setVertexBytes:&vpa length:sizeof(vpa) atIndex:4];
                    [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:tpl_vtx_cnt[t] instanceCount:kPerT];
                }
                [re endEncoding];
                VisionParams vps{VW, VH, 0u, 0u, 2.0f, 0u, kRefTot, 0u};
                id<MTLComputeCommandEncoder> ce = [cbr computeCommandEncoder];
                [ce setComputePipelineState:p_vis_score]; [ce setTexture:t_atlas atIndex:0];
                [ce setBuffer:jfa_src offset:0 atIndex:0]; [ce setBuffer:b_hyps offset:0 atIndex:1];
                [ce setBytes:&vps length:sizeof(vps) atIndex:2];
                [ce dispatchThreadgroups:MTLSizeMake(kRefTot, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
                [ce endEncoding];
                [cbr commit]; [cbr waitUntilCompleted];
                t_gpu_ns += uint64_t((cbr.GPUEndTime - cbr.GPUStartTime) * 1e9);
                bank.assign(hy, hy + kRefTot);
            }

            float best_t[3] = {999, 999, 999};
            for (const auto &c : bank) best_t[c.tpl_id] = std::min(best_t[c.tpl_id], c.score_out);
            std::sort(bank.begin(), bank.end(), [](const VisionHyp64 &a, const VisionHyp64 &b){ return a.score_out < b.score_out; });

            /* Final Winner Fine Polish (64 tiles focused purely on winning template's pose + DOF) */
            uint32_t win_t = bank[0].tpl_id;
            const uint32_t kPol = 64u;
            hy[0] = bank[0];
            for (uint32_t i = 1; i < kPol; ++i) {
                VisionHyp64 ch = bank[0];
                std::normal_distribution<float> nd(0.0f, 1.0f);
                ch.px += nd(rng)*0.02f; ch.py += nd(rng)*0.02f; ch.pz += nd(rng)*0.04f;
                ch.yaw += nd(rng)*0.06f; ch.pitch += nd(rng)*0.06f; ch.roll += nd(rng)*0.06f;
                if (win_t == 0u) ch.dof0 = float(i - 1u) * (1.5f / float(kPol - 2u));
                hy[i] = ch;
            }
            {
                id<MTLCommandBuffer> cbf = [q commandBuffer];
                MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
                rp.colorAttachments[0].texture = t_atlas; rp.colorAttachments[0].loadAction = MTLLoadActionClear;
                rp.colorAttachments[0].storeAction = MTLStoreActionStore; rp.colorAttachments[0].clearColor = MTLClearColorMake(0,0,0,0);
                rp.depthAttachment.texture = t_atlas_d; rp.depthAttachment.loadAction = MTLLoadActionClear;
                rp.depthAttachment.storeAction = MTLStoreActionDontCare; rp.depthAttachment.clearDepth = 1.0;
                id<MTLRenderCommandEncoder> re = [cbf renderCommandEncoderWithDescriptor:rp];
                [re setDepthStencilState:ds_3d]; [re setCullMode:MTLCullModeNone]; [re setRenderPipelineState:p_vis_atlas];
                [re setVertexBuffer:b_hyps offset:0 atIndex:0]; [re setVertexBuffer:b_vhdr offset:0 atIndex:1];
                [re setVertexBuffer:b_vvtx offset:0 atIndex:2]; [re setVertexBuffer:b_vlid offset:0 atIndex:3];
                VisionParams vpa{VW, VH, tpl_first_m[win_t], 0u, 2.0f, 0u, kPol, 0u};
                [re setVertexBytes:&vpa length:sizeof(vpa) atIndex:4];
                [re drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:tpl_vtx_cnt[win_t] instanceCount:kPol];
                [re endEncoding];
                id<MTLComputeCommandEncoder> ce = [cbf computeCommandEncoder];
                [ce setComputePipelineState:p_vis_score]; [ce setTexture:t_atlas atIndex:0];
                [ce setBuffer:jfa_src offset:0 atIndex:0]; [ce setBuffer:b_hyps offset:0 atIndex:1];
                [ce setBytes:&vpa length:sizeof(vpa) atIndex:2];
                [ce dispatchThreadgroups:MTLSizeMake(kPol, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
                [ce endEncoding]; [cbf commit]; [cbf waitUntilCompleted];
                t_gpu_ns += uint64_t((cbf.GPUEndTime - cbf.GPUStartTime) * 1e9);
                std::sort(hy, hy + kPol, [](const VisionHyp64 &a, const VisionHyp64 &b){ return a.score_out < b.score_out; });
            }
            const auto &best = hy[0];
            double tot_ms = double(t_gpu_ns) * 1e-6;
            vis_ms.push_back(tot_ms);
            float terr = sqrtf((best.px-gts[g].px)*(best.px-gts[g].px)+(best.py-gts[g].py)*(best.py-gts[g].py)+(best.pz-gts[g].pz)*(best.pz-gts[g].pz));
            float rerr_deg = sqrtf((best.yaw-gts[g].yaw)*(best.yaw-gts[g].yaw)+(best.pitch-gts[g].pitch)*(best.pitch-gts[g].pitch)+(best.roll-gts[g].roll)*(best.roll-gts[g].roll)) * (180.0f / 3.1415926f);
            float derr = fabsf(best.dof0 - gts[g].dof0);
            if (best.tpl_id == gts[g].tpl) correct++;
            sum_terr += terr; sum_rerr += rerr_deg; sum_derr += derr; sum_iou += best.iou_out;
            printf("  GT %-10s (t=[%.2f,%.2f,%.2f]) -> Pred tpl=%u | score=%.2f iou=%.3f dt=%.2f lum=%.3f | t_err=%.3f m | rot_err=%.2f deg | dof_err=%.3f | GPU=%.3f ms\n",
                   gts[g].name, best_t[0], best_t[1], best_t[2], best.tpl_id, best.score_out, best.iou_out,
                   best.edge_err, best.lum_err, terr, rerr_deg, derr, tot_ms);
        }

        double m_vis = median_of(vis_ms);
        if (json_out) {
            FILE *fp = fopen(json_out, "w");
            if (fp) {
                fprintf(fp,
                        "{\n"
                        "  \"gpu_name\": \"%s\",\n"
                        "  \"exp1_4k_ms\": {\n"
                        "    \"baseline_rgb_only\": %.3f,\n"
                        "    \"opt1_mrt_private_compute\": %.3f,\n"
                        "    \"opt2_mrt_memoryless_tile\": %.3f,\n"
                        "    \"opt3_halfres_id_pass\": %.3f,\n"
                        "    \"tile_max_tg\": %lu\n"
                        "  },\n"
                        "  \"exp2_grounding\": {\n"
                        "    \"parity_pass\": %s,\n"
                        "    \"btn_ok_px\": %u,\n"
                        "    \"btn_hidden_px\": %u,\n"
                        "    \"vlist_clipped_px\": %u,\n"
                        "    \"sl_partial_px\": %u,\n"
                        "    \"opt3_sl_partial_px\": %u,\n"
                        "    \"center_pick_slot\": %u,\n"
                        "    \"anchor_x\": %u,\n"
                        "    \"anchor_y\": %u,\n"
                        "    \"anchor_pick_slot\": %u,\n"
                        "    \"opt3_anchor_pick_slot\": %u,\n"
                        "    \"mesh1_px\": %u,\n"
                        "    \"analytic1_px\": %u,\n"
                        "    \"pass\": %s\n"
                        "  },\n"
                        "  \"exp3_vision_align\": {\n"
                        "    \"correct_templates\": %d,\n"
                        "    \"total_templates\": 3,\n"
                        "    \"mean_iou\": %.3f,\n"
                        "    \"median_gpu_ms\": %.3f,\n"
                        "    \"mean_trans_err_m\": %.4f,\n"
                        "    \"mean_rot_err_deg\": %.2f,\n"
                        "    \"mean_dof_err\": %.4f\n"
                        "  }\n"
                        "}\n",
                        dev.name.UTF8String, m_base, m_opt1, m_opt2, m_opt3, (unsigned long)p_tile.maxTotalThreadsPerThreadgroup,
                        parity_ok ? "true" : "false", gs_opt2[ui_base+2].visible_px, gs_opt2[ui_base+3].visible_px,
                        gs_opt2[ui_base+35].visible_px, gs_opt2[ui_base+4].visible_px, gs_opt3[ui_base+4].visible_px,
                        ground_slot(center_pick_opt2), anc_x, anc_y, ground_slot(anc_pick), ground_slot(opt3_anc_pick),
                        mesh1_px, prx1_px, exp2_pass ? "true" : "false",
                        correct, sum_iou / 3.0, m_vis, sum_terr / 3.0, sum_rerr / 3.0, sum_derr / 3.0);
                fclose(fp);
            }
        }
        return (exp2_pass && correct == 3) ? 0 : 1;
    }
}
