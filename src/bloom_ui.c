#include "bloom_ui.h"
#include "bloom_scene.h"
#include "bloom_prefab.h"
#include "flow.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

/*
 * ============================================================================
 * Pillar 2 ("React Native"): Declarative SVG Compiler, Zero-VDOM Flexbox /
 * Auto-Bento Layout, Wave64 Dual-Culling, Pointer Capture, IME & LaTeX SDF
 * Zero FloatOps | Zero Heap Allocations | FLOW v6.0.0 Native Tactile Physics
 * ============================================================================
 */

static inline int32_t ui_clamp32(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int32_t ui_clamp64_to_32(int64_t v, int32_t lo, int32_t hi) {
    if (v < (int64_t)lo) return lo;
    if (v > (int64_t)hi) return hi;
    return (int32_t)v;
}

static inline int32_t ui_abs32(int32_t x) {
    if (x == INT32_MIN) return INT32_MAX;
    return (x < 0) ? -x : x;
}

static void ui_stamp_mac(void *block64, uint64_t seed, uint32_t *out_lo, uint32_t *out_hi) {
    uint64_t mac = bloom_hash56_fnv1a(block64, seed);
    *out_lo = (uint32_t)(mac & 0xFFFFFFFFULL);
    *out_hi = (uint32_t)(mac >> 32ULL);
}

/* 1. Wave64 8x8 Spatial Coverage Computation & ViewBox Normalization
   Normalized screen coordinates: X, Y in [-65536 .. +65536] (center = 0,0) */
int32_t bloom_svg_norm_viewbox_q16(int32_t coord_px, int32_t extent_px) {
    if (extent_px <= 0) return 0;
    /* Map [0 .. extent_px] -> [-65536 .. +65536] */
    int64_t num = ((int64_t)coord_px * 2LL - (int64_t)extent_px) * (int64_t)BLOOM_Q16_ONE;
    int64_t res = num / (int64_t)extent_px;
    if (res < -BLOOM_Q16_ONE) res = -BLOOM_Q16_ONE;
    if (res > BLOOM_Q16_ONE) res = BLOOM_Q16_ONE;
    return (int32_t)res;
}

uint32_t bloom_ui_cursor_to_wave64_bit(int32_t cursor_x_q16, int32_t cursor_y_q16) {
    /* Map [-65536 .. +65536] into [0..7] x [0..7] = 64 tiles (overflow-proof in int64_t) */
    int32_t cx = ui_clamp64_to_32(((int64_t)cursor_x_q16 + (int64_t)BLOOM_Q16_ONE) >> 14, 0, 7);
    int32_t cy = ui_clamp64_to_32(((int64_t)cursor_y_q16 + (int64_t)BLOOM_Q16_ONE) >> 14, 0, 7);
    return (uint32_t)(cy * 8 + cx);
}

void bloom_ui_compute_wave64_mask(int32_t cx_q16,
                                  int32_t cy_q16,
                                  int32_t hw_q16,
                                  int32_t hh_q16,
                                  uint32_t *out_lo32,
                                  uint32_t *out_hi32) {
    uint32_t lo = 0U, hi = 0U;
    /* Pad by 1.25x for 2-Jet hover expansion so GPU/CPU never clips expanded springs */
    int64_t hw = (hw_q16 > 0) ? (int64_t)hw_q16 : 0LL;
    int64_t hh = (hh_q16 > 0) ? (int64_t)hh_q16 : 0LL;
    int64_t pad_w = hw + (hw >> 2) + 1024LL;
    int64_t pad_h = hh + (hh >> 2) + 1024LL;
    int32_t x0 = ui_clamp64_to_32((((int64_t)cx_q16 - pad_w) + (int64_t)BLOOM_Q16_ONE) >> 14, 0, 7);
    int32_t x1 = ui_clamp64_to_32((((int64_t)cx_q16 + pad_w) + (int64_t)BLOOM_Q16_ONE) >> 14, 0, 7);
    int32_t y0 = ui_clamp64_to_32((((int64_t)cy_q16 - pad_h) + (int64_t)BLOOM_Q16_ONE) >> 14, 0, 7);
    int32_t y1 = ui_clamp64_to_32((((int64_t)cy_q16 + pad_h) + (int64_t)BLOOM_Q16_ONE) >> 14, 0, 7);

    for (int32_t r = y0; r <= y1; ++r) {
        for (int32_t c = x0; c <= x1; ++c) {
            bloom_wave64_set_u32x2(&lo, &hi, (uint32_t)(r * 8 + c));
        }
    }
    if (out_lo32) *out_lo32 = lo;
    if (out_hi32) *out_hi32 = hi;
}

/* 2. Zero-Malloc W3C .svg Compiler (<svg>, <rect>, <circle>, <line>, <path>) */
static bool ui_is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static const char *ui_parse_int(const char *p, int32_t *out_val, bool *out_ok) {
    while (*p && (ui_is_space(*p) || *p == ',')) ++p;
    bool neg = false;
    if (*p == '-' || *p == '+') {
        neg = (*p == '-');
        ++p;
    }
    if (*p < '0' || *p > '9') {
        if (out_ok) *out_ok = false;
        return p;
    }
    int64_t acc = 0;
    while (*p >= '0' && *p <= '9') {
        acc = acc * 10 + (*p - '0');
        if (acc > 0x7FFFFFFFLL) acc = 0x7FFFFFFFLL;
        ++p;
    }
    /* Skip optional decimal fraction in SVG attribute */
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') ++p;
    }
    *out_val = neg ? -(int32_t)acc : (int32_t)acc;
    if (out_ok) *out_ok = true;
    return p;
}

static const char *ui_find_attr_in_tag(const char *tag_start, const char *tag_end, const char *attr) {
    if (!tag_start || !tag_end || tag_end <= tag_start || !attr) return NULL;
    size_t klen = strlen(attr);
    const char *p = tag_start;
    while ((size_t)(tag_end - p) > klen + 2U) {
        if (p == tag_start || p[-1] == ' ' || p[-1] == '\t' || p[-1] == '\n' || p[-1] == '\r') {
            if (strncmp(p, attr, klen) == 0 && p[klen] == '=') {
                const char *v = p + klen + 1U;
                if (v < tag_end && (*v == '"' || *v == '\'')) ++v;
                return v;
            }
        }
        ++p;
    }
    return NULL;
}

static int32_t ui_attr_i32(const char *ts, const char *te, const char *attr, int32_t def_val) {
    const char *v = ui_find_attr_in_tag(ts, te, attr);
    if (!v) return def_val;
    int32_t parsed = def_val;
    bool ok = false;
    (void)ui_parse_int(v, &parsed, &ok);
    return ok ? parsed : def_val;
}

