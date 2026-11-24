// object_ssot_bench.mm — Empirical Apple M1 Metal Benchmark:
// Industry GLB/Skinning (64B/vtx) vs BLOOM vNext Constraint-Native Object SSOT
// Track A: BloomVtx12 (12B/vtx) + BloomMeshletHeader32 (32B) + BloomObjectState48 (48B)
// Track B: Tight-Proxy Analytic Intersection (BloomAnalyticPrim32, 32B)
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
#include <sstream>
#include <string>
#include <time.h>
#include <vector>

namespace {

static uint64_t now_ns() {
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
}

static inline uint16_t f32_to_f16_hw(float v) {
    _Float16 h = static_cast<_Float16>(v);
    uint16_t bits = 0;
    std::memcpy(&bits, &h, sizeof(bits));
    return bits;
}

static inline uint16_t pack_q14_2(float px) {
    float v = px * 4.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 65535.0f) v = 65535.0f;
    return static_cast<uint16_t>(v + 0.5f);
}

static inline uint8_t pack_q6_2(float px) {
    float v = px * 4.0f;
    if (v < 0.0f) v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return static_cast<uint8_t>(v + 0.5f);
}

static inline uint16_t pack_pos_unorm16(float x, float lo, float span) {
    float t = (x - lo) / span;
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return static_cast<uint16_t>(t * 65535.0f + 0.5f);
}

static inline float unpack_pos_unorm16(uint16_t q, float lo, float span) {
    return lo + (static_cast<float>(q) / 65535.0f) * span;
}

static inline void oct_encode(float nx, float ny, float nz, int8_t out[2]) {
    float inv_l1 = 1.0f / std::max(std::fabs(nx) + std::fabs(ny) + std::fabs(nz), 1e-8f);
    float ox = nx * inv_l1;
    float oy = ny * inv_l1;
    if (nz < 0.0f) {
        float tx = (1.0f - std::fabs(oy)) * (ox >= 0.0f ? 1.0f : -1.0f);
        float ty = (1.0f - std::fabs(ox)) * (oy >= 0.0f ? 1.0f : -1.0f);
        ox = tx;
        oy = ty;
    }
    auto q = [](float v) -> int8_t {
        float s = std::max(-1.0f, std::min(1.0f, v)) * 127.0f;
        return static_cast<int8_t>(s >= 0.0f ? (s + 0.5f) : (s - 0.5f));
    };
    out[0] = q(ox);
    out[1] = q(oy);
}

static inline void oct_decode(const int8_t in[2], float &nx, float &ny, float &nz) {
    float ox = static_cast<float>(in[0]) / 127.0f;
    float oy = static_cast<float>(in[1]) / 127.0f;
    nz = 1.0f - std::fabs(ox) - std::fabs(oy);
    if (nz < 0.0f) {
        float tx = (1.0f - std::fabs(oy)) * (ox >= 0.0f ? 1.0f : -1.0f);
        float ty = (1.0f - std::fabs(ox)) * (oy >= 0.0f ? 1.0f : -1.0f);
        ox = tx;
        oy = ty;
    }
    float len = std::sqrt(ox * ox + oy * oy + nz * nz);
    nx = ox / len;
    ny = oy / len;
    nz = nz / len;
}

struct Stats {
    double min_val = 0.0;
    double median_val = 0.0;
    double p95_val = 0.0;
    double mean_val = 0.0;
};

static Stats compute_stats(std::vector<double> v) {
    Stats s{};
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.min_val = v.front();
    size_t n = v.size();
    s.median_val = (n & 1u) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
    size_t idx95 = static_cast<size_t>(std::ceil(0.95 * static_cast<double>(n))) - 1;
    if (idx95 >= n) idx95 = n - 1;
    s.p95_val = v[idx95];
    double sum = 0.0;
    for (double x : v) sum += x;
    s.mean_val = sum / static_cast<double>(n);
    return s;
}

// ============================================================================
// Industry vs BLOOM vNext Object-SSOT GPU Structures (Zero FLOW)
// ============================================================================
struct alignas(4) IndustryVtx64 {
    float pos[3];             // 12 B
    float norm[3];            // 12 B
    float tan[4];             // 16 B
    float uv[2];              //  8 B
    uint16_t joints[4];       //  8 B (JOINTS_0)
    uint16_t weights_u16[4];  //  8 B (WEIGHTS_0)
};
static_assert(sizeof(IndustryVtx64) == 64, "IndustryVtx64 must be 64 B");

struct alignas(16) IndustryRig512 {
    float mats[8][16]; // 8 x 4x4 float matrices = 512 B/obj
};
static_assert(sizeof(IndustryRig512) == 512, "IndustryRig512 must be 512 B");

struct alignas(4) BloomVtx12 {
    uint16_t pos_unorm16[3];  //  6 B: quantized inside local [-1.5, +1.5] box (~0.046 mm @ 3m)
    int8_t   oct_norm[2];     //  2 B: octahedral unit normal
    uint8_t  uv_u8[2];        //  2 B
    uint8_t  joint_id;        //  1 B: articulated link id
    uint8_t  mat_id;          //  1 B: canonical material id
};
static_assert(sizeof(BloomVtx12) == 12, "BloomVtx12 must be 12 B");

struct alignas(4) BloomMeshletHeader32 {
    uint32_t vtx_offset;      //  4 B
    uint32_t idx_offset;      //  4 B
    uint16_t vtx_cnt;         //  2 B (<= 64)
    uint16_t tri_cnt;         //  2 B (<= 96)
    uint16_t obj_id;          //  2 B
    uint16_t flags;           //  2 B
    uint16_t bsphere_f16[4];  //  8 B: local center xyz + radius r
    int8_t   cone_axis[3];    //  3 B: mean normal direction
    int8_t   cone_cutoff;     //  1 B: sin(half_angle) quantized
    uint32_t aabb_scale_tag;  //  4 B
};
static_assert(sizeof(BloomMeshletHeader32) == 32, "BloomMeshletHeader32 must be 32 B");

struct alignas(4) BloomObjectState48 {
    float    pos[3];            // 12 B: world translation
    uint16_t quat_f16[4];       //  8 B: orientation
    uint16_t scale_f16[3];      //  6 B: scale
    uint16_t dof_f16[3];        //  6 B: kinematic hinge/slider/twist DOFs
    uint32_t rgba8_mat;         //  4 B: runtime tint
    uint16_t clip_plane_f16[4]; //  8 B: live X-Ray cutaway (nx, ny, nz, d)
    uint32_t flags;             //  4 B: bit0=cutaway_active
};
static_assert(sizeof(BloomObjectState48) == 48, "BloomObjectState48 must be 48 B");

struct alignas(4) BloomAnalyticPrim32 {
    float    center[3];         // 12 B
    float    radius;            //  4 B
    uint16_t axis_len_f16[4];   //  8 B: capsule axis xyz + half_len
    uint32_t rgba8;             //  4 B
    uint32_t prim_kind_flags;   //  4 B
};
static_assert(sizeof(BloomAnalyticPrim32) == 32, "BloomAnalyticPrim32 must be 32 B");

// 2D SVG-SSOT HUD structs reused in Exp 6 single-CB composite
struct alignas(4) BloomQuadHot {
    uint16_t bounds_q14_2[4];
    uint32_t rgba8_fill;
    uint32_t rgba8_border;
    uint8_t  radii_q6_2[4];
    uint8_t  borders_q6_2[4];
    uint16_t clip_id;
    uint16_t style_id;
    uint32_t z_flags;
};
static_assert(sizeof(BloomQuadHot) == 32, "BloomQuadHot must be 32 B");

struct alignas(4) BloomShadowHot {
    uint16_t bounds_q14_2[4];
    uint16_t shape_params[4];
    uint16_t blur_r1_exp_sc[4];
    uint32_t rgba8;
    uint16_t clip_id;
    uint16_t z_flags;
};
static_assert(sizeof(BloomShadowHot) == 32, "BloomShadowHot must be 32 B");

struct alignas(4) BloomGlyphInst {
    uint16_t dst_q14_2[4];
    uint16_t uv_unorm16[4];
    uint32_t rgba8;
    uint16_t clip_id;
    uint16_t z_flags;
};
static_assert(sizeof(BloomGlyphInst) == 24, "BloomGlyphInst must be 24 B");

struct alignas(16) BloomClipRect {
    float x, y, w, h;
};
static_assert(sizeof(BloomClipRect) == 16, "BloomClipRect must be 16 B");

struct MeshletDrawParams {
    float vp_w, vp_h;
    float cam_x, cam_y, cam_z;
    float focal;
    float frustum_half_tan;
    uint32_t total_meshlets;
    uint32_t mode_flags;
    uint32_t pad0;
};

struct RaymarchTileParams {
    uint32_t prim_count;
    uint32_t width;
    uint32_t height;
    float    focal;
    float    cam_x, cam_y, cam_z;
    uint32_t pad0;
};

// ============================================================================
// Procedural Parametric Meshlet Grid Generator (64 verts = 8x8 -> 98? 7x7 quads = 98 -> cap 64v / 96 tris!)
// Actually: 8 cols x 7 rows = 56 verts -> 7 x 6 quads x 2 = 84 tris,
// OR 8 cols x 8 rows = 64 verts -> 7 x 7 quads x 2 = 98 tris (drop last 2 -> exact 64 verts & 96 tris!)
// ============================================================================
static constexpr uint32_t CLUSTER_VTX = 64u;
static constexpr uint32_t CLUSTER_TRI = 96u;
static constexpr float LOCAL_BOX_LO = -1.5f;
static constexpr float LOCAL_BOX_SPAN = 3.0f;

