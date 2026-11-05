#include <metal_stdlib>
using namespace metal;
#include "flow_gpu_abi.h"

struct VertexOut {
    float4 position [[position]];
    float2 screen_px;
    float2 tile_uv;
    uint tile_id;
};

vertex VertexOut tile_vertex(
    uint vid [[vertex_id]],
    uint iid [[instance_id]],
    constant uint *active_tiles [[buffer(0)]],
    constant FlowGpuFrameUniforms256 &u [[buffer(1)]]
) {
    uint tile_idx = active_tiles[iid];
    uint tx = tile_idx % 64U;
    uint ty = tile_idx / 64U;

    float tile_sz = 60.0f;
    float2 base_px = float2(float(tx) * tile_sz, float(ty) * tile_sz);

    /* 4-vertex triangle strip */
    float2 quad_uv = float2(float(vid & 1U), float((vid >> 1U) & 1U));
    float2 px = base_px + quad_uv * tile_sz;

    /* Normalized Device Coordinates: [-1, 1] */
    float2 ndc = (px / float2(float(u.s0.screen_w), float(u.s0.screen_h))) * 2.0f - 1.0f;
    ndc.y = -ndc.y;

    VertexOut out;
    out.position = float4(ndc, 0.0f, 1.0f);
    out.screen_px = px;
    out.tile_uv = quad_uv;
    out.tile_id = tile_idx;
    return out;
}

/*
 * Photorealistic Single-Pass SDF Cone-Tracing, Micro-Voxel CSG Carving,
 * Smooth-CSG Surface-Tension Fluid, Interceptor Chase Hull, and CineCamera Optics
 */
#ifndef FLOW_GPU_INLINE
#define FLOW_GPU_INLINE static inline __attribute__((always_inline))
#endif

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

