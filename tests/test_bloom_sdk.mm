#include "bloom_sdk.h"
#include "bloom_vision.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

static void euler_to_quat(float yaw, float pitch, float roll, float out_q[4]) {
    float cy = cosf(yaw * 0.5f), sy = sinf(yaw * 0.5f);
    float cp = cosf(pitch * 0.5f), sp = sinf(pitch * 0.5f);
    float cr = cosf(roll * 0.5f), sr = sinf(roll * 0.5f);
    float px = sp * cr, py = -sp * sr, pz = cp * sr, pw = cp * cr;
    out_q[0] = cy * px + sy * pz;
    out_q[1] = sy * pw + cy * py;
    out_q[2] = cy * pz - sy * px;
    out_q[3] = cy * pw - sy * py;
}

static uint16_t register_torus(BloomScene *sc) {
    const uint32_t rings = 2u, segs = 4u, mcnt = rings * segs;
    std::vector<BloomMeshletHeader32> hdrs(mcnt);
    std::vector<BloomVtx12> verts(mcnt * BLOOM_CLUSTER_VTX);
    std::vector<uint8_t> lidx(84u * 3u);
    uint32_t it = 0u;
    for (uint32_t iy = 0u; iy < 6u; ++iy) {
        for (uint32_t ix = 0u; ix < 7u; ++ix) {
            uint8_t i0 = (uint8_t)(iy * 8u + ix), i1 = (uint8_t)(i0 + 1u);
            uint8_t i2 = (uint8_t)(i0 + 8u), i3 = (uint8_t)(i2 + 1u);
            lidx[it * 3u + 0u] = i0; lidx[it * 3u + 1u] = i2; lidx[it * 3u + 2u] = i1; ++it;
            lidx[it * 3u + 0u] = i1; lidx[it * 3u + 1u] = i2; lidx[it * 3u + 2u] = i3; ++it;
        }
    }
    for (uint32_t m = 0u; m < mcnt; ++m) {
        float u0 = ((float)(m % segs) / (float)segs) * 6.2831853f;
        float u1 = ((float)((m % segs) + 1u) / (float)segs) * 6.2831853f;
        float v0 = ((float)(m / segs) / (float)rings) * 6.2831853f;
        float v1 = ((float)((m / segs) + 1u) / (float)rings) * 6.2831853f;
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
                v.joint_id = (uint8_t)(m & 3u);
                cx += px; cy += py; cz += pz;
            }
        }
        BloomMeshletHeader32 h{};
        h.vtx_offset = m * BLOOM_CLUSTER_VTX;
        h.idx_offset = 0u;
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
    return bloom_scene_add_template(sc, "torus", mcnt, hdrs.data(), verts.data(),
                                    lidx.data(), (uint32_t)lidx.size());
}

