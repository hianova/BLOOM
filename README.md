# BLOOM v9.0.0 — Zero-Noise Radiance Cascades, 2-Jet Differential Field, Modal DSP, Concentric Shells, Time-Rewind & Geodesic Cinema Engine

**AOT Lean 4 Formal Compiler & React-Native Flexbox DSL (`40` Theorems, `0 sorryAx`, `0 lean -c`, `0` External Test Files) → Sealed `16KB` `.bloom` Container (`BLM7` / `BLOMIMG7`) + Auxiliary `32KB` `BloomMeshArena` → Standalone Zero-Lean-Runtime Pure C17 Native Studio & CLI (`build/bloom`) + Pure-GPR / Zero-Float / Zero-Heap / Zero-`blr` C17 Core (`libbloom.a`) + 3-Pass Zero-Noise 3D Radiance Cascades GI (`524KB` `rc_buf`) + Zero-Texture 2-Jet Differential Geometry Field (`BloomFieldJet64`) + Zero-WAV 48 kHz Modal Physical-Modeling DSP (`BloomAudioRig`, `576B`) + Sub-Frame (`20.8 μs`) Swept-CCD & Huygens Shockwave + Concentric SDF Gameplay Shells / `-18dB` Hitstop Contrast (`BloomGameplayState64`) + $O(1)$ `16KB` Arena & `576B` AudioRig Circular Spacetime Rewind (`BloomSpacetimeSnap`, `16,960B`) + Relativistic Geodesic Lensing & Lorentz Doppler (`BloomMetricScale64`) + Anamorphic `2.39:1` `CineCamera` & CMX 3600 `.edl` Export + Pure-GPR Moodboard-to-SSOT Compiler (`BloomMoodboard64`) + Standalone WebGPU `.html` & Kitty TTY Egress + Symbiotic `SEARU × FLOW 2-Jet` + C++20 `64B` ABI Gate (`35` Cells) + Single-Source `WGSL` Compute Shader (`naga` → `SPIR-V` + `MSL 2.4`) & `176 / 176` (`100.0%`) Mach-O Binary Sabotage Audit atop [`FLOW v8.0.0`](https://github.com/hianova/FLOW).**

---

## 1. Architectural Positioning: Post-Texture Sensory Engine, Time-Rewind Gameplay & AOT `.bloom` Decoupling

Just as [`FLOW v8.0.0`](https://github.com/hianova/FLOW) uses `Flow.lean` strictly as an AOT `.flow` formal compiler and `build/flow` as a pure C runtime with self-synthesized [`core/flow_opt_params.h`](https://github.com/hianova/FLOW/blob/main/core/flow_opt_params.h) constants, `BLOOM v9.0.0` pairs formal AOT verification with a full zero-texture differential-geometry & geodesic-lens renderer, zero-noise 3D Radiance Cascades GI, pure-GPR Doppler modal acoustic synthesis, concentric gameplay shells with $O(1)$ spacetime time-rewind, and a pure-GPR moodboard-to-SSOT compiler — verified end-to-end by `make verify` with **zero authored `.metal` / `.m` / `.mm` / `.swift` files**, **zero `lean -c` codegen**, **zero Lean runtime (`0 leanshared`) in `build/bloom`**, **zero external texture atlases**, **zero prerecorded `.wav` assets**, **zero fake software fallbacks**, and **zero external `.c` / `.lean` test files**:

```text
  ┌────────────────────────────────────────────────────────────────────────────┐
  │ 1. AOT Formal Verification & React-Native Flexbox Compiler (Bloom.lean)    │
  │    - 40 theorems (25 arith/CAD/phys/gp + 15 CaDiCaL SAT bv_decide, 0 sorry)│
  │    - Declarative nested `<View>` Flexbox DSL (`UiElement` -> `compileUiTree│
  │      -> `0`-slot layout wrappers, inline 16B labels & auto-fit `LatexCard`)│
  │    - Elaboration-time `#eval compileTimeBoundaryGate` (`0` external tests):│
  │      • `verifySearuCore`: 10 pure-GPR C17 suites incl. STL/GLB Jordan,     │
  │        2-Jet `(H, K, e1)` field, sub-frame CCD + modal DSP, concentric SDF │
  │        shells (-18dB hitstop), 16KB spacetime rewind, Doppler & moodboard  │
  │      • `verifyGpuAlignment`: Dispatches 0..5 over 8-Prefab, X-Ray, UI,     │
  │        Diagonal Section + Mesh BVH, 3-Pass Radiance Cascades GI + Occluder,│
  │        and v9.0.0 Geodesic Metric + 2.39:1 CineCamera Optics + Shells HUD  │
  │      • Exports Merkle-sealed `16,384B` `build/studio.bloom` VM-page image  │
  │    - Standalone kernel replay via `leanchecker`                            │
  └─────────────────────────────────────┬──────────────────────────────────────┘
                                        │ Sealed `16,384B` (`256 × 64B`) `.bloom`
                                        │ (`BLOMIMG7` FNV-1a Merkle over Slots != 249)
  ┌─────────────────────────────────────▼──────────────────────────────────────┐
  │ 2. Zero-Lean-Runtime Pure C17 Studio, DSP & Core (`bloom` + `libbloom.a`)  │
  │    - `src/bloom_cli.c` -> `build/bloom` (`0` `<lean/lean.h>`, `0 leanshared│
  │      audited via `otool -L` / `ldd` & `nm -u`):                            │
  │      • `.bloom`/`.stl`/`.glb` + `--cine`, `--metric phi,beta`, `--shells`, │
  │        `--rewind N`, `--import-mood`, `--export-html`, `--edl-out`, `--kitty│
  │      • Interactive `CINE/LENS/SHELLS/REWIND/RC/FIELD/STRIKE` + Headless CLI│
  │    - `libbloom.a` (`-mgeneral-regs-only -fsanitize-trap=all`, 0 float/heap,│
  │      `0` `blr` indirect calls across `24,954` A64 insns / `61` functions): │
  │      • Pillar 1 (`bloom_scene.c`): CSG, `.stl`/`.glb` BVH, DQ skeleton,    │
  │        4×4×4 bricks, 2-Jet field, Section/Slab, Relativistic Metric scale, │
  │        Thin-lens `CineCamera`, Pure-GPR Moodboard-to-SSOT, 8 Matters       │
  │      • Pillar 2 (`bloom_ui.c`): Streamed `.svg` DFA, Flexbox, Auto-Bento,  │
  │        `SEARU_SOLVE_INLINE` glass-contrast solver, Pointer/IME, DFA LaTeX  │
  │      • Pillar 3 (`bloom_prefab.c`): Prefab SSOT, O(1) Revert, Swept-CCD    │
  │        Rebound, Concentric Shells + 6-Frame Buffer + Fulcrum + -18dB Hit,  │
  │        16KB+576B Circular Rewind, Doppler Modal DSP, `FLOW v8.0.0` 2-Jet    │
  │    - Binary Sabotage (`tools/sabotage_check.py`): 176 / 176 (100.0%) killed│
  └─────────────────────────────────────┬──────────────────────────────────────┘
                                        │ `SDL3 GPU`: `@group(0) b(0)` 16KB Arena
                                        │ + `b(1)` 32KB Mesh + `@group(1) b(1)` 524KB RC
  ┌─────────────────────────────────────▼──────────────────────────────────────┐
  │ 3. 4-Pass GPU Compute, 48kHz Audio, WebGPU HTML & TTY Host (WGSL + C17)    │
  │    - `shaders/bloom_core.wgsl`: Sole authored shader SSOT (`0` u64/i64)    │
  │      • Pass 1..3: `bloom_rc_cascade2` (8×4×8×16) -> `bloom_rc_cascade1`    │
  │        (16×8×16×8) -> `bloom_rc_cascade0` (16×8×16×6) octahedral 3D GI     │
  │      • Pass 4: `bloom_render_frame` (`eval_diff_jet2`, Geodesic/Einstein   │
  │        lensing + Lorentz aberration, RC + Huygens wave, 2.39:1 Cine DoF/  │
  │        anamorphic streaks, concentric Graze/Hitstop HUD & LaTeX overlay)   │
  │      ├─ `naga` build-time -> `build/bloom_core.spv`   (Vulkan SPIR-V)      │
  │      └─ `naga` build-time -> `build/bloom_core.metal` (Metal MSL 2.4)      │
  │    - `src/bloom_gpu_host.c`: 4-pipeline `SDL3 GPU` + `SDL_AudioStream`/WAV,│
  │      CMX 3600 `.edl`, standalone zero-dep WebGPU `.html` & Kitty APC TTY   │
  └────────────────────────────────────────────────────────────────────────────┘
```

### Physical Memory, Sensory Breakthroughs & v9.0.0 Recovered Engines
- **Zero-Parse `16,384B` `.bloom` Arena + `32,768B` `BloomMeshArena` + `524,288B` `rc_buf`**: [`Bloom.lean`](Bloom.lean) compiles the nested React-Native Flexbox UI tree, analytic SDF LaTeX cards, 3D Prefab instances, section/physics descriptors (`Slots 254..255`), and 16 `FLOW v8.0.0` Manifold Charts into a single `16,384-byte` (`256 × 64B`) `BloomEngineArena`, seals `Slot 249` (`BloomImageHeader64`, `"BLOMIMG7"`) with a 64-bit FNV-1a Merkle root over all non-superblock slots (`Slots != 249`), and writes `build/studio.bloom`.
- **3-Pass Zero-Noise 3D Radiance Cascades GI**: [`shaders/bloom_core.wgsl`](shaders/bloom_core.wgsl) dispatches a hierarchical 3D octahedral probe cascade into `@group(1) @binding(1) rc_buf` (`32,768 × vec4<f32>` = `524,288B` / Metal `[[buffer(2)]]`) across `bloom_rc_cascade2` (`8×4×8 × 16`), `bloom_rc_cascade1` (`16×8×16 × 8`), and `bloom_rc_cascade0` (`16×8×16 × 6`), integrating indirect bounce and specular GI with zero Monte-Carlo temporal noise.
- **Zero-Texture 2-Jet Differential Geometry & Continuous Field**: [`bloom_scene_eval_jet2_q16`](src/bloom_scene.c) (`BloomFieldJet64`) and [`eval_diff_jet2`](shaders/bloom_core.wgsl) extract Mean Curvature $H$, Gaussian Curvature $K$, anisotropy, and tangent principal direction $\mathbf{e}_1$ directly from the analytical SDF for burnished ridges, crevice patina, and flow-aligned micro-grooves.
- **Zero-WAV Pure-GPR Modal DSP (`576B` `BloomAudioRig`) + Sub-Frame Swept-CCD (`20.8 μs`)**: [`bloom_audio_step_frame64`](src/bloom_prefab.c) locks modal excitation to `(earliest_toi_q16 * 800) >> 16` (**`0` pre-TOI sample leakage**) alongside [`bloom_physics_step_arena64`](src/bloom_prefab.c) piecewise `(toi, 1 - toi)` rebound and Huygens contact wavefronts.
- **v9.0.0 Concentric SDF Gameplay Shells, `-18dB` Hitstop Contrast & $O(1)$ Spacetime Rewind**: [`bloom_gameplay_step64`](src/bloom_prefab.c) (`BloomGameplayState64`) evaluates ordered `hurt < 0 < graze <= hit` shells, dual-threshold graze hysteresis (`hyst_enter <= hyst_exit`) for zero chatter, 6-frame input buffering with equal-and-opposite fulcrum momentum conservation, `-18dB` (`8231 / 65536`) hitstop silence before catharsis release (`4x` peak boom), and circular `64`-slot `BloomSpacetimeSnap` (`16,960B`) rewind via [`bloom_rewind_push`](src/bloom_prefab.c) / [`bloom_rewind_restore`](src/bloom_prefab.c).
- **v9.0.0 Relativistic Geodesic Lensing, Doppler DSP, `2.39:1` `CineCamera` & Moodboard Compiler**: [`bloom_metric_eval_scale64`](src/bloom_scene.c) (`BloomMetricScale64`) + `BLOOM_FLAG_METRIC_FIELD` drive Schwarzschild horizon/photon-ring deflection, Lorentz relativistic aberration, and modal Doppler pitch shifting `D = sqrt((1 + beta)/(1 - beta))`; [`bloom_camera_step_cine64`](src/bloom_scene.c) + `BLOOM_FLAG_CINE_OPTICS` drive deadzone tracking, quadratic trauma shake, `2.39:1` letterbox, CoC DoF, Panavision blue anamorphic streaks, and CMX 3600 `.edl` reels; [`bloom_moodboard_extract_rgb24`](src/bloom_scene.c) & [`bloom_moodboard_compile_to_ssot64`](src/bloom_scene.c) synthesize `BloomArtStyle64`/`BloomPrefabDesc64` from `.ppm` palettes, with [`bloom_export_webgpu_html`](src/bloom_gpu_host.c) & [`bloom_gpu_write_kitty_tty`](src/bloom_gpu_host.c) for portable browser/terminal output.
- **Cross-Platform `SDL3 GPU` Dispatch & `0-FPS` Idle Short-Circuit**: [`src/bloom_gpu_host.c`](src/bloom_gpu_host.c) short-circuits immediately when `BLOOM_FLAG_IDLE_SLEEP` is asserted (**`0` bytes transferred and `0` compute dispatches when idle**).

---

## 2. Unified `16,384-Byte` Arena, `32KB` Mesh & `512KB` RC Buffer Map

Every ABI cell is strictly **`64 bytes` (`16 × u32` / `8 × u64`)** aligned to `64` bytes (`BLOOM_STATIC_ASSERT64`, `35` × `64B` cell structures + `BloomAudioRig` `9 × 64B = 576B` + `BloomSpacetimeSnap` `265 × 64B = 16,960B` in [`include/bloom_abi.h`](include/bloom_abi.h)), addressed via $\mathbb{Z}/256\mathbb{Z}$ OOB-immune masked indexing (`idx & 0xFFu`):

| Buffer / Slots | Cell Type ([`include/bloom_abi.h`](include/bloom_abi.h)) | Responsibility |
| :--- | :--- | :--- |
| Arena `0` | `BloomFrameHeader64` | Magic `"BLM7"`, resolution, counts, X-Ray plane, `IDLE_SLEEP` / `TRC_SAFE` / `STUDIO_GI` / `RC` / `DIFF_FIELD` / `SUBFRAME_LOCK` / `CINE_OPTICS` / `METRIC_FIELD` / `GAMEPLAY_SHELLS` flags |
| Arena `1` | `BloomCamera64` | Physical Thin-Lens (`focal_mm`, `aperture_fstop`, `focus_dist`), `2.39:1` deadzone CineCam & 2-Jet Orbit Rig |
| Arena `2` | `BloomRegBank64` | Zero-VDOM `12`-channel `Q16.16` register bank (`0..3` general, `METRIC_PHI`, `LORENTZ_BETA`, `FLUX_PRESSURE`, `HITSTOP_FRAMES`, `GRAZE_SHELL`, `GRAZE_COUNT`, `REWIND_CURSOR`, `CINE_TRAUMA`) + MAC |
| Arena `3` | `BloomDomState64` | `Wave64` (`u32x2`) DOM mask, hover/active/Pointer-Capture/focused-input IDs, hot-reload epoch |
| Arena `4` | `BloomFlexContainer64` | `O(N)` Row/Column Flexbox root container descriptor & `Wave64` tile mask |
| Arena `5..6` | `BloomTextInput64 + BloomIme64` | RFC 3629 codepoint-safe live UTF-8 `TextInput` (`39B`) & CJK/phonetic IME preedit bridge (`35B`) |
| Arena `7` | `BloomWindow64` | Cross-platform HiDPI logical/pixel dimensions, normalized Q16 pointer/drag/wheel & key events |
| Arena `8..15` | `BloomMatter64[8]` | Constitutive Matter presets ($\eta, \kappa, \mu_a$, density, `viscosity_quench_q16`, Kelvin, `128-Hex` MAC) |
| Arena `16..23` | `BloomArtStyle64[8]` | Canonical Art Bible optical styles (`TRC <= 3.0 Hz` strobe guard + colorblind geometry stroke) |
| Arena `24..39` | `BloomPrefabDesc64[16]` | Reusable 3D Prefab SSOT blueprints (`BLOOM_CSG_MASK_PACK` 2-primitive CSG + `56B` FNV-1a hash) |
| Arena `40..71` | `BloomPrefabInst64[32]` | Live 3D instances, Euler rotation, `kin_vel_packed` Q4.10 velocity, thermal Kelvin & $O(1)$ Revert |
| Arena `72..103` | `BloomCsgOp64[32]` | Data-driven 3D CSG operations & `.glb` primitives (evaluated identically on CPU & GPU) |
| Arena `104..135` | `BloomBvhNode64[32]` | Pointer-free `60-bit` 3D Morton Z-order Linear BVH hierarchy for `Slots 72..103` |
| Arena `136..143` | `BloomJointDq64[8]` | Volume-preserving `Q16.16` Dual-Quaternion skeletal joints (evaluated identically on CPU & GPU) |
| Arena `144..151` | `BloomSdfBrick64[8]` | Sparse `4×4×4` `4-bit` quantized SDF voxel bricks with trilinear sampling (CPU & GPU parity) |
| Arena `152..167` | `BloomUiNode64[16]` | Compiled React-Native Flexbox / `.svg` Liquid Glass buttons, sliders, `TextInput`, `LatexCard` & gauges |
| Arena `168..183` | `BloomGlyph64[16]` | Vector `<path>` / `<line>` SDF icon glyphs (`Q8` normalized segment coordinates) |
| Arena `184..199` | `BloomLatexBox64[16]` | Single-pass DFA compiled zero-font-atlas analytic SDF LaTeX formula boxes |
| Arena `200..231` | `uint64_t flow_states[32][8]` | `FLOW v8.0.0` 2-Jet channels: Camera (`0..3`), UI Springs (`4..19`), X-Ray (`20`), Scroll (`21`), Prefabs (`22..31`) |
| Arena `232..247` | `uint64_t flow_charts[16][8]` | `FLOW v8.0.0` Manifold Atlas (`UI_SPRING`, `UI_SCRUB`, `CAMERA_RIG`, `XRAY_SWEEP`, `800K/760K` `KanLock`) |
| Arena `248` | `BloomRaycastHit64` | Deterministic CPU `Q16.16` 3D SDF/BVH/Mesh raycast pick hit position, normal & instance ID |
| Arena `249` | `BloomImageHeader64` | `.bloom` (`"BLOMIMG7"`) zero-parse `mmap` superblock with 64-bit FNV-1a Merkle seal (`Slots != 249`) |
| Arena `250..253` | `char ui_labels[16][16]` | Inline 16-byte `'\0'`-padded ASCII widget labels for `BloomUiNode64[0..15]` (vector SDF rendered) |
| Arena `254` | `BloomSectionSpec64` | 3D Cross-Section plane/slab/corner wedge, SDF isoline pitch, DFM min-wall, inspect flags & CAE counts |
| Arena `255` | `BloomPhysicsSummary64` | Step epoch, interference/warning/CCD counts, min clearance, max penetration, `earliest_toi_q16`, peak K & contact witness |
| Mesh `0..255` | `BloomTri64[256]` | Auxiliary `@group(0) @binding(1)` 32KB `BloomMeshArena`: `.stl`/`.glb` indexed triangles, unit normal & shell thickness |
| Mesh `256..511` | `BloomBvhNode64[256]` | Auxiliary `@group(0) @binding(1)` pointer-free `60-bit` Morton Z-order Linear BVH for Jordan `+X` solid mesh SDF |
| RC `0..32767` | `vec4<f32>[32768]` | `@group(1) @binding(1)` (`[[buffer(2)]]`) 524,288B 3-level Radiance Cascades volume (C2 `8×4×8×16`, C1 `16×8×16×8`, C0 `16×8×16×6`) |
| Audio Rig | `BloomAudioRig` (`576B`) | `BloomAudioHeader64` + `BloomAudioVoice64[8]`: 4-mode `Q14/Q15/Q24` Doppler-aware modal resonator bank (`0` heap, `0` WAV) |
| Rewind Snap | `BloomSpacetimeSnap` (`16,960B`) | `64`-slot ring snapshot (`BloomEngineArena` `16,384B` + `BloomAudioRig` `576B`), indexed by `BloomRewindCtrl64` |
| Aux `64B` Cells | `BloomGameplayState64`, `BloomMetricScale64`, `BloomMoodboard64`, `BloomRewindCtrl64` | Concentric `hurt/graze/hit` shells & hitstop controller, geodesic/Doppler metric scale, RGB moodboard SSOT descriptors & rewind ring head |

---

## 3. Repository Layout (`0` Dual-Track Files, `0` External Test Files)

```text
BLOOM/
├── include/
│   ├── bloom_abi.h            # 35 × 64B cache-line structs + 16KB Arena + 32KB Mesh + 576B AudioRig + 16,960B RewindSnap ABI
│   ├── bloom_scene.h          # Pillar 1: CSG, .stl/.glb BVH, Section, DQ, Brick, 2-Jet, Metric, CineCamera, Moodboard
│   ├── bloom_ui.h             # Pillar 2 (React Native): .svg, Flexbox, SEARU_SOLVE_INLINE UI, IME, LaTeX
│   ├── bloom_prefab.h         # Pillar 3: Prefab SSOT, Swept-CCD, Gameplay Shells, 16KB Rewind, Modal DSP, FLOW v8.0.0
│   ├── bloom_host.h           # Single-track C17 SDL3 GPU (4-pipeline), WAV, CMX 3600 EDL, WebGPU HTML & Kitty TTY ABI
│   └── bloom.h                # Umbrella SDK header (BLOOM v9.0.0)
├── src/
│   ├── bloom_scene.c          # Pure C17 (-mgeneral-regs-only, -fsanitize-trap=all, 0 float, 0 heap, 0 blr)
│   ├── bloom_ui.c             # Pure C17 streamed SVG DFA, Flexbox/Bento, SEARU_SOLVE_INLINE, IME & LaTeX
│   ├── bloom_prefab.c         # Pure C17 Prefab SSOT, Swept-CCD, Gameplay Shells, Rewind, Modal DSP & FLOW v8.0.0
│   ├── bloom_lean_jsi.c       # Lean 4 AOT JSI compiler bridge + 10 SEARU suites & GPU Dispatches 0..5 verifiers
│   ├── bloom_gpu_host.c       # Pure C17 SDL3 GPU (RC C2/C1/C0 + Primary), WAV, EDL, WebGPU HTML & Kitty TTY egress
│   └── bloom_cli.c            # Standalone zero-Lean-runtime C17 Studio & CLI (--cine/--metric/--shells/--rewind/--import-mood/--export-html/--edl-out/--kitty)
├── shaders/
│   └── bloom_core.wgsl        # Sole authored WGSL compute kernel (4 entrypoints, 0 u64/i64 extensions)
├── specs/
│   └── studio_inspector.svg   # Browser-previewable W3C .svg Studio UI blueprint (optional --watch-svg)
├── tools/
│   └── sabotage_check.py      # Zero-SDL3 Mach-O ARM64 binary sabotage & ISA purity audit (176/176 = 100.0%)
├── .github/workflows/
│   └── ci.yml                 # Linux (Vulkan Lavapipe) + macOS (Metal) CI matrix pinned to FLOW v8.0.0
├── Bloom.lean                 # Pure FLOW-converged ArenaSchema (C17+WGSL) + React-Native Flexbox DSL + AOT studio.bloom export
├── Makefile                   # Single-track C++20 + C17 + Naga + Lean 4 AOT + Zero-Lean CLI + Sabotage
├── LICENSE                    # MIT License
└── VERSION                    # 9.0.0
```

---

## 4. Implementation Details & Verified Metrics

### 4.1 Declarative React-Native Flexbox DSL & `FLOW v8.0.0` SSOT Convergence ([`Bloom.lean`](Bloom.lean))
- **Zero-Local-Theorem SSOT Convergence ([`../FLOW/core/lean/Flow/`](../FLOW/core/lean/Flow/) & [`../FLOW/sdk/lean/Flow/`](../FLOW/sdk/lean/Flow/))**: All universal machine-word (`bv_decide` SAT), interval/BVH, discrete 2-Jet curvature, radiance cascade, modal damping, hysteresis, and spacetime rewind theorems now reside in domain-free `FLOW v8.0.0` modules (`0` local theorems in `Bloom.lean`).
- **Full-512b C17 + WGSL `ArenaSpec` Co-Generation (`bloomArenaSchema`)**: Declares all `26` disjoint Arena slot regions (`0..255`) and `512`-bit `CellLayout64` specs (`frame_header`, `ui_node`, `section_spec`), verified at compile time by `by decide` to emit `build/bloom_schema_abi.h` (C17/C++20 static-assert checked against [`include/bloom_abi.h`](include/bloom_abi.h)) and zero-`u64`/`i64` `build/bloom_schema.wgsl` (`naga`-checked).
- **Nested `<View>` Flexbox Tree (`UiElement` + `compileUiTree`)**: UI layouts are authored declaratively using arbitrary nested `.view (dir : FlexDir) (justify : Justify) (align : Align) (style : UiBoxStyle) (children : List UiElement)` containers alongside `.button`, `.slider`, `.textInput`, `.latexCard`, `.ringGauge`, and `.readout7Seg`. Pure layout `.view` wrappers (`drawBg == false`) consume **`0` `BloomUiNode64` slots**, while `.latexCard` measures and auto-scales formulas inside padded card bounds (`verifyLatexInCards` enforces zero horizontal/vertical overflow). `mountBlueprint` sets `"BLOOM 9.0"` and activates `FrameFlag.radianceCascades`, `FrameFlag.diffField`, `FrameFlag.subframeLock`, and `FrameFlag.cineOptics`.
- **Elaboration-Time Hardware & AOT Export Gate (`compileTimeBoundaryGate`)**: Runs `10` pure-GPR verification suites (`verifySearuCore` #1..#10), `6` CPU/GPU parity dispatches (`verifyGpuAlignment` Dispatches `0..5` covering 8-Prefab, X-Ray, UI, Diagonal 3D Section + `A ∩ B` + DQ Joint + SDF Brick + STL Mesh BVH, 3-Pass Radiance Cascades Thermal GI + CSG Blocker Occlusion, and Geodesic Metric + `2.39:1` `CineCamera` Optics + Gameplay Shells), 256-trial dual-engine SplitMix64 determinism, `0-FPS` `IDLE_SLEEP` verification, and exports `build/studio.bloom` (`16,384` bytes).

### 4.2 Standalone Zero-Lean-Runtime Pure C17 Studio, Audio & CLI (`build/bloom`, [`src/bloom_cli.c`](src/bloom_cli.c))
`build/bloom` links **only** `bloom_cli.o` + `bloom_gpu_host.o` + `libbloom.a` + `SDL3` (verified by `Makefile` via `otool -L` / `ldd` and `nm -u` to contain **`0` `leanshared` / `lean_*` symbols**):
- **Interactive 60Hz Native Studio & Live Modal Audio (`./build/bloom [model.{bloom,stl,glb}] [--watch-svg ...]`)**:
  - Load sealed `.bloom` pages or directly ingest raw binary `.stl` / `.glb` CAD meshes (normalized into unit bounds and BVH-accelerated), with real-time 48 kHz stereo `SDL_AudioStream` modal synthesis on object impacts & section scrubbing.
  - Click & drag 3D viewport to orbit, scroll wheel to zoom, click objects to raycast-select + excite constitutive modal chime, click `REVERT` to restore SSOT, or scrub `X-RAY`.
  - Click the `TextInput` bar to type UTF-8 / IME text (`Enter` runs `JADE`, `GOLD`, `GLASS`, `SAPPHIRE`, `TITANIUM`, `ALERT`/`860K`, `NORMAL`/`300K`, `REVERT`, `XRAY`, `RC`, `FIELD`, `CINE`, `LENS`, `SHELLS`, `REWIND`, `STRIKE`, `CLEAR`, plus `SECTION`, `SLAB`, `CORNER`, and `STEP`).
- **Headless CAD Cross-Section, Cinema, Relativistic Metric, Time-Rewind, HTML/EDL/Kitty & Modal WAV CLI**:
  ```sh
  ./build/bloom --headless build/studio.bloom --rc --field --cine --metric 0.22,0.35 --shells --rewind 8 --edl-out story.edl --export-html studio.html --audio-out hit.wav --out frame.ppm --steps 24
  ./build/bloom --headless build/studio.bloom --import-mood frame.ppm --kitty --steps 4
  ./build/bloom --headless model.stl --section 1,1,0,0 --inspect --out section.ppm --steps 10
  ./build/bloom --headless model.glb --slab 0.25 --inspect --out slab.ppm --steps 10
  ./build/bloom --headless build/studio.bloom --corner --inspect --out corner.ppm --steps 60
  ```

### 4.3 Pure-GPR / Zero-Float / Zero-Heap / Zero-`blr` Core (`libbloom.a`) & `176 / 176` Sabotage Gate
- **Hardware Trap & Pure-GPR ISA Purity (`libbloom.a`)**: [`src/bloom_scene.c`](src/bloom_scene.c), [`src/bloom_ui.c`](src/bloom_ui.c), and [`src/bloom_prefab.c`](src/bloom_prefab.c) compile with `-mgeneral-regs-only -fsanitize=signed-integer-overflow,shift-exponent,bounds,null,return,unreachable -fsanitize-trap=all`, audited by `Makefile` and [`tools/sabotage_check.py`](tools/sabotage_check.py) to contain **`0` FPU/SIMD instructions, `0` `blr` indirect branch-and-link calls, and `0` heap allocations** across `24,954` ARM64 instructions (`61` core functions).
- **Adversarial Mach-O Binary Sabotage (`176 / 176 = 100.0%`)**: Mutates `176` ARM64 instructions directly in `bloom_scene.o`, `bloom_ui.o`, and `bloom_prefab.o` (`datapath_addimm: 12/12`, `datapath_addsub: 19/19`, `datapath_csel: 73/73`, `datapath_mul: 12/12`, `predicate_cbz: 48/48`, `static_isa_purity: 12/12`) and achieves a **`176 / 176` (`100.0%`)** kill rate with zero static or dynamic blind spots.
- **Single-Source 4-Entrypoint `WGSL` Compute Kernel ([`shaders/bloom_core.wgsl`](shaders/bloom_core.wgsl))**: Compiles via `naga` into both Vulkan `SPIR-V` (`build/bloom_core.spv`) and Apple Metal `MSL 2.4` (`build/bloom_core.metal`), uniting 3-pass Radiance Cascades GI (`bloom_rc_cascade2`/`1`/`0`), 2-Jet `eval_diff_jet2` curvature, relativistic geodesic lensing & photon ring, `2.39:1` thin-lens CoC DoF & blue anamorphic streak, concentric Graze/Hitstop HUD, CSG/BVH/DQ-Joint/SDF-Brick/Morton-Mesh SDF, 3D cross-sections, Huygens shockwave, vector SDF ASCII HUD, and analytic LaTeX overlays.

---

## 5. Toolchain Prerequisites & Build Flow

### Prerequisites
- **Sibling Upstream**: [`FLOW`](https://github.com/hianova/FLOW) (`../FLOW` at `v8.0.0`, override via `FLOW_DIR=/path/to/FLOW`).
- **macOS (Apple Silicon / Metal)**:
  ```sh
  brew install llvm naga-cli sdl3 pkg-config
  # Plus Lean 4 (`elan` toolchain `leanprover/lean4:v4.36.0` providing `lean` and `leanchecker`) and `python3`
  ```
- **Linux (Ubuntu/Debian + Headless Vulkan Lavapipe or Hardware Vulkan)**:
  ```sh
  sudo apt-get install -y clang clang-tidy llvm lld pkg-config mesa-vulkan-drivers libvulkan-dev
  # Plus SDL3 >= 3.2 (`-DSDL_VULKAN=ON -DSDL_OFFSCREEN=ON`), `naga-cli`, and Lean 4 (`elan`)
  ```

### Step-by-Step Build & Verification Pipeline ([`Makefile`](Makefile))

```sh
# Build + 40 formal theorems + live SDL3 GPU `#eval` + `studio.bloom` + standalone CLI headless smoke test
make test

# Full 5-Gate sign-off including 176-mutant Mach-O ARM64 binary sabotage audit (100.0% kill rate)
make verify

# Launch standalone pure C17 60Hz interactive Studio (`./build/bloom build/studio.bloom`, 0 Lean runtime!)
make studio

# Render 3-pass RC GI + 2-Jet Field + 2.39:1 Cine DoF + Geodesic + Time-Rewind + EDL/HTML/WAV headlessly
./build/bloom --headless build/studio.bloom --rc --field --cine --metric 0.22,0.35 --shells --rewind 8 --edl-out studio.edl --export-html studio.html --audio-out studio_hit.wav --out studio_preview.ppm --steps 24

# Optional: Install `bin/bloom`, `share/bloom/studio.bloom`, headers, libraries, and shaders under $(PREFIX)
make install PREFIX=/usr/local
```

---

## 6. License

Released under the [MIT License](LICENSE).
