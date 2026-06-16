# Gungi Side-Aware Anti-Draw Formal 300-Game Evaluation

日期：2026-06-16
模型設定：`models/vp_antidraw_side_v1black_a005white.config.json`
模式：`vp-side-hybrid`
對手：`minimax2`
搜尋深度：2
max ply：1000
正式 seed：24680、13579、97531
規模：每個 seed 50 games per side，共 300 games

## 結論

Side-aware 版本在正式 300 局中**有降低 timeout**，但**未達正式取代 V1 的標準**。

核心結果：

```text
Side-aware formal 300:
102 wins / 47 losses / 151 draws
timeouts = 115 / 300
black = 56-33-61
white = 46-14-90
avg ply = 543.43
```

對照 V1 formal 300：

```text
V1 formal 300:
105 wins / 40 losses / 155 draws
timeouts = 133 / 300
black = 56-33-61
white = 49-7-94
avg ply = 606.98
```

正式判讀：

- timeout：`133 -> 115`，下降 18 局，有明確改善。
- draw：`155 -> 151`，只下降 4 局，改善幅度不足。
- wins：`105 -> 102`，小幅下降。
- losses：`40 -> 47`，增加 7 局，不可忽略。
- black：完全維持 `56-33-61`，side-aware 黑方保護有效。
- white：從 `49-7-94` 變成 `46-14-90`，白方 timeout 降低但敗場增加。

因此本輪結論是：

```text
side-aware anti-draw 是有效方向，但 a005 white policy 還太冒進；
它降低 timeout，卻把部分白方 timeout 轉成敗局，正式採用未通過。
```

## Formal Seed Results

| seed | games | W-L-D | timeout | non-timeout draw | black | white | avg ply | median | p90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 13579 | 100 | 39-15-46 | 32 | 14 | 23-12-15 | 16-3-31 | 481.8 | 307.0 | 1000 |
| 24680 | 100 | 33-16-51 | 40 | 11 | 18-11-21 | 15-5-30 | 575.3 | 634.0 | 1000 |
| 97531 | 100 | 30-16-54 | 43 | 11 | 15-10-25 | 15-6-29 | 573.2 | 520.0 | 1000 |

## Baseline V1 Formal Results

| seed | games | W-L-D | timeout | non-timeout draw | black | white | avg ply | median | p90 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 13579 | 100 | 44-14-42 | 36 | 6 | 23-12-15 | 21-2-27 | 518.5 | 380.0 | 1000 |
| 24680 | 100 | 33-13-54 | 45 | 9 | 18-11-21 | 15-2-33 | 636.5 | 868.5 | 1000 |
| 97531 | 100 | 28-13-59 | 52 | 7 | 15-10-25 | 13-3-34 | 665.9 | 1000.0 | 1000 |

## What Worked

### 1. 黑方保護成功

Side-aware formal 300 的黑方結果：

```text
black = 56-33-61
```

V1 formal 300 的黑方結果也是：

```text
black = 56-33-61
```

這證明「黑方用 V1 policy」這個設計有效，沒有重現純 blend / V2A 的黑方崩盤。

### 2. Timeout 有下降

```text
V1 timeout         = 133 / 300
Side-aware timeout = 115 / 300
```

下降 18 局，表示白方 anti-draw policy 確實能減少拖到 max ply 的局面。

### 3. 平均 ply 降低

```text
V1 avg ply         = 606.98
Side-aware avg ply = 543.43
```

平均對局長度下降約 63.55 ply，表示對局更容易進入決勝或非 timeout 結果。

## What Failed

### 1. 白方敗場增加

```text
V1 white       = 49-7-94
Side-aware white = 46-14-90
```

白方 loss 從 7 增加到 14。這是正式採用失敗的主要原因。

### 2. Draw 下降太少

```text
V1 draws       = 155 / 300
Side-aware draws = 151 / 300
```

雖然 timeout 下降，但總和局只少 4 局。也就是有些 timeout 被轉成非 timeout draw 或 loss，沒有充分轉成 win。

### 3. 攻擊性沒有穩定提升

```text
V1 wins       = 105 / 300
Side-aware wins = 102 / 300
```

小樣本 40 局的 `22W/5L/13D` 很好，但正式 300 局回歸後，勝場反而略低於 V1。

## Acceptance Decision

本版不通過正式採用。

```text
status = research-useful, not accepted
```

理由：

- timeout 明確下降，方向有效。
- 黑方 side-aware 保護有效。
- 但白方 loss 增加，且 wins 沒有維持。
- draw rate 只小幅下降，不足以宣稱已解決和局問題。

## 下一版方向

下一版不要直接把 a005 white policy 接上，而要做「白方 anti-draw 加風險閥」：

1. 保留 side-aware 架構。
2. 黑方繼續使用 V1 policy 或更保守 policy。
3. 白方 policy 不能只追求縮短局面，必須加入 loss-risk penalty。
4. 訓練 target 分拆：
   ```text
   attack_score
   timeout_risk
   loss_risk
   ```
5. 白方排序分數建議改成：
   ```text
   score = attack_score - timeout_risk - 2.0 * loss_risk
   ```
6. 評估門檻改成正式 300 局：
   ```text
   wins >= 105
   losses <= 40
   draws < 151
   timeouts < 115
   white losses <= 8
   black result >= 56-33-61
   ```

## Artifacts

Formal CSV/log:

```text
antidraw_side_v1black_a005white_formal_1000ply_seed13579_100x.csv
antidraw_side_v1black_a005white_formal_1000ply_seed13579_100x.log
antidraw_side_v1black_a005white_formal_1000ply_seed13579_100x.stderr.log

antidraw_side_v1black_a005white_formal_1000ply_seed24680_100x.csv
antidraw_side_v1black_a005white_formal_1000ply_seed24680_100x.log
antidraw_side_v1black_a005white_formal_1000ply_seed24680_100x.stderr.log

antidraw_side_v1black_a005white_formal_1000ply_seed97531_100x.csv
antidraw_side_v1black_a005white_formal_1000ply_seed97531_100x.log
antidraw_side_v1black_a005white_formal_1000ply_seed97531_100x.stderr.log
```

stderr:

```text
all formal stderr logs are empty
```