static uint16_t register_box(BloomScene *sc, float hx, float hy, float hz) {
    std::vector<BloomVtx12> verts;
    std::vector<uint8_t> lidx;
    const float fv[6][4][3] = {
        {{ hx,-hy,-hz},{ hx, hy,-hz},{ hx, hy, hz},{ hx,-hy, hz}},
        {{-hx,-hy, hz},{-hx, hy, hz},{-hx, hy,-hz},{-hx,-hy,-hz}},
        {{-hx, hy,-hz},{-hx, hy, hz},{ hx, hy, hz},{ hx, hy,-hz}},
        {{-hx,-hy, hz},{-hx,-hy,-hz},{ hx,-hy,-hz},{ hx,-hy, hz}},
        {{-hx,-hy, hz},{ hx,-hy, hz},{ hx, hy, hz},{-hx, hy, hz}},
        {{ hx,-hy,-hz},{-hx,-hy,-hz},{-hx, hy,-hz},{ hx, hy,-hz}}
    };
    const float fn[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    for (int f = 0; f < 6; ++f) {
        uint8_t bi = (uint8_t)(f * 4);
        int8_t on[2];
        bloom_oct_encode(fn[f][0], fn[f][1], fn[f][2], on);
        for (int c = 0; c < 4; ++c) {
            BloomVtx12 v{};
            v.pos_unorm16[0] = bloom_pack_pos_unorm16(fv[f][c][0], BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
            v.pos_unorm16[1] = bloom_pack_pos_unorm16(fv[f][c][1], BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
            v.pos_unorm16[2] = bloom_pack_pos_unorm16(fv[f][c][2], BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
            v.oct_norm[0] = on[0]; v.oct_norm[1] = on[1];
            verts.push_back(v);
        }
        lidx.push_back(bi); lidx.push_back((uint8_t)(bi + 1u)); lidx.push_back((uint8_t)(bi + 2u));
        lidx.push_back(bi); lidx.push_back((uint8_t)(bi + 2u)); lidx.push_back((uint8_t)(bi + 3u));
    }
    BloomMeshletHeader32 h{};
    h.vtx_cnt = 24u; h.tri_cnt = 12u;
    h.bsphere_f16[3] = bloom_f32_to_f16(1.35f);
    h.cone_axis[2] = 127; h.cone_cutoff = 127;
    return bloom_scene_add_template(sc, "bevel_box", 1u, &h, verts.data(),
                                    lidx.data(), (uint32_t)lidx.size());
}

static uint16_t register_octa(BloomScene *sc, float rx, float ry, float rz) {
    std::vector<BloomVtx12> verts;
    std::vector<uint8_t> lidx;
    const float pv[6][3] = {{rx,0,0},{-rx,0,0},{0,ry,0},{0,-ry,0},{0,0,rz},{0,0,-rz}};
    const int tris[8][3] = {{0,2,4},{4,2,1},{1,2,5},{5,2,0},{4,3,0},{1,3,4},{5,3,1},{0,3,5}};
    for (int f = 0; f < 8; ++f) {
        const float *a = pv[tris[f][0]], *b = pv[tris[f][1]], *c = pv[tris[f][2]];
        float ux = b[0] - a[0], uy = b[1] - a[1], uz = b[2] - a[2];
        float vx = c[0] - a[0], vy = c[1] - a[1], vz = c[2] - a[2];
        float nx = uy * vz - uz * vy, ny = uz * vx - ux * vz, nz = ux * vy - uy * vx;
        if (nx * (a[0] + b[0] + c[0]) + ny * (a[1] + b[1] + c[1]) + nz * (a[2] + b[2] + c[2]) < 0.0f) {
            nx = -nx; ny = -ny; nz = -nz;
            const float *tmp = b; b = c; c = tmp;
        }
        float il = 1.0f / std::max(sqrtf(nx * nx + ny * ny + nz * nz), 1e-6f);
        int8_t on[2];
        bloom_oct_encode(nx * il, ny * il, nz * il, on);
        const float *pts[3] = {a, b, c};
        for (int k = 0; k < 3; ++k) {
            BloomVtx12 v{};
            v.pos_unorm16[0] = bloom_pack_pos_unorm16(pts[k][0], BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
            v.pos_unorm16[1] = bloom_pack_pos_unorm16(pts[k][1], BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
            v.pos_unorm16[2] = bloom_pack_pos_unorm16(pts[k][2], BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
            v.oct_norm[0] = on[0]; v.oct_norm[1] = on[1];
            verts.push_back(v);
            lidx.push_back((uint8_t)(f * 3 + k));
        }
    }
    BloomMeshletHeader32 h{};
    h.vtx_cnt = 24u; h.tri_cnt = 8u;
    h.bsphere_f16[3] = bloom_f32_to_f16(1.35f);
    h.cone_axis[2] = 127; h.cone_cutoff = 127;
    return bloom_scene_add_template(sc, "octa_prism", 1u, &h, verts.data(),
                                    lidx.data(), (uint32_t)lidx.size());
}

static void run_test1_ui_sdk_and_modal(BloomGpu *gpu) {
    BloomScene *sc = bloom_scene_create();
    uint16_t tid = register_torus(sc);
    BloomConstraintSpec cs{};
    cs.template_id = tid;
    cs.dof_min_f16[0] = bloom_f32_to_f16(0.0f);
    cs.dof_max_f16[0] = bloom_f32_to_f16(3.1415926f);
    float pos[3] = {2.0f, 0.0f, 5.0f}, quat[4] = {0, 0, 0, 1}, scale[3] = {0.75f, 0.75f, 0.75f};
    BloomHandle reactor = bloom_scene_spawn_object(sc, "reactor_core", tid, pos, quat, scale,
                                                   BLOOM_RGBA(0x48, 0xC0, 0xEC, 0xFF), &cs);
    assert(reactor != BLOOM_HANDLE_NULL);

    BloomUiNodeDesc d{};
    d.id = "sidebar"; d.kind = BLOOM_UI_PANEL; d.flex_dir = BLOOM_FLEX_COL;
    d.x = 20.0f; d.y = 20.0f; d.w = 560.0f; d.h = 1100.0f; d.pad = 16.0f; d.gap = 14.0f;
    d.fill_rgba8 = BLOOM_RGBA(0x16, 0x20, 0x2E, 0xFF);
    d.label = "Embodied Inspector";
    BloomHandle sidebar = bloom_scene_add_ui(sc, &d);

    BloomUiNodeDesc b1{};
    b1.id = "btn_ok"; b1.parent = sidebar; b1.kind = BLOOM_UI_BUTTON;
    b1.w = 0.0f; b1.h = 88.0f; b1.fill_rgba8 = BLOOM_RGBA(0x28, 0x4E, 0x7A, 0xFF);
    b1.label = "Confirm Target";
    BloomHandle btn_ok = bloom_scene_add_ui(sc, &b1);

    BloomUiNodeDesc b2{};
    b2.id = "btn_hidden"; b2.parent = sidebar; b2.kind = BLOOM_UI_BUTTON;
    b2.w = 260.0f; b2.h = 88.0f; b2.fill_rgba8 = BLOOM_RGBA(0x32, 0x5A, 0x86, 0xFF);
    b2.label = "Occluded";
    bloom_scene_add_ui(sc, &b2);

    BloomUiNodeDesc sl{};
    sl.id = "sl_partial"; sl.parent = sidebar; sl.kind = BLOOM_UI_SLIDER;
    sl.w = 0.0f; sl.h = 96.0f; sl.fill_rgba8 = BLOOM_RGBA(0x22, 0x34, 0x4C, 0xFF);
    sl.label = "Reactor Phase";
    sl.bind_obj_key = bloom_fnv1a64("reactor_core");
    sl.bind_dof_slot = 0u;
    BloomHandle sl_partial = bloom_scene_add_ui(sc, &sl);

    BloomUiNodeDesc bd{};
    bd.id = "btn_disabled"; bd.parent = sidebar; bd.kind = BLOOM_UI_BUTTON;
    bd.w = 0.0f; bd.h = 84.0f; bd.fill_rgba8 = BLOOM_RGBA(0x26, 0x2E, 0x3A, 0xFF);
    bd.label = "Locked";
    bd.disabled = true;
    bloom_scene_add_ui(sc, &bd);

    BloomUiNodeDesc vl{};
    vl.id = "tele_vlist"; vl.parent = sidebar; vl.kind = BLOOM_UI_VLIST;
    vl.w = 0.0f; vl.h = 340.0f; vl.fill_rgba8 = BLOOM_RGBA(0x14, 0x1B, 0x26, 0xFF);
    vl.vlist_total_rows = 1000u; vl.vlist_row_h = 34.0f;
    bloom_scene_add_ui(sc, &vl);

    /* Top-level modal covering btn_hidden 100% and sl_partial's center, leaving right portion visible */
    BloomUiNodeDesc md{};
    md.id = "modal_dlg"; md.kind = BLOOM_UI_MODAL; md.flex_dir = BLOOM_FLEX_COL;
    md.x = 10.0f; md.y = 158.0f; md.w = 320.0f; md.h = 230.0f; md.pad = 16.0f; md.gap = 12.0f;
    md.fill_rgba8 = BLOOM_RGBA(0x2B, 0x21, 0x3A, 0xFF);
    md.modal_trap = true; md.label = "Safety Modal";
    BloomHandle modal = bloom_scene_add_ui(sc, &md);

    BloomUiNodeDesc mb{};
    mb.id = "modal_btn"; mb.parent = modal; mb.kind = BLOOM_UI_BUTTON;
    mb.w = 0.0f; mb.h = 64.0f; mb.fill_rgba8 = BLOOM_RGBA(0x88, 0x34, 0x4A, 0xFF);
    mb.label = "Dismiss";
    BloomHandle modal_btn = bloom_scene_add_ui(sc, &mb);

    BloomSdkSnapshot snap{};
    assert(bloom_sdk_capture_snapshot(gpu, sc, 3840u, 2160u, &snap));
    BloomGpuStats gs = bloom_gpu_get_stats(gpu);
    assert(gs.grounding_active);
    assert(gs.max_tg_ground == 1024u);
    assert(snap.ground_ms > 0.0);

    const BloomSdkEntity *e_hidden = bloom_sdk_find_by_id(&snap, "btn_hidden");
    const BloomSdkEntity *e_part   = bloom_sdk_find_by_id(&snap, "sl_partial");
    const BloomSdkEntity *e_dis    = bloom_sdk_find_by_id(&snap, "btn_disabled");
    const BloomSdkEntity *e_mbtn   = bloom_sdk_find_by_id(&snap, "modal_btn");
    assert(e_hidden && e_part && e_dis && e_mbtn);

    assert(e_hidden->visible_px == 0u);
    assert(e_hidden->occluded == true);
    assert(e_hidden->actionable == false);
    assert(!bloom_sdk_dispatch_action(sc, &snap, "btn_hidden", BLOOM_SDK_ACT_CLICK, 0.0f));

    assert(e_dis->actionable == false);
    assert(!bloom_sdk_dispatch_action(sc, &snap, "btn_disabled", BLOOM_SDK_ACT_CLICK, 0.0f));

    assert(!e_part->occluded);
    assert(e_part->visible_ratio > 0.10f && e_part->visible_ratio < 0.90f);
    assert(e_part->actionable == false); /* Blocked by modal trap */
    assert(!bloom_sdk_dispatch_action(sc, &snap, "sl_partial", BLOOM_SDK_ACT_SET_VALUE, 0.8f));

    /* Center of sl_partial sits under modal_dlg; GPU anchor sits in exposed right segment */
    BloomSdkEntity center_pick{}, anchor_pick{};
    uint32_t cx = (uint32_t)(e_part->layout_rect[0] + e_part->layout_rect[2] * 0.5f);
    uint32_t cy = (uint32_t)(e_part->layout_rect[1] + e_part->layout_rect[3] * 0.5f);
    assert(bloom_sdk_pick_at(gpu, sc, 3840u, 2160u, cx, cy, &center_pick));
    assert(strcmp(center_pick.id, "modal_dlg") == 0);
    assert(bloom_sdk_pick_at(gpu, sc, 3840u, 2160u, (uint32_t)e_part->anchor_x, (uint32_t)e_part->anchor_y, &anchor_pick));
    assert(strcmp(anchor_pick.id, "sl_partial") == 0);

    assert(e_mbtn->actionable == true);
    /* Tab roving focus stays trapped inside modal */
    BloomInputEvent tab{};
    tab.type = BLOOM_EVENT_KEY_DOWN;
    tab.key_code = 48u;
    assert(bloom_scene_handle_input(sc, &tab));
    assert(bloom_scene_get_focused_ui(sc) == modal_btn);

    /* Dismiss modal -> background becomes unoccluded & actionable */
    assert(bloom_scene_destroy_ui(sc, modal));
    BloomSdkSnapshot snap2{};
    assert(bloom_sdk_capture_snapshot(gpu, sc, 3840u, 2160u, &snap2));
    const BloomSdkEntity *e_hidden2 = bloom_sdk_find_by_id(&snap2, "btn_hidden");
    const BloomSdkEntity *e_part2   = bloom_sdk_find_by_id(&snap2, "sl_partial");
    assert(e_hidden2 && !e_hidden2->occluded && e_hidden2->actionable);
    assert(e_part2 && e_part2->actionable);

    assert(bloom_sdk_dispatch_action(sc, &snap2, "sl_partial", BLOOM_SDK_ACT_SET_VALUE, 0.8f));
    assert(fabsf(bloom_scene_get_ui_slider(sc, sl_partial) - 0.8f) < 0.03f);
    assert(bloom_sdk_dispatch_action(sc, &snap2, "btn_ok", BLOOM_SDK_ACT_CLICK, 0.0f));
    assert(bloom_scene_get_ui_toggle(sc, btn_ok) == true);

    uint32_t q_idxs[32];
    uint32_t q_cnt = bloom_sdk_query(&snap2, 1u << BLOOM_GROUND_UI, BLOOM_UI_BUTTON, true, q_idxs, 32u);
    assert(q_cnt >= 2u); /* btn_ok + btn_hidden; btn_disabled excluded */

    BloomObjectMeta om{};
    assert(bloom_scene_get_object_meta(sc, 1u, &om));
    assert(om.local_dof[0] > 2.3f);

    std::vector<char> jbuf(65536u);
    size_t jlen = bloom_sdk_snapshot_to_json(&snap2, jbuf.data(), jbuf.size());
    assert(jlen > 100u && jlen < jbuf.size());
    assert(strstr(jbuf.data(), "\"sl_partial\"") != nullptr);
    assert(strstr(jbuf.data(), "\"reactor_core\"") != nullptr);
    assert(strstr(jbuf.data(), "\"anchor\"") != nullptr);

    printf("  [PASS] Test 1 (4K UI SDK + Modal Trap): ground=%.3f ms | hidden_px=0 -> visible=%u | sl_ratio=%.2f -> t=%.2f\n",
           snap.ground_ms, e_hidden2->visible_px, e_part->visible_ratio, bloom_scene_get_ui_slider(sc, sl_partial));

    bloom_sdk_free_snapshot(&snap);
    bloom_sdk_free_snapshot(&snap2);
    bloom_scene_destroy(sc);
}

static void run_test2_3d_pixel_pick(BloomGpu *gpu) {
    BloomScene *sc = bloom_scene_create();
    BloomCamera cam{{0.0f, 0.0f, -4.0f}, 0.0f, 0.0f, 1.8f, 0.5f, 80.0f};
    bloom_scene_set_camera(sc, &cam);

    uint16_t tid = register_torus(sc);
    BloomConstraintSpec cs{};
    cs.template_id = tid;
    float p1[3] = {-0.9f, 0.2f, 0.5f}, q1[4] = {0, 0, 0, 1}, s1[3] = {0.55f, 0.55f, 0.55f};
    BloomHandle h_reactor = bloom_scene_spawn_object(sc, "reactor_core", tid, p1, q1, s1,
                                                     BLOOM_RGBA(0x4C, 0xC4, 0xF0, 0xFF), &cs);

    BloomAnalyticPrim32 sp{};
    sp.center[0] = 1.1f; sp.center[1] = -0.1f; sp.center[2] = 0.2f;
    sp.radius = 0.55f;
    sp.rgba8 = BLOOM_RGBA(0xFF, 0x80, 0x40, 0xFF);
    sp.prim_kind_flags = 1u;
    bloom_scene_spawn_analytic(sc, "sensor_probe_0", &sp);

    BloomSdkSnapshot snap{};
    assert(bloom_sdk_capture_snapshot(gpu, sc, 1280u, 720u, &snap));

    const BloomSdkEntity *er = bloom_sdk_find_by_id(&snap, "reactor_core");
    const BloomSdkEntity *ep = bloom_sdk_find_by_id(&snap, "sensor_probe_0");
    assert(er && ep);
    assert(er->visible_px > 200u && !er->occluded && er->actionable);
    assert(ep->visible_px > 200u && !ep->occluded && ep->actionable);

    BloomSdkEntity pk_mesh{}, pk_ana{};
    assert(bloom_sdk_pick_at(gpu, sc, 1280u, 720u, (uint32_t)er->anchor_x, (uint32_t)er->anchor_y, &pk_mesh));
    assert(pk_mesh.domain == BLOOM_GROUND_MESH);
    assert(pk_mesh.handle == h_reactor);
    float d_mesh = sqrtf((pk_mesh.world_pos[0] - p1[0]) * (pk_mesh.world_pos[0] - p1[0]) +
                         (pk_mesh.world_pos[1] - p1[1]) * (pk_mesh.world_pos[1] - p1[1]) +
                         (pk_mesh.world_pos[2] - p1[2]) * (pk_mesh.world_pos[2] - p1[2]));
    assert(d_mesh < 0.80f);

    assert(bloom_sdk_pick_at(gpu, sc, 1280u, 720u, (uint32_t)ep->anchor_x, (uint32_t)ep->anchor_y, &pk_ana));
    assert(pk_ana.domain == BLOOM_GROUND_ANALYTIC);
    assert(pk_ana.key == bloom_fnv1a64("sensor_probe_0"));
    float r_hit = sqrtf((pk_ana.world_pos[0] - sp.center[0]) * (pk_ana.world_pos[0] - sp.center[0]) +
                        (pk_ana.world_pos[1] - sp.center[1]) * (pk_ana.world_pos[1] - sp.center[1]) +
                        (pk_ana.world_pos[2] - sp.center[2]) * (pk_ana.world_pos[2] - sp.center[2]));
    assert(fabsf(r_hit - sp.radius) < 0.08f);

    printf("  [PASS] Test 2 (3D Pixel Pick): mesh_err=%.3f m (part=%u) | sphere_surf_err=%.4f m\n",
           d_mesh, pk_mesh.part_id, fabsf(r_hit - sp.radius));

    bloom_sdk_free_snapshot(&snap);
    bloom_scene_destroy(sc);
}

static void run_test3_vision_localization(BloomGpu *gpu) {
    BloomScene *sc_tpl = bloom_scene_create();
    uint16_t t0 = register_torus(sc_tpl);
    uint16_t t1 = register_box(sc_tpl, 0.92f, 0.45f, 0.65f);
    uint16_t t2 = register_octa(sc_tpl, 1.05f, 1.15f, 0.90f);
    assert(t0 == 0u && t1 == 1u && t2 == 2u);

    BloomVision *vis = bloom_vision_create("shaders/bloom_vision.metal");
    assert(vis != nullptr);
    assert(bloom_vision_get_max_tg(vis) == 1024u);
    assert(bloom_vision_register_from_scene(vis, sc_tpl, t0, "torus"));
    assert(bloom_vision_register_from_scene(vis, sc_tpl, t1, "bevel_box"));
    assert(bloom_vision_register_from_scene(vis, sc_tpl, t2, "octa_prism"));

    struct GtPose {
        uint16_t tid;
        const char *name;
        float pos[3];
        float yaw, pitch, roll, dof0;
    };
    const GtPose gts[3] = {
        {0u, "torus",      { 0.18f, -0.10f, 4.15f},  0.52f, -0.28f,  0.14f, 0.85f},
        {1u, "bevel_box",  {-0.16f,  0.14f, 4.35f}, -0.44f,  0.36f, -0.21f, 0.00f},
        {2u, "octa_prism", { 0.12f,  0.10f, 4.05f},  0.35f,  0.42f,  0.18f, 0.00f}
    };

    BloomCamera cam{{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 2.0f, 0.5f, 80.0f};
    const uint32_t VW = 256u, VH = 256u;
    std::vector<uint8_t> rgba(VW * VH * 4u);
    std::vector<double> gpu_times;
    uint32_t correct = 0u;
    double sum_rot_deg = 0.0, sum_trans = 0.0;
    bool warmed = false;

    for (int g = 0; g < 3; ++g) {
        BloomScene *sc_obs = bloom_scene_create();
        register_torus(sc_obs);
        register_box(sc_obs, 0.92f, 0.45f, 0.65f);
        register_octa(sc_obs, 1.05f, 1.15f, 0.90f);
        bloom_scene_set_camera(sc_obs, &cam);

        BloomConstraintSpec cs{};
        cs.template_id = gts[g].tid;
        cs.dof_min_f16[0] = bloom_f32_to_f16(0.0f);
        cs.dof_max_f16[0] = bloom_f32_to_f16(3.1415926f);
        float gt_q[4], ones[3] = {1.0f, 1.0f, 1.0f};
        euler_to_quat(gts[g].yaw, gts[g].pitch, gts[g].roll, gt_q);
        BloomHandle oh = bloom_scene_spawn_object(sc_obs, gts[g].name, gts[g].tid,
                                                  gts[g].pos, gt_q, ones,
                                                  BLOOM_RGBA(0x8C, 0xD9, 0xF5, 0xFF), &cs);
        bloom_scene_set_object_dof(sc_obs, oh, 0u, gts[g].dof0);
        assert(bloom_gpu_render_headless(gpu, sc_obs, VW, VH, true));
        uint32_t rw = 0u, rh = 0u;
        assert(bloom_gpu_readback_headless_rgba8(gpu, rgba.data(), (uint32_t)rgba.size(), &rw, &rh));
        assert(rw == VW && rh == VH);

        /* Inject sensor noise + distractor bar */
        std::mt19937 rng(100u + (uint32_t)g);
        std::normal_distribution<float> gn(0.0f, 6.0f);
        for (size_t i = 0u; i < VW * VH; ++i) {
            for (int c = 0; c < 3; ++c) {
                float v = (float)rgba[i * 4u + c] + gn(rng);
                rgba[i * 4u + c] = (uint8_t)std::max(0.0f, std::min(255.0f, v));
            }
        }
        for (uint32_t x = 10u; x < 42u; ++x) {
            size_t idx = (24u * VW + x) * 4u;
            rgba[idx] = rgba[idx + 1u] = rgba[idx + 2u] = 190u;
        }

        BloomVisionMatch match{};
        if (!warmed) {
            for (int w = 0; w < 3; ++w) {
                bloom_vision_localize_rgba8(vis, rgba.data(), VW, VH, &cam, nullptr, 0.0f, &match);
            }
            warmed = true;
        }
        assert(bloom_vision_localize_rgba8(vis, rgba.data(), VW, VH, &cam, nullptr, 0.0f, &match));

        float dot_q = fabsf(match.quat[0] * gt_q[0] + match.quat[1] * gt_q[1] +
                            match.quat[2] * gt_q[2] + match.quat[3] * gt_q[3]);
        float rot_err_deg = 2.0f * acosf(std::min(1.0f, dot_q)) * (180.0f / 3.1415926f);
        float t_err = sqrtf((match.pos[0] - gts[g].pos[0]) * (match.pos[0] - gts[g].pos[0]) +
                            (match.pos[1] - gts[g].pos[1]) * (match.pos[1] - gts[g].pos[1]) +
                            (match.pos[2] - gts[g].pos[2]) * (match.pos[2] - gts[g].pos[2]));
        if (match.template_id == gts[g].tid) correct++;
        sum_rot_deg += rot_err_deg;
        sum_trans += t_err;
        gpu_times.push_back(match.gpu_ms);

        /* Verify BloomStateProjection round-trip into tracked BloomScene object */
        BloomHandle tracked = bloom_scene_find_object_by_key(sc_obs, bloom_fnv1a64(gts[g].name));
        assert(tracked != BLOOM_HANDLE_NULL);
        BloomStateProjection proj = match.projection;
        proj.node_key = bloom_fnv1a64(gts[g].name);
        bloom_scene_apply_projections(sc_obs, &proj, 1u);
        bloom_scene_sync_transforms(sc_obs);
        BloomObjectState48 st{};
        assert(bloom_scene_get_object_state(sc_obs, tracked, &st));
        assert(fabsf(st.pos[0] - match.pos[0]) < 1e-4f);
        assert(fabsf(st.pos[2] - match.pos[2]) < 1e-4f);

        printf("    GT %-10s -> pred=%-10s | iou=%.3f dt=%.2f lum=%.3f | t_err=%.3f m | rot_err=%.2f deg | GPU=%.3f ms\n",
               gts[g].name, match.template_name, match.iou, match.edge_err, match.lum_err,
               t_err, rot_err_deg, match.gpu_ms);

        bloom_scene_destroy(sc_obs);
    }

    std::sort(gpu_times.begin(), gpu_times.end());
    double med_ms = gpu_times[gpu_times.size() / 2u];
    double mean_rot = sum_rot_deg / 3.0;
    double mean_trans = sum_trans / 3.0;

    printf("  [PASS] Test 3 (GPU Vision Localization): cls=%u/3 | mean_rot=%.2f deg | mean_t=%.4f m | med_gpu=%.3f ms\n",
           correct, mean_rot, mean_trans, med_ms);

    assert(correct == 3u);
    assert(mean_rot < 12.0);
    assert(mean_trans < 0.15);
    assert(med_ms < 3.0);

    bloom_vision_destroy(vis);
    bloom_scene_destroy(sc_tpl);
}

int main() {
    printf("=== BLOOM Embodied UI SDK & GPU Visual Localization Hardware CI ===\n");
    BloomGpu *gpu = bloom_gpu_create(nullptr, "shaders/bloom_core.metal");
    assert(gpu != nullptr);

    run_test1_ui_sdk_and_modal(gpu);
    run_test2_3d_pixel_pick(gpu);
    run_test3_vision_localization(gpu);

    bloom_gpu_destroy(gpu);
    printf("ALL EMBODIED SDK & VISION HARDWARE CI TESTS PASSED!\n");
    return 0;
}
