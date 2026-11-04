/*
 * ============================================================================
 * FLOW WALLPAPER ENGINE: Formal Verification & Unit Test Suite (Pure C17)
 * ============================================================================
 * Validates:
 *   1. 64-Byte Univalence of FlowWallpaperState64 & FlowDesktopHostConfig64.
 *   2. Dual-Threshold Occlusion Pause Hysteresis ([75%, 90%]) & 0 Hz GPU Sleep.
 *   3. Dual-Threshold Battery Eco-Governor Hysteresis ([40%, 50%]) & AC Hot-Plug.
 *   4. 2-Jet Cursor Parallax Convergence & Framing Clamp ([-0.25, +0.25] Q16).
 *   5. 128-Char Hex Share Code Round-Trip, 1-Bit Tamper Rejection & Web Runner.
 * ============================================================================
 */

#include "flow_wallpaper.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_64b_univalence(void) {
    printf("  [Test 1/5] 64B Univalence (FlowWallpaperState64 & FlowDesktopHostConfig64)... ");
    assert(sizeof(FlowWallpaperState64) == 64U);
    assert(_Alignof(FlowWallpaperState64) == 64U);
    assert(sizeof(FlowDesktopHostConfig64) == 64U);
    assert(_Alignof(FlowDesktopHostConfig64) == 64U);
    printf("PASS (64B / 64B)\n");
}

static void test_occlusion_pause_hysteresis(void) {
    printf("  [Test 2/5] Dual-Threshold Occlusion Hysteresis ([75%%, 90%%]) & 0 Hz Sleep... ");
    FlowWallpaperState64 wp;
    FlowDesktopHostConfig64 host;
    flow_wallpaper_init64(&wp, 0x20261105ULL, 2U, 1U, true);
    flow_wallpaper_host_init64(&host, FLOW_OS_DESKTOP_MACOS_CGLEVEL, 3840U, 2160U, true);

    /* Initially active at 60 Hz */
    assert(wp.power_mode == FLOW_WP_POWER_ACTIVE_60HZ);
    assert(host.active_refresh_hz == 60U);

    /* Rise to 88% occlusion (57671 < 58982 Q16): must remain ACTIVE (no premature pause) */
    bool should_render = flow_wallpaper_step_governor64(&wp, &host, 57671, 100U, true);
    assert(should_render);
    assert(wp.power_mode == FLOW_WP_POWER_ACTIVE_60HZ);
    assert(host.active_refresh_hz == 60U);

    /* Reach 90% occlusion (58982 Q16): must transition to PAUSED_0HZ (0 GPU wakeups) */
    should_render = flow_wallpaper_step_governor64(&wp, &host, FLOW_WP_OCCLUSION_PAUSE_ENTER_Q16, 100U, true);
    assert(!should_render);
    assert(wp.power_mode == FLOW_WP_POWER_PAUSED_0HZ);
    assert(host.active_refresh_hz == 0U);
    assert(host.eco_pause_transitions == 1U);
    assert(host.frames_skipped_sleep == 1U);

    /* Jitter 10,000 times inside deadband [49153, 58981]: must stay PAUSED_0HZ with 0 extra transitions */
    for (uint32_t i = 0U; i < 10000U; ++i) {
        int32_t occ_q16 = FLOW_WP_OCCLUSION_PAUSE_EXIT_Q16 + 1 + (int32_t)(i % 9800U);
        should_render = flow_wallpaper_step_governor64(&wp, &host, occ_q16, 100U, true);
        assert(!should_render);
        assert(wp.power_mode == FLOW_WP_POWER_PAUSED_0HZ);
        assert(host.active_refresh_hz == 0U);
    }
    assert(host.eco_pause_transitions == 1U);
    assert(host.frames_skipped_sleep == 10001U);

    /* Drop to 75% (49152 Q16 <= FLOW_WP_OCCLUSION_PAUSE_EXIT_Q16): must resume ACTIVE_60HZ */
    should_render = flow_wallpaper_step_governor64(&wp, &host, FLOW_WP_OCCLUSION_PAUSE_EXIT_Q16, 100U, true);
    assert(should_render);
    assert(wp.power_mode == FLOW_WP_POWER_ACTIVE_60HZ);
    assert(host.active_refresh_hz == 60U);
    assert(host.eco_pause_transitions == 2U);

    printf("PASS (0 Hz Sleep Verified, 0 Deadband Oscillation across 10k jitter steps)\n");
}

