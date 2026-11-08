#include <metal_stdlib>
using namespace metal;
#include "flow_gpu_abi.h"
#include "gpu/flow_sdf_ops.h"
#include "gpu/flow_matter_optics.h"
#include "gpu/flow_mood_stages.h"
#include "gpu/flow_svg_ui_pass.h"

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
        float border_dist = min(abs(p.y - bar_px), abs(p.y - (h - bar_px)));
        float border_line = smoothstep(1.5f, 0.0f, border_dist) * 0.18f;
        return float4(float3(0.02f, 0.05f, 0.09f) * border_line, 1.0f);
    }

    float time_sec = float(u.s0.frame_index) * 0.033f + 1.2f;
    float thermal_norm = clamp((float(u.s0.thermal_kelvin) - 700.0f) / 150.0f, 0.0f, 1.2f);

    float2 uv_raw = (p - float2(w * 0.5f, h * 0.5f)) / h;
    float2 uv = uv_raw - float2(u.s1.cam_look_offset[0], u.s1.cam_look_offset[1]) * 0.45f;
    float3 cam_off = float3(u.s1.cam_offset[0], u.s1.cam_offset[1], u.s1.cam_offset[2]);

    float focal_mm = clamp(u.s1.focal_length_mm, 18.0f, 135.0f);
    float focal_ratio = focal_mm / 35.0f;
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
        col = eval_space_citadel_and_cine_optics(
            uv_raw, uv, uv_rot, time_sec, thermal_norm, focal_ratio,
            bg_zoom, ro_fg, fg_fov_z, has_letterbox, u, hud_glyphs
        );
    }

    col = eval_svg_ui_compositor_pass(col, uv_raw, uv, time_sec, has_letterbox, u, engine_bus);
    return float4(col, 1.0f);
}
