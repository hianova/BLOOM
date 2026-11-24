#ifndef BLOOM_GPU_H
#define BLOOM_GPU_H

#include "bloom_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BloomGpu BloomGpu;

typedef struct {
    uint32_t max_tg_cull;
    uint32_t max_tg_checksum;
    uint64_t submitted_cbs;
    uint64_t idle_skipped_frames;
    double   last_gpu_ms;
    double   last_cpu_pack_us;
    uint32_t last_wc_bytes;
    uint32_t atlas_glyph_count;
    uint32_t surviving_meshlets;
    uint32_t total_meshlets;
    bool     memoryless_depth_active;
    bool     private_opt_atlas_active;
} BloomGpuStats;

/**
 * Create Metal 3 Layer 2 GPU backend.
 * Pass NULL for ca_metal_layer in headless test/render mode.
 */
BloomGpu *bloom_gpu_create(void *ca_metal_layer, const char *shader_path);
void bloom_gpu_destroy(BloomGpu *gpu);

/** Triple-buffered Single-CB render into CAMetalLayer drawable (skips if idle and !force). */
bool bloom_gpu_render_drawable(BloomGpu *gpu, BloomScene *scene,
                               uint32_t vp_w, uint32_t vp_h, bool force);

/** Synchronous Single-CB offscreen 4K/viewport render into Private+Opt target (skips if idle and !force). */
bool bloom_gpu_render_headless(BloomGpu *gpu, BloomScene *scene,
                               uint32_t vp_w, uint32_t vp_h, bool force);

/** Compute deterministic pixel checksum of last headless frame using k_tex_checksum. */
uint32_t bloom_gpu_headless_checksum(BloomGpu *gpu);

/** Read back headless target strictly for explicit offline PPM inspection/testing. */
bool bloom_gpu_save_headless_ppm(BloomGpu *gpu, const char *ppm_path);

BloomGpuStats bloom_gpu_get_stats(const BloomGpu *gpu);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_GPU_H */
