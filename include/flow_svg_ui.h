#ifndef FLOW_SVG_UI_H
#define FLOW_SVG_UI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "flow_gpu_abi.h"
#include "flow_hott_jet.h"
#include "ricardian.h"

/*
 * ============================================================================
 * FLOW SVG-UI FRAMEWORK & ZERO-COPY BROADCAST / OBS BRIDGE (64-Byte Pure C17)
 * ============================================================================
 * Enforces Lean 4 Formal Invariants (research/SvgUiFramework.lean):
 *   1. Responsive Anchor Zero-Reflow Layout (O(1) viewport affine mapping)
 *   2. Reactive O(1) Telemetry Register Data Binding (Zero Virtual DOM Diff)
 *   3. Wave64 8x8 Spatial Tile Bitmask + AABB Hit-Testing & 2-Jet Micro-Spring
 *   4. Triple-Buffered Zero-Copy Broadcast Ring (IOSurface + OBS SHM + 4-VCam)
 * ============================================================================
 */

#define FLOW_SVG_UI_GPU_BUS_SLOTS 8U
#define FLOW_SVG_UI_MAX_NODES     64U

/*
 * 64-Byte Declarative SVG-UI DOM & Interaction State Descriptor
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t compiled_node_count;    /*  4B: Number of compiled FlowSvgUiNode64 elements */
    uint32_t hovered_node_id;        /*  4B: Currently hovered component ID (0 = none) */
    uint32_t active_node_id;         /*  4B: Currently pressed/active component ID (0 = none) */
    uint32_t last_dispatched_action; /*  4B: Most recent FLOW_SVG_ACTION_* opcode fired */
    int32_t  cursor_norm_x_q16;      /*  4B: Cursor X in normalized [-65536, +65536] */
    int32_t  cursor_norm_y_q16;      /*  4B: Cursor Y in normalized [-65536, +65536] */
    uint32_t viewport_width_px;      /*  4B: Current viewport width in pixels */
    uint32_t viewport_height_px;     /*  4B: Current viewport height in pixels */
    uint32_t click_dispatch_count;   /*  4B: Total interactive UI click events dispatched */
    uint32_t wave64_cull_skips;      /*  4B: Hit-test AABB checks skipped by Wave64 bitmask */
    uint32_t hot_reload_epoch;       /*  4B: QSBR SVG specification reload generation */
    uint32_t audio_click_patch_id;   /*  4B: Triggered tactile audio click patch (0 = silent) */
    uint64_t active_dom_mask_wave64; /*  8B: Combined Wave64 screen coverage mask of UI DOM */
    uint64_t dom_state_mac;          /*  8B: Deterministic SipHash-style DOM state MAC */
} FlowSvgUiDomState64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowSvgUiDomState64, RicardianStateV4);
_Static_assert(sizeof(FlowSvgUiDomState64) == 64U, "FlowSvgUiDomState64 must be 64 bytes");
_Static_assert(_Alignof(FlowSvgUiDomState64) == 64U, "FlowSvgUiDomState64 must be 64-byte aligned");

/*
 * 64-Byte Zero-Copy Broadcast & OBS Studio Telemetry Bridge Descriptor
 */
#define FLOW_BROADCAST_MODE_IDLE         0U
#define FLOW_BROADCAST_MODE_LIVE_OBS_SHM 1U
#define FLOW_BROADCAST_MODE_REPLAY_4VCAM 2U

typedef struct __attribute__((aligned(64))) {
    uint64_t state_trajectory_hash;  /*  8B: Rolling deterministic hash of 64B state stream */
    uint32_t iosurface_id_ring[3];   /* 12B: Triple-buffered macOS IOSurfaceID zero-copy handles */
    uint32_t write_ring_slot;        /*  4B: Next write slot in [0..2] (never aliases read slot) */
    uint32_t read_ring_slot;         /*  4B: Latest published slot for OBS Studio / Encoder [0..2] */
    uint32_t broadcast_mode;         /*  4B: FLOW_BROADCAST_MODE_* (Idle / Live OBS / 4-VCam Replay) */
    uint32_t published_frame_seq;    /*  4B: Monotonic published frame sequence number */
    uint32_t active_vcam_mask;       /*  4B: Bitmask of active VCams (0xF = Post-Play 4-VCam Director) */
    uint32_t highlight_jackpot_tick; /*  4B: Frame tick of latest Dopamine Jackpot highlight */
    uint32_t replay_frames_buffered; /*  4B: Deterministic 64B state frames stored in replay ring */
    uint32_t zero_copy_bytes_saved;  /*  4B: Cumulative MB of CPU readback avoided via IOSurface */
    uint32_t obs_audio_sample_rate;  /*  4B: 48000 Hz binaural stereo feed rate */
    uint64_t bridge_seal_mac;        /*  8B: Cryptographic OBS SHM packet integrity seal */
} FlowBroadcastObsBridge64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowBroadcastObsBridge64, RicardianStateV4);
_Static_assert(sizeof(FlowBroadcastObsBridge64) == 64U, "FlowBroadcastObsBridge64 must be 64 bytes");
_Static_assert(_Alignof(FlowBroadcastObsBridge64) == 64U, "FlowBroadcastObsBridge64 must be 64-byte aligned");

