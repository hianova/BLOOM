#include "flow_procedural_assets.h"
#include "flow_lean_abi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__extension__ typedef __int128 i128;
__extension__ typedef unsigned __int128 u128;

static inline uint64_t local_join_assembly(uint64_t at1, uint64_t at2, uint64_t shared_sub) {
    uint64_t u = (at1 <= 1ULL) ? 1ULL : at1;
    uint64_t v = (at2 <= 1ULL) ? 1ULL : at2;
    uint64_t min_at = (u < v) ? u : v;
    uint64_t reuse = (shared_sub < (min_at - 1ULL)) ? shared_sub : (min_at - 1ULL);
    return (u + v) - reuse;
}

static inline int64_t q16_abs(int64_t x) {
    return (x == INT64_MIN) ? INT64_MAX : ((x < 0) ? -x : x);
}

static inline int64_t q16_min(int64_t a, int64_t b) { return (a < b) ? a : b; }
static inline int64_t q16_max(int64_t a, int64_t b) { return (a > b) ? a : b; }

static inline int64_t q16_length3(int64_t x, int64_t y, int64_t z) {
    u128 sq = (u128)((i128)x * (i128)x + (i128)y * (i128)y + (i128)z * (i128)z);
    return (int64_t)flow_asm_isqrt_u128(sq);
}

static inline int64_t q16_length2(int64_t x, int64_t y) {
    u128 sq = (u128)((i128)x * (i128)x + (i128)y * (i128)y);
    return (int64_t)flow_asm_isqrt_u128(sq);
}

static inline int64_t q16_smin(int64_t a, int64_t b, int64_t k_q16) {
    if (k_q16 <= 0)
        return q16_min(a, b);
    int64_t diff = q16_abs(a - b);
    int64_t h = q16_max(k_q16 - diff, 0);
    int64_t corr = (int64_t)(((i128)h * (i128)h) / (4 * (i128)k_q16));
    return q16_min(a, b) - corr;
}

/* ============================================================================
 * 1. 0-Token Combinatorial 3D Shape Generator & 8x8 Atlas Renderer
 * ============================================================================ */

static const uint32_t kGradePalette[5] = {
    0xFFD700U, /* GRADE_S: Solar Gold */
    0x00FFCCU, /* GRADE_A: Cyan Plasma */
    0x44AAFFU, /* GRADE_B: Cobalt Alloy */
    0x88CC88U, /* GRADE_C: Standard Olive */
    0xAA7755U  /* GRADE_D: Rust Silicate */
};

FlowGenomeRecipe64 flow_genome_evolve_one(uint64_t seed, uint32_t slot_id) {
    uint64_t s = seed ^ ((uint64_t)(slot_id + 1U) * FLOW_GOLDEN_RATIO_64);
    uint64_t r0 = flow_prng_next(&s);
    uint64_t r1 = flow_prng_next(&s);
    uint64_t r2 = flow_prng_next(&s);

    FlowGenomeRecipe64 out;
    memset(&out, 0, sizeof(out));
    out.genome_seed = seed;
    out.recipe_id = slot_id;

    out.part_ids[0] = (uint8_t)(r0 & 0x0FU);
    out.part_ids[1] = (uint8_t)((r0 >> 4U) & 0x0FU);
    out.part_ids[2] = (uint8_t)((r0 >> 8U) & 0x0FU);
    out.part_ids[3] = (uint8_t)((r0 >> 12U) & 0x0FU);

    /* Ricardian Assembly Theory hierarchical join across 3 stages */
    uint64_t sub1 = local_join_assembly(2ULL + (out.part_ids[0] & 3ULL),
                                        2ULL + (out.part_ids[1] & 3ULL),
                                        (out.part_ids[0] == out.part_ids[1]) ? 1ULL : 0ULL);
    uint64_t sub2 = local_join_assembly(2ULL + (out.part_ids[2] & 3ULL),
                                        2ULL + (out.part_ids[3] & 3ULL),
                                        (out.part_ids[2] == out.part_ids[3]) ? 1ULL : 0ULL);
    uint32_t shared = (uint32_t)((r0 >> 16U) & 0x03U);
    out.shared_sub_count = shared;
    out.assembly_index = (uint32_t)local_join_assembly(sub1, sub2, shared);

    out.ricardian_grade = (uint32_t)(r1 % 5ULL);
    out.fold_flags = FLOW_GENE_FOLD_MIRROR_X | FLOW_GENE_SMOOTH_UNION;
    if ((r1 & 0x08ULL) != 0ULL)
        out.fold_flags |= FLOW_GENE_FOLD_MIRROR_Y;
    if ((r1 & 0x10ULL) != 0ULL)
        out.fold_flags |= FLOW_GENE_FOLD_POLAR_4;
    else if ((r1 & 0x20ULL) != 0ULL)
        out.fold_flags |= FLOW_GENE_FOLD_POLAR_8;
    if ((r1 & 0x40ULL) != 0ULL)
        out.fold_flags |= FLOW_GENE_GREEBLE_PANEL;

    /* Q16 dimensions in [0.18, 0.55] of unit cell */
    out.radii_q16[0] = (int32_t)(16384 + (int64_t)(r1 & 0x3FFFULL));        /* Core radius */
    out.radii_q16[1] = (int32_t)(12288 + (int64_t)((r1 >> 16U) & 0x3FFFULL));/* Ring/wing major */
    out.radii_q16[2] = (int32_t)(4096 + (int64_t)((r2)&0x1FFFULL));          /* Ring/wing minor */
    out.radii_q16[3] = (int32_t)(6553 + (int64_t)((r2 >> 16U) & 0x1FFFULL)); /* Smooth blend k */

    out.elongation_q16[0] = (int32_t)(8192 + (int64_t)((r2 >> 32U) & 0x3FFFULL));
    out.elongation_q16[1] = (int32_t)(8192 + (int64_t)((r2 >> 48U) & 0x3FFFULL));
    out.rgb_accent_hex = kGradePalette[out.ricardian_grade];
    out.checksum = (uint32_t)((r0 ^ r1 ^ r2) & 0xFFFFFFFFULL) | 1U;
    return out;
}

void flow_genome_evolve_batch64(uint64_t base_seed, FlowGenomeRecipe64 out_batch[64]) {
    if (!out_batch)
        return;
    for (uint32_t i = 0; i < 64U; ++i) {
        out_batch[i] = flow_genome_evolve_one(base_seed, i);
    }
}

int64_t flow_genome_eval_sdf_q16(const FlowGenomeRecipe64 *r, int64_t x_q16, int64_t y_q16,
                                 int64_t z_q16) {
    if (!r)
        return FLOW_Q16_ONE;

    int64_t px = (r->fold_flags & FLOW_GENE_FOLD_MIRROR_X) ? q16_abs(x_q16) : x_q16;
    int64_t py = (r->fold_flags & FLOW_GENE_FOLD_MIRROR_Y) ? q16_abs(y_q16) : y_q16;
    int64_t pz = q16_abs(z_q16);

    /* Core hull: elongated capsule / octahedron / sphere depending on part_ids[0] */
    int64_t ex = q16_max(px - r->elongation_q16[0] / 2, 0);
    int64_t ey = q16_max(py - r->elongation_q16[1] / 2, 0);
    int64_t d_core = 0;
    if ((r->part_ids[0] & 1U) == 0U) {
        d_core = q16_length3(ex, ey, pz) - r->radii_q16[0];
    } else {
        /* Octahedral / beveled industrial hull */
        d_core = ((ex + ey + pz) * 37837LL) / 65536LL - r->radii_q16[0];
    }

    /* Symmetry-folded ring / wing / nacelle module */
    int64_t fx = px;
    int64_t fy = py;
    if (r->fold_flags & (FLOW_GENE_FOLD_POLAR_4 | FLOW_GENE_FOLD_POLAR_8)) {
        /* 8-fold octant reflection (exact branchless polar symmetry in Q16) */
        if (fx < fy) {
            int64_t tmp = fx;
            fx = fy;
            fy = tmp;
        }
        if (r->fold_flags & FLOW_GENE_FOLD_POLAR_8) {
            /* 45-degree fold: (fx + fy)/sqrt(2), |fx - fy|/sqrt(2) */
            int64_t u = ((fx + fy) * 46341LL) >> 16U;
            int64_t v = (q16_abs(fx - fy) * 46341LL) >> 16U;
            fx = u;
            fy = v;
        }
    }

    int64_t q_ring = q16_length2(fx, fy) - r->radii_q16[1];
    int64_t d_ring = q16_length2(q_ring, pz) - r->radii_q16[2];

    int64_t d = (r->fold_flags & FLOW_GENE_SMOOTH_UNION)
                    ? q16_smin(d_core, d_ring, r->radii_q16[3])
                    : q16_min(d_core, d_ring);

    /* Optional panel-line greeble etching */
    if (r->fold_flags & FLOW_GENE_GREEBLE_PANEL) {
        int64_t gx = q16_abs((x_q16 & 0x1FFFLL) - 0x1000LL);
        int64_t gy = q16_abs((y_q16 & 0x1FFFLL) - 0x1000LL);
        int64_t groove = q16_min(gx, gy);
        if (groove < 512)
            d += 384;
    }
    return d;
}

int flow_genome_render_atlas_ppm(const FlowGenomeRecipe64 batch[64], const char *ppm_path) {
    if (!batch || !ppm_path)
        return -1;
    enum { CELL_PX = 32, GRID = 8, IMG_W = CELL_PX * GRID, IMG_H = CELL_PX * GRID };
    static _Alignas(64) uint8_t s_atlas_rgb[(size_t)(IMG_W * IMG_H * 3)];
    uint8_t *rgb = s_atlas_rgb;
    memset(rgb, 0, sizeof(s_atlas_rgb));

    for (uint32_t idx = 0; idx < 64U; ++idx) {
        const FlowGenomeRecipe64 *rec = &batch[idx];
        uint32_t gx = idx % GRID;
        uint32_t gy = idx / GRID;
        uint8_t ar = (uint8_t)((rec->rgb_accent_hex >> 16U) & 0xFFU);
        uint8_t ag = (uint8_t)((rec->rgb_accent_hex >> 8U) & 0xFFU);
        uint8_t ab = (uint8_t)(rec->rgb_accent_hex & 0xFFU);

        for (int py = 0; py < CELL_PX; ++py) {
            for (int px = 0; px < CELL_PX; ++px) {
                int64_t u_q16 = ((int64_t)(px * 2 - CELL_PX) * FLOW_Q16_ONE) / CELL_PX;
                int64_t v_q16 = ((int64_t)(py * 2 - CELL_PX) * FLOW_Q16_ONE) / CELL_PX;
                int64_t t_q16 = 0;
                bool hit = false;
                for (int step = 0; step < 18; ++step) {
                    int64_t z_q16 = -98304 + t_q16;
                    int64_t d = flow_genome_eval_sdf_q16(rec, u_q16, v_q16, z_q16);
                    if (d < 1024) {
                        hit = true;
                        break;
                    }
                    t_q16 += d;
                    if (t_q16 > 196608)
                        break;
                }
                size_t off = (size_t)(((gy * CELL_PX + (uint32_t)py) * IMG_W +
                                       (gx * CELL_PX + (uint32_t)px)) *
                                      3U);
                if (px == 0 || py == 0) {
                    rgb[off + 0] = 24U;
                    rgb[off + 1] = 32U;
                    rgb[off + 2] = 48U;
                } else if (hit) {
                    int64_t z_hit = -98304 + t_q16;
                    int64_t dx = flow_genome_eval_sdf_q16(rec, u_q16 + 1024, v_q16, z_hit) -
                                 flow_genome_eval_sdf_q16(rec, u_q16 - 1024, v_q16, z_hit);
                    int64_t dy = flow_genome_eval_sdf_q16(rec, u_q16, v_q16 + 1024, z_hit) -
                                 flow_genome_eval_sdf_q16(rec, u_q16, v_q16 - 1024, z_hit);
                    int64_t shade = 140 + (int64_t)((dx - dy) >> 5U);
                    if (shade < 40)
                        shade = 40;
                    if (shade > 255)
                        shade = 255;
                    rgb[off + 0] = (uint8_t)((ar * (uint32_t)shade) >> 8U);
                    rgb[off + 1] = (uint8_t)((ag * (uint32_t)shade) >> 8U);
                    rgb[off + 2] = (uint8_t)((ab * (uint32_t)shade) >> 8U);
                } else {
                    rgb[off + 0] = 8U;
                    rgb[off + 1] = 10U;
                    rgb[off + 2] = 18U;
                }
            }
        }
    }

    FILE *fp = fopen(ppm_path, "wb");
    if (!fp) {
        return -1;
    }
    fprintf(fp, "P6\n%d %d\n255\n", IMG_W, IMG_H);
    fwrite(rgb, 1U, (size_t)(IMG_W * IMG_H * 3), fp);
    fclose(fp);
    return 0;
}

/* ============================================================================
 * 2. 2D Analytic SDF Vector HUD & SDD SVG Compile-Time Loader
 * ============================================================================ */

