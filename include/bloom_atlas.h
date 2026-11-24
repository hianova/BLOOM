#ifndef BLOOM_ATLAS_H
#define BLOOM_ATLAS_H

#include "bloom_scene.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct BloomAtlas BloomAtlas;

BloomAtlas *bloom_atlas_create(uint32_t width, uint32_t height);
void bloom_atlas_destroy(BloomAtlas *atlas);

/** Rasterize missing glyphs via CoreText/CoreGraphics (with CJK fallback) and pack BloomGlyphInst. */
uint32_t bloom_atlas_pack_runs(BloomAtlas *atlas, const BloomTextRun *runs, uint32_t run_cnt,
                               BloomGlyphInst *out_glyphs, uint32_t max_glyphs);

/** Measure intrinsic bounding box of UTF-8 text (respecting newlines & optional max_width wrap). */
void bloom_atlas_measure_text(BloomAtlas *atlas, const char *utf8, float font_px,
                              float max_width, float *out_w, float *out_h);

/** Return dirty row span for incremental Blit into Private+Opt R8Unorm GPU texture. */
bool bloom_atlas_take_dirty(BloomAtlas *atlas, const uint8_t **out_pixels,
                            uint32_t *out_w, uint32_t *out_h,
                            uint32_t *out_y0, uint32_t *out_y1);

uint32_t bloom_atlas_glyph_count(const BloomAtlas *atlas);

/** Pack an RGBA8 icon/sprite into the 512x512 RGBA8 Sprite Atlas and return normalized unorm16 UVs. */
bool bloom_atlas_add_sprite_rgba8(BloomAtlas *atlas, const uint8_t *rgba8,
                                  uint32_t w, uint32_t h,
                                  uint16_t out_uv_unorm16[4]);

/** Return dirty row span for incremental Blit into Private+Opt RGBA8 Sprite Atlas texture. */
bool bloom_atlas_take_sprite_dirty(BloomAtlas *atlas, const uint8_t **out_pixels,
                                   uint32_t *out_w, uint32_t *out_h,
                                   uint32_t *out_y0, uint32_t *out_y1);

uint32_t bloom_atlas_sprite_count(const BloomAtlas *atlas);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_ATLAS_H */
