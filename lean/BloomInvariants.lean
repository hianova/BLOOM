/-!
BLOOM — Pure-Init Formal Invariant Proofs
=========================================
Zero external dependencies (pure Lean 4 `Init`), verified with `lean` and `leanchecker`
with 0 `sorryAx`, 0 `Lean.ofReduceBool`, and 0 `Lean.trustCompiler`.

Proves:
  1. Foveated VRS 15-deg full-res containment & physical-pixel monotonicity
     (matching `include/bloom_gpu.h` and `src/bloom_metal.mm` `eval_vrs_axis_rate`)
  2. BOBR / BOBC / BLMW alignment, layout, ground-ID packing & canonical varint invariants
     (matching `include/bloom_abi.h`, `src/bloom_bobj.c`, `src/bloom_wire.c`)
  3. Generational SlotMap handle non-aliasing & ABA-free slot reuse
     (matching `src/bloom_scene.c` 20-bit index + 12-bit generation)
-/

namespace BloomInvariants

/-! ## ABI & Format Constants (synchronized with `include/bloom_abi.h` & `src/bloom_scene.c`) -/

def CLUSTER_VTX : Nat := 64
def CLUSTER_TRI : Nat := 96
def VTX12_BYTES : Nat := 12
def MESHLET_HDR32_BYTES : Nat := 32
def OBJ_STATE48_BYTES : Nat := 48
def ANALYTIC32_BYTES : Nat := 32
def HUD_BIN28_BYTES : Nat := 28
def BOBJ_HEADER_BYTES : Nat := 24
def BLMW_HEADER_BYTES : Nat := 8

def HANDLE_IDX_MOD : Nat := 1048576
def HANDLE_GEN_MOD : Nat := 4096

def GROUND_SLOT_MOD : Nat := 16384
def GROUND_PART_MOD : Nat := 256
def GROUND_DEPTH_MOD : Nat := 256

/-! ## 1. Foveated VRS 15-deg Full-Res Containment & Physical-Pixel Monotonicity -/

/-- Distance from gaze coordinate `c` to tile interval `[z0, z1]` (matching `eval_vrs_axis_rate`). -/
def tileDist (z0 z1 c : Int) : Int :=
  if c < z0 then z0 - c else if c > z1 then c - z1 else 0

/-- Axis shading rate evaluated per VRS tile (scaled integer / Q16 units). -/
def evalVrsAxisRate (z0 z1 c rFov rMid midRate periphRate fullRate : Int) : Int :=
  if tileDist z0 z1 c ≤ rFov then fullRate
  else if tileDist z0 z1 c ≤ rMid then midRate
  else periphRate

/-- Sum of `Nat` tile rates over `Fin n`. -/
def sumNat : (n : Nat) → (Fin n → Nat) → Nat
  | 0, _     => 0
  | n + 1, f => sumNat n (fun i => f i.castSucc) + f ⟨n, Nat.lt_succ_self n⟩

/-- Total physical shading pixels/units summed over `n` tiles. -/
def physPixels (n : Nat) (tileRate : Fin n → Nat) : Nat :=
  sumNat n tileRate

theorem tileDist_nonneg (z0 z1 c : Int) : 0 ≤ tileDist z0 z1 c := by
  unfold tileDist
  split
  · omega
  · split <;> omega

theorem tileDist_zero_if_contains (z0 z1 c : Int) (h0 : z0 ≤ c) (h1 : c ≤ z1) :
    tileDist z0 z1 c = 0 := by
  unfold tileDist
  have hlt : ¬ (c < z0) := by omega
  have hgt : ¬ (c > z1) := by omega
  simp [hlt, hgt]

/-- Any tile whose interval intersects the 15-degree foveal zone `[c - rFov, c + rFov]`
    (equivalently `z0 ≤ c + rFov ∧ c - rFov ≤ z1`) is shaded at full 1.0x resolution. -/
theorem vrs_15deg_fovea_full_res_containment
    (z0 z1 c rFov rMid midRate periphRate fullRate : Int)
    (_hz : z0 ≤ z1) (hfov : 0 ≤ rFov)
    (h_hi : c - rFov ≤ z1) (h_lo : z0 ≤ c + rFov) :
    evalVrsAxisRate z0 z1 c rFov rMid midRate periphRate fullRate = fullRate := by
  unfold evalVrsAxisRate tileDist
  have hd : (if c < z0 then z0 - c else if c > z1 then c - z1 else 0) ≤ rFov := by
    split
    · omega
    · split <;> omega
  simp [hd]

