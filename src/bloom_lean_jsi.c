#define _POSIX_C_SOURCE 200809L

#include <lean/lean.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bloom.h"

/*
 * ============================================================================
 * BLOOM v6.0.0 — Zero-Copy Lean 4 Dynamic Plugin JSI Bridge (0 `lean -c`)
 * ============================================================================
 * Exports both direct C symbols (`bloom_jsi_*`) AND Lean 4 VM interpreter
 * boxed symbols (`l_Bloom_*___boxed`), enabling pure `lean --load-dynlib=...`
 * execution on top of the 16KB VM-page-aligned Arena + SDL3 GPU Compute Host
 * without any `lean -c` C codegen!
 * ============================================================================
 */

typedef struct {
    BloomEngineArena *arena;
    BloomGpuCtx      *gpu;
} BloomJsiHandle;

static lean_external_class *g_bloom_jsi_class = NULL;

static void bloom_jsi_finalize(void *ptr) {
    BloomJsiHandle *h = (BloomJsiHandle *)ptr;
    if (!h) return;
    if (h->gpu) {
        bloom_gpu_destroy(h->gpu);
        h->gpu = NULL;
    }
    if (h->arena) {
        free(h->arena);
        h->arena = NULL;
    }
    free(h);
}

static void bloom_jsi_foreach(void *ptr, b_lean_obj_arg fn) {
    (void)ptr;
    (void)fn;
}

static BloomJsiHandle *bloom_jsi_unwrap(b_lean_obj_arg obj) {
    return (BloomJsiHandle *)lean_get_external_data(obj);
}

LEAN_EXPORT lean_obj_res bloom_jsi_create(uint32_t w, uint32_t h, b_lean_obj_arg shader_path) {
    if (!g_bloom_jsi_class) {
        g_bloom_jsi_class = lean_register_external_class(bloom_jsi_finalize, bloom_jsi_foreach);
    }

    void *raw_page = NULL;
    if (posix_memalign(&raw_page, BLOOM_VM_PAGE_ALIGN, sizeof(BloomEngineArena)) != 0 || !raw_page) {
        return lean_io_result_mk_error(
            lean_mk_io_user_error(lean_mk_string("bloom_jsi_create: posix_memalign(16384) failed")));
    }

    BloomEngineArena *arena = (BloomEngineArena *)raw_page;
    bloom_arena_init_default(arena, w, h);

    const char *spath = lean_string_cstr(shader_path);
    BloomGpuCtx *gpu = bloom_gpu_init(arena, spath);
    if (!gpu) {
        free(arena);
        return lean_io_result_mk_error(
            lean_mk_io_user_error(lean_mk_string("bloom_jsi_create: SDL3 GPU initialization failed")));
    }

    BloomJsiHandle *handle = (BloomJsiHandle *)calloc(1U, sizeof(BloomJsiHandle));
    if (!handle) {
        bloom_gpu_destroy(gpu);
        free(arena);
        return lean_io_result_mk_error(
            lean_mk_io_user_error(lean_mk_string("bloom_jsi_create: handle alloc failed")));
    }
    handle->arena = arena;
    handle->gpu = gpu;

    lean_object *ext = lean_alloc_external(g_bloom_jsi_class, handle);
    return lean_io_result_mk_ok(ext);
}

LEAN_EXPORT lean_obj_res bloom_jsi_device_info(b_lean_obj_arg eng) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    char buf[256];
    snprintf(buf, sizeof(buf), "%s [HardwareGPU=%s, UnifiedMemory=%s, Arena=%uB]",
             bloom_gpu_device_name(h->gpu),
             bloom_gpu_is_hardware_gpu(h->gpu) ? "true" : "false",
             bloom_gpu_is_unified_memory(h->gpu) ? "true" : "false",
             (unsigned)sizeof(BloomEngineArena));
    return lean_io_result_mk_ok(lean_mk_string(buf));
}

LEAN_EXPORT lean_obj_res bloom_jsi_is_hardware_gpu(b_lean_obj_arg eng) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    return lean_io_result_mk_ok(lean_box(bloom_gpu_is_hardware_gpu(h->gpu) ? 1 : 0));
}

LEAN_EXPORT lean_obj_res bloom_jsi_is_unified_memory(b_lean_obj_arg eng) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    return lean_io_result_mk_ok(lean_box(bloom_gpu_is_unified_memory(h->gpu) ? 1 : 0));
}

LEAN_EXPORT lean_obj_res bloom_jsi_load_svg(b_lean_obj_arg eng, b_lean_obj_arg svg_path) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    BloomEngineArena *arena = h->arena;
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    BloomUiNode64 *nodes = arena->map.ui_nodes;
    BloomGlyph64 *glyphs = arena->map.glyphs;
    BloomDomState64 *dom = &arena->map.dom;
    BloomRegBank64 *bank = &arena->map.reg_bank;

    size_t glyph_cnt = 0U;
    size_t ui_cnt = bloom_svg_compile_file(lean_string_cstr(svg_path),
                                           nodes, BLOOM_MAX_UI_NODES,
                                           glyphs, BLOOM_MAX_GLYPHS,
                                           &glyph_cnt, dom);
    if (ui_cnt > BLOOM_MAX_UI_NODES) ui_cnt = BLOOM_MAX_UI_NODES;
    hdr->ui_count = (uint32_t)ui_cnt;
    hdr->glyph_count = (uint32_t)glyph_cnt;

    for (size_t i = 0U; i < ui_cnt; ++i) {
        if (nodes[i].bind_channel < BLOOM_REG_CHANNELS) {
            bloom_reg_bank_set_q16(bank, nodes[i].bind_channel, nodes[i].bound_value_q16);
        }
    }
    hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    return lean_io_result_mk_ok(lean_box_uint32((uint32_t)ui_cnt));
}

LEAN_EXPORT lean_obj_res bloom_jsi_compile_latex(b_lean_obj_arg eng,
                                                 b_lean_obj_arg latex_str,
                                                 uint32_t x_q16,
                                                 uint32_t y_q16,
                                                 uint32_t em_q16,
                                                 uint32_t rgb24) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    BloomEngineArena *arena = h->arena;
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    int32_t total_w = 0;
    size_t n = bloom_latex_compile_boxes64(lean_string_cstr(latex_str),
                                           (int32_t)x_q16,
                                           (int32_t)y_q16,
                                           (int32_t)em_q16,
                                           rgb24,
                                           arena->map.latex_boxes,
                                           BLOOM_MAX_LATEX_BOXES,
                                           &total_w);
    hdr->latex_count = (uint32_t)n;
    hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    return lean_io_result_mk_ok(lean_box_uint32((uint32_t)n));
}

LEAN_EXPORT lean_obj_res bloom_jsi_spawn_prefab(b_lean_obj_arg eng,
                                                uint32_t prefab_id,
                                                uint32_t x_q16,
                                                uint32_t y_q16,
                                                uint32_t z_q16,
                                                uint32_t scale_q16) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    BloomEngineArena *arena = h->arena;
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;

    int32_t pos[3] = { (int32_t)x_q16, (int32_t)y_q16, (int32_t)z_q16 };
    uint32_t inst_id = bloom_prefab_spawn_inst(arena, prefab_id, pos, (int32_t)scale_q16);
    if (inst_id > 0U && hdr->csg_count < BLOOM_MAX_CSG_OPS) {
        const BloomPrefabDesc64 *pf = &arena->map.prefabs[(prefab_id - 1U) & 15U];
        uint32_t prim1 = pf->csg_op_mask & 0xFU;
        int32_t r = (int32_t)(((int64_t)pf->bounding_radius_q16 * (int32_t)scale_q16) >> 16);
        int32_t ext[3] = { r, r, r };
        uint32_t idx = hdr->csg_count++;
        bloom_csg_op_init64(&arena->map.csg_ops[idx],
                            inst_id, prim1, BLOOM_CSG_BOOL_UNION, pf->matter_id,
                            pos, ext, 0, 2048);
        hdr->bvh_count = (uint32_t)bloom_bvh_build_from_csg64(
            arena->map.csg_ops,
            hdr->csg_count,
            arena->map.bvh_nodes,
            BLOOM_MAX_BVH_NODES);
    }
    hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    return lean_io_result_mk_ok(lean_box_uint32(inst_id));
}

