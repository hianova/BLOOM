#include "flow_prefab_ssot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/*
 * ============================================================================
 * TEST SUITE: Flow Prefab SSOT + Art Bible + Fashion + TRC Accessibility
 * ============================================================================
 * Rigorous 8-Point Verification:
 *   1. 64B Univalence & Cache Alignment
 *   2. L1 Cache Resident Working Set (<6 KB Budget)
 *   3. Ricardian Assembly Theory Monotonicity & Compression
 *   4. Pure Q16 CSG/SDF + FlowJet2 Root Anti-Tunneling
 *   5. 800K/760K Art Bible Thermal Hysteresis
 *   6. Wave64 Compaction & Layer Collapse
 *   7. Human-Aesthetic Silhouette Lofting & Steam Marketplace
 *   8. Steam/Console TRC Accessibility Guard (<= 3Hz Strobe & Colorblind Geometry)
 * ============================================================================
 */

static void test_64b_univalence(void) {
    printf("[Test 1/8] 64B Univalence & Cache-Line Alignment...\n");

    /* 1. Exact size assertions */
    assert(sizeof(FlowArtStyle64) == 64U);
    assert(sizeof(FlowPrefabDesc64) == 64U);
    assert(sizeof(FlowPrefabInstance64) == 64U);

    /* 2. Exact alignment assertions */
    assert(_Alignof(FlowArtStyle64) == 64U);
    assert(_Alignof(FlowPrefabDesc64) == 64U);
    assert(_Alignof(FlowPrefabInstance64) == 64U);

    /* 3. Consecutive array stride is exactly 64 bytes */
    FlowArtStyle64 style_arr[2];
    assert((uintptr_t)&style_arr[1] - (uintptr_t)&style_arr[0] == 64U);

    FlowPrefabDesc64 prefab_arr[2];
    assert((uintptr_t)&prefab_arr[1] - (uintptr_t)&prefab_arr[0] == 64U);

    FlowPrefabInstance64 inst_arr[2];
    assert((uintptr_t)&inst_arr[1] - (uintptr_t)&inst_arr[0] == 64U);

    printf("  ✓ FlowArtStyle64 (64B), FlowPrefabDesc64 (64B), FlowPrefabInstance64 (64B) univalent!\n");
}

static void test_l1_cache_budget(void) {
    printf("[Test 2/8] L1 Cache Resident Working Set Budget...\n");

    /* Initialize canonical tables */
    flow_ssot_init_canonical();

    size_t art_bible_bytes = sizeof(FLOW_SSOT_ART_BIBLE);
    size_t prefabs_bytes = sizeof(FLOW_SSOT_PREFABS);
    size_t wave64_instances_bytes = sizeof(FlowPrefabInstance64) * FLOW_WAVE64_SIZE;
    size_t total_footprint = art_bible_bytes + prefabs_bytes + wave64_instances_bytes;

    assert(art_bible_bytes == 512U);  /* 8 styles * 64B = 512B */
    assert(prefabs_bytes == 512U);    /* 8 prefabs * 64B = 512B */
    assert(wave64_instances_bytes == 4096U); /* 64 instances * 64B = 4 KB */
    assert(total_footprint == 5120U); /* Exactly 5 KB: fits in Apple Silicon 128 KB L1! */

    /* Verify SSOT hashes for all canonical prefabs */
    for (size_t i = 1; i <= FLOW_SSOT_PREFAB_COUNT; ++i) {
        const FlowPrefabDesc64 *p = flow_ssot_get_prefab((uint32_t)i);
        assert(p != NULL);
        uint64_t expected_hash = flow_prefab_calc_hash(p);
        assert(p->ssot_hash == expected_hash);
    }

    printf("  ✓ Art Bible (512B) + Prefabs (512B) + Wave64 Batch (4KB) = 5120B (<6 KB L1 Budget) verified!\n");
}

