#import <Cocoa/Cocoa.h>
#import <ImageIO/ImageIO.h>

#include "bloom.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static void rotate_q_f32(const float q[4], const float v[3], float out[3]) {
    float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
    float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
    float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
    out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
    out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
    out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
}

static bool decode_image_rgba8(const char *path, std::vector<uint8_t> &out_rgba, uint32_t &out_w, uint32_t &out_h) {
    if (!path || !path[0]) return false;
    NSString *ns_path = [NSString stringWithUTF8String:path];
    NSData *data = [NSData dataWithContentsOfFile:ns_path];
    if (!data) return false;
    CGImageSourceRef src = CGImageSourceCreateWithData((__bridge CFDataRef)data, nullptr);
    if (!src) return false;
    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
    CFRelease(src);
    if (!img) return false;

    size_t w = CGImageGetWidth(img);
    size_t h = CGImageGetHeight(img);
    if (w == 0 || h == 0) {
        CGImageRelease(img);
        return false;
    }
    out_w = (uint32_t)w;
    out_h = (uint32_t)h;
    out_rgba.assign(w * h * 4u, 255u);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(out_rgba.data(), w, h, 8, w * 4u, cs,
                                             kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGContextSetBlendMode(ctx, kCGBlendModeCopy);
    CGContextDrawImage(ctx, CGRectMake(0.0, 0.0, (CGFloat)w, (CGFloat)h), img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    CGImageRelease(img);
    return true;
}

static uint32_t pack_rgba_f4(NSArray *arr, bool with_alpha) {
    float r = (arr.count > 0) ? [arr[0] floatValue] : 1.0f;
    float g = (arr.count > 1) ? [arr[1] floatValue] : 1.0f;
    float b = (arr.count > 2) ? [arr[2] floatValue] : 1.0f;
    float a = (with_alpha && arr.count > 3) ? [arr[3] floatValue] : 1.0f;
    auto q = [](float x) -> uint32_t {
        long v = lroundf(std::max(0.0f, std::min(1.0f, x)) * 255.0f);
        return (uint32_t)v;
    };
    return BLOOM_RGBA(q(r), q(g), q(b), q(a));
}

struct ModelCase {
    const char *name;
    float yaw;
    float pitch;
    float dist_mul;
    uint32_t min_visible_px;
    uint32_t min_visible_objs;
    double min_luma;
    bool run_4k_bench;
};

static bool run_model(const ModelCase &mc, const fs::path &bobc_dir, const fs::path &out_dir) {
    fs::path bobc_path = bobc_dir / (std::string(mc.name) + ".bobc");
    fs::path blmw_path = bobc_dir / (std::string(mc.name) + "_pbr.blmw");
    fs::path json_path = bobc_dir / (std::string(mc.name) + "_pbr.json");

    BloomBobjDoc doc{};
    if (!bloom_bobj_load_file(bobc_path.c_str(), &doc)) {
        fprintf(stderr, "[FAIL] Could not load %s\n", bobc_path.c_str());
        return false;
    }

    BloomScene *scene = bloom_scene_create();
    BloomGpu *gpu = bloom_gpu_create(nullptr, nullptr);
    if (!scene || !gpu) {
        fprintf(stderr, "[FAIL] Engine creation failed\n");
        return false;
    }
    if (!bloom_scene_load_bobj(scene, &doc, false)) {
        fprintf(stderr, "[FAIL] bloom_scene_load_bobj failed on %s\n", mc.name);
        return false;
    }
    bloom_bobj_free(&doc);
    bloom_scene_sync_transforms(scene);

    /* Load packed binary "BLMW" PBR manifest (fallback to legacy JSON for backward compatibility) */
    std::vector<uint16_t> tex_slices;
    std::vector<uint16_t> mat_ids;
    std::vector<int32_t> obj_mat_indices;

    auto resolve_tex_path = [&](const char *raw_path) -> std::string {
        if (!raw_path || !raw_path[0]) return "";
        fs::path p(raw_path);
        if (p.is_relative()) return (bobc_dir / p).string();
        return std::string(raw_path);
    };

    BloomPbrManifest blmw{};
    if (fs::exists(blmw_path) && bloom_wire_load_pbr_manifest_file(blmw_path.c_str(), &blmw)) {
        tex_slices.resize(blmw.texture_cnt, 0u);
        for (uint32_t i = 0u; i < blmw.texture_cnt; ++i) {
            std::string tpath = resolve_tex_path(blmw.textures[i].path);
            if (!tpath.empty()) {
                std::vector<uint8_t> rgba;
                uint32_t tw = 0u, th = 0u;
                if (decode_image_rgba8(tpath.c_str(), rgba, tw, th)) {
                    tex_slices[i] = bloom_gpu_upload_texture_rgba8(gpu, rgba.data(), tw, th, true);
                }
            }
        }
        auto slice_at = [&](int32_t idx) -> uint16_t {
            if (idx >= 0 && (size_t)idx < tex_slices.size()) return tex_slices[(size_t)idx];
            return 0u;
        };
        auto q8 = [](float x) -> uint32_t {
            long v = lroundf(std::max(0.0f, std::min(1.0f, x)) * 255.0f);
            return (uint32_t)v;
        };
        for (uint32_t mi = 0u; mi < blmw.material_cnt; ++mi) {
            const BloomPbrMaterialDesc &md = blmw.materials[mi];
            BloomMaterial32 bm{};
            bm.base_rgba8 = BLOOM_RGBA(q8(md.base_color_factor[0]), q8(md.base_color_factor[1]),
                                       q8(md.base_color_factor[2]), q8(md.base_color_factor[3]));
            bm.emissive_rgba8 = BLOOM_RGBA(q8(md.emissive_factor[0]), q8(md.emissive_factor[1]),
                                           q8(md.emissive_factor[2]), 255u) & 0x00FFFFFFu;
            bm.metallic_f16 = bloom_f32_to_f16(md.metallic_factor);
            bm.roughness_f16 = bloom_f32_to_f16(md.roughness_factor);
            bm.ao_f16 = bloom_f32_to_f16(1.0f);
            bm.normal_scale_f16 = bloom_f32_to_f16(md.normal_scale);
            bm.albedo_tex = slice_at(md.base_color_tex);
            bm.mr_tex = slice_at(md.mr_tex);
            bm.normal_tex = slice_at(md.normal_tex);
            bm.emissive_tex = slice_at(md.emissive_tex);
            bm.ao_tex = slice_at(md.ao_tex);
            bm.flags = BLOOM_MAT_FLAG_PBR | (md.double_sided ? BLOOM_MAT_FLAG_DOUBLE_SIDED : 0u);
            mat_ids.push_back(bloom_scene_add_material(scene, &bm));
        }
        obj_mat_indices.reserve(blmw.object_cnt);
        for (uint32_t oi = 0u; oi < blmw.object_cnt; ++oi) {
            obj_mat_indices.push_back(blmw.objects[oi].material_index);
        }
        bloom_pbr_manifest_free(&blmw);
    } else {
        NSData *jdata = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:json_path.c_str()]];
        NSDictionary *manifest = jdata ? [NSJSONSerialization JSONObjectWithData:jdata options:0 error:nil] : nil;

        NSArray *jtexs = manifest[@"textures"];
        if (jtexs) {
            tex_slices.resize(jtexs.count, 0u);
            for (NSUInteger i = 0; i < jtexs.count; ++i) {
                NSDictionary *td = jtexs[i];
                NSString *tpath = td[@"path"];
                if (tpath && tpath.length > 0) {
                    std::string resolved = resolve_tex_path(tpath.UTF8String);
                    std::vector<uint8_t> rgba;
                    uint32_t tw = 0u, th = 0u;
                    if (decode_image_rgba8(resolved.c_str(), rgba, tw, th)) {
                        tex_slices[i] = bloom_gpu_upload_texture_rgba8(gpu, rgba.data(), tw, th, true);
                    }
                }
            }
        }

        auto tex_slice = [&](NSNumber *n) -> uint16_t {
            if (!n) return 0u;
            NSInteger idx = [n integerValue];
            if (idx >= 0 && (size_t)idx < tex_slices.size()) return tex_slices[(size_t)idx];
            return 0u;
        };

        NSArray *jmats = manifest[@"materials"];
        if (jmats && jmats.count > 0) {
            for (NSDictionary *md in jmats) {
                BloomMaterial32 bm{};
                bm.base_rgba8 = pack_rgba_f4(md[@"baseColorFactor"], true);
                bm.emissive_rgba8 = md[@"emissiveFactor"] ? pack_rgba_f4(md[@"emissiveFactor"], false) & 0x00FFFFFFu : 0u;
                bm.metallic_f16 = bloom_f32_to_f16([md[@"metallicFactor"] floatValue]);
                bm.roughness_f16 = bloom_f32_to_f16([md[@"roughnessFactor"] floatValue]);
                bm.ao_f16 = bloom_f32_to_f16(1.0f);
                float ns = md[@"normalScale"] ? [md[@"normalScale"] floatValue] : 1.0f;
                bm.normal_scale_f16 = bloom_f32_to_f16(ns);
                bm.albedo_tex = tex_slice(md[@"baseColorTex"]);
                bm.mr_tex = tex_slice(md[@"mrTex"]);
                bm.normal_tex = tex_slice(md[@"normalTex"]);
                bm.emissive_tex = tex_slice(md[@"emissiveTex"]);
                bm.ao_tex = tex_slice(md[@"aoTex"]);
                bm.flags = BLOOM_MAT_FLAG_PBR | ([md[@"doubleSided"] boolValue] ? BLOOM_MAT_FLAG_DOUBLE_SIDED : 0u);
                mat_ids.push_back(bloom_scene_add_material(scene, &bm));
            }
        }
        NSArray *jobjs = manifest[@"objects"];
        if (jobjs) {
            obj_mat_indices.reserve(jobjs.count);
            for (NSDictionary *od in jobjs) {
                obj_mat_indices.push_back((int32_t)[od[@"material_index"] integerValue]);
            }
        }
    }
    BloomMaterial32 def_bm{};
    def_bm.base_rgba8 = BLOOM_RGBA(225, 228, 234, 255);
    def_bm.metallic_f16 = bloom_f32_to_f16(0.15f);
    def_bm.roughness_f16 = bloom_f32_to_f16(0.45f);
    def_bm.ao_f16 = bloom_f32_to_f16(1.0f);
    def_bm.normal_scale_f16 = bloom_f32_to_f16(1.0f);
    def_bm.flags = BLOOM_MAT_FLAG_PBR;
    uint16_t def_mat_id = bloom_scene_add_material(scene, &def_bm);

    uint32_t slot_cnt = bloom_scene_get_object_slot_count(scene);
    size_t obj_ord = 0u;
    for (uint32_t s = 0u; s < slot_cnt; ++s) {
        BloomObjectMeta meta{};
        if (!bloom_scene_get_object_meta(scene, s, &meta)) continue;
        uint16_t mid = def_mat_id;
        if (obj_ord < obj_mat_indices.size()) {
            int32_t mi = obj_mat_indices[obj_ord];
            if (mi >= 0 && (size_t)mi < mat_ids.size()) mid = mat_ids[(size_t)mi];
        }
        bloom_scene_set_object_material(scene, meta.handle, mid);
        ++obj_ord;
    }

    /* 3-point PBR studio lighting illuminated onto -Z front faces */
    BloomLight32 key_l{{0.45f, -0.65f, 0.60f}, 0.0f, BLOOM_RGBA(255, 248, 236, 255), 2.8f, BLOOM_LIGHT_DIR, 0u};
    BloomLight32 fill_l{{-0.70f, -0.25f, 0.50f}, 0.0f, BLOOM_RGBA(180, 210, 255, 255), 1.4f, BLOOM_LIGHT_DIR, 0u};
    BloomLight32 rim_l{{-0.10f, 0.35f, -0.85f}, 0.0f, BLOOM_RGBA(255, 215, 175, 255), 1.4f, BLOOM_LIGHT_DIR, 0u};
    bloom_scene_add_light(scene, &key_l);
    bloom_scene_add_light(scene, &fill_l);
    bloom_scene_add_light(scene, &rim_l);

    /* Compute world AABB from live object states + template bounds */
    bloom_scene_sync_transforms(scene);
    float wmin[3] = {1e9f, 1e9f, 1e9f}, wmax[3] = {-1e9f, -1e9f, -1e9f};
    uint32_t active_objs = 0u;
    for (uint32_t s = 0u; s < slot_cnt; ++s) {
        BloomObjectMeta meta{};
        if (!bloom_scene_get_object_meta(scene, s, &meta)) continue;
        ++active_objs;
        const BloomMeshletHeader32 *hdrs = nullptr;
        const BloomVtx12 *verts = nullptr;
        const uint8_t *lidx = nullptr;
        uint32_t mcnt = 0u, ibytes = 0u;
        /* Look up local bounds via template mesh by scanning templates */
        float lmin[3] = {1e9f, 1e9f, 1e9f}, lmax[3] = {-1e9f, -1e9f, -1e9f};
        bool found_tpl = false;
        for (uint16_t t = 0u; t < 256u; ++t) {
            if (!bloom_scene_get_template_mesh(scene, t, &mcnt, &hdrs, &verts, &lidx, &ibytes)) break;
            if (mcnt > 0u && verts && (verts[0].mat_id == (t & 7u))) {
                /* Wait: find exact template via GPU meshlets obj_id or fallback bounding box [-1.5,1.5] */
                (void)hdrs;
            }
        }
        if (!found_tpl) {
            const BloomMeshletHeader32 *gm = nullptr;
            uint32_t gm_cnt = bloom_scene_get_gpu_meshlets(scene, &gm);
            for (uint32_t mi = 0u; mi < gm_cnt; ++mi) {
                if (gm[mi].obj_id != active_objs - 1u) continue;
                float cx = bloom_f16_to_f32(gm[mi].bsphere_f16[0]);
                float cy = bloom_f16_to_f32(gm[mi].bsphere_f16[1]);
                float cz = bloom_f16_to_f32(gm[mi].bsphere_f16[2]);
                float cr = bloom_f16_to_f32(gm[mi].bsphere_f16[3]);
                lmin[0] = std::min(lmin[0], cx - cr); lmax[0] = std::max(lmax[0], cx + cr);
                lmin[1] = std::min(lmin[1], cy - cr); lmax[1] = std::max(lmax[1], cy + cr);
                lmin[2] = std::min(lmin[2], cz - cr); lmax[2] = std::max(lmax[2], cz + cr);
                found_tpl = true;
            }
        }
        if (!found_tpl) {
            lmin[0] = lmin[1] = lmin[2] = -1.5f;
            lmax[0] = lmax[1] = lmax[2] = 1.5f;
        }
        float q[4] = {
            bloom_f16_to_f32(meta.world_state.quat_f16[0]),
            bloom_f16_to_f32(meta.world_state.quat_f16[1]),
            bloom_f16_to_f32(meta.world_state.quat_f16[2]),
            bloom_f16_to_f32(meta.world_state.quat_f16[3]),
        };
        float sc[3] = {
            bloom_f16_to_f32(meta.world_state.scale_f16[0]),
            bloom_f16_to_f32(meta.world_state.scale_f16[1]),
            bloom_f16_to_f32(meta.world_state.scale_f16[2]),
        };
        for (int c = 0; c < 8; ++c) {
            float pt[3] = {
                ((c & 1) ? lmax[0] : lmin[0]) * sc[0],
                ((c & 2) ? lmax[1] : lmin[1]) * sc[1],
                ((c & 4) ? lmax[2] : lmin[2]) * sc[2],
            };
            float rp[3];
            rotate_q_f32(q, pt, rp);
            float wp[3] = {rp[0] + meta.world_state.pos[0], rp[1] + meta.world_state.pos[1], rp[2] + meta.world_state.pos[2]};
            for (int k = 0; k < 3; ++k) {
                wmin[k] = std::min(wmin[k], wp[k]);
                wmax[k] = std::max(wmax[k], wp[k]);
            }
        }
    }

    float ctr[3] = {0.5f * (wmin[0] + wmax[0]), 0.5f * (wmin[1] + wmax[1]), 0.5f * (wmin[2] + wmax[2])};
    float dx = wmax[0] - wmin[0], dy = wmax[1] - wmin[1], dz = wmax[2] - wmin[2];
    float rad = std::max(0.25f, 0.5f * sqrtf(dx * dx + dy * dy + dz * dz));
    float dist = rad * mc.dist_mul;
    float cy = cosf(mc.yaw), sy = sinf(mc.yaw);
    float cp = cosf(mc.pitch), sp = sinf(mc.pitch);
    float fwd[3] = {sy * cp, -sp, cy * cp};

    BloomCamera cam{};
    cam.pos[0] = ctr[0] - fwd[0] * dist;
    cam.pos[1] = ctr[1] - fwd[1] * dist;
    cam.pos[2] = ctr[2] - fwd[2] * dist;
    cam.yaw = mc.yaw;
    cam.pitch = mc.pitch;
    cam.focal = 2.0f;
    cam.near_z = std::max(0.05f, dist * 0.05f);
    cam.far_z = std::max(80.0f, dist * 6.0f);
    bloom_scene_set_camera(scene, &cam);

    const uint32_t vp_w = 1280u, vp_h = 720u;
    uint32_t max_slots = std::max<uint32_t>(64u, (active_objs + 8u) * 3u);
    std::vector<BloomGroundedGpu32> gstats(max_slots);
    BloomPickGpuResult pick{};
    if (!bloom_gpu_render_grounded(gpu, scene, vp_w, vp_h, vp_w / 2u, vp_h / 2u,
                                   gstats.data(), max_slots, &pick)) {
        fprintf(stderr, "[FAIL] %s: bloom_gpu_render_grounded failed\n", mc.name);
        return false;
    }

    uint32_t vis_px = 0u, vis_objs = 0u;
    for (uint32_t i = 1u; i <= active_objs; ++i) {
        if (gstats[i].visible_px > 0u) {
            vis_px += gstats[i].visible_px;
            ++vis_objs;
        }
    }

    /* Check deterministic headless render + readback luminance */
    bloom_scene_mark_dirty(scene);
    bloom_gpu_render_headless(gpu, scene, vp_w, vp_h, true);
    uint32_t chk1 = bloom_gpu_headless_checksum(gpu);
    bloom_scene_mark_dirty(scene);
    bloom_gpu_render_headless(gpu, scene, vp_w, vp_h, true);
    uint32_t chk2 = bloom_gpu_headless_checksum(gpu);
    /* Idle Zero-Submit check */
    BloomGpuStats st_before = bloom_gpu_get_stats(gpu);
    bloom_scene_clear_dirty(scene);
    bloom_gpu_render_headless(gpu, scene, vp_w, vp_h, false);
    BloomGpuStats st_after = bloom_gpu_get_stats(gpu);

    std::vector<uint8_t> rgba(vp_w * vp_h * 4u);
    uint32_t rw = 0u, rh = 0u;
    bloom_gpu_readback_headless_rgba8(gpu, rgba.data(), (uint32_t)rgba.size(), &rw, &rh);
    double fg_luma_sum = 0.0;
    uint32_t fg_cnt = 0u;
    /* Clear color is ~11, 15, 23 -> anything clearly brighter belongs to the lit model */
    for (size_t p = 0u; p < (size_t)rw * rh; ++p) {
        uint8_t r = rgba[p * 4u + 0u], g = rgba[p * 4u + 1u], b = rgba[p * 4u + 2u];
        int dr = abs((int)r - 11), dg = abs((int)g - 15), db = abs((int)b - 23);
        if (dr + dg + db > 18) {
            fg_luma_sum += 0.299 * r + 0.587 * g + 0.114 * b;
            ++fg_cnt;
        }
    }
    double mean_luma = fg_cnt ? (fg_luma_sum / (double)fg_cnt) : 0.0;

    if (!out_dir.empty()) {
        fs::create_directories(out_dir);
        if (strcmp(mc.name, "DamagedHelmet") == 0 || strcmp(mc.name, "MetalRoughSpheres") == 0 ||
            strcmp(mc.name, "NegativeScaleTest") == 0 || strcmp(mc.name, "BoxTextured") == 0) {
            fs::path ppm = out_dir / (std::string(mc.name) + ".ppm");
            bloom_gpu_save_headless_ppm(gpu, ppm.c_str());
        }
    }

    double med_4k_ms = 0.0;
    if (mc.run_4k_bench) {
        std::vector<double> ts;
        for (int f = 0; f < 10; ++f) {
            bloom_scene_mark_dirty(scene);
            bloom_gpu_render_headless(gpu, scene, 3840u, 2160u, true);
            ts.push_back(bloom_gpu_get_stats(gpu).last_gpu_ms);
        }
        std::sort(ts.begin(), ts.end());
        med_4k_ms = ts[ts.size() / 2u];
    }

    BloomGpuStats gst = bloom_gpu_get_stats(gpu);
    printf("  [%-22s] objs=%u/%u vis_px=%u fg=%u luma=%.1f tex=%u chk=0x%08x%s%s\n",
           mc.name, vis_objs, active_objs, vis_px, fg_cnt, mean_luma, gst.uploaded_textures, chk1,
           mc.run_4k_bench ? " 4K_med=" : "",
           mc.run_4k_bench ? (std::to_string(med_4k_ms).substr(0, 5) + "ms").c_str() : "");

    bool ok = (chk1 == chk2) &&
              (st_after.submitted_cbs == st_before.submitted_cbs) &&
              (st_after.idle_skipped_frames == st_before.idle_skipped_frames + 1u) &&
              (vis_px >= mc.min_visible_px) &&
              (vis_objs >= mc.min_visible_objs) &&
              (mean_luma >= mc.min_luma) &&
              (!mc.run_4k_bench || med_4k_ms < 4.0);

    if (!ok) {
        fprintf(stderr, "  [FAIL] %s failed assertions (chk1=0x%x chk2=0x%x vis_px=%u vis_objs=%u luma=%.1f 4k=%.3f)\n",
                mc.name, chk1, chk2, vis_px, vis_objs, mean_luma, med_4k_ms);
    }

    bloom_gpu_destroy(gpu);
    bloom_scene_destroy(scene);
    return ok;
}

