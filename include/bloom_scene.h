#ifndef BLOOM_SCENE_H
#define BLOOM_SCENE_H

#include "bloom_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 * Pillar 1 ("Three.js"): 64-Byte 3D Spatial Scene, BVH, Skinning & Matter Optics
 * ============================================================================
 */

/* CSG Primitive Types */
#define BLOOM_CSG_PRIM_NONE       0U
#define BLOOM_CSG_PRIM_SPHERE     1U
#define BLOOM_CSG_PRIM_TORUS      2U
#define BLOOM_CSG_PRIM_BOX        3U
#define BLOOM_CSG_PRIM_OCTAHEDRON 4U
#define BLOOM_CSG_PRIM_CYLINDER   5U
#define BLOOM_CSG_PRIM_CAPSULE    6U

/* CSG Boolean Operations */
#define BLOOM_CSG_BOOL_UNION        0U
#define BLOOM_CSG_BOOL_SUBTRACT     1U
#define BLOOM_CSG_BOOL_INTERSECT    2U
#define BLOOM_CSG_BOOL_SMOOTH_UNION 3U

/* Canonical Matter Preset IDs */
#define BLOOM_MATTER_CRYSTAL_GLASS 0U
#define BLOOM_MATTER_IMPERIAL_JADE 1U
#define BLOOM_MATTER_LIQUID_WATER  2U
#define BLOOM_MATTER_TITANIUM      3U
#define BLOOM_MATTER_AURUM_GOLD    4U
#define BLOOM_MATTER_SAPPHIRE      5U
#define BLOOM_MATTER_OBSIDIAN      6U
#define BLOOM_MATTER_BLACKBODY     7U

/* Pure Integer IEEE-754 Binary32 -> Q16.16 Converter (0 Float Instructions) */
int32_t bloom_ieee754_bits_to_q16(uint32_t ieee754_bits);

/* Fixed-Point Algebra & Polynomial Smooth Min/Max */
int32_t bloom_isqrt_u64_to_i32(uint64_t n);
int32_t bloom_sin_q16(int32_t angle_q16);
int32_t bloom_cos_q16(int32_t angle_q16);
int64_t bloom_smin_q16(int64_t a_q16, int64_t b_q16, int64_t k_q16);
int64_t bloom_smax_q16(int64_t a_q16, int64_t b_q16, int64_t k_q16);

/* 1. CSG Primitive & Binary glTF (.glb) Ingestion */
void bloom_csg_op_init64(BloomCsgOp64 *out_op,
                         uint32_t op_id,
                         uint32_t prim_type,
                         uint32_t bool_op,
                         uint32_t matter_id,
                         const int32_t pos_q16[3],
                         const int32_t extents_q16[3],
                         int32_t smooth_k_q16,
                         int32_t bevel_r_q16);

bool bloom_glb_parse_to_csg64(const uint8_t *glb_bytes,
                              size_t glb_len,
                              uint32_t op_id,
                              uint32_t matter_id,
                              BloomCsgOp64 *out_op,
                              uint32_t *out_vertex_count);

int64_t bloom_csg_eval_single_q16(const BloomCsgOp64 *op,
                                  int64_t px_q16,
                                  int64_t py_q16,
                                  int64_t pz_q16);

int64_t bloom_csg_eval_program_q16(const BloomCsgOp64 *ops,
                                   size_t op_count,
                                   int64_t px_q16,
                                   int64_t py_q16,
                                   int64_t pz_q16,
                                   uint32_t *out_matter_id);

/* 2. 60-Bit 3D Morton Z-Order & Pointer-Free Linear BVH */
uint64_t bloom_bvh_morton3d_q16(int32_t x_q16, int32_t y_q16, int32_t z_q16);

size_t bloom_bvh_build_from_csg64(const BloomCsgOp64 *ops,
                                  size_t op_count,
                                  BloomBvhNode64 *out_nodes,
                                  size_t max_nodes);

