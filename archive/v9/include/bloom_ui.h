#ifndef BLOOM_UI_H
#define BLOOM_UI_H

#include "bloom_abi.h"
#include "flow_ir.h"

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
#define BLOOM_UI_PRIM_TEXT_INPUT   7U

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

/* Generic UI Node Capability & Behavior Bits (stored in upper 16 bits of action_opcode) */
#define BLOOM_UI_OP_MASK           0x0000FFFFU
#define BLOOM_UI_CAP_MASK          0xFFFF0000U
#define BLOOM_UI_CAP_FOCUSABLE     (1U << 16)
#define BLOOM_UI_CAP_DISABLED      (1U << 17)
#define BLOOM_UI_CAP_TOGGLE        (1U << 18)
#define BLOOM_UI_CAP_CHECKED       (1U << 19)
#define BLOOM_UI_CAP_AGENT_GUARD   (1U << 20)
#define BLOOM_UI_CAP_MODAL         (1U << 21)

/* Agent-Native UI Bridge Sources, Action Kinds & Status Codes */
#define BLOOM_UI_SRC_NONE          0U
#define BLOOM_UI_SRC_HUMAN         1U
#define BLOOM_UI_SRC_AGENT         2U
#define BLOOM_UI_SRC_SYSTEM        3U

#define BLOOM_UI_ACT_CLICK          0U
#define BLOOM_UI_ACT_SET_VALUE      1U
#define BLOOM_UI_ACT_TOGGLE         2U
#define BLOOM_UI_ACT_FOCUS          3U
#define BLOOM_UI_ACT_ROVE_NEXT      4U
#define BLOOM_UI_ACT_ROVE_PREV      5U
#define BLOOM_UI_ACT_TEXT_CHAR      6U
#define BLOOM_UI_ACT_TEXT_BACKSPACE 7U

#define BLOOM_UI_STATUS_OK           0U
#define BLOOM_UI_STATUS_VETOED_GUARD 1U
#define BLOOM_UI_STATUS_NOT_FOUND    2U
#define BLOOM_UI_STATUS_DISABLED     3U
#define BLOOM_UI_STATUS_MODAL_TRAP   4U
#define BLOOM_UI_STATUS_INVALID_ARG  5U

#define BLOOM_UI_GUARD_NONE          0U
#define BLOOM_UI_GUARD_ALLOW         1U
#define BLOOM_UI_GUARD_VETO          2U

/* Packed DOM focus / provenance / agent-seq field inside BloomDomState64.reserved0 */
#define BLOOM_DOM_FOCUSED_NODE(dom) ((uint32_t)((dom)->reserved0 & 0xFFFFU))
#define BLOOM_DOM_LAST_SOURCE(dom)  ((uint32_t)(((dom)->reserved0 >> 16U) & 0xFU))
#define BLOOM_DOM_LAST_STATUS(dom)  ((uint32_t)(((dom)->reserved0 >> 20U) & 0xFU))
#define BLOOM_DOM_AGENT_SEQ(dom)    ((uint32_t)(((dom)->reserved0 >> 24U) & 0xFFU))
#define BLOOM_DOM_PACK_STATE(focused_nid, src, status, agent_seq) \
    ((((uint32_t)(focused_nid)) & 0xFFFFU) |                      \
     ((((uint32_t)(src)) & 0xFU) << 16U) |                        \
     ((((uint32_t)(status)) & 0xFU) << 20U) |                     \
     ((((uint32_t)(agent_seq)) & 0xFFU) << 24U))

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

/* 5. Wave64 Hit-Testing, Pointer Capture, Zero-BLR SEARU Solver & FLOW v9.0.0 Tactile Springs */
uint32_t bloom_ui_step_pointer_arena(BloomEngineArena *arena,
                                     int32_t cursor_x_q16,
                                     int32_t cursor_y_q16,
                                     int32_t drag_dx_q16,
                                     bool mouse_down,
                                     bool mouse_just_clicked);

bool bloom_ui_solve_glass_tracks64(BloomUiNode64 *nodes,
                                   size_t node_count,
                                   const uint16_t *conflict_edges,
                                   uint32_t edge_count,
                                   uint64_t kan_bit_pin,
                                   uint64_t seed,
                                   uint32_t max_iters,
                                   uint32_t *out_unsat_edges);

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

