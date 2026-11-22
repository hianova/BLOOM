#define _POSIX_C_SOURCE 200809L

#include "bloom.h"
#include "flow_ir_io.h"
#include "flow_sha256_io.h"
#include "searu.h"

#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/*
 * ============================================================================
 * BLOOM v9.0.0 — Standalone Zero-Lean-Runtime Pure C17 Studio, Game & CLI
 * ============================================================================
 * Consumes sealed 16,384-byte (256 x 64B) .bloom Arena images compiled AOT by
 * Bloom.lean OR ingests binary .stl / .glb CAD triangle meshes directly into
 * BloomMeshArena (32,768B = 512 x 64B), driving:
 *   1. Interactive 60Hz HiDPI SDL3 GPU Native Studio (Radiance Cascades GI,
 *      2-Jet Curvature Field, Relativistic Geodesic Metric, Thin-Lens CineCamera,
 *      Concentric SDF Gameplay Shells, 16KB Spacetime Rewind & Modal DSP),
 *   2. Headless GPU compute, --cine, --metric, --shells, --rewind,
 *      --import-mood, --export-html, --edl-out, --kitty & --audio-out WAV.
 *
 * Zero <lean/lean.h>, zero -lleanshared, zero Lean runtime dependency.
 * ============================================================================
 */

typedef struct {
    const char *bloom_path;
    const char *shader_path;
    const char *watch_svg_path;
    const char *out_ppm_path;
    const char *out_wav_path;
    const char *import_mood_path;
    const char *export_html_path;
    const char *out_edl_path;
    const char *out_fir_path;
    const char *hunt_fir_path;
    const char *aot_shader_path;
    const char *compile_fir_in;
    const char *compile_wgsl_out;
    const char *faultsim_fir_path;
    const char *equiv_fir_a;
    const char *equiv_fir_b;
    const char *gpu_ncd_dirs;
    const char *searu_cnf_path;
    uint32_t    hunt_trials;
    uint32_t    hunt_mode;
    uint32_t    hunt_vary_mask;
    uint64_t    hunt_seed;
    uint64_t    hunt_target;
    uint32_t    ncd_thr;
    uint32_t    ncd_top;
    bool        trials_explicit;
    bool        hunt_prng;
    bool        hunt_bitslice;
    bool        verify_cpu;
    bool        searu_bench_3sat;
    uint32_t    searu_3sat_vars;
    uint32_t    searu_3sat_clauses;
    bool        searu_bench_color;
    uint32_t    searu_color_nodes;
    uint32_t    searu_color_k;
    uint32_t    searu_universes;
    uint32_t    searu_iters;
    uint32_t    searu_rounds;
    uint32_t    override_w;
    uint32_t    override_h;
    uint32_t    steps;
    uint32_t    rewind_steps;
    int32_t     sec_norm_q16[3];
    int32_t     sec_dist_q16;
    int32_t     slab_half_q16;
    int32_t     impact_vx_q16;
    int32_t     metric_phi_q16;
    int32_t     metric_beta_q16;
    bool        set_section;
    bool        set_slab;
    bool        set_corner;
    bool        inspect_cae;
    bool        emit_sha256;
    bool        enable_rc;
    bool        enable_field;
    bool        enable_cine;
    bool        has_metric;
    bool        enable_shells;
    bool        has_impact;
    bool        emit_kitty;
    bool        headless;
    bool        show_help;
    bool        show_version;
} BloomCliArgs;

typedef struct {
    BloomEngineArena    *arena;
    BloomEngineArena    *staging;
    BloomMeshArena      *mesh;
    BloomGpuCtx         *gpu;
    BloomAudioRig        audio;
    BloomGameplayState64 gameplay;
    BloomRewindCtrl64    rewind_ctrl;
    BloomSpacetimeSnap  *rewind_ring;
    const char          *bloom_path;
    const char          *watch_svg_path;
    time_t               last_bloom_mtime;
    time_t               last_svg_mtime;
    uint32_t             frame_tick;
    uint32_t             matter_cursor;
    uint16_t             mode_word;
    bool                 alert_toggled;
    bool                 mesh_mode;
} BloomStudioRuntime;

static int32_t cli_clamp64(int64_t v, int32_t lo, int32_t hi) {
    if (v < (int64_t)lo) return lo;
    if (v > (int64_t)hi) return hi;
    return (int32_t)v;
}

static void cli_stamp_mac(void *block64, uint64_t seed, uint32_t *out_lo, uint32_t *out_hi) {
    uint64_t mac = bloom_hash56_fnv1a(block64, seed);
    *out_lo = (uint32_t)(mac & 0xFFFFFFFFULL);
    *out_hi = (uint32_t)(mac >> 32ULL);
}

static bool cli_parse_u64(const char *s, uint64_t *out_val) {
    if (!s || s[0] == '\0' || !out_val) return false;
    char *endp = NULL;
    unsigned long long v = strtoull(s, &endp, 0);
    if (!endp || *endp != '\0') return false;
    *out_val = (uint64_t)v;
    return true;
}

static bool cli_parse_u32(const char *s, uint32_t *out_val) {
    if (!s || s[0] == '\0' || !out_val) return false;
    uint64_t acc = 0ULL;
    for (size_t i = 0U; s[i] != '\0'; ++i) {
        char c = s[i];
        if (c < '0' || c > '9') return false;
        acc = acc * 10ULL + (uint64_t)(uint8_t)(c - '0');
        if (acc > 0xFFFFFFFFULL) return false;
    }
    *out_val = (uint32_t)acc;
    return true;
}

