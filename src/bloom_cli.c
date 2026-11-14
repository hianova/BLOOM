#define _POSIX_C_SOURCE 200809L

#include "bloom.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/*
 * ============================================================================
 * BLOOM v7.0.1 — Standalone Zero-Lean-Runtime Pure C17 Native Studio & CLI
 * ============================================================================
 * Consumes sealed 16,384-byte (256 x 64B) .bloom Arena images compiled AOT by
 * Bloom.lean, driving either:
 *   1. Interactive 60Hz HiDPI SDL3 GPU Native Studio (3D raycast picking,
 *      orbit rig, 2-Jet tactile Flexbox UI, live UTF-8/IME TextInput command
 *      bar, and 15-frame stat() mtime zero-downtime hot-reload), or
 *   2. Headless offscreen GPU compute settlement & deterministic P6 PPM export.
 *
 * Zero <lean/lean.h>, zero -lleanshared, zero Lean runtime dependency.
 * ============================================================================
 */

typedef struct {
    const char *bloom_path;
    const char *shader_path;
    const char *watch_svg_path;
    const char *out_ppm_path;
    uint32_t    override_w;
    uint32_t    override_h;
    uint32_t    steps;
    bool        headless;
    bool        show_help;
    bool        show_version;
} BloomCliArgs;

typedef struct {
    BloomEngineArena *arena;
    BloomEngineArena *staging;
    BloomGpuCtx      *gpu;
    const char       *bloom_path;
    const char       *watch_svg_path;
    time_t            last_bloom_mtime;
    time_t            last_svg_mtime;
    uint32_t          frame_tick;
    uint32_t          matter_cursor;
    uint16_t          mode_word;
    bool              alert_toggled;
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

static void cli_print_usage(const char *prog) {
    printf("BLOOM v%s — Pure C17 64-Byte Spatial & Flexbox Native Runner\n", BLOOM_VERSION_STRING);
    printf("Usage:\n");
    printf("  %s [file.bloom] [--watch-svg <spec.svg>] [--shader <core.wgsl>] [--width W --height H]\n", prog);
    printf("  %s --headless [file.bloom] --out <frame.ppm> [--width W --height H] [--steps N]\n", prog);
    printf("\nOptions:\n");
    printf("  --headless            Run offscreen GPU compute & exit after rendering\n");
    printf("  --out <frame.ppm>     Export rendered RGBA8 framebuffer to binary P6 PPM\n");
    printf("  --steps <N>           FLOW v7.0.1 2-Jet settlement steps in headless mode (default: 60)\n");
    printf("  --width <W>           Override framebuffer width in pixels\n");
    printf("  --height <H>          Override framebuffer height in pixels\n");
    printf("  --watch-svg <file>    Optional W3C .svg file to hot-reload alongside .bloom\n");
    printf("  --shader <file.wgsl>  WGSL compute shader base path (default: shaders/bloom_core.wgsl)\n");
    printf("  --version, -v         Print BLOOM version\n");
    printf("  --help, -h            Show this help message\n");
}

static bool cli_parse_args(int argc, char **argv, BloomCliArgs *out) {
    memset(out, 0, sizeof(*out));
    out->bloom_path = "build/studio.bloom";
    out->shader_path = "shaders/bloom_core.wgsl";
    out->steps = 60U;

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
    return bloom_image_verify_arena(out_arena);
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
    if (rt->bloom_path && cli_stat_mtime(rt->bloom_path, &cur_mtime) &&
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
                if (bloom_scene_raycast64(arena, win->pointer_x_q16, win->pointer_y_q16, &hit) &&
                    hit.hit_valid != 0U) {
                    hdr->selected_inst_id = hit.hit_inst_id;
                    bloom_reg_bank_set_q16(&arena->map.reg_bank, 2U, (int32_t)(hit.hit_inst_id * 100U));
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
    (void)bloom_gpu_render(rt->gpu, false);
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
        printf("bloom %s\n", BLOOM_VERSION_STRING);
        return 0;
    }

    void *raw_arena = NULL;
    void *raw_staging = NULL;
    if (posix_memalign(&raw_arena, BLOOM_VM_PAGE_ALIGN, sizeof(BloomEngineArena)) != 0 || !raw_arena) {
        fprintf(stderr, "[BLOOM CLI] Failed to allocate 16KB page-aligned BloomEngineArena\n");
        return 1;
    }
    if (posix_memalign(&raw_staging, BLOOM_VM_PAGE_ALIGN, sizeof(BloomEngineArena)) != 0 || !raw_staging) {
        free(raw_arena);
        fprintf(stderr, "[BLOOM CLI] Failed to allocate hot-reload staging arena\n");
        return 1;
    }

    BloomEngineArena *arena = (BloomEngineArena *)raw_arena;
    BloomEngineArena *staging = (BloomEngineArena *)raw_staging;
    memset(arena, 0, sizeof(*arena));
    memset(staging, 0, sizeof(*staging));

    if (!cli_load_bloom_file(args.bloom_path, arena)) {
        fprintf(stderr, "[BLOOM CLI] Failed to load or verify 16,384-byte Merkle container: %s\n",
                args.bloom_path);
        free(staging);
        free(arena);
        return 1;
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

    BloomGpuCtx *gpu = bloom_gpu_init(arena, args.shader_path);
    if (!gpu) {
        fprintf(stderr, "[BLOOM CLI] Failed to initialize SDL3 GPU context (shader=%s)\n",
                args.shader_path);
        free(staging);
        free(arena);
        return 1;
    }

    int exit_code = 0;
    if (args.headless) {
        for (uint32_t s = 0U; s < args.steps; ++s) {
            (void)bloom_arena_step_dynamics(arena, 0x0001U);
        }
        if (!bloom_gpu_render(gpu, true)) {
            fprintf(stderr, "[BLOOM CLI] Headless GPU compute dispatch failed\n");
            exit_code = 1;
        } else {
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
    } else {
        BloomStudioRuntime rt;
        memset(&rt, 0, sizeof(rt));
        rt.arena = arena;
        rt.staging = staging;
        rt.gpu = gpu;
        rt.bloom_path = args.bloom_path;
        rt.watch_svg_path = args.watch_svg_path;
        rt.matter_cursor = 1U;
        rt.mode_word = 0x0001U;
        (void)cli_stat_mtime(args.bloom_path, &rt.last_bloom_mtime);
        if (args.watch_svg_path) {
            (void)cli_stat_mtime(args.watch_svg_path, &rt.last_svg_mtime);
        }
        int wrc = bloom_gpu_run_window(gpu, arena, cli_studio_step_cb, &rt);
        if (wrc != 0) {
            exit_code = 1;
        }
    }

    bloom_gpu_destroy(gpu);
    free(staging);
    free(arena);
    return exit_code;
}
