#ifndef FLOW_WALLPAPER_H
#define FLOW_WALLPAPER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ricardian.h"

/*
 * ============================================================================
 * FLOW WALLPAPER ENGINE: Cross-Platform 64-Byte Zero-Token Live Wallpaper Core
 * ============================================================================
 * Enforces Lean 4 Formal Invariants (research/WallpaperEngine.lean):
 *   1. Occlusion Pause Hysteresis:
 *      - >= 90% occluded (58982 Q16) or fullscreen app -> FLOW_WP_POWER_PAUSED_0HZ (0 GPU wakeups)
 *      - <= 75% occluded (49152 Q16) -> resumes active/eco rendering (zero boundary chatter)
 *   2. Battery Eco-Governor Hysteresis:
 *      - Unplugged & battery <= 40% -> FLOW_WP_POWER_BATTERY_30HZ (33333 us budget)
 *      - AC connected or battery >= 50% -> FLOW_WP_POWER_ACTIVE_60HZ (16666 us budget)
 *   3. 2-Jet Cursor Parallax Framing Bound:
 *      - Clamped strictly within [-FLOW_WP_MAX_PARALLAX_Q16, +FLOW_WP_MAX_PARALLAX_Q16]
 *   4. 128-Char Hex Zero-Asset Wallpaper Workshop Share Code (64 Bytes = 1 Wallpaper)
 * ============================================================================
 */

#define FLOW_WP_OCCLUSION_PAUSE_ENTER_Q16 58982 /* 90% screen occlusion -> 0 Hz sleep */
#define FLOW_WP_OCCLUSION_PAUSE_EXIT_Q16  49152 /* 75% screen occlusion -> wake up */
#define FLOW_WP_BATTERY_ECO_ENTER_PCT     40U   /* <= 40% battery -> 30 Hz Eco mode */
#define FLOW_WP_BATTERY_ECO_EXIT_PCT      50U   /* >= 50% battery -> 60 Hz Active mode */
#define FLOW_WP_MAX_PARALLAX_Q16          16384 /* Max +-0.25 normalized camera parallax */

#define FLOW_WP_INTERVAL_ACTIVE_60HZ_US   16666U
#define FLOW_WP_INTERVAL_ECO_30HZ_US      33333U
#define FLOW_WP_INTERVAL_PAUSED_0HZ_US    0xFFFFFFFFU

/* Power & Frame Rate Modes */
#define FLOW_WP_POWER_ACTIVE_60HZ         0U
#define FLOW_WP_POWER_BATTERY_30HZ        1U
#define FLOW_WP_POWER_PAUSED_0HZ          2U

/* finish_and_flags Bit Layout */
#define FLOW_WP_FLAG_AUDIO_MUTE           (1U << 2) /* Default: muted in background wallpaper mode */
#define FLOW_WP_FLAG_CLICK_THROUGH        (1U << 3) /* Pass mouse clicks to OS desktop icons */
#define FLOW_WP_FLAG_ALL_SPACES           (1U << 4) /* Stationary across all virtual desktops */

/* Cross-Platform OS Desktop Attachment Backends */
#define FLOW_OS_DESKTOP_MACOS_CGLEVEL     0U /* NSWindow kCGDesktopWindowLevel + ignoresMouseEvents */
#define FLOW_OS_DESKTOP_WIN32_WORKERW     1U /* Win32 Progman 0x052C -> SHELLDLL_DefView -> WorkerW */
#define FLOW_OS_DESKTOP_LINUX_X11         2U /* X11 _NET_WM_WINDOW_TYPE_DESKTOP + XShape input mask */
#define FLOW_OS_DESKTOP_LINUX_WAYLAND     3U /* Wayland zwlr_layer_shell_v1 (BACKGROUND layer) */
#define FLOW_OS_DESKTOP_WEBGPU_CANVAS     4U /* Cross-Platform WebGPU / HTML5 Wallpaper Runner */

