#ifndef FLOW_GPU_MOOD_STAGES_H
#define FLOW_GPU_MOOD_STAGES_H

#include "gpu/flow_sdf_ops.h"

/*
 * ============================================================================
 * Moodboard-to-Prefab Stages 1..3 (Window Condensation, Particle Corridor, Diorama)
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
    float3 sky_lin      = sky_col * sky_col;
    float3 ground_lin   = ground_col * ground_col;
    float3 accent_lin   = accent_col * accent_col;
    float3 backdrop_lin = backdrop_col * backdrop_col * 0.14f;

    float2 w_uv = uv_raw - look_off * 0.14f;
    float2 win_half = float2(0.215f, 0.365f);
    float win_rx = 0.052f;
    float2 q_win = abs(w_uv) - win_half + float2(win_rx);
    float d_win = min(max(q_win.x, q_win.y), 0.0f) + length(max(q_win, 0.0f)) - win_rx;

    if (d_win > 0.0f) {
        float gasket = smoothstep(0.024f, 0.002f, d_win);
        float bevel_rim = exp(-abs(d_win - 0.006f) * 360.0f) * 0.035f;
        float bounce = exp(-d_win * 16.0f) * 0.025f;
        float3 cab_col = backdrop_lin + (sky_lin * 0.30f + ground_lin * 0.70f) * bounce;
        return mix(cab_col, float3(0.002f, 0.0025f, 0.002f), gasket) + accent_lin * bevel_rim;
    }

    float wipe_clear = 0.0f;
    float2 grad_h = float2(0.0f);
    float meniscus_rim = 0.0f;

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
                    float w_mask = smoothstep(1.10f, 0.25f, abs(norm_dx)) * v_prog;
                    wipe_clear = max(wipe_clear, w_mask * rivulet_strength);

                    float env = exp(-norm_dx * norm_dx * 2.4f) * v_prog * rivulet_strength;
                    grad_h.x += -norm_dx * env * 0.18f;
                    grad_h.y += -0.14f * env;
                    meniscus_rim += smoothstep(0.50f, 0.90f, abs(norm_dx)) * smoothstep(1.30f, 0.90f, abs(norm_dx)) * v_prog;
                }
            }
        }
    }

    float mist_vertical = smoothstep(-0.07f, 0.06f, w_uv.y);
    float raw_fog = mist_vertical * fog_condense;
    float active_fog = clamp(raw_fog * (1.0f - wipe_clear * 0.92f), 0.0f, 1.0f);

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

    float2 ext_uv = w_uv + grad_h;
    float horizon_y = -0.025f + 0.012f * sin(ext_uv.x * 7.5f) - 0.006f * cos(ext_uv.x * 19.0f);

    float sky_t = clamp((-ext_uv.y + 0.04f) * 2.5f, 0.0f, 1.0f);
    float3 sky_grad = mix(mix(sky_lin, accent_lin, 0.55f), sky_lin * 0.85f, sky_t);

    float motion_streak = sin(ext_uv.y * 75.0f + sin(ext_uv.x * 5.5f - time_sec * 1.4f) * 2.2f) * 0.08f;
    float bottom_shade = smoothstep(0.06f, 0.35f, ext_uv.y);
    float3 autumn_col = ground_lin * (1.18f + motion_streak) * (1.0f - 0.58f * bottom_shade);

    float mountain_band = exp(-abs(ext_uv.y - horizon_y) * 34.0f);
    float blend_ground = smoothstep(horizon_y - 0.032f, horizon_y + 0.042f, ext_uv.y);
    float3 ext_col = mix(sky_grad, autumn_col, blend_ground);
    ext_col = mix(ext_col, mix(sky_lin, ground_lin, 0.38f) * 0.65f, mountain_band * 0.55f);

    float3 mist_scatter = mix(accent_lin * 0.85f, mix(sky_lin, ground_lin, 0.62f) + float3(0.06f), 0.65f);
    float3 glass_col = mix(ext_col, mist_scatter, active_fog * 0.38f) + accent_lin * (droplet_lens * 0.22f);

    float caustic_sky = clamp(-grad_h.x * 6.0f + wipe_clear * 0.45f, 0.0f, 1.0f) * smoothstep(0.09f, -0.05f, w_uv.y);
    glass_col *= (1.0f - 0.48f * clamp(meniscus_rim, 0.0f, 1.0f) * smoothstep(-0.03f, 0.22f, w_uv.y));
    glass_col *= (1.0f - 0.22f * wipe_clear * smoothstep(0.02f, 0.28f, w_uv.y));
    glass_col += mix(sky_lin, accent_lin, 0.55f) * caustic_sky * 0.55f;

    float edge_ao = smoothstep(0.0f, -0.022f, d_win);
    return glass_col * (0.18f + 0.82f * edge_ao);
}

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
    float3 ground_lin = ground_col * ground_col;
    float3 accent_lin = accent_col * accent_col;

    float2 sing_uv = float2(0.0f, -0.025f) + look_off * 0.12f;
    float2 duv = uv_rot - sing_uv;
    float r_sing = length(duv);

    float core_bloom = exp(-r_sing * 22.0f) * 3.0f * singularity_gain;
    float mie_haze   = exp(-r_sing * 4.2f) * 0.85f * singularity_gain;
    float hor_flare  = exp(-abs(duv.y) * 52.0f) * exp(-abs(duv.x) * 3.2f) * 0.85f * singularity_gain;

    float3 col = ground_lin * 0.14f
               + sky_lin * (mie_haze * 0.85f + exp(-abs(duv.y) * 5.0f) * 0.22f)
               + mix(sky_lin, accent_lin, 0.65f) * hor_flare
               + accent_lin * core_bloom;

    for (int zi = 0; zi < 16; ++zi) {
        float fz = float(zi);
        float z_p = 1.6f + fz * 0.85f - fract(time_sec * 0.14f) * 0.85f;
        float inv_z = 1.0f / z_p;

        float x_world = duv.x * z_p;
        float y_world = -duv.y * z_p + 0.30f;

        for (int side = -1; side <= 1; side += 2) {
            float fside = float(side);
            float col_id = fz * 2.0f + float(side > 0 ? 1 : 0);
            float h_id = fract(sin(col_id * 91.7f) * 43758.5f);

            float x_center = fside * (0.68f + 0.52f * fract(h_id * 7.3f));
            float half_w   = 0.16f + 0.14f * fract(h_id * 3.1f);
            float top_y    = 0.55f + 0.85f * fract(h_id * 11.7f) * smoothstep(0.08f, 0.55f, abs(duv.x));

            if (abs(x_world - x_center) < half_w && y_world > -0.18f && y_world < top_y) {
                float2 cell_uv = float2((x_world - x_center) * 52.0f, y_world * 52.0f);
                float2 cid = floor(cell_uv);
                float2 lc = cell_uv - cid - float2(0.5f);
                float p_hash = fract(sin(dot(cid + float2(col_id * 13.0f, fz * 29.0f),
                                             float2(12.9898f, 78.233f))) * 43758.5f);

                float edge_erode = smoothstep(half_w, half_w * 0.35f, abs(x_world - x_center))
                                 * smoothstep(top_y, top_y - 0.35f, y_world);
                float active_thresh = 1.0f - clamp(particle_density, 0.15f, 0.95f) * 0.68f * edge_erode;

                if (p_hash > active_thresh) {
                    float d_pt = length(lc);
                    float pt_rad = (0.20f + 0.24f * fract(p_hash * 17.0f)) * clamp(inv_z * 3.2f, 0.55f, 1.35f);
                    if (d_pt < pt_rad) {
                        float spark = smoothstep(pt_rad, pt_rad * 0.15f, d_pt);
                        float v_fade = smoothstep(1.65f, 0.15f, y_world) * smoothstep(-0.15f, 0.02f, y_world);
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

    if (duv.y > 0.002f) {
        float z_g = 0.36f / (duv.y + 0.008f);
        if (z_g < 30.0f) {
            float x_g = duv.x * z_g;
            float z_move = z_g - time_sec * 0.42f;
            float wave = 0.10f * sin(x_g * 3.4f + z_move * 1.8f)
                       + 0.05f * cos(x_g * 6.8f - z_move * 2.6f + time_sec * 0.6f);

            float2 g_uv = float2(duv.x * 320.0f + wave * 12.0f, z_move * 34.0f + wave * 6.0f);
            float2 g_id = floor(g_uv);
            float2 g_lc = g_uv - g_id - float2(0.5f);
            float h_g = fract(sin(dot(g_id, float2(41.31f, 289.73f))) * 43758.5453f);

            float x_path = 0.040f * sin(z_g * 0.95f - time_sec * 1.1f) + wave * 0.18f;
            float path_env = exp(-abs(x_g - x_path) * (4.8f + 0.22f * z_g));
            float d_dot = length(g_lc);
            float dot_mask = smoothstep(0.34f, 0.05f, d_dot);
            float dist_fade = exp(-z_g * 0.10f) * smoothstep(0.002f, 0.035f, duv.y);

            if (h_g > 0.48f) {
                float wave_crest = clamp(0.45f + wave * 3.8f, 0.10f, 1.1f);
                col += (sky_lin * 1.2f + float3(0.02f, 0.05f, 0.14f)) * (dot_mask * wave_crest * dist_fade * 0.42f);
            }

            float glitter_thresh = 0.82f - 0.52f * path_env * clamp(particle_density, 0.3f, 1.0f);
            if (h_g > glitter_thresh) {
                float twinkle = 0.50f + 0.50f * sin(time_sec * 5.5f + h_g * 62.8f);
                col += accent_lin * (dot_mask * path_env * twinkle * dist_fade * 2.4f);
            }
        }
    }

    return col;
}

FLOW_GPU_INLINE float eval_diorama_terrain_h(float2 xz) {
    float shore = xz.x * 0.72f + xz.y * 0.68f - 0.04f;
    float base_slope = clamp(-shore * 0.38f + 0.02f, -0.19f, 0.24f);

    float2 d_cab = xz - float2(-0.28f, -0.22f);
    float cab_pad = exp(-dot(d_cab, d_cab) * 14.0f);

    float n1 = sin(xz.x * 6.3f + sin(xz.y * 5.7f) * 1.4f) * cos(xz.y * 6.9f - sin(xz.x * 4.8f) * 1.3f);
    float n2 = sin(xz.x * 14.7f - xz.y * 12.3f + sin(xz.x * 9.1f)) * 0.45f;
    float n3 = cos(xz.x * 29.0f + xz.y * 27.0f) * 0.18f;
    float crags = (n1 * 0.048f + n2 * 0.028f + n3 * 0.012f) * (1.0f - 0.78f * cab_pad);

    float r1 = exp(-dot(xz - float2(0.46f, -0.18f), xz - float2(0.46f, -0.18f)) * 45.0f) * 0.15f;
    float r2 = exp(-dot(xz - float2(0.12f,  0.48f), xz - float2(0.12f,  0.48f)) * 48.0f) * 0.14f;

    float h_raw = base_slope + crags + r1 + r2;
    return mix(h_raw, 0.135f, cab_pad * 0.85f);
}

FLOW_GPU_INLINE float2 map_diorama_solid(float3 p) {
    float h_t = eval_diorama_terrain_h(float2(p.x, p.z));
    float d_terr = (p.y - h_t) * 0.75f;
    float mat_id = 1.0f;

    float3 q = p - float3(-0.28f, 0.205f, -0.22f);
    float d_walls = sd_round_box(q - float3(0.0f, -0.015f, 0.0f), float3(0.145f, 0.085f, 0.115f), 0.004f);
    float3 qr = q - float3(0.0f, 0.070f, 0.0f);
    float d_roof = max(max(abs(qr.z) * 0.72f + qr.y - 0.082f, -qr.y - 0.012f),
                       max(abs(qr.x) - 0.165f, abs(qr.z) - 0.138f));
    float d_chimney = sd_round_box(q - float3(0.035f, 0.145f, -0.045f), float3(0.018f, 0.045f, 0.018f), 0.002f);

    if (d_walls < d_terr)   { d_terr = d_walls;   mat_id = 2.0f; }
    if (d_roof < d_terr)    { d_terr = d_roof;    mat_id = 3.0f; }
    if (d_chimney < d_terr) { d_terr = d_chimney; mat_id = 4.0f; }

    float3 pq = p - float3(0.12f, 0.022f, -0.04f);
    float d_deck = sd_round_box(pq, float3(0.24f, 0.010f, 0.048f), 0.002f);
    float2 piling_xz = float2(fract(pq.x * 7.5f) - 0.5f, abs(pq.z) - 0.038f);
    float d_piling = max(length(piling_xz) - 0.012f, max(abs(pq.x) - 0.23f, abs(pq.y + 0.055f) - 0.060f));
    float d_pier = min(d_deck, d_piling);
    if (d_pier < d_terr) {
        d_terr = d_pier;
        mat_id = 5.0f;
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
    float vig = 1.0f - 0.16f * dot(uv_raw, uv_raw);
    float3 bg_col = backdrop_col * vig;

    float yaw = 0.78f + look_off.x * 0.55f + craft_rot.y * 0.30f;
    float pitch = clamp(0.50f - look_off.y * 0.30f + craft_rot.x * 0.20f, 0.28f, 0.82f);
    float cy = cos(yaw), sy = sin(yaw);
    float cp = cos(pitch), sp = sin(pitch);

    float3 fwd = float3(-sy * cp, -sp, -cy * cp);
    float3 rgt = float3(cy, 0.0f, -sy);
    float3 up  = float3(rgt.y * fwd.z - rgt.z * fwd.y,
                        rgt.z * fwd.x - rgt.x * fwd.z,
                        rgt.x * fwd.y - rgt.y * fwd.x);

    float ortho_scale = 1.76f;
    float3 ro = fwd * (-4.5f) + (rgt * uv_raw.x - up * (uv_raw.y + 0.025f)) * ortho_scale;
    float3 rd = normalize(fwd + (rgt * uv_raw.x - up * uv_raw.y) * 0.12f);

    float active_cut = clamp(diorama_cut - xray_radius * 0.65f, 0.38f, 1.0f);
    float3 box_b = float3(0.82f * active_cut, 0.42f, 0.82f);

    float3 inv_rd = float3(1.0f / (rd.x + 1e-6f), 1.0f / (rd.y + 1e-6f), 1.0f / (rd.z + 1e-6f));
    float3 t0s = (float3(0.0f) - box_b - ro) * inv_rd;
    float3 t1s = ( box_b - ro) * inv_rd;
    float3 tsmaller = float3(min(t0s.x, t1s.x), min(t0s.y, t1s.y), min(t0s.z, t1s.z));
    float3 tbigger  = float3(max(t0s.x, t1s.x), max(t0s.y, t1s.y), max(t0s.z, t1s.z));
    float t_near = max(max(tsmaller.x, tsmaller.y), max(tsmaller.z, 0.0f));
    float t_far  = min(tbigger.x, min(tbigger.y, tbigger.z));

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

    float3 p_entry = ro + rd * t_near;
    float3 d_face = abs(abs(p_entry) - box_b);
    float3 n_box = float3(0.0f, 1.0f, 0.0f);
    if (d_face.x <= d_face.y && d_face.x <= d_face.z) {
        n_box = float3((p_entry.x >= 0.0f) ? 1.0f : -1.0f, 0.0f, 0.0f);
    } else if (d_face.z <= d_face.x && d_face.z <= d_face.y) {
        n_box = float3(0.0f, 0.0f, (p_entry.z >= 0.0f) ? 1.0f : -1.0f);
    }

    float t = t_near;
    float2 hit = float2(100.0f, 0.0f);
    float t_water_hit = -1.0f;
    float3 n_water_hit = float3(0.0f, 1.0f, 0.0f);
    float water_level = -0.015f;

    for (int i = 0; i < 32; ++i) {
        if (t > t_far) break;
        float3 p = ro + rd * t;
        if (p.y < y_table) break;

        float wave_y = water_level
                     + 0.0045f * sin(p.x * 26.0f + time_sec * 2.2f + sin(p.z * 14.0f))
                     * cos(p.z * 24.0f - time_sec * 1.7f);
        float h_terr_here = eval_diorama_terrain_h(float2(p.x, p.z));
        if (t_water_hit < 0.0f && p.y <= wave_y && p.y > h_terr_here) {
            t_water_hit = t;
            if (t <= t_near + 0.008f && abs(n_box.y) < 0.1f) {
                n_water_hit = n_box;
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
            float h_top = eval_diorama_terrain_h(pos_xz);
            float strata = sin(pos.y * 42.0f + sin(pos.x * 9.0f - pos.z * 8.0f) * 1.8f) * 0.5f + 0.5f;
            float grain = fract(sin(dot(floor(float2(pos.x + pos.z, pos.y) * 95.0f), float2(12.9898f, 78.233f))) * 43758.5f);
            float3 rock_wall = mix(float3(0.28f, 0.27f, 0.25f), float3(0.48f, 0.45f, 0.42f), strata * 0.75f + grain * 0.25f);
            float snow_lip = smoothstep(h_top - 0.024f, h_top - 0.006f, pos.y);
            surf_col = mix(rock_wall, snow_col, snow_lip) * (diff * 0.65f + 0.48f);
        } else if (hit.y < 1.5f) {
            float rock_noise = sin(pos.x * 19.0f + sin(pos.z * 17.0f) * 1.6f) * cos(pos.z * 21.0f - pos.x * 13.0f);
            float wet_shore = smoothstep(-0.025f, 0.025f, pos.y);
            float snow_mask = smoothstep(0.86f - snow_cover * 0.35f, 0.94f - snow_cover * 0.25f,
                                         nor.y + 0.06f * rock_noise) * wet_shore;
            float3 granite = mix(float3(0.14f, 0.15f, 0.16f), float3(0.24f, 0.25f, 0.27f), 0.5f + 0.5f * rock_noise);
            float3 albedo = mix(granite, snow_col, snow_mask);
            surf_col = albedo * (diff * 0.68f + float3(0.45f, 0.48f, 0.54f) * amb)
                     + warm_2400k * cab_gi * 0.72f;
        } else if (hit.y < 2.5f) {
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
            float roof_snow = smoothstep(0.20f, 0.50f, nor.y);
            surf_col = mix(float3(0.24f, 0.18f, 0.16f), snow_col, roof_snow) * (diff * 0.65f + 0.48f);
        } else if (hit.y < 4.5f) {
            surf_col = float3(0.10f, 0.11f, 0.13f) * (diff * 0.7f + 0.3f);
        } else {
            float pier_snow = smoothstep(0.65f, 0.90f, nor.y) * 0.45f;
            float3 wood = mix(float3(0.34f, 0.24f, 0.16f), snow_col, pier_snow);
            surf_col = wood * (diff * 0.85f + 0.38f) + warm_2400k * cab_gi * 0.75f;
        }

        if (t_water_hit > 0.0f && t_water_hit < t) {
            float water_depth = max(t - t_water_hit, 0.01f);
            float3 p_w = ro + rd * t_water_hit;
            float3 absorption = exp(-float3(2.8f, 1.05f, 0.75f) * water_depth * 3.6f);
            float3 deep_sea = sea_col * (0.42f + 0.35f * max(dot(n_water_hit, sun_dir), 0.0f));
            float3 water_body = mix(deep_sea, surf_col * float3(0.75f, 0.92f, 0.98f), absorption);

            float fresnel = pow(1.0f - max(dot(-rd, n_water_hit), 0.0f), 4.0f);
            float2 glow_dir = float2(p_w.x - cabin_light_pos.x, p_w.z - cabin_light_pos.z);
            float glow_path = exp(-abs(glow_dir.x * 0.55f - glow_dir.y * 0.83f) * 9.5f)
                            * exp(-length(glow_dir) * 2.4f);
            float3 refl_rd = rd - 2.0f * dot(rd, n_water_hit) * n_water_hit;
            float ripple_glint = pow(clamp(dot(refl_rd, normalize(cabin_light_pos - p_w)), 0.0f, 1.0f), 14.0f);
            float3 cab_refl = warm_2400k * (glow_path * 0.42f + ripple_glint * 0.65f) * step(0.5f, n_water_hit.y);

            float cut_rim = (1.0f - step(0.5f, n_water_hit.y)) * 0.14f;
            surf_col = mix(water_body, snow_col * 0.85f, fresnel * 0.38f + cut_rim) + cab_refl;
        }

        col = surf_col;
    }

    return col;
}

#endif /* FLOW_GPU_MOOD_STAGES_H */
