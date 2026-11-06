# BLOOM (v1.1.0) — 64-Byte 定點物理光學、程序化資產與影視預演引擎

**BLOOM**（原專案代號 `flow_game`）是一套以 100% 純 C17（`-std=c17 -Wall -Wextra -Werror -O2`）與 Apple Metal 建構的**定點物理模擬、程序化 SDF/CSG 幾何渲染、64-Byte UI 排版與影視預演（Previz）管線基礎庫**，底層建構於 [`FLOW v4`](../FLOW)（`libflow_rt.a`）與 `Ricardian v4`（[`include/ricardian.h`](include/ricardian.h)）。

---

## 專案架構與設計核心

BLOOM 採用 **「算子與參數分離」** 的設計模式：將底層物理運動、光學折射與微分幾何固定為預編譯的 C17 / Metal 算子，並將場景資產、材質風格、攝影機鏡位與介面節點統一表達為 **`64-Byte (128-Hex)` Cache-Line 對齊描述子（Descriptors）**，在執行期直接路由而無須重新編譯或載入大型外部貼圖資產：

- **100% Pure C17 / Zero Float Drift / Zero Heap**：核心模擬邏輯皆為純 C17（`FloatOps: 0 | HeapCalls: 0`），採用 Q16.16 / Q48.16 定點數與 2-Jet 自動微分（`FlowJet2`），確保跨平台、跨執行緒與網路同步具備 **Bit-for-Bit 確定性**，並編譯為靜態函式庫 `build/libbloom.a` 供下游專案直接連結。
- **零編譯期 `.lean` $\leftrightarrow$ `FLOW` 64-Byte ABI 直接路由 (`include/flow_lean_abi.h`)**：
  - 執行期直接解析 `research/*.lean` 形式化規格檔（`flow_lean_abi_load_file`），提取常數與狀態轉移條件並綁定為 64-byte `FlowLeanClause64` 算子描述子。
  - 直接驅動底層 `FLOW` 核心算子（`FlowAbiPoly1D_eval_hysteresis` + `flow_cubical_step_neighbor` Kan 同倫驗證、`flow_jet2_ema_step` 二階阻尼、守恆比例拆分、区间鉗制與配額閘門），無須在 C 端重複手寫 `if/else` 膠水碼。
- **原生 64-Byte 外部資產與資料驅動 CSG 管線 (`include/flow_assets.h` / `include/flow_gpu_abi.h`)**：
  - **純 Wire-Format 封包標頭與執行期解耦**：公開封包與跨語言 ABI 標頭（`flow_lean_abi.h`、`flow_gpu_abi.h`、`flow_svg_ui.h`、`flow_wallpaper.h`）僅依賴標準 `<stdint.h>` / `<stdbool.h>`，將 `FLOW` 執行期算子封裝於 `.c` 實作層，支援跨語言與 GPU Shader Zero-Copy 映射。
  - **RIFF/WAVE (`.wav`) 載入與多軌相對論都卜勒混音 (`FlowWavAssetDesc64` / `FlowAudioVoice64`)**：支援 8/16/24/32-bit PCM 與 IEEE-754 float32 音訊載入、立體聲相位平移與相對論都卜勒變速混音。
  - **glTF 2.0 Binary (`.glb`) 3D 模型解析與資料驅動 GPU CSG 指令串流 (`FlowGlbMeshDesc64` / `FlowGpuCsgOp64`)**：以純整數位移（`flow_ieee754_bits_to_q16`，0 浮點指令）解析 `.glb` 二進位頂點緩衝區，並可編譯為 64-byte 資料驅動 CSG 布林指令串流（Union / Subtract / Intersect / Smooth-Union，`flow_asset_compile_glb_to_csg_op64`、`flow_asset_eval_csg_program_q16`）。
  - **標準 W3C SVG `<path>` 圖示編譯 (`flow_asset_compile_svg_icon64`)**：支援解析標準美術匯出之 `<path d="M... L... H... V... Z"/>` 與 `viewBox` 為 64-byte GPU 向量圖示（`FlowGpuGlyphDesc64`）。
