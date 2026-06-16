# Gungi 抗和局 Side-Aware VP 實驗報告

日期：2026-06-16
目標：降低 VP hybrid 的和局與 timeout，同時保留或提升攻擊性，避免用高敗率換短局。

## 結論

本輪最佳版本是：

```text
vp_antidraw_side_v1black_a005white
```

設定檔：

```text
models/vp_antidraw_side_v1black_a005white.config.json
```

核心設計：

```text
value model        = models/vp_hybrid_student_100000.bin
black policy model = models/vp_hybrid_student_100000.bin
white policy model = models/vp_antidraw_blend_a005.bin
mode               = vp-side-hybrid
depth              = 2
```

這個版本不是單一 policy head，而是 side-aware move ordering：

- 黑方保留 V1 policy，避免 V2A/anti-draw policy 導致黑方敗局暴增。
- 白方使用 alpha=0.05 的 anti-draw blend policy，降低 V1 白方 timeout farming，提升白方攻擊性。
- leaf value 仍使用 V1 value head，最終決策仍由 hybrid alpha-beta 決定。

1000-ply 40 局檢查結果：

```text
22 wins / 5 losses / 13 draws
timeouts = 8 / 40
black = 9-4-7
white = 13-1-6
avg ply = 452.4
median ply = 314.5
```

和 V1 同 seed 600-ply baseline 相比：

```text
V1 same-seed 600-ply:
11 wins / 4 losses / 25 draws
timeouts = 23 / 40
black = 8-4-8
white = 3-0-17

Side-aware 600-ply:
19 wins / 4 losses / 17 draws
timeouts = 15 / 40
black = 8-4-8
white = 11-0-9
```

重點是：side-aware 600-ply 完整保住 V1 黑方的 `8-4-8`，同時把白方從 `3-0-17` 拉到 `11-0-9`。這表示它不是靠犧牲黑方穩定性換短局，而是針對白方拖局問題改善。

## 產物索引

新增或更新的程式：

```text
tools/blend_vp_policy.py
tools/summarize_compare.py
src/q_search.h
src/q_search.c
tools/compare_model_minimax.c
tests/test_vp_model.c
```

新增模型/設定：

```text
models/vp_antidraw_blend_a005.bin
models/vp_antidraw_blend_a010.bin
models/vp_antidraw_blend_a015.bin
models/vp_antidraw_blend_a020.bin
models/vp_antidraw_blend_a025.bin
models/vp_antidraw_blend_a035.bin
models/vp_antidraw_blend_a050.bin
models/vp_antidraw_blend_a075.bin
models/vp_antidraw_side_v1black_a005white.config.json
```

主要評估資料：

```text
vp_hybrid_vs_minimax2_600ply_seed626262_40x.csv
antidraw_blend_a005_hybrid_vs_minimax2_600ply_40x.csv
antidraw_blend_a010_hybrid_vs_minimax2_600ply_40x.csv
antidraw_blend_a015_hybrid_vs_minimax2_600ply_40x.csv
antidraw_blend_a025_hybrid_vs_minimax2_600ply_40x.csv
antidraw_blend_a050_hybrid_vs_minimax2_600ply_40x.csv
antidraw_blend_a075_hybrid_vs_minimax2_600ply_40x.csv
antidraw_side_v1black_a005white_hybrid_vs_minimax2_600ply_40x.csv
antidraw_side_v1black_a005white_hybrid_vs_minimax2_1000ply_40x.csv
```

所有對戰都有對應 `.log`、`.stdout.log`、`.stderr.log`。本輪主要 stderr 都是空檔，未觀察到 illegal move。

## 方法

第一階段先做純 policy blend：

```text
policy = (1 - alpha) * V1_policy + alpha * V2A_policy
value  = V1_value
```

測試過的 alpha：

```text
0.05, 0.10, 0.15, 0.25, 0.50, 0.75
```

純 blend 的結果顯示：

- anti-draw policy 確實能降低 timeout。
- 但它會系統性傷害黑方。
- 即使 alpha=0.05，黑方仍變成 `4-12-4`。

因此第二階段改為 side-aware：

```text
black to move: use V1 policy ordering
white to move: use alpha=0.05 anti-draw blend policy ordering
```

這個設計直接對應觀察到的問題：V1 黑方相對健康，V1 白方過度拖局；V2A/a005 白方攻擊性強，但黑方風險過高。

## 600-ply 結果

| 版本 | games | W-L-D | timeout | non-timeout draw | black | white | avg ply | median | p90 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| V1 same seed | 40 | 11-4-25 | 23 | 2 | 8-4-8 | 3-0-17 | 437.9 | 600.0 | 600 |
| V2A | 40 | 11-12-17 | 11 | 6 | 5-12-3 | 6-0-14 | 255.4 | 164.0 | 600 |
| blend a005 | 40 | 15-12-13 | 10 | 3 | 4-12-4 | 11-0-9 | 261.6 | 130.0 | 600 |
| blend a010 | 40 | 9-14-17 | 11 | 6 | 5-12-3 | 4-2-14 | 260.9 | 122.5 | 600 |
| blend a015 | 40 | 11-13-16 | 11 | 5 | 5-12-3 | 6-1-13 | 272.2 | 186.5 | 600 |
| blend a025 | 40 | 10-13-17 | 11 | 6 | 5-12-3 | 5-1-14 | 262.5 | 186.5 | 600 |
| blend a050 | 40 | 12-12-16 | 9 | 7 | 5-12-3 | 7-0-13 | 252.4 | 164.0 | 600 |
| blend a075 | 40 | 11-12-17 | 10 | 7 | 5-12-3 | 6-0-14 | 252.6 | 164.0 | 600 |
| side-aware V1 black/a005 white | 40 | 19-4-17 | 15 | 2 | 8-4-8 | 11-0-9 | 356.3 | 314.5 | 600 |