/-- A tile directly containing the gaze center `c` always receives full 1.0x resolution. -/
theorem vrs_gaze_tile_full_res
    (z0 z1 c rFov rMid midRate periphRate fullRate : Int)
    (h0 : z0 ≤ c) (h1 : c ≤ z1) (hfov : 0 ≤ rFov) :
    evalVrsAxisRate z0 z1 c rFov rMid midRate periphRate fullRate = fullRate :=
  vrs_15deg_fovea_full_res_containment z0 z1 c rFov rMid midRate periphRate fullRate
    (Int.le_trans h0 h1) hfov (by omega) (by omega)

/-- Shading rate is monotonically non-increasing as tile distance from gaze grows. -/
theorem vrs_rate_monotone_in_dist
    (z0 z1 z0' z1' c rFov rMid midRate periphRate fullRate : Int)
    (_h_rad : rFov ≤ rMid)
    (h_pm : periphRate ≤ midRate) (h_mf : midRate ≤ fullRate)
    (h_dist : tileDist z0 z1 c ≤ tileDist z0' z1' c) :
    evalVrsAxisRate z0' z1' c rFov rMid midRate periphRate fullRate ≤
      evalVrsAxisRate z0 z1 c rFov rMid midRate periphRate fullRate := by
  unfold evalVrsAxisRate
  split
  · split
    · exact Int.le_refl fullRate
    · omega
  · split
    · split
      · exact h_mf
      · split
        · exact Int.le_refl midRate
        · omega
    · split
      · exact Int.le_trans h_pm h_mf
      · split
        · exact h_pm
        · exact Int.le_refl periphRate

/-- Increasing per-tile config rates (`periphRate`, `midRate`, `fullRate`) and radii
    monotonically increases the evaluated axis rate. -/
theorem vrs_rate_monotone_in_config
    (z0 z1 c rFov1 rMid1 mid1 periph1 full1 rFov2 rMid2 mid2 periph2 full2 : Int)
    (h_rf : rFov1 ≤ rFov2) (h_rm : rMid1 ≤ rMid2)
    (h_p : periph1 ≤ periph2) (h_m : mid1 ≤ mid2) (h_f : full1 ≤ full2)
    (h_ord2 : periph2 ≤ mid2 ∧ mid2 ≤ full2) :
    evalVrsAxisRate z0 z1 c rFov1 rMid1 mid1 periph1 full1 ≤
      evalVrsAxisRate z0 z1 c rFov2 rMid2 mid2 periph2 full2 := by
  unfold evalVrsAxisRate
  have hpm2 := h_ord2.1
  have hmf2 := h_ord2.2
  split
  · have h2 : tileDist z0 z1 c ≤ rFov2 := by omega
    simp [h2, h_f]
  · split
    · have h2 : tileDist z0 z1 c ≤ rMid2 := by omega
      split
      · exact Int.le_trans h_m hmf2
      · exact h_m
    · split
      · exact Int.le_trans h_p (Int.le_trans hpm2 hmf2)
      · split
        · exact Int.le_trans h_p hpm2
        · exact h_p

theorem sumNat_monotone :
    ∀ (n : Nat) (f g : Fin n → Nat), (∀ i, f i ≤ g i) → sumNat n f ≤ sumNat n g
  | 0, _, _, _      => Nat.le_refl 0
  | n + 1, f, g, h  => by
      have ih := sumNat_monotone n (fun i => f i.castSucc) (fun i => g i.castSucc) (fun i => h i.castSucc)
      have hl := h ⟨n, Nat.lt_succ_self n⟩
      simp [sumNat]
      omega

/-- Pointwise tile-rate monotonicity implies total physical pixel count monotonicity. -/
theorem vrs_physical_pixels_monotone (n : Nat) (r1 r2 : Fin n → Nat)
    (h : ∀ i, r1 i ≤ r2 i) :
    physPixels n r1 ≤ physPixels n r2 :=
  sumNat_monotone n r1 r2 h

