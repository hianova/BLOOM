#ifndef BLOOM_HOST_H
#define BLOOM_HOST_H

#include "bloom_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * BLOOM v6.0.0 — Single-Track Cross-Platform SDL3 GPU & Window Host ABI
 * ============================================================================
 * Pure C17 cross-platform GPU compute & native window host built atop SDL3 GPU:
 *   - Consumes the single-source shaders/bloom_core.wgsl compiled by W3C Naga
 *     into build/bloom_core.metal (MSL 2.4 on Apple Metal) and
 *     build/bloom_core.spv (SPIR-V on Vulkan / Mesa Lavapipe).
 *   - Uploads the 16,384-byte (256 x 64B) BloomEngineArena and dispatches
 *     bloom_render_frame into an RGBA8 storage texture with 0-FPS IDLE_SLEEP.
 *   - Supports both headless/offscreen GPU compute verification (PPM + FNV-1a)
 *     and 60Hz interactive HiDPI native windowing + CJK UTF-8 IME input.
 *   - Zero fake software gradient fallbacks: fails honestly if no GPU exists.
 * ============================================================================
 */

typedef struct BloomGpuCtx BloomGpuCtx;

BloomGpuCtx *bloom_gpu_init(BloomEngineArena *arena, const char *shader_path);

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

void bloom_gpu_destroy(BloomGpuCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_HOST_H */
