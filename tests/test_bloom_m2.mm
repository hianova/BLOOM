#include "bloom_abi.h"
#include "bloom_atlas.h"
#include "bloom_bobj.h"
#include "bloom_camera.h"
#include "bloom_gesture.h"
#include "bloom_gpu.h"
#include "bloom_scene.h"
#include "bloom_sdk.h"
#include "bloom_ui_kit.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static void atlas_measure_cb(void *userdata, const char *utf8, float font_px,
                             float max_width, float *out_w, float *out_h) {
    BloomAtlas *atlas = static_cast<BloomAtlas *>(userdata);
    bloom_atlas_measure_text(atlas, utf8, font_px, max_width, out_w, out_h);
}

static BloomUiMeta find_ui_meta(const BloomScene *sc, BloomHandle h) {
    BloomUiMeta m{};
    uint32_t cnt = bloom_scene_get_ui_count(sc);
    for (uint32_t i = 1u; i < cnt; ++i) {
        BloomUiMeta tmp{};
        if (bloom_scene_get_ui_meta(sc, i, &tmp) && tmp.handle == h) return tmp;
    }
    assert(false && "UI handle not found in scene metadata");
    return m;
}

static void inject_click(BloomScene *sc, float x, float y) {
    BloomInputEvent dn{};
    dn.type = BLOOM_EVENT_POINTER_DOWN;
    dn.x = x;
    dn.y = y;
    bloom_scene_handle_input(sc, &dn);

    BloomInputEvent up{};
    up.type = BLOOM_EVENT_POINTER_UP;
    up.x = x;
    up.y = y;
    bloom_scene_handle_input(sc, &up);
}

static double median_double(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    size_t n = v.size();
    return (n % 2u == 1u) ? v[n / 2u] : 0.5 * (v[n / 2u - 1u] + v[n / 2u]);
}

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
    hdr.obj_id = 0u;
    hdr.flags = 0u;
    hdr.bsphere_f16[0] = bloom_f32_to_f16(0.0f);
    hdr.bsphere_f16[1] = bloom_f32_to_f16(0.0f);
    hdr.bsphere_f16[2] = bloom_f32_to_f16(0.0f);
    hdr.bsphere_f16[3] = bloom_f32_to_f16(R + r + 0.1f);
    hdr.cone_axis[0] = 0;
    hdr.cone_axis[1] = 0;
    hdr.cone_axis[2] = 127;
    hdr.cone_cutoff = 127;
    hdr.aabb_scale_tag = 0u;

    return bloom_scene_add_template(sc, name, 1u, &hdr, verts, lidx, w);
}

