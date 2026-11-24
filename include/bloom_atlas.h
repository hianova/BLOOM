#ifndef BLOOM_ATLAS_H
#define BLOOM_ATLAS_H

#include "bloom_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BloomAtlas BloomAtlas;

BloomAtlas *bloom_atlas_create(uint32_t width, uint32_t height);
void bloom_atlas_destroy(BloomAtlas *atlas);

/** Rasterize missing glyphs via CoreText/CoreGraphics and pack BloomGlyphInst array. */
uint32_t bloom_atlas_pack_runs(BloomAtlas *atlas, const BloomTextRun *runs, uint32_t run_cnt,
                               BloomGlyphInst *out_glyphs, uint32_t max_glyphs);

/** Return dirty row span for incremental Blit into Private+Opt R8Unorm GPU texture. */
bool bloom_atlas_take_dirty(BloomAtlas *atlas, const uint8_t **out_pixels,
                            uint32_t *out_w, uint32_t *out_h,
                            uint32_t *out_y0, uint32_t *out_y1);

uint32_t bloom_atlas_glyph_count(const BloomAtlas *atlas);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_ATLAS_H */
