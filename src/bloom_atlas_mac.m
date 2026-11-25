#import <CoreGraphics/CoreGraphics.h>
#import <CoreText/CoreText.h>
#import <Foundation/Foundation.h>

#include "bloom_atlas.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t kSpriteAtlasDim = 512u;

typedef struct {
    uint32_t key_cp;
    uint16_t size_px;
    uint16_t x, y, w, h;
    float advance_x;
    float bearing_x;
    float bearing_y;
    bool occupied;
} GlyphEntry;

typedef struct {
    uint16_t size_px;
    CTFontRef ct_font;
    float ascent;
} FontSlot;

struct BloomAtlas {
    uint32_t width;
    uint32_t height;
    uint8_t *pixels;
    uint32_t shelf_x;
    uint32_t shelf_y;
    uint32_t shelf_h;
    uint32_t dirty_y0;
    uint32_t dirty_y1;
    bool has_dirty;

    uint8_t *sprite_pixels;
    uint32_t sp_shelf_x;
    uint32_t sp_shelf_y;
    uint32_t sp_shelf_h;
    uint32_t sp_dirty_y0;
    uint32_t sp_dirty_y1;
    bool sp_has_dirty;
    uint32_t sprite_cnt;

    GlyphEntry *entries;
    uint32_t entry_cnt;
    uint32_t entry_cap;

    FontSlot fonts[16];
    uint32_t font_cnt;
};

static CTFontRef get_font(BloomAtlas *a, uint16_t size_px, float *out_ascent) {
    if (size_px < 8u) size_px = 8u;
    if (size_px > 96u) size_px = 96u;
    for (uint32_t i = 0u; i < a->font_cnt; ++i) {
        if (a->fonts[i].size_px == size_px) {
            if (out_ascent) *out_ascent = a->fonts[i].ascent;
            return a->fonts[i].ct_font;
        }
    }
    CTFontRef f = CTFontCreateWithName(CFSTR("SFMono-Regular"), (CGFloat)size_px, NULL);
    if (!f) f = CTFontCreateWithName(CFSTR("Menlo-Regular"), (CGFloat)size_px, NULL);
    if (!f) f = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, (CGFloat)size_px, NULL);
    float asc = f ? (float)CTFontGetAscent(f) : (float)size_px * 0.8f;
    if (a->font_cnt < 16u) {
        a->fonts[a->font_cnt].size_px = size_px;
        a->fonts[a->font_cnt].ct_font = f;
        a->fonts[a->font_cnt].ascent = asc;
        a->font_cnt++;
    }
    if (out_ascent) *out_ascent = asc;
    return f;
}

static uint32_t utf8_next(const char **p) {
    const unsigned char *s = (const unsigned char *)(*p);
    if (!s || !*s) return 0u;
    uint32_t c0 = s[0];
    if (c0 < 0x80u) { *p += 1; return c0; }
    if ((c0 & 0xE0u) == 0xC0u && (s[1] & 0xC0u) == 0x80u) {
        *p += 2;
        return ((c0 & 0x1Fu) << 6u) | (s[1] & 0x3Fu);
    }
    if ((c0 & 0xF0u) == 0xE0u && (s[1] & 0xC0u) == 0x80u && (s[2] & 0xC0u) == 0x80u) {
        *p += 3;
        return ((c0 & 0x0Fu) << 12u) | ((uint32_t)(s[1] & 0x3Fu) << 6u) | (s[2] & 0x3Fu);
    }
    if ((c0 & 0xF8u) == 0xF0u && (s[1] & 0xC0u) == 0x80u && (s[2] & 0xC0u) == 0x80u && (s[3] & 0xC0u) == 0x80u) {
        *p += 4;
        return ((c0 & 0x07u) << 18u) | ((uint32_t)(s[1] & 0x3Fu) << 12u) |
               ((uint32_t)(s[2] & 0x3Fu) << 6u) | (s[3] & 0x3Fu);
    }
    *p += 1;
    return '?';
}