static void test_m2_a1_flex_cjk_sprite_4k(BloomGpu *gpu) {
    BloomScene *sc = bloom_scene_create();
    assert(sc != nullptr);

    BloomAtlas *atlas = bloom_gpu_get_atlas(gpu);
    assert(atlas != nullptr);
    bloom_scene_set_text_measurer(sc, atlas_measure_cb, atlas);

    float mw = 0.0f, mh = 0.0f;
    bloom_atlas_measure_text(atlas, "具身介面\nBLOOM M2", 18.0f, 0.0f, &mw, &mh);
    assert(mw > 40.0f && mh > 30.0f);

    std::vector<uint8_t> checker(32u * 32u * 4u, 0u);
    for (uint32_t y = 0u; y < 32u; ++y) {
        for (uint32_t x = 0u; x < 32u; ++x) {
            bool hi = (((x >> 2u) ^ (y >> 2u)) & 1u) != 0u;
            size_t off = ((size_t)y * 32u + x) * 4u;
            checker[off + 0u] = hi ? 56u : 20u;
            checker[off + 1u] = hi ? 210u : 80u;
            checker[off + 2u] = hi ? 250u : 150u;
            checker[off + 3u] = 255u;
        }
    }
    uint16_t uv[4]{};
    bool added = bloom_atlas_add_sprite_rgba8(atlas, checker.data(), 32u, 32u, uv);
    assert(added && bloom_atlas_sprite_count(atlas) == 1u);

    BloomUiNodeDesc panel{};
    panel.id = "m2_a1_panel";
    panel.kind = BLOOM_UI_PANEL;
    panel.flex_dir = BLOOM_FLEX_ROW;
    panel.justify = BLOOM_JUSTIFY_SPACE_BETWEEN;
    panel.align = BLOOM_ALIGN_CENTER;
    panel.flex_wrap = true;
    panel.x = 40.0f;
    panel.y = 40.0f;
    panel.w = 640.0f;
    panel.h = 180.0f;
    panel.pad = 16.0f;
    panel.gap = 12.0f;
    panel.fill_rgba8 = 0xE8182230u;
    panel.border_rgba8 = 0xFF38D0FFu;
    panel.radius_px = 12.0f;
    panel.border_px = 1.5f;
    BloomHandle hp = bloom_scene_add_ui(sc, &panel);
    assert(hp != BLOOM_HANDLE_NULL);

    BloomUiNodeDesc lbl{};
    lbl.id = "m2_a1_cjk";
    lbl.parent = hp;
    lbl.kind = BLOOM_UI_TEXT;
    lbl.label = "具身介面 / BLOOM M2";
    lbl.font_px = 18.0f;
    lbl.text_rgba8 = 0xFFF2F6FCu;
    lbl.flex_grow = 1.0f;
    assert(bloom_scene_add_ui(sc, &lbl) != BLOOM_HANDLE_NULL);

    BloomUiNodeDesc sp{};
    sp.id = "m2_a1_sprite";
    sp.parent = hp;
    sp.kind = BLOOM_UI_SPRITE;
    sp.w = 64.0f;
    sp.h = 64.0f;
    sp.radius_px = 8.0f;
    memcpy(sp.sprite_uv_unorm16, uv, sizeof(uv));
    sp.sprite_tint_rgba8 = 0xFFFFFFFFu;
    BloomHandle hsp = bloom_scene_add_ui(sc, &sp);
    assert(hsp != BLOOM_HANDLE_NULL);

    BloomUiNodeDesc badge{};
    badge.id = "m2_a1_badge";
    badge.parent = hp;
    badge.kind = BLOOM_UI_BUTTON;
    badge.pos_absolute = true;
    badge.x = 520.0f;
    badge.y = 16.0f;
    badge.w = 96.0f;
    badge.h = 28.0f;
    badge.label = "LIVE";
    badge.font_px = 12.0f;
    badge.fill_rgba8 = 0xFF28C888u;
    badge.text_rgba8 = 0xFF0A1018u;
    badge.radius_px = 6.0f;
    BloomHandle hbadge = bloom_scene_add_ui(sc, &badge);
    assert(hbadge != BLOOM_HANDLE_NULL);

    bloom_scene_layout_ui(sc, 3840.0f, 2160.0f);
    BloomUiMeta m_badge = find_ui_meta(sc, hbadge);
    assert(fabsf(m_badge.x - (40.0f + 520.0f)) < 2.0f);
    assert(fabsf(m_badge.y - (40.0f + 16.0f)) < 2.0f);

    assert(bloom_gpu_render_headless(gpu, sc, 3840u, 2160u, true));
    std::vector<double> gpu_times;
    for (int f = 0; f < 5; ++f) {
        bloom_scene_mark_dirty(sc);
        assert(bloom_gpu_render_headless(gpu, sc, 3840u, 2160u, true));
        gpu_times.push_back(bloom_gpu_get_stats(gpu).last_gpu_ms);
    }

    BloomGpuStats st = bloom_gpu_get_stats(gpu);
    uint32_t chk = bloom_gpu_headless_checksum(gpu);
    double med = median_double(gpu_times);
    std::printf("[Test M2-A1] Flex+CJK+Sprite@4K: med=%.3f ms sprites=%u glyphs=%u chk=0x%08x\n",
                med, bloom_atlas_sprite_count(atlas), st.atlas_glyph_count, chk);
    assert(st.sprite_active);
    assert(chk != 0u);
    assert(med < 2.5);

    bloom_scene_destroy(sc);
}