/* API: 1. Compile Declarative .svg Component Tree into 64B FlowSvgUiNode64 Array */
size_t flow_svg_ui_compile_file(const char *svg_path,
                                FlowSvgUiNode64 *out_nodes,
                                size_t max_nodes,
                                FlowSvgUiDomState64 *out_dom);

/* API: 2. Responsive Anchor Layout Resolution (Zero-Reflow Q16 Screen Center) */
void flow_svg_ui_resolve_anchor_q16(const FlowSvgUiNode64 *node,
                                    uint32_t screen_w_px,
                                    uint32_t screen_h_px,
                                    int32_t *out_center_x_q16,
                                    int32_t *out_center_y_q16);

/* API: 3. Reactive O(1) Telemetry Register Data Binding (Zero Virtual DOM Diff) */
void flow_svg_ui_bind_telemetry_q16(FlowSvgUiNode64 *nodes,
                                    size_t node_count,
                                    uint32_t thermal_kelvin,
                                    int32_t craft_speed_q16,
                                    uint32_t active_vcam_id,
                                    uint32_t dopamine_phase,
                                    uint32_t broadcast_mode);

/* API: 4. Wave64 O(1) Spatial Hit-Testing, 2-Jet Spring Micro-Interaction & Action Dispatch */
uint32_t flow_svg_ui_step_interaction(FlowSvgUiNode64 *nodes,
                                      size_t node_count,
                                      FlowSvgUiDomState64 *dom,
                                      int32_t cursor_norm_x_q16,
                                      int32_t cursor_norm_y_q16,
                                      bool mouse_pressed,
                                      bool mouse_just_clicked);

/* API: 5. Zero-Copy Broadcast & OBS Studio Shared-Memory Bridge */
void flow_broadcast_obs_init64(FlowBroadcastObsBridge64 *bridge,
                               uint32_t surface_id_0,
                               uint32_t surface_id_1,
                               uint32_t surface_id_2);

void flow_broadcast_obs_publish_frame64(FlowBroadcastObsBridge64 *bridge,
                                        uint32_t frame_seq,
                                        uint32_t active_vcam_id,
                                        uint32_t dopamine_phase,
                                        uint64_t frame_state_hash);

int flow_broadcast_obs_export_shm_packet(const FlowBroadcastObsBridge64 *bridge,
                                         const FlowSvgUiDomState64 *dom,
                                         const char *shm_file_path);

/*
 * ============================================================================
 * 6. Zero-Allocation 64-Byte Flight-Recorder Debug Log & Pipeline Telemetry
 * ============================================================================
 */
#define FLOW_STAGE_INPUT_ARENA   0U
#define FLOW_STAGE_PREFAB_CSG    1U
#define FLOW_STAGE_PHYSICS_SIM   2U
#define FLOW_STAGE_GPU_RAYMARCH  3U
#define FLOW_STAGE_SVG_UI_DOM    4U
#define FLOW_STAGE_OBS_BROADCAST 5U

#define FLOW_LOG_EVENT_NONE          0U
#define FLOW_LOG_EVENT_UI_CLICK      1U
#define FLOW_LOG_EVENT_POINTER_SCRUB 2U
#define FLOW_LOG_EVENT_PREFAB_REVERT 3U
#define FLOW_LOG_EVENT_XRAY_TOGGLE   4U
#define FLOW_LOG_EVENT_THERMAL_TRIP   5U
#define FLOW_LOG_EVENT_MODAL_CANCEL   6U
#define FLOW_LOG_EVENT_FPS_VSYNC      7U
#define FLOW_LOG_EVENT_STORY_DIRECTOR 8U