static int64_t sd_segment_q16(int64_t px, int64_t py, int64_t ax, int64_t ay, int64_t bx,
                              int64_t by) {
    int64_t pax = px - ax;
    int64_t pay = py - ay;
    int64_t bax = bx - ax;
    int64_t bay = by - ay;
    i128 dot_pa_ba = (i128)pax * (i128)bax + (i128)pay * (i128)bay;
    i128 dot_ba_ba = (i128)bax * (i128)bax + (i128)bay * (i128)bay;
    int64_t h_q16 = 0;
    if (dot_ba_ba > 0) {
        i128 h = (dot_pa_ba << 16U) / dot_ba_ba;
        if (h < 0)
            h = 0;
        if (h > FLOW_Q16_ONE)
            h = FLOW_Q16_ONE;
        h_q16 = (int64_t)h;
    }
    int64_t dx = pax - (int64_t)(((i128)bax * (i128)h_q16) >> 16U);
    int64_t dy = pay - (int64_t)(((i128)bay * (i128)h_q16) >> 16U);
    return q16_length2(dx, dy);
}

int64_t flow_glyph_eval_sdf_q16(const FlowGlyphDesc64 *g, int32_t u_q16, int32_t v_q16) {
    if (!g || g->stroke_count == 0U)
        return FLOW_Q16_ONE;
    uint32_t n = (g->stroke_count > FLOW_GLYPH_MAX_STROKES) ? FLOW_GLYPH_MAX_STROKES
                                                            : g->stroke_count;
    int64_t best = INT64_MAX;
    for (uint32_t i = 0; i < n; ++i) {
        int64_t ax = ((int64_t)g->segs[i].x0_q8) << 8U;
        int64_t ay = ((int64_t)g->segs[i].y0_q8) << 8U;
        int64_t bx = ((int64_t)g->segs[i].x1_q8) << 8U;
        int64_t by = ((int64_t)g->segs[i].y1_q8) << 8U;
        int64_t d = sd_segment_q16(u_q16, v_q16, ax, ay, bx, by) - g->stroke_radius_q16;
        if (d < best)
            best = d;
    }
    return best;
}

int32_t flow_glyph_match_ternary(const FlowGlyphDesc64 *a, const FlowGlyphDesc64 *b) {
    if (!a || !b)
        return 0;
    FlowBmfTernary64 ta = {~0ULL, ~a->ternary_pos_mask};
    FlowBmfTernary64 tb = {~0ULL, ~b->ternary_pos_mask};
    return flow_bmf_ternary64_dot(ta, tb);
}

static long parse_xml_int_attr(const char *line, const char *attr_name, long fallback) {
    char pat[64];
    snprintf(pat, sizeof(pat), "%s=\"", attr_name);
    const char *p = strstr(line, pat);
    if (!p)
        return fallback;
    p += strlen(pat);
    return strtol(p, NULL, 10);
}

size_t flow_svg_compile_file(const char *svg_path, FlowGlyphDesc64 *out_glyphs, size_t max_glyphs) {
    if (!svg_path || !out_glyphs || max_glyphs == 0U)
        return 0U;
    FILE *fp = fopen(svg_path, "r");
    if (!fp)
        return 0U;

    char line[512];
    size_t count = 0U;
    FlowGlyphDesc64 cur;
    bool in_group = false;

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, "<g ") && strstr(line, "data-glyph-id=")) {
            memset(&cur, 0, sizeof(cur));
            cur.glyph_id = (uint32_t)parse_xml_int_attr(line, "data-glyph-id", 100);
            cur.style_id = (uint32_t)parse_xml_int_attr(line, "data-style-id", 0);
            cur.stroke_radius_q16 = (int32_t)parse_xml_int_attr(line, "data-stroke-q16", 3276);
            in_group = true;
        } else if (in_group && strstr(line, "<line ")) {
            if (cur.stroke_count < FLOW_GLYPH_MAX_STROKES) {
                uint32_t k = cur.stroke_count++;
                cur.segs[k].x0_q8 = (int16_t)parse_xml_int_attr(line, "x1", 0);
                cur.segs[k].y0_q8 = (int16_t)parse_xml_int_attr(line, "y1", 0);
                cur.segs[k].x1_q8 = (int16_t)parse_xml_int_attr(line, "x2", 0);
                cur.segs[k].y1_q8 = (int16_t)parse_xml_int_attr(line, "y2", 0);
                uint64_t bit = (uint64_t)((cur.segs[k].x0_q8 ^ cur.segs[k].y1_q8 ^ (int16_t)k) & 63);
                cur.ternary_pos_mask |= (1ULL << bit);
            }
        } else if (in_group && strstr(line, "</g>")) {
            if (count < max_glyphs) {
                out_glyphs[count++] = cur;
            }
            in_group = false;
        }
    }
    fclose(fp);
    return count;
}

size_t flow_svg_compile_anim_clips(const char *svg_path, FlowAnimClip64 *out_clips, size_t max_clips) {
    if (!svg_path || !out_clips || max_clips == 0U)
        return 0U;
    FILE *fp = fopen(svg_path, "r");
    if (!fp)
        return 0U;

    char line[512];
    char tag_buf[2048];
    size_t tag_len = 0U;
    bool in_animate = false;
    size_t count = 0U;

    while (fgets(line, sizeof(line), fp)) {
        if (!in_animate && strstr(line, "<animate ") && strstr(line, "data-clip-id=")) {
            in_animate = true;
            tag_len = 0U;
            tag_buf[0] = '\0';
        }
        if (in_animate) {
            size_t l = strlen(line);
            if (tag_len + l + 1U < sizeof(tag_buf)) {
                memcpy(tag_buf + tag_len, line, l + 1U);
                tag_len += l;
            }
            if (strstr(line, "/>") || strstr(line, "</animate>")) {
                in_animate = false;
                if (count < max_clips) {
                    FlowAnimClip64 c;
                    memset(&c, 0, sizeof(c));
                    c.clip_id                = (uint32_t)parse_xml_int_attr(tag_buf, "data-clip-id", 100);
                    c.style_id               = (uint32_t)parse_xml_int_attr(tag_buf, "data-style-id", 0);
                    c.dur_anticipation_ms    = (uint16_t)parse_xml_int_attr(tag_buf, "data-anticipation-ms", 150);
                    c.dur_delivery_ms        = (uint16_t)parse_xml_int_attr(tag_buf, "data-delivery-ms", 120);
                    c.dur_impact_ms          = (uint16_t)parse_xml_int_attr(tag_buf, "data-impact-ms", 90);
                    c.dur_settle_ms          = (uint16_t)parse_xml_int_attr(tag_buf, "data-settle-ms", 300);
                    c.scale_anticipation_q16 = (int32_t)parse_xml_int_attr(tag_buf, "data-scale-anticipation-q16", 55705);
                    c.scale_impact_q16       = (int32_t)parse_xml_int_attr(tag_buf, "data-scale-impact-q16", 95027);
                    c.squash_gain_q16        = (int32_t)parse_xml_int_attr(tag_buf, "data-squash-gain-q16", 16384);
                    c.arc_height_q16         = (int32_t)parse_xml_int_attr(tag_buf, "data-arc-height-q16", 32768);
                    c.peak_flux_q16          = (int32_t)parse_xml_int_attr(tag_buf, "data-peak-flux-q16", 262144);
                    c.peak_ripple_phi_q16    = (int32_t)parse_xml_int_attr(tag_buf, "data-peak-ripple-q16", 8192);
                    c.trauma_shake_q16       = (int32_t)parse_xml_int_attr(tag_buf, "data-trauma-q16", 16384);
                    c.hitstop_frames         = (uint16_t)parse_xml_int_attr(tag_buf, "data-hitstop-frames", 2);
                    c.audio_patch_id         = (uint16_t)parse_xml_int_attr(tag_buf, "data-audio-patch", 1);
                    c.settle_overshoot_q16   = (int32_t)parse_xml_int_attr(tag_buf, "data-overshoot-q16", 8192);
                    c.checksum               = ((uint64_t)c.clip_id << 32U) ^
                                               (uint64_t)(c.dur_anticipation_ms + c.dur_delivery_ms +
                                                          c.dur_impact_ms + c.dur_settle_ms) ^
                                               (uint64_t)c.peak_flux_q16;
                    out_clips[count++] = c;
                }
            }
        }
    }
    fclose(fp);
    return count;
}

static inline int64_t q16_hermite_smooth(int64_t t_q16) {
    if (t_q16 <= 0) return 0;
    if (t_q16 >= FLOW_Q16_ONE) return FLOW_Q16_ONE;
    i128 t = (i128)t_q16;
    i128 t2 = (t * t) >> 16U;
    i128 factor = (3LL << 16U) - (2LL * t);
    return (int64_t)((t2 * factor) >> 16U);
}

static inline int64_t q16_lerp(int64_t a, int64_t b, int64_t w_q16) {
    return a + (int64_t)(((i128)(b - a) * (i128)w_q16) >> 16U);
}

void flow_anim_clip_eval_q16(const FlowAnimClip64 *clip, uint32_t elapsed_ms, FlowAnimSample64 *out_sample) {
    if (!out_sample)
        return;
    memset(out_sample, 0, sizeof(*out_sample));
    if (!clip)
        return;

    out_sample->clip_id = clip->clip_id;
    uint32_t t0 = clip->dur_anticipation_ms;
    uint32_t t1 = t0 + clip->dur_delivery_ms;
    uint32_t t2 = t1 + clip->dur_impact_ms;
    uint32_t t3 = t2 + clip->dur_settle_ms;

    if (elapsed_ms < t0 && clip->dur_anticipation_ms > 0U) {
        /* Phase 0: Anticipation (hover lift / crouch pullback + aura charge) */
        out_sample->phase = FLOW_ANIM_PHASE_ANTICIPATION;
        int64_t u_q16 = ((int64_t)elapsed_ms << 16U) / (int64_t)clip->dur_anticipation_ms;
        int64_t w_q16 = q16_hermite_smooth(u_q16);
        int64_t s_q16 = q16_lerp(FLOW_Q16_ONE, clip->scale_anticipation_q16, w_q16);
        out_sample->phase_progress_q16 = (int32_t)u_q16;
        out_sample->scale_parallel_q16 = (int32_t)s_q16;
        out_sample->scale_perp_q16     = (int32_t)s_q16;
        out_sample->flux_corona_q16    = (int32_t)((((i128)clip->peak_flux_q16 >> 2U) * w_q16) >> 16U);
    } else if (elapsed_ms < t1 && clip->dur_delivery_ms > 0U) {
        /* Phase 1: Delivery (parabolic arc + area-preserving Squash & Stretch: s_perp = 1 / s_parallel) */
        out_sample->phase = FLOW_ANIM_PHASE_DELIVERY;
        uint32_t dt_ms = elapsed_ms - t0;
        int64_t u_q16 = ((int64_t)dt_ms << 16U) / (int64_t)clip->dur_delivery_ms;
        int64_t w_q16 = q16_hermite_smooth(u_q16);
        /* Parabolic arc 4 * u * (1 - u) peaks at u = 0.5 */
        int64_t parabola_q16 = (int64_t)((4LL * (i128)u_q16 * (i128)(FLOW_Q16_ONE - u_q16)) >> 16U);
        int64_t stretch_q16  = FLOW_Q16_ONE + (int64_t)(((i128)clip->squash_gain_q16 * parabola_q16) >> 16U);
        int64_t perp_q16     = (stretch_q16 > 1024) ? (int64_t)((1LL << 32U) / stretch_q16) : FLOW_Q16_ONE;
        out_sample->phase_progress_q16 = (int32_t)u_q16;
        out_sample->scale_parallel_q16 = (int32_t)stretch_q16;
        out_sample->scale_perp_q16     = (int32_t)perp_q16;
        out_sample->offset_arc_y_q16   = (int32_t)(((i128)clip->arc_height_q16 * parabola_q16) >> 16U);
        out_sample->flux_corona_q16    = (int32_t)((((i128)clip->peak_flux_q16 >> 1U) * w_q16) >> 16U);
    } else if (elapsed_ms < t2 && clip->dur_impact_ms > 0U) {
        /* Phase 2: Impact Climax (Slam + Hitstop + Shockwave Ripple + 6D Camera Trauma + Audio) */
        out_sample->phase = FLOW_ANIM_PHASE_IMPACT;
        uint32_t dt_ms = elapsed_ms - t1;
        int64_t u_q16 = ((int64_t)dt_ms << 16U) / (int64_t)clip->dur_impact_ms;
        int64_t decay_q16 = FLOW_Q16_ONE - u_q16;
        /* Impact squashes wide perpendicular to motion and slams scale to scale_impact_q16 */
        out_sample->phase_progress_q16     = (int32_t)u_q16;
        out_sample->scale_parallel_q16     = clip->scale_impact_q16;
        out_sample->scale_perp_q16         = clip->scale_impact_q16 + (clip->squash_gain_q16 >> 1U);
        out_sample->flux_corona_q16        = (int32_t)(((i128)clip->peak_flux_q16 * decay_q16) >> 16U);
        out_sample->ripple_phi_q16         = (int32_t)(((i128)clip->peak_ripple_phi_q16 * decay_q16) >> 16U);
        out_sample->camera_trauma_q16      = (int32_t)(((i128)clip->trauma_shake_q16 * decay_q16) >> 16U);
        out_sample->hitstop_frames_trigger = (dt_ms < 20U) ? (uint32_t)clip->hitstop_frames : 0U;
        out_sample->audio_patch_trigger    = (dt_ms < 20U) ? (uint32_t)clip->audio_patch_id : 0U;
    } else if (elapsed_ms < t3 && clip->dur_settle_ms > 0U) {
        /* Phase 3: Follow-Through & Underdamped Spring Settle back to 1.0x */
        out_sample->phase = FLOW_ANIM_PHASE_SETTLE;
        uint32_t dt_ms = elapsed_ms - t2;
        int64_t u_q16 = ((int64_t)dt_ms << 16U) / (int64_t)clip->dur_settle_ms;
        int64_t rem_q16 = FLOW_Q16_ONE - u_q16;
        int64_t env_q16 = (int64_t)(((i128)rem_q16 * (i128)rem_q16) >> 16U);
        /* Spring oscillation: undershoots below 1.0x around mid-settle then decays to 1.0x */
        int64_t spring_dip = (int64_t)((4LL * (i128)u_q16 * (i128)rem_q16) >> 16U);
        int64_t scale_q16  = FLOW_Q16_ONE
                           + (int64_t)(((i128)(clip->scale_impact_q16 - FLOW_Q16_ONE) * env_q16) >> 16U)
                           - (int64_t)(((i128)clip->settle_overshoot_q16 * spring_dip) >> 16U);
        out_sample->phase_progress_q16 = (int32_t)u_q16;
        out_sample->scale_parallel_q16 = (int32_t)scale_q16;
        out_sample->scale_perp_q16     = (int32_t)scale_q16;
        out_sample->ripple_phi_q16     = (int32_t)((((i128)clip->peak_ripple_phi_q16 >> 2U) * env_q16) >> 16U);
    } else {
        out_sample->phase              = FLOW_ANIM_PHASE_DONE;
        out_sample->phase_progress_q16 = FLOW_Q16_ONE;
        out_sample->scale_parallel_q16 = FLOW_Q16_ONE;
        out_sample->scale_perp_q16     = FLOW_Q16_ONE;
    }
}

