# BLOOM Engineering Roadmap & Structural Audit (`todo.md`)

本文件記錄 `BLOOM` 邁向通用原生應用程式交付引擎（對標 Windows 11 / macOS 上以 Electron 交付之桌面與高互動應用）之結構重構進度、64-Byte ABI 規格與後續待辦事項。

---

## 已完成之結構清理與引擎升級 (Completed)

1. **移除虛假跨平台 Hash Stub 與展示型假測資**：
   - 移除未呼叫任何作業系統或 GPU API、僅計算記憶體 FNV-1a 雜湊的 `include/flow_platform_bridge.h` 與 `src/sdl3_gpu_host.c`。
   - 移除 `include/flow_svg_ui.h` 與 `src/svg_ui_core.c` 中硬編碼回傳常數的 `FlowIssueAuditReport64` / `flow_svg_ui_run_historical_issue_torture64`。
2. **升級 `flow_svg_ui` 為通用 64-Byte UI 排版與元件引擎 ([`include/flow_svg_ui.h`](include/flow_svg_ui.h), [`src/svg_ui_core.c`](src/svg_ui_core.c))**：
   - **解除 8 節點上限**：將 UI DOM Arena 容量 (`FLOW_SVG_UI_MAX_NODES`) 擴充至 `64U`（並保留 `FLOW_SVG_UI_GPU_BUS_SLOTS = 8U` 對應 `FlowEngineBus1K` 高速匯流排槽位）。
   - **標準 SVG 屬性自動正規化 (`normSvgViewBoxCoordQ16`)**：`flow_svg_ui_compile_file` 支援直接解析標準 SVG `<svg viewBox="...">`、`<rect x y width height rx>` 與 `<circle cx cy r>` 像素座標，自動以純整數運算 (`0 FloatOps`) 轉換為 Q16 視窗座標，無須手寫私有 `data-ox-q16` 屬性。
   - **64-Byte Flexbox / Stack 自動排版容器 (`FlowUiLayoutContainer64`)**：實作支援橫向 (`FLOW_UI_LAYOUT_ROW`) 與縱向 (`FLOW_UI_LAYOUT_COLUMN`)、對齊模式 (`START` / `CENTER` / `END` / `SPACE_BETWEEN` / `STRETCH`)、內距 (`padding_q16`)、間距 (`gap_q16`) 與捲動邊界夾擠 (`max_scroll_q16`) 之單趟 `O(N)` 排版器，並由 Lean 4 形式化證明單調不重疊性質 (`flex_stack_layout_monotonic_advance`)。
   - **64-Byte 通用狀態暫存器庫 (`FlowUiRegisterBank64`)**：提供 12 通道通用 Q16 狀態綁定 (`flow_svg_ui_bind_register_bank64`)，使一般桌面應用能綁定任意業務數據，不再耦合太空船專用參數 (`thermal_kelvin` / `craft_speed_q16`)。
   - **64-Byte 互動文字輸入元件 (`FlowUiTextInput64`)**：提供單一 Cache Line 內嵌 39-Byte UTF-8/ASCII 緩衝區、游標移動、插入與退格編輯狀態機 (`0 HeapCalls`)，並由 Lean 4 證明游標與容量不越界 (`ui_text_input_cursor_in_bounds`)。
3. **建立零編譯期 `.lean` $\leftrightarrow$ `FLOW` 64-Byte ABI 直接橋接層 ([`include/flow_lean_abi.h`](include/flow_lean_abi.h), [`src/lean_abi_bridge.c`](src/lean_abi_bridge.c), [`research/LeanFlowAbi.lean`](research/LeanFlowAbi.lean))**：
   - 執行期直接載入 `research/*.lean` 形式化規格檔（`flow_lean_abi_load_file`），提取 `def` 常數與定理雜湊並封裝為 64-byte `FlowLeanClause64` 算子描述子。
   - 將 [`src/wallpaper_core.c`](src/wallpaper_core.c)、[`src/procedural_assets.c`](src/procedural_assets.c)、[`src/engine_core.c`](src/engine_core.c) 改由 `flow_lean_abi.h` 直接驅動底層 `FLOW` 算子（`FlowAbiPoly1D_eval_hysteresis` + `flow_cubical_step_neighbor` Kan 同倫驗證、`flow_jet2_ema_step`、`flow_lean_abi_clamp_i64`、`flow_lean_abi_sub_floor_i64`、`flow_lean_abi_mac64`），消除 `.lean` 與 C 雙重實作斷層。
   - 移除與引擎無關之筆記測試檔（`research/NoteClaims.lean`、`research/SliceValidity.lean`、`research/ZeroOverheadScaling.lean`）。
