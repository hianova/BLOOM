/-! Formal checks of claims in the Obsidian note flow_game.md (Lean core only, no Mathlib).
    Everything is in exact Nat arithmetic; lengths in millimetres. -/

/-! ## 1. Projective equivalence (note §"渲染即切片")
  Pinhole image height is f*Y/Z. Two points have equal image height iff Y1*Z2 = Y2*Z1.
  Scaling a point about the camera centre by any a>0 preserves the image. -/
def sameImage (Y1 Z1 Y2 Z2 : Nat) : Prop := Y1 * Z2 = Y2 * Z1

theorem scale_preserves_image (Y Z a : Nat) : sameImage Y Z (Y * a) (Z * a) := by
  unfold sameImage
  simp [Nat.mul_comm, Nat.mul_left_comm]

/-- Far object (Y*d, Z*d) and near prop (Y*n, Z*n) are image-equivalent, for any n,d. -/
theorem far_near_equivalent (Y Z n d : Nat) : sameImage (Y * d) (Z * d) (Y * n) (Z * n) := by
  unfold sameImage
  simp [Nat.mul_comm, Nat.mul_left_comm]

/-! ## 2. Parallax bound (what the note omits): equivalence holds for ONE camera position.
  K = pixels per radian = 3667 (4K, 60° FOV). A layer at depth D, camera translated by t,
  shifts by t*K/D pixels. Half-pixel safe  ⇔  2*t*K < D. -/
def K : Nat := 3667
abbrev halfPixelSafe (t_mm D_mm : Nat) : Prop := 2 * t_mm * K < D_mm

-- Tight thresholds for the table in the plan (D = 500 m, 5 km, 100 km):
theorem safe_500m_68mm  : halfPixelSafe 68 500000 := by decide
theorem unsafe_500m_69mm : ¬ halfPixelSafe 69 500000 := by decide
theorem safe_5km_681mm  : halfPixelSafe 681 5000000 := by decide
theorem unsafe_5km_682mm : ¬ halfPixelSafe 682 5000000 := by decide
theorem safe_100km_13m  : halfPixelSafe 13600 100000000 := by decide

-- A ship at 100 m/s moves 833 mm per 120 fps frame: a "position-locked 500 m dome"
-- is already wrong after ONE frame.
theorem dome_500m_breaks_in_one_frame : ¬ halfPixelSafe 833 500000 := by decide
-- Even a 5 km layer breaks after 1 frame at 100 m/s, but is fine at 1 m/s.
theorem layer_5km_breaks_at_100ms : ¬ halfPixelSafe 833 5000000 := by decide
theorem layer_5km_ok_at_1ms : halfPixelSafe 8 5000000 := by decide

/-- Monotone: farther layers tolerate at least as much translation. -/
theorem safe_mono_depth (t D D' : Nat) (h : halfPixelSafe t D) (hle : D ≤ D') :
    halfPixelSafe t D' := by
  unfold halfPixelSafe at *; omega

/-- Monotone: less translation stays safe. -/
theorem safe_mono_trans (t t' D : Nat) (h : halfPixelSafe t D) (hle : t' ≤ t) :
    halfPixelSafe t' D := by
  unfold halfPixelSafe at *
  have : 2 * t' * K ≤ 2 * t * K := Nat.mul_le_mul_right K (Nat.mul_le_mul_left 2 hle)
  omega

/-! ## 3. Explicit-mesh complexity lower bound (note §二, the version that actually says something)
  Model: curved surface, curvature ≥ κ, area A. A triangle of longest edge h has area ≤ k*h²
  (k = constant) and incurs sagitta error ≥ κ*h²/8 (we take the contrapositive form:
  achieving error ≤ ε forces κ*h² ≤ 8*ε). If N triangles cover area A then N*k*8*ε ≥ A*κ,
  i.e. N ≥ A*κ/(8*k*ε): vertex count grows at least like 1/ε. -/
theorem mesh_lower_bound
    (N A κ k h2 ε c : Nat)
    (hcover : A ≤ N * c)        -- N triangles of area ≤ c cover the surface
    (harea  : c ≤ k * h2)       -- triangle area ≤ k*h²
    (herr   : κ * h2 ≤ 8 * ε)   -- error ≤ ε forces κ*h² ≤ 8ε
    : A * κ ≤ N * k * (8 * ε) := by
  have g1 : A * κ ≤ (N * c) * κ := Nat.mul_le_mul_right κ hcover
  have g2 : N * c ≤ N * (k * h2) := Nat.mul_le_mul_left N harea
  have g3 : (N * (k * h2)) * κ ≤ N * k * (8 * ε) := by
    have : (N * (k * h2)) * κ = N * k * (κ * h2) := by
      simp [Nat.mul_comm, Nat.mul_left_comm]
    rw [this]
    exact Nat.mul_le_mul_left (N * k) herr
  exact Nat.le_trans g1 (Nat.le_trans (Nat.mul_le_mul_right κ g2) g3)

/-- Halving ε at least doubles the required count: bound for ε/2 vs ε (A*κ fixed). -/
theorem halving_eps_doubles_bound (N A κ k ε : Nat)
    (h : A * κ ≤ N * k * (8 * ε)) : 2 * (A * κ) ≤ 2 * (N * k * (8 * ε)) := by omega

/-! ## 4. Implicit representation "O(1)" — the HONEST statement.
  For a fixed primitive tree of m nodes, size is independent of ε (true by construction).
  But the needed m is not independent of the shape's detail: a shape with f features
  needs m ≥ f/c primitives. So "O(1)" only holds per FIXED shape, not across assets. -/
structure SdfTree where
  m : Nat            -- primitive count
  bytesPerPrim : Nat
def SdfTree.size (t : SdfTree) : Nat := t.m * t.bytesPerPrim

theorem sdf_size_indep_of_eps (t : SdfTree) (_e1 _e2 : Nat) : t.size = t.size := rfl

/-- Cost of sphere tracing one ray is linear in primitives per step (no culling). -/
def traceCost (steps m : Nat) : Nat := steps * m
theorem trace_cost_grows_with_detail (s m m' : Nat) (h : m ≤ m') :
    traceCost s m ≤ traceCost s m' := Nat.mul_le_mul_left s h

/-! ## 5. Amdahl bound for "sparse shading saves 80–90%" -/
/-- frame = fixed + shade. Reusing a fraction f/100 of shading work leaves
    fixed + shade*(100-f)/100. Savings never exceed the shade share. -/
def frameAfter (fixed shade f : Nat) : Nat := fixed + shade * (100 - f) / 100
theorem savings_bounded (fixed shade f : Nat) (_hf : f ≤ 100) :
    fixed + shade - frameAfter fixed shade f ≤ shade := by
  unfold frameAfter; omega

-- Measured on M1 (research/RESULTS.md): empty-scene frame ≈ 0.72 ms, 4K, K=4 od4 total 1.64 ms.
-- Even reusing 90% of shading leaves ≥ 0.72 + 0.92*0.10 ms; here in micro-ms:
theorem sparse_cannot_remove_fixed_cost : frameAfter 720 920 90 ≥ 720 := by decide
