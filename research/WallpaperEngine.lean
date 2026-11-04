/-
  ============================================================================
  BLOOM: Cross-Platform Live Wallpaper Engine Formal Invariants
  ============================================================================
  Formalizes Dual-Threshold Occlusion Pause Hysteresis ([75%, 90%]),
  Battery Eco-Governor Hysteresis ([40%, 50%]), 0Hz Zero-Wakeup Sleep,
  and 2-Jet Cursor Parallax Framing Bounds for FlowWallpaperState64.
  ============================================================================
-/

namespace FlowGame.WallpaperEngine

/--
  64-Byte Wallpaper Engine State Projection in Q16.16 / Natural units:
  powerMode:
    0 = Active60Hz (16666 us frame budget)
    1 = Battery30Hz (33333 us frame budget)
    2 = Paused0Hz (0 GPU wakeups when occluded >= 90% or fullscreen app active)
-/
structure WallpaperState64 where
  masterSeed : Nat
  occlusionQ16 : Nat
  batteryPct : Nat
  acConnected : Bool
  prevPaused : Bool
  prevEco : Bool
  parallaxOffsetMag : Nat
  maxParallaxBound : Nat
  deriving Repr, DecidableEq

/--
  Dual-threshold occlusion hysteresis:
  - Enters Paused0Hz when occlusionQ16 >= 58982 (90% of 65536)
  - Exits Paused0Hz only when occlusionQ16 <= 49152 (75% of 65536)
  - Strictly preserves `prevPaused` inside the deadband (49152, 58982)
-/
def resolveOcclusionPause (s : WallpaperState64) : Bool :=
  if s.occlusionQ16 >= 58982 then
    true
  else if s.occlusionQ16 <= 49152 then
    false
  else
    s.prevPaused

/--
  Dual-threshold battery eco hysteresis:
  - When on AC power (`acConnected = true`), eco mode is false.
  - On battery, enters Eco30Hz when `batteryPct <= 40`, exits when `batteryPct >= 50`,
    and preserves `prevEco` inside `(40, 50)`.
-/
def resolveBatteryEco (s : WallpaperState64) : Bool :=
  if s.acConnected then
    false
  else if s.batteryPct <= 40 then
    true
  else if s.batteryPct >= 50 then
    false
  else
    s.prevEco

/--
  Target GPU frame wakeup rate in Hz:
  - 0 Hz when occluded/paused (`resolveOcclusionPause s = true`)
  - 30 Hz when in battery eco mode (`resolveBatteryEco s = true`)
  - 60 Hz when active on AC or high battery
-/
def targetRefreshHz (s : WallpaperState64) : Nat :=
  if resolveOcclusionPause s then
    0
  else if resolveBatteryEco s then
    30
  else
    60

/--
  Clamped 2-Jet cursor parallax displacement magnitude so the foreground hero
  never drifts outside the safe CinemaScope composition frame.
-/
def clampParallaxMag (s : WallpaperState64) : Nat :=
  if s.parallaxOffsetMag <= s.maxParallaxBound then
    s.parallaxOffsetMag
  else
    s.maxParallaxBound

/--
  Theorem 1 (Occlusion Hysteresis Deadband Stability):
  Inside the deadband `49152 < s.occlusionQ16 < 58982` (75%..90% occlusion),
  the pause state is strictly invariant (`resolveOcclusionPause s = s.prevPaused`),
  eliminating window-drag boundary chatter.
-/
theorem occlusion_pause_hysteresis_no_chatter (s : WallpaperState64)
    (hLow : s.occlusionQ16 > 49152)
    (hHigh : s.occlusionQ16 < 58982) :
    resolveOcclusionPause s = s.prevPaused := by
  unfold resolveOcclusionPause
  have hNotHigh : ¬(s.occlusionQ16 >= 58982) := by omega
  have hNotLow : ¬(s.occlusionQ16 <= 49152) := by omega
  simp [hNotHigh, hNotLow]

/--
  Theorem 2 (Fullscreen / 90% Occlusion Guarantees 0Hz Zero GPU Wakeups):
  Whenever another application covers >= 90% of the screen (`occlusionQ16 >= 58982`),
  the wallpaper engine drops to 0 Hz (`targetRefreshHz s = 0`).
-/
theorem occluded_zero_gpu_wakeups (s : WallpaperState64)
    (hOcc : s.occlusionQ16 >= 58982) :
    targetRefreshHz s = 0 := by
  unfold targetRefreshHz resolveOcclusionPause
  simp [hOcc]

/--
  Theorem 3 (Battery Eco Hysteresis Deadband Stability):
  When unplugged (`acConnected = false`) and battery is inside `(40, 50)`,
  the eco state is strictly invariant (`resolveBatteryEco s = s.prevEco`).
-/
theorem battery_eco_hysteresis_soundness (s : WallpaperState64)
    (hUnplugged : s.acConnected = false)
    (hAbove40 : s.batteryPct > 40)
    (hBelow50 : s.batteryPct < 50) :
    resolveBatteryEco s = s.prevEco := by
  unfold resolveBatteryEco
  have hNotLe40 : ¬(s.batteryPct <= 40) := by omega
  have hNotGe50 : ¬(s.batteryPct >= 50) := by omega
  simp [hUnplugged, hNotLe40, hNotGe50]

/--
  Theorem 4 (2-Jet Cursor Parallax Safe Framing Bound):
  The clamped cursor parallax offset never exceeds `maxParallaxBound`.
-/
theorem cursor_parallax_bounded_framing (s : WallpaperState64) :
    clampParallaxMag s <= s.maxParallaxBound := by
  unfold clampParallaxMag
  split <;> omega

end FlowGame.WallpaperEngine
