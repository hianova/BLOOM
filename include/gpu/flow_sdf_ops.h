#ifndef FLOW_GPU_SDF_OPS_H
#define FLOW_GPU_SDF_OPS_H

#include "flow_gpu_abi.h"

/*
 * ============================================================================
 * FLOW Single-Source Portable SDF, Dual-Quaternion, BVH & Voxel Brick Kernels
 * ============================================================================
 * Uses FLOW_CONST_REF / FLOW_THREAD_CREF / FLOW_OUT_F32 portability macros so
 * the core geometric operators cross-compile across Metal, Vulkan GLSL & HLSL.
 * ============================================================================
 */

FLOW_GPU_INLINE float smin_poly(float a, float b, float k) {
    float h = clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
    return mix(b, a, h) - k * h * (1.0f - h);
}

FLOW_GPU_INLINE float smax_poly(float a, float b, float k) {
    return -smin_poly(-a, -b, k);
}

FLOW_GPU_INLINE float sd_round_box(float3 p, float3 b, float r) {
    float3 q = abs(p) - b;
    return length(max(q, 0.0f)) + min(max(q.x, max(q.y, q.z)), 0.0f) - r;
}

FLOW_GPU_INLINE float sd_capsule(float3 p, float3 a, float3 b, float r) {
    float3 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0f, 1.0f);
    return length(pa - ba * h) - r;
}

FLOW_GPU_INLINE float sd_segment_2d(float2 p, float2 a, float2 b) {
    float2 pa = p - a, ba = b - a;
    float h = clamp(dot(pa, ba) / max(dot(ba, ba), 1e-6f), 0.0f, 1.0f);
    return length(pa - ba * h);
}

FLOW_GPU_INLINE float eval_sdd_glyph(FLOW_CONST_REF(FlowGpuGlyphDesc64) g, float2 p_norm) {
    float d = 1e6f;
    uint count = min(g.stroke_count, 5U);
    for (uint i = 0U; i < count; ++i) {
        float2 a = float2(float(g.seg_coords_q8[i * 4U + 0U]), float(g.seg_coords_q8[i * 4U + 1U])) * (1.0f / 256.0f);
        float2 b = float2(float(g.seg_coords_q8[i * 4U + 2U]), float(g.seg_coords_q8[i * 4U + 3U])) * (1.0f / 256.0f);
        d = min(d, sd_segment_2d(p_norm, a, b));
    }
    return d;
}

FLOW_GPU_INLINE float3 transform_point_dq64(FLOW_CONST_REF(FlowGpuJointDq64) j, float3 p) {
    float4 qr = float4(float(j.q_real_q16[0]), float(j.q_real_q16[1]), float(j.q_real_q16[2]), float(j.q_real_q16[3])) * (1.0f / 65536.0f);
    float4 qd = float4(float(j.q_dual_q16[0]), float(j.q_dual_q16[1]), float(j.q_dual_q16[2]), float(j.q_dual_q16[3])) * (1.0f / 65536.0f);
    float3 u = qr.yzw;
    float3 t = 2.0f * (-qd.x * u + qr.x * qd.yzw - cross(qd.yzw, u));
    float3 p_rot = p + 2.0f * cross(u, cross(u, p) + qr.x * p);
    return p_rot + t;
}

FLOW_GPU_INLINE float eval_bvh_node_aabb64(FLOW_CONST_REF(FlowGpuBvhNode64) n, float3 p) {
    float3 bmin = float3(float(n.aabb_min_q16[0]), float(n.aabb_min_q16[1]), float(n.aabb_min_q16[2])) * (1.0f / 65536.0f);
    float3 bmax = float3(float(n.aabb_max_q16[0]), float(n.aabb_max_q16[1]), float(n.aabb_max_q16[2])) * (1.0f / 65536.0f);
    float3 d = max(max(bmin - p, p - bmax), float3(0.0f));
    return max(d.x, max(d.y, d.z));
}

