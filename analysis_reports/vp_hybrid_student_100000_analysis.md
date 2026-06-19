# Gungi VP Hybrid Student 100000 正式訓練與評估報告

日期：2026-06-16
模型：`models/vp_hybrid_student_100000.bin`
訓練資料：`train_vp_hybrid_student_100000.samples.bin`
訓練目標：驗證線性 Value + Policy 雙頭模型是否能改善現有 value-only student hybrid 的穩定性、side skew 與 timeout 問題。

## 摘要

本次已完成原計畫的正式流程：

1. 產生 100,000 筆 teacher/self-play 樣本。
2. 使用 PyTorch/CUDA 訓練線性 value + policy 雙頭模型。
3. 匯出 `GUNGIP4` VP 模型。
4. 執行 A-E 評估矩陣。
5. 保留完整 CSV/log 供後續報告、圖表與失敗分析使用。

最終結論是：

```text
VP hybrid 在 loss rate 與黑方 side balance 上有改善，
但沒有通過整體驗收。

主要失敗原因是 win rate 低於 baseline，且 draw/timeout rate 過高。
```

1000-ply 三組正式 VP hybrid 評估合計 300 局：

```text
VP hybrid 1000-ply combined 300 games:
105 wins / 40 losses / 155 draws
timeouts: 133
black: 56-33-61
white: 49-7-94
```

對照目前 baseline：

```text
current student-hybrid 100x:
48 wins / 21 losses / 31 draws
black: 19-21-10
white: 29-0-21
```

VP hybrid 的 loss rate 較低，但 win rate 也較低，且 draw/timeout 明顯偏高。這代表第一版 policy head 尚未解決「長局、和局、timeout」問題。

## 產物索引

| 類型 | 檔案 | 說明 |
|---|---|---|
| VP 模型 | `models/vp_hybrid_student_100000.bin` | `GUNGIP4` value + policy 雙頭模型 |
| 訓練 dataset | `train_vp_hybrid_student_100000.samples.bin` | GPU trainer 讀取的 binary dataset |
| 樣本摘要 | `train_vp_hybrid_student_100000.csv` | 100,000 筆 position summary |
| 候選著資料 | `train_vp_hybrid_student_100000_candidates.csv` | 候選著與 teacher 標記 |
| 樣本產生 log | `train_vp_hybrid_student_100000.log` | teacher sample generation 設定與進度 |
| GPU metrics | `train_vp_hybrid_student_100000_gpu.csv` | 每 epoch loss |
| GPU log | `train_vp_hybrid_student_100000_gpu.log` | CUDA 裝置與訓練摘要 |
| A 評估 | `vp_policy_vs_minimax2_1000ply_20x.csv` | VP policy-only vs minimax2 |
| B 評估 | `vp_hybrid_vs_minimax2_1000ply_seed24680_100x.csv` | VP hybrid seed 24680 |
| C 評估 | `vp_hybrid_vs_minimax2_1000ply_seed13579_100x.csv` | VP hybrid seed 13579 |
| D 評估 | `vp_hybrid_vs_minimax2_1000ply_seed97531_100x.csv` | VP hybrid seed 97531 |
| E 評估 | `vp_hybrid_vs_minimax2_600ply_seed24680_40x.csv` | VP hybrid 600-ply check |

## 訓練方法

### 模型格式

本次新增 `GUNGIP4` VP model：

```text
value_weights[44]
policy_weights[47]
```

舊 `GUNGIV3` value-only 模型仍可載入。載入舊模型成 VP model 時：

```text
value weights = 舊模型權重
policy weights = 0
```

這確保 `models/v_weights_hybrid_student_50000.bin` 可作為 VP 初始化來源。

### Teacher 設定

```text
teacher model = models/v_weights_hybrid_student_50000.bin
teacher search = hybrid depth 2
model scale = 30000
target scale = 60000
seed = 515151
max ply = 1000
samples = 100000
max candidates per position = 64
```

每筆樣本包含：

- 44 維 value state features
- teacher move
- teacher black score
- side-to-move normalized value target
- 最多 64 個候選著的 47 維 policy features

### GPU 訓練設定

```text
PyTorch = 2.11.0+cu128
CUDA = true
GPU = NVIDIA GeForce RTX 5060 Ti
epochs = 10
batch size = 4096
value_lr = 0.0005
policy_lr = 0.002
margin = 0.05
```

訓練 loss：

- value loss：MSE
- policy loss：teacher move vs hard negative 的 pairwise ranking loss