static void test_m2_a2_ui_kit_ime_widgets_gesture_sdk(BloomGpu *gpu) {
    BloomTheme light = bloom_theme_light();
    BloomTheme dark = bloom_theme_dark();
    assert(light.bg_canvas != dark.bg_canvas);

    BloomScene *sc = bloom_scene_create();
    assert(sc != nullptr);
    bloom_scene_set_text_measurer(sc, atlas_measure_cb, bloom_gpu_get_atlas(gpu));

    BloomHandle hp = bloom_kit_panel(sc, &dark, BLOOM_HANDLE_NULL, "kit_panel",
                                     "Studio Inspector", 40.0f, 40.0f, 460.0f, 680.0f, BLOOM_FLEX_COL);
    BloomHandle hti = bloom_kit_text_input(sc, &dark, hp, "ti_search", "搜尋 / Search", 420.0f, 36.0f);
    BloomHandle hchk = bloom_kit_checkbox(sc, &dark, hp, "chk_pbr", "Enable PBR", 420.0f, 30.0f);
    BloomHandle hsw = bloom_kit_switch(sc, &dark, hp, "sw_vsync", "Zero-Submit Idle", 420.0f, 32.0f);
    BloomHandle hsel = bloom_kit_select(sc, &dark, hp, "sel_backend", "Metal|WebGPU|Vulkan", 420.0f, 36.0f);
    BloomHandle htab = bloom_kit_tabs(sc, &dark, hp, "tab_mode", "Scene|Material|SDK", 420.0f, 34.0f);
    BloomHandle hprog = bloom_kit_progress(sc, &dark, hp, "prog_bake", 0.25f, 420.0f, 14.0f);
    BloomHandle hsv = bloom_kit_scrollview(sc, &dark, hp, "sv_list", 420.0f, 120.0f, BLOOM_FLEX_COL);
    for (int i = 0; i < 12; ++i) {
        char bid[32], blabel[32];
        std::snprintf(bid, sizeof(bid), "sv_item_%d", i);
        std::snprintf(blabel, sizeof(blabel), "Layer %d", i);
        assert(bloom_kit_button(sc, &dark, hsv, bid, blabel, 380.0f, 32.0f) != BLOOM_HANDLE_NULL);
    }
    BloomHandle htoast = bloom_kit_toast(sc, &dark, "toast_ok", "Milestone 2 Ready",
                                         540.0f, 40.0f, 320.0f, 44.0f);
    assert(hp && hti && hchk && hsw && hsel && htab && hprog && hsv && htoast);

    bloom_scene_layout_ui(sc, 1280.0f, 720.0f);

    /* CJK IME Preedit + Commit + UTF-8 Codepoint Backspace */
    assert(bloom_scene_set_ui_focused(sc, hti));
    BloomInputEvent ime{};
    ime.type = BLOOM_EVENT_IME_PREEDIT;
    std::snprintf(ime.text, sizeof(ime.text), "ㄓㄨ");
    assert(bloom_scene_handle_input(sc, &ime));
    assert(std::strcmp(bloom_scene_get_ui_preedit(sc, hti), "ㄓㄨ") == 0);

    BloomInputEvent commit{};
    commit.type = BLOOM_EVENT_TEXT_INPUT;
    std::snprintf(commit.text, sizeof(commit.text), "中文A");
    assert(bloom_scene_handle_input(sc, &commit));
    assert(std::strcmp(bloom_scene_get_ui_preedit(sc, hti), "") == 0);
    assert(std::strcmp(bloom_scene_get_ui_text(sc, hti), "中文A") == 0);

    BloomInputEvent bs{};
    bs.type = BLOOM_EVENT_KEY_DOWN;
    bs.key_code = 51u;
    assert(bloom_scene_handle_input(sc, &bs));
    assert(std::strcmp(bloom_scene_get_ui_text(sc, hti), "中文") == 0);
    assert(bloom_scene_handle_input(sc, &bs));
    assert(std::strcmp(bloom_scene_get_ui_text(sc, hti), "中") == 0);

    /* Checkbox, Switch, Select dropdown, Tabs, ScrollView, Progress */
    BloomUiMeta m_chk = find_ui_meta(sc, hchk);
    BloomUiMeta m_sw = find_ui_meta(sc, hsw);
    inject_click(sc, m_chk.x + m_chk.w * 0.5f, m_chk.y + m_chk.h * 0.5f);
    inject_click(sc, m_sw.x + m_sw.w * 0.5f, m_sw.y + m_sw.h * 0.5f);
    assert(bloom_scene_get_ui_toggle(sc, hchk));
    assert(bloom_scene_get_ui_toggle(sc, hsw));

    BloomUiMeta m_sel = find_ui_meta(sc, hsel);
    inject_click(sc, m_sel.x + m_sel.w * 0.5f, m_sel.y + m_sel.h * 0.5f);
    float opt_h = m_sel.h > 20.0f ? m_sel.h : 34.0f;
    inject_click(sc, m_sel.x + 12.0f, m_sel.y + m_sel.h + 4.0f + 1.5f * opt_h);
    assert(bloom_scene_get_ui_selected(sc, hsel) == 1);

    BloomUiMeta m_tab = find_ui_meta(sc, htab);
    inject_click(sc, m_tab.x + m_tab.w * (2.5f / 3.0f), m_tab.y + m_tab.h * 0.5f);
    assert(bloom_scene_get_ui_selected(sc, htab) == 2);

    BloomUiMeta m_sv = find_ui_meta(sc, hsv);
    BloomInputEvent scr{};
    scr.type = BLOOM_EVENT_SCROLL;
    scr.x = m_sv.x + m_sv.w * 0.5f;
    scr.y = m_sv.y + m_sv.h * 0.5f;
    scr.dy = -3.0f;
    assert(bloom_scene_handle_input(sc, &scr));
    m_sv = find_ui_meta(sc, hsv);
    assert(m_sv.scroll_y > 0.0f);

    assert(bloom_scene_set_ui_progress(sc, hprog, 0.75f));

    /* Embodied SDK Snapshot Grounding across UI Kit widgets */
    BloomSdkSnapshot snap{};
    assert(bloom_sdk_capture_snapshot(gpu, sc, 1280u, 720u, &snap));
    const BloomSdkEntity *e_chk = bloom_sdk_find_by_id(&snap, "chk_pbr");
    const BloomSdkEntity *e_sel = bloom_sdk_find_by_id(&snap, "sel_backend");
    assert(e_chk != nullptr && e_chk->visible_px > 0u && !e_chk->occluded);
    assert(e_sel != nullptr && e_sel->visible_px > 0u && !e_sel->occluded);
    std::printf("[Test M2-A2] UIKit+IME+SDK: chk_px=%u sel_px=%u scroll_y=%.1f ground=%.3f ms\n",
                e_chk->visible_px, e_sel->visible_px, m_sv.scroll_y, snap.ground_ms);
    bloom_sdk_free_snapshot(&snap);

    /* Zero-Alloc Gesture & Keymap State Machine */
    BloomGestureState gs{};
    BloomGestureEvent gev{};
    bloom_gesture_init(&gs);
    assert(bloom_gesture_bind_key(&gs, 1u, BLOOM_MOD_SUPER, 42u));
    assert(bloom_gesture_bind_key(&gs, 0u, 0u, 99u));

    BloomInputEvent ev{};
    ev.type = BLOOM_EVENT_POINTER_DOWN; ev.x = 100.0f; ev.y = 100.0f;
    bloom_gesture_feed(&gs, &ev, 1.00, false, &gev);
    ev.type = BLOOM_EVENT_POINTER_UP; ev.x = 101.0f; ev.y = 100.0f;
    assert(bloom_gesture_feed(&gs, &ev, 1.05, false, &gev));
    assert(gev.kind == BLOOM_GESTURE_CLICK);

    ev.type = BLOOM_EVENT_POINTER_DOWN; ev.x = 101.0f; ev.y = 100.0f;
    bloom_gesture_feed(&gs, &ev, 1.20, false, &gev);
    ev.type = BLOOM_EVENT_POINTER_UP; ev.x = 101.0f; ev.y = 100.0f;
    assert(bloom_gesture_feed(&gs, &ev, 1.25, false, &gev));
    assert(gev.kind == BLOOM_GESTURE_DOUBLE_CLICK);

    ev.type = BLOOM_EVENT_POINTER_DOWN; ev.x = 200.0f; ev.y = 200.0f;
    bloom_gesture_feed(&gs, &ev, 2.00, false, &gev);
    ev.type = BLOOM_EVENT_POINTER_MOVE; ev.x = 230.0f; ev.y = 220.0f;
    assert(bloom_gesture_feed(&gs, &ev, 2.05, false, &gev));
    assert(gev.kind == BLOOM_GESTURE_DRAG_START);
    ev.type = BLOOM_EVENT_POINTER_MOVE; ev.x = 260.0f; ev.y = 240.0f;
    assert(bloom_gesture_feed(&gs, &ev, 2.10, false, &gev));
    assert(gev.kind == BLOOM_GESTURE_DRAG_MOVE);
    ev.type = BLOOM_EVENT_POINTER_UP; ev.x = 260.0f; ev.y = 240.0f;
    assert(bloom_gesture_feed(&gs, &ev, 2.15, false, &gev));
    assert(gev.kind == BLOOM_GESTURE_DRAG_END);

    ev.type = BLOOM_EVENT_POINTER_DOWN; ev.x = 300.0f; ev.y = 300.0f;
    bloom_gesture_feed(&gs, &ev, 3.00, false, &gev);
    assert(bloom_gesture_poll_long_press(&gs, 3.70, &gev));
    assert(gev.kind == BLOOM_GESTURE_LONG_PRESS);
    ev.type = BLOOM_EVENT_POINTER_UP; ev.x = 300.0f; ev.y = 300.0f;
    bloom_gesture_feed(&gs, &ev, 3.75, false, &gev);

    BloomInputEvent pinch{};
    pinch.type = BLOOM_EVENT_PINCH;
    pinch.x = 320.0f; pinch.y = 240.0f; pinch.scale_delta = 0.15f;
    assert(bloom_gesture_feed(&gs, &pinch, 4.00, false, &gev));
    assert(gev.kind == BLOOM_GESTURE_PINCH);

    BloomInputEvent cmd_s{};
    cmd_s.type = BLOOM_EVENT_KEY_DOWN; cmd_s.key_code = 1u; cmd_s.modifiers = BLOOM_MOD_SUPER;
    assert(bloom_gesture_feed(&gs, &cmd_s, 4.10, true, &gev));
    assert(gev.kind == BLOOM_GESTURE_ACTION && gev.action_id == 42u);

    BloomInputEvent bare_a{};
    bare_a.type = BLOOM_EVENT_KEY_DOWN; bare_a.key_code = 0u; bare_a.modifiers = 0u;
    assert(!bloom_gesture_feed(&gs, &bare_a, 4.20, true, &gev));
    assert(bloom_gesture_feed(&gs, &bare_a, 4.30, false, &gev));
    assert(gev.kind == BLOOM_GESTURE_ACTION && gev.action_id == 99u);

    bloom_scene_destroy(sc);
}