/* 8. Domain-Agnostic Agent-Native UI SDK (Snapshot / Query / Guarded Act / Roving Focus) */
typedef struct {
    uint32_t slot_index;
    uint32_t node_id;
    uint32_t prim_type;
    uint32_t action_op;
    uint32_t caps;
    uint32_t bind_channel;
    int32_t  bound_value_q16;
    int32_t  offset_x_q16;
    int32_t  offset_y_q16;
    int32_t  half_w_q16;
    int32_t  half_h_q16;
    bool     hovered;
    bool     active;
    bool     focused;
    bool     disabled;
    bool     checked;
    bool     guarded;
    bool     modal;
    char     label[16];
} BloomUiNodeView;

typedef struct {
    uint32_t        node_count;
    uint32_t        focused_node_id;
    uint32_t        hovered_node_id;
    uint32_t        active_node_id;
    uint32_t        captured_node_id;
    uint32_t        focused_input_id;
    uint32_t        last_action_op;
    uint32_t        last_source;
    uint32_t        last_status;
    uint32_t        agent_seq;
    bool            modal_active;
    uint32_t        frame_seq;
    int32_t         reg_channels_q16[BLOOM_REG_CHANNELS];
    char            text_input_utf8[40];
    BloomUiNodeView nodes[BLOOM_MAX_UI_NODES];
} BloomUiSnapshot;

typedef struct {
    uint32_t node_id;
    uint32_t prim_type;
    uint32_t action_op;
    uint32_t required_caps;
    uint32_t excluded_caps;
    bool     only_focused;
    bool     only_enabled;
    const char *label_prefix;
} BloomUiQuery;

typedef struct {
    uint32_t node_id;
    uint32_t act_kind;
    int32_t  value_q16;
    uint32_t source;
    bool     allow_guarded;
    uint8_t  guard_verdict;
    uint8_t  char_byte;
} BloomUiActRequest;

typedef struct {
    uint32_t status;
    uint32_t fired_action;
    uint32_t target_node_id;
    uint32_t focused_node_id;
    uint32_t source;
    uint32_t agent_seq;
    int32_t  new_value_q16;
    bool     checked;
} BloomUiActResult;

void bloom_ui_set_node_label(BloomEngineArena *arena, uint32_t slot_idx, const char *label);

void bloom_ui_set_node_caps(BloomUiNode64 *node, uint32_t caps_enable, uint32_t caps_disable);

bool bloom_ui_snapshot_arena(const BloomEngineArena *arena, BloomUiSnapshot *out_snap);

size_t bloom_ui_query_arena(const BloomEngineArena *arena,
                            const BloomUiQuery *query,
                            BloomUiNodeView *out_views,
                            size_t max_views);

bool bloom_ui_fill_guard_inputs(const BloomEngineArena *arena,
                                const BloomUiActRequest *req,
                                uint64_t out_vec[6]);

uint32_t bloom_ui_agent_act(BloomEngineArena *arena,
                            const BloomUiActRequest *req,
                            BloomUiActResult *out_res);

static inline uint32_t bloom_ui_agent_act_with_fir(BloomEngineArena *arena,
                                                   const BloomUiActRequest *req,
                                                   const flow_ir_prog_t *fir_guard,
                                                   BloomUiActResult *out_res) {
    if (!arena || !req) {
        return bloom_ui_agent_act(arena, req, out_res);
    }
    BloomUiActRequest eff = *req;
    if (fir_guard != NULL && eff.source == BLOOM_UI_SRC_AGENT) {
        uint64_t in_vec[FLOW_IR_MAX_IO];
        uint64_t out_vec[FLOW_IR_MAX_IO];
        for (size_t i = 0U; i < FLOW_IR_MAX_IO; ++i) {
            in_vec[i] = UINT64_C(0);
            out_vec[i] = UINT64_C(0);
        }
        bool has_node = bloom_ui_fill_guard_inputs(arena, req, in_vec);
        bool eval_ok = has_node && flow_ir_eval_u64(fir_guard, in_vec, out_vec);
        eff.guard_verdict = (eval_ok && out_vec[0] != UINT64_C(0))
            ? (uint8_t)BLOOM_UI_GUARD_ALLOW
            : (uint8_t)BLOOM_UI_GUARD_VETO;
    }
    return bloom_ui_agent_act(arena, &eff, out_res);
}

size_t bloom_ui_snapshot_to_json(const BloomUiSnapshot *snap, char *out_buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_UI_H */