LEAN_EXPORT lean_obj_res bloom_jsi_override_inst(b_lean_obj_arg eng,
                                                 uint32_t inst_id,
                                                 uint32_t style_id,
                                                 uint32_t matter_id,
                                                 uint32_t param_delta_q16) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    BloomEngineArena *arena = h->arena;
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    if (inst_id >= 1U && inst_id <= hdr->inst_count && inst_id <= BLOOM_MAX_INSTANCES) {
        bloom_prefab_override_inst(&arena->map.instances[inst_id - 1U],
                                   style_id, matter_id, (int32_t)param_delta_q16);
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res bloom_jsi_revert_inst(b_lean_obj_arg eng, uint32_t inst_id) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    BloomEngineArena *arena = h->arena;
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    if (inst_id >= 1U && inst_id <= hdr->inst_count && inst_id <= BLOOM_MAX_INSTANCES) {
        BloomPrefabInst64 *inst = &arena->map.instances[inst_id - 1U];
        const BloomPrefabDesc64 *ssot = &arena->map.prefabs[(inst->prefab_id - 1U) & 15U];
        bloom_prefab_revert_to_ssot(inst, ssot);
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res bloom_jsi_is_inst_overridden(b_lean_obj_arg eng, uint32_t inst_id) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    const BloomEngineArena *arena = h->arena;
    const BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    bool ovr = false;
    if (inst_id >= 1U && inst_id <= hdr->inst_count && inst_id <= BLOOM_MAX_INSTANCES) {
        ovr = (arena->map.instances[inst_id - 1U].flags & BLOOM_INST_FLAG_OVERRIDDEN) != 0U;
    }
    return lean_io_result_mk_ok(lean_box(ovr ? 1 : 0));
}

LEAN_EXPORT lean_obj_res bloom_jsi_set_inst_kelvin(b_lean_obj_arg eng, uint32_t inst_id, uint32_t kelvin) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    BloomEngineArena *arena = h->arena;
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    if (inst_id >= 1U && inst_id <= hdr->inst_count && inst_id <= BLOOM_MAX_INSTANCES) {
        arena->map.instances[inst_id - 1U].thermal_kelvin = kelvin;
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res bloom_jsi_get_inst_flow_slot(b_lean_obj_arg eng, uint32_t inst_id) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    const BloomEngineArena *arena = h->arena;
    const BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    uint32_t fslot = 0U;
    if (inst_id >= 1U && inst_id <= hdr->inst_count && inst_id <= BLOOM_MAX_INSTANCES) {
        fslot = arena->map.instances[inst_id - 1U].flow_slot;
    }
    return lean_io_result_mk_ok(lean_box_uint32(fslot));
}

LEAN_EXPORT lean_obj_res bloom_jsi_get_flow_chart(b_lean_obj_arg eng, uint32_t flow_slot) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    uint64_t w5 = h->arena->map.flow_states[flow_slot & 31U][5];
    uint32_t chart = (uint32_t)((w5 >> 16U) & BLOOM_FLOW_CHART_MASK);
    return lean_io_result_mk_ok(lean_box_uint32(chart));
}

LEAN_EXPORT lean_obj_res bloom_jsi_get_flow_mode(b_lean_obj_arg eng, uint32_t flow_slot) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    uint64_t w5 = h->arena->map.flow_states[flow_slot & 31U][5];
    uint16_t mode = (uint16_t)(w5 & 0xFFFFULL);
    return lean_io_result_mk_ok(lean_box(mode));
}

LEAN_EXPORT lean_obj_res bloom_jsi_swap_chart(b_lean_obj_arg eng,
                                              uint32_t flow_slot,
                                              uint32_t target_chart,
                                              uint16_t mode_req) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    bool ok = bloom_arena_try_swap_chart(h->arena, flow_slot, target_chart, mode_req);
    h->arena->map.frame_hdr.flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    return lean_io_result_mk_ok(lean_box(ok ? 1 : 0));
}

LEAN_EXPORT lean_obj_res bloom_jsi_set_reg(b_lean_obj_arg eng, uint32_t ch, uint32_t val_q16) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    bloom_reg_bank_set_q16(&h->arena->map.reg_bank, ch, (int32_t)val_q16);
    h->arena->map.frame_hdr.flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    return lean_io_result_mk_ok(lean_box(0));
}

LEAN_EXPORT lean_obj_res bloom_jsi_set_camera(b_lean_obj_arg eng,
                                              uint32_t yaw_q16,
                                              uint32_t pitch_q16,
                                              uint32_t dist_q16) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    BloomCamera64 *cam = &h->arena->map.camera;
    cam->orbit_yaw_q16 = (int32_t)yaw_q16;
    cam->orbit_pitch_q16 = (int32_t)pitch_q16;
    cam->orbit_dist_q16 = (int32_t)dist_q16;
    h->arena->map.frame_hdr.flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    return lean_io_result_mk_ok(lean_box(0));
}

static uint32_t bloom_jsi_dispatch_pointer_core(BloomEngineArena *arena,
                                                int32_t cx_q16,
                                                int32_t cy_q16,
                                                int32_t dx_q16,
                                                bool down,
                                                bool clicked) {
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    uint32_t action = bloom_ui_step_pointer_arena(arena, cx_q16, cy_q16, dx_q16, down, clicked);
    if (clicked && action == BLOOM_ACTION_NONE && arena->map.dom.hovered_node_id == 0U) {
        BloomRaycastHit64 *hit = &arena->map.ray_hit;
        if (bloom_scene_raycast64(arena, cx_q16, cy_q16, hit)) {
            hdr->selected_inst_id = hit->hit_inst_id;
            bloom_reg_bank_set_q16(&arena->map.reg_bank, 2U, (int32_t)(hit->hit_inst_id * 100U));
        }
    }
    if (down || clicked || action != BLOOM_ACTION_NONE) {
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }
    return action;
}

LEAN_EXPORT lean_obj_res bloom_jsi_pointer_event(b_lean_obj_arg eng,
                                                 uint32_t cx_q16,
                                                 uint32_t cy_q16,
                                                 uint32_t dx_q16,
                                                 uint8_t down,
                                                 uint8_t just_clicked) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    uint32_t action = bloom_jsi_dispatch_pointer_core(
        h->arena, (int32_t)cx_q16, (int32_t)cy_q16, (int32_t)dx_q16, down != 0U, just_clicked != 0U);
    return lean_io_result_mk_ok(lean_box_uint32(action));
}

LEAN_EXPORT lean_obj_res bloom_jsi_poll_window_input(b_lean_obj_arg eng) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    BloomEngineArena *arena = h->arena;
    BloomFrameHeader64 *hdr = &arena->map.frame_hdr;
    BloomWindow64 *win = &arena->map.window;
    BloomCamera64 *cam = &arena->map.camera;
    BloomDomState64 *dom = &arena->map.dom;

    bool down = (win->host_flags & BLOOM_WIN_FLAG_MOUSE_DOWN) != 0U;
    bool clicked = (win->host_flags & BLOOM_WIN_FLAG_JUST_CLICKED) != 0U;
    bool rclick = (win->host_flags & BLOOM_WIN_FLAG_RIGHT_CLICK) != 0U;

    uint32_t action = bloom_jsi_dispatch_pointer_core(
        arena, win->pointer_x_q16, win->pointer_y_q16, win->drag_dx_q16, down, clicked);

    if (rclick) {
        action = BLOOM_ACTION_PREFAB_REVERT;
    }

    if (down && dom->captured_node_id == 0U && dom->hovered_node_id == 0U &&
        (win->drag_dx_q16 != 0 || win->drag_dy_q16 != 0)) {
        cam->orbit_yaw_q16 += win->drag_dx_q16 * 2;
        int32_t np = cam->orbit_pitch_q16 + win->drag_dy_q16;
        if (np < -85000) np = -85000;
        if (np >  85000) np =  85000;
        cam->orbit_pitch_q16 = np;
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }

    if (win->scroll_dy_q16 != 0) {
        int32_t nd = cam->orbit_dist_q16 - win->scroll_dy_q16 * 4;
        if (nd < 2 * BLOOM_Q16_ONE) nd = 2 * BLOOM_Q16_ONE;
        if (nd > 14 * BLOOM_Q16_ONE) nd = 14 * BLOOM_Q16_ONE;
        cam->orbit_dist_q16 = nd;
        hdr->flags &= ~BLOOM_FLAG_IDLE_SLEEP;
    }

    return lean_io_result_mk_ok(lean_box_uint32(action));
}

LEAN_EXPORT lean_obj_res bloom_jsi_step_and_render(b_lean_obj_arg eng,
                                                   uint16_t mode_req,
                                                   uint8_t force_render) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    bool is_idle = bloom_arena_step_dynamics(h->arena, mode_req);
    if (!bloom_gpu_render(h->gpu, force_render != 0U)) {
        return lean_io_result_mk_error(
            lean_mk_io_user_error(lean_mk_string("bloom_jsi_step_and_render: SDL3 GPU dispatch failed")));
    }
    return lean_io_result_mk_ok(lean_box(is_idle ? 1 : 0));
}

LEAN_EXPORT lean_obj_res bloom_jsi_get_selected_inst(b_lean_obj_arg eng) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    return lean_io_result_mk_ok(lean_box_uint32(h->arena->map.frame_hdr.selected_inst_id));
}

LEAN_EXPORT lean_obj_res bloom_jsi_get_captured_node(b_lean_obj_arg eng) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    return lean_io_result_mk_ok(lean_box_uint32(h->arena->map.dom.captured_node_id));
}

LEAN_EXPORT lean_obj_res bloom_jsi_get_xray_q16(b_lean_obj_arg eng) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    return lean_io_result_mk_ok(lean_box_uint32((uint32_t)h->arena->map.frame_hdr.xray_plane_q16));
}

LEAN_EXPORT lean_obj_res bloom_jsi_get_dispatch_stats(b_lean_obj_arg eng) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    uint64_t dispatches = bloom_gpu_dispatch_count(h->gpu);
    uint64_t skips = bloom_gpu_idle_skip_count(h->gpu);
    return lean_io_result_mk_ok(lean_box_uint64((skips << 32ULL) | dispatches));
}

LEAN_EXPORT lean_obj_res bloom_jsi_seal_and_verify(b_lean_obj_arg eng) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    bloom_image_seal_arena(h->arena);
    bool ok = bloom_image_verify_arena(h->arena);
    return lean_io_result_mk_ok(lean_box(ok ? 1 : 0));
}