## 訓練資料統計

100,000 筆樣本分布：

```text
positions = 100000
black-to-move = 50000
white-to-move = 50000
legal moves avg = 113.833
legal moves min = 5
legal moves max = 396
candidate moves avg = 57.48
candidate moves min = 5
candidate moves max = 64
teacher_black_score min = -6926
teacher_black_score max = 7833
teacher_black_score avg = 2913.047
value_target min = -0.130550
value_target max = 0.108217
value_target avg = -0.011158
```

樣本產生結果：

```text
done samples=100000 games=100 black_wins=0 white_wins=0 draws=100 timeouts=100
```

這是本次最重要的資料層觀察：100 局 teacher self-play 全部跑到 1000-ply timeout。也就是說，正式訓練資料本身高度偏向長局、未終局、timeout 局面。

這不代表訓練失敗，但會限制 policy head 能學到的內容。第一版 VP 主要學到 teacher 在長局局面中的排序偏好，未必能學到有效終局策略。

## GPU 訓練結果

GPU 訓練 10 epochs 完成，loss 穩定下降：

| epoch | value loss | policy loss | total loss |
|---:|---:|---:|---:|
| 1 | 0.0003138805 | 0.0503294762 | 0.0506433568 |
| 2 | 0.0003107036 | 0.0501843362 | 0.0504950399 |
| 3 | 0.0003076541 | 0.0500547075 | 0.0503623615 |
| 4 | 0.0003047260 | 0.0500156011 | 0.0503203271 |
| 5 | 0.0003019253 | 0.0499879141 | 0.0502898394 |
| 6 | 0.0002992270 | 0.0499291413 | 0.0502283681 |
| 7 | 0.0002966336 | 0.0499088678 | 0.0502055014 |
| 8 | 0.0002941409 | 0.0498596833 | 0.0501538239 |
| 9 | 0.0002917482 | 0.0498428583 | 0.0501346065 |
| 10 | 0.0002894459 | 0.0498067020 | 0.0500961482 |

解讀：

- value loss 低，主要因 value head 初始化自既有 value model，且 target 範圍不大。
- policy loss 有下降，但幅度有限。
- 因訓練資料全是 timeout 長局，policy head 沒有足夠終局勝負訊號。

## 評估結果

### A. VP policy-only vs minimax2

設定：

```text
mode = vp-policy
games per side = 10
max ply = 1000
seed = 24680
opponent = minimax2
```

結果：

```text
overall = 0 wins / 17 losses / 3 draws
timeouts = 2
black = 0-9-1
white = 0-8-2
avg ply = 203.3
```

解讀：

policy-only 完全不能作為獨立 AI。這符合預期，因為第一版 policy head 只做 teacher ranking，且訓練資料缺乏終局勝負訊號。policy head 目前較適合用於 hybrid search 的 move ordering。

### B. VP hybrid vs minimax2, seed 24680

```text
games = 100
overall = 33 wins / 13 losses / 54 draws
timeouts = 45
black = 18-11-21
white = 15-2-33
avg ply = 636.52
```

### C. VP hybrid vs minimax2, seed 13579

```text
games = 100
overall = 44 wins / 14 losses / 42 draws
timeouts = 36
black = 23-12-15
white = 21-2-27
avg ply = 518.53
```

### D. VP hybrid vs minimax2, seed 97531

```text
games = 100
overall = 28 wins / 13 losses / 59 draws
timeouts = 52
black = 15-10-25
white = 13-3-34
avg ply = 665.89
```

### B-D combined: 1000-ply formal 300 games

```text
games = 300
overall = 105 wins / 40 losses / 155 draws
timeouts = 133
black = 56-33-61
white = 49-7-94
avg ply = 606.98
```

Rates:

```text
win rate = 35.0%
loss rate = 13.3%
draw rate = 51.7%
timeout rate = 44.3%
```

### E. VP hybrid vs minimax2, 600-ply seed 24680

```text
games = 40
overall = 12 wins / 5 losses / 23 draws
timeouts = 22
black = 7-4-9
white = 5-1-14
avg ply = 432.90
```

Rates:

```text
win rate = 30.0%
loss rate = 12.5%
draw rate = 57.5%
timeout rate = 55.0%
```

## Acceptance Criteria 判定

