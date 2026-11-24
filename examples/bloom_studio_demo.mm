#include "bloom_gpu.h"
#include "bloom_host.h"
#include "bloom_scene.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void build_torus_mesh(BloomScene *sc) {
    const uint32_t rings = 4u, mcnt = rings * 4u;
    std::vector<BloomMeshletHeader32> hdrs(mcnt);
    std::vector<BloomVtx12> verts(mcnt * BLOOM_CLUSTER_VTX);
    std::vector<uint8_t> lidx(84u * 3u);

    uint32_t it = 0u;
    for (uint32_t iy = 0u; iy < 6u; ++iy) {
        for (uint32_t ix = 0u; ix < 7u; ++ix) {
            uint8_t i0 = (uint8_t)(iy * 8u + ix);
            uint8_t i1 = (uint8_t)(i0 + 1u);
            uint8_t i2 = (uint8_t)(i0 + 8u);
            uint8_t i3 = (uint8_t)(i2 + 1u);
            lidx[it * 3u + 0u] = i0; lidx[it * 3u + 1u] = i2; lidx[it * 3u + 2u] = i1;
            ++it;
            lidx[it * 3u + 0u] = i1; lidx[it * 3u + 1u] = i2; lidx[it * 3u + 2u] = i3;
            ++it;
        }
    }

    for (uint32_t m = 0u; m < mcnt; ++m) {
        float u0 = ((float)(m % 4u) / 4.0f) * 6.2831853f;
        float u1 = ((float)((m % 4u) + 1u) / 4.0f) * 6.2831853f;
        float v0 = ((float)(m / 4u) / (float)rings) * 6.2831853f;
        float v1 = ((float)((m / 4u) + 1u) / (float)rings) * 6.2831853f;
        float cx = 0.0f, cy = 0.0f, cz = 0.0f;
        for (uint32_t iy = 0u; iy < 7u; ++iy) {
            float phi = v0 + ((float)iy / 6.0f) * (v1 - v0);
            for (uint32_t ix = 0u; ix < 8u; ++ix) {
                float th = u0 + ((float)ix / 7.0f) * (u1 - u0);
                float px = (0.85f + 0.35f * cosf(phi)) * cosf(th);
                float py = 0.35f * sinf(phi);
                float pz = (0.85f + 0.35f * cosf(phi)) * sinf(th);
                float nx = cosf(phi) * cosf(th), ny = sinf(phi), nz = cosf(phi) * sinf(th);
                BloomVtx12 &v = verts[m * BLOOM_CLUSTER_VTX + iy * 8u + ix];
                v.pos_unorm16[0] = bloom_pack_pos_unorm16(px, BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
                v.pos_unorm16[1] = bloom_pack_pos_unorm16(py, BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
                v.pos_unorm16[2] = bloom_pack_pos_unorm16(pz, BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
                bloom_oct_encode(nx, ny, nz, v.oct_norm);
                v.uv_u8[0] = (uint8_t)ix; v.uv_u8[1] = (uint8_t)iy;
                v.joint_id = (uint8_t)(m & 3u); v.mat_id = 0u;
                cx += px; cy += py; cz += pz;
            }
        }
        BloomMeshletHeader32 h{};
        h.vtx_offset = m * BLOOM_CLUSTER_VTX;
        h.vtx_cnt = 56u;
        h.tri_cnt = 84u;
        h.bsphere_f16[0] = bloom_f32_to_f16(cx / 56.0f);
        h.bsphere_f16[1] = bloom_f32_to_f16(cy / 56.0f);
        h.bsphere_f16[2] = bloom_f32_to_f16(cz / 56.0f);
        h.bsphere_f16[3] = bloom_f32_to_f16(1.35f);
        h.cone_axis[2] = 127;
        h.cone_cutoff = 127;
        hdrs[m] = h;
    }
    bloom_scene_add_template(sc, "torus_reactor", mcnt, hdrs.data(), verts.data(),
                             lidx.data(), (uint32_t)lidx.size());
}

typedef struct {
    BloomScene *scene;
    BloomGpu *gpu;
    BloomHandle ui_fps;
    double elapsed;
    uint32_t frames;
} DemoApp;

static void populate_demo_scene(DemoApp *app) {
    BloomScene *sc = app->scene;
    build_torus_mesh(sc);

    BloomCamera cam = {{0.0f, 0.2f, -1.5f}, 0.0f, 0.05f, 1.85f, 0.5f, 80.0f};
    bloom_scene_set_camera(sc, &cam);

    float core_pos[3] = {0.6f, 0.0f, 7.5f};
    float core_sc[3] = {1.35f, 1.35f, 1.35f};
    BloomHandle core = bloom_scene_spawn_object(sc, "reactor_core", 0u, core_pos, nullptr, core_sc,
                                                BLOOM_RGBA(0x40, 0xD8, 0xF0, 0xFF), nullptr);
    float cut[4] = {0.707f, 0.0f, 0.707f, 0.0f};
    bloom_scene_set_object_cutaway(sc, core, true, cut);

    float ring_pos[3] = {2.35f, 0.0f, 0.0f};
    float ring_sc[3] = {0.42f, 0.42f, 0.42f};
    BloomHandle sat = bloom_scene_spawn_object(sc, "orbital_ring", 0u, ring_pos, nullptr, ring_sc,
                                               BLOOM_RGBA(0xFF, 0x98, 0x38, 0xFF), nullptr);
    bloom_scene_set_parent(sc, sat, core);

    for (uint32_t i = 0u; i < 8u; ++i) {
        char idb[32];
        snprintf(idb, sizeof(idb), "sensor_probe_%u", i);
        float a = (float)i * (6.2831853f / 8.0f);
        BloomAnalyticPrim32 pr{{0.6f + cosf(a) * 2.4f, sinf(a) * 1.4f, 7.5f + sinf(a) * 2.4f},
                               0.22f, {0, bloom_f32_to_f16(1.0f), 0, 0},
                               (i & 1u) ? BLOOM_RGBA(0x38, 0xFF, 0xB8, 0xFF) : BLOOM_RGBA(0xFF, 0x50, 0x90, 0xFF), 1u};
        bloom_scene_spawn_analytic(sc, idb, &pr);
    }

    BloomUiNodeDesc panel{};
    panel.id = "studio_sidebar"; panel.kind = BLOOM_UI_PANEL; panel.flex_dir = BLOOM_FLEX_COL;
    panel.x = 28.0f; panel.y = 28.0f; panel.w = 340.0f; panel.h = 620.0f;
    panel.pad = 14.0f; panel.gap = 10.0f;
    panel.fill_rgba8 = BLOOM_RGBA(0x10, 0x17, 0x22, 0xE6); panel.border_rgba8 = BLOOM_RGBA(0x2D, 0x44, 0x5E, 0xAA);
    panel.radius_px = 14.0f; panel.border_px = 1.5f;
    panel.shadow_blur_px = 18.0f; panel.shadow_rgba8 = BLOOM_RGBA(0x00, 0x00, 0x00, 0x90);
    panel.label = "BLOOM vNext Studio";
    BloomHandle hp = bloom_scene_add_ui(sc, &panel);

    BloomUiNodeDesc fps_lbl{};
    fps_lbl.id = "fps_readout"; fps_lbl.parent = hp; fps_lbl.kind = BLOOM_UI_TEXT; fps_lbl.h = 34.0f;
    fps_lbl.fill_rgba8 = BLOOM_RGBA(0x00, 0x00, 0x00, 0x00); fps_lbl.label = "Initializing M1 TBDR..."; fps_lbl.font_px = 14.0f;
    fps_lbl.text_rgba8 = BLOOM_RGBA(0x38, 0xD0, 0xFF, 0xFF);
    app->ui_fps = bloom_scene_add_ui(sc, &fps_lbl);

    BloomUiNodeDesc s1{};
    s1.id = "dof_slider"; s1.parent = hp; s1.kind = BLOOM_UI_SLIDER; s1.h = 56.0f;
    s1.fill_rgba8 = BLOOM_RGBA(0x1A, 0x25, 0x35, 0xFF); s1.radius_px = 8.0f; s1.label = "Reactor Articulation DOF";
    s1.font_px = 14.0f; s1.bind_obj_key = bloom_fnv1a64("reactor_core"); s1.bind_dof_slot = 0u;
    bloom_scene_add_ui(sc, &s1);

    BloomUiNodeDesc s2{};
    s2.id = "clip_slider"; s2.parent = hp; s2.kind = BLOOM_UI_SLIDER; s2.h = 56.0f;
    s2.fill_rgba8 = BLOOM_RGBA(0x1A, 0x25, 0x35, 0xFF); s2.radius_px = 8.0f; s2.label = "X-Ray Cutaway Plane";
    s2.font_px = 14.0f; s2.bind_obj_key = bloom_fnv1a64("reactor_core"); s2.bind_dof_slot = 3u;
    bloom_scene_add_ui(sc, &s2);

    BloomUiNodeDesc btn{};
    btn.id = "cut_btn"; btn.parent = hp; btn.kind = BLOOM_UI_BUTTON; btn.h = 44.0f;
    btn.fill_rgba8 = BLOOM_RGBA(0x22, 0x34, 0x4C, 0xFF); btn.border_rgba8 = BLOOM_RGBA(0x38, 0xD0, 0xFF, 0xFF); btn.border_px = 1.0f;
    btn.radius_px = 8.0f; btn.label = "Toggle HW [[clip_distance]]"; btn.font_px = 14.0f;
    btn.bind_obj_key = bloom_fnv1a64("reactor_core");
    bloom_scene_add_ui(sc, &btn);

    BloomUiNodeDesc vl{};
    vl.id = "tele_list"; vl.parent = hp; vl.kind = BLOOM_UI_VLIST; vl.h = 300.0f;
    vl.fill_rgba8 = BLOOM_RGBA(0x0C, 0x12, 0x1A, 0xFF); vl.border_rgba8 = BLOOM_RGBA(0x23, 0x31, 0x44, 0x80); vl.border_px = 1.0f;
    vl.radius_px = 8.0f; vl.vlist_total_rows = 2000u; vl.vlist_row_h = 26.0f; vl.font_px = 12.0f;
    bloom_scene_add_ui(sc, &vl);

    BloomAnimTrack spin{bloom_fnv1a64("reactor_core"), BLOOM_ANIM_ROT_Y, 0.0f, 8.0f, 0.0f, 6.2831853f,
                        BLOOM_EASE_LINEAR, BLOOM_LOOP_REPEAT};
    bloom_timeline_add_track(sc, &spin);
    bloom_timeline_set_playing(sc, true);
}

static void step_simulation_projection(DemoApp *app, double dt) {
    app->elapsed += dt;
    /* Demonstrate Layer 0 unidirectional projection from an external simulation step */
    BloomStateProjection p{};
    p.node_key = bloom_fnv1a64("orbital_ring");
    float a = (float)(app->elapsed * 1.4);
    p.pos[0] = cosf(a) * 2.2f;
    p.pos[1] = sinf(a * 2.0f) * 0.45f;
    p.pos[2] = sinf(a) * 2.2f;
    p.field_mask = BLOOM_PROJ_POS;
    bloom_scene_apply_projections(app->scene, &p, 1u);
}

static void cb_init(void *ud, void *layer, uint32_t vp_w, uint32_t vp_h) {
    (void)vp_w; (void)vp_h;
    DemoApp *app = (DemoApp *)ud;
    app->gpu = bloom_gpu_create(layer, "shaders/bloom_core.metal");
    app->scene = bloom_scene_create();
    populate_demo_scene(app);
}

static void cb_frame(void *ud, double dt, uint32_t vp_w, uint32_t vp_h) {
    DemoApp *app = (DemoApp *)ud;
    step_simulation_projection(app, dt);
    bloom_timeline_step(app->scene, (float)dt);
    if ((app->frames % 10u) == 0u) {
        BloomGpuStats st = bloom_gpu_get_stats(app->gpu);
        char msg[96];
        snprintf(msg, sizeof(msg), "GPU %.3f ms | Mesh %u/%u | CBs %llu",
                 st.last_gpu_ms, st.surviving_meshlets, st.total_meshlets,
                 (unsigned long long)st.submitted_cbs);
        bloom_scene_set_ui_label(app->scene, app->ui_fps, msg);
    }
    bloom_gpu_render_drawable(app->gpu, app->scene, vp_w, vp_h, false);
    app->frames++;
}

static void cb_input(void *ud, const BloomInputEvent *ev) {
    DemoApp *app = (DemoApp *)ud;
    if (app && app->scene) bloom_scene_handle_input(app->scene, ev);
}

static void cb_shutdown(void *ud) {
    DemoApp *app = (DemoApp *)ud;
    bloom_scene_destroy(app->scene);
    bloom_gpu_destroy(app->gpu);
}

int main(int argc, char **argv) {
    uint32_t hl_frames = 0u;
    uint32_t w = 2560u, h = 1440u;
    const char *ppm = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless-frames") == 0 && i + 1 < argc) hl_frames = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--w") == 0 && i + 1 < argc) w = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--h") == 0 && i + 1 < argc) h = (uint32_t)atoi(argv[++i]);
        else if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc) ppm = argv[++i];
    }

    DemoApp app{};
    if (hl_frames > 0u) {
        app.gpu = bloom_gpu_create(nullptr, "shaders/bloom_core.metal");
        app.scene = bloom_scene_create();
        populate_demo_scene(&app);
        for (uint32_t f = 0u; f < hl_frames; ++f) {
            step_simulation_projection(&app, 0.016);
            bloom_timeline_step(app.scene, 0.016f);
            if (f > 0u) {
                BloomGpuStats stf = bloom_gpu_get_stats(app.gpu);
                char msg[96];
                snprintf(msg, sizeof(msg), "GPU %.3f ms | Mesh %u/%u | CBs %llu",
                         stf.last_gpu_ms, stf.surviving_meshlets, stf.total_meshlets,
                         (unsigned long long)stf.submitted_cbs);
                bloom_scene_set_ui_label(app.scene, app.ui_fps, msg);
            }
            bloom_gpu_render_headless(app.gpu, app.scene, w, h, true);
        }
        BloomGpuStats st = bloom_gpu_get_stats(app.gpu);
        uint32_t chk = bloom_gpu_headless_checksum(app.gpu);
        printf("[bloom_studio_demo] Headless %ux%u x%u frames: GPU=%.3f ms | CPU pack=%.1f us | WC=%.1f KB | chk=0x%08X\n",
               w, h, hl_frames, st.last_gpu_ms, st.last_cpu_pack_us, (double)st.last_wc_bytes / 1024.0, chk);
        if (ppm && bloom_gpu_save_headless_ppm(app.gpu, ppm)) {
            printf("[bloom_studio_demo] Saved PPM snapshot -> %s\n", ppm);
        }
        bloom_scene_destroy(app.scene);
        bloom_gpu_destroy(app.gpu);
        return (chk != 0u) ? 0 : 1;
    }

    BloomHostCallbacks cbs{cb_init, cb_frame, cb_input, cb_shutdown, &app};
    return bloom_host_run_cocoa("BLOOM vNext Studio (Cocoa + Metal 3)", 1280u, 720u, &cbs);
}