FLOW_GPU_INLINE float sample_sdf_brick64(FLOW_CONST_REF(FlowGpuSdfBrick64) b, float3 p) {
    float3 orig = float3(float(b.origin_q16[0]), float(b.origin_q16[1]), float(b.origin_q16[2])) * (1.0f / 65536.0f);
    float step = max(float(b.voxel_step_q16) * (1.0f / 65536.0f), 1e-4f);
    float3 uvw = clamp((p - orig) / step, 0.0f, 3.0f);
    uint3 idx = min(uint3(uvw), uint3(2U));
    uint shift = (idx.y * 4U + idx.x) * 4U;
    int nibble = int((b.sdf_z_planes[idx.z] >> shift) & 0xFULL) - 8;
    return float(nibble) * (step * 0.25f);
}

struct MicroSceneCtx {
    float2 rot_cs;
    float3 drop1;
    float3 drop2;
    float3 drop3;
    float k_visc;
    float wave_freq;
    float wave_phase;
    bool eval_rock;
    bool eval_fluid;
    bool eval_ship;
};

FLOW_GPU_INLINE float2 map_micro_scene(float3 p, FLOW_THREAD_CREF(MicroSceneCtx) ctx) {
    float d_best = 100.0f;
    float mat_id = 1.0f;

    if (ctx.eval_rock) {
        float3 q = p - float3(-1.05f, -0.12f, 4.8f);
        float qx = ctx.rot_cs.x * q.x - ctx.rot_cs.y * q.z;
        float qz = ctx.rot_cs.y * q.x + ctx.rot_cs.x * q.z;
        q.x = qx;
        q.z = qz;

        float d_macro = length(q) - 0.92f;
        float3 qp = abs(q);
        if (qp.x < qp.z) {
            float tmp = qp.x;
            qp.x = qp.z;
            qp.z = tmp;
        }
        float d_truss = sd_round_box(qp - float3(0.62f, 0.0f, 0.0f), float3(0.30f, 0.18f, 0.18f), 0.04f);
        float d_ring = length(float2(length(float2(q.x, q.z)) - 0.84f, q.y)) - 0.11f;
        d_macro = smin_poly(d_macro, smin_poly(d_truss, d_ring, 0.08f), 0.10f);

        float d_carve = length(q - float3(0.32f, 0.10f, -0.50f)) - (0.28f + ctx.k_visc);
        float d_tunnel = length(float2(q.x - 0.10f, q.y - 0.04f)) - 0.26f;
        float d_cut = smin_poly(d_carve, d_tunnel, 0.10f);

        d_best = smax_poly(d_macro, -d_cut, 0.038f);
        mat_id = (d_cut < 0.045f && d_macro < 0.035f) ? 2.0f : 1.0f;
    }

    if (ctx.eval_fluid) {
        float3 fp = p - float3(0.95f, 0.15f, 4.4f);
        float d_fluid = smin_poly(length(fp - ctx.drop1) - 0.30f,
                                  length(fp - ctx.drop2) - 0.26f, ctx.k_visc);
        d_fluid = smin_poly(d_fluid, length(fp - ctx.drop3) - 0.24f, ctx.k_visc);
        float r_wave = length(fp);
        float ripple = 0.0085f * cos(r_wave * ctx.wave_freq - ctx.wave_phase) * exp(-r_wave * 1.4f);
        d_fluid += ripple;
        if (d_fluid < d_best) {
            d_best = d_fluid;
            mat_id = 3.0f;
        }
    }

    if (ctx.eval_ship) {
        float3 sp = p - float3(0.38f, 0.26f, 2.35f);
        float spx = 0.94f * sp.x + 0.34f * sp.y;
        float spy = -0.34f * sp.x + 0.94f * sp.y;
        sp.x = spx;
        sp.y = spy;
        float d_fuselage = sd_capsule(sp, float3(0.0f, 0.0f, -0.24f), float3(0.0f, -0.02f, 0.38f), 0.078f);
        float3 sw = sp;
        sw.x = abs(sw.x);
        float d_wing = sd_round_box(sw - float3(0.22f, 0.01f, -0.04f), float3(0.24f, 0.015f, 0.15f), 0.010f);
        float d_fin  = sd_round_box(sw - float3(0.34f, -0.06f, -0.12f), float3(0.015f, 0.075f, 0.09f), 0.008f);
        float d_ship = smin_poly(d_fuselage, min(d_wing, d_fin), 0.030f);

        float d_nozzle = length(float2(abs(sp.x) - 0.062f, sp.y - 0.005f)) - 0.038f;
        d_nozzle = max(d_nozzle, abs(sp.z + 0.34f) - 0.07f);
        if (d_nozzle < d_ship) {
            if (d_nozzle < d_best) {
                d_best = d_nozzle;
                mat_id = 5.0f;
            }
        } else if (d_ship < d_best) {
            d_best = d_ship;
            mat_id = 4.0f;
        }
    }

    return float2(d_best, mat_id);
}

