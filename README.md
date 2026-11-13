# BLOOM v6.0.0 — 64-Byte `Three.js` + `React Native` + `Prefab SSOT` Engine

**First-Class Lean 4 Interactive & Formal Boundary (`18` Theorems, `0 sorryAx`, `0 lean -c`, `0` External Test Files) + Zero-Float / Zero-Heap Pure C17 Core (`libbloom.a`) + C++20 `64B` Alignment ABI Gate + Single-Source Cross-Platform `WGSL` Compute Shader (`naga` → `SPIR-V` + `MSL 2.4`) & Pure C17 `SDL3 GPU` Host (`bloom_gpu_host.c`) + `90 / 90` (`100.0%`) Mach-O Binary Sabotage Audit atop [`FLOW v6.0.0`](https://github.com/hianova/FLOW).**

---

## 1. Architectural Positioning & Single-Track Guarantee

`BLOOM v6.0.0` treats [`FLOW v6.0.0`](https://github.com/hianova/FLOW) (silicon-verified by [`ELE v6.0.0`](https://github.com/hianova/ELE)) as its atomic dynamical primitive and enforces a **100% single-track cross-platform architecture** verified end-to-end by `make verify` — with **zero authored `.metal` / `.m` / `.mm` / `.swift` files**, **zero `lean -c` C codegen**, **zero fake software gradient fallbacks**, and **zero external `.c` / `.lean` test files**:

```text
  ┌────────────────────────────────────────────────────────────────────────────┐
  │ 1. Top-Level Interactive & Formal Boundary (Lean 4: Bloom.lean)            │
  │    - 18 theorems (10 arithmetic/set + 8 CaDiCaL SAT bv_decide, 0 sorryAx)  │
  │    - Zero `lean -c`: loads `libbloom.{dylib,so}` via `--load-dynlib` JSI   │
  │    - Elaboration-time `#eval compileTimeBoundaryGate` (`0` external tests): │
  │      • `verifySearuCore`: SEARU DualChaos + Epiphany full C17 API torture  │
  │      • `verifyGpuAlignment`: 1-frame 8-Prefab + X-Ray + UI CPU/GPU parity  │
  │    - Standalone kernel replay via `leanchecker`                            │
  └─────────────────────────────────────┬──────────────────────────────────────┘
                                        │ 0-memcpy In-Process 16KB VM-Page Arena
                                        │ (`posix_memalign(16384)`, 256 × 64B)
  ┌─────────────────────────────────────▼──────────────────────────────────────┐
  │ 2. Deterministic Spatial, UI & Dynamical Engine (C17: libbloom.a + FLOW)   │
  │    - `-mgeneral-regs-only` + `-fsanitize-trap=all`: 0 float/SIMD, 0 heap   │
  │    - C++20 `-Wpedantic -Werror` `static_assert` (`sizeof` + `alignof` 64B) │
  │    - Pillar 1 (`bloom_scene.c`): CSG, `.glb`, 60-bit Morton BVH (`0-ULP`), │
  │      Dual-Quat skinning, 4×4×4 4-bit SDF bricks, 8 Matters (`128-Hex`), Ray│
  │    - Pillar 2 (`bloom_ui.c`): W3C `.svg` compiler, Flexbox, Golden-Ratio   │
  │      Auto-Bento, Wave64 `u32x2`, Pointer Capture, CJK IME, DFA LaTeX SDF   │
  │    - Pillar 3 (`bloom_prefab.c`): Prefab SSOT, O(1) Revert, X-Ray cutaway, │
  │      TRC `<= 3.0 Hz` guard, `FLOW v6.0.0` 2-Jet + `0-FPS` `IDLE_SLEEP`     │
  │    - Binary Sabotage (`tools/sabotage_check.py`): 90 / 90 (100.0%) killed  │
  └─────────────────────────────────────┬──────────────────────────────────────┘
                                        │ `SDL3 GPU` 16KB Storage Buffer Upload
                                        │ (`0` bytes & `0` dispatches when idle)
  ┌─────────────────────────────────────▼──────────────────────────────────────┐
  │ 3. Single-Source GPU Compute & Window Layer (WGSL + Pure C17 SDL3 GPU)     │
  │    - `shaders/bloom_core.wgsl`: Sole authored shader SSOT (`0` u64/i64)    │
  │      ├─ `naga` build-time -> `build/bloom_core.spv`   (Vulkan SPIR-V)      │
  │      └─ `naga` build-time -> `build/bloom_core.metal` (Metal MSL 2.4)      │
  │    - `src/bloom_gpu_host.c`: Pure C17 `SDL3 GPU` compute + HiDPI/IME host  │
  └────────────────────────────────────────────────────────────────────────────┘
```

### Physical Memory & `0-FPS` Energy Model
- **In-Process Zero-Copy CPU/Lean/FLOW Arena**: [`src/bloom_lean_jsi.c`](src/bloom_lean_jsi.c) allocates a single `16,384-byte` (`256 × 64B`, 16KB hardware VM page) `BloomEngineArena` via `posix_memalign(16384)`. Lean 4 (`@[extern]` + boxed VM entrypoints `l_Bloom_*___boxed`), `libbloom.a`, and [`FLOW v6.0.0`](https://github.com/hianova/FLOW) (`flow_step_atlas` stepping `Slots 200..231` against `Slots 232..247`) mutate and inspect the exact same memory page in place with zero serialization.
- **Cross-Platform `SDL3 GPU` Dispatch & `0-FPS` Idle Short-Circuit**: [`src/bloom_gpu_host.c`](src/bloom_gpu_host.c) avoids platform-specific Objective-C/Metal shims by driving `SDL3 GPU` (`SDL_GPUCopyPass` uploading `16,384` bytes into `@group(0) @binding(0)` read-only storage buffer + `SDL_GPUComputePass` dispatching `@workgroup_size(8, 8, 1)` into `@group(1) @binding(0)` `RGBA8_UNORM` storage texture). Once all 32 `FLOW v6.0.0` channels reach Lyapunov convergence (`FLOW_STATUS_CONVERGED`), `BLOOM_FLAG_IDLE_SLEEP` is asserted and `bloom_gpu_render()` returns immediately at function entry (`idle_skip_count++`) — performing **`0` bytes of transfer upload and `0` GPU compute dispatches** while stationary.

---

## 2. Unified `16,384-Byte` (`256 × 64B`) Arena Slot Map

Every runtime, scene, UI, dynamical, and `.bloom` container cell is strictly **`64 bytes` (`16 × u32` / `8 × u64`)** aligned to `64` bytes (`BLOOM_STATIC_ASSERT64`), addressed via $\mathbb{Z}/256\mathbb{Z}$ OOB-immune masked indexing (`idx & 0xFFu`):

| Arena Slots | Cell Type ([`include/bloom_abi.h`](include/bloom_abi.h)) | Responsibility |
| :--- | :--- | :--- |
| `0` | `BloomFrameHeader64` | Magic `"BLM6"`, resolution, active counts, X-Ray plane, selection, `IDLE_SLEEP` / `TRC_SAFE` flags |
| `1` | `BloomCamera64` | Physical Thin-Lens (`focal_mm`, `aperture_fstop`, `focus_dist`) & 2-Jet smoothed Orbit Rig |
| `2` | `BloomRegBank64` | Zero-VDOM `12`-channel reactive `Q16.16` register bank + `56B` FNV-1a MAC |
| `3` | `BloomDomState64` | `Wave64` (`u32x2`) DOM mask, hover/active/Pointer-Capture node IDs, cull skip counter |
| `4` | `BloomFlexContainer64` | `O(N)` Row/Column Flexbox layout, scroll clamp, `Wave64` container tile mask |
| `5..6` | `BloomTextInput64 + BloomIme64` | RFC 3629 codepoint-safe UTF-8 text input (`39B`) & CJK IME preedit/candidate bridge (`35B`) |
| `7` | `BloomWindow64` | Cross-platform HiDPI logical/pixel dimensions, normalized Q16 pointer/drag/wheel & key events |
| `8..15` | `BloomMatter64[8]` | Constitutive Matter presets ($\eta, \kappa, \mu_a$, Mie scatter, Kelvin, `128-Hex` MAC wire codec) |
| `16..23` | `BloomArtStyle64[8]` | Canonical Art Bible optical styles (`TRC <= 3.0 Hz` strobe guard + colorblind geometry stroke) |
| `24..39` | `BloomPrefabDesc64[16]` | Reusable 3D Prefab SSOT blueprints (`BLOOM_CSG_MASK_PACK` 2-primitive CSG + `56B` FNV-1a hash) |
| `40..71` | `BloomPrefabInst64[32]` | Live 3D scene instances, non-destructive style/matter/param overrides & $O(1)$ `Revert to SSOT` |
| `72..103` | `BloomCsgOp64[32]` | Data-driven 3D CSG operations & binary glTF 2.0 (`.glb`) ingested primitives |
| `104..135` | `BloomBvhNode64[32]` | Pointer-free `60-bit` 3D Morton Z-order Linear BVH hierarchy (`0-ULP` parity with brute-force CSG) |
| `136..143` | `BloomJointDq64[8]` | Volume-preserving `Q16.16` Dual-Quaternion skeletal joints (antipodal & zero-norm safe blend) |
| `144..151` | `BloomSdfBrick64[8]` | Sparse `4×4×4` `4-bit` quantized SDF voxel bricks with trilinear `Q16.16` sampling |
| `152..167` | `BloomUiNode64[16]` | Compiled `.svg` / Auto-Bento Liquid Glass widgets, sliders, ring gauges & 7-segment readouts |
| `168..183` | `BloomGlyph64[16]` | Vector `<path>` / `<line>` SDF icon glyphs (`Q8` normalized segment coordinates) |
| `184..199` | `BloomLatexBox64[16]` | Single-pass DFA compiled zero-font-atlas analytic SDF LaTeX formula boxes |
| `200..231` | `uint64_t flow_states[32][8]` | `FLOW v6.0.0` 2-Jet channels: Camera (`0..3`), UI Springs (`4..19`), X-Ray (`20`), Prefabs (`22..31`) |
| `232..247` | `uint64_t flow_charts[16][8]` | `FLOW v6.0.0` Manifold Atlas (`UI_SPRING`, `UI_SCRUB`, `CAMERA_RIG`, `XRAY_SWEEP`, `800K/760K` `KanLock`) |
| `248` | `BloomRaycastHit64` | Deterministic CPU `Q16.16` 3D SDF/BVH raycast pick hit position, normal & instance ID |
| `249` | `BloomImageHeader64` | `.bloom` (`"BLOMIMG6"`) zero-parse `mmap` superblock with 64-bit FNV-1a Merkle seal over `Slots 0..248` |
| `250..255` | `BloomCell64[6]` | Reserved tail padding completing the exact `16,384B` (`256 × 64B`) VM page |

---

## 3. Repository Layout (`0` Dual-Track Files, `0` External Test Files)

```text
BLOOM/
├── include/
│   ├── bloom_abi.h            # 21 × 64B cache-line structs + 16KB BloomEngineArena ABI (C11 & C++20)
│   ├── bloom_scene.h          # Pillar 1 (Three.js): CSG, .glb, Morton BVH, DQ, Brick, Matter, Raycast
│   ├── bloom_ui.h             # Pillar 2 (React Native): .svg, Flexbox, Auto-Bento, IME, LaTeX SDF
│   ├── bloom_prefab.h         # Pillar 3 (Prefab SSOT): Overrides, Revert, TRC, FLOW v6.0.0 & .bloom
│   ├── bloom_host.h           # Single-track C17 SDL3 GPU compute & native window host ABI
│   └── bloom.h                # Umbrella SDK header (BLOOM v6.0.0)
├── src/
│   ├── bloom_scene.c          # Pure C17 (-mgeneral-regs-only, -fsanitize-trap=all, 0 float, 0 heap)
│   ├── bloom_ui.c             # Pure C17 SVG compiler, Flexbox/Bento, Pointer Capture, IME & DFA LaTeX
│   ├── bloom_prefab.c         # Pure C17 Prefab SSOT, TRC guard, FLOW v6.0.0 dynamics & 0-FPS sleep
│   ├── bloom_lean_jsi.c       # Lean 4 --load-dynlib JSI plugin + SEARU & GPU alignment verifiers
│   └── bloom_gpu_host.c       # Pure C17 cross-platform SDL3 GPU host (Metal MSL 2.4 & Vulkan SPIR-V)
├── shaders/
│   └── bloom_core.wgsl        # Sole authored W3C WebGPU WGSL compute kernel (0 u64/i64 extensions)
├── specs/
│   └── studio_inspector.svg   # Browser-previewable W3C .svg Studio UI blueprint
├── tools/
│   └── sabotage_check.py      # Zero-SDL3 Mach-O ARM64 binary sabotage & FPU purity audit (90/90 = 100%)
├── .github/workflows/
│   └── ci.yml                 # Linux (Vulkan Lavapipe) + macOS (Metal) CI matrix pinned to FLOW v6.0.0
├── Bloom.lean                 # 18 formal theorems + JSI SDK + compile-time `#eval` boundary gate
├── Makefile                   # 5-Gate single-track C++20 + C17 + Naga + Lean 4 + Sabotage build
├── LICENSE                    # MIT License
└── VERSION                    # 6.0.0
```

---

## 4. Implementation Details Across Layers

### 4.1 Zero-Float / Zero-Heap C17 Core (`libbloom.a`) & C++20 `64B` ABI Gate
[`src/bloom_scene.c`](src/bloom_scene.c), [`src/bloom_ui.c`](src/bloom_ui.c), and [`src/bloom_prefab.c`](src/bloom_prefab.c) are compiled into `build/libbloom.a` with `-std=c17 -Wall -Wextra -Wpedantic -Werror -mgeneral-regs-only -fsanitize=signed-integer-overflow,shift-exponent,bounds,null,return,unreachable -fsanitize-trap=all`:
- **C11 & C++20 `64-Byte` Size + Alignment ABI Contract ([`include/bloom_abi.h`](include/bloom_abi.h))**: `BLOOM_STATIC_ASSERT64(T)` enforces both `sizeof(T) == 64U` and `alignof(T) == 64U` (`_Alignof` in C11, `alignof` + `static_assert` in C++20), continuously verified by `make cpp-gate` (`clang++ -std=c++20 -Wall -Wextra -Wpedantic -Werror` across all public headers).
- **Hardware Trap Safety (`0` UBSan Runtime Bloat, `0` Traps under SEARU Chaos)**: Every signed integer overflow, shift-exponent violation, out-of-bounds array index, or null dereference compiles directly to a CPU hardware trap instruction (`brk` / `ud2`) without linking `__ubsan_*` symbols:
  - **`.glb` & `BVH == Brute-Force CSG` (`0-ULP`)**: 64-bit widened glTF chunk bounds prevent `uint32_t` wraparound; `sc_ieee754_finite_to_q16()` converts IEEE-754 `binary32` vertices via pure integer bit extraction (`0` dead NaN/Inf instructions); `bloom_bvh_eval_csg_q16()` matches `bloom_csg_eval_program_q16()` at **`0-ULP`** across smooth-union (`smin` padding), subtraction, intersection, and $L_1/\sqrt{3}$ octahedron bounds.
  - **Dual-Quaternion & `4-Bit` SDF Bricks**: `bloom_joint_dq64_blend()` guards against `> 0x1FFFFFFF` dot-product overflow via pre-shift normalization, antipodal shortest-arc flips (`dot < 0`), and zero-norm (`inv_len == 0`) singularities; `bloom_sdf_brick64_sample_q16()` performs overflow-immune trilinear sampling over `4×4×4` quantized nibbles.
  - **RFC 3629 UTF-8 & CJK IME Integrity**: `bloom_text_input_insert_char64()` (`39B` capacity), `bloom_text_input_backspace64()`, `bloom_ime_set_preedit64()` (`35B` capacity), and `bloom_ime_commit64()` stage and splice 1..4-byte codepoints atomically, guaranteeing zero split codepoints, zero overlong sequences, and zero buffer overruns at capacity cliffs.
- **`FLOW v6.0.0` Native Dynamical Integration ([`src/bloom_prefab.c`](src/bloom_prefab.c))**:
  - `bloom_arena_init_default()` publishes 16 canonical `FLOW v6.0.0` Manifold Charts via `flow_arena_publish_bound()` (`Slots 232..247`), including `800K` enter / `760K` exit Schmitt thermal hysteresis between `BLOOM_CHART_PREFAB_NORMAL` (`Chart 4`) and `BLOOM_CHART_PREFAB_ALERT` (`Chart 5`, `kan_lock = 0x0001`).
  - `bloom_arena_step_dynamics()` steps Camera Orbit (`0..3`), UI tactile micro-springs (`4..19`, equilibrium `1.0x` normal / `1.10x` hover / `0.92x` active), X-Ray cutaway plane (`20`), and Prefab thermal states (`22..31`) directly via `flow_step_atlas()`.

### 4.2 Single-Source `WGSL` Compute Pipeline & `SDL3 GPU` Binding Contract
- **Portable `u32x2` `Wave64` Masks (`0` `shader-int64`)**: [`shaders/bloom_core.wgsl`](shaders/bloom_core.wgsl) represents `8×8` screen-space tile masks as `(lo32, hi32)` `u32` pairs (`wave64_test_u32x2`), avoiding non-portable `u64`/`i64` WGSL extensions.
- **Unified `SDL3 GPU` Descriptor Layout & Exact CPU/GPU Spatial Parity**:
  - `@group(0) @binding(0) var<storage, read> arena : array<Cell64, 256>;` (`16,384` bytes)
  - `@group(1) @binding(0) var out_tex : texture_storage_2d<rgba8unorm, write>;`
  - `@compute @workgroup_size(8, 8, 1) fn bloom_render_frame(...)` implements all 8 canonical Prefab CSG combinations (`Union`, `Subtract`, `Intersect`, `Smooth-Union`), `xray_plane - p.x` cutaway, Complex Fresnel / Beer-Lambert / Planck blackbody shading, and `Wave64`-culled Liquid Glass `.svg` / Auto-Bento UI + SDF glyphs + analytic SDF LaTeX boxes in exact spatial lockstep with CPU `bloom_scene_raycast64()`.
- **Build-Time `naga` Dual-Target Translation**:
  - `naga shaders/bloom_core.wgsl build/bloom_core.spv` emits Vulkan SPIR-V matching `SDL3 GPU`'s Set 0 / Set 1 layout.
  - `naga --metal-version 2.4 shaders/bloom_core.wgsl build/bloom_core.metal` emits Apple Metal MSL 2.4 with `arena [[buffer(0)]]` and `out_tex [[texture(0)]]` (`0` `user(fake*)` placeholders).

### 4.3 Formal Proofs & Zero-External-Test-File Compile-Time Boundary ([`Bloom.lean`](Bloom.lean))
`Bloom.lean` proves **18 theorems (`0 sorryAx`)** before executing `#eval compileTimeBoundaryGate`:
- **10 Arithmetic & Algebraic Invariants (`decide` / `omega` / `rfl`)**: `arena_page_bijection`, `arena_slot_mask_inbounds`, `poly_smin_le_operands`, `bvh_parent_encloses_children`, `beer_lambert_crystal_floor_bounded`, `trc_strobe_clamp_safe`, `bento_split_disjoint`, `wave64_u32x2_bijection`, `prefab_revert_restores_ssot`, `prefab_revert_idempotent`.
- **8 Hardware Bit-Blasted Machine-Word Theorems (`Std.Tactic.BVDecide` CaDiCaL SAT)**: `arena_slot_mask_bv32_inbounds`, `wave64_set_test_exact_bv32` ($2^{96}$ state space), `wave64_set_frame_independence_bv32`, `csg_mask_pack_roundtrip_bv32`, `trc_strobe_clamp_bv32`, `beer_lambert_clamp_bv32`, `kan_lock_mode_preserve_bv16`, `prefab_override_revert_flags_bv16`.
- **Private Compile-Time Elaboration Gate (`0` External Test Files)**: `private def compileTimeBoundaryGate` runs during `lean --load-dynlib=build/libbloom.{dylib,so} -o build/Bloom.olean Bloom.lean` (never serialized into runtime `.olean` init), enforcing:
  1. **`verifySearuCore`**: Pure-CPU SEARU `DualChaosState` (branchless SWAR XorShift `AND` cooling + Zipf singular jumps) & `Epiphany` phase-boundary lock bombarding every public C17 API in `libbloom.a` with `INT32_MIN`/`INT32_MAX`, hostile `.glb`/`.svg`/LaTeX/UTF-8 payloads, `BVH == Brute-Force CSG` `0-ULP` parity, Dual-Quaternion antipodal/zero-norm blends, and `39B`/`35B` CJK/emoji boundary splices (`0` hardware traps).
  2. **`verifyGpuAlignment`**: Single-frame 8-Prefab (`1..8`) + X-Ray half-space cutaway (`world_x < xray_plane`) + Auto-Bento UI spatial alignment cross-checking CPU `Q16.16` `bloom_scene_raycast64()` and UI hit-boxes against live `SDL3 GPU` `WGSL f32` rendered pixels on the shared `engEdge` headless context.
  3. **256-Trial Dual-Engine SplitMix64 Lockstep & `0-FPS` Sleep**: `Override -> Revert`, `@swap` `KanLock`, pointer dispatch, `FLOW v6.0.0` Lyapunov convergence to `0-FPS` `IDLE_SLEEP` (`0` GPU dispatches across 16 idle steps), byte-exact dual-engine GPU `P6` `PPM` readback equality, and `.bloom` Merkle superblock sealing.

---

## 5. Toolchain Prerequisites & Build Flow

### Prerequisites
- **Sibling Upstream**: [`FLOW`](https://github.com/hianova/FLOW) (`../FLOW` at `v6.0.0`, pinned via `ref: v6.0.0` in [`.github/workflows/ci.yml`](.github/workflows/ci.yml); override via `FLOW_DIR=/path/to/FLOW`).
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
# Fast 4-Gate build + formal SAT + live SDL3 GPU `#eval` + `leanchecker` sign-off
make clean && make test

# Full 5-Gate sign-off including 90-mutant Mach-O ARM64 binary sabotage audit (100.0% kill rate)
make clean && make verify

# Launch live 60Hz interactive HiDPI SDL3 GPU Studio window (`0 lean -c`, via `--load-dynlib`)
make studio

# Optional: Install headers, libraries, shaders, specs, and Bloom.olean under $(PREFIX)
make install PREFIX=/usr/local
```

When `make verify` runs, it enforces **five sequential gates**:
1. **Gate 0 — ISO C++20 Strict Header & `64-Byte` Alignment Gate (`make cpp-gate`)**: Compiles all public `include/*.h` headers under `clang++ -std=c++20 -Wall -Wextra -Wpedantic -Werror`, checking `sizeof` and `alignof` (`64B` cells, `16,384B` arena).
2. **Gate 1 — Single-Track Hygiene, `clang-tidy` Static Analysis & `libbloom.a` ASM Audit**: Rejects any `.metal`, `.m`, `.mm`, `.swift`, `.cpp`, or `.cc` file under `include/`, `src/`, `shaders/`, or `specs/`; runs `clang-tidy` (`clang-analyzer-*`, `-warnings-as-errors='*'`) across all 5 C17 translation units; audits `build/libbloom.a` via `nm -u` (`0` `malloc`/`free`/`strtod`/`__ubsan` symbols) and `otool -tv` on macOS (`0` FPU/NEON/scalar-SIMD opcodes including `mov.[bhsdq]`).
3. **Gate 2 — Single-Source `WGSL` Audit & `naga` Cross-Compilation**: Verifies `0` `u64`/`i64` tokens in `shaders/bloom_core.wgsl` and compiles both `build/bloom_core.spv` (SPIR-V) and `build/bloom_core.metal` (MSL 2.4, `0` `user(fake*)` bindings).
4. **Gate 3 — `Bloom.lean` Formal SAT Proof, Live `SDL3 GPU` `#eval` & `leanchecker` Replay**: Checks all 18 theorems, runs `compileTimeBoundaryGate` (`verifySearuCore` + `verifyGpuAlignment` + 256-trial dual-engine GPU determinism), replays `build/Bloom.olean` in standalone `leanchecker Bloom`, and audits `#print axioms` (`0 sorryAx`).
5. **Gate 4 — Zero-SDL3 Mach-O ARM64 Binary Sabotage & Purity Audit (`make mutate-check`)**: [`tools/sabotage_check.py`](tools/sabotage_check.py) injects **`90` Mach-O binary mutants** (`9` static FPU/SIMD opcode injections + `81` stratified pure-GPR `CSEL`/`MUL`/`ADDSUB`/`ADDIMM`/`CBZ` instruction mutations across 30 core functions in `bloom_scene.o`, `bloom_ui.o`, and `bloom_prefab.o`), relinking pure CPU `.o` objects in parallel (~6s, zero SDL3 overhead) and requiring **`90 / 90` (`100.0%`) mutant kill rate (`0` survivors)**.

---

## 6. License

Released under the [MIT License](LICENSE).
