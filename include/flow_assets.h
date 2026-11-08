#ifndef FLOW_ASSETS_H
#define FLOW_ASSETS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "flow_gpu_abi.h"
#include "flow_lean_abi.h"
#include "ricardian.h"

/*
 * ============================================================================
 * BLOOM: Native 64-Byte External Asset Pipeline (WAV, GLB 2.0, W3C SVG Icons)
 * ============================================================================
 * Enforces Lean 4 Formal Invariants (research/AssetPipeline.lean):
 *   1. RIFF/WAVE (.wav) PCM Loader & Polyphonic Relativistic Doppler Mixer:
 *      100% pure integer Q16 mixing with [-32768, 32767] saturation clamp.
 *   2. glTF 2.0 Binary (.glb) 3D Mesh Parser & Q16 Bounding/SDF Proxy:
 *      Decodes IEEE-754 binary32 vertices via pure integer bit manipulation
 *      (zero float instructions, zero heap allocations).
 *   3. Standard W3C SVG (<path d="M... L..."/> / <polygon> / <circle>) Compiler:
 *      Compiles standard artist SVG icons directly into 64B FlowGpuGlyphDesc64.
 * ============================================================================
 */

#define FLOW_GLB_MAGIC_HEADER 0x46546C67U /* "glTF" in little-endian */
#define FLOW_GLB_CHUNK_JSON   0x4E4F534AU /* "JSON" in little-endian */
#define FLOW_GLB_CHUNK_BIN    0x004E4942U /* "BIN\0" in little-endian */
#define FLOW_MIXER_MAX_VOICES 16U

