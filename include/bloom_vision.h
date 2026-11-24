#ifndef BLOOM_VISION_H
#define BLOOM_VISION_H

#include "bloom_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BloomVision BloomVision;

typedef struct {
    bool                 found;
    uint16_t             template_id;
    char                 template_name[32];
    float                pos[3];
    float                quat[4];
    float                euler_ypr[3];
    float                scale;
    float                dof0;
    float                iou;
    float                edge_err;
    float                lum_err;
    float                score;
    double               gpu_ms;
    BloomStateProjection projection;
} BloomVisionMatch;

/** Create hardware TBDR 16x8 Tiled Analysis-by-Synthesis localizer. */
BloomVision *bloom_vision_create(const char *shader_path);
void bloom_vision_destroy(BloomVision *v);

/** Register a canonical BloomVtx12 meshlet template for visual recognition + 6-DoF alignment. */
bool bloom_vision_register_template_mesh(BloomVision *v, uint16_t template_id, const char *name,
                                         uint32_t meshlet_cnt, const BloomMeshletHeader32 *hdrs,
                                         const BloomVtx12 *verts, const uint8_t *local_idx,
                                         uint32_t idx_bytes);

/** Convenience helper registering an existing template directly from BloomScene. */
bool bloom_vision_register_from_scene(BloomVision *v, const BloomScene *scene,
                                      uint16_t template_id, const char *name);

/**
 * Localize + classify an RGBA8 frame via GPU 64x64 Sobel+JFA + 16x8 TBDR render-and-compare.
 * Emits 6-DoF pose, template classification, DOF0 estimate, and ready-to-apply BloomStateProjection.
 */
bool bloom_vision_localize_rgba8(BloomVision *v, const uint8_t *rgba8_pixels,
                                 uint32_t img_w, uint32_t img_h,
                                 const BloomCamera *cam,
                                 const float hint_pos[3], float search_radius,
                                 BloomVisionMatch *out_match);

/** Minimum maxTotalThreadsPerThreadgroup across all Compute PSOs (Gate: == 1024 on M1). */
uint32_t bloom_vision_get_max_tg(const BloomVision *v);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_VISION_H */
