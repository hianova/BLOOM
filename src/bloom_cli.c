#define _POSIX_C_SOURCE 200809L

#include "bloom.h"
#include "flow_ir_io.h"
#include "flow_sha256_io.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
    uint32_t    hunt_trials;
    uint32_t    hunt_mode;
    uint32_t    hunt_vary_mask;
    uint64_t    hunt_seed;
    uint64_t    hunt_target;
    bool        hunt_prng;
    bool        verify_cpu;
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
                out->hunt_trials == 0U || out->hunt_trials > 268435456U) {
                fprintf(stderr, "[BLOOM CLI] Invalid --trials (expected 1..268435456)\n");
                return false;
            }
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

static int cli_run_gpu_hunt_command(const BloomCliArgs *args, BloomEngineArena *arena) {
    FILE *fp = fopen(args->hunt_fir_path, "rb");
    if (!fp) {
        fprintf(stderr, "[BLOOM GPU-HUNT] Cannot open .fir file: %s\n", args->hunt_fir_path);
        return 1;
    }
    uint8_t *fir_buf = (uint8_t *)malloc(FLOW_IR_MAX_FILE_BYTES + 1U);
    if (!fir_buf) {
        fclose(fp);
        return 1;
    }
    size_t fir_len = fread(fir_buf, 1U, FLOW_IR_MAX_FILE_BYTES + 1U, fp);
    fclose(fp);
    if (fir_len < FLOW_IR_HEADER_BYTES + 8U || fir_len > FLOW_IR_MAX_FILE_BYTES) {
        fprintf(stderr, "[BLOOM GPU-HUNT] Invalid .fir file size (%zuB): %s\n", fir_len, args->hunt_fir_path);
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
    cfg.sweep_mode = args->hunt_prng ? 1U : 0U;
    cfg.vary_input_mask = args->hunt_vary_mask;
    cfg.seed_lo = (uint32_t)(args->hunt_seed & UINT64_C(0xFFFFFFFF));
    cfg.seed_hi = (uint32_t)(args->hunt_seed >> 32U);
    cfg.target_lo = (uint32_t)(args->hunt_target & UINT64_C(0xFFFFFFFF));
    cfg.target_hi = (uint32_t)(args->hunt_target >> 32U);

    BloomFir1HuntResult res;
    if (!bloom_gpu_hunt_fir1(gpu, fir_buf, fir_len, &cfg, &res)) {
        fprintf(stderr, "[BLOOM GPU-HUNT] Failed on %s (circuit may exceed 128 vals / 512 ops / 64b width or witness mismatch)\n",
                args->hunt_fir_path);
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
            for (uint32_t i = 0U; i < prog->num_inputs; ++i) {
                uint64_t wm = flow_ir_mask_u64(prog->vals[i].width);
                uint64_t iv = cfg.base_inputs[i];
                uint32_t sh = (i < 31U) ? i : 31U;
                if (((cfg.vary_input_mask >> sh) & 1U) != 0U) {
                    if (cfg.sweep_mode == 1U) {
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
    printf("[BLOOM GPU-HUNT] file=%s trials=%u hits=%u first_hit=%s xor=0x%016" PRIx64
           " time=%.2fms (%.2f M_evals/s) cpu_parity=%s GPU=%s\n",
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

    if (args.hunt_fir_path != NULL) {
        int hunt_rc = cli_run_gpu_hunt_command(&args, arena);
        free(mesh);
        free(staging);
        free(arena);
        return hunt_rc;
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