theorem sumNat_const (n c : Nat) : sumNat n (fun _ => c) = n * c := by
  induction n with
  | zero => simp [sumNat]
  | succ k ih =>
      simp [sumNat, ih, Nat.succ_mul]

/-- Foveated physical pixel count never exceeds full-resolution (`n * fullRate`). -/
theorem vrs_foveated_phys_le_full (n fullRate : Nat) (tileRate : Fin n → Nat)
    (hle : ∀ i, tileRate i ≤ fullRate) :
    physPixels n tileRate ≤ n * fullRate := by
  unfold physPixels
  rw [← sumNat_const n fullRate]
  exact sumNat_monotone n tileRate (fun _ => fullRate) hle

/-- Anti-vacuity witness: 15-deg fovea (`0.125 = 8192` in Q16) on 16 tiles (`1.0 = 65536`)
    contains central tiles at full 1.0x and reduces outer periphery to 0.25x. -/
theorem vrs_15deg_q16_witness :
    let rFov : Int := 8192
    let rMid : Int := 19660
    let fullR : Int := 65536
    let midR : Int := 32768
    let periphR : Int := 16384
    let gaze : Int := 32768
    evalVrsAxisRate 28672 32768 gaze rFov rMid midR periphR fullR = fullR ∧
    evalVrsAxisRate 0 4096 gaze rFov rMid midR periphR fullR = periphR ∧
    periphR < fullR := by
  decide

/-! ## 2. BOBR / BOBC / BLMW Alignment, Varint & Ground-ID Invariants -/

/-- BOBR header (24B) + `mc` meshlet headers (32B) + `mc * 64` vertices (12B) is 8-byte aligned. -/
theorem bobr_header_geom_8byte_aligned (mc : Nat) :
    (BOBJ_HEADER_BYTES + mc * MESHLET_HDR32_BYTES + mc * (CLUSTER_VTX * VTX12_BYTES)) % 8 = 0 := by
  unfold BOBJ_HEADER_BYTES MESHLET_HDR32_BYTES CLUSTER_VTX VTX12_BYTES
  omega

/-- BOBR object state (`48B`) + analytic primitive (`32B`) blocks are always 16-byte aligned. -/
theorem bobr_object_analytic_16byte_aligned (oc ac : Nat) :
    (oc * OBJ_STATE48_BYTES + ac * ANALYTIC32_BYTES) % 16 = 0 := by
  unfold OBJ_STATE48_BYTES ANALYTIC32_BYTES
  omega

/-- BOBR HUD binary records (`28B`) and BLMW wire header (`8B`) are 4-byte aligned. -/
theorem bobr_hud_blmw_4byte_aligned (hc : Nat) :
    (BLMW_HEADER_BYTES + hc * HUD_BIN28_BYTES) % 4 = 0 := by
  unfold BLMW_HEADER_BYTES HUD_BIN28_BYTES
  omega

/-- ZigZag encoding on `Int` (matching `zz_enc` / `bloom_wire_zz_enc`). -/
def zzEncInt : Int → Nat
  | Int.ofNat n   => 2 * n
  | Int.negSucc n => 2 * n + 1

/-- ZigZag decoding from `Nat` to `Int` (matching `zz_dec` / `bloom_wire_zz_dec`). -/
def zzDecNat (u : Nat) : Int :=
  if u % 2 = 0 then Int.ofNat (u / 2) else Int.negSucc (u / 2)

/-- ZigZag decode is the exact left inverse of ZigZag encode across all `Int`. -/
theorem zigzag_left_inv (x : Int) : zzDecNat (zzEncInt x) = x := by
  cases x with
  | ofNat n =>
      unfold zzEncInt zzDecNat
      simp
  | negSucc n =>
      unfold zzEncInt zzDecNat
      simp
      omega

/-- ZigZag encode is the exact right inverse of ZigZag decode across all `Nat`. -/
theorem zigzag_right_inv (u : Nat) : zzEncInt (zzDecNat u) = u := by
  unfold zzDecNat
  split
  · simp [zzEncInt]; omega
  · dsimp [zzEncInt]; omega