int64_t bloom_bvh_eval_csg_q16(const BloomBvhNode64 *nodes,
                               size_t node_count,
                               const BloomCsgOp64 *ops,
                               size_t op_count,
                               int64_t px_q16,
                               int64_t py_q16,
                               int64_t pz_q16,
                               uint32_t *out_visited_nodes);

/* 3. Volume-Preserving Q16.16 Dual-Quaternion Skeletal Skinning */
void bloom_joint_dq64_make(BloomJointDq64 *out_joint,
                           uint32_t joint_id,
                           uint32_t parent_id,
                           const int32_t q_rot_q16[4],
                           const int32_t trans_q16[3],
                           int32_t bone_len_q16,
                           int32_t bone_radius_q16,
                           int32_t smooth_k_q16,
                           uint32_t matter_id);

void bloom_joint_dq64_blend(const BloomJointDq64 *a,
                            const BloomJointDq64 *b,
                            int32_t t_q16,
                            BloomJointDq64 *out_blended);

void bloom_joint_dq64_transform_q16(const BloomJointDq64 *joint,
                                    const int32_t in_pt_q16[3],
                                    int32_t out_pt_q16[3]);

int64_t bloom_skeleton_eval_sdf_q16(const BloomJointDq64 *joints,
                                    size_t joint_count,
                                    int64_t px_q16,
                                    int64_t py_q16,
                                    int64_t pz_q16);

/* 4. Sparse 4x4x4 4-Bit SDF Voxel Brick */
void bloom_sdf_brick64_pack(BloomSdfBrick64 *out_brick,
                            uint32_t brick_id,
                            uint32_t matter_id,
                            const int32_t origin_q16[3],
                            int32_t voxel_step_q16,
                            const int32_t sdf_samples_q16[64]);

int64_t bloom_sdf_brick64_sample_q16(const BloomSdfBrick64 *brick,
                                     int64_t px_q16,
                                     int64_t py_q16,
                                     int64_t pz_q16);

/* 5. Constitutive Physical Matter Optics & 128-Hex Wire Protocol */
void bloom_matter_init_canonical(BloomMatter64 out_matters[BLOOM_MAX_MATTERS]);

void bloom_matter_init64(BloomMatter64 *out_matter,
                         uint32_t matter_id,
                         int32_t ior_eta_q16,
                         int32_t extinction_kappa_q16,
                         int32_t viscosity_quench_q16,
                         int32_t surface_relief_q16,
                         int32_t micro_roughness_q16,
                         int32_t mie_scatter_q16,
                         const int32_t absorb_rgb_q16[3],
                         int32_t lattice_wire_q16,
                         int32_t thickness_mm_q16,
                         uint32_t thermal_kelvin,
                         uint32_t density_kg_m3);

void bloom_matter_eval_radiance_q16(const BloomMatter64 *matter,
                                    const int32_t grad_h_q16[2],
                                    int32_t laplacian_h_q16,
                                    const int32_t bg_rgb_q16[3],
                                    const int32_t light_delta_q16[2],
                                    int32_t out_rgb_q16[3]);

void bloom_matter_format_hex128(const BloomMatter64 *matter, char out_hex129[129]);
bool bloom_matter_parse_hex128(const char *hex128, BloomMatter64 *out_matter);

/* 6. Thin-Lens Physical Orbit Camera (FLOW v6.0.0 Driven) & 3D Raycaster */
void bloom_camera_init64(BloomCamera64 *cam,
                         int32_t yaw_q16,
                         int32_t pitch_q16,
                         int32_t dist_q16,
                         int32_t focal_mm_q16,
                         int32_t fstop_q16);

void bloom_camera_sync_from_orbit(BloomCamera64 *cam);

int32_t bloom_camera_eval_coc_q16(const BloomCamera64 *cam, int32_t depth_q16);

bool bloom_scene_raycast64(BloomEngineArena *arena,
                           int32_t cursor_norm_x_q16,
                           int32_t cursor_norm_y_q16,
                           BloomRaycastHit64 *out_hit);

#ifdef __cplusplus
}
#endif

#endif /* BLOOM_SCENE_H */