static std::vector<uint8_t> build_ktx2_rgba8(uint32_t w, uint32_t h) {
    size_t px_bytes = (size_t)w * (size_t)h * 4u;
    std::vector<uint8_t> blob(104u + px_bytes, 0u);
    static const uint8_t kMagic[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32, 0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
    std::memcpy(blob.data(), kMagic, 12u);
    uint32_t vk_fmt = 37u; /* VK_FORMAT_R8G8B8A8_UNORM */
    uint32_t type_sz = 1u;
    uint32_t lvl_cnt = 1u;
    uint32_t sc = 0u;
    uint64_t off = 104u;
    uint64_t len = (uint64_t)px_bytes;
    std::memcpy(blob.data() + 12u, &vk_fmt, 4u);
    std::memcpy(blob.data() + 16u, &type_sz, 4u);
    std::memcpy(blob.data() + 20u, &w, 4u);
    std::memcpy(blob.data() + 24u, &h, 4u);
    std::memcpy(blob.data() + 40u, &lvl_cnt, 4u);
    std::memcpy(blob.data() + 44u, &sc, 4u);
    std::memcpy(blob.data() + 80u, &off, 8u);
    std::memcpy(blob.data() + 88u, &len, 8u);
    for (uint32_t y = 0u; y < h; ++y) {
        for (uint32_t x = 0u; x < w; ++x) {
            size_t p = 104u + ((size_t)y * w + x) * 4u;
            blob[p + 0u] = 240u;
            blob[p + 1u] = (uint8_t)(160u + ((x * 4u) & 63u));
            blob[p + 2u] = 48u;
            blob[p + 3u] = 255u;
        }
    }
    return blob;
}

static void test_m2_b1_pbr_tex_ktx2_4k(BloomGpu *gpu) {
    uint32_t base_tex = bloom_gpu_get_stats(gpu).uploaded_textures;
    std::vector<uint8_t> albedo(64u * 64u * 4u);
    std::vector<uint8_t> mr(64u * 64u * 4u);
    std::vector<uint8_t> nrm(64u * 64u * 4u);
    for (uint32_t y = 0u; y < 64u; ++y) {
        for (uint32_t x = 0u; x < 64u; ++x) {
            size_t i = ((size_t)y * 64u + x) * 4u;
            bool chk = (((x >> 3u) ^ (y >> 3u)) & 1u) != 0u;
            albedo[i + 0u] = chk ? 225u : 45u;
            albedo[i + 1u] = chk ? 235u : 120u;
            albedo[i + 2u] = chk ? 245u : 200u;
            albedo[i + 3u] = 255u;

            mr[i + 0u] = 255u;
            mr[i + 1u] = chk ? 70u : 190u;   /* G = roughness */
            mr[i + 2u] = chk ? 220u : 30u;   /* B = metallic */
            mr[i + 3u] = 255u;

            nrm[i + 0u] = (uint8_t)(128 + (int)(sinf((float)x * 0.4f) * 32.0f));
            nrm[i + 1u] = (uint8_t)(128 + (int)(cosf((float)y * 0.4f) * 32.0f));
            nrm[i + 2u] = 248u;
            nrm[i + 3u] = 255u;
        }
    }
    uint16_t s_alb = bloom_gpu_upload_texture_rgba8(gpu, albedo.data(), 64u, 64u, true);
    uint16_t s_mr  = bloom_gpu_upload_texture_rgba8(gpu, mr.data(), 64u, 64u, true);
    uint16_t s_nrm = bloom_gpu_upload_texture_rgba8(gpu, nrm.data(), 64u, 64u, true);
    assert(s_alb == base_tex + 1u && s_mr == base_tex + 2u && s_nrm == base_tex + 3u);

    std::vector<uint8_t> ktx2 = build_ktx2_rgba8(32u, 32u);
    uint16_t s_ktx = bloom_gpu_upload_ktx2(gpu, ktx2.data(), ktx2.size());
    assert(s_ktx == base_tex + 4u);
    assert(bloom_gpu_get_stats(gpu).uploaded_textures == base_tex + 4u);

    BloomScene *sc = bloom_scene_create();
    assert(sc != nullptr);

    BloomMaterial32 mat0{};
    mat0.base_rgba8 = 0xFFFFFFFFu;
    mat0.emissive_rgba8 = 0xFF040810u;
    mat0.metallic_f16 = bloom_f32_to_f16(0.85f);
    mat0.roughness_f16 = bloom_f32_to_f16(0.32f);
    mat0.ao_f16 = bloom_f32_to_f16(1.0f);
    mat0.normal_scale_f16 = bloom_f32_to_f16(1.0f);
    mat0.albedo_tex = s_alb;
    mat0.mr_tex = s_mr;
    mat0.normal_tex = s_nrm;
    mat0.flags = BLOOM_MAT_FLAG_PBR;
    uint16_t m0 = bloom_scene_add_material(sc, &mat0);

    BloomMaterial32 mat1{};
    mat1.base_rgba8 = 0xFF38C8FFu;
    mat1.metallic_f16 = bloom_f32_to_f16(0.95f);
    mat1.roughness_f16 = bloom_f32_to_f16(0.20f);
    mat1.ao_f16 = bloom_f32_to_f16(1.0f);
    mat1.normal_scale_f16 = bloom_f32_to_f16(1.0f);
    mat1.albedo_tex = s_ktx;
    mat1.flags = BLOOM_MAT_FLAG_PBR;
    uint16_t m1 = bloom_scene_add_material(sc, &mat1);

    BloomLight32 sun{};
    sun.pos_or_dir[0] = 0.4f; sun.pos_or_dir[1] = -0.7f; sun.pos_or_dir[2] = 0.58f;
    sun.rgba8_color = 0xFFF2FAFFu;
    sun.intensity = 2.6f;
    sun.kind = BLOOM_LIGHT_DIR;
    assert(bloom_scene_add_light(sc, &sun) == 0u);

    BloomLight32 pt0{};
    pt0.pos_or_dir[0] = -4.0f; pt0.pos_or_dir[1] = 2.5f; pt0.pos_or_dir[2] = 10.0f;
    pt0.range = 18.0f; pt0.rgba8_color = 0xFFFF9040u; pt0.intensity = 8.0f; pt0.kind = BLOOM_LIGHT_POINT;
    assert(bloom_scene_add_light(sc, &pt0) == 1u);

    BloomLight32 pt1{};
    pt1.pos_or_dir[0] = 4.0f; pt1.pos_or_dir[1] = 2.5f; pt1.pos_or_dir[2] = 10.0f;
    pt1.range = 18.0f; pt1.rgba8_color = 0xFF40D8FFu; pt1.intensity = 8.0f; pt1.kind = BLOOM_LIGHT_POINT;
    assert(bloom_scene_add_light(sc, &pt1) == 2u);

    uint16_t tpl = build_torus_template(sc, "pbr_torus");
    float qid[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float sc1[3] = {0.85f, 0.85f, 0.85f};
    for (int r = 0; r < 8; ++r) {
        for (int c = 0; c < 8; ++c) {
            char oid[32];
            std::snprintf(oid, sizeof(oid), "pbr_obj_%d_%d", r, c);
            float pos[3] = {((float)c - 3.5f) * 2.1f, ((float)r - 3.5f) * 2.1f, 14.0f};
            BloomHandle ho = bloom_scene_spawn_object(sc, oid, tpl, pos, qid, sc1, 0xFFFFFFFFu, nullptr);
            assert(ho != BLOOM_HANDLE_NULL);
            assert(bloom_scene_set_object_material(sc, ho, ((r + c) & 1) ? m1 : m0));
        }
    }
    assert(bloom_scene_has_pbr_objects(sc));

    assert(bloom_gpu_render_headless(gpu, sc, 3840u, 2160u, true));
    assert(bloom_gpu_render_headless(gpu, sc, 3840u, 2160u, true));

    std::vector<double> gpu_times;
    for (int f = 0; f < 10; ++f) {
        bloom_scene_mark_dirty(sc);
        assert(bloom_gpu_render_headless(gpu, sc, 3840u, 2160u, true));
        gpu_times.push_back(bloom_gpu_get_stats(gpu).last_gpu_ms);
    }
    BloomGpuStats st = bloom_gpu_get_stats(gpu);
    uint32_t chk_pbr = bloom_gpu_headless_checksum(gpu);
    double med = median_double(gpu_times);
    std::printf("[Test M2-B1] PBR+KTX2@4K (64 tori): med=%.3f ms tex=%u chk=0x%08x\n",
                med, st.uploaded_textures, chk_pbr);
    assert(st.pbr_active);
    assert(chk_pbr != 0u);
    assert(med < 2.5);

    uint64_t before_cbs = st.submitted_cbs;
    uint64_t before_skip = st.idle_skipped_frames;
    assert(bloom_gpu_render_headless(gpu, sc, 3840u, 2160u, false));
    BloomGpuStats after = bloom_gpu_get_stats(gpu);
    assert(after.submitted_cbs == before_cbs);
    assert(after.idle_skipped_frames == before_skip + 1u);

    bloom_scene_destroy(sc);
}

static void test_m2_b2_camera_raycast(void) {
    BloomCamController c{};
    float tgt[3] = {0.0f, 0.0f, 8.0f};
    bloom_cam_init_orbit(&c, tgt, 0.0f, 0.2f, 10.0f);
    bloom_cam_orbit_drag(&c, 50.0f, -20.0f);
    bloom_cam_orbit_pan(&c, 10.0f, 5.0f);
    bloom_cam_orbit_zoom(&c, 2.0f);
    BloomCamera cam{};
    for (int i = 0; i < 60; ++i) cam = bloom_cam_step(&c, 1.0f / 60.0f);
    assert(std::isfinite(cam.yaw) && std::isfinite(cam.pos[2]));
    assert(fabsf(cam.yaw) > 0.01f && fabsf(c.distance - 10.0f) > 0.01f);

    float fly_pos[3] = {0.0f, 1.0f, 0.0f};
    bloom_cam_init_fly(&c, fly_pos, 0.0f, 0.0f);
    bloom_cam_fly_move(&c, 1.0f, 0.5f, 0.25f, 0.5f);
    cam = bloom_cam_step(&c, 1.0f / 60.0f);
    assert(cam.pos[2] > 0.5f && cam.pos[0] > 0.2f && cam.pos[1] > 1.05f);

    float offset[3] = {0.0f, 3.0f, -6.0f};
    float new_tgt[3] = {10.0f, 0.0f, 5.0f};
    bloom_cam_init_follow(&c, offset, 8.0f);
    bloom_cam_follow_set_target(&c, new_tgt);
    bloom_cam_add_trauma(&c, 0.8f);
    assert(c.trauma > 0.5f);
    cam = bloom_cam_step(&c, 1.0f / 60.0f);
    assert(c.trauma < 0.8f);
    for (int i = 0; i < 180; ++i) cam = bloom_cam_step(&c, 1.0f / 60.0f);
    assert(c.trauma == 0.0f);
    assert(fabsf(cam.pos[0] - 10.0f) < 0.25f && fabsf(cam.pos[1] - 3.0f) < 0.25f);

    BloomCamera rc = {{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f, 1.8f, 0.5f, 100.0f};
    float ro[3]{}, rd[3]{};
    bloom_cam_screen_ray(&rc, 1280.0f, 720.0f, 640.0f, 360.0f, ro, rd);
    assert(fabsf(rd[2] - 1.0f) < 1e-4f);

    float hit_c[3] = {0.0f, 0.0f, 10.0f};
    float miss_c[3] = {10.0f, 0.0f, 10.0f};
    float t_hit = 0.0f;
    assert(bloom_cam_ray_sphere(ro, rd, hit_c, 1.5f, &t_hit) && fabsf(t_hit - 8.5f) < 1e-3f);
    assert(!bloom_cam_ray_sphere(ro, rd, miss_c, 1.5f, &t_hit));

    float bmin[3] = {-1.0f, -1.0f, 9.0f};
    float bmax[3] = {1.0f, 1.0f, 11.0f};
    float mmin[3] = {5.0f, 5.0f, 9.0f};
    float mmax[3] = {6.0f, 6.0f, 11.0f};
    assert(bloom_cam_ray_aabb(ro, rd, bmin, bmax, &t_hit) && fabsf(t_hit - 9.0f) < 1e-3f);
    assert(!bloom_cam_ray_aabb(ro, rd, mmin, mmax, &t_hit));
    std::printf("[Test M2-B2] Camera(Orbit/Fly/Follow/Trauma) & Raycast PASSED\n");
}

static void test_m2_b3_multi_glb_bobc(BloomGpu *gpu, const char *multi_bobc_path) {
    BloomBobjDoc doc{};
    assert(bloom_bobj_load_file(multi_bobc_path, &doc));
    assert(doc.template_cnt == 2u && doc.object_cnt == 3u);

    BloomScene *sc = bloom_scene_create();
    assert(sc != nullptr);
    assert(bloom_scene_load_bobj(sc, &doc, false));
    bloom_scene_sync_transforms(sc);
    assert(bloom_scene_get_object_slot_count(sc) >= 4u);

    assert(bloom_gpu_render_headless(gpu, sc, 1280u, 720u, true));
    uint32_t chk = bloom_gpu_headless_checksum(gpu);
    std::printf("[Test M2-B3] Multi-Node glTF BOBC (%s): tpls=%u objs=%u chk=0x%08x\n",
                multi_bobc_path, doc.template_cnt, doc.object_cnt, chk);
    assert(chk != 0u);

    bloom_scene_destroy(sc);
    bloom_bobj_free(&doc);
}

int main(int argc, char **argv) {
    const char *multi_bobc = "build/multi_sample.bobc";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--multi-bobc") == 0) {
            multi_bobc = argv[i + 1];
        }
    }

    BloomGpu *gpu = bloom_gpu_create(nullptr, "shaders/bloom_core.metal");
    assert(gpu != nullptr);

    test_m2_a1_flex_cjk_sprite_4k(gpu);
    test_m2_a2_ui_kit_ime_widgets_gesture_sdk(gpu);
    test_m2_b1_pbr_tex_ktx2_4k(gpu);
    test_m2_b2_camera_raycast();
    test_m2_b3_multi_glb_bobc(gpu, multi_bobc);

    bloom_gpu_destroy(gpu);
    std::printf("ALL MILESTONE-2 TESTS PASSED!\n");
    return 0;
}
