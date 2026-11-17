#ifndef BLOOM_HOST_H
#define BLOOM_HOST_H

#include "bloom_abi.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * BLOOM v9.0.0 — Single-Track Cross-Platform SDL3 GPU & Audio Host ABI
 * ============================================================================
 * Pure C17 cross-platform GPU compute & native window host built atop SDL3 GPU:
 *   - Consumes the single-source shaders/bloom_core.wgsl compiled by W3C Naga
 *     into build/bloom_core.metal (MSL 2.4 on Apple Metal) and
 *     build/bloom_core.spv (SPIR-V on Vulkan / Mesa Lavapipe).
 *   - Uploads the 16,384-byte (256 x 64B) BloomEngineArena + 32,768-byte
 *     (512 x 64B) BloomMeshArena and dispatches 4 compute passes (RC C2/C1/C0
 *     + Primary Field/Metric/Cine Frame) into an RGBA8 storage texture.
 *   - Exports standalone WebGPU .html interactive bundles, CMX 3600 .edl reels,
 *     Kitty Graphics Protocol terminal frames, and 48kHz stereo WAV audio.
 * ============================================================================
 */

typedef struct BloomGpuCtx BloomGpuCtx;

BloomGpuCtx *bloom_gpu_init(BloomEngineArena *arena, const char *shader_path);
bool bloom_gpu_upload_mesh(BloomGpuCtx *ctx, const BloomMeshArena *mesh);

bool bloom_gpu_is_hardware_gpu(const BloomGpuCtx *ctx);
bool bloom_gpu_is_unified_memory(const BloomGpuCtx *ctx);
const char *bloom_gpu_device_name(const BloomGpuCtx *ctx);
const char *bloom_gpu_driver_name(const BloomGpuCtx *ctx);
uint32_t bloom_gpu_dispatch_count(const BloomGpuCtx *ctx);
uint32_t bloom_gpu_idle_skip_count(const BloomGpuCtx *ctx);

/* Dispatches GPU compute if !BLOOM_FLAG_IDLE_SLEEP or force_render == true */
bool bloom_gpu_render(BloomGpuCtx *ctx, bool force_render);

/* Downloads the latest RGBA8 GPU texture, computes FNV-1a hash + non-black pixel count,
 * and optionally writes binary P6 PPM when ppm_path is non-NULL and non-empty */
bool bloom_gpu_write_ppm(const BloomGpuCtx *ctx,
                         const char *ppm_path,
                         uint64_t *out_fnv_hash,
                         uint32_t *out_nonblack_px);

/* Opens a live interactive HiDPI SDL3 GPU window driving step_cb(user_ctx) */
int bloom_gpu_run_window(BloomGpuCtx *ctx,
                         BloomEngineArena *arena,
                         void (*step_cb)(void *),
                         void *user_ctx);

bool bloom_gpu_push_audio_i16(BloomGpuCtx *ctx,
                              const int16_t *stereo_pcm,
                              uint32_t sample_frames);

bool bloom_write_wav_i16(const char *wav_path,
                         const int16_t *stereo_pcm,
                         uint32_t sample_frames,
                         uint32_t sample_rate_hz);

bool bloom_export_webgpu_html(const char *html_path,
                              const BloomEngineArena *arena,
                              const char *wgsl_path);

bool bloom_write_cmx3600_edl(const char *edl_path,
                             const char *reel_title,
                             uint32_t total_frames,
                             uint32_t fps);

bool bloom_gpu_write_kitty_tty(const BloomGpuCtx *ctx,
                               FILE *out_stream,
                               uint32_t max_cols_px);

void bloom_gpu_destroy(BloomGpuCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_HOST_H */