| 項目 | 標準 | 結果 | 判定 |
|---|---|---:|---|
| 無非法著 | stderr empty / compare 完成 | stderr 全空 | 通過 |
| 無 NaN/Inf | trainer 與 VP load test | 通過 | 通過 |
| model reload | `test_vp_model.exe` | 通過 | 通過 |
| log / CSV 完整 | A-E 皆有 CSV/log | 完整 | 通過 |
| 1000-ply win rate | >= baseline 48% | 35.0% | 未通過 |
| 1000-ply loss rate | <= baseline 21% | 13.3% | 通過 |
| draw/timeout rate | <= 30% | draw 51.7%, timeout 44.3% | 未通過 |
| black side net | >= -5 over 150 games | 56 - 33 = +23 | 通過 |
| white side timeout farming | 不應過度依賴 timeout | white draws 94/150, timeouts 84/150 | 未通過 / 高風險 |
| 600-ply loss | 需低於 current student 600-ply baseline | 本次 12.5%，缺少對照 baseline | 無法完整判定 |
| 600-ply timeout | 不應增加 | 本次 timeout 55.0% | 高風險 |

總判定：

```text
第一版 VP hybrid 未通過正式 acceptance criteria。
```

## 與 baseline 對照

Baseline：

```text
current student-hybrid 100x:
48 wins / 21 losses / 31 draws
black: 19-21-10
white: 29-0-21
```

VP 300-game result：

```text
VP hybrid 300x:
105 wins / 40 losses / 155 draws
black: 56-33-61
white: 49-7-94
```

標準化成比例：

| 指標 | Baseline | VP hybrid |
|---|---:|---:|
| win rate | 48.0% | 35.0% |
| loss rate | 21.0% | 13.3% |
| draw rate | 31.0% | 51.7% |
| timeout rate | 未完全同欄位，但 baseline draw/timeout 31.0% | 44.3% timeout |

VP 的主要優點：

- loss rate 明顯低於 baseline。
- 黑方不再是明顯弱邊，black net 為 +23。
- white side 仍強，但不再呈現 baseline 的 white 29-0-21 那種完全不敗形態。

VP 的主要問題：

- win rate 下降。
- draw/timeout 大幅上升。
- white side 的大量和局與 timeout 仍可能代表 timeout farming。

## 技術分析

本次結果和訓練資料分布高度一致。teacher self-play 的 100 局全部 timeout，代表 student policy 幾乎沒有看到「如何有效結束對局」的 teacher signal。policy head 學到的是在長局中模仿 teacher 的局部排序，而不是學到勝負導向的終局偏好。

因此 VP hybrid 的行為變成：

```text
更穩、更少輸，
但更保守、更容易拖進 timeout。
```

這解釋了為什麼 loss rate 通過，但 win rate 與 draw/timeout rate 未通過。

policy-only 的 0-17-3 也支持這個判斷：目前 policy head 不能單獨下棋；它只能作為 hybrid search 的排序輔助。

## 重要限制

1. 訓練資料全部來自 teacher self-play timeout 長局。
2. 每個 position 最多保留 64 個 candidates，沒有全 legal move 訓練。
3. teacher search depth 只有 2，缺乏長期終局規劃能力。
4. 規則引擎目前沒有完整 checkmate search。
5. 600-ply 缺少 current student 對照檔，因此只能報告 VP 觀察值，不能完整判定「是否不增加」。

## 下一版建議

下一版不要只增加樣本數。100k 已證明資料分布本身有問題。建議改訓練資料來源與 teacher config：

1. 加入 late-game pressure 與 repetition penalty。
2. 產生 teacher vs minimax2，而不是純 teacher self-play。
3. 對 opening 加 epsilon noise，避免每 1000 samples 重複同類長局。
4. 加入 600-ply cutoff 的訓練資料，讓 policy 學會避免拖局。
5. 對 terminal / near-terminal 局面做 oversampling。
6. 在 policy target 中加入 timeout penalty，不只模仿 teacher move。
7. compare tool 應改成逐局 flush，方便長時間評估監控。

## 結論

本次 VP 第一版成功完成工程閉環：

```text
C teacher sample generation -> GPU training -> GUNGIP4 export -> C runtime reload -> A-E evaluation
```

但就正式棋力目標而言，第一版沒有通過 acceptance criteria。

最準確的結論是：

```text
VP hybrid 第一版降低了敗率並改善黑方 side balance，
但它把大量對局轉化為 draw/timeout，
尚未達成「更穩且降低 timeout」的核心目標。
```

因此本版適合作為方法驗證與後續改良基線，不建議切入正式遊戲 AI。