static const GlyphEntry *get_or_rasterize_glyph(BloomAtlas *a, uint32_t cp, uint16_t size_px) {
    for (uint32_t i = 0u; i < a->entry_cnt; ++i) {
        if (a->entries[i].occupied && a->entries[i].key_cp == cp && a->entries[i].size_px == size_px) {
            return &a->entries[i];
        }
    }
    if (a->entry_cnt >= a->entry_cap) {
        uint32_t ncap = a->entry_cap ? a->entry_cap * 2u : 256u;
        GlyphEntry *ne = (GlyphEntry *)realloc(a->entries, (size_t)ncap * sizeof(GlyphEntry));
        if (!ne) return NULL;
        a->entries = ne;
        a->entry_cap = ncap;
    }

    CTFontRef font = get_font(a, size_px, NULL);
    UniChar chars[2];
    CFIndex char_len = 0;
    if (cp <= 0xFFFFu) {
        chars[0] = (UniChar)cp;
        char_len = 1;
    } else {
        uint32_t u = cp - 0x10000u;
        chars[0] = (UniChar)(0xD800u + (u >> 10u));
        chars[1] = (UniChar)(0xDC00u + (u & 0x3FFu));
        char_len = 2;
    }
    CGGlyph glyphs[2] = {0, 0};
    if (font) CTFontGetGlyphsForCharacters(font, chars, glyphs, char_len);
    CGGlyph g = glyphs[0];

    CTFontRef used_font = font;
    bool release_used = false;
    if (font && g == 0) {
        CFStringRef cs = CFStringCreateWithCharactersNoCopy(kCFAllocatorDefault, chars, char_len, kCFAllocatorNull);
        if (cs) {
            CTFontRef fb = CTFontCreateForString(font, cs, CFRangeMake(0, char_len));
            if (fb) {
                CGGlyph fg[2] = {0, 0};
                if (CTFontGetGlyphsForCharacters(fb, chars, fg, char_len) && fg[0] != 0) {
                    g = fg[0];
                    used_font = fb;
                    release_used = true;
                } else {
                    CFRelease(fb);
                }
            }
            CFRelease(cs);
        }
    }

    CGSize adv = CGSizeZero;
    CGRect bbox = CGRectZero;
    if (used_font) {
        CTFontGetAdvancesForGlyphs(used_font, kCTFontOrientationDefault, &g, &adv, 1);
        CTFontGetBoundingRectsForGlyphs(used_font, kCTFontOrientationDefault, &g, &bbox, 1);
    }

    GlyphEntry ge;
    memset(&ge, 0, sizeof(ge));
    ge.key_cp = cp;
    ge.size_px = size_px;
    ge.advance_x = adv.width > 0.5 ? (float)adv.width : ((float)size_px * 0.6f);
    ge.occupied = true;

    if (!CGRectIsEmpty(bbox) && bbox.size.width > 0.01 && bbox.size.height > 0.01) {
        double x0 = floor(bbox.origin.x) - 1.0;
        double y0 = floor(bbox.origin.y) - 1.0;
        double x1 = ceil(bbox.origin.x + bbox.size.width) + 1.0;
        double y1 = ceil(bbox.origin.y + bbox.size.height) + 1.0;
        uint32_t bw = (uint32_t)(x1 - x0);
        uint32_t bh = (uint32_t)(y1 - y0);
        if (bw > 0u && bh > 0u && bw < a->width && bh < a->height) {
            if (a->shelf_x + bw + 2u > a->width) {
                a->shelf_y += a->shelf_h + 2u;
                a->shelf_x = 2u;
                a->shelf_h = 0u;
            }
            if (a->shelf_y + bh + 2u <= a->height) {
                uint8_t *tmp = (uint8_t *)calloc((size_t)bw * bh, 1u);
                if (tmp) {
                    CGContextRef ctx = CGBitmapContextCreate(tmp, bw, bh, 8, bw, NULL, (CGBitmapInfo)kCGImageAlphaOnly);
                    if (ctx) {
                        CGContextSetShouldAntialias(ctx, true);
                        CGContextSetAllowsAntialiasing(ctx, true);
                        CGPoint pos = CGPointMake(-x0, -y0);
                        CTFontDrawGlyphs(used_font, &g, &pos, 1, ctx);
                        CGContextRelease(ctx);
                        for (uint32_t row = 0u; row < bh; ++row) {
                            memcpy(a->pixels + (size_t)(a->shelf_y + row) * a->width + a->shelf_x,
                                   tmp + (size_t)row * bw, bw);
                        }
                        if (!a->has_dirty || a->shelf_y < a->dirty_y0) a->dirty_y0 = a->shelf_y;
                        if (!a->has_dirty || a->shelf_y + bh > a->dirty_y1) a->dirty_y1 = a->shelf_y + bh;
                        a->has_dirty = true;
                        ge.x = (uint16_t)a->shelf_x;
                        ge.y = (uint16_t)a->shelf_y;
                        ge.w = (uint16_t)bw;
                        ge.h = (uint16_t)bh;
                        ge.bearing_x = (float)x0;
                        ge.bearing_y = (float)y1;
                        a->shelf_x += bw + 2u;
                        if (bh > a->shelf_h) a->shelf_h = bh;
                    }
                    free(tmp);
                }
            }
        }
    }
    if (release_used) CFRelease(used_font);
    a->entries[a->entry_cnt] = ge;
    return &a->entries[a->entry_cnt++];
}

