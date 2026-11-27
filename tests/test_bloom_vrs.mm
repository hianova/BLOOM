#include "bloom.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static uint16_t build_torus_template(BloomScene *sc, const char *name) {
    constexpr uint32_t kNu = 8u;
    constexpr uint32_t kNv = 6u;
    constexpr float R = 0.85f;
    constexpr float r = 0.32f;

    BloomVtx12 verts[BLOOM_CLUSTER_VTX]{};
    for (uint32_t iu = 0u; iu < kNu; ++iu) {
        float u = (float)iu / (float)kNu;
        float phi = u * 6.2831853f;
        float cp = cosf(phi), sp = sinf(phi);
        for (uint32_t iv = 0u; iv < kNv; ++iv) {
            float v = (float)iv / (float)kNv;
            float theta = v * 6.2831853f;
            float ct = cosf(theta), st = sinf(theta);

            float px = (R + r * ct) * cp;
            float py = r * st;
            float pz = (R + r * ct) * sp;
            float nx = ct * cp;
            float ny = st;
            float nz = ct * sp;

            uint32_t idx = iu * kNv + iv;
            verts[idx].pos_unorm16[0] = bloom_pack_pos_unorm16(px, BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
            verts[idx].pos_unorm16[1] = bloom_pack_pos_unorm16(py, BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
            verts[idx].pos_unorm16[2] = bloom_pack_pos_unorm16(pz, BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
            bloom_oct_encode(nx, ny, nz, verts[idx].oct_norm);
            verts[idx].uv_u8[0] = (uint8_t)lroundf(u * 255.0f);
            verts[idx].uv_u8[1] = (uint8_t)lroundf(v * 255.0f);
            verts[idx].joint_id = 0u;
            verts[idx].mat_id = 0u;
        }
    }
    for (uint32_t slot = kNu * kNv; slot < BLOOM_CLUSTER_VTX; ++slot) {
        verts[slot].pos_unorm16[0] = 32768u;
        verts[slot].pos_unorm16[1] = 32768u;
        verts[slot].pos_unorm16[2] = 32768u;
        verts[slot].oct_norm[1] = 127;
    }

    uint8_t lidx[kNu * kNv * 6u]{};
    uint32_t w = 0u;
    for (uint32_t iu = 0u; iu < kNu; ++iu) {
        uint32_t iu1 = (iu + 1u) % kNu;
        for (uint32_t iv = 0u; iv < kNv; ++iv) {
            uint32_t iv1 = (iv + 1u) % kNv;
            uint8_t a = (uint8_t)(iu * kNv + iv);
            uint8_t b = (uint8_t)(iu1 * kNv + iv);
            uint8_t c = (uint8_t)(iu1 * kNv + iv1);
            uint8_t d = (uint8_t)(iu * kNv + iv1);
            lidx[w++] = a; lidx[w++] = b; lidx[w++] = c;
            lidx[w++] = a; lidx[w++] = c; lidx[w++] = d;
        }
    }

    BloomMeshletHeader32 hdr{};
    hdr.vtx_offset = 0u;
    hdr.idx_offset = 0u;
    hdr.vtx_cnt = kNu * kNv;
    hdr.tri_cnt = kNu * kNv * 2u;
    hdr.bsphere_f16[3] = bloom_f32_to_f16(R + r + 0.1f);
    hdr.cone_axis[2] = 127;
    hdr.cone_cutoff = 127;
    return bloom_scene_add_template(sc, name, 1u, &hdr, verts, lidx, w);
}

static void calc_region_metrics(const uint8_t *ref, const uint8_t *test,
                                uint32_t w, uint32_t h,
                                float cx_norm, float cy_norm, float rx_norm, float ry_norm,
                                double *out_psnr, double *out_mae, uint32_t *out_px) {
    uint32_t x0 = (uint32_t)std::max(0, (int)std::floor((cx_norm - rx_norm) * (float)w));
    uint32_t x1 = (uint32_t)std::min((int)w, (int)std::ceil((cx_norm + rx_norm) * (float)w));
    uint32_t y0 = (uint32_t)std::max(0, (int)std::floor((cy_norm - ry_norm) * (float)h));
    uint32_t y1 = (uint32_t)std::min((int)h, (int)std::ceil((cy_norm + ry_norm) * (float)h));

    double mse_sum = 0.0;
    double abs_sum = 0.0;
    uint64_t cnt = 0u;
    for (uint32_t y = y0; y < y1; ++y) {
        for (uint32_t x = x0; x < x1; ++x) {
            size_t idx = ((size_t)y * w + x) * 4u;
            for (int c = 0; c < 3; ++c) {
                double d = (double)ref[idx + (size_t)c] - (double)test[idx + (size_t)c];
                mse_sum += d * d;
                abs_sum += std::abs(d);
            }
            cnt += 3u;
        }
    }
    double mse = cnt ? (mse_sum / (double)cnt) : 0.0;
    *out_mae = cnt ? (abs_sum / (double)cnt) : 0.0;
    *out_psnr = (mse < 1e-9) ? 99.99 : (10.0 * std::log10((255.0 * 255.0) / mse));
    if (out_px) *out_px = (x1 - x0) * (y1 - y0);
}

static double median_of(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    if (n == 0u) return 0.0;
    return (n & 1u) ? v[n / 2u] : 0.5 * (v[n / 2u - 1u] + v[n / 2u]);
}

static BloomScene *build_dense_pbr_scene(BloomGpu *gpu) {
    std::vector<uint8_t> albedo(64u * 64u * 4u);
    std::vector<uint8_t> mr(64u * 64u * 4u);
    std::vector<uint8_t> nrm(64u * 64u * 4u);
    for (uint32_t y = 0u; y < 64u; ++y) {
        for (uint32_t x = 0u; x < 64u; ++x) {
            size_t i = ((size_t)y * 64u + x) * 4u;
            bool chk = (((x >> 2u) ^ (y >> 2u)) & 1u) != 0u;
            albedo[i + 0u] = chk ? 230u : 55u;
            albedo[i + 1u] = chk ? 225u : 135u;
            albedo[i + 2u] = chk ? 240u : 210u;
            albedo[i + 3u] = 255u;

            mr[i + 0u] = 255u;
            mr[i + 1u] = chk ? 64u : 185u;
            mr[i + 2u] = chk ? 215u : 35u;
            mr[i + 3u] = 255u;

            nrm[i + 0u] = (uint8_t)(128 + (int)(sinf((float)x * 0.45f) * 32.0f));
            nrm[i + 1u] = (uint8_t)(128 + (int)(cosf((float)y * 0.45f) * 32.0f));
            nrm[i + 2u] = 248u;
            nrm[i + 3u] = 255u;
        }
    }
    uint16_t s_alb = bloom_gpu_upload_texture_rgba8(gpu, albedo.data(), 64u, 64u, true);
    uint16_t s_mr  = bloom_gpu_upload_texture_rgba8(gpu, mr.data(), 64u, 64u, true);
    uint16_t s_nrm = bloom_gpu_upload_texture_rgba8(gpu, nrm.data(), 64u, 64u, true);
    assert(s_alb && s_mr && s_nrm);

    BloomScene *sc = bloom_scene_create();
    assert(sc != nullptr);

    BloomMaterial32 mat{};
    mat.base_rgba8 = 0xFFFFFFFFu;
    mat.emissive_rgba8 = 0xFF040810u;
    mat.metallic_f16 = bloom_f32_to_f16(0.85f);
    mat.roughness_f16 = bloom_f32_to_f16(0.28f);
    mat.ao_f16 = bloom_f32_to_f16(1.0f);
    mat.normal_scale_f16 = bloom_f32_to_f16(1.0f);
    mat.albedo_tex = s_alb;
    mat.mr_tex = s_mr;
    mat.normal_tex = s_nrm;
    mat.flags = BLOOM_MAT_FLAG_PBR;
    uint16_t m0 = bloom_scene_add_material(sc, &mat);

    BloomLight32 sun{{0.4f, -0.7f, 0.58f}, 0.0f, 0xFFF2FAFFu, 2.8f, BLOOM_LIGHT_DIR, 0u};
    BloomLight32 pt0{{-5.0f, 3.0f, 7.0f}, 24.0f, 0xFFFF9040u, 9.0f, BLOOM_LIGHT_POINT, 0u};
    BloomLight32 pt1{{ 5.0f, 3.0f, 7.0f}, 24.0f, 0xFF40D8FFu, 9.0f, BLOOM_LIGHT_POINT, 0u};
    BloomLight32 pt2{{ 0.0f,-3.0f, 6.0f}, 20.0f, 0xFF90FF70u, 6.0f, BLOOM_LIGHT_POINT, 0u};
    bloom_scene_add_light(sc, &sun);
    bloom_scene_add_light(sc, &pt0);
    bloom_scene_add_light(sc, &pt1);
    bloom_scene_add_light(sc, &pt2);

    uint16_t tpl = build_torus_template(sc, "vrs_pbr_torus");
    float qid[4] = {0.25f, 0.15f, 0.0f, 0.956f};
    float sc_big[3] = {1.15f, 1.15f, 1.15f};
    for (int r = 0; r < 10; ++r) {
        for (int c = 0; c < 14; ++c) {
            char oid[32];
            std::snprintf(oid, sizeof(oid), "vrs_obj_%02d_%02d", r, c);
            float pos[3] = {((float)c - 6.5f) * 1.45f, ((float)r - 4.5f) * 1.15f, 9.0f + (float)((r + c) % 3) * 1.6f};
            BloomHandle ho = bloom_scene_spawn_object(sc, oid, tpl, pos, qid, sc_big, 0xFFFFFFFFu, nullptr);
            assert(ho != BLOOM_HANDLE_NULL);
            bloom_scene_set_object_material(sc, ho, m0);
        }
    }
    for (int i = 0; i < 24; ++i) {
        char pid[32];
        std::snprintf(pid, sizeof(pid), "vrs_sph_%02d", i);
        BloomAnalyticPrim32 pr{{((float)(i % 6) - 2.5f) * 2.4f, ((float)(i / 6) - 1.5f) * 1.8f, 6.5f},
                               0.55f, {0, bloom_f32_to_f16(1.0f), 0, 0}, BLOOM_RGBA(0xFF, 0x78, 0x28, 0xFF), 1u};
        bloom_scene_spawn_analytic(sc, pid, &pr);
    }

    /* Corner HUD panel to verify foveate_ui=false preserves native crisp UI */
    BloomUiNodeDesc panel{};
    panel.id = "vrs_hud";
    panel.kind = BLOOM_UI_PANEL;
    panel.x = 32.0f; panel.y = 32.0f; panel.w = 360.0f; panel.h = 120.0f;
    panel.fill_rgba8 = BLOOM_RGBA(0x14, 0x1E, 0x2C, 0xFF);
    panel.border_rgba8 = BLOOM_RGBA(0x38, 0xD0, 0xFF, 0xFF);
    panel.border_px = 2.0f;
    panel.radius_px = 10.0f;
    panel.label = "Foveated VRS Telemetry";
    bloom_scene_add_ui(sc, &panel);
    return sc;
}

int main() {
    printf("=== BLOOM Hardware Foveated VRS (MTLRasterizationRateMap) Verification & Benchmark ===\n");
    BloomGpu *gpu = bloom_gpu_create(nullptr, nullptr);
    assert(gpu != nullptr);

    bool vrs_hw = bloom_gpu_vrs_supported(gpu);
    printf("  Device VRS (MTLRasterizationRateMap) Supported: %s\n", vrs_hw ? "YES" : "NO");
    assert(vrs_hw && "Expected Apple M1 hardware VRS support");

    /* 1. Default non-VRS backward compatibility check */
    BloomVrsConfig def_cfg = bloom_gpu_get_vrs(gpu);
    assert(!def_cfg.enabled);
    BloomScene *sc = build_dense_pbr_scene(gpu);

    const uint32_t W = 3840u, H = 2160u;
    assert(bloom_gpu_render_headless(gpu, sc, W, H, true));
    BloomGpuStats st_off = bloom_gpu_get_stats(gpu);
    assert(!st_off.vrs_active && st_off.vrs_phys_w == W && st_off.vrs_phys_h == H && st_off.vrs_pixel_ratio == 1.0f);
    uint32_t chk_full = bloom_gpu_headless_checksum(gpu);

    std::vector<uint8_t> full_rgba((size_t)W * H * 4u);
    uint32_t rw = 0u, rh = 0u;
    assert(bloom_gpu_readback_headless_rgba8(gpu, full_rgba.data(), (uint32_t)full_rgba.size(), &rw, &rh));
    assert(rw == W && rh == H);

    /* 2. Enable opt-in Foveated VRS (1.0x in ~15-deg fovea, 0.5x mid, 0.25x outer) */
    BloomVrsConfig cfg = bloom_vrs_default_config();
    cfg.enabled = true;
    cfg.gaze_x = 0.50f;
    cfg.gaze_y = 0.50f;
    cfg.fovea_radius = 0.125f;
    cfg.mid_radius = 0.30f;
    cfg.mid_rate = 0.50f;
    cfg.periphery_rate = 0.25f;
    bloom_gpu_set_vrs(gpu, &cfg);

    /* Gaze update / config update must render even when force=false */
    assert(bloom_gpu_render_headless(gpu, sc, W, H, false));
    BloomGpuStats st_vrs = bloom_gpu_get_stats(gpu);
    assert(st_vrs.vrs_active);
    assert(st_vrs.vrs_phys_w < W && st_vrs.vrs_phys_h < H);
    assert(st_vrs.vrs_pixel_ratio < 0.65f);
    uint32_t chk_vrs_center = bloom_gpu_headless_checksum(gpu);
    assert(chk_vrs_center != 0u);

    /* Idle skipping still works after VRS render */
    uint64_t cbs_before_idle = st_vrs.submitted_cbs;
    assert(bloom_gpu_render_headless(gpu, sc, W, H, false));
    assert(bloom_gpu_get_stats(gpu).submitted_cbs == cbs_before_idle);

    /* 3. Verify resolve quality inside central 15-deg fovea & crisp corner HUD */
    std::vector<uint8_t> vrs_rgba((size_t)W * H * 4u);
    assert(bloom_gpu_readback_headless_rgba8(gpu, vrs_rgba.data(), (uint32_t)vrs_rgba.size(), &rw, &rh));

    double fov_psnr = 0.0, fov_mae = 0.0;
    uint32_t fov_px = 0u;
    calc_region_metrics(full_rgba.data(), vrs_rgba.data(), W, H, 0.50f, 0.50f, 0.09f, 0.15f,
                        &fov_psnr, &fov_mae, &fov_px);
    double corner_hud_psnr = 0.0, corner_hud_mae = 0.0;
    calc_region_metrics(full_rgba.data(), vrs_rgba.data(), W, H, 212.0f / (float)W, 92.0f / (float)H,
                        160.0f / (float)W, 50.0f / (float)H, &corner_hud_psnr, &corner_hud_mae, nullptr);

    printf("  [Accuracy @ Center Gaze (0.50, 0.50)] Phys=%ux%u (%.1f%% pixels, -%.1f%% shading)\n",
           st_vrs.vrs_phys_w, st_vrs.vrs_phys_h,
           st_vrs.vrs_pixel_ratio * 100.0f, (1.0f - st_vrs.vrs_pixel_ratio) * 100.0f);
    printf("    Fovea (%u px): PSNR=%.2f dB | MAE=%.3f/255 | Corner HUD PSNR=%.2f dB\n",
           fov_px, fov_psnr, fov_mae, corner_hud_psnr);
    assert(fov_psnr > 42.0 && fov_mae < 2.0);
    assert(corner_hud_psnr > 48.0);

    /* 4. Dynamic gaze shift verification */
    bloom_gpu_set_vrs_gaze(gpu, 0.25f, 0.55f);
    assert(bloom_gpu_render_headless(gpu, sc, W, H, false));
    uint32_t chk_vrs_left = bloom_gpu_headless_checksum(gpu);
    assert(chk_vrs_left != chk_vrs_center);
    assert(bloom_gpu_readback_headless_rgba8(gpu, vrs_rgba.data(), (uint32_t)vrs_rgba.size(), &rw, &rh));

    double left_psnr = 0.0, left_mae = 0.0;
    calc_region_metrics(full_rgba.data(), vrs_rgba.data(), W, H, 0.25f, 0.55f, 0.08f, 0.14f,
                        &left_psnr, &left_mae, nullptr);
    printf("  [Accuracy @ Shifted Gaze (0.25, 0.55)] Fovea PSNR=%.2f dB | MAE=%.3f/255 | chk=0x%08X->0x%08X\n",
           left_psnr, left_mae, chk_vrs_center, chk_vrs_left);
    assert(left_psnr > 40.0 && left_mae < 2.5);

    /* 5. Apple M1 GPU Hardware Timing Benchmark (Uniform Full vs Foveated VRS @ 4K) */
    const int warmup = 6;
    const int iters = 24;

    bloom_gpu_set_vrs(gpu, nullptr); /* disable VRS */
    std::vector<double> full_times;
    for (int f = 0; f < warmup + iters; ++f) {
        bloom_scene_mark_dirty(sc);
        assert(bloom_gpu_render_headless(gpu, sc, W, H, true));
        if (f >= warmup) full_times.push_back(bloom_gpu_get_stats(gpu).last_gpu_ms);
    }

    cfg.gaze_x = 0.50f;
    cfg.gaze_y = 0.50f;
    bloom_gpu_set_vrs(gpu, &cfg);
    std::vector<double> vrs_times;
    for (int f = 0; f < warmup + iters; ++f) {
        bloom_scene_mark_dirty(sc);
        assert(bloom_gpu_render_headless(gpu, sc, W, H, true));
        if (f >= warmup) vrs_times.push_back(bloom_gpu_get_stats(gpu).last_gpu_ms);
    }

    double full_med = median_of(full_times);
    double vrs_med = median_of(vrs_times);
    double speedup = full_med / std::max(vrs_med, 1e-6);
    printf("  [M1 4K Bench] Uniform_Full (%ux%u): med=%.3f ms | Foveated_VRS (%ux%u): med=%.3f ms (%.2fx speedup, -%.1f%% time)\n",
           W, H, full_med, st_vrs.vrs_phys_w, st_vrs.vrs_phys_h, vrs_med,
           speedup, (1.0 - vrs_med / full_med) * 100.0);
    assert(vrs_med < full_med);

    /* 6. Graceful fallback on tiny viewports (<64px) & grounding compatibility while VRS enabled */
    assert(bloom_gpu_render_headless(gpu, sc, 32u, 32u, true));
    assert(!bloom_gpu_get_stats(gpu).vrs_active);
    BloomSdkSnapshot snap{};
    assert(bloom_sdk_capture_snapshot(gpu, sc, 1280u, 720u, &snap));
    const BloomSdkEntity *hud_e = bloom_sdk_find_by_id(&snap, "vrs_hud");
    assert(hud_e != nullptr && hud_e->visible_px > 0u);
    bloom_sdk_free_snapshot(&snap);

    /* 7. Disable VRS again -> exact original checksum restored */
    bloom_gpu_set_vrs(gpu, nullptr);
    assert(bloom_gpu_render_headless(gpu, sc, W, H, true));
    assert(bloom_gpu_headless_checksum(gpu) == chk_full);

    bloom_scene_destroy(sc);
    bloom_gpu_destroy(gpu);
    printf("ALL HARDWARE FOVEATED VRS TESTS & BENCHMARKS PASSED!\n");
    return 0;
}