typedef struct __attribute__((aligned(64))) {
    uint64_t timestamp_us;        /*  8B: Monotonic microsecond timestamp */
    uint32_t frame_seq;           /*  4B: Frame sequence number */
    uint16_t stage_id;            /*  2B: FLOW_STAGE_* (0=Input..5=OBS) */
    uint16_t event_opcode;        /*  2B: FLOW_LOG_EVENT_* structured opcode */
    uint32_t source_node_id;      /*  4B: SVG-UI node ID that triggered the event */
    uint32_t target_prefab_id;    /*  4B: Target Prefab ID */
    int32_t  prev_value_q16;      /*  4B: State value before event in Q16 */
    int32_t  next_value_q16;      /*  4B: State value after event in Q16 */
    int32_t  cursor_x_q16;        /*  4B: Cursor X at event time in Q16 */
    int32_t  cursor_y_q16;        /*  4B: Cursor Y at event time in Q16 */
    uint32_t ripple_energy_q16;   /*  4B: Injected 2-Jet optical ripple energy */
    uint32_t active_flags;        /*  4B: Capture / Modal / X-Ray state flags */
    uint64_t entry_hash;          /*  8B: Deterministic SipHash-style flight-recorder MAC */
    uint64_t chain_hash;          /*  8B: Cryptographic hash chain link to previous log entry */
} FlowDebugLogEntry64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowDebugLogEntry64, RicardianStateV4);
_Static_assert(sizeof(FlowDebugLogEntry64) == 64U, "FlowDebugLogEntry64 must be 64 bytes");
_Static_assert(_Alignof(FlowDebugLogEntry64) == 64U, "FlowDebugLogEntry64 must be 64-byte aligned");

typedef struct __attribute__((aligned(64))) {
    uint32_t frame_seq;           /*  4B: Current frame index */
    uint32_t us_stage_input;      /*  4B: Stage 0: Input & Pointer Capture Arena (us) */
    uint32_t us_stage_prefab;     /*  4B: Stage 1: Prefab CSG, Undo & X-Ray Mask (us) */
    uint32_t us_stage_physics;    /*  4B: Stage 2: Relativistic Worldline & TimingWheel (us) */
    uint32_t us_stage_raymarch;   /*  4B: Stage 3: 4K Single-Pass GPU Raymarch + Glass (us) */
    uint32_t us_stage_svg_dom;    /*  4B: Stage 4: SVG-UI Reactive Binding & Wave64 (us) */
    uint32_t us_stage_broadcast;  /*  4B: Stage 5: Zero-Copy OBS IOSurface Bridge (us) */
    uint32_t total_frame_us;      /*  4B: Total frame time in microseconds */
    int32_t  budget_headroom_us;  /*  4B: 8333 us (120Hz) - total_frame_us */
    uint32_t log_write_head;      /*  4B: Ring buffer write index */
    uint32_t total_logs_emitted;  /*  4B: Cumulative structured debug events recorded */
    uint32_t pipeline_health_q16; /*  4B: Normalized [0..65536] pipeline SLA health */
    uint64_t telemetry_mac;       /*  8B: 64-bit integrity seal */
    uint64_t reserved_pad;        /*  8B: Exact 64B alignment */
} FlowPipelineTelemetry64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowPipelineTelemetry64, RicardianStateV4);
_Static_assert(sizeof(FlowPipelineTelemetry64) == 64U, "FlowPipelineTelemetry64 must be 64 bytes");
_Static_assert(_Alignof(FlowPipelineTelemetry64) == 64U, "FlowPipelineTelemetry64 must be 64-byte aligned");

/* API: 6. Physical Glass Beer-Lambert Transmittance & TrueColor Extraction */
uint32_t flow_svg_ui_extract_fill_rgb24(const FlowSvgUiNode64 *node);
int32_t  flow_svg_ui_eval_glass_transmittance_q16(uint32_t fill_rgb24, int32_t bevel_thickness_q16);

/* API: 7. Pointer Capture Continuous Scrubbing Arena */
uint32_t flow_svg_ui_step_pointer_arena(FlowSvgUiNode64 *nodes,
                                        size_t node_count,
                                        FlowSvgUiDomState64 *dom,
                                        int32_t cursor_norm_x_q16,
                                        int32_t cursor_norm_y_q16,
                                        int32_t drag_delta_x_q16,
                                        bool mouse_pressed,
                                        bool mouse_just_clicked);

/* API: 8. Zero-Allocation 64B Debug Log Ring & Live Pipeline Telemetry */
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
                             uint32_t ripple_energy_q16);

