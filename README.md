# BLOOM v2.0.0 — Verified Spatial Geometry, Constitutive Optics & Zero-Copy Auto-Bento UI Layer

**Pure Lean 4 (`0 sorryAx`, `0` `.c`/`.h`/`.cpp`/`.m` files) + Zero-Copy `64-Byte` Metal & WebGPU Compute Libraries (`0-ULP` GPU/CPU pixel & tile-mask co-verification) atop `FLOW v5.0.0` & `UNIT v2.0.0`.**

---

## Architecture

`BLOOM v2.0.0` eliminates legacy C17 pseudo-VM / runtime `.lean` text-parser duplication and `#ifdef` macro-header shims, unifying **空間幾何 (3D SDF/CSG & Swept Anti-Tunneling)**, **本構物質光學與攝影機 (Constitutive Matter Optics & Physical `CineCamera`)**, and **零座標光學玻璃 UI (Zero-Coordinate `.flow` Auto-Bento HUD)** over a single pointer-free **`64-Byte` (`16 × u32`) Flat Buffer ABI**:

```text
  Bloom.lean (import Unit — 0 sorryAx, 0 .c/.h/.cpp/.m)
  ├── Bloom.Geometry : Polynomial smooth-CSG Lipschitz bounds (`polySmin`),
  │                    2-Jet swept continuous anti-tunneling, volume-preserving
  │                    Dual-Quaternion / rigid isometry, Morton 3D Linear BVH
  ├── Bloom.Optics   : Gladstone-Dale refractive index `eta(rho)`, Schlick
  │                    dielectric Fresnel, Beer-Lambert transmittance, Thin-Lens
  │                    Circle of Confusion, `cineCameraAtlas` ("CCAM"), and
  │                    ITU-R BT.1702 `<= 3.0 Hz` `strobeSafetyAtlas` ("STRB")
  ├── Bloom.Ui       : Zero-coordinate Auto-Bento constraint partitioning
  │                    (`0` overlap, exact area conservation) + portable `u32x2`
  │                    Wave64 (`8x8`) tile bitmask bijection (`bv_decide`)
  └── Bloom.GpuAbi & : Canonical `64-byte` (`16 × u32`) packet ABI, exact
      Bloom.Render     `32-bit`-only `Q16.16` split math (`0` `u64` extensions),
                       deterministic CPU reference renderer, binary `P6` `PPM`
                       image egress, and **Kitty Graphics Protocol APC** stream
                                │
             ┌──────────────────┴──────────────────┐
             ▼                                     ▼
  Apple Silicon Unified Memory          Cross-Platform Vulkan / DX12 / Web
  `shaders/bloom_core.metal` (MSL)      `shaders/bloom_core.wgsl` (WGSL 1.0)
  Zero-copy `MTLStorageModeShared`      Single-BindGroup `@group(0)` storage
  over raw `.flow` `64B` Atlas cells    buffers (`wgpu` / `Dawn` / Browser)
```

---

## Reusable Zero-Copy Compute Shaders (`shaders/`)

Downstream native macOS/iOS/visionOS apps and cross-platform Vulkan/DX12/Web apps bind `64-byte` `BloomPacket64` buffers and raw `.flow` Manifold Atlas bundles directly — **zero window-system lock-in (`NSWindow` / `HWND` / DOM), zero C glue, zero intermediate `.flow` serialization**:

1. **`shaders/bloom_core.metal`** (Apple Metal Shading Language):
   - Designed for Apple Silicon `MTLResourceStorageModeShared` Unified Memory zero-copy execution.
   - Exports 3 headless data-driven compute kernels (`bloom_wave64_tile_cull`, `bloom_sdf_matter_march`, `bloom_bento_glass_composite`) plus reusable hardware `float` (`f32`) SDF/optics primitives (`bloom_poly_smin_f32`, `bloom_gladstone_dale_f32`, `bloom_schlick_fresnel_f32`, `bloom_beer_lambert_f32`, `bloom_aces_tonemap_f32`).
   - `bloom_bento_glass_composite` inspects raw `FLOW v5.0.0` `64-byte` `State` + `Chart` cells directly in GPU memory, decoding Goldilocks seal / `sieveSealValid` health, `Q16.16` state `x`, power-of-two `chartSlot`, and Schmitt `[bExit, bEnter]` proximity into Emerald Safe / Amber Warning / Crimson Trip refractive frosted glass and single-pass analytic 7-segment SDF readouts.
2. **`shaders/bloom_core.wgsl`** (W3C WebGPU Shading Language 1.0):
   - 1:1 ABI-identical twin of `bloom_core.metal` under a single `@group(0)` bind group (`@binding(0..6)`), zero C preprocessor directives, and pure `32-bit` `u32`/`i32` exact fixed-point math (`0` unportable `u64`/`i64` shader extensions) — runs unmodified across Linux/SteamOS (Vulkan), Windows (DX12/Vulkan), macOS (Metal), and Web browsers via `wgpu` or `Dawn`.

---

## Repository Layout

```text
BLOOM/
├── Bloom.lean               # L2 Spatial/Optics/UI theorems, Atlases, 64B ABI & CPU renderer
├── TestBloom.lean           # .flow lifecycle + PPM/Kitty egress + Metal 0-ULP co-sim suite
├── shaders/
│   ├── bloom_core.metal     # Apple Silicon Unified Memory zero-copy MSL compute library
│   └── bloom_core.wgsl      # Cross-platform WebGPU WGSL 1.0 compute library
├── tests/
│   └── metal_cosim.swift    # Headless Metal.framework runner (0 .c/.h/.m files)
├── Makefile                 # Formal proof + Metal compilation + 0-ULP GPU/CPU co-sim gates
└── VERSION                  # 2.0.0
```

---

## Quick Start & Verification Gates

Requires sibling [`FLOW`](../FLOW) (`v5.0.0`), [`UNIT`](../UNIT) (`v2.0.0`), and `lean` (Lean 4); on macOS, `swiftc` + `Metal.framework` automatically enables live Apple GPU `0-ULP` co-verification:

```sh
make clean && make test
```

`make test` enforces all verification gates:
- **Gate 1 (Formal Proof & Zero-C/Obj-C Gate)**: Compiles `Bloom.lean` (`-DwarningAsError=true`), verifies `#print axioms` across all 14 core theorems (`0 sorryAx`), and asserts `0` `.c`/`.h`/`.cpp`/`.m` files in the repository.
- **Gate 2 (Shader Portability & Metal GPU Pipeline Gate)**: Compiles `tests/metal_cosim.swift` (`-warnings-as-errors`), compiles `shaders/bloom_core.metal` into native Apple GPU compute pipeline states (`MTLComputePipelineState`), and validates `shaders/bloom_core.wgsl` (`naga` when installed + ABI audit).
- **Gate 3 (`.flow` Runtime, Visual Egress & `0-ULP` Apple GPU Co-Verification)**: Executes `cineCameraAtlas` (`"CCAM"` + live `@swap`) and `strobeSafetyAtlas` (`"STRB"` strict $\le 3.0\text{ Hz}$ flash clamp) through `FLOW/build/flow`, verifies `P6` `PPM` and **Kitty Graphics Protocol APC** egress, and asserts **`0-ULP` bit-exact equality** between Apple Silicon Metal GPU and `Bloom.lean` across all `384/384` Wave64 tile-mask words and all `12,288/12,288` rendered `RGBA8` pixels.
