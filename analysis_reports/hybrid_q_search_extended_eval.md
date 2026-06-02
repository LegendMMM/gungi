# Hybrid q_search extended evaluation

## Setup

- Hybrid implementation: `src/q_search.c`
- Comparison tool: `tools/compare_model_minimax.c`
- Model: `models/v_weights_100000.bin`
- Search depth: 2
- Hybrid config: `hybrid`, `model_scale=30000`, draw/repetition/late penalties disabled
- Opponent: original `gungi_get_ai_move(depth=2)`
- Per-game logs: `model_minimax_qsearch*.csv`
- Summary CSV: `model_minimax_qsearch_extended_summary.csv`

## Verification

- `build.bat`: passed
- `tests/test_rules.exe`: 383 checks passed
- Final smoke: `model_minimax_qsearch_final_smoke.csv`, no illegal moves

Build warning observed:

```text
src/main.c:1011:13: warning: 'RunAutoBenchmark' defined but not used
```

This warning is unrelated to q_search.

## 1000-ply results

| Run | Games | Hybrid wins | Original wins | Draws/timeouts | Natural draws | Timeouts | Avg ply |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| seed 24680 | 40 | 11 | 8 | 21 | 14 | 7 | 463.55 |
| seed 13579 | 20 | 8 | 2 | 10 | 6 | 4 | 521.70 |
| seed 97531 | 20 | 6 | 3 | 11 | 5 | 6 | 475.55 |
| combined | 80 | 25 | 13 | 42 | 25 | 17 | 481.09 |

Combined 1000-ply record:

```text
hybrid: 25 wins
original: 13 wins
draws/timeouts: 42
net: +12 wins for hybrid
draw/timeout rate: 52.5%
```

By side across the 80 1000-ply games:

| Hybrid side | Hybrid wins | Original wins | Draws/timeouts |
| --- | ---: | ---: | ---: |
| Black | 16 | 4 | 20 |
| White | 9 | 9 | 22 |

Hybrid is much stronger as black than as white. As white it only breaks even.

## 600-ply check

| Run | Games | Hybrid wins | Original wins | Draws/timeouts | Natural draws | Timeouts | Avg ply |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| seed 24680 | 20 | 3 | 8 | 9 | 6 | 3 | 311.95 |

The 600-ply run is negative for hybrid. This means the current advantage is not
robust under a shorter game cap. Hybrid appears to need longer lines to convert
its better positions.

## Interpretation

The extended 1000-ply data supports the direction:

```text
original evaluator + v_weights_100000 model at scale 30000
```

It performs better than original minimax depth 2 over 80 fixed-seed games. The
effect is meaningful but not dominant because more than half of games still end
as draw or timeout.

The main weakness is clear:

- high draw/timeout rate: 52.5% at 1000 ply
- weak white-side performance: 9 wins / 9 losses / 22 draws
- poor 600-ply result: 3 wins / 8 losses / 9 draws

## Recommendation

Do not promote this as the default AI yet.

Use this hybrid search as the next teacher candidate. The next experiment should
train a student model from hybrid search positions and evaluate whether the
student improves:

```text
1. Generate positions from hybrid-vs-original and self-play games.
2. Use hybrid search score / selected move as teacher data.
3. Train a new student value model.
4. Test student model-only and student-hybrid against original minimax depth 2.
```

Before promotion, the required acceptance bar should be higher:

```text
1000-ply: at least +20 net wins over 100 games
draw/timeout rate: below 45%
white side: positive or at least non-negative with fewer draws
600-ply: not negative
```
