# BLOOM (v1.0.0) — 零折舊 64-Byte 物理光學虛擬機、跨平台遊戲與影視母帶基礎庫

**BLOOM**（原專案代號 `flow_game` 已正式棄用並遷移至本倉庫）是一套從零建構、100% 純 C17（`-std=c17 -Wall -Wextra -Werror -O2`）與 Apple Metal Compute / SDL3_GPU 的**零折舊（Zero-Depreciation）物理光學虛擬機、跨平台遊戲引擎、時尚素材筆記本（Moodboard-to-Prefab）編譯器與 FFmpeg 影視母帶管線**，底層建構於 `FLOW v4`（`libflow_rt.a`）與 `Ricardian v4`（`ricardian.h`）。

---

## 專案定位：為什麼 BLOOM 是「免折舊」的基礎庫？

傳統創意與引擎軟體（如 Office 美工圖案、威力導演特效包、Photoshop 時代濾鏡、Canva 靜態模板庫，或綁定龐大靜態資產的傳統遊戲引擎）往往將「當下的流行審美」寫死為靜態素材，每隔 3 年審美更迭即面臨資產報銷與重工。

**BLOOM 的核心定位**是只封裝**「永不隨時間折舊的物理定律、微分幾何算子與 64-Byte Cache-Line 交易代數」**，並將所有瞬態審美外推為外部輸入的 **`64-Byte (128-Hex)` 參數種子**：

- **100% Pure C17 / Zero Float Drift / Zero Heap**：全核心計算皆為純 C17（`FloatOps: 0 | HeapCalls: 0`），全面採用 Q16.16 / Q48.16 定點數與 2-Jet 自動微分（`FlowJet2`），保證跨平台、跨執行緒與跨網路連線 **Bit-for-Bit 100% 確定性**。
- **64-Byte Univalence 同構與 $O(1)$ 交易回滾**：所有引擎狀態、Prefab 資產、Moodboard 色票筆記本、攝影機分鏡（`FlowCinemaShot64`）與 GPU ABI 均嚴格對齊 64-byte Cache Line（與 `RicardianStateV4` 雙射同構），透過 64B Shadow 雙緩衝實現零虛擬機開銷的原子回滾（Atomic Rollback）。
- **統一 64-Byte Engine Bus (`FlowEngineBus1K`)**：主機端與 GPU 管線透過 16 個 64B 槽位（共 `1024 Bytes`）直接映射至 Metal / Vulkan `buffer(3)`。
- **四大物理光學舞台與 Moodboard-to-Prefab 直轉工作流 (`FlowMoodboardPrefab64`)**：
  - 支援將任意靈感參考圖（`.jpg` / `.png` / `.ppm`）或 `128-Hex` 分享碼直接編譯為 `FlowPrefabDesc64` 與 `FlowArtStyle64` 單一真實來源（SSOT），免除手刻 Pixel、3D 建模與 AE 粒子預渲染的交接稅。
  - 內建四大永不過時的即時物理光學算子：**Stage 0 相對論星際母艦與 800K 黑體腔**、**Stage 1 秋日車窗微米凝露與 2-Jet 水滴折射**、**Stage 2 無限 3D 空間雜湊點雲迴廊與星光粒子海**、**Stage 3 等軸 3D CSG 地質/海水剖面微縮世界**。
- **零暫存檔 FFmpeg 影視母帶與 CMX 3600 `.edl` 分鏡管線 (`FlowCinemaShot64`)**：
  - 核心以 `24 fps` 嚴格鎖幀（Frame-Accurate Lockstep）搭配每幀精確 `2000` 取樣的 `48kHz` 純整數立體聲合成音軌，透過 POSIX Pipe 直接串流至 `ffmpeg` 輸出 `H.264 / ProRes` 電影母帶與剪輯軟體（DaVinci Resolve / Premiere）通用的 `.edl` 分鏡表，零 `.ppm` 磁碟垃圾、零 Codec 折舊包袱。
