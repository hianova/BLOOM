/-
  BLOOM v6.0.0 — 64-Byte Three.js + React Native + Prefab SSOT Engine
  ===================================================================
  Top-Level Interactive Lean 4 Host + Formal Verification Kernel (`0 sorryAx`).
  Bridges directly via Zero-Copy JSI (`@[extern]`) over a 16,384-byte (256 × 64B)
  VM-page-aligned Arena shared with `libbloom.a`, `FLOW v6.0.0`, and the single-source
  `shaders/bloom_core.wgsl` compute kernel via SDL3 GPU (Metal MSL 2.4 & Vulkan SPIR-V).
-/

import Flow
import Std.Tactic.BVDecide

namespace Bloom

def version       : String := "6.0.0"
def cellBytes     : Nat    := 64
def arenaCells    : Nat    := 256
def arenaBytes    : Nat    := arenaCells * cellBytes
def trcMaxHzQ16   : Int    := 3 * Flow.q16One
def crystalFloor  : Int    := 4096

/-! ## Part 1: Formal Invariants & Mathematical Proofs (`0 sorryAx`) -/

/-- Exact 64-byte cell width and 16,384-byte (16KB VM page) Unified Memory Arena bijection. -/
theorem arena_page_bijection :
    cellBytes = 64 ∧ arenaCells = 256 ∧ arenaBytes = 16384 := by
  decide

/-- OOB-immune Z/256Z masked slot addressing always stays strictly inside `[0, 255]`. -/
theorem arena_slot_mask_inbounds (idx : Nat) :
    idx % 256 < arenaCells := by
  unfold arenaCells
  omega

/-- Polynomial Smooth-Min CSG union (`min a b - corr` with `corr >= 0`) strictly encloses both operands. -/
def polySminModel (a b corr : Int) : Int :=
  (if a <= b then a else b) - corr

theorem poly_smin_le_operands (a b corr : Int) (hCorr : 0 <= corr) :
    polySminModel a b corr <= a ∧ polySminModel a b corr <= b := by
  unfold polySminModel
  split <;> omega

/-- Pointer-free Linear BVH parent union AABB strictly encloses both child AABBs. -/
structure Interval1D where
  lo : Int
  hi : Int
  deriving Repr, DecidableEq

def Interval1D.contains (iv : Interval1D) (x : Int) : Prop :=
  iv.lo <= x ∧ x <= iv.hi

def Interval1D.union (a b : Interval1D) : Interval1D :=
  { lo := if a.lo <= b.lo then a.lo else b.lo
    hi := if a.hi >= b.hi then a.hi else b.hi }

theorem bvh_parent_encloses_children (left right : Interval1D) (x : Int)
    (h : left.contains x ∨ right.contains x) :
    (left.union right).contains x := by
  unfold Interval1D.contains Interval1D.union at *
  dsimp
  rcases h with ⟨hl0, hl1⟩ | ⟨hr0, hr1⟩
  · constructor <;> split <;> omega
  · constructor <;> split <;> omega

/-- Beer-Lambert Liquid Glass transmittance stays strictly inside `[4096, 65536]` (crystal floor & energy conservation). -/
def clampTransmittance (rawQ16 : Int) : Int :=
  if rawQ16 < crystalFloor then crystalFloor
  else if rawQ16 > Flow.q16One then Flow.q16One
  else rawQ16

theorem beer_lambert_crystal_floor_bounded (rawQ16 : Int) :
    4096 <= clampTransmittance rawQ16 ∧ clampTransmittance rawQ16 <= 65536 := by
  unfold clampTransmittance crystalFloor Flow.q16One
  split
  · omega
  · split <;> omega

/-- Harding / Console TRC Photosensitive Guard strictly clamps flash frequency to `<= 3.0 Hz` (`196,608` Q16). -/
def clampTrcStrobe (hzQ16 : Int) (safeEnabled : Bool) : Int :=
  let nonNeg := if hzQ16 < 0 then 0 else hzQ16
  if safeEnabled then
    if nonNeg > trcMaxHzQ16 then trcMaxHzQ16 else nonNeg
  else nonNeg

theorem trc_strobe_clamp_safe (hzQ16 : Int) :
    0 <= clampTrcStrobe hzQ16 true ∧ clampTrcStrobe hzQ16 true <= 196608 := by
  unfold clampTrcStrobe trcMaxHzQ16 Flow.q16One
  dsimp
  split <;> split <;> omega

/-- Golden-Ratio Auto-Bento partition around cut `c` with gap `2 * halfGap` (`halfGap >= 1`) yields strictly disjoint panels inside `[x0, x1]`. -/
theorem bento_split_disjoint (x0 c x1 halfGap : Int)
    (hOrder : x0 + halfGap <= c ∧ c + halfGap <= x1) (hGap : 1 <= halfGap) :
    x0 <= c - halfGap ∧ (c - halfGap) < (c + halfGap) ∧ c + halfGap <= x1 := by
  omega