- **64-Byte Cache-Line Univalence 與 $O(1)$ 狀態回滾**：引擎狀態、Prefab 資產（`FlowPrefabDesc64`）、色票配置（`FlowMoodboardPrefab64`）、攝影機分鏡（`FlowCinemaShot64`）與 GPU ABI 均對齊 64-byte Cache Line，支援透過 64B Shadow 雙緩衝進行常數時間狀態回滾（Atomic Rollback）。
- **統一 64-Byte Engine Bus (`FlowEngineBus1K`)**：CPU 主機端與 GPU 管線透過 16 個 64B 槽位（共 `1024 Bytes`）直接映射至 Metal / Vulkan `buffer(3)`。
- **程序化 CSG/SDF 舞台與參考圖色票萃取 (`FlowMoodboardPrefab64`)**：
  - 支援從參考影像（`.jpg` / `.png` / `.ppm`）或 `128-Hex` 參數碼萃取色票與材質參數，轉換為 `FlowPrefabDesc64`（結構）與 `FlowArtStyle64`（表面光學屬性）。
  - 內建四組程序化物理光學場景：**Stage 0 相對論飛行器與黑體輻射腔**、**Stage 1 車窗凝露與 2-Jet 水滴折射**、**Stage 2 3D 空間雜湊點雲迴廊**、**Stage 3 等軸 3D CSG 地質與水體剖面**。
- **POSIX Pipe FFmpeg 影視預演與 CMX 3600 `.edl` 分鏡輸出 (`FlowCinemaShot64`)**：
  - 支援 `24 fps` 鎖幀渲染搭配每幀 `2000` 取樣的 `48kHz` 純整數立體聲合成音軌，透過 POSIX Pipe 直接串流至 `ffmpeg` 輸出 `.mp4` 預演影片與剪輯軟體（DaVinci Resolve / Premiere）相容的 `.edl` 分鏡表，避免產生中間暫存圖檔。
- **宣告式 64B SVG-UI、CJK IME 輸入法橋接、跨平台 HiDPI 視窗與單通道 LaTeX SDF**：支援標準 SVG `viewBox` / `<rect>` / `<circle>` 像素座標自動正規化、`O(N)` 64-Byte Flexbox/Stack 容器排版（`FlowUiLayoutContainer64`）、12 通道通用狀態暫存器庫（`FlowUiRegisterBank64`）、64-Byte 內嵌文字輸入元件（`FlowUiTextInput64`）、64-Byte 系統字型圖集描述子（`FlowFontAtlasGlyph64`）與 CJK 輸入法組字橋接（`FlowImeComposition64`）、跨平台 HiDPI 視窗與原生檔案對話框描述子（`FlowHostWindowDesc64` / `FlowNativeDialogDesc64`）、Beer-Lambert 透射光學 UI、無字型圖集之 64B LaTeX SDF 數學公式盒（`FlowLatexBoxNode64`）、過場運鏡控制器（`FlowNarrativeDirector64`），以及 `IOSurface` 零拷貝 OBS 擷取與獨立 `5KB` WebGL2 動態桌布匯出。

---

## 工具用法 (Tool Usage)

### 1. 環境需求與建置

本專案依賴 Clang C17 編譯器與 Apple Metal API，並引用上層目錄之工具鏈：
- `FLOW` 執行期函式庫：`../FLOW/build/libflow_rt.a`
- `FLOWY` Gate 0 跨層稽核工具：`../flowy/build/flowy`
- `FFmpeg`（選用，用於 `--cinema-reel` / `--cinema-out` 預演影片輸出）：`/opt/homebrew/bin/ffmpeg`

#### 快速建置指令

```bash
# 編譯所有模組並執行 8 道驗證閘門（Gate 0 FLOWY 跨層稽核 + Gate 1..6 共 53 項單元測試 + Gate 7 4K Metal 基準測試）
make test

# 僅編譯核心二進位執行檔（build/bloom）
make

# 查詢引擎版本（BLOOM v1.1.0）
./build/bloom --version

# 執行 Gate 0 FLOWY 形式化與硬體稽核（64B 矩陣、LLVM IR、Metal AIR、Lean 4 定理證明驗證）
make verify-flowy

# 封裝獨立可執行之 macOS 應用程式套件（build/BLOOM.app）
make bundle-macos

# 清除所有建置產物
make clean
```

---

### 2. 命令列工具選項 (`./build/bloom`)

編譯完成後，執行檔 `./build/bloom` 支援**互動視窗、參考圖色票匯入、影視預演輸出、動態桌布與無頭伺服器模擬**五種模式：

