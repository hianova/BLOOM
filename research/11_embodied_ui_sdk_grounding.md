# 11｜Embodied UI SDK 與 Pixel-Exact Grounding 實證研究

> 本研究報告透過原生 Apple M1 Metal 探針（[`probes/embodied_grounding_bench.mm`](./probes/embodied_grounding_bench.mm)，結果存於 `build/embodied_grounding_results.json`），實測驗證 **統一 Pixel-Render 物件四元組模型**、**4K UI + 3D Occlusion-Aware Grounding 架構決選**，以及 **GPU Analysis-by-Synthesis 視覺 6-DoF 定位與多模板辨識**。

```bash
make -C /Users/kuangtalin/Documents/BLOOM/research/probes run-ground
```

---

## 1. 統一物件本體定義（Pixel Render ↔ Embodied Dual）

承接 [`08_object_ssot_3d.md`](./08_object_ssot_3d.md)「準確的物件只於 `init` 發生，其餘皆為約束狀態變換」之實證，從像素渲染與具身互動對偶出發，2D UI 元件與 3D 實體共用同一個四元組契約：

$$\text{Object} = \langle \text{Identity},\; G_{\text{init}},\; s \in \mathcal{C},\; \mathcal{A} \rangle$$

1. **Identity（像素可回溯身分）**：每個像素透過 32-bit Grounding Packed ID 攜帶因果索引：
   - Bit `31..30`：`domain`（`0=empty`, `1=mesh`, `2=analytic`, `3=ui`）
   - Bit `29..16`：`slot`（`1..16383`）
   - Bit `15..8`：`part_id`（`0..255`，對應關節或次部件）
   - Bit `7..0`：`qdepth8`（Window depth量化 `[0..255]`）
2. **$G_{\text{init}}$（`init` 定格流形）**：3D `BloomVtx12` / `BloomAnalyticPrim32` 與 2D `BloomQuadHot` SDF／字形輪廓同時兼任正渲染幾何與逆向定位參考模板（單一視覺 SSOT）。
3. **$s \in \mathcal{C}$（`48 B` 受限自由度）**：Runtime 僅傳遞合法約束狀態（位置、四元數、尺度、`dof[3]`、剖面與 UI 控制量），同時壓低前向頻寬與逆向搜尋維度。
4. **$\Pi_V(G_{\text{init}}, s)$**：UI 按鈕與 3D 機械零件統一表現為不同投影參數 $V$ 下的螢幕觀測場與像素足跡。
5. **$\mathcal{A}$（Cold Affordance Contract）**：不膨脹 GPU Hot Structs，僅在 CPU Cold 表定義 `role`、`action_mask`、`enabled`、`modal_trap`、合法夾限與雙向 Grounding/Action 介面。

---

## 2. Experiment 1：4K (`3840×2160`) Grounding 架構決選（Apple M1 實測）

測試負載：4K 解析度、`256` 顆動態切分 Meshlet 3D 反應爐物件（`2,048` meshlets）、`32` 顆 Analytic 球探針、以及含 Sidebar／Modal／40 列 VirtualList 之複合 HUD。四組架構實測比較如下：

| 架構方案 | 4K GPU 中位數 | 4K p95 | 相對 Baseline 增量 | ID Buffer VRAM | `maxTG` 佔用率 |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`baseline_rgb_only`**（純繪圖基線） | **`0.893 ms`** (`1,119.4 fps`) | `1.596 ms` | `—` | `0.0 MB` | `1024` |
| **`opt1_mrt_private_compute`**（4K `Private R32Uint` MRT + Compute） | `2.197 ms` | `3.022 ms` | `+1.304 ms` | `31.64 MB` | `1024` |
| **`opt2_mrt_memoryless_tile`**（4K `Memoryless R32Uint` + Tile Shader） | `2.086 ms` | `2.426 ms` | `+1.192 ms` | **`0.0 MB`** | `1024` |
| **`opt3_halfres_id_pass`（勝出 ✅）**（Pass 1 純 `BGRA8` + Pass 2 半解析 `Private R32Uint` + Compute） | **`1.289 ms`** (`775.8 fps`) | `2.084 ms` | **`+0.396 ms`** | `7.91 MB`（按需配置） | **`1024`** |

### 矽片物理原理解析（為何 `opt3` 比 `opt2` 快 `3×` 增量？）

原本直覺認為 `opt2_mrt_memoryless_tile` 零 VRAM 且不用重繪第二個 RenderPass 會最快；**M1 實測推翻了直覺**：
1. 在 `opt2` 中掛載第二個 `R32Uint` Color Attachment，使 Apple TBDR Imageblock 每像素儲存從 `32-bit` 翻倍為 `64-bit`，直接壓縮片上 Tile 尺寸並迫使 HUD 圓角管線寫入 MRT 時因局部 `discard_fragment()` 破壞主繪圖 Pass 的 TBDR HSR，一口氣墊高 **`+1.192 ms`**。
2. 反之，**`opt3_halfres_id_pass` 讓 Pass 1 (`BGRA8`) 維持 100% 純淨（`0.893 ms` 滿血 TBDR HSR），僅在 `bloom_sdk_capture_snapshot` / `pick_at` 被呼叫時，才於同一條 CommandBuffer 尾端執行一次半解析 ID RenderPass（`1920×1080`；若視埠 `<= 2048` 則為原尺寸 `1:1`）+ Compute 歸約**。總增量僅 **`+0.396 ms`**（一般顯示幀更是 `0.000 ms` 零負擔！），且 `7.91 MB` 剛好落在 M1 `8 MB` SLC 甜點帶內。
   - **關鍵實作細節（已由探針踩坑實證）**：半解析 `ID RenderPass` 雖沿用邏輯全解析 `BloomFrameUniforms` 做頂點投影，但 Fragment 端 `[[position]]` 是半解析紋素座標，因此 `f_hud_id` 與 `f_proxy_id` **必須乘以 `pos_scale = (1 << half_shift)` / 半解析螢幕尺寸**，才能確保圓角 SDF 與射線交點 100% 對齊。

