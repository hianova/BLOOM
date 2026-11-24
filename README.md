# BLOOM vNext — Pure-GPU Constraint-Native 3D + UI Substrate

Clean-slate Metal-First graphics and UI architecture built directly on Apple M1 silicon limits and algorithmic information density proofs (`./research`). **Zero `FLOW` / `SDL` / third-party library dependencies.**

---

## Architecture Stack

| Layer | Module | Responsibility |
| :--- | :--- | :--- |
| **Layer 0** | [`include/bloom.h`](include/bloom.h), [`include/bloom_abi.h`](include/bloom_abi.h), [`include/bloom_host.h`](include/bloom_host.h), [`src/bloom_host_mac.mm`](src/bloom_host_mac.mm) | Single umbrella header `bloom.h`, zero-coupling external simulation projection bridge (`BloomStateProjection` preserving `mat_id` on `BLOOM_PROJ_FLAGS`), Cocoa `NSWindow` / `CAMetalLayer` / `NSTextInputClient` (CJK IME, pinch, keyup, clipboard) |
| **Layer 1** | [`include/bloom_bobj.h`](include/bloom_bobj.h), [`src/bloom_bobj.c`](src/bloom_bobj.c), [`include/bloom_scene.h`](include/bloom_scene.h), [`src/bloom_scene.c`](src/bloom_scene.c) | 3C Dual-Form Binary SSOT (`BOBC`/`BOBR`), Generational SlotMap object manager, automatic Front-to-Back Early-Z meshlet sorting, extended Flexbox, interactive widgets, `fnv1a64(id)` hot-reload state preservation, PBR material/light registry, Timeline & Idle Zero-Submit |
| **Layer 2** | [`include/bloom_gpu.h`](include/bloom_gpu.h), [`include/bloom_atlas.h`](include/bloom_atlas.h), [`src/bloom_atlas_mac.m`](src/bloom_atlas_mac.m), [`src/bloom_metal.mm`](src/bloom_metal.mm), [`tools/embed_shaders.py`](tools/embed_shaders.py) | Single-CommandBuffer TBDR Render Graph (`Blit -> Compute Cull -> Single RenderPass`), Triple-Buffered `Shared\|WriteCombined` rings, 2px-padded integer-snapped `R8Unorm` Glyph Atlas + `RGBA8` Sprite Atlas, embedded Metal shaders (`bloom_gpu_create(layer, NULL)` zero-path init), mipmapped PBR `texture2d_array` (`RGBA8` & `KTX2`), `MTLStorageModeMemoryless` `Depth32Float` |
| **Layer 3** | [`shaders/bloom_core.metal`](shaders/bloom_core.metal), [`shaders/bloom_grounding.metal`](shaders/bloom_grounding.metal), [`shaders/bloom_pbr.metal`](shaders/bloom_pbr.metal), [`shaders/bloom_sprite.metal`](shaders/bloom_sprite.metal), [`shaders/bloom_vision.metal`](shaders/bloom_vision.metal) | Compact specialized `fp16` Metal 3 shaders (`<= 400` lines each, `maxTotalThreadsPerThreadgroup == 1024`): Compute cluster cull, Track A `BloomVtx12` + HW `[[clip_distance]]`, Cook-Torrance metallic-roughness PBR + normal mapping, Track B analytic proxies, `Refetch20B` UI quads / shadows / glyphs / RGBA8 sprites, adaptive half-res ID grounding/reduction, and TBDR `16x8` vision atlas scoring |
| **M2 Packages** | [`include/bloom_ui_kit.h`](include/bloom_ui_kit.h), [`src/bloom_ui_kit.c`](src/bloom_ui_kit.c), [`include/bloom_gesture.h`](include/bloom_gesture.h), [`src/bloom_gesture.c`](src/bloom_gesture.c), [`include/bloom_camera.h`](include/bloom_camera.h), [`src/bloom_camera.c`](src/bloom_camera.c) | Track 2A Dark/Light `BloomTheme` tokens + 12 UI kit builders & zero-alloc Gesture/Keymap state machine (`Click/DoubleClick/LongPress/Drag/Pinch/Wheel/Action`); Track 2B Orbit/Fly/Follow/Trauma camera controller + screen raycast (`Sphere`/`AABB`) |
| **SDK / Vision** | [`include/bloom_sdk.h`](include/bloom_sdk.h), [`src/bloom_sdk.c`](src/bloom_sdk.c), [`include/bloom_vision.h`](include/bloom_vision.h), [`src/bloom_vision_mac.mm`](src/bloom_vision_mac.mm), [`tests/test_bloom_sdk.mm`](tests/test_bloom_sdk.mm), [`tests/test_bloom_m2.mm`](tests/test_bloom_m2.mm) | Unified Embodied UI & 3D Grounding SDK (occlusion-aware snapshots, interior anchors, `BLOOM_UI_MODAL` trap, 3D/UI pixel pick, action dispatch, JSON export) + GPU TBDR `16x8` Analysis-by-Synthesis 6-DoF visual localization + Milestone 2 4K hardware CI suite |
| **Tools** | [`tools/bloom_import.py`](tools/bloom_import.py) | Optional build-time compiler turning multi-node glTF 2.0 `.glb` (world TRS propagation, Morton-sorted meshlets, PBR base color) + `.svg` into `.bobc` / `.bobr` (`0` runtime XML/JSON tax) |

---

## Build, 7-Gate Hardware CI & Demo

```sh
make clean && make gate-ci   # Builds build/libbloom.a, runs M1/SDK/M2 4K CI, zero-coupling & shader checks
make lib                     # Emits self-contained build/libbloom.a with embedded Metal 3 shaders
make demo                    # Opens native Retina Cocoa + Metal 3 Studio Demo
```

External zero-path consumers only need `#include "bloom.h"`, link against `build/libbloom.a`, and call `bloom_gpu_create(layer, NULL)`.

- **Silicon, Architecture & Ecosystem Research**: [`research/README.md`](research/README.md) (`01`–`11`: M1 limits, GPUI comparison, Tri-Layer SSOT, 3D Constraint-Native SSOT, `../INFO` Hutter MDL format density proofs, frontend/3D package inventory, and Embodied UI SDK / Pixel Grounding M1 benchmarks).
- **Consumer Boundary**: [`/Users/kuangtalin/Desktop/Games/include/ge_host_if.h`](file:///Users/kuangtalin/Desktop/Games/include/ge_host_if.h).