/-- Canonical ULEB128 step preserves value and strictly decreases remaining quotient when `v >= 128`. -/
theorem uleb128_step_exact (v : Nat) :
    (v % 128) + 128 * (v / 128) = v ∧ (128 ≤ v → v / 128 < v) := by
  omega

/-- `u16` byte-lane decomposition (`hi = x / 256`, `lo = x % 256`) is lossless on `[0, 65536)`. -/
theorem u16_byte_lanes_roundtrip (x : Nat) (hx : x < 65536) :
    (x / 256) * 256 + (x % 256) = x ∧ x / 256 < 256 ∧ x % 256 < 256 := by
  omega

/-- Packed 32-bit GPU grounding ID `[domain:2 | slot:14 | part:8 | qdepth8:8]`. -/
def packGroundId (dom slot part depth : Nat) : Nat :=
  ((dom * GROUND_SLOT_MOD + slot) * GROUND_PART_MOD + part) * GROUND_DEPTH_MOD + depth

theorem ground_id_unpack_exact (dom slot part depth : Nat)
    (_hd : dom < 4) (hs : slot < GROUND_SLOT_MOD)
    (hp : part < GROUND_PART_MOD) (hq : depth < GROUND_DEPTH_MOD) :
    let g := packGroundId dom slot part depth
    g % GROUND_DEPTH_MOD = depth ∧
    (g / GROUND_DEPTH_MOD) % GROUND_PART_MOD = part ∧
    (g / (GROUND_DEPTH_MOD * GROUND_PART_MOD)) % GROUND_SLOT_MOD = slot ∧
    g / (GROUND_DEPTH_MOD * GROUND_PART_MOD * GROUND_SLOT_MOD) = dom := by
  unfold packGroundId GROUND_SLOT_MOD GROUND_PART_MOD GROUND_DEPTH_MOD at *
  omega

/-! ## 3. Handle-Generation Non-Aliasing (`src/bloom_scene.c`) -/

/-- Pack 20-bit slot index and 12-bit generation into 32-bit `BloomHandle`. -/
def packHandle (idx gen : Nat) : Nat :=
  (idx % HANDLE_IDX_MOD) + HANDLE_IDX_MOD * (gen % HANDLE_GEN_MOD)

def handleIdx (h : Nat) : Nat := h % HANDLE_IDX_MOD
def handleGen (h : Nat) : Nat := (h / HANDLE_IDX_MOD) % HANDLE_GEN_MOD

/-- Generation advance matching `bloom_scene_spawn_object` (wraps `0` to `1`). -/
def nextGen (g : Nat) : Nat :=
  if (g + 1) % HANDLE_GEN_MOD = 0 then 1 else (g + 1) % HANDLE_GEN_MOD

theorem handle_pack_unpack_exact (idx gen : Nat)
    (hidx : idx < HANDLE_IDX_MOD) (hgen : gen < HANDLE_GEN_MOD) :
    handleIdx (packHandle idx gen) = idx ∧ handleGen (packHandle idx gen) = gen := by
  unfold packHandle handleIdx handleGen HANDLE_IDX_MOD HANDLE_GEN_MOD at *
  omega

/-- Two valid handles are equal if and only if both their slot index and generation match. -/
theorem handle_pack_injective (idx1 gen1 idx2 gen2 : Nat)
    (hi1 : idx1 < HANDLE_IDX_MOD) (hg1 : gen1 < HANDLE_GEN_MOD)
    (hi2 : idx2 < HANDLE_IDX_MOD) (hg2 : gen2 < HANDLE_GEN_MOD)
    (heq : packHandle idx1 gen1 = packHandle idx2 gen2) :
    idx1 = idx2 ∧ gen1 = gen2 := by
  unfold packHandle HANDLE_IDX_MOD HANDLE_GEN_MOD at *
  omega

/-- Any slot `idx >= 1` produces a non-null handle (`!= BLOOM_HANDLE_NULL = 0`). -/
theorem handle_nonnull (idx gen : Nat) (h1 : 1 ≤ idx) (hidx : idx < HANDLE_IDX_MOD) :
    packHandle idx gen ≠ 0 := by
  unfold packHandle HANDLE_IDX_MOD at *
  omega