BloomAtlas *bloom_atlas_create(uint32_t width, uint32_t height) {
    BloomAtlas *a = (BloomAtlas *)calloc(1u, sizeof(BloomAtlas));
    if (!a) return NULL;
    a->width = width ? width : 1024u;
    a->height = height ? height : 1024u;
    a->pixels = (uint8_t *)calloc((size_t)a->width * a->height, 1u);
    a->sprite_pixels = (uint8_t *)calloc((size_t)kSpriteAtlasDim * kSpriteAtlasDim * 4u, 1u);
    if (!a->pixels || !a->sprite_pixels) {
        free(a->pixels);
        free(a->sprite_pixels);
        free(a);
        return NULL;
    }
    a->shelf_x = 2u;
    a->shelf_y = 2u;
    a->dirty_y0 = 0u;
    a->dirty_y1 = a->height;
    a->has_dirty = true;
    a->sp_shelf_x = 2u;
    a->sp_shelf_y = 2u;
    a->sp_dirty_y0 = 0u;
    a->sp_dirty_y1 = kSpriteAtlasDim;
    a->sp_has_dirty = true;
    return a;
}

void bloom_atlas_destroy(BloomAtlas *a) {
    if (!a) return;
    for (uint32_t i = 0u; i < a->font_cnt; ++i) {
        if (a->fonts[i].ct_font) CFRelease(a->fonts[i].ct_font);
    }
    free(a->entries);
    free(a->pixels);
    free(a->sprite_pixels);
    free(a);
}

static uint16_t clamp_q14_2(float v) {
    long q = lround((double)(v < 0.0f ? 0.0f : (v > 16383.75f ? 16383.75f : v)) * 4.0);
    return (uint16_t)(q < 0 ? 0 : (q > 65535 ? 65535 : q));
}

void bloom_atlas_measure_text(BloomAtlas *a, const char *utf8, float font_px,
                              float max_width, float *out_w, float *out_h) {
    if (!a || !utf8) {
        if (out_w) *out_w = 0.0f;
        if (out_h) *out_h = 0.0f;
        return;
    }
    uint16_t fpx = (uint16_t)lround((double)(font_px > 8.0f ? font_px : 14.0f));
    (void)get_font(a, fpx, NULL);
    float line_h = (float)fpx * 1.30f;
    float cur_w = 0.0f, max_line_w = 0.0f;
    uint32_t lines = 1u;
    const char *p = utf8;
    while (*p) {
        uint32_t cp = utf8_next(&p);
        if (cp == 0u) break;
        if (cp == '\n') {
            if (cur_w > max_line_w) max_line_w = cur_w;
            cur_w = 0.0f;
            lines++;
            continue;
        }
        const GlyphEntry *ge = get_or_rasterize_glyph(a, cp, fpx);
        float adv = ge ? ge->advance_x : ((float)fpx * 0.6f);
        if (max_width > 4.0f && cur_w > 0.5f && cur_w + adv > max_width) {
            if (cur_w > max_line_w) max_line_w = cur_w;
            cur_w = 0.0f;
            lines++;
        }
        cur_w += adv;
    }
    if (cur_w > max_line_w) max_line_w = cur_w;
    if (out_w) *out_w = max_line_w;
    if (out_h) *out_h = (float)lines * line_h;
}

