#ifndef FLOW_GPU_ABI_H
#define FLOW_GPU_ABI_H

#include "flow_shader_portability.h"

/*
 * ============================================================================
 * BLOOM — Single Source of Truth (SSOT) CPU <-> Metal GPU 64B ABI Header
 *
 * Included directly by BOTH:
 *   - Host C17 / Objective-C (src/metal_host.m, tests directory)
 *   - Apple Metal Shading Language (src/shaders.metal via __METAL_VERSION__)
 *
 * Every Uniform Slice and Glyph Descriptor is strictly 64-Byte Cache-Line
 * Univalent (sizeof == 64, align == 64, 0 padding holes), and the combined
 * frame uniform packet FlowGpuFrameUniforms256 is an exact 4 x 64B = 256B
 * cache-line multiple.
 * ============================================================================
 */

#ifdef __METAL_VERSION__
#if !defined(FLOWY_MSL_SHIM)
#include <metal_stdlib>
using namespace metal;
#endif
#else
#include <stdint.h>
#include "ricardian.h"
#endif

/*
 * Slice 0 (Bytes 0..63): Core Screen, Worldline Trajectory & Planet State (64B)
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t screen_w;        /*  4B: Framebuffer width in pixels (3840) */
    uint32_t screen_h;        /*  4B: Framebuffer height in pixels (2160) */
    uint32_t frame_index;     /*  4B: Monotonic frame tick counter */
    uint32_t stage_layer;     /*  4B: Forced-perspective layer (0=Near, 1=Far) */
    float    craft_pos[3];    /* 12B: Craft worldline position (x, y, z) */
    float    craft_rot[3];    /* 12B: Craft Euler attitude (pitch, yaw, roll) */
    float    craft_speed;     /*  4B: Craft scalar speed (m/s) */
    float    planet_pos[3];   /* 12B: Layer-1 planet center (x, y, z) */
    float    planet_radius;   /*  4B: Effective planet radius (m) */
    uint32_t thermal_kelvin;  /*  4B: Ricardian hull temperature (700K..1800K) */
} FlowGpuSlice0Core64;

/*
 * Slice 1 (Bytes 64..127): Physical CineCamera Rig & Tile/Contact Telemetry (64B)
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t thermal_shutdown;   /*  4B: 1 if 800K thermal trip active */
    uint32_t assembly_depth;     /*  4B: Ricardian Assembly Index complexity */
    uint32_t active_tiles;       /*  4B: Active Wave64 non-empty screen tiles */
    uint32_t collision_hit;      /*  4B: 1 if worldline root contact occurred */
    uint32_t vcam_id;            /*  4B: Active virtual camera slot (0..3) */
    uint32_t cine_flags;         /*  4B: Letterbox, DoF, Anamorphic, HUD flags */
    float    focal_length_mm;    /*  4B: Physical lens focal length (18..135mm) */
    float    aperture_fstop;     /*  4B: Physical aperture f-number (f/1.4..f/16) */
    float    focus_dist;         /*  4B: Focus plane distance (m) */
    float    exposure_gain;      /*  4B: Auto-exposure linear gain */
    float    cam_look_offset[2]; /*  8B: Rule-of-thirds & G-force composer offset */
    float    cam_offset[3];      /* 12B: 2-Jet damped camera world offset + 6D shake */
    float    shutter_angle_norm; /*  4B: Normalized shutter angle (0.5 = 180 deg) */
} FlowGpuSlice1Camera64;

/*
 * Slice 2 (Bytes 128..191): Unified Relative Scale Tensor & Boundary Flux (64B)
 */
typedef struct __attribute__((aligned(64))) {
    float shutter_vel[3];      /* 12B: 180-deg shutter velocity vector (vx, vy, vz) */
    float lorentz_beta;        /*  4B: Relativistic beta = |v| / c_eff in [0, 1) */
    float lorentz_gamma;       /*  4B: Lorentz dilation factor gamma >= 1.0 */
    float doppler_factor;      /*  4B: Relativistic longitudinal Doppler factor */
    float metric_potential;    /*  4B: Gravitational potential rs / r */
    float grav_redshift;       /*  4B: Gravitational redshift sqrt(1 - rs/r) */
    float flux_pressure_ratio; /*  4B: Signed boundary flux pressure ratio */
    float wave_phase_freq;     /*  4B: Boundary-layer ripple frequency */
    float contact_pos[3];      /* 12B: Worldline contact point (x, y, z) */
    float rel_vel_dir[3];      /* 12B: Unit relative velocity direction (nx, ny, nz) */
} FlowGpuSlice2Relativity64;

