# BLOOM (v1.0.0)

**BLOOM**（原專案代號 `flow_game` 已正式棄用並遷移至本倉庫）是一套從零建構、100% 純 C17（`-std=c17 -Wall -Wextra -Werror -O2`）與 Apple Metal Compute / SDL3_GPU 的確定性（Deterministic）遊戲、跨平台模擬與宣告式物理液態玻璃 UI 引擎，底層建構於 `FLOW v4`（`libflow_rt.a`）與 `Ricardian v4`（`ricardian.h`）。

---

## 簡介 (Introduction)

BLOOM (`v1.0.0`) 旨在為高性能遊戲、跨平台掌機/桌機模擬與動態互動視覺提供具備形式化證明保證的純 C17 解決方案：

- **100% Pure C17 / Zero Float Drift**：全核心計算皆為純 C17（零 C++、零 `float`/`double` 漂移），全面採用 Q16.16 / Q48.16 定點數與 2-Jet 自動微分（`FlowJet2`），保證跨平台與跨網路連線 **Bit-for-Bit 100% 確定性**。
- **64-Byte Univalence 同構架構**：所有引擎狀態、物件工單、視聽描述子、桌布狀態與 GPU ABI 均嚴格對齊 64-byte Cache Line，並與 `RicardianStateV4` 雙射同構。
- **統一 64-Byte Engine Bus (1 KB)**：主機端與 GPU 管線透過 `FlowEngineBus1K`（16 個 64B 槽位）直接映射至 Metal / Vulkan `buffer(3)`，新增模組能力時無需修改底層渲染綁定。
- **4K 120 FPS 單通道 Metal Compute Raymarcher**：Apple M1 實測 4K（3840x2160）單通道 GPU Frame Time 中位數僅 ~6.19 ms，內建雙門檻遲滯調速器（`FlowFrameGovernor64`）動態維持 120 FPS 預算。
- **跨平台零資產動態桌布**：支援 macOS 原生背景動態桌布（Click-Through 事件穿透、游標 2-Jet 視差、全螢幕遮蔽 0 Hz 零喚醒休眠），並可一鍵匯出單一檔案 WebGL2 / HTML5 桌布與 128-Hex 數位簽章分享碼。
- **宣告式物理液態玻璃 SVG-UI、單通道 LaTeX SDF、跨平台 JSI 橋接與一鍵劇情殺/過渡動畫導演**：SDD 向量儀表與 24-bit TrueColor `fill="#RRGGBB"` 編譯、Beer-Lambert 光學透射率 + 2-Jet 水波焦散、單通道 DFA 零字型圖集 64B LaTeX SDF 公式盒（`FlowLatexBoxNode64`）、跨平台 JSI `FlowEngineBus1K` + Switch HD Rumble / Steam Deck 陀螺儀橋接（`FlowNativeBridgeVTable64`）、**一鍵劇情殺與無縫過渡動畫導演介面（`FlowNarrativeDirector64`）**，並支援 `IOSurface` 零拷貝共享記憶體直接對接 OBS Studio 直播。
- **零 GPU 無頭權威伺服器**：單核驅動 100,000 並發實體（`FlowTimingWheel64`，Tick Spread $\le 1$）與 64B Raw UDP 連線。

---

## 工具用法 (Tool Usage)

### 1. 環境需求與建置

本專案依賴 Clang C17 編譯器與 Apple Metal API，並會自動引用上層目錄之常駐工具：
- `FLOW` 執行期函式庫：`../FLOW/build/libflow_rt.a`
- `FLOWY` 形式化輔助工具：`../flowy/build/flowy`

#### 快速建置指令

```bash
# 編譯所有模組並執行 8 大主權品質閘門（Gate 0 FLOWY 跨層審計 + Gate 1..6 共 51 項單元測試 + Gate 7 4K Metal 基準測試）
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

編譯完成後，執行檔 `./build/bloom` 提供豐富的執行模式與參數（視窗內支援 `V` 鍵即時切換 VSync、`G` 鍵或 UI 點擊循環切換 `30 / 60 / 120 / Uncapped FPS`，底層相對論物理採用 Fixed-Tick 累加器與渲染幀率完全解耦）：

```bash
# 啟動即時互動式 Metal 視窗應用（預設視窗模式）
./build/bloom
# 或
make run

