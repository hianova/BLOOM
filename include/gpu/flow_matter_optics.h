#ifndef FLOW_GPU_MATTER_OPTICS_H
#define FLOW_GPU_MATTER_OPTICS_H

#include "gpu/flow_sdf_ops.h"

/*
 * ============================================================================
 * Constitutive Physical Matter Optical Operator (64-Byte FlowMatterState64)
 * & Relativistic Space Citadel / CineCamera Post-FX Evaluator
 * ============================================================================
 */
FLOW_GPU_INLINE float3 eval_constitutive_matter_optics64(
    FLOW_CONST_REF(FlowMatterState64) m,
    float2 uv,
    float2 grad_h,
    float laplacian_h,
    float3 bg_sample,
    float2 light_delta
) {
    float eta     = clamp(float(m.ior_eta_q16) * (1.0f / 65536.0f), 1.0f, 4.0f);
    float kappa   = clamp(float(m.extinction_kappa_q16) * (1.0f / 65536.0f), 0.0f, 8.0f);
    float relief  = clamp(float(m.surface_relief_q16) * (1.0f / 65536.0f), 0.0f, 2.0f);
    float sigma_r = clamp(float(m.micro_roughness_q16) * (1.0f / 65536.0f), 0.0f, 1.0f);
    float mu_s    = clamp(float(m.mie_scatter_q16) * (1.0f / 65536.0f), 0.0f, 1.0f);
    float3 mu_a   = float3(float(m.absorb_rgb_q16[0]),
                           float(m.absorb_rgb_q16[1]),
                           float(m.absorb_rgb_q16[2])) * (1.0f / 65536.0f);
    float wire_d  = clamp(float(m.lattice_wire_q16) * (1.0f / 65536.0f), 0.0f, 1.0f);
    float d_norm  = clamp(float(m.thickness_mm_q16) * (1.0f / (8.0f * 65536.0f)), 0.05f, 3.0f);

    float2 g_eff = grad_h * relief;
    float snell_factor = (eta - 1.0f) / eta;

    /* 1. Complex Fresnel Reflectance F(\theta, \eta, \kappa) & Skin-Depth Transmittance */
    float3 n_surf = normalize(float3(-g_eff.x, -g_eff.y, 1.0f));
    float cos_t = clamp(n_surf.z, 0.0f, 1.0f);
    float f0_num = (eta - 1.0f) * (eta - 1.0f) + kappa * kappa;
    float f0_den = (eta + 1.0f) * (eta + 1.0f) + kappa * kappa;
    float f0 = clamp(f0_num / max(f0_den, 1e-4f), 0.0f, 1.0f);
    float f_theta = f0 + (1.0f - f0) * pow(1.0f - cos_t, 5.0f);
    float skin_trans = (1.0f - f_theta) * exp(-kappa * 2.2f);

    /* 2. Analytic 2-Jet Jacobian Caustic Convergence (damped by Vogel scattering) */
    float jacobian_det = max(0.25f, abs(1.0f - d_norm * snell_factor * relief * laplacian_h));
    float scatter_damp = clamp((sigma_r + mu_s) * 0.5f, 0.0f, 1.0f);
    float caustic_gain = mix(1.0f / jacobian_det, 1.0f, scatter_damp);

    /* 3. Embedded Internal Wire Lattice & Refracted Parallax Shadow */
    float wire_occ = 1.0f;
    if (wire_d > 0.001f) {
        float2 w_cell = abs(fract((uv - g_eff * 0.04f) * 18.0f) - 0.5f);
        float d_wire = min(w_cell.x, w_cell.y);
        float2 s_cell = abs(fract((uv + (light_delta * 0.08f + g_eff * 0.06f) * d_norm) * 18.0f) - 0.5f);
        float d_shad = min(s_cell.x, s_cell.y);
        float mask_w = 1.0f - smoothstep(0.012f, 0.028f, d_wire);
        float mask_s = (1.0f - smoothstep(0.015f, 0.065f, d_shad)) * 0.35f;
        wire_occ = 1.0f - clamp((mask_w * 0.82f + mask_s) * wire_d, 0.0f, 0.88f);
    }

    /* 4. Beer-Lambert Spectral Transmittance + Mie Subsurface Scatter */
    float3 beer_t = exp(-mu_a * d_norm * (1.0f + 0.5f * mu_s));
    float3 sss_glow = beer_t * (mu_s * 0.38f);
    float3 body_rad = (bg_sample * beer_t * caustic_gain + sss_glow) * wire_occ;

    /* 5. Micro-Pebble Softbox Light Vector Bending: u_bent = light_delta + 0.35 * grad_h */
    float2 u_bent = light_delta + g_eff * 0.35f;
    float softbox = exp(-dot(u_bent, u_bent) * 3.0f);
    float3 refl_rad = float3(0.75f + softbox) * f_theta;

    /* 6. Planck Blackbody Thermal Radiation (T > 650K) */
    float3 planck_rad = float3(0.0f);
    if (m.thermal_kelvin > 650U) {
        float ex = clamp(float(m.thermal_kelvin - 650U) * (1.0f / 1000.0f), 0.0f, 2.0f);
        planck_rad = float3(ex * 1.4f, ex * ex * 0.95f, max(0.0f, ex - 0.45f) * 0.85f);
    }

    return body_rad * skin_trans + refl_rad + planck_rad;
}