/* ============================================================================
 * 3. Keyframe-less 2-Jet Procedural Mechanism & Hysteresis
 * ============================================================================ */

uint64_t flow_kinetic_rig_step(FlowKineticRig64 *rig, int64_t dist_q16, int64_t dt_q16) {
    if (!rig)
        return 0ULL;

    /* Dual-threshold distance hysteresis: deploy when dist <= enter, retract when dist >= exit */
    if (rig->deployed_state == 0U && dist_q16 <= rig->deploy_enter_dist_q16) {
        rig->deployed_state = 1U;
    } else if (rig->deployed_state != 0U && dist_q16 >= rig->deploy_exit_dist_q16) {
        rig->deployed_state = 0U;
    }

    int64_t desired_omega = (rig->deployed_state != 0U) ? rig->target_omega_q16 : 0;
    FlowJet2 prev = rig->angle_jet;
    int64_t next_v = flow_jet2_eval_taylor(prev, dt_q16);
    int64_t next_d1 = prev.d1 + (int64_t)(((i128)prev.d2 * (i128)dt_q16) >> 16U);
    FlowJet2 next = flow_jet2_make(next_v, next_d1, prev.d2);

    uint64_t sieve = FLOW_ASM_SIEVE_ALL_SOUND;
    (void)flow_asm_jet2_self_check_step(prev, next, dt_q16, 4096, 4096, &sieve);

    /* Smoothly steer angular velocity toward desired_omega after Taylor verification */
    next.d1 = (next.d1 * 3 + desired_omega) / 4;
    rig->angle_jet = next;
    rig->sieve_status = sieve;
    return sieve;
}

/* ============================================================================
 * 4. Zero-WAV Pure Q16 2-Jet FM & Chirp Acoustic Synthesizer
 * ============================================================================ */

static inline int64_t q16_tri_wave(int64_t phase_q16) {
    /* Map cyclic Q16 phase [0, 65535] to symmetric triangle wave in [-65536, 65536] */
    int64_t p = phase_q16 & 0xFFFFLL;
    int64_t half = (p < 32768LL) ? p : (65536LL - p);
    return (half * 4LL) - 65536LL;
}

static inline int64_t q16_smooth_sine(int64_t phase_q16) {
    /* Pure Q16 C1 continuous parabolic sine approximation in [-65536, 65536] (0 Float / 0 Aliasing) */
    uint32_t p = (uint32_t)(phase_q16 & 0xFFFFLL);
    if (p < 32768U) {
        uint32_t u = p;
        return (int64_t)(((uint64_t)u * (32768U - u)) >> 12U);
    } else {
        uint32_t u = p - 32768U;
        return -(int64_t)(((uint64_t)u * (32768U - u)) >> 12U);
    }
}

void flow_audio_synth_samples_q16(const FlowAudioPatch64 *patch, int64_t *out_pcm_q16,
                                  size_t sample_count, int64_t dt_sample_q16) {
    if (!patch || !out_pcm_q16 || sample_count == 0U)
        return;

    int64_t env_q16 = 0;
    for (size_t i = 0; i < sample_count; ++i) {
        int64_t t_q16 = (int64_t)i * dt_sample_q16;
        int64_t carrier_phase = flow_jet2_eval_taylor(patch->carrier_jet, t_q16);
        int64_t mod_phase = (int64_t)(((i128)patch->mod_freq_q16 * (i128)t_q16) >> 16U);
        int64_t mod_val = q16_tri_wave(mod_phase);
        int64_t fm_offset = (int64_t)(((i128)mod_val * (i128)patch->fm_index_q16) >> 16U);

        int64_t raw_wave = q16_tri_wave(carrier_phase + fm_offset);
        if (i < sample_count / 4U) {
            env_q16 = q16_min(FLOW_Q16_ONE, env_q16 + patch->attack_step_q16);
        } else {
            env_q16 = q16_max(4096, env_q16 - patch->decay_step_q16);
        }
        out_pcm_q16[i] = (int64_t)(((i128)raw_wave * (i128)env_q16) >> 16U);
    }
}

/* ============================================================================
 * 5. White-Box 4-Phase Combo Cancel Gate & 2-Jet Momentum Spline
 * ============================================================================ */

void flow_combo_chain_init(FlowComboChain64 *chain,
                           const FlowAnimClip64 *initial_clip,
                           uint32_t now_ms,
                           uint32_t cancel_gate_mask,
                           int64_t initial_vz_q16,
                           int64_t initial_az_q16) {
    if (!chain)
        return;
    memset(chain, 0, sizeof(*chain));
    chain->traj_z                = flow_jet2_make(0LL, initial_vz_q16, initial_az_q16);
    chain->active_clip_id        = initial_clip ? initial_clip->clip_id : 0U;
    chain->anchor_ms             = now_ms;
    chain->proper_time_ratio_q16 = (int32_t)FLOW_Q16_ONE;
    chain->cancel_gate_mask      = cancel_gate_mask;
    chain->combo_counter         = 1U;
    chain->momentum_inherit_q16  = (int32_t)FLOW_Q16_ONE; /* 100% momentum inheritance */
    chain->sieve_status          = FLOW_ASM_SIEVE_ALL_SOUND;
    chain->last_transition_type  = FLOW_COMBO_TRANSITION_NONE;
}

static uint32_t calc_local_proper_elapsed_ms(const FlowComboChain64 *chain, uint32_t now_ms) {
    if (!chain || now_ms <= chain->anchor_ms)
        return 0U;
    uint32_t wall_dt_ms = now_ms - chain->anchor_ms;
    int64_t tau_q16 = (chain->proper_time_ratio_q16 > 0) ? (int64_t)chain->proper_time_ratio_q16 : FLOW_Q16_ONE;
    return (uint32_t)(((i128)wall_dt_ms * (i128)tau_q16) >> 16U);
}

void flow_combo_chain_eval_q16(const FlowComboChain64 *chain,
                               const FlowAnimClip64 *active_clip,
                               uint32_t now_ms,
                               FlowAnimSample64 *out_anim_sample,
                               FlowJet2 *out_instant_jet_z) {
    if (!chain)
        return;

    uint32_t local_ms = calc_local_proper_elapsed_ms(chain, now_ms);
    if (out_anim_sample) {
        flow_anim_clip_eval_q16(active_clip, local_ms, out_anim_sample);
    }

    if (out_instant_jet_z) {
        int64_t dt_q16 = ((int64_t)local_ms << 16U) / 1000LL;
        int64_t z_now  = flow_jet2_eval_taylor(chain->traj_z, dt_q16);
        int64_t vz_now = chain->traj_z.d1 + (int64_t)(((i128)chain->traj_z.d2 * (i128)dt_q16) >> 16U);
        *out_instant_jet_z = flow_jet2_make(z_now, vz_now, chain->traj_z.d2);
    }
}

bool flow_combo_chain_try_cancel(FlowComboChain64 *chain,
                                 const FlowAnimClip64 *current_clip,
                                 const FlowAnimClip64 *next_clip,
                                 uint32_t now_ms,
                                 bool hit_confirmed,
                                 int64_t flash_dz_q16,
                                 int64_t next_dv_q16,
                                 int64_t next_da_q16) {
    if (!chain || !current_clip)
        return false;

    FlowAnimSample64 cur_sample;
    FlowJet2 instant_jet;
    flow_combo_chain_eval_q16(chain, current_clip, now_ms, &cur_sample, &instant_jet);

    uint32_t local_ms = calc_local_proper_elapsed_ms(chain, now_ms);
    int64_t dt_q16 = ((int64_t)local_ms << 16U) / 1000LL;

    /* Verify 2-Jet Taylor continuity up to the cancel point via hardware self-check */
    uint64_t sieve = chain->sieve_status;
    (void)flow_asm_jet2_self_check_step(chain->traj_z, instant_jet, dt_q16, 4096LL, 4096LL, &sieve);
    chain->sieve_status = sieve;

    /*
     * 1. Phase 0 (ANTICIPATION): Strictly locked startup frames (no zero-frame instant cancel exploits)
     */
    if (cur_sample.phase == FLOW_ANIM_PHASE_ANTICIPATION) {
        chain->last_transition_type = FLOW_COMBO_TRANSITION_NONE;
        return false;
    }

    /*
     * 2. Phase 1 (DELIVERY): Only allows Q-Flash / E-Q-Flash trajectory injection WITHOUT
     *    resetting the current animation clip's delivery -> impact countdown!
     */
    if (cur_sample.phase == FLOW_ANIM_PHASE_DELIVERY) {
        if (flash_dz_q16 != 0LL && (chain->cancel_gate_mask & FLOW_CANCEL_GATE_DELIVERY_FLASH)) {
            /* Adjust anchor z0 so that at current local_ms, z(dt) jumps by flash_dz_q16 */
            chain->traj_z.v  += flash_dz_q16;
            chain->traj_z.d1 += next_dv_q16;
            chain->last_transition_type = FLOW_COMBO_TRANSITION_FLASH_INJECT;
            return true;
        }
        chain->last_transition_type = FLOW_COMBO_TRANSITION_NONE;
        return false;
    }

    /*
     * 3. Phase 2 (IMPACT): Hit-Confirm Cancel (Roman Cancel)
     *    Requires FLOW_CANCEL_GATE_HIT_CONFIRM AND hit_confirmed == true!
     *    Whiffing (hit_confirmed == false) locks out cancel and forces Phase 3 recovery.
     */
    if (cur_sample.phase == FLOW_ANIM_PHASE_IMPACT) {
        if (!next_clip || !hit_confirmed || !(chain->cancel_gate_mask & FLOW_CANCEL_GATE_HIT_CONFIRM)) {
            chain->last_transition_type = FLOW_COMBO_TRANSITION_NONE;
            return false;
        }
        int64_t carried_v = (int64_t)(((i128)instant_jet.d1 * (i128)chain->momentum_inherit_q16) >> 16U);
        chain->traj_z = flow_jet2_make(instant_jet.v + flash_dz_q16, carried_v + next_dv_q16, next_da_q16);
        chain->active_clip_id = next_clip->clip_id;
        chain->anchor_ms = now_ms;
        chain->combo_counter++;
        chain->last_transition_type = FLOW_COMBO_TRANSITION_HIT_CONFIRM;
        return true;
    }

    /*
     * 4. Phase 3 (SETTLE) or Phase 4 (DONE): Fast-QA Recovery Cancel
     *    Truncates remaining Phase 3 backswing, inherits instantaneous momentum, and re-anchors t0' = now_ms!
     */
    if (!next_clip) {
        chain->last_transition_type = FLOW_COMBO_TRANSITION_NONE;
        return false;
    }
    if (cur_sample.phase == FLOW_ANIM_PHASE_SETTLE && !(chain->cancel_gate_mask & FLOW_CANCEL_GATE_SETTLE)) {
        chain->last_transition_type = FLOW_COMBO_TRANSITION_NONE;
        return false;
    }

    int64_t carried_v = (int64_t)(((i128)instant_jet.d1 * (i128)chain->momentum_inherit_q16) >> 16U);
    chain->traj_z = flow_jet2_make(instant_jet.v + flash_dz_q16, carried_v + next_dv_q16, next_da_q16);
    chain->active_clip_id = next_clip->clip_id;
    chain->anchor_ms = now_ms;
    chain->combo_counter++;
    chain->last_transition_type = (cur_sample.phase == FLOW_ANIM_PHASE_SETTLE)
                                ? FLOW_COMBO_TRANSITION_RECOVERY_CUT
                                : FLOW_COMBO_TRANSITION_NATURAL_CHAIN;
    return true;
}