# 指定目標幀率與關閉 VSync（方便測試極限 Uncapped GPU 吞吐量或鎖定 ProMotion 120Hz）
./build/bloom --fps 120 --vsync 0

# 執行 4K (3840x2160) Metal 單通道基準測試（純 GPU/CPU 測速，略過硬碟寫入）
./build/bloom --bench
# 或
make bench

# 執行 4K 基準測試並將 4 鏡位與預覽畫面匯出為 4K PPM 分鏡圖（build/vcam_0..3_*.ppm）
./build/bloom --storyboard
# 或
make storyboard

# 啟動無頭權威伺服器模擬（6,000 ticks @ 60Hz，0 GPU / 0 MB VRAM，10 萬並發實體調度）
./build/bloom --headless
# 或
make headless

# 啟動 macOS 原生桌面背景動態桌布（Click-Through 事件穿透、游標 2-Jet 視差）
./build/bloom --wallpaper
# 或
make wallpaper

# 啟動桌面動態桌布並開啟背景 Q16 程序化音效合成
./build/bloom --wallpaper-audio

# 執行指定幀數的動態桌布測試（例如 300 幀後自動退出）
./build/bloom --wallpaper-frames 300

# 匯出單一檔案零資產 WebGL2 / HTML5 動態桌布（含 128-Hex 創意工坊數位簽章碼）
./build/bloom --wallpaper-export build/flow_wallpaper_web.html
```

---

### 3. 各測試子模組單獨驗證

除 `make test` 外，亦可針對各核心子系統獨立執行單元測試：

```bash
./build/test-bloom              # Gate 1：物理防穿隧、相對論度規、UDP 快照、QSBR 與調速器
./build/test-prefab-ssot        # Gate 2：64B CSG/SDF 預製件、時尚放樣、市場代幣與無障礙守衛
./build/test-procedural-assets  # Gate 3：零 Token 幾何基因組、SVG HUD、物件工單與多巴胺經濟
./build/test-cine-camera        # Gate 4：FlowCineRig64 虛擬光學攝影機、薄透鏡景深與 6D 震動
./build/test-wallpaper-engine   # Gate 5：動態桌布雙門檻遲滯休眠、電池 Eco 調速與 Web 匯出
./build/test-svg-ui             # Gate 6：宣告式 64B SVG-UI、物理玻璃、LaTeX SDF、跨平台 JSI 與劇情殺導演
```

---

## 遊戲製作速查表 (Game Authoring Cheat Sheet)

### 1. 即時互動快捷鍵與滑鼠操作總表 (`./build/bloom`)

| 按鍵 / 操作 | 所屬子系統 | 即時功能說明 |
| :--- | :--- | :--- |
| **`W` / `S` / `A` / `D`** | 相對論飛行物理 | 控制攔截機俯仰（Pitch）、偏航（Yaw）、滾轉（Roll）與推進節流閥 |
| **`Space` / `Tab`** | 相對論衝刺 / 武器 | `Space` 開啟 `3.0x` 極限推力；`Tab` 觸發武器脈衝與時間輪事件 |
| **`1` / `2` / `3` / `4` / `C`** | `FlowCineRig64` 攝影機 | 切換 4 組電影光學鏡位（`135mm` 望遠 / `24mm` 追尾 / `35mm` 掠過 / `18mm` 戰術座艙），`C` 循環切換 |
| **`F` / `R`** | 程序化塗裝與母種子 | `F` 循環切換 4 款高訂艦體塗裝（藍寶石 / 淬火藍寶 / 金繼大馬士革 / 帝王翡翠）；`R` 抽取新 64-bit 母種子 |
| **`T`** | 四維原子物件工單 | 循環切換 4 組跨流派 `FlowObjectTicket64` 預設集（恐怖手電筒 / 槓桿攀岩 / 戰術潛行 / 高速競技場） |
| **`X`** | 主權散熱守門員 | 觸發 `-120K` 熱能緊急洩壓（Thermal Purge ASMR 氣動噴流聲 + 6D 鏡頭震動） |
| **`J`** | 多巴胺開獎經濟 | 觸發 4 階段 `FlowDopamineJackpot64`（懸念暗角 $\to$ 頓幀 $\to$ 金光綻放 $\to$ 135mm 特寫） |
| **`K`** | **劇情殺 & 過渡動畫** | **一鍵觸發 `FlowNarrativeDirector64`（鎖操作 + 鎖血下限 + 2.39:1 黑邊 + 頓幀 + SDF X-Ray 破核 + 轉場遮罩）** |
| **`V` / `G`** | **動態 VSync & FPS** | **`V` 即時切換 `CAMetalLayer` VSync；`G` 循環切換 `60` $\to$ `120` $\to$ `Uncapped (240+)` $\to$ `30` FPS 調速預算** |
| **左鍵點擊 / 按住拖拽** | **SVG-UI & 跨磚塊 Scrub** | 點擊液態玻璃導播列按鈕，或按住 `X-Ray` 滑桿透過 **Pointer Capture** 跨 Wave64 磚塊連續調整剖面半徑 |
| **右鍵點擊 (`Right-Click`)** | **Prefab 非破壞性還原** | Blender 式取消模態拖拽 / Godot 式一鍵將 `FlowPrefabInstance64` 還原至唯讀 SSOT 預設值 |

---

### 2. C17 遊戲製作「一行呼叫」API 範例

```c
#include "bloom.h"