```bash
# 1. 啟動即時互動式 Metal 視窗應用（按 B 鍵即時切換 4 組程序化光學場景）
./build/bloom
# 或直接指定初始場景 (0: 太空飛行器, 1: 車窗水滴折射, 2: 點雲粒子迴廊, 3: 微縮地質水體剖面)
./build/bloom --mood 3

# 2. [色票與風格萃取] 匯入參考圖 (.jpg/.png/.ppm) 或 128-Hex 轉換為 64B Prefab 並輸出 4K 渲染圖
./build/bloom --import-mood path/to/reference.jpg --mood-out build/lookbook_4k.ppm

# 3. 批次輸出三組程序化場景之 4K 渲染圖 (build/mood_1..3_*.ppm)
./build/bloom --mood-gallery

# 4. [影視預演管線] 透過 Pipe 串流 FFmpeg 輸出 24fps 音畫同步預演影片 (.mp4) 與 CMX 3600 分鏡表 (.edl)
./build/bloom --cinema-reel
# 或自訂輸出 MP4 路徑
./build/bloom --cinema-out build/my_film_previz.mp4

# 5. 指定目標幀率與切換 VSync（測試 GPU 吞吐量或鎖定 120Hz）
./build/bloom --fps 120 --vsync 0

# 6. 執行 4K (3840x2160) Metal 單通道基準測試（略過磁碟寫入）
./build/bloom --bench

# 7. 執行 4K 基準測試並匯出 4 鏡位 4K PPM 分鏡圖（build/vcam_0..3_*.ppm）
./build/bloom --storyboard

# 8. 啟動無頭伺服器模擬（6,000 ticks @ 60Hz，0 GPU / 0 MB VRAM，10 萬實體調度）
./build/bloom --headless

# 9. 啟動 macOS 桌面背景動態桌布或匯出 5KB WebGL2 網頁桌布
./build/bloom --wallpaper --wallpaper-audio
./build/bloom --wallpaper-export build/flow_wallpaper_web.html
```

---

### 3. 各測試子模組單獨驗證

除 `make test` 外，亦可針對各核心子系統獨立執行單元測試：

```bash
./build/test-bloom              # Gate 1：連續碰撞防穿隧、相對論度規、UDP 快照、QSBR 與調速器 (10 項測試)
./build/test-prefab-ssot        # Gate 2：64B CSG/SDF 預製件、材質參數與參考圖轉 Prefab (9 項測試)
./build/test-procedural-assets  # Gate 3：程序化幾何、SVG HUD、物件互動屬性與視覺回饋狀態機 (7 項測試)
./build/test-cine-camera        # Gate 4：FlowCineRig64 攝影機、薄透鏡景深、24fps EDL 與 48kHz 鎖幀 (7 項測試)
./build/test-wallpaper-engine   # Gate 5：動態桌布雙門檻遲滯休眠、低電量調速與 Web 匯出 (5 項測試)
./build/test-svg-ui             # Gate 6：宣告式 64B SVG-UI、Flexbox 排版、通用暫存器、文字輸入、LaTeX SDF 與過場控制 (15 項測試)
```

---

## 操作與 API 速查表 (Authoring Cheat Sheet)

### 1. 即時互動快捷鍵與滑鼠操作總表 (`./build/bloom`)

| 按鍵 / 操作 | 所屬子系統 | 即時功能說明 |
| :--- | :--- | :--- |
| **`B`** | **程序化場景切換** | **循環切換 `Stage 0 (太空飛行器)` $\to$ `Stage 1 (車窗水滴折射)` $\to$ `Stage 2 (點雲粒子迴廊)` $\to$ `Stage 3 (地質水體剖面)`** |
| **`W` / `S` / `A` / `D`** | 飛行物理控制 | 控制飛行器俯仰（Pitch）、偏航（Yaw）、滾轉（Roll）與推進節流閥 |
| **`Space` / `Tab`** | 推進加速 / 脈衝 | `Space` 開啟 `3.0x` 推力加速；`Tab` 觸發脈衝事件與時間輪排程 |
| **`1` / `2` / `3` / `4` / `C`** | `FlowCineRig64` 攝影機 | 切換 4 組虛擬攝影機鏡位（`135mm` 望遠 / `24mm` 追尾 / `35mm` 掠過 / `18mm` 座艙），`C` 循環切換 |
| **`F` / `R`** | 材質預設與隨機種子 | `F` 循環切換 4 組表面光學材質預設；`R` 更新 64-bit 程序化種子 |
| **`T`** | 物件屬性描述子 | 循環切換 4 組 `FlowObjectTicket64` 互動物理參數預設集 |
| **`X`** | 熱力學冷卻控制 | 觸發 `-120K` 降溫冷卻程序（含氣動音效合成與 6D 鏡頭震動回饋） |
| **`J`** | 視覺回饋展演 | 觸發 4 階段 `FlowDopamineJackpot64` 特效轉場（暗角聚焦 $\to$ 頓幀 $\to$ 高光擴散 $\to$ 135mm 特寫） |
| **`K`** | **過場運鏡控制器** | **觸發 `FlowNarrativeDirector64`（鎖定輸入 + 2.39:1 遮幅 + 頓幀 + SDF X-Ray 剖面 + 轉場遮罩）** |
| **`V` / `G`** | **VSync 與目標幀率** | **`V` 切換 `CAMetalLayer` VSync；`G` 循環切換 `60` $\to$ `120` $\to$ `Uncapped` $\to$ `30` FPS 幀率目標** |
| **`M`** | **音訊合成開關** | **切換即時程序化音訊合成靜音 / 開啟** |
| **滑鼠游標移動** | **3D 游標 2-Jet 視差** | **鏡頭視角平滑追隨游標角度轉向（支援微縮剖面與車窗視差觀察）** |
| **滑鼠按住拖曳** | **視角旋轉 / X-Ray 剖面** | **拖曳滑鼠旋轉視角；拖曳 UI 的 X-Ray 滑桿即時推進 3D 剖面位置** |
| **滑鼠左鍵點擊** | **推進脈衝 / UI 點擊** | **於 3D 空間點擊觸發推進脈衝與鏡頭反衝；點擊下方按鈕觸發對應 UI 功能** |
| **滑鼠滾輪滾動** | **推進節流閥調速** | **滾輪向上加速，滾輪向下減速** |
| **右鍵點擊 (`Right-Click`)** | **Prefab 狀態還原** | **取消拖曳操作或將 `FlowPrefabInstance64` 還原至 SSOT 預設值** |