- **宣告式物理液態玻璃 SVG-UI、單通道 LaTeX SDF 與跨平台 JSI / OBS 零拷貝橋接**：支援 Beer-Lambert 光學透射率、零字型圖集 64B LaTeX SDF 公式盒（`FlowLatexBoxNode64`）、Switch HD Rumble / Steam Deck 陀螺儀橋接、一鍵劇情殺/過渡動畫導演（`FlowNarrativeDirector64`），以及 `IOSurface` 零拷貝 OBS 直播與 `5KB` 零資產 WebGL2 動態桌布匯出。

---

## 工具用法 (Tool Usage)

### 1. 環境需求與建置

本專案依賴 Clang C17 編譯器與 Apple Metal API，並會自動引用上層目錄之常駐工具：
- `FLOW` 執行期函式庫：`../FLOW/build/libflow_rt.a`
- `FLOWY` 形式化輔助工具：`../flowy/build/flowy`
- `FFmpeg`（選用，用於 `--cinema-reel` / `--cinema-out` 影視母帶壓製）：`/opt/homebrew/bin/ffmpeg`

#### 快速建置指令

```bash
# 編譯所有模組並執行 8 大主權品質閘門（Gate 0 FLOWY 跨層審計 + Gate 1..6 共 53 項單元測試 + Gate 7 4K Metal 基準測試）
make test

# 僅編譯核心二進位執行檔（build/bloom）
make

# 查詢引擎版本（BLOOM v1.0.0）
./build/bloom --version

# 執行 Gate 0 FLOWY 形式化審查（64B 矩陣、LLVM IR、Metal AIR、Lean 4 定理證明快取驗證）
make verify-flowy

# 清除所有建置產物
make clean
```

---

### 2. 命令列工具選項 (`./build/bloom`)

編譯完成後，執行檔 `./build/bloom` 涵蓋**互動遊戲、時尚 Moodboard 素材匯入、影視母帶輸出、動態桌布與無頭伺服器**五大工作流：

```bash
# 1. 啟動即時互動式 Metal 視窗應用（按 B 鍵即時切換 4 大物理光學舞台）
./build/bloom
# 或直接指定初始舞台 (0: 太空母艦, 1: 秋日車窗水痕, 2: 無限粒子迴廊, 3: 微縮剖面雪岸)
./build/bloom --mood 3

# 2. [素材發想 / 時尚筆記本] 直接匯入任意參考圖 (.jpg/.png/.ppm) 或 128-Hex 編譯為 64B Prefab 並輸出 4K 渲染圖
./build/bloom --import-mood path/to/inspiration.jpg --mood-out build/lookbook_4k.ppm

# 3. 一鍵生成三大物理光學舞台 4K Lookbook 畫廊 (build/mood_1..3_*.ppm)
./build/bloom --mood-gallery

# 4. [影視製作 / 預演母帶] 零暫存檔直通 FFmpeg 輸出 24fps 音畫同步電影母帶 (.mp4) + 好萊塢 CMX 3600 分鏡表 (.edl)
./build/bloom --cinema-reel
# 或自訂輸出 MP4 路徑
./build/bloom --cinema-out build/my_film_previz.mp4

# 5. 指定目標幀率與關閉 VSync（測試極限 Uncapped GPU 吞吐量或鎖定 ProMotion 120Hz）
./build/bloom --fps 120 --vsync 0

# 6. 執行 4K (3840x2160) Metal 單通道基準測試（純 GPU/CPU 測速，略過硬碟寫入）
./build/bloom --bench

# 7. 執行 4K 基準測試並匯出 4 鏡位 4K PPM 分鏡圖（build/vcam_0..3_*.ppm）
./build/bloom --storyboard

# 8. 啟動無頭權威伺服器模擬（6,000 ticks @ 60Hz，0 GPU / 0 MB VRAM，10 萬並發實體調度）
./build/bloom --headless

# 9. 啟動 macOS 原生桌面背景動態桌布與匯出 5KB 零資產 WebGL2 網頁桌布
./build/bloom --wallpaper --wallpaper-audio
./build/bloom --wallpaper-export build/flow_wallpaper_web.html
```