static void test_battery_eco_hysteresis(void) {
    printf("  [Test 3/5] Battery Eco-Governor Hysteresis ([40%%, 50%%]) & AC Hot-Plug... ");
    FlowWallpaperState64 wp;
    FlowDesktopHostConfig64 host;
    flow_wallpaper_init64(&wp, 0xCAFEBABEULL, 1U, 2U, true);
    flow_wallpaper_host_init64(&host, FLOW_OS_DESKTOP_WIN32_WORKERW, 2560U, 1440U, true);

    /* Unplug AC at 45% battery (> 40%): stays ACTIVE_60HZ */
    bool should_render = flow_wallpaper_step_governor64(&wp, &host, 0, 45U, false);
    assert(should_render);
    assert(wp.power_mode == FLOW_WP_POWER_ACTIVE_60HZ);
    assert(host.active_refresh_hz == 60U);

    /* Battery drops to 40%: enters BATTERY_30HZ (33333 us budget) */
    should_render = flow_wallpaper_step_governor64(&wp, &host, 0, 40U, false);
    assert(should_render);
    assert(wp.power_mode == FLOW_WP_POWER_BATTERY_30HZ);
    assert(host.active_refresh_hz == 30U);
    assert(wp.target_frame_interval_us == FLOW_WP_INTERVAL_ECO_30HZ_US);

    /* Battery fluctuates in [41%, 49%] without AC: stays BATTERY_30HZ */
    for (uint32_t bat = 41U; bat <= 49U; ++bat) {
        should_render = flow_wallpaper_step_governor64(&wp, &host, 0, bat, false);
        assert(should_render);
        assert(wp.power_mode == FLOW_WP_POWER_BATTERY_30HZ);
        assert(host.active_refresh_hz == 30U);
    }

    /* Battery charges to 50% (>= FLOW_WP_BATTERY_ECO_EXIT_PCT): resumes ACTIVE_60HZ */
    should_render = flow_wallpaper_step_governor64(&wp, &host, 0, 50U, false);
    assert(should_render);
    assert(wp.power_mode == FLOW_WP_POWER_ACTIVE_60HZ);
    assert(host.active_refresh_hz == 60U);

    /* Re-enter ECO at 25%, then hot-plug AC power: immediately resumes ACTIVE_60HZ */
    should_render = flow_wallpaper_step_governor64(&wp, &host, 0, 25U, false);
    assert(wp.power_mode == FLOW_WP_POWER_BATTERY_30HZ);
    should_render = flow_wallpaper_step_governor64(&wp, &host, 0, 25U, true);
    assert(wp.power_mode == FLOW_WP_POWER_ACTIVE_60HZ);
    assert(host.active_refresh_hz == 60U);

    printf("PASS (30 Hz Eco Mode & AC Restore Verified)\n");
}