static uint32_t ui_attr_hex_rgb(const char *ts, const char *te, const char *attr, uint32_t def_rgb) {
    const char *v = ui_find_attr_in_tag(ts, te, attr);
    if (!v) return def_rgb;
    if (*v == '#') ++v;
    else if (v[0] == '0' && (v[1] == 'x' || v[1] == 'X')) v += 2;
    uint32_t acc = 0U;
    uint32_t digits = 0U;
    while (digits < 6U && v < te) {
        char c = *v++;
        uint32_t nib = 0U;
        if (c >= '0' && c <= '9') nib = (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') nib = 10U + (uint32_t)(c - 'a');
        else if (c >= 'A' && c <= 'F') nib = 10U + (uint32_t)(c - 'A');
        else break;
        acc = (acc << 4U) | nib;
        ++digits;
    }
    return (digits == 6U) ? acc : def_rgb;
}

static void ui_glyph_add_seg(BloomGlyph64 *g, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                             int32_t vb_min_x, int32_t vb_min_y, int32_t vb_w, int32_t vb_h) {
    if (!g || g->stroke_count >= 5U || vb_w <= 0 || vb_h <= 0) return;
    uint32_t idx = g->stroke_count * 4U;
    /* Normalize into Q8 [-256 .. +256] using overflow-free int64_t */
    g->seg_coords_q8[idx + 0U] = (int16_t)ui_clamp64_to_32((((int64_t)x1 - vb_min_x) * 512LL) / vb_w - 256LL, -256, 256);
    g->seg_coords_q8[idx + 1U] = (int16_t)ui_clamp64_to_32((((int64_t)y1 - vb_min_y) * 512LL) / vb_h - 256LL, -256, 256);
    g->seg_coords_q8[idx + 2U] = (int16_t)ui_clamp64_to_32((((int64_t)x2 - vb_min_x) * 512LL) / vb_w - 256LL, -256, 256);
    g->seg_coords_q8[idx + 3U] = (int16_t)ui_clamp64_to_32((((int64_t)y2 - vb_min_y) * 512LL) / vb_h - 256LL, -256, 256);
    g->stroke_count++;
}

static void ui_parse_svg_path_d(const char *d_attr, const char *te, BloomGlyph64 *g,
                                int32_t vb_min_x, int32_t vb_min_y, int32_t vb_w, int32_t vb_h) {
    const char *p = d_attr;
    int32_t cx = 0, cy = 0, sx = 0, sy = 0;
    char cmd = 0;
    while (p < te && *p != '"' && *p != '\'') {
        while (p < te && (ui_is_space(*p) || *p == ',')) ++p;
        if (p >= te || *p == '"' || *p == '\'') break;
        if ((*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')) {
            cmd = *p++;
            if (cmd == 'Z' || cmd == 'z') {
                ui_glyph_add_seg(g, cx, cy, sx, sy, vb_min_x, vb_min_y, vb_w, vb_h);
                cx = sx;
                cy = sy;
                continue;
            }
        }
        bool ok1 = false, ok2 = false;
        int32_t v1 = 0, v2 = 0;
        if (cmd == 'M' || cmd == 'L') {
            p = ui_parse_int(p, &v1, &ok1);
            p = ui_parse_int(p, &v2, &ok2);
            if (!ok1 || !ok2) break;
            if (cmd == 'M') {
                cx = sx = v1;
                cy = sy = v2;
                cmd = 'L';
            } else {
                ui_glyph_add_seg(g, cx, cy, v1, v2, vb_min_x, vb_min_y, vb_w, vb_h);
                cx = v1;
                cy = v2;
            }
        } else if (cmd == 'H') {
            p = ui_parse_int(p, &v1, &ok1);
            if (!ok1) break;
            ui_glyph_add_seg(g, cx, cy, v1, cy, vb_min_x, vb_min_y, vb_w, vb_h);
            cx = v1;
        } else if (cmd == 'V') {
            p = ui_parse_int(p, &v1, &ok1);
            if (!ok1) break;
            ui_glyph_add_seg(g, cx, cy, cx, v1, vb_min_x, vb_min_y, vb_w, vb_h);
            cy = v1;
        } else {
            ++p;
        }
    }
}

size_t bloom_svg_compile_text(const char *svg_text,
                              BloomUiNode64 *out_nodes,
                              size_t max_nodes,
                              BloomGlyph64 *out_glyphs,
                              size_t max_glyphs,
                              size_t *out_glyph_count,
                              BloomDomState64 *out_dom) {
    if (!svg_text || !out_nodes || max_nodes == 0U) return 0U;
    size_t n_nodes = 0U;
    size_t n_glyphs = 0U;
    int32_t vb_min_x = 0, vb_min_y = 0, vb_w = 1280, vb_h = 720;
    uint32_t dom_lo = 0U, dom_hi = 0U;

    const char *p = svg_text;
    while (*p) {
        const char *lt = strchr(p, '<');
        if (!lt) break;
        /* Skip XML comments <!-- ... --> */
        if (strncmp(lt, "<!--", 4U) == 0) {
            const char *cend = strstr(lt + 4U, "-->");
            if (!cend) break;
            p = cend + 3U;
            continue;
        }
        const char *gt = strchr(lt, '>');
        if (!gt) break;

        if (strncmp(lt, "<svg", 4U) == 0) {
            const char *vb = ui_find_attr_in_tag(lt + 4U, gt, "viewBox");
            if (vb) {
                bool ok = false;
                vb = ui_parse_int(vb, &vb_min_x, &ok);
                vb = ui_parse_int(vb, &vb_min_y, &ok);
                vb = ui_parse_int(vb, &vb_w, &ok);
                (void)ui_parse_int(vb, &vb_h, &ok);
                if (vb_w <= 0) vb_w = 1280;
                if (vb_h <= 0) vb_h = 720;
            } else {
                vb_w = ui_attr_i32(lt + 4U, gt, "width", 1280);
                vb_h = ui_attr_i32(lt + 4U, gt, "height", 720);
                if (vb_w <= 0) vb_w = 1280;
                if (vb_h <= 0) vb_h = 720;
            }
            vb_min_x = ui_clamp32(vb_min_x, -1048576, 1048576);
            vb_min_y = ui_clamp32(vb_min_y, -1048576, 1048576);
            vb_w = ui_clamp32(vb_w, 1, 1048576);
            vb_h = ui_clamp32(vb_h, 1, 1048576);
        } else if ((strncmp(lt, "<rect", 5U) == 0 || strncmp(lt, "<circle", 7U) == 0) &&
                   n_nodes < max_nodes) {
            bool is_circle = (strncmp(lt, "<circle", 7U) == 0);
            const char *ts = lt + (is_circle ? 7U : 5U);
            /* Only compile UI elements tagged with data-node-id (or all top-level shapes if <= max_nodes) */
            int32_t nid = ui_attr_i32(ts, gt, "data-node-id", (int32_t)(n_nodes + 1U));
            if (nid > 0) {
                BloomUiNode64 *nd = &out_nodes[n_nodes];
                memset(nd, 0, sizeof(*nd));
                nd->node_id = (uint32_t)nid;
                nd->anchor_mode = (uint32_t)ui_attr_i32(ts, gt, "data-anchor", 0);
                int32_t bind_ch = ui_attr_i32(ts, gt, "data-bind", -1);
                nd->bind_channel = (bind_ch >= 0 && bind_ch < (int32_t)BLOOM_REG_CHANNELS)
                    ? (uint32_t)bind_ch : 0xFFFFFFFFU;
                nd->action_opcode = (uint32_t)ui_attr_i32(ts, gt, "data-action", 0);
                uint32_t glass_m = (uint32_t)ui_attr_i32(ts, gt, "data-glass", BLOOM_GLASS_BEER_LAMBERT) & 0xFU;
                uint32_t rgb = ui_attr_hex_rgb(ts, gt, "fill", 0x20D8FFU);
                nd->style_and_state = glass_m | BLOOM_UI_STATE_GLASS_BIT | (rgb << 8U);
                nd->scale_jet_v_q16 = BLOOM_Q16_ONE;
                nd->bound_value_q16 = ui_attr_i32(ts, gt, "data-init-q16", 32768);

                if (is_circle) {
                    int32_t cx_px = ui_clamp64_to_32((int64_t)ui_attr_i32(ts, gt, "cx", vb_min_x + vb_w / 2) - vb_min_x, -1048576, 1048576);
                    int32_t cy_px = ui_clamp64_to_32((int64_t)ui_attr_i32(ts, gt, "cy", vb_min_y + vb_h / 2) - vb_min_y, -1048576, 1048576);
                    int32_t r_px  = ui_clamp32(ui_attr_i32(ts, gt, "r", 24), 1, 1048576);
                    int32_t sw_px = ui_clamp32(ui_attr_i32(ts, gt, "stroke-width", 6), 0, r_px);
                    nd->prim_type = (uint32_t)ui_attr_i32(ts, gt, "data-prim", BLOOM_UI_PRIM_CIRCLE_RING);
                    nd->offset_x_q16 = bloom_svg_norm_viewbox_q16(cx_px, vb_w);
                    nd->offset_y_q16 = bloom_svg_norm_viewbox_q16(cy_px, vb_h);
                    nd->half_w_q16 = ui_clamp64_to_32(((int64_t)r_px * 2LL * BLOOM_Q16_ONE) / vb_w, 512, BLOOM_Q16_ONE);
                    nd->half_h_q16 = ui_clamp64_to_32(((int64_t)(r_px - sw_px) * 2LL * BLOOM_Q16_ONE) / vb_w, 512, BLOOM_Q16_ONE);
                } else {
                    int32_t x_px = ui_clamp64_to_32((int64_t)ui_attr_i32(ts, gt, "x", 0) - vb_min_x, -1048576, 1048576);
                    int32_t y_px = ui_clamp64_to_32((int64_t)ui_attr_i32(ts, gt, "y", 0) - vb_min_y, -1048576, 1048576);
                    int32_t w_px = ui_clamp32(ui_attr_i32(ts, gt, "width", 120), 1, 1048576);
                    int32_t h_px = ui_clamp32(ui_attr_i32(ts, gt, "height", 40), 1, 1048576);
                    int32_t rx_px = ui_clamp32(ui_attr_i32(ts, gt, "rx", 8), 0, 1048576);
                    nd->prim_type = (uint32_t)ui_attr_i32(ts, gt, "data-prim", BLOOM_UI_PRIM_RECT_GLASS);
                    nd->offset_x_q16 = bloom_svg_norm_viewbox_q16(x_px + w_px / 2, vb_w);
                    nd->offset_y_q16 = bloom_svg_norm_viewbox_q16(y_px + h_px / 2, vb_h);
                    nd->half_w_q16 = ui_clamp64_to_32(((int64_t)w_px * BLOOM_Q16_ONE) / vb_w, 256, BLOOM_Q16_ONE);
                    nd->half_h_q16 = ui_clamp64_to_32(((int64_t)h_px * BLOOM_Q16_ONE) / vb_h, 256, BLOOM_Q16_ONE);
                    nd->corner_radius_q16 = ui_clamp64_to_32(((int64_t)rx_px * 2LL * BLOOM_Q16_ONE) / vb_h, 0, BLOOM_Q16_ONE);
                }

                bloom_ui_compute_wave64_mask(nd->offset_x_q16, nd->offset_y_q16,
                                             nd->half_w_q16, nd->half_h_q16,
                                             &nd->tile_mask_lo32, &nd->tile_mask_hi32);
                dom_lo |= nd->tile_mask_lo32;
                dom_hi |= nd->tile_mask_hi32;
                ++n_nodes;
            }
        } else if (strncmp(lt, "<path", 5U) == 0 && out_glyphs && n_glyphs < max_glyphs) {
            const char *ts = lt + 5U;
            const char *d_attr = ui_find_attr_in_tag(ts, gt, "d");
            if (d_attr) {
                BloomGlyph64 *g = &out_glyphs[n_glyphs];
                memset(g, 0, sizeof(*g));
                g->glyph_id = (uint32_t)ui_attr_i32(ts, gt, "data-glyph-id", (int32_t)(n_glyphs + 1U));
                g->stroke_width_q16 = ui_clamp32(ui_attr_i32(ts, gt, "stroke-width", 3), 1, 1024) * 1024;
                g->stroke_rgb24 = ui_attr_hex_rgb(ts, gt, "stroke", 0x00FFCCU);
                ui_parse_svg_path_d(d_attr, gt, g, vb_min_x, vb_min_y, vb_w, vb_h);
                ui_stamp_mac(g, 0x619F0064ULL, &g->mac_lo32, &g->mac_hi32);
                ++n_glyphs;
            }
        } else if (strncmp(lt, "<line", 5U) == 0 && out_glyphs && max_glyphs > 0U) {
            const char *ts = lt + 5U;
            uint32_t gid = (uint32_t)ui_attr_i32(ts, gt, "data-glyph-id", 1);
            size_t slot = 0U;
            bool found = false;
            for (size_t k = 0U; k < n_glyphs; ++k) {
                if (out_glyphs[k].glyph_id == gid) {
                    slot = k;
                    found = true;
                    break;
                }
            }
            if (!found && n_glyphs < max_glyphs) {
                slot = n_glyphs++;
                memset(&out_glyphs[slot], 0, sizeof(BloomGlyph64));
                out_glyphs[slot].glyph_id = gid;
                out_glyphs[slot].stroke_width_q16 = ui_clamp32(ui_attr_i32(ts, gt, "stroke-width", 3), 1, 1024) * 1024;
                out_glyphs[slot].stroke_rgb24 = ui_attr_hex_rgb(ts, gt, "stroke", 0x00FFCCU);
            }
            if (found || slot < n_glyphs) {
                int32_t x1 = ui_attr_i32(ts, gt, "x1", 0);
                int32_t y1 = ui_attr_i32(ts, gt, "y1", 0);
                int32_t x2 = ui_attr_i32(ts, gt, "x2", 0);
                int32_t y2 = ui_attr_i32(ts, gt, "y2", 0);
                ui_glyph_add_seg(&out_glyphs[slot], x1, y1, x2, y2, vb_min_x, vb_min_y, vb_w, vb_h);
                ui_stamp_mac(&out_glyphs[slot], 0x619F0064ULL,
                             &out_glyphs[slot].mac_lo32, &out_glyphs[slot].mac_hi32);
            }
        }
        p = gt + 1U;
    }

    if (out_glyph_count) *out_glyph_count = n_glyphs;
    if (out_dom) {
        uint32_t prev_epoch = out_dom->hot_reload_epoch;
        memset(out_dom, 0, sizeof(*out_dom));
        out_dom->compiled_node_count = (uint32_t)n_nodes;
        out_dom->hot_reload_epoch = prev_epoch + 1U;
        out_dom->dom_mask_lo32 = dom_lo;
        out_dom->dom_mask_hi32 = dom_hi;
        ui_stamp_mac(out_dom, 0xD0A00064ULL, &out_dom->mac_lo32, &out_dom->mac_hi32);
    }
    return n_nodes;
}

size_t bloom_svg_compile_file(const char *svg_path,
                              BloomUiNode64 *out_nodes,
                              size_t max_nodes,
                              BloomGlyph64 *out_glyphs,
                              size_t max_glyphs,
                              size_t *out_glyph_count,
                              BloomDomState64 *out_dom) {
    if (!svg_path) return 0U;
    FILE *fp = fopen(svg_path, "rb");
    if (!fp) return 0U;
    char svg_buf[16384];
    size_t n_read = fread(svg_buf, 1U, sizeof(svg_buf) - 1U, fp);
    fclose(fp);
    svg_buf[n_read] = '\0';
    return bloom_svg_compile_text(svg_buf, out_nodes, max_nodes,
                                  out_glyphs, max_glyphs, out_glyph_count, out_dom);
}

/* 3. O(N) Flexbox / Stack Container Layout & Golden-Ratio Auto-Bento */
void bloom_flex_init64(BloomFlexContainer64 *flex,
                       uint16_t direction,
                       uint16_t justify_mode,
                       uint16_t align_items,
                       int32_t origin_x_q16,
                       int32_t origin_y_q16,
                       int32_t half_w_q16,
                       int32_t half_h_q16,
                       int32_t padding_q16,
                       int32_t gap_q16) {
    if (!flex) return;
    memset(flex, 0, sizeof(*flex));
    flex->direction = direction;
    flex->justify_mode = justify_mode;
    flex->align_items = align_items;
    flex->overflow_mode = 1U; /* Scroll clamp */
    flex->origin_x_q16 = origin_x_q16;
    flex->origin_y_q16 = origin_y_q16;
    flex->half_w_q16 = (half_w_q16 > 0) ? half_w_q16 : 16384;
    flex->half_h_q16 = (half_h_q16 > 0) ? half_h_q16 : 8192;
    flex->padding_q16 = (padding_q16 >= 0) ? padding_q16 : 0;
    flex->gap_q16 = (gap_q16 >= 0) ? gap_q16 : 0;
    ui_stamp_mac(flex, 0xF1EC0064ULL, &flex->mac_lo32, &flex->mac_hi32);
}

void bloom_flex_apply64(BloomFlexContainer64 *flex,
                        BloomUiNode64 *nodes,
                        size_t node_count,
                        int32_t scroll_delta_q16) {
    if (!flex || !nodes || node_count == 0U) return;
    size_t n = (node_count > 1024U) ? 1024U : node_count;
    bool is_col = (flex->direction == BLOOM_FLEX_COLUMN);
    int64_t f_hw = (flex->half_w_q16 > 0) ? (int64_t)flex->half_w_q16 : 0LL;
    int64_t f_hh = (flex->half_h_q16 > 0) ? (int64_t)flex->half_h_q16 : 0LL;
    int64_t pad = (flex->padding_q16 > 0) ? (int64_t)flex->padding_q16 : 0LL;
    int64_t gap = (flex->gap_q16 > 0) ? (int64_t)flex->gap_q16 : 0LL;
    if (pad > (int64_t)BLOOM_MAX_Q16 / 2LL) pad = (int64_t)BLOOM_MAX_Q16 / 2LL;
    if (gap > (int64_t)BLOOM_MAX_Q16 / 4LL) gap = (int64_t)BLOOM_MAX_Q16 / 4LL;

    int64_t inner_main = 2LL * (is_col ? f_hh : f_hw) - 2LL * pad;
    int64_t inner_cross = 2LL * (is_col ? f_hw : f_hh) - 2LL * pad;
    if (inner_main < 0LL) inner_main = 0LL;
    if (inner_main > (int64_t)BLOOM_MAX_Q16) inner_main = (int64_t)BLOOM_MAX_Q16;
    if (inner_cross < 0LL) inner_cross = 0LL;
    if (inner_cross > (int64_t)BLOOM_MAX_Q16) inner_cross = (int64_t)BLOOM_MAX_Q16;

    int64_t total_items = 0LL;
    for (size_t i = 0U; i < n; ++i) {
        int64_t ih = is_col ? (int64_t)nodes[i].half_h_q16 : (int64_t)nodes[i].half_w_q16;
        if (ih < 0LL) ih = 0LL;
        if (ih > (int64_t)BLOOM_MAX_Q16 / 4LL) ih = (int64_t)BLOOM_MAX_Q16 / 4LL;
        total_items += 2LL * ih;
        if (total_items > (int64_t)BLOOM_MAX_Q16) total_items = (int64_t)BLOOM_MAX_Q16;
    }
    int64_t total_gaps = (n > 1U) ? ((int64_t)(n - 1U) * gap) : 0LL;
    if (total_gaps > (int64_t)BLOOM_MAX_Q16) total_gaps = (int64_t)BLOOM_MAX_Q16;
    int64_t content_ext64 = total_items + total_gaps;
    if (content_ext64 > (int64_t)BLOOM_MAX_Q16) content_ext64 = (int64_t)BLOOM_MAX_Q16;

    int32_t content_ext = (int32_t)content_ext64;
    flex->content_extent_q16 = content_ext;
    int64_t max_scr64 = (content_ext64 > inner_main) ? (content_ext64 - inner_main) : 0LL;
    flex->max_scroll_q16 = ui_clamp64_to_32(max_scr64, 0, BLOOM_MAX_Q16);
    flex->scroll_offset_q16 = ui_clamp64_to_32((int64_t)flex->scroll_offset_q16 + (int64_t)scroll_delta_q16,
                                               0, flex->max_scroll_q16);

    int64_t main_min = (is_col ? ((int64_t)flex->origin_y_q16 - f_hh) : ((int64_t)flex->origin_x_q16 - f_hw)) + pad;
    int64_t free_space = (inner_main > content_ext64) ? (inner_main - content_ext64) : 0LL;
    int64_t step_gap = gap;
    int64_t cursor = main_min - (int64_t)flex->scroll_offset_q16;

    if (flex->justify_mode == BLOOM_JUSTIFY_CENTER) {
        cursor += free_space / 2LL;
    } else if (flex->justify_mode == BLOOM_JUSTIFY_END) {
        cursor += free_space;
    } else if (flex->justify_mode == BLOOM_JUSTIFY_SPACE_BETWEEN && n > 1U && free_space > 0LL) {
        step_gap = gap + free_space / (int64_t)(n - 1U);
    }

    uint32_t mask_lo = 0U, mask_hi = 0U;
    for (size_t i = 0U; i < n; ++i) {
        int64_t item_half = is_col ? (int64_t)nodes[i].half_h_q16 : (int64_t)nodes[i].half_w_q16;
        if (item_half < 0LL) item_half = 0LL;
        if (item_half > (int64_t)BLOOM_MAX_Q16 / 4LL) item_half = (int64_t)BLOOM_MAX_Q16 / 4LL;
        int64_t cross_half = is_col ? (int64_t)nodes[i].half_w_q16 : (int64_t)nodes[i].half_h_q16;
        if (cross_half < 0LL) cross_half = 0LL;
        if (cross_half > (int64_t)BLOOM_MAX_Q16 / 2LL) cross_half = (int64_t)BLOOM_MAX_Q16 / 2LL;

        if (flex->align_items == BLOOM_ALIGN_STRETCH) {
            cross_half = inner_cross / 2LL;
            if (is_col) nodes[i].half_w_q16 = ui_clamp64_to_32(cross_half, 0, BLOOM_MAX_Q16);
            else nodes[i].half_h_q16 = ui_clamp64_to_32(cross_half, 0, BLOOM_MAX_Q16);
        }
        int64_t main_center = cursor + item_half;
        int64_t cross_origin = is_col ? (int64_t)flex->origin_x_q16 : (int64_t)flex->origin_y_q16;
        int64_t cross_center = cross_origin;
        if (flex->align_items == BLOOM_ALIGN_START) {
            cross_center = cross_origin - (inner_cross / 2LL) + cross_half;
        } else if (flex->align_items == BLOOM_ALIGN_END) {
            cross_center = cross_origin + (inner_cross / 2LL) - cross_half;
        }

        int32_t mc32 = ui_clamp64_to_32(main_center, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
        int32_t cc32 = ui_clamp64_to_32(cross_center, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
        if (is_col) {
            nodes[i].offset_x_q16 = cc32;
            nodes[i].offset_y_q16 = mc32;
        } else {
            nodes[i].offset_x_q16 = mc32;
            nodes[i].offset_y_q16 = cc32;
        }
        if (nodes[i].scale_jet_v_q16 <= 0) {
            nodes[i].scale_jet_v_q16 = BLOOM_Q16_ONE;
        }
        bloom_ui_compute_wave64_mask(nodes[i].offset_x_q16, nodes[i].offset_y_q16,
                                     nodes[i].half_w_q16, nodes[i].half_h_q16,
                                     &nodes[i].tile_mask_lo32, &nodes[i].tile_mask_hi32);
        mask_lo |= nodes[i].tile_mask_lo32;
        mask_hi |= nodes[i].tile_mask_hi32;
        cursor = ui_clamp64_to_32(cursor + 2LL * item_half + step_gap, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
    }

    flex->child_count = (uint32_t)n;
    flex->tile_mask_lo32 = mask_lo;
    flex->tile_mask_hi32 = mask_hi;
    ui_stamp_mac(flex, 0xF1EC0064ULL, &flex->mac_lo32, &flex->mac_hi32);
}

uint32_t bloom_ui_eval_optical_state(int32_t val_q16,
                                     int32_t lo_exit_q16,
                                     int32_t hi_enter_q16,
                                     uint32_t prev_state) {
    if (val_q16 >= hi_enter_q16) return BLOOM_OPTICAL_TRIP;
    if (val_q16 <= lo_exit_q16) return BLOOM_OPTICAL_SAFE;
    return (prev_state == BLOOM_OPTICAL_TRIP) ? BLOOM_OPTICAL_TRIP : BLOOM_OPTICAL_HYSTERESIS;
}

/* Iterative Golden-Ratio (40492 / 65536 ~= 0.6178) Disjoint Auto-Bento Synthesizer */
static void ui_bento_split_iter(BloomUiNode64 *out_nodes,
                                size_t count,
                                int32_t x0,
                                int32_t y0,
                                int32_t x1,
                                int32_t y1,
                                int32_t gap_q16,
                                bool split_x) {
    for (size_t idx = 0U; idx < count; ++idx) {
        BloomUiNode64 *nd = &out_nodes[idx];
        if (idx + 1U == count) {
            nd->offset_x_q16 = (int32_t)(((int64_t)x0 + (int64_t)x1) / 2LL);
            nd->offset_y_q16 = (int32_t)(((int64_t)y0 + (int64_t)y1) / 2LL);
            nd->half_w_q16 = ui_clamp64_to_32(((int64_t)x1 - (int64_t)x0 - (int64_t)gap_q16) / 2LL, 512, BLOOM_Q16_ONE);
            nd->half_h_q16 = ui_clamp64_to_32(((int64_t)y1 - (int64_t)y0 - (int64_t)gap_q16) / 2LL, 512, BLOOM_Q16_ONE);
            return;
        }
        if (split_x) {
            int32_t cut = x0 + (int32_t)((((int64_t)x1 - (int64_t)x0) * 40492LL) >> 16);
            nd->offset_x_q16 = (int32_t)(((int64_t)x0 + (int64_t)cut) / 2LL);
            nd->offset_y_q16 = (int32_t)(((int64_t)y0 + (int64_t)y1) / 2LL);
            nd->half_w_q16 = ui_clamp64_to_32(((int64_t)cut - (int64_t)x0 - (int64_t)gap_q16) / 2LL, 512, BLOOM_Q16_ONE);
            nd->half_h_q16 = ui_clamp64_to_32(((int64_t)y1 - (int64_t)y0 - (int64_t)gap_q16) / 2LL, 512, BLOOM_Q16_ONE);
            x0 = cut;
            split_x = false;
        } else {
            int32_t cut = y0 + (int32_t)((((int64_t)y1 - (int64_t)y0) * 40492LL) >> 16);
            nd->offset_x_q16 = (int32_t)(((int64_t)x0 + (int64_t)x1) / 2LL);
            nd->offset_y_q16 = (int32_t)(((int64_t)y0 + (int64_t)cut) / 2LL);
            nd->half_w_q16 = ui_clamp64_to_32(((int64_t)x1 - (int64_t)x0 - (int64_t)gap_q16) / 2LL, 512, BLOOM_Q16_ONE);
            nd->half_h_q16 = ui_clamp64_to_32(((int64_t)cut - (int64_t)y0 - (int64_t)gap_q16) / 2LL, 512, BLOOM_Q16_ONE);
            y0 = cut;
            split_x = true;
        }
    }
}

size_t bloom_ui_synthesize_bento64(BloomIntent64 *intents,
                                   size_t intent_count,
                                   const BloomRegBank64 *reg_bank,
                                   BloomUiNode64 *out_nodes,
                                   size_t max_nodes,
                                   BloomDomState64 *out_dom) {
    if (!intents || intent_count == 0U || !out_nodes || max_nodes == 0U) return 0U;
    size_t n = (intent_count > max_nodes) ? max_nodes : intent_count;
    if (n > 1024U) n = 1024U;
    for (size_t i = 0U; i < n; ++i) {
        memset(&out_nodes[i], 0, sizeof(BloomUiNode64));
    }
    /* Synthesize into bottom HUD strip [-58000..+58000] x [+34000..+60000] */
    ui_bento_split_iter(out_nodes, n, -58000, 34000, 58000, 60000, 1024, true);

    uint32_t dom_lo = 0U, dom_hi = 0U;
    for (size_t i = 0U; i < n; ++i) {
        int32_t val = intents[i].current_val_q16;
        if (reg_bank && intents[i].bind_channel < BLOOM_REG_CHANNELS) {
            val = reg_bank->channels_q16[intents[i].bind_channel];
            intents[i].current_val_q16 = val;
        }
        uint32_t opt = bloom_ui_eval_optical_state(val, intents[i].lo_exit_q16,
                                                   intents[i].hi_enter_q16, intents[i].optical_state);
        intents[i].optical_state = opt;
        ui_stamp_mac(&intents[i], 0xBE470064ULL, &intents[i].mac_lo32, &intents[i].mac_hi32);

        uint32_t rgb = (opt == BLOOM_OPTICAL_SAFE) ? 0x20D8FFU
                     : ((opt == BLOOM_OPTICAL_HYSTERESIS) ? 0xFFB020U : 0xFF2848U);
        BloomUiNode64 *nd = &out_nodes[i];
        nd->node_id = intents[i].intent_id ? intents[i].intent_id : (uint32_t)(i + 1U);
        nd->prim_type = intents[i].prim_type ? intents[i].prim_type : BLOOM_UI_PRIM_RECT_GLASS;
        nd->bind_channel = intents[i].bind_channel;
        nd->action_opcode = intents[i].action_opcode;
        nd->corner_radius_q16 = 1536;
        nd->scale_jet_v_q16 = BLOOM_Q16_ONE;
        nd->bound_value_q16 = ui_clamp32(val, 0, BLOOM_Q16_ONE);
        nd->style_and_state = (intents[i].glass_mode & 0xFU) | BLOOM_UI_STATE_GLASS_BIT | (rgb << 8U);
        bloom_ui_compute_wave64_mask(nd->offset_x_q16, nd->offset_y_q16,
                                     nd->half_w_q16, nd->half_h_q16,
                                     &nd->tile_mask_lo32, &nd->tile_mask_hi32);
        dom_lo |= nd->tile_mask_lo32;
        dom_hi |= nd->tile_mask_hi32;
    }

    if (out_dom) {
        out_dom->compiled_node_count = (uint32_t)n;
        out_dom->dom_mask_lo32 = dom_lo;
        out_dom->dom_mask_hi32 = dom_hi;
        ui_stamp_mac(out_dom, 0xD0A00064ULL, &out_dom->mac_lo32, &out_dom->mac_hi32);
    }
    return n;
}

/* 4. Zero-VDOM Register Bank & Beer-Lambert Glass Transmittance */
void bloom_reg_bank_init64(BloomRegBank64 *bank) {
    if (!bank) return;
    memset(bank, 0, sizeof(*bank));
    ui_stamp_mac(bank, 0x4E6B0064ULL, &bank->mac_lo32, &bank->mac_hi32);
}

void bloom_reg_bank_set_q16(BloomRegBank64 *bank, uint32_t channel, int32_t val_q16) {
    if (!bank || channel >= BLOOM_REG_CHANNELS) return;
    if (bank->channels_q16[channel] != val_q16) {
        bank->channels_q16[channel] = val_q16;
        bank->dirty_mask |= (1U << channel);
        bank->epoch++;
        ui_stamp_mac(bank, 0x4E6B0064ULL, &bank->mac_lo32, &bank->mac_hi32);
    }
}

int32_t bloom_reg_bank_get_q16(const BloomRegBank64 *bank, uint32_t channel) {
    if (!bank || channel >= BLOOM_REG_CHANNELS) return 0;
    return bank->channels_q16[channel];
}

void bloom_ui_bind_reg_bank64(BloomUiNode64 *nodes,
                              size_t node_count,
                              const BloomRegBank64 *bank) {
    if (!nodes || !bank) return;
    for (size_t i = 0U; i < node_count; ++i) {
        uint32_t ch = nodes[i].bind_channel;
        if (ch < BLOOM_REG_CHANNELS) {
            nodes[i].bound_value_q16 = bank->channels_q16[ch];
        }
    }
}

int32_t bloom_ui_eval_glass_transmittance_q16(uint32_t fill_rgb24, int32_t bevel_q16) {
    /* Crystal-floor [4096, 65536] Beer-Lambert optical transmittance so glass never turns dead black */
    int32_t r = (int32_t)((fill_rgb24 >> 16U) & 0xFFU);
    int32_t g = (int32_t)((fill_rgb24 >> 8U) & 0xFFU);
    int32_t b = (int32_t)(fill_rgb24 & 0xFFU);
    int32_t luma_q16 = ((r * 54 + g * 183 + b * 19) * 256) / 255;
    int32_t absorb = BLOOM_Q16_ONE - ui_clamp32(luma_q16, 0, BLOOM_Q16_ONE);
    int32_t thick = ui_clamp32(bevel_q16, 0, 2 * BLOOM_Q16_ONE);
    int64_t atten = ((int64_t)absorb * (int64_t)thick) >> 17;
    return ui_clamp32(BLOOM_Q16_ONE - (int32_t)atten, 4096, BLOOM_Q16_ONE);
}

/* 5. Wave64 Hit-Testing, Pointer Capture Scrubbing Arena & FLOW v6.0.0 Tactile Springs */
uint32_t bloom_ui_step_pointer_arena(BloomEngineArena *arena,
                                     int32_t cursor_x_q16,
                                     int32_t cursor_y_q16,
                                     int32_t drag_dx_q16,
                                     bool mouse_down,
                                     bool mouse_just_clicked) {
    if (!arena) return BLOOM_ACTION_NONE;
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    BloomDomState64 *dom = &arena->map.dom;
    BloomRegBank64 *bank = &arena->map.reg_bank;
    BloomUiNode64 *nodes = arena->map.ui_nodes;

    uint32_t count = (hdr->ui_count > BLOOM_MAX_UI_NODES) ? BLOOM_MAX_UI_NODES : hdr->ui_count;
    dom->cursor_norm_x_q16 = cursor_x_q16;
    dom->cursor_norm_y_q16 = cursor_y_q16;

    uint32_t fired_action = BLOOM_ACTION_NONE;

    /* Release pointer capture lock when mouse button is released */
    if (!mouse_down) {
        dom->captured_node_id = 0U;
        dom->active_node_id = 0U;
    }

    /* If a slider/scrub node holds Pointer Capture, scrub continuously even outside AABB! */
    if (mouse_down && dom->captured_node_id != 0U) {
        for (uint32_t i = 0U; i < count; ++i) {
            if (nodes[i].node_id == dom->captured_node_id) {
                int64_t hw64 = (nodes[i].half_w_q16 > 1024)
                    ? ui_clamp64_to_32((int64_t)nodes[i].half_w_q16, 1024, 16 * BLOOM_Q16_ONE)
                    : 16384LL;
                int64_t span = 2LL * hw64;
                int64_t delta_val = (((int64_t)drag_dx_q16) * (int64_t)BLOOM_Q16_ONE) / span;
                int32_t next_v = ui_clamp64_to_32((int64_t)nodes[i].bound_value_q16 + delta_val, 0, BLOOM_Q16_ONE);
                nodes[i].bound_value_q16 = next_v;
                nodes[i].style_and_state |= (BLOOM_UI_STATE_ACTIVE_BIT | BLOOM_UI_STATE_CAPTURED_BIT);
                if (nodes[i].bind_channel < BLOOM_REG_CHANNELS) {
                    bloom_reg_bank_set_q16(bank, nodes[i].bind_channel, next_v);
                }
                if ((nodes[i].action_opcode & 0xFFFFU) == BLOOM_ACTION_SCRUB_XRAY) {
                    /* Map [0..65536] -> [-2.0 .. +2.0] world X-Ray cutaway plane */
                    hdr->xray_plane_q16 = (next_v - 32768) * 4;
                    hdr->flags |= BLOOM_FLAG_XRAY_ENABLED;
                }
                fired_action = nodes[i].action_opcode & 0xFFFFU;
                dom->last_action_opcode = fired_action;
                break;
            }
        }
    }

    uint32_t cbit = bloom_ui_cursor_to_wave64_bit(cursor_x_q16, cursor_y_q16);
    uint32_t hovered_id = 0U;

    /* Fast DOM-wide Wave64 early reject */
    bool in_dom_tile = bloom_wave64_test_u32x2(dom->dom_mask_lo32, dom->dom_mask_hi32, cbit);

    for (uint32_t i = 0U; i < count; ++i) {
        BloomUiNode64 *nd = &nodes[i];
        nd->style_and_state &= ~(BLOOM_UI_STATE_HOVER_BIT |
                                 (mouse_down ? 0U : (BLOOM_UI_STATE_ACTIVE_BIT | BLOOM_UI_STATE_CAPTURED_BIT)));

        bool hit = false;
        if (!in_dom_tile || !bloom_wave64_test_u32x2(nd->tile_mask_lo32, nd->tile_mask_hi32, cbit)) {
            dom->wave64_cull_skips++;
        } else {
            int64_t dx64 = (int64_t)cursor_x_q16 - (int64_t)nd->offset_x_q16;
            int64_t dy64 = (int64_t)cursor_y_q16 - (int64_t)nd->offset_y_q16;
            if (dx64 < 0) dx64 = -dx64;
            if (dy64 < 0) dy64 = -dy64;
            hit = (dx64 <= (int64_t)nd->half_w_q16 && dy64 <= (int64_t)nd->half_h_q16);
        }

        if (hit && hovered_id == 0U) {
            hovered_id = nd->node_id;
            nd->style_and_state |= BLOOM_UI_STATE_HOVER_BIT;
            if (mouse_just_clicked) {
                dom->active_node_id = nd->node_id;
                dom->click_dispatch_count++;
                nd->style_and_state |= BLOOM_UI_STATE_ACTIVE_BIT;
                if (nd->prim_type == BLOOM_UI_PRIM_SLIDER_BAR ||
                    (nd->action_opcode & 0xFFFFU) == BLOOM_ACTION_SCRUB_XRAY) {
                    dom->captured_node_id = nd->node_id;
                    nd->style_and_state |= BLOOM_UI_STATE_CAPTURED_BIT;
                }
                fired_action = nd->action_opcode & 0xFFFFU;
                dom->last_action_opcode = fired_action;
            }
        }

        /* Drive UI Node's 2-Jet tactile spring via FLOW v6.0.0 flow_step_atlas!
           Target equilibrium: normal=65536 (1.0x), hovered=72089 (1.10x), active=60293 (0.92x) */
        int64_t target_scale = BLOOM_Q16_ONE;
        if ((nd->style_and_state & BLOOM_UI_STATE_ACTIVE_BIT) != 0U) {
            target_scale = 60293;
        } else if ((nd->style_and_state & BLOOM_UI_STATE_HOVER_BIT) != 0U) {
            target_scale = 72089;
        }

        uint64_t *st = arena->map.flow_states[BLOOM_FLOW_CH_UI_NODE0 + i];
        const uint64_t (*charts)[8] = (const uint64_t (*)[8])arena->map.flow_charts;
        flow_step_atlas(st, st, charts, BLOOM_FLOW_CHART_MASK, target_scale, 0U);

        nd->scale_jet_v_q16 = ui_clamp32((int32_t)(int64_t)st[3], 49152, 85196);
        nd->scale_jet_d1_q16 = (int32_t)(st[4] & 0x7FFFFFFFULL);
    }

    dom->hovered_node_id = hovered_id;
    ui_stamp_mac(dom, 0xD0A00064ULL, &dom->mac_lo32, &dom->mac_hi32);
    return fired_action;
}

/* 6. Single-Pass 256-Byte ASCII Jump-Table DFA -> Zero-Font-Atlas Analytic SDF LaTeX Compiler */
#define DFA_CLS_OTHER  0U
#define DFA_CLS_SPACE  1U
#define DFA_CLS_DOLLAR 2U
#define DFA_CLS_BSLASH 3U
#define DFA_CLS_LBRACE 4U
#define DFA_CLS_RBRACE 5U
#define DFA_CLS_CARET  6U
#define DFA_CLS_USCORE 7U
#define DFA_CLS_ALPHA  8U
#define DFA_CLS_DIGIT  9U

static uint8_t ui_dfa_class(uint8_t ch) {
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') return DFA_CLS_SPACE;
    if (ch == '$')  return DFA_CLS_DOLLAR;
    if (ch == '\\') return DFA_CLS_BSLASH;
    if (ch == '{')  return DFA_CLS_LBRACE;
    if (ch == '}')  return DFA_CLS_RBRACE;
    if (ch == '^')  return DFA_CLS_CARET;
    if (ch == '_')  return DFA_CLS_USCORE;
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')) return DFA_CLS_ALPHA;
    if (ch >= '0' && ch <= '9') return DFA_CLS_DIGIT;
    return DFA_CLS_OTHER;
}

static size_t ui_skip_brace_group(const char *s, size_t idx, size_t len, size_t *out_inner_start, size_t *out_inner_end) {
    while (idx < len && ui_dfa_class((uint8_t)s[idx]) == DFA_CLS_SPACE) ++idx;
    if (idx < len && s[idx] == '{') {
        size_t start = idx + 1U;
        uint32_t depth = 1U;
        ++idx;
        while (idx < len && depth > 0U) {
            if (s[idx] == '{') ++depth;
            else if (s[idx] == '}') --depth;
            ++idx;
        }
        *out_inner_start = start;
        *out_inner_end = (idx > 0U) ? (idx - 1U) : start;
        return idx;
    }
    *out_inner_start = idx;
    *out_inner_end = (idx < len) ? (idx + 1U) : idx;
    return *out_inner_end;
}

static void ui_emit_latex_box(BloomLatexBox64 *out_boxes,
                              size_t *inout_cnt,
                              size_t max_boxes,
                              uint32_t kind,
                              uint32_t cp,
                              uint32_t rgb,
                              int32_t cx,
                              int32_t cy,
                              int32_t hw,
                              int32_t hh,
                              int32_t stroke) {
    if (*inout_cnt >= max_boxes) return;
    BloomLatexBox64 *b = &out_boxes[*inout_cnt];
    memset(b, 0, sizeof(*b));
    b->box_id = (uint32_t)(*inout_cnt + 1U);
    b->box_kind = kind;
    b->codepoint_or_op = cp;
    b->tint_rgb24 = rgb;
    b->x_q16 = cx;
    b->y_q16 = cy;
    b->half_w_q16 = (hw > 128) ? hw : 128;
    b->half_h_q16 = (hh > 128) ? hh : 128;
    b->stroke_q16 = (stroke > 64) ? stroke : 64;
    bloom_ui_compute_wave64_mask(cx, cy, b->half_w_q16, b->half_h_q16,
                                 &b->tile_mask_lo32, &b->tile_mask_hi32);
    ui_stamp_mac(b, 0x1A7EC064ULL, &b->mac_lo32, &b->mac_hi32);
    (*inout_cnt)++;
}

static int32_t ui_compile_latex_span(const char *src,
                                     size_t start,
                                     size_t end,
                                     int32_t pen_x_in,
                                     int32_t base_y_in,
                                     int32_t em_in,
                                     uint32_t rgb,
                                     uint32_t depth,
                                     BloomLatexBox64 *out_boxes,
                                     size_t *inout_cnt,
                                     size_t max_boxes) {
    if (depth > 16U) return pen_x_in;
    int64_t pen_x = (int64_t)pen_x_in;
    int64_t base_y = (int64_t)ui_clamp32(base_y_in, -2097152, 2097152);
    int64_t em = (int64_t)ui_clamp32(em_in, 256, 32768);
    int32_t stroke = (int32_t)((em >> 3) > 128 ? (em >> 3) : 128);
    size_t i = start;
    while (i < end && *inout_cnt < max_boxes) {
        uint8_t cls = ui_dfa_class((uint8_t)src[i]);
        if (cls == DFA_CLS_SPACE || cls == DFA_CLS_DOLLAR) {
            ++i;
            continue;
        }
        if (cls == DFA_CLS_BSLASH) {
            ++i;
            size_t m0 = i;
            while (i < end && ui_dfa_class((uint8_t)src[i]) == DFA_CLS_ALPHA) ++i;
            size_t mlen = i - m0;
            if (mlen == 4U && strncmp(src + m0, "frac", 4U) == 0) {
                size_t ns = 0U, ne = 0U, ds = 0U, de = 0U;
                i = ui_skip_brace_group(src, i, end, &ns, &ne);
                i = ui_skip_brace_group(src, i, end, &ds, &de);
                int32_t sub_em = (int32_t)((em * 3LL) >> 2);
                int32_t sub_x = ui_clamp64_to_32(pen_x + (em >> 3), -BLOOM_MAX_Q16 / 2, BLOOM_MAX_Q16 / 2);
                int32_t num_end = ui_compile_latex_span(src, ns, ne, sub_x,
                                                        ui_clamp64_to_32(base_y - (em >> 1), -2097152, 2097152),
                                                        sub_em, rgb, depth + 1U, out_boxes, inout_cnt, max_boxes);
                int32_t den_end = ui_compile_latex_span(src, ds, de, sub_x,
                                                        ui_clamp64_to_32(base_y + (em >> 1), -2097152, 2097152),
                                                        sub_em, rgb, depth + 1U, out_boxes, inout_cnt, max_boxes);
                int64_t nw = (int64_t)num_end - (int64_t)sub_x;
                int64_t dw = (int64_t)den_end - (int64_t)sub_x;
                int64_t bar_w = (nw > dw ? nw : dw) + (em >> 2);
                if (bar_w < em) bar_w = em;
                if (bar_w > 4194304LL) bar_w = 4194304LL;
                ui_emit_latex_box(out_boxes, inout_cnt, max_boxes, BLOOM_LATEX_FRAC_BAR, '-',
                                  rgb, ui_clamp64_to_32(pen_x + bar_w / 2LL, -BLOOM_MAX_Q16, BLOOM_MAX_Q16),
                                  (int32_t)base_y, (int32_t)(bar_w / 2LL), stroke, stroke);
                pen_x = ui_clamp64_to_32(pen_x + bar_w + (em >> 2), -BLOOM_MAX_Q16 / 2, BLOOM_MAX_Q16 / 2);
            } else if (mlen == 4U && strncmp(src + m0, "sqrt", 4U) == 0) {
                size_t rs = 0U, re = 0U;
                i = ui_skip_brace_group(src, i, end, &rs, &re);
                int32_t inner_x = ui_clamp64_to_32(pen_x + (em >> 1), -BLOOM_MAX_Q16 / 2, BLOOM_MAX_Q16 / 2);
                int32_t inner_end = ui_compile_latex_span(src, rs, re, inner_x, (int32_t)base_y,
                                                          (int32_t)((em * 7LL) >> 3), rgb, depth + 1U,
                                                          out_boxes, inout_cnt, max_boxes);
                int64_t iw = (int64_t)inner_end - (int64_t)inner_x;
                if (iw < 0LL) iw = 0LL;
                int64_t tot_w = (em >> 1) + iw + (em >> 3);
                if (tot_w > 4194304LL) tot_w = 4194304LL;
                ui_emit_latex_box(out_boxes, inout_cnt, max_boxes, BLOOM_LATEX_SQRT_RAD, 'V',
                                  rgb, ui_clamp64_to_32(pen_x + tot_w / 2LL, -BLOOM_MAX_Q16, BLOOM_MAX_Q16),
                                  (int32_t)base_y, (int32_t)(tot_w / 2LL), (int32_t)((em * 3LL) >> 2), stroke);
                pen_x = ui_clamp64_to_32(pen_x + tot_w + (em >> 3), -BLOOM_MAX_Q16 / 2, BLOOM_MAX_Q16 / 2);
            } else if (mlen == 3U && strncmp(src + m0, "int", 3U) == 0) {
                ui_emit_latex_box(out_boxes, inout_cnt, max_boxes, BLOOM_LATEX_INT_OP, 'I',
                                  rgb, ui_clamp64_to_32(pen_x + (em >> 1), -BLOOM_MAX_Q16, BLOOM_MAX_Q16),
                                  (int32_t)base_y, (int32_t)(em >> 2), (int32_t)em, stroke);
                pen_x = ui_clamp64_to_32(pen_x + em, -BLOOM_MAX_Q16 / 2, BLOOM_MAX_Q16 / 2);
            } else if (mlen == 3U && strncmp(src + m0, "sum", 3U) == 0) {
                ui_emit_latex_box(out_boxes, inout_cnt, max_boxes, BLOOM_LATEX_SUM_OP, 'S',
                                  rgb, ui_clamp64_to_32(pen_x + (em >> 1), -BLOOM_MAX_Q16, BLOOM_MAX_Q16),
                                  (int32_t)base_y, (int32_t)(em >> 1), (int32_t)((em * 3LL) >> 2), stroke);
                pen_x = ui_clamp64_to_32(pen_x + em + (em >> 2), -BLOOM_MAX_Q16 / 2, BLOOM_MAX_Q16 / 2);
            } else {
                /* Greek / macro symbol (\gamma, \beta, \eta, etc.) */
                uint32_t ch = (mlen > 0U) ? (uint32_t)(uint8_t)src[m0] : '?';
                ui_emit_latex_box(out_boxes, inout_cnt, max_boxes, BLOOM_LATEX_SYM_GLYPH, ch,
                                  rgb, ui_clamp64_to_32(pen_x + (em >> 1), -BLOOM_MAX_Q16, BLOOM_MAX_Q16),
                                  (int32_t)base_y, (int32_t)((em * 3LL) >> 3), (int32_t)(em >> 1), stroke);
                pen_x = ui_clamp64_to_32(pen_x + ((em * 3LL) >> 2), -BLOOM_MAX_Q16 / 2, BLOOM_MAX_Q16 / 2);
            }
        } else if (cls == DFA_CLS_CARET || cls == DFA_CLS_USCORE) {
            bool is_sup = (cls == DFA_CLS_CARET);
            ++i;
            size_t gs = 0U, ge = 0U;
            i = ui_skip_brace_group(src, i, end, &gs, &ge);
            uint32_t ch = (gs < ge) ? (uint32_t)(uint8_t)src[gs] : '2';
            int32_t sy = ui_clamp64_to_32(is_sup ? (base_y - (em >> 1)) : (base_y + (em >> 1)), -2097152, 2097152);
            ui_emit_latex_box(out_boxes, inout_cnt, max_boxes, BLOOM_LATEX_SUP_SUB, ch,
                              rgb, ui_clamp64_to_32(pen_x + (em >> 2), -BLOOM_MAX_Q16, BLOOM_MAX_Q16),
                              sy, (int32_t)(em >> 2), (int32_t)(em >> 2), stroke);
            pen_x = ui_clamp64_to_32(pen_x + (em >> 1), -BLOOM_MAX_Q16 / 2, BLOOM_MAX_Q16 / 2);
        } else if (cls == DFA_CLS_LBRACE || cls == DFA_CLS_RBRACE) {
            ++i;
        } else {
            uint32_t ch = (uint32_t)(uint8_t)src[i++];
            ui_emit_latex_box(out_boxes, inout_cnt, max_boxes, BLOOM_LATEX_SYM_GLYPH, ch,
                              rgb, ui_clamp64_to_32(pen_x + (em * 3LL) / 8LL, -BLOOM_MAX_Q16, BLOOM_MAX_Q16),
                              (int32_t)base_y, (int32_t)(em >> 2), (int32_t)((em * 3LL) >> 3), stroke);
            pen_x = ui_clamp64_to_32(pen_x + ((em * 3LL) >> 2), -BLOOM_MAX_Q16 / 2, BLOOM_MAX_Q16 / 2);
        }
    }
    return ui_clamp64_to_32(pen_x, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
}

size_t bloom_latex_compile_boxes64(const char *latex_src,
                                   int32_t origin_x_q16,
                                   int32_t origin_y_q16,
                                   int32_t em_size_q16,
                                   uint32_t tint_rgb24,
                                   BloomLatexBox64 *out_boxes,
                                   size_t max_boxes,
                                   int32_t *out_total_width_q16) {
    if (!latex_src || !out_boxes || max_boxes == 0U) return 0U;
    size_t len = strlen(latex_src);
    size_t cnt = 0U;
    int32_t ox = ui_clamp32(origin_x_q16, -262144, 262144);
    int32_t oy = ui_clamp32(origin_y_q16, -262144, 262144);
    int32_t em = ui_clamp32((em_size_q16 > 512) ? em_size_q16 : 2048, 512, 32768);
    int32_t end_x = ui_compile_latex_span(latex_src, 0U, len, ox, oy,
                                          em, tint_rgb24, 0U, out_boxes, &cnt, max_boxes);
    if (out_total_width_q16) *out_total_width_q16 = ui_clamp64_to_32((int64_t)end_x - (int64_t)ox, 0, BLOOM_MAX_Q16);
    return cnt;
}

static int32_t ui_sd_seg_q16(int32_t px, int32_t py, int32_t ax, int32_t ay, int32_t bx, int32_t by) {
    int64_t pax = (int64_t)px - ax, pay = (int64_t)py - ay;
    int64_t bax = (int64_t)bx - ax, bay = (int64_t)by - ay;
    int64_t baba = (bax * bax + bay * bay) >> 16;
    int64_t h = 0;
    if (baba > 0) {
        h = (((pax * bax + pay * bay) >> 16) * 65536LL) / baba;
        if (h < 0) h = 0;
        if (h > BLOOM_Q16_ONE) h = BLOOM_Q16_ONE;
    }
    int64_t dx = pax - ((bax * h) >> 16);
    int64_t dy = pay - ((bay * h) >> 16);
    return bloom_isqrt_u64_to_i32((uint64_t)(dx * dx + dy * dy));
}

int32_t bloom_latex_eval_box_sdf_q16(const BloomLatexBox64 *b, int32_t px_q16, int32_t py_q16) {
    if (!b || b->box_kind == 0U) return BLOOM_MAX_Q16;
    int32_t lx = ui_clamp64_to_32((int64_t)px_q16 - (int64_t)b->x_q16, -16777216, 16777216);
    int32_t ly = ui_clamp64_to_32((int64_t)py_q16 - (int64_t)b->y_q16, -16777216, 16777216);
    int32_t hw = ui_clamp32(b->half_w_q16, 64, 4194304);
    int32_t hh = ui_clamp32(b->half_h_q16, 64, 4194304);
    int64_t stroke = (int64_t)ui_clamp32(b->stroke_q16, 0, 4194304);

    switch (b->box_kind) {
    case BLOOM_LATEX_FRAC_BAR:
        return ui_clamp64_to_32((int64_t)ui_sd_seg_q16(lx, ly, -hw, 0, hw, 0) - stroke,
                                -BLOOM_MAX_Q16, BLOOM_MAX_Q16);

    case BLOOM_LATEX_SQRT_RAD: {
        int32_t d1 = ui_sd_seg_q16(lx, ly, -hw, 0, -hw + (hw >> 1), hh);
        int32_t d2 = ui_sd_seg_q16(lx, ly, -hw + (hw >> 1), hh, 0, -hh);
        int32_t d3 = ui_sd_seg_q16(lx, ly, 0, -hh, hw, -hh);
        int32_t mn = (d1 < d2) ? d1 : d2;
        if (d3 < mn) mn = d3;
        return ui_clamp64_to_32((int64_t)mn - stroke, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
    }

    default: {
        int32_t qx = ui_abs32(lx) - hw;
        int32_t qy = ui_abs32(ly) - hh;
        int32_t ox = (qx > 0) ? qx : 0;
        int32_t oy = (qy > 0) ? qy : 0;
        int32_t out_d = bloom_isqrt_u64_to_i32((uint64_t)((int64_t)ox * ox + (int64_t)oy * oy));
        int32_t in_d = (qx > qy) ? qx : qy;
        if (in_d > 0) in_d = 0;
        int64_t signed_d = (int64_t)out_d + (int64_t)in_d;
        if (signed_d < 0LL) signed_d = -signed_d;
        return ui_clamp64_to_32(signed_d - stroke, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
    }
    }
}

/* 7. Inline UTF-8 Text Input, CJK IME Preedit & HiDPI Host Window */
static uint32_t ui_utf8_lead_need(uint8_t b0) {
    if (b0 >= 0x01U && b0 <= 0x7FU) return 1U;
    if (b0 >= 0xC2U && b0 <= 0xDFU) return 2U;
    if (b0 >= 0xE0U && b0 <= 0xEFU) return 3U;
    if (b0 >= 0xF0U && b0 <= 0xF4U) return 4U;
    return 0U;
}

static bool ui_utf8_valid_second(uint8_t b0, uint8_t b1) {
    if ((b1 & 0xC0U) != 0x80U) return false;
    if (b0 == 0xE0U) return b1 >= 0xA0U;
    if (b0 == 0xEDU) return b1 <= 0x9FU;
    if (b0 == 0xF0U) return b1 >= 0x90U;
    if (b0 == 0xF4U) return b1 <= 0x8FU;
    return true;
}

/* Returns byte length (1..4) of valid RFC 3629 UTF-8 codepoint at s[0..rem-1], or 0 if invalid */
static size_t ui_utf8_codepoint_len(const uint8_t *s, size_t rem) {
    if (!s || rem == 0U) return 0U;
    uint32_t need = ui_utf8_lead_need(s[0]);
    if (need == 0U || (size_t)need > rem) return 0U;
    if (need == 1U) return 1U;
    if (!ui_utf8_valid_second(s[0], s[1])) return 0U;
    for (uint32_t k = 2U; k < need; ++k) {
        if ((s[k] & 0xC0U) != 0x80U) return 0U;
    }
    return (size_t)need;
}

static void ui_input_clear_staging(BloomTextInput64 *input) {
    uint8_t blen = (input->byte_len > BLOOM_TEXT_INPUT_CAP) ? BLOOM_TEXT_INPUT_CAP : input->byte_len;
    for (size_t k = (size_t)blen; k < 40U; ++k) {
        input->utf8_buf[k] = '\0';
    }
}

/* Ensures utf8_buf[0..byte_len-1] is 100% valid RFC 3629 UTF-8 and cursor_pos is on a boundary */
static void ui_input_sanitize(BloomTextInput64 *input, bool preserve_staging) {
    uint8_t raw_len = (input->byte_len > BLOOM_TEXT_INPUT_CAP) ? BLOOM_TEXT_INPUT_CAP : input->byte_len;
    size_t valid_len = 0U;
    while (valid_len < (size_t)raw_len) {
        size_t cp = ui_utf8_codepoint_len((const uint8_t *)input->utf8_buf + valid_len,
                                          (size_t)raw_len - valid_len);
        if (cp == 0U) break;
        valid_len += cp;
    }
    input->byte_len = (uint8_t)valid_len;
    input->utf8_buf[valid_len] = '\0';

    uint8_t cpos = (input->cursor_pos > input->byte_len) ? input->byte_len : input->cursor_pos;
    while (cpos > 0U && cpos < input->byte_len &&
           (((uint8_t)input->utf8_buf[cpos] & 0xC0U) == 0x80U)) {
        --cpos;
    }
    input->cursor_pos = cpos;
    if (input->anchor_pos > input->byte_len) input->anchor_pos = cpos;

    if (!preserve_staging || valid_len != (size_t)raw_len) {
        ui_input_clear_staging(input);
    }
}

void bloom_text_input_init64(BloomTextInput64 *input, uint32_t bound_node_id, uint32_t action_id) {
    if (!input) return;
    memset(input, 0, sizeof(*input));
    input->bound_node_id = bound_node_id;
    input->submit_action_id = action_id;
    input->focused = 1U;
    ui_stamp_mac(input, 0x7EC70064ULL, &input->hash_lo32, &input->hash_hi32);
}

static bool ui_input_splice_utf8(BloomTextInput64 *input, const uint8_t *seq, uint8_t seq_len) {
    uint8_t blen = input->byte_len;
    if (seq_len == 0U || (uint32_t)blen + (uint32_t)seq_len > BLOOM_TEXT_INPUT_CAP) {
        return false;
    }
    uint8_t pos = input->cursor_pos;
    if (pos > blen) pos = blen;
    ui_input_clear_staging(input);
    for (uint8_t i = blen; i > pos; --i) {
        input->utf8_buf[i + seq_len - 1U] = input->utf8_buf[i - 1U];
    }
    for (uint8_t k = 0U; k < seq_len; ++k) {
        input->utf8_buf[pos + k] = (char)seq[k];
    }
    input->byte_len = (uint8_t)(blen + seq_len);
    input->cursor_pos = (uint8_t)(pos + seq_len);
    input->anchor_pos = input->cursor_pos;
    input->utf8_buf[input->byte_len] = '\0';
    input->edit_revision++;
    ui_stamp_mac(input, 0x7EC70064ULL, &input->hash_lo32, &input->hash_hi32);
    return true;
}

bool bloom_text_input_insert_char64(BloomTextInput64 *input, char ch) {
    if (!input || ch == '\0') return false;
    uint8_t u = (uint8_t)ch;
    ui_input_sanitize(input, (u & 0xC0U) == 0x80U);

    if (u <= 0x7FU) {
        ui_input_clear_staging(input);
        return ui_input_splice_utf8(input, &u, 1U);
    }

    /* Multi-byte UTF-8 continuation byte: combine with staged lead bytes in utf8_buf[36..39] */
    if ((u & 0xC0U) == 0x80U) {
        if (input->byte_len >= 38U) {
            ui_input_clear_staging(input);
            ui_stamp_mac(input, 0x7EC70064ULL, &input->hash_lo32, &input->hash_hi32);
            return false;
        }
        uint8_t tag = (uint8_t)input->utf8_buf[39];
        uint32_t need = (uint32_t)(tag >> 4U);
        uint32_t got = (uint32_t)(tag & 0xFU);
        if (need < 2U || need > 4U || got < 1U || got >= need ||
            (uint32_t)input->byte_len + need > BLOOM_TEXT_INPUT_CAP) {
            ui_input_clear_staging(input);
            ui_stamp_mac(input, 0x7EC70064ULL, &input->hash_lo32, &input->hash_hi32);
            return false;
        }
        size_t stage_base = 40U - (size_t)need;
        uint8_t b0 = (uint8_t)input->utf8_buf[stage_base];
        if (ui_utf8_lead_need(b0) != need || (got == 1U && !ui_utf8_valid_second(b0, u))) {
            ui_input_clear_staging(input);
            ui_stamp_mac(input, 0x7EC70064ULL, &input->hash_lo32, &input->hash_hi32);
            return false;
        }
        if (got + 1U < need) {
            input->utf8_buf[stage_base + got] = (char)u;
            input->utf8_buf[39] = (char)((need << 4U) | (got + 1U));
            ui_stamp_mac(input, 0x7EC70064ULL, &input->hash_lo32, &input->hash_hi32);
            return true;
        }
        uint8_t seq[4] = { 0U, 0U, 0U, 0U };
        for (uint32_t k = 0U; k < got; ++k) {
            seq[k] = (uint8_t)input->utf8_buf[stage_base + k];
        }
        seq[got] = u;
        if (ui_utf8_codepoint_len(seq, need) != (size_t)need) {
            ui_input_clear_staging(input);
            ui_stamp_mac(input, 0x7EC70064ULL, &input->hash_lo32, &input->hash_hi32);
            return false;
        }
        return ui_input_splice_utf8(input, seq, (uint8_t)need);
    }

    /* Multi-byte UTF-8 lead byte (2..4 bytes): reject immediately if full codepoint exceeds 39B */
    ui_input_clear_staging(input);
    uint32_t need = ui_utf8_lead_need(u);
    if (need < 2U || (uint32_t)input->byte_len + need > BLOOM_TEXT_INPUT_CAP) {
        ui_stamp_mac(input, 0x7EC70064ULL, &input->hash_lo32, &input->hash_hi32);
        return false;
    }
    size_t stage_base = 40U - (size_t)need;
    input->utf8_buf[stage_base] = (char)u;
    input->utf8_buf[39] = (char)((need << 4U) | 1U);
    ui_stamp_mac(input, 0x7EC70064ULL, &input->hash_lo32, &input->hash_hi32);
    return true;
}

bool bloom_text_input_backspace64(BloomTextInput64 *input) {
    if (!input) return false;
    /* First sanitize any corrupt state & clear incomplete staged bytes */
    uint8_t raw_len = (input->byte_len > BLOOM_TEXT_INPUT_CAP) ? BLOOM_TEXT_INPUT_CAP : input->byte_len;
    uint8_t raw_cpos = (input->cursor_pos > raw_len) ? raw_len : input->cursor_pos;
    ui_input_sanitize(input, false);
    uint8_t blen = (input->byte_len > BLOOM_TEXT_INPUT_CAP) ? BLOOM_TEXT_INPUT_CAP : input->byte_len;
    if (blen == 0U || raw_cpos == 0U) {
        ui_stamp_mac(input, 0x7EC70064ULL, &input->hash_lo32, &input->hash_hi32);
        return false;
    }
    /* If caller placed cursor inside a multi-byte codepoint, delete that enclosing codepoint;
       otherwise delete the complete UTF-8 codepoint immediately preceding cursor_pos */
    uint8_t cpos = (raw_cpos > blen) ? blen : raw_cpos;
    uint8_t del_start = (cpos < blen && (((uint8_t)input->utf8_buf[cpos] & 0xC0U) == 0x80U))
        ? cpos
        : (uint8_t)(cpos - 1U);
    while (del_start > 0U && (((uint8_t)input->utf8_buf[del_start] & 0xC0U) == 0x80U)) {
        --del_start;
    }
    size_t cp_len = ui_utf8_codepoint_len((const uint8_t *)input->utf8_buf + del_start,
                                          (size_t)blen - (size_t)del_start);
    uint8_t max_cut = (uint8_t)(blen - del_start);
    uint8_t cut = (cp_len > 0U && cp_len <= (size_t)max_cut) ? (uint8_t)cp_len : 1U;
    for (uint8_t i = del_start; (uint32_t)i + (uint32_t)cut < (uint32_t)blen; ++i) {
        input->utf8_buf[i] = input->utf8_buf[i + cut];
    }
    blen = (uint8_t)(blen - cut);
    input->byte_len = blen;
    input->cursor_pos = del_start;
    input->anchor_pos = del_start;
    ui_input_clear_staging(input);
    input->edit_revision++;
    ui_stamp_mac(input, 0x7EC70064ULL, &input->hash_lo32, &input->hash_hi32);
    return true;
}

void bloom_ime_init64(BloomIme64 *ime, uint32_t target_node_id) {
    if (!ime) return;
    memset(ime, 0, sizeof(*ime));
    ime->target_node_id = target_node_id;
    ui_stamp_mac(ime, 0x1AE00064ULL, &ime->mac_lo32, &ime->mac_hi32);
}

bool bloom_ime_set_preedit64(BloomIme64 *ime, const char *utf8_preedit, uint8_t candidate_idx) {
    if (!ime || !utf8_preedit) return false;
    size_t raw_len = strlen(utf8_preedit);
    size_t fit_len = 0U;
    size_t scan = 0U;
    while (scan < raw_len) {
        size_t cp = ui_utf8_codepoint_len((const uint8_t *)utf8_preedit + scan, raw_len - scan);
        if (cp == 0U) {
            return false;
        }
        if (fit_len + cp <= 35U) {
            fit_len += cp;
        }
        scan += cp;
    }
    memset(ime->preedit_utf8, 0, sizeof(ime->preedit_utf8));
    if (fit_len > 0U) {
        memcpy(ime->preedit_utf8, utf8_preedit, fit_len);
    }
    ime->preedit_len = (uint8_t)fit_len;
    ime->cursor_offset = (uint8_t)fit_len;
    ime->candidate_index = candidate_idx;
    ime->ime_active = (fit_len > 0U) ? 1U : 0U;
    ui_stamp_mac(ime, 0x1AE00064ULL, &ime->mac_lo32, &ime->mac_hi32);
    return true;
}

bool bloom_ime_commit64(BloomIme64 *ime, BloomTextInput64 *input) {
    if (!ime || !input) return false;
    ui_input_sanitize(input, false);
    uint8_t raw_plen = (ime->preedit_len > 35U) ? 35U : ime->preedit_len;
    if (raw_plen == 0U) return false;

    /* Verify preedit buffer contains exclusively complete RFC 3629 UTF-8 codepoints */
    size_t vlen = 0U;
    while (vlen < (size_t)raw_plen) {
        size_t cp = ui_utf8_codepoint_len((const uint8_t *)ime->preedit_utf8 + vlen,
                                          (size_t)raw_plen - vlen);
        if (cp == 0U) return false;
        vlen += cp;
    }
    if ((uint32_t)input->byte_len + (uint32_t)vlen > BLOOM_TEXT_INPUT_CAP) {
        ui_stamp_mac(input, 0x7EC70064ULL, &input->hash_lo32, &input->hash_hi32);
        return false;
    }
    if (!ui_input_splice_utf8(input, (const uint8_t *)ime->preedit_utf8, (uint8_t)vlen)) {
        return false;
    }
    memset(ime->preedit_utf8, 0, sizeof(ime->preedit_utf8));
    ime->preedit_len = 0U;
    ime->cursor_offset = 0U;
    ime->ime_active = 0U;
    ime->committed_count++;
    ui_stamp_mac(ime, 0x1AE00064ULL, &ime->mac_lo32, &ime->mac_hi32);
    return true;
}

void bloom_window_init64(BloomWindow64 *win,
                         uint32_t backend_api,
                         uint32_t logical_w,
                         uint32_t logical_h,
                         int32_t dpi_scale_q16,
                         uint32_t target_fps) {
    if (!win) return;
    memset(win, 0, sizeof(*win));
    win->backend_api = backend_api;
    win->logical_w_px = (logical_w > 0U) ? logical_w : 1280U;
    win->logical_h_px = (logical_h > 0U) ? logical_h : 720U;
    win->dpi_scale_q16 = (dpi_scale_q16 >= 32768) ? dpi_scale_q16 : BLOOM_Q16_ONE;
    win->fb_w_px = (uint32_t)((((uint64_t)win->logical_w_px) * (uint64_t)win->dpi_scale_q16) >> 16);
    win->fb_h_px = (uint32_t)((((uint64_t)win->logical_h_px) * (uint64_t)win->dpi_scale_q16) >> 16);
    win->target_fps = (target_fps > 0U) ? target_fps : 60U;
    win->host_flags = BLOOM_WIN_FLAG_VSYNC;
}
