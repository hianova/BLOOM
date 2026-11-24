# BLOOM vNext 格式資訊密度評測：`../INFO` Hutter MDL 實證（3A vs 3B vs 3C vs SVG vs GLB64）

> **實驗緣起（回應格式相容與資訊密度抉擇）**：  
> 當底層 GPU 執行態結構（`BloomVtx12` + `BloomObjectState48` + `Tight-Proxy` + `Refetch20B`）已確定能將 4K 混合幀壓在 `0.959 ms` 後，上層 SSOT 文件格式究竟該選 **XML (`3A <bloom-obj>`)**、**SVG 合流 (`3B <svg><b:scene>`)**，還是 **純二進位 (`3C BOBR Row / BOBC Columnar`)**？連帶問題是：**以 XML 為本體的 W3C SVG 本身是否真的適合當作核心 SSOT，還是應退位為可選匯入器？**  
> 本章完整實作這 **5 套 3D+HUD 格式**（含 `baseline_glb64` 與單檔 Base64 `3a_xml_b64`）及 **3 套獨立 2D HUD 格式**，以 **128-bit 語意 Digest** 與 [`INFO`](file:///Users/kuangtalin/Documents/INFO/README.md) 獨立零堆積驗證器（`eval-bpb`、`compress` + `info_decomp` SHA-256、`eval-mdl-group`、`ncd`）給出無可爭議的資訊論實證結論。

---

## §1 評測方法與實作規格（Zero-Lossy 128-bit & SHA-256 雙重鎖定）

實作與評測管線位於 [format_ssot_bench.cpp](file:///Users/kuangtalin/Documents/BLOOM/research/probes/format_ssot_bench.cpp) 與 [run_format_info_eval.py](file:///Users/kuangtalin/Documents/BLOOM/research/probes/run_format_info_eval.py)（完整 JSON 見 [format_info_results.json](file:///Users/kuangtalin/Documents/BLOOM/research/build/format_info_results.json)）：

1. **所有格式表徵完全等價的語意 IR (`BloomSceneIR`)**：包含模板 Meshlet 幾何（`BloomMeshletHeader32` + `BloomVtx12` + `local_idx`）、`ObjectNode`（`BloomObjectState48` + `ConstraintSpec` 關節範圍）、`AnalyticNode`（`BloomAnalyticPrim32`）與綁定 3D DOF 的 `HudNode`。每種格式均分別輸出 **`[full]`（含全部網格幾何）** 與 **`[scene]`（僅含場景物件、約束、解析原語與 2D HUD）**，避免大塊頂點陣列稀釋語法層本身的結構稅。
2. **雙重數學無損驗證**：
   - **C++17 語意層**：每一份編碼輸出皆即時解碼還原並計算 128-bit `ir_digest` / `hud_digest`，全部 **`100% PASS`**（含 `baseline_glb64` 反向重新量化與 XML 浮點精確往返）。
   - **Ring-0 二進位層**：所有檔案皆送入 `/Users/kuangtalin/Documents/INFO/build/info compress` 產生 `.inf` (`INF1`) 並以獨立零堆積 `/Users/kuangtalin/Documents/INFO/build/info_decomp` 解壓後比對 **SHA-256**，全部 **`100% PASS`**。
3. **兩個必須同時檢視的資訊論指標**：
   - **Intrinsic Packing Density ($\eta = \text{bpb} / 8 \times 100\%$)**：原始位元組中「不可被 Hutter 預測的真實資訊」比例，越低代表冗餘/樣板雜訊越多（例如 W3C SVG HUD 僅 **`6.2%`**）。
   - **Total Algorithmic MDL ($L_{\text{INF1}}$)**：編碼整個等價語意所需的真實壓縮後總位元組（近似 Kolmogorov 複雜度）。兩者併用可避開「亂排或混雜欄位雖有高 `bpb` 卻多佔總 MDL 位元組」的錯覺（印證 `Info.ProofMDL.infp_container_beats_flat_ast` 定理）。

### 參與實測的候選格式架構

| 格式識別 | 架構設計與資料佈局 |
| :--- | :--- |
| **`baseline_glb64`** | 業界基準：glTF JSON (`meshes`/`nodes`/`skins`/`extras`) + Binary `IndustryVtx64` (`64 B/vtx`) + `uint32_t` 索引 + `IndustryRig512` (`512 B/obj`) |
| **`3a_xml_bobj`** | 方案 3A：`<bloom-obj>` 宣告式 XML（3D/約束）+ 獨立 W3C `<svg>`（2D HUD）+ `.bmesh` 二進位幾何段 (`3a_xml_b64` 為單檔 XML Base64 內嵌版) |
| **`3b_svg_scene`** | 方案 3B：單一 W3C `<svg xmlns:b="...">` 在 `<defs><b:scene>` 宣告 3D 物件/約束、`[scene]` 為純 SVG 文字檔、`[full]` 附加 `.bmesh` |
| **`3c_bobj_row` (`BOBR`)** | 方案 3C 執行態/零解析路徑：`32 B` Header 後直接連續擺放 GPU-Direct POD（`BloomVtx12[]`、`BloomObjectState48[]`、`ConstraintSpec[]`、`HudNodeBin[]`），可零解析直接 mmap/memcpy |
| **`3c_bobj_col` (`BOBC`)** | 方案 3C 傳輸/封存路徑：仿 `.infp` 欄位轉置 + ZigZag LEB128 Delta-Varint + `float32`/`f16`/`RGBA8` Byte-Plane 拆軌 + Front-Coded 字串前綴壓縮池 |

---

## §2 核心實測總表：原始體積、資訊密度、`INF1` MDL 與 CPU Decode 延遲

### 2.1 `s3_stage` 實戰場景（`8` 模板 / `64` meshlets / `4,096` vtx / `6,144` tris；`256` 物件 + `64` 解析球 + `128` HUD = `448` 實體）

| 層級 / 格式 | Raw Size | `B / entity` | `INFO` bpb ($\eta\%$) | **`INF1` MDL** | `zlib -9` | `lzma -9` | CPU Decode (`init`) |
| :--- | ---: | ---: | :---: | ---: | ---: | ---: | ---: |
| **`[scene]` baseline_glb64** | `243,792 B` | `544.2` | `0.807` (`10.1%`) | `24,516 B` | `30,063 B` | `23,896 B` | `1,145.0 us` |
| **`[scene]` 3a_xml_bobj** | `104,073 B` | `232.3` | `1.271` (`15.9%`) | `16,540 B` | `22,202 B` | `18,700 B` | `542.4 us` |
| **`[scene]` 3b_svg_scene** | `104,781 B` | `233.9` | `1.266` (`15.8%`) | `16,587 B` | `22,242 B` | `18,744 B` | `548.2 us` |
| **`[scene]` 3c_bobj_row** | `29,067 B` | `64.9` | `3.253` (`40.7%`) | `11,824 B` | `13,113 B` | `10,488 B` | **`6.2 us`** |
| **`[scene]` 3c_bobj_col** | **`23,100 B`** | **`51.6`** | `2.869` (`35.9%`) | **`8,292 B`** | **`8,924 B`** | **`8,192 B`** | **`15.5 us`** |
| **`[full]` baseline_glb64** | `579,664 B` | `1,293.9` | `0.845` (`10.6%`) | `61,003 B` | `94,536 B` | `41,596 B` | `1,187.8 us` |
| **`[full]` 3a_xml_b64** *(XML+Base64)* | `175,744 B` | `392.3` | `2.167` (`27.1%`) | `47,595 B` | `56,037 B` | `39,752 B` | `617.8 us` |
| **`[full]` 3b_svg_scene** | `158,502 B` | `353.8` | `1.998` (`25.0%`) | `39,571 B` | `52,542 B` | `35,080 B` | `542.2 us` |
| **`[full]` 3a_xml_bobj** | `157,778 B` | `352.2` | `2.004` (`25.1%`) | `39,510 B` | `52,483 B` | `34,880 B` | `547.6 us` |
| **`[full]` 3c_bobj_row** | `82,571 B` | `184.3` | `3.374` (`42.2%`) | `34,810 B` | `43,002 B` | `24,472 B` | **`8.8 us`** |
| **`[full]` 3c_bobj_col** | **`74,621 B`** | **`166.6`** | `1.819` (`22.7%`) | **`16,951 B`** | **`18,166 B`** | **`16,224 B`** | **`80.6 us`** |

### 2.2 跨尺度驗證（`s1_micro` `22` 實體 & `s2_work` `128` 實體）

| 規模 / 層級 | `baseline_glb64` (`Raw` / `INF1` / `dec`) | `3a_xml_bobj` (`Raw` / `INF1` / `dec`) | `3b_svg_scene` (`Raw` / `INF1` / `dec`) | `3c_bobj_row` (`Raw` / `INF1` / `dec`) | **`3c_bobj_col` (`Raw` / `INF1` / `dec`)** |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`s1_micro [scene]`** | `7,536 B` / `1,369 B` / `102.2 us` | `4,683 B` / `1,201 B` / `51.0 us` | `4,757 B` / `1,221 B` / `44.7 us` | `1,304 B` / `729 B` / **`0.8 us`** | **`1,024 B`** (`64.7%`) / **`678 B`** / `2.8 us` |
| **`s1_micro [full]`** | `49,520 B` / `7,037 B` / `124.0 us` | `11,404 B` / `4,167 B` / `51.8 us` | `11,494 B` / `4,197 B` / `45.6 us` | `7,992 B` / `3,654 B` / **`1.2 us`** | **`7,430 B`** / **`1,598 B`** / `16.2 us` |
| **`s2_work [scene]`** | `63,401 B` / `7,204 B` / `337.3 us` | `29,266 B` / `5,167 B` / `152.8 us` | `29,486 B` / `5,195 B` / `154.3 us` | `8,171 B` / `3,523 B` / **`2.0 us`** | **`6,451 B`** / **`2,577 B`** / `6.1 us` |
| **`s2_work [full]`** | `231,337 B` / `27,852 B` / `419.8 us` | `56,123 B` / `18,527 B` / `156.0 us` | `56,359 B` / `18,569 B` / `151.4 us` | `34,923 B` / `16,789 B` / **`3.2 us`** | **`32,181 B`** / **`7,941 B`** / `38.7 us` |

**三大定量事實**：
1. **`3A` 與 `3B` 實為同一情報等級（差距 `< 0.5%`）**：在 `s3_stage [scene]` 上，把 XML 與 SVG 合併為單一 SVG (`3B`) 的 `INF1` 是 `16,587 B`，拆開 (`3A`) 是 `16,540 B`，真正消耗 **~84% 原始空間 (`104 KB` 裡只有 `~16.5 KB` 真實資訊)** 的元兇是 **XML 標籤屬性與十進位文字表示法本身**，而非拆檔與否。
2. **`3C Columnar (BOBC)` 擊穿業界與 XML 雙重冗餘牆**：在 `s3_stage [scene]` 上，原始體積僅 `23,100 B`（比 `baseline_glb64` 小 **`10.55×`**、比 `3A/3B` XML/SVG 小 **`4.5×`**）；真實資訊熵 **`INF1 = 8,292 B`**（比 `baseline_glb64` 少 **`2.96×`**、比 `3A/3B` 少 **`2.00×`**），且純壓縮表現徹底壓制所有文字格式（`[full]` `INF1 = 16,951 B`，比 `3A` 的 `39,510 B` 再小 **`2.33×`**！）。
3. **Base64 XML 是最差方案**：`3a_xml_b64` (`175,744 B` / `INF1 47,595 B`) 既放棄零解析直接載入，又把二進位幾何額外膨脹 `~18 KB` 原始體積與 `+8,085 B` `INF1` MDL。

---

## §3 跨場景樣板、熱重載編輯局部性與獨立 2D HUD 檢驗

### 3.1 多場景跨專案樣板冗餘（`info eval-mdl-group` on `G0..G3`）

將 4 個彼此獨立、含不同網格種子/ID/座標的 `s2` 級場景送入 `info eval-mdl-group` 量測「跨場景共享可預測樣板」：

| 格式 (`G0..G3` 4 檔) | Raw 4x 總計 | $\sum K(G_i)$ (`indep`) | 循序聯合 $K(G_{0..3})$ (`joint`) | **跨場景樣板冗餘 (`savings`)** | 冗餘佔比 |
| :--- | ---: | ---: | ---: | ---: | :---: |
| **`baseline_glb64 [scene]`** | `253,813 B` | `28,759.8 B` | `24,989.4 B` | `3,770.5 B` | `13.11%` |
| **`3a_xml_bobj [scene]`** | `117,273 B` | `20,559.9 B` | `16,788.3 B` | **`3,771.6 B`** | **`18.34%`** |
| **`3b_svg_scene [scene]`** | `118,153 B` | `20,645.2 B` | `16,802.7 B` | **`3,842.4 B`** | **`18.61%`** |
| **`3c_bobj_row [scene]`** | `32,685 B` | `13,983.8 B` | `11,876.9 B` | `2,106.9 B` | `15.07%` |
| **`3c_bobj_col [scene]`** | **`25,805 B`** | **`10,231.9 B`** | **`8,801.9 B`** | **`1,430.0 B`** | `13.98%` |
| **`3c_bobj_col [full]`** | **`128,634 B`** | **`29,935.1 B`** | **`17,450.0 B`** | `12,485.1 B` *(幾何族共享)* | `41.71%` |
| **`3a_xml_bobj [full]`** | `224,702 B` | `72,364.6 B` | `51,380.9 B` | `20,983.6 B` | `29.00%` |

在 `[scene]` 層上，`3A/3B` 的跨場景純語法重複冗餘（`3,771.6–3,842.4 B`）是 `3c_bobj_col`（`1,430.0 B`）的 **`2.6×` 以上**——證明就算經過 `INF1` 壓縮，XML/SVG 依然在每個場景檔案重複付出固定「標籤語法結構稅」。

### 3.2 熱重載條件編輯增量（`info ncd S2_v1 -> S2_v2`）

模擬熱更新（修改 3 個物件 DOF、切換 1 個剖切面、改 1 個 HUD 字串、新增 1 顆解析探針）後的條件熵 $K(v_2 \mid v_1)$ 與正規化壓縮距離 `NCD`：

| 格式 (`S2_v1 -> S2_v2` `[scene]`) | `v1` → `v2` (Raw Δ) | 條件增量 $K(v_2 \mid v_1)$ | `NCD` |
| :--- | :---: | ---: | :---: |
| **`baseline_glb64 [scene]`** | `63,401 -> 63,524 B` (`+123 B`) | `218.9 B` | `0.0297` |
| **`3a_xml_bobj [scene]`** | `29,266 -> 29,390 B` (`+124 B`) | `136.5 B` | `0.0251` |
| **`3b_svg_scene [scene]`** | `29,486 -> 29,612 B` (`+126 B`) | `126.9 B` | `0.0236` |
| **`3c_bobj_row [scene]`** | **`8,171 -> 8,220 B` (`+49 B`)** | **`126.1 B`** *(全組最小條件增量)* | `0.0342` |
| **`3c_bobj_col [scene]`** | `6,451 -> 6,489 B` (`+38 B`) | `221.8 B` | `0.0849` |

> **關鍵工程發現（不掩蓋的物理實情）**：Columnar 轉置雖然讓靜態總 MDL 降到最小，但一旦**結構數量增加（如插入 1 個 `AnalyticNode`）**，所有欄位偏移量會整體錯位，因此未分塊的 `BOBC` 條件擴散增量 (`221.8 B`) 反而大於 `BOBR` Row-Major (`126.1 B`)！而在執行態 mmap 緩衝中，`BOBR` 的順序記錄布局同時擁有 **`126.1 B` 最小編輯增量** 與 **`6.2 us`（比 XML/SVG 快 `88×`）即時熱更新速度**。

### 3.3 獨立 2D HUD 實測（`s3_stage` 128 HUD 節點：W3C SVG vs Binary Row vs Binary Columnar）

直接回應「W3C SVG 本身的資訊密度究竟高不高」——將 `s3_stage` 的 `128` 個 HUD 控制項（面板/按鈕/滑桿/標籤+3D 綁定）剝離 3D 單獨送入 `../INFO`：

| 2D HUD 格式 (`128` 節點) | Raw Size (`B/node`) | `INFO` bpb ($\eta\%$) | **`INF1` MDL** (`B/node`) | `zlib -9` | `lzma -9` | CPU Decode (`init`) |
| :--- | ---: | :---: | ---: | ---: | ---: | ---: |
| **`hud_svg` (W3C `<svg>`)** | `25,286 B` (`197.5`) | **`0.500` (`6.2%`)** | `1,591 B` (`12.4`) | `2,478 B` | `2,048 B` | `106.0 us` |
| **`hud_bui_row` (`BUIR`)** | `6,842 B` (`53.5`) | `1.520` (`19.0%`) | `1,312 B` (`10.3`) | `1,575 B` | `1,012 B` | **`2.8 us`** |
| **`hud_bui_col` (`BUIC`)** | **`4,967 B` (`38.8`)** | `0.871` (`10.9%`) | **`556 B` (`4.3`)** | **`593 B`** | **`532 B`** | **`6.2 us`** |

這組數據幾乎一錘定音：
- W3C SVG 在 2D UI 描述上的 **真實資訊密度只有 `6.2%` (`0.500 bpb`)** —— **`93.8%` 的原始位元組都是重複 XML 語法噪音**（`<g id="..." data-kind="..." data-bind="..."><rect x="..." .../><text ...>`），連 `lzma -9` (`2,048 B`) 與 `INFO` (`1,591 B`) 都無法消除它高出 **`hud_bui_col` (`556 B`) 高達 `2.86×` 的結構冗餘**！
- 同時，Binary UI (`BUIR`/`BUIC`) 讓純 CPU `init` 反序列化快 **`17×–38×`**（`2.8–6.2 us` vs `106.0 us`）。

---

## §4 `../INFO` 量化裁決與 BLOOM vNext 雙格式定位定案建議

### 裁決結論：**選擇 3C（二進位原生雙態：`BOBC` + `BOBR`），正式將 SVG 與 glTF 降級為「選配前端匯入格式」**

根據 `../INFO` 的實證數據，**3A (`<bloom-obj>`) 與 3B (`<svg><b:scene>`) 的差別微乎其微，真正的分水嶺在於「XML/SVG 語法層 vs 二進位結構」**：

1. **為何捨棄 SVG / XML 作為引擎核心 SSOT？**  
   - **資訊論敗訴**：SVG 在 HUD (`6.2%` 密度, `INF1 1,591 B vs 556 B`) 與場景 (`15.8–15.9%` 密度, `INF1 ~16.5 KB vs 8.3 KB`) 兩側，真實 MDL 均比 `BOBC` 膨脹 **`2.0×–2.86×`**，Raw 體積膨脹 **`4.5×–5.1×`**。
   - **效能稅無謂**：在 Apple M1 上把一個場景從 `init` XML 解析成記憶體結構要 `542–548 us`，直接讀二進位 `BOBR` 僅 `6.2–8.8 us`（快 **`62×–88×`**）。
   - **相容性已足夠透過薄匯入器解決**：保留極薄工具層匯入器（`.svg -> BOBC/BOBR`、`.glb -> Meshlet BOBC/BOBR`）與瀏覽器預覽導出，就能享有設計工具相容性，完全不必讓核心 GPU 引擎背負高達 **~94% 的語法冗餘稅**。

2. **為何核心應採「3C 雙形態 (`BOBC` Wire + `BOBR` Memory/Hot-Delta)」？**  
   - **封裝與磁碟 / 網路傳遞 SSOT → `3C_bobj_col` (`BOBC` ± `INF1`)**：全系列最低真實 MDL（`s3 [scene] = 8,292 B` / `[full] = 16,951 B`、`hud = 556 B`），跨場景樣板冗餘最小。
   - **執行期記憶體映射 / `StateStore` / 熱重載差分 → `3C_bobj_row` (`BOBR`)**：零解析（`6.2–8.8 us`）、可直接作為 GPU `Shared|WC` Upload 鏡像，且在單點狀態/物件熱更新時擁有全場最低 **`126.1 B`** 條件編輯熵（避免 `BOBC` 欄位插入位移效應）。
   - **完全統一 2D + 3D 核心介面**：無論 2D UI (`HudNodeBin`) 還是 3D 物件 (`BloomVtx12` + `BloomObjectState48` + `BloomAnalyticPrim32`)，核心執行態不再混用 XML DOM 與 GPU POD，徹底收斂為單一 **Constraint-Native Binary SSOT**。