static void test_cursor_parallax_and_audio_envelope(void) {
    printf("  [Test 4/5] 2-Jet Cursor Parallax Convergence & Framing Clamp... ");
    FlowWallpaperState64 wp;
    flow_wallpaper_init64(&wp, 0x13572468ULL, 3U, 0U, false);

    /* Drive cursor to extreme corner (+65536, -65536) with high audio energy */
    for (int i = 0; i < 64; ++i) {
        flow_wallpaper_step_parallax_q16(&wp, 65536, -65536, 50000);
        assert(wp.parallax_offset_x_q16 >= -FLOW_WP_MAX_PARALLAX_Q16);
        assert(wp.parallax_offset_x_q16 <= FLOW_WP_MAX_PARALLAX_Q16);
        assert(wp.parallax_offset_y_q16 >= -FLOW_WP_MAX_PARALLAX_Q16);
        assert(wp.parallax_offset_y_q16 <= FLOW_WP_MAX_PARALLAX_Q16);
    }

    /* Must converge to +16384 and -16384 (exact framing bound) */
    assert(wp.parallax_offset_x_q16 > 16000 && wp.parallax_offset_x_q16 <= FLOW_WP_MAX_PARALLAX_Q16);
    assert(wp.parallax_offset_y_q16 < -16000 && wp.parallax_offset_y_q16 >= -FLOW_WP_MAX_PARALLAX_Q16);
    assert(wp.audio_reactive_env_q16 > 30000);

    /* Even with out-of-bounds cursor input, offset never exceeds +/-16384 */
    for (int i = 0; i < 64; ++i) {
        flow_wallpaper_step_parallax_q16(&wp, 500000, -500000, 0);
        assert(wp.parallax_offset_x_q16 <= FLOW_WP_MAX_PARALLAX_Q16);
        assert(wp.parallax_offset_y_q16 >= -FLOW_WP_MAX_PARALLAX_Q16);
    }
    assert(wp.audio_reactive_env_q16 < 30000);
    assert(flow_wallpaper_verify_mac64(&wp));

    printf("PASS (x_q16=%d, y_q16=%d within [-16384, +16384])\n",
           wp.parallax_offset_x_q16, wp.parallax_offset_y_q16);
}

static void test_hex128_workshop_and_web_export(void) {
    printf("  [Test 5/5] 128-Char Hex Share Code Round-Trip, Tamper Gate & Web Runner... ");
    FlowWallpaperState64 orig;
    flow_wallpaper_init64(&orig, 0x20261105CAFEULL, 2U, 3U, true);
    flow_wallpaper_step_parallax_q16(&orig, 32768, -16384, 42000);

    char hex128[129];
    flow_wallpaper_encode_hex128(&orig, hex128);
    assert(strlen(hex128) == 128U);

    FlowWallpaperState64 decoded;
    assert(flow_wallpaper_decode_hex128(hex128, &decoded));
    assert(memcmp(&orig, &decoded, sizeof(FlowWallpaperState64)) == 0);
    assert(flow_wallpaper_verify_mac64(&decoded));

    /* Flip 1 hex nibble in the share code -> must fail MAC check */
    char tampered[129];
    memcpy(tampered, hex128, 129U);
    tampered[0] = (tampered[0] == '0') ? '1' : '0';
    FlowWallpaperState64 bad;
    assert(!flow_wallpaper_decode_hex128(tampered, &bad));

    /* Export cross-platform HTML5 / WebGL2 live wallpaper runner */
    int rc = flow_wallpaper_export_web_runner(&orig, "build/flow_wallpaper_web.html");
    assert(rc == 0);

    FILE *f = fopen("build/flow_wallpaper_web.html", "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fclose(f);
    assert(fsize > 1024L && fsize < 16384L);

    printf("PASS (128-Hex MAC Verified, Web Runner=%ld Bytes)\n", fsize);
}

int main(void) {
    printf("================================================================================\n");
    printf("  FLOW WALLPAPER ENGINE: Cross-Platform Zero-Token Live Wallpaper Suite\n");
    printf("================================================================================\n");
    test_64b_univalence();
    test_occlusion_pause_hysteresis();
    test_battery_eco_hysteresis();
    test_cursor_parallax_and_audio_envelope();
    test_hex128_workshop_and_web_export();
    printf("================================================================================\n");
    printf("  ALL 5 FLOW WALLPAPER ENGINE TESTS PASSED (0 ERRORS)\n");
    printf("================================================================================\n");
    return 0;
}