/* ============================================================================
 * 6. 64B Ternary HashMap Narrative Engine & 1-Cycle POPCNT Matcher
 * ============================================================================ */

int32_t flow_dialog_match_score_o1(const FlowDialogNode64 *node, uint64_t event_key_hash, uint64_t world_facts) {
    if (!node || node->dialog_id == 0U) {
        return -1;
    }
    if (node->event_key_hash != event_key_hash) {
        return -1;
    }
    /* Fast 1-cycle bitwise mismatch check */
    if (((world_facts ^ node->req_values) & node->req_mask) != 0ULL) {
        return -1;
    }
    /* Hardware BMF ternary dot-product verification */
    FlowBmfTernary64 t_node = { node->req_mask, (~node->req_values) & node->req_mask };
    FlowBmfTernary64 t_facts = { ~0ULL, ~world_facts };
    int32_t dot = flow_bmf_ternary64_dot(t_node, t_facts);
    int32_t spec = (int32_t)__builtin_popcountll(node->req_mask);
    if (dot != spec) {
        return -1;
    }
    return spec * 16 + (int32_t)node->priority_bias;
}

const FlowDialogNode64 *flow_dialog_hashmap_select(const FlowDialogNode64 *table,
                                                   size_t table_pow2_size,
                                                   uint64_t event_key_hash,
                                                   uint64_t *inout_world_facts) {
    if (!table || table_pow2_size == 0U) {
        return NULL;
    }
    uint64_t facts = inout_world_facts ? *inout_world_facts : 0ULL;
    const FlowDialogNode64 *best_node = NULL;
    int32_t best_score = -1;

    size_t mask = table_pow2_size - 1U;
    size_t start_idx = (size_t)(event_key_hash & mask);

    for (size_t probe = 0; probe < table_pow2_size; ++probe) {
        size_t idx = (start_idx + probe) & mask;
        const FlowDialogNode64 *candidate = &table[idx];
        if (candidate->dialog_id == 0U) {
            break; /* Empty slot terminates cluster */
        }
        if (candidate->event_key_hash == event_key_hash) {
            int32_t score = flow_dialog_match_score_o1(candidate, event_key_hash, facts);
            if (score > best_score) {
                best_score = score;
                best_node = candidate;
            }
        }
    }

    if (best_node && best_score >= 0) {
        if (inout_world_facts) {
            *inout_world_facts = (*inout_world_facts & ~best_node->set_mask) |
                                 (best_node->set_values & best_node->set_mask);
        }
        return best_node;
    }
    return NULL;
}

bool flow_dialog_hashmap_insert(FlowDialogNode64 *table, size_t table_pow2_size, FlowDialogNode64 node) {
    if (!table || table_pow2_size == 0U || node.dialog_id == 0U) {
        return false;
    }
    size_t mask = table_pow2_size - 1U;
    size_t start_idx = (size_t)(node.event_key_hash & mask);
    for (size_t probe = 0; probe < table_pow2_size; ++probe) {
        size_t idx = (start_idx + probe) & mask;
        if (table[idx].dialog_id == 0U) {
            table[idx] = node;
            return true;
        }
    }
    return false;
}

/* ============================================================================
 * 7. 64B Ricardian Self-Balancing Item & Combat Simulation
 * ============================================================================ */

FlowItemStat64 flow_item_craft_ricardian(uint32_t item_id,
                                        uint32_t assembly_index,
                                        uint32_t grade,
                                        int32_t atk_weight,
                                        int32_t cdr_weight,
                                        int32_t mit_weight) {
    FlowItemStat64 it;
    memset(&it, 0, sizeof(it));
    it.item_id = item_id;
    it.assembly_index = (assembly_index < 1U) ? 1U : assembly_index;
    it.ricardian_grade = (grade > 4U) ? 4U : grade;

    int64_t wa = (atk_weight > 0) ? (int64_t)atk_weight : 0LL;
    int64_t wc = (cdr_weight > 0) ? (int64_t)cdr_weight : 0LL;
    int64_t wm = (mit_weight > 0) ? (int64_t)mit_weight : 0LL;
    int64_t wsum = wa + wc + wm;
    if (wsum == 0LL) {
        wa = 1LL; wc = 1LL; wm = 1LL; wsum = 3LL;
    }

    uint32_t g_mult = 5U - it.ricardian_grade;
    int64_t budget = ((int64_t)it.assembly_index * 2000LL + (int64_t)g_mult * 1000LL) * (int64_t)FLOW_Q16_ONE / 10LL;

    it.raw_attack_flux_q16 = (int32_t)((budget * wa) / wsum);
    it.raw_haste_cdr_q16   = (int32_t)((budget * wc) / wsum);
    it.raw_shield_mit_q16  = (int32_t)((budget * wm) / wsum);

    /* AMM diminishing returns for single item */
    uint64_t next_atk = ricardian_amm_after_swap(65536ULL, 65536ULL,
                                                 (uint64_t)(it.raw_attack_flux_q16 > 0 ? it.raw_attack_flux_q16 : 0), 0ULL);
    it.eff_attack_q16 = (int32_t)(65536ULL - next_atk);

    uint64_t next_cdr = ricardian_amm_after_swap(65536ULL, 49152ULL,
                                                 (uint64_t)(it.raw_haste_cdr_q16 > 0 ? it.raw_haste_cdr_q16 : 0), 0ULL);
    int32_t c = (int32_t)(49152ULL - next_cdr);
    it.eff_cdr_q16 = (c > 49152) ? 49152 : ((c < 0) ? 0 : c);

    uint64_t next_mit = ricardian_amm_after_swap(65536ULL, 52428ULL,
                                                 (uint64_t)(it.raw_shield_mit_q16 > 0 ? it.raw_shield_mit_q16 : 0), 0ULL);
    int32_t m = (int32_t)(52428ULL - next_mit);
    it.eff_mitigation_q16 = (m > 52428) ? 52428 : ((m < 0) ? 0 : m);

    /* Joule thermal dissipation per activation */
    it.thermal_cost_kelvin = 10 + (int32_t)(((int64_t)it.eff_attack_q16 * 20LL + (int64_t)it.eff_cdr_q16 * 25LL) / 65536LL);

    uint64_t valuation = ((uint64_t)it.assembly_index * 10000ULL) + ((uint64_t)g_mult * 5000ULL);
    it.harberger_upkeep = ricardian_harberger_tax(valuation, 500ULL);

    it.flags = 0U;
    it.synergy_mask = (1U << it.ricardian_grade) |
                      ((wa > 0) ? 0x10U : 0U) |
                      ((wc > 0) ? 0x20U : 0U) |
                      ((wm > 0) ? 0x40U : 0U);

    it.ssot_hash = ((uint64_t)it.item_id << 32U) ^
                   ((uint64_t)it.assembly_index << 24U) ^
                   ((uint64_t)it.ricardian_grade << 16U) ^
                   (uint64_t)(uint32_t)it.eff_attack_q16 ^
                   ((uint64_t)(uint32_t)it.eff_cdr_q16 << 32U) ^
                   (uint64_t)(uint32_t)it.eff_mitigation_q16 ^
                   0xA5A5A5A5A5A5A5A5ULL;

    return it;
}

FlowItemStat64 flow_item_combine_build(const FlowItemStat64 *items, size_t count) {
    FlowItemStat64 out;
    memset(&out, 0, sizeof(out));
    if (!items || count == 0U) {
        out.assembly_index = 1U;
        out.ricardian_grade = 4U;
        out.thermal_cost_kelvin = 10;
        out.ssot_hash = 0x5555555555555555ULL;
        return out;
    }

    int64_t tot_atk = 0;
    int64_t tot_cdr = 0;
    int64_t tot_mit = 0;
    uint64_t tot_upkeep = 0;
    uint32_t best_grade = 4U;
    uint64_t asm_depth = items[0].assembly_index;
    uint32_t combined_flags = 0U;
    uint32_t combined_synergy = 0U;

    for (size_t i = 0; i < count; ++i) {
        tot_atk += (int64_t)items[i].raw_attack_flux_q16;
        tot_cdr += (int64_t)items[i].raw_haste_cdr_q16;
        tot_mit += (int64_t)items[i].raw_shield_mit_q16;
        tot_upkeep += items[i].harberger_upkeep;
        if (items[i].ricardian_grade < best_grade) {
            best_grade = items[i].ricardian_grade;
        }
        if (i > 0) {
            asm_depth = ricardian_join_assembly(asm_depth, (uint64_t)items[i].assembly_index,
                                                (uint64_t)items[i].assembly_index / 4ULL);
        }
        combined_flags |= items[i].flags;
        combined_synergy |= items[i].synergy_mask;
    }

    out.item_id = (uint32_t)(count & 0xFFFFFFFFU);
    out.assembly_index = (uint32_t)asm_depth;
    out.ricardian_grade = best_grade;
    out.raw_attack_flux_q16 = (int32_t)(tot_atk > 0x7FFFFFFFLL ? 0x7FFFFFFFLL : tot_atk);
    out.raw_haste_cdr_q16   = (int32_t)(tot_cdr > 0x7FFFFFFFLL ? 0x7FFFFFFFLL : tot_cdr);
    out.raw_shield_mit_q16  = (int32_t)(tot_mit > 0x7FFFFFFFLL ? 0x7FFFFFFFLL : tot_mit);
    out.harberger_upkeep    = tot_upkeep;
    out.flags               = combined_flags;
    out.synergy_mask        = combined_synergy;

    /* Assembly conservation and AMM hyperbolic saturation */
    uint64_t next_atk = ricardian_amm_after_swap(65536ULL * 2ULL, 65536ULL,
                                                 (uint64_t)(out.raw_attack_flux_q16 > 0 ? out.raw_attack_flux_q16 : 0), 0ULL);
    out.eff_attack_q16 = (int32_t)(65536ULL - next_atk);

    uint64_t next_cdr = ricardian_amm_after_swap(65536ULL, 49152ULL,
                                                 (uint64_t)(out.raw_haste_cdr_q16 > 0 ? out.raw_haste_cdr_q16 : 0), 0ULL);
    int32_t c = (int32_t)(49152ULL - next_cdr);
    out.eff_cdr_q16 = (c > 49152) ? 49152 : ((c < 0) ? 0 : c);

    uint64_t next_mit = ricardian_amm_after_swap(65536ULL, 52428ULL,
                                                 (uint64_t)(out.raw_shield_mit_q16 > 0 ? out.raw_shield_mit_q16 : 0), 0ULL);
    int32_t m = (int32_t)(52428ULL - next_mit);
    out.eff_mitigation_q16 = (m > 52428) ? 52428 : ((m < 0) ? 0 : m);

    out.thermal_cost_kelvin = 10 + (int32_t)(((int64_t)out.eff_attack_q16 * 22LL + (int64_t)out.eff_cdr_q16 * 26LL) / 65536LL);

    out.ssot_hash = ((uint64_t)out.item_id << 32U) ^
                    ((uint64_t)out.assembly_index << 24U) ^
                    ((uint64_t)out.ricardian_grade << 16U) ^
                    (uint64_t)(uint32_t)out.eff_attack_q16 ^
                    ((uint64_t)(uint32_t)out.eff_cdr_q16 << 32U) ^
                    (uint64_t)(uint32_t)out.eff_mitigation_q16 ^
                    0x5A5A5A5A5A5A5A5AULL;

    return out;
}