static bool cli_parse_q16(const char *s, int32_t *out_q16, const char **out_end) {
    if (!s || !out_q16) return false;
    const char *p = s;
    bool neg = false;
    if (*p == '-') {
        neg = true;
        ++p;
    } else if (*p == '+') {
        ++p;
    }
    if (*p < '0' || *p > '9') return false;
    int64_t whole = 0;
    while (*p >= '0' && *p <= '9') {
        whole = whole * 10LL + (int64_t)(*p - '0');
        if (whole > 32767LL) whole = 32767LL;
        ++p;
    }
    int64_t frac = 0;
    int64_t frac_scale = 1;
    if (*p == '.') {
        ++p;
        int digits = 0;
        while (*p >= '0' && *p <= '9') {
            if (digits < 4) {
                frac = frac * 10LL + (int64_t)(*p - '0');
                frac_scale *= 10LL;
                ++digits;
            }
            ++p;
        }
    }
    int64_t val = (whole * (int64_t)BLOOM_Q16_ONE) + ((frac * (int64_t)BLOOM_Q16_ONE) / frac_scale);
    if (neg) val = -val;
    *out_q16 = cli_clamp64(val, -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
    if (out_end) *out_end = p;
    return true;
}

static bool cli_parse_q16_pair(const char *s, int32_t *out_a, int32_t *out_b) {
    const char *p = s;
    if (!cli_parse_q16(p, out_a, &p) || *p != ',') return false;
    ++p;
    if (!cli_parse_q16(p, out_b, &p) || *p != '\0') return false;
    return true;
}

static bool cli_parse_i32_pair(const char *s, int32_t *out_a, int32_t *out_b) {
    if (!s || !out_a || !out_b) return false;
    char *e1 = NULL;
    long v1 = strtol(s, &e1, 10);
    if (!e1 || e1 == s || *e1 != ',') return false;
    char *e2 = NULL;
    long v2 = strtol(e1 + 1, &e2, 10);
    if (!e2 || e2 == (e1 + 1) || *e2 != '\0') return false;
    if (v1 < -2147483647L || v1 > 2147483647L || v2 < -2147483647L || v2 > 2147483647L) {
        return false;
    }
    *out_a = (int32_t)v1;
    *out_b = (int32_t)v2;
    return true;
}

static bool cli_parse_section_vec4(const char *s, int32_t out_n[3], int32_t *out_d) {
    const char *p = s;
    for (int i = 0; i < 3; ++i) {
        if (!cli_parse_q16(p, &out_n[i], &p) || *p != ',') return false;
        ++p;
    }
    if (!cli_parse_q16(p, out_d, &p) || *p != '\0') return false;
    return true;
}

static void cli_print_usage(const char *prog) {
    printf("BLOOM v%s — Pure C17 Radiance Cascades, Diff Field, Relativistic Metric, CineCamera & Modal DSP Engine\n", BLOOM_VERSION_STRING);
    printf("Usage:\n");
    printf("  %s [model.{bloom,stl,glb}] [--watch-svg <spec.svg>] [--shader <core.wgsl>] [--width W --height H]\n", prog);
    printf("  %s --headless [model.{bloom,stl,glb}] [--rc] [--field] [--cine] [--metric phi,beta] [--shells] [--rewind N] [--import-mood ref.ppm] [--export-html app.html] [--edl-out reel.edl] [--kitty] [--impact vx] [--audio-out out.wav] [--out frame.ppm]\n", prog);
    printf("\nOptions:\n");
    printf("  --rc                   Enable 3-pass zero-noise 3D Radiance Cascades GI\n");
    printf("  --field                Enable 2-Jet differential geometry curvature/patina fields\n");
    printf("  --cine                 Enable 2.39:1 thin-lens DoF & anamorphic blue streaks\n");
    printf("  --metric phi,beta      Enable geodesic lensing + Lorentz Doppler scaling\n");
    printf("  --shells               Enable concentric SDF hurt/graze/hit gameplay shells\n");
    printf("  --rewind <N>           Record 16KB spacetime ring and restore N frames back\n");
    printf("  --import-mood <in.ppm> Compile P6 PPM moodboard into 64B Style & Prefab SSOT\n");
    printf("  --export-html <out.html> Bundle 16KB Arena + WGSL into single-file WebGPU HTML\n");
    printf("  --edl-out <out.edl>    Write 24fps CMX 3600 storyboard EDL reel\n");
    printf("  --emit-fir <out.fir>   Certify and export QTT Linear/Unitary FLOW v9.0.0 .fir witness\n");
    printf("  --hunt-fir <in.fir>    Run GPU warp-scale FLOW FIR1 evaluator & witness hunter\n");
    printf("  --trials <N>           Number of GPU FIR1 trials to evaluate (default: 65536)\n");
    printf("  --hunt-seed <u64>      Initial 64-bit sweep/PRNG seed for --hunt-fir\n");
    printf("  --hunt-mode <m>        Witness predicate: eq | neq | sweep (default: sweep)\n");
    printf("  --hunt-target <u64>    64-bit output target for eq / neq hunt modes\n");
    printf("  --hunt-vary <mask32>   Bitmask of varying input slots (default: 0xFFFFFFFF)\n");
    printf("  --hunt-prng            Use fmix32 PRNG input generator instead of linear counter\n");
    printf("  --bitslice             Use 32-way SIMT bit-sliced netlist accelerator (gtid ^ seed)\n");
    printf("  --compile-fir-wgsl <in.fir> <out.wgsl>  Compile FLOW FIR1 circuit to straight-line WGSL\n");
    printf("  --aot-shader <path>    Execute compiled straight-line AOT FIR1 shader (base/.metal/.spv)\n");
    printf("  --faultsim-fir <f.fir> GPU stuck-at-0/1 ATPG fault simulation across all signal wires\n");
    printf("  --equiv-fir <a.fir> <b.fir>  GPU bit-exact formal hardware equivalence miter\n");
    printf("  --gpu-ncd <d1[,d2..]>  GPU all-pairs b-bit MinHash / NCD similarity matrix\n");
    printf("  --ncd-thr <0..64>      Match threshold out of 64 buckets (default: 36)\n");
    printf("  --top <K>              Top clone pairs to print for --gpu-ncd (default: 10)\n");
    printf("  --searu-cnf <in.cnf>   Solve DIMACS 3-SAT file using GPU Parallel SEARU (<=256v, <=8192c)\n");
    printf("  --searu-bench-3sat V,C Planted random 3-SAT GPU vs CPU benchmark\n");
    printf("  --searu-bench-color N,K Planted dense K-coloring GPU vs CPU benchmark\n");
    printf("  --universes <N>        Parallel GPU SEARU universes (1..8192, default 4096)\n");
    printf("  --searu-iters <I>      SEARU iterations per round (1..4096, default 256)\n");
    printf("  --searu-rounds <R>     SEARU replica-exchange rounds (1..32, default 4)\n");
    printf("  --verify-cpu           Cross-check GPU hit/checksum bit-for-bit on CPU\n");
    printf("  --sha256               Print/verify Pure-GPR SHA-256 digest of loaded asset/arena\n");
    printf("  --kitty                Emit Kitty Graphics APC terminal image payload\n");
    printf("  --impact <vx>          Launch deterministic sub-frame Swept-CCD sphere impact\n");
    printf("  --audio-out <out.wav>  Render 48kHz stereo physical-modeling modal DSP WAV\n");
    printf("  --section nx,ny,nz,d   Enable arbitrary 3D cross-section plane n.p = d\n");
    printf("  --slab <half>          Enable dual-plane slab tomography around section plane\n");
    printf("  --corner               Enable surgical quarter-corner cutaway section mode\n");
    printf("  --inspect              Step CAE physics and print telemetry\n");
    printf("  --headless             Run offscreen GPU compute & exit after rendering\n");
    printf("  --out <frame.ppm>      Export rendered RGBA8 framebuffer to binary P6 PPM\n");
    printf("  --steps <N>            2-Jet settlement steps in headless mode (default: 60)\n");
    printf("  --width <W>            Override framebuffer width in pixels\n");
    printf("  --height <H>           Override framebuffer height in pixels\n");
    printf("  --watch-svg <file>     Optional W3C .svg file to hot-reload alongside .bloom\n");
    printf("  --shader <file.wgsl>   WGSL compute shader base path (default: shaders/bloom_core.wgsl)\n");
    printf("  --version, -v          Print BLOOM version\n");
    printf("  --help, -h             Show this help message\n");
}

static bool cli_parse_args(int argc, char **argv, BloomCliArgs *out) {
    memset(out, 0, sizeof(*out));
    out->bloom_path = "build/studio.bloom";
    out->shader_path = "shaders/bloom_core.wgsl";
    out->steps = 60U;
    out->hunt_trials = 65536U;
    out->hunt_mode = BLOOM_FIR_HUNT_SWEEP;
    out->hunt_vary_mask = 0xFFFFFFFFU;
    out->searu_universes = 4096U;
    out->searu_iters = 256U;
    out->searu_rounds = 4U;
    out->ncd_thr = 36U;
    out->ncd_top = 10U;
    out->sec_norm_q16[0] = BLOOM_Q16_ONE;
    out->slab_half_q16 = 16384;

    bool positional_set = false;
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            out->show_help = true;
            return true;
        }
        if (strcmp(arg, "--version") == 0 || strcmp(arg, "-v") == 0) {
            out->show_version = true;
            return true;
        }
        if (strcmp(arg, "--headless") == 0) {
            out->headless = true;
        } else if (strcmp(arg, "--rc") == 0) {
            out->enable_rc = true;
        } else if (strcmp(arg, "--field") == 0) {
            out->enable_field = true;
        } else if (strcmp(arg, "--cine") == 0) {
            out->enable_cine = true;
        } else if (strcmp(arg, "--metric") == 0) {
            if (i + 1 >= argc || !cli_parse_q16_pair(argv[++i], &out->metric_phi_q16, &out->metric_beta_q16)) {
                fprintf(stderr, "[BLOOM CLI] Invalid --metric (expected phi,beta)\n");
                return false;
            }
            out->has_metric = true;
        } else if (strcmp(arg, "--shells") == 0) {
            out->enable_shells = true;
        } else if (strcmp(arg, "--rewind") == 0) {
            if (i + 1 >= argc || !cli_parse_u32(argv[++i], &out->rewind_steps) ||
                out->rewind_steps == 0U || out->rewind_steps >= BLOOM_REWIND_RING_CAP) {
                fprintf(stderr, "[BLOOM CLI] Invalid --rewind (expected 1..63)\n");
                return false;
            }
        } else if (strcmp(arg, "--import-mood") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Missing path after --import-mood\n");
                return false;
            }
            out->import_mood_path = argv[++i];
        } else if (strcmp(arg, "--export-html") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Missing path after --export-html\n");
                return false;
            }
            out->export_html_path = argv[++i];
            out->headless = true;
        } else if (strcmp(arg, "--edl-out") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Missing path after --edl-out\n");
                return false;
            }
            out->out_edl_path = argv[++i];
            out->headless = true;
        } else if (strcmp(arg, "--emit-fir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Missing path after --emit-fir\n");
                return false;
            }
            out->out_fir_path = argv[++i];
            out->headless = true;
        } else if (strcmp(arg, "--hunt-fir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Missing path after --hunt-fir\n");
                return false;
            }
            out->hunt_fir_path = argv[++i];
            out->headless = true;
        } else if (strcmp(arg, "--trials") == 0) {
            if (i + 1 >= argc || !cli_parse_u32(argv[++i], &out->hunt_trials) ||
                out->hunt_trials == 0U || out->hunt_trials > 1073741824U) {
                fprintf(stderr, "[BLOOM CLI] Invalid --trials (expected 1..1073741824)\n");
                return false;
            }
            out->trials_explicit = true;
        } else if (strcmp(arg, "--hunt-seed") == 0) {
            if (i + 1 >= argc || !cli_parse_u64(argv[++i], &out->hunt_seed)) {
                fprintf(stderr, "[BLOOM CLI] Invalid --hunt-seed\n");
                return false;
            }
        } else if (strcmp(arg, "--hunt-target") == 0) {
            if (i + 1 >= argc || !cli_parse_u64(argv[++i], &out->hunt_target)) {
                fprintf(stderr, "[BLOOM CLI] Invalid --hunt-target\n");
                return false;
            }
        } else if (strcmp(arg, "--hunt-vary") == 0) {
            if (i + 1 >= argc || !cli_parse_u32(argv[++i], &out->hunt_vary_mask)) {
                fprintf(stderr, "[BLOOM CLI] Invalid --hunt-vary\n");
                return false;
            }
        } else if (strcmp(arg, "--hunt-mode") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Missing mode after --hunt-mode\n");
                return false;
            }
            const char *hm = argv[++i];
            if (strcmp(hm, "eq") == 0) {
                out->hunt_mode = BLOOM_FIR_HUNT_EQ;
            } else if (strcmp(hm, "neq") == 0) {
                out->hunt_mode = BLOOM_FIR_HUNT_NEQ;
            } else if (strcmp(hm, "sweep") == 0) {
                out->hunt_mode = BLOOM_FIR_HUNT_SWEEP;
            } else {
                fprintf(stderr, "[BLOOM CLI] Unknown --hunt-mode: %s (expected eq|neq|sweep)\n", hm);
                return false;
            }
        } else if (strcmp(arg, "--hunt-prng") == 0) {
            out->hunt_prng = true;
        } else if (strcmp(arg, "--bitslice") == 0) {
            out->hunt_bitslice = true;
        } else if (strcmp(arg, "--aot-shader") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Missing path after --aot-shader\n");
                return false;
            }
            out->aot_shader_path = argv[++i];
        } else if (strcmp(arg, "--compile-fir-wgsl") == 0) {
            if (i + 2 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Expected <in.fir> <out.wgsl> after --compile-fir-wgsl\n");
                return false;
            }
            out->compile_fir_in = argv[++i];
            out->compile_wgsl_out = argv[++i];
            out->headless = true;
        } else if (strcmp(arg, "--faultsim-fir") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Missing path after --faultsim-fir\n");
                return false;
            }
            out->faultsim_fir_path = argv[++i];
            out->headless = true;
        } else if (strcmp(arg, "--equiv-fir") == 0) {
            if (i + 2 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Expected <spec.fir> <impl.fir> after --equiv-fir\n");
                return false;
            }
            out->equiv_fir_a = argv[++i];
            out->equiv_fir_b = argv[++i];
            out->headless = true;
        } else if (strcmp(arg, "--gpu-ncd") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Missing directories after --gpu-ncd\n");
                return false;
            }
            out->gpu_ncd_dirs = argv[++i];
            out->headless = true;
        } else if (strcmp(arg, "--ncd-thr") == 0) {
            if (i + 1 >= argc || !cli_parse_u32(argv[++i], &out->ncd_thr) || out->ncd_thr > 64U) {
                fprintf(stderr, "[BLOOM CLI] Invalid --ncd-thr (expected 0..64)\n");
                return false;
            }
        } else if (strcmp(arg, "--top") == 0) {
            if (i + 1 >= argc || !cli_parse_u32(argv[++i], &out->ncd_top) ||
                out->ncd_top == 0U || out->ncd_top > 64U) {
                fprintf(stderr, "[BLOOM CLI] Invalid --top (expected 1..64)\n");
                return false;
            }
        } else if (strcmp(arg, "--searu-cnf") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Missing path after --searu-cnf\n");
                return false;
            }
            out->searu_cnf_path = argv[++i];
            out->headless = true;
        } else if (strcmp(arg, "--searu-bench-3sat") == 0) {
            int32_t v = 0, c = 0;
            if (i + 1 >= argc || !cli_parse_i32_pair(argv[++i], &v, &c) ||
                v < 3 || v > 256 || c < 1 || c > 8192) {
                fprintf(stderr, "[BLOOM CLI] Invalid --searu-bench-3sat (expected V,C with V=3..256, C=1..8192)\n");
                return false;
            }
            out->searu_bench_3sat = true;
            out->searu_3sat_vars = (uint32_t)v;
            out->searu_3sat_clauses = (uint32_t)c;
            out->headless = true;
        } else if (strcmp(arg, "--searu-bench-color") == 0) {
            int32_t n = 0, k = 0;
            if (i + 1 >= argc || !cli_parse_i32_pair(argv[++i], &n, &k) ||
                n < 2 || n > 64 || k < 2 || k > 16) {
                fprintf(stderr, "[BLOOM CLI] Invalid --searu-bench-color (expected N,K with N=2..64, K=2..16)\n");
                return false;
            }
            out->searu_bench_color = true;
            out->searu_color_nodes = (uint32_t)n;
            out->searu_color_k = (uint32_t)k;
            out->headless = true;
        } else if (strcmp(arg, "--universes") == 0) {
            if (i + 1 >= argc || !cli_parse_u32(argv[++i], &out->searu_universes) ||
                out->searu_universes == 0U || out->searu_universes > 8192U) {
                fprintf(stderr, "[BLOOM CLI] Invalid --universes (expected 1..8192)\n");
                return false;
            }
        } else if (strcmp(arg, "--searu-iters") == 0) {
            if (i + 1 >= argc || !cli_parse_u32(argv[++i], &out->searu_iters) ||
                out->searu_iters == 0U || out->searu_iters > 4096U) {
                fprintf(stderr, "[BLOOM CLI] Invalid --searu-iters (expected 1..4096)\n");
                return false;
            }
        } else if (strcmp(arg, "--searu-rounds") == 0) {
            if (i + 1 >= argc || !cli_parse_u32(argv[++i], &out->searu_rounds) ||
                out->searu_rounds == 0U || out->searu_rounds > 32U) {
                fprintf(stderr, "[BLOOM CLI] Invalid --searu-rounds (expected 1..32)\n");
                return false;
            }
        } else if (strcmp(arg, "--verify-cpu") == 0) {
            out->verify_cpu = true;
        } else if (strcmp(arg, "--sha256") == 0) {
            out->emit_sha256 = true;
            out->headless = true;
        } else if (strcmp(arg, "--kitty") == 0) {
            out->emit_kitty = true;
            out->headless = true;
        } else if (strcmp(arg, "--impact") == 0) {
            const char *endp = NULL;
            if (i + 1 >= argc || !cli_parse_q16(argv[++i], &out->impact_vx_q16, &endp) ||
                !endp || *endp != '\0') {
                fprintf(stderr, "[BLOOM CLI] Invalid --impact velocity\n");
                return false;
            }
            out->has_impact = true;
        } else if (strcmp(arg, "--audio-out") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Missing path after --audio-out\n");
                return false;
            }
            out->out_wav_path = argv[++i];
            out->headless = true;
        } else if (strcmp(arg, "--corner") == 0) {
            out->set_corner = true;
        } else if (strcmp(arg, "--inspect") == 0) {
            out->inspect_cae = true;
        } else if (strcmp(arg, "--section") == 0) {
            if (i + 1 >= argc || !cli_parse_section_vec4(argv[++i], out->sec_norm_q16, &out->sec_dist_q16)) {
                fprintf(stderr, "[BLOOM CLI] Invalid --section (expected nx,ny,nz,d)\n");
                return false;
            }
            out->set_section = true;
        } else if (strcmp(arg, "--slab") == 0) {
            const char *endp = NULL;
            if (i + 1 >= argc || !cli_parse_q16(argv[++i], &out->slab_half_q16, &endp) ||
                !endp || *endp != '\0' || out->slab_half_q16 <= 0) {
                fprintf(stderr, "[BLOOM CLI] Invalid --slab half-thickness\n");
                return false;
            }
            out->set_slab = true;
        } else if (strcmp(arg, "--out") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Missing path after --out\n");
                return false;
            }
            out->out_ppm_path = argv[++i];
            out->headless = true;
        } else if (strcmp(arg, "--watch-svg") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Missing path after --watch-svg\n");
                return false;
            }
            out->watch_svg_path = argv[++i];
        } else if (strcmp(arg, "--shader") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "[BLOOM CLI] Missing path after --shader\n");
                return false;
            }
            out->shader_path = argv[++i];
        } else if (strcmp(arg, "--width") == 0) {
            if (i + 1 >= argc || !cli_parse_u32(argv[++i], &out->override_w) ||
                out->override_w == 0U || out->override_w > 4096U) {
                fprintf(stderr, "[BLOOM CLI] Invalid --width (expected 1..4096)\n");
                return false;
            }
        } else if (strcmp(arg, "--height") == 0) {
            if (i + 1 >= argc || !cli_parse_u32(argv[++i], &out->override_h) ||
                out->override_h == 0U || out->override_h > 4096U) {
                fprintf(stderr, "[BLOOM CLI] Invalid --height (expected 1..4096)\n");
                return false;
            }
        } else if (strcmp(arg, "--steps") == 0) {
            if (i + 1 >= argc || !cli_parse_u32(argv[++i], &out->steps) || out->steps > 100000U) {
                fprintf(stderr, "[BLOOM CLI] Invalid --steps (expected 0..100000)\n");
                return false;
            }
        } else if (arg[0] == '-') {
            fprintf(stderr, "[BLOOM CLI] Unknown flag: %s\n", arg);
            return false;
        } else if (!positional_set) {
            out->bloom_path = arg;
            positional_set = true;
        } else {
            fprintf(stderr, "[BLOOM CLI] Unexpected extra argument: %s\n", arg);
            return false;
        }
    }
    return true;
}

static bool cli_stat_mtime(const char *path, time_t *out_mtime) {
    if (!path || !out_mtime) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    *out_mtime = st.st_mtime;
    return true;
}

static bool cli_load_bloom_file(const char *path, BloomEngineArena *out_arena) {
    if (!path || !out_arena) return false;
    struct stat st;
    if (stat(path, &st) != 0 || (size_t)st.st_size != sizeof(BloomEngineArena)) {
        return false;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) return false;
    size_t nr = fread(out_arena, 1U, sizeof(BloomEngineArena), fp);
    fclose(fp);
    if (nr != sizeof(BloomEngineArena)) {
        return false;
    }
    if (out_arena->map.frame_hdr.magic != BLOOM_MAGIC_U32 ||
        out_arena->map.frame_hdr.version != BLOOM_VERSION_U32) {
        return false;
    }
    if (!bloom_image_verify_arena(out_arena)) {
        return false;
    }
    char file_sha[65];
    char mem_sha[65];
    if (flow_sha256_file_hex(path, file_sha) != 0) {
        return false;
    }
    bloom_arena_sha256_hex(out_arena, mem_sha);
    return memcmp(file_sha, mem_sha, 64U) == 0;
}

static bool cli_ends_with_ci(const char *s, const char *suf) {
    if (!s || !suf) return false;
    size_t sl = strlen(s), xl = strlen(suf);
    if (sl < xl) return false;
    for (size_t i = 0U; i < xl; ++i) {
        char a = s[sl - xl + i], b = suf[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - ('A' - 'a'));
        if (b >= 'A' && b <= 'Z') b = (char)(b - ('A' - 'a'));
        if (a != b) return false;
    }
    return true;
}