void flow_pipeline_telemetry_step64(FlowPipelineTelemetry64 *telemetry,
                                    uint32_t frame_seq,
                                    uint32_t us_input,
                                    uint32_t us_prefab,
                                    uint32_t us_physics,
                                    uint32_t us_raymarch,
                                    uint32_t us_svg_dom,
                                    uint32_t us_broadcast);

bool flow_debug_log_verify_chain64(const FlowDebugLogEntry64 *ring,
                                   size_t count);

/*
 * ============================================================================
 * 9. Single-Pass DFA Markdown/LaTeX Parser & Zero-Font-Atlas 64B SDF Math Box
 * ============================================================================
 * Enforces Lean 4 Formal Invariants (Theorems 10, 11, 12):
 *   - 256-Byte ASCII Jump Table (Zero Regex Backtracking, O(N) Single-Pass)
 *   - Pandora/GitHub '$' Flanking Rule (immunizes '$100 ~ $200' & newline '$')
 *   - Identifier '_' / '*' Protection ('flow_svg_ui' needs zero double-escape)
 *   - 16-Bit Hashed Symbol Opcodes (\frac, \sqrt, \int, \sum, \alpha, \gamma...)
 *   - TeX Box-and-Glue Q16 Prefix-Sum Layout & Stretchy Analytic SDF Delimiters
 * ============================================================================
 */
#define FLOW_DFA_CHAR_PLAIN       0U /* Normal ASCII character / identifier */
#define FLOW_DFA_CHAR_DOLLAR      1U /* '$' Pandora/GitHub flanking math delimiter */
#define FLOW_DFA_CHAR_UNDERSCORE  2U /* '_' Subscript in math OR suppressed in C ident */
#define FLOW_DFA_CHAR_ASTERISK    3U /* '*' Multiply in math / emphasis outside ident */
#define FLOW_DFA_CHAR_BACKSLASH   4U /* '\' LaTeX macro starter (\frac, \sqrt, \int...) */
#define FLOW_DFA_CHAR_ENTITY      5U /* '<', '>', '&', '^', '{', '}' XML/TeX group & super */

/* 16-Bit Hashed Zero-String LaTeX Symbol & Stretchy Operator Opcodes */
#define FLOW_LATEX_OP_NONE        0x0000U
#define FLOW_LATEX_OP_GLYPH_VAR   0x0001U /* Algebraic variable / digit / literal char */
#define FLOW_LATEX_OP_FRAC        0x0101U /* \frac{A}{B} stretchy horizontal fraction bar */
#define FLOW_LATEX_OP_SQRT        0x0102U /* \sqrt{X} stretchy radical check + vinculum roof */
#define FLOW_LATEX_OP_INT         0x0103U /* \int stretchy integral sign */
#define FLOW_LATEX_OP_SUM         0x0104U /* \sum stretchy sigma summation operator */
#define FLOW_LATEX_OP_PAREN_L     0x0105U /* \left( stretchy left parenthesis arc */
#define FLOW_LATEX_OP_PAREN_R     0x0106U /* \right) stretchy right parenthesis arc */
#define FLOW_LATEX_OP_ALPHA       0x0201U /* \alpha Greek symbol */
#define FLOW_LATEX_OP_GAMMA       0x0202U /* \gamma Lorentz factor symbol */
#define FLOW_LATEX_OP_GE          0x0203U /* \ge (>=) relational operator */
#define FLOW_LATEX_OP_LE          0x0204U /* \le (<=) relational operator */
#define FLOW_LATEX_OP_BINARY_OP   0x0205U /* +, -, =, * binary operators */

#define FLOW_TEX_ROLE_BASELINE    0U /* Normal baseline box (scale = 1.0x [65536], dy = 0) */
#define FLOW_TEX_ROLE_SUPERSCRIPT 1U /* Superscript '^' (scale = 0.7x [45875], dy = -0.45h) */
#define FLOW_TEX_ROLE_SUBSCRIPT   2U /* Subscript '_'   (scale = 0.7x [45875], dy = +0.35h) */
#define FLOW_TEX_ROLE_FRAC_NUM    3U /* Fraction numerator {A} centered above bar (dy = -0.55h) */
#define FLOW_TEX_ROLE_FRAC_DEN    4U /* Fraction denominator {B} centered below bar (dy = +0.55h) */
#define FLOW_TEX_ROLE_RADICAND    5U /* Radical interior {X} under \sqrt vinculum bar */