/* (A) 一行編譯 SVG 液態玻璃 UI 組件樹 (支援 fill="#RRGGBB" Beer-Lambert 透射光學) */
size_t n_ui = flow_svg_ui_compile_file("specs/cockpit_hud.svg", nodes, FLOW_SVG_UI_MAX_NODES, &dom);

/* (B) 一行編譯單通道 DFA Markdown/LaTeX 數學公式為零字型圖集 64B SDF 幾何盒 */
size_t n_tex = flow_latex_dfa_compile_boxes64("$\\gamma = \\frac{1}{\\sqrt{1 - \\beta^2}}$",
                                              -25559, -20971, 2048, 0xFFD830U,
                                              latex_boxes, 16U, NULL, NULL);

/* (C) 一行觸發「劇情殺 (Scripted Story Finisher)」：鎖操作 + 鎖血保底 + 頓幀 + X-Ray 破核 */
flow_director_trigger_story_finisher64(&director, 0x777U, FLOW_PREFAB_ORBITAL_CITADEL,
                                       FLOW_VCAM_ORBIT_TELEPHOTO, FLOW_TRANS_LIQUID_GLASS_WAVE, 1024);

/* (D) 一行觸發獨立「過渡動畫 (Scene Transition)」：100% 遮蔽中點 0-Pop-In 原子換場 */
flow_director_trigger_scene_transition64(&director, 3U, FLOW_STAGE_LAYER_1_RELATIVISTIC,
                                         FLOW_TRANS_IRIS_APERTURE, 320U);

/* (E) 物理與渲染解耦步進：無論 30 / 60 / 120 / 240+ FPS，底層物理永遠維持 60Hz Bit-Identical */
uint32_t substeps = flow_game_step_fixed_accumulator(&engine, measured_frame_us, &accum_us, &alpha_q16);

/* (F) 跨平台 React Native JSI / TurboModule 橋接 (macOS Metal / Steam Deck Vulkan / Switch NVN) */
flow_platform_bridge_init64(&plat, &vt, FLOW_PLATFORM_SWITCH_NVN, FLOW_OPMODE_HANDHELD_720P, 80U);
vt.poll_input(&plat, &input_intent, &dom);
vt.submit_bus1k(&plat, &engine_bus_1k);
vt.push_audio_haptics(&plat, audio_rms_q16, glass_ripple_jet_q16);
```