/*
 * Slice 3 (Bytes 192..255): 120FPS Governor, Tri-Modal Optics & Dopamine Economy (64B)
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t raymarch_max_steps;  /*  4B: Governor raymarch step cap (32 or 64) */
    float    layer0_radius;       /*  4B: Governor Layer-0 handoff radius (m) */
    uint32_t active_finish_class; /*  4B: Luxury finish (0=Sapphire,1=BlueGem,2=Kintsugi,3=Jade) */
    float    squash_parallel;     /*  4B: Area-preserving longitudinal stretch */
    float    squash_perp;         /*  4B: Area-preserving transverse squash */
    float    shockwave_ripple;    /*  4B: Impact shockwave ring amplitude */
    float    cavity_resonance;    /*  4B: SDF cavity acoustic/optical resonance */
    float    trc_strobe_hz;       /*  4B: TRC-clamped safe strobe frequency (<= 3.0Hz) */
    uint32_t dopamine_phase;      /*  4B: 0=Idle, 1=Suspense, 2=Hitstop, 3=Boom, 4=Flex */
    float    catharsis_bloom;     /*  4B: Dopamine jackpot execution bloom gain */
    float    suspense_vignette;   /*  4B: Slot-machine build-up edge vignette */
    float    flex_spotlight;      /*  4B: 135mm cosmetic flex C-position spotlight */
    uint32_t ticket_vis_mode;     /*  4B: Composable ticket visibility mode (0..3) */
    uint32_t xray_mask_mode;      /*  4B: 0=Off, 1=Cursor X-Ray Sphere, 2=Section Plane */
    float    xray_radius;         /*  4B: SDF X-Ray cutaway aperture radius (m) */
    float    xray_cursor_uv_x;    /*  4B: Normalized cursor X [-0.5, +0.5] for X-Ray lens */
} FlowGpuSlice3DopamineAv64;

/*
 * Shared 64-Byte SDD SVG HUD Vector Glyph Descriptor (CPU Compiler <-> Metal Shader)
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t glyph_id;          /*  4B: SDD SVG symbol ID (101..104) */
    uint32_t stroke_count;      /*  4B: Active line segments (1..5) */
    int32_t  stroke_radius_q16; /*  4B: SDF stroke half-width in Q16 */
    uint32_t style_id;          /*  4B: Palette / emissive style index */
    int16_t  seg_coords_q8[20]; /* 40B: 5 segments x (x0, y0, x1, y1) in Q8.8 */
    uint64_t ternary_pos_mask;  /*  8B: 64-bit spatial quadrant bitmask */
} FlowGpuGlyphDesc64;

/*
 * Shared 64-Byte Data-Driven GPU CSG Instruction Descriptor (CPU Prefab/GLB <-> Metal Raymarcher)
 */
#define FLOW_CSG_PRIM_BOX         0U
#define FLOW_CSG_PRIM_SPHERE      1U
#define FLOW_CSG_PRIM_CAPSULE     2U
#define FLOW_CSG_PRIM_CYLINDER    3U
#define FLOW_CSG_PRIM_TORUS       4U
#define FLOW_CSG_PRIM_GLB_AABB    5U

#define FLOW_CSG_BOOL_UNION        0U
#define FLOW_CSG_BOOL_SUBTRACT     1U
#define FLOW_CSG_BOOL_INTERSECT    2U
#define FLOW_CSG_BOOL_SMOOTH_UNION 3U

typedef struct __attribute__((aligned(64))) {
    uint32_t op_id;             /*  4B: Unique CSG instruction ID */
    uint32_t prim_type;         /*  4B: FLOW_CSG_PRIM_* */
    uint32_t bool_op;           /*  4B: FLOW_CSG_BOOL_* */
    uint32_t material_id;       /*  4B: Luxury finish / material index */
    int32_t  pos_q16[3];        /* 12B: Local center offset (x, y, z) in Q16 */
    int32_t  extents_q16[3];    /* 12B: Half-extents or (radius, half_h, minor_r) in Q16 */
    int32_t  smooth_k_q16;      /*  4B: Smooth boolean blend radius k in Q16 */
    int32_t  bevel_r_q16;       /*  4B: Edge rounding bevel radius in Q16 */
    uint64_t active_stage_mask; /*  8B: Bitmask of stage layers where op is active */
    uint64_t csg_op_mac;        /*  8B: 64-bit integrity MAC */
} FlowGpuCsgOp64;

