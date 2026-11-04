#include "flow_platform_bridge.h"
#include <string.h>

/*
 * ============================================================================
 * FLOW CROSS-PLATFORM SDL3_GPU & JSI NATIVE BRIDGE (src/sdl3_gpu_host.c)
 * ============================================================================
 * 100% Pure C17 (Zero FloatOps, Zero HeapCalls) unified host layer for:
 *   - macOS           : Apple Metal AIR
 *   - Steam Deck      : SteamOS AMD RDNA2 Vulkan SPIR-V + Trackpad + IMU
 *   - Nintendo Switch : NVN / Vulkan SPIR-V + Joy-Con Gyro + HD Rumble
 * ============================================================================
 */

static int32_t clamp_i32_bridge(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint64_t calc_platform_state_mac64(const FlowPlatformState64 *s) {
    uint64_t h = FLOW_JSI_BRIDGE_MAGIC;
    h ^= ((uint64_t)s->platform_id << 32U) | ((uint64_t)s->operation_mode << 16U) | (uint64_t)s->battery_pct;
    h ^= ((uint64_t)s->viewport_width_px << 32U) | (uint64_t)s->viewport_height_px;
    h ^= ((uint64_t)s->raymarch_step_budget << 32U) | (uint64_t)s->on_ac_power;
    h ^= ((uint64_t)(uint32_t)s->gyro_aim_x_q16 << 32U) | (uint64_t)(uint32_t)s->gyro_aim_y_q16;
    h ^= ((uint64_t)s->hd_rumble_low_160hz_q16 << 32U) | (uint64_t)s->hd_rumble_high_320hz_q16;
    h ^= s->last_bus1k_digest ^ (uint64_t)s->bus1k_frames_submitted;
    h *= 0x9E3779B97F4A7C15ULL;
    return h ^ (h >> 31U);
}

static uint64_t calc_vtable_mac64(const FlowNativeBridgeVTable64 *vt) {
    uint64_t h = vt->abi_magic ^ vt->shader_abi_hash;
    h ^= ((uint64_t)vt->backend_platform_id << 32U) | (uint64_t)vt->jsi_bus_size_bytes;
    h *= 0xBF58476D1CE4E5B9ULL;
    return h ^ (h >> 32U);
}

/* 1. VTable Slot 0: TurboModule Input, Trackpad & 6-Axis IMU Gyro Polling */
static int flow_sdl3_poll_input(FlowPlatformState64 *state,
                                FlowInputIntent64 *out_intent,
                                FlowSvgUiDomState64 *inout_dom) {
    if (!state) return -1;

    uint32_t dev_class = FLOW_DEVICE_KEYMOUSE;
    if (state->platform_id == FLOW_PLATFORM_STEAMDECK_VULKAN ||
        state->platform_id == FLOW_PLATFORM_SWITCH_NVN) {
        dev_class = FLOW_DEVICE_GAMEPAD;
    }

    if (out_intent) {
        *out_intent = flow_input_lift_keymouse_to_intent(
            FLOW_ACTION_PRIMARY_STRIKE,
            state->gyro_aim_x_q16,
            state->gyro_aim_y_q16,
            32768,
            state->trackpad_scrub_dx_q16
        );
        out_intent->device_class = dev_class;
        out_intent->haptic_freq_out_q16 = state->hd_rumble_low_160hz_q16;
    }

    if (inout_dom) {
        inout_dom->cursor_norm_x_q16 = state->gyro_aim_x_q16;
        inout_dom->cursor_norm_y_q16 = state->gyro_aim_y_q16;
        inout_dom->viewport_width_px  = state->viewport_width_px;
        inout_dom->viewport_height_px = state->viewport_height_px;
    }

    state->platform_mac = calc_platform_state_mac64(state);
    return 0;
}

/* 2. VTable Slot 1: React Native JSI Single-Buffer FlowEngineBus1K (1024B) Submission */
static int flow_sdl3_submit_bus1k(FlowPlatformState64 *state,
                                  const FlowEngineBus1K *bus1k) {
    if (!state || !bus1k) return -1;

    const uint64_t *words = (const uint64_t *)bus1k;
    uint64_t digest = 0xCBF29CE484222325ULL;
    for (size_t i = 0U; i < (sizeof(FlowEngineBus1K) / sizeof(uint64_t)); ++i) {
        digest ^= words[i];
        digest *= 0x100000001B3ULL;
    }

    state->bus1k_frames_submitted++;
    state->last_bus1k_digest = digest;
    state->platform_mac      = calc_platform_state_mac64(state);
    return 0;
}

/* 3. VTable Slot 2: 48kHz Audio Envelope -> Switch HD Rumble (160Hz/320Hz) & Deck Haptics */
static int flow_sdl3_push_audio_haptics(FlowPlatformState64 *state,
                                        int32_t audio_env_q16,
                                        int32_t ripple_jet_q16) {
    if (!state) return -1;

    int32_t env_pos = (audio_env_q16 < 0) ? -audio_env_q16 : audio_env_q16;
    int32_t rip_pos = (ripple_jet_q16 < 0) ? -ripple_jet_q16 : ripple_jet_q16;

    /* Theorem 14: Dual-Band Linear Resonant Actuator Clamping in [0, 65536] */
    int32_t low_160hz  = clamp_i32_bridge(env_pos + (rip_pos >> 1), 0, 65536);
    int32_t high_320hz = clamp_i32_bridge((env_pos >> 1) + rip_pos, 0, 65536);

    state->hd_rumble_low_160hz_q16  = (uint32_t)low_160hz;
    state->hd_rumble_high_320hz_q16 = (uint32_t)high_320hz;
    state->platform_mac             = calc_platform_state_mac64(state);
    return 0;
}

/* 4. VTable Slot 3: Console Docked/Handheld & Steam Deck BAT1 Power Governor */
static int flow_sdl3_get_power_state(FlowPlatformState64 *state,
                                     FlowWallpaperState64 *inout_wp,
                                     FlowFrameGovernor64 *inout_gov) {
    if (!state) return -1;

    bool is_docked = (state->operation_mode != FLOW_OPMODE_HANDHELD_720P);
    state->on_ac_power          = is_docked ? 1U : 0U;
    /* Theorem 13: 64 Ultra steps when Docked, 32 Fast steps when Handheld */
    state->raymarch_step_budget = is_docked ? 64U : 32U;

    if (inout_wp) {
        FlowDesktopHostConfig64 host_cfg;
        memset(&host_cfg, 0, sizeof(host_cfg));
        host_cfg.screen_width_px  = state->viewport_width_px;
        host_cfg.screen_height_px = state->viewport_height_px;
        (void)flow_wallpaper_step_governor64(inout_wp,
                                             &host_cfg,
                                             0,
                                             state->battery_pct,
                                             is_docked);
    }

    if (inout_gov) {
        inout_gov->raymarch_max_steps = state->raymarch_step_budget;
        inout_gov->target_budget_us   = is_docked ? FLOW_FPS120_TARGET_BUDGET_US : FLOW_FPS60_TARGET_BUDGET_US;
        inout_gov->quality_throttled  = is_docked ? 0U : 1U;
    }

    state->platform_mac = calc_platform_state_mac64(state);
    return 0;
}

void flow_platform_bridge_init64(FlowPlatformState64 *state,
                                 FlowNativeBridgeVTable64 *vtable,
                                 uint32_t platform_id,
                                 uint16_t initial_op_mode,
                                 uint16_t initial_battery_pct) {
    if (state) {
        memset(state, 0, sizeof(*state));
        state->platform_id      = platform_id;
        state->operation_mode   = initial_op_mode;
        state->battery_pct      = (initial_battery_pct > 100U) ? 100U : initial_battery_pct;
        state->on_ac_power      = (initial_op_mode != FLOW_OPMODE_HANDHELD_720P) ? 1U : 0U;
        if (initial_op_mode == FLOW_OPMODE_HANDHELD_720P) {
            state->viewport_width_px    = 1280U;
            state->viewport_height_px   = 720U;
            state->raymarch_step_budget = 32U;
        } else if (initial_op_mode == FLOW_OPMODE_DOCKED_1080P) {
            state->viewport_width_px    = 1920U;
            state->viewport_height_px   = 1080U;
            state->raymarch_step_budget = 64U;
        } else {
            state->viewport_width_px    = 3840U;
            state->viewport_height_px   = 2160U;
            state->raymarch_step_budget = 64U;
        }
        state->platform_mac = calc_platform_state_mac64(state);
    }

    if (vtable) {
        memset(vtable, 0, sizeof(*vtable));
        vtable->poll_input          = flow_sdl3_poll_input;
        vtable->submit_bus1k        = flow_sdl3_submit_bus1k;
        vtable->push_audio_haptics  = flow_sdl3_push_audio_haptics;
        vtable->get_power_state     = flow_sdl3_get_power_state;
        vtable->abi_magic           = FLOW_JSI_BRIDGE_MAGIC;
        vtable->backend_platform_id = platform_id;
        vtable->jsi_bus_size_bytes  = (uint32_t)sizeof(FlowEngineBus1K);
        vtable->shader_abi_hash     = 0x53444C3347505531ULL; /* "SDL3GPU1" */
        vtable->vtable_mac          = calc_vtable_mac64(vtable);
    }
}

void flow_platform_inject_imu_and_touch(FlowPlatformState64 *state,
                                        int32_t gyro_dx_q16,
                                        int32_t gyro_dy_q16,
                                        int32_t trackpad_scrub_dx_q16) {
    if (!state) return;
    state->gyro_aim_x_q16        = clamp_i32_bridge(state->gyro_aim_x_q16 + gyro_dx_q16, -65536, 65536);
    state->gyro_aim_y_q16        = clamp_i32_bridge(state->gyro_aim_y_q16 + gyro_dy_q16, -65536, 65536);
    state->trackpad_scrub_dx_q16 = trackpad_scrub_dx_q16;
    state->platform_mac          = calc_platform_state_mac64(state);
}

void flow_platform_notify_operation_mode(FlowPlatformState64 *state,
                                         uint16_t new_op_mode,
                                         uint16_t battery_pct,
                                         FlowSvgUiNode64 *svg_nodes,
                                         size_t svg_node_count,
                                         FlowSvgUiDomState64 *inout_dom,
                                         FlowWallpaperState64 *inout_wp,
                                         FlowGameEngineState *inout_engine) {
    if (!state) return;

    state->operation_mode = new_op_mode;
    state->battery_pct    = (battery_pct > 100U) ? 100U : battery_pct;

    if (new_op_mode == FLOW_OPMODE_HANDHELD_720P) {
        state->viewport_width_px  = 1280U;
        state->viewport_height_px = 720U;
    } else if (new_op_mode == FLOW_OPMODE_DOCKED_1080P) {
        state->viewport_width_px  = 1920U;
        state->viewport_height_px = 1080U;
    } else {
        state->viewport_width_px  = 3840U;
        state->viewport_height_px = 2160U;
    }

    FlowFrameGovernor64 *gov_ptr = inout_engine ? &inout_engine->fps_governor : NULL;
    (void)flow_sdl3_get_power_state(state, inout_wp, gov_ptr);
    if (inout_engine) {
        uint32_t target_fps = (new_op_mode == FLOW_OPMODE_HANDHELD_720P)
            ? ((battery_pct < 40U) ? 30U : 60U)
            : 120U;
        flow_fps_governor_set_mode(inout_engine, target_fps, true);
        inout_engine->fps_governor.raymarch_max_steps = state->raymarch_step_budget;
    }

    if (inout_dom) {
        inout_dom->viewport_width_px  = state->viewport_width_px;
        inout_dom->viewport_height_px = state->viewport_height_px;
    }

    /* Zero-Reflow O(1) Anchor Resolution across Handheld 720p <-> Docked 1080p/4K */
    if (svg_nodes && svg_node_count > 0U) {
        for (size_t i = 0U; i < svg_node_count; ++i) {
            int32_t cx = 0, cy = 0;
            flow_svg_ui_resolve_anchor_q16(&svg_nodes[i],
                                           state->viewport_width_px,
                                           state->viewport_height_px,
                                           &cx,
                                           &cy);
        }
    }
}
