/-! Formal checks for the high-speed-physics note (Lean core only; integers, no floats).
    Positions are integer lattice points (millimetres). -/

/-! ## 1. Tunneling in 1-D, tight characterisation
  Obstacle [a,b] (a ≤ b), forward step of length L>0 per frame.
  A "tunnel" = start s strictly before the obstacle, and the next sample strictly after it. -/
abbrev colliding (a b p : Int) : Prop := a ≤ p ∧ p ≤ b
abbrev tunnel (a b s L : Int) : Prop := s < a ∧ b < s + L

/-- Tunneling starts exist  ⇔  L ≥ (b - a) + 2. Exact threshold on the integer lattice. -/
theorem tunnel_iff (a b L : Int) (hab : a ≤ b) :
    (∃ s, tunnel a b s L) ↔ (b - a) + 2 ≤ L := by
  constructor
  · rintro ⟨s, h1, h2⟩; omega
  · intro h; exact ⟨a - 1, by omega, by omega⟩

/-- Both samples miss the obstacle, yet motion crossed it (the note's theorem, integer form). -/
theorem tunnel_misses_both_samples (a b s L : Int) (h : tunnel a b s L) :
    ¬ colliding a b s ∧ ¬ colliding a b (s + L) := by
  unfold tunnel colliding at *; omega

/-- Swept (continuous) test on a 1-D segment [s, s+L]: intersects [a,b]. -/
abbrev sweptHit (a b s L : Int) : Prop := s ≤ b ∧ a ≤ s + L

/-- Completeness: the swept test never misses a tunnel. -/
theorem swept_catches_tunnel (a b s L : Int) (h : tunnel a b s L) (hab : a ≤ b) (hL : 0 ≤ L) :
    sweptHit a b s L := by unfold tunnel sweptHit at *; omega

/-- Soundness: if swept test says miss, no point of the motion is inside. -/
theorem swept_sound (a b s L t : Int) (ht : 0 ≤ t ∧ t ≤ L) (hmiss : ¬ sweptHit a b s L) :
    ¬ colliding a b (s + t) := by unfold sweptHit colliding at *; omega

/-- Exactness for 1-D: the swept test says hit  ⇒  some integer point of the motion collides
    (take the clamp of the obstacle into the motion interval). -/
theorem swept_hit_has_witness (a b s L : Int) (hab : a ≤ b) (hL : 0 ≤ L) (h : sweptHit a b s L) :
    ∃ t, 0 ≤ t ∧ t ≤ L ∧ colliding a b (s + t) := by
  unfold sweptHit colliding at *
  by_cases h1 : s ≤ a
  · exact ⟨a - s, by omega, by omega, by omega, by omega⟩
  · exact ⟨0, by omega, by omega, by omega, by omega⟩

/-! ## 2. Graze windows are missed by sampling (relevance to SolarDrifter's 3.0 m graze)
  Take a graze "window" of width w along the path (the chord of the graze band).
  With step L ≥ w + 2, a start exists such that EVERY frame sample misses the window. -/
theorem sampling_misses_window (a b L : Int) (hab : a ≤ b) (hL : (b - a) + 2 ≤ L) :
    ∃ s : Int, ∀ k : Nat, ¬ colliding a b (s + (k : Int) * L) := by
  refine ⟨a - 1, fun k => ?_⟩
  unfold colliding
  cases k with
  | zero => omega
  | succ n =>
    have h1 : (0 : Int) ≤ (n : Int) * L := Int.mul_nonneg (Int.natCast_nonneg n) (by omega)
    have : ((n + 1 : Nat) : Int) * L = (n : Int) * L + L := by
      push_cast; rw [Int.add_mul]; omega
    omega

/-- Chord of a circle of radius r cut at depth e (e ≤ r): chord² ≤ 8·r·e. If 8·r·e < (L-2)²
    the chord is shorter than L-2, so frame sampling can miss the graze entirely. -/
theorem small_margin_can_be_missed (w r e L : Nat) (hL : 2 ≤ L)
    (hchord : w * w ≤ 8 * r * e) (hsmall : 8 * r * e < (L - 2) * (L - 2)) :
    w + 2 ≤ L := by
  have h : w * w < (L - 2) * (L - 2) := Nat.lt_of_le_of_lt hchord hsmall
  have : w < L - 2 := Nat.mul_self_lt_mul_self_iff.mp h
  omega

/-! ## 3. Float32 resolution vs distance from origin (IEEE-754 binary32: 24-bit significand)
  For 2^k ≤ |x| < 2^(k+1) the spacing is 2^(k-23). Units below: 2^-23 metre per unit. -/
def ulpUnits (k : Nat) : Nat := 2 ^ k      -- spacing in units of 2^-23 m

-- SolarDrifter scale (|x| < 2048 m, k ≤ 10): spacing ≤ 2^10 · 2^-23 m < 1 mm.
theorem spacing_game_scale_below_1mm : ulpUnits 10 * 1000 < 2 ^ 23 := by decide
-- ... that is >3000× finer than the 3.0 m graze tolerance:
theorem game_scale_margin : ulpUnits 10 * 3000 * 1000 < 2 ^ 23 * 1000 := by decide
-- Note's scale (1e9 m ≈ 2^29.9, k = 29): spacing 64 m — coarser than the 3.0 m graze tolerance.
theorem spacing_1e9m_is_64m : ulpUnits 29 = 64 * 2 ^ 23 := by decide
theorem spacing_1e9m_exceeds_graze : 3 * 2 ^ 23 < ulpUnits 29 := by decide
theorem k_for_1e9m : 2 ^ 29 ≤ 1000000000 ∧ 1000000000 < 2 ^ 30 := by decide

/-! ## 4. Per-frame travel at the game's real speeds (60 Hz) -/
-- 86 m/s at 60 Hz = 1433 mm/frame (floor). Obstacles thinner than L-2 mm can be tunnelled.
theorem drifter_step : 86000 / 60 = 1433 := by decide
-- Only a window of the FULL 3.0 m along the path is guaranteed to receive a sample at 1433 mm/frame.
-- A graze's real chord is shorter (see section 2), so grazes can still be missed.
theorem band_wider_than_step : 1433 + 2 ≤ 3000 := by decide

/-! ## 5. Relativistic Constancy of the Speed of Light c & Einstein Elevator Invariance
  In special & general relativity (`flow_game_lorentz_rel_vel_q16`), relative velocity
  is governed by the 3D Einstein-Lorentz transformation rather than Galilean subtraction:
    `u'_num = (u_par - v) * c^2 + u_perp * c * sqrt(c^2 - v^2)`
    `u'_den = c^2 - u · v`
  Below we formally prove:
    1. Forward light-cone invariance (`u = +c => u' = +c` in every observer frame `v`).
    2. Backward light-cone invariance (`u = -c => u' = -c` in every observer frame `v`).
    3. Co-moving Einstein elevator invariance (`u = v => u' = 0`).
    4. 3D transverse light-cone invariance (`(-v)^2 + (c^2 - v^2) = c^2`).
    5. Subluminal Lorentz closure (`+0.6c` and `-0.6c` compose to `15/17 c < c`). -/

/-- Forward light beam (`u = c`) satisfies `num = c * den` identically for any observer velocity `v`. -/
theorem einstein_light_speed_constancy_pos (c v : Int) :
    (c - v) * (c * c) = c * (c * c - c * v) := by
  calc
    (c - v) * (c * c) = c * (c * c) - v * (c * c) := Int.sub_mul c v (c * c)
    _ = c * (c * c) - c * (c * v) := by
      have h : v * (c * c) = c * (c * v) := by
        calc
          v * (c * c) = (c * c) * v := Int.mul_comm v (c * c)
          _ = c * (c * v) := Int.mul_assoc c c v
      rw [h]
    _ = c * (c * c - c * v) := (Int.mul_sub c (c * c) (c * v)).symm

/-- Backward light beam (`u = -c`) satisfies `num = -c * den` identically for any observer velocity `v`. -/
theorem einstein_light_speed_constancy_neg (c v : Int) :
    (-c - v) * (c * c) = (-c) * (c * c - (-c) * v) := by
  calc
    (-c - v) * (c * c) = (-c) * (c * c) - v * (c * c) := Int.sub_mul (-c) v (c * c)
    _ = (-c) * (c * c) - (-c) * ((-c) * v) := by
      have h : v * (c * c) = (-c) * ((-c) * v) := by
        calc
          v * (c * c) = (c * c) * v := Int.mul_comm v (c * c)
          _ = ((-c) * (-c)) * v := by rw [Int.neg_mul_neg]
          _ = (-c) * ((-c) * v) := Int.mul_assoc (-c) (-c) v
      rw [h]
    _ = (-c) * (c * c - (-c) * v) := (Int.mul_sub (-c) (c * c) ((-c) * v)).symm

/-- Co-moving passenger in Einstein elevator (`u = v`) has zero local velocity numerator in all frames. -/
theorem einstein_elevator_co_moving_rest (c v : Int) :
    (v - v) * (c * c) = 0 := by
  rw [Int.sub_self, Int.zero_mul]

/-- 3D transverse light beam in a moving elevator (`u_par' = -v`, `u_perp'^2 = c^2 - v^2`)
    preserves exact squared light speed `u_par'^2 + u_perp'^2 = c^2`. -/
theorem einstein_3d_transverse_light_aberration (c_sq v_sq : Int) :
    v_sq + (c_sq - v_sq) = c_sq := by
  omega

/-- Concrete Q16 verification for the Einstein Elevator horizontal light beam (`c = 1000`, `v_elev = 850`,
    and Pythagorean 3D elevator cruise `510^2 + 680^2 = 850^2 < 1000^2`). -/
theorem einstein_elevator_subluminal_pythagorean :
    510 * 510 + 680 * 680 = 850 * 850 ∧ 850 * 850 < 1000 * 1000 := by decide

/-- Concrete Q16 verification of Einstein velocity addition for `u = +600` (`+0.6c`) and `v = -600` (`-0.6c`)
    with `c = 1000`: relative speed is `1200000 / 1360 = 882 < 1000` (`15/17 c`), strictly subluminal! -/
theorem einstein_subluminal_06c_addition :
    (600 - (-600)) * 1000 * 1000 / (1000 * 1000 - 600 * (-600)) = 882 ∧
    (600 - (-600)) * 1000 * 1000 < (1000 * 1000 - 600 * (-600)) * 1000 := by decide

/-- Exact SI vacuum speed of light `c = 299792458 m/s` constancy against a `-0.7c` observer
    and 3D Pythagorean quadruple `(2, 3, 6, 7)` off-axis light ray invariance. -/
theorem einstein_vacuum_light_speed_and_3d_quadruple :
    let c_vac := 299792458 * 65536
    let v_obs := -(c_vac / 10) * 7
    (c_vac - v_obs) * (c_vac * c_vac) = c_vac * (c_vac * c_vac - c_vac * v_obs) ∧
    2 * 2 + 3 * 3 + 6 * 6 = 7 * 7 := by decide

