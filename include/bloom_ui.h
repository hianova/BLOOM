#ifndef BLOOM_UI_H
#define BLOOM_UI_H

#include "bloom_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Pillar 2 ("React Native"): Declarative SVG Compiler, Zero-VDOM Flexbox /
 * Auto-Bento Layout, Wave64 Dual-Culling, Pointer Capture, IME & LaTeX SDF
 * ============================================================================
 */

/* UI Primitive Kinds */
#define BLOOM_UI_PRIM_NONE         0U
#define BLOOM_UI_PRIM_RECT_GLASS   1U
#define BLOOM_UI_PRIM_CIRCLE_RING  2U
#define BLOOM_UI_PRIM_ICON_GLYPH   3U
#define BLOOM_UI_PRIM_SLIDER_BAR   4U
#define BLOOM_UI_PRIM_LATEX_PANEL  5U
#define BLOOM_UI_PRIM_READOUT_7SEG 6U

/* Responsive Screen Anchors */
#define BLOOM_UI_ANCHOR_CENTER        0U
#define BLOOM_UI_ANCHOR_TOP_LEFT      1U
#define BLOOM_UI_ANCHOR_TOP_RIGHT     2U
#define BLOOM_UI_ANCHOR_BOTTOM_LEFT   3U
#define BLOOM_UI_ANCHOR_BOTTOM_RIGHT  4U
#define BLOOM_UI_ANCHOR_BOTTOM_CENTER 5U

/* Physical Glass Optics Modes */
#define BLOOM_GLASS_CRYSTAL      0U
#define BLOOM_GLASS_BEER_LAMBERT 1U
#define BLOOM_GLASS_IRIDESCENT   2U
#define BLOOM_GLASS_MERCURY      3U

/* Interactive State Bits inside style_and_state[4..7] */
#define BLOOM_UI_STATE_HOVER_BIT    (1U << 4)
#define BLOOM_UI_STATE_ACTIVE_BIT   (1U << 5)
#define BLOOM_UI_STATE_GLASS_BIT    (1U << 6)
#define BLOOM_UI_STATE_CAPTURED_BIT (1U << 7)

/* Built-In UI Action Opcodes */
#define BLOOM_ACTION_NONE          0U
#define BLOOM_ACTION_SCRUB_XRAY    1U
#define BLOOM_ACTION_PREFAB_REVERT 2U
#define BLOOM_ACTION_CYCLE_MATTER  3U
#define BLOOM_ACTION_SWAP_CHART    4U
#define BLOOM_ACTION_TOGGLE_TRC    5U
#define BLOOM_ACTION_FOCUS_INPUT   6U
#define BLOOM_ACTION_CUSTOM_BASE   16U

/* Flexbox Layout Modes */
#define BLOOM_FLEX_ROW    0U
#define BLOOM_FLEX_COLUMN 1U

#define BLOOM_JUSTIFY_START         0U
#define BLOOM_JUSTIFY_CENTER        1U
#define BLOOM_JUSTIFY_END           2U
#define BLOOM_JUSTIFY_SPACE_BETWEEN 3U

#define BLOOM_ALIGN_START   0U
#define BLOOM_ALIGN_CENTER  1U
#define BLOOM_ALIGN_END     2U
#define BLOOM_ALIGN_STRETCH 3U

/* LaTeX Box Kinds */
#define BLOOM_LATEX_SYM_GLYPH 1U
#define BLOOM_LATEX_FRAC_BAR  2U
#define BLOOM_LATEX_SQRT_RAD  3U
#define BLOOM_LATEX_INT_OP    4U
#define BLOOM_LATEX_SUM_OP    5U
#define BLOOM_LATEX_SUP_SUB   6U

/* Constraint-Projected Optical Alert States */
#define BLOOM_OPTICAL_SAFE       0U /* Ice-Cyan Glass 0x20D8FF */
#define BLOOM_OPTICAL_HYSTERESIS 1U /* Amber Warning  0xFFB020 */
#define BLOOM_OPTICAL_TRIP       2U /* Crimson Laser  0xFF2848 */

/* 1. Wave64 8x8 Spatial Coverage Computation & ViewBox Normalization */
int32_t bloom_svg_norm_viewbox_q16(int32_t coord_px, int32_t extent_px);
void bloom_ui_compute_wave64_mask(int32_t cx_q16,
                                  int32_t cy_q16,
                                  int32_t hw_q16,
                                  int32_t hh_q16,
                                  uint32_t *out_lo32,
                                  uint32_t *out_hi32);