typedef struct __attribute__((aligned(64))) {
    uint64_t master_seed;             /* 8B: 64-bit Generative Aesthetic Master Seed */
    int32_t  cursor_x_q16;            /* 4B: Normalized desktop cursor X in [-65536, +65536] */
    int32_t  cursor_y_q16;            /* 4B: Normalized desktop cursor Y in [-65536, +65536] */
    int32_t  parallax_offset_x_q16;   /* 4B: 2-Jet damped camera parallax X in [-16384, +16384] */
    int32_t  parallax_offset_y_q16;   /* 4B: 2-Jet damped camera parallax Y in [-16384, +16384] */
    int32_t  audio_reactive_env_q16;  /* 4B: Desktop audio bass/peak envelope in [0, 65536] */
    int32_t  occlusion_ratio_q16;     /* 4B: Desktop occlusion ratio in [0, 65536] (>=58982 -> Pause) */
    uint32_t battery_percent;         /* 4B: Host battery percentage [0..100] (<=40 -> Eco) */
    uint32_t ac_power_connected;      /* 4B: 1 if on AC power, 0 if on battery */
    uint32_t power_mode;              /* 4B: FLOW_WP_POWER_* (0=60Hz, 1=30Hz Eco, 2=0Hz Paused) */
    uint32_t target_frame_interval_us;/* 4B: 16666 (60Hz), 33333 (30Hz), or 0xFFFFFFFF (Paused) */
    uint32_t vcam_preset_id;          /* 4B: Selected CineCamera lens (0=135mm,1=24mm,2=35mm,3=18mm) */
    uint32_t finish_and_flags;        /* 4B: Bits[0..1]=FinishClass, Bit[2]=AudioMute, Bit[3]=ClickThru */
    uint64_t wallpaper_mac;           /* 8B: Deterministic 64B Workshop Share Code MAC */
} FlowWallpaperState64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowWallpaperState64, RicardianStateV4);
_Static_assert(sizeof(FlowWallpaperState64) == 64U, "FlowWallpaperState64 must be 64 bytes");
_Static_assert(_Alignof(FlowWallpaperState64) == 64U, "FlowWallpaperState64 must be 64-byte aligned");

typedef struct __attribute__((aligned(64))) {
    uint64_t native_window_handle;    /* 8B: OS window handle (NSWindow* / HWND / XID / wl_surface*) */
    uint32_t os_platform_id;          /* 4B: FLOW_OS_DESKTOP_* (macOS / Win32 / X11 / Wayland / WebGPU) */
    uint32_t desktop_z_level;         /* 4B: OS desktop background z-order level */
    uint32_t screen_width_px;         /* 4B: Desktop monitor width in pixels */
    uint32_t screen_height_px;        /* 4B: Desktop monitor height in pixels */
    uint32_t click_through_enabled;   /* 4B: 1 = mouse clicks pass through to desktop icons */
    uint32_t stationary_all_spaces;   /* 4B: 1 = pinned across all virtual desktops/Spaces */
    uint32_t audio_muted_by_default;  /* 4B: 1 = muted background wallpaper, 0 = binaural audio on */
    uint32_t multi_monitor_index;     /* 4B: Display index (0..N-1) */
    uint32_t active_refresh_hz;       /* 4B: Current scheduled refresh rate (0, 30, or 60 Hz) */
    uint32_t eco_pause_transitions;   /* 4B: Count of 0Hz sleep/wake transitions */
    uint32_t frames_rendered_total;   /* 4B: Monotonic rendered wallpaper frames */
    uint32_t frames_skipped_sleep;    /* 4B: Frames skipped at 0% GPU in Paused0Hz */
    uint64_t host_seal_mac;           /* 8B: Cryptographic host attachment seal */
} FlowDesktopHostConfig64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowDesktopHostConfig64, RicardianStateV4);
_Static_assert(sizeof(FlowDesktopHostConfig64) == 64U, "FlowDesktopHostConfig64 must be 64 bytes");
_Static_assert(_Alignof(FlowDesktopHostConfig64) == 64U, "FlowDesktopHostConfig64 must be 64-byte aligned");

/* API: 1. Initialize 64B Wallpaper State & Cross-Platform Host Config */
void flow_wallpaper_init64(FlowWallpaperState64 *wp,
                           uint64_t master_seed,
                           uint32_t vcam_preset_id,
                           uint32_t finish_class,
                           bool audio_muted);

void flow_wallpaper_host_init64(FlowDesktopHostConfig64 *host,
                                uint32_t os_platform_id,
                                uint32_t width_px,
                                uint32_t height_px,
                                bool audio_muted);

/* API: 2. Dual-Threshold Occlusion Pause ([75%, 90%]) & Battery Eco ([40%, 50%]) Governor */
bool flow_wallpaper_step_governor64(FlowWallpaperState64 *wp,
                                    FlowDesktopHostConfig64 *host,
                                    int32_t occlusion_ratio_q16,
                                    uint32_t battery_percent,
                                    bool ac_power_connected);

/* API: 3. 2-Jet Critically-Damped Cursor Parallax & Audio-Reactive Envelope */
void flow_wallpaper_step_parallax_q16(FlowWallpaperState64 *wp,
                                      int32_t desktop_cursor_x_q16,
                                      int32_t desktop_cursor_y_q16,
                                      int32_t audio_sample_abs_q16);

/* API: 4. 128-Char Hex Zero-Asset Wallpaper Workshop Share Code (Round-Trip + MAC) */
void flow_wallpaper_encode_hex128(const FlowWallpaperState64 *wp, char out_hex[129]);
bool flow_wallpaper_decode_hex128(const char *hex_str, FlowWallpaperState64 *out_wp);
bool flow_wallpaper_verify_mac64(const FlowWallpaperState64 *wp);

/* API: 5. Export Self-Contained Cross-Platform WebGPU / HTML5 Live Wallpaper Runner */
int flow_wallpaper_export_web_runner(const FlowWallpaperState64 *wp, const char *html_path);

#endif /* FLOW_WALLPAPER_H */
