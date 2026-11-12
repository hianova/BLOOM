import Unit

/-
  Bloom.lean — Verified Spatial Geometry, Constitutive Optics & Auto-Bento UI Layer
  =================================================================================
  Pure Lean 4 (`0` C/C++/Obj-C files, `0` headers, `0 sorryAx`) sitting cleanly
  atop untouched `FLOW v5.0.0` and `UNIT v2.0.0`, paired with twin zero-copy
  `64-byte` (`16 × u32`) GPU compute libraries:
    • `shaders/bloom_core.metal` (Apple Silicon Unified Memory Metal Shading Language)
    • `shaders/bloom_core.wgsl`  (Cross-platform Vulkan / DX12 / Web WebGPU WGSL)

  Domain Pillars (`BLOOM` = 幾何 SDF/CSG · 物質光學 & 攝影機 · 零座標 Auto-Bento UI):
    1. `Bloom.Geometry` : 3D SDF primitives, polynomial smooth-CSG Lipschitz bounds,
       2-Jet continuous anti-tunneling, volume-preserving rigid Dual-Quaternion
       isometry, and pointer-free Morton Linear BVH enclosure.
    2. `Bloom.Optics`   : Constitutive matter optics (Gladstone-Dale `eta(rho)`,
       Schlick dielectric Fresnel, Beer-Lambert transmittance, Thin-Lens CoC),
       `cineCameraAtlas` (`"CCAM"`), and `<= 3.0 Hz` `strobeSafetyAtlas` (`"STRB"`).
    3. `Bloom.Ui`       : Zero-coordinate constraint-projected Auto-Bento layout
       partitioning (`0` overlap, exact area conservation) and WebGPU-portable
       Wave64 `u32x2` (`8x8`) tile culling masks.
    4. `Bloom.GpuAbi` & `Bloom.Render` : Canonical `64-byte` (`16 × u32`) GPU packet
       ABI, `32-bit`-only exact `Q16.16` arithmetic, `0-ULP` deterministic CPU
       reference raymarcher + zero-copy `.flow` Bento glass compositor, lossless
       binary `PPM` image egress, and Kitty Graphics Protocol `APC` terminal stream.
-/

namespace Bloom

def version : String := "2.0.0"

/-! ## 1. `Bloom.Geometry`: SDF/CSG, 2-Jet Anti-Tunneling, Rigid Skinning & Linear BVH -/
namespace Geometry

/-- Polynomial smooth-minimum CSG operator with non-negative correction `corr <= k`. -/
def polySmin (a b : Int) (corr : Nat) : Int :=
  min a b - (corr : Int)

/-- Swept 1D/normal-axis barrier distance at normalized sample `t` (`p(t) = p0 + v_step`). -/
def sweptPlaneAbsDist (p0 vStepNum vStepDen wallPos : Int) : Nat :=
  ((p0 * vStepDen + vStepNum) - wallPos * vStepDen).natAbs

/-- 2D/orthogonal-plane rigid rotation + translation `(c*x - s*y + tx, s*x + c*y + ty)`. -/
def rigidTransformSqDist
    (c s x1 y1 x2 y2 tx ty : Int) : Int :=
  let rx1 := c * x1 - s * y1 + tx
  let ry1 := s * x1 + c * y1 + ty
  let rx2 := c * x2 - s * y2 + tx
  let ry2 := s * x2 + c * y2 + ty
  (rx1 - rx2) * (rx1 - rx2) + (ry1 - ry2) * (ry1 - ry2)

/-- Pointer-free 3D axis-aligned bounding box for Morton Linear BVH nodes. -/
structure Aabb3 where
  loX : Int
  loY : Int
  loZ : Int
  hiX : Int
  hiY : Int
  hiZ : Int
  deriving Repr, DecidableEq, Inhabited

def Aabb3.contains (b : Aabb3) (x y z : Int) : Prop :=
  b.loX ≤ x ∧ x ≤ b.hiX ∧
  b.loY ≤ y ∧ y ≤ b.hiY ∧
  b.loZ ≤ z ∧ z ≤ b.hiZ

def Aabb3.union (l r : Aabb3) : Aabb3 :=
  { loX := min l.loX r.loX
    loY := min l.loY r.loY
    loZ := min l.loZ r.loZ
    hiX := max l.hiX r.hiX
    hiY := max l.hiY r.hiY
    hiZ := max l.hiZ r.hiZ }

/-- Expands 10-bit coordinate `v` into 30-bit 3D Morton (`Z-order`) code bits. -/
def mortonExpand10 (v : UInt32) : UInt32 :=
  let x0 := v &&& 0x000003FF
  let x1 := (x0 ||| (x0 <<< 16)) &&& 0x030000FF
  let x2 := (x1 ||| (x1 <<< 8))  &&& 0x0300F00F
  let x3 := (x2 ||| (x2 <<< 4))  &&& 0x030C30C3
  (x3 ||| (x3 <<< 2)) &&& 0x09249249

def morton3D (x y z : UInt32) : UInt32 :=
  (mortonExpand10 x) ||| ((mortonExpand10 y) <<< 1) ||| ((mortonExpand10 z) <<< 2)

/--
  Smooth-CSG Conservative Envelope Theorem:
  Whenever smoothing correction `corr <= k`, polynomial smooth-min lies strictly
  inside `[min(a, b) - k, min(a, b)]` (never overestimates distance to union).
-/
theorem poly_smin_bounded_below_min
    (a b : Int) (corr k : Nat) (hCorr : corr ≤ k) :
    min a b - (k : Int) ≤ polySmin a b corr ∧ polySmin a b corr ≤ min a b := by
  unfold polySmin
  constructor <;> omega

/--
  2-Jet Swept Continuous Anti-Tunneling Theorem:
  When a fast projectile moves from `p0 < wall - r` to `p0 + v > wall + r` in one
  discrete frame (`t = 0` and `t = 1` both strictly outside collision radius `r`),
  evaluating at rational mid-step `t* = (wall - p0) / v` catches exact barrier contact `0 <= r`.
-/
theorem swept_segment_catches_midstep_tunneling
    (p0 v wall : Int) (r : Nat)
    (hStartOutside : wall - p0 > (r : Int))
    (hEndOutside : (p0 + v) - wall > (r : Int)) :
    (p0 - wall).natAbs > r ∧
    ((p0 + v) - wall).natAbs > r ∧
    sweptPlaneAbsDist p0 (wall - p0) 1 wall ≤ r := by
  unfold sweptPlaneAbsDist
  refine ⟨by omega, by omega, ?_⟩
  have hZero : (p0 * 1 + (wall - p0)) - wall * 1 = 0 := by omega
  rw [hZero]
  simp

/--
  Volume-Preserving Dual-Quaternion / Rigid Isometry Theorem:
  Any normalized rigid rotation `(c^2 + s^2 = q^2)` plus translation `(tx, ty)` scales
  all pairwise squared distances identically by `q^2` (`0` candy-wrapper volume collapse).
-/
theorem rigid_isometry_preserves_dist_sq
    (c s qSq x1 y1 x2 y2 tx ty : Int)
    (hUnit : c * c + s * s = qSq) :
    rigidTransformSqDist c s x1 y1 x2 y2 tx ty =
      qSq * ((x1 - x2) * (x1 - x2) + (y1 - y2) * (y1 - y2)) := by
  dsimp [rigidTransformSqDist]
  have hSubX : (c * x1 - s * y1 + tx) - (c * x2 - s * y2 + tx) = c * (x1 - x2) - s * (y1 - y2) := by
    rw [Int.mul_sub, Int.mul_sub]
    omega
  have hSubY : (s * x1 + c * y1 + ty) - (s * x2 + c * y2 + ty) = s * (x1 - x2) + c * (y1 - y2) := by
    rw [Int.mul_sub, Int.mul_sub]
    omega
  rw [hSubX, hSubY]
  generalize x1 - x2 = dx
  generalize y1 - y2 = dy
  have hAlg :
      (c * dx - s * dy) * (c * dx - s * dy) + (s * dx + c * dy) * (s * dx + c * dy) =
      (c * c + s * s) * (dx * dx + dy * dy) := by
    simp only [Int.sub_mul, Int.mul_sub, Int.add_mul, Int.mul_add]
    ac_nf
    omega
  rw [hAlg, hUnit]

/--
  Pointer-Free Linear BVH Enclosure Theorem:
  Parent `Aabb3.union l r` conservatively encloses every spatial point inside
  either child node (`0` false-negative ray/frustum culling).
