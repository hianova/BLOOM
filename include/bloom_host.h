#ifndef BLOOM_HOST_H
#define BLOOM_HOST_H

#include "bloom_abi.h"

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * BLOOM v9.0.0 — Single-Track Cross-Platform SDL3 GPU & Audio Host ABI
 * ============================================================================
 * Pure C17 cross-platform GPU compute & native window host built atop SDL3 GPU:
 *   - Consumes the single-source shaders/bloom_core.wgsl compiled by W3C Naga
 *     into build/bloom_core.metal (MSL 2.4 on Apple Metal) and
 *     build/bloom_core.spv (SPIR-V on Vulkan / Mesa Lavapipe).
 *   - Uploads the 16,384-byte (256 x 64B) BloomEngineArena + 32,768-byte
 *     (512 x 64B) BloomMeshArena and dispatches 4 compute passes (RC C2/C1/C0
 *     + Primary Field/Metric/Cine Frame) into an RGBA8 storage texture.
 *   - Exports standalone WebGPU .html interactive bundles, CMX 3600 .edl reels,
 *     Kitty Graphics Protocol terminal frames, and 48kHz stereo WAV audio.
 * ============================================================================
 */

typedef struct BloomGpuCtx BloomGpuCtx;

BloomGpuCtx *bloom_gpu_init(BloomEngineArena *arena, const char *shader_path);
bool bloom_gpu_upload_mesh(BloomGpuCtx *ctx, const BloomMeshArena *mesh);

bool bloom_gpu_is_hardware_gpu(const BloomGpuCtx *ctx);
bool bloom_gpu_is_unified_memory(const BloomGpuCtx *ctx);
const char *bloom_gpu_device_name(const BloomGpuCtx *ctx);
const char *bloom_gpu_driver_name(const BloomGpuCtx *ctx);
uint32_t bloom_gpu_dispatch_count(const BloomGpuCtx *ctx);
uint32_t bloom_gpu_idle_skip_count(const BloomGpuCtx *ctx);
uint32_t bloom_gpu_rc_cache_hits(const BloomGpuCtx *ctx);

/* Warp-scale FLOW "FIR1" parallel witness hunter & sweep engine */
typedef enum {
    BLOOM_FIR_HUNT_EQ = 0U,
    BLOOM_FIR_HUNT_NEQ = 1U,
    BLOOM_FIR_HUNT_SWEEP = 2U
} BloomFir1HuntMode;

typedef struct {
    uint32_t num_trials;
    uint32_t target_mode;
    uint32_t sweep_mode; /* 0 = sequential, 1 = deterministic fmix32 PRNG, 2 = exhaustive bijective (gtid ^ seed) */
    uint32_t vary_input_mask;
    uint32_t seed_lo;
    uint32_t seed_hi;
    uint32_t target_lo;
    uint32_t target_hi;
    uint64_t base_inputs[32];
} BloomFir1HuntConfig;

typedef struct {
    uint32_t trials_evaluated;
    uint32_t hit_count;
    uint32_t first_hit_trial;
    uint32_t num_inputs;
    uint32_t num_outputs;
    bool     found;
    bool     cpu_verified;
    uint64_t out0_xor_checksum;
    uint64_t elapsed_us;
    uint64_t witness_inputs[32];
    uint64_t witness_outputs[32];
} BloomFir1HuntResult;

bool bloom_gpu_hunt_fir1(BloomGpuCtx *ctx,
                         const uint8_t *fir_bytes,
                         size_t fir_len,
                         const BloomFir1HuntConfig *cfg,
                         BloomFir1HuntResult *out);

bool bloom_gpu_hunt_fir1_bitslice(BloomGpuCtx *ctx,
                                  const uint8_t *fir_bytes,
                                  size_t fir_len,
                                  const BloomFir1HuntConfig *cfg,
                                  BloomFir1HuntResult *out);

bool bloom_fir1_compile_wgsl(const uint8_t *fir_bytes,
                             size_t fir_len,
                             const char *wgsl_out_path,
                             uint32_t *out_gate_count,
                             uint32_t *out_in_bits,
                             uint32_t *out_out0_bits);

bool bloom_gpu_hunt_fir1_aot(BloomGpuCtx *ctx,
                             const uint8_t *fir_bytes,
                             size_t fir_len,
                             const char *aot_shader_path,
                             const BloomFir1HuntConfig *cfg,
                             BloomFir1HuntResult *out);

/* GPU Parallel SEARU Multi-Universe Solver (3-SAT & Graph Coloring) */
typedef struct {
    uint32_t num_universes;   /* 1..8192 (default 4096) */
    uint32_t iters_per_round; /* 1..4096 (default 256) */
    uint32_t max_rounds;      /* 1..32   (default 4) */
    uint64_t seed;
} BloomSearuGpuConfig;

typedef struct {
    uint64_t best_canvas[4];
    uint64_t best_energy;
    uint32_t winning_universe;
    uint32_t rounds_run;
    uint32_t total_universes_eval;
    uint64_t total_steps_eval;
    uint64_t elapsed_us;
    bool     solved;
    bool     cpu_verified;
} BloomSearuGpuResult;