600-ply 判讀：

- 純 blend 能壓 timeout，但黑方敗率不可接受。
- a005 的白方很強，`11-0-9`，但黑方 `4-12-4` 失敗。
- side-aware 直接保留 V1 黑方 `8-4-8`，同時得到 a005 白方 `11-0-9`。
- side-aware 的 timeout 是 `15/40`，沒有達到理想的 `<=12/40`，但已從 V1 的 `23/40` 降到 `15/40`，且勝場從 11 提升到 19。

## 1000-ply 檢查

| 版本 | games | W-L-D | timeout | non-timeout draw | black | white | avg ply | median | p90 |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| side-aware V1 black/a005 white | 40 | 22-5-13 | 8 | 5 | 9-4-7 | 13-1-6 | 452.4 | 314.5 | 1000 |
| V2A 1000-ply | 40 | 13-12-15 | 7 | 8 | 5-12-3 | 8-0-12 | 349.9 | 164.0 | 1000 |

1000-ply 判讀：

- side-aware 把 600-ply 的部分 timeout 轉成實際勝局。
- timeout 降到 `8/40`。
- 勝場升到 `22/40`。
- 敗場只有 `5/40`，遠低於 V2A 的 `12/40`。
- 黑方 `9-4-7`，白方 `13-1-6`，side balance 可接受。

## 為什麼加長訓練時間不是主解

本輪實驗顯示，問題不是單純訓練不夠久：

- V1 的訓練資料本身來自大量 teacher timeout/draw，policy 容易學到保守長局。
- V2A 的 anti-draw 訊號有效，但方向對黑方有系統性副作用。
- 純 alpha blend 即使很小，也無法修掉黑方副作用。

因此，下一步有效方向不是盲目加 epoch，而是把訊號拆開：

```text
black stability signal
white anti-timeout / attacking signal
loss-risk guardrail
```

side-aware 版本就是第一個證據：同一個 anti-draw 訊號，限定用在白方後，效果明顯好於全局使用。

## 驗證

已執行：

```text
build.bat
tests/test_rules.exe
tests/test_vp_model.exe
```

結果：

```text
test_rules: 383 checks passed
test_vp_model: checks passed
```

`test_vp_model.exe` 已加入 side-aware hybrid 合法走法檢查。

stderr 檢查：

```text
antidraw_side_v1black_a005white_hybrid_vs_minimax2_600ply_40x.stderr.log  = empty
antidraw_side_v1black_a005white_hybrid_vs_minimax2_1000ply_40x.stderr.log = empty
```

## 下一版建議

建議 V3 不要再做單一 policy head 的純 blend，而是正式化 side-aware policy：

1. 擴充 VP model 格式，支援 `black_policy_weights` 與 `white_policy_weights`。
2. policy feature 加入 explicit side / phase 訊號，避免靠 runtime wrapper 分流。
3. V2B/V3 training label 拆成三個目標：
   ```text
   attack_score
   timeout_risk
   loss_risk
   ```
4. 訓練時對黑方加強 loss-risk penalty，避免重現 `5-12-3`。
5. 對白方加強 timeout-risk penalty 和 decisive-win bonus，延續本輪 `13-1-6` 的方向。
6. 正式採用前，建議再跑：
   ```text
   side-aware 1000-ply, 100 games, seed 24680
   side-aware 1000-ply, 100 games, seed 13579
   side-aware 1000-ply, 100 games, seed 97531
   ```

## 本輪採用判定

40 局檢查時可以採為「目前最佳研究候選」，但正式 300 局後，判定修正為：

```text
research-useful, not accepted
```

理由：

- 40 局小樣本顯示 side-aware 有效，尤其白方攻擊性提升。
- 正式 300 局顯示 timeout 有下降，但白方 loss 增加。
- draw 總數只小幅下降，未充分轉成 win。
- 因此不能取代 V1，只能作為下一版 side-aware + loss-risk 訓練的證據。

正式 300 局報告：

```text
analysis_reports/anti_draw_side_policy_formal_300.md
```

正式 300 局結果：

```text
Side-aware:
102 wins / 47 losses / 151 draws
timeouts = 115 / 300
black = 56-33-61
white = 46-14-90

V1 baseline:
105 wins / 40 losses / 155 draws
timeouts = 133 / 300
black = 56-33-61
white = 49-7-94
```

正式判定：

```text
timeout 有改善，但 wins 下降、losses 上升；正式採用未通過。
```