/*
 * Shared 64-Byte Declarative SVG-UI Frontend Node Descriptor (CPU Compiler <-> Metal Shader)
 */
#define FLOW_SVG_PRIM_GLYPH_LINES  0U /* Vector line-segment icon (<g><line>) */
#define FLOW_SVG_PRIM_RECT_GLASS   1U /* Rounded liquid-glass card / button / bar (<rect rx>) */
#define FLOW_SVG_PRIM_CIRCLE_RING  2U /* Radial gauge / progress ring / rec indicator (<circle>) */
#define FLOW_SVG_PRIM_NUM_READOUT  3U /* Zero-Font-Atlas 7-segment SDF digital readout (<text>) */
#define FLOW_SVG_PRIM_LATEX_BOX    4U /* Zero-Font-Atlas Analytic SDF LaTeX Formula (\frac, \sqrt, \int, \sum) */

#define FLOW_SVG_ANCHOR_CENTER        0U
#define FLOW_SVG_ANCHOR_TOP_LEFT      1U
#define FLOW_SVG_ANCHOR_TOP_RIGHT     2U
#define FLOW_SVG_ANCHOR_BOTTOM_LEFT   3U
#define FLOW_SVG_ANCHOR_BOTTOM_RIGHT  4U
#define FLOW_SVG_ANCHOR_BOTTOM_CENTER 5U

#define FLOW_SVG_BIND_NONE            0U
#define FLOW_SVG_BIND_THERMAL_KELVIN  1U /* Maps [700K..850K] -> [0..65536] */
#define FLOW_SVG_BIND_CRAFT_SPEED     2U /* Maps [0..1200 m/s] -> [0..65536] */
#define FLOW_SVG_BIND_VCAM_ID         3U /* Active VCam highlight (0..3) */
#define FLOW_SVG_BIND_DOPAMINE_PHASE  4U /* Dopamine jackpot phase (0..4) */
#define FLOW_SVG_BIND_BROADCAST_REC   5U /* Live Recording / OBS Bridge active */
#define FLOW_SVG_BIND_XRAY_RADIUS     6U /* Prefab SDF X-Ray cutaway aperture radius [0..65536] */
#define FLOW_SVG_BIND_PIPELINE_SLA    7U /* Live 120Hz pipeline headroom SLA [0..65536] */
#define FLOW_SVG_BIND_FPS_GOVERNOR    8U /* Maps [30 / 60 / 120 / Uncapped FPS & VSync] -> [0..65536] */

#define FLOW_SVG_GLASS_CRYSTAL        0U /* Pure refractive crystal lens + caustics */
#define FLOW_SVG_GLASS_BEER_LAMBERT   1U /* TrueColor SVG fill Beer-Lambert volume transmittance */
#define FLOW_SVG_GLASS_IRIDESCENT     2U /* Thin-film phase-shifted pearl glass + sheen */
#define FLOW_SVG_GLASS_MERCURY        3U /* High-reflectance liquid mercury metallic sheen */

#define FLOW_SVG_STATE_HOVER_BIT      (1U << 4)
#define FLOW_SVG_STATE_ACTIVE_BIT     (1U << 5)
#define FLOW_SVG_STATE_GLASS_BIT      (1U << 6)
#define FLOW_SVG_STATE_CAPTURED_BIT   (1U << 7) /* Pointer Capture drag lock active */

#define FLOW_SVG_ACTION_NONE          0U
#define FLOW_SVG_ACTION_VCAM_0        1U
#define FLOW_SVG_ACTION_VCAM_1        2U
#define FLOW_SVG_ACTION_VCAM_2        3U
#define FLOW_SVG_ACTION_VCAM_3        4U
#define FLOW_SVG_ACTION_THERMAL_PURGE 5U
#define FLOW_SVG_ACTION_DOPAMINE_BOOM 6U
#define FLOW_SVG_ACTION_BROADCAST_REC 7U
#define FLOW_SVG_ACTION_XRAY_SCRUB    8U  /* Toggle / scrub Prefab SDF X-Ray cutaway mask */
#define FLOW_SVG_ACTION_PREFAB_REVERT 9U  /* Non-destructive revert Prefab instance to SSOT */
#define FLOW_SVG_ACTION_TOGGLE_VSYNC  10U /* Toggle CAMetalLayer displaySyncEnabled (VSync ON/OFF) */
#define FLOW_SVG_ACTION_CYCLE_FPS_GOV 11U /* Cycle 60 -> 120 -> Uncapped -> 30 FPS Governor budget */
#define FLOW_SVG_ACTION_STORY_DIRECTOR 12U /* Trigger Scripted Story Finisher ("劇情殺") + Scene Transition */
#define FLOW_SVG_ACTION_CYCLE_MOOD    13U /* Cycle Moodboard-to-Prefab Stage (Space / Window / Particles / Diorama) */