---

### 2. C17 基礎庫 API 呼叫範例

```c
#include "bloom.h"

/* (A) [風格與資產萃取] 從 RGB 緩衝區自動判別場景類型、萃取 4 組色票並轉換為 64B Prefab & ArtStyle 描述子 */
flow_moodboard_extract_from_rgb_buffer64(rgb_buf, w, h, w * 3U, FLOW_MOOD_STAGE_AUTO_DETECT, &moodboard);
flow_moodboard_to_prefab_and_style64(&moodboard, &prefab_ssot, &art_style_ssot);
flow_moodboard_encode_hex128(&moodboard, share_hex128);

/* (B) [影視預演管線] 建立 24fps 四幕 EDL 分鏡表並合成每幀 2000 取樣之 48kHz 立體聲 PCM */
flow_cine_build_default_reel64(cinema_shots, 24U);
flow_cine_format_smpte_timecode(cinema_shots[0].start_frame, 24U, smpte_tc_str);
flow_cine_synth_lockstep_audio48k(&cinema_shots[0], frame_idx, pcm_stereo_buf, 2000U);

/* (C) 解析標準 SVG 向量檔為 64B UI 節點樹，並執行單趟 O(N) Flexbox/Stack 自動排版與通用暫存器綁定 */
size_t n_ui = flow_svg_ui_compile_file("specs/cockpit_hud.svg", nodes, FLOW_SVG_UI_MAX_NODES, &dom);
flow_ui_layout_container_init64(&flex_row, FLOW_UI_LAYOUT_ROW, FLOW_UI_JUSTIFY_SPACE_BETWEEN,
                                FLOW_UI_ALIGN_CENTER, 0, -20480, 28000, 2400, 512, 256);
flow_ui_layout_container_apply64(&flex_row, nodes, n_ui, 0);
flow_ui_register_bank_set_q16(&reg_bank, 1U, 49152);
flow_svg_ui_bind_register_bank64(nodes, n_ui, &reg_bank);

/* (D) 單通道 DFA 解析 LaTeX 數學公式為無字型圖集之 64B SDF 幾何盒 */
size_t n_tex = flow_latex_dfa_compile_boxes64("$\\gamma = \\frac{1}{\\sqrt{1 - \\beta^2}}$",
                                              -25559, -20971, 2048, 0xFFD830U,
                                              latex_boxes, 16U, NULL, NULL);

/* (E) 觸發腳本化運鏡特寫與場景過渡遮罩 */
flow_director_trigger_story_finisher64(&director, 0x777U, FLOW_PREFAB_ORBITAL_CITADEL,
                                       FLOW_VCAM_ORBIT_TELEPHOTO, FLOW_TRANS_LIQUID_GLASS_WAVE, 1024);
flow_director_trigger_scene_transition64(&director, 3U, FLOW_STAGE_LAYER_1_RELATIVISTIC,
                                         FLOW_TRANS_IRIS_APERTURE, 320U);

/* (F) 固定步長物理與渲染解耦步進：在不同目標顯示幀率下維持物理狀態 Bit-Identical */
uint32_t substeps = flow_game_step_fixed_accumulator(&engine, measured_frame_us, &accum_us, &alpha_q16);
```