static bool cli_load_input_asset(const char *path,
                                 BloomEngineArena *out_arena,
                                 BloomMeshArena *out_mesh,
                                 bool *out_is_mesh) {
    if (!path || !out_arena || !out_mesh || !out_is_mesh) return false;
    *out_is_mesh = false;
    if (cli_load_bloom_file(path, out_arena)) {
        return true;
    }

    struct stat st;
    if (stat(path, &st) != 0 || st.st_size < 28 || (uint64_t)st.st_size > 16777216ULL) {
        return false;
    }
    size_t fsize = (size_t)st.st_size;
    uint8_t *raw = (uint8_t *)malloc(fsize);
    if (!raw) return false;
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        free(raw);
        return false;
    }
    size_t nr = fread(raw, 1U, fsize, fp);
    fclose(fp);
    if (nr != fsize) {
        free(raw);
        return false;
    }

    bool is_glb = (fsize >= 12U && raw[0] == 'g' && raw[1] == 'l' && raw[2] == 'T' && raw[3] == 'F') ||
                  cli_ends_with_ci(path, ".glb");
    uint32_t tri_cnt = 0U;
    bool parsed = false;
    if (is_glb) {
        parsed = bloom_glb_parse_to_mesh64(raw, fsize, BLOOM_MATTER_TITANIUM, true, out_mesh, &tri_cnt);
    } else if (cli_ends_with_ci(path, ".stl") || (fsize >= 84U && ((fsize - 84U) % 50U) == 0U)) {
        parsed = bloom_stl_parse_to_mesh64(raw, fsize, BLOOM_MATTER_TITANIUM, true, out_mesh, &tri_cnt);
    }
    free(raw);
    if (!parsed || tri_cnt == 0U) {
        return false;
    }

    bloom_arena_init_default(out_arena, 1280U, 720U);
    out_arena->map.frame_hdr.flags |= BLOOM_FLAG_STUDIO_GI;
    bloom_camera_init64(&out_arena->map.camera, 36000, 22000, 4 * BLOOM_Q16_ONE,
                        35 * BLOOM_Q16_ONE, 183500);
    for (uint32_t c = 0U; c < 4U; ++c) {
        int32_t v = (c == 0U) ? out_arena->map.camera.orbit_yaw_q16
                  : ((c == 1U) ? out_arena->map.camera.orbit_pitch_q16
                  : out_arena->map.camera.orbit_dist_q16);
        out_arena->map.flow_states[c][3] = (uint64_t)(int64_t)v;
        out_arena->map.flow_states[c][4] = 0ULL;
    }

    BloomSectionSpec64 *sec = &out_arena->map.section_spec;
    sec->mesh_tri_count = (uint16_t)tri_cnt;
    sec->mesh_bvh_count = (uint16_t)bloom_bvh_build_from_tris64(
        out_mesh->map.tris, tri_cnt, out_mesh->map.bvh_nodes, BLOOM_MAX_MESH_BVH_NODES);
    sec->mesh_matter_style = (uint32_t)BLOOM_MATTER_TITANIUM | (4U << 16U);
    sec->inspect_flags = BLOOM_INSPECT_ISOLINES | BLOOM_INSPECT_WALL_DFM |
                         BLOOM_INSPECT_INTERFERENCE | BLOOM_INSPECT_TRIPLANAR |
                         BLOOM_INSPECT_MESH_SOLID;
    cli_stamp_mac(sec, 0x5EC70064ULL, &sec->mac_lo32, &sec->mac_hi32);
    *out_is_mesh = true;
    return true;
}

static char cli_ascii_upper(char c) {
    if (c >= 'a' && c <= 'z') {
        return (char)(c - ('a' - 'A'));
    }
    return c;
}

