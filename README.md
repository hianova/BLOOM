# BLOOM vNext — Pure-GPU Constraint-Native 3D + UI Substrate

Clean-slate Metal-First graphics and UI architecture built directly on Apple M1 silicon limits and algorithmic information density proofs (`./research`). **Zero `FLOW` / `SDL` / third-party library dependencies.**

---

## Architecture Stack

| Layer | Module | Responsibility |
| :--- | :--- | :--- |
| **Layer 0** | [`include/bloom_abi.h`](include/bloom_abi.h), [`include/bloom_host.h`](include/bloom_host.h), [`src/bloom_host_mac.mm`](src/bloom_host_mac.mm) | Zero-coupling external simulation projection bridge (`BloomStateProjection` -> `48 B` `BloomObjectState48`) + thin Cocoa `NSWindow` / `CAMetalLayer` / `CADisplayLink` host |
| **Layer 1** | [`include/bloom_bobj.h`](include/bloom_bobj.h), [`src/bloom_bobj.c`](src/bloom_bobj.c), [`include/bloom_scene.h`](include/bloom_scene.h), [`src/bloom_scene.c`](src/bloom_scene.c) | 3C Dual-Form Binary SSOT (`BOBC` columnar wire + `BOBR` GPU-direct row mirror), Generational SlotMap object manager, parent-child hierarchy, Flexbox + VirtualList UI + reverse-Z hit-testing, `fnv1a64(id)` hot-reload state preservation, Visual Presentation Timeline & Idle Zero-Submit dirty tracking |
| **Layer 2** | [`include/bloom_gpu.h`](include/bloom_gpu.h), [`include/bloom_atlas.h`](include/bloom_atlas.h), [`src/bloom_atlas_mac.m`](src/bloom_atlas_mac.m), [`src/bloom_metal.mm`](src/bloom_metal.mm) | Single-CommandBuffer TBDR Render Graph (`Blit -> Compute Cull -> Single RenderPass`), Triple-Buffered `Shared\|WriteCombined` rings, `MTLStorageModePrivate` static geometry + CoreText `R8Unorm` Alpha Atlas (`Private+Opt`), `MTLStorageModeMemoryless` `Depth32Float` |
| **Layer 3** | [`shaders/bloom_core.metal`](shaders/bloom_core.metal), [`shaders/bloom_grounding.metal`](shaders/bloom_grounding.metal), [`shaders/bloom_vision.metal`](shaders/bloom_vision.metal) | Compact specialized `fp16` Metal 3 shaders (`<= 400` lines each, `maxTotalThreadsPerThreadgroup == 1024`): Compute cluster cull, Track A `BloomVtx12` + HW `[[clip_distance]]`, Track B Tight-Proxy $O(1)$ analytic primitives, `Refetch20B` UI quads / shadows / glyphs, adaptive half-res ID grounding/reduction, and TBDR `16x8` vision atlas scoring |
| **SDK / Vision** | [`include/bloom_sdk.h`](include/bloom_sdk.h), [`src/bloom_sdk.c`](src/bloom_sdk.c), [`include/bloom_vision.h`](include/bloom_vision.h), [`src/bloom_vision_mac.mm`](src/bloom_vision_mac.mm), [`tests/test_bloom_sdk.mm`](tests/test_bloom_sdk.mm) | Unified Embodied UI & 3D Grounding SDK (occlusion-aware snapshots, interior anchors, `BLOOM_UI_MODAL` trap, 3D/UI pixel pick, action dispatch, JSON export) + GPU TBDR `16x8` Analysis-by-Synthesis 6-DoF visual localization emitting `BloomStateProjection` |
| **Tools** | [`tools/bloom_import.py`](tools/bloom_import.py) | Optional build-time compiler turning `.glb` + `.svg` into `.bobc` / `.bobr` (`0` runtime XML/JSON tax) |

---

## Build, 7-Gate Hardware CI & Demo

```sh
make clean && make gate-ci   # Runs C17 unit tests, GLB/SVG importer check, M1 4K 7-Gate CI, zero-coupling & shader checks
make demo                    # Opens native Retina Cocoa + Metal 3 Studio Demo
```

- **Silicon, Architecture & Ecosystem Research**: [`research/README.md`](research/README.md) (`01`–`11`: M1 limits, GPUI comparison, Tri-Layer SSOT, 3D Constraint-Native SSOT, `../INFO` Hutter MDL format density proofs, frontend/3D package inventory, and Embodied UI SDK / Pixel Grounding M1 benchmarks).
- **Consumer Boundary**: [`/Users/kuangtalin/Desktop/Games/include/ge_host_if.h`](file:///Users/kuangtalin/Desktop/Games/include/ge_host_if.h).
