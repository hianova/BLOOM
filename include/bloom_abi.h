#ifndef BLOOM_ABI_H
#define BLOOM_ABI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * BLOOM v6.0.0 — Unified 64-Byte Cache-Line Wire, Unified-Memory & GPU ABI
 * ============================================================================
 * Every runtime, scene, prefab, UI, and .bloom mmap block is strictly 64 bytes
 * (16 x 32-bit words / 8 x 64-bit lanes), aligned to 64 bytes, with 0 padding
 * holes and 32-bit pair (lo32, hi32) Wave64 masks for WebGPU WGSL portability.
 *
 * The complete BloomEngineArena occupies 256 x 64B = 16,384 Bytes (exact 16KB
 * hardware VM page), enabling 0-memcpy in-process sharing across Lean 4 JSI,
 * C17 libbloom.a, and FLOW v6.0.0, and deterministic GPU compute execution via
 * single-source WGSL + SDL3 GPU (Metal MSL 2.4 & Vulkan SPIR-V).
 * ============================================================================
 */

#if defined(_MSC_VER)
#define BLOOM_ALIGN64 __declspec(align(64))
#else
#define BLOOM_ALIGN64 __attribute__((aligned(64)))
#endif

#if defined(__cplusplus)
#define BLOOM_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#define BLOOM_STATIC_ASSERT64(T)                                               \
    static_assert(sizeof(T) == 64U, #T " must be exactly 64 bytes");           \
    static_assert(alignof(T) == 64U, #T " must be 64-byte aligned")
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
#define BLOOM_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#define BLOOM_STATIC_ASSERT64(T)                                               \
    _Static_assert(sizeof(T) == 64U, #T " must be exactly 64 bytes");          \
    _Static_assert(_Alignof(T) == 64U, #T " must be 64-byte aligned")
#else
#error "BLOOM requires ISO C11+ or ISO C++11+ for 64-byte ABI static_assert/alignof"
#endif

#define BLOOM_MAGIC_U32       0x424C4D36U /* "BLM6" */
#define BLOOM_VERSION_U32     0x00060000U /* v6.0.0 */
#define BLOOM_Q16_SHIFT       16
#define BLOOM_Q16_ONE         65536
#define BLOOM_MAX_Q16         0x7FFFFFFF

/* 256 x 64B = 16,384 Bytes (1 Apple Silicon VM Page, Z/256Z OOB-immune mask) */
#define BLOOM_ARENA_CELLS     256U
#define BLOOM_ARENA_CAP_MASK  255U
#define BLOOM_ARENA_BYTES     ((size_t)BLOOM_ARENA_CELLS * 64ULL)
#define BLOOM_VM_PAGE_ALIGN   16384U

/* Canonical Arena Cell Slot Map [0..255] */
#define BLOOM_SLOT_HEADER         0U
#define BLOOM_SLOT_CAMERA         1U
#define BLOOM_SLOT_REGBANK        2U
#define BLOOM_SLOT_DOM            3U
#define BLOOM_SLOT_FLEX           4U
#define BLOOM_SLOT_TEXT_INPUT     5U
#define BLOOM_SLOT_IME            6U
#define BLOOM_SLOT_WINDOW         7U

#define BLOOM_SLOT_MATTER_BASE    8U   /* 8 slots:   8..15  */
#define BLOOM_MAX_MATTERS         8U

#define BLOOM_SLOT_STYLE_BASE     16U  /* 8 slots:  16..23  */
#define BLOOM_MAX_STYLES          8U

#define BLOOM_SLOT_PREFAB_BASE    24U  /* 16 slots: 24..39  */
#define BLOOM_MAX_PREFABS         16U

#define BLOOM_SLOT_INST_BASE      40U  /* 32 slots: 40..71  */
#define BLOOM_MAX_INSTANCES       32U

#define BLOOM_SLOT_CSG_BASE       72U  /* 32 slots: 72..103 */
#define BLOOM_MAX_CSG_OPS         32U

#define BLOOM_SLOT_BVH_BASE       104U /* 32 slots: 104..135 */
#define BLOOM_MAX_BVH_NODES       32U

#define BLOOM_SLOT_JOINT_BASE     136U /* 8 slots:  136..143 */
#define BLOOM_MAX_JOINTS          8U

#define BLOOM_SLOT_BRICK_BASE     144U /* 8 slots:  144..151 */
#define BLOOM_MAX_BRICKS          8U

#define BLOOM_SLOT_UI_BASE        152U /* 16 slots: 152..167 */
#define BLOOM_MAX_UI_NODES        16U

#define BLOOM_SLOT_GLYPH_BASE     168U /* 16 slots: 168..183 */
#define BLOOM_MAX_GLYPHS          16U

#define BLOOM_SLOT_LATEX_BASE     184U /* 16 slots: 184..199 */
#define BLOOM_MAX_LATEX_BOXES     16U

#define BLOOM_SLOT_FLOW_STATE_BASE 200U /* 32 slots: 200..231 */
#define BLOOM_MAX_FLOW_STATES      32U

#define BLOOM_SLOT_FLOW_CHART_BASE 232U /* 16 slots: 232..247 (2^4, mask 15) */
#define BLOOM_MAX_FLOW_CHARTS      16U
#define BLOOM_FLOW_CHART_MASK      15ULL

#define BLOOM_SLOT_RAYCAST_HIT    248U
#define BLOOM_SLOT_IMAGE_HDR      249U

/* Dedicated FLOW v6.0.0 State Channel Assignments within [0..31] */
#define BLOOM_FLOW_CH_CAM_YAW     0U
#define BLOOM_FLOW_CH_CAM_PITCH   1U
#define BLOOM_FLOW_CH_CAM_DIST    2U
#define BLOOM_FLOW_CH_CAM_FOCUS   3U
#define BLOOM_FLOW_CH_UI_NODE0    4U  /* 4..19: 16 UI Node 2-Jet Micro-Springs */
#define BLOOM_FLOW_CH_XRAY_PLANE  20U
#define BLOOM_FLOW_CH_SCROLL      21U
#define BLOOM_FLOW_CH_PREFAB0     22U /* 22..31: Prefab / App Dynamical States */

/* Dedicated FLOW v6.0.0 Chart Indices within [0..15] */
#define BLOOM_CHART_UI_SPRING     0U
#define BLOOM_CHART_UI_SCRUB      1U
#define BLOOM_CHART_CAMERA_RIG    2U
#define BLOOM_CHART_XRAY_SWEEP    3U
#define BLOOM_CHART_PREFAB_NORMAL 4U
#define BLOOM_CHART_PREFAB_ALERT  5U

/* Frame Flags */
#define BLOOM_FLAG_IDLE_SLEEP      (1U << 0)
#define BLOOM_FLAG_XRAY_ENABLED    (1U << 1)
#define BLOOM_FLAG_TRC_SAFE        (1U << 2)
#define BLOOM_FLAG_COLORBLIND_GEOM (1U << 3)

/* 0. Frame & Scene Header (Slot 0, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t magic;
    uint32_t version;
    uint32_t width_px;
    uint32_t height_px;
    int32_t  dpi_scale_q16;
    uint32_t frame_seq;
    uint32_t inst_count;
    uint32_t csg_count;
    uint32_t bvh_count;
    uint32_t ui_count;
    uint32_t glyph_count;
    uint32_t latex_count;
    int32_t  xray_plane_q16;
    uint32_t selected_inst_id;
    uint32_t flags;
    uint32_t cap_mask;
} BloomFrameHeader64;
BLOOM_STATIC_ASSERT64(BloomFrameHeader64);

/* 1. Thin-Lens Physical & Orbit Camera Descriptor (Slot 1, 64B) */
typedef struct BLOOM_ALIGN64 {
    int32_t eye_q16[3];
    int32_t target_q16[3];
    int32_t up_q16[3];
    int32_t focal_mm_q16;
    int32_t aperture_fstop_q16;
    int32_t focus_dist_q16;
    int32_t exposure_q16;
    int32_t orbit_yaw_q16;
    int32_t orbit_pitch_q16;
    int32_t orbit_dist_q16;
} BloomCamera64;
BLOOM_STATIC_ASSERT64(BloomCamera64);

/* 2. Zero-VDOM Reactive Register Bank (Slot 2, 64B) */
#define BLOOM_REG_CHANNELS 12U
typedef struct BLOOM_ALIGN64 {
    int32_t  channels_q16[BLOOM_REG_CHANNELS];
    uint32_t dirty_mask;
    uint32_t epoch;
    uint32_t mac_lo32;
    uint32_t mac_hi32;
} BloomRegBank64;
BLOOM_STATIC_ASSERT64(BloomRegBank64);

/* 3. Declarative UI DOM & Wave64 Interaction State (Slot 3, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t compiled_node_count;
    uint32_t hovered_node_id;
    uint32_t active_node_id;
    uint32_t captured_node_id;
    uint32_t last_action_opcode;
    int32_t  cursor_norm_x_q16;
    int32_t  cursor_norm_y_q16;
    uint32_t click_dispatch_count;
    uint32_t wave64_cull_skips;
    uint32_t hot_reload_epoch;
    uint32_t focused_input_id;
    uint32_t reserved0;
    uint32_t dom_mask_lo32;
    uint32_t dom_mask_hi32;
    uint32_t mac_lo32;
    uint32_t mac_hi32;
} BloomDomState64;
BLOOM_STATIC_ASSERT64(BloomDomState64);

/* 4. O(N) Flexbox / Stack Auto-Layout Container (Slot 4, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint16_t direction;
    uint16_t justify_mode;
    uint16_t align_items;
    uint16_t overflow_mode;
    int32_t  origin_x_q16;
    int32_t  origin_y_q16;
    int32_t  half_w_q16;
    int32_t  half_h_q16;
    int32_t  padding_q16;
    int32_t  gap_q16;
    int32_t  scroll_offset_q16;
    int32_t  content_extent_q16;
    int32_t  max_scroll_q16;
    uint32_t child_count;
    uint32_t tile_mask_lo32;
    uint32_t tile_mask_hi32;
    uint32_t mac_lo32;
    uint32_t mac_hi32;
} BloomFlexContainer64;
BLOOM_STATIC_ASSERT64(BloomFlexContainer64);

/* 5. Inline UTF-8 Interactive Text Input Widget (Slot 5, 64B) */
#define BLOOM_TEXT_INPUT_CAP 39U
typedef struct BLOOM_ALIGN64 {
    char     utf8_buf[40];
    uint8_t  byte_len;
    uint8_t  cursor_pos;
    uint8_t  anchor_pos;
    uint8_t  focused;
    uint32_t bound_node_id;
    uint32_t submit_action_id;
    uint32_t edit_revision;
    uint32_t hash_lo32;
    uint32_t hash_hi32;
} BloomTextInput64;
BLOOM_STATIC_ASSERT64(BloomTextInput64);

/* 6. CJK / Phonetic OS IME Preedit & Candidate Bridge (Slot 6, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t target_node_id;
    uint8_t  preedit_len;
    uint8_t  cursor_offset;
    uint8_t  candidate_index;
    uint8_t  ime_active;
    char     preedit_utf8[36];
    int32_t  anchor_x_q16;
    int32_t  anchor_y_q16;
    uint32_t committed_count;
    uint32_t mac_lo32;
    uint32_t mac_hi32;
} BloomIme64;
BLOOM_STATIC_ASSERT64(BloomIme64);

/* 7. Cross-Platform HiDPI Window & Input Event Descriptor (Slot 7, 64B) */
#define BLOOM_WIN_FLAG_VSYNC        (1U << 0)
#define BLOOM_WIN_FLAG_HDR          (1U << 1)
#define BLOOM_WIN_FLAG_CLOSED       (1U << 2)
#define BLOOM_WIN_FLAG_MOUSE_DOWN   (1U << 3)
#define BLOOM_WIN_FLAG_JUST_CLICKED (1U << 4)
#define BLOOM_WIN_FLAG_RIGHT_CLICK  (1U << 5)

typedef struct BLOOM_ALIGN64 {
    uint32_t backend_api;
    uint32_t logical_w_px;
    uint32_t logical_h_px;
    uint32_t fb_w_px;
    uint32_t fb_h_px;
    int32_t  dpi_scale_q16;
    uint32_t target_fps;
    uint32_t host_flags;
    int32_t  pointer_x_q16;
    int32_t  pointer_y_q16;
    int32_t  drag_dx_q16;
    int32_t  drag_dy_q16;
    int32_t  scroll_dy_q16;
    uint32_t key_code;
    uint32_t frame_epoch_lo32;
    uint32_t frame_epoch_hi32;
} BloomWindow64;
BLOOM_STATIC_ASSERT64(BloomWindow64);

/* 8. Constitutive Physical Matter State (Slots 8..15, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t matter_id;
    int32_t  ior_eta_q16;
    int32_t  extinction_kappa_q16;
    int32_t  viscosity_quench_q16;
    int32_t  surface_relief_q16;
    int32_t  micro_roughness_q16;
    int32_t  mie_scatter_q16;
    int32_t  absorb_rgb_q16[3];
    int32_t  lattice_wire_q16;
    int32_t  thickness_mm_q16;
    uint32_t thermal_kelvin;
    uint32_t density_kg_m3;
    uint32_t mac_lo32;
    uint32_t mac_hi32;
} BloomMatter64;
BLOOM_STATIC_ASSERT64(BloomMatter64);

/* 9. Canonical Art Bible Optical Style (Slots 16..23, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t style_id;
    uint32_t base_rgb24;
    uint32_t accent_rgb24;
    int32_t  roughness_q16;
    int32_t  metallic_q16;
    int32_t  emissive_gain_q16;
    int32_t  sdf_noise_freq_q16;
    int32_t  sdf_noise_amp_q16;
    int32_t  msdf_stroke_q16;
    int32_t  strobe_hz_q16;
    int32_t  thermal_shift_q16;
    uint32_t flags;
    uint32_t matter_slot;
    uint32_t reserved0;
    uint32_t mac_lo32;
    uint32_t mac_hi32;
} BloomArtStyle64;
BLOOM_STATIC_ASSERT64(BloomArtStyle64);

/* 10. Prefab SSOT Blueprint Descriptor (Slots 24..39, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t prefab_id;
    uint32_t art_style_id;
    uint32_t matter_id;
    uint32_t csg_op_mask;
    int32_t  bounding_radius_q16;
    int32_t  prim_params_q16[6];
    uint32_t csg_start_idx;
    uint32_t csg_count;
    uint32_t flags;
    uint32_t hash_lo32;
    uint32_t hash_hi32;
} BloomPrefabDesc64;
BLOOM_STATIC_ASSERT64(BloomPrefabDesc64);

/* 11. Live 3D Prefab Scene Instance (Slots 40..71, 64B) */
#define BLOOM_INST_FLAG_ACTIVE      (1U << 0)
#define BLOOM_INST_FLAG_SELECTED    (1U << 1)
#define BLOOM_INST_FLAG_OVERRIDDEN  (1U << 2)
#define BLOOM_INST_FLAG_COLLIDABLE  (1U << 3)

typedef struct BLOOM_ALIGN64 {
    uint32_t inst_id;
    uint32_t prefab_id;
    uint32_t style_override_id;
    uint32_t matter_override_id;
    int32_t  pos_q16[3];
    int32_t  rot_euler_q16[3];
    int32_t  scale_q16;
    uint32_t thermal_kelvin;
    uint16_t active_layer;
    uint16_t flags;
    uint32_t flow_slot;
    int32_t  param_override_q16;
    uint32_t reserved0;
} BloomPrefabInst64;
BLOOM_STATIC_ASSERT64(BloomPrefabInst64);

/* 12. Data-Driven 3D CSG / .glb Primitive Operation (Slots 72..103, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t op_id;
    uint32_t prim_type;
    uint32_t bool_op;
    uint32_t matter_id;
    int32_t  pos_q16[3];
    int32_t  extents_q16[3];
    int32_t  smooth_k_q16;
    int32_t  bevel_r_q16;
    uint32_t stage_mask_lo32;
    uint32_t stage_mask_hi32;
    uint32_t mac_lo32;
    uint32_t mac_hi32;
} BloomCsgOp64;
BLOOM_STATIC_ASSERT64(BloomCsgOp64);

/* 13. Pointer-Free 60-Bit Morton Linear BVH Node (Slots 104..135, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t node_id;
    uint32_t left_or_first;
    uint32_t right_or_count;
    uint32_t is_leaf;
    int32_t  aabb_min_q16[3];
    int32_t  aabb_max_q16[3];
    uint32_t morton_lo32;
    uint32_t morton_hi32;
    uint32_t stage_mask_lo32;
    uint32_t stage_mask_hi32;
    uint32_t mac_lo32;
    uint32_t mac_hi32;
} BloomBvhNode64;
BLOOM_STATIC_ASSERT64(BloomBvhNode64);

/* 14. Volume-Preserving Dual-Quaternion Skeletal Joint (Slots 136..143, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t joint_id;
    uint32_t parent_id;
    int32_t  q_real_q16[4];
    int32_t  q_dual_q16[4];
    int32_t  bone_len_q16;
    int32_t  bone_radius_q16;
    int32_t  smooth_k_q16;
    uint32_t matter_id;
    uint32_t mac_lo32;
    uint32_t mac_hi32;
} BloomJointDq64;
BLOOM_STATIC_ASSERT64(BloomJointDq64);

/* 15. Sparse 4x4x4 4-Bit SDF Voxel Brick (Slots 144..151, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t brick_id;
    uint32_t matter_id;
    int32_t  origin_q16[3];
    int32_t  voxel_step_q16;
    uint32_t sdf_words[8];
    uint32_t mac_lo32;
    uint32_t mac_hi32;
} BloomSdfBrick64;
BLOOM_STATIC_ASSERT64(BloomSdfBrick64);

/* 16. Declarative SVG / Flexbox / Auto-Bento UI Node (Slots 152..167, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t node_id;
    uint32_t prim_type;
    uint32_t anchor_mode;
    uint32_t bind_channel;
    int32_t  offset_x_q16;
    int32_t  offset_y_q16;
    int32_t  half_w_q16;
    int32_t  half_h_q16;
    int32_t  corner_radius_q16;
    int32_t  scale_jet_v_q16;
    int32_t  scale_jet_d1_q16;
    int32_t  bound_value_q16;
    uint32_t style_and_state;
    uint32_t action_opcode;
    uint32_t tile_mask_lo32;
    uint32_t tile_mask_hi32;
} BloomUiNode64;
BLOOM_STATIC_ASSERT64(BloomUiNode64);

/* 17. Vector <line> / <path> SDF Icon Glyph (Slots 168..183, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t glyph_id;
    uint32_t stroke_count;
    int32_t  stroke_width_q16;
    uint32_t stroke_rgb24;
    int16_t  seg_coords_q8[20];
    uint32_t mac_lo32;
    uint32_t mac_hi32;
} BloomGlyph64;
BLOOM_STATIC_ASSERT64(BloomGlyph64);

/* 18. Zero-Font-Atlas Single-Pass DFA Analytic SDF LaTeX Box (Slots 184..199, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t box_id;
    uint32_t box_kind;
    uint32_t codepoint_or_op;
    uint32_t tint_rgb24;
    int32_t  x_q16;
    int32_t  y_q16;
    int32_t  half_w_q16;
    int32_t  half_h_q16;
    int32_t  stroke_q16;
    int32_t  baseline_shift_q16;
    uint32_t parent_panel_id;
    uint32_t flags;
    uint32_t tile_mask_lo32;
    uint32_t tile_mask_hi32;
    uint32_t mac_lo32;
    uint32_t mac_hi32;
} BloomLatexBox64;
BLOOM_STATIC_ASSERT64(BloomLatexBox64);

/* 19. Zero-Coordinate Auto-Bento Semantic Intent Spec (64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t intent_id;
    uint32_t bind_channel;
    uint32_t action_opcode;
    uint32_t priority_weight;
    int32_t  lo_exit_q16;
    int32_t  hi_enter_q16;
    int32_t  current_val_q16;
    uint32_t optical_state;
    uint32_t prim_type;
    uint32_t glass_mode;
    uint32_t reserved[4];
    uint32_t mac_lo32;
    uint32_t mac_hi32;
} BloomIntent64;
BLOOM_STATIC_ASSERT64(BloomIntent64);

/* 20. 3D Raycaster Pick Result Manifold (Slot 248, 64B) */
typedef struct BLOOM_ALIGN64 {
    uint32_t hit_valid;
    uint32_t hit_inst_id;
    uint32_t hit_prefab_id;
    uint32_t bvh_nodes_visited;
    int32_t  t_hit_q16;
    int32_t  sdf_dist_q16;
    int32_t  hit_pos_q16[3];
    int32_t  hit_normal_q16[3];
    int32_t  ray_origin_q16[2];
    uint32_t mac_lo32;
    uint32_t mac_hi32;
} BloomRaycastHit64;
BLOOM_STATIC_ASSERT64(BloomRaycastHit64);

/* 21. Unified .bloom Memory-Mapped Container Superblock (Slot 249, 64B) */
#define BLOOM_IMAGE_MAGIC_LO 0x4D4F4C42U /* "BLOM" */
#define BLOOM_IMAGE_MAGIC_HI 0x36474D49U /* "IMG6" */

typedef struct BLOOM_ALIGN64 {
    uint32_t magic_lo32;
    uint32_t magic_hi32;
    uint32_t block_cap_mask;
    uint32_t version;
    uint32_t prefab_off;
    uint32_t prefab_count;
    uint32_t csg_off;
    uint32_t csg_count;
    uint32_t ui_off;
    uint32_t ui_count;
    uint32_t flow_chart_off;
    uint32_t flow_chart_count;
    uint32_t schema_lo32;
    uint32_t schema_hi32;
    uint32_t merkle_lo32;
    uint32_t merkle_hi32;
} BloomImageHeader64;
BLOOM_STATIC_ASSERT64(BloomImageHeader64);

/* Universal 64-Byte Cell Union */
typedef union BLOOM_ALIGN64 {
    uint8_t             bytes[64];
    uint32_t            u32[16];
    uint64_t            u64[8];
    BloomFrameHeader64  frame_hdr;
    BloomCamera64       camera;
    BloomRegBank64      reg_bank;
    BloomDomState64     dom;
    BloomFlexContainer64 flex;
    BloomTextInput64    text_input;
    BloomIme64          ime;
    BloomWindow64       window;
    BloomMatter64       matter;
    BloomArtStyle64     style;
    BloomPrefabDesc64   prefab;
    BloomPrefabInst64   inst;
    BloomCsgOp64        csg_op;
    BloomBvhNode64      bvh_node;
    BloomJointDq64      joint_dq;
    BloomSdfBrick64     sdf_brick;
    BloomUiNode64       ui_node;
    BloomGlyph64        glyph;
    BloomLatexBox64     latex_box;
    BloomIntent64       intent;
    BloomRaycastHit64   ray_hit;
    BloomImageHeader64  img_hdr;
} BloomCell64;
BLOOM_STATIC_ASSERT64(BloomCell64);

/* Named Standard-Layout Map of the 16,384-Byte Unified Memory Engine Arena */
typedef struct BLOOM_ALIGN64 {
    BloomFrameHeader64   frame_hdr;                             /* Slot 0        */
    BloomCamera64        camera;                                /* Slot 1        */
    BloomRegBank64       reg_bank;                              /* Slot 2        */
    BloomDomState64      dom;                                   /* Slot 3        */
    BloomFlexContainer64 flex;                                  /* Slot 4        */
    BloomTextInput64     text_input;                            /* Slot 5        */
    BloomIme64           ime;                                   /* Slot 6        */
    BloomWindow64        window;                                /* Slot 7        */
    BloomMatter64        matters[BLOOM_MAX_MATTERS];            /* Slots 8..15   */
    BloomArtStyle64      styles[BLOOM_MAX_STYLES];              /* Slots 16..23  */
    BloomPrefabDesc64    prefabs[BLOOM_MAX_PREFABS];            /* Slots 24..39  */
    BloomPrefabInst64    instances[BLOOM_MAX_INSTANCES];        /* Slots 40..71  */
    BloomCsgOp64         csg_ops[BLOOM_MAX_CSG_OPS];            /* Slots 72..103 */
    BloomBvhNode64       bvh_nodes[BLOOM_MAX_BVH_NODES];        /* Slots 104..135*/
    BloomJointDq64       joints[BLOOM_MAX_JOINTS];              /* Slots 136..143*/
    BloomSdfBrick64      bricks[BLOOM_MAX_BRICKS];              /* Slots 144..151*/
    BloomUiNode64        ui_nodes[BLOOM_MAX_UI_NODES];          /* Slots 152..167*/
    BloomGlyph64         glyphs[BLOOM_MAX_GLYPHS];              /* Slots 168..183*/
    BloomLatexBox64      latex_boxes[BLOOM_MAX_LATEX_BOXES];    /* Slots 184..199*/
    uint64_t             flow_states[BLOOM_MAX_FLOW_STATES][8]; /* Slots 200..231*/
    uint64_t             flow_charts[BLOOM_MAX_FLOW_CHARTS][8]; /* Slots 232..247*/
    BloomRaycastHit64    ray_hit;                               /* Slot 248      */
    BloomImageHeader64   img_hdr;                               /* Slot 249      */
    BloomCell64          reserved_tail[6];                      /* Slots 250..255*/
} BloomEngineArenaMap;

/* Complete 16,384-Byte (256 x 64B) Unified Memory Engine Arena */
typedef union BLOOM_ALIGN64 {
    BloomCell64         cells[BLOOM_ARENA_CELLS];
    BloomEngineArenaMap map;
} BloomEngineArena;

BLOOM_STATIC_ASSERT(sizeof(BloomEngineArena) == BLOOM_ARENA_BYTES,
                    "BloomEngineArena must be exactly 16,384 bytes (256 x 64B)");
#if defined(__cplusplus)
BLOOM_STATIC_ASSERT(alignof(BloomEngineArena) == 64U,
                    "BloomEngineArena must be 64-byte aligned");
#else
BLOOM_STATIC_ASSERT(_Alignof(BloomEngineArena) == 64U,
                    "BloomEngineArena must be 64-byte aligned");
#endif
BLOOM_STATIC_ASSERT(offsetof(BloomEngineArena, map) == 0U,
                    "BloomEngineArena.map must start at byte 0");
BLOOM_STATIC_ASSERT(offsetof(BloomEngineArenaMap, ui_nodes) == (size_t)BLOOM_SLOT_UI_BASE * 64ULL,
                    "Arena map.ui_nodes must align to Slot 152");
BLOOM_STATIC_ASSERT(offsetof(BloomEngineArenaMap, img_hdr) == (size_t)BLOOM_SLOT_IMAGE_HDR * 64ULL,
                    "Arena map.img_hdr must align to Slot 249");

/* Portable 32-Bit Pair Wave64 Bitmask Helpers */
static inline bool bloom_wave64_test_u32x2(uint32_t lo32, uint32_t hi32, uint32_t bit_idx) {
    uint32_t word = (bit_idx < 32U) ? lo32 : hi32;
    uint32_t shift = bit_idx & 31U;
    return ((word >> shift) & 1U) != 0U;
}

static inline void bloom_wave64_set_u32x2(uint32_t *lo32, uint32_t *hi32, uint32_t bit_idx) {
    uint32_t shift = bit_idx & 31U;
    if (bit_idx < 32U) {
        *lo32 |= (1U << shift);
    } else if (bit_idx < 64U) {
        *hi32 |= (1U << shift);
    }
}

/* Deterministic 64-Bit FNV-1a Integrity MAC over First 56 Bytes of any 64B Block */
static inline uint64_t bloom_hash56_fnv1a(const void *block64, uint64_t domain_seed) {
    const uint8_t *p = (const uint8_t *)block64;
    uint64_t h = UINT64_C(0xCBF29CE484222325) ^ domain_seed;
    for (size_t i = 0U; i < 56U; ++i) {
        h ^= (uint64_t)p[i];
        h *= UINT64_C(0x100000001B3);
    }
    return h;
}

/* OOB-Immune Masked Arena Accessor (Z / 256Z) */
static inline BloomCell64 *bloom_arena_slot(BloomEngineArena *arena, uint32_t idx) {
    return &arena->cells[idx & BLOOM_ARENA_CAP_MASK];
}

static inline const BloomCell64 *bloom_arena_slot_ro(const BloomEngineArena *arena, uint32_t idx) {
    return &arena->cells[idx & BLOOM_ARENA_CAP_MASK];
}

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_ABI_H */