typedef struct __attribute__((aligned(64))) {
    uint32_t node_id;           /*  4B: Component ID from SVG id / data-node-id */
    uint32_t prim_type;         /*  4B: FLOW_SVG_PRIM_* (Lines / GlassRect / Ring / Readout) */
    uint32_t anchor_mode;       /*  4B: FLOW_SVG_ANCHOR_* responsive screen anchor */
    uint32_t bind_source;       /*  4B: FLOW_SVG_BIND_* reactive O(1) telemetry source */
    int32_t  offset_x_q16;      /*  4B: Anchor-relative X offset in normalized Q16 */
    int32_t  offset_y_q16;      /*  4B: Anchor-relative Y offset in normalized Q16 */
    int32_t  half_w_q16;        /*  4B: Component half-width / outer radius in Q16 */
    int32_t  half_h_q16;        /*  4B: Component half-height / inner radius in Q16 */
    int32_t  corner_radius_q16; /*  4B: Rounded rect radius rx or stroke width in Q16 */
    int32_t  scale_jet_v_q16;   /*  4B: 2-Jet interactive spring scale value (65536 = 1.0x) */
    int32_t  scale_jet_d1_q16;  /*  4B: 2-Jet interactive spring scale velocity & ripple energy */
    int32_t  bound_value_q16;   /*  4B: Resolved reactive telemetry / scrubbed value in Q16 */
    uint32_t style_and_state;   /*  4B: Bits[0..3]=GlassMode, Bits[4..7]=State, Bits[8..31]=FillRGB24 */
    uint32_t action_opcode;     /*  4B: FLOW_SVG_ACTION_* triggered on click / pointer scrub */
    uint64_t tile_mask_wave64;  /*  8B: Precomputed 8x8 screen-space Wave64 hit-test bitmask */
} FlowSvgUiNode64;

/*
 * Combined 256-Byte (4 x 64B) Frame Uniform Packet
 */
typedef struct __attribute__((aligned(64))) {
    FlowGpuSlice0Core64       s0; /* Bytes   0..63  (Cache Line 0) */
    FlowGpuSlice1Camera64     s1; /* Bytes  64..127 (Cache Line 1) */
    FlowGpuSlice2Relativity64 s2; /* Bytes 128..191 (Cache Line 2) */
    FlowGpuSlice3DopamineAv64 s3; /* Bytes 192..255 (Cache Line 3) */
} FlowGpuFrameUniforms256;

/*
 * Unified 64-Byte Engine Plugin / Module Bus Slot Registry (16 x 64B = 1024B)
 * Direct 1:1 Mapping to GPU buffer(3)
 */
#define FLOW_ENGINE_BUS_SLOTS         16U

/* Dedicated Bus Slot Indices */
#define FLOW_BUS_SLOT_SVG_UI_0        0U   /* Reactive SVG-UI Node 0 */
#define FLOW_BUS_SLOT_SVG_UI_1        1U   /* Reactive SVG-UI Node 1 */
#define FLOW_BUS_SLOT_SVG_UI_2        2U   /* Reactive SVG-UI Node 2 */
#define FLOW_BUS_SLOT_SVG_UI_3        3U   /* Reactive SVG-UI Node 3 */
#define FLOW_BUS_SLOT_SVG_UI_4        4U   /* Reactive SVG-UI Node 4 */
#define FLOW_BUS_SLOT_SVG_UI_5        5U   /* Reactive SVG-UI Node 5 */
#define FLOW_BUS_SLOT_SVG_UI_6        6U   /* Reactive SVG-UI Node 6 */
#define FLOW_BUS_SLOT_SVG_UI_7        7U   /* Reactive SVG-UI Node 7 */
#define FLOW_BUS_SLOT_WALLPAPER       8U   /* FlowWallpaperState64 */
#define FLOW_BUS_SLOT_DOPAMINE        9U   /* FlowDopamineJackpot64 */
#define FLOW_BUS_SLOT_SOVEREIGN_GATE  10U  /* FlowSovereignGate64 */
#define FLOW_BUS_SLOT_OBS_BRIDGE      11U  /* FlowBroadcastObsBridge64 */
#define FLOW_BUS_SLOT_STORY_DIRECTOR  12U  /* FlowNarrativeDirector64 ("劇情殺" & "過渡動畫") */
#define FLOW_BUS_SLOT_MOODBOARD       13U  /* FlowMoodboardPrefab64 (Moodboard-to-Prefab Stage & Swatches) */
#define FLOW_BUS_SLOT_CUSTOM_14       14U  /* Future Plugin Slot 14 */
#define FLOW_BUS_SLOT_CUSTOM_15       15U  /* Future Plugin Slot 15 */