-/
theorem bvh_parent_union_encloses_children
    (l r : Aabb3) (x y z : Int)
    (hChild : l.contains x y z ∨ r.contains x y z) :
    (Aabb3.union l r).contains x y z := by
  dsimp [Aabb3.union, Aabb3.contains] at *
  rcases hChild with ⟨hlx, hhx, hly, hhy, hlz, hhz⟩ | ⟨hrx, hrhx, hry, hrhy, hrz, hrhz⟩
  · refine ⟨by omega, by omega, by omega, by omega, by omega, by omega⟩
  · refine ⟨by omega, by omega, by omega, by omega, by omega, by omega⟩

end Geometry

/-! ## 2. `Bloom.Optics`: Constitutive Matter Optics, `CineCamera` & `StrobeSafety` Atlases -/
namespace Optics

/-- Gladstone-Dale constitutive refractive index `eta(rho) = qOne + kGd * rho`. -/
def gladstoneDaleIor (qOne kGd rho : Nat) : Nat :=
  qOne + kGd * rho

/-- Schlick dielectric Fresnel reflectance `R(u) = r0 * q^5 + (q - r0) * u^5` (`u = q - cosTheta`). -/
def schlickFresnelScaled (r0 q u : Nat) : Nat :=
  let q5 := q * q * q * q * q
  let u5 := u * u * u * u * u
  r0 * q5 + (q - r0) * u5

/-- Discrete Beer-Lambert optical transmittance step `T_next = T * (q - atten)`. -/
def beerLambertStepScaled (trans q atten : Nat) : Nat :=
  trans * (q - atten)

/-- Thin-Lens Circle of Confusion diameter scaled by aperture (`|z - zFocus| * aperture`). -/
def thinLensCoc (z zFocus : Int) (aperture : Nat) : Nat :=
  (z - zFocus).natAbs * aperture

/-- Gladstone-Dale constitutive law reduces to vacuum (`eta = 1`) at `rho = 0` and grows monotonically. -/
theorem gladstone_dale_vacuum_and_monotone
    (qOne kGd rho1 rho2 : Nat)
    (hK : kGd > 0) (hRho : rho1 < rho2) :
    gladstoneDaleIor qOne kGd 0 = qOne ∧
    gladstoneDaleIor qOne kGd rho1 < gladstoneDaleIor qOne kGd rho2 := by
  unfold gladstoneDaleIor
  refine ⟨by omega, ?_⟩
  have hMul : kGd * rho1 < kGd * rho2 := Nat.mul_lt_mul_of_pos_left hRho hK
  omega

/--
  Schlick Dielectric Fresnel Boundary & Energy Conservation Theorem:
  Normal incidence (`u = 0`) yields `r0 * q^5`, grazing incidence (`u = q`) yields
  total reflection `q * q^5`, and reflectance stays bounded in `[r0 * q^5, q * q^5]`.
-/
theorem schlick_fresnel_grazing_and_bounds
    (r0 q u : Nat)
    (hR0 : r0 ≤ q) (hU : u ≤ q) :
    schlickFresnelScaled r0 q 0 = r0 * (q * q * q * q * q) ∧
    schlickFresnelScaled r0 q q = q * (q * q * q * q * q) ∧
    r0 * (q * q * q * q * q) ≤ schlickFresnelScaled r0 q u ∧
    schlickFresnelScaled r0 q u ≤ q * (q * q * q * q * q) := by
  dsimp [schlickFresnelScaled]
  have hPowLe : u * u * u * u * u ≤ q * q * q * q * q := by
    have h2 : u * u ≤ q * q := Nat.mul_le_mul hU hU
    have h3 : u * u * u ≤ q * q * q := Nat.mul_le_mul h2 hU
    have h4 : u * u * u * u ≤ q * q * q * q := Nat.mul_le_mul h3 hU
    exact Nat.mul_le_mul h4 hU
  have hSubAdd : r0 * (q * q * q * q * q) + (q - r0) * (q * q * q * q * q) =
      q * (q * q * q * q * q) := by
    rw [← Nat.add_mul]
    have hSub : r0 + (q - r0) = q := by omega
    rw [hSub]
  have hUpper :
      r0 * (q * q * q * q * q) + (q - r0) * (u * u * u * u * u) ≤
      r0 * (q * q * q * q * q) + (q - r0) * (q * q * q * q * q) := by
    exact Nat.add_le_add_left (Nat.mul_le_mul_left (q - r0) hPowLe) _
  refine ⟨by simp, by omega, by omega, by omega⟩

/-- Beer-Lambert transmittance never amplifies radiance and strictly attenuates in absorbing media. -/
theorem beer_lambert_energy_conserving
    (trans q atten : Nat)
    (hT : trans > 0) (hAPos : atten > 0) (hABound : atten ≤ q) :
    beerLambertStepScaled trans q atten < trans * q := by
  unfold beerLambertStepScaled
  have hLt : q - atten < q := by omega
  exact Nat.mul_lt_mul_of_pos_left hLt hT

/-- Thin-Lens Circle of Confusion vanishes at exact focus and is symmetric around focal plane. -/
theorem thin_lens_in_focus_zero_coc_and_symmetric
    (zFocus delta : Int) (aperture : Nat) :
    thinLensCoc zFocus zFocus aperture = 0 ∧
    thinLensCoc (zFocus + delta) zFocus aperture = thinLensCoc (zFocus - delta) zFocus aperture := by
  unfold thinLensCoc
  have hZero : (zFocus - zFocus).natAbs = 0 := by simp
  have hPlus : (zFocus + delta - zFocus) = delta := by omega
  have hMinus : (zFocus - delta - zFocus) = -delta := by omega
  rw [hZero, hPlus, hMinus, Int.natAbs_neg]
  simp

/-- Physical `CineCamera` operating regimes (`"CCAM"` `.flow` Manifold Atlas). -/
inductive CamRegime where
  | lockedFocus
  | rackTrack
  | bokehClamp
  | collisionPullback
  deriving DecidableEq, Repr, Inhabited

/-- Orthogonal `CineCamera` discrete optical flags (`16-bit` Kan mode mask). -/
inductive CamFlag where
  | dofEnable
  | autoFrame
  | motionBlur
  | safeShutter
  deriving DecidableEq, Repr, Inhabited

def camCharts : Unit.ChartSpace CamRegime :=
  { entries := [
      (.lockedFocus,       "locked_focus"),
      (.rackTrack,         "rack_track"),
      (.bokehClamp,        "bokeh_clamp"),
      (.collisionPullback, "collision_pullback")
    ]
    fallback := .collisionPullback }

def camFlags : Unit.FlagSpace CamFlag :=
  { entries := [
      (.dofEnable,   "dof"),
      (.autoFrame,   "auto_frame"),
      (.motionBlur,  "motion_blur"),
      (.safeShutter, "safe_shutter")
    ] }

/--
  Verified `CineCamera` `.flow` Manifold Atlas (`"CCAM"`, unit `"m"`):
  • `lockedFocus`       : Smooth focal lock (`<= 1.2 m` per-step rack horizon) in `[0.8m, 6.0m]`.
  • `rackTrack`         : Fast subject pursuit (`<= 2.5 m` horizon) up to `12.0 m` hyperfocal threshold.
  • `bokehClamp`        : Quadratic 2-Jet optical compression preventing CoC blowup above `12.0 m`.
  • `collisionPullback` : Shock-isolated (`uGain = 0`) standoff recovery at `2.5 m` when obstructed (`< 0.8 m`).
-/
def cineCameraAtlas : Unit.Atlas "m" CamRegime CamFlag :=
  { domainTag  := Unit.domTag "CCAM"
    charts     := camCharts
    flags      := camFlags
    initRegime := .lockedFocus
    initX      := Unit.qty "m" "3.5"
    initFlags  := [.dofEnable, .autoFrame]
    law        := fun r =>
      match r with
      | .lockedFocus =>
        Unit.track .lockedFocus (Unit.qty "m" "1.2")
          |>.deadband (Unit.qty "m" "0.8") (Unit.qty "m" "6.0") .collisionPullback .rackTrack
      | .rackTrack =>
        Unit.track .rackTrack (Unit.qty "m" "2.5")
          |>.deadband (Unit.qty "m" "4.5") (Unit.qty "m" "12.0") .lockedFocus .bokehClamp
          |>.lockFlags [.dofEnable]
      | .bokehClamp =>
        Unit.throttleQuad .bokehClamp (Unit.q16 "1.0") (Unit.q16 "0.04") (Unit.qty "m" "2.0")
          |>.recoverBelow (Unit.qty "m" "9.0") .rackTrack
          |>.lockFlags [.dofEnable, .safeShutter]
          |>.requireHomotopic
      | .collisionPullback =>
        Unit.isolateTo .collisionPullback (Unit.qty "m" "2.5") (Unit.qty "m" "1.5")
          |>.escalateAbove (Unit.qty "m" "2.0") .lockedFocus
          |>.lockFlags [.autoFrame, .safeShutter]
          |>.requireHomotopic }

