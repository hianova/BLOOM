# BLOOM v9.0.0 — Zero-Noise Radiance Cascades, 2-Jet Differential Field, Modal DSP, Concentric Shells, Time-Rewind & Geodesic Cinema Engine

**AOT Lean 4 Formal Compiler & React-Native Flexbox DSL (`0` Local Theorems / Upstream [`FLOW v9.0.0`](https://github.com/hianova/FLOW), `0 sorryAx`, `0 lean -c`, `0` External Test Files) → Sealed `16KB` `.bloom` Container (`BLM7` / `BLOMIMG7`) + Auxiliary `32KB` `BloomMeshArena` → Standalone Zero-Lean-Runtime Pure C17 Native Studio & CLI (`build/bloom`) + Pure-GPR / Zero-Float / Zero-Heap / Zero-`blr` C17 Core (`libbloom.a`) + 3-Pass Zero-Noise 3D Radiance Cascades GI (`524KB` `rc_buf` + Temporal Scene-Dirty Cache) + Warp-Scale `FIR1` GPU Witness Hunter, 32-Way Bit-Sliced Accelerator & AOT `.fir → Straight-Line WGSL` Compiler (up to `34.34 B_evals/s` on `chisholm.fir` / `192.22 B_evals/s` on `contract-loophole.fir`, Apple M1) + GPU Parallel `SEARU` Multi-Universe Solver (`8,192` universes, WalkSAT/Min-Conflicts + CLZ Lévy-Flight + Replica Exchange) + GPU Stuck-at-0/1 RTL Fault Simulator & Formal Equivalence Miter (`ELE` AES GF(256)/LUT5 `100.00%` fault coverage & exhaustive equivalence in `2.5–5.5ms`) + GPU All-Pairs `4-bit × 64-Perm` b-bit MinHash / NCD Matrix (`942.54 M_pairs/s`, `3,721` files / `6.92M` pairs in `7.34ms`) + GPU Parallel-Universe Distributed System DST (`bloom_node_dst`, `16,384` universes × `128` ticks = `2.10M` universe-ticks in `34.84ms` / `60.20 M_uticks/s`, `cpu_verified=EXACT`) + Zero-Texture 2-Jet Differential Geometry Field (`BloomFieldJet64`) + Zero-WAV 48 kHz Modal Physical-Modeling DSP (`BloomAudioRig`, `576B`) + Sub-Frame (`20.8 μs`) Swept-CCD & Huygens Shockwave + Concentric SDF Gameplay Shells / `-18dB` Hitstop Contrast (`BloomGameplayState64`) + $O(1)$ `16KB` Arena & `576B` AudioRig Circular Spacetime Rewind (`BloomSpacetimeSnap`, `16,960B`) + Relativistic Geodesic Lensing & Lorentz Doppler (`BloomMetricScale64`) + Anamorphic `2.39:1` `CineCamera` & CMX 3600 `.edl` Export + Pure-GPR Moodboard-to-SSOT Compiler (`BloomMoodboard64`) + Standalone WebGPU `.html` & Kitty TTY Egress + Symbiotic `SEARU × FLOW 2-Jet` + C++20 `64B` ABI Gate (`35` Cells) + 10-Entrypoint `WGSL` Compute Shader (`naga` → `SPIR-V` + `MSL 2.4`) & `36 / 36` (`100.0%`) `FLOW v9.0.0` Shared C17 Source-Level Sabotage Audit.**

---

## 1. Architectural Positioning: Post-Texture Sensory Engine, Time-Rewind Gameplay & AOT `.bloom` Decoupling

Just as [`FLOW v9.0.0`](https://github.com/hianova/FLOW) uses `Flow.lean` strictly as an AOT `.flow` formal compiler and `build/flow` as a pure C runtime with self-synthesized [`core/flow_opt_params.h`](https://github.com/hianova/FLOW/blob/main/core/flow_opt_params.h) constants, `BLOOM v9.0.0` pairs formal AOT verification with a full zero-texture differential-geometry & geodesic-lens renderer, zero-noise 3D Radiance Cascades GI with Temporal Scene-Dirty Caching, a Warp-Scale GPU `FIR1` Parallel Witness Hunter, pure-GPR Doppler modal acoustic synthesis, concentric gameplay shells with $O(1)$ spacetime time-rewind, and a pure-GPR moodboard-to-SSOT compiler — verified end-to-end by `make verify` with **zero authored `.metal` / `.m` / `.mm` / `.swift` files**, **zero `lean -c` codegen**, **zero Lean runtime (`0 leanshared`) in `build/bloom`**, **zero external texture atlases**, **zero prerecorded `.wav` assets**, **zero fake software fallbacks**, and **zero external `.c` / `.lean` test files**:

```text
  ┌────────────────────────────────────────────────────────────────────────────┐
  │ 1. AOT Formal Verification & React-Native Flexbox Compiler (Bloom.lean)    │
  │    - Zero local theorems (`FLOW` SSOT): imports verified `FLOW v9.0.0`     │
  │      + `by decide` full-512b C17/WGSL Schema layout disjointness proofs   │
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
  │      • `--emit-fir` / `--hunt-fir` / `--bitslice` / `--compile-fir-wgsl` /   │
  │        `--aot-shader` / `--faultsim-fir` / `--equiv-fir` + `--verify-cpu`  │
  │      • `--searu-cnf` / `--searu-bench-3sat` / `--searu-bench-color`        │
  │      • `--gpu-ncd <dirs>` (all-pairs MinHash/NCD clone & drift radar)      │
  │      • `--node-dst --universes N --ticks T [--buggy]` (GPU Raft DST)     │
  │    - `libbloom.a` (`-mgeneral-regs-only -fsanitize-trap=all`, 0 float/heap,│
  │      `0` `blr` indirect calls, `clang-tidy` 0 warnings across 6 C files):  │
  │      • Pillar 1 (`bloom_scene.c`): CSG, `.stl`/`.glb` BVH, DQ skeleton,    │
  │        4×4×4 bricks, 2-Jet field, Section/Slab, Relativistic Metric scale, │
  │        Thin-lens `CineCamera`, Pure-GPR Moodboard-to-SSOT, 8 Matters       │
  │      • Pillar 2 (`bloom_ui.c`): Streamed `.svg` DFA, Flexbox, Auto-Bento,  │
  │        `SEARU_SOLVE_INLINE` glass-contrast solver, Pointer/IME, DFA LaTeX  │
  │      • Pillar 3 (`bloom_prefab.c`): Prefab SSOT, O(1) Revert, Swept-CCD    │
  │        Rebound, Concentric Shells + 6-Frame Buffer + Fulcrum + -18dB Hit,  │
  │        16KB+576B Circular Rewind, Doppler Modal DSP, `FLOW v9.0.0` IR/2-Jet│
  │    - Shared C17 Sabotage (`tools/sabotage_check.py`): 36 / 36 (100.0%)     │
  └─────────────────────────────────────┬──────────────────────────────────────┘
                                        │ `SDL3 GPU`: `@group(0) b(0)` 16KB Arena
                                        │ + `b(1)` 32KB Mesh + `@group(1) b(1)` 524KB RC
  ┌─────────────────────────────────────▼──────────────────────────────────────┐
  │ 3. 10-Entrypoint GPU Compute, 48kHz Audio, HTML & TTY Host (WGSL + C17)    │
  │    - `shaders/bloom_core.wgsl`: Sole authored shader SSOT (`0` u64/i64)    │
  │      • Pass 1..3: `bloom_rc_cascade2` (8×4×8×16) -> `bloom_rc_cascade1`    │
  │        (16×8×16×8) -> `bloom_rc_cascade0` (16×8×16×6) octahedral 3D GI     │
  │        (skipped via Temporal Scene-Dirty Cache on unchanged static/orbit)  │
  │      • Pass 4: `bloom_render_frame` (`eval_diff_jet2`, Geodesic/Einstein   │
  │        lensing + Lorentz aberration, RC + Huygens wave, 2.39:1 Cine DoF/  │
  │        anamorphic streaks, concentric Graze/Hitstop HUD & LaTeX overlay)   │
  │      • Entrypoint 5: `bloom_fir1_hunt` (Zero-workgroup register-stride     │
  │        32,768-thread GPU `FIR1` evaluator & witness hunter, 18 opcodes)    │
  │      • Entrypoint 6: `bloom_fir1_bitslice` (32-way SIMT bit-sliced netlist)│
  │      • Entrypoint 7: `bloom_searu_solve` (8,192-universe Parallel Tempering│
  │        WalkSAT/Min-Conflicts + CLZ Levy-Flight + Replica Exchange solver)  │
  │      • Entrypoint 8: `bloom_fir1_faultsim` (RTL Stuck-at-0/1 ATPG sim)     │
  │      • Entrypoint 9: `bloom_info_sim_matrix` (SWAR 64-perm all-pairs NCD)  │
  │      • Entrypoint 10: `bloom_node_dst` (Chunked 16K+ universe 5-node Raft  │
  │        crash/partition DST, safe vs `--buggy` volatile-vote split brain)   │
  │      ├─ `naga` build-time -> `build/bloom_core.spv`   (Vulkan SPIR-V)      │
  │      └─ `naga` build-time -> `build/bloom_core.metal` (Metal MSL 2.4)      │
  │    - `src/bloom_gpu_host.c`: 10-pipeline `SDL3 GPU` + AOT `.fir -> WGSL`   │
  │      compiler + Formal Equiv Miter + `SDL_AudioStream`/WAV, EDL, HTML, TTY │
  └────────────────────────────────────────────────────────────────────────────┘
```

### Physical Memory, Sensory Breakthroughs & v9.0.0 Recovered Engines
- **Zero-Parse `16,384B` `.bloom` Arena + `32,768B` `BloomMeshArena` + `524,288B` `rc_buf`**: [`Bloom.lean`](Bloom.lean) compiles the nested React-Native Flexbox UI tree, analytic SDF LaTeX cards, 3D Prefab instances, section/physics descriptors (`Slots 254..255`), and 16 `FLOW v9.0.0` Manifold Charts into a single `16,384-byte` (`256 × 64B`) `BloomEngineArena`, seals `Slot 249` (`BloomImageHeader64`, `"BLOMIMG7"`) with a 64-bit FNV-1a Merkle root over all non-superblock slots (`Slots != 249`), and writes `build/studio.bloom`.
- **3-Pass Zero-Noise 3D Radiance Cascades GI + Temporal Scene-Dirty Cache**: [`shaders/bloom_core.wgsl`](shaders/bloom_core.wgsl) dispatches a hierarchical 3D octahedral probe cascade into `@group(1) @binding(1) rc_buf` (`32,768 × vec4<f32>` = `524,288B` / Metal `[[buffer(2)]]`) across `bloom_rc_cascade2` (`8×4×8 × 16`), `bloom_rc_cascade1` (`16×8×16 × 8`), and `bloom_rc_cascade0` (`16×8×16 × 6`), integrating indirect bounce and specular GI with zero Monte-Carlo temporal noise. [`bloom_gpu_hash_rc_scene`](src/bloom_gpu_host.c) hashes scene geometry/material/CSG/mesh-epoch state (`Slots 8..151`, `254..255`, `hdr`), allowing `bloom_gpu_render` to skip 3/4 compute passes (`bloom_gpu_rc_cache_hits`) on camera-orbit or unchanged idle frames while preserving full invalidation on `force_render` or `bloom_gpu_upload_mesh` / `bloom_gpu_hunt_fir1`.
- **Universal Warp-Scale GPU `FIR1` Hunter, 32-Way Bit-Sliced SIMT Engine & AOT `.fir → Straight-Line WGSL` Compiler ([`bloom_fir1_hunt`](shaders/bloom_core.wgsl), [`bloom_fir1_bitslice`](shaders/bloom_core.wgsl), [`bloom_fir1_compile_wgsl`](src/bloom_gpu_host.c))**: Evaluates arbitrary `FLOW` `"FIR1"` boolean/arithmetic circuits across up to `32,768` concurrent GPU threads via zero-`var<workgroup>`, zero-`u64`/`i64` execution, plus C17 bit-blasting (`ADD`/`SUB`/`SHL`/`SHR`/`LUT`/`CTRL_SEL`/bitwise with constant folding & DCE) into either a runtime 32-way bit-sliced netlist (`--bitslice`) or a zero-branch straight-line SSA WGSL kernel (`--compile-fir-wgsl` + `--aot-shader`), cross-checked bit-for-bit on CPU via `flow_ir_eval_u64` (`--verify-cpu`). Measured across **`1,073,741,824` (`1.07B`) trials** on Apple M1 (`SDL3-GPU/metal`):
  - `SOUND` `chisholm.fir` (`133` gates, `130` input bits, `64`-bit output): interpreter `102.07 M_evals/s` (`16.78M` in `164.37ms`) → 32-way `--bitslice` **`755.57 M_evals/s`** (`1,421.10ms`) → **AOT Straight-Line WGSL `34,335.57 M_evals/s` (`34.34 B_evals/s`, `31.27ms` for `1.07B` trials — `336×` speedup)**, `cpu_parity=EXACT`.
  - `SOUND` `contract-loophole.fir` (`2` gates, `2` input bits): `--bitslice` **`22,864.06 M_evals/s`** (`46.96ms`) → **AOT WGSL `192,220.16 M_evals/s` (`192.22 B_evals/s`, `5.59ms`)**, `cpu_parity=EXACT`.
  - `bloom` certified `.fir` (`8` input bits, `16`-bit output): `--bitslice` **`3,865.65 M_evals/s`** (`277.76ms`) → **AOT WGSL `118,974.16 M_evals/s` (`118.97 B_evals/s`, `9.03ms`)**, `cpu_parity=EXACT`.
  - Odd-trial non-zero 64-bit XOR verification (`12,345` trials on `chisholm.fir`, `seed=0xCAFEBABE12345678`): `bitslice` and `aot-wgsl` match CPU `xor=0xcafebabe12346640`, `cpu_parity=EXACT`.
- **GPU Parallel `SEARU` Multi-Universe Combinatorial Solver ([`bloom_searu_solve`](shaders/bloom_core.wgsl), [`bloom_gpu_searu_solve_3sat`](src/bloom_gpu_host.c), [`bloom_gpu_searu_solve_graph_color`](src/bloom_gpu_host.c))**: Executes up to `8,192` Parallel Tempering universes simultaneously with 1-pass WalkSAT/Min-Conflicts, CLZ Lévy-Flight Black Swan jumps, Kan-pin locking, and cross-round top-16 elite replica exchange, with **zero-trust Pure-GPR CPU certification** via `FLOW`'s `searu_eval_3sat` / `searu_eval_graph_color`. Measured on Apple M1 (`SDL3-GPU/metal`):
  - Planted dense 4-coloring (`64` nodes, `4` colors, `1,036` edges, Kan-pinned): single-core CPU `searu_solve_graph_color` (`20,000` iters) stalled at **`best_e=49 (UNSOLVED)`**; GPU `bloom_searu_solve` (`4,096` universes, Round 1) found **`best_e=0 (SOLVED)` in `320.34ms`** (`3.04 M_steps/s`, `cpu_verified=EXACT`).
  - Critical-ratio 3-SAT (`250` vars, `1,065` clauses, ratio `4.26`, Kan-pinned): GPU `4,096` universes solved in Round 1 (`best_e=0, 262.65ms, 3.99 M_steps/s, cpu_verified=EXACT`; up to **`7.31 M_steps/s`** on `100` vars / `426` clauses vs CPU `328–642 k_steps/s`).
- **GPU RTL Stuck-at-0/1 Fault Simulator & Formal Hardware Equivalence Miter ([`bloom_fir1_faultsim`](shaders/bloom_core.wgsl), [`bloom_gpu_faultsim_fir1`](src/bloom_gpu_host.c), [`bloom_gpu_equiv_fir1`](src/bloom_gpu_host.c))**: Simulates stuck-at-0/1 faults across every non-constant wire in parallel on 32 vectors/lane, and fuses `(spec.fir, impl.fir)` into a constant-folded XOR-OR miter for exhaustive/bounded equivalence verification with zero-trust CPU ATPG vector certification. Verified on real [`ELE`](../ELE) hardware & [`SOUND`](../SOUND) circuits (Apple M1 `SDL3-GPU/metal`):
  - `ELE` AES S-Box equivalence `gf256_tower_a32.fir` vs `gf256_tower_d4.fir` (`332` miter gates, `256` exhaustive trials): **`EQUIVALENT` in `2.69ms`** (`cpu_verified=EXACT`); `lut5_bijection_spec.fir` (`32`-entry `LUT_ROM`) vs `lut5_bijection_opt.fir` (`97` miter gates, `32` exhaustive trials): **`EQUIVALENT` in `2.54ms`** (`cpu_verified=EXACT`).
  - `ELE` Stuck-at-0/1 coverage: `gf256_tower_a32.fir` (`334` faults, `85,504` fault-evals) **`334/334 = 100.00%`** in `5.51ms` (`15.52 M_fevals/s`); `gf256_tower_d4.fir` **`332/332 = 100.00%`** in `5.28ms`; `lut5_bijection_opt.fir` **`72/72 = 100.00%`** in `2.21ms`; `SOUND` `chisholm.fir` (`65,536` vectors × `126` faults = `8.26M` fault-evals) **`46.64 M_fevals/s`** (`177.04ms`), all **`cpu_verified=EXACT`**.
- **GPU All-Pairs `4-Bit × 64-Perm` b-bit MinHash / NCD Similarity Matrix ([`bloom_info_sim_matrix`](shaders/bloom_core.wgsl), [`bloom_gpu_info_sim_matrix`](src/bloom_gpu_host.c))**: Sketches each file via 5-byte OPH + Shrivastava densification into `256`-bit (`64 × 4-bit` uniform nibble) signatures and evaluates all $N(N-1)/2$ pairs via SWAR nibble equality (`--gpu-ncd`) in one GPU dispatch (`<= 8,192` files). Measured on Apple M1:
  - **10-repo full workspace scan (`3,721` files / `6,921,060` pairs)**: **`7.34ms` (`942.54 M_pairs/s`)**, `cpu_verified=EXACT` (surfacing `168` vendored exact duplicates in `.venv`).
  - **Authored core source scan across 10 repos (`161` files / `12,880` pairs)**: **`0.54ms` (`23.68 M_pairs/s`)**, **`0` clones above `36/64`** (`near_dup_files=0, exact_dup_pairs=0`), `cpu_verified=EXACT`.
- **GPU Parallel-Universe Distributed System DST ([`bloom_node_dst`](shaders/bloom_core.wgsl), [`bloom_gpu_node_dst`](src/bloom_gpu_host.c))**: Executes up to `65,536` independent 5-node Raft/BFT cluster universes across chunked GPU batches under deterministic crash/recovery + network partition faults (`--node-dst --universes N --ticks T --fault-prob P [--buggy]`), cross-checked bit-for-bit against C17 CPU simulation (`cpu_verified=EXACT`). Measured on Apple M1 (`16,384` universes × `128` ticks = `2,097,152` universe-ticks, `fault_ppm=150000`):
  - **Safe persistent `votedFor` cluster**: `0 / 16,384` violations (`1,441,056` commits, `1,513,650` elections) in **`34.84ms` (`60.20 M_uticks/s`)**, `cpu_verified=EXACT`.
  - **Buggy volatile-vote fault injection (`--buggy`)**: **`3,529 / 16,384` split-brain counterexamples** (`first_cex=0`, `1,436,270` commits) in **`32.93ms` (`63.68 M_uticks/s`)**, `cpu_verified=EXACT`.
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
| Arena `200..231` | `uint64_t flow_states[32][8]` | `FLOW v9.0.0` 2-Jet channels: Camera (`0..3`), UI Springs (`4..19`), X-Ray (`20`), Scroll (`21`), Prefabs (`22..31`) |
| Arena `232..247` | `uint64_t flow_charts[16][8]` | `FLOW v9.0.0` Manifold Atlas (`UI_SPRING`, `UI_SCRUB`, `CAMERA_RIG`, `XRAY_SWEEP`, `800K/760K` `KanLock`) |
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
│   ├── bloom_prefab.h         # Pillar 3: Prefab SSOT, Swept-CCD, Gameplay Shells, 16KB Rewind, Modal DSP, FLOW v9.0.0
│   ├── bloom_host.h           # Single-track C17 SDL3 GPU (9-pipeline + AOT FIR1 + SEARU + FaultSim/Equiv + NCD), WAV, EDL, HTML & TTY ABI
│   └── bloom.h                # Umbrella SDK header (BLOOM v9.0.0)
├── src/
│   ├── bloom_scene.c          # Pure C17 (-mgeneral-regs-only, -fsanitize-trap=all, 0 float, 0 heap, 0 blr)
│   ├── bloom_ui.c             # Pure C17 streamed SVG DFA, Flexbox/Bento, SEARU_SOLVE_INLINE, IME & LaTeX
│   ├── bloom_prefab.c         # Pure C17 Prefab SSOT, Swept-CCD, Gameplay Shells, Rewind, Modal DSP & FLOW v9.0.0 IR/2-Jet
│   ├── bloom_lean_jsi.c       # Lean 4 AOT JSI compiler bridge + 10 SEARU suites & GPU Dispatches 0..5 verifiers
│   ├── bloom_gpu_host.c       # Pure C17 SDL3 GPU (RC C2/C1/C0 + Dirty Cache + Primary + FIR1 + BitSlice/AOT + SEARU + FaultSim/Equiv + NCD)
│   └── bloom_cli.c            # Standalone zero-Lean-runtime C17 Studio & CLI (--hunt-fir/--faultsim-fir/--equiv-fir/--searu-*/--gpu-ncd)
├── shaders/
│   └── bloom_core.wgsl        # Sole authored WGSL compute kernel (9 entrypoints, 0 u64/i64 extensions, 0 workgroup globals)
├── specs/
│   └── studio_inspector.svg   # Browser-previewable W3C .svg Studio UI blueprint (optional --watch-svg)
├── tools/
│   └── sabotage_check.py      # FLOW v9.0.0 shared C17 source-level sabotage & ISA purity audit (36/36 = 100.0%)
├── .github/workflows/
│   └── ci.yml                 # Linux (Vulkan Lavapipe) + macOS (Metal) CI matrix tracking FLOW main
├── Bloom.lean                 # Pure FLOW-converged ArenaSchema (C17+WGSL) + React-Native Flexbox DSL + AOT studio.bloom export
├── Makefile                   # Single-track C++20 + C17 + Naga + Lean 4 AOT + Zero-Lean CLI + Sabotage
├── LICENSE                    # MIT License
└── VERSION                    # 9.0.0
```

---

## 4. Implementation Details & Verified Metrics

### 4.1 Declarative React-Native Flexbox DSL & `FLOW v9.0.0` SSOT Convergence ([`Bloom.lean`](Bloom.lean))
- **Zero-Local-Theorem SSOT Convergence ([`../FLOW/core/lean/Flow/`](../FLOW/core/lean/Flow/) & [`../FLOW/sdk/lean/Flow/`](../FLOW/sdk/lean/Flow/))**: All universal machine-word (`bv_decide` SAT), interval/BVH, discrete 2-Jet curvature, radiance cascade, modal damping, hysteresis, and spacetime rewind theorems reside in domain-free `FLOW v9.0.0` modules (`0` local theorems in `Bloom.lean`).
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
  ./build/bloom --headless build/studio.bloom --emit-fir studio.fir --steps 30
  ./build/bloom --compile-fir-wgsl studio.fir studio_aot.wgsl
  naga studio_aot.wgsl studio_aot.spv && naga --metal-version 2.4 studio_aot.wgsl studio_aot.metal
  ./build/bloom --hunt-fir studio.fir --aot-shader studio_aot.wgsl --trials 1073741824 --hunt-mode neq --hunt-target 0
  ./build/bloom --searu-bench-3sat 250,1065 --universes 4096 --searu-iters 256 --searu-rounds 4 --verify-cpu
  ./build/bloom --searu-bench-color 64,4 --universes 4096 --searu-iters 256 --searu-rounds 4 --verify-cpu
  ./build/bloom --equiv-fir ../ELE/build/gf256_tower_a32.fir ../ELE/build/gf256_tower_d4.fir --verify-cpu
  ./build/bloom --faultsim-fir ../ELE/build/gf256_tower_a32.fir --trials 256 --verify-cpu
  ./build/bloom --gpu-ncd src,include,../FLOW/core,../FLOW/sdk --top 10 --verify-cpu
  ```

### 4.3 Pure-GPR / Zero-Float / Zero-Heap / Zero-`blr` Core (`libbloom.a`) & `36 / 36` Sabotage Gate
- **Hardware Trap & Pure-GPR ISA Purity (`libbloom.a`)**: [`src/bloom_scene.c`](src/bloom_scene.c), [`src/bloom_ui.c`](src/bloom_ui.c), and [`src/bloom_prefab.c`](src/bloom_prefab.c) compile with `-mgeneral-regs-only -fsanitize=signed-integer-overflow,shift-exponent,bounds,null,return,unreachable -fsanitize-trap=all`, audited by `Makefile` and [`tools/sabotage_check.py`](tools/sabotage_check.py) to contain **`0` FPU/SIMD instructions, `0` `blr` indirect branch-and-link calls, and `0` heap allocations**.
- **Shared `FLOW v9.0.0` C17 Source-Level Sabotage (`36 / 36 = 100.0%`)**: Powered by `FLOW/sdk/tools/flow_sabotage_runner.py`, executes `36` single-fault source mutations across `bloom_scene.c` (`12/12`: `scene_algebra`, `scene_mesh_bvh`, `scene_cholesky_sdd`, `scene_metric_mood`), `bloom_ui.c` (`12/12`: `ui_flex_layout`, `ui_glass_searu`, `ui_latex_sdf`, `ui_utf8_ime`), and `bloom_prefab.c` (`12/12`: `prefab_flow_v9_sdk`, `prefab_ssot_trc`, `prefab_ccd_dsp`, `prefab_game_rewind`), achieving **`36 / 36` (`100.0%`)** kill rate with zero SDL3/GPU overhead.
- **Single-Source 9-Entrypoint `WGSL` Compute Kernel ([`shaders/bloom_core.wgsl`](shaders/bloom_core.wgsl))**: Compiles via `naga` into both Vulkan `SPIR-V` (`build/bloom_core.spv`) and Apple Metal `MSL 2.4` (`build/bloom_core.metal`) with **zero** `u64`/`i64` tokens and **zero** `var<workgroup>` globals, uniting 3-pass Radiance Cascades GI (`bloom_rc_cascade2`/`1`/`0`), 2-Jet `eval_diff_jet2` curvature, relativistic geodesic lensing & photon ring, `2.39:1` thin-lens CoC DoF & blue anamorphic streak, concentric Graze/Hitstop HUD, CSG/BVH/DQ-Joint/SDF-Brick/Morton-Mesh SDF, 3D cross-sections, Huygens shockwave, vector SDF ASCII HUD, analytic LaTeX overlays, the 18-opcode Warp-Scale `"FIR1"` Witness Hunter (`bloom_fir1_hunt`), the 32-way SIMT Bit-Sliced Netlist Accelerator (`bloom_fir1_bitslice`), the `8,192`-universe Parallel Tempering `SEARU` Solver (`bloom_searu_solve`), the RTL Stuck-at-0/1 Fault Simulator (`bloom_fir1_faultsim`), and the All-Pairs MinHash / NCD Similarity Matrix (`bloom_info_sim_matrix`).

---

## 5. Toolchain Prerequisites & Build Flow

### Prerequisites
- **Sibling Upstream**: [`FLOW`](https://github.com/hianova/FLOW) (`../FLOW` at `v9.0.0`, override via `FLOW_DIR=/path/to/FLOW`).
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
# Build + upstream FLOW v9.0.0 + live SDL3 GPU `#eval` + `studio.bloom` + CLI smoke & GPU-vs-CPU FIR1 test
make test

# Full sign-off including 36-mutant FLOW v9.0.0 shared C17 source-level sabotage audit (100.0% kill rate)
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
