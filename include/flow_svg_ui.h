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

#define FLOW_SVG_UI_MAX_NODES 8U

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
 * 11. 64-Byte Historical Issue Torture & Comparative Evaluation Harness
 *     (Replays & Crushes anthropics/claude-code Historical Pain Points)
 * ============================================================================
 * Enforces Lean 4 Formal Invariants (Theorems 1, 2, 10, 11, 17, 18):
 *   - Category 1 (FLOW_ISSUE_CAT_OOM_SESSION): Long-session V8 heap OOM
 *     (#2099, #3178, #10592, #19100) -> O(1) 64B Ring Arena (0 Heap Calls).
 *   - Category 2 (FLOW_ISSUE_CAT_INK_FLICKER): React-Ink full-terminal clear &
 *     Flexbox reflow CPU spikes -> O(1) Q16 Anchor & Register Binding (0 Reflow).
 *   - Category 3 (FLOW_ISSUE_CAT_REGEX_STALL): Markdown/Regex/Segmenter stalls
 *     & streaming offset bugs (#21153) -> 256B ASCII Jump-Table DFA (0 Backtrack).
 *   - Category 4 (FLOW_ISSUE_CAT_TOOL_ZOMBIE): Hung background tool 100% CPU
 *     spin & glob permission bypass (#22198) -> 1-Tick Watchdog & Bitmask Gate.
 * ============================================================================
 */
#define FLOW_ISSUE_CAT_OOM_SESSION  (1U << 0) /* #2099/#3178/#10592/#19100 Heap OOM */
#define FLOW_ISSUE_CAT_INK_FLICKER  (1U << 1) /* Ink clearTerminal/Reflow Flicker */
#define FLOW_ISSUE_CAT_REGEX_STALL  (1U << 2) /* #21153 Markdown/Regex/Segmenter Stall */
#define FLOW_ISSUE_CAT_TOOL_ZOMBIE  (1U << 3) /* #22198 Hung Tool Spin & Glob Bypass */
#define FLOW_ISSUE_CAT_ALL_FOUR     0x0FU

typedef struct __attribute__((aligned(64))) {
    uint32_t issue_category_mask;        /*  4B: Bitmask of replayed historical issue classes (0x0F) */
    uint32_t replayed_session_events;    /*  4B: Total long-session events replayed in-place */
    uint32_t peak_arena_bytes;           /*  4B: Peak ring arena footprint in bytes (64 * cap) */
    uint32_t heap_alloc_calls;           /*  4B: Dynamic heap allocations executed (strictly 0) */
    uint32_t viewport_reflow_mutations;  /*  4B: DOM node geometry mutations on resize (strictly 0) */
    uint32_t dfa_adversarial_chars;      /*  4B: Adversarial Markdown/LaTeX chars parsed in 1 pass */
    uint32_t dfa_backtrack_steps;        /*  4B: Regex/DFA backtracking steps (strictly 0) */
    uint32_t watchdog_reclaimed_count;   /*  4B: Hung tool steps reclaimed in 1 tick by watchdog */
    uint32_t permission_bypass_blocked;  /*  4B: Flag-reordered / compound permission bypasses blocked */
    uint32_t lean_theorems_verified;     /*  4B: Count of formal Lean 4 invariants enforced (18) */
    uint32_t p99_step_cost_ops;          /*  4B: Deterministic worst-case O(1) ops per event step */
    uint32_t hash_chain_verified;        /*  4B: 1 if cryptographic flight-recorder ring verified */
    uint64_t trajectory_equivalence_mac; /*  8B: Deterministic trajectory hash across all replays */
    uint64_t audit_seal_mac;             /*  8B: 64-bit cryptographic audit report seal */
} FlowIssueAuditReport64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowIssueAuditReport64, RicardianStateV4);
_Static_assert(sizeof(FlowIssueAuditReport64) == 64U, "FlowIssueAuditReport64 must be 64 bytes");
_Static_assert(_Alignof(FlowIssueAuditReport64) == 64U, "FlowIssueAuditReport64 must be 64-byte aligned");

void flow_svg_ui_run_historical_issue_torture64(FlowIssueAuditReport64 *out_report,
                                                uint32_t session_event_count);

#endif /* FLOW_SVG_UI_H */