static void build_torus_patch_cluster(uint32_t patch_idx, uint32_t total_patches,
                                      uint16_t obj_id,
                                      BloomVtx12 *out_bv, IndustryVtx64 *out_iv,
                                      uint8_t *out_local_idx, uint32_t *out_u32_idx,
                                      BloomMeshletHeader32 &out_hdr,
                                      uint32_t vtx_base, uint32_t idx_base,
                                      float *out_max_pos_err = nullptr,
                                      double *out_sum_norm_deg = nullptr,
                                      double *out_max_norm_deg = nullptr) {
    float R = 0.85f, r = 0.35f;
    float u0 = (static_cast<float>(patch_idx % 4u) / 4.0f) * 2.0f * 3.14159265f;
    float u1 = (static_cast<float>((patch_idx % 4u) + 1u) / 4.0f) * 2.0f * 3.14159265f;
    uint32_t rings = std::max(1u, total_patches / 4u);
    float v0 = (static_cast<float>((patch_idx / 4u) % rings) / static_cast<float>(rings)) * 2.0f * 3.14159265f;
    float v1 = (static_cast<float>(((patch_idx / 4u) % rings) + 1u) / static_cast<float>(rings)) * 2.0f * 3.14159265f;

    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    float nsum_x = 0.0f, nsum_y = 0.0f, nsum_z = 0.0f;
    float px_cache[CLUSTER_VTX][3]{};
    float nm_cache[CLUSTER_VTX][3]{};

    for (uint32_t iy = 0; iy < 8u; ++iy) {
        float tv = static_cast<float>(iy) / 7.0f;
        float phi = v0 + tv * (v1 - v0);
        float cp = std::cos(phi), sp = std::sin(phi);
        for (uint32_t ix = 0; ix < 8u; ++ix) {
            float tu = static_cast<float>(ix) / 7.0f;
            float theta = u0 + tu * (u1 - u0);
            float ct = std::cos(theta), st = std::sin(theta);

            float px = (R + r * cp) * ct;
            float py = r * sp;
            float pz = (R + r * cp) * st;
            float nx = cp * ct, ny = sp, nz = cp * st;

            uint32_t vi = iy * 8u + ix;
            px_cache[vi][0] = px; px_cache[vi][1] = py; px_cache[vi][2] = pz;
            nm_cache[vi][0] = nx; nm_cache[vi][1] = ny; nm_cache[vi][2] = nz;
            cx += px; cy += py; cz += pz;
            nsum_x += nx; nsum_y += ny; nsum_z += nz;

            if (out_bv) {
                BloomVtx12 &b = out_bv[vtx_base + vi];
                b.pos_unorm16[0] = pack_pos_unorm16(px, LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                b.pos_unorm16[1] = pack_pos_unorm16(py, LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                b.pos_unorm16[2] = pack_pos_unorm16(pz, LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                oct_encode(nx, ny, nz, b.oct_norm);
                b.uv_u8[0] = static_cast<uint8_t>(tu * 255.0f);
                b.uv_u8[1] = static_cast<uint8_t>(tv * 255.0f);
                b.joint_id = static_cast<uint8_t>(patch_idx & 3u);
                b.mat_id = 0u;

                if (out_max_pos_err) {
                    float rx = unpack_pos_unorm16(b.pos_unorm16[0], LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                    float ry = unpack_pos_unorm16(b.pos_unorm16[1], LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                    float rz = unpack_pos_unorm16(b.pos_unorm16[2], LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                    float err = std::sqrt((rx - px) * (rx - px) + (ry - py) * (ry - py) + (rz - pz) * (rz - pz));
                    if (err > *out_max_pos_err) *out_max_pos_err = err;
                }
                if (out_sum_norm_deg && out_max_norm_deg) {
                    float dnx, dny, dnz;
                    oct_decode(b.oct_norm, dnx, dny, dnz);
                    float dot = std::max(-1.0f, std::min(1.0f, dnx * nx + dny * ny + dnz * nz));
                    double deg = std::acos(dot) * (180.0 / 3.14159265358979);
                    *out_sum_norm_deg += deg;
                    if (deg > *out_max_norm_deg) *out_max_norm_deg = deg;
                }
            }
            if (out_iv) {
                IndustryVtx64 &iv = out_iv[vtx_base + vi];
                iv.pos[0] = px; iv.pos[1] = py; iv.pos[2] = pz;
                iv.norm[0] = nx; iv.norm[1] = ny; iv.norm[2] = nz;
                iv.tan[0] = -st; iv.tan[1] = 0.0f; iv.tan[2] = ct; iv.tan[3] = 1.0f;
                iv.uv[0] = tu; iv.uv[1] = tv;
                iv.joints[0] = 0; iv.joints[1] = 1; iv.joints[2] = 2; iv.joints[3] = 3;
                iv.weights_u16[0] = 32768; iv.weights_u16[1] = 16384; iv.weights_u16[2] = 8192; iv.weights_u16[3] = 8191;
            }
        }
    }

    cx /= static_cast<float>(CLUSTER_VTX);
    cy /= static_cast<float>(CLUSTER_VTX);
    cz /= static_cast<float>(CLUSTER_VTX);
    float max_r = 0.0f;
    for (uint32_t i = 0; i < CLUSTER_VTX; ++i) {
        float dx = px_cache[i][0] - cx, dy = px_cache[i][1] - cy, dz = px_cache[i][2] - cz;
        float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d > max_r) max_r = d;
    }

    float nlen = std::sqrt(nsum_x * nsum_x + nsum_y * nsum_y + nsum_z * nsum_z);
    if (nlen > 1e-6f) { nsum_x /= nlen; nsum_y /= nlen; nsum_z /= nlen; }
    else { nsum_x = 0.0f; nsum_y = 1.0f; nsum_z = 0.0f; }

    float min_dot = 1.0f;
    for (uint32_t i = 0; i < CLUSTER_VTX; ++i) {
        float d = nm_cache[i][0] * nsum_x + nm_cache[i][1] * nsum_y + nm_cache[i][2] * nsum_z;
        if (d < min_dot) min_dot = d;
    }
    // cone cutoff: sin(cone_half_angle) >= sqrt(1 - min_dot^2)
    float cone_sin = (min_dot > 0.0f) ? std::sqrt(std::max(0.0f, 1.0f - min_dot * min_dot)) : 1.0f;

    uint32_t t = 0;
    for (uint32_t iy = 0; iy < 7u && t < CLUSTER_TRI; ++iy) {
        for (uint32_t ix = 0; ix < 7u && t < CLUSTER_TRI; ++ix) {
            uint32_t i0 = iy * 8u + ix;
            uint32_t i1 = i0 + 1u;
            uint32_t i2 = i0 + 8u;
            uint32_t i3 = i2 + 1u;
            if (out_local_idx) {
                out_local_idx[t * 3u + 0u] = static_cast<uint8_t>(i0);
                out_local_idx[t * 3u + 1u] = static_cast<uint8_t>(i2);
                out_local_idx[t * 3u + 2u] = static_cast<uint8_t>(i1);
            }
            if (out_u32_idx) {
                out_u32_idx[idx_base + t * 3u + 0u] = vtx_base + i0;
                out_u32_idx[idx_base + t * 3u + 1u] = vtx_base + i2;
                out_u32_idx[idx_base + t * 3u + 2u] = vtx_base + i1;
            }
            ++t;
            if (t < CLUSTER_TRI) {
                if (out_local_idx) {
                    out_local_idx[t * 3u + 0u] = static_cast<uint8_t>(i1);
                    out_local_idx[t * 3u + 1u] = static_cast<uint8_t>(i2);
                    out_local_idx[t * 3u + 2u] = static_cast<uint8_t>(i3);
                }
                if (out_u32_idx) {
                    out_u32_idx[idx_base + t * 3u + 0u] = vtx_base + i1;
                    out_u32_idx[idx_base + t * 3u + 1u] = vtx_base + i2;
                    out_u32_idx[idx_base + t * 3u + 2u] = vtx_base + i3;
                }
                ++t;
            }
        }
    }

    out_hdr.vtx_offset = vtx_base;
    out_hdr.idx_offset = idx_base;
    out_hdr.vtx_cnt = static_cast<uint16_t>(CLUSTER_VTX);
    out_hdr.tri_cnt = static_cast<uint16_t>(CLUSTER_TRI);
    out_hdr.obj_id = obj_id;
    out_hdr.flags = 0u;
    out_hdr.bsphere_f16[0] = f32_to_f16_hw(cx);
    out_hdr.bsphere_f16[1] = f32_to_f16_hw(cy);
    out_hdr.bsphere_f16[2] = f32_to_f16_hw(cz);
    out_hdr.bsphere_f16[3] = f32_to_f16_hw(max_r);
    out_hdr.cone_axis[0] = static_cast<int8_t>(nsum_x * 127.0f);
    out_hdr.cone_axis[1] = static_cast<int8_t>(nsum_y * 127.0f);
    out_hdr.cone_axis[2] = static_cast<int8_t>(nsum_z * 127.0f);
    out_hdr.cone_cutoff = static_cast<int8_t>(std::min(1.0f, cone_sin + 0.02f) * 127.0f);
    out_hdr.aabb_scale_tag = 0u;
}

// ============================================================================
// Embedded Metal 3 MSL Shaders (Compute, Vertex/Fragment & Object/Mesh)
// ============================================================================
static const char *kObjBenchMetal = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct IndustryVtx64 {
    packed_float3 pos;
    packed_float3 norm;
    packed_float4 tan;
    packed_float2 uv;
    ushort joints[4];
    ushort weights_u16[4];
};

struct IndustryRig512 {
    float4x4 bones[8];
};

struct BloomVtx12 {
    ushort px, py, pz;
    char oct_x, oct_y;
    uchar u, v;
    uchar joint_id, mat_id;
};

struct BloomMeshletHeader32 {
    uint vtx_offset;
    uint idx_offset;
    ushort vtx_cnt;
    ushort tri_cnt;
    ushort obj_id;
    ushort flags;
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
    uint rgba8;
    uint prim_kind_flags;
};

struct BloomQuadHot {
    ushort b0, b1, b2, b3;
    uint rgba8_fill;
    uint rgba8_border;
    uchar r0, r1, r2, r3;
    uchar bw0, bw1, bw2, bw3;
    ushort clip_id;
    ushort style_id;
    uint z_flags;
};

struct BloomShadowHot {
    ushort b0, b1, b2, b3;
    half sp0, sp1, sp2, sp3;
    half br0, br1, br2, br3;
    uint rgba8;
    ushort clip_id;
    ushort z_flags;
};

struct BloomGlyphInst {
    ushort d0, d1, d2, d3;
    ushort u0, u1, u2, u3;
    uint rgba8;
    ushort clip_id;
    ushort z_flags;
};

struct BloomClipRect {
    float4 rect;
};

static_assert(sizeof(IndustryVtx64) == 64, "MSL IndustryVtx64 mismatch");
static_assert(sizeof(IndustryRig512) == 512, "MSL IndustryRig512 mismatch");
static_assert(sizeof(BloomVtx12) == 12, "MSL BloomVtx12 mismatch");
static_assert(sizeof(BloomMeshletHeader32) == 32, "MSL BloomMeshletHeader32 mismatch");
static_assert(sizeof(BloomObjectState48) == 48, "MSL BloomObjectState48 mismatch");
static_assert(sizeof(BloomAnalyticPrim32) == 32, "MSL BloomAnalyticPrim32 mismatch");
static_assert(sizeof(BloomQuadHot) == 32, "MSL BloomQuadHot mismatch");
static_assert(sizeof(BloomShadowHot) == 32, "MSL BloomShadowHot mismatch");
static_assert(sizeof(BloomGlyphInst) == 24, "MSL BloomGlyphInst mismatch");

struct MeshletDrawParams {
    float vp_w, vp_h;
    float cam_x, cam_y, cam_z;
    float focal;
    float frustum_half_tan;
    uint total_meshlets;
    uint mode_flags;
    uint pad0;
};

struct RaymarchTileParams {
    uint prim_count;
    uint width;
    uint height;
    float focal;
    float cam_x, cam_y, cam_z;
    uint pad0;
};

struct DrawIndexedIndirectArgs {
    uint indexCount;
    atomic_uint instanceCount;
    uint indexStart;
    int baseVertex;
    uint baseInstance;
};

// Utilities
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

inline float4 project_persp(float3 view_p, float2 vp, float focal) {
    float z = max(view_p.z, 0.25f);
    float aspect = vp.x / vp.y;
    float ndc_x = (view_p.x * focal) / (z * aspect);
    float ndc_y = (view_p.y * focal) / z;
    // Metal NDC depth [0.20..0.98] for 3D scene (front [0.01..0.15] reserved for SVG HUD)
    float ndc_z = clamp(0.20f + (1.0f - (2.0f / z)) * 0.78f, 0.20f, 0.98f);
    return float4(ndc_x, ndc_y, ndc_z, 1.0f);
}

kernel void k_dvfs_warm(device const float4 *in_a [[buffer(0)]],
                        device float4 *out_b [[buffer(1)]],
                        constant uint &iters [[buffer(2)]],
                        uint gid [[thread_position_in_grid]]) {
    float4 acc = in_a[gid & 1023u];
    for (uint i = 0; i < iters; ++i) acc = fma(acc, float4(1.0001f), float4(0.0002f));
    out_b[gid & 1023u] = acc;
}

kernel void k_tex_checksum(texture2d<half, access::read> tex [[texture(0)]],
                           device atomic_uint *out_acc [[buffer(0)]],
                           uint2 gid [[thread_position_in_grid]]) {
    uint w = tex.get_width(), h = tex.get_height();
    uint x = gid.x * 16u, y = gid.y * 16u;
    if (x >= w || y >= h) return;
    half4 p = tex.read(uint2(x, y));
    uint q = uint(clamp(p.r * 255.0h, 0.0h, 255.0h)) +
             (uint(clamp(p.g * 255.0h, 0.0h, 255.0h)) << 8u) +
             (uint(clamp(p.b * 255.0h, 0.0h, 255.0h)) << 16u);
    if (q != 0u) atomic_fetch_add_explicit(out_acc, q, memory_order_relaxed);
}

// ----------------------------------------------------------------------------
// Exp 2: Industry GLB 64B Skinned vs BLOOM Vtx12 + State48
// ----------------------------------------------------------------------------
struct ObjFragIn {
    float4 pos [[position]];
    half3 nrm;
    half4 col;
};

vertex ObjFragIn v_industry_skin64(uint vid [[vertex_id]],
                                   uint iid [[instance_id]],
                                   device const IndustryVtx64 *verts [[buffer(0)]],
                                   device const IndustryRig512 *rigs [[buffer(1)]],
                                   constant MeshletDrawParams &pm [[buffer(2)]]) {
    IndustryVtx64 v = verts[iid * 64u + (vid % 64u)];
    IndustryRig512 rig = rigs[iid];
    float4 w = float4(float(v.weights_u16[0]), float(v.weights_u16[1]), float(v.weights_u16[2]), float(v.weights_u16[3])) * (1.0f / 65535.0f);
    float4 lp = float4(float3(v.pos), 1.0f);
    float4 wp = (rig.bones[v.joints[0] & 7u] * lp) * w.x +
                (rig.bones[v.joints[1] & 7u] * lp) * w.y +
                (rig.bones[v.joints[2] & 7u] * lp) * w.z +
                (rig.bones[v.joints[3] & 7u] * lp) * w.w;
    float3x3 nm = float3x3(rig.bones[v.joints[0] & 7u][0].xyz,
                           rig.bones[v.joints[0] & 7u][1].xyz,
                           rig.bones[v.joints[0] & 7u][2].xyz);
    float3 wn = normalize(nm * float3(v.norm));
    float3 vp = wp.xyz - float3(pm.cam_x, pm.cam_y, pm.cam_z);
    ObjFragIn o;
    o.pos = project_persp(vp, float2(pm.vp_w, pm.vp_h), pm.focal);
    o.nrm = half3(wn);
    o.col = half4(0.75h, 0.82h, 0.95h, 1.0h);
    return o;
}

vertex ObjFragIn v_bloom_vtx12_state48(uint vid [[vertex_id]],
                                       uint iid [[instance_id]],
                                       device const BloomVtx12 *verts [[buffer(0)]],
                                       device const BloomObjectState48 *states [[buffer(1)]],
                                       constant MeshletDrawParams &pm [[buffer(2)]]) {
    BloomVtx12 v = verts[iid * 64u + (vid % 64u)];
    BloomObjectState48 st = states[iid];
    float3 lp = unpack_bloom_pos(v) * float3(float(st.sx), float(st.sy), float(st.sz));
    uint jmod = uint(v.joint_id % 3u);
    float dof = float(jmod == 0u ? st.dof0 : (jmod == 1u ? st.dof1 : st.dof2));
    lp.y += sin(dof + lp.x) * 0.08f;
    float4 q = float4(float(st.qx), float(st.qy), float(st.qz), float(st.qw));
    float3 wp = rotate_quat(q, lp) + float3(st.px, st.py, st.pz);
    float3 wn = rotate_quat(q, oct_decode_gpu(v.oct_x, v.oct_y));
    float3 vp = wp - float3(pm.cam_x, pm.cam_y, pm.cam_z);
    ObjFragIn o;
    o.pos = project_persp(vp, float2(pm.vp_w, pm.vp_h), pm.focal);
    o.nrm = half3(wn);
    o.col = unpack_unorm4x8_to_half(st.rgba8_mat);
    return o;
}

fragment half4 f_obj_lit(ObjFragIn in [[stage_in]]) {
    half3 n = normalize(in.nrm);
    half ndotl = clamp(dot(n, normalize(half3(0.4h, 0.7h, -0.5h))), 0.18h, 1.0h);
    half rim = pow(clamp(1.0h - abs(n.z), 0.0h, 1.0h), 2.0h) * 0.28h;
    return half4(in.col.rgb * ndotl + half3(0.10h, 0.25h, 0.35h) * rim, 1.0h);
}

// ----------------------------------------------------------------------------
// Exp 3: Meshlet Culling — Brute vs Compute-Indirect vs Metal 3 Object/Mesh
// ----------------------------------------------------------------------------
inline bool meshlet_visible(BloomMeshletHeader32 m, BloomObjectState48 st, constant MeshletDrawParams &pm) {
    float4 q = float4(float(st.qx), float(st.qy), float(st.qz), float(st.qw));
    float3 sc3 = float3(float(st.sx), float(st.sy), float(st.sz));
    float3 bs_c = float3(float(m.bs_x), float(m.bs_y), float(m.bs_z));
    float3 wc = rotate_quat(q, bs_c * sc3) + float3(st.px, st.py, st.pz);
    float3 vc = wc - float3(pm.cam_x, pm.cam_y, pm.cam_z);
    float max_sc = max(sc3.x, max(sc3.y, sc3.z));
    float rad = float(m.bs_r) * max_sc;
    float aspect = pm.vp_w / pm.vp_h;
    if (vc.z + rad < 0.5f || vc.z - rad > 60.0f) return false;
    float lim_y = vc.z * pm.frustum_half_tan;
    float lim_x = lim_y * aspect;
    if (abs(vc.x) - rad > lim_x || abs(vc.y) - rad > lim_y) return false;
    float3 axis = rotate_quat(q, float3(float(m.cone_x), float(m.cone_y), float(m.cone_z)) * (1.0f / 127.0f));
    float cutoff = float(m.cone_c) * (1.0f / 127.0f);
    if (dot(normalize(vc), axis) >= cutoff + rad / max(length(vc), 1e-4f)) return false;
    return true;
}

kernel void k_cull_meshlets(device const BloomMeshletHeader32 *hdrs [[buffer(0)]],
                            device const BloomObjectState48 *states [[buffer(1)]],
                            constant MeshletDrawParams &pm [[buffer(2)]],
                            device uint *surviving_ids [[buffer(3)]],
                            device DrawIndexedIndirectArgs &ind [[buffer(4)]],
                            uint gid [[thread_position_in_grid]]) {
    if (gid >= pm.total_meshlets) return;
    BloomMeshletHeader32 m = hdrs[gid];
    BloomObjectState48 st = states[m.obj_id];
    if (meshlet_visible(m, st, pm)) {
        uint slot = atomic_fetch_add_explicit(&ind.instanceCount, 1u, memory_order_relaxed);
        surviving_ids[slot] = gid;
    }
}

vertex ObjFragIn v_meshlet_brute(uint vid [[vertex_id]],
                                 uint iid [[instance_id]],
                                 device const BloomMeshletHeader32 *hdrs [[buffer(0)]],
                                 device const BloomVtx12 *verts [[buffer(1)]],
                                 device const BloomObjectState48 *states [[buffer(2)]],
                                 constant MeshletDrawParams &pm [[buffer(3)]]) {
    BloomMeshletHeader32 m = hdrs[iid];
    BloomVtx12 v = verts[m.vtx_offset + (vid % 64u)];
    BloomObjectState48 st = states[m.obj_id];
    float4 q = float4(float(st.qx), float(st.qy), float(st.qz), float(st.qw));
    float3 lp = unpack_bloom_pos(v) * float3(float(st.sx), float(st.sy), float(st.sz));
    float3 wp = rotate_quat(q, lp) + float3(st.px, st.py, st.pz);
    float3 wn = rotate_quat(q, oct_decode_gpu(v.oct_x, v.oct_y));
    ObjFragIn o;
    o.pos = project_persp(wp - float3(pm.cam_x, pm.cam_y, pm.cam_z), float2(pm.vp_w, pm.vp_h), pm.focal);
    o.nrm = half3(wn);
    o.col = unpack_unorm4x8_to_half(st.rgba8_mat);
    return o;
}

vertex ObjFragIn v_meshlet_indirect(uint vid [[vertex_id]],
                                    uint iid [[instance_id]],
                                    device const uint *surviving_ids [[buffer(0)]],
                                    device const BloomMeshletHeader32 *hdrs [[buffer(1)]],
                                    device const BloomVtx12 *verts [[buffer(2)]],
                                    device const BloomObjectState48 *states [[buffer(3)]],
                                    constant MeshletDrawParams &pm [[buffer(4)]]) {
    uint m_id = surviving_ids[iid];
    BloomMeshletHeader32 m = hdrs[m_id];
    BloomVtx12 v = verts[m.vtx_offset + (vid % 64u)];
    BloomObjectState48 st = states[m.obj_id];
    float4 q = float4(float(st.qx), float(st.qy), float(st.qz), float(st.qw));
    float3 lp = unpack_bloom_pos(v) * float3(float(st.sx), float(st.sy), float(st.sz));
    float3 wp = rotate_quat(q, lp) + float3(st.px, st.py, st.pz);
    float3 wn = rotate_quat(q, oct_decode_gpu(v.oct_x, v.oct_y));
    ObjFragIn o;
    o.pos = project_persp(wp - float3(pm.cam_x, pm.cam_y, pm.cam_z), float2(pm.vp_w, pm.vp_h), pm.focal);
    o.nrm = half3(wn);
    o.col = unpack_unorm4x8_to_half(st.rgba8_mat);
    return o;
}

// Metal 3 Object + Mesh Shader Pipeline
struct MeshCullPayload {
    uint meshlet_ids[32];
};

using BloomTriMesh = mesh<ObjFragIn, void, 64, 96, topology::triangle>;

[[object]]
void o_bloom_cull(object_data MeshCullPayload &pl [[payload]],
                  mesh_grid_properties mgp,
                  device const BloomMeshletHeader32 *hdrs [[buffer(0)]],
                  device const BloomObjectState48 *states [[buffer(1)]],
                  constant MeshletDrawParams &pm [[buffer(2)]],
                  uint tid [[thread_position_in_threadgroup]],
                  uint gid [[threadgroup_position_in_grid]]) {
    uint m_id = gid * 32u + tid;
    bool vis = false;
    if (m_id < pm.total_meshlets) {
        BloomMeshletHeader32 m = hdrs[m_id];
        vis = meshlet_visible(m, states[m.obj_id], pm);
    }
    uint out_slot = simd_prefix_exclusive_sum(vis ? 1u : 0u);
    uint surv_cnt = simd_sum(vis ? 1u : 0u);
    if (vis) pl.meshlet_ids[out_slot] = m_id;
    if (tid == 0u) mgp.set_threadgroups_per_grid(uint3(surv_cnt, 1u, 1u));
}

[[mesh]]
void m_bloom_emit(BloomTriMesh out,
                  const object_data MeshCullPayload &pl [[payload]],
                  device const BloomMeshletHeader32 *hdrs [[buffer(0)]],
                  device const BloomVtx12 *verts [[buffer(1)]],
                  device const uchar *local_idx [[buffer(2)]],
                  device const BloomObjectState48 *states [[buffer(3)]],
                  constant MeshletDrawParams &pm [[buffer(4)]],
                  uint tid [[thread_position_in_threadgroup]],
                  uint mgid [[threadgroup_position_in_grid]]) {
    uint m_id = pl.meshlet_ids[mgid];
    BloomMeshletHeader32 m = hdrs[m_id];
    BloomObjectState48 st = states[m.obj_id];
    float4 q = float4(float(st.qx), float(st.qy), float(st.qz), float(st.qw));
    if (tid == 0u) out.set_primitive_count(m.tri_cnt);
    if (tid < m.vtx_cnt) {
        BloomVtx12 v = verts[m.vtx_offset + tid];
        float3 lp = unpack_bloom_pos(v) * float3(float(st.sx), float(st.sy), float(st.sz));
        float3 wp = rotate_quat(q, lp) + float3(st.px, st.py, st.pz);
        float3 wn = rotate_quat(q, oct_decode_gpu(v.oct_x, v.oct_y));
        ObjFragIn vo;
        vo.pos = project_persp(wp - float3(pm.cam_x, pm.cam_y, pm.cam_z), float2(pm.vp_w, pm.vp_h), pm.focal);
        vo.nrm = half3(wn);
        vo.col = unpack_unorm4x8_to_half(st.rgba8_mat);
        out.set_vertex(tid, vo);
    }
    if (tid < m.tri_cnt) {
        out.set_index(tid * 3u + 0u, local_idx[tid * 3u + 0u]);
        out.set_index(tid * 3u + 1u, local_idx[tid * 3u + 1u]);
        out.set_index(tid * 3u + 2u, local_idx[tid * 3u + 2u]);
    }
}

// ----------------------------------------------------------------------------
// Exp 4: Tight-Proxy Analytic Spheres vs Tessellated Mesh vs 48-step Raymarch
// ----------------------------------------------------------------------------
kernel void k_raymarch_48step(texture2d<half, access::write> out_tex [[texture(0)]],
                              device const BloomAnalyticPrim32 *prims [[buffer(0)]],
                              constant RaymarchTileParams &rp [[buffer(1)]],
                              uint2 gid [[thread_position_in_grid]]) {
    if (gid.x >= rp.width || gid.y >= rp.height) return;
    float2 uv = (float2(gid) + 0.5f) / float2(float(rp.width), float(rp.height)) * 2.0f - 1.0f;
    uv.y = -uv.y;
    float aspect = float(rp.width) / float(rp.height);
    float3 ro = float3(rp.cam_x, rp.cam_y, rp.cam_z);
    float3 rd = normalize(float3(uv.x * aspect / rp.focal, uv.y / rp.focal, 1.0f));

    uint cell_x = clamp(uint((uv.x + 1.0f) * 8.0f), 0u, 15u);
    uint cell_y = clamp(uint((-uv.y + 1.0f) * 8.0f), 0u, 15u);
    uint start_i = (cell_y * 16u + cell_x) % rp.prim_count;

    float t = 0.2f;
    half3 col = half3(0.03h, 0.04h, 0.07h);
    for (int step = 0; step < 48; ++step) {
        float3 p = ro + rd * t;
        float min_d = 1e6f;
        uint hit_i = 0u;
        for (uint k = 0u; k < 16u; ++k) {
            BloomAnalyticPrim32 sp = prims[(start_i + k) % rp.prim_count];
            float d = length(p - float3(sp.cx, sp.cy, sp.cz)) - sp.radius;
            if (d < min_d) { min_d = d; hit_i = (start_i + k) % rp.prim_count; }
        }
        if (min_d < 0.005f) {
            BloomAnalyticPrim32 sp = prims[hit_i];
            half3 n = half3(normalize(p - float3(sp.cx, sp.cy, sp.cz)));
            half diff = clamp(dot(n, normalize(half3(0.4h, 0.7h, -0.5h))), 0.2h, 1.0h);
            col = unpack_unorm4x8_to_half(sp.rgba8).rgb * diff;
            break;
        }
        t += max(min_d, 0.02f);
        if (t > 40.0f) break;
    }
    out_tex.write(half4(col, 1.0h), gid);
}

struct TightProxyIn {
    float4 pos [[position]];
    uint idx [[flat]];
};

vertex TightProxyIn v_tight_proxy_analytic(uint vid [[vertex_id]],
                                           uint iid [[instance_id]],
                                           device const BloomAnalyticPrim32 *prims [[buffer(0)]],
                                           constant MeshletDrawParams &pm [[buffer(1)]]) {
    BloomAnalyticPrim32 sp = prims[iid];
    float3 vc = float3(sp.cx, sp.cy, sp.cz) - float3(pm.cam_x, pm.cam_y, pm.cam_z);
    float aspect = pm.vp_w / pm.vp_h;
    float r = sp.radius;
    float z_near = max(vc.z - r, 0.25f);
    float half_ndc_y = (r * pm.focal) / z_near;
    float half_ndc_x = half_ndc_y / aspect;
    float2 c_ndc = float2((vc.x * pm.focal) / (vc.z * aspect), (vc.y * pm.focal) / vc.z);
    float2 corner = float2((vid & 1u) ? 1.0f : -1.0f, (vid & 2u) ? 1.0f : -1.0f);
    float ndc_z = clamp(0.20f + (1.0f - (2.0f / max(vc.z, 0.25f))) * 0.78f, 0.20f, 0.98f);
    return TightProxyIn{float4(c_ndc + corner * float2(half_ndc_x, half_ndc_y), ndc_z, 1.0f), iid};
}

fragment half4 f_tight_proxy_analytic(TightProxyIn in [[stage_in]],
                                      device const BloomAnalyticPrim32 *prims [[buffer(0)]],
                                      constant MeshletDrawParams &pm [[buffer(1)]]) {
    BloomAnalyticPrim32 sp = prims[in.idx];
    float2 uv = (in.pos.xy / float2(pm.vp_w, pm.vp_h)) * 2.0f - 1.0f;
    uv.y = -uv.y;
    float aspect = pm.vp_w / pm.vp_h;
    float3 ro = float3(pm.cam_x, pm.cam_y, pm.cam_z);
    float3 rd = normalize(float3(uv.x * aspect / pm.focal, uv.y / pm.focal, 1.0f));
    float3 sp_c = float3(sp.cx, sp.cy, sp.cz);
    float3 oc = ro - sp_c;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - sp.radius * sp.radius;
    float h = b * b - c;
    if (h < 0.0f) return half4(0.0h);
    float t = -b - sqrt(h);
    float3 hp = ro + rd * t;
    half3 n = half3(normalize(hp - sp_c));
    half3 col = unpack_unorm4x8_to_half(sp.rgba8).rgb;
    half diff = clamp(dot(n, normalize(half3(0.4h, 0.7h, -0.5h))), 0.22h, 1.0h);
    return half4(col * diff, 1.0h);
}

struct FragDepthOut {
    half4 color [[color(0)]];
    float depth [[depth(less)]];
};

fragment FragDepthOut f_tight_proxy_exact_depth(TightProxyIn in [[stage_in]],
                                                device const BloomAnalyticPrim32 *prims [[buffer(0)]],
                                                constant MeshletDrawParams &pm [[buffer(1)]]) {
    BloomAnalyticPrim32 sp = prims[in.idx];
    float2 uv = (in.pos.xy / float2(pm.vp_w, pm.vp_h)) * 2.0f - 1.0f;
    uv.y = -uv.y;
    float aspect = pm.vp_w / pm.vp_h;
    float3 ro = float3(pm.cam_x, pm.cam_y, pm.cam_z);
    float3 rd = normalize(float3(uv.x * aspect / pm.focal, uv.y / pm.focal, 1.0f));
    float3 sp_c = float3(sp.cx, sp.cy, sp.cz);
    float3 oc = ro - sp_c;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - sp.radius * sp.radius;
    float h = b * b - c;
    FragDepthOut o;
    if (h < 0.0f) { o.color = half4(0.0h); o.depth = 1.0f; return o; }
    float t = -b - sqrt(h);
    float3 hp = ro + rd * t;
    half3 n = half3(normalize(hp - sp_c));
    half3 col = unpack_unorm4x8_to_half(sp.rgba8).rgb;
    half diff = clamp(dot(n, normalize(half3(0.4h, 0.7h, -0.5h))), 0.22h, 1.0h);
    o.color = half4(col * diff, 1.0h);
    float vz = max((hp.z - pm.cam_z), 0.25f);
    o.depth = clamp(0.20f + (1.0f - (2.0f / vz)) * 0.78f, 0.20f, 0.98f);
    return o;
}

// ----------------------------------------------------------------------------
// Exp 5: Live Runtime Cutaway — Hardware [[clip_distance]] vs discard_fragment()
// ----------------------------------------------------------------------------
struct ObjClipDistIn {
    float4 pos [[position]];
    float clip_d [[clip_distance]] [1];
    half3 nrm;
    half4 col;
};

struct ObjFragDiscIn {
    float4 pos [[position]];
    float plane_d;
    half3 nrm;
    half4 col [[flat]];
};

vertex ObjClipDistIn v_bloom_cutaway_clip(uint vid [[vertex_id]],
                                          uint iid [[instance_id]],
                                          device const BloomVtx12 *verts [[buffer(0)]],
                                          device const BloomObjectState48 *states [[buffer(1)]],
                                          constant MeshletDrawParams &pm [[buffer(2)]]) {
    BloomVtx12 v = verts[iid * 64u + (vid % 64u)];
    BloomObjectState48 st = states[iid];
    float4 q = float4(float(st.qx), float(st.qy), float(st.qz), float(st.qw));
    float3 lp = unpack_bloom_pos(v) * float3(float(st.sx), float(st.sy), float(st.sz));
    float3 wp = rotate_quat(q, lp) + float3(st.px, st.py, st.pz);
    float3 wn = rotate_quat(q, oct_decode_gpu(v.oct_x, v.oct_y));
    ObjClipDistIn o;
    o.pos = project_persp(wp - float3(pm.cam_x, pm.cam_y, pm.cam_z), float2(pm.vp_w, pm.vp_h), pm.focal);
    o.clip_d[0] = ((st.flags & 1u) != 0u) ? dot(lp, float3(float(st.cp0), float(st.cp1), float(st.cp2))) - float(st.cp3) : 1.0f;
    o.nrm = half3(wn);
    o.col = unpack_unorm4x8_to_half(st.rgba8_mat);
    return o;
}

fragment half4 f_bloom_cutaway_clip(ObjFragIn in [[stage_in]]) {
    half3 n = normalize(in.nrm);
    half ndotl = clamp(dot(n, normalize(half3(0.4h, 0.7h, -0.5h))), 0.18h, 1.0h);
    return half4(in.col.rgb * ndotl, 1.0h);
}

vertex ObjFragDiscIn v_bloom_cutaway_disc(uint vid [[vertex_id]],
                                          uint iid [[instance_id]],
                                          device const BloomVtx12 *verts [[buffer(0)]],
                                          device const BloomObjectState48 *states [[buffer(1)]],
                                          constant MeshletDrawParams &pm [[buffer(2)]]) {
    BloomVtx12 v = verts[iid * 64u + (vid % 64u)];
    BloomObjectState48 st = states[iid];
    float4 q = float4(float(st.qx), float(st.qy), float(st.qz), float(st.qw));
    float3 lp = unpack_bloom_pos(v) * float3(float(st.sx), float(st.sy), float(st.sz));
    float3 wp = rotate_quat(q, lp) + float3(st.px, st.py, st.pz);
    float3 wn = rotate_quat(q, oct_decode_gpu(v.oct_x, v.oct_y));
    ObjFragDiscIn o;
    o.pos = project_persp(wp - float3(pm.cam_x, pm.cam_y, pm.cam_z), float2(pm.vp_w, pm.vp_h), pm.focal);
    o.plane_d = ((st.flags & 1u) != 0u) ? dot(lp, float3(float(st.cp0), float(st.cp1), float(st.cp2))) - float(st.cp3) : 1.0f;
    o.nrm = half3(wn);
    o.col = unpack_unorm4x8_to_half(st.rgba8_mat);
    return o;
}

fragment half4 f_bloom_cutaway_disc(ObjFragDiscIn in [[stage_in]]) {
    if (in.plane_d < 0.0f) discard_fragment();
    half3 n = normalize(in.nrm);
    half ndotl = clamp(dot(n, normalize(half3(0.4h, 0.7h, -0.5h))), 0.18h, 1.0h);
    return half4(in.col.rgb * ndotl, 1.0h);
}

// ----------------------------------------------------------------------------
// Exp 6: 2D SVG-SSOT HUD Overlay Shaders (Depth [0.01..0.15] in same pass)
// ----------------------------------------------------------------------------
inline float2 to_ndc_2d(float2 px, float2 vp) {
    float2 n = (px / vp) * 2.0f - 1.0f;
    return float2(n.x, -n.y);
}

inline half erf7_h(half x) {
    half s = sign(x);
    half a = abs(x);
    half p = 1.0h + a * (0.278393h + a * (0.230389h + a * (0.000972h + a * 0.078108h)));
    half p2 = p * p;
    return s * (1.0h - 1.0h / (p2 * p2));
}

struct HudOpaqueIn { float4 pos [[position]]; half4 fill [[flat]]; };
struct HudRefetchIn { float4 pos [[position]]; uint idx [[flat]]; };

vertex HudOpaqueIn v_hud_opaque(uint vid [[vertex_id]], uint iid [[instance_id]],
                                device const BloomQuadHot *quads [[buffer(0)]],
                                device const BloomClipRect *clips [[buffer(1)]],
                                constant float2 &vp [[buffer(2)]]) {
    BloomQuadHot q = quads[iid];
    BloomClipRect c = clips[q.clip_id];
    float4 b = float4(float(q.b0), float(q.b1), float(q.b2), float(q.b3)) * 0.25f;
    float2 b0 = max(b.xy, c.rect.xy), b1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    float2 px = mix(b0, b1, float2(float(vid & 1u), float((vid >> 1u) & 1u)));
    float z = 0.14f - clamp(float(q.z_flags & 0xFFFFu) * 1e-5f, 0.0f, 0.12f);
    return HudOpaqueIn{float4(to_ndc_2d(px, vp), z, 1.0f), unpack_unorm4x8_to_half(q.rgba8_fill)};
}
fragment half4 f_hud_opaque(HudOpaqueIn in [[stage_in]]) { return in.fill; }

vertex HudRefetchIn v_hud_alpha(uint vid [[vertex_id]], uint iid [[instance_id]],
                                device const BloomQuadHot *quads [[buffer(0)]],
                                device const BloomClipRect *clips [[buffer(1)]],
                                constant float2 &vp [[buffer(2)]]) {
    BloomQuadHot q = quads[iid];
    BloomClipRect c = clips[q.clip_id];
    float4 b = float4(float(q.b0), float(q.b1), float(q.b2), float(q.b3)) * 0.25f;
    float2 b0 = max(b.xy, c.rect.xy), b1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    float2 px = mix(b0, b1, float2(float(vid & 1u), float((vid >> 1u) & 1u)));
    float z = 0.14f - clamp(float(q.z_flags & 0xFFFFu) * 1e-5f, 0.0f, 0.12f);
    return HudRefetchIn{float4(to_ndc_2d(px, vp), z, 1.0f), iid};
}
fragment half4 f_hud_alpha(HudRefetchIn in [[stage_in]], device const BloomQuadHot *quads [[buffer(0)]]) {
    BloomQuadHot q = quads[in.idx];
    half4 b = half4(half(q.b0), half(q.b1), half(q.b2), half(q.b3)) * 0.25h;
    half2 half_sz = b.zw * 0.5h, c2p = (half2(in.pos.xy) - b.xy) - half_sz;
    half4 rr = half4(half(q.r0), half(q.r1), half(q.r2), half(q.r3)) * 0.25h;
    half4 bb = half4(half(q.bw0), half(q.bw1), half(q.bw2), half(q.bw3)) * 0.25h;
    half r = (c2p.x < 0.0h) ? (c2p.y < 0.0h ? rr.x : rr.w) : (c2p.y < 0.0h ? rr.y : rr.z);
    half bw = min(c2p.x < 0.0h ? bb.w : bb.y, c2p.y < 0.0h ? bb.x : bb.z);
    half2 d = abs(c2p) - half_sz + r;
    half outer = length(max(d, 0.0h)) + min(max(d.x, d.y), 0.0h) - r;
    half4 col = mix(unpack_unorm4x8_to_half(q.rgba8_border), unpack_unorm4x8_to_half(q.rgba8_fill), clamp(0.5h - (outer + bw), 0.0h, 1.0h));
    col.a *= clamp(0.5h - outer, 0.0h, 1.0h);
    return col;
}

vertex HudRefetchIn v_hud_shadow(uint vid [[vertex_id]], uint iid [[instance_id]],
                                 device const BloomShadowHot *shadows [[buffer(0)]],
                                 device const BloomClipRect *clips [[buffer(1)]],
                                 constant float2 &vp [[buffer(2)]]) {
    BloomShadowHot sh = shadows[iid];
    BloomClipRect c = clips[sh.clip_id];
    float4 b = float4(float(sh.b0), float(sh.b1), float(sh.b2), float(sh.b3)) * 0.25f;
    float2 b0 = max(b.xy, c.rect.xy), b1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    float2 px = mix(b0, b1, float2(float(vid & 1u), float((vid >> 1u) & 1u)));
    float z = 0.14f - clamp(float(sh.z_flags) * 1e-5f, 0.0f, 0.12f);
    return HudRefetchIn{float4(to_ndc_2d(px, vp), z, 1.0f), iid};
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
                                constant float2 &vp [[buffer(2)]]) {
    BloomGlyphInst g = glyphs[iid];
    BloomClipRect c = clips[g.clip_id];
    float4 b = float4(float(g.d0), float(g.d1), float(g.d2), float(g.d3)) * 0.25f;
    float2 p0 = max(b.xy, c.rect.xy), p1 = min(b.xy + b.zw, c.rect.xy + c.rect.zw);
    float2 px = mix(p0, p1, float2(float(vid & 1u), float((vid >> 1u) & 1u)));
    float z = 0.14f - clamp(float(g.z_flags) * 1e-5f, 0.0f, 0.12f);
    return HudRefetchIn{float4(to_ndc_2d(px, vp), z, 1.0f), iid};
}
fragment half4 f_hud_glyph(HudRefetchIn in [[stage_in]], device const BloomGlyphInst *glyphs [[buffer(0)]], texture2d<half> atlas [[texture(0)]]) {
    BloomGlyphInst g = glyphs[in.idx];
    float4 b = float4(float(g.d0), float(g.d1), float(g.d2), float(g.d3)) * 0.25f;
    float2 luv = (in.pos.xy - b.xy) / max(b.zw, float2(1e-4f));
    float4 uvr = float4(float(g.u0), float(g.u1), float(g.u2), float(g.u3)) * (1.0f / 65535.0f);
    constexpr sampler smp(mag_filter::linear, min_filter::linear);
    half4 col = unpack_unorm4x8_to_half(g.rgba8);
    col.a *= atlas.sample(smp, uvr.xy + luv * uvr.zw).r;
    return col;
}
)MSL";

// ============================================================================
// Metal Context & Builders
// ============================================================================
struct Ctx {
    id<MTLDevice> dev = nil;
    id<MTLCommandQueue> q = nil;
    id<MTLLibrary> lib = nil;
};

static id<MTLComputePipelineState> cpso(Ctx &ctx, const char *fn) {
    NSError *err = nil;
    id<MTLFunction> f = [ctx.lib newFunctionWithName:[NSString stringWithUTF8String:fn]];
    if (!f) { fprintf(stderr, "missing compute fn %s\n", fn); exit(1); }
    id<MTLComputePipelineState> p = [ctx.dev newComputePipelineStateWithFunction:f error:&err];
    if (!p || err) { fprintf(stderr, "compute pso err %s\n", fn); exit(1); }
    return p;
}

static id<MTLRenderPipelineState> rpso(Ctx &ctx, const char *vf, const char *ff, bool blend) {
    NSError *err = nil;
    id<MTLFunction> vfn = [ctx.lib newFunctionWithName:[NSString stringWithUTF8String:vf]];
    id<MTLFunction> ffn = [ctx.lib newFunctionWithName:[NSString stringWithUTF8String:ff]];
    if (!vfn || !ffn) { fprintf(stderr, "missing render fn %s/%s\n", vf, ff); exit(1); }
    MTLRenderPipelineDescriptor *d = [[MTLRenderPipelineDescriptor alloc] init];
    d.vertexFunction = vfn;
    d.fragmentFunction = ffn;
    d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    if (blend) {
        d.colorAttachments[0].blendingEnabled = YES;
        d.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        d.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        d.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        d.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        d.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
        d.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    }
    id<MTLRenderPipelineState> p = [ctx.dev newRenderPipelineStateWithDescriptor:d error:&err];
    if (!p || err) {
        fprintf(stderr, "render pso err %s/%s: %s\n", vf, ff, err ? err.localizedDescription.UTF8String : "?");
        exit(1);
    }
    return p;
}

static id<MTLRenderPipelineState> mesh_pso(Ctx &ctx, const char *ofn, const char *mfn, const char *ffn,
                                           size_t payload_bytes, std::string &out_err) {
    NSError *err = nil;
    id<MTLFunction> o = [ctx.lib newFunctionWithName:[NSString stringWithUTF8String:ofn]];
    id<MTLFunction> m = [ctx.lib newFunctionWithName:[NSString stringWithUTF8String:mfn]];
    id<MTLFunction> f = [ctx.lib newFunctionWithName:[NSString stringWithUTF8String:ffn]];
    if (!o || !m || !f) { out_err = "missing object/mesh/frag fn"; return nil; }
    MTLMeshRenderPipelineDescriptor *d = [[MTLMeshRenderPipelineDescriptor alloc] init];
    d.objectFunction = o;
    d.meshFunction = m;
    d.fragmentFunction = f;
    d.payloadMemoryLength = payload_bytes;
    d.maxTotalThreadsPerObjectThreadgroup = 32;
    d.maxTotalThreadsPerMeshThreadgroup = 96;
    d.maxTotalThreadgroupsPerMeshGrid = 32;
    d.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    d.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    id<MTLRenderPipelineState> p = [ctx.dev newRenderPipelineStateWithMeshDescriptor:d options:0 reflection:nil error:&err];
    if (!p || err) {
        out_err = err ? err.localizedDescription.UTF8String : "mesh pso creation failed";
        return nil;
    }
    return p;
}

static id<MTLTexture> tex2d(id<MTLDevice> dev, uint32_t w, uint32_t h, MTLPixelFormat fmt,
                            MTLTextureUsage usage, MTLStorageMode mode, bool opt) {
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:fmt width:w height:h mipmapped:NO];
    d.usage = usage;
    d.storageMode = mode;
    d.allowGPUOptimizedContents = opt ? YES : NO;
    id<MTLTexture> t = [dev newTextureWithDescriptor:d];
    if (!t) { fprintf(stderr, "tex2d alloc failed\n"); exit(1); }
    return t;
}

static id<MTLBuffer> upload_private(Ctx &ctx, const void *src, size_t bytes) {
    id<MTLBuffer> stg = [ctx.dev newBufferWithBytes:src length:bytes options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> prv = [ctx.dev newBufferWithLength:bytes options:MTLResourceStorageModePrivate];
    id<MTLCommandBuffer> cb = [ctx.q commandBuffer];
    id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
    [bl copyFromBuffer:stg sourceOffset:0 toBuffer:prv destinationOffset:0 size:bytes];
    [bl endEncoding]; [cb commit]; [cb waitUntilCompleted];
    return prv;
}

static void warm_gpu_dvfs(Ctx &ctx) {
    id<MTLComputePipelineState> pw = cpso(ctx, "k_dvfs_warm");
    id<MTLBuffer> b0 = [ctx.dev newBufferWithLength:16384 options:MTLResourceStorageModeShared];
    id<MTLBuffer> b1 = [ctx.dev newBufferWithLength:16384 options:MTLResourceStorageModeShared];
    uint32_t iters = 4096u;
    for (int i = 0; i < 6; ++i) {
        id<MTLCommandBuffer> c = [ctx.q commandBuffer];
        id<MTLComputeCommandEncoder> e = [c computeCommandEncoder];
        [e setComputePipelineState:pw];
        [e setBuffer:b0 offset:0 atIndex:0];
        [e setBuffer:b1 offset:0 atIndex:1];
        [e setBytes:&iters length:sizeof(iters) atIndex:2];
        [e dispatchThreadgroups:MTLSizeMake(1024, 1, 1) threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
        [e endEncoding]; [c commit]; [c waitUntilCompleted];
    }
}

static uint32_t compute_tex_checksum(Ctx &ctx, id<MTLComputePipelineState> psum, id<MTLTexture> tex) {
    id<MTLBuffer> acc = [ctx.dev newBufferWithLength:4 options:MTLResourceStorageModeShared];
    *static_cast<uint32_t *>(acc.contents) = 0u;
    id<MTLCommandBuffer> c = [ctx.q commandBuffer];
    id<MTLComputeCommandEncoder> e = [c computeCommandEncoder];
    [e setComputePipelineState:psum];
    [e setTexture:tex atIndex:0];
    [e setBuffer:acc offset:0 atIndex:0];
    uint32_t gx = (static_cast<uint32_t>(tex.width) + 15u) / 16u;
    uint32_t gy = (static_cast<uint32_t>(tex.height) + 15u) / 16u;
    [e dispatchThreadgroups:MTLSizeMake((gx + 15u) / 16u, (gy + 15u) / 16u, 1u)
      threadsPerThreadgroup:MTLSizeMake(16u, 16u, 1u)];
    [e endEncoding]; [c commit]; [c waitUntilCompleted];
    return *static_cast<uint32_t *>(acc.contents);
}

static void fill_quat_y(float angle, uint16_t out_f16[4]) {
    float s = std::sin(angle * 0.5f);
    float c = std::cos(angle * 0.5f);
    out_f16[0] = f32_to_f16_hw(0.0f);
    out_f16[1] = f32_to_f16_hw(s);
    out_f16[2] = f32_to_f16_hw(0.0f);
    out_f16[3] = f32_to_f16_hw(c);
}

// ============================================================================
// Exp 1: Init Bake Latency, Footprint Compression & Quantization Error
// ============================================================================
static std::string bench_exp1_init_bake() {
    printf("\n=== [1/6] init_bake_and_compression: Canonical Torus-Reactor (16 meshlets, 1024 vtx, 1536 tris) ===\n");
    const uint32_t M = 16u;
    std::vector<BloomVtx12> bv(M * CLUSTER_VTX);
    std::vector<IndustryVtx64> iv(M * CLUSTER_VTX);
    std::vector<uint8_t> b_idx(CLUSTER_TRI * 3u);
    std::vector<uint32_t> i_idx(M * CLUSTER_TRI * 3u);
    std::vector<BloomMeshletHeader32> hdrs(M);

    float max_pos_err = 0.0f;
    double sum_norm_deg = 0.0, max_norm_deg = 0.0;
    std::vector<double> bloom_bake_us, ind_bake_us;

    for (int it = 0; it < 100; ++it) {
        max_pos_err = 0.0f; sum_norm_deg = 0.0; max_norm_deg = 0.0;
        uint64_t t0 = now_ns();
        for (uint32_t m = 0; m < M; ++m) {
            build_torus_patch_cluster(m, M, 0u, bv.data(), nullptr,
                                      m == 0 ? b_idx.data() : nullptr, nullptr, hdrs[m],
                                      m * CLUSTER_VTX, 0u, &max_pos_err, &sum_norm_deg, &max_norm_deg);
        }
        uint64_t t1 = now_ns();
        uint64_t t2 = now_ns();
        for (uint32_t m = 0; m < M; ++m) {
            build_torus_patch_cluster(m, M, 0u, nullptr, iv.data(),
                                      nullptr, i_idx.data(), hdrs[m],
                                      m * CLUSTER_VTX, m * CLUSTER_TRI * 3u);
        }
        uint64_t t3 = now_ns();
        if (it >= 5) {
            bloom_bake_us.push_back(static_cast<double>(t1 - t0) / 1e3);
            ind_bake_us.push_back(static_cast<double>(t3 - t2) / 1e3);
        }
    }

    size_t ind_bytes = iv.size() * sizeof(IndustryVtx64) + i_idx.size() * sizeof(uint32_t);
    size_t bloom_bytes = bv.size() * sizeof(BloomVtx12) + hdrs.size() * sizeof(BloomMeshletHeader32) + b_idx.size();
    double compression = static_cast<double>(ind_bytes) / static_cast<double>(bloom_bytes);
    double mean_norm_deg = sum_norm_deg / static_cast<double>(bv.size());
    Stats sb = compute_stats(bloom_bake_us), si = compute_stats(ind_bake_us);

    printf("  Industry GLB64+u32 | %6zu B | bake=%5.1f us\n", ind_bytes, si.median_val);
    printf("  BLOOM Vtx12+Hdr32  | %6zu B (%.2fx smaller) | bake=%5.1f us | max_pos_err=%.5f m (%.4f%% diag) | norm_err mean=%.2f deg max=%.2f deg\n",
           bloom_bytes, compression, sb.median_val, max_pos_err, (max_pos_err / 3.0) * 100.0, mean_norm_deg, max_norm_deg);

    std::ostringstream js;
    js << "  \"init_bake_and_compression\": {\n"
       << "    \"meshlets\": " << M << ", \"vertices\": " << bv.size() << ", \"triangles\": " << (M * CLUSTER_TRI) << ",\n"
       << "    \"industry_bytes\": " << ind_bytes << ", \"bloom_bytes\": " << bloom_bytes << ",\n"
       << "    \"compression_ratio\": " << compression << ",\n"
       << "    \"industry_bake_us\": " << si.median_val << ", \"bloom_bake_us\": " << sb.median_val << ",\n"
       << "    \"max_pos_err_m\": " << max_pos_err << ", \"mean_norm_err_deg\": " << mean_norm_deg << ", \"max_norm_err_deg\": " << max_norm_deg << "\n"
       << "  }";
    return js.str();
}

// ============================================================================
// Exp 2: 2,048 Articulated Objects (196k Tris) — Industry 64B Skin vs BLOOM 12B+48B State
// ============================================================================
static std::string bench_exp2_runtime_state(Ctx &ctx) {
    printf("\n=== [2/6] runtime_state_stream_vs_skinning: 2,048 Articulated Objects (196,608 tris) @ 4K ===\n");
    const uint32_t W = 3840, H = 2160;
    const uint32_t N_OBJ = 2048u;
    const uint32_t N_VTX = N_OBJ * CLUSTER_VTX; // 131,072 unique vertices

    std::vector<BloomVtx12> bv(N_VTX);
    std::vector<IndustryVtx64> iv(N_VTX);
    std::vector<uint8_t> loc_u8(CLUSTER_TRI * 3u);
    std::vector<uint16_t> shared_tri_u16(CLUSTER_TRI * 3u);
    std::vector<uint32_t> ind_u32(CLUSTER_TRI * 3u);
    BloomMeshletHeader32 dummy_hdr{};
    for (uint32_t i = 0; i < N_OBJ; ++i) {
        build_torus_patch_cluster(i & 15u, 16u, static_cast<uint16_t>(i),
                                  bv.data(), iv.data(),
                                  i == 0 ? loc_u8.data() : nullptr, nullptr, dummy_hdr,
                                  i * CLUSTER_VTX, 0u);
    }
    for (size_t k = 0; k < CLUSTER_TRI * 3u; ++k) {
        shared_tri_u16[k] = loc_u8[k];
        ind_u32[k] = loc_u8[k];
    }

    size_t ind_vram = iv.size() * sizeof(IndustryVtx64);
    size_t bloom_vram = bv.size() * sizeof(BloomVtx12);
    id<MTLBuffer> priv_iv = upload_private(ctx, iv.data(), ind_vram);
    id<MTLBuffer> priv_bv = upload_private(ctx, bv.data(), bloom_vram);
    id<MTLBuffer> priv_i32 = upload_private(ctx, ind_u32.data(), ind_u32.size() * sizeof(uint32_t));
    id<MTLBuffer> priv_i16 = upload_private(ctx, shared_tri_u16.data(), shared_tri_u16.size() * sizeof(uint16_t));

    size_t ind_wc_bytes = N_OBJ * sizeof(IndustryRig512);
    size_t bloom_wc_bytes = N_OBJ * sizeof(BloomObjectState48);
    id<MTLBuffer> wc_rigs   = [ctx.dev newBufferWithLength:ind_wc_bytes options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> wc_states = [ctx.dev newBufferWithLength:bloom_wc_bytes options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];

    std::vector<double> cpu_ind_us, cpu_bloom_us;
    for (int it = 0; it < 20; ++it) {
        float phase = static_cast<float>(it) * 0.05f;
        uint64_t t0 = now_ns();
        auto *rigs = static_cast<IndustryRig512 *>(wc_rigs.contents);
        for (uint32_t i = 0; i < N_OBJ; ++i) {
            float px = (static_cast<float>(i % 64u) - 31.5f) * 0.55f;
            float py = (static_cast<float>(i / 64u) - 15.5f) * 0.55f;
            float pz = 12.0f + static_cast<float>((i * 7u) % 10u) * 1.4f;
            float c = std::cos(phase + i * 0.03f), s = std::sin(phase + i * 0.03f);
            for (int b = 0; b < 8; ++b) {
                float *m = rigs[i].mats[b];
                std::memset(m, 0, sizeof(float) * 16);
                m[0] = 0.28f * c; m[2] = -0.28f * s; m[5] = 0.28f; m[8] = 0.28f * s; m[10] = 0.28f * c;
                m[12] = px; m[13] = py + 0.02f * b; m[14] = pz; m[15] = 1.0f;
            }
        }
        uint64_t t1 = now_ns();

        uint64_t b0 = now_ns();
        auto *st = static_cast<BloomObjectState48 *>(wc_states.contents);
        uint16_t sc = f32_to_f16_hw(0.28f);
        for (uint32_t i = 0; i < N_OBJ; ++i) {
            BloomObjectState48 s{};
            s.pos[0] = (static_cast<float>(i % 64u) - 31.5f) * 0.55f;
            s.pos[1] = (static_cast<float>(i / 64u) - 15.5f) * 0.55f;
            s.pos[2] = 12.0f + static_cast<float>((i * 7u) % 10u) * 1.4f;
            fill_quat_y(phase + i * 0.03f, s.quat_f16);
            s.scale_f16[0] = sc; s.scale_f16[1] = sc; s.scale_f16[2] = sc;
            s.dof_f16[0] = f32_to_f16_hw(phase); s.dof_f16[1] = f32_to_f16_hw(phase * 0.5f); s.dof_f16[2] = f32_to_f16_hw(0.2f);
            s.rgba8_mat = 0xFFE8B844u ^ ((i & 7u) * 0x080402u);
            s.flags = 0u;
            st[i] = s;
        }
        uint64_t b1 = now_ns();
        if (it >= 2) {
            cpu_ind_us.push_back(static_cast<double>(t1 - t0) / 1e3);
            cpu_bloom_us.push_back(static_cast<double>(b1 - b0) / 1e3);
        }
    }

    id<MTLRenderPipelineState> p_ind   = rpso(ctx, "v_industry_skin64", "f_obj_lit", false);
    id<MTLRenderPipelineState> p_bloom = rpso(ctx, "v_bloom_vtx12_state48", "f_obj_lit", false);
    id<MTLComputePipelineState> p_chk  = cpso(ctx, "k_tex_checksum");
    id<MTLTexture> rt  = tex2d(ctx.dev, W, H, MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
    id<MTLTexture> dep = tex2d(ctx.dev, W, H, MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);
    MTLDepthStencilDescriptor *dsd = [[MTLDepthStencilDescriptor alloc] init];
    dsd.depthCompareFunction = MTLCompareFunctionLess; dsd.depthWriteEnabled = YES;
    id<MTLDepthStencilState> ds_w = [ctx.dev newDepthStencilStateWithDescriptor:dsd];

    MeshletDrawParams pm{static_cast<float>(W), static_cast<float>(H), 0, 0, 0, 1.8f, 0.56f, N_OBJ, 0, 0};
    std::vector<double> gpu_ind_ms, gpu_bloom_ms;
    uint32_t chk_i = 0, chk_b = 0;

    for (int it = 0; it < 36; ++it) {
        auto run = [&](bool bloom) -> double {
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture = rt; rpd.colorAttachments[0].loadAction = MTLLoadActionClear; rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.depthAttachment.texture = dep; rpd.depthAttachment.loadAction = MTLLoadActionClear; rpd.depthAttachment.storeAction = MTLStoreActionDontCare; rpd.depthAttachment.clearDepth = 1.0;
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
            [enc setDepthStencilState:ds_w];
            [enc setCullMode:MTLCullModeBack];
            if (!bloom) {
                [enc setRenderPipelineState:p_ind];
                [enc setVertexBuffer:priv_iv offset:0 atIndex:0];
                [enc setVertexBuffer:wc_rigs offset:0 atIndex:1];
                [enc setVertexBytes:&pm length:sizeof(pm) atIndex:2];
                [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:CLUSTER_TRI * 3u indexType:MTLIndexTypeUInt32
                               indexBuffer:priv_i32 indexBufferOffset:0 instanceCount:N_OBJ];
            } else {
                [enc setRenderPipelineState:p_bloom];
                [enc setVertexBuffer:priv_bv offset:0 atIndex:0];
                [enc setVertexBuffer:wc_states offset:0 atIndex:1];
                [enc setVertexBytes:&pm length:sizeof(pm) atIndex:2];
                [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:CLUSTER_TRI * 3u indexType:MTLIndexTypeUInt16
                               indexBuffer:priv_i16 indexBufferOffset:0 instanceCount:N_OBJ];
            }
            [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
            return (cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0;
        };
        double d0 = run(false);
        if (it == 35) chk_i = compute_tex_checksum(ctx, p_chk, rt);
        double d1 = run(true);
        if (it == 35) chk_b = compute_tex_checksum(ctx, p_chk, rt);
        if (it >= 6) { gpu_ind_ms.push_back(d0); gpu_bloom_ms.push_back(d1); }
    }

    if (chk_i == 0 || chk_b == 0) { fprintf(stderr, "Exp2 zero checksum!\n"); exit(1); }
    Stats ci = compute_stats(cpu_ind_us), cb = compute_stats(cpu_bloom_us);
    Stats gi = compute_stats(gpu_ind_ms), gb = compute_stats(gpu_bloom_ms);
    printf("  Industry GLB64+512B | vram=%5.2f MB | WC=%6.1f KB | CPU=%6.1f us | GPU=%6.3f ms (%5.1f fps) | chk=0x%08X\n",
           ind_vram / (1024.0 * 1024.0), ind_wc_bytes / 1024.0, ci.median_val, gi.median_val, 1000.0 / gi.median_val, chk_i);
    printf("  BLOOM Vtx12+State48 | vram=%5.2f MB | WC=%6.1f KB | CPU=%6.1f us | GPU=%6.3f ms (%5.1f fps) | %.2fx GPU | %.1fx WC | chk=0x%08X\n",
           bloom_vram / (1024.0 * 1024.0), bloom_wc_bytes / 1024.0, cb.median_val, gb.median_val, 1000.0 / gb.median_val,
           gi.median_val / gb.median_val, static_cast<double>(ind_wc_bytes) / bloom_wc_bytes, chk_b);

    std::ostringstream js;
    js << "  \"runtime_state_stream_vs_skinning\": {\n"
       << "    \"objects\": " << N_OBJ << ", \"triangles\": " << (N_OBJ * CLUSTER_TRI) << ",\n"
       << "    \"industry\": { \"vram_mb\": " << (ind_vram / 1048576.0) << ", \"wc_kb\": " << (ind_wc_bytes / 1024.0)
       << ", \"cpu_us\": " << ci.median_val << ", \"gpu_med_ms\": " << gi.median_val << ", \"gpu_p95_ms\": " << gi.p95_val
       << ", \"fps\": " << (1000.0 / gi.median_val) << ", \"checksum\": " << chk_i << " },\n"
       << "    \"bloom\": { \"vram_mb\": " << (bloom_vram / 1048576.0) << ", \"wc_kb\": " << (bloom_wc_bytes / 1024.0)
       << ", \"cpu_us\": " << cb.median_val << ", \"gpu_med_ms\": " << gb.median_val << ", \"gpu_p95_ms\": " << gb.p95_val
       << ", \"fps\": " << (1000.0 / gb.median_val) << ", \"checksum\": " << chk_b << " }\n"
       << "  }";
    return js.str();
}

// ============================================================================
// Exp 3: 4,096 Meshlets (393k tris, ~60% cullable) — Brute vs Compute-Indirect vs Metal 3 Mesh Shader
// ============================================================================
static std::string bench_exp3_cull_modes(Ctx &ctx) {
    printf("\n=== [3/6] mesh_shader_vs_compute_cull_vs_brute: 4,096 Meshlets (393,216 tris) @ 4K ===\n");
    const uint32_t W = 3840, H = 2160;
    const uint32_t N_OBJ = 512u, N_MESH = 4096u; // 8 meshlets per object

    std::vector<BloomVtx12> bv(16u * CLUSTER_VTX);
    std::vector<BloomMeshletHeader32> hdrs(N_MESH);
    std::vector<uint8_t> loc_u8(CLUSTER_TRI * 3u);
    std::vector<uint16_t> loc_u16(CLUSTER_TRI * 3u);
    std::vector<BloomMeshletHeader32> base_h(16u);
    for (uint32_t m = 0; m < 16u; ++m) {
        build_torus_patch_cluster(m, 16u, 0u, bv.data(), nullptr, m == 0 ? loc_u8.data() : nullptr, nullptr, base_h[m], m * CLUSTER_VTX, 0u);
    }
    for (size_t k = 0; k < CLUSTER_TRI * 3u; ++k) loc_u16[k] = loc_u8[k];
    for (uint32_t i = 0; i < N_MESH; ++i) {
        hdrs[i] = base_h[i & 7u];
        hdrs[i].obj_id = static_cast<uint16_t>(i / 8u);
    }

    // Place 512 objects: ~45% visible inside frustum, ~55% outside/behind so realistic ~60% cluster culling occurs!
    id<MTLBuffer> wc_states = [ctx.dev newBufferWithLength:N_OBJ * sizeof(BloomObjectState48) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    auto *st = static_cast<BloomObjectState48 *>(wc_states.contents);
    for (uint32_t i = 0; i < N_OBJ; ++i) {
        BloomObjectState48 s{};
        bool offscreen = (i % 5u >= 2u); // 60% placed offscreen/wide
        s.pos[0] = offscreen ? (40.0f + static_cast<float>(i & 31u)) : ((static_cast<float>(i % 16u) - 7.5f) * 1.15f);
        s.pos[1] = offscreen ? 35.0f : ((static_cast<float>((i / 16u) % 16u) - 7.5f) * 0.9f);
        s.pos[2] = 10.0f + static_cast<float>(i & 7u) * 1.6f;
        fill_quat_y(static_cast<float>(i) * 0.25f, s.quat_f16);
        uint16_t sc = f32_to_f16_hw(0.55f);
        s.scale_f16[0] = sc; s.scale_f16[1] = sc; s.scale_f16[2] = sc;
        s.rgba8_mat = 0xFF40D8F0u;
        st[i] = s;
    }

    id<MTLBuffer> priv_bv  = upload_private(ctx, bv.data(), bv.size() * sizeof(BloomVtx12));
    id<MTLBuffer> priv_hdr = upload_private(ctx, hdrs.data(), hdrs.size() * sizeof(BloomMeshletHeader32));
    id<MTLBuffer> priv_i16 = upload_private(ctx, loc_u16.data(), loc_u16.size() * sizeof(uint16_t));
    id<MTLBuffer> priv_i8  = upload_private(ctx, loc_u8.data(), loc_u8.size());
    id<MTLBuffer> surv_ids = [ctx.dev newBufferWithLength:N_MESH * sizeof(uint32_t) options:MTLResourceStorageModePrivate];
    id<MTLBuffer> ind_args = [ctx.dev newBufferWithLength:32 options:MTLResourceStorageModeShared];

    id<MTLComputePipelineState> p_cull  = cpso(ctx, "k_cull_meshlets");
    id<MTLComputePipelineState> p_chk   = cpso(ctx, "k_tex_checksum");
    id<MTLRenderPipelineState>  p_brute = rpso(ctx, "v_meshlet_brute", "f_obj_lit", false);
    id<MTLRenderPipelineState>  p_indir = rpso(ctx, "v_meshlet_indirect", "f_obj_lit", false);
    std::string mesh_err;
    id<MTLRenderPipelineState>  p_mesh  = mesh_pso(ctx, "o_bloom_cull", "m_bloom_emit", "f_obj_lit", 32 * sizeof(uint32_t), mesh_err);
    bool mesh_ok = (p_mesh != nil);
    if (!mesh_ok) {
        fprintf(stderr, "Metal 3 Mesh Pipeline FAILED on Apple M1: %s\n", mesh_err.c_str());
        exit(1);
    }

    id<MTLTexture> rt  = tex2d(ctx.dev, W, H, MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
    id<MTLTexture> dep = tex2d(ctx.dev, W, H, MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);
    MTLDepthStencilDescriptor *dsd = [[MTLDepthStencilDescriptor alloc] init];
    dsd.depthCompareFunction = MTLCompareFunctionLess; dsd.depthWriteEnabled = YES;
    id<MTLDepthStencilState> ds_w = [ctx.dev newDepthStencilStateWithDescriptor:dsd];

    MeshletDrawParams pm{static_cast<float>(W), static_cast<float>(H), 0, 0, 0, 1.8f, 1.0f / 1.8f, N_MESH, 0, 0};
    std::vector<double> m_br, m_ci, m_ms;
    uint32_t chk_br = 0, chk_ci = 0, chk_ms = 0, surv_cnt = 0;

    for (int it = 0; it < 36; ++it) {
        // Mode 1: Brute
        {
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture = rt; rpd.colorAttachments[0].loadAction = MTLLoadActionClear; rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.depthAttachment.texture = dep; rpd.depthAttachment.loadAction = MTLLoadActionClear; rpd.depthAttachment.storeAction = MTLStoreActionDontCare; rpd.depthAttachment.clearDepth = 1.0;
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
            [enc setDepthStencilState:ds_w]; [enc setCullMode:MTLCullModeBack]; [enc setRenderPipelineState:p_brute];
            [enc setVertexBuffer:priv_hdr offset:0 atIndex:0]; [enc setVertexBuffer:priv_bv offset:0 atIndex:1];
            [enc setVertexBuffer:wc_states offset:0 atIndex:2]; [enc setVertexBytes:&pm length:sizeof(pm) atIndex:3];
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:CLUSTER_TRI * 3u indexType:MTLIndexTypeUInt16
                           indexBuffer:priv_i16 indexBufferOffset:0 instanceCount:N_MESH];
            [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
            if (it >= 6) m_br.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
            if (it == 35) chk_br = compute_tex_checksum(ctx, p_chk, rt);
        }
        // Mode 2: Compute Cull + Indirect Draw (Single CB)
        {
            uint32_t *ia = static_cast<uint32_t *>(ind_args.contents);
            ia[0] = CLUSTER_TRI * 3u; ia[1] = 0u; ia[2] = 0u; ia[3] = 0u; ia[4] = 0u;
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            id<MTLComputeCommandEncoder> ce = [cmd computeCommandEncoder];
            [ce setComputePipelineState:p_cull];
            [ce setBuffer:priv_hdr offset:0 atIndex:0]; [ce setBuffer:wc_states offset:0 atIndex:1];
            [ce setBytes:&pm length:sizeof(pm) atIndex:2]; [ce setBuffer:surv_ids offset:0 atIndex:3];
            [ce setBuffer:ind_args offset:0 atIndex:4];
            [ce dispatchThreadgroups:MTLSizeMake((N_MESH + 63u) / 64u, 1, 1) threadsPerThreadgroup:MTLSizeMake(64, 1, 1)];
            [ce endEncoding];

            MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture = rt; rpd.colorAttachments[0].loadAction = MTLLoadActionClear; rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.depthAttachment.texture = dep; rpd.depthAttachment.loadAction = MTLLoadActionClear; rpd.depthAttachment.storeAction = MTLStoreActionDontCare; rpd.depthAttachment.clearDepth = 1.0;
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
            [enc setDepthStencilState:ds_w]; [enc setCullMode:MTLCullModeBack]; [enc setRenderPipelineState:p_indir];
            [enc setVertexBuffer:surv_ids offset:0 atIndex:0]; [enc setVertexBuffer:priv_hdr offset:0 atIndex:1];
            [enc setVertexBuffer:priv_bv offset:0 atIndex:2]; [enc setVertexBuffer:wc_states offset:0 atIndex:3];
            [enc setVertexBytes:&pm length:sizeof(pm) atIndex:4];
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexType:MTLIndexTypeUInt16
                           indexBuffer:priv_i16 indexBufferOffset:0 indirectBuffer:ind_args indirectBufferOffset:0];
            [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
            surv_cnt = ia[1];
            if (it >= 6) m_ci.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
            if (it == 35) chk_ci = compute_tex_checksum(ctx, p_chk, rt);
        }
        // Mode 3: Metal 3 Object + Mesh Shader
        {
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture = rt; rpd.colorAttachments[0].loadAction = MTLLoadActionClear; rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.depthAttachment.texture = dep; rpd.depthAttachment.loadAction = MTLLoadActionClear; rpd.depthAttachment.storeAction = MTLStoreActionDontCare; rpd.depthAttachment.clearDepth = 1.0;
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
            [enc setDepthStencilState:ds_w]; [enc setCullMode:MTLCullModeBack]; [enc setRenderPipelineState:p_mesh];
            [enc setObjectBuffer:priv_hdr offset:0 atIndex:0]; [enc setObjectBuffer:wc_states offset:0 atIndex:1];
            [enc setObjectBytes:&pm length:sizeof(pm) atIndex:2];
            [enc setMeshBuffer:priv_hdr offset:0 atIndex:0]; [enc setMeshBuffer:priv_bv offset:0 atIndex:1];
            [enc setMeshBuffer:priv_i8 offset:0 atIndex:2]; [enc setMeshBuffer:wc_states offset:0 atIndex:3];
            [enc setMeshBytes:&pm length:sizeof(pm) atIndex:4];
            [enc drawMeshThreadgroups:MTLSizeMake((N_MESH + 31u) / 32u, 1, 1)
             threadsPerObjectThreadgroup:MTLSizeMake(32, 1, 1)
               threadsPerMeshThreadgroup:MTLSizeMake(96, 1, 1)];
            [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
            if (it >= 6) m_ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
            if (it == 35) chk_ms = compute_tex_checksum(ctx, p_chk, rt);
        }
    }

    if (chk_br == 0 || chk_ci == 0 || chk_ms == 0 || chk_br != chk_ci || chk_ms != chk_ci) {
        fprintf(stderr, "Exp3 checksum mismatch/zero (0x%08X, 0x%08X, 0x%08X)\n", chk_br, chk_ci, chk_ms);
        exit(1);
    }
    Stats sbr = compute_stats(m_br), sci = compute_stats(m_ci), sms = compute_stats(m_ms);
    double cull_pct = 100.0 * static_cast<double>(N_MESH - surv_cnt) / static_cast<double>(N_MESH);
    printf("  Culled=%u / %u (%.1f%% culled, %u surviving)\n", N_MESH - surv_cnt, N_MESH, cull_pct, surv_cnt);
    printf("  Brute Draw 4096      | med=%6.3f ms (p95=%6.3f ms, %6.1f fps) | chk=0x%08X\n", sbr.median_val, sbr.p95_val, 1000.0 / sbr.median_val, chk_br);
    printf("  ComputeCull+Indirect | med=%6.3f ms (p95=%6.3f ms, %6.1f fps) | chk=0x%08X\n", sci.median_val, sci.p95_val, 1000.0 / sci.median_val, chk_ci);
    printf("  Metal3 Object+Mesh   | med=%6.3f ms (p95=%6.3f ms, %6.1f fps) | chk=0x%08X\n", sms.median_val, sms.p95_val, 1000.0 / sms.median_val, chk_ms);

    std::ostringstream js;
    js << "  \"mesh_shader_vs_compute_cull_vs_brute\": {\n"
       << "    \"total_meshlets\": " << N_MESH << ", \"surviving_meshlets\": " << surv_cnt << ", \"culled_pct\": " << cull_pct << ",\n"
       << "    \"metal3_mesh_pso_ok\": " << (mesh_ok ? "true" : "false") << ",\n"
       << "    \"brute\": { \"med_ms\": " << sbr.median_val << ", \"p95_ms\": " << sbr.p95_val << ", \"fps\": " << (1000.0 / sbr.median_val) << ", \"checksum\": " << chk_br << " },\n"
       << "    \"compute_indirect\": { \"med_ms\": " << sci.median_val << ", \"p95_ms\": " << sci.p95_val << ", \"fps\": " << (1000.0 / sci.median_val) << ", \"checksum\": " << chk_ci << " },\n"
       << "    \"metal3_mesh\": { \"med_ms\": " << sms.median_val << ", \"p95_ms\": " << sms.p95_val << ", \"fps\": " << (1000.0 / sms.median_val) << ", \"checksum\": " << chk_ms << " }\n"
       << "  }";
    return js.str();
}

// ============================================================================
// Exp 4: Tight-Proxy Analytic Spheres vs 512-Tri Tessellated vs 48-Step Raymarch @ 4K
// ============================================================================
static std::string bench_exp4_tight_proxy(Ctx &ctx) {
    printf("\n=== [4/6] tight_proxy_analytic_vs_tessellated_vs_raymarch: 256 Precision Spheres @ 4K ===\n");
    const uint32_t W = 3840, H = 2160;
    const uint32_t N_PRIM = 256u;

    id<MTLBuffer> wc_prims  = [ctx.dev newBufferWithLength:N_PRIM * sizeof(BloomAnalyticPrim32) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> wc_states = [ctx.dev newBufferWithLength:N_PRIM * sizeof(BloomObjectState48) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    auto *pr = static_cast<BloomAnalyticPrim32 *>(wc_prims.contents);
    auto *st = static_cast<BloomObjectState48 *>(wc_states.contents);
    for (uint32_t i = 0; i < N_PRIM; ++i) {
        float px = (static_cast<float>(i % 16u) - 7.5f) * 1.15f;
        float py = (static_cast<float>(i / 16u) - 7.5f) * 0.65f;
        float pz = 11.0f + static_cast<float>(i & 3u) * 0.8f;
        pr[i] = {{px, py, pz}, 0.42f, {0,0,0,0}, 0xFF38D0FFu, 1u};
        BloomObjectState48 s{};
        s.pos[0] = px; s.pos[1] = py; s.pos[2] = pz;
        fill_quat_y(0.0f, s.quat_f16);
        uint16_t sc = f32_to_f16_hw(0.42f);
        s.scale_f16[0] = sc; s.scale_f16[1] = sc; s.scale_f16[2] = sc;
        s.rgba8_mat = 0xFF38D0FFu;
        st[i] = s;
    }

    // Build a 512-tri UV sphere (16 rings x 16 segs, ~289 verts) for Baseline B
    std::vector<BloomVtx12> sph_vtx(289u);
    std::vector<uint16_t> sph_idx(512u * 3u);
    for (uint32_t y = 0; y <= 16u; ++y) {
        float phi = (static_cast<float>(y) / 16.0f) * 3.14159265f;
        for (uint32_t x = 0; x <= 16u; ++x) {
            float th = (static_cast<float>(x) / 16.0f) * 2.0f * 3.14159265f;
            float nx = std::sin(phi) * std::cos(th);
            float ny = std::cos(phi);
            float nz = std::sin(phi) * std::sin(th);
            BloomVtx12 &v = sph_vtx[y * 17u + x];
            v.pos_unorm16[0] = pack_pos_unorm16(nx, LOCAL_BOX_LO, LOCAL_BOX_SPAN);
            v.pos_unorm16[1] = pack_pos_unorm16(ny, LOCAL_BOX_LO, LOCAL_BOX_SPAN);
            v.pos_unorm16[2] = pack_pos_unorm16(nz, LOCAL_BOX_LO, LOCAL_BOX_SPAN);
            oct_encode(nx, ny, nz, v.oct_norm);
        }
    }
    uint32_t si = 0;
    for (uint32_t y = 0; y < 16u; ++y) {
        for (uint32_t x = 0; x < 16u; ++x) {
            uint16_t a = static_cast<uint16_t>(y * 17u + x);
            uint16_t b = a + 17u;
            sph_idx[si++] = a; sph_idx[si++] = b; sph_idx[si++] = a + 1u;
            sph_idx[si++] = a + 1u; sph_idx[si++] = b; sph_idx[si++] = b + 1u;
        }
    }
    //Note: v_bloom_vtx12_state48 uses iid * 64u vertex indexing; for 289-vtx sphere reuse v_meshlet_brute with shared hdr!
    // Actually upload 256 headers pointing to offset 0 of priv_sv or modify hdr offset:
    std::vector<BloomMeshletHeader32> sph_hdrs(N_PRIM);
    for (uint32_t i = 0; i < N_PRIM; ++i) { sph_hdrs[i].vtx_offset = 0u; sph_hdrs[i].obj_id = static_cast<uint16_t>(i); }
    // Wait: v_meshlet_brute does (vid % 64u); make a clean indexed draw using vertex offset 0 without %64 wrap?
    // Even simpler: copy sph_vtx once and run drawIndexedPrimitives across 256 copies of v_tess512? Look: in v_meshlet_brute `vid % 64u` wraps >64! Let's check:
    // Actually `sph_vtx` has 289 verts -> 512 tris = 256 quads; better yet, 8 separate meshlets per sphere (8 x 64 tris = 512 tris)! Look how authentic that is: 8 x 64-tri patches = 512 tris/sphere * 256 = 2048 meshlets!
    std::vector<BloomVtx12> patch_v(8u * CLUSTER_VTX);
    std::vector<uint8_t> ploc(64u * 3u);
    std::vector<uint16_t> pidx(64u * 3u);
    std::vector<BloomMeshletHeader32> phdrs(N_PRIM * 8u);
    for (uint32_t p = 0; p < 8u; ++p) {
        // pack sphere patches 8x8 vertices (64 tris out of 98)
        float th0 = (static_cast<float>(p & 3u) / 4.0f) * 2.0f * 3.14159265f;
        float th1 = (static_cast<float>((p & 3u) + 1u) / 4.0f) * 2.0f * 3.14159265f;
        float ph0 = (p >= 4u ? 0.5f : 0.0f) * 3.14159265f;
        float ph1 = (p >= 4u ? 1.0f : 0.5f) * 3.14159265f;
        for (uint32_t iy = 0; iy < 8u; ++iy) {
            float phi = ph0 + (static_cast<float>(iy) / 7.0f) * (ph1 - ph0);
            for (uint32_t ix = 0; ix < 8u; ++ix) {
                float th = th0 + (static_cast<float>(ix) / 7.0f) * (th1 - th0);
                float nx = std::sin(phi) * std::cos(th), ny = std::cos(phi), nz = std::sin(phi) * std::sin(th);
                BloomVtx12 &v = patch_v[p * CLUSTER_VTX + iy * 8u + ix];
                v.pos_unorm16[0] = pack_pos_unorm16(nx, LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                v.pos_unorm16[1] = pack_pos_unorm16(ny, LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                v.pos_unorm16[2] = pack_pos_unorm16(nz, LOCAL_BOX_LO, LOCAL_BOX_SPAN);
                oct_encode(nx, ny, nz, v.oct_norm);
            }
        }
    }
    uint32_t pt = 0;
    for (uint32_t iy = 0; iy < 7u && pt < 64u; ++iy) {
        for (uint32_t ix = 0; ix < 7u && pt < 64u; ++ix) {
            uint16_t a = static_cast<uint16_t>(iy * 8u + ix);
            pidx[pt * 3u + 0u] = a; pidx[pt * 3u + 1u] = a + 8u; pidx[pt * 3u + 2u] = a + 1u; ++pt;
            if (pt < 64u) { pidx[pt * 3u + 0u] = a + 1u; pidx[pt * 3u + 1u] = a + 8u; pidx[pt * 3u + 2u] = a + 9u; ++pt; }
        }
    }
    for (uint32_t i = 0; i < N_PRIM; ++i) {
        for (uint32_t p = 0; p < 8u; ++p) {
            phdrs[i * 8u + p].vtx_offset = p * CLUSTER_VTX;
            phdrs[i * 8u + p].obj_id = static_cast<uint16_t>(i);
        }
    }

    id<MTLBuffer> priv_pv   = upload_private(ctx, patch_v.data(), patch_v.size() * sizeof(BloomVtx12));
    id<MTLBuffer> priv_phdr = upload_private(ctx, phdrs.data(), phdrs.size() * sizeof(BloomMeshletHeader32));
    id<MTLBuffer> priv_pidx = upload_private(ctx, pidx.data(), pidx.size() * sizeof(uint16_t));

    id<MTLComputePipelineState> p_rm  = cpso(ctx, "k_raymarch_48step");
    id<MTLComputePipelineState> p_chk = cpso(ctx, "k_tex_checksum");
    id<MTLRenderPipelineState>  p_msh = rpso(ctx, "v_meshlet_brute", "f_obj_lit", false);
    id<MTLRenderPipelineState>  p_prx = rpso(ctx, "v_tight_proxy_analytic", "f_tight_proxy_analytic", false);
    id<MTLRenderPipelineState>  p_prxz= rpso(ctx, "v_tight_proxy_analytic", "f_tight_proxy_exact_depth", false);

    id<MTLTexture> comp_rt = tex2d(ctx.dev, W, H, MTLPixelFormatBGRA8Unorm, MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
    id<MTLTexture> rt      = tex2d(ctx.dev, W, H, MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
    id<MTLTexture> dep     = tex2d(ctx.dev, W, H, MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);
    MTLDepthStencilDescriptor *dwd = [[MTLDepthStencilDescriptor alloc] init];
    dwd.depthCompareFunction = MTLCompareFunctionLess; dwd.depthWriteEnabled = YES;
    id<MTLDepthStencilState> ds_w = [ctx.dev newDepthStencilStateWithDescriptor:dwd];
    MTLDepthStencilDescriptor *dtd = [[MTLDepthStencilDescriptor alloc] init];
    dtd.depthCompareFunction = MTLCompareFunctionLessEqual; dtd.depthWriteEnabled = NO;
    id<MTLDepthStencilState> ds_ro = [ctx.dev newDepthStencilStateWithDescriptor:dtd];

    RaymarchTileParams rp{N_PRIM, W, H, 1.8f, 0, 0, 0, 0};
    MeshletDrawParams pm{static_cast<float>(W), static_cast<float>(H), 0, 0, 0, 1.8f, 0.56f, N_PRIM * 8u, 0, 0};

    std::vector<double> m_rm, m_ms, m_px, m_pz;
    uint32_t c_rm = 0, c_ms = 0, c_px = 0, c_pz = 0;

    for (int it = 0; it < 32; ++it) {
        // 1. 48-step Raymarch Compute
        {
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            id<MTLComputeCommandEncoder> ce = [cmd computeCommandEncoder];
            [ce setComputePipelineState:p_rm];
            [ce setTexture:comp_rt atIndex:0];
            [ce setBuffer:wc_prims offset:0 atIndex:0]; [ce setBytes:&rp length:sizeof(rp) atIndex:1];
            [ce dispatchThreadgroups:MTLSizeMake((W + 15u) / 16u, (H + 15u) / 16u, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
            [ce endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
            if (it >= 4) m_rm.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
            if (it == 31) c_rm = compute_tex_checksum(ctx, p_chk, comp_rt);
        }
        auto draw_tbdr = [&](int mode) -> double {
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture = rt; rpd.colorAttachments[0].loadAction = MTLLoadActionClear; rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.depthAttachment.texture = dep; rpd.depthAttachment.loadAction = MTLLoadActionClear; rpd.depthAttachment.storeAction = MTLStoreActionDontCare; rpd.depthAttachment.clearDepth = 1.0;
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
            if (mode == 0) {
                [enc setDepthStencilState:ds_w]; [enc setCullMode:MTLCullModeBack]; [enc setRenderPipelineState:p_msh];
                [enc setVertexBuffer:priv_phdr offset:0 atIndex:0]; [enc setVertexBuffer:priv_pv offset:0 atIndex:1];
                [enc setVertexBuffer:wc_states offset:0 atIndex:2]; [enc setVertexBytes:&pm length:sizeof(pm) atIndex:3];
                [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:64u * 3u indexType:MTLIndexTypeUInt16
                               indexBuffer:priv_pidx indexBufferOffset:0 instanceCount:N_PRIM * 8u];
            } else {
                [enc setDepthStencilState:(mode == 1 ? ds_ro : ds_w)];
                [enc setRenderPipelineState:(mode == 1 ? p_prx : p_prxz)];
                [enc setVertexBuffer:wc_prims offset:0 atIndex:0]; [enc setVertexBytes:&pm length:sizeof(pm) atIndex:1];
                [enc setFragmentBuffer:wc_prims offset:0 atIndex:0]; [enc setFragmentBytes:&pm length:sizeof(pm) atIndex:1];
                [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N_PRIM];
            }
            [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
            return (cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0;
        };
        double d1 = draw_tbdr(0); if (it == 31) c_ms = compute_tex_checksum(ctx, p_chk, rt);
        double d2 = draw_tbdr(1); if (it == 31) c_px = compute_tex_checksum(ctx, p_chk, rt);
        double d3 = draw_tbdr(2); if (it == 31) c_pz = compute_tex_checksum(ctx, p_chk, rt);
        if (it >= 4) { m_ms.push_back(d1); m_px.push_back(d2); m_pz.push_back(d3); }
    }

    if (c_rm == 0 || c_ms == 0 || c_px == 0 || c_pz == 0) { fprintf(stderr, "Exp4 zero checksum!\n"); exit(1); }
    Stats srm = compute_stats(m_rm), sms = compute_stats(m_ms), spx = compute_stats(m_px), spz = compute_stats(m_pz);
    size_t tess_vram = patch_v.size() * sizeof(BloomVtx12) + phdrs.size() * sizeof(BloomMeshletHeader32) + pidx.size() * 2u;
    printf("  48-Step Compute Raymarch | med=%6.3f ms (%6.1f fps) | vram=8 KB prims   | chk=0x%08X\n", srm.median_val, 1000.0 / srm.median_val, c_rm);
    printf("  512-Tri Tessellated Mesh | med=%6.3f ms (%6.1f fps) | vram=%5.1f KB mesh | chk=0x%08X\n", sms.median_val, 1000.0 / sms.median_val, tess_vram / 1024.0, c_ms);
    printf("  Tight-Proxy (no z-write) | med=%6.3f ms (%6.1f fps) | vram=0 mesh (8 KB) | chk=0x%08X (%.1fx vs raymarch)\n", spx.median_val, 1000.0 / spx.median_val, c_px, srm.median_val / spx.median_val);
    printf("  Tight-Proxy (exact depth)| med=%6.3f ms (%6.1f fps) | vram=0 mesh (8 KB) | chk=0x%08X\n", spz.median_val, 1000.0 / spz.median_val, c_pz);

    std::ostringstream js;
    js << "  \"tight_proxy_analytic_vs_tessellated_vs_raymarch\": {\n"
       << "    \"primitives\": " << N_PRIM << ",\n"
       << "    \"raymarch_48step\": { \"med_ms\": " << srm.median_val << ", \"p95_ms\": " << srm.p95_val << ", \"fps\": " << (1000.0 / srm.median_val) << ", \"checksum\": " << c_rm << " },\n"
       << "    \"tess_512tri\": { \"med_ms\": " << sms.median_val << ", \"p95_ms\": " << sms.p95_val << ", \"fps\": " << (1000.0 / sms.median_val) << ", \"mesh_kb\": " << (tess_vram / 1024.0) << ", \"checksum\": " << c_ms << " },\n"
       << "    \"tight_proxy_fast\": { \"med_ms\": " << spx.median_val << ", \"p95_ms\": " << spx.p95_val << ", \"fps\": " << (1000.0 / spx.median_val) << ", \"checksum\": " << c_px << " },\n"
       << "    \"tight_proxy_exact_depth\": { \"med_ms\": " << spz.median_val << ", \"p95_ms\": " << spz.p95_val << ", \"fps\": " << (1000.0 / spz.median_val) << ", \"checksum\": " << c_pz << " }\n"
       << "  }";
    return js.str();
}

// ============================================================================
// Exp 5: X-Ray Cutaway — No Cut vs Hardware [[clip_distance]] vs fragment discard() @ 4K
// ============================================================================
static std::string bench_exp5_cutaway(Ctx &ctx) {
    printf("\n=== [5/6] cutaway_clip_distance_vs_discard: 2,048 Overlapping Objects @ 4K ===\n");
    const uint32_t W = 3840, H = 2160;
    const uint32_t N_OBJ = 2048u;

    std::vector<BloomVtx12> bv(N_OBJ * CLUSTER_VTX);
    std::vector<uint8_t> loc8(CLUSTER_TRI * 3u);
    std::vector<uint16_t> loc16(CLUSTER_TRI * 3u);
    BloomMeshletHeader32 dummy{};
    for (uint32_t i = 0; i < N_OBJ; ++i) {
        build_torus_patch_cluster(i & 15u, 16u, static_cast<uint16_t>(i), bv.data(), nullptr, i == 0 ? loc8.data() : nullptr, nullptr, dummy, i * CLUSTER_VTX, 0u);
    }
    for (size_t k = 0; k < CLUSTER_TRI * 3u; ++k) loc16[k] = loc8[k];

    id<MTLBuffer> priv_bv  = upload_private(ctx, bv.data(), bv.size() * sizeof(BloomVtx12));
    id<MTLBuffer> priv_i16 = upload_private(ctx, loc16.data(), loc16.size() * sizeof(uint16_t));
    id<MTLBuffer> wc_st    = [ctx.dev newBufferWithLength:N_OBJ * sizeof(BloomObjectState48) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    auto *st = static_cast<BloomObjectState48 *>(wc_st.contents);
    uint16_t sc = f32_to_f16_hw(1.35f);
    for (uint32_t i = 0; i < N_OBJ; ++i) {
        BloomObjectState48 s{};
        // Dense overlapping 8x4 clusters x 64 layers back-to-front so HSR sees ~4x-8x overdraw
        s.pos[0] = (static_cast<float>(i % 8u) - 3.5f) * 1.1f;
        s.pos[1] = (static_cast<float>((i / 8u) % 4u) - 1.5f) * 1.1f;
        s.pos[2] = 16.0f - static_cast<float>(i / 32u) * 0.14f; // back-to-front worst case for discard!
        fill_quat_y(0.4f, s.quat_f16);
        s.scale_f16[0] = sc; s.scale_f16[1] = sc; s.scale_f16[2] = sc;
        s.rgba8_mat = 0xFF40D890u;
        s.clip_plane_f16[0] = f32_to_f16_hw(0.7071f);
        s.clip_plane_f16[1] = f32_to_f16_hw(0.0f);
        s.clip_plane_f16[2] = f32_to_f16_hw(0.7071f);
        s.clip_plane_f16[3] = f32_to_f16_hw(0.0f);
        s.flags = 1u;
        st[i] = s;
    }

    id<MTLRenderPipelineState> p_base = rpso(ctx, "v_bloom_vtx12_state48", "f_obj_lit", false);
    id<MTLRenderPipelineState> p_clip = rpso(ctx, "v_bloom_cutaway_clip", "f_bloom_cutaway_clip", false);
    id<MTLRenderPipelineState> p_disc = rpso(ctx, "v_bloom_cutaway_disc", "f_bloom_cutaway_disc", false);
    id<MTLComputePipelineState> p_chk = cpso(ctx, "k_tex_checksum");
    id<MTLTexture> rt  = tex2d(ctx.dev, W, H, MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
    id<MTLTexture> dep = tex2d(ctx.dev, W, H, MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);
    MTLDepthStencilDescriptor *dwd = [[MTLDepthStencilDescriptor alloc] init];
    dwd.depthCompareFunction = MTLCompareFunctionLess; dwd.depthWriteEnabled = YES;
    id<MTLDepthStencilState> ds_w = [ctx.dev newDepthStencilStateWithDescriptor:dwd];
    MeshletDrawParams pm{static_cast<float>(W), static_cast<float>(H), 0, 0, 0, 1.8f, 0.56f, N_OBJ, 0, 0};

    std::vector<double> m0, m1, m2;
    uint32_t c0 = 0, c1 = 0, c2 = 0;
    for (int it = 0; it < 30; ++it) {
        auto run = [&](id<MTLRenderPipelineState> pso) -> double {
            id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
            MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            rpd.colorAttachments[0].texture = rt; rpd.colorAttachments[0].loadAction = MTLLoadActionClear; rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
            rpd.depthAttachment.texture = dep; rpd.depthAttachment.loadAction = MTLLoadActionClear; rpd.depthAttachment.storeAction = MTLStoreActionDontCare; rpd.depthAttachment.clearDepth = 1.0;
            id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
            [enc setDepthStencilState:ds_w]; [enc setCullMode:MTLCullModeBack]; [enc setRenderPipelineState:pso];
            [enc setVertexBuffer:priv_bv offset:0 atIndex:0]; [enc setVertexBuffer:wc_st offset:0 atIndex:1]; [enc setVertexBytes:&pm length:sizeof(pm) atIndex:2];
            [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:CLUSTER_TRI * 3u indexType:MTLIndexTypeUInt16
                           indexBuffer:priv_i16 indexBufferOffset:0 instanceCount:N_OBJ];
            [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
            return (cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0;
        };
        double d0 = run(p_base); if (it == 29) c0 = compute_tex_checksum(ctx, p_chk, rt);
        double d1 = run(p_clip); if (it == 29) c1 = compute_tex_checksum(ctx, p_chk, rt);
        double d2 = run(p_disc); if (it == 29) c2 = compute_tex_checksum(ctx, p_chk, rt);
        if (it >= 4) { m0.push_back(d0); m1.push_back(d1); m2.push_back(d2); }
    }
    if (c0 == 0 || c1 == 0 || c2 == 0) { fprintf(stderr, "Exp5 zero checksum!\n"); exit(1); }
    Stats s0 = compute_stats(m0), s1 = compute_stats(m1), s2 = compute_stats(m2);
    printf("  No Cutaway (Full HSR)    | med=%6.3f ms (%6.1f fps) | chk=0x%08X\n", s0.median_val, 1000.0 / s0.median_val, c0);
    printf("  HW [[clip_distance]]     | med=%6.3f ms (%6.1f fps) | chk=0x%08X (preserves HSR!)\n", s1.median_val, 1000.0 / s1.median_val, c1);
    printf("  Fragment discard()       | med=%6.3f ms (%6.1f fps) | chk=0x%08X (%.2fx slower vs clip_distance)\n", s2.median_val, 1000.0 / s2.median_val, c2, s2.median_val / s1.median_val);

    std::ostringstream js;
    js << "  \"cutaway_clip_distance_vs_discard\": {\n"
       << "    \"objects\": " << N_OBJ << ",\n"
       << "    \"no_cutaway\": { \"med_ms\": " << s0.median_val << ", \"p95_ms\": " << s0.p95_val << ", \"fps\": " << (1000.0 / s0.median_val) << ", \"checksum\": " << c0 << " },\n"
       << "    \"hw_clip_distance\": { \"med_ms\": " << s1.median_val << ", \"p95_ms\": " << s1.p95_val << ", \"fps\": " << (1000.0 / s1.median_val) << ", \"checksum\": " << c1 << " },\n"
       << "    \"fragment_discard\": { \"med_ms\": " << s2.median_val << ", \"p95_ms\": " << s2.p95_val << ", \"fps\": " << (1000.0 / s2.median_val) << ", \"checksum\": " << c2 << " }\n"
       << "  }";
    return js.str();
}

// ============================================================================
// Exp 6: End-to-End 4K Single-CB Composite (1,024 Mesh3D + 64 Analytic + Full SVG HUD)
// ============================================================================
static std::string bench_exp6_composite(Ctx &ctx) {
    printf("\n=== [6/6] full_4k_3d_plus_svg_hud_composite: Single-CB 4K 3D + SVG HUD ===\n");
    const uint32_t W = 3840, H = 2160;
    const uint32_t N_3D = 1024u, N_PRX = 64u, N_OPQ = 35u, N_ALP = 195u, N_SH = 24u, N_GL = 12000u;

    std::vector<BloomVtx12> bv(N_3D * CLUSTER_VTX);
    std::vector<uint8_t> loc8(CLUSTER_TRI * 3u);
    std::vector<uint16_t> loc16(CLUSTER_TRI * 3u);
    BloomMeshletHeader32 dummy{};
    for (uint32_t i = 0; i < N_3D; ++i) {
        build_torus_patch_cluster(i & 15u, 16u, static_cast<uint16_t>(i), bv.data(), nullptr, i == 0 ? loc8.data() : nullptr, nullptr, dummy, i * CLUSTER_VTX, 0u);
    }
    for (size_t k = 0; k < CLUSTER_TRI * 3u; ++k) loc16[k] = loc8[k];

    id<MTLBuffer> priv_bv  = upload_private(ctx, bv.data(), bv.size() * sizeof(BloomVtx12));
    id<MTLBuffer> priv_i16 = upload_private(ctx, loc16.data(), loc16.size() * sizeof(uint16_t));

    id<MTLBuffer> wc_st  = [ctx.dev newBufferWithLength:N_3D * sizeof(BloomObjectState48) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> wc_prx = [ctx.dev newBufferWithLength:N_PRX * sizeof(BloomAnalyticPrim32) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> wc_opq = [ctx.dev newBufferWithLength:N_OPQ * sizeof(BloomQuadHot) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> wc_alp = [ctx.dev newBufferWithLength:N_ALP * sizeof(BloomQuadHot) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> wc_sh  = [ctx.dev newBufferWithLength:N_SH * sizeof(BloomShadowHot) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> wc_gl  = [ctx.dev newBufferWithLength:N_GL * sizeof(BloomGlyphInst) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    id<MTLBuffer> wc_clp = [ctx.dev newBufferWithLength:16 * sizeof(BloomClipRect) options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
    static_cast<BloomClipRect *>(wc_clp.contents)[0] = {0.0f, 0.0f, static_cast<float>(W), static_cast<float>(H)};

    size_t total_wc_bytes = N_3D * sizeof(BloomObjectState48) + N_PRX * sizeof(BloomAnalyticPrim32) +
                            (N_OPQ + N_ALP) * sizeof(BloomQuadHot) + N_SH * sizeof(BloomShadowHot) +
                            N_GL * sizeof(BloomGlyphInst);

    id<MTLTexture> atlas_r8 = tex2d(ctx.dev, 1024, 1024, MTLPixelFormatR8Unorm, MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
    {
        std::vector<uint8_t> ink(1024 * 1024, 200u);
        id<MTLBuffer> stg = [ctx.dev newBufferWithBytes:ink.data() length:ink.size() options:MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined];
        id<MTLCommandBuffer> cb = [ctx.q commandBuffer];
        id<MTLBlitCommandEncoder> bl = [cb blitCommandEncoder];
        [bl copyFromBuffer:stg sourceOffset:0 sourceBytesPerRow:1024 sourceBytesPerImage:ink.size()
                sourceSize:MTLSizeMake(1024, 1024, 1) toTexture:atlas_r8 destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
        [bl endEncoding]; [cb commit]; [cb waitUntilCompleted];
    }

    id<MTLRenderPipelineState> p_3d    = rpso(ctx, "v_bloom_cutaway_clip", "f_bloom_cutaway_clip", false);
    id<MTLRenderPipelineState> p_prx   = rpso(ctx, "v_tight_proxy_analytic", "f_tight_proxy_analytic", false);
    id<MTLRenderPipelineState> p_opq   = rpso(ctx, "v_hud_opaque", "f_hud_opaque", false);
    id<MTLRenderPipelineState> p_sh    = rpso(ctx, "v_hud_shadow", "f_hud_shadow", true);
    id<MTLRenderPipelineState> p_alp   = rpso(ctx, "v_hud_alpha", "f_hud_alpha", true);
    id<MTLRenderPipelineState> p_gl    = rpso(ctx, "v_hud_glyph", "f_hud_glyph", true);
    id<MTLComputePipelineState> p_chk  = cpso(ctx, "k_tex_checksum");

    id<MTLTexture> rt  = tex2d(ctx.dev, W, H, MTLPixelFormatBGRA8Unorm, MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead, MTLStorageModePrivate, true);
    id<MTLTexture> dep = tex2d(ctx.dev, W, H, MTLPixelFormatDepth32Float, MTLTextureUsageRenderTarget, MTLStorageModeMemoryless, true);
    MTLDepthStencilDescriptor *dwd = [[MTLDepthStencilDescriptor alloc] init];
    dwd.depthCompareFunction = MTLCompareFunctionLess; dwd.depthWriteEnabled = YES;
    id<MTLDepthStencilState> ds_w = [ctx.dev newDepthStencilStateWithDescriptor:dwd];
    MTLDepthStencilDescriptor *dtd = [[MTLDepthStencilDescriptor alloc] init];
    dtd.depthCompareFunction = MTLCompareFunctionLessEqual; dtd.depthWriteEnabled = NO;
    id<MTLDepthStencilState> ds_ro = [ctx.dev newDepthStencilStateWithDescriptor:dtd];

    float vp[2] = {static_cast<float>(W), static_cast<float>(H)};
    MeshletDrawParams pm{static_cast<float>(W), static_cast<float>(H), 0, 0, 0, 1.8f, 0.56f, N_3D, 0, 0};

    std::vector<double> cpu_us, gpu_ms;
    uint32_t chk = 0;

    for (int it = 0; it < 36; ++it) {
        float phase = static_cast<float>(it) * 0.04f;
        uint64_t t0 = now_ns();
        auto *st = static_cast<BloomObjectState48 *>(wc_st.contents);
        uint16_t sc = f32_to_f16_hw(0.32f);
        for (uint32_t i = 0; i < N_3D; ++i) {
            BloomObjectState48 s{};
            s.pos[0] = (static_cast<float>(i % 32u) - 15.5f) * 0.45f;
            s.pos[1] = (static_cast<float>(i / 32u) - 15.5f) * 0.35f;
            s.pos[2] = 10.0f + static_cast<float>(i & 7u) * 1.1f;
            fill_quat_y(phase + i * 0.02f, s.quat_f16);
            s.scale_f16[0] = sc; s.scale_f16[1] = sc; s.scale_f16[2] = sc;
            s.rgba8_mat = 0xFF38BDF8u;
            s.clip_plane_f16[0] = f32_to_f16_hw(0.7071f);
            s.clip_plane_f16[2] = f32_to_f16_hw(0.7071f);
            s.flags = (i & 3u) == 0u ? 1u : 0u;
            st[i] = s;
        }
        auto *pr = static_cast<BloomAnalyticPrim32 *>(wc_prx.contents);
        for (uint32_t i = 0; i < N_PRX; ++i) {
            pr[i] = {{(static_cast<float>(i % 8u) - 3.5f) * 1.2f, (static_cast<float>(i / 8u) - 3.5f) * 0.9f, 9.5f},
                     0.36f, {0,0,0,0}, 0xFFFFB020u, 1u};
        }
        auto *qo = static_cast<BloomQuadHot *>(wc_opq.contents);
        for (uint32_t i = 0; i < N_OPQ; ++i) {
            qo[i] = {{pack_q14_2(32.0f), pack_q14_2(120.0f + i * 54.0f), pack_q14_2(760.0f), pack_q14_2(48.0f)},
                     0xFF18202Cu, 0, {0,0,0,0}, {0,0,0,0}, 0, 1, 2u + i};
        }
        auto *qa = static_cast<BloomQuadHot *>(wc_alp.contents);
        uint8_t r8q = pack_q6_2(8.0f), b1q = pack_q6_2(1.5f);
        for (uint32_t i = 0; i < N_ALP; ++i) {
            qa[i] = {{pack_q14_2(1500.0f + (i % 8u) * 280.0f), pack_q14_2(120.0f + (i / 8u) * 80.0f), pack_q14_2(260.0f), pack_q14_2(70.0f)},
                     0xD820344Cu, 0xFF38BDF8u, {r8q,r8q,r8q,r8q}, {b1q,b1q,b1q,b1q}, 0, 1, 50u + i};
        }
        auto *sh = static_cast<BloomShadowHot *>(wc_sh.contents);
        for (uint32_t i = 0; i < N_SH; ++i) {
            sh[i] = {{pack_q14_2(1500.0f + (i % 8u) * 280.0f), pack_q14_2(120.0f + (i / 8u) * 80.0f), pack_q14_2(300.0f), pack_q14_2(110.0f)},
                     {f32_to_f16_hw(130.0f), f32_to_f16_hw(35.0f), f32_to_f16_hw(0.0589f), f32_to_f16_hw(70.0f)},
                     {f32_to_f16_hw(30.0f), f32_to_f16_hw(3.2f), f32_to_f16_hw(0.5f), f32_to_f16_hw(8.0f)},
                     0x59000000u, 0, static_cast<uint16_t>(40u + i)};
        }
        auto *gl = static_cast<BloomGlyphInst *>(wc_gl.contents);
        for (uint32_t i = 0; i < N_GL; ++i) {
            gl[i] = {{pack_q14_2(1520.0f + (i % 150u) * 14.0f), pack_q14_2(140.0f + ((i / 150u) % 68u) * 26.0f), 48u, 96u},
                     {static_cast<uint16_t>((i & 63u) * 896u), 0, 768u, 1536u}, 0xFFE8F4FFu, 0, 300u};
        }
        uint64_t t1 = now_ns();

        id<MTLCommandBuffer> cmd = [ctx.q commandBuffer];
        MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
        rpd.colorAttachments[0].texture = rt; rpd.colorAttachments[0].loadAction = MTLLoadActionClear; rpd.colorAttachments[0].clearColor = MTLClearColorMake(0.04, 0.05, 0.09, 1.0); rpd.colorAttachments[0].storeAction = MTLStoreActionStore;
        rpd.depthAttachment.texture = dep; rpd.depthAttachment.loadAction = MTLLoadActionClear; rpd.depthAttachment.storeAction = MTLStoreActionDontCare; rpd.depthAttachment.clearDepth = 1.0;

        id<MTLRenderCommandEncoder> enc = [cmd renderCommandEncoderWithDescriptor:rpd];
        // 1. Opaque HUD sidebar first (Early-Z rejects 3D hidden behind HUD panels!)
        [enc setDepthStencilState:ds_w]; [enc setCullMode:MTLCullModeNone]; [enc setRenderPipelineState:p_opq];
        [enc setVertexBuffer:wc_opq offset:0 atIndex:0]; [enc setVertexBuffer:wc_clp offset:0 atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N_OPQ];

        // 2. 3D Scene (BloomVtx12 + State48 + [[clip_distance]] + Tight-Proxy Spheres)
        [enc setCullMode:MTLCullModeBack]; [enc setRenderPipelineState:p_3d];
        [enc setVertexBuffer:priv_bv offset:0 atIndex:0]; [enc setVertexBuffer:wc_st offset:0 atIndex:1]; [enc setVertexBytes:&pm length:sizeof(pm) atIndex:2];
        [enc drawIndexedPrimitives:MTLPrimitiveTypeTriangle indexCount:CLUSTER_TRI * 3u indexType:MTLIndexTypeUInt16
                       indexBuffer:priv_i16 indexBufferOffset:0 instanceCount:N_3D];

        [enc setCullMode:MTLCullModeNone]; [enc setDepthStencilState:ds_ro];
        [enc setRenderPipelineState:p_prx];
        [enc setVertexBuffer:wc_prx offset:0 atIndex:0]; [enc setVertexBytes:&pm length:sizeof(pm) atIndex:1];
        [enc setFragmentBuffer:wc_prx offset:0 atIndex:0]; [enc setFragmentBytes:&pm length:sizeof(pm) atIndex:1];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N_PRX];

        // 3. SVG-SSOT HUD Overlay (Shadows -> Alpha Glass -> 12k Glyphs)
        [enc setRenderPipelineState:p_sh];
        [enc setVertexBuffer:wc_sh offset:0 atIndex:0]; [enc setVertexBuffer:wc_clp offset:0 atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
        [enc setFragmentBuffer:wc_sh offset:0 atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N_SH];

        [enc setRenderPipelineState:p_alp];
        [enc setVertexBuffer:wc_alp offset:0 atIndex:0]; [enc setVertexBuffer:wc_clp offset:0 atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
        [enc setFragmentBuffer:wc_alp offset:0 atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N_ALP];

        [enc setRenderPipelineState:p_gl];
        [enc setVertexBuffer:wc_gl offset:0 atIndex:0]; [enc setVertexBuffer:wc_clp offset:0 atIndex:1]; [enc setVertexBytes:vp length:sizeof(vp) atIndex:2];
        [enc setFragmentBuffer:wc_gl offset:0 atIndex:0]; [enc setFragmentTexture:atlas_r8 atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:N_GL];

        [enc endEncoding]; [cmd commit]; [cmd waitUntilCompleted];
        if (it >= 6) {
            cpu_us.push_back(static_cast<double>(t1 - t0) / 1e3);
            gpu_ms.push_back((cmd.GPUEndTime - cmd.GPUStartTime) * 1000.0);
        }
        if (it == 35) chk = compute_tex_checksum(ctx, p_chk, rt);
    }

    if (chk == 0) { fprintf(stderr, "Exp6 zero checksum!\n"); exit(1); }
    Stats sc = compute_stats(cpu_us), sg = compute_stats(gpu_ms);
    printf("  Single-CB 4K Composite | WC=%5.1f KB | CPU pack=%5.1f us | GPU med=%6.3f ms (%6.1f fps, p95=%6.3f ms) | chk=0x%08X\n",
           total_wc_bytes / 1024.0, sc.median_val, sg.median_val, 1000.0 / sg.median_val, sg.p95_val, chk);

    std::ostringstream js;
    js << "  \"full_4k_3d_plus_svg_hud_composite\": {\n"
       << "    \"mesh_objects\": " << N_3D << ", \"mesh_tris\": " << (N_3D * CLUSTER_TRI) << ", \"analytic_prims\": " << N_PRX
       << ", \"hud_quads\": " << (N_OPQ + N_ALP) << ", \"hud_shadows\": " << N_SH << ", \"hud_glyphs\": " << N_GL << ",\n"
       << "    \"wc_kb\": " << (total_wc_bytes / 1024.0) << ", \"cpu_pack_us\": " << sc.median_val << ",\n"
       << "    \"gpu_med_ms\": " << sg.median_val << ", \"gpu_p95_ms\": " << sg.p95_val << ", \"fps\": " << (1000.0 / sg.median_val)
       << ", \"checksum\": " << chk << "\n"
       << "  }";
    return js.str();
}

} // namespace

int main(int argc, char **argv) {
    @autoreleasepool {
        std::string json_path;
        for (int i = 1; i < argc; ++i) {
            if (std::strcmp(argv[i], "--json") == 0 && i + 1 < argc) json_path = argv[++i];
        }

        Ctx ctx;
        ctx.dev = MTLCreateSystemDefaultDevice();
        if (!ctx.dev) { fprintf(stderr, "No Metal device\n"); return 1; }
        ctx.q = [ctx.dev newCommandQueue];
        MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
        opts.languageVersion = MTLLanguageVersion3_0;
        NSError *err = nil;
        ctx.lib = [ctx.dev newLibraryWithSource:[NSString stringWithUTF8String:kObjBenchMetal] options:opts error:&err];
        if (!ctx.lib || err) {
            fprintf(stderr, "MSL compile error: %s\n", err ? err.localizedDescription.UTF8String : "unknown");
            return 1;
        }

        warm_gpu_dvfs(ctx);

        std::vector<std::string> sections;
        sections.push_back(bench_exp1_init_bake());
        sections.push_back(bench_exp2_runtime_state(ctx));
        sections.push_back(bench_exp3_cull_modes(ctx));
        sections.push_back(bench_exp4_tight_proxy(ctx));
        sections.push_back(bench_exp5_cutaway(ctx));
        sections.push_back(bench_exp6_composite(ctx));

        if (!json_path.empty()) {
            FILE *fp = std::fopen(json_path.c_str(), "w");
            if (!fp) { fprintf(stderr, "Cannot open %s\n", json_path.c_str()); return 1; }
            std::fprintf(fp, "{\n");
            for (size_t i = 0; i < sections.size(); ++i) {
                std::fprintf(fp, "%s%s\n", sections[i].c_str(), (i + 1 < sections.size()) ? "," : "");
            }
            std::fprintf(fp, "}\n");
            std::fclose(fp);
            printf("\nWrote 3D Object-SSOT JSON results to %s\n", json_path.c_str());
        }
        return 0;
    }
}