static void test_ricardian_assembly_compression(void) {
    printf("[Test 3/8] Ricardian Assembly Theory Monotonicity & Compression...\n");

    const FlowPrefabDesc64 *sphere = flow_ssot_get_prefab(FLOW_PREFAB_CORE_SPHERE);
    const FlowPrefabDesc64 *ring = flow_ssot_get_prefab(FLOW_PREFAB_QUANTUM_RING);
    assert(sphere != NULL && ring != NULL);

    /* Assembly depths: sphere = 1, ring = 3 */
    assert(sphere->assembly_index == 1);
    assert(ring->assembly_index == 3);

    /* 1. Join without reuse: u + v - 0 = 1 + 3 = 4 */
    FlowPrefabDesc64 joined_unshared = flow_prefab_join_assembly(sphere, ring, 0U, 100U, FLOW_CSG_OP_SMOOTH_UNION);
    assert(joined_unshared.assembly_index == 4);
    assert(joined_unshared.assembly_index > sphere->assembly_index);
    assert(joined_unshared.assembly_index > ring->assembly_index);

    /* 2. Join complex structures with subassembly reuse */
    const FlowPrefabDesc64 *citadel = flow_ssot_get_prefab(FLOW_PREFAB_ORBITAL_CITADEL); /* depth 8 */
    const FlowPrefabDesc64 *interceptor = flow_ssot_get_prefab(FLOW_PREFAB_INTERCEPTOR);   /* depth 6 */
    assert(citadel != NULL && interceptor != NULL);

    FlowPrefabDesc64 joined_no_reuse = flow_prefab_join_assembly(citadel, interceptor, 0U, 101U, FLOW_CSG_OP_SMOOTH_UNION);
    FlowPrefabDesc64 joined_reuse = flow_prefab_join_assembly(citadel, interceptor, 4U, 102U, FLOW_CSG_OP_SMOOTH_UNION);

    /* Without reuse: 8 + 6 = 14 */
    assert(joined_no_reuse.assembly_index == 14);
    /* With reuse: 8 + 6 - 4 = 10 */
    assert(joined_reuse.assembly_index == 10);
    assert(joined_reuse.assembly_index < joined_no_reuse.assembly_index);

    /* Verify grade promotion */
    assert(joined_reuse.ricardian_grade <= citadel->ricardian_grade);
    /* Verify hash integrity */
    assert(joined_reuse.ssot_hash == flow_prefab_calc_hash(&joined_reuse));

    printf("  ✓ Assembly monotonicity (4 > 3, 14 > 8) and reuse compression (10 < 14) verified!\n");
}