LEAN_EXPORT lean_obj_res bloom_jsi_export_ppm(b_lean_obj_arg eng, b_lean_obj_arg ppm_path) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    uint64_t fnv = 0ULL;
    uint32_t nonblack = 0U;
    if (!bloom_gpu_write_ppm(h->gpu, lean_string_cstr(ppm_path), &fnv, &nonblack)) {
        return lean_io_result_mk_error(
            lean_mk_io_user_error(lean_mk_string("bloom_jsi_export_ppm: failed to read back GPU frame")));
    }
    return lean_io_result_mk_ok(lean_box_uint32(nonblack));
}

LEAN_EXPORT lean_obj_res bloom_jsi_frame_hash(b_lean_obj_arg eng) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    uint64_t fnv = 0ULL;
    uint32_t nonblack = 0U;
    if (!bloom_gpu_write_ppm(h->gpu, NULL, &fnv, &nonblack)) {
        return lean_io_result_mk_error(
            lean_mk_io_user_error(lean_mk_string("bloom_jsi_frame_hash: failed to read back GPU frame")));
    }
    return lean_io_result_mk_ok(lean_box_uint64(fnv));
}

typedef struct {
    lean_object *closure;
} BloomLeanWindowCbCtx;

static void bloom_jsi_window_step_trampoline(void *raw) {
    BloomLeanWindowCbCtx *c = (BloomLeanWindowCbCtx *)raw;
    if (!c || !c->closure) return;
    lean_inc(c->closure);
    lean_object *res = lean_apply_2(c->closure, lean_box(0), lean_box(0));
    if (lean_io_result_is_error(res)) {
        lean_io_result_show_error(res);
    }
    lean_dec(res);
}

LEAN_EXPORT lean_obj_res bloom_jsi_run_window(b_lean_obj_arg eng, lean_obj_arg on_frame) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    BloomLeanWindowCbCtx cb_ctx = { .closure = on_frame };
    int rc = bloom_gpu_run_window(h->gpu, h->arena, bloom_jsi_window_step_trampoline, &cb_ctx);
    lean_dec(on_frame);
    return lean_io_result_mk_ok(lean_box_uint32((uint32_t)(rc == 0 ? 0U : 1U)));
}

/* ============================================================================
 * Compile-Time SEARU Torture + CPU Q16.16 vs GPU WGSL f32 Alignment Gates
 * ============================================================================ */

typedef struct {
    uint64_t sm64;
    uint32_t xs32;
    uint32_t epiphanies;
} BloomSearuState;

static void searu_init(BloomSearuState *st, uint64_t seed) {
    st->sm64 = seed | 1ULL;
    uint32_t fold = (uint32_t)((seed ^ (seed >> 32U)) & 0xFFFFFFFFULL);
    st->xs32 = fold ? fold : 0xCAFEBABEU;
    st->epiphanies = 0U;
}

static uint64_t searu_next_u64(BloomSearuState *st) {
    st->sm64 += UINT64_C(0x9E3779B97F4A7C15);
    uint64_t z = st->sm64;
    z = (z ^ (z >> 30U)) * UINT64_C(0xBF58476D1CE4E5B9);
    z = (z ^ (z >> 27U)) * UINT64_C(0x94D049BB133111EB);
    return z ^ (z >> 31U);
}

static uint32_t searu_next_u32(BloomSearuState *st) {
    uint32_t x = st->xs32;
    x ^= x << 13U;
    x ^= x >> 17U;
    x ^= x << 5U;
    st->xs32 = x ? x : 1U;
    return st->xs32;
}

/* Pure-integer SEARU DualChaosState Levy-flight / Black-Swan generator:
 * Heavy-tailed inverse-power jump in [1 .. 10000], hitting 10000 on Black Swan */
static int64_t searu_next_levy_step(BloomSearuState *st, int64_t scale_q16, bool *out_black_swan) {
    uint32_t r = searu_next_u32(st) & 0xFFFFU;
    int64_t zipf_mag = 10000LL;
    if (r > 12U) {
        /* Heavy-tailed Pareto ~ (65536 / r) capped at 10000 */
        zipf_mag = (int64_t)(65536U / r);
        if (zipf_mag < 1LL) zipf_mag = 1LL;
        if (zipf_mag > 10000LL) zipf_mag = 10000LL;
    }
    if (out_black_swan) *out_black_swan = (zipf_mag >= 5000LL);
    int64_t sign = (searu_next_u32(st) & 1U) ? 1LL : -1LL;
    return sign * ((zipf_mag * scale_q16) / 1000LL);
}

static int32_t searu_hostile_i32(BloomSearuState *st) {
    uint64_t r = searu_next_u64(st);
    switch (r & 7ULL) {
    case 0ULL: return INT32_MIN;
    case 1ULL: return INT32_MAX;
    case 2ULL: return 0;
    case 3ULL: return -1;
    case 4ULL: return BLOOM_MAX_Q16;
    case 5ULL: return -BLOOM_MAX_Q16;
    default:   return (int32_t)(uint32_t)(r >> 16U);
    }
}

/* Strict RFC 3629 UTF-8 + NUL validator over bounded capacity */
static bool searu_utf8_valid(const char *buf, size_t max_cap, size_t *out_len) {
    size_t n = 0U;
    while (n <= max_cap && buf[n] != '\0') ++n;
    if (n > max_cap) return false;
    size_t i = 0U;
    while (i < n) {
        uint8_t b0 = (uint8_t)buf[i];
        if (b0 < 0x80U) {
            ++i;
        } else if (b0 >= 0xC2U && b0 <= 0xDFU) {
            if (i + 2U > n) return false;
            if (((uint8_t)buf[i + 1U] & 0xC0U) != 0x80U) return false;
            i += 2U;
        } else if (b0 >= 0xE0U && b0 <= 0xEFU) {
            if (i + 3U > n) return false;
            uint8_t b1 = (uint8_t)buf[i + 1U], b2 = (uint8_t)buf[i + 2U];
            if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U) return false;
            if (b0 == 0xE0U && b1 < 0xA0U) return false;
            if (b0 == 0xEDU && b1 >= 0xA0U) return false;
            i += 3U;
        } else if (b0 >= 0xF0U && b0 <= 0xF4U) {
            if (i + 4U > n) return false;
            uint8_t b1 = (uint8_t)buf[i + 1U], b2 = (uint8_t)buf[i + 2U], b3 = (uint8_t)buf[i + 3U];
            if ((b1 & 0xC0U) != 0x80U || (b2 & 0xC0U) != 0x80U || (b3 & 0xC0U) != 0x80U) return false;
            if (b0 == 0xF0U && b1 < 0x90U) return false;
            if (b0 == 0xF4U && b1 >= 0x90U) return false;
            i += 4U;
        } else {
            return false;
        }
    }
    if (out_len) *out_len = n;
    return true;
}

static void searu_put_u32_le(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8U) & 0xFFU);
    p[2] = (uint8_t)((v >> 16U) & 0xFFU);
    p[3] = (uint8_t)((v >> 24U) & 0xFFU);
}

#define SEARU_REQ(cond, msg)                                                      \
    do {                                                                          \
        if (!(cond)) {                                                            \
            return lean_io_result_mk_error(                                       \
                lean_mk_io_user_error(lean_mk_string("[SEARU Core Gate] " msg))); \
        }                                                                         \
    } while (0)

