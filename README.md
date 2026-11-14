# BLOOM v7.0.1 — 64-Byte `Three.js` + `React Native` + `Prefab SSOT` Engine

**AOT Lean 4 Formal Compiler & React-Native Flexbox DSL (`22` Theorems, `0 sorryAx`, `0 lean -c`, `0` External Test Files) → Sealed `16KB` `.bloom` Container (`BLM7` / `BLOMIMG7`) → Standalone Zero-Lean-Runtime Pure C17 Native Studio & CLI (`build/bloom`) + Pure-GPR / Zero-Float / Zero-Heap / Zero-`blr` C17 Core (`libbloom.a`) + Symbiotic `SEARU × FLOW 2-Jet` Self-Healing & Zero-`blr` Inline UI Solver + C++20 `64B` Alignment ABI Gate + Single-Source Cross-Platform `WGSL` Compute Shader (`naga` → `SPIR-V` + `MSL 2.4`) & `SDL3 GPU` Host + `100 / 100` (`100.0%`) Mach-O Binary Sabotage Audit atop [`FLOW v7.0.1`](https://github.com/hianova/FLOW).**

---

## 1. Architectural Positioning: AOT `.bloom` Decoupling & Symbiotic `FLOW v7.0.1` Guarantee

Just as [`FLOW v7.0.1`](https://github.com/hianova/FLOW) (silicon-verified alongside [`ELE`](https://github.com/hianova/ELE)) uses `Flow.lean` strictly as an AOT `.flow` formal compiler and `build/flow` as a pure C runtime with self-synthesized [`flow_opt_params.h`](https://github.com/hianova/FLOW/blob/v7.0.1/flow_opt_params.h) constants, `BLOOM v7.0.1` decouples AOT formal authoring from native execution — verified end-to-end by `make verify` with **zero authored `.metal` / `.m` / `.mm` / `.swift` files**, **zero `lean -c` codegen**, **zero Lean runtime (`0 leanshared`) in `build/bloom`**, **zero hand-tuned solver heuristics**, **zero fake software gradient fallbacks**, and **zero external `.c` / `.lean` test files**:

```text
  ┌────────────────────────────────────────────────────────────────────────────┐
  │ 1. AOT Formal Verification & React-Native Flexbox Compiler (Bloom.lean)    │
  │    - 22 theorems (11 arithmetic/set + 11 CaDiCaL SAT bv_decide, 0 sorryAx) │
  │    - Declarative nested `<View>` Flexbox DSL (`UiElement` -> `compileUiTree│
  │      -> `0`-slot layout wrappers, inline 16B labels & auto-fit `LatexCard`)│
  │    - Elaboration-time `#eval compileTimeBoundaryGate` (`0` external tests):│
  │      • `verifySearuCore`: `searu.h` CLZ Zipf + `searu_heal_and_swap` +     │
  │        `SEARU_SOLVE_INLINE` + Ring-0 / Petersen 3-Color / 3-SAT torture    │
  │      • `verifyGpuAlignment`: 1-frame 8-Prefab + X-Ray + UI CPU/GPU parity  │
  │      • Exports Merkle-sealed `16,384B` `build/studio.bloom` VM-page image  │
  │    - Standalone kernel replay via `leanchecker`                            │
  └─────────────────────────────────────┬──────────────────────────────────────┘
                                        │ Sealed `16,384B` (`256 × 64B`) `.bloom`
                                        │ (`BLOMIMG7` FNV-1a Merkle Superblock)
  ┌─────────────────────────────────────▼──────────────────────────────────────┐
  │ 2. Zero-Lean-Runtime Pure C17 Native Studio & Core (`bloom` + `libbloom.a`)│
  │    - `src/bloom_cli.c` -> `build/bloom` (`0` `<lean/lean.h>`, `0 leanshared│
  │      audited via `otool -L` / `ldd` & `nm -u`):                            │
  │      • 60Hz Interactive Studio: 3D raycast pick, Orbit rig, 2-Jet springs, │
  │        live UTF-8/IME `TextInput` command bar & 15-frame `stat()` hot-load │
  │      • Headless CLI (`--headless --out frame.ppm --steps N`): GPU settle   │
  │    - `libbloom.a` (`-mgeneral-regs-only -fsanitize-trap=all`, 0 float/heap,│
  │      `0` `blr` indirect calls across all `16,149` A64 instructions):       │
  │      • Pillar 1 (`bloom_scene.c`): CSG, `.glb`, Morton BVH (`0-ULP`), DQ,  │
  │        4×4×4 4-bit SDF bricks, 8 Matters (`128-Hex`), CPU 3D Raycaster     │
  │      • Pillar 2 (`bloom_ui.c`): Streamed `.svg` DFA, Flexbox, Auto-Bento,  │
  │        `SEARU_SOLVE_INLINE` glass-contrast solver, Pointer/IME, DFA LaTeX  │
  │      • Pillar 3 (`bloom_prefab.c`): Prefab SSOT, O(1) Revert, X-Ray cut,   │
  │        TRC `<= 3.0 Hz` guard, `FLOW v7.0.1` 2-Jet + `searu_heal_and_swap`  │
  │    - Binary Sabotage (`tools/sabotage_check.py`): 100 / 100 (100.0%) killed│
  └─────────────────────────────────────┬──────────────────────────────────────┘
                                        │ `SDL3 GPU` 16KB Storage Buffer Upload
                                        │ (`0` bytes & `0` dispatches when idle)
  ┌─────────────────────────────────────▼──────────────────────────────────────┐
  │ 3. Single-Source GPU Compute & Window Layer (WGSL + Pure C17 SDL3 GPU)     │
  │    - `shaders/bloom_core.wgsl`: Sole authored shader SSOT (`0` u64/i64)    │
  │      • Refractive SDF optics, `STUDIO_GI` shadows/planar reflections,      │
  │        crisp vector SDF ASCII typography, `TextInput`/IME & LaTeX overlay  │
  │      ├─ `naga` build-time -> `build/bloom_core.spv`   (Vulkan SPIR-V)      │
  │      └─ `naga` build-time -> `build/bloom_core.metal` (Metal MSL 2.4)      │
  │    - `src/bloom_gpu_host.c`: Pure C17 `SDL3 GPU` compute + HiDPI/IME host  │
  └────────────────────────────────────────────────────────────────────────────┘
```

### Physical Memory, Symbiotic `SEARU × FLOW 2-Jet` Healing & `0-FPS` Energy Model
- **Zero-Parse `16,384-Byte` `.bloom` VM-Page Container**: [`Bloom.lean`](Bloom.lean) compiles the nested React-Native Flexbox UI tree, analytic SDF LaTeX cards, 3D Prefab instances, and 16 `FLOW v7.0.1` Manifold Charts into a single `16,384-byte` (`256 × 64B`) `BloomEngineArena`, seals `Slot 249` (`BloomImageHeader64`, `"BLOMIMG7"`) with a 64-bit FNV-1a Merkle root over `Slots 0..248`, and writes `build/studio.bloom`. [`src/bloom_cli.c`](src/bloom_cli.c) loads the exact 16KB page into `posix_memalign(16384)` memory, verifies `bloom_image_verify_arena()`, and polls `stat()` `st_mtime` every 15 frames for zero-downtime live hot-reload while preserving live camera orbit and 3D selection.
- **Phase-Gated Orthogonal Self-Healing (`bloom_arena_heal_and_swap`) & Zero-`blr` UI Contrast (`bloom_ui_solve_glass_tracks64`)**:
  - [`bloom_arena_heal_and_swap`](src/bloom_prefab.c#L504-L534) wraps `FLOW v7.0.1`'s `searu_heal_and_swap` (`searu.h` + `flow_opt_params.h`): it executes an $O(1)$ zero-chaos no-op whenever a `FLOW` 2-Jet channel is converged (`FLOW_STATUS_CONVERGED`) or actively descending (`dt_resolved != 0`), waking coupled `SEARU × FLOW 2-Jet` bilevel synthesis **only** when trapped at a topological/discriminant barrier (`!CONVERGED && dt_resolved == 0`) while preserving `KanLock` bit-pins and atomically publishing the healed chart via lock-free `@swap` (`flow_arena_try_swap`).
  - [`bloom_ui_solve_glass_tracks64`](src/bloom_ui.c#L824-L871) expands `FLOW v7.0.1`'s variadic `SEARU_SOLVE_INLINE` macro directly inline (**`0` function pointers, `0` `blr` instructions**) to assign non-colliding 2-bit Liquid Glass refractive contrast modes (`CLEAR`, `FROSTED`, `SMOKED`, `IRIDESCENT`) across adjacent `BloomUiNode64` widgets under `KanLock` 2-bit pins.
- **Cross-Platform `SDL3 GPU` Dispatch & `0-FPS` Idle Short-Circuit**: [`src/bloom_gpu_host.c`](src/bloom_gpu_host.c) drives `SDL3 GPU` (`SDL_GPUCopyPass` uploading `16,384` bytes into `@group(0) @binding(0)` read-only storage buffer + `SDL_GPUComputePass` dispatching `@workgroup_size(8, 8, 1)` into `@group(1) @binding(0)` `RGBA8_UNORM` storage texture). Once all 32 `FLOW v7.0.1` channels reach Lyapunov convergence (`FLOW_STATUS_CONVERGED`), `BLOOM_FLAG_IDLE_SLEEP` is asserted and `bloom_gpu_render()` returns immediately at function entry (`idle_skip_count++`) — performing **`0` bytes of transfer upload and `0` GPU compute dispatches** while stationary.

---

## 2. Unified `16,384-Byte` (`256 × 64B`) Arena Slot Map

Every runtime, scene, UI, dynamical, and `.bloom` container cell is strictly **`64 bytes` (`16 × u32` / `8 × u64`)** aligned to `64` bytes (`BLOOM_STATIC_ASSERT64`), addressed via $\mathbb{Z}/256\mathbb{Z}$ OOB-immune masked indexing (`idx & 0xFFu`):

| Arena Slots | Cell Type ([`include/bloom_abi.h`](include/bloom_abi.h)) | Responsibility |
| :--- | :--- | :--- |
| `0` | `BloomFrameHeader64` | Magic `"BLM7"`, resolution, counts, X-Ray plane, selection, `IDLE_SLEEP` / `TRC_SAFE` / `STUDIO_GI` flags |
| `1` | `BloomCamera64` | Physical Thin-Lens (`focal_mm`, `aperture_fstop`, `focus_dist`) & 2-Jet smoothed Orbit Rig |
| `2` | `BloomRegBank64` | Zero-VDOM `12`-channel reactive `Q16.16` register bank + `56B` FNV-1a MAC |
| `3` | `BloomDomState64` | `Wave64` (`u32x2`) DOM mask, hover/active/Pointer-Capture/focused-input IDs, hot-reload epoch |
| `4` | `BloomFlexContainer64` | `O(N)` Row/Column Flexbox root container descriptor & `Wave64` tile mask |
| `5..6` | `BloomTextInput64 + BloomIme64` | RFC 3629 codepoint-safe live UTF-8 `TextInput` (`39B`) & CJK/phonetic IME preedit bridge (`35B`) |
| `7` | `BloomWindow64` | Cross-platform HiDPI logical/pixel dimensions, normalized Q16 pointer/drag/wheel & key events |
| `8..15` | `BloomMatter64[8]` | Constitutive Matter presets ($\eta, \kappa, \mu_a$, Mie scatter, Kelvin, `128-Hex` MAC wire codec) |
| `16..23` | `BloomArtStyle64[8]` | Canonical Art Bible optical styles (`TRC <= 3.0 Hz` strobe guard + colorblind geometry stroke) |
| `24..39` | `BloomPrefabDesc64[16]` | Reusable 3D Prefab SSOT blueprints (`BLOOM_CSG_MASK_PACK` 2-primitive CSG + `56B` FNV-1a hash) |
| `40..71` | `BloomPrefabInst64[32]` | Live 3D scene instances, non-destructive style/matter/param overrides & $O(1)$ `Revert to SSOT` |
| `72..103` | `BloomCsgOp64[32]` | Data-driven 3D CSG operations & binary glTF 2.0 (`.glb`) ingested primitives |
| `104..135` | `BloomBvhNode64[32]` | Pointer-free `60-bit` 3D Morton Z-order Linear BVH hierarchy (`0-ULP` parity with brute-force CSG) |
| `136..143` | `BloomJointDq64[8]` | Volume-preserving `Q16.16` Dual-Quaternion skeletal joints (antipodal & zero-norm safe blend) |
| `144..151` | `BloomSdfBrick64[8]` | Sparse `4×4×4` `4-bit` quantized SDF voxel bricks with trilinear `Q16.16` sampling |
| `152..167` | `BloomUiNode64[16]` | Compiled React-Native Flexbox / `.svg` Liquid Glass buttons, sliders, `TextInput`, `LatexCard` & gauges |
| `168..183` | `BloomGlyph64[16]` | Vector `<path>` / `<line>` SDF icon glyphs (`Q8` normalized segment coordinates) |
| `184..199` | `BloomLatexBox64[16]` | Single-pass DFA compiled zero-font-atlas analytic SDF LaTeX formula boxes |
| `200..231` | `uint64_t flow_states[32][8]` | `FLOW v7.0.1` 2-Jet channels: Camera (`0..3`), UI Springs (`4..19`), X-Ray (`20`), Scroll (`21`), Prefabs (`22..31`) |
| `232..247` | `uint64_t flow_charts[16][8]` | `FLOW v7.0.1` Manifold Atlas (`UI_SPRING`, `UI_SCRUB`, `CAMERA_RIG`, `XRAY_SWEEP`, `800K/760K` `KanLock`) |
| `248` | `BloomRaycastHit64` | Deterministic CPU `Q16.16` 3D SDF/BVH raycast pick hit position, normal & instance ID |
| `249` | `BloomImageHeader64` | `.bloom` (`"BLOMIMG7"`) zero-parse `mmap` superblock with 64-bit FNV-1a Merkle seal over `Slots 0..248` |
| `250..253` | `char ui_labels[16][16]` | Inline 16-byte `'\0'`-padded ASCII widget labels for `BloomUiNode64[0..15]` (vector SDF rendered) |
| `254..255` | `BloomCell64[2]` | Reserved tail padding completing the exact `16,384B` (`256 × 64B`) VM page |

---

## 3. Repository Layout (`0` Dual-Track Files, `0` External Test Files)

```text
BLOOM/
├── include/
│   ├── bloom_abi.h            # 21 × 64B cache-line structs + 16KB BloomEngineArena ABI (C11 & C++20)
│   ├── bloom_scene.h          # Pillar 1 (Three.js): CSG, .glb, Morton BVH, DQ, Brick, Matter, Raycast
│   ├── bloom_ui.h             # Pillar 2 (React Native): .svg, Flexbox, SEARU_SOLVE_INLINE UI, IME, LaTeX
│   ├── bloom_prefab.h         # Pillar 3 (Prefab SSOT): Overrides, Revert, TRC, FLOW v7.0.1 & searu_heal_and_swap
│   ├── bloom_host.h           # Single-track C17 SDL3 GPU compute & native window host ABI
│   └── bloom.h                # Umbrella SDK header (BLOOM v7.0.1)
├── src/
│   ├── bloom_scene.c          # Pure C17 (-mgeneral-regs-only, -fsanitize-trap=all, 0 float, 0 heap, 0 blr)
│   ├── bloom_ui.c             # Pure C17 streamed SVG DFA, Flexbox/Bento, SEARU_SOLVE_INLINE, IME & LaTeX
│   ├── bloom_prefab.c         # Pure C17 Prefab SSOT, TRC guard, FLOW v7.0.1 2-Jet, searu_heal_and_swap & 0-FPS
│   ├── bloom_lean_jsi.c       # Lean 4 AOT JSI compiler bridge + FLOW v7.0.1 SEARU & GPU alignment verifiers
│   ├── bloom_gpu_host.c       # Pure C17 cross-platform SDL3 GPU host (Metal MSL 2.4 & Vulkan SPIR-V)
│   └── bloom_cli.c            # Standalone zero-Lean-runtime pure C17 Studio & headless render CLI
├── shaders/
│   └── bloom_core.wgsl        # Sole authored W3C WebGPU WGSL compute kernel (0 u64/i64 extensions)
├── specs/
│   └── studio_inspector.svg   # Browser-previewable W3C .svg Studio UI blueprint (optional --watch-svg)
├── tools/
│   └── sabotage_check.py      # Zero-SDL3 Mach-O ARM64 binary sabotage & ISA purity audit (100/100 = 100.0%)
├── .github/workflows/
│   └── ci.yml                 # Linux (Vulkan Lavapipe) + macOS (Metal) CI matrix pinned to FLOW v7.0.1
├── Bloom.lean                 # 22 formal theorems + React-Native Flexbox DSL + AOT studio.bloom export
├── Makefile                   # Single-track C++20 + C17 + Naga + Lean 4 AOT + Zero-Lean CLI + Sabotage
├── LICENSE                    # MIT License
└── VERSION                    # 7.0.1
```

---

## 4. Implementation Details & Verified Metrics

### 4.1 Declarative React-Native Flexbox DSL & `22` Formal Proofs ([`Bloom.lean`](Bloom.lean))
- **Nested `<View>` Flexbox Tree (`UiElement` + `compileUiTree`)**: UI layouts are authored declaratively using arbitrary nested `.view (dir : FlexDir) (justify : Justify) (align : Align) (style : UiBoxStyle) (children : List UiElement)` containers alongside `.button`, `.slider`, `.textInput`, `.latexCard`, `.ringGauge`, and `.readout7Seg`. Pure layout `.view` wrappers (`drawBg == false`) consume **`0` `BloomUiNode64` slots**, while `.latexCard` measures and auto-scales formulas inside padded card bounds (`verifyLatexInCards` enforces zero horizontal/vertical overflow).
- **22 Machine-Checked Theorems (`0 sorryAx`, `leanchecker` Verified)**:
  - **11 Arithmetic & Set Invariants (`decide` / `omega` / `rfl`)**: `arena_page_bijection`, `arena_slot_mask_inbounds`, `poly_smin_le_operands`, `bvh_parent_encloses_children`, `beer_lambert_crystal_floor_bounded`, `trc_strobe_clamp_safe`, `bento_split_disjoint`, `wave64_u32x2_bijection`, `prefab_revert_restores_ssot`, `prefab_revert_idempotent`, `flex_child_span_bounded`.
  - **11 Hardware Bit-Blasted Machine-Word Theorems (`Std.Tactic.BVDecide` CaDiCaL SAT)**: `arena_slot_mask_bv32_inbounds`, `wave64_set_test_exact_bv32` ($2^{96}$ state space), `wave64_set_frame_independence_bv32`, `csg_mask_pack_roundtrip_bv32`, `trc_strobe_clamp_bv32`, `beer_lambert_clamp_bv32`, `kan_lock_mode_preserve_bv16`, `prefab_override_revert_flags_bv16`, `ui_label_slot_inbounds_bv32`, `searu_phase_gate_silence_bv64` ($2^{128}$ proof of `searu_heal_and_swap` phase-gate silence whenever `CONVERGED` or `dt_resolved != 0`), and `searu_kan_pin_flip_invariant_bv64` ($2^{192}$ proof that masked SEARU candidate flips preserve every `KanLock`-pinned bit).
- **Elaboration-Time Hardware & AOT Export Gate (`compileTimeBoundaryGate`)**: Runs `verifySearuCore` (including `bloom_arena_heal_and_swap`, `bloom_ui_solve_glass_tracks64`, `searu_ring0_step_1bit`, `searu_ring0_bilevel_step`, Petersen graph `searu_solve_graph_color`, and `searu_solve_3sat`), `verifyGpuAlignment`, 256-trial dual-engine SplitMix64 GPU pixel-exact determinism, `0-FPS` `IDLE_SLEEP` verification, and exports the canonical `1280×720` `build/studio.bloom` (`16,384` bytes).

### 4.2 Standalone Zero-Lean-Runtime Pure C17 Runner (`build/bloom`, [`src/bloom_cli.c`](src/bloom_cli.c))
`build/bloom` links **only** `bloom_cli.o` + `bloom_gpu_host.o` + `libbloom.a` + `SDL3` (verified by `Makefile` via `otool -L` / `ldd` and `nm -u` to contain **`0` `leanshared` / `lean_*` symbols**):
- **Interactive 60Hz Native Studio (`./build/bloom [file.bloom] [--watch-svg ...]`)**:
  - Click & drag 3D viewport to orbit camera, scroll wheel to zoom, click 3D objects to raycast-select (`BLOOM_FLAG_STUDIO_GI` rim highlight), right-click or click `REVERT` to restore SSOT, click `MATTER` / `@SWAP` or scrub `X-RAY`.
  - Click the top `TextInput` bar to type live UTF-8 / IME text (`Backspace` edits, `Escape` unfocuses) and press `Enter` to execute studio commands (`JADE`, `GOLD`, `GLASS`, `SAPPHIRE`, `TITANIUM`, `ALERT` / `860K`, `NORMAL` / `300K`, `REVERT`, `XRAY`, `CLEAR`).
  - Polls `stat()` `st_mtime` every 15 frames on `<file.bloom>` (and optional `--watch-svg`) to hot-reload live edits without losing camera pose or selection.
- **Headless GPU Settlement & Render CLI**:
  ```sh
  ./build/bloom --headless build/studio.bloom --out frame.ppm --width 1280 --height 720 --steps 60
  # -> [BLOOM] Rendered 1280x720 -> frame.ppm (FNV1a=0x6f90007a12c2660e, nonblack=921600, GPU=Apple M1 (SDL3-GPU/metal))
  ```

### 4.3 Pure-GPR / Zero-Float / Zero-Heap / Zero-`blr` Core (`libbloom.a`) & `100 / 100` Sabotage Gate
- **Hardware Trap & Pure-GPR ISA Purity (`libbloom.a`)**: [`src/bloom_scene.c`](src/bloom_scene.c), [`src/bloom_ui.c`](src/bloom_ui.c), and [`src/bloom_prefab.c`](src/bloom_prefab.c) compile with `-mgeneral-regs-only -fsanitize=signed-integer-overflow,shift-exponent,bounds,null,return,unreachable -fsanitize-trap=all`, audited by `Makefile` and [`tools/sabotage_check.py`](tools/sabotage_check.py) to contain **`0` FPU/SIMD instructions, `0` `blr` indirect branch-and-link calls, and `0` heap allocations** across `16,149` ARM64 instructions (`32` core functions).
- **Adversarial Mach-O Binary Sabotage (`100 / 100 = 100.0%`)**: Mutates `100` ARM64 instructions directly in `bloom_scene.o`, `bloom_ui.o`, and `bloom_prefab.o` (`datapath_addimm: 6/6`, `datapath_addsub: 14/14`, `datapath_csel: 39/39`, `datapath_mul: 3/3`, `predicate_cbz: 26/26`, `static_isa_purity: 12/12`) and achieves a **`100 / 100` (`100.0%`)** kill rate with zero static or dynamic blind spots.
- **Cinema-Grade Single-Source `WGSL` Kernel ([`shaders/bloom_core.wgsl`](shaders/bloom_core.wgsl))**: Compiles cleanly via `naga` into both Vulkan `SPIR-V` (`build/bloom_core.spv`) and Apple Metal `MSL 2.4` (`build/bloom_core.metal`), combining refractive SDF raymarching, Beer-Lambert absorption, `BLOOM_FLAG_STUDIO_GI` studio shadows & planar floor reflections, `Wave64`-culled Liquid Glass Flexbox widgets, vector SDF ASCII labels (`Slots 250..253`), live `TextInput` cursor/IME rendering, and analytic SDF LaTeX overlays.

---

## 5. Toolchain Prerequisites & Build Flow

### Prerequisites
- **Sibling Upstream**: [`FLOW`](https://github.com/hianova/FLOW) (`../FLOW` at `v7.0.1`, override via `FLOW_DIR=/path/to/FLOW`).
- **macOS (Apple Silicon / Metal)**:
  ```sh
  brew install llvm naga-cli sdl3 pkg-config
  # Plus Lean 4 (`elan` toolchain `leanprover/lean4:v4.35.0` providing `lean` and `leanchecker`) and `python3`
  ```
- **Linux (Ubuntu/Debian + Headless Vulkan Lavapipe or Hardware Vulkan)**:
  ```sh
  sudo apt-get install -y clang clang-tidy llvm lld pkg-config mesa-vulkan-drivers libvulkan-dev
  # Plus SDL3 >= 3.2 (`-DSDL_VULKAN=ON -DSDL_OFFSCREEN=ON`), `naga-cli`, and Lean 4 (`elan`)
  ```

### Step-by-Step Build & Verification Pipeline ([`Makefile`](Makefile))

```sh
# Build + 22 formal theorems + live SDL3 GPU `#eval` + `studio.bloom` + standalone CLI headless smoke test
make test

# Full 5-Gate sign-off including 100-mutant Mach-O ARM64 binary sabotage audit (100.0% kill rate)
make verify

# Launch standalone pure C17 60Hz interactive Studio (`./build/bloom build/studio.bloom`, 0 Lean runtime!)
make studio

# Render any .bloom container headlessly to binary P6 PPM via standalone pure C17 CLI
./build/bloom --headless build/studio.bloom --out studio_preview.ppm --steps 60

# Optional: Install `bin/bloom`, `share/bloom/studio.bloom`, headers, libraries, and shaders under $(PREFIX)
make install PREFIX=/usr/local
```

---

## 6. License

Released under the [MIT License](LICENSE).
