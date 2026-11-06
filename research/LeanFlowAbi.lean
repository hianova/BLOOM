/-
  ============================================================================
  BLOOM: Direct .lean <-> FLOW 64-Byte ABI Execution Kernel Formal Proofs
  ============================================================================
  Formalizes the universal 64-byte ABI operator descriptors (`FlowLeanClause64`)
  that map `.lean` specifications directly onto `FLOW` runtime primitives:
    1. Upper Dual-Threshold Hysteresis (`FLOW_LEAN_OP_HYSTERESIS_HI` + Kan step)
    2. Lower Dual-Threshold Hysteresis (`FLOW_LEAN_OP_HYSTERESIS_LO` + Kan step)
    3. Exact Interval Clamp (`FLOW_LEAN_OP_CLAMP_INTERVAL`)
    4. 2-Jet Exponential Damping Step (`FLOW_LEAN_OP_JET2_EMA_DAMP`)
    5. Exact Basis-Point Conservation Split (`FLOW_LEAN_OP_CONSERVATION_SPLIT`)
    6. Monotone Floor-Bounded Subtraction (`FLOW_LEAN_OP_FLOOR_SUB`)
    7. Multi-Axis SLA & Ternary Mask Gate (`FLOW_LEAN_OP_QUOTA_SLA_GATE`)
  Core Lean 4 only (zero Mathlib, zero sorryAx, zero orphan_defs).
  ============================================================================
-/

namespace Bloom.LeanFlowAbi

def q16One : Nat := 65536
def bpsScale : Nat := 10000

/-- Universal 64-Byte ABI Clause Descriptor (`FlowLeanClause64` in C17). -/
structure FlowLeanClause64 where
  clauseId   : Nat
  opKind     : Nat
  srcSlot    : Nat
  dstSlot    : Nat
  paramA     : Nat
  paramB     : Nat
  reqMask    : Nat
  forbidMask : Nat
  deriving Repr, DecidableEq

/-- 1. Upper Dual-Threshold Hysteresis (`enter >= bEnter`, `exit <= bExit`, `bExit <= bEnter`). -/
def evalAbiHysteresisHi (prevLatched : Bool) (val bEnter bExit : Nat) : Bool :=
  if val >= bEnter then
    true
  else if val <= bExit then
    false
  else
    prevLatched

theorem abi_hysteresis_hi_deadband_invariant (prev : Bool) (val bEnter bExit : Nat)
    (hAboveExit : val > bExit)
    (hBelowEnter : val < bEnter) :
    evalAbiHysteresisHi prev val bEnter bExit = prev := by
  unfold evalAbiHysteresisHi
  have h1 : ¬(val >= bEnter) := by omega
  have h2 : ¬(val <= bExit) := by omega
  simp [h1, h2]

theorem abi_hysteresis_hi_enter_latches (prev : Bool) (val bEnter bExit : Nat)
    (hEnter : val >= bEnter) :
    evalAbiHysteresisHi prev val bEnter bExit = true := by
  unfold evalAbiHysteresisHi
  simp [hEnter]

/-- 2. Lower Dual-Threshold Hysteresis (`enter <= bEnter`, `exit >= bExit`, `bEnter <= bExit`). -/
def evalAbiHysteresisLo (prevLatched : Bool) (val bEnter bExit : Nat) : Bool :=
  if val <= bEnter then
    true
  else if val >= bExit then
    false
  else
    prevLatched

theorem abi_hysteresis_lo_deadband_invariant (prev : Bool) (val bEnter bExit : Nat)
    (hAboveEnter : val > bEnter)
    (hBelowExit : val < bExit) :
    evalAbiHysteresisLo prev val bEnter bExit = prev := by
  unfold evalAbiHysteresisLo
  have h1 : ¬(val <= bEnter) := by omega
  have h2 : ¬(val >= bExit) := by omega
  simp [h1, h2]

theorem abi_hysteresis_lo_enter_latches (prev : Bool) (val bEnter bExit : Nat)
    (hEnter : val <= bEnter) :
    evalAbiHysteresisLo prev val bEnter bExit = true := by
  unfold evalAbiHysteresisLo
  simp [hEnter]

