/-
  BLOOM v9.0.0 — Pure Config, Flexbox DSL & AOT Arena Image Compiler (`import Flow.Schema`)
  =======================================================================================
  SSOT compliance: all formal machine-word, interval, convexity, SEARU, 2-Jet,
  and full-512b C17+WGSL Schema compilers reside in domain-free `../FLOW/{core,sdk}/lean/Flow/*.lean`.
  This file configures the 256-slot `16,384B` `bloomArenaSchema`, declares the
  React-Native `<View>` Flexbox `defaultStudioBlueprint`, executes elaboration-time
  SDL3 GPU/CPU parity gates (`compileTimeBoundaryGate`), and seals `build/studio.bloom`
  with ZERO local theorems (`0 theorem`, `0 sorryAx`).
-/

import Flow.Schema

namespace Bloom

def version    : String := "9.0.0"
def cellBytes  : Nat    := 64
def arenaCells : Nat    := 256
def arenaBytes : Nat    := arenaCells * cellBytes

/-! ## Part 1: SSOT 256-Slot (`16,384B`) Arena & Full-512b C17/WGSL Schema Config -/

def bloomFrameHeaderLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "frame_hdr" "BloomFrameHeader64" "frame_hdr" [
    ("magic", .u32, 1), ("version", .u32, 1), ("width_px", .u32, 1), ("height_px", .u32, 1),
    ("dpi_scale_q16", .i32, 1), ("frame_seq", .u32, 1), ("inst_count", .u32, 1), ("csg_count", .u32, 1),
    ("bvh_count", .u32, 1), ("ui_count", .u32, 1), ("glyph_count", .u32, 1), ("latex_count", .u32, 1),
    ("xray_plane_q16", .i32, 1), ("selected_inst_id", .u32, 1), ("flags", .u32, 1), ("cap_mask", .u32, 1)
  ]

def bloomCameraLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "camera" "BloomCamera64" "camera" [
    ("eye_q16", .i32, 3), ("target_q16", .i32, 3), ("up_q16", .i32, 3), ("focal_mm_q16", .i32, 1),
    ("aperture_fstop_q16", .i32, 1), ("focus_dist_q16", .i32, 1), ("exposure_q16", .i32, 1),
    ("orbit_yaw_q16", .i32, 1), ("orbit_pitch_q16", .i32, 1), ("orbit_dist_q16", .i32, 1)
  ]

def bloomRegBankLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "reg_bank" "BloomRegBank64" "reg_bank" [
    ("channels_q16", .i32, 12), ("dirty_mask", .u32, 1), ("epoch", .u32, 1),
    ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomDomStateLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "dom" "BloomDomState64" "dom" [
    ("compiled_node_count", .u32, 1), ("hovered_node_id", .u32, 1), ("active_node_id", .u32, 1),
    ("captured_node_id", .u32, 1), ("last_action_opcode", .u32, 1), ("cursor_norm_x_q16", .i32, 1),
    ("cursor_norm_y_q16", .i32, 1), ("click_dispatch_count", .u32, 1), ("wave64_cull_skips", .u32, 1),
    ("hot_reload_epoch", .u32, 1), ("focused_input_id", .u32, 1), ("reserved0", .u32, 1),
    ("dom_mask_lo32", .u32, 1), ("dom_mask_hi32", .u32, 1), ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomFlexLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "flex" "BloomFlexContainer64" "flex" [
    ("direction", .u16, 1), ("justify_mode", .u16, 1), ("align_items", .u16, 1), ("overflow_mode", .u16, 1),
    ("origin_x_q16", .i32, 1), ("origin_y_q16", .i32, 1), ("half_w_q16", .i32, 1), ("half_h_q16", .i32, 1),
    ("padding_q16", .i32, 1), ("gap_q16", .i32, 1), ("scroll_offset_q16", .i32, 1), ("content_extent_q16", .i32, 1),
    ("max_scroll_q16", .i32, 1), ("child_count", .u32, 1), ("tile_mask_lo32", .u32, 1), ("tile_mask_hi32", .u32, 1),
    ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomTextInputLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "text_input" "BloomTextInput64" "text_input" [
    ("utf8_buf", .cchar, 40), ("byte_len", .u8, 1), ("cursor_pos", .u8, 1), ("anchor_pos", .u8, 1),
    ("focused", .u8, 1), ("bound_node_id", .u32, 1), ("submit_action_id", .u32, 1),
    ("edit_revision", .u32, 1), ("hash_lo32", .u32, 1), ("hash_hi32", .u32, 1)
  ]

def bloomImeLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "ime" "BloomIme64" "ime" [
    ("target_node_id", .u32, 1), ("preedit_len", .u8, 1), ("cursor_offset", .u8, 1),
    ("candidate_index", .u8, 1), ("ime_active", .u8, 1), ("preedit_utf8", .cchar, 36),
    ("anchor_x_q16", .i32, 1), ("anchor_y_q16", .i32, 1), ("committed_count", .u32, 1),
    ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomWindowLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "window" "BloomWindow64" "window" [
    ("backend_api", .u32, 1), ("logical_w_px", .u32, 1), ("logical_h_px", .u32, 1), ("fb_w_px", .u32, 1),
    ("fb_h_px", .u32, 1), ("dpi_scale_q16", .i32, 1), ("target_fps", .u32, 1), ("host_flags", .u32, 1),
    ("pointer_x_q16", .i32, 1), ("pointer_y_q16", .i32, 1), ("drag_dx_q16", .i32, 1), ("drag_dy_q16", .i32, 1),
    ("scroll_dy_q16", .i32, 1), ("key_code", .u32, 1), ("frame_epoch_lo32", .u32, 1), ("frame_epoch_hi32", .u32, 1)
  ]

def bloomMatterLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "matter" "BloomMatter64" "matter" [
    ("matter_id", .u32, 1), ("ior_eta_q16", .i32, 1), ("extinction_kappa_q16", .i32, 1),
    ("viscosity_quench_q16", .i32, 1), ("surface_relief_q16", .i32, 1), ("micro_roughness_q16", .i32, 1),
    ("mie_scatter_q16", .i32, 1), ("absorb_rgb_q16", .i32, 3), ("lattice_wire_q16", .i32, 1),
    ("thickness_mm_q16", .i32, 1), ("thermal_kelvin", .u32, 1), ("density_kg_m3", .u32, 1),
    ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomArtStyleLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "style" "BloomArtStyle64" "style" [
    ("style_id", .u32, 1), ("base_rgb24", .u32, 1), ("accent_rgb24", .u32, 1), ("roughness_q16", .i32, 1),
    ("metallic_q16", .i32, 1), ("emissive_gain_q16", .i32, 1), ("sdf_noise_freq_q16", .i32, 1),
    ("sdf_noise_amp_q16", .i32, 1), ("msdf_stroke_q16", .i32, 1), ("strobe_hz_q16", .i32, 1),
    ("thermal_shift_q16", .i32, 1), ("flags", .u32, 1), ("matter_slot", .u32, 1), ("reserved0", .u32, 1),
    ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomPrefabDescLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "prefab" "BloomPrefabDesc64" "prefab" [
    ("prefab_id", .u32, 1), ("art_style_id", .u32, 1), ("matter_id", .u32, 1), ("csg_op_mask", .u32, 1),
    ("bounding_radius_q16", .i32, 1), ("prim_params_q16", .i32, 6), ("csg_start_idx", .u32, 1),
    ("csg_count", .u32, 1), ("flags", .u32, 1), ("hash_lo32", .u32, 1), ("hash_hi32", .u32, 1)
  ]

def bloomPrefabInstLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "inst" "BloomPrefabInst64" "inst" [
    ("inst_id", .u32, 1), ("prefab_id", .u32, 1), ("style_override_id", .u32, 1), ("matter_override_id", .u32, 1),
    ("pos_q16", .i32, 3), ("rot_euler_q16", .i32, 3), ("scale_q16", .i32, 1), ("thermal_kelvin", .u32, 1),
    ("active_layer", .u16, 1), ("flags", .u16, 1), ("flow_slot", .u32, 1), ("param_override_q16", .i32, 1),
    ("kin_vel_packed", .u32, 1)
  ]

def bloomCsgOpLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "csg_op" "BloomCsgOp64" "csg_op" [
    ("op_id", .u32, 1), ("prim_type", .u32, 1), ("bool_op", .u32, 1), ("matter_id", .u32, 1),
    ("pos_q16", .i32, 3), ("extents_q16", .i32, 3), ("smooth_k_q16", .i32, 1), ("bevel_r_q16", .i32, 1),
    ("stage_mask_lo32", .u32, 1), ("stage_mask_hi32", .u32, 1), ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomBvhNodeLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "bvh_node" "BloomBvhNode64" "bvh_node" [
    ("node_id", .u32, 1), ("left_or_first", .u32, 1), ("right_or_count", .u32, 1), ("is_leaf", .u32, 1),
    ("aabb_min_q16", .i32, 3), ("aabb_max_q16", .i32, 3), ("morton_lo32", .u32, 1), ("morton_hi32", .u32, 1),
    ("stage_mask_lo32", .u32, 1), ("stage_mask_hi32", .u32, 1), ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomJointDqLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "joint_dq" "BloomJointDq64" "joint_dq" [
    ("joint_id", .u32, 1), ("parent_id", .u32, 1), ("q_real_q16", .i32, 4), ("q_dual_q16", .i32, 4),
    ("bone_len_q16", .i32, 1), ("bone_radius_q16", .i32, 1), ("smooth_k_q16", .i32, 1), ("matter_id", .u32, 1),
    ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomSdfBrickLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "sdf_brick" "BloomSdfBrick64" "sdf_brick" [
    ("brick_id", .u32, 1), ("matter_id", .u32, 1), ("origin_q16", .i32, 3), ("voxel_step_q16", .i32, 1),
    ("sdf_words", .u32, 8), ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomUiNodeLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "ui_node" "BloomUiNode64" "ui_node" [
    ("node_id", .u32, 1), ("prim_type", .u32, 1), ("anchor_mode", .u32, 1), ("bind_channel", .u32, 1),
    ("offset_x_q16", .i32, 1), ("offset_y_q16", .i32, 1), ("half_w_q16", .i32, 1), ("half_h_q16", .i32, 1),
    ("corner_radius_q16", .i32, 1), ("scale_jet_v_q16", .i32, 1), ("scale_jet_d1_q16", .i32, 1),
    ("bound_value_q16", .i32, 1), ("style_and_state", .u32, 1), ("action_opcode", .u32, 1),
    ("tile_mask_lo32", .u32, 1), ("tile_mask_hi32", .u32, 1)
  ]

def bloomGlyphLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "glyph" "BloomGlyph64" "glyph" [
    ("glyph_id", .u32, 1), ("stroke_count", .u32, 1), ("stroke_width_q16", .i32, 1),
    ("stroke_rgb24", .u32, 1), ("seg_coords_q8", .i16, 20), ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomLatexBoxLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "latex_box" "BloomLatexBox64" "latex_box" [
    ("box_id", .u32, 1), ("box_kind", .u32, 1), ("codepoint_or_op", .u32, 1), ("tint_rgb24", .u32, 1),
    ("x_q16", .i32, 1), ("y_q16", .i32, 1), ("half_w_q16", .i32, 1), ("half_h_q16", .i32, 1),
    ("stroke_q16", .i32, 1), ("baseline_shift_q16", .i32, 1), ("parent_panel_id", .u32, 1), ("flags", .u32, 1),
    ("tile_mask_lo32", .u32, 1), ("tile_mask_hi32", .u32, 1), ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomIntentLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "intent" "BloomIntent64" "intent" [
    ("intent_id", .u32, 1), ("bind_channel", .u32, 1), ("action_opcode", .u32, 1), ("priority_weight", .u32, 1),
    ("lo_exit_q16", .i32, 1), ("hi_enter_q16", .i32, 1), ("current_val_q16", .i32, 1), ("optical_state", .u32, 1),
    ("prim_type", .u32, 1), ("glass_mode", .u32, 1), ("reserved", .u32, 4), ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomRaycastHitLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "ray_hit" "BloomRaycastHit64" "ray_hit" [
    ("hit_valid", .u32, 1), ("hit_inst_id", .u32, 1), ("hit_prefab_id", .u32, 1), ("bvh_nodes_visited", .u32, 1),
    ("t_hit_q16", .i32, 1), ("sdf_dist_q16", .i32, 1), ("hit_pos_q16", .i32, 3), ("hit_normal_q16", .i32, 3),
    ("ray_origin_q16", .i32, 2), ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomImageHeaderLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "img_hdr" "BloomImageHeader64" "img_hdr" [
    ("magic_lo32", .u32, 1), ("magic_hi32", .u32, 1), ("block_cap_mask", .u32, 1), ("version", .u32, 1),
    ("prefab_off", .u32, 1), ("prefab_count", .u32, 1), ("csg_off", .u32, 1), ("csg_count", .u32, 1),
    ("ui_off", .u32, 1), ("ui_count", .u32, 1), ("flow_chart_off", .u32, 1), ("flow_chart_count", .u32, 1),
    ("schema_lo32", .u32, 1), ("schema_hi32", .u32, 1), ("merkle_lo32", .u32, 1), ("merkle_hi32", .u32, 1)
  ]

def bloomSectionSpecLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "section_spec" "BloomSectionSpec64" "section_spec" [
    ("section_mode", .u32, 1), ("inspect_flags", .u32, 1), ("joint_count", .u16, 1), ("brick_count", .u16, 1),
    ("mesh_tri_count", .u16, 1), ("mesh_bvh_count", .u16, 1), ("plane_norm_q16", .i32, 3),
    ("plane_dist_q16", .i32, 1), ("slab_half_q16", .i32, 1), ("min_wall_q16", .i32, 1),
    ("iso_pitch_q16", .i32, 1), ("clearance_tol_q16", .i32, 1), ("global_csg_start", .u16, 1),
    ("global_csg_count", .u16, 1), ("mesh_matter_style", .u32, 1), ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomPhysicsSummaryLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "phys_summary" "BloomPhysicsSummary64" "phys_summary" [
    ("step_epoch", .u32, 1), ("interference_pairs", .u16, 1), ("clearance_warn_pairs", .u16, 1),
    ("ccd_hit_count", .u16, 1), ("pair_inst_a", .u16, 1), ("pair_inst_b", .u16, 1), ("phys_flags", .u16, 1),
    ("min_clearance_q16", .i32, 1), ("earliest_toi_q16", .i32, 1), ("peak_kelvin", .u32, 1),
    ("max_penetration_q16", .i32, 1), ("contact_pos_q16", .i32, 3), ("contact_norm_q16", .i32, 3),
    ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomTriLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "tri" "BloomTri64" "tri" [
    ("tri_id", .u32, 1), ("matter_id", .u16, 1), ("shell_q12", .u16, 1),
    ("v0_q16", .i32, 3), ("v1_q16", .i32, 3), ("v2_q16", .i32, 3), ("normal_q16", .i32, 3),
    ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomFieldJetLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "field_jet" "BloomFieldJet64" "field_jet" [
    ("sdf_q16", .i32, 1), ("grad_q16", .i32, 3), ("hess_diag_q16", .i32, 3),
    ("mean_curv_q16", .i32, 1), ("gauss_curv_q16", .i32, 1), ("prin_dir_q16", .i32, 3),
    ("anisotropy_q16", .i32, 1), ("matter_id", .u32, 1), ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomAudioVoiceLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "audio_voice" "BloomAudioVoice64" "audio_voice" [
    ("active", .u16, 1), ("matter_id", .u16, 1), ("pan_l_q14", .i16, 1), ("pan_r_q14", .i16, 1),
    ("cos_q14", .i16, 4), ("sin_q14", .i16, 4), ("decay_q15", .u16, 4), ("u_q24", .i32, 4), ("v_q24", .i32, 4)
  ]

def bloomAudioHeaderLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "audio_hdr" "BloomAudioHeader64" "audio_hdr" [
    ("sample_rate_hz", .u32, 1), ("voice_cursor", .u32, 1), ("last_phys_epoch", .u32, 1), ("rng_state", .u32, 1),
    ("last_subframe_sample", .u32, 1), ("total_impacts", .u32, 1), ("prev_sec_dist_q16", .i32, 1),
    ("peak_abs_pcm", .i32, 1), ("reserved_hdr", .u32, 6), ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomGameplayStateLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "gameplay" "BloomGameplayState64" "gameplay" [
    ("player_inst_id", .u32, 1), ("delta_hurt_q16", .i32, 1), ("delta_graze_q16", .i32, 1), ("delta_hit_q16", .i32, 1),
    ("hyst_enter_q16", .i32, 1), ("hyst_exit_q16", .i32, 1), ("graze_latched", .u16, 1), ("hitstop_frames", .u16, 1),
    ("input_buf_frames", .u16, 1), ("buffered_action", .u16, 1), ("graze_score", .u32, 1), ("lethal_hits", .u32, 1),
    ("proper_time_q16", .i32, 1), ("audio_duck_q16", .i32, 1), ("buffered_dir_q16", .i32, 2),
    ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomRewindCtrlLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "rewind_ctrl" "BloomRewindCtrl64" "rewind_ctrl" [
    ("capacity", .u32, 1), ("head", .u32, 1), ("count", .u32, 1), ("cursor", .u32, 1),
    ("branch_epoch", .u32, 1), ("total_rewinds", .u32, 1), ("reserved_rw", .u32, 8),
    ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomMetricScaleLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "metric_scale" "BloomMetricScale64" "metric_scale" [
    ("proper_time_q16", .i32, 1), ("lorentz_beta_q16", .i32, 1), ("lorentz_gamma_q16", .i32, 1),
    ("doppler_factor_q16", .i32, 1), ("metric_potential_q16", .i32, 1), ("grav_redshift_q16", .i32, 1),
    ("flux_pressure_q16", .i32, 1), ("spatial_scale_q16", .i32, 1), ("rel_vel_dir_q16", .i32, 3),
    ("center_pos_q16", .i32, 3), ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomMoodboardLayout : Flow.Schema.CellLayout64 :=
  Flow.Schema.CellLayout64.packed64 "moodboard" "BloomMoodboard64" "moodboard" [
    ("dominant_rgb24", .u32, 1), ("accent_rgb24", .u32, 1), ("warmth_q16", .i32, 1), ("contrast_q16", .i32, 1),
    ("edge_density_q16", .i32, 1), ("specular_hint_q16", .i32, 1), ("inferred_matter_id", .u32, 1),
    ("inferred_prim_mask", .u32, 1), ("palette_luma_q16", .i32, 4), ("reserved_mb", .u32, 2),
    ("mac_lo32", .u32, 1), ("mac_hi32", .u32, 1)
  ]

def bloomArenaSchema : Flow.Schema.ArenaSpec where
  cPrefix         := "bloom_schema"
  headerGuard     := "BLOOM_SCHEMA_ABI_H"
  totalCells      := arenaCells
  cCellUnionName  := "BloomCell64"
  cArenaMapName   := "BloomEngineArenaMap"
  cArenaUnionName := "BloomEngineArena"
  regions := [
    { name := "header",       baseSlot := 0,   count := 1,  cSlotMacro := "BLOOM_SLOT_HEADER",          mapDecl := "BloomFrameHeader64 frame_hdr;" },
    { name := "camera",       baseSlot := 1,   count := 1,  cSlotMacro := "BLOOM_SLOT_CAMERA",          mapDecl := "BloomCamera64 camera;" },
    { name := "regbank",      baseSlot := 2,   count := 1,  cSlotMacro := "BLOOM_SLOT_REGBANK",         mapDecl := "BloomRegBank64 reg_bank;" },
    { name := "dom",          baseSlot := 3,   count := 1,  cSlotMacro := "BLOOM_SLOT_DOM",             mapDecl := "BloomDomState64 dom;" },
    { name := "flex",         baseSlot := 4,   count := 1,  cSlotMacro := "BLOOM_SLOT_FLEX",            mapDecl := "BloomFlexContainer64 flex;" },
    { name := "text_input",   baseSlot := 5,   count := 1,  cSlotMacro := "BLOOM_SLOT_TEXT_INPUT",      mapDecl := "BloomTextInput64 text_input;" },
    { name := "ime",          baseSlot := 6,   count := 1,  cSlotMacro := "BLOOM_SLOT_IME",             mapDecl := "BloomIme64 ime;" },
    { name := "window",       baseSlot := 7,   count := 1,  cSlotMacro := "BLOOM_SLOT_WINDOW",          mapDecl := "BloomWindow64 window;" },
    { name := "matter",       baseSlot := 8,   count := 8,  cSlotMacro := "BLOOM_SLOT_MATTER_BASE",     cCountMacro := "BLOOM_MAX_MATTERS",     mapDecl := "BloomMatter64 matters[BLOOM_MAX_MATTERS];" },
    { name := "style",        baseSlot := 16,  count := 8,  cSlotMacro := "BLOOM_SLOT_STYLE_BASE",      cCountMacro := "BLOOM_MAX_STYLES",      mapDecl := "BloomArtStyle64 styles[BLOOM_MAX_STYLES];" },
    { name := "prefab",       baseSlot := 24,  count := 16, cSlotMacro := "BLOOM_SLOT_PREFAB_BASE",     cCountMacro := "BLOOM_MAX_PREFABS",     mapDecl := "BloomPrefabDesc64 prefabs[BLOOM_MAX_PREFABS];" },
    { name := "inst",         baseSlot := 40,  count := 32, cSlotMacro := "BLOOM_SLOT_INST_BASE",       cCountMacro := "BLOOM_MAX_INSTANCES",   mapDecl := "BloomPrefabInst64 instances[BLOOM_MAX_INSTANCES];" },
    { name := "csg",          baseSlot := 72,  count := 32, cSlotMacro := "BLOOM_SLOT_CSG_BASE",        cCountMacro := "BLOOM_MAX_CSG_OPS",     mapDecl := "BloomCsgOp64 csg_ops[BLOOM_MAX_CSG_OPS];" },
    { name := "bvh",          baseSlot := 104, count := 32, cSlotMacro := "BLOOM_SLOT_BVH_BASE",        cCountMacro := "BLOOM_MAX_BVH_NODES",   mapDecl := "BloomBvhNode64 bvh_nodes[BLOOM_MAX_BVH_NODES];" },
    { name := "joint",        baseSlot := 136, count := 8,  cSlotMacro := "BLOOM_SLOT_JOINT_BASE",      cCountMacro := "BLOOM_MAX_JOINTS",      mapDecl := "BloomJointDq64 joints[BLOOM_MAX_JOINTS];" },
    { name := "brick",        baseSlot := 144, count := 8,  cSlotMacro := "BLOOM_SLOT_BRICK_BASE",      cCountMacro := "BLOOM_MAX_BRICKS",      mapDecl := "BloomSdfBrick64 bricks[BLOOM_MAX_BRICKS];" },
    { name := "ui",           baseSlot := 152, count := 16, cSlotMacro := "BLOOM_SLOT_UI_BASE",         cCountMacro := "BLOOM_MAX_UI_NODES",    mapDecl := "BloomUiNode64 ui_nodes[BLOOM_MAX_UI_NODES];" },
    { name := "glyph",        baseSlot := 168, count := 16, cSlotMacro := "BLOOM_SLOT_GLYPH_BASE",      cCountMacro := "BLOOM_MAX_GLYPHS",      mapDecl := "BloomGlyph64 glyphs[BLOOM_MAX_GLYPHS];" },
    { name := "latex",        baseSlot := 184, count := 16, cSlotMacro := "BLOOM_SLOT_LATEX_BASE",      cCountMacro := "BLOOM_MAX_LATEX_BOXES", mapDecl := "BloomLatexBox64 latex_boxes[BLOOM_MAX_LATEX_BOXES];" },
    { name := "flow_state",   baseSlot := 200, count := 32, cSlotMacro := "BLOOM_SLOT_FLOW_STATE_BASE", cCountMacro := "BLOOM_MAX_FLOW_STATES", mapDecl := "uint64_t flow_states[BLOOM_MAX_FLOW_STATES][8];" },
    { name := "flow_chart",   baseSlot := 232, count := 16, cSlotMacro := "BLOOM_SLOT_FLOW_CHART_BASE", cCountMacro := "BLOOM_MAX_FLOW_CHARTS", mapDecl := "uint64_t flow_charts[BLOOM_MAX_FLOW_CHARTS][8];" },
    { name := "raycast_hit",  baseSlot := 248, count := 1,  cSlotMacro := "BLOOM_SLOT_RAYCAST_HIT",     mapDecl := "BloomRaycastHit64 ray_hit;" },
    { name := "image_hdr",    baseSlot := 249, count := 1,  cSlotMacro := "BLOOM_SLOT_IMAGE_HDR",       mapDecl := "BloomImageHeader64 img_hdr;" },
    { name := "ui_label",     baseSlot := 250, count := 4,  cSlotMacro := "BLOOM_SLOT_UI_LABEL_BASE",   mapDecl := "char ui_labels[BLOOM_MAX_UI_NODES][16];" },
    { name := "section_spec", baseSlot := 254, count := 1,  cSlotMacro := "BLOOM_SLOT_SECTION_SPEC",    mapDecl := "BloomSectionSpec64 section_spec;" },
    { name := "phys_summary", baseSlot := 255, count := 1,  cSlotMacro := "BLOOM_SLOT_PHYS_SUMMARY",    mapDecl := "BloomPhysicsSummary64 phys_summary;" }
  ]
  layouts := [
    bloomFrameHeaderLayout, bloomCameraLayout, bloomRegBankLayout, bloomDomStateLayout,
    bloomFlexLayout, bloomTextInputLayout, bloomImeLayout, bloomWindowLayout,
    bloomMatterLayout, bloomArtStyleLayout, bloomPrefabDescLayout, bloomPrefabInstLayout,
    bloomCsgOpLayout, bloomBvhNodeLayout, bloomJointDqLayout, bloomSdfBrickLayout,
    bloomUiNodeLayout, bloomGlyphLayout, bloomLatexBoxLayout, bloomIntentLayout,
    bloomRaycastHitLayout, bloomImageHeaderLayout, bloomSectionSpecLayout, bloomPhysicsSummaryLayout,
    bloomTriLayout, bloomFieldJetLayout, bloomAudioVoiceLayout, bloomAudioHeaderLayout,
    bloomGameplayStateLayout, bloomRewindCtrlLayout, bloomMetricScaleLayout, bloomMoodboardLayout
  ]

def emitSchemaOnly : IO Bool := do
  let ok1 ← bloomArenaSchema.emitC17HeaderChecked "build/bloom_schema_abi.h"
  let ok2 ← bloomArenaSchema.emitWgslModuleChecked "build/bloom_schema.wgsl"
  return ok1 && ok2

/-! ## Part 2: Declarative Three.js + React Native + Prefab SSOT JSI Host API -/

/-! ### Canonical Constitutive Physical Matter Presets (`Slots 8..15`) -/
namespace Matter
  def crystalGlass : UInt32 := 0
  def imperialJade : UInt32 := 1
  def liquidWater  : UInt32 := 2
  def titanium     : UInt32 := 3
  def aurumGold    : UInt32 := 4
  def sapphire     : UInt32 := 5
  def obsidian     : UInt32 := 6
  def blackbody    : UInt32 := 7
end Matter

/-! ### Canonical Reusable 3D Prefab SSOT Blueprints (`Slots 24..39`) -/
namespace Prefab
  def crystalSphere  : UInt32 := 1
  def quantumTorus   : UInt32 := 2
  def citadelCore    : UInt32 := 3
  def octaPrism      : UInt32 := 4
  def capsulePillar  : UInt32 := 5
  def jadeVessel     : UInt32 := 6
  def aurumMonolith  : UInt32 := 7
  def thermalReactor : UInt32 := 8
end Prefab

/-! ### Declarative UI Primitive Kinds (`BloomUiNode64.prim_type`) -/
namespace UiPrim
  def none        : UInt32 := 0
  def rectGlass   : UInt32 := 1
  def circleRing  : UInt32 := 2
  def iconGlyph   : UInt32 := 3
  def sliderBar   : UInt32 := 4
  def latexPanel  : UInt32 := 5
  def readout7Seg : UInt32 := 6
  def textInput   : UInt32 := 7
end UiPrim

/-! ### Physical Liquid Glass Optical Modes (`style_and_state[0..3]`) -/
namespace GlassMode
  def crystal     : UInt32 := 0
  def beerLambert : UInt32 := 1
  def iridescent  : UInt32 := 2
  def mercury     : UInt32 := 3
end GlassMode

/-! ### Built-in Declarative UI Action Opcodes (`BloomUiNode64.action_opcode`) -/
namespace UiAction
  def none         : UInt32 := 0
  def scrubXRay    : UInt32 := 1
  def prefabRevert : UInt32 := 2
  def cycleMatter  : UInt32 := 3
  def swapChart    : UInt32 := 4
  def toggleTrc    : UInt32 := 5
  def focusInput   : UInt32 := 6
end UiAction

namespace SectionMode
  def off    : UInt32 := 0
  def plane  : UInt32 := 1
  def slab   : UInt32 := 2
  def corner : UInt32 := 3
end SectionMode

namespace InspectFlag
  def isolines     : UInt32 := 1
  def wallDfm      : UInt32 := 2
  def interference : UInt32 := 4
  def triplanar    : UInt32 := 8
  def meshSolid    : UInt32 := 16
end InspectFlag

/-! ### Frame Header Bitflags (`BloomFrameHeader64.flags`) -/
namespace FrameFlag
  def idleSleep        : UInt32 := 1
  def xrayEnabled      : UInt32 := 2
  def trcSafe          : UInt32 := 4
  def colorblindGeom   : UInt32 := 8
  def studioGi         : UInt32 := 16
  def radianceCascades : UInt32 := 32
  def diffField        : UInt32 := 64
  def subframeLock     : UInt32 := 128
  def cineOptics       : UInt32 := 256
  def metricField      : UInt32 := 512
  def gameplayShells   : UInt32 := 1024
end FrameFlag

/-! ### FLOW v9.0.0 Canonical Manifold Chart Indices (`Slots 232..247`) -/
namespace Chart
  def uiSpring     : UInt32 := 0
  def uiScrub      : UInt32 := 1
  def cameraRig    : UInt32 := 2
  def xraySweep    : UInt32 := 3
  def prefabNormal : UInt32 := 4
  def prefabAlert  : UInt32 := 5
end Chart

/-- Converts signed Q16.16 `Int` into `Int32` for zero-float JSI transport. -/
def i32Q16 (v : Int) : Int32 :=
  Int32.ofInt v

/-- Converts integer units into Q16.16 `Int32` (`n * 65536`). -/
def q16Int (n : Int) : Int32 :=
  Int32.ofInt (n * 65536)

/-- Converts milli-units (`m / 1000.0`) into Q16.16 `Int32` with 0 floating-point ops. -/
def q16Milli (m : Int) : Int32 :=
  Int32.ofInt ((m * 65536) / 1000)

/-- Opaque In-Process Shared-Arena Engine Handle (`16,384B` VM-Page Arena + SDL3 GPU Context). -/
opaque EnginePointed : NonemptyType
def Engine : Type := EnginePointed.type
instance : Nonempty Engine := EnginePointed.property

@[extern "bloom_jsi_create"]
opaque createEngine (widthPx heightPx : UInt32) (shaderPath : @& String) : IO Engine

@[extern "bloom_jsi_device_info"]
opaque deviceInfo (eng : @& Engine) : IO String

@[extern "bloom_jsi_is_hardware_gpu"]
opaque isHardwareGpu (eng : @& Engine) : IO Bool

@[extern "bloom_jsi_is_unified_memory"]
opaque isUnifiedMemory (eng : @& Engine) : IO Bool

@[extern "bloom_jsi_load_svg"]
opaque loadSvgUI (eng : @& Engine) (svgPath : @& String) : IO UInt32

@[extern "bloom_jsi_compile_latex"]
opaque compileLatex (eng : @& Engine) (latex : @& String)
    (xQ16 yQ16 emQ16 : Int32) (rgb24 : UInt32) : IO UInt32

@[extern "bloom_jsi_clear_ui_dom"]
opaque clearUiDom (eng : @& Engine) (flexModePack padQ16 gapQ16 : UInt32) : IO Unit

@[extern "bloom_jsi_mount_ui_node"]
opaque mountUiNodeRaw (eng : @& Engine) (metaPack posPack sizePack radValPack : UInt64)
    (label : @& String) : IO UInt32

@[extern "bloom_jsi_config_text_input"]
opaque configureTextInput (eng : @& Engine) (boundNodeId action : UInt32)
    (initText : @& String) (anchorXQ16 anchorYQ16 : Int32) : IO Unit

@[extern "bloom_jsi_compile_latex_card"]
opaque compileLatexCardRaw (eng : @& Engine) (latex : @& String)
    (posPack sizePack emPanelPack : UInt64) (rgb24 : UInt32) : IO UInt32

@[extern "bloom_jsi_set_frame_flags"]
opaque setFrameFlags (eng : @& Engine) (enableMask disableMask : UInt32) : IO Unit

@[extern "bloom_jsi_sync_camera_now"]
opaque syncCameraOrbitNow (eng : @& Engine) (yawQ16 pitchQ16 distQ16 : Int32) : IO Unit

@[extern "bloom_jsi_save_image"]
opaque saveArenaImage (eng : @& Engine) (outPath : @& String) : IO Bool

@[extern "bloom_jsi_verify_latex_in_cards"]
opaque verifyLatexInCards (eng : @& Engine) : IO Bool

@[extern "bloom_jsi_spawn_prefab"]
opaque spawnPrefab (eng : @& Engine) (prefabId : UInt32)
    (xQ16 yQ16 zQ16 scaleQ16 : Int32) : IO UInt32

@[extern "bloom_jsi_override_inst"]
opaque overrideInst (eng : @& Engine) (instId styleId matterId : UInt32)
    (paramDeltaQ16 : Int32) : IO Unit

@[extern "bloom_jsi_revert_inst"]
opaque revertInst (eng : @& Engine) (instId : UInt32) : IO Unit

@[extern "bloom_jsi_is_inst_overridden"]
opaque isInstOverridden (eng : @& Engine) (instId : UInt32) : IO Bool

@[extern "bloom_jsi_set_inst_kelvin"]
opaque setInstKelvin (eng : @& Engine) (instId kelvin : UInt32) : IO Unit

@[extern "bloom_jsi_get_inst_flow_slot"]
opaque getInstFlowSlot (eng : @& Engine) (instId : UInt32) : IO UInt32

@[extern "bloom_jsi_get_flow_chart"]
opaque getFlowChart (eng : @& Engine) (flowSlot : UInt32) : IO UInt32

@[extern "bloom_jsi_get_flow_mode"]
opaque getFlowMode (eng : @& Engine) (flowSlot : UInt32) : IO UInt16

@[extern "bloom_jsi_swap_chart"]
opaque swapChart (eng : @& Engine) (flowSlot targetChart : UInt32) (modeReq : UInt16) : IO Bool

@[extern "bloom_jsi_set_reg"]
opaque setReg (eng : @& Engine) (channel : UInt32) (valQ16 : Int32) : IO Unit

@[extern "bloom_jsi_set_camera"]
opaque setCameraOrbit (eng : @& Engine) (yawQ16 pitchQ16 distQ16 : Int32) : IO Unit

@[extern "bloom_jsi_pointer_event"]
opaque dispatchPointer (eng : @& Engine) (cxQ16 cyQ16 dxQ16 : Int32)
    (mouseDown justClicked : Bool) : IO UInt32

@[extern "bloom_jsi_poll_window_input"]
opaque pollWindowInput (eng : @& Engine) : IO UInt32

@[extern "bloom_jsi_step_and_render"]
opaque stepAndRender (eng : @& Engine) (modeReq : UInt16) (forceRender : Bool) : IO Bool

@[extern "bloom_jsi_get_selected_inst"]
opaque getSelectedInst (eng : @& Engine) : IO UInt32

@[extern "bloom_jsi_get_captured_node"]
opaque getCapturedNode (eng : @& Engine) : IO UInt32

@[extern "bloom_jsi_get_xray_q16"]
opaque getXRayPlaneQ16 (eng : @& Engine) : IO Int32

@[extern "bloom_jsi_get_dispatch_stats"]
opaque getDispatchStats (eng : @& Engine) : IO UInt64

@[extern "bloom_jsi_seal_and_verify"]
opaque sealAndVerifyArena (eng : @& Engine) : IO Bool

@[extern "bloom_jsi_export_ppm"]
opaque exportPpm (eng : @& Engine) (ppmPath : @& String) : IO UInt32

@[extern "bloom_jsi_frame_hash"]
opaque frameHash (eng : @& Engine) : IO UInt64

@[extern "bloom_jsi_set_section_spec"]
opaque setSectionSpec (eng : @& Engine) (mode : UInt32) (nxQ16 nyQ16 nzQ16 distQ16 : Int32)
    (inspectFlags : UInt32) : IO Unit

@[extern "bloom_jsi_step_physics"]
opaque stepPhysics (eng : @& Engine) (dtQ16 : UInt32) : IO UInt32

@[extern "bloom_jsi_run_window"]
opaque runNativeWindow (eng : @& Engine) (onFrame : Unit → IO Unit) : IO UInt32

@[extern "bloom_jsi_verify_searu_core"]
opaque verifySearuCore (seed : UInt64) : IO Bool

@[extern "bloom_jsi_verify_gpu_alignment"]
opaque verifyGpuAlignment (eng : @& Engine) : IO Bool

@[extern "bloom_jsi_arena_sha256"]
opaque arenaSha256 (eng : @& Engine) : IO String

@[extern "bloom_jsi_file_sha256"]
opaque fileSha256 (path : @& String) : IO String

/-! ## Part 3: React-Native Style Nested Flexbox DSL & AOT Scene Compiler -/

inductive FlexDir where
  | row
  | column
  deriving Repr, DecidableEq, Inhabited

inductive Justify where
  | start
  | center
  | end_
  | spaceBetween
  deriving Repr, DecidableEq, Inhabited

inductive Align where
  | start
  | center
  | end_
  | stretch
  deriving Repr, DecidableEq, Inhabited

def FlexDir.toCode : FlexDir → UInt32
  | .row    => 0
  | .column => 1

def Justify.toCode : Justify → UInt32
  | .start        => 0
  | .center       => 1
  | .end_         => 2
  | .spaceBetween => 3

def Align.toCode : Align → UInt32
  | .start   => 0
  | .center  => 1
  | .end_    => 2
  | .stretch => 3

structure UiBoxStyle where
  wPx      : Nat    := 0
  hPx      : Nat    := 0
  flexGrow : Nat    := 0
  padPx    : Nat    := 0
  gapPx    : Nat    := 0
  radiusPx : Nat    := 14
  rgb24    : UInt32 := 0x20D8FF
  glass    : UInt32 := GlassMode.beerLambert
  drawBg   : Bool   := false
  deriving Repr, DecidableEq, Inhabited

/-- React-Native style declarative nested Flexbox UI component tree.
    Pure layout `.view` containers (`drawBg == false`) consume **0** `BloomUiNode64` slots. -/
inductive UiElement where
  | view (dir : FlexDir) (justify : Justify) (align : Align) (style : UiBoxStyle) (children : List UiElement)
  | button (label : String) (action : UInt32) (initQ16 : Int32 := 65536) (style : UiBoxStyle := {})
  | slider (label : String) (bindCh : UInt32) (initQ16 : Int32 := 32768) (action : UInt32 := UiAction.scrubXRay) (style : UiBoxStyle := {})
  | textInput (placeholder : String) (initialText : String) (action : UInt32 := UiAction.focusInput) (style : UiBoxStyle := {})
  | latexCard (formula : String) (emPx : Nat := 20) (tintRgb : UInt32 := 0xFFE878) (style : UiBoxStyle := {})
  | ringGauge (bindCh : UInt32) (initQ16 : Int32 := 52428) (style : UiBoxStyle := {})
  | readout7Seg (bindCh : UInt32) (initQ16 : Int32 := 300) (style : UiBoxStyle := {})
  deriving Repr, Inhabited

structure CompiledUiNode where
  primType  : UInt32
  glassMode : UInt32
  bindByte  : UInt32
  action    : UInt32
  rgb24     : UInt32
  cxQ16     : Int32
  cyQ16     : Int32
  hwQ16     : Int32
  hhQ16     : Int32
  radiusQ16 : Int32
  initQ16   : Int32
  label     : String
  deriving Repr, Inhabited

structure CompiledTextInputSpec where
  nodeIndex   : Nat
  action      : UInt32
  initialText : String
  anchorXQ16  : Int32
  anchorYQ16  : Int32
  deriving Repr, Inhabited

structure CompiledLatexCardSpec where
  panelIndex : Nat
  formula    : String
  cxQ16      : Int32
  cyQ16      : Int32
  hwQ16      : Int32
  hhQ16      : Int32
  emQ16      : Int32
  tintRgb    : UInt32
  deriving Repr, Inhabited

structure CompiledUiLayout where
  rootDir     : FlexDir
  rootJustify : Justify
  rootAlign   : Align
  rootPadQ16  : UInt32
  rootGapQ16  : UInt32
  nodes       : List CompiledUiNode
  textInput   : Option CompiledTextInputSpec
  latexCards  : List CompiledLatexCardSpec
  deriving Repr, Inhabited

private structure RectPx where
  x : Nat
  y : Nat
  w : Nat
  h : Nat
  deriving Repr, Inhabited

private def clampInt (v lo hi : Int) : Int :=
  if v < lo then lo else if v > hi then hi else v

private def sanitizeAscii15 (s : String) : String :=
  let chars := s.toList.filter (fun c => decide (32 <= c.toNat ∧ c.toNat <= 126))
  String.ofList (chars.take 15)

private def elemStyle : UiElement → UiBoxStyle
  | .view _ _ _ st _      => st
  | .button _ _ _ st      => st
  | .slider _ _ _ _ st    => st
  | .textInput _ _ _ st   => st
  | .latexCard _ _ _ st   => st
  | .ringGauge _ _ st     => st
  | .readout7Seg _ _ st   => st

private def normCenterQ16 (posPx spanPx vpExtent : Nat) : Int32 :=
  let ext : Int := Int.ofNat (if vpExtent == 0 then 1 else vpExtent)
  let num : Int := ((2 * Int.ofNat posPx + Int.ofNat spanPx) - ext) * 65536
  Int32.ofInt (clampInt (num / ext) (-65536) 65536)

private def normHalfQ16 (spanPx vpExtent : Nat) (minQ16 : Int := 256) : Int32 :=
  let ext : Int := Int.ofNat (if vpExtent == 0 then 1 else vpExtent)
  let raw : Int := (Int.ofNat spanPx * 65536) / ext
  Int32.ofInt (clampInt raw minQ16 65536)

private def normRadiusQ16 (radPx vpH : Nat) (hwQ16 hhQ16 : Int32) : Int32 :=
  let ext : Int := Int.ofNat (if vpH == 0 then 1 else vpH)
  let raw : Int := (Int.ofNat radPx * 2 * 65536) / ext
  let cap : Int := min hwQ16.toInt hhQ16.toInt
  Int32.ofInt (clampInt raw 0 (max cap 0))

private def distributeGrowSizes (dir : FlexDir) (remSpace totalGrow : Nat) :
    List UiBoxStyle → Nat → List Nat
  | [], _ => []
  | s :: rest, remLeft =>
    let base := match dir with | .row => s.wPx | .column => s.hPx
    if s.flexGrow == 0 || totalGrow == 0 then
      let fixed := if base == 0 then 64 else base
      fixed :: distributeGrowSizes dir remSpace totalGrow rest remLeft
    else
      let hasMoreGrow := rest.any (fun r => r.flexGrow > 0)
      let extra := if hasMoreGrow then min ((remSpace * s.flexGrow) / totalGrow) remLeft else remLeft
      (base + extra) :: distributeGrowSizes dir remSpace totalGrow rest (remLeft - extra)

private def placeChildrenLoop (dir : FlexDir) (align : Align)
    (innerX innerY innerCross cursor gapPx extraGapStep extraGapRem idx : Nat) :
    List (UiBoxStyle × Nat) → List RectPx
  | [] => []
  | (st, mainSz) :: rest =>
    let rawCross := match dir with | .row => st.hPx | .column => st.wPx
    let crossSz :=
      if align == .stretch && rawCross == 0 then innerCross
      else if rawCross == 0 then min innerCross 48
      else min rawCross innerCross
    let crossOff :=
      match align with
      | .start   => 0
      | .stretch => 0
      | .center  => (innerCross - crossSz) / 2
      | .end_    => innerCross - crossSz
    let rect : RectPx :=
      match dir with
      | .row    => { x := innerX + cursor, y := innerY + crossOff, w := mainSz, h := crossSz }
      | .column => { x := innerX + crossOff, y := innerY + cursor, w := crossSz, h := mainSz }
    let bonus := if idx < extraGapRem then 1 else 0
    let nextCursor := cursor + mainSz + gapPx + extraGapStep + bonus
    rect :: placeChildrenLoop dir align innerX innerY innerCross nextCursor
      gapPx extraGapStep extraGapRem (idx + 1) rest

private def computeChildRects (dir : FlexDir) (justify : Justify) (align : Align)
    (boxX boxY boxW boxH padPx gapPx : Nat) (styles : List UiBoxStyle) : List RectPx :=
  let padX := min padPx (boxW / 2)
  let padY := min padPx (boxH / 2)
  let innerX := boxX + padX
  let innerY := boxY + padY
  let innerW := boxW - 2 * padX
  let innerH := boxH - 2 * padY
  let innerMain := match dir with | .row => innerW | .column => innerH
  let innerCross := match dir with | .row => innerH | .column => innerW
  let n := styles.length
  if n == 0 then []
  else
    let totalGap := if n > 1 then (n - 1) * gapPx else 0
    let effGap := if totalGap <= innerMain then gapPx else (if n > 1 then innerMain / (n - 1) else 0)
    let usedGap := if n > 1 then (n - 1) * effGap else 0
    let availMain := innerMain - usedGap
    let fixedSum := styles.foldl (fun acc s =>
      let b := match dir with | .row => s.wPx | .column => s.hPx
      acc + (if s.flexGrow > 0 then b else (if b == 0 then 64 else b))) 0
    let totalGrow := styles.foldl (fun acc s => acc + s.flexGrow) 0
    let (mainSizes, freeMain) : List Nat × Nat :=
      if fixedSum <= availMain then
        let rem := availMain - fixedSum
        if totalGrow > 0 then
          (distributeGrowSizes dir rem totalGrow styles rem, 0)
        else
          let szs := styles.map (fun s =>
            let b := match dir with | .row => s.wPx | .column => s.hPx
            if b == 0 then 64 else b)
          (szs, rem)
      else
        let denom := max fixedSum 1
        let szs := styles.map (fun s =>
          let b := match dir with | .row => s.wPx | .column => s.hPx
          let raw := if s.flexGrow > 0 then b else (if b == 0 then 64 else b)
          (raw * availMain) / denom)
        (szs, 0)
    let (startOff, extraStep, extraRem) :=
      match justify with
      | .start        => (0, 0, 0)
      | .center       => (freeMain / 2, 0, 0)
      | .end_         => (freeMain, 0, 0)
      | .spaceBetween =>
        if n > 1 then (0, freeMain / (n - 1), freeMain % (n - 1))
        else (0, 0, 0)
    placeChildrenLoop dir align innerX innerY innerCross startOff
      effGap extraStep extraRem 0 (styles.zip mainSizes)

private structure LayoutAcc where
  nodes      : List CompiledUiNode := []
  textInput  : Option CompiledTextInputSpec := none
  latexCards : List CompiledLatexCardSpec := []

private def pushRectNode (vpW vpH : Nat) (r : RectPx) (prim glass bindByte action rgb24 : UInt32)
    (radiusPx : Nat) (initQ16 : Int32) (label : String) (acc : LayoutAcc) : LayoutAcc × Nat :=
  let idx := acc.nodes.length
  if idx >= 16 then
    (acc, idx)
  else
    let cx := normCenterQ16 r.x r.w vpW
    let cy := normCenterQ16 r.y r.h vpH
    let hw := normHalfQ16 r.w vpW 256
    let hh := normHalfQ16 r.h vpH 256
    let rad := normRadiusQ16 radiusPx vpH hw hh
    let nd : CompiledUiNode := {
      primType  := prim
      glassMode := glass
      bindByte  := bindByte
      action    := action
      rgb24     := rgb24
      cxQ16     := cx
      cyQ16     := cy
      hwQ16     := hw
      hhQ16     := hh
      radiusQ16 := rad
      initQ16   := initQ16
      label     := sanitizeAscii15 label
    }
    ({ acc with nodes := acc.nodes ++ [nd] }, idx)

mutual
  private def compileElem (vpW vpH : Nat) (el : UiElement) (r : RectPx) (acc : LayoutAcc) : LayoutAcc :=
    match el with
    | .view dir justify align st children =>
      let acc1 :=
        if st.drawBg && st.rgb24 != 0 then
          (pushRectNode vpW vpH r UiPrim.rectGlass st.glass 0xFF UiAction.none
            st.rgb24 st.radiusPx 65536 "" acc).1
        else
          acc
      let childRects := computeChildRects dir justify align r.x r.y r.w r.h
        st.padPx st.gapPx (children.map elemStyle)
      compileChildren vpW vpH children childRects acc1

    | .button label action initQ16 st =>
      (pushRectNode vpW vpH r UiPrim.rectGlass st.glass 0xFF action
        st.rgb24 st.radiusPx initQ16 label acc).1

    | .slider label bindCh initQ16 action st =>
      (pushRectNode vpW vpH r UiPrim.sliderBar st.glass (bindCh &&& 0xFF) action
        st.rgb24 st.radiusPx initQ16 label acc).1

    | .textInput placeholder initialText action st =>
      let (acc1, idx) := pushRectNode vpW vpH r UiPrim.textInput st.glass 0xFF action
        st.rgb24 st.radiusPx 65536 placeholder acc
      if idx < 16 && acc1.textInput.isNone then
        let cx := normCenterQ16 r.x r.w vpW
        let cy := normCenterQ16 r.y r.h vpH
        let hw := normHalfQ16 r.w vpW 256
        let hh := normHalfQ16 r.h vpH 256
        let ax := Int32.ofInt (clampInt (cx.toInt - hw.toInt + 1024) (-65536) 65536)
        let ay := Int32.ofInt (clampInt (cy.toInt + hh.toInt) (-65536) 65536)
        let spec : CompiledTextInputSpec := {
          nodeIndex   := idx
          action      := action
          initialText := initialText
          anchorXQ16  := ax
          anchorYQ16  := ay
        }
        { acc1 with textInput := some spec }
      else
        acc1

    | .latexCard formula emPx tintRgb st =>
      let (acc1, idx) := pushRectNode vpW vpH r UiPrim.latexPanel st.glass 0xFF UiAction.none
        st.rgb24 st.radiusPx 65536 "" acc
      if idx < 16 then
        let cx := normCenterQ16 r.x r.w vpW
        let cy := normCenterQ16 r.y r.h vpH
        let hw := normHalfQ16 r.w vpW 256
        let hh := normHalfQ16 r.h vpH 256
        let extH : Int := Int.ofNat (if vpH == 0 then 1 else vpH)
        let emRaw : Int := (Int.ofNat (if emPx == 0 then 18 else emPx) * 2 * 65536) / extH
        let card : CompiledLatexCardSpec := {
          panelIndex := idx
          formula    := formula
          cxQ16      := cx
          cyQ16      := cy
          hwQ16      := hw
          hhQ16      := hh
          emQ16      := Int32.ofInt (clampInt emRaw 512 16384)
          tintRgb    := tintRgb
        }
        { acc1 with latexCards := acc1.latexCards ++ [card] }
      else
        acc1

    | .ringGauge bindCh initQ16 st =>
      let idx := acc.nodes.length
      if idx >= 16 then
        acc
      else
        let rPx := max (min r.w r.h / 2) 6
        let swPx := max (rPx / 4) 3
        let innerRPx := if rPx > swPx then rPx - swPx else 1
        let extW : Int := Int.ofNat (if vpW == 0 then 1 else vpW)
        let outerQ := clampInt ((Int.ofNat rPx * 2 * 65536) / extW) 768 65536
        let innerQ := clampInt ((Int.ofNat innerRPx * 2 * 65536) / extW) 512 (outerQ - 256)
        let nd : CompiledUiNode := {
          primType  := UiPrim.circleRing
          glassMode := st.glass
          bindByte  := bindCh &&& 0xFF
          action    := UiAction.none
          rgb24     := st.rgb24
          cxQ16     := normCenterQ16 r.x r.w vpW
          cyQ16     := normCenterQ16 r.y r.h vpH
          hwQ16     := Int32.ofInt outerQ
          hhQ16     := Int32.ofInt innerQ
          radiusQ16 := 0
          initQ16   := initQ16
          label     := ""
        }
        { acc with nodes := acc.nodes ++ [nd] }

    | .readout7Seg bindCh initQ16 st =>
      (pushRectNode vpW vpH r UiPrim.readout7Seg st.glass (bindCh &&& 0xFF) UiAction.none
        st.rgb24 st.radiusPx initQ16 "" acc).1

  private def compileChildren (vpW vpH : Nat) : List UiElement → List RectPx → LayoutAcc → LayoutAcc
    | [], _, acc => acc
    | _, [], acc => acc
    | c :: cs, rc :: rcs, acc =>
      let acc' := compileElem vpW vpH c rc acc
      compileChildren vpW vpH cs rcs acc'
end

/-- Pure Lean 4 React-Native Flexbox compiler: resolves arbitrary nested `<View>` trees into
    normalized `[-1..+1]` Q16.16 `BloomUiNode64` descriptors, inline 15-char ASCII labels,
    `BloomTextInput64` bindings, and auto-fitted `BloomLatexBox64` cards. -/
def compileUiTree (vpW vpH : Nat) (root : UiElement) : CompiledUiLayout :=
  let safeW := if vpW < 64 then 1280 else vpW
  let safeH := if vpH < 64 then 720 else vpH
  let (rDir, rJust, rAlign, rPadPx, rGapPx) :=
    match root with
    | .view d j a st _ => (d, j, a, st.padPx, st.gapPx)
    | _                => (.column, .start, .stretch, 0, 0)
  let acc := compileElem safeW safeH root { x := 0, y := 0, w := safeW, h := safeH } {}
  let padQ16 : UInt32 := UInt32.ofNat ((rPadPx * 2 * 65536) / safeH)
  let gapQ16 : UInt32 := UInt32.ofNat ((rGapPx * 2 * 65536) / safeH)
  { rootDir     := rDir
    rootJustify := rJust
    rootAlign   := rAlign
    rootPadQ16  := padQ16
    rootGapQ16  := gapQ16
    nodes       := acc.nodes
    textInput   := acc.textInput
    latexCards  := acc.latexCards }

private def packI32Pair (lo hi : Int32) : UInt64 :=
  lo.toUInt32.toUInt64 ||| (hi.toUInt32.toUInt64 <<< 32)

/-- Mounts a compiled Flexbox `CompiledUiLayout` into the 16KB Unified Memory Arena. -/
def mountUiLayout (eng : Engine) (layout : CompiledUiLayout) : IO (UInt32 × UInt32) := do
  let flexPack : UInt32 :=
    layout.rootDir.toCode |||
    (layout.rootJustify.toCode <<< 8) |||
    (layout.rootAlign.toCode <<< 16)
  clearUiDom eng flexPack layout.rootPadQ16 layout.rootGapQ16

  let mut uiMounted : UInt32 := 0
  for nd in layout.nodes do
    let p0 : UInt64 := nd.primType.toUInt64 &&& 0xFF
    let p1 : UInt64 := (nd.glassMode.toUInt64 &&& 0xF) <<< 8
    let p2 : UInt64 := (nd.bindByte.toUInt64 &&& 0xFF) <<< 16
    let p3 : UInt64 := (nd.action.toUInt64 &&& 0xFFFF) <<< 24
    let p4 : UInt64 := (nd.rgb24.toUInt64 &&& 0xFFFFFF) <<< 40
    let metaPack : UInt64 := p0 ||| p1 ||| p2 ||| p3 ||| p4
    let posPack := packI32Pair nd.cxQ16 nd.cyQ16
    let sizePack := packI32Pair nd.hwQ16 nd.hhQ16
    let radValPack := packI32Pair nd.radiusQ16 nd.initQ16
    let nid ← mountUiNodeRaw eng metaPack posPack sizePack radValPack nd.label
    if nid != 0 then
      uiMounted := uiMounted + 1

  match layout.textInput with
  | some ti =>
    configureTextInput eng (UInt32.ofNat (ti.nodeIndex + 1)) ti.action
      ti.initialText ti.anchorXQ16 ti.anchorYQ16
  | none => pure ()

  let mut lxMounted : UInt32 := 0
  for card in layout.latexCards do
    let posPack := packI32Pair card.cxQ16 card.cyQ16
    let sizePack := packI32Pair card.hwQ16 card.hhQ16
    let panelId : UInt32 := UInt32.ofNat (card.panelIndex + 1)
    let emPanelPack : UInt64 :=
      card.emQ16.toUInt32.toUInt64 ||| (panelId.toUInt64 <<< 32)
    let emitted ← compileLatexCardRaw eng card.formula posPack sizePack emPanelPack card.tintRgb
    lxMounted := lxMounted + emitted

  pure (uiMounted, lxMounted)

structure PrefabNodeSpec where
  prefabId : UInt32
  xMilli   : Int
  yMilli   : Int
  zMilli   : Int
  scaleMil : Int := 1000
  deriving Repr

structure StudioBlueprint where
  refWidthPx      : Nat := 1280
  refHeightPx     : Nat := 720
  uiTree          : UiElement
  orbitYawMilli   : Int := 260
  orbitPitchMilli : Int := 280
  orbitDistMilli  : Int := 5200
  enableStudioGi  : Bool := true
  sceneNodes      : List PrefabNodeSpec
  deriving Repr

/-- Mounts a complete React-Native Flexbox UI + LaTeX + 3D Prefab scene into the 16KB Arena. -/
def mountBlueprint (eng : Engine) (bp : StudioBlueprint) : IO (UInt32 × UInt32 × List UInt32) := do
  let layout := compileUiTree bp.refWidthPx bp.refHeightPx bp.uiTree
  let (uiCnt, lxCnt) ← mountUiLayout eng layout
  syncCameraOrbitNow eng
    (q16Milli bp.orbitYawMilli) (q16Milli bp.orbitPitchMilli) (q16Milli bp.orbitDistMilli)
  if bp.enableStudioGi then
    setFrameFlags eng (FrameFlag.studioGi ||| FrameFlag.radianceCascades ||| FrameFlag.diffField ||| FrameFlag.subframeLock) 0
  else
    setFrameFlags eng 0 (FrameFlag.studioGi ||| FrameFlag.radianceCascades ||| FrameFlag.diffField ||| FrameFlag.subframeLock)
  let mut ids : List UInt32 := []
  for item in bp.sceneNodes do
    let iid ← spawnPrefab eng item.prefabId
      (q16Milli item.xMilli) (q16Milli item.yMilli) (q16Milli item.zMilli) (q16Milli item.scaleMil)
    if bp.enableStudioGi && item.prefabId == Prefab.thermalReactor then
      setInstKelvin eng iid 740
    ids := ids ++ [iid]
  pure (uiCnt, lxCnt, ids)

/-- Top-level React-Native / Elm style application state owned by Lean 4. -/
structure StudioState where
  matterCursor : UInt32 := 1
  alertToggled : Bool   := false
  modeWord     : UInt16 := 0x0001
  revertsCount : Nat    := 0
  overridesCnt : Nat    := 0
  swapsCount   : Nat    := 0
  deriving Repr

/-- Purely functional / IO-bridged interactive action reducer over the Unified Memory Arena. -/
def reduceStudioAction (eng : Engine) (st : StudioState) (action : UInt32) : IO StudioState := do
  let selRaw ← getSelectedInst eng
  let targetInst : UInt32 := if selRaw == 0 then 1 else selRaw
  if action == UiAction.prefabRevert then
    revertInst eng targetInst
    setReg eng 2 (Int32.ofInt 300)
    pure { st with revertsCount := st.revertsCount + 1 }
  else if action == UiAction.cycleMatter then
    let nextMat := (st.matterCursor % 7) + 1
    overrideInst eng targetInst nextMat nextMat (q16Milli 120)
    setReg eng 2 (Int32.ofInt (Int.ofNat (nextMat.toNat * 111)))
    pure { st with matterCursor := nextMat, overridesCnt := st.overridesCnt + 1 }
  else if action == UiAction.swapChart then
    let fslot ← getInstFlowSlot eng targetInst
    let nextAlert := !st.alertToggled
    let tgtChart := if nextAlert then Chart.prefabAlert else Chart.prefabNormal
    let _ ← swapChart eng fslot tgtChart st.modeWord
    setInstKelvin eng targetInst (if nextAlert then 860 else 300)
    setReg eng 2 (Int32.ofInt (if nextAlert then 860 else 300))
    pure { st with alertToggled := nextAlert, swapsCount := st.swapsCount + 1 }
  else
    pure st

/-- Canonical v9.0.0 Studio Blueprint authored via React-Native nested `<View>` Flexbox DSL
    (`1280 x 720` reference viewport, scaled cleanly to any resolution). -/
def defaultStudioBlueprint : StudioBlueprint := {
  refWidthPx      := 1280
  refHeightPx     := 720
  orbitYawMilli   := 180
  orbitPitchMilli := 235
  orbitDistMilli  := 6400
  enableStudioGi  := true
  uiTree :=
    .view .column .spaceBetween .stretch { padPx := 20 } [
      -- 1. Top Header Row: Brand Pill + Live Command TextInput + Auto-Fitted Optical LaTeX Card
      .view .row .spaceBetween .center { hPx := 64, gapPx := 16 } [
        .button "BLOOM 9.0" UiAction.none 65536
          { wPx := 196, hPx := 52, radiusPx := 26, rgb24 := 0x28EAFF, glass := GlassMode.crystal },
        .textInput "CMD" "JADE / 860K" UiAction.focusInput
          { flexGrow := 1, hPx := 52, radiusPx := 14, rgb24 := 0x18B8E8, glass := GlassMode.beerLambert },
        .latexCard "\\int \\frac{\\sqrt{\\eta^2 + \\kappa^2}}{2\\pi} d\\Omega" 19 0xFFE878
          { wPx := 360, hPx := 64, radiusPx := 16, rgb24 := 0xFFD258, glass := GlassMode.beerLambert }
      ],
      -- 2. Bottom Control Dock Row: X-Ray Scrub + Revert + Matter + @swap + Ring Gauge + 7-Seg Readout
      .view .row .spaceBetween .center { hPx := 58, gapPx := 14 } [
        .slider "X-RAY" 0 32768 UiAction.scrubXRay
          { flexGrow := 2, hPx := 56, radiusPx := 14, rgb24 := 0x00E8C6, glass := GlassMode.beerLambert },
        .button "REVERT" UiAction.prefabRevert 65536
          { flexGrow := 1, hPx := 56, radiusPx := 14, rgb24 := 0xFFB020, glass := GlassMode.beerLambert },
        .button "MATTER" UiAction.cycleMatter 49152
          { flexGrow := 1, hPx := 56, radiusPx := 14, rgb24 := 0x58A8FF, glass := GlassMode.iridescent },
        .button "@SWAP" UiAction.swapChart 32768
          { flexGrow := 1, hPx := 56, radiusPx := 14, rgb24 := 0xFF4870, glass := GlassMode.beerLambert },
        .ringGauge 1 52428
          { wPx := 56, hPx := 56, rgb24 := 0x20F0B8, glass := GlassMode.crystal },
        .readout7Seg 2 300
          { wPx := 148, hPx := 56, radiusPx := 14, rgb24 := 0x20FFD0, glass := GlassMode.beerLambert }
      ]
    ]
  sceneNodes := [
    { prefabId := Prefab.jadeVessel,     xMilli := 0,     yMilli := -180, zMilli := -120, scaleMil := 1020 },
    { prefabId := Prefab.citadelCore,    xMilli := -2350, yMilli := -240, zMilli := 260,  scaleMil := 860  },
    { prefabId := Prefab.quantumTorus,   xMilli := 2350,  yMilli := -180, zMilli := 220,  scaleMil := 920  },
    { prefabId := Prefab.thermalReactor, xMilli := -1120, yMilli := 480,  zMilli := -1350, scaleMil := 720 }
  ]
}

/-- Single-source WGSL compute shader path (resolved to build/bloom_core.{metal,spv} by SDL3 GPU host). -/
def defaultShaderPath : String := "shaders/bloom_core.wgsl"

/-- Canonical sealed 16,384-byte AOT `.bloom` Studio image path consumed by `build/bloom`. -/
def defaultStudioImagePath : String := "build/studio.bloom"

/-- Compiles `bp` at `(widthPx, heightPx)` and exports a verified `16,384`-byte `.bloom` image. -/
def exportStudioImage (outPath : String := defaultStudioImagePath)
    (widthPx : UInt32 := 1280) (heightPx : UInt32 := 720)
    (bp : StudioBlueprint := defaultStudioBlueprint) : IO Bool := do
  let eng ← createEngine widthPx heightPx defaultShaderPath
  let _ ← mountBlueprint eng bp
  saveArenaImage eng outPath

/-- Default 60Hz interactive Studio launcher (also available natively via `./build/bloom`). -/
def launchDefaultStudio (widthPx : UInt32 := 1280) (heightPx : UInt32 := 720) : IO Unit := do
  let eng ← createEngine widthPx heightPx defaultShaderPath
  let _ ← mountBlueprint eng defaultStudioBlueprint
  let stRef ← IO.mkRef ({} : StudioState)
  let _ ← runNativeWindow eng (fun _ => do
    let act ← pollWindowInput eng
    if act != UiAction.none then
      let st ← stRef.get
      let st' ← reduceStudioAction eng st act
      stRef.set st'
    let _ ← stepAndRender eng 0x0001 false)
  pure ()

/-! ## Part 4: Compile-Time JSI, Flexbox, Hardware Boundary & AOT `.bloom` Export Gate
    Executed once during `Bloom.lean -> Bloom.olean` elaboration (`--load-dynlib`);
    not serialized into `.olean` runtime initialization, keeping `leanchecker` and
    downstream `import Bloom` pure and zero-overhead. -/

private def splitMix64 (x : UInt64) : UInt64 × UInt64 :=
  let s := x + 0x9E3779B97F4A7C15
  let z1 := (s ^^^ (s >>> 30)) * 0xBF58476D1CE4E5B9
  let z2 := (z1 ^^^ (z1 >>> 27)) * 0x94D049BB133111EB
  (s, z2 ^^^ (z2 >>> 31))

private def requireContract (label : String) (cond : Bool) : IO Unit := do
  if !cond then
    throw (IO.userError s!"[BLOOM Compile-Time Boundary Violation] {label}")

def compileTimeBoundaryGate : IO Unit := do
  -- 0. Emit Checked Full-512b C17/C++20 & WGSL Arena Schema + Pure-CPU FLOW SEARU Gate
  let schemaOk ← emitSchemaOnly
  requireContract "FLOW v9.0.0 Full-512b ArenaSpec C17+WGSL Schema emission" schemaOk

  let searuOk ← verifySearuCore 0xB100A60020265EA4
  requireContract "FLOW v9.0.0 SEARU + CholeskySDD + SHA-256 + FIR1 + BVH==CSG 0-ULP + UTF-8/IME" searuOk

  -- 1. Pure Lean 4 Nested React-Native Flexbox Layout Compiler Invariants
  let studioLayout := compileUiTree 1280 720 defaultStudioBlueprint.uiTree
  requireContract "Pure layout <View> wrappers cost 0 BloomUiNode64 slots (exact 9 leaf widgets)"
    (studioLayout.nodes.length == 9 && studioLayout.textInput.isSome && studioLayout.latexCards.length == 1)
  let wrapZero := compileUiTree 1280 720 (.view .column .center .center {} [.view .row .start .stretch {} []])
  let wrapBg := compileUiTree 0 0 (.view .row .end_ .end_ { rgb24 := 0x102030, drawBg := true } [])
  requireContract "Nested <View> 0-slot wrapper vs drawBg glass panel & 0x0 singular viewport guard"
    (wrapZero.nodes.isEmpty && wrapBg.nodes.length == 1)

  -- 2. Hostile & Singular Boundary Sweep + 8-Prefab / X-Ray / UI CPU-vs-GPU Spatial Alignment Gate
  let engEdge ← createEngine 320 180 defaultShaderPath
  let alignOk ← verifyGpuAlignment engEdge
  requireContract "8-Prefab + X-Ray Cutaway + UI Bento CPU Q16.16 vs GPU WGSL f32 alignment" alignOk

  let infoEdge ← deviceInfo engEdge
  let _ ← isHardwareGpu engEdge
  let _ ← isUnifiedMemory engEdge
  requireContract "SDL3 GPU device & driver active"
    (!infoEdge.isEmpty && (infoEdge.splitOn "SDL3-GPU/").length >= 2)

  let iMin : Int32 := Int32.minValue
  let iMax : Int32 := Int32.maxValue
  let _ ← loadSvgUI engEdge "specs/__nonexistent_adversarial__.svg"
  let _ ← compileLatex engEdge "" iMin iMax iMin 0xFFFFFF
  let _ ← compileLatex engEdge "\\frac{\\frac{\\sqrt{\\alpha_1^2}}{}}{\\int_0^\\infty" iMax iMin iMax 0
  let edgeId ← spawnPrefab engEdge 0xFFFFFFFF iMin iMax iMin iMin
  overrideInst engEdge 0xFFFFFFFF 0xFFFFFFFF 0xFFFFFFFF iMin
  setInstKelvin engEdge edgeId 0xFFFFFFFF
  revertInst engEdge edgeId
  setReg engEdge 0xFFFFFFFF iMin
  setCameraOrbit engEdge iMin iMax iMin
  let _ ← dispatchPointer engEdge iMin iMax iMin true true
  let _ ← swapChart engEdge 0xFFFFFFFF 0xFFFFFFFF 0xFFFF
  let _ ← getFlowChart engEdge 0xFFFFFFFF
  let _ ← getCapturedNode engEdge
  let _ ← getXRayPlaneQ16 engEdge
  let _ ← stepPhysics engEdge 65536
  setSectionSpec engEdge SectionMode.plane 46341 46341 0 0 (InspectFlag.isolines ||| InspectFlag.wallDfm ||| InspectFlag.interference ||| InspectFlag.triplanar)
  setSectionSpec engEdge SectionMode.off 65536 0 0 0 (InspectFlag.isolines ||| InspectFlag.wallDfm ||| InspectFlag.interference ||| InspectFlag.triplanar)
  let _ ← stepAndRender engEdge 0xFFFF true
  let edgeSealed ← sealAndVerifyArena engEdge
  requireContract "Adversarial boundary Arena Merkle seal intact" edgeSealed

  -- 3. Dual-Engine 256-Trial SplitMix64 Property Sweep & Pixel-Exact Determinism Gate
  let wPx : UInt32 := 640
  let hPx : UInt32 := 360
  let engA ← createEngine wPx hPx defaultShaderPath
  let engB ← createEngine wPx hPx defaultShaderPath

  let (uiA, lxA, idsA) ← mountBlueprint engA defaultStudioBlueprint
  let (uiB, lxB, idsB) ← mountBlueprint engB defaultStudioBlueprint
  let lxFitA ← verifyLatexInCards engA
  let lxFitB ← verifyLatexInCards engB
  requireContract "Canonical Flexbox Blueprint UI/LaTeX/Prefab isomorphism & zero LaTeX card overflow"
    (uiA == 9 && uiA == uiB && lxA == lxB && idsA == idsB && lxA >= 8 && lxFitA && lxFitB)

  let mut stA : StudioState := {}
  let mut stB : StudioState := {}
  stA ← reduceStudioAction engA stA UiAction.cycleMatter
  stB ← reduceStudioAction engB stB UiAction.cycleMatter
  stA ← reduceStudioAction engA stA UiAction.swapChart
  stB ← reduceStudioAction engB stB UiAction.swapChart
  stA ← reduceStudioAction engA stA UiAction.prefabRevert
  stB ← reduceStudioAction engB stB UiAction.prefabRevert
  requireContract "Top-level Studio reducer determinism"
    (stA.revertsCount == 1 && stB.revertsCount == 1 && stA.swapsCount == stB.swapsCount)

  let mut rng : UInt64 := 0xB100A6002026CAFE
  for _ in [0:256] do
    let (r1, v1) := splitMix64 rng
    let (r2, v2) := splitMix64 r1
    let (r3, v3) := splitMix64 r2
    rng := r3

    let instId   : UInt32 := (v1.toUInt32 % 4) + 1
    let styleId  : UInt32 := ((v1 >>> 8).toUInt32 % 8) + 1
    let matterId : UInt32 := ((v1 >>> 16).toUInt32 % 8)
    let deltaQ16 : Int32  := Int32.ofInt (Int.ofNat (v2.toNat % 65536) - 32768)
    let kelvin   : UInt32 := (250 : UInt32) + ((v2 >>> 16).toUInt32 % 950)

    overrideInst engA instId styleId matterId deltaQ16
    overrideInst engB instId styleId matterId deltaQ16
    setInstKelvin engA instId kelvin
    setInstKelvin engB instId kelvin
    let ovA ← isInstOverridden engA instId
    requireContract "overrideInst sets OVERRIDDEN flag" ovA

    revertInst engA instId
    revertInst engB instId
    let revA ← isInstOverridden engA instId
    let revB ← isInstOverridden engB instId
    requireContract "revertInst restores SSOT cleanly" (!revA && !revB)

    let fslot ← getInstFlowSlot engA instId
    let reqInit : UInt16 := v3.toUInt16
    let _ ← swapChart engA fslot Chart.prefabAlert reqInit
    let _ ← swapChart engB fslot Chart.prefabAlert reqInit
    let lockedBefore ← getFlowMode engA fslot
    let reqAttack : UInt16 := ((v3 >>> 16).toUInt16) ^^^ 0x0001
    let _ ← swapChart engA fslot Chart.prefabNormal reqAttack
    let _ ← swapChart engB fslot Chart.prefabNormal reqAttack
    let modeAfterA ← getFlowMode engA fslot
    let modeAfterB ← getFlowMode engB fslot
    requireContract "KanLock bit 0 preserved across @swap"
      ((modeAfterA &&& 0x0001) == (lockedBefore &&& 0x0001) &&
       (modeAfterA &&& 0xFFFE) == (reqAttack &&& 0xFFFE) &&
       modeAfterA == modeAfterB)

    let cx : Int32 := Int32.ofInt (Int.ofNat ((v3 >>> 32).toNat % 131072) - 65536)
    let cy : Int32 := Int32.ofInt (Int.ofNat ((v3 >>> 48).toNat % 131072) - 65536)
    let dx : Int32 := Int32.ofInt (Int.ofNat ((v2 >>> 32).toNat % 16384) - 8192)
    let down := (v1 &&& 1) == 1
    let clk  := (v1 &&& 3) == 3
    let actA ← dispatchPointer engA cx cy dx down clk
    let actB ← dispatchPointer engB cx cy dx down clk
    requireContract "Deterministic pointer dispatch isomorphism" (actA == actB)

  let _ ← dispatchPointer engA 0 0 0 false false
  let _ ← dispatchPointer engB 0 0 0 false false
  let mut convergedA := false
  let mut convergedB := false
  for _ in [0:512] do
    convergedA ← stepAndRender engA 0x0001 false
    convergedB ← stepAndRender engB 0x0001 false
  requireContract "FLOW v9.0.0 Lyapunov convergence to 0-FPS Idle Sleep" (convergedA && convergedB)

  let statsBefore ← getDispatchStats engA
  for _ in [0:16] do
    let idle ← stepAndRender engA 0x0001 false
    requireContract "Idle frame remains asleep" idle
  let statsAfter ← getDispatchStats engA
  let dispBefore := statsBefore &&& 0xFFFFFFFF
  let dispAfter  := statsAfter  &&& 0xFFFFFFFF
  let skipsAfter := statsAfter >>> 32
  requireContract "Zero GPU dispatches during 0-FPS Idle Sleep"
    (dispAfter == dispBefore && skipsAfter >= 16)

  let _ ← stepAndRender engA 0x0001 true
  let _ ← stepAndRender engB 0x0001 true
  let hA ← frameHash engA
  let hB ← frameHash engB
  let ppmA := "build/.ct_frame_a.ppm"
  let ppmB := "build/.ct_frame_b.ppm"
  let pxA ← exportPpm engA ppmA
  let pxB ← exportPpm engB ppmB
  let bytesA ← IO.FS.readBinFile ppmA
  let bytesB ← IO.FS.readBinFile ppmB
  IO.FS.removeFile ppmA
  IO.FS.removeFile ppmB
  requireContract "Dual-Engine byte-exact GPU framebuffer determinism"
    (hA == hB && hA != 0 && pxA == pxB && pxA > (wPx * hPx) / 10 && bytesA == bytesB)

  let sealedA ← sealAndVerifyArena engA
  let sealedB ← sealAndVerifyArena engB
  requireContract ".bloom 64B Merkle container superblock integrity" (sealedA && sealedB)

  -- 4. AOT Canonical 1280x720 `build/studio.bloom` Export & Exact 16KB Verification
  let engStudio ← createEngine 1280 720 defaultShaderPath
  let (uiS, lxS, idsS) ← mountBlueprint engStudio defaultStudioBlueprint
  let _ ← dispatchPointer engStudio 0 (q16Milli 60) 0 false true
  let lxFitS ← verifyLatexInCards engStudio
  requireContract "Canonical 1280x720 Studio Arena layout & LaTeX card bounds exact"
    (uiS == 9 && lxS >= 8 && idsS.length == 4 && lxFitS)
  let savedOk ← saveArenaImage engStudio defaultStudioImagePath
  requireContract "AOT build/studio.bloom Merkle seal & write succeeded" savedOk
  let imgBytes ← IO.FS.readBinFile defaultStudioImagePath
  requireContract "AOT build/studio.bloom is exact 16,384-byte VM page" (imgBytes.size == arenaBytes)
  let memSha ← arenaSha256 engStudio
  let fileSha ← fileSha256 defaultStudioImagePath
  requireContract "FLOW v9.0.0 Pure-GPR SHA-256 parity across Arena memory & build/studio.bloom"
    (memSha.length == 64 && memSha == fileSha)

end Bloom
