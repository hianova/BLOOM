# 歷史研究與基準測試記錄 (Historical Research Benchmarks & Notes)

> [!NOTE]
> 本文件記錄 BLOOM v1.0.0 之前的早期原型基準（Phase 0/2 概念驗證、Solar Walker 歷史對照）。現行正式基準請參見根目錄 `make bench` / `./build/bloom --bench`。

## Phase 0/2 結果 (Apple M1, 4K, GPU 計時, 中位數)

重現:`cd bench && clang++ -std=c++17 -O2 -fobjc-arc -framework Metal -framework Foundation bench.mm -o bench && ./bench K N overdraw`
(`bench2.mm` 同理,最後多一個 lights 參數)。

## bench:forward,離屏縮放 + 升頻 vs 單 pass 原生
| 負載 | A 傳統 100% | A 75% | A 50% | B 單 pass 原生 |
|---|---|---|---|---|
| K=4, od 4 | 1.64 | 1.11 | 0.69 | 1.32 |
| K=16, od 8 | 5.03 | 3.07 | 1.66 | 4.75 |
| K=64, od 4 | 18.3 | 10.7 | 5.0 | 17.4 |
| 空場景(K=0,N=1) | 0.72 | 0.75 | 0.81 | 0.80 |

## bench2:deferred,G-buffer 寫 DRAM vs tile 內 framebuffer fetch
| 負載 | D 多 pass | E 單 pass tile |
|---|---|---|
| K=0, od 1, 1 light(頻寬主導) | 1.41 | 0.76 |
| K=0, od 2, 1 light | 1.72 | 0.84 |
| K=4, od 4, 8 lights | 4.62 | 4.57 |
| K=8, od 8, 32 lights | 15.0 | 14.9 |

## 限制
- 合成負載(隨機 quad),不是真實場景。
- 單一裝置(M1),未量測功耗與 thermal。
- 未驗證 pixel-perfect(此處縮放用 bilinear 升頻)。

# 高速物理(physics/)
- `HighSpeedPhysics.lean`:核心 Lean 編譯無錯誤;1-D 穿隧門檻 `L ≥ (b-a)+2` 為精確條件;掃掠測試完備且健全;小邊際擦邊可被逐幀取樣漏掉;float32 在 1e9 m 解析度 64 m,在 <2048 m 時 <1 mm。
- `swept.c`:全整數(mm,int64/int128)的線段對球精確測試,`make test`:邊界、端點、1e9 m 尺度切線、20 萬筆隨機對照稠密取樣,全數通過,零誤判。範圍外輸入回傳 `SWEPT_ERR_RANGE`。
- `kepler_energy.c`(double):10⁴ 圈。e=0 時最大能量誤差 explicit Euler 0.98、symplectic Euler 4.0e-3、velocity Verlet 3.9e-6;e=0.9、每圈 100 步時三者都失效(Verlet 19 倍),須用閉式解或自適應步長。
- 專案實況(Solar Walker):船速 38–86 m/s,SolarDrifter 為自訂運動學(非 PhysicsServer),座標量級約 1.4 km;環小行星 720 個為 MultiMesh(僅視覺)。

# Solar Walker 月球場景(sw_profile/,M1 MacBook Air 8GB,電池供電,3840×2160 實際渲染)
- 場景:約 12.5 萬三角形、420 draw calls、612 節點。空場景 2.4 ms。
- **同一設定兩次獨立行程得到 11.53 ms 與 6.93 ms**(base 重複)。因此先前所有逐項關閉(ssao/glow/fog/shadow/sky/縮放)的差異無法歸因於設定,已作廢。
- 快速狀態下縮放 0.5、關特效皆為 6.9 ms:6.9 ms 為 CPU/排程下限,GPU 低於此值。
- 慢速狀態(11.5–11.8 ms)可持續 100 秒不漂移;成因未查明(候選:電池/GPU 時脈、WindowServer 與其他 GPU 使用者、視窗狀態)。
- 換 chunk 的最差幀 14–20 ms,無大型卡頓。
