# Hybrid q_search stage 1 analysis

## Implementation

- Added reusable hybrid search module: `src/q_search.c` / `src/q_search.h`
- Kept original minimax unchanged: `gungi_get_ai_move()` still uses the old evaluator.
- Updated comparison tool: `tools/compare_model_minimax.c`
- Updated build scripts: `build.bat` and `setup_macos.command`
- Default hybrid config:
  - mode: `hybrid`
  - model: `models/v_weights_100000.bin`
  - depth: 2
  - model scale: 30000
  - draw penalty: 0
  - repetition penalty: 0

Hybrid leaf score is black-positive:

```text
leaf = gungi_evaluate_board(state) + signed_model_value * model_scale
```

The model value is side-to-move positive, so white-to-move values are negated
before being added to the black-positive minimax score.

## Verification

- `build.bat`: passed
- `tests/test_rules.exe`: 383 checks passed
- `tools/compare_model_minimax.exe` smoke test: passed with no illegal moves

Build warning observed:

```text
src/main.c:1011:13: warning: 'RunAutoBenchmark' defined but not used
```

This warning is unrelated to hybrid search.

## Scale and penalty sweep

All rows use:

- model: `models/v_weights_100000.bin`
- depth: 2
- max ply: 1000
- fixed seed: 24680
- 5 games with hybrid as black, 5 games with hybrid as white

| Config | Hybrid wins | Original wins | Draws/timeouts | Log |
| --- | ---: | ---: | ---: | --- |
| scale 10000, draw 5000, repetition 1000 | 1 | 4 | 5 | `model_minimax_qsearch_hybrid_100000_depth2_1000ply_5x.csv` |
| scale 10000, no penalties | 0 | 1 | 9 | `model_minimax_qsearch_hybrid_nopenalty_100000_depth2_1000ply_5x.csv` |
| scale 5000, no penalties | 2 | 5 | 3 | `model_minimax_qsearch_hybrid_scale5000_100000_depth2_1000ply_5x.csv` |
| scale 20000, no penalties | 3 | 3 | 4 | `model_minimax_qsearch_hybrid_scale20000_100000_depth2_1000ply_5x.csv` |
| scale 30000, no penalties | 5 | 1 | 4 | `model_minimax_qsearch_hybrid_scale30000_100000_depth2_1000ply_5x.csv` |

The first anti-draw attempt was worse, so the current default keeps draw and
repetition penalties disabled. Scale 30000 was the best tested setting.

## Larger stage-1 comparison

Setting:

- hybrid config: scale 30000, no penalties
- original opponent: `gungi_get_ai_move(depth=2)`
- 20 games with hybrid as black
- 20 games with hybrid as white
- max ply: 1000
- fixed seed: 24680
- log: `model_minimax_qsearch_hybrid_scale30000_100000_depth2_1000ply_20x.csv`

| Metric | Value |
| --- | ---: |
| Hybrid wins | 11 |
| Original minimax wins | 8 |
| Draws/timeouts | 21 |
| Average ply | 463.55 |
| Min ply | 29 |
| Max ply | 1000 |
| Natural draws | 14 |
| Timeouts | 7 |

By side:

| Hybrid side | Hybrid wins | Original wins | Draws/timeouts |
| --- | ---: | ---: | ---: |
| Black | 8 | 1 | 11 |
| White | 3 | 7 | 10 |

## Conclusion

Stage 1 is implemented and shows a small positive result:

```text
hybrid scale 30000: 11 wins / 8 losses / 21 draws
```

This is better than the original minimax head-to-head result in this fixed-seed
run, but the edge is still small and the draw rate remains high.

Do not promote this as the default game AI yet. It is a usable teacher candidate
for the next experiment, but it still needs stronger anti-draw logic or a
student distillation pass before replacing the current AI.