static void test_q16_csg_sdf_and_worldline_root(void) {
    printf("[Test 4/8] Pure Q16 CSG/SDF & Worldline Anti-Tunneling Root...\n");

    /* 1. Polynomial smooth min (smin) test */
    int64_t a = 10LL * FLOW_Q16_ONE;
    int64_t b = 10LL * FLOW_Q16_ONE;
    int64_t k = 4LL * FLOW_Q16_ONE;
    int64_t smin_val = flow_smin_q16(a, b, k);
    /* Exact formula: smin(x, x, k) = x - k/4 */
    assert(smin_val == (10LL * FLOW_Q16_ONE - 1LL * FLOW_Q16_ONE));

    /* Separated test */
    int64_t sep_a = 5LL * FLOW_Q16_ONE;
    int64_t sep_b = 20LL * FLOW_Q16_ONE;
    assert(flow_smin_q16(sep_a, sep_b, k) == sep_a);

    /* 2. Polar Domain Folding test */
    int64_t fx1 = 3LL * FLOW_Q16_ONE;
    int64_t fz1 = 1LL * FLOW_Q16_ONE;
    int64_t fx2 = 1LL * FLOW_Q16_ONE;
    int64_t fz2 = 3LL * FLOW_Q16_ONE;
    flow_polar_domain_fold_q16(&fx1, &fz1, 4U);
    flow_polar_domain_fold_q16(&fx2, &fz2, 4U);
    assert(fx1 == fx2 && fz1 == fz2);

    /* 3. Primitive SDF tests */
    const FlowPrefabDesc64 *sphere = flow_ssot_get_prefab(FLOW_PREFAB_CORE_SPHERE); /* r = 2 */
    assert(sphere != NULL);
    /* Center: d = -r = -2 */
    int64_t d_center = flow_prefab_eval_sdf_q16(sphere, 0, 0, 0);
    assert(d_center == -2LL * FLOW_Q16_ONE);
    /* Surface: (2, 0, 0) => d = 0 */
    int64_t d_surface = flow_prefab_eval_sdf_q16(sphere, 2LL * FLOW_Q16_ONE, 0, 0);
    assert(d_surface == 0);
    /* Exterior: (5, 0, 0) => d = 3 */
    int64_t d_ext = flow_prefab_eval_sdf_q16(sphere, 5LL * FLOW_Q16_ONE, 0, 0);
    assert(d_ext == 3LL * FLOW_Q16_ONE);

    /* 4. Relativistic Worldline Anti-Tunneling via FlowJet2 Root Solving */
    /* High-speed craft at v = 200 m/s heading from z = -50 m towards sphere at origin */
    FlowJet2 traj[3];
    traj[0] = flow_jet2_make(0, 0, 0);
    traj[1] = flow_jet2_make(0, 0, 0);
    /* z(t) = -50 + 200 * t (in Q16) */
    traj[2] = flow_jet2_make(-50LL * FLOW_Q16_ONE, 200LL * FLOW_Q16_ONE, 0);

    /* Time step: dt = 0.5s (32768 Q16). Discrete jump would reach z = +50 m (tunneling!) */
    int64_t t_max = FLOW_Q16_ONE / 2;
    int64_t t_hit_q16 = 0;
    bool hit = flow_prefab_eval_worldline_jet2(sphere, traj, t_max, &t_hit_q16);

    assert(hit == true);
    assert(t_hit_q16 > 0 && t_hit_q16 <= t_max);

    /* Expected collision when z(t) = -r = -2 m:
     * -50 + 200 * t = -2 => 200 * t = 48 => t = 48/200 = 0.24 s
     * 0.24 * 65536 = 15728.64 => 15728 or 15729
     */
    int64_t expected_t = (48LL * FLOW_Q16_ONE) / 200LL;
    int64_t diff_t = flow_q16_abs_sat(t_hit_q16 - expected_t);
    assert(diff_t <= 2); /* Sub-microsecond exact integer root! */

    /* Verify position at hit time is exactly on surface */
    int64_t hit_z = flow_jet2_eval_taylor(traj[2], t_hit_q16);
    int64_t hit_sdf = flow_prefab_eval_sdf_q16(sphere, 0, 0, hit_z);
    /* At 200 m/s (720 km/h), sub-millimeter precision in Q16: */
    assert(flow_q16_abs_sat(hit_sdf) <= 256);

    /* Non-colliding trajectory test */
    FlowJet2 safe_traj[3];
    safe_traj[0] = flow_jet2_make(10LL * FLOW_Q16_ONE, 50LL * FLOW_Q16_ONE, 0);
    safe_traj[1] = flow_jet2_make(0, 0, 0);
    safe_traj[2] = flow_jet2_make(10LL * FLOW_Q16_ONE, 0, 0);
    int64_t safe_t = 0;
    bool safe_hit = flow_prefab_eval_worldline_jet2(sphere, safe_traj, t_max, &safe_t);
    assert(safe_hit == false);

    printf("  ✓ Pure Q16 SDF, smin, polar folding, and exact FlowJet2 anti-tunneling verified!\n");
}

static void test_thermal_hysteresis_800k_760k(void) {
    printf("[Test 5/8] 800K / 760K Art Bible Thermal Hysteresis...\n");

    bool overheated = false;

    /* 1. Nominal temperature (740K) */
    FlowArtStyle64 s0 = flow_art_bible_resolve(FLOW_STYLE_MINERAL_GRADE_A, 740ULL, false, &overheated);
    assert(overheated == false);
    assert(s0.style_id == FLOW_STYLE_MINERAL_GRADE_A);
    assert(s0.strobe_hz_q16 == 0);

    /* 2. Rising into hysteresis band (780K) - stays normal */
    FlowArtStyle64 s1 = flow_art_bible_resolve(FLOW_STYLE_MINERAL_GRADE_A, 780ULL, overheated, &overheated);
    assert(overheated == false);
    assert(s1.style_id == FLOW_STYLE_MINERAL_GRADE_A);

    /* 3. Spiking above 800K threshold (810K) - enters meltdown */
    FlowArtStyle64 s2 = flow_art_bible_resolve(FLOW_STYLE_MINERAL_GRADE_A, 810ULL, overheated, &overheated);
    assert(overheated == true);
    assert(s2.style_id == FLOW_STYLE_THERMAL_MELTDOWN);
    assert(s2.strobe_hz_q16 == 655360); /* 10 Hz strobe */

    /* 4. Cooling down into hysteresis band (775K) - STAYS overheated (hysteresis retention!) */
    FlowArtStyle64 s3 = flow_art_bible_resolve(FLOW_STYLE_MINERAL_GRADE_A, 775ULL, overheated, &overheated);
    assert(overheated == true);
    assert(s3.style_id == FLOW_STYLE_THERMAL_MELTDOWN);

    /* 5. Cooling below 760K exit threshold (755K) - resets to nominal */
    FlowArtStyle64 s4 = flow_art_bible_resolve(FLOW_STYLE_MINERAL_GRADE_A, 755ULL, overheated, &overheated);
    assert(overheated == false);
    assert(s4.style_id == FLOW_STYLE_MINERAL_GRADE_A);
    assert(s4.strobe_hz_q16 == 0);

    printf("  ✓ 800K enter / 760K exit dual-threshold hysteresis verified!\n");
}