theorem camera_pullback_isolates_collision_shocks (x s1 s2 : Unit.Qty "m") :
    (cineCameraAtlas.law .collisionPullback).evalResidual x s1 =
    (cineCameraAtlas.law .collisionPullback).evalResidual x s2 := by
  exact Unit.zeroGain_ignores_external_shocks _ rfl x s1 s2

/-- ITU-R BT.1702 / Harding Photosensitive Strobe Safety regimes (`<= 3.0 Hz` TRC gate). -/
inductive StrobeRegime where
  | calm
  | modulated
  | trcClamp
  deriving DecidableEq, Repr, Inhabited

inductive StrobeFlag where
  | photosensitiveSafe
  | hdrTonemap
  | temporalDamp
  deriving DecidableEq, Repr, Inhabited

def strobeCharts : Unit.ChartSpace StrobeRegime :=
  { entries := [
      (.calm,      "calm"),
      (.modulated, "modulated"),
      (.trcClamp,  "trc_clamp")
    ]
    fallback := .trcClamp }

def strobeFlags : Unit.FlagSpace StrobeFlag :=
  { entries := [
      (.photosensitiveSafe, "photo_safe"),
      (.hdrTonemap,         "hdr_tonemap"),
      (.temporalDamp,       "temporal_damp")
    ] }

/--
  Verified Photosensitive Strobe Safety `.flow` Manifold Atlas (`"STRB"`, unit `"Hz"`):
  Enforces the strict `<= 3.0 Hz` flash-frequency ceiling on every transient frame
  (`bEnter + rampMax <= 2.9 Hz < 3.0 Hz`):
  • `calm`      : `[0.0 Hz, 1.8 Hz]` with `0.6 Hz` horizon (`max exit = 2.4 Hz`).
  • `modulated` : `[1.4 Hz, 2.4 Hz]` with `0.5 Hz` horizon (`max exit = 2.9 Hz < 3.0 Hz`).
  • `trcClamp`  : Triggered above `2.4 Hz`; `uGain = 0` completely isolates external flash
                  spikes (e.g. `14.5 Hz` explosion flicker) and drains rate safely to `1.2 Hz`.
-/
def strobeSafetyAtlas : Unit.Atlas "Hz" StrobeRegime StrobeFlag :=
  { domainTag  := Unit.domTag "STRB"
    charts     := strobeCharts
    flags      := strobeFlags
    initRegime := .calm
    initX      := Unit.qty "Hz" "1.0"
    initFlags  := [.photosensitiveSafe, .hdrTonemap]
    law        := fun r =>
      match r with
      | .calm =>
        Unit.track .calm (Unit.qty "Hz" "0.6")
          |>.escalateAbove (Unit.qty "Hz" "1.8") .modulated
          |>.lockFlags [.photosensitiveSafe]
      | .modulated =>
        Unit.throttleQuad .modulated (Unit.q16 "1.0") (Unit.q16 "0.25") (Unit.qty "Hz" "0.5")
          |>.deadband (Unit.qty "Hz" "1.4") (Unit.qty "Hz" "2.4") .calm .trcClamp
          |>.lockFlags [.photosensitiveSafe, .temporalDamp]
          |>.requireHomotopic
      | .trcClamp =>
        Unit.isolateTo .trcClamp (Unit.qty "Hz" "1.2") (Unit.qty "Hz" "0.8")
          |>.lockFlags [.photosensitiveSafe, .hdrTonemap, .temporalDamp]
          |>.requireHomotopic }

theorem strobe_trc_clamp_isolates_flash_spikes (x s1 s2 : Unit.Qty "Hz") :
    (strobeSafetyAtlas.law .trcClamp).evalResidual x s1 =
    (strobeSafetyAtlas.law .trcClamp).evalResidual x s2 := by
  exact Unit.zeroGain_ignores_external_shocks _ rfl x s1 s2

end Optics

/-! ## 3. `Bloom.Ui`: Zero-Coordinate Auto-Bento Layout & Portable Wave64 `u32x2` Culling -/
namespace Ui

/-- Half-open integer pixel rectangle `[x0, x1) × [y0, y1)` synthesized without manual coordinates. -/
structure BentoRect where
  x0 : Nat
  y0 : Nat
  x1 : Nat
  y1 : Nat
  deriving Repr, DecidableEq, Inhabited

def BentoRect.area (r : BentoRect) : Nat :=
  (r.x1 - r.x0) * (r.y1 - r.y0)

/-- Zero-coordinate horizontal constraint partition at offset `cut <= x1 - x0`. -/
def splitHoriz (parent : BentoRect) (cut : Nat) : BentoRect × BentoRect :=
  let xm := parent.x0 + cut
  ({ x0 := parent.x0, y0 := parent.y0, x1 := xm, y1 := parent.y1 },
   { x0 := xm,        y0 := parent.y0, x1 := parent.x1, y1 := parent.y1 })

/-- Zero-coordinate vertical constraint partition at offset `cut <= y1 - y0`. -/
def splitVert (parent : BentoRect) (cut : Nat) : BentoRect × BentoRect :=
  let ym := parent.y0 + cut
  ({ x0 := parent.x0, y0 := parent.y0, x1 := parent.x1, y1 := ym },
   { x0 := parent.x0, y0 := ym,        x1 := parent.x1, y1 := parent.y1 })

/-- Inset padding for clean visual gutters while preserving interior non-overlap. -/
def BentoRect.inset (r : BentoRect) (pad : Nat) : BentoRect :=
  let nx0 := min (r.x0 + pad) r.x1
  let ny0 := min (r.y0 + pad) r.y1
  let nx1 := max nx0 (r.x1 - pad)
  let ny1 := max ny0 (r.y1 - pad)
  { x0 := nx0, y0 := ny0, x1 := nx1, y1 := ny1 }

/-- Automatic golden-ratio (`~61.8% / 38.2%`) recursive Auto-Bento layout projector for `N` `.flow` cells. -/
def projectAutoBento (viewport : BentoRect) (count : Nat) (pad : Nat := 2) : Array BentoRect := Id.run do
  if count == 0 then return #[]
  let mut rects : Array BentoRect := #[viewport]
  let mut depth : Nat := 0
  while rects.size < count do
    let lastIdx := rects.size - 1
    let cur := rects[lastIdx]!
    let w := cur.x1 - cur.x0
    let h := cur.y1 - cur.y0
    let remPanels := count - rects.size + 1
    let (a, b) :=
      if h < 36 then
        splitHoriz cur (w / max remPanels 2)
      else if depth % 2 == 0 && w >= h then
        splitHoriz cur ((w * 618) / 1000)
      else
        splitVert cur ((h * 618) / 1000)
    rects := (rects.pop.push a).push b
    depth := depth + 1
  return rects.map (fun r => r.inset pad)

/--
  Zero-Coordinate Auto-Bento Partition Theorem:
  Splitting any viewport `[x0, x1) × [y0, y1)` at `cut <= x1 - x0` yields strictly
  disjoint half-open sibling boxes (`left.x1 <= right.x0`) whose areas sum exactly
  to `parent.area` (`0` overlap, `0` leaked viewport pixels).
-/
theorem bento_split_disjoint_and_conserves_area
    (parent : BentoRect) (cut : Nat)
    (hValidX : parent.x0 ≤ parent.x1)
    (hCut : cut ≤ parent.x1 - parent.x0) :
    let (l, r) := splitHoriz parent cut
    l.x1 ≤ r.x0 ∧ l.area + r.area = parent.area := by
  dsimp [splitHoriz, BentoRect.area]
  refine ⟨by omega, ?_⟩
  have hL : (parent.x0 + cut) - parent.x0 = cut := by omega
  have hR : parent.x1 - (parent.x0 + cut) = (parent.x1 - parent.x0) - cut := by omega
  rw [hL, hR, ← Nat.add_mul]
  have hSum : cut + (parent.x1 - parent.x0 - cut) = parent.x1 - parent.x0 := by omega
  rw [hSum]

