/-! Formal verification of Sovereign Quality Gates from todo.md:
    1. Deterministic Frame-Time Budget Gate (SDF Ops + Script Instruction Quota ≤ Budget)
    2. Dual-Currency Orthogonality (`funds` vs Sovereign Reserve `sr` across arbitrary action lists)
    3. Two-Player Lineage Tether Single Genesis Plot Claim (`claimCount ≤ 1`)
    4. Master-Seed Oklab Contrast (`ΔL ≥ ΔL_min`) & Negative Space ("計白當黑" 60%..75%) Bounds
    Core Lean 4 only (no Mathlib, zero unproven axioms, zero orphan_defs). -/

structure SovereignGateState64 where
  activeSdfOps      : Nat
  activeScriptSteps : Nat
  fundsG            : Nat
  sovereignSr       : Nat
  orderId           : Nat
  tetherClaimed     : Nat
  oklabDeltaLQ16    : Nat
  negativeSpaceQ16  : Nat

/-- 1. Frame-Time Budget Gate (SDF Ops + Sandbox Script Steps Quota) -/
structure EngineQuotaConfig where
  basePhysicsUs  : Nat
  baseRenderUs   : Nat
  sdfCostPerOp   : Nat
  scriptCostStep : Nat
  maxSdfOps      : Nat
  maxScriptSteps : Nat

def actualFrameTimeUs (cfg : EngineQuotaConfig) (activeSdfOps activeScriptSteps : Nat) : Nat :=
  cfg.basePhysicsUs + cfg.baseRenderUs +
  (cfg.sdfCostPerOp * activeSdfOps) +
  (cfg.scriptCostStep * activeScriptSteps)

theorem frame_budget_guaranteed
    (cfg : EngineQuotaConfig)
    (ops steps : Nat)
    (h_ops : ops ≤ cfg.maxSdfOps)
    (h_steps : steps ≤ cfg.maxScriptSteps)
    (h_budget : actualFrameTimeUs cfg cfg.maxSdfOps cfg.maxScriptSteps ≤ 16666) :
    actualFrameTimeUs cfg ops steps ≤ 16666 := by
  unfold actualFrameTimeUs at *
  have h1 : cfg.sdfCostPerOp * ops ≤ cfg.sdfCostPerOp * cfg.maxSdfOps :=
    Nat.mul_le_mul_left cfg.sdfCostPerOp h_ops
  have h2 : cfg.scriptCostStep * steps ≤ cfg.scriptCostStep * cfg.maxScriptSteps :=
    Nat.mul_le_mul_left cfg.scriptCostStep h_steps
  omega

theorem frame_budget_120fps_guaranteed
    (cfg : EngineQuotaConfig)
    (ops steps : Nat)
    (h_ops : ops ≤ cfg.maxSdfOps)
    (h_steps : steps ≤ cfg.maxScriptSteps)
    (h_budget : actualFrameTimeUs cfg cfg.maxSdfOps cfg.maxScriptSteps ≤ 8333) :
    actualFrameTimeUs cfg ops steps ≤ 8333 := by
  unfold actualFrameTimeUs at *
  have h1 : cfg.sdfCostPerOp * ops ≤ cfg.sdfCostPerOp * cfg.maxSdfOps :=
    Nat.mul_le_mul_left cfg.sdfCostPerOp h_ops
  have h2 : cfg.scriptCostStep * steps ≤ cfg.scriptCostStep * cfg.maxScriptSteps :=
    Nat.mul_le_mul_left cfg.scriptCostStep h_steps
  omega

/-- 2. Dual-Currency Orthogonality (`fundsG` vs `sovereignSr`) -/
inductive GameplayAction where
  | mineOre       (gain : Nat)
  | payHarberger  (tax  : Nat)
  | fundWonder    (cost : Nat)
  | squeezeTarget (pnl  : Nat)
  | sculptPlanetG (fee  : Nat)

def execGameplayTransition (w : SovereignGateState64) (act : GameplayAction) : SovereignGateState64 :=
  match act with
  | .mineOre gain      => { w with fundsG := w.fundsG + gain }
  | .payHarberger tax  => { w with fundsG := w.fundsG - tax }
  | .fundWonder cost   => { w with fundsG := w.fundsG - cost }
  | .squeezeTarget pnl => { w with fundsG := w.fundsG + pnl }
  | .sculptPlanetG fee => { w with fundsG := w.fundsG - fee }

theorem sr_strictly_orthogonal_step (w : SovereignGateState64) (act : GameplayAction) :
    (execGameplayTransition w act).sovereignSr = w.sovereignSr := by
  cases act <;> rfl

theorem sr_strictly_orthogonal_to_gameplay
    (w : SovereignGateState64) (scriptActions : List GameplayAction) :
    (scriptActions.foldl execGameplayTransition w).sovereignSr = w.sovereignSr := by
  induction scriptActions generalizing w with
  | nil => rfl
  | cons act rest ih =>
    simp only [List.foldl_cons]
    rw [ih (execGameplayTransition w act)]
    exact sr_strictly_orthogonal_step w act

/-- 3. Two-Player Lineage Tether Quota Lock (At most 1 free genesis plot per shared `orderId`) -/
def tryClaimTetherPlot (claimed : Nat) : Nat :=
  if claimed == 0 then 1 else claimed

theorem lineage_tether_single_claim (claimed : Nat) (h : claimed ≤ 1) :
    tryClaimTetherPlot claimed ≤ 1 := by
  unfold tryClaimTetherPlot
  split <;> omega

theorem lineage_tether_idempotent (claimed : Nat) :
    tryClaimTetherPlot (tryClaimTetherPlot claimed) = tryClaimTetherPlot claimed := by
  unfold tryClaimTetherPlot
  split <;> simp

/-- 4. Generative Master-Seed Oklab Readability (ΔL ≥ 26214 = 0.40) & Negative Space (60%..75%) -/
def minOklabDeltaLQ16 : Nat := 26214
def minNegSpaceQ16 : Nat := 39321
def maxNegSpaceQ16 : Nat := 49152

def enforceOklabContrast (rawDeltaL : Nat) : Nat :=
  if rawDeltaL < minOklabDeltaLQ16 then minOklabDeltaLQ16 else rawDeltaL

def clampNegativeSpace (rawRatio : Nat) : Nat :=
  if rawRatio < minNegSpaceQ16 then minNegSpaceQ16
  else if rawRatio > maxNegSpaceQ16 then maxNegSpaceQ16
  else rawRatio

theorem oklab_contrast_always_readable (rawDeltaL : Nat) :
    enforceOklabContrast rawDeltaL ≥ minOklabDeltaLQ16 := by
  unfold enforceOklabContrast
  split <;> omega

theorem negative_space_always_balanced (rawRatio : Nat) :
    minNegSpaceQ16 ≤ clampNegativeSpace rawRatio ∧ clampNegativeSpace rawRatio ≤ maxNegSpaceQ16 := by
  unfold clampNegativeSpace minNegSpaceQ16 maxNegSpaceQ16
  split
  · omega
  · split <;> omega
