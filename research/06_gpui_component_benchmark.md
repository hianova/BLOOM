# 06｜`../zed-main` GPUI 全元件拆解與 Apple M1 實證對標

> 本章直接剖析本機 [`../zed-main`](../../zed-main/crates/gpui/src/scene.rs) 中的 `gpui` / `gpui_apple` / `gpui_wgpu` 元件實作，並以原生 Metal 探針 [`gpui_component_bench.mm`](./probes/gpui_component_bench.mm) 在同一顆 Apple M1 上對標實測 **6 大核心元件**（原始數據：`research/build/gpui_bench_results.json`）。目的只有一個：**看清 GPUI 哪裡做得極對值得吸收、哪裡有結構包袱可被 BLOOM 直接打穿 `1.4–5.0×`。**

---

## 1. `zed-main` GPUI 核心元件解剖

| 元件模組 | `zed-main` 原始檔案 | 核心機制 | 矽片評估（M1 觀點） |
| :--- | :--- | :--- | :--- |
| **Scene & DrawOrder R-Tree** | [`bounds_tree.rs`](../../zed-main/crates/gpui/src/bounds_tree.rs#L11-L374)<br>[`scene.rs`](../../zed-main/crates/gpui/src/scene.rs#L455-L794) | `MAX_CHILDREN=12` 扁平陣列 R-Tree (`max_leaf` O(1) 快路徑 + `max_key` 剪枝)；依 `PrimitiveKind` 分桶並透過 `max_shared_kind` 跨類型共用 `order`，再用 `BatchIterator` 合併同種 Draw Call | **優點**：在隨機混合 UI 場景將 Draw Call 砍 **`3.3–3.5×`**。<br>**弱點**：下方實測揭露 `Overlap/Nested` 病理下 `N=4,096` CPU 會暴漲到 **`21.2 ms`！** |
| **Animated Sprite Fast-Path** | [`scene.rs`](../../zed-main/crates/gpui/src/scene.rs#L120-L129) | `Scene::advance_animations` 直接原位改寫 `monochrome_sprites` 的 `transformation` 與 `color`，無須重建 Layout 與整棵 Scene | **極佳**——把低頻靜態場景快取起來只更新活動精靈矩陣，適合融合進 BLOOM Dirty-Diff 管線 |
| **Dual-Pass Opaque Early-Z** | [`metal_renderer.rs`](../../zed-main/crates/gpui_apple/src/metal_renderer.rs#L802-L824)<br>[`shaders.metal`](../../zed-main/crates/gpui_apple/src/shaders.metal#L451-L477) | 同一個 `160 B` `Quad` 緩衝送進 GPU **兩次**：Pass 1 反序畫不透明矩形進 `Memoryless Depth32Float`（非實心矩形在 Vertex 輸出 `CULLED_POSITION`），Pass 2 用 `MTLViewport(depth, depth)` 定深畫半透明元素 | **思路正確但實作有浪費**：送兩遍 `160 B` 緩衝 + Vertex 丟棄死實例。BLOOM 在 CPU 打包時直接分桶寫 `32 B`（下方實測 GPU 快 `1.42×`、CPU 也快 `1.46×`） |
| **Taffy Incremental Layout** | [`taffy.rs`](../../zed-main/crates/gpui/src/taffy.rs#L48-L140) | `SecondaryMap<DefaultKey, NodeContext>` 把量測閉包 (`KeyedMeasure`) 存在 `TaffyTree` 外部，避免每幀替換 context 觸發 Taffy 重設髒標記而破壞子樹快取 | **極重要的工程細節**：無論未來 BLOOM 保留強化版 C17 `bloom_ui.c` 或接 Taffy，Tree 節點跨幀快取與量測閉包外置都能守住 `< 20 µs` 前端預算 |
| **Frame Pacing & Ring** | [`display_link.rs`](../../zed-main/crates/gpui_apple/src/display_link.rs#L52-L104)<br>[`metal_renderer.rs`](../../zed-main/crates/gpui_apple/src/metal_renderer.rs#L56-L114) | macOS 14+ 用 `CAMetalDisplayLink` 驅動、`2 MB` `InstanceBufferPool` (`StorageModeShared \| WriteCombined`) 隨 `addCompletedHandler` 歸還、`Depth32Float` 採 `Memoryless` | **完全吻合 M1 硬體物理**；BLOOM 再疊加「靜止畫面 0 submit」與「明確 3-slot 固定環形池（無每幀鎖 Mutex）」 |
| **9 條特化短 Shader** | [`shaders.metal`](../../zed-main/crates/gpui_apple/src/shaders.metal) (`1,354` 行總長) | `opaque_quad`, `quad`, `shadow` (`erf7`), `underline`, `monochrome_sprite`, `polychrome_sprite`, `path_tile` (Slug), `surface` (NV12 `CVMetalTextureCache`), `native_view` | 總共才 1,354 行 MSL（vs 舊版 BLOOM `v9` 單檔 11,711 行）；主要瓶頸在於 `160/112 B` Monolithic 結構、`56–100 B` `[[stage_in]]` 膨脹，以及 Vertex 端重複運算 `prepare_blurred_rounded_rect` |
| **Atlas Upload 差異** | [`metal_atlas.rs`](../../zed-main/crates/gpui_apple/src/metal_atlas.rs#L172-L260)<br>[`wgpu_atlas.rs`](../../zed-main/crates/gpui_wgpu/src/wgpu_atlas.rs#L25-L76) | Metal 版用 `MTLStorageModeShared` 貼圖＋每次插入同步 `replaceRegion`；WGPU 版先存進 `pending_uploads`，開幀前才批次送進 `wgpu::Queue` | **實證出現精確交叉點**（見第 2 節）：微小 16×16 glyph 用 `replaceRegion` 極快，但 `Private` 貼圖渲染採樣快 `1.84×`——理想架構需結合「Staging Ring + 單幀同 CB Blit」 |

---

## 2. 六項實證對標測試數據（Apple M1 @ 4K `3840×2160`）

所有測試均經過 150 ms DVFS 預熱，採用金屬硬體 `GPUEndTime - GPUStartTime` 與 `CLOCK_UPTIME_RAW` 中位數（≥ 20 次計時樣本）。

### 2.1 文字熱路徑與 Atlas 上傳 (`glyph_atlas_and_sprites`)

| Atlas 上傳情境 | GPUI (`Shared replaceRegion`) | BLOOM (`Private + WC Staging Blit`, 獨立 CB) | 關鍵發現 |
| :--- | :---: | :---: | :--- |
| `128 × (16×16)` 小字形 (`A8` / `R8`, `32 KB`) | **`76.8 µs`** | `930.6 µs` wall (`215.6 µs` GPU) | 拆 128 筆小 `copyFromBuffer` 加單開 CB (`~220 µs`) 同步懲罰極重；極小增量字形應在 CPU 合併為單一 Dirty Patch Rect 再 Blit（或與主渲染同 CB 零等待） |
| `8 × (256×256)` 圖磚/頭像 (`BGRA8`, `2 MB`) | `1,038.8 µs` | **`414.0 µs` wall (`40.5 µs` GPU)** | **大圖塊 `WC + Blit` 比 `replaceRegion` 快 `2.51×` wall / `25.6×` GPU** |

**4K 文字精靈繪製實測（GPUI `GpuiMonoSprite112` `112 B` + `Shared A8Unorm` + `56 B StageIn` vs BLOOM `BloomGlyphInst` `24 B` + `Private R8Unorm`）：**

| 4K 字元數 (`12×24 px`) | GPUI `112 B` (Buf / CPU / GPU) | BLOOM `24 B` Buf / CPU | BLOOM `ClipVert` (`28 B` StageIn) | BLOOM `Refetch20B` (`20 B` StageIn) | GPU 加速比 |
| :---: | :---: | :---: | :---: | :---: | :---: |
| **`2,000` 字元** | `218.8 KB` / `10.0 µs` / `0.319 ms` | `46.9 KB` / `8.0 µs` | `0.289 ms` | `0.290 ms` | **`1.10×`** (`4.7×` 省緩衝) |
| **`20,000` 字元** | `2,187.5 KB` / `121.5 µs` / `0.891 ms` | `468.8 KB` / `97.1 µs` | `0.736 ms` | **`0.721 ms`** | **`1.24×`** (`Q14.2` CPU 也快 `25%`) |
| **`50,000` 字元** | `5,468.8 KB` / `309.5 µs` / `1.909 ms` | `1,171.9 KB` / `242.8 µs` | `1.537 ms` | **`1.316 ms`** | **`1.45×`** (`5.47 MB` 壓到 `1.17 MB` 鎖進 SLC) |

> **補充實測證據**：當字元達 5 萬個（全螢幕密排程式碼），GPUI 的 `112 B × 50k = 5.47 MB` 加上 `Shared A8Unorm` 與 `56 B StageIn` (`float4 clip_distance`) 開始頂到 SLC/參數緩衝上限；BLOOM `24 B` 搭配 Vertex 預裁剪、`Q14.2` 整數打包與 `Refetch20B`，在 GPU 與 CPU 同時獲勝。

---

### 2.2 Dual-Pass 頂點剔除 vs CPU 預分桶 (`dual_pass_vs_prepartitioned`)

在 4K 渲染 **`20,000` 個混合矩形**（50% 實心不透明矩形、50% 圓角帶邊框半透明矩形，含 `Memoryless Depth32Float` Early-Z）：

| 架構 | 送 GPU 緩衝總量 | GPU 頂點執行總數 | CPU 打包時間 | 4K GPU 時間 | 綜合對比 |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **GPUI Dual-Pass Vertex Cull** (`160 B × 2 passes`) | `3,125.0 KB` | `40,000` instances（各剔除一半） | `83.8 µs` | `5.928 ms` | 實作簡單但多跑 2× 死頂點且吃 5× 頻寬 |
| **BLOOM CPU Pre-Partitioned** (`Opaque 32B` + `Alpha 32B Refetch20B`) | **`625.0 KB` (`5.0×` 更小)** | **`20,000` instances (0 浪費)** | **`57.2 µs`** | **`4.186 ms`** | **GPU 快 `1.42×`、CPU 也快 `1.46×`** |

---

### 2.3 解析高斯陰影：`erf7` 4-Vertex 重複預計算 vs CPU Precompute + `Refetch20B` (`analytic_shadows`)

GPUI 的 [`shadow_vertex`](../../zed-main/crates/gpui_apple/src/shaders.metal#L554-L579) 會替每個陰影的 4 個頂點重複執行 `exp`、`sqrt` 與 4 次 `erf7`，再透過 **`100 B` `ShadowFragmentInput`** 塞過 TBDR Parameter Buffer；BLOOM 將均勻圓角陰影預計算收進 CPU 算 1 次並壓進 **`32 B` `BloomShadowHot`** + **`20 B` StageIn** + **`half` `erf7_h`**：

| 4K 陰影數量 (`240×120 px`, `blur=12 px`) | GPUI (`112 B + 100 B StageIn`) | BLOOM (`32 B + Refetch20B`) | GPU 加速倍率 |
| :---: | :---: | :---: | :---: |
| **`64` 個** (常見視窗/浮層數) | CPU `1.0 µs` / **`0.520 ms`** | CPU `0.8 µs` / **`0.340 ms`** | **`1.53×`** |
| **`512` 個** | CPU `2.6 µs` / **`3.579 ms`** | CPU `2.0 µs` / **`1.741 ms`** | **`2.06×`** |
| **`2,000` 個** (重壓壓力測試) | CPU `11.2 µs` / **`13.863 ms`** (逼近掉出 60 FPS) | CPU `8.1 µs` / **`6.501 ms`** | **`2.13×`** |

---

### 2.4 向量路徑：GPUI Slug `PathTile` vs 快取 `Private` Atlas (`slug_path_tiles_vs_atlas`)

在 4K 渲染 `48×48 px` 帶 16 條單調二次 Bézier 曲線的向量圖示（每圖切 4 個 `24×24` tiles）：

| 4K 圖示規模 | GPUI Slug (`72 B StageIn`) | BLOOM Slug (`Refetch20B`) | **快取進 `Private R8Unorm` Atlas (`24 B`)** | Atlas 快過動態 Slug 倍率 |
| :---: | :---: | :---: | :---: | :---: |
| **`256` Icons** (`1,024` tiles) | `0.523 ms` | `0.514 ms` | **`0.277 ms`** | **`1.89×`** |
| **`2,048` Icons** (`8,192` tiles) | `2.446 ms` | `2.396 ms` | **`0.488 ms`** | **`5.01×`** |

> **工程定論**：GPUI 的 Slug `PathTile` 解算在片元端佔了絕大部分時間（縮減 StageIn 只省 `~2%`），但其品質對「無段縮放 / 動態貝茲路徑 / 任意線寬圖表」極有價值；**反覆出現的固定 DPI UI SVG 圖示，必須快取進 `Private R8Unorm` Atlas（實測快 `5.0×`）**，動態縮放路徑才轉交 Slug `PathTile`。

---

### 2.5 `BoundsTree` CPU 繪製順序合併與 `Overlap` 病理實測 (`bounds_tree_cpu_batching`)

把 `zed-main/crates/gpui/src/bounds_tree.rs` (`MAX_CHILDREN=12`) 完整移植為 C++17 並用 `zed-main/crates/gpui_platform/benches/primitives.rs` 幾何模式實測：

| 圖元數 `N` | Pattern (來自 `primitives.rs`) | `BoundsTree` + 排序耗時 | 未排序 Draw Batches → 合併後 Batches | 觀察到的雙面物理真相 |
| :---: | :--- | :---: | :---: | :--- |
| `256` | `random` (Shadow/Quad/UL/Sprite 混排) | **`73.6 µs`** | `197` → **`60`** (**砍 `3.3×`**) | 一般介面規模開銷極低、合批收益極高 |
| `1,024` | `random` | **`504.3 µs`** (`0.50 ms`) | `742` → **`214`** (**砍 `3.5×`**) | 可接受，仍是 120Hz 幀預算 6% |
| `1,024` | `overlap` (全部重疊) | **`1,423.9 µs`** (`1.42 ms`) | `1` → `1` | 剪枝失效開始顯現 |
| `4,096` | `overlay` (偶發覆蓋層) | `681.7 µs` | `1` → `1` | 大多數不相交因此 `max_leaf` 快路徑有效 |
| `4,096` | `random` | **`3,711.6 µs`** (`3.71 ms`) | `2,850` → **`826`** (**砍 `3.5×`**) | `N ≥ 4,096` 時 CPU 排序與樹查找已是 ms 級成本 |
| **`4,096`** | **`overlap` / `nested` (病理極端)** | **`21,193.8 µs` (`21.19 ms`!)** | `1` → `1` | **致命病理：全重疊時樹退化成掃描，單靠 CPU 排序就超支 60 FPS！** |

> **改良方案**：在遊戲 HUD 或大批次數據視覺化（`> 1,000` 重疊圖元或已知 Layer 階層），**絕對不可對所有原生圖元盲塞 `BoundsTree`**；應採 **Explicit Layer Buckets（按 UI 層級先給大 `Z_layer`，只在同一容器視圖的 `< 512` 個動態重疊控件才跑小範圍 `BoundsTree` / 2D Spatial Hash）**，完全繞過 `21.19 ms` 重疊病理。

---

### 2.6 End-to-End 4K「程式編輯器 + Bento HUD」綜合實測 (`full_4k_ide_hud_composite`)

單幀同時於 **4K (`3840×2160`)** 繪製：`120` 實心面板 + `80` 圓角邊框玻璃卡片 + `24` 高斯陰影 (`blur=12 px`) + `15,000` 等寬程式碼字形 (`12×24 px`) + `40` 底線（含 20 條波浪線）：

| 端到端指標 (`@ 4K`) | GPUI-Faithful Metal 管線 | BLOOM vNext 純 GPU 管線 | 差距 |
| :--- | :---: | :---: | :--- |
| **Instance Buffers 總量** | `1,677.0 KB` (`1,717,248 B`) | **`359.5 KB` (`368,128 B`)** | **小 `4.66×` (遠低於 8 MB SLC!)** |
| **CPU 打包耗時 (`WC Ring`)** | `28.0 µs` | **`25.0 µs`** | 快 `1.12×` |
| **4K 全幀 GPU 中位數 (p95)** | `1.274 ms` (`p95 = 1.635 ms`, `785 fps`) | **`0.671 ms`** (`p95 = 0.907 ms`, **`1,491 fps`**) | **GPU 快 `1.90×` (省近一半時間與功耗)** |

---

## 3. BLOOM vNext 對標結論矩陣（Adopt / Beat / Skip）

| GPUI 元件 / 概念 | BLOOM 決策 | 實測理由 |
| :--- | :---: | :--- |
| `Memoryless Depth32Float` + Opaque Pass + 9 條短特化 Shader | **Adopt** | 維持 `1024` Occupancy 與片上深度零 DRAM 佔用 |
| `CAMetalDisplayLink` + `Shared \| WriteCombined` 實例緩衝池 | **Adopt (強化)** | 加碼固定 3-slot 環形緩衝 + Idle `0 submit` |
| `Scene::advance_animations` 靜態場景增量變換 + `Taffy` 外置 Context 概念 | **Adopt** | 保留靜態佈局不重算，只更新動畫矩陣 |
| `Quad` `160 B` / `Sprite` `112 B` / `Shadow` `112 B` Monolithic 結構 | **Beat (全面取代)** | Hot/Cold 拆成 `32 B` / `24 B` + **`Refetch20B`**，4K 綜合端到端快 **`1.90×`**、緩衝區小 **`4.66×`** |
| 頂點端 `prepare_blurred_rounded_rect` + `100 B StageIn` | **Beat** | CPU 預計算 1 次 + `32 B` + `half` `erf7_h`，陰影快 **`1.53–2.13×`** |
| `MetalAtlas` 全用 `Shared` 同步 `replaceRegion` + Dual-Pass 死頂點剔除 | **Beat** | 大圖磚用 `WC + 同 CB Blit` + `Private+Opt`（採樣快 `1.84×`）；CPU 先分 Opaque/Alpha 桶（快 `1.42×`） |
| 盲用全局 `BoundsTree` 處理超大量重疊元件 | **Skip / 限縮範圍** | `overlap-4096` 會飆出 **`21.19 ms`** CPU 尖峰；改用 Explicit Layer Key + 小範圍容器合批 |
| 動態 SVG 每次都即時跑 Slug `PathTile` | **Hybrid** | 固定尺寸重複圖示進 `Private R8Unorm` Atlas（快 **`5.0×`**）；僅動態任意縮放曲線才走 `Refetch20B` Slug |
| `gpui_wgpu` `Storage` vs `WebGL` + `wgpu_atlas` `before_frame()` 批次上傳概念 | **Adopt (跨平台參考)** | `before_frame()` 延後到單次 Queue 提交的思想可升級為 BLOOM 單一 CommandBuffer `Blit→Render` |