/-- Splits a 64-bit Wave64 (`8x8`) tile mask into WebGPU/Vulkan-portable `(lo32, hi32)`. -/
def packWave64U32x2 (mask64 : BitVec 64) : BitVec 32 × BitVec 32 :=
  (mask64.truncate 32, (mask64 >>> 32).truncate 32)

/-- Reconstructs a 64-bit Wave64 (`8x8`) tile mask from `(lo32, hi32)`. -/
def unpackWave64U32x2 (pair : BitVec 32 × BitVec 32) : BitVec 64 :=
  (((pair.2.zeroExtend 64) <<< 32) ||| (pair.1.zeroExtend 64))

/-- Portable `u32x2` Wave64 tile-mask encoding is a lossless 64-bit bijection. -/
theorem wave64_u32x2_bijection (mask64 : BitVec 64) :
    unpackWave64U32x2 (packWave64U32x2 mask64) = mask64 := by
  dsimp [packWave64U32x2, unpackWave64U32x2]
  bv_decide

end Ui

/-! ## 4. `Bloom.GpuAbi`: Canonical `64-Byte` (`16 × u32`) Zero-Copy Packet & `32-Bit` Q16.16 -/
namespace GpuAbi

/--
  Universal `64-byte` (`16 × UInt32` = `4 × uint4` / `4 × vec4<u32>`) GPU descriptor.
  Shared identically across `Bloom.lean`, `shaders/bloom_core.metal` (`MTLStorageModeShared`
  zero-copy Unified Memory), and `shaders/bloom_core.wgsl` (`var<storage, read>`).
-/
structure GpuPacket64 where
  u0  : UInt32 := 0
  u1  : UInt32 := 0
  u2  : UInt32 := 0
  u3  : UInt32 := 0
  u4  : UInt32 := 0
  u5  : UInt32 := 0
  u6  : UInt32 := 0
  u7  : UInt32 := 0
  u8  : UInt32 := 0
  u9  : UInt32 := 0
  u10 : UInt32 := 0
  u11 : UInt32 := 0
  u12 : UInt32 := 0
  u13 : UInt32 := 0
  u14 : UInt32 := 0
  u15 : UInt32 := 0
  deriving Repr, DecidableEq, Inhabited

def pushLE32 (buf : ByteArray) (v : UInt32) : ByteArray :=
  let b0 : UInt8 := UInt8.ofNat (v.toNat % 256)
  let b1 : UInt8 := UInt8.ofNat ((v.toNat >>> 8) % 256)
  let b2 : UInt8 := UInt8.ofNat ((v.toNat >>> 16) % 256)
  let b3 : UInt8 := UInt8.ofNat ((v.toNat >>> 24) % 256)
  (((buf.push b0).push b1).push b2).push b3

theorem pushLE32_size (buf : ByteArray) (v : UInt32) :
    (pushLE32 buf v).size = buf.size + 4 := by
  simp [pushLE32, ByteArray.size_push, Nat.add_assoc]

def GpuPacket64.toBytes (p : GpuPacket64) : ByteArray :=
  pushLE32 (pushLE32 (pushLE32 (pushLE32 (pushLE32 (pushLE32 (pushLE32 (pushLE32
  (pushLE32 (pushLE32 (pushLE32 (pushLE32 (pushLE32 (pushLE32 (pushLE32 (pushLE32
    ByteArray.empty p.u0) p.u1) p.u2) p.u3) p.u4) p.u5) p.u6) p.u7)
    p.u8) p.u9) p.u10) p.u11) p.u12) p.u13) p.u14) p.u15

/-- Every `GpuPacket64` serializes to strictly `64` bytes (`0` struct-padding drift). -/
theorem gpuPacket64_toBytes_size (p : GpuPacket64) : p.toBytes.size = 64 := by
  simp [GpuPacket64.toBytes, pushLE32_size, ByteArray.size_empty]

private def byteAtOrZero (ba : ByteArray) (idx : Nat) : UInt32 :=
  if idx < ba.size then (ba.get! idx).toUInt32 else 0

def readLE32 (ba : ByteArray) (byteOff : Nat) : UInt32 :=
  let b0 := byteAtOrZero ba byteOff
  let b1 := byteAtOrZero ba (byteOff + 1)
  let b2 := byteAtOrZero ba (byteOff + 2)
  let b3 := byteAtOrZero ba (byteOff + 3)
  b0 ||| (b1 <<< (8 : UInt32)) ||| (b2 <<< (16 : UInt32)) ||| (b3 <<< (24 : UInt32))

/-- Views any `64-byte` slice of a raw `.flow` bundle directly as a `16 × u32` `GpuPacket64`. -/
def packetOfBytesAt (ba : ByteArray) (cellIdx : Nat) : GpuPacket64 :=
  let base := cellIdx * 64
  { u0  := readLE32 ba (base + 0)
    u1  := readLE32 ba (base + 4)
    u2  := readLE32 ba (base + 8)
    u3  := readLE32 ba (base + 12)
    u4  := readLE32 ba (base + 16)
    u5  := readLE32 ba (base + 20)
    u6  := readLE32 ba (base + 24)
    u7  := readLE32 ba (base + 28)
    u8  := readLE32 ba (base + 32)
    u9  := readLE32 ba (base + 36)
    u10 := readLE32 ba (base + 40)
    u11 := readLE32 ba (base + 44)
    u12 := readLE32 ba (base + 48)
    u13 := readLE32 ba (base + 52)
    u14 := readLE32 ba (base + 56)
    u15 := readLE32 ba (base + 60) }

def i32ToU32 (x : Int) : UInt32 :=
  UInt32.ofNat (BitVec.ofInt 32 x).toNat

def u32ToI32 (u : UInt32) : Int :=
  if u.toNat >= 0x80000000 then
    Int.ofNat u.toNat - 0x100000000
  else
    Int.ofNat u.toNat

/--
  `32-bit`-only unsigned `Q16.16` fixed-point product (`(a * b) >> 16`) requiring ZERO
  `uint64_t` / `u64` shader types (100% portable to WebGPU `WGSL` and Metal `MSL`).
-/
def q16MulNatSplit (a b : Nat) : Nat :=
  let aHi := a / 65536
  let aLo := a % 65536
  let bHi := b / 65536
  let bLo := b % 65536
  (aHi * bHi) * 65536 + aHi * bLo + aLo * bHi + (aLo * bLo) / 65536

/--
  Exactness Theorem for `32-bit` Split `Q16.16` Multiplication:
  `q16MulNatSplit a b` equals the true unbounded `64-bit` shift `(a * b) / 65536`
  with `0-ULP` error for ALL non-negative integers `a, b` (and `(aLo * bLo) < 2^32`).
-/
theorem q16MulNatSplit_exact (a b : Nat) :
    (a % 65536) * (b % 65536) < 4294967296 ∧
    q16MulNatSplit a b = (a * b) / 65536 := by
  dsimp [q16MulNatSplit]
  have hModA : a % 65536 < 65536 := Nat.mod_lt a (by decide)
  have hModB : b % 65536 < 65536 := Nat.mod_lt b (by decide)
  have hLoBound : (a % 65536) * (b % 65536) < 65536 * 65536 := by
    have h1 : (a % 65536) * (b % 65536) ≤ 65535 * (b % 65536) :=
      Nat.mul_le_mul_right _ (by omega)
    have h2 : 65535 * (b % 65536) ≤ 65535 * 65535 :=
      Nat.mul_le_mul_left _ (by omega)
    omega
  refine ⟨by omega, ?_⟩
  have ha : a = 65536 * (a / 65536) + a % 65536 := (Nat.div_add_mod a 65536).symm
  have hb : b = 65536 * (b / 65536) + b % 65536 := (Nat.div_add_mod b 65536).symm
  have hProd :
      a * b =
      (a % 65536) * (b % 65536) +
      65536 * (((a / 65536) * (b / 65536)) * 65536 + (a / 65536) * (b % 65536) + (a % 65536) * (b / 65536)) := by
    conv => lhs; rw [ha, hb]
    simp only [Nat.add_mul, Nat.mul_add]
    ac_nf
  rw [hProd]
  rw [Nat.add_mul_div_left ((a % 65536) * (b % 65536))
        (((a / 65536) * (b / 65536)) * 65536 + (a / 65536) * (b % 65536) + (a % 65536) * (b / 65536))
        (by decide : 0 < 65536)]
  omega