LEAN_EXPORT lean_obj_res bloom_jsi_verify_searu_core(uint64_t seed) {
    BloomSearuState rng;
    searu_init(&rng, seed);

    /* 1. Binary glTF (.glb) Hostile / 0xFFFFFFFF Overflow / Valid Ingestion */
    {
        BloomCsgOp64 op;
        uint32_t vcnt = 0U;
        uint8_t glb[96];
        memset(glb, 0, sizeof(glb));
        SEARU_REQ(!bloom_glb_parse_to_csg64(NULL, 96U, 1U, 0U, &op, &vcnt), "GLB null rejected");
        SEARU_REQ(!bloom_glb_parse_to_csg64(glb, 20U, 1U, 0U, &op, &vcnt), "GLB short rejected");

        /* Build canonical valid .glb: 12B hdr + (8+4)B JSON "{  }" + (8+48)B BIN (4 float3 verts) = 80B */
        searu_put_u32_le(glb + 0U, 0x46546C67U); /* 'glTF' */
        searu_put_u32_le(glb + 4U, 2U);
        searu_put_u32_le(glb + 8U, 80U);
        searu_put_u32_le(glb + 12U, 4U);
        searu_put_u32_le(glb + 16U, 0x4E4F534AU); /* 'JSON' */
        memcpy(glb + 20U, "{  }", 4U);
        searu_put_u32_le(glb + 24U, 48U);
        searu_put_u32_le(glb + 28U, 0x004E4942U); /* 'BIN\0' */
        /* v0=(-1,-2,-0.5), v1=(3,2,1.5), v2=(0,0,0), v3=(1,1,1) as IEEE-754 bits */
        const uint32_t verts[12] = {
            0xBF800000U, 0xC0000000U, 0xBF000000U,
            0x40400000U, 0x40000000U, 0x3FC00000U,
            0x00000000U, 0x00000000U, 0x00000000U,
            0x3F800000U, 0x3F800000U, 0x3F800000U
        };
        for (size_t k = 0U; k < 12U; ++k) searu_put_u32_le(glb + 32U + k * 4U, verts[k]);

        SEARU_REQ(bloom_glb_parse_to_csg64(glb, 80U, 7U, BLOOM_MATTER_SAPPHIRE, &op, &vcnt), "Valid GLB parsed");
        SEARU_REQ(vcnt == 4U && op.pos_q16[0] == 65536 && op.pos_q16[1] == 0 && op.pos_q16[2] == 32768,
                  "GLB AABB center exact");
        SEARU_REQ(op.extents_q16[0] == 131072 && op.extents_q16[1] == 131072 && op.extents_q16[2] == 65536,
                  "GLB AABB extents exact");

        /* Inject 0xFFFFFFFF chunk_len wraparound attack at offset 12 */
        uint8_t bad[80];
        memcpy(bad, glb, 80U);
        searu_put_u32_le(bad + 12U, 0xFFFFFFFFU);
        SEARU_REQ(!bloom_glb_parse_to_csg64(bad, 80U, 1U, 0U, &op, &vcnt), "GLB 0xFFFFFFFF overflow rejected");

        /* Unaligned chunk_len = 3 */
        memcpy(bad, glb, 80U);
        searu_put_u32_le(bad + 12U, 3U);
        SEARU_REQ(!bloom_glb_parse_to_csg64(bad, 80U, 1U, 0U, &op, &vcnt), "GLB unaligned chunk rejected");

        /* NaN vertex in BIN chunk */
        memcpy(bad, glb, 80U);
        searu_put_u32_le(bad + 36U, 0x7FC00001U);
        SEARU_REQ(!bloom_glb_parse_to_csg64(bad, 80U, 1U, 0U, &op, &vcnt), "GLB NaN vertex rejected");

        /* Embedded NUL in JSON chunk */
        memcpy(bad, glb, 80U);
        bad[21] = 0U;
        SEARU_REQ(!bloom_glb_parse_to_csg64(bad, 80U, 1U, 0U, &op, &vcnt), "GLB JSON NUL rejected");
    }

    /* 2. BVH == Brute-Force CSG Exact 0-ULP Parity Gate (Union 24-op + Concentric + Smin/Sub/Int) */
    {
        BloomCsgOp64 ops_union[24];
        BloomBvhNode64 bvh[32];
        for (uint32_t i = 0U; i < 24U; ++i) {
            int32_t pos[3] = {
                (int32_t)(((int64_t)(i % 6U) - 3LL) * 45000LL),
                (int32_t)(((int64_t)((i / 6U) % 2U) - 1LL) * 52000LL),
                (int32_t)(((int64_t)(i / 12U)) * 48000LL)
            };
            int32_t ext[3] = { 28000 + (int32_t)(i * 1100U), 18000 + (int32_t)(i * 700U), 22000 };
            /* Include concentric inner sphere at i==1 with same pos as i==0 to test negative interior SDF */
            if (i == 1U) {
                pos[0] = ops_union[0].pos_q16[0];
                pos[1] = ops_union[0].pos_q16[1];
                pos[2] = ops_union[0].pos_q16[2];
                ext[0] = 90000;
            }
            uint32_t prim = (i % 6U) + 1U;
            bloom_csg_op_init64(&ops_union[i], i + 1U, prim, BLOOM_CSG_BOOL_UNION, i & 7U, pos, ext, 0, 2048);
        }
        size_t n_nodes = bloom_bvh_build_from_csg64(ops_union, 24U, bvh, 32U);
        SEARU_REQ(n_nodes > 0U && n_nodes <= 32U, "BVH 24-op build succeeded");

        for (int gx = -4; gx <= 4; ++gx) {
            for (int gy = -4; gy <= 4; ++gy) {
                for (int gz = -4; gz <= 4; ++gz) {
                    int64_t px = (int64_t)gx * 49152LL;
                    int64_t py = (int64_t)gy * 49152LL;
                    int64_t pz = (int64_t)gz * 49152LL;
                    uint32_t vis = 0U, mat = 0U;
                    int64_t db = bloom_bvh_eval_csg_q16(bvh, n_nodes, ops_union, 24U, px, py, pz, &vis);
                    int64_t dc = bloom_csg_eval_program_q16(ops_union, 24U, px, py, pz, &mat);
                    SEARU_REQ(db == dc, "BVH == CSG 0-ULP grid parity (24-op union)");
                }
            }
        }

        /* Mixed CSG tree (SMOOTH_UNION, SUBTRACT, INTERSECT) + SEARU Chaos & Extreme Coords */
        BloomCsgOp64 ops_mix[6];
        for (uint32_t i = 0U; i < 6U; ++i) {
            int32_t pos[3] = { (int32_t)((int64_t)i * 20000LL - 50000LL), 0, 0 };
            int32_t ext[3] = { 45000, 32000, 38000 };
            uint32_t bop = (i == 0U) ? BLOOM_CSG_BOOL_UNION
                         : ((i % 3U == 1U) ? BLOOM_CSG_BOOL_SMOOTH_UNION
                         : ((i % 3U == 2U) ? BLOOM_CSG_BOOL_SUBTRACT : BLOOM_CSG_BOOL_INTERSECT));
            bloom_csg_op_init64(&ops_mix[i], i + 1U, (i % 6U) + 1U, bop, i, pos, ext, 24576, 1536);
        }
        size_t n_mix = bloom_bvh_build_from_csg64(ops_mix, 6U, bvh, 32U);
        SEARU_REQ(n_mix > 0U, "BVH mixed CSG build succeeded");

        for (uint32_t q = 0U; q < 384U; ++q) {
            bool swan = false;
            int64_t px = (q < 8U) ? ((q & 1U) ? INT64_MAX : INT64_MIN) : searu_next_levy_step(&rng, 65536LL, &swan);
            int64_t py = (q < 8U) ? ((q & 2U) ? INT64_MAX : INT64_MIN) : searu_next_levy_step(&rng, 65536LL, &swan);
            int64_t pz = (q < 8U) ? ((q & 4U) ? INT64_MAX : INT64_MIN) : searu_next_levy_step(&rng, 65536LL, &swan);
            uint32_t vis = 0U, mat = 0U;
            int64_t ub = bloom_bvh_eval_csg_q16(bvh, n_mix, ops_mix, 6U, px, py, pz, &vis);
            int64_t uc = bloom_csg_eval_program_q16(ops_mix, 6U, px, py, pz, &mat);
            SEARU_REQ(ub == uc, "BVH == CSG 0-ULP SEARU chaos parity (mixed smin/sub/int)");
        }
    }

    /* 3. Dual-Quaternion Antipodal (dot < 0), Zero-Norm & INT32_MIN/MAX Torture */
    {
        BloomJointDq64 j_pos, j_neg, j_mid, j_zero, j_ext;
        const int32_t q_pos[4] = {  65536, 0, 0, 0 };
        const int32_t q_neg[4] = { -65536, 0, 0, 0 };
        const int32_t q_z[4]   = { 0, 0, 0, 0 };
        const int32_t q_min[4] = { INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN };
        const int32_t tr_a[3]  = { 65536, 0, 0 };
        const int32_t tr_b[3]  = { 65536, 0, 0 };
        const int32_t tr_m[3]  = { INT32_MIN, INT32_MAX, INT32_MIN };

        bloom_joint_dq64_make(&j_pos, 1U, 0U, q_pos, tr_a, 65536, 16384, 8192, 1U);
        bloom_joint_dq64_make(&j_neg, 2U, 1U, q_neg, tr_b, 65536, 16384, 8192, 1U);
        bloom_joint_dq64_blend(&j_pos, &j_neg, 32768, &j_mid);

        const int32_t p_in[3] = { 32768, -16384, 49152 };
        int32_t p_out[3] = { 0, 0, 0 };
        bloom_joint_dq64_transform_q16(&j_mid, p_in, p_out);
        /* Antipodal identity (w=+1 vs w=-1) blend with translation +1.0 X places p_out at (1.5, -0.25, 0.75) */
        SEARU_REQ(p_out[0] > 96000 && p_out[0] < 100500 &&
                  p_out[1] > -18000 && p_out[1] < -15000 &&
                  p_out[2] > 47500 && p_out[2] < 50800,
                  "DQ antipodal hemisphere blend preserves rigid transform");

        bloom_joint_dq64_make(&j_zero, 3U, 0U, q_z, tr_a, 0, 0, 0, 0U);
        bloom_joint_dq64_make(&j_ext, 4U, 0U, q_min, tr_m, INT32_MIN, INT32_MIN, INT32_MIN, 0U);
        bloom_joint_dq64_blend(&j_zero, &j_ext, INT32_MIN, &j_mid);
        const int32_t p_ext[3] = { INT32_MIN, INT32_MAX, INT32_MIN };
        bloom_joint_dq64_transform_q16(&j_ext, p_ext, p_out);

        BloomJointDq64 skel[2] = { j_pos, j_mid };
        int64_t sd_origin = bloom_skeleton_eval_sdf_q16(skel, 2U, 65536, 0, 0);
        int64_t sd_far    = bloom_skeleton_eval_sdf_q16(skel, 2U, INT64_MIN, INT64_MAX, INT64_MIN);
        SEARU_REQ(sd_origin < 0 && sd_far > 0, "Skeleton SDF interior/extreme valid");
    }

    /* 4. 4-Bit SDF Brick Pack / Trilinear Sample & Extreme Boundary Torture */
    {
        BloomSdfBrick64 brick;
        int32_t samples[64];
        const int32_t orig[3] = { -65536, -65536, -65536 };
        for (uint32_t z = 0U; z < 4U; ++z) {
            for (uint32_t y = 0U; y < 4U; ++y) {
                for (uint32_t x = 0U; x < 4U; ++x) {
                    samples[z * 16U + y * 4U + x] = ((int32_t)x - 2) * 16384;
                }
            }
        }
        bloom_sdf_brick64_pack(&brick, 1U, 2U, orig, 65536, samples);
        int64_t s_lo = bloom_sdf_brick64_sample_q16(&brick, -65536, 0, 0);
        int64_t s_hi = bloom_sdf_brick64_sample_q16(&brick, 131072, 0, 0);
        int64_t s_ex1 = bloom_sdf_brick64_sample_q16(&brick, INT64_MIN, INT64_MAX, INT64_MIN);
        int64_t s_ex2 = bloom_sdf_brick64_sample_q16(&brick, INT64_MAX, INT64_MIN, INT64_MAX);
        SEARU_REQ(s_lo < 0 && s_hi > 0 && s_hi > s_lo && s_ex2 >= s_ex1, "4-bit SDF brick monotone & overflow-free");
    }

    /* 5. Strict RFC 3629 UTF-8 CJK (3B) + Emoji (4B) + ASCII 39B/35B Cliff & IME Torture */
    {
        BloomTextInput64 inp;
        BloomIme64 ime;
        bloom_text_input_init64(&inp, 1U, BLOOM_ACTION_FOCUS_INPUT);
        bloom_ime_init64(&ime, 1U);

        /* 14 Traditional Chinese chars (42B) -> must accept exactly 13 chars (39B) and reject 14th cleanly */
        const char *cjk42 = "繁體中文測試輸入法邊界安全驗";
        for (size_t i = 0U; cjk42[i] != '\0'; ++i) {
            (void)bloom_text_input_insert_char64(&inp, cjk42[i]);
            size_t ulen = 0U;
            SEARU_REQ(searu_utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP, &ulen),
                      "UTF-8 valid after every byte insert");
            SEARU_REQ(ulen == inp.byte_len, "byte_len matches NUL-terminated UTF-8 length");
        }
        SEARU_REQ(inp.byte_len == 39U, "Exact 39B CJK capacity filled");

        /* Test 37B and 38B cliffs with 4-byte emoji "🚀" (\xF0\x9F\x9A\x80) */
        SEARU_REQ(bloom_text_input_backspace64(&inp) && inp.byte_len == 36U, "Backspace deletes full 3B CJK char");
        SEARU_REQ(bloom_text_input_insert_char64(&inp, 'A') && inp.byte_len == 37U, "37B ASCII insert");
        const char *rocket = "\xF0\x9F\x9A\x80";
        for (size_t k = 0U; k < 4U; ++k) {
            SEARU_REQ(!bloom_text_input_insert_char64(&inp, rocket[k]), "4B emoji rejected at 37B cliff");
            SEARU_REQ(searu_utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP, NULL) && inp.byte_len == 37U,
                      "37B cliff intact");
        }
        /* Reject invalid UTF-8 sequences (continuation, overlong, UTF-16 surrogate) */
        const uint8_t bad_utf8[6] = { 0x80U, 0xC0U, 0xFFU, 0xEDU, 0xA0U, 0x80U };
        for (size_t k = 0U; k < 6U; ++k) {
            SEARU_REQ(!bloom_text_input_insert_char64(&inp, (char)bad_utf8[k]), "Invalid UTF-8 byte rejected");
            SEARU_REQ(searu_utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP, NULL), "UTF-8 valid after bad byte");
        }

        /* Backspace all codepoints down to 0 */
        uint32_t bs_steps = 0U;
        while (inp.byte_len > 0U && bs_steps < 64U) {
            SEARU_REQ(bloom_text_input_backspace64(&inp), "Backspace step ok");
            SEARU_REQ(searu_utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP, NULL), "UTF-8 valid on Backspace");
            ++bs_steps;
        }
        SEARU_REQ(inp.byte_len == 0U && bs_steps == 13U, "12 CJK + 1 ASCII deleted in 13 Backspaces");

        /* IME 35B preedit cliff with mixed CJK + 4B emoji + commit splice */
        const char *pre_long = "繁體中文🚀🔥🌊測試輸入法邊界安全驗證超長字串";
        SEARU_REQ(bloom_ime_set_preedit64(&ime, pre_long, 2U), "IME preedit set");
        size_t pre_len = 0U;
        SEARU_REQ(searu_utf8_valid(ime.preedit_utf8, sizeof(ime.preedit_utf8) - 1U, &pre_len) &&
                  pre_len <= (sizeof(ime.preedit_utf8) - 1U) && pre_len >= 33U,
                  "IME preedit fitted cleanly at <=35B codepoint boundary");
        (void)bloom_text_input_insert_char64(&inp, 'X');
        const char *taiwan = "台灣";
        for (size_t k = 0U; taiwan[k] != '\0'; ++k) (void)bloom_text_input_insert_char64(&inp, taiwan[k]);
        inp.cursor_pos = 2U; /* adversarial mid-codepoint cursor! */
        /* 7B + >=33B > 39B must reject atomic overflow cleanly without corrupting UTF-8 */
        SEARU_REQ(!bloom_ime_commit64(&ime, &inp), "IME commit >39B atomic overflow rejected");
        SEARU_REQ(searu_utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP, NULL) && inp.byte_len == 7U,
                  "UTF-8 intact after rejected IME overflow commit");
        /* Now set 10B preedit ("繁體🚀") -> 7B + 10B = 17B <= 39B must splice cleanly at codepoint boundary */
        SEARU_REQ(bloom_ime_set_preedit64(&ime, "繁體\xF0\x9F\x9A\x80", 1U), "IME 10B preedit set");
        inp.cursor_pos = 2U;
        SEARU_REQ(bloom_ime_commit64(&ime, &inp) && inp.byte_len == 17U, "IME commit splice ok");
        SEARU_REQ(searu_utf8_valid(inp.utf8_buf, BLOOM_TEXT_INPUT_CAP, NULL), "UTF-8 valid after mid-cp IME commit");
    }

    /* 6. SEARU DualChaosState + Epiphany Simulated Annealing across Flex/Bento/LOD/SVG/LaTeX/Matter/Style */
    {
        int64_t temp_q16 = 65536LL;
        int64_t best_fit = INT64_MIN;
        BloomFlexContainer64 flex;
        BloomUiNode64 nodes[8];
        BloomIntent64 intents[6];
        BloomDomState64 dom;
        BloomRegBank64 bank;
        BloomPrefabInst64 insts[16], compacted[16];
        BloomArtStyle64 styles[BLOOM_MAX_STYLES];
        BloomPrefabDesc64 prefabs[BLOOM_MAX_PREFABS];
        BloomMatter64 matters[BLOOM_MAX_MATTERS];

        bloom_reg_bank_init64(&bank);
        bloom_prefab_init_canonical(styles, prefabs);
        bloom_matter_init_canonical(matters);

        /* Verify 128-hex Matter wire roundtrip & MAC tamper rejection */
        char hex129[129];
        BloomMatter64 parsed_m;
        bloom_matter_format_hex128(&matters[1], hex129);
        SEARU_REQ(bloom_matter_parse_hex128(hex129, &parsed_m) && parsed_m.ior_eta_q16 == matters[1].ior_eta_q16,
                  "Matter hex128 roundtrip exact");
        hex129[10] = (hex129[10] == '0') ? '1' : '0';
        SEARU_REQ(!bloom_matter_parse_hex128(hex129, &parsed_m), "Matter hex128 MAC tamper rejected");

        /* Verify TRC strobe <= 3.0 Hz guard + 800K/760K Schmitt hysteresis */
        bool hot = false;
        BloomArtStyle64 st_hot = bloom_style_resolve_thermal(styles, 1U, 820U, false, true, true, &hot);
        SEARU_REQ(hot && st_hot.style_id == 8U && st_hot.strobe_hz_q16 <= BLOOM_TRC_MAX_STROBE_HZ_Q16,
                  "Thermal 800K enter + TRC <=3Hz clamp");
        (void)bloom_style_resolve_thermal(styles, 1U, 775U, hot, true, false, &hot);
        SEARU_REQ(hot, "Thermal 775K stays latched above 760K exit");
        (void)bloom_style_resolve_thermal(styles, 1U, 755U, hot, true, false, &hot);
        SEARU_REQ(!hot, "Thermal 755K exits below 760K");

        for (uint32_t iter = 0U; iter < 192U; ++iter) {
            bool black_swan = false;
            int64_t step = searu_next_levy_step(&rng, temp_q16, &black_swan);

            int32_t ox = (iter < 16U) ? searu_hostile_i32(&rng) : (int32_t)(step & 0x7FFFFFFFLL);
            int32_t oy = searu_hostile_i32(&rng);
            int32_t hw = searu_hostile_i32(&rng);
            int32_t hh = searu_hostile_i32(&rng);
            int32_t pad = searu_hostile_i32(&rng);
            int32_t gap = searu_hostile_i32(&rng);

            memset(nodes, 0, sizeof(nodes));
            for (size_t k = 0U; k < 8U; ++k) {
                nodes[k].half_w_q16 = searu_hostile_i32(&rng);
                nodes[k].half_h_q16 = searu_hostile_i32(&rng);
            }
            bloom_flex_init64(&flex, (uint16_t)(iter & 1U), (uint16_t)(iter & 3U),
                              (uint16_t)((iter >> 2U) & 3U), ox, oy, hw, hh, pad, gap);
            bloom_flex_apply64(&flex, nodes, 8U, searu_hostile_i32(&rng));

            memset(intents, 0, sizeof(intents));
            for (size_t k = 0U; k < 6U; ++k) {
                intents[k].intent_id = (uint32_t)(k + 1U);
                intents[k].current_val_q16 = searu_hostile_i32(&rng);
                intents[k].lo_exit_q16 = searu_hostile_i32(&rng);
                intents[k].hi_enter_q16 = searu_hostile_i32(&rng);
                intents[k].bind_channel = (uint32_t)(k + 8U);
            }
            memset(&dom, 0, sizeof(dom));
            size_t bn = bloom_ui_synthesize_bento64(intents, 6U, &bank, nodes, 8U, &dom);
            SEARU_REQ(bn == 6U && dom.compiled_node_count == 6U, "Bento synthesized 6 panels");

            for (size_t k = 0U; k < 16U; ++k) {
                memset(&insts[k], 0, sizeof(BloomPrefabInst64));
                insts[k].inst_id = (uint32_t)(k + 1U);
                insts[k].flags = (k & 1U) ? BLOOM_INST_FLAG_ACTIVE : 0U;
                insts[k].pos_q16[0] = searu_hostile_i32(&rng);
                insts[k].pos_q16[1] = searu_hostile_i32(&rng);
                insts[k].pos_q16[2] = searu_hostile_i32(&rng);
            }
            const int32_t eye_ext[3] = { searu_hostile_i32(&rng), searu_hostile_i32(&rng), searu_hostile_i32(&rng) };
            uint64_t mask64 = 0ULL;
            uint32_t cn = bloom_prefab_cull_wave64(insts, 16U, eye_ext, searu_hostile_i32(&rng),
                                                   searu_hostile_i32(&rng), compacted, &mask64);
            SEARU_REQ(cn == 8U && mask64 == 0xAAAAULL, "Wave64 LOD culling exact under extreme coords");

            int64_t fit = (int64_t)dom.dom_mask_lo32 + (int64_t)flex.tile_mask_lo32;
            if (black_swan && fit > best_fit) {
                /* SEARU Epiphany re-heat! */
                rng.epiphanies++;
                temp_q16 = (temp_q16 * 4LL <= 65536LL) ? (temp_q16 * 4LL) : 65536LL;
                best_fit = fit;
            } else {
                temp_q16 = (temp_q16 * 63LL) >> 6U;
                if (temp_q16 < 1024LL) temp_q16 = 1024LL;
            }
        }

        /* Malformed SVG & deeply nested LaTeX hostile string torture */
        BloomGlyph64 glyphs[4];
        size_t gcnt = 0U;
        const char *bad_svg = "<svg viewBox='0 0 -999999999999 0'><rect x='-99999999999' width='-5' data-action='scrub_xray'/><path d='M --.. L 999999999999 ,,";
        (void)bloom_svg_compile_text(bad_svg, nodes, 8U, glyphs, 4U, &gcnt, &dom);

        BloomLatexBox64 lboxes[16];
        int32_t lw = 0;
        const char *deep_lx = "\\frac{\\frac{\\frac{\\frac{\\frac{\\sqrt{\\int\\sum x^2_i}}{a}}{b}}{c}}{d}}{e}";
        size_t lc = bloom_latex_compile_boxes64(deep_lx, INT32_MIN, INT32_MAX, INT32_MIN, 0xFFFFFFU, lboxes, 16U, &lw);
        SEARU_REQ(lc > 0U && lc <= 16U, "Deep hostile LaTeX bounded");
        (void)bloom_latex_eval_box_sdf_q16(&lboxes[0], INT32_MIN, INT32_MAX);
    }

    return lean_io_result_mk_ok(lean_box(1));
}