/-- Portable `u32x2` (`lo32`, `hi32`) Wave64 bitmask decomposition is a total bijection on `[0, 63]`. -/
def wave64Decompose (bitIdx : Nat) : Nat × Nat :=
  (bitIdx / 32, bitIdx % 32)

def wave64Reconstruct (pair : Nat × Nat) : Nat :=
  pair.1 * 32 + pair.2

theorem wave64_u32x2_bijection (bitIdx : Nat) (h : bitIdx < 64) :
    let p := wave64Decompose bitIdx
    p.1 < 2 ∧ p.2 < 32 ∧ wave64Reconstruct p = bitIdx := by
  unfold wave64Decompose wave64Reconstruct
  dsimp
  omega

/-- Non-destructive Prefab Instance Override & O(1) Revert-to-SSOT idempotence. -/
structure PrefabInstModel where
  prefabId        : Nat
  styleOverride   : Nat := 0
  matterOverride  : Nat := 0
  paramOverride   : Int := 0
  thermalKelvin   : Nat := 300
  overridden      : Bool := false
  deriving Repr, DecidableEq

def applyOverride (inst : PrefabInstModel) (style matter : Nat) (delta : Int) : PrefabInstModel :=
  { inst with
    styleOverride  := style
    matterOverride := matter
    paramOverride  := delta
    overridden     := true }

def revertToSSOT (inst : PrefabInstModel) : PrefabInstModel :=
  { inst with
    styleOverride  := 0
    matterOverride := 0
    paramOverride  := 0
    thermalKelvin  := 300
    overridden     := false }

theorem prefab_revert_restores_ssot (pid style matter : Nat) (delta : Int) :
    let base : PrefabInstModel := { prefabId := pid }
    revertToSSOT (applyOverride base style matter delta) = base := by
  rfl

theorem prefab_revert_idempotent (inst : PrefabInstModel) :
    revertToSSOT (revertToSSOT inst) = revertToSSOT inst := by
  rfl

/-! ### Machine-Word Hardware & ABI Bit-Blasted Theorems (`Std.Tactic.BVDecide` CaDiCaL SAT) -/

