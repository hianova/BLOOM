#ifndef FLOW_GPU_SVG_UI_PASS_H
#define FLOW_GPU_SVG_UI_PASS_H

#include "gpu/flow_sdf_ops.h"

/*
 * ============================================================================
 * Portable Wave64 Tile-Culled SVG-UI Frontend DOM & Story Transition Pass
 * ============================================================================
 * Uses 32-bit pair (lo32, hi32) Wave64 bitmask evaluation via
 * `flow_wave64_mask_test_u32x2` so WebGPU (WGSL) and mobile Vulkan SPIR-V
 * execute identical O(1) spatial culling without 64-bit integer shader extensions.
 * ============================================================================
 */
FLOW_GPU_INLINE float3 eval_svg_ui_compositor_pass(
    float3 col,
    float2 uv_raw,
    float2 uv,
    float time_sec,
    bool has_letterbox,
    FLOW_CONST_REF(FlowGpuFrameUniforms256) u,
    constant FlowEngineBus64 *engine_bus
) {
    /* 1. Declarative 64-Byte FLOW SVG-UI Frontend DOM & Liquid-Glass Director Bar */
    if (abs(uv_raw.y) > 0.21f) {
        float uv_ui_y = (has_letterbox && uv_raw.y > 0.18f) ? (uv_raw.y + 0.065f) : uv_raw.y;
        uint wc = uint(clamp((uv_raw.x + 0.5f) * 8.0f, 0.0f, 7.0f));
        uint wr = uint(clamp((uv_ui_y + 0.375f) * 10.666667f, 0.0f, 7.0f));
        uint bit_idx = wr * 8U + wc;

        for (uint i = 0U; i < 8U; ++i) {
            /* Portable 32-bit pair Wave64 mask read from u32_quads[3].zw (bytes 56..63) */
            uint4 q3 = engine_bus[FLOW_BUS_SLOT_SVG_UI_0 + i].u32_quads[3];
            if (!flow_wave64_mask_test_u32x2(q3.z, q3.w, bit_idx)) {
                continue;
            }

            constant FlowSvgUiNode64 &nd = engine_bus[FLOW_BUS_SLOT_SVG_UI_0 + i].svg_node;
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
            float3 fill_rgb  = unpack_rgb24(rgb24);

            if (nd.prim_type == FLOW_SVG_PRIM_RECT_GLASS) {
                float rx = float(nd.corner_radius_q16) * (1.0f / 65536.0f);
                float2 qd = abs(lp) - half_sz + rx;
                float sdf = min(max(qd.x, qd.y), 0.0f) + length(max(qd, 0.0f)) - rx;
                if (sdf < 0.0018f) {
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

    /* 2. Scripted Story Finisher ("劇情殺") & Scene Transition ("過渡動畫") GPU Wipe */
    uint dir_meta = engine_bus[FLOW_BUS_SLOT_STORY_DIRECTOR].u32_quads[0].y;
    uint dir_phase = dir_meta & 0xFFFFU;
    if (dir_phase != 0U) {
        uint trans_style = (dir_meta >> 16U) & 3U;
        float wipe_p = clamp(float(int(engine_bus[FLOW_BUS_SLOT_STORY_DIRECTOR].u32_quads[1].w)) * (1.0f / 65536.0f), 0.0f, 1.0f);
        if (wipe_p > 0.001f) {
            float mask_w = wipe_p;
            if (trans_style == 1U) {
                float wave_x = uv_raw.x + 0.06f * sin(uv_raw.y * 18.0f + time_sec * 10.0f) + 0.55f;
                mask_w = smoothstep(wave_x - 0.08f, wave_x + 0.08f, wipe_p * 1.15f);
            } else if (trans_style == 2U) {
                float rad_uv = length(uv);
                float iris_r = (1.0f - wipe_p) * 0.68f;
                mask_w = smoothstep(iris_r - 0.025f, iris_r + 0.025f, rad_uv);
            } else if (trans_style == 3U) {
                float warp_r = length(uv) + 0.04f * cos(atan2(uv.y, uv.x) * 8.0f);
                mask_w = smoothstep(1.0f - wipe_p - 0.06f, 1.0f - wipe_p + 0.06f, warp_r * 1.35f);
            }
            float3 wipe_tint = float3(0.015f, 0.022f, 0.038f)
                             + float3(0.25f, 0.85f, 1.0f) * (mask_w * (1.0f - mask_w) * 2.8f);
            col = mix(col, wipe_tint, mask_w);
        }
    }

    return col;
}

#endif /* FLOW_GPU_SVG_UI_PASS_H */