4. **建立原生 64-Byte 外部資產匯入管線 ([`include/flow_assets.h`](include/flow_assets.h), [`src/flow_assets.c`](src/flow_assets.c), [`research/AssetPipeline.lean`](research/AssetPipeline.lean))**：
   - **RIFF/WAVE (`.wav`) 載入與多軌相對論都卜勒混音 (`FlowWavAssetDesc64` / `FlowAudioVoice64`)**：支援 8/16/24/32-bit PCM 與 IEEE-754 float32 音訊載入、立體聲相位平移與 Q16 步進重取樣混音（`flow_asset_mix_voices_q16`）。
   - **glTF 2.0 Binary (`.glb`) 3D 模型解析與 Q16 SDF 距離場代理 (`FlowGlbMeshDesc64`)**：透過純整數位移（`flow_ieee754_bits_to_q16`，`0 FloatOps`）解析 `.glb` 二進位頂點緩衝區、計算精確 3D AABB 與包圍球並提供 `flow_asset_eval_glb_sdf_q16` 距離場查詢。
   - **標準 W3C SVG `<path>` 圖示編譯 (`flow_asset_compile_svg_icon64`)**：直接解析外部美術工具匯出之 `<path d="M... L... H... V... Z"/>` 與 `viewBox` 為 64-byte GPU 圖示描述子（`FlowGpuGlyphDesc64`）。
5. **建置獨立靜態框架庫 (`build/libbloom.a`) 與全閘門驗證**：
   - 於 [`Makefile`](Makefile) 新增 `build/libbloom.a` 靜態封裝目標，供下游應用直接連結。
   - 全數 10 份 Lean 4 規格（`116 theorems, 97/97 defs covered, 0 sorryAx`）與 Gate 0–7 共 8 道驗證閘門 100% 通過。

---

## 待辦事項：邁向完整 Windows 11 / macOS 原生應用交付 (Open TODOs)

### TODO 1: 實作真實 Windows 11 (`Win32` + `Direct3D 12` / `Vulkan`) 與 Linux 原生宿主層
- **現況限制**：目前僅 [`src/metal_host.m`](src/metal_host.m) 具備真實的視窗、音訊與 GPU 管線（綁定 macOS `Cocoa` + `Metal` + `AudioToolbox`）。
- **64-Byte ABI 與實作規格**：
  - 於 `include/flow_host_abi.h` 定義 64-byte `FlowHostWindowDesc64`（包含視窗尺寸、Per-Monitor DPI Q16 縮放比、VSync 模式、Swapchain 幀索引與硬體句柄雜湊）。
  - 新增 `src/win32_d3d12_host.c`（或連結真實 `SDL3` 動態庫之 `src/sdl3_vulkan_host.c`），實作 Windows 11 `HWND` 建立、`WM_DPICHANGED` 響應、滑鼠/鍵盤事件映射至 `FlowEngineBus1K`，以及 WASAPI 48kHz 低延遲音訊佇列。
  - 利用 [`include/flow_shader_portability.h`](include/flow_shader_portability.h) 將 [`src/shaders.metal`](src/shaders.metal) 透過 HLSL / SPIR-V 編譯管線接入 Direct3D 12 / Vulkan Constant Buffer (`b3` / `binding = 3`)。
- **驗收條件**：
  - `flowy slice include/flow_host_abi.h --audit-all` 驗證 `FlowHostWindowDesc64` 為 `64B / align 64 / 0 padding`。
  - 於 Windows 11 / Vulkan 環境下維持 `FloatOps: 0` 模擬核心與 `FlowEngineBus1K` 1024-byte 二進位完全相容。

### TODO 2: 系統字型排版 (`CoreText` / `DirectWrite`) 與 CJK IME 輸入法合成橋接
- **現況限制**：目前 [`src/svg_ui_core.c`](src/svg_ui_core.c) 僅內建 7 段數碼管數字與幾何 SDF 數學符號 (`\frac`, `\sqrt`, `\gamma`)；`FlowUiTextInput64` 雖支援單行文字與游標編輯，但尚未接入作業系統 IME 組字視窗（如注音/拼音候選字）與系統 TrueType/OpenType 字型圖集。
- **64-Byte ABI 與實作規格**：
  - 定義 64-byte `FlowFontAtlasGlyph64`（Unicode Codepoint、圖集 UV Q16 邊界、Advance/Bearing Q16 度量與 Kerning 雜湊）與 64-byte `FlowImeComposition64`（Pre-edit 組字串、選取範圍 `[sel_start, sel_end]`、候選視窗螢幕錨點座標）。
  - 實作按需 (On-Demand) MSDF / 灰階字型圖集快取，於 macOS 呼叫 `CoreText` (`CTFontCreateWithName` / `CTFontDrawGlyphs`)、於 Windows 呼叫 `DirectWrite` (`IDWriteFontFace::GetGlyphRunOutline`) 進行 Unicode / CJK 字形光柵化。
  - 在宿主視窗層實作 `NSTextInputClient` (macOS) 與 `TSF / WM_IME_COMPOSITION` (Windows 11)，將組字狀態同步至 `FlowImeComposition64` 與 `FlowUiTextInput64`。