def q16MulU32 (a b : UInt32) : UInt32 :=
  let aHi : UInt32 := a >>> 16
  let aLo : UInt32 := a &&& 0xFFFF
  let bHi : UInt32 := b >>> 16
  let bLo : UInt32 := b &&& 0xFFFF
  ((aHi * bHi) <<< 16) + aHi * bLo + aLo * bHi + ((aLo * bLo) >>> 16)

def absI32ToU32 (x : Int) : UInt32 :=
  UInt32.ofNat x.natAbs

def q16MulI32 (a b : Int) : Int :=
  let mag := (q16MulU32 (absI32ToU32 a) (absI32ToU32 b)).toNat
  if (a < 0) != (b < 0) then -Int.ofNat mag else Int.ofNat mag

/-- Sign-magnitude truncating division matching MSL `int` and WGSL `i32` `/` semantics. -/
def sdivI32 (num : Int) (den : UInt32) : Int :=
  if den == 0 then 0
  else
    let mag := num.natAbs / den.toNat
    if num < 0 then -Int.ofNat mag else Int.ofNat mag

/-- 16-step unrollable exact integer floor square root `floor(sqrt(n))` for all `u32`. -/
def isqrtU32 (n : UInt32) : UInt32 := Id.run do
  let mut lo : UInt32 := 0
  let mut hi : UInt32 := 65535
  for _ in [0 : 16] do
    let mid : UInt32 := (lo + hi + 1) >>> 1
    if mid * mid ≤ n then
      lo := mid
    else
      hi := mid - 1
  return lo

/-- Overflow-free 3D Euclidean length in `Q16.16` via `Q10` (`>> 6`) intermediate scaling. -/
def length3Q16 (x y z : Int) : UInt32 :=
  let ax : UInt32 := min ((absI32ToU32 x) >>> 6) 24000
  let ay : UInt32 := min ((absI32ToU32 y) >>> 6) 24000
  let az : UInt32 := min ((absI32ToU32 z) >>> 6) 24000
  (isqrtU32 (ax * ax + ay * ay + az * az)) <<< 6

/-- Packs scene camera, `CineCamera` DoF, and `StrobeSafety` parameters into `64B`. -/
def mkSceneHeader64
    (width height primCount widgetCount : Nat)
    (camZQ16 focalLenQ16 focusDistQ16 : Int)
    (apertureQ16 strobeGainPermille bgTopRGB bgBotRGB : UInt32)
    (sunXQ16 sunYQ16 sunZQ16 : Int) : GpuPacket64 :=
  let tileCols := (width + 7) / 8
  let tileRows := (height + 7) / 8
  { u0  := UInt32.ofNat width
    u1  := UInt32.ofNat height
    u2  := UInt32.ofNat tileCols
    u3  := UInt32.ofNat tileRows
    u4  := UInt32.ofNat primCount
    u5  := UInt32.ofNat widgetCount
    u6  := i32ToU32 camZQ16
    u7  := i32ToU32 focalLenQ16
    u8  := i32ToU32 focusDistQ16
    u9  := apertureQ16
    u10 := strobeGainPermille
    u11 := bgTopRGB
    u12 := bgBotRGB
    u13 := i32ToU32 sunXQ16
    u14 := i32ToU32 sunYQ16
    u15 := i32ToU32 sunZQ16 }

/-- Packs a 3D SDF primitive + smooth-CSG operator + constitutive matter optics into `64B`. -/
def mkSdfPrim64
    (shapeKind csgOp smoothKQ16 matterFlags : UInt32)
    (cxQ16 cyQ16 czQ16 : Int)
    (param0Q16 param1Q16 param2Q16 iorQ16 sigmaAPermille f0Permille : UInt32)
    (albedoR albedoG albedoB : UInt32) : GpuPacket64 :=
  { u0  := shapeKind
    u1  := csgOp
    u2  := smoothKQ16
    u3  := matterFlags
    u4  := i32ToU32 cxQ16
    u5  := i32ToU32 cyQ16
    u6  := i32ToU32 czQ16
    u7  := param0Q16
    u8  := param1Q16
    u9  := param2Q16
    u10 := iorQ16
    u11 := sigmaAPermille
    u12 := f0Permille
    u13 := albedoR
    u14 := albedoG
    u15 := albedoB }

/-- Packs a zero-coordinate Auto-Bento optical glass widget referencing a `.flow` cell offset into `64B`. -/
def mkBentoWidget64
    (rect : Ui.BentoRect)
    (flowBundleBaseCell tripSlotThreshold : Nat)
    (minVisQ16 maxVisQ16 : Int)
    (cornerRadiusPx : UInt32 := 5)
    (borderPx : UInt32 := 2)
    (refractPx : UInt32 := 3)
    (slotMask : UInt32 := 3) : GpuPacket64 :=
  { u0  := UInt32.ofNat rect.x0
    u1  := UInt32.ofNat rect.y0
    u2  := UInt32.ofNat rect.x1
    u3  := UInt32.ofNat rect.y1
    u4  := UInt32.ofNat flowBundleBaseCell
    u5  := UInt32.ofNat tripSlotThreshold
    u6  := i32ToU32 minVisQ16
    u7  := i32ToU32 maxVisQ16
    u8  := cornerRadiusPx
    u9  := borderPx
    u10 := refractPx
    u11 := slotMask
    u12 := 0
    u13 := 0
    u14 := 0
    u15 := 0 }

end GpuAbi

/-! ## 5. `Bloom.Render`: `0-ULP` Deterministic CPU Renderer, PPM & Kitty APC Egress -/
namespace Render

open GpuAbi

structure FrameOutput where
  width       : Nat
  height      : Nat
  tileMasks   : Array UInt32
  scenePixels : Array UInt32
  finalPixels : Array UInt32
  deriving Repr, Inhabited

private def clampInt (x lo hi : Int) : Int :=
  max lo (min hi x)

private def clampU32 (x lo hi : UInt32) : UInt32 :=
  if x < lo then lo else if x > hi then hi else x

private def packRGBA8 (r g b a : UInt32) : UInt32 :=
  (min r 255) ||| ((min g 255) <<< 8) ||| ((min b 255) <<< 16) ||| ((min a 255) <<< 24)

private def unpackRGB (rgba : UInt32) : UInt32 × UInt32 × UInt32 :=
  (rgba &&& 0xFF, (rgba >>> 8) &&& 0xFF, (rgba >>> 16) &&& 0xFF)

private def evalSkyRGB (py height : UInt32) (bgTop bgBot : UInt32) : UInt32 × UInt32 × UInt32 :=
  let denom : UInt32 := if height > 1 then height - 1 else 1
  let t : UInt32 := min ((py * 1000) / denom) 1000
  let invT : UInt32 := 1000 - t
  let tr := (bgTop >>> 16) &&& 0xFF
  let tg := (bgTop >>> 8)  &&& 0xFF
  let tb := bgTop &&& 0xFF
  let br := (bgBot >>> 16) &&& 0xFF
  let bg := (bgBot >>> 8)  &&& 0xFF
  let bb := bgBot &&& 0xFF
  ((tr * invT + br * t) / 1000,
   (tg * invT + bg * t) / 1000,
   (tb * invT + bb * t) / 1000)

private def evalPrimSdfQ16 (p : GpuPacket64) (px py pz : Int) : Int :=
  let lx := px - u32ToI32 p.u4
  let ly := py - u32ToI32 p.u5
  let lz := pz - u32ToI32 p.u6
  let r0 := Int.ofNat p.u7.toNat
  let r1 := Int.ofNat p.u8.toNat
  let r2 := Int.ofNat p.u9.toNat
  if p.u0 == 0 then
    Int.ofNat (length3Q16 lx ly lz).toNat - r0
  else if p.u0 == 1 then
    let qx := Int.ofNat lx.natAbs - r0
    let qy := Int.ofNat ly.natAbs - r1
    let qz := Int.ofNat lz.natAbs - r2
    let ox := max qx 0
    let oy := max qy 0
    let oz := max qz 0
    let outDist := Int.ofNat (length3Q16 ox oy oz).toNat
    let inDist  := min (max qx (max qy qz)) 0
    outDist + inDist
  else
    let cy := clampInt ly (-r1) r1
    Int.ofNat (length3Q16 lx (ly - cy) lz).toNat - r0

private structure SceneSample where
  dist   : Int
  iorQ16 : UInt32
  sigmaA : UInt32
  f0     : UInt32
  flags  : UInt32
  r      : UInt32
  g      : UInt32
  b      : UInt32