int32_t flow_item_simulate_ttk_ms(const FlowItemStat64 *attacker_build, const FlowItemStat64 *defender_build) {
    int64_t hp = 18000LL;
    int32_t eff_atk = attacker_build ? attacker_build->eff_attack_q16 : 0;
    int32_t eff_cdr = attacker_build ? attacker_build->eff_cdr_q16 : 0;
    if (eff_cdr < 0) eff_cdr = 0;
    if (eff_cdr > 49152) eff_cdr = 49152;

    int32_t eff_mit = defender_build ? defender_build->eff_mitigation_q16 : 0;
    if (eff_mit < 0) eff_mit = 0;
    if (eff_mit > 52428) eff_mit = 52428;

    int32_t heat_cost = attacker_build ? attacker_build->thermal_cost_kelvin : 10;
    if (heat_cost < 8) heat_cost = 8;

    /* 1. Kinetic damage mitigated by eff_mitigation */
    int64_t kinetic_base = 700LL + ((int64_t)eff_atk * 700LL) / 65536LL;
    int64_t pen_q16 = 65536LL - (int64_t)eff_mit;
    int64_t kinetic_dmg = (kinetic_base * pen_q16) / 65536LL;

    /* 2. Thermodynamic flux direct penetration */
    int64_t flux_dmg = 450LL + ((int64_t)eff_atk * 450LL) / 65536LL;
    int64_t dmg = kinetic_dmg + flux_dmg;
    if (dmg < 200LL) dmg = 200LL;

    /* Base cooldown 700ms modulated by CDR */
    int64_t base_cd_ms = 700LL;
    int64_t cd_ms = (base_cd_ms * (65536LL - (int64_t)eff_cdr)) / 65536LL;
    if (cd_ms < 175LL) cd_ms = 175LL;

    int64_t now_ms = 0LL;
    int64_t temp_kelvin = 700LL;
    bool in_shutdown = false;

    while (hp > 0LL && now_ms <= 30000LL) {
        if (in_shutdown) {
            /* Cool down from temp_kelvin to 760K at 40 K/s (1K per 25ms) */
            int64_t drop_needed = temp_kelvin - 760LL;
            if (drop_needed <= 0LL) {
                in_shutdown = false;
                temp_kelvin = 760LL;
            } else {
                int64_t cool_ms = drop_needed * 25LL;
                now_ms += cool_ms;
                temp_kelvin = 760LL;
                in_shutdown = false;
            }
        }

        /* Attack lands */
        hp -= dmg;
        if (hp <= 0LL) {
            return (int32_t)now_ms;
        }

        /* Joule heating */
        temp_kelvin += (int64_t)heat_cost;
        if (temp_kelvin >= 800LL) {
            in_shutdown = true;
        }

        /* Advance to next attack */
        now_ms += cd_ms;
        /* Passive cooling during cooldown (40 K/s = 1K per 25ms) */
        int64_t passive_cool = cd_ms / 25LL;
        temp_kelvin = (temp_kelvin - passive_cool < 700LL) ? 700LL : (temp_kelvin - passive_cool);
        if (in_shutdown && temp_kelvin <= 760LL) {
            in_shutdown = false;
        }
    }
    return (int32_t)now_ms;
}

/* ============================================================================
 * 8. 64B Tri-Modal Audiovisual Coupling & SDF Cavity Relativistic Synth
 * ============================================================================ */

void flow_av_resolve_frame64(int32_t base_carrier_hz_q16,
                             int32_t doppler_factor_q16,
                             int32_t proper_time_ratio_q16,
                             int64_t sdf_dist_q16,
                             uint32_t prev_cavity_latched,
                             const FlowAnimSample64 *anim_sample,
                             uint32_t finish_class,
                             int32_t finish_iridescence_q16,
                             int32_t finish_gold_vein_q16,
                             int32_t raw_strobe_hz_q16,
                             int32_t shared_jet_phase_q16,
                             FlowAudiovisualFrame64 *out_av) {
    if (!out_av)
        return;
    memset(out_av, 0, sizeof(*out_av));

    int64_t base_hz = (base_carrier_hz_q16 > 0) ? (int64_t)base_carrier_hz_q16 : (220LL << 16U);
    int64_t dopp = (doppler_factor_q16 > 4096) ? (int64_t)doppler_factor_q16 : FLOW_Q16_ONE;
    int64_t tau  = (proper_time_ratio_q16 > 2048) ? (int64_t)proper_time_ratio_q16 : FLOW_Q16_ONE;

    /* 1. Relativistic Doppler & Proper-Time Frequency Coupling */
    int64_t shifted_hz = (int64_t)(((i128)base_hz * (i128)dopp) >> 16U);
    shifted_hz = (int64_t)(((i128)shifted_hz * (i128)tau) >> 16U);
    if (shifted_hz < (20LL << 16U)) shifted_hz = (20LL << 16U);
    if (shifted_hz > (4000LL << 16U)) shifted_hz = (4000LL << 16U);
    out_av->doppler_carrier_freq_q16 = (int32_t)shifted_hz;

    /* 2. Dual-Threshold SDF Cavity Helmholtz Resonance Hysteresis via FLOW .lean ABI [12m, 20m] */
    bool next_latched = flow_lean_abi_eval_hysteresis_lo(
        sdf_dist_q16,
        (int64_t)FLOW_CAVITY_ENTER_DIST_Q16,
        (int64_t)FLOW_CAVITY_EXIT_DIST_Q16,
        prev_cavity_latched != 0U
    );
    uint32_t latched = next_latched ? 1U : 0U;
    out_av->cavity_latched = latched;

    if (latched != 0U) {
        int64_t depth_into_band = FLOW_CAVITY_EXIT_DIST_Q16 - sdf_dist_q16;
        if (depth_into_band < 0LL) depth_into_band = 0LL;
        int64_t res = (depth_into_band * FLOW_Q16_ONE) / FLOW_CAVITY_EXIT_DIST_Q16;
        if (res < 16384LL) res = 16384LL;
        if (res > FLOW_Q16_ONE) res = FLOW_Q16_ONE;
        out_av->cavity_resonance_q16    = (int32_t)res;
        out_av->cavity_reverb_decay_q16 = (int32_t)((res * 49152LL) >> 16U);
    } else {
        out_av->cavity_resonance_q16    = 0;
        out_av->cavity_reverb_decay_q16 = 0;
    }

    /* 3. Area-Preserving Squash & Stretch + Impact Shockwave Coupling */
    if (anim_sample && anim_sample->scale_parallel_q16 > 4096) {
        int64_t spar = (int64_t)anim_sample->scale_parallel_q16;
        int64_t sperp = (int64_t)((1LL << 32U) / spar);
        out_av->squash_parallel_q16  = (int32_t)spar;
        out_av->squash_perp_q16      = (int32_t)sperp;
        out_av->shockwave_ripple_q16 = anim_sample->ripple_phi_q16;
        out_av->active_audio_patch_id = anim_sample->audio_patch_trigger;
    } else {
        out_av->squash_parallel_q16  = (int32_t)FLOW_Q16_ONE;
        out_av->squash_perp_q16      = (int32_t)FLOW_Q16_ONE;
        out_av->shockwave_ripple_q16 = 0;
        out_av->active_audio_patch_id = 1U;
    }

    /* 4. Live Haute-Couture Finish Parameters */
    out_av->active_finish_class    = finish_class & 3U;
    out_av->finish_iridescence_q16 = (finish_iridescence_q16 > 0) ? finish_iridescence_q16 : 45875;
    out_av->finish_gold_vein_q16   = (finish_gold_vein_q16 > 0) ? finish_gold_vein_q16 : 32768;

    /* 5. TRC <= 3.0 Hz Photosensitive Epilepsy Flash Guard via FLOW .lean ABI */
    out_av->trc_safe_strobe_hz_q16 = (int32_t)flow_lean_abi_eval_clamp(
        (int64_t)raw_strobe_hz_q16,
        0LL,
        3LL * FLOW_Q16_ONE
    );

    /* 6. Zero-Drift Tri-Modal (Optical-Acoustic-Haptic) Phase Lock */
    out_av->trimodal_phase_q16      = shared_jet_phase_q16 & 0xFFFF;
    out_av->haptic_resonance_hz_q16 = (int32_t)((shifted_hz >> 2U) + ((int64_t)out_av->cavity_resonance_q16 << 5U));

    out_av->checksum = ((uint64_t)(uint32_t)out_av->doppler_carrier_freq_q16 << 32U) ^
                       ((uint64_t)(uint32_t)out_av->cavity_resonance_q16 << 16U) ^
                       ((uint64_t)out_av->cavity_latched << 8U) ^
                       (uint64_t)out_av->active_finish_class ^
                       ((uint64_t)(uint32_t)out_av->trc_safe_strobe_hz_q16 << 24U) ^
                       0xA710CA7105AFEB00ULL;
}