---

### 3. 各測試子模組單獨驗證

除 `make test` 外，亦可針對各核心子系統獨立執行單元測試：

```bash
./build/test-bloom              # Gate 1：物理防穿隧、相對論度規、UDP 快照、QSBR 與調速器 (10 項測試)
./build/test-prefab-ssot        # Gate 2：64B CSG/SDF 預製件、時尚放樣、Moodboard 圖片直轉 Prefab (9 項測試)
./build/test-procedural-assets  # Gate 3：零 Token 幾何基因組、SVG HUD、物件工單與多巴胺經濟 (7 項測試)
./build/test-cine-camera        # Gate 4：FlowCineRig64 攝影機、薄透鏡景深、24fps EDL 與 48kHz 鎖幀 (7 項測試)
./build/test-wallpaper-engine   # Gate 5：動態桌布雙門檻遲滯休眠、電池 Eco 調速與 Web 匯出 (5 項測試)
./build/test-svg-ui             # Gate 6：宣告式 64B SVG-UI、物理玻璃、LaTeX SDF、跨平台 JSI 與劇情殺導演 (15 項測試)
```

---

## 遊戲、素材與影視製作速查表 (Authoring Cheat Sheet)

### 1. 即時互動快捷鍵與滑鼠操作總表 (`./build/bloom`)

| 按鍵 / 操作 | 所屬子系統 | 即時功能說明 |
| :--- | :--- | :--- |
| **`B`** | **Moodboard 四大舞台** | **即時循環切換 `Stage 0 (太空母艦)` $\to$ `Stage 1 (秋日車窗水痕)` $\to$ `Stage 2 (無限粒子迴廊)` $\to$ `Stage 3 (Winter Shore 微縮剖面島)`** |
| **`W` / `S` / `A` / `D`** | 相對論飛行物理 | 控制攔截機俯仰（Pitch）、偏航（Yaw）、滾轉（Roll）與推進節流閥 |
| **`Space` / `Tab`** | 相對論衝刺 / 武器 | `Space` 開啟 `3.0x` 極限推力；`Tab` 觸發武器脈衝與時間輪事件 |
| **`1` / `2` / `3` / `4` / `C`** | `FlowCineRig64` 攝影機 | 切換 4 組電影光學鏡位（`135mm` 望遠 / `24mm` 追尾 / `35mm` 掠過 / `18mm` 戰術座艙），`C` 循環切換 |
| **`F` / `R`** | 程序化塗裝與母種子 | `F` 循環切換 4 款高訂艦體塗裝（藍寶石 / 淬火藍寶 / 金繼大馬士革 / 帝王翡翠）；`R` 抽取新 64-bit 母種子 |
| **`T`** | 四維原子物件工單 | 循環切換 4 組跨流派 `FlowObjectTicket64` 預設集（恐怖手電筒 / 槓桿攀岩 / 戰術潛行 / 高速競技場） |
| **`X`** | 主權散熱守門員 | 觸發 `-120K` 熱能緊急洩壓（Thermal Purge ASMR 氣動噴流聲 + 6D 鏡頭震動） |
| **`J`** | 多巴胺開獎經濟 | 觸發 4 階段 `FlowDopamineJackpot64`（懸念暗角 $\to$ 頓幀 $\to$ 金光綻放 $\to$ 135mm 特寫） |
| **`K`** | **劇情殺 & 過渡動畫** | **一鍵觸發 `FlowNarrativeDirector64`（鎖操作 + 鎖血下限 + 2.39:1 黑邊 + 頓幀 + SDF X-Ray 破核 + 轉場遮罩）** |
| **`V` / `G`** | **動態 VSync & FPS** | **`V` 即時切換 `CAMetalLayer` VSync；`G` 循環切換 `60` $\to$ `120` $\to$ `Uncapped (240+)` $\to$ `30` FPS 調速預算** |
| **`M`** | **音訊合成控制** | **即時切換引擎音效靜音 / 開啟（超柔和 1/f² 棕噪音深空環境音）** |
| **滑鼠游標移動** | **3D 游標 2-Jet 視差** | **鏡頭視角平滑追隨游標角度即時轉向（支援微縮模型與車窗視差觀察）** |
| **滑鼠按住拖曳** | **視角旋轉 / X-Ray 剖面** | **拖曳滑鼠旋轉視角；拖曳 UI 的 X-Ray 滑桿即時推進微縮模型或母艦的 3D 剖面刀** |
| **滑鼠左鍵點擊** | **推進脈衝 / UI 點擊** | **於 3D 空間點擊觸發推進脈衝與鏡頭反衝；點擊下方液態玻璃按鈕觸發 UI 功能** |
| **滑鼠滾輪滾動** | **推進節流閥調速** | **滾輪向上推進加速，滾輪向下平滑減速** |
| **右鍵點擊 (`Right-Click`)** | **Prefab 非破壞性還原** | **Blender 式取消模態拖拽 / Godot 式一鍵將 `FlowPrefabInstance64` 還原至唯讀 SSOT 預設值** |

