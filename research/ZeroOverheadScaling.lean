/-! Phase 0: device-independent traffic model for "zero-overhead scaling".
    Units: bytes of DRAM traffic per frame. Resolution scale is a percent (s100)
    applied per axis, so pixel count scales by s100^2 / 10000. -/

/-- A pass touches one buffer at a resolution scale (percent per axis). -/
structure Access where
  bytesPerPx : Nat      -- storage format size
  scale100   : Nat      -- 100 = native output resolution
  isRead     : Bool
  inTile     : Bool     -- lives in tile memory: costs no DRAM traffic

structure Pass where
  isResample : Bool     -- pass exists only to change resolution
  accesses   : List Access

/-- DRAM bytes for one access given native pixel count `P`. -/
def Access.traffic (P : Nat) (a : Access) : Nat :=
  if a.inTile then 0 else P * a.scale100 * a.scale100 / 10000 * a.bytesPerPx

def Pass.traffic (P : Nat) (p : Pass) : Nat :=
  (p.accesses.map (·.traffic P)).foldl (· + ·) 0

def Pipeline := List Pass

def Pipeline.traffic (P : Nat) (pl : Pipeline) : Nat :=
  (pl.map (·.traffic P)).foldl (· + ·) 0

/-- Resample overhead: traffic of passes that exist only to rescale. -/
def Pipeline.scalingOverhead (P : Nat) (pl : Pipeline) : Nat :=
  ((pl.filter (·.isResample)).map (·.traffic P)).foldl (· + ·) 0

def P4K : Nat := 3840 * 2160

/-- Classic: render at scale s into RGBA16F + depth, then upscale pass. -/
def classic (s : Nat) : Pipeline :=
  [ ⟨false, [⟨8, s, false, false⟩, ⟨4, s, false, false⟩]⟩,   -- color w, depth w
    ⟨true,  [⟨8, s, true,  false⟩, ⟨4, 100, false, false⟩]⟩ ] -- read small, write native RGBA8

/-- Zero-overhead: one pass, intermediates in tile memory, scale only picks
    the raster/sample density; only the native output is written. -/
def zeroOverhead (_s : Nat) : Pipeline :=
  [ ⟨false, [⟨8, 100, false, true⟩, ⟨4, 100, false, true⟩, ⟨4, 100, false, false⟩]⟩ ]

theorem zero_has_no_resample (s : Nat) : (zeroOverhead s).scalingOverhead P4K = 0 := by
  simp [zeroOverhead, Pipeline.scalingOverhead, Pass.traffic]

/-- Traffic is independent of the scale parameter. -/
theorem zero_scale_independent (s t : Nat) :
    (zeroOverhead s).traffic P4K = (zeroOverhead t).traffic P4K := rfl

-- Concrete numbers at 4K.
#eval (classic 100).traffic P4K        -- native, classic
#eval (classic 67).traffic P4K         -- ~67% scale, classic (incl. upscale)
#eval (classic 67).scalingOverhead P4K
#eval (zeroOverhead 67).traffic P4K
#eval (zeroOverhead 67).scalingOverhead P4K

/-- At every scale, classic costs strictly more than zero-overhead (checked 25..100). -/
theorem classic_dominated : ∀ s ∈ List.range' 25 76,
    (zeroOverhead s).traffic P4K < (classic s).traffic P4K := by decide
