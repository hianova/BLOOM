# 10｜前端框架與遊戲／3D 引擎全域盤點暨 BLOOM 必要套件清單

> 本文件將主流前端 UI 框架與遊戲／3D 引擎拆解為正交子系統，逐項對照 **BLOOM vNext Milestone 1** 的已實作能力（[`bloom_abi.h`](../include/bloom_abi.h)、[`bloom_scene.h`](../include/bloom_scene.h)、[`bloom_gpu.h`](../include/bloom_gpu.h)、[`bloom_atlas.h`](../include/bloom_atlas.h)、[`bloom_host.h`](../include/bloom_host.h)、[`bloom_core.metal`](../shaders/bloom_core.metal)、[`bloom_import.py`](../tools/bloom_import.py)）與消費者邊界（[`ge_host_if.h`](file:///Users/kuangtalin/Desktop/Games/include/ge_host_if.h)），定義出一套**不重複造輪、不陷入 v9 Megashader 膨脹、嚴守 7 Hardware CI Gates** 的官方必要套件邊界與開發順序。

---

## 1. 全景盤點：主流前端框架與遊戲／3D 引擎的核心子系統

無論前端生態或遊戲引擎外觀語言如何差異，真正構成完整產品開發力的核心能力可拆解如下：

### 1.1 前端 UI 框架陣營（4 類・12 代表）

| 陣營 | 代表框架 / 生態 | 核心競爭力與必備子系統 |
| :--- | :--- | :--- |
| **Web 宣告式與反應式** | **React / Next.js**<br>**Vue 3 / Svelte 5 / SolidJS** | Component Tree 宣告式組合、VDOM Diff 或細粒度 Signal 響應圖、Context / Store 狀態傳遞、Nested Router / 頁面轉場、Portal / Overlay 分層、Fast Refresh 熱重載與狀態保留。 |
| **CSS 與元件設計系統** | **Tailwind CSS**<br>**Bootstrap**<br>**shadcn/ui + Radix UI** | 統一 Design Tokens（色彩、字級、圓角、間距、陰影階梯）、Dark/Light Theme 切換、完整 Flex + CSS Grid + Absolute/Sticky 定位、響應式斷點、`Button / Input / Select / Checkbox / Switch / Tabs / Dialog / Tooltip / Toast / Table / Tree` 完整狀態機（`:hover/:active/:focus/:disabled`）。 |
| **跨平台原生 UI** | **React Native** (`Yoga`, `Fabric`, `Reanimated`, `Gesture Handler`)<br>**Flutter** (`Impeller`, `Slivers`, `Paragraph`)<br>**SwiftUI / Compose Multiplatform** | Yoga C++ Flexbox 與 Intrinsic 文字量測、多點觸控與手勢辨識（Tap/Drag/Pinch/Pan）、原生 TextInput 與多語 IME 組字、高階圖片快取與向量圖示、`VirtualList` / `Sliver` 惰性捲動、平台轉場路由與共用元素動畫。 |
| **GPU 原生桌面與工具 UI** | **Zed GPUI** (`Taffy`, `Entity`, `NSTextInputClient`)<br>**Slint / Iced**<br>**Dear ImGui / egui** | Rust/C 零 DOM 原生 GPU 繪製、`Taffy` Layout、Entity/Model 雙向觀察、Keymap + Action Dispatch、FocusHandle 焦點與 Modal 攔截、`NSTextInputClient` 原生 CJK IME、Docking / Property Grid / Gizmo 除錯檢查器。 |

### 1.2 遊戲與 3D 引擎陣營（4 類・13 代表）

| 陣營 | 代表引擎 / 生態 | 核心競爭力與必備子系統 |
| :--- | :--- | :--- |
| **商業 AAA 通用引擎** | **Unreal Engine 5** (`Nanite`, `Lumen`, `Niagara`, `UMG`, `Sequencer`, `ControlRig`, `MetaSounds`)<br>**Unity** (`DOTS`, `URP/HDRP`, `UI Toolkit`, `Cinemachine`, `Addressables`)<br>**Godot 4** (`SceneTree`, `Forward+`, `AnimationTree`, `AudioServer`) | GPU Meshlet 裁剪、PBR 材質圖與預計算 IBL 光照、CSM 陰影與後處理（Tonemap / Bloom / MSAA）、Sequencer / Animator 狀態機融合與骨骼蒙皮 / Morph Targets、Cinemachine 相機軌、Niagara 粒子與空間音訊混音、非同步資產串流。 |
| **現代資料導向原生引擎** | **Bevy** (`Archetype ECS`, `RenderGraph`, `AssetServer`, `bevy_ui`, `Gizmos`) | RenderGraph 按需編排獨立 PSO Pass、`Handle<T>` / `AssetServer` 熱更新與參照計數、純資料分離邏輯與渲染提取（Extract）、場景 Gizmo 與檢查器。 |
| **Web 3D 與 React Three 生態** | **Three.js**<br>**React Three Fiber (`R3F`) + `drei` + `postprocessing`**<br>**Babylon.js / PlayCanvas / Spline** | 一行式宣告 `<Canvas>` + `<OrbitControls>` + `<Environment>` + `<EffectComposer>`、完整 `glTF 2.0 + KTX2/ASTC + Draco` 載入器、`MeshStandardMaterial`（Albedo/Normal/Roughness/Metallic/Emissive/AO）、Raycaster 3D 滑鼠點選、PMREM IBL、TransformControls 3D Gizmo。 |
| **底層跨平台圖形／遊戲層** | **wgpu / Dawn (WebGPU)**<br>**bgfx / sokol / SDL3 / raylib** | 單一 Shader ABI 同時跑 Metal / Vulkan / DX12 / WebGPU、跨平台開窗與手把/鍵鼠輸入抽象（不強迫包辦高層 UI/3D 規則）。 |

---

## 2. BLOOM vNext Milestone 1 現況對照與缺口盤點表

對照目標「**React Native + Three.js + 方便的物件與時序系統**（以及對接 [`ge_host_if.h`](file:///Users/kuangtalin/Desktop/Games/include/ge_host_if.h) 遊戲執行階段）」之實際狀態：

| 能力維度 | BLOOM vNext Milestone 1 已實作現況 | 關鍵缺口（相較完整 App + Game/3D 產品） |
| :--- | :--- | :--- |
| **二進位資產與 SSOT** | `BOBC` / `BOBR` 雙形態編解碼（[`bloom_bobj.h`](../include/bloom_bobj.h)）、128-bit Digest、[`bloom_import.py`](../tools/bloom_import.py) 基礎 `.glb+.svg -> .bobc/.bobr`。 | **無 KTX2/ASTC 貼圖與材質段、無骨骼/Morph 段**；`bloom_import.py` 目前只吃單一 mesh primitive，無完整 glTF 場景樹、無視錐最佳化分簇器、無字形預烘焙與 Font Fallback manifest。 |
| **2D 版面配置 (Layout)** | [`bloom_scene.c`](../src/bloom_scene.c) 基礎一維 `BLOOM_FLEX_COL/ROW`、`pad` / `gap`、面板標題預留高、`clip_id` 裁剪、1 萬列 `BLOOM_UI_VLIST` $O(\text{visible})$ 虛擬清單。 | **缺完整 Flexbox 特性**（`flex_wrap`, `justify_content`, `align_items`, `flex_grow/shrink`, `min/max`, `aspect_ratio`, 百分比 `%`, `position: absolute/fixed`）；**缺 CSS Grid**；**缺文字自動量測回推容器寬高**。 |
| **文字與多語 IME** | CoreText `R8Unorm` (`1024×1024`) `Private+Opt` 增量圖集（[`bloom_atlas_mac.m`](../src/bloom_atlas_mac.m)）、UTF-8 單字距排字、`BloomTextRun.text[128]` 固定上限。 | **缺多行折行、省略號截斷與對齊**；**固定 `SFMono`/`Menlo` 單字體（無系統/CJK Fallback 與粗斜體切換、無 Color Emoji `RGBA8` 圖集）**；**無 `NSTextInputClient` CJK 組字（Preedit）、游標選取、剪貼簿與編輯器 `TextInput`**；`128 B` 長文截斷。 |
| **UI 元件庫與樣式** | 僅 5 種基礎原語：`PANEL / BUTTON / SLIDER / TEXT / VLIST`（[`BloomUiKind`](../include/bloom_scene.h#L13-L19)），支援單向綁定物件 DOF / 剖面與 toggle。 | **缺現代應用元件**：TextInput/TextArea、Checkbox/Radio/Switch、Select/Dropdown、Tabs/Accordion、Modal/Drawer/Popover（Z 軸 Portal 遮罩）、ScrollView（雙向/慣性）、Table/Tree、Toast/Progress；**缺 Theme Design Tokens 與 `:hover/:focus/:disabled` 狀態樣式轉換**。 |
| **輸入、焦點與 Agent/A11y** | 指標壓下/放開/移動、滾輪、視窗 Resize、`BLOOM_EVENT_KEY_DOWN` 僅傳原始 `keyCode`（[`bloom_host_mac.mm`](../src/bloom_host_mac.mm#L106-L115)）；滑桿與面板反向 Z 命中測試。 | **缺 KeyUp、修飾鍵 (`Cmd/Ctrl/Shift/Opt`)、滑鼠 Cursor 形狀、Keymap Action Dispatch**；**缺 Roving TabIndex / Focus Trap / Hover 事件 / Drag & Drop / Multi-touch**；**缺手把 (`GameController`) 與 Agent-Native Semantic Snapshot**。 |
| **2D 媒體與向量圖示** | 純色/邊框圓角矩形（`BloomQuadHot`）、解析高斯陰影（`BloomShadowHot`）、點陣文字（`BloomGlyphInst`）。 | **無 2D 圖片／精靈紋理 (`BloomSpriteInst`) 與 `Private+Opt` `RGBA8`/`ASTC` 圖像快取**；**無路徑向量（Path / Curve / Icon Atlas）著色器或離線多密度 SVG 圖示集打包**。 |
| **物件管理與約束橋接** | Generational SlotMap `BloomHandle`、父子階層矩陣傳遞（[`bloom_scene.h`](../include/bloom_scene.h#L115-L129)）、`fnv1a64(id)` 熱重載保留 `StateStore`、Layer 0 `BloomStateProjection`（支援 DOF 夾限與 `q16` 轉換）。 | 僅固定 3 組 `dof[3]` 與示範級 `lp.y += sin(dof + lp.x) * 0.08f` 變形（[`bloom_core.metal`](../shaders/bloom_core.metal#L190-L192)）；**缺泛型 Constraint 表驅動（剛體關節、BlendShape 權重陣列）、標籤/Layer Bitmask 查詢與批次 Instancing 生成器**。 |
| **時序與動畫系統** | `BloomAnimTrack`（9 通道：`POS_X/Y/Z`, `ROT_Y`, `SCALE`, `DOF_0..2`, `CLIP_D`）、3 種緩動、`ONCE/REPEAT/PING_PONG`、Idle Zero-Submit。 | **缺完整四元數 (`ROT_XYZ` / SLERP)、不透明度/色彩軌、UI 屬性動畫（Spring UI layout transition）**；**缺動畫狀態機融合（Cross-fade）、關鍵幀曲線（Bezier）、序列事件回調 (Trigger Callbacks)**。 |
| **3D 材質、光照與後處理** | **Track A** `BloomVtx12`（Compute Cull + Indirect Draw + HW `[[clip_distance]]` 半剖）與 **Track B** `BloomAnalyticPrim32`（球體 / 膠囊體 $O(1)$ Ray Proxy）、單一硬編方向光 + Rim 光。 | **無 PBR Metallic-Roughness 材質 (`BloomMaterial32`)、無 3D 法線/反照率/粗糙度貼圖、無 IBL 環境球或點光/聚光光源陣列**；**無陰影圖 (CSM)、無 4x MSAA、無 ACES Tonemap / Selection Outline / Skybox**。 |
| **3D 互動與工具（Studio/Games）** | 固定鏡頭結構 [`BloomCamera`](../include/bloom_abi.h#L187-L194)（`pos, yaw, pitch, focal, near_z, far_z`）。 | **缺 Orbit/Fly/Follow 相機控制器（如對齊 `ge_host_camera_t` 的 `target + yaw/pitch/dist + trauma`）**；**缺 3D Raycast / GPU Object-ID 拾取、Transform Gizmo（平移/旋轉/縮放環）與內建 Debug Inspector UI**。 |
| **跨平台、音訊與粒子** | Apple M1 原生 Cocoa `NSWindow` + Metal 3 Single-CB（`4K 1.066 ms / 937.8 fps`、`maxTG=1024`、`390` 行 MSL）。 | **無 `bloom-gpu-wgpu` 與 Web(WASM)/Linux/Windows/iOS 宿主**；**無空間音訊消費器（對應 [`ge_host_audio_ev_t`](file:///Users/kuangtalin/Desktop/Games/include/ge_host_if.h#L114-L119)）**；**無 Compute+Indirect GPU 粒子系統**。 |

---

## 3. 四層架構歸屬與「必要套件清單」

為避免把核心引擎重新膨脹成 `v9`，以下嚴格切分 **A. 核心內核升級**、**B. 官方一等套件 (`packages/bloom-*`)**、**C. 離線工具鏈** 與 **D. 明確不納入的外部範疇**。

### Layer A — Core Kernel Upgrades（精簡核心內核升級，零外部依賴）

此層直接強化現有 `include/` + `src/` + `shaders/bloom_core.metal`，仍是唯一必裝基礎：

1. **`bloom-layout`（內核排版升級）**
   - 將 `bloom_scene.c` 現有一維 Flex 擴充為**完整 Flexbox（`wrap`, `justify`, `align`, `grow/shrink`, `min/max`, 百分比, `position: absolute`）+ 輕量 CSS Grid**，加入 **Text Measure Callback**（由 `bloom_atlas` 提供真實文字換行寬高計算）。
   - 保持 zero-malloc 預分配池紀律，計算時間鎖在 `< 100 µs`（數千節點）。
2. **`bloom-text` + `bloom-input`（多語文字、IME 與輸入事件完整化）**
   - `bloom_atlas` 支援動態字體鏈（系統 CJK Fallback、Bold/Italic、多行換行與省略號）、以及 **`RGBA8Unorm` 第二圖集專供 Color Emoji / 2D 圖示**。
   - `bloom_host_mac.mm` 實作 Cocoa **`NSTextInputClient`**，將中文/日文組字框位置（標定微調游標座標）與 Preedit 傳入 `BloomInputEvent`；補齊 **`KEY_UP`、Modifier 位元遮罩、滑鼠 Cursor 樣式、剪貼簿讀寫 API**，並取消 `BloomTextRun.text[128]` 上限（改用幀暫存字串區段 `offset + len`）。
3. **`bloom-media`（核心 2D 圖像／精靈圖元）**
   - 在 `bloom_abi.h` 新增緊湊 **`BloomSpriteInst` (`32 B`)** 與 `Refetch20B` 紋理採樣 Pass，專職顯示 2D 圖片、頭像與預烘焙圖示，讓所有 UI 與 2D HUD 元素零繞道直接走 TBDR。

---

### Layer B — Official First-Party Packages（`packages/bloom-*`，按需掛載的必要套件）

#### P0｜無此組件即無法交付真實 App / React Native 介面 / 3D Studio / 遊戲

| 套件名稱 | 對標來源 | 建議尺寸與邊界 | 核心功能清單 |
| :--- | :--- | :--- | :--- |
| **`bloom-ui-kit` + `bloom-theme`** | Bootstrap / shadcn / React Native | C17/C++ 純上層元件庫（100% 編譯為既有 `QuadHot` / `ShadowHot` / `GlyphInst` / `SpriteInst`，**零新增 Shader / 零 Compute**） | **12 項基礎元件**：`TextInput / TextArea`（含選取反白與閃爍游標）、`Checkbox / Switch / Radio`、`Select / Dropdown`、`Tabs`、`Modal / Popover`（Z 層 Portal 與背景阻斷）、`ScrollView`（慣性與捲軸條）、`Table / TreeView`、`Toast / Tooltip`、`Progress / Badge`；內建一套開箱即用 Dark/Light Design Tokens 與狀態樣式表（`hover/active/focus/disabled`）並兼容 [`ge_host_ui_node_t`](file:///Users/kuangtalin/Desktop/Games/include/ge_host_if.h#L105-L112)。 |
| **`bloom-pbr` + `bloom-tex`** | Three.js `MeshStandardMaterial` / Unity URP | **獨立專用 PSO + `Private+Opt` KTX2/ASTC Texture Array**（嚴守 `maxTG=1024`，與既有簡約 `f_obj_lit` 並存按需切換） | **`BloomMaterial32`**：Albedo / Normal / Metallic-Roughness / AO / Emissive 貼圖或純色常數、**預濾波 CubeMap/Octahedral IBL 環境光**、多光源清單（1 方向光 + $N$ 點光/聚光）。 |
| **`bloom-camera` + `bloom-pick`** | Three.js `OrbitControls` & `Raycaster` / Cinemachine | 純數學 + 輕量 GPU Object-ID Pass / BVH 查詢 | **四類相機控制器**：`Orbit`（阻尼縮放平移）、`FirstPerson / Fly`、`Follow / Spring-Arm`、`Trauma Screen-Shake`（直接對應 `ge_host_camera_t` 的 `target/yaw/pitch/dist/trauma`）；**3D 拾取**：CPU Bounding-Sphere 射線 + GPU `R32Uint` 視埠一點回讀精準命中 `BloomHandle`。 |
| **`bloom-gesture` + `bloom-a11y-agent`** | RN `Gesture Handler` / GPUI Keymap & Action / BLOOMA11y | Zero-alloc 狀態機 + JSON/Struct Snapshot | 完整手勢辨識（`Click/DoubleClick/LongPress/Drag/Pinch`）、宣告式 **`Keymap -> Action`** 分派、Roving `TabIndex` + `FocusTrap`、手把輸入轉譯，以及可供自動化測試與 AI Agent 檢查的 **UI/Scene 語意快照與指令注入 (`Snapshot/Query`)**。 |

#### P1｜達成完整 React Native + Three.js + 工作室生產力

| 套件名稱 | 對標來源 | 核心功能清單 |
| :--- | :--- | :--- |
| **`bloom-dsl` + `bloom-bindings` + `bloom-hotreload` + `bloom-router`** | React Fast Refresh / Signal / RN Navigation | **統一宣告與跨語言綁定層**：以現有 `fnv1a64(id)` `StateStore` 與 `.bobc/.bobr` 熱重載（`62.29 µs`）為底座，提供 Signal 細粒度節點 Diff、Navigation Stack（Push/Pop/Modal 路由與轉場）、檔案變更熱更新，以及給 **TypeScript (QuickJS/WASM) / Lean 4 / Python / C++** 使用的標準宣告式綁定層。 |
| **`bloom-shadow` + `bloom-postfx`** | `R3F postprocessing` / Godot Forward+ | 善用 Apple M1 TBDR 片上頻寬免費特性提供 **4x MSAA (`Memoryless`)**；按需開啟的**單向 Shadow Map Pass (Depth PCF)**、**ACES Filmic Tonemap**、**Selection Highlight / Edge Outline**（物件選取描邊）。 |
| **`bloom-anim` + `bloom-skinning`** | Unity `Animator` / glTF Skinning / Bevy Animation | 雙軌並存設計：機械與道具**保留 zero-skinning `48 B` Constraint-DOF 快車道**；當 `.bobc` 包含 `JointMatrices` 或 `MorphDeltas` 旗標時才載入獨立 `v_meshlet_skinned` PSO，提供骨骼融合（Cross-Fade）、Morph Targets 與動畫事件觸發器。 |
| **`bloom-asset-server`** | Bevy `AssetServer` / Unity `Addressables` | 背景執行緒解壓 `.bobc` + KTX2、透過 `MTLHeap` 配置與參照計數釋放未用幾何與貼圖、依相機距離切換 `Meshlet LOD` 層級。 |
| **`bloom-gizmo` + `bloom-devtools`** | `drei TransformControls` / ImGui / RenderDoc Lite | 開發期可掛載浮層：3D 平移/旋轉/縮放軸環互動 Gizmo、Grid/Bounding-Box 線框疊加、即時 Scene Graph / UI Tree / DrawCall / Metal Pass GPU 耗時面板。 |

#### P2｜跨平台擴展與完整遊戲表現力

| 套件名稱 | 對標來源 | 核心功能清單 |
| :--- | :--- | :--- |
| **`bloom-gpu-wgpu` + `bloom-host-{web,win,linux,ios}`** | `gpui_wgpu` / wgpu / Dawn | 依照已定案之 **選項 A**：共用 [`bloom_abi.h`](../include/bloom_abi.h)（純 32-bit 友善佈局），提供 WebGPU Storage Buffer 後端與 WASM Canvas (`CADisplayLink -> requestAnimationFrame`) / iOS / Win32 / Wayland 原生宿主 shim，絕不拖累 M1 Metal 主線。 |
| **`bloom-audio`** | `ge_host_audio_ev_t` / Godot `AudioServer` | 輕量空間音訊與混音器（macOS 使用原生 `AVAudioEngine`，Web 使用 WebAudio），**直接無縫消費 [`ge_host_audio_ev_t`](file:///Users/kuangtalin/Desktop/Games/include/ge_host_if.h#L114-L119) 內的 `kind`、`source_id`、`gain_q16` 與 `pan_q16`**（解決遊戲端音訊無處播放的斷點）。 |
| **`bloom-vfx`** | UE `Niagara` 輕量版 / Three.js `InstancedMesh` | GPU `Compute Update + Indirect Billboard/Ribbon Draw` 粒子系統（火焰、火花、拖尾軌跡、環境霧與地表 Decal）。 |

---

### Layer C — Build-Time Offline Compiler（`tools/bloom-bake` 升級清單）

保持「**執行期零 XML/JSON/glTF 解析稅**」的鐵律，現有 [`tools/bloom_import.py`](../tools/bloom_import.py) 必須擴充為正式建置期編譯器 **`bloom-bake`**（可維持 Python CLI + C 共享核心，或單一靜態編譯二進位）：

1. **完整 glTF 2.0 / GLB 管線**：多節點階層場景、多 Material (`BloomMaterial32`) 索引、多 Primitive 展開。
2. **真 Meshlet 圖元切分 + 視野外擴錐計算**：真正進行頂點區域聚集（`<= 64` vtx、`<= 96` tris）、生成頂點重用優化、LOD0–LOD2 簡化與精準 `bsphere_f16` + `cone_axis/cone_cutoff`，並**只產出封閉無破洞補丁**（已於 Milestone 1 `8x7` 修正驗證過其重要性）。
3. **貼圖與向量圖示烘焙**：將 `.png/.jpg/.exr` 轉碼並生成 Mipmaps 打包進 `.bobc`（iOS/macOS 編為 `ASTC`、跨平台 `BC7`/KTX2）；將常用 SVG Icon Set 預先點陣化為多密度（`1x/2x`）Spritesheet 或靜態 Path 圖表。
4. **字型與多語子集檢查**：在工具鏈階段檢查 UI/遊戲文案涵蓋字元與 Fallback 字型配置。

---

### Layer D — Explicit Non-Goals（明確不納入 BLOOM，保留給外部宿主／CPU `FLOW`）

以下能力無論前端或遊戲框架常把其包進單體引擎，但在 BLOOM 架構中**刻意切割在 Layer 0 之外**：

1. **物理求解器與剛體/碰撞演算**（Jolt / Rapier / Box2D / `FLOW` 物理約束）
   - BLOOM 不執行步進模擬；外部物理或 `FLOW` 約束求解器於 CPU 算完離散狀態後，透過 [`BloomStateProjection`](../include/bloom_abi.h#L175-L185) 單向投影到 `BLOOM`（僅利用 GPU/CPU 繪製碰撞框輔助 Debug）。
2. **遊戲規則、經濟數值與狀態機邏輯**（例如 `/Users/kuangtalin/Desktop/Games` 之 [`GameEcon.lean`](file:///Users/kuangtalin/Desktop/Games/GameEcon.lean)、[`GameAtoms.lean`](file:///Users/kuangtalin/Desktop/Games/GameAtoms.lean) 與 [`ge_host_if.h`](file:///Users/kuangtalin/Desktop/Games/include/ge_host_if.h)）
   - 嚴禁把業務規則、AI 尋路 (NavMesh)、回合狀態或經濟驗證塞進 BLOOM Shader 或 Scene 內核；兩者僅以穩定 C ABI 介面交接。
3. **網路通訊、HTTP / WebSocket / 資料庫與序列化商業狀態**
   - 由宿主應用程式語言（TypeScript / Lean / Rust / Swift / C++）自行管理，僅將「視圖狀態差異」餵給 `bloom_scene`。

---

## 4. 架構防線：新增套件絕不重蹈 `v9` Megashader 覆轍的 5 條設計紅線

1. **Pass 專精化、禁止單一 Megashader**：`bloom-pbr`、`bloom-skinning`、`bloom-shadow`、`bloom-vfx` 各自是獨立、不超過 `400` 行的 `.metal` 模組並各自通過 `maxTotalThreadsPerThreadgroup == 1024`（或對應複雜 shader 專用 Occupancy 上限），由 `Layer 2` Render Graph **僅在場景實際存在該元件時編排進 Single CommandBuffer**。基礎純色與 UI 永遠保持現有極輕量路徑。
2. **UI 元件庫零自訂像素著色器**：`bloom-ui-kit` 與 `bloom-theme` 的所有 12+ 元件與表單控制項必須 **100% 降階輸出為既有的 `BloomQuadHot` / `BloomShadowHot` / `BloomGlyphInst` / `BloomSpriteInst`**，絕不為了特殊控制項開新全螢幕 Compute Pass。
3. **`libbloom_core` + `libbloom_gpu` 維持零三方相依**：平台級字體 (`CoreText`)、IME (`NSTextInputClient`)、視窗 (`Cocoa`/`CAMetalLayer`)、音訊 (`AVAudioEngine`) 皆直接呼叫 OS 原生 API；高階烘焙 (`ktx`/`draco`) 隔離在 **build-time 離線工具鏈 `Layer C`**，絕不進入 Runtime 二進位。
4. **任何 3D/UI 互動套件嚴守 Idle Zero-Submit 契約**：Cursor 閃爍或 Slider 靜止時不可無腦重送 CB；只有 Dirty Flag 或活躍 Timeline 才喚醒提交，並且嚴格禁止讀取 `Shared|WriteCombined` Ring。
5. **每個 P0/P1 套件必須自帶 4K M1 Hardware CI 閘門**：新增任何 Render Pass 或資料結構（如 `BloomMaterial32`、`BloomSpriteInst`），都必須先在 `tests/` 實測 M1 4K GPU ms 與 SLC 流量（不得擠壓現有 `1.066 ms` 合成基準線與 `< 8 MB` 工作集）。

---

## 5. 建議分期路線與待拍板決策

依據上述盤點，BLOOM vNext Milestone 1 已把最難的「矽片物理底盤 + 雙形態 SSOT + 4K Single-CB TBDR + `1,042 fps` 複合基準」打穩；接下來的開發可依產品先後順序切為兩條清晰推展路線：

- **Milestone 2（兩線可並行）**：
  - **2A｜App / IDE Studio 就緒路線**：`Layer A` (`bloom-layout` Flex+TextMeasure + `NSTextInputClient` CJK IME + `RGBA8` Icon/Emoji Atlas + `BloomSpriteInst`) + **P0 `bloom-ui-kit` + `bloom-theme`** + **P0 `bloom-gesture` + `bloom-a11y-agent`**。
  - **2B｜Games / 3D Studio 就緒路線**：**P0 `bloom-pbr` + `bloom-tex`**（KTX2/IBL） + **P0 `bloom-camera` + `bloom-pick`**（無縫對接 `Games/include/ge_host_if.h` 與 glTF 實物） + 升級 `tools/bloom_import.py` 成完整 glTF Meshlet + KTX2 `bloom-bake`。
- **Milestone 3（完整 React Native + Three.js 生產力閉環）**：
  - `bloom-shadow` + `bloom-postfx`（4x MSAA Memoryless + Selection Outline + ACES）、`bloom-skinning` + 進階 `bloom-anim`、**`bloom-dsl` + `bloom-hotreload` + `bloom-router`**、`bloom-gizmo` + `bloom-devtools`、`bloom-audio`。
- **Milestone 4（跨平台同源發佈）**：
  - `bloom-gpu-wgpu` + `bloom-host-{web,ios,win,linux}`。

### 待確認的 3 個路線決策

1. **Milestone 2 第一優先順序**：要先打通 **2A（App／Studio UI 完備：`NSTextInputClient` CJK IME + TextInput/Select/Modal/ScrollView + Sprite Atlas）**，還是先打通 **2B（3D／Games 完備：PBR 貼圖/IBL + OrbitCamera/RayPick + 完整 glTF Meshlet Bake + `Games/ge_host_if.h` 串接）**？
2. **React Native 等價宣告層的介面語言 (`bloom-dsl`)**：高層宣告與狀態綁定你希望優先定在 **A. Web/TS 開發者熟悉的 TypeScript/JSX（或極薄 WASM 宿主）**、**B. 自研 SVG/BOBC-DSL + C API**，還是 **C. Lean 4 型別安全 DSL + C ABI**？
3. **骨骼動畫切線**：[`08_object_ssot_3d.md`](./08_object_ssot_3d.md) 證明了 `48 B` Constraint-DOF 速度極快，是否認同「機械／遊戲道具走 `48 B` 零蒙皮快車道；僅人形角色走按需載入的 `bloom-skinning` 獨立 PSO」此雙軌設計？
