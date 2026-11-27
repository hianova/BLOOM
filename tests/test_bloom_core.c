#include "bloom_scene.h"
#include "bloom_wire.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void build_sample_meshlet(BloomMeshletHeader32 *out_h, BloomVtx12 *out_v, uint8_t *out_idx) {
    float cx = 0.0f, cy = 0.0f, cz = 0.0f;
    uint32_t it = 0u;
    memset(out_v, 0, BLOOM_CLUSTER_VTX * sizeof(BloomVtx12));
    for (uint32_t iy = 0u; iy < 7u; ++iy) {
        float tv = (float)iy / 6.0f;
        float phi = tv * 6.2831853f;
        for (uint32_t ix = 0u; ix < 8u; ++ix) {
            float tu = (float)ix / 7.0f;
            float th = tu * 6.2831853f;
            float px = (0.85f + 0.35f * cosf(phi)) * cosf(th);
            float py = 0.35f * sinf(phi);
            float pz = (0.85f + 0.35f * cosf(phi)) * sinf(th);
            float nx = cosf(phi) * cosf(th), ny = sinf(phi), nz = cosf(phi) * sinf(th);
            uint32_t vi = iy * 8u + ix;
            out_v[vi].pos_unorm16[0] = bloom_pack_pos_unorm16(px, BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
            out_v[vi].pos_unorm16[1] = bloom_pack_pos_unorm16(py, BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
            out_v[vi].pos_unorm16[2] = bloom_pack_pos_unorm16(pz, BLOOM_LOCAL_BOX_LO, BLOOM_LOCAL_BOX_SPAN);
            bloom_oct_encode(nx, ny, nz, out_v[vi].oct_norm);
            out_v[vi].uv_u8[0] = (uint8_t)lround(tu * 255.0f);
            out_v[vi].uv_u8[1] = (uint8_t)lround(tv * 255.0f);
            out_v[vi].joint_id = (uint8_t)(ix & 3u);
            out_v[vi].mat_id = 0u;
            cx += px; cy += py; cz += pz;
        }
    }
    for (uint32_t iy = 0u; iy < 6u; ++iy) {
        for (uint32_t ix = 0u; ix < 7u; ++ix) {
            uint8_t i0 = (uint8_t)(iy * 8u + ix);
            uint8_t i1 = (uint8_t)(i0 + 1u);
            uint8_t i2 = (uint8_t)(i0 + 8u);
            uint8_t i3 = (uint8_t)(i2 + 1u);
            out_idx[it * 3u + 0u] = i0; out_idx[it * 3u + 1u] = i2; out_idx[it * 3u + 2u] = i1;
            ++it;
            out_idx[it * 3u + 0u] = i1; out_idx[it * 3u + 1u] = i2; out_idx[it * 3u + 2u] = i3;
            ++it;
        }
    }
    memset(out_h, 0, sizeof(*out_h));
    out_h->vtx_cnt = 56u;
    out_h->tri_cnt = 84u;
    out_h->bsphere_f16[0] = bloom_f32_to_f16(cx / 56.0f);
    out_h->bsphere_f16[1] = bloom_f32_to_f16(cy / 56.0f);
    out_h->bsphere_f16[2] = bloom_f32_to_f16(cz / 56.0f);
    out_h->bsphere_f16[3] = bloom_f32_to_f16(1.35f);
    out_h->cone_axis[2] = 127;
    out_h->cone_cutoff = 127;
}

static void test_bobj_codecs(void) {
    BloomMeshletHeader32 hdr;
    BloomVtx12 verts[BLOOM_CLUSTER_VTX];
    uint8_t lidx[84u * 3u];
    build_sample_meshlet(&hdr, verts, lidx);

    BloomBobjTemplate tpl = {"core_torus", 1u, 84u * 3u, &hdr, verts, lidx};
    BloomBobjObject objs[2];
    memset(objs, 0, sizeof(objs));
    objs[0].id = "reactor_00";
    objs[0].state.pos[0] = -1.5f; objs[0].state.pos[1] = 0.25f; objs[0].state.pos[2] = 10.0f;
    objs[0].state.quat_f16[3] = bloom_f32_to_f16(1.0f);
    objs[0].state.scale_f16[0] = objs[0].state.scale_f16[1] = objs[0].state.scale_f16[2] = bloom_f32_to_f16(1.0f);
    objs[0].state.rgba8_mat = BLOOM_RGBA(0x40, 0xD8, 0xF0, 0xFF);
    objs[0].cspec.dof_min_f16[0] = bloom_f32_to_f16(-1.5f);
    objs[0].cspec.dof_max_f16[0] = bloom_f32_to_f16(1.5f);
    objs[0].cspec.template_id = 0u;

    objs[1] = objs[0];
    objs[1].id = "reactor_01";
    objs[1].state.pos[0] = 2.0f;
    objs[1].state.flags = 1u;

    BloomBobjAnalytic ana = {"sensor_0", {{0.0f, 1.0f, 9.0f}, 0.4f, {0, 0x3C00u, 0, 0}, BLOOM_RGBA(0x38, 0xD0, 0xFF, 0xFF), 1u}};
    BloomBobjHud hud = {"sl_dof", "CoreDOF", {{40, 40, 800, 120}, BLOOM_RGBA(0x22, 0x34, 0x4C, 0xEB), BLOOM_RGBA(0x38, 0xD0, 0xFF, 0xFF), 0u, 2u, 24u, 0u, 0u, 0u, 0u}};

    BloomBobjDoc doc = {&tpl, 1u, objs, 2u, &ana, 1u, &hud, 1u};
    BloomDigest128 exp = bloom_bobj_digest128(&doc, true);

    uint8_t *buf_r = NULL, *buf_c = NULL;
    size_t len_r = 0u, len_c = 0u;
    assert(bloom_bobj_encode_row(&doc, true, &buf_r, &len_r));
    assert(bloom_bobj_encode_col(&doc, true, &buf_c, &len_c));
    assert(len_c < len_r);

    BloomBobjDoc dec_r, dec_c;
    assert(bloom_bobj_decode_auto(buf_r, len_r, &dec_r));
    assert(bloom_bobj_decode_auto(buf_c, len_c, &dec_c));
    BloomDigest128 dr = bloom_bobj_digest128(&dec_r, true);
    BloomDigest128 dc = bloom_bobj_digest128(&dec_c, true);
    assert(dr.lo == exp.lo && dr.hi == exp.hi);
    assert(dc.lo == exp.lo && dc.hi == exp.hi);

    bloom_bobj_free(&dec_r);
    bloom_bobj_free(&dec_c);
    free(buf_r);
    free(buf_c);
    printf("  [PASS] BOBR (%zu B) & BOBC (%zu B) 128-bit digest roundtrip\n", len_r, len_c);
}

static void test_scene_handles_hierarchy_projections_and_ui(void) {
    BloomScene *sc = bloom_scene_create();
    assert(sc != NULL);

    BloomMeshletHeader32 hdr;
    BloomVtx12 verts[BLOOM_CLUSTER_VTX];
    uint8_t lidx[84u * 3u];
    build_sample_meshlet(&hdr, verts, lidx);
    uint16_t tid = bloom_scene_add_template(sc, "torus", 1u, &hdr, verts, lidx, sizeof(lidx));
    assert(tid == 0u);

    float p0[3] = {10.0f, 0.0f, 12.0f};
    float p1[3] = {2.0f, 0.0f, 0.0f};
    BloomHandle parent = bloom_scene_spawn_object(sc, "root_hub", tid, p0, NULL, NULL, BLOOM_RGBA(0x40, 0xD8, 0xF0, 0xFF), NULL);
    BloomHandle child = bloom_scene_spawn_object(sc, "sat_arm", tid, p1, NULL, NULL, BLOOM_RGBA(0xFF, 0x88, 0x30, 0xFF), NULL);
    assert(bloom_scene_is_handle_valid(sc, parent));
    assert(bloom_scene_set_parent(sc, child, parent));

    /* Destroy & respawn verifies Generational SlotMap invalidates stale handles */
    BloomHandle temp = bloom_scene_spawn_object(sc, "temp_node", tid, NULL, NULL, NULL, 0, NULL);
    assert(bloom_scene_destroy_object(sc, temp));
    assert(!bloom_scene_is_handle_valid(sc, temp));
    BloomHandle reused = bloom_scene_spawn_object(sc, "new_node", tid, NULL, NULL, NULL, 0, NULL);
    assert(bloom_scene_is_handle_valid(sc, reused));
    assert(!bloom_scene_is_handle_valid(sc, temp));
    bloom_scene_destroy_object(sc, reused);

    /* Sync hierarchy and verify world offset */
    bloom_scene_sync_transforms(sc);
    BloomObjectState48 st_child;
    assert(bloom_scene_get_object_state(sc, child, &st_child));
    assert(fabsf(st_child.pos[0] - 12.0f) < 1e-4f);

    /* Layer 0 external Q16.16 constraint projection + clamp verification */
    BloomStateProjection proj = {0};
    proj.node_key = bloom_fnv1a64("root_hub");
    proj.pos[0] = bloom_q16_to_f32(65536 * 4); /* 4.0 */
    proj.pos[1] = bloom_q16_to_f32(65536 / 2); /* 0.5 */
    proj.pos[2] = 14.0f;
    proj.dof[0] = 99.0f; /* exceeds default max 1.5 -> must clamp! */
    proj.field_mask = BLOOM_PROJ_POS | BLOOM_PROJ_DOF;
    bloom_scene_apply_projections(sc, &proj, 1u);
    bloom_scene_sync_transforms(sc);
    BloomObjectState48 st_root;
    assert(bloom_scene_get_object_state(sc, parent, &st_root));
    assert(fabsf(st_root.pos[0] - 4.0f) < 1e-4f);
    assert(fabsf(bloom_f16_to_f32(st_root.dof_f16[0]) - 1.5f) < 1e-2f);

    /* Tri-Layer UI Flexbox + Slider Drag + VirtualList O(visible) + Hot-Reload State Preservation */
    BloomUiNodeDesc pnl = {0};
    pnl.id = "ctrl_panel"; pnl.kind = BLOOM_UI_PANEL; pnl.flex_dir = BLOOM_FLEX_COL;
    pnl.x = 20.0f; pnl.y = 20.0f; pnl.w = 320.0f; pnl.h = 420.0f; pnl.pad = 12.0f; pnl.gap = 8.0f;
    pnl.fill_rgba8 = BLOOM_RGBA(0x14, 0x1C, 0x28, 0xE0); pnl.radius_px = 10.0f; pnl.shadow_blur_px = 12.0f; pnl.shadow_rgba8 = BLOOM_RGBA(0x00, 0x00, 0x00, 0x80);
    BloomHandle h_pnl = bloom_scene_add_ui(sc, &pnl);

    BloomUiNodeDesc sld = {0};
    sld.id = "dof_slider"; sld.parent = h_pnl; sld.kind = BLOOM_UI_SLIDER; sld.h = 56.0f;
    sld.fill_rgba8 = BLOOM_RGBA(0x1E, 0x29, 0x38, 0xFF); sld.radius_px = 6.0f; sld.label = "Core DOF";
    sld.bind_obj_key = bloom_fnv1a64("root_hub"); sld.bind_dof_slot = 0u;
    BloomHandle h_sld = bloom_scene_add_ui(sc, &sld);

    BloomUiNodeDesc vl = {0};
    vl.id = "tele_vlist"; vl.parent = h_pnl; vl.kind = BLOOM_UI_VLIST; vl.h = 200.0f;
    vl.fill_rgba8 = BLOOM_RGBA(0x10, 0x18, 0x22, 0xFF); vl.vlist_total_rows = 10000u; vl.vlist_row_h = 28.0f;
    BloomHandle h_vl = bloom_scene_add_ui(sc, &vl);
    (void)h_vl;

    bloom_scene_layout_ui(sc, 1920.0f, 1080.0f);
    const BloomQuadHot *opq = NULL, *alp = NULL;
    uint32_t n_opq = bloom_scene_get_opaque_quads(sc, &opq);
    uint32_t n_alp = bloom_scene_get_alpha_quads(sc, &alp);
    assert(n_opq + n_alp < 30u); /* 10k rows virtualized to ~8 visible rows! */

    /* Simulate slider drag near left edge (x=50, y=50 inside dof_slider) */
    BloomInputEvent down = {0};
    down.type = BLOOM_EVENT_POINTER_DOWN; down.x = 50.0f; down.y = 50.0f;
    assert(bloom_scene_handle_input(sc, &down));
    assert(bloom_scene_get_ui_slider(sc, h_sld) < 0.15f);

    /* Visual Timeline + Idle Zero-Submit */
    bloom_scene_clear_dirty(sc);
    bloom_scene_clear_geom_dirty(sc);
    assert(!bloom_scene_is_dirty(sc));
    BloomAnimTrack trk = {bloom_fnv1a64("sat_arm"), BLOOM_ANIM_ROT_Y, 0.0f, 1.0f, 0.0f, 1.0f, BLOOM_EASE_CUBIC_IN_OUT, BLOOM_LOOP_ONCE};
    bloom_timeline_add_track(sc, &trk);
    bloom_timeline_set_playing(sc, true);
    assert(bloom_timeline_step(sc, 0.5f));
    assert(bloom_scene_is_dirty(sc));
    bloom_timeline_step(sc, 0.6f); /* reaches t=1.1 >= 1.0 */
    bloom_scene_clear_dirty(sc);
    assert(!bloom_timeline_step(sc, 0.016f)); /* finished ONCE stops cleanly */
    assert(!bloom_scene_is_dirty(sc));

    bloom_scene_destroy(sc);
    printf("  [PASS] SlotMap, Hierarchy, Layer-0 Q16 Projection, Flex/VList, Slider & Timeline Idle-0\n");
}

static void test_imported_bobc(const char *path) {
    BloomBobjDoc doc;
    assert(bloom_bobj_load_file(path, &doc));
    assert(doc.template_cnt >= 1u);
    assert(doc.object_cnt >= 1u);
    assert(doc.hud_cnt >= 1u);
    BloomScene *sc = bloom_scene_create();
    assert(bloom_scene_load_bobj(sc, &doc, true));
    /* Override DOF and reload preserving live StateStore */
    BloomHandle h = bloom_scene_find_object_by_key(sc, bloom_fnv1a64(doc.objects[0].id));
    assert(h != BLOOM_HANDLE_NULL);
    assert(bloom_scene_set_object_dof(sc, h, 0u, 0.77f));
    assert(bloom_scene_load_bobj(sc, &doc, true));
    BloomHandle h2 = bloom_scene_find_object_by_key(sc, bloom_fnv1a64(doc.objects[0].id));
    bloom_scene_sync_transforms(sc);
    BloomObjectState48 st;
    assert(bloom_scene_get_object_state(sc, h2, &st));
    assert(fabsf(bloom_f16_to_f32(st.dof_f16[0]) - 0.77f) < 1e-2f);
    bloom_scene_destroy(sc);
    bloom_bobj_free(&doc);
    printf("  [PASS] Loaded bloom_import.py BOBC (%s) + live StateStore preservation on hot-reload\n", path);
}

static void test_blmw_wire_codecs(void) {
    /* 1. Canonical ULEB128 roundtrip + non-canonical rejection */
    uint8_t vbuf[16];
    uint64_t vals[] = {0u, 1u, 127u, 128u, 16383u, 16384u, UINT32_MAX, UINT64_MAX};
    for (size_t i = 0u; i < sizeof(vals) / sizeof(vals[0]); ++i) {
        size_t wp = 0u, rp = 0u;
        uint64_t got = 0u;
        assert(bloom_wire_put_varu64(vbuf, sizeof(vbuf), &wp, vals[i]));
        assert(bloom_wire_get_varu64(vbuf, wp, &rp, &got));
        assert(rp == wp && got == vals[i]);
    }
    /* Reject non-canonical trailing zero 2-byte encoding of 0: 0x80 0x00 */
    uint8_t bad_var[2] = {0x80u, 0x00u};
    size_t rp_bad = 0u;
    uint64_t dummy = 0u;
    assert(!bloom_wire_get_varu64(bad_var, 2u, &rp_bad, &dummy));

    /* 2. PBR Manifest BLMW bit-exact roundtrip */
    BloomPbrTextureRef texs[2] = {
        {"DamagedHelmet_pbr_textures/tex_0.png"},
        {"DamagedHelmet_pbr_textures/tex_1.png"},
    };
    BloomPbrMaterialDesc mats[2] = {0};
    mats[0].base_color_factor[0] = 1.0f; mats[0].base_color_factor[1] = 1.0f;
    mats[0].base_color_factor[2] = 1.0f; mats[0].base_color_factor[3] = 1.0f;
    mats[0].metallic_factor = 1.0f; mats[0].roughness_factor = 0.45f; mats[0].normal_scale = 1.0f;
    mats[0].base_color_tex = 0; mats[0].mr_tex = 1; mats[0].normal_tex = -1;
    mats[0].emissive_tex = -1; mats[0].ao_tex = -1; mats[0].double_sided = 0u; mats[0].alpha_mode = BLOOM_ALPHA_OPAQUE;
    mats[1] = mats[0];
    mats[1].base_color_factor[0] = 0.25f; mats[1].double_sided = 1u; mats[1].alpha_mode = BLOOM_ALPHA_MASK;
    BloomPbrObjectBinding objs[3] = {
        {"helmet_body", 0},
        {"helmet_visor", 1},
        {"helmet_trim", 0},
    };
    BloomPbrManifest pbr = {texs, 2u, mats, 2u, objs, 3u};
    BloomDigest128 pbr_exp = bloom_pbr_manifest_digest128(&pbr);

    uint8_t *pbr_buf = NULL;
    size_t pbr_len = 0u;
    assert(bloom_wire_encode_pbr_manifest(&pbr, &pbr_buf, &pbr_len));
    BloomPbrManifest pbr_dec = {0};
    assert(bloom_wire_decode_pbr_manifest(pbr_buf, pbr_len, &pbr_dec));
    BloomDigest128 pbr_got = bloom_pbr_manifest_digest128(&pbr_dec);
    assert(pbr_got.lo == pbr_exp.lo && pbr_got.hi == pbr_exp.hi);
    bloom_pbr_manifest_free(&pbr_dec);
    free(pbr_buf);

    /* 3. Benchmark Report BLMW bit-exact roundtrip */
    BloomBenchMetric bms[3] = {
        {"4k_frame_time", "ms", 1.125, 0u, 0x12345678u},
        {"pbr_manifest_wire", "bytes", 184.0, 184u, 0x9ABCDEF0u},
        {"sdk_snapshot_wire", "bytes", 160.0, 160u, 0xCAFEBABEu},
    };
    BloomBenchReport rep = {"bloom_core_bench", UINT64_C(1700000000000), bms, 3u};
    BloomDigest128 rep_exp = bloom_bench_report_digest128(&rep);
    uint8_t *rep_buf = NULL;
    size_t rep_len = 0u;
    assert(bloom_wire_encode_bench_report(&rep, &rep_buf, &rep_len));
    BloomBenchReport rep_dec = {0};
    assert(bloom_wire_decode_bench_report(rep_buf, rep_len, &rep_dec));
    BloomDigest128 rep_got = bloom_bench_report_digest128(&rep_dec);
    assert(rep_got.lo == rep_exp.lo && rep_got.hi == rep_exp.hi);
    bloom_bench_report_free(&rep_dec);
    free(rep_buf);

    printf("  [PASS] BLMW Canonical Wire Codec (PBR=%zu B, Bench=%zu B) 128-bit bit-exact roundtrip\n",
           pbr_len, rep_len);
}

int main(int argc, char **argv) {
    printf("=== BLOOM vNext Core C17 Unit & Importer Integration Tests ===\n");
    test_bobj_codecs();
    test_blmw_wire_codecs();
    test_scene_handles_hierarchy_projections_and_ui();
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp(argv[i], "--imported-bobc") == 0) test_imported_bobc(argv[i + 1]);
        if (strcmp(argv[i], "--imported-blmw") == 0) {
            BloomPbrManifest pm = {0};
            assert(bloom_wire_load_pbr_manifest_file(argv[i + 1], &pm));
            uint8_t *re_buf = NULL;
            size_t re_len = 0u;
            assert(bloom_wire_encode_pbr_manifest(&pm, &re_buf, &re_len));
            BloomPbrManifest pm2 = {0};
            assert(bloom_wire_decode_pbr_manifest(re_buf, re_len, &pm2));
            BloomDigest128 d1 = bloom_pbr_manifest_digest128(&pm);
            BloomDigest128 d2 = bloom_pbr_manifest_digest128(&pm2);
            assert(d1.lo == d2.lo && d1.hi == d2.hi);
            bloom_pbr_manifest_free(&pm2);
            bloom_pbr_manifest_free(&pm);
            free(re_buf);
            printf("  [PASS] Loaded bloom_import.py BLMW (%s, %zu B) bit-exact C17 roundtrip\n", argv[i + 1], re_len);
        }
    }
    printf("ALL CORE TESTS PASSED!\n");
    return 0;
}