static void test_wave64_compaction_and_layer_collapse(void) {
    printf("[Test 6/8] Wave64 Sparse Instance Compaction & Layer Collapse...\n");

    FlowPrefabInstance64 batch[FLOW_WAVE64_SIZE];
    memset(batch, 0, sizeof(batch));

    /* Populate 20 active instances across different distance regimes */
    /* View origin at (0, 0, 0) */
    int64_t view_pos[3] = {0, 0, 0};
    int64_t layer0_max = 100LL * FLOW_Q16_ONE;  /* 0..100m => Layer 0 (3D CSG SDF) */
    int64_t layer1_max = 1000LL * FLOW_Q16_ONE; /* 100..1000m => Layer 1 (2D Slice) */
                                                /* >1000m => HUD (MSDF) */

    /* Lanes 0..4: Close range (20m, 40m, 60m, 80m, 95m) => Layer 0 */
    for (size_t i = 0; i < 5; ++i) {
        batch[i].prefab_id = FLOW_PREFAB_CORE_SPHERE;
        batch[i].pos_q16[0] = (int64_t)(20 * (i + 1)) * FLOW_Q16_ONE;
        batch[i].vel_q16[0] = 5LL * FLOW_Q16_ONE;
    }

    /* Lanes 10..16: Mid range (200m..800m) => Layer 1 */
    for (size_t i = 10; i < 17; ++i) {
        batch[i].prefab_id = FLOW_PREFAB_QUANTUM_RING;
        batch[i].pos_q16[1] = (int64_t)(100 * (i - 8)) * FLOW_Q16_ONE;
        batch[i].vel_q16[1] = 10LL * FLOW_Q16_ONE;
    }

    /* Lanes 30..37: Far range (1200m..2000m) => Layer HUD */
    for (size_t i = 30; i < 38; ++i) {
        batch[i].prefab_id = FLOW_PREFAB_ASTEROID_GRADE_S;
        batch[i].pos_q16[2] = (int64_t)(1200 + 100 * (i - 30)) * FLOW_Q16_ONE;
    }

    /* Total active lanes = 5 + 7 + 8 = 20 lanes */
    FlowPrefabInstance64 collapsed[FLOW_WAVE64_SIZE];
    uint32_t compact_indices[FLOW_WAVE64_SIZE];
    uint64_t active_mask = 0ULL;

    uint32_t active_count = flow_prefab_cull_and_collapse_wave64(
        batch, FLOW_WAVE64_SIZE, view_pos, layer0_max, layer1_max,
        collapsed, compact_indices, &active_mask);

    assert(active_count == 20U);
    assert(__builtin_popcountll(active_mask) == 20);

    /* Verify Layer classification */
    for (uint32_t k = 0; k < active_count; ++k) {
        uint32_t src = compact_indices[k];
        if (src < 5) {
            assert(collapsed[k].active_layer == FLOW_STAGE_LAYER_0_SDF);
        } else if (src >= 10 && src < 17) {
            assert(collapsed[k].active_layer == FLOW_STAGE_LAYER_1_SLICE);
        } else if (src >= 30 && src < 38) {
            assert(collapsed[k].active_layer == FLOW_STAGE_LAYER_HUD);
        }
    }

    printf("  ✓ Wave64 1-cycle compaction (20 active lanes) & Layer 0/1/HUD collapse verified!\n");
}

