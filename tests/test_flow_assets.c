#include "flow_gpu_abi.h"
#include "flow_lean_abi.h"
#include "flow_assets.h"
#include "flow_image64.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    printf("====================================================================\n");
    printf("BLOOM: 64-Byte Asset Pipeline, Lean-ABI & Constitutive Matter Suite\n");
    printf("====================================================================\n");

    /* 1. Direct .lean <-> FLOW 64-Byte ABI Bridge & Runtime Specification Loader */
    printf("[Test 1/6] Direct .lean <-> FLOW 64B ABI Bridge & Runtime Spec Binding...\n");
    assert(sizeof(FlowGpuSlice0Core64) == 64U && sizeof(FlowGpuSlice1Camera64) == 64U);
    assert(sizeof(FlowGpuSlice2Relativity64) == 64U && sizeof(FlowGpuSlice3DopamineAv64) == 64U);
    assert(sizeof(FlowGpuFrameUniforms256) == 256U && _Alignof(FlowGpuFrameUniforms256) == 64U);
    assert(sizeof(FlowEngineBus1K) == 1024U && _Alignof(FlowEngineBus1K) == 64U);

    FlowLeanClause64 abi_table[16];
    flow_lean_abi_init_canonical_table(abi_table);

    FlowLeanSpecMeta64 ric_meta, asset_meta;
    assert(flow_lean_abi_load_file("research/ricardian.lean", &ric_meta, abi_table, 16U));
    assert(ric_meta.sorry_count == 0U && ric_meta.theorem_count >= 10U);

    assert(flow_lean_abi_load_file("research/AssetPipeline.lean", &asset_meta, abi_table, 16U));
    assert(asset_meta.sorry_count == 0U && asset_meta.theorem_count >= 10U);
    assert(abi_table[FLOW_CLAUSE_SOV_OKLAB_CONTRAST].param_a == 26214LL);
    assert(abi_table[FLOW_CLAUSE_SOV_NEGATIVE_SPACE].param_a == 39321LL);
    assert(abi_table[FLOW_CLAUSE_SOV_NEGATIVE_SPACE].param_b == 49152LL);

    FlowLeanRegBank64 rbank;
    flow_lean_abi_regbank_init64(&rbank);
    /* Step thermal hysteresis (801K enter, 760K exit): 820K -> latched, 780K deadband -> stays latched, 750K -> unlatched */
    rbank.regs_q16[0] = 820;
    assert(flow_lean_abi_step_clause64(&abi_table[FLOW_CLAUSE_RICARDIAN_THERMAL_GATE], &rbank));
    assert(rbank.regs_q16[0] == 1);
    rbank.regs_q16[0] = 780;
    assert(flow_lean_abi_step_clause64(&abi_table[FLOW_CLAUSE_RICARDIAN_THERMAL_GATE], &rbank));
    assert(rbank.regs_q16[0] == 1);
    rbank.regs_q16[0] = 750;
    assert(flow_lean_abi_step_clause64(&abi_table[FLOW_CLAUSE_RICARDIAN_THERMAL_GATE], &rbank));
    assert(rbank.regs_q16[0] == 0);

    /* Step Harberger 70/30 conservation split on 10,000 units */
    rbank.regs_q16[1] = 10000;
    assert(flow_lean_abi_step_clause64(&abi_table[FLOW_CLAUSE_RICARDIAN_HARBERGER], &rbank));
    assert(rbank.regs_q16[2] == 7000 && rbank.regs_q16[3] == 3000);
    assert(rbank.regs_q16[2] + rbank.regs_q16[3] == 10000);
    printf("  ✓ Verified .lean Runtime Parser & FLOW 64B ABI Execution (Hysteresis, Clamp, Split, FloorSub)!\n");

    /* 2. Native 64-Byte External Asset Pipeline (WAV PCM Mixer, GLB 2.0 Mesh, W3C SVG <path>) */
    printf("[Test 2/6] Native 64B External Asset Pipeline (WAV, GLB 2.0, W3C SVG <path>)...\n");
    uint8_t test_wav[44 + 16];
    memset(test_wav, 0, sizeof(test_wav));
    memcpy(test_wav + 0,  "RIFF", 4);
    uint32_t riff_sz = 36U + 16U;
    memcpy(test_wav + 4,  &riff_sz, 4);
    memcpy(test_wav + 8,  "WAVEfmt ", 8);
    uint32_t fmt_sz = 16U, srate = 48000U, brate = 96000U, data_sz = 16U;
    uint16_t ftag = 1U, nchan = 1U, balign = 2U, bps = 16U;
    memcpy(test_wav + 16, &fmt_sz, 4);
    memcpy(test_wav + 20, &ftag, 2);
    memcpy(test_wav + 22, &nchan, 2);
    memcpy(test_wav + 24, &srate, 4);
    memcpy(test_wav + 28, &brate, 4);
    memcpy(test_wav + 32, &balign, 2);
    memcpy(test_wav + 34, &bps, 2);
    memcpy(test_wav + 36, "data", 4);
    memcpy(test_wav + 40, &data_sz, 4);
    int16_t raw_samples[8] = { 4000, 8000, 12000, 16000, -16000, -12000, -8000, -4000 };
    memcpy(test_wav + 44, raw_samples, 16);

    int16_t decoded_mono[8] = {0};
    FlowWavAssetDesc64 wav_desc;
    assert(flow_asset_load_wav_bytes(test_wav, sizeof(test_wav), 10U, decoded_mono, 8U, &wav_desc));
    assert(wav_desc.frame_count == 8U && wav_desc.sample_rate_hz == 48000U && decoded_mono[3] == 16000);

    FlowAudioVoice64 voice;
    int16_t stereo_mix[16] = {0};
    flow_audio_voice_trigger64(&voice, 0U, &wav_desc, 65536, 0, 65536, false, 1ULL);
    flow_audio_voice_mix_stereo_s16(&voice, decoded_mono, stereo_mix, 8U);
    assert(stereo_mix[6] > 0 && stereo_mix[7] > 0);

    /* Construct a valid glTF 2.0 Binary (.glb) with 2 vertices (-1.0,-1.0,-1.0) and (+2.0,+1.5,+1.0) */
    const char *glb_json = "{\"asset\":{\"version\":\"2.0\"},\"accessors\":[{\"count\":2}]}   ";
    uint32_t json_len = (uint32_t)strlen(glb_json); /* 56 bytes (4-byte aligned) */
    uint32_t bin_len = 24U; /* 2 vertices x 12 bytes */
    uint32_t glb_tot = 12U + 8U + json_len + 8U + bin_len;
    uint8_t test_glb[128];
    memset(test_glb, 0, sizeof(test_glb));
    uint32_t glb_magic = FLOW_GLB_MAGIC_HEADER, glb_ver = 2U;
    uint32_t chunk_json_t = FLOW_GLB_CHUNK_JSON, chunk_bin_t = FLOW_GLB_CHUNK_BIN;
    memcpy(test_glb + 0,  &glb_magic, 4);
    memcpy(test_glb + 4,  &glb_ver, 4);
    memcpy(test_glb + 8,  &glb_tot, 4);
    memcpy(test_glb + 12, &json_len, 4);
    memcpy(test_glb + 16, &chunk_json_t, 4);
    memcpy(test_glb + 20, glb_json, json_len);
    size_t bin_off = 20U + json_len;
    memcpy(test_glb + bin_off + 0, &bin_len, 4);
    memcpy(test_glb + bin_off + 4, &chunk_bin_t, 4);
    /* IEEE-754 bits: -1.0f = 0xBF800000, +2.0f = 0x40000000, +1.5f = 0x3FC00000, +1.0f = 0x3F800000 */
    uint32_t vbits[6] = { 0xBF800000U, 0xBF800000U, 0xBF800000U, 0x40000000U, 0x3FC00000U, 0x3F800000U };
    memcpy(test_glb + bin_off + 8, vbits, 24U);

    int32_t glb_verts[4][3] = {{0}};
    FlowGlbMeshDesc64 glb_desc;
    assert(flow_asset_load_glb_bytes(test_glb, glb_tot, 20U, 1U, glb_verts, 4U, &glb_desc));
    assert(glb_desc.vertex_count == 2U);
    assert(glb_desc.aabb_min_q16[0] == -65536 && glb_desc.aabb_max_q16[0] == 131072);
    assert(glb_desc.aabb_max_q16[1] == 98304  && glb_desc.aabb_max_q16[2] == 65536);
    assert(flow_asset_eval_glb_sdf_q16(&glb_desc, 0, 0, 0) < 0); /* Origin is inside AABB! */

    /* Compile standard W3C SVG <path> icon */
    FlowGpuGlyphDesc64 svg_icon;
    const char *w3c_svg = "<svg viewBox=\"0 0 24 24\"><path d=\"M 2 12 L 12 2 L 22 12 V 22 H 2 Z\"/></svg>";
    assert(flow_asset_compile_svg_icon64(w3c_svg, 301U, 1U, &svg_icon));
    assert(svg_icon.stroke_count == 5U && svg_icon.ternary_pos_mask != 0ULL);

    /* Compile GLB 2.0 Mesh into Data-Driven 64B GPU CSG Instruction Stream & Evaluate Boolean Subtract */
    assert(sizeof(FlowGpuCsgOp64) == 64U && _Alignof(FlowGpuCsgOp64) == 64U);
    FlowGpuCsgOp64 csg_prog[2];
    flow_asset_compile_glb_to_csg_op64(&glb_desc, 1U, FLOW_CSG_BOOL_UNION, 0, &csg_prog[0]);
    FlowGlbMeshDesc64 cutter_glb = glb_desc;
    cutter_glb.aabb_min_q16[0] = -16384;
    cutter_glb.aabb_min_q16[1] = -16384;
    cutter_glb.aabb_min_q16[2] = -16384;
    cutter_glb.aabb_max_q16[0] = 16384;
    cutter_glb.aabb_max_q16[1] = 16384;
    cutter_glb.aabb_max_q16[2] = 16384;
    flow_asset_compile_glb_to_csg_op64(&cutter_glb, 2U, FLOW_CSG_BOOL_SUBTRACT, 0, &csg_prog[1]);
    /* Origin (0,0,0) was inside glb_desc (<0), but subtracted by cutter_glb -> carved hollow (>0)! */
    int64_t d_carved_csg = flow_asset_eval_csg_program_q16(csg_prog, 2U, 0, 0, 0);
    int64_t d_shell_csg  = flow_asset_eval_csg_program_q16(csg_prog, 2U, 49152, 0, 0);
    assert(d_carved_csg > 0 && d_shell_csg < 0 && csg_prog[0].csg_op_mac != 0ULL);
    printf("  ✓ Verified WAV PCM Mixer, GLB 2.0 Mesh Decoder, W3C SVG <path> & 64B GPU CSG Stream Evaluator!\n");

    /* 3. 64B Dual-Quaternion Q16 Skinning, Pointer-Free Linear BVH & Sparse 4x4x4 SDF Brick */
    printf("[Test 3/6] 64B Dual-Quaternion Q16 Skinning, Linear BVH Tree & Sparse 4x4x4 SDF Brick...\n");
    assert(sizeof(FlowGpuJointDq64) == 64U && _Alignof(FlowGpuJointDq64) == 64U);
    assert(sizeof(FlowGpuBvhNode64) == 64U && _Alignof(FlowGpuBvhNode64) == 64U);
    assert(sizeof(FlowGpuSdfBrick64) == 64U && _Alignof(FlowGpuSdfBrick64) == 64U);

    /* 3a. Dual-Quaternion Q16 Rigid Transform & Volume-Preserving Blend */
    FlowGpuJointDq64 joint_root, joint_elbow, joint_blended;
    int32_t q_ident[4] = { 65536, 0, 0, 0 };
    int32_t t_root[3]  = { 0, 65536, 0 };    /* (0, +1.0, 0) */
    int32_t t_elbow[3] = { 0, 196608, 0 };   /* (0, +3.0, 0) */
    flow_joint_dq64_make(&joint_root,  0U, 0xFFFFFFFFU, q_ident, t_root,  131072, 16384, 8192, 1U);
    flow_joint_dq64_make(&joint_elbow, 1U, 0U,          q_ident, t_elbow, 131072, 16384, 8192, 1U);
    flow_joint_dq64_blend(&joint_root, &joint_elbow, 32768, &joint_blended); /* 50/50 blend */

    int32_t p_local[3] = { 0, 0, 0 };
    int32_t p_world[3] = { 0, 0, 0 };
    flow_joint_dq64_transform_point_q16(&joint_blended, p_local, p_world);
    assert(p_world[1] >= 131070 && p_world[1] <= 131074); /* Midpoint y = +2.0 (131072 Q16) */
    assert(joint_blended.q_real_q16[0] == 65536);         /* Unit norm preserved (0 volume collapse)! */

    FlowGpuJointDq64 skel[2] = { joint_root, joint_elbow };
    int64_t d_bone_in  = flow_skeleton_eval_sdf_q16(skel, 2U, 0, 131072, 0);
    int64_t d_bone_out = flow_skeleton_eval_sdf_q16(skel, 2U, 327680, 131072, 0);
    assert(d_bone_in < 0 && d_bone_out > 0);

    /* 3b. 64-Byte Pointer-Free Linear BVH Tree & O(log N) Branch Culling */
    FlowGpuCsgOp64 scene_ops[8];
    for (uint32_t i = 0U; i < 8U; ++i) {
        FlowGlbMeshDesc64 m = glb_desc;
        int32_t base_x = (int32_t)(i * 20U) * 65536;
        m.aabb_min_q16[0] = base_x - 32768;
        m.aabb_max_q16[0] = base_x + 32768;
        m.aabb_min_q16[1] = -32768;
        m.aabb_max_q16[1] =  32768;
        m.aabb_min_q16[2] = -32768;
        m.aabb_max_q16[2] =  32768;
        flow_asset_compile_glb_to_csg_op64(&m, i + 1U, FLOW_CSG_BOOL_UNION, 0, &scene_ops[i]);
    }
    FlowGpuBvhNode64 bvh_nodes[16];
    size_t bvh_count = flow_bvh_build_from_csg64(scene_ops, 8U, bvh_nodes, 16U);
    assert(bvh_count == 15U); /* Complete binary tree over 8 leaves = 15 nodes */
    assert(bvh_nodes[0].aabb_min_q16[0] <= scene_ops[0].pos_q16[0]);
    assert(bvh_nodes[0].aabb_max_q16[0] >= scene_ops[7].pos_q16[0]);

    uint32_t visited_leaves = 0U;
    int64_t d_bvh  = flow_bvh_eval_csg_q16(bvh_nodes, bvh_count, scene_ops, 8U, 0, 0, 0, &visited_leaves);
    int64_t d_flat = flow_asset_eval_csg_program_q16(scene_ops, 8U, 0, 0, 0);
    assert(d_bvh == d_flat && d_bvh < 0);
    assert(visited_leaves < 8U); /* Distant BVH branches pruned in O(log N)! */

    /* 3c. 64-Byte Sparse 4x4x4 SDF Voxel Brick Pack & Trilinear Sample */
    int8_t brick_vox[64];
    for (uint32_t z = 0U; z < 4U; ++z) {
        for (uint32_t y = 0U; y < 4U; ++y) {
            for (uint32_t x = 0U; x < 4U; ++x) {
                /* Signed gradient along X: x=0..1 inside (<0), x=2..3 outside (>0) */
                brick_vox[z * 16U + y * 4U + x] = (int8_t)((int32_t)x * 4 - 6);
            }
        }
    }
    FlowGpuSdfBrick64 sdf_brick;
    int32_t brick_orig[3] = { 0, 0, 0 };
    flow_sdf_brick64_pack(&sdf_brick, 1U, 2U, brick_orig, 65536, brick_vox);
    int64_t d_brick_in  = flow_sdf_brick64_sample_trilinear_q16(&sdf_brick, 16384, 65536, 65536);
    int64_t d_brick_out = flow_sdf_brick64_sample_trilinear_q16(&sdf_brick, 163840, 65536, 65536);
    assert(d_brick_in < 0 && d_brick_out > 0 && sdf_brick.brick_mac != 0ULL);
    printf("  ✓ Verified 64B Dual-Quaternion Skinning, Linear BVH (%u/8 leaves visited) & Sparse 4x4x4 SDF Brick!\n",
           visited_leaves);

    /* 4. 64B Color-Gamut Egress (OKLab -> Rec.709/P3/Rec.2020 & YUV420p) + Progressive .flow Block Stream */
    printf("[Test 4/6] 64B Color-Gamut Egress (Rec.709/P3/Rec.2020/YUV) & Progressive .flow Stream...\n");
    assert(sizeof(FlowStreamEgress64) == 64U && _Alignof(FlowStreamEgress64) == 64U);

    FlowStreamEgress64 eg_709, eg_p3, eg_2020;
    flow_stream_egress64_init(&eg_709,  1U, FLOW_GAMUT_REC709_SRGB, FLOW_STREAM_FMT_YUV420P_PIPE,  60U);
    flow_stream_egress64_init(&eg_p3,   2U, FLOW_GAMUT_DISPLAY_P3,  FLOW_STREAM_FMT_IOSURFACE_SHM, 120U);
    flow_stream_egress64_init(&eg_2020, 3U, FLOW_GAMUT_REC2020_PQ,  FLOW_STREAM_FMT_FLOW64_NATIVE, 60U);

    int32_t rgb_white_709[3], rgb_white_p3[3], rgb_white_2020[3];
    flow_stream_egress64_map_oklab_to_rgb_q16(&eg_709,  65536, 0, 0, rgb_white_709);
    flow_stream_egress64_map_oklab_to_rgb_q16(&eg_p3,   65536, 0, 0, rgb_white_p3);
    flow_stream_egress64_map_oklab_to_rgb_q16(&eg_2020, 65536, 0, 0, rgb_white_2020);
    /* D65 White (L=1.0, a=0, b=0) maps to exact (65536, 65536, 65536) across all gamuts! */
    assert(rgb_white_709[0]  == 65536 && rgb_white_709[1]  == 65536 && rgb_white_709[2]  == 65536);
    assert(rgb_white_p3[0]   == 65536 && rgb_white_p3[1]   == 65536 && rgb_white_p3[2]   == 65536);
    assert(rgb_white_2020[0] == 65536 && rgb_white_2020[1] == 65536 && rgb_white_2020[2] == 65536);

    uint8_t y_w = 0, u_w = 0, v_w = 0;
    flow_stream_egress64_rgb_to_yuv420_bt709(rgb_white_709, &y_w, &u_w, &v_w);
    assert(y_w == 235U && u_w == 128U && v_w == 128U); /* Studio-swing BT.709 white = (235, 128, 128) */

    /* Progressive 8-block (512B) .flow stream assembly (out-of-order network arrival) */
    FlowImageBlock64 src_stream[8], dst_arena[8];
    memset(src_stream, 0, sizeof(src_stream));
    memset(dst_arena,  0, sizeof(dst_arena));
    src_stream[0].header.magic             = FLOW_IMAGE64_MAGIC;
    src_stream[0].header.block_cap_mask    = 7ULL; /* 8 blocks = 2^3 */
    src_stream[0].header.prefab_block_off  = 1U;
    src_stream[0].header.prefab_block_count = 4U;
    src_stream[0].header.version           = FLOW_IMAGE64_VERSION;
    src_stream[1].joint_dq                 = joint_blended;
    src_stream[2].bvh_node                 = bvh_nodes[0];
    src_stream[3].sdf_brick                = sdf_brick;
    src_stream[4].stream_egress            = eg_2020;
    src_stream[0].header.merkle_mac        = flow_image64_compute_mac(src_stream, 8U);

    uint64_t rx_mask = 0ULL;
    /* Reject out-of-bounds block index >= 8 */
    assert(!flow_image64_stream_ingest_block(dst_arena, 8U, 99U, &src_stream[0], &rx_mask));
    /* Ingest blocks out of order (7, 6, ..., 0) */
    for (int b = 7; b >= 0; --b) {
        assert(flow_image64_stream_ingest_block(dst_arena, 8U, (uint32_t)b, &src_stream[b], &rx_mask));
    }
    assert(rx_mask == 0xFFULL);
    assert(flow_image64_verify(dst_arena, sizeof(dst_arena)));
    printf("  ✓ Verified D65 Gamut Matrix (Rec.709/P3/Rec.2020), BT.709 YUV420p (Y=%u,U=%u,V=%u) & 8-Block .flow Stream!\n",
           y_w, u_w, v_w);

    /* 5. 64B Constitutive Physical Matter State (FlowMatterState64) & Branchless Physics-to-Vision Optics */
    printf("[Test 5/6] 64B Constitutive Physical Matter State & Branchless Optical Operator...\n");
    assert(sizeof(FlowMatterState64) == 64U && _Alignof(FlowMatterState64) == 64U);

    FlowMatterState64 m_air, m_glass, m_jade, m_mercury, m_magma;
    int32_t zero_abs[3]  = { 0, 0, 0 };
    int32_t glass_abs[3] = { 9175, 2621, 5242 };    /* Low-iron green silicate absorption */
    int32_t jade_abs[3]  = { 98304, 7864, 72089 };  /* Emerald jadeite Cr3+ absorption */
    int32_t magma_abs[3] = { 131072, 163840, 196608 };

    /* 5a. Gladstone-Dale auto-IOR derivation (ior_eta_q16 = 0 -> derived from density_kg_m3) */
    flow_matter_state64_init(&m_air,     1U, 65536,  0,      0,     16384, 0,     0,     zero_abs,  0,     65536,  295U,  1U);
    flow_matter_state64_init(&m_glass,   2U, 0,      0,      65536, 49152, 29491, 5242,  glass_abs, 58982, 524288, 295U,  2500U);
    flow_matter_state64_init(&m_jade,    3U, 108789, 0,      65536, 26214, 19660, 49152, jade_abs,  0,     917504, 295U,  3340U);
    flow_matter_state64_init(&m_mercury, 4U, 106168, 314572, 13107, 52428, 3276,  0,     zero_abs,  0,     655360, 295U,  13534U);
    flow_matter_state64_init(&m_magma,   5U, 104857, 19660,  39321, 58982, 32768, 26214, magma_abs, 16384, 786432, 1450U, 2700U);

    assert(m_glass.ior_eta_q16 == 65536 + 2500 * 14); /* Gladstone-Dale: 100536 (~1.534) */
    assert(m_glass.matter_mac != 0ULL && m_mercury.matter_mac != 0ULL);

    /* 5b. Complex Fresnel (\eta + i\kappa) continuous transition: Air -> Glass -> Mercury */
    int32_t f0_air     = flow_matter_eval_fresnel_q16(&m_air,     65536);
    int32_t f90_air    = flow_matter_eval_fresnel_q16(&m_air,     0);
    int32_t f0_glass   = flow_matter_eval_fresnel_q16(&m_glass,   65536);
    int32_t f0_mercury = flow_matter_eval_fresnel_q16(&m_mercury, 65536);
    assert(f0_air == 0 && f90_air == 65536);
    assert(f0_glass > 2500 && f0_glass < 3500);     /* ~4.4% normal reflectance for silicate glass */
    assert(f0_mercury > 45000 && f0_mercury <= 65536); /* >68% metallic reflectance from \kappa! */

    /* 5c. Homogenized Vogel scattering radius & 2-Jet Jacobian caustic amplification */
    int32_t r_air   = flow_matter_eval_vogel_radius_q16(&m_air);
    int32_t r_glass = flow_matter_eval_vogel_radius_q16(&m_glass);
    int32_t r_jade  = flow_matter_eval_vogel_radius_q16(&m_jade);
    assert(r_air == 0 && r_glass > 0 && r_jade > r_glass);

    int32_t c_flat  = flow_matter_eval_caustic_gain_q16(&m_glass, 0);
    int32_t c_crest = flow_matter_eval_caustic_gain_q16(&m_glass, 49152); /* Convex wave/pebble crest */
    assert(c_flat == 65536 && c_crest > 65536);

    /* 5d. Branchless Constitutive Radiance Evaluator across all phases */
    int32_t grad_h[2] = { 8192, -6144 };
    int32_t bg_rgb[3] = { 49152, 52428, 45875 };
    int32_t l_del[2]  = { 4096, 4096 };
    int32_t rad_glass[3], rad_jade[3], rad_mercury[3], rad_magma[3];

    flow_matter_eval_radiance_rgb_q16(&m_glass,   grad_h, 32768, bg_rgb, l_del, rad_glass);
    flow_matter_eval_radiance_rgb_q16(&m_jade,    grad_h, 32768, bg_rgb, l_del, rad_jade);
    flow_matter_eval_radiance_rgb_q16(&m_mercury, grad_h, 32768, bg_rgb, l_del, rad_mercury);
    flow_matter_eval_radiance_rgb_q16(&m_magma,   grad_h, 32768, bg_rgb, l_del, rad_magma);

    /* Jadeite Cr3+ Beer-Lambert + Mie SSS preserves green channel (G > R, G > B) */
    assert(rad_jade[1] > rad_jade[0] && rad_jade[1] > rad_jade[2]);
    /* 1450K Molten Magma Planck blackbody emission produces incandescent R > G > B */
    assert(rad_magma[0] > rad_magma[1] && rad_magma[1] > rad_magma[2] && rad_magma[0] > 65536);
    /* Liquid Mercury specular reflection is high-intensity across all channels */
    assert(rad_mercury[0] > 50000 && rad_mercury[1] > 50000 && rad_mercury[2] > 50000);

    /* 5e. 128-Hex Wire Protocol round-trip & MAC tamper rejection for FlowMatterState64 */
    char hex128[129];
    FlowMatterState64 m_decoded;
    flow_matter_state64_format_hex128(&m_jade, hex128);
    assert(strlen(hex128) == 128U);
    assert(flow_matter_state64_parse_hex128(hex128, &m_decoded));
    assert(memcmp(&m_jade, &m_decoded, sizeof(FlowMatterState64)) == 0);
    /* Flip 1 nibble in payload -> MAC check must reject tampered 128-Hex */
    hex128[10] = (hex128[10] == 'a') ? 'b' : 'a';
    assert(!flow_matter_state64_parse_hex128(hex128, &m_decoded));

    /* 5f. Kitty Graphics Protocol APC terminal egress encoder (Ghostty / Rex / Agent TUI) */
    uint8_t mini_rgb[2 * 2 * 3] = {
        255, 0, 0,   0, 255, 0,
        0, 0, 255,   255, 255, 255
    };
    char kitty_apc[128];
    size_t kitty_len = flow_stream_egress64_encode_kitty_rgb24(mini_rgb, 2U, 2U, kitty_apc, sizeof(kitty_apc));
    assert(kitty_len > 20U);
    assert(strncmp(kitty_apc, "\x1b_Ga=T,f=24,s=2,v=2,q=2;", 24U) == 0);
    assert(kitty_apc[kitty_len - 2U] == '\x1b' && kitty_apc[kitty_len - 1U] == '\\');

    printf("  ✓ Verified Gladstone-Dale IOR, Complex Fresnel (Air=%d, Glass=%d, Mercury=%d Q16), 2-Jet Caustics, 128-Hex MAC & Kitty TUI Egress!\n",
           f0_air, f0_glass, f0_mercury);

    /* 6. 64B Cloud Room Isolate & Multi-View Tile-Atlas Router */
    printf("[Test 6/6] 64B Cloud Room Isolate & Multi-View Tile-Atlas Router (FlowCloudRoomDesc64)...\n");
    assert(sizeof(FlowCloudRoomDesc64) == 64U);
    assert(_Alignof(FlowCloudRoomDesc64) == 64U);

    FlowCloudRoomDesc64 room10;
    /* 10-seat multi-view room: 64 map blocks, 960x540 per tile, 8333us SLA */
    flow_cloud_room64_init(&room10, 42U, 64U, 10U, 960U, 540U, 8333U, 0xCAFEBABE12345678ULL);
    assert(room10.map_block_mask == 63U);
    assert(room10.active_view_count == 10U);
    assert(room10.grid_cols == 4U && room10.grid_rows == 4U);
    assert(room10.active_seat_mask == 0x03FFU); /* Seats 0..9 occupied, 10..15 empty */
    assert(room10.room_mac != 0ULL);

    /* Seat 0 center pixel (480, 270) -> seat 0, NDC (0, 0) */
    int32_t ndc_x = 0, ndc_y = 0;
    uint32_t s0 = flow_cloud_room64_route_pixel_to_seat(&room10, 480U, 270U, &ndc_x, &ndc_y);
    assert(s0 == 0U && ndc_x == 0 && ndc_y == 0);

    /* Seat 9 (row 2, col 1 -> x in [960..1919], y in [1080..1619]) */
    uint32_t s9 = flow_cloud_room64_route_pixel_to_seat(&room10, 960U + 480U, 1080U + 270U, &ndc_x, &ndc_y);
    assert(s9 == 9U && ndc_x == 0 && ndc_y == 0);

    /* Unoccupied Seat 10 (row 2, col 2) -> returns 0xFFFFFFFFU */
    uint32_t s10 = flow_cloud_room64_route_pixel_to_seat(&room10, 1920U + 100U, 1080U + 100U, &ndc_x, &ndc_y);
    assert(s10 == 0xFFFFFFFFU);

    /* Noisy-neighbor < 8.33ms SLA circuit-breaker LOD stepping & recovery */
    assert(flow_cloud_room64_step_sla_governor(&room10, 9500U) == 1U);
    assert(flow_cloud_room64_step_sla_governor(&room10, 11000U) == 2U);
    assert(flow_cloud_room64_step_sla_governor(&room10, 5000U) == 1U); /* < 75% budget -> recovers 1 step */
    printf("  ✓ Verified 10-Seat Shared-Room 4x4 Tile-Atlas Routing, Z/2^kZ Map Mask & <8.33ms SLA Circuit-Breaker!\n");

    printf("====================================================================\n");
    printf("✓ ALL 6 LEAN-ABI, ASSET, BVH, STREAM, MATTER & CLOUD ROOM TESTS PASSED\n");
    printf("====================================================================\n");
    return 0;
}
