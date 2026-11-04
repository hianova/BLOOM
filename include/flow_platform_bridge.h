#ifndef FLOW_PLATFORM_BRIDGE_H
#define FLOW_PLATFORM_BRIDGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "flow_game.h"
#include "flow_gpu_abi.h"
#include "flow_shader_portability.h"
#include "flow_svg_ui.h"
#include "flow_wallpaper.h"

/*
 * ============================================================================
 * FLOW CROSS-PLATFORM JSI & TURBOMODULE NATIVE BRIDGE (64-Byte Pure C17)
 * ============================================================================
 * Unifies macOS (Metal), Steam Deck (SteamOS Vulkan RDNA2), and Nintendo Switch
 * (NVN / Vulkan SDL3_GPU) behind a zero-overhead 64-Byte VTable:
 *   1. JSI Render Bridge     : Submits 1 x FlowEngineBus1K (1024B) per frame.
 *   2. TurboModule Input/IMU : Maps Mouse / Trackpad / Joy-Con 6-Axis Gyro into
 *                              FlowInputIntent64 & FlowSvgUiDomState64 cursor.
 *   3. Audio -> HD Rumble    : Drives Switch HD Rumble (160Hz Low / 320Hz High)
 *                              & Steam Deck Haptics directly from 48kHz synth.
 *   4. Dock/Handheld Power   : Zero-Reflow hot-swap (720p Handheld <-> 1080p/4K
 *                              Docked) + Steam Deck BAT1 Eco TDP governor.
 * ============================================================================
 */

#define FLOW_PLATFORM_MACOS_METAL       0U
#define FLOW_PLATFORM_STEAMDECK_VULKAN  1U
#define FLOW_PLATFORM_SWITCH_NVN        2U

#define FLOW_OPMODE_HANDHELD_720P       0U /* 1280x720, Battery Eco (32 Raymarch steps) */
#define FLOW_OPMODE_DOCKED_1080P        1U /* 1920x1080, AC Docked (64 Ultra steps) */
#define FLOW_OPMODE_DOCKED_4K           2U /* 3840x2160, ProMotion / 4K Docked (64 Ultra steps) */

#define FLOW_JSI_BRIDGE_MAGIC           0x464C4F574A534931ULL /* "FLOWJSI1" */

/*
 * 1. 64-Byte Cross-Platform TurboModule Hardware, Gyro & HD Rumble State
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t platform_id;              /*  4B: FLOW_PLATFORM_* (macOS / SteamDeck / Switch) */
    uint16_t operation_mode;           /*  2B: FLOW_OPMODE_* (Handheld 720p / Docked 1080p / 4K) */
    uint16_t battery_pct;              /*  2B: Battery capacity [0..100%] (BAT1 / nn::psm / IOPS) */
    uint32_t on_ac_power;              /*  4B: 1 = Docked / AC Charger, 0 = Handheld Battery */
    uint32_t viewport_width_px;        /*  4B: Active viewport width (1280 / 1920 / 3840) */
    uint32_t viewport_height_px;       /*  4B: Active viewport height (720 / 1080 / 2160) */
    uint32_t raymarch_step_budget;     /*  4B: 32 (Handheld Eco) or 64 (Docked Ultra) */
    int32_t  gyro_aim_x_q16;           /*  4B: 6-Axis IMU / Joy-Con Gyro laser X [-65536..+65536] */
    int32_t  gyro_aim_y_q16;           /*  4B: 6-Axis IMU / Joy-Con Gyro laser Y [-65536..+65536] */
    int32_t  trackpad_scrub_dx_q16;    /*  4B: Steam Deck Trackpad / Switch Touch scrub delta Q16 */
    uint32_t hd_rumble_low_160hz_q16;  /*  4B: Switch HD Rumble / Deck Haptic 160Hz Low-Band Q16 */
    uint32_t hd_rumble_high_320hz_q16; /*  4B: Switch HD Rumble / Deck Haptic 320Hz High-Band Q16 */
    uint32_t bus1k_frames_submitted;   /*  4B: Total 1024-Byte FlowEngineBus1K JSI submissions */
    uint64_t last_bus1k_digest;        /*  8B: Deterministic 64-bit hash of latest FlowEngineBus1K */
    uint64_t platform_mac;             /*  8B: 64-bit cryptographic state seal */
} FlowPlatformState64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowPlatformState64, RicardianStateV4);
_Static_assert(sizeof(FlowPlatformState64) == 64U, "FlowPlatformState64 must be 64 bytes");
_Static_assert(_Alignof(FlowPlatformState64) == 64U, "FlowPlatformState64 must be 64-byte aligned");

