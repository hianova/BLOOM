# BLOOM Architecture Archive & First-Principles Re-Research (`./research`)

舊版 `v9.0.0` Megashader 程式碼已完整封存於 [`../archive/v9/`](../archive/v9/README.md)，讓新世代 **BLOOM vNext** 從第一性原理重新出發——**零 `FLOW` 耦合、純 GPU 原生管線（搭配微秒級薄 CPU 前端）**。

---

## 0. 研究報告目錄與一鍵重現

所有數據皆實測於本機 **`Apple M1` (`7-core GPU`, `4P+4E`, `8 GB UMA`, `128 B cache line`, `16 KB page`, `Metal 4`)**，零 `FLOW` / `SDL3` / 外部資產依賴。

```bash
make -C /Users/kuangtalin/Documents/BLOOM/research/probes clean && make -C /Users/kuangtalin/Documents/BLOOM/research/probes run-all
```

| 文件 / 檔案 | 主題與核心實測結論 |
| :--- | :--- |
| **[`01_silicon_limits.md`](./01_silicon_limits.md)** | **矽片物理上限**：8 MB SLC 懸崖（Copy `493.8 GB/s @ 2 MB` → `31.3 GB/s @ 64 MB`）、`fp16` (`2,061.9 GFLOPS`) vs `u64` 模擬 (`107.1 G_u64_OPS`, 慢 `19.2×`)、高暫存器 `megakernel` 佔用率崩潰 (`1024 → 448`)、CB 喚醒牆 (`225.8 µs`) |
| **[`02_memory_cache_tbdr.md`](./02_memory_cache_tbdr.md)** | **快取與 TBDR 架構**：`Shared_WC` CPU 回讀崩崖 (`30.62 → 0.33 GB/s`, 慢 `93×`)、`Private+Optimized` 貼圖快 `1.83×`、分散 UI (`256 widgets`) **TBDR `0.612 ms` vs Compute `78.047 ms` (`127.54×`!)** vs 滿版 16 層 Compute 暫存器累積 (`6.795 vs 12.574 ms`) |
| **[`03_gpu_data_structures.md`](./03_gpu_data_structures.md)** | **純 GPU 原生資料結構**：發現 **TBDR Parameter Buffer 定律 (`Refetch20B`)**——`32 B` `BloomQuadHot` 僅傳 `20 B StageIn` (`pos + quad_id`) 再由 L1 重讀，`4K 2k Quads = 0.581 ms` (`1,721 fps`) / `20k Quads = 4.806 ms`，擊敗 GPUI-like `176 B` (`1.087 / 10.424 ms`) 與肥大 StageIn (`1.440–1.541 ms`) |
| **[`04_transfer_and_pipeline_order.md`](./04_transfer_and_pipeline_order.md)** | **傳輸與管線排序**：Triple-Buffer (`154.7 µs/f`, CPU `21.5 µs`)、Apple TBDR HSR 與 **`discard_fragment()` 摧毀 HSR 實測 (`4.36 ms → 68.13 ms`, 暴慢 `15.8×`)**、同 CB 2 MB SLC 接力 vs 24 MB 溢位 DRAM (`1.018 → 2.156 ms`, `SplitCB` 再多損失 `+0.28–0.44 ms`) |
| **[`05_architecture_synthesis.md`](./05_architecture_synthesis.md)** | **BLOOM vNext 藍圖與討論題**：7 條硬體鐵律 (CI Gates)、4 層零耦合結構、**5 項開放架構決策**準備討論 |
| **[`06_gpui_component_benchmark.md`](./06_gpui_component_benchmark.md)** | **`zed-main` GPUI 全元件實證對標**：6 大元件實測對決——**4K IDE+HUD 綜合幀 BLOOM `0.671 ms` (`1,491 fps`, `360 KB`) vs GPUI `1.274 ms` (`785 fps`, `1,677 KB`) 快 `1.90×`**；陰影快 `2.13×`；Atlas 圖示快 `5.0×` vs Slug；揭露 `BoundsTree overlap-4096` `21.19 ms` 病理與 Adopt/Beat/Skip 矩陣 |
| **[`07_svg_ssot_fusion.md`](./07_svg_ssot_fusion.md)** | **SVG-SSOT 融合架構與 Git 法醫審計**：拆解 `v1→v2→v9` 成敗、實證 Tri-Layer (`Blueprint[2]` + `NodeKey` `StateStore` + `WC Ring` `TBDR`)——**`497` 節點 `.svg` 編譯 `778 µs`、熱重載完整保狀態 (`62.29 µs`)、1 萬列虛擬清單僅 `45 KB`、4K UI `1.087 ms` (`920 fps`) 比 `v9_compute_256` (`47.11 ms`) 快 `43.3×`** |
| **[`probes/m1_silicon_probe.mm`](./probes/m1_silicon_probe.mm)** | 原生 Metal 矽片物理探針（輸出 `build/m1_probe_results.json`） |
| **[`probes/gpui_component_bench.mm`](./probes/gpui_component_bench.mm)** | `zed-main` GPUI vs BLOOM 元件對標探針（輸出 `build/gpui_bench_results.json`） |
| **[`probes/svg_ssot_bench.mm`](./probes/svg_ssot_bench.mm)** | Tri-Layer SVG-SSOT 編譯、熱重載保狀態與 4K TBDR 對標探針（輸出 `build/svg_ssot_results.json`） |

---

## 1. Empirical Apple M1 Baselines Measured Before Archiving (Archived Reference)

