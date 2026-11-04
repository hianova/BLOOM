/-! Flat-slice (billboard) validity for perspective cross-sections. Core Lean only.
  Focal length in pixels F = 3326 (4K, 60° FOV). Lengths in mm.
  An object point at lateral offset X, depth Z+d (d = thickness behind the slice plane Z)
  is drawn at F*X/Z on the slice but truly lands at F*X/(Z+d).
  Pixel error = F*X*d / (Z*(Z+d)).  Half-pixel safe  ⇔  2*F*X*d < Z*(Z+d). -/
def F : Nat := 3326
abbrev sliceSafe (X d Z : Nat) : Prop := 2 * F * X * d < Z * (Z + d)

-- 20 m wide ship (half-width 10 m), 10 m thick:
theorem ship_at_1km_ok      : sliceSafe 10000 10000 1000000 := by decide
theorem ship_at_500m_breaks : ¬ sliceSafe 10000 10000 500000 := by decide
-- thinner object (2 m) at 500 m is fine:
theorem thin_at_500m_ok     : sliceSafe 10000 2000 500000 := by decide
-- Planet-scale (6e6 m half-width, 6e6 m deep). At Moon distance (4e8 m) one flat slice is NOT enough,
-- at 1e9 m it is:
theorem planet_at_4e8m_breaks : ¬ sliceSafe 6000000000 6000000000 400000000000 := by decide
theorem planet_at_1e9m_ok     : sliceSafe 6000000000 6000000000 1000000000000 := by decide

/-- Thinner is always safer (error d/(Z+d) is increasing in d). -/
theorem thinner_safer (X d d' Z : Nat) (h : sliceSafe X d' Z) (hle : d ≤ d') : sliceSafe X d Z := by
  unfold sliceSafe at *
  generalize 2 * F * X = A at *
  have hZ : 0 < Z := by
    apply Nat.pos_of_ne_zero; intro hz; subst hz; simp at h
  have s1 : d * (Z + d') ≤ d' * (Z + d) := by
    have e1 : d * Z ≤ d' * Z := Nat.mul_le_mul_right Z hle
    have e2 : d * d' = d' * d := Nat.mul_comm d d'
    rw [Nat.mul_add, Nat.mul_add]; omega
  have s2 : A * d * (Z + d') ≤ A * d' * (Z + d) := by
    rw [Nat.mul_assoc, Nat.mul_assoc]; exact Nat.mul_le_mul_left A s1
  have s3 : A * d' * (Z + d) < Z * (Z + d') * (Z + d) :=
    Nat.mul_lt_mul_of_pos_right h (by omega)
  have s4 : A * d * (Z + d') < Z * (Z + d) * (Z + d') := by
    have : Z * (Z + d') * (Z + d) = Z * (Z + d) * (Z + d') := by
      simp [Nat.mul_assoc, Nat.mul_comm, Nat.mul_left_comm]
    omega
  exact Nat.lt_of_mul_lt_mul_right s4

/-- Farther is always safer (for fixed X,d): Z ≤ Z' keeps the inequality. -/
theorem farther_safer (X d Z Z' : Nat) (h : sliceSafe X d Z) (hle : Z ≤ Z') : sliceSafe X d Z' := by
  unfold sliceSafe at *
  have h2 : Z * (Z + d) ≤ Z' * (Z' + d) := Nat.mul_le_mul hle (by omega)
  omega

/-! Pure camera ROTATION about its centre moves every ray identically regardless of depth:
  image displacement is independent of Z (no parallax term). In this integer model the
  parallax contribution of a rotation is 0, versus translation t which gives 2*t*K vs D (NoteClaims.lean). -/
def rotParallax (_Z : Nat) : Nat := 0
theorem rotation_has_no_depth_dependence (Z Z' : Nat) : rotParallax Z = rotParallax Z' := rfl