static bool searu_load_ppm_rgb(const BloomGpuCtx *gpu, const char *tmp_path, uint8_t *out_rgb, size_t expected_bytes) {
    uint64_t fnv = 0ULL;
    uint32_t nonblack = 0U;
    if (!bloom_gpu_write_ppm(gpu, tmp_path, &fnv, &nonblack)) return false;
    const size_t cap = expected_bytes + 64U;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) {
        (void)remove(tmp_path);
        return false;
    }
    FILE *fp = fopen(tmp_path, "rb");
    if (!fp) {
        free(buf);
        (void)remove(tmp_path);
        return false;
    }
    size_t total = fread(buf, 1U, cap, fp);
    fclose(fp);
    (void)remove(tmp_path);

    /* Parse "P6\n<w> <h>\n255\n" (3 newline-terminated header lines) purely in memory */
    size_t pos = 0U;
    uint32_t nl = 0U;
    while (pos < total && nl < 3U) {
        if (buf[pos] == (uint8_t)'\n') ++nl;
        ++pos;
    }
    bool ok = false;
    if (nl == 3U && total >= pos && (total - pos) >= expected_bytes) {
        memcpy(out_rgb, buf + pos, expected_bytes);
        ok = true;
    }
    free(buf);
    return ok;
}

LEAN_EXPORT lean_obj_res bloom_jsi_verify_gpu_alignment(b_lean_obj_arg eng) {
    BloomJsiHandle *h = bloom_jsi_unwrap(eng);
    BloomEngineArena *arena = h->arena;
    const uint32_t w = arena->map.frame_hdr.width_px;
    const uint32_t hp = arena->map.frame_hdr.height_px;
    SEARU_REQ(w >= 160U && hp >= 120U && w <= 1024U && hp <= 1024U, "Alignment viewport dimensions valid");

    const size_t rgb_bytes = (size_t)w * (size_t)hp * 3U;
    uint8_t *bg_rgb = (uint8_t *)malloc(rgb_bytes);
    uint8_t *fg_rgb = (uint8_t *)malloc(rgb_bytes);
    if (!bg_rgb || !fg_rgb) {
        free(bg_rgb);
        free(fg_rgb);
        return lean_io_result_mk_error(lean_mk_io_user_error(lean_mk_string("Alignment scratch buffer alloc")));
    }

    const char *ppm_tmp = "build/.align_probe_tmp.ppm";
    char err_buf[256];
    err_buf[0] = '\0';

    /* Helper camera setup: eye=(0,0,-7.0), target=(0,0,0), focal=28mm (fov_z=1.0) */
    bloom_arena_init_default(arena, w, hp);
    arena->map.camera.eye_q16[0] = 0;
    arena->map.camera.eye_q16[1] = 0;
    arena->map.camera.eye_q16[2] = -7 * BLOOM_Q16_ONE;
    arena->map.camera.target_q16[0] = 0;
    arena->map.camera.target_q16[1] = 0;
    arena->map.camera.target_q16[2] = 0;
    arena->map.camera.focal_mm_q16 = 28 * BLOOM_Q16_ONE;
    arena->map.frame_hdr.flags &= ~BLOOM_FLAG_IDLE_SLEEP;

    /* Dispatch 0: Empty scene background reference */
    if (!bloom_gpu_render(h->gpu, true) || !searu_load_ppm_rgb(h->gpu, ppm_tmp, bg_rgb, rgb_bytes)) {
        snprintf(err_buf, sizeof(err_buf), "Background reference GPU render/readback failed");
        goto done;
    }

    /* Dispatch 1: All 8 Standard Prefabs (2x4 grid in upper viewport) + UI Bento Hit-Boxes (bottom HUD) */
    {
        const int32_t xs[4] = { -265420 /* -4.05 */, -88474 /* -1.35 */, 88474 /* +1.35 */, 265420 /* +4.05 */ };
        const int32_t ys[2] = { 140902 /* +2.15 */, -3277 /* -0.05 */ };
        for (uint32_t p = 1U; p <= 8U; ++p) {
            uint32_t col = (p - 1U) % 4U;
            uint32_t row = (p - 1U) / 4U;
            int32_t pos[3] = { xs[col], ys[row], 0 };
            uint32_t iid = bloom_prefab_spawn_inst(arena, p, pos, 34078 /* 0.52x */);
            if (iid != p) {
                snprintf(err_buf, sizeof(err_buf), "8-Prefab spawn id mismatch (%u vs %u)", iid, p);
                goto done;
            }
            /* Use Blackbody incandescent matter override (param_delta=0!) so every hit pixel has high contrast vs sky */
            bloom_prefab_override_inst(&arena->map.instances[iid - 1U], 8U, BLOOM_MATTER_BLACKBODY, 0);
        }

        BloomIntent64 intents[4];
        memset(intents, 0, sizeof(intents));
        for (uint32_t k = 0U; k < 4U; ++k) {
            intents[k].intent_id = k + 1U;
            intents[k].prim_type = BLOOM_UI_PRIM_RECT_GLASS;
            intents[k].bind_channel = k;
            intents[k].lo_exit_q16 = 16384;
            intents[k].hi_enter_q16 = 49152;
            intents[k].current_val_q16 = 60000; /* TRIP crimson 0xFF2848 for crisp foreground contrast */
        }
        arena->map.frame_hdr.ui_count = (uint32_t)bloom_ui_synthesize_bento64(
            intents, 4U, NULL, arena->map.ui_nodes, BLOOM_MAX_UI_NODES, &arena->map.dom);
        arena->map.frame_hdr.flags &= ~BLOOM_FLAG_IDLE_SLEEP;

        if (!bloom_gpu_render(h->gpu, true) || !searu_load_ppm_rgb(h->gpu, ppm_tmp, fg_rgb, rgb_bytes)) {
            snprintf(err_buf, sizeof(err_buf), "8-Prefab + UI Bento GPU render/readback failed");
            goto done;
        }

        uint32_t inter_pf[9] = { 0 }, uni_pf[9] = { 0 }, cpu_pf[9] = { 0 }, gpu_pf[9] = { 0 };
        uint32_t ui_inter = 0U, ui_uni = 0U;

        for (uint32_t py = 0U; py < hp; ++py) {
            int32_t norm_y_q16 = (int32_t)((((int64_t)(2U * py + 1U) * 65536LL) / (int64_t)hp) - 65536LL);
            for (uint32_t px = 0U; px < w; ++px) {
                int32_t norm_x_q16 = (int32_t)((((int64_t)(2U * px + 1U) * 65536LL) / (int64_t)w) - 65536LL);
                size_t idx = ((size_t)py * (size_t)w + (size_t)px) * 3U;
                int dr = (int)fg_rgb[idx + 0U] - (int)bg_rgb[idx + 0U];
                int dg = (int)fg_rgb[idx + 1U] - (int)bg_rgb[idx + 1U];
                int db = (int)fg_rgb[idx + 2U] - (int)bg_rgb[idx + 2U];
                int l1 = (dr < 0 ? -dr : dr) + (dg < 0 ? -dg : dg) + (db < 0 ? -db : db);

                if (norm_y_q16 < 23592 /* +0.36: 3D 8-Prefab region */) {
                    bool gpu_hit = (l1 > 45);
                    uint32_t tile_col = (px * 4U) / w;
                    uint32_t tile_row = (norm_y_q16 < -20316 /* -0.31 corridor */) ? 0U : 1U;
                    uint32_t expected_p = tile_row * 4U + tile_col + 1U;

                    BloomRaycastHit64 hit;
                    bool cpu_hit = bloom_scene_raycast64(arena, norm_x_q16, norm_y_q16, &hit);
                    if (cpu_hit && (hit.hit_inst_id != expected_p || hit.hit_prefab_id != expected_p)) {
                        snprintf(err_buf, sizeof(err_buf),
                                 "CPU raycast tile mismatch at px=%u py=%u: expected=%u got=%u",
                                 px, py, expected_p, hit.hit_inst_id);
                        goto done;
                    }
                    if (cpu_hit) cpu_pf[expected_p]++;
                    if (gpu_hit) gpu_pf[expected_p]++;
                    if (cpu_hit && gpu_hit) inter_pf[expected_p]++;
                    if (cpu_hit || gpu_hit) uni_pf[expected_p]++;
                } else if (norm_y_q16 > 28835 /* +0.44: UI Bento HUD region */) {
                    bool gpu_ui = (l1 > 36);
                    (void)bloom_ui_step_pointer_arena(arena, norm_x_q16, norm_y_q16, 0, false, false);
                    for (uint32_t u = 0U; u < arena->map.frame_hdr.ui_count; ++u) {
                        arena->map.ui_nodes[u].scale_jet_v_q16 = BLOOM_Q16_ONE;
                    }
                    bool cpu_ui = (arena->map.dom.hovered_node_id != 0U);
                    if (cpu_ui && gpu_ui) ui_inter++;
                    if (cpu_ui || gpu_ui) ui_uni++;
                }
            }
        }

        for (uint32_t p = 1U; p <= 8U; ++p) {
            if (cpu_pf[p] < 40U || gpu_pf[p] < 40U || uni_pf[p] == 0U ||
                (inter_pf[p] * 100U) < (uni_pf[p] * 85U)) {
                snprintf(err_buf, sizeof(err_buf),
                         "Prefab %u CPU-vs-GPU IoU < 0.85 (cpu=%u gpu=%u inter=%u uni=%u)",
                         p, cpu_pf[p], gpu_pf[p], inter_pf[p], uni_pf[p]);
                goto done;
            }
        }
        if (ui_uni == 0U || (ui_inter * 100U) < (ui_uni * 85U)) {
            snprintf(err_buf, sizeof(err_buf),
                     "UI Bento CPU-vs-GPU IoU < 0.85 (inter=%u uni=%u)", ui_inter, ui_uni);
            goto done;
        }
    }

    /* Dispatch 2: X-Ray Cutaway Active (xray_plane = 0.0) on Hollow Prefab 6 (Imperial Jade Vessel) */
    {
        bloom_arena_init_default(arena, w, hp);
        arena->map.camera.eye_q16[0] = 0;
        arena->map.camera.eye_q16[1] = 0;
        arena->map.camera.eye_q16[2] = -7 * BLOOM_Q16_ONE;
        arena->map.camera.target_q16[0] = 0;
        arena->map.camera.target_q16[1] = 0;
        arena->map.camera.target_q16[2] = 0;
        arena->map.camera.focal_mm_q16 = 28 * BLOOM_Q16_ONE;

        const int32_t origin_y[3] = { 0, 52428 /* +0.80 */, 0 };
        uint32_t jid = bloom_prefab_spawn_inst(arena, BLOOM_PREFAB_JADE_VESSEL, origin_y, 78643 /* 1.20x */);
        bloom_prefab_override_inst(&arena->map.instances[jid - 1U], 8U, BLOOM_MATTER_BLACKBODY, 0);
        arena->map.frame_hdr.xray_plane_q16 = 0;
        arena->map.frame_hdr.flags |= BLOOM_FLAG_XRAY_ENABLED;
        arena->map.frame_hdr.flags &= ~BLOOM_FLAG_IDLE_SLEEP;

        if (!bloom_gpu_render(h->gpu, true) || !searu_load_ppm_rgb(h->gpu, ppm_tmp, fg_rgb, rgb_bytes)) {
            snprintf(err_buf, sizeof(err_buf), "X-Ray Cutaway GPU render/readback failed");
            goto done;
        }

        uint32_t left_leaks_cpu = 0U, left_leaks_gpu = 0U;
        uint32_t xr_inter = 0U, xr_uni = 0U;
        for (uint32_t py = 0U; py < hp; ++py) {
            int32_t norm_y_q16 = (int32_t)((((int64_t)(2U * py + 1U) * 65536LL) / (int64_t)hp) - 65536LL);
            for (uint32_t px = 0U; px < w; ++px) {
                int32_t norm_x_q16 = (int32_t)((((int64_t)(2U * px + 1U) * 65536LL) / (int64_t)w) - 65536LL);
                size_t idx = ((size_t)py * (size_t)w + (size_t)px) * 3U;
                int dr = (int)fg_rgb[idx + 0U] - (int)bg_rgb[idx + 0U];
                int dg = (int)fg_rgb[idx + 1U] - (int)bg_rgb[idx + 1U];
                int db = (int)fg_rgb[idx + 2U] - (int)bg_rgb[idx + 2U];
                int l1 = (dr < 0 ? -dr : dr) + (dg < 0 ? -dg : dg) + (db < 0 ? -db : db);
                bool gpu_hit = (l1 > 45);

                BloomRaycastHit64 hit;
                bool cpu_hit = bloom_scene_raycast64(arena, norm_x_q16, norm_y_q16, &hit);

                if (norm_x_q16 < -1638) {
                    if (cpu_hit) left_leaks_cpu++;
                    if (gpu_hit) left_leaks_gpu++;
                }
                if (cpu_hit && gpu_hit) xr_inter++;
                if (cpu_hit || gpu_hit) xr_uni++;
            }
        }
        if (left_leaks_cpu != 0U || left_leaks_gpu != 0U) {
            snprintf(err_buf, sizeof(err_buf),
                     "X-Ray Cutaway x<0 leak (cpu=%u gpu=%u)", left_leaks_cpu, left_leaks_gpu);
            goto done;
        }
        if (xr_uni < 200U || (xr_inter * 100U) < (xr_uni * 85U)) {
            snprintf(err_buf, sizeof(err_buf),
                     "X-Ray Cutaway CPU-vs-GPU IoU < 0.85 (inter=%u uni=%u)", xr_inter, xr_uni);
            goto done;
        }
    }

done:
    free(bg_rgb);
    free(fg_rgb);
    if (err_buf[0] != '\0') {
        return lean_io_result_mk_error(lean_mk_io_user_error(lean_mk_string(err_buf)));
    }
    return lean_io_result_mk_ok(lean_box(1));
}