uint32_t bloom_atlas_pack_runs(BloomAtlas *a, const BloomTextRun *runs, uint32_t run_cnt,
                               BloomGlyphInst *out_glyphs, uint32_t max_glyphs) {
    if (!a || !runs || !out_glyphs || max_glyphs == 0u) return 0u;
    uint32_t written = 0u;
    float inv_w = 65535.0f / (float)a->width;
    float inv_h = 65535.0f / (float)a->height;

    for (uint32_t r = 0u; r < run_cnt && written < max_glyphs; ++r) {
        const BloomTextRun *run = &runs[r];
        uint16_t fpx = (uint16_t)lround((double)(run->font_px > 8.0f ? run->font_px : 14.0f));
        float ascent = 0.0f;
        (void)get_font(a, fpx, &ascent);
        float line_h = (float)fpx * 1.30f;
        float pen_x = run->x;
        float baseline_y = run->y + ascent;
        const char *p = run->text;
        while (*p && written < max_glyphs) {
            uint32_t cp = utf8_next(&p);
            if (cp == 0u) break;
            if (cp == '\n') {
                pen_x = run->x;
                baseline_y += line_h;
                continue;
            }
            const GlyphEntry *ge = get_or_rasterize_glyph(a, cp, fpx);
            if (!ge) continue;
            if (run->wrap && run->w > 8.0f && pen_x > run->x + 0.5f && pen_x + ge->advance_x > run->x + run->w) {
                pen_x = run->x;
                baseline_y += line_h;
            }
            if (baseline_y - ascent >= run->y + run->h) break;
            if (ge->w > 0u && ge->h > 0u) {
                float gx = floorf(pen_x + ge->bearing_x + 0.5f);
                float gy = floorf(baseline_y - ge->bearing_y + 0.5f);
                if (gx + (float)ge->w > run->x && gx < run->x + run->w &&
                    gy < run->y + run->h) {
                    BloomGlyphInst inst;
                    inst.dst_q14_2[0] = clamp_q14_2(gx);
                    inst.dst_q14_2[1] = clamp_q14_2(gy);
                    inst.dst_q14_2[2] = clamp_q14_2((float)ge->w);
                    inst.dst_q14_2[3] = clamp_q14_2((float)ge->h);
                    inst.uv_unorm16[0] = (uint16_t)lround((double)ge->x * inv_w);
                    inst.uv_unorm16[1] = (uint16_t)lround((double)ge->y * inv_h);
                    inst.uv_unorm16[2] = (uint16_t)lround((double)ge->w * inv_w);
                    inst.uv_unorm16[3] = (uint16_t)lround((double)ge->h * inv_h);
                    inst.rgba8 = run->rgba8;
                    inst.clip_id = run->clip_id;
                    inst.z_flags = run->z_order;
                    out_glyphs[written++] = inst;
                }
            }
            pen_x += ge->advance_x;
        }
    }
    return written;
}

bool bloom_atlas_take_dirty(BloomAtlas *a, const uint8_t **out_pixels,
                            uint32_t *out_w, uint32_t *out_h,
                            uint32_t *out_y0, uint32_t *out_y1) {
    if (!a || !a->has_dirty) return false;
    if (out_pixels) *out_pixels = a->pixels;
    if (out_w) *out_w = a->width;
    if (out_h) *out_h = a->height;
    if (out_y0) *out_y0 = a->dirty_y0;
    if (out_y1) *out_y1 = a->dirty_y1;
    a->has_dirty = false;
    a->dirty_y0 = 0u;
    a->dirty_y1 = 0u;
    return true;
}

uint32_t bloom_atlas_glyph_count(const BloomAtlas *a) {
    return a ? a->entry_cnt : 0u;
}