/-- Exact C17/WGSL 32-bit masked arena slot index `idx & 0xFFu` is OOB-immune across all `2^32` words. -/
theorem arena_slot_mask_bv32_inbounds (idx : BitVec 32) :
    BitVec.ult (idx &&& 0xFF#32) 256#32 = true := by
  bv_decide

/-- Exact C17/WGSL `u32x2` (`lo32`, `hi32`) Wave64 bit-set models matching `bloom_wave64_set_u32x2`. -/
def wave64SetLoBV (lo32 bitIdx : BitVec 32) : BitVec 32 :=
  if BitVec.ult bitIdx 32#32 then lo32 ||| (1#32 <<< (bitIdx &&& 31#32)) else lo32

def wave64SetHiBV (hi32 bitIdx : BitVec 32) : BitVec 32 :=
  if !BitVec.ult bitIdx 32#32 && BitVec.ult bitIdx 64#32 then hi32 ||| (1#32 <<< (bitIdx &&& 31#32)) else hi32

/-- Exact C17/WGSL `u32x2` (`lo32`, `hi32`) Wave64 bit-test model matching `bloom_wave64_test_u32x2`. -/
def wave64TestBV (lo32 hi32 bitIdx : BitVec 32) : Bool :=
  let word  := if BitVec.ult bitIdx 32#32 then lo32 else hi32
  let shift := bitIdx &&& 31#32
  ((word >>> shift) &&& 1#32) != 0#32

/-- Setting any valid Wave64 lane `bitIdx < 64` tests `true` across all `2^96` (`lo32, hi32, bitIdx`) states. -/
theorem wave64_set_test_exact_bv32 (lo32 hi32 bitIdx : BitVec 32)
    (h : BitVec.ult bitIdx 64#32 = true) :
    wave64TestBV (wave64SetLoBV lo32 bitIdx) (wave64SetHiBV hi32 bitIdx) bitIdx = true := by
  unfold wave64SetLoBV wave64SetHiBV wave64TestBV
  bv_decide

/-- Setting Wave64 lane `i < 64` never mutates any distinct lane `j < 64` (`i != j`, zero cross-lane bleed). -/
theorem wave64_set_frame_independence_bv32 (lo32 hi32 i j : BitVec 32)
    (hi : BitVec.ult i 64#32 = true) (hj : BitVec.ult j 64#32 = true) (hne : i != j) :
    wave64TestBV (wave64SetLoBV lo32 i) (wave64SetHiBV hi32 i) j = wave64TestBV lo32 hi32 j := by
  unfold wave64SetLoBV wave64SetHiBV wave64TestBV
  bv_decide

/-- Exact C17 `BLOOM_CSG_MASK_PACK` 32-bit bitfield codec has zero inter-field bit bleed. -/
def csgMaskPackBV (prim1 prim2 op polar grade : BitVec 32) : BitVec 32 :=
  ((prim1 &&& 0xF#32) <<< 0#32) |||
  ((prim2 &&& 0xF#32) <<< 4#32) |||
  ((op    &&& 0xF#32) <<< 8#32) |||
  ((polar &&& 0x3F#32) <<< 12#32) |||
  ((grade &&& 0xF#32) <<< 18#32)

theorem csg_mask_pack_roundtrip_bv32 (prim1 prim2 op polar grade : BitVec 32) :
    let m := csgMaskPackBV prim1 prim2 op polar grade
    ((m >>> 0#32)  &&& 0xF#32)  = (prim1 &&& 0xF#32) ∧
    ((m >>> 4#32)  &&& 0xF#32)  = (prim2 &&& 0xF#32) ∧
    ((m >>> 8#32)  &&& 0xF#32)  = (op    &&& 0xF#32) ∧
    ((m >>> 12#32) &&& 0x3F#32) = (polar &&& 0x3F#32) ∧
    ((m >>> 18#32) &&& 0xF#32)  = (grade &&& 0xF#32) := by
  unfold csgMaskPackBV
  bv_decide

/-- Signed 32-bit two's-complement Q16.16 TRC Strobe Clamp (`<= 3.0 Hz = 196608`) is overflow-free. -/
def clampTrcStrobeBV32 (hzQ16 : BitVec 32) : BitVec 32 :=
  let clampedHi := if BitVec.slt 196608#32 hzQ16 then 196608#32 else hzQ16
  if BitVec.slt clampedHi 0#32 then 0#32 else clampedHi

theorem trc_strobe_clamp_bv32 (hzQ16 : BitVec 32) :
    let res := clampTrcStrobeBV32 hzQ16
    BitVec.sle 0#32 res = true ∧ BitVec.sle res 196608#32 = true := by
  unfold clampTrcStrobeBV32
  bv_decide

/-- Signed 32-bit two's-complement Beer-Lambert Transmittance Clamp (`[4096, 65536]`) is overflow-free. -/
def clampGlassTransBV32 (rawQ16 : BitVec 32) : BitVec 32 :=
  if BitVec.slt rawQ16 4096#32 then 4096#32
  else if BitVec.slt 65536#32 rawQ16 then 65536#32
  else rawQ16

theorem beer_lambert_clamp_bv32 (rawQ16 : BitVec 32) :
    let res := clampGlassTransBV32 rawQ16
    BitVec.sle 4096#32 res = true ∧ BitVec.sle res 65536#32 = true := by
  unfold clampGlassTransBV32
  bv_decide

/-- Exact 16-bit Kan-Lock `@swap` mode projection (`(req & ~lock) | (cur & lock)`) preserves locked invariants. -/
theorem kan_lock_mode_preserve_bv16 (modeReq curMode kanLock : BitVec 16) :
    let nextMode := (modeReq &&& ~~~kanLock) ||| (curMode &&& kanLock)
    (nextMode &&& kanLock) = (curMode &&& kanLock) ∧
    (nextMode &&& ~~~kanLock) = (modeReq &&& ~~~kanLock) := by
  bv_decide

/-- Exact 16-bit Prefab `BLOOM_INST_FLAG_OVERRIDDEN` (`0x0004`) set/revert bit algebra. -/
theorem prefab_override_revert_flags_bv16 (flags : BitVec 16) (hClean : (flags &&& 4#16) = 0#16) :
    let ovr := flags ||| 4#16
    let rev := ovr &&& ~~~4#16
    (ovr &&& 4#16) = 4#16 ∧ rev = flags ∧ (rev &&& 4#16) = 0#16 := by
  bv_decide

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

/-! ### Built-in Declarative SVG-UI Action Opcodes (`data-action`) -/
namespace UiAction
  def none         : UInt32 := 0
  def scrubXRay    : UInt32 := 1
  def prefabRevert : UInt32 := 2
  def cycleMatter  : UInt32 := 3
  def swapChart    : UInt32 := 4
  def toggleTrc    : UInt32 := 5
  def focusInput   : UInt32 := 6
end UiAction

/-! ### FLOW v6.0.0 Canonical Manifold Chart Indices (`Slots 232..247`) -/
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

@[extern "bloom_jsi_run_window"]
opaque runNativeWindow (eng : @& Engine) (onFrame : Unit → IO Unit) : IO UInt32

@[extern "bloom_jsi_verify_searu_core"]
opaque verifySearuCore (seed : UInt64) : IO Bool

@[extern "bloom_jsi_verify_gpu_alignment"]
opaque verifyGpuAlignment (eng : @& Engine) : IO Bool

/-! ## Part 3: Declarative Scene + SVG-UI Blueprint & Top-Level Interactive Reducer -/

structure PrefabNodeSpec where
  prefabId : UInt32
  xMilli   : Int
  yMilli   : Int
  zMilli   : Int
  scaleMil : Int := 1000
  deriving Repr

structure StudioBlueprint where
  svgPath   : String
  latexFormula : String
  latexXMilli  : Int := 520
  latexYMilli  : Int := -645
  latexEmMilli : Int := 42
  latexRgb24   : UInt32 := 0xFFD848
  sceneNodes   : List PrefabNodeSpec
  deriving Repr

/-- Mounts a complete declarative SVG-UI + LaTeX + 3D Prefab scene into the 16KB Unified Memory Arena. -/
def mountBlueprint (eng : Engine) (bp : StudioBlueprint) : IO (UInt32 × UInt32 × List UInt32) := do
  let uiCnt ← loadSvgUI eng bp.svgPath
  let lxCnt ← compileLatex eng bp.latexFormula
    (q16Milli bp.latexXMilli) (q16Milli bp.latexYMilli) (q16Milli bp.latexEmMilli) bp.latexRgb24
  let mut ids : List UInt32 := []
  for item in bp.sceneNodes do
    let iid ← spawnPrefab eng item.prefabId
      (q16Milli item.xMilli) (q16Milli item.yMilli) (q16Milli item.zMilli) (q16Milli item.scaleMil)
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

/-- Default Studio blueprint shared between runtime Studio and compile-time boundary verification. -/
def defaultStudioBlueprint : StudioBlueprint := {
  svgPath      := "specs/studio_inspector.svg"
  latexFormula := "\\int \\frac{\\sqrt{\\eta^2 + \\kappa^2}}{2\\pi} d\\Omega"
  sceneNodes   := [
    { prefabId := Prefab.jadeVessel,     xMilli := 0,     yMilli := 0,    zMilli := 0,    scaleMil := 1150 },
    { prefabId := Prefab.citadelCore,    xMilli := -1650, yMilli := -100, zMilli := 350,  scaleMil := 950  },
    { prefabId := Prefab.quantumTorus,   xMilli := 1650,  yMilli := 120,  zMilli := 250,  scaleMil := 950  },
    { prefabId := Prefab.thermalReactor, xMilli := 0,     yMilli := 1250, zMilli := -600, scaleMil := 850  }
  ]
}

/-- Single-source WGSL compute shader path (resolved to build/bloom_core.{metal,spv} by SDL3 GPU host). -/
def defaultShaderPath : String := "shaders/bloom_core.wgsl"

/-- Default 60Hz interactive Studio launcher (`0 lean -c`, invoked via `--load-dynlib`). -/
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

/-! ## Part 4: Compile-Time JSI & Hardware Boundary Gate (`0` External Test Files)
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

private def compileTimeBoundaryGate : IO Unit := do
  -- 0. Pure-CPU `libbloom.a` Public API + SEARU `DualChaosState` & `Epiphany` Torture Gate
  let searuOk ← verifySearuCore 0xB100A60020265EA4
  requireContract "SEARU Core API + BVH==CSG 0-ULP + UTF-8/IME + DQ/Brick torture" searuOk

  -- 1. Hostile & Singular Boundary Sweep + 8-Prefab / X-Ray / UI CPU-vs-GPU Spatial Alignment Gate
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
  let _ ← stepAndRender engEdge 0xFFFF true
  let edgeSealed ← sealAndVerifyArena engEdge
  requireContract "Adversarial boundary Arena Merkle seal intact" edgeSealed

  -- 2. Dual-Engine 256-Trial SplitMix64 Property Sweep & Pixel-Exact Determinism Gate
  let wPx : UInt32 := 640
  let hPx : UInt32 := 360
  let engA ← createEngine wPx hPx defaultShaderPath
  let engB ← createEngine wPx hPx defaultShaderPath

  let (uiA, lxA, idsA) ← mountBlueprint engA defaultStudioBlueprint
  let (uiB, lxB, idsB) ← mountBlueprint engB defaultStudioBlueprint
  requireContract "Canonical Blueprint UI/LaTeX/Prefab isomorphism"
    (uiA == uiB && lxA == lxB && idsA == idsB && uiA >= 6 && lxA >= 6)

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
  requireContract "FLOW v6.0.0 Lyapunov convergence to 0-FPS Idle Sleep" (convergedA && convergedB)

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

#eval compileTimeBoundaryGate

end Bloom