- **驗收條件**：
  - 新增 `research/FontImeSafety.lean` 形式化證明 UTF-8 解碼狀態機與組字游標合併永不越界（`0 sorryAx`）。

### TODO 3: 資料驅動 (Data-Driven) GPU CSG 指令緩衝區與 64+ 節點 Instanced UI Pass
- **現況限制**：[`src/shaders.metal`](src/shaders.metal) 中的 3D 場景幾何 (`eval_CitadelSDF` 等) 仍寫死於 Shader 函數內；且 `FlowEngineBus1K` 僅映射前 8 個 `FlowSvgUiNode64` 至高速常數槽位。
- **64-Byte ABI 與實作規格**：
  - 定義 64-byte `FlowGpuCsgOp64`（基元類型 Box/Sphere/Capsule/Torus/GlbProxy、布林運算 Union/Subtract/SmoothIntersect、Q16 局部變換與平滑半徑 $k$），將 [`include/flow_prefab_ssot.h`](include/flow_prefab_ssot.h) 的 `FlowPrefabDesc64` 與 [`include/flow_assets.h`](include/flow_assets.h) 的 `FlowGlbMeshDesc64` 編譯為平坦的 64-Byte CSG 指令陣列，透過 GPU `buffer(4)` 動態求值。
  - 為 64 節點以上的複雜桌面 UI 新增獨立的 Instanced Quad / Tile Culling GPU Buffer (`buffer(5)`)，使全畫面 64+ 個 `FlowSvgUiNode64`、`FlowGpuGlyphDesc64` 與 `FlowUiTextInput64` 皆能於單一 Render Pass 完成繪製。
- **驗收條件**：
  - 無須重新編譯 `shaders.metal`，即可由外部 `.glb` / Prefab 描述子動態增刪 3D 場景基元，且 4K 渲染維持 `< 4ms/frame`。

### TODO 4: 作業系統原生整合套件 (Native OS Capabilities & Single-Binary Bundler)
- **現況限制**：缺乏桌面應用程式必備之檔案對話框、系統剪貼簿、系統匣 (System Tray) 與單一指令打包工具。
- **64-Byte ABI 與實作規格**：
  - 定義 64-byte `FlowNativeDialogDesc64` 與 `FlowClipboardSlot64` 描述子，無阻塞封裝原生檔案選擇器 (`NSOpenPanel` / Windows `IFileOpenDialog`) 與系統剪貼簿 (`NSPasteboard` / Win32 Clipboard)。
  - 在 [`Makefile`](Makefile) 新增 `make bundle-macos`（自動封裝 `BLOOM.app/Contents/MacOS`、`Info.plist` 與 `.dmg` 映像檔）與 Windows MSIX 封裝腳本。
- **驗收條件**：
  - 透過 `make bundle-macos` 產出可獨立雙擊啟動且零外部動態庫依賴之 macOS 原生應用程式套件。

### TODO 5: 下游應用 (`Solar Walker`) 完整遷移至 `libbloom.a` 與 `flow_assets.h` 實體資產管線
- **現況限制**：[`../Solar Walker`](../Solar%20Walker) 雖已重構為以 `BLOOM` 驅動之太空巡航應用，但音訊與圖示仍沿用早期內建程序化合成與硬編碼 `cockpit_hud.svg` 路徑，尚未掛載外部 `.wav`、`.glb` 與美術匯出之標準 `.svg` 資產。
- **實作規格**：
  - 於 `Solar Walker` 連結 `../bloom/build/libbloom.a` 與 [`include/flow_assets.h`](include/flow_assets.h)。
  - 使用 `flow_asset_load_wav_file64` 與 `flow_asset_mix_voices_q16` 載入外部座艙環境音與推進器音效並套用相對論都卜勒頻移。
  - 使用 `flow_asset_load_glb_file64` 與 `flow_asset_compile_svg_icon64` 匯入外部船體 `.glb` 網格包圍盒與標準 W3C SVG `<path>` 儀表圖示。
- **驗收條件**：
  - `Solar Walker` 通過 `flowy slice` / `flowy lean` Gate 0 稽核，並支援由命令列或設定檔熱載入外部 `.wav` / `.glb` / `.svg` 資產。
