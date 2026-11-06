/*
 * ============================================================================
 * FLOW SVG-UI FRAMEWORK & ZERO-COPY BROADCAST / OBS BRIDGE (100% Pure C17)
 * ============================================================================
 * Zero floating-point operations (FloatOps == 0), zero heap allocations
 * (HeapCalls == 0). Enforces all 4 theorems in research/SvgUiFramework.lean.
 * ============================================================================
 */

#include "flow_svg_ui.h"

#include <stdio.h>
#include <string.h>

static int32_t clamp_i32_ui(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static uint64_t compute_dom_mac64(const FlowSvgUiDomState64 *dom) {
    uint64_t h = 0x517CC1B727220A95ULL;
    h ^= (uint64_t)dom->compiled_node_count * 0x9E3779B97F4A7C15ULL;
    h = (h << 17U) | (h >> 47U);
    h ^= (uint64_t)dom->hovered_node_id * 0xBF58476D1CE4E5B9ULL;
    h ^= (uint64_t)dom->active_node_id * 0x94D049BB133111EBULL;
    h ^= (uint64_t)dom->click_dispatch_count * 0xD6E8FEB86659FD93ULL;
    h ^= dom->active_dom_mask_wave64;
    return h ^ (h >> 31U);
}

static uint64_t compute_bridge_mac64(const FlowBroadcastObsBridge64 *b) {
    uint64_t h = 0x9E3779B97F4A7C15ULL ^ b->state_trajectory_hash;
    h ^= ((uint64_t)b->iosurface_id_ring[0] << 32U) | (uint64_t)b->iosurface_id_ring[1];
    h ^= ((uint64_t)b->iosurface_id_ring[2] << 32U) | (uint64_t)b->published_frame_seq;
    h ^= ((uint64_t)b->broadcast_mode << 16U) | (uint64_t)b->read_ring_slot;
    h *= 0xBF58476D1CE4E5B9ULL;
    return h ^ (h >> 32U);
}

static int32_t parse_svg_attr_i32(const char *line, const char *attr_key, int32_t default_val) {
    const char *p = strstr(line, attr_key);
    if (!p) return default_val;
    p += strlen(attr_key);
    while (*p == ' ' || *p == '=' || *p == '"') {
        ++p;
    }
    int32_t sign = 1;
    if (*p == '-') {
        sign = -1;
        ++p;
    }
    int32_t acc = 0;
    bool found = false;
    while (*p >= '0' && *p <= '9') {
        acc = acc * 10 + (int32_t)(*p - '0');
        found = true;
        ++p;
    }
    return found ? (acc * sign) : default_val;
}

static uint32_t parse_svg_hex_rgb24(const char *line, const char *attr_key, uint32_t default_rgb24) {
    const char *p = strstr(line, attr_key);
    if (!p) return default_rgb24;
    p += strlen(attr_key);
    while (*p == ' ' || *p == '=' || *p == '"') {
        ++p;
    }
    if (*p == '#') {
        ++p;
    }
    uint32_t acc = 0U;
    uint32_t digits = 0U;
    while (digits < 6U) {
        char c = *p;
        uint32_t nib = 0U;
        if (c >= '0' && c <= '9') {
            nib = (uint32_t)(c - '0');
        } else if (c >= 'A' && c <= 'F') {
            nib = (uint32_t)(c - 'A' + 10);
        } else if (c >= 'a' && c <= 'f') {
            nib = (uint32_t)(c - 'a' + 10);
        } else {
            break;
        }
        acc = (acc << 4U) | nib;
        ++digits;
        ++p;
    }
    return (digits == 6U) ? (acc & 0x00FFFFFFU) : default_rgb24;
}

/*
 * Precomputes the conservative 8x8 screen-space Wave64 tile bitmask for a UI node.
 * Normalized UV coordinates in shader are roughly x in [-0.50, +0.50] (-32768..+32768 Q16)
 * and y in [-0.36, +0.36] (-23592..+23592 Q16).
 */
static uint64_t compute_node_wave64_mask(int32_t cx_q16,
                                         int32_t cy_q16,
                                         int32_t hw_q16,
                                         int32_t hh_q16) {
    int32_t x0 = cx_q16 - hw_q16 - 1024;
    int32_t x1 = cx_q16 + hw_q16 + 1024;
    int32_t y0 = cy_q16 - hh_q16 - 1024;
    int32_t y1 = cy_q16 + hh_q16 + 1024;

    /* Map [-32768, +32768] onto [0..7] columns and [-24576, +24576] onto [0..7] rows */
    int32_t col_min = clamp_i32_ui((x0 + 32768) / 8192, 0, 7);
    int32_t col_max = clamp_i32_ui((x1 + 32768) / 8192, 0, 7);
    int32_t row_min = clamp_i32_ui((y0 + 24576) / 6144, 0, 7);
    int32_t row_max = clamp_i32_ui((y1 + 24576) / 6144, 0, 7);

    uint64_t mask = 0ULL;
    for (int32_t r = row_min; r <= row_max; ++r) {
        for (int32_t c = col_min; c <= col_max; ++c) {
            uint32_t bit = (uint32_t)(r * 8 + c);
            mask |= (1ULL << bit);
        }
    }
    return mask;
}

static uint32_t cursor_to_wave64_bit(int32_t x_q16, int32_t y_q16) {
    int32_t c = clamp_i32_ui((x_q16 + 32768) / 8192, 0, 7);
    int32_t r = clamp_i32_ui((y_q16 + 24576) / 6144, 0, 7);
    return (uint32_t)(r * 8 + c);
}

static bool has_svg_attr(const char *line, const char *attr_key) {
    return strstr(line, attr_key) != NULL;
}

static void parse_svg_viewbox_wh(const char *line, int32_t *inout_w, int32_t *inout_h) {
    const char *p = strstr(line, "viewBox");
    if (!p) return;
    p += 7;
    while (*p == ' ' || *p == '=' || *p == '"') ++p;
    int32_t vals[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; ++i) {
        while (*p == ' ' || *p == ',') ++p;
        int32_t sign = 1;
        if (*p == '-') { sign = -1; ++p; }
        int32_t v = 0;
        bool ok = false;
        while (*p >= '0' && *p <= '9') {
            v = v * 10 + (int32_t)(*p - '0');
            ok = true;
            ++p;
        }
        if (*p == '.') {
            ++p;
            while (*p >= '0' && *p <= '9') ++p;
        }
        if (!ok) return;
        vals[i] = v * sign;
    }
    if (vals[2] > 0 && vals[3] > 0) {
        *inout_w = vals[2];
        *inout_h = vals[3];
    }
}

int32_t flow_svg_ui_norm_viewbox_coord_q16(int32_t coord_px, int32_t view_extent_px) {
    if (view_extent_px <= 0 || coord_px <= 0) {
        return 0;
    }
    if (coord_px >= view_extent_px) {
        return 65536;
    }
    int64_t scaled = ((int64_t)coord_px * 65536LL) / (int64_t)view_extent_px;
    return clamp_i32_ui((int32_t)scaled, 0, 65536);
}

size_t flow_svg_ui_compile_file(const char *svg_path,
                                FlowSvgUiNode64 *out_nodes,
                                size_t max_nodes,
                                FlowSvgUiDomState64 *out_dom) {
    if (!svg_path || !out_nodes || max_nodes == 0U) {
        return 0U;
    }
    memset(out_nodes, 0, sizeof(FlowSvgUiNode64) * max_nodes);
    if (out_dom) {
        memset(out_dom, 0, sizeof(*out_dom));
        out_dom->viewport_width_px  = 3840U;
        out_dom->viewport_height_px = 2160U;
        out_dom->hot_reload_epoch   = 1U;
    }

    FILE *f = fopen(svg_path, "r");
    if (!f) {
        return 0U;
    }

    char line_buf[512];
    char elem_buf[1536];
    elem_buf[0] = '\0';
    bool in_ui_elem = false;
    size_t count = 0U;
    uint64_t combined_mask = 0ULL;
    int32_t view_w = 1920;
    int32_t view_h = 1080;

    while (fgets(line_buf, (int)sizeof(line_buf), f) != NULL && count < max_nodes) {
        if (strstr(line_buf, "<svg") != NULL || strstr(line_buf, "viewBox=") != NULL) {
            parse_svg_viewbox_wh(line_buf, &view_w, &view_h);
        }
        if (!in_ui_elem) {
            if (strstr(line_buf, "data-ui-node=\"") != NULL ||
                strstr(line_buf, "data-ui-widget=\"") != NULL) {
                in_ui_elem = true;
                elem_buf[0] = '\0';
                strncat(elem_buf, line_buf, sizeof(elem_buf) - strlen(elem_buf) - 1U);
            }
        } else {
            strncat(elem_buf, line_buf, sizeof(elem_buf) - strlen(elem_buf) - 1U);
        }

        if (in_ui_elem && (strchr(line_buf, '>') != NULL)) {
            FlowSvgUiNode64 *n = &out_nodes[count];
            int32_t fallback_id = parse_svg_attr_i32(elem_buf, "data-ui-widget", (int32_t)(201U + count));
            n->node_id           = (uint32_t)parse_svg_attr_i32(elem_buf, "data-ui-node", fallback_id);
            n->prim_type         = (uint32_t)parse_svg_attr_i32(elem_buf, "data-prim", (int32_t)FLOW_SVG_PRIM_RECT_GLASS);
            n->anchor_mode       = (uint32_t)parse_svg_attr_i32(elem_buf, "data-anchor", (int32_t)FLOW_SVG_ANCHOR_CENTER);
            n->bind_source       = (uint32_t)parse_svg_attr_i32(elem_buf, "data-bind", (int32_t)FLOW_SVG_BIND_NONE);

            if (has_svg_attr(elem_buf, "data-ox-q16") || has_svg_attr(elem_buf, "data-oy-q16")) {
                n->offset_x_q16      = parse_svg_attr_i32(elem_buf, "data-ox-q16", 0);
                n->offset_y_q16      = parse_svg_attr_i32(elem_buf, "data-oy-q16", 0);
                n->half_w_q16        = parse_svg_attr_i32(elem_buf, "data-hw-q16", 3276);
                n->half_h_q16        = parse_svg_attr_i32(elem_buf, "data-hh-q16", 1638);
                n->corner_radius_q16 = parse_svg_attr_i32(elem_buf, "data-rx-q16", 655);
            } else if (has_svg_attr(elem_buf, "cx=") && has_svg_attr(elem_buf, "cy=")) {
                int32_t cx_px = parse_svg_attr_i32(elem_buf, "cx=", view_w / 2);
                int32_t cy_px = parse_svg_attr_i32(elem_buf, "cy=", view_h / 2);
                int32_t r_px  = parse_svg_attr_i32(elem_buf, "r=", 24);
                int32_t nx    = flow_svg_ui_norm_viewbox_coord_q16(cx_px, view_w);
                int32_t ny    = flow_svg_ui_norm_viewbox_coord_q16(cy_px, view_h);
                int32_t nr    = (int32_t)(((int64_t)r_px * 24576LL) / (int64_t)(view_h > 0 ? view_h : 1080));
                n->offset_x_q16      = nx - 32768;
                n->offset_y_q16      = 24576 - ((ny * 49152) >> 16);
                n->half_w_q16        = (nr > 0) ? nr : 1024;
                n->half_h_q16        = (nr > 0) ? nr : 1024;
                n->corner_radius_q16 = n->half_h_q16;
            } else {
                int32_t x_px  = parse_svg_attr_i32(elem_buf, "x=", 0);
                int32_t y_px  = parse_svg_attr_i32(elem_buf, "y=", 0);
                int32_t w_px  = parse_svg_attr_i32(elem_buf, "width=", 120);
                int32_t h_px  = parse_svg_attr_i32(elem_buf, "height=", 48);
                int32_t rx_px = parse_svg_attr_i32(elem_buf, "rx=", 8);
                int32_t cx_px = x_px + (w_px / 2);
                int32_t cy_px = y_px + (h_px / 2);
                int32_t nx    = flow_svg_ui_norm_viewbox_coord_q16(cx_px, view_w);
                int32_t ny    = flow_svg_ui_norm_viewbox_coord_q16(cy_px, view_h);
                n->offset_x_q16      = nx - 32768;
                n->offset_y_q16      = 24576 - ((ny * 49152) >> 16);
                n->half_w_q16        = (int32_t)(((int64_t)w_px * 32768LL) / (int64_t)(view_w > 0 ? view_w : 1920));
                n->half_h_q16        = (int32_t)(((int64_t)h_px * 24576LL) / (int64_t)(view_h > 0 ? view_h : 1080));
                n->corner_radius_q16 = (int32_t)(((int64_t)rx_px * 24576LL) / (int64_t)(view_h > 0 ? view_h : 1080));
            }
            n->scale_jet_v_q16   = 65536; /* 1.0x rest scale */
            n->scale_jet_d1_q16  = 0;
            n->bound_value_q16   = 32768;
            uint32_t base_style  = (uint32_t)parse_svg_attr_i32(elem_buf, "data-style", 1) & 0x7FU;
            uint32_t fill_rgb24  = parse_svg_hex_rgb24(elem_buf, "fill=", 0x20B8FFU);
            n->style_and_state   = (fill_rgb24 << 8U) | base_style;
            n->action_opcode     = (uint32_t)parse_svg_attr_i32(elem_buf, "data-action", 0);
            n->tile_mask_wave64  = compute_node_wave64_mask(n->offset_x_q16,
                                                            n->offset_y_q16,
                                                            n->half_w_q16,
                                                            n->half_h_q16);
            combined_mask |= n->tile_mask_wave64;
            count++;
            in_ui_elem = false;
            elem_buf[0] = '\0';
        }
    }

    fclose(f);

    if (out_dom) {
        out_dom->compiled_node_count    = (uint32_t)count;
        out_dom->active_dom_mask_wave64 = combined_mask;
        out_dom->dom_state_mac          = compute_dom_mac64(out_dom);
    }
    return count;
}

void flow_svg_ui_resolve_anchor_q16(const FlowSvgUiNode64 *node,
                                    uint32_t screen_w_px,
                                    uint32_t screen_h_px,
                                    int32_t *out_center_x_q16,
                                    int32_t *out_center_y_q16) {
    if (!node || !out_center_x_q16 || !out_center_y_q16) {
        return;
    }
    uint32_t w = (screen_w_px > 0U) ? screen_w_px : 3840U;
    uint32_t h = (screen_h_px > 0U) ? screen_h_px : 2160U;

    /* Aspect ratio in Q16 relative to 16:9 baseline (where half-width = 32768 Q16) */
    int32_t half_ext_x_q16 = (int32_t)(((int64_t)w * 18432LL) / (int64_t)h);
    int32_t half_ext_y_q16 = 18432;

    int32_t cx = node->offset_x_q16;
    int32_t cy = node->offset_y_q16;

    switch (node->anchor_mode) {
        case FLOW_SVG_ANCHOR_TOP_LEFT:
            cx = -half_ext_x_q16 + (node->offset_x_q16 + 32768);
            cy = -half_ext_y_q16 + (node->offset_y_q16 + 24576);
            break;
        case FLOW_SVG_ANCHOR_TOP_RIGHT:
            cx = half_ext_x_q16 - (32768 - node->offset_x_q16);
            cy = -half_ext_y_q16 + (node->offset_y_q16 + 24576);
            break;
        case FLOW_SVG_ANCHOR_BOTTOM_LEFT:
            cx = -half_ext_x_q16 + (node->offset_x_q16 + 32768);
            cy = half_ext_y_q16 - (24576 - node->offset_y_q16);
            break;
        case FLOW_SVG_ANCHOR_BOTTOM_RIGHT:
            cx = half_ext_x_q16 - (32768 - node->offset_x_q16);
            cy = half_ext_y_q16 - (24576 - node->offset_y_q16);
            break;
        case FLOW_SVG_ANCHOR_BOTTOM_CENTER:
            cx = node->offset_x_q16;
            cy = half_ext_y_q16 - (24576 - node->offset_y_q16);
            break;
        case FLOW_SVG_ANCHOR_CENTER:
        default:
            break;
    }

    *out_center_x_q16 = cx;
    *out_center_y_q16 = cy;
}

void flow_svg_ui_bind_telemetry_q16(FlowSvgUiNode64 *nodes,
                                    size_t node_count,
                                    uint32_t thermal_kelvin,
                                    int32_t craft_speed_q16,
                                    uint32_t active_vcam_id,
                                    uint32_t dopamine_phase,
                                    uint32_t broadcast_mode) {
    if (!nodes) return;

    for (size_t i = 0U; i < node_count; ++i) {
        FlowSvgUiNode64 *n = &nodes[i];
        int32_t val_q16 = 0;

        switch (n->bind_source) {
            case FLOW_SVG_BIND_THERMAL_KELVIN: {
                if (n->prim_type == FLOW_SVG_PRIM_NUM_READOUT) {
                    /* Pass integer Kelvin directly in bound_value_q16 for 7-segment display */
                    val_q16 = (int32_t)clamp_i32_ui((int32_t)thermal_kelvin, 0, 9999);
                } else {
                    int32_t delta = (thermal_kelvin > 700U) ? (int32_t)(thermal_kelvin - 700U) : 0;
                    val_q16 = clamp_i32_ui((delta * 65536) / 150, 0, 65536);
                }
                break;
            }
            case FLOW_SVG_BIND_CRAFT_SPEED: {
                int32_t spd = craft_speed_q16 >> 16;
                val_q16 = clamp_i32_ui((spd * 65536) / 1200, 0, 65536);
                break;
            }
            case FLOW_SVG_BIND_VCAM_ID: {
                uint32_t target_vcam = (n->action_opcode >= FLOW_SVG_ACTION_VCAM_0 &&
                                        n->action_opcode <= FLOW_SVG_ACTION_VCAM_3)
                    ? (n->action_opcode - FLOW_SVG_ACTION_VCAM_0)
                    : 0U;
                val_q16 = (active_vcam_id == target_vcam) ? 65536 : 16384;
                break;
            }
            case FLOW_SVG_BIND_DOPAMINE_PHASE:
                val_q16 = (dopamine_phase > 0U) ? 65536 : 24576;
                break;
            case FLOW_SVG_BIND_BROADCAST_REC:
                val_q16 = (broadcast_mode != FLOW_BROADCAST_MODE_IDLE) ? 65536 : 16384;
                break;
            case FLOW_SVG_BIND_XRAY_RADIUS:
            case FLOW_SVG_BIND_PIPELINE_SLA:
                /* Preserve interactive scrubbed value in [0..65536] */
                val_q16 = clamp_i32_ui(n->bound_value_q16, 0, 65536);
                break;
            case FLOW_SVG_BIND_NONE:
            default:
                val_q16 = 32768;
                break;
        }
        n->bound_value_q16 = val_q16;
    }
}

uint32_t flow_svg_ui_extract_fill_rgb24(const FlowSvgUiNode64 *node) {
    if (!node) return 0U;
    return (node->style_and_state >> 8U) & 0x00FFFFFFU;
}

int32_t flow_svg_ui_eval_glass_transmittance_q16(uint32_t fill_rgb24, int32_t bevel_thickness_q16) {
    int32_t r = (int32_t)((fill_rgb24 >> 16U) & 0xFFU);
    int32_t g = (int32_t)((fill_rgb24 >> 8U)  & 0xFFU);
    int32_t b = (int32_t)(fill_rgb24          & 0xFFU);
    int32_t luma_255 = (r * 77 + g * 150 + b * 29) >> 8;
    int32_t absorb_255 = 255 - clamp_i32_ui(luma_255, 0, 255);
    int32_t thick = clamp_i32_ui(bevel_thickness_q16, 0, 65536);
    int32_t optical_loss_q16 = (int32_t)(((int64_t)absorb_255 * (int64_t)thick) / 320LL);
    int32_t raw_q16 = 65536 - optical_loss_q16;
    /* Enforces Theorem 5 (glass_beer_lambert_transmittance_bounded): [4096, 65536] */
    return clamp_i32_ui(raw_q16, 4096, 65536);
}

uint32_t flow_svg_ui_step_pointer_arena(FlowSvgUiNode64 *nodes,
                                        size_t node_count,
                                        FlowSvgUiDomState64 *dom,
                                        int32_t cursor_norm_x_q16,
                                        int32_t cursor_norm_y_q16,
                                        int32_t drag_delta_x_q16,
                                        bool mouse_pressed,
                                        bool mouse_just_clicked) {
    if (!nodes || !dom) {
        return FLOW_SVG_ACTION_NONE;
    }

    uint32_t prev_captured_id = mouse_pressed ? dom->active_node_id : 0U;

    dom->cursor_norm_x_q16 = cursor_norm_x_q16;
    dom->cursor_norm_y_q16 = cursor_norm_y_q16;
    dom->hovered_node_id   = 0U;
    dom->active_node_id    = 0U;
    dom->audio_click_patch_id = 0U;

    uint32_t cursor_bit = cursor_to_wave64_bit(cursor_norm_x_q16, cursor_norm_y_q16);
    uint64_t cursor_mask = 1ULL << cursor_bit;
    uint32_t fired_action = FLOW_SVG_ACTION_NONE;

    for (size_t i = 0U; i < node_count; ++i) {
        FlowSvgUiNode64 *n = &nodes[i];
        bool is_hovered = false;
        bool is_captured = (prev_captured_id != 0U && n->node_id == prev_captured_id);

        if (!is_captured && (n->tile_mask_wave64 & cursor_mask) == 0ULL) {
            dom->wave64_cull_skips++;
        } else if (is_captured) {
            /* Theorem 6: Pointer Capture keeps tracking even outside Wave64 tile bounds */
            is_hovered = true;
        } else {
            int32_t dx = cursor_norm_x_q16 - n->offset_x_q16;
            int32_t dy = cursor_norm_y_q16 - n->offset_y_q16;
            if (dx < 0) dx = -dx;
            if (dy < 0) dy = -dy;
            if (dx <= n->half_w_q16 && dy <= n->half_h_q16 && n->action_opcode != FLOW_SVG_ACTION_NONE) {
                is_hovered = true;
            }
        }

        n->style_and_state &= ~(FLOW_SVG_STATE_HOVER_BIT |
                                FLOW_SVG_STATE_ACTIVE_BIT |
                                FLOW_SVG_STATE_CAPTURED_BIT);
        int32_t target_scale_q16 = 65536;

        if (is_hovered) {
            dom->hovered_node_id = n->node_id;
            n->style_and_state |= FLOW_SVG_STATE_HOVER_BIT;
            target_scale_q16 = 72089; /* 1.10x Hover Lift */

            if (mouse_pressed) {
                dom->active_node_id = n->node_id;
                n->style_and_state |= (FLOW_SVG_STATE_ACTIVE_BIT | FLOW_SVG_STATE_CAPTURED_BIT);
                target_scale_q16 = 58982; /* 0.90x Anticipation Press */

                if (drag_delta_x_q16 != 0) {
                    /* Theorem 6: Continuous Pointer Capture Scrubbing in [0, 65536] */
                    n->bound_value_q16 = clamp_i32_ui(n->bound_value_q16 + drag_delta_x_q16, 0, 65536);
                    /* Inject continuous surface ripple energy while scrubbing */
                    int32_t abs_dx = (drag_delta_x_q16 < 0) ? -drag_delta_x_q16 : drag_delta_x_q16;
                    n->scale_jet_d1_q16 += clamp_i32_ui(abs_dx >> 1, 1024, 16384);
                    fired_action = n->action_opcode;
                    dom->last_dispatched_action = fired_action;
                }
            }
            if (mouse_just_clicked) {
                fired_action = n->action_opcode;
                dom->last_dispatched_action = fired_action;
                dom->click_dispatch_count++;
                dom->audio_click_patch_id = 1U + (n->action_opcode & 3U);
                /* Inject 2-Jet Impact Velocity impulse (+0.35x spring & water ripple pop) */
                n->scale_jet_d1_q16 += 22937;
            }
        }

        /* 2-Jet Critically-Damped Spring Step on (scale_jet_v_q16, scale_jet_d1_q16) */
        int32_t err_q16 = target_scale_q16 - n->scale_jet_v_q16;
        int32_t accel_q16 = (err_q16 >> 2) - (n->scale_jet_d1_q16 >> 1);
        n->scale_jet_d1_q16 += accel_q16;
        n->scale_jet_v_q16  += n->scale_jet_d1_q16;
        n->scale_jet_v_q16   = clamp_i32_ui(n->scale_jet_v_q16, 49152, 91750);
    }

    dom->dom_state_mac = compute_dom_mac64(dom);
    return fired_action;
}

uint32_t flow_svg_ui_step_interaction(FlowSvgUiNode64 *nodes,
                                      size_t node_count,
                                      FlowSvgUiDomState64 *dom,
                                      int32_t cursor_norm_x_q16,
                                      int32_t cursor_norm_y_q16,
                                      bool mouse_pressed,
                                      bool mouse_just_clicked) {
    return flow_svg_ui_step_pointer_arena(nodes,
                                          node_count,
                                          dom,
                                          cursor_norm_x_q16,
                                          cursor_norm_y_q16,
                                          0,
                                          mouse_pressed,
                                          mouse_just_clicked);
}

void flow_broadcast_obs_init64(FlowBroadcastObsBridge64 *bridge,
                               uint32_t surface_id_0,
                               uint32_t surface_id_1,
                               uint32_t surface_id_2) {
    if (!bridge) return;
    memset(bridge, 0, sizeof(*bridge));
    bridge->state_trajectory_hash = 0xCBF29CE484222325ULL;
    bridge->iosurface_id_ring[0]  = (surface_id_0 != 0U) ? surface_id_0 : 0x1001U;
    bridge->iosurface_id_ring[1]  = (surface_id_1 != 0U) ? surface_id_1 : 0x1002U;
    bridge->iosurface_id_ring[2]  = (surface_id_2 != 0U) ? surface_id_2 : 0x1003U;
    bridge->write_ring_slot       = 1U;
    bridge->read_ring_slot        = 0U;
    bridge->broadcast_mode        = FLOW_BROADCAST_MODE_LIVE_OBS_SHM;
    bridge->active_vcam_mask      = 0x1U;
    bridge->obs_audio_sample_rate = 48000U;
    bridge->bridge_seal_mac       = compute_bridge_mac64(bridge);
}

void flow_broadcast_obs_publish_frame64(FlowBroadcastObsBridge64 *bridge,
                                        uint32_t frame_seq,
                                        uint32_t active_vcam_id,
                                        uint32_t dopamine_phase,
                                        uint64_t frame_state_hash) {
    if (!bridge) return;

    /* Advance Triple-Buffered Zero-Copy Ring (Theorem 4: write_ring_slot != read_ring_slot) */
    uint32_t published_slot = bridge->write_ring_slot % 3U;
    bridge->read_ring_slot  = published_slot;
    bridge->write_ring_slot = (published_slot + 1U) % 3U;

    bridge->published_frame_seq = frame_seq;
    bridge->state_trajectory_hash ^= frame_state_hash + 0x9E3779B97F4A7C15ULL
                                   + (bridge->state_trajectory_hash << 6U)
                                   + (bridge->state_trajectory_hash >> 2U);

    if (bridge->replay_frames_buffered < 3600U) {
        bridge->replay_frames_buffered++;
    }
    /* Each 4K RGBA8 zero-copy IOSurface frame saves 33 MB of CPU readback */
    bridge->zero_copy_bytes_saved += 33U;

    /* When Dopamine Jackpot Catharsis Boom triggers, unlock 4-VCam Post-Play Replay mask */
    if (dopamine_phase >= 3U) {
        bridge->highlight_jackpot_tick = frame_seq;
        bridge->active_vcam_mask       = 0xFU; /* All 4 VCams (135mm, 24mm, 35mm, 18mm) */
        bridge->broadcast_mode         = FLOW_BROADCAST_MODE_REPLAY_4VCAM;
    } else {
        bridge->active_vcam_mask = (1U << (active_vcam_id & 3U));
    }

    bridge->bridge_seal_mac = compute_bridge_mac64(bridge);
}

int flow_broadcast_obs_export_shm_packet(const FlowBroadcastObsBridge64 *bridge,
                                         const FlowSvgUiDomState64 *dom,
                                         const char *shm_file_path) {
    if (!bridge || !dom || !shm_file_path) {
        return -1;
    }
    FILE *f = fopen(shm_file_path, "wb");
    if (!f) {
        return -2;
    }
    size_t w1 = fwrite(bridge, sizeof(FlowBroadcastObsBridge64), 1U, f);
    size_t w2 = fwrite(dom, sizeof(FlowSvgUiDomState64), 1U, f);
    fclose(f);
    return (w1 == 1U && w2 == 1U) ? 0 : -3;
}

void flow_debug_log_append64(FlowDebugLogEntry64 *ring,
                             size_t ring_capacity,
                             FlowPipelineTelemetry64 *telemetry,
                             uint64_t timestamp_us,
                             uint16_t stage_id,
                             uint16_t event_opcode,
                             uint32_t source_node_id,
                             uint32_t target_prefab_id,
                             int32_t prev_val_q16,
                             int32_t next_val_q16,
                             int32_t cursor_x_q16,
                             int32_t cursor_y_q16,
                             uint32_t ripple_energy_q16) {
    if (!ring || ring_capacity == 0U || !telemetry) {
        return;
    }
    uint32_t idx = telemetry->log_write_head % (uint32_t)ring_capacity;
    uint32_t prev_idx = (idx == 0U) ? (uint32_t)(ring_capacity - 1U) : (idx - 1U);
    uint64_t prev_chain = (telemetry->total_logs_emitted > 0U)
        ? ring[prev_idx].entry_hash
        : 0xCBF29CE484222325ULL;

    FlowDebugLogEntry64 *e = &ring[idx];
    e->timestamp_us      = timestamp_us;
    e->frame_seq         = telemetry->frame_seq;
    e->stage_id          = stage_id;
    e->event_opcode      = event_opcode;
    e->source_node_id    = source_node_id;
    e->target_prefab_id  = target_prefab_id;
    e->prev_value_q16    = prev_val_q16;
    e->next_value_q16    = next_val_q16;
    e->cursor_x_q16      = cursor_x_q16;
    e->cursor_y_q16      = cursor_y_q16;
    e->ripple_energy_q16 = ripple_energy_q16;
    e->active_flags      = (uint32_t)stage_id | ((uint32_t)event_opcode << 8U);
    e->chain_hash        = prev_chain;

    uint64_t h = prev_chain ^ (timestamp_us * 0x9E3779B97F4A7C15ULL);
    h ^= ((uint64_t)e->frame_seq << 32U) | ((uint64_t)stage_id << 16U) | (uint64_t)event_opcode;
    h ^= ((uint64_t)source_node_id << 32U) | (uint64_t)target_prefab_id;
    h ^= ((uint64_t)(uint32_t)prev_val_q16 << 32U) | (uint64_t)(uint32_t)next_val_q16;
    h *= 0xBF58476D1CE4E5B9ULL;
    e->entry_hash = h ^ (h >> 31U);

    telemetry->log_write_head = (idx + 1U) % (uint32_t)ring_capacity;
    telemetry->total_logs_emitted++;
}

void flow_pipeline_telemetry_step64(FlowPipelineTelemetry64 *telemetry,
                                    uint32_t frame_seq,
                                    uint32_t us_input,
                                    uint32_t us_prefab,
                                    uint32_t us_physics,
                                    uint32_t us_raymarch,
                                    uint32_t us_svg_dom,
                                    uint32_t us_broadcast) {
    if (!telemetry) return;

    telemetry->frame_seq          = frame_seq;
    telemetry->us_stage_input     = us_input;
    telemetry->us_stage_prefab    = us_prefab;
    telemetry->us_stage_physics   = us_physics;
    telemetry->us_stage_raymarch  = us_raymarch;
    telemetry->us_stage_svg_dom   = us_svg_dom;
    telemetry->us_stage_broadcast = us_broadcast;

    uint32_t total = us_input + us_prefab + us_physics + us_raymarch + us_svg_dom + us_broadcast;
    telemetry->total_frame_us     = total;
    telemetry->budget_headroom_us = 8333 - (int32_t)total;

    int32_t sla_q16 = (total >= 8333U)
        ? 0
        : (int32_t)((((int64_t)(8333U - total)) * 65536LL) / 8333LL);
    telemetry->pipeline_health_q16 = (uint32_t)clamp_i32_ui(sla_q16, 0, 65536);

    uint64_t h = 0x517CC1B727220A95ULL ^ ((uint64_t)frame_seq << 32U) ^ (uint64_t)total;
    h ^= ((uint64_t)us_raymarch << 16U) ^ (uint64_t)telemetry->total_logs_emitted;
    h *= 0x94D049BB133111EBULL;
    telemetry->telemetry_mac = h ^ (h >> 32U);
}

bool flow_debug_log_verify_chain64(const FlowDebugLogEntry64 *ring,
                                   size_t count) {
    if (!ring || count == 0U) {
        return false;
    }
    size_t cyclic_links = 0U;
    size_t linear_links = 0U;
    for (size_t i = 0U; i < count; ++i) {
        const FlowDebugLogEntry64 *e = &ring[i];
        if (e->chain_hash == 0ULL) {
            return false;
        }
        uint64_t h = e->chain_hash ^ (e->timestamp_us * 0x9E3779B97F4A7C15ULL);
        h ^= ((uint64_t)e->frame_seq << 32U) | ((uint64_t)e->stage_id << 16U) | (uint64_t)e->event_opcode;
        h ^= ((uint64_t)e->source_node_id << 32U) | (uint64_t)e->target_prefab_id;
        h ^= ((uint64_t)(uint32_t)e->prev_value_q16 << 32U) | (uint64_t)(uint32_t)e->next_value_q16;
        h *= 0xBF58476D1CE4E5B9ULL;
        uint64_t expected_entry = h ^ (h >> 31U);
        if (e->entry_hash != expected_entry) {
            return false;
        }
        size_t next_idx = (i + 1U) % count;
        if (ring[next_idx].chain_hash == e->entry_hash) {
            cyclic_links++;
            if (i + 1U < count) {
                linear_links++;
            }
        }
    }
    if (ring[0].chain_hash == 0xCBF29CE484222325ULL && linear_links == count - 1U) {
        return true;
    }
    return (count > 1U) && (cyclic_links >= count - 1U);
}

/*
 * ============================================================================
 * 9. Single-Pass DFA Markdown/LaTeX Parser & Zero-Font-Atlas 64B SDF Math Box
 * ============================================================================
 */
const uint8_t FLOW_ASCII_DFA_JUMP_TABLE[256] = {
    ['$']  = FLOW_DFA_CHAR_DOLLAR,
    ['_']  = FLOW_DFA_CHAR_UNDERSCORE,
    ['*']  = FLOW_DFA_CHAR_ASTERISK,
    ['\\'] = FLOW_DFA_CHAR_BACKSLASH,
    ['<']  = FLOW_DFA_CHAR_ENTITY,
    ['>']  = FLOW_DFA_CHAR_ENTITY,
    ['&']  = FLOW_DFA_CHAR_ENTITY,
    ['^']  = FLOW_DFA_CHAR_ENTITY,
    ['{']  = FLOW_DFA_CHAR_ENTITY,
    ['}']  = FLOW_DFA_CHAR_ENTITY
};

static bool dfa_is_alnum(char c) {
    uint8_t u = (uint8_t)c;
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || (u >= 0x80U);
}

static bool dfa_is_digit(char c) {
    return (c >= '0' && c <= '9');
}

static bool dfa_is_space(char c) {
    return (c == ' ' || c == '\t' || c == '\r' || c == '\n');
}

/*
 * Pandora / GitHub '$' Flanking Lookahead (Bounded to Current Line, Zero Regex Backtracking):
 *   - Opening '$' cannot be followed by whitespace.
 *   - Currency prefix '$<digits>' followed by whitespace or non-math punctuation
 *     ('~', ')', ']', ';', ':', '\0') is rejected in O(1) so "$100 (~$\alpha$)"
 *     never steals the opening '$' of a subsequent math span on the same line.
 *   - Closing '$' on the same line cannot be preceded by whitespace and cannot be
 *     immediately followed by a digit (immunizes "$100 ~ $200" currency strings).
 *   - Unclosed '$' before '\n' or '\0' degrades to literal '$' in O(1).
 */
static bool dfa_validate_dollar_math_open(const char *src, size_t open_idx, size_t len) {
    if (open_idx + 1U >= len) {
        return false;
    }
    if (dfa_is_space(src[open_idx + 1U]) || src[open_idx + 1U] == '$') {
        return false;
    }
    if (dfa_is_digit(src[open_idx + 1U])) {
        size_t d = open_idx + 1U;
        while (d < len && (dfa_is_digit(src[d]) || src[d] == '.' || src[d] == ',')) {
            d++;
        }
        if (d >= len || dfa_is_space(src[d]) || src[d] == '~' || src[d] == ')' ||
            src[d] == ']' || src[d] == ';' || src[d] == ':') {
            return false;
        }
    }
    for (size_t k = open_idx + 1U; k < len; ++k) {
        char c = src[k];
        if (c == '\n' || c == '\r') {
            return false; /* Never backtrack across lines */
        }
        if (c == '$' && src[k - 1U] != '\\') {
            bool before_close_space = dfa_is_space(src[k - 1U]);
            bool after_close_digit  = (k + 1U < len) ? dfa_is_digit(src[k + 1U]) : false;
            return (!before_close_space) && (!after_close_digit);
        }
    }
    return false;
}

uint16_t flow_latex_hash_macro_single_pass(const char *src,
                                           size_t len,
                                           size_t *inout_cursor) {
    if (!src || !inout_cursor || *inout_cursor >= len) {
        return FLOW_LATEX_OP_NONE;
    }
    size_t i = *inout_cursor;
    if (src[i] == '\\') {
        i++;
    }
    uint32_t h = 2166136261U;
    size_t start = i;
    while (i < len && ((src[i] >= 'a' && src[i] <= 'z') || (src[i] >= 'A' && src[i] <= 'Z'))) {
        h ^= (uint32_t)(uint8_t)src[i];
        h *= 16777619U;
        i++;
    }
    size_t macro_len = i - start;
    if (macro_len == 4U && strncmp(&src[start], "frac", 4U) == 0) {
        *inout_cursor = i;
        return FLOW_LATEX_OP_FRAC;
    }
    if (macro_len == 4U && strncmp(&src[start], "sqrt", 4U) == 0) {
        *inout_cursor = i;
        return FLOW_LATEX_OP_SQRT;
    }
    if (macro_len == 3U && strncmp(&src[start], "int", 3U) == 0) {
        *inout_cursor = i;
        return FLOW_LATEX_OP_INT;
    }
    if (macro_len == 3U && strncmp(&src[start], "sum", 3U) == 0) {
        *inout_cursor = i;
        return FLOW_LATEX_OP_SUM;
    }
    if (macro_len == 4U && strncmp(&src[start], "left", 4U) == 0) {
        if (i < len && (src[i] == '(' || src[i] == '[')) i++;
        *inout_cursor = i;
        return FLOW_LATEX_OP_PAREN_L;
    }
    if (macro_len == 5U && strncmp(&src[start], "right", 5U) == 0) {
        if (i < len && (src[i] == ')' || src[i] == ']')) i++;
        *inout_cursor = i;
        return FLOW_LATEX_OP_PAREN_R;
    }
    if (macro_len == 5U && strncmp(&src[start], "alpha", 5U) == 0) {
        *inout_cursor = i;
        return FLOW_LATEX_OP_ALPHA;
    }
    if (macro_len == 5U && strncmp(&src[start], "gamma", 5U) == 0) {
        *inout_cursor = i;
        return FLOW_LATEX_OP_GAMMA;
    }
    if (macro_len == 2U && strncmp(&src[start], "ge", 2U) == 0) {
        *inout_cursor = i;
        return FLOW_LATEX_OP_GE;
    }
    if (macro_len == 2U && strncmp(&src[start], "le", 2U) == 0) {
        *inout_cursor = i;
        return FLOW_LATEX_OP_LE;
    }
    *inout_cursor = i;
    return (uint16_t)(0x0300U | (h & 0x00FFU));
}

static uint64_t compute_latex_box_mac64(const FlowLatexBoxNode64 *b) {
    uint64_t h = 0x9E3779B97F4A7C15ULL;
    h ^= ((uint64_t)b->symbol_opcode << 48U) | ((uint64_t)b->tex_role << 40U) |
         ((uint64_t)b->dfa_flags << 32U) | (uint64_t)b->ascii_codepoint;
    h ^= ((uint64_t)(uint32_t)b->box_x_q16 << 32U) | (uint64_t)(uint32_t)b->box_y_q16;
    h ^= ((uint64_t)(uint32_t)b->half_w_q16 << 32U) | (uint64_t)(uint32_t)b->half_h_q16;
    h ^= b->tile_mask_wave64;
    h *= 0xBF58476D1CE4E5B9ULL;
    return h ^ (h >> 31U);
}

size_t flow_latex_dfa_compile_boxes64(const char *input,
                                      int32_t origin_x_q16,
                                      int32_t origin_y_q16,
                                      int32_t base_height_q16,
                                      uint32_t color_rgb24,
                                      FlowLatexBoxNode64 *out_boxes,
                                      size_t max_boxes,
                                      uint32_t *out_suppressed_ident_count,
                                      uint32_t *out_literal_dollar_count) {
    if (out_suppressed_ident_count) *out_suppressed_ident_count = 0U;
    if (out_literal_dollar_count)   *out_literal_dollar_count   = 0U;
    if (!input || !out_boxes || max_boxes == 0U) {
        return 0U;
    }

    size_t len = strlen(input);
    size_t count = 0U;
    bool in_math = false;
    uint8_t next_role = FLOW_TEX_ROLE_BASELINE;
    uint8_t group_role = FLOW_TEX_ROLE_BASELINE;
    uint32_t active_frac_idx = UINT32_MAX;
    uint32_t active_sqrt_idx = UINT32_MAX;
    uint32_t frac_brace_stage = 0U; /* 1 = expecting {num}, 2 = expecting {den} */

    int32_t h_q16 = (base_height_q16 > 256) ? base_height_q16 : 2048;
    int32_t cursor_x_q16 = origin_x_q16;
    int32_t frac_start_x_q16 = origin_x_q16;

    size_t i = 0U;
    while (i < len) {
        char c = input[i];
        uint8_t char_class = FLOW_ASCII_DFA_JUMP_TABLE[(uint8_t)c];

        if (c == '\n' || c == '\r') {
            in_math = false;
            next_role = FLOW_TEX_ROLE_BASELINE;
            group_role = FLOW_TEX_ROLE_BASELINE;
            i++;
            continue;
        }
        if (c == ' ' || c == '\t') {
            if (!in_math) {
                cursor_x_q16 += (h_q16 >> 1);
            }
            i++;
            continue;
        }

        if (char_class == FLOW_DFA_CHAR_DOLLAR) {
            if (!in_math) {
                if (dfa_validate_dollar_math_open(input, i, len)) {
                    in_math = true;
                    i++;
                    continue;
                } else {
                    /* Pandora/GitHub Rule: Degrade unclosed or currency '$100' to literal '$' in O(1) */
                    if (out_literal_dollar_count) (*out_literal_dollar_count)++;
                    if (count < max_boxes) {
                        FlowLatexBoxNode64 *b = &out_boxes[count++];
                        memset(b, 0, sizeof(*b));
                        b->symbol_opcode      = FLOW_LATEX_OP_GLYPH_VAR;
                        b->tex_role           = FLOW_TEX_ROLE_BASELINE;
                        b->dfa_flags          = FLOW_DFA_FLAG_LITERAL_DOLLAR;
                        b->ascii_codepoint    = (uint32_t)'$';
                        b->scale_q16          = 65536;
                        b->half_w_q16         = h_q16 / 3;
                        b->half_h_q16         = h_q16 / 2;
                        b->advance_x_q16      = (h_q16 * 3) / 4;
                        b->baseline_shift_q16 = 0;
                        b->box_x_q16          = cursor_x_q16 + b->half_w_q16;
                        b->box_y_q16          = origin_y_q16;
                        b->stroke_q16         = h_q16 / 10;
                        b->color_rgb24        = color_rgb24;
                        b->parent_box_idx     = UINT32_MAX;
                        b->tile_mask_wave64   = compute_node_wave64_mask(b->box_x_q16, b->box_y_q16, b->half_w_q16, b->half_h_q16);
                        b->box_mac            = compute_latex_box_mac64(b);
                        cursor_x_q16         += b->advance_x_q16;
                    }
                    i++;
                    continue;
                }
            } else {
                in_math = false;
                next_role = FLOW_TEX_ROLE_BASELINE;
                group_role = FLOW_TEX_ROLE_BASELINE;
                i++;
                continue;
            }
        }

        if (char_class == FLOW_DFA_CHAR_UNDERSCORE || char_class == FLOW_DFA_CHAR_ASTERISK) {
            bool prev_alnum = (i > 0U) && dfa_is_alnum(input[i - 1U]);
            bool next_alnum = (i + 1U < len) && dfa_is_alnum(input[i + 1U]);
            if (!in_math && prev_alnum && next_alnum) {
                /* C & UTF-8 Identifier protection (e.g. flow_svg_ui, 測試_變數): suppress Markdown emphasis! */
                if (out_suppressed_ident_count) (*out_suppressed_ident_count)++;
                if (count < max_boxes) {
                    FlowLatexBoxNode64 *b = &out_boxes[count++];
                    memset(b, 0, sizeof(*b));
                    b->symbol_opcode      = FLOW_LATEX_OP_GLYPH_VAR;
                    b->tex_role           = FLOW_TEX_ROLE_BASELINE;
                    b->dfa_flags          = FLOW_DFA_FLAG_IDENT_PROTECTED;
                    b->ascii_codepoint    = (uint32_t)(uint8_t)c;
                    b->scale_q16          = 65536;
                    b->half_w_q16         = h_q16 / 3;
                    b->half_h_q16         = h_q16 / 2;
                    b->advance_x_q16      = (h_q16 * 3) / 4;
                    b->box_x_q16          = cursor_x_q16 + b->half_w_q16;
                    b->box_y_q16          = origin_y_q16;
                    b->stroke_q16         = h_q16 / 10;
                    b->color_rgb24        = color_rgb24;
                    b->parent_box_idx     = UINT32_MAX;
                    b->tile_mask_wave64   = compute_node_wave64_mask(b->box_x_q16, b->box_y_q16, b->half_w_q16, b->half_h_q16);
                    b->box_mac            = compute_latex_box_mac64(b);
                    cursor_x_q16         += b->advance_x_q16;
                }
                i++;
                continue;
            }
            if (in_math && c == '_') {
                next_role = FLOW_TEX_ROLE_SUBSCRIPT;
                i++;
                continue;
            }
        }

        if (char_class == FLOW_DFA_CHAR_ENTITY) {
            if (in_math && c == '^') {
                next_role = FLOW_TEX_ROLE_SUPERSCRIPT;
                i++;
                continue;
            }
            if (c == '{') {
                if (frac_brace_stage == 1U) {
                    group_role = FLOW_TEX_ROLE_FRAC_NUM;
                    frac_start_x_q16 = cursor_x_q16;
                } else if (frac_brace_stage == 2U) {
                    group_role = FLOW_TEX_ROLE_FRAC_DEN;
                    cursor_x_q16 = frac_start_x_q16; /* Center denominator below numerator */
                } else if (active_sqrt_idx != UINT32_MAX) {
                    group_role = FLOW_TEX_ROLE_RADICAND;
                } else if (next_role != FLOW_TEX_ROLE_BASELINE) {
                    group_role = next_role;
                    next_role = FLOW_TEX_ROLE_BASELINE;
                }
                i++;
                continue;
            }
            if (c == '}') {
                if (frac_brace_stage == 1U) {
                    frac_brace_stage = 2U;
                } else if (frac_brace_stage == 2U) {
                    frac_brace_stage = 0U;
                    active_frac_idx = UINT32_MAX;
                } else if (group_role == FLOW_TEX_ROLE_RADICAND) {
                    active_sqrt_idx = UINT32_MAX;
                }
                group_role = FLOW_TEX_ROLE_BASELINE;
                i++;
                continue;
            }
        }

        uint16_t opcode = FLOW_LATEX_OP_GLYPH_VAR;
        uint32_t codepoint = (uint32_t)(uint8_t)c;
        bool is_stretchy = false;

        if (char_class == FLOW_DFA_CHAR_BACKSLASH) {
            opcode = flow_latex_hash_macro_single_pass(input, len, &i);
            codepoint = (uint32_t)opcode;
            if (opcode >= FLOW_LATEX_OP_FRAC && opcode <= FLOW_LATEX_OP_PAREN_R) {
                is_stretchy = true;
            }
        } else if ((uint8_t)c >= 0x80U) {
            /* Single-pass UTF-8 codepoint decoder (#21153 CJK / multi-byte synchronization) */
            uint8_t u0 = (uint8_t)c;
            if ((u0 & 0xE0U) == 0xC0U && (i + 1U < len) &&
                (((uint8_t)input[i + 1U] & 0xC0U) == 0x80U)) {
                codepoint = ((uint32_t)(u0 & 0x1FU) << 6U) |
                            (uint32_t)((uint8_t)input[i + 1U] & 0x3FU);
                i += 2U;
            } else if ((u0 & 0xF0U) == 0xE0U && (i + 2U < len) &&
                       (((uint8_t)input[i + 1U] & 0xC0U) == 0x80U) &&
                       (((uint8_t)input[i + 2U] & 0xC0U) == 0x80U)) {
                codepoint = ((uint32_t)(u0 & 0x0FU) << 12U) |
                            ((uint32_t)((uint8_t)input[i + 1U] & 0x3FU) << 6U) |
                            (uint32_t)((uint8_t)input[i + 2U] & 0x3FU);
                i += 3U;
            } else if ((u0 & 0xF8U) == 0xF0U && (i + 3U < len) &&
                       (((uint8_t)input[i + 1U] & 0xC0U) == 0x80U) &&
                       (((uint8_t)input[i + 2U] & 0xC0U) == 0x80U) &&
                       (((uint8_t)input[i + 3U] & 0xC0U) == 0x80U)) {
                codepoint = ((uint32_t)(u0 & 0x07U) << 18U) |
                            ((uint32_t)((uint8_t)input[i + 1U] & 0x3FU) << 12U) |
                            ((uint32_t)((uint8_t)input[i + 2U] & 0x3FU) << 6U) |
                            (uint32_t)((uint8_t)input[i + 3U] & 0x3FU);
                i += 4U;
            } else {
                i++;
            }
        } else {
            if (c == '+' || c == '-' || c == '=' || c == '*') {
                opcode = FLOW_LATEX_OP_BINARY_OP;
            }
            i++;
        }

        uint8_t role = (next_role != FLOW_TEX_ROLE_BASELINE) ? next_role : group_role;
        next_role = FLOW_TEX_ROLE_BASELINE;

        if (count >= max_boxes) {
            continue;
        }

        /* TeX Box-and-Glue Q16 Fixed-Point Scale & Baseline Shift */
        int32_t scale_q16 = 65536;
        int32_t dy_q16    = 0;
        if (role == FLOW_TEX_ROLE_SUPERSCRIPT) {
            scale_q16 = 45875; /* 0.70x in Q16 */
            dy_q16    = -(h_q16 * 45) / 100; /* -0.45h */
        } else if (role == FLOW_TEX_ROLE_SUBSCRIPT) {
            scale_q16 = 45875; /* 0.70x in Q16 */
            dy_q16    = (h_q16 * 35) / 100;  /* +0.35h */
        } else if (role == FLOW_TEX_ROLE_FRAC_NUM) {
            scale_q16 = 52428; /* 0.80x in Q16 */
            dy_q16    = -(h_q16 * 55) / 100; /* -0.55h above fraction bar */
        } else if (role == FLOW_TEX_ROLE_FRAC_DEN) {
            scale_q16 = 52428; /* 0.80x in Q16 */
            dy_q16    = (h_q16 * 55) / 100;  /* +0.55h below fraction bar */
        }

        int32_t scaled_h = (int32_t)(((int64_t)h_q16 * (int64_t)scale_q16) >> 16);
        int32_t hw = (opcode == FLOW_LATEX_OP_FRAC) ? h_q16 : (scaled_h / 3);
        int32_t hh = is_stretchy ? ((h_q16 * 4) / 5) : (scaled_h / 2);
        int32_t adv = (opcode == FLOW_LATEX_OP_FRAC) ? (h_q16 / 4) : ((scaled_h * 3) / 4);

        uint32_t cur_idx = (uint32_t)count;
        FlowLatexBoxNode64 *b = &out_boxes[count++];
        memset(b, 0, sizeof(*b));
        b->symbol_opcode      = opcode;
        b->tex_role           = role;
        b->dfa_flags          = (in_math ? FLOW_DFA_FLAG_MATH_MODE : 0U) |
                                (is_stretchy ? FLOW_DFA_FLAG_STRETCHY : 0U);
        b->ascii_codepoint    = codepoint;
        b->scale_q16          = scale_q16;
        b->half_w_q16         = hw;
        b->half_h_q16         = hh;
        b->advance_x_q16      = adv;
        b->baseline_shift_q16 = dy_q16;
        b->box_x_q16          = cursor_x_q16 + hw;
        b->box_y_q16          = origin_y_q16 + dy_q16;
        b->stroke_q16         = (h_q16 / 10 > 32) ? (h_q16 / 10) : 32;
        b->color_rgb24        = color_rgb24;
        b->parent_box_idx     = (role == FLOW_TEX_ROLE_FRAC_NUM || role == FLOW_TEX_ROLE_FRAC_DEN)
                                ? active_frac_idx
                                : ((role == FLOW_TEX_ROLE_RADICAND) ? active_sqrt_idx : UINT32_MAX);

        if (opcode == FLOW_LATEX_OP_FRAC) {
            active_frac_idx = cur_idx;
            frac_brace_stage = 1U;
        } else if (opcode == FLOW_LATEX_OP_SQRT) {
            active_sqrt_idx = cur_idx;
        }

        /* Stretch parent \frac bar or \sqrt vinculum roof as child boxes advance */
        if (b->parent_box_idx < count) {
            FlowLatexBoxNode64 *parent = &out_boxes[b->parent_box_idx];
            int32_t span = (b->box_x_q16 + b->half_w_q16) - (parent->box_x_q16 - parent->half_w_q16);
            if (span / 2 > parent->half_w_q16) {
                parent->half_w_q16 = span / 2;
                parent->tile_mask_wave64 = compute_node_wave64_mask(
                    parent->box_x_q16, parent->box_y_q16, parent->half_w_q16, parent->half_h_q16);
                parent->box_mac = compute_latex_box_mac64(parent);
            }
        }

        b->tile_mask_wave64 = compute_node_wave64_mask(b->box_x_q16, b->box_y_q16, b->half_w_q16, b->half_h_q16);
        b->box_mac          = compute_latex_box_mac64(b);
        cursor_x_q16       += adv;
    }

    return count;
}

int32_t flow_latex_eval_box_sdf_q16(const FlowLatexBoxNode64 *box,
                                    int32_t sample_x_q16,
                                    int32_t sample_y_q16) {
    if (!box) {
        return 65536;
    }
    int32_t dx = sample_x_q16 - box->box_x_q16;
    int32_t dy = sample_y_q16 - box->box_y_q16;
    int32_t adx = (dx < 0) ? -dx : dx;
    int32_t ady = (dy < 0) ? -dy : dy;

    if (box->symbol_opcode == FLOW_LATEX_OP_FRAC) {
        /* Stretchy horizontal fraction bar SDF */
        int32_t qx = (adx > box->half_w_q16) ? (adx - box->half_w_q16) : 0;
        int32_t qy = ady;
        return (qx > qy ? qx : qy) - box->stroke_q16;
    }
    if (box->symbol_opcode == FLOW_LATEX_OP_SQRT) {
        /* Stretchy radical check + top vinculum roof bar SDF */
        int32_t roof_dy = sample_y_q16 - (box->box_y_q16 - box->half_h_q16);
        int32_t aroof = (roof_dy < 0) ? -roof_dy : roof_dy;
        int32_t d_roof = ((adx > box->half_w_q16) ? (adx - box->half_w_q16) : 0) + aroof - box->stroke_q16;
        int32_t d_stem = ((adx > box->stroke_q16 * 2) ? (adx - box->stroke_q16 * 2) : 0) +
                         ((ady > box->half_h_q16) ? (ady - box->half_h_q16) : 0) - box->stroke_q16;
        return (d_roof < d_stem) ? d_roof : d_stem;
    }
    /* General glyph / stretchy delimiter box SDF */
    int32_t qx = adx - box->half_w_q16;
    int32_t qy = ady - box->half_h_q16;
    int32_t out_d = ((qx > 0) ? qx : 0) + ((qy > 0) ? qy : 0);
    int32_t in_d  = (qx > qy) ? qx : qy;
    return out_d + ((in_d < 0) ? in_d : 0) - box->stroke_q16;
}

/*
 * ============================================================================
 * 10. 64-Byte Scripted Story Finisher ("劇情殺") & Scene Transition ("過渡動畫")
 * ============================================================================
 */
static uint64_t compute_director_mac64(const FlowNarrativeDirector64 *d) {
    uint64_t h = 0xCBF29CE484222325ULL;
    h ^= ((uint64_t)d->script_id << 32U) | ((uint64_t)d->director_phase << 16U) |
         (uint64_t)d->transition_style;
    h ^= ((uint64_t)d->input_lock_mask << 32U) | (uint64_t)d->target_prefab_id;
    h ^= ((uint64_t)d->target_vcam_id << 32U) | (uint64_t)d->next_ticket_preset_id;
    h ^= ((uint64_t)(uint32_t)d->wipe_progress_q16 << 32U) |
         (uint64_t)(uint32_t)d->time_dilation_q16;
    h ^= ((uint64_t)(uint32_t)d->scripted_hp_floor_q16 << 32U) |
         (uint64_t)d->atomic_swap_completed;
    h *= 0x9E3779B97F4A7C15ULL;
    return h ^ (h >> 31U);
}

void flow_director_init64(FlowNarrativeDirector64 *dir) {
    if (!dir) return;
    memset(dir, 0, sizeof(*dir));
    dir->director_phase    = FLOW_DIRECTOR_IDLE;
    dir->transition_style  = FLOW_TRANS_LIQUID_GLASS_WAVE;
    dir->input_lock_mask   = FLOW_INPUT_LOCK_NONE;
    dir->time_dilation_q16 = 65536; /* 1.0x realtime */
    dir->director_mac      = compute_director_mac64(dir);
}

void flow_director_trigger_story_finisher64(FlowNarrativeDirector64 *dir,
                                            uint32_t script_id,
                                            uint32_t target_prefab_id,
                                            uint32_t vcam_id,
                                            uint16_t transition_style,
                                            int32_t hp_floor_q16) {
    if (!dir) return;
    memset(dir, 0, sizeof(*dir));
    dir->script_id             = (script_id != 0U) ? script_id : 0x777U;
    dir->director_phase        = FLOW_DIRECTOR_ANTICIPATION_LOCK;
    dir->transition_style      = (uint16_t)(transition_style & 3U);
    dir->input_lock_mask       = FLOW_INPUT_LOCK_FULL_CUTSCENE;
    dir->target_prefab_id      = target_prefab_id;
    dir->target_vcam_id        = (uint16_t)(vcam_id & 3U);
    dir->next_ticket_preset_id = (uint8_t)((vcam_id + 1U) & 3U);
    dir->next_stage_layer      = 1U;
    dir->phase_elapsed_ms      = 0U;
    dir->phase_duration_ms     = 320U;
    dir->wipe_progress_q16     = 0;
    dir->letterbox_aspect_q16  = 65536; /* 2.39:1 CinemaScope framing */
    dir->time_dilation_q16     = 16384; /* 0.25x slow-motion anticipation */
    dir->scripted_hp_floor_q16 = (hp_floor_q16 > 1) ? hp_floor_q16 : 1;
    dir->xray_cutaway_q16      = 16384; /* Pre-strike X-Ray core lock scan */
    dir->atomic_swap_completed = 0U;
    dir->world_facts_delta     = 1ULL << (dir->script_id & 63U);
    dir->director_mac          = compute_director_mac64(dir);
}

void flow_director_trigger_scene_transition64(FlowNarrativeDirector64 *dir,
                                              uint32_t next_ticket_preset_id,
                                              uint32_t next_stage_layer,
                                              uint16_t transition_style,
                                              uint32_t duration_ms) {
    if (!dir) return;
    uint32_t half_dur = (duration_ms >= 64U) ? (duration_ms >> 1U) : 160U;
    memset(dir, 0, sizeof(*dir));
    dir->script_id             = 0xCF01U + (next_ticket_preset_id & 3U);
    dir->director_phase        = FLOW_DIRECTOR_CLIMAX_EXECUTION;
    dir->transition_style      = (uint16_t)(transition_style & 3U);
    dir->input_lock_mask       = FLOW_INPUT_LOCK_MOVEMENT;
    dir->target_prefab_id      = 0U;
    dir->target_vcam_id        = (uint16_t)(next_ticket_preset_id & 3U);
    dir->next_ticket_preset_id = (uint8_t)(next_ticket_preset_id & 3U);
    dir->next_stage_layer      = (uint8_t)(next_stage_layer & 1U);
    dir->phase_elapsed_ms      = 0U;
    dir->phase_duration_ms     = half_dur;
    dir->wipe_progress_q16     = 0;
    dir->letterbox_aspect_q16  = 49152;
    dir->time_dilation_q16     = 32768; /* 0.50x transition glide */
    dir->scripted_hp_floor_q16 = 0;
    dir->xray_cutaway_q16      = 0;
    dir->atomic_swap_completed = 0U;
    dir->world_facts_delta     = 1ULL << (next_ticket_preset_id & 3U);
    dir->director_mac          = compute_director_mac64(dir);
}

int32_t flow_director_eval_hp_gate_q16(const FlowNarrativeDirector64 *dir,
                                       int32_t raw_hp_q16) {
    if (!dir) {
        return raw_hp_q16;
    }
    /* Theorem 15: Plot-Armor HP floor holds strictly during ANTICIPATION_LOCK */
    if (dir->director_phase == FLOW_DIRECTOR_ANTICIPATION_LOCK &&
        raw_hp_q16 < dir->scripted_hp_floor_q16) {
        return dir->scripted_hp_floor_q16;
    }
    return raw_hp_q16;
}

bool flow_director_step_choreography64(FlowNarrativeDirector64 *dir,
                                       uint32_t dt_ms,
                                       uint32_t *out_active_vcam_id,
                                       int32_t *out_trauma_boost_q16,
                                       uint32_t *inout_ticket_preset_id,
                                       uint32_t *inout_stage_layer) {
    if (out_trauma_boost_q16) *out_trauma_boost_q16 = 0;
    if (!dir || dir->director_phase == FLOW_DIRECTOR_IDLE) {
        return false;
    }

    bool swapped_now = false;
    dir->phase_elapsed_ms += dt_ms;
    uint32_t dur = (dir->phase_duration_ms > 0U) ? dir->phase_duration_ms : 1U;
    int32_t prog_q16 = (int32_t)((((int64_t)dir->phase_elapsed_ms) * 65536LL) / (int64_t)dur);
    prog_q16 = clamp_i32_ui(prog_q16, 0, 65536);

    if (out_active_vcam_id) {
        *out_active_vcam_id = dir->target_vcam_id;
    }

    if (dir->director_phase == FLOW_DIRECTOR_ANTICIPATION_LOCK) {
        dir->wipe_progress_q16    = 0;
        dir->letterbox_aspect_q16 = 65536;
        dir->time_dilation_q16    = 16384;
        if (dir->phase_elapsed_ms >= dir->phase_duration_ms) {
            /* Transition to CLIMAX_EXECUTION: release HP floor & trigger X-Ray shatter! */
            dir->director_phase        = FLOW_DIRECTOR_CLIMAX_EXECUTION;
            dir->phase_elapsed_ms      = 0U;
            dir->phase_duration_ms     = 240U;
            dir->scripted_hp_floor_q16 = 0;     /* Plot armor released for fatal strike */
            dir->time_dilation_q16     = 4096;  /* 0.0625x relativistic hitstop */
            dir->xray_cutaway_q16      = 52428; /* 0.80 SDF X-Ray internal core shatter */
            if (out_trauma_boost_q16) {
                *out_trauma_boost_q16 = 49152;  /* 0.75 6D camera trauma impulse */
            }
        }
    } else if (dir->director_phase == FLOW_DIRECTOR_CLIMAX_EXECUTION) {
        /* Closing wipe envelope [0 -> 65536] (Theorem 16) */
        dir->wipe_progress_q16 = prog_q16;
        if (dir->phase_elapsed_ms >= dir->phase_duration_ms) {
            dir->director_phase    = FLOW_DIRECTOR_SCENE_TRANSITION;
            dir->phase_elapsed_ms  = 0U;
            dir->phase_duration_ms = 64U;
            dir->wipe_progress_q16 = 65536; /* Exact 100% occlusion midpoint */
            if (dir->atomic_swap_completed == 0U) {
                if (inout_ticket_preset_id) {
                    *inout_ticket_preset_id = dir->next_ticket_preset_id;
                }
                if (inout_stage_layer) {
                    *inout_stage_layer = dir->next_stage_layer;
                }
                dir->atomic_swap_completed = 1U;
                swapped_now = true;
            }
        }
    } else if (dir->director_phase == FLOW_DIRECTOR_SCENE_TRANSITION) {
        /* Full 100% occlusion held across atomic swap frame (Theorem 16: == 65536) */
        dir->wipe_progress_q16 = 65536;
        if (dir->atomic_swap_completed == 0U) {
            if (inout_ticket_preset_id) {
                *inout_ticket_preset_id = dir->next_ticket_preset_id;
            }
            if (inout_stage_layer) {
                *inout_stage_layer = dir->next_stage_layer;
            }
            dir->atomic_swap_completed = 1U;
            swapped_now = true;
        }
        if (dir->phase_elapsed_ms >= dir->phase_duration_ms) {
            dir->director_phase    = FLOW_DIRECTOR_RECOVERY_SETTLE;
            dir->phase_elapsed_ms  = 0U;
            dir->phase_duration_ms = 240U;
            dir->time_dilation_q16 = 49152; /* 0.75x smooth settle */
            dir->xray_cutaway_q16  = 0;
        }
    } else if (dir->director_phase == FLOW_DIRECTOR_RECOVERY_SETTLE) {
        /* Opening wipe envelope [65536 -> 0] (Theorem 16) */
        dir->wipe_progress_q16    = 65536 - prog_q16;
        dir->letterbox_aspect_q16 = 65536 - prog_q16;
        if (dir->phase_elapsed_ms >= dir->phase_duration_ms) {
            dir->director_phase       = FLOW_DIRECTOR_IDLE;
            dir->input_lock_mask      = FLOW_INPUT_LOCK_NONE;
            dir->phase_elapsed_ms     = 0U;
            dir->wipe_progress_q16    = 0;
            dir->letterbox_aspect_q16 = 0;
            dir->time_dilation_q16    = 65536; /* Restore 1.0x realtime */
            dir->xray_cutaway_q16     = 0;
        }
    }

    dir->director_mac = compute_director_mac64(dir);
    return swapped_now;
}

/*
 * ============================================================================
 * 11. 64-Byte Flexbox / Stack Auto-Layout Container, Generic Register Bank &
 *     Interactive Text Input Widget State (General-Purpose Native App UI)
 * ============================================================================
 */
static uint64_t compute_layout_container_mac64(const FlowUiLayoutContainer64 *c) {
    uint64_t h = 0x517CC1B727220A95ULL;
    h ^= ((uint64_t)c->direction << 48U) | ((uint64_t)c->justify_mode << 32U) |
         ((uint64_t)c->align_items << 16U) | (uint64_t)c->overflow_mode;
    h ^= (uint64_t)(uint32_t)c->content_extent_q16 * 0x9E3779B97F4A7C15ULL;
    h ^= (uint64_t)(uint32_t)c->scroll_offset_q16 * 0xBF58476D1CE4E5B9ULL;
    h ^= (uint64_t)c->child_count * 0x94D049BB133111EBULL;
    h ^= c->container_tile_mask;
    return h ^ (h >> 31U);
}

void flow_ui_layout_container_init64(FlowUiLayoutContainer64 *container,
                                     uint16_t direction,
                                     uint16_t justify_mode,
                                     uint16_t align_items,
                                     int32_t origin_x_q16,
                                     int32_t origin_y_q16,
                                     int32_t half_w_q16,
                                     int32_t half_h_q16,
                                     int32_t padding_q16,
                                     int32_t gap_q16) {
    if (!container) return;
    memset(container, 0, sizeof(*container));
    container->direction         = (uint16_t)(direction & 1U);
    container->justify_mode      = (uint16_t)(justify_mode & 3U);
    container->align_items       = (uint16_t)(align_items & 3U);
    container->overflow_mode     = FLOW_UI_OVERFLOW_SCROLL_CLAMP;
    container->origin_x_q16      = origin_x_q16;
    container->origin_y_q16      = origin_y_q16;
    container->half_w_q16        = (half_w_q16 > 0) ? half_w_q16 : 8192;
    container->half_h_q16        = (half_h_q16 > 0) ? half_h_q16 : 8192;
    container->padding_q16       = (padding_q16 >= 0) ? padding_q16 : 0;
    container->gap_q16           = (gap_q16 >= 0) ? gap_q16 : 0;
    container->scroll_offset_q16 = 0;
    container->layout_mac        = compute_layout_container_mac64(container);
}

void flow_ui_layout_container_apply64(FlowUiLayoutContainer64 *container,
                                      FlowSvgUiNode64 *nodes,
                                      size_t node_count,
                                      int32_t scroll_delta_q16) {
    if (!container || !nodes || node_count == 0U) {
        return;
    }
    bool is_row = (container->direction == FLOW_UI_LAYOUT_ROW);
    int32_t main_half  = is_row ? container->half_w_q16 : container->half_h_q16;
    int32_t cross_half = is_row ? container->half_h_q16 : container->half_w_q16;
    int32_t pad        = (container->padding_q16 >= 0) ? container->padding_q16 : 0;
    int32_t gap        = (container->gap_q16 >= 0) ? container->gap_q16 : 0;

    int32_t children_span = 0;
    for (size_t i = 0U; i < node_count; ++i) {
        int32_t ch_half = is_row ? nodes[i].half_w_q16 : nodes[i].half_h_q16;
        if (ch_half < 64) ch_half = 64;
        children_span += (ch_half * 2);
    }
    int32_t total_gaps = (node_count > 1U) ? (int32_t)(node_count - 1U) * gap : 0;
    int32_t content_ext = children_span + total_gaps + (pad * 2);
    int32_t viewport_ext = main_half * 2;
    int32_t max_scroll = (content_ext > viewport_ext) ? (content_ext - viewport_ext) : 0;

    container->content_extent_q16 = content_ext;
    container->max_scroll_q16     = max_scroll;
    container->child_count        = (uint32_t)node_count;
    container->scroll_offset_q16  = clamp_i32_ui(container->scroll_offset_q16 + scroll_delta_q16,
                                                 0, max_scroll);

    int32_t inner_ext = viewport_ext - (pad * 2);
    int32_t free_space = (inner_ext > (children_span + total_gaps))
        ? (inner_ext - (children_span + total_gaps))
        : 0;
    int32_t start_lead = pad;
    int32_t eff_gap = gap;

    if (container->justify_mode == FLOW_UI_JUSTIFY_CENTER) {
        start_lead = pad + (free_space / 2);
    } else if (container->justify_mode == FLOW_UI_JUSTIFY_END) {
        start_lead = pad + free_space;
    } else if (container->justify_mode == FLOW_UI_JUSTIFY_SPACE_BETWEEN && node_count > 1U) {
        eff_gap = gap + (free_space / (int32_t)(node_count - 1U));
    }

    int32_t inner_cross_half = (cross_half > pad) ? (cross_half - pad) : cross_half;
    int32_t cursor_q16 = start_lead;
    uint64_t mask_acc = 0ULL;

    for (size_t i = 0U; i < node_count; ++i) {
        FlowSvgUiNode64 *n = &nodes[i];
        if (container->align_items == FLOW_UI_ALIGN_STRETCH) {
            if (is_row) {
                n->half_h_q16 = inner_cross_half;
            } else {
                n->half_w_q16 = inner_cross_half;
            }
        }
        int32_t ch_main_half  = is_row ? n->half_w_q16 : n->half_h_q16;
        int32_t ch_cross_half = is_row ? n->half_h_q16 : n->half_w_q16;
        if (ch_main_half < 64) ch_main_half = 64;

        int32_t local_main_center = cursor_q16 + ch_main_half - container->scroll_offset_q16;
        int32_t cross_offset = 0;
        if (container->align_items == FLOW_UI_ALIGN_START) {
            cross_offset = -inner_cross_half + ch_cross_half;
        } else if (container->align_items == FLOW_UI_ALIGN_END) {
            cross_offset = inner_cross_half - ch_cross_half;
        }

        if (is_row) {
            n->offset_x_q16 = (container->origin_x_q16 - main_half) + local_main_center;
            n->offset_y_q16 = container->origin_y_q16 - cross_offset;
        } else {
            /* Column flows top (+Y) to bottom (-Y) in screen Q16 coordinates */
            n->offset_y_q16 = (container->origin_y_q16 + main_half) - local_main_center;
            n->offset_x_q16 = container->origin_x_q16 + cross_offset;
        }

        n->tile_mask_wave64 = compute_node_wave64_mask(n->offset_x_q16,
                                                       n->offset_y_q16,
                                                       n->half_w_q16,
                                                       n->half_h_q16);
        mask_acc |= n->tile_mask_wave64;
        cursor_q16 += (ch_main_half * 2) + eff_gap;
    }

    container->container_tile_mask = mask_acc;
    container->layout_mac          = compute_layout_container_mac64(container);
}

static uint64_t compute_register_bank_mac64(const FlowUiRegisterBank64 *bank) {
    uint64_t h = 0x9E3779B97F4A7C15ULL ^ ((uint64_t)bank->epoch_tick << 32U) ^ bank->dirty_channel_mask;
    for (uint32_t i = 0U; i < FLOW_UI_REGISTER_CHANNELS; ++i) {
        h ^= (uint64_t)(uint32_t)bank->channels_q16[i] * (0xBF58476D1CE4E5B9ULL + (uint64_t)i * 0x10001ULL);
        h = (h << 13U) | (h >> 51U);
    }
    return h ^ (h >> 32U);
}

void flow_ui_register_bank_init64(FlowUiRegisterBank64 *bank) {
    if (!bank) return;
    memset(bank, 0, sizeof(*bank));
    bank->bank_mac = compute_register_bank_mac64(bank);
}

void flow_ui_register_bank_set_q16(FlowUiRegisterBank64 *bank,
                                   uint32_t channel_id,
                                   int32_t value_q16) {
    if (!bank || channel_id >= FLOW_UI_REGISTER_CHANNELS) {
        return;
    }
    bank->channels_q16[channel_id] = value_q16;
    bank->dirty_channel_mask      |= (1U << channel_id);
    bank->epoch_tick              += 1U;
    bank->bank_mac                 = compute_register_bank_mac64(bank);
}

int32_t flow_ui_register_bank_get_q16(const FlowUiRegisterBank64 *bank,
                                      uint32_t channel_id) {
    if (!bank || channel_id >= FLOW_UI_REGISTER_CHANNELS) {
        return 0;
    }
    return bank->channels_q16[channel_id];
}

void flow_svg_ui_bind_register_bank64(FlowSvgUiNode64 *nodes,
                                      size_t node_count,
                                      const FlowUiRegisterBank64 *bank) {
    if (!nodes || !bank) return;
    for (size_t i = 0U; i < node_count; ++i) {
        FlowSvgUiNode64 *n = &nodes[i];
        uint32_t ch = n->bind_source;
        if (ch == FLOW_SVG_BIND_NONE) {
            continue;
        }
        int32_t raw = flow_ui_register_bank_get_q16(bank, ch);
        if (n->prim_type == FLOW_SVG_PRIM_NUM_READOUT) {
            n->bound_value_q16 = clamp_i32_ui(raw, 0, 999999);
        } else {
            n->bound_value_q16 = clamp_i32_ui(raw, 0, 65536);
        }
    }
}

static uint64_t compute_text_input_hash64(const FlowUiTextInput64 *in) {
    uint64_t h = 0xCBF29CE484222325ULL;
    for (uint8_t i = 0U; i < in->byte_len && i < FLOW_UI_TEXT_INPUT_CAP; ++i) {
        h ^= (uint64_t)(uint8_t)in->utf8_buf[i];
        h *= 0x100000001B3ULL;
    }
    h ^= ((uint64_t)in->cursor_byte_pos << 32U) |
         ((uint64_t)in->byte_len << 24U) |
         (uint64_t)in->edit_revision;
    return h;
}

static void sync_text_input_to_node(const FlowUiTextInput64 *in, FlowSvgUiNode64 *node) {
    if (!in || !node) return;
    node->bound_value_q16 = clamp_i32_ui(((int32_t)in->byte_len * 65536) / (int32_t)FLOW_UI_TEXT_INPUT_CAP,
                                         0, 65536);
}

void flow_ui_text_input_init64(FlowUiTextInput64 *input,
                               uint32_t bound_node_id,
                               uint32_t action_id,
                               const char *initial_text) {
    if (!input) return;
    memset(input, 0, sizeof(*input));
    input->bound_node_id = bound_node_id;
    input->action_id     = action_id;
    input->focused       = 1U;
    input->edit_revision = 1U;
    if (initial_text) {
        uint8_t len = 0U;
        while (initial_text[len] != '\0' && len < FLOW_UI_TEXT_INPUT_CAP) {
            input->utf8_buf[len] = initial_text[len];
            len++;
        }
        input->utf8_buf[len]        = '\0';
        input->byte_len             = len;
        input->cursor_byte_pos      = len;
        input->selection_anchor_pos = len;
    }
    input->content_hash = compute_text_input_hash64(input);
}

bool flow_ui_text_input_insert_char64(FlowUiTextInput64 *input,
                                      char ch,
                                      FlowSvgUiNode64 *bound_node) {
    if (!input || !input->focused || (uint8_t)ch < 32U || (uint8_t)ch > 126U) {
        return false;
    }
    if (input->byte_len >= FLOW_UI_TEXT_INPUT_CAP) {
        return false;
    }
    uint8_t pos = (input->cursor_byte_pos <= input->byte_len)
        ? input->cursor_byte_pos
        : input->byte_len;
    for (uint8_t k = input->byte_len; k > pos; --k) {
        input->utf8_buf[k] = input->utf8_buf[k - 1U];
    }
    input->utf8_buf[pos]        = ch;
    input->byte_len            += 1U;
    input->utf8_buf[input->byte_len] = '\0';
    input->cursor_byte_pos      = (uint8_t)(pos + 1U);
    input->selection_anchor_pos = input->cursor_byte_pos;
    input->edit_revision       += 1U;
    input->content_hash         = compute_text_input_hash64(input);
    sync_text_input_to_node(input, bound_node);
    return true;
}

bool flow_ui_text_input_backspace64(FlowUiTextInput64 *input,
                                    FlowSvgUiNode64 *bound_node) {
    if (!input || !input->focused || input->byte_len == 0U || input->cursor_byte_pos == 0U) {
        return false;
    }
    uint8_t del_idx = (uint8_t)(input->cursor_byte_pos - 1U);
    for (uint8_t k = del_idx; k + 1U < input->byte_len; ++k) {
        input->utf8_buf[k] = input->utf8_buf[k + 1U];
    }
    input->byte_len            -= 1U;
    input->utf8_buf[input->byte_len] = '\0';
    input->cursor_byte_pos      = del_idx;
    input->selection_anchor_pos = del_idx;
    input->edit_revision       += 1U;
    input->content_hash         = compute_text_input_hash64(input);
    sync_text_input_to_node(input, bound_node);
    return true;
}

void flow_ui_text_input_move_cursor64(FlowUiTextInput64 *input,
                                      int32_t delta) {
    if (!input) return;
    int32_t next_pos = clamp_i32_ui((int32_t)input->cursor_byte_pos + delta,
                                    0, (int32_t)input->byte_len);
    input->cursor_byte_pos      = (uint8_t)next_pos;
    input->selection_anchor_pos = (uint8_t)next_pos;
    input->content_hash         = compute_text_input_hash64(input);
}

