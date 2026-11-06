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

#endif /* FLOW_ASSETS_H */