bool bloom_gpu_searu_solve_3sat(BloomGpuCtx *ctx,
                                const uint32_t *clauses,
                                uint32_t num_clauses,
                                uint32_t num_vars,
                                const uint64_t init_canvas[4],
                                const uint64_t kan_pin[4],
                                const BloomSearuGpuConfig *cfg,
                                BloomSearuGpuResult *out);

bool bloom_gpu_searu_solve_graph_color(BloomGpuCtx *ctx,
                                       const uint16_t *edges,
                                       uint32_t num_edges,
                                       uint32_t num_nodes,
                                       uint32_t num_colors,
                                       const uint64_t init_canvas[4],
                                       uint64_t kan_node_pin,
                                       const BloomSearuGpuConfig *cfg,
                                       BloomSearuGpuResult *out);

/* GPU Warp-Scale RTL Stuck-at-0/1 Fault Simulator & Equivalence Miter */
typedef struct {
    uint32_t total_wires;
    uint32_t total_gates;
    uint32_t total_faults;     /* 2 * (num_wires - 2): SA0 and SA1 per signal wire */
    uint32_t detected_faults;
    uint32_t undetected_faults;
    uint32_t trials_per_fault;
    uint64_t total_fault_evals;
    uint64_t elapsed_us;
    uint32_t sample_fault_wire;
    uint32_t sample_stuck_val;
    uint32_t sample_atpg_vec;
    bool     cpu_verified;
} BloomFir1FaultSimResult;

bool bloom_gpu_faultsim_fir1(BloomGpuCtx *ctx,
                             const uint8_t *fir_bytes,
                             size_t fir_len,
                             uint32_t num_trials,
                             uint64_t seed,
                             BloomFir1FaultSimResult *out);

typedef struct {
    uint32_t trials_evaluated;
    uint32_t mismatch_count;
    uint32_t first_mismatch_trial;
    uint32_t miter_gates;
    uint32_t total_in_bits;
    uint64_t elapsed_us;
    uint64_t cex_inputs[32];
    uint64_t cex_out_a[32];
    uint64_t cex_out_b[32];
    bool     equivalent;
    bool     exhaustive;
    bool     cpu_verified;
} BloomFir1EquivResult;

bool bloom_gpu_equiv_fir1(BloomGpuCtx *ctx,
                          const uint8_t *fir_a,
                          size_t len_a,
                          const uint8_t *fir_b,
                          size_t len_b,
                          uint32_t num_trials, /* 0 = auto exhaustive up to 2^24 */
                          uint64_t seed,
                          BloomFir1EquivResult *out);

/* GPU All-Pairs 4-Bit x 64-Perm b-bit MinHash / Structural Similarity Matrix */
typedef struct {
    uint32_t best_match_idx;
    uint32_t best_matches;   /* 0..64 matching 4-bit MinHash buckets */
    uint32_t near_dup_count;
    uint32_t exact_dup_count;
} BloomInfoSimRow;

typedef struct {
    uint32_t num_files;
    uint64_t total_pairs_eval;
    uint32_t files_with_near_dup;
    uint32_t exact_dup_pairs;
    uint64_t elapsed_us;
    bool     cpu_verified;
} BloomInfoSimResult;

void bloom_info_sketch_bytes(const uint8_t *data, size_t len, uint32_t out_sig8[8]);

bool bloom_gpu_info_sim_matrix(BloomGpuCtx *ctx,
                               const uint32_t *sigs_8xu32,
                               uint32_t num_files,    /* 2..8192 */
                               uint32_t thr_matches,  /* e.g. 36 (~53% Jaccard) */
                               BloomInfoSimRow *out_rows,
                               BloomInfoSimResult *out_summary);

/* Dispatches GPU compute if !BLOOM_FLAG_IDLE_SLEEP or force_render == true */
bool bloom_gpu_render(BloomGpuCtx *ctx, bool force_render);

/* Downloads the latest RGBA8 GPU texture, computes FNV-1a hash + non-black pixel count,
 * and optionally writes binary P6 PPM when ppm_path is non-NULL and non-empty */
bool bloom_gpu_write_ppm(const BloomGpuCtx *ctx,
                         const char *ppm_path,
                         uint64_t *out_fnv_hash,
                         uint32_t *out_nonblack_px);

/* Opens a live interactive HiDPI SDL3 GPU window driving step_cb(user_ctx) */
int bloom_gpu_run_window(BloomGpuCtx *ctx,
                         BloomEngineArena *arena,
                         void (*step_cb)(void *),
                         void *user_ctx);

bool bloom_gpu_push_audio_i16(BloomGpuCtx *ctx,
                              const int16_t *stereo_pcm,
                              uint32_t sample_frames);

bool bloom_write_wav_i16(const char *wav_path,
                         const int16_t *stereo_pcm,
                         uint32_t sample_frames,
                         uint32_t sample_rate_hz);

bool bloom_export_webgpu_html(const char *html_path,
                              const BloomEngineArena *arena,
                              const char *wgsl_path);

bool bloom_write_cmx3600_edl(const char *edl_path,
                             const char *reel_title,
                             uint32_t total_frames,
                             uint32_t fps);

bool bloom_gpu_write_kitty_tty(const BloomGpuCtx *ctx,
                               FILE *out_stream,
                               uint32_t max_cols_px);

void bloom_gpu_destroy(BloomGpuCtx *ctx);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_HOST_H */