#define FLOW_DFA_FLAG_MATH_MODE       (1U << 0)
#define FLOW_DFA_FLAG_STRETCHY        (1U << 1)
#define FLOW_DFA_FLAG_IDENT_PROTECTED (1U << 2)
#define FLOW_DFA_FLAG_LITERAL_DOLLAR  (1U << 3)

typedef struct __attribute__((aligned(64))) {
    uint16_t symbol_opcode;       /*  2B: FLOW_LATEX_OP_* 16-bit hashed symbol (0 string retention) */
    uint8_t  tex_role;            /*  1B: FLOW_TEX_ROLE_* (Baseline / Super / Sub / Num / Den / Rad) */
    uint8_t  dfa_flags;           /*  1B: FLOW_DFA_FLAG_* bitmask */
    uint32_t ascii_codepoint;     /*  4B: Literal ASCII char or macro hash seed */
    int32_t  box_x_q16;           /*  4B: Resolved TeX prefix-sum center X in Q16 */
    int32_t  box_y_q16;           /*  4B: Resolved TeX baseline + shift Y in Q16 */
    int32_t  half_w_q16;          /*  4B: Stretchy / glyph half-width in Q16 */
    int32_t  half_h_q16;          /*  4B: Stretchy / glyph half-height in Q16 */
    int32_t  advance_x_q16;       /*  4B: Compile-time horizontal glue advance in Q16 */
    int32_t  baseline_shift_q16;  /*  4B: Compile-time vertical shift dy in Q16 */
    int32_t  scale_q16;           /*  4B: 65536 (1.0x) or 45875 (0.7x super/sub/frac) */
    int32_t  stroke_q16;          /*  4B: Analytic SDF stroke thickness in Q16 */
    uint32_t color_rgb24;         /*  4B: 24-bit TrueColor fill/stroke */
    uint32_t parent_box_idx;      /*  4B: Enclosing \frac or \sqrt box index */
    uint64_t tile_mask_wave64;    /*  8B: Conservative 8x8 screen tile bitmask */
    uint64_t box_mac;             /*  8B: 64-bit deterministic TeX box integrity MAC */
} FlowLatexBoxNode64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowLatexBoxNode64, RicardianStateV4);
_Static_assert(sizeof(FlowLatexBoxNode64) == 64U, "FlowLatexBoxNode64 must be 64 bytes");
_Static_assert(_Alignof(FlowLatexBoxNode64) == 64U, "FlowLatexBoxNode64 must be 64-byte aligned");

extern const uint8_t FLOW_ASCII_DFA_JUMP_TABLE[256];

uint16_t flow_latex_hash_macro_single_pass(const char *src,
                                           size_t len,
                                           size_t *inout_cursor);

size_t flow_latex_dfa_compile_boxes64(const char *input,
                                      int32_t origin_x_q16,
                                      int32_t origin_y_q16,
                                      int32_t base_height_q16,
                                      uint32_t color_rgb24,
                                      FlowLatexBoxNode64 *out_boxes,
                                      size_t max_boxes,
                                      uint32_t *out_suppressed_ident_count,
                                      uint32_t *out_literal_dollar_count);

int32_t flow_latex_eval_box_sdf_q16(const FlowLatexBoxNode64 *box,
                                    int32_t sample_x_q16,
                                    int32_t sample_y_q16);

/*
 * ============================================================================
 * 10. 64-Byte Scripted Story Finisher ("劇情殺") & Scene Transition ("過渡動畫")
 *     One-Call Game Authoring Interface
 * ============================================================================
 * Enforces Lean 4 Formal Invariants (Theorems 15 & 16):
 *   - Plot-Armor HP Floor Gate (`story_execution_hp_floor_invariant`):
 *     Target/player HP is clamped >= `scripted_hp_floor_q16` during Anticipation
 *     so entities cannot die prematurely before the climax execution blow lands.
 *   - Zero-Pop-In Scene Wipe (`scene_transition_wipe_atomic_swap_bounded`):
 *     Wipe progress is strictly bounded in `[0, 65536]` and performs the atomic
 *     ticket/stage/seed swap at exact 100% occlusion (`FLOW_DIRECTOR_SCENE_TRANSITION`).
 * ============================================================================
 */