---

## 3. Experiment 2：Occlusion-Aware UI + 3D 雙域錨點驗證

在 4K 場景中構造 Modal 彈窗完全遮蔽 `btn_hidden`、部分遮蔽且剛巧覆蓋 `sl_partial` 幾何中心 `(280, 272)`、VirtualList 裁剪溢出列 `row=35`，並同步包含 3D 網格與 Analytic 球體：

| 觀測目標 | `opt2` (4K Tile) 實測像素 | `opt3` (Half-Res+Compute) 換算像素 | GPU 歸約 / 拾取驗證結論 |
| :--- | :---: | :---: | :--- |
| **`btn_ok` (`ui=2`, 無遮蔽)** | `28,120 px` | `28,112 px` | 精度誤差 `< 0.03%` |
| **`btn_hidden` (`ui=3`, Modal 全遮)** | **`0 px`** | **`0 px`** | 自動標定為 `occluded / actionable=false` |
| **`vlist_clipped` (`ui=35`, 容器裁切)** | **`0 px`** | **`0 px`** | 捲動可視域外清單列不回報可點擊 |
| **`sl_partial` (`ui=4`, Modal 遮中心)** | **`11,422 px`** | **`11,420 px`** | **中心 `(280,272)` 誤中 Modal (`slot=5`)；GPU `best_anchor` `(488,300)` / `(493,297)` 100% 精準命中 `sl_partial (slot=4)`** |
| **3D `reactor_1` / `analytic_1`** | `10,793` / `11,927 px` | `10,768` / `11,940 px` | 單一管線同時輸出 UI + Mesh + Analytic 像素實證 |

---

## 4. Experiment 3：GPU Analysis-by-Synthesis 視覺定位與辨識（Track V1 vs Track V2）

針對三個真實 `BloomVtx12` 網格模板（`torus` 含 `dof0` 關節變形、`bevel_box`、`octa_prism`），於 `256×256` 含高斯雜訊 (`sigma=6`) 與背景干擾條紋之觀測畫面下進行 **未知 6-DoF 姿態 + 未知模板類別 + 未知 `dof0`** 反演定位：

- **Track V1（失敗教訓 — 點雲雙向 Chamfer `k_eval_hypotheses`）**：耗時 `1.98 ms`，僅用有限表面點雲投影無法正確處理 Genus-1 `torus` 自遮蔽與平直多面體內部飽和度，導致出現子集塌縮（誤分類 `bevel_box -> torus`）及高姿態誤差（`26°–65°`）。
- **Track V2（勝出 ✅ — Hardware TBDR `16×8` Tiled Render-and-Compare）**：
  - 先以 `k_vision_prep_64`（threadgroup `10×10` 共享快取 `maxTG=1024`，零多餘取樣）+ 5 步 `k_vision_jfa64` 產出 `64×64` 抗噪前景與 Euclidean Chamfer 距離場。
  - 利用 BLOOM 原生硬體光柵管線，將 `128` 組候選假設以 **`[[clip_distance]][4]` 防溢界直接繪入 `1024×512` `RG8Unorm` (`16×8` Tiles) + `Memoryless Depth32Float`**（自然處理所有自遮蔽與方向光照！），再由 `k_vision_score_tiles` 計算精準 `Soft-IoU + Contour DT + Luminance` 誤差並進行 CEM 細化。

| 真實目標 (`GT`) | 模板分類結果 | 實測 IoU | 平移誤差 (`t_err`) | 旋轉誤差 (`rot_err`) | 總 GPU 耗時 | Compute `maxTG` |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **`torus`** (`dof0=0.85`) | **`tpl=0` (正確)** | `0.765` | `0.178 m` | `15.60°` *(環對稱特性)* | `1.389 ms` | **`1024`** |
| **`bevel_box`** | **`tpl=1` (正確)** | `0.834` | **`0.045 m`** | **`4.13°`** | `1.335 ms` | **`1024`** |
| **`octa_prism`** | **`tpl=2` (正確)** | **`0.918`** | **`0.094 m`** | **`3.38°`** | `1.285 ms` | **`1024`** |
| **平均 / 總結** | **`3 / 3 (100%)`** | **`0.839`** | **`0.1058 m`** | **`7.71°`** | **中位 `1.335 ms`** | **`1024` 全達標** |

> *可觀測性附註*：`torus` 之局部 `0.08` 振幅 `dof0` 波形在 `64×64` 縮放尺度下僅約 `~1.2 px` 位移，與輕微 `yaw` 旋轉存在幾何權衡（此例 `dof_err = 0.802`）；較大振幅機械關節或以全解析度做最末輪微調即可進一步收斂。所有 **Go 準則**（`opt3` 增量 `+0.396 ms < 0.6 ms`、全域 `maxTG = 1024`、Experiment 2 遮擋與錨點全過、`3/3` 分類、`1.335 ms < 2.5 ms`、`0.106 m < 0.15 m`、`7.71° < 12°`）已全數達標，**直接進入 Phase 2 正式引擎實作**。