/-- `nextGen` always yields a valid non-zero generation in `[1, 4095]`. -/
theorem nextGen_bounds_nonzero (g : Nat) :
    1 ≤ nextGen g ∧ nextGen g < HANDLE_GEN_MOD := by
  unfold nextGen HANDLE_GEN_MOD
  split <;> omega

/-- Every destroy + respawn strictly changes the slot generation (`nextGen g ≠ g`). -/
theorem nextGen_neq_prev (g : Nat) (h1 : 1 ≤ g) (h2 : g < HANDLE_GEN_MOD) :
    nextGen g ≠ g := by
  unfold nextGen HANDLE_GEN_MOD at *
  split <;> omega

/-- Slot reuse non-aliasing: after destroying `h1 = packHandle idx g1` and respawning
    into the same `idx` with `h2 = packHandle idx (nextGen g1)`, `h1 ≠ h2` and the stale
    handle `h1` is rejected against the new generation `nextGen g1`. -/
theorem slot_reuse_non_aliasing (idx g1 : Nat)
    (hidx : idx < HANDLE_IDX_MOD) (hg1_lo : 1 ≤ g1) (hg1_hi : g1 < HANDLE_GEN_MOD) :
    let g2 := nextGen g1
    packHandle idx g1 ≠ packHandle idx g2 ∧ handleGen (packHandle idx g1) ≠ g2 := by
  have hneq := nextGen_neq_prev g1 hg1_lo hg1_hi
  have hbnd := nextGen_bounds_nonzero g1
  unfold packHandle handleGen HANDLE_IDX_MOD HANDLE_GEN_MOD at *
  omega

/-- Iterative `k`-step generation advance starting from `g`. -/
def iterNextGen : Nat → Nat → Nat
  | 0, g     => g
  | k + 1, g => iterNextGen k (nextGen g)

/-- Non-zero 1-based modular invariant for `iterNextGen`. -/
theorem iterNextGen_mod (k g : Nat) (hg1 : 1 ≤ g) (hg2 : g < HANDLE_GEN_MOD) :
    1 ≤ iterNextGen k g ∧ iterNextGen k g < HANDLE_GEN_MOD ∧
    (iterNextGen k g - 1) % (HANDLE_GEN_MOD - 1) = (g - 1 + k) % (HANDLE_GEN_MOD - 1) := by
  induction k generalizing g with
  | zero =>
      refine ⟨hg1, hg2, ?_⟩
      unfold iterNextGen HANDLE_GEN_MOD at *
      omega
  | succ m ih =>
      have hb := nextGen_bounds_nonzero g
      have ihm := ih (nextGen g) hb.1 hb.2
      have hstep : (nextGen g - 1) % (HANDLE_GEN_MOD - 1) = (g - 1 + 1) % (HANDLE_GEN_MOD - 1) := by
        unfold nextGen HANDLE_GEN_MOD at *
        split <;> omega
      refine ⟨ihm.1, ihm.2.1, ?_⟩
      unfold iterNextGen
      rw [ihm.2.2]
      have hmod : (g - 1 + (m + 1)) = ((g - 1 + 1) + m) := by omega
      rw [hmod, Nat.add_mod (nextGen g - 1) m, hstep, ← Nat.add_mod]

/-- Full-epoch ABA non-aliasing: for any `1 <= k < 4095` respawns of the same slot,
    the resulting handle generation never aliases the initial generation `g0`. -/
theorem slot_epoch_aba_non_aliasing (idx g0 k : Nat)
    (_hidx : idx < HANDLE_IDX_MOD) (hg1 : 1 ≤ g0) (hg2 : g0 < HANDLE_GEN_MOD)
    (hk1 : 1 ≤ k) (hk2 : k < HANDLE_GEN_MOD - 1) :
    let gk := iterNextGen k g0
    packHandle idx g0 ≠ packHandle idx gk ∧ handleGen (packHandle idx g0) ≠ gk := by
  have hm := iterNextGen_mod k g0 hg1 hg2
  have hdiff : iterNextGen k g0 ≠ g0 := by
    intro heq
    have hm_eq := hm.2.2
    rw [heq] at hm_eq
    unfold HANDLE_GEN_MOD at *
    omega
  unfold packHandle handleGen HANDLE_IDX_MOD HANDLE_GEN_MOD at *
  omega

end BloomInvariants