| Benchmark Workload | 1280x720 | 1920x1080 | 3840x2160 (4K) | Architecture |
| :--- | :---: | :---: | :---: | :--- |
| **BLOOM `v1.0.0`** (Metal, `RESULTS.md` / `README.md`) | — | — | `0.72–0.80 ms` empty<br>`~6.19 ms` full + SVG-UI | 864-line MSL, `tile_vertex` + `tile_fragment` rasterization |
| **BLOOM `v2.0.0`** (`bloom_bento_glass_composite`, 16 windows M1) | — | `1.60 ms` (`623.8 fps`) | `10.45 ms` (`95.7 fps`) | 603-line MSL, `maxThreadsPerTG=1024`, dedicated pass |
| **GPUI-style Metal Instanced Quads** (M1 probe) | — | `0.33 ms` (`3008.6 fps`, 16q) | `0.93 ms` (`1078.9 fps`, 16q)<br>`0.87 ms` (`1148.4 fps`, 2000q) | Instanced `MTLRenderCommandEncoder`, on-chip TBDR |
| **Archived `v9` Pure UI: `ui_empty`** (0 objs, 0 UI, `flags=0`) | `1.99 ms` (`502.7 fps`) | `4.73 ms` (`211.3 fps`) | `14.08 ms` (`71.0 fps`) | 11,711-line translated MSL, always dispatches 3D passes |
| **Archived `v9` Pure UI: `ui_4btn`** (4 glass buttons) | `2.26 ms` (`443.3 fps`) | `6.61 ms` (`151.3 fps`) | `16.12 ms` (`62.0 fps`) | Fullscreen compute UI + `scene_hdr` DRAM roundtrip |
| **Archived `v9` Pure UI: `ui_16full`** (16 full-screen panels) | `3.97 ms` (`252.1 fps`) | `8.16 ms` (`122.5 fps`) | `26.41 ms` (`37.9 fps`) | Per-pixel panel scan `O(W*H*N)` + `67 MB` HDR traffic |
| **Archived `v9` Pure UI: `ui_16full_text`** (+4 TextInput/Glyph/LaTeX) | `5.37 ms` (`186.3 fps`) | `8.92 ms` (`112.1 fps`) | `35.12 ms` (`28.5 fps`) | Per-pixel 425-line vector font SDF + 115-step UTF-8 loop |
| **Archived `v9` Full 3D+GI** (`ge_bloom_native`, 3-pass split) | `39.37 ms` (`25.4 fps`) | `74.64 ms` (`13.4 fps`) | `257.10 ms` (`3.9 fps`) | Full-res multi-bounce SDF raymarching + soft shadow + RC GI |
| **CPU UI Kernel (`bloom_ui.c`)** (`flex(4)+pointer` / JSON) | — | — | `2.18 µs/frame` / `2.97 µs/call` | Pure C17 Q16 fixed-point, 0 heap alloc (`libbloom_wire.a` / `bloom_ui.c`) |

---

## 2. Six Root Bottlenecks Identified in Archived `v9`

1. **Compute Megashader Bloat & Occupancy Collapse**: Translating a 5,123-line monolithic WGSL containing 3D raymarching, GI, optics, analytic glyphs, IME, and 6 non-rendering FIR1/SEARU compute kernels produced an 11,711-line MSL shader. Naga's `max_total_threads_per_threadgroup(64)` and high live-register pressure reduced true Apple M1 hardware occupancy (`384` / `448` / `640` threads vs `1024` in `v2.0.0`), leaving an empty 4K frame at `14.08 ms` (~18× slower than `v1.0.0`).
2. **Fullscreen Compute UI `O(W*H*N)` vs Hardware Instanced Quads `O(covered px)`**: Running UI inside full-screen compute (`bloom_ui_overlay`) incurs `67 MB` of `scene_hdr` DRAM traffic at 4K and loops over widget descriptors per pixel, whereas instanced Metal/WebGPU render passes draw 2,000 rounded quads directly in TBDR tile memory (`Refetch20B`: `0.581 ms` @ 4K).
3. **Broken Wave64 Early-Out for Vector Glyphs / LaTeX**: `if (dom_hit || glyph_cnt > 0u || latex_cnt > 0u)` forced 100% of screen pixels into the HDR buffer and overlay pass whenever a single glyph or LaTeX formula was present, with zero per-glyph spatial culling.
4. **Per-Pixel Vector Font SDF & UTF-8 Linear Decode**: Evaluating ~425 lines of procedural ASCII SDF strokes and re-scanning up to `40 + 35 + 40 = 115` UTF-8 bytes per pixel inside `shade_text_input_widget` added `+8.71 ms` at 4K for just four 32-byte inputs. Replacing per-pixel stroke eval with a cached Glyph/MSDF Texture Atlas + CPU-side column pre-decoding removes this completely.
5. **Extra Fullscreen Offscreen Swapchain Blit**: `bloom_gpu_run_window` computed into a 32 MB offscreen texture before executing an extra fullscreen `SDL_BlitGPUTexture` each frame instead of rendering directly into the swapchain/CAMetalLayer pass with dirty tracking.
6. **Full-Res Multi-Bounce SDF Raymarching vs Hardware Rasterization**: Evaluating 8 instances, 5 global CSG ops, 8 dual-quaternion joints, soft shadows, planar reflections, and RC GI per pixel at native 4K hits raw M1 ALU limits (`257 ms`). Hardware triangle rasterization (`v1.0.0` `RESULTS.md`: `6.9–11.5 ms` @ 4K for 125k tris) or resolution-scaled 3D + native-res UI is required for 4K 60–120 FPS.