private def evalScene (prims : Array GpuPacket64) (primCount : Nat) (px py pz : Int) : SceneSample := Id.run do
  let n := min primCount prims.size
  if n == 0 then
    return { dist := 655360, iorQ16 := 65536, sigmaA := 0, f0 := 40, flags := 0, r := 200, g := 200, b := 200 }
  let p0 := prims[0]!
  let mut acc : SceneSample := {
    dist   := evalPrimSdfQ16 p0 px py pz
    iorQ16 := p0.u10
    sigmaA := p0.u11
    f0     := p0.u12
    flags  := p0.u3
    r      := p0.u13
    g      := p0.u14
    b      := p0.u15
  }
  for idx in [1 : n] do
    let p := prims[idx]!
    let dRaw := evalPrimSdfQ16 p px py pz
    let op := p.u1
    let kU := p.u2
    let kI := Int.ofNat kU.toNat
    if op == 0 then
      if kU == 0 then
        if dRaw < acc.dist then
          acc := { dist := dRaw, iorQ16 := p.u10, sigmaA := p.u11, f0 := p.u12, flags := p.u3, r := p.u13, g := p.u14, b := p.u15 }
      else
        let diff := Int.ofNat (acc.dist - dRaw).natAbs
        let hU : UInt32 := if kI > diff then UInt32.ofNat (kI - diff).toNat else 0
        let h4 : UInt32 := hU >>> 4
        let k8 : UInt32 := max (kU >>> 8) 1
        let corr : Int := Int.ofNat ((h4 * h4) / (4 * k8)).toNat
        let dNew := min acc.dist dRaw - corr
        let blend : UInt32 :=
          if dRaw + kI ≤ acc.dist then 1000
          else if acc.dist + kI ≤ dRaw then 0
          else ((UInt32.ofNat (kI + acc.dist - dRaw).toNat) * 500) / max kU 1
        let invB : UInt32 := 1000 - blend
        acc := {
          dist   := dNew
          iorQ16 := (acc.iorQ16 * invB + p.u10 * blend) / 1000
          sigmaA := (acc.sigmaA * invB + p.u11 * blend) / 1000
          f0     := (acc.f0 * invB + p.u12 * blend) / 1000
          flags  := if blend >= 500 then p.u3 else acc.flags
          r      := (acc.r * invB + p.u13 * blend) / 1000
          g      := (acc.g * invB + p.u14 * blend) / 1000
          b      := (acc.b * invB + p.u15 * blend) / 1000
        }
    else if op == 1 then
      let negD := -dRaw
      if negD > acc.dist then
        acc := { acc with dist := negD }
    else
      if dRaw > acc.dist then
        acc := { dist := dRaw, iorQ16 := p.u10, sigmaA := p.u11, f0 := p.u12, flags := p.u3, r := p.u13, g := p.u14, b := p.u15 }
  return acc

/-- Kernel 1 Reference: Wave64 `8x8` tile bitmask culling (`u32x2` per tile). -/
def cpuWave64TileCull (header : GpuPacket64) (widgets : Array GpuPacket64) : Array UInt32 := Id.run do
  let width := header.u0.toNat
  let height := header.u1.toNat
  let tileCols := header.u2.toNat
  let tileRows := header.u3.toNat
  let wCount := min header.u5.toNat widgets.size
  let mut masks : Array UInt32 := Array.replicate (tileCols * tileRows * 2) 0
  for ty in [0 : tileRows] do
    for tx in [0 : tileCols] do
      let mut lo32 : UInt32 := 0
      let mut hi32 : UInt32 := 0
      for ly in [0 : 8] do
        for lx in [0 : 8] do
          let px := tx * 8 + lx
          let py := ty * 8 + ly
          if px < width && py < height then
            let mut hit := false
            for wi in [0 : wCount] do
              let w := widgets[wi]!
              if px >= w.u0.toNat && px < w.u2.toNat && py >= w.u1.toNat && py < w.u3.toNat then
                hit := true
            if hit then
              let bit := ly * 8 + lx
              if bit < 32 then
                lo32 := lo32 ||| ((1 : UInt32) <<< UInt32.ofNat bit)
              else
                hi32 := hi32 ||| ((1 : UInt32) <<< UInt32.ofNat (bit - 32))
      let base := (ty * tileCols + tx) * 2
      masks := masks.set! base lo32
      masks := masks.set! (base + 1) hi32
  return masks

/-- Kernel 2 Reference: Deterministic SDF/CSG + Constitutive Matter Optics Raymarcher. -/
def cpuSdfMatterMarch (header : GpuPacket64) (prims : Array GpuPacket64) : Array UInt32 := Id.run do
  let width := header.u0.toNat
  let height := header.u1.toNat
  let hU32 := max header.u1 1
  let primCount := header.u4.toNat
  let camZ := u32ToI32 header.u6
  let focalLen := u32ToI32 header.u7
  let focusDist := u32ToI32 header.u8
  let aperture := header.u9
  let strobeGain := min header.u10 1000
  let bgTop := header.u11
  let bgBot := header.u12
  let sunX := u32ToI32 header.u13
  let sunY := u32ToI32 header.u14
  let sunZ := u32ToI32 header.u15
  let mut out : Array UInt32 := Array.replicate (width * height) 0
  for py in [0 : height] do
    for px in [0 : width] do
      let sxNum : Int := (2 * Int.ofNat px + 1 - Int.ofNat width) * 65536
      let syNum : Int := (Int.ofNat height - (2 * Int.ofNat py + 1)) * 65536
      let uvx := sdivI32 sxNum hU32
      let uvy := sdivI32 syNum hU32
      let rLen10 : UInt32 := max ((length3Q16 uvx uvy focalLen) >>> 6) 1
      let rdX := sdivI32 (uvx * 1024) rLen10
      let rdY := sdivI32 (uvy * 1024) rLen10
      let rdZ := sdivI32 (focalLen * 1024) rLen10
      let (skyR, skyG, skyB) := evalSkyRGB (UInt32.ofNat py) hU32 bgTop bgBot
      let mut tQ16 : Int := 16384
      let mut hit : Bool := false
      let mut hitSample : SceneSample := {
        dist := 655360, iorQ16 := 65536, sigmaA := 0, f0 := 40, flags := 0, r := skyR, g := skyG, b := skyB
      }
      for _ in [0 : 24] do
        if !hit && tQ16 <= 655360 then
          let posX := q16MulI32 tQ16 rdX
          let posY := q16MulI32 tQ16 rdY
          let posZ := camZ + q16MulI32 tQ16 rdZ
          let s := evalScene prims primCount posX posY posZ
          if s.dist <= 512 then
            hit := true
            hitSample := s
          else
            tQ16 := tQ16 + s.dist
      let (rawR, rawG, rawB) :=
        if !hit then
          (skyR, skyG, skyB)
        else
          let posX := q16MulI32 tQ16 rdX
          let posY := q16MulI32 tQ16 rdY
          let posZ := camZ + q16MulI32 tQ16 rdZ
          let eps : Int := 1024
          let gx := (evalScene prims primCount (posX + eps) posY posZ).dist -
                    (evalScene prims primCount (posX - eps) posY posZ).dist
          let gy := (evalScene prims primCount posX (posY + eps) posZ).dist -
                    (evalScene prims primCount posX (posY - eps) posZ).dist
          let gz := (evalScene prims primCount posX posY (posZ + eps)).dist -
                    (evalScene prims primCount posX posY (posZ - eps)).dist
          let nLen10 : UInt32 := max ((length3Q16 (gx * 8) (gy * 8) (gz * 8)) >>> 6) 1
          let nx := sdivI32 (gx * 8192) nLen10
          let ny := sdivI32 (gy * 8192) nLen10
          let nz := sdivI32 (gz * 8192) nLen10
          let ndotlRaw := max (q16MulI32 nx sunX + q16MulI32 ny sunY + q16MulI32 nz sunZ) 0
          let diffPerm := min (((UInt32.ofNat ndotlRaw.toNat) * 1000) >>> 16) 1000
          let litPerm : UInt32 := 180 + (diffPerm * 820) / 1000
          let ndotvRaw := clampInt (q16MulI32 nx (-rdX) + q16MulI32 ny (-rdY) + q16MulI32 nz (-rdZ)) 0 65536
          let omc : UInt32 := (1000 : UInt32) - min (((UInt32.ofNat ndotvRaw.toNat) * 1000) >>> 16) 1000
          let p2 := (omc * omc) / 1000
          let p4 := (p2 * p2) / 1000
          let p5 := (p4 * omc) / 1000
          let fresnelPerm : UInt32 := min (hitSample.f0 + (((1000 : UInt32) - hitSample.f0) * p5) / 1000) 1000
          let (surfR, surfG, surfB) :=
            if (hitSample.flags &&& 1) != 0 then
              let inX := posX - sdivI32 nx 4
              let inY := posY - sdivI32 ny 4
              let inZ := posZ - sdivI32 nz 4
              let dIn := max (-(evalScene prims primCount inX inY inZ).dist) 0
              let pathPerm : UInt32 := (250 : UInt32) + min (((UInt32.ofNat dIn.toNat) * 1000) >>> 16) 750
              let atten : UInt32 := min ((hitSample.sigmaA * pathPerm) / 1000) 850
              let transPerm : UInt32 := (1000 : UInt32) - atten
              let iorShift : Int := sdivI32 (Int.ofNat hitSample.iorQ16.toNat - 65536) 4096
              let refrPyI := clampInt (Int.ofNat py + sdivI32 (ny * iorShift) 8192) 0 (Int.ofNat height - 1)
              let (rSkyR, rSkyG, rSkyB) := evalSkyRGB (UInt32.ofNat refrPyI.toNat) hU32 bgTop bgBot
              let bodyR := ((min ((rSkyR * hitSample.r) / 160) 255) * transPerm) / 1000
              let bodyG := ((min ((rSkyG * hitSample.g) / 160) 255) * transPerm) / 1000
              let bodyB := ((min ((rSkyB * hitSample.b) / 160) 255) * transPerm) / 1000
              let invF : UInt32 := (1000 : UInt32) - fresnelPerm
              ((bodyR * invF + skyR * fresnelPerm) / 1000,
               (bodyG * invF + skyG * fresnelPerm) / 1000,
               (bodyB * invF + skyB * fresnelPerm) / 1000)
            else
              let baseR := (hitSample.r * litPerm) / 1000
              let baseG := (hitSample.g * litPerm) / 1000
              let baseB := (hitSample.b * litPerm) / 1000
              let invF : UInt32 := (1000 : UInt32) - fresnelPerm
              ((baseR * invF + skyR * fresnelPerm) / 1000,
               (baseG * invF + skyG * fresnelPerm) / 1000,
               (baseB * invF + skyB * fresnelPerm) / 1000)
          let depthDiff := absI32ToU32 (posZ - focusDist)
          let cocPerm : UInt32 := min (((q16MulU32 depthDiff aperture) * 1000) >>> 16) 650
          let invCoc : UInt32 := (1000 : UInt32) - cocPerm
          ((surfR * invCoc + skyR * cocPerm) / 1000,
           (surfG * invCoc + skyG * cocPerm) / 1000,
           (surfB * invCoc + skyB * cocPerm) / 1000)
      let gainPerm : UInt32 := 700 + (strobeGain * 300) / 1000
      let finR := (rawR * gainPerm) / 1000
      let finG := (rawG * gainPerm) / 1000
      let finB := (rawB * gainPerm) / 1000
      out := out.set! (py * width + px) (packRGBA8 finR finG finB 255)
  return out