#define FLOW_DIRECTOR_IDLE              0U /* Normal gameplay (1.0x time, 0 wipe, unlocked input) */
#define FLOW_DIRECTOR_ANTICIPATION_LOCK 1U /* "劇情殺" Pre-strike lock: 2.39:1 letterbox, VCam blend, HP floor active */
#define FLOW_DIRECTOR_CLIMAX_EXECUTION  2U /* "劇情殺" Climax strike: hitstop, SDF X-Ray shatter, HP floor released */
#define FLOW_DIRECTOR_SCENE_TRANSITION  3U /* "過渡動畫" 100% Wipe Midpoint: atomic 0-pop-in stage/ticket swap */
#define FLOW_DIRECTOR_RECOVERY_SETTLE   4U /* "過渡動畫" Wipe opening: retract letterbox & restore 1.0x gameplay */

#define FLOW_TRANS_CINEMA_CUT           0U /* Instant frame-accurate smash cut + anamorphic flash */
#define FLOW_TRANS_LIQUID_GLASS_WAVE    1U /* Beer-Lambert refractive liquid-glass wave sweep */
#define FLOW_TRANS_IRIS_APERTURE        2U /* Circular SDF iris aperture close -> swap -> open */
#define FLOW_TRANS_RELATIVISTIC_WARP    3U /* Lorentz star-streak radial tunnel warp transition */

#define FLOW_INPUT_LOCK_NONE            0U /* All player controls active */
#define FLOW_INPUT_LOCK_MOVEMENT        1U /* Lock steering/throttle, allow camera/UI */
#define FLOW_INPUT_LOCK_FULL_CUTSCENE   3U /* Full cinematic lock during scripted finisher */
#define FLOW_INPUT_LOCK_QTE_WINDOW      4U /* Only finisher trigger chord / QTE active */

typedef struct __attribute__((aligned(64))) {
    uint32_t script_id;             /*  4B: Active story finisher or transition choreography ID */
    uint16_t director_phase;        /*  2B: FLOW_DIRECTOR_* state machine phase (0..4) */
    uint16_t transition_style;      /*  2B: FLOW_TRANS_* procedural GPU wipe style (0..3) */
    uint16_t input_lock_mask;       /*  2B: FLOW_INPUT_LOCK_* player input filter mask */
    uint16_t atomic_swap_completed; /*  2B: 1 once midpoint 0-pop-in scene swap has executed */
    uint32_t target_prefab_id;      /*  4B: Target boss/entity Prefab ID for scripted execution */
    uint16_t target_vcam_id;        /*  2B: Locked VCam ID during finisher (0..3) */
    uint8_t  next_ticket_preset_id; /*  1B: Next FlowObjectTicket64 preset swapped at midpoint */
    uint8_t  next_stage_layer;      /*  1B: Next FlowStageLayer swapped atomically at midpoint */
    uint32_t phase_elapsed_ms;      /*  4B: Elapsed milliseconds in current director phase */
    uint32_t phase_duration_ms;     /*  4B: Target duration of current phase in milliseconds */
    int32_t  wipe_progress_q16;     /*  4B: Procedural transition wipe occlusion [0..65536] */
    int32_t  letterbox_aspect_q16;  /*  4B: 2.39:1 CinemaScope letterbox bar envelope [0..65536] */
    int32_t  time_dilation_q16;     /*  4B: Relativistic hitstop / slow-mo scale (65536 = 1.0x) */
    int32_t  scripted_hp_floor_q16; /*  4B: Plot-armor HP floor (>= 1 in lock, 0 at climax blow) */
    int32_t  xray_cutaway_q16;      /*  4B: Automatic SDF X-Ray internal core shatter radius */
    uint64_t world_facts_delta;     /*  8B: Narrative world-fact bitmask unlocked on completion */
    uint64_t director_mac;          /*  8B: 64-bit deterministic choreography integrity MAC */
} FlowNarrativeDirector64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowNarrativeDirector64, RicardianStateV4);
_Static_assert(sizeof(FlowNarrativeDirector64) == 64U, "FlowNarrativeDirector64 must be 64 bytes");
_Static_assert(_Alignof(FlowNarrativeDirector64) == 64U, "FlowNarrativeDirector64 must be 64-byte aligned");

void flow_director_init64(FlowNarrativeDirector64 *dir);

/* One-Call Authoring API: Trigger Scripted Story Finisher ("劇情殺") */
void flow_director_trigger_story_finisher64(FlowNarrativeDirector64 *dir,
                                            uint32_t script_id,
                                            uint32_t target_prefab_id,
                                            uint32_t vcam_id,
                                            uint16_t transition_style,
                                            int32_t hp_floor_q16);