/*
 * 2. 64-Byte React Native / SDL3_GPU Style Host VTable Descriptor
 */
typedef int (*FlowBridgePollInputFn)(FlowPlatformState64 *state,
                                     FlowInputIntent64 *out_intent,
                                     FlowSvgUiDomState64 *inout_dom);

typedef int (*FlowBridgeSubmitBus1KFn)(FlowPlatformState64 *state,
                                       const FlowEngineBus1K *bus1k);

typedef int (*FlowBridgePushAudioHapticsFn)(FlowPlatformState64 *state,
                                            int32_t audio_env_q16,
                                            int32_t ripple_jet_q16);

typedef int (*FlowBridgeGetPowerStateFn)(FlowPlatformState64 *state,
                                         FlowWallpaperState64 *inout_wp,
                                         FlowFrameGovernor64 *inout_gov);

typedef struct __attribute__((aligned(64))) {
    FlowBridgePollInputFn        poll_input;         /*  8B: TurboModule unified input + 6-DoF gyro poll */
    FlowBridgeSubmitBus1KFn      submit_bus1k;       /*  8B: JSI 1024-Byte FlowEngineBus1K GPU upload */
    FlowBridgePushAudioHapticsFn push_audio_haptics; /*  8B: 48kHz Synth -> Binaural + HD Rumble / Haptics */
    FlowBridgeGetPowerStateFn    get_power_state;    /*  8B: Handheld/Docked hot-swap & BAT1 TDP governor */
    uint64_t                     abi_magic;          /*  8B: FLOW_JSI_BRIDGE_MAGIC ("FLOWJSI1") */
    uint32_t                     backend_platform_id;/*  4B: FLOW_PLATFORM_* target identifier */
    uint32_t                     jsi_bus_size_bytes; /*  4B: Always sizeof(FlowEngineBus1K) == 1024 */
    uint64_t                     shader_abi_hash;    /*  8B: Single-Source Shader ABI compatibility hash */
    uint64_t                     vtable_mac;         /*  8B: 64-bit VTable integrity MAC */
} FlowNativeBridgeVTable64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowNativeBridgeVTable64, RicardianStateV4);
_Static_assert(sizeof(FlowNativeBridgeVTable64) == 64U, "FlowNativeBridgeVTable64 must be 64 bytes");
_Static_assert(_Alignof(FlowNativeBridgeVTable64) == 64U, "FlowNativeBridgeVTable64 must be 64-byte aligned");

/* Cross-Platform Bridge Lifecycle & Hot-Swap API (implemented in src/sdl3_gpu_host.c) */
void flow_platform_bridge_init64(FlowPlatformState64 *state,
                                 FlowNativeBridgeVTable64 *vtable,
                                 uint32_t platform_id,
                                 uint16_t initial_op_mode,
                                 uint16_t initial_battery_pct);

void flow_platform_inject_imu_and_touch(FlowPlatformState64 *state,
                                        int32_t gyro_dx_q16,
                                        int32_t gyro_dy_q16,
                                        int32_t trackpad_scrub_dx_q16);

void flow_platform_notify_operation_mode(FlowPlatformState64 *state,
                                         uint16_t new_op_mode,
                                         uint16_t battery_pct,
                                         FlowSvgUiNode64 *svg_nodes,
                                         size_t svg_node_count,
                                         FlowSvgUiDomState64 *inout_dom,
                                         FlowWallpaperState64 *inout_wp,
                                         FlowGameEngineState *inout_engine);

#endif /* FLOW_PLATFORM_BRIDGE_H */