FLOW_GPU_INLINE float3 calc_sdf_normal(float3 p, FLOW_THREAD_CREF(MicroSceneCtx) ctx) {
    const float2 e = float2(0.002f, -0.002f);
    const float3 exyy = float3(e.x, e.y, e.y);
    const float3 eyyx = float3(e.y, e.y, e.x);
    const float3 eyxy = float3(e.y, e.x, e.y);
    const float3 exxx = float3(e.x, e.x, e.x);
    return normalize(
        exyy * map_micro_scene(p + exyy, ctx).x +
        eyyx * map_micro_scene(p + eyyx, ctx).x +
        eyxy * map_micro_scene(p + eyxy, ctx).x +
        exxx * map_micro_scene(p + exxx, ctx).x
    );
}

FLOW_GPU_INLINE float3 apply_micro_voxel_lattice(float3 p, float3 nor, FLOW_THREAD_CREF(MicroSceneCtx) ctx,
                                                 FLOW_OUT_F32(out_bevel_ao), FLOW_OUT_F32(out_cell_hash),
                                                 FLOW_OUT_F32(out_vein)) {
    float3 q = p - float3(-1.05f, -0.12f, 4.8f);
    float qx = ctx.rot_cs.x * q.x - ctx.rot_cs.y * q.z;
    float qz = ctx.rot_cs.y * q.x + ctx.rot_cs.x * q.z;
    q.x = qx;
    q.z = qz;

    const float vox_scale = 0.075f;
    float3 cell_p = q * (1.0f / vox_scale);
    float3 cell_id = floor(cell_p + 0.5f);
    float3 local = cell_p - cell_id;
    out_cell_hash = fract(dot(cell_id, float3(0.1031f, 0.1730f, 0.2973f)));
    out_vein = smoothstep(0.76f, 0.90f, fract(out_cell_hash * 7.13f));

    float3 edge_d = smoothstep(0.36f, 0.49f, abs(local)) * sign(local);
    out_bevel_ao = 1.0f - 0.55f * max(abs(edge_d.x), max(abs(edge_d.y), abs(edge_d.z)));

    float3 bev_w = float3(ctx.rot_cs.x * edge_d.x + ctx.rot_cs.y * edge_d.z,
                          edge_d.y,
                         -ctx.rot_cs.y * edge_d.x + ctx.rot_cs.x * edge_d.z);
    return normalize(nor + bev_w * 0.38f);
}

FLOW_GPU_INLINE float calc_sdf_soft_shadow(float3 ro, float3 rd, FLOW_THREAD_CREF(MicroSceneCtx) ctx) {
    float res = 1.0f;
    float t = 0.04f;
    for (int i = 0; i < 5; ++i) {
        float h = map_micro_scene(ro + rd * t, ctx).x;
        res = min(res, 10.0f * h / t);
        t += clamp(h, 0.05f, 0.35f);
        if (res < 0.01f || t > 2.2f) break;
    }
    return clamp(res, 0.0f, 1.0f);
}