FLOW_GPU_INLINE float3 eval_space_citadel_and_cine_optics(
    float2 uv_raw,
    float2 uv,
    float2 uv_rot,
    float time_sec,
    float thermal_norm,
    float focal_ratio,
    float bg_zoom,
    float3 ro_fg,
    float fg_fov_z,
    bool has_letterbox,
    FLOW_CONST_REF(FlowGpuFrameUniforms256) u,
    constant FlowGpuGlyphDesc64 *hud_glyphs
) {
    float3 ro_bg = float3(0.0f, 0.0f, -500.0f);
    float3 rd_bg = normalize(float3(uv_rot / bg_zoom, 1.15f));
    float3 rd_fg = normalize(float3(uv_rot, fg_fov_z));

    /* 0. Deep Space Background + Starfield */
    float star_seed = fract(sin(dot(floor(uv_rot * 520.0f), float2(12.9898f, 78.233f))) * 43758.5453f);
    float star = step(0.9965f, star_seed) * (0.5f + 1.5f * fract(star_seed * 19.3f));
    float3 col = float3(0.003f, 0.005f, 0.012f) + float3(1.0f, 0.95f, 0.9f) * star;

    float3 sun_dir = normalize(float3(0.62f, 0.42f, -0.66f));

    /* 1. Layer 1 Background: Cassini-Grade Gas Giant + Rayleigh Atmosphere + Ice Rings */
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

            float limb_soft = smoothstep(0.0f, sphere_r * (0.04f + 0.18f * bg_coc), sqrt(disc));
            col = mix(col, planet_col, limb_soft);
        }
    }

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

    /* 2. Layer 0 Foreground: Micro-Voxel Carved Citadel + Smooth-CSG Liquid + Chase Interceptor */
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
                    float melt_glow = 0.45f + 0.65f * vein;
                    surf_col += blackbody_col * (1.35f * melt_glow);
                }
                col = surf_col;
            } else if (hit.y < 3.5f) {
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
                float3 hal = normalize(sun_dir - rd_fg);
                float diff = max(dot(nor, sun_dir), 0.0f) * shadow;
                float spec = pow(max(dot(nor, hal), 0.0f), 72.0f) * shadow;
                float rim  = pow(1.0f - max(dot(-rd_fg, nor), 0.0f), 3.0f);
                uint fclass = u.s3.active_finish_class & 3U;
                float3 base_albedo = float3(0.75f, 0.82f, 0.90f);
                float3 rim_tint    = float3(0.20f, 0.85f, 1.0f);
                float3 emissive_vein = float3(0.0f);

                if (fclass == 0U) {
                    base_albedo = mix(float3(0.18f, 0.48f, 0.96f), float3(0.68f, 0.22f, 0.96f), rim);
                    rim_tint    = float3(0.30f, 0.92f, 1.0f);
                } else if (fclass == 1U) {
                    float quench = smoothstep(0.25f, 0.75f, sin(pos.x * 18.0f + pos.z * 14.0f) * 0.5f + 0.5f);
                    base_albedo = mix(float3(0.88f, 0.68f, 0.24f), float3(0.12f, 0.62f, 1.0f), quench);
                    rim_tint    = float3(0.45f, 0.85f, 1.0f);
                } else if (fclass == 2U) {
                    float damascus = abs(sin(pos.z * 36.0f + sin(pos.x * 24.0f) * 2.2f));
                    float gold_v = smoothstep(0.84f, 0.95f, damascus);
                    base_albedo = mix(float3(0.14f, 0.16f, 0.22f), float3(1.0f, 0.82f, 0.22f), gold_v);
                    rim_tint    = float3(1.0f, 0.78f, 0.25f);
                    emissive_vein = float3(1.2f, 0.85f, 0.20f) * gold_v * 0.55f;
                } else {
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
                float core_hot = clamp(1.0f - nor.z, 0.2f, 1.0f);
                col = float3(0.45f, 2.2f, 3.8f) * core_hot + float3(2.6f, 0.5f, 1.8f) * thermal_norm;
            }
        }
    }

    /* 3. CineCamera Post-Processing & Dopamine Jackpot Payoff */
    if ((u.s1.cine_flags & 2U) != 0U) {
        float2 cavity_uv = (u.s1.vcam_id == 2U) ? float2(-0.34f, -0.02f) : float2(-0.22f, -0.03f);
        float dy_cav = abs(uv_rot.y - cavity_uv.y);
        float dx_cav = abs(uv_rot.x - cavity_uv.x);
        float streak1 = exp(-dy_cav * 110.0f) * exp(-dx_cav * 2.8f) * (0.55f + 0.45f * thermal_norm);
        col += float3(0.18f, 0.58f, 1.65f) * streak1 * 0.65f;

        if (u.s1.vcam_id == 1U) {
            float2 thr_uv = float2(0.08f, 0.11f);
            float streak2 = exp(-abs(uv_rot.y - thr_uv.y) * 140.0f) * exp(-abs(uv_rot.x - thr_uv.x) * 3.2f);
            col += float3(0.25f, 0.85f, 1.85f) * streak2 * 0.65f;
        }
    }

    if ((u.s1.cine_flags & 8U) != 0U && fg_depth > 50.0f && !hit_sphere && !hit_ring) {
        float streak_ang = atan2(uv.y, uv.x);
        float streak_r = length(uv);
        float ang_bin = floor(streak_ang * 110.0f);
        float ang_hash = fract(sin(ang_bin * 91.7f) * 43758.5f);
        float streak_val = cos(fract(streak_ang * 110.0f) * 6.28318f);
        float3 svel = float3(u.s2.shutter_vel[0], u.s2.shutter_vel[1], u.s2.shutter_vel[2]);
        float speed_factor = clamp((u.s0.craft_speed + length(svel) * 0.25f) / 500.0f, 0.25f, 2.2f);

        float rad_coord = streak_r * (14.0f / max(speed_factor, 0.5f)) + ang_hash * 6.0f;
        float rad_cell = fract(rad_coord);
        float rad_id_hash = fract(sin((ang_bin * 17.3f + floor(rad_coord)) * 37.1f) * 19753.3f);
        float trail_env = smoothstep(0.05f, 0.35f, rad_cell) * smoothstep(0.95f, 0.45f, rad_cell) * rad_cell;
        float radial_band = smoothstep(0.24f, 0.48f, streak_r) * step(0.84f, ang_hash) * step(0.45f, rad_id_hash);
        float streak_mask = smoothstep(0.88f, 0.995f, streak_val) * radial_band * trail_env;
        float3 streak_col = (u.s0.stage_layer == 1U) ? float3(0.40f, 0.88f, 1.30f) : float3(0.60f, 0.90f, 1.40f);
        col += streak_col * streak_mask * speed_factor * 0.75f;
    }

    if (u.s1.collision_hit != 0U || u.s3.shockwave_ripple > 0.01f) {
        float shock_r = length(uv_rot - float2(-0.22f, -0.03f));
        float shock_env = exp(-abs(shock_r - (0.09f + 0.12f * u.s3.shockwave_ripple)) * 55.0f)
                        * clamp(u.s3.shockwave_ripple + float(u.s1.collision_hit) * 0.45f, 0.0f, 1.0f);
        col += float3(0.28f, 0.88f, 1.45f) * shock_env * 0.65f;
    }
    if (u.s3.dopamine_phase != 0U) {
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
    float ev_gain = clamp(u.s1.exposure_gain, 0.35f, 2.6f);
    col = aces_tonemap(col * ev_gain * cos4_vig);
    col = pow(col, float3(1.0f / 2.2f));

    /* 4. SDD SVG Cockpit Reticle & Thermal Telemetry Gauge */
    if ((u.s1.cine_flags & 16U) == 0U) {
        float2 hud_ret = uv_raw * (1.0f / 0.11f);
        if (abs(hud_ret.x) < 1.05f && abs(hud_ret.y) < 1.05f) {
            float d_ret = eval_sdd_glyph(hud_glyphs[0], hud_ret);
            float a_ret = 1.0f - smoothstep(0.008f, 0.022f, d_ret);
            float3 ret_col = (u.s0.stage_layer == 0U) ? float3(0.0f, 0.92f, 1.0f) : float3(0.25f, 0.75f, 1.0f);
            col = mix(col, ret_col, a_ret * 0.48f);
        }

        float2 hud_wing = (uv_raw - float2(0.38f, 0.24f)) * (1.0f / 0.055f);
        if (abs(hud_wing.x) < 1.05f && abs(hud_wing.y) < 1.05f) {
            uint g_idx = (u.s1.vcam_id % 3U) + 1U;
            float d_w = eval_sdd_glyph(hud_glyphs[g_idx], hud_wing);
            float a_w = 1.0f - smoothstep(0.015f, 0.040f, d_w);
            col = mix(col, float3(0.2f, 0.88f, 1.0f), a_w * 0.65f);
        }

        float2 auv = abs(uv_raw);
        float d_tick = min(abs(auv.x - 0.28f), abs(auv.y - 0.16f));
        if (auv.x > 0.25f && auv.x < 0.29f && auv.y > 0.14f && auv.y < 0.17f && d_tick < 0.0012f) {
            col = mix(col, float3(0.45f, 0.80f, 1.0f), 0.45f);
        }
    }

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

    return col;
}

#endif /* FLOW_GPU_MATTER_OPTICS_H */