int main(int argc, char **argv) {
    @autoreleasepool {
        fs::path bobc_dir = "build/conformance_deps/bobc";
        fs::path out_dir;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--bobc-dir") == 0 && i + 1 < argc) bobc_dir = argv[++i];
            else if (strcmp(argv[i], "--out-dir") == 0 && i + 1 < argc) out_dir = argv[++i];
        }

        const ModelCase cases[] = {
            {"Box",                   0.55f,  0.30f, 2.3f, 5000u, 1u,  25.0, false},
            {"BoxTextured",           0.55f,  0.30f, 2.3f, 5000u, 1u,  25.0, false},
            {"BoxInterleaved",        0.55f,  0.30f, 2.3f, 5000u, 1u,  12.0, false},
            {"CesiumMilkTruck",       0.55f,  0.22f, 2.2f, 4000u, 3u,  25.0, false},
            {"DamagedHelmet",         0.35f,  0.12f, 2.2f, 8000u, 1u,  25.0, true},
            {"MetalRoughSpheres",     0.0f,   0.0f,  1.8f, 4000u, 5u,  25.0, false},
            {"NegativeScaleTest",     0.20f,  0.12f, 2.0f, 3000u, 11u, 25.0, false},
            {"Avocado",               0.45f,  0.18f, 2.2f, 2000u, 1u,  18.0, false},
        };

        printf("=== Khronos glTF-Sample-Assets Conformance Suite (libbloom.a Embedded Metal 3) ===\n");
        bool all_ok = true;
        for (const auto &mc : cases) {
            if (!run_model(mc, bobc_dir, out_dir)) all_ok = false;
        }
        if (!all_ok) return 1;
        printf("ALL 8 KHRONOS GLTF CONFORMANCE TORTURE CASES PASSED!\n");
        return 0;
    }
}