FLOW_GPU_INLINE float eval_sdd_glyph(constant FlowGpuGlyphDesc64 &g, float2 p_norm) {
    float d = 1e6f;
    uint count = min(g.stroke_count, 5U);
    for (uint i = 0U; i < count; ++i) {
        float2 a = float2(float(g.seg_coords_q8[i * 4U + 0U]), float(g.seg_coords_q8[i * 4U + 1U])) * (1.0f / 256.0f);
        float2 b = float2(float(g.seg_coords_q8[i * 4U + 2U]), float(g.seg_coords_q8[i * 4U + 3U])) * (1.0f / 256.0f);
        d = min(d, sd_segment_2d(p_norm, a, b));
    }
    return d;
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

FLOW_GPU_INLINE float2 map_micro_scene(float3 p, thread const MicroSceneCtx &ctx) {
    float d_best = 100.0f;
    float mat_id = 1.0f;

    if (ctx.eval_rock) {
        float3 q = p - float3(-1.05f, -0.12f, 4.8f);
        float qx = ctx.rot_cs.x * q.x - ctx.rot_cs.y * q.z;
        float qz = ctx.rot_cs.y * q.x + ctx.rot_cs.x * q.z;
        q.x = qx;
        q.z = qz;

        /* Exact Lipschitz-1 Smooth CSG Citadel + Asteroid Core */
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

        /* Real-time CSG Thermal Laser Carving Cavity + Prefab SDF X-Ray Cutaway Aperture */
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
        /* Unified Boundary-Layer Phase Ripple: f_rippled = f_0 + A * cos(k*r - omega*t) */
        float r_wave = length(fp);
        float ripple = 0.0085f * cos(r_wave * ctx.wave_freq - ctx.wave_phase) * exp(-r_wave * 1.4f);
        d_fluid += ripple;
        if (d_fluid < d_best) {
            d_best = d_fluid;
            mat_id = 3.0f;
        }
    }

    if (ctx.eval_ship) {
        /* Sleek Interceptor Craft in Dynamic Chase View (lower-right rule-of-thirds) */
        float3 sp = p - float3(0.38f, 0.26f, 2.35f);
        /* Banking turn toward the asteroid citadel */
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

        /* Rear Twin Ion Thruster Nozzles protruding clearly behind the fuselage (-Z) */
        float d_nozzle = length(float2(abs(sp.x) - 0.062f, sp.y - 0.005f)) - 0.038f;
        d_nozzle = max(d_nozzle, abs(sp.z + 0.34f) - 0.07f);
        if (d_nozzle < d_ship) {
            if (d_nozzle < d_best) {
                d_best = d_nozzle;
                mat_id = 5.0f; /* Twin Ion Thruster Plasma Core */
            }
        } else if (d_ship < d_best) {
            d_best = d_ship;
            mat_id = 4.0f; /* Polar-White Titanium Interceptor Hull */
        }
    }

    return float2(d_best, mat_id);
}

FLOW_GPU_INLINE float3 calc_sdf_normal(float3 p, thread const MicroSceneCtx &ctx) {
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

/* 3D Micro-Voxel Beveled Chisel Lattice Perturbation (zero silhouette clipping!) */
FLOW_GPU_INLINE float3 apply_micro_voxel_lattice(float3 p, float3 nor, thread const MicroSceneCtx &ctx,
                                                 thread float &out_bevel_ao, thread float &out_cell_hash,
                                                 thread float &out_vein) {
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

FLOW_GPU_INLINE float calc_sdf_soft_shadow(float3 ro, float3 rd, thread const MicroSceneCtx &ctx) {
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

FLOW_GPU_INLINE float calc_sdf_ao(float3 pos, float3 nor, thread const MicroSceneCtx &ctx) {
    float d1 = map_micro_scene(pos + 0.05f * nor, ctx).x;
    float d2 = map_micro_scene(pos + 0.14f * nor, ctx).x;
    float occ = (0.05f - d1) + 0.5f * (0.14f - d2);
    return clamp(1.0f - 2.8f * occ, 0.08f, 1.0f);
}

FLOW_GPU_INLINE float3 aces_tonemap(float3 x) {
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

/* Zero-Font-Atlas 7-Segment Vector Digit SDF Evaluator (Digits 0..9, Zero Stack Array) */
FLOW_GPU_INLINE float eval_7seg_digit_sdf(float2 p, uint digit) {
    uint d_idx = digit % 10U;
    uint m = (d_idx < 4U)
        ? ((0x4F5B063FU >> (d_idx * 8U)) & 0x7FU)
        : ((d_idx < 8U)
            ? ((0x077D6D66U >> ((d_idx - 4U) * 8U)) & 0x7FU)
            : ((d_idx == 8U) ? 0x7FU : 0x6FU));
    float d = 1.0f;
    if ((m & 0x01U) != 0U) d = min(d, max(abs(p.x) - 0.28f, abs(p.y + 0.48f) - 0.06f)); /* Top */
    if ((m & 0x02U) != 0U) d = min(d, max(abs(p.x - 0.28f) - 0.06f, abs(p.y + 0.24f) - 0.22f)); /* Top-Right */
    if ((m & 0x04U) != 0U) d = min(d, max(abs(p.x - 0.28f) - 0.06f, abs(p.y - 0.24f) - 0.22f)); /* Bot-Right */
    if ((m & 0x08U) != 0U) d = min(d, max(abs(p.x) - 0.28f, abs(p.y - 0.48f) - 0.06f)); /* Bottom */
    if ((m & 0x10U) != 0U) d = min(d, max(abs(p.x + 0.28f) - 0.06f, abs(p.y - 0.24f) - 0.22f)); /* Bot-Left */
    if ((m & 0x20U) != 0U) d = min(d, max(abs(p.x + 0.28f) - 0.06f, abs(p.y + 0.24f) - 0.22f)); /* Top-Left */
    if ((m & 0x40U) != 0U) d = min(d, max(abs(p.x) - 0.26f, abs(p.y) - 0.06f)); /* Middle */
    return d;
}

FLOW_GPU_INLINE float3 unpack_rgb24(uint rgb24) {
    return float3(float((rgb24 >> 16U) & 0xFFU),
                  float((rgb24 >> 8U)  & 0xFFU),
                  float(rgb24          & 0xFFU)) * (1.0f / 255.0f);
}

/*
 * ============================================================================
 * Moodboard Stage 1: Autumn Train Window Condensation, Gravity Rivulet Wiping
 *                    & 2-Jet Surface Normal Snell's Law Refraction
 * ============================================================================
 */
FLOW_GPU_INLINE float3 eval_mood_window_optics(
    float2 uv_raw,
    float2 look_off,
    float time_sec,
    float3 sky_col,
    float3 ground_col,
    float3 accent_col,
    float3 backdrop_col,
    float fog_condense,
    float rivulet_strength
) {
    /* Linearize sRGB moodboard swatches before physical HDR lighting */
    float3 sky_lin      = sky_col * sky_col;
    float3 ground_lin   = ground_col * ground_col;
    float3 accent_lin   = accent_col * accent_col;
    float3 backdrop_lin = backdrop_col * backdrop_col * 0.14f;

    float2 w_uv = uv_raw - look_off * 0.14f;
    float2 win_half = float2(0.215f, 0.365f);
    float win_rx = 0.052f;
    float2 q_win = abs(w_uv) - win_half + float2(win_rx);
    float d_win = min(max(q_win.x, q_win.y), 0.0f) + length(max(q_win, 0.0f)) - win_rx;

    /* Outside Window: Dark Train Cabin Interior Silhouette + Rubber Gasket Bevel */
    if (d_win > 0.0f) {
        float gasket = smoothstep(0.024f, 0.002f, d_win);
        float bevel_rim = exp(-abs(d_win - 0.006f) * 360.0f) * 0.035f;
        float bounce = exp(-d_win * 16.0f) * 0.025f;
        float3 cab_col = backdrop_lin + (sky_lin * 0.30f + ground_lin * 0.70f) * bounce;
        return mix(cab_col, float3(0.002f, 0.0025f, 0.002f), gasket) + accent_lin * bevel_rim;
    }

    /* Inside Window Glass: Evaluate Gravity Rivulet Wiping State & 2-Jet Meniscus Gradient */
    float wipe_clear = 0.0f;
    float2 grad_h = float2(0.0f);
    float meniscus_rim = 0.0f;

    /* Primary & secondary rivulet column bands */
    for (int layer = 0; layer < 2; ++layer) {
        float freq = (layer == 0) ? 26.0f : 48.0f;
        float u_x = (w_uv.x + float(layer) * 0.073f) * freq;
        float cid = floor(u_x);
        float h_col = fract(sin(cid * 127.1f + float(layer) * 311.7f) * 43758.5453f);

        if (h_col > 0.32f) {
            float y_head = -0.06f + 0.12f * fract(h_col * 13.7f)
                         - 0.020f * sin(time_sec * 0.35f + h_col * 6.28f);
            if (w_uv.y > y_head) {
                float v_prog = clamp((w_uv.y - y_head) * 18.0f, 0.0f, 1.0f);
                float wobble = 0.13f * sin(w_uv.y * 24.0f + h_col * 19.0f)
                             + 0.06f * cos(w_uv.y * 58.0f - h_col * 11.0f);
                float local_x = (u_x - cid - 0.5f) - wobble;
                float track_w = (0.16f + 0.15f * fract(h_col * 5.3f)) * (0.72f + 0.28f * v_prog);
                float norm_dx = local_x / max(track_w, 0.05f);

                if (abs(norm_dx) < 1.35f) {
                    /* Droplet wipes away micro-condensation inside the track */
                    float w_mask = smoothstep(1.10f, 0.25f, abs(norm_dx)) * v_prog;
                    wipe_clear = max(wipe_clear, w_mask * rivulet_strength);

                    /* 2-Jet derivative grad(h) of the curved water meniscus */
                    float env = exp(-norm_dx * norm_dx * 2.4f) * v_prog * rivulet_strength;
                    grad_h.x += -norm_dx * env * 0.18f;
                    grad_h.y += -0.14f * env;
                    meniscus_rim += smoothstep(0.50f, 0.90f, abs(norm_dx)) * smoothstep(1.30f, 0.90f, abs(norm_dx)) * v_prog;
                }
            }
        }
    }

    /* Condensation Mist Field (thick in lower 60% of window, wiped clean inside rivulet trails) */
    float mist_vertical = smoothstep(-0.07f, 0.06f, w_uv.y);
    float raw_fog = mist_vertical * fog_condense;
    float active_fog = clamp(raw_fog * (1.0f - wipe_clear * 0.92f), 0.0f, 1.0f);

    /* Micro-droplet grain normal perturbation in unwiped condensation zones */
    float2 bead_uv = w_uv * 260.0f;
    float2 bead_id = floor(bead_uv);
    float2 bead_lc = bead_uv - bead_id - float2(0.5f);
    float bead_h = fract(sin(dot(bead_id, float2(12.9898f, 78.233f))) * 43758.5f);
    float droplet_lens = 0.0f;
    if (bead_h > 0.30f && active_fog > 0.05f) {
        float b_rad = length(bead_lc);
        if (b_rad < 0.44f) {
            grad_h = grad_h - bead_lc * (active_fog * 0.09f);
            droplet_lens = (0.44f - b_rad) * active_fog;
        }
    }

    /* Snell's Law Refracted Exterior Sampling (motion-blurred autumn field & cerulean sky) */
    float2 ext_uv = w_uv + grad_h;
    float horizon_y = -0.025f + 0.012f * sin(ext_uv.x * 7.5f) - 0.006f * cos(ext_uv.x * 19.0f);

    /* Cerulean sky gradient from zenith to warm horizon haze */
    float sky_t = clamp((-ext_uv.y + 0.04f) * 2.5f, 0.0f, 1.0f);
    float3 sky_grad = mix(mix(sky_lin, accent_lin, 0.55f), sky_lin * 0.85f, sky_t);

    /* Rich golden-amber autumn foliage with depth shading */
    float motion_streak = sin(ext_uv.y * 75.0f + sin(ext_uv.x * 5.5f - time_sec * 1.4f) * 2.2f) * 0.08f;
    float bottom_shade = smoothstep(0.06f, 0.35f, ext_uv.y);
    float3 autumn_col = ground_lin * (1.18f + motion_streak) * (1.0f - 0.58f * bottom_shade);

    /* Distant mountain silhouette haze band at horizon */
    float mountain_band = exp(-abs(ext_uv.y - horizon_y) * 34.0f);
    float blend_ground = smoothstep(horizon_y - 0.032f, horizon_y + 0.042f, ext_uv.y);
    float3 ext_col = mix(sky_grad, autumn_col, blend_ground);
    ext_col = mix(ext_col, mix(sky_lin, ground_lin, 0.38f) * 0.65f, mountain_band * 0.55f);

    /* Mie forward scattering on fogged glass vs. dark crisp contrast inside wiped rivulets */
    float3 mist_scatter = mix(accent_lin * 0.85f, mix(sky_lin, ground_lin, 0.62f) + float3(0.06f), 0.65f);
    float3 glass_col = mix(ext_col, mist_scatter, active_fog * 0.38f) + accent_lin * (droplet_lens * 0.22f);

    /* Meniscus dark refractive channel & sky-blue caustic highlight along upper rivulet heads */
    float caustic_sky = clamp(-grad_h.x * 6.0f + wipe_clear * 0.45f, 0.0f, 1.0f) * smoothstep(0.09f, -0.05f, w_uv.y);
    glass_col *= (1.0f - 0.48f * clamp(meniscus_rim, 0.0f, 1.0f) * smoothstep(-0.03f, 0.22f, w_uv.y));
    glass_col *= (1.0f - 0.22f * wipe_clear * smoothstep(0.02f, 0.28f, w_uv.y));
    glass_col += mix(sky_lin, accent_lin, 0.55f) * caustic_sky * 0.55f;

    /* Inner window gasket soft shadow */
    float edge_ao = smoothstep(0.0f, -0.022f, d_win);
    return glass_col * (0.18f + 0.82f * edge_ao);
}

/*
 * ============================================================================
 * Moodboard Stage 2: Infinite 3D Hash-Lattice Point-Cloud Monolith Corridors,
 *                    Anamorphic Horizon Singularity & Glittering Wave Path
 * ============================================================================
 */
FLOW_GPU_INLINE float3 eval_mood_particle_corridor(
    float2 uv_rot,
    float2 look_off,
    float time_sec,
    float3 sky_col,
    float3 ground_col,
    float3 accent_col,
    float particle_density,
    float singularity_gain
) {
    float3 sky_lin    = sky_col * sky_col;
    float3 ground_lin = ground_col * ground_col * 0.18f;
    float3 accent_lin = accent_col * accent_col;

    float2 vp = float2(look_off.x * 0.18f, -0.015f + look_off.y * 0.12f);
    float2 duv = uv_rot - vp;

    /* 1. Deep Void + Atmospheric Horizon Haze + Anamorphic Singularity Core */
    float3 col = ground_lin * 0.25f;
    float r_core = length(duv);
    float horizon_band = exp(-abs(duv.y) * 24.0f) * exp(-abs(duv.x) * 1.6f);
    col += sky_lin * horizon_band * 0.85f;

    float core_energy = (0.00055f / (r_core * r_core + 0.00018f) + exp(-r_core * 26.0f) * 1.35f) * singularity_gain;
    float anamorphic_streak = exp(-abs(duv.y) * 240.0f) * exp(-abs(duv.x) * 4.5f) * 1.85f * singularity_gain;
    col += accent_lin * core_energy + mix(accent_lin, float3(0.65f, 0.85f, 1.0f), 0.40f) * anamorphic_streak;

    /* 2. Infinite Volumetric Point-Cloud Monolith Corridors (Left & Right Perspective Slabs) */
    float abs_x = abs(duv.x);
    if (abs_x > 0.035f && duv.y < 0.045f) {
        float density_thresh = clamp(0.80f - particle_density * 0.22f, 0.55f, 0.86f);
        for (int k = 0; k < 4; ++k) {
            float wall_x = 0.36f + float(k) * 0.32f;
            float z_p = wall_x / abs_x;
            float y_p = -duv.y * z_p;

            if (z_p > 1.2f && z_p < 24.0f && y_p > -0.15f && y_p < 1.65f) {
                float z_travel = z_p + time_sec * 0.45f + float(k) * 1.7f;
                float pillar_mod = fract(z_travel * 0.55f);
                /* Monolith vertical columns with dark architectural gaps between pillars */
                if (pillar_mod < 0.58f) {
                    /* Screen-adaptive lattice so particles stay crisp micro-sparks at all depths */
                    float2 grid_uv = float2(z_travel * 36.0f, (-duv.y) * 240.0f + float(k) * 19.0f);
                    float2 cell_id = floor(grid_uv);
                    float2 local_p = grid_uv - cell_id - float2(0.5f);
                    float s_dx = (duv.x >= 0.0f) ? 1.0f : -1.0f;
                    float h_cell = fract(sin(dot(cell_id + float2(s_dx * 17.0f, float(k) * 31.0f),
                                                 float2(127.1f, 311.7f))) * 43758.5453f);
                    if (h_cell > density_thresh) {
                        float2 jitter = float2(fract(h_cell * 13.3f) - 0.5f, fract(h_cell * 29.7f) - 0.5f) * 0.52f;
                        float d_pt = length(local_p - jitter);
                        float spark = smoothstep(0.32f, 0.04f, d_pt) * (0.40f + 0.60f * sin(time_sec * 3.2f + h_cell * 40.0f));
                        float v_fade = smoothstep(1.65f, 0.15f, y_p) * smoothstep(-0.15f, 0.02f, y_p);
                        float z_fade = exp(-z_p * 0.14f) * smoothstep(1.2f, 2.4f, z_p);
                        float warm_mix = exp(-abs(duv.x) * 3.2f) * exp(-abs(duv.y) * 8.0f);
                        float3 pt_col = mix(sky_lin * 1.8f + float3(0.08f, 0.14f, 0.28f),
                                            accent_lin * 1.25f,
                                            0.35f + 0.65f * warm_mix);
                        col += pt_col * spark * v_fade * z_fade * 0.95f;
                    }
                }
            }
        }
    }

    /* 3. Undulating Particle Wave Sea & Central Glittering Starlight Path (duv.y > 0) */
    if (duv.y > 0.002f) {
        float z_g = 0.36f / (duv.y + 0.008f);
        if (z_g < 30.0f) {
            float x_g = duv.x * z_g;
            float z_move = z_g - time_sec * 0.42f;
            float wave = 0.10f * sin(x_g * 3.4f + z_move * 1.8f)
                       + 0.05f * cos(x_g * 6.8f - z_move * 2.6f + time_sec * 0.6f);

            /* Keep wave particle grid fine and dense across the entire ocean floor */
            float2 g_uv = float2(duv.x * 320.0f + wave * 12.0f, z_move * 34.0f + wave * 6.0f);
            float2 g_id = floor(g_uv);
            float2 g_lc = g_uv - g_id - float2(0.5f);
            float h_g = fract(sin(dot(g_id, float2(41.31f, 289.73f))) * 43758.5453f);

            /* Central meandering glittering reflection path under the singularity */
            float x_path = 0.040f * sin(z_g * 0.95f - time_sec * 1.1f) + wave * 0.18f;
            float path_env = exp(-abs(x_g - x_path) * (4.8f + 0.22f * z_g));
            float d_dot = length(g_lc);
            float dot_mask = smoothstep(0.34f, 0.05f, d_dot);
            float dist_fade = exp(-z_g * 0.10f) * smoothstep(0.002f, 0.035f, duv.y);

            /* Subtle deep-blue wave contour particles */
            if (h_g > 0.48f) {
                float wave_crest = clamp(0.45f + wave * 3.8f, 0.10f, 1.1f);
                col += (sky_lin * 1.2f + float3(0.02f, 0.05f, 0.14f)) * (dot_mask * wave_crest * dist_fade * 0.42f);
            }

            /* Specular microfacet glitter sparks along the central starlight path */
            float glitter_thresh = 0.82f - 0.52f * path_env * clamp(particle_density, 0.3f, 1.0f);
            if (h_g > glitter_thresh) {
                float twinkle = 0.50f + 0.50f * sin(time_sec * 5.5f + h_g * 62.8f);
                col += accent_lin * (dot_mask * path_env * twinkle * dist_fade * 2.4f);
            }
        }
    }

    return col;
}

/*
 * ============================================================================
 * Moodboard Stage 3: "Winter Shore" Isometric CSG Cutaway Diorama Block
 *                    (Snow Terrain + Geological Strata Cross-Section +
 *                     Beer-Lambert Translucent Sea + 2400K Cabin Blackbody GI)
 * ============================================================================
 */
FLOW_GPU_INLINE float eval_diorama_terrain_h(float2 xz) {
    /* Diagonal shoreline separating upper-left snowy rocky ridge from lower-right sea basin */
    float shore = xz.x * 0.72f + xz.y * 0.68f - 0.04f;
    float base_slope = clamp(-shore * 0.38f + 0.02f, -0.19f, 0.24f);

    /* Flat foundation terrace around the Nordic cabin at (-0.28, -0.22) */
    float2 d_cab = xz - float2(-0.28f, -0.22f);
    float cab_pad = exp(-dot(d_cab, d_cab) * 14.0f);

    /* Multi-octave organic Nordic coastal rock crags (zero regular sine stripes) */
    float n1 = sin(xz.x * 6.3f + sin(xz.y * 5.7f) * 1.4f) * cos(xz.y * 6.9f - sin(xz.x * 4.8f) * 1.3f);
    float n2 = sin(xz.x * 14.7f - xz.y * 12.3f + sin(xz.x * 9.1f)) * 0.45f;
    float n3 = cos(xz.x * 29.0f + xz.y * 27.0f) * 0.18f;
    float crags = (n1 * 0.048f + n2 * 0.028f + n3 * 0.012f) * (1.0f - 0.78f * cab_pad);

    /* Sea stack rocks emerging in the water basin */
    float r1 = exp(-dot(xz - float2(0.46f, -0.18f), xz - float2(0.46f, -0.18f)) * 45.0f) * 0.15f;
    float r2 = exp(-dot(xz - float2(0.12f,  0.48f), xz - float2(0.12f,  0.48f)) * 48.0f) * 0.14f;

    float h_raw = base_slope + crags + r1 + r2;
    return mix(h_raw, 0.135f, cab_pad * 0.85f);
}

FLOW_GPU_INLINE float2 map_diorama_solid(float3 p) {
    float h_t = eval_diorama_terrain_h(float2(p.x, p.z));
    float d_terr = (p.y - h_t) * 0.75f;
    float mat_id = 1.0f; /* 1.0 = Snow/Rock Terrain */

    /* Nordic Red Cabin seated firmly on the upper-left snowy terrace */
    float3 q = p - float3(-0.28f, 0.205f, -0.22f);
    float d_walls = sd_round_box(q - float3(0.0f, -0.015f, 0.0f), float3(0.145f, 0.085f, 0.115f), 0.004f);
    float3 qr = q - float3(0.0f, 0.070f, 0.0f);
    float d_roof = max(max(abs(qr.z) * 0.72f + qr.y - 0.082f, -qr.y - 0.012f),
                       max(abs(qr.x) - 0.165f, abs(qr.z) - 0.138f));
    float d_chimney = sd_round_box(q - float3(0.035f, 0.145f, -0.045f), float3(0.018f, 0.045f, 0.018f), 0.002f);

    if (d_walls < d_terr) {
        d_terr = d_walls;
        mat_id = 2.0f; /* 2.0 = Nordic Red Cabin Wall & Glowing 2400K Windows */
    }
    if (d_roof < d_terr) {
        d_terr = d_roof;
        mat_id = 3.0f; /* 3.0 = Snow-Capped Gable Roof */
    }
    if (d_chimney < d_terr) {
        d_terr = d_chimney;
        mat_id = 4.0f; /* 4.0 = Soot-Black Chimney */
    }

    /* Wooden Pier & Pilings extending from cabin deck into the sea (+x direction) */
    float3 pq = p - float3(0.12f, 0.022f, -0.04f);
    float d_deck = sd_round_box(pq, float3(0.24f, 0.010f, 0.048f), 0.002f);
    float2 piling_xz = float2(fract(pq.x * 7.5f) - 0.5f, abs(pq.z) - 0.038f);
    float d_piling = max(length(piling_xz) - 0.012f, max(abs(pq.x) - 0.23f, abs(pq.y + 0.055f) - 0.060f));
    float d_pier = min(d_deck, d_piling);
    if (d_pier < d_terr) {
        d_terr = d_pier;
        mat_id = 5.0f; /* 5.0 = Weathered Timber Pier */
    }

    return float2(d_terr, mat_id);
}

FLOW_GPU_INLINE float3 eval_mood_isometric_diorama(
    float2 uv_raw,
    float2 look_off,
    float3 craft_rot,
    float time_sec,
    float3 snow_col,
    float3 sea_col,
    float3 cabin_col,
    float3 backdrop_col,
    float diorama_cut,
    float xray_radius,
    float snow_cover
) {
    /* Warm Studio Backdrop with soft radial lighting */
    float vig = 1.0f - 0.16f * dot(uv_raw, uv_raw);
    float3 bg_col = backdrop_col * vig;

    /* Isometric Telephoto Camera Rig looking from (+x, +y, +z) toward origin */
    float yaw = 0.78f + look_off.x * 0.55f + craft_rot.y * 0.30f;
    float pitch = clamp(0.50f - look_off.y * 0.30f + craft_rot.x * 0.20f, 0.28f, 0.82f);
    float cy = cos(yaw), sy = sin(yaw);
    float cp = cos(pitch), sp = sin(pitch);

    float3 fwd = float3(-sy * cp, -sp, -cy * cp);
    float3 rgt = float3(cy, 0.0f, -sy);
    float3 up  = float3(rgt.y * fwd.z - rgt.z * fwd.y,
                        rgt.z * fwd.x - rgt.x * fwd.z,
                        rgt.x * fwd.y - rgt.y * fwd.x);

    /* Ortho-telephoto rays for true miniature tabletop proportions */
    float ortho_scale = 1.76f;
    float3 ro = fwd * (-4.5f) + (rgt * uv_raw.x - up * (uv_raw.y + 0.025f)) * ortho_scale;
    float3 rd = normalize(fwd + (rgt * uv_raw.x - up * uv_raw.y) * 0.12f);

    /* Cutaway Bounding Box (interactive with X-Ray slider!) */
    float active_cut = clamp(diorama_cut - xray_radius * 0.65f, 0.38f, 1.0f);
    float3 box_b = float3(0.82f * active_cut, 0.42f, 0.82f);

    /* Analytic Ray-AABB Slab Intersection */
    float3 inv_rd = float3(1.0f / (rd.x + 1e-6f), 1.0f / (rd.y + 1e-6f), 1.0f / (rd.z + 1e-6f));
    float3 t0s = (float3(0.0f) - box_b - ro) * inv_rd;
    float3 t1s = ( box_b - ro) * inv_rd;
    float3 tsmaller = float3(min(t0s.x, t1s.x), min(t0s.y, t1s.y), min(t0s.z, t1s.z));
    float3 tbigger  = float3(max(t0s.x, t1s.x), max(t0s.y, t1s.y), max(t0s.z, t1s.z));
    float t_near = max(max(tsmaller.x, tsmaller.y), max(tsmaller.z, 0.0f));
    float t_far  = min(tbigger.x, min(tbigger.y, tbigger.z));

    /* Tabletop Plane (y = -0.25f) Soft Contact Shadow */
    float y_table = -0.25f;
    float3 col = bg_col;
    if (rd.y < -1e-4f) {
        float t_tab = (y_table - ro.y) / rd.y;
        if (t_tab > 0.0f) {
            float3 p_tab = ro + rd * t_tab;
            float2 q_sh = abs(float2(p_tab.x, p_tab.z) - float2(-0.05f, -0.05f)) - float2(box_b.x, box_b.z);
            float d_sh = length(max(q_sh, 0.0f)) + min(max(q_sh.x, q_sh.y), 0.0f);
            float sh = smoothstep(-0.06f, 0.36f, d_sh);
            float ao_under = smoothstep(-0.20f, 0.10f, d_sh);
            col = bg_col * (0.66f + 0.34f * (0.55f * sh + 0.45f * ao_under));
        }
    }

    if (t_near >= t_far) {
        return col;
    }

    /* Entry face normal on the cutaway bounding box */
    float3 p_entry = ro + rd * t_near;
    float3 d_face = abs(abs(p_entry) - box_b);
    float3 n_box = float3(0.0f, 1.0f, 0.0f);
    if (d_face.x <= d_face.y && d_face.x <= d_face.z) {
        n_box = float3((p_entry.x >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);
    } else if (d_face.z <= d_face.x && d_face.z <= d_face.y) {
        n_box = float3(0.0f, 0.0f, (p_entry.z >= 0.0f) ? 1.0f : -1.0f);
    }

    /* Raymarch inside the Cutaway Diorama Slab */
    float t = t_near;
    float2 hit = float2(100.0f, 0.0f);
    float t_water_hit = -1.0f;
    float3 n_water_hit = float3(0.0f, 1.0f, 0.0f);
    float water_level = -0.015f;

    for (int i = 0; i < 32; ++i) {
        if (t > t_far) break;
        float3 p = ro + rd * t;
        if (p.y < y_table) break;

        /* Check if ray enters the translucent sea volume */
        float wave_y = water_level
                     + 0.0045f * sin(p.x * 26.0f + time_sec * 2.2f + sin(p.z * 14.0f))
                     * cos(p.z * 24.0f - time_sec * 1.7f);
        float h_terr_here = eval_diorama_terrain_h(float2(p.x, p.z));
        if (t_water_hit < 0.0f && p.y <= wave_y && p.y > h_terr_here) {
            t_water_hit = t;
            if (t <= t_near + 0.008f && abs(n_box.y) < 0.1f) {
                n_water_hit = n_box; /* Sliced vertical sea cross-section wall! */
            } else {
                float wx = 0.11f * cos(p.x * 26.0f + time_sec * 2.2f) * cos(p.z * 24.0f - time_sec * 1.7f);
                float wz = -0.10f * sin(p.x * 26.0f + time_sec * 2.2f) * sin(p.z * 24.0f - time_sec * 1.7f);
                n_water_hit = normalize(float3(-wx, 1.0f, -wz));
            }
        }

        hit = map_diorama_solid(p);
        if (hit.x < 0.0025f) break;
        t += max(hit.x * 0.85f, 0.008f);
    }

    /* Key light from front-top-right so both cutaway walls and terrain are well-illuminated */
    float3 sun_dir = normalize(float3(0.48f, 0.76f, 0.44f));
    float3 cabin_light_pos = float3(-0.14f, 0.16f, -0.10f);
    float3 warm_2400k = float3(2.2f, 1.15f, 0.30f);

    if (hit.x < 0.012f && t <= t_far) {
        float3 pos = ro + rd * t;
        float2 pos_xz = float2(pos.x, pos.z);
        bool is_cut_wall = (t <= t_near + 0.010f && abs(n_box.y) < 0.1f && pos.y < eval_diorama_terrain_h(pos_xz));

        float3 nor = n_box;
        if (!is_cut_wall) {
            const float2 e = float2(0.0025f, -0.0025f);
            const float3 exyy = float3(e.x, e.y, e.y);
            const float3 eyyx = float3(e.y, e.y, e.x);
            const float3 eyxy = float3(e.y, e.x, e.y);
            const float3 exxx = float3(e.x, e.x, e.x);
            nor = normalize(
                exyy * map_diorama_solid(pos + exyy).x +
                eyyx * map_diorama_solid(pos + eyyx).x +
                eyxy * map_diorama_solid(pos + eyxy).x +
                exxx * map_diorama_solid(pos + exxx).x
            );
        }

        float diff = max(dot(nor, sun_dir), 0.0f);
        float amb  = 0.46f + 0.24f * nor.y;
        float3 to_cab = cabin_light_pos - pos;
        float d_cab = length(to_cab);
        float cab_gi = max(dot(nor, to_cab / max(d_cab, 0.05f)), 0.0f) / (1.0f + d_cab * d_cab * 16.0f);

        float3 surf_col = snow_col;
        if (is_cut_wall) {
            /* Geological Sedimentary Rock Strata on Cutaway Wall + Crisp White Snow Crust Lip */
            float h_top = eval_diorama_terrain_h(pos_xz);
            float strata = sin(pos.y * 42.0f + sin(pos.x * 9.0f - pos.z * 8.0f) * 1.8f) * 0.5f + 0.5f;
            float grain = fract(sin(dot(floor(float2(pos.x + pos.z, pos.y) * 95.0f), float2(12.9898f, 78.233f))) * 43758.5f);
            float3 rock_wall = mix(float3(0.28f, 0.27f, 0.25f), float3(0.48f, 0.45f, 0.42f), strata * 0.75f + grain * 0.25f);
            float snow_lip = smoothstep(h_top - 0.024f, h_top - 0.006f, pos.y);
            surf_col = mix(rock_wall, snow_col, snow_lip) * (diff * 0.65f + 0.48f);
        } else if (hit.y < 1.5f) {
            /* Terrain: Slope & Elevation Snow Cover vs. Dark Nordic Coastal Granite */
            float rock_noise = sin(pos.x * 19.0f + sin(pos.z * 17.0f) * 1.6f) * cos(pos.z * 21.0f - pos.x * 13.0f);
            float wet_shore = smoothstep(-0.025f, 0.025f, pos.y);
            float snow_mask = smoothstep(0.86f - snow_cover * 0.35f, 0.94f - snow_cover * 0.25f,
                                         nor.y + 0.06f * rock_noise) * wet_shore;
            float3 granite = mix(float3(0.14f, 0.15f, 0.16f), float3(0.24f, 0.25f, 0.27f), 0.5f + 0.5f * rock_noise);
            float3 albedo = mix(granite, snow_col, snow_mask);
            surf_col = albedo * (diff * 0.68f + float3(0.45f, 0.48f, 0.54f) * amb)
                     + warm_2400k * cab_gi * 0.72f;
        } else if (hit.y < 2.5f) {
            /* Nordic Falu-Red Cabin + Glowing 2400K Windows */
            float3 q = pos - float3(-0.28f, 0.205f, -0.22f);
            bool win_front = (q.z > 0.105f && abs(q.x + 0.030f) < 0.038f && abs(q.y - 0.010f) < 0.032f);
            bool win_side  = (q.x > 0.135f && abs(q.z - 0.010f) < 0.036f && abs(q.y - 0.012f) < 0.040f);
            if (win_front || win_side) {
                float pane = step(0.005f, abs(q.x + 0.030f)) * step(0.005f, abs(q.y - 0.010f));
                surf_col = mix(float3(0.28f, 0.12f, 0.06f), float3(1.0f, 0.86f, 0.48f), 0.35f + 0.65f * pane);
            } else {
                float plank = 0.92f + 0.08f * sin(q.x * 160.0f);
                surf_col = cabin_col * plank * (diff * 0.72f + 0.42f) + warm_2400k * cab_gi * 0.35f;
            }
        } else if (hit.y < 3.5f) {
            /* Snow-Capped Gable Roof */
            float roof_snow = smoothstep(0.20f, 0.50f, nor.y);
            surf_col = mix(float3(0.24f, 0.18f, 0.16f), snow_col, roof_snow) * (diff * 0.65f + 0.48f);
        } else if (hit.y < 4.5f) {
            /* Chimney */
            surf_col = float3(0.10f, 0.11f, 0.13f) * (diff * 0.7f + 0.3f);
        } else {
            /* Wooden Pier + Snow Dusting */
            float pier_snow = smoothstep(0.65f, 0.90f, nor.y) * 0.45f;
            float3 wood = mix(float3(0.34f, 0.24f, 0.16f), snow_col, pier_snow);
            surf_col = wood * (diff * 0.85f + 0.38f) + warm_2400k * cab_gi * 0.75f;
        }

        /* If ray passed through the Translucent Beer-Lambert Sea Volume before hitting seabed/rocks */
        if (t_water_hit > 0.0f && t_water_hit < t) {
            float water_depth = max(t - t_water_hit, 0.01f);
            float3 p_w = ro + rd * t_water_hit;
            float3 absorption = exp(-float3(2.8f, 1.05f, 0.75f) * water_depth * 3.6f);
            float3 deep_sea = sea_col * (0.42f + 0.35f * max(dot(n_water_hit, sun_dir), 0.0f));
            float3 water_body = mix(deep_sea, surf_col * float3(0.75f, 0.92f, 0.98f), absorption);

            /* Fresnel sky reflection + 2400K Warm Cabin Window Reflection on water ripples! */
            float fresnel = pow(1.0f - max(dot(-rd, n_water_hit), 0.0f), 4.0f);
            float2 glow_dir = float2(p_w.x - cabin_light_pos.x, p_w.z - cabin_light_pos.z);
            float glow_path = exp(-abs(glow_dir.x * 0.55f - glow_dir.y * 0.83f) * 9.5f)
                            * exp(-length(glow_dir) * 2.4f);
            float3 refl_rd = rd - 2.0f * dot(rd, n_water_hit) * n_water_hit;
            float ripple_glint = pow(clamp(dot(refl_rd, normalize(cabin_light_pos - p_w)), 0.0f, 1.0f), 14.0f);
            float3 cab_refl = warm_2400k * (glow_path * 0.42f + ripple_glint * 0.65f) * step(0.5f, n_water_hit.y);

            /* Crisp glass-resin edge bevel on the sliced water block corner */
            float cut_rim = (1.0f - step(0.5f, n_water_hit.y)) * 0.14f;
            surf_col = mix(water_body, snow_col * 0.85f, fresnel * 0.38f + cut_rim) + cab_refl;
        }

        col = surf_col;
    }

    return col;
}


fragment float4 tile_fragment(
    VertexOut in [[stage_in]],
    constant FlowGpuFrameUniforms256 &u [[buffer(1)]],
    constant FlowGpuGlyphDesc64 *hud_glyphs [[buffer(2)]],
    constant FlowEngineBus64 *engine_bus [[buffer(3)]]
) {
    float2 p = in.screen_px;
    float w = float(u.s0.screen_w);
    float h = float(u.s0.screen_h);

    /* 2.39:1 CinemaScope Anamorphic Letterbox Early-Out */
    bool has_letterbox = (u.s1.cine_flags & 1U) != 0U;
    float active_h = w / 2.39f;
    float bar_px = max(0.0f, (h - active_h) * 0.5f);
    if (has_letterbox && (p.y < bar_px || p.y > (h - bar_px))) {
        /* Subtle cinema telemetry ruler mark along letterbox border */
        float border_dist = min(abs(p.y - bar_px), abs(p.y - (h - bar_px)));
        float border_line = smoothstep(1.5f, 0.0f, border_dist) * 0.18f;
        return float4(float3(0.02f, 0.05f, 0.09f) * border_line, 1.0f);
    }

    float time_sec = float(u.s0.frame_index) * 0.033f + 1.2f;
    float thermal_norm = clamp((float(u.s0.thermal_kelvin) - 700.0f) / 150.0f, 0.0f, 1.2f);

    /* Normalized screen coordinates with Composer Rule-of-Thirds & Shake offset */
    float2 uv_raw = (p - float2(w * 0.5f, h * 0.5f)) / h;
    float2 uv = uv_raw - float2(u.s1.cam_look_offset[0], u.s1.cam_look_offset[1]) * 0.45f;
    float3 cam_off = float3(u.s1.cam_offset[0], u.s1.cam_offset[1], u.s1.cam_offset[2]);

    /* Physical Focal Length Ratio (35mm reference) */
    float focal_mm = clamp(u.s1.focal_length_mm, 18.0f, 135.0f);
    float focal_ratio = focal_mm / 35.0f;

    /*
     * Continuous Physical Camera Rig Projection (Zero Discontinuous VCam Branching!):
     *   - ro_fg is driven directly by the 2-Jet damped & blended cam_offset.
     *   - fg_fov_z and bg_zoom scale continuously with physical focal_length_mm (18mm..135mm),
     *     magnifying the background gas giant ~2.35x at 135mm while keeping foreground hero scale!
     */
    float bg_zoom = clamp(0.72f + 0.42f * focal_ratio, 0.78f, 2.35f)
                  / (1.0f + clamp(u.s2.lorentz_beta - 0.05f, 0.0f, 0.85f) * 0.12f);
    float3 ro_fg = cam_off;
    float fg_fov_z = clamp(0.92f + (focal_mm - 18.0f) * (1.23f / (135.0f - 18.0f)), 0.88f, 2.20f);
    float roll_angle = u.s0.craft_rot[2] + clamp(cam_off.x * 0.55f, -0.14f, 0.16f) * step(-1.5f, cam_off.z);

    float cr = cos(roll_angle), sr = sin(roll_angle);
    float2 uv_rot = float2(uv.x * cr - uv.y * sr, uv.x * sr + uv.y * cr);

    uint mood_stage = engine_bus[FLOW_BUS_SLOT_MOODBOARD].u32_quads[0].y & 3U;
    float3 col = float3(0.0f);

    if (mood_stage != 0U) {
        float3 sky_col      = unpack_rgb24(engine_bus[FLOW_BUS_SLOT_MOODBOARD].u32_quads[0].z);
        float3 ground_col   = unpack_rgb24(engine_bus[FLOW_BUS_SLOT_MOODBOARD].u32_quads[0].w);
        float3 accent_col   = unpack_rgb24(engine_bus[FLOW_BUS_SLOT_MOODBOARD].u32_quads[1].x);
        float3 backdrop_col = unpack_rgb24(engine_bus[FLOW_BUS_SLOT_MOODBOARD].u32_quads[1].y);
        float fog_cond      = clamp(float(int(engine_bus[FLOW_BUS_SLOT_MOODBOARD].u32_quads[1].z)) * (1.0f / 65536.0f), 0.0f, 1.0f);
        float riv_wipe      = clamp(float(int(engine_bus[FLOW_BUS_SLOT_MOODBOARD].u32_quads[1].w)) * (1.0f / 65536.0f), 0.0f, 1.0f);
        float part_dens     = clamp(float(int(engine_bus[FLOW_BUS_SLOT_MOODBOARD].u32_quads[2].x)) * (1.0f / 65536.0f), 0.1f, 1.0f);
        float sing_gain     = clamp(float(int(engine_bus[FLOW_BUS_SLOT_MOODBOARD].u32_quads[2].y)) * (1.0f / 65536.0f), 0.2f, 2.0f);
        float dio_cut       = clamp(float(int(engine_bus[FLOW_BUS_SLOT_MOODBOARD].u32_quads[2].z)) * (1.0f / 65536.0f), 0.4f, 1.0f);
        float snow_cov      = clamp(float(int(engine_bus[FLOW_BUS_SLOT_MOODBOARD].u32_quads[3].x)) * (1.0f / 65536.0f), 0.0f, 1.0f);
        float2 look_off     = float2(u.s1.cam_look_offset[0], u.s1.cam_look_offset[1]);
        float3 c_rot        = float3(u.s0.craft_rot[0], u.s0.craft_rot[1], u.s0.craft_rot[2]);

        if (mood_stage == 1U) {
            col = eval_mood_window_optics(uv_raw, look_off, time_sec,
                                          sky_col, ground_col, accent_col, backdrop_col,
                                          fog_cond, riv_wipe);
            col = pow(aces_tonemap(col * 1.15f), float3(1.0f / 2.2f));
        } else if (mood_stage == 2U) {
            col = eval_mood_particle_corridor(uv_rot, look_off, time_sec,
                                              sky_col, ground_col, accent_col,
                                              part_dens, sing_gain);
            col = pow(aces_tonemap(col), float3(1.0f / 2.2f));
        } else {
            col = eval_mood_isometric_diorama(uv_raw, look_off, c_rot, time_sec,
                                              sky_col, ground_col, accent_col, backdrop_col,
                                              dio_cut, u.s3.xray_radius, snow_cov);
            col = clamp(col, 0.0f, 1.0f);
        }

        /* Clean Fashion Lookbook 4-Swatch Pill Strip in Upper-Left Corner */
        float2 sw_p = uv_raw - float2(-0.74f, -0.41f);
        if (sw_p.x >= 0.0f && sw_p.x <= 0.16f && abs(sw_p.y) <= 0.014f) {
            uint sw_idx = uint(clamp(sw_p.x * 25.0f, 0.0f, 3.0f));
            float3 sw_c = (sw_idx == 0U) ? sky_col
                        : ((sw_idx == 1U) ? ground_col
                        : ((sw_idx == 2U) ? accent_col : backdrop_col));
            float cell_fx = fract(sw_p.x * 25.0f);
            float border = smoothstep(0.04f, 0.09f, min(cell_fx, 1.0f - cell_fx))
                         * smoothstep(0.014f, 0.010f, abs(sw_p.y));
            col = mix(float3(0.12f), sw_c, border);
        }
    } else {
    float3 ro_bg = float3(0.0f, 0.0f, -500.0f);
    float3 rd_bg = normalize(float3(uv_rot / bg_zoom, 1.15f));
    float3 rd_fg = normalize(float3(uv_rot, fg_fov_z));

    /* 0. Deep Space Background + Starfield */
    float star_seed = fract(sin(dot(floor(uv_rot * 520.0f), float2(12.9898f, 78.233f))) * 43758.5453f);
    float star = step(0.9965f, star_seed) * (0.5f + 1.5f * fract(star_seed * 19.3f));
    col = float3(0.003f, 0.005f, 0.012f) + float3(1.0f, 0.95f, 0.9f) * star;

    float3 sun_dir = normalize(float3(0.62f, 0.42f, -0.66f));

    /*
     * 1. Layer 1 Background: Cassini-Grade Gas Giant + Rayleigh Atmosphere + Shadowed Ice Rings
     *    With Physical Circle-of-Confusion (CoC) Telephoto Bokeh Blur when f/1.4 DoF is active!
     */
    float bg_coc = ((u.s1.cine_flags & 4U) != 0U)
        ? clamp((focal_ratio * focal_ratio) / max(u.s1.aperture_fstop * 4.5f, 1.0f), 0.0f, 1.0f)
        : 0.0f;

    float3 sphere_center = float3(0.0f, -0.05f, 2500.0f);
    float sphere_r = 480.0f;

    float3 oc = ro_bg - sphere_center;
    float b = dot(oc, rd_bg);
    float c = dot(oc, oc) - sphere_r * sphere_r;
    float disc = b * b - c;

    float dist_to_axis = sqrt(max(0.0f, dot(oc, oc) - b * b));
    float halo_width = sphere_r * (0.06f + 0.08f * bg_coc);
    float atmo_halo = exp(-max(0.0f, dist_to_axis - sphere_r * 0.98f) / halo_width);
    col += float3(0.18f, 0.48f, 1.15f) * atmo_halo * 0.38f;

    bool hit_sphere = false;
    bool hit_ring = false;
    float t_sphere = 1e9f;
    if (disc > 0.0f) {
        float t0 = -b - sqrt(disc);
        if (t0 > 0.0f) {
            hit_sphere = true;
            t_sphere = t0;
            float3 p_surf = ro_bg + t0 * rd_bg;
            float3 n_surf = normalize(p_surf - sphere_center);

            /* Bokeh DoF attenuates high-frequency zonal bands into creamy telephoto gradients */
            float freq_atten = 1.0f - 0.65f * bg_coc;
            float zonal = sin(n_surf.y * 42.0f * freq_atten + sin(n_surf.x * 14.0f + n_surf.z * 9.0f) * 0.55f);
            float storm = smoothstep(0.6f, 0.95f, sin(n_surf.x * 8.0f) * cos(n_surf.y * 12.0f));
            float3 albedo = mix(float3(0.78f, 0.58f, 0.38f), float3(0.28f, 0.20f, 0.16f), zonal * 0.5f + 0.5f);
            albedo = mix(albedo, float3(0.92f, 0.42f, 0.22f), storm * 0.5f);

            float ndotl = dot(n_surf, sun_dir);
            float diff = max(ndotl, 0.0f);
            float terminator = exp(-abs(ndotl) * 6.0f) * smoothstep(-0.2f, 0.3f, ndotl);
            float rim = pow(1.0f - max(dot(-rd_bg, n_surf), 0.0f), 3.5f);

            float3 planet_col = albedo * (diff * 1.15f + 0.015f)
                              + float3(1.1f, 0.32f, 0.08f) * terminator * 0.45f
                              + float3(0.22f, 0.58f, 1.25f) * rim * (diff + 0.15f);

            /* Soften limb under shallow DoF Circle-of-Confusion */
            float limb_soft = smoothstep(0.0f, sphere_r * (0.04f + 0.18f * bg_coc), sqrt(disc));
            col = mix(col, planet_col, limb_soft);
        }
    }

    /* Cassini Ice Rings with Analytic Planetary Penumbra Shadow & Bokeh Softening */
    float ring_r_in = sphere_r * 1.32f;
    float ring_r_out = sphere_r * 2.55f;
    float3 ring_n = normalize(float3(0.18f, 0.94f, -0.28f));
    float denom = dot(rd_bg, ring_n);
    if (abs(denom) > 1e-4f) {
        float t_ring = dot(sphere_center - ro_bg, ring_n) / denom;
        if (t_ring > 0.0f && (!hit_sphere || t_ring < t_sphere)) {
            float3 p_ring = ro_bg + t_ring * rd_bg;
            float r_dist = length(p_ring - sphere_center);
            if (r_dist >= ring_r_in && r_dist <= ring_r_out) {
                float u_r = (r_dist - ring_r_in) / (ring_r_out - ring_r_in);
                float cassini_div = smoothstep(0.01f, 0.04f + 0.04f * bg_coc, abs(u_r - 0.58f));
                float ring_detail = mix(0.55f + 0.45f * sin(r_dist * 0.08f) * cos(r_dist * 0.21f), 0.78f, bg_coc);
                float3 oc_sh = p_ring - sphere_center;
                float b_sh = dot(oc_sh, sun_dir);
                float c_sh = dot(oc_sh, oc_sh) - sphere_r * sphere_r;
                float disc_sh = b_sh * b_sh - c_sh;
                float shadow = (disc_sh > 0.0f && b_sh < 0.0f)
                    ? clamp(1.0f - sqrt(disc_sh) / (sphere_r * 0.15f), 0.04f, 1.0f)
                    : 1.0f;
                float3 ring_col = mix(float3(0.85f, 0.78f, 0.66f), float3(0.55f, 0.68f, 0.82f), u_r)
                                * ring_detail * cassini_div * shadow;
                col = mix(col, ring_col, 0.82f * cassini_div);
                if (cassini_div > 0.1f) {
                    hit_ring = true;
                }
            }
        }
    }

    /*
     * 2. Layer 0 Foreground: Micro-Voxel Carved Citadel + Smooth-CSG Liquid + Chase Interceptor
     *    Analytic Bounding-Sphere Entry Pre-Filter relative to ro_fg
     */
    float3 b_rock  = float3(-1.05f, -0.12f, 4.8f) - ro_fg;
    float3 b_fluid = float3(0.95f, 0.15f, 4.4f) - ro_fg;
    float3 b_ship  = float3(0.38f, 0.26f, 2.35f) - ro_fg;

    float proj_rock  = dot(b_rock, rd_fg);
    float disc_rock  = proj_rock * proj_rock - (dot(b_rock, b_rock) - 1.12f * 1.12f);
    float proj_fluid = dot(b_fluid, rd_fg);
    float disc_fluid = proj_fluid * proj_fluid - (dot(b_fluid, b_fluid) - 0.70f * 0.70f);
    float proj_ship  = dot(b_ship, rd_fg);
    float disc_ship  = (u.s1.vcam_id == 1U)
        ? (proj_ship * proj_ship - (dot(b_ship, b_ship) - 0.65f * 0.65f))
        : -1.0f;

    float fg_depth = 100.0f;

    if (disc_rock > 0.0f || disc_fluid > 0.0f || disc_ship > 0.0f) {
        float ang = time_sec * 0.25f + 0.6f;
        float flux_push = clamp(u.s2.flux_pressure_ratio * 0.04f, -0.06f, 0.12f);
        MicroSceneCtx ctx;
        ctx.rot_cs = float2(cos(ang), sin(ang));
        ctx.drop1 = float3(sin(time_sec * 1.3f) * (0.28f + flux_push), cos(time_sec * 1.1f) * 0.20f, 0.0f);
        ctx.drop2 = float3(-sin(time_sec * 0.9f) * (0.26f + flux_push), sin(time_sec * 1.5f) * 0.19f, 0.12f);
        ctx.drop3 = float3(0.0f, -0.25f + sin(time_sec * 2.0f) * 0.11f + flux_push * 0.5f, -0.11f);
        ctx.k_visc      = 0.24f + 0.08f * thermal_norm + clamp(u.s3.xray_radius, 0.0f, 0.35f);
        ctx.wave_freq   = clamp(u.s2.wave_phase_freq, 12.0f, 42.0f);
        ctx.wave_phase  = time_sec * 6.5f;
        ctx.eval_rock   = (disc_rock > 0.0f);
        ctx.eval_fluid  = (disc_fluid > 0.0f);
        ctx.eval_ship   = (disc_ship > 0.0f);

        float max_dist = (u.s3.layer0_radius > 1.0f) ? min(12.0f, u.s3.layer0_radius) : 12.0f;
        float t_start = max_dist;
        float t_end = 0.5f;
        if (ctx.eval_rock) {
            float s = sqrt(disc_rock);
            t_start = min(t_start, max(0.5f, proj_rock - s));
            t_end   = max(t_end,   min(max_dist, proj_rock + s));
        }
        if (ctx.eval_fluid) {
            float s = sqrt(disc_fluid);
            t_start = min(t_start, max(0.5f, proj_fluid - s));
            t_end   = max(t_end,   min(max_dist, proj_fluid + s));
        }
        if (ctx.eval_ship) {
            float s = sqrt(disc_ship);
            t_start = min(t_start, max(0.5f, proj_ship - s));
            t_end   = max(t_end,   min(max_dist, proj_ship + s));
        }

        int max_steps = (u.s3.raymarch_max_steps > 0U && u.s3.raymarch_max_steps <= 32U) ? 8 : 11;
        float t = t_start;
        float2 hit = float2(100.0f, 0.0f);
        for (int i = 0; i < max_steps; ++i) {
            hit = map_micro_scene(ro_fg + rd_fg * t, ctx);
            if (hit.x < 0.003f || t > t_end) break;
            t += hit.x;
        }

        if (hit.x < 0.008f && t <= t_end) {
            fg_depth = t;
            float3 pos = ro_fg + rd_fg * t;
            float3 nor = calc_sdf_normal(pos, ctx);
            float shadow = calc_sdf_soft_shadow(pos + nor * 0.015f, sun_dir, ctx);
            float ao = calc_sdf_ao(pos, nor, ctx);

            /* 1-Bounce Blackbody GI from the 800K laser-carved molten cavity */
            float3 cavity_world = float3(-0.62f, -0.05f, 2.92f);
            float3 to_cavity = cavity_world - pos;
            float cav_dist = max(length(to_cavity), 0.22f);
            float gi_bounce = max(dot(nor, to_cavity / cav_dist), 0.0f) / (1.0f + cav_dist * cav_dist * 2.2f);
            float3 blackbody_col = float3(2.8f, 0.72f, 0.14f) * (0.75f + 0.6f * thermal_norm);

            if (hit.y < 2.5f) {
                float bevel_ao = 1.0f, cell_hash = 0.0f, vein = 0.0f;
                nor = apply_micro_voxel_lattice(pos, nor, ctx, bevel_ao, cell_hash, vein);
                ao *= bevel_ao;
                float3 hal = normalize(sun_dir - rd_fg);
                float diff = max(dot(nor, sun_dir), 0.0f) * shadow;
                float3 base_vox = mix(float3(0.14f, 0.16f, 0.19f), float3(0.24f, 0.26f, 0.30f), cell_hash);
                float3 rock_albedo = mix(base_vox, float3(1.0f, 0.78f, 0.22f), vein);
                float spec = pow(max(dot(nor, hal), 0.0f), mix(20.0f, 72.0f, vein)) * shadow * bevel_ao;

                float3 surf_col = rock_albedo * (diff * 1.4f + float3(0.06f, 0.08f, 0.12f) * ao)
                                + float3(1.0f, 0.92f, 0.75f) * spec * (0.3f + 0.7f * vein)
                                + blackbody_col * gi_bounce * ao * 0.95f;

                if (hit.y > 1.5f) {
                    /* Molten 800K..1800K CSG Carved Rim & Internal Vein Emission */
                    float melt_glow = 0.45f + 0.65f * vein;
                    surf_col += blackbody_col * (1.35f * melt_glow);
                }
                col = surf_col;
            } else if (hit.y < 3.5f) {
                /*
                 * Unified Optical Interface (Ripple <-> Ray-Tracing):
                 * Surface wave gradient nor = grad(f_rippled) bends incident ray via Snell's law
                 * (refract) and focuses curvature caustics!
                 */
                float3 rd_refr = refract(rd_fg, nor, 1.0f / 1.333f);
                float3 hal = normalize(sun_dir - rd_fg);
                float diff = max(dot(nor, sun_dir), 0.0f) * shadow;
                float fresnel = pow(1.0f - max(dot(-rd_fg, nor), 0.0f), 4.0f);
                float thickness = clamp(0.45f - map_micro_scene(pos + rd_refr * 0.28f, ctx).x, 0.05f, 0.75f);
                float3 absorption = exp(-float3(1.8f, 0.35f, 0.08f) * thickness * 3.2f);
                float caustic = pow(clamp(dot(rd_refr, sun_dir) * 0.5f + 0.5f, 0.0f, 1.0f), 6.0f) * 0.55f;
                float3 refr_bg = col * absorption + float3(0.25f, 0.85f, 1.25f) * caustic * shadow;
                float spec_fluid = pow(max(dot(nor, hal), 0.0f), 96.0f) * shadow;
                float3 fluid_subsurf = float3(0.08f, 0.65f, 0.95f) * (diff * 0.6f + 0.25f * ao)
                                     + blackbody_col * gi_bounce * 0.6f;
                col = mix(refr_bg + fluid_subsurf * 0.45f, float3(0.55f, 0.82f, 1.0f), fresnel * 0.75f)
                    + float3(1.8f, 1.9f, 2.0f) * spec_fluid;
            } else if (hit.y < 4.5f) {
                /*
                 * Interceptor Haute-Couture Luxury Finishes (FlowFashionSkin64 0..3):
                 *   0: Doppler Sapphire (Thin-film phase-shifted sapphire/violet pearl)
                 *   1: Case-Hardened Blue Gem (800K-1800K thermal quench blue-gem / gold patina)
                 *   2: Kintsugi Gold Damascus (Folded obsidian Damascus steel + 24K molten gold veins)
                 *   3: Celestial Subsurface Jade (Translucent Beer-Lambert imperial jade)
                 */
                float3 hal = normalize(sun_dir - rd_fg);
                float diff = max(dot(nor, sun_dir), 0.0f) * shadow;
                float spec = pow(max(dot(nor, hal), 0.0f), 72.0f) * shadow;
                float rim  = pow(1.0f - max(dot(-rd_fg, nor), 0.0f), 3.0f);
                uint fclass = u.s3.active_finish_class & 3U;
                float3 base_albedo = float3(0.75f, 0.82f, 0.90f);
                float3 rim_tint    = float3(0.20f, 0.85f, 1.0f);
                float3 emissive_vein = float3(0.0f);

                if (fclass == 0U) {
                    /* Doppler Sapphire: thin-film sapphire -> violet -> cyan phase shift */
                    base_albedo = mix(float3(0.18f, 0.48f, 0.96f), float3(0.68f, 0.22f, 0.96f), rim);
                    rim_tint    = float3(0.30f, 0.92f, 1.0f);
                } else if (fclass == 1U) {
                    /* Case-Hardened 800K Blue Gem: tempered gold & blaze cyan-blue patina */
                    float quench = smoothstep(0.25f, 0.75f, sin(pos.x * 18.0f + pos.z * 14.0f) * 0.5f + 0.5f);
                    base_albedo = mix(float3(0.88f, 0.68f, 0.24f), float3(0.12f, 0.62f, 1.0f), quench);
                    rim_tint    = float3(0.45f, 0.85f, 1.0f);
                } else if (fclass == 2U) {
                    /* Kintsugi Gold Damascus: folded obsidian steel + 24K molten gold veins */
                    float damascus = abs(sin(pos.z * 36.0f + sin(pos.x * 24.0f) * 2.2f));
                    float gold_v = smoothstep(0.84f, 0.95f, damascus);
                    base_albedo = mix(float3(0.14f, 0.16f, 0.22f), float3(1.0f, 0.82f, 0.22f), gold_v);
                    rim_tint    = float3(1.0f, 0.78f, 0.25f);
                    emissive_vein = float3(1.2f, 0.85f, 0.20f) * gold_v * 0.55f;
                } else {
                    /* Celestial Subsurface Jade: imperial emerald subsurface scatter */
                    base_albedo = float3(0.12f, 0.76f, 0.52f);
                    rim_tint    = float3(0.35f, 1.0f, 0.75f);
                    emissive_vein = float3(0.08f, 0.45f, 0.30f) * (1.0f - diff) * 0.45f;
                }

                float flex_key = 1.0f + clamp(u.s3.flex_spotlight, 0.0f, 1.5f) * 0.65f;
                float3 hull_col = base_albedo * (diff * 1.3f * flex_key + 0.18f * ao)
                                + rim_tint * rim * (0.68f * flex_key)
                                + float3(1.2f, 1.2f, 1.3f) * spec * flex_key
                                + emissive_vein;
                col = hull_col;
            } else {
                /* Interceptor Twin Ion Thruster Plasma Core */
                float core_hot = clamp(1.0f - nor.z, 0.2f, 1.0f);
                col = float3(0.45f, 2.2f, 3.8f) * core_hot + float3(2.6f, 0.5f, 1.8f) * thermal_norm;
            }
        }
    }

    /*
     * 3. Unreal CineCamera Post-Processing: Anamorphic Lens Flares, 180-deg Shutter Blur,
     *    EV Auto-Exposure, Edge Chromatic Dispersion, Cos^4 Optical Vignette,
     *    and 4-Phase Dopamine Jackpot Audiovisual Payoff (Slice 3)
     */
    if ((u.s1.cine_flags & 2U) != 0U) {
        /* Horizontal Blue Anamorphic Streak Flare anchored to the 800K Molten Cavity & Thrusters */
        float2 cavity_uv = (u.s1.vcam_id == 2U) ? float2(-0.34f, -0.02f) : float2(-0.22f, -0.03f);
        float dy_cav = abs(uv_rot.y - cavity_uv.y);
        float dx_cav = abs(uv_rot.x - cavity_uv.x);
        float streak1 = exp(-dy_cav * 110.0f) * exp(-dx_cav * 2.8f) * (0.55f + 0.45f * thermal_norm);
        col += float3(0.18f, 0.58f, 1.65f) * streak1 * 0.65f;

        if (u.s1.vcam_id == 1U) {
            /* Secondary cyan/blue anamorphic streak across lower-right Interceptor ion thrusters */
            float2 thr_uv = float2(0.08f, 0.11f);
            float streak2 = exp(-abs(uv_rot.y - thr_uv.y) * 140.0f) * exp(-abs(uv_rot.x - thr_uv.x) * 3.2f);
            col += float3(0.25f, 0.85f, 1.85f) * streak2 * 0.65f;
        }
    }

    /* 180-Degree Shutter Peripheral Star-Streak Motion Blur (strictly in background space!) */
    if ((u.s1.cine_flags & 8U) != 0U && fg_depth > 50.0f && !hit_sphere && !hit_ring) {
        float streak_ang = atan2(uv.y, uv.x);
        float streak_r = length(uv);
        float ang_bin = floor(streak_ang * 110.0f);
        float ang_hash = fract(sin(ang_bin * 91.7f) * 43758.5f);
        float streak_val = cos( fract(streak_ang * 110.0f) * 6.28318f );
        float3 svel = float3(u.s2.shutter_vel[0], u.s2.shutter_vel[1], u.s2.shutter_vel[2]);
        float speed_factor = clamp((u.s0.craft_speed + length(svel) * 0.25f) / 500.0f, 0.25f, 2.2f);

        /* Segment streaks radially so each streak is a finite 180-deg shutter star trail */
        float rad_coord = streak_r * (14.0f / max(speed_factor, 0.5f)) + ang_hash * 6.0f;
        float rad_cell = fract(rad_coord);
        float rad_id_hash = fract(sin((ang_bin * 17.3f + floor(rad_coord)) * 37.1f) * 19753.3f);
        float trail_env = smoothstep(0.05f, 0.35f, rad_cell) * smoothstep(0.95f, 0.45f, rad_cell) * rad_cell;
        float radial_band = smoothstep(0.24f, 0.48f, streak_r) * step(0.84f, ang_hash) * step(0.45f, rad_id_hash);
        float streak_mask = smoothstep(0.88f, 0.995f, streak_val) * radial_band * trail_env;
        float3 streak_col = (u.s0.stage_layer == 1U) ? float3(0.40f, 0.88f, 1.30f) : float3(0.60f, 0.90f, 1.40f);
        col += streak_col * streak_mask * speed_factor * 0.75f;
    }

    /* EV Auto-Exposure Gain, Relativistic Doppler Spectral Shift & Natural Cos^4 Optical Vignette */
    if (u.s1.collision_hit != 0U || u.s3.shockwave_ripple > 0.01f) {
        float shock_r = length(uv_rot - float2(-0.22f, -0.03f));
        float shock_env = exp(-abs(shock_r - (0.09f + 0.12f * u.s3.shockwave_ripple)) * 55.0f)
                        * clamp(u.s3.shockwave_ripple + float(u.s1.collision_hit) * 0.45f, 0.0f, 1.0f);
        col += float3(0.28f, 0.88f, 1.45f) * shock_env * 0.65f;
    }
    if (u.s3.dopamine_phase != 0U) {
        /* 4-Phase Dopamine Jackpot Payoff: Suspense Vignette -> Hitstop -> Catharsis Boom -> 135mm Flex */
        float r_jack = length(uv_rot - float2(-0.22f, -0.03f));
        float boom_ring = exp(-abs(r_jack - (0.06f + 0.28f * (1.0f - u.s3.catharsis_bloom))) * 38.0f)
                        * clamp(u.s3.catharsis_bloom, 0.0f, 1.5f);
        col += float3(1.0f, 0.78f, 0.22f) * boom_ring * 0.85f;
        float susp_atten = 1.0f - clamp(u.s3.suspense_vignette, 0.0f, 0.85f) * smoothstep(0.18f, 0.62f, length(uv_raw));
        col *= susp_atten;
    }
    if (abs(u.s2.doppler_factor - 1.0f) > 0.05f) {
        float dopp_shift = clamp(u.s2.doppler_factor - 1.0f, -0.35f, 0.65f);
        col *= float3(1.0f - 0.18f * dopp_shift, 1.0f + 0.04f * dopp_shift, 1.0f + 0.28f * dopp_shift);
    }
    float r2_vig = dot(uv_raw, uv_raw);
    float cos4_vig = 1.0f / ((1.0f + r2_vig * 0.42f) * (1.0f + r2_vig * 0.42f));
    col *= clamp(u.s1.exposure_gain, 0.70f, 1.35f) * cos4_vig;

    /* Lateral Edge Chromatic Dispersion */
    if ((u.s1.cine_flags & 32U) != 0U) {
        float ca_edge = smoothstep(0.25f, 0.85f, length(uv_raw)) * 0.06f;
        col.x *= (1.0f + ca_edge);
        col.z *= (1.0f - ca_edge * 0.8f);
    }

    /* ACES Filmic Tone Mapping + Gamma 2.2 */
    col = pow(aces_tonemap(col), float3(1.0f / 2.2f));

    /*
     * 4. VCam-Specific Overlays:
     *    - VCAM_COCKPIT_TACTICAL (id=3): Armored Canopy Struts + Compiled SDD SVG Tactical HUD
     *    - Director / Chase VCams (id=0..2): Subtle CineCamera Optical Viewfinder Framing
     */
    if ((u.s1.cine_flags & 64U) != 0U) {
        /* Armored Cockpit Canopy Frame Struts at outer corners */
        float2 auv = abs(uv_raw);
        float canopy_strut = min(
            abs(auv.x - 0.62f - auv.y * 0.35f),
            abs(auv.y - 0.44f - auv.x * 0.12f)
        );
        if (auv.x > 0.62f + auv.y * 0.35f || auv.y > 0.46f) {
            col *= 0.14f;
        } else if (canopy_strut < 0.012f) {
            float strut_bevel = smoothstep(0.0f, 0.012f, canopy_strut);
            col = mix(float3(0.08f, 0.10f, 0.13f), col, strut_bevel);
        }

        /* Compiled SDD SVG Glyph 101 (glyph_reticle_prograde) + Pitch Ladder */
        float d_center = length(uv);
        float d_svg_reticle = eval_sdd_glyph(hud_glyphs[0], uv * 14.0f) / 14.0f;
        float reticle_ring = abs(d_center - 0.042f);
        float d_reticle = min(d_svg_reticle, (d_center > 0.016f ? reticle_ring : 1.0f));

        /* Pitch ladder horizontal bars at y = +-0.09, +-0.18 */
        float2 pl_uv = abs(uv);
        float d_ladder = min(abs(pl_uv.y - 0.09f), abs(pl_uv.y - 0.18f));
        if (pl_uv.x > 0.09f && pl_uv.x < 0.16f) {
            d_reticle = min(d_reticle, d_ladder);
        }
        float hud_alpha = 1.0f - smoothstep(0.0008f, 0.0020f, d_reticle);
        col = mix(col, float3(0.0f, 1.0f, 0.80f), hud_alpha * 0.88f);

        /* Crisp L-Bracket Target Lock Box around Foreground Micro-Voxel Citadel + SDD Grade-S Badge (Glyph 103) */
        float2 tgt_box = abs(uv - float2(-0.22f, -0.02f));
        bool in_corner = (tgt_box.x > 0.095f || tgt_box.y > 0.095f) && (tgt_box.x <= 0.142f && tgt_box.y <= 0.142f);
        float d_box = min(abs(tgt_box.x - 0.140f), abs(tgt_box.y - 0.140f));
        if (in_corner && d_box < 0.0022f) {
            col = mix(col, float3(1.0f, 0.84f, 0.15f), 0.88f);
        }
        float2 badge_uv = (uv - float2(-0.055f, -0.145f)) * 42.0f;
        if (max(abs(badge_uv.x), abs(badge_uv.y)) < 1.2f) {
            float d_badge = eval_sdd_glyph(hud_glyphs[2], badge_uv);
            float badge_a = 1.0f - smoothstep(0.04f, 0.09f, d_badge);
            col = mix(col, float3(1.0f, 0.84f, 0.15f), badge_a * 0.90f);
        }

        /* Compiled SDD SVG Glyph 102 (glyph_alert_800k) in Upper-Right HUD */
        float2 aw = (uv_raw - float2(0.41f, -0.30f)) * 22.0f;
        if (max(abs(aw.x), abs(aw.y)) < 1.2f) {
            float d_alert = eval_sdd_glyph(hud_glyphs[1], aw);
            float alert_a = 1.0f - smoothstep(0.035f, 0.075f, d_alert);
            col = mix(col, float3(1.0f, 0.28f, 0.08f), alert_a * 0.92f);
        }

        /* Compiled SDD SVG Glyph 104 (glyph_ship_hull_profile) in Lower-Left Tactical Status */
        float2 sw_hud = (uv_raw - float2(-0.42f, 0.32f)) * 24.0f;
        if (max(abs(sw_hud.x), abs(sw_hud.y)) < 1.2f) {
            float d_ship_hud = eval_sdd_glyph(hud_glyphs[3], sw_hud);
            float ship_a = 1.0f - smoothstep(0.035f, 0.075f, d_ship_hud);
            col = mix(col, float3(0.40f, 0.82f, 1.0f), ship_a * 0.88f);
        }
    } else {
        /* Subtle Director Viewfinder Corner Brackets & Crosshair Ticks */
        float2 auv = abs(uv_raw);
        float d_tick = min(abs(auv.x - 0.28f), abs(auv.y - 0.16f));
        if (auv.x > 0.25f && auv.x < 0.29f && auv.y > 0.14f && auv.y < 0.17f && d_tick < 0.0012f) {
            col = mix(col, float3(0.45f, 0.80f, 1.0f), 0.45f);
        }
    }

    /* Ricardian Thermal & Optical Telemetry Gauge (Bottom-Right) */
    float gauge_y = has_letterbox ? 0.31f : 0.36f;
    float2 hud_gauge = uv_raw - float2(0.35f, gauge_y);
    if (hud_gauge.x >= 0.0f && hud_gauge.x <= 0.12f && abs(hud_gauge.y) < 0.010f) {
        float gauge_frac = hud_gauge.x / 0.12f;
        float temp_frac = clamp((float(u.s0.thermal_kelvin) - 700.0f) / 150.0f, 0.0f, 1.0f);
        float3 gauge_col = (u.s1.thermal_shutdown == 1U)
            ? float3(1.0f, 0.15f, 0.1f)
            : (temp_frac > 0.6f ? float3(1.0f, 0.75f, 0.1f) : float3(0.2f, 0.95f, 0.4f));
        col = mix(col, (gauge_frac <= temp_frac) ? gauge_col : float3(0.12f, 0.14f, 0.18f), 0.85f);
        if (abs(gauge_frac - 0.667f) < 0.005f) col = float3(1.0f, 0.2f, 0.2f);
    }
    } /* End of mood_stage == 0U space scene */

    /*
     * 5. Declarative 64-Byte FLOW SVG-UI Frontend DOM & Liquid-Glass Director Bar
     *    Uses Wave64 8x8 spatial tile bitmask pre-filtering so 78% of 4K SIMD groups
     *    skip the DOM buffer completely in O(1)!
     */
    if (abs(uv_raw.y) > 0.21f) {
        float uv_ui_y = (has_letterbox && uv_raw.y > 0.18f) ? (uv_raw.y + 0.065f) : uv_raw.y;
        uint wc = uint(clamp((uv_raw.x + 0.5f) * 8.0f, 0.0f, 7.0f));
        uint wr = uint(clamp((uv_ui_y + 0.375f) * 10.666667f, 0.0f, 7.0f));
        ulong px_wave64 = 1UL << (wr * 8U + wc);

        for (uint i = 0U; i < 8U; ++i) {
            constant FlowSvgUiNode64 &nd = engine_bus[FLOW_BUS_SLOT_SVG_UI_0 + i].svg_node;
            if ((nd.tile_mask_wave64 & px_wave64) == 0UL) continue;

            float2 center_uv = float2(float(nd.offset_x_q16), float(nd.offset_y_q16)) * (1.0f / 65536.0f);
            if (has_letterbox && center_uv.y > 0.25f) {
                center_uv.y -= 0.065f;
            }
            float scale_f = clamp(float(nd.scale_jet_v_q16) * (1.0f / 65536.0f), 0.75f, 1.40f);
            float2 half_sz = float2(float(nd.half_w_q16), float(nd.half_h_q16)) * (scale_f / 65536.0f);
            float2 lp = uv_raw - center_uv;

            if (abs(lp.x) > half_sz.x + 0.015f || abs(lp.y) > half_sz.y + 0.015f) {
                continue;
            }

            float bound_norm = clamp(float(nd.bound_value_q16) * (1.0f / 65536.0f), 0.0f, 1.0f);
            bool is_hovered  = (nd.style_and_state & FLOW_SVG_STATE_HOVER_BIT) != 0U;
            uint rgb24       = (nd.style_and_state >> 8U) & 0x00FFFFFFU;
            float3 fill_rgb  = float3(float((rgb24 >> 16U) & 0xFFU),
                                      float((rgb24 >> 8U)  & 0xFFU),
                                      float(rgb24          & 0xFFU)) * (1.0f / 255.0f);

            if (nd.prim_type == FLOW_SVG_PRIM_RECT_GLASS) {
                float rx = float(nd.corner_radius_q16) * (1.0f / 65536.0f);
                float2 qd = abs(lp) - half_sz + rx;
                float sdf = min(max(qd.x, qd.y), 0.0f) + length(max(qd, 0.0f)) - rx;
                if (sdf < 0.0018f) {
                    /*
                     * Physical Liquid-Glass Optics (Zero iOS 7 Flat Darkening!):
                     *   1. 3D Lenticular Pillow Bevel Curvature & TrueColor Beer-Lambert Fill
                     *   2. 2-Jet Surface Ripple Caustics + Parallax Anisotropic Silk Sheen
                     */
                    float bevel = 1.0f - clamp(-sdf * 320.0f, 0.0f, 1.0f);
                    float jet_e = clamp(abs(float(nd.scale_jet_d1_q16)) * (1.0f / 16384.0f), 0.0f, 1.2f)
                                + (is_hovered ? 0.36f : 0.12f);
                    float ripple = (0.5f + 0.5f * sin(length(lp) * 210.0f - time_sec * 12.0f)) * jet_e * 0.42f;
                    float sheen = clamp(1.0f - abs(lp.x + lp.y * 0.7f - u.s1.cam_look_offset[0] * 0.3f) * 28.0f, 0.0f, 0.42f);
                    float rim = 1.0f - smoothstep(0.0f, 0.0018f, abs(sdf));
                    float3 transmittance = clamp(float3(1.0f) - (float3(1.0f) - fill_rgb) * (0.18f + 0.65f * bevel), 0.15f, 1.0f);
                    float3 glass_bg = col * mix(float3(0.92f), transmittance, 0.78f)
                                    + fill_rgb * (0.16f + 0.24f * bound_norm + 0.26f * bevel + ripple + sheen);
                    col = mix(col, glass_bg, (sdf < 0.0f) ? (is_hovered ? 0.86f : 0.76f) : 0.0f);
                    col = mix(col, fill_rgb * (is_hovered ? 1.45f : 1.08f), rim * 0.88f);
                    /* Zero-Font-Atlas Stretchy LaTeX SDF (\frac{A}{\sqrt{B^2}}) on HUD Card (i == 7U) */
                    if (i == 7U && sdf < -0.002f) {
                        float2 mp = lp / max(half_sz, float2(0.001f));
                        float d_frac = sd_segment_2d(mp, float2(-0.55f, 0.0f), float2(0.55f, 0.0f));
                        float d_sqrt = min(sd_segment_2d(mp, float2(-0.42f, 0.38f), float2(-0.28f, 0.58f)),
                                       min(sd_segment_2d(mp, float2(-0.28f, 0.58f), float2(-0.14f, 0.18f)),
                                           sd_segment_2d(mp, float2(-0.14f, 0.18f), float2(0.48f, 0.18f))));
                        float d_sup2 = abs(length(mp - float2(0.32f, 0.34f)) - 0.10f);
                        float d_ltx  = min(d_frac, min(d_sqrt, d_sup2));
                        if (d_ltx < 0.065f) {
                            col = mix(col, float3(1.0f, 0.98f, 0.88f), 1.0f - smoothstep(0.015f, 0.065f, d_ltx));
                        }
                    }
                }
            } else if (nd.prim_type == FLOW_SVG_PRIM_CIRCLE_RING) {
                float r = length(lp);
                float d_ring = abs(r - half_sz.x) - (half_sz.x - half_sz.y) * 0.5f;
                if (d_ring < 0.0015f) {
                    float ang = fract(atan2(lp.y, lp.x) * (1.0f / 6.2831853f) + 0.25f);
                    float3 ring_col = (ang <= bound_norm) ? fill_rgb : float3(0.14f, 0.18f, 0.24f);
                    float alpha = 1.0f - smoothstep(-0.0005f, 0.0015f, d_ring);
                    col = mix(col, ring_col, alpha * 0.92f);
                }
            } else if (i == 6U && nd.prim_type == FLOW_SVG_PRIM_NUM_READOUT) {
                uint val = uint(clamp(nd.bound_value_q16, 0, 999));
                uint d0 = (val / 100U) % 10U;
                uint d1 = (val / 10U)  % 10U;
                uint d2 = val % 10U;
                float2 dp = lp / max(half_sz, float2(0.001f));
                float seg_d = min(eval_7seg_digit_sdf(float2(dp.x * 1.6f + 0.95f, dp.y), d0),
                              min(eval_7seg_digit_sdf(float2(dp.x * 1.6f,         dp.y), d1),
                                  eval_7seg_digit_sdf(float2(dp.x * 1.6f - 0.95f, dp.y), d2)));
                if (seg_d < 0.08f) {
                    float a_seg = 1.0f - smoothstep(0.0f, 0.08f, seg_d);
                    col = mix(col, float3(0.0f, 1.0f, 0.82f), a_seg * 0.92f);
                }
            }
        }
    }

    /*
     * 6. Scripted Story Finisher ("劇情殺") & Scene Transition ("過渡動畫") GPU Wipe
     *    Reads FlowNarrativeDirector64 from engine_bus[FLOW_BUS_SLOT_STORY_DIRECTOR]:
     *      - u32_quads[0].y: bits[0..15] = director_phase, bits[16..31] = transition_style
     *      - u32_quads[1].w: wipe_progress_q16 [0..65536]
     */
    uint dir_meta = engine_bus[FLOW_BUS_SLOT_STORY_DIRECTOR].u32_quads[0].y;
    uint dir_phase = dir_meta & 0xFFFFU;
    if (dir_phase != 0U) {
        uint trans_style = (dir_meta >> 16U) & 3U;
        float wipe_p = clamp(float(int(engine_bus[FLOW_BUS_SLOT_STORY_DIRECTOR].u32_quads[1].w)) * (1.0f / 65536.0f), 0.0f, 1.0f);
        if (wipe_p > 0.001f) {
            float mask_w = wipe_p;
            if (trans_style == 1U) {
                /* Liquid-Glass Wave Sweep */
                float wave_x = uv_raw.x + 0.06f * sin(uv_raw.y * 18.0f + time_sec * 10.0f) + 0.55f;
                mask_w = smoothstep(wave_x - 0.08f, wave_x + 0.08f, wipe_p * 1.15f);
            } else if (trans_style == 2U) {
                /* Iris Aperture Close/Open */
                float rad_uv = length(uv);
                float iris_r = (1.0f - wipe_p) * 0.68f;
                mask_w = smoothstep(iris_r - 0.025f, iris_r + 0.025f, rad_uv);
            } else if (trans_style == 3U) {
                /* Relativistic Warp Tunnel */
                float warp_r = length(uv) + 0.04f * cos(atan2(uv.y, uv.x) * 8.0f);
                mask_w = smoothstep(1.0f - wipe_p - 0.06f, 1.0f - wipe_p + 0.06f, warp_r * 1.35f);
            }
            float3 wipe_tint = float3(0.015f, 0.022f, 0.038f)
                             + float3(0.25f, 0.85f, 1.0f) * (mask_w * (1.0f - mask_w) * 2.8f);
            col = mix(col, wipe_tint, mask_w);
        }
    }

    return float4(col, 1.0f);
}