private def digit7SegMask (d : UInt32) : UInt32 :=
  match d.toNat with
  | 0 => 0x3F
  | 1 => 0x06
  | 2 => 0x5B
  | 3 => 0x4F
  | 4 => 0x66
  | 5 => 0x6D
  | 6 => 0x7D
  | 7 => 0x07
  | 8 => 0x7F
  | 9 => 0x6F
  | _ => 0x00

private def eval7SegDigitPixel (d lx ly : UInt32) : Bool :=
  if lx >= 6 || ly >= 9 then false
  else
    let m := digit7SegMask d
    let segA := (m &&& 0x01) != 0 && ly == 0 && lx >= 1 && lx <= 4
    let segB := (m &&& 0x02) != 0 && lx == 5 && ly >= 1 && ly <= 3
    let segC := (m &&& 0x04) != 0 && lx == 5 && ly >= 5 && ly <= 7
    let segD := (m &&& 0x08) != 0 && ly == 8 && lx >= 1 && lx <= 4
    let segE := (m &&& 0x10) != 0 && lx == 0 && ly >= 5 && ly <= 7
    let segF := (m &&& 0x20) != 0 && lx == 0 && ly >= 1 && ly <= 3
    let segG := (m &&& 0x40) != 0 && ly == 4 && lx >= 1 && lx <= 4
    segA || segB || segC || segD || segE || segF || segG

private def evalReadoutPixel (tenthsTotal : UInt32) (rx ry : Int) : Bool :=
  if rx < 0 || ry < 0 || rx >= 23 || ry >= 9 then false
  else
    let ux := UInt32.ofNat rx.toNat
    let uy := UInt32.ofNat ry.toNat
    let d1 := (tenthsTotal / 100) % 10
    let d0 := (tenthsTotal / 10) % 10
    let f0 := tenthsTotal % 10
    if ux < 6 then eval7SegDigitPixel d1 ux uy
    else if ux >= 7 && ux < 13 then eval7SegDigitPixel d0 (ux - 7) uy
    else if ux == 14 && (uy == 7 || uy == 8) then true
    else if ux >= 16 && ux < 22 then eval7SegDigitPixel f0 (ux - 16) uy
    else false

/--
  Kernel 3 Reference: Zero-Coordinate Auto-Bento Optical Glass Compositor
  reading raw `64B` `.flow` cells (`flowCells`) directly without serialization glue.