/-- 3. Exact Interval Clamp (`lo <= hi`). -/
def evalAbiClampInterval (val lo hi : Nat) : Nat :=
  if val < lo then
    lo
  else if val > hi then
    hi
  else
    val

theorem abi_clamp_interval_bounded (val lo hi : Nat) (hOrder : lo <= hi) :
    lo <= evalAbiClampInterval val lo hi ∧ evalAbiClampInterval val lo hi <= hi := by
  unfold evalAbiClampInterval
  split
  · omega
  · split <;> omega

theorem abi_clamp_interval_idempotent (val lo hi : Nat) (hOrder : lo <= hi) :
    evalAbiClampInterval (evalAbiClampInterval val lo hi) lo hi =
    evalAbiClampInterval val lo hi := by
  have h := abi_clamp_interval_bounded val lo hi hOrder
  generalize evalAbiClampInterval val lo hi = x at *
  unfold evalAbiClampInterval
  have hNotLt : ¬(x < lo) := by omega
  have hNotGt : ¬(x > hi) := by omega
  simp [hNotLt, hNotGt]

/-- 4. Basis-Point Conservation Split (`bps <= 10000`). -/
def evalAbiSplitPrimary (total bps : Nat) : Nat :=
  (total * bps) / bpsScale

def evalAbiSplitRemainder (total bps : Nat) : Nat :=
  total - evalAbiSplitPrimary total bps

theorem abi_conservation_split_exact (total bps : Nat) (hBps : bps <= bpsScale) :
    evalAbiSplitPrimary total bps + evalAbiSplitRemainder total bps = total := by
  unfold evalAbiSplitRemainder evalAbiSplitPrimary bpsScale at *
  have hProd : total * bps <= total * 10000 := Nat.mul_le_mul_left total hBps
  have hDiv : (total * bps) / 10000 <= (total * 10000) / 10000 := Nat.div_le_div_right hProd
  omega

/-- 5. Monotone Floor-Bounded Subtraction (`max(floorVal, curVal - deltaVal)`). -/
def evalAbiFloorSub (curVal deltaVal floorVal : Nat) : Nat :=
  if curVal > floorVal + deltaVal then
    curVal - deltaVal
  else
    floorVal

theorem abi_floor_sub_never_underflows (curVal deltaVal floorVal : Nat) :
    evalAbiFloorSub curVal deltaVal floorVal >= floorVal := by
  unfold evalAbiFloorSub
  split <;> omega

theorem abi_floor_sub_monotone_cooling (curVal deltaVal floorVal : Nat)
    (hAbove : curVal >= floorVal) :
    evalAbiFloorSub curVal deltaVal floorVal <= curVal := by
  unfold evalAbiFloorSub
  split <;> omega

/-- 6. 2-Jet Exponential Moving Average Unity Identity (`alpha = q16One => target`). -/
def evalAbiJet2Ema (curVal targetVal alphaQ16 : Nat) : Nat :=
  (curVal * (q16One - alphaQ16) + targetVal * alphaQ16) / q16One

theorem abi_jet2_ema_unity_exact (curVal targetVal : Nat) :
    evalAbiJet2Ema curVal targetVal q16One = targetVal := by
  unfold evalAbiJet2Ema q16One
  omega

/-- 7. Multi-Axis SLA Quota & Clause Execution Gate. -/
def evalAbiQuotaGate (c : FlowLeanClause64) (costA costB : Nat) : Bool :=
  (costA <= c.paramA) && (costB <= c.paramB)

theorem abi_quota_gate_soundness (c : FlowLeanClause64) (costA costB : Nat)
    (hPass : evalAbiQuotaGate c costA costB = true) :
    costA <= c.paramA ∧ costB <= c.paramB := by
  unfold evalAbiQuotaGate at hPass
  simp only [Bool.and_eq_true, decide_eq_true_eq] at hPass
  exact hPass

end Bloom.LeanFlowAbi