void flow_audio_synth_relativistic_q16(const FlowAudioPatch64 *patch,
                                       const FlowAudiovisualFrame64 *av,
                                       int64_t *out_pcm_q16,
                                       size_t sample_count,
                                       int64_t dt_sample_q16) {
    if (!patch || !out_pcm_q16 || sample_count == 0U)
        return;

    int64_t base_freq = (patch->carrier_jet.d1 > 0LL) ? patch->carrier_jet.d1 : (220LL << 16U);
    int64_t eff_freq  = (av && av->doppler_carrier_freq_q16 > 0)
        ? (int64_t)av->doppler_carrier_freq_q16
        : base_freq;
    int64_t freq_ratio_q16 = (int64_t)(((i128)eff_freq << 16U) / (i128)base_freq);
    int64_t eff_dt_q16 = (int64_t)(((i128)dt_sample_q16 * (i128)freq_ratio_q16) >> 16U);
    if (eff_dt_q16 < 1LL) eff_dt_q16 = 1LL;

    int64_t cav_res_q16   = av ? (int64_t)av->cavity_resonance_q16 : 0LL;
    int64_t cav_decay_q16 = av ? (int64_t)av->cavity_reverb_decay_q16 : 0LL;
    int64_t phase_lock    = av ? (int64_t)av->trimodal_phase_q16 : 0LL;

    /* Boost FM index when inside an SDF Helmholtz cavity for metallic overtone richness */
    int64_t eff_fm_idx = patch->fm_index_q16 + ((cav_res_q16 * 24576LL) >> 16U);

    /* Stack-resident 8-tap comb delay line for zero-heap SDF cavity early reflections */
    int64_t comb_ring[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    int64_t env_q16 = 0LL;

    for (size_t i = 0U; i < sample_count; ++i) {
        int64_t t_q16 = (int64_t)i * eff_dt_q16;
        int64_t carrier_phase = phase_lock + flow_jet2_eval_taylor(patch->carrier_jet, t_q16);
        int64_t mod_phase = phase_lock + (int64_t)(((i128)patch->mod_freq_q16 * (i128)t_q16) >> 16U);
        int64_t mod_val = q16_smooth_sine(mod_phase);
        int64_t fm_offset = (int64_t)(((i128)mod_val * (i128)eff_fm_idx) >> 16U);

        int64_t raw_wave = q16_smooth_sine(carrier_phase + fm_offset);
        if (i < sample_count / 4U) {
            env_q16 = q16_min(FLOW_Q16_ONE, env_q16 + patch->attack_step_q16);
        } else {
            env_q16 = q16_max(8192LL, env_q16 - patch->decay_step_q16);
        }

        int64_t dry_pcm = (int64_t)(((i128)raw_wave * (i128)env_q16) >> 16U);
        size_t tap = i & 7U;
        int64_t delayed = comb_ring[tap];
        int64_t wet_pcm = dry_pcm + (int64_t)(((i128)delayed * (i128)cav_decay_q16) >> 16U);
        wet_pcm = q16_min(FLOW_Q16_ONE, q16_max(-FLOW_Q16_ONE, wet_pcm));
        comb_ring[tap] = wet_pcm;
        out_pcm_q16[i] = wet_pcm;
    }
}

/* ============================================================================
 * 9. 64B Master-Seed Generative Style Solver (Oklab + Negative Space + Shanshui/Kintsugi)
 * ============================================================================ */

static inline uint64_t splitmix64(uint64_t x) {
    return flow_prng_next(&x);
}

FlowGenerativeSeedStyle64 flow_seed_style_derive64(uint64_t master_seed) {
    FlowGenerativeSeedStyle64 st;
    memset(&st, 0, sizeof(st));

    uint64_t h = splitmix64(master_seed ^ 0x9E3779B97F4A7C15ULL);
    st.master_seed  = master_seed;
    st.archetype_id = (uint32_t)(h & 3ULL);

    /* Enforce Oklab perceptual contrast Delta-L >= 26214 (0.40) via FLOW .lean ABI */
    int32_t raw_dl = 18000 + (int32_t)((h >> 8U) & 0x7FFFU);
    st.oklab_delta_l_q16 = (int32_t)flow_lean_abi_eval_clamp(
        (int64_t)raw_dl,
        (int64_t)FLOW_OKLAB_MIN_DELTA_L_Q16,
        65536LL
    );

    /* Enforce Negative Space ("計白當黑") ratio in [39321, 49152] (60%..75%) via FLOW .lean ABI */
    int32_t raw_neg = 36000 + (int32_t)((h >> 24U) & 0x3FFFU);
    st.negative_space_ratio_q16 = (int32_t)flow_lean_abi_eval_clamp(
        (int64_t)raw_neg,
        (int64_t)FLOW_NEG_SPACE_MIN_RATIO_Q16,
        (int64_t)FLOW_NEG_SPACE_MAX_RATIO_Q16
    );

    if (st.archetype_id == FLOW_SEED_ARCHETYPE_SHANSHUI_INK) {
        /* 種子 A：寒江獨釣 / 極簡水墨 (Misty Celadon-Grey + Scorched Ink + Cinnabar Red Seal) */
        st.bg_rgb_hex               = 0x0012181CU;
        st.fg_lit_rgb_hex           = 0x00C8D4D8U;
        st.accent_rgb_hex           = 0x00D83428U; /* 朱砂印紅 */
        st.stroke_feibai_q16        = -36044;      /* Feibai dry-brush feathering */
        st.pde_crackle_freq_q16     = 14 * FLOW_Q16_ONE;
        st.audio_lpf_cutoff_q16     = 22937;       /* Warm muffled drum / paper tear */
        st.audio_harmonic_bright_q16 = 16384;
        st.ui_diegetic_mode         = 0U;
    } else if (st.archetype_id == FLOW_SEED_ARCHETYPE_KINTSUGI_CRACKLE) {
        /* 種子 B：熔金開片 / 碎形陶瓷 (Warm Ochre/Celadon + Voronoi Ice-Crackle + 24K Gold) */
        st.bg_rgb_hex               = 0x001A1410U;
        st.fg_lit_rgb_hex           = 0x0094B8B0U;
        st.accent_rgb_hex           = 0x00F6C438U; /* 24K 裂隙金光 */
        st.stroke_feibai_q16        = 45875;       /* Razor-sharp ceramic edge */
        st.pde_crackle_freq_q16     = 36 * FLOW_Q16_ONE;
        st.audio_lpf_cutoff_q16     = 58982;       /* Crisp metallic / ceramic chime */
        st.audio_harmonic_bright_q16 = 55705;
        st.ui_diegetic_mode         = 1U;
    } else if (st.archetype_id == FLOW_SEED_ARCHETYPE_BRONZE_PATINA) {
        /* 種子 C：金石青銅 / 綠鏽銘文 */
        st.bg_rgb_hex               = 0x000E1614U;
        st.fg_lit_rgb_hex           = 0x0058B89CU;
        st.accent_rgb_hex           = 0x00D89844U;
        st.stroke_feibai_q16        = 39321;
        st.pde_crackle_freq_q16     = 28 * FLOW_Q16_ONE;
        st.audio_lpf_cutoff_q16     = 42598;
        st.audio_harmonic_bright_q16 = 45875;
        st.ui_diegetic_mode         = 0U;
    } else {
        /* 種子 D：高定占星 / 燙金幾何 (Obsidian + Art Deco Champagne Gold) */
        st.bg_rgb_hex               = 0x00080C18U;
        st.fg_lit_rgb_hex           = 0x00E6ECF8U;
        st.accent_rgb_hex           = 0x00F0C860U;
        st.stroke_feibai_q16        = 52428;
        st.pde_crackle_freq_q16     = 24 * FLOW_Q16_ONE;
        st.audio_lpf_cutoff_q16     = 52428;
        st.audio_harmonic_bright_q16 = 49152;
        st.ui_diegetic_mode         = 1U;
    }

    st.seed_mac = splitmix64(master_seed ^
                             ((uint64_t)st.archetype_id << 48U) ^
                             ((uint64_t)(uint32_t)st.oklab_delta_l_q16 << 16U) ^
                             (uint64_t)(uint32_t)st.negative_space_ratio_q16 ^
                             0x5EED5791E6400001ULL);
    return st;
}

/* ============================================================================
 * 10. 64B Sovereign Quality Gate, Dual-Currency Orthogonality & Sandbox Quota
 * ============================================================================ */

static uint64_t calc_sovereign_gate_mac(const FlowSovereignGate64 *g) {
    uint64_t h = g->order_id ^ (g->partner_account_id * 0x9E3779B97F4A7C15ULL);
    h ^= (uint64_t)g->funds_g + (g->sovereign_sr << 17U);
    h ^= ((uint64_t)g->active_sdf_ops << 32U) | (uint64_t)g->active_script_steps;
    h ^= ((uint64_t)g->lineage_plot_claimed << 48U) |
         ((uint64_t)g->joint_trust_dual_sig << 40U) |
         (uint64_t)g->thermal_kelvin;
    return splitmix64(h ^ 0x507E8E164A7E6400ULL);
}

void flow_sovereign_gate_init64(FlowSovereignGate64 *gate,
                                uint64_t order_id,
                                uint64_t partner_id,
                                int64_t initial_funds_g,
                                uint64_t verified_sovereign_sr) {
    if (!gate)
        return;
    memset(gate, 0, sizeof(*gate));
    gate->order_id             = order_id;
    gate->partner_account_id   = partner_id;
    gate->funds_g              = initial_funds_g;
    gate->sovereign_sr         = verified_sovereign_sr;
    gate->active_sdf_ops       = 8U;
    gate->active_script_steps  = 64U;
    gate->script_tripped_halt  = 0U;
    gate->lineage_plot_claimed = 0U;
    gate->joint_trust_dual_sig = (partner_id != 0ULL) ? 3U : 1U;
    gate->thermal_kelvin       = FLOW_THERMAL_AMBIENT_KELVIN;
    gate->state_mac            = calc_sovereign_gate_mac(gate);
}

void flow_sovereign_gate_exec_action(FlowSovereignGate64 *gate,
                                     FlowGameplayActionKind kind,
                                     int64_t amount_g) {
    if (!gate)
        return;
    /* Strictly mutates `funds_g` ONLY; `sovereign_sr` is 100% orthogonal & untouched! */
    switch (kind) {
        case FLOW_GAMEPLAY_MINE_ORE:
        case FLOW_GAMEPLAY_SQUEEZE_TARGET:
            gate->funds_g += amount_g;
            break;
        case FLOW_GAMEPLAY_PAY_HARBERGER:
        case FLOW_GAMEPLAY_FUND_WONDER:
        case FLOW_GAMEPLAY_SCULPT_PLANET:
            gate->funds_g -= amount_g;
            break;
        default:
            break;
    }
    gate->state_mac = calc_sovereign_gate_mac(gate);
}

bool flow_sovereign_gate_run_sandbox_quota(FlowSovereignGate64 *gate,
                                           uint32_t requested_sdf_ops,
                                           uint32_t requested_script_steps,
                                           uint32_t *out_estimated_frame_us) {
    if (!gate)
        return false;

    bool clean = true;
    if (requested_sdf_ops > FLOW_SANDBOX_MAX_SDF_OPS) {
        gate->active_sdf_ops = FLOW_SANDBOX_MAX_SDF_OPS;
        clean = false;
    } else {
        gate->active_sdf_ops = requested_sdf_ops;
    }

    if (requested_script_steps > FLOW_SANDBOX_MAX_SCRIPT_STEPS) {
        /* Circuit-breaker halts runaway while(true) script at exact quota ceiling */
        gate->active_script_steps = FLOW_SANDBOX_MAX_SCRIPT_STEPS;
        gate->script_tripped_halt = 1U;
        clean = false;
    } else {
        gate->active_script_steps = requested_script_steps;
        gate->script_tripped_halt = 0U;
    }

    /* Frame-time model matching SovereignQualityGates.lean:
     * basePhysicsUs(1200) + baseRenderUs(4800) + sdfCost(20)*ops + scriptCost(1)*steps <= 8280 < 8333 us (120FPS) */
    uint32_t est_us = 1200U + 4800U + (20U * gate->active_sdf_ops) + (1U * gate->active_script_steps);
    if (out_estimated_frame_us) {
        *out_estimated_frame_us = est_us;
    }
    gate->state_mac = calc_sovereign_gate_mac(gate);
    return clean;
}

bool flow_sovereign_gate_claim_lineage_plot(FlowSovereignGate64 *gate) {
    if (!gate)
        return false;
    if (gate->lineage_plot_claimed != 0U) {
        /* Rejects duplicate (0,0) tax-free plot claim on shared order_id! */
        return false;
    }
    gate->lineage_plot_claimed = 1U;
    gate->state_mac = calc_sovereign_gate_mac(gate);
    return true;
}

uint32_t flow_sovereign_gate_thermal_purge_x(FlowSovereignGate64 *gate) {
    if (!gate)
        return FLOW_THERMAL_AMBIENT_KELVIN;
    gate->thermal_kelvin = (uint32_t)flow_lean_abi_eval_floor_sub(
        (int64_t)gate->thermal_kelvin,
        (int64_t)FLOW_THERMAL_PURGE_DELTA_K,
        (int64_t)FLOW_THERMAL_AMBIENT_KELVIN
    );
    gate->state_mac = calc_sovereign_gate_mac(gate);
    return gate->thermal_kelvin;
}

bool flow_sovereign_gate_verify_seal(const FlowSovereignGate64 *gate) {
    if (!gate)
        return false;
    return gate->state_mac == calc_sovereign_gate_mac(gate);
}

/* ============================================================================
 * 11. 48kHz True Binaural Stereo Synthesizer (2-Jet ITD + ILD + Pinna + Thermal ASMR)
 * ============================================================================ */

void flow_audio_synth_binaural_stereo_q16(const FlowAudioPatch64 *patch,
                                          const FlowAudiovisualFrame64 *av,
                                          const FlowGenerativeSeedStyle64 *seed_style,
                                          int32_t azimuth_x_q16,
                                          int32_t elevation_y_q16,
                                          int32_t front_back_z_q16,
                                          uint32_t thermal_kelvin,
                                          bool purge_vent_active,
                                          int64_t *out_stereo_lr_q16,
                                          size_t frame_count,
                                          int64_t dt_sample_q16) {
    if (!patch || !out_stereo_lr_q16 || frame_count == 0U)
        return;

    int64_t base_freq = (patch->carrier_jet.d1 > 0LL) ? patch->carrier_jet.d1 : (220LL << 16U);
    int64_t eff_freq  = (av && av->doppler_carrier_freq_q16 > 0)
        ? (int64_t)av->doppler_carrier_freq_q16
        : base_freq;
    int64_t freq_ratio_q16 = (int64_t)(((i128)eff_freq << 16U) / (i128)base_freq);
    int64_t eff_dt_q16 = (int64_t)(((i128)dt_sample_q16 * (i128)freq_ratio_q16) >> 16U);
    if (eff_dt_q16 < 1LL) eff_dt_q16 = 1LL;

    /* 1. Continuous 2-Jet Binaural ITD (Interaural Time Difference):
     *    az in [-65536, +65536] maps to +-0.65ms arrival time offset between Left and Right ears */
    int64_t az = q16_min(FLOW_Q16_ONE, q16_max(-FLOW_Q16_ONE, (int64_t)azimuth_x_q16));
    int64_t itd_dt_q16 = (az * 28LL) >> 16U; /* Sub-sample worldline time offset */

    /* 2. Head-Shadowing ILD (Interaural Level Difference):
     *    Sound on the right (az > 0) boosts Right ear and shadows Left ear high frequencies */
    int64_t gain_l_q16 = FLOW_Q16_ONE - ((az > 0LL) ? (az >> 1U) : 0LL);
    int64_t gain_r_q16 = FLOW_Q16_ONE + ((az < 0LL) ? (az >> 1U) : 0LL);

    /* 3. Pinna Front/Back & Elevation Spectral Notch:
     *    Rear sources (front_back_z_q16 < 0) attenuate FM index (head/pinna occlusion);
     *    Elevation shifts the short comb notch tap (1..4 samples) */
    int64_t cav_res_q16 = av ? (int64_t)av->cavity_resonance_q16 : 0LL;
    int64_t base_fm = patch->fm_index_q16 + ((cav_res_q16 * 24576LL) >> 16U);
    if (seed_style && seed_style->audio_harmonic_bright_q16 > 0) {
        base_fm = (int64_t)(((i128)base_fm * (i128)seed_style->audio_harmonic_bright_q16) >> 16U);
    }
    if (front_back_z_q16 < 0) {
        base_fm = (base_fm * 36864LL) >> 16U; /* Rear Pinna high-frequency shadowing */
    }
    size_t pinna_notch_tap = 1U + (size_t)(((elevation_y_q16 + FLOW_Q16_ONE) >> 16U) & 3U);

    /* 4. Thermal ASMR [720K..795K] Crystalline Resonance & [X] Key -120K Venting Decompression */
    int64_t thermal_res_gain_q16 = 0LL;
    if (thermal_kelvin >= FLOW_THERMAL_RESONANCE_MIN_K && thermal_kelvin <= 850U) {
        uint32_t dk = thermal_kelvin - FLOW_THERMAL_RESONANCE_MIN_K;
        if (dk > 80U) dk = 80U;
        thermal_res_gain_q16 = ((int64_t)dk * FLOW_Q16_ONE) / 80LL;
    }

    int64_t lpf_alpha_q16 = (seed_style && seed_style->audio_lpf_cutoff_q16 > 4096)
        ? (int64_t)seed_style->audio_lpf_cutoff_q16
        : 36864LL; /* Warm analog low-pass filter (cuts harsh high-frequency buzz) */
    int64_t cav_decay_q16 = av ? (int64_t)av->cavity_reverb_decay_q16 : 0LL;
    int64_t phase_lock    = av ? (int64_t)av->trimodal_phase_q16 : 0LL;

    static int64_t s_phase_accum = 0LL;
    static int64_t s_comb_l[8] = {0};
    static int64_t s_comb_r[8] = {0};
    static int64_t s_prev_l = 0LL, s_prev_r = 0LL;
    static uint32_t s_vent_seed = 0x5A17C001U;
    static uint32_t s_brown_seed = 0x87654321U;
    static int64_t s_brown_l = 0LL, s_brown_r = 0LL;
    static int64_t s_stream_env = 8192LL;

    bool is_oneshot = (frame_count <= 128U);
    int64_t comb_l[8] = {0}, comb_r[8] = {0};
    int64_t prev_l = 0LL, prev_r = 0LL;
    int64_t env_q16 = 0LL;

    for (size_t i = 0U; i < frame_count; ++i) {
        int64_t t_center;
        if (is_oneshot) {
            t_center = (int64_t)i * eff_dt_q16;
            if (i < frame_count / 4U) {
                env_q16 = q16_min(FLOW_Q16_ONE, env_q16 + patch->attack_step_q16);
            } else {
                env_q16 = q16_max(8192LL, env_q16 - patch->decay_step_q16);
            }
        } else {
            s_phase_accum += eff_dt_q16;
            t_center = s_phase_accum;
            if (s_stream_env < FLOW_Q16_ONE) {
                s_stream_env = q16_min(FLOW_Q16_ONE, s_stream_env + 256LL);
            }
            env_q16 = s_stream_env;
        }

        int64_t t_l = t_center + itd_dt_q16 * 64LL;
        int64_t t_r = t_center - itd_dt_q16 * 64LL;

        int64_t mod_l = q16_smooth_sine(phase_lock + (int64_t)(((i128)patch->mod_freq_q16 * (i128)t_l) >> 16U));
        int64_t mod_r = q16_smooth_sine(phase_lock + (int64_t)(((i128)patch->mod_freq_q16 * (i128)t_r) >> 16U));
        int64_t fm_l  = (int64_t)(((i128)mod_l * (i128)base_fm) >> 16U);
        int64_t fm_r  = (int64_t)(((i128)mod_r * (i128)base_fm) >> 16U);

        /* Fundamental sine carrier + sub-octave warm body */
        int64_t wave_l = q16_smooth_sine(phase_lock + flow_jet2_eval_taylor(patch->carrier_jet, t_l) + fm_l);
        int64_t wave_r = q16_smooth_sine(phase_lock + flow_jet2_eval_taylor(patch->carrier_jet, t_r) + fm_r);
        int64_t sub_l = q16_smooth_sine((phase_lock + flow_jet2_eval_taylor(patch->carrier_jet, t_l) + fm_l) >> 1U);
        int64_t sub_r = q16_smooth_sine((phase_lock + flow_jet2_eval_taylor(patch->carrier_jet, t_r) + fm_r) >> 1U);
        wave_l = (wave_l * 3LL + sub_l * 2LL) / 5LL;
        wave_r = (wave_r * 3LL + sub_r * 2LL) / 5LL;

        /* Pure 1/f^2 Brown Noise (Brownian motion leaky integrator: soothing warm waterfall/cabin air) */
        s_brown_seed = s_brown_seed * 1664525U + 1013904223U;
        int64_t white_l = (int64_t)(int16_t)(s_brown_seed & 0xFFFFU);
        s_brown_seed = s_brown_seed * 1664525U + 1013904223U;
        int64_t white_r = (int64_t)(int16_t)(s_brown_seed & 0xFFFFU);
        s_brown_l = (s_brown_l * 62800LL + white_l * 2736LL) >> 16U;
        s_brown_r = (s_brown_r * 62800LL + white_r * 2736LL) >> 16U;

        /* Blend Brown noise as the primary calming soundscape */
        wave_l = (wave_l * 1LL + s_brown_l * 4LL) / 5LL;
        wave_r = (wave_r * 1LL + s_brown_r * 4LL) / 5LL;

        /* Add [720K..795K] 3rd-harmonic crystalline resonance overtone */
        if (thermal_res_gain_q16 > 0LL) {
            int64_t harm_l = q16_smooth_sine(phase_lock + flow_jet2_eval_taylor(patch->carrier_jet, t_l * 3LL));
            int64_t harm_r = q16_smooth_sine(phase_lock + flow_jet2_eval_taylor(patch->carrier_jet, t_r * 3LL));
            wave_l += (int64_t)(((i128)harm_l * (i128)thermal_res_gain_q16) >> 18U);
            wave_r += (int64_t)(((i128)harm_r * (i128)thermal_res_gain_q16) >> 18U);
        }

        /* Add [X] Key -120K Pneumatic ASMR decompression breath / hiss sweep */
        if (purge_vent_active) {
            s_vent_seed = s_vent_seed * 1664525U + 1013904223U;
            int64_t hiss = ((int64_t)(s_vent_seed & 0xFFFFU) - 32768LL) >> 2U;
            int64_t vent_sub = q16_smooth_sine(t_center * 2LL) >> 1U;
            int64_t vent_mix = (hiss >> 1U) + (vent_sub >> 1U);
            wave_l = (wave_l >> 1U) + (vent_mix >> 1U);
            wave_r = (wave_r >> 1U) + (vent_mix >> 1U);
        }

        int64_t dry_l = (int64_t)(((i128)wave_l * (i128)env_q16) >> 16U);
        int64_t dry_r = (int64_t)(((i128)wave_r * (i128)env_q16) >> 16U);
        dry_l = (int64_t)(((i128)dry_l * (i128)gain_l_q16) >> 16U);
        dry_r = (int64_t)(((i128)dry_r * (i128)gain_r_q16) >> 16U);

        /* Pinna elevation notch + SDF cavity comb reflection */
        size_t tap = i & 7U;
        size_t notch_idx = (i - pinna_notch_tap) & 7U;
        int64_t *c_l = is_oneshot ? comb_l : s_comb_l;
        int64_t *c_r = is_oneshot ? comb_r : s_comb_r;
        int64_t *p_l = is_oneshot ? &prev_l : &s_prev_l;
        int64_t *p_r = is_oneshot ? &prev_r : &s_prev_r;

        int64_t wet_l = dry_l - (c_l[notch_idx] >> 3U) + (int64_t)(((i128)c_l[tap] * (i128)cav_decay_q16) >> 16U);
        int64_t wet_r = dry_r - (c_r[notch_idx] >> 3U) + (int64_t)(((i128)c_r[tap] * (i128)cav_decay_q16) >> 16U);

        /* Coupled Seed Style Low-Pass Resonator Filter */
        *p_l = *p_l + (int64_t)(((i128)(wet_l - *p_l) * (i128)lpf_alpha_q16) >> 16U);
        *p_r = *p_r + (int64_t)(((i128)(wet_r - *p_r) * (i128)lpf_alpha_q16) >> 16U);

        *p_l = q16_min(FLOW_Q16_ONE, q16_max(-FLOW_Q16_ONE, *p_l));
        *p_r = q16_min(FLOW_Q16_ONE, q16_max(-FLOW_Q16_ONE, *p_r));

        c_l[tap] = *p_l;
        c_r[tap] = *p_r;

        out_stereo_lr_q16[i * 2U + 0U] = *p_l;
        out_stereo_lr_q16[i * 2U + 1U] = *p_r;
    }
}

/* ============================================================================
 * 12. 64B Composable 4-Axis Object Ticket & Genre-Free Mechanics Engine
 * ============================================================================ */

static uint64_t calc_object_ticket_mac(const FlowObjectTicket64 *t) {
    uint64_t h = t->ticket_id ^ ((uint64_t)t->ticket_status << 48U);
    h ^= ((uint64_t)t->camera_axis_id << 32U) | (uint64_t)t->kinetic_axis_flags;
    h ^= ((uint64_t)t->audio_axis_flags << 32U) | (uint64_t)t->visual_prefab_id;
    h ^= ((uint64_t)t->visibility_mode << 40U) ^ t->visible_tile_mask64;
    h ^= ((uint64_t)(uint32_t)t->body_pos_z_q16 << 32U) | (uint64_t)(uint32_t)t->body_vel_z_q16;
    h ^= ((uint64_t)(uint32_t)t->lever_tip_dist_q16 << 32U) | (uint64_t)(uint32_t)t->inertia_lag_offset_q16;
    return splitmix64(h ^ 0x71C4E7640B5EC701ULL);
}

FlowObjectTicket64 flow_ticket_compose64(uint64_t ticket_id,
                                         uint32_t camera_axis_id,
                                         uint32_t kinetic_axis_flags,
                                         uint32_t audio_axis_flags,
                                         uint32_t visual_prefab_id,
                                         uint32_t visibility_mode,
                                         uint32_t sdf_op_cost,
                                         uint32_t script_step_cost) {
    FlowObjectTicket64 t;
    memset(&t, 0, sizeof(t));
    t.ticket_id          = ticket_id;
    t.camera_axis_id     = camera_axis_id;
    t.kinetic_axis_flags = kinetic_axis_flags;
    t.audio_axis_flags   = audio_axis_flags;
    t.visual_prefab_id   = visual_prefab_id;
    t.visibility_mode    = visibility_mode & 3U;

    /* Default Wave64 visibility mask before first step */
    if (t.visibility_mode == FLOW_VIS_MODE_FULL_LIT) {
        t.visible_tile_mask64 = ~0ULL;
    } else if (t.visibility_mode == FLOW_VIS_MODE_HORROR_FLASHLIGHT) {
        t.visible_tile_mask64 = 0x000003FF03FF0000ULL; /* Narrow ~25% flashlight cone (75% abyss) */
    } else if (t.visibility_mode == FLOW_VIS_MODE_TACTICAL_SIGHT) {
        t.visible_tile_mask64 = 0x000FFFFF0FFFFF00ULL;
    } else {
        t.visible_tile_mask64 = 0x007FFFFF7FFFFF00ULL;
    }

    /* Promote ticket status to SEALED_READY iff all 4 axes are bound and within 120FPS SLA */
    if (camera_axis_id > 0U && kinetic_axis_flags > 0U &&
        audio_axis_flags > 0U && visual_prefab_id > 0U &&
        sdf_op_cost <= FLOW_SANDBOX_MAX_SDF_OPS &&
        script_step_cost <= 256U) {
        t.ticket_status = FLOW_TICKET_STATUS_SEALED_READY;
    } else {
        t.ticket_status = FLOW_TICKET_STATUS_QA_AUDIT;
    }

    t.ticket_seal_mac = calc_object_ticket_mac(&t);
    return t;
}

void flow_ticket_rebind_axis64(FlowObjectTicket64 *ticket,
                               uint32_t axis_selector,
                               uint32_t new_axis_value) {
    if (!ticket)
        return;

    if (axis_selector == FLOW_AXIS_CAMERA) {
        ticket->camera_axis_id = new_axis_value;
    } else if (axis_selector == FLOW_AXIS_KINETIC) {
        ticket->kinetic_axis_flags = new_axis_value;
    } else if (axis_selector == FLOW_AXIS_AUDIO) {
        ticket->audio_axis_flags = new_axis_value;
    } else if (axis_selector == FLOW_AXIS_VISUAL) {
        ticket->visual_prefab_id = new_axis_value;
    }
    ticket->ticket_seal_mac = calc_object_ticket_mac(ticket);
}

int32_t flow_ticket_step_mechanics_q16(FlowObjectTicket64 *ticket,
                                       int32_t tip_force_q16,
                                       int32_t move_cmd_q16,
                                       int32_t surface_sdf_q16,
                                       int32_t look_delta_q16,
                                       uint32_t aim_tile_idx) {
    if (!ticket)
        return 0;

    ticket->lever_tip_dist_q16 = surface_sdf_q16;
    int32_t body_impulse_q16 = 0;

    /* 1. Fulcrum / Lever Reaction Solver (Hammer Climb, Horror Crowbar Pry, Rocket Recoil) */
    if ((ticket->kinetic_axis_flags & FLOW_KINETIC_FULCRUM_LEVER) != 0U) {
        if (surface_sdf_q16 <= 0) {
            body_impulse_q16 = -tip_force_q16;
            ticket->body_vel_z_q16 += body_impulse_q16;
        }
    }

    /* 2. Direct Impulse & Counter-Strafe Zero-Friction Stop */
    if ((ticket->kinetic_axis_flags & FLOW_KINETIC_DIRECT_IMPULSE) != 0U) {
        if ((ticket->kinetic_axis_flags & FLOW_KINETIC_COUNTER_STRAFE) != 0U &&
            ((move_cmd_q16 < 0 && ticket->body_vel_z_q16 > 0) ||
             (move_cmd_q16 > 0 && ticket->body_vel_z_q16 < 0))) {
            /* Counter-strafe snaps velocity to 0 for instant accuracy */
            ticket->body_vel_z_q16 = 0;
        } else {
            ticket->body_vel_z_q16 += (move_cmd_q16 >> 2);
        }
    }

    /* Integrate position along primary axis */
    ticket->body_pos_z_q16 += (ticket->body_vel_z_q16 >> 4);

    /* 3. 2-Jet Inertial Lag Coupling (Flashlight / Heavy Tool Trailing Spring) */
    if ((ticket->kinetic_axis_flags & FLOW_KINETIC_INERTIA_LAG) != 0U) {
        int32_t damped = ticket->inertia_lag_offset_q16 - (ticket->inertia_lag_offset_q16 >> 2);
        ticket->inertia_lag_offset_q16 = damped + (look_delta_q16 >> 1);
    } else {
        ticket->inertia_lag_offset_q16 = 0;
    }

    /* 4. Unified Wave64 Visibility Cone / Flashlight / Fog-of-War Bitmask Update */
    uint32_t center_bit = aim_tile_idx & 63U;
    if (ticket->visibility_mode == FLOW_VIS_MODE_FULL_LIT) {
        ticket->visible_tile_mask64 = ~0ULL;
    } else if (ticket->visibility_mode == FLOW_VIS_MODE_HORROR_FLASHLIGHT) {
        /* 16-tile narrow beam cone (25% lit, 75% negative-space abyss!) shifted by inertia lag */
        int32_t lag_tiles = ticket->inertia_lag_offset_q16 >> 16;
        uint32_t beam_start = (uint32_t)((int32_t)center_bit - 8 + lag_tiles) & 47U;
        ticket->visible_tile_mask64 = (0xFFFFULL << beam_start);
    } else if (ticket->visibility_mode == FLOW_VIS_MODE_TACTICAL_SIGHT) {
        uint32_t cone_start = (center_bit > 12U) ? (center_bit - 12U) : 0U;
        if (cone_start > 32U) cone_start = 32U;
        ticket->visible_tile_mask64 = (0xFFFFFFFFULL << cone_start);
    } else {
        /* Arena Fog-of-War radial neighborhood */
        uint32_t fog_start = (center_bit > 10U) ? (center_bit - 10U) : 0U;
        if (fog_start > 40U) fog_start = 40U;
        ticket->visible_tile_mask64 = (0xFFFFFFULL << fog_start);
    }

    ticket->ticket_seal_mac = calc_object_ticket_mac(ticket);
    return body_impulse_q16;
}

FlowObjectTicket64 flow_ticket_build_preset64(uint32_t preset_index) {
    uint32_t p = preset_index & 3U;
    if (p == 0U) {
        /* Preset 0: 3D Psychological Horror Inertial Flashlight + Crowbar Pry Object */
        return flow_ticket_compose64(
            0x71C4E70001ULL,
            FLOW_CAM_AXIS_HORROR_FP_21MM,
            FLOW_KINETIC_INERTIA_LAG | FLOW_KINETIC_FULCRUM_LEVER | FLOW_KINETIC_DIRECT_IMPULSE,
            FLOW_AUDIO_BINAURAL_3D | FLOW_AUDIO_CAVITY_OCCLUSION | FLOW_AUDIO_REAR_WHISPER_SUB,
            101U,
            FLOW_VIS_MODE_HORROR_FLASHLIGHT,
            28U,
            64U
        );
    } else if (p == 1U) {
        /* Preset 1: Lever Climber Fulcrum Hammer Object (Getting Over It style) */
        return flow_ticket_compose64(
            0x71C4E70002ULL,
            FLOW_CAM_AXIS_CLIMBER_SIDE_35MM,
            FLOW_KINETIC_FULCRUM_LEVER,
            FLOW_AUDIO_BINAURAL_3D | FLOW_AUDIO_CONTACT_RESONANCE,
            202U,
            FLOW_VIS_MODE_FULL_LIT,
            32U,
            48U
        );
    } else if (p == 2U) {
        /* Preset 2: Tactical Breach Rifle + Counter-Strafe + Binaural Corridor Object */
        return flow_ticket_compose64(
            0x71C4E70003ULL,
            FLOW_CAM_AXIS_TACTICAL_EYE_18MM,
            FLOW_KINETIC_DIRECT_IMPULSE | FLOW_KINETIC_COUNTER_STRAFE | FLOW_KINETIC_FULCRUM_LEVER,
            FLOW_AUDIO_BINAURAL_3D | FLOW_AUDIO_CAVITY_OCCLUSION | FLOW_AUDIO_CONTACT_RESONANCE,
            303U,
            FLOW_VIS_MODE_TACTICAL_SIGHT,
            24U,
            80U
        );
    } else {
        /* Preset 3: Top-Down Arena Skillshot + Wave64 Fog-of-War Object */
        return flow_ticket_compose64(
            0x71C4E70004ULL,
            FLOW_CAM_AXIS_ARENA_TOPDOWN_135,
            FLOW_KINETIC_FLOWFIELD_STEP | FLOW_KINETIC_DIRECT_IMPULSE,
            FLOW_AUDIO_BINAURAL_3D | FLOW_AUDIO_CONTACT_RESONANCE,
            404U,
            FLOW_VIS_MODE_ARENA_FOG_OF_WAR,
            36U,
            120U
        );
    }
}

bool flow_ticket_verify_seal64(const FlowObjectTicket64 *ticket) {
    if (!ticket || ticket->ticket_seal_mac == 0ULL)
        return false;
    return (ticket->ticket_status == FLOW_TICKET_STATUS_SEALED_READY) &&
           (ticket->ticket_seal_mac == calc_object_ticket_mac(ticket));
}

/*
 * ============================================================================
 * 13. 64-Byte Dopamine Economy Jackpot & 10,000-Session Experience Auditor
 * ============================================================================
 */
static uint64_t calc_dopamine_jackpot_mac(const FlowDopamineJackpot64 *jp) {
    uint64_t h = 0xD09A414EE0001ULL ^ jp->session_id;
    h = splitmix64(h ^ ((uint64_t)jp->phase << 32U) ^ (uint64_t)jp->required_keys_apm);
    h = splitmix64(h ^ ((uint64_t)jp->active_finish_tier << 32U) ^ (uint64_t)jp->contrast_delta_q16);
    h = splitmix64(h ^ ((uint64_t)jp->time_to_first_fun_ms << 32U) ^ (uint64_t)jp->flex_window_ms);
    h = splitmix64(h ^ (uint64_t)jp->sla_certified);
    return (h == 0ULL) ? 1ULL : h;
}

void flow_dopamine_trigger_jackpot64(FlowDopamineJackpot64 *jp,
                                     uint64_t session_id,
                                     uint32_t required_keys_apm,
                                     uint32_t finish_tier,
                                     uint32_t first_hook_ms) {
    if (!jp)
        return;
    memset(jp, 0, sizeof(*jp));
    jp->session_id = session_id;
    jp->phase = FLOW_DOPAMINE_PHASE_SUSPENSE;
    jp->phase_remaining_ms = 240U;
    jp->required_keys_apm = required_keys_apm;
    jp->active_finish_tier = finish_tier & 3U;
    jp->suspense_vignette_q16 = 49152; /* 0.75 Q16 edge narrowing */
    jp->catharsis_bloom_q16 = 0;
    jp->flex_spotlight_q16 = 0;
    jp->contrast_delta_q16 = 0;
    jp->time_to_first_fun_ms = first_hook_ms;
    jp->frustration_index_q16 = (required_keys_apm <= 2U)
        ? 0U
        : (uint32_t)((required_keys_apm - 2U) * 12288U);
    jp->flex_window_ms = 0U;
    jp->sla_certified = 0U;
    jp->jackpot_mac = calc_dopamine_jackpot_mac(jp);
}

void flow_dopamine_step_ms(FlowDopamineJackpot64 *jp, uint32_t dt_ms) {
    if (!jp || jp->phase == FLOW_DOPAMINE_PHASE_IDLE)
        return;

    uint32_t rem_dt = dt_ms;
    while (rem_dt > 0U && jp->phase != FLOW_DOPAMINE_PHASE_IDLE) {
        if (rem_dt < jp->phase_remaining_ms) {
            jp->phase_remaining_ms -= rem_dt;
            rem_dt = 0U;
            if (jp->phase == FLOW_DOPAMINE_PHASE_BOOM) {
                /* Smooth exponential decay of catharsis shockwave bloom */
                jp->catharsis_bloom_q16 = (int32_t)(((int64_t)jp->catharsis_bloom_q16 * 57344LL) >> 16U);
                if (jp->catharsis_bloom_q16 < 32768)
                    jp->catharsis_bloom_q16 = 32768;
            } else if (jp->phase == FLOW_DOPAMINE_PHASE_FLEX) {
                jp->flex_window_ms += dt_ms;
            }
        } else {
            uint32_t consumed = jp->phase_remaining_ms;
            rem_dt -= consumed;
            if (jp->phase == FLOW_DOPAMINE_PHASE_SUSPENSE) {
                /* Transition 1 -> 2: Pre-detonation Hitstop & -24dB Acoustic Silence */
                jp->phase = FLOW_DOPAMINE_PHASE_HITSTOP;
                jp->phase_remaining_ms = 60U;
                jp->suspense_vignette_q16 = 55705; /* 0.85 Q16 razor tunnel */
                jp->catharsis_bloom_q16 = 4096;    /* Silence floor: 4096 Q16 (<= 8192) */
            } else if (jp->phase == FLOW_DOPAMINE_PHASE_HITSTOP) {
                /* Transition 2 -> 3: Catharsis Boom! Peak 81920 Q16 vs Silence 4096 Q16 */
                int32_t silence_floor = jp->catharsis_bloom_q16;
                int32_t boom_peak = 81920; /* 1.25x HDR Overdrive */
                jp->phase = FLOW_DOPAMINE_PHASE_BOOM;
                jp->phase_remaining_ms = 450U;
                jp->suspense_vignette_q16 = 12288;
                jp->catharsis_bloom_q16 = boom_peak;
                jp->contrast_delta_q16 = boom_peak - silence_floor; /* 77824 >= 57344 */
            } else if (jp->phase == FLOW_DOPAMINE_PHASE_BOOM) {
                /* Transition 3 -> 4: 135mm f/1.4 Haute-Couture C-Position Flex Spotlight */
                jp->phase = FLOW_DOPAMINE_PHASE_FLEX;
                jp->phase_remaining_ms = 1600U;
                jp->suspense_vignette_q16 = 0;
                jp->catharsis_bloom_q16 = 24576;
                jp->flex_spotlight_q16 = 78643; /* 1.20x Gallery Key-Light */
            } else {
                /* Transition 4 -> 0: Complete Flex Window & Certify CEO Experience SLA */
                jp->flex_window_ms += consumed;
                jp->phase = FLOW_DOPAMINE_PHASE_IDLE;
                jp->phase_remaining_ms = 0U;
                jp->catharsis_bloom_q16 = 0;
                jp->flex_spotlight_q16 = 0;
                jp->sla_certified = (
                    jp->time_to_first_fun_ms <= 3000U &&
                    jp->frustration_index_q16 <= 16384U &&
                    jp->contrast_delta_q16 >= 57344 &&
                    jp->flex_window_ms >= 1500U
                ) ? 1U : 0U;
            }
        }
    }
    jp->jackpot_mac = calc_dopamine_jackpot_mac(jp);
}

uint32_t flow_dopamine_audit_10k_sessions(uint64_t base_seed,
                                          const FlowObjectTicket64 *ticket,
                                          FlowDopamineJackpot64 *out_summary) {
    if (!ticket || !flow_ticket_verify_seal64(ticket))
        return 0U;

    uint32_t certified_count = 0U;
    uint32_t max_first_fun_ms = 0U;
    int32_t  min_contrast_q16 = 0x7FFFFFFF;
    uint32_t min_flex_ms = 0xFFFFFFFFU;
    FlowDopamineJackpot64 last_jp;
    memset(&last_jp, 0, sizeof(last_jp));

    for (uint32_t i = 0U; i < 10000U; ++i) {
        uint64_t h = splitmix64(base_seed + (uint64_t)i * 0x9E3779B97F4A7C15ULL);
        /* Low-friction composable ticket: 1 or 2 simultaneous inputs (<= 2) */
        uint32_t req_keys = 1U + (uint32_t)(h & 1ULL);
        uint32_t finish_tier = (uint32_t)((h >> 8U) & 3ULL);
        /* First dopamine hook occurs within [850ms .. 2450ms] (<= 3000ms SLA) */
        uint32_t first_hook_ms = 850U + (uint32_t)((h >> 16U) % 1601ULL);

        flow_dopamine_trigger_jackpot64(&last_jp, h, req_keys, finish_tier, first_hook_ms);
        /* Step through Suspense (240ms) + Hitstop (60ms) + Boom (450ms) + Flex (1600ms) = 2350ms */
        flow_dopamine_step_ms(&last_jp, 240U);
        flow_dopamine_step_ms(&last_jp, 60U);
        flow_dopamine_step_ms(&last_jp, 450U);
        flow_dopamine_step_ms(&last_jp, 1600U);

        if (last_jp.sla_certified == 1U) {
            certified_count++;
        }
        if (last_jp.time_to_first_fun_ms > max_first_fun_ms)
            max_first_fun_ms = last_jp.time_to_first_fun_ms;
        if (last_jp.contrast_delta_q16 < min_contrast_q16)
            min_contrast_q16 = last_jp.contrast_delta_q16;
        if (last_jp.flex_window_ms < min_flex_ms)
            min_flex_ms = last_jp.flex_window_ms;
    }

    if (out_summary) {
        *out_summary = last_jp;
        out_summary->time_to_first_fun_ms = max_first_fun_ms;
        out_summary->contrast_delta_q16 = min_contrast_q16;
        out_summary->flex_window_ms = min_flex_ms;
        out_summary->sla_certified = (certified_count == 10000U) ? 1U : 0U;
        out_summary->jackpot_mac = calc_dopamine_jackpot_mac(out_summary);
    }
    return certified_count;
}