/* One-Call Authoring API: Trigger Standalone Scene Transition ("過渡動畫") */
void flow_director_trigger_scene_transition64(FlowNarrativeDirector64 *dir,
                                              uint32_t next_ticket_preset_id,
                                              uint32_t next_stage_layer,
                                              uint16_t transition_style,
                                              uint32_t duration_ms);

/* Plot-Armor / Scripted Finisher HP Floor Gate (Enforces Theorem 15) */
int32_t flow_director_eval_hp_gate_q16(const FlowNarrativeDirector64 *dir,
                                       int32_t raw_hp_q16);

/* Deterministic Choreography Stepper (Enforces Theorem 16 & returns true on atomic swap frame) */
bool flow_director_step_choreography64(FlowNarrativeDirector64 *dir,
                                       uint32_t dt_ms,
                                       uint32_t *out_active_vcam_id,
                                       int32_t *out_trauma_boost_q16,
                                       uint32_t *inout_ticket_preset_id,
                                       uint32_t *inout_stage_layer);

/*
 * ============================================================================
 * 11. 64-Byte Flexbox / Stack Auto-Layout Container, Generic Register Bank &
 *     Interactive Text Input Widget State (General-Purpose Native App UI)
 * ============================================================================
 * Enforces Lean 4 Formal Invariants (Theorems 13, 14, 17, 18):
 *   - Row/Column Flex/Stack Monotonic Layout (`flex_stack_layout_monotonic_advance`)
 *   - Standard SVG viewBox -> Q16 Normalization (`svg_viewbox_q16_normalization_bounded`)
 *   - 64-Byte Inline Text Input Bounds Safety (`ui_text_input_cursor_in_bounds`)
 *   - Generic Register Bank Channel Lookup (`ui_register_bank_channel_lookup_sound`)
 * ============================================================================
 */
#define FLOW_UI_LAYOUT_ROW             0U
#define FLOW_UI_LAYOUT_COLUMN          1U

#define FLOW_UI_JUSTIFY_START          0U
#define FLOW_UI_JUSTIFY_CENTER         1U
#define FLOW_UI_JUSTIFY_END            2U
#define FLOW_UI_JUSTIFY_SPACE_BETWEEN  3U

#define FLOW_UI_ALIGN_START            0U
#define FLOW_UI_ALIGN_CENTER           1U
#define FLOW_UI_ALIGN_END              2U
#define FLOW_UI_ALIGN_STRETCH          3U

#define FLOW_UI_OVERFLOW_VISIBLE       0U
#define FLOW_UI_OVERFLOW_SCROLL_CLAMP  1U

#define FLOW_UI_REGISTER_CHANNELS      12U

typedef struct __attribute__((aligned(64))) {
    uint16_t direction;             /*  2B: FLOW_UI_LAYOUT_ROW (0) or COLUMN (1) */
    uint16_t justify_mode;          /*  2B: FLOW_UI_JUSTIFY_* along main axis */
    uint16_t align_items;           /*  2B: FLOW_UI_ALIGN_* along cross axis */
    uint16_t overflow_mode;         /*  2B: FLOW_UI_OVERFLOW_* scroll clamping mode */
    int32_t  origin_x_q16;          /*  4B: Container center X in Q16 */
    int32_t  origin_y_q16;          /*  4B: Container center Y in Q16 */
    int32_t  half_w_q16;            /*  4B: Container half-width in Q16 */
    int32_t  half_h_q16;            /*  4B: Container half-height in Q16 */
    int32_t  padding_q16;           /*  4B: Inner container padding in Q16 */
    int32_t  gap_q16;               /*  4B: Gap between consecutive children in Q16 */
    int32_t  scroll_offset_q16;     /*  4B: Current main-axis scroll offset in Q16 */
    int32_t  content_extent_q16;    /*  4B: Total computed child extent along main axis */
    int32_t  max_scroll_q16;        /*  4B: Maximum valid scroll offset >= 0 */
    uint32_t child_count;           /*  4B: Number of child nodes laid out in pass */
    uint64_t container_tile_mask;   /*  8B: Combined Wave64 screen tile bitmask of children */
    uint64_t layout_mac;            /*  8B: Deterministic 64-bit layout state MAC */
} FlowUiLayoutContainer64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowUiLayoutContainer64, RicardianStateV4);
_Static_assert(sizeof(FlowUiLayoutContainer64) == 64U, "FlowUiLayoutContainer64 must be 64 bytes");
_Static_assert(_Alignof(FlowUiLayoutContainer64) == 64U, "FlowUiLayoutContainer64 must be 64-byte aligned");