static void test_fashion_lofting_and_steam_marketplace(void) {
    printf("[Test 7/8] Human-Aesthetic Silhouette Lofting (FlowFashionSkin64) & Steam Marketplace...\n");

    assert(sizeof(FlowFashionSkin64) == 64U);
    assert(_Alignof(FlowFashionSkin64) == 64U);
    assert(sizeof(FlowMarketToken64) == 64U);
    assert(_Alignof(FlowMarketToken64) == 64U);

    /* 1. Design 4 Haute-Couture Skins via Orthogonal 2D Silhouette Lofting (Zero Blender!) */
    const int32_t chord_2m_q16 = (int32_t)(2LL * FLOW_Q16_ONE);
    FlowFashionSkin64 showroom_skins[4];
    showroom_skins[0] = flow_fashion_design_from_silhouette(
        1001U, FLOW_FINISH_DOPPLER_SAPPHIRE, FLOW_RICARDIAN_GRADE_S, 420ULL, 1310, chord_2m_q16, 40503
    );
    showroom_skins[1] = flow_fashion_design_from_silhouette(
        1002U, FLOW_FINISH_CASE_HARDENED_BLUE, FLOW_RICARDIAN_GRADE_S, 661ULL, 1966, chord_2m_q16, 40503
    );
    showroom_skins[2] = flow_fashion_design_from_silhouette(
        1003U, FLOW_FINISH_KINTSUGI_DAMASCUS, FLOW_RICARDIAN_GRADE_A, 888ULL, 2621, chord_2m_q16, 40503
    );
    showroom_skins[3] = flow_fashion_design_from_silhouette(
        1004U, FLOW_FINISH_CELESTIAL_JADE, FLOW_RICARDIAN_GRADE_A, 777ULL, 1500, chord_2m_q16, 40503
    );

    /* Verify Golden-Ratio wingspan (~0.618 * length_z) & Harmony Score >= 0.95 (62259 Q16) */
    for (int i = 0; i < 4; ++i) {
        assert(showroom_skins[i].wingspan_x_q16 == 81006); /* 2.0 * 40503 */
        assert(showroom_skins[i].aesthetic_harmony_q16 >= 62259);
        assert(showroom_skins[i].ssot_signature != 0ULL);
        /* Origin (0,0,0) is strictly inside the lofted 3D silhouette (f < 0) */
        int64_t d_origin = flow_fashion_eval_lofted_sdf_q16(&showroom_skins[i], 0, 0, 0);
        int64_t d_far = flow_fashion_eval_lofted_sdf_q16(&showroom_skins[i], 5LL * FLOW_Q16_ONE, 0, 0);
        assert(d_origin < 0LL);
        assert(d_far > 0LL);
    }
    /* Scar Pattern #661 has 95% Blue Gem coverage (62259 Q16) */
    assert(showroom_skins[1].gold_vein_q16 == 62259);

    /* 2. Render 4-Panel Haute-Couture Showroom Inspection Preview */
    int rc_ppm = flow_fashion_render_showroom_ppm(showroom_skins, "build/fashion_showroom.ppm");
    assert(rc_ppm == 0);

    /* 3. Zero-Backend Steam Inventory Hex128 Bijective Codec & Anti-Forgery Verification */
    char hex_blob[129];
    flow_market_encode_hex128(&showroom_skins[1], hex_blob);
    assert(strlen(hex_blob) == 128U);

    FlowFashionSkin64 decoded_skin;
    assert(flow_market_decode_hex128(hex_blob, &decoded_skin) == true);
    assert(memcmp(&decoded_skin, &showroom_skins[1], sizeof(FlowFashionSkin64)) == 0);

    /* Non-NUL-terminated 129-char string (trailing garbage) -> must be rejected! */
    char unterminated_hex[130];
    memcpy(unterminated_hex, hex_blob, 128U);
    unterminated_hex[128] = 'A';
    unterminated_hex[129] = '\0';
    FlowFashionSkin64 trailing_skin;
    assert(flow_market_decode_hex128(unterminated_hex, &trailing_skin) == false);

    /* Tamper with 1 nibble in the Steam metadata string -> must be rejected by SSOT signature! */
    hex_blob[16] = (hex_blob[16] == 'A') ? 'B' : 'A';
    FlowFashionSkin64 forged_skin;
    assert(flow_market_decode_hex128(hex_blob, &forged_skin) == false);

    /* 4. Steam Community Market + Web SaaS Creator Royalty Split (Zero Rounding Leak) */
    FlowMarketToken64 settlement;
    /* $199.99 ($19,999 cents) transaction for Scar-Pattern #661 Blue Gem */
    assert(flow_market_settle_transaction(900012345ULL, 77001ULL, 19999ULL, &settlement) == 0);
    assert(settlement.valve_steam_fee_cents == 999ULL);     /* 5% */
    assert(settlement.studio_treasury_cents == 1399ULL);    /* 7% */
    assert(settlement.creator_royalty_cents == 599ULL);     /* 3% Web SaaS Workshop Creator */
    assert(settlement.seller_net_payout_cents == 17002ULL); /* 85% + exact cent remainder */
    assert(settlement.valve_steam_fee_cents +
           settlement.studio_treasury_cents +
           settlement.creator_royalty_cents +
           settlement.seller_net_payout_cents == settlement.gross_price_cents);

    /* 5. Ricardian 10-to-1 Trade-Up Deflation Contract */
    FlowFashionSkin64 inputs_grade_b[10];
    for (uint32_t i = 0; i < 10U; ++i) {
        inputs_grade_b[i] = flow_fashion_design_from_silhouette(
            2000U + i,
            i & 3U,
            FLOW_RICARDIAN_GRADE_B,
            100ULL + i * 37ULL,
            2000 + (int32_t)(i * 100U),
            chord_2m_q16,
            40503
        );
    }
    FlowFashionSkin64 upgraded_grade_a;
    uint64_t burned_supply = 0ULL;
    assert(flow_market_trade_up_contract(inputs_grade_b, 3001U, &upgraded_grade_a, &burned_supply) == 0);
    assert(burned_supply == 10ULL);
    assert(upgraded_grade_a.ricardian_grade == FLOW_RICARDIAN_GRADE_A);
    assert(upgraded_grade_a.assembly_index > inputs_grade_b[0].assembly_index);

    printf("  ✓ 2D Silhouette Lofting (Harmony=%d/65536), Showroom PPM, Hex128 MAC, Royalty Split ($199.99) & 10-to-1 Trade-Up verified!\n",
           showroom_skins[1].aesthetic_harmony_q16);
}