/* ============================================================================
 * Lean 4 VM Interpreter Boxed Entrypoints (`lean --load-dynlib=...`, 0 `lean -c`)
 * ============================================================================ */

LEAN_EXPORT lean_obj_res l_Bloom_createEngine___boxed(lean_obj_arg w, lean_obj_arg h, lean_obj_arg spath, lean_obj_arg world) {
    (void)world;
    uint32_t uw = lean_unbox_uint32(w), uh = lean_unbox_uint32(h);
    lean_dec(w);
    lean_dec(h);
    lean_obj_res r = bloom_jsi_create(uw, uh, spath);
    lean_dec(spath);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_deviceInfo___boxed(lean_obj_arg eng, lean_obj_arg world) {
    (void)world;
    lean_obj_res r = bloom_jsi_device_info(eng);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_isHardwareGpu___boxed(lean_obj_arg eng, lean_obj_arg world) {
    (void)world;
    lean_obj_res r = bloom_jsi_is_hardware_gpu(eng);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_isUnifiedMemory___boxed(lean_obj_arg eng, lean_obj_arg world) {
    (void)world;
    lean_obj_res r = bloom_jsi_is_unified_memory(eng);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_loadSvgUI___boxed(lean_obj_arg eng, lean_obj_arg spath, lean_obj_arg world) {
    (void)world;
    lean_obj_res r = bloom_jsi_load_svg(eng, spath);
    lean_dec(eng);
    lean_dec(spath);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_compileLatex___boxed(lean_obj_arg eng, lean_obj_arg lstr, lean_obj_arg x, lean_obj_arg y, lean_obj_arg em, lean_obj_arg rgb, lean_obj_arg world) {
    (void)world;
    uint32_t ux = lean_unbox_uint32(x), uy = lean_unbox_uint32(y), uem = lean_unbox_uint32(em), urgb = lean_unbox_uint32(rgb);
    lean_dec(x); lean_dec(y); lean_dec(em); lean_dec(rgb);
    lean_obj_res r = bloom_jsi_compile_latex(eng, lstr, ux, uy, uem, urgb);
    lean_dec(eng);
    lean_dec(lstr);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_spawnPrefab___boxed(lean_obj_arg eng, lean_obj_arg pid, lean_obj_arg x, lean_obj_arg y, lean_obj_arg z, lean_obj_arg sc, lean_obj_arg world) {
    (void)world;
    uint32_t upid = lean_unbox_uint32(pid), ux = lean_unbox_uint32(x), uy = lean_unbox_uint32(y), uz = lean_unbox_uint32(z), usc = lean_unbox_uint32(sc);
    lean_dec(pid); lean_dec(x); lean_dec(y); lean_dec(z); lean_dec(sc);
    lean_obj_res r = bloom_jsi_spawn_prefab(eng, upid, ux, uy, uz, usc);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_overrideInst___boxed(lean_obj_arg eng, lean_obj_arg iid, lean_obj_arg sid, lean_obj_arg mid, lean_obj_arg delta, lean_obj_arg world) {
    (void)world;
    uint32_t uiid = lean_unbox_uint32(iid), usid = lean_unbox_uint32(sid), umid = lean_unbox_uint32(mid), udel = lean_unbox_uint32(delta);
    lean_dec(iid); lean_dec(sid); lean_dec(mid); lean_dec(delta);
    lean_obj_res r = bloom_jsi_override_inst(eng, uiid, usid, umid, udel);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_revertInst___boxed(lean_obj_arg eng, lean_obj_arg iid, lean_obj_arg world) {
    (void)world;
    uint32_t uiid = lean_unbox_uint32(iid);
    lean_dec(iid);
    lean_obj_res r = bloom_jsi_revert_inst(eng, uiid);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_isInstOverridden___boxed(lean_obj_arg eng, lean_obj_arg iid, lean_obj_arg world) {
    (void)world;
    uint32_t uiid = lean_unbox_uint32(iid);
    lean_dec(iid);
    lean_obj_res r = bloom_jsi_is_inst_overridden(eng, uiid);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_setInstKelvin___boxed(lean_obj_arg eng, lean_obj_arg iid, lean_obj_arg k, lean_obj_arg world) {
    (void)world;
    uint32_t uiid = lean_unbox_uint32(iid), uk = lean_unbox_uint32(k);
    lean_dec(iid); lean_dec(k);
    lean_obj_res r = bloom_jsi_set_inst_kelvin(eng, uiid, uk);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_getInstFlowSlot___boxed(lean_obj_arg eng, lean_obj_arg iid, lean_obj_arg world) {
    (void)world;
    uint32_t uiid = lean_unbox_uint32(iid);
    lean_dec(iid);
    lean_obj_res r = bloom_jsi_get_inst_flow_slot(eng, uiid);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_getFlowChart___boxed(lean_obj_arg eng, lean_obj_arg fslot, lean_obj_arg world) {
    (void)world;
    uint32_t uf = lean_unbox_uint32(fslot);
    lean_dec(fslot);
    lean_obj_res r = bloom_jsi_get_flow_chart(eng, uf);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_getFlowMode___boxed(lean_obj_arg eng, lean_obj_arg fslot, lean_obj_arg world) {
    (void)world;
    uint32_t uf = lean_unbox_uint32(fslot);
    lean_dec(fslot);
    lean_obj_res r = bloom_jsi_get_flow_mode(eng, uf);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_swapChart___boxed(lean_obj_arg eng, lean_obj_arg fslot, lean_obj_arg tchart, lean_obj_arg mreq, lean_obj_arg world) {
    (void)world;
    uint32_t uf = lean_unbox_uint32(fslot), utc = lean_unbox_uint32(tchart);
    uint16_t um = (uint16_t)lean_unbox(mreq);
    lean_dec(fslot); lean_dec(tchart); lean_dec(mreq);
    lean_obj_res r = bloom_jsi_swap_chart(eng, uf, utc, um);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_setReg___boxed(lean_obj_arg eng, lean_obj_arg ch, lean_obj_arg val, lean_obj_arg world) {
    (void)world;
    uint32_t uch = lean_unbox_uint32(ch), uval = lean_unbox_uint32(val);
    lean_dec(ch); lean_dec(val);
    lean_obj_res r = bloom_jsi_set_reg(eng, uch, uval);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_setCameraOrbit___boxed(lean_obj_arg eng, lean_obj_arg yaw, lean_obj_arg pitch, lean_obj_arg dist, lean_obj_arg world) {
    (void)world;
    uint32_t uy = lean_unbox_uint32(yaw), up = lean_unbox_uint32(pitch), ud = lean_unbox_uint32(dist);
    lean_dec(yaw); lean_dec(pitch); lean_dec(dist);
    lean_obj_res r = bloom_jsi_set_camera(eng, uy, up, ud);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_dispatchPointer___boxed(lean_obj_arg eng, lean_obj_arg cx, lean_obj_arg cy, lean_obj_arg dx, lean_obj_arg down, lean_obj_arg clicked, lean_obj_arg world) {
    (void)world;
    uint32_t ucx = lean_unbox_uint32(cx), ucy = lean_unbox_uint32(cy), udx = lean_unbox_uint32(dx);
    uint8_t udown = (uint8_t)lean_unbox(down), uclick = (uint8_t)lean_unbox(clicked);
    lean_dec(cx); lean_dec(cy); lean_dec(dx); lean_dec(down); lean_dec(clicked);
    lean_obj_res r = bloom_jsi_pointer_event(eng, ucx, ucy, udx, udown, uclick);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_pollWindowInput___boxed(lean_obj_arg eng, lean_obj_arg world) {
    (void)world;
    lean_obj_res r = bloom_jsi_poll_window_input(eng);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_stepAndRender___boxed(lean_obj_arg eng, lean_obj_arg mreq, lean_obj_arg force, lean_obj_arg world) {
    (void)world;
    uint16_t um = (uint16_t)lean_unbox(mreq);
    uint8_t uforce = (uint8_t)lean_unbox(force);
    lean_dec(mreq); lean_dec(force);
    lean_obj_res r = bloom_jsi_step_and_render(eng, um, uforce);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_getSelectedInst___boxed(lean_obj_arg eng, lean_obj_arg world) {
    (void)world;
    lean_obj_res r = bloom_jsi_get_selected_inst(eng);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_getCapturedNode___boxed(lean_obj_arg eng, lean_obj_arg world) {
    (void)world;
    lean_obj_res r = bloom_jsi_get_captured_node(eng);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_getXRayPlaneQ16___boxed(lean_obj_arg eng, lean_obj_arg world) {
    (void)world;
    lean_obj_res r = bloom_jsi_get_xray_q16(eng);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_getDispatchStats___boxed(lean_obj_arg eng, lean_obj_arg world) {
    (void)world;
    lean_obj_res r = bloom_jsi_get_dispatch_stats(eng);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_sealAndVerifyArena___boxed(lean_obj_arg eng, lean_obj_arg world) {
    (void)world;
    lean_obj_res r = bloom_jsi_seal_and_verify(eng);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_exportPpm___boxed(lean_obj_arg eng, lean_obj_arg ppath, lean_obj_arg world) {
    (void)world;
    lean_obj_res r = bloom_jsi_export_ppm(eng, ppath);
    lean_dec(eng);
    lean_dec(ppath);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_frameHash___boxed(lean_obj_arg eng, lean_obj_arg world) {
    (void)world;
    lean_obj_res r = bloom_jsi_frame_hash(eng);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_runNativeWindow___boxed(lean_obj_arg eng, lean_obj_arg on_frame, lean_obj_arg world) {
    (void)world;
    lean_obj_res r = bloom_jsi_run_window(eng, on_frame);
    lean_dec(eng);
    return r;
}

LEAN_EXPORT lean_obj_res l_Bloom_verifySearuCore___boxed(lean_obj_arg seed, lean_obj_arg world) {
    (void)world;
    uint64_t useed = lean_unbox_uint64(seed);
    lean_dec(seed);
    return bloom_jsi_verify_searu_core(useed);
}

LEAN_EXPORT lean_obj_res l_Bloom_verifyGpuAlignment___boxed(lean_obj_arg eng, lean_obj_arg world) {
    (void)world;
    lean_obj_res r = bloom_jsi_verify_gpu_alignment(eng);
    lean_dec(eng);
    return r;
}
