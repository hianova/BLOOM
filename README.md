# BLOOM — 64-Byte 定點物理光學、空間場論與零座標語義 UI 基礎庫

**BLOOM**（`libbloom.a`）是一套以 100% 純 C17（`-std=c17 -Wall -Wextra -Werror -O2`）與跨平台 Single-Source GPU 算子（[`include/gpu/`](include/gpu)）建構的 **64-Byte 空間場論、本構物理光學（Constitutive Matter Optics）、程序化 SDF/CSG 幾何、零座標約束投影語義 UI 與物理攝影機基礎庫**。全程維持零浮點數（`FloatOps == 0`）、零動態記憶體配置（`HeapCalls == 0`）與零作業系統視窗耦合。

---

## 專案架構與五大核心模組

BLOOM 採用 **「算子與參數分離（Operator-Parameter Separation）」** 與 **「從物性與約束直接推導視覺（Physics & Constraint-to-Vision）」** 的設計模式：既不是綁定 2D 盒模型與系統 Widget 狀態樹的傳統 GUI 框架，也不是耦合特定商業邏輯的巨石型引擎，而是將空間幾何、連續介質光學與介面布局精煉為 **5 個純粹的 64-Byte 底層基礎設施模組**：

1. **64-Byte 物質本構光學與外部資產管線 ([`include/flow_assets.h`](include/flow_assets.h) / [`include/gpu/flow_matter_optics.h`](include/gpu/flow_matter_optics.h) / [`include/flow_gpu_abi.h`](include/flow_gpu_abi.h))**：
   - **物質本構狀態 (`FlowMatterState64`)**：將真空、流體、玻璃、玉石、液態金屬與高溫熔岩統一表達為單一 64-Byte 連續物性狀態向量（質量密度 $\rho$、複折射率 $\eta + i\kappa$、黏滯淬火度 $\mu_{\text{visc}}$、2-Jet 表面曲率起伏 $\|\nabla h, \nabla^2 h\|$、微表面粗糙度 $\sigma_r$、體積米氏散射 $\mu_s$、Beer-Lambert 光譜吸收係數 $\mu_a$、內部晶格密度 $\Lambda$、厚度 $d$ 與熱力學溫度 $T$），透過無分支算子 `flow_matter_eval_radiance_rgb_q16` 與 GPU 端 `eval_constitutive_matter_optics64` 直接推導視覺輻射，並支援 `128-Hex` 帶 MAC 驗證之跨行程序列化。
   - **原生 64-Byte 資產與空間加速結構**：支援 RIFF/WAVE (`.wav`) 載入與相對論都卜勒混音（`FlowWavAssetDesc64` / `FlowAudioVoice64`）、glTF Binary (`.glb`) 純整數解析與資料驅動 GPU CSG 布林指令串流（`FlowGlbMeshDesc64` / `FlowGpuCsgOp64`）、體積守恆之定點對偶四元數骨骼蒙皮（`FlowGpuJointDq64`）、60-bit Morton 無指標線性 BVH 空間加速樹（`FlowGpuBvhNode64`）、稀疏 $4 \times 4 \times 4$ SDF 體素磚（`FlowGpuSdfBrick64`），以及色域輸出閘門（`FlowStreamEgress64`：OKLab $\to$ Rec.709 / P3 / Rec.2020 / BT.709 YUV420p / Kitty Graphics Protocol APC）與 16 視角雲端同房 Tile-Atlas 共算微容器（`FlowCloudRoomDesc64`）。
2. **零座標約束投影 64-Byte 語義 UI 與單通道 LaTeX SDF ([`include/flow_svg_ui.h`](include/flow_svg_ui.h) / [`include/gpu/flow_svg_ui_pass.h`](include/gpu/flow_svg_ui_pass.h))**：
   - **主幹典範（Zero-XML / Zero-Coordinate Constraint-Projected UI）**：廢除手寫 HTML/SVG 標籤、手算像素座標與手動調配 Hex 色碼的前端負擔；透過 64-Byte `FlowUiIntentSpec64`（`flow_ui_intent_init64`、`flow_ui_synthesize_bento64`）或單行指令 `flow_ui_project_from_abi64(&abi_clause, &reg_bank, ...)`，直接由 `.lean` / `64B ABI` 的雙閾值遲滯區間 $[b_{\text{exit}}, b_{\text{enter}}]$ 自動推導光學警戒色態（Safe 冰藍毛玻璃 `0x20D8FF` $\to$ Hysteresis 琥珀金 `0xFFB020` $\to$ Trip 緋紅雷射 `0xFF2848`），並自動合成零重疊 Auto-Bento 響應式網格與 `Wave64` 空間碰撞遮罩。
   - **桌面互動、LaTeX SDF 與標準 SVG 相容**：內建無字型圖集之單通道 DFA LaTeX SDF 數學公式盒（`FlowLatexBoxNode64`）、`O(N)` 64-Byte Flexbox/Stack 容器排版（`FlowUiLayoutContainer64`）、12 通道通用狀態暫存器庫（`FlowUiRegisterBank64`）、64-Byte 內嵌文字輸入元件（`FlowUiTextInput64`）、系統字型圖集與 CJK 輸入法組字橋接（`FlowFontAtlasGlyph64` / `FlowImeComposition64`）、跨平台 HiDPI 視窗與原生對話框描述子（`FlowHostWindowDesc64` / `FlowNativeDialogDesc64`），並保留標準 SVG `viewBox` 匯入編譯器（`flow_svg_ui_compile_file`）作為相容層。
