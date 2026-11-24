#ifndef BLOOM_GPU_H
#define BLOOM_GPU_H

#include "bloom_atlas.h"
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
    bool     grounding_active;
    uint32_t max_tg_ground;
    double   last_ground_ms;
    bool     pbr_active;
    bool     sprite_active;
    uint32_t uploaded_textures;
} BloomGpuStats;

/**
 * Create Metal 3 Layer 2 GPU backend.
 * Pass NULL for ca_metal_layer in headless test/render mode.
 */
BloomGpu *bloom_gpu_create(void *ca_metal_layer, const char *shader_path);
void bloom_gpu_destroy(BloomGpu *gpu);

/** Access CoreText + RGBA8 Sprite Atlas owned by the GPU backend. */
BloomAtlas *bloom_gpu_get_atlas(BloomGpu *gpu);

/** Upload RGBA8 texture into Private+Opt 2D texture array (returns 1-based slice ID 1..32, 0 on error). */
uint16_t bloom_gpu_upload_texture_rgba8(BloomGpu *gpu, const uint8_t *rgba8,
                                        uint32_t w, uint32_t h, bool generate_mips);

/** Decode uncompressed KTX2 R8G8B8A8 payload and upload into Private+Opt texture array. */
uint16_t bloom_gpu_upload_ktx2(BloomGpu *gpu, const uint8_t *ktx2_bytes, size_t len);

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

/** Read back headless BGRA8 target into RGBA8 buffer (max_bytes >= w*h*4). */
bool bloom_gpu_readback_headless_rgba8(BloomGpu *gpu, uint8_t *out_rgba8, uint32_t max_bytes,
                                       uint32_t *out_w, uint32_t *out_h);

/**
 * Adaptive Half-Res ID Grounding + Picking (Single CommandBuffer):
 * Runs pure RGB pass followed by adaptive half-res R32Uint ID render + GPU reduction.
 * Normal render calls pay zero ID allocation and 0.000 ms latency overhead.
 */
bool bloom_gpu_render_grounded(BloomGpu *gpu, BloomScene *scene,
                               uint32_t vp_w, uint32_t vp_h,
                               uint32_t pick_x, uint32_t pick_y,
                               BloomGroundedGpu32 *out_stats, uint32_t max_slots,
                               BloomPickGpuResult *out_pick);

BloomGpuStats bloom_gpu_get_stats(const BloomGpu *gpu);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_GPU_H */
