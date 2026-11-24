# 03｜純 GPU 原生資料結構設計（零 `FLOW` 耦合）

> 本文直接對標本機 [`zed-main/crates/gpui`](../zed-main/crates/gpui/src/scene.rs) 的真實架構，並給出在 Apple M1 實測中比 GPUI-like 更省頻寬、且比傳統頂點屬性傳遞快 **`2.48–3.22×`** 的結構佈局定案。

---

## 1. 對標分析：`zed-main` GPUI vs BLOOM 的矽片瓶頸在哪？

透過實際閱讀本機 [`gpui/src/scene.rs`](../../zed-main/crates/gpui/src/scene.rs#L866-L922)、[`gpui/src/color.rs`](../../zed-main/crates/gpui/src/color.rs#L779-L888) 與 [`gpui_apple/src/shaders.metal`](../../zed-main/crates/gpui_apple/src/shaders.metal#L59-L160)，我們確認 GPUI 的真實設計：
* **`gpui::Quad` 實際大小 = `160 Bytes`**（`#[repr(C)]`：`order` 4B + `border_style` 4B + `bounds` 16B + `content_mask` 16B + `Background` 72B + `border_color` 16B + `corner_radii` 16B + `border_widths` 16B）。即使只畫一個純色矩形，也帶著 72B 漸層結構與全套 `fp32` 邊框/圓角/裁切矩形一起塞進實例緩衝。
* **`quad_fragment` 在 Fragment 內回查 `quads[input.quad_id]`**：Vertex 傳遞 `QuadFragmentInput` (`quad_id` + `pos` + `border_color` + `PreparedBackground`) 給 Fragment，然後 Fragment 再去緩衝區讀取整個龐大的 `Quad` 來算 4 角不同圓角與 4 邊不同寬度的 `fp32` SDF。

在我們的 M1 實測探針（`[6/7] struct_layout`）中，我們同時測試了 **BLOOM `BloomQuadHot` (`32 B`)** 與 **GPUI-like Monolithic (`176 B`, `fp32` + fragment re-fetch)**，**並且意外挖出了一個決定 Apple TBDR 效能的核心定律！**

---

## 2. 重大實測發現：Apple TBDR Parameter Buffer 定律 (`Refetch20B`)

在 `3840×2160 (4K)` 畫面上繪製 `120×32 px` 圓角帶邊框 UI Quad（以及 `100k` 個 `4×4` 小 Quad 以隔離純頂點/緩衝區頻寬），實測四種管線配置如下：

| 測試規模 (`@ 4K`) | `Bloom32` Buf / CPU Pack | **`Bloom32 HalfInterp`** (`44 B` StageIn) | **`Bloom32 F32Pos`** (`72 B` StageIn) | **`Bloom32 Refetch20B`** (**`20 B` StageIn + L1 重讀 `32 B`**) | **`Gpui176` Monolithic** (Buf / CPU / GPU) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`2,000` UI Quads** (`120×32`) | `62.5 KB` / `7.9 µs` | `1.440 ms` | `1.541 ms` | **`0.581 ms` (`1,721 fps`)** | `343.8 KB` / `10.5 µs` / `1.087 ms` |
| **`20,000` UI Quads** (`120×32`) | `625.0 KB` / `55.9 µs` | `14.045 ms` | `15.197 ms` | **`4.806 ms` (`208 fps`)** | `3,437.5 KB` / `53.9 µs` / `10.424 ms` |
| **`100,000` UI Quads** (`120×32`) | `3,125.0 KB` / `732.5 µs` | `70.111 ms` | `76.028 ms` | **`23.583 ms`** | `17,187.5 KB` / `652.7 µs` / `51.868 ms` |
| **`100,000` Tiny Quads** (`4×4`) | `3,125.0 KB` / `732.6 µs` | `2.275 ms` | `2.353 ms` | **`1.936 ms`** | `17,187.5 KB` / `654.6 µs` / `2.331 ms` |

### 為什麼 `Refetch20B` 比「預解包塞給 StageIn」快 `2.48–2.65×`，同時比 GPUI-like 快 `1.87–2.20×`？
1. **Apple TBDR Parameter Buffer（頂點→片段參數緩衝）很昂貴**：
   `HalfInterp` 與 `F32Pos` 在 Vertex Shader 就把 `fill`、`border`、`origin`、`half_size`、`radii`、`bw` 全透過 `[[stage_in]]` / `[[flat]]` 寫入每頂點 `44–72 B` 的 Tiler Parameter RAM，每個 `32×32` Tile 在執行 Fragment 前都得把這堆變量搬進片上暫存器！
2. **`Refetch20B` 只經過 TBDR Parameter Buffer 傳 `20 B` (`float4 pos` + `uint quad_id [[flat]]`)**：
   一個矩形對所有像素 (`3,840 px`) 來說幾何參數都相同；在 Fragment Shader 內以 `quads[in.quad_id]` 直接走 **GPU L1 / Texture-Constant Cache** 讀取只有 **32 Bytes（1/4 條 Cacheline）** 的 `BloomQuadHot` 並用 **`half` (`2,061.9 GFLOPS`)** 運算 SDF：
   - **Tiler 參數流量降為極限（僅 1 個 `u32` ID + 座標）**；
   - **同一 32×32 tile 內同一個 `quad_id` 100% 命中 GPU L1 Cache**；
   - 緩衝區總體積只有 GPUI `160–176 B` 的 **~`1/5`**（`20,000` quads 僅 `625 KB`，全數鎖在 GPU L2 / SLC 內，完全不溢到 LPDDR4X DRAM）！
3. **關於 CPU Pack 時間的一條誠實修正**：
   在探針中 `100,000` quads 時軟體位元移位 `f32_to_f16()` 讓 CPU 花了 `732.5 µs`（反比直接填 `fp32` `652.7 µs` 慢 12%）。在實際引擎的 ARM64 C17 實作中，應使用硬體 `_Float16` (`FCVT` 單指令轉換) **或下節設計的 `uint16_t Q14.2` 定點子像素座標**——這也同時順手解決 `fp16` 在 `> 2048 px` 4K 右半螢幕時精度退化成 `2.0 px` 階梯的問題！

---

## 3. BLOOM 純 GPU 原生資料結構設計藍圖（Zero `FLOW`）

### 3.1 熱/冷分離 2D 原語結構

| 結構體 | 大小 | 佈局設計 (自然對齊、0 隱藏 Padding) | 用途與取用頻率 |
| :--- | :---: | :--- | :--- |
| **`BloomQuadHot`** | **`32 B`** | `int16_t x, y, w, h` (`Q14.2` 0.25px 精度到 `±8191px`, `8 B`)<br>`uint32_t rgba8_fill` (`4 B`)<br>`uint32_t rgba8_border` (`4 B`)<br>`uint16_t radius_x4_or_uniform` (`4×u8` 圓角或 `fp16` 細參數, `4 B`)<br>`uint16_t border_q8_8` (`2 B`) + `uint16_t clip_id` (`2 B`)<br>`uint16_t cold_style_id` (`2 B`) + `uint16_t z_flags` (`2 B` / `u32 z_flags`) | **95% 常見 UI 實心/漸層帶邊框矩形**；L1 一條 Cacheline (128B) 一次預載 **4 個 Quads**；`Refetch20B` 零冷資料浪費 |
| **`BloomClipRect`** | **`16 B`** | `float x0, y0, x1, y1` (16B) | UI 滾動視口遮罩通常整個頁面共用不到 16–64 個；存在獨立索引表，`BloomQuadHot` 只帶 `uint16_t clip_id`（省下每個 Quad `16 B`） |
| **`BloomStyleCold`** | **`64 B`** | `uint32_t tag` (Gradient / Pattern / Shadow / Transform ID), `float4 stops[2]`, `float angle_params[4]`, `float2x2 mat` | **冷路徑（僅漸層、旋轉矩陣、9-slice 材質才索引讀取）**；不污染 95% 普通矩形的熱頻寬 |
| **`BloomGlyphInst`** | **`24 B`** (pad `32 B` 若與 Quad 共用池) | `int16_t dst_x, dst_y, dst_w, dst_h` (`8 B`)<br>`uint16_t uv_x, uv_y, uv_w, uv_h` (`unorm16`, `8 B`)<br>`uint32_t rgba8_color` (`4 B`)<br>`uint16_t clip_id` (`2 B`) + `uint16_t atlas_flags` (`2 B`: page/MSDF/color-emoji) | 專供字體/單色圖標渲染；實例體積小至 `24 B`，4K 繪製 `100,000` 字元僅需 **`2.4 MB`**（全進 SLC） |
| **`BloomShadowInst`** | **`32 B`** | `int16_t box_xywh[4]` (`8 B`) + `int16_t clip_xywh_or_id` + `uint32_t rgba8` + `uint16_t blur_q8` + `uint16_t radius_q8` + `uint32_t flags` | 解析積分高斯圓角盒陰影 (Analytic Erf Box Shadow)，單實例 1 Pass 直接於片上混合 |

> **註**：所有 `x, y, w, h` 若需支援超大縮放畫布，改採 `float x, y` + `half w, h` 也能用 **`32 B`**（只要把 `cold_style_id` / `clip_id` 合併壓成 `uint32_t meta`），確保 **4K/8K subpixel 精確無階梯** 與 **嚴格 32 B 熱快取行** 兼得。

### 3.2 字體與圖標 Atlas 架構（徹底根除 `v9` 每像素解析字型）
1. **紋理分池 (`MTLStorageModePrivate`, `allowGPUOptimizedContents = YES`)**：
   - **Mono/MSDF Pool (`R8Unorm`)**：單通道字形 Alpha（Retina 1x 精準微調）或 MSDF（可縮放圖標與數學公式/LaTeX 點陣），頻寬只要 `RGBA8` 的 **1/4**。
   - **Polychrome Pool (`BGRA8Unorm_sRGB`)**：Emoji / 彩色圖示。
2. **CPU 端 Shelf / Skyline 2D Packing + `2 MB` Dirty Staging Ring**：
   - CPU 僅在新字形未快取時才觸發點陣化並寫入 `WriteCombined` staging slice，在每幀開頭透過單次 `MTLBlitCommandEncoder` `copyFromBuffer:toTexture:` 增量補丁進 `Private` Atlas。
3. **Shaping 留駐 CPU**：UTF-8 解碼、CoreText/HarfBuzz 字距與斷行每幀只在髒節點重算（微秒級），**絕不讓 Shader 逐像素掃描 UTF-8 字串**。

### 3.3 2D/UI 空間索引邊界 vs 3D 原生幾何結構

| 領域 | 圖元數量邊界 | 最佳空間資料結構 | 硬體原因 |
| :--- | :---: | :--- | :--- |
| **2D / UI** | `< 50,000` quads / 幀 | **無需 CPU/Compute Binning，直接交給 TBDR 硬體 Tiler** | `20,000` quads @ 4K 只要 `4.8 ms`（常見 UI `2,000` quads 僅 `0.58 ms`）；Compute 自建 Tile Bin 在中等量級只會增加同步與緩衝開銷 |
| **2D / UI 極端** | `> 50,000–100,000` 重疊實例 | **GPU Coarse Tile Prefix-Sum Binning (64×64 tile)** + Indirect Draw | 避免 Tiler Parameter Buffer 溢出、減少超密畫布逐元掃描 |
| **3D 幾何** | 任意網格（取代高耗能全螢幕 SDF） | **SoA Vertex Streams + Meshlet (`≤64v / ≤126t`)** | Position Stream (`float3` / `snorm16x4`) 獨立一條緩衝，讓 Shadow / Depth Prepass 零浪費讀取 Normal/UV |
| **3D 屬性壓縮** | 每頂點從 `48–64 B` 壓到 **`16–20 B`** | `Position` (`float3` 12B 或 `snorm16x4` 8B) + `OctNormal` (`snorm16x2` 4B) + `UV` (`half2` 4B) | 頂點提取頻寬砍半以上，完美契合 M1 68 GB/s DRAM 頻寬牆 |
| **3D GPU-Driven** | 場景物件 `> 1,000` | `BloomCullEntry (32 B bounding sphere + AABB + meshlet range)` → Compute Culling → `MTLIndirectCommandBuffer` | GPU 自己剔除視錐外與遮擋 Meshlet，中間緩衝 `< 4 MB` 留在 8 MB SLC 內交棒給 RenderPass |

---

## 4. 與上游（`Games` / App / 可選 `FLOW`）的零耦合邊界

```text
[上游 Game / FLOW 引擎：純語義輸出]
   例：Games/include/ge_host_if.h (ge_host_frame_t: 實體座標、血量、UI 語義鈕、攝影機)
            │
            ▼  (薄翻譯層 — 跑在 CPU，耗時 < 10 µs)
[BLOOM Frontend：Layout / Font Atlas Lookup / Dirty Diff]
   輸出純渲染原語：BloomQuadHot (32B), BloomGlyphInst (24B), MeshletDraws
            │
            ▼  (循序寫入 Triple-Buffered Shared|WriteCombined Ring，0-copy 概念直寫)
[BLOOM GPU Core：Metal / WebGPU]
   0 行遊戲邏輯、0 行 FLOW header、0 支 64-bit GPU 核心
```

透過這個結構邊界，`Games` 的模擬完全不需要知道 `BloomQuadHot` 或 SDF 的存在；反過來，`BLOOM` 的 Shader 永遠保持短小、`fp16` 滿載、高 Occupancy (`maxTotalThreads = 1024`)，不因上游增加一個欄位而發生暫存器雪崩。