3. **64-Byte CSG/SDF 預製件 SSOT 與 TRC 無障礙光學防禦 ([`include/flow_prefab_ssot.h`](include/flow_prefab_ssot.h) / [`include/gpu/flow_sdf_ops.h`](include/gpu/flow_sdf_ops.h))**：
   - 以 64-Byte Cache-Line 對齊之 `FlowPrefabDesc64`、`FlowArtStyle64` 與 `FlowPrefabEditState64` 統一管理 3D CSG 結構、表面光學風格、非破壞性模態編輯（Revert to SSOT）與 SDF X-Ray 剖面視窗（`flow_prefab_eval_xray_cutaway_q16`），並內建 `800K / 760K` 熱失控雙門檻遲滯與 `<= 3.0 Hz` 光敏性癲癇防護（TRC Accessibility Guard）及色盲友善單調幾何頻率映射。
4. **4D 時空世界線、連續碰撞防穿隧與相對論場核心 ([`include/flow_game.h`](include/flow_game.h) / [`include/ricardian.h`](include/ricardian.h))**：
   - 結合二階 2-Jet 4D 拋物線世界線求根（Swept Analytical Worldline Intersection，根除高速移動穿隧 Tunneling）、光錐因果排序、雙門檻熱遲滯、Wave64 `1-Cycle` 群組壓縮與固定步長物理/渲染幀率解耦調速器（`flow_game_step_fixed_accumulator`）。
5. **零編譯期 `.lean` 64-Byte 規則橋接與物理攝影機光學 ([`include/flow_lean_abi.h`](include/flow_lean_abi.h) / [`include/flow_cine_camera.h`](include/flow_cine_camera.h))**：
   - **`.lean` 執行期規則載入器**：直接解析 `research/*.lean` 形式化規格並綁定為 64-Byte `FlowLeanClause64` 算子描述子（`flow_lean_abi_load_file`、`flow_lean_abi_step_clause64`）。
   - **64-Byte 物理攝影機與 SMPTE 分鏡 (`FlowCineRig64` / `FlowCinemaShot64`)**：實作二階 2-Jet 彈簧阻尼追焦、視線遮擋防抖動遲滯、薄透鏡散景光圈（Thin-Lens CoC）與 `24fps` SMPTE 時碼 / `48kHz` 鎖幀音訊預演管線。

---

## 建置與驗證 (Build & Verification)

```bash
# 編譯靜態函式庫 (build/libbloom.a) 並執行完整形式化稽核與全部單元測試
make test

# 清除所有建置產物
make clean
```

### 各測試閘門單獨執行

```bash
./build/test-bloom        # Gate 1：4D 世界線防穿隧、相對論度規、UDP 快照、QSBR 與調速器 (10 項測試)
./build/test-prefab-ssot  # Gate 2：64B CSG/SDF 預製件、800K/760K 熱遲滯、Wave64 與 TRC 無障礙防護 (7 項測試)
./build/test-flow-assets  # Gate 3：.lean ABI、WAV/GLB/SVG、對偶四元數骨骼、線性 BVH、SDF 磚、本構物性光學與雲端同房共算 (6 項測試)
./build/test-cine-camera  # Gate 4：FlowCineRig64 物理攝影機、薄透鏡景深、24fps EDL 與 48kHz 鎖幀 (7 項測試)
./build/test-svg-ui       # Gate 5：零座標約束投影 UI (Auto-Bento)、物理毛玻璃、單通道 LaTeX SDF、Flexbox 與 CJK IME (13 項測試)
```

---

## C17 核心 API 速查表 (Quick Reference)

```c
#include "bloom.h"

/* (A) [物性推導視覺 & 128-Hex 協議] 初始化 64B 物質本構狀態、推導輻射亮度並序列化為 128-Hex 封包 */
flow_matter_state64_init(&matter, 1U, 0, 0, 65536, 42598, 22938, 19660, absorb_rgb, 32768, 524288, 295U, 2500U);
flow_matter_eval_radiance_rgb_q16(&matter, grad_h, laplacian_h, bg_rgb, light_delta, out_rgb_q16);
flow_matter_state64_format_hex128(&matter, matter_hex128);
flow_gpu_bus1k_update_slot64(&bus1k, FLOW_BUS_SLOT_MATTER_STATE, &matter);

/* (B) [零 XML / 零座標 UI 投影] 直接由 64B Lean ABI 規則與暫存器自動合成 Auto-Bento 毛玻璃面板 */
size_t n_bento = flow_ui_project_from_abi64(&abi_clause, &reg_bank, 1920U, 1080U,
                                            intents, nodes, FLOW_SVG_UI_MAX_NODES, &dom);

/* (C) [單通道 LaTeX SDF] 解析數學公式為無字型圖集之 64B SDF 幾何盒 */
size_t n_tex = flow_latex_dfa_compile_boxes64("$\\gamma = \\frac{1}{\\sqrt{1 - \\beta^2}}$",
                                              -25559, -20971, 2048, 0xFFD830U,
                                              latex_boxes, 16U, NULL, NULL);

/* (D) [雲端同房多視角共算] 初始化 64B 房間隔離描述子、Tile-Atlas 視角路由與 <8.33ms SLA 調速器 */
flow_cloud_room64_init(&room, 42U, 64U, 10U, 960U, 540U, 8333U, 0xCAFEBABE12345678ULL);
uint32_t seat_idx = flow_cloud_room64_route_pixel_to_seat(&room, px, py, &ndc_x, &ndc_y);
uint32_t lod_step = flow_cloud_room64_step_sla_governor(&room, measured_gpu_us);
```
