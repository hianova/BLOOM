/-
  BLOOM v7.0.1 — 64-Byte Three.js + React Native + Prefab SSOT Engine
  ===================================================================
  AOT Formal Verification Kernel (`22` Theorems, `0 sorryAx`) + Declarative
  React-Native Nested Flexbox Compiler (`UiElement` -> `16,384B` `.bloom` Arena).
  Compiles and seals `build/studio.bloom` (`256 × 64B` VM page) consumed directly
  by the standalone zero-Lean-runtime pure C17 native runner (`build/bloom`) over
  `libbloom.a`, `FLOW v7.0.1` (`flow.h` + `searu.h` + `flow_opt_params.h`), and
  `shaders/bloom_core.wgsl` (`SDL3 GPU`).
-/

import Flow
import Std.Tactic.BVDecide

namespace Bloom

def version       : String := "7.0.1"
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

/-- Nested React-Native Flexbox child span `[parentLo + pad + offset, parentLo + pad + offset + childSpan]`
    never overflows the padded parent container `[parentLo + pad, parentHi - pad]`. -/
theorem flex_child_span_bounded (parentLo parentHi pad offset childSpan : Int)
    (hPad : 0 <= pad) (hOff : 0 <= offset) (hSpan : 0 <= childSpan)
    (hFit : 2 * pad + offset + childSpan <= parentHi - parentLo) :
    parentLo <= parentLo + pad + offset ∧
    parentLo + pad + offset <= parentLo + pad + offset + childSpan ∧
    parentLo + pad + offset + childSpan <= parentHi - pad ∧
    parentHi - pad <= parentHi := by
  omega

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

/-- Exact C17/WGSL inline UI label table (`BLOOM_SLOT_UI_LABEL_BASE = 250`, 4 × 16B labels per 64B cell)
    maps any masked UI node index `idx & 15` strictly into Arena Slots `[250 .. 253]` (`< 254`). -/
theorem ui_label_slot_inbounds_bv32 (idx : BitVec 32) :
    let slot := 250#32 + ((idx &&& 15#32) >>> 2#32)
    BitVec.ule 250#32 slot = true ∧ BitVec.ult slot 254#32 = true := by
  bv_decide

/-- Exact 64-bit `searu_heal_and_swap` phase-gate predicate (`FLOW v7.0.1`):
    SEARU wakes ONLY at `!CONVERGED && dt == 0` barriers, guaranteeing zero-chaos silence
    whenever `FLOW_STATUS_CONVERGED` (`bit 0`) is asserted or `dt != 0` (active 2-Jet descent). -/
def searuHealGateBV64 (status64 dt64 : BitVec 64) : Bool :=
  ((status64 &&& 1#64) == 0#64) && (dt64 == 0#64)

theorem searu_phase_gate_silence_bv64 (status64 dt64 : BitVec 64)
    (hSmooth : (status64 &&& 1#64) != 0#64 ∨ dt64 != 0#64) :
    searuHealGateBV64 status64 dt64 = false := by
  unfold searuHealGateBV64
  bv_decide

/-- Exact 64-bit `SEARU_SOLVE_INLINE` / `searu_ring0_step_1bit` Kan-pinned Sparse-`k` XOR mutation
    `cur ^^^ (flip &&& ~~~kanPin)` strictly preserves all hardware-pinned bits across `2^192` states. -/
theorem searu_kan_pin_flip_invariant_bv64 (cur flip kanPin : BitVec 64) :
    let next := cur ^^^ (flip &&& ~~~kanPin)
    (next &&& kanPin) = (cur &&& kanPin) := by
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

/-! ### Frame Header Bitflags (`BloomFrameHeader64.flags`) -/
namespace FrameFlag
  def idleSleep      : UInt32 := 1
  def xrayEnabled    : UInt32 := 2
  def trcSafe        : UInt32 := 4
  def colorblindGeom : UInt32 := 8
  def studioGi       : UInt32 := 16
end FrameFlag

/-! ### FLOW v7.0.1 Canonical Manifold Chart Indices (`Slots 232..247`) -/
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

@[extern "bloom_jsi_run_window"]
opaque runNativeWindow (eng : @& Engine) (onFrame : Unit → IO Unit) : IO UInt32

@[extern "bloom_jsi_verify_searu_core"]
opaque verifySearuCore (seed : UInt64) : IO Bool

@[extern "bloom_jsi_verify_gpu_alignment"]
opaque verifyGpuAlignment (eng : @& Engine) : IO Bool

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
    setFrameFlags eng FrameFlag.studioGi 0
  else
    setFrameFlags eng 0 FrameFlag.studioGi
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

/-- Canonical v7.0.1 Studio Blueprint authored via React-Native nested `<View>` Flexbox DSL
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
        .button "BLOOM 7.0" UiAction.none 65536
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

private def compileTimeBoundaryGate : IO Unit := do
  -- 0. Pure-CPU `libbloom.a` Public API + FLOW v7.0.1 Symbiotic SEARU x 2-Jet Gate
  let searuOk ← verifySearuCore 0xB100A60020265EA4
  requireContract "FLOW v7.0.1 SEARU (heal_and_swap + SOLVE_INLINE) + BVH==CSG 0-ULP + UTF-8/IME" searuOk

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
  requireContract "FLOW v7.0.1 Lyapunov convergence to 0-FPS Idle Sleep" (convergedA && convergedB)

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

#eval compileTimeBoundaryGate

end Bloom