static bool cli_has_token_ci(const char *buf, size_t len, const char *tok) {
    size_t tlen = strlen(tok);
    if (tlen == 0U || len < tlen) return false;
    for (size_t i = 0U; i + tlen <= len; ++i) {
        bool prev_sep = (i == 0U) ||
                        !((buf[i - 1U] >= 'A' && buf[i - 1U] <= 'Z') ||
                          (buf[i - 1U] >= 'a' && buf[i - 1U] <= 'z') ||
                          (buf[i - 1U] >= '0' && buf[i - 1U] <= '9'));
        bool next_sep = (i + tlen == len) ||
                        !((buf[i + tlen] >= 'A' && buf[i + tlen] <= 'Z') ||
                          (buf[i + tlen] >= 'a' && buf[i + tlen] <= 'z') ||
                          (buf[i + tlen] >= '0' && buf[i + tlen] <= '9'));
        if (!prev_sep || !next_sep) continue;
        bool match = true;
        for (size_t k = 0U; k < tlen; ++k) {
            if (cli_ascii_upper(buf[i + k]) != cli_ascii_upper(tok[k])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

static uint32_t cli_resolve_target_inst_idx(const BloomEngineArena *arena) {
    uint32_t cnt = arena->map.frame_hdr.inst_count;
    if (cnt == 0U) return 0xFFFFFFFFU;
    if (cnt > BLOOM_MAX_INSTANCES) cnt = BLOOM_MAX_INSTANCES;
    uint32_t sel = arena->map.frame_hdr.selected_inst_id;
    if (sel >= 1U && sel <= cnt) {
        return sel - 1U;
    }
    return 0U;
}

static void cli_apply_studio_action(BloomStudioRuntime *rt, uint32_t action) {
    BloomEngineArena *arena = rt->arena;
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    uint32_t idx = cli_resolve_target_inst_idx(arena);

    if (action == BLOOM_ACTION_TOGGLE_TRC) {
        hdr->flags ^= BLOOM_FLAG_TRC_SAFE;
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
        return;
    }
    if (idx >= BLOOM_MAX_INSTANCES) return;

    BloomPrefabInst64 *inst = &arena->map.instances[idx];
    uint32_t pid = inst->prefab_id;
    const BloomPrefabDesc64 *ssot =
        (pid >= 1U && pid <= BLOOM_MAX_PREFABS) ? &arena->map.prefabs[pid - 1U] : NULL;

    if (action == BLOOM_ACTION_PREFAB_REVERT) {
        bloom_prefab_revert_to_ssot(inst, ssot);
        (void)bloom_arena_try_swap_chart(arena, inst->flow_slot, BLOOM_CHART_PREFAB_NORMAL, rt->mode_word);
        rt->alert_toggled = false;
        bloom_reg_bank_set_q16(&arena->map.reg_bank, 1U, 52428);
        bloom_reg_bank_set_q16(&arena->map.reg_bank, 2U, 300);
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    } else if (action == BLOOM_ACTION_CYCLE_MATTER) {
        uint32_t next_mat = (rt->matter_cursor % 7U) + 1U;
        rt->matter_cursor = next_mat;
        bloom_prefab_override_inst(inst, next_mat, next_mat, 7864);
        bloom_reg_bank_set_q16(&arena->map.reg_bank, 2U, (int32_t)(next_mat * 111U));
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    } else if (action == BLOOM_ACTION_SWAP_CHART) {
        bool next_alert = !rt->alert_toggled;
        rt->alert_toggled = next_alert;
        uint32_t tgt_chart = next_alert ? BLOOM_CHART_PREFAB_ALERT : BLOOM_CHART_PREFAB_NORMAL;
        (void)bloom_arena_try_swap_chart(arena, inst->flow_slot, tgt_chart, rt->mode_word);
        inst->thermal_kelvin = next_alert ? 860U : 300U;
        bloom_reg_bank_set_q16(&arena->map.reg_bank, 1U, next_alert ? 62259 : 52428);
        bloom_reg_bank_set_q16(&arena->map.reg_bank, 2U, next_alert ? 860 : 300);
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
}

static void cli_exec_text_command(BloomStudioRuntime *rt) {
    BloomEngineArena *arena = rt->arena;
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    BloomTextInput64 *tinput = &arena->map.text_input;
    BloomIme64 *ime = &arena->map.ime;

    if (ime->ime_active != 0U && ime->preedit_len > 0U) {
        (void)bloom_ime_commit64(ime, tinput);
    }

    size_t len = (tinput->byte_len <= BLOOM_TEXT_INPUT_CAP) ? (size_t)tinput->byte_len : 0U;
    const char *buf = tinput->utf8_buf;

    if (cli_has_token_ci(buf, len, "CLEAR")) {
        uint32_t bnode = tinput->bound_node_id;
        uint32_t sact = tinput->submit_action_id;
        bloom_text_input_init64(tinput, bnode, sact);
        tinput->focused = 1U;
        cli_stamp_mac(tinput, 0x7EC70064ULL, &tinput->hash_lo32, &tinput->hash_hi32);
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
        return;
    }

    if (cli_has_token_ci(buf, len, "XRAY")) {
        hdr->flags ^= BLOOM_FLAG_XRAY_ENABLED;
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    if (cli_has_token_ci(buf, len, "RC")) {
        hdr->flags ^= BLOOM_FLAG_RADIANCE_CASCADES;
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    if (cli_has_token_ci(buf, len, "FIELD")) {
        hdr->flags ^= BLOOM_FLAG_DIFF_FIELD;
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    if (cli_has_token_ci(buf, len, "STRIKE")) {
        uint32_t sidx = cli_resolve_target_inst_idx(arena);
        if (sidx < BLOOM_MAX_INSTANCES) {
            bloom_audio_excite_impact64(&rt->audio, arena, sidx, BLOOM_Q16_ONE * 4,
                                        arena->map.instances[sidx].pos_q16[0] >> 2);
        }
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    if (cli_has_token_ci(buf, len, "CINE")) {
        hdr->flags ^= BLOOM_FLAG_CINE_OPTICS;
        arena->map.camera.aperture_fstop_q16 = 117964; /* f/1.8 */
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    if (cli_has_token_ci(buf, len, "LENS")) {
        hdr->flags ^= BLOOM_FLAG_METRIC_FIELD;
        bloom_reg_bank_set_q16(&arena->map.reg_bank, BLOOM_REG_METRIC_PHI, 16384);
        bloom_reg_bank_set_q16(&arena->map.reg_bank, BLOOM_REG_LORENTZ_BETA, 26214);
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    if (cli_has_token_ci(buf, len, "SHELLS")) {
        hdr->flags ^= BLOOM_FLAG_GAMEPLAY_SHELLS;
        if (rt->gameplay.player_inst_id == 0U) {
            bloom_gameplay_init64(&rt->gameplay, 1U, 16384, 32768, 65536);
        }
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    if (cli_has_token_ci(buf, len, "REWIND")) {
        if (rt->rewind_ring && rt->rewind_ctrl.count > 1U) {
            uint32_t back = (rt->rewind_ctrl.count > 30U) ? 30U : (rt->rewind_ctrl.count - 1U);
            (void)bloom_rewind_restore(&rt->rewind_ctrl, rt->rewind_ring, back, arena, &rt->audio);
            hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
        }
    }
    if (cli_has_token_ci(buf, len, "SECTION")) {
        BloomSectionSpec64 *sec = &arena->map.section_spec;
        uint32_t next_m = (sec->section_mode == BLOOM_SEC_MODE_OFF) ? BLOOM_SEC_MODE_PLANE : BLOOM_SEC_MODE_OFF;
        bloom_section_init64(sec, next_m, sec->plane_norm_q16, hdr->xray_plane_q16,
                             sec->slab_half_q16, sec->min_wall_q16, sec->iso_pitch_q16,
                             sec->inspect_flags);
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    if (cli_has_token_ci(buf, len, "SLAB")) {
        BloomSectionSpec64 *sec = &arena->map.section_spec;
        bloom_section_init64(sec, BLOOM_SEC_MODE_SLAB, sec->plane_norm_q16, hdr->xray_plane_q16,
                             sec->slab_half_q16, sec->min_wall_q16, sec->iso_pitch_q16,
                             sec->inspect_flags);
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    if (cli_has_token_ci(buf, len, "CORNER")) {
        BloomSectionSpec64 *sec = &arena->map.section_spec;
        bloom_section_init64(sec, BLOOM_SEC_MODE_CORNER, sec->plane_norm_q16, hdr->xray_plane_q16,
                             sec->slab_half_q16, sec->min_wall_q16, sec->iso_pitch_q16,
                             sec->inspect_flags);
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    if (cli_has_token_ci(buf, len, "STEP")) {
        bloom_physics_step_arena64(arena, BLOOM_Q16_ONE);
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }

    if (cli_has_token_ci(buf, len, "REVERT")) {
        cli_apply_studio_action(rt, BLOOM_ACTION_PREFAB_REVERT);
        return;
    }

    uint32_t idx = cli_resolve_target_inst_idx(arena);
    if (idx < BLOOM_MAX_INSTANCES) {
        BloomPrefabInst64 *inst = &arena->map.instances[idx];
        if (cli_has_token_ci(buf, len, "JADE")) {
            bloom_prefab_override_inst(inst, 2U, BLOOM_MATTER_IMPERIAL_JADE, 0);
            bloom_reg_bank_set_q16(&arena->map.reg_bank, 2U, 155);
            hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
        } else if (cli_has_token_ci(buf, len, "GOLD")) {
            bloom_prefab_override_inst(inst, 5U, BLOOM_MATTER_AURUM_GOLD, 0);
            bloom_reg_bank_set_q16(&arena->map.reg_bank, 2U, 444);
            hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
        } else if (cli_has_token_ci(buf, len, "GLASS")) {
            bloom_prefab_override_inst(inst, 1U, BLOOM_MATTER_CRYSTAL_GLASS, 0);
            bloom_reg_bank_set_q16(&arena->map.reg_bank, 2U, 152);
            hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
        } else if (cli_has_token_ci(buf, len, "SAPPHIRE")) {
            bloom_prefab_override_inst(inst, 6U, BLOOM_MATTER_SAPPHIRE, 0);
            bloom_reg_bank_set_q16(&arena->map.reg_bank, 2U, 555);
            hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
        } else if (cli_has_token_ci(buf, len, "TITANIUM")) {
            bloom_prefab_override_inst(inst, 4U, BLOOM_MATTER_TITANIUM, 0);
            bloom_reg_bank_set_q16(&arena->map.reg_bank, 2U, 333);
            hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
        }

        if (cli_has_token_ci(buf, len, "ALERT") || cli_has_token_ci(buf, len, "860K")) {
            rt->alert_toggled = true;
            (void)bloom_arena_try_swap_chart(arena, inst->flow_slot, BLOOM_CHART_PREFAB_ALERT, rt->mode_word);
            inst->thermal_kelvin = 860U;
            bloom_reg_bank_set_q16(&arena->map.reg_bank, 1U, 62259);
            bloom_reg_bank_set_q16(&arena->map.reg_bank, 2U, 860);
            hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
        } else if (cli_has_token_ci(buf, len, "NORMAL") || cli_has_token_ci(buf, len, "300K")) {
            rt->alert_toggled = false;
            (void)bloom_arena_try_swap_chart(arena, inst->flow_slot, BLOOM_CHART_PREFAB_NORMAL, rt->mode_word);
            inst->thermal_kelvin = 300U;
            bloom_reg_bank_set_q16(&arena->map.reg_bank, 1U, 52428);
            bloom_reg_bank_set_q16(&arena->map.reg_bank, 2U, 300);
            hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
        }
    }
}

static void cli_poll_hot_reload(BloomStudioRuntime *rt) {
    BloomEngineArena *arena = rt->arena;
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    BloomDomState64 *dom = &arena->map.dom;

    time_t cur_mtime = 0;
    if (!rt->mesh_mode && rt->bloom_path && cli_stat_mtime(rt->bloom_path, &cur_mtime) &&
        cur_mtime != rt->last_bloom_mtime) {
        if (cli_load_bloom_file(rt->bloom_path, rt->staging)) {
            BloomCamera64 saved_cam = arena->map.camera;
            BloomWindow64 saved_win = arena->map.window;
            uint32_t saved_sel = hdr->selected_inst_id;
            uint32_t saved_w = hdr->width_px;
            uint32_t saved_h = hdr->height_px;
            uint32_t next_epoch = dom->hot_reload_epoch + 1U;
            uint64_t saved_cam_flow[4][8];
            memcpy(saved_cam_flow, &arena->map.flow_states[0], sizeof(saved_cam_flow));

            memcpy(arena, rt->staging, sizeof(BloomEngineArena));

            arena->map.camera = saved_cam;
            arena->map.window = saved_win;
            memcpy(&arena->map.flow_states[0], saved_cam_flow, sizeof(saved_cam_flow));
            hdr->width_px = saved_w;
            hdr->height_px = saved_h;
            if (saved_sel <= hdr->inst_count) {
                hdr->selected_inst_id = saved_sel;
            }
            dom->hot_reload_epoch = next_epoch;
            cli_stamp_mac(dom, 0xD0A00064ULL, &dom->mac_lo32, &dom->mac_hi32);
            hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
            rt->last_bloom_mtime = cur_mtime;
        }
    }

    time_t svg_mtime = 0;
    if (rt->watch_svg_path && cli_stat_mtime(rt->watch_svg_path, &svg_mtime) &&
        svg_mtime != rt->last_svg_mtime) {
        size_t g_cnt = 0U;
        size_t n_nodes = bloom_svg_compile_file(rt->watch_svg_path,
                                                arena->map.ui_nodes,
                                                BLOOM_MAX_UI_NODES,
                                                arena->map.glyphs,
                                                BLOOM_MAX_GLYPHS,
                                                &g_cnt,
                                                dom);
        if (n_nodes > 0U) {
            hdr->ui_count = (uint32_t)n_nodes;
            hdr->glyph_count = (uint32_t)g_cnt;
            hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
        }
        rt->last_svg_mtime = svg_mtime;
    }
}

static void cli_studio_step_cb(void *raw_ctx) {
    BloomStudioRuntime *rt = (BloomStudioRuntime *)raw_ctx;
    BloomEngineArena *arena = rt->arena;
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    BloomWindow64 *win = &arena->map.window;
    BloomCamera64 *cam = &arena->map.camera;
    BloomDomState64 *dom = &arena->map.dom;
    BloomTextInput64 *tinput = &arena->map.text_input;
    BloomIme64 *ime = &arena->map.ime;

    rt->frame_tick++;
    if ((rt->frame_tick % 15U) == 0U) {
        cli_poll_hot_reload(rt);
    }

    bool down = (win->host_flags & BLOOM_WIN_FLAG_MOUSE_DOWN) != 0U;
    bool clicked = (win->host_flags & BLOOM_WIN_FLAG_JUST_CLICKED) != 0U;
    bool rclick = (win->host_flags & BLOOM_WIN_FLAG_RIGHT_CLICK) != 0U;

    uint32_t action = bloom_ui_step_pointer_arena(
        arena, win->pointer_x_q16, win->pointer_y_q16, win->drag_dx_q16, down, clicked);

    if (clicked) {
        if (tinput->bound_node_id != 0U &&
            (action == BLOOM_ACTION_FOCUS_INPUT || dom->active_node_id == tinput->bound_node_id)) {
            tinput->focused = 1U;
            cli_stamp_mac(tinput, 0x7EC70064ULL, &tinput->hash_lo32, &tinput->hash_hi32);
            dom->focused_input_id = tinput->bound_node_id;
            cli_stamp_mac(dom, 0xD0A00064ULL, &dom->mac_lo32, &dom->mac_hi32);
            hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
        } else {
            if (tinput->focused != 0U) {
                tinput->focused = 0U;
                cli_stamp_mac(tinput, 0x7EC70064ULL, &tinput->hash_lo32, &tinput->hash_hi32);
                dom->focused_input_id = 0U;
                cli_stamp_mac(dom, 0xD0A00064ULL, &dom->mac_lo32, &dom->mac_hi32);
                (void)bloom_ime_set_preedit64(ime, "", 0U);
                hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
            }
            if (action == BLOOM_ACTION_NONE && dom->hovered_node_id == 0U) {
                BloomRaycastHit64 hit;
                if (bloom_scene_raycast_ex64(arena, rt->mesh_mode ? rt->mesh : NULL,
                                             win->pointer_x_q16, win->pointer_y_q16, &hit) &&
                    hit.hit_valid != 0U) {
                    hdr->selected_inst_id = hit.hit_inst_id;
                    bloom_reg_bank_set_q16(&arena->map.reg_bank, 2U, (int32_t)(hit.hit_inst_id * 100U));
                    uint32_t sidx = (hit.hit_inst_id > 0U && hit.hit_inst_id <= BLOOM_MAX_INSTANCES)
                        ? (hit.hit_inst_id - 1U) : 0U;
                    bloom_audio_excite_impact64(&rt->audio, arena, sidx, BLOOM_Q16_ONE * 3,
                                                hit.hit_pos_q16[0] >> 2);
                    hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
                }
            }
        }
    }

    if (down && dom->captured_node_id == 0U && dom->hovered_node_id == 0U &&
        (win->drag_dx_q16 != 0 || win->drag_dy_q16 != 0)) {
        cam->orbit_yaw_q16 = cli_clamp64(
            (int64_t)cam->orbit_yaw_q16 + (int64_t)win->drag_dx_q16 * 2LL,
            -BLOOM_MAX_Q16, BLOOM_MAX_Q16);
        cam->orbit_pitch_q16 = cli_clamp64(
            (int64_t)cam->orbit_pitch_q16 + (int64_t)win->drag_dy_q16,
            -85000, 85000);
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }

    if (win->scroll_dy_q16 != 0) {
        cam->orbit_dist_q16 = cli_clamp64(
            (int64_t)cam->orbit_dist_q16 - (int64_t)win->scroll_dy_q16 * 4LL,
            2 * BLOOM_Q16_ONE, 14 * BLOOM_Q16_ONE);
        cam->focus_dist_q16 = cam->orbit_dist_q16;
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }

    if (rclick) {
        action = BLOOM_ACTION_PREFAB_REVERT;
    }
    if (action != BLOOM_ACTION_NONE && action != BLOOM_ACTION_FOCUS_INPUT) {
        cli_apply_studio_action(rt, action);
    }

    if (win->key_code == 13U && tinput->focused != 0U) {
        cli_exec_text_command(rt);
    }

    (void)bloom_arena_step_dynamics(arena, rt->mode_word);
    if ((arena->map.frame_hdr.flags & BLOOM_FLAG_GAMEPLAY_SHELLS) != 0U) {
        (void)bloom_gameplay_step64(&rt->gameplay, arena, &rt->audio, NULL, false);
    }
    if ((arena->map.frame_hdr.flags & BLOOM_FLAG_CINE_OPTICS) != 0U) {
        bloom_camera_step_cine64(&arena->map.camera, &arena->map.reg_bank, NULL, 8192, 0);
    }
    if (rt->rewind_ring && (arena->map.frame_hdr.flags & BLOOM_FLAG_IDLE_SLEEP) == 0U) {
        bloom_rewind_push(&rt->rewind_ctrl, rt->rewind_ring, arena, &rt->audio);
    }
    {
        int16_t pcm[1600];
        (void)bloom_audio_step_frame64(&rt->audio, arena, pcm, 800U);
        (void)bloom_gpu_push_audio_i16(rt->gpu, pcm, 800U);
    }
    (void)bloom_gpu_render(rt->gpu, false);
}

static bool cli_buf_next_token(const uint8_t *buf, size_t len, size_t *io_pos, char *tok, size_t cap) {
    size_t p = *io_pos;
    while (p < len) {
        uint8_t c = buf[p];
        if (c == '#') {
            while (p < len && buf[p] != '\n') ++p;
            continue;
        }
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++p;
            continue;
        }
        break;
    }
    size_t i = 0U;
    while (p < len) {
        uint8_t c = buf[p];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            ++p;
            break;
        }
        if (c == '#') {
            while (p < len && buf[p] != '\n') ++p;
            if (p < len) ++p;
            break;
        }
        if (i + 1U < cap) tok[i++] = (char)c;
        ++p;
    }
    tok[i] = '\0';
    *io_pos = p;
    return i > 0U;
}

static bool cli_import_mood_ppm(const char *ppm_path, BloomEngineArena *arena) {
    if (!ppm_path || !arena) return false;
    struct stat st;
    if (stat(ppm_path, &st) != 0 || st.st_size < 11 || (uint64_t)st.st_size > 67108864ULL) {
        return false;
    }
    size_t fsize = (size_t)st.st_size;
    uint8_t *raw = (uint8_t *)malloc(fsize);
    if (!raw) return false;
    FILE *fp = fopen(ppm_path, "rb");
    if (!fp) {
        free(raw);
        return false;
    }
    size_t nr = fread(raw, 1U, fsize, fp);
    fclose(fp);
    if (nr != fsize) {
        free(raw);
        return false;
    }

    size_t pos = 0U;
    char tok[32];
    uint32_t w = 0U, h = 0U, mv = 0U;
    bool hdr_ok = cli_buf_next_token(raw, fsize, &pos, tok, sizeof(tok)) && strcmp(tok, "P6") == 0 &&
                  cli_buf_next_token(raw, fsize, &pos, tok, sizeof(tok)) && cli_parse_u32(tok, &w) &&
                  cli_buf_next_token(raw, fsize, &pos, tok, sizeof(tok)) && cli_parse_u32(tok, &h) &&
                  cli_buf_next_token(raw, fsize, &pos, tok, sizeof(tok)) && cli_parse_u32(tok, &mv) &&
                  mv == 255U && w >= 2U && h >= 2U && w <= 4096U && h <= 4096U;
    size_t rgb_len = (size_t)w * (size_t)h * 3U;
    if (!hdr_ok || pos > fsize || (fsize - pos) < rgb_len) {
        free(raw);
        return false;
    }

    BloomMoodboard64 mood;
    bool extracted = bloom_moodboard_extract_rgb24(raw + pos, w, h, w * 3U, &mood);
    free(raw);
    if (!extracted) return false;

    bloom_moodboard_compile_to_ssot64(&mood, 8U, 9U, &arena->map.styles[7], &arena->map.prefabs[8]);
    if (arena->map.frame_hdr.inst_count > 0U) {
        arena->map.instances[0].prefab_id = 9U;
        bloom_prefab_override_inst(&arena->map.instances[0], 8U, mood.inferred_matter_id, 0);
    } else {
        const int32_t orig[3] = { 0, 0, 0 };
        (void)bloom_prefab_spawn_inst(arena, 9U, orig, BLOOM_Q16_ONE);
    }
    arena->map.frame_hdr.flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    fprintf(stderr, "[BLOOM MOOD] %s -> matter=%u dom=#%06x acc=#%06x edge_q16=%d\n",
            ppm_path, mood.inferred_matter_id, mood.dominant_rgb24, mood.accent_rgb24, mood.edge_density_q16);
    return true;
}

static uint32_t cli_fir_fmix32(uint32_t h) {
    h ^= h >> 16U;
    h *= UINT32_C(0x85EBCA6B);
    h ^= h >> 13U;
    h *= UINT32_C(0xC2B2AE35);
    h ^= h >> 16U;
    return h;
}

static uint64_t cli_monotonic_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec * 1000000ULL) + ((uint64_t)ts.tv_nsec / 1000ULL);
}

static uint8_t *cli_read_fir_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fprintf(stderr, "[BLOOM FIR] Cannot open .fir file: %s\n", path);
        return NULL;
    }
    uint8_t *fir_buf = (uint8_t *)malloc(FLOW_IR_MAX_FILE_BYTES + 1U);
    if (!fir_buf) {
        fclose(fp);
        return NULL;
    }
    size_t fir_len = fread(fir_buf, 1U, FLOW_IR_MAX_FILE_BYTES + 1U, fp);
    fclose(fp);
    if (fir_len < FLOW_IR_HEADER_BYTES + 8U || fir_len > FLOW_IR_MAX_FILE_BYTES) {
        fprintf(stderr, "[BLOOM FIR] Invalid .fir file size (%zuB): %s\n", fir_len, path);
        free(fir_buf);
        return NULL;
    }
    *out_len = fir_len;
    return fir_buf;
}

static int cli_run_compile_fir_wgsl(const BloomCliArgs *args) {
    size_t fir_len = 0U;
    uint8_t *fir_buf = cli_read_fir_file(args->compile_fir_in, &fir_len);
    if (!fir_buf) {
        return 1;
    }
    uint32_t gates = 0U, in_bits = 0U, out0_bits = 0U;
    bool ok = bloom_fir1_compile_wgsl(fir_buf, fir_len, args->compile_wgsl_out,
                                      &gates, &in_bits, &out0_bits);
    free(fir_buf);
    if (!ok) {
        fprintf(stderr, "[BLOOM AOT-WGSL] Failed to compile %s -> %s\n",
                args->compile_fir_in, args->compile_wgsl_out);
        return 1;
    }
    printf("[BLOOM AOT-WGSL] in=%s out=%s gates=%u in_bits=%u out0_bits=%u\n",
           args->compile_fir_in, args->compile_wgsl_out, gates, in_bits, out0_bits);
    return 0;
}

static int cli_run_gpu_hunt_command(const BloomCliArgs *args, BloomEngineArena *arena) {
    size_t fir_len = 0U;
    uint8_t *fir_buf = cli_read_fir_file(args->hunt_fir_path, &fir_len);
    if (!fir_buf) {
        return 1;
    }

    if ((args->hunt_bitslice || args->aot_shader_path != NULL) && args->hunt_prng) {
        fprintf(stderr, "[BLOOM GPU-HUNT] --bitslice / --aot-shader use bijective (gtid ^ seed) and cannot be combined with --hunt-prng\n");
        free(fir_buf);
        return 1;
    }

    arena->map.frame_hdr.width_px = 64U;
    arena->map.frame_hdr.height_px = 64U;
    BloomGpuCtx *gpu = bloom_gpu_init(arena, args->shader_path);
    if (!gpu) {
        free(fir_buf);
        return 1;
    }

    BloomFir1HuntConfig cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.num_trials = args->hunt_trials;
    cfg.target_mode = args->hunt_mode;
    cfg.sweep_mode = (args->hunt_bitslice || args->aot_shader_path != NULL)
                         ? 2U
                         : (args->hunt_prng ? 1U : 0U);
    cfg.vary_input_mask = args->hunt_vary_mask;
    cfg.seed_lo = (uint32_t)(args->hunt_seed & UINT64_C(0xFFFFFFFF));
    cfg.seed_hi = (uint32_t)(args->hunt_seed >> 32U);
    cfg.target_lo = (uint32_t)(args->hunt_target & UINT64_C(0xFFFFFFFF));
    cfg.target_hi = (uint32_t)(args->hunt_target >> 32U);

    BloomFir1HuntResult res;
    const char *engine_tag = "interp";
    bool ok = false;
    if (args->aot_shader_path != NULL) {
        engine_tag = "aot-wgsl";
        ok = bloom_gpu_hunt_fir1_aot(gpu, fir_buf, fir_len, args->aot_shader_path, &cfg, &res);
    } else if (args->hunt_bitslice) {
        engine_tag = "bitslice";
        ok = bloom_gpu_hunt_fir1_bitslice(gpu, fir_buf, fir_len, &cfg, &res);
    } else {
        ok = bloom_gpu_hunt_fir1(gpu, fir_buf, fir_len, &cfg, &res);
    }
    if (!ok) {
        fprintf(stderr, "[BLOOM GPU-HUNT] Failed (%s) on %s\n", engine_tag, args->hunt_fir_path);
        bloom_gpu_destroy(gpu);
        free(fir_buf);
        return 1;
    }

    int rc = 0;
    bool cpu_parity = res.found ? res.cpu_verified : true;
    if (args->verify_cpu) {
        flow_ir_prog_t *prog = (flow_ir_prog_t *)calloc(1U, sizeof(flow_ir_prog_t));
        if (!prog || !flow_ir_load_bytes(prog, fir_buf, fir_len)) {
            free(prog);
            bloom_gpu_destroy(gpu);
            free(fir_buf);
            return 1;
        }
        uint32_t cpu_hits = 0U;
        uint32_t cpu_first = 0xFFFFFFFFU;
        bool cpu_found = false;
        uint64_t cpu_xor = 0ULL;
        for (uint32_t tid = 0U; tid < cfg.num_trials; ++tid) {
            uint64_t ins[FLOW_IR_MAX_IO];
            uint64_t outs[FLOW_IR_MAX_IO];
            memset(ins, 0, sizeof(ins));
            memset(outs, 0, sizeof(outs));
            uint64_t ex_val = args->hunt_seed ^ (uint64_t)tid;
            uint32_t ex_bit = 0U;
            for (uint32_t i = 0U; i < prog->num_inputs; ++i) {
                uint64_t wm = flow_ir_mask_u64(prog->vals[i].width);
                uint64_t iv = cfg.base_inputs[i];
                uint32_t sh = (i < 31U) ? i : 31U;
                if (((cfg.vary_input_mask >> sh) & 1U) != 0U) {
                    if (cfg.sweep_mode == 2U) {
                        iv = (ex_bit < 64U) ? (ex_val >> ex_bit) : UINT64_C(0);
                        ex_bit += (uint32_t)prog->vals[i].width;
                    } else if (cfg.sweep_mode == 1U) {
                        uint32_t h0 = cli_fir_fmix32(cfg.seed_lo ^ tid ^ (i * UINT32_C(0x9E3779B9)));
                        uint32_t h1 = cli_fir_fmix32(cfg.seed_hi ^ (tid * UINT32_C(0x85EBCA6B)) ^
                                                     (i * UINT32_C(0xC2B2AE35)) ^ h0);
                        iv = ((uint64_t)h1 << 32U) | (uint64_t)h0;
                    } else if (i == 0U) {
                        iv += args->hunt_seed + (uint64_t)tid;
                    } else {
                        iv += (uint64_t)(tid ^ (i * UINT32_C(0x9E3779B9)));
                    }
                }
                ins[i] = iv & wm;
            }
            if (!flow_ir_eval_u64(prog, ins, outs)) {
                cpu_parity = false;
                break;
            }
            uint64_t out0 = (prog->num_outputs > 0U) ? outs[0] : 0ULL;
            cpu_xor ^= out0;
            bool m = (cfg.target_mode == BLOOM_FIR_HUNT_EQ) ? (out0 == args->hunt_target) :
                     ((cfg.target_mode == BLOOM_FIR_HUNT_NEQ) ? (out0 != args->hunt_target) : true);
            if (m) {
                cpu_hits++;
                if (!cpu_found) {
                    cpu_found = true;
                    cpu_first = tid;
                }
            }
        }
        if (cpu_hits != res.hit_count || cpu_first != res.first_hit_trial ||
            cpu_xor != res.out0_xor_checksum) {
            cpu_parity = false;
            rc = 1;
            fprintf(stderr,
                    "[BLOOM GPU-HUNT MISMATCH] GPU(hits=%u first=%u xor=0x%016" PRIx64
                    ") != CPU(hits=%u first=%u xor=0x%016" PRIx64 ")\n",
                    res.hit_count, res.first_hit_trial, res.out0_xor_checksum,
                    cpu_hits, cpu_first, cpu_xor);
        }
        free(prog);
    }

    double mevals = (double)res.trials_evaluated / (double)res.elapsed_us;
    printf("[BLOOM GPU-HUNT:%s] file=%s trials=%u hits=%u first_hit=%s xor=0x%016" PRIx64
           " time=%.2fms (%.2f M_evals/s) cpu_parity=%s GPU=%s\n",
           engine_tag,
           args->hunt_fir_path,
           res.trials_evaluated,
           res.hit_count,
           res.found ? "FOUND" : "NONE",
           res.out0_xor_checksum,
           (double)res.elapsed_us / 1000.0,
           mevals,
           cpu_parity ? "EXACT" : "FAIL",
           bloom_gpu_device_name(gpu));
    if (res.found && res.num_inputs > 0U && res.num_outputs > 0U) {
        printf("  witness[trial=%u]: in0=0x%016" PRIx64 " -> out0=0x%016" PRIx64 "\n",
               res.first_hit_trial, res.witness_inputs[0], res.witness_outputs[0]);
    }

    bloom_gpu_destroy(gpu);
    free(fir_buf);
    return rc;
}

static int cli_run_searu_bench_3sat(const BloomCliArgs *args, BloomEngineArena *arena) {
    uint32_t n_vars = args->searu_3sat_vars;
    uint32_t n_clauses = args->searu_3sat_clauses;
    uint32_t *clauses = (uint32_t *)calloc((size_t)n_clauses, sizeof(uint32_t));
    if (!clauses) return 1;

    uint64_t rng = (args->hunt_seed ^ UINT64_C(0x5A730001DEADBEEF)) | UINT64_C(1);
    uint64_t planted[4] = { 0ULL, 0ULL, 0ULL, 0ULL };
    for (uint32_t v = 0U; v < n_vars; ++v) {
        if ((searu_rng_next(&rng) & 1ULL) != 0ULL) {
            planted[v >> 6U] |= UINT64_C(1) << (v & 63U);
        }
    }
    uint64_t kan_pin[4] = { 0ULL, 0ULL, 0ULL, 0ULL };
    uint64_t init_canvas[4] = { 0ULL, 0ULL, 0ULL, 0ULL };
    uint32_t pin_cnt = n_vars / 12U;
    if (pin_cnt > 8U) pin_cnt = 8U;
    for (uint32_t k = 0U; k < pin_cnt; ++k) {
        uint32_t v = (k * (n_vars / (pin_cnt + 1U))) + 1U;
        if (v < n_vars) {
            uint64_t bit = UINT64_C(1) << (v & 63U);
            kan_pin[v >> 6U] |= bit;
            if ((planted[v >> 6U] & bit) != 0ULL) {
                init_canvas[v >> 6U] |= bit;
            }
        }
    }

    for (uint32_t ci = 0U; ci < n_clauses; ++ci) {
        uint32_t v0 = searu_Bounded_u32(searu_rng_next(&rng), n_vars);
        uint32_t v1 = searu_Bounded_u32(searu_rng_next(&rng), n_vars);
        uint32_t v2 = searu_Bounded_u32(searu_rng_next(&rng), n_vars);
        while (v1 == v0) v1 = searu_Bounded_u32(searu_rng_next(&rng), n_vars);
        while (v2 == v0 || v2 == v1) v2 = searu_Bounded_u32(searu_rng_next(&rng), n_vars);

        uint32_t neg0 = 0U, neg1 = 0U, neg2 = 0U;
        for (uint32_t t = 0U; t < 16U; ++t) {
            uint64_t r = searu_rng_next(&rng);
            neg0 = (uint32_t)(r & 1ULL);
            neg1 = (uint32_t)((r >> 1U) & 1ULL);
            neg2 = (uint32_t)((r >> 2U) & 1ULL);
            uint32_t b0 = (uint32_t)((planted[v0 >> 6U] >> (v0 & 63U)) & 1ULL) ^ neg0;
            uint32_t b1 = (uint32_t)((planted[v1 >> 6U] >> (v1 & 63U)) & 1ULL) ^ neg1;
            uint32_t b2 = (uint32_t)((planted[v2 >> 6U] >> (v2 & 63U)) & 1ULL) ^ neg2;
            if ((b0 | b1 | b2) != 0U) break;
        }
        uint32_t fb0 = (uint32_t)((planted[v0 >> 6U] >> (v0 & 63U)) & 1ULL) ^ neg0;
        uint32_t fb1 = (uint32_t)((planted[v1 >> 6U] >> (v1 & 63U)) & 1ULL) ^ neg1;
        uint32_t fb2 = (uint32_t)((planted[v2 >> 6U] >> (v2 & 63U)) & 1ULL) ^ neg2;
        if ((fb0 | fb1 | fb2) == 0U) {
            neg0 ^= 1U;
        }
        clauses[ci] = SEARU_SAT3_CLAUSE(v0, neg0, v1, neg1, v2, neg2);
    }

    arena->map.frame_hdr.width_px = 64U;
    arena->map.frame_hdr.height_px = 64U;
    BloomGpuCtx *gpu = bloom_gpu_init(arena, args->shader_path);
    if (!gpu) {
        free(clauses);
        return 1;
    }

    BloomSearuGpuConfig scfg;
    scfg.num_universes = args->searu_universes;
    scfg.iters_per_round = args->searu_iters;
    scfg.max_rounds = args->searu_rounds;
    scfg.seed = (args->hunt_seed != 0ULL) ? args->hunt_seed : UINT64_C(0xB100BEEF12345678);

    BloomSearuGpuResult gres;
    bool gpu_ok = bloom_gpu_searu_solve_3sat(gpu, clauses, n_clauses, n_vars,
                                             init_canvas, kan_pin, &scfg, &gres);
    double gpu_msteps = (gres.elapsed_us > 0ULL)
                            ? ((double)gres.total_steps_eval / (double)gres.elapsed_us)
                            : 0.0;
    printf("[BLOOM GPU-SEARU 3SAT] vars=%u clauses=%u unis=%u rounds=%u steps=%" PRIu64
           " best_e=%" PRIu64 " solved=%s win_uni=%u time=%.2fms (%.2f M_steps/s) cpu_verified=%s GPU=%s\n",
           n_vars, n_clauses, scfg.num_universes, gres.rounds_run, gres.total_steps_eval,
           gres.best_energy, gres.solved ? "SOLVED" : "UNSOLVED", gres.winning_universe,
           (double)gres.elapsed_us / 1000.0, gpu_msteps,
           gres.cpu_verified ? "EXACT" : "FAIL", bloom_gpu_device_name(gpu));

    if (args->verify_cpu) {
        searu_result cres;
        memset(&cres, 0, sizeof(cres));
        uint64_t t0 = cli_monotonic_us();
        (void)searu_solve_3sat(&cres, clauses, n_clauses, n_vars,
                               init_canvas, kan_pin, scfg.seed, UINT32_C(20000));
        uint64_t dt = cli_monotonic_us() - t0;
        if (dt == 0ULL) dt = 1ULL;
        printf("[BLOOM CPU-SEARU 3SAT] vars=%u clauses=%u iters=%u best_e=%" PRIu64
               " solved=%s time=%.2fms (%.2f k_steps/s)\n",
               n_vars, n_clauses, cres.iterations_run, cres.best_energy,
               (cres.best_energy == 0ULL) ? "SOLVED" : "UNSOLVED",
               (double)dt / 1000.0, (double)cres.iterations_run * 1000.0 / (double)dt);
    }

    bloom_gpu_destroy(gpu);
    free(clauses);
    return (gpu_ok && gres.cpu_verified && gres.solved) ? 0 : 1;
}

static int cli_run_searu_bench_color(const BloomCliArgs *args, BloomEngineArena *arena) {
    uint32_t n_nodes = args->searu_color_nodes;
    uint32_t k_col = args->searu_color_k;
    uint32_t max_edges = (n_nodes * (n_nodes - 1U)) / 2U;
    uint16_t *edges = (uint16_t *)calloc((size_t)(max_edges > 0U ? max_edges : 1U), sizeof(uint16_t));
    if (!edges) return 1;

    uint64_t rng = (args->hunt_seed ^ UINT64_C(0xC010A464FEEDFACE)) | UINT64_C(1);
    uint64_t planted[4] = { 0ULL, 0ULL, 0ULL, 0ULL };
    for (uint32_t u = 0U; u < n_nodes; ++u) {
        uint32_t col = searu_Bounded_u32(searu_rng_next(&rng), k_col);
        searu_color_set(planted, u, col);
    }
    uint64_t kan_pin = 0ULL;
    uint64_t init_canvas[4] = { 0ULL, 0ULL, 0ULL, 0ULL };
    uint32_t pin_cnt = n_nodes / 12U;
    if (pin_cnt > 4U) pin_cnt = 4U;
    for (uint32_t p = 0U; p < pin_cnt; ++p) {
        uint32_t u = p * 3U;
        if (u < n_nodes) {
            kan_pin |= UINT64_C(1) << u;
            searu_color_set(init_canvas, u, searu_color_get(planted, u));
        }
    }

    uint32_t n_edges = 0U;
    for (uint32_t u = 0U; u < n_nodes; ++u) {
        uint32_t cu = searu_color_get(planted, u);
        for (uint32_t v = u + 1U; v < n_nodes; ++v) {
            if (cu != searu_color_get(planted, v) && (searu_rng_next(&rng) % 10ULL) < 7ULL) {
                edges[n_edges++] = SEARU_EDGE_U16(u, v);
            }
        }
    }

    arena->map.frame_hdr.width_px = 64U;
    arena->map.frame_hdr.height_px = 64U;
    BloomGpuCtx *gpu = bloom_gpu_init(arena, args->shader_path);
    if (!gpu) {
        free(edges);
        return 1;
    }

    BloomSearuGpuConfig scfg;
    scfg.num_universes = args->searu_universes;
    scfg.iters_per_round = args->searu_iters;
    scfg.max_rounds = args->searu_rounds;
    scfg.seed = (args->hunt_seed != 0ULL) ? args->hunt_seed : UINT64_C(0xC010A40112345678);

    BloomSearuGpuResult gres;
    bool gpu_ok = bloom_gpu_searu_solve_graph_color(gpu, edges, n_edges, n_nodes, k_col,
                                                    init_canvas, kan_pin, &scfg, &gres);
    double gpu_msteps = (gres.elapsed_us > 0ULL)
                            ? ((double)gres.total_steps_eval / (double)gres.elapsed_us)
                            : 0.0;
    printf("[BLOOM GPU-SEARU COLOR] nodes=%u colors=%u edges=%u unis=%u rounds=%u steps=%" PRIu64
           " best_e=%" PRIu64 " solved=%s win_uni=%u time=%.2fms (%.2f M_steps/s) cpu_verified=%s GPU=%s\n",
           n_nodes, k_col, n_edges, scfg.num_universes, gres.rounds_run, gres.total_steps_eval,
           gres.best_energy, gres.solved ? "SOLVED" : "UNSOLVED", gres.winning_universe,
           (double)gres.elapsed_us / 1000.0, gpu_msteps,
           gres.cpu_verified ? "EXACT" : "FAIL", bloom_gpu_device_name(gpu));

    if (args->verify_cpu) {
        searu_result cres;
        memset(&cres, 0, sizeof(cres));
        uint64_t t0 = cli_monotonic_us();
        (void)searu_solve_graph_color(&cres, edges, n_edges, n_nodes, k_col,
                                      init_canvas, kan_pin, scfg.seed, UINT32_C(20000));
        uint64_t dt = cli_monotonic_us() - t0;
        if (dt == 0ULL) dt = 1ULL;
        printf("[BLOOM CPU-SEARU COLOR] nodes=%u colors=%u edges=%u iters=%u best_e=%" PRIu64
               " solved=%s time=%.2fms (%.2f k_steps/s)\n",
               n_nodes, k_col, n_edges, cres.iterations_run, cres.best_energy,
               (cres.best_energy == 0ULL) ? "SOLVED" : "UNSOLVED",
               (double)dt / 1000.0, (double)cres.iterations_run * 1000.0 / (double)dt);
    }

    bloom_gpu_destroy(gpu);
    free(edges);
    return (gpu_ok && gres.cpu_verified && gres.solved) ? 0 : 1;
}

static int cli_run_searu_cnf(const BloomCliArgs *args, BloomEngineArena *arena) {
    FILE *fp = fopen(args->searu_cnf_path, "rb");
    if (!fp) {
        fprintf(stderr, "[BLOOM GPU-SEARU CNF] Cannot open %s\n", args->searu_cnf_path);
        return 1;
    }
    uint32_t *clauses = (uint32_t *)calloc(8192U, sizeof(uint32_t));
    if (!clauses) {
        fclose(fp);
        return 1;
    }
    uint32_t n_vars = 0U, declared_c = 0U, n_clauses = 0U;
    uint32_t lits[3] = { 0U, 0U, 0U }, negs[3] = { 0U, 0U, 0U }, lc = 0U;
    char line[1024];
    while (fgets(line, (int)sizeof(line), fp)) {
        if (line[0] == 'c' || line[0] == '\n' || line[0] == '\r' || line[0] == '%') {
            continue;
        }
        if (line[0] == 'p') {
            (void)sscanf(line, "p cnf %u %u", &n_vars, &declared_c);
            continue;
        }
        const char *p = line;
        while (*p != '\0') {
            while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
            if (*p == '\0' || *p == 'c') break;
            char *endp = NULL;
            long val = strtol(p, &endp, 10);
            if (endp == p) break;
            p = endp;
            if (val == 0L) {
                if (lc > 0U && n_clauses < 8192U) {
                    while (lc < 3U) {
                        lits[lc] = lits[0];
                        negs[lc] = negs[0];
                        lc++;
                    }
                    clauses[n_clauses++] = SEARU_SAT3_CLAUSE(lits[0], negs[0], lits[1], negs[1], lits[2], negs[2]);
                }
                lc = 0U;
            } else {
                long av = (val < 0L) ? -val : val;
                if (av >= 1L && av <= 256L && lc < 3U) {
                    uint32_t vid = (uint32_t)(av - 1L);
                    if (vid + 1U > n_vars) n_vars = vid + 1U;
                    lits[lc] = vid;
                    negs[lc] = (val < 0L) ? 1U : 0U;
                    lc++;
                }
            }
        }
    }
    fclose(fp);
    (void)declared_c;

    if (n_vars == 0U || n_vars > 256U || n_clauses == 0U) {
        fprintf(stderr, "[BLOOM GPU-SEARU CNF] Invalid or empty 3-SAT CNF: %s (vars=%u clauses=%u)\n",
                args->searu_cnf_path, n_vars, n_clauses);
        free(clauses);
        return 1;
    }

    arena->map.frame_hdr.width_px = 64U;
    arena->map.frame_hdr.height_px = 64U;
    BloomGpuCtx *gpu = bloom_gpu_init(arena, args->shader_path);
    if (!gpu) {
        free(clauses);
        return 1;
    }
    BloomSearuGpuConfig scfg;
    scfg.num_universes = args->searu_universes;
    scfg.iters_per_round = args->searu_iters;
    scfg.max_rounds = args->searu_rounds;
    scfg.seed = (args->hunt_seed != 0ULL) ? args->hunt_seed : UINT64_C(0x5A70C4F012345678);

    uint64_t zero4[4] = { 0ULL, 0ULL, 0ULL, 0ULL };
    BloomSearuGpuResult gres;
    bool gpu_ok = bloom_gpu_searu_solve_3sat(gpu, clauses, n_clauses, n_vars,
                                             zero4, zero4, &scfg, &gres);
    double gpu_msteps = (gres.elapsed_us > 0ULL)
                            ? ((double)gres.total_steps_eval / (double)gres.elapsed_us)
                            : 0.0;
    printf("[BLOOM GPU-SEARU CNF] file=%s vars=%u clauses=%u unis=%u rounds=%u best_e=%" PRIu64
           " solved=%s time=%.2fms (%.2f M_steps/s) cpu_verified=%s GPU=%s\n",
           args->searu_cnf_path, n_vars, n_clauses, scfg.num_universes, gres.rounds_run,
           gres.best_energy, gres.solved ? "SOLVED" : "UNSOLVED",
           (double)gres.elapsed_us / 1000.0, gpu_msteps,
           gres.cpu_verified ? "EXACT" : "FAIL", bloom_gpu_device_name(gpu));
    bloom_gpu_destroy(gpu);
    free(clauses);
    return (gpu_ok && gres.cpu_verified) ? 0 : 1;
}

static int cli_run_faultsim_command(const BloomCliArgs *args, BloomEngineArena *arena) {
    size_t fir_len = 0U;
    uint8_t *fir_buf = cli_read_fir_file(args->faultsim_fir_path, &fir_len);
    if (!fir_buf) return 1;

    arena->map.frame_hdr.width_px = 64U;
    arena->map.frame_hdr.height_px = 64U;
    BloomGpuCtx *gpu = bloom_gpu_init(arena, args->shader_path);
    if (!gpu) {
        free(fir_buf);
        return 1;
    }

    BloomFir1FaultSimResult res;
    bool ok = bloom_gpu_faultsim_fir1(gpu, fir_buf, fir_len, args->hunt_trials, args->hunt_seed, &res);
    double cov = (res.total_faults > 0U)
                     ? (100.0 * (double)res.detected_faults / (double)res.total_faults)
                     : 100.0;
    double mfevals = (res.elapsed_us > 0ULL)
                         ? ((double)res.total_fault_evals / (double)res.elapsed_us)
                         : 0.0;
    printf("[BLOOM GPU-FAULTSIM] file=%s wires=%u gates=%u faults=%u detected=%u undetected=%u cov=%.2f%% trials/fault=%u total_evals=%" PRIu64 " time=%.2fms (%.2f M_fevals/s) cpu_verified=%s GPU=%s\n",
           args->faultsim_fir_path, res.total_wires, res.total_gates, res.total_faults,
           res.detected_faults, res.undetected_faults, cov, res.trials_per_fault,
           res.total_fault_evals, (double)res.elapsed_us / 1000.0, mfevals,
           res.cpu_verified ? "EXACT" : "FAIL", bloom_gpu_device_name(gpu));
    if (res.sample_atpg_vec != 0xFFFFFFFFU) {
        printf("  sample_atpg: wire=w%u SA%u detected_by=0x%08x\n",
               res.sample_fault_wire, res.sample_stuck_val, res.sample_atpg_vec);
    }

    bloom_gpu_destroy(gpu);
    free(fir_buf);
    return (ok && res.cpu_verified) ? 0 : 1;
}

static int cli_run_equiv_command(const BloomCliArgs *args, BloomEngineArena *arena) {
    size_t len_a = 0U, len_b = 0U;
    uint8_t *buf_a = cli_read_fir_file(args->equiv_fir_a, &len_a);
    uint8_t *buf_b = cli_read_fir_file(args->equiv_fir_b, &len_b);
    if (!buf_a || !buf_b) {
        free(buf_b);
        free(buf_a);
        return 1;
    }

    arena->map.frame_hdr.width_px = 64U;
    arena->map.frame_hdr.height_px = 64U;
    BloomGpuCtx *gpu = bloom_gpu_init(arena, args->shader_path);
    if (!gpu) {
        free(buf_b);
        free(buf_a);
        return 1;
    }

    BloomFir1EquivResult res;
    uint32_t trials = args->trials_explicit ? args->hunt_trials : 0U;
    bool ok = bloom_gpu_equiv_fir1(gpu, buf_a, len_a, buf_b, len_b, trials, args->hunt_seed, &res);
    double mevals = (res.elapsed_us > 0ULL)
                        ? ((double)res.trials_evaluated / (double)res.elapsed_us)
                        : 0.0;
    printf("[BLOOM GPU-EQUIV] spec=%s impl=%s in_bits=%u miter_gates=%u trials=%u exhaustive=%s mismatches=%u verdict=%s time=%.2fms (%.2f M_evals/s) cpu_verified=%s GPU=%s\n",
           args->equiv_fir_a, args->equiv_fir_b, res.total_in_bits, res.miter_gates,
           res.trials_evaluated, res.exhaustive ? "YES" : "BOUNDED", res.mismatch_count,
           res.equivalent ? "EQUIVALENT" : "NONEQUIVALENT",
           (double)res.elapsed_us / 1000.0, mevals,
           res.cpu_verified ? "EXACT" : "FAIL", bloom_gpu_device_name(gpu));
    if (!res.equivalent && res.cpu_verified) {
        printf("  counterexample[trial=%u]: in0=0x%" PRIx64 " spec_out0=0x%" PRIx64 " impl_out0=0x%" PRIx64 "\n",
               res.first_mismatch_trial, res.cex_inputs[0], res.cex_out_a[0], res.cex_out_b[0]);
    }

    bloom_gpu_destroy(gpu);
    free(buf_b);
    free(buf_a);
    return (ok && res.cpu_verified && res.equivalent) ? 0 : 1;
}

typedef struct {
    char     path[512];
    uint32_t sig[8];
    uint32_t size_bytes;
} CliNcdFile;

typedef struct {
    uint32_t i;
    uint32_t j;
    uint32_t matches;
} CliNcdPair;

static bool cli_ncd_has_ext(const char *name) {
    static const char *const exts[8] = { ".c", ".h", ".lean", ".wgsl", ".py", ".md", ".v", ".sv" };
    size_t nlen = strlen(name);
    for (size_t k = 0U; k < 8U; ++k) {
        size_t elen = strlen(exts[k]);
        if (nlen > elen && strcmp(name + (nlen - elen), exts[k]) == 0) {
            return true;
        }
    }
    return false;
}

static bool cli_ncd_skip_dir(const char *name) {
    return (strcmp(name, ".") == 0) || (strcmp(name, "..") == 0) ||
           (strcmp(name, ".git") == 0) || (strcmp(name, "build") == 0) ||
           (strcmp(name, "__pycache__") == 0) || (strcmp(name, "node_modules") == 0) ||
           (strcmp(name, ".lake") == 0) || (strcmp(name, ".elan") == 0);
}

static void cli_ncd_walk(const char *dir_path,
                         CliNcdFile *files,
                         uint32_t *io_count,
                         uint32_t max_files) {
    if (*io_count >= max_files) return;
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && *io_count < max_files) {
        if (cli_ncd_skip_dir(ent->d_name)) continue;
        char full[512];
        int n = snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
        if (n <= 0 || (size_t)n >= sizeof(full)) continue;

        struct stat st;
        if (lstat(full, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            cli_ncd_walk(full, files, io_count, max_files);
        } else if (S_ISREG(st.st_mode) && cli_ncd_has_ext(ent->d_name) &&
                   st.st_size >= 32 && st.st_size <= 4194304) {
            FILE *fp = fopen(full, "rb");
            if (!fp) continue;
            size_t sz = (size_t)st.st_size;
            uint8_t *buf = (uint8_t *)malloc(sz);
            if (buf && fread(buf, 1U, sz, fp) == sz) {
                CliNcdFile *dst = &files[*io_count];
                snprintf(dst->path, sizeof(dst->path), "%s", full);
                dst->size_bytes = (uint32_t)sz;
                bloom_info_sketch_bytes(buf, sz, dst->sig);
                (*io_count)++;
            }
            free(buf);
            fclose(fp);
        }
    }
    closedir(d);
}

static uint32_t cli_ncd_sig_eq(const uint32_t a[8], const uint32_t b[8]) {
    uint32_t tot = 0U;
    for (uint32_t w = 0U; w < 8U; ++w) {
        uint32_t x = a[w] ^ b[w];
        uint32_t nz = (x | (x >> 1U) | (x >> 2U) | (x >> 3U)) & UINT32_C(0x11111111);
        for (uint32_t k = 0U; k < 8U; ++k) {
            if (((nz >> (k * 4U)) & 1U) == 0U) {
                tot++;
            }
        }
    }
    return tot;
}

static int cli_run_gpu_ncd_command(const BloomCliArgs *args, BloomEngineArena *arena) {
    const uint32_t max_files = 8192U;
    CliNcdFile *files = (CliNcdFile *)calloc(max_files, sizeof(CliNcdFile));
    if (!files) return 1;

    uint32_t n_files = 0U;
    char *dirs_copy = (char *)malloc(strlen(args->gpu_ncd_dirs) + 1U);
    if (!dirs_copy) {
        free(files);
        return 1;
    }
    memcpy(dirs_copy, args->gpu_ncd_dirs, strlen(args->gpu_ncd_dirs) + 1U);

    char *tok = dirs_copy;
    while (tok && *tok != '\0' && n_files < max_files) {
        char *comma = strchr(tok, ',');
        if (comma) *comma = '\0';
        if (tok[0] != '\0') {
            cli_ncd_walk(tok, files, &n_files, max_files);
        }
        tok = comma ? (comma + 1) : NULL;
    }
    free(dirs_copy);

    if (n_files < 2U) {
        fprintf(stderr, "[BLOOM GPU-NCD] Need >= 2 files to compute similarity matrix (found %u)\n", n_files);
        free(files);
        return (n_files == 1U) ? 0 : 1;
    }

    uint32_t *sigs = (uint32_t *)calloc((size_t)n_files * 8U, sizeof(uint32_t));
    BloomInfoSimRow *rows = (BloomInfoSimRow *)calloc(n_files, sizeof(BloomInfoSimRow));
    if (!sigs || !rows) {
        free(rows);
        free(sigs);
        free(files);
        return 1;
    }
    for (uint32_t i = 0U; i < n_files; ++i) {
        memcpy(&sigs[i * 8U], files[i].sig, 8U * sizeof(uint32_t));
    }

    arena->map.frame_hdr.width_px = 64U;
    arena->map.frame_hdr.height_px = 64U;
    BloomGpuCtx *gpu = bloom_gpu_init(arena, args->shader_path);
    if (!gpu) {
        free(rows);
        free(sigs);
        free(files);
        return 1;
    }

    BloomInfoSimResult sum;
    bool ok = bloom_gpu_info_sim_matrix(gpu, sigs, n_files, args->ncd_thr, rows, &sum);

    bool cpu_full_ok = sum.cpu_verified;
    if (args->verify_cpu && n_files <= 512U && ok) {
        for (uint32_t i = 0U; i < n_files && cpu_full_ok; ++i) {
            uint32_t bj = i, bm = 0U, nc = 0U, ec = 0U;
            for (uint32_t j = 0U; j < n_files; ++j) {
                if (j == i) continue;
                uint32_t m = cli_ncd_sig_eq(files[i].sig, files[j].sig);
                if (m > bm || (bj == i && m == 0U)) { bm = m; bj = j; }
                if (m >= args->ncd_thr) nc++;
                if (m == 64U) ec++;
            }
            if (bj != rows[i].best_match_idx || bm != rows[i].best_matches ||
                nc != rows[i].near_dup_count || ec != rows[i].exact_dup_count) {
                cpu_full_ok = false;
            }
        }
    }

    double mpairs = (sum.elapsed_us > 0ULL)
                        ? ((double)sum.total_pairs_eval / (double)sum.elapsed_us)
                        : 0.0;
    printf("[BLOOM GPU-NCD] files=%u pairs=%" PRIu64 " thr=%u/64 near_dup_files=%u exact_dup_pairs=%u time=%.2fms (%.2f M_pairs/s) cpu_verified=%s GPU=%s\n",
           sum.num_files, sum.total_pairs_eval, args->ncd_thr,
           sum.files_with_near_dup, sum.exact_dup_pairs,
           (double)sum.elapsed_us / 1000.0, mpairs,
           cpu_full_ok ? "EXACT" : "FAIL", bloom_gpu_device_name(gpu));

    CliNcdPair top[64];
    memset(top, 0, sizeof(top));
    uint32_t top_n = 0U;
    uint32_t k_lim = (args->ncd_top <= 64U) ? args->ncd_top : 64U;
    for (uint32_t i = 0U; i < n_files; ++i) {
        uint32_t j = rows[i].best_match_idx;
        if (j >= n_files || j == i) continue;
        uint32_t lo = (i < j) ? i : j;
        uint32_t hi = (i < j) ? j : i;
        uint32_t m = rows[i].best_matches;
        bool dup = false;
        for (uint32_t t = 0U; t < top_n; ++t) {
            if (top[t].i == lo && top[t].j == hi) {
                dup = true;
                break;
            }
        }
        if (dup) continue;
        if (top_n < k_lim || m > top[top_n - 1U].matches) {
            uint32_t pos = (top_n < k_lim) ? top_n++ : (k_lim - 1U);
            while (pos > 0U && m > top[pos - 1U].matches) {
                top[pos] = top[pos - 1U];
                pos--;
            }
            top[pos].i = lo;
            top[pos].j = hi;
            top[pos].matches = m;
        }
    }
    for (uint32_t t = 0U; t < top_n; ++t) {
        double p_match = (double)top[t].matches / 64.0;
        double est_j = (p_match - (1.0 / 16.0)) / (15.0 / 16.0);
        if (est_j < 0.0) est_j = 0.0;
        if (est_j > 1.0) est_j = 1.0;
        printf("  top[%u]: %2u/64 (est_J=%.3f est_NCD=%.3f)  %s (%uB)  <->  %s (%uB)\n",
               t + 1U, top[t].matches, est_j, 1.0 - est_j,
               files[top[t].i].path, files[top[t].i].size_bytes,
               files[top[t].j].path, files[top[t].j].size_bytes);
    }

    bloom_gpu_destroy(gpu);
    free(rows);
    free(sigs);
    free(files);
    return (ok && cpu_full_ok) ? 0 : 1;
}

int main(int argc, char **argv) {
    BloomCliArgs args;
    if (!cli_parse_args(argc, argv, &args)) {
        return 1;
    }
    if (args.show_help) {
        cli_print_usage(argv[0]);
        return 0;
    }
    if (args.show_version) {
        printf("bloom %s (FLOW %s)\n", BLOOM_VERSION_STRING, FLOW_VERSION_STRING);
        return 0;
    }

    void *raw_arena = NULL;
    void *raw_staging = NULL;
    void *raw_mesh = NULL;
    if (posix_memalign(&raw_arena, BLOOM_VM_PAGE_ALIGN, sizeof(BloomEngineArena)) != 0 || !raw_arena) {
        fprintf(stderr, "[BLOOM CLI] Failed to allocate 16KB page-aligned BloomEngineArena\n");
        return 1;
    }
    if (posix_memalign(&raw_staging, BLOOM_VM_PAGE_ALIGN, sizeof(BloomEngineArena)) != 0 || !raw_staging) {
        free(raw_arena);
        fprintf(stderr, "[BLOOM CLI] Failed to allocate hot-reload staging arena\n");
        return 1;
    }
    if (posix_memalign(&raw_mesh, 64U, sizeof(BloomMeshArena)) != 0 || !raw_mesh) {
        free(raw_staging);
        free(raw_arena);
        fprintf(stderr, "[BLOOM CLI] Failed to allocate 32KB BloomMeshArena\n");
        return 1;
    }

    BloomEngineArena *arena = (BloomEngineArena *)raw_arena;
    BloomEngineArena *staging = (BloomEngineArena *)raw_staging;
    BloomMeshArena *mesh = (BloomMeshArena *)raw_mesh;
    memset(arena, 0, sizeof(*arena));
    memset(staging, 0, sizeof(*staging));
    memset(mesh, 0, sizeof(*mesh));

    if (args.compile_fir_in != NULL) {
        int c_rc = cli_run_compile_fir_wgsl(&args);
        free(mesh);
        free(staging);
        free(arena);
        return c_rc;
    }

    if (args.hunt_fir_path != NULL) {
        int hunt_rc = cli_run_gpu_hunt_command(&args, arena);
        free(mesh);
        free(staging);
        free(arena);
        return hunt_rc;
    }

    if (args.searu_bench_3sat) {
        int s_rc = cli_run_searu_bench_3sat(&args, arena);
        free(mesh);
        free(staging);
        free(arena);
        return s_rc;
    }

    if (args.searu_bench_color) {
        int c_rc = cli_run_searu_bench_color(&args, arena);
        free(mesh);
        free(staging);
        free(arena);
        return c_rc;
    }

    if (args.faultsim_fir_path != NULL) {
        int fs_rc = cli_run_faultsim_command(&args, arena);
        free(mesh);
        free(staging);
        free(arena);
        return fs_rc;
    }

    if (args.equiv_fir_a != NULL) {
        int eq_rc = cli_run_equiv_command(&args, arena);
        free(mesh);
        free(staging);
        free(arena);
        return eq_rc;
    }

    if (args.gpu_ncd_dirs != NULL) {
        int ncd_rc = cli_run_gpu_ncd_command(&args, arena);
        free(mesh);
        free(staging);
        free(arena);
        return ncd_rc;
    }

    if (args.searu_cnf_path != NULL) {
        int cnf_rc = cli_run_searu_cnf(&args, arena);
        free(mesh);
        free(staging);
        free(arena);
        return cnf_rc;
    }

    bool is_mesh_asset = false;
    if (!cli_load_input_asset(args.bloom_path, arena, mesh, &is_mesh_asset)) {
        fprintf(stderr, "[BLOOM CLI] Failed to load/verify .bloom, .stl, or .glb asset: %s\n",
                args.bloom_path);
        free(mesh);
        free(staging);
        free(arena);
        return 1;
    }

    if (args.set_section || args.set_slab || args.set_corner) {
        BloomSectionSpec64 *sec = &arena->map.section_spec;
        uint32_t smode = args.set_corner ? BLOOM_SEC_MODE_CORNER
                       : (args.set_slab ? BLOOM_SEC_MODE_SLAB : BLOOM_SEC_MODE_PLANE);
        uint32_t iflags = sec->inspect_flags |
                          BLOOM_INSPECT_ISOLINES | BLOOM_INSPECT_WALL_DFM |
                          BLOOM_INSPECT_INTERFERENCE | BLOOM_INSPECT_TRIPLANAR;
        bloom_section_init64(sec, smode, args.sec_norm_q16, args.sec_dist_q16,
                             args.slab_half_q16, sec->min_wall_q16 > 0 ? sec->min_wall_q16 : 11796,
                             sec->iso_pitch_q16 > 0 ? sec->iso_pitch_q16 : 9830, iflags);
        arena->map.frame_hdr.xray_plane_q16 = args.sec_dist_q16;
    }

    if (args.enable_rc) {
        arena->map.frame_hdr.flags |= BLOOM_FLAG_RADIANCE_CASCADES;
        arena->map.frame_hdr.flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    if (args.enable_field) {
        arena->map.frame_hdr.flags |= BLOOM_FLAG_DIFF_FIELD;
        arena->map.frame_hdr.flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    if (args.enable_cine) {
        arena->map.frame_hdr.flags |= BLOOM_FLAG_CINE_OPTICS;
        arena->map.camera.aperture_fstop_q16 = 117964; /* f/1.8 */
        arena->map.camera.focal_mm_q16 = 45 * BLOOM_Q16_ONE;
        arena->map.frame_hdr.flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    if (args.has_metric) {
        BloomMetricScale64 mscale;
        bloom_metric_eval_scale64(args.metric_beta_q16, args.metric_phi_q16, 0, NULL, NULL, &mscale);
        bloom_reg_bank_set_q16(&arena->map.reg_bank, BLOOM_REG_METRIC_PHI, mscale.metric_potential_q16);
        bloom_reg_bank_set_q16(&arena->map.reg_bank, BLOOM_REG_LORENTZ_BETA, mscale.lorentz_beta_q16);
        bloom_reg_bank_set_q16(&arena->map.reg_bank, BLOOM_REG_FLUX_PRESSURE, mscale.flux_pressure_q16);
        arena->map.frame_hdr.flags |= BLOOM_FLAG_METRIC_FIELD;
        arena->map.frame_hdr.flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    BloomGameplayState64 cli_gp;
    bloom_gameplay_init64(&cli_gp, 1U, 16384, 32768, 65536);
    if (args.enable_shells) {
        arena->map.frame_hdr.flags |= BLOOM_FLAG_GAMEPLAY_SHELLS;
        bloom_reg_bank_set_q16(&arena->map.reg_bank, BLOOM_REG_GRAZE_SHELL, cli_gp.delta_graze_q16);
        arena->map.frame_hdr.flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    if (args.import_mood_path) {
        if (!cli_import_mood_ppm(args.import_mood_path, arena)) {
            fprintf(stderr, "[BLOOM CLI] Failed to import moodboard PPM: %s\n", args.import_mood_path);
            free(mesh);
            free(staging);
            free(arena);
            return 1;
        }
    }
    if (args.has_impact) {
        arena->map.frame_hdr.flags |= BLOOM_FLAG_SUBFRAME_LOCK;
        arena->map.frame_hdr.flags &= ~BLOOM_FLAG_IDLE_SLEEP;
        const int32_t pa[3] = { -144179 /* -2.20 */, 16384 /* +0.25 */, -104857 /* -1.60 */ };
        const int32_t pb[3] = {   13107 /* +0.20 */, 16384 /* +0.25 */, -104857 /* -1.60 */ };
        uint32_t ia = bloom_prefab_spawn_inst(arena, BLOOM_PREFAB_CRYSTAL_SPHERE, pa, BLOOM_Q16_ONE);
        uint32_t ib = bloom_prefab_spawn_inst(arena, BLOOM_PREFAB_CRYSTAL_SPHERE, pb, BLOOM_Q16_ONE);
        if (ia > 0U && ib > 0U) {
            int32_t v_in[3] = { args.impact_vx_q16, 0, 0 };
            arena->map.instances[ia - 1U].flags |= BLOOM_INST_FLAG_COLLIDABLE;
            arena->map.instances[ib - 1U].flags |= BLOOM_INST_FLAG_COLLIDABLE;
            bloom_prefab_override_inst(&arena->map.instances[ib - 1U], 4U, BLOOM_MATTER_TITANIUM, 0);
            bloom_inst_set_vel_q16(&arena->map.instances[ia - 1U], v_in);
        }
    }

    if (args.override_w > 0U) {
        arena->map.frame_hdr.width_px = args.override_w;
        arena->map.window.logical_w_px = args.override_w;
        arena->map.window.fb_w_px = args.override_w;
    }
    if (args.override_h > 0U) {
        arena->map.frame_hdr.height_px = args.override_h;
        arena->map.window.logical_h_px = args.override_h;
        arena->map.window.fb_h_px = args.override_h;
    }

    if (args.emit_sha256) {
        char arena_sha[65];
        char file_sha[65];
        bloom_arena_sha256_hex(arena, arena_sha);
        if (flow_sha256_file_hex(args.bloom_path, file_sha) != 0) {
            file_sha[0] = '\0';
        }
        printf("[BLOOM SHA256] arena=%s file=%s\n", arena_sha, file_sha);
    }

    if (args.inspect_cae && !args.has_impact) {
        bloom_physics_step_arena64(arena, BLOOM_Q16_ONE);
        int64_t hess_mu_q32 = 0LL;
        bool hess_psd = bloom_scene_certify_hessian_psd_q16(
            arena, is_mesh_asset ? mesh : NULL, 0LL, 0LL, 0LL, 8LL * BLOOM_Q16_ONE, &hess_mu_q32);
        char arena_sha[65];
        bloom_arena_sha256_hex(arena, arena_sha);
        const BloomSectionSpec64 *sec = &arena->map.section_spec;
        const BloomPhysicsSummary64 *ps = &arena->map.phys_summary;
        printf("[BLOOM v%s CAE] inst=%u mesh_tris=%u mesh_bvh=%u sec_mode=%u inter_pairs=%u warn_pairs=%u ccd_hits=%u min_clr_q16=%d max_pen_q16=%d toi_q16=%d peak_K=%u pair=(%u,%u) hess_psd=%d hess_mu_q32=%" PRId64 " sha256=%.16s\n",
               BLOOM_VERSION_STRING,
               arena->map.frame_hdr.inst_count,
               (unsigned)sec->mesh_tri_count,
               (unsigned)sec->mesh_bvh_count,
               sec->section_mode,
               (unsigned)ps->interference_pairs,
               (unsigned)ps->clearance_warn_pairs,
               (unsigned)ps->ccd_hit_count,
               ps->min_clearance_q16,
               ps->max_penetration_q16,
               ps->earliest_toi_q16,
               ps->peak_kelvin,
               (unsigned)ps->pair_inst_a,
               (unsigned)ps->pair_inst_b,
               hess_psd ? 1 : 0,
               hess_mu_q32,
               arena_sha);
    }

    BloomGpuCtx *gpu = bloom_gpu_init(arena, args.shader_path);
    if (!gpu) {
        fprintf(stderr, "[BLOOM CLI] Failed to initialize SDL3 GPU context (shader=%s)\n",
                args.shader_path);
        free(mesh);
        free(staging);
        free(arena);
        return 1;
    }
    if (is_mesh_asset && !bloom_gpu_upload_mesh(gpu, mesh)) {
        fprintf(stderr, "[BLOOM CLI] Failed to upload CAD mesh buffer to GPU\n");
        bloom_gpu_destroy(gpu);
        free(mesh);
        free(staging);
        free(arena);
        return 1;
    }

    int exit_code = 0;
    if (args.headless || args.inspect_cae) {
        if (args.headless) {
            uint32_t wav_steps = (args.steps > 0U) ? args.steps : 1U;
            if (args.rewind_steps > 0U && wav_steps <= args.rewind_steps) {
                wav_steps = args.rewind_steps + 2U;
            }
            uint32_t wav_frames = wav_steps * 800U;
            int16_t *wav_buf = NULL;
            BloomAudioRig audio_rig;
            bloom_audio_init_rig(&audio_rig, BLOOM_AUDIO_SAMPLE_RATE);
            BloomRewindCtrl64 rw_ctrl;
            BloomSpacetimeSnap *rw_ring = NULL;
            bloom_rewind_init64(&rw_ctrl, BLOOM_REWIND_RING_CAP);
            if (args.rewind_steps > 0U) {
                rw_ring = (BloomSpacetimeSnap *)calloc(BLOOM_REWIND_RING_CAP, sizeof(BloomSpacetimeSnap));
            }
            if (args.out_wav_path) {
                wav_buf = (int16_t *)calloc((size_t)wav_frames * 2U, sizeof(int16_t));
            }
            uint32_t first_hit_s = 0xFFFFFFFFU;
            int32_t max_pcm = 0;
            for (uint32_t s = 0U; s < wav_steps; ++s) {
                if (rw_ring) {
                    bloom_rewind_push(&rw_ctrl, rw_ring, arena, &audio_rig);
                }
                (void)bloom_arena_step_dynamics(arena, 0x0001U);
                if (args.enable_shells) {
                    (void)bloom_gameplay_step64(&cli_gp, arena, &audio_rig, NULL, false);
                } else if (args.has_impact) {
                    bloom_physics_step_arena64(arena, 6554);
                }
                if (args.enable_cine) {
                    bloom_camera_step_cine64(&arena->map.camera, &arena->map.reg_bank, NULL, 8192, 0);
                }
                int16_t scratch_pcm[1600];
                int16_t *dst_pcm = wav_buf ? (wav_buf + (size_t)s * 1600U) : scratch_pcm;
                uint32_t hs = bloom_audio_step_frame64(&audio_rig, arena, dst_pcm, 800U);
                if (first_hit_s == 0xFFFFFFFFU && hs < 800U) {
                    first_hit_s = s * 800U + hs;
                }
                if (audio_rig.hdr.peak_abs_pcm > max_pcm) {
                    max_pcm = audio_rig.hdr.peak_abs_pcm;
                }
            }
            if (rw_ring && args.rewind_steps > 0U) {
                if (!bloom_rewind_restore(&rw_ctrl, rw_ring, args.rewind_steps, arena, &audio_rig)) {
                    fprintf(stderr, "[BLOOM CLI] Rewind %u frames failed\n", args.rewind_steps);
                    exit_code = 1;
                } else {
                    fprintf(stderr, "[BLOOM REWIND] Restored -%u frames (branch_epoch=%u, ring_count=%u)\n",
                            args.rewind_steps, rw_ctrl.branch_epoch, rw_ctrl.count);
                }
            }
            free(rw_ring);

            if (args.out_wav_path) {
                if (!wav_buf || !bloom_write_wav_i16(args.out_wav_path, wav_buf, wav_frames, BLOOM_AUDIO_SAMPLE_RATE)) {
                    fprintf(stderr, "[BLOOM CLI] Failed to write WAV audio to %s\n", args.out_wav_path);
                    exit_code = 1;
                } else {
                    fprintf(stderr, "[BLOOM AUDIO] Wrote %s (frames=%u, rate=%uHz, hit_sample=%u, peak_pcm=%d)\n",
                            args.out_wav_path, wav_frames, BLOOM_AUDIO_SAMPLE_RATE,
                            (first_hit_s == 0xFFFFFFFFU) ? 0U : first_hit_s, max_pcm);
                }
            }
            free(wav_buf);

            if (args.out_edl_path) {
                uint32_t reel_f = (wav_steps >= 24U) ? wav_steps : 96U;
                if (!bloom_write_cmx3600_edl(args.out_edl_path, "BLOOM_V9_STUDIO", reel_f, 24U)) {
                    fprintf(stderr, "[BLOOM CLI] Failed to write CMX 3600 EDL to %s\n", args.out_edl_path);
                    exit_code = 1;
                } else {
                    fprintf(stderr, "[BLOOM EDL] Wrote %s (frames=%u @ 24fps)\n", args.out_edl_path, reel_f);
                }
            }
            if (args.out_fir_path) {
                flow_ir_builder_ws_t *fir_ws =
                    (flow_ir_builder_ws_t *)calloc(1U, sizeof(flow_ir_builder_ws_t));
                uint32_t mbits = 0U, mdepth = 0U;
                size_t wlen = 0U;
                bool fir_ok = (fir_ws != NULL) &&
                              bloom_fir_certify_arena_contracts(fir_ws, &mbits, &mdepth, &wlen) &&
                              flow_ir_write_bytes_file(args.out_fir_path, fir_ws->wire_buf, wlen);
                free(fir_ws);
                if (!fir_ok) {
                    fprintf(stderr, "[BLOOM CLI] Failed to certify/write FIR1 witness to %s\n", args.out_fir_path);
                    exit_code = 1;
                } else {
                    fprintf(stderr, "[BLOOM FIR] Certified & wrote %s (%zuB, mult_bits=%u, mult_depth=%u)\n",
                            args.out_fir_path, wlen, mbits, mdepth);
                }
            }
            if (args.export_html_path) {
                if (!bloom_export_webgpu_html(args.export_html_path, arena, args.shader_path)) {
                    fprintf(stderr, "[BLOOM CLI] Failed to export WebGPU HTML to %s\n", args.export_html_path);
                    exit_code = 1;
                } else {
                    fprintf(stderr, "[BLOOM HTML] Exported standalone WebGPU bundle -> %s\n", args.export_html_path);
                }
            }

            if (!bloom_gpu_render(gpu, true)) {
                fprintf(stderr, "[BLOOM CLI] Headless GPU compute dispatch failed\n");
                exit_code = 1;
            } else {
                if (args.emit_kitty) {
                    if (!bloom_gpu_write_kitty_tty(gpu, stdout, 160U)) {
                        fprintf(stderr, "[BLOOM CLI] Failed to emit Kitty Graphics stream\n");
                        exit_code = 1;
                    }
                }
                if (args.out_ppm_path || (!args.out_wav_path && !args.export_html_path && !args.out_edl_path && !args.out_fir_path && !args.emit_sha256 && !args.emit_kitty)) {
                    uint64_t fnv = 0ULL;
                    uint32_t nonblack = 0U;
                    if (!bloom_gpu_write_ppm(gpu, args.out_ppm_path, &fnv, &nonblack)) {
                        fprintf(stderr, "[BLOOM CLI] Failed to export GPU framebuffer to %s\n",
                                args.out_ppm_path ? args.out_ppm_path : "<memory>");
                        exit_code = 1;
                    } else {
                        printf("[BLOOM] Rendered %ux%u -> %s (FNV1a=0x%016" PRIx64 ", nonblack=%u, GPU=%s)\n",
                               arena->map.frame_hdr.width_px,
                               arena->map.frame_hdr.height_px,
                               args.out_ppm_path ? args.out_ppm_path : "<memory>",
                               fnv,
                               nonblack,
                               bloom_gpu_device_name(gpu));
                    }
                }
            }
        }
    } else {
        BloomStudioRuntime rt;
        memset(&rt, 0, sizeof(rt));
        rt.arena = arena;
        rt.staging = staging;
        rt.mesh = mesh;
        rt.gpu = gpu;
        bloom_audio_init_rig(&rt.audio, BLOOM_AUDIO_SAMPLE_RATE);
        bloom_gameplay_init64(&rt.gameplay, 1U, 16384, 32768, 65536);
        bloom_rewind_init64(&rt.rewind_ctrl, BLOOM_REWIND_RING_CAP);
        rt.rewind_ring = (BloomSpacetimeSnap *)calloc(BLOOM_REWIND_RING_CAP, sizeof(BloomSpacetimeSnap));
        rt.bloom_path = args.bloom_path;
        rt.watch_svg_path = args.watch_svg_path;
        rt.matter_cursor = 1U;
        rt.mode_word = 0x0001U;
        rt.mesh_mode = is_mesh_asset;
        (void)cli_stat_mtime(args.bloom_path, &rt.last_bloom_mtime);
        if (args.watch_svg_path) {
            (void)cli_stat_mtime(args.watch_svg_path, &rt.last_svg_mtime);
        }
        int wrc = bloom_gpu_run_window(gpu, arena, cli_studio_step_cb, &rt);
        free(rt.rewind_ring);
        if (wrc != 0) {
            exit_code = 1;
        }
    }

    bloom_gpu_destroy(gpu);
    free(mesh);
    free(staging);
    free(arena);
    return exit_code;
}