bool bloom_atlas_add_sprite_rgba8(BloomAtlas *a, const uint8_t *rgba8,
                                  uint32_t w, uint32_t h,
                                  uint16_t out_uv_unorm16[4]) {
    if (!a || !rgba8 || w == 0u || h == 0u || w >= kSpriteAtlasDim - 4u || h >= kSpriteAtlasDim - 4u) {
        return false;
    }
    if (a->sp_shelf_x + w + 2u > kSpriteAtlasDim) {
        a->sp_shelf_y += a->sp_shelf_h + 2u;
        a->sp_shelf_x = 2u;
        a->sp_shelf_h = 0u;
    }
    if (a->sp_shelf_y + h + 2u > kSpriteAtlasDim) return false;

    for (uint32_t row = 0u; row < h; ++row) {
        memcpy(a->sprite_pixels + ((size_t)(a->sp_shelf_y + row) * kSpriteAtlasDim + a->sp_shelf_x) * 4u,
               rgba8 + (size_t)row * w * 4u,
               (size_t)w * 4u);
    }
    if (!a->sp_has_dirty || a->sp_shelf_y < a->sp_dirty_y0) a->sp_dirty_y0 = a->sp_shelf_y;
    if (!a->sp_has_dirty || a->sp_shelf_y + h > a->sp_dirty_y1) a->sp_dirty_y1 = a->sp_shelf_y + h;
    a->sp_has_dirty = true;

    if (out_uv_unorm16) {
        float inv = 65535.0f / (float)kSpriteAtlasDim;
        out_uv_unorm16[0] = (uint16_t)lround((double)a->sp_shelf_x * inv);
        out_uv_unorm16[1] = (uint16_t)lround((double)a->sp_shelf_y * inv);
        out_uv_unorm16[2] = (uint16_t)lround((double)w * inv);
        out_uv_unorm16[3] = (uint16_t)lround((double)h * inv);
    }
    a->sp_shelf_x += w + 2u;
    if (h > a->sp_shelf_h) a->sp_shelf_h = h;
    a->sprite_cnt++;
    return true;
}

bool bloom_atlas_update_sprite_rgba8(BloomAtlas *a, const uint16_t uv_unorm16[4],
                                     const uint8_t *rgba8, uint32_t w, uint32_t h) {
    if (!a || !uv_unorm16 || !rgba8 || w == 0u || h == 0u) return false;
    uint32_t sx = (uint32_t)lround(((double)uv_unorm16[0] * (double)kSpriteAtlasDim) / 65535.0);
    uint32_t sy = (uint32_t)lround(((double)uv_unorm16[1] * (double)kSpriteAtlasDim) / 65535.0);
    if (sx + w > kSpriteAtlasDim || sy + h > kSpriteAtlasDim) return false;

    for (uint32_t row = 0u; row < h; ++row) {
        memcpy(a->sprite_pixels + ((size_t)(sy + row) * kSpriteAtlasDim + sx) * 4u,
               rgba8 + (size_t)row * w * 4u,
               (size_t)w * 4u);
    }
    if (!a->sp_has_dirty || sy < a->sp_dirty_y0) a->sp_dirty_y0 = sy;
    if (!a->sp_has_dirty || sy + h > a->sp_dirty_y1) a->sp_dirty_y1 = sy + h;
    a->sp_has_dirty = true;
    return true;
}

bool bloom_atlas_take_sprite_dirty(BloomAtlas *a, const uint8_t **out_pixels,
                                   uint32_t *out_w, uint32_t *out_h,
                                   uint32_t *out_y0, uint32_t *out_y1) {
    if (!a || !a->sp_has_dirty) return false;
    if (out_pixels) *out_pixels = a->sprite_pixels;
    if (out_w) *out_w = kSpriteAtlasDim;
    if (out_h) *out_h = kSpriteAtlasDim;
    if (out_y0) *out_y0 = a->sp_dirty_y0;
    if (out_y1) *out_y1 = a->sp_dirty_y1;
    a->sp_has_dirty = false;
    a->sp_dirty_y0 = 0u;
    a->sp_dirty_y1 = 0u;
    return true;
}

uint32_t bloom_atlas_sprite_count(const BloomAtlas *a) {
    return a ? a->sprite_cnt : 0u;
}