#ifdef __METAL_VERSION__
typedef union __attribute__((aligned(64))) {
    FlowSvgUiNode64 svg_node;
    uint4           u32_quads[4];
    ulong           u64_octets[8];
    float4          f32_quads[4];
} FlowEngineBus64;
#else
typedef union __attribute__((aligned(64))) {
    uint8_t          raw_bytes[64];
    uint32_t         raw_u32[16];
    uint64_t         raw_u64[8];
    RicardianStateV4 ricardian;
    FlowSvgUiNode64  svg_node;
} FlowEngineBus64;
#endif

typedef struct __attribute__((aligned(64))) {
    FlowEngineBus64 slots[FLOW_ENGINE_BUS_SLOTS];
} FlowEngineBus1K;

#ifndef __METAL_VERSION__
FLOW_STATIC_ASSERT_UNIVALENT(FlowGpuSlice0Core64, RicardianStateV4);
FLOW_STATIC_ASSERT_UNIVALENT(FlowGpuSlice1Camera64, RicardianStateV4);
FLOW_STATIC_ASSERT_UNIVALENT(FlowGpuSlice2Relativity64, RicardianStateV4);
FLOW_STATIC_ASSERT_UNIVALENT(FlowGpuSlice3DopamineAv64, RicardianStateV4);
FLOW_STATIC_ASSERT_UNIVALENT(FlowGpuGlyphDesc64, RicardianStateV4);
FLOW_STATIC_ASSERT_UNIVALENT(FlowGpuCsgOp64, RicardianStateV4);
FLOW_STATIC_ASSERT_UNIVALENT(FlowSvgUiNode64, RicardianStateV4);
FLOW_STATIC_ASSERT_UNIVALENT(FlowEngineBus64, RicardianStateV4);
_Static_assert(sizeof(FlowGpuSlice0Core64) == 64U, "Slice 0 must be 64B");
_Static_assert(sizeof(FlowGpuSlice1Camera64) == 64U, "Slice 1 must be 64B");
_Static_assert(sizeof(FlowGpuSlice2Relativity64) == 64U, "Slice 2 must be 64B");
_Static_assert(sizeof(FlowGpuSlice3DopamineAv64) == 64U, "Slice 3 must be 64B");
_Static_assert(sizeof(FlowGpuGlyphDesc64) == 64U, "FlowGpuGlyphDesc64 must be 64B");
_Static_assert(sizeof(FlowGpuCsgOp64) == 64U, "FlowGpuCsgOp64 must be 64B");
_Static_assert(sizeof(FlowSvgUiNode64) == 64U, "FlowSvgUiNode64 must be 64B");
_Static_assert(sizeof(FlowEngineBus64) == 64U, "FlowEngineBus64 must be 64B");
_Static_assert(_Alignof(FlowEngineBus64) == 64U, "FlowEngineBus64 must be 64B aligned");
_Static_assert(sizeof(FlowEngineBus1K) == 1024U, "FlowEngineBus1K must be 1024B");
_Static_assert(_Alignof(FlowEngineBus1K) == 64U, "FlowEngineBus1K must be 64B aligned");
_Static_assert(sizeof(FlowGpuFrameUniforms256) == 256U, "FrameUniforms256 must be 4 x 64B = 256B");
_Static_assert(_Alignof(FlowGpuFrameUniforms256) == 64U, "FrameUniforms256 must be 64B aligned");
#endif

#endif /* FLOW_GPU_ABI_H */