typedef struct __attribute__((aligned(64))) {
    int32_t  channels_q16[FLOW_UI_REGISTER_CHANNELS]; /* 48B: 12 generic Q16 state channels [0..11] */
    uint32_t dirty_channel_mask;                      /*  4B: Bitmask of channels updated since init */
    uint32_t epoch_tick;                              /*  4B: Monotonic register bank update tick */
    uint64_t bank_mac;                                /*  8B: Deterministic 64-bit register bank MAC */
} FlowUiRegisterBank64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowUiRegisterBank64, RicardianStateV4);
_Static_assert(sizeof(FlowUiRegisterBank64) == 64U, "FlowUiRegisterBank64 must be 64 bytes");
_Static_assert(_Alignof(FlowUiRegisterBank64) == 64U, "FlowUiRegisterBank64 must be 64-byte aligned");

#define FLOW_UI_TEXT_INPUT_CAP 39U

typedef struct __attribute__((aligned(64))) {
    char     utf8_buf[40];          /* 40B: Inline null-terminated UTF-8/ASCII buffer (max 39 chars) */
    uint8_t  byte_len;              /*  1B: Active string length in bytes [0..39] */
    uint8_t  cursor_byte_pos;       /*  1B: Caret byte offset in [0..byte_len] */
    uint8_t  selection_anchor_pos;  /*  1B: Selection anchor offset in [0..byte_len] */
    uint8_t  focused;               /*  1B: 1 if input widget currently holds keyboard focus */
    uint32_t bound_node_id;         /*  4B: Target FlowSvgUiNode64.node_id synchronized on edit */
    uint32_t action_id;             /*  4B: Action opcode dispatched on Enter/Submit */
    uint32_t edit_revision;         /*  4B: Monotonic text edit counter */
    uint64_t content_hash;          /*  8B: Deterministic 64-bit text & caret integrity hash */
} FlowUiTextInput64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowUiTextInput64, RicardianStateV4);
_Static_assert(sizeof(FlowUiTextInput64) == 64U, "FlowUiTextInput64 must be 64 bytes");
_Static_assert(_Alignof(FlowUiTextInput64) == 64U, "FlowUiTextInput64 must be 64-byte aligned");

/* Normalize standard SVG coordinate [0..view_extent] into Q16 [0..65536] (0 FloatOps) */
int32_t flow_svg_ui_norm_viewbox_coord_q16(int32_t coord_px, int32_t view_extent_px);

/* Initialize & Execute O(N) 64-Byte Flexbox / Stack Auto-Layout Pass (0 FloatOps, 0 Heap) */
void flow_ui_layout_container_init64(FlowUiLayoutContainer64 *container,
                                     uint16_t direction,
                                     uint16_t justify_mode,
                                     uint16_t align_items,
                                     int32_t origin_x_q16,
                                     int32_t origin_y_q16,
                                     int32_t half_w_q16,
                                     int32_t half_h_q16,
                                     int32_t padding_q16,
                                     int32_t gap_q16);

void flow_ui_layout_container_apply64(FlowUiLayoutContainer64 *container,
                                      FlowSvgUiNode64 *nodes,
                                      size_t node_count,
                                      int32_t scroll_delta_q16);

/* Generic 12-Channel Telemetry Register Bank API */
void flow_ui_register_bank_init64(FlowUiRegisterBank64 *bank);
void flow_ui_register_bank_set_q16(FlowUiRegisterBank64 *bank,
                                   uint32_t channel_id,
                                   int32_t value_q16);
int32_t flow_ui_register_bank_get_q16(const FlowUiRegisterBank64 *bank,
                                      uint32_t channel_id);
void flow_svg_ui_bind_register_bank64(FlowSvgUiNode64 *nodes,
                                      size_t node_count,
                                      const FlowUiRegisterBank64 *bank);

/* 64-Byte Interactive Text Input Widget API */
void flow_ui_text_input_init64(FlowUiTextInput64 *input,
                               uint32_t bound_node_id,
                               uint32_t action_id,
                               const char *initial_text);
bool flow_ui_text_input_insert_char64(FlowUiTextInput64 *input,
                                      char ch,
                                      FlowSvgUiNode64 *bound_node);
bool flow_ui_text_input_backspace64(FlowUiTextInput64 *input,
                                    FlowSvgUiNode64 *bound_node);
void flow_ui_text_input_move_cursor64(FlowUiTextInput64 *input,
                                      int32_t delta);

#endif /* FLOW_SVG_UI_H */

