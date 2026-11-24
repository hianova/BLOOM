#include "bloom_gpu.h"
#include "bloom_scene.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static void build_torus_template(BloomScene *sc, uint32_t rings) {
    uint32_t mcnt = rings * 4u;
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
    uint16_t tid = bloom_scene_add_template(sc, "torus_reactor", mcnt, hdrs.data(), verts.data(),
                                            lidx.data(), (uint32_t)lidx.size());
    assert(tid == 0u);
}

int main() {
    printf("=== BLOOM vNext Apple M1 7-Gate Hardware CI Verification (4K 3840x2160) ===\n");
    BloomGpu *gpu = bloom_gpu_create(nullptr, "shaders/bloom_core.metal");
    assert(gpu != nullptr);

    BloomGpuStats st0 = bloom_gpu_get_stats(gpu);
    printf("  Gate 2 Occupancy: k_cull_meshlets=%u k_tex_checksum=%u (required: 1024)\n",
           st0.max_tg_cull, st0.max_tg_checksum);
    assert(st0.max_tg_cull == 1024u);
    assert(st0.max_tg_checksum == 1024u);
    assert(st0.memoryless_depth_active && st0.private_opt_atlas_active);

    BloomScene *sc = bloom_scene_create();
    build_torus_template(sc, 2u); /* 8 meshlets = 672 tris per object */

    for (uint32_t i = 0u; i < 256u; ++i) {
        char idb[32];
        snprintf(idb, sizeof(idb), "reactor_%03u", i);
        float pos[3] = {((float)(i % 16u) - 7.5f) * 1.15f, ((float)(i / 16u) - 7.5f) * 0.65f, 11.0f + (float)(i & 7u) * 0.8f};
        float sc3[3] = {0.42f, 0.42f, 0.42f};
        BloomHandle h = bloom_scene_spawn_object(sc, idb, 0u, pos, nullptr, sc3, BLOOM_RGBA(0x40, 0xD8, 0xF0, 0xFF), nullptr);
        if (i % 5u == 0u) {
            float plane[4] = {0.707f, 0.0f, 0.707f, 0.05f};
            bloom_scene_set_object_cutaway(sc, h, true, plane);
        }
    }
    for (uint32_t i = 0u; i < 32u; ++i) {
        char idb[32];
        snprintf(idb, sizeof(idb), "probe_%02u", i);
        BloomAnalyticPrim32 pr{{((float)(i % 8u) - 3.5f) * 1.8f, ((float)(i / 8u) - 1.5f) * 1.5f, 9.5f},
                               0.32f, {0, bloom_f32_to_f16(1.0f), 0, 0}, BLOOM_RGBA(0xFF, 0x88, 0x38, 0xFF), 1u};
        bloom_scene_spawn_analytic(sc, idb, &pr);
    }

    BloomUiNodeDesc hud_root{};
    hud_root.id = "hud_panel"; hud_root.kind = BLOOM_UI_PANEL; hud_root.flex_dir = BLOOM_FLEX_COL;
    hud_root.x = 32.0f; hud_root.y = 32.0f; hud_root.w = 480.0f; hud_root.h = 720.0f;
    hud_root.pad = 16.0f; hud_root.gap = 10.0f; hud_root.fill_rgba8 = BLOOM_RGBA(0x12, 0x19, 0x24, 0xE8);
    hud_root.radius_px = 14.0f; hud_root.shadow_blur_px = 16.0f; hud_root.shadow_rgba8 = BLOOM_RGBA(0x00, 0x00, 0x00, 0x90);
    hud_root.label = "BLOOM vNext 4K Studio Gate";
    BloomHandle hp = bloom_scene_add_ui(sc, &hud_root);
    for (int i = 0; i < 6; ++i) {
        char bid[32], lbl[48];
        snprintf(bid, sizeof(bid), "ctrl_%d", i);
        snprintf(lbl, sizeof(lbl), "Constraint_DOF_%d [Active]", i);
        BloomUiNodeDesc sl{};
        sl.id = bid; sl.parent = hp; sl.kind = BLOOM_UI_SLIDER; sl.h = 64.0f;
        sl.fill_rgba8 = BLOOM_RGBA(0x1A, 0x24, 0x34, 0xFF); sl.radius_px = 8.0f; sl.label = lbl; sl.font_px = 16.0f;
        bloom_scene_add_ui(sc, &sl);
    }

    /* Warmup + 30 measured 4K frames with live timeline */
    BloomAnimTrack trk{bloom_fnv1a64("reactor_000"), BLOOM_ANIM_ROT_Y, 0.0f, 2.0f, 0.0f, 6.283f,
                       BLOOM_EASE_LINEAR, BLOOM_LOOP_REPEAT};
    bloom_timeline_add_track(sc, &trk);
    bloom_timeline_set_playing(sc, true);

    std::vector<double> gpu_ms;
    for (int it = 0; it < 36; ++it) {
        bloom_timeline_step(sc, 0.016f);
        assert(bloom_gpu_render_headless(gpu, sc, 3840u, 2160u, false));
        if (it >= 6) gpu_ms.push_back(bloom_gpu_get_stats(gpu).last_gpu_ms);
    }
    std::sort(gpu_ms.begin(), gpu_ms.end());
    double med_ms = gpu_ms[gpu_ms.size() / 2];

    uint32_t chk1 = bloom_gpu_headless_checksum(gpu);
    assert(chk1 != 0u);

    /* Determinism check at static timeline scrub position */
    bloom_timeline_set_playing(sc, false);
    bloom_timeline_scrub(sc, 0.75f);
    assert(bloom_gpu_render_headless(gpu, sc, 3840u, 2160u, false));
    uint32_t chk_det1 = bloom_gpu_headless_checksum(gpu);
    assert(bloom_gpu_render_headless(gpu, sc, 3840u, 2160u, true));
    uint32_t chk_det2 = bloom_gpu_headless_checksum(gpu);
    assert(chk_det1 == chk_det2 && chk_det1 != 0u);

    /* Gate 7: Idle Zero-Submit check */
    uint64_t submits_before_idle = bloom_gpu_get_stats(gpu).submitted_cbs;
    for (int i = 0; i < 20; ++i) {
        bloom_timeline_step(sc, 0.016f);
        assert(bloom_gpu_render_headless(gpu, sc, 3840u, 2160u, false));
    }
    BloomGpuStats st = bloom_gpu_get_stats(gpu);
    printf("  4K (3840x2160) Composite: GPU med=%.3f ms (%.1f fps) | CPU pack=%.1f us | WC=%.1f KB\n",
           med_ms, 1000.0 / med_ms, st.last_cpu_pack_us, (double)st.last_wc_bytes / 1024.0);
    printf("  Stats: meshlets=%u/%u | glyphs=%u | chk=0x%08X | submits=%llu | idle_skipped=%llu\n",
           st.surviving_meshlets, st.total_meshlets, st.atlas_glyph_count, chk_det1,
           (unsigned long long)st.submitted_cbs, (unsigned long long)st.idle_skipped_frames);

    assert(med_ms < 2.5);
    assert(st.atlas_glyph_count >= 15u);
    assert(st.submitted_cbs == submits_before_idle);
    assert(st.idle_skipped_frames == 20u);

    bloom_scene_destroy(sc);
    bloom_gpu_destroy(gpu);
    printf("ALL 7 HARDWARE CI GATES PASSED!\n");
    return 0;
}