---

### 2. C17 基礎庫「一行呼叫」API 範例

```c
#include "bloom.h"

/* (A) [素材筆記本] 從任意 RGB 影像緩衝區自動判別舞台流派、萃取 4 組色票並編譯為 64B Prefab & ArtStyle SSOT */
flow_moodboard_extract_from_rgb_buffer64(rgb_buf, w, h, w * 3U, FLOW_MOOD_STAGE_AUTO_DETECT, &moodboard);
flow_moodboard_to_prefab_and_style64(&moodboard, &prefab_ssot, &art_style_ssot);
flow_moodboard_encode_hex128(&moodboard, share_hex128);

/* (B) [影視母帶管線] 建立 24fps 四幕電影 EDL 分鏡表並合成每幀鎖定 2000 取樣之 48kHz 立體聲 PCM */
flow_cine_build_default_reel64(cinema_shots, 24U);
flow_cine_format_smpte_timecode(cinema_shots[0].start_frame, 24U, smpte_tc_str);
flow_cine_synth_lockstep_audio48k(&cinema_shots[0], frame_idx, pcm_stereo_buf, 2000U);

/* (C) 一行編譯 SVG 液態玻璃 UI 組件樹 (支援 fill="#RRGGBB" Beer-Lambert 透射光學) */
size_t n_ui = flow_svg_ui_compile_file("specs/cockpit_hud.svg", nodes, FLOW_SVG_UI_MAX_NODES, &dom);

/* (D) 一行編譯單通道 DFA Markdown/LaTeX 數學公式為零字型圖集 64B SDF 幾何盒 */
size_t n_tex = flow_latex_dfa_compile_boxes64("$\\gamma = \\frac{1}{\\sqrt{1 - \\beta^2}}$",
                                              -25559, -20971, 2048, 0xFFD830U,
                                              latex_boxes, 16U, NULL, NULL);

/* (E) 一行觸發「劇情殺 (Scripted Story Finisher)」與獨立「過渡動畫 (Scene Transition)」 */
flow_director_trigger_story_finisher64(&director, 0x777U, FLOW_PREFAB_ORBITAL_CITADEL,
                                       FLOW_VCAM_ORBIT_TELEPHOTO, FLOW_TRANS_LIQUID_GLASS_WAVE, 1024);
flow_director_trigger_scene_transition64(&director, 3U, FLOW_STAGE_LAYER_1_RELATIVISTIC,
                                         FLOW_TRANS_IRIS_APERTURE, 320U);

/* (F) 物理與渲染解耦步進：無論 24 / 30 / 60 / 120 / 240+ FPS，底層物理永遠維持 Bit-Identical */
uint32_t substeps = flow_game_step_fixed_accumulator(&engine, measured_frame_us, &accum_us, &alpha_q16);
```