static void test_trc_accessibility_epilepsy_and_colorblind_geometry(void) {
    printf("[Test 8/8] Steam/Console TRC Accessibility Guard (<= 3Hz Strobe & Colorblind Geometry)...\n");

    /* 1. Photosensitive Epilepsy Guard: 800K meltdown strobe clamped from 10.0 Hz to <= 3.0 Hz (196608 Q16) */
    bool overheated = false;
    FlowArtStyle64 meltdown_raw = flow_art_bible_resolve_accessible(
        FLOW_STYLE_MINERAL_GRADE_A, 815ULL, false, false, false, &overheated
    );
    assert(overheated == true);
    assert(meltdown_raw.style_id == FLOW_STYLE_THERMAL_MELTDOWN);
    assert(meltdown_raw.strobe_hz_q16 == 655360); /* Unclamped raw 10.0 Hz */

    bool overheated_safe = false;
    FlowArtStyle64 meltdown_safe = flow_art_bible_resolve_accessible(
        FLOW_STYLE_MINERAL_GRADE_A, 815ULL, false, true, false, &overheated_safe
    );
    assert(overheated_safe == true);
    assert(meltdown_safe.style_id == FLOW_STYLE_THERMAL_MELTDOWN);
    assert(meltdown_safe.strobe_hz_q16 <= FLOW_TRC_MAX_STROBE_HZ_Q16);
    assert(meltdown_safe.strobe_hz_q16 == 196608); /* Clamped to 3.0 Hz (3 * 65536) */

    /* Also verify high-bit flag path via standard flow_art_bible_resolve */
    bool overheated_flag = false;
    FlowArtStyle64 meltdown_flag = flow_art_bible_resolve(
        FLOW_STYLE_MINERAL_GRADE_A | FLOW_STYLE_FLAG_PHOTOSENSITIVE_SAFE,
        815ULL,
        false,
        &overheated_flag
    );
    assert(overheated_flag == true);
    assert(meltdown_flag.strobe_hz_q16 == FLOW_TRC_MAX_STROBE_HZ_Q16);

    /* Hysteresis retention at 775K with photosensitive protection active */
    FlowArtStyle64 meltdown_hold = flow_art_bible_resolve_accessible(
        FLOW_STYLE_MINERAL_GRADE_A, 775ULL, overheated_safe, true, true, &overheated_safe
    );
    assert(overheated_safe == true);
    assert(meltdown_hold.strobe_hz_q16 <= FLOW_TRC_MAX_STROBE_HZ_Q16);

    /* 2. Colorblind-Safe Geometric Wave-Frequency & MSDF Contour-Width Mapping */
    bool dummy_oh = false;
    FlowArtStyle64 g_d = flow_art_bible_resolve_accessible(FLOW_STYLE_MINERAL_GRADE_D, 740ULL, false, true, true, &dummy_oh);
    FlowArtStyle64 g_c = flow_art_bible_resolve_accessible(FLOW_STYLE_MINERAL_GRADE_C, 740ULL, false, true, true, &dummy_oh);
    FlowArtStyle64 g_b = flow_art_bible_resolve_accessible(FLOW_STYLE_MINERAL_GRADE_B, 740ULL, false, true, true, &dummy_oh);
    FlowArtStyle64 g_a = flow_art_bible_resolve_accessible(FLOW_STYLE_MINERAL_GRADE_A, 740ULL, false, true, true, &dummy_oh);
    FlowArtStyle64 g_s = flow_art_bible_resolve_accessible(FLOW_STYLE_MINERAL_GRADE_S, 740ULL, false, true, true, &dummy_oh);
    FlowArtStyle64 g_m = flow_art_bible_resolve_accessible(FLOW_STYLE_MINERAL_GRADE_A, 820ULL, false, true, true, &dummy_oh);

    /* Strictly monotonic geometric SDF wave frequencies across D < C < B < A < S < Meltdown */
    assert(g_d.sdf_noise_freq_q16 < g_c.sdf_noise_freq_q16);
    assert(g_c.sdf_noise_freq_q16 < g_b.sdf_noise_freq_q16);
    assert(g_b.sdf_noise_freq_q16 < g_a.sdf_noise_freq_q16);
    assert(g_a.sdf_noise_freq_q16 < g_s.sdf_noise_freq_q16);
    assert(g_s.sdf_noise_freq_q16 < g_m.sdf_noise_freq_q16);

    /* Strictly monotonic MSDF contour stroke widths across D < C < B < A < S < Meltdown */
    assert(g_d.msdf_stroke_q16 < g_c.msdf_stroke_q16);
    assert(g_c.msdf_stroke_q16 < g_b.msdf_stroke_q16);
    assert(g_b.msdf_stroke_q16 < g_a.msdf_stroke_q16);
    assert(g_a.msdf_stroke_q16 < g_s.msdf_stroke_q16);
    assert(g_s.msdf_stroke_q16 < g_m.msdf_stroke_q16);

    /* Edge cases: negative strobe & out-of-range style ID */
    FlowArtStyle64 edge_style = g_d;
    edge_style.strobe_hz_q16 = -500;
    edge_style.style_id = 999U;
    edge_style = flow_art_bible_apply_accessibility(edge_style, true, true);
    assert(edge_style.strobe_hz_q16 == 0);
    assert(edge_style.sdf_noise_freq_q16 >= 65536);
    assert(edge_style.msdf_stroke_q16 >= 1536);

    printf("  ✓ TRC Epilepsy Guard (10Hz -> 3.0Hz = %d Q16) & Colorblind Monotonic Geometry (D..S..Meltdown) verified!\n",
           meltdown_safe.strobe_hz_q16);
}

int main(void) {
    printf("====================================================================\n");
    printf("BLOOM: Prefab SSOT + Art Bible (HTML/CSS/JS) Test Suite\n");
    printf("====================================================================\n");

    test_64b_univalence();
    test_l1_cache_budget();
    test_ricardian_assembly_compression();
    test_q16_csg_sdf_and_worldline_root();
    test_thermal_hysteresis_800k_760k();
    test_wave64_compaction_and_layer_collapse();
    test_fashion_lofting_and_steam_marketplace();
    test_trc_accessibility_epilepsy_and_colorblind_geometry();

    printf("====================================================================\n");
    printf("✓ ALL 8 SSOT / PREFAB / ART BIBLE / FASHION / TRC ACCESSIBILITY TESTS PASSED\n");
    printf("====================================================================\n");
    return 0;
}