uint32_t bloom_ui_cursor_to_wave64_bit(int32_t cursor_x_q16, int32_t cursor_y_q16);

/* 2. Standard W3C .svg String / File Compiler (UI Nodes + Vector <path>/<line> Glyphs) */
size_t bloom_svg_compile_text(const char *svg_text,
                              BloomUiNode64 *out_nodes,
                              size_t max_nodes,
                              BloomGlyph64 *out_glyphs,
                              size_t max_glyphs,
                              size_t *out_glyph_count,
                              BloomDomState64 *out_dom);

size_t bloom_svg_compile_file(const char *svg_path,
                              BloomUiNode64 *out_nodes,
                              size_t max_nodes,
                              BloomGlyph64 *out_glyphs,
                              size_t max_glyphs,
                              size_t *out_glyph_count,
                              BloomDomState64 *out_dom);

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
                       int32_t gap_q16);

void bloom_flex_apply64(BloomFlexContainer64 *flex,
                        BloomUiNode64 *nodes,
                        size_t node_count,
                        int32_t scroll_delta_q16);

uint32_t bloom_ui_eval_optical_state(int32_t val_q16,
                                     int32_t lo_exit_q16,
                                     int32_t hi_enter_q16,
                                     uint32_t prev_state);

size_t bloom_ui_synthesize_bento64(BloomIntent64 *intents,
                                   size_t intent_count,
                                   const BloomRegBank64 *reg_bank,
                                   BloomUiNode64 *out_nodes,
                                   size_t max_nodes,
                                   BloomDomState64 *out_dom);

/* 4. Zero-VDOM Register Bank & Beer-Lambert Glass Transmittance */
void bloom_reg_bank_init64(BloomRegBank64 *bank);
void bloom_reg_bank_set_q16(BloomRegBank64 *bank, uint32_t channel, int32_t val_q16);
int32_t bloom_reg_bank_get_q16(const BloomRegBank64 *bank, uint32_t channel);
void bloom_ui_bind_reg_bank64(BloomUiNode64 *nodes,
                              size_t node_count,
                              const BloomRegBank64 *bank);

int32_t bloom_ui_eval_glass_transmittance_q16(uint32_t fill_rgb24, int32_t bevel_q16);

/* 5. Wave64 Hit-Testing, Pointer Capture Scrubbing Arena & FLOW v6.0.0 Tactile Springs */
uint32_t bloom_ui_step_pointer_arena(BloomEngineArena *arena,
                                     int32_t cursor_x_q16,
                                     int32_t cursor_y_q16,
                                     int32_t drag_dx_q16,
                                     bool mouse_down,
                                     bool mouse_just_clicked);

/* 6. Single-Pass 256B Jump-Table DFA -> Zero-Font-Atlas Analytic SDF LaTeX Compiler */
size_t bloom_latex_compile_boxes64(const char *latex_src,
                                   int32_t origin_x_q16,
                                   int32_t origin_y_q16,
                                   int32_t em_size_q16,
                                   uint32_t tint_rgb24,
                                   BloomLatexBox64 *out_boxes,
                                   size_t max_boxes,
                                   int32_t *out_total_width_q16);

int32_t bloom_latex_eval_box_sdf_q16(const BloomLatexBox64 *box,
                                     int32_t px_q16,
                                     int32_t py_q16);

/* 7. Inline UTF-8 Text Input, CJK IME Preedit & HiDPI Host Window */
void bloom_text_input_init64(BloomTextInput64 *input, uint32_t bound_node_id, uint32_t action_id);
bool bloom_text_input_insert_char64(BloomTextInput64 *input, char ch);
bool bloom_text_input_backspace64(BloomTextInput64 *input);

void bloom_ime_init64(BloomIme64 *ime, uint32_t target_node_id);
bool bloom_ime_set_preedit64(BloomIme64 *ime, const char *utf8_preedit, uint8_t candidate_idx);
bool bloom_ime_commit64(BloomIme64 *ime, BloomTextInput64 *input);

void bloom_window_init64(BloomWindow64 *win,
                         uint32_t backend_api,
                         uint32_t logical_w,
                         uint32_t logical_h,
                         int32_t dpi_scale_q16,
                         uint32_t target_fps);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_UI_H */