/*
 * 1. FlowWavAssetDesc64: 64-Byte Univalent RIFF/WAVE Audio Asset Descriptor
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t asset_id;          /*  4B: Unique audio asset ID */
    uint32_t sample_rate_hz;    /*  4B: Sample rate in Hz (e.g. 44100, 48000) */
    uint32_t num_channels;      /*  4B: 1 = Mono, 2 = Stereo */
    uint32_t bits_per_sample;   /*  4B: 8, 16, 24, or 32 bits */
    uint32_t frame_count;       /*  4B: Decoded PCM sample frames */
    uint32_t data_offset_bytes; /*  4B: Byte offset of `data` chunk in source */
    uint32_t data_size_bytes;   /*  4B: Byte size of `data` chunk */
    int32_t  peak_amp_q16;      /*  4B: Peak normalized amplitude in Q16 [0..65536] */
    int32_t  rms_energy_q16;    /*  4B: Mean absolute / RMS energy in Q16 */
    uint32_t duration_ms;       /*  4B: Total playback duration in milliseconds */
    uint32_t format_tag;        /*  4B: 1 = PCM integer, 3 = IEEE-754 float */
    uint32_t flags;             /*  4B: Asset playback flags */
    uint64_t pcm_hash;          /*  8B: Deterministic 64-bit FNV/SplitMix PCM hash */
    uint64_t asset_mac;         /*  8B: 64-bit integrity MAC */
} FlowWavAssetDesc64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowWavAssetDesc64, RicardianStateV4);
_Static_assert(sizeof(FlowWavAssetDesc64) == 64U, "FlowWavAssetDesc64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowWavAssetDesc64) == 64U, "FlowWavAssetDesc64 must be 64-byte aligned");

/*
 * 2. FlowAudioVoice64: 64-Byte Univalent Polyphonic Mixer Voice State
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t voice_id;       /*  4B: Voice slot index (0..15) */
    uint32_t asset_id;       /*  4B: Bound FlowWavAssetDesc64 ID */
    uint32_t cursor_frame;   /*  4B: Current integer sample frame cursor */
    uint32_t frac_q16;       /*  4B: Sub-sample fractional phase [0..65535] */
    uint32_t total_frames;   /*  4B: Total sample frames in bound buffer */
    int32_t  pitch_step_q16; /*  4B: Relativistic Doppler / pitch ratio (65536 = 1.0x) */
    int32_t  gain_left_q16;  /*  4B: Left ear linear gain in Q16 */
    int32_t  gain_right_q16; /*  4B: Right ear linear gain in Q16 */
    int32_t  pan_q16;        /*  4B: Stereo pan [-65536 (L) .. +65536 (R)] */
    uint32_t active;         /*  4B: 1 = playing, 0 = idle */
    uint32_t loop_enabled;   /*  4B: 1 = loop continuously, 0 = one-shot */
    uint32_t priority;       /*  4B: Voice stealing priority */
    uint64_t trigger_tick;   /*  8B: Engine tick when voice started */
    uint64_t voice_mac;      /*  8B: 64-bit voice state MAC */
} FlowAudioVoice64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowAudioVoice64, RicardianStateV4);
_Static_assert(sizeof(FlowAudioVoice64) == 64U, "FlowAudioVoice64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowAudioVoice64) == 64U, "FlowAudioVoice64 must be 64-byte aligned");

/*
 * 3. FlowGlbMeshDesc64: 64-Byte Univalent glTF 2.0 (.glb) 3D Mesh Descriptor
 */
typedef struct __attribute__((aligned(64))) {
    uint32_t asset_id;            /*  4B: Unique 3D mesh asset ID */
    uint32_t gltf_version;        /*  4B: glTF specification version (2) */
    uint32_t json_chunk_bytes;    /*  4B: Byte length of JSON scene graph chunk */
    uint32_t bin_chunk_bytes;     /*  4B: Byte length of BIN geometry buffer chunk */
    uint32_t vertex_count;        /*  4B: Decoded 3D vertex count */
    uint32_t index_count;         /*  4B: Decoded triangle index count */
    int32_t  aabb_min_q16[3];     /* 12B: Exact mesh AABB minimum (x, y, z) in Q16 */
    int32_t  aabb_max_q16[3];     /* 12B: Exact mesh AABB maximum (x, y, z) in Q16 */
    int32_t  bounding_radius_q16; /*  4B: Enclosing bounding sphere radius in Q16 */
    uint32_t material_style_id;   /*  4B: Bound Art Bible style ID */
    uint64_t mesh_mac;            /*  8B: 64-bit integrity MAC */
} FlowGlbMeshDesc64;

FLOW_STATIC_ASSERT_UNIVALENT(FlowGlbMeshDesc64, RicardianStateV4);
_Static_assert(sizeof(FlowGlbMeshDesc64) == 64U, "FlowGlbMeshDesc64 must be exactly 64 bytes");
_Static_assert(_Alignof(FlowGlbMeshDesc64) == 64U, "FlowGlbMeshDesc64 must be 64-byte aligned");

/* Pure-Integer IEEE-754 binary32 Bit-Pattern to Q16.16 Converter (Zero Float Ops) */
int32_t flow_ieee754_bits_to_q16(uint32_t ieee754_bits);

/* 1. RIFF/WAVE (.wav) PCM Loader & Polyphonic Voice Mixer API */
bool flow_asset_load_wav_bytes(const uint8_t *wav_bytes,
                               size_t wav_len,
                               uint32_t asset_id,
                               int16_t *out_mono_s16,
                               uint32_t max_frames,
                               FlowWavAssetDesc64 *out_desc);

bool flow_asset_load_wav_file(const char *wav_path,
                              uint32_t asset_id,
                              int16_t *out_mono_s16,
                              uint32_t max_frames,
                              FlowWavAssetDesc64 *out_desc);

void flow_audio_voice_trigger64(FlowAudioVoice64 *voice,
                                uint32_t voice_id,
                                const FlowWavAssetDesc64 *wav_desc,
                                int32_t gain_q16,
                                int32_t pan_q16,
                                int32_t pitch_step_q16,
                                bool loop_enabled,
                                uint64_t trigger_tick);

void flow_audio_voice_mix_stereo_s16(FlowAudioVoice64 *voice,
                                     const int16_t *mono_pcm_s16,
                                     int16_t *inout_stereo_s16,
                                     uint32_t out_frames);

/* 2. glTF 2.0 Binary (.glb) 3D Mesh Loader & Q16 SDF Proxy API */
bool flow_asset_load_glb_bytes(const uint8_t *glb_bytes,
                               size_t glb_len,
                               uint32_t asset_id,
                               uint32_t material_style_id,
                               int32_t (*out_vertices_q16)[3],
                               uint32_t max_vertices,
                               FlowGlbMeshDesc64 *out_desc);

bool flow_asset_load_glb_file(const char *glb_path,
                              uint32_t asset_id,
                              uint32_t material_style_id,
                              int32_t (*out_vertices_q16)[3],
                              uint32_t max_vertices,
                              FlowGlbMeshDesc64 *out_desc);

int64_t flow_asset_eval_glb_sdf_q16(const FlowGlbMeshDesc64 *mesh,
                                    int64_t local_x_q16,
                                    int64_t local_y_q16,
                                    int64_t local_z_q16);

/* 3. Standard W3C SVG (<path d="M... L..."/> / <polygon> / <circle>) Icon Compiler */
bool flow_asset_compile_svg_icon64(const char *svg_text,
                                   uint32_t glyph_id,
                                   uint32_t style_id,
                                   FlowGpuGlyphDesc64 *out_glyph);

bool flow_asset_load_svg_icon_file(const char *svg_path,
                                   uint32_t glyph_id,
                                   uint32_t style_id,
                                   FlowGpuGlyphDesc64 *out_glyph);

/* 4. Data-Driven 64-Byte GPU CSG Instruction Compiler & Program Evaluator */
void flow_asset_compile_glb_to_csg_op64(const FlowGlbMeshDesc64 *mesh,
                                        uint32_t op_id,
                                        uint32_t bool_op,
                                        int32_t smooth_k_q16,
                                        FlowGpuCsgOp64 *out_op);

int64_t flow_asset_eval_csg_program_q16(const FlowGpuCsgOp64 *ops,
                                        size_t op_count,
                                        int64_t px_q16,
                                        int64_t py_q16,
                                        int64_t pz_q16);

/* 5. 64-Byte Dual-Quaternion Q16 Skeletal Skinning & Capsule Rig API */
void flow_joint_dq64_make(FlowGpuJointDq64 *out_joint,
                          uint32_t joint_id,
                          uint32_t parent_id,
                          const int32_t q_real_q16[4],
                          const int32_t trans_q16[3],
                          int32_t bone_len_q16,
                          int32_t bone_radius_q16,
                          int32_t smooth_k_q16,
                          uint32_t material_id);

void flow_joint_dq64_blend(const FlowGpuJointDq64 *joint_a,
                           const FlowGpuJointDq64 *joint_b,
                           int32_t weight_a_q16,
                           FlowGpuJointDq64 *out_blended);

void flow_joint_dq64_transform_point_q16(const FlowGpuJointDq64 *joint,
                                         const int32_t in_pt_q16[3],
                                         int32_t out_pt_q16[3]);

int64_t flow_skeleton_eval_sdf_q16(const FlowGpuJointDq64 *joints,
                                   size_t joint_count,
                                   int64_t px_q16,
                                   int64_t py_q16,
                                   int64_t pz_q16);

/* 6. 64-Byte Pointer-Free Linear BVH Spatial Acceleration Tree API */
uint64_t flow_bvh_morton3d_q16(int32_t x_q16, int32_t y_q16, int32_t z_q16);

size_t flow_bvh_build_from_csg64(const FlowGpuCsgOp64 *ops,
                                 size_t op_count,
                                 FlowGpuBvhNode64 *out_nodes,
                                 size_t max_nodes);

int64_t flow_bvh_eval_csg_q16(const FlowGpuBvhNode64 *nodes,
                              size_t node_count,
                              const FlowGpuCsgOp64 *ops,
                              size_t op_count,
                              int64_t px_q16,
                              int64_t py_q16,
                              int64_t pz_q16,
                              uint32_t *out_visited_leaves);

/* 7. 64-Byte Sparse 4x4x4 High-Frequency SDF Voxel Brick API */
void flow_sdf_brick64_pack(FlowGpuSdfBrick64 *out_brick,
                           uint32_t brick_id,
                           uint32_t material_id,
                           const int32_t origin_q16[3],
                           int32_t voxel_step_q16,
                           const int8_t sdf_offsets_i8[64]);

int64_t flow_sdf_brick64_sample_trilinear_q16(const FlowGpuSdfBrick64 *brick,
                                              int64_t px_q16,
                                              int64_t py_q16,
                                              int64_t pz_q16);

/* 8. 64-Byte Color-Gamut & Streaming Egress API (OKLab -> Rec.709/P3/Rec.2020 & YUV420p) */
void flow_stream_egress64_init(FlowStreamEgress64 *out_egress,
                               uint32_t stream_id,
                               uint32_t target_gamut,
                               uint32_t stream_format,
                               uint32_t target_fps);

void flow_stream_egress64_map_oklab_to_rgb_q16(const FlowStreamEgress64 *egress,
                                               int32_t l_q16,
                                               int32_t a_q16,
                                               int32_t b_q16,
                                               int32_t out_rgb_q16[3]);

void flow_stream_egress64_rgb_to_yuv420_bt709(const int32_t rgb_q16[3],
                                              uint8_t *out_y,
                                              uint8_t *out_u,
                                              uint8_t *out_v);

size_t flow_stream_egress64_encode_kitty_rgb24(const uint8_t *rgb24_pixels,
                                               uint32_t width,
                                               uint32_t height,
                                               char *out_buf,
                                               size_t max_buf_len);

/*
 * 9. 64-Byte Constitutive Physical Matter State & Branchless Optical Operator API
 * Derives visual appearance directly from continuum mechanics & electrodynamics
 * without semantic "Material Classes" (Glass / Water / Jade / Metal / Lava).
 */
void flow_matter_state64_init(FlowMatterState64 *out_matter,
                              uint32_t matter_id,
                              int32_t ior_eta_q16,
                              int32_t extinction_kappa_q16,
                              int32_t viscosity_quench_q16,
                              int32_t surface_relief_q16,
                              int32_t micro_roughness_q16,
                              int32_t mie_scatter_q16,
                              const int32_t absorb_rgb_q16[3],
                              int32_t lattice_wire_q16,
                              int32_t thickness_mm_q16,
                              uint32_t thermal_kelvin,
                              uint32_t density_kg_m3);

void flow_matter_state64_format_hex128(const FlowMatterState64 *matter,
                                       char out_hex[129]);

bool flow_matter_state64_parse_hex128(const char *hex128,
                                      FlowMatterState64 *out_matter);

int32_t flow_matter_eval_fresnel_q16(const FlowMatterState64 *matter,
                                     int32_t cos_theta_q16);

int32_t flow_matter_eval_vogel_radius_q16(const FlowMatterState64 *matter);

int32_t flow_matter_eval_caustic_gain_q16(const FlowMatterState64 *matter,
                                          int32_t laplacian_h_q16);

void flow_matter_eval_radiance_rgb_q16(const FlowMatterState64 *matter,
                                       const int32_t grad_h_q16[2],
                                       int32_t laplacian_h_q16,
                                       const int32_t bg_rgb_q16[3],
                                       const int32_t light_delta_q16[2],
                                       int32_t out_rgb_q16[3]);

/*
 * 10. Cross-Platform 64-Byte GPU Bus1K Packer API (Metal / Vulkan / DX12 / WebGPU)
 * Decouples 16 x 64B GPU Bus slot assembly from platform-specific window hosts.
 */
void flow_gpu_bus1k_update_slot64(FlowEngineBus1K *inout_bus,
                                  uint32_t slot_id,
                                  const void *block64);

void flow_gpu_bus1k_sync_svg_nodes(FlowEngineBus1K *inout_bus,
                                   const FlowSvgUiNode64 *svg_nodes,
                                   size_t node_count);

/*
 * 11. 64-Byte Cloud Room Isolate & Multi-View Tile-Atlas Router API
 * Multiplexes up to 16 concurrent player viewports (FlowGpuSlice1Camera64) over
 * a single shared room world manifold (s0_core + FlowEngineBus1K) with < 8.33ms
 * SLA noisy-neighbor LOD circuit-breaker.
 */
void flow_cloud_room64_init(FlowCloudRoomDesc64 *out_room,
                            uint32_t room_id,
                            uint32_t map_block_count,
                            uint32_t active_views,
                            uint32_t tile_width_px,
                            uint32_t tile_height_px,
                            uint32_t sla_budget_us,
                            uint64_t shared_world_hash);

uint32_t flow_cloud_room64_route_pixel_to_seat(const FlowCloudRoomDesc64 *room,
                                               uint32_t atlas_x_px,
                                               uint32_t atlas_y_px,
                                               int32_t *out_local_ndc_x_q16,
                                               int32_t *out_local_ndc_y_q16);

uint32_t flow_cloud_room64_step_sla_governor(FlowCloudRoomDesc64 *inout_room,
                                             uint32_t measured_frame_us);

#endif /* FLOW_ASSETS_H */
