# Gungi VP Resolution V2A 最小實驗報告

日期：2026-06-16
模型：`models/vp_resolution_v2a_10000.bin`
實驗定位：最小 V2A resolution 訊號實驗，不是正式替代模型。

## 摘要

本次依照「先跑最小實驗版本」執行 V2A：

```text
10,000 positions
8 candidates per position
40-ply random rollout
GPU training 8 epochs
600-ply 40-game evaluation
1000-ply 40-game evaluation
```

V2A 沒有新增大型網路，也沒有新增完整 VPR 格式；它採用最小可跑版本：保留 V1 的 value head，將現有 VP policy head 改訓練成 resolution-aware move ordering head。

核心結果：

```text
V2A 明顯降低 timeout，但犧牲勝負穩定性，尤其黑方敗率惡化。
```

與 V1 600-ply 40 局對照：

```text
V1 600-ply:
12 wins / 5 losses / 23 draws / 22 timeouts

V2A 600-ply:
11 wins / 12 losses / 17 draws / 11 timeouts
```

V2A 的 timeout 從 22/40 降到 11/40，但 loss 從 5/40 升到 12/40。這表示 resolution 訊號確實改變了行為，讓對局更常提前分出結果；但目前它沒有分清楚「快速贏」和「快速輸」，因此還不能採用。

## 新增實作

本次新增：

- `tools/generate_resolution_dataset.c`
- `tools/train_resolution_policy.py`
- `models/vp_resolution_v2a_10000.bin`
- `resolution_v2a_10000.csv`
- `resolution_v2a_10000_gpu.csv`
- `resolution_v2a_hybrid_vs_minimax2_600ply_40x.csv`
- `resolution_v2a_hybrid_vs_minimax2_1000ply_40x.csv`

V2A runtime 沿用既有 `GUNGIP4` VP model 與 VP hybrid search，因此可以直接用 `compare_model_minimax.exe` 評估。

## 資料產生設定

```text
samples = 10000
teacher = models\v_weights_hybrid_student_50000.bin
teacher_depth = 2
max_ply = 1000
seed = 626262
max_candidates = 8
horizon = 40
policy_features = 47
```

每個 candidate 的 resolution target 由兩部分組成：

```text
resolution_target = 0.55 * rollout_score + 0.45 * direct_score
```

direct score 包含：

- gives check
- capture value
- material delta
- opponent legal move pressure
- repetition penalty
- late-game penalty

rollout score 來自 40-ply random rollout：

- 快速贏：高分
- 快速輸：低分
- draw / timeout：負分

## Dataset 統計

```text
positions = 10000
candidate rows = 79890
target min = -0.56
target max = 0.147625
target avg = -0.163852
rollout timeout rate = 99.9987%
positive targets (> 0.05) = 113
negative targets (< -0.05) = 67061
```

重要觀察：

```text
40-ply random rollout 幾乎全部 timeout。
```

這表示 V2A 的 label 主要仍由 direct score 決定，而不是 rollout outcome。換句話說，這次雖然形式上加入 rollout，但 horizon 40 + random rollout 對 Gungi 仍太弱，無法有效產生大量勝負訊號。

## GPU 訓練結果

```text
rows = 79890
pairs = 9309
device = cuda
GPU = NVIDIA GeForce RTX 5060 Ti
epochs = 8
batch size = 4096
lr = 0.01
```

loss：

| epoch | regression loss | pairwise loss | total loss |
|---:|---:|---:|---:|
| 1 | 0.01380530 | 0.03225269 | 0.01799575 |
| 2 | 0.00815009 | 0.00567975 | 0.00900994 |
| 3 | 0.00656143 | 0.00631997 | 0.00749575 |
| 4 | 0.00664104 | 0.00652901 | 0.00759314 |
| 5 | 0.00645095 | 0.00518412 | 0.00724029 |
| 6 | 0.00669239 | 0.00599858 | 0.00757486 |
| 7 | 0.00635041 | 0.00463358 | 0.00704815 |
| 8 | 0.00647904 | 0.00532250 | 0.00724158 |

訓練本身穩定，沒有 NaN/Inf，模型可匯出為 `GUNGIP4`。

## 評估結果

### V2A 600-ply, 40 games

```text
overall = 11 wins / 12 losses / 17 draws
timeouts = 11
black = 5-12-3
white = 6-0-14
avg ply = 255.425
```

對照 V1 600-ply：

```text
V1 = 12 wins / 5 losses / 23 draws / 22 timeouts
V2A = 11 wins / 12 losses / 17 draws / 11 timeouts
```

V2A 讓 timeout 減半，但多出 7 場敗局。

### V2A 1000-ply, 40 games

```text
overall = 13 wins / 12 losses / 15 draws
timeouts = 7
black = 5-12-3
white = 8-0-12
avg ply = 349.875
```

1000-ply 下 timeout 只有 7/40，顯著低於 V1 正式 300-game 的 44.3% timeout rate。但黑方 5-12-3 很差，代表 resolution policy 讓黑方更容易快速輸。

## 技術解讀

V2A 成功驗證一件事：

```text
resolution-aware policy 可以顯著改變對局長度。
```

V1 的問題是過度保守，拖進 timeout。V2A 則相反：它讓對局更快進入有結果狀態，但沒有足夠保護 loss rate。

目前問題不是「resolution 訊號沒用」，而是訊號太粗：

1. 40-ply random rollout 幾乎全部 timeout，提供不了可靠勝負 label。
2. direct score 太偏向 tactical pressure，可能鼓勵冒險。
3. policy head 只做 move ordering，leaf value 還是 V1 value，因此搜尋沒有真正理解 resolution 的風險。
4. 黑方失敗集中，代表 V2A 對 side balance 造成副作用。

## 結論

V2A 最小實驗達成目標：早期驗證 resolution 方向是否有訊號。

結論：

```text
有訊號，但不能直接採用。
```

具體判定：

- timeout 下降：通過
- loss rate 控制：未通過
- 黑方 side balance：未通過
- 工程管線：通過

V2A 比 V1 更「有決斷」，但也更容易輸。下一版應該保留 resolution 方向，但不能只用 policy head 學粗糙 target。

## 下一步建議

V2B 不應直接擴大樣本，而應先改 label：

1. rollout policy 從 random 改成 `minimax1` 或 `hybrid depth1`。
2. target 拆成三個 label：
   - `win_within_horizon`
   - `loss_within_horizon`
   - `timeout_or_repetition_risk`
3. 搜尋加上風險懲罰：

```text
score = value + board_eval + resolution_win_bonus - resolution_loss_penalty - timeout_risk_penalty
```

4. 加入 side-aware calibration，特別修黑方 5-12-3 的問題。
5. 優先跑 600-ply 40-game gate：

```text
timeout <= 12/40
losses <= 6/40
black net >= -2
```

只有 V2B 通過這個 gate，才值得跑 100k 或 300-game 正式評估。