FLOW_GPU_INLINE float calc_sdf_ao(float3 pos, float3 nor, FLOW_THREAD_CREF(MicroSceneCtx) ctx) {
    float d1 = map_micro_scene(pos + 0.05f * nor, ctx).x;
    float d2 = map_micro_scene(pos + 0.14f * nor, ctx).x;
    float occ = (0.05f - d1) + 0.5f * (0.14f - d2);
    return clamp(1.0f - 2.8f * occ, 0.08f, 1.0f);
}

FLOW_GPU_INLINE float3 aces_tonemap(float3 x) {
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

FLOW_GPU_INLINE float eval_7seg_digit_sdf(float2 p, uint digit) {
    uint d_idx = digit % 10U;
    uint m = (d_idx < 4U)
        ? ((0x4F5B063FU >> (d_idx * 8U)) & 0x7FU)
        : ((d_idx < 8U)
            ? ((0x077D6D66U >> ((d_idx - 4U) * 8U)) & 0x7FU)
            : ((d_idx == 8U) ? 0x7FU : 0x6FU));
    float d = 1.0f;
    if ((m & 0x01U) != 0U) d = min(d, max(abs(p.x) - 0.28f, abs(p.y + 0.48f) - 0.06f));
    if ((m & 0x02U) != 0U) d = min(d, max(abs(p.x - 0.28f) - 0.06f, abs(p.y + 0.24f) - 0.22f));
    if ((m & 0x04U) != 0U) d = min(d, max(abs(p.x - 0.28f) - 0.06f, abs(p.y - 0.24f) - 0.22f));
    if ((m & 0x08U) != 0U) d = min(d, max(abs(p.x) - 0.28f, abs(p.y - 0.48f) - 0.06f));
    if ((m & 0x10U) != 0U) d = min(d, max(abs(p.x + 0.28f) - 0.06f, abs(p.y - 0.24f) - 0.22f));
    if ((m & 0x20U) != 0U) d = min(d, max(abs(p.x + 0.28f) - 0.06f, abs(p.y + 0.24f) - 0.22f));
    if ((m & 0x40U) != 0U) d = min(d, max(abs(p.x) - 0.26f, abs(p.y) - 0.06f));
    return d;
}

FLOW_GPU_INLINE float3 unpack_rgb24(uint rgb24) {
    return float3(float((rgb24 >> 16U) & 0xFFU),
                  float((rgb24 >> 8U)  & 0xFFU),
                  float(rgb24          & 0xFFU)) * (1.0f / 255.0f);
}

/*
 * Single-Source Cloud Room Multi-View Tile-Atlas Coordinate Router
 * Routes a global Atlas UV in [0, 1]^2 to the owning player seat [0..15]
 * and outputs intra-tile normalized UV in [0, 1]^2 via out_u / out_v.
 */
FLOW_GPU_INLINE uint eval_cloud_room_tile_seat_and_uv(
    float2 atlas_uv,
    FLOW_CONST_REF(FlowCloudRoomDesc64) room,
    FLOW_OUT_F32(out_local_u),
    FLOW_OUT_F32(out_local_v)
) {
    uint cols = max(uint(room.grid_cols), 1U);
    uint rows = max(uint(room.grid_rows), 1U);
    float2 scaled = clamp(atlas_uv, 0.0f, 0.999999f) * float2(float(cols), float(rows));
    uint col_idx = min(uint(scaled.x), cols - 1U);
    uint row_idx = min(uint(scaled.y), rows - 1U);
    out_local_u = fract(scaled.x);
    out_local_v = fract(scaled.y);
    uint seat_idx = row_idx * cols + col_idx;
    if (seat_idx >= 16U || ((uint(room.active_seat_mask) >> seat_idx) & 1U) == 0U) {
        return 0xFFFFFFFFU;
    }
    return seat_idx;
}

#endif /* FLOW_GPU_SDF_OPS_H */