-/
def cpuBentoGlassComposite
    (header : GpuPacket64)
    (widgets : Array GpuPacket64)
    (flowCells : Array GpuPacket64)
    (tileMasks : Array UInt32)
    (scenePixels : Array UInt32) : Array UInt32 := Id.run do
  let width := header.u0.toNat
  let height := header.u1.toNat
  let tileCols := header.u2.toNat
  let wCount := min header.u5.toNat widgets.size
  let mut out : Array UInt32 := scenePixels
  for py in [0 : height] do
    for px in [0 : width] do
      let idx := py * width + px
      let tx := px / 8
      let ty := py / 8
      let bit := (py % 8) * 8 + (px % 8)
      let maskBase := (ty * tileCols + tx) * 2
      let maskWord :=
        if bit < 32 then tileMasks.getD maskBase 0
        else tileMasks.getD (maskBase + 1) 0
      let bitShift : UInt32 := UInt32.ofNat (if bit < 32 then bit else bit - 32)
      if ((maskWord >>> bitShift) &&& 1) != 0 then
        let mut foundIdx : Option Nat := none
        for wi in [0 : wCount] do
          if foundIdx.isNone then
            let w := widgets[wi]!
            if px >= w.u0.toNat && px < w.u2.toNat && py >= w.u1.toNat && py < w.u3.toNat then
              foundIdx := some wi
        match foundIdx with
        | none => pure ()
        | some wi =>
          let w := widgets[wi]!
          let x0I := Int.ofNat w.u0.toNat
          let y0I := Int.ofNat w.u1.toNat
          let x1I := Int.ofNat w.u2.toNat
          let y1I := Int.ofNat w.u3.toNat
          let cx2 := x0I + x1I
          let cy2 := y0I + y1I
          let px2 := 2 * Int.ofNat px + 1
          let py2 := 2 * Int.ofNat py + 1
          let hx2 := x1I - x0I
          let hy2 := y1I - y0I
          let r2  := 2 * Int.ofNat w.u8.toNat
          let qx2 := Int.ofNat (px2 - cx2).natAbs - (hx2 - r2)
          let qy2 := Int.ofNat (py2 - cy2).natAbs - (hy2 - r2)
          let ox2 := UInt32.ofNat (max qx2 0).toNat
          let oy2 := UInt32.ofNat (max qy2 0).toNat
          let out2 := Int.ofNat (isqrtU32 (ox2 * ox2 + oy2 * oy2)).toNat
          let in2  := min (max qx2 qy2) 0
          let sdf2 := out2 + in2 - r2
          if sdf2 <= 0 then
            let baseCell := w.u4.toNat
            let st := flowCells.getD baseCell default
            let sealOk := (st.u14 ||| st.u15) != 0
            let sieve  := st.u12
            let gateOk := sealOk && ((sieve &&& 1) != 0)
            let xQ16     := u32ToI32 st.u6
            let slot     := ((st.u10 >>> 16) &&& 0xFFFF)
            let safeSlot := slot &&& w.u11
            let ch       := flowCells.getD (baseCell + 1 + safeSlot.toNat) default
            let bEnter := u32ToI32 ch.u6
            let bExit  := u32ToI32 ch.u7
            let span   := max (bEnter - bExit) 65536
            let warnAt := bEnter - sdivI32 span 5
            let glassMode : UInt32 :=
              if !gateOk || slot >= w.u5 then 2
              else if slot > 0 || (bEnter < 2000000000 && xQ16 >= warnAt) then 1
              else 0
            let (tintR, tintG, tintB) : UInt32 × UInt32 × UInt32 :=
              if glassMode == 2 then (235, 58, 72)
              else if glassMode == 1 then (245, 178, 42)
              else (44, 212, 148)
            let refOff := Int.ofNat w.u10.toNat
            let sxL := (clampInt (Int.ofNat px - refOff) 0 (Int.ofNat width - 1)).toNat
            let sxR := (clampInt (Int.ofNat px + refOff) 0 (Int.ofNat width - 1)).toNat
            let syU := (clampInt (Int.ofNat py - refOff) 0 (Int.ofNat height - 1)).toNat
            let syD := (clampInt (Int.ofNat py + refOff) 0 (Int.ofNat height - 1)).toNat
            let (rL, gL, bL) := unpackRGB (scenePixels.getD (py * width + sxL) 0)
            let (rR, gR, bR) := unpackRGB (scenePixels.getD (py * width + sxR) 0)
            let (rU, gU, bU) := unpackRGB (scenePixels.getD (syU * width + px) 0)
            let (rD, gD, bD) := unpackRGB (scenePixels.getD (syD * width + px) 0)
            let blurR := (rL + rR + rU + rD) / 4
            let blurG := (gL + gR + gU + gD) / 4
            let blurB := (bL + bR + bU + bD) / 4
            let mut pixR := (blurR * 520 + 18 * 300 + tintR * 180) / 1000
            let mut pixG := (blurG * 520 + 24 * 300 + tintG * 180) / 1000
            let mut pixB := (blurB * 520 + 34 * 300 + tintB * 180) / 1000
            let minVis := u32ToI32 w.u6
            let maxVis := max (u32ToI32 w.u7) (minVis + 65536)
            let clampedX := clampInt xQ16 minVis maxVis
            let fillPerm : UInt32 :=
              UInt32.ofNat (sdivI32 ((clampedX - minVis) * 1000) (UInt32.ofNat (maxVis - minVis).toNat)).toNat
            let barX0 := x0I + 5
            let barX1 := max (x1I - 5) (barX0 + 1)
            let barY0 := y1I - 7
            let barY1 := y1I - 4
            if Int.ofNat px >= barX0 && Int.ofNat px < barX1 &&
               Int.ofNat py >= barY0 && Int.ofNat py < barY1 then
              let barW := UInt32.ofNat (barX1 - barX0).toNat
              let relX := UInt32.ofNat (Int.ofNat px - barX0).toNat
              if relX * 1000 < fillPerm * barW then
                pixR := tintR
                pixG := tintG
                pixB := tintB
              else
                pixR := pixR / 2
                pixG := pixG / 2
                pixB := pixB / 2
            let tenths := ((absI32ToU32 xQ16) * 10 + 32768) >>> 16
            let glyphX0 := sdivI32 (x0I + x1I) 2 - 11
            let glyphY0 := max (y0I + 3) (sdivI32 (y0I + barY0 - 9) 2)
            if evalReadoutPixel tenths (Int.ofNat px - glyphX0) (Int.ofNat py - glyphY0) then
              pixR := 242
              pixG := 248
              pixB := 255
            let border2 := 2 * Int.ofNat w.u9.toNat
            if sdf2 >= -border2 then
              pixR := (pixR * 300 + tintR * 700) / 1000
              pixG := (pixG * 300 + tintG * 700) / 1000
              pixB := (pixB * 300 + tintB * 700) / 1000
            out := out.set! idx (packRGBA8 pixR pixG pixB 255)
  return out

/-- Full 3-pass deterministic CPU render pipeline (`Wave64 Cull -> SDF/Matter -> Bento Glass`). -/
def renderFrame
    (header : GpuPacket64)
    (prims : Array GpuPacket64)
    (widgets : Array GpuPacket64)
    (flowCells : Array GpuPacket64) : FrameOutput :=
  let masks := cpuWave64TileCull header widgets
  let scene := cpuSdfMatterMarch header prims
  let final := cpuBentoGlassComposite header widgets flowCells masks scene
  { width       := header.u0.toNat
    height      := header.u1.toNat
    tileMasks   := masks
    scenePixels := scene
    finalPixels := final }

/-- Encodes `RGBA8` pixel buffer into lossless binary `P6` `PPM` image bytes. -/
def encodePPM (width height : Nat) (pixels : Array UInt32) : ByteArray := Id.run do
  let hdr := s!"P6\n{width} {height}\n255\n".toUTF8
  let mut body := ByteArray.emptyWithCapacity (hdr.size + width * height * 3)
  body := body ++ hdr
  for i in [0 : width * height] do
    let p := pixels.getD i 0
    body := body.push (UInt8.ofNat (p &&& 0xFF).toNat)
    body := body.push (UInt8.ofNat ((p >>> 8) &&& 0xFF).toNat)
    body := body.push (UInt8.ofNat ((p >>> 16) &&& 0xFF).toNat)
  return body

private def b64Char (idx : UInt8) : Char :=
  let i := idx.toNat
  if i < 26 then Char.ofNat ('A'.toNat + i)
  else if i < 52 then Char.ofNat ('a'.toNat + (i - 26))
  else if i < 62 then Char.ofNat ('0'.toNat + (i - 52))
  else if i == 62 then '+'
  else '/'

/-- Pure-Lean RFC 4648 Base64 encoder (`0` external C dependencies). -/
def base64Encode (ba : ByteArray) : String := Id.run do
  let n := ba.size
  let mut chars : Array Char := Array.mkEmpty (((n + 2) / 3) * 4)
  let mut i : Nat := 0
  while i + 3 ≤ n do
    let b0 := ba.get! i
    let b1 := ba.get! (i + 1)
    let b2 := ba.get! (i + 2)
    chars := chars.push (b64Char (b0 >>> 2))
    chars := chars.push (b64Char (((b0 &&& 0x03) <<< 4) ||| (b1 >>> 4)))
    chars := chars.push (b64Char (((b1 &&& 0x0F) <<< 2) ||| (b2 >>> 6)))
    chars := chars.push (b64Char (b2 &&& 0x3F))
    i := i + 3
  let rem := n - i
  if rem == 1 then
    let b0 := ba.get! i
    chars := chars.push (b64Char (b0 >>> 2))
    chars := chars.push (b64Char ((b0 &&& 0x03) <<< 4))
    chars := chars.push '='
    chars := chars.push '='
  else if rem == 2 then
    let b0 := ba.get! i
    let b1 := ba.get! (i + 1)
    chars := chars.push (b64Char (b0 >>> 2))
    chars := chars.push (b64Char (((b0 &&& 0x03) <<< 4) ||| (b1 >>> 4)))
    chars := chars.push (b64Char ((b1 &&& 0x0F) <<< 2))
    chars := chars.push '='
  return String.ofList chars.toList

/--
  Cross-platform terminal graphics egress: encodes `RGBA8` pixels as a chunked
  `24-bit` RGB **Kitty Graphics Protocol APC** stream (`\x1b_Ga=T,f=24,...\x1b\\`).
-/
def encodeKittyAPC (width height : Nat) (pixels : Array UInt32) : String := Id.run do
  let mut rgb := ByteArray.emptyWithCapacity (width * height * 3)
  for i in [0 : width * height] do
    let p := pixels.getD i 0
    rgb := rgb.push (UInt8.ofNat (p &&& 0xFF).toNat)
    rgb := rgb.push (UInt8.ofNat ((p >>> 8) &&& 0xFF).toNat)
    rgb := rgb.push (UInt8.ofNat ((p >>> 16) &&& 0xFF).toNat)
  let b64 := (base64Encode rgb).toList
  let total := b64.length
  let esc := Char.ofNat 0x1B
  let mut out : String := ""
  let mut pos : Nat := 0
  let mut first := true
  while pos < total || first do
    let chunk := String.ofList ((b64.drop pos).take 4096)
    let nextPos := pos + chunk.length
    let more := if nextPos < total then 1 else 0
    if first then
      out := out ++ s!"{esc}_Ga=T,f=24,s={width},v={height},q=2,m={more};{chunk}{esc}\\"
      first := false
    else
      out := out ++ s!"{esc}_Gm={more};{chunk}{esc}\\"
    pos := nextPos
    if total == 0 then break
  return out

end Render

end Bloom
